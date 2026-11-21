#include "dat-fetch.h"

#include <stdio.h>
#include <string.h>

#include <glib/gstdio.h>

#include "rh-hash.h"
#include "rh-version.h"

void rh_dat_fetch_result_clear(RhDatFetchResult *result)
{
    if (result == NULL)
        return;
    g_free(result->platform);
    g_free(result->path);
    g_free(result->url);
    g_free(result->message);
    memset(result, 0, sizeof *result);
}

#ifndef RH_DAT_FETCH

gboolean rh_dat_fetch_supported(void)
{
    return FALSE;
}

RhErr rh_dat_fetch(const RhDatSource *source, const char *dir,
                   const RhDatFetchOptions *options, RhDatFetchResult *out)
{
    (void)source;
    (void)dir;
    (void)options;
    if (out != NULL)
        memset(out, 0, sizeof *out);
    return RH_ERR_UNSUPPORTED;
}

#else /* RH_DAT_FETCH */

#include <curl/curl.h>

gboolean rh_dat_fetch_supported(void)
{
    return TRUE;
}

typedef struct {
    FILE                 *fp;
    const gchar          *url;
    RhDatFetchOptions     options;
} CurlSink;

static void ensure_global_init(void)
{
    static gsize once = 0;
    if (g_once_init_enter(&once)) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        g_once_init_leave(&once, 1);
    }
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *user_data)
{
    CurlSink *sink = user_data;
    size_t bytes = size * nmemb;
    if (bytes == 0)
        return 0;
    return fwrite(ptr, 1, bytes, sink->fp);
}

static int xferinfo_cb(void *user_data, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    CurlSink *sink = user_data;
    if (sink->options.progress == NULL)
        return 0;

    if (!sink->options.progress(sink->url, (guint64)dlnow,
                                (guint64)dltotal, sink->options.user_data))
        return 1;
    return 0;
}

static RhErr download_to(const char *url, const char *temp_path,
                         const RhDatFetchOptions *options, gchar **out_message)
{
    ensure_global_init();

    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        if (out_message != NULL)
            *out_message = g_strdup("curl_easy_init failed");
        return RH_ERR_NOMEM;
    }

    FILE *fp = g_fopen(temp_path, "wb");
    if (fp == NULL) {
        curl_easy_cleanup(curl);
        if (out_message != NULL)
            *out_message = g_strdup("cannot open temporary file for writing");
        return RH_ERR_IO;
    }

    CurlSink sink;
    memset(&sink, 0, sizeof sink);
    sink.fp = fp;
    sink.url = url;
    sink.options = *options;

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                     (long)(options->connect_timeout > 0
                            ? options->connect_timeout : 20));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT,
                     (long)(options->low_speed_limit > 0
                            ? options->low_speed_limit : 1024));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,
                     (long)(options->low_speed_time > 0
                            ? options->low_speed_time : 30));
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     RH_PROJECT_NAME "/" RH_VERSION);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https,file");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                     (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS | CURLPROTO_FILE));
#endif

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (fclose(fp) != 0 && rc == CURLE_OK)
        rc = CURLE_WRITE_ERROR;

    if (rc != CURLE_OK) {
        if (out_message != NULL) {
            *out_message = g_strdup(errbuf[0] != '\0'
                                    ? errbuf : curl_easy_strerror(rc));
        }
        return rc == CURLE_ABORTED_BY_CALLBACK ? RH_ERR_CANCELLED : RH_ERR_IO;
    }
    return RH_OK;
}

static RhErr sha1_of(const char *path, gchar out_sha1[41], guint64 *out_size)
{
    RhHashOptions hash_options = { 0 };
    hash_options.flags = RH_HASH_SHA1;

    RhHashResult result;
    RhErr err = rh_hash_file(path, &hash_options, &result);
    if (err != RH_OK)
        return err;

    memcpy(out_sha1, result.sha1, 41);
    if (out_size != NULL)
        *out_size = result.size;
    return RH_OK;
}

static void set_message(gchar **slot, gchar *message)
{
    g_free(*slot);
    *slot = message;
}

RhErr rh_dat_fetch(const RhDatSource *source, const char *dir,
                   const RhDatFetchOptions *options, RhDatFetchResult *out)
{
    if (out == NULL)
        return RH_ERR_INVALID;
    memset(out, 0, sizeof *out);

    if (source == NULL || dir == NULL || source->file == NULL ||
        source->file[0] == '\0' || source->urls == NULL ||
        source->urls->len == 0)
        return RH_ERR_INVALID;

    RhDatFetchOptions defaults;
    memset(&defaults, 0, sizeof defaults);
    if (options == NULL)
        options = &defaults;

    if (g_mkdir_with_parents(dir, 0755) != 0)
        return RH_ERR_IO;

    gchar *final_path = g_build_filename(dir, source->file, NULL);
    gchar *temp_path = g_strconcat(final_path, ".part", NULL);

    out->platform = g_strdup(source->platform);
    out->path = g_strdup(final_path);

    gboolean have_local = g_file_test(final_path, G_FILE_TEST_IS_REGULAR);
    gchar local_sha1[41] = { 0 };
    guint64 local_size = 0;
    if (have_local &&
        sha1_of(final_path, local_sha1, &local_size) != RH_OK)
        have_local = FALSE;

    /* 清单自带 sha1 且本地一致：不联网。 */
    if (have_local && !options->force && source->sha1 != NULL &&
        g_ascii_strcasecmp(local_sha1, source->sha1) == 0) {
        memcpy(out->sha1, local_sha1, 41);
        out->bytes = local_size;
        out->changed = FALSE;
        out->installed = FALSE;
        g_free(final_path);
        g_free(temp_path);
        return RH_OK;
    }

    RhErr last_err = RH_ERR_IO;
    gchar *last_message = NULL;

    for (guint i = 0; i < source->urls->len; i++) {
        const gchar *url = g_ptr_array_index(source->urls, i);
        out->mirrors_tried++;

        gchar *message = NULL;
        RhErr err = download_to(url, temp_path, options, &message);
        if (err != RH_OK) {
            g_unlink(temp_path);
            set_message(&last_message, message);
            last_err = err;
            if (err == RH_ERR_CANCELLED)
                break;
            continue;
        }

        gchar sha1[41] = { 0 };
        guint64 size = 0;
        err = sha1_of(temp_path, sha1, &size);
        if (err != RH_OK) {
            g_unlink(temp_path);
            g_free(message);
            set_message(&last_message,
                        g_strdup("cannot read downloaded file"));
            last_err = err;
            continue;
        }

        if (source->sha1 != NULL &&
            g_ascii_strcasecmp(sha1, source->sha1) != 0) {
            g_unlink(temp_path);
            g_free(message);
            set_message(&last_message,
                        g_strdup_printf("sha1 mismatch: expected %s, got %s",
                                        source->sha1, sha1));
            last_err = RH_ERR_IO;
            continue;
        }

        g_free(last_message);
        last_message = NULL;

        out->bytes = size;
        memcpy(out->sha1, sha1, 41);
        out->url = g_strdup(url);

        gboolean changed = !have_local ||
            g_ascii_strcasecmp(sha1, local_sha1) != 0;
        out->changed = changed;

        if (!changed || options->check_only) {
            g_unlink(temp_path);
            out->installed = FALSE;
            g_free(final_path);
            g_free(temp_path);
            return RH_OK;
        }

        if (g_rename(temp_path, final_path) != 0) {
            g_unlink(temp_path);
            out->message = g_strdup("cannot replace the existing DAT file");
            g_free(final_path);
            g_free(temp_path);
            return RH_ERR_IO;
        }

        out->installed = TRUE;
        g_free(final_path);
        g_free(temp_path);
        return RH_OK;
    }

    g_free(final_path);
    g_free(temp_path);
    out->message = last_message;
    return last_err;
}

#endif /* RH_DAT_FETCH */
