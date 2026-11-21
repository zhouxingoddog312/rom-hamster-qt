# DAT 阶段二验收清单

## 功能

- [x] `dat update --platform nes` 能下载并落盘，第二次显示 “up to date”
- [x] 远端变化后 `--check` 显示 “update available”，本地文件不变
- [x] `--import` 在下载后建立 DAT 索引，`dat list` 能看到
- [x] 镜像 1 失败时自动尝试镜像 2
- [x] 清单 `sha1` 不匹配时拒绝安装并报错
- [x] 取消进度回调后不留下 `.part` 或正式文件
- [x] `RH_DAT_FETCH=OFF` 时构建可用，`dat update` 提示未启用

## 回归

- [x] Linux `ctest` 23/23 通过
- [x] Windows `ctest` 22/22 通过
- [x] `core/no-gui-linkage` 通过（核心未引入 Qt）
- [x] `po/zh_CN.po` 无 fuzzy、无未翻译，`msgfmt -c` 通过
