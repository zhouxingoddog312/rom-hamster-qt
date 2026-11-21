# DAT 阶段三规格：标准文件名（schema v5）

状态：已实施

## 1. 目标

DAT 匹配不再只用于回填哈希与来源，还用来规范化 ROM 文件名：

1. schema v5 在 `roms` 上新增 `dat_name`，保存命中 DAT 的标准 ROM 名；
2. 导入时唯一命中 DAT 就记录标准名；
3. 导入确认框提供「命中 DAT 时使用标准文件名（推荐）」，默认勾选；
4. 改名为库内操作的附加步骤，失败或目标已存在时保留来源名，绝不覆盖。

## 2. 设计

- `RhRomFile.dat_name`（可 NULL）；`store.c` 的 roms 读写同步；
- 迁移 v5：`ALTER TABLE roms ADD COLUMN dat_name TEXT`；
- `RhImportExecOptions.name_mode`：
  - `RH_IMPORT_NAMES_ORIGINAL`（0，默认）：保留来源文件名，仅记录 `dat_name`；
  - `RH_IMPORT_NAMES_DAT`（1）：复制后在同一游戏目录内 `g_rename` 为标准名；
- 标准名经 `rh_str_sanitize_component` 处理；含路径分隔符、目标已存在或
  改名失败时保持原名；
- 改名的 ROM 清空 `filename_raw`（原始字节只对应来源名）；
- `RhImportExecStats.renamed` 统计实际改名数；
- GUI 导入确认框用 `QMessageBox::setCheckBox` 提供勾选项，默认勾选。

本阶段不改变游戏目录名（仍由扫描推断），只改 ROM 文件名。

## 3. 验证

- `core/import` 新增 `/import/execute-dat-names`：来源 `Tetris.gb` 命中 DAT 后
  库内文件名与 `dat_name` 均为 `Tetris (World).gb`，来源名不再存在；
- `core/store` 升级断言改为 `RH_SCHEMA_VERSION`（5）；
- 双端构建与全量 ctest 通过。

## 4. 明确不做

- 不引入中文 DAT / 名称翻译表；
- 不改游戏目录名与 m3u 名；
- 不做导入前的逐条改名预览（GUI 只给整体勾选项）。
