#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "rh-hash.h"
#include "rh-test-main.h"

typedef struct {
    guint    calls;
    guint64  last_processed;
    guint64  total;
    gboolean cancel_after_two;
} ProgressState;

static gboolean progress_cb(const char *path, guint64 processed,
                            guint64 total, gpointer user_data)
{
    g_assert_nonnull(path);
    ProgressState *state = user_data;
    state->calls++;
    state->last_processed = processed;
    state->total = total;
    return !(state->cancel_after_two && state->calls >= 2);
}

static gchar *make_dir(void)
{
    gchar *dir = g_dir_make_tmp("rh-hash-XXXXXX", NULL);
    g_assert_nonnull(dir);
    return dir;
}

static gchar *write_file(const char *dir, const char *name,
                         const void *data, gsize len)
{
    gchar *path = g_build_filename(dir, name, NULL);
    g_assert_true(g_file_set_contents(path, data, (gssize)len, NULL));
    return path;
}

static void remove_file_and_dir(const char *path, const char *dir)
{
    g_remove(path);
    g_rmdir(dir);
}

static void assert_vectors(const RhHashResult *result, guint32 crc,
                           const gchar *md5, const gchar *sha1)
{
    g_assert_cmpint(result->error, ==, RH_OK);
    g_assert_true(result->has_crc32);
    g_assert_cmpuint(result->crc32, ==, crc);
    g_assert_true(result->has_md5);
    g_assert_cmpstr(result->md5, ==, md5);
    g_assert_true(result->has_sha1);
    g_assert_cmpstr(result->sha1, ==, sha1);
}

/* 独立实现的逐位 CRC32，用来交叉验证 zlib 快表结果。 */
static guint32 ref_crc32(const guchar *data, gsize len)
{
    guint32 crc = 0xFFFFFFFFu;
    for (gsize i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

static void test_empty_file(void)
{
    gchar *dir = make_dir();
    gchar *path = write_file(dir, "empty.rom", "", 0);

    RhHashResult result;
    g_assert_cmpint(rh_hash_file(path, NULL, &result), ==, RH_OK);
    assert_vectors(&result, 0x00000000u,
                   "d41d8cd98f00b204e9800998ecf8427e",
                   "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    g_assert_cmpuint(result.size, ==, 0);

    /* 空文件不应触发进度回调 */
    ProgressState state = { 0 };
    RhHashOptions options = {.flags = RH_HASH_ALL, .chunk_size = 0,
                             .progress = progress_cb, .user_data = &state};
    g_assert_cmpint(rh_hash_file(path, &options, &result), ==, RH_OK);
    g_assert_cmpuint(state.calls, ==, 0);

    remove_file_and_dir(path, dir);
    g_free(path);
    g_free(dir);
}

static void test_known_vectors(void)
{
    gchar *dir = make_dir();

    struct {
        const gchar *name;
        const gchar *data;
        guint32 crc;
        const gchar *md5;
        const gchar *sha1;
    } cases[] = {
        { "abc.rom", "abc", 0x352441C2u,
          "900150983cd24fb0d6963f7d28e17f72",
          "a9993e364706816aba3e25717850c26c9cd0d89d" },
        { "fox.rom", "The quick brown fox jumps over the lazy dog", 0x414FA339u,
          "9e107d9d372bb6826bd81d3542a419d6",
          "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12" },
        { "digits.rom", "123456789", 0xCBF43926u,
          "25f9e794323b453885f5181f1b624d0b",
          "f7c3bc1d808e04732adf679965ccc34ca7ae3441" },
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        gchar *path = write_file(dir, cases[i].name,
                                 cases[i].data, strlen(cases[i].data));
        RhHashResult result;
        g_assert_cmpint(rh_hash_file(path, NULL, &result), ==, RH_OK);
        assert_vectors(&result, cases[i].crc, cases[i].md5, cases[i].sha1);
        g_assert_cmpuint(result.size, ==, strlen(cases[i].data));
        g_remove(path);
        g_free(path);
    }

    g_rmdir(dir);
    g_free(dir);
}

/* 超过单次缓冲区的文件：默认块大小与极小块的流式结果必须一致。 */
static void test_large_streaming(void)
{
    const gsize len = 1024 * 1024 + 12345;
    guchar *data = g_malloc(len);
    for (gsize i = 0; i < len; i++)
        data[i] = (guchar)((i * 31 + 7) & 0xFF);

    gchar *dir = make_dir();
    gchar *path = write_file(dir, "large.rom", data, len);

    gchar *expected_md5 = g_compute_checksum_for_data(G_CHECKSUM_MD5, data, len);
    gchar *expected_sha1 = g_compute_checksum_for_data(G_CHECKSUM_SHA1, data, len);
    guint32 expected_crc = ref_crc32(data, len);

    RhHashResult whole;
    g_assert_cmpint(rh_hash_file(path, NULL, &whole), ==, RH_OK);
    assert_vectors(&whole, expected_crc, expected_md5, expected_sha1);
    g_assert_cmpuint(whole.size, ==, len);

    /* 7 字节一块，强制跨很多块 */
    ProgressState state = { 0 };
    RhHashOptions options = {.flags = RH_HASH_ALL, .chunk_size = 7,
                             .progress = progress_cb, .user_data = &state};
    RhHashResult chunked;
    g_assert_cmpint(rh_hash_file(path, &options, &chunked), ==, RH_OK);
    assert_vectors(&chunked, expected_crc, expected_md5, expected_sha1);
    g_assert_cmpuint(state.calls, ==, (len + 6) / 7);
    g_assert_cmpuint(state.last_processed, ==, len);
    g_assert_cmpuint(state.total, ==, len);

    g_free(expected_sha1);
    g_free(expected_md5);
    g_free(data);
    g_remove(path);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

/* 中文 + emoji 路径必须走 GLib 的宽字符通道。 */
static void test_chinese_path(void)
{
    gchar *dir = make_dir();
    gchar *path = write_file(dir, "中文 游戏 🎮.gb", "abc", 3);

    RhHashResult result;
    g_assert_cmpint(rh_hash_file(path, NULL, &result), ==, RH_OK);
    assert_vectors(&result, 0x352441C2u,
                   "900150983cd24fb0d6963f7d28e17f72",
                   "a9993e364706816aba3e25717850c26c9cd0d89d");

    remove_file_and_dir(path, dir);
    g_free(path);
    g_free(dir);
}

static void test_batch_reuses_one_pass(void)
{
    gchar *dir = make_dir();
    gchar *empty = write_file(dir, "0.rom", "", 0);
    gchar *abc = write_file(dir, "1.rom", "abc", 3);
    gchar *fox = write_file(dir, "2.rom",
                            "The quick brown fox jumps over the lazy dog", 43);
    const gchar *paths[] = { empty, abc, fox };

    RhHashResult results[3];
    g_assert_cmpint(rh_hash_files(paths, 3, NULL, results), ==, RH_OK);

    assert_vectors(&results[0], 0x00000000u,
                   "d41d8cd98f00b204e9800998ecf8427e",
                   "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    assert_vectors(&results[1], 0x352441C2u,
                   "900150983cd24fb0d6963f7d28e17f72",
                   "a9993e364706816aba3e25717850c26c9cd0d89d");
    assert_vectors(&results[2], 0x414FA339u,
                   "9e107d9d372bb6826bd81d3542a419d6",
                   "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    /* 只请求 SHA1：其余字段必须明确标记为未计算，不能给假值 */
    RhHashOptions only_sha1 = {.flags = RH_HASH_SHA1};
    RhHashResult subset[3];
    g_assert_cmpint(rh_hash_files(paths, 3, &only_sha1, subset), ==, RH_OK);
    for (guint i = 0; i < 3; i++) {
        g_assert_false(subset[i].has_crc32);
        g_assert_false(subset[i].has_md5);
        g_assert_true(subset[i].has_sha1);
        g_assert_cmpstr(subset[i].md5, ==, "");
    }
    g_assert_cmpstr(subset[2].sha1, ==,
                    "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    g_remove(empty);
    g_remove(abc);
    g_remove(fox);
    g_free(empty);
    g_free(abc);
    g_free(fox);
    g_rmdir(dir);
    g_free(dir);
}

static void test_cancel(void)
{
    const gsize len = 512 * 1024;
    guchar *data = g_malloc(len);
    memset(data, 0x5A, len);

    gchar *dir = make_dir();
    gchar *path = write_file(dir, "cancel.rom", data, len);

    ProgressState state = { 0, 0, 0, TRUE };
    RhHashOptions options = {.flags = RH_HASH_ALL, .chunk_size = 4096,
                             .progress = progress_cb, .user_data = &state};
    RhHashResult result;
    g_assert_cmpint(rh_hash_file(path, &options, &result), ==, RH_ERR_CANCELLED);
    g_assert_cmpint(result.error, ==, RH_ERR_CANCELLED);
    g_assert_cmpuint(result.size, ==, 0);
    g_assert_false(result.has_crc32);
    g_assert_false(result.has_md5);
    g_assert_false(result.has_sha1);
    g_assert_cmpstr(result.md5, ==, "");
    g_assert_cmpstr(result.sha1, ==, "");
    g_assert_cmpuint(state.calls, ==, 2);
    g_assert_cmpuint(state.last_processed, <, len);

    /* 批量中取消要立刻返回，且不继续读后面的文件 */
    const gchar *paths[] = { path, path };
    RhHashResult batch[2];
    g_assert_cmpint(rh_hash_files(paths, 2, &options, batch),
                    ==, RH_ERR_CANCELLED);
    g_assert_cmpint(batch[0].error, ==, RH_ERR_CANCELLED);

    g_free(data);
    remove_file_and_dir(path, dir);
    g_free(path);
    g_free(dir);
}

static void test_errors(void)
{
    gchar *dir = make_dir();
    gchar *path = write_file(dir, "ok.rom", "abc", 3);
    gchar *missing = g_build_filename(dir, "missing.rom", NULL);

    RhHashResult result;
    g_assert_cmpint(rh_hash_file(missing, NULL, &result), ==, RH_ERR_IO);
    g_assert_cmpint(result.error, ==, RH_ERR_IO);

    g_assert_cmpint(rh_hash_file(dir, NULL, &result), ==, RH_ERR_IO);

    RhHashOptions bad_flags = {.flags = 0};
    g_assert_cmpint(rh_hash_file(path, &bad_flags, &result), ==, RH_ERR_INVALID);

    g_assert_cmpint(rh_hash_file(NULL, NULL, &result), ==, RH_ERR_INVALID);
    g_assert_cmpint(rh_hash_file(path, NULL, NULL), ==, RH_ERR_INVALID);

    const gchar *bad_batch[] = { missing, path };
    RhHashResult batch[2];
    g_assert_cmpint(rh_hash_files(bad_batch, 2, NULL, batch), ==, RH_ERR_IO);
    g_assert_cmpint(batch[0].error, ==, RH_ERR_IO);
    g_assert_cmpint(batch[1].error, ==, RH_OK);
    g_assert_true(batch[1].has_sha1);

    g_assert_cmpint(rh_hash_files(NULL, 1, NULL, batch), ==, RH_ERR_INVALID);
    g_assert_cmpint(rh_hash_files(NULL, 0, NULL, batch), ==, RH_OK);

    g_free(missing);
    remove_file_and_dir(path, dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char **argv)
{
    rh_test_init(&argc, &argv);

    g_test_add_func("/hash/empty-file", test_empty_file);
    g_test_add_func("/hash/known-vectors", test_known_vectors);
    g_test_add_func("/hash/large-streaming", test_large_streaming);
    g_test_add_func("/hash/chinese-path", test_chinese_path);
    g_test_add_func("/hash/batch", test_batch_reuses_one_pass);
    g_test_add_func("/hash/cancel", test_cancel);
    g_test_add_func("/hash/errors", test_errors);

    return g_test_run();
}
