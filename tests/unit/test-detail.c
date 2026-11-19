#include <glib.h>
#include <glib/gstdio.h>

#include "detail.h"
#include "rh-test-main.h"
#include "store.h"

static RhStore *open_temp_store(gchar **out_dir, gchar **out_path)
{
    gchar *dir = g_dir_make_tmp("romham-detail-XXXXXX", NULL);
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

static void test_detail_view(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);

    RhGame *game = rh_game_new();
    game->id = g_strdup("b0000001");
    game->platform = g_strdup("gb");
    game->name = g_strdup("Tetris");
    game->game_main_name = g_strdup("Tetris");
    game->dir_path = g_strdup("gb/Tetris");
    game->year = 1989;

    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    RhRomFile *rom = rh_rom_file_new();
    rom->filename = g_strdup("Tetris (World).gb");
    rom->has_size = TRUE;
    rom->size = 32768;
    rom->sha1 = g_strdup("0123456789abcdef0123456789abcdef01234567");
    g_ptr_array_add(roms, rom);

    GPtrArray *media = g_ptr_array_new_with_free_func(rh_media_file_free_func);
    RhMediaFile *cover = rh_media_file_new();
    cover->kind = g_strdup("covers");
    cover->filename = g_strdup("cover.png");
    cover->has_size = TRUE;
    cover->size = 1000;
    g_ptr_array_add(media, cover);
    RhMediaFile *video = rh_media_file_new();
    video->kind = g_strdup("videos");
    video->filename = g_strdup("intro.mp4");
    video->has_size = TRUE;
    video->size = 2000;
    g_ptr_array_add(media, video);

    GPtrArray *tags = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(tags, g_strdup("puzzle"));

    g_assert_cmpint(store_game_insert(store, game, roms, media, tags), ==, RH_OK);
    g_ptr_array_unref(roms);
    g_ptr_array_unref(media);
    g_ptr_array_unref(tags);

    /* 导入一个 DAT 条目，验证详情里的匹配视图 */
    gint64 dat_id = 0;
    RhErr err = store_dat_begin(store, "gb", "gb.dat", "Nintendo GB", "1.0",
                                "utf-8", &dat_id);
    g_assert_cmpint(err, ==, RH_OK);
    GPtrArray *entries = g_ptr_array_new_with_free_func(rh_dat_entry_free_func);
    RhDatEntry *entry = rh_dat_entry_new();
    entry->entry_type = g_strdup("rom");
    entry->game_name = g_strdup("Tetris");
    entry->rom_name = g_strdup("Tetris (World).gb");
    entry->has_size = TRUE;
    entry->size = 32768;
    entry->sha1 = g_strdup("0123456789abcdef0123456789abcdef01234567");
    g_ptr_array_add(entries, entry);
    g_assert_cmpint(store_dat_entries_add(store, dat_id, entries), ==, RH_OK);
    gint64 count = 0;
    g_assert_cmpint(store_dat_finish(store, dat_id, "Nintendo GB", "1.0",
                                     "deadbeef", 0, &count), ==, RH_OK);
    g_ptr_array_unref(entries);

    RhGameDetail *detail = rh_detail_load(store, "b0000001", TRUE, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(detail);
    g_assert_cmpstr(detail->game->name, ==, "Tetris");
    g_assert_cmpuint(detail->roms->len, ==, 1);
    g_assert_cmpuint(detail->media->len, ==, 2);
    g_assert_cmpuint(detail->tags->len, ==, 1);
    g_assert_cmpint(detail->rom_bytes, ==, 32768);
    g_assert_cmpint(detail->media_bytes, ==, 3000);
    g_assert_cmpuint(detail->dat_matches->len, ==, 1);
    RhDatMatch *match = g_ptr_array_index(detail->dat_matches, 0);
    g_assert_cmpstr(match->dat_name, ==, "Nintendo GB");
    g_assert_cmpstr(match->entry->rom_name, ==, "Tetris (World).gb");
    rh_detail_free(detail);

    detail = rh_detail_load(store, "b0000001", FALSE, &err);
    g_assert_nonnull(detail);
    g_assert_cmpuint(detail->dat_matches->len, ==, 0);
    rh_detail_free(detail);

    g_assert_null(rh_detail_load(store, "missing1", TRUE, &err));
    g_assert_cmpint(err, ==, RH_ERR_NOT_FOUND);

    close_temp_store(store, dir, path);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/detail/load", test_detail_view);
    return g_test_run();
}
