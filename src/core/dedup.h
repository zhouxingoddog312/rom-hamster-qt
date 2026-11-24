/*
 * dedup.h —— 重复文件识别（ARCHITECTURE.md §5）
 *
 * 边界：只读文件与库，按内容摘要聚类；不删除、不改名、不打印。
 * 去头摘要规则见 specs/dedup-phase1/spec.md。
 */
#ifndef RH_DEDUP_H
#define RH_DEDUP_H

#include <glib.h>

#include "rh-err.h"
#include "rh-records.h"
#include "store.h"

/* 计算应跳过的容器头字节数；无法读取或参数非法返回 FALSE，*out_skip = 0。 */
gboolean rh_dedup_header_skip(const char *abs_path, const char *filename,
                              gint64 size, guint64 *out_skip);

/* 返回 FALSE 取消；done/total 为已完成/总条目数。 */
typedef gboolean (*RhDedupProgressFn)(const char *path, guint done,
                                      guint total, gpointer user_data);

typedef struct {
    const char *root;
    const char *platform;        /* NULL = 全部平台 */
    gboolean    include_media;
    gboolean    backfill;        /* 先补算缺失摘要 */
    gint64      min_size;        /* 0 = 不限 */
    RhDedupProgressFn progress;
    gpointer    user_data;
} RhDedupOptions;

typedef struct {
    guint  hashed;
    guint  failed;               /* 读盘失败或文件缺失 */
    guint  clusters;
    guint  extra_copies;
    gint64 bytes_duplicated;
} RhDedupStats;

/* 为缺失摘要的 ROM/媒体补算 sha1、crc32 与 canon_sha1。 */
RhErr rh_dedup_backfill(RhStore *store, const RhDedupOptions *options,
                        RhDedupStats *out_stats);

/* 返回 GPtrArray<RhDupGroup*>（调用方 g_ptr_array_unref）。
 * options->backfill 为 TRUE 时先补算；成员 missing 标记磁盘状态。 */
RhErr rh_dedup_report(RhStore *store, const RhDedupOptions *options,
                      GPtrArray **out_groups, RhDedupStats *out_stats);

/*
 * 导入查重：abs_path 为待导入文件，root 为库根，rel_name 为库内相对名。
 * 无重复时 *out_match = NULL；命中时返回新分配的成员，调用方负责释放。
 */
RhErr rh_dedup_check_file(RhStore *store, const char *root,
                          const char *abs_path, const char *platform,
                          const char *rel_name, RhDupMember **out_match);

#endif /* RH_DEDUP_H */
