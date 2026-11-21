# DAT 阶段一验收清单

## 构建与测试

- [x] Linux CLI 配置与构建成功（无新增告警）
- [x] Linux 全量 ctest 通过（22/22）
- [x] Windows CLI/GUI 构建成功
- [x] Windows 全量 ctest 通过（21/21）
- [x] Windows `rom-hamster.exe --version` 与 `rom-hamster-gui.exe --version` 正常
- [x] `core/no-gui-linkage` 通过（核心未引入 Qt）

## 功能

- [x] `dat sources --explain` 命中源码树 `data/dats/sources.conf`，列出 27 个平台
- [x] 清单解析单测覆盖合法格式、重复平台、非法文件名、非法 URL、非法 SHA1、spec_version、段前键值
- [x] 真实清单单测通过（27 条、平台 ID 无重复、每条至少 2 个镜像）
- [x] 临时 DAT 导入成功，统计为 1 game / 1 entry / 0 skipped
- [x] `dat list` 正确显示平台、条目、版本、名称、路径
- [x] 重复导入显示 Unchanged，`--force` 触发重建
- [x] `ROM_HAMSTER_DAT_SOURCES` 覆盖默认清单路径
- [x] 设置 `LANG=zh_CN.UTF-8` 后 CLI 输出中文

## 数据与文案

- [x] `data/dats/sources.conf` 与 `platforms.conf` 的平台 ID 对齐
- [x] CMake 安装规则包含 `data/dats`
- [x] `po/POTFILES` 加入 `src/cli/cmd-dat.c`
- [x] `po/zh_CN.po` 无未翻译、无 fuzzy，`msgfmt -c` 通过

## 提交

- [x] 本地提交完成，未推送
