#include "store.h"

#include <sqlite3.h>

#include "store-schema.h"

#define RH_STORE_DOMAIN "romham/store"

struct RhStore {
    sqlite3 *db;
    int schema_version;
    gboolean read_only;
};

static RhErr exec_sql(sqlite3 *db, const char *sql)
{
    char *msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &msg);
    if (rc != SQLITE_OK) {
        g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING,
              "sqlite: %s; statement: %s", msg != NULL ? msg : "?", sql);
        sqlite3_free(msg);
        return RH_ERR_DB;
    }
    return RH_OK;
}

static RhErr read_user_version(sqlite3 *db, int *out_version)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL) != SQLITE_OK)
        return RH_ERR_DB;

    RhErr err = RH_ERR_DB;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out_version = sqlite3_column_int(stmt, 0);
        err = RH_OK;
    }
    sqlite3_finalize(stmt);
    return err;
}

static RhErr set_user_version(sqlite3 *db, int version)
{
    char sql[64];
    g_snprintf(sql, sizeof sql, "PRAGMA user_version=%d;", version);
    return exec_sql(db, sql);
}

/* 空库：建 v1。FTS5 建立失败不阻断建库——检索降级为 LIKE（§3.5）。 */
static RhErr create_schema_v1(sqlite3 *db)
{
    if (exec_sql(db, "BEGIN IMMEDIATE;") != RH_OK)
        return RH_ERR_DB;

    for (int i = 0; i < rh_schema_v1_sql_count; i++) {
        if (exec_sql(db, rh_schema_v1_sql[i]) != RH_OK) {
            exec_sql(db, "ROLLBACK;");
            return RH_ERR_DB;
        }
    }

    for (int i = 0; i < rh_schema_fts_sql_count; i++) {
        if (exec_sql(db, rh_schema_fts_sql[i]) != RH_OK) {
            /* 当前 SQLite 缺少 FTS5 或 trigram：记录后继续 */
            g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_INFO,
                  "FTS5 不可用，全文检索降级为 LIKE");
            break;
        }
    }

    if (set_user_version(db, RH_SCHEMA_VERSION) != RH_OK ||
        exec_sql(db, "COMMIT;") != RH_OK) {
        exec_sql(db, "ROLLBACK;");
        return RH_ERR_DB;
    }
    return RH_OK;
}

RhStore *store_open(const char *path, RhErr *out_err)
{
    RhErr err = RH_OK;
    sqlite3 *db = NULL;

    if (out_err != NULL)
        *out_err = RH_OK;

    if (path == NULL || path[0] == '\0') {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return NULL;
    }

    if (sqlite3_open_v2(path, &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING, "无法打开库文件: %s",
              db != NULL ? sqlite3_errmsg(db) : "unknown");
        sqlite3_close(db);
        if (out_err != NULL)
            *out_err = RH_ERR_DB;
        return NULL;
    }

    sqlite3_busy_timeout(db, 5000);

    if (exec_sql(db, "PRAGMA journal_mode=WAL;") != RH_OK ||
        exec_sql(db, "PRAGMA foreign_keys=ON;") != RH_OK ||
        exec_sql(db, "PRAGMA synchronous=NORMAL;") != RH_OK) {
        err = RH_ERR_DB;
        goto fail;
    }

    int version = 0;
    if ((err = read_user_version(db, &version)) != RH_OK)
        goto fail;

    gboolean read_only = FALSE;

    if (version == 0) {
        if ((err = create_schema_v1(db)) != RH_OK)
            goto fail;
        version = RH_SCHEMA_VERSION;
    } else if (version > RH_SCHEMA_VERSION) {
        /* 降级防护：更高版本的库只读打开，绝不写入 */
        sqlite3_close(db);
        db = NULL;
        if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
            g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING, "无法以只读方式打开库文件");
            sqlite3_close(db);
            if (out_err != NULL)
                *out_err = RH_ERR_DB;
            return NULL;
        }
        sqlite3_busy_timeout(db, 5000);
        read_only = TRUE;
        g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING,
              "库 schema v%d 高于本程序 v%d，已只读打开", version, RH_SCHEMA_VERSION);
    } else if (version < RH_SCHEMA_VERSION) {
        /* M0 只有 v1；后续版本在此按 migration[] 逐级迁移 */
        g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING,
              "库 schema v%d 需要迁移到 v%d，但本版本未提供迁移脚本",
              version, RH_SCHEMA_VERSION);
        err = RH_ERR_MIGRATE;
        goto fail;
    }

    RhStore *store = g_new0(RhStore, 1);
    store->db = db;
    store->schema_version = version;
    store->read_only = read_only;
    return store;

fail:
    if (out_err != NULL)
        *out_err = err;
    sqlite3_close(db);
    return NULL;
}

void store_close(RhStore *store)
{
    if (store == NULL)
        return;
    sqlite3_close(store->db);
    g_free(store);
}

int store_schema_version(const RhStore *store)
{
    return store != NULL ? store->schema_version : 0;
}

gboolean store_is_read_only(const RhStore *store)
{
    return store != NULL ? store->read_only : TRUE;
}
