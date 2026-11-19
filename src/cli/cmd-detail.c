#include <glib.h>

#include "cli-common.h"
#include "detail.h"

static gchar *opt_db = NULL;

static const char *kind_label(const char *kind)
{
    if (g_strcmp0(kind, "covers") == 0) return "封面";
    if (g_strcmp0(kind, "screenshots") == 0) return "截图";
    if (g_strcmp0(kind, "videos") == 0) return "视频";
    if (g_strcmp0(kind, "marquees") == 0) return "标志";
    if (g_strcmp0(kind, "miximages") == 0) return "混合图";
    if (g_strcmp0(kind, "intro") == 0) return "片头";
    if (g_strcmp0(kind, "patcher") == 0) return "补丁";
    return kind;
}

static void append_field(GString *out, const char *label, const char *value)
{
    if (value != NULL && value[0] != '\0')
        g_string_append_printf(out, "%s: %s\n", label, value);
}

int rh_cmd_show(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, "数据库文件", "PATH" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new("- 显示游戏详情");
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
        return rh_cli_error("用法: rom-hamster show <游戏ID> [--db PATH]");

    gchar *db = opt_db != NULL ? g_strdup(opt_db) : rh_cli_default_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    RhGameDetail *detail = rh_detail_load(store, argv[1], TRUE, &err);
    if (detail == NULL) {
        store_close(store);
        gchar *msg = g_strdup_printf("找不到游戏 %s（%s）", argv[1],
                                     rh_err_str(err));
        int rc = rh_cli_error(msg);
        g_free(msg);
        return rc;
    }

    RhGame *game = detail->game;
    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "ID: %s\n", game->id);
    append_field(out, "名称", game->name);
    append_field(out, "主名", game->game_main_name);
    append_field(out, "平台", game->platform);
    append_field(out, "目录", game->dir_path);
    if (game->year > 0)
        g_string_append_printf(out, "年份: %d\n", game->year);
    append_field(out, "类型", game->genre);
    if (game->players > 0)
        g_string_append_printf(out, "玩家: %d\n", game->players);
    if (game->rating > 0.0)
        g_string_append_printf(out, "评分: %.1f\n", game->rating);
    append_field(out, "区域", game->region);
    append_field(out, "开发商", game->developer);
    append_field(out, "发行商", game->publisher);
    append_field(out, "状态", game->rom_status);
    append_field(out, "格式", game->rom_format);
    append_field(out, "DAT 来源", game->dat_source);
    append_field(out, "补丁类型", game->patch_type);
    g_string_append_printf(out, "多碟: %s\n", game->is_multi_disc ? "是" : "否");
    g_string_append_printf(out, "街机: %s\n", game->is_arcade ? "是" : "否");
    if (game->description != NULL && game->description[0] != '\0')
        g_string_append_printf(out, "简介: %s\n", game->description);

    if (detail->tags->len > 0) {
        g_string_append(out, "标签:");
        for (guint i = 0; i < detail->tags->len; i++)
            g_string_append_printf(out, " %s", (char *)g_ptr_array_index(detail->tags, i));
        g_string_append_c(out, '\n');
    }

    g_string_append_printf(out, "ROM: %u 个，%" G_GINT64_FORMAT " 字节\n",
                           detail->roms->len, detail->rom_bytes);
    for (guint i = 0; i < detail->roms->len; i++) {
        RhRomFile *rom = g_ptr_array_index(detail->roms, i);
        g_string_append_printf(out, "  [%u] %s", i + 1, rom->filename);
        if (rom->disc_no > 0)
            g_string_append_printf(out, "  碟 %d", rom->disc_no);
        if (rom->has_size)
            g_string_append_printf(out, "  %" G_GINT64_FORMAT " 字节", rom->size);
        if (rom->sha1 != NULL)
            g_string_append_printf(out, "  sha1=%s", rom->sha1);
        g_string_append_c(out, '\n');
    }

    g_string_append_printf(out, "媒体: %u 个，%" G_GINT64_FORMAT " 字节\n",
                           detail->media->len, detail->media_bytes);
    for (guint i = 0; i < detail->media->len; i++) {
        RhMediaFile *media = g_ptr_array_index(detail->media, i);
        g_string_append_printf(out, "  %s: %s\n", kind_label(media->kind),
                               media->filename);
    }

    if (detail->dat_matches->len > 0) {
        g_string_append(out, "DAT 匹配:\n");
        for (guint i = 0; i < detail->dat_matches->len; i++) {
            RhDatMatch *match = g_ptr_array_index(detail->dat_matches, i);
            g_string_append_printf(out, "  %s / %s (%s %s)\n",
                                   match->entry->game_name,
                                   match->entry->rom_name,
                                   match->dat_name != NULL ? match->dat_name : "-",
                                   match->dat_version != NULL ? match->dat_version : "");
        }
    }

    rh_cli_print(stdout, out->str);
    g_string_free(out, TRUE);
    rh_detail_free(detail);
    store_close(store);
    return 0;
}
