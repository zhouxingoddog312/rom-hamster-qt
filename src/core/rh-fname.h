/*
 * rh-fname.h —— 文件名原始字节与可逆显示（ARCHITECTURE.md §15）
 *
 * 两种来源都会给出非 UTF-8 字节：
 *   1. 压缩包条目名：老 Windows 工具打的 zip，条目名是 GBK 字节且无 UTF-8 标记；
 *   2. 文件系统文件名：NTFS-3G / Samba / 老 zip 解出的文件，POSIX 上是字节串。
 *
 * 本模块只做「字节 → 状态 + 显示名」的判定，以及显示名与原始字节之间的
 * 可逆转换。它不改名、不碰磁盘、不猜平台。
 *
 * 判定顺序：
 *   合法 UTF-8        -> RH_FNAME_UTF8（无需猜测）
 *   否则整串 GB18030  -> RH_FNAME_GB18030（成功但属于推测）
 *   否则逐字节转义    -> RH_FNAME_ESCAPED（失败兜底，仍无损）
 *
 * 原始字节始终保留在 RhFName.raw，数据库存 BLOB，显示名只用于展示与索引。
 */
#ifndef RH_FNAME_H
#define RH_FNAME_H

#include <glib.h>

typedef enum {
    RH_FNAME_UTF8 = 0,   /* 原始字节即合法 UTF-8 */
    RH_FNAME_GB18030,    /* 非 UTF-8，整串按 GB18030 解码（推测） */
    RH_FNAME_ESCAPED     /* 非 UTF-8 且 GB18030 解不了，逐字节转义（兜底） */
} RhFNameEncoding;

typedef struct {
    gchar          *display;  /* UTF-8，可直接显示/入库 */
    GBytes         *raw;      /* 原始字节，始终保留，用于还原真实路径 */
    RhFNameEncoding encoding;
} RhFName;

/* bytes 可为任意字节；len < 0 时按 NUL 结尾。bytes 为 NULL 返回 NULL。 */
RhFName *rh_fname_from_bytes(const gchar *bytes, gssize len);
RhFName *rh_fname_from_utf8(const gchar *utf8);
void rh_fname_free(RhFName *name);

/* 推测成功（GB18030）为 TRUE；合法 UTF-8 与转义兜底均为 FALSE。 */
gboolean rh_fname_is_guessed(const RhFName *name);

/* 是否能在当前平台定位到真实文件：
 * POSIX 永远可以（直接用原始字节）；Windows 上转义状态不可定位。 */
gboolean rh_fname_is_locatable(const RhFName *name);

/*
 * 交给 GLib / 文件系统调用的名字：
 *   POSIX   原始字节（NUL 结尾，保持文件系统里的真实名字）
 *   Windows UTF-8 显示名；转义状态返回 NULL（无法用展示名安全落到磁盘）
 * 返回新分配字符串。
 */
gchar *rh_fname_native_name(const RhFName *name);

/* 原始字节的 NUL 结尾副本；out_len 可为 NULL。 */
gchar *rh_fname_raw_string(const RhFName *name, gsize *out_len);

/* 用 '/' 连接两段原始字节路径；parent 为空时直接复制 child。 */
GBytes *rh_fname_join_raw(GBytes *parent, GBytes *child);

/*
 * 兜底转义：0x20–0x7E 中除 '%' 与 '\' 外原样；'%' -> "%25"，'\' -> "%5C"，
 * 其余字节 -> "%NN"（大写十六进制）。结果无空格、无路径分隔符，是合法 UTF-8。
 * 之所以不用 "\xNN"：反斜杠在 Windows 上是路径分隔符，转义名一旦参与
 * 解压/建目录就会意外切出子目录。'%' 在双端都是普通文件名字符。
 */
gchar *rh_fname_escape(const gchar *bytes, gssize len);

/* 转义的逆运算；格式非法时返回 NULL。成功时 out_len 为解出的字节数。 */
gchar *rh_fname_unescape(const gchar *escaped, gsize *out_len);

#endif /* RH_FNAME_H */
