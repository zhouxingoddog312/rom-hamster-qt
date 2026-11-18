#include "rh-archive.h"

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

#include "rh-path.h"

#define RH_ARCHIVE_BLOCK_SIZE (64 * 1024)
#define RH_ARCHIVE_TMP_SUFFIX ".rhpart-XXXXXX"
#define RH_ARCHIVE_DOMAIN "romham/archive"

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

/* libarchive 对未标 UTF-8 的 zip 返回条目名原始字节（可能是 GBK）。交给
 * rh-fname 判定：合法 UTF-8 原样使用，否则按 GB18030 推测，再不行逐字节转义。
 * 返回的 fname 保留原始字节，供入库与后续还原。 */
static RhEntryPathState normalize_entry_path(const char *raw, gchar **out,
                                             RhFName **out_fname)
{
    *out = NULL;
    if (out_fname != NULL)
        *out_fname = NULL;
    if (raw == NULL || raw[0] == '\0')
        return RH_ENTRY_PATH_SKIP;

    RhFName *fname = rh_fname_from_bytes(raw, -1);
    if (fname == NULL)
        return RH_ENTRY_PATH_SKIP;

    gchar *utf8 = g_strdup(fname->display);

    /* 解码后再替换分隔符：GBK 的尾字节可能等于 '\\'，先切分会切坏字符。
     * UTF-8 多字节序列里不会出现 0x5C，转义表示里也没有裸 '\\'，因此这里只
     * 会命中真正的 Windows 路径分隔符。 */
    for (gchar *p = utf8; *p != '\0'; p++) {
        if (*p == '\\')
            *p = '/';
    }

    if (utf8[0] == '/' || (g_ascii_isalpha(utf8[0]) && utf8[1] == ':')) {
        g_free(utf8);
        rh_fname_free(fname);
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

    if (state == RH_ENTRY_PATH_OK) {
        *out = g_string_free(joined, FALSE);
        if (out_fname != NULL)
            *out_fname = fname;
        else
            rh_fname_free(fname);
    } else {
        g_string_free(joined, TRUE);
        rh_fname_free(fname);
    }

    g_strfreev(parts);
    return state;
}

/* 优先取 pathname()：它给出条目里的原始字节（未标 UTF-8 的 zip 就是 GBK）。
 * pathname_utf8() 会把无标记条目按 CP437 转成 UTF-8（D3 -> "Ó"），反而丢失
 * 真实字节，只在 pathname() 不可用时兜底。 */
static gchar *entry_raw_pathname(struct archive_entry *entry)
{
    const char *p = archive_entry_pathname(entry);
    if (p == NULL)
        p = archive_entry_pathname_utf8(entry);
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
    if (entry->raw_pathname != NULL)
        g_bytes_unref(entry->raw_pathname);
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
        RhFName *fname = NULL;
        RhEntryPathState state = normalize_entry_path(raw, &pathname, &fname);

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
        item->raw_pathname = g_bytes_ref(fname->raw);
        item->name_encoding = fname->encoding;
        if (fname->encoding == RH_FNAME_GB18030)
            list->guessed_names++;
        else if (fname->encoding == RH_FNAME_ESCAPED)
            list->escaped_names++;
        rh_fname_free(fname);
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

    /* 回退发生时记一条 warning，供日志排查；结果里同时保留逐条状态。 */
    if (list->guessed_names > 0 || list->escaped_names > 0) {
        g_log(RH_ARCHIVE_DOMAIN, G_LOG_LEVEL_WARNING,
              "条目名不是 UTF-8：%u 个按 GB18030 推测，%u 个无法解码已转义",
              list->guessed_names, list->escaped_names);
    }

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
        RhEntryPathState st = normalize_entry_path(options->entry, &wanted, NULL);
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
        RhEntryPathState state = normalize_entry_path(raw, &pathname, NULL);
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
