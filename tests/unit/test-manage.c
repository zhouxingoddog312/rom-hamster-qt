#include <glib.h>
#include <glib/gstdio.h>

#include "manage.h"
#include "rh-test-main.h"
#include "store.h"

static RhStore *open_temp_store(gchar **out_dir, gchar **out_path)
{
    gchar *dir = g_dir_make_tmp("romham-manage-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *path = g_build_filename(dir, "romhamster.db", NULL);
    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    *out_dir = dir;
    *out_path = path;
    return store;
}

static void close_temp_store(RhStore *store, gchar *dir, gchar *path)
{
    store_close(store);
    g_remove(path);
    gchar *wal = g_strconcat(path, "-wal", NULL);
    gchar *shm = g_strconcat(path, "-shm", NULL);
    g_remove(wal);
    g_remove(shm);
    g_free(wal);
    g_free(shm);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void insert_simple(RhStore *store, const char *id, const char *name,
                          int disc1, int disc2)
{
    RhGame *game = rh_game_new();
    game->id = g_strdup(id);
    game->platform = g_strdup("gb");
    game->name = g_strdup(name);
    game->game_main_name = g_strdup(name);
    game->dir_path = g_strdup_printf("gb/%s", name);

    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    RhRomFile *rom = rh_rom_file_new();
    rom->filename = g_strdup_printf("%s.gb", name);
    rom->has_size = TRUE;
    rom->size = 4;
    rom->disc_no = disc1;
    g_ptr_array_add(roms, rom);
    if (disc2 > 0) {
        RhRomFile *second = rh_rom_file_new();
        second->filename = g_strdup_printf("%s (Disc 2).gb", name);
        second->has_size = TRUE;
        second->size = 4;
        second->disc_no = disc2;
        g_ptr_array_add(roms, second);
    }
    g_assert_cmpint(store_game_insert(store, game, roms, NULL, NULL), ==, RH_OK);
    g_ptr_array_unref(roms);
}

static void test_edit_and_patch(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);
    insert_simple(store, "c0000001", "Tetris", 0, 0);

    RhErr err = RH_OK;
    RhGame *game = store_game_get(store, "c0000001", &err);
    g_free(game->name);
    game->name = g_strdup("Tetris DX");
    g_assert_cmpint(rh_manage_update(store, game), ==, RH_OK);
    rh_game_free(game);

    game = store_game_get(store, "c0000001", &err);
    g_assert_cmpstr(game->name, ==, "Tetris DX");
    rh_game_free(game);

    RhGame *missing = rh_game_new();
    missing->id = g_strdup("nope0000");
    g_assert_cmpint(rh_manage_update(store, missing), ==, RH_ERR_NOT_FOUND);
    rh_game_free(missing);

    g_assert_cmpint(rh_manage_set_patch_type(store, "c0000001", "bogus"),
                    ==, RH_ERR_INVALID);
    g_assert_cmpint(rh_manage_set_patch_type(store, "c0000001", "mixed"), ==, RH_OK);
    game = store_game_get(store, "c0000001", &err);
    g_assert_cmpstr(game->patch_type, ==, "mixed");
    rh_game_free(game);

    close_temp_store(store, dir, path);
}

static void test_renumber_discs(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);
    insert_simple(store, "c0000002", "Chrono", 5, 9);

    RhErr err = RH_OK;
    g_assert_cmpint(rh_manage_renumber_discs(store, "c0000002"), ==, RH_OK);

    RhGame *game = store_game_get(store, "c0000002", &err);
    g_assert_true(game->is_multi_disc);
    rh_game_free(game);

    GPtrArray *roms = store_roms_for_game(store, "c0000002", &err);
    g_assert_cmpuint(roms->len, ==, 2);
    int disc_a = 0, disc_b = 0;
    for (guint i = 0; i < roms->len; i++) {
        RhRomFile *r = g_ptr_array_index(roms, i);
        if (g_strcmp0(r->filename, "Chrono.gb") == 0)
            disc_a = r->disc_no;
        else if (g_strcmp0(r->filename, "Chrono (Disc 2).gb") == 0)
            disc_b = r->disc_no;
    }
    g_assert_cmpint(disc_a, ==, 1);
    g_assert_cmpint(disc_b, ==, 2);
    g_ptr_array_unref(roms);

    insert_simple(store, "c0000003", "Single", 0, 0);
    g_assert_cmpint(rh_manage_renumber_discs(store, "c0000003"), ==, RH_OK);
    game = store_game_get(store, "c0000003", &err);
    g_assert_false(game->is_multi_disc);
    rh_game_free(game);

    close_temp_store(store, dir, path);
}

static void test_stats(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);
    insert_simple(store, "c0000004", "Multi", 1, 2);
    insert_simple(store, "c0000005", "Solo", 0, 0);

    RhLibStats stats;
    RhErr err = RH_OK;
    g_assert_cmpint(rh_manage_stats(store, &stats, &err), ==, RH_OK);
    g_assert_cmpint(stats.games, ==, 2);
    g_assert_cmpint(stats.roms, ==, 3);
    g_assert_cmpint(stats.rom_bytes, ==, 12);
    g_assert_cmpint(stats.multi_disc, ==, 0); /* is_multi_disc 未手动置位 */

    GPtrArray *by_platform = rh_manage_stats_by_platform(store, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(by_platform->len, ==, 1);
    RhPlatformStats *p = g_ptr_array_index(by_platform, 0);
    g_assert_cmpstr(p->platform, ==, "gb");
    g_assert_cmpint(p->games, ==, 2);
    g_ptr_array_unref(by_platform);

    close_temp_store(store, dir, path);
}

static void test_integrity_and_delete(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    gchar *root = g_dir_make_tmp("romham-lib-XXXXXX", NULL);
    g_assert_nonnull(root);
    gchar *game_dir = g_build_filename(root, "gb", "Tetris", NULL);
    g_assert_cmpint(g_mkdir_with_parents(game_dir, 0755), ==, 0);

    RhGame *game = rh_game_new();
    game->id = g_strdup("c0000006");
    game->platform = g_strdup("gb");
    game->name = g_strdup("Tetris");
    game->game_main_name = g_strdup("Tetris");
    game->dir_path = g_strdup("gb/Tetris");

    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    RhRomFile *ok = rh_rom_file_new();
    ok->filename = g_strdup("Tetris.gb");
    ok->has_size = TRUE;
    ok->size = 4;
    g_ptr_array_add(roms, ok);
    RhRomFile *missing = rh_rom_file_new();
    missing->filename = g_strdup("missing.gb");
    missing->has_size = TRUE;
    missing->size = 4;
    g_ptr_array_add(roms, missing);
    g_assert_cmpint(store_game_insert(store, game, roms, NULL, NULL), ==, RH_OK);
    g_ptr_array_unref(roms);

    gchar *file = g_build_filename(game_dir, "Tetris.gb", NULL);
    g_assert_true(g_file_set_contents(file, "ABCD", 4, NULL));
    g_free(file);

    insert_simple(store, "c0000007", "Ghost", 0, 0); /* 目录不存在 */

    RhIntegrityReport *report = rh_integrity_report_new();
    RhErr err = RH_OK;
    g_assert_cmpint(rh_manage_integrity(store, root, report, &err), ==, RH_OK);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpint(report->games_checked, ==, 2);
    g_assert_cmpint(report->roms_checked, ==, 2);

    gboolean saw_missing_rom = FALSE;
    gboolean saw_missing_dir = FALSE;
    for (guint i = 0; i < report->issues->len; i++) {
        RhIntegrityIssue *issue = g_ptr_array_index(report->issues, i);
        if (issue->kind == RH_INTEGRITY_MISSING_ROM)
            saw_missing_rom = TRUE;
        if (issue->kind == RH_INTEGRITY_MISSING_DIR)
            saw_missing_dir = TRUE;
    }
    g_assert_true(saw_missing_rom);
    g_assert_true(saw_missing_dir);
    rh_integrity_report_free(report);

    g_assert_cmpint(rh_manage_delete(store, "c0000006", root, TRUE), ==, RH_OK);
    g_assert_false(g_file_test(game_dir, G_FILE_TEST_EXISTS));
    g_assert_null(store_game_get(store, "c0000006", &err));
    g_assert_cmpint(rh_manage_delete(store, "nope0000", root, FALSE),
                    ==, RH_ERR_NOT_FOUND);

    g_free(game_dir);
    g_rmdir(root);
    g_free(root);
    close_temp_store(store, dir, path);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/manage/edit-and-patch", test_edit_and_patch);
    g_test_add_func("/manage/renumber-discs", test_renumber_discs);
    g_test_add_func("/manage/stats", test_stats);
    g_test_add_func("/manage/integrity-and-delete", test_integrity_and_delete);
    return g_test_run();
}
