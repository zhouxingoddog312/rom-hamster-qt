#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#include "dat.h"
#include "import.h"
#include "rh-hash.h"
#include "rh-records.h"
#include "rh-test-main.h"
#include "store.h"

#define DATA_FILE RH_TEST_DATA_DIR "/platforms.conf"

static RhPlatformRules *load(void)
{
    RhErr err = RH_ERR_INVALID;
    RhPlatformRules *rules = rh_platform_rules_load(DATA_FILE, &err, NULL);
    g_assert_cmpint(err, ==, RH_OK);
    return rules;
}

static void touch(const char *dir, const char *name)
{
    gchar *path = g_build_filename(dir, name, NULL);
    gchar *parent = g_path_get_dirname(path);
    g_mkdir_with_parents(parent, 0755);
    g_assert_true(g_file_set_contents(path, "x", -1, NULL));
    g_free(parent);
    g_free(path);
}

static RhImportPlanEntry *find(RhImportPlan *plan, const char *name)
{
    for (guint i = 0; i < plan->entries->len; i++) {
        RhImportPlanEntry *entry = g_ptr_array_index(plan->entries, i);
        if (g_strcmp0(entry->name, name) == 0)
            return entry;
    }
    return NULL;
}

static void test_plan_scan(void)
{
    RhPlatformRules *rules = load();
    gchar *root = g_dir_make_tmp("romham-import-XXXXXX", NULL);

    gchar *gb = g_build_filename(root, "gb", NULL);
    touch(gb, "Tetris (World).gb");
    touch(gb, "translation.ips");
    touch(gb, "covers/Tetris.png");
    g_free(gb);

    gchar *snes = g_build_filename(root, "snes", NULL);
    touch(snes, "Super Metroid.sfc");
    touch(snes, "notes.xyz");
    g_free(snes);

    touch(root, "collection.zip");

    RhErr err = RH_ERR_INVALID;
    RhImportPlan *plan = rh_import_plan_scan(rules, root, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(plan->entries->len, ==, 3);
    g_assert_cmpuint(plan->files_scanned, ==, 6);
    g_assert_cmpuint(plan->skipped_unknown, ==, 1);
    g_assert_cmpuint(plan->skipped_media, ==, 0);

    RhImportPlanEntry *gb_entry = find(plan, "gb");
    g_assert_nonnull(gb_entry);
    g_assert_cmpstr(gb_entry->platform, ==, "gb");
    g_assert_cmpstr(gb_entry->game_dir, ==, "gb/gb");
    g_assert_false(gb_entry->ambiguous);
    g_assert_false(gb_entry->target_exists);
    g_assert_cmpuint(gb_entry->roms->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(gb_entry->roms, 0), ==, "Tetris (World).gb");
    g_assert_cmpuint(gb_entry->patches->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(gb_entry->patches, 0), ==, "translation.ips");
    g_assert_cmpuint(gb_entry->media->len, ==, 1);
    RhImportMediaFile *media = g_ptr_array_index(gb_entry->media, 0);
    g_assert_cmpstr(media->kind, ==, "covers");
    g_assert_cmpstr(media->filename, ==, "covers/Tetris.png");

    RhImportPlanEntry *snes_entry = find(plan, "snes");
    g_assert_nonnull(snes_entry);
    g_assert_cmpstr(snes_entry->platform, ==, "snes");
    g_assert_cmpuint(snes_entry->roms->len, ==, 1);

    RhImportPlanEntry *zip_entry = find(plan, "collection.zip");
    g_assert_nonnull(zip_entry);
    g_assert_null(zip_entry->platform);
    g_assert_true(zip_entry->ambiguous);
    g_assert_cmpuint(zip_entry->roms->len, ==, 1);

    rh_import_plan_free(plan);
    rh_platform_rules_free(rules);

    /* 清理 */
    gchar *paths[] = {
        g_build_filename(root, "gb", "Tetris (World).gb", NULL),
        g_build_filename(root, "gb", "translation.ips", NULL),
        g_build_filename(root, "gb", "covers", "Tetris.png", NULL),
        g_build_filename(root, "snes", "Super Metroid.sfc", NULL),
        g_build_filename(root, "snes", "notes.xyz", NULL),
        g_build_filename(root, "collection.zip", NULL),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(paths); i++) {
        g_remove(paths[i]);
        g_free(paths[i]);
    }
    gchar *dirs[] = {
        g_build_filename(root, "gb", "covers", NULL),
        g_build_filename(root, "gb", NULL),
        g_build_filename(root, "snes", NULL),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(dirs); i++) {
        g_rmdir(dirs[i]);
        g_free(dirs[i]);
    }
    g_rmdir(root);
    g_free(root);
}

static void test_plan_errors(void)
{
    RhPlatformRules *rules = load();
    RhErr err = RH_OK;

    RhImportPlan *none = rh_import_plan_scan(rules, "/nonexistent/roms", &err);
    g_assert_cmpint(err, ==, RH_ERR_IO);
    g_assert_cmpuint(none->entries->len, ==, 0);
    rh_import_plan_free(none);

    RhImportPlan *invalid = rh_import_plan_scan(NULL, "/tmp", &err);
    g_assert_cmpint(err, ==, RH_ERR_INVALID);
    rh_import_plan_free(invalid);

    gchar *empty_root = g_dir_make_tmp("romham-import-empty-XXXXXX", NULL);
    RhImportPlan *empty = rh_import_plan_scan(rules, empty_root, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(empty->entries->len, ==, 0);
    g_assert_cmpuint(empty->files_scanned, ==, 0);
    rh_import_plan_free(empty);
    g_rmdir(empty_root);
    g_free(empty_root);

    rh_platform_rules_free(rules);
}

/* GBK 字节的磁盘文件名：显示要正确，但定位必须仍走原始字节。 */
#ifndef _WIN32
static void test_gbk_filename(void)
{
    static const gchar gbk_name[] = "\xD3\xCE\xCF\xB7.gb";

    RhPlatformRules *rules = load();
    gchar *root = g_dir_make_tmp("romham-import-XXXXXX", NULL);
    gchar *gb = g_build_filename(root, "gb", NULL);
    g_mkdir_with_parents(gb, 0755);

    gchar *raw_path = g_build_filename(gb, gbk_name, NULL);
    g_assert_true(g_file_set_contents(raw_path, "x", -1, NULL));

    RhErr err = RH_ERR_INVALID;
    RhImportPlan *plan = rh_import_plan_scan(rules, root, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(plan->guessed_names, ==, 1);
    g_assert_cmpuint(plan->escaped_names, ==, 0);

    RhImportPlanEntry *entry = find(plan, "gb");
    g_assert_nonnull(entry);
    g_assert_cmpuint(entry->roms->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(entry->roms, 0), ==, "游戏.gb");

    GBytes *raw = g_ptr_array_index(entry->roms_raw, 0);
    gsize len = 0;
    const gchar *data = g_bytes_get_data(raw, &len);
    g_assert_cmpuint(len, ==, sizeof gbk_name - 1);
    g_assert_cmpint(memcmp(data, gbk_name, len), ==, 0);

    /* 用原始字节拼出来的路径必须真的能定位到文件 */
    gchar *raw_copy = g_strndup(data, len);
    gchar *located = g_build_filename(gb, raw_copy, NULL);
    g_assert_true(g_file_test(located, G_FILE_TEST_IS_REGULAR));
    g_free(located);
    g_free(raw_copy);

    rh_import_plan_free(plan);
    rh_platform_rules_free(rules);
    g_remove(raw_path);
    g_free(raw_path);
    g_rmdir(gb);
    g_rmdir(root);
    g_free(gb);
    g_free(root);
}

/* 完全非法字节：条目不能消失，显示为转义名，原始字节仍可定位。 */
static void test_invalid_filename(void)
{
    static const gchar bad_name[] = "\xFF\xFE.gb";

    RhPlatformRules *rules = load();
    gchar *root = g_dir_make_tmp("romham-import-XXXXXX", NULL);
    gchar *gb = g_build_filename(root, "gb", NULL);
    g_mkdir_with_parents(gb, 0755);

    gchar *raw_path = g_build_filename(gb, bad_name, NULL);
    g_assert_true(g_file_set_contents(raw_path, "x", -1, NULL));

    RhErr err = RH_ERR_INVALID;
    RhImportPlan *plan = rh_import_plan_scan(rules, root, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(plan->escaped_names, ==, 1);

    RhImportPlanEntry *entry = find(plan, "gb");
    g_assert_nonnull(entry);
    g_assert_cmpuint(entry->roms->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(entry->roms, 0), ==, "%FF%FE.gb");

    GBytes *raw = g_ptr_array_index(entry->roms_raw, 0);
    gsize len = 0;
    const gchar *data = g_bytes_get_data(raw, &len);
    g_assert_cmpuint(len, ==, sizeof bad_name - 1);
    g_assert_cmpint(memcmp(data, bad_name, len), ==, 0);

    gchar *raw_copy = g_strndup(data, len);
    gchar *located = g_build_filename(gb, raw_copy, NULL);
    g_assert_true(g_file_test(located, G_FILE_TEST_IS_REGULAR));
    g_free(located);
    g_free(raw_copy);

    rh_import_plan_free(plan);
    rh_platform_rules_free(rules);
    g_remove(raw_path);
    g_free(raw_path);
    g_rmdir(gb);
    g_rmdir(root);
    g_free(gb);
    g_free(root);
}
#endif /* !_WIN32 */

/* Windows 宽字符/UTF-8 往返：中文与 emoji 都不能在入口处丢信息。 */
static void test_wide_char_names(void)
{
    RhPlatformRules *rules = load();
    gchar *root = g_dir_make_tmp("romham-import-XXXXXX", NULL);

    gchar *gb = g_build_filename(root, "gb", NULL);
    touch(gb, "中文 游戏 🎮.gb");
    touch(gb, "covers/封面 🎨.png");
    g_free(gb);

    RhErr err = RH_ERR_INVALID;
    RhImportPlan *plan = rh_import_plan_scan(rules, root, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(plan->guessed_names, ==, 0);
    g_assert_cmpuint(plan->escaped_names, ==, 0);

    RhImportPlanEntry *entry = find(plan, "gb");
    g_assert_nonnull(entry);
    g_assert_cmpstr(g_ptr_array_index(entry->roms, 0), ==, "中文 游戏 🎮.gb");

    GBytes *raw = g_ptr_array_index(entry->roms_raw, 0);
    gsize len = 0;
    const gchar *data = g_bytes_get_data(raw, &len);
    g_assert_cmpuint(len, ==, strlen("中文 游戏 🎮.gb"));
    g_assert_cmpint(memcmp(data, "中文 游戏 🎮.gb", len), ==, 0);

    g_assert_cmpuint(entry->media->len, ==, 1);
    RhImportMediaFile *media = g_ptr_array_index(entry->media, 0);
    g_assert_cmpstr(media->filename, ==, "covers/封面 🎨.png");

    /* 用原始字节仍能定位（POSIX）或 UTF-8 名仍能定位（Windows） */
    gchar *raw_copy = g_strndup(data, len);
    gchar *rom_path = g_build_filename(root, "gb", raw_copy, NULL);
    g_assert_true(g_file_test(rom_path, G_FILE_TEST_IS_REGULAR));
    g_free(rom_path);
    g_free(raw_copy);

    rh_import_plan_free(plan);
    rh_platform_rules_free(rules);

    gchar *roms[] = {
        g_build_filename(root, "gb", "中文 游戏 🎮.gb", NULL),
        g_build_filename(root, "gb", "covers", "封面 🎨.png", NULL),
    };
    for (gsize i = 0; i < G_N_ELEMENTS(roms); i++) {
        g_remove(roms[i]);
        g_free(roms[i]);
    }
    gchar *covers = g_build_filename(root, "gb", "covers", NULL);
    g_rmdir(covers);
    g_free(covers);
    gchar *gb_dir = g_build_filename(root, "gb", NULL);
    g_rmdir(gb_dir);
    g_free(gb_dir);
    g_rmdir(root);
    g_free(root);
}

/* ---------------------------------------------------------------------------
 * 执行阶段
 * ------------------------------------------------------------------------- */

static void remove_tree(const gchar *path)
{
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        GDir *dir = g_dir_open(path, 0, NULL);
        if (dir != NULL) {
            const gchar *name;
            while ((name = g_dir_read_name(dir)) != NULL) {
                gchar *child = g_build_filename(path, name, NULL);
                remove_tree(child);
                g_free(child);
            }
            g_dir_close(dir);
        }
        g_rmdir(path);
    } else {
        g_remove(path);
    }
}

static void write_file(const gchar *dir, const gchar *rel, const gchar *content)
{
    gchar *path = g_build_filename(dir, rel, NULL);
    gchar *parent = g_path_get_dirname(path);
    g_mkdir_with_parents(parent, 0755);
    g_assert_true(g_file_set_contents(path, content, -1, NULL));
    g_free(parent);
    g_free(path);
}

static RhStore *open_store_at(const gchar *root)
{
    gchar *db = g_build_filename(root, "romhamster.db", NULL);
    RhErr err = RH_OK;
    RhStore *store = store_open(db, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(store);
    g_free(db);
    return store;
}

static guint32 crc_of(const gchar *path)
{
    RhHashResult hash;
    RhHashOptions options = { .flags = RH_HASH_CRC32 };
    g_assert_cmpint(rh_hash_file(path, &options, &hash), ==, RH_OK);
    g_assert_true(hash.has_crc32);
    return hash.crc32;
}

static gchar *dat_text_for(guint32 crc, gint64 size)
{
    return g_strdup_printf(
        "<?xml version=\"1.0\"?>\n<datafile>\n"
        "<header><name>Test DAT</name><version>1</version></header>\n"
        "<game name=\"gb\"><rom name=\"Tetris (World).gb\" size=\"%"
        G_GINT64_FORMAT "\" crc=\"%08x\"/></game>\n</datafile>\n",
        size, crc);
}

static void test_execute(void)
{
    RhPlatformRules *rules = load();
    gchar *src = g_dir_make_tmp("romham-src-XXXXXX", NULL);
    gchar *lib = g_dir_make_tmp("romham-lib-XXXXXX", NULL);

    write_file(src, "gb/Tetris (World).gb", "abc");
    write_file(src, "gb/covers/Tetris.png", "img");
    write_file(src, "gb/translation.ips", "patch");

    RhStore *store = open_store_at(lib);

    gchar *rom_path = g_build_filename(src, "gb", "Tetris (World).gb", NULL);
    guint32 crc = crc_of(rom_path);
    g_free(rom_path);

    gchar *dat_text = dat_text_for(crc, 3);
    gchar *dat_path = g_build_filename(lib, "test.dat", NULL);
    g_assert_true(g_file_set_contents(dat_path, dat_text, -1, NULL));
    g_free(dat_text);

    RhDatImportOptions dat_options = { .platform = "gb" };
    RhDatImportStats dat_stats;
    g_assert_cmpint(rh_dat_import(store, dat_path, &dat_options, &dat_stats),
                    ==, RH_OK);
    g_assert_cmpint(dat_stats.entry_count, ==, 1);

    gint64 dat_id = 0;
    gchar *stored_sha1 = NULL;
    g_assert_cmpint(store_dat_lookup(store, "gb", dat_path, &stored_sha1,
                                     &dat_id, NULL), ==, RH_OK);
    g_assert_nonnull(stored_sha1);
    g_free(stored_sha1);
    g_assert_cmpint(dat_id, >, 0);

    gint64 job_id = 0;
    g_assert_cmpint(store_job_create(store, "import", 1, &job_id), ==, RH_OK);

    RhErr err = RH_OK;
    RhImportPlan *plan = rh_import_plan_scan(rules, src, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(plan->entries->len, ==, 1);

    RhImportExecOptions options = {0};
    options.root = lib;
    options.source_dir = src;
    options.match_dat = TRUE;
    options.job_id = job_id;
    RhImportExecStats stats;
    g_assert_cmpint(rh_import_execute(store, rules, plan, &options, &stats),
                    ==, RH_OK);
    g_assert_cmpuint(stats.imported, ==, 1);
    g_assert_cmpuint(stats.skipped, ==, 0);
    g_assert_cmpuint(stats.failed, ==, 0);

    gchar *target = g_build_filename(lib, "gb", "gb", "Tetris (World).gb", NULL);
    g_assert_true(g_file_test(target, G_FILE_TEST_IS_REGULAR));
    gchar *content = NULL;
    g_assert_true(g_file_get_contents(target, &content, NULL, NULL));
    g_assert_cmpstr(content, ==, "abc");
    g_free(content);
    gchar *part = g_strdup_printf("%s.romham-part", target);
    g_assert_false(g_file_test(part, G_FILE_TEST_EXISTS));
    g_free(part);
    g_free(target);

    gchar *cover = g_build_filename(lib, "gb", "gb", "covers", "Tetris.png", NULL);
    g_assert_true(g_file_test(cover, G_FILE_TEST_IS_REGULAR));
    g_free(cover);
    gchar *patch = g_build_filename(lib, "gb", "gb", "translation.ips", NULL);
    g_assert_true(g_file_test(patch, G_FILE_TEST_IS_REGULAR));
    g_free(patch);

    RhGameFilter filter = {0};
    GPtrArray *games = store_games_list(store, &filter, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(games->len, ==, 1);
    RhGame *game = g_ptr_array_index(games, 0);
    g_assert_cmpstr(game->dir_path, ==, "gb/gb");
    g_assert_cmpstr(game->dat_source, ==, "Test DAT");
    g_assert_cmpuint(strlen(game->id), ==, 8);
    gchar *game_id = g_strdup(game->id);

    GPtrArray *roms = store_roms_for_game(store, game_id, &err);
    g_assert_cmpuint(roms->len, ==, 1);
    RhRomFile *rom = g_ptr_array_index(roms, 0);
    gchar *crc_text = g_strdup_printf("%08x", crc);
    g_assert_cmpstr(rom->crc32, ==, crc_text);
    g_free(crc_text);
    g_assert_cmpstr(rom->filename, ==, "Tetris (World).gb");
    g_assert_true(rom->has_size);
    g_assert_cmpint(rom->size, ==, 3);
    g_assert_cmpint(rom->dat_id, ==, dat_id);
    g_ptr_array_unref(roms);

    GPtrArray *media = store_media_for_game(store, game_id, NULL, &err);
    g_assert_cmpuint(media->len, ==, 2);
    gboolean has_cover = FALSE;
    gboolean has_patch = FALSE;
    for (guint i = 0; i < media->len; i++) {
        RhMediaFile *m = g_ptr_array_index(media, i);
        if (g_strcmp0(m->kind, "covers") == 0)
            has_cover = TRUE;
        if (g_strcmp0(m->kind, "patcher") == 0)
            has_patch = TRUE;
    }
    g_assert_true(has_cover);
    g_assert_true(has_patch);
    g_ptr_array_unref(media);

    /* job_items 与游戏记录同事务写入 */
    gchar *db_path = g_build_filename(lib, "romhamster.db", NULL);
    sqlite3 *db = NULL;
    g_assert_cmpint(sqlite3_open(db_path, &db), ==, SQLITE_OK);
    sqlite3_stmt *stmt = NULL;
    g_assert_cmpint(sqlite3_prepare_v2(db,
        "SELECT state, target_id FROM job_items WHERE job_id = ?;", -1, &stmt, NULL),
        ==, SQLITE_OK);
    sqlite3_bind_int64(stmt, 1, job_id);
    g_assert_cmpint(sqlite3_step(stmt), ==, SQLITE_ROW);
    g_assert_cmpstr((const char *)sqlite3_column_text(stmt, 0), ==, "imported");
    g_assert_cmpstr((const char *)sqlite3_column_text(stmt, 1), ==, game_id);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    g_free(db_path);

    g_ptr_array_unref(games);
    g_free(game_id);
    rh_import_plan_free(plan);
    store_close(store);
    rh_platform_rules_free(rules);
    g_free(dat_path);
    remove_tree(src);
    remove_tree(lib);
    g_free(src);
    g_free(lib);
}

static void test_execute_conflict(void)
{
    RhPlatformRules *rules = load();
    gchar *src = g_dir_make_tmp("romham-src-XXXXXX", NULL);
    gchar *lib = g_dir_make_tmp("romham-lib-XXXXXX", NULL);
    write_file(src, "gb/Tetris (World).gb", "new");

    /* 目标已存在：整条跳过，原文件一个字节都不改。 */
    write_file(lib, "gb/gb/Tetris (World).gb", "old");

    RhStore *store = open_store_at(lib);
    RhErr err = RH_OK;
    RhImportPlan *plan = rh_import_plan_scan(rules, src, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(plan->entries->len, ==, 1);

    RhImportExecOptions options = {0};
    options.root = lib;
    options.source_dir = src;
    RhImportExecStats stats;
    g_assert_cmpint(rh_import_execute(store, rules, plan, &options, &stats),
                    ==, RH_OK);
    g_assert_cmpuint(stats.skipped, ==, 1);
    g_assert_cmpuint(stats.imported, ==, 0);

    gchar *target = g_build_filename(lib, "gb", "gb", "Tetris (World).gb", NULL);
    gchar *content = NULL;
    g_assert_true(g_file_get_contents(target, &content, NULL, NULL));
    g_assert_cmpstr(content, ==, "old");
    g_free(content);
    gchar *part = g_strdup_printf("%s.romham-part", target);
    g_assert_false(g_file_test(part, G_FILE_TEST_EXISTS));
    g_free(part);
    g_free(target);

    RhGameFilter filter = {0};
    GPtrArray *games = store_games_list(store, &filter, &err);
    g_assert_cmpuint(games->len, ==, 0);
    g_ptr_array_unref(games);

    rh_import_plan_free(plan);
    store_close(store);
    rh_platform_rules_free(rules);
    remove_tree(src);
    remove_tree(lib);
    g_free(src);
    g_free(lib);
}

static void test_execute_rollback(void)
{
    RhPlatformRules *rules = load();
    gchar *src = g_dir_make_tmp("romham-src-XXXXXX", NULL);
    gchar *lib = g_dir_make_tmp("romham-lib-XXXXXX", NULL);
    write_file(src, "gb/Tetris (World).gb", "abc");

    RhStore *store = open_store_at(lib);

    /* 先占住 gb/gb 这个 dir_path，让插入必然失败。 */
    RhGame *existing = rh_game_new();
    existing->id = g_strdup("aaaa0001");
    existing->platform = g_strdup("gb");
    existing->name = g_strdup("Existing");
    existing->game_main_name = g_strdup("Existing");
    existing->dir_path = g_strdup("gb/gb");
    g_assert_cmpint(store_game_insert(store, existing, NULL, NULL, NULL), ==, RH_OK);
    rh_game_free(existing);

    RhErr err = RH_OK;
    RhImportPlan *plan = rh_import_plan_scan(rules, src, &err);
    g_assert_cmpint(err, ==, RH_OK);

    RhImportExecOptions options = {0};
    options.root = lib;
    options.source_dir = src;
    RhImportExecStats stats;
    /* 这条用例故意触发 UNIQUE 约束；store 会按约定记 warning，
     * 测试期间关掉 fatal-warning，避免把预期失败当崩溃。 */
    GLogLevelFlags fatal = g_log_set_always_fatal(0);
    RhErr exec_err = rh_import_execute(store, rules, plan, &options, &stats);
    g_log_set_always_fatal(fatal);
    g_assert_cmpint(exec_err, ==, RH_OK);
    g_assert_cmpuint(stats.failed, ==, 1);
    g_assert_cmpuint(stats.imported, ==, 0);

    /* 复制出来的文件与新建目录都已回滚 */
    gchar *target = g_build_filename(lib, "gb", "gb", "Tetris (World).gb", NULL);
    g_assert_false(g_file_test(target, G_FILE_TEST_EXISTS));
    g_free(target);
    gchar *gb_dir = g_build_filename(lib, "gb", NULL);
    g_assert_false(g_file_test(gb_dir, G_FILE_TEST_EXISTS));
    g_free(gb_dir);

    RhGameFilter filter = {0};
    GPtrArray *games = store_games_list(store, &filter, &err);
    g_assert_cmpuint(games->len, ==, 1);
    g_ptr_array_unref(games);

    rh_import_plan_free(plan);
    store_close(store);
    rh_platform_rules_free(rules);
    remove_tree(src);
    remove_tree(lib);
    g_free(src);
    g_free(lib);
}

typedef struct {
    int calls;
} CancelState;

static gboolean cancel_after_first(gpointer data)
{
    CancelState *state = data;
    state->calls++;
    return state->calls > 1;
}

static void test_execute_cancel(void)
{
    RhPlatformRules *rules = load();
    gchar *src = g_dir_make_tmp("romham-src-XXXXXX", NULL);
    gchar *lib = g_dir_make_tmp("romham-lib-XXXXXX", NULL);
    write_file(src, "gb/Tetris (World).gb", "abc");
    write_file(src, "snes/Super Metroid.sfc", "xyz");

    RhStore *store = open_store_at(lib);
    RhErr err = RH_OK;
    RhImportPlan *plan = rh_import_plan_scan(rules, src, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(plan->entries->len, ==, 2);

    CancelState cancel = {0};
    RhImportExecOptions options = {0};
    options.root = lib;
    options.source_dir = src;
    options.cancel = cancel_after_first;
    options.cancel_data = &cancel;
    RhImportExecStats stats;
    g_assert_cmpint(rh_import_execute(store, rules, plan, &options, &stats),
                    ==, RH_ERR_CANCELLED);
    g_assert_true(stats.cancelled);
    g_assert_cmpuint(stats.imported, ==, 1);
    g_assert_cmpuint(stats.failed, ==, 0);

    /* 第一个条目已提交，第二个没动 */
    RhGameFilter filter = {0};
    GPtrArray *games = store_games_list(store, &filter, &err);
    g_assert_cmpuint(games->len, ==, 1);
    g_ptr_array_unref(games);

    gchar *gb_target = g_build_filename(lib, "gb", "gb", "Tetris (World).gb", NULL);
    gchar *snes_target = g_build_filename(lib, "snes", "snes", "Super Metroid.sfc", NULL);
    int present = (g_file_test(gb_target, G_FILE_TEST_EXISTS) ? 1 : 0)
                + (g_file_test(snes_target, G_FILE_TEST_EXISTS) ? 1 : 0);
    g_assert_cmpint(present, ==, 1);
    g_free(gb_target);
    g_free(snes_target);

    rh_import_plan_free(plan);
    store_close(store);
    rh_platform_rules_free(rules);
    remove_tree(src);
    remove_tree(lib);
    g_free(src);
    g_free(lib);
}

static void test_execute_multidisc(void)
{
    RhPlatformRules *rules = load();
    gchar *src = g_dir_make_tmp("romham-src-XXXXXX", NULL);
    gchar *lib = g_dir_make_tmp("romham-lib-XXXXXX", NULL);
    write_file(src, "gb/Multi (Disc 1).gb", "one");
    write_file(src, "gb/Multi (Disc 2).gb", "two");
    write_file(src, "gb/fix.ips", "g");

    RhStore *store = open_store_at(lib);
    RhErr err = RH_OK;
    RhImportPlan *plan = rh_import_plan_scan(rules, src, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(plan->entries->len, ==, 1);
    RhImportPlanEntry *entry = g_ptr_array_index(plan->entries, 0);
    g_assert_cmpuint(entry->roms->len, ==, 2);
    g_assert_cmpuint(entry->patches->len, ==, 1);

    RhImportExecOptions options = {0};
    options.root = lib;
    options.source_dir = src;
    options.generate_m3u = TRUE;
    RhImportExecStats stats;
    g_assert_cmpint(rh_import_execute(store, rules, plan, &options, &stats),
                    ==, RH_OK);
    g_assert_cmpuint(stats.imported, ==, 1);

    RhGameFilter filter = {0};
    GPtrArray *games = store_games_list(store, &filter, &err);
    g_assert_cmpuint(games->len, ==, 1);
    RhGame *game = g_ptr_array_index(games, 0);
    g_assert_true(game->is_multi_disc);
    g_assert_cmpstr(game->patch_type, ==, "generic");
    gchar *game_id = g_strdup(game->id);

    GPtrArray *roms = store_roms_for_game(store, game_id, &err);
    g_assert_cmpuint(roms->len, ==, 2);
    gint disc_sum = 0;
    for (guint i = 0; i < roms->len; i++) {
        RhRomFile *rom = g_ptr_array_index(roms, i);
        g_assert_cmpint(rom->disc_no, >, 0);
        disc_sum += rom->disc_no;
    }
    g_assert_cmpint(disc_sum, ==, 3);
    g_ptr_array_unref(roms);

    gchar *m3u = g_build_filename(lib, "gb", "gb", "gb.m3u", NULL);
    g_assert_true(g_file_test(m3u, G_FILE_TEST_IS_REGULAR));
    gchar *content = NULL;
    g_assert_true(g_file_get_contents(m3u, &content, NULL, NULL));
    g_assert_cmpstr(content, ==,
                    "Multi (Disc 1).gb\nMulti (Disc 2).gb\n");
    g_free(content);
    gchar *part = g_strdup_printf("%s.romham-part", m3u);
    g_assert_false(g_file_test(part, G_FILE_TEST_EXISTS));
    g_free(part);
    g_free(m3u);

    g_ptr_array_unref(games);
    g_free(game_id);
    rh_import_plan_free(plan);
    store_close(store);
    rh_platform_rules_free(rules);
    remove_tree(src);
    remove_tree(lib);
    g_free(src);
    g_free(lib);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/import/plan-scan", test_plan_scan);
    g_test_add_func("/import/plan-errors", test_plan_errors);
#ifndef _WIN32
    g_test_add_func("/import/gbk-filename", test_gbk_filename);
    g_test_add_func("/import/invalid-filename", test_invalid_filename);
#endif
    g_test_add_func("/import/wide-char-names", test_wide_char_names);
    g_test_add_func("/import/execute", test_execute);
    g_test_add_func("/import/execute-conflict", test_execute_conflict);
    g_test_add_func("/import/execute-rollback", test_execute_rollback);
    g_test_add_func("/import/execute-cancel", test_execute_cancel);
    g_test_add_func("/import/execute-multidisc", test_execute_multidisc);
    return g_test_run();
}
