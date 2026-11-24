/*
 * rh-hash.h —— 单次读盘同时计算 CRC32 / MD5 / SHA1（ARCHITECTURE.md §5）
 *
 * 边界：只读一个普通文件，不知道它是不是 ROM，不写库、不打印。
 * 路径入参是 UTF-8；打开文件走 GLib（Windows 上转宽字符），不经过 ANSI fopen。
 *
 * 设计要点：
 *   - 流式分块读取，块大小可配置，内存占用与文件大小无关；
 *   - 同一份数据同时喂给三种算法，批量入口也不会让同一文件读多遍；
 *   - 进度回调返回 FALSE 即取消，返回 RH_ERR_CANCELLED，供 GUI 长任务中断。
 */
#ifndef RH_HASH_H
#define RH_HASH_H

#include <glib.h>

#include "rh-err.h"

typedef enum {
    RH_HASH_CRC32 = 1u << 0,
    RH_HASH_MD5   = 1u << 1,
    RH_HASH_SHA1  = 1u << 2,
    RH_HASH_ALL   = RH_HASH_CRC32 | RH_HASH_MD5 | RH_HASH_SHA1
} RhHashFlags;

typedef struct {
    guint64  size;       /* 实际读到的字节数 */
    gboolean has_crc32;
    guint32  crc32;      /* zlib 的 IEEE CRC32 */
    gboolean has_md5;
    gchar    md5[33];    /* 小写十六进制，未请求时为空串 */
    gboolean has_sha1;
    gchar    sha1[41];   /* 小写十六进制，未请求时为空串 */
    RhErr    error;      /* 批量时表示该项结果；RH_OK 表示成功 */
} RhHashResult;

/*
 * 每读完一块调用一次。processed 为累计字节（含跳过部分之外的实际读取量），
 * total 为 stat 得到的大小减去 skip_bytes（未知时为 0）。返回 FALSE 取消本次
 * 计算；空文件不会调用。
 * path 是调用方传入的 UTF-8 路径，便于批量任务定位当前文件。
 */
typedef gboolean (*RhHashProgressFn)(const char *path, guint64 processed,
                                     guint64 total, gpointer user_data);

typedef struct {
    RhHashFlags      flags;       /* 0 视为非法；options 为 NULL 时默认 ALL */
    guint            chunk_size;  /* 0 = 默认 256 KiB */
    guint64          skip_bytes;  /* 跳过文件头字节数；0 = 整个文件 */
    RhHashProgressFn progress;    /* 可为 NULL */
    gpointer         user_data;
} RhHashOptions;

/* 清零结果（含 error）。批量入口在开始前会对每一项调用。 */
void rh_hash_result_clear(RhHashResult *result);

/*
 * 单文件：一次顺序读盘算出已请求的摘要。
 * skip_bytes > 0 时从该偏移开始读取；skip_bytes >= 文件大小视为非法。
 * 失败返回对应错误码，out 被清零（error 同步置为该码）。
 */
RhErr rh_hash_file(const char *path, const RhHashOptions *options,
                   RhHashResult *out);

/*
 * 批量：逐项调用上面的单文件逻辑，每个文件只读一遍。
 *   - 全部成功返回 RH_OK；
 *   - 回调取消立即返回 RH_ERR_CANCELLED，未处理项保持清零；
 *   - 其它错误会继续处理后续文件，返回值是第一个错误，具体失败看
 *     out_results[i].error（调用方据此区分部分成功）。
 * out_results 至少能容纳 count 项。
 */
RhErr rh_hash_files(const char *const *paths, guint count,
                    const RhHashOptions *options, RhHashResult *out_results);

#endif /* RH_HASH_H */
