#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "query.h"
#include "rh-test-main.h"
#include "store.h"

static RhStore *open_temp_store(gchar **out_dir, gchar **out_path)
{
    gchar *dir = g_dir_make_tmp("romham-query-XXXXXX", NULL);
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

static void insert_game(RhStore *store, const char *id, const char *platform,
                        const char *name, int year, double rating,
                        const char *genre, const char *developer,
                        const char *publisher, const char *region,
                        const char *status, const char *format,
                        int disc_count, gboolean has_patch,
                        const char *dat_source, const char *tag)
{
    RhGame *game = rh_game_new();
    game->id = g_strdup(id);
    game->platform = g_strdup(platform);
    game->name = g_strdup(name);
    game->game_main_name = g_strdup(name);
    game->dir_path = g_strdup_printf("%s/%s", platform, name);
    game->year = year;
    game->rating = rating;
    game->genre = g_strdup(genre);
    game->developer = g_strdup(developer);
    game->publisher = g_strdup(publisher);
    game->region = g_strdup(region);
    game->rom_format = g_strdup(format);
    game->rom_status = g_strdup(status);
    game->dat_source = g_strdup(dat_source);
    game->is_multi_disc = disc_count > 1;
    game->patch_type = g_strdup(has_patch ? "platform" : "");

    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    for (int d = 1; d <= (disc_count > 0 ? disc_count : 1); d++) {
        RhRomFile *rom = rh_rom_file_new();
        rom->filename = disc_count > 1
            ? g_strdup_printf("%s (Disc %d).bin", name, d)
            : g_strdup_printf("%s.rom", name);
        rom->disc_no = disc_count > 1 ? d : 0;
        rom->has_size = TRUE;
        rom->size = 1024;
        g_ptr_array_add(roms, rom);
    }

    GPtrArray *media = NULL;
    if (has_patch) {
        media = g_ptr_array_new_with_free_func(rh_media_file_free_func);
        RhMediaFile *patch = rh_media_file_new();
        patch->kind = g_strdup("patcher");
        patch->filename = g_strdup("patch.ips");
        g_ptr_array_add(media, patch);
    }

    GPtrArray *tags = NULL;
    if (tag != NULL) {
        tags = g_ptr_array_new_with_free_func(g_free);
        g_ptr_array_add(tags, g_strdup(tag));
    }

    g_assert_cmpint(store_game_insert(store, game, roms, media, tags), ==, RH_OK);
    g_ptr_array_unref(roms);
    if (media != NULL)
        g_ptr_array_unref(media);
    if (tags != NULL)
        g_ptr_array_unref(tags);
}

static void seed(RhStore *store)
{
    insert_game(store, "a0000001", "nes", "Super Mario Bros", 1985, 9.0,
                "Platformer", "Nintendo", "Nintendo", "USA", "good", "raw",
                1, FALSE, NULL, NULL);
    insert_game(store, "a0000002", "nes", "Zelda", 1986, 8.5, "Action",
                "Nintendo", "Nintendo", "USA", "good", "raw",
                1, TRUE, NULL, NULL);
    insert_game(store, "a0000003", "snes", "Chrono Trigger", 1995, 9.5, "RPG",
                "Square", "Square", "Japan", "unknown", "zip",
                2, FALSE, NULL, "rpg");
    insert_game(store, "a0000004", "gb", "Tetris", 1989, 4.5, "Puzzle",
                "Nintendo", "Nintendo", "World", "unknown", "raw",
                1, FALSE, NULL, NULL);
    insert_game(store, "a0000005", "gba", "Metroid Fusion", 2002, 8.0, "Action",
                "Nintendo", "Nintendo", "USA", "good", "raw",
                1, FALSE, "metroid.dat", NULL);
}

static gint64 run_count(RhStore *store, RhFilter *filter)
{
    RhErr err = RH_OK;
    RhQueryResult *result = rh_query_run(store, filter, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(result);
    gint64 total = result->total;
    rh_query_result_free(result);
    return total;
}

static void test_filters(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);
    seed(store);

    RhFilter f;
    memset(&f, 0, sizeof f);
    f.multi = -1;
    f.has_patch = -1;

    f.platform = "nes";
    g_assert_cmpint(run_count(store, &f), ==, 2);
    f.platform = NULL;

    f.genre = "Action";
    g_assert_cmpint(run_count(store, &f), ==, 2);
    f.genre = NULL;

    f.developer = "Square";
    g_assert_cmpint(run_count(store, &f), ==, 1);
    f.developer = NULL;

    f.publisher = "Nintendo";
    g_assert_cmpint(run_count(store, &f), ==, 4);
    f.publisher = NULL;

    f.region = "Japan";
    g_assert_cmpint(run_count(store, &f), ==, 1);
    f.region = NULL;

    f.year_from = 1985;
    f.year_to = 1989;
    g_assert_cmpint(run_count(store, &f), ==, 3);
    f.year_from = 0;
    f.year_to = 0;

    f.multi = 1;
    g_assert_cmpint(run_count(store, &f), ==, 1);
    f.multi = 0;
    g_assert_cmpint(run_count(store, &f), ==, 4);
    f.multi = -1;

    f.has_patch = 1;
    g_assert_cmpint(run_count(store, &f), ==, 1);
    f.has_patch = 0;
    g_assert_cmpint(run_count(store, &f), ==, 4);
    f.has_patch = -1;

    f.patch_type = "platform";
    g_assert_cmpint(run_count(store, &f), ==, 1);
    f.patch_type = NULL;

    f.status = "good";
    g_assert_cmpint(run_count(store, &f), ==, 3);
    f.status = NULL;

    f.format = "zip";
    g_assert_cmpint(run_count(store, &f), ==, 1);
    f.format = NULL;

    f.tags = (const char *[]){ "rpg" };
    f.n_tags = 1;
    g_assert_cmpint(run_count(store, &f), ==, 1);
    f.tags = NULL;
    f.n_tags = 0;

    f.kw = "mario";
    g_assert_cmpint(run_count(store, &f), ==, 1);

    close_temp_store(store, dir, path);
}

static void test_sort_and_paging(void)
{
    gchar *dir = NULL, *path = NULL;
    RhStore *store = open_temp_store(&dir, &path);
    seed(store);

    RhFilter f;
    memset(&f, 0, sizeof f);
    f.multi = -1;
    f.has_patch = -1;
    f.limit = 2;

    RhErr err = RH_OK;
    f.sort = "rating";
    f.desc = 1;
    RhQueryResult *result = rh_query_run(store, &f, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpint(result->total, ==, 5);
    g_assert_cmpuint(result->games->len, ==, 2);
    g_assert_cmpstr(((RhGame *)g_ptr_array_index(result->games, 0))->id, ==, "a0000003");
    g_assert_cmpstr(((RhGame *)g_ptr_array_index(result->games, 1))->id, ==, "a0000001");
    rh_query_result_free(result);

    f.sort = "year";
    f.desc = 0;
    f.offset = 2;
    f.limit = 2;
    result = rh_query_run(store, &f, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpstr(((RhGame *)g_ptr_array_index(result->games, 0))->id, ==, "a0000004");
    g_assert_cmpstr(((RhGame *)g_ptr_array_index(result->games, 1))->id, ==, "a0000003");
    rh_query_result_free(result);

    f.sort = "discs";
    f.desc = 1;
    f.offset = 0;
    f.limit = 1;
    result = rh_query_run(store, &f, &err);
    g_assert_cmpstr(((RhGame *)g_ptr_array_index(result->games, 0))->id, ==, "a0000003");
    rh_query_result_free(result);

    f.sort = "name";
    f.desc = 0;
    f.limit = 0;
    result = rh_query_run(store, &f, &err);
    g_assert_cmpstr(((RhGame *)g_ptr_array_index(result->games, 0))->name, ==, "Chrono Trigger");
    rh_query_result_free(result);

    f.sort = "bogus";
    g_assert_null(rh_query_run(store, &f, &err));
    g_assert_cmpint(err, ==, RH_ERR_INVALID);

    close_temp_store(store, dir, path);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/query/filters", test_filters);
    g_test_add_func("/query/sort-and-paging", test_sort_and_paging);
    return g_test_run();
}
