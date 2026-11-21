# DAT 阶段四任务清单

依据：[spec.md](spec.md)。

## [x] 1. core/export

- `export.h`：`RhExportNameMode` 与 `options.name_mode`。
- `export.c`：`export_rom_name()`；复制、`rom_rel`、m3u 统一使用导出名。

## [x] 2. CLI

- `cmd-export.c`：`--names original|dat|display`，非法值报错。

## [x] 3. 测试与文案

- `tests/unit/test-export.c`：`/export/dat-names`。
- `po/zh_CN.po`：`--names` 帮助与错误文案中文。

## [x] 4. 验证

- Linux/Windows 构建与全量 ctest 通过；默认模式黄金文件不变。
