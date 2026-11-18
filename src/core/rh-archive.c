#include "rh-archive.h"

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

#include "rh-path.h"

#define RH_ARCHIVE_BLOCK_SIZE (64 * 1024)
#define RH_ARCHIVE_TMP_SUFFIX ".rhpart-XXXXXX"

/* ------------------------------------------------------------------ */
/* 打开与错误映射                                                      */
/* ------------------------------------------------------------------ */

static gboolean contains_ascii_ci(const char *haystack, const char *needle)
{
    gchar *lower = g_ascii_strdown(haystack, -1);
    gboolean found = g_strstr_len(lower, -1, needle) != NULL;
    g_free(lower);
    return found;
}

static gboolean error_says_encrypted(struct archive *a)
{
    const char *msg = archive_error_string(a);
    if (msg == NULL)
        return FALSE;
    return contains_ascii_ci(msg, "passphrase")
        || contains_ascii_ci(msg, "password")
        || contains_ascii_ci(msg, "encrypted");
}

static RhErr map_read_error(struct archive *a)
{
    if (archive_errno(a) == EPERM || error_says_encrypted(a))
        return RH_ERR_ENCRYPTED;
    if (archive_format(a) == 0)
        return RH_ERR_UNSUPPORTED;
    return RH_ERR_IO;
}

static struct archive *reader_open(const char *archive_path, RhErr *out_err)
{
    struct archive *a = archive_read_new();
    if (a == NULL) {
        *out_err = RH_ERR_NOMEM;
        return NULL;
    }

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    int rc;
#ifdef _WIN32
    /* libarchive 的窄路径入口在 Windows 上走 ANSI，中文路径会打不开；
     * 统一转宽字符后使用 _w 版本。 */
    wchar_t *wide = rh_path_to_wide(archive_path);
    if (wide == NULL) {
        archive_read_free(a);
        *out_err = RH_ERR_INVALID;
        return NULL;
    }
    rc = archive_read_open_filename_w(a, wide, RH_ARCHIVE_BLOCK_SIZE);
    g_free(wide);
#else
    rc = archive_read_open_filename(a, archive_path, RH_ARCHIVE_BLOCK_SIZE);
#endif

    if (rc != ARCHIVE_OK) {
        int saved_errno = archive_errno(a);
        if (saved_errno == EPERM || error_says_encrypted(a))
            *out_err = RH_ERR_ENCRYPTED;
        else if (saved_errno == ENOENT || saved_errno == EACCES)
            *out_err = RH_ERR_IO;
        else if (archive_format(a) == 0)
            *out_err = RH_ERR_UNSUPPORTED;
        else
            *out_err = RH_ERR_IO;
        archive_read_free(a);
        return NULL;
    }
    return a;
}

/* ------------------------------------------------------------------ */
/* 条目路径规范化                                                      */
/* ------------------------------------------------------------------ */

typedef enum {
    RH_ENTRY_PATH_OK,
    RH_ENTRY_PATH_SKIP,
    RH_ENTRY_PATH_UNSAFE
} RhEntryPathState;

/* libarchive 的 pathname 在 zip 未标 UTF-8 时可能是 CP437；优先取它已转好的
 * UTF-8 版本，退回原字节再尝试一次 CP437→UTF-8 转换。 */
static gchar *entry_name_to_utf8(const char *raw)
{
    if (raw == NULL)
        return NULL;
    if (g_utf8_validate(raw, -1, NULL))
        return g_strdup(raw);
    return g_convert(raw, -1, "UTF-8", "CP437", NULL, NULL, NULL);
}

/*
 * 返回 OK 时 *out 为新分配的规范路径（'/' 分隔、无 "."/".."、无空段）；
 * SKIP 表示该条目没有可用路径（空名、".", 无效 UTF-8），应忽略；
 * UNSAFE 表示绝对路径或含 ".."，调用方必须拒绝整个压缩包。
 */
static RhEntryPathState normalize_entry_path(const char *raw, gchar **out)
{
    *out = NULL;
    if (raw == NULL || raw[0] == '\0')
        return RH_ENTRY_PATH_SKIP;

    gchar *utf8 = entry_name_to_utf8(raw);
    if (utf8 == NULL)
        return RH_ENTRY_PATH_SKIP;

    /* 存档里可能混入 Windows 反斜杠；内部统一 '/'，也避免绕过 ".." 检查。 */
    for (gchar *p = utf8; *p != '\0'; p++) {
        if (*p == '\\')
            *p = '/';
    }

    if (utf8[0] == '/' || (g_ascii_isalpha(utf8[0]) && utf8[1] == ':')) {
        g_free(utf8);
        return RH_ENTRY_PATH_UNSAFE;
    }

    gchar **parts = g_strsplit(utf8, "/", -1);
    g_free(utf8);

    GString *joined = g_string_new(NULL);
    RhEntryPathState state = RH_ENTRY_PATH_SKIP;

    for (guint i = 0; parts[i] != NULL; i++) {
        const gchar *part = parts[i];
        if (part[0] == '\0' || g_strcmp0(part, ".") == 0)
            continue;
        /* ".." 越界；":" 在 Windows 上是盘符/备用数据流分隔符，跨端一律拒绝 */
        if (g_strcmp0(part, "..") == 0 || strchr(part, ':') != NULL) {
            state = RH_ENTRY_PATH_UNSAFE;
            break;
        }
        if (state == RH_ENTRY_PATH_OK)
            g_string_append_c(joined, '/');
        g_string_append(joined, part);
        state = RH_ENTRY_PATH_OK;
    }

    if (state == RH_ENTRY_PATH_OK)
        *out = g_string_free(joined, FALSE);
    else
        g_string_free(joined, TRUE);

    g_strfreev(parts);
    return state;
}

static gchar *entry_raw_pathname(struct archive_entry *entry)
{
    const char *p = archive_entry_pathname_utf8(entry);
    if (p == NULL)
        p = archive_entry_pathname(entry);
    return g_strdup(p);
}

static gboolean entry_is_dir(struct archive_entry *entry, const char *raw)
{
    if (archive_entry_filetype(entry) == AE_IFDIR)
        return TRUE;
    if (raw != NULL) {
        gsize len = strlen(raw);
        if (len > 0 && (raw[len - 1] == '/' || raw[len - 1] == '\\'))
            return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* 列表                                                                */
/* ------------------------------------------------------------------ */

static void archive_entry_item_free(RhArchiveEntry *entry)
{
    if (entry == NULL)
        return;
    g_free(entry->pathname);
    g_free(entry);
}

void rh_archive_list_free(RhArchiveList *list)
{
    if (list == NULL)
        return;
    g_ptr_array_free(list->entries, TRUE);
    g_free(list);
}

RhErr rh_archive_list(const char *archive_path, RhArchiveList **out_list)
{
    if (archive_path == NULL || out_list == NULL || !rh_path_is_utf8(archive_path))
        return RH_ERR_INVALID;
    *out_list = NULL;

    RhErr err = RH_OK;
    struct archive *a = reader_open(archive_path, &err);
    if (a == NULL)
        return err;

    RhArchiveList *list = g_new0(RhArchiveList, 1);
    list->entries = g_ptr_array_new_with_free_func(
        (GDestroyNotify)archive_entry_item_free);

    struct archive_entry *entry = NULL;
    int rc;
    while ((rc = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        if (archive_entry_is_encrypted(entry) == 1)
            list->encrypted = TRUE;

        gchar *raw = entry_raw_pathname(entry);
        gchar *pathname = NULL;
        RhEntryPathState state = normalize_entry_path(raw, &pathname);

        if (state == RH_ENTRY_PATH_UNSAFE) {
            g_free(raw);
            rh_archive_list_free(list);
            archive_read_free(a);
            return RH_ERR_INVALID;
        }
        if (state == RH_ENTRY_PATH_SKIP) {
            g_free(raw);
            archive_read_data_skip(a);
            continue;
        }

        RhArchiveEntry *item = g_new0(RhArchiveEntry, 1);
        item->pathname = pathname;
        item->is_dir = entry_is_dir(entry, raw);
        item->is_encrypted = archive_entry_is_encrypted(entry) == 1;
        if (!item->is_dir && archive_entry_size_is_set(entry)
            && archive_entry_size(entry) > 0)
            item->size = (guint64)archive_entry_size(entry);
        else
            item->size = 0;
        g_ptr_array_add(list->entries, item);

        g_free(raw);
        archive_read_data_skip(a);
    }

    if (rc != ARCHIVE_EOF) {
        err = map_read_error(a);
        rh_archive_list_free(list);
        archive_read_free(a);
        return err;
    }

    /* 部分格式（如 7z）只有读完全部头之后才能回答整包是否加密 */
    if (archive_read_has_encrypted_entries(a) == 1)
        list->encrypted = TRUE;

    archive_read_free(a);
    *out_list = list;
    return RH_OK;
}

/* ------------------------------------------------------------------ */
/* 加密探测                                                            */
/* ------------------------------------------------------------------ */

RhErr rh_archive_is_encrypted(const char *archive_path, gboolean *out_encrypted)
{
    if (archive_path == NULL || out_encrypted == NULL
        || !rh_path_is_utf8(archive_path))
        return RH_ERR_INVALID;
    *out_encrypted = FALSE;

    RhErr err = RH_OK;
    struct archive *a = reader_open(archive_path, &err);
    if (a == NULL)
        return err;

    gboolean encrypted = FALSE;
    struct archive_entry *entry = NULL;
    int rc;
    while ((rc = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        if (archive_entry_is_encrypted(entry) == 1)
            encrypted = TRUE;
        archive_read_data_skip(a);
    }

    if (rc != ARCHIVE_EOF) {
        err = map_read_error(a);
        archive_read_free(a);
        return err;
    }

    if (archive_read_has_encrypted_entries(a) == 1)
        encrypted = TRUE;

    archive_read_free(a);
    *out_encrypted = encrypted;
    return RH_OK;
}

/* ------------------------------------------------------------------ */
/* 解压                                                                */
/* ------------------------------------------------------------------ */

static gchar *join_target_path(const char *dir, const char *name)
{
    gsize len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/')
        return g_strconcat(dir, name, NULL);
    return g_strconcat(dir, "/", name, NULL);
}

/* 先写同目录临时文件再改名：任何一步失败都不会留下半个目标文件。 */
static RhErr extract_regular_file(struct archive *a, const char *dest)
{
    gchar *parent = g_path_get_dirname(dest);
    if (g_mkdir_with_parents(parent, 0755) != 0) {
        g_free(parent);
        return RH_ERR_IO;
    }
    g_free(parent);

    gchar *tmp = g_strconcat(dest, RH_ARCHIVE_TMP_SUFFIX, NULL);
    int fd = g_mkstemp(tmp);
    if (fd < 0) {
        g_free(tmp);
        return RH_ERR_IO;
    }

    int rc = archive_read_data_into_fd(a, fd);
    int saved_errno = archive_errno(a);

    GError *close_err = NULL;
    gboolean closed = g_close(fd, &close_err);
    if (close_err != NULL)
        g_error_free(close_err);

    RhErr err = RH_OK;
    if (rc != ARCHIVE_OK)
        err = saved_errno == EPERM ? RH_ERR_ENCRYPTED : RH_ERR_IO;
    else if (!closed)
        err = RH_ERR_IO;
    else if (g_rename(tmp, dest) != 0)
        err = RH_ERR_IO;

    if (err == RH_OK)
        g_chmod(dest, 0644);
    else
        g_unlink(tmp);

    g_free(tmp);
    return err;
}

RhErr rh_archive_extract(const char *archive_path,
                         const RhArchiveExtractOptions *options,
                         RhArchiveExtractStats *out_stats)
{
    RhArchiveExtractStats stats = { 0 };
    if (out_stats != NULL)
        *out_stats = stats;

    if (archive_path == NULL || options == NULL || options->target_dir == NULL
        || options->target_dir[0] == '\0'
        || !rh_path_is_utf8(archive_path)
        || !rh_path_is_utf8(options->target_dir)
        || !rh_path_is_utf8(options->entry))
        return RH_ERR_INVALID;

    gchar *wanted = NULL;
    if (options->entry != NULL) {
        RhEntryPathState st = normalize_entry_path(options->entry, &wanted);
        if (st != RH_ENTRY_PATH_OK)
            return RH_ERR_INVALID;
    }

    if (g_mkdir_with_parents(options->target_dir, 0755) != 0) {
        g_free(wanted);
        return RH_ERR_IO;
    }

    RhErr err = RH_OK;
    struct archive *a = reader_open(archive_path, &err);
    if (a == NULL) {
        g_free(wanted);
        return err;
    }

    RhErr result = RH_OK;
    gboolean found = wanted == NULL;
    gboolean stopped = FALSE;
    struct archive_entry *entry = NULL;
    int rc;

    while ((rc = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        gchar *raw = entry_raw_pathname(entry);
        gboolean raw_is_dir = entry_is_dir(entry, raw);
        gchar *pathname = NULL;
        RhEntryPathState state = normalize_entry_path(raw, &pathname);
        g_free(raw);

        if (state == RH_ENTRY_PATH_UNSAFE) {
            result = RH_ERR_INVALID;
            break;
        }
        if (state == RH_ENTRY_PATH_SKIP) {
            stats.skipped++;
            archive_read_data_skip(a);
            continue;
        }

        if (wanted != NULL && g_strcmp0(pathname, wanted) != 0) {
            g_free(pathname);
            archive_read_data_skip(a);
            continue;
        }
        found = TRUE;

        if (archive_entry_is_encrypted(entry) == 1) {
            g_free(pathname);
            result = RH_ERR_ENCRYPTED;
            break;
        }

        gchar *dest = join_target_path(options->target_dir, pathname);
        g_free(pathname);

        if (raw_is_dir) {
            if (g_mkdir_with_parents(dest, 0755) != 0)
                result = RH_ERR_IO;
            else
                stats.dirs++;
        } else if (archive_entry_filetype(entry) == AE_IFREG) {
            result = extract_regular_file(a, dest);
            if (result == RH_OK)
                stats.files++;
        } else {
            /* 符号链接/硬链接/设备节点：ROM 库用不到，跳过且不跟随 */
            stats.skipped++;
            archive_read_data_skip(a);
        }
        g_free(dest);

        if (result != RH_OK)
            break;

        if (wanted != NULL) {
            stopped = TRUE; /* 单条目模式，目标已处理，不再读后续头 */
            break;
        }
    }

    if (!stopped && result == RH_OK && rc != ARCHIVE_EOF)
        result = map_read_error(a);

    if (result == RH_OK && !found)
        result = RH_ERR_NOT_FOUND;

    archive_read_free(a);
    g_free(wanted);

    if (out_stats != NULL)
        *out_stats = stats;
    return result;
}
