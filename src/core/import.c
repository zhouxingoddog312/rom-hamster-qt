#include "import.h"

#include <string.h>

#include "classify.h"
#include "rh-records.h"
#include "rh-str.h"

void rh_import_media_free(RhImportMediaFile *media)
{
    if (media == NULL)
        return;
    g_free(media->kind);
    g_free(media->filename);
    if (media->filename_raw != NULL)
        g_bytes_unref(media->filename_raw);
    g_free(media);
}

void rh_import_entry_free(RhImportPlanEntry *entry)
{
    if (entry == NULL)
        return;
    g_free(entry->source_subdir);
    if (entry->source_subdir_raw != NULL)
        g_bytes_unref(entry->source_subdir_raw);
    g_free(entry->platform);
    g_free(entry->name);
    g_free(entry->game_dir);
    g_ptr_array_free(entry->roms, TRUE);
    g_ptr_array_free(entry->roms_raw, TRUE);
    g_ptr_array_free(entry->media, TRUE);
    g_ptr_array_free(entry->patches, TRUE);
    g_ptr_array_free(entry->patches_raw, TRUE);
    g_free(entry);
}

void rh_import_plan_free(RhImportPlan *plan)
{
    if (plan == NULL)
        return;
    g_ptr_array_free(plan->entries, TRUE);
    g_free(plan);
}

static RhImportPlanEntry *entry_new(const char *subdir, GBytes *subdir_raw,
                                    const char *name)
{
    RhImportPlanEntry *entry = g_new0(RhImportPlanEntry, 1);
    entry->source_subdir = g_strdup(subdir);
    entry->source_subdir_raw = subdir_raw != NULL
        ? g_bytes_ref(subdir_raw) : g_bytes_new("", 0);
    entry->name = g_strdup(name);
    entry->roms = g_ptr_array_new_with_free_func(g_free);
    entry->roms_raw = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
    entry->media = g_ptr_array_new_with_free_func((GDestroyNotify)rh_import_media_free);
    entry->patches = g_ptr_array_new_with_free_func(g_free);
    entry->patches_raw = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
    return entry;
}

/* 统计未知编码的名字，供计划展示与后续批量修正 */
static void account_name(RhImportPlan *plan, const RhFName *name)
{
    if (name->encoding == RH_FNAME_GB18030)
        plan->guessed_names++;
    else if (name->encoding == RH_FNAME_ESCAPED)
        plan->escaped_names++;
}

/* 目录名是否就是媒体目录（covers/screenshots/...） */
static const gchar *media_dir_kind(const RhPlatformRules *rules, const char *name)
{
    for (gsize i = 0; i < G_N_ELEMENTS(rh_media_kind_names); i++) {
        const gchar *kind = rh_media_kind_names[i];
        const gchar *dir = rh_platform_media_dir(rules, kind);
        if (dir != NULL && g_strcmp0(dir, name) == 0)
            return kind;
    }
    return NULL;
}

/* 把文件归入条目；返回是否已被消费 */
static gboolean consume_file(RhImportPlan *plan, RhImportPlanEntry *entry,
                             const RhPlatformRules *rules, const char *rel_path,
                             GBytes *rel_raw, const char *basename)
{
    RhClassifyResult *result = rh_classify_file(rules, basename);
    gboolean consumed = TRUE;

    switch (result->kind) {
    case RH_CLASSIFY_ROM:
        g_ptr_array_add(entry->roms, g_strdup(rel_path));
        g_ptr_array_add(entry->roms_raw, g_bytes_ref(rel_raw));
        if (result->ambiguous)
            entry->ambiguous = TRUE;
        break;
    case RH_CLASSIFY_ARCHIVE:
        /* 压缩包整体作为一个 ROM；平台由内容决定，预扫描阶段不展开 */
        g_ptr_array_add(entry->roms, g_strdup(rel_path));
        g_ptr_array_add(entry->roms_raw, g_bytes_ref(rel_raw));
        break;
    case RH_CLASSIFY_PATCH:
        g_ptr_array_add(entry->patches, g_strdup(rel_path));
        g_ptr_array_add(entry->patches_raw, g_bytes_ref(rel_raw));
        break;
    case RH_CLASSIFY_MEDIA: {
        RhImportMediaFile *media = g_new0(RhImportMediaFile, 1);
        media->kind = g_strdup(result->media_kind);
        media->filename = g_strdup(rel_path);
        media->filename_raw = g_bytes_ref(rel_raw);
        g_ptr_array_add(entry->media, media);
        break;
    }
    case RH_CLASSIFY_UNKNOWN:
    default:
        plan->skipped_unknown++;
        consumed = FALSE;
        break;
    }

    rh_classify_result_free(result);
    return consumed;
}

static gchar *relative_join(const char *prefix, const char *name)
{
    return (prefix == NULL || prefix[0] == '\0')
               ? g_strdup(name)
               : g_strconcat(prefix, "/", name, NULL);
}

/* 用原始字节定位磁盘上的真实文件；Windows 上转义名不可定位，返回 NULL。 */
static gchar *child_native_path(const char *dir, const RhFName *name)
{
    gchar *native = rh_fname_native_name(name);
    if (native == NULL)
        return NULL;
    gchar *full = g_build_filename(dir, native, NULL);
    g_free(native);
    return full;
}

/* 扫描一个游戏目录（一层文件 + 一层媒体子目录） */
static RhImportPlanEntry *scan_game_dir(RhImportPlan *plan,
                                        const RhPlatformRules *rules,
                                        const char *root,
                                        const RhFName *subdir)
{
    RhImportPlanEntry *entry = entry_new(subdir->display, subdir->raw, subdir->display);
    gchar *dir_path = child_native_path(root, subdir);
    GDir *dir = dir_path != NULL ? g_dir_open(dir_path, 0, NULL) : NULL;
    if (dir == NULL) {
        g_free(dir_path);
        rh_import_entry_free(entry);
        return NULL;
    }

    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        RhFName *child = rh_fname_from_bytes(name, -1);
        if (child == NULL)
            continue;
        account_name(plan, child);

        gchar *full = child_native_path(dir_path, child);
        if (full == NULL) {
            rh_fname_free(child);
            continue;
        }

        if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
            const gchar *kind = media_dir_kind(rules, child->display);
            if (kind != NULL) {
                GDir *media_dir = g_dir_open(full, 0, NULL);
                if (media_dir != NULL) {
                    const gchar *media_name;
                    while ((media_name = g_dir_read_name(media_dir)) != NULL) {
                        RhFName *media_fname = rh_fname_from_bytes(media_name, -1);
                        if (media_fname == NULL)
                            continue;
                        account_name(plan, media_fname);

                        gchar *media_full = child_native_path(full, media_fname);
                        if (media_full != NULL
                            && g_file_test(media_full, G_FILE_TEST_IS_REGULAR)) {
                            RhImportMediaFile *media = g_new0(RhImportMediaFile, 1);
                            media->kind = g_strdup(kind);
                            media->filename =
                                relative_join(child->display, media_fname->display);
                            media->filename_raw =
                                rh_fname_join_raw(child->raw, media_fname->raw);
                            g_ptr_array_add(entry->media, media);
                            plan->files_scanned++;
                        }
                        g_free(media_full);
                        rh_fname_free(media_fname);
                    }
                    g_dir_close(media_dir);
                }
            }
            g_free(full);
            rh_fname_free(child);
            continue;
        }

        if (g_file_test(full, G_FILE_TEST_IS_REGULAR)) {
            plan->files_scanned++;
            consume_file(plan, entry, rules, child->display, child->raw, child->display);
        }
        g_free(full);
        rh_fname_free(child);
    }
    g_dir_close(dir);
    g_free(dir_path);

    if (entry->roms->len == 0 && entry->patches->len == 0) {
        rh_import_entry_free(entry);
        return NULL;
    }
    return entry;
}

/* 平台：目录名优先，其次看 ROM 候选是否唯一 */
static void resolve_platform(RhImportPlanEntry *entry, const RhPlatformRules *rules)
{
    RhPlatform *by_dir = rh_platform_by_id(rules, entry->source_subdir);
    if (by_dir != NULL) {
        entry->platform = g_strdup(by_dir->id);
        return;
    }

    gchar *decided = NULL;
    for (guint i = 0; i < entry->roms->len; i++) {
        const gchar *file = g_ptr_array_index(entry->roms, i);
        GPtrArray *candidates = rh_platform_candidates_for_file(rules, file);
        if (candidates->len == 1) {
            const gchar *id = ((RhPlatform *)g_ptr_array_index(candidates, 0))->id;
            if (decided == NULL)
                decided = g_strdup(id);
            else if (g_strcmp0(decided, id) != 0) {
                g_free(decided);
                decided = NULL;
                g_ptr_array_free(candidates, TRUE);
                break;
            }
        } else if (candidates->len > 1) {
            g_free(decided);
            decided = NULL;
            g_ptr_array_free(candidates, TRUE);
            break;
        }
        g_ptr_array_free(candidates, TRUE);
    }

    entry->platform = decided;
    if (decided == NULL)
        entry->ambiguous = TRUE;
}

static void fill_game_dir(RhImportPlanEntry *entry, const char *root)
{
    if (entry->platform == NULL)
        return;

    gchar *safe_name = rh_str_sanitize_component(entry->name);
    entry->game_dir = g_strconcat(entry->platform, "/", safe_name, NULL);
    g_free(safe_name);

    gchar *target = g_build_filename(root, entry->platform, entry->name, NULL);
    entry->target_exists = g_file_test(target, G_FILE_TEST_IS_DIR);
    g_free(target);
}

static gint cmp_fnames(gconstpointer a, gconstpointer b)
{
    const RhFName *fa = *(const RhFName *const *)a;
    const RhFName *fb = *(const RhFName *const *)b;
    return g_bytes_compare(fa->raw, fb->raw);
}

RhImportPlan *rh_import_plan_scan(const RhPlatformRules *rules, const char *source_dir,
                                  RhErr *out_err)
{
    RhImportPlan *plan = g_new0(RhImportPlan, 1);
    plan->entries = g_ptr_array_new_with_free_func((GDestroyNotify)rh_import_entry_free);

    if (out_err != NULL)
        *out_err = RH_OK;
    if (rules == NULL || source_dir == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return plan;
    }

    GDir *dir = g_dir_open(source_dir, 0, NULL);
    if (dir == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_IO;
        return plan;
    }

    /* 先收集并排序，保证计划稳定可复现；按原始字节排序，不受解码影响 */
    GPtrArray *names = g_ptr_array_new_with_free_func((GDestroyNotify)rh_fname_free);
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        RhFName *fname = rh_fname_from_bytes(name, -1);
        if (fname != NULL)
            g_ptr_array_add(names, fname);
    }
    g_dir_close(dir);
    g_ptr_array_sort(names, cmp_fnames);

    for (guint i = 0; i < names->len; i++) {
        RhFName *item = g_ptr_array_index(names, i);
        account_name(plan, item);

        gchar *full = child_native_path(source_dir, item);
        if (full == NULL)
            continue;

        if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
            RhImportPlanEntry *entry = scan_game_dir(plan, rules, source_dir, item);
            if (entry != NULL) {
                resolve_platform(entry, rules);
                fill_game_dir(entry, source_dir);
                g_ptr_array_add(plan->entries, entry);
            }
        } else if (g_file_test(full, G_FILE_TEST_IS_REGULAR)) {
            plan->files_scanned++;
            RhClassifyResult *result = rh_classify_file(rules, item->display);
            if (result->kind == RH_CLASSIFY_MEDIA) {
                plan->skipped_media++;
            } else if (result->kind == RH_CLASSIFY_UNKNOWN) {
                plan->skipped_unknown++;
            } else {
                RhImportPlanEntry *entry = entry_new("", NULL, item->display);
                g_ptr_array_add(entry->roms, g_strdup(item->display));
                g_ptr_array_add(entry->roms_raw, g_bytes_ref(item->raw));
                if (result->kind == RH_CLASSIFY_ROM) {
                    entry->ambiguous = result->ambiguous;
                }
                resolve_platform(entry, rules);
                fill_game_dir(entry, source_dir);
                g_ptr_array_add(plan->entries, entry);
            }
            rh_classify_result_free(result);
        }

        g_free(full);
    }

    g_ptr_array_free(names, TRUE);
    return plan;
}
