/*
 * i18n.h —— CLI 的 gettext 接入（ARCHITECTURE.md §11）
 *
 * msgid 一律写英文，译文放在 po/<lang>.po，构建时由 msgfmt 编译成 .mo。
 * 核心库不使用这些宏：核心不产生用户可见字符串。
 */
#ifndef RH_CLI_I18N_H
#define RH_CLI_I18N_H

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "rom-hamster"
#endif

#include <glib/gi18n.h>

/* 在解析命令行之前调用一次：选择语言、绑定 .mo 目录与翻译域。 */
void rh_i18n_init(void);

#endif /* RH_CLI_I18N_H */
