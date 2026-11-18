/*
 * store-schema.h —— schema 版本与 DDL（ARCHITECTURE.md §3.3、§3.4）
 *
 * 只有 store.c 引用这些 SQL；其他模块不得出现 SQL 语句。
 */
#ifndef RH_STORE_SCHEMA_H
#define RH_STORE_SCHEMA_H

#define RH_SCHEMA_VERSION 4

/* v1 必装 DDL：9 张表 */
extern const char *const rh_schema_v1_sql[];
extern const int rh_schema_v1_sql_count;

/* v2 迁移：为路径类字段补原始字节列（文件名编码可逆） */
extern const char *const rh_schema_v2_sql[];
extern const int rh_schema_v2_sql_count;

/* v3 迁移：DAT 来源/编码、roms.dat_id，重建 dat_entries 以 game 为键的一部分 */
extern const char *const rh_schema_v3_sql[];
extern const int rh_schema_v3_sql_count;

/* v4 迁移：DAT 文件自身哈希、dat_entries 条目标类型（rom/disk）、(size,crc32) 复合索引 */
extern const char *const rh_schema_v4_sql[];
extern const int rh_schema_v4_sql_count;

/* v1 可选 DDL：FTS5 虚拟表与同步触发器；建立失败时降级为 LIKE（§3.5） */
extern const char *const rh_schema_fts_sql[];
extern const int rh_schema_fts_sql_count;

#endif /* RH_STORE_SCHEMA_H */
