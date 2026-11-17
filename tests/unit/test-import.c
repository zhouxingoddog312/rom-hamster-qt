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

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/import/plan-scan", test_plan_scan);
    g_test_add_func("/import/plan-errors", test_plan_errors);
    return g_test_run();
}
