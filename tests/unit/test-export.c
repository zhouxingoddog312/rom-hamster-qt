#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "export.h"
#include "rh-test-main.h"
#include "store.h"

static RhStore *open_temp_store(gchar **out_dir, gchar **out_path)
{
    gchar *dir = g_dir_make_tmp("romham-export-XXXXXX", NULL);
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

static gchar *make_library(void)
{
    gchar *root = g_dir_make_tmp("romham-lib-XXXXXX", NULL);
    g_assert_nonnull(root);

    gchar *game_dir = g_build_filename(root, "gb", "Tetris", NULL);
    g_assert_cmpint(g_mkdir_with_parents(game_dir, 0755), ==, 0);

    gchar *rom = g_build_filename(game_dir, "Tetris.gb", NULL);
    g_assert_true(g_file_set_contents(rom, "ABCD", 4, NULL));
    g_free(rom);

    gchar *cover_dir = g_build_filename(game_dir, "covers", NULL);
    g_assert_cmpint(g_mkdir_with_parents(cover_dir, 0755), ==, 0);
    gchar *cover = g_build_filename(cover_dir, "cover.png", NULL);
    g_assert_true(g_file_set_contents(cover, "PNG", 3, NULL));
    g_free(cover);
    g_free(cover_dir);
    g_free(game_dir);
    return root;
}

static void seed_game(RhStore *store)
{
    RhGame *game = rh_game_new();
    game->id = g_strdup("d0000001");
    game->platform = g_strdup("gb");
    game->name = g_strdup("Tetris");
    game->game_main_name = g_strdup("Tetris");
    game->dir_path = g_strdup("gb/Tetris");
    game->year = 1989;
    game->rating = 4.5;
    game->genre = g_strdup("Puzzle");
    game->developer = g_strdup("Nintendo");
    game->players = 2;

    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    RhRomFile *rom = rh_rom_file_new();
    rom->filename = g_strdup("Tetris.gb");
    rom->dat_name = g_strdup("Tetris (World).gb");
    rom->has_size = TRUE;
    rom->size = 4;
    g_ptr_array_add(roms, rom);

    GPtrArray *media = g_ptr_array_new_with_free_func(rh_media_file_free_func);
    RhMediaFile *cover = rh_media_file_new();
    cover->kind = g_strdup("covers");
    cover->filename = g_strdup("covers/cover.png");
    cover->has_size = TRUE;
    cover->size = 3;
    g_ptr_array_add(media, cover);

    g_assert_cmpint(store_game_insert(store, game, roms, media, NULL), ==, RH_OK);
    g_ptr_array_unref(roms);
    g_ptr_array_unref(media);
}

static void assert_file_matches_golden(const char *actual_path,
                                       const char *golden_path)
{
    gchar *actual = NULL;
    gsize actual_len = 0;
    gchar *expected = NULL;
    gsize expected_len = 0;
    g_assert_true(g_file_get_contents(actual_path, &actual, &actual_len, NULL));
    g_assert_true(g_file_get_contents(golden_path, &expected, &expected_len, NULL));
    g_assert_cmpuint(actual_len, ==, expected_len);
    g_assert_cmpmem(actual, actual_len, expected, expected_len);
    g_free(actual);
    g_free(expected);
}

static void test_esde_export(void)
{
    gchar *dir = NULL, *db = NULL;
    RhStore *store = open_temp_store(&dir, &db);
    seed_game(store);

    gchar *root = make_library();
    gchar *out = g_dir_make_tmp("romham-out-XXXXXX", NULL);
    g_assert_nonnull(out);

    RhExportOptions options = {
        .library_root = root,
        .output_dir = out,
        .copy_roms = TRUE,
        .copy_media = TRUE,
    };
    RhExportStats stats;
    RhErr err = rh_export_run(store, NULL, RH_TEST_FRONTEND_DIR "/esde.conf",
                              &options, NULL, NULL, &stats);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(stats.games, ==, 1);
    g_assert_cmpuint(stats.roms, ==, 1);
    g_assert_cmpuint(stats.errors, ==, 0);

    gchar *manifest = g_build_filename(out, "gb", "gamelist.xml", NULL);
    assert_file_matches_golden(
        manifest, RH_TEST_GOLDEN_DIR "/esde-gb-gamelist.xml");

    gchar *rom = g_build_filename(out, "gb", "Tetris.gb", NULL);
    gchar *cover = g_build_filename(out, "gb", "media", "covers", "Tetris.png", NULL);
    g_assert_true(g_file_test(rom, G_FILE_TEST_IS_REGULAR));
    g_assert_true(g_file_test(cover, G_FILE_TEST_IS_REGULAR));

    g_free(cover);
    g_free(rom);
    g_free(manifest);
    g_free(out);
    g_free(root);
    close_temp_store(store, dir, db);
}

static void test_pegasus_export(void)
{
    gchar *dir = NULL, *db = NULL;
    RhStore *store = open_temp_store(&dir, &db);
    seed_game(store);

    gchar *out = g_dir_make_tmp("romham-out-XXXXXX", NULL);
    g_assert_nonnull(out);

    RhExportOptions options = {
        .library_root = NULL,
        .output_dir = out,
        .copy_roms = FALSE,
        .copy_media = FALSE,
    };
    RhExportStats stats;
    RhErr err = rh_export_run(store, NULL, RH_TEST_FRONTEND_DIR "/pegasus.conf",
                              &options, NULL, NULL, &stats);
    g_assert_cmpint(err, ==, RH_OK);

    gchar *manifest = g_build_filename(out, "gb", "metadata.pegasus.txt", NULL);
    assert_file_matches_golden(
        manifest, RH_TEST_GOLDEN_DIR "/pegasus-gb-metadata.pegasus.txt");

    g_free(manifest);
    g_free(out);
    close_temp_store(store, dir, db);
}

static void test_dat_names(void)
{
    gchar *dir = NULL, *db = NULL;
    RhStore *store = open_temp_store(&dir, &db);
    seed_game(store);

    gchar *root = make_library();
    gchar *out = g_dir_make_tmp("romham-out-XXXXXX", NULL);
    g_assert_nonnull(out);

    RhExportOptions options = {
        .library_root = root,
        .output_dir = out,
        .copy_roms = TRUE,
        .copy_media = FALSE,
        .name_mode = RH_EXPORT_NAMES_DAT,
    };
    RhExportStats stats;
    RhErr err = rh_export_run(store, NULL, RH_TEST_FRONTEND_DIR "/esde.conf",
                              &options, NULL, NULL, &stats);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(stats.roms, ==, 1);
    g_assert_cmpuint(stats.errors, ==, 0);

    /* 复制出来的文件用 DAT 标准名，源库内的原名不受影响。 */
    gchar *dat_rom = g_build_filename(out, "gb", "Tetris (World).gb", NULL);
    gchar *original = g_build_filename(out, "gb", "Tetris.gb", NULL);
    g_assert_true(g_file_test(dat_rom, G_FILE_TEST_IS_REGULAR));
    g_assert_false(g_file_test(original, G_FILE_TEST_EXISTS));

    g_free(original);
    g_free(dat_rom);
    g_free(out);
    g_free(root);
    close_temp_store(store, dir, db);
}

static void test_template_errors(void)
{
    gchar *dir = g_dir_make_tmp("romham-spec-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *path = g_build_filename(dir, "bad.conf", NULL);

    const char *content =
        "[frontend]\n"
        "id = bad\n"
        "spec_version = 1\n"
        "manifest = %platform%/gamelist.xml\n"
        "layout_rom = %platform%/%rom_file%\n"
        "\n"
        "[export.game]\n"
        "%unknown_var%\n";
    g_assert_true(g_file_set_contents(path, content, -1, NULL));

    RhErr err = RH_OK;
    gchar *message = NULL;
    RhExportSpec *spec = rh_export_spec_load(path, &err, &message);
    g_assert_null(spec);
    g_assert_cmpint(err, ==, RH_ERR_INVALID);
    g_assert_nonnull(message);
    g_assert_nonnull(strstr(message, ":8:"));
    g_assert_nonnull(strstr(message, "unknown_var"));
    g_free(message);

    const char *unclosed =
        "[frontend]\n"
        "id = bad\n"
        "manifest = %platform%/gamelist.xml\n"
        "layout_rom = %platform%/%rom_file%\n"
        "[export.game]\n"
        "%if:name%\n";
    g_assert_true(g_file_set_contents(path, unclosed, -1, NULL));
    message = NULL;
    spec = rh_export_spec_load(path, &err, &message);
    g_assert_null(spec);
    g_assert_cmpint(err, ==, RH_ERR_INVALID);
    g_assert_nonnull(message);
    g_assert_nonnull(strstr(message, "endif"));
    g_free(message);

    g_free(path);
    g_remove(path);
    g_rmdir(dir);
    g_free(dir);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/export/esde", test_esde_export);
    g_test_add_func("/export/pegasus", test_pegasus_export);
    g_test_add_func("/export/dat-names", test_dat_names);
    g_test_add_func("/export/template-errors", test_template_errors);
    return g_test_run();
}
