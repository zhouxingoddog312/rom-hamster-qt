#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include "store-schema.h"
#include "store.h"

static void set_user_version(const char *path, int version)
{
    sqlite3 *db = NULL;
    g_assert_cmpint(sqlite3_open(path, &db), ==, SQLITE_OK);

    char sql[64];
    g_snprintf(sql, sizeof sql, "PRAGMA user_version=%d;", version);
    g_assert_cmpint(sqlite3_exec(db, sql, NULL, NULL, NULL), ==, SQLITE_OK);
    sqlite3_close(db);
}

static void remove_db_files(const char *path)
{
    g_remove(path);
    gchar *wal = g_strconcat(path, "-wal", NULL);
    gchar *shm = g_strconcat(path, "-shm", NULL);
    g_remove(wal);
    g_remove(shm);
    g_free(wal);
    g_free(shm);
}

static void test_create_and_reopen(void)
{
    gchar *dir = g_dir_make_tmp("romham-store-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *path = g_build_filename(dir, "romhamster.db", NULL);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(store);
    g_assert_cmpint(store_schema_version(store), ==, RH_SCHEMA_VERSION);
    g_assert_false(store_is_read_only(store));
    store_close(store);

    /* 第二次打开不应重建 schema */
    store = store_open(path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpint(store_schema_version(store), ==, RH_SCHEMA_VERSION);
    g_assert_false(store_is_read_only(store));
    store_close(store);

    remove_db_files(path);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_newer_schema_is_read_only(void)
{
    gchar *dir = g_dir_make_tmp("romham-store-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *path = g_build_filename(dir, "romhamster.db", NULL);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    store_close(store);

    /* 模拟"更高版本的 rom-hamster 创建的库" */
    set_user_version(path, RH_SCHEMA_VERSION + 1);

    g_test_expect_message("romham/store", G_LOG_LEVEL_WARNING, "*已只读打开*");
    store = store_open(path, &err);
    g_test_assert_expected_messages();
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(store);
    g_assert_true(store_is_read_only(store));
    g_assert_cmpint(store_schema_version(store), ==, RH_SCHEMA_VERSION + 1);
    store_close(store);

    remove_db_files(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_invalid_argument(void)
{
    RhErr err = RH_OK;
    g_assert_null(store_open(NULL, &err));
    g_assert_cmpint(err, ==, RH_ERR_INVALID);
    g_assert_null(store_open("", &err));
    g_assert_cmpint(err, ==, RH_ERR_INVALID);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/store/create-and-reopen", test_create_and_reopen);
    g_test_add_func("/store/newer-schema-read-only", test_newer_schema_is_read_only);
    g_test_add_func("/store/invalid-argument", test_invalid_argument);
    return g_test_run();
}
