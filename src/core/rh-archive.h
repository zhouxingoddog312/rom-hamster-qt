/*
 * rh-archive.h —— 压缩包列表 / 解压 / 加密探测（ARCHITECTURE.md §5）
 *
 * 边界：只知道压缩包，不知道"ROM 是什么"，不判定平台、不写库、不打印。
 * 入参与返回的条目名一律是 UTF-8，返回的条目名分隔符统一为 '/'。
 *
 * 文件名带 rh- 前缀是为了避开 libarchive 的 <archive.h>：src/core 在
 * 编译器的 -I 路径里，若本文件叫 archive.h，`#include <archive.h>` 会
 * 优先命中自己而不是 libarchive。
 */
#ifndef RH_ARCHIVE_H
#define RH_ARCHIVE_H

#include <glib.h>

#include "rh-err.h"

typedef struct {
    gchar   *pathname;      /* UTF-8，'/' 分隔，目录条目不带结尾 '/' */
    guint64  size;          /* 非负；目录为 0 */
    gboolean is_dir;
    gboolean is_encrypted;
} RhArchiveEntry;

typedef struct {
    GPtrArray *entries;     /* RhArchiveEntry* */
    gboolean   encrypted;   /* 任一条目加密，或格式声明整包加密 */
} RhArchiveList;

typedef struct {
    guint files;            /* 解出的普通文件数 */
    guint dirs;             /* 创建/确认的目录数 */
    guint skipped;          /* 符号链接、设备节点、空路径等未解出的条目 */
} RhArchiveExtractStats;

typedef struct {
    const char *target_dir; /* UTF-8；不存在时创建 */
    const char *entry;      /* NULL = 解压全部；否则只解压该条目（'/' 分隔） */
} RhArchiveExtractOptions;

/* 列目录索引，不解压。返回 RH_ERR_UNSUPPORTED 表示格式无法识别；
 * 条目路径越界（绝对路径、".."）返回 RH_ERR_INVALID。 */
RhErr rh_archive_list(const char *archive_path, RhArchiveList **out_list);
void rh_archive_list_free(RhArchiveList *list);

/* 只读探测是否加密（zip/7z/rar 等由 libarchive 声明）。 */
RhErr rh_archive_is_encrypted(const char *archive_path, gboolean *out_encrypted);

/* 解压全部或单条目。没有密码输入通道：遇到加密条目返回 RH_ERR_ENCRYPTED，
 * 已写出的文件保留在目标目录（调用方应在临时目录解压）。
 * 单条目未找到返回 RH_ERR_NOT_FOUND。 */
RhErr rh_archive_extract(const char *archive_path,
                         const RhArchiveExtractOptions *options,
                         RhArchiveExtractStats *out_stats);

#endif /* RH_ARCHIVE_H */
