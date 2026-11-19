/*
 * cli-common.h —— CLI 子命令共用工具
 */
#ifndef RH_CLI_COMMON_H
#define RH_CLI_COMMON_H

#include <stdio.h>

#include "rh-err.h"
#include "store.h"

/* 始终按 UTF-8 写字节（§15；不依赖 locale）。 */
void rh_cli_print(FILE *stream, const char *text);

/* --db 的默认值：环境变量 ROM_HAMSTER_DB，否则 ./romhamster.db */
char *rh_cli_default_db(void);

/* 打开库；失败时向 stderr 打印一行并返回 NULL。 */
RhStore *rh_cli_open_store(const char *db, RhErr *out_err);

/* 统一错误输出：返回退出码 1。 */
int rh_cli_error(const char *message);

#endif /* RH_CLI_COMMON_H */
