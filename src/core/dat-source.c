#include "dat-source.h"

#include <stdarg.h>
#include <string.h>

#include "rh-path.h"

#define RH_DAT_SOURCES_SPEC_VERSION 1

void rh_dat_source_free(RhDatSource *source)
{
    if (source == NULL)
        return;
    g_free(source->platform);
    g_free(source->name);
    g_free(source->file);
    g_ptr_array_free(source->urls, TRUE);
    g_free(source->sha1);
    g_free(source);
}

static RhDatSource *source_new(void)
{
    RhDatSource *source = g_new0(RhDatSource, 1);
    source->urls = g_ptr_array_new_with_free_func(g_free);
    return source;
}

static gboolean set_error(gchar **message, const char *path, int lineno,
                          const char *format, ...)
{
    va_list args;
    va_start(args, format);
    gchar *detail = g_strdup_vprintf(format, args);
    va_end(args);

    if (message != NULL)
        *message = g_strdup_printf("%s:%d: %s", path, lineno, detail);
    g_free(detail);
    return FALSE;
}

static gboolean valid_platform_id(const gchar *id)
{
    if (id == NULL || id[0] == '\0')
        return FALSE;
    for (const gchar *p = id; *p != '\0'; p++) {
        if (!(g_ascii_islower(*p) || g_ascii_isdigit(*p) || *p == '-'))
            return FALSE;
    }
    return TRUE;
}

static gboolean valid_sha1(const gchar *sha1)
{
    if (sha1 == NULL || strlen(sha1) != 40)
        return FALSE;
    for (const gchar *p = sha1; *p != '\0'; p++) {
        if (!g_ascii_isxdigit(*p))
            return FALSE;
    }
    return TRUE;
}

static gboolean source_finish(RhDatSource *source, const char *path,
                              int lineno, gchar **message)
{
    if (source->file == NULL || source->file[0] == '\0')
        return set_error(message, path, lineno, "平台 %s 缺少 file=",
                         source->platform);
    if (strchr(source->file, '/') != NULL || strchr(source->file, '\\') != NULL)
        return set_error(message, path, lineno,
                         "平台 %s 的 file= 只能是文件名，不能包含路径分隔符",
                         source->platform);
    if (source->urls->len == 0)
        return set_error(message, path, lineno, "平台 %s 至少需要一条 url=",
                         source->platform);

    for (guint i = 0; i < source->urls->len; i++) {
        const gchar *url = g_ptr_array_index(source->urls, i);
        if (!g_str_has_prefix(url, "http://") &&
            !g_str_has_prefix(url, "https://"))
            return set_error(message, path, lineno,
                             "平台 %s 的 url 必须以 http:// 或 https:// 开头: %s",
                             source->platform, url);
    }

    if (source->sha1 != NULL) {
        if (!valid_sha1(source->sha1))
            return set_error(message, path, lineno,
                             "平台 %s 的 sha1= 必须是 40 位十六进制",
                             source->platform);
        gchar *lower = g_ascii_strdown(source->sha1, -1);
        g_free(source->sha1);
        source->sha1 = lower;
    }

    if (source->name == NULL || source->name[0] == '\0') {
        g_free(source->name);
        source->name = g_strdup(source->file);
        gchar *dot = g_strrstr(source->name, ".dat");
        if (dot != NULL && dot[4] == '\0')
            *dot = '\0';
    }
    return TRUE;
}

static void replace_string(gchar **target, const gchar *value)
{
    gchar *copy = g_strdup(value);
    g_free(*target);
    *target = copy;
}

RhErr rh_dat_sources_load(const char *path, GPtrArray **out_sources,
                          gchar **message)
{
    if (out_sources != NULL)
        *out_sources = NULL;
    if (message != NULL)
        *message = NULL;
    if (path == NULL || out_sources == NULL)
        return RH_ERR_INVALID;

    gchar *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL)) {
        if (message != NULL)
            *message = g_strdup_printf("无法读取 DAT 源清单: %s", path);
        return RH_ERR_IO;
    }
    if (!g_utf8_validate(content, -1, NULL)) {
        if (message != NULL)
            *message = g_strdup_printf("%s: 清单不是合法的 UTF-8", path);
        g_free(content);
        return RH_ERR_INVALID;
    }

    GPtrArray *sources = g_ptr_array_new_with_free_func(
        (GDestroyNotify)rh_dat_source_free);
    RhDatSource *current = NULL;
    int current_line = 0;
    gboolean in_meta = FALSE;
    RhErr result = RH_OK;

    gchar **lines = g_strsplit(content, "\n", -1);
    for (int i = 0; lines[i] != NULL; i++) {
        int lineno = i + 1;
        gchar *line = g_strstrip(lines[i]);

        if (line[0] == '#' || line[0] == '\0')
            continue;

        if (line[0] == '[') {
            gchar *close = strchr(line + 1, ']');
            if (close == NULL) {
                set_error(message, path, lineno, "段名缺少 ']'");
                result = RH_ERR_INVALID;
                break;
            }
            gchar *section = g_strstrip(g_strndup(line + 1,
                                                  (gsize)(close - (line + 1))));

            if (current != NULL) {
                if (!source_finish(current, path, current_line, message)) {
                    g_free(section);
                    result = RH_ERR_INVALID;
                    break;
                }
                g_ptr_array_add(sources, current);
                current = NULL;
            }

            if (g_strcmp0(section, "meta") == 0) {
                in_meta = TRUE;
                g_free(section);
                continue;
            }
            in_meta = FALSE;

            if (!valid_platform_id(section)) {
                set_error(message, path, lineno,
                         "平台 ID 必须是小写字母、数字或连字符: [%s]", section);
                g_free(section);
                result = RH_ERR_INVALID;
                break;
            }

            gboolean duplicate = FALSE;
            for (guint s = 0; s < sources->len; s++) {
                RhDatSource *existing = g_ptr_array_index(sources, s);
                if (g_strcmp0(existing->platform, section) == 0) {
                    duplicate = TRUE;
                    break;
                }
            }
            if (duplicate) {
                set_error(message, path, lineno, "平台重复: [%s]", section);
                g_free(section);
                result = RH_ERR_INVALID;
                break;
            }

            current = source_new();
            current->platform = g_strdup(section);
            current_line = lineno;
            g_free(section);
            continue;
        }

        gchar *eq = strchr(line, '=');
        if (eq == NULL) {
            set_error(message, path, lineno, "缺少 '=': %s", line);
            result = RH_ERR_INVALID;
            break;
        }
        *eq = '\0';
        gchar *key = g_strstrip(line);
        gchar *value = g_strstrip(eq + 1);

        if (in_meta) {
            if (g_strcmp0(key, "spec_version") == 0) {
                gchar *end = NULL;
                gint64 version = g_ascii_strtoll(value, &end, 10);
                if (end == value || *end != '\0' || version < 1) {
                    set_error(message, path, lineno,
                             "spec_version 必须是正整数: %s", value);
                    result = RH_ERR_INVALID;
                    break;
                }
                if (version > RH_DAT_SOURCES_SPEC_VERSION) {
                    set_error(message, path, lineno,
                             "不支持的 spec_version %s（请升级 rom-hamster）",
                             value);
                    result = RH_ERR_INVALID;
                    break;
                }
            }
            /* [meta] 里的其他键忽略 */
            continue;
        }

        if (current == NULL) {
            set_error(message, path, lineno, "键值出现在平台段之前: %s", key);
            result = RH_ERR_INVALID;
            break;
        }

        if (g_strcmp0(key, "name") == 0) {
            replace_string(&current->name, value);
        } else if (g_strcmp0(key, "file") == 0) {
            replace_string(&current->file, value);
        } else if (g_strcmp0(key, "url") == 0) {
            g_ptr_array_add(current->urls, g_strdup(value));
        } else if (g_strcmp0(key, "sha1") == 0) {
            replace_string(&current->sha1, value);
        }
        /* 其他键：按格式规范忽略 */
    }

    if (result == RH_OK && current != NULL) {
        if (!source_finish(current, path, current_line, message))
            result = RH_ERR_INVALID;
        else
            g_ptr_array_add(sources, current);
        current = NULL;
    }
    if (result != RH_OK && current != NULL)
        rh_dat_source_free(current);

    g_strfreev(lines);
    g_free(content);

    if (result != RH_OK) {
        g_ptr_array_free(sources, TRUE);
        return result;
    }
    *out_sources = sources;
    return RH_OK;
}

gchar *rh_dat_sources_default_path(void)
{
    const gchar *env = g_getenv("ROM_HAMSTER_DAT_SOURCES");
    if (env != NULL && env[0] != '\0' &&
        g_file_test(env, G_FILE_TEST_IS_REGULAR))
        return g_strdup(env);

    gchar *user = g_build_filename(g_get_user_config_dir(), "rom-hamster",
                                   "dats", "sources.conf", NULL);
    if (g_file_test(user, G_FILE_TEST_IS_REGULAR))
        return user;
    g_free(user);

    GPtrArray *dirs = rh_path_data_dir_candidates();
    gchar *found = NULL;
    for (guint i = 0; i < dirs->len && found == NULL; i++) {
        const char *dir = g_ptr_array_index(dirs, i);
        gchar *candidate = g_build_filename(dir, "dats", "sources.conf", NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR))
            found = candidate;
        else
            g_free(candidate);
    }
    g_ptr_array_free(dirs, TRUE);
    return found;
}
