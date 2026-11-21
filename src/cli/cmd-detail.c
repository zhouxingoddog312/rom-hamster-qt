#include <glib.h>

#include "cli-common.h"
#include "detail.h"
#include "i18n.h"

static gchar *opt_db = NULL;

static const char *kind_label(const char *kind)
{
    if (g_strcmp0(kind, "covers") == 0) return _("Cover");
    if (g_strcmp0(kind, "screenshots") == 0) return _("Screenshot");
    if (g_strcmp0(kind, "videos") == 0) return _("Video");
    if (g_strcmp0(kind, "marquees") == 0) return _("Marquee");
    if (g_strcmp0(kind, "miximages") == 0) return _("Mix image");
    if (g_strcmp0(kind, "intro") == 0) return _("Intro");
    if (g_strcmp0(kind, "patcher") == 0) return _("Patch");
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
        { "db", 0, 0, G_OPTION_ARG_FILENAME, &opt_db, N_("Database file"), "PATH" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *ctx = g_option_context_new(_("- Show game details"));
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
        return rh_cli_error(_("Usage: rom-hamster show <game-ID> [--db PATH]"));

    gchar *db = opt_db != NULL ? g_strdup(opt_db) : rh_cli_default_db();
    RhErr err = RH_OK;
    RhStore *store = rh_cli_open_store(db, &err);
    g_free(db);
    if (store == NULL)
        return 1;

    RhGameDetail *detail = rh_detail_load(store, argv[1], TRUE, &err);
    if (detail == NULL) {
        store_close(store);
        gchar *msg = g_strdup_printf(_("Game not found: %s (%s)"), argv[1],
                                     rh_err_str(err));
        int rc = rh_cli_error(msg);
        g_free(msg);
        return rc;
    }

    RhGame *game = detail->game;
    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "ID: %s\n", game->id);
    append_field(out, _("Name"), game->name);
    append_field(out, _("Main name"), game->game_main_name);
    append_field(out, _("Platform"), game->platform);
    append_field(out, _("Directory"), game->dir_path);
    if (game->year > 0)
        g_string_append_printf(out, _("Year: %d\n"), game->year);
    append_field(out, _("Genre"), game->genre);
    if (game->players > 0)
        g_string_append_printf(out, _("Players: %d\n"), game->players);
    if (game->rating > 0.0)
        g_string_append_printf(out, _("Rating: %.1f\n"), game->rating);
    append_field(out, _("Region"), game->region);
    append_field(out, _("Developer"), game->developer);
    append_field(out, _("Publisher"), game->publisher);
    append_field(out, _("Status"), game->rom_status);
    append_field(out, _("Format"), game->rom_format);
    append_field(out, _("DAT source"), game->dat_source);
    append_field(out, _("Patch type"), game->patch_type);
    g_string_append_printf(out, _("Multi-disc: %s\n"),
                           game->is_multi_disc ? _("Yes") : _("No"));
    g_string_append_printf(out, _("Arcade: %s\n"),
                           game->is_arcade ? _("Yes") : _("No"));
    if (game->description != NULL && game->description[0] != '\0')
        g_string_append_printf(out, _("Description: %s\n"), game->description);

    if (detail->tags->len > 0) {
        g_string_append(out, _("Tags:"));
        for (guint i = 0; i < detail->tags->len; i++)
            g_string_append_printf(out, " %s", (char *)g_ptr_array_index(detail->tags, i));
        g_string_append_c(out, '\n');
    }

    gchar *rom_bytes = rh_cli_format_int64(detail->rom_bytes);
    g_string_append_printf(out, _("ROM files: %u; bytes: %s\n"),
                           detail->roms->len, rom_bytes);
    g_free(rom_bytes);
    for (guint i = 0; i < detail->roms->len; i++) {
        RhRomFile *rom = g_ptr_array_index(detail->roms, i);
        g_string_append_printf(out, "  [%u] %s", i + 1, rom->filename);
        if (rom->disc_no > 0)
            g_string_append_printf(out, _("  Disc %d"), rom->disc_no);
        if (rom->has_size) {
            gchar *size = rh_cli_format_int64(rom->size);
            g_string_append_printf(out, _("  %s bytes"), size);
            g_free(size);
        }
        if (rom->sha1 != NULL)
            g_string_append_printf(out, "  sha1=%s", rom->sha1);
        g_string_append_c(out, '\n');
    }

    gchar *media_bytes = rh_cli_format_int64(detail->media_bytes);
    g_string_append_printf(out, _("Media files: %u; bytes: %s\n"),
                           detail->media->len, media_bytes);
    g_free(media_bytes);
    for (guint i = 0; i < detail->media->len; i++) {
        RhMediaFile *media = g_ptr_array_index(detail->media, i);
        g_string_append_printf(out, "  %s: %s\n", kind_label(media->kind),
                               media->filename);
    }

    if (detail->dat_matches->len > 0) {
        g_string_append(out, _("DAT matches:\n"));
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
