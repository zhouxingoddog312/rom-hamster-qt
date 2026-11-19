/*
 * detail.h —— 只读详情视图模型（ARCHITECTURE.md §4、§5.1）
 *
 * 只组装数据，不格式化文字、不写库、不删文件；纯文本渲染由 CLI 负责。
 */
#ifndef RH_DETAIL_H
#define RH_DETAIL_H

#include <glib.h>

#include "rh-err.h"
#include "rh-records.h"
#include "store.h"

typedef struct {
    RhGame    *game;        /* 非 NULL */
    GPtrArray *roms;        /* RhRomFile* */
    GPtrArray *media;       /* RhMediaFile*，全部类型 */
    GPtrArray *tags;        /* gchar* */
    GPtrArray *dat_matches; /* RhDatMatch*；include_dat_matches 为 FALSE 时为空 */
    gint64     rom_bytes;
    gint64     media_bytes;
} RhGameDetail;

/*
 * id 不存在时返回 NULL 且 *out_err = RH_ERR_NOT_FOUND（若定义）或 RH_OK。
 * include_dat_matches 为 TRUE 时按 sha1→md5→crc32 查已导入 DAT，只用于展示。
 */
RhGameDetail *rh_detail_load(RhStore *store, const char *id,
                             gboolean include_dat_matches, RhErr *out_err);
void          rh_detail_free(RhGameDetail *detail);

#endif /* RH_DETAIL_H */
