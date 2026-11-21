#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "dat-fetch.h"

static gchar *tmp_dir = NULL;
static gchar *cache_dir = NULL;

static void setup(gpointer fixture, gconstpointer user_data)
{
    (void)fixture;
    (void)user_data;
    tmp_dir = g_dir_make_tmp("rh-dat-fetch-XXXXXX", NULL);
    g_assert_nonnull(tmp_dir);
    cache_dir = g_build_filename(tmp_dir, "cache", NULL);
}

static void teardown(gpointer fixture, gconstpointer user_data)
{
    (void)fixture;
    (void)user_data;
    gchar *local = g_build_filename(cache_dir, "Nintendo.dat", NULL);
    gchar *part = g_strconcat(local, ".part", NULL);
    g_remove(part);
    g_remove(local);
    g_free(part);
    g_free(local);
    g_rmdir(cache_dir);

    gchar *remote = g_build_filename(tmp_dir, "remote.dat", NULL);
    g_remove(remote);
    g_free(remote);

    g_rmdir(tmp_dir);
    g_free(cache_dir);
    g_free(tmp_dir);
    cache_dir = NULL;
    tmp_dir = NULL;
}

static gchar *write_remote(const char *content, gchar out_sha1[41])
{
    gchar *path = g_build_filename(tmp_dir, "remote.dat", NULL);
    GError *error = NULL;
    g_assert_true(g_file_set_contents(path, content, -1, &error));
    g_assert_no_error(error);

    if (out_sha1 != NULL) {
        gchar *digest = g_compute_checksum_for_data(
            G_CHECKSUM_SHA1, (const guchar *)content, strlen(content));
        memcpy(out_sha1, digest, 41);
        g_free(digest);
    }
    return path;
}

static RhDatSource *make_source(const char *url, const char *mirror,
                                const char *sha1)
{
    RhDatSource *source = g_new0(RhDatSource, 1);
    source->platform = g_strdup("nes");
    source->name = g_strdup("Nintendo Entertainment System");
    source->file = g_strdup("Nintendo.dat");
    source->urls = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(source->urls, g_strdup(url));
    if (mirror != NULL)
        g_ptr_array_add(source->urls, g_strdup(mirror));
    source->sha1 = sha1 != NULL ? g_strdup(sha1) : NULL;
    return source;
}

static gchar *local_path(void)
{
    return g_build_filename(cache_dir, "Nintendo.dat", NULL);
}

static void test_install_and_unchanged(gpointer fixture, gconstpointer user_data)
{
    (void)fixture;
    (void)user_data;
    gchar sha1[41];
    gchar *remote = write_remote("abcdef", sha1);
    gchar *url = g_filename_to_uri(remote, NULL, NULL);
    g_assert_nonnull(url);

    RhDatSource *source = make_source(url, NULL, sha1);
    RhDatFetchResult result;

    RhErr err = rh_dat_fetch(source, cache_dir, NULL, &result);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_true(result.changed);
    g_assert_true(result.installed);
    g_assert_cmpstr(result.sha1, ==, sha1);
    g_assert_cmpuint(result.bytes, ==, 6);
    g_assert_cmpint(result.mirrors_tried, ==, 1);

    gchar *local = local_path();
    gchar *content = NULL;
    g_assert_true(g_file_get_contents(local, &content, NULL, NULL));
    g_assert_cmpstr(content, ==, "abcdef");
    g_free(content);

    /* 清单 sha1 与本地一致：不联网、不覆盖。 */
    rh_dat_fetch_result_clear(&result);
    err = rh_dat_fetch(source, cache_dir, NULL, &result);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_false(result.changed);
    g_assert_false(result.installed);
    g_assert_cmpint(result.mirrors_tried, ==, 0);
    g_assert_cmpstr(result.sha1, ==, sha1);

    rh_dat_fetch_result_clear(&result);
    rh_dat_source_free(source);
    g_free(local);
    g_free(url);
    g_free(remote);
}

static void test_check_only_and_update(gpointer fixture, gconstpointer user_data)
{
    (void)fixture;
    (void)user_data;
    gchar first_sha1[41];
    gchar *remote = write_remote("first", first_sha1);
    gchar *url = g_filename_to_uri(remote, NULL, NULL);
    RhDatSource *source = make_source(url, NULL, NULL);
    RhDatFetchResult result;

    RhErr err = rh_dat_fetch(source, cache_dir, NULL, &result);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_true(result.installed);

    /* 远端变化后 --check：只报告有更新，不落盘。 */
    g_file_set_contents(remote, "second", -1, NULL);
    rh_dat_fetch_result_clear(&result);
    RhDatFetchOptions options = { 0 };
    options.check_only = TRUE;
    err = rh_dat_fetch(source, cache_dir, &options, &result);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_true(result.changed);
    g_assert_false(result.installed);

    gchar *local = local_path();
    gchar *content = NULL;
    g_assert_true(g_file_get_contents(local, &content, NULL, NULL));
    g_assert_cmpstr(content, ==, "first");
    g_free(content);

    rh_dat_fetch_result_clear(&result);
    err = rh_dat_fetch(source, cache_dir, NULL, &result);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_true(result.changed);
    g_assert_true(result.installed);

    content = NULL;
    g_assert_true(g_file_get_contents(local, &content, NULL, NULL));
    g_assert_cmpstr(content, ==, "second");
    g_free(content);

    rh_dat_fetch_result_clear(&result);
    rh_dat_source_free(source);
    g_free(local);
    g_free(url);
    g_free(remote);
}

static void test_sha1_mismatch(gpointer fixture, gconstpointer user_data)
{
    (void)fixture;
    (void)user_data;
    gchar real_sha1[41];
    gchar *remote = write_remote("wrong", real_sha1);
    gchar *url = g_filename_to_uri(remote, NULL, NULL);
    const gchar *expected = "0000000000000000000000000000000000000000";
    RhDatSource *source = make_source(url, NULL, expected);
    RhDatFetchResult result;

    RhErr err = rh_dat_fetch(source, cache_dir, NULL, &result);
    g_assert_cmpint(err, ==, RH_ERR_IO);
    g_assert_null(result.url);
    g_assert_nonnull(result.message);
    g_assert_nonnull(strstr(result.message, "sha1"));

    gchar *local = local_path();
    g_assert_false(g_file_test(local, G_FILE_TEST_EXISTS));

    rh_dat_fetch_result_clear(&result);
    rh_dat_source_free(source);
    g_free(local);
    g_free(url);
    g_free(remote);
}

static void test_mirror_fallback(gpointer fixture, gconstpointer user_data)
{
    (void)fixture;
    (void)user_data;
    gchar sha1[41];
    gchar *remote = write_remote("mirror", sha1);
    gchar *good = g_filename_to_uri(remote, NULL, NULL);
    gchar *missing = g_build_filename(tmp_dir, "missing.dat", NULL);
    gchar *bad = g_filename_to_uri(missing, NULL, NULL);
    RhDatSource *source = make_source(bad, good, sha1);
    RhDatFetchResult result;

    RhErr err = rh_dat_fetch(source, cache_dir, NULL, &result);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_true(result.installed);
    g_assert_cmpint(result.mirrors_tried, ==, 2);
    g_assert_cmpstr(result.url, ==, good);

    rh_dat_fetch_result_clear(&result);
    rh_dat_source_free(source);
    g_free(bad);
    g_free(good);
    g_free(missing);
    g_free(remote);
}

static gboolean cancel_progress(const gchar *url, guint64 done,
                                guint64 total, gpointer user_data)
{
    (void)url;
    (void)done;
    (void)total;
    (void)user_data;
    return FALSE;
}

static void test_cancel(gpointer fixture, gconstpointer user_data)
{
    (void)fixture;
    (void)user_data;
    gchar *payload = g_strnfill(256 * 1024, 'x');
    gchar *remote = write_remote(payload, NULL);
    g_free(payload);
    gchar *url = g_filename_to_uri(remote, NULL, NULL);
    RhDatSource *source = make_source(url, NULL, NULL);
    RhDatFetchResult result;

    RhDatFetchOptions options = { 0 };
    options.progress = cancel_progress;
    RhErr err = rh_dat_fetch(source, cache_dir, &options, &result);
    g_assert_cmpint(err, ==, RH_ERR_CANCELLED);

    gchar *local = local_path();
    gchar *part = g_strconcat(local, ".part", NULL);
    g_assert_false(g_file_test(local, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(part, G_FILE_TEST_EXISTS));

    rh_dat_fetch_result_clear(&result);
    rh_dat_source_free(source);
    g_free(part);
    g_free(local);
    g_free(url);
    g_free(remote);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_assert_true(rh_dat_fetch_supported());

    g_test_add("/dat-fetch/install-unchanged", void, NULL, setup,
               test_install_and_unchanged, teardown);
    g_test_add("/dat-fetch/check-update", void, NULL, setup,
               test_check_only_and_update, teardown);
    g_test_add("/dat-fetch/sha1-mismatch", void, NULL, setup,
               test_sha1_mismatch, teardown);
    g_test_add("/dat-fetch/mirror-fallback", void, NULL, setup,
               test_mirror_fallback, teardown);
    g_test_add("/dat-fetch/cancel", void, NULL, setup,
               test_cancel, teardown);

    return g_test_run();
}
