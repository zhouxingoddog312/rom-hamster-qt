#include <glib.h>
#include <glib/gstdio.h>

#include "rh-path.h"
#include "rh-test-main.h"
#include "store.h"

static void test_utf8_predicate(void)
{
    g_assert_true(rh_path_is_utf8(NULL));
    g_assert_true(rh_path_is_utf8(""));
    g_assert_true(rh_path_is_utf8("普通/路径"));
    g_assert_true(rh_path_is_utf8("两端通用的rom管理程序"));
    g_assert_false(rh_path_is_utf8("\xff\xfe"));
}

static void test_current_dir_is_utf8(void)
{
    char *cwd = rh_path_current_dir();
    g_assert_nonnull(cwd);
    g_assert_true(rh_path_is_utf8(cwd));
    g_free(cwd);
}

static void test_native_roundtrip(void)
{
    const char *utf8 = "两端通用的rom管理程序";
    char *native = rh_path_utf8_from_native(utf8);
    g_assert_nonnull(native);
    g_assert_true(rh_path_is_utf8(native));
    g_free(native);

#ifdef _WIN32
    wchar_t *wide = rh_path_to_wide(utf8);
    g_assert_nonnull(wide);
    char *back = rh_path_from_wide(wide);
    g_assert_cmpstr(back, ==, utf8);
    g_free(back);
    g_free(wide);
#endif
}

/* 回归：库文件位于含中文的目录时，打开与建表都不得因编码失败（§15） */
static void test_store_in_non_ascii_dir(void)
{
    char *dir = g_dir_make_tmp("romham-path-XXXXXX", NULL);
    g_assert_nonnull(dir);

    gchar *sub = g_build_filename(dir, "游戏库", NULL);
    g_assert_cmpint(g_mkdir(sub, 0700), ==, 0);

    gchar *db_path = g_build_filename(sub, "romhamster.db", NULL);
    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(db_path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(store);
    g_assert_true(g_file_test(db_path, G_FILE_TEST_EXISTS));
    store_close(store);

    gchar *wal = g_strconcat(db_path, "-wal", NULL);
    gchar *shm = g_strconcat(db_path, "-shm", NULL);
    g_remove(wal);
    g_remove(shm);
    g_free(wal);
    g_free(shm);
    g_remove(db_path);
    g_rmdir(sub);
    g_rmdir(dir);
    g_free(db_path);
    g_free(sub);
    g_free(dir);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/path/utf8", test_utf8_predicate);
    g_test_add_func("/path/current-dir", test_current_dir_is_utf8);
    g_test_add_func("/path/native-roundtrip", test_native_roundtrip);
    g_test_add_func("/path/store-non-ascii", test_store_in_non_ascii_dir);
    return g_test_run();
}
