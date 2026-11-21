#include <stdio.h>
#include <string.h>

#include "cli-common.h"
#include "dat.h"
#include "dat-fetch.h"
#include "dat-source.h"
#include "i18n.h"
#include "rh-path.h"

static gchar *opt_db = NULL;

static gchar *opt_platform = NULL;
static gchar *opt_name = NULL;
static gchar *opt_version = NULL;
static gboolean opt_force = FALSE;

static gchar *opt_file = NULL;
static gboolean opt_explain = FALSE;

static gboolean opt_check = FALSE;
static gchar *opt_dat_dir = NULL;
static gboolean opt_import = FALSE;
static gboolean opt_quiet = FALSE;

static gchar *resolve_db(void)
{
    return opt_db != NULL ? g_strdup(opt_db) : rh_cli_default_db();
}

typedef struct {
    int last_percent;
} DatProgress;

static gboolean dat_progress(const gchar *path, gint64 entries,
                             guint64 bytes_done, guint64 bytes_total,
                             gpointer user_data)
{
    (void)path;
    (void)entries;
    DatProgress *progress = user_data;

    if (bytes_total == 0)
        return TRUE;

    int percent = (int)(bytes_done * 100 / bytes_total);
    if (percent == progress->last_percent)
        return TRUE;
    if (percent < progress->last_percent + 5 && percent != 100)
        return TRUE;

    progress->last_percent = percent;
    gchar *line = g_strdup_printf("\r%3d%%", percent);
    rh_cli_print(stderr, line);
    g_free(line);
    return TRUE;
}

static int dat_import(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { "platform", 0, 0, G_OPTION_ARG_STRING, &opt_platform,
          N_("Platform ID the DAT belongs to"), "ID" },
        { "name", 0, 0, G_OPTION_ARG_STRING, &opt_name,
          N_("Override the DAT name"), "TEXT" },
        { "version", 0, 0, G_OPTION_ARG_STRING, &opt_version,
          N_("Override the DAT version"), "TEXT" },
        { "force", 0, 0, G_OPTION_ARG_NONE, &opt_force,
          N_("Rebuild the index even if the file is unchanged"), NULL },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Import a Logiqx DAT file"));
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

    if (argc < 2)
        return rh_cli_error(_("Usage: rom-hamster dat import <FILE> --platform <ID>"));
    if (opt_platform == NULL || opt_platform[0] == '\0')
        return rh_cli_error(_("--platform is required"));

    gchar *db = resolve_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    DatProgress progress = { 0, };
    RhDatImportOptions options = { 0 };
    options.platform = opt_platform;
    options.dat_name = opt_name;
    options.dat_version = opt_version;
    options.force = opt_force;
    options.progress = dat_progress;
    options.user_data = &progress;

    RhDatImportStats stats;
    rh_dat_import_stats_clear(&stats);
    err = rh_dat_import(store, argv[1], &options, &stats);
    store_close(store);

    if (progress.last_percent > 0)
        rh_cli_print(stderr, "\n");

    if (err != RH_OK) {
        gchar *msg = g_strdup_printf(_("DAT import failed: %s"), rh_err_str(err));
        return rh_cli_error(msg);
    }

    if (stats.unchanged) {
        rh_cli_print(stdout,
                     _("Unchanged: index is up to date (use --force to rebuild)\n"));
        return 0;
    }

    gchar *line = g_strdup_printf(_("Imported %s\n"), argv[1]);
    rh_cli_print(stdout, line);
    g_free(line);

    line = g_strdup_printf(_("Platform: %s\n"), opt_platform);
    rh_cli_print(stdout, line);
    g_free(line);

    line = g_strdup_printf(_("Encoding: %s%s%s\n"),
                           stats.encoding[0] != '\0' ? stats.encoding : "?",
                           stats.encoding_guessed ? _(" (guessed)") : "",
                           stats.escaped ? _(" (invalid bytes escaped)") : "");
    rh_cli_print(stdout, line);
    g_free(line);

    gchar *games_text = rh_cli_format_int64(stats.games);
    gchar *entries_text = rh_cli_format_int64(stats.entry_count);
    gchar *skipped_text = rh_cli_format_int64(stats.skipped);
    line = g_strdup_printf(_("Games: %s, entries: %s, skipped: %s\n"),
                           games_text, entries_text, skipped_text);
    rh_cli_print(stdout, line);
    g_free(line);
    g_free(games_text);
    g_free(entries_text);
    g_free(skipped_text);

    if (stats.xml_error)
        rh_cli_print(stdout, _("Warning: DAT structure error, partial index imported\n"));
    return 0;
}

static int dat_list(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- List imported DAT indexes"));
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

    gchar *db = resolve_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    GPtrArray *dats = store_dat_list(store, &err);
    store_close(store);

    if (err != RH_OK) {
        g_ptr_array_unref(dats);
        gchar *msg = g_strdup_printf(_("Cannot list DAT indexes: %s"),
                                     rh_err_str(err));
        return rh_cli_error(msg);
    }

    if (dats->len == 0) {
        rh_cli_print(stdout, _("No DAT imported\n"));
        g_ptr_array_unref(dats);
        return 0;
    }

    rh_cli_print(stdout, _("Platform\tEntries\tVersion\tName\tPath\n"));
    for (guint i = 0; i < dats->len; i++) {
        const RhDatInfo *info = g_ptr_array_index(dats, i);
        gchar *count = rh_cli_format_int64(info->entry_count);
        gchar *line = g_strdup_printf("%s\t%s\t%s\t%s\t%s\n",
                                      info->platform,
                                      count,
                                      info->version != NULL ? info->version : "",
                                      info->name != NULL ? info->name : "",
                                      info->path);
        rh_cli_print(stdout, line);
        g_free(line);
        g_free(count);
    }
    g_ptr_array_unref(dats);
    return 0;
}

static void print_search_hint(void)
{
    rh_cli_print(stderr, _("Searched:\n"));

    const gchar *env = g_getenv("ROM_HAMSTER_DAT_SOURCES");
    if (env != NULL && env[0] != '\0') {
        gchar *line = g_strdup_printf(_("  %s\n"), env);
        rh_cli_print(stderr, line);
        g_free(line);
    }

    gchar *user = g_build_filename(g_get_user_config_dir(), "rom-hamster",
                                   "dats", "sources.conf", NULL);
    gchar *line = g_strdup_printf(_("  %s\n"), user);
    rh_cli_print(stderr, line);
    g_free(line);
    g_free(user);

    GPtrArray *dirs = rh_path_data_dir_candidates();
    for (guint i = 0; i < dirs->len; i++) {
        const char *dir = g_ptr_array_index(dirs, i);
        gchar *candidate = g_build_filename(dir, "dats", "sources.conf", NULL);
        line = g_strdup_printf(_("  %s\n"), candidate);
        rh_cli_print(stderr, line);
        g_free(line);
        g_free(candidate);
    }
    g_ptr_array_free(dirs, TRUE);
}

static int dat_sources(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "file", 0, 0, G_OPTION_ARG_FILENAME, &opt_file,
          N_("Manifest file (default: search data directories)"), "PATH" },
        { "explain", 0, 0, G_OPTION_ARG_NONE, &opt_explain,
          N_("Show which manifest file is used"), NULL },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- List DAT sources"));
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

    gchar *path = opt_file != NULL ? g_strdup(opt_file)
                                   : rh_dat_sources_default_path();
    if (path == NULL) {
        rh_cli_print(stderr, _("DAT source manifest not found\n"));
        print_search_hint();
        return 1;
    }

    if (opt_explain) {
        gchar *line = g_strdup_printf(_("Manifest: %s\n"), path);
        rh_cli_print(stdout, line);
        g_free(line);
    }

    GPtrArray *sources = NULL;
    gchar *message = NULL;
    RhErr err = rh_dat_sources_load(path, &sources, &message);
    if (err != RH_OK) {
        gchar *msg = message != NULL
            ? g_strdup_printf(_("Cannot load DAT sources: %s"), message)
            : g_strdup_printf(_("Cannot load DAT sources: %s"), rh_err_str(err));
        g_free(message);
        g_free(path);
        return rh_cli_error(msg);
    }
    g_free(path);

    for (guint i = 0; i < sources->len; i++) {
        const RhDatSource *source = g_ptr_array_index(sources, i);
        gchar *line = g_strdup_printf(_("[%s] %s\n  file: %s\n"),
                                      source->platform,
                                      source->name != NULL ? source->name : "",
                                      source->file);
        rh_cli_print(stdout, line);
        g_free(line);

        for (guint u = 0; u < source->urls->len; u++) {
            line = g_strdup_printf(_("  url: %s\n"),
                                   (const gchar *)g_ptr_array_index(source->urls, u));
            rh_cli_print(stdout, line);
            g_free(line);
        }
        if (source->sha1 != NULL) {
            line = g_strdup_printf(_("  sha1: %s\n"), source->sha1);
            rh_cli_print(stdout, line);
            g_free(line);
        }
    }

    g_ptr_array_unref(sources);
    return 0;
}

typedef struct {
    int last_percent;
    gboolean printed;
} FetchProgress;

static gboolean fetch_progress(const gchar *url, guint64 bytes_done,
                               guint64 bytes_total, gpointer user_data)
{
    (void)url;
    FetchProgress *progress = user_data;

    if (bytes_total == 0)
        return TRUE;

    int percent = (int)(bytes_done * 100 / bytes_total);
    if (percent == progress->last_percent)
        return TRUE;
    if (percent < progress->last_percent + 5 && percent != 100)
        return TRUE;

    progress->last_percent = percent;
    progress->printed = TRUE;
    gchar *line = g_strdup_printf("\r%3d%%", percent);
    rh_cli_print(stderr, line);
    g_free(line);
    return TRUE;
}

static int dat_update(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { "platform", 0, 0, G_OPTION_ARG_STRING, &opt_platform,
          N_("Only update the DAT of this platform"), "ID" },
        { "file", 0, 0, G_OPTION_ARG_FILENAME, &opt_file,
          N_("Manifest file (default: search data directories)"), "PATH" },
        { "check", 0, 0, G_OPTION_ARG_NONE, &opt_check,
          N_("Only report whether an update is available"), NULL },
        { "force", 0, 0, G_OPTION_ARG_NONE, &opt_force,
          N_("Download and compare even if the local file matches"), NULL },
        { "dir", 0, 0, G_OPTION_ARG_FILENAME, &opt_dat_dir,
          N_("Directory for downloaded DAT files"), "PATH" },
        { "import", 0, 0, G_OPTION_ARG_NONE, &opt_import,
          N_("Import the downloaded DAT files into the library"), NULL },
        { "quiet", 'q', 0, G_OPTION_ARG_NONE, &opt_quiet,
          N_("No output; with --check exit 1 when updates are available"), NULL },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Download and update DAT files"));
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

    if (!rh_dat_fetch_supported())
        return rh_cli_error(_("This build has no DAT download support"));

    gchar *manifest = opt_file != NULL ? g_strdup(opt_file)
                                       : rh_dat_sources_default_path();
    if (manifest == NULL) {
        rh_cli_print(stderr, _("DAT source manifest not found\n"));
        print_search_hint();
        return 1;
    }

    GPtrArray *sources = NULL;
    gchar *message = NULL;
    RhErr err = rh_dat_sources_load(manifest, &sources, &message);
    g_free(manifest);
    if (err != RH_OK) {
        gchar *msg = message != NULL
            ? g_strdup_printf(_("Cannot load DAT sources: %s"), message)
            : g_strdup_printf(_("Cannot load DAT sources: %s"), rh_err_str(err));
        g_free(message);
        return rh_cli_error(msg);
    }

    gchar *dir = opt_dat_dir != NULL ? g_strdup(opt_dat_dir)
                                     : rh_path_dat_cache_dir();

    RhStore *store = NULL;
    if (opt_import) {
        gchar *db = resolve_db();
        store = rh_cli_open_store(db, &err);
        g_free(db);
        if (store == NULL) {
            g_ptr_array_unref(sources);
            g_free(dir);
            return 1;
        }
    }

    int updated = 0, unchanged = 0, available = 0, failed = 0, matched = 0;

    for (guint i = 0; i < sources->len; i++) {
        const RhDatSource *source = g_ptr_array_index(sources, i);
        if (opt_platform != NULL && opt_platform[0] != '\0' &&
            g_ascii_strcasecmp(source->platform, opt_platform) != 0)
            continue;
        matched++;

        RhDatFetchOptions options = { 0 };
        options.check_only = opt_check;
        options.force = opt_force;
        FetchProgress progress = { 0, };
        options.progress = opt_quiet ? NULL : fetch_progress;
        options.user_data = &progress;

        RhDatFetchResult result;
        err = rh_dat_fetch(source, dir, &options, &result);
        if (progress.printed)
            rh_cli_print(stderr, "\n");

        gchar *line = NULL;
        if (err != RH_OK) {
            failed++;
            line = g_strdup_printf(_("%s: failed: %s\n"), source->platform,
                                   result.message != NULL
                                       ? result.message : rh_err_str(err));
            if (!opt_quiet)
                rh_cli_print(stderr, line);
            g_free(line);
            rh_dat_fetch_result_clear(&result);
            continue;
        }

        gchar *bytes = rh_cli_format_int64((gint64)result.bytes);
        if (opt_check) {
            if (result.changed) {
                available++;
                line = g_strdup_printf(_("%s: update available (%s bytes)\n"),
                                       source->platform, bytes);
            } else {
                unchanged++;
                line = g_strdup_printf(_("%s: up to date\n"), source->platform);
            }
        } else if (result.installed) {
            updated++;
            line = g_strdup_printf(_("%s: updated (%s bytes)\n"),
                                   source->platform, bytes);
        } else {
            unchanged++;
            line = g_strdup_printf(_("%s: up to date\n"), source->platform);
        }
        if (!opt_quiet)
            rh_cli_print(stdout, line);
        g_free(line);
        g_free(bytes);

        if (opt_import && store != NULL) {
            RhDatImportOptions import_options = { 0 };
            import_options.platform = source->platform;
            import_options.dat_name = source->name;
            import_options.force = opt_force;
            DatProgress import_progress = { 0, };
            import_options.progress = opt_quiet ? NULL : dat_progress;
            import_options.user_data = &import_progress;

            RhDatImportStats stats;
            rh_dat_import_stats_clear(&stats);
            err = rh_dat_import(store, result.path, &import_options, &stats);
            if (import_progress.last_percent > 0)
                rh_cli_print(stderr, "\n");

            if (err != RH_OK) {
                failed++;
                line = g_strdup_printf(_("%s: index update failed: %s\n"),
                                       source->platform, rh_err_str(err));
                if (!opt_quiet)
                    rh_cli_print(stderr, line);
                g_free(line);
            } else if (stats.unchanged) {
                line = g_strdup_printf(_("%s: index is up to date\n"),
                                       source->platform);
                if (!opt_quiet)
                    rh_cli_print(stdout, line);
                g_free(line);
            } else {
                gchar *entries = rh_cli_format_int64(stats.entry_count);
                line = g_strdup_printf(_("%s: indexed %s entries\n"),
                                       source->platform, entries);
                if (!opt_quiet)
                    rh_cli_print(stdout, line);
                g_free(line);
                g_free(entries);
            }
        }

        rh_dat_fetch_result_clear(&result);
    }

    g_free(dir);
    if (store != NULL)
        store_close(store);
    g_ptr_array_unref(sources);

    if (matched == 0) {
        gchar *msg = opt_platform != NULL
            ? g_strdup_printf(_("No DAT source for platform: %s"), opt_platform)
            : g_strdup(_("No DAT source matched"));
        return rh_cli_error(msg);
    }

    gchar *summary = opt_check
        ? g_strdup_printf(_("Check finished: %d update(s), %d up to date, %d failed\n"),
                          available, unchanged, failed)
        : g_strdup_printf(_("Update finished: %d updated, %d unchanged, %d failed\n"),
                          updated, unchanged, failed);
    if (!opt_quiet)
        rh_cli_print(stdout, summary);
    g_free(summary);

    if (opt_quiet && opt_check) {
        /* 供 cron/计划任务使用：0 已最新，1 有更新，2 出错。 */
        if (failed > 0)
            return 2;
        return available > 0 ? 1 : 0;
    }
    return failed > 0 ? 1 : 0;
}

int rh_cmd_dat(int argc, char **argv)
{
    if (argc < 2)
        return rh_cli_error(_("Usage: rom-hamster dat <import|list|sources|update> [options]"));

    const char *sub = argv[1];
    if (strcmp(sub, "import") == 0)
        return dat_import(argc - 1, argv + 1);
    if (strcmp(sub, "list") == 0)
        return dat_list(argc - 1, argv + 1);
    if (strcmp(sub, "sources") == 0)
        return dat_sources(argc - 1, argv + 1);
    if (strcmp(sub, "update") == 0)
        return dat_update(argc - 1, argv + 1);

    gchar *msg = g_strdup_printf(_("Unknown dat subcommand: %s"), sub);
    return rh_cli_error(msg);
}
