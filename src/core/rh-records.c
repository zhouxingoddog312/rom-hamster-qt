#include "rh-records.h"

const char *const rh_media_kind_names[7] = {
    "covers", "screenshots", "videos", "marquees", "miximages", "intro", "patcher"
};

gboolean rh_media_kind_valid(const char *kind)
{
    if (kind == NULL)
        return FALSE;

    for (gsize i = 0; i < G_N_ELEMENTS(rh_media_kind_names); i++) {
        if (g_strcmp0(kind, rh_media_kind_names[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

RhGame *rh_game_new(void)
{
    RhGame *game = g_new0(RhGame, 1);
    game->rom_format = g_strdup("raw");
    game->rom_status = g_strdup("unknown");
    game->patch_type = g_strdup("");
    return game;
}

void rh_game_free(RhGame *game)
{
    if (game == NULL)
        return;

    g_free(game->id);
    g_free(game->platform);
    g_free(game->name);
    g_free(game->game_main_name);
    g_free(game->dir_path);
    g_free(game->developer);
    g_free(game->publisher);
    g_free(game->genre);
    g_free(game->region);
    g_free(game->description);
    g_free(game->rom_format);
    g_free(game->rom_status);
    g_free(game->dat_source);
    g_free(game->patch_type);
    g_free(game->created_at);
    g_free(game->updated_at);
    g_free(game);
}

void rh_game_free_func(gpointer data)
{
    rh_game_free(data);
}

RhRomFile *rh_rom_file_new(void)
{
    return g_new0(RhRomFile, 1);
}

void rh_rom_file_free(RhRomFile *rom)
{
    if (rom == NULL)
        return;

    g_free(rom->filename);
    g_free(rom->crc32);
    g_free(rom->md5);
    g_free(rom->sha1);
    g_free(rom);
}

void rh_rom_file_free_func(gpointer data)
{
    rh_rom_file_free(data);
}

RhMediaFile *rh_media_file_new(void)
{
    return g_new0(RhMediaFile, 1);
}

void rh_media_file_free(RhMediaFile *media)
{
    if (media == NULL)
        return;

    g_free(media->kind);
    g_free(media->filename);
    g_free(media);
}

void rh_media_file_free_func(gpointer data)
{
    rh_media_file_free(data);
}
