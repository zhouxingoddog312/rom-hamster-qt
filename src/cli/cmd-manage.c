#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "cli-common.h"
#include "i18n.h"
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

static void append_int64_field(GString *out, const char *format, gint64 value)
{
    gchar *text = rh_cli_format_int64(value);
    g_string_append_printf(out, format, text);
    g_free(text);
}

int rh_cmd_delete(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { "with-files", 0, 0, G_OPTION_ARG_NONE, &opt_with_files,
          N_("Also delete the game directory under the library root"), NULL },
        { "root", 0, 0, G_OPTION_ARG_FILENAME, &opt_root, N_("Library root"), "DIR" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Delete a game record"));
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
        return rh_cli_error(_("Usage: rom-hamster delete <game-ID> [--with-files --root DIR]"));

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
        gchar *msg = g_strdup_printf(_("Delete failed: %s"), rh_err_str(err));
        return rh_cli_error(msg);
    }
    rh_cli_print(stdout, _("Deleted\n"));
    return 0;
}

int rh_cmd_edit(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { "name", 0, 0, G_OPTION_ARG_STRING, &opt_name, N_("Display name"), "TEXT" },
        { "developer", 0, 0, G_OPTION_ARG_STRING, &opt_developer, N_("Developer"), "TEXT" },
        { "publisher", 0, 0, G_OPTION_ARG_STRING, &opt_publisher, N_("Publisher"), "TEXT" },
        { "genre", 0, 0, G_OPTION_ARG_STRING, &opt_genre, N_("Genre"), "TEXT" },
        { "region", 0, 0, G_OPTION_ARG_STRING, &opt_region, N_("Region"), "TEXT" },
        { "description", 0, 0, G_OPTION_ARG_STRING, &opt_description, N_("Description"), "TEXT" },
        { "patch-type", 0, 0, G_OPTION_ARG_STRING, &opt_patch_type,
          N_("Patch type (empty string clears it)"), "TYPE" },
        { "year", 0, 0, G_OPTION_ARG_INT, &opt_year, N_("Year (0 clears it)"), "YEAR" },
        { "players", 0, 0, G_OPTION_ARG_INT, &opt_players, N_("Players (0 clears it)"), "N" },
        { "rating", 0, 0, G_OPTION_ARG_DOUBLE, &opt_rating, N_("Rating 0-10 (0 clears it)"), "R" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Edit game metadata"));
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
        return rh_cli_error(_("Usage: rom-hamster edit <game-ID> [options]"));

    gchar *db = resolve_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    RhGame *game = store_game_get(store, argv[1], &err);
    if (game == NULL) {
        store_close(store);
        gchar *msg = g_strdup_printf(_("Game not found: %s"), argv[1]);
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
        gchar *msg = g_strdup_printf(_("Update failed: %s"), rh_err_str(err));
        return rh_cli_error(msg);
    }
    rh_cli_print(stdout, _("Updated\n"));
    return 0;
}

int rh_cmd_stats(int argc, char **argv)
{
    GOptionEntry entries[] = {
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Library statistics"));
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

    RhLibStats stats;
    err = rh_manage_stats(store, &stats, &err);
    if (err != RH_OK) {
        store_close(store);
        return rh_cli_error(_("Statistics failed"));
    }

    GString *out = g_string_new(NULL);
    append_int64_field(out, _("Games: %s\n"), stats.games);
    gchar *roms = rh_cli_format_int64(stats.roms);
    gchar *rom_bytes = rh_cli_format_int64(stats.rom_bytes);
    g_string_append_printf(out, _("ROM files: %s; bytes: %s\n"), roms, rom_bytes);
    g_free(roms);
    g_free(rom_bytes);
    gchar *media = rh_cli_format_int64(stats.media);
    gchar *media_bytes = rh_cli_format_int64(stats.media_bytes);
    g_string_append_printf(out, _("Media files: %s; bytes: %s\n"), media, media_bytes);
    g_free(media);
    g_free(media_bytes);
    append_int64_field(out, _("Multi-disc: %s\n"), stats.multi_disc);
    append_int64_field(out, _("Arcade: %s\n"), stats.arcade);
    append_int64_field(out, _("With patches: %s\n"), stats.with_patch);
    append_int64_field(out, _("DAT matched: %s\n"), stats.dat_matched);
    append_int64_field(out, _("Missing ROM records: %s\n"), stats.missing_rom);

    GPtrArray *by_platform = rh_manage_stats_by_platform(store, &err);
    if (err == RH_OK && by_platform->len > 0) {
        g_string_append(out, _("\nPlatform\tGames\tROM files\tBytes\n"));
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
