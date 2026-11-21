# DAT 阶段四规格：导出文件名切换

状态：已实施

## 1. 目标

导出前端清单时可以选择 ROM 文件名：

1. `original`：库内原文件名（默认，行为不变）；
2. `dat`：命中的 DAT 标准名（通常是英文 No-Intro/Redump 名）；
3. `display`：游戏显示名（通常为中文）+ 原后缀，多碟带 `(Disc N)`。

复制文件、清单里的 `rom_file`/`rom_rel`、m3u 内容使用同一个名字，
源文件仍按库内文件名读取。

## 2. 设计

- `export.h`：
  ```c
  typedef enum {
      RH_EXPORT_NAMES_ORIGINAL = 0,
      RH_EXPORT_NAMES_DAT = 1,
      RH_EXPORT_NAMES_DISPLAY = 2
  } RhExportNameMode;
  ```
  `RhExportOptions.name_mode`。
- `export.c` 的 `export_rom_name()`：
  - `dat` 且 `dat_name` 非空：直接使用；
  - `display` 且单 ROM 或多碟（`disc_no > 0`）：`游戏显示名` + 原后缀，
    多碟追加 ` (Disc N)`；
  - 其余情况（含多文件街机包）回落到库内文件名，避免重名覆盖。
- CLI：
  ```text
  rom-hamster export ... [--names original|dat|display]
  ```
  非法取值报错并退出 1。
- m3u 内容按导出名逐行写入；`--m3u` 仍只在多碟游戏时生成。

## 3. 验证

- `core/export` 新增 `/export/dat-names`：`Tetris.gb` + `dat_name =
  Tetris (World).gb`，`--names dat` 后输出目录只有标准名文件；
- 既有 esde/pegasus 黄金文件在默认模式下不变；
- 双端构建与 ctest 通过。

## 4. 明确不做

- 不提供 GUI 导出界面；
- 不做文件名冲突自动编号；
- 不重命名媒体文件（仍按前端规格）。
