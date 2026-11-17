#include "platform.h"

#include <string.h>

#include "rh-str.h"

#define RH_PLATFORM_DOMAIN "romham/platform"

struct RhPlatformRules {
    GPtrArray  *platforms;        /* RhPlatform* */
    GHashTable *by_name;          /* id/alias(lower) -> RhPlatform* */
    GPtrArray  *arcade_suffixes;
    GHashTable *media_dirs;       /* kind -> gchar* */
    GHashTable *media_suffixes;   /* kind -> GPtrArray* of gchar* */
    GPtrArray  *archive_suffixes;
    int         spec_version;
};

static void platform_free(gpointer data)
{
    RhPlatform *platform = data;
    if (platform == NULL)
        return;
    g_free(platform->id);
    g_free(platform->recommended_format);
    g_ptr_array_free(platform->suffixes, TRUE);
    g_ptr_array_free(platform->patch_formats, TRUE);
    g_ptr_array_free(platform->ambiguous_formats, TRUE);
    g_ptr_array_free(platform->aliases, TRUE);
    g_free(platform);
}

static GPtrArray *split_suffixes(const gchar *value)
{
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    gchar **parts = g_strsplit(value, ";", -1);
    for (int i = 0; parts[i] != NULL; i++) {
        gchar *item = g_strstrip(parts[i]);
        if (item[0] == '.') {
            gchar *norm = g_ascii_strdown(item, -1);
            g_ptr_array_add(out, norm);
        }
    }
    g_strfreev(parts);
    return out;
}

static GPtrArray *split_list(const gchar *value)
{
    GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
    gchar **parts = g_strsplit(value, ",", -1);
    for (int i = 0; parts[i] != NULL; i++) {
        gchar *item = g_strstrip(parts[i]);
        if (item[0] != '\0')
            g_ptr_array_add(out, g_ascii_strdown(item, -1));
    }
    g_strfreev(parts);
    return out;
}

static const gchar *section_of(const gchar *line)
{
    /* 返回静态区段名；未识别返回 NULL */
    if (g_str_has_prefix(line, "[meta]"))
        return "meta";
    if (g_str_has_prefix(line, "[platform_extensions]"))
        return "platform_extensions";
    if (g_str_has_prefix(line, "[arcade_rom_suffixes]"))
        return "arcade_rom_suffixes";
    if (g_str_has_prefix(line, "[media_type_dirs]"))
        return "media_type_dirs";
    if (g_str_has_prefix(line, "[media_suffix_rules]"))
        return "media_suffix_rules";
    if (g_str_has_prefix(line, "[archive_extensions]"))
        return "archive_extensions";
    return NULL;
}

static void add_suffixes_to(GPtrArray *target, const gchar *value)
{
    GPtrArray *tmp = split_suffixes(value);
    for (guint i = 0; i < tmp->len; i++)
        g_ptr_array_add(target, g_strdup(g_ptr_array_index(tmp, i)));
    g_ptr_array_free(tmp, TRUE);
}

static gboolean parse_platform_line(RhPlatformRules *rules, const gchar *line,
                                    gchar **message)
{
    const gchar *colon = strchr(line, ':');
    if (colon == NULL) {
        *message = g_strdup_printf("平台行缺少 ':': %s", line);
        return FALSE;
    }

    gchar *id = g_ascii_strdown(g_strndup(line, (gsize)(colon - line)), -1);
    if (id[0] == '\0' || rh_platform_by_id(rules, id) != NULL) {
        *message = g_strdup_printf("平台 ID 为空或重复: %s", line);
        g_free(id);
        return FALSE;
    }

    RhPlatform *platform = g_new0(RhPlatform, 1);
    platform->id = id;
    platform->suffixes = g_ptr_array_new_with_free_func(g_free);
    platform->patch_formats = g_ptr_array_new_with_free_func(g_free);
    platform->ambiguous_formats = g_ptr_array_new_with_free_func(g_free);
    platform->aliases = g_ptr_array_new_with_free_func(g_free);

    gchar **fields = g_strsplit(colon + 1, ";", -1);

    /* 列表型键的值会跨多个 ';' 分段，需要累积到下一个 key=value 为止 */
    gchar *list_key = NULL;
    GString *list_value = NULL;

    #define FLUSH_LIST()                                                        \
        do {                                                                   \
            if (list_key != NULL) {                                            \
                if (g_strcmp0(list_key, "aliases") == 0) {                     \
                    GPtrArray *aliases = split_list(list_value->str);          \
                    for (guint a = 0; a < aliases->len; a++)                   \
                        g_ptr_array_add(platform->aliases,                     \
                                        g_strdup(g_ptr_array_index(aliases, a))); \
                    g_ptr_array_free(aliases, TRUE);                           \
                } else if (g_strcmp0(list_key, "patch_formats") == 0) {        \
                    add_suffixes_to(platform->patch_formats, list_value->str); \
                } else if (g_strcmp0(list_key, "ambiguous_formats") == 0) {    \
                    add_suffixes_to(platform->ambiguous_formats,               \
                                    list_value->str);                          \
                }                                                              \
                g_clear_pointer(&list_key, g_free);                            \
                if (list_value != NULL) {                                      \
                    g_string_free(list_value, TRUE);                           \
                    list_value = NULL;                                         \
                }                                                              \
            }                                                                  \
        } while (0)

    for (int i = 0; fields[i] != NULL; i++) {
        gchar *field = g_strstrip(fields[i]);
        if (field[0] == '\0')
            continue;

        gchar *eq = strchr(field, '=');
        if (eq == NULL) {
            if (list_value != NULL) {
                g_string_append_c(list_value, ';');
                g_string_append(list_value, field);
            } else if (field[0] == '.') {
                g_ptr_array_add(platform->suffixes, g_ascii_strdown(field, -1));
            }
            continue;
        }

        FLUSH_LIST();

        gchar *key = g_strstrip(g_strndup(field, (gsize)(eq - field)));
        gchar *value = g_strstrip(g_strdup(eq + 1));

        if (g_strcmp0(key, "compress_supported") == 0) {
            platform->compress_supported = g_ascii_strcasecmp(value, "true") == 0;
        } else if (g_strcmp0(key, "arcade") == 0) {
            platform->arcade = g_ascii_strcasecmp(value, "true") == 0;
        } else if (g_strcmp0(key, "recommended_format") == 0) {
            g_free(platform->recommended_format);
            platform->recommended_format = g_ascii_strdown(value, -1);
        } else if (g_strcmp0(key, "patch_formats") == 0 ||
                   g_strcmp0(key, "ambiguous_formats") == 0 ||
                   g_strcmp0(key, "aliases") == 0) {
            list_key = key;
            key = NULL;
            list_value = g_string_new(value);
        }
        /* 其他键：按格式规范忽略 */

        g_free(key);
        g_free(value);
    }
    FLUSH_LIST();
    #undef FLUSH_LIST
    g_strfreev(fields);

    if (platform->recommended_format == NULL)
        platform->recommended_format = g_strdup("raw");

    g_ptr_array_add(rules->platforms, platform);
    g_hash_table_insert(rules->by_name, g_strdup(platform->id), platform);
    for (guint i = 0; i < platform->aliases->len; i++) {
        const gchar *alias = g_ptr_array_index(platform->aliases, i);
        if (!g_hash_table_contains(rules->by_name, alias))
            g_hash_table_insert(rules->by_name, g_strdup(alias), platform);
    }
    return TRUE;
}

RhPlatformRules *rh_platform_rules_load(const char *path, RhErr *out_err, gchar **message)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    if (message != NULL)
        *message = NULL;
    if (path == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return NULL;
    }

    gchar *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL)) {
        if (out_err != NULL)
            *out_err = RH_ERR_IO;
        if (message != NULL)
            *message = g_strdup_printf("无法读取规则文件: %s", path);
        return NULL;
    }

    RhPlatformRules *rules = g_new0(RhPlatformRules, 1);
    rules->platforms = g_ptr_array_new_with_free_func(platform_free);
    rules->by_name = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    rules->arcade_suffixes = g_ptr_array_new_with_free_func(g_free);
    rules->media_dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    rules->media_suffixes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                  (GDestroyNotify)g_ptr_array_unref);
    rules->archive_suffixes = g_ptr_array_new_with_free_func(g_free);

    const gchar *section = NULL;
    gchar **lines = g_strsplit(content, "\n", -1);
    for (int i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        int lineno = i + 1;

        if (line[0] == '#' || line[0] == '\0')
            continue;

        if (line[0] == '[') {
            section = section_of(line);
            continue;
        }

        if (section == NULL)
            continue;

        if (g_strcmp0(section, "meta") == 0) {
            if (g_str_has_prefix(line, "spec_version="))
                rules->spec_version = atoi(line + strlen("spec_version="));
            continue;
        }

        if (g_strcmp0(section, "platform_extensions") == 0) {
            gchar *problem = NULL;
            if (!parse_platform_line(rules, line, &problem)) {
                if (out_err != NULL)
                    *out_err = RH_ERR_INVALID;
                if (message != NULL)
                    *message = g_strdup_printf("%s:%d: %s", path, lineno, problem);
                g_free(problem);
                g_free(content);
                g_strfreev(lines);
                rh_platform_rules_free(rules);
                return NULL;
            }
            continue;
        }

        if (g_strcmp0(section, "arcade_rom_suffixes") == 0) {
            add_suffixes_to(rules->arcade_suffixes, line);
            continue;
        }

        if (g_strcmp0(section, "archive_extensions") == 0) {
            add_suffixes_to(rules->archive_suffixes, line);
            continue;
        }

        if (g_strcmp0(section, "media_type_dirs") == 0) {
            gchar **pair = g_strsplit(line, "=", 2);
            if (pair[0] != NULL && pair[1] != NULL)
                g_hash_table_insert(rules->media_dirs, g_strstrip(g_strdup(pair[0])),
                                    g_strstrip(g_strdup(pair[1])));
            g_strfreev(pair);
            continue;
        }

        if (g_strcmp0(section, "media_suffix_rules") == 0) {
            gchar **pair = g_strsplit(line, "=", 2);
            if (pair[0] != NULL && pair[1] != NULL) {
                GPtrArray *suffixes = split_suffixes(pair[1]);
                g_hash_table_insert(rules->media_suffixes,
                                    g_strstrip(g_strdup(pair[0])), suffixes);
            }
            g_strfreev(pair);
            continue;
        }
    }
    g_strfreev(lines);
    g_free(content);

    if (rules->platforms->len == 0) {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        if (message != NULL)
            *message = g_strdup_printf("%s: 没有任何平台条目", path);
        rh_platform_rules_free(rules);
        return NULL;
    }
    return rules;
}

void rh_platform_rules_free(RhPlatformRules *rules)
{
    if (rules == NULL)
        return;
    g_ptr_array_free(rules->platforms, TRUE);
    g_hash_table_destroy(rules->by_name);
    g_ptr_array_free(rules->arcade_suffixes, TRUE);
    g_hash_table_destroy(rules->media_dirs);
    g_hash_table_destroy(rules->media_suffixes);
    g_ptr_array_free(rules->archive_suffixes, TRUE);
    g_free(rules);
}

int rh_platform_rules_version(const RhPlatformRules *rules)
{
    return rules != NULL ? rules->spec_version : 0;
}

guint rh_platform_count(const RhPlatformRules *rules)
{
    return rules != NULL ? rules->platforms->len : 0;
}

RhPlatform *rh_platform_by_id(const RhPlatformRules *rules, const char *id_or_alias)
{
    if (rules == NULL || id_or_alias == NULL)
        return NULL;
    gchar *key = g_ascii_strdown(id_or_alias, -1);
    RhPlatform *platform = g_hash_table_lookup(rules->by_name, key);
    g_free(key);
    return platform;
}

const GPtrArray *rh_platform_all(const RhPlatformRules *rules)
{
    return rules != NULL ? rules->platforms : NULL;
}

static gboolean suffix_in(const GPtrArray *suffixes, const gchar *ext)
{
    for (guint i = 0; i < suffixes->len; i++) {
        if (g_strcmp0(g_ptr_array_index(suffixes, i), ext) == 0)
            return TRUE;
    }
    return FALSE;
}

/* 取 ".p8.png" 这类复合后缀；无内层后缀时返回 NULL */
static gchar *compound_suffix(const char *filename)
{
    gchar *last = rh_str_extension(filename);
    if (last == NULL)
        return NULL;

    gchar *stem = rh_str_stem(filename);
    gchar *inner = rh_str_extension(stem);
    gchar *result = NULL;
    if (inner != NULL) {
        result = g_strconcat(inner, last, NULL);
        g_free(inner);
    }
    g_free(stem);
    g_free(last);
    return result;
}

GPtrArray *rh_platform_candidates_for_file(const RhPlatformRules *rules,
                                           const char *filename)
{
    GPtrArray *out = g_ptr_array_new();
    if (rules == NULL || filename == NULL)
        return out;

    gchar *last = rh_str_extension(filename);
    if (last == NULL)
        return out;

    gchar *compound = compound_suffix(filename);
    for (guint i = 0; i < rules->platforms->len; i++) {
        RhPlatform *platform = g_ptr_array_index(rules->platforms, i);
        if (suffix_in(platform->suffixes, last) ||
            (compound != NULL && suffix_in(platform->suffixes, compound)))
            g_ptr_array_add(out, platform);
    }

    g_free(compound);
    g_free(last);
    return out;
}

gboolean rh_platform_matches_file(const RhPlatform *platform, const char *filename)
{
    if (platform == NULL || filename == NULL)
        return FALSE;

    gchar *last = rh_str_extension(filename);
    if (last == NULL)
        return FALSE;

    gchar *compound = compound_suffix(filename);
    gboolean match = suffix_in(platform->suffixes, last) ||
                     (compound != NULL && suffix_in(platform->suffixes, compound));
    g_free(compound);
    g_free(last);
    return match;
}

gboolean rh_platform_is_archive(const RhPlatformRules *rules, const char *filename)
{
    if (rules == NULL || filename == NULL)
        return FALSE;
    gchar *ext = rh_str_extension(filename);
    gboolean yes = ext != NULL && suffix_in(rules->archive_suffixes, ext);
    g_free(ext);
    return yes;
}

gboolean rh_platform_is_arcade_rom_member(const RhPlatformRules *rules, const char *filename)
{
    if (rules == NULL || filename == NULL)
        return FALSE;
    gchar *ext = rh_str_extension(filename);
    gboolean yes = ext != NULL && suffix_in(rules->arcade_suffixes, ext);
    g_free(ext);
    return yes;
}

const gchar *rh_platform_media_dir(const RhPlatformRules *rules, const char *kind)
{
    if (rules == NULL || kind == NULL)
        return NULL;
    return g_hash_table_lookup(rules->media_dirs, kind);
}

gboolean rh_platform_media_suffix_allowed(const RhPlatformRules *rules,
                                          const char *kind, const char *filename)
{
    if (rules == NULL || kind == NULL || filename == NULL)
        return FALSE;
    GPtrArray *suffixes = g_hash_table_lookup(rules->media_suffixes, kind);
    if (suffixes == NULL)
        return FALSE;
    gchar *ext = rh_str_extension(filename);
    gboolean yes = ext != NULL && suffix_in(suffixes, ext);
    g_free(ext);
    return yes;
}
