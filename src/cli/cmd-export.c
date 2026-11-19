#include <glib.h>

#include "cli-common.h"
#include "export.h"
#include "platform.h"

static gchar *opt_db = NULL;
static gchar *opt_frontend = NULL;
static gchar *opt_out = NULL;
static gchar *opt_root = NULL;
static gchar *opt_platforms = NULL;
static gboolean opt_no_roms = FALSE;
static gboolean opt_no_media = FALSE;
static gboolean opt_m3u = FALSE;

int rh_cmd_export(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, "数据库文件", "PATH" },
        { "frontend", 0, 0, G_OPTION_ARG_FILENAME, &opt_frontend,
          "前端规格 .conf", "FILE" },
        { "out", 0, 0, G_OPTION_ARG_FILENAME, &opt_out, "输出目录", "DIR" },
        { "root", 0, 0, G_OPTION_ARG_FILENAME, &opt_root, "库根目录（复制 ROM/媒体）", "DIR" },
        { "platforms", 0, 0, G_OPTION_ARG_FILENAME, &opt_platforms,
          "platforms.conf（可选）", "FILE" },
        { "no-roms", 0, 0, G_OPTION_ARG_NONE, &opt_no_roms, "不复制 ROM", NULL },
        { "no-media", 0, 0, G_OPTION_ARG_NONE, &opt_no_media, "不复制媒体", NULL },
        { "m3u", 0, 0, G_OPTION_ARG_NONE, &opt_m3u, "为多碟游戏生成 m3u", NULL },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new("- 导出前端清单");
    g_option_context_add_main_entries(ctx, entries, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        gchar *msg = g_strdup_printf("参数错误: %s", error->message);
        g_error_free(error);
        g_option_context_free(ctx);
        return rh_cli_error(msg);
    }
    g_option_context_free(ctx);

    if (opt_frontend == NULL || opt_out == NULL)
        return rh_cli_error("用法: rom-hamster export --frontend FILE --out DIR [--root DIR]");

    /* 先加载一次，把「文件:行号」错误直接展示给用户 */
    RhErr err = RH_OK;
    gchar *message = NULL;
    RhExportSpec *spec = rh_export_spec_load(opt_frontend, &err, &message);
    if (spec == NULL) {
        int rc = rh_cli_error(message != NULL ? message : "无法加载前端规格");
        g_free(message);
        return rc;
    }
    rh_export_spec_free(spec);
    g_free(message);

    RhPlatformRules *rules = NULL;
    if (opt_platforms != NULL) {
        gchar *rules_msg = NULL;
        rules = rh_platform_rules_load(opt_platforms, &err, &rules_msg);
        if (rules == NULL) {
            int rc = rh_cli_error(rules_msg != NULL ? rules_msg : "平台规则加载失败");
            g_free(rules_msg);
            return rc;
        }
        g_free(rules_msg);
    }

    if (opt_root == NULL && !opt_no_roms && !opt_no_media)
        opt_root = g_strdup(g_getenv("ROM_HAMSTER_ROOT"));

    gchar *db = opt_db != NULL ? g_strdup(opt_db) : rh_cli_default_db();
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL) {
        rh_platform_rules_free(rules);
        return 1;
    }

    RhExportOptions options = {
        .library_root = opt_root,
        .output_dir = opt_out,
        .copy_roms = !opt_no_roms && opt_root != NULL,
        .copy_media = !opt_no_media && opt_root != NULL,
        .compress_roms = FALSE,
        .generate_m3u = opt_m3u,
    };

    RhExportStats stats;
    err = rh_export_run(store, rules, opt_frontend, &options, NULL, NULL, &stats);

    store_close(store);
    rh_platform_rules_free(rules);

    if (err != RH_OK) {
        gchar *msg = g_strdup_printf("导出失败: %s", rh_err_str(err));
        return rh_cli_error(msg);
    }

    gchar *summary = g_strdup_printf(
        "导出完成：游戏 %u，ROM %u，媒体 %u，错误 %u\n",
        stats.games, stats.roms, stats.media, stats.errors);
    rh_cli_print(stdout, summary);
    g_free(summary);
    return stats.errors > 0 ? 1 : 0;
}
