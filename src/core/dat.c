#include "dat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

#define RH_DAT_DEFAULT_CHUNK (64u * 1024u)
#define RH_DAT_DEFAULT_BATCH 2000u
#define RH_DAT_SNIFF_BYTES   (64u * 1024u)

/* ===========================================================================
 * 解码：字节流 → UTF-8（供 GMarkup）
 * ========================================================================= */

typedef enum { DEC_UTF8, DEC_CHARSET } DecMode;

typedef enum {
    DEC_OK = 0,
    DEC_RESTART,   /* UTF-8 严格解码失败，需整份按 GB18030 重来 */
    DEC_ERROR      /* 转换器不可用等硬错误 */
} DecStatus;

typedef struct {
    DecMode      mode;
    gboolean     restartable;  /* 无声明时的 UTF-8：失败可重来 */
    gchar       *charset;
    GIConv       cd;
    gboolean     escaped;
    GByteArray  *carry;        /* 块边界截断的半个字符 */
} Decoder;

static void decoder_reset(Decoder *dec)
{
    if (dec->cd != (GIConv)-1)
        g_iconv_close(dec->cd);
    g_free(dec->charset);
    if (dec->carry != NULL)
        g_byte_array_free(dec->carry, TRUE);
    memset(dec, 0, sizeof *dec);
    dec->cd = (GIConv)-1;
    dec->carry = g_byte_array_new();
}

static void decoder_clear(Decoder *dec)
{
    if (dec->cd != (GIConv)-1)
        g_iconv_close(dec->cd);
    g_free(dec->charset);
    g_byte_array_free(dec->carry, TRUE);
    memset(dec, 0, sizeof *dec);
    dec->cd = (GIConv)-1;
}

static gboolean decoder_use_utf8(Decoder *dec, gboolean restartable)
{
    decoder_reset(dec);
    dec->mode = DEC_UTF8;
    dec->restartable = restartable;
    return TRUE;
}

static gboolean decoder_use_charset(Decoder *dec, const gchar *charset)
{
    decoder_reset(dec);
    dec->mode = DEC_CHARSET;
    dec->charset = g_strdup(charset);
    dec->cd = g_iconv_open("UTF-8", charset);
    return dec->cd != (GIConv)-1;
}

static void append_escaped_byte(GString *out, guchar byte)
{
    g_string_append_printf(out, "%%%02X", byte);
}

/*
 * UTF-8 严格解码。块边界可能截断多字节字符：末尾 <=3 字节先留在 carry，
 * 等下一块拼上再判。restartable 时遇到真正的非法字节返回 DEC_RESTART，
 * 否则逐字节转义（不静默丢弃）。
 */
static DecStatus decode_utf8(Decoder *dec, const gchar *data, gsize len,
                             GString *out, gboolean eof)
{
    GByteArray *buf = g_byte_array_new();
    if (dec->carry->len > 0)
        g_byte_array_append(buf, dec->carry->data, dec->carry->len);
    if (len > 0)
        g_byte_array_append(buf, (const guint8 *)data, len);
    g_byte_array_set_size(dec->carry, 0);

    gsize pos = 0;
    while (pos < buf->len) {
        const gchar *start = (const gchar *)buf->data + pos;
        gsize remaining = buf->len - pos;
        const gchar *bad = NULL;

        if (g_utf8_validate(start, (gssize)remaining, &bad)) {
            g_string_append_len(out, start, (gssize)remaining);
            break;
        }

        gsize valid = (gsize)(bad - start);
        if (valid > 0)
            g_string_append_len(out, start, (gssize)valid);
        pos += valid;

        gsize tail = buf->len - pos;
        if (!eof && tail <= 3) {
            g_byte_array_append(dec->carry, buf->data + pos, tail);
            break;
        }
        if (dec->restartable) {
            g_byte_array_free(buf, TRUE);
            return DEC_RESTART;
        }

        append_escaped_byte(out, buf->data[pos]);
        dec->escaped = TRUE;
        pos++;
    }

    g_byte_array_free(buf, TRUE);
    return DEC_OK;
}

/* 按声明字符集（或 GB18030）转码；非法字节转义后继续。 */
static DecStatus decode_charset(Decoder *dec, const gchar *data, gsize len,
                                GString *out, gboolean eof)
{
    GByteArray *buf = g_byte_array_new();
    if (dec->carry->len > 0)
        g_byte_array_append(buf, dec->carry->data, dec->carry->len);
    if (len > 0)
        g_byte_array_append(buf, (const guint8 *)data, len);
    g_byte_array_set_size(dec->carry, 0);

    gchar *in = (gchar *)buf->data;
    gsize in_left = buf->len;
    gchar outbuf[8192];

    while (in_left > 0) {
        gchar *outp = outbuf;
        gsize out_left = sizeof outbuf - 1;
        gsize rc = g_iconv(dec->cd, &in, &in_left, &outp, &out_left);
        gsize produced = (gsize)(sizeof outbuf - 1) - out_left;
        if (produced > 0)
            g_string_append_len(out, outbuf, (gssize)produced);

        if (rc != (gsize)-1)
            continue;
        if (errno == E2BIG)
            continue;
        if (errno == EINVAL) {
            /* 不完整序列最多 3 字节（GB18030 最长 4 字节）。Windows 的
             * winiconv 对非法字节也报 EINVAL，此时剩余数据远多于 3 字节，
             * 必须按非法字节处理，否则整段剩余 XML 都会被转义打乱。 */
            if (!eof && in_left <= 3) {
                g_byte_array_append(dec->carry, (const guint8 *)in, in_left);
                in_left = 0;
                break;
            }
            append_escaped_byte(out, (guchar)*in);
            dec->escaped = TRUE;
            in++;
            in_left--;
            g_iconv(dec->cd, NULL, NULL, NULL, NULL);
            continue;
        }
        if (errno == EILSEQ) {
            append_escaped_byte(out, (guchar)*in);
            dec->escaped = TRUE;
            in++;
            in_left--;
            /* 清掉可能残留的移位状态，避免一个坏字节污染后续 */
            g_iconv(dec->cd, NULL, NULL, NULL, NULL);
            continue;
        }

        g_byte_array_free(buf, TRUE);
        return DEC_ERROR;
    }

    g_byte_array_free(buf, TRUE);
    return DEC_OK;
}

static DecStatus decoder_feed(Decoder *dec, const gchar *data, gsize len,
                              GString *out, gboolean eof)
{
    if (dec->mode == DEC_UTF8)
        return decode_utf8(dec, data, len, out, eof);
    return decode_charset(dec, data, len, out, eof);
}

/* ===========================================================================
 * 编码嗅探：只看文件开头，不改动解析位置
 * ========================================================================= */

static gchar *sniff_declared_charset(const guchar *buf, gsize len)
{
    gsize head_len = len < 1024 ? len : 1024;
    gchar *head = g_strndup((const gchar *)buf, head_len);
    if (head == NULL)
        return NULL;

    gchar *result = NULL;
    const gchar *p = strstr(head, "<?xml");
    if (p != NULL) {
        const gchar *enc = strstr(p, "encoding");
        if (enc != NULL) {
            const gchar *eq = strchr(enc, '=');
            if (eq != NULL) {
                gchar quote = eq[1];
                if (quote == '"' || quote == '\'') {
                    const gchar *start = eq + 2;
                    const gchar *end = strchr(start, quote);
                    if (end != NULL && end > start)
                        result = g_ascii_strdown(start, (gssize)(end - start));
                }
            }
        }
    }

    g_free(head);
    return result;
}

static gboolean charset_is_utf8(const gchar *charset)
{
    return g_ascii_strcasecmp(charset, "utf-8") == 0 ||
           g_ascii_strcasecmp(charset, "utf8") == 0 ||
           g_ascii_strcasecmp(charset, "us-ascii") == 0 ||
           g_ascii_strcasecmp(charset, "ascii") == 0;
}

/*
 * 读取开头一段判断编码，然后把位置复位。
 * 返回 FALSE 表示文件读失败（位置已尽量复位）。
 */
static gboolean sniff_encoding(FILE *fp, gboolean *out_declared,
                               gchar **out_charset, gboolean *out_guess_gb,
                               RhDatImportStats *stats)
{
    guchar *buf = g_malloc(RH_DAT_SNIFF_BYTES);
    size_t n = fread(buf, 1, RH_DAT_SNIFF_BYTES, fp);
    if (ferror(fp)) {
        g_free(buf);
        return FALSE;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        g_free(buf);
        return FALSE;
    }

    gchar *declared = sniff_declared_charset(buf, (gsize)n);
    *out_declared = declared != NULL;
    *out_guess_gb = FALSE;

    if (declared != NULL) {
        *out_charset = declared; /* 所有权转移 */
        g_strlcpy(stats->encoding, declared, sizeof stats->encoding);
        stats->encoding_declared = TRUE;
        stats->encoding_guessed = FALSE;
        g_free(buf);
        return TRUE;
    }

    /* 无声明：整段样本是合法 UTF-8（末尾半个字符不算）就按 UTF-8，
     * 否则按 GB18030。截断字符最多 3 字节。 */
    const gchar *bad = NULL;
    gboolean utf8 = n == 0 ||
        g_utf8_validate((const gchar *)buf, (gssize)n, &bad);
    if (!utf8 && n - (gsize)(bad - (const gchar *)buf) <= 3)
        utf8 = TRUE;

    if (utf8) {
        *out_charset = NULL;
        g_strlcpy(stats->encoding, "utf-8", sizeof stats->encoding);
    } else {
        *out_charset = g_strdup("GB18030");
        g_strlcpy(stats->encoding, "gb18030", sizeof stats->encoding);
        stats->encoding_guessed = TRUE;
        *out_guess_gb = TRUE;
    }

    g_free(buf);
    return TRUE;
}

/* ===========================================================================
 * Logiqx 解析（GMarkup，流式）
 * ========================================================================= */

typedef enum {
    PARSE_DONE = 0,
    PARSE_RESTART,
    PARSE_CANCEL,
    PARSE_XML_ERROR,
    PARSE_STORE_ERROR,
    PARSE_IO_ERROR
} ParseOutcome;

typedef struct {
    RhStore     *store;
    gint64       dat_id;
    guint        batch_size;
    GPtrArray   *batch;          /* RhDatEntry*，攒批落库 */

    gint64       games;
    gint64       parsed_entries;
    gint64       disk_entries;   /* Clrmamepro disk 元素计数 */
    gint64       skipped;
    gint64       sample_skipped; /* sample 元素跳过计数 */
    gint64       bad_fields;

    GString     *header_name;
    GString     *header_version;
    gboolean     in_header;
    int          text_target;    /* 0 无 / 1 header.name / 2 header.version */

    gboolean     in_game;
    gboolean     game_malformed;
    gchar       *game_name;
    gchar       *cloneof;
    gchar       *romof;

    RhErr        store_error;
} ParseState;

static RhErr flush_batch(ParseState *state)
{
    if (state->batch->len == 0)
        return RH_OK;

    RhErr err = store_dat_entries_add(state->store, state->dat_id, state->batch);
    g_ptr_array_set_size(state->batch, 0);
    return err;
}

static const gchar *attr_get(const gchar **names, const gchar **values,
                             const gchar *key)
{
    for (int i = 0; names != NULL && names[i] != NULL; i++) {
        if (g_strcmp0(names[i], key) == 0)
            return values[i];
    }
    return NULL;
}

static gchar *trimmed_dup(const gchar *value)
{
    gchar *copy = g_strdup(value != NULL ? value : "");
    g_strstrip(copy);
    return copy;
}

/* 校验并归一化十六进制摘要；长度不符或含非十六进制字符返回 NULL。 */
static gchar *normalize_hash(const gchar *value, gsize expected)
{
    if (value == NULL)
        return NULL;

    gchar *trim = trimmed_dup(value);
    if (trim[0] == '\0') {
        g_free(trim);
        return NULL;
    }
    if (strlen(trim) != expected) {
        g_free(trim);
        return NULL;
    }
    for (const gchar *p = trim; *p != '\0'; p++) {
        if (!g_ascii_isxdigit(*p)) {
            g_free(trim);
            return NULL;
        }
    }
    gchar *lower = g_ascii_strdown(trim, -1);
    g_free(trim);
    return lower;
}

static gboolean parse_size_value(const gchar *value, gint64 *out)
{
    if (value == NULL)
        return FALSE;
    gchar *trim = trimmed_dup(value);
    if (trim[0] == '\0') {
        g_free(trim);
        return FALSE;
    }

    errno = 0;
    gchar *end = NULL;
    gint64 parsed = g_ascii_strtoll(trim, &end, 10);
    gboolean ok = errno == 0 && end != NULL && *end == '\0' && parsed >= 0;
    g_free(trim);
    if (ok)
        *out = parsed;
    return ok;
}

static void reset_game(ParseState *state)
{
    g_free(state->game_name);
    g_free(state->cloneof);
    g_free(state->romof);
    state->game_name = NULL;
    state->cloneof = NULL;
    state->romof = NULL;
    state->in_game = FALSE;
    state->game_malformed = FALSE;
}

static void handle_rom(ParseState *state, const gchar **names,
                       const gchar **values)
{
    if (!state->in_game || state->game_malformed) {
        state->skipped++;
        return;
    }

    const gchar *rom_name = attr_get(names, values, "name");
    if (rom_name == NULL || rom_name[0] == '\0') {
        state->skipped++;
        return;
    }

    RhDatEntry *entry = rh_dat_entry_new();
    entry->entry_type = g_strdup("rom");
    entry->game_name = g_strdup(state->game_name);
    entry->rom_name = trimmed_dup(rom_name);
    entry->status = trimmed_dup(attr_get(names, values, "status"));
    if (entry->status[0] == '\0') {
        g_free(entry->status);
        entry->status = NULL;
    }
    entry->cloneof = g_strdup(state->cloneof);
    entry->romof = g_strdup(state->romof);
    entry->merge_name = trimmed_dup(attr_get(names, values, "merge"));
    if (entry->merge_name[0] == '\0') {
        g_free(entry->merge_name);
        entry->merge_name = NULL;
    }

    const gchar *size = attr_get(names, values, "size");
    if (size != NULL && size[0] != '\0') {
        if (parse_size_value(size, &entry->size))
            entry->has_size = TRUE;
        else
            state->bad_fields++;
    }

    const gchar *crc = attr_get(names, values, "crc");
    const gchar *md5 = attr_get(names, values, "md5");
    const gchar *sha1 = attr_get(names, values, "sha1");

    entry->crc32 = normalize_hash(crc, 8);
    entry->md5 = normalize_hash(md5, 32);
    entry->sha1 = normalize_hash(sha1, 40);
    if (crc != NULL && crc[0] != '\0' && entry->crc32 == NULL)
        state->bad_fields++;
    if (md5 != NULL && md5[0] != '\0' && entry->md5 == NULL)
        state->bad_fields++;
    if (sha1 != NULL && sha1[0] != '\0' && entry->sha1 == NULL)
        state->bad_fields++;

    g_ptr_array_add(state->batch, entry);
    state->parsed_entries++;

    if (state->batch->len >= state->batch_size) {
        RhErr err = flush_batch(state);
        if (err != RH_OK)
            state->store_error = err;
    }
}

/*
 * <disk> 元素：Redump 风格盘片镜像。与 <rom> 的区别——
 *   - 只有 name / sha1 / md5；没有 crc32、size、status、cloneof、merge
 *   - 匹配时不能走 size 优先路径，但 sha1/md5 哈希匹配照常
 */
static void handle_disk(ParseState *state, const gchar **names,
                        const gchar **values)
{
    if (!state->in_game || state->game_malformed) {
        state->skipped++;
        return;
    }

    const gchar *disk_name = attr_get(names, values, "name");
    if (disk_name == NULL || disk_name[0] == '\0') {
        state->skipped++;
        return;
    }

    RhDatEntry *entry = rh_dat_entry_new();
    entry->entry_type = g_strdup("disk");
    entry->game_name = g_strdup(state->game_name);
    entry->rom_name = trimmed_dup(disk_name);
    /* disk 无 status / size / crc32 / merge；保持默认 NULL/0 */
    entry->cloneof = g_strdup(state->cloneof);
    entry->romof = g_strdup(state->romof);

    const gchar *md5 = attr_get(names, values, "md5");
    const gchar *sha1 = attr_get(names, values, "sha1");
    entry->md5 = normalize_hash(md5, 32);
    entry->sha1 = normalize_hash(sha1, 40);
    if (md5 != NULL && md5[0] != '\0' && entry->md5 == NULL)
        state->bad_fields++;
    if (sha1 != NULL && sha1[0] != '\0' && entry->sha1 == NULL)
        state->bad_fields++;

    g_ptr_array_add(state->batch, entry);
    state->parsed_entries++;
    state->disk_entries++;

    if (state->batch->len >= state->batch_size) {
        RhErr err = flush_batch(state);
        if (err != RH_OK)
            state->store_error = err;
    }
}

static void markup_start(GMarkupParseContext *context, const gchar *element,
                         const gchar **names, const gchar **values,
                         gpointer user_data, GError **error)
{
    (void)context;
    (void)error;
    ParseState *state = user_data;

    if (g_strcmp0(element, "header") == 0) {
        state->in_header = TRUE;
        return;
    }
    if (state->in_header) {
        if (g_strcmp0(element, "name") == 0) {
            g_string_truncate(state->header_name, 0);
            state->text_target = 1;
        } else if (g_strcmp0(element, "version") == 0) {
            g_string_truncate(state->header_version, 0);
            state->text_target = 2;
        }
        return;
    }

    if (g_strcmp0(element, "game") == 0 || g_strcmp0(element, "machine") == 0) {
        reset_game(state);
        state->in_game = TRUE;
        const gchar *name = attr_get(names, values, "name");
        state->game_name = trimmed_dup(name);
        if (name == NULL || name[0] == '\0')
            state->game_malformed = TRUE;
        state->cloneof = trimmed_dup(attr_get(names, values, "cloneof"));
        state->romof = trimmed_dup(attr_get(names, values, "romof"));
        if (state->cloneof[0] == '\0') {
            g_free(state->cloneof);
            state->cloneof = NULL;
        }
        if (state->romof[0] == '\0') {
            g_free(state->romof);
            state->romof = NULL;
        }
        return;
    }

    if (g_strcmp0(element, "rom") == 0)
        handle_rom(state, names, values);
    else if (g_strcmp0(element, "disk") == 0)
        handle_disk(state, names, values);
    else if (g_strcmp0(element, "sample") == 0)
        state->sample_skipped++;
}

static void markup_end(GMarkupParseContext *context, const gchar *element,
                       gpointer user_data, GError **error)
{
    (void)context;
    (void)error;
    ParseState *state = user_data;

    if (g_strcmp0(element, "name") == 0 || g_strcmp0(element, "version") == 0)
        state->text_target = 0;
    else if (g_strcmp0(element, "header") == 0)
        state->in_header = FALSE;
    else if (g_strcmp0(element, "game") == 0 ||
             g_strcmp0(element, "machine") == 0) {
        if (state->in_game)
            state->games++;
        reset_game(state);
    }
}

static void markup_text(GMarkupParseContext *context, const gchar *text,
                        gsize len, gpointer user_data, GError **error)
{
    (void)context;
    (void)error;
    ParseState *state = user_data;

    if (state->text_target == 1)
        g_string_append_len(state->header_name, text, (gssize)len);
    else if (state->text_target == 2)
        g_string_append_len(state->header_version, text, (gssize)len);
}

static const GMarkupParser rh_dat_markup_parser = {
    .start_element = markup_start,
    .end_element = markup_end,
    .text = markup_text
};

static ParseOutcome parse_document(FILE *fp, Decoder *dec, ParseState *state,
                                   const RhDatImportOptions *options,
                                   const gchar *path, guint64 total_bytes)
{
    GMarkupParseContext *ctx = g_markup_parse_context_new(
        &rh_dat_markup_parser, 0, state, NULL);

    guint chunk_size = options->chunk_size > 0
        ? options->chunk_size : RH_DAT_DEFAULT_CHUNK;
    gchar *buffer = g_malloc(chunk_size);
    ParseOutcome outcome = PARSE_DONE;
    guint64 done = 0;

    while (TRUE) {
        size_t n = fread(buffer, 1, chunk_size, fp);
        if (n == 0)
            break;
        done += n;

        GString *utf8 = g_string_sized_new(n + n / 2 + 16);
        DecStatus ds = decoder_feed(dec, buffer, (gsize)n, utf8, FALSE);
        if (ds == DEC_RESTART) {
            g_string_free(utf8, TRUE);
            outcome = PARSE_RESTART;
            goto out;
        }
        if (ds == DEC_ERROR) {
            g_string_free(utf8, TRUE);
            outcome = PARSE_IO_ERROR;
            goto out;
        }

        if (utf8->len > 0) {
            GError *error = NULL;
            gboolean ok = g_markup_parse_context_parse(
                ctx, utf8->str, (gssize)utf8->len, &error);
            g_string_free(utf8, TRUE);
            if (!ok) {
                g_clear_error(&error);
                outcome = PARSE_XML_ERROR;
                goto out;
            }
        } else {
            g_string_free(utf8, TRUE);
        }

        if (state->store_error != RH_OK) {
            outcome = PARSE_STORE_ERROR;
            goto out;
        }

        if (options->progress != NULL &&
            !options->progress(path, state->parsed_entries, done, total_bytes,
                               options->user_data)) {
            outcome = PARSE_CANCEL;
            goto out;
        }
    }

    if (ferror(fp)) {
        outcome = PARSE_IO_ERROR;
        goto out;
    }

    /* 收尾：处理 carry 里残留的半个字符 */
    {
        GString *tail = g_string_new(NULL);
        DecStatus ds = decoder_feed(dec, "", 0, tail, TRUE);
        if (ds == DEC_RESTART) {
            g_string_free(tail, TRUE);
            outcome = PARSE_RESTART;
            goto out;
        }
        if (ds == DEC_ERROR) {
            g_string_free(tail, TRUE);
            outcome = PARSE_IO_ERROR;
            goto out;
        }
        if (tail->len > 0) {
            GError *error = NULL;
            if (!g_markup_parse_context_parse(ctx, tail->str, (gssize)tail->len,
                                              &error)) {
                g_clear_error(&error);
                g_string_free(tail, TRUE);
                outcome = PARSE_XML_ERROR;
                goto out;
            }
        }
        g_string_free(tail, TRUE);
    }

    {
        GError *error = NULL;
        if (!g_markup_parse_context_end_parse(ctx, &error)) {
            g_clear_error(&error);
            outcome = PARSE_XML_ERROR;
        }
    }

out:
    g_markup_parse_context_free(ctx);
    g_free(buffer);
    return outcome;
}

/* ===========================================================================
 * 导入入口
 * ========================================================================= */

void rh_dat_import_stats_clear(RhDatImportStats *stats)
{
    if (stats != NULL)
        memset(stats, 0, sizeof *stats);
}

static void parse_state_init(ParseState *state, RhStore *store,
                             const RhDatImportOptions *options)
{
    memset(state, 0, sizeof *state);
    state->store = store;
    state->batch_size = options->batch_size > 0
        ? options->batch_size : RH_DAT_DEFAULT_BATCH;
    state->batch = g_ptr_array_new_with_free_func(rh_dat_entry_free_func);
    state->header_name = g_string_new(NULL);
    state->header_version = g_string_new(NULL);
    state->store_error = RH_OK;
}

static void parse_state_clear(ParseState *state)
{
    if (state->batch != NULL)
        g_ptr_array_free(state->batch, TRUE);
    if (state->header_name != NULL)
        g_string_free(state->header_name, TRUE);
    if (state->header_version != NULL)
        g_string_free(state->header_version, TRUE);
    reset_game(state);
}

RhErr rh_dat_import(RhStore *store, const char *path,
                    const RhDatImportOptions *options,
                    RhDatImportStats *out_stats)
{
    RhDatImportStats stats;
    rh_dat_import_stats_clear(&stats);
    if (out_stats != NULL)
        *out_stats = stats;

    if (store == NULL || path == NULL || options == NULL ||
        options->platform == NULL || options->platform[0] == '\0')
        return RH_ERR_INVALID;
    if (!g_utf8_validate(path, -1, NULL))
        return RH_ERR_INVALID;
    /* DAT 导入是独立事务：调用方已开事务时语义不清，直接拒绝。 */
    if (store_in_transaction(store))
        return RH_ERR_INVALID;

    GStatBuf st;
    if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return RH_ERR_IO;
    guint64 total_bytes = (guint64)st.st_size;

    FILE *fp = g_fopen(path, "rb");
    if (fp == NULL)
        return RH_ERR_IO;

    gboolean declared = FALSE;
    gchar *charset = NULL;
    gboolean guess_gb = FALSE;
    if (!sniff_encoding(fp, &declared, &charset, &guess_gb, &stats)) {
        fclose(fp);
        return RH_ERR_IO;
    }

    Decoder dec;
    memset(&dec, 0, sizeof dec);
    dec.cd = (GIConv)-1;
    dec.carry = g_byte_array_new();

    gboolean decoder_ok;
    if (declared && charset_is_utf8(charset))
        decoder_ok = decoder_use_utf8(&dec, FALSE);
    else if (declared)
        decoder_ok = decoder_use_charset(&dec, charset);
    else if (charset != NULL) /* 嗅探为 GB18030 */
        decoder_ok = decoder_use_charset(&dec, charset);
    else
        decoder_ok = decoder_use_utf8(&dec, TRUE);

    g_free(charset);
    if (!decoder_ok) {
        decoder_clear(&dec);
        fclose(fp);
        return RH_ERR_UNSUPPORTED;
    }

    RhErr err = store_begin(store);
    if (err != RH_OK) {
        decoder_clear(&dec);
        fclose(fp);
        return err;
    }

    gint64 dat_id = 0;
    err = store_dat_begin(store, options->platform, path,
                          options->dat_name, options->dat_version,
                          stats.encoding, &dat_id);
    if (err != RH_OK) {
        store_rollback(store);
        decoder_clear(&dec);
        fclose(fp);
        return err;
    }

    ParseState state;
    parse_state_init(&state, store, options);
    state.dat_id = dat_id;

    ParseOutcome outcome = parse_document(fp, &dec, &state, options, path,
                                          total_bytes);

    /* 无声明且 UTF-8 解码失败：回滚后按 GB18030 重来一次。 */
    if (outcome == PARSE_RESTART) {
        parse_state_clear(&state);
        store_rollback(store);

        stats.encoding_guessed = TRUE;
        stats.encoding_declared = FALSE;
        g_strlcpy(stats.encoding, "gb18030", sizeof stats.encoding);
        if (!decoder_use_charset(&dec, "GB18030")) {
            decoder_clear(&dec);
            fclose(fp);
            return RH_ERR_UNSUPPORTED;
        }

        if (fseek(fp, 0, SEEK_SET) != 0) {
            decoder_clear(&dec);
            fclose(fp);
            return RH_ERR_IO;
        }

        err = store_begin(store);
        if (err != RH_OK) {
            decoder_clear(&dec);
            fclose(fp);
            return err;
        }
        err = store_dat_begin(store, options->platform, path,
                              options->dat_name, options->dat_version,
                              stats.encoding, &dat_id);
        if (err != RH_OK) {
            store_rollback(store);
            decoder_clear(&dec);
            fclose(fp);
            return err;
        }

        parse_state_init(&state, store, options);
        state.dat_id = dat_id;
        outcome = parse_document(fp, &dec, &state, options, path, total_bytes);
    }

    stats.encoding_guessed = stats.encoding_guessed || guess_gb;
    stats.escaped = dec.escaped != 0;
    stats.games = state.games;
    stats.skipped = state.skipped;
    stats.bad_fields = state.bad_fields;
    stats.disk_entries = state.disk_entries;
    stats.sample_skipped = state.sample_skipped;
    stats.cancelled = outcome == PARSE_CANCEL;
    stats.xml_error = outcome == PARSE_XML_ERROR;

    {
        const gchar *label = dec.mode == DEC_UTF8 ? "utf-8" : dec.charset;
        gchar *lower = g_ascii_strdown(label != NULL ? label : "utf-8", -1);
        g_strlcpy(stats.encoding, lower, sizeof stats.encoding);
        g_free(lower);
    }

    if (outcome == PARSE_CANCEL) {
        parse_state_clear(&state);
        store_rollback(store);
        decoder_clear(&dec);
        fclose(fp);
        if (out_stats != NULL)
            *out_stats = stats;
        return RH_ERR_CANCELLED;
    }

    if (outcome == PARSE_STORE_ERROR || outcome == PARSE_IO_ERROR) {
        RhErr failure = state.store_error != RH_OK ? state.store_error : RH_ERR_IO;
        parse_state_clear(&state);
        store_rollback(store);
        decoder_clear(&dec);
        fclose(fp);
        return failure;
    }

    /* XML 结构错误不整体失败：把出错前攒下的条目落库，报告 xml_error。 */
    err = flush_batch(&state);
    if (err == RH_OK) {
        gchar *header_name = g_strstrip(g_strdup(state.header_name->str));
        gchar *header_version = g_strstrip(g_strdup(state.header_version->str));

        gchar *basename = g_path_get_basename(path);
        const gchar *final_name = options->dat_name;
        if (final_name == NULL || final_name[0] == '\0')
            final_name = header_name[0] != '\0' ? header_name : basename;
        const gchar *final_version = options->dat_version;
        if (final_version == NULL || final_version[0] == '\0')
            final_version = header_version[0] != '\0' ? header_version : NULL;

        err = store_dat_finish(store, dat_id, final_name, final_version,
                               stats.skipped, &stats.entry_count);
        g_free(header_name);
        g_free(header_version);
        g_free(basename);
    }

    gint64 entry_count = stats.entry_count;
    parse_state_clear(&state);

    if (err == RH_OK && entry_count == 0) {
        /* 一条都没解析出来：不是可用的 DAT，回滚不留空壳。 */
        store_rollback(store);
        decoder_clear(&dec);
        fclose(fp);
        return RH_ERR_INVALID;
    }

    if (err == RH_OK)
        err = store_commit(store);
    else
        store_rollback(store);

    decoder_clear(&dec);
    fclose(fp);

    if (out_stats != NULL)
        *out_stats = stats;
    return err;
}

/* ===========================================================================
 * 匹配
 * ========================================================================= */

RhDatMatchKind rh_dat_match(RhStore *store, const RhHashResult *hash,
                            const char *platform, GPtrArray **out_matches,
                            RhErr *out_err)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    if (out_matches != NULL)
        *out_matches = NULL;
    if (store == NULL || hash == NULL || out_matches == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return RH_DAT_MATCH_NONE;
    }

    /* 抗碰撞优先：sha1 → md5 → crc32，前一种有命中就停。
     * sha1 是 DAT 生态的事实标准（No-Intro / Redump 都要求），碰撞概率最低；
     * crc32 无密码学安全性，仅作为最后兜底（部分旧 DAT 只有 crc）。
     * 每次查询都先按 size 缩小范围（store_dat_find_by_hash 的 size 参数）。 */
    typedef struct {
        const char *algo;
        gboolean    available;
        gchar      *value;
    } Probe;

    gchar crc_text[9] = {0};
    gchar *crc = NULL;
    if (hash->has_crc32) {
        g_snprintf(crc_text, sizeof crc_text, "%08x", hash->crc32);
        crc = crc_text;
    }

    Probe probes[3] = {
        { "sha1", hash->has_sha1 && hash->sha1[0] != '\0', (gchar *)hash->sha1 },
        { "md5",  hash->has_md5 && hash->md5[0] != '\0',   (gchar *)hash->md5 },
        { "crc32", crc != NULL, crc }
    };

    for (int i = 0; i < 3; i++) {
        if (!probes[i].available)
            continue;

        GPtrArray *matches = NULL;
        RhErr err = store_dat_find_by_hash(store, probes[i].algo, probes[i].value,
                                           (gint64)hash->size, platform, &matches);
        if (err != RH_OK) {
            if (out_err != NULL)
                *out_err = err;
            return RH_DAT_MATCH_NONE;
        }
        if (matches->len == 0) {
            g_ptr_array_unref(matches);
            continue;
        }

        *out_matches = matches;
        return matches->len == 1 ? RH_DAT_MATCH_UNIQUE : RH_DAT_MATCH_AMBIGUOUS;
    }

    GPtrArray *empty = g_ptr_array_new_with_free_func(rh_dat_match_free_func);
    *out_matches = empty;
    return RH_DAT_MATCH_NONE;
}
