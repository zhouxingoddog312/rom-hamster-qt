# DAT 阶段五规格：GUI 管理、定时检查与打包验收

状态：已实施

## 1. 目标

1. GUI 提供与 CLI 对等的 DAT 管理入口；
2. 为定时/脚本化检查提供稳定的退出码；
3. 验证 libcurl 进入运行时依赖后的全部安装包。

## 2. GUI：DAT 源对话框

新增 `src/gui/dat-dialog.h/.cpp`，从 `DAT Sources…` 菜单打开：

- 读取 `rh_dat_sources_default_path()` 清单，表格列出平台、名称、本地文件大小；
- `Check for Updates`：`rh_dat_fetch` 的 `check_only` 模式，逐平台显示
  “Update available / Up to date / Failed”；
- `Download and Update`：下载并原子替换，可勾选“下载后导入库中”，
  对每个成功文件调用 `rh_dat_import`；
- 后台线程 `DatFetchThread` 执行网络与写库，进度与结果通过 queued signal
  回到界面；`Cancel` 通过进度回调中止，关闭窗口会等待线程结束；
- 下载目录与 CLI 相同（`rh_path_dat_cache_dir()`）。

## 3. 定时检查策略

`rom-hamster dat update --check --quiet`：

- 不输出任何进度/结果；
- 退出码：`0` 已是最新，`1` 有可用更新，`2` 检查失败；可直接用于 cron
  或 Windows 计划任务。

示例：

```cron
17 4 * * * rom-hamster dat update --check --quiet || \
  rom-hamster dat update --import
```

## 4. 打包验收（libcurl）

- Debian：`rom-hamster` 的 `Depends` 自动包含 `libcurl4t64`；
  `rom-hamster-gui` 补 `copyright` 与 `changelog.gz`，`qt.conf`/GUI 主程序的
  `no-manual-page` 用 lintian override 标注；三个 deb `lintian` 无 error。
- Windows：CLI zip 27 个 DLL、GUI zip 40 个 DLL，均含 `libcurl-4.dll` 及
  传递依赖，`System32`-only PATH 下 `--version` 通过；Inno Setup 安装包
  重新生成。
- AppImage：确认 `squashfs-root/usr/lib/libcurl.so.4` 存在。
- `dist/SHA256SUMS` 覆盖 deb（3）、tar.gz、AppImage、Windows zip（2）与 setup。

## 5. 明确不做

- 不做定时器常驻/开机自启；
- 不在 GUI 内编辑 `sources.conf`；
- 不固定上游 DAT 的 sha1（会阻碍后续自动更新）。
