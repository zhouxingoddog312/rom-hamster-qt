#include <glib.h>

#include "cli-common.h"
#include "export.h"
#include "i18n.h"
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
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { "frontend", 0, 0, G_OPTION_ARG_FILENAME, &opt_frontend,
          N_("Front-end specification .conf"), "FILE" },
        { "out", 0, 0, G_OPTION_ARG_FILENAME, &opt_out, N_("Output directory"), "DIR" },
        { "root", 0, 0, G_OPTION_ARG_FILENAME, &opt_root, N_("Library root (copy ROMs/media)"), "DIR" },
        { "platforms", 0, 0, G_OPTION_ARG_FILENAME, &opt_platforms,
          N_("platforms.conf (optional)"), "FILE" },
        { "no-roms", 0, 0, G_OPTION_ARG_NONE, &opt_no_roms, N_("Do not copy ROMs"), NULL },
        { "no-media", 0, 0, G_OPTION_ARG_NONE, &opt_no_media, N_("Do not copy media"), NULL },
        { "m3u", 0, 0, G_OPTION_ARG_NONE, &opt_m3u, N_("Generate m3u for multi-disc games"), NULL },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Export a front-end manifest"));
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_set_translation_domain(ctx, GETTEXT_PACKAGE);
    GError *error = NULL;
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        gchar *msg = g_strdup_printf(_("Option error: %s"), error->message);
        g_error_free(error);
        g_option_context_free(ctx);
        return rh_cli_error(msg);
    }
    g_option_context_free(ctx);

    if (opt_frontend == NULL || opt_out == NULL)
        return rh_cli_error(_("Usage: rom-hamster export --frontend FILE --out DIR [--root DIR]"));

    /* 先加载一次，把「文件:行号」错误直接展示给用户 */
    RhErr err = RH_OK;
    gchar *message = NULL;
    RhExportSpec *spec = rh_export_spec_load(opt_frontend, &err, &message);
    if (spec == NULL) {
        int rc = rh_cli_error(message != NULL ? message : _("Cannot load front-end specification"));
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
            int rc = rh_cli_error(rules_msg != NULL ? rules_msg : _("Failed to load platform rules"));
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
        gchar *msg = g_strdup_printf(_("Export failed: %s"), rh_err_str(err));
        return rh_cli_error(msg);
    }

    gchar *summary = g_strdup_printf(
        _("Export complete: %u games, %u ROM files, %u media files, %u errors\n"),
        stats.games, stats.roms, stats.media, stats.errors);
    rh_cli_print(stdout, summary);
    g_free(summary);
    return stats.errors > 0 ? 1 : 0;
}
