#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "cli-common.h"
#include "dedup.h"
#include "i18n.h"

static gchar *opt_db = NULL;
static gchar *opt_root = NULL;
static gchar *opt_platform = NULL;
static gchar *opt_min_size = NULL;
static gboolean opt_media = FALSE;
static gboolean opt_backfill = FALSE;

static int dedup_fail(const char *message)
{
    rh_cli_print(stderr, message);
    return 2;
}

int rh_cmd_dedup(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { "root", 0, 0, G_OPTION_ARG_FILENAME, &opt_root,
          N_("Library root (required to read files)"), "DIR" },
        { "platform", 0, 0, G_OPTION_ARG_STRING, &opt_platform,
          N_("Only check this platform"), "ID" },
        { "media", 0, 0, G_OPTION_ARG_NONE, &opt_media,
          N_("Also check media and patches"), NULL },
        { "backfill", 0, 0, G_OPTION_ARG_NONE, &opt_backfill,
          N_("Compute missing hashes first"), NULL },
        { "min-size", 0, 0, G_OPTION_ARG_STRING, &opt_min_size,
          N_("Ignore files smaller than this many bytes"), "BYTES" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Find duplicate ROM/media files"));
    g_option_context_add_main_entries(ctx, entries, NULL);
    g_option_context_set_translation_domain(ctx, GETTEXT_PACKAGE);
    GError *error = NULL;
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        gchar *msg = g_strdup_printf(_("Option error: %s"), error->message);
        g_error_free(error);
        g_option_context_free(ctx);
        int rc = dedup_fail(msg);
        g_free(msg);
        return rc;
    }
    g_option_context_free(ctx);

    if (opt_root == NULL || opt_root[0] == '\0')
        opt_root = g_strdup(g_getenv("ROM_HAMSTER_ROOT"));
    if (opt_root == NULL || opt_root[0] == '\0')
        return dedup_fail(
            _("Usage: rom-hamster dedup --root DIR [--db PATH] [--platform ID] "
              "[--media] [--backfill] [--min-size BYTES]\n"));

    gint64 min_size = 0;
    if (opt_min_size != NULL) {
        gchar *end = NULL;
        min_size = g_ascii_strtoll(opt_min_size, &end, 10);
        if (end == NULL || *end != '\0' || min_size < 0) {
            gchar *msg = g_strdup_printf(_("Invalid --min-size value: %s"),
                                         opt_min_size);
            int rc = dedup_fail(msg);
            g_free(msg);
            return rc;
        }
    }

    gchar *db = opt_db != NULL ? g_strdup(opt_db) : rh_cli_default_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 2;

    RhDedupOptions options = {
        .root = opt_root,
        .platform = opt_platform,
        .include_media = opt_media,
        .backfill = opt_backfill,
        .min_size = min_size,
    };

    GPtrArray *groups = NULL;
    RhDedupStats stats;
    err = rh_dedup_report(store, &options, &groups, &stats);
    store_close(store);

    if (err != RH_OK) {
        gchar *msg = g_strdup_printf(_("Duplicate check failed: %s\n"),
                                     rh_err_str(err));
        int rc = dedup_fail(msg);
        g_free(msg);
        return rc;
    }

    if (opt_backfill) {
        gchar *line = g_strdup_printf(
            _("Backfilled %u hashes (%u failed/missing).\n"),
            stats.hashed, stats.failed);
        rh_cli_print(stdout, line);
        g_free(line);
    }

    for (guint i = 0; i < groups->len; i++) {
        RhDupGroup *group = g_ptr_array_index(groups, i);
        gchar *key = g_strndup(group->key, 12);
        gchar *size = rh_cli_format_int64(group->size);
        gint64 wasted = group->size * (gint64)(group->members->len - 1);
        gchar *waste = rh_cli_format_int64(wasted);
        gchar *header = g_strdup_printf(
            _("[%s] %s  size=%s  copies=%u  wasted=%s\n"),
            group->kind, key, size, group->members->len, waste);
        rh_cli_print(stdout, header);
        g_free(header);
        g_free(key);
        g_free(size);
        g_free(waste);

        for (guint j = 0; j < group->members->len; j++) {
            RhDupMember *member = g_ptr_array_index(group->members, j);
            gchar *path = g_strdup_printf("%s/%s", member->dir_path,
                                          member->filename);
            gchar *line = member->kind != NULL
                ? g_strdup_printf("  %s %s   %s\n", member->kind, path,
                                  member->game_name != NULL
                                      ? member->game_name : "")
                : g_strdup_printf("  %s   %s\n", path,
                                  member->game_name != NULL
                                      ? member->game_name : "");
            rh_cli_print(stdout, line);
            g_free(line);
            g_free(path);
        }
    }

    gchar *extra = rh_cli_format_int64((gint64)stats.extra_copies);
    gchar *bytes = rh_cli_format_int64(stats.bytes_duplicated);
    gchar *summary = g_strdup_printf(
        _("Checked: %u duplicate clusters, %s extra copies, %s bytes wasted.\n"),
        stats.clusters, extra, bytes);
    rh_cli_print(stdout, summary);
    g_free(summary);
    g_free(extra);
    g_free(bytes);

    gboolean found = groups->len > 0;
    g_ptr_array_unref(groups);
    return found ? 1 : 0;
}
