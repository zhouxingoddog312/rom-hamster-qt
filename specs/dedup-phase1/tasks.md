# 重复文件识别（方案 B）任务清单

依据：[spec.md](spec.md)。状态：已完成。

## [x] 1. schema v6

- `src/core/store-schema.h`：`RH_SCHEMA_VERSION` → 6，新增 `rh_schema_v6_sql`。
- `src/core/store.c`：`apply_migrations()` 增加 `version < 6`；`insert_roms` /
  `insert_media` 写新列；`store_roms_for_game` / `store_media_for_game` 读新列。

## [x] 2. rh-hash 跳过文件头

- `src/core/rh-hash.h/.c`：`RhHashOptions.skip_bytes`、`fseek`、进度总量与
  `out->size` 语义、非法跳过量返回 `RH_ERR_INVALID`。

## [x] 3. 记录与释放

- `src/core/rh-records.h/.c`：`RhRomFile.canon_sha1`、`RhMediaFile.crc32/sha1`、
  `RhDupMember` / `RhDupGroup` 及 free 函数。

## [x] 4. store 查询与回写 API

- `store_roms_dup_groups`、`store_media_dup_groups`
- `store_roms_missing_hash`、`store_media_missing_hash`
- `store_roms_by_size`
- `store_rom_set_hashes`、`store_media_set_hash`

## [x] 5. dedup 模块

- 新增 `src/core/dedup.h/.c`：`rh_dedup_header_skip`、`rh_dedup_backfill`、
  `rh_dedup_report`、`rh_dedup_check_file`。
- `CMakeLists.txt` 的 `romham` 源列表加入 `dedup.c`。

## [x] 6. import 查重与处置

- `src/core/import.h/.c`：`RhDupAction`、`RhImportDup`、`entry->dups`、
  `rh_import_plan_detect_duplicates`、`rh_import_dup_set_action`。
- `execute_entry`：IGNORE / COEXIST / OVERWRITE 语义；复制时算 sha1/canon。
- `RhImportExecStats` 增加三个计数。

## [x] 7. CLI dedup

- 新增 `src/cli/cmd-dedup.c`，注册到 `src/cli/main.c` 与帮助文案。
- `CMakeLists.txt` CLI 源列表加入 `cmd-dedup.c`。
- 退出码 0/1/2；`po/zh_CN.po` 补中文。

## [x] 8. 单元测试

- 新增 `tests/unit/test-dedup.c`，`CMakeLists.txt` 注册 `test-dedup`。
- `test-hash.c` 覆盖 `skip_bytes`；`test-store.c` 覆盖 v6 迁移与新列。

## [x] 9. 构建与验证

- `cmake --build build/linux-cli -j` 通过。
- `ctest --test-dir build/linux-cli --output-on-failure` 全绿。
- `msgfmt --check po/zh_CN.po` 通过。
- 临时库冒烟：`rom-hamster dedup --root <dir> --media --backfill` 能报簇。

## [x] 10. GUI（core/CLI 通过后）

- `src/gui/dup-dialog.h/.cpp`：重复列表 + 补算按钮 + 后台线程。
- `window-main`：`查找重复…` 菜单；导入前重复确认对话框。
- `CMakeLists.txt` GUI 源列表；`i18n/rom-hamster_zh_CN.ts` 补中文。
- 手工验证三种处置结果。
