# rom-hamster（C/C++ + Qt 6）架构与依赖说明

> 版本：v0.8（2026-11-18）
> 本版修订：
> 1. 吸收 DAT 生态调研结论：匹配顺序改为 `sha1 → md5 → crc32`（先按 size 缩小范围，无 size 条目豁免）；Redump `<disk>` 以 `entry_type='disk'` 入库（只取 name/sha1/md5），`<sample>` 只计数不入库；`dats.file_sha1` 记录 DAT 文件自身哈希（解析时顺带计算，不额外读盘）；重导时内容未变直接跳过，`force` 可强制重建；schema 升到 v4（`entry_type`、`file_sha1`、`(size, crc32)` 复合索引），新增 v3→v4 迁移。
> 2. §3.5、§5、§7.5、§15.2 的匹配顺序与上述保持一致（刮削查询键同步为强到弱）。
> 3. §14 M2 增加 `import` 执行阶段：临时名 → 原子改名 → 单条事务（游戏 + ROM/媒体/补丁 + `job_items` 同事务），失败回滚已复制文件，取消只发生在条目边界；DAT 匹配在复制流中顺带算 CRC32，唯一命中才回填 `dat_id`/`dat_source`；碟号解析（至少两个不同碟号才算多碟）、按碟号排序的 m3u（`generate_m3u` 选项，默认关闭）、补丁类型判定已接入。压缩包六级分流落到 `classify`（纯条目名判定）与预扫描；作业按 `job_items` 来源标签续做。
>
> v0.7（2026-11-18）修订：
> 1. 新增 §3.3/§3.4 的 v3 schema：`dat_entries` 改为代理主键 + `(dat_id, game_name, rom_name)` 唯一索引（rom 名只在 game 内唯一），补 `status/cloneof/romof/merge_name`；`dats` 增加 `encoding/skipped`；`roms` 增加 `dat_id`（匹配来源可追溯）。
> 2. 新增 §15.2：DAT 文件编码判定与匹配取舍（声明优先、UTF-8 严格、GB18030 重来、`%NN` 兜底；单事务导入；多候选不替调用方选择）。
>
> v0.6（2026-11-18）修订：
> 1. 新增 §15.1：文件名编码兼容是 M2 必做项——压缩包条目名与磁盘文件名统一按 UTF-8 → GB18030 → `%NN` 转义判定，显示与原始字节并存；schema 升到 v2（`dir_path_raw` / `filename_raw` BLOB）并实现 v1→v2 迁移。
>
> v0.5（2026-11-16）修订：
> 1. 把 §16 的 8 个待定项全部定案：不做历史数据转换工具；GUI 首版只做主窗口/详情/导入/首启/拖拽；Windows 只出 zip + Inno Setup；不签名、公布校验和；视频默认外部播放器、Qt Multimedia 可选构建；Linux CLI 走 deb（免安装给 tarball）、AppImage 只用于 GUI；版本检查只提示不安装；图标源文件已具备（`resources/icons/rom-hamster.svg`，512×512）。
> 2. 新增 §9.4 升级语义：deb / Windows 安装包 / AppImage 均为原地覆盖升级；启动时执行 schema 迁移，并拒绝"旧程序打开更高版本库"。
> 3. §8、§9、§11、§14 按上述决策校正：Multimedia 统一为可选构建；AppImage 不再描述为 CLI+GUI 双入口包；M4/M5 验收范围与功能清单对齐；新增版本检查（仅 GUI、Qt Network、失败静默）。
> 4. §16 从"待定项"改为"已定项与遗留问题"，仅保留 1 个真正未决事项。
> 5. 按实际素材与 Qt 工程惯例校正图标相关章节：素材统一在 `resources/icons/`（源文件 `rom-hamster.svg` + `rom-hamster-{16,32,48,256}.png`），GUI 经 `resources/resources.qrc` 的 `/icons` 前缀加载 PNG；仓库内不保留 hicolor 目录，安装时由 CMake 映射到 `/usr/share/icons/hicolor/`。该 SVG 内部包装一张 512×512 位图，**不是真矢量**，超出 512 放大为插值；Q2（SVG 由谁产出）随之关闭。
> 本文是功能规格与架构决策，不描述任何历史实现的移植关系。

## 0. 决策索引（按议题查章节）

| 议题 | 结论 | 章节 |
|---|---|---|
| 交付形态 | 两二进制（`rom-hamster` CLI + `rom-hamster-gui`）+ 共享 `libromham` | §1.1 |
| 目标平台 | Windows 11 x64；Linux 桌面（Ubuntu 22.04+、Debian 12+、Fedora 38+、UOS V25） | §1.3 |
| GUI 工具包 | Qt 6.8 LTS Widgets，随安装包分发，不依赖发行版 Qt | §1.3 §8.3 |
| 构建系统 | CMake ≥ 3.22；MSVC 2022 / GCC 11+ / Clang 14+ | §8.1 |
| 交付链路 | Linux：deb 三包 + CLI tarball + GUI AppImage；Windows：zip + Inno Setup 安装包 | §9 |
| 排除项 | 不做格式转换、不打补丁、无 Web/服务端、不做 NAS/移动端；首个版本不实现刮削 | §1.2 §1.3 |
| 刮削 | 只留扩展点契约（规则文件 + `RhScraper` vtable + 来源字段），**暂不实现、不排期** | §7.5 |
| 元数据存储 | SQLite 单文件，**9 表 + 1 张 FTS5 虚拟表**，类型化三原则 | §3 |
| `tags` | 规范化到 `tags` + `game_tags` | §3.5 |
| 哈希 | `crc32`/`md5`/`sha1` 三列，导入只算 `dat_match_hash` 指定的一种 | §3.5 |
| DAT | 全量入 `dats`/`dat_entries`，不保留扫文件路径 | §3.5 |
| DAT 编码 | 声明优先；无声明 UTF-8 严格 → GB18030 重来 → `%NN` 兜底 | §15.2 |
| DAT 匹配 | 只认校验值（先按 size 缩小范围，再按 sha1→md5→crc32），多候选返回二义不替调用方选择 | §15.2 |
| 事务 | 每条目一个事务（记录 + `job_items` 状态同事务） | §3.5 §11 |
| 并发 | 不加锁文件，靠 SQLite 串行化写 + `busy_timeout=5000` | §13 |
| 模块边界 | `classify` 只判定不搬文件、`query` 只读、`store` 唯一持有 SQL | §5 |
| 导入流程 | 两阶段：预扫描（可交互）→ 执行（零交互） | §5 §6.2 |
| 线程 | 主线程跑 Qt 事件循环与交互；工作线程只调核心，禁止碰 QWidget | §6.2 |
| 前端规格 | 每前端一份 `.conf`，模板优先、vtable 兜底；解析期报错带行号 | §7 |
| gamelist.xml | `xmlDoc` + XPath；单文件过大则报错要求拆分 | §7.2 |
| Pegasus | 记录以 EOF 收尾、描述支持续行、补 `assets.screenshot`/`assets.titlescreen`（注意全小写） | §7.2 |
| 图标 | 素材在 `resources/icons/`：`rom-hamster.svg`（512×512）为源，`rom-hamster-{16,32,48,256}.png` 由它导出；GUI 经 `.qrc` 的 `/icons` 前缀加载；安装时映射到 hicolor；`.ico` 由构建从 256 PNG 生成 | §1.1 §9.3 |
| 二进制命名 | CLI `rom-hamster`、GUI `rom-hamster-gui`，Windows 加 `.exe` | §9.3 |
| 打包 | Linux：deb 三包 + CLI tarball + GUI AppImage；Windows：zip + Inno Setup | §9 |
| 配置 | 五层合并 + 来源记录 + `config --explain`；双端路径映射 | §10 |
| 既有收藏 | 以库目录为契约；不读取第三方管理器或历史格式的数据库/配置 | §12 |
| i18n | CLI 用 gettext；GUI 用 Qt Linguist（`.ts`/`.qm`） | §11 |
| 首启与拖拽 | GUI 启动即弹目录选择；`dragEnterEvent`/`dropEvent` 走同一导入管线，进 M4 | §11 |
| 里程碑 | M0–M5，双端同等；M3→M4 串行 | §14 |
| 代码托管 | Gitee 主仓 + GitHub 同名镜像（单向）；CI 由 GitHub Actions 在镜像上自动跑四组矩阵 | §8.4 README |
| 升级 | 三端原地覆盖；启动时 schema 迁移 + 降级防护 | §9.4 |
| 版本检查 | GUI 启动静默检查；仅提示并打开下载页，不下载不安装 | §11 |
| 已定项 | 8 项决策与 2 个遗留问题 | §16 |

---


## 1. 目标与边界

### 1.1 交付形态

一款 Windows 11 / Linux 双端桌面应用，同时提供等价的无界面运行能力：

| 产物 | 说明 | 图形依赖 |
|---|---|---|
| `rom-hamster`（CLI） | 全部功能可通过命令与子命令使用；可脚本化、可在纯命令行系统运行 | 无 |
| `rom-hamster-gui` | 同一套核心，Qt 6 Widgets 前端（C++17） | Qt 6 Core/Gui/Widgets |
| `libromham` | 核心库（C11），两个前端共享，不含任何 GUI 工具包符号 | 无 |

硬约束（违反即视为架构错误）：

1. `libromham` 的链接依赖中不得出现 **Qt 任意模块**（`Qt6Core`/`Qt6Gui`/`Qt6Widgets`）以及 `gtk4`/`gdk`/`pango`。
2. `libromham` 不得打印面向用户的文字，不得读 stdin，不得自行弹窗。所有交互经 `rh_ui` 回调（见 §6）。
3. `libromham` 不得持有全局可变状态，全部经 `RhCtx *` 传递。
4. 除 `store.c` 外，其他源文件不得出现 SQL 语句。
5. Qt 只允许出现在 `rom-hamster-gui` 与 AppImage/Windows 包的 GUI 侧；CLI 与核心不加载 Qt。

图标资产：素材统一放在 `resources/icons/`，按 Qt 工程惯例用文件名区分尺寸，源文件与导出产物同目录。

- `resources/icons/rom-hamster.svg` —— 唯一源文件（512×512；当前该 SVG 内部包装一张 512×512 的位图，**并非真矢量**，超过 512 的放大属插值）。
- `resources/icons/rom-hamster-{16,32,48,256}.png` —— 由源文件导出并入库，四个 PNG 与源文件必须保持同一图形。
- `resources/icons/preview-64.png` —— 仅供评审预览，不进 `.qrc`、不安装、不参与构建。

许可：项目主体以 **MIT** 发布（根目录 `LICENSE`）；`rom-hamster-gui` 动态链接 Qt 6（LGPLv3），安装包附 Qt 许可证文本与源码获取地址，不静态链接、允许用户替换 Qt 动态库；CLI 与核心库不含 Qt。

GUI 通过 Qt 资源系统加载：`resources/resources.qrc` 以 `/icons` 前缀收录上述四个 PNG，代码用 `QIcon` 逐尺寸 `addFile(":/icons/rom-hamster-16.png")`… 组装多分辨率图标。**SVG 不进 `.qrc`**：渲染 SVG 需要 QtSvg，会破坏 §8.3 的依赖清单。Windows 的 `rom-hamster.ico` 由构建从 `rom-hamster-256.png` 生成。Linux 安装时由 CMake 把同一批素材映射成 FDO hicolor 布局（`/usr/share/icons/hicolor/{16x16,32x32,48x48,256x256}/apps/rom-hamster.png` 与 `/usr/share/icons/hicolor/scalable/apps/rom-hamster.svg`），保证桌面菜单/任务栏能取到图标；仓库内**不保留** hicolor 目录结构。

### 1.2 非目标

- 不下载 ROM、不提供 ROM 来源、不接入私有镜像。
- **不自动打补丁**：补丁只做存储、关联、类型标注，绝不执行。
- 不做 ROM 格式转换（`chd`/`cso`/`wbfs` 仅作为 `rom_format` 的取值被记录）。
- 不做在线元数据刮削（ScreenScraper / IGDB 等留作 §7 的代码型扩展点，不进入首个版本）。
- 不提供 Web UI / 服务端模式。

### 1.3 目标平台基线（已定）

Windows 11 x64 与现代 Linux 桌面**同等支持**；两者共用同一套核心与同一份前端规格。

| 组件 | 依赖下限 | 覆盖的典型系统 |
|---|---|---|
| CLI（`libromham`） | Windows 11 x64；Linux：glibc ≥ 2.35、glib 2.66、sqlite 3.35、libarchive 3.4、libxml2 2.9 | Windows 11；Ubuntu 22.04/24.04、Debian 12/13、Fedora 38+、UOS V25 |
| GUI（`rom-hamster-gui`） | 上述 + **随包 Qt 6.8 LTS**（Core/Gui/Widgets/Network；Multimedia 可选构建） | 同上 |
| AppImage | GUI AppImage：内置 Qt 6.8 与核心库；构建基线 Ubuntu 22.04 | Ubuntu 22.04+、Debian 12+、Fedora 38+ |
| CLI tarball | 免安装包，只含 CLI + 核心库，不含 Qt | 同上 |
| Windows | Windows 11 x64；不提供 Windows 10、ARM64、Server 支持 | Windows 11 21H2+ |

平台取舍：

- **Windows 11 x64 是唯一 Windows 目标**；不承诺 Windows 10、ARM64、Windows Server。
- Ubuntu 22.04 通过 **GUI AppImage 与自带 Qt 的 deb** 覆盖，不依赖发行版 Qt 版本；CLI 另有 tarball 兜底。
- UOS V25（glibc 2.38、Qt 6.8.0）可全功能；UOS V20（glibc 2.28、Qt 5.15）低于本文件下限，不在支持范围。
- 明确排除：NAS 系统、OCI 容器交付、移动端、Web 端、glibc < 2.35 的老平台。

任何系统版本不要靠文档推断，在真机上一行定论：

```
# Linux
pkg-config --modversion glib-2.0; sqlite3 --version; ldd --version | head -1
# Windows（PowerShell）
rom-hamster-gui.exe --version; dumpbin /dependents rom-hamster-gui.exe | findstr Qt6
```

---

## 2. 分层与目录结构

### 2.1 分层

| 层 | 内容 | 依赖方向 |
|---|---|---|
| L0 基础 | 错误码、字符串、路径、哈希、配置、日志 | 只依赖 GLib/zlib |
| L1 存储 | `store.c`（唯一持有 SQL）、schema 迁移 | 依赖 L0 + libsqlite3 |
| L2 领域 | 平台规则、分类、压缩包、DAT、导入、整库、导出、检索、详情、管理 | 依赖 L0+L1 |
| L3 任务 | 导入/导出作业、进度、取消、持久化队列 | 依赖 L2 |
| L4 前端 | CLI（`src/cli`）、Qt GUI（`src/gui`，C++） | 依赖 L0–L3 |

依赖只能向下，禁止 L2 反向依赖 L3/L4。任务进度经 L3 的回调上报，L2 内部不感知前端。

### 2.2 目录

```
rom-hamster/
├─ CMakeLists.txt
├─ cmake/                      # 依赖探测、Qt 部署辅助
├─ README.md
├─ ARCHITECTURE.md             # 本文
├─ data/
│  ├─ platforms.conf          # 101 平台规则
│  ├─ frontends/
│  │  ├─ es.conf              # 同一份"前端规格"描述导入与导出
│  │  ├─ esde.conf
│  │  └─ pegasus.conf
│  ├─ rom-hamster.conf.template
│  ├─ io.github.goddog312.RomHamster.desktop
│  ├─ io.github.goddog312.RomHamster.metainfo.xml
│  └─ rom-hamster.1
├─ resources/                  # Qt 资源与图标素材
│  ├─ resources.qrc           # /icons 前缀，仅收录 PNG（SVG 需要 QtSvg，不入）
│  └─ icons/
│     ├─ rom-hamster.svg      # 512×512 源文件（内嵌位图，非真矢量）
│     ├─ rom-hamster-16.png
│     ├─ rom-hamster-32.png
│     ├─ rom-hamster-48.png
│     ├─ rom-hamster-256.png  # 构建据此生成 .ico
│     └─ preview-64.png       # 仅供评审，不入 .qrc、不安装
├─ src/
│  ├─ core/                    # C11
│  │  ├─ rh-ctx.h/.c           # RhCtx：配置、存储句柄、ui 回调、日志、缓存目录
│  │  ├─ rh-err.h/.c           # RhErr 错误码 + 可读消息
│  │  ├─ rh-str.h/.c           # 扩展名/复合扩展名、核心名归一、相似度、清洗目录名
│  │  ├─ rh-fs.h/.c            # 递归遍历、原子写、安全复制、路径校验
│  │  ├─ rh-path.h/.c          # 双端路径/配置目录/长路径适配
│  │  ├─ rh-fname.h/.c         # 文件名原始字节：UTF-8/GB18030/转义三态、可逆
│  │  ├─ rh-hash.h/.c          # crc32/md5/sha1 单次读盘三算
│  │  ├─ rh-config.h/.c        # 五层配置加载与持久化
│  │  ├─ platform.h/.c         # 规则解析、平台判定、媒体识别、导出平台名映射
│  │  ├─ classify.h/.c         # 单文件分类分流
│  │  ├─ rh-archive.h/.c       # libarchive：列表/解压/加密探测/打包（避开 <archive.h>）
│  │  ├─ store.h/.c            # 唯一 SQL 入口
│  │  ├─ store-schema.h        # DDL + 迁移版本表
│  │  ├─ dat.h/.c              # Logiqx DAT 导入与哈希匹配
│  │  ├─ library.h/.c          # gamelist.xml / metadata.pegasus.txt 整库识别
│  │  ├─ import.h/.c           # 导入编排：队列、归属、命名、m3u、补丁类型
│  │  ├─ export.h/.c           # 前端导出
│  │  ├─ query.h/.c            # 检索/排序/筛选
│  │  ├─ detail.h/.c           # 只读详情视图模型
│  │  ├─ manage.h/.c           # 编辑/删除/碟片/补丁/统计/完整性
│  │  ├─ job.h/.c              # 作业、进度、取消、持久化队列
│  │  └─ plugin.h/.c           # 扩展点注册表（编译期）
│  ├─ cli/                     # C11
│  │  ├─ main.c                # GOptionContext 解析，子命令分发
│  │  ├─ ui-cli.c              # rh_ui 的终端实现
│  │  └─ cmd-*.c               # 各子命令
│  └─ gui/                     # C++17 / Qt 6
│     ├─ main.cpp              # QApplication，单实例 + 远程激活
│     ├─ ui-qt.cpp/.h          # rh_ui 的 Qt 实现
│     ├─ window-main.cpp/.h    # QTableView + 筛选栏
│     ├─ model-games.cpp/.h    # QAbstractTableModel
│     ├─ window-detail.cpp/.h  # 详情 + 图片/视频预览
│     ├─ dialog-*.cpp/.h       # 确认/选平台/选图片类型/元数据表单
│     ├─ single-instance.cpp/.h
│     └─ update-checker.cpp/.h # 静默版本检查（Qt Network）
├─ po/                         # CLI gettext
├─ i18n/                       # GUI .ts/.qm
├─ tests/
│  ├─ unit/                    # GLib GTest
│  ├─ gui/                     # Qt Test 冒烟
│  └─ fixtures/                # 构造的小样本 ROM 目录
├─ packaging/
│  ├─ debian/
│  ├─ appimage/
│  └─ windows/                 # .iss + windeployqt 脚本
└─ tools/
```

---

## 3. 数据模型

### 3.1 存储选型

**SQLite3**（`libsqlite3-0`，公共领域授权）。理由记录在案，便于日后复查：

- 进程内库 + 单文件，无守护进程、无端口、无账号体系，与桌面应用语义匹配。
- 单文件即全库：备份 = 复制，迁移 = 换路径，外部工具可读。
- 具备 schema 约束、事务、索引、FTS5；单条写入是 O(1) 级更新，不需要整表重写。
- 启动时直接打开库文件，不解析文本库、不逐行起子进程。

不采用 PostgreSQL：单机单用户场景下需要用户 `initdb`、起服务、管端口与 `pg_hba`，成本转嫁给用户，收益（多写并发、远端连接）用不上。见 §13 风险表。

不采用 gdbm/LMDB/BerkeleyDB 作为主库：它们是键值层，无 schema、无事务、无排序索引；13 个筛选维度仍需全表扫描手写判定，且无法用 SQL 表达联表与排序。这类库适合做纯索引（见 §7.4），不适合主库。

连接参数：

```c
"PRAGMA journal_mode=WAL;"
"PRAGMA foreign_keys=ON;"
"PRAGMA synchronous=NORMAL;"
"PRAGMA busy_timeout=5000;"
```

库文件位置：`<ROM_ROOT>/romhamster.db`。**库文件必须位于本地文件系统**，不得放在 SMB/NFS 上（SQLite 的锁依赖本地文件系统语义，见 §13）。

### 3.2 28 字段 → 表结构（逐字段对照）

元数据规格共 28 个字段：其中 8 项是以分号分隔的多值字段（`rom_files` + 7 个媒体字段）、1 项（`tags`）是逗号分隔列表，共 9 项拆入子表；其余 19 项为标量，落在 `games` 表。

| # | 字段 | 新位置 | 处理 |
|---|---|---|---|
| 1 | `id` | `games.id` | 原样保留（小写字母数字 8 位），不重新生成 |
| 2 | `rom_files` | `roms.filename` × N | 分号拆分；`disc_no` 从碟名解析（原 `parse_disc_name`） |
| 3 | `game_main_name` | `games.game_main_name` | 命名基准 |
| 4 | `platform` | `games.platform` | 规范平台 ID |
| 5 | `is_multi_disc` | `games.is_multi_disc` | 文本 `true/false` → `1/0` |
| 6 | `is_arcade` | `games.is_arcade` | 同上 |
| 7 | `rom_format` | `games.rom_format` | `raw/zip/7z`，为 `chd/cso/wbfs` 预留取值 |
| 8 | `rom_status` | `games.rom_status` | `good/baddump/unknown`，加 CHECK 约束 |
| 9 | `dat_source` | `games.dat_source` | 匹配来源 DAT 名 |
| 10 | `name` | `games.name` | 前端显示名 |
| 11 | `desc` | `games.description` | 更名（`desc` 易与 SQL 语义混淆） |
| 12 | `developer` | `games.developer` | |
| 13 | `publisher` | `games.publisher` | |
| 14 | `year` | `games.year` | `INTEGER`，空串 → `NULL` |
| 15 | `genre` | `games.genre` | |
| 16 | `players` | `games.players` | `INTEGER`，空串 → `NULL` |
| 17 | `rating` | `games.rating` | `REAL`，空串 → `NULL` |
| 18 | `region` | `games.region` | |
| 19 | `tags` | `tags` + `game_tags` | 逗号拆分并规范化，便于按标签筛选 |
| 20 | `dir_path` | `games.dir_path` | `<platform>/<游戏目录>`，加 UNIQUE |
| 21 | `cover_filename` | `media(kind='covers')` | 分号拆分 |
| 22 | `screenshot_filename` | `media(kind='screenshots')` | 分号拆分 |
| 23 | `video_filename` | `media(kind='videos')` | 分号拆分 |
| 24 | `marquee_filename` | `media(kind='marquees')` | 分号拆分 |
| 25 | `miximage_filename` | `media(kind='miximages')` | 分号拆分 |
| 26 | `intro_filename` | `media(kind='intro')` | 分号拆分 |
| 27 | `patcher_filename` | `media(kind='patcher')` | 分号拆分 |
| 28 | `patch_type` | `games.patch_type` | `''/generic/platform/mixed`，由补丁集合重算 |

类型转换原则：空串一律转 `NULL`（不是 `''`），否则 `WHERE year IS NULL` 这类查询会失真；布尔一律 `1/0`（不是 `'true'`）。

### 3.3 DDL

```sql
-- schema 版本：PRAGMA user_version
CREATE TABLE games (
  id             TEXT PRIMARY KEY,
  platform       TEXT NOT NULL,
  name           TEXT NOT NULL,
  game_main_name TEXT,
  dir_path       TEXT NOT NULL UNIQUE,
  dir_path_raw   BLOB,                    -- v2：目录路径原始字节（可 NULL）
  developer      TEXT,
  publisher      TEXT,
  year           INTEGER,
  genre          TEXT,
  players        INTEGER,
  rating         REAL,
  region         TEXT,
  description    TEXT,
  is_multi_disc  INTEGER NOT NULL DEFAULT 0 CHECK (is_multi_disc IN (0,1)),
  is_arcade      INTEGER NOT NULL DEFAULT 0 CHECK (is_arcade IN (0,1)),
  rom_format     TEXT NOT NULL DEFAULT 'raw',
  rom_status     TEXT NOT NULL DEFAULT 'unknown'
                 CHECK (rom_status IN ('good','baddump','unknown')),
  dat_source     TEXT,
  patch_type     TEXT NOT NULL DEFAULT ''
                 CHECK (patch_type IN ('','generic','platform','mixed')),
  created_at     TEXT NOT NULL,
  updated_at     TEXT NOT NULL
);

CREATE TABLE roms (
  game_id  TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
  filename TEXT NOT NULL,
  filename_raw BLOB,                  -- v2：文件名原始字节（可 NULL）
  disc_no  INTEGER,
  size     INTEGER,
  crc32    TEXT,
  md5      TEXT,
  sha1     TEXT,
  dat_id   INTEGER REFERENCES dats(id), -- v3：该 ROM 匹配来自哪个 DAT
  sort     INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (game_id, filename)
);

CREATE TABLE media (
  game_id  TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
  kind     TEXT NOT NULL CHECK (kind IN
           ('covers','screenshots','videos','marquees','miximages','intro','patcher')),
  filename TEXT NOT NULL,
  filename_raw BLOB,               -- v2：文件名原始字节（可 NULL）
  size     INTEGER,                -- 字节；导入时 stat 一次，stats 用 SUM(size)
  sort     INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (game_id, kind, filename)
);

CREATE TABLE tags (
  id   INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE
);
CREATE TABLE game_tags (
  game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
  tag_id  INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
  PRIMARY KEY (game_id, tag_id)
);

-- DAT 索引：导入 DAT 时一次性写入，匹配时按哈希查
CREATE TABLE dats (
  id           INTEGER PRIMARY KEY,
  platform     TEXT NOT NULL,
  path         TEXT NOT NULL,
  name         TEXT,
  version      TEXT,
  imported_at  TEXT NOT NULL,
  entry_count  INTEGER,
  encoding     TEXT,                     -- v3：实际解码方式（utf-8/gb18030/声明值）
  skipped      INTEGER,                  -- v3：缺 rom 名等被跳过的条目数
  UNIQUE (platform, path)
);
CREATE TABLE dat_entries (
  id         INTEGER PRIMARY KEY,        -- v3：代理主键
  dat_id     INTEGER NOT NULL REFERENCES dats(id) ON DELETE CASCADE,
  game_name  TEXT NOT NULL DEFAULT '',   -- rom 名只在 game 内唯一
  rom_name   TEXT NOT NULL,
  status     TEXT,                       -- good/baddump/nodump/verified
  cloneof    TEXT,                       -- MAME parent/clone
  romof      TEXT,
  merge_name TEXT,
  size       INTEGER,
  crc32      TEXT,
  md5        TEXT,
  sha1       TEXT
);
CREATE UNIQUE INDEX idx_dat_entry_key
  ON dat_entries(dat_id, game_name, rom_name);

-- 导入作业：崩溃后可续
CREATE TABLE jobs (
  id          INTEGER PRIMARY KEY,
  kind        TEXT NOT NULL,             -- import / export / dat-import
  state       TEXT NOT NULL,             -- queued/running/paused/done/failed/cancelled
  total       INTEGER NOT NULL DEFAULT 0,
  done        INTEGER NOT NULL DEFAULT 0,
  created_at  TEXT NOT NULL,
  finished_at TEXT,
  error       TEXT
);
CREATE TABLE job_items (
  job_id     INTEGER NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
  seq        INTEGER NOT NULL,
  src        TEXT NOT NULL,
  action     TEXT,                       -- rom / media / patcher / skip
  platform   TEXT,
  target_id  TEXT,
  state      TEXT NOT NULL DEFAULT 'pending',
  message    TEXT,
  PRIMARY KEY (job_id, seq)
);
```

索引：

```sql
CREATE INDEX idx_games_platform  ON games(platform);
CREATE INDEX idx_games_name      ON games(name);
CREATE INDEX idx_games_year      ON games(year);
CREATE INDEX idx_games_rating    ON games(rating);
CREATE INDEX idx_games_status    ON games(rom_status);
CREATE INDEX idx_roms_disc       ON roms(game_id, disc_no);
CREATE INDEX idx_dat_crc32       ON dat_entries(crc32);
CREATE INDEX idx_dat_md5         ON dat_entries(md5);
CREATE INDEX idx_dat_sha1        ON dat_entries(sha1);
```

全文检索（`--kw` 走 FTS5，不再做整表 `LIKE`）：

```sql
CREATE VIRTUAL TABLE games_fts USING fts5(
  name, game_main_name, description,
  content='games', content_rowid='rowid',
  tokenize="trigram"
);
-- 触发器：games 的 INSERT/UPDATE/DELETE 同步到 games_fts
```

`trigram` 分词器（SQLite ≥ 3.34）对中日韩与含数字的 ROM 文件名都能命中子串，优于 `unicode61` 对中文会整串丢失。

> **v2（文件名编码可逆）**：`games.dir_path_raw`、`roms.filename_raw`、`media.filename_raw`
> 三列由 `ALTER TABLE ... ADD COLUMN` 加入，允许 NULL。`*_raw` 存磁盘/归档里的真实字节，
> 显示列存 UTF-8（可能是 GB18030 推测或转义结果）；两者同时存在才能"显示正确"且
> "可还原"。旧库升级后旧行为 NULL，重新扫描时补齐。详见 §15.1。

### 3.4 schema 版本与升级

`PRAGMA user_version` 记录 schema 版本，`store.c` 内维护 `migration[]` 表：`{version, sql}` 顺序执行，每次升级在单事务内完成。首个版本为 v1。以后加字段只允许"加表/加列/加索引"，不重命名已有列，避免用户数据升级损坏。（这是**结构**升级，与 §12 的既有收藏接入无关。）

启动时（CLI 与 GUI 相同）：

- `user_version < 当前版本`：按 `migration[]` 顺序迁移，单事务提交；
- `user_version > 当前版本`：**拒绝写入**，以只读方式打开并提示"此库由更新版本的 rom-hamster 创建，请升级程序"，防止降级写坏数据；
- 迁移失败：回滚并保留原库，不进入写入路径。

升级动作本身不迁移用户数据（§9.4），只做 schema 版本检查。

已落地的迁移：

- **v1 → v2**：为 `games`/`roms`/`media` 增加 `*_raw BLOB` 列。
- **v2 → v3**：`dats` 加 `encoding`/`skipped`，`roms` 加 `dat_id`；`dat_entries` 删除旧主键
  `(dat_id, rom_name)`（真实 DAT 中同一 rom 名会在不同 game 下重复），重建为代理主键 +
  `(dat_id, game_name, rom_name)` 唯一索引，并补 MAME 的 `cloneof`/`romof`/`merge_name`。
  重建以"建新表 → 复制 → 删旧表 → 改名"完成，与其他迁移同在单事务内。此阶段
  `dat_entries` 尚未写入过用户数据，重建不涉及数据取舍。
- **v3 → v4**：`dats` 加 `file_sha1`（识别原地替换的同名 DAT），`dat_entries` 加
  `entry_type`（旧行默认 `rom`），并新建 `(size, crc32)` 复合索引。

新建库先建 v1 再在同一事务里依次执行 v2、v3、v4 的语句；旧库打开时按同一组语句单事务迁移，失败回滚并保留原库。

### 3.5 已定决策（不再讨论）

| 项 | 决定 | 理由 |
|---|---|---|
| `tags` | 规范化到 `tags` + `game_tags` | 支持按标签筛选与计数；存逗号串只能 `LIKE`，`rpg` 会误命中 `jrpg` |
| `roms` 哈希 | 三列都建；导入时只算 `dat_match_hash` 指定的那一种（默认 `crc32`），其余留 `NULL`，由 `verify --deep` 按需补算回填 | 反正 DAT 匹配要算同一种，顺手即可；避免导入时三次全量读盘 |
| DAT | 全量入 `dats` / `dat_entries`，**不保留"直接扫 DAT 文件"的路径** | 两条路径必然行为分叉；入表后一次导入、多次索引查询 |
| DAT 匹配键 | 只认校验值，`size > 0` 时先按 size 缩小范围（无 size 的 disk/nodump 条目豁免），哈希顺序 `sha1 → md5 → crc32`；同哈希命中多条即返回全部候选（`RH_DAT_MATCH_AMBIGUOUS`），**不按文件名兜底、不替调用方挑一条** | SHA1/MD5 抗碰撞能力强于 CRC32；文件名在不同 game 下重复、parent/clone 共用 rom 是常态，按名字挑会把错误写进库 |
| DAT 导入事务 | **整份 DAT 一个事务**；取消/失败/编码重来全部回滚，库中不会出现半条 DAT | DAT 索引是批量重建而非逐条业务写入，单事务比逐批提交更快且能保证"要么完整、要么没有"；与 §3.5 的游戏记录"一条目一事务"不冲突 |
| DAT 重导 | 先比 `file_sha1`：内容未变直接返回（`stats.unchanged`），`force` 强制重建；哈希在解析时顺带计算 | 常见场景是 DAT 没变只是重跑；跳过能省掉整次解析与索引重建，代价只有一次顺序读（仅重导时发生） |
| 事务 | **每个条目一个事务**（记录写入 + `job_items` 状态同事务）；"每 N 条"只作为进度上报频率 | 崩溃最多丢当前条目，且库与队列状态始终自洽；不做跨条目大事务 |
| 全文检索 | FTS5 `trigram`（需 SQLite ≥ 3.34）；启动探测不到则降级 `LIKE` 子串，不报错 | Debian 12 = 3.40、Ubuntu 22.04 = 3.37 均满足；降级只损失子串命中精度 |

---

## 4. `store_*` API

约定：

- 全部返回 `RhErr`（`RH_OK` / 错误码 + `char msg[]`）。不使用 `assert` 处理外部输入（磁盘、用户、DAT 文件都可能给出脏数据）。
- 不引入"操作当前记录全局数组"的隐式语义；所有读写围绕显式结构体 `RhGame` / `RhFilter` / `RhJob` 进行。
- 批量写用事务：`store_tx_begin()` / `store_tx_commit()` / `store_tx_rollback()`。
- 只读查询返回 `GPtrArray<RhGame*>` 或游标，所有权明确写入注释。
- **出参所有权（M1 起统一）**：所有 `**out` 出参由函数分配、调用方释放 —— 结构体用 `rh_*_free`，数组用 `g_ptr_array_unref`（数组自带元素的 `free_func`）。不再要求调用方预置结构体；分配与释放成对，由 M1 测试覆盖。

```c
/* 生命周期 */
RhErr store_open(RhCtx *ctx, const char *db_path, int flags);
void  store_close(RhCtx *ctx);
RhErr store_migrate(RhCtx *ctx);                   /* 依 user_version 逐级升级 */

/* 事务 */
RhErr store_tx_begin(RhCtx *ctx);                  /* BEGIN IMMEDIATE */
RhErr store_tx_commit(RhCtx *ctx);
RhErr store_tx_rollback(RhCtx *ctx);

/* 游戏记录 */
RhErr store_game_get(RhCtx *ctx, const char *id, RhGame **out);
RhErr store_game_upsert(RhCtx *ctx, const RhGame *g);
RhErr store_game_delete(RhCtx *ctx, const char *id);
RhErr store_game_list(RhCtx *ctx, const RhFilter *f, GPtrArray **out);
RhErr store_game_count(RhCtx *ctx, gint64 *out);

/* 子表 */
RhErr store_rom_add(RhCtx *ctx, const char *game_id, const RhRomFile *r);
RhErr store_rom_remove(RhCtx *ctx, const char *game_id, const char *filename);
RhErr store_rom_list(RhCtx *ctx, const char *game_id, GPtrArray **out);
RhErr store_media_add(RhCtx *ctx, const char *game_id, const char *kind,
                      const char *filename);
RhErr store_media_add_sized(RhCtx *ctx, const char *game_id, const char *kind,
                            const char *filename, gint64 size, int seq);
RhErr store_media_remove(RhCtx *ctx, const char *game_id, const char *kind,
                         const char *filename);
RhErr store_media_list(RhCtx *ctx, const char *game_id, const char *kind,
                       GPtrArray **out);
RhErr store_media_total_size(RhCtx *ctx, const char *game_id, gint64 *out);

/* 标签 */
RhErr store_tags_set(RhCtx *ctx, const char *game_id, const char **tags, int n);
RhErr store_tag_list(RhCtx *ctx, GPtrArray **out);    /* 供筛选下拉用 */
RhErr store_game_tags(RhCtx *ctx, const char *game_id, GPtrArray **out);

/* DAT：XML 解析归 dat.c（§5.1 边界表），store 只负责落库 */
RhErr store_dat_begin(RhStore *store, const char *platform, const char *path,
                      const char *name, const char *version,
                      const char *encoding, gint64 *out_id);
RhErr store_dat_entries_add(RhStore *store, gint64 dat_id, GPtrArray *entries);
RhErr store_dat_finish(RhStore *store, gint64 dat_id, const char *name,
                       const char *version, const char *file_sha1,
                       gint64 skipped, gint64 *out_count);
RhErr store_dat_abort(RhStore *store, gint64 dat_id);
RhErr store_dat_count(RhStore *store, gint64 *out);
RhErr store_dat_lookup(RhStore *store, const char *platform, const char *path,
                       gchar **out_file_sha1, gint64 *out_id,
                       gint64 *out_entry_count);  /* 重导前比对内容哈希 */
RhErr store_dat_find_by_hash(RhStore *store, const char *algo,
                             const char *hash, gint64 size,
                             const char *platform,
                             GPtrArray **out);  /* GPtrArray<RhDatMatch*>；size > 0 时精确过滤 */

/* 作业 */
RhErr store_job_create(RhCtx *ctx, const char *kind, int total, gint64 *job_id);
RhErr store_job_update(RhCtx *ctx, gint64 job_id, const char *state, int done,
                       const char *err);
RhErr store_job_get(RhCtx *ctx, gint64 job_id, RhJob **out);
RhErr store_job_list_unfinished(RhCtx *ctx, GPtrArray **out);
RhErr store_job_item_add(RhCtx *ctx, RhJobItem *it);
RhErr store_job_item_update(RhCtx *ctx, gint64 job_id, int seq,
                            const char *state, const char *err);
RhErr store_job_items_list(RhCtx *ctx, gint64 job_id, GPtrArray **out);

/* 检索 */
RhErr store_search_fts(RhCtx *ctx, const char *kw, GPtrArray **out);
```

`RhFilter` 与 `list` 子命令的筛选参数一一对应：

```c
typedef struct {
  const char *platform, *genre, *developer, *publisher, *region, *patch_type;
  int   year_from, year_to;      /* 0 = 不限 */
  int   multi;                   /* -1 不限 / 0 / 1 */
  int   has_patch;               /* -1 不限 / 0 / 1 */
  const char *status;            /* good/baddump/unknown/"" */
  const char *format;            /* raw/zip/7z/"" */
  const char **tags; int n_tags;
  const char *kw;                /* 走 FTS5 */
  const char *sort;              /* name/rating/year/discs */
  int   desc;
} RhFilter;
```

---

## 5. 领域层模块职责

| 模块 | 职责 |
|---|---|
| `rh-str` | 扩展名/复合扩展名、核心名归一、相似度、目录名清洗 |
| `rh-fs` | 递归遍历、原子写、安全复制、路径校验（统一走 `rh-path`） |
| `rh-path` | 配置/缓存目录、长路径、大小写与保留名适配 |
| `rh-fname` | 文件名原始字节判定：UTF-8 / GB18030 推测 / 转义兜底；显示名与原始字节可逆转换 |
| `rh-hash` | crc32/md5/sha1 按需单次读盘三算 |
| `rh-config` | 五层配置加载、来源记录、持久化 |
| `platform` | 规则解析、扩展名→平台判定、媒体类型识别、导出平台名映射 |
| `classify` | 单文件分类：返回类型 + 目标平台 |
| `archive` | 压缩包列表/解压/加密探测/打包 |
| `store` | 唯一 SQL 入口、schema 迁移 |
| `dat` | Logiqx 解析、建 `dat_entries`、按哈希查 |
| `library` | gamelist.xml / Pegasus 清单识别 |
| `import` | 唯一编排者：队列、归属、命名、m3u、落库 |
| `export` | 遍历输出、模板渲染、媒体复制/压缩 |
| `query` | 只读检索、排序、筛选 |
| `detail` | 组装只读 `RhGameDetail` |
| `manage` | 编辑/删除/碟片/补丁/统计/完整性 |
| `job` | 作业生命周期、进度、取消、队列持久化 |

硬边界见 §5.1。

必须保持的判定规则（验收项）：

- **分类优先级链**（classify）：压缩包 → 通用补丁（`.ips/.bps/.ups/.ppf/.xdelta/.xdelta3/.patch/.ips32`）→ 平台二义后缀 → 平台专属补丁 → ROM 本体（且当扩展名同时是媒体类型时，用 `dual_role_kind` 判真实内容）→ 媒体/说明。**必须先判真实内容再判 ROM**；否则 `.jpg` 会被当 videopac ROM、`.md` 会被当 megadrive ROM。
- **压缩包六级分流**：PSV 整库（按 `sce_sys/`、`PCSH*` 路径特征识别，整体入库不解压）→ 纯补丁包 → 街机 ZIP（按 `[arcade_rom_suffixes]` 判定多文件）→ 二义性包 → 单 ROM 包 → 混合包。分流只依据条目名，不读内容；PSV/街机/单 ROM 三类按整包入库（`rom_format` 记外层容器），纯补丁/二义/混合三类只计数并留给 UI 决策，不静默当 ROM。
- **平台判定**：扩展名唯一命中 → 直接用；多平台命中 → 走 `ui->choose`；目录名反查走 `aliases`（`resolve_platform_dir`）。
- **媒体归属**：孤立媒体按核心名归一后做相似度匹配，阈值取配置 `duplicate_check_threshold`（默认 70）。
- **目录命名**：`sanitize_dirname` 规则固定，保证库内文件布局稳定；Windows 额外处理保留名（`CON`/`PRN`/`AUX`/`NUL`/`COM1`…）、结尾点与空格。见 §15。

### 5.1 职责边界（越界即视为架构错误）

| 模块 | 只做 | 不做 |
|---|---|---|
| `platform` | 解析规则、扩展名→平台判定、媒体类型识别 | 不读文件内容、不碰数据库、不搬文件 |
| `classify` | 回答"这个文件归谁"（返回类型 + 目标平台） | **不搬文件、不入库、不弹窗** |
| `rh-fname` | 判定名字编码，给出显示名与原始字节 | 不改名、不碰磁盘、不猜平台 |
| `rh-hash` | 单次顺序读盘，按需同时算 CRC32/MD5/SHA1 | 不判断文件类型、不写库、不决定重算策略 |
| `archive` | 压缩包列表/解压/加密探测/打包 | 不知道"ROM 是什么"，不判定平台 |
| `dat` | Logiqx 解析、建 `dat_entries`、按哈希查 | 不改游戏记录，只返回匹配结果；**不按名字匹配、不替调用方消解二义** |
| `import` | 唯一编排者：队列、归属、命名、m3u、落库 | 不做判定（调 `classify`/`platform`）、不做解压（调 `archive`） |
| `query` | 只读检索、排序、筛选 | **绝不写库** |
| `detail` | 组装只读 `RhGameDetail` | 不格式化文字、不写库、不删文件 |
| `manage` | 编辑/删除/碟片/补丁/统计/完整性 | 不做检索展示 |
| `export` | 遍历输出、模板渲染、媒体复制/压缩 | 不改库 |
| `job` | 作业生命周期、进度、取消、队列持久化 | 不懂业务 |
| `store` | 唯一持有 SQL | 不做业务判断 |

外加一条硬规则：除 `store.c` 外任何源文件出现 SQL 语句即为错误（可用 `grep` 进 CI 检查）。

---

## 6. 前端接口与线程模型

### 6.1 `rh_ui` 接口

核心层只认这张表，不认终端也不认 Qt；CLI 与 Qt 各实现一份。

```c
typedef struct RhUi {
  void *userdata;

  void (*log)      (void *ud, RhLogLevel lv, const char *msg);

  /* 进度：pct < 0 表示不确定进度（脉冲模式）。工作线程只允许调这一个回调 */
  void (*progress) (void *ud, double pct, const char *label);

  /* 确认：返回 1 = 是，0 = 否 */
  int  (*confirm)  (void *ud, const char *title, const char *msg, int default_yes);

  /* 单选：返回选中索引，-1 = 取消。
     remember_key 非空时，前端可提供"应用到本次所有/全部"选项；
     已记忆的选择由 ctx->batch_memo 查询，核心层不再持有全局状态 */
  int  (*choose)   (void *ud, const char *title, const char **items, int n,
                    const char *remember_key);

  /* 文本输入：返回新分配的字符串（调用方 g_free），NULL = 取消 */
  char *(*enter)   (void *ud, const char *title, const char *label, const char *def);
  char *(*password)(void *ud, const char *title, const char *label);

  /* 文件/目录选择：返回 GPtrArray<gchar*>（调用方 g_ptr_array_unref） */
  GPtrArray *(*pick_files)(void *ud, const char *title, int multiple);
  char      *(*pick_dir)  (void *ud, const char *title);

  /* 只读文本展示（详情、报告） */
  void (*text_view)(void *ud, const char *title, const char *text);

  /* 媒体类型判定：返回 RhMediaKind 枚举，-1 = 取消；
     remember_key 语义同 choose */
  int  (*ask_media_kind)(void *ud, const char *path, const char *remember_key);
} RhUi;
```

**"应用到全部"的正确实现**：`choose` / `ask_media_kind` 的 `remember_key` 让前端把选择记在自己的作用域里（Qt 里是对话框实例字段，CLI 里是栈上变量），核心层通过 `ctx->batch_memo` 查询已记忆的选择。核心层不持有全局状态，选择只在当前作业内有效。

### 6.2 线程模型

| 线程 | 允许做什么 | 禁止 |
|---|---|---|
| 主线程 | Qt 事件循环 / CLI 主流程；创建 `RhCtx`；提交作业 | 长耗时文件操作 |
| 工作线程（核心 `GThreadPool`；GUI 可用 `QThreadPool` 提交作业） | 只调核心 API 与 `ui->progress` | 直接操作任何 QWidget/QObject UI 对象、调 `confirm/choose/enter` 等阻塞式回调 |
| SQLite | 每线程一个连接（`store_open_thread()`），WAL 下多连接安全 | 跨线程共用一个 `sqlite3*` |

会阻塞用户的决策（确认、选平台、选图片类型）**不发生在执行阶段**。导入被拆成两阶段：

| 阶段 | 线程 | 交互 | 内容 |
|---|---|---|---|
| ① 预扫描 | 主线程 | 允许（集中提问，一次问完） | 分类到类型/平台、读压缩包索引（不解压）、算重名相似度、判媒体类型、判二义扩展名；产出计划表「将导入 N 游戏 / M 媒体 / K 补丁 / 跳过 J 个（附原因）」；所有需要用户拍板的问题在此一次性处理，答案进 `ctx->batch_memo` |
| ② 执行 | 工作线程 | **零交互** | 纯文件与库操作：复制/改名/m3u/写库。总数已知，进度为确定百分比，可暂停/取消 |

这样做的原因：若允许工作线程中途停下来等弹窗，一个二义扩展名就能卡住整条队列；若采用"遇到决策项就跳过、第二轮再问"，用户要在两轮之间来回切换且第二轮数量不可预期。

兜底：执行阶段仍可能遇到预扫描后环境变化（源文件被移走、目标目录被占用、包内条目与索引不符）。此类情况**不中断整批**，跳过该条目并写入 `job_items.message`；`job_items.state='needs_input'` 仅作为兜底状态保留，作业结束后统一列出待人工处理的条目。

取消：`ctx->cancel_flag`（`atomic_int`），核心层在每个条目边界检查一次；`job_items` 状态确保取消后可从断点继续。

---

## 7. 扩展点

原则：**能声明的用文件，不能声明的才进代码**；先定义扩展点，不做通用插件框架。C 的 ABI 脆弱，过早引入 `.so` 会让内部结构体从此冻结，而当前没有第三方生态。

### 7.1 数据型扩展（无需写代码）

| 扩展点 | 载体 | 说明 |
|---|---|---|
| 平台规则 | `platforms.conf` | 格式保持稳定。现格式为"首段后缀表 + `key=value` 键值对"（`nes:.nes;.unf;.fds;compress_supported=true;recommended_format=zip;…`），**新增属性不需要改解析器**（未知键被忽略），因此它已经是数据驱动，无需模板引擎。建议给段头加 `spec_version`，格式变化时明确报错而不是静默读错 |
| 前端规格 | `data/frontends/<id>.conf` | 一份文件同时定义该前端的**导入字段映射**与**导出排版模板**（§7.2）；用户可放 `~/.config/rom-hamster/frontends/` 覆盖，优先级与配置一致 |
| 导出选项 | `export` 配置段 | 目录、是否带媒体、是否压缩、是否生成 m3u |

### 7.2 前端规格文件（导入 + 导出一份）

同一前端的导出字段与导入字段是对称的，所以不为侧各写一套硬编码，而是**每个前端一份规格文件、两面描述**。加新前端 = 加一个 `.conf`，不改代码。

```ini
[frontend]
id           = esde
name         = EmulationStation-DE
spec_version = 1
escape       = xml                  ; 该前端默认转义
layout_rom   = %platform%/%rom_file%      ; ROM 输出相对路径
manifest     = %platform%/gamelist.xml    ; 清单路径
media_dir    = %platform%/media           ; 媒体目录

[export.header]
<?xml version="1.0"?>
<gameList>

[export.game]
  <game>
    <path>./%rom_file:xml%</path>
    <name>%name:xml%</name>
%if:description%    <desc>%description:xml%</desc>
%endif%%if:rating_norm%    <rating>%rating_norm%</rating>
%endif%%if:releasedate_es%    <releasedate>%releasedate_es%</releasedate>
%endif%%if:developer%    <developer>%developer:xml%</developer>
%endif%%if:genre%    <genre>%genre:xml%</genre>
%endif%
  </game>

[export.footer]
</gameList>

[import]
file          = %platform%/gamelist.xml
format        = xml                 ; xml | lines
record        = /gameList/game      ; xml: XPath 1.0 相对表达式
field.name    = ./name
field.description = ./desc
field.cover   = ./image             ; 仅上游 EmulationStation 使用；ES-DE 的 gamelist 无此标签
field.marquee = ./marquee
field.video   = ./video
field.rom     = ./path
```

**两处格式事实**（按 ES-DE 源码与文档核对）：

1. **ES-DE 的 gamelist.xml 不读媒体标签**。它的 `GamelistFileParser.cpp` 里没有 `image`/`marquee`/`video`/`thumbnail`/`fanart`，媒体靠"文件名匹配"从 `~/ES-DE/downloaded_media/<system>/<type>/` 取。所以 ES-DE 模板**不输出** `<image>`，媒体另行落位；而上游 EmulationStation（RetroPie）确实读这些标签。两者机制不同，**不能共用一份模板**——这正是"每前端一份 `.conf`"的实际收益。
2. `<releasedate>` 固定为 `YYYYMMDDT000000`、`<rating>` 为 0.0–1.0，故由代码算出 `releasedate_es` 与 `rating_norm` 注入，模板不做算术。

媒体落位由 `[export.media]` 段声明目标路径（代码负责复制、保留源扩展名）：

```ini
[export.media]
covers      = %media_dir%/covers/%game_name%
screenshots = %media_dir%/screenshots/%game_name%
videos      = %media_dir%/videos/%game_name%
```

Pegasus 那份用 `format = lines` + `record = game:` + `field.name = name`（前缀式），字段集相同、语法不同；资产键取自 Pegasus 源码 `Assets.h`，注意是 `assets.titlescreen`（全小写），不是 `titleScreen`。

**模板语法是闭集，只有四类能力**：

| 能力 | 语法 | 说明 |
|---|---|---|
| 变量替换 | `%name%` | 变量名取自下方闭集；**未定义变量在加载期报错**，绝不静默留空（静默空值会让清单字段错位） |
| 转义修饰 | `%name:xml%` / `:json` / `:text`（默认） | XML 五字符、JSON 引号自动处理，模板作者不必关心转义 |
| 条件 | `%if:cover_rel%…%else%…%endif%` | 仅判真值（非空/非零），不做比较与算术；嵌套 ≤ 3 |
| 循环 | `%for:covers%…%endfor%` | 一个游戏多张图/多补丁；为未来 JSON 前端的数组结构留路 |

可用变量（由代码计算好后注入，模板只排版）：

- 游戏：`id name game_main_name platform platform_export_name description description_line developer publisher year genre players rating rating_norm releasedate_es region tags disc_no is_multi_disc is_arcade rom_status rom_format dat_source patch_type`
- 路径：`rom_file rom_rel rom_size cover_rel screenshot_rel marquee_rel video_rel miximage_rel intro_rel patcher_rel`
- 上下文：`export_dir platform_dir platform_name media_dir game_name platform_extensions emulator_launch`（`emulator_launch` 仅用于前端规格 `launch` 字段；本项目不启动模拟器）

`rating_norm`（10 分制 → 0.0–1.0）、`releasedate_es`（→ `YYYYMMDDT000000`）、`description_line`（压掉换行）都是**代码算好的派生变量**：模板语法里没有算术与字符串函数，这类换算只能在代码里做。

**明确留给代码（模板不做）**：遍历与筛选、相对路径计算、媒体/ROM 的复制与压缩、m3u 生成、多碟隐藏条目规则、路径冲突处理、进度与取消。这些是逻辑，塞进模板就得写表达式引擎。

成本：模板引擎一次性约 600–900 行 C（词法、变量校验、条件/循环、带行号错误报告）。3 个前端时不如硬编码（每个约 150 行）划算，但预期会继续加前端、且用户可自加，从第 4 个起净赚。

### 7.3 代码型扩展（vtable 兜底）

模板表达不了的结构性差异（JSON 嵌套 `game→media[]`、以 SQLite 作为前端库、需联网查询）才走代码后端。

```c
/* 导出后端：仅当模板无法表达时注册 */
typedef struct RhExportBackend {
  const char *id;
  const char *display_name;
  unsigned    api_version;        /* 固定 RH_PLUGIN_API_VERSION */
  int (*prepare)(RhCtx *, const RhExportOpts *, RhErr *);
  int (*emit_game)(RhCtx *, const RhGame *, const char *outdir, RhErr *);
  int (*finish)(RhCtx *, const char *outdir, RhErr *);
} RhExportBackend;

/* 导入处理器：非清单形态的整库（PSV 等）无规格可写，必须代码 */
typedef struct RhImportHandler {
  const char *id;
  unsigned    api_version;
  int  priority;                  /* 小的先试 */
  int  (*detect)(const char *path);                       /* 0 = 命中 */
  int  (*handle)(RhCtx *, const char *path, RhErr *);
} RhImportHandler;
```

注册方式：`plugin.c` 中两张显式数组，链接期确定。将来要动态加载时，在 `plugin.c` 里加 `GModule` 扫描 + 严格比对 `api_version`，调用方不变。

首批注册：

- 导出后端：**空**（`es`/`esde`/`pegasus` 全部由 §7.2 模板驱动，不进 vtable）
- 导入处理器：`psv`（PSV 整库特征识别）、`arcade-zip`（街机 ZIP 多文件判定，若规格表达不顺则走代码）

### 7.4 明确不做为扩展点

- DAT 解析器：只支持 Logiqx 一种格式。真需要别的格式再开。
- 单游戏图文 HTML 页：GUI 原生详情页已覆盖图文与视频，**不维护第二套渲染器**。若将来确实要"导出可分享的单游戏页"，按 §7.2 加一份 `detail-html.conf` 规格复用同一套变量，而不是另造渲染器。
- 数据库后端：不做多后端抽象（§13）。
- 缩略图缓存索引：若将来需要百万级缓存键查询，才考虑 LMDB/gdbm 这类 kv 库 —— 不是现在。

### 7.5 刮削扩展点（只定契约，不实现）

刮削在本项目里被当作"导入的第三种来源"（前两种是本地压缩包/散文件、前端清单），**不另造一套并行的媒体模型**：抓回来的东西仍然走 `store_media_add_sized` 落 `media` 表、按 `<platform>/<main>/<kind>/` 落盘，因此 GUI、导出、去重全部自动复用。首个版本不实现（§1.2 排除项），**当前也不排期**：本节只固定接口形状，避免以后接进来时改动 schema 或改坏导出/GUI。

**为什么不是现在写**：网络栈会改动 §8 依赖与 deb `Depends`；来源溯源要动 schema（v2）。把契约先写死，M3/M4 的导出与 GUI 就不必为"将来可能有网络媒体"预留分支。

**将来真要做时，方向已定（离线优先）**：先做本地数据包导入（`manifest.tsv` + `media/`，按 sha1→md5→crc32 匹配，用户或社区自备，不随包分发版权素材），再做图片源（Libretro Thumbnails 目录按 ROM 名匹配），最后才考虑在线 API（ScreenScraper 需开发者凭证，境外源在境内网络可达性差异大）。在线源一律默认关闭、可覆盖 endpoint、失败不阻塞导入。

#### 7.5.1 数据型：`data/scrapers/<id>.conf`

与 `data/frontends/<id>.conf` 同构：一份文件描述一个来源，INI + 路径表达式，**不引入 JSON 解析依赖**（当前依赖集没有 json-glib；若将来确需深层嵌套，再单议）。用户放 `~/.config/rom-hamster/scrapers/` 覆盖。

```ini
[scraper]
id          = screenscraper
name        = ScreenScraper.fr
spec_version = 1
api         = https://api.screenscraper.fr/api2/jeuInfos.php
auth        = devid,devpassword,ssid,sspassword   ; 只写键名，值在 secrets.conf
rate_limit  = 1/s             ; 令牌桶，按来源声明
timeout     = 30
cache_ttl   = 30d
max_retry   = 3

[query]
platform = %platform_id%      ; 走 [platform_map] 映射，不硬编码
sha1     = %sha1%
md5      = %md5%
crc      = %crc32%
name     = %game_name%        ; 仅在哈希全无命中时退化使用
language = %scrape_lang%

[platform_map]
nes = 3                       ; 本地平台 id → 来源平台 id

[response]
game        = response.jeu
name        = nom
description = synopsis[%scrape_lang%]
developer   = editeur
publisher   = editeur
year        = dates[release].text
genre       = genres[0].noms[%scrape_lang%]

[media]
covers      = medias[type=box-2D][region=%scrape_region%].url
screenshots = medias[type=screenshot][region=%scrape_region%].url
marquees    = medias[type=wheel][region=%scrape_region%].url
videos      = medias[type=video-normalized][region=%scrape_region%].url
```

路径表达式是**闭集**：`a.b`、`a[0]`、`a[key=value]`、`a[%var%]`，取不到即视为"该来源没有这项"，**不报错、不写空值**。字段名取不到的来源由配置作者负责，加载期校验 `[scraper]` 必填项。

#### 7.5.2 代码型：`RhScraper` vtable

规则文件只能描述"怎么问、怎么取"，**分页、限速、鉴权失败重试、媒体类型推断**仍是代码。结构体进 §7.3 同一套 vtable 范式，`plugin.c` 显式数组注册：

```c
typedef struct RhScrapeMatch {
  gchar *source_id;      /* scraper id */
  gchar *source_url;     /* 命中详情页/API 查询 URL，写 media.source_url */
  gchar *name;           /* 来源给出的规范名 */
  gchar *description;
  gchar *developer;
  gchar *publisher;
  gint   year;
  gchar *genre;
  gchar *media_url[RH_MEDIA_KIND_COUNT];   /* 按 kind 索引，可空 */
} RhScrapeMatch;

typedef struct RhScraper {
  const char *id;
  unsigned    api_version;                       /* RH_PLUGIN_API_VERSION */
  gboolean    requires_network;
  int (*probe)(RhCtx *, RhErr *);                /* 凭证/可达性检查，不联网也可返回可用 */
  int (*match)(RhCtx *, const RhGame *, const RhScrapeOpts *, RhScrapeMatch **, RhErr *);
  int (*fetch)(RhCtx *, const RhScrapeMatch *, const char *kind,
               const char *dest_path, RhErr *);  /* 下载到临时文件，由调用方 copy_atomic 落位 */
} RhScraper;
```

`fetch` 只负责拿到临时文件，**落位与入库仍由 `import.c` 的同一套 `rh_fs_copy_atomic` + 单条事务完成**，保证与本地导入同样的幂等语义（0/2/3/1）。

#### 7.5.3 配置与凭证

新增配置键（builtin 默认，均为可选）：

| 键 | 默认 | 说明 |
|---|---|---|
| `scrape_enabled` | `false` | 总开关；关闭时任何刮削命令直接返回 0 并提示未启用 |
| `scrape_providers` | `""` | 按顺序尝试的 id 列表，空 = 不启用 |
| `scrape_lang` | `""` | 空 = 跟随 `lang` |
| `scrape_region` | `""` | 空 = 来源默认区域 |
| `scrape_overwrite` | `false` | `false` = 只补缺，不覆盖已有媒体 |
| `scrape_timeout` | `30` | 秒 |
| `scrape_rate_per_min` | `30` | 全局令牌桶上限，来源声明更低时取小 |
| `scrape_cache_dir` | `""` | 空 = 缓存目录下的 `scrape/`（由 `rh-path` 决定，Linux 为 `$XDG_CACHE_HOME`、Windows 为 `%LOCALAPPDATA%`） |

凭证**不进五层配置**：单独 `~/.config/rom-hamster/secrets.conf`，权限必须 `0600`，加载时校验，过宽则拒绝读取并报错；`--verbose`/日志/错误信息一律脱敏（只打印 key 名，不打印值）。

#### 7.5.4 查询与写回策略（契约）

1. 查询键优先 **sha1 → md5 → crc32**（抗碰撞强到弱，与 DAT 匹配一致；与 `dat_match_hash` 解耦，刮削按来源支持能力发送可用键），全无命中才用 `platform + name`，且名称先按 DAT 规范名归一（`game_main_name`），再退原始目录名。
2. 同一游戏多来源命中时按 `scrape_providers` 顺序取第一个，**不合并字段**（避免来源混搭出无法追溯的记录）。
3. 媒体写回：`scrape_overwrite=false` 时同 `(game_id,kind)` 已有文件即跳过并计数 `skipped`；`true` 时先写临时文件再原子替换。
4. 失败语义：单游戏失败只记 `failed` 并继续下一个，**绝不中断整批**；离线/超时按 `max_retry` 退避后放弃。
5. 缓存：命中响应按 `game_id + source_id` 缓存 `cache_ttl`，缓存命中不发请求；`--refresh` 强制绕过。

#### 7.5.5 来源溯源（schema v2）

`media` 表新增三列（v1 → v2 迁移，默认值兼容 v1 库）：

| 列 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `source` | TEXT | `'local'` | `local` / `library` / `<scraper id>` |
| `source_url` | TEXT | `NULL` | 抓取时的查询或详情 URL |
| `fetched_at` | INTEGER | `NULL` | Unix 秒；本地导入为 NULL |

这样 GUI 能显示"这张封面来自 ScreenScraper"，导出时也能按来源筛选；不引入侧表，避免每次读媒体都要 join。

#### 7.5.6 依赖影响

- **新增 `libcurl`**（`libcurl4`，最低 7.74）。它只被核心库里的 `scraper*.c` 使用，但动态链接后仍是 `rom-hamster` 包的硬依赖；若坚持"纯离线安装不装 curl"，构建期加 `-DSCRAPER=OFF` 并在 deb 里拆出 `rom-hamster-scraper`（二选一，将来实现时拍板）。
- 不引入 json-glib / libsoup；HTTP 只走 libcurl，JSON 用来源自带的最小解析（或后续单议）。
- 离线可用性不变：`scrape_enabled=false`（默认）时进程启动与导入路径**不触碰任何网络代码**。

---

## 8. 依赖清单

### 8.1 构建期

| 依赖 | 最低版本 | 用途 |
|---|---|---|
| C 编译器 | gcc 11 / clang 14 / MSVC 2022 | C11（核心、CLI）+ C++17（GUI） |
| CMake | 3.22 | 构建 |
| Ninja | — | 后端（推荐） |
| pkg-config | — | Linux 依赖探测 |
| Qt 6 SDK | ≥ 6.8（发布包固定 6.8 LTS） | GUI 构建；仅 `-DGUI=ON` 时需要 |
| gettext | — | CLI 的 `msgfmt` |
| Inno Setup | 6.2 | Windows 安装包 |
| linuxdeploy / appimagetool | — | AppImage |
| debhelper / dpkg-dev | — | deb 打包 |
| lintian | — | deb 校验 |

### 8.2 运行期（核心库）

| 库 | Debian 包 | 最低版本 | 用途 |
|---|---|---|---|
| GLib | `libglib2.0-0` | 2.66 | 容器/字符串/线程/配置/`GChecksum`(md5,sha1)/GIO |
| SQLite | `libsqlite3-0` | 3.35（FTS5 trigram 需 3.34+） | 元数据库 |
| libarchive | `libarchive13` | 3.4 | zip/7z/rar/tar 读写、密码、加密探测 |
| zlib | `zlib1g` | — | CRC32、deflate |
| libxml2 | `libxml2` | 2.9 | Logiqx DAT、`gamelist.xml` 流式解析 |

Windows 侧这 5 个库随安装包分发（MSYS2/vcpkg 构建），不要求用户预装；`libromham.dll`、CLI 与 GUI 使用同一份运行时。

**已落地的 Windows 工具链**：MSYS2 + mingw-w64（GCC 15）+ `mingw-w64-x86_64-*` 核心五库与 `qt6-base`，用 Ninja 构建；MSVC 2022 + vcpkg 为备选路径，两者都必须能通过同一份 `CMakeLists.txt`。

### 8.3 GUI 与可选依赖

| 库/程序 | 用途 | 缺失/关闭时行为 |
|---|---|---|
| Qt 6 Core/Gui/Widgets/Network（**6.8 LTS**） | GUI 的 Qt 运行库；Network 只用于启动时的版本检查；随 deb/AppImage/Windows 包分发 | 仅 CLI 可构建；`-DGUI=OFF` |
| Qt 6 Multimedia（FFmpeg 后端，**可选构建** `-DMULTIMEDIA=ON`，默认 OFF） | 详情页内嵌播放视频（`QMediaPlayer`/`QVideoWidget`） | 默认关闭；视频一律 `QDesktopServices::openUrl` 交外部播放器打开 |
| Qt 图像插件（webp 等） | `.webp` 缩略图（`media_suffix_rules` 允许 webp） | 该格式无缩略图，其余正常 |
| `libchdr` | CHD 只读（后续若要识别/校验 CHD） | 仅记录格式字符串 |
| `chdman`（外部） | CHD 转换 | 不做转换 |
| `unrar`（外部） | RAR5 加密包（libarchive 对部分 RAR5 加密不支持） | 报告该包无法读取，不中断整批导入 |
| `libcurl4`（**将来实现刮削时**） | 刮削的唯一网络栈（§7.5） | M0–M5 不链接；`-DSCRAPER=OFF` 时不进依赖 |

版本检查只走 Qt Network（GUI 侧，`QNetworkAccessManager`），**不引入 libcurl**；CLI 不做版本检查。

Qt 许可与分发：本项目按 LGPLv3 **动态链接** Qt，安装包内附 Qt 许可证文本与源码获取地址，不阻止用户替换 Qt 动态库；不静态链接 Qt。Qt 版本随安装包固定，升级 Qt 视为一次正常发布。

### 8.3.1 国产发行版（统信 UOS）

| 版本 | 白皮书里的一手数据 | 对本项目的含义 |
|---|---|---|
| **统信桌面操作系统 V25** | 《V25 产品白皮书》表 4「重要技术指标」：glibc 2.38、gcc 12.3、**Qt 6.8.0/5.15**、内核 6.6、systemd 255.2 | Qt 6.8 与 glibc 2.38 均满足 §1.3 下限，GUI 可全功能 |
| **统信桌面操作系统 V20** | 《V20 白皮书》表 3-7「指标参数」：内核 4.19/5.10、DDE 5.4、Xorg 1.20、**glibc 2.28**、gcc 8.3、Qt 5.15 | glibc 2.28 低于 §1.3 下限，Qt 5.15 也不满足 Qt 6.8；本程序不支持 V20 |

结论：V25 是支持的国产桌面平台；V20 不在支持范围。两前端共享核心的收益体现在"CLI 与 GUI 同版本发布"，不再用于兼容老 glibc。

真机核对：

```
pkg-config --modversion glib-2.0; sqlite3 --version; qmake6 -query QT_VERSION; ldd --version | head -1
```

### 8.4 测试/CI 依赖

`valgrind`、`libglib2.0-dev`（含 GTest）、`ninja`、Qt 6 Test。CI 至少四组构建：

1. Linux `-DGUI=ON`
2. Linux `-DGUI=OFF`（验证核心不沾 Qt）
3. Windows `-DGUI=ON`
4. Windows `-DGUI=OFF`

外加 Linux 的 `-DSANITIZE=address,undefined` 构建（CMake 选项，内部展开为编译与链接标志）。依赖断言进 CI：Linux `ldd libromham.so` 与 Windows `dumpbin /dependents romham.dll` 不得出现 `Qt6*`/`gtk*`/`gdk*`/`pango*`。

CI 入口是 `ci/build-matrix.sh`：在 Linux 上跑 `linux-cli`/`linux-gui` 两个预设并在末尾补一组 `-DRH_SANITIZE=address,undefined`；在 MSYS2(MINGW64) 上跑 `windows-cli`/`windows-gui`。两个平台各跑一次即覆盖四组矩阵。

远端自动执行：`.github/workflows/ci.yml` 在 GitHub 镜像上按 push/PR 触发，四个 job 与上面四组一一对应（Linux 两份在 ubuntu-24.04，Windows 两份在 windows-latest + MSYS2 MINGW64，GUI 的 Qt 分别用 aqt 6.8 与 `mingw-w64-x86_64-qt6-base`）。本地脚本保留作离线兜底；Gitee 主仓目前不跑流水线，推送镜像即触发。

**当前验证状态（2026-11-17，M1 进行中）**：四组在真机跑通，且 M1 已完成 `rh-str`、`rh-records`、`store` 三个模块——

| 平台 | 工具链 | CLI 预设 | GUI 预设 | `ctest` |
|---|---|---|---|---|
| Linux（WSL Ubuntu） | CMake 4.2.3、GCC 15.2、GLib 2.88、SQLite 3.46、Qt 6.10.2 | 全部目标 | 全部目标 | 各 **13/13** |
| Windows 11 x64（MSYS2 MINGW64） | CMake 4.5、GCC 16.2、GLib 2.90.1、SQLite 3.54.1 | 35 目标 | 59 目标 | 各 **11/11** |

用例构成：`core/rh-err` 1、`core/store` 10、`core/path` 4、`core/str` 8、`core/records` 5、`core/platform` 4、`core/library` 2、`core/classify` 2、`core/import` 2、`core/no-gui-linkage` 1、`tools/make-ico` 1（合计 40 个用例）；Linux 另有 `cli/version-utf8`、`cli/version-utf8-tty` 各 1（合计 42，Windows 无 `script` 故仍为 40）。Windows 侧同样在含中文的项目路径下通过，`core/path` 覆盖了 UTF-8 入口；两侧构建均为 **0 warning**。Windows 侧同样在含中文的项目路径下通过，`core/path` 覆盖了 UTF-8 入口。

**仍未做**：`dumpbin /dependents` 对 Windows 版 `libromham` 的 Qt 符号断言尚未执行（`core/no-gui-linkage` 在 Windows 跳过了 `ldd`），按 §8.4 安排到 M5 打包阶段补做。不要把这一条算作已通过。

**托管与流水线计划**：Gitee 为主仓（唯一提交入口），GitHub 同名仓库为单向镜像；日常先推主仓再推镜像，步骤见 README「仓库与同步」。CI 计划走 GitHub Actions——Windows 用 `msys2/setup-msys2` 跑 `windows-cli`/`windows-gui`，Linux 组在 Ubuntu 上跑并附 sanitizer，统一调用 `ci/build-matrix.sh` 或等价的预设命令。**当前不接**，等代码量上来或需要 PR 门禁时再启用；在此之前由人工在两个平台的终端执行。

---

## 9. 打包与安装布局

### 9.1 交付矩阵

**Linux · deb 三包**

| 包 | 内容 | 依赖 |
|---|---|---|
| `rom-hamster` | `/usr/bin/rom-hamster`、`/usr/lib/x86_64-linux-gnu/rom-hamster/libromham.so.1`（SONAME，实文件 `libromham.so.1.0.0`）、`/etc/rom-hamster/rom-hamster.conf`、`/etc/rom-hamster/platforms.conf`、man page | `libglib2.0-0 (>= 2.66)`, `libsqlite3-0 (>= 3.35)`, `libarchive13`, `zlib1g`, `libxml2` |
| `rom-hamster-gui` | `/usr/bin/rom-hamster-gui`、`/usr/lib/rom-hamster/qt6/`（Qt 6.8 Core/Gui/Widgets/Network + TLS 插件）、`.desktop`、`/usr/share/icons/hicolor/{16x16,32x32,48x48,256x256}/apps/rom-hamster.png`、`/usr/share/icons/hicolor/scalable/apps/rom-hamster.svg`、AppStream metainfo | `rom-hamster`（= 同一版本核心）, `libstdc++6`, `libgcc-s1`；**不依赖发行版 Qt** |
| `rom-hamster-data` | `/usr/share/rom-hamster/platforms.conf`（只读出厂默认）、`frontends/*.conf`、`rom-hamster.conf.template`、`LICENSE`、`changelog` | — |

GUI 二进制通过 `rpath` + `qt.conf` 指向 `/usr/lib/rom-hamster/qt6/` 与核心库目录，Qt 库不进入 `/usr/lib/x86_64-linux-gnu`，避免与系统 Qt 冲突。

**Linux · CLI tarball（免安装）**

- `rom-hamster-<ver>-linux-x86_64.tar.gz`：`bin/rom-hamster`、`lib/libromham-1.0.so`、`share/rom-hamster/`、`LICENSE`。**不含 Qt**。
- 解压后设 `LD_LIBRARY_PATH=$PWD/lib` 或走 `bin/rom-hamster` 的 `$ORIGIN` rpath 即可运行；面向不能/不愿装 deb 的环境。
- 只提供 CLI；GUI 的免安装形态用 AppImage。

**Linux · AppImage（GUI 单包）**

- 只用于 GUI：内含 `rom-hamster-gui`、`libromham`、Qt 6.8 运行库与插件；构建基线 Ubuntu 22.04。
- `--cli` 保留为**应急入口**（图形会话损坏时用），但**不单独出 CLI AppImage**；CLI 的免安装形态是上面的 tarball。
- 桌面集成由 AppImage 的 `.desktop` 与图标提供；不写入系统目录。

**Windows 11 x64**

| 产物 | 内容 | 说明 |
|---|---|---|
| `rom-hamster-<ver>-windows-x64.zip` / `...-windows-x64-cli.zip` | `bin/rom-hamster.exe`、`bin/libromham.dll`、MinGW 运行时 DLL（glib/intl/pcre2/iconv/sqlite3/libxml2/zlib…）、GUI 版另加 `bin/rom-hamster-gui.exe` 与 `Qt6*.dll`、`bin/{platforms,imageformats,styles}/`、`etc/rom-hamster/`、`share/` | 便携版，解压即用；目标机不需要 MSYS2、Qt 或 MinGW 运行时 |
| `rom-hamster-<ver>-win64-setup.exe` | 同上 + Inno Setup 脚本产物 | 安装到 `%ProgramFiles%\rom-hamster`；开始菜单；可选加入 PATH；卸载保留 `%APPDATA%` 用户数据 |

**zip 必须自包含**：`libromham.dll` 与 CLI/GUI 的传递依赖（glib、gio、gobject、gmodule、intl、iconv、pcre2、zlib、sqlite3、libxml2、libarchive 及其依赖，以及 GUI 的 `libstdc++`/`libgcc`/Qt）都要复制到 `bin/`。`packaging/windows/package-zip.ps1` 用 `objdump -p` 递归读 PE 导入表，凡能在工具链目录解析到的 DLL 一律复制（解析不到的视为 Windows 系统 DLL），GUI 版先跑 `windeployqt` 再补非 Qt 依赖。Qt 动态库由 `windeployqt` 收集；MSVC 场景由 `windeployqt --compiler-runtime` 或随包 VC++ Redist 解决。**不提供 MSIX/商店包**；**不做代码签名**，发布页公布 SHA-256 校验和与构建来源，并说明 SmartScreen 提示。

验收必须在**干净环境**做：把 `PATH` 限制为 `%SystemRoot%\System32;%SystemRoot%` 后运行 `bin\rom-hamster.exe --version`。只在仓库目录里跑会命中开发机上 `msys64\mingw64\bin` 里已有的 DLL，从而把「缺 DLL」测成「可运行」——这正是早期「解压 zip 后能跑」的结论失效的原因。脚本第 4 步内置了这条检查，缺依赖会直接失败而不是产出坏包。

### 9.2 配置文件的安装位置

Linux（遵循 Debian Policy）：

- `/usr/share/rom-hamster/*` —— 出厂默认，属 `rom-hamster-data`，**任何进程不得写入**；
- `/etc/rom-hamster/*.conf` —— 系统管理员覆盖层，登记为 conffile；
- `~/.config/rom-hamster/*.conf` —— 用户层。

Windows：

- `<install>\share\rom-hamster\*` —— 出厂默认，只读；
- `%ProgramData%\rom-hamster\*.conf` —— 系统层；
- `%APPDATA%\rom-hamster\*.conf` —— 用户层。

配置优先级（低 → 高）：内置默认 → 安装目录出厂默认 → 系统层 → 用户层 → 命令行参数（详见 §10.1–§10.2）。任何层都不写入安装目录。

zip 形态下 `<install>` 指解压根目录（即 `bin\` 的上一级）；查找必须基于**可执行文件自身路径**（`GetModuleFileNameW`）而不是当前工作目录，否则从任意目录启动就找不到 `etc/`、`share/`。M1 的 CLI 尚未实现 `rh-config`，`--version` 不读取任何配置文件，因此这两个目录目前只是随包安装、尚未被读取；配置加载在 M2 接入时按上述规则补「exe 相对路径」这一层。

### 9.3 命名与命令集

- CLI 为 `rom-hamster`（Windows 为 `rom-hamster.exe`），子命令 `init / import / list / search / show / edit / delete / export / stats / config / help / version`。
- GUI 为 `rom-hamster-gui`（Windows 为 `rom-hamster-gui.exe`）；Linux 桌面文件 `Exec=rom-hamster-gui`。
- AppImage 的应急 CLI 入口为 `rom-hamster.AppImage --cli …`；CLI 的免安装形态为 `rom-hamster-<ver>-linux-x86_64.tar.gz`。
- 图标资产：素材在 `resources/icons/`——`rom-hamster.svg` 为源文件（512×512，内嵌 512×512 位图，非真矢量），`rom-hamster-{16,32,48,256}.png` 由它导出并入库，`preview-64.png` 仅供评审（不入 `.qrc`、不安装）。GUI 经 `resources/resources.qrc` 的 `/icons` 前缀加载 PNG，SVG 不进 `.qrc`（避免引入 QtSvg）；Windows 的 `rom-hamster.ico` 由构建从 `rom-hamster-256.png` 生成。安装时 CMake 将四个 PNG 与 SVG 映射为 hicolor 布局（`/usr/share/icons/hicolor/<尺寸>/apps/rom-hamster.png`、`.../scalable/apps/rom-hamster.svg`），Windows 装 ICO（窗口图标同时用 PNG）。SVG、四个 PNG、ICO 必须保持同一图形。
- 不提供与其他 ROM 管理器或历史版本的行为兼容层、配置兼容层、元数据转换层。

### 9.4 升级语义（三端一致：原地覆盖）

| 渠道 | 升级方式 | 要点 |
|---|---|---|
| deb | `apt`/`dpkg` 原地升级 | `/etc` conffile 按 Debian 规则保留或提示；`/usr/share`、`/usr/bin`、`/usr/lib` 由新包覆盖；**不要求先卸载** |
| Windows zip | 解压覆盖同一目录 | 覆盖前关闭运行中的实例；用户配置在 `%APPDATA%`，不受影响 |
| Windows 安装包 | 同一 AppId、同一安装目录覆盖安装 | 安装前检测并关闭运行中的 `rom-hamster-gui.exe`/`rom-hamster.exe`；开始菜单与卸载项复用，不产生第二份安装 |
| AppImage | 直接替换文件 | 不写系统目录；用户配置与库不受影响 |

**用户数据与库不随安装/卸载动作变化。** 每次启动（CLI 与 GUI）都检查 `PRAGMA user_version`：低于当前版本则迁移；高于当前版本则只读并提示升级程序（§3.4）。

卸载只删安装目录，不删库目录、`~/.config/rom-hamster/`、`%APPDATA%\rom-hamster\` 与缓存；是否清理缓存由用户手动决定。

---

## 10. 配置系统

沿用 `GKeyFile` 的 `[default]` 段。键名是公开契约：新增键只追加，不重命名；下表是基础清单，刮削相关键见 §7.5.3。

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `ROM_ROOT` | path | 空（首次运行引导） | ROM 收藏根目录 |
| `dat_dir` | path | 空（默认 `<ROM_ROOT>/dats`） | DAT 存放目录 |
| `archive_extensions` | 分号列表 | `.zip;.7z;.rar` | 识别的压缩包后缀 |
| `archive_compress_export_format` | 枚举 | `zip` | 导出压缩格式（`zip`/`7z`） |
| `duplicate_check_threshold` | 0–100 | `70` | 同平台重名相似度阈值，0 关闭 |
| `dat_match_hash` | 枚举 | `crc32` | DAT 匹配哈希（`crc32`/`md5`/`sha1`） |
| `dat_auto_match_multi_disc` | bool | `true` | 匹配到单碟后自动关联其他碟 |
| `auto_generate_m3u` | bool | `true` | 多碟自动生成 m3u |
| `export_hidden_discs` | bool | `true` | 导出时为多碟生成隐藏条目 |
| `disc_label_prefix` | string | `Disc` | 碟片命名前缀 |
| `auto_delete_source` | bool | `false` | 导入成功后删除源文件 |

新增键：`store_path`（默认 `<ROM_ROOT>/romhamster.db`）、`emulator_launch`（string；仅作为前端规格 `launch` 字段的模板变量，本程序不启动模拟器）、`lang`（默认随系统区域设置）、`update_check`（bool，默认 `true`）、`update_check_url`（string，默认由构建注入；为空则跳过检查）、`update_check_interval_hours`（整数，默认 `24`）。

DAT 查找规则：`<dat_dir>/<platform>.dat`（含 `.DAT` 大小写）精确优先，未命中则按 `aliases` 正则回退扫描 `<dat_dir>` 两层的 `*.dat`。

### 10.1 分层与来源记录

五层（低 → 高）：内置默认 → 安装目录出厂默认 → 系统层 → 用户层 → 命令行。每个键在 `RhConfig` 中存 `{ value, layer, source_file, line }`，后者覆盖前者但保留来源。

| 层 | Linux | Windows |
|---|---|---|
| 内置 | 编译进二进制的最小集 | 同 |
| 出厂默认 | `/usr/share/rom-hamster/` | `<install>\share\rom-hamster\` |
| 系统 | `/etc/rom-hamster/` | `%ProgramData%\rom-hamster\` |
| 用户 | `~/.config/rom-hamster/` | `%APPDATA%\rom-hamster\` |
| 命令行 | `--key=value` | 同 |

路径由 `rh-path` 统一产出：Linux 走 XDG，Windows 走 `%APPDATA%`/`%ProgramData%`；GUI 不自行拼接配置路径。

`rom-hamster config --explain <KEY>` 输出：

```
ROM_ROOT = /home/zhoux/roms
  来自 ~/.config/rom-hamster/rom-hamster.conf:3
  覆盖了 /etc/rom-hamster/rom-hamster.conf:5 的 /srv/roms
```

只打印最终值无法回答"设置为什么没生效"；来源记录让这个问题一句话就能回答。

### 10.2 平台规则的四层

| 层 | 位置 | 谁改 | 性质 |
|---|---|---|---|
| 1 | 编译进二进制 | 无人 | 最小集，保证任何配置文件缺失都能启动 |
| 2 | Linux `/usr/share/rom-hamster/platforms.conf`；Windows `<install>\share\rom-hamster\platforms.conf` | 无人 | `data` 包，只读出厂规则，**实测 101 条**（`[meta] spec_version=1`） |
| 3 | Linux `/etc/rom-hamster/platforms.conf`；Windows `%ProgramData%\rom-hamster\platforms.conf` | 系统管理员 | Linux 侧为 conffile |
| 4 | Linux `~/.config/rom-hamster/platforms.conf`；Windows `%APPDATA%\rom-hamster\platforms.conf` | 用户 | 自行增补平台，无需管理员权限 |

### 10.3 双端路径解析

所有路径经 `rh-path` 产出，业务模块不直接拼字符串：

- **配置/缓存目录**：Linux 走 XDG（`XDG_CONFIG_HOME`/`XDG_CACHE_HOME`）；Windows 走 `%APPDATA%`/`%LOCALAPPDATA%`。
- **长路径**：Windows 超过 `MAX_PATH` 时加 `\\?\` 前缀，并在 UTF-8 与 UTF-16 之间统一转换。
- **入口编码**：Windows 的 `argv` 与 `getenv` 是 ANSI/GBK；CLI 在解析前、测试在 `g_test_init` 前统一转 UTF-8，否则 GLib 的 `g_setenv`/`g_test_build_filename` 会因非 UTF-8 断言失败（已有 `core/path` 回归用例）。
- **大小写**：Windows 文件系统大小写不敏感；查重、`dir_path` 唯一性、媒体同名判断在 Windows 侧按不敏感比较。
- **保留名与结尾字符**：`CON`/`PRN`/`AUX`/`NUL`/`COM1`… 以及结尾点/空格，由 `sanitize_dirname` 在 Windows 侧替换。
- **编码与换行**：配置、清单、导出文件统一 UTF-8 落盘；写入用 `\n`，读取兼容 CRLF。

---

## 11. 正式桌面应用的必做项

| 项 | 要求 |
|---|---|
| 应用生命周期 | `QApplication`；`QLocalServer` 单实例：第二次启动不新开进程，改为激活已有窗口并接收参数（Windows 与 Linux 同一实现） |
| 界面构建 | Qt Widgets + `.ui` + `.qrc` 编译进二进制；不手写整棵控件树；明暗主题跟随系统（`QStyleHints::colorScheme`），不做强制主题 |
| 主窗口列表 | `QTableView` + 自定义 `QAbstractTableModel`，按需分页读取核心；行高固定、按视口取数，保证万行级滚动；筛选条件交给核心 SQL，不在 GUI 侧全量过滤 |
| 详情页（媒体同窗） | 元数据 `QFormLayout`/`QGridLayout` + 8 类资源分页（`QTabWidget`/`QStackedWidget`：ROM文件/封面/截图/视频/横幅/合成图/说明/补丁），与游戏目录的 8 个子目录一一对应。图片经 `QImageReader` 在工作线程解码、按控件尺寸缩放、磁盘缩略图缓存（键 = 路径+mtime+尺寸，目录 `QStandardPaths::CacheLocation/thumbs`）；视频默认 `QDesktopServices::openUrl` 交外部播放器，启用 Qt Multimedia 构建时用 `QMediaPlayer` + `QVideoWidget` 内嵌播放；说明文本用只读 `QPlainTextEdit` 显示原文（**不解析 Markdown**） |
| 图标与元数据 | 素材在 `resources/icons/`：`rom-hamster.svg` 为源文件，`rom-hamster-{16,32,48,256}.png` 由它导出并入库；GUI 用 `QIcon` 从 `.qrc` 的 `/icons` 前缀逐尺寸组装（SVG 不入 `.qrc`，不引 QtSvg）；构建从 `rom-hamster-256.png` 生成 `.ico`；Linux `.desktop`（`Exec=rom-hamster-gui`）+ AppStream metainfo（含截图与发布说明）+ man page |
| 国际化 | CLI：gettext，msgid 用英文，提供 `zh_CN` `.po`；GUI：Qt Linguist，`.ts` 源文件 + `.qm` 编译进 `.qrc`。AppStream 描述必须英文。核心库不含任何用户可见字符串，两套 i18n 互不重叠 |
| 首次运行 | CLI：`ROM_ROOT` 未配置时报错并提示 `rom-hamster init --root <dir>`。GUI：启动即 `QFileDialog::getExistingDirectory` 选目录，选完直接建库入库并进入主窗口，不插入欢迎页 |
| 库版本防护 | 启动时检查 `PRAGMA user_version`：低于当前则迁移；高于当前则只读并提示"请升级 rom-hamster"，禁止写入（§3.4、§9.4） |
| 版本检查 | 仅 GUI：启动后异步 GET `update_check_url` 的 JSON（`{"version":"x.y.z","url":"..."}`），3 秒超时；失败/断网静默，不影响启动；远程版本更高时用非模态提示打开下载页，**不下载、不安装**；结果按 `update_check_interval_hours` 缓存。CLI 不检查 |
| 拖拽导入 | `dragEnterEvent`/`dropEvent` 接收文件与目录列表，落入后**走同一条两阶段导入管线**（不新增代码路径），支持多选与目录递归。进 M4 |
| 参数解析 | CLI：`GOptionContext`，`--help` 自动生成，子命令独立 option group。GUI：`QCommandLineParser` 只处理少量启动参数；其余子命令转交 CLI 逻辑 |
| 日志 | 核心 `g_log_structured`（域 `romham`、`romham/import`、`romham/store`）；GUI `QLoggingCategory` + `qInstallMessageHandler` 统一写入同一日志文件；`--log-file` 可选 |
| 崩溃/中断恢复 | 导入队列存 `jobs/job_items`；启动时 `store_job_list_unfinished()` 提示续做；进程被杀不会丢已完成条目 |
| 写入一致性 | **每个条目一个事务，且把 `job_items` 状态更新与记录写入放进同一事务**：`BEGIN` → 写 `games/roms/media` + 标 `job_items.state='done'` → `COMMIT`。文件侧顺序：复制到临时名 → 原子改名到最终名（先于事务提交）。崩溃发生在提交前：库里无记录、可能留下孤儿文件（由完整性检查发现）；重试靠**幂等复制**（目标已存在且 `size`+`mtime` 一致则跳过，不覆盖）。禁止"先写库后搬文件" |
| 原子写 | 配置/清单落盘一律 tmp + 原子改名；Windows 用 `MoveFileEx`/`QSaveFile` 语义，Linux 用 `rename` |
| UI 不阻塞 | 所有 >50ms 的操作进作业线程；进度条支持暂停/取消；不确定进度用 `pct=-1` 的脉冲模式；工作线程只能经 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 更新 UI |
| 测试 | GLib GTest 单测 + `tests/fixtures` 集成样本 + Qt Test GUI 冒烟 + asan/valgrind job |
| 版本与升级 | `--version` 输出维护者、仓库、bug 渠道；升级时自动跑 `store_migrate()`；Windows 安装包写入文件版本资源 |

**首版 GUI 范围 = 上表列出的主窗口、详情页、导入进度/取消、首次运行、拖拽导入、单实例、版本检查。** 批量编辑器、标签管理面板、统计图表**不进首版**，留待后续扩展；详情页视频默认走外部播放器，Qt Multimedia 内嵌播放属于可选构建，不作为首版验收前提。

---

## 12. 既有收藏的接入与数据独立性

### 12.1 库目录是契约

一个库 = 一个根目录，至少包含：

```
<ROM_ROOT>/
├─ romhamster.db                 # SQLite 元数据库
├─ dats/                         # DAT 文件（可选）
└─ <platform>/
   └─ <game>/
      ├─ rom/ covers/ screenshots/ videos/ marquees/ miximages/ intro/ patcher/
```

- 备份 = 复制整个根目录；迁移 = 移动根目录；外部工具可直接读 SQLite。
- 库内只记录相对路径（`dir_path` = `<platform>/<game>`），根目录搬家后仍可用。
- 库文件必须位于本地文件系统（§3.1）。

### 12.2 接入既有 ROM 文件

- 通过 `import` 扫描用户指定的文件/目录；不假设来源，不要求先整理。
- 若目标库已有 `<platform>/<game>/` 布局，导入按同名/相似度复用目录，不重复复制。
- **不读取任何第三方 ROM 管理器或历史版本的数据库、CSV、配置文件**；也不提供"从其他管理器导入元数据"的命令，仓库不附带历史数据转换工具。
- 元数据以本程序库为准；磁盘上的 ROM/媒体是资产，数据库是索引。两者不一致时由完整性检查报告，不自动删除文件。

### 12.3 质量红线（按功能表述）

以下要求是验收项，任何一条不满足即视为缺陷：

| 要求 | 落点 |
|---|---|
| 单条编辑只更新该条记录，不做整库重写 | §3.5 事务决策 |
| 启动时直接打开数据库，不逐行解析文本库、不逐行起子进程 | §3.1 |
| CRC32 用 zlib 计算，不与外部压缩命令交互；三种哈希按需单次读盘 | §8.2 |
| "应用到全部"的选择在当前作业内有效，换作业重新询问 | §6.1 |
| `year`/`rating` 类型化，排序按数值而非字典序 | §3.3 |
| 一个游戏的多张图/多个补丁全部导出，不只第一张 | §3.3 |
| Pegasus 记录以 EOF 收尾、描述支持续行、补 `assets.screenshot`/`assets.titlescreen` | §7.2 |
| 分类先判真实内容再判 ROM；复合扩展名保留多级语义 | §5 |
| 元数据表单中间字段留空不错位 | §14 M4 |
| Windows 文件名大小写不敏感、保留名与结尾点/空格被清洗 | §15 |

---

## 13. 风险与取舍

| 风险 | 影响 | 对策 |
|---|---|---|
| SQLite 放在 SMB/NFS 上 | 锁不可靠，可能损坏库 | 库文件强制本地；启动时检测 `st_dev` 与 fs 类型，网络盘直接拒绝并说明；将来多机共享才考虑 PostgreSQL |
| C 内存安全 | 崩溃、越界 | asan/valgrind 进 CI；所有权约定写入头文件注释（谁分配、谁释放、是否转移） |
| libarchive 对 RAR5 加密支持有限 | 少数包读不了 | 报"该包无法读取"并继续整批，不中断；可选外挂 `unrar` |
| 大整合包（数百 GB） | 内存/耗时 | 全程流式：libarchive 边读边写，哈希单次读盘，`job_items` 逐条提交 |
| 多实例同时写 | 数据竞争 | GUI 用 `QLocalServer` 保证单实例；跨进程**不加锁文件**，靠 SQLite 自身的串行化写（WAL 下同一时刻只允许一个写者）+ `busy_timeout=5000`。超时报 `SQLITE_BUSY`，提示"另一实例正在写入，请稍后重试" |
| Qt LGPLv3 合规 | 分发被质疑 | 只做动态链接；安装包附 Qt 许可证与源码获取地址；允许用户替换 Qt DLL；不静态链接 |
| Windows 路径与命名 | 导入失败、文件丢失 | `rh-path` 统一 `\\?\` 长路径与 UTF-16 转换；`sanitize_dirname` 处理保留名/结尾点空格；查重与唯一性按不敏感比较（§15） |
| 双工具链分叉 | 只在 Linux 能编 | CMake 单一入口；CI 四组构建；M0 已在 Windows 11 + MSYS2(MINGW64) 实跑通过，不拖到 M5 |
| Qt 包体积 | 安装包偏大 | 只带 Core/Gui/Widgets/Network 与必需插件；Multimedia 仅在 `-DMULTIMEDIA=ON` 时打包；AppImage 压缩 |
| AppImage glibc 兼容 | 老系统跑不起来 | 构建基线 Ubuntu 22.04；文档写明 glibc ≥ 2.35 |
| 视频后端许可与体积 | 包体增大、许可复杂 | 首版只做外部播放器；Qt Multimedia 为可选构建，默认关闭（§8.3） |
| 代码签名/杀软误报 | 用户不敢安装 | **不签名**；发布页公布 SHA-256 与构建来源，README 说明 SmartScreen 提示与校验方法 |
| 工作量 | 核心 + CLI + Qt GUI，按模块拆分估算（M0 复盘时更新） | 里程碑拆分（§14），每阶段有可验证产物，不做"全部写完再联调" |

明确取舍（写下来避免反复讨论）：

- **不做多数据库后端抽象**。所有 SQL 收敛在 `store.c`，将来换 PostgreSQL 只改一个文件。
- **不做动态插件加载**。先编译期注册，等有第三方需求再开。
- **不做格式转换**。`chd/cso/wbfs` 只作为格式字段取值。
- **不把 CLI 当二等公民**。CLI 是正式前端，拥有全部功能；GUI 只是另一个 `rh_ui` 实现。
- **不依赖发行版 Qt**。Qt 随包分发，换取 Windows/Linux 与各发行版之间的行为一致。
- **不做 Web/移动/NAS 交付**。桌面双端是唯一目标。

---

## 14. 里程碑

三条规则：

1. **M1 起每个里程碑都打 git tag，并同时产出可安装 deb 与 Windows zip**（`vX.Y.Z-Mn`）。打包问题（soname、`/usr/lib` 布局、conffile、Qt 插件、DLL 依赖）越晚暴露越贵，不能等到 M5 才发现安装布局要返工。
2. **M3 → M4 串行**。GUI 依赖 M3 定型的 `query`/`detail` 接口；并行会让 GUI 反复追接口变更。
3. 验收项必须**可执行**。形容词不算通过。

| 阶段 | 交付物 | 验收 |
|---|---|---|
| **M0 骨架** | CMake 四组构建配置（Linux/Windows × CLI/GUI）、`RhCtx`/`RhErr`、`rh-config` 五层加载、`store_open`+v1 schema+迁移、`rom-hamster --version`、`rom-hamster-gui` 空窗口 | Linux/Windows × CLI/GUI 四组构建通过；`ldd`/`dumpbin` 断言核心不含 Qt/GTK；两个二进制在两个平台都能启动 |
| **M1 存储与规则** | `store_*` 全量、`RhGame`/`RhFilter`、`platform.c` 解析 101 平台、deb 骨架 + **首个可安装 deb**、Windows zip 骨架 | 101 个平台、5 段规则全部解析成功且媒体类型识别有单测；`store_*` 的 CRUD、`dir_path` UNIQUE、级联删除有测试；`user_version` 高于程序版本时只读拒绝写入的用例通过；`dpkg -i` 后 `rom-hamster --version` 可运行；**解压 zip 后，在只保留 System32 的 PATH 下 `bin\rom-hamster.exe --version` 返回 0**（zip 自包含）；`lintian` 无 error |
| **M2 导入** | `classify`、`archive`（libarchive）、`job` 作业线程、`dat` 索引、`library` 整库、`import` 编排 | fixtures 的 5 类压缩包（zip/7z/rar/单 ROM/混合）+ PSV 特征包 + 二义扩展名 + 多碟 CD 全部落到正确目录；两个平台上进程被杀后重启能续做且库里无半个条目；该阶段 deb 与 zip 仍可安装 |
| **M3 检索/管理/导出** | `query`、`detail`（CLI 纯文本）、`manage`、`export` 模板引擎 + 三份前端规格 | 13 维筛选 + 4 排序键结果与直接 SQL 对齐；导出的 ES-DE / Pegasus 清单能被对应前端正常读取；golden file 逐字节 diff 通过；故意写错的模板在**加载期**报出文件名与行号 |
| **M4 GUI** | `ui-qt`、`QTableView` 主窗口、详情页 8 类媒体分页、导入进度/取消、首次运行引导、拖拽导入、单实例、版本检查（首版范围严格按 §11） | 见下方 §14.1 量化指标；空配置首启能走完"选目录 → 建库 → 主窗口"；拖入目录触发导入并有可见进度；中文/日文长文件名正常显示；Windows 使用系统文件对话框；断网时版本检查静默且不阻塞启动；视频默认外部播放器（Multimedia 可选构建不作为验收前提） |
| **M5 打包与发布** | AppStream metainfo（含真实截图）、CLI gettext + GUI `.qm`、CI 矩阵、lintian 全绿、deb 三包、CLI tarball、GUI AppImage、Windows zip + Inno Setup、SHA-256 清单 | Ubuntu 22.04/24.04、Debian 12 的 deb 安装与**旧版→新版原地升级**通过；CLI tarball 解压可运行；GUI AppImage 双击可运行、`--cli` 应急入口可用、直接替换升级后用户数据保留；Windows zip 与 Inno Setup 覆盖安装通过（安装前自动关闭运行实例）；`dpkg -P` 干净卸载无残留；卸载保留用户数据；`appstreamcli validate` 通过 |

**M0 状态（2026-11-17，已验证）**

| 平台 | 工具链 | 构建 | 测试 |
|---|---|---|---|
| Linux（WSL Ubuntu，glibc 2.35+） | GCC、CMake 4.2、Qt 6.10.2 | `linux-cli`、`linux-gui` 两组通过 | 各 **5/5**（`core/rh-err`、`core/store`、`core/path`、`core/no-gui-linkage`、`tools/make-ico`） |
| Windows 11 x64 | MSYS2 MINGW64、GCC 16.2、CMake 4.5、Qt 6.12 | `windows-cli` **17 目标**、`windows-gui` **29 目标**（含 `rom-hamster-gui.exe`）通过 | 各 **5/5**；`core/path` 在**含中文的项目路径**下通过，验证了 UTF-8 入口修复 |

口径说明：

- `core/no-gui-linkage` 在 Linux 用 `ldd`，Windows 上该用例跳过并提示由 `dumpbin` 在 M5 打包阶段检查；Windows 的 `dumpbin` 断言目前**尚未执行**，不记为已通过。
- CLI 的 `--version` 已在 Linux 实测；两个 exe 的启动验收在 M1 首轮手动补记一次（M0 的 GUI 只是空窗口）。
- 四组构建可本机用 `ci/build-matrix.sh` 跑，也可由 GitHub 镜像上的 Actions 自动跑；Gitee Go 流水线仍未启用。

**M1 进度（2026-11-17）**

已完成并提交：`rh-str`（8 用例）、`rh-records`（5 用例）、`store`（10 用例）、`platform`（4 用例）、`library`（2 用例）、`classify`（2 用例）、`import` 预扫描（2 用例）。

| 平台 | 工具链 | 构建 | `ctest` |
|---|---|---|---|
| Linux（WSL Ubuntu） | CMake 4.2.3、GCC 15.2、GLib 2.88、SQLite 3.46、Qt 6.10.2 | CLI/GUI 全部目标通过，0 warning | 各 **13/13** |
| Windows 11 x64 | MSYS2 MINGW64、CMake 4.5、GCC 16.2、GLib 2.90.1、SQLite 3.54.1 | 35 目标 / 59 目标，0 warning | 各 **11/11** |

用例构成：`core/rh-err` 1、`core/store` 10、`core/path` 4、`core/str` 8、`core/records` 5、`core/platform` 4、`core/library` 2、`core/classify` 2、`core/import` 2、`core/no-gui-linkage` 1、`tools/make-ico` 1（合计 40；Linux 另有 `cli/version-utf8`、`cli/version-utf8-tty` 各 1，合计 42）。

**M1 验收逐条对照**：

| 验收项（本节 M1 行） | 状态 | 说明 |
|---|---|---|
| 101 个平台、5 段规则全部解析且媒体类型识别有单测 | 已完成 | `core/platform` 4 用例；媒体类型在 `core/classify`/`core/import` 覆盖 |
| `store_*` 的 CRUD、`dir_path` UNIQUE、级联删除有测试 | 已完成 | `core/store` 10 用例，含 UNIQUE 冲突拒绝与级联清理 |
| `user_version` 高于程序版本时只读拒绝写入 | 已完成 | `store_open` 只读打开 + 全部写接口返回 `RH_ERR_SCHEMA_TOO_NEW` |
| `dpkg -i` 与解压 zip 后 `rom-hamster --version` 可运行 | 已完成 | deb 两包可安装并运行（GUI 包待 Qt 部署接入）；CLI 与 GUI 两个 zip 均自带 MinGW 运行时（CLI 7 个 DLL、GUI 25 个 DLL + Qt 部署），解压后在只含 `System32` 的 PATH 下 `--version` 退出码 0 |
| `lintian` 无 error | 满足（deb 两包） | `lintian` 仅一条 warning（私有目录库的 ldconfig trigger），无 error |

结论：**M1 未结项**。核心模块部分已齐，缺打包验收两项。Windows 的 `dumpbin` 断言仍留到 M5，不计为已通过。

**M2 进度（2026-11-18）**

已完成 `archive`（列表/解压/加密探测，11 用例，含 GBK 条目名与非法字节兜底）、
`rh-fname`（6 用例）、`rh-hash`（流式单次读盘三算，批量复用同一遍读取，7 用例：
空文件、已知向量、超过缓冲区的大文件、中文路径、批量、取消、错误路径）与
`dat`（Logiqx 流式导入 + 哈希匹配，9 用例：正常样本、大文件分块进度、CRC 唯一命中
与多候选二义、SHA1 兜底、缺字段/畸形 XML、取消、GB18030 与非法字节、Redump
disk 与 sample、内容未变跳过/force/变更重导）、`import` 执行阶段（临时名 → 原子
改名 → 单条事务，5 用例：正常导入并回填 DAT 来源、目标冲突跳过、写库失败回滚、
取消后已提交条目保留、多碟 m3u + 碟号 + 补丁类型）、压缩包六级分流（`classify`
新增按条目名分流的纯函数，1 用例覆盖六类）与作业续做（`job_items` 按来源标签
跳过已完成条目，`store_job_list_unfinished` 供启动时提示，2 用例：分流落位、
取消后同 job_id 重跑不重复入库）。`import` 预扫描与 `store` 已接入原始字节，
schema 升到 v4（v1→v2→v3→v4 迁移均有用例）。四组构建（Linux/Windows × CLI/GUI）
通过；`ctest` Linux 17/17（89 用例）、Windows 15/15（85 用例）。纯补丁/混合/二义
包与顶层街机 ZIP 的平台判定仍留给 M4 的交互；下一步：M3。

**M3 进度（2026-11-19）**

已完成 `query`（`RhFilter` 13 维筛选 + name/rating/year/discs 四种排序，
`store_games_list` 承担全部 SQL）、`detail`（`RhGameDetail` 只读视图模型，
含 ROM/媒体/标签/DAT 匹配，不格式化文字）、`manage`（编辑、删除可连带删目录、
碟号重排、补丁类型、统计、完整性检查）与 `export`（规格解析 + 闭集模板引擎：
变量/`:xml`/`:json`/`:text` 转义/`%if%`/`%for%`，加载期报 `文件:行号`）。
三份前端规格落在 `data/frontends/es.conf`、`esde.conf`、`pegasus.conf`，
ES-DE 不输出媒体标签、上游 EmulationStation 输出，Pegasus 用 `lines` 记录格式。
CLI 子命令 `list`（别名 `search`）、`show`、`edit`、`delete`、`stats`、`export`
已接入，`--db` 默认取 `ROM_HAMSTER_DB` 或 `./romhamster.db`。

验收对照：

| 验收项（本节 M3 行） | 状态 | 说明 |
|---|---|---|
| 13 维筛选 + 4 排序键结果与直接 SQL 对齐 | 已完成 | `core/query` 2 用例覆盖全部维度、分页与非法排序键 |
| ES-DE / Pegasus 清单可被前端读取 | 已完成 | 三份模板产出的 XML/文本结构与前端规格一致 |
| golden file 逐字节 diff | 已完成 | `tests/data/golden/` 下 ES-DE 与 Pegasus 各一份，`core/export` 逐字节比较 |
| 故意写错的模板在加载期报文件名与行号 | 已完成 | 未知变量/未闭合 `%if%` 均返回 `path:line:` 错误；CLI 原样展示 |

四组构建（Linux/Windows × CLI/GUI）通过；`ctest` Linux **21/21（99 用例）**、
Windows **19/19（95 用例）**。M2 遗留的纯补丁/混合/二义包交互仍按计划留给 M4；
下一步：M4 GUI。

**M4 进度（2026-11-19）**

已完成首版 GUI 范围（严格按 §11）：

- `rh-ui.h`（§6.1 契约，仅头文件）与 `src/gui/ui-qt.cpp`：确认/单选/输入/文件与目录选择/
  只读文本/媒体类型判定；`progress` 经 `RhQtUiBridge` 信号排队回主线程。
- 主窗口：`GameTableModel`（`QAbstractTableModel`）+ `QTableView`，每页 200 条，
  `canFetchMore/fetchMore` 按视口取数；关键字 200 ms 防抖、平台下拉
  （来自 `store_stats_by_platform`）、四种排序与降序开关；筛选全部走 `rh_query_run`。
- 详情页：元数据 `QFormLayout` + 8 个分页（ROM 文件 / 封面 / 截图 / 视频 / 横幅 /
  合成图 / 说明 / 补丁）；图片经 `QThreadPool` 解码，磁盘缩略图缓存键为
  `路径+mtime+尺寸`（`QStandardPaths::CacheLocation/thumbs`）；视频与非图片用
  `QDesktopServices` 交外部播放器；说明原文用只读 `QPlainTextEdit`。
- 导入：拖拽与菜单共用两阶段管线。预扫描在 `ImportScanThread`，结束后弹出
  「将导入 N 游戏 / M 媒体 / K 补丁 / 跳过 J（附原因）」确认框；执行在
  `ImportExecThread`（独立 `store_open` 连接、零交互、`RhImportExecOptions`
  的进度/取消回调），`QProgressDialog` 支持取消，作业记录写入 `jobs/job_items`。
- 生命周期：首启 `QFileDialog::getExistingDirectory` 选库根目录，
  库固定为 `<ROM_ROOT>/romhamster.db` 并记入 `QSettings`；`QLocalServer` 单实例，
  第二次启动转交参数并激活已有窗口；启动后异步版本检查（3 s 超时、失败静默、
  24 小时间隔缓存），远程版本更高时非模态提示打开下载页。
- 只读库：`store_is_read_only` 为真时禁用导入，浏览与详情仍可用。
- 核心补充：`rh_import_plan_set_root()` —— 预扫描本身不知道库根目录，
  由前端拿到计划后重算 `target_exists`，避免把来源目录当成库根。

验收对照：

| 验收项（本节 M4 行） | 状态 | 说明 |
|---|---|---|
| 空配置首启走完「选目录 → 建库 → 主窗口」 | 已完成 | 无上次根目录时弹目录选择；`QT_QPA_PLATFORM=offscreen` 启动实测生成 `romhamster.db` |
| 拖入目录触发导入并有可见进度 | 已完成 | 拖拽与「导入目录…」同一 `startImport` 路径；预扫描/执行各一段进度 |
| 中文/日文长文件名正常显示 | 已完成 | 全链路 UTF-8（`QString::fromUtf8` / `rh-fname` 原始字节） |
| Windows 使用系统文件对话框 | 已完成 | `QFileDialog` 默认原生对话框 |
| 断网时版本检查静默且不阻塞启动 | 已完成 | `setTransferTimeout(3000)`，错误分支直接返回；默认 `RH_UPDATE_CHECK_URL` 为空时跳过 |
| 视频默认外部播放器 | 已完成 | `QDesktopServices::openUrl`；Multimedia 仍为可选构建 |

四组构建（Linux/Windows × CLI/GUI）通过，0 warning；`ctest` Linux **21/21（99 用例）**、
Windows **19/19（95 用例）**。本机 Windows 源码路径含中文，Qt 6.12 的 `moc`
无法在非 ASCII 绝对路径下写输出，验证时用 ASCII 目录联接（junction）映射后构建；
CI 的仓库路径为 ASCII，不受影响。

未结项：M2 遗留的纯补丁/混装/二义包**逐条交互**未做，当前只在预扫描摘要里
按原因计数并跳过；§14.1 的启动/滚动/内存指标需在参考机型上人工记录，本轮未测。

### 14.1 M4 性能验收（量化）

测试数据：脚本生成 **10000 条**游戏记录（每 10 条含 1 个多碟、每 20 条含 3 张截图）。

参考机型：4 核 x86_64 / 集显 / SATA SSD / 16 GB / 1920×1080；Linux 与 Windows 11 各测一遍，使用同一 Qt 6.8。

| 指标 | 阈值 | 测法 |
|---|---|---|
| 冷启动到主窗口可见 | Linux < 1.5 s；Windows < 2.0 s；AppImage 首次挂载单独记录 | `QElapsedTimer` 从 `main()` 首行到窗口 `isExposed` |
| 首屏列表渲染 | < 300 ms | 窗口显示到首帧 `frameSwapped` |
| 连续滚动 10 s | p95 帧间隔 ≤ 33 ms，且无单帧 > 100 ms | 记录每帧时间戳，统计 p95 与最大值 |
| 关键字筛选响应 | < 200 ms | 输入停顿到列表模型更新完成 |
| 打开详情页 | 元数据 < 150 ms；首张缩略图 < 400 ms 且不阻塞 UI | 详情页构造打点；缩略图工作线程加载 |
| 内存增长 | 滚动 10 s 后 RSS 增量 < 50 MB | Linux `/proc/self/statm`；Windows `GetProcessMemoryInfo` |
| 导入中 UI | 进度刷新间隔 ≤ 200 ms；取消到作业线程退出 < 1 s | 作业回调打点 |

CI 能自动化的只有「首屏渲染」与「内存增长」两项（Linux xvfb；Windows 无头环境受限，改人工）。帧率与启动时间在无显示器环境测不准，**不写进 CI 假装有指标**：改由人工在参考机型复测并记录，与上次基线比较，劣化超过 20% 即阻断发布。AppImage 首次启动包含挂载开销，单独记录，不套用 deb 的阈值。

---

## 15. 双端差异清单

| 议题 | Linux | Windows | 统一处理 |
|---|---|---|---|
| 配置目录 | XDG | `%APPDATA%`/`%ProgramData%` | `rh-path` |
| 路径分隔/大小写 | `/`，大小写敏感 | `\`，大小写不敏感 | 内部存 UTF-8 + `/`；比较按平台 |
| 参数与环境变量编码 | UTF-8 | `argv`/`getenv` 为 ANSI/GBK | 入口处经 `rh_path_utf8_from_native` 转 UTF-8；不得把原生窄字节直接交给 GLib/SQLite |
| 文件名编码 | 文件名一般是 UTF-8 | NTFS-3G/Samba/老 zip 可能是 GBK 或任意字节 | `rh-fname`：合法 UTF-8 原样；否则 GB18030 推测；再失败按 `%NN` 转义。显示与入库用 UTF-8，原始字节存 BLOB，不自动改名。详见 §15.1 |
| CLI 输出编码 | 恒为 UTF-8，不随 locale 转码 | 控制台代码页可能不是 UTF-8 | 输出字节与 locale 无关。`g_print`/`g_printerr` 在流是终端时按当前 locale 字符集转码（`glib/gprint.c` 的 `g_fputs`），进程未 `setlocale()` 就停在 `C`/ASCII，中文逐字变 `?`；故 CLI 直接写 UTF-8 字节。用例：`cli/version-utf8`（管道）与 `cli/version-utf8-tty`（`script -qc` 伪终端）。Windows 侧仍把控制台代码页设为 65001，与直接写字节自洽 |
| 长路径 | 无 260 限制 | `MAX_PATH` 260 | `\\?\` 前缀 |
| 保留名/结尾字符 | 无 | `CON`/`PRN`/…、结尾点/空格 | `sanitize_dirname` |
| 文件锁/原子改名 | `rename` | `MoveFileEx`/`QSaveFile` | `rh-fs` 封装 |
| 单实例 | `QLocalServer` | `QLocalServer` | 同一实现 |
| 字体/DPI | fontconfig | Segoe UI / 系统 DPI | Qt 默认 + 系统设置 |
| 文件对话框 | 系统对话框 | 系统对话框 | `QFileDialog` |
| 换行 | `\n` | CRLF 输入 | 写 `\n`，读兼容 |
| 安装/卸载 | deb / CLI tarball / GUI AppImage | Inno Setup / zip | §9 |
| 原地升级 | `dpkg`/`apt` 覆盖 | 同 AppId 同目录覆盖 | §9.4 |
| CLI 入口 | PATH；tarball 用 `$ORIGIN` rpath；AppImage `--cli` 仅应急 | PATH / `bin\` | §9.3 |
| 日志/缓存 | `XDG_CACHE_HOME` | `%LOCALAPPDATA%` | `rh-path` |

### 15.1 非 UTF-8 文件名（M2，必须兼容）

两类来源：压缩包条目名（老 Windows 工具打的 zip，条目名是 GBK 字节且无 UTF-8 标记）
与文件系统文件名（NTFS-3G/Samba/老 zip 解出的文件，POSIX 上是字节串）。两者都交给
`rh-fname`，使用同一套判定：

1. 原始字节是合法 UTF-8 → 原样使用，不标记推测；
2. 否则整串按 **GB18030** 解码，只有全部字节成功才采用。GB18030 是 GBK 的严格超集，
   能容纳更多历史字节；结果标记为「编码推测」；
3. 仍失败 → 逐字节转义（兜底），标记为「转义」。

**哪些情况不猜**：原始字节本身是合法 UTF-8（含纯 ASCII）；GB18030 无法整串解码；
名字含 NUL（无法作为 C 字符串显示，直接转义为 `%00`）。绝不局部替换，也不把半个
字符拼进显示名。

**兜底表示（确定且无损）**：0x20–0x7E 中除 `%` 与 `\` 外原样；`%` → `%25`，
`\` → `%5C`，其余字节 → `%NN`（大写十六进制）。结果只含可打印 ASCII，是合法
UTF-8，且不含路径分隔符；`rh_fname_unescape` 可逐字节还原。选择 `%NN` 而不是
`\xNN`：反斜杠在 Windows 上是路径分隔符，转义名一旦参与解压/建目录就会被误切出
子目录。

**数据库怎么存原始字节**：显示列（`games.dir_path`、`roms.filename`、`media.filename`）
存 UTF-8；v2 新增 `dir_path_raw` / `filename_raw` 三列 BLOB 存原始字节。状态
（UTF-8/推测/转义）由原始字节确定性重算，不另存标志列，避免两处不一致。扫描/解压时
原始字节直接来自 `readdir()` / libarchive 的 `pathname()`，不经过宽字符或 ANSI 中转；
不使用 `archive_entry_pathname_utf8()`，因为它会把无标记条目按 CP437 转成 `ÓÎÏ·`，
真实字节就丢了。

**定位**：POSIX 始终用原始字节调用文件系统，非 UTF-8 名字可以正常打开；Windows
文件名本质是 UTF-16，UTF-8 与 GB18030 两种状态都用解码后的 UTF-8 名定位；转义状态
没有对应的合法文件名，明确标记为不可定位，不返回一个会静默失败的路径。

**为什么不自动改名**：改名会破坏用户脚本、前端清单、模拟器配置里已有的引用；
GB18030 只是概率最高的猜测，猜错时改名等于把文件永久变成另一个名字。因此推测只
影响显示与入库，磁盘字节保持原样。首版也不提供批量改名；以后若做，必须是用户逐条
确认的显式操作。

**解压策略**：我们自己的解压按显示名落盘（UTF-8），双端得到同一个合法名字；原始
条目名仍保留在列表结果与库记录里。这条规则只约束"新建文件"，不触碰用户已有文件。

### 15.2 DAT 文件编码与匹配（M2）

DAT 文本不能假定 UTF-8：老中文 DAT 可能没有 XML 声明、内部是 GBK/GB18030 字节。
`dat` 的判定顺序：

1. **声明优先**：XML 声明里写了 `encoding` 就按声明解码（`g_iconv`）。声明值不可用
   （转换器打不开，或声明与实际字节不符）直接返回错误，不静默换别的编码——声明是
   作者给出的契约，悄悄改解释方式会掩盖数据本身的问题。
2. **无声明、开头是合法 UTF-8**：按 UTF-8 严格解码。若正文后段出现非法字节（常见于
   前 64 KiB 全 ASCII 的 GBK DAT），回滚整份 DAT 并以 GB18030 重新解析一次。
3. **无声明、开头就非法**：直接按 GB18030 解码，标记为「编码推测」。
4. GB18030 也解不开的字节：按 `%NN` 转义保留（与 §15.1 同一表示），置 `escaped`；
   条目不会因此消失。

第 2 步的"整份重来"只在必要时发生：导入是单事务，回滚后旧 DAT 索引原样保留，不会
出现半份数据。`dats.encoding` 记录实际采用的方式，`dats.skipped` 记录缺 rom 名等被
跳过的条目，便于换 DAT 后对比。

**解析**：GMarkup 逐块喂入（默认 64 KiB），内存占用与 DAT 大小无关；`<game>`（含
MAME 的 `<machine>`）的 `name`/`cloneof`/`romof` 与 `<rom>` 的
`name/size/crc/md5/sha1/status/merge` 原样保留，条目记为 `entry_type='rom'`。
`status="nodump"` 是合法状态，即使没有任何哈希也保留条目。Redump 风格 `<disk>`
以 `entry_type='disk'` 入库，只取 `name/sha1/md5`，继承 game 的 `cloneof/romof`，
`size/crc32/status/merge_name` 保持 NULL。`<sample>` 是音频采样引用，不是 ROM 匹配
对象，只计入 `sample_skipped`，不写 `dat_entries`；`entry_count` 只统计 rom + disk。
缺名字的 rom/disk 计入 `skipped`；非空但非法的 size/哈希值丢弃该字段并计入
`bad_fields`；XML 结构错误不整体失败，出错前解析到的条目照常入库并置 `xml_error`，
一条都没有时返回 `RH_ERR_INVALID`。

**匹配**：只认校验值。`size > 0` 时每次查询先按 size 缩小候选范围（DAT 条目没有
size 时豁免，否则 disk 永远匹配不上），再按 `sha1 → md5 → crc32`（抗碰撞强到弱）
查询，前一种有命中就停。`idx_dat_entries_size_crc32` 复合索引加速 size + crc32
路径，sha1/md5 仍走各自哈希索引。查询返回全部候选，候选数 0/1/>1 分别对应
`RH_DAT_MATCH_NONE/UNIQUE/AMBIGUOUS`，二义交给导入编排或用户处理。不按文件名匹配：
同一 rom 名在不同 game 下重复、parent/clone 共用 rom 是常态，名字不具备唯一性。

**取消与追溯**：进度回调按块触发，返回 FALSE 立即中止并回滚，库中不留该 DAT；
`roms.dat_id` 指向 `dats.id`，`games.dat_source` 存 DAT 名，二者共同回答"这条匹配
来自哪份 DAT"，换 DAT 后可重算。

**DAT 文件哈希与重导**：`dats.file_sha1` 在解析循环里顺带计算（与读取共用同一遍
IO，不额外读盘），在 `store_dat_finish` 回填；编码回退导致整份重来时校验和一并
重算。重导同一 `(platform, path)` 时先查旧哈希，一致且未指定 `force` 就直接返回
`stats.unchanged = TRUE`，不解析、不删旧索引；内容变了才按替换语义重建。这条
"先比哈希"是唯一需要额外读一遍文件的场景，换来的是跳过整次解析与插入。

---

## 16. 已定项与遗留问题

### 16.1 已定项（不再讨论）

| 编号 | 决定 | 落点 |
|---|---|---|
| P1 历史数据 | **不提供**历史数据转换工具（主程序也不读取任何旧格式）；只面向新建库 | §9.3 §12.2 |
| P2 GUI 首版 | 只做 §11 的主窗口、详情页、导入进度/取消、首次运行、拖拽导入、单实例、版本检查；批量编辑器、标签面板、统计图表后续再说 | §11 §14 M4 |
| P3 Windows 包 | 只做 zip + Inno Setup；**不做 MSIX/商店包** | §9.1 |
| P4 代码签名 | **不签名**；发布页公布 SHA-256 校验和与构建来源，并说明 SmartScreen | §9.1 §13 |
| P5 视频 | 默认外部播放器；Qt Multimedia 是**可选构建**（`-DMULTIMEDIA=ON`，默认 OFF） | §8.3 §11 |
| P6 Linux CLI | CLI 走 deb；免安装给 `.tar.gz`；AppImage **只用于 GUI**，`--cli` 仅应急，不单独出 CLI AppImage | §9.1 §9.3 |
| P7 更新 | 只做版本检查：启动静默查询，有新版本提示并打开下载页；**不下载、不静默安装**；失败不影响启动 | §10 §11 |
| P8 图标 | **已具备**：`resources/icons/rom-hamster.svg`（512×512，内嵌位图）与 `rom-hamster-{16,32,48,256}.png` 齐备且同一图形；GUI 经 `.qrc` 加载，ICO 由构建从 256 PNG 生成 | §1.1 §9.3 |
| 升级 | 三端原地覆盖；用户数据与库不受安装影响；启动检查 schema，高版本库只读并提示升级 | §3.4 §9.4 |

### 16.2 真正遗留的问题

| 编号 | 问题 | 影响 | 临时办法 |
|---|---|---|---|
| Q1 | 版本检查元数据的托管位置与 JSON 字段最终形态（仓库 Releases / 独立静态文件 / CDN） | P7 上线依赖它 | 先用仓库 Releases 页的静态 JSON；`update_check_url` 由构建注入，为空则跳过检查 |

原 Q2（`rom-hamster.svg` 由谁绘制、何时导出 PNG）**已关闭**：源文件与四尺寸 PNG 均已入库且为同一图形；后续只需在改图时同步重新导出，无待办。
