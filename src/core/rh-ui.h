/*
 * rh-ui.h —— 前端接口表（ARCHITECTURE.md §6.1）
 *
 * 核心层只认这张表，不认终端也不认 Qt；CLI 与 Qt 各实现一份。
 * 本头文件只定义结构体，不含实现，也不引入任何 GUI 头文件。
 *
 * 线程约束（§6.2）：除 progress 外的回调都只允许在主线程调用；
 * 工作线程只能调 ui->progress。
 */
#ifndef RH_UI_H
#define RH_UI_H

#include <glib.h>

typedef enum {
    RH_LOG_DEBUG = 0,
    RH_LOG_INFO,
    RH_LOG_WARNING,
    RH_LOG_ERROR
} RhLogLevel;

typedef struct RhUi {
    void *userdata;

    void (*log)(void *ud, RhLogLevel lv, const char *msg);

    /* 进度：pct < 0 表示不确定进度（脉冲模式）。工作线程只允许调这一个回调 */
    void (*progress)(void *ud, double pct, const char *label);

    /* 确认：返回 1 = 是，0 = 否 */
    int (*confirm)(void *ud, const char *title, const char *msg, int default_yes);

    /* 单选：返回选中索引，-1 = 取消。remember_key 非空时可提供"应用到全部" */
    int (*choose)(void *ud, const char *title, const char **items, int n,
                  const char *remember_key);

    /* 文本输入：返回新分配的字符串（调用方 g_free），NULL = 取消 */
    char *(*enter)(void *ud, const char *title, const char *label, const char *def);
    char *(*password)(void *ud, const char *title, const char *label);

    /* 文件/目录选择：返回 GPtrArray<gchar*>（调用方 g_ptr_array_unref） */
    GPtrArray *(*pick_files)(void *ud, const char *title, int multiple);
    char *(*pick_dir)(void *ud, const char *title);

    /* 只读文本展示（详情、报告） */
    void (*text_view)(void *ud, const char *title, const char *text);

    /* 媒体类型判定：返回 RhMediaKind 枚举，-1 = 取消；remember_key 语义同 choose */
    int (*ask_media_kind)(void *ud, const char *path, const char *remember_key);
} RhUi;

#endif /* RH_UI_H */
