#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "platform.h"
#include "rh-test-main.h"

#define DATA_FILE RH_TEST_DATA_DIR "/platforms.conf"

static RhPlatformRules *load(void)
{
    RhErr err = RH_ERR_INVALID;
    gchar *message = NULL;
    RhPlatformRules *rules = rh_platform_rules_load(DATA_FILE, &err, &message);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(rules);
    g_assert_null(message);
    return rules;
}

static void test_load_all(void)
{
    RhPlatformRules *rules = load();
    g_assert_cmpint(rh_platform_rules_version(rules), ==, 1);
    g_assert_cmpuint(rh_platform_count(rules), ==, 101);
    g_assert_cmpuint(rh_platform_all(rules)->len, ==, 101);

    RhPlatform *gb = rh_platform_by_id(rules, "gb");
    g_assert_nonnull(gb);
    g_assert_cmpstr(gb->id, ==, "gb");
    g_assert_true(gb->compress_supported);
    g_assert_false(gb->arcade);
    g_assert_cmpstr(gb->recommended_format, ==, "zip");
    g_assert_cmpuint(gb->aliases->len, >, 0);

    /* 别名与大小写 */
    g_assert_true(rh_platform_by_id(rules, "GameBoy") == gb);
    g_assert_true(rh_platform_by_id(rules, "superfamicom") == rh_platform_by_id(rules, "snes"));

    /* 不带连字符与带连字符的 ID 都要能取到 */
    g_assert_nonnull(rh_platform_by_id(rules, "sg-1000"));
    g_assert_nonnull(rh_platform_by_id(rules, "pico-8"));
    g_assert_nonnull(rh_platform_by_id(rules, "chip-8"));

    RhPlatform *mame = rh_platform_by_id(rules, "mame");
    g_assert_nonnull(mame);
    g_assert_true(mame->arcade);
    g_assert_cmpuint(mame->patch_formats->len, ==, 2);

    rh_platform_rules_free(rules);
}

static void test_candidates(void)
{
    RhPlatformRules *rules = load();

    GPtrArray *one = rh_platform_candidates_for_file(rules, "Tetris (World).GB");
    g_assert_cmpuint(one->len, ==, 1);
    g_assert_cmpstr(((RhPlatform *)g_ptr_array_index(one, 0))->id, ==, "gb");
    g_ptr_array_free(one, TRUE);

    /* .bin 天然二义：应返回多个平台 */
    GPtrArray *many = rh_platform_candidates_for_file(rules, "game.bin");
    g_assert_cmpuint(many->len, >, 1);
    g_ptr_array_free(many, TRUE);

    /* 复合后缀 .p8.png 只应命中 pico-8 */
    GPtrArray *pico = rh_platform_candidates_for_file(rules, "celeste.p8.png");
    g_assert_cmpuint(pico->len, ==, 1);
    g_assert_cmpstr(((RhPlatform *)g_ptr_array_index(pico, 0))->id, ==, "pico-8");
    g_ptr_array_free(pico, TRUE);

    GPtrArray *none = rh_platform_candidates_for_file(rules, "readme.txt");
    g_assert_cmpuint(none->len, ==, 0);
    g_ptr_array_free(none, TRUE);

    RhPlatform *n64 = rh_platform_by_id(rules, "n64");
    g_assert_true(rh_platform_matches_file(n64, "Mario.Z64"));
    g_assert_false(rh_platform_matches_file(n64, "Mario.gb"));
    g_assert_false(rh_platform_matches_file(NULL, "Mario.z64"));

    rh_platform_rules_free(rules);
}

static void test_archive_and_media(void)
{
    RhPlatformRules *rules = load();

    g_assert_true(rh_platform_is_archive(rules, "rom.zip"));
    g_assert_true(rh_platform_is_archive(rules, "rom.7Z"));
    g_assert_false(rh_platform_is_archive(rules, "rom.gb"));

    g_assert_true(rh_platform_is_arcade_rom_member(rules, "sf2.rom"));
    g_assert_false(rh_platform_is_arcade_rom_member(rules, "sf2.zip"));

    g_assert_cmpstr(rh_platform_media_dir(rules, "covers"), ==, "covers");
    g_assert_cmpstr(rh_platform_media_dir(rules, "videos"), ==, "videos");
    g_assert_null(rh_platform_media_dir(rules, "nope"));

    g_assert_true(rh_platform_media_suffix_allowed(rules, "covers", "a.PNG"));
    g_assert_false(rh_platform_media_suffix_allowed(rules, "covers", "a.mp4"));
    g_assert_true(rh_platform_media_suffix_allowed(rules, "videos", "a.mp4"));
    g_assert_false(rh_platform_media_suffix_allowed(rules, "videos", "a.png"));
    g_assert_false(rh_platform_media_suffix_allowed(rules, "nope", "a.png"));

    rh_platform_rules_free(rules);
}

static void test_errors(void)
{
    RhErr err = RH_OK;
    gchar *message = NULL;

    g_assert_null(rh_platform_rules_load("/nonexistent/platforms.conf", &err, &message));
    g_assert_cmpint(err, ==, RH_ERR_IO);
    g_assert_nonnull(message);
    g_clear_pointer(&message, g_free);

    gchar *dir = g_dir_make_tmp("romham-rules-XXXXXX", NULL);
    gchar *path = g_build_filename(dir, "bad.conf", NULL);
    g_assert_true(g_file_set_contents(path,
                                      "[platform_extensions]\n"
                                      "gb .gb\n",
                                      -1, NULL));

    g_assert_null(rh_platform_rules_load(path, &err, &message));
    g_assert_cmpint(err, ==, RH_ERR_INVALID);
    g_assert_nonnull(message);
    g_assert_true(strstr(message, ":2:") != NULL);
    g_clear_pointer(&message, g_free);

    g_assert_true(g_file_set_contents(path, "[meta]\nspec_version=1\n", -1, NULL));
    g_assert_null(rh_platform_rules_load(path, &err, &message));
    g_assert_cmpint(err, ==, RH_ERR_INVALID);

    g_remove(path);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);

    g_assert_null(rh_platform_rules_load(NULL, &err, NULL));
    g_assert_cmpint(err, ==, RH_ERR_INVALID);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/platform/load-all", test_load_all);
    g_test_add_func("/platform/candidates", test_candidates);
    g_test_add_func("/platform/archive-and-media", test_archive_and_media);
    g_test_add_func("/platform/errors", test_errors);
    return g_test_run();
}
