#include "detail.h"

RhGameDetail *rh_detail_load(RhStore *store, const char *id,
                             gboolean include_dat_matches, RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    if (store == NULL || id == NULL || id[0] == '\0') {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return NULL;
    }

    RhErr err = RH_OK;
    RhGame *game = store_game_get(store, id, &err);
    if (err != RH_OK) {
        if (out_err != NULL)
            *out_err = err;
        return NULL;
    }
    if (game == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_NOT_FOUND;
        return NULL;
    }

    RhGameDetail *detail = g_new0(RhGameDetail, 1);
    detail->game = game;
    detail->roms = store_roms_for_game(store, id, &err);
    if (err == RH_OK)
        detail->media = store_media_for_game(store, id, NULL, &err);
    if (err == RH_OK)
        detail->tags = store_tags_for_game(store, id, &err);
    detail->dat_matches = g_ptr_array_new_with_free_func(rh_dat_match_free_func);

    if (err == RH_OK) {
        for (guint i = 0; i < detail->roms->len; i++) {
            RhRomFile *rom = g_ptr_array_index(detail->roms, i);
            if (rom->has_size)
                detail->rom_bytes += rom->size;
        }
        for (guint i = 0; i < detail->media->len; i++) {
            RhMediaFile *media = g_ptr_array_index(detail->media, i);
            if (media->has_size)
                detail->media_bytes += media->size;
        }
    }

    /* DAT 匹配只是展示信息：按 sha1 → md5 → crc32 取第一个非空结果。 */
    if (err == RH_OK && include_dat_matches) {
        for (guint i = 0; i < detail->roms->len; i++) {
            RhRomFile *rom = g_ptr_array_index(detail->roms, i);
            const char *algos[] = { "sha1", "md5", "crc32" };
            const char *hashes[] = { rom->sha1, rom->md5, rom->crc32 };
            for (int a = 0; a < 3; a++) {
                if (hashes[a] == NULL || hashes[a][0] == '\0')
                    continue;
                GPtrArray *matches = NULL;
                gint64 size = rom->has_size ? rom->size : 0;
                err = store_dat_find_by_hash(store, algos[a], hashes[a], size,
                                             detail->game->platform, &matches);
                if (err != RH_OK)
                    break;
                if (matches != NULL && matches->len > 0) {
                    g_ptr_array_set_free_func(matches, NULL);
                    for (guint m = 0; m < matches->len; m++)
                        g_ptr_array_add(detail->dat_matches,
                                        g_ptr_array_index(matches, m));
                }
                if (matches != NULL)
                    g_ptr_array_unref(matches);
                if (detail->dat_matches->len > 0)
                    break;
            }
            if (err != RH_OK)
                break;
        }
    }

    if (err != RH_OK) {
        if (out_err != NULL)
            *out_err = err;
        rh_detail_free(detail);
        return NULL;
    }

    return detail;
}

void rh_detail_free(RhGameDetail *detail)
{
    if (detail == NULL)
        return;
    rh_game_free(detail->game);
    if (detail->roms != NULL)
        g_ptr_array_unref(detail->roms);
    if (detail->media != NULL)
        g_ptr_array_unref(detail->media);
    if (detail->tags != NULL)
        g_ptr_array_unref(detail->tags);
    if (detail->dat_matches != NULL)
        g_ptr_array_unref(detail->dat_matches);
    g_free(detail);
}
