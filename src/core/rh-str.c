#include "rh-str.h"

#include <string.h>

gchar *rh_str_extension(const char *filename)
{
    if (filename == NULL)
        return NULL;

    const gchar *base = rh_str_basename(filename);
    const gchar *dot = strrchr(base, '.');

    /* 开头的点不算后缀（.bashrc） */
    if (dot == NULL || dot == base || dot[1] == '\0')
        return NULL;

    return g_ascii_strdown(dot, -1);
}

gchar *rh_str_stem(const char *filename)
{
    if (filename == NULL)
        return NULL;

    const gchar *base = rh_str_basename(filename);
    const gchar *dot = strrchr(base, '.');

    if (dot == NULL || dot == base)
        return g_strdup(filename);

    return g_strndup(filename, (gsize)(dot - filename));
}

const gchar *rh_str_basename(const char *path)
{
    if (path == NULL)
        return NULL;

    const gchar *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

gchar *rh_str_norm_key(const char *s)
{
    if (s == NULL)
        return g_strdup("");

    gchar *lower = g_utf8_strdown(s, -1);
    GString *out = g_string_sized_new(strlen(lower));

    for (const gchar *p = lower; *p != '\0';) {
        gunichar ch = g_utf8_get_char(p);
        if (g_unichar_isalnum(ch))
            g_string_append_unichar(out, ch);
        p = g_utf8_next_char(p);
    }

    g_free(lower);
    return g_string_free(out, FALSE);
}

/* 编辑距离，按 Unicode 字符逐位比较 */
static int edit_distance(const gchar *a, const gchar *b)
{
    glong la = g_utf8_strlen(a, -1);
    glong lb = g_utf8_strlen(b, -1);
    gint *prev = g_new(gint, (gsize)lb + 1);
    gint *cur = g_new(gint, (gsize)lb + 1);

    for (glong j = 0; j <= lb; j++)
        prev[j] = (gint)j;

    const gchar *pa = a;
    for (glong i = 1; i <= la; i++) {
        cur[0] = (gint)i;
        const gchar *pb = b;
        for (glong j = 1; j <= lb; j++) {
            gint cost = (pa[0] == pb[0]) ? 0 : 1;
            gint del = prev[j] + 1;
            gint ins = cur[j - 1] + 1;
            gint sub = prev[j - 1] + cost;
            gint best = del < ins ? del : ins;
            cur[j] = best < sub ? best : sub;
            pb = g_utf8_next_char(pb);
        }
        gint *swap = prev;
        prev = cur;
        cur = swap;
        pa = g_utf8_next_char(pa);
    }

    gint dist = prev[lb];
    g_free(prev);
    g_free(cur);
    return dist;
}

int rh_str_similarity(const char *a, const char *b)
{
    gchar *ka = rh_str_norm_key(a);
    gchar *kb = rh_str_norm_key(b);
    int score;

    if (*ka == '\0' && *kb == '\0') {
        score = 100;
    } else if (*ka == '\0' || *kb == '\0') {
        score = 0;
    } else {
        glong la = g_utf8_strlen(ka, -1);
        glong lb = g_utf8_strlen(kb, -1);
        glong max = MAX(la, lb);
        int dist = edit_distance(ka, kb);
        score = (int)(100 - (dist * 100) / max);
        score = CLAMP(score, 0, 100);
    }

    g_free(ka);
    g_free(kb);
    return score;
}

/* 大小写不敏感的子串查找（GLib 没有提供） */
static const gchar *find_ci(const gchar *hay, const gchar *needle)
{
    gsize len = strlen(needle);
    for (const gchar *p = hay; *p != '\0'; p = g_utf8_next_char(p)) {
        if (g_ascii_strncasecmp(p, needle, len) == 0)
            return p;
    }
    return NULL;
}

/*
 * 找到关键字后跳过可选的分隔符，再读 1–2 位数字。
 * 关键字前必须是字符串开头或非字母（避免 "disc" 命中 "discover"）。
 */
static gboolean match_disc_token(const gchar *base, const gchar *kw, int *out)
{
    gsize kwlen = strlen(kw);
    const gchar *p = base;

    while ((p = find_ci(p, kw)) != NULL) {
        gboolean boundary = (p == base);
        if (!boundary) {
            const gchar *prev = g_utf8_prev_char(p);
            boundary = !g_unichar_isalpha(g_utf8_get_char(prev));
        }

        if (boundary) {
            const gchar *q = p + kwlen;
            while (*q == ' ' || *q == '-' || *q == '_' || *q == '.')
                q++;
            if (*q == '#' || *q == ':')
                q++;
            while (*q == ' ' || *q == '-')
                q++;

            if (g_ascii_isdigit(*q)) {
                int value = 0;
                int digits = 0;
                while (g_ascii_isdigit(*q) && digits < 2) {
                    value = value * 10 + (*q - '0');
                    q++;
                    digits++;
                }
                if (value >= 1 && value <= 99) {
                    *out = value;
                    return TRUE;
                }
            }
        }
        p += kwlen;
    }
    return FALSE;
}

gboolean rh_str_parse_disc(const char *filename, int *disc_no)
{
    if (filename == NULL || disc_no == NULL)
        return FALSE;

    const gchar *base = rh_str_basename(filename);
    static const char *const keywords[] = { "disc", "disk", "cd", "side" };

    for (gsize i = 0; i < G_N_ELEMENTS(keywords); i++) {
        if (match_disc_token(base, keywords[i], disc_no))
            return TRUE;
    }
    return FALSE;}

gboolean rh_str_is_reserved_name(const char *name)
{
    if (name == NULL || *name == '\0')
        return FALSE;

    const gchar *dot = strchr(name, '.');
    gchar *head = dot != NULL ? g_strndup(name, (gsize)(dot - name)) : g_strdup(name);
    gchar *upper = g_ascii_strup(head, -1);
    gboolean reserved = FALSE;

    static const char *const fixed[] = { "CON", "PRN", "AUX", "NUL" };
    for (gsize i = 0; i < G_N_ELEMENTS(fixed); i++) {
        if (strcmp(upper, fixed[i]) == 0) {
            reserved = TRUE;
            break;
        }
    }

    if (!reserved && strlen(upper) == 4) {
        const char *prefix = NULL;
        if (g_str_has_prefix(upper, "COM"))
            prefix = "COM";
        else if (g_str_has_prefix(upper, "LPT"))
            prefix = "LPT";
        if (prefix != NULL && upper[3] >= '1' && upper[3] <= '9')
            reserved = TRUE;
    }

    g_free(upper);
    g_free(head);
    return reserved;
}

gchar *rh_str_sanitize_component(const char *name)
{
    if (name == NULL)
        return g_strdup("_");

    GString *out = g_string_sized_new(strlen(name));
    for (const gchar *p = name; *p != '\0';) {
        gunichar ch = g_utf8_get_char(p);
        if (ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' ||
            ch == '\\' || ch == '|' || ch == '?' || ch == '*' || ch < 0x20) {
            g_string_append_c(out, '_');
        } else {
            g_string_append_unichar(out, ch);
        }
        p = g_utf8_next_char(p);
    }

    /* 截断到 120 个字符以内，Windows 单级名上限 255 个 UTF-16 单元，留足余量 */
    if (g_utf8_strlen(out->str, -1) > 120) {
        gchar *cut = g_utf8_offset_to_pointer(out->str, 120);
        g_string_truncate(out, (gsize)(cut - out->str));
    }

    /* 去掉结尾的点和空格 */
    while (out->len > 0 && (out->str[out->len - 1] == '.' || out->str[out->len - 1] == ' '))
        g_string_truncate(out, out->len - 1);

    /* 去掉开头的空格 */
    gsize lead = 0;
    while (out->str[lead] == ' ')
        lead++;
    if (lead > 0)
        g_string_erase(out, 0, (gssize)lead);

    if (out->len == 0)
        g_string_append_c(out, '_');

    if (rh_str_is_reserved_name(out->str))
        g_string_prepend_c(out, '_');

    return g_string_free(out, FALSE);
}
