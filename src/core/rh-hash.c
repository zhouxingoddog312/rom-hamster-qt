#include "rh-hash.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>
#include <zlib.h>

#define RH_HASH_DEFAULT_CHUNK (256u * 1024u)

void rh_hash_result_clear(RhHashResult *result)
{
    if (result != NULL)
        memset(result, 0, sizeof *result);
}

static RhErr fail(RhHashResult *out, RhErr err)
{
    rh_hash_result_clear(out);
    if (out != NULL)
        out->error = err;
    return err;
}

/* 一次顺序读盘：同一块数据同时喂给已请求的算法。 */
static RhErr hash_one(const char *path, const RhHashOptions *options,
                      RhHashResult *out)
{
    rh_hash_result_clear(out);
    if (path == NULL || out == NULL || !g_utf8_validate(path, -1, NULL))
        return fail(out, RH_ERR_INVALID);

    RhHashFlags flags = options != NULL ? options->flags : RH_HASH_ALL;
    if (flags == 0 || (flags & ~RH_HASH_ALL) != 0)
        return fail(out, RH_ERR_INVALID);

    GStatBuf st;
    if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return fail(out, RH_ERR_IO);
    guint64 file_size = (guint64)st.st_size;
    guint64 skip = options != NULL ? options->skip_bytes : 0;
    if (skip > 0 && skip >= file_size)
        return fail(out, RH_ERR_INVALID);
    guint64 total = file_size - skip;

    /* g_fopen 在 Windows 上把 UTF-8 路径转成宽字符，避免 ANSI fopen 打不开
     * 中文路径；POSIX 上就是普通 fopen。 */
    FILE *fp = g_fopen(path, "rb");
    if (fp == NULL)
        return fail(out, RH_ERR_IO);

    if (skip > 0 && fseek(fp, (long)skip, SEEK_SET) != 0) {
        fclose(fp);
        return fail(out, RH_ERR_IO);
    }

    guint chunk = options != NULL && options->chunk_size > 0
        ? options->chunk_size : RH_HASH_DEFAULT_CHUNK;
    guchar *buffer = g_try_malloc(chunk);
    if (buffer == NULL) {
        fclose(fp);
        return fail(out, RH_ERR_NOMEM);
    }

    GChecksum *md5 = (flags & RH_HASH_MD5)
        ? g_checksum_new(G_CHECKSUM_MD5) : NULL;
    GChecksum *sha1 = (flags & RH_HASH_SHA1)
        ? g_checksum_new(G_CHECKSUM_SHA1) : NULL;
    uLong crc = crc32(0L, Z_NULL, 0);

    guint64 processed = 0;
    RhErr err = RH_OK;

    while (err == RH_OK) {
        gsize n = fread(buffer, 1, chunk, fp);
        if (n == 0)
            break;

        processed += n;
        if (md5 != NULL)
            g_checksum_update(md5, buffer, n);
        if (sha1 != NULL)
            g_checksum_update(sha1, buffer, n);
        if (flags & RH_HASH_CRC32)
            crc = crc32(crc, (const Bytef *)buffer, (uInt)n);

        if (options != NULL && options->progress != NULL
            && !options->progress(path, processed, total, options->user_data))
            err = RH_ERR_CANCELLED;
    }

    if (err == RH_OK && ferror(fp))
        err = RH_ERR_IO;

    fclose(fp);
    g_free(buffer);

    if (err != RH_OK) {
        if (md5 != NULL)
            g_checksum_free(md5);
        if (sha1 != NULL)
            g_checksum_free(sha1);
        return fail(out, err);
    }

    out->size = processed;
    if (flags & RH_HASH_CRC32) {
        out->has_crc32 = TRUE;
        out->crc32 = (guint32)crc;
    }
    if (md5 != NULL) {
        out->has_md5 = TRUE;
        g_strlcpy(out->md5, g_checksum_get_string(md5), sizeof out->md5);
        g_checksum_free(md5);
    }
    if (sha1 != NULL) {
        out->has_sha1 = TRUE;
        g_strlcpy(out->sha1, g_checksum_get_string(sha1), sizeof out->sha1);
        g_checksum_free(sha1);
    }
    out->error = RH_OK;
    return RH_OK;
}

RhErr rh_hash_file(const char *path, const RhHashOptions *options,
                   RhHashResult *out)
{
    if (out == NULL)
        return RH_ERR_INVALID;
    return hash_one(path, options, out);
}

RhErr rh_hash_files(const char *const *paths, guint count,
                    const RhHashOptions *options, RhHashResult *out_results)
{
    if ((paths == NULL && count > 0) || out_results == NULL)
        return RH_ERR_INVALID;

    RhErr first_error = RH_OK;
    for (guint i = 0; i < count; i++) {
        RhErr err = hash_one(paths[i], options, &out_results[i]);
        if (err == RH_ERR_CANCELLED)
            return err;
        if (err != RH_OK && first_error == RH_OK)
            first_error = err;
    }
    return first_error;
}
