# rom-hamster

[![CI](https://github.com/zhouxingoddog312/rom-hamster-qt/actions/workflows/ci.yml/badge.svg)](https://github.com/zhouxingoddog312/rom-hamster-qt/actions/workflows/ci.yml)
[![Release](https://github.com/zhouxingoddog312/rom-hamster-qt/actions/workflows/release.yml/badge.svg)](https://github.com/zhouxingoddog312/rom-hamster-qt/actions/workflows/release.yml)

Windows 11 / Linux 双端 ROM 库管理器：C11 核心 `libromham` + CLI + Qt 6 Widgets GUI。
规格与决策见 [ARCHITECTURE.md](ARCHITECTURE.md)。

当前状态：**M0 骨架已完成并在双端验证**（2026-11-17）。

- 构建系统、核心库空壳、CLI `--version`、GUI 空窗口、schema v1 与迁移入口
- Linux（GCC + Qt 6.10.2）与 Windows 11（MSYS2 MINGW64、GCC 16.2、Qt 6.12）各跑 CLI/GUI 两组，`ctest` 均 **5/5**；`core/path` 在含中文的项目路径下通过

业务功能从 M1 开始实现。

## 依赖

- 构建：CMake ≥ 3.22、Ninja、C11/C++17 编译器（MSVC 2022 / GCC 11+ / Clang 14+）
- 核心：glib 2.66+、sqlite 3.35+、libarchive 3.4+、libxml2 2.9+、zlib（用 pkg-config 探测）
- GUI：Qt 6.8（Core/Gui/Widgets/Network；Multimedia 仅 `-DRH_MULTIMEDIA=ON` 时需要）

## 构建

```sh
cmake --preset linux-cli && cmake --build --preset linux-cli
ctest --preset linux-cli
```

四组预设对应 ARCHITECTURE.md 的构建矩阵：`linux-cli`、`linux-gui`、`windows-cli`、`windows-gui`。
Windows 侧验证过的路径是 MSYS2「MINGW64」shell + `mingw-w64-x86_64-*` 包（GCC/CMake/Ninja/pkgconf/glib2/sqlite3/libarchive/libxml2/zlib/qt6-base），
在该 shell 里直接 `cmake --preset windows-cli` 即可；MSVC 2022 + vcpkg 为备选路径，尚未验证。

> **构建目录里的 `rom-hamster-gui.exe` 不是自包含的**：它依赖 MSYS2 的 Qt/GLib 运行时，
> 双击会提示缺少 `libstdc++-6.dll` 等。需要独立运行时请使用打包产物：
> `powershell -ExecutionPolicy Bypass -File packaging/windows/package-zip.ps1 -Gui`
> 生成的 zip，或 `make-setup.ps1` 生成的 `*-win64-setup.exe`（两者都会用 `objdump`
> 递归收集依赖 DLL）。构建树不复制 DLL，是避免运行库版本漂移和构建目录膨胀。

CI 入口：`ci/build-matrix.sh`（Linux 跑本机两组并补 sanitizer，MSYS2 跑本机两组），四组已在本机跑通。
GitHub Actions 跑同一套矩阵（`.github/workflows/ci.yml`）：Ubuntu 24.04 的 CLI/GUI、
ASan+UBSan、Ubuntu 22.04（glibc 2.35 基线）与 MSYS2 MINGW64 的 CLI/GUI，并校验
gettext 目录（`msgfmt --check`）与 Qt 翻译（`lrelease`）。
打 `v*` 标签时 `.github/workflows/release.yml` 在四个平台打包 deb / tar.gz /
AppImage / zip / setup，汇总 `SHA256SUMS` 后发布 GitHub Release。

## 仓库与同步

| 角色 | 地址 |
|---|---|
| 主仓（Gitee） | https://gitee.com/goddog312/rom-hamster-qt |
| 镜像（GitHub） | https://github.com/zhouxingoddog312/rom-hamster-qt |

主仓是唯一提交入口，GitHub 只做单向镜像，不单独在镜像上提交。

首次配置（本机已配置过，换机器时执行）：

```sh
git remote add origin git@gitee.com:goddog312/rom-hamster-qt.git
git remote add github https://github.com/zhouxingoddog312/rom-hamster-qt.git
```

日常同步（先推 Gitee，再推镜像）：

```sh
git push origin main --tags
git push github main --tags
```

拉取只从主仓：`git pull --ff-only origin main`。
不用 `git push --mirror`，避免误删镜像侧分支；Gitee 的「镜像」设置可作为备选的纯单向方案，但需要额外配置第三方 token，默认不启用。
认证由 Git Credential Manager（HTTPS）或 SSH 承担，**任何令牌、密钥都不入库**。

## 目录

```
src/core   libromham（唯一持有 SQL 的 store.c 在此）
src/cli    rom-hamster
src/gui    rom-hamster-gui（Qt Widgets）
resources  Qt 资源与图标素材（安装时映射为 hicolor）
data       平台规则、前端规格、desktop/metainfo/man
tests      g_test 单测与 fixtures
packaging  deb / AppImage / Inno Setup
```

## 许可

本项目以 **MIT** 许可发布，见 [LICENSE](LICENSE)。Copyright (c) 2026 zhouxingoddog312。

### Qt（LGPLv3）

`rom-hamster-gui` 以**动态链接**方式使用 Qt 6（Core/Gui/Widgets/Network，可选 Multimedia），按 LGPLv3 分发：

- 安装包内附 Qt 许可证文本与源码获取地址（发布时写入 `share/doc/rom-hamster/Qt-LICENSE*`）；
- 不静态链接 Qt，也不修改 Qt 源码；
- 用户可自行替换 Qt 动态库：Linux deb 装的 Qt 位于 `/usr/lib/rom-hamster/qt6/`，AppImage 解包后位于 `usr/lib/`，Windows 安装目录下为 `qt6/` 子目录，用同版本兼容的 Qt 库覆盖同名文件即可；
- CLI（`rom-hamster`）与核心库 `libromham` 不链接任何 Qt 模块。
