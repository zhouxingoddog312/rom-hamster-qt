# v0.1.0 实机验收清单

自动化部分（双端构建、`ctest`、`msgfmt`/`lrelease`、AppStream 校验、glibc 基线）
已在 CI 与本机完成；本清单只列**必须有真实机器和桌面会话**的项目。
逐项勾选，任何一项失败都阻断发布。

## 0. 取产物并校验

- [ ] 从 GitHub Release 下载全部产物与 `SHA256SUMS`
- [ ] `sha256sum -c SHA256SUMS`（Windows 用 `certutil -hashfile <文件> SHA256` 对照）
- [ ] 记录版本：`rom-hamster version` 输出 `0.1.0`

## 1. Linux deb（Ubuntu 22.04、24.04、Debian 12 各一遍）

- [ ] `sudo apt install ./rom-hamster_0.1.0_amd64.deb ./rom-hamster-data_0.1.0_all.deb ./rom-hamster-gui_0.1.0_amd64.deb`
      无未满足依赖（GUI 包自带 Qt，不应拉发行版 Qt 包）
- [ ] 新开终端 `rom-hamster --version`、`rom-hamster help` 正常
- [ ] 应用菜单出现 ROM Hamster，双击启动到主窗口；首启可选库目录并建库
- [ ] 导入一个真实 ROM 目录，列表出现记录；关闭再开，数据仍在
- [ ] 升级：安装**上一版** deb 后建库，再装 0.1.0（`apt install ./...`），
      数据库自动迁移且记录不丢
- [ ] `dpkg -P rom-hamster-gui rom-hamster-data rom-hamster` 干净卸载
- [ ] 卸载后 `~/.local/share/rom-hamster`（或用户选定库目录）仍在

## 2. Linux AppImage（Ubuntu 22.04 优先）

- [ ] `chmod +x rom-hamster-0.1.0-linux-x86_64.AppImage` 后双击可运行
- [ ] 首次启动记录挂载耗时（单独记录，不套用 deb 阈值）
- [ ] `./rom-hamster-0.1.0-linux-x86_64.AppImage --cli stats` 走 CLI 应急入口
- [ ] 用新版 AppImage 直接覆盖旧文件升级，用户数据保留

## 3. Linux CLI tarball

- [ ] 解压 `rom-hamster-0.1.0-linux-x86_64.tar.gz`，在**未安装** deb 的机器上
      `bin/rom-hamster --version` 可运行（只依赖系统 glib/sqlite/libarchive/libxml2/curl）
- [ ] `LANG=zh_CN.UTF-8 bin/rom-hamster --version` 输出中文

## 4. Windows GUI/CLI zip

- [ ] 解压到**没有 MSYS2/Qt** 的 Windows 11，双击 `rom-hamster-gui.exe` 可启动
- [ ] `rom-hamster.exe --version` 在 cmd 里返回 0，无缺 DLL 提示
- [ ] 中文/日文长文件名 ROM 导入后列表显示正常
- [ ] 拖拽目录到窗口触发导入，进度可见、可取消

## 5. Windows Inno Setup 安装包

- [ ] 静默安装：`rom-hamster-0.1.0-win64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES`
- [ ] 开始菜单与桌面快捷方式都能启动 GUI，CLI 在新 cmd 里可用
- [ ] 程序运行中再次安装：自动关闭旧实例后原地覆盖，不产生第二个“应用和功能”条目
- [ ] 卸载后 `%APPDATA%\rom-hamster` 保留；重新安装能读到旧配置与库

## 6. GUI DAT 对话框走查

- [ ] 菜单打开 `DAT Sources…`，表格列全清单平台，未下载状态清晰
- [ ] “检查更新”正常结束；断网时给出可读错误，不崩溃、不卡界面
- [ ] “更新”下载时进度条推进，取消后线程退出（窗口可正常关闭）
- [ ] 更新完成后选择导入索引，`stats` 中 DAT 匹配数增加
- [ ] 下载中断（拔网线/挂起）后重试，能续传并最终通过 sha1 校验

## 7. §14.1 性能记录（10000 条测试库，参考机型）

| 指标 | 阈值 | Linux 实测 | Windows 实测 |
|---|---|---|---|
| 冷启动到主窗口可见 | Linux < 1.5 s / Windows < 2.0 s | | |
| AppImage 首次启动（含挂载） | 单独记录 | | |
| 首屏列表渲染 | < 300 ms | | |
| 连续滚动 10 s 帧间隔 | p95 ≤ 33 ms，最大 ≤ 100 ms | | |
| 关键字筛选响应 | < 200 ms | | |
| 打开详情页（元数据 / 首图） | < 150 ms / < 400 ms | | |
| 滚动 10 s 后 RSS 增量 | < 50 MB | | |
| 导入进度刷新 / 取消退出 | ≤ 200 ms / < 1 s | | |

与上次基线比较，劣化超过 20% 即阻断发布。

## 8. 结果记录

- 验收人：
- 日期：
- 失败项与复现步骤：
- 结论（通过 / 阻断）：
