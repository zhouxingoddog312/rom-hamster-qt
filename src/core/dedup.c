#include "dedup.h"

#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

#include "rh-fname.h"
#include "rh-hash.h"
#include "rh-str.h"

static const char *const snes_exts[] = {
    ".smc", ".sfc", ".fig", ".swc", ".bs"
};

gboolean rh_dedup_header_skip(const char *abs_path, const char *filename,
                              gint64 size, guint64 *out_skip)
{
    if (out_skip == NULL)
        return FALSE;
    *out_skip = 0;
    if (abs_path == NULL || filename == NULL || size <= 0)
        return FALSE;

    FILE *fp = g_fopen(abs_path, "rb");
    if (fp == NULL)
        return FALSE;

    guchar magic[4] = {0};
    size_t got = fread(magic, 1, sizeof magic, fp);
    fclose(fp);

    if (got == 4 && memcmp(magic, "NES\x1a", 4) == 0 &&
        size > 16 && (size - 16) % 16384 == 0) {
        *out_skip = 16;
        return TRUE;
    }

    gchar *ext = rh_str_extension(filename);
    if (ext != NULL) {
        for (gsize i = 0; i < G_N_ELEMENTS(snes_exts); i++) {
            if (g_strcmp0(ext, snes_exts[i]) == 0) {
                if (size > 512 && size % 32768 == 512)
                    *out_skip = 512;
                break;
            }
        }
        g_free(ext);
    }
    return TRUE;
}

/* 用原始字节还原磁盘路径；没有原始字节时退回显示名。 */
static gchar *join_native(const gchar *base, GBytes *raw, const gchar *display)
{
    if (raw != NULL) {
        gsize len = 0;
        const gchar *data = g_bytes_get_data(raw, &len);
        if (len > 0) {
            RhFName *name = rh_fname_from_bytes(data, (gssize)len);
            if (name != NULL) {
                gchar *native = rh_fname_native_name(name);
                rh_fname_free(name);
                if (native != NULL) {
                    gchar *full = g_build_filename(base, native, NULL);
                    g_free(native);
                    return full;
                }
            }
        }
    }
    return display != NULL ? g_build_filename(base, display, NULL) : NULL;
}

static gchar *member_path(const char *root, const RhDupMember *member)
{
    gchar *base = join_native(root, member->dir_path_raw, member->dir_path);
    if (base == NULL)
        return NULL;
    gchar *full = join_native(base, member->filename_raw, member->filename);
    g_free(base);
    return full;
}

/* 整文件 CRC32+SHA1；有容器头时再读一遍算 canon_sha1。 */
static RhErr hash_with_canon(const gchar *path, const gchar *filename,
                             RhHashResult *whole, gchar **out_canon)
{
    if (out_canon != NULL)
        *out_canon = NULL;

    GStatBuf st;
    if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return RH_ERR_IO;

    guint64 skip = 0;
    if (!rh_dedup_header_skip(path, filename, (gint64)st.st_size, &skip))
        skip = 0;

    RhHashOptions options = {.flags = RH_HASH_CRC32 | RH_HASH_SHA1};
    RhErr err = rh_hash_file(path, &options, whole);
    if (err != RH_OK)
        return err;

    if (skip > 0) {
        RhHashOptions tail = {.flags = RH_HASH_SHA1, .skip_bytes = skip};
        RhHashResult tail_result;
        err = rh_hash_file(path, &tail, &tail_result);
        if (err != RH_OK)
            return err;
        if (out_canon != NULL)
            *out_canon = g_strdup(tail_result.sha1);
    } else if (out_canon != NULL) {
        *out_canon = g_strdup(whole->sha1);
    }
    return RH_OK;
}

static RhDupMember *copy_member(const RhDupMember *src)
{
    RhDupMember *member = rh_dup_member_new();
    member->game_id = g_strdup(src->game_id);
    member->game_name = g_strdup(src->game_name);
    member->platform = g_strdup(src->platform);
    member->dir_path = g_strdup(src->dir_path);
    member->filename = g_strdup(src->filename);
    member->kind = g_strdup(src->kind);
    member->size = src->size;
    member->sha1 = g_strdup(src->sha1);
    member->canon_sha1 = g_strdup(src->canon_sha1);
    if (src->dir_path_raw != NULL)
        member->dir_path_raw = g_bytes_ref(src->dir_path_raw);
    if (src->filename_raw != NULL)
        member->filename_raw = g_bytes_ref(src->filename_raw);
    member->missing = src->missing;
    return member;
}

RhErr rh_dedup_backfill(RhStore *store, const RhDedupOptions *options,
                        RhDedupStats *out_stats)
{
    if (out_stats != NULL)
        memset(out_stats, 0, sizeof *out_stats);
    if (store == NULL || options == NULL || options->root == NULL)
        return RH_ERR_INVALID;

    RhErr err = RH_OK;
    GPtrArray *roms = store_roms_missing_hash(store, options->platform, &err);
    if (err != RH_OK) {
        g_ptr_array_unref(roms);
        return err;
    }
    GPtrArray *media = NULL;
    if (options->include_media) {
        media = store_media_missing_hash(store, options->platform, &err);
        if (err != RH_OK) {
            g_ptr_array_unref(roms);
            g_ptr_array_unref(media);
            return err;
        }
    }

    guint total = roms->len + (media != NULL ? media->len : 0);
    guint done = 0;
    RhDedupStats stats = {0};
    RhErr result = RH_OK;

    for (guint i = 0; i < roms->len; i++) {
        RhDupMember *member = g_ptr_array_index(roms, i);
        gchar *path = member_path(options->root, member);
        if (options->progress != NULL &&
            !options->progress(path != NULL ? path : "", done, total,
                               options->user_data)) {
            result = RH_ERR_CANCELLED;
            g_free(path);
            break;
        }
        if (path == NULL) {
            stats.failed++;
            done++;
            continue;
        }

        RhHashResult whole;
        gchar *canon = NULL;
        if (hash_with_canon(path, member->filename, &whole, &canon) == RH_OK) {
            gchar *crc = g_strdup_printf("%08x", whole.crc32);
            RhErr set = store_rom_set_hashes(store, member->game_id,
                                             member->filename, member->size,
                                             crc, whole.sha1, canon);
            g_free(crc);
            if (set == RH_OK)
                stats.hashed++;
            else if (set == RH_ERR_NOT_FOUND)
                stats.failed++;      /* 行已不存在：按数据缺失处理 */
            else
                result = set;        /* 写库失败：硬错误，交给前端返回 ≥2 */
        } else {
            stats.failed++;
        }
        g_free(canon);
        g_free(path);
        if (result != RH_OK)
            break;
        done++;
    }

    for (guint i = 0; media != NULL && result == RH_OK && i < media->len; i++) {
        RhDupMember *member = g_ptr_array_index(media, i);
        gchar *path = member_path(options->root, member);
        if (options->progress != NULL &&
            !options->progress(path != NULL ? path : "", done, total,
                               options->user_data)) {
            result = RH_ERR_CANCELLED;
            g_free(path);
            break;
        }
        if (path == NULL) {
            stats.failed++;
            done++;
            continue;
        }

        RhHashResult whole;
        gchar *canon = NULL;
        if (hash_with_canon(path, member->filename, &whole, &canon) == RH_OK) {
            gchar *crc = g_strdup_printf("%08x", whole.crc32);
            RhErr set = store_media_set_hash(store, member->game_id,
                                             member->kind, member->filename,
                                             member->size, crc, whole.sha1);
            g_free(crc);
            if (set == RH_OK)
                stats.hashed++;
            else if (set == RH_ERR_NOT_FOUND)
                stats.failed++;
            else
                result = set;
        } else {
            stats.failed++;
        }
        g_free(canon);
        g_free(path);
        if (result != RH_OK)
            break;
        done++;
    }

    if (out_stats != NULL)
        *out_stats = stats;
    g_ptr_array_unref(roms);
    if (media != NULL)
        g_ptr_array_unref(media);
    return result;
}

RhErr rh_dedup_report(RhStore *store, const RhDedupOptions *options,
                      GPtrArray **out_groups, RhDedupStats *out_stats)
{
    if (out_groups != NULL)
        *out_groups = NULL;
    if (out_stats != NULL)
        memset(out_stats, 0, sizeof *out_stats);
    if (store == NULL || options == NULL || options->root == NULL ||
        out_groups == NULL)
        return RH_ERR_INVALID;

    RhDedupStats stats = {0};
    if (options->backfill) {
        RhErr err = rh_dedup_backfill(store, options, &stats);
        if (err != RH_OK)
            return err;
    }

    RhErr err = RH_OK;
    GPtrArray *groups = store_roms_dup_groups(store, options->platform, &err);
    if (err != RH_OK) {
        g_ptr_array_unref(groups);
        return err;
    }

    if (options->include_media) {
        GPtrArray *media = store_media_dup_groups(store, options->platform, &err);
        if (err != RH_OK) {
            g_ptr_array_unref(media);
            g_ptr_array_unref(groups);
            return err;
        }
        while (media->len > 0)
            g_ptr_array_add(groups, g_ptr_array_steal_index(media, 0));
        g_ptr_array_unref(media);
    }

    for (gint i = (gint)groups->len - 1; i >= 0; i--) {
        RhDupGroup *group = g_ptr_array_index(groups, (guint)i);
        if (group->size < options->min_size)
            g_ptr_array_remove_index(groups, (guint)i);
    }

    stats.clusters = groups->len;
    for (guint i = 0; i < groups->len; i++) {
        RhDupGroup *group = g_ptr_array_index(groups, i);
        if (group->members->len > 1) {
            guint extra = group->members->len - 1;
            stats.extra_copies += extra;
            stats.bytes_duplicated += group->size * (gint64)extra;
        }
        for (guint j = 0; j < group->members->len; j++) {
            RhDupMember *member = g_ptr_array_index(group->members, j);
            gchar *path = member_path(options->root, member);
            member->missing = path == NULL ||
                !g_file_test(path, G_FILE_TEST_EXISTS);
            g_free(path);
        }
    }

    *out_groups = groups;
    if (out_stats != NULL)
        *out_stats = stats;
    return RH_OK;
}

RhErr rh_dedup_check_file(RhStore *store, const char *root,
                          const char *abs_path, const char *platform,
                          const char *rel_name, RhDupMember **out_match)
{
    if (out_match == NULL)
        return RH_ERR_INVALID;
    *out_match = NULL;
    if (store == NULL || root == NULL || abs_path == NULL)
        return RH_ERR_INVALID;

    GStatBuf st;
    if (g_stat(abs_path, &st) != 0 || !S_ISREG(st.st_mode))
        return RH_ERR_IO;

    const gchar *name = rel_name != NULL ? rel_name : abs_path;
    RhHashResult whole;
    gchar *canon = NULL;
    RhErr err = hash_with_canon(abs_path, rh_str_basename(name), &whole, &canon);
    if (err != RH_OK) {
        g_free(canon);
        return err;
    }

    GPtrArray *candidates = store_roms_by_hash(store, platform, canon, &err);
    if (err != RH_OK) {
        g_free(canon);
        g_ptr_array_unref(candidates);
        return err;
    }

    /* 带头文件与已有同内容文件大小可能不同，再按整文件 sha1 找一遍。 */
    if (g_strcmp0(whole.sha1, canon) != 0) {
        GPtrArray *more = store_roms_by_hash(store, platform, whole.sha1, &err);
        if (err != RH_OK) {
            g_free(canon);
            g_ptr_array_unref(more);
            g_ptr_array_unref(candidates);
            return err;
        }
        while (more->len > 0) {
            RhDupMember *extra = g_ptr_array_steal_index(more, 0);
            gboolean seen = FALSE;
            for (guint k = 0; k < candidates->len; k++) {
                RhDupMember *have = g_ptr_array_index(candidates, k);
                if (g_strcmp0(have->game_id, extra->game_id) == 0 &&
                    g_strcmp0(have->filename, extra->filename) == 0) {
                    seen = TRUE;
                    break;
                }
            }
            if (seen)
                rh_dup_member_free(extra);
            else
                g_ptr_array_add(candidates, extra);
        }
        g_ptr_array_unref(more);
    }

    gchar *abs_canonical = g_canonicalize_filename(abs_path, NULL);
    for (guint i = 0; i < candidates->len; i++) {
        RhDupMember *candidate = g_ptr_array_index(candidates, i);
        gchar *candidate_path = member_path(root, candidate);
        if (candidate_path != NULL) {
            gchar *canonical = g_canonicalize_filename(candidate_path, NULL);
            gboolean same = g_strcmp0(canonical, abs_canonical) == 0;
            g_free(canonical);
            g_free(candidate_path);
            if (same)
                continue;
        }

        const gchar *key = candidate->canon_sha1 != NULL &&
                           candidate->canon_sha1[0] != '\0'
                               ? candidate->canon_sha1
                               : candidate->sha1;
        if (g_strcmp0(key, canon) == 0 ||
            g_strcmp0(candidate->sha1, whole.sha1) == 0) {
            *out_match = copy_member(candidate);
            break;
        }
    }

    g_free(abs_canonical);
    g_free(canon);
    g_ptr_array_unref(candidates);
    return RH_OK;
}
