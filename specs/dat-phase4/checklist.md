# DAT 阶段四验收清单

- [x] `--names original` 与旧行为完全一致（黄金文件通过）
- [x] `--names dat` 输出 DAT 标准名文件，清单与 m3u 同步
- [x] `--names display` 输出游戏显示名（中文）并保留后缀
- [x] 多文件街机包在 display 模式下回落原名，不互相覆盖
- [x] 非法 `--names` 取值返回 1 并提示
- [x] 双端 ctest 全部通过
