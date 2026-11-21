/*
 * dat-source.h —— DAT 源清单（data/dats/sources.conf）
 *
 * 边界：只解析清单、给出默认查找路径；不下载、不访问数据库、不打印。
 *
 * 清单格式（spec_version = 1，INI 风格、UTF-8、# 开头为注释）：
 *
 *   [meta]
 *   spec_version=1
 *
 *   [nes]
 *   name=Nintendo Entertainment System
 *   file=Nintendo - Nintendo Entertainment System.dat
 *   url=https://raw.githubusercontent.com/.../Nintendo%20-%20...dat
 *   url=https://ghfast.top/https://raw.githubusercontent.com/...dat
 *   sha1=<可选，40 位十六进制>
 *
 * 除 [meta] 外，段名即平台 ID，与 data/platforms.conf 的规范 ID 一致。
 * 未知键、未知段内容一律忽略；spec_version > 1 拒绝加载，提示升级程序。
 */
#ifndef RH_DAT_SOURCE_H
#define RH_DAT_SOURCE_H

#include <glib.h>

#include "rh-err.h"

typedef struct {
    gchar     *platform;  /* 段名，已规范为小写平台 ID */
    gchar     *name;      /* 展示名；缺省由 file 去掉 .dat 后缀得到 */
    gchar     *file;      /* 本地文件名，不含路径分隔符 */
    GPtrArray *urls;      /* gchar*，按书写顺序作为镜像优先级 */
    gchar     *sha1;      /* 可选，小写 40 位十六进制；NULL 表示未提供 */
} RhDatSource;

void rh_dat_source_free(RhDatSource *source);

/*
 * 解析清单文件。
 *
 * 成功返回 RH_OK，*out_sources 为 GPtrArray<RhDatSource*>（带释放函数，
 * 调用方 g_ptr_array_unref）。语法/校验错误返回 RH_ERR_INVALID，读取失败
 * 返回 RH_ERR_IO；*message 由本函数分配，形如 "路径:行号: 描述"，调用方
 * 用 g_free 释放。path 为 UTF-8。
 */
RhErr rh_dat_sources_load(const char *path, GPtrArray **out_sources,
                          gchar **message);

/*
 * 默认清单路径（第一个存在的文件）：
 *   1. ROM_HAMSTER_DAT_SOURCES
 *   2. ~/.config/rom-hamster/dats/sources.conf
 *   3. <数据目录>/dats/sources.conf
 *
 * 返回新分配的 UTF-8 路径；都不存在返回 NULL。数据目录候选见 rh-path。
 */
gchar *rh_dat_sources_default_path(void);

#endif /* RH_DAT_SOURCE_H */
