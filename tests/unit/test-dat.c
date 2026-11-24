#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "dat.h"
#include "rh-test-main.h"
#include "store.h"

/* ---------------------------------------------------------------------------
 * 工具
 * ------------------------------------------------------------------------- */

static void cleanup_dir(const char *dir)
{
    GDir *d = g_dir_open(dir, 0, NULL);
    if (d != NULL) {
        const gchar *name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar *path = g_build_filename(dir, name, NULL);
            g_remove(path);
            g_free(path);
        }
        g_dir_close(d);
    }
    g_rmdir(dir);
}

static RhStore *open_store_in(const char *dir, gchar **out_db_path)
{
    gchar *db_path = g_build_filename(dir, "romhamster.db", NULL);
    RhErr err = RH_ERR_INVALID;
    RhStore *store = store_open(db_path, &err);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_nonnull(store);
    if (out_db_path != NULL)
        *out_db_path = db_path;
    else
        g_free(db_path);
    return store;
}

static gchar *write_bytes(const char *dir, const char *name,
                          const gchar *bytes, gsize len)
{
    gchar *path = g_build_filename(dir, name, NULL);
    GError *error = NULL;
    g_assert_true(g_file_set_contents(path, bytes, (gssize)len, &error));
    g_clear_error(&error);
    return path;
}

static gchar *write_text(const char *dir, const char *name, const char *text)
{
    return write_bytes(dir, name, text, strlen(text));
}

static RhHashResult hash_with_crc(guint32 value)
{
    RhHashResult hash;
    memset(&hash, 0, sizeof hash);
    hash.has_crc32 = TRUE;
    hash.crc32 = value;
    return hash;
}

typedef struct {
    int      calls;
    guint64  last_done;
    guint64  last_total;
    gboolean cancel_at_first_chunk;
} ProgressState;

static gboolean progress_cb(const gchar *path, gint64 entries, guint64 done,
                            guint64 total, gpointer user_data)
{
    (void)path;
    (void)entries;
    ProgressState *state = user_data;
    state->calls++;
    state->last_done = done;
    state->last_total = total;
    if (state->cancel_at_first_chunk && done > 0)
        return FALSE;
    return TRUE;
}

static const char *SAMPLE_DAT =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE datafile PUBLIC \"-//Logiqx//DTD ROM Management Datafile//EN\"\n"
    "  \"http://www.logiqx.com/Dats/datafile.dtd\">\n"
    "<datafile>\n"
    "  <header>\n"
    "    <name>Test DAT</name>\n"
    "    <description>Unit test datafile</description>\n"
    "    <version>20260101</version>\n"
    "  </header>\n"
    "  <game name=\"Alpha (World)\">\n"
    "    <description>Alpha</description>\n"
    "    <rom name=\"alpha.bin\" size=\"1024\" crc=\"AABBCCDD\"\n"
    "         md5=\"0123456789abcdef0123456789abcdef\"\n"
    "         sha1=\"0123456789abcdef0123456789abcdef01234567\"\n"
    "         status=\"verified\"/>\n"
    "    <rom name=\"shared.bin\" size=\"16\" crc=\"11111111\"/>\n"
    "  </game>\n"
    "  <game name=\"Beta (Japan)\" cloneof=\"Alpha (World)\"\n"
    "        romof=\"Alpha (World)\">\n"
    "    <rom name=\"shared.bin\" size=\"16\" crc=\"11111111\"/>\n"
    "    <rom name=\"beta.bin\" size=\"2048\"\n"
    "         sha1=\"89abcdef0123456789abcdef0123456789abcdef\"/>\n"
    "  </game>\n"
    "</datafile>\n";

/* ---------------------------------------------------------------------------
 * 正常 Logiqx 样本：重复 rom 名、parent/clone、来源可追溯
 * ------------------------------------------------------------------------- */

static void test_logiqx_basic(void)
{
    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *dat_path = write_text(dir, "test.dat", SAMPLE_DAT);
    RhStore *store = open_store_in(dir, NULL);

    RhDatImportOptions options = {0};
    options.platform = "gb";

    RhDatImportStats stats;
    RhErr err = rh_dat_import(store, dat_path, &options, &stats);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpint(stats.games, ==, 2);
    g_assert_cmpint(stats.entry_count, ==, 4); /* shared.bin 在两个 game 下各一条 */
    g_assert_cmpint(stats.skipped, ==, 0);
    g_assert_cmpint(stats.bad_fields, ==, 0);
    g_assert_false(stats.encoding_guessed);
    g_assert_false(stats.xml_error);
    g_assert_cmpstr(stats.encoding, ==, "utf-8");

    gint64 dat_count = 0;
    g_assert_cmpint(store_dat_count(store, &dat_count), ==, RH_OK);
    g_assert_cmpint(dat_count, ==, 1);

    /* crc32 唯一命中：名字、大小、状态、DAT 来源都对得上 */
    GPtrArray *matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "aabbccdd", 1024, "gb",
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    RhDatMatch *match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->game_name, ==, "Alpha (World)");
    g_assert_cmpstr(match->entry->rom_name, ==, "alpha.bin");
    g_assert_true(match->entry->has_size);
    g_assert_cmpint(match->entry->size, ==, 1024);
    g_assert_cmpstr(match->entry->status, ==, "verified");
    g_assert_cmpstr(match->dat_name, ==, "Test DAT");
    g_assert_cmpstr(match->dat_version, ==, "20260101");
    g_assert_cmpstr(match->platform, ==, "gb");
    g_assert_true(g_str_has_suffix(match->dat_path, "test.dat"));
    gint64 dat_id = match->dat_id;

    /* 同一 crc32 两条候选：多义而不是随便挑一个 */
    g_ptr_array_unref(matches);
    matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "11111111", 16, NULL,
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 2);
    g_ptr_array_unref(matches);

    /* 匹配入口：唯一 / 二义 */
    RhHashResult hash = hash_with_crc(0xAABBCCDDu);
    g_assert_cmpint(rh_dat_match(store, &hash, "gb", &matches, NULL),
                    ==, RH_DAT_MATCH_UNIQUE);
    g_ptr_array_unref(matches);

    hash = hash_with_crc(0x11111111u);
    g_assert_cmpint(rh_dat_match(store, &hash, "gb", &matches, NULL),
                    ==, RH_DAT_MATCH_AMBIGUOUS);
    g_assert_cmpuint(matches->len, ==, 2);
    g_ptr_array_unref(matches);

    /* parent/clone 结构原样保留 */
    g_assert_cmpint(store_dat_find_by_hash(
                        store, "sha1", "89abcdef0123456789abcdef0123456789abcdef",
                        2048, NULL, &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->cloneof, ==, "Alpha (World)");
    g_assert_cmpstr(match->entry->romof, ==, "Alpha (World)");
    g_ptr_array_unref(matches);

    /* 入库可追溯：ROM 记录能指回 dat_id */
    RhGame *game = rh_game_new();
    game->id = g_strdup("game0001");
    game->platform = g_strdup("gb");
    game->name = g_strdup("Alpha");
    game->dir_path = g_strdup("gb/Alpha");
    RhRomFile *rom = rh_rom_file_new();
    rom->filename = g_strdup("alpha.bin");
    rom->dat_id = dat_id;
    GPtrArray *roms = g_ptr_array_new_with_free_func(rh_rom_file_free_func);
    g_ptr_array_add(roms, rom);
    g_assert_cmpint(store_game_insert(store, game, roms, NULL, NULL), ==, RH_OK);
    rh_game_free(game);
    g_ptr_array_unref(roms);

    GPtrArray *loaded = store_roms_for_game(store, "game0001", NULL);
    g_assert_cmpuint(loaded->len, ==, 1);
    RhRomFile *loaded_rom = g_ptr_array_index(loaded, 0);
    g_assert_cmpint(loaded_rom->dat_id, ==, dat_id);
    g_ptr_array_unref(loaded);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(dir);
}

/* ---------------------------------------------------------------------------
 * 大文件：流式读取、分块进度
 * ------------------------------------------------------------------------- */

static void test_large_stream(void)
{
    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);

    const int game_count = 20000;
    GString *xml = g_string_sized_new(8 * 1024 * 1024);
    g_string_append(xml, "<?xml version=\"1.0\"?>\n<datafile>\n<header>"
                         "<name>Big</name><version>1</version></header>\n");
    for (int i = 0; i < game_count; i++) {
        g_string_append_printf(
            xml,
            "<game name=\"Game %d\"><rom name=\"g%05d.bin\" size=\"%d\""
            " crc=\"%08x\"/></game>\n",
            i, i, i + 1, (unsigned)i);
    }
    g_string_append(xml, "</datafile>\n");

    gchar *dat_path = write_bytes(dir, "big.dat", xml->str, xml->len);
    guint64 file_size = xml->len;
    g_string_free(xml, TRUE);

    RhStore *store = open_store_in(dir, NULL);

    ProgressState progress = {0};
    RhDatImportOptions options = {0};
    options.platform = "gb";
    options.chunk_size = 4096;
    options.batch_size = 500;
    options.progress = progress_cb;
    options.user_data = &progress;

    RhDatImportStats stats;
    RhErr err = rh_dat_import(store, dat_path, &options, &stats);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpint(stats.games, ==, game_count);
    g_assert_cmpint(stats.entry_count, ==, game_count);
    g_assert_cmpint(stats.skipped, ==, 0);

    /* 流式：进度回调按块触发，且总量与文件大小一致 */
    g_assert_cmpint(progress.calls, >, 10);
    g_assert_cmpuint(progress.last_total, ==, file_size);
    g_assert_cmpuint(progress.last_done, ==, file_size);

    GPtrArray *matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "00004e1f", 20000, "gb",
                                           &matches), ==, RH_OK); /* 19999 */
    g_assert_cmpuint(matches->len, ==, 1);
    g_ptr_array_unref(matches);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(dir);
}

/* ---------------------------------------------------------------------------
 * SHA1 兜底：DAT 里没有 crc，只有 sha1
 * ------------------------------------------------------------------------- */

static void test_sha1_fallback(void)
{
    static const char *dat_text =
        "<?xml version=\"1.0\"?>\n<datafile>\n<header><name>ShaOnly</name>"
        "</header>\n"
        "<game name=\"Solo\"><rom name=\"solo.bin\" size=\"42\""
        " sha1=\"deadbeefdeadbeefdeadbeefdeadbeefdeadbeef\"/></game>\n"
        "</datafile>\n";

    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *dat_path = write_text(dir, "sha.dat", dat_text);
    RhStore *store = open_store_in(dir, NULL);

    RhDatImportOptions options = {0};
    options.platform = "nes";
    RhDatImportStats stats;
    g_assert_cmpint(rh_dat_import(store, dat_path, &options, &stats), ==, RH_OK);

    RhHashResult hash;
    memset(&hash, 0, sizeof hash);
    hash.has_crc32 = TRUE;
    hash.crc32 = 0x12345678u; /* 库里没有这个 crc */
    hash.has_sha1 = TRUE;
    g_strlcpy(hash.sha1, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
              sizeof hash.sha1);

    GPtrArray *matches = NULL;
    RhErr err = RH_OK;
    g_assert_cmpint(rh_dat_match(store, &hash, "nes", &matches, &err),
                    ==, RH_DAT_MATCH_UNIQUE);
    g_assert_cmpint(err, ==, RH_OK);
    RhDatMatch *match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->rom_name, ==, "solo.bin");
    g_assert_cmpstr(match->dat_name, ==, "ShaOnly");
    g_ptr_array_unref(matches);

    /* 平台不匹配时不命中 */
    g_assert_cmpint(rh_dat_match(store, &hash, "gb", &matches, &err),
                    ==, RH_DAT_MATCH_NONE);
    g_assert_cmpuint(matches->len, ==, 0);
    g_ptr_array_unref(matches);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(dir);
}

/* ---------------------------------------------------------------------------
 * 缺字段与畸形 XML：跳过坏条目，保留已解析的部分
 * ------------------------------------------------------------------------- */

static void test_missing_fields_and_malformed(void)
{
    static const char *dat_text =
        "<?xml version=\"1.0\"?>\n<datafile>\n<header><name>Broken</name>"
        "</header>\n"
        "<game name=\"Good\">"
        "<rom name=\"good.bin\" size=\"5\" crc=\"ZZZZZZZZ\"/>"
        "<rom size=\"7\"/>"          /* 没有 rom 名 */
        "<rom name=\"\"/>"
        "</game>\n"
        "<game>"                       /* 没有 game 名 */
        "<rom name=\"orphan.bin\" crc=\"22222222\"/>"
        "</game>\n"
        "<game name=\"Partial\">"
        "<rom name=\"partial.bin\" crc=\"33333333\"/>"
        "</game>\n"
        "<game name=\"Unclosed\"><rom name=\"late.bin\" crc=\"44444444\"/>\n";

    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *dat_path = write_text(dir, "broken.dat", dat_text);
    RhStore *store = open_store_in(dir, NULL);

    RhDatImportOptions options = {0};
    options.platform = "gb";
    RhDatImportStats stats;
    RhErr err = rh_dat_import(store, dat_path, &options, &stats);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_true(stats.xml_error);
    /* Good、无名的 game、Partial 都已闭合；最后未闭合的 game 在 </datafile>
     * 处触发结构错误，其 rom 是在出错前解析到的，保留入库。 */
    g_assert_cmpint(stats.games, ==, 3);
    g_assert_cmpint(stats.skipped, ==, 3); /* 无 rom 名、空 rom 名、无 game 名 */
    g_assert_cmpint(stats.bad_fields, ==, 1); /* ZZZZZZZZ */
    g_assert_cmpint(stats.entry_count, ==, 3);

    /* 坏哈希被丢弃，条目本身保留 */
    GPtrArray *matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "33333333", 0, "gb",
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    g_assert_cmpstr(((RhDatMatch *)g_ptr_array_index(matches, 0))->entry->game_name,
                    ==, "Partial");
    g_ptr_array_unref(matches);

    /* 空 DAT（一条都没有）视为无效，不留下空壳行 */
    gchar *empty_path = write_text(dir, "empty.dat",
                                   "<?xml version=\"1.0\"?><datafile></datafile>");
    g_assert_cmpint(rh_dat_import(store, empty_path, &options, &stats),
                    ==, RH_ERR_INVALID);
    gint64 dat_count = 0;
    g_assert_cmpint(store_dat_count(store, &dat_count), ==, RH_OK);
    g_assert_cmpint(dat_count, ==, 1);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(empty_path);
    g_free(dir);
}

/* ---------------------------------------------------------------------------
 * 取消：不留半条 DAT
 * ------------------------------------------------------------------------- */

static void test_cancel(void)
{
    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);

    GString *xml = g_string_sized_new(1024 * 1024);
    g_string_append(xml, "<?xml version=\"1.0\"?>\n<datafile>\n<header>"
                         "<name>Cancel</name></header>\n");
    for (int i = 0; i < 5000; i++) {
        g_string_append_printf(
            xml, "<game name=\"G%d\"><rom name=\"g%d.bin\" size=\"%d\""
                 " crc=\"%08x\"/></game>\n", i, i, i, (unsigned)i);
    }
    g_string_append(xml, "</datafile>\n");

    gchar *dat_path = write_bytes(dir, "cancel.dat", xml->str, xml->len);
    g_string_free(xml, TRUE);
    RhStore *store = open_store_in(dir, NULL);

    ProgressState progress = {0};
    progress.cancel_at_first_chunk = TRUE;
    RhDatImportOptions options = {0};
    options.platform = "gb";
    options.chunk_size = 4096;
    options.progress = progress_cb;
    options.user_data = &progress;

    RhDatImportStats stats;
    RhErr err = rh_dat_import(store, dat_path, &options, &stats);
    g_assert_cmpint(err, ==, RH_ERR_CANCELLED);
    g_assert_true(stats.cancelled);
    g_assert_cmpint(progress.calls, ==, 1);

    gint64 dat_count = -1;
    g_assert_cmpint(store_dat_count(store, &dat_count), ==, RH_OK);
    g_assert_cmpint(dat_count, ==, 0);

    GPtrArray *matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "00000000", 0, NULL,
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 0);
    g_ptr_array_unref(matches);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(dir);
}

/* ---------------------------------------------------------------------------
 * 非 UTF-8 DAT：GB18030 推测、XML 声明、非法字节转义
 * ------------------------------------------------------------------------- */

static gchar *to_gb18030(const char *utf8, gsize *out_len)
{
    gsize len = 0;
    GError *error = NULL;
    gchar *bytes = g_convert(utf8, -1, "GB18030", "UTF-8", NULL, &len, &error);
    g_assert_no_error(error);
    g_assert_nonnull(bytes);
    if (out_len != NULL)
        *out_len = len;
    return bytes;
}

static void test_gbk_dat(void)
{
    /* 前 64 KiB 全是 ASCII（注释填充），把中文放到嗅探窗口之后，
     * 逼出"UTF-8 解析失败 → 整份按 GB18030 重来"的路径。 */
    GString *xml = g_string_sized_new(128 * 1024);
    g_string_append(xml, "<?xml version=\"1.0\"?>\n<datafile>\n<header>\n");
    while (xml->len < 70 * 1024)
        g_string_append(xml, "<!-- padding padding padding padding -->\n");
    g_string_append(xml,
        "<name>中文测试 DAT</name><version>2026</version>\n"
        "</header>\n"
        "<game name=\"游戏一\"><rom name=\"rom1.bin\" size=\"10\""
        " crc=\"aa000001\"/></game>\n"
        "<game name=\"游戏二\"><rom name=\"rom2.bin\" size=\"20\""
        " crc=\"aa000002\"/></game>\n"
        "</datafile>\n");

    gsize gbk_len = 0;
    gchar *gbk = to_gb18030(xml->str, &gbk_len);
    g_string_free(xml, TRUE);

    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *dat_path = write_bytes(dir, "gbk.dat", gbk, gbk_len);
    g_free(gbk);

    RhStore *store = open_store_in(dir, NULL);
    RhDatImportOptions options = {0};
    options.platform = "gb";
    RhDatImportStats stats;
    g_assert_cmpint(rh_dat_import(store, dat_path, &options, &stats), ==, RH_OK);
    g_assert_true(stats.encoding_guessed);
    g_assert_false(stats.encoding_declared);
    g_assert_false(stats.escaped);
    g_assert_cmpstr(stats.encoding, ==, "gb18030");
    g_assert_cmpint(stats.entry_count, ==, 2);

    GPtrArray *matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "aa000001", 10, NULL,
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    RhDatMatch *match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->game_name, ==, "游戏一");
    g_assert_cmpstr(match->dat_name, ==, "中文测试 DAT");
    g_ptr_array_unref(matches);

    /* 带 XML 声明的 GB18030：不猜、按声明解码 */
    static const char *declared_utf8 =
        "<?xml version=\"1.0\" encoding=\"GB18030\"?>\n"
        "<datafile><header><name>声明 DAT</name></header>"
        "<game name=\"声明游戏\"><rom name=\"d.bin\" size=\"1\""
        " crc=\"bb000001\"/></game></datafile>\n";
    gsize declared_len = 0;
    gchar *declared_bytes = to_gb18030(declared_utf8, &declared_len);
    gchar *declared_path = write_bytes(dir, "declared.dat", declared_bytes,
                                       declared_len);
    g_free(declared_bytes);

    g_assert_cmpint(rh_dat_import(store, declared_path, &options, &stats),
                    ==, RH_OK);
    g_assert_true(stats.encoding_declared);
    g_assert_false(stats.encoding_guessed);
    g_assert_cmpstr(stats.encoding, ==, "gb18030");

    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "bb000001", 1, NULL,
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->game_name, ==, "声明游戏");
    g_assert_cmpstr(match->dat_name, ==, "声明 DAT");
    g_ptr_array_unref(matches);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(declared_path);
    g_free(dir);
}

static void test_invalid_bytes_escaped(void)
{
    /* 非法 GB18030 字节：按 %NN 转义保留，条目不得消失 */
    GString *xml = g_string_sized_new(16 * 1024);
    g_string_append(xml, "<?xml version=\"1.0\"?>\n<datafile>\n<header>"
                         "<name>Esc</name></header>\n");
    g_string_append(xml, "<game name=\"Bad");
    g_string_append_c(xml, (gchar)0xFF);
    g_string_append(xml, "Name\"><rom name=\"esc.bin\" size=\"3\""
                         " crc=\"cc000001\"/></game>\n</datafile>\n");

    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *dat_path = write_bytes(dir, "esc.dat", xml->str, xml->len);
    g_string_free(xml, TRUE);

    RhStore *store = open_store_in(dir, NULL);
    RhDatImportOptions options = {0};
    options.platform = "gb";
    RhDatImportStats stats;
    g_assert_cmpint(rh_dat_import(store, dat_path, &options, &stats), ==, RH_OK);
    g_assert_true(stats.escaped);
    g_assert_true(stats.encoding_guessed);
    g_assert_cmpint(stats.entry_count, ==, 1);

    GPtrArray *matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "cc000001", 3, NULL,
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    RhDatMatch *match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->game_name, ==, "Bad%FFName");
    g_ptr_array_unref(matches);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(dir);
}

/* ---------------------------------------------------------------------------
 * Redump disk 与 MAME sample：disk 入库，sample 只计数
 * ------------------------------------------------------------------------- */

static void test_disk_and_sample(void)
{
    static const char *disk_sha1 = "1111111111111111111111111111111111111111";
    static const char *disk_md5 = "22222222222222222222222222222222";
    static const char *dat_text =
        "<?xml version=\"1.0\"?>\n<datafile>\n"
        "<header><name>Disc DAT</name><version>1</version></header>\n"
        "<game name=\"Disc Game\" cloneof=\"Base Game\" romof=\"Base Game\">\n"
        "  <rom name=\"track.bin\" size=\"1024\" crc=\"AABBCCDD\""
        " md5=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\""
        " sha1=\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"/>\n"
        "  <disk name=\"disc.chd\""
        " sha1=\"1111111111111111111111111111111111111111\""
        " md5=\"22222222222222222222222222222222\"/>\n"
        "  <sample name=\"audio.wav\"/>\n"
        "</game>\n</datafile>\n";

    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *dat_path = write_text(dir, "disc.dat", dat_text);
    RhStore *store = open_store_in(dir, NULL);

    RhDatImportOptions options = {0};
    options.platform = "psx";
    RhDatImportStats stats;
    g_assert_cmpint(rh_dat_import(store, dat_path, &options, &stats), ==, RH_OK);
    g_assert_cmpint(stats.games, ==, 1);
    g_assert_cmpint(stats.entry_count, ==, 2); /* rom + disk，不含 sample */
    g_assert_cmpint(stats.disk_entries, ==, 1);
    g_assert_cmpint(stats.sample_skipped, ==, 1);
    g_assert_cmpint(stats.skipped, ==, 0);
    g_assert_cmpint(stats.bad_fields, ==, 0);

    /* disk 没有 size/crc32，sha1/md5 与 parent 关系必须保留。 */
    GPtrArray *matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "sha1", disk_sha1, 0, "psx",
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    RhDatMatch *match = g_ptr_array_index(matches, 0);
    RhDatEntry *entry = match->entry;
    g_assert_cmpstr(entry->entry_type, ==, "disk");
    g_assert_cmpstr(entry->game_name, ==, "Disc Game");
    g_assert_cmpstr(entry->rom_name, ==, "disc.chd");
    g_assert_false(entry->has_size);
    g_assert_null(entry->crc32);
    g_assert_null(entry->status);
    g_assert_null(entry->merge_name);
    g_assert_cmpstr(entry->md5, ==, disk_md5);
    g_assert_cmpstr(entry->sha1, ==, disk_sha1);
    g_assert_cmpstr(entry->cloneof, ==, "Base Game");
    g_assert_cmpstr(entry->romof, ==, "Base Game");
    g_ptr_array_unref(matches);

    /* 同时有三种哈希时 sha1 优先：命中 disk，而不是 crc32 对应的 rom。
     * size 非零也必须命中：disk 没有 size，不能因 size 过滤被排除。 */
    RhHashResult hash;
    memset(&hash, 0, sizeof hash);
    hash.size = 4096;
    hash.has_sha1 = TRUE;
    g_strlcpy(hash.sha1, disk_sha1, sizeof hash.sha1);
    hash.has_md5 = TRUE;
    g_strlcpy(hash.md5, disk_md5, sizeof hash.md5);
    hash.has_crc32 = TRUE;
    hash.crc32 = 0xAABBCCDDu;

    matches = NULL;
    g_assert_cmpint(rh_dat_match(store, &hash, "psx", &matches, NULL),
                    ==, RH_DAT_MATCH_UNIQUE);
    match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->entry_type, ==, "disk");
    g_assert_cmpstr(match->entry->rom_name, ==, "disc.chd");
    g_ptr_array_unref(matches);

    /* rom 条目类型正确，size 参数会精确缩小候选。 */
    matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "aabbccdd", 1024,
                                           "psx", &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->entry_type, ==, "rom");
    g_assert_true(match->entry->has_size);
    g_assert_cmpint(match->entry->size, ==, 1024);
    g_ptr_array_unref(matches);

    matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "aabbccdd", 2048,
                                           "psx", &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 0);
    g_ptr_array_unref(matches);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(dir);
}

/* ---------------------------------------------------------------------------
 * 重导：内容未变跳过、force 强制重建、内容变化后重建
 * ------------------------------------------------------------------------- */

static void test_reimport_unchanged(void)
{
    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *dat_path = write_text(dir, "again.dat", SAMPLE_DAT);
    RhStore *store = open_store_in(dir, NULL);

    RhDatImportOptions options = {0};
    options.platform = "gb";
    RhDatImportStats stats;
    g_assert_cmpint(rh_dat_import(store, dat_path, &options, &stats), ==, RH_OK);
    g_assert_cmpint(stats.entry_count, ==, 4);
    g_assert_false(stats.unchanged);

    gchar *sha1_before = NULL;
    gint64 dat_id_before = 0;
    gint64 entries_before = 0;
    g_assert_cmpint(store_dat_lookup(store, "gb", dat_path, &sha1_before,
                                     &dat_id_before, &entries_before), ==, RH_OK);
    g_assert_nonnull(sha1_before);
    g_assert_cmpint(entries_before, ==, 4);

    /* 同内容重写文件（mtime 变了）：按内容哈希判定为未变，不解析。 */
    g_assert_nonnull(write_text(dir, "again.dat", SAMPLE_DAT));
    ProgressState progress = {0};
    options.progress = progress_cb;
    options.user_data = &progress;
    g_assert_cmpint(rh_dat_import(store, dat_path, &options, &stats), ==, RH_OK);
    g_assert_true(stats.unchanged);
    g_assert_cmpint(stats.entry_count, ==, 4);
    g_assert_cmpint(progress.calls, ==, 0); /* 连解析都没开始 */

    gchar *sha1_after = NULL;
    gint64 dat_id_after = 0;
    gint64 entries_after = 0;
    g_assert_cmpint(store_dat_lookup(store, "gb", dat_path, &sha1_after,
                                     &dat_id_after, &entries_after), ==, RH_OK);
    g_assert_cmpstr(sha1_after, ==, sha1_before);
    g_assert_cmpint(dat_id_after, ==, dat_id_before);
    g_assert_cmpint(entries_after, ==, 4);
    g_free(sha1_after);

    /* force：内容没变也重建索引，dat_id 不变。 */
    options.force = TRUE;
    progress.calls = 0;
    g_assert_cmpint(rh_dat_import(store, dat_path, &options, &stats), ==, RH_OK);
    g_assert_false(stats.unchanged);
    g_assert_cmpint(stats.entry_count, ==, 4);
    g_assert_cmpint(progress.calls, >, 0);
    options.force = FALSE;

    /* 内容变化：新 game 入库，来源 id 不变、哈希更新。 */
    static const char *changed =
        "<?xml version=\"1.0\"?>\n<datafile>\n"
        "<header><name>Test DAT</name><version>20260102</version></header>\n"
        "<game name=\"Gamma\"><rom name=\"gamma.bin\" size=\"8\""
        " crc=\"44444444\"/></game>\n"
        "</datafile>\n";
    g_free(write_text(dir, "again.dat", changed));
    g_assert_cmpint(rh_dat_import(store, dat_path, &options, &stats), ==, RH_OK);
    g_assert_false(stats.unchanged);
    g_assert_cmpint(stats.entry_count, ==, 1);

    gchar *sha1_changed = NULL;
    gint64 dat_id_changed = 0;
    gint64 entries_changed = 0;
    g_assert_cmpint(store_dat_lookup(store, "gb", dat_path, &sha1_changed,
                                     &dat_id_changed, &entries_changed), ==, RH_OK);
    g_assert_cmpint(dat_id_changed, ==, dat_id_before);
    g_assert_cmpint(entries_changed, ==, 1);
    g_assert_cmpstr(sha1_changed, !=, sha1_before);
    g_free(sha1_changed);
    g_free(sha1_before);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(dir);
}

/* ---------------------------------------------------------------------------
 * clrmamepro 文本 DAT（libretro-database 下载源使用的格式）
 * ------------------------------------------------------------------------- */

static const char *CLRMAMEPRO_DAT =
    "clrmamepro (\n"
    "    name \"Libretro Test\"\n"
    "    version \"2026-11-24\"\n"
    ")\n"
    "game (\n"
    "    name \"Sample Game (USA)\"\n"
    "    rom ( name \"Sample Game (USA).nes\" size 16384 crc aabbccdd )\n"
    "    rom ( name \"Extra \\\"Quoted\\\".bin\" size 128 crc deadbeef )\n"
    ")\n"
    "game (\n"
    "    name \"Clone Game\"\n"
    "    cloneof \"Sample Game (USA)\"\n"
    "    rom ( name \"Clone.nes\" size 16384 crc aabbccdd )\n"
    ")\n";

static void test_clrmamepro_text(void)
{
    gchar *dir = g_dir_make_tmp("romham-dat-XXXXXX", NULL);
    g_assert_nonnull(dir);
    gchar *dat_path = write_text(dir, "text.dat", CLRMAMEPRO_DAT);
    RhStore *store = open_store_in(dir, NULL);

    RhDatImportOptions options = {0};
    options.platform = "gb";

    RhDatImportStats stats;
    RhErr err = rh_dat_import(store, dat_path, &options, &stats);
    g_assert_cmpint(err, ==, RH_OK);
    g_assert_cmpint(stats.games, ==, 2);
    g_assert_cmpint(stats.entry_count, ==, 3);
    g_assert_cmpint(stats.skipped, ==, 0);
    g_assert_cmpint(stats.bad_fields, ==, 0);
    g_assert_false(stats.xml_error);
    g_assert_cmpstr(stats.encoding, ==, "utf-8");

    GPtrArray *matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "deadbeef", 128, "gb",
                                           &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 1);
    RhDatMatch *match = g_ptr_array_index(matches, 0);
    g_assert_cmpstr(match->entry->game_name, ==, "Sample Game (USA)");
    g_assert_cmpstr(match->entry->rom_name, ==, "Extra \"Quoted\".bin");
    g_assert_cmpstr(match->dat_name, ==, "Libretro Test");
    g_assert_cmpstr(match->dat_version, ==, "2026-11-24");
    g_ptr_array_unref(matches);

    matches = NULL;
    g_assert_cmpint(store_dat_find_by_hash(store, "crc32", "aabbccdd", 16384,
                                           "gb", &matches), ==, RH_OK);
    g_assert_cmpuint(matches->len, ==, 2);
    gboolean saw_clone = FALSE;
    for (guint i = 0; i < matches->len; i++) {
        RhDatMatch *m = g_ptr_array_index(matches, i);
        if (g_strcmp0(m->entry->game_name, "Clone Game") == 0) {
            saw_clone = TRUE;
            g_assert_cmpstr(m->entry->cloneof, ==, "Sample Game (USA)");
        }
    }
    g_assert_true(saw_clone);
    g_ptr_array_unref(matches);

    store_close(store);
    cleanup_dir(dir);
    g_free(dat_path);
    g_free(dir);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);
    g_test_add_func("/dat/logiqx-basic", test_logiqx_basic);
    g_test_add_func("/dat/clrmamepro-text", test_clrmamepro_text);
    g_test_add_func("/dat/large-stream", test_large_stream);
    g_test_add_func("/dat/sha1-fallback", test_sha1_fallback);
    g_test_add_func("/dat/missing-fields-malformed", test_missing_fields_and_malformed);
    g_test_add_func("/dat/cancel", test_cancel);
    g_test_add_func("/dat/gbk-encoding", test_gbk_dat);
    g_test_add_func("/dat/invalid-bytes-escaped", test_invalid_bytes_escaped);
    g_test_add_func("/dat/disk-and-sample", test_disk_and_sample);
    g_test_add_func("/dat/reimport-unchanged", test_reimport_unchanged);
    return g_test_run();
}
