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
