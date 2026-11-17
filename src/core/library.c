#include "library.h"

#include <glib/gstdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "rh-str.h"

#define RH_LIBRARY_DOMAIN "romham/library"

void rh_library_entry_free(RhLibraryEntry *entry)
{
    if (entry == NULL)
        return;
    g_free(entry->name);
    g_free(entry->path);
    g_free(entry->description);
    g_free(entry->developer);
    g_free(entry->publisher);
    g_free(entry->genre);
    g_free(entry->region);
    g_free(entry->image);
    g_free(entry->video);
    g_free(entry->marquee);
    g_free(entry->platform_hint);
    g_free(entry->source);
    g_free(entry);
}

void rh_library_entry_free_func(gpointer data)
{
    rh_library_entry_free(data);
}

static RhLibraryEntry *entry_new(const char *platform_hint, const char *source)
{
    RhLibraryEntry *entry = g_new0(RhLibraryEntry, 1);
    entry->platform_hint = g_strdup(platform_hint);
    entry->source = g_strdup(source);
    return entry;
}

/* 去掉清单里常见的 "./" 前缀，统一用 '/' */
static gchar *normalize_rel_path(const char *path)
{
    if (path == NULL)
        return NULL;

    gchar *trimmed = g_strstrip(g_strdup(path));
    gchar *result = NULL;
    if (g_str_has_prefix(trimmed, "./"))
        result = g_strdup(trimmed + 2);
    else if (g_str_has_prefix(trimmed, ".\\"))
        result = g_strdup(trimmed + 2);
    else
        result = g_strdup(trimmed);

    for (gchar *p = result; *p != '\0'; p++) {
        if (*p == '\\')
            *p = '/';
    }
    g_free(trimmed);
    return result;
}

/* 取 XML 元素的文本内容；空内容返回 NULL */
static gchar *node_text(xmlNode *node)
{
    if (node == NULL)
        return NULL;
    xmlChar *content = xmlNodeGetContent(node);
    if (content == NULL)
        return NULL;
    gchar *text = g_strstrip(g_strdup((const gchar *)content));
    xmlFree(content);
    if (text[0] == '\0') {
        g_free(text);
        return NULL;
    }
    return text;
}

/* 在 <game> 下按名字找第一个子元素 */
static xmlNode *child_by_name(xmlNode *parent, const char *name)
{
    for (xmlNode *node = parent->children; node != NULL; node = node->next) {
        if (node->type == XML_ELEMENT_NODE &&
            xmlStrcmp(node->name, (const xmlChar *)name) == 0)
            return node;
    }
    return NULL;
}

static gint parse_year(const gchar *value)
{
    if (value == NULL)
        return 0;

    gchar *text = g_strstrip(g_strdup(value));
    gint year = 0;

    /* ES-DE 的 releasedate 是 19890614T000000 这种紧凑格式 */
    if (strlen(text) >= 8) {
        gboolean digits = TRUE;
        for (int i = 0; i < 8; i++) {
            if (!g_ascii_isdigit(text[i])) {
                digits = FALSE;
                break;
            }
        }
        if (digits) {
            gchar *head = g_strndup(text, 4);
            year = (gint)strtol(head, NULL, 10);
            g_free(head);
            g_free(text);
            return year;
        }
    }

    year = (gint)strtol(text, NULL, 10);
    g_free(text);
    return year;
}

static gdouble parse_rating(const gchar *value)
{
    if (value == NULL)
        return 0.0;
    return g_ascii_strtod(value, NULL);
}

static gint parse_players(const gchar *value)
{
    if (value == NULL)
        return 0;
    return (gint)strtol(value, NULL, 10);
}

static void parse_gamelist(const char *file, const char *platform_hint,
                           GPtrArray *out)
{
    xmlDoc *doc = xmlReadFile(file, "UTF-8", XML_PARSE_NONET | XML_PARSE_NOERROR |
                                                    XML_PARSE_NOWARNING);
    if (doc == NULL) {
        g_log(RH_LIBRARY_DOMAIN, G_LOG_LEVEL_WARNING, "无法解析 gamelist: %s", file);
        return;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (root == NULL || xmlStrcmp(root->name, (const xmlChar *)"gameList") != 0) {
        g_log(RH_LIBRARY_DOMAIN, G_LOG_LEVEL_WARNING, "根元素不是 gameList: %s", file);
        xmlFreeDoc(doc);
        return;
    }

    for (xmlNode *node = root->children; node != NULL; node = node->next) {
        if (node->type != XML_ELEMENT_NODE ||
            xmlStrcmp(node->name, (const xmlChar *)"game") != 0)
            continue;

        RhLibraryEntry *entry = entry_new(platform_hint, "gamelist");
        entry->name = node_text(child_by_name(node, "name"));
        gchar *raw_path = node_text(child_by_name(node, "path"));
        entry->path = normalize_rel_path(raw_path);
        g_free(raw_path);
        entry->description = node_text(child_by_name(node, "desc"));
        entry->developer = node_text(child_by_name(node, "developer"));
        entry->publisher = node_text(child_by_name(node, "publisher"));
        entry->genre = node_text(child_by_name(node, "genre"));
        entry->region = node_text(child_by_name(node, "region"));
        gchar *year = node_text(child_by_name(node, "releasedate"));
        entry->year = parse_year(year);
        g_free(year);
        gchar *players = node_text(child_by_name(node, "players"));
        entry->players = parse_players(players);
        g_free(players);
        gchar *rating = node_text(child_by_name(node, "rating"));
        entry->rating = parse_rating(rating);
        g_free(rating);
        gchar *image = node_text(child_by_name(node, "image"));
        entry->image = normalize_rel_path(image);
        g_free(image);
        gchar *video = node_text(child_by_name(node, "video"));
        entry->video = normalize_rel_path(video);
        g_free(video);
        gchar *marquee = node_text(child_by_name(node, "marquee"));
        entry->marquee = normalize_rel_path(marquee);
        g_free(marquee);

        if (entry->path == NULL && entry->name == NULL) {
            rh_library_entry_free(entry);
            continue;
        }
        g_ptr_array_add(out, entry);
    }

    xmlFreeDoc(doc);
}

/* Pegasus 的键名；续行靠"不含这些前缀"判断 */
static gboolean is_pegasus_key(const char *line)
{
    static const char *const keys[] = {
        "game:", "file:", "description:", "developer:", "publisher:", "genre:",
        "players:", "release:", "rating:", "assets.boxFront:", "assets.screenshot:",
        "assets.video:", "assets.marquee:", "assets.logo:", "assets.background:"
    };
    for (gsize i = 0; i < G_N_ELEMENTS(keys); i++) {
        if (g_str_has_prefix(line, keys[i]))
            return TRUE;
    }
    return FALSE;
}

static void pegasus_commit(GPtrArray *out, RhLibraryEntry **current)
{
    if (*current == NULL)
        return;
    if ((*current)->path == NULL && (*current)->name == NULL)
        rh_library_entry_free(*current);
    else
        g_ptr_array_add(out, *current);
    *current = NULL;
}

static void parse_pegasus(const char *file, const char *platform_hint, GPtrArray *out)
{
    gchar *content = NULL;
    if (!g_file_get_contents(file, &content, NULL, NULL)) {
        g_log(RH_LIBRARY_DOMAIN, G_LOG_LEVEL_WARNING, "无法读取 Pegasus 清单: %s", file);
        return;
    }

    RhLibraryEntry *current = NULL;
    gchar **lines = g_strsplit(content, "\n", -1);
    for (int i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strchomp(lines[i]);
        if (line[0] == '\0' || line[0] == '#') {
            pegasus_commit(out, &current);
            continue;
        }

        if (!is_pegasus_key(line)) {
            /* 续行：拼到描述后面 */
            if (current != NULL && current->description != NULL) {
                gchar *joined = g_strconcat(current->description, "\n", line, NULL);
                g_free(current->description);
                current->description = joined;
            }
            continue;
        }

        if (g_str_has_prefix(line, "game:")) {
            pegasus_commit(out, &current);
            current = entry_new(platform_hint, "pegasus");
            current->name = g_strstrip(g_strdup(line + strlen("game:")));
            continue;
        }

        if (current == NULL)
            continue;

        if (g_str_has_prefix(line, "file:")) {
            gchar *raw = g_strstrip(g_strdup(line + strlen("file:")));
            g_free(current->path);
            current->path = normalize_rel_path(raw);
            g_free(raw);
        } else if (g_str_has_prefix(line, "description:")) {
            g_free(current->description);
            current->description = g_strstrip(g_strdup(line + strlen("description:")));
        } else if (g_str_has_prefix(line, "developer:")) {
            current->developer = g_strstrip(g_strdup(line + strlen("developer:")));
        } else if (g_str_has_prefix(line, "publisher:")) {
            current->publisher = g_strstrip(g_strdup(line + strlen("publisher:")));
        } else if (g_str_has_prefix(line, "genre:")) {
            current->genre = g_strstrip(g_strdup(line + strlen("genre:")));
        } else if (g_str_has_prefix(line, "release:")) {
            current->year = parse_year(line + strlen("release:"));
        } else if (g_str_has_prefix(line, "players:")) {
            current->players = parse_players(line + strlen("players:"));
        } else if (g_str_has_prefix(line, "rating:")) {
            current->rating = parse_rating(line + strlen("rating:"));
        } else if (g_str_has_prefix(line, "assets.boxFront:")) {
            gchar *raw = g_strstrip(g_strdup(line + strlen("assets.boxFront:")));
            g_free(current->image);
            current->image = normalize_rel_path(raw);
            g_free(raw);
        } else if (g_str_has_prefix(line, "assets.screenshot:")) {
            gchar *raw = g_strstrip(g_strdup(line + strlen("assets.screenshot:")));
            g_free(current->image);
            current->image = normalize_rel_path(raw);
            g_free(raw);
        } else if (g_str_has_prefix(line, "assets.video:")) {
            gchar *raw = g_strstrip(g_strdup(line + strlen("assets.video:")));
            g_free(current->video);
            current->video = normalize_rel_path(raw);
            g_free(raw);
        } else if (g_str_has_prefix(line, "assets.marquee:")) {
            gchar *raw = g_strstrip(g_strdup(line + strlen("assets.marquee:")));
            g_free(current->marquee);
            current->marquee = normalize_rel_path(raw);
            g_free(raw);
        }
    }
    pegasus_commit(out, &current);

    g_strfreev(lines);
    g_free(content);
}

GPtrArray *rh_library_scan(const char *root, RhErr *out_err)
{
    GPtrArray *entries = g_ptr_array_new_with_free_func(rh_library_entry_free_func);
    if (out_err != NULL)
        *out_err = RH_OK;
    if (root == NULL || root[0] == '\0') {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return entries;
    }

    GDir *dir = g_dir_open(root, 0, NULL);
    if (dir == NULL) {
        if (out_err != NULL)
            *out_err = RH_ERR_IO;
        return entries;
    }

    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *subdir = g_build_filename(root, name, NULL);
        if (!g_file_test(subdir, G_FILE_TEST_IS_DIR)) {
            g_free(subdir);
            continue;
        }

        gchar *gamelist = g_build_filename(subdir, "gamelist.xml", NULL);
        gchar *pegasus = g_build_filename(subdir, "metadata.pegasus.txt", NULL);

        if (g_file_test(gamelist, G_FILE_TEST_IS_REGULAR))
            parse_gamelist(gamelist, name, entries);
        else if (g_file_test(pegasus, G_FILE_TEST_IS_REGULAR))
            parse_pegasus(pegasus, name, entries);

        g_free(gamelist);
        g_free(pegasus);
        g_free(subdir);
    }
    g_dir_close(dir);

    return entries;
}
