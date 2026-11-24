# 重复文件识别与导入处置（方案 B）规格

状态：待批准

## 1. 目标

在不引入图片解码/感知哈希的前提下，用「内容摘要 + 去头摘要」识别库内重复的
ROM、补丁与媒体文件，并在导入时让用户对每个疑似重复项选择：

- 忽略（不导入这一份）
- 共存（改名后保留两份）
- 覆盖（用新文件替换已有文件并更新其摘要）

识别结果只提示，不自动删除任何文件。

## 2. 术语与判定规则

### 2.1 整文件摘要

`sha1`：对文件全部字节计算的 SHA1（小写十六进制），用于媒体与补丁。

### 2.2 去头摘要 `canon_sha1`

部分主机的 ROM 带固定长度的容器头，同一内容因是否带头而摘要不同。计算规则：

| 条件 | 跳过字节 |
| --- | --- |
| 前 4 字节为 `NES\x1A` 且 `size > 16` 且 `(size - 16) % 16384 == 0` | 16 |
| 扩展名（不区分大小写）属于 `.smc .sfc .fig .swc .bs` 且 `size > 512` 且 `size % 32768 == 512` | 512 |
| 其他 | 0 |

`canon_sha1` = 跳过上述字节后的 SHA1；跳过量 0 时与 `sha1` 相同。

### 2.3 重复判定键

- ROM：`COALESCE(NULLIF(canon_sha1,''), sha1)`。
- 媒体：`sha1`。
- 补丁（`media.kind='patcher'`）：归入媒体，按 `sha1` 判定。

不要求 `size` 相同：带头与不带头 ROM 的大小天然相差 16/512 字节，正是去头摘要
要消除的差异。`size` 只用于展示与估算浪费，簇内取最大值。

默认按平台隔离比较；CLI 不传 `--platform` 时跨平台比较。

## 3. schema v6

修改 [store-schema.h](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/store-schema.h)：

- `RH_SCHEMA_VERSION` 改为 `6`；
- 新增 `rh_schema_v6_sql[]` / `rh_schema_v6_sql_count`。

DDL：

```sql
ALTER TABLE roms  ADD COLUMN canon_sha1 TEXT;
ALTER TABLE media ADD COLUMN crc32 TEXT;
ALTER TABLE media ADD COLUMN sha1 TEXT;
CREATE INDEX idx_roms_sha1       ON roms(sha1);
CREATE INDEX idx_roms_size       ON roms(size);
CREATE INDEX idx_roms_canon_sha1 ON roms(canon_sha1);
CREATE INDEX idx_media_sha1      ON media(sha1);
CREATE INDEX idx_media_size      ON media(size);
```

修改 [store.c](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/store.c)：

- `apply_migrations()` 增加 `version < 6` 分支；
- `insert_roms()` 的 INSERT 增加 `canon_sha1` 列（`RhRomFile.canon_sha1`）；
- `insert_media()` 的 INSERT 增加 `crc32`、`sha1` 列（`RhMediaFile` 新字段）；
- `store_roms_for_game()` 读取 `canon_sha1`；`store_media_for_game()` 读取
  `crc32`、`sha1`。

## 4. rh-hash：支持跳过文件头

修改 [rh-hash.h](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/rh-hash.h) /
[rh-hash.c](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/rh-hash.c)：

- `RhHashOptions` 增加 `guint64 skip_bytes;`（0 = 整个文件）。
- `hash_one()` 在读取前 `fseek(fp, skip_bytes, SEEK_SET)`；失败返回 `RH_ERR_IO`。
- `skip_bytes > 0 && skip_bytes >= 文件大小` 返回 `RH_ERR_INVALID`。
- `out->size` 为实际参与哈希的字节数（`st_size - skip_bytes`）。
- 进度回调的 `total` 为 `st_size - skip_bytes`，`processed` 从 0 开始累计。
- 其余语义（取消、批量、失败清零）不变。

## 5. 记录结构

修改 [rh-records.h](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/rh-records.h) /
[rh-records.c](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/rh-records.c)：

```c
/* RhRomFile 新增 */
gchar *canon_sha1;   /* 去头 SHA1；未计算为 NULL */

/* RhMediaFile 新增 */
gchar *crc32;
gchar *sha1;

typedef struct {
    gchar   *game_id;
    gchar   *game_name;
    gchar   *platform;
    gchar   *dir_path;        /* games.dir_path，相对库根 */
    gchar   *filename;        /* roms/media 的相对文件名 */
    gchar   *kind;            /* 媒体类型；ROM 为 NULL */
    gint64   size;
    gchar   *sha1;            /* 整文件 SHA1 */
    gchar   *canon_sha1;      /* ROM 去头 SHA1，可 NULL */
    GBytes  *dir_path_raw;    /* 非 UTF-8 路径定位用，可 NULL */
    GBytes  *filename_raw;
    gboolean missing;         /* 磁盘上不存在；由 dedup 层填写 */
} RhDupMember;

typedef struct {
    gchar     *kind;          /* "rom" 或 "media" */
    gchar     *key;           /* 判定键（sha1/canon_sha1） */
    gint64     size;
    GPtrArray *members;       /* RhDupMember* */
} RhDupGroup;

void rh_dup_member_free(RhDupMember *member);
void rh_dup_member_free_func(gpointer data);
void rh_dup_group_free(RhDupGroup *group);
void rh_dup_group_free_func(gpointer data);
```

`RhRomFile` / `RhMediaFile` 的释放函数同步释放新字段。

## 6. store 新增 API

在 [store.h](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/store.h) 声明、
[store.c](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/store.c) 实现：

```c
/* 重复簇：返回 GPtrArray<RhDupGroup*>，调用方 g_ptr_array_unref。 */
GPtrArray *store_roms_dup_groups(RhStore *store, const char *platform,
                                 RhErr *out_err);
GPtrArray *store_media_dup_groups(RhStore *store, const char *platform,
                                  RhErr *out_err);

/* 待补算摘要：返回 GPtrArray<RhDupMember*>。 */
GPtrArray *store_roms_missing_hash(RhStore *store, const char *platform,
                                   RhErr *out_err);
GPtrArray *store_media_missing_hash(RhStore *store, const char *platform,
                                    RhErr *out_err);

/* 导入查重候选：同平台、sha1 或 canon_sha1 命中的 ROM。 */
GPtrArray *store_roms_by_hash(RhStore *store, const char *platform,
                              const char *hash, RhErr *out_err);

/* 导入/补算回写。crc32/canon_sha1 可为 NULL；size < 0 表示不改 size。 */
RhErr store_rom_set_hashes(RhStore *store, const char *game_id,
                           const char *filename, gint64 size,
                           const char *crc32, const char *sha1,
                           const char *canon_sha1);
RhErr store_media_set_hash(RhStore *store, const char *game_id,
                           const char *kind, const char *filename, gint64 size,
                           const char *crc32, const char *sha1);
```

ROM 簇 SQL 要点：

```sql
SELECT COALESCE(NULLIF(r.canon_sha1,''), r.sha1) AS key,
       r.size, g.id, g.name, g.platform, g.dir_path,
       r.filename, r.filename_raw, g.dir_path_raw, r.sha1, r.canon_sha1
FROM roms r JOIN games g ON g.id = r.game_id
WHERE r.size IS NOT NULL
  AND COALESCE(NULLIF(r.canon_sha1,''), NULLIF(r.sha1,'')) IS NOT NULL
  [AND g.platform = ?]
ORDER BY key, g.dir_path, r.filename;
```

在结果集上按 `key` 聚合，只保留 `COUNT(*) > 1` 的簇（在 C 侧聚合，
避免 SQLite 对 NULL/空串表达式的兼容差异）；簇的 `size` 取成员最大值。
媒体簇同理，键为 `m.sha1`，并带出 `m.kind`、`m.crc32`。

`store_roms_missing_hash` 条件：`r.sha1 IS NULL OR r.canon_sha1 IS NULL`；
`store_media_missing_hash` 条件：`m.sha1 IS NULL`。

`store_rom_set_hashes` / `store_media_set_hash` 只更新对应行，不存在的行返回
`RH_ERR_NOT_FOUND`。

## 7. 新模块 dedup

新增 `src/core/dedup.h` / `src/core/dedup.c`，加入 `romham` 源列表。

```c
/* 计算应跳过的容器头字节数；无法 stat 或参数非法返回 FALSE。 */
gboolean rh_dedup_header_skip(const char *abs_path, const char *filename,
                              gint64 size, guint64 *out_skip);

typedef gboolean (*RhDedupProgressFn)(const char *path, guint done,
                                      guint total, gpointer user_data);

typedef struct {
    const char *root;
    const char *platform;        /* NULL = 全部平台 */
    gboolean    include_media;
    gboolean    backfill;        /* 先补算缺失摘要 */
    gint64      min_size;        /* 0 = 不限 */
    RhDedupProgressFn progress;
    gpointer    user_data;
} RhDedupOptions;

typedef struct {
    guint   hashed;
    guint   failed;
    guint   clusters;
    guint   extra_copies;
    gint64  bytes_duplicated;
} RhDedupStats;

RhErr rh_dedup_backfill(RhStore *store, const RhDedupOptions *options,
                        RhDedupStats *out_stats);
RhErr rh_dedup_report(RhStore *store, const RhDedupOptions *options,
                      GPtrArray **out_groups, RhDedupStats *out_stats);

/* 导入查重：abs_path 为待导入文件，root 为库根。无重复时 *out_match = NULL。 */
RhErr rh_dedup_check_file(RhStore *store, const char *root,
                          const char *abs_path, const char *platform,
                          const char *rel_name, RhDupMember **out_match);
```

实现约束：

- `rh_dedup_backfill`：遍历 missing 列表，用
  `rh_fname_native_name()` 还原目录/文件名后 stat；文件不存在则跳过并计入
  `failed`，不写库。计算 `RH_HASH_CRC32 | RH_HASH_SHA1`，再用
  `rh_dedup_header_skip()` 计算 `canon_sha1`（跳过量 > 0 时二次读盘）。
  ROM 调 `store_rom_set_hashes()`，媒体调 `store_media_set_hash()`。
  进度回调返回 FALSE 时返回 `RH_ERR_CANCELLED`。
- `rh_dedup_report`：`options->backfill` 为 TRUE 时先补算；再取 ROM 簇，
  `include_media` 时并入媒体簇；过滤 `size < min_size`；对每个成员 stat，
  不存在则置 `missing`。`extra_copies = Σ(members-1)`，
  `bytes_duplicated = Σ(size × (members-1))`。
- `rh_dedup_check_file`：stat → 算源文件 `sha1`/`canon_sha1` →
  `store_roms_by_hash()` 按 canon 与整文件 sha1 各查一次（合并去重）→
  与候选 `COALESCE(canon_sha1, sha1)` 比较，其次比 `sha1`。跳过与
  `abs_path` 指向同一文件的候选（`g_canonicalize_filename` 比较）。命中时
  复制成员并填 `missing=FALSE`，同时由调用方补充 `same_name` / `similarity`。
- 不链接 Qt；不打印；SQL 只出现在 store。

## 8. import：导入前查重与三种处置

修改 [import.h](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/import.h) /
[import.c](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/core/import.c)。

```c
typedef enum {
    RH_DUP_IGNORE = 0,
    RH_DUP_COEXIST = 1,
    RH_DUP_OVERWRITE = 2
} RhDupAction;

typedef struct {
    guint        rom_index;        /* entry->roms 下标 */
    gchar       *sha1;
    gchar       *existing_game_id;
    gchar       *existing_rel_path;/* "<dir_path>/<filename>" */
    gchar       *existing_name;    /* 已有游戏显示名 */
    gint64       size;
    gboolean     same_name;
    int          similarity;       /* 0-100，rh_str_similarity */
    RhDupAction  action;           /* 默认 RH_DUP_IGNORE */
} RhImportDup;

/* RhImportPlanEntry 新增 */
GPtrArray *dups;   /* RhImportDup*，rh_import_entry_free 释放 */

RhErr rh_import_plan_detect_duplicates(RhImportPlan *plan, RhStore *store,
                                       const char *root,
                                       const RhPlatformRules *rules,
                                       RhErr *out_err);
gboolean rh_import_dup_set_action(RhImportPlanEntry *entry, guint rom_index,
                                  RhDupAction action);
```

检测规则：

- 跳过 `is_patch_bundle`、平台未定、`roms->len == 0` 的条目；
- 对每个 ROM 用 `entry->roms_raw[i]` 还原来源路径，调用
  `rh_dedup_check_file()`；`rel_name` 传 `entry->roms[i]`；
- 命中后填 `same_name`（文件名相同）与 `similarity`（`rh_str_similarity`，
  比较去掉扩展名后的名字）；
- 单个文件探测失败只记录跳过，不中断整批；仅参数错误/数据库错误返回非
  `RH_OK`；
- 重复检测不修改计划其他字段，可重复调用（先清空旧 `dups`）。

执行语义（`execute_entry()`）：

- `RH_DUP_IGNORE`：不复制、不写 `roms` 行；`stats.dup_ignored++`。若条目所有
  ROM 都被忽略且没有媒体/补丁，整条记为 skipped（消息“重复 ROM 已忽略”）。
- `RH_DUP_COEXIST`：若目标同名文件已存在，在同一游戏目录内选择
  `"<stem> (2).<ext>"`、`(3)…` 直到不冲突；`rom->filename` 使用新名，
  正常写库；`stats.dup_coexisted++`。此时不再执行 DAT 改名。
- `RH_DUP_OVERWRITE`：复制到 `existing_rel_path`（跨游戏目录也允许），
  用 `store_rom_set_hashes()` 更新已有 ROM 行；不插入本条目 ROM 行；
  `stats.dup_overwritten++`。覆盖前先把新文件写到 `.romham-part` 临时名，
  再删除旧文件并原子改名；失败时尽量保留旧文件并记该条目 failed。
- 复制 ROM 时同时计算 `sha1` 与 `canon_sha1`（`copy_one()` 增加
  `GChecksum`，按 `rh_dedup_header_skip()` 决定是否累加前 N 字节）；
  `crc32` 仍按整文件计算，保持 DAT 匹配语义。
- 复制媒体/补丁时同时计算 `sha1` 与 `crc32`，写入 `RhMediaFile` 新字段。
- `RhImportExecStats` 新增 `dup_ignored`、`dup_coexisted`、`dup_overwritten`。

## 9. CLI `dedup`

新增 `src/cli/cmd-dedup.c`，并在 [main.c](file:///c:/Users/zhoux/Desktop/两端通用的rom管理程序/src/cli/main.c)
注册：

- 声明 `int rh_cmd_dedup(int argc, char **argv);`
- 子命令分发 `dedup`；
- `--help` 的命令列表增加 `dedup  Find duplicate ROM/media files`。

选项：

```
--db PATH        数据库文件（默认 ROM_HAMSTER_DB 或 ./romhamster.db）
--root DIR       库根目录（默认 ROM_HAMSTER_ROOT）
--platform ID    只检查一个平台
--media          同时检查媒体与补丁
--backfill       先补算缺失的 sha1/canon_sha1
--min-size BYTES 忽略小于该大小的文件（默认 0）
```

输出（UTF-8）：

```
[rom] <key 前 12 位>  size=<n>  copies=<n>  wasted=<bytes>
  <dir_path>/<filename>   <game_name>
  ...
[media] <key 前 12 位>  size=<n>  copies=<n>  wasted=<bytes>
  <kind> <dir_path>/<filename>   <game_name>
```

汇总一行：`Checked N files: X duplicate clusters, Y extra copies, Z bytes wasted.`
`--backfill` 时先输出 `Backfilled N hashes (M failed/missing).`

退出码约定：`0` 无重复；`1` 扫描成功且发现重复；`2`（或更大）执行失败——
参数错误、`--root` 不存在或不是目录、库打不开、数据库读写失败、IO 失败、取消等。
`1` 只用于“成功完成并发现重复”，任何硬失败都不得返回 1。
`--backfill` 中个别文件缺失或不可读按 `failed/missing` 计数，不中断扫描、不改变
0/1 语义；写库失败属于硬失败，返回 2。
`--root` 缺失时直接报错退出（重复检测需要读取文件）。

## 10. GUI

在 core/CLI 全部通过后实施：

1. `window-main` 增加菜单动作 `查找重复…`，打开新增
   `src/gui/dup-dialog.h/.cpp`：
   - 顶部平台下拉、`包含媒体`、`最小大小`；
   - `计算缺失哈希` 按钮在后台线程跑 `rh_dedup_backfill`；
   - 表格按簇展示 key、类型、大小、份数、浪费字节与成员路径；
   - 只读，不提供删除。
2. `chooseAndImport()`：
   - `rh_import_plan_set_root()` 之后调用
     `rh_import_plan_detect_duplicates()`；
   - 存在重复时弹出 `ImportDupDialog`：逐项显示来源 ROM、已有路径、大小、
     相似度，单选 忽略/共存/覆盖，支持「应用到全部」；
   - 用户确认后调用 `rh_import_dup_set_action()`，再启动
     `ImportExecThread`；
   - 取消对话框则终止本次导入。
3. `CMakeLists.txt` 的 GUI 源列表加入新文件；
   `i18n/rom-hamster_zh_CN.ts` 补中文。

## 11. 测试

新增 `tests/unit/test-dedup.c`（加入 CMake，链接 `romham` + GLIB2 + SQLITE3）：

- `header_skip`：NES 16 字节头、SNES 512 字节头、非整倍数不跳、扩展名不匹配
  不跳；
- `hash_skip`：`skip_bytes` 后结果等于尾部内容摘要，`out->size` 正确，
  `skip_bytes >= size` 返回 `RH_ERR_INVALID`；
- `backfill`：写入无摘要行 → 建临时文件 → 补算后 `sha1`/`canon_sha1` 非空，
  不存在文件计入 failed；
- `report`：构造两份同内容 ROM（一份带头、一份不带头）→ 同簇；
  两份同内容媒体 → 媒体簇；`min_size` 过滤生效；
- `check_file`：源文件与库内文件同内容（带头差异）命中，不同内容不命中；
- `import dup`：构造计划 + 已有记录，验证默认 IGNORE、设置 COEXIST 后
  生成 `(2)` 文件名、OVERWRITE 后更新原行且不新增 ROM 行。

修改 `tests/unit/test-hash.c` 覆盖 `skip_bytes`；`tests/unit/test-store.c`
增加 v6 迁移与新列读回断言。

## 12. i18n

`po/zh_CN.po` 为新增 CLI 文案补中文（`dedup` 帮助、输出标签、错误），
无 fuzzy；保持 `msgfmt --check` 通过。

## 13. 验证步骤

```bash
# 构建 + 单元测试
wsl -e bash -lc "cd /mnt/c/Users/zhoux/rhwork && \
  cmake --build build/linux-cli -j && \
  ctest --test-dir build/linux-cli --output-on-failure"

# 翻译校验
wsl -e bash -lc "cd /mnt/c/Users/zhoux/rhwork && \
  msgfmt --check -o /dev/null po/zh_CN.po"

# 手工冒烟：构造一个含重复 ROM 的临时库
#  1) 用现有 CLI 建库/导入两个同内容不同名文件（或直接写测试库）
#  2) rom-hamster dedup --root <dir> --media --backfill
#  3) 确认退出码 1、输出两个成员路径
```

GUI 阶段额外验证：导入重复文件时弹窗出现，三种选择分别产生预期结果；
`查找重复…` 能列出簇并完成补算。

## 14. 明确不做

- 感知哈希（pHash/dHash）与图片解码；
- 压缩包内部条目 CRC 级查重（当前按容器整文件比较）；
- 自动删除重复文件；
- 导入时对媒体/补丁弹重复处置（仍沿用“目标存在则跳过”，由 dedup 报告兜底）；
- 跨平台自动合并（默认按平台隔离，CLI 可显式不传 `--platform`）。

## 15. 实施状态

已按本规格实施，核心、CLI、GUI 与测试均已完成；实施中发现「去头后大小不同」
不能作为重复条件，判定键已改为纯摘要键（见 §2.3）。
