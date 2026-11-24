#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "dedup.h"
#include "import.h"
#include "rh-hash.h"
#include "rh-test-main.h"
#include "store.h"

#define DATA_FILE RH_TEST_DATA_DIR "/platforms.conf"

static RhPlatformRules *load_rules(void)
{
    RhErr err = RH_ERR_INVALID;
    RhPlatformRules *rules = rh_platform_rules_load(DATA_FILE, &err, NULL);
    g_assert_cmpint(err, ==, RH_OK);
    return rules;
}

static void test_dup_free(gpointer data)
{
    RhImportDup *dup = data;
    if (dup == NULL)
        return;
    g_free(dup->sha1);
    g_free(dup->existing_game_id);
    g_free(dup->existing_rel_path);
    g_free(dup->existing_filename);
    g_free(dup->existing_name);
    g_free(dup);
}

static const guint8 nes_magic[4] = {'N', 'E', 'S', 0x1a};

static gchar *temp_dir(const char *prefix)
{
    gchar *dir = g_dir_make_tmp(prefix, NULL);
    g_assert_nonnull(dir);
    return dir;
}

static void write_pattern(const char *path, const guint8 *prefix,
                          gsize prefix_len, gsize total)
{
    gchar *parent = g_path_get_dirname(path);
    g_mkdir_with_parents(parent, 0755);
    g_free(parent);

    guint8 *data = g_malloc(total);
    memset(data, 0x5a, total);
    if (prefix != NULL && prefix_len > 0)
        memcpy(data, prefix, MIN(prefix_len, total));
    g_assert_true(g_file_set_contents(path, (const char *)data,
                                      (gssize)total, NULL));
    g_free(data);
}

static void hash_path(const char *path, RhHashResult *out)
{
    RhHashOptions options = {.flags = RH_HASH_CRC32 | RH_HASH_SHA1};
    g_assert_cmpint(rh_hash_file(path, &options, out), ==, RH_OK);
}

static RhGame *make_game(const char *id, const char *dir_path, const char *name)
{
    RhGame *game = rh_game_new();
    game->id = g_strdup(id);
    game->platform = g_strdup("gb");
    game->name = g_strdup(name);
    game->game_main_name = g_strdup(name);
    game->dir_path = g_strdup(dir_path);
    return game;
}

static void add_rom_game(RhStore *store, const char *id, const char *dir_path,
                         const char *name, const char *filename,
                         gint64 size, const RhHashResult *hash,
                         const char *canon)
{
    RhGame *game = make_game(id, dir_path, name);
    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    RhRomFile *rom = rh_rom_file_new();
    rom->filename = g_strdup(filename);
    rom->has_size = TRUE;
    rom->size = size;
    if (hash != NULL) {
        rom->crc32 = g_strdup_printf("%08x", hash->crc32);
        rom->sha1 = g_strdup(hash->sha1);
    }
    rom->canon_sha1 = g_strdup(canon);
    g_ptr_array_add(roms, rom);

    g_assert_cmpint(store_game_insert(store, game, roms, NULL, NULL), ==, RH_OK);
    g_ptr_array_unref(roms);
    rh_game_free(game);
}

static void add_media_game(RhStore *store, const char *id, const char *dir_path,
                           const char *name, const char *kind,
                           const char *filename, gint64 size,
                           const RhHashResult *hash)
{
    RhGame *game = make_game(id, dir_path, name);
    GPtrArray *media = g_ptr_array_new_with_free_func(rh_media_file_free_func);
    RhMediaFile *item = rh_media_file_new();
    item->kind = g_strdup(kind);
    item->filename = g_strdup(filename);
    item->has_size = TRUE;
    item->size = size;
    if (hash != NULL) {
        item->crc32 = g_strdup_printf("%08x", hash->crc32);
        item->sha1 = g_strdup(hash->sha1);
    }
    g_ptr_array_add(media, item);

    g_assert_cmpint(store_game_append_media(store, game, media), ==, RH_OK);
    g_ptr_array_unref(media);
    rh_game_free(game);
}

static void remove_db_files(const char *path)
{
    g_remove(path);
    gchar *wal = g_strconcat(path, "-wal", NULL);
    gchar *shm = g_strconcat(path, "-shm", NULL);
    g_remove(wal);
    g_remove(shm);
    g_free(wal);
    g_free(shm);
}

static void test_header_skip(void)
{
    gchar *dir = temp_dir("romham-dedup-XXXXXX");
    gchar *nes = g_build_filename(dir, "game.nes", NULL);
    gchar *snes = g_build_filename(dir, "game.smc", NULL);
    gchar *snes_bin = g_build_filename(dir, "game.bin", NULL);
    gchar *short_nes = g_build_filename(dir, "short.nes", NULL);

    write_pattern(nes, nes_magic, sizeof nes_magic, 16 + 16384);
    write_pattern(snes, NULL, 0, 512 + 32768);
    write_pattern(snes_bin, NULL, 0, 512 + 32768);
    write_pattern(short_nes, nes_magic, sizeof nes_magic, 16 + 100);

    guint64 skip = 999;
    g_assert_true(rh_dedup_header_skip(nes, "game.nes", 16 + 16384, &skip));
    g_assert_cmpuint(skip, ==, 16);
    g_assert_true(rh_dedup_header_skip(snes, "game.smc", 512 + 32768, &skip));
    g_assert_cmpuint(skip, ==, 512);
    g_assert_true(rh_dedup_header_skip(snes_bin, "game.bin", 512 + 32768, &skip));
    g_assert_cmpuint(skip, ==, 0);
    g_assert_true(rh_dedup_header_skip(short_nes, "short.nes", 16 + 100, &skip));
    g_assert_cmpuint(skip, ==, 0);

    g_remove(nes);
    g_remove(snes);
    g_remove(snes_bin);
    g_remove(short_nes);
    g_rmdir(dir);
    g_free(nes);
    g_free(snes);
    g_free(snes_bin);
    g_free(short_nes);
    g_free(dir);
}

static void test_hash_skip(void)
{
    gchar *dir = temp_dir("romham-dedup-XXXXXX");
    gchar *path = g_build_filename(dir, "data.bin", NULL);
    const gsize total = 4096;
    write_pattern(path, NULL, 0, total);

    RhHashOptions options = {.flags = RH_HASH_SHA1, .skip_bytes = 100};
    RhHashResult result;
    g_assert_cmpint(rh_hash_file(path, &options, &result), ==, RH_OK);
    g_assert_cmpuint(result.size, ==, total - 100);

    GChecksum *expected = g_checksum_new(G_CHECKSUM_SHA1);
    guint8 *data = g_malloc(total - 100);
    gsize len = 0;
    gchar *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, &len, NULL));
    memcpy(data, contents + 100, total - 100);
    g_checksum_update(expected, data, total - 100);
    g_assert_cmpstr(result.sha1, ==, g_checksum_get_string(expected));

    g_checksum_free(expected);
    g_free(data);
    g_free(contents);

    options.skip_bytes = total;
    g_assert_cmpint(rh_hash_file(path, &options, &result), ==, RH_ERR_INVALID);

    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_report_and_backfill(void)
{
    gchar *root = temp_dir("romham-dedup-XXXXXX");
    gchar *dbdir = temp_dir("romham-dedup-XXXXXX");
    gchar *db = g_build_filename(dbdir, "romhamster.db", NULL);

    gchar *a = g_build_filename(root, "gb/GameA/a.nes", NULL);
    gchar *b = g_build_filename(root, "gb/GameB/b.nes", NULL);
    gchar *m1 = g_build_filename(root, "gb/GameA/covers/x.png", NULL);
    gchar *m2 = g_build_filename(root, "gb/GameB/covers/x.png", NULL);
    write_pattern(a, nes_magic, sizeof nes_magic, 16 + 16384);
    write_pattern(b, NULL, 0, 16384);
    write_pattern(m1, NULL, 0, 1024);
    write_pattern(m2, NULL, 0, 1024);

    RhHashResult ha, hb, hm;
    hash_path(a, &ha);
    hash_path(b, &hb);
    hash_path(m1, &hm);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(db, &err);
    g_assert_cmpint(err, ==, RH_OK);

    add_rom_game(store, "aaaa0001", "gb/GameA", "GameA", "a.nes",
                 16 + 16384, &ha, hb.sha1);
    add_rom_game(store, "bbbb0001", "gb/GameB", "GameB", "b.nes",
                 16384, &hb, hb.sha1);
    add_media_game(store, "aaaa0001", "gb/GameA", "GameA", "covers",
                   "covers/x.png", 1024, &hm);
    add_media_game(store, "bbbb0001", "gb/GameB", "GameB", "covers",
                   "covers/x.png", 1024, &hm);

    RhDedupOptions options = {.root = root, .include_media = TRUE};
    GPtrArray *groups = NULL;
    RhDedupStats stats;
    g_assert_cmpint(rh_dedup_report(store, &options, &groups, &stats), ==, RH_OK);
    g_assert_cmpuint(stats.clusters, ==, 2);
    g_assert_cmpuint(stats.extra_copies, ==, 2);
    g_assert_cmpuint(groups->len, ==, 2);
    for (guint i = 0; i < groups->len; i++) {
        RhDupGroup *group = g_ptr_array_index(groups, i);
        g_assert_cmpuint(group->members->len, ==, 2);
        for (guint j = 0; j < group->members->len; j++)
            g_assert_false(((RhDupMember *)g_ptr_array_index(group->members, j))->missing);
    }
    g_ptr_array_unref(groups);

    /* min_size 过滤：ROM 簇 16400 保留，媒体簇 1024 被过滤。 */
    RhDedupOptions filtered = {.root = root, .include_media = TRUE,
                               .min_size = 2000};
    g_assert_cmpint(rh_dedup_report(store, &filtered, &groups, &stats), ==, RH_OK);
    g_assert_cmpuint(stats.clusters, ==, 1);
    g_ptr_array_unref(groups);

    /* 补算：新增一个没有摘要的 ROM 记录。 */
    gchar *raw = g_build_filename(root, "gb/Raw/raw.bin", NULL);
    write_pattern(raw, NULL, 0, 512);
    add_rom_game(store, "cccc0001", "gb/Raw", "Raw", "raw.bin", 512, NULL, NULL);

    RhDedupOptions backfill = {.root = root, .platform = "gb"};
    RhDedupStats bstats;
    g_assert_cmpint(rh_dedup_backfill(store, &backfill, &bstats), ==, RH_OK);
    g_assert_cmpuint(bstats.hashed, >=, 1);

    GPtrArray *roms = store_roms_for_game(store, "cccc0001", &err);
    g_assert_cmpuint(roms->len, ==, 1);
    RhRomFile *rom = g_ptr_array_index(roms, 0);
    g_assert_nonnull(rom->sha1);
    g_assert_nonnull(rom->canon_sha1);
    g_ptr_array_unref(roms);

    store_close(store);
    remove_db_files(db);
    g_rmdir(dbdir);
    g_rmdir(root);
    g_free(db);
    g_free(dbdir);
    g_free(root);
    g_free(a);
    g_free(b);
    g_free(m1);
    g_free(m2);
    g_free(raw);
}

static void test_check_file(void)
{
    gchar *root = temp_dir("romham-dedup-XXXXXX");
    gchar *srcroot = temp_dir("romham-dedup-XXXXXX");
    gchar *dbdir = temp_dir("romham-dedup-XXXXXX");
    gchar *db = g_build_filename(dbdir, "romhamster.db", NULL);

    gchar *inside = g_build_filename(root, "gb/Game/known.nes", NULL);
    gchar *outside = g_build_filename(srcroot, "copy.nes", NULL);
    gchar *other = g_build_filename(srcroot, "other.nes", NULL);
    write_pattern(inside, nes_magic, sizeof nes_magic, 16 + 16384);
    write_pattern(outside, NULL, 0, 16384);
    write_pattern(other, NULL, 0, 16384);

    RhHashResult hi;
    hash_path(inside, &hi);
    RhHashOptions tail = {.flags = RH_HASH_SHA1, .skip_bytes = 16};
    RhHashResult hc;
    g_assert_cmpint(rh_hash_file(inside, &tail, &hc), ==, RH_OK);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(db, &err);
    g_assert_cmpint(err, ==, RH_OK);
    add_rom_game(store, "aaaa0001", "gb/Game", "Game", "known.nes",
                 16 + 16384, &hi, hc.sha1);

    RhDupMember *match = NULL;
    g_assert_cmpint(rh_dedup_check_file(store, root, outside, "gb", "copy.nes",
                                        &match), ==, RH_OK);
    g_assert_nonnull(match);
    g_assert_cmpstr(match->filename, ==, "known.nes");
    rh_dup_member_free(match);

    /* 同一文件自身不算重复。 */
    match = NULL;
    g_assert_cmpint(rh_dedup_check_file(store, root, inside, "gb", "known.nes",
                                        &match), ==, RH_OK);
    g_assert_null(match);

    store_close(store);
    remove_db_files(db);
    g_rmdir(dbdir);
    g_rmdir(root);
    g_rmdir(srcroot);
    g_free(db);
    g_free(dbdir);
    g_free(root);
    g_free(srcroot);
    g_free(inside);
    g_free(outside);
    g_free(other);
}

static RhImportPlan *make_named_plan(const char *source_dir, const char *name,
                                     const char *filename)
{
    RhImportPlan *plan = g_new0(RhImportPlan, 1);
    plan->source_dir = g_strdup(source_dir);
    plan->entries = g_ptr_array_new_with_free_func(
        (GDestroyNotify)rh_import_entry_free);

    RhImportPlanEntry *entry = g_new0(RhImportPlanEntry, 1);
    entry->source_subdir = g_strdup("");
    entry->source_subdir_raw = g_bytes_new("", 0);
    entry->platform = g_strdup("gb");
    entry->name = g_strdup(name);
    entry->game_dir = g_strdup_printf("gb/%s", name);
    entry->rom_format = g_strdup("raw");
    entry->roms = g_ptr_array_new_with_free_func(g_free);
    entry->roms_raw = g_ptr_array_new_with_free_func(
        (GDestroyNotify)g_bytes_unref);
    entry->media = g_ptr_array_new_with_free_func(
        (GDestroyNotify)rh_import_media_free);
    entry->patches = g_ptr_array_new_with_free_func(g_free);
    entry->patches_raw = g_ptr_array_new_with_free_func(
        (GDestroyNotify)g_bytes_unref);
    entry->dups = g_ptr_array_new_with_free_func(test_dup_free);

    g_ptr_array_add(entry->roms, g_strdup(filename));
    g_ptr_array_add(entry->roms_raw, g_bytes_new(filename, strlen(filename)));
    g_ptr_array_add(plan->entries, entry);
    return plan;
}

static RhImportPlan *make_single_plan(const char *source_dir, const char *name)
{
    return make_named_plan(source_dir, name, "c.nes");
}

/* 同名的同内容 ROM 选择共存时，必须写成 "<stem> (2)<ext>"，不能被跳过。 */
static void test_import_coexist_same_name(void)
{
    gchar *root = temp_dir("romham-dedup-XXXXXX");
    gchar *srcroot = temp_dir("romham-dedup-XXXXXX");
    gchar *dbdir = temp_dir("romham-dedup-XXXXXX");
    gchar *db = g_build_filename(dbdir, "romhamster.db", NULL);

    gchar *existing = g_build_filename(root, "gb/GameA/game.nes", NULL);
    gchar *src = g_build_filename(srcroot, "game.nes", NULL);
    write_pattern(existing, NULL, 0, 16384);
    write_pattern(src, NULL, 0, 16384);

    RhHashResult he;
    hash_path(existing, &he);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(db, &err);
    g_assert_cmpint(err, ==, RH_OK);
    add_rom_game(store, "aaaa0001", "gb/GameA", "GameA", "game.nes", 16384,
                 &he, NULL);

    RhImportPlan *plan = make_named_plan(srcroot, "GameA", "game.nes");
    RhImportPlanEntry *entry = g_ptr_array_index(plan->entries, 0);
    g_assert_cmpint(rh_import_plan_detect_duplicates(plan, store, root, NULL,
                                                     &err), ==, RH_OK);
    g_assert_cmpuint(entry->dups->len, ==, 1);
    RhImportDup *dup = g_ptr_array_index(entry->dups, 0);
    g_assert_true(dup->same_name);
    g_assert_true(rh_import_dup_set_action(entry, 0, RH_DUP_COEXIST));

    RhImportExecOptions options = {
        .root = root,
        .source_dir = srcroot,
        .name_mode = RH_IMPORT_NAMES_ORIGINAL,
    };
    RhImportExecStats stats;
    RhPlatformRules *rules = load_rules();
    g_assert_cmpint(rh_import_execute(store, rules, plan, &options, &stats),
                    ==, RH_OK);
    rh_platform_rules_free(rules);
    g_assert_cmpuint(stats.dup_coexisted, ==, 1);

    gchar *renamed = g_build_filename(root, "gb/GameA/game (2).nes", NULL);
    g_assert_true(g_file_test(renamed, G_FILE_TEST_EXISTS));

    g_free(renamed);
    rh_import_plan_free(plan);
    store_close(store);
    remove_db_files(db);
    g_rmdir(dbdir);
    g_rmdir(root);
    g_rmdir(srcroot);
    g_free(db);
    g_free(dbdir);
    g_free(root);
    g_free(srcroot);
    g_free(existing);
    g_free(src);
}

static void test_import_coexist(void)
{
    gchar *root = temp_dir("romham-dedup-XXXXXX");
    gchar *srcroot = temp_dir("romham-dedup-XXXXXX");
    gchar *dbdir = temp_dir("romham-dedup-XXXXXX");
    gchar *db = g_build_filename(dbdir, "romhamster.db", NULL);

    gchar *existing = g_build_filename(root, "gb/GameA/known.nes", NULL);
    gchar *target = g_build_filename(root, "gb/GameC/c.nes", NULL);
    gchar *src = g_build_filename(srcroot, "c.nes", NULL);
    write_pattern(existing, nes_magic, sizeof nes_magic, 16 + 16384);
    write_pattern(target, NULL, 0, 100);   /* 占位，触发改名共存 */
    write_pattern(src, NULL, 0, 16384);    /* 与 existing 去头后同内容 */

    RhHashResult he;
    hash_path(existing, &he);
    RhHashOptions tail = {.flags = RH_HASH_SHA1, .skip_bytes = 16};
    RhHashResult hc;
    g_assert_cmpint(rh_hash_file(existing, &tail, &hc), ==, RH_OK);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(db, &err);
    g_assert_cmpint(err, ==, RH_OK);
    add_rom_game(store, "aaaa0001", "gb/GameA", "GameA", "known.nes",
                 16 + 16384, &he, hc.sha1);

    RhImportPlan *plan = make_single_plan(srcroot, "GameC");
    RhImportPlanEntry *entry = g_ptr_array_index(plan->entries, 0);
    g_assert_cmpint(rh_import_plan_detect_duplicates(plan, store, root, NULL,
                                                     &err), ==, RH_OK);
    g_assert_cmpuint(entry->dups->len, ==, 1);
    g_assert_cmpint(((RhImportDup *)g_ptr_array_index(entry->dups, 0))->action,
                    ==, RH_DUP_IGNORE);
    g_assert_true(rh_import_dup_set_action(entry, 0, RH_DUP_COEXIST));

    RhImportExecOptions options = {
        .root = root,
        .source_dir = srcroot,
        .name_mode = RH_IMPORT_NAMES_ORIGINAL,
    };
    RhImportExecStats stats;
    RhPlatformRules *rules = load_rules();
    g_assert_cmpint(rh_import_execute(store, rules, plan, &options, &stats),
                    ==, RH_OK);
    rh_platform_rules_free(rules);
    g_assert_cmpuint(stats.dup_coexisted, ==, 1);
    gchar *renamed = g_build_filename(root, "gb/GameC/c (2).nes", NULL);
    g_assert_true(g_file_test(renamed, G_FILE_TEST_EXISTS));

    g_free(renamed);
    rh_import_plan_free(plan);
    store_close(store);
    remove_db_files(db);
    g_rmdir(dbdir);
    g_rmdir(root);
    g_rmdir(srcroot);
    g_free(db);
    g_free(dbdir);
    g_free(root);
    g_free(srcroot);
    g_free(existing);
    g_free(target);
    g_free(src);
}

static void test_import_overwrite(void)
{
    gchar *root = temp_dir("romham-dedup-XXXXXX");
    gchar *srcroot = temp_dir("romham-dedup-XXXXXX");
    gchar *dbdir = temp_dir("romham-dedup-XXXXXX");
    gchar *db = g_build_filename(dbdir, "romhamster.db", NULL);

    gchar *existing = g_build_filename(root, "gb/GameA/known.nes", NULL);
    gchar *src = g_build_filename(srcroot, "c.nes", NULL);
    write_pattern(existing, NULL, 0, 16384);
    write_pattern(src, NULL, 0, 16384);

    RhHashResult he;
    hash_path(existing, &he);

    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(db, &err);
    g_assert_cmpint(err, ==, RH_OK);
    add_rom_game(store, "aaaa0001", "gb/GameA", "GameA", "known.nes",
                 16384, &he, NULL);

    RhImportPlan *plan = make_single_plan(srcroot, "GameC");
    RhImportPlanEntry *entry = g_ptr_array_index(plan->entries, 0);
    g_assert_cmpint(rh_import_plan_detect_duplicates(plan, store, root, NULL,
                                                     &err), ==, RH_OK);
    g_assert_cmpuint(entry->dups->len, ==, 1);
    g_assert_true(rh_import_dup_set_action(entry, 0, RH_DUP_OVERWRITE));

    RhImportExecOptions options = {
        .root = root,
        .source_dir = srcroot,
        .name_mode = RH_IMPORT_NAMES_ORIGINAL,
    };
    RhImportExecStats stats;
    RhPlatformRules *rules = load_rules();
    g_assert_cmpint(rh_import_execute(store, rules, plan, &options, &stats),
                    ==, RH_OK);
    rh_platform_rules_free(rules);
    g_assert_cmpuint(stats.dup_overwritten, ==, 1);

    GPtrArray *roms = store_roms_for_game(store, "aaaa0001", &err);
    g_assert_cmpuint(roms->len, ==, 1);
    RhRomFile *rom = g_ptr_array_index(roms, 0);
    g_assert_nonnull(rom->sha1);
    g_assert_nonnull(rom->canon_sha1);
    g_ptr_array_unref(roms);

    gchar *new_dir = g_build_filename(root, "gb/GameC", NULL);
    g_assert_false(g_file_test(new_dir, G_FILE_TEST_IS_DIR));
    g_free(new_dir);

    rh_import_plan_free(plan);
    store_close(store);
    remove_db_files(db);
    g_rmdir(dbdir);
    g_rmdir(root);
    g_rmdir(srcroot);
    g_free(db);
    g_free(dbdir);
    g_free(root);
    g_free(srcroot);
    g_free(existing);
    g_free(src);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/dedup/header-skip", test_header_skip);
    g_test_add_func("/dedup/hash-skip", test_hash_skip);
    g_test_add_func("/dedup/report-and-backfill", test_report_and_backfill);
    g_test_add_func("/dedup/check-file", test_check_file);
    g_test_add_func("/dedup/import-coexist", test_import_coexist);
    g_test_add_func("/dedup/import-coexist-same-name",
                    test_import_coexist_same_name);
    g_test_add_func("/dedup/import-overwrite", test_import_overwrite);
    return g_test_run();
}
