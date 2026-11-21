# DAT 阶段一任务清单

依据：[spec.md](spec.md)。用户确认后按以下顺序执行；每项完成后立即验证，失败不进入下一项。

## [x] 1. core：列出已导入 DAT

- 修改 `src/core/store.h`：新增 `RhDatInfo`、`rh_dat_info_free`、`store_dat_list`。
- 修改 `src/core/store.c`：在 DAT 区段末尾实现；SQL
  `SELECT id, platform, path, name, version, encoding, imported_at, entry_count, skipped, file_sha1 FROM dats ORDER BY platform, name, path;`
  空结果返回空数组且 `RH_OK`，`store == NULL` 返回 `RH_ERR_INVALID`。
- 验收：`cmake --build build/linux-cli` 通过；无新增告警。

## [x] 2. core：共用数据目录候选

- 修改 `src/core/rh-path.h`：声明 `GPtrArray *rh_path_data_dir_candidates(void);`。
- 修改 `src/core/rh-path.c`：实现可执行文件目录（POSIX `/proc/self/exe`；
  Windows `GetModuleFileNameW`），按 spec 3.3 顺序收集、去重，返回带 `g_free` 的数组。
- 修改 `src/gui/main.cpp`：`findDataFile()` 改为遍历该候选数组并检查文件存在；
  删除 GUI 自己的候选拼接与 `RH_DATA_DIR` 宏兜底。
- 修改 `CMakeLists.txt`：`RH_DATA_DIR` 编译定义从 `rom-hamster-gui` 移到 `romham`。
- 验收：Linux 构建通过；`build/linux-cli/rom-hamster --version` 正常；
  在源码树中 `rh_path_data_dir_candidates()` 最终能命中 `data/`。

## [x] 3. core：清单解析器 + 单测

- 新增 `src/core/dat-source.h` / `src/core/dat-source.c`：
  - `RhDatSource`（platform/name/file/urls/sha1）；
  - `rh_dat_sources_load(path, &sources, &message)`：实现 spec 3.1 的全部校验，
    错误信息形如 `路径:行号: 描述`；`spec_version > 1` 返回 `RH_ERR_INVALID`；
  - `rh_dat_source_free`；
  - `rh_dat_sources_default_path()`：实现 spec 3.2 顺序。
- 新增 `tests/unit/test-dat-source.c`：覆盖合法解析（多 URL、sha1）、重复平台、
  非法文件名（含 `/`）、非法 URL、`spec_version=2`、空段等。
- 修改 `CMakeLists.txt`：把 `dat-source.c` 加入 `romham`，新增 `test-dat-source` 并注册测试。
- 验收：`ctest --test-dir build/linux-cli -R dat-source --output-on-failure` 通过。

## [x] 4. data：默认清单与安装

- 新增 `data/dats/sources.conf`：文件头写明格式、覆盖顺序、镜像说明与未收录 DAT 名单；
  写入 spec 3.1 的 27 个平台段，每条两个镜像（raw.githubusercontent.com、ghfast.top）。
- 修改 `CMakeLists.txt`：安装 `data/dats` 到 `${CMAKE_INSTALL_DATADIR}/rom-hamster/dats`。
- 验收：解析器单测额外读取真实 `sources.conf` 成功（在测试中加入 `RH_TEST_DATA_DIR`
  断言：27 条、平台 ID 无重复）；`dat sources` 输出与文件一致。

## [x] 5. CLI：dat 子命令

- 新增 `src/cli/cmd-dat.c`：`rh_cmd_dat` 及 import/list/sources 三个子命令，
  选项与输出按 spec 3.4；错误统一走 `rh_cli_error`；退出码与现有命令一致（0/1/2）。
- 修改 `src/cli/main.c`：声明并分发 `dat`，帮助文本增加 `dat` 一行。
- 修改 `CMakeLists.txt`：`cmd-dat.c` 加入 CLI 目标。
- 验收：`dat sources --explain`、`dat import`、`dat list` 手工验证通过（见 spec 5.3、5.4）。

## [x] 6. 双端构建与回归

- Linux：`cmake -S . -B build/linux-cli -DRH_GUI=OFF -DBUILD_TESTING=ON`，
  构建后跑全量 `ctest`。
- Windows：`$env:PATH="C:\Users\zhoux\msys64\mingw64\bin;$env:PATH"`，
  `cmake --build build/win-gui -j`，再跑可用的 `ctest`。
- 修复所有新增告警；确认 `core/no-gui-linkage` 仍通过（核心未引入 Qt）。
- 验收：两端构建成功；Linux 全量测试通过；Windows 构建产物可启动（`--version`）。

## [x] 7. i18n 与提交

- 若 WSL 有 `xgettext`/`msgmerge`：从 `src/cli/*.c` 提取新文案并合并进 `po/zh_CN.po`，
  补写中文；否则手工向 `po/zh_CN.po` 追加新增 msgid，保持既有条目不变。
- 重新配置构建，确认 `msgfmt` 编译 `.mo` 无警告。
- 按仓库习惯本地分次提交（建议：core 清单/store → CLI 命令 → 数据与 i18n），
  **不推送**。
- 验收：`git status` 只剩预期文件；提交信息说明阶段一范围与未做事项。

## 阻塞与约定

- 实机验收延期，用户不在实机旁；本轮以构建与自动测试为准。
- 阶段二（libcurl 下载）与阶段三/四（schema v5、命名切换）不在本清单内。
- 未经确认不开始上述阶段之外的改动。
