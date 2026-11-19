#include "query.h"

#include <string.h>

#define RH_QUERY_DEFAULT_LIMIT 200
#define RH_QUERY_MAX_LIMIT     5000

static const char *nonempty(const char *s)
{
    return (s != NULL && s[0] != '\0') ? s : NULL;
}

gboolean rh_query_sort_valid(const char *sort)
{
    if (sort == NULL || sort[0] == '\0')
        return TRUE;
    return g_strcmp0(sort, "name") == 0 || g_strcmp0(sort, "rating") == 0 ||
           g_strcmp0(sort, "year") == 0 || g_strcmp0(sort, "discs") == 0;
}

void rh_query_result_free(RhQueryResult *result)
{
    if (result == NULL)
        return;
    if (result->games != NULL)
        g_ptr_array_unref(result->games);
    g_free(result);
}

RhQueryResult *rh_query_run(RhStore *store, const RhFilter *filter, RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;

    if (store == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return NULL;
    }

    const char *sort = (filter != NULL) ? filter->sort : NULL;
    if (!rh_query_sort_valid(sort)) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return NULL;
    }

    RhGameFilter f;
    memset(&f, 0, sizeof f);
    f.limit = RH_QUERY_DEFAULT_LIMIT;
    f.order_by = (sort != NULL && sort[0] != '\0') ? sort : "name";

    if (filter != NULL) {
        f.platform = nonempty(filter->platform);
        f.genre = nonempty(filter->genre);
        f.developer = nonempty(filter->developer);
        f.publisher = nonempty(filter->publisher);
        f.region = nonempty(filter->region);
        f.patch_type = nonempty(filter->patch_type);
        f.rom_status = nonempty(filter->status);
        f.rom_format = nonempty(filter->format);
        f.tags = filter->tags;
        f.n_tags = filter->tags != NULL ? filter->n_tags : 0;
        f.search = nonempty(filter->kw);
        f.year_from = filter->year_from > 0 ? filter->year_from : 0;
        f.year_to = filter->year_to > 0 ? filter->year_to : 0;
        f.multi = filter->multi == 1 ? 1 : (filter->multi == 0 ? 2 : 0);
        f.has_patch = filter->has_patch == 1 ? 1
                      : (filter->has_patch == 0 ? 2 : 0);
        f.descending = filter->desc != 0;
        f.offset = filter->offset > 0 ? filter->offset : 0;

        if (filter->limit == -1) {
            f.limit = -1;
        } else if (filter->limit > 0) {
            f.limit = MIN(filter->limit, RH_QUERY_MAX_LIMIT);
        }
    }

    RhErr err = RH_OK;
    gint64 total = store_games_count(store, &f, &err);
    if (err != RH_OK) {
        if (out_err != NULL)
            *out_err = err;
        return NULL;
    }

    RhQueryResult *result = g_new0(RhQueryResult, 1);
    result->games = store_games_list(store, &f, &err);
    result->total = total;
    result->limit = f.limit;
    result->offset = f.offset;

    if (err != RH_OK) {
        if (out_err != NULL)
            *out_err = err;
        rh_query_result_free(result);
        return NULL;
    }

    return result;
}
