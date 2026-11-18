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

static RhArchiveClassifyResult *classify_entries(const RhPlatformRules *rules,
                                                 const char *const *names,
                                                 guint count)
{
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < count; i++)
        g_ptr_array_add(paths, g_strdup(names[i]));
    RhArchiveClassifyResult *result = rh_classify_archive(rules, paths);
    g_ptr_array_free(paths, TRUE);
    return result;
}

static void test_archive_branches(void)
{
    RhPlatformRules *rules = load();

    /* PSV：出现 sce_sys/ 或 PCSH* 路径，整体入库 */
    const char *psv[] = { "PCSH00001/sce_sys/param.sfo", "PCSH00001/game.vpk" };
    RhArchiveClassifyResult *r = classify_entries(rules, psv, G_N_ELEMENTS(psv));
    g_assert_cmpint(r->branch, ==, RH_ARCHIVE_PSV);
    rh_archive_classify_free(r);

    /* 纯补丁包 */
    const char *patches[] = { "fix.ips", "fix.bps" };
    r = classify_entries(rules, patches, G_N_ELEMENTS(patches));
    g_assert_cmpint(r->branch, ==, RH_ARCHIVE_PATCHES);
    g_assert_cmpuint(r->rom_count, ==, 0);
    rh_archive_classify_free(r);

    /* 街机 ZIP：多个成员后缀命中 [arcade_rom_suffixes] */
    const char *arcade[] = { "sf2.rom", "sf2.bin", "sf2.z80", "readme.txt" };
    r = classify_entries(rules, arcade, G_N_ELEMENTS(arcade));
    g_assert_cmpint(r->branch, ==, RH_ARCHIVE_ARCADE);
    rh_archive_classify_free(r);

    /* 二义性包：.bin 命中多个平台 */
    const char *ambiguous[] = { "game.bin" };
    r = classify_entries(rules, ambiguous, G_N_ELEMENTS(ambiguous));
    g_assert_cmpint(r->branch, ==, RH_ARCHIVE_AMBIGUOUS);
    g_assert_true(r->ambiguous);
    rh_archive_classify_free(r);

    /* 单 ROM 包：平台可从内部条目唯一确定 */
    const char *single[] = { "Tetris (World).gb" };
    r = classify_entries(rules, single, G_N_ELEMENTS(single));
    g_assert_cmpint(r->branch, ==, RH_ARCHIVE_SINGLE_ROM);
    g_assert_cmpstr(r->platform, ==, "gb");
    rh_archive_classify_free(r);

    /* 混合包：ROM 与媒体同包 */
    const char *mixed[] = { "Tetris (World).gb", "cover.png" };
    r = classify_entries(rules, mixed, G_N_ELEMENTS(mixed));
    g_assert_cmpint(r->branch, ==, RH_ARCHIVE_MIXED);
    rh_archive_classify_free(r);

    /* 目录条目不参与计数 */
    const char *with_dirs[] = { "gfx/", "gfx/Tetris (World).gb" };
    r = classify_entries(rules, with_dirs, G_N_ELEMENTS(with_dirs));
    g_assert_cmpint(r->branch, ==, RH_ARCHIVE_SINGLE_ROM);
    rh_archive_classify_free(r);

    rh_platform_rules_free(rules);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/classify/rom", test_rom);
    g_test_add_func("/classify/archive-patch-media", test_archive_patch_media);
    g_test_add_func("/classify/archive-branches", test_archive_branches);
    return g_test_run();
}
