#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "library.h"
#include "rh-test-main.h"

static gchar *write_file(const char *dir, const char *name, const char *content)
{
    gchar *path_dir = g_build_filename(dir, NULL);
    gchar *path = g_build_filename(dir, name, NULL);
    g_assert_cmpint(g_mkdir_with_parents(path_dir, 0755), ==, 0);
    g_assert_true(g_file_set_contents(path, content, -1, NULL));
    g_free(path_dir);
    return path;
}

static RhLibraryEntry *find_entry(GPtrArray *entries, const char *name)
{
    for (guint i = 0; i < entries->len; i++) {
        RhLibraryEntry *entry = g_ptr_array_index(entries, i);
        if (g_strcmp0(entry->name, name) == 0)
            return entry;
    }
    return NULL;
}

static void test_scan_gamelist_and_pegasus(void)
{
    gchar *root = g_dir_make_tmp("romham-lib-XXXXXX", NULL);
    g_assert_nonnull(root);

    gchar *gb_dir = g_build_filename(root, "gb", NULL);
    gchar *gb_xml = write_file(gb_dir,
        "gamelist.xml",
        "<?xml version=\"1.0\"?>\n"
        "<gameList>\n"
        "  <game>\n"
        "    <path>./Tetris (World).gb</path>\n"
        "    <name>Tetris</name>\n"
        "    <desc>Puzzle classic</desc>\n"
        "    <developer>Nintendo</developer>\n"
        "    <publisher>Nintendo</publisher>\n"
        "    <genre>Puzzle</genre>\n"
        "    <releasedate>19890614T000000</releasedate>\n"
        "    <players>2</players>\n"
        "    <rating>0.85</rating>\n"
        "    <image>./images/tetris.png</image>\n"
        "  </game>\n"
        "  <game>\n"
        "    <path>Zelda.gb</path>\n"
        "    <name>Zelda</name>\n"
        "  </game>\n"
        "</gameList>\n");

    gchar *snes_dir = g_build_filename(root, "snes", NULL);
    gchar *pegasus = write_file(snes_dir,
        "metadata.pegasus.txt",
        "game: Super Metroid\n"
        "file: Super Metroid.sfc\n"
        "description: First line\n"
        "  second line\n"
        "developer: Nintendo R&D1\n"
        "release: 1994-03-19\n"
        "players: 1\n"
        "rating: 0.95\n"
        "assets.boxFront: box.png\n"
        "assets.screenshot: shot.png\n"
        "assets.video: video.mp4\n"
        "\n"
        "game: F-Zero\n"
        "file: ./F-Zero.sfc\n");

    RhErr err = RH_ERR_INVALID;
    GPtrArray *entries = rh_library_scan(root, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(entries->len, ==, 4);

    RhLibraryEntry *tetris = find_entry(entries, "Tetris");
    g_assert_nonnull(tetris);
    g_assert_cmpstr(tetris->path, ==, "Tetris (World).gb");
    g_assert_cmpstr(tetris->platform_hint, ==, "gb");
    g_assert_cmpstr(tetris->source, ==, "gamelist");
    g_assert_cmpint(tetris->year, ==, 1989);
    g_assert_cmpint(tetris->players, ==, 2);
    g_assert_cmpfloat(tetris->rating, ==, 0.85);
    g_assert_cmpstr(tetris->image, ==, "images/tetris.png");

    RhLibraryEntry *zelda = find_entry(entries, "Zelda");
    g_assert_nonnull(zelda);
    g_assert_cmpstr(zelda->path, ==, "Zelda.gb");

    RhLibraryEntry *metroid = find_entry(entries, "Super Metroid");
    g_assert_nonnull(metroid);
    g_assert_cmpstr(metroid->source, ==, "pegasus");
    g_assert_cmpstr(metroid->path, ==, "Super Metroid.sfc");
    g_assert_cmpstr(metroid->platform_hint, ==, "snes");
    g_assert_cmpint(metroid->year, ==, 1994);
    g_assert_true(g_str_has_prefix(metroid->description, "First line"));
    g_assert_true(strstr(metroid->description, "second line") != NULL);
    g_assert_cmpstr(metroid->image, ==, "shot.png");
    g_assert_cmpstr(metroid->video, ==, "video.mp4");

    RhLibraryEntry *fzero = find_entry(entries, "F-Zero");
    g_assert_nonnull(fzero);
    g_assert_cmpstr(fzero->path, ==, "F-Zero.sfc");

    g_ptr_array_free(entries, TRUE);
    g_free(gb_xml);
    g_free(gb_dir);
    g_free(pegasus);
    g_free(snes_dir);

    gchar *gb_xml_path = g_build_filename(root, "gb", "gamelist.xml", NULL);
    gchar *pegasus_path = g_build_filename(root, "snes", "metadata.pegasus.txt", NULL);
    g_remove(gb_xml_path);
    g_remove(pegasus_path);
    g_free(gb_xml_path);
    g_free(pegasus_path);
    gchar *gb_dir2 = g_build_filename(root, "gb", NULL);
    gchar *snes_dir2 = g_build_filename(root, "snes", NULL);
    g_rmdir(gb_dir2);
    g_rmdir(snes_dir2);
    g_free(gb_dir2);
    g_free(snes_dir2);
    g_rmdir(root);
    g_free(root);
}

static void test_scan_errors_and_empty(void)
{
    RhErr err = RH_OK;
    GPtrArray *none = rh_library_scan("/nonexistent/roms", &err);
    g_assert_cmpint(err, ==, RH_ERR_IO);
    g_assert_cmpuint(none->len, ==, 0);
    g_ptr_array_free(none, TRUE);

    g_assert_cmpuint(rh_library_scan(NULL, &err)->len, ==, 0);
    g_assert_cmpint(err, ==, RH_ERR_INVALID);

    gchar *root = g_dir_make_tmp("romham-lib-empty-XXXXXX", NULL);
    GPtrArray *empty = rh_library_scan(root, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(empty->len, ==, 0);

    /* 损坏的 gamelist 不应中断扫描，只是跳过 */
    gchar *gb_dir = g_build_filename(root, "gb", NULL);
    gchar *bad = write_file(gb_dir, "gamelist.xml", "<gameList><game>");
    g_test_expect_message("romham/library", G_LOG_LEVEL_WARNING, "*无法解析 gamelist*");
    GPtrArray *broken = rh_library_scan(root, &err);
    g_test_assert_expected_messages();
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpuint(broken->len, ==, 0);

    g_ptr_array_free(empty, TRUE);
    g_ptr_array_free(broken, TRUE);
    g_free(bad);
    gchar *bad_path = g_build_filename(root, "gb", "gamelist.xml", NULL);
    g_remove(bad_path);
    g_free(bad_path);
    g_free(gb_dir);
    gchar *gb_dir2 = g_build_filename(root, "gb", NULL);
    g_rmdir(gb_dir2);
    g_free(gb_dir2);
    g_rmdir(root);
    g_free(root);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/library/scan-gamelist-and-pegasus", test_scan_gamelist_and_pegasus);
    g_test_add_func("/library/scan-errors-and-empty", test_scan_errors_and_empty);
    return g_test_run();
}
