#include "classify.h"

#include <string.h>

#include "rh-records.h"
#include "rh-str.h"

void rh_classify_result_free(RhClassifyResult *result)
{
    if (result == NULL)
        return;
    g_free(result->extension);
    g_free(result->media_kind);
    g_free(result->patch_platform);
    g_ptr_array_free(result->candidates, TRUE);
    g_free(result);
}

const gchar *rh_classify_media_kind(const RhPlatformRules *rules, const char *filename)
{
    if (rules == NULL || filename == NULL)
        return NULL;

    for (gsize i = 0; i < G_N_ELEMENTS(rh_media_kind_names); i++) {
        const gchar *kind = rh_media_kind_names[i];
        if (rh_platform_media_suffix_allowed(rules, kind, filename))
            return kind;
    }
    return NULL;
}

static gboolean platform_lists_suffix(const GPtrArray *suffixes, const gchar *ext)
{
    for (guint i = 0; i < suffixes->len; i++) {
        if (g_strcmp0(g_ptr_array_index(suffixes, i), ext) == 0)
            return TRUE;
    }
    return FALSE;
}

/* 复合后缀（内层后缀 + 最后后缀），无内层时返回 NULL */
static gchar *compound_extension(const char *filename)
{
    gchar *last = rh_str_extension(filename);
    if (last == NULL)
        return NULL;
    gchar *stem = rh_str_stem(filename);
    gchar *inner = rh_str_extension(stem);
    gchar *result = NULL;
    if (inner != NULL)
        result = g_strconcat(inner, last, NULL);
    g_free(inner);
    g_free(stem);
    g_free(last);
    return result;
}

static RhClassifyResult *result_new(void)
{
    RhClassifyResult *result = g_new0(RhClassifyResult, 1);
    result->candidates = g_ptr_array_new();
    return result;
}

RhClassifyResult *rh_classify_file(const RhPlatformRules *rules, const char *filename)
{
    RhClassifyResult *result = result_new();
    result->kind = RH_CLASSIFY_UNKNOWN;

    if (rules == NULL || filename == NULL)
        return result;

    gchar *ext = rh_str_extension(filename);
    if (ext == NULL)
        return result;
    result->extension = g_strdup(ext);

    /* 1. 复合后缀优先：celeste.p8.png 属于 pico-8，不是一张封面 */
    gchar *compound = compound_extension(filename);
    if (compound != NULL) {
        for (guint i = 0; i < rh_platform_all(rules)->len; i++) {
            RhPlatform *platform = g_ptr_array_index(rh_platform_all(rules), i);
            if (platform_lists_suffix(platform->suffixes, compound))
                g_ptr_array_add(result->candidates, platform);
        }
        g_free(compound);
        if (result->candidates->len > 0) {
            result->kind = RH_CLASSIFY_ROM;
            result->ambiguous = result->candidates->len > 1;
            g_free(ext);
            return result;
        }
    }

    /* 2. 归档 */
    if (rh_platform_is_archive(rules, filename)) {
        result->kind = RH_CLASSIFY_ARCHIVE;
        g_free(ext);
        return result;
    }

    /* 3. 媒体（封面/截图/视频/…），按类型后缀白名单判定 */
    const gchar *media_kind = rh_classify_media_kind(rules, filename);
    if (media_kind != NULL) {
        result->kind = RH_CLASSIFY_MEDIA;
        result->media_kind = g_strdup(media_kind);
        g_free(ext);
        return result;
    }

    /* 4. ROM：普通后缀匹配 */
    GPtrArray *candidates = rh_platform_candidates_for_file(rules, filename);
    if (candidates->len > 0) {
        result->kind = RH_CLASSIFY_ROM;
        result->ambiguous = candidates->len > 1;
        for (guint i = 0; i < candidates->len; i++) {
            RhPlatform *platform = g_ptr_array_index(candidates, i);
            g_ptr_array_add(result->candidates, platform);
            if (platform_lists_suffix(platform->ambiguous_formats, ext))
                result->ambiguous = TRUE;
        }
        g_ptr_array_free(candidates, TRUE);
        g_free(ext);
        return result;
    }
    g_ptr_array_free(candidates, TRUE);

    /* 5. 补丁：属于某平台的 patch_formats，且不是该平台的 ROM 后缀 */
    for (guint i = 0; i < rh_platform_all(rules)->len; i++) {
        RhPlatform *platform = g_ptr_array_index(rh_platform_all(rules), i);
        if (platform_lists_suffix(platform->patch_formats, ext)) {
            result->kind = RH_CLASSIFY_PATCH;
            result->patch_platform = g_strdup(platform->id);
            break;
        }
    }

    g_free(ext);
    return result;
}

/* ---------------------------------------------------------------------------
 * 压缩包六级分流
 * ------------------------------------------------------------------------- */

void rh_archive_classify_free(RhArchiveClassifyResult *result)
{
    if (result == NULL)
        return;
    g_free(result->platform);
    g_free(result);
}

static gboolean component_starts_with(const gchar *lower_path, const gchar *prefix)
{
    const gchar *p = lower_path;
    while (p != NULL && *p != '\0') {
        if (g_str_has_prefix(p, prefix))
            return TRUE;
        p = strchr(p, '/');
        if (p != NULL)
            p++;
    }
    return FALSE;
}

static gboolean path_is_psv(const gchar *path)
{
    gchar *lower = g_ascii_strdown(path, -1);
    gboolean psv = g_str_has_prefix(lower, "sce_sys/") ||
                   strstr(lower, "/sce_sys/") != NULL ||
                   component_starts_with(lower, "pcsh");
    g_free(lower);
    return psv;
}

RhArchiveClassifyResult *rh_classify_archive(const RhPlatformRules *rules,
                                             const GPtrArray *entry_paths)
{
    RhArchiveClassifyResult *result = g_new0(RhArchiveClassifyResult, 1);
    if (rules == NULL || entry_paths == NULL) {
        result->branch = RH_ARCHIVE_AMBIGUOUS;
        result->ambiguous = TRUE;
        return result;
    }

    GHashTable *platform_ids = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                     g_free, NULL);
    guint arcade_members = 0;
    gboolean platform_ambiguous = FALSE;
    gboolean psv = FALSE;

    for (guint i = 0; i < entry_paths->len; i++) {
        const gchar *path = g_ptr_array_index(entry_paths, i);
        if (path == NULL || *path == '\0' || g_str_has_suffix(path, "/"))
            continue;
        if (!psv && path_is_psv(path))
            psv = TRUE;

        const gchar *base = strrchr(path, '/');
        base = base != NULL ? base + 1 : path;
        if (rh_platform_is_arcade_rom_member(rules, base))
            arcade_members++;

        RhClassifyResult *one = rh_classify_file(rules, base);
        switch (one->kind) {
        case RH_CLASSIFY_ROM:
            result->rom_count++;
            if (one->ambiguous)
                platform_ambiguous = TRUE;
            for (guint k = 0; k < one->candidates->len; k++) {
                RhPlatform *candidate = g_ptr_array_index(one->candidates, k);
                g_hash_table_add(platform_ids, g_strdup(candidate->id));
            }
            break;
        case RH_CLASSIFY_PATCH:
            result->patch_count++;
            break;
        case RH_CLASSIFY_MEDIA:
            result->media_count++;
            break;
        case RH_CLASSIFY_ARCHIVE:
            /* 嵌套压缩包按未知处理，避免静默展开 */
            result->unknown_count++;
            break;
        case RH_CLASSIFY_UNKNOWN:
            result->unknown_count++;
            break;
        }
        rh_classify_result_free(one);
    }

    result->ambiguous = platform_ambiguous || g_hash_table_size(platform_ids) > 1;
    if (g_hash_table_size(platform_ids) == 1) {
        GHashTableIter iter;
        gpointer key = NULL;
        g_hash_table_iter_init(&iter, platform_ids);
        if (g_hash_table_iter_next(&iter, &key, NULL))
            result->platform = g_strdup(key);
    }
    g_hash_table_unref(platform_ids);

    if (psv)
        result->branch = RH_ARCHIVE_PSV;
    else if (result->rom_count == 0 && result->patch_count > 0)
        result->branch = RH_ARCHIVE_PATCHES;
    else if (arcade_members >= 2)
        result->branch = RH_ARCHIVE_ARCADE;
    else if (result->ambiguous)
        result->branch = RH_ARCHIVE_AMBIGUOUS;
    else if (result->rom_count == 1 && result->patch_count == 0 &&
             result->media_count == 0 && result->unknown_count == 0)
        result->branch = RH_ARCHIVE_SINGLE_ROM;
    else
        result->branch = RH_ARCHIVE_MIXED;

    return result;
}
