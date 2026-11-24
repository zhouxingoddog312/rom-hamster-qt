/*
 * rh-records.h —— 领域记录结构（ARCHITECTURE.md §3.2）
 *
 * 与 9 张表一一对应，字段名与列名一致。纯数据 + 释放函数，
 * 不含 SQL、不打印、不持有全局状态。
 */
#ifndef RH_RECORDS_H
#define RH_RECORDS_H

#include <glib.h>

/* media.kind 的 7 个取值，顺序与 DDL 的 CHECK 一致 */
extern const char *const rh_media_kind_names[7];

gboolean rh_media_kind_valid(const char *kind);

typedef struct {
    gchar *id;              /* 8 位小写字母数字 */
    gchar *platform;        /* 规范平台 ID，如 "gb" */
    gchar *name;            /* 前端显示名 */
    gchar *game_main_name;  /* 命名基准 */
    gchar *dir_path;        /* "<platform>/<游戏目录>"，内部用 '/' */
    GBytes *dir_path_raw;   /* dir_path 的原始字节；还原非 UTF-8 路径用，可 NULL */
    gchar *developer;
    gchar *publisher;
    gint   year;            /* 0 表示 NULL */
    gchar *genre;
    gint   players;         /* 0 表示 NULL */
    gdouble rating;         /* 0.0 表示 NULL */
    gchar *region;
    gchar *description;
    gboolean is_multi_disc;
    gboolean is_arcade;
    gchar *rom_format;      /* raw/zip/7z/chd/cso/wbfs */
    gchar *rom_status;      /* good/baddump/unknown */
    gchar *dat_source;
    gchar *patch_type;      /* ""/generic/platform/mixed */
    gchar *created_at;
    gchar *updated_at;
} RhGame;

typedef struct {
    gchar   *filename;      /* 相对游戏目录的文件名 */
    GBytes  *filename_raw;  /* 同一路径的原始字节，可 NULL */
    gint     disc_no;       /* 0 表示 NULL */
    gboolean has_size;
    gint64   size;
    gchar   *crc32;
    gchar   *md5;
    gchar   *sha1;
    gchar   *canon_sha1;    /* 去头 SHA1；未计算为 NULL（v6） */
    gint64   dat_id;        /* 0 表示 NULL：本条 ROM 的匹配来自哪个 DAT */
    gchar   *dat_name;      /* 命中的 DAT 标准 ROM 名（v5），未匹配为 NULL */
    gint     sort;
} RhRomFile;

/* dat_entries 的一行。rom 名只在 game 内唯一，跨 game 可重复。 */
typedef struct {
    gchar   *entry_type;    /* rom / disk / sample；NULL 视为 rom（v4 新增） */
    gchar   *game_name;     /* 所属 game；缺失时为空串 */
    gchar   *rom_name;
    gchar   *status;        /* good/baddump/nodump/verified，可 NULL */
    gchar   *cloneof;       /* MAME 结构：父 game，可 NULL */
    gchar   *romof;         /* 共用 ROM 的 game，可 NULL */
    gchar   *merge_name;    /* 与父 game 共用的 rom 名，可 NULL */
    gboolean has_size;
    gint64   size;
    gchar   *crc32;         /* 小写十六进制，可 NULL */
    gchar   *md5;
    gchar   *sha1;
} RhDatEntry;

/* 按哈希查询的匹配视图（dat_entries JOIN dats），不是表。 */
typedef struct {
    gint64      dat_id;
    RhDatEntry *entry;
    gchar      *dat_name;
    gchar      *dat_version;
    gchar      *dat_path;
    gchar      *platform;
} RhDatMatch;

typedef struct {
    gchar   *kind;          /* 见 rh_media_kind_names */
    gchar   *filename;
    GBytes  *filename_raw;  /* 同一路径的原始字节，可 NULL */
    gboolean has_size;
    gint64   size;
    gchar   *crc32;         /* v6：整文件 CRC32 */
    gchar   *sha1;          /* v6：整文件 SHA1 */
    gint     sort;
} RhMediaFile;

/* 重复簇里的一个文件。ROM 与媒体共用，kind 区分来源。 */
typedef struct {
    gchar   *game_id;
    gchar   *game_name;
    gchar   *platform;
    gchar   *dir_path;      /* games.dir_path，相对库根 */
    gchar   *filename;      /* roms/media 的相对文件名 */
    gchar   *kind;          /* 媒体类型；ROM 为 NULL */
    gint64   size;
    gchar   *sha1;          /* 整文件 SHA1 */
    gchar   *canon_sha1;    /* ROM 去头 SHA1，可 NULL */
    GBytes  *dir_path_raw;  /* 非 UTF-8 路径定位用，可 NULL */
    GBytes  *filename_raw;
    gboolean missing;       /* 磁盘上不存在；由 dedup 层填写 */
} RhDupMember;

typedef struct {
    gchar     *kind;        /* "rom" 或 "media" */
    gchar     *key;         /* 判定键（sha1/canon_sha1） */
    gint64     size;
    GPtrArray *members;     /* RhDupMember* */
} RhDupGroup;

/* 默认值按 DDL：rom_format=raw、rom_status=unknown、patch_type=""、布尔为 FALSE */
RhGame *rh_game_new(void);
void    rh_game_free(RhGame *game);
void    rh_game_free_func(gpointer data);

RhRomFile *rh_rom_file_new(void);
void       rh_rom_file_free(RhRomFile *rom);
void       rh_rom_file_free_func(gpointer data);

RhMediaFile *rh_media_file_new(void);
void         rh_media_file_free(RhMediaFile *media);
void         rh_media_file_free_func(gpointer data);

RhDupMember *rh_dup_member_new(void);
void         rh_dup_member_free(RhDupMember *member);
void         rh_dup_member_free_func(gpointer data);
void         rh_dup_group_free(RhDupGroup *group);
void         rh_dup_group_free_func(gpointer data);

RhDatEntry *rh_dat_entry_new(void);
void        rh_dat_entry_free(RhDatEntry *entry);
void        rh_dat_entry_free_func(gpointer data);

void        rh_dat_match_free(RhDatMatch *match);
void        rh_dat_match_free_func(gpointer data);

#endif /* RH_RECORDS_H */
