# DAT 阶段五任务清单

依据：[spec.md](spec.md)。

## [x] 1. GUI DAT 源

- `src/gui/dat-dialog.h/.cpp`：清单表格、检查、下载+导入、进度、取消。
- `DatFetchThread` 后台执行 `rh_dat_fetch` / `rh_dat_import`。
- `window-main.cpp` 增加 `DAT Sources…` 菜单；`core-bridge.h` 暴露 DAT 头文件。
- `CMakeLists.txt` 加入新源文件；`i18n/rom-hamster_zh_CN.ts` 补 29 条中文。

## [x] 2. 定时检查

- `dat update` 新增 `--quiet/-q`。
- `--check --quiet` 退出码 0/1/2；`po/zh_CN.po` 补文案。

## [x] 3. 打包

- 重新生成 deb / GUI deb / tar.gz / AppImage / Windows CLI+GUI zip / setup.exe。
- GUI deb 增加 `copyright`、`changelog.gz`、lintian override。
- Inno Setup 中文语言文件入库，`.iss` 改用相对路径。
- 重新生成 `dist/SHA256SUMS`。

## [x] 4. 验证

- Linux ctest 23/23；Windows ctest 22/22。
- deb `Depends` 含 `libcurl4t64`；lintian 无 error。
- Windows 包 `System32`-only PATH 自检通过；AppImage 含 `libcurl.so.4`。
