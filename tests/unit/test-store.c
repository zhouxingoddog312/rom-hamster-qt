#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#include "rh-test-main.h"
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

/* --------------------------------------------------------------------------
 * CRUD 与事务
 * ------------------------------------------------------------------------ */

static RhStore *open_temp_store(gchar **out_dir, gchar **out_path)
{
    gchar *dir = g_dir_make_tmp("romham-store-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *path = g_build_filename(dir, "romhamster.db", NULL);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(store);

    *out_dir = dir;
    *out_path = path;
    return store;
}

static void close_temp_store(RhStore *store, gchar *dir, gchar *path)
{
    store_close(store);
    remove_db_files(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static RhGame *make_game(const char *id, const char *platform, const char *name)
{
    RhGame *game = rh_game_new();
    game->id = g_strdup(id);
    game->platform = g_strdup(platform);
    game->name = g_strdup(name);
    game->game_main_name = g_strdup(name);
    game->dir_path = g_strdup_printf("%s/%s", platform, name);
    return game;
}

static void test_game_crud(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    RhGame *game = make_game("abc12345", "gb", "Tetris");
    game->year = 1989;
    game->players = 2;
    game->rating = 4.5;
    game->region = g_strdup("World");
    game->is_multi_disc = FALSE;

    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    RhRomFile *rom = rh_rom_file_new();
    rom->filename = g_strdup("Tetris (World).gb");
    rom->has_size = TRUE;
    rom->size = 32768;
    rom->crc32 = g_strdup("deadbeef");
    g_ptr_array_add(roms, rom);

    GPtrArray *media = g_ptr_array_new_with_free_func(rh_media_file_free_func);
    RhMediaFile *cover = rh_media_file_new();
    cover->kind = g_strdup("covers");
    cover->filename = g_strdup("cover.png");
    g_ptr_array_add(media, cover);

    GPtrArray *tags = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(tags, g_strdup("puzzle"));
    g_ptr_array_add(tags, g_strdup("classic"));

    g_assert_cmpint(store_game_insert(store, game, roms, media, tags), ==, RH_OK);

    RhErr err = RH_ERR_INVALID;
    RhGame *loaded = store_game_get(store, "abc12345", &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(loaded);
    g_assert_cmpstr(loaded->name, ==, "Tetris");
    g_assert_cmpint(loaded->year, ==, 1989);
    g_assert_cmpint(loaded->players, ==, 2);
    g_assert_cmpfloat(loaded->rating, ==, 4.5);
    g_assert_cmpstr(loaded->rom_format, ==, "raw");
    g_assert_cmpstr(loaded->rom_status, ==, "unknown");
    g_assert_nonnull(loaded->created_at);

    GPtrArray *loaded_roms = store_roms_for_game(store, "abc12345", &err);
    g_assert_cmpuint(loaded_roms->len, ==, 1);
    RhRomFile *r0 = g_ptr_array_index(loaded_roms, 0);
    g_assert_cmpstr(r0->filename, ==, "Tetris (World).gb");
    g_assert_true(r0->has_size);
    g_assert_cmpint(r0->size, ==, 32768);

    GPtrArray *covers = store_media_for_game(store, "abc12345", "covers", &err);
    g_assert_cmpuint(covers->len, ==, 1);
    GPtrArray *videos = store_media_for_game(store, "abc12345", "videos", &err);
    g_assert_cmpuint(videos->len, ==, 0);
    GPtrArray *bogus = store_media_for_game(store, "abc12345", "bogus", &err);
    g_assert_cmpint(err, ==, RH_ERR_INVALID);
    g_assert_cmpuint(bogus->len, ==, 0);
    g_ptr_array_free(covers, TRUE);
    g_ptr_array_free(videos, TRUE);
    g_ptr_array_free(bogus, TRUE);

    GPtrArray *loaded_tags = store_tags_for_game(store, "abc12345", &err);
    g_assert_cmpuint(loaded_tags->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(loaded_tags, 0), ==, "classic");
    g_assert_cmpstr(g_ptr_array_index(loaded_tags, 1), ==, "puzzle");

    /* 更新元数据 */
    loaded->name = g_strdup("Tetris (rev A)");
    loaded->rom_status = g_strdup("good");
    g_assert_cmpint(store_game_update(store, loaded), ==, RH_OK);
    RhGame *reloaded = store_game_get(store, "abc12345", &err);
    g_assert_cmpstr(reloaded->name, ==, "Tetris (rev A)");
    g_assert_cmpstr(reloaded->rom_status, ==, "good");

    /* 删除要级联清掉 roms / media / game_tags */
    g_assert_cmpint(store_game_delete(store, "abc12345"), ==, RH_OK);
    g_assert_null(store_game_get(store, "abc12345", &err));
    g_assert_cmpint(err, ==, RH_OK);
    GPtrArray *gone_roms = store_roms_for_game(store, "abc12345", &err);
    GPtrArray *gone_media = store_media_for_game(store, "abc12345", NULL, &err);
    GPtrArray *gone_tags = store_tags_for_game(store, "abc12345", &err);
    g_assert_cmpuint(gone_roms->len, ==, 0);
    g_assert_cmpuint(gone_media->len, ==, 0);
    g_assert_cmpuint(gone_tags->len, ==, 0);
    g_ptr_array_free(gone_roms, TRUE);
    g_ptr_array_free(gone_media, TRUE);
    g_ptr_array_free(gone_tags, TRUE);

    rh_game_free(loaded);
    rh_game_free(reloaded);
    rh_game_free(game);
    g_ptr_array_free(roms, TRUE);
    g_ptr_array_free(media, TRUE);
    g_ptr_array_free(tags, TRUE);
    close_temp_store(store, dir, path);
}

static void test_game_invalid(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    RhGame *game = make_game("bad00001", "gb", "NoDir");
    g_free(game->dir_path);
    game->dir_path = NULL;

    g_assert_cmpint(store_game_insert(store, game, NULL, NULL, NULL), ==, RH_ERR_INVALID);
    g_assert_cmpint(store_game_insert(store, NULL, NULL, NULL, NULL), ==, RH_ERR_INVALID);
    g_assert_cmpint(store_game_delete(store, NULL), ==, RH_ERR_INVALID);
    g_assert_cmpint(store_game_update(store, NULL), ==, RH_ERR_INVALID);

    rh_game_free(game);
    close_temp_store(store, dir, path);
}

static void test_filter_list_and_count(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    const char *names[] = { "Zelda", "Metroid", "Tetris" };
    const char *platforms[] = { "gb", "gb", "nes" };
    for (int i = 0; i < 3; i++) {
        gchar *id = g_strdup_printf("id00000%d", i);
        RhGame *game = make_game(id, platforms[i], names[i]);
        game->year = 1990 + i;
        g_assert_cmpint(store_game_insert(store, game, NULL, NULL, NULL), ==, RH_OK);
        rh_game_free(game);
        g_free(id);
    }

    RhErr err = RH_ERR_INVALID;
    RhGameFilter filter = { 0 };

    g_assert_cmpint(store_games_count(store, &filter, &err), ==, 3);

    filter.platform = "gb";
    g_assert_cmpint(store_games_count(store, &filter, &err), ==, 2);

    GPtrArray *list = store_games_list(store, &filter, &err);
    g_assert_cmpuint(list->len, ==, 2);
    g_assert_cmpstr(((RhGame *)g_ptr_array_index(list, 0))->name, ==, "Metroid");
    g_assert_cmpstr(((RhGame *)g_ptr_array_index(list, 1))->name, ==, "Zelda");
    g_ptr_array_free(list, TRUE);

    RhGameFilter search = { 0 };
    search.search = "tet";
    g_assert_cmpint(store_games_count(store, &search, &err), ==, 1);

    RhGameFilter by_year = { 0 };
    by_year.order_by = "year";
    by_year.descending = TRUE;
    by_year.limit = 1;
    list = store_games_list(store, &by_year, &err);
    g_assert_cmpuint(list->len, ==, 1);
    g_assert_cmpint(((RhGame *)g_ptr_array_index(list, 0))->year, ==, 1992);
    g_ptr_array_free(list, TRUE);

    close_temp_store(store, dir, path);
}

static void test_transaction_rollback(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    g_assert_cmpint(store_begin(store), ==, RH_OK);
    RhGame *game = make_game("tx000001", "gb", "Rollback Me");
    g_assert_cmpint(store_game_insert(store, game, NULL, NULL, NULL), ==, RH_OK);
    g_assert_cmpint(store_rollback(store), ==, RH_OK);

    RhErr err = RH_OK;
    RhGameFilter filter = { 0 };
    g_assert_cmpint(store_games_count(store, &filter, &err), ==, 0);

    /* 已提交的事务可见 */
    g_assert_cmpint(store_begin(store), ==, RH_OK);
    g_assert_cmpint(store_game_insert(store, game, NULL, NULL, NULL), ==, RH_OK);
    g_assert_cmpint(store_commit(store), ==, RH_OK);
    g_assert_cmpint(store_games_count(store, &filter, &err), ==, 1);

    g_assert_cmpint(store_commit(store), ==, RH_ERR_INVALID);
    g_assert_cmpint(store_rollback(store), ==, RH_ERR_INVALID);

    rh_game_free(game);
    close_temp_store(store, dir, path);
}

static void test_read_only_rejects_writes(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);
    store_close(store);

    set_user_version(path, RH_SCHEMA_VERSION + 1);

    RhErr err = RH_ERR_INVALID;
    g_test_expect_message("romham/store", G_LOG_LEVEL_WARNING, "*已只读打开*");
    store = store_open(path, &err);
    g_test_assert_expected_messages();
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_true(store_is_read_only(store));

    RhGame *game = make_game("ro000001", "gb", "Read Only");
    g_assert_cmpint(store_game_insert(store, game, NULL, NULL, NULL),
                    ==, RH_ERR_SCHEMA_TOO_NEW);
    g_assert_cmpint(store_game_delete(store, "ro000001"), ==, RH_ERR_SCHEMA_TOO_NEW);
    g_assert_cmpint(store_begin(store), ==, RH_ERR_SCHEMA_TOO_NEW);
    g_assert_cmpint(store_job_create(store, "import", 1, NULL), ==, RH_ERR_SCHEMA_TOO_NEW);

    rh_game_free(game);
    close_temp_store(store, dir, path);
}

static void test_dir_path_unique(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    RhGame *first = make_game("uniq0001", "gb", "Tetris");
    g_assert_cmpint(store_game_insert(store, first, NULL, NULL, NULL), ==, RH_OK);

    /* dir_path 有 UNIQUE 约束：同目录第二条必须在写入时被拒绝 */
    RhGame *second = make_game("uniq0002", "gb", "Tetris");
    g_test_expect_message("romham/store", G_LOG_LEVEL_WARNING, "*写 games 失败*");
    g_assert_cmpint(store_game_insert(store, second, NULL, NULL, NULL), ==, RH_ERR_DB);
    g_test_assert_expected_messages();

    RhErr err = RH_OK;
    RhGameFilter filter = { 0 };
    g_assert_cmpint(store_games_count(store, &filter, &err), ==, 1);

    rh_game_free(first);
    rh_game_free(second);
    close_temp_store(store, dir, path);
}

static void test_jobs(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    gint64 job_id = 0;
    g_assert_cmpint(store_job_create(store, "import", 10, &job_id), ==, RH_OK);
    g_assert_cmpint(job_id, >, 0);
    g_assert_cmpint(store_job_progress(store, job_id, 4), ==, RH_OK);
    g_assert_cmpint(store_job_finish(store, job_id, NULL), ==, RH_OK);

    gint64 failed_id = 0;
    g_assert_cmpint(store_job_create(store, "export", 1, &failed_id), ==, RH_OK);
    g_assert_cmpint(store_job_finish(store, failed_id, "disk full"), ==, RH_OK);

    store_close(store);

    sqlite3 *db = NULL;
    g_assert_cmpint(sqlite3_open(path, &db), ==, SQLITE_OK);
    sqlite3_stmt *stmt = NULL;
    g_assert_cmpint(sqlite3_prepare_v2(db,
        "SELECT state, done, error FROM jobs WHERE id = ?;", -1, &stmt, NULL),
        ==, SQLITE_OK);
    sqlite3_bind_int64(stmt, 1, job_id);
    g_assert_cmpint(sqlite3_step(stmt), ==, SQLITE_ROW);
    g_assert_cmpstr((const char *)sqlite3_column_text(stmt, 0), ==, "done");
    g_assert_cmpint(sqlite3_column_int(stmt, 1), ==, 4);
    g_assert_true(sqlite3_column_type(stmt, 2) == SQLITE_NULL);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "SELECT state, error FROM jobs WHERE id = ?;", -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, failed_id);
    g_assert_cmpint(sqlite3_step(stmt), ==, SQLITE_ROW);
    g_assert_cmpstr((const char *)sqlite3_column_text(stmt, 0), ==, "failed");
    g_assert_cmpstr((const char *)sqlite3_column_text(stmt, 1), ==, "disk full");
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    remove_db_files(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

/* 原始字节列：显示名可以是 GB18030 推测值，但 BLOB 必须原样往返。 */
static void test_raw_bytes_roundtrip(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    static const gchar gbk_dir[] = "\xD3\xCE\xCF\xB7";
    static const gchar gbk_rom[] = "\xD3\xCE\xCF\xB7.nes";

    RhGame *game = make_game("raw00001", "nes", "游戏");
    game->dir_path_raw = g_bytes_new(gbk_dir, sizeof gbk_dir - 1);

    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    RhRomFile *rom = rh_rom_file_new();
    rom->filename = g_strdup("游戏.nes");
    rom->filename_raw = g_bytes_new(gbk_rom, sizeof gbk_rom - 1);
    g_ptr_array_add(roms, rom);

    GPtrArray *media = g_ptr_array_new_with_free_func(rh_media_file_free_func);
    RhMediaFile *cover = rh_media_file_new();
    cover->kind = g_strdup("covers");
    cover->filename = g_strdup("游戏.png");
    cover->filename_raw = g_bytes_new(gbk_rom, sizeof gbk_rom - 1);
    g_ptr_array_add(media, cover);

    g_assert_cmpint(store_game_insert(store, game, roms, media, NULL), ==, RH_OK);

    RhErr err = RH_ERR_INVALID;
    RhGame *loaded = store_game_get(store, "raw00001", &err);
    g_assert_cmpint(err, ==, RH_OK);
    gsize len = 0;
    const gchar *data = g_bytes_get_data(loaded->dir_path_raw, &len);
    g_assert_cmpuint(len, ==, sizeof gbk_dir - 1);
    g_assert_cmpint(memcmp(data, gbk_dir, len), ==, 0);

    GPtrArray *loaded_roms = store_roms_for_game(store, "raw00001", &err);
    RhRomFile *r0 = g_ptr_array_index(loaded_roms, 0);
    data = g_bytes_get_data(r0->filename_raw, &len);
    g_assert_cmpuint(len, ==, sizeof gbk_rom - 1);
    g_assert_cmpint(memcmp(data, gbk_rom, len), ==, 0);

    GPtrArray *loaded_media = store_media_for_game(store, "raw00001", "covers", &err);
    RhMediaFile *m0 = g_ptr_array_index(loaded_media, 0);
    data = g_bytes_get_data(m0->filename_raw, &len);
    g_assert_cmpuint(len, ==, sizeof gbk_rom - 1);
    g_assert_cmpint(memcmp(data, gbk_rom, len), ==, 0);

    /* 更新也要写回 BLOB 列 */
    static const gchar new_raw[] = "new-raw";
    g_bytes_unref(loaded->dir_path_raw);
    loaded->dir_path_raw = g_bytes_new(new_raw, sizeof new_raw - 1);
    g_assert_cmpint(store_game_update(store, loaded), ==, RH_OK);

    RhGame *reloaded = store_game_get(store, "raw00001", &err);
    data = g_bytes_get_data(reloaded->dir_path_raw, &len);
    g_assert_cmpuint(len, ==, sizeof new_raw - 1);
    g_assert_cmpint(memcmp(data, new_raw, len), ==, 0);

    rh_game_free(reloaded);
    rh_game_free(loaded);
    rh_game_free(game);
    g_ptr_array_free(loaded_roms, TRUE);
    g_ptr_array_free(loaded_media, TRUE);
    g_ptr_array_free(roms, TRUE);
    g_ptr_array_free(media, TRUE);
    close_temp_store(store, dir, path);
}

/* v1 库（没有 raw 列）打开时必须自动迁移到 v2，旧行 raw 为 NULL。 */
static void test_migrate_v1_to_v2(void)
{
    gchar *dir = g_dir_make_tmp("romham-store-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *path = g_build_filename(dir, "romhamster.db", NULL);

    sqlite3 *db = NULL;
    g_assert_cmpint(sqlite3_open(path, &db), ==, SQLITE_OK);
    for (int i = 0; i < rh_schema_v1_sql_count; i++)
        g_assert_cmpint(sqlite3_exec(db, rh_schema_v1_sql[i], NULL, NULL, NULL),
                        ==, SQLITE_OK);
    g_assert_cmpint(sqlite3_exec(db,
        "INSERT INTO games (id, platform, name, dir_path, created_at, updated_at)"
        " VALUES ('old00001','gb','Old','gb/Old','2020-01-01T00:00:00Z',"
        " '2020-01-01T00:00:00Z');",
        NULL, NULL, NULL), ==, SQLITE_OK);
    g_assert_cmpint(sqlite3_exec(db, "PRAGMA user_version=1;", NULL, NULL, NULL),
                    ==, SQLITE_OK);
    sqlite3_close(db);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(store);
    g_assert_cmpint(store_schema_version(store), ==, RH_SCHEMA_VERSION);
    g_assert_false(store_is_read_only(store));

    RhGame *old = store_game_get(store, "old00001", &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(old);
    g_assert_null(old->dir_path_raw);

    /* 迁移后新写入可以带 raw 列 */
    RhGame *fresh = make_game("new00001", "gb", "New");
    static const gchar raw[] = "gb/New";
    fresh->dir_path_raw = g_bytes_new(raw, sizeof raw - 1);
    g_assert_cmpint(store_game_insert(store, fresh, NULL, NULL, NULL), ==, RH_OK);

    RhGame *loaded = store_game_get(store, "new00001", &err);
    gsize len = 0;
    const gchar *data = g_bytes_get_data(loaded->dir_path_raw, &len);
    g_assert_cmpuint(len, ==, sizeof raw - 1);
    g_assert_cmpint(memcmp(data, raw, len), ==, 0);

    rh_game_free(loaded);
    rh_game_free(fresh);
    rh_game_free(old);
    store_close(store);
    remove_db_files(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

/* v3 库打开时必须自动迁移到当前版本，旧 DAT 行保留并补默认值。 */
static void test_migrate_v3_to_current(void)
{
    gchar *dir = g_dir_make_tmp("romham-store-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *path = g_build_filename(dir, "romhamster.db", NULL);

    sqlite3 *db = NULL;
    g_assert_cmpint(sqlite3_open(path, &db), ==, SQLITE_OK);
    for (int i = 0; i < rh_schema_v1_sql_count; i++)
        g_assert_cmpint(sqlite3_exec(db, rh_schema_v1_sql[i], NULL, NULL, NULL),
                        ==, SQLITE_OK);
    for (int i = 0; i < rh_schema_v2_sql_count; i++)
        g_assert_cmpint(sqlite3_exec(db, rh_schema_v2_sql[i], NULL, NULL, NULL),
                        ==, SQLITE_OK);
    for (int i = 0; i < rh_schema_v3_sql_count; i++)
        g_assert_cmpint(sqlite3_exec(db, rh_schema_v3_sql[i], NULL, NULL, NULL),
                        ==, SQLITE_OK);

    g_assert_cmpint(sqlite3_exec(db,
        "INSERT INTO dats (id, platform, path, imported_at)"
        " VALUES (1, 'gb', 'old.dat', '2020-01-01T00:00:00Z');",
        NULL, NULL, NULL), ==, SQLITE_OK);
    g_assert_cmpint(sqlite3_exec(db,
        "INSERT INTO dat_entries (dat_id, game_name, rom_name, size, crc32)"
        " VALUES (1, 'Old', 'old.bin', 16, 'aabbccdd');",
        NULL, NULL, NULL), ==, SQLITE_OK);
    g_assert_cmpint(sqlite3_exec(db, "PRAGMA user_version=3;", NULL, NULL, NULL),
                    ==, SQLITE_OK);
    sqlite3_close(db);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(store);
    g_assert_cmpint(store_schema_version(store), ==, RH_SCHEMA_VERSION);
    g_assert_cmpint(store_schema_version(store), ==, 6);
    g_assert_false(store_is_read_only(store));
    store_close(store);

    g_assert_cmpint(sqlite3_open(path, &db), ==, SQLITE_OK);
    sqlite3_stmt *stmt = NULL;

    /* 新列可直接查询；旧 DAT 尚未重导入，文件哈希保持 NULL。 */
    g_assert_cmpint(sqlite3_prepare_v2(db,
        "SELECT file_sha1 FROM dats WHERE id = 1;", -1, &stmt, NULL),
        ==, SQLITE_OK);
    g_assert_cmpint(sqlite3_step(stmt), ==, SQLITE_ROW);
    g_assert_true(sqlite3_column_type(stmt, 0) == SQLITE_NULL);
    sqlite3_finalize(stmt);

    /* 旧条目由列默认值回填为 rom，原哈希与大小不动。 */
    g_assert_cmpint(sqlite3_prepare_v2(db,
        "SELECT entry_type, size, crc32 FROM dat_entries"
        " WHERE dat_id = 1 AND rom_name = 'old.bin';", -1, &stmt, NULL),
        ==, SQLITE_OK);
    g_assert_cmpint(sqlite3_step(stmt), ==, SQLITE_ROW);
    g_assert_cmpstr((const char *)sqlite3_column_text(stmt, 0), ==, "rom");
    g_assert_cmpint(sqlite3_column_int(stmt, 1), ==, 16);
    g_assert_cmpstr((const char *)sqlite3_column_text(stmt, 2), ==, "aabbccdd");
    sqlite3_finalize(stmt);

    /* v6：摘要列与索引存在，旧行留 NULL。 */
    g_assert_cmpint(sqlite3_prepare_v2(db,
        "SELECT canon_sha1 FROM roms WHERE 0;", -1, &stmt, NULL), ==, SQLITE_OK);
    sqlite3_finalize(stmt);
    g_assert_cmpint(sqlite3_prepare_v2(db,
        "SELECT crc32, sha1 FROM media WHERE 0;", -1, &stmt, NULL), ==, SQLITE_OK);
    sqlite3_finalize(stmt);

    gboolean found_roms_size_index = FALSE;
    g_assert_cmpint(sqlite3_prepare_v2(db,
        "PRAGMA index_list(roms);", -1, &stmt, NULL), ==, SQLITE_OK);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (g_strcmp0(name, "idx_roms_size") == 0)
            found_roms_size_index = TRUE;
    }
    sqlite3_finalize(stmt);
    g_assert_true(found_roms_size_index);

    gboolean found_size_crc32_index = FALSE;
    g_assert_cmpint(sqlite3_prepare_v2(db,
        "PRAGMA index_list(dat_entries);", -1, &stmt, NULL), ==, SQLITE_OK);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (g_strcmp0(name, "idx_dat_entries_size_crc32") == 0)
            found_size_crc32_index = TRUE;
    }
    sqlite3_finalize(stmt);
    g_assert_true(found_size_crc32_index);

    sqlite3_close(db);
    remove_db_files(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/store/create-and-reopen", test_create_and_reopen);
    g_test_add_func("/store/newer-schema-read-only", test_newer_schema_is_read_only);
    g_test_add_func("/store/invalid-argument", test_invalid_argument);
    g_test_add_func("/store/game-crud", test_game_crud);
    g_test_add_func("/store/game-invalid", test_game_invalid);
    g_test_add_func("/store/filter-list-count", test_filter_list_and_count);
    g_test_add_func("/store/transaction-rollback", test_transaction_rollback);
    g_test_add_func("/store/read-only-rejects-writes", test_read_only_rejects_writes);
    g_test_add_func("/store/dir-path-unique", test_dir_path_unique);
    g_test_add_func("/store/jobs", test_jobs);
    g_test_add_func("/store/raw-bytes-roundtrip", test_raw_bytes_roundtrip);
    g_test_add_func("/store/migrate-v1-to-v2", test_migrate_v1_to_v2);
    g_test_add_func("/store/migrate-v3-to-v6", test_migrate_v3_to_current);
    return g_test_run();
}
