# 重复文件识别（方案 B）验收清单

依据：[spec.md](spec.md) / [tasks.md](tasks.md)。

## 功能

- [x] 旧库（v5）打开后自动升到 v6，原有数据不丢。
- [x] `rom-hamster dedup --root <dir>` 能列出重复 ROM；加 `--media` 能列出
      重复媒体与补丁。
- [x] `--backfill` 能为缺失摘要的文件补算 `sha1` / `canon_sha1`。
- [x] 退出码语义：无重复 `0`、发现重复 `1`、参数/根目录/库/数据库/IO/取消等
      硬失败 `≥2`；`1` 不与任何错误混用。
- [x] 同内容但一个带 NES/SNES 头、一个不带的 ROM 被判为同一簇。
- [x] 导入时可对每个重复项选择忽略/共存/覆盖；「应用到全部」生效。
- [x] 忽略：不复制、不新增 ROM 行。
- [x] 共存：生成 `name (2).ext`，两份都在，库中有两行。
- [x] 覆盖：旧文件被替换，原 ROM 行摘要更新且没有新增 ROM 行。
- [x] GUI「查找重复…」已接线并随 GUI 构建通过；**界面交互本身待实机走查**，见文末「实机走查」。

## 回归

- [x] `ctest` 全部通过（含原有用例）。
- [x] 非重复导入行为与之前一致（目标存在仍跳过）。
- [x] DAT 匹配仍按整文件 CRC32，未因去头哈希改变。
- [x] `msgfmt --check po/zh_CN.po` 通过。

## 安全与边界

- [x] 识别只读，不删除任何文件。
- [x] 覆盖失败时不会静默丢失旧文件（记 failed 并尽量保留）。
- [x] 缺失文件不会导致 dedup 中止，只计入 failed/missing，退出码仍按 0/1 判断。

## 实机走查（需要真人操作）

自动化只能证明「能构建、逻辑用例通过」；以下必须在真实桌面会话里逐项执行。
未勾选项一律视为尚未验证，不得据此宣称发布通过。每步先写操作，再写预期结果。

准备：准备一个测试目录，放入同一个 ROM 的两份拷贝（如 `game.nes` 与
`game - 副本.nes`），再放一份带 16 字节 NES 头的同内容 ROM；三者应被识别为
同一重复簇。

### A. Windows GUI（Windows 11 x64 + MSYS2 MINGW64）

- [ ] 在 MSYS2 MINGW64 shell 里重建：`cmake --preset windows-gui` 后
      `cmake --build --preset windows-gui`。
      预期：0 error；生成 `build/windows-gui/rom-hamster-gui.exe`。
- [ ] 双击 `build/windows-gui/rom-hamster-gui.exe`。
      预期：主窗口出现，标题含 ROM Hamster；任务栏与标题栏图标是应用图标，
      不是系统默认空白图标；不弹出黑色控制台窗口。
- [ ] 首次启动按提示选择空目录作为库。
      预期：建库成功，列表为空但界面可操作；关闭再启动仍记住该目录。
- [ ] 打开 File → Import Directory…（Ctrl+I），选准备目录。
      预期：导入前弹出「Duplicate Files Found」对话框，表格列出源文件/已有文件/
      大小/相似度/处置方式；处置下拉为 Ignore / Keep both / Overwrite，
      顶部「Set all to」+「Apply to all」可批量设置；取消后不产生任何改动。
- [ ] 对一组选「Keep both」并点「Apply to all」后确认。
      预期：导入完成；目录里出现 `game (2).nes` 一类的新文件名，两份文件都在，
      列表出现两条记录。
- [ ] 再次导入同一目录，对一组选「Ignore」。
      预期：该组不复制文件、不新增记录；其它非重复文件照常导入。
- [ ] 再次导入，对一组选「Overwrite」。
      预期：目标文件被新文件替换，列表记录数不增加，详情页中的摘要已更新。
- [ ] 菜单打开 Find Duplicates…（查找重复…）。
      预期：对话框列出重复簇及成员大小/摘要；勾选「Compute missing hashes」后
      点「Scan」，进度结束且缺失计数减少；关闭对话框后主窗口仍可正常操作。
- [ ] 把准备目录拖进主窗口。
      预期：走同一导入流程；遇重复时同样弹出处置对话框。
- [ ] `build/windows-gui/rom-hamster.exe dedup --root <准备目录>`。
      预期：列出 1 个重复簇，包含三份同内容 ROM（含带头那份）。
- [ ] 双击列表记录打开详情页，切换各分页后关闭。
      预期：元数据与图片正常，无崩溃、无明显卡顿。
- [ ] 关闭程序后重新启动。
      预期：记录与设置仍在（`%APPDATA%\rom-hamster` 下的数据库未被重置）。

### B. Windows 安装包（桌面图标与安装后运行）

- [ ] 用 `packaging/windows/package-zip.ps1 -Gui` 与 `make-setup.ps1` 重新出包
      （或使用 CI 的 `rom-hamster-*-win64-setup.exe`）。
      预期：`dist/` 下生成安装包，无日志类文件。
- [ ] 双击安装包完成安装。
      预期：开始菜单出现 ROM Hamster；**桌面快捷方式存在**，图标为应用图标，
      不是空白或通用 exe 图标。
- [ ] 从桌面快捷方式启动。
      预期：GUI 正常启动；在任意工作目录下都能启动（不依赖仓库目录）。
- [ ] 另开 cmd 运行
      `"%ProgramFiles%\rom-hamster\bin\rom-hamster.exe" --version`。
      预期：输出版本号，退出码 0，无缺 DLL 提示。
- [ ] 程序运行中再次运行安装包覆盖安装。
      预期：自动关闭旧实例并完成覆盖；「应用和功能」里仍只有一条记录。
- [ ] 卸载后检查。
      预期：安装目录清空；`%APPDATA%\rom-hamster` 保留；重新安装能读到旧库。

### C. Linux GUI（Ubuntu 22.04/24.04 或 Debian 12 桌面会话）

- [ ] 重建并跑测试：`cmake --preset linux-gui`、
      `cmake --build --preset linux-gui`、`ctest --preset linux-gui`。
      预期：构建 0 error，ctest 全部通过。
- [ ] 打包并安装：`bash packaging/make-deb.sh`、
      `bash packaging/make-gui-deb.sh`、`sudo apt install ./dist/*.deb`。
      预期：依赖自动满足（不拉发行版 Qt 包）；安装无报错。
- [ ] `desktop-file-validate /usr/share/applications/io.github.goddog312.RomHamster.desktop`。
      预期：无输出（校验通过）。
- [ ] 在应用菜单搜索 ROM Hamster 并点击。
      预期：主窗口出现；Dock/任务栏显示 ROM Hamster 图标而不是问号或空白；
      窗口与图标正确关联（不出现两个图标）。
- [ ] 终端执行 `rom-hamster-gui`。
      预期：同上正常启动；关闭终端不影响已打开的窗口。
- [ ] 按 A 节第 4–9 步走一遍导入与查找重复。
      预期：行为与 Windows 一致。
- [ ] `rom-hamster dedup --root <准备目录> --media`。
      预期：列出重复 ROM（加 `--media` 时也包含重复媒体/补丁），退出码 **1**
      （发现重复）；无重复目录才是 0，参数/库/IO 错误应为 2。
- [ ] `dpkg -P rom-hamster-gui rom-hamster-data rom-hamster`。
      预期：应用菜单项与图标消失；`~/.local/share/rom-hamster` 仍保留用户库。

### D. 结果记录

- 验收人 / 日期：
- 未通过项与复现步骤：
- 结论（通过 / 阻断）：
