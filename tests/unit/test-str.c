#include <glib.h>

#include "rh-str.h"
#include "rh-test-main.h"

static void test_extension(void)
{
    gchar *ext = rh_str_extension("Game.GB");
    g_assert_cmpstr(ext, ==, ".gb");
    g_free(ext);

    ext = rh_str_extension("pico.p8.png");
    g_assert_cmpstr(ext, ==, ".png");
    g_free(ext);

    g_assert_null(rh_str_extension(".bashrc"));
    g_assert_null(rh_str_extension("README"));
    g_assert_null(rh_str_extension(NULL));

    ext = rh_str_extension("a/b.Game.GB");
    g_assert_cmpstr(ext, ==, ".gb");
    g_free(ext);
}

static void test_stem(void)
{
    gchar *stem = rh_str_stem("a/b.Game.GB");
    g_assert_cmpstr(stem, ==, "a/b.Game");
    g_free(stem);

    stem = rh_str_stem(".bashrc");
    g_assert_cmpstr(stem, ==, ".bashrc");
    g_free(stem);

    stem = rh_str_stem("noext");
    g_assert_cmpstr(stem, ==, "noext");
    g_free(stem);
}

static void test_basename(void)
{
    g_assert_cmpstr(rh_str_basename("a/b/c.gb"), ==, "c.gb");
    g_assert_cmpstr(rh_str_basename("c.gb"), ==, "c.gb");
    g_assert_null(rh_str_basename(NULL));
}

static void test_norm_key(void)
{
    gchar *key = rh_str_norm_key("Final Fantasy VII (USA)");
    g_assert_cmpstr(key, ==, "finalfantasyviiusa");
    g_free(key);

    key = rh_str_norm_key("  超级 马力欧  ");
    g_assert_cmpstr(key, ==, "超级马力欧");
    g_free(key);

    key = rh_str_norm_key(NULL);
    g_assert_cmpstr(key, ==, "");
    g_free(key);
}

static void test_similarity(void)
{
    g_assert_cmpint(rh_str_similarity("Sonic the Hedgehog", "Sonic the Hedgehog"), ==, 100);
    g_assert_cmpint(rh_str_similarity("", ""), ==, 100);
    g_assert_cmpint(rh_str_similarity("Sonic", ""), ==, 0);
    g_assert_cmpint(rh_str_similarity("Sonic the Hedgehog", "Sonic the Hedgehog 2"), >, 80);
    g_assert_cmpint(rh_str_similarity("Sonic", "Zelda"), <, 50);

    int ab = rh_str_similarity("Mario Kart 64", "MarioKart64 (USA)");
    int ba = rh_str_similarity("MarioKart64 (USA)", "Mario Kart 64");
    g_assert_cmpint(ab, ==, ba);
}

static void test_parse_disc(void)
{
    int disc = 0;

    g_assert_true(rh_str_parse_disc("Game (Disc 2).cue", &disc));
    g_assert_cmpint(disc, ==, 2);

    g_assert_true(rh_str_parse_disc("Game (Disk 1 of 2).chd", &disc));
    g_assert_cmpint(disc, ==, 1);

    g_assert_true(rh_str_parse_disc("Game [CD3].bin", &disc));
    g_assert_cmpint(disc, ==, 3);

    g_assert_true(rh_str_parse_disc("Game - Disc 1.iso", &disc));
    g_assert_cmpint(disc, ==, 1);

    g_assert_true(rh_str_parse_disc("Disc1.bin", &disc));
    g_assert_cmpint(disc, ==, 1);

    g_assert_true(rh_str_parse_disc("Sonic CD (Disc 2).chd", &disc));
    g_assert_cmpint(disc, ==, 2);

    g_assert_false(rh_str_parse_disc("Game v1.2.gb", &disc));
    g_assert_false(rh_str_parse_disc("discover 2.gb", &disc));
    g_assert_false(rh_str_parse_disc(NULL, &disc));
}

static void test_reserved_name(void)
{
    g_assert_true(rh_str_is_reserved_name("CON"));
    g_assert_true(rh_str_is_reserved_name("con.txt"));
    g_assert_true(rh_str_is_reserved_name("COM1"));
    g_assert_true(rh_str_is_reserved_name("LPT9.rom"));
    g_assert_false(rh_str_is_reserved_name("CONSOLE"));
    g_assert_false(rh_str_is_reserved_name("COM0"));
    g_assert_false(rh_str_is_reserved_name(""));
}

static void test_sanitize(void)
{
    gchar *s = rh_str_sanitize_component("a<b>c:d\"e/f\\g|h?i*j");
    g_assert_cmpstr(s, ==, "a_b_c_d_e_f_g_h_i_j");
    g_free(s);

    s = rh_str_sanitize_component("trail.  ");
    g_assert_cmpstr(s, ==, "trail");
    g_free(s);

    s = rh_str_sanitize_component("CON");
    g_assert_cmpstr(s, ==, "_CON");
    g_free(s);

    s = rh_str_sanitize_component("");
    g_assert_cmpstr(s, ==, "_");
    g_free(s);

    s = rh_str_sanitize_component("超级马力欧 世界");
    g_assert_cmpstr(s, ==, "超级马力欧 世界");
    g_free(s);

    /* 超过 120 个字符要截断，且保持 UTF-8 完整 */
    gchar *long_name = g_strnfill(200, 'x');
    s = rh_str_sanitize_component(long_name);
    g_assert_true(g_utf8_validate(s, -1, NULL));
    g_assert_cmpint((int)g_utf8_strlen(s, -1), ==, 120);
    g_free(s);
    g_free(long_name);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/str/extension", test_extension);
    g_test_add_func("/str/stem", test_stem);
    g_test_add_func("/str/basename", test_basename);
    g_test_add_func("/str/norm-key", test_norm_key);
    g_test_add_func("/str/similarity", test_similarity);
    g_test_add_func("/str/parse-disc", test_parse_disc);
    g_test_add_func("/str/reserved-name", test_reserved_name);
    g_test_add_func("/str/sanitize", test_sanitize);
    return g_test_run();
}
