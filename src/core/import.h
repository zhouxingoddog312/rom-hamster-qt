/*
 * import.h —— 导入预扫描（ARCHITECTURE.md §5、§6.2）
 *
 * 只读阶段：遍历来源目录、分类文件、给出计划，不创建目录、不复制、不改库。
 * 所有写操作属于执行阶段（后续实现）。
 *
 * 每个名字同时保留「显示名」（UTF-8，可能来自 GB18030 推测或转义）与
 * 「原始字节」：显示名用于 UI/入库，原始字节用于定位磁盘上的真实文件
 * （见 rh-fname.h）。绝不因编码推测重命名来源文件。
 */
#ifndef RH_IMPORT_H
#define RH_IMPORT_H

#include <glib.h>

#include "rh-err.h"
#include "rh-fname.h"
#include "platform.h"

typedef struct {
    gchar  *kind;          /* 媒体类型（RhMediaKind） */
    gchar  *filename;      /* 相对所在游戏目录，用 '/' */
    GBytes *filename_raw;  /* 同一路径的原始字节 */
} RhImportMediaFile;

typedef struct {
    gchar     *source_subdir;     /* 相对来源根目录；顶层文件为空串 */
    GBytes    *source_subdir_raw; /* source_subdir 的原始字节 */
    gchar     *platform;          /* 规范平台 ID，可 NULL */
    gchar     *name;              /* 推断的显示名 */
    gchar     *game_dir;          /* 计划目录 "<platform>/<name>"，未定平台时 NULL */
    GPtrArray *roms;              /* gchar*，相对 source_subdir */
    GPtrArray *roms_raw;          /* GBytes*，与 roms 一一对应 */
    GPtrArray *media;             /* RhImportMediaFile* */
    GPtrArray *patches;           /* gchar* */
    GPtrArray *patches_raw;       /* GBytes*，与 patches 一一对应 */
    gboolean   ambiguous;         /* 平台无法唯一确定 */
    gboolean   target_exists;     /* 目标目录已存在（只读探测） */
} RhImportPlanEntry;

typedef struct {
    GPtrArray *entries;        /* RhImportPlanEntry* */
    guint      files_scanned;
    guint      skipped_unknown;
    guint      skipped_media;
    guint      guessed_names;  /* 按 GB18030 推测成功的名字数 */
    guint      escaped_names;  /* 无法解码、只能转义的名字数 */
} RhImportPlan;

void rh_import_media_free(RhImportMediaFile *media);
void rh_import_entry_free(RhImportPlanEntry *entry);
RhImportPlan *rh_import_plan_scan(const RhPlatformRules *rules, const char *source_dir,
                                  RhErr *out_err);
void rh_import_plan_free(RhImportPlan *plan);

#endif /* RH_IMPORT_H */
