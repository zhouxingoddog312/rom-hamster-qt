# DAT 阶段五验收清单

## GUI

- [x] 菜单可打开 `DAT Sources…`，表格列出清单全部平台
- [x] 本地已下载文件显示大小，未下载显示明显状态
- [x] 检查/更新在后台线程执行，界面可取消
- [x] 更新后可选择导入索引，失败信息回显
- [x] 关闭窗口时等待下载线程结束，不崩溃

## 定时检查

- [x] `dat update --check --quiet` 无输出
- [x] 已最新退出 0，有更新退出 1，出错退出 2

## 打包

- [x] 三个 deb 的 Depends 正确，lintian 无 error
- [x] Windows CLI/GUI zip 含 `libcurl-4.dll` 及依赖，自检通过
- [x] setup.exe 重新生成（含中文安装界面）
- [x] AppImage 内含 `libcurl.so.4`
- [x] `SHA256SUMS` 覆盖全部产物

## 回归

- [x] Linux ctest 23/23
- [x] Windows ctest 22/22
- [x] PO 与 TS 无未翻译/fuzzy
