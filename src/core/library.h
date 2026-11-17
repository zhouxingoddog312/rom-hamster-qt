/*
 * library.h —— 整库识别（ARCHITECTURE.md §5 library、§12.2）
 *
 * 读取已有收藏目录里的前端清单：
 *   - gamelist.xml（EmulationStation / ES-DE）
 *   - metadata.pegasus.txt（Pegasus）
 * 只做读取，不写入、不搬移文件；返回的条目供导入预扫描使用。
 */
#ifndef RH_LIBRARY_H
#define RH_LIBRARY_H

#include <glib.h>

#include "rh-err.h"

typedef struct {
    gchar  *name;
    gchar  *path;          /* 清单里的 ROM 路径，已去掉 "./" 前缀 */
    gchar  *description;
    gchar  *developer;
    gchar  *publisher;
    gchar  *genre;
    gchar  *region;
    gint    year;          /* 0 表示未知 */
    gint    players;
    gdouble rating;
    gchar  *image;         /* 相对路径，可能为 NULL */
    gchar  *video;
    gchar  *marquee;
    gchar  *platform_hint; /* 清单所在目录名，用于反查平台 */
    gchar  *source;        /* "gamelist" 或 "pegasus" */
} RhLibraryEntry;

void rh_library_entry_free(RhLibraryEntry *entry);
void rh_library_entry_free_func(gpointer data);

/*
 * 扫描 root 下一级子目录里的清单文件。没有清单的目录被跳过。
 * 返回 RhLibraryEntry* 数组（带 free func），永不返回 NULL。
 * 某个清单解析失败时跳过该文件并记一条 warning 日志（不中断整次扫描）。
 */
GPtrArray *rh_library_scan(const char *root, RhErr *out_err);

#endif /* RH_LIBRARY_H */
