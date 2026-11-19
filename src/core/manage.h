/*
 * manage.h —— 编辑 / 删除 / 碟片 / 补丁 / 统计 / 完整性（ARCHITECTURE.md §5、§5.1）
 *
 * 不做检索展示：查询由 query/detail 负责。
 */
#ifndef RH_MANAGE_H
#define RH_MANAGE_H

#include <glib.h>

#include "rh-err.h"
#include "rh-records.h"
#include "store.h"

/* 更新一条游戏记录（字段由调用方先 get 再改）。id 不存在返回 RH_ERR_NOT_FOUND。 */
RhErr rh_manage_update(RhStore *store, const RhGame *game);

/*
 * 删除一条游戏记录。
 * library_root 非 NULL 且 delete_files 为 TRUE 时，同时删除
 * <root>/<dir_path> 整个目录；目录必须位于 root 之内（拒绝 ".." 与绝对路径）。
 */
RhErr rh_manage_delete(RhStore *store, const char *id,
                       const char *library_root, gboolean delete_files);

/*
 * 按现有 disc_no 排序后重排为 1..n（disc_no=0 的不参与），
 * 两个以上不同碟号则把 is_multi_disc 置 1。
 */
RhErr rh_manage_renumber_discs(RhStore *store, const char *id);

/* patch_type 只接受 ""/generic/platform/mixed。 */
RhErr rh_manage_set_patch_type(RhStore *store, const char *id, const char *patch_type);

/* 统计（委托 store，保持 SQL 唯一入口）。 */
RhErr      rh_manage_stats(RhStore *store, RhLibStats *out, RhErr *out_err);
GPtrArray *rh_manage_stats_by_platform(RhStore *store, RhErr *out_err);

/* ---------------------------------------------------------------------------
 * 完整性检查：只读探测库根目录下的文件是否存在、大小是否与记录一致。
 * 非 UTF-8 文件名按显示名检查（原始字节定位留给导入/导出路径）。
 * ------------------------------------------------------------------------- */
typedef enum {
    RH_INTEGRITY_MISSING_DIR = 1,
    RH_INTEGRITY_MISSING_ROM = 2,
    RH_INTEGRITY_SIZE_MISMATCH = 3
} RhIntegrityKind;

typedef struct {
    gchar            *game_id;
    gchar            *path;      /* 相对库根目录 */
    RhIntegrityKind   kind;
    gint64            expected;
    gint64            actual;
} RhIntegrityIssue;

typedef struct {
    GPtrArray *issues;       /* RhIntegrityIssue* */
    gint64     games_checked;
    gint64     roms_checked;
} RhIntegrityReport;

RhIntegrityReport *rh_integrity_report_new(void);
void               rh_integrity_report_free(RhIntegrityReport *report);
void               rh_integrity_issue_free(RhIntegrityIssue *issue);

RhErr rh_manage_integrity(RhStore *store, const char *library_root,
                          RhIntegrityReport *out, RhErr *out_err);

#endif /* RH_MANAGE_H */
