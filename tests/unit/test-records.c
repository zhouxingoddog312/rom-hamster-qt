#include <glib.h>

#include "rh-records.h"
#include "rh-test-main.h"

static void test_media_kinds(void)
{
    g_assert_cmpint(G_N_ELEMENTS(rh_media_kind_names), ==, 7);
    g_assert_true(rh_media_kind_valid("covers"));
    g_assert_true(rh_media_kind_valid("patcher"));
    g_assert_false(rh_media_kind_valid("cover"));
    g_assert_false(rh_media_kind_valid(NULL));
}

static void test_game_defaults(void)
{
    RhGame *game = rh_game_new();
    g_assert_nonnull(game);
    g_assert_cmpstr(game->rom_format, ==, "raw");
    g_assert_cmpstr(game->rom_status, ==, "unknown");
    g_assert_cmpstr(game->patch_type, ==, "");
    g_assert_false(game->is_multi_disc);
    g_assert_false(game->is_arcade);
    g_assert_cmpint(game->year, ==, 0);
    g_assert_null(game->id);
    rh_game_free(game);

    rh_game_free(NULL);
}

static void test_rom_file_defaults(void)
{
    RhRomFile *rom = rh_rom_file_new();
    g_assert_nonnull(rom);
    g_assert_null(rom->filename);
    g_assert_false(rom->has_size);
    g_assert_cmpint(rom->disc_no, ==, 0);
    g_assert_cmpint(rom->sort, ==, 0);
    rom->filename = g_strdup("Game (Disc 1).cue");
    rh_rom_file_free(rom);

    rh_rom_file_free(NULL);
}

static void test_media_file_defaults(void)
{
    RhMediaFile *media = rh_media_file_new();
    g_assert_nonnull(media);
    g_assert_null(media->kind);
    media->kind = g_strdup("covers");
    media->filename = g_strdup("cover.png");
    rh_media_file_free(media);

    rh_media_file_free(NULL);
}

/* 与 store 层一样的容器用法：g_ptr_array_new_with_free_func */
static void test_ptr_array(void)
{
    GPtrArray *games = g_ptr_array_new_with_free_func(rh_game_free_func);
    for (int i = 0; i < 3; i++) {
        RhGame *game = rh_game_new();
        game->name = g_strdup_printf("Game %d", i);
        g_ptr_array_add(games, game);
    }
    g_assert_cmpuint(games->len, ==, 3);
    g_ptr_array_free(games, TRUE);

    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    g_ptr_array_add(roms, rh_rom_file_new());
    g_ptr_array_free(roms, TRUE);

    GPtrArray *media = g_ptr_array_new_with_free_func(rh_media_file_free_func);
    g_ptr_array_add(media, rh_media_file_new());
    g_ptr_array_free(media, TRUE);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/records/media-kinds", test_media_kinds);
    g_test_add_func("/records/game-defaults", test_game_defaults);
    g_test_add_func("/records/rom-file-defaults", test_rom_file_defaults);
    g_test_add_func("/records/media-file-defaults", test_media_file_defaults);
    g_test_add_func("/records/ptr-array", test_ptr_array);
    return g_test_run();
}
