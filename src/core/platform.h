/*
 * platform.h —— 平台规则库（ARCHITECTURE.md §5、§7）
 *
 * 数据来自 platforms.conf（v1 格式，见文件头）。解析期错误带
 * "文件:行号"，由前端决定怎么展示；核心不打印。
 */
#ifndef RH_PLATFORM_H
#define RH_PLATFORM_H

#include <glib.h>

#include "rh-err.h"

typedef struct {
    gchar     *id;               /* 规范平台 ID */
    GPtrArray *suffixes;         /* gchar*，全部带点、小写 */
    GPtrArray *patch_formats;
    GPtrArray *ambiguous_formats;
    GPtrArray *aliases;          /* gchar*，小写 */
    gchar     *recommended_format;
    gboolean   compress_supported;
    gboolean   arcade;
} RhPlatform;

typedef struct RhPlatformRules RhPlatformRules;

/*
 * 加载规则文件。失败返回 NULL，out_err 为 RH_ERR_IO（读不到文件）或
 * RH_ERR_INVALID（语法错误，message 带行号）。message 可为 NULL。
 * 同一文件重复加载是允许的；规则库不可变，可跨线程共享读取。
 */
RhPlatformRules *rh_platform_rules_load(const char *path, RhErr *out_err, gchar **message);
void rh_platform_rules_free(RhPlatformRules *rules);

int             rh_platform_rules_version(const RhPlatformRules *rules);
guint           rh_platform_count(const RhPlatformRules *rules);
RhPlatform     *rh_platform_by_id(const RhPlatformRules *rules, const char *id_or_alias);
const GPtrArray *rh_platform_all(const RhPlatformRules *rules);

/*
 * 按后缀找候选平台：
 *   - 先尝试复合后缀（如 ".p8.png"），再退到最后一个后缀；
 *   - 返回的数组元素为 RhPlatform*，按规则文件顺序；
 *   - 调用方用完 g_ptr_array_free(arr, TRUE)（元素不归调用方释放）。
 * 未命中返回空数组。
 */
GPtrArray *rh_platform_candidates_for_file(const RhPlatformRules *rules,
                                           const char *filename);

/* filename 是否是该平台的 ROM（同上规则，命中任一即 TRUE） */
gboolean rh_platform_matches_file(const RhPlatform *platform, const char *filename);

gboolean rh_platform_is_archive(const RhPlatformRules *rules, const char *filename);
gboolean rh_platform_is_arcade_rom_member(const RhPlatformRules *rules, const char *filename);

/* 媒体目录名与后缀约束；kind 见 RhMediaKind。未知 kind 返回 NULL/FALSE */
const gchar *rh_platform_media_dir(const RhPlatformRules *rules, const char *kind);
gboolean rh_platform_media_suffix_allowed(const RhPlatformRules *rules,
                                          const char *kind, const char *filename);

#endif /* RH_PLATFORM_H */
