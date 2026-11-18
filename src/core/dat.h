/*
 * dat.h —— Logiqx DAT 导入与哈希匹配（ARCHITECTURE.md §3.3、§5.1）
 *
 * 边界：只解析 DAT、建 dat_entries、按哈希查；不改游戏记录、不打印、不依赖 GUI。
 *
 * 设计要点：
 *   - 流式解析：GMarkup 逐块喂入，内存占用与 DAT 大小无关；全量 DAT
 *     （No-Intro / Redump）不需要整棵树入内存。
 *   - 可取消：进度回调按块调用，返回 FALSE 立即中止；导入是单事务，
 *     取消/失败不留半条 DAT。
 *   - 编码不硬编码 UTF-8：优先 XML 声明；无声明先按 UTF-8 严格解码，
 *     遇到非法字节则整份重来一次按 GB18030 解码；GB18030 仍解不开的
 *     字节按 %NN 转义保留（不静默丢弃）。
 *   - 名字不作唯一键：rom 名只在 game 内唯一，MAME 的 parent/clone
 *     结构照原样保留（cloneof/romof/merge），匹配只认校验值。
 *   - 匹配：crc32 → sha1 → md5，返回全部候选；同哈希多候选即二义，
 *     由调用方决策，绝不替调用方挑一个。
 */
#ifndef RH_DAT_H
#define RH_DAT_H

#include <glib.h>

#include "rh-err.h"
#include "rh-hash.h"
#include "rh-records.h"
#include "store.h"

typedef enum {
    RH_DAT_MATCH_NONE = 0,   /* 未命中 */
    RH_DAT_MATCH_UNIQUE,     /* 恰好 1 条候选 */
    RH_DAT_MATCH_AMBIGUOUS   /* >= 2 条候选，需调用方选择 */
} RhDatMatchKind;

typedef struct {
    gchar       encoding[32];     /* 实际采用：utf-8 / gb18030 / 声明的字符集名 */
    gboolean    encoding_guessed; /* 无声明且非 UTF-8，按 GB18030 推测 */
    gboolean    encoding_declared;/* XML 声明指定了字符集 */
    gboolean    escaped;          /* 有字节无法解码，已按 %NN 转义保留 */
    gboolean    xml_error;        /* 结构错误：已入库的是出错前解析到的部分 */
    gboolean    cancelled;
    gboolean    unchanged;        /* 内容哈希与已导入的一致，未重建索引 */
    gint64      games;            /* 解析到的 game/machine/resource 数 */
    gint64      entry_count;      /* 实际入库的 dat_entries 行数（rom + disk） */
    gint64      disk_entries;     /* 其中 entry_type = "disk" 的条目数（Clrmamepro disk 元素） */
    gint64      skipped;          /* 缺 rom 名/不在 game 内/非法位置而被跳过的 rom 条目 */
    gint64      sample_skipped;   /* 跳过的 sample 元素数（音频采样，非 ROM 匹配对象） */
    gint64      bad_fields;       /* 非空但非法的 size/哈希值个数 */
} RhDatImportStats;

/*
 * 每读完一块调用一次。entries 为已解析条目数，bytes_done/bytes_total
 * 为已读/总字节（total 未知时为 0）。返回 FALSE 取消导入。
 */
typedef gboolean (*RhDatProgressFn)(const gchar *path, gint64 entries,
                                    guint64 bytes_done, guint64 bytes_total,
                                    gpointer user_data);

typedef struct {
    const gchar     *platform;    /* 必填：写入 dats.platform */
    const gchar     *dat_name;    /* NULL：取 <header><name>，再退回文件名 */
    const gchar     *dat_version; /* NULL：取 <header><version> */
    guint            batch_size;  /* 每批落库条目数，0 = 2000 */
    guint            chunk_size;  /* 读取块大小，0 = 64 KiB（测试可调小） */
    gboolean         force;       /* TRUE：即使内容哈希未变也重建索引 */
    RhDatProgressFn  progress;    /* 可为 NULL */
    gpointer         user_data;
} RhDatImportOptions;

void rh_dat_import_stats_clear(RhDatImportStats *stats);

/*
 * 导入一份 Logiqx DAT（.dat / .xml）。
 *
 * path 是 UTF-8；打开文件走 g_fopen，Windows 上会转宽字符。
 * 成功返回 RH_OK；取消返回 RH_ERR_CANCELLED（库中不留该 DAT 的任何行）；
 * 结构错误不整体失败：能解析的部分照常入库，out_stats->xml_error 为 TRUE；
 * 一条都没解析出来时返回 RH_ERR_INVALID。
 *
 * 同一 (platform, path) 重复导入是替换语义，dats.id 保持不变，
 * 已写入的 roms.dat_id 仍然有效。文件内容哈希在解析时顺带计算；
 * 若与库中记录一致且未指定 force，则直接返回（stats.unchanged = TRUE），
 * 不解析、不写库。换了解析逻辑需要重扫时用 force。
 *
 * 本函数自己管理事务，调用前 store 不能已处于事务中（返回 RH_ERR_INVALID）。
 */
RhErr rh_dat_import(RhStore *store, const char *path,
                    const RhDatImportOptions *options,
                    RhDatImportStats *out_stats);

/*
 * 按校验值匹配。hash 为 NULL 或三种值都没有时返回 NONE。
 *
 * 查询顺序 sha1 → md5 → crc32（抗碰撞强到弱），每种算法都先按 size 缩小
 * 范围；DAT 条目没有 size（<disk>、未给 size 的 rom）时不受 size 限制。
 * 前一种算法有命中就不再往下走。
 *
 * out_matches 返回 GPtrArray<RhDatMatch*>（调用方 g_ptr_array_unref）；
 * platform 非 NULL 时只在该平台的 DAT 内查。
 */
RhDatMatchKind rh_dat_match(RhStore *store, const RhHashResult *hash,
                            const char *platform,
                            GPtrArray **out_matches, RhErr *out_err);

#endif /* RH_DAT_H */
