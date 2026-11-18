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
gboolean store_in_transaction(const RhStore *store);

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
gboolean store_game_id_exists(RhStore *store, const char *id, RhErr *out_err);

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
/* 写入/更新一个作业条目；导入执行阶段把状态与游戏记录放在同一事务里。 */
RhErr store_job_item_set(RhStore *store, gint64 job_id, int seq,
                         const char *src, const char *action,
                         const char *platform, const char *target_id,
                         const char *state, const char *message);

/* ---------------------------------------------------------------------------
 * DAT：XML 解析归 dat.c，这里只落库与按哈希查（§5.1 边界表）。
 * store_dat_begin 对同一 (platform, path) 是"替换"语义：旧索引先清空，
 * dats.id 保持不变，已有 roms.dat_id 仍然有效。
 * store_dat_abort 删除整条 DAT，并把引用它的 roms.dat_id 置 NULL。
 * ------------------------------------------------------------------------- */
RhErr store_dat_begin(RhStore *store, const char *platform, const char *path,
                      const char *name, const char *version,
                      const char *encoding, gint64 *out_id);
RhErr store_dat_entries_add(RhStore *store, gint64 dat_id,
                            const GPtrArray *entries); /* RhDatEntry* */
RhErr store_dat_finish(RhStore *store, gint64 dat_id, const char *name,
                       const char *version, const char *file_sha1,
                       gint64 skipped, gint64 *out_count);
RhErr store_dat_abort(RhStore *store, gint64 dat_id);
RhErr store_dat_count(RhStore *store, gint64 *out);

/* 查 (platform, path) 已导入的 DAT 元数据，用于"内容未变则跳过重建"。
 * 不存在返回 RH_OK，三个出参保持 NULL/0。 */
RhErr store_dat_lookup(RhStore *store, const char *platform, const char *path,
                       gchar **out_file_sha1, gint64 *out_id,
                       gint64 *out_entry_count);

/* algo 只接受 crc32 / md5 / sha1；platform 为 NULL 表示不限。
 * size > 0 时额外要求 size 吻合（size 为 NULL 的条目不受限制）；
 * size = 0 不加条件。
 * 返回 GPtrArray<RhDatMatch*>，调用方用 g_ptr_array_unref 释放。 */
RhErr store_dat_find_by_hash(RhStore *store, const char *algo, const char *hash,
                             gint64 size, const char *platform, GPtrArray **out);

#endif /* RH_STORE_H */
