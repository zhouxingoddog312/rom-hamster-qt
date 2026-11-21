/*
 * rh-path.h —— 双端路径与环境变量的编码适配（ARCHITECTURE.md §2.1 L0、§15）
 *
 * Windows 的窄字符串 API（argv、getenv）给的是 ANSI/GBK 字节；GLib 与 SQLite
 * 要求 UTF-8。凡是把原生窄字符串交给 GLib/SQLite 的地方都要经过本模块。
 * POSIX 上这些函数退化为直接复制（本来就用 UTF-8）。
 */
#ifndef RH_PATH_H
#define RH_PATH_H

#include <glib.h>

#ifdef _WIN32
#include <wchar.h>
#endif

/* 合法 UTF-8？NULL 视为合法（便于直接校验可选参数）。 */
gboolean rh_path_is_utf8(const char *s);

/* 当前工作目录，UTF-8；失败返回 NULL。 */
char *rh_path_current_dir(void);

/*
 * 数据目录候选（platforms.conf、dats/sources.conf 所在目录），按优先级：
 *   1. 环境变量 ROM_HAMSTER_DATA_DIR
 *   2. <可执行文件目录>/../share/rom-hamster
 *   3. <可执行文件目录>/share/rom-hamster
 *   4. <可执行文件目录>/../../share/rom-hamster
 *   5. 编译期 RH_DATA_DIR（源码树 data/，开发构建兜底）
 *   6. /usr/share/rom-hamster（仅 POSIX）
 *
 * 只收集候选，不检查目录是否存在；返回新数组（g_free 释放元素），
 * 调用方用 g_ptr_array_free(dirs, TRUE) 释放。CLI 与 GUI 共用。
 */
GPtrArray *rh_path_data_dir_candidates(void);

/* 原生窄字符串 → UTF-8。Windows 走 CP_ACP，POSIX 直接复制。 */
char *rh_path_utf8_from_native(const char *native);

#ifdef _WIN32
/* UTF-8 ↔ 宽字符，供需要 *W API 的路径操作使用。 */
wchar_t *rh_path_to_wide(const char *utf8);
char *rh_path_from_wide(const wchar_t *wide);
#endif

#endif /* RH_PATH_H */
