#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "import.h"
#include "rh-test-main.h"

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
    return g_test_run();
}
