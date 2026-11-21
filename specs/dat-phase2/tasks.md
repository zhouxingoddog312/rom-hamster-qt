# DAT 阶段二任务清单

依据：[spec.md](spec.md)。

## [x] 1. core：缓存目录

- `rh-path.h/.c` 新增 `rh_path_dat_cache_dir()`。

## [x] 2. core：下载模块

- 新增 `src/core/dat-fetch.h/.c`：镜像回退、`.part` 原子替换、sha1 校验、
  `check_only` / `force`、进度与取消。
- `CMakeLists.txt`：`RH_DAT_FETCH`（默认 ON）、`pkg_check_modules(CURL)`、
  链接与 `RH_DAT_FETCH=1`。

## [x] 3. tests：file:// 单测

- 新增 `tests/unit/test-dat-fetch.c`：安装/未变、检查后更新、sha1 不匹配、
  镜像回退、取消；注册 `core/dat-fetch`。

## [x] 4. CLI：dat update

- `src/cli/cmd-dat.c`：`--platform/--file/--dir/--check/--force/--import/--db`；
  每平台结果行与汇总行；`--import` 复用 `rh_dat_import`。
- `src/cli/main.c`：帮助文本改为 “Import, fetch and inspect DAT indexes”。

## [x] 5. i18n 与打包

- `po/zh_CN.po`：新增文案中文翻译，无 fuzzy/untranslated。
- `packaging/debian/control`：Build-Depends 增加 `libcurl4-openssl-dev`。

## [x] 6. 验证

- Linux：构建 + `ctest` 23/23；`dat update` 手工闭环（更新、已最新、检查、导入）。
- Windows：`cmake --build build/win-gui` + `ctest` 22/22。

## 约定

- 不推送；阶段三/四不在本清单内。
