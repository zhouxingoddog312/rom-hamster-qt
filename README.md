# rom-hamster

Windows 11 / Linux 双端 ROM 库管理器：C11 核心 `libromham` + CLI + Qt 6 Widgets GUI。
规格与决策见 [ARCHITECTURE.md](ARCHITECTURE.md)。

当前状态：**M0 骨架**（构建系统、核心库空壳、CLI `--version`、GUI 空窗口、schema v1 与迁移入口）。
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
Windows 上需先进入 MSVC 环境（`vcvars64.bat`），并让 pkg-config 能找到核心五库
（推荐 MSYS2 或 vcpkg，详见 §8.4）。

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

待定（M5 前确定）。
