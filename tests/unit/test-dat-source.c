#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "dat-source.h"

static gchar *tmp_dir = NULL;

static gchar *write_manifest(const char *content)
{
    gchar *path = g_build_filename(tmp_dir, "sources.conf", NULL);
    GError *error = NULL;
    g_assert_true(g_file_set_contents(path, content, -1, &error));
    g_assert_no_error(error);
    return path;
}

static void test_valid(void)
{
    const char *content =
        "[meta]\n"
        "spec_version=1\n"
        "\n"
        "# 注释与空行\n"
        "[nes]\n"
        "name=Nintendo Entertainment System\n"
        "file=Nintendo - Nintendo Entertainment System.dat\n"
        "url=https://example.com/a%20b.dat\n"
        "url=https://mirror.example.com/a.dat\n"
        "sha1=0123456789ABCDEF0123456789ABCDEF01234567\n"
        "unknown=ignored\n"
        "\n"
        "[gbc]\n"
        "file=Game Boy Color.dat\n"
        "url=https://example.com/gbc.dat\n";

    gchar *path = write_manifest(content);
    GPtrArray *sources = NULL;
    gchar *message = NULL;
    RhErr err = rh_dat_sources_load(path, &sources, &message);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_null(message);
    g_assert_cmpuint(sources->len, ==, 2);

    RhDatSource *nes = g_ptr_array_index(sources, 0);
    g_assert_cmpstr(nes->platform, ==, "nes");
    g_assert_cmpstr(nes->name, ==, "Nintendo Entertainment System");
    g_assert_cmpstr(nes->file, ==, "Nintendo - Nintendo Entertainment System.dat");
    g_assert_cmpuint(nes->urls->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(nes->urls, 1), ==,
                    "https://mirror.example.com/a.dat");
    g_assert_cmpstr(nes->sha1, ==,
                    "0123456789abcdef0123456789abcdef01234567");

    /* name 缺省时由文件名去掉 .dat 得到 */
    RhDatSource *gbc = g_ptr_array_index(sources, 1);
    g_assert_cmpstr(gbc->platform, ==, "gbc");
    g_assert_cmpstr(gbc->name, ==, "Game Boy Color");
    g_assert_null(gbc->sha1);

    g_ptr_array_unref(sources);
    g_free(path);
}

static void assert_invalid(const char *content, const char *needle)
{
    gchar *path = write_manifest(content);
    GPtrArray *sources = NULL;
    gchar *message = NULL;
    RhErr err = rh_dat_sources_load(path, &sources, &message);

    g_assert_cmpint(err, ==, RH_ERR_INVALID);
    g_assert_null(sources);
    g_assert_nonnull(message);
    if (needle != NULL)
        g_assert_nonnull(strstr(message, needle));

    g_free(message);
    g_free(path);
}

static void test_duplicate_platform(void)
{
    assert_invalid("[nes]\nfile=a.dat\nurl=https://x/a.dat\n"
                   "[nes]\nfile=b.dat\nurl=https://x/b.dat\n",
                   "重复");
}

static void test_bad_file(void)
{
    assert_invalid("[nes]\nfile=sub/a.dat\nurl=https://x/a.dat\n", "路径分隔符");
    assert_invalid("[nes]\nurl=https://x/a.dat\n", "缺少 file");
}

static void test_bad_url(void)
{
    assert_invalid("[nes]\nfile=a.dat\nurl=ftp://x/a.dat\n", "http");
    assert_invalid("[nes]\nfile=a.dat\n", "至少需要一条 url");
}

static void test_bad_sha1(void)
{
    assert_invalid("[nes]\nfile=a.dat\nurl=https://x/a.dat\nsha1=abc\n",
                   "40 位");
}

static void test_bad_platform(void)
{
    assert_invalid("[NES]\nfile=a.dat\nurl=https://x/a.dat\n", "小写");
}

static void test_spec_version(void)
{
    assert_invalid("[meta]\nspec_version=2\n[nes]\nfile=a.dat\n"
                   "url=https://x/a.dat\n",
                   "spec_version");
    assert_invalid("[meta]\nspec_version=abc\n", "spec_version");
}

static void test_key_before_section(void)
{
    assert_invalid("file=a.dat\n", "平台段之前");
}

static void test_default_path_env(void)
{
    const char *content = "[nes]\nfile=a.dat\nurl=https://x/a.dat\n";
    gchar *path = write_manifest(content);
    g_assert_true(g_setenv("ROM_HAMSTER_DAT_SOURCES", path, TRUE));

    gchar *resolved = rh_dat_sources_default_path();
    g_assert_cmpstr(resolved, ==, path);
    g_free(resolved);

    g_unsetenv("ROM_HAMSTER_DAT_SOURCES");
    g_free(path);
}

#ifdef RH_TEST_DATA_DIR
static void test_shipped_manifest(void)
{
    gchar *path = g_build_filename(RH_TEST_DATA_DIR, "dats", "sources.conf", NULL);
    GPtrArray *sources = NULL;
    gchar *message = NULL;
    RhErr err = rh_dat_sources_load(path, &sources, &message);

    g_assert_cmpint(err, ==, RH_OK);
    g_assert_null(message);
    g_assert_cmpuint(sources->len, ==, 27);

    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < sources->len; i++) {
        RhDatSource *source = g_ptr_array_index(sources, i);
        g_assert_false(g_hash_table_contains(seen, source->platform));
        g_hash_table_add(seen, source->platform);
        g_assert_cmpuint(source->urls->len, >=, 2);
    }
    g_hash_table_destroy(seen);

    g_ptr_array_unref(sources);
    g_free(path);
}
#endif

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    GError *error = NULL;
    tmp_dir = g_dir_make_tmp("rh-dat-source-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmp_dir);

    g_test_add_func("/dat-source/valid", test_valid);
    g_test_add_func("/dat-source/duplicate-platform", test_duplicate_platform);
    g_test_add_func("/dat-source/bad-file", test_bad_file);
    g_test_add_func("/dat-source/bad-url", test_bad_url);
    g_test_add_func("/dat-source/bad-sha1", test_bad_sha1);
    g_test_add_func("/dat-source/bad-platform", test_bad_platform);
    g_test_add_func("/dat-source/spec-version", test_spec_version);
    g_test_add_func("/dat-source/key-before-section", test_key_before_section);
    g_test_add_func("/dat-source/default-path-env", test_default_path_env);
#ifdef RH_TEST_DATA_DIR
    g_test_add_func("/dat-source/shipped-manifest", test_shipped_manifest);
#endif

    int result = g_test_run();

    gchar *manifest = g_build_filename(tmp_dir, "sources.conf", NULL);
    g_remove(manifest);
    g_free(manifest);
    g_rmdir(tmp_dir);
    g_free(tmp_dir);

    return result;
}
