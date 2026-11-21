/*
 * export.h —— 前端导出（ARCHITECTURE.md §7.2）
 *
 * 一份前端规格同时描述导入与导出；本模块只实现导出侧：
 *   - 解析 .conf（[frontend] / [export.header|game|footer] / [export.media]）
 *   - 编译闭集模板（变量 / 转义 / %if% / %for%），加载期校验变量并带行号报错
 *   - 遍历游戏、复制 ROM/媒体、按平台生成清单
 *
 * 模板只排版；相对路径、媒体复制、m3u 等逻辑都在代码里。
 */
#ifndef RH_EXPORT_H
#define RH_EXPORT_H

#include <glib.h>

#include "platform.h"
#include "rh-err.h"
#include "store.h"

typedef struct RhExportSpec RhExportSpec;

/*
 * 加载规格。失败返回 NULL：
 *   out_err = RH_ERR_IO 读不到文件，RH_ERR_INVALID 语法/模板错误；
 *   out_message 为 "文件:行号: 说明"（可为 NULL）。
 */
RhExportSpec *rh_export_spec_load(const char *path, RhErr *out_err,
                                  gchar **out_message);
void          rh_export_spec_free(RhExportSpec *spec);

const char *rh_export_spec_id(const RhExportSpec *spec);
const char *rh_export_spec_name(const RhExportSpec *spec);

/* 导出清单与复制文件时使用的 ROM 文件名。 */
typedef enum {
    RH_EXPORT_NAMES_ORIGINAL = 0, /* 库内文件名（默认） */
    RH_EXPORT_NAMES_DAT = 1,      /* DAT 标准名（通常为英文） */
    RH_EXPORT_NAMES_DISPLAY = 2   /* 游戏显示名（通常为中文）+ 原后缀 */
} RhExportNameMode;

typedef struct {
    const char *library_root;  /* 源库根；copy_roms/copy_media 时需要 */
    const char *output_dir;    /* 目标根目录 */
    gboolean    copy_roms;     /* 按 layout_rom 复制 ROM */
    gboolean    copy_media;    /* 按 [export.media] 复制媒体 */
    gboolean    compress_roms; /* 预留：M3 不压缩，字段保留 */
    gboolean    generate_m3u;  /* 多碟游戏生成 m3u 并把它作为 rom_file */
    RhExportNameMode name_mode; /* ROM 文件名策略 */
} RhExportOptions;

typedef struct {
    guint games;
    guint roms;
    guint media;
    guint errors;
} RhExportStats;

typedef void (*RhExportProgressFn)(guint done, guint total,
                                   const char *name, gpointer user_data);

/*
 * 执行导出。rules 可为 NULL（仅影响 platform_extensions 变量）。
 * 单个游戏失败不中止整批，计入 stats->errors 并继续。
 */
RhErr rh_export_run(RhStore *store, const RhPlatformRules *rules,
                    const char *spec_path, const RhExportOptions *options,
                    RhExportProgressFn progress, gpointer user_data,
                    RhExportStats *out_stats);

#endif /* RH_EXPORT_H */
