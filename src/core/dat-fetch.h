/*
 * dat-fetch.h —— 按清单下载/更新 DAT（data/dats/sources.conf）
 *
 * 边界：只下载单个 DAT 到指定目录并校验内容；不解析 XML、不写数据库、
 * 不打印，也不决定更新哪几个平台（由前端遍历清单）。
 *
 * 设计要点：
 *   - 镜像按清单顺序尝试，前一个失败才走下一个；
 *   - 每个镜像失败后自动重试（默认 1 次），重试沿用已下载的 .part；
 *   - 清单给了 sha1 时，残留的 .part 用 HTTP Range 续传；没有 sha1 时
 *     无法判断残留内容是否属于同一版本，一律从头下载；
 *   - 先写 <file>.part，校验通过后原子替换正式文件，失败不留半份文件；
 *   - 清单给了 sha1 就强制校验，不匹配视为该镜像失败；
 *   - 本地已有文件且内容一致时不覆盖（stats.changed = FALSE）；
 *   - check_only 只下到临时文件比较内容，绝不写正式文件；
 *   - libcurl 只在 RH_DAT_FETCH 打开时编译进来，关闭时返回 RH_ERR_UNSUPPORTED，
 *     CLI/GUI 仍可正常构建。
 */
#ifndef RH_DAT_FETCH_H
#define RH_DAT_FETCH_H

#include <glib.h>

#include "dat-source.h"
#include "rh-err.h"

typedef struct {
    gboolean check_only;   /* TRUE：只报告是否有更新，不落盘 */
    gboolean force;        /* TRUE：即使本地与清单 sha1 一致也重新下载比对 */
    guint    retries;      /* 每个镜像失败后的重试次数，0 = 默认 1 次 */
    guint    connect_timeout; /* 秒，0 = 20 */
    guint    low_speed_time;  /* 秒，0 = 30 */
    guint    low_speed_limit; /* 字节/秒，0 = 1024 */
    /* 每收到数据调用一次；返回 FALSE 中止当前下载。total 未知时为 0。 */
    gboolean (*progress)(const gchar *url, guint64 bytes_done,
                         guint64 bytes_total, gpointer user_data);
    gpointer user_data;
} RhDatFetchOptions;

typedef struct {
    gchar   *platform;    /* 清单段名 */
    gchar   *path;        /* 正式文件路径（不保证已存在） */
    gchar   *url;         /* 实际成功的镜像；失败时为 NULL */
    guint64  bytes;       /* 下载到的字节数；未联网时为本地文件大小 */
    guint64  resumed_from; /* 成功下载时从 .part 续传的起点；0 = 从头下载 */
    gchar    sha1[41];    /* 下载/本地内容的小写 sha1，失败时为空串 */
    gboolean changed;     /* 远端内容与本地不同（check_only 下即"有更新"） */
    gboolean installed;   /* 本次是否真的写了正式文件 */
    gint     mirrors_tried;
    guint    attempts;    /* 实际下载尝试次数（含重试，跨镜像累计） */
    gchar   *message;     /* 失败时的具体原因（可翻译由前端决定，当前为英文） */
} RhDatFetchResult;

void rh_dat_fetch_result_clear(RhDatFetchResult *result);

/* 当前构建是否带网络下载能力（RH_DAT_FETCH）。 */
gboolean rh_dat_fetch_supported(void);

/*
 * 下载/更新一份 DAT 到 dir（目录不存在时创建）。
 *
 * 成功返回 RH_OK，具体结果见 out（changed/installed）。
 * 全部镜像失败返回 RH_ERR_IO，取消返回 RH_ERR_CANCELLED，
 * 参数非法返回 RH_ERR_INVALID，未编译网络支持返回 RH_ERR_UNSUPPORTED。
 * out 在调用开始时被清零；复用同一结构前先 rh_dat_fetch_result_clear()。
 * 调用方用 rh_dat_fetch_result_clear() 释放结果中的字符串。
 */
RhErr rh_dat_fetch(const RhDatSource *source, const char *dir,
                   const RhDatFetchOptions *options, RhDatFetchResult *out);

#endif /* RH_DAT_FETCH_H */
