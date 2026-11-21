#include <glib.h>
#include <string.h>

#include "rh-fname.h"
#include "rh-test-main.h"

/* "游戏.nes" 的 GBK 字节 */
static const gchar GBK_GAME_NES[] = "\xD3\xCE\xCF\xB7.nes";

static void assert_raw_equals(const RhFName *name, const gchar *bytes, gsize len)
{
    gsize raw_len = 0;
    const gchar *raw = g_bytes_get_data(name->raw, &raw_len);
    g_assert_cmpuint(raw_len, ==, len);
    /* 空名字时 GBytes 的数据指针可能是 NULL，memcmp(NULL, "", 0) 属于未定义行为。 */
    if (len > 0)
        g_assert_cmpint(memcmp(raw, bytes, len), ==, 0);
}

static void test_utf8_passthrough(void)
{
    RhFName *name = rh_fname_from_utf8("中文/游戏.nes");
    g_assert_nonnull(name);
    g_assert_cmpint(name->encoding, ==, RH_FNAME_UTF8);
    g_assert_cmpstr(name->display, ==, "中文/游戏.nes");
    g_assert_false(rh_fname_is_guessed(name));
    g_assert_true(rh_fname_is_locatable(name));
    assert_raw_equals(name, "中文/游戏.nes", strlen("中文/游戏.nes"));

    /* 合法 UTF-8 在双端都能原样定位 */
    gchar *native = rh_fname_native_name(name);
    g_assert_cmpstr(native, ==, "中文/游戏.nes");
    g_free(native);
    rh_fname_free(name);

    /* 空名字也是合法 UTF-8，不特殊处理 */
    name = rh_fname_from_utf8("");
    g_assert_cmpint(name->encoding, ==, RH_FNAME_UTF8);
    g_assert_cmpstr(name->display, ==, "");
    assert_raw_equals(name, "", 0);
    rh_fname_free(name);
}

static void test_gb18030_guessed(void)
{
    RhFName *name = rh_fname_from_bytes(GBK_GAME_NES, -1);
    g_assert_nonnull(name);
    g_assert_cmpint(name->encoding, ==, RH_FNAME_GB18030);
    g_assert_cmpstr(name->display, ==, "游戏.nes");
    g_assert_true(rh_fname_is_guessed(name));
    g_assert_true(rh_fname_is_locatable(name));
    assert_raw_equals(name, GBK_GAME_NES, strlen(GBK_GAME_NES));

    /* 猜测值只是显示；原始字节必须还能拿回来 */
    gsize len = 0;
    gchar *raw = rh_fname_raw_string(name, &len);
    g_assert_cmpuint(len, ==, strlen(GBK_GAME_NES));
    g_assert_cmpint(memcmp(raw, GBK_GAME_NES, len), ==, 0);
    g_free(raw);

    /* POSIX 定位用原始字节；Windows 用解码后的 UTF-8 名 */
#ifdef _WIN32
    gchar *native = rh_fname_native_name(name);
    g_assert_cmpstr(native, ==, "游戏.nes");
    g_free(native);
#else
    gchar *native = rh_fname_native_name(name);
    g_assert_cmpint(strlen(native), ==, strlen(GBK_GAME_NES));
    g_assert_cmpint(memcmp(native, GBK_GAME_NES, strlen(GBK_GAME_NES)), ==, 0);
    g_free(native);
#endif

    rh_fname_free(name);
}

/* 猜错也要可逆：C4E3 在 GB18030 里是「你」，但原始字节可能是别的编码。 */
static void test_wrong_guess_reversible(void)
{
    static const gchar bytes[] = "\xC4\xE3.nes";
    RhFName *name = rh_fname_from_bytes(bytes, -1);
    g_assert_cmpint(name->encoding, ==, RH_FNAME_GB18030);
    g_assert_cmpstr(name->display, ==, "你.nes");
    g_assert_true(rh_fname_is_guessed(name));

    /* 显示名已经变了，但原始字节仍完整保留，可以还原磁盘上的真实名字 */
    assert_raw_equals(name, bytes, strlen(bytes));
    gsize len = 0;
    gchar *raw = rh_fname_raw_string(name, &len);
    g_assert_cmpint(memcmp(raw, bytes, strlen(bytes)), ==, 0);
    g_free(raw);
    rh_fname_free(name);
}

static void test_invalid_bytes_escaped(void)
{
    static const gchar bytes[] = "\xFF\xFE.nes";
    RhFName *name = rh_fname_from_bytes(bytes, -1);
    g_assert_cmpint(name->encoding, ==, RH_FNAME_ESCAPED);
    g_assert_cmpstr(name->display, ==, "%FF%FE.nes");
    g_assert_false(rh_fname_is_guessed(name));
    assert_raw_equals(name, bytes, strlen(bytes));

    /* 兜底表示里不得出现路径分隔符，否则解压/建目录会切出子目录 */
    g_assert_null(strchr(name->display, '/'));
    g_assert_null(strchr(name->display, '\\'));

    gsize len = 0;
    gchar *restored = rh_fname_unescape(name->display, &len);
    g_assert_nonnull(restored);
    g_assert_cmpuint(len, ==, strlen(bytes));
    g_assert_cmpint(memcmp(restored, bytes, len), ==, 0);
    g_free(restored);

#ifdef _WIN32
    /* 转义名只是展示，不代表磁盘上的真实文件；Windows 上不可定位 */
    g_assert_false(rh_fname_is_locatable(name));
    g_assert_null(rh_fname_native_name(name));
#else
    g_assert_true(rh_fname_is_locatable(name));
#endif

    rh_fname_free(name);
}

static void test_escape_roundtrip(void)
{
    /* 可打印 ASCII 原样保留；'%'、'\' 与非 ASCII 逐字节转义 */
    static const gchar bytes[] = { 'a', '%', '\\', 'b', 0x01, (gchar)0xFF, '.', 'g', 'b' };
    gchar *escaped = rh_fname_escape(bytes, sizeof bytes);
    g_assert_cmpstr(escaped, ==, "a%25%5Cb%01%FF.gb");

    gsize len = 0;
    gchar *restored = rh_fname_unescape(escaped, &len);
    g_assert_nonnull(restored);
    g_assert_cmpuint(len, ==, sizeof bytes);
    g_assert_cmpint(memcmp(restored, bytes, sizeof bytes), ==, 0);
    g_free(restored);

    /* 格式非法必须明确失败，不能悄悄给出半个结果 */
    g_assert_null(rh_fname_unescape("%4", NULL));
    g_assert_null(rh_fname_unescape("%GG", NULL));
    g_assert_null(rh_fname_unescape("abc\x80", NULL));
    g_free(escaped);

    /* 内嵌 NUL 也算不可解码，走转义而不是截断 */
    RhFName *name = rh_fname_from_bytes("ab\0cd", 5);
    g_assert_cmpint(name->encoding, ==, RH_FNAME_ESCAPED);
    g_assert_cmpstr(name->display, ==, "ab%00cd");
    assert_raw_equals(name, "ab\0cd", 5);
    rh_fname_free(name);
}

static void test_join_raw(void)
{
    GBytes *parent = g_bytes_new("gb", 2);
    GBytes *child = g_bytes_new(GBK_GAME_NES, strlen(GBK_GAME_NES));
    GBytes *joined = rh_fname_join_raw(parent, child);

    gsize len = 0;
    const gchar *data = g_bytes_get_data(joined, &len);
    g_assert_cmpuint(len, ==, 2 + 1 + strlen(GBK_GAME_NES));
    g_assert_cmpint(memcmp(data, "gb/", 3), ==, 0);
    g_assert_cmpint(memcmp(data + 3, GBK_GAME_NES, strlen(GBK_GAME_NES)), ==, 0);

    /* 空父路径：直接复制子路径 */
    GBytes *empty = g_bytes_new("", 0);
    GBytes *plain = rh_fname_join_raw(empty, child);
    gsize plain_len = 0;
    const gchar *plain_data = g_bytes_get_data(plain, &plain_len);
    g_assert_cmpuint(plain_len, ==, strlen(GBK_GAME_NES));
    g_assert_cmpint(memcmp(plain_data, GBK_GAME_NES, plain_len), ==, 0);

    g_bytes_unref(plain);
    g_bytes_unref(empty);
    g_bytes_unref(joined);
    g_bytes_unref(child);
    g_bytes_unref(parent);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);

    g_test_add_func("/fname/utf8-passthrough", test_utf8_passthrough);
    g_test_add_func("/fname/gb18030-guessed", test_gb18030_guessed);
    g_test_add_func("/fname/wrong-guess-reversible", test_wrong_guess_reversible);
    g_test_add_func("/fname/invalid-bytes-escaped", test_invalid_bytes_escaped);
    g_test_add_func("/fname/escape-roundtrip", test_escape_roundtrip);
    g_test_add_func("/fname/join-raw", test_join_raw);

    return g_test_run();
}
