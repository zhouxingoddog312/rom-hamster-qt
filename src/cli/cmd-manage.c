#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "cli-common.h"
#include "manage.h"

static gchar *opt_db = NULL;
static gchar *opt_root = NULL;
static gboolean opt_with_files = FALSE;

static gchar *opt_name = NULL;
static gchar *opt_developer = NULL;
static gchar *opt_publisher = NULL;
static gchar *opt_genre = NULL;
static gchar *opt_region = NULL;
static gchar *opt_description = NULL;
static gchar *opt_patch_type = NULL;
static gint opt_year = -1;
static gint opt_players = -1;
static gdouble opt_rating = -1.0;

static gchar *resolve_db(void)
{
    return opt_db != NULL ? g_strdup(opt_db) : rh_cli_default_db();
}

int rh_cmd_delete(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, "数据库文件", "PATH" },
        { "with-files", 0, 0, G_OPTION_ARG_NONE, &opt_with_files,
          "同时删除库目录下的游戏目录", NULL },
        { "root", 0, 0, G_OPTION_ARG_FILENAME, &opt_root, "库根目录", "DIR" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new("- 删除游戏记录");
    g_option_context_add_main_entries(ctx, entries, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        gchar *msg = g_strdup_printf("参数错误: %s", error->message);
        g_error_free(error);
        g_option_context_free(ctx);
        return rh_cli_error(msg);
    }
    g_option_context_free(ctx);

    if (argc < 2)
        return rh_cli_error("用法: rom-hamster delete <游戏ID> [--with-files --root DIR]");

    if (opt_with_files && opt_root == NULL)
        opt_root = g_strdup(g_getenv("ROM_HAMSTER_ROOT"));

    gchar *db = resolve_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    err = rh_manage_delete(store, argv[1], opt_root, opt_with_files);
    store_close(store);

    if (err != RH_OK) {
        gchar *msg = g_strdup_printf("删除失败: %s", rh_err_str(err));
        return rh_cli_error(msg);
    }
    rh_cli_print(stdout, "已删除\n");
    return 0;
}

int rh_cmd_edit(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, "数据库文件", "PATH" },
        { "name", 0, 0, G_OPTION_ARG_STRING, &opt_name, "显示名", "TEXT" },
        { "developer", 0, 0, G_OPTION_ARG_STRING, &opt_developer, "开发商", "TEXT" },
        { "publisher", 0, 0, G_OPTION_ARG_STRING, &opt_publisher, "发行商", "TEXT" },
        { "genre", 0, 0, G_OPTION_ARG_STRING, &opt_genre, "类型", "TEXT" },
        { "region", 0, 0, G_OPTION_ARG_STRING, &opt_region, "区域", "TEXT" },
        { "description", 0, 0, G_OPTION_ARG_STRING, &opt_description, "简介", "TEXT" },
        { "patch-type", 0, 0, G_OPTION_ARG_STRING, &opt_patch_type,
          "补丁类型（空串清除）", "TYPE" },
        { "year", 0, 0, G_OPTION_ARG_INT, &opt_year, "年份（0 清除）", "YEAR" },
        { "players", 0, 0, G_OPTION_ARG_INT, &opt_players, "玩家数（0 清除）", "N" },
        { "rating", 0, 0, G_OPTION_ARG_DOUBLE, &opt_rating, "评分 0-10（0 清除）", "R" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new("- 编辑游戏元数据");
    g_option_context_add_main_entries(ctx, entries, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        gchar *msg = g_strdup_printf("参数错误: %s", error->message);
        g_error_free(error);
        g_option_context_free(ctx);
        return rh_cli_error(msg);
    }
    g_option_context_free(ctx);

    if (argc < 2)
        return rh_cli_error("用法: rom-hamster edit <游戏ID> [选项]");

    gchar *db = resolve_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    RhGame *game = store_game_get(store, argv[1], &err);
    if (game == NULL) {
        store_close(store);
        gchar *msg = g_strdup_printf("找不到游戏 %s", argv[1]);
        return rh_cli_error(msg);
    }

#define REPLACE(field, value) \
    do { g_free(game->field); game->field = g_strdup(value); } while (0)

    if (opt_name != NULL) REPLACE(name, opt_name);
    if (opt_developer != NULL) REPLACE(developer, opt_developer);
    if (opt_publisher != NULL) REPLACE(publisher, opt_publisher);
    if (opt_genre != NULL) REPLACE(genre, opt_genre);
    if (opt_region != NULL) REPLACE(region, opt_region);
    if (opt_description != NULL) REPLACE(description, opt_description);
    if (opt_patch_type != NULL) REPLACE(patch_type, opt_patch_type);
#undef REPLACE

    if (opt_year >= 0)
        game->year = opt_year;
    if (opt_players >= 0)
        game->players = opt_players;
    if (opt_rating >= 0.0)
        game->rating = opt_rating;

    err = rh_manage_update(store, game);
    rh_game_free(game);
    store_close(store);

    if (err != RH_OK) {
        gchar *msg = g_strdup_printf("更新失败: %s", rh_err_str(err));
        return rh_cli_error(msg);
    }
    rh_cli_print(stdout, "已更新\n");
    return 0;
}

int rh_cmd_stats(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, "数据库文件", "PATH" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new("- 库统计");
    g_option_context_add_main_entries(ctx, entries, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        gchar *msg = g_strdup_printf("参数错误: %s", error->message);
        g_error_free(error);
        g_option_context_free(ctx);
        return rh_cli_error(msg);
    }
    g_option_context_free(ctx);

    gchar *db = resolve_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    RhLibStats stats;
    err = rh_manage_stats(store, &stats, &err);
    if (err != RH_OK) {
        store_close(store);
        return rh_cli_error("统计失败");
    }

    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "游戏: %" G_GINT64_FORMAT "\n", stats.games);
    g_string_append_printf(out, "ROM: %" G_GINT64_FORMAT " 个，%"
                           G_GINT64_FORMAT " 字节\n", stats.roms, stats.rom_bytes);
    g_string_append_printf(out, "媒体: %" G_GINT64_FORMAT " 个，%"
                           G_GINT64_FORMAT " 字节\n", stats.media, stats.media_bytes);
    g_string_append_printf(out, "多碟: %" G_GINT64_FORMAT "\n", stats.multi_disc);
    g_string_append_printf(out, "街机: %" G_GINT64_FORMAT "\n", stats.arcade);
    g_string_append_printf(out, "有补丁: %" G_GINT64_FORMAT "\n", stats.with_patch);
    g_string_append_printf(out, "DAT 命中: %" G_GINT64_FORMAT "\n", stats.dat_matched);
    g_string_append_printf(out, "无 ROM 记录: %" G_GINT64_FORMAT "\n", stats.missing_rom);

    GPtrArray *by_platform = rh_manage_stats_by_platform(store, &err);
    if (err == RH_OK && by_platform->len > 0) {
        g_string_append(out, "\n平台\t游戏\tROM\t字节\n");
        for (guint i = 0; i < by_platform->len; i++) {
            RhPlatformStats *p = g_ptr_array_index(by_platform, i);
            g_string_append_printf(out, "%s\t%" G_GINT64_FORMAT "\t%"
                                   G_GINT64_FORMAT "\t%" G_GINT64_FORMAT "\n",
                                   p->platform, p->games, p->roms, p->rom_bytes);
        }
    }
    g_ptr_array_unref(by_platform);

    rh_cli_print(stdout, out->str);
    g_string_free(out, TRUE);
    store_close(store);
    return 0;
}
