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
#include "store.h"

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

/* ---------------------------------------------------------------------------
 * 执行阶段（ARCHITECTURE.md §6.2）：零交互，复制 + 原子改名 + 单条事务。
 * ------------------------------------------------------------------------- */

typedef struct {
    guint    imported;
    guint    skipped;      /* 平台未定、目标已存在等，不写库 */
    guint    failed;       /* 复制或写库失败，已回滚 */
    gboolean cancelled;
} RhImportExecStats;

typedef gboolean (*RhImportCancelFn)(gpointer user_data);
typedef void (*RhImportExecProgressFn)(guint done, guint total,
                                       const gchar *name, gpointer user_data);

typedef struct {
    const gchar *root;        /* 库根目录，UTF-8 */
    const gchar *source_dir;  /* 预扫描时的来源根目录，UTF-8 */
    gboolean     match_dat;   /* TRUE：复制时算 CRC32 并查已导入的 DAT */
    gint64       job_id;      /* >0：条目状态与游戏记录同事务写 job_items */
    RhImportCancelFn cancel;  /* 每个条目开始时检查，返回 TRUE 取消 */
    gpointer     cancel_data;
    RhImportExecProgressFn progress; /* 每处理完一个条目调用 */
    gpointer     user_data;
} RhImportExecOptions;

/*
 * 逐条执行计划。每个条目：
 *   1. 复制到目标目录的临时名，成功后原子改名为最终名；
 *   2. 单个事务内写 games/roms/media（含 patch）与 job_items 状态；
 *   3. 任一步失败：回滚事务、删除本次复制的文件与空目录，记为 failed，
 *      继续处理后续条目（不整体中止）。
 *
 * 取消只发生在条目边界：已提交的条目保留，当前条目回滚，返回
 * RH_ERR_CANCELLED 且 stats.cancelled 为 TRUE。
 */
RhErr rh_import_execute(RhStore *store, const RhPlatformRules *rules,
                        const RhImportPlan *plan,
                        const RhImportExecOptions *options,
                        RhImportExecStats *out_stats);

#endif /* RH_IMPORT_H */
