/*
 * store.h —— 唯一持有 SQL 的模块（ARCHITECTURE.md §4、§5.1）
 */
#ifndef RH_STORE_H
#define RH_STORE_H

#include <glib.h>

#include "rh-err.h"
#include "rh-records.h"

typedef struct RhStore RhStore;

/* 检索条件：字段为 NULL 表示不限（§3、§4） */
typedef struct {
    const char *platform;   /* 规范平台 ID */
    const char *search;     /* 子串匹配 name / game_main_name（大小写不敏感） */
    const char *tag;        /* 标签名 */
    const char *rom_status; /* good/baddump/unknown */
    const char *order_by;   /* "name"（默认）/"year"/"rating"/"updated" */
    gboolean descending;
    int limit;              /* <=0 时取 200 */
    int offset;
} RhGameFilter;

/*
 * 打开（必要时创建）库文件。
 * path 必须是 UTF-8（Windows 下由 rh_path_utf8_from_native 转换 argv 得到），
 * 因为 sqlite3_open_v2 的路径参数要求 UTF-8。
 * - schema 为空：建 v1 并写入 user_version；
 * - user_version 低于程序：按迁移表升级；
 * - user_version 高于程序：**只读**打开（store_is_read_only() 为 TRUE），
 *   前端据此拒绝写入并提示升级程序（§3.4、§9.4）。
 */
RhStore *store_open(const char *path, RhErr *out_err);
void store_close(RhStore *store);

int store_schema_version(const RhStore *store);
gboolean store_is_read_only(const RhStore *store);

/* ---------------------------------------------------------------------------
 * 事务：一条记录一个事务（§3.5）。已处于事务中时不会再嵌套开启。
 * ------------------------------------------------------------------------- */
RhErr store_begin(RhStore *store);
RhErr store_commit(RhStore *store);
RhErr store_rollback(RhStore *store);

/* ---------------------------------------------------------------------------
 * 游戏记录。store_game_insert 会同时写 roms / media / tags，
 * 并保证 same transaction；失败时整条记录不落库。
 * ------------------------------------------------------------------------- */
RhErr store_game_insert(RhStore *store, const RhGame *game,
                        const GPtrArray *roms,   /* RhRomFile*，可为 NULL */
                        const GPtrArray *media,  /* RhMediaFile*，可为 NULL */
                        const GPtrArray *tags);  /* gchar*，可为 NULL */

RhErr store_game_update(RhStore *store, const RhGame *game);
RhErr store_game_delete(RhStore *store, const char *id);

RhGame    *store_game_get(RhStore *store, const char *id, RhErr *out_err);
GPtrArray *store_roms_for_game(RhStore *store, const char *id, RhErr *out_err);
GPtrArray *store_media_for_game(RhStore *store, const char *id, const char *kind,
                                RhErr *out_err); /* kind 为 NULL 时返回全部 */
GPtrArray *store_tags_for_game(RhStore *store, const char *id, RhErr *out_err);

GPtrArray *store_games_list(RhStore *store, const RhGameFilter *filter, RhErr *out_err);
gint64     store_games_count(RhStore *store, const RhGameFilter *filter, RhErr *out_err);

/* ---------------------------------------------------------------------------
 * 作业（导入/导出进度持久化，§5 job 模块）
 * ------------------------------------------------------------------------- */
RhErr store_job_create(RhStore *store, const char *kind, int total, gint64 *out_id);
RhErr store_job_progress(RhStore *store, gint64 job_id, int done);
RhErr store_job_finish(RhStore *store, gint64 job_id, const char *error);

#endif /* RH_STORE_H */
