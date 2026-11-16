/*
 * store.h —— 唯一持有 SQL 的模块（ARCHITECTURE.md §4、§5.1）
 */
#ifndef RH_STORE_H
#define RH_STORE_H

#include <glib.h>

#include "rh-err.h"

typedef struct RhStore RhStore;

/*
 * 打开（必要时创建）库文件。
 * - schema 为空：建 v1 并写入 user_version；
 * - user_version 低于程序：按迁移表升级；
 * - user_version 高于程序：**只读**打开（store_is_read_only() 为 TRUE），
 *   前端据此拒绝写入并提示升级程序（§3.4、§9.4）。
 */
RhStore *store_open(const char *path, RhErr *out_err);
void store_close(RhStore *store);

int store_schema_version(const RhStore *store);
gboolean store_is_read_only(const RhStore *store);

#endif /* RH_STORE_H */
