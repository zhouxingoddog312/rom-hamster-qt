/*
 * query.h —— 只读检索、排序、筛选（ARCHITECTURE.md §4、§5.1）
 *
 * 本模块只做参数校验与翻译，SQL 全部留在 store.c；绝不写库。
 * 13 个筛选维度与 `list` 子命令参数一一对应。
 */
#ifndef RH_QUERY_H
#define RH_QUERY_H

#include <glib.h>

#include "rh-err.h"
#include "rh-records.h"
#include "store.h"

typedef struct {
    const char *platform;
    const char *genre;
    const char *developer;
    const char *publisher;
    const char *region;
    const char *patch_type;
    int         year_from;  /* 0 = 不限 */
    int         year_to;    /* 0 = 不限 */
    int         multi;      /* -1 不限 / 0 / 1 */
    int         has_patch;  /* -1 不限 / 0 / 1 */
    const char *status;     /* good/baddump/unknown；NULL 或空串不限 */
    const char *format;     /* raw/zip/7z/…；NULL 或空串不限 */
    const char **tags;      /* 标签名数组，可 NULL */
    int         n_tags;
    const char *kw;         /* 子串检索 name / game_main_name */
    const char *sort;       /* name（默认）/ rating / year / discs */
    int         desc;
    int         limit;      /* 0 取默认 200；-1 不限 */
    int         offset;
} RhFilter;

typedef struct {
    GPtrArray *games;       /* RhGame* */
    gint64     total;       /* 忽略 limit/offset 的命中总数 */
    int        limit;
    int        offset;
} RhQueryResult;

/* sort 只接受 name/rating/year/discs（NULL 视为 name）。 */
gboolean rh_query_sort_valid(const char *sort);

/*
 * 执行检索。失败返回 NULL；成功至少返回一个空结果对象。
 * 调用方用 rh_query_result_free 释放。
 */
RhQueryResult *rh_query_run(RhStore *store, const RhFilter *filter, RhErr *out_err);
void           rh_query_result_free(RhQueryResult *result);

#endif /* RH_QUERY_H */
