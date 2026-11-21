# DAT 阶段二规格：下载与更新

状态：已实施

## 1. 目标

让 `data/dats/sources.conf` 从“可查看”变成“可用”：

1. 新增核心模块 `dat-fetch`，按清单里的镜像顺序下载 DAT；
2. 下载先写 `.part`，校验通过后原子替换，失败不留半份文件；
3. 支持 `--check` 只比对不落盘、`--force` 强制重新下载；
4. CLI 新增 `dat update`，可只处理一个平台，也可 `--import` 顺带建索引；
5. 下载目录独立于库文件，默认 `<用户数据目录>/rom-hamster/dats/`。

GUI 与 CLI 共用同一套核心依赖（libcurl），但本阶段不新增 GUI 界面；
GUI 版本检查继续使用 Qt Network。

## 2. 设计

### 2.1 核心 API（`src/core/dat-fetch.h`）

```c
typedef struct {
    gboolean check_only;
    gboolean force;
    guint    connect_timeout; /* 秒，0 = 20 */
    guint    low_speed_time;  /* 秒，0 = 30 */
    guint    low_speed_limit; /* 字节/秒，0 = 1024 */
    gboolean (*progress)(const gchar *url, guint64 done, guint64 total,
                         gpointer user_data);
    gpointer user_data;
} RhDatFetchOptions;

typedef struct {
    gchar   *platform;
    gchar   *path;        /* 正式文件路径 */
    gchar   *url;         /* 成功的镜像 */
    guint64  bytes;
    gchar    sha1[41];
    gboolean changed;     /* 远端与本地不同 */
    gboolean installed;   /* 本次写了正式文件 */
    gint     mirrors_tried;
    gchar   *message;
} RhDatFetchResult;

gboolean rh_dat_fetch_supported(void);
RhErr rh_dat_fetch(const RhDatSource *source, const char *dir,
                   const RhDatFetchOptions *options, RhDatFetchResult *out);
```

行为：

- 本地存在且 `sha1` 与清单一致、未指定 `force`：不联网，`changed = FALSE`；
- 下载到 `<file>.part`，流式计算 sha1（复用 `rh_hash_file`）；
- 清单给了 `sha1` 就必须匹配，否则换下一个镜像；
- 与本地内容相同：删除临时文件，不覆盖；
- 内容不同且不是 `check_only`：`g_rename` 原子替换；
- 全部镜像失败返回 `RH_ERR_IO`，进度回调取消返回 `RH_ERR_CANCELLED`。

协议只允许 `http/https/file`（`file` 供单元测试与本地镜像）。模块在
`RH_DAT_FETCH=OFF` 时不引入 curl 头文件，API 返回 `RH_ERR_UNSUPPORTED`。

### 2.2 缓存目录

`rh_path_dat_cache_dir()`：`ROM_HAMSTER_DAT_DIR`，否则
`g_get_user_data_dir()/rom-hamster/dats`；只给路径，目录由 `rh_dat_fetch` 创建。

### 2.3 CLI

```text
rom-hamster dat update [--platform ID] [--file PATH] [--dir PATH]
                       [--check] [--force] [--import] [--db PATH]
```

- 默认处理清单内全部平台；`--platform` 只处理一个；
- 每个平台一行结果：`updated` / `up to date` / `update available` / `failed`；
- `--import` 时把成功（或本地已存在）的文件交给 `rh_dat_import`，平台取清单段名；
- 任一平台失败退出码为 1；`--check` 不改动任何文件。

## 3. 构建与打包

- 新选项 `RH_DAT_FETCH`，默认 ON；使用 `pkg_check_modules(CURL ... libcurl>=7.74)`；
- `romham` 私有链接 libcurl，并定义 `RH_DAT_FETCH=1`；
- `tests/unit/test-dat-fetch.c` 用 `file://` 覆盖安装、未变、检查、sha1 不匹配、
  镜像回退与取消；
- Debian `Build-Depends` 增加 `libcurl4-openssl-dev`；Windows 打包脚本按 PE
  导入表自动收集 `libcurl-4.dll` 及其依赖。

## 4. 明确不做

- 不改 schema（仍是 v4）；
- 不解析 DAT 内容（复用 `rh_dat_import`）；
- 不新增 GUI 对话框；
- 不做定时自动更新与状态缓存文件。
