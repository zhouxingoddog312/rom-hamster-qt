#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "rh-archive.h"
#include "rh-test-main.h"

#define FIXTURE_RAR RH_TEST_FIXTURE_DIR "/archives/sample.rar"
#define FIXTURE_GBK RH_TEST_FIXTURE_DIR "/archives/gbk-name.zip"
#define FIXTURE_INVALID RH_TEST_FIXTURE_DIR "/archives/invalid-name.zip"

typedef struct {
    const char *name;
    const char *data; /* 目录条目为 NULL */
    gboolean    is_dir;
} FixtureEntry;

/* ------------------------------------------------------------------ */
/* 辅助                                                                */
/* ------------------------------------------------------------------ */

static gchar *make_dir(void)
{
    gchar *dir = g_dir_make_tmp("rh-archive-XXXXXX", NULL);
    g_assert_nonnull(dir);
    return dir;
}

static void remove_tree(const char *path)
{
    GDir *dir = g_dir_open(path, 0, NULL);
    if (dir != NULL) {
        const gchar *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            gchar *child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR))
                remove_tree(child);
            else
                g_unlink(child);
            g_free(child);
        }
        g_dir_close(dir);
    }
    g_rmdir(path);
}

/* 用 libarchive 写测试样本；g_open + archive_write_open_fd 保证 Windows 上
 * 中文路径也能创建（archive_write_open_filename 走 ANSI）。 */
static gboolean build_archive(const char *path, const char *format,
                              const FixtureEntry *files, gsize n,
                              const char *passphrase)
{
    struct archive *a = archive_write_new();
    if (a == NULL)
        return FALSE;

    int rc;
    if (g_strcmp0(format, "zip") == 0)
        rc = archive_write_set_format_zip(a);
    else if (g_strcmp0(format, "7z") == 0)
        rc = archive_write_set_format_7zip(a);
    else
        rc = ARCHIVE_FATAL;

    if (rc != ARCHIVE_OK) {
        g_printerr("format %s failed: %s\n", format,
                   archive_error_string(a) ? archive_error_string(a) : "(null)");
        archive_write_free(a);
        return FALSE;
    }
    archive_write_add_filter_none(a);

    /* Windows 上进程 locale 不是 UTF-8；明确让 zip 写入器按 UTF-8 存条目名。
     * Linux 的 locale 已是 UTF-8，设置该选项反而会被写入器误用，故仅 Windows。 */
#ifdef _WIN32
    if (g_strcmp0(format, "zip") == 0)
        archive_write_set_options(a, "zip:hdrcharset=UTF-8");
#endif

    if (passphrase != NULL) {
        if (archive_write_set_options(a, "zip:encryption=zipcrypt") != ARCHIVE_OK
            || archive_write_set_passphrase(a, passphrase) != ARCHIVE_OK) {
            g_printerr("encryption setup failed: %s\n",
                       archive_error_string(a) ? archive_error_string(a) : "(null)");
            archive_write_free(a);
            return FALSE;
        }
    }

    int fd = g_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        g_printerr("g_open failed for %s: %s\n", path, g_strerror(errno));
        archive_write_free(a);
        return FALSE;
    }
    if (archive_write_open_fd(a, fd) != ARCHIVE_OK) {
        g_printerr("open_fd failed: %s\n",
                   archive_error_string(a) ? archive_error_string(a) : "(null)");
        g_close(fd, NULL);
        archive_write_free(a);
        return FALSE;
    }

    gboolean ok = TRUE;
    for (gsize i = 0; i < n && ok; i++) {
        struct archive_entry *entry = archive_entry_new();
#ifdef _WIN32
        /* Windows 进程 locale 不是 UTF-8，必须显式给 UTF-8 名，否则 zip 写入器
         * 会按 ANSI 转码。 */
        archive_entry_set_pathname_utf8(entry, files[i].name);
#else
        /* libarchive 3.6 的 zip 写入器对 pathname_utf8() 的非 ASCII 路径会解引用
         * 空指针（3.7 已修）；Linux 进程 locale 是 UTF-8，用普通入口即可。 */
        archive_entry_set_pathname(entry, files[i].name);
#endif
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_mtime(entry, 1700000000, 0);
        if (files[i].is_dir) {
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_size(entry, 0);
        } else {
            gsize len = strlen(files[i].data);
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_size(entry, (la_int64_t)len);
        }

        rc = archive_write_header(a, entry);
        if (rc == ARCHIVE_OK && !files[i].is_dir) {
            la_ssize_t written =
                archive_write_data(a, files[i].data, strlen(files[i].data));
            if (written < 0)
                rc = ARCHIVE_FATAL;
        }
        archive_entry_free(entry);
        if (rc < ARCHIVE_OK) {
            g_printerr("write entry %s failed rc=%d: %s\n", files[i].name, rc,
                       archive_error_string(a) ? archive_error_string(a) : "(null)");
            ok = FALSE;
        }
    }

    archive_write_close(a);
    archive_write_free(a);
    g_close(fd, NULL);
    return ok;
}

static RhArchiveEntry *find_entry(RhArchiveList *list, const char *name)
{
    for (guint i = 0; i < list->entries->len; i++) {
        RhArchiveEntry *entry = g_ptr_array_index(list->entries, i);
        if (g_strcmp0(entry->pathname, name) == 0)
            return entry;
    }
    return NULL;
}

static gchar *read_text(const char *path)
{
    gchar *contents = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents(path, &contents, &len, NULL));
    return contents;
}

/* ------------------------------------------------------------------ */
/* 用例                                                                */
/* ------------------------------------------------------------------ */

static void test_list_zip(void)
{
    gchar *dir = make_dir();
    gchar *path = g_build_filename(dir, "sample.zip", NULL);

    FixtureEntry files[] = {
        { "nes/Tetris (World).nes", "NESDATA", FALSE },
        { "readme.txt", "hello", FALSE },
        { "中文/游戏.nes", "ROM", FALSE },
        { "empty", NULL, TRUE },
    };
    g_assert_true(build_archive(path, "zip", files, G_N_ELEMENTS(files), NULL));

    RhArchiveList *list = NULL;
    g_assert_cmpint(rh_archive_list(path, &list), ==, RH_OK);
    g_assert_nonnull(list);
    g_assert_cmpuint(list->entries->len, ==, 4);
    g_assert_false(list->encrypted);

    RhArchiveEntry *entry = find_entry(list, "nes/Tetris (World).nes");
    g_assert_nonnull(entry);
    g_assert_true(entry->size == strlen(files[0].data));
    g_assert_false(entry->is_dir);
    g_assert_false(entry->is_encrypted);

    entry = find_entry(list, "中文/游戏.nes");
    g_assert_nonnull(entry);
    g_assert_cmpstr(entry->pathname, ==, "中文/游戏.nes");

    entry = find_entry(list, "empty");
    g_assert_nonnull(entry);
    g_assert_true(entry->is_dir);
    g_assert_true(entry->size == 0);

    /* 内部路径一律 '/'，不得出现 '\' */
    for (guint i = 0; i < list->entries->len; i++) {
        RhArchiveEntry *item = g_ptr_array_index(list->entries, i);
        g_assert_null(strchr(item->pathname, '\\'));
    }

    gboolean encrypted = TRUE;
    g_assert_cmpint(rh_archive_is_encrypted(path, &encrypted), ==, RH_OK);
    g_assert_false(encrypted);

    rh_archive_list_free(list);
    remove_tree(dir);
    g_free(path);
    g_free(dir);
}

static void test_extract_all(void)
{
    gchar *dir = make_dir();
    gchar *path = g_build_filename(dir, "all.zip", NULL);
    gchar *target = g_build_filename(dir, "out", NULL);

    FixtureEntry files[] = {
        { "sub/a.txt", "alpha", FALSE },
        { "b.txt", "bravo", FALSE },
        { "win\\style.txt", "backslash", FALSE },
    };
    g_assert_true(build_archive(path, "zip", files, G_N_ELEMENTS(files), NULL));

    RhArchiveExtractStats stats = { 0 };
    RhArchiveExtractOptions options = { target, NULL };
    g_assert_cmpint(rh_archive_extract(path, &options, &stats), ==, RH_OK);
    g_assert_cmpuint(stats.files, ==, 3);
    g_assert_cmpuint(stats.skipped, ==, 0);

    gchar *a = g_build_filename(target, "sub", "a.txt", NULL);
    gchar *b = g_build_filename(target, "b.txt", NULL);
    gchar *w = g_build_filename(target, "win", "style.txt", NULL);

    gchar *contents = read_text(a);
    g_assert_cmpstr(contents, ==, "alpha");
    g_free(contents);
    contents = read_text(b);
    g_assert_cmpstr(contents, ==, "bravo");
    g_free(contents);
    contents = read_text(w);
    g_assert_cmpstr(contents, ==, "backslash");
    g_free(contents);

    g_free(w);
    g_free(b);
    g_free(a);
    g_free(target);
    g_free(path);
    remove_tree(dir);
    g_free(dir);
}

static void test_extract_single(void)
{
    gchar *dir = make_dir();
    gchar *path = g_build_filename(dir, "single.zip", NULL);
    gchar *target = g_build_filename(dir, "out", NULL);

    FixtureEntry files[] = {
        { "a.txt", "alpha", FALSE },
        { "b.txt", "bravo", FALSE },
    };
    g_assert_true(build_archive(path, "zip", files, G_N_ELEMENTS(files), NULL));

    RhArchiveExtractStats stats = { 0 };
    RhArchiveExtractOptions options = { target, "b.txt" };
    g_assert_cmpint(rh_archive_extract(path, &options, &stats), ==, RH_OK);
    g_assert_cmpuint(stats.files, ==, 1);

    gchar *b = g_build_filename(target, "b.txt", NULL);
    gchar *a = g_build_filename(target, "a.txt", NULL);
    g_assert_true(g_file_test(b, G_FILE_TEST_IS_REGULAR));
    g_assert_false(g_file_test(a, G_FILE_TEST_EXISTS));

    options.entry = "missing.txt";
    g_assert_cmpint(rh_archive_extract(path, &options, NULL), ==, RH_ERR_NOT_FOUND);

    g_free(a);
    g_free(b);
    g_free(target);
    g_free(path);
    remove_tree(dir);
    g_free(dir);
}

static void test_encrypted_zip(void)
{
    gchar *dir = make_dir();
    gchar *path = g_build_filename(dir, "secret.zip", NULL);
    gchar *target = g_build_filename(dir, "out", NULL);

    FixtureEntry files[] = { { "secret.txt", "classified", FALSE } };
    g_assert_true(build_archive(path, "zip", files, G_N_ELEMENTS(files), "pw"));

    RhArchiveList *list = NULL;
    g_assert_cmpint(rh_archive_list(path, &list), ==, RH_OK);
    g_assert_true(list->encrypted);
    g_assert_cmpuint(list->entries->len, ==, 1);
    RhArchiveEntry *entry = g_ptr_array_index(list->entries, 0);
    g_assert_true(entry->is_encrypted);
    rh_archive_list_free(list);

    gboolean encrypted = FALSE;
    g_assert_cmpint(rh_archive_is_encrypted(path, &encrypted), ==, RH_OK);
    g_assert_true(encrypted);

    RhArchiveExtractOptions options = { target, NULL };
    g_assert_cmpint(rh_archive_extract(path, &options, NULL), ==, RH_ERR_ENCRYPTED);

    gchar *dest = g_build_filename(target, "secret.txt", NULL);
    g_assert_false(g_file_test(dest, G_FILE_TEST_EXISTS));

    g_free(dest);
    g_free(target);
    g_free(path);
    remove_tree(dir);
    g_free(dir);
}

static void test_corrupt(void)
{
    gchar *dir = make_dir();
    gchar *path = g_build_filename(dir, "junk.bin", NULL);
    g_assert_true(g_file_set_contents(path, "this is not an archive\n", -1, NULL));

    RhArchiveList *list = NULL;
    g_assert_cmpint(rh_archive_list(path, &list), ==, RH_ERR_UNSUPPORTED);
    g_assert_null(list);

    gboolean encrypted = FALSE;
    g_assert_cmpint(rh_archive_is_encrypted(path, &encrypted), ==, RH_ERR_UNSUPPORTED);

    gchar *target = g_build_filename(dir, "out", NULL);
    RhArchiveExtractOptions options = { target, NULL };
    g_assert_cmpint(rh_archive_extract(path, &options, NULL), ==, RH_ERR_UNSUPPORTED);

    gchar *missing = g_build_filename(dir, "no-such.zip", NULL);
    g_assert_cmpint(rh_archive_list(missing, &list), ==, RH_ERR_IO);

    g_free(missing);
    g_free(target);
    g_free(path);
    remove_tree(dir);
    g_free(dir);
}

static void test_traversal_rejected(void)
{
    gchar *dir = make_dir();
    gchar *path = g_build_filename(dir, "evil.zip", NULL);
    gchar *target = g_build_filename(dir, "out", NULL);

    FixtureEntry files[] = {
        { "../evil.txt", "pwned", FALSE },
        { "ok.txt", "fine", FALSE },
    };
    if (!build_archive(path, "zip", files, G_N_ELEMENTS(files), NULL)) {
        g_test_skip("libarchive 写入了不合法的条目名");
        goto out;
    }

    RhArchiveList *list = NULL;
    g_assert_cmpint(rh_archive_list(path, &list), ==, RH_ERR_INVALID);

    RhArchiveExtractOptions options = { target, NULL };
    g_assert_cmpint(rh_archive_extract(path, &options, NULL), ==, RH_ERR_INVALID);

    gchar *escaped = g_build_filename(dir, "evil.txt", NULL);
    g_assert_false(g_file_test(escaped, G_FILE_TEST_EXISTS));
    g_free(escaped);

out:
    g_free(target);
    g_free(path);
    remove_tree(dir);
    g_free(dir);
}

static void test_7z_roundtrip(void)
{
    gchar *dir = make_dir();
    gchar *path = g_build_filename(dir, "sample.7z", NULL);
    gchar *target = g_build_filename(dir, "out", NULL);

    FixtureEntry files[] = {
        { "roms/game.nes", "SEVENZIP", FALSE },
        { "info.txt", "seven", FALSE },
    };
    if (!build_archive(path, "7z", files, G_N_ELEMENTS(files), NULL)) {
        g_test_skip("libarchive 未编译 7z 写入支持");
        goto out;
    }

    RhArchiveList *list = NULL;
    g_assert_cmpint(rh_archive_list(path, &list), ==, RH_OK);
    g_assert_cmpuint(list->entries->len, ==, 2);
    g_assert_nonnull(find_entry(list, "roms/game.nes"));
    g_assert_false(list->encrypted);
    rh_archive_list_free(list);

    RhArchiveExtractOptions options = { target, NULL };
    RhArchiveExtractStats stats = { 0 };
    g_assert_cmpint(rh_archive_extract(path, &options, &stats), ==, RH_OK);
    g_assert_cmpuint(stats.files, ==, 2);

    gchar *file = g_build_filename(target, "roms", "game.nes", NULL);
    gchar *contents = read_text(file);
    g_assert_cmpstr(contents, ==, "SEVENZIP");
    g_free(contents);
    g_free(file);

out:
    g_free(target);
    g_free(path);
    remove_tree(dir);
    g_free(dir);
}

static void test_rar_read(void)
{
    if (!g_file_test(FIXTURE_RAR, G_FILE_TEST_EXISTS)) {
        g_test_skip("缺少 RAR 样本 " FIXTURE_RAR);
        return;
    }

    RhArchiveList *list = NULL;
    g_assert_cmpint(rh_archive_list(FIXTURE_RAR, &list), ==, RH_OK);
    RhArchiveEntry *entry = find_entry(list, "hello.nes");
    g_assert_nonnull(entry);
    g_assert_true(entry->size == 7);
    g_assert_false(entry->is_dir);
    rh_archive_list_free(list);

    gchar *dir = make_dir();
    gchar *target = g_build_filename(dir, "out", NULL);
    RhArchiveExtractOptions options = { target, NULL };
    g_assert_cmpint(rh_archive_extract(FIXTURE_RAR, &options, NULL), ==, RH_OK);

    gchar *file = g_build_filename(target, "hello.nes", NULL);
    gchar *contents = read_text(file);
    g_assert_cmpstr(contents, ==, "RARFIX\n");
    g_free(contents);
    g_free(file);

    g_free(target);
    remove_tree(dir);
    g_free(dir);
}

/* 老 Windows 工具打的 zip：条目名是 GBK 字节且无 UTF-8 标记。 */
static void test_gbk_entry_name(void)
{
    if (!g_file_test(FIXTURE_GBK, G_FILE_TEST_EXISTS)) {
        g_test_skip("缺少 GBK 样本 " FIXTURE_GBK);
        return;
    }

    static const gchar expected_raw[] = "\xD3\xCE\xCF\xB7.nes";

    RhArchiveList *list = NULL;
    g_test_expect_message("romham/archive", G_LOG_LEVEL_WARNING, "*GB18030*");
    g_assert_cmpint(rh_archive_list(FIXTURE_GBK, &list), ==, RH_OK);
    g_test_assert_expected_messages();

    g_assert_cmpuint(list->entries->len, ==, 1);
    g_assert_cmpuint(list->guessed_names, ==, 1);
    g_assert_cmpuint(list->escaped_names, ==, 0);

    RhArchiveEntry *entry = g_ptr_array_index(list->entries, 0);
    g_assert_cmpstr(entry->pathname, ==, "游戏.nes");
    g_assert_cmpint(entry->name_encoding, ==, RH_FNAME_GB18030);

    gsize raw_len = 0;
    const gchar *raw = g_bytes_get_data(entry->raw_pathname, &raw_len);
    g_assert_cmpuint(raw_len, ==, strlen(expected_raw));
    g_assert_cmpint(memcmp(raw, expected_raw, raw_len), ==, 0);
    rh_archive_list_free(list);

    /* 解压按显示名落盘：双端都得到合法 UTF-8 文件名 */
    gchar *dir = make_dir();
    gchar *target = g_build_filename(dir, "out", NULL);
    RhArchiveExtractOptions options = { target, NULL };
    g_assert_cmpint(rh_archive_extract(FIXTURE_GBK, &options, NULL), ==, RH_OK);

    gchar *file = g_build_filename(target, "游戏.nes", NULL);
    gchar *contents = read_text(file);
    g_assert_cmpstr(contents, ==, "GBKDATA");
    g_free(contents);
    g_free(file);
    g_free(target);
    remove_tree(dir);
    g_free(dir);
}

/* 既不是 UTF-8 也解不出 GB18030：必须转义保留，不能丢条目。 */
static void test_invalid_entry_name(void)
{
    if (!g_file_test(FIXTURE_INVALID, G_FILE_TEST_EXISTS)) {
        g_test_skip("缺少非法字节样本 " FIXTURE_INVALID);
        return;
    }

    static const gchar expected_raw[] = "\xFF\xFE.nes";

    RhArchiveList *list = NULL;
    g_test_expect_message("romham/archive", G_LOG_LEVEL_WARNING, "*转义*");
    g_assert_cmpint(rh_archive_list(FIXTURE_INVALID, &list), ==, RH_OK);
    g_test_assert_expected_messages();

    g_assert_cmpuint(list->entries->len, ==, 1);
    g_assert_cmpuint(list->guessed_names, ==, 0);
    g_assert_cmpuint(list->escaped_names, ==, 1);

    RhArchiveEntry *entry = g_ptr_array_index(list->entries, 0);
    g_assert_cmpstr(entry->pathname, ==, "%FF%FE.nes");
    g_assert_cmpint(entry->name_encoding, ==, RH_FNAME_ESCAPED);
    g_assert_null(strchr(entry->pathname, '/'));
    g_assert_null(strchr(entry->pathname, '\\'));

    gsize raw_len = 0;
    const gchar *raw = g_bytes_get_data(entry->raw_pathname, &raw_len);
    g_assert_cmpuint(raw_len, ==, strlen(expected_raw));
    g_assert_cmpint(memcmp(raw, expected_raw, raw_len), ==, 0);
    rh_archive_list_free(list);

    /* 兜底名不含分隔符，解压不会切出子目录 */
    gchar *dir = make_dir();
    gchar *target = g_build_filename(dir, "out", NULL);
    RhArchiveExtractOptions options = { target, NULL };
    g_assert_cmpint(rh_archive_extract(FIXTURE_INVALID, &options, NULL), ==, RH_OK);

    gchar *file = g_build_filename(target, "%FF%FE.nes", NULL);
    gchar *contents = read_text(file);
    g_assert_cmpstr(contents, ==, "BADDATA");
    g_free(contents);
    g_free(file);
    g_free(target);
    remove_tree(dir);
    g_free(dir);
}

/* 中文路径既是 Windows 宽字符 API 的回归，也是 UTF-8 入口的回归。 */
static void test_chinese_paths(void)
{
    gchar *base = make_dir();
    gchar *subdir = g_build_filename(base, "中文 目录", NULL);
    g_assert_cmpint(g_mkdir_with_parents(subdir, 0755), ==, 0);

    gchar *path = g_build_filename(subdir, "压缩包 测试.zip", NULL);
    gchar *target = g_build_filename(subdir, "输出 目录", NULL);

    FixtureEntry files[] = { { "游戏/机战.nes", "CHINESE", FALSE } };
    g_assert_true(build_archive(path, "zip", files, G_N_ELEMENTS(files), NULL));

    RhArchiveList *list = NULL;
    g_assert_cmpint(rh_archive_list(path, &list), ==, RH_OK);
    g_assert_nonnull(find_entry(list, "游戏/机战.nes"));
    rh_archive_list_free(list);

    RhArchiveExtractOptions options = { target, NULL };
    g_assert_cmpint(rh_archive_extract(path, &options, NULL), ==, RH_OK);

    gchar *file = g_build_filename(target, "游戏", "机战.nes", NULL);
    gchar *contents = read_text(file);
    g_assert_cmpstr(contents, ==, "CHINESE");
    g_free(contents);
    g_free(file);

    g_free(target);
    g_free(path);
    g_free(subdir);
    remove_tree(base);
    g_free(base);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);

    g_test_add_func("/archive/list-zip", test_list_zip);
    g_test_add_func("/archive/extract-all", test_extract_all);
    g_test_add_func("/archive/extract-single", test_extract_single);
    g_test_add_func("/archive/encrypted-zip", test_encrypted_zip);
    g_test_add_func("/archive/corrupt", test_corrupt);
    g_test_add_func("/archive/traversal-rejected", test_traversal_rejected);
    g_test_add_func("/archive/7z-roundtrip", test_7z_roundtrip);
    g_test_add_func("/archive/rar-read", test_rar_read);
    g_test_add_func("/archive/gbk-entry-name", test_gbk_entry_name);
    g_test_add_func("/archive/invalid-entry-name", test_invalid_entry_name);
    g_test_add_func("/archive/chinese-paths", test_chinese_paths);

    return g_test_run();
}
