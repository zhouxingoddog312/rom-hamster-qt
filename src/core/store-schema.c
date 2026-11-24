#include "store-schema.h"

/* 类型化三原则：空串转 NULL、布尔 1/0、多值拆子表（§3.2） */
const char *const rh_schema_v1_sql[] = {
    "CREATE TABLE games ("
    "  id             TEXT PRIMARY KEY,"
    "  platform       TEXT NOT NULL,"
    "  name           TEXT NOT NULL,"
    "  game_main_name TEXT,"
    "  dir_path       TEXT NOT NULL UNIQUE,"
    "  developer      TEXT,"
    "  publisher      TEXT,"
    "  year           INTEGER,"
    "  genre          TEXT,"
    "  players        INTEGER,"
    "  rating         REAL,"
    "  region         TEXT,"
    "  description    TEXT,"
    "  is_multi_disc  INTEGER NOT NULL DEFAULT 0 CHECK (is_multi_disc IN (0,1)),"
    "  is_arcade      INTEGER NOT NULL DEFAULT 0 CHECK (is_arcade IN (0,1)),"
    "  rom_format     TEXT NOT NULL DEFAULT 'raw',"
    "  rom_status     TEXT NOT NULL DEFAULT 'unknown'"
    "                 CHECK (rom_status IN ('good','baddump','unknown')),"
    "  dat_source     TEXT,"
    "  patch_type     TEXT NOT NULL DEFAULT ''"
    "                 CHECK (patch_type IN ('','generic','platform','mixed')),"
    "  created_at     TEXT NOT NULL,"
    "  updated_at     TEXT NOT NULL"
    ")",

    "CREATE TABLE roms ("
    "  game_id  TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,"
    "  filename TEXT NOT NULL,"
    "  disc_no  INTEGER,"
    "  size     INTEGER,"
    "  crc32    TEXT,"
    "  md5      TEXT,"
    "  sha1     TEXT,"
    "  sort     INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (game_id, filename)"
    ")",

    "CREATE TABLE media ("
    "  game_id  TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,"
    "  kind     TEXT NOT NULL CHECK (kind IN"
    "           ('covers','screenshots','videos','marquees','miximages','intro','patcher')),"
    "  filename TEXT NOT NULL,"
    "  size     INTEGER,"
    "  sort     INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (game_id, kind, filename)"
    ")",

    "CREATE TABLE tags ("
    "  id   INTEGER PRIMARY KEY,"
    "  name TEXT NOT NULL UNIQUE"
    ")",

    "CREATE TABLE game_tags ("
    "  game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,"
    "  tag_id  INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,"
    "  PRIMARY KEY (game_id, tag_id)"
    ")",

    "CREATE TABLE dats ("
    "  id           INTEGER PRIMARY KEY,"
    "  platform     TEXT NOT NULL,"
    "  path         TEXT NOT NULL,"
    "  name         TEXT,"
    "  version      TEXT,"
    "  imported_at  TEXT NOT NULL,"
    "  entry_count  INTEGER,"
    "  UNIQUE (platform, path)"
    ")",

    "CREATE TABLE dat_entries ("
    "  dat_id    INTEGER NOT NULL REFERENCES dats(id) ON DELETE CASCADE,"
    "  game_name TEXT,"
    "  rom_name  TEXT NOT NULL,"
    "  size      INTEGER,"
    "  crc32     TEXT,"
    "  md5       TEXT,"
    "  sha1      TEXT,"
    "  PRIMARY KEY (dat_id, rom_name)"
    ")",

    "CREATE TABLE jobs ("
    "  id          INTEGER PRIMARY KEY,"
    "  kind        TEXT NOT NULL,"
    "  state       TEXT NOT NULL,"
    "  total       INTEGER NOT NULL DEFAULT 0,"
    "  done        INTEGER NOT NULL DEFAULT 0,"
    "  created_at  TEXT NOT NULL,"
    "  finished_at TEXT,"
    "  error       TEXT"
    ")",

    "CREATE TABLE job_items ("
    "  job_id     INTEGER NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,"
    "  seq        INTEGER NOT NULL,"
    "  src        TEXT NOT NULL,"
    "  action     TEXT,"
    "  platform   TEXT,"
    "  target_id  TEXT,"
    "  state      TEXT NOT NULL DEFAULT 'pending',"
    "  message    TEXT,"
    "  PRIMARY KEY (job_id, seq)"
    ")",

    "CREATE INDEX idx_games_platform  ON games(platform)",
    "CREATE INDEX idx_games_name      ON games(name)",
    "CREATE INDEX idx_games_year      ON games(year)",
    "CREATE INDEX idx_games_rating    ON games(rating)",
    "CREATE INDEX idx_games_status    ON games(rom_status)",
    "CREATE INDEX idx_roms_disc       ON roms(game_id, disc_no)",
    "CREATE INDEX idx_dat_crc32       ON dat_entries(crc32)",
    "CREATE INDEX idx_dat_md5         ON dat_entries(md5)",
    "CREATE INDEX idx_dat_sha1        ON dat_entries(sha1)"
};

const int rh_schema_v1_sql_count =
    (int)(sizeof(rh_schema_v1_sql) / sizeof(rh_schema_v1_sql[0]));

/*
 * v2：所有由用户文件名推导出的路径都补一列原始字节。
 * 显示列保存 UTF-8（可能是 GB18030 推测或转义结果），BLOB 列保存磁盘/归档
 * 里的真实字节，二者同时存在才能「显示正确」且「可还原」。旧库的行留 NULL，
 * 首次重新扫描时补齐。
 */
const char *const rh_schema_v2_sql[] = {
    "ALTER TABLE games ADD COLUMN dir_path_raw BLOB",
    "ALTER TABLE roms  ADD COLUMN filename_raw BLOB",
    "ALTER TABLE media ADD COLUMN filename_raw BLOB"
};

const int rh_schema_v2_sql_count =
    (int)(sizeof(rh_schema_v2_sql) / sizeof(rh_schema_v2_sql[0]));

/*
 * v3：DAT 阶段。
 *
 * 1) dats 记录 DAT 文件本身的编码判定与跳过条目数，换 DAT 后可对比；
 * 2) roms.dat_id 记录这条 ROM 的匹配来自哪个 DAT（NULL = 未匹配）；
 * 3) dat_entries 旧主键 (dat_id, rom_name) 与真实 DAT 冲突：同一 ROM 名
 *    在不同 game 下重复是常态（MAME parent/clone 尤甚）。改为代理主键 +
 *    (dat_id, game_name, rom_name) 唯一索引，并保留 status/cloneof/romof/merge。
 *    dat_entries 在本阶段之前从未写入用户数据，重建不涉及数据取舍；
 *    迁移仍按"先复制再改名"在同事务内完成。
 */
const char *const rh_schema_v3_sql[] = {
    "ALTER TABLE dats ADD COLUMN encoding TEXT",
    "ALTER TABLE dats ADD COLUMN skipped INTEGER",
    "ALTER TABLE roms ADD COLUMN dat_id INTEGER REFERENCES dats(id)",

    "CREATE TABLE dat_entries_v3 ("
    "  id         INTEGER PRIMARY KEY,"
    "  dat_id     INTEGER NOT NULL REFERENCES dats(id) ON DELETE CASCADE,"
    "  game_name  TEXT NOT NULL DEFAULT '',"
    "  rom_name   TEXT NOT NULL,"
    "  status     TEXT,"
    "  cloneof    TEXT,"
    "  romof      TEXT,"
    "  merge_name TEXT,"
    "  size       INTEGER,"
    "  crc32      TEXT,"
    "  md5        TEXT,"
    "  sha1       TEXT"
    ")",

    "INSERT INTO dat_entries_v3"
    " (dat_id, game_name, rom_name, size, crc32, md5, sha1)"
    " SELECT dat_id, COALESCE(game_name, ''), rom_name, size, crc32, md5, sha1"
    " FROM dat_entries",

    "DROP TABLE dat_entries",
    "ALTER TABLE dat_entries_v3 RENAME TO dat_entries",

    "CREATE UNIQUE INDEX idx_dat_entry_key"
    " ON dat_entries(dat_id, game_name, rom_name)",
    "CREATE INDEX idx_dat_crc32 ON dat_entries(crc32)",
    "CREATE INDEX idx_dat_md5   ON dat_entries(md5)",
    "CREATE INDEX idx_dat_sha1  ON dat_entries(sha1)"
};

const int rh_schema_v3_sql_count =
    (int)(sizeof(rh_schema_v3_sql) / sizeof(rh_schema_v3_sql[0]));

/*
 * v4 迁移（DAT 生态调研结论）：
 * - dats.file_sha1：DAT 文件自身 SHA1，用于识别原地替换同名 DAT
 * - dat_entries.entry_type：rom/disk/sample，缺省 rom，v3 全量回填
 * - idx_dat_entries_size_crc32：(size, crc32) 复合索引，先 size 缩小范围
 *   再 hash，提升大 DAT 匹配性能（RomVault 默认走 size+CRC+MD5+SHA1 防碰撞）
 */
const char *const rh_schema_v4_sql[] = {
    "ALTER TABLE dats ADD COLUMN file_sha1 TEXT",

    "ALTER TABLE dat_entries ADD COLUMN entry_type TEXT DEFAULT 'rom'",

    "CREATE INDEX idx_dat_entries_size_crc32"
    " ON dat_entries(size, crc32)"
};

const int rh_schema_v4_sql_count =
    (int)(sizeof(rh_schema_v4_sql) / sizeof(rh_schema_v4_sql[0]));

/*
 * v5：记录命中的 DAT 标准 ROM 名。
 *
 * 导入时若 CRC32 唯一命中 DAT，就把它写进这一列：既能作为「推荐文件名」
 * 展示，也让导出的 --names dat 不必再回查哈希。未匹配的行保持 NULL，
 * 老库升级后不需要重扫才能使用其他功能。
 */
const char *const rh_schema_v5_sql[] = {
    "ALTER TABLE roms ADD COLUMN dat_name TEXT"
};

const int rh_schema_v5_sql_count =
    (int)(sizeof(rh_schema_v5_sql) / sizeof(rh_schema_v5_sql[0]));

/*
 * v6：重复文件识别所需的摘要列与索引。
 *
 * - roms.canon_sha1 保存去掉容器头后的 SHA1，同一内容带不带头都能对上；
 * - media 只有 size，补 crc32/sha1 后媒体与补丁也能查重；
 * - 按 sha1/size 建索引，导入时「同 size 取候选」不必全表扫描。
 */
const char *const rh_schema_v6_sql[] = {
    "ALTER TABLE roms  ADD COLUMN canon_sha1 TEXT",
    "ALTER TABLE media ADD COLUMN crc32 TEXT",
    "ALTER TABLE media ADD COLUMN sha1 TEXT",
    "CREATE INDEX idx_roms_sha1       ON roms(sha1)",
    "CREATE INDEX idx_roms_size       ON roms(size)",
    "CREATE INDEX idx_roms_canon_sha1 ON roms(canon_sha1)",
    "CREATE INDEX idx_media_sha1      ON media(sha1)",
    "CREATE INDEX idx_media_size      ON media(size)"
};

const int rh_schema_v6_sql_count =
    (int)(sizeof(rh_schema_v6_sql) / sizeof(rh_schema_v6_sql[0]));

const char *const rh_schema_fts_sql[] = {
    "CREATE VIRTUAL TABLE games_fts USING fts5("
    "  name, game_main_name, description,"
    "  content='games', content_rowid='rowid',"
    "  tokenize=\"trigram\""
    ")",

    "CREATE TRIGGER games_fts_ai AFTER INSERT ON games BEGIN"
    "  INSERT INTO games_fts(rowid, name, game_main_name, description)"
    "  VALUES (new.rowid, new.name, new.game_main_name, new.description);"
    " END",

    "CREATE TRIGGER games_fts_ad AFTER DELETE ON games BEGIN"
    "  INSERT INTO games_fts(games_fts, rowid, name, game_main_name, description)"
    "  VALUES ('delete', old.rowid, old.name, old.game_main_name, old.description);"
    " END",

    "CREATE TRIGGER games_fts_au AFTER UPDATE ON games BEGIN"
    "  INSERT INTO games_fts(games_fts, rowid, name, game_main_name, description)"
    "  VALUES ('delete', old.rowid, old.name, old.game_main_name, old.description);"
    "  INSERT INTO games_fts(rowid, name, game_main_name, description)"
    "  VALUES (new.rowid, new.name, new.game_main_name, new.description);"
    " END"
};

const int rh_schema_fts_sql_count =
    (int)(sizeof(rh_schema_fts_sql) / sizeof(rh_schema_fts_sql[0]));
