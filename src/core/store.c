#include "store.h"

#include <sqlite3.h>

#include "rh-records.h"
#include "store-schema.h"

#define RH_STORE_DOMAIN "romham/store"

struct RhStore {
    sqlite3 *db;
    int schema_version;
    gboolean read_only;
    gboolean in_tx;
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

/* 逐级迁移：只允许加表/加列/加索引（§3.4）。 */
static RhErr apply_migrations(sqlite3 *db, int from)
{
    int version = from;

    if (version < 2) {
        for (int i = 0; i < rh_schema_v2_sql_count; i++) {
            if (exec_sql(db, rh_schema_v2_sql[i]) != RH_OK)
                return RH_ERR_MIGRATE;
        }
        version = 2;
    }

    return set_user_version(db, version);
}

/* 旧库升级：单事务，失败回滚并保留原库。 */
static RhErr migrate_schema(sqlite3 *db, int from)
{
    if (exec_sql(db, "BEGIN IMMEDIATE;") != RH_OK)
        return RH_ERR_DB;

    RhErr err = apply_migrations(db, from);
    if (err == RH_OK) {
        if (exec_sql(db, "COMMIT;") != RH_OK) {
            exec_sql(db, "ROLLBACK;");
            return RH_ERR_MIGRATE;
        }
        return RH_OK;
    }

    exec_sql(db, "ROLLBACK;");
    return RH_ERR_MIGRATE;
}

/* 空库：建 v1 再按 migration[] 升到当前版本。FTS5 失败不阻断（§3.5）。 */
static RhErr create_schema(sqlite3 *db)
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

    if (set_user_version(db, 1) != RH_OK ||
        apply_migrations(db, 1) != RH_OK) {
        exec_sql(db, "ROLLBACK;");
        return RH_ERR_DB;
    }

    if (exec_sql(db, "COMMIT;") != RH_OK) {
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
        if ((err = create_schema(db)) != RH_OK)
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
        if ((err = migrate_schema(db, version)) != RH_OK)
            goto fail;
        version = RH_SCHEMA_VERSION;
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

/* ===========================================================================
 * 内部工具
 * ========================================================================= */

static RhErr ensure_writable(const RhStore *store)
{
    if (store == NULL)
        return RH_ERR_INVALID;
    return store->read_only ? RH_ERR_SCHEMA_TOO_NEW : RH_OK;
}

static RhErr prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt)
{
    if (sqlite3_prepare_v2(db, sql, -1, stmt, NULL) != SQLITE_OK) {
        g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING, "prepare 失败: %s",
              sqlite3_errmsg(db));
        *stmt = NULL;
        return RH_ERR_DB;
    }
    return RH_OK;
}

static void bind_text(sqlite3_stmt *stmt, int index, const char *value)
{
    if (value != NULL)
        sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, index);
}

/* 原始字节列：NULL 表示旧库未回填；空 GBytes 表示合法的零字节路径。 */
static void bind_blob(sqlite3_stmt *stmt, int index, GBytes *bytes)
{
    if (bytes == NULL) {
        sqlite3_bind_null(stmt, index);
        return;
    }
    gsize len = 0;
    const void *data = g_bytes_get_data(bytes, &len);
    sqlite3_bind_blob(stmt, index, data, (int)len, SQLITE_TRANSIENT);
}

static GBytes *column_bytes(sqlite3_stmt *stmt, int index)
{
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL)
        return NULL;
    const void *data = sqlite3_column_blob(stmt, index);
    int len = sqlite3_column_bytes(stmt, index);
    if (data == NULL || len <= 0)
        return g_bytes_new("", 0);
    return g_bytes_new(data, (gsize)len);
}

static void bind_int_or_null(sqlite3_stmt *stmt, int index, gint value)
{
    if (value != 0)
        sqlite3_bind_int(stmt, index, value);
    else
        sqlite3_bind_null(stmt, index);
}

static void bind_real_or_null(sqlite3_stmt *stmt, int index, gdouble value)
{
    if (value != 0.0)
        sqlite3_bind_double(stmt, index, value);
    else
        sqlite3_bind_null(stmt, index);
}

static gchar *column_str(sqlite3_stmt *stmt, int index)
{
    return sqlite3_column_type(stmt, index) == SQLITE_NULL
               ? NULL
               : g_strdup((const gchar *)sqlite3_column_text(stmt, index));
}

/* ===========================================================================
 * 事务
 * ========================================================================= */

RhErr store_begin(RhStore *store)
{
    RhErr err = ensure_writable(store);
    if (err != RH_OK)
        return err;
    if (store->in_tx)
        return RH_OK;

    if (exec_sql(store->db, "BEGIN IMMEDIATE;") != RH_OK)
        return RH_ERR_DB;

    store->in_tx = TRUE;
    return RH_OK;
}

RhErr store_commit(RhStore *store)
{
    if (store == NULL || !store->in_tx)
        return RH_ERR_INVALID;

    RhErr err = exec_sql(store->db, "COMMIT;") == RH_OK ? RH_OK : RH_ERR_DB;
    store->in_tx = FALSE;
    if (err != RH_OK)
        exec_sql(store->db, "ROLLBACK;");
    return err;
}

RhErr store_rollback(RhStore *store)
{
    if (store == NULL || !store->in_tx)
        return RH_ERR_INVALID;

    RhErr err = exec_sql(store->db, "ROLLBACK;") == RH_OK ? RH_OK : RH_ERR_DB;
    store->in_tx = FALSE;
    return err;
}

/* ===========================================================================
 * 写入
 * ========================================================================= */

static RhErr insert_roms(RhStore *store, const char *game_id, const GPtrArray *roms)
{
    if (roms == NULL)
        return RH_OK;

    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db,
                        "INSERT INTO roms (game_id, filename, disc_no, size,"
                        " crc32, md5, sha1, sort, filename_raw)"
                        " VALUES (?,?,?,?,?,?,?,?,?);",
                        &stmt);
    if (err != RH_OK)
        return err;

    for (guint i = 0; i < roms->len; i++) {
        const RhRomFile *rom = g_ptr_array_index(roms, i);
        if (rom == NULL || rom->filename == NULL) {
            err = RH_ERR_INVALID;
            break;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bind_text(stmt, 1, game_id);
        bind_text(stmt, 2, rom->filename);
        bind_int_or_null(stmt, 3, rom->disc_no);
        if (rom->has_size)
            sqlite3_bind_int64(stmt, 4, rom->size);
        else
            sqlite3_bind_null(stmt, 4);
        bind_text(stmt, 5, rom->crc32);
        bind_text(stmt, 6, rom->md5);
        bind_text(stmt, 7, rom->sha1);
        sqlite3_bind_int(stmt, 8, rom->sort);
        bind_blob(stmt, 9, rom->filename_raw);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING, "写 roms 失败: %s",
                  sqlite3_errmsg(store->db));
            err = RH_ERR_DB;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return err;
}

static RhErr insert_media(RhStore *store, const char *game_id, const GPtrArray *media)
{
    if (media == NULL)
        return RH_OK;

    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db,
                        "INSERT INTO media (game_id, kind, filename, size, sort,"
                        " filename_raw)"
                        " VALUES (?,?,?,?,?,?);",
                        &stmt);
    if (err != RH_OK)
        return err;

    for (guint i = 0; i < media->len; i++) {
        const RhMediaFile *item = g_ptr_array_index(media, i);
        if (item == NULL || item->filename == NULL || !rh_media_kind_valid(item->kind)) {
            err = RH_ERR_INVALID;
            break;
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        bind_text(stmt, 1, game_id);
        bind_text(stmt, 2, item->kind);
        bind_text(stmt, 3, item->filename);
        if (item->has_size)
            sqlite3_bind_int64(stmt, 4, item->size);
        else
            sqlite3_bind_null(stmt, 4);
        sqlite3_bind_int(stmt, 5, item->sort);
        bind_blob(stmt, 6, item->filename_raw);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING, "写 media 失败: %s",
                  sqlite3_errmsg(store->db));
            err = RH_ERR_DB;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return err;
}

static RhErr insert_tags(RhStore *store, const char *game_id, const GPtrArray *tags)
{
    if (tags == NULL)
        return RH_OK;

    sqlite3_stmt *upsert = NULL;
    sqlite3_stmt *select = NULL;
    sqlite3_stmt *link = NULL;
    RhErr err = prepare(store->db, "INSERT OR IGNORE INTO tags (name) VALUES (?);", &upsert);
    if (err == RH_OK)
        err = prepare(store->db, "SELECT id FROM tags WHERE name = ?;", &select);
    if (err == RH_OK)
        err = prepare(store->db, "INSERT OR IGNORE INTO game_tags (game_id, tag_id) VALUES (?,?);", &link);

    for (guint i = 0; err == RH_OK && i < tags->len; i++) {
        const char *name = g_ptr_array_index(tags, i);
        if (name == NULL || name[0] == '\0') {
            err = RH_ERR_INVALID;
            break;
        }

        sqlite3_reset(upsert);
        sqlite3_clear_bindings(upsert);
        bind_text(upsert, 1, name);
        if (sqlite3_step(upsert) != SQLITE_DONE) {
            err = RH_ERR_DB;
            break;
        }

        sqlite3_reset(select);
        sqlite3_clear_bindings(select);
        bind_text(select, 1, name);
        if (sqlite3_step(select) != SQLITE_ROW) {
            err = RH_ERR_DB;
            break;
        }
        sqlite3_int64 tag_id = sqlite3_column_int64(select, 0);

        sqlite3_reset(link);
        sqlite3_clear_bindings(link);
        bind_text(link, 1, game_id);
        sqlite3_bind_int64(link, 2, tag_id);
        if (sqlite3_step(link) != SQLITE_DONE) {
            err = RH_ERR_DB;
            break;
        }
    }

    sqlite3_finalize(upsert);
    sqlite3_finalize(select);
    sqlite3_finalize(link);
    return err;
}

static RhErr insert_game_row(RhStore *store, const RhGame *game)
{
    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db,
                        "INSERT INTO games (id, platform, name, game_main_name, dir_path,"
                        " developer, publisher, year, genre, players, rating, region,"
                        " description, is_multi_disc, is_arcade, rom_format, rom_status,"
                        " dat_source, patch_type, dir_path_raw, created_at, updated_at)"
                        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
                        " COALESCE(?, strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
                        " strftime('%Y-%m-%dT%H:%M:%SZ','now'));",
                        &stmt);
    if (err != RH_OK)
        return err;

    bind_text(stmt, 1, game->id);
    bind_text(stmt, 2, game->platform);
    bind_text(stmt, 3, game->name);
    bind_text(stmt, 4, game->game_main_name);
    bind_text(stmt, 5, game->dir_path);
    bind_text(stmt, 6, game->developer);
    bind_text(stmt, 7, game->publisher);
    bind_int_or_null(stmt, 8, game->year);
    bind_text(stmt, 9, game->genre);
    bind_int_or_null(stmt, 10, game->players);
    bind_real_or_null(stmt, 11, game->rating);
    bind_text(stmt, 12, game->region);
    bind_text(stmt, 13, game->description);
    sqlite3_bind_int(stmt, 14, game->is_multi_disc ? 1 : 0);
    sqlite3_bind_int(stmt, 15, game->is_arcade ? 1 : 0);
    bind_text(stmt, 16, game->rom_format);
    bind_text(stmt, 17, game->rom_status);
    bind_text(stmt, 18, game->dat_source);
    bind_text(stmt, 19, game->patch_type);
    bind_blob(stmt, 20, game->dir_path_raw);
    bind_text(stmt, 21, game->created_at);

    err = sqlite3_step(stmt) == SQLITE_DONE ? RH_OK : RH_ERR_DB;
    if (err != RH_OK)
        g_log(RH_STORE_DOMAIN, G_LOG_LEVEL_WARNING, "写 games 失败: %s",
              sqlite3_errmsg(store->db));
    sqlite3_finalize(stmt);
    return err;
}

RhErr store_game_insert(RhStore *store, const RhGame *game,
                        const GPtrArray *roms, const GPtrArray *media,
                        const GPtrArray *tags)
{
    RhErr err = ensure_writable(store);
    if (err != RH_OK)
        return err;
    if (game == NULL || game->id == NULL || game->platform == NULL ||
        game->name == NULL || game->dir_path == NULL)
        return RH_ERR_INVALID;

    gboolean own_tx = !store->in_tx;
    if (own_tx && (err = store_begin(store)) != RH_OK)
        return err;

    err = insert_game_row(store, game);
    if (err == RH_OK)
        err = insert_roms(store, game->id, roms);
    if (err == RH_OK)
        err = insert_media(store, game->id, media);
    if (err == RH_OK)
        err = insert_tags(store, game->id, tags);

    if (own_tx) {
        if (err == RH_OK)
            err = store_commit(store);
        else
            store_rollback(store);
    }
    return err;
}

RhErr store_game_update(RhStore *store, const RhGame *game)
{
    RhErr err = ensure_writable(store);
    if (err != RH_OK)
        return err;
    if (game == NULL || game->id == NULL)
        return RH_ERR_INVALID;

    sqlite3_stmt *stmt = NULL;
    err = prepare(store->db,
                  "UPDATE games SET platform = ?, name = ?, game_main_name = ?,"
                  " dir_path = ?, developer = ?, publisher = ?, year = ?, genre = ?,"
                  " players = ?, rating = ?, region = ?, description = ?,"
                  " is_multi_disc = ?, is_arcade = ?, rom_format = ?, rom_status = ?,"
                  " dat_source = ?, patch_type = ?, dir_path_raw = ?,"
                  " updated_at = strftime('%Y-%m-%dT%H:%M:%SZ','now')"
                  " WHERE id = ?;",
                  &stmt);
    if (err != RH_OK)
        return err;

    bind_text(stmt, 1, game->platform);
    bind_text(stmt, 2, game->name);
    bind_text(stmt, 3, game->game_main_name);
    bind_text(stmt, 4, game->dir_path);
    bind_text(stmt, 5, game->developer);
    bind_text(stmt, 6, game->publisher);
    bind_int_or_null(stmt, 7, game->year);
    bind_text(stmt, 8, game->genre);
    bind_int_or_null(stmt, 9, game->players);
    bind_real_or_null(stmt, 10, game->rating);
    bind_text(stmt, 11, game->region);
    bind_text(stmt, 12, game->description);
    sqlite3_bind_int(stmt, 13, game->is_multi_disc ? 1 : 0);
    sqlite3_bind_int(stmt, 14, game->is_arcade ? 1 : 0);
    bind_text(stmt, 15, game->rom_format);
    bind_text(stmt, 16, game->rom_status);
    bind_text(stmt, 17, game->dat_source);
    bind_text(stmt, 18, game->patch_type);
    bind_blob(stmt, 19, game->dir_path_raw);
    bind_text(stmt, 20, game->id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? RH_OK : RH_ERR_DB;
}

RhErr store_game_delete(RhStore *store, const char *id)
{
    RhErr err = ensure_writable(store);
    if (err != RH_OK)
        return err;
    if (id == NULL)
        return RH_ERR_INVALID;

    sqlite3_stmt *stmt = NULL;
    if ((err = prepare(store->db, "DELETE FROM games WHERE id = ?;", &stmt)) != RH_OK)
        return err;

    bind_text(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? RH_OK : RH_ERR_DB;
}

/* ===========================================================================
 * 读取
 * ========================================================================= */

#define GAME_COLUMNS                                                        \
    "id, platform, name, game_main_name, dir_path, developer, publisher,"   \
    " year, genre, players, rating, region, description, is_multi_disc,"   \
    " is_arcade, rom_format, rom_status, dat_source, patch_type,"          \
    " created_at, updated_at, dir_path_raw"

static RhGame *read_game_row(sqlite3_stmt *stmt)
{
    RhGame *game = rh_game_new();
    game->id = column_str(stmt, 0);
    game->platform = column_str(stmt, 1);
    game->name = column_str(stmt, 2);
    game->game_main_name = column_str(stmt, 3);
    game->dir_path = column_str(stmt, 4);
    game->developer = column_str(stmt, 5);
    game->publisher = column_str(stmt, 6);
    game->year = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? 0 : sqlite3_column_int(stmt, 7);
    game->genre = column_str(stmt, 8);
    game->players = sqlite3_column_type(stmt, 9) == SQLITE_NULL ? 0 : sqlite3_column_int(stmt, 9);
    game->rating = sqlite3_column_type(stmt, 10) == SQLITE_NULL ? 0.0 : sqlite3_column_double(stmt, 10);
    game->region = column_str(stmt, 11);
    game->description = column_str(stmt, 12);
    game->is_multi_disc = sqlite3_column_int(stmt, 13) != 0;
    game->is_arcade = sqlite3_column_int(stmt, 14) != 0;

    g_free(game->rom_format);
    game->rom_format = column_str(stmt, 15);
    g_free(game->rom_status);
    game->rom_status = column_str(stmt, 16);
    game->dat_source = column_str(stmt, 17);
    g_free(game->patch_type);
    game->patch_type = column_str(stmt, 18);
    game->created_at = column_str(stmt, 19);
    game->updated_at = column_str(stmt, 20);
    game->dir_path_raw = column_bytes(stmt, 21);
    return game;
}

RhGame *store_game_get(RhStore *store, const char *id, RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    if (store == NULL || id == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return NULL;
    }

    gchar *sql = g_strdup_printf("SELECT " GAME_COLUMNS " FROM games WHERE id = ?;");
    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db, sql, &stmt);
    g_free(sql);
    if (err != RH_OK) {
        if (out_err != NULL)
            *out_err = err;
        return NULL;
    }

    bind_text(stmt, 1, id);
    RhGame *game = NULL;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        game = read_game_row(stmt);
    } else if (rc != SQLITE_DONE) {
        err = RH_ERR_DB;
    }

    sqlite3_finalize(stmt);
    if (out_err != NULL)
        *out_err = err;
    return game;
}

GPtrArray *store_roms_for_game(RhStore *store, const char *id, RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    if (store == NULL || id == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return roms;
    }

    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db,
                        "SELECT filename, disc_no, size, crc32, md5, sha1, sort,"
                        " filename_raw"
                        " FROM roms WHERE game_id = ? ORDER BY sort, filename;",
                        &stmt);
    if (err != RH_OK) {
        if (out_err != NULL)
            *out_err = err;
        return roms;
    }

    bind_text(stmt, 1, id);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        RhRomFile *rom = rh_rom_file_new();
        rom->filename = column_str(stmt, 0);
        rom->disc_no = sqlite3_column_type(stmt, 1) == SQLITE_NULL ? 0 : sqlite3_column_int(stmt, 1);
        rom->has_size = sqlite3_column_type(stmt, 2) != SQLITE_NULL;
        rom->size = rom->has_size ? sqlite3_column_int64(stmt, 2) : 0;
        rom->crc32 = column_str(stmt, 3);
        rom->md5 = column_str(stmt, 4);
        rom->sha1 = column_str(stmt, 5);
        rom->sort = sqlite3_column_int(stmt, 6);
        rom->filename_raw = column_bytes(stmt, 7);
        g_ptr_array_add(roms, rom);
    }
    if (rc != SQLITE_DONE && out_err != NULL)
        *out_err = RH_ERR_DB;

    sqlite3_finalize(stmt);
    return roms;
}

GPtrArray *store_media_for_game(RhStore *store, const char *id, const char *kind,
                                RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    GPtrArray *media = g_ptr_array_new_with_free_func(rh_media_file_free_func);
    if (store == NULL || id == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return media;
    }
    if (kind != NULL && !rh_media_kind_valid(kind)) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return media;
    }

    const char *sql = kind == NULL
        ? "SELECT kind, filename, size, sort, filename_raw FROM media WHERE game_id = ?"
          " ORDER BY kind, sort, filename;"
        : "SELECT kind, filename, size, sort, filename_raw FROM media"
          " WHERE game_id = ? AND kind = ?"
          " ORDER BY sort, filename;";

    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db, sql, &stmt);
    if (err != RH_OK) {
        if (out_err != NULL)
            *out_err = err;
        return media;
    }

    bind_text(stmt, 1, id);
    if (kind != NULL)
        bind_text(stmt, 2, kind);

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        RhMediaFile *item = rh_media_file_new();
        item->kind = column_str(stmt, 0);
        item->filename = column_str(stmt, 1);
        item->has_size = sqlite3_column_type(stmt, 2) != SQLITE_NULL;
        item->size = item->has_size ? sqlite3_column_int64(stmt, 2) : 0;
        item->sort = sqlite3_column_int(stmt, 3);
        item->filename_raw = column_bytes(stmt, 4);
        g_ptr_array_add(media, item);
    }
    if (rc != SQLITE_DONE && out_err != NULL)
        *out_err = RH_ERR_DB;

    sqlite3_finalize(stmt);
    return media;
}

GPtrArray *store_tags_for_game(RhStore *store, const char *id, RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    GPtrArray *tags = g_ptr_array_new_with_free_func(g_free);
    if (store == NULL || id == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return tags;
    }

    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db,
                        "SELECT t.name FROM tags t"
                        " JOIN game_tags gt ON gt.tag_id = t.id"
                        " WHERE gt.game_id = ? ORDER BY t.name;",
                        &stmt);
    if (err != RH_OK) {
        if (out_err != NULL)
            *out_err = err;
        return tags;
    }

    bind_text(stmt, 1, id);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        g_ptr_array_add(tags, column_str(stmt, 0));
    if (rc != SQLITE_DONE && out_err != NULL)
        *out_err = RH_ERR_DB;

    sqlite3_finalize(stmt);
    return tags;
}

/* WHERE 子句构建：平台、搜索、状态、标签 */
static gchar *build_where(const RhGameFilter *filter, GPtrArray *params,
                          gboolean *join_tags)
{
    GString *where = g_string_new(NULL);
    *join_tags = FALSE;

    if (filter != NULL && filter->platform != NULL) {
        g_string_append(where, where->len ? " AND " : " WHERE ");
        g_string_append(where, "g.platform = ?");
        g_ptr_array_add(params, g_strdup(filter->platform));
    }
    if (filter != NULL && filter->rom_status != NULL) {
        g_string_append(where, where->len ? " AND " : " WHERE ");
        g_string_append(where, "g.rom_status = ?");
        g_ptr_array_add(params, g_strdup(filter->rom_status));
    }
    if (filter != NULL && filter->search != NULL && filter->search[0] != '\0') {
        g_string_append(where, where->len ? " AND " : " WHERE ");
        g_string_append(where, "(g.name LIKE ? OR g.game_main_name LIKE ?)");
        gchar *pattern = g_strdup_printf("%%%s%%", filter->search);
        g_ptr_array_add(params, pattern);
        g_ptr_array_add(params, g_strdup(pattern));
    }
    if (filter != NULL && filter->tag != NULL) {
        *join_tags = TRUE;
        g_string_append(where, where->len ? " AND " : " WHERE ");
        g_string_append(where,
                        "g.id IN (SELECT gt.game_id FROM game_tags gt"
                        " JOIN tags t ON t.id = gt.tag_id WHERE t.name = ?)");
        g_ptr_array_add(params, g_strdup(filter->tag));
    }
    return g_string_free(where, FALSE);
}

static const char *order_clause(const RhGameFilter *filter)
{
    const char *column = "g.name";
    if (filter != NULL && filter->order_by != NULL) {
        if (g_strcmp0(filter->order_by, "year") == 0)
            column = "g.year";
        else if (g_strcmp0(filter->order_by, "rating") == 0)
            column = "g.rating";
        else if (g_strcmp0(filter->order_by, "updated") == 0)
            column = "g.updated_at";
    }

    gboolean desc = filter != NULL && filter->descending;
    if (g_strcmp0(column, "g.name") == 0)
        return desc ? "g.name DESC" : "g.name ASC";
    if (g_strcmp0(column, "g.year") == 0)
        return desc ? "g.year DESC, g.name ASC" : "g.year ASC, g.name ASC";
    if (g_strcmp0(column, "g.rating") == 0)
        return desc ? "g.rating DESC, g.name ASC" : "g.rating ASC, g.name ASC";
    return desc ? "g.updated_at DESC, g.name ASC" : "g.updated_at ASC, g.name ASC";
}

GPtrArray *store_games_list(RhStore *store, const RhGameFilter *filter, RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    GPtrArray *games = g_ptr_array_new_with_free_func(rh_game_free_func);
    if (store == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return games;
    }

    GPtrArray *params = g_ptr_array_new_with_free_func(g_free);
    gboolean join_tags = FALSE;
    gchar *where = build_where(filter, params, &join_tags);
    (void)join_tags;

    int limit = (filter != NULL && filter->limit > 0) ? filter->limit : 200;
    int offset = (filter != NULL && filter->offset > 0) ? filter->offset : 0;

    gchar *sql = g_strdup_printf("SELECT " GAME_COLUMNS " FROM games g%s ORDER BY %s"
                                 " LIMIT ? OFFSET ?;",
                                 where, order_clause(filter));
    g_free(where);

    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db, sql, &stmt);
    g_free(sql);
    if (err != RH_OK) {
        g_ptr_array_free(params, TRUE);
        if (out_err != NULL)
            *out_err = err;
        return games;
    }

    int index = 1;
    for (guint i = 0; i < params->len; i++)
        bind_text(stmt, index++, g_ptr_array_index(params, i));
    sqlite3_bind_int(stmt, index++, limit);
    sqlite3_bind_int(stmt, index++, offset);

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        g_ptr_array_add(games, read_game_row(stmt));
    if (rc != SQLITE_DONE && out_err != NULL)
        *out_err = RH_ERR_DB;

    sqlite3_finalize(stmt);
    g_ptr_array_free(params, TRUE);
    return games;
}

gint64 store_games_count(RhStore *store, const RhGameFilter *filter, RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    if (store == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return -1;
    }

    GPtrArray *params = g_ptr_array_new_with_free_func(g_free);
    gboolean join_tags = FALSE;
    gchar *where = build_where(filter, params, &join_tags);
    (void)join_tags;

    gchar *sql = g_strdup_printf("SELECT COUNT(*) FROM games g%s;", where);
    g_free(where);

    sqlite3_stmt *stmt = NULL;
    RhErr err = prepare(store->db, sql, &stmt);
    g_free(sql);
    if (err != RH_OK) {
        g_ptr_array_free(params, TRUE);
        if (out_err != NULL)
            *out_err = err;
        return -1;
    }

    for (guint i = 0; i < params->len; i++)
        bind_text(stmt, (int)i + 1, g_ptr_array_index(params, i));

    gint64 count = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int64(stmt, 0);
    else if (out_err != NULL)
        *out_err = RH_ERR_DB;

    sqlite3_finalize(stmt);
    g_ptr_array_free(params, TRUE);
    return count;
}

/* ===========================================================================
 * 作业
 * ========================================================================= */

RhErr store_job_create(RhStore *store, const char *kind, int total, gint64 *out_id)
{
    RhErr err = ensure_writable(store);
    if (err != RH_OK)
        return err;
    if (kind == NULL || out_id == NULL)
        return RH_ERR_INVALID;

    sqlite3_stmt *stmt = NULL;
    err = prepare(store->db,
                  "INSERT INTO jobs (kind, state, total, done, created_at)"
                  " VALUES (?, 'running', ?, 0, strftime('%Y-%m-%dT%H:%M:%SZ','now'));",
                  &stmt);
    if (err != RH_OK)
        return err;

    bind_text(stmt, 1, kind);
    sqlite3_bind_int(stmt, 2, total);
    err = sqlite3_step(stmt) == SQLITE_DONE ? RH_OK : RH_ERR_DB;
    sqlite3_finalize(stmt);

    if (err == RH_OK)
        *out_id = sqlite3_last_insert_rowid(store->db);
    return err;
}

RhErr store_job_progress(RhStore *store, gint64 job_id, int done)
{
    RhErr err = ensure_writable(store);
    if (err != RH_OK)
        return err;

    sqlite3_stmt *stmt = NULL;
    if ((err = prepare(store->db, "UPDATE jobs SET done = ? WHERE id = ?;", &stmt)) != RH_OK)
        return err;

    sqlite3_bind_int(stmt, 1, done);
    sqlite3_bind_int64(stmt, 2, job_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? RH_OK : RH_ERR_DB;
}

RhErr store_job_finish(RhStore *store, gint64 job_id, const char *error)
{
    RhErr err = ensure_writable(store);
    if (err != RH_OK)
        return err;

    sqlite3_stmt *stmt = NULL;
    err = prepare(store->db,
                  "UPDATE jobs SET state = ?, error = ?,"
                  " finished_at = strftime('%Y-%m-%dT%H:%M:%SZ','now')"
                  " WHERE id = ?;",
                  &stmt);
    if (err != RH_OK)
        return err;

    bind_text(stmt, 1, error == NULL ? "done" : "failed");
    bind_text(stmt, 2, error);
    sqlite3_bind_int64(stmt, 3, job_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? RH_OK : RH_ERR_DB;
}
