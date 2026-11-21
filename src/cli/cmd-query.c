#include <glib.h>
#include <string.h>

#include "cli-common.h"
#include "i18n.h"
#include "query.h"

static gchar *opt_db = NULL;
static gchar *opt_platform = NULL;
static gchar *opt_genre = NULL;
static gchar *opt_developer = NULL;
static gchar *opt_publisher = NULL;
static gchar *opt_region = NULL;
static gchar *opt_patch_type = NULL;
static gchar *opt_status = NULL;
static gchar *opt_format = NULL;
static gchar *opt_search = NULL;
static gchar *opt_sort = NULL;
static gchar **opt_tags = NULL;
static gint opt_year_from = 0;
static gint opt_year_to = 0;
static gint opt_limit = 0;
static gint opt_offset = 0;
static gboolean opt_multi = FALSE;
static gboolean opt_no_multi = FALSE;
static gboolean opt_has_patch = FALSE;
static gboolean opt_no_patch = FALSE;
static gboolean opt_desc = FALSE;

int rh_cmd_list(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { "platform", 0, 0, G_OPTION_ARG_STRING, &opt_platform, N_("Platform ID"), "ID" },
        { "genre", 0, 0, G_OPTION_ARG_STRING, &opt_genre, N_("Genre"), "TEXT" },
        { "developer", 0, 0, G_OPTION_ARG_STRING, &opt_developer, N_("Developer"), "TEXT" },
        { "publisher", 0, 0, G_OPTION_ARG_STRING, &opt_publisher, N_("Publisher"), "TEXT" },
        { "region", 0, 0, G_OPTION_ARG_STRING, &opt_region, N_("Region"), "TEXT" },
        { "patch-type", 0, 0, G_OPTION_ARG_STRING, &opt_patch_type,
          N_("Patch type: generic/platform/mixed"), "TYPE" },
        { "status", 0, 0, G_OPTION_ARG_STRING, &opt_status,
          N_("ROM status: good/baddump/unknown"), "STATUS" },
        { "format", 0, 0, G_OPTION_ARG_STRING, &opt_format,
          N_("ROM format: raw/zip/7z/chd/cso/wbfs"), "FMT" },
        { "search", 0, 0, G_OPTION_ARG_STRING, &opt_search, N_("Name substring"), "KW" },
        { "tag", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_tags, N_("Tag (repeatable)"), "TAG" },
        { "year-from", 0, 0, G_OPTION_ARG_INT, &opt_year_from, N_("First year"), "YEAR" },
        { "year-to", 0, 0, G_OPTION_ARG_INT, &opt_year_to, N_("Last year"), "YEAR" },
        { "multi", 0, 0, G_OPTION_ARG_NONE, &opt_multi, N_("Multi-disc games only"), NULL },
        { "no-multi", 0, 0, G_OPTION_ARG_NONE, &opt_no_multi, N_("Single-disc games only"), NULL },
        { "has-patch", 0, 0, G_OPTION_ARG_NONE, &opt_has_patch, N_("Games with patches only"), NULL },
        { "no-patch", 0, 0, G_OPTION_ARG_NONE, &opt_no_patch, N_("Games without patches only"), NULL },
        { "sort", 0, 0, G_OPTION_ARG_STRING, &opt_sort,
          N_("Sort key: name/rating/year/discs"), "KEY" },
        { "desc", 0, 0, G_OPTION_ARG_NONE, &opt_desc, N_("Descending order"), NULL },
        { "limit", 0, 0, G_OPTION_ARG_INT, &opt_limit, N_("Result limit (0=200)"), "N" },
        { "offset", 0, 0, G_OPTION_ARG_INT, &opt_offset, N_("Results to skip"), "N" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Search games"));
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

    if (!rh_query_sort_valid(opt_sort))
        return rh_cli_error(_("--sort accepts only name/rating/year/discs"));

    gchar *db = opt_db != NULL ? g_strdup(opt_db) : rh_cli_default_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    RhFilter filter;
    memset(&filter, 0, sizeof filter);
    filter.platform = opt_platform;
    filter.genre = opt_genre;
    filter.developer = opt_developer;
    filter.publisher = opt_publisher;
    filter.region = opt_region;
    filter.patch_type = opt_patch_type;
    filter.status = opt_status;
    filter.format = opt_format;
    filter.kw = opt_search;
    filter.tags = (const char **)opt_tags;
    filter.n_tags = opt_tags != NULL ? (int)g_strv_length(opt_tags) : 0;
    filter.year_from = opt_year_from;
    filter.year_to = opt_year_to;
    filter.multi = opt_multi ? 1 : (opt_no_multi ? 0 : -1);
    filter.has_patch = opt_has_patch ? 1 : (opt_no_patch ? 0 : -1);
    filter.sort = opt_sort;
    filter.desc = opt_desc;
    filter.limit = opt_limit;
    filter.offset = opt_offset;

    RhQueryResult *result = rh_query_run(store, &filter, &err);
    if (result == NULL) {
        store_close(store);
        gchar *msg = g_strdup_printf(_("Query failed: %s"), rh_err_str(err));
        int rc = rh_cli_error(msg);
        g_free(msg);
        return rc;
    }

    rh_cli_print(stdout, "ID\tPLATFORM\tNAME\tYEAR\tRATING\tSTATUS\tFORMAT\n");
    for (guint i = 0; i < result->games->len; i++) {
        RhGame *game = g_ptr_array_index(result->games, i);
        gchar *year = game->year > 0 ? g_strdup_printf("%d", game->year)
                                     : g_strdup("-");
        gchar *rating = game->rating > 0.0
            ? g_strdup_printf("%.1f", game->rating) : g_strdup("-");
        gchar *line = g_strdup_printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
                                      game->id, game->platform, game->name,
                                      year, rating, game->rom_status,
                                      game->rom_format);
        rh_cli_print(stdout, line);
        g_free(line);
        g_free(year);
        g_free(rating);
    }

    gchar *total = rh_cli_format_int64(result->total);
    gchar *summary = g_strdup_printf(_("Total: %s; showing %u\n"),
                                     total, result->games->len);
    rh_cli_print(stderr, summary);
    g_free(summary);
    g_free(total);

    rh_query_result_free(result);
    store_close(store);
    return 0;
}
