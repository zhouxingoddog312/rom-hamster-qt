# DAT 阶段一规格：清单、入口与共用数据目录

状态：待用户确认后实施

## 1. 目标

补齐 DAT 能力的“可发现、可导入、可查看”闭环，为阶段二的自动下载打基础：

1. 随程序发布一份独立的 DAT 源清单 `data/dats/sources.conf`，用户可覆盖、可改；
2. CLI 增加 `dat import`，让已实现的 `rh_dat_import` 有正式入口；
3. CLI 增加 `dat list`，查看库内已导入的 DAT；
4. CLI 增加 `dat sources`，展示清单内容与生效路径；
5. 新增 CLI/GUI 共用的数据目录解析，消除 GUI 里重复的路径猜测逻辑。

本阶段不下载、不联网；联网下载（libcurl，CLI/GUI 共用同一套依赖）属于阶段二。
GUI 的版本检查继续使用 Qt Network，不受影响。

## 2. 现状

- `src/core/dat.c` 已实现 Logiqx DAT 流式解析、编码判定、哈希匹配；
- `store` 已有 `store_dat_begin/entries_add/finish/abort/count/lookup/find_by_hash`，
  但没有“列出已导入 DAT”的 API；
- CLI 只在测试里调用过 `rh_dat_import`，`main.c` 没有 `dat` 命令；
- 项目没有配置层；`dat_dir`、`dat_match_hash`、`dat_auto_update` 仅存在于 ARCHITECTURE；
- GUI 的 `findDataFile()` 自带一套“环境变量 → 安装布局 → 源码树”的查找逻辑，
  CLI 没有等价实现。

## 3. 设计决策

### 3.1 清单文件格式（spec_version = 1）

`data/dats/sources.conf` 为 INI 风格文本，UTF-8，`#` 开头为注释：

```ini
[meta]
spec_version=1

[nes]
name=Nintendo Entertainment System
file=Nintendo - Nintendo Entertainment System.dat
url=https://raw.githubusercontent.com/libretro/libretro-database/master/dat/Nintendo%20-%20Nintendo%20Entertainment%20System.dat
url=https://ghfast.top/https://raw.githubusercontent.com/libretro/libretro-database/master/dat/Nintendo%20-%20Nintendo%20Entertainment%20System.dat
sha1=<可选，40 位十六进制>
```

规则：

- 除 `[meta]` 外，每个段名就是平台 ID，必须与 `data/platforms.conf` 的规范 ID 一致；
- `file=` 必填，只能是文件名，不得含 `/` 或 `\`（防止路径穿越）；
- `url=` 至少一条，可重复，按书写顺序作为下载镜像优先级；
- `sha1=` 可选，存在时必须是 40 位十六进制（阶段二用于校验下载）；
- `name=` 可选，供界面展示；缺省用 `file` 去掉 `.dat`；
- 未知键、未知段属性一律忽略（前向兼容）；`spec_version > 1` 时拒绝加载并提示升级程序。

默认清单覆盖 libretro-database 中能与 `platforms.conf` 一一对应的 27 份 DAT
（NES、SNES、N64 之外的 GC/Wii/WiiU/3DS/NDS/Switch、PS3、Saturn、Amiga/CD32、
PC-98、ZX 系、Neo Geo、ScummVM、DOOM、Z-Machine、J2ME、PICO-8、TIC-80、
WASM-4、Uzebox、CHIP-8、Atomiswave、Amstrad CPC）。无法映射到现有平台 ID 的
DAT（如 Quake、Arduboy、HBMAME、System）不写入默认清单，只在文件头注释里列名，
用户可自行追加。

### 3.2 清单查找顺序（用户可覆盖）

`rh_dat_sources_default_path()` 按以下顺序返回第一个存在的文件：

1. 环境变量 `ROM_HAMSTER_DAT_SOURCES` 指定的文件；
2. `g_get_user_config_dir()/rom-hamster/dats/sources.conf`；
3. `数据目录/dats/sources.conf`（见 3.3）。

`dat sources --file PATH` 时直接用指定文件，跳过上述顺序。

### 3.3 共用数据目录解析

在核心 `rh-path` 新增 `rh_path_data_dir_candidates()`，按优先级返回候选目录
（新分配的 UTF-8 字符串数组，不检查目录是否存在、已去重）：

1. `ROM_HAMSTER_DATA_DIR`；
2. `<可执行文件目录>/../share/rom-hamster`；
3. `<可执行文件目录>/share/rom-hamster`；
4. `<可执行文件目录>/../../share/rom-hamster`；
5. 编译期 `RH_DATA_DIR`（源码树的 `data/`，只为开发构建兜底）；
6. `/usr/share/rom-hamster`（仅 POSIX）。

可执行文件目录：POSIX 读 `/proc/self/exe`，Windows 用 `GetModuleFileNameW`。
`RH_DATA_DIR` 编译定义从 GUI 目标移到 `romham` 目标，GUI 不再自带一份路径逻辑。
GUI 的 `findDataFile()` 改为遍历该候选列表并检查具体文件是否存在，行为保持不变。

### 3.4 CLI 命令

```text
rom-hamster dat import <FILE> --platform <ID> [--name NAME] [--version VER]
                              [--force] [--db PATH]
rom-hamster dat list [--db PATH]
rom-hamster dat sources [--file PATH] [--explain]
```

- `dat import`：调用 `rh_dat_import`，打印编码、game 数、写入条目数、跳过数、
  是否因内容未变而跳过（`--force` 强制重建）。进度在已知总字节时按 5% 输出到
  stderr，使用 `rh_cli_print` 写字节，避免 TTY 转码；
- `dat list`：平台、条目数、版本、名称、路径，按平台/名称排序；
- `dat sources`：逐平台打印 `file` 与各镜像 URL；`--explain` 先输出实际生效的
  清单路径；找不到清单时报错并给出查找过的位置提示。

所有面向用户的文案走 `_()` / `N_()`，输出始终 UTF-8。

### 3.5 store 新增 API

```c
typedef struct {
    gint64 id;
    gchar *platform;
    gchar *path;
    gchar *name;
    gchar *version;
    gchar *encoding;
    gchar *imported_at;
    gint64 entry_count;
    gint64 skipped;
    gchar *file_sha1;
} RhDatInfo;

void rh_dat_info_free(RhDatInfo *info);
GPtrArray *store_dat_list(RhStore *store, RhErr *out_err);
```

`store_dat_list` 只读，`ORDER BY platform, name, path`；返回带 free func 的
`GPtrArray<RhDatInfo*>`。SQL 仍然只存在于 `store.c`。

## 4. 变更清单

| 文件 | 变更 |
| --- | --- |
| `src/core/store.h` / `src/core/store.c` | 新增 `RhDatInfo`、`rh_dat_info_free`、`store_dat_list` |
| `src/core/rh-path.h` / `src/core/rh-path.c` | 新增 `rh_path_data_dir_candidates()`，内部实现可执行文件目录解析 |
| `src/core/dat-source.h` / `src/core/dat-source.c` | 新增清单解析 `rh_dat_sources_load`、释放 `rh_dat_source_free`、默认路径 `rh_dat_sources_default_path` |
| `src/cli/cmd-dat.c` | 新增 `rh_cmd_dat`，实现 import/list/sources |
| `src/cli/main.c` | 分发 `dat`，帮助文本加入一行说明 |
| `src/gui/main.cpp` | `findDataFile()` 改用核心候选目录，删除重复逻辑 |
| `CMakeLists.txt` | 加入 `dat-source.c`、`cmd-dat.c`；`RH_DATA_DIR` 定义移到 `romham`；安装 `data/dats`；新增 `test-dat-source` |
| `data/dats/sources.conf` | 新增 27 条平台 DAT 源清单 |
| `tests/unit/test-dat-source.c` | 新增解析器单测 |
| `po/zh_CN.po` | 补齐新增 CLI 文案的中文翻译（有 gettext 工具时用 xgettext/msgmerge） |

## 5. 验证

1. Linux CLI 构建 + 全量测试：
   `cmake -S . -B build/linux-cli -DRH_GUI=OFF -DBUILD_TESTING=ON`
   `cmake --build build/linux-cli -j`
   `ctest --test-dir build/linux-cli --output-on-failure`
2. Windows GUI/CLI 构建：把 `C:\Users\zhoux\msys64\mingw64\bin` 加入 PATH 后
   `cmake --build build/win-gui -j`，并运行 `ctest`（跳过需要显示环境的 GUI 用例）。
3. 清单可用性：`rom-hamster dat sources --explain` 必须找到源码树清单并列出 27 个平台；
   把 `ROM_HAMSTER_DAT_SOURCES` 指向临时文件时以临时文件为准。
4. 导入闭环：用临时最小 DAT 执行 `dat import --platform nes`，确认输出统计正确，
   `dat list` 能看到该条，重复导入显示 unchanged，`--force` 会重建。
5. 回归：原有 `core/*`、`cli/version-utf8*`、`gui/version` 测试全部通过。

## 6. 明确不做

- 不实现下载/更新（阶段二，libcurl）；
- 不改 schema（v5 属于阶段三）；
- 不做导入标准名推荐、不做导出文件名切换（阶段三/四）；
- 不改 GUI 界面，不新增 GUI DAT 对话框；
- 不自动推送；阶段一完成并验证后仅本地提交。
