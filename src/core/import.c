#include "import.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <glib/gstdio.h>

#include "classify.h"
#include "dat.h"
#include "rh-archive.h"
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
    g_free(entry->rom_format);
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
    entry->rom_format = g_strdup("raw");
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

/* rom_format 只接受 DDL 里的取值；.rar 等无语义容器按 raw 记。 */
static gchar *container_format_for(const gchar *filename)
{
    gchar *ext = rh_str_extension(filename);
    gchar *format = NULL;
    if (ext != NULL && g_strcmp0(ext, ".zip") == 0)
        format = g_strdup("zip");
    else if (ext != NULL && g_strcmp0(ext, ".7z") == 0)
        format = g_strdup("7z");
    else if (ext != NULL && g_strcmp0(ext, ".chd") == 0)
        format = g_strdup("chd");
    else if (ext != NULL && g_strcmp0(ext, ".cso") == 0)
        format = g_strdup("cso");
    else if (ext != NULL && g_strcmp0(ext, ".wbfs") == 0)
        format = g_strdup("wbfs");
    g_free(ext);
    return format != NULL ? format : g_strdup("raw");
}

static void add_single_entry(RhImportPlan *plan, const RhPlatformRules *rules,
                             const char *source_dir, RhFName *item,
                             gboolean ambiguous, gboolean is_arcade,
                             const gchar *rom_format,
                             const gchar *fallback_platform)
{
    RhImportPlanEntry *entry = entry_new("", NULL, item->display);
    g_ptr_array_add(entry->roms, g_strdup(item->display));
    g_ptr_array_add(entry->roms_raw, g_bytes_ref(item->raw));
    entry->ambiguous = ambiguous;
    entry->is_arcade = is_arcade;
    if (rom_format != NULL) {
        g_free(entry->rom_format);
        entry->rom_format = g_strdup(rom_format);
    }
    resolve_platform(entry, rules);
    if (entry->platform == NULL && fallback_platform != NULL)
        entry->platform = g_strdup(fallback_platform);
    fill_game_dir(entry, source_dir);
    g_ptr_array_add(plan->entries, entry);
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
            } else if (result->kind == RH_CLASSIFY_ARCHIVE) {
                RhArchiveList *list = NULL;
                RhErr list_err = rh_archive_list(full, &list);
                if (list_err != RH_OK) {
                    /* 列不出索引（损坏/不支持的容器）：按普通文件保留，不丢数据 */
                    add_single_entry(plan, rules, source_dir, item, FALSE, FALSE,
                                     container_format_for(item->display), NULL);
                } else {
                    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
                    for (guint k = 0; k < list->entries->len; k++) {
                        RhArchiveEntry *e = g_ptr_array_index(list->entries, k);
                        if (!e->is_dir)
                            g_ptr_array_add(paths, g_strdup(e->pathname));
                    }
                    RhArchiveClassifyResult *ac = rh_classify_archive(rules, paths);
                    if (ac->branch == RH_ARCHIVE_PATCHES)
                        plan->skipped_patch_archives++;
                    else if (ac->branch == RH_ARCHIVE_MIXED)
                        plan->skipped_mixed_archives++;
                    else if (ac->branch == RH_ARCHIVE_AMBIGUOUS)
                        plan->skipped_ambiguous_archives++;
                    else
                        add_single_entry(plan, rules, source_dir, item,
                                         FALSE,
                                         ac->branch == RH_ARCHIVE_ARCADE,
                                         container_format_for(item->display),
                                         ac->platform);
                    rh_archive_classify_free(ac);
                    g_ptr_array_free(paths, TRUE);
                    rh_archive_list_free(list);
                }
            } else {
                add_single_entry(plan, rules, source_dir, item,
                                 result->ambiguous, FALSE, NULL, NULL);
            }
            rh_classify_result_free(result);
        }

        g_free(full);
    }

    g_ptr_array_free(names, TRUE);
    return plan;
}

/* ===========================================================================
 * 执行阶段
 * ========================================================================= */

typedef struct {
    GPtrArray *files;   /* gchar*：本次落盘的最终路径 */
    GPtrArray *dirs;    /* gchar*：本次真正创建的目录，按创建顺序 */
} EntryWrites;

static void entry_writes_init(EntryWrites *writes)
{
    writes->files = g_ptr_array_new_with_free_func(g_free);
    writes->dirs = g_ptr_array_new_with_free_func(g_free);
}

/* 释放列表；rollback 为 TRUE 时先删文件，再从深到浅删本次创建的目录。 */
static void entry_writes_free(EntryWrites *writes, gboolean rollback)
{
    if (writes->files != NULL) {
        if (rollback) {
            for (gint i = (gint)writes->files->len - 1; i >= 0; i--)
                g_remove(g_ptr_array_index(writes->files, i));
        }
        g_ptr_array_free(writes->files, TRUE);
        writes->files = NULL;
    }
    if (writes->dirs != NULL) {
        if (rollback) {
            for (gint i = (gint)writes->dirs->len - 1; i >= 0; i--)
                g_rmdir(g_ptr_array_index(writes->dirs, i));
        }
        g_ptr_array_free(writes->dirs, TRUE);
        writes->dirs = NULL;
    }
}

/* 只创建缺失的层级，并记录本次真正创建的目录，回滚时不误删已有目录。 */
static void ensure_dir(const gchar *dir, EntryWrites *writes)
{
    if (g_file_test(dir, G_FILE_TEST_IS_DIR))
        return;
    gchar *parent = g_path_get_dirname(dir);
    if (g_strcmp0(parent, dir) != 0)
        ensure_dir(parent, writes);
    g_free(parent);
    if (g_mkdir(dir, 0755) == 0)
        g_ptr_array_add(writes->dirs, g_strdup(dir));
}

static gchar *native_from_raw(const gchar *dir, GBytes *raw)
{
    if (raw == NULL)
        return g_strdup(dir);
    gsize len = 0;
    const gchar *data = g_bytes_get_data(raw, &len);
    if (len == 0)
        return g_strdup(dir);

    RhFName *name = rh_fname_from_bytes(data, (gssize)len);
    if (name == NULL)
        return NULL;
    gchar *native = rh_fname_native_name(name);
    gchar *full = native != NULL ? g_build_filename(dir, native, NULL) : NULL;
    g_free(native);
    rh_fname_free(name);
    return full;
}

static gchar *source_path(const gchar *source_dir,
                          const RhImportPlanEntry *entry, GBytes *rel_raw)
{
    gchar *base = native_from_raw(source_dir, entry->source_subdir_raw);
    if (base == NULL)
        return NULL;
    gchar *full = native_from_raw(base, rel_raw);
    g_free(base);
    return full;
}

static gchar *target_path(const RhImportExecOptions *options,
                          const RhImportPlanEntry *entry, const gchar *filename)
{
    return g_build_filename(options->root, entry->game_dir, filename, NULL);
}

/*
 * 复制到同目录临时名，成功后原子改名。CRC32 在复制过程中顺带算出，
 * 不额外读盘。src 为平台原生路径，dst 为 UTF-8。
 */
static RhErr copy_one(const gchar *src, const gchar *dst,
                      guint32 *out_crc, gint64 *out_size, gchar **out_error)
{
    FILE *in = g_fopen(src, "rb");
    if (in == NULL) {
        *out_error = g_strdup_printf("无法读取来源：%s", g_strerror(errno));
        return RH_ERR_IO;
    }

    gchar *tmp = g_strdup_printf("%s.romham-part", dst);
    FILE *out = g_fopen(tmp, "wb");
    if (out == NULL) {
        *out_error = g_strdup_printf("无法创建目标：%s", g_strerror(errno));
        fclose(in);
        g_free(tmp);
        return RH_ERR_IO;
    }

    guchar buffer[64 * 1024];
    guint32 crc = (guint32)crc32(0L, Z_NULL, 0);
    gint64 total = 0;
    gboolean ok = TRUE;
    for (;;) {
        size_t n = fread(buffer, 1, sizeof buffer, in);
        if (n > 0) {
            crc = (guint32)crc32(crc, buffer, (uInt)n);
            if (fwrite(buffer, 1, n, out) != n) {
                ok = FALSE;
                break;
            }
            total += (gint64)n;
        }
        if (n < sizeof buffer) {
            if (ferror(in))
                ok = FALSE;
            break;
        }
    }
    if (fclose(out) != 0)
        ok = FALSE;
    if (fclose(in) != 0)
        ok = FALSE;
    if (ok && g_rename(tmp, dst) != 0) {
        *out_error = g_strdup_printf("改名失败：%s", g_strerror(errno));
        ok = FALSE;
    }
    if (!ok) {
        g_remove(tmp);
        if (*out_error == NULL)
            *out_error = g_strdup("写入目标文件失败");
    }
    g_free(tmp);
    if (!ok)
        return RH_ERR_IO;

    *out_crc = crc;
    *out_size = total;
    return RH_OK;
}

static gchar *new_game_id(RhStore *store)
{
    for (int attempt = 0; attempt < 64; attempt++) {
        gchar *id = g_strdup_printf("%08x", g_random_int());
        RhErr err = RH_OK;
        if (!store_game_id_exists(store, id, &err))
            return err == RH_OK ? id : NULL;
        g_free(id);
        if (err != RH_OK)
            return NULL;
    }
    return NULL;
}

static gboolean name_is_archive(const RhPlatformRules *rules, const gchar *filename)
{
    RhClassifyResult *result = rh_classify_file(rules, filename);
    gboolean archive = result != NULL && result->kind == RH_CLASSIFY_ARCHIVE;
    rh_classify_result_free(result);
    return archive;
}

/* 通用补丁扩展名；其余被判为补丁的视为平台专属（如 .ips32）。 */
static gboolean patch_ext_is_generic(const gchar *ext)
{
    static const gchar *const generic[] = {
        ".ips", ".bps", ".ups", ".ppf", ".xdelta", ".xdelta3", ".patch"
    };
    for (gsize i = 0; i < G_N_ELEMENTS(generic); i++) {
        if (g_strcmp0(ext, generic[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

static gchar *patch_type_for(const GPtrArray *patches)
{
    if (patches == NULL || patches->len == 0)
        return g_strdup("");

    gboolean generic = FALSE;
    gboolean platform = FALSE;
    for (guint i = 0; i < patches->len; i++) {
        gchar *ext = rh_str_extension(g_ptr_array_index(patches, i));
        if (ext == NULL) {
            platform = TRUE;
        } else if (patch_ext_is_generic(ext)) {
            generic = TRUE;
        } else {
            platform = TRUE;
        }
        g_free(ext);
    }
    if (generic && platform)
        return g_strdup("mixed");
    return g_strdup(generic ? "generic" : "platform");
}

static gchar *m3u_name_for(const RhImportPlanEntry *entry)
{
    gchar *safe = rh_str_sanitize_component(entry->name);
    gchar *name = g_strconcat(safe, ".m3u", NULL);
    g_free(safe);
    return name;
}

static RhErr write_text_atomic(const gchar *dst, const gchar *text,
                              gchar **out_error)
{
    gchar *tmp = g_strdup_printf("%s.romham-part", dst);
    if (!g_file_set_contents(tmp, text, -1, NULL)) {
        *out_error = g_strdup("写入 m3u 失败");
        g_free(tmp);
        return RH_ERR_IO;
    }
    if (g_rename(tmp, dst) != 0) {
        *out_error = g_strdup_printf("m3u 改名失败：%s", g_strerror(errno));
        g_remove(tmp);
        g_free(tmp);
        return RH_ERR_IO;
    }
    g_free(tmp);
    return RH_OK;
}

/* CRC32 命中唯一 DAT 条目时回填来源；二义/未命中都不挑。 */
static void match_rom(RhStore *store, const RhImportPlanEntry *entry,
                      RhRomFile *rom, RhGame *game, guint32 crc, gint64 size)
{
    RhHashResult hash;
    memset(&hash, 0, sizeof hash);
    hash.size = (guint64)size;
    hash.has_crc32 = TRUE;
    hash.crc32 = crc;

    GPtrArray *matches = NULL;
    RhErr err = RH_OK;
    RhDatMatchKind kind = rh_dat_match(store, &hash, entry->platform,
                                       &matches, &err);
    if (kind == RH_DAT_MATCH_UNIQUE && matches != NULL && matches->len == 1) {
        RhDatMatch *match = g_ptr_array_index(matches, 0);
        rom->dat_id = match->dat_id;
        if (match->entry->sha1 != NULL)
            rom->sha1 = g_strdup(match->entry->sha1);
        if (match->entry->md5 != NULL)
            rom->md5 = g_strdup(match->entry->md5);
        if (game->dat_source == NULL)
            game->dat_source = g_strdup(match->dat_name);
    }
    if (matches != NULL)
        g_ptr_array_unref(matches);
}

static void remember_file(EntryWrites *writes, const gchar *path)
{
    g_ptr_array_add(writes->files, g_strdup(path));
}

/* 作业条目在库里的稳定标识：目录条目用子目录名，顶层文件用文件名。 */
static const gchar *entry_label(const RhImportPlanEntry *entry)
{
    if (entry->source_subdir != NULL && *entry->source_subdir != '\0')
        return entry->source_subdir;
    return entry->name != NULL ? entry->name : "";
}

static void mark_job_item(RhStore *store, const RhImportExecOptions *options,
                          guint seq, const RhImportPlanEntry *entry,
                          const gchar *state, const gchar *target_id,
                          const gchar *message)
{
    if (options->job_id <= 0)
        return;
    store_job_item_set(store, options->job_id, (int)seq, entry_label(entry),
                       "import", entry->platform, target_id, state, message);
}

typedef enum {
    ENTRY_IMPORTED,
    ENTRY_SKIPPED,
    ENTRY_FAILED
} EntryOutcome;

static EntryOutcome execute_entry(RhStore *store, const RhPlatformRules *rules,
                                  const RhImportPlanEntry *entry,
                                  const RhImportExecOptions *options,
                                  guint seq, gchar **out_message)
{
    if (entry->platform == NULL || entry->game_dir == NULL) {
        *out_message = g_strdup("平台未确定");
        return ENTRY_SKIPPED;
    }
    if (entry->roms->len == 0) {
        *out_message = g_strdup("没有可导入的 ROM");
        return ENTRY_SKIPPED;
    }

    /* 碟号识别：至少两个不同碟号才算多碟，避免把 cue/bin 误判成多碟。 */
    gboolean disc_seen[100] = {FALSE};
    gint disc_count = 0;
    gint max_disc = 0;
    for (guint i = 0; i < entry->roms->len; i++) {
        int disc = 0;
        if (rh_str_parse_disc(g_ptr_array_index(entry->roms, i), &disc) &&
            disc > 0 && disc < 100) {
            if (!disc_seen[disc]) {
                disc_seen[disc] = TRUE;
                disc_count++;
            }
            if (disc > max_disc)
                max_disc = disc;
        }
    }
    gboolean multi_disc = disc_count >= 2;
    gchar *m3u_name = NULL;

    /* 先查冲突：任一目标文件已存在就整条跳过，绝不覆盖。 */
    for (guint i = 0; i < entry->roms->len; i++) {
        gchar *dst = target_path(options, entry, g_ptr_array_index(entry->roms, i));
        gboolean exists = g_file_test(dst, G_FILE_TEST_EXISTS);
        g_free(dst);
        if (exists) {
            *out_message = g_strdup("目标文件已存在");
            return ENTRY_SKIPPED;
        }
    }
    for (guint i = 0; i < entry->media->len; i++) {
        RhImportMediaFile *m = g_ptr_array_index(entry->media, i);
        gchar *dst = target_path(options, entry, m->filename);
        gboolean exists = g_file_test(dst, G_FILE_TEST_EXISTS);
        g_free(dst);
        if (exists) {
            *out_message = g_strdup("目标媒体文件已存在");
            return ENTRY_SKIPPED;
        }
    }
    for (guint i = 0; i < entry->patches->len; i++) {
        gchar *dst = target_path(options, entry, g_ptr_array_index(entry->patches, i));
        gboolean exists = g_file_test(dst, G_FILE_TEST_EXISTS);
        g_free(dst);
        if (exists) {
            *out_message = g_strdup("目标补丁文件已存在");
            return ENTRY_SKIPPED;
        }
    }

    if (multi_disc && options->generate_m3u) {
        m3u_name = m3u_name_for(entry);
        gchar *dst = target_path(options, entry, m3u_name);
        gboolean exists = g_file_test(dst, G_FILE_TEST_EXISTS);
        g_free(dst);
        if (exists) {
            g_free(m3u_name);
            *out_message = g_strdup("目标 m3u 已存在");
            return ENTRY_SKIPPED;
        }
    }

    RhGame *game = rh_game_new();
    game->id = new_game_id(store);
    if (game->id == NULL) {
        g_free(m3u_name);
        rh_game_free(game);
        *out_message = g_strdup("无法生成游戏 ID");
        return ENTRY_FAILED;
    }
    game->platform = g_strdup(entry->platform);
    game->name = g_strdup(entry->name);
    game->game_main_name = g_strdup(entry->name);
    game->dir_path = g_strdup(entry->game_dir);
    game->is_multi_disc = multi_disc;
    game->is_arcade = entry->is_arcade;
    if (entry->rom_format != NULL) {
        g_free(game->rom_format);
        game->rom_format = g_strdup(entry->rom_format);
    }
    g_free(game->patch_type);
    game->patch_type = patch_type_for(entry->patches);

    GPtrArray *roms = g_ptr_array_new_with_free_func(
        (GDestroyNotify)rh_rom_file_free);
    GPtrArray *media = g_ptr_array_new_with_free_func(
        (GDestroyNotify)rh_media_file_free);
    EntryWrites writes;
    entry_writes_init(&writes);

    gchar *game_dir = g_build_filename(options->root, entry->game_dir, NULL);
    ensure_dir(game_dir, &writes);
    g_free(game_dir);

    RhErr err = RH_OK;
    gchar *error = NULL;

    for (guint i = 0; i < entry->roms->len && err == RH_OK; i++) {
        const gchar *display = g_ptr_array_index(entry->roms, i);
        GBytes *raw = g_ptr_array_index(entry->roms_raw, i);
        gchar *src = source_path(options->source_dir, entry, raw);
        if (src == NULL) {
            err = RH_ERR_INVALID;
            error = g_strdup("来源文件名无法在当前平台定位");
            break;
        }
        gchar *dst = target_path(options, entry, display);
        gchar *parent = g_path_get_dirname(dst);
        ensure_dir(parent, &writes);
        g_free(parent);

        guint32 crc = 0;
        gint64 size = 0;
        err = copy_one(src, dst, &crc, &size, &error);
        g_free(src);
        if (err != RH_OK) {
            g_free(dst);
            break;
        }
        remember_file(&writes, dst);

        RhRomFile *rom = rh_rom_file_new();
        rom->filename = g_strdup(display);
        rom->filename_raw = g_bytes_ref(raw);
        rom->has_size = TRUE;
        rom->size = size;
        rom->crc32 = g_strdup_printf("%08x", crc);
        rom->sort = (gint)i;
        int disc = 0;
        if (rh_str_parse_disc(display, &disc) && disc > 0 && disc < 100)
            rom->disc_no = disc;
        if (options->match_dat && !name_is_archive(rules, display))
            match_rom(store, entry, rom, game, crc, size);
        g_ptr_array_add(roms, rom);
        g_free(dst);
    }

    for (guint i = 0; i < entry->media->len && err == RH_OK; i++) {
        RhImportMediaFile *m = g_ptr_array_index(entry->media, i);
        gchar *src = source_path(options->source_dir, entry, m->filename_raw);
        if (src == NULL) {
            err = RH_ERR_INVALID;
            error = g_strdup("来源媒体文件名无法定位");
            break;
        }
        gchar *dst = target_path(options, entry, m->filename);
        gchar *parent = g_path_get_dirname(dst);
        ensure_dir(parent, &writes);
        g_free(parent);

        guint32 crc = 0;
        gint64 size = 0;
        err = copy_one(src, dst, &crc, &size, &error);
        g_free(src);
        if (err == RH_OK) {
            remember_file(&writes, dst);
            RhMediaFile *media_file = rh_media_file_new();
            media_file->kind = g_strdup(m->kind);
            media_file->filename = g_strdup(m->filename);
            media_file->filename_raw = m->filename_raw != NULL
                ? g_bytes_ref(m->filename_raw) : NULL;
            media_file->has_size = TRUE;
            media_file->size = size;
            media_file->sort = (gint)i;
            g_ptr_array_add(media, media_file);
        }
        g_free(dst);
    }

    for (guint i = 0; i < entry->patches->len && err == RH_OK; i++) {
        const gchar *display = g_ptr_array_index(entry->patches, i);
        GBytes *raw = g_ptr_array_index(entry->patches_raw, i);
        gchar *src = source_path(options->source_dir, entry, raw);
        if (src == NULL) {
            err = RH_ERR_INVALID;
            error = g_strdup("来源补丁文件名无法定位");
            break;
        }
        gchar *dst = target_path(options, entry, display);
        guint32 crc = 0;
        gint64 size = 0;
        err = copy_one(src, dst, &crc, &size, &error);
        g_free(src);
        if (err == RH_OK) {
            remember_file(&writes, dst);
            RhMediaFile *patch = rh_media_file_new();
            patch->kind = g_strdup("patcher");
            patch->filename = g_strdup(display);
            patch->filename_raw = raw != NULL ? g_bytes_ref(raw) : NULL;
            patch->has_size = TRUE;
            patch->size = size;
            patch->sort = (gint)i;
            g_ptr_array_add(media, patch);
        }
        g_free(dst);
    }

    /* 多碟：按碟号顺序生成 m3u，内容只列带碟号的 ROM。 */
    if (err == RH_OK && m3u_name != NULL) {
        GString *playlist = g_string_new(NULL);
        for (gint disc = 1; disc <= max_disc; disc++) {
            for (guint i = 0; i < roms->len; i++) {
                RhRomFile *rom = g_ptr_array_index(roms, i);
                if (rom->disc_no == disc)
                    g_string_append_printf(playlist, "%s\n", rom->filename);
            }
        }
        gchar *dst = target_path(options, entry, m3u_name);
        err = write_text_atomic(dst, playlist->str, &error);
        if (err == RH_OK)
            remember_file(&writes, dst);
        g_free(dst);
        g_string_free(playlist, TRUE);
    }
    g_free(m3u_name);

    if (err == RH_OK) {
        err = store_begin(store);
        if (err == RH_OK)
            err = store_game_insert(store, game, roms, media, NULL);
        if (err == RH_OK && options->job_id > 0)
            err = store_job_item_set(store, options->job_id, (int)seq,
                                     entry_label(entry),
                                     "import", entry->platform, game->id,
                                     "imported", NULL);
        if (err == RH_OK)
            err = store_commit(store);
        else
            store_rollback(store);
    }

    if (err != RH_OK) {
        entry_writes_free(&writes, TRUE);
        if (error == NULL)
            error = g_strdup(rh_err_str(err));
        *out_message = error;
        g_ptr_array_free(roms, TRUE);
        g_ptr_array_free(media, TRUE);
        rh_game_free(game);
        return ENTRY_FAILED;
    }

    entry_writes_free(&writes, FALSE);
    g_ptr_array_free(roms, TRUE);
    g_ptr_array_free(media, TRUE);
    rh_game_free(game);
    *out_message = NULL;
    return ENTRY_IMPORTED;
}

RhErr rh_import_execute(RhStore *store, const RhPlatformRules *rules,
                        const RhImportPlan *plan,
                        const RhImportExecOptions *options,
                        RhImportExecStats *out_stats)
{
    if (store == NULL || rules == NULL || plan == NULL || options == NULL ||
        options->root == NULL || options->source_dir == NULL)
        return RH_ERR_INVALID;

    RhImportExecStats stats = {0};
    RhErr result = RH_OK;
    guint total = plan->entries->len;

    for (guint i = 0; i < total; i++) {
        if (options->cancel != NULL && options->cancel(options->cancel_data)) {
            stats.cancelled = TRUE;
            result = RH_ERR_CANCELLED;
            break;
        }

        RhImportPlanEntry *entry = g_ptr_array_index(plan->entries, i);

        /* 续做：同一作业里已完成的条目不再碰磁盘。标签用来源子目录/文件名，
         * 不依赖扫描顺序。 */
        if (options->job_id > 0) {
            gchar *state = NULL;
            store_job_item_state(store, options->job_id, entry_label(entry),
                                 entry->platform, &state);
            gboolean finished = state != NULL &&
                (g_strcmp0(state, "imported") == 0 ||
                 g_strcmp0(state, "skipped") == 0);
            g_free(state);
            if (finished) {
                stats.imported++;
                if (options->progress != NULL)
                    options->progress(i + 1, total, entry->name,
                                      options->user_data);
                store_job_progress(store, options->job_id, (int)(i + 1));
                continue;
            }
        }

        gchar *message = NULL;
        EntryOutcome outcome = execute_entry(store, rules, entry, options, i,
                                             &message);
        if (outcome == ENTRY_IMPORTED) {
            stats.imported++;
        } else if (outcome == ENTRY_SKIPPED) {
            stats.skipped++;
            mark_job_item(store, options, i, entry, "skipped", NULL, message);
        } else {
            stats.failed++;
            mark_job_item(store, options, i, entry, "failed", NULL, message);
        }
        g_free(message);

        if (options->progress != NULL)
            options->progress(i + 1, total, entry->name, options->user_data);
        if (options->job_id > 0)
            store_job_progress(store, options->job_id, (int)(i + 1));
    }

    if (options->job_id > 0) {
        if (stats.cancelled)
            store_job_set_state(store, options->job_id, "cancelled");
        else
            store_job_finish(store, options->job_id, NULL);
    }

    if (out_stats != NULL)
        *out_stats = stats;
    return result;
}
