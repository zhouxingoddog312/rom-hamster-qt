/*
 * store-schema.h —— schema 版本与 DDL（ARCHITECTURE.md §3.3、§3.4）
 *
 * 只有 store.c 引用这些 SQL；其他模块不得出现 SQL 语句。
 */
#ifndef RH_STORE_SCHEMA_H
#define RH_STORE_SCHEMA_H

#define RH_SCHEMA_VERSION 1

/* v1 必装 DDL：9 张表 */
extern const char *const rh_schema_v1_sql[];
extern const int rh_schema_v1_sql_count;

/* v1 可选 DDL：FTS5 虚拟表与同步触发器；建立失败时降级为 LIKE（§3.5） */
extern const char *const rh_schema_fts_sql[];
extern const int rh_schema_fts_sql_count;

#endif /* RH_STORE_SCHEMA_H */
