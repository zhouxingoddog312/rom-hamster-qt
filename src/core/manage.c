#include "manage.h"

#include <glib/gstdio.h>
#include <string.h>

static gboolean path_is_relative_safe(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return FALSE;
    if (g_path_is_absolute(path) || path[0] == '/' || path[0] == '\\')
        return FALSE;
    if (g_strrstr(path, "..") != NULL)
        return FALSE;
    return TRUE;
}

/* 递归删除目录；只接受调用方已校验过的路径。 */
static RhErr remove_tree(const char *path)
{
    if (!g_file_test(path, G_FILE_TEST_EXISTS))
        return RH_OK;

    if (!g_file_test(path, G_FILE_TEST_IS_DIR))
        return g_remove(path) == 0 ? RH_OK : RH_ERR_IO;

    GDir *dir = g_dir_open(path, 0, NULL);
    if (dir == NULL)
        return RH_ERR_IO;

    RhErr err = RH_OK;
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *child = g_build_filename(path, name, NULL);
        RhErr child_err = remove_tree(child);
        if (child_err != RH_OK)
            err = child_err;
        g_free(child);
    }
    g_dir_close(dir);

    if (g_rmdir(path) != 0)
        err = RH_ERR_IO;
    return err;
}

RhErr rh_manage_update(RhStore *store, const RhGame *game)
{
    if (store == NULL || game == NULL || game->id == NULL)
        return RH_ERR_INVALID;

    RhErr err = RH_OK;
    if (!store_game_id_exists(store, game->id, &err))
        return err != RH_OK ? err : RH_ERR_NOT_FOUND;
    return store_game_update(store, game);
}

RhErr rh_manage_delete(RhStore *store, const char *id,
                       const char *library_root, gboolean delete_files)
{
    if (store == NULL || id == NULL)
        return RH_ERR_INVALID;

    RhErr err = RH_OK;
    RhGame *game = store_game_get(store, id, &err);
    if (err != RH_OK)
        return err;
    if (game == NULL)
        return RH_ERR_NOT_FOUND;

    gchar *dir = NULL;
    if (delete_files) {
        if (library_root == NULL || !path_is_relative_safe(game->dir_path)) {
            rh_game_free(game);
            return RH_ERR_INVALID;
        }
        dir = g_build_filename(library_root, game->dir_path, NULL);
    }
    rh_game_free(game);

    err = store_game_delete(store, id);
    if (err == RH_OK && dir != NULL)
        err = remove_tree(dir);

    g_free(dir);
    return err;
}

static gint compare_disc(gconstpointer a, gconstpointer b)
{
    const RhRomFile *ra = *(const RhRomFile *const *)a;
    const RhRomFile *rb = *(const RhRomFile *const *)b;
    if (ra->disc_no != rb->disc_no)
        return ra->disc_no < rb->disc_no ? -1 : 1;
    return g_strcmp0(ra->filename, rb->filename);
}

RhErr rh_manage_renumber_discs(RhStore *store, const char *id)
{
    if (store == NULL || id == NULL)
        return RH_ERR_INVALID;

    RhErr err = RH_OK;
    GPtrArray *roms = store_roms_for_game(store, id, &err);
    if (err != RH_OK) {
        g_ptr_array_unref(roms);
        return err;
    }

    GPtrArray *discs = g_ptr_array_new();
    for (guint i = 0; i < roms->len; i++) {
        RhRomFile *rom = g_ptr_array_index(roms, i);
        if (rom->disc_no > 0)
            g_ptr_array_add(discs, rom);
    }
    g_ptr_array_sort(discs, compare_disc);

    for (guint i = 0; i < discs->len; i++) {
        RhRomFile *rom = g_ptr_array_index(discs, i);
        int disc_no = (int)i + 1;
        if (rom->disc_no != disc_no) {
            err = store_rom_set_disc(store, id, rom->filename, disc_no);
            if (err != RH_OK)
                break;
        }
    }
    gboolean multi = discs->len >= 2;
    g_ptr_array_unref(discs);
    g_ptr_array_unref(roms);

    if (err != RH_OK)
        return err;

    RhGame *game = store_game_get(store, id, &err);
    if (err != RH_OK)
        return err;
    if (game == NULL)
        return RH_ERR_NOT_FOUND;

    if (game->is_multi_disc != multi) {
        game->is_multi_disc = multi;
        err = store_game_update(store, game);
    }
    rh_game_free(game);
    return err;
}

RhErr rh_manage_set_patch_type(RhStore *store, const char *id, const char *patch_type)
{
    if (store == NULL || id == NULL)
        return RH_ERR_INVALID;
    const char *type = patch_type != NULL ? patch_type : "";
    if (strcmp(type, "") != 0 && strcmp(type, "generic") != 0 &&
        strcmp(type, "platform") != 0 && strcmp(type, "mixed") != 0)
        return RH_ERR_INVALID;

    RhErr err = RH_OK;
    RhGame *game = store_game_get(store, id, &err);
    if (err != RH_OK)
        return err;
    if (game == NULL)
        return RH_ERR_NOT_FOUND;

    g_free(game->patch_type);
    game->patch_type = g_strdup(type);
    err = store_game_update(store, game);
    rh_game_free(game);
    return err;
}

RhErr rh_manage_stats(RhStore *store, RhLibStats *out, RhErr *out_err)
{
    return store_stats(store, out, out_err);
}

GPtrArray *rh_manage_stats_by_platform(RhStore *store, RhErr *out_err)
{
    return store_stats_by_platform(store, out_err);
}

void rh_integrity_issue_free(RhIntegrityIssue *issue)
{
    if (issue == NULL)
        return;
    g_free(issue->game_id);
    g_free(issue->path);
    g_free(issue);
}

RhIntegrityReport *rh_integrity_report_new(void)
{
    RhIntegrityReport *report = g_new0(RhIntegrityReport, 1);
    report->issues = g_ptr_array_new_with_free_func(
        (GDestroyNotify)rh_integrity_issue_free);
    return report;
}

void rh_integrity_report_free(RhIntegrityReport *report)
{
    if (report == NULL)
        return;
    g_ptr_array_unref(report->issues);
    g_free(report);
}

static void add_issue(RhIntegrityReport *report, const char *game_id,
                      const char *path, RhIntegrityKind kind,
                      gint64 expected, gint64 actual)
{
    RhIntegrityIssue *issue = g_new0(RhIntegrityIssue, 1);
    issue->game_id = g_strdup(game_id);
    issue->path = g_strdup(path);
    issue->kind = kind;
    issue->expected = expected;
    issue->actual = actual;
    g_ptr_array_add(report->issues, issue);
}

RhErr rh_manage_integrity(RhStore *store, const char *library_root,
                          RhIntegrityReport *out, RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    if (store == NULL || library_root == NULL || out == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return RH_ERR_INVALID;
    }

    RhGameFilter filter;
    memset(&filter, 0, sizeof filter);
    filter.limit = -1;
    filter.multi = 0;
    filter.has_patch = 0;

    RhErr err = RH_OK;
    GPtrArray *games = store_games_list(store, &filter, &err);
    if (err != RH_OK) {
        g_ptr_array_unref(games);
        if (out_err != NULL)
            *out_err = err;
        return err;
    }

    for (guint i = 0; i < games->len; i++) {
        RhGame *game = g_ptr_array_index(games, i);
        out->games_checked++;

        if (!path_is_relative_safe(game->dir_path)) {
            add_issue(out, game->id, game->dir_path, RH_INTEGRITY_MISSING_DIR,
                      0, 0);
            continue;
        }

        gchar *dir = g_build_filename(library_root, game->dir_path, NULL);
        if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
            add_issue(out, game->id, game->dir_path, RH_INTEGRITY_MISSING_DIR,
                      0, 0);
            g_free(dir);
            continue;
        }

        GPtrArray *roms = store_roms_for_game(store, game->id, &err);
        if (err != RH_OK) {
            g_free(dir);
            break;
        }
        for (guint r = 0; r < roms->len; r++) {
            RhRomFile *rom = g_ptr_array_index(roms, r);
            out->roms_checked++;

            gchar *file = g_build_filename(dir, rom->filename, NULL);
            gchar *rel = g_build_filename(game->dir_path, rom->filename, NULL);
            GStatBuf st;
            if (g_stat(file, &st) != 0) {
                add_issue(out, game->id, rel, RH_INTEGRITY_MISSING_ROM, 0, 0);
            } else if (rom->has_size && (gint64)st.st_size != rom->size) {
                add_issue(out, game->id, rel, RH_INTEGRITY_SIZE_MISMATCH,
                          rom->size, (gint64)st.st_size);
            }
            g_free(file);
            g_free(rel);
        }
        g_ptr_array_unref(roms);
        g_free(dir);
    }

    g_ptr_array_unref(games);
    if (out_err != NULL)
        *out_err = err;
    return err;
}
