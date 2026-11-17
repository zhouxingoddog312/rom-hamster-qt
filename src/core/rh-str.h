/*
 * rh-str.h —— 文件名与文本工具（ARCHITECTURE.md §2.1 L0）
 *
 * 只依赖 GLib；不碰文件系统，不打印。路径分隔符约定见 §15：
 * 内部统一用 '/' 存储，Windows 的 '\\' 由 rh-path 负责转换。
 */
#ifndef RH_STR_H
#define RH_STR_H

#include <glib.h>

/*
 * 最后一个后缀，含点、转小写；无后缀返回 NULL。
 * "Game.GB" -> ".gb"；"pico.p8.png" -> ".png"；"README" -> NULL。
 */
gchar *rh_str_extension(const char *filename);

/* 去掉最后一个后缀（保留目录部分）。"a/b.Game.GB" -> "a/b.Game" */
gchar *rh_str_stem(const char *filename);

/* 取文件名部分（最后一个 '/' 之后）。"a/b/c.gb" -> "c.gb" */
const gchar *rh_str_basename(const char *path);

/*
 * 归一化键：小写、只保留字母数字（Unicode 字母保留）。
 * 用于去重与相似度，不用于显示。"Final Fantasy VII (USA)" -> "finalfantasyviiusa"
 */
gchar *rh_str_norm_key(const char *s);

/*
 * 相似度 0–100：对归一化键做编辑距离并按较长者归一。
 * 两边都为空返回 100；一边为空返回 0。
 */
int rh_str_similarity(const char *a, const char *b);

/*
 * 从文件名解析碟号：识别 (Disc 1)、(Disk 2)、[CD3]、- Disc 1、CD1、Disk 1 of 2 等。
 * 命中返回 TRUE 并写 *disc_no（1 起）；未命中不改 *disc_no。
 * "v1.2" 里的 1 不算碟号。
 */
gboolean rh_str_parse_disc(const char *filename, int *disc_no);

/* 是否为 Windows 保留设备名（CON/PRN/AUX/NUL/COM1-9/LPT1-9，忽略大小写与后缀）。 */
gboolean rh_str_is_reserved_name(const char *name);

/*
 * 把任意字符串变成双端都合法的单级文件名/目录名：
 *   - 非法字符 < > : " / \ | ? * 与控制字符替换为 '_'
 *   - 去掉结尾的点与空格（Windows 不允许）
 *   - 保留名加 '_' 前缀
 *   - 超过 120 个字符按 UTF-8 截断
 *   - 结果为空时返回 "_"
 * 返回新分配字符串。
 */
gchar *rh_str_sanitize_component(const char *name);

#endif /* RH_STR_H */
