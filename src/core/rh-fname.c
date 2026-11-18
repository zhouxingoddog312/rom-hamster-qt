#include "rh-fname.h"

#include <string.h>

/* 整串都必须是合法的 GB18030；任何一段失败都视为解码失败，不做局部替换。 */
static gchar *decode_gb18030(const char *bytes, gsize len)
{
    gsize consumed = 0;
    GError *error = NULL;
    gchar *out = g_convert(bytes, (gssize)len, "UTF-8", "GB18030",
                           &consumed, NULL, &error);
    if (error != NULL) {
        g_error_free(error);
        g_free(out);
        return NULL;
    }
    if (out == NULL)
        return NULL;
    if (consumed != len || !g_utf8_validate(out, -1, NULL)) {
        g_free(out);
        return NULL;
    }
    return out;
}

static gint hex_value(gchar c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

RhFName *rh_fname_from_bytes(const gchar *bytes, gssize len)
{
    if (bytes == NULL)
        return NULL;
    if (len < 0)
        len = (gssize)strlen(bytes);

    RhFName *name = g_new0(RhFName, 1);
    name->raw = g_bytes_new(bytes, (gsize)len);

    /* 内嵌 NUL 无法作为 C 字符串显示，直接走转义（%00），保证无损。 */
    gboolean has_nul = memchr(bytes, '\0', (gsize)len) != NULL;
    if (!has_nul && g_utf8_validate(bytes, len, NULL)) {
        name->display = g_strndup(bytes, (gsize)len);
        name->encoding = RH_FNAME_UTF8;
        return name;
    }

    gchar *decoded = has_nul ? NULL : decode_gb18030(bytes, (gsize)len);
    if (decoded != NULL) {
        name->display = decoded;
        name->encoding = RH_FNAME_GB18030;
    } else {
        name->display = rh_fname_escape(bytes, len);
        name->encoding = RH_FNAME_ESCAPED;
    }
    return name;
}

RhFName *rh_fname_from_utf8(const gchar *utf8)
{
    return rh_fname_from_bytes(utf8, -1);
}

void rh_fname_free(RhFName *name)
{
    if (name == NULL)
        return;
    g_free(name->display);
    g_bytes_unref(name->raw);
    g_free(name);
}

gboolean rh_fname_is_guessed(const RhFName *name)
{
    return name != NULL && name->encoding == RH_FNAME_GB18030;
}

gboolean rh_fname_is_locatable(const RhFName *name)
{
    if (name == NULL)
        return FALSE;
#ifdef _WIN32
    return name->encoding != RH_FNAME_ESCAPED;
#else
    return TRUE;
#endif
}

gchar *rh_fname_raw_string(const RhFName *name, gsize *out_len)
{
    if (name == NULL)
        return NULL;
    gsize len = 0;
    const gchar *data = g_bytes_get_data(name->raw, &len);
    if (out_len != NULL)
        *out_len = len;
    return g_strndup(data, len);
}

gchar *rh_fname_native_name(const RhFName *name)
{
    if (name == NULL)
        return NULL;
#ifdef _WIN32
    return name->encoding == RH_FNAME_ESCAPED ? NULL : g_strdup(name->display);
#else
    return rh_fname_raw_string(name, NULL);
#endif
}

GBytes *rh_fname_join_raw(GBytes *parent, GBytes *child)
{
    if (child == NULL)
        return NULL;

    gsize parent_len = 0;
    const gchar *parent_data = parent != NULL
        ? g_bytes_get_data(parent, &parent_len) : NULL;
    gsize child_len = 0;
    const gchar *child_data = g_bytes_get_data(child, &child_len);

    if (parent_data == NULL || parent_len == 0)
        return g_bytes_new(child_data, child_len);
    if (child_len == 0)
        return g_bytes_new(parent_data, parent_len);

    gchar *joined = g_malloc(parent_len + 1 + child_len);
    memcpy(joined, parent_data, parent_len);
    joined[parent_len] = '/';
    memcpy(joined + parent_len + 1, child_data, child_len);
    GBytes *result = g_bytes_new_take(joined, parent_len + 1 + child_len);
    return result;
}

gchar *rh_fname_escape(const gchar *bytes, gssize len)
{
    if (bytes == NULL)
        return NULL;
    if (len < 0)
        len = (gssize)strlen(bytes);

    GString *out = g_string_sized_new((gsize)len + 8);
    for (gssize i = 0; i < len; i++) {
        guchar b = (guchar)bytes[i];
        if (b == '%' || b == '\\')
            g_string_append_printf(out, "%%%02X", b);
        else if (b >= 0x20 && b <= 0x7E)
            g_string_append_c(out, (gchar)b);
        else
            g_string_append_printf(out, "%%%02X", b);
    }
    return g_string_free(out, FALSE);
}

gchar *rh_fname_unescape(const gchar *escaped, gsize *out_len)
{
    if (out_len != NULL)
        *out_len = 0;
    if (escaped == NULL)
        return NULL;

    GString *out = g_string_sized_new(strlen(escaped));
    for (const gchar *p = escaped; *p != '\0';) {
        if (*p == '%') {
            gint hi = hex_value(p[1]);
            gint lo = hi >= 0 ? hex_value(p[2]) : -1;
            /* 必须恰好两位十六进制；\0 结尾由 hex_value 判负拒绝 */
            if (hi < 0 || lo < 0) {
                g_string_free(out, TRUE);
                return NULL;
            }
            g_string_append_c(out, (gchar)((hi << 4) | lo));
            p += 3;
        } else if ((guchar)*p >= 0x20 && (guchar)*p <= 0x7E) {
            g_string_append_c(out, *p);
            p++;
        } else {
            /* 转义结果只应包含可打印 ASCII */
            g_string_free(out, TRUE);
            return NULL;
        }
    }

    gsize len = out->len;
    g_string_append_c(out, '\0');
    gchar *result = g_string_free(out, FALSE);
    if (out_len != NULL)
        *out_len = len;
    return result;
}
