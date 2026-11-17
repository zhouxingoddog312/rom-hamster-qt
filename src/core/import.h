/*
 * import.h —— 导入预扫描（ARCHITECTURE.md §5、§6.2）
 *
 * 只读阶段：遍历来源目录、分类文件、给出计划，不创建目录、不复制、不改库。
 * 所有写操作属于执行阶段（后续实现）。
 */
#ifndef RH_IMPORT_H
#define RH_IMPORT_H

#include <glib.h>

#include "rh-err.h"
#include "platform.h"

typedef struct {
    gchar *kind;      /* 媒体类型（RhMediaKind） */
    gchar *filename;  /* 相对所在游戏目录，用 '/' */
} RhImportMediaFile;

typedef struct {
    gchar     *source_subdir; /* 相对来源根目录；顶层文件为空串 */
    gchar     *platform;      /* 规范平台 ID，可 NULL */
    gchar     *name;          /* 推断的显示名 */
    gchar     *game_dir;      /* 计划目录 "<platform>/<name>"，未定平台时 NULL */
    GPtrArray *roms;          /* gchar*，相对 source_subdir */
    GPtrArray *media;         /* RhImportMediaFile* */
    GPtrArray *patches;       /* gchar* */
    gboolean   ambiguous;     /* 平台无法唯一确定 */
    gboolean   target_exists; /* 目标目录已存在（只读探测） */
} RhImportPlanEntry;

typedef struct {
    GPtrArray *entries;        /* RhImportPlanEntry* */
    guint      files_scanned;
    guint      skipped_unknown;
    guint      skipped_media;
} RhImportPlan;

void rh_import_media_free(RhImportMediaFile *media);
void rh_import_entry_free(RhImportPlanEntry *entry);
RhImportPlan *rh_import_plan_scan(const RhPlatformRules *rules, const char *source_dir,
                                  RhErr *out_err);
void rh_import_plan_free(RhImportPlan *plan);

#endif /* RH_IMPORT_H */
