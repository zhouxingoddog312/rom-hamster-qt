#include <glib.h>

#include "classify.h"
#include "rh-test-main.h"

#define DATA_FILE RH_TEST_DATA_DIR "/platforms.conf"

static RhPlatformRules *load(void)
{
    RhErr err = RH_ERR_INVALID;
    RhPlatformRules *rules = rh_platform_rules_load(DATA_FILE, &err, NULL);
    g_assert_cmpint(err, ==, RH_OK);
    return rules;
}

static void test_rom(void)
{
    RhPlatformRules *rules = load();

    RhClassifyResult *r = rh_classify_file(rules, "Tetris (World).GB");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_ROM);
    g_assert_cmpstr(r->extension, ==, ".gb");
    g_assert_false(r->ambiguous);
    g_assert_cmpuint(r->candidates->len, ==, 1);
    g_assert_cmpstr(((RhPlatform *)g_ptr_array_index(r->candidates, 0))->id, ==, "gb");
    g_assert_null(r->media_kind);
    rh_classify_result_free(r);

    /* .bin 天然二义，必须标记 */
    r = rh_classify_file(rules, "game.bin");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_ROM);
    g_assert_true(r->ambiguous);
    g_assert_cmpuint(r->candidates->len, >, 1);
    rh_classify_result_free(r);

    /* 复合后缀优先于媒体后缀 */
    r = rh_classify_file(rules, "celeste.p8.png");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_ROM);
    g_assert_cmpuint(r->candidates->len, ==, 1);
    g_assert_cmpstr(((RhPlatform *)g_ptr_array_index(r->candidates, 0))->id, ==, "pico-8");
    rh_classify_result_free(r);

    rh_platform_rules_free(rules);
}

static void test_archive_patch_media(void)
{
    RhPlatformRules *rules = load();

    RhClassifyResult *r = rh_classify_file(rules, "collection.zip");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_ARCHIVE);
    g_assert_cmpuint(r->candidates->len, ==, 0);
    rh_classify_result_free(r);

    r = rh_classify_file(rules, "translation.ips");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_PATCH);
    g_assert_nonnull(r->patch_platform);
    rh_classify_result_free(r);

    r = rh_classify_file(rules, "cover.PNG");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_MEDIA);
    g_assert_cmpstr(r->media_kind, ==, "covers");
    rh_classify_result_free(r);

    r = rh_classify_file(rules, "clip.mp4");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_MEDIA);
    g_assert_cmpstr(r->media_kind, ==, "videos");
    rh_classify_result_free(r);

    r = rh_classify_file(rules, "notes.xyz");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_UNKNOWN);
    rh_classify_result_free(r);

    r = rh_classify_file(NULL, "Tetris.gb");
    g_assert_cmpint(r->kind, ==, RH_CLASSIFY_UNKNOWN);
    g_assert_null(r->extension);
    rh_classify_result_free(r);

    g_assert_null(rh_classify_media_kind(rules, "notes.xyz"));
    g_assert_cmpstr(rh_classify_media_kind(rules, "a.webp"), ==, "covers");

    rh_classify_result_free(NULL);
    rh_platform_rules_free(rules);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/classify/rom", test_rom);
    g_test_add_func("/classify/archive-patch-media", test_archive_patch_media);
    return g_test_run();
}
