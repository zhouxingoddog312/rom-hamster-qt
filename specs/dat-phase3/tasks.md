# DAT 阶段三任务清单

依据：[spec.md](spec.md)。

## [x] 1. schema v5 与记录

- `store-schema.h`：`RH_SCHEMA_VERSION 5`，声明 `rh_schema_v5_sql`。
- `store-schema.c`：`ALTER TABLE roms ADD COLUMN dat_name TEXT`。
- `store.c`：迁移到 5；roms 写入/读取增加 `dat_name`。
- `rh-records.h/.c`：`RhRomFile.dat_name` 与释放。

## [x] 2. 导入命名策略

- `import.h`：`RhImportNameMode`、`options.name_mode`、`stats.renamed`。
- `import.c`：唯一命中时写 `dat_name`；DAT 模式复制后安全改名。
- `tests/unit/test-import.c`：原用例断言 `dat_name`；新增 execute-dat-names。
- `tests/unit/test-store.c`：版本断言更新为 5。

## [x] 3. GUI

- `window-main.cpp`：导入确认框新增勾选项（默认勾选）。
- `import-job.h/.cpp`：线程参数与 `options.name_mode` 传递。

## [x] 4. 验证与文案

- Linux/Windows 构建、全量 ctest 通过。
- `po/zh_CN.po` 与 `i18n/rom-hamster_zh_CN.ts` 补中文。
