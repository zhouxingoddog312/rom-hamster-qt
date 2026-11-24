/*
 * store.h —— 唯一持有 SQL 的模块（ARCHITECTURE.md §4、§5.1）
 */
#ifndef RH_STORE_H
#define RH_STORE_H

#include <glib.h>

#include "rh-err.h"
#include "rh-records.h"

typedef struct RhStore RhStore;

/*
 * 检索条件：字符串为 NULL 表示不限（§3、§4）。
 *
 * 13 个筛选维度：platform / genre / developer / publisher / region /
 * patch_type / year_from / year_to / multi / has_patch / rom_status /
 * rom_format / tag（外加 search 关键字，走 LIKE；FTS5 不可用时行为一致）。
 */
typedef struct {
    const char *platform;   /* 规范平台 ID */
    const char *search;     /* 子串匹配 name / game_main_name（大小写不敏感） */
    const char *tag;        /* 单个标签名 */
    const char **tags;      /* 多个标签名，n_tags > 0 时优先于 tag */
    int         n_tags;
    const char *rom_status; /* good/baddump/unknown */
    const char *order_by;   /* "name"（默认）/"year"/"rating"/"updated"/"discs" */
    gboolean descending;
    int limit;              /* <=0 时取 200；-1 表示不限（导出用） */
    int offset;

    /* M3 新增维度 */
    const char *genre;
    const char *developer;
    const char *publisher;
    const char *region;
    const char *patch_type;
    int year_from;          /* 0 = 不限 */
    int year_to;            /* 0 = 不限 */
    int multi;              /* 0 不限 / 1 是 / 2 否（零初始化即不限） */
    int has_patch;          /* 0 不限 / 1 是 / 2 否 */
    const char *rom_format; /* raw/zip/7z/chd/cso/wbfs */
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
/* 追加媒体并更新游戏元数据；两件事在同一事务中完成。 */
RhErr store_game_append_media(RhStore *store, const RhGame *game,
                              const GPtrArray *media);
RhErr store_game_delete(RhStore *store, const char *id);
gboolean store_game_id_exists(RhStore *store, const char *id, RhErr *out_err);

RhGame    *store_game_get(RhStore *store, const char *id, RhErr *out_err);
GPtrArray *store_roms_for_game(RhStore *store, const char *id, RhErr *out_err);
/* 只改一个 ROM 的碟号（manage 重排碟片用）；disc_no <= 0 写 NULL。 */
RhErr store_rom_set_disc(RhStore *store, const char *game_id,
                         const char *filename, int disc_no);
GPtrArray *store_media_for_game(RhStore *store, const char *id, const char *kind,
                                RhErr *out_err); /* kind 为 NULL 时返回全部 */
GPtrArray *store_tags_for_game(RhStore *store, const char *id, RhErr *out_err);

GPtrArray *store_games_list(RhStore *store, const RhGameFilter *filter, RhErr *out_err);
gint64     store_games_count(RhStore *store, const RhGameFilter *filter, RhErr *out_err);

/* ---------------------------------------------------------------------------
 * 重复文件识别（§5.1：SQL 留在 store，文件相关的判断留给 dedup）
 * ------------------------------------------------------------------------- */

/* 返回 GPtrArray<RhDupGroup*>，调用方 g_ptr_array_unref。
 * platform 为 NULL 表示不限；只返回成员数 > 1 的簇。 */
GPtrArray *store_roms_dup_groups(RhStore *store, const char *platform,
                                 RhErr *out_err);
GPtrArray *store_media_dup_groups(RhStore *store, const char *platform,
                                  RhErr *out_err);

/* 待补算摘要的条目，返回 GPtrArray<RhDupMember*>。 */
GPtrArray *store_roms_missing_hash(RhStore *store, const char *platform,
                                   RhErr *out_err);
GPtrArray *store_media_missing_hash(RhStore *store, const char *platform,
                                    RhErr *out_err);

/* 导入查重候选：同平台、sha1 或 canon_sha1 命中的 ROM。
 * 返回 GPtrArray<RhDupMember*>。 */
GPtrArray *store_roms_by_hash(RhStore *store, const char *platform,
                              const char *hash, RhErr *out_err);

/* 回写摘要。crc32/canon_sha1 可为 NULL（沿用旧值）；size < 0 表示不改 size。
 * 目标行不存在返回 RH_ERR_NOT_FOUND。 */
RhErr store_rom_set_hashes(RhStore *store, const char *game_id,
                           const char *filename, gint64 size,
                           const char *crc32, const char *sha1,
                           const char *canon_sha1);
RhErr store_media_set_hash(RhStore *store, const char *game_id,
                           const char *kind, const char *filename, gint64 size,
                           const char *crc32, const char *sha1);

/* ---------------------------------------------------------------------------
 * 库统计（只读，SQL 留在 store.c；manage/stats 命令使用）
 * ------------------------------------------------------------------------- */
typedef struct {
    gint64 games;
    gint64 roms;
    gint64 media;
    gint64 rom_bytes;
    gint64 media_bytes;
    gint64 multi_disc;   /* is_multi_disc = 1 的游戏数 */
    gint64 arcade;
    gint64 with_patch;   /* patch_type != '' */
    gint64 dat_matched;  /* dat_source 非空的游戏数 */
    gint64 missing_rom;  /* 没有任何 ROM 记录的游戏数 */
} RhLibStats;

typedef struct {
    gchar *platform;
    gint64 games;
    gint64 roms;
    gint64 rom_bytes;
} RhPlatformStats;

void rh_platform_stats_free(RhPlatformStats *stats);

RhErr      store_stats(RhStore *store, RhLibStats *out, RhErr *out_err);
GPtrArray *store_stats_by_platform(RhStore *store, RhErr *out_err); /* RhPlatformStats* */

/* ---------------------------------------------------------------------------
 * 作业（导入/导出进度持久化，§5 job 模块）
 * ------------------------------------------------------------------------- */
RhErr store_job_create(RhStore *store, const char *kind, int total, gint64 *out_id);
RhErr store_job_progress(RhStore *store, gint64 job_id, int done);
RhErr store_job_finish(RhStore *store, gint64 job_id, const char *error);
RhErr store_job_set_state(RhStore *store, gint64 job_id, const char *state);

typedef struct {
    gint64 id;
    gchar *kind;
    gchar *state;
    gint   total;
    gint   done;
    gchar *created_at;
} RhJobInfo;

void rh_job_info_free(RhJobInfo *info);
/* state 不是 done/failed 的作业，按 id 升序；调用方 g_ptr_array_unref。 */
GPtrArray *store_job_list_unfinished(RhStore *store, RhErr *out_err);
/* 写入/更新一个作业条目；导入执行阶段把状态与游戏记录放在同一事务里。 */
RhErr store_job_item_set(RhStore *store, gint64 job_id, int seq,
                         const char *src, const char *action,
                         const char *platform, const char *target_id,
                         const char *state, const char *message);
/* 按来源标签查最近一条条目状态；未找到 *out_state = NULL。 */
RhErr store_job_item_state(RhStore *store, gint64 job_id, const char *src,
                           const char *platform, gchar **out_state);

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

/* 一条已导入 DAT 的元数据（dat list 使用）。字符串均由本结构持有。 */
typedef struct {
    gint64 id;
    gchar *platform;
    gchar *path;
    gchar *name;
    gchar *version;
    gchar *encoding;
    gchar *imported_at;
    gint64 entry_count;
    gint64 skipped;
    gchar *file_sha1;
} RhDatInfo;

void rh_dat_info_free(RhDatInfo *info);

/* 按 platform、name、path 升序列出全部已导入 DAT。
 * 返回 GPtrArray<RhDatInfo*>（调用方 g_ptr_array_unref）；
 * 空库返回空数组且 *out_err = RH_OK。 */
GPtrArray *store_dat_list(RhStore *store, RhErr *out_err);

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
