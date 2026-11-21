#include "export.h"

#include <glib/gstdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rh-records.h"
#include "rh-str.h"

#define RH_EXPORT_DOMAIN "romham/export"

/* ===========================================================================
 * 模板节点
 * ========================================================================= */

typedef enum { NODE_TEXT, NODE_VAR, NODE_IF, NODE_FOR } NodeKind;

typedef struct RhNode {
    NodeKind   kind;
    gchar     *text;   /* TEXT 内容 / VAR 名 / IF、FOR 的条件名 */
    gchar     *mod;    /* VAR 转义修饰：""/xml/json/text */
    GPtrArray *children;      /* IF 的 then 或 FOR 的 body */
    GPtrArray *else_children; /* IF 的 else */
} RhNode;

static RhNode *node_new(NodeKind kind)
{
    RhNode *node = g_new0(RhNode, 1);
    node->kind = kind;
    node->children = g_ptr_array_new();
    return node;
}

static void node_free(RhNode *node)
{
    if (node == NULL)
        return;
    g_free(node->text);
    g_free(node->mod);
    if (node->children != NULL) {
        for (guint i = 0; i < node->children->len; i++)
            node_free(g_ptr_array_index(node->children, i));
        g_ptr_array_unref(node->children);
    }
    if (node->else_children != NULL) {
        for (guint i = 0; i < node->else_children->len; i++)
            node_free(g_ptr_array_index(node->else_children, i));
        g_ptr_array_unref(node->else_children);
    }
    g_free(node);
}

static void node_free_func(gpointer data)
{
    node_free(data);
}

static void gstring_free_func(gpointer data)
{
    g_string_free((GString *)data, TRUE);
}

/* 前端清单里的路径必须用 '/'（Windows 的 g_build_filename 给 '\\'）。 */
static gchar *to_forward_slashes(gchar *path)
{
    for (gchar *p = path; p != NULL && *p != '\0'; p++) {
        if (*p == '\\')
            *p = '/';
    }
    return path;
}

/* ===========================================================================
 * 规格
 * ========================================================================= */

struct RhExportSpec {
    gchar      *path;
    gchar      *id;
    gchar      *name;
    gchar      *escape;
    gchar      *layout_rom;
    gchar      *manifest;
    gchar      *media_dir;
    gchar      *launch;
    GHashTable *media_map; /* kind -> 目标路径模板 */
    GPtrArray  *header;
    GPtrArray  *game;
    GPtrArray  *footer;
};

static const char *const rh_export_vars[] = {
    "id", "name", "game_main_name", "platform", "platform_export_name",
    "description", "description_line", "developer", "publisher", "year",
    "genre", "players", "rating", "rating_norm", "releasedate_es", "region",
    "tags", "disc_no", "is_multi_disc", "is_arcade", "rom_status",
    "rom_format", "dat_source", "patch_type",
    "rom_file", "rom_rel", "rom_size", "cover_rel", "screenshot_rel",
    "marquee_rel", "video_rel", "miximage_rel", "intro_rel", "patcher_rel",
    "export_dir", "platform_dir", "platform_name", "media_dir", "game_name",
    "platform_extensions", "emulator_launch",
    "rel", "filename", "index", "kind"
};

static const char *const rh_export_loops[] = {
    "covers", "screenshots", "videos", "marquees", "miximages", "intro",
    "patcher", "tags", "roms"
};

static gboolean var_known(const char *name)
{
    for (guint i = 0; i < G_N_ELEMENTS(rh_export_vars); i++) {
        if (g_strcmp0(name, rh_export_vars[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

static gboolean loop_known(const char *name)
{
    for (guint i = 0; i < G_N_ELEMENTS(rh_export_loops); i++) {
        if (g_strcmp0(name, rh_export_loops[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

static gboolean mod_known(const char *mod)
{
    return mod == NULL || mod[0] == '\0' || g_strcmp0(mod, "xml") == 0 ||
           g_strcmp0(mod, "json") == 0 || g_strcmp0(mod, "text") == 0;
}

typedef struct {
    RhNode   *node;
    gboolean  in_else;
} CompileFrame;

static guint line_at(const char *text, gsize offset, guint base_line)
{
    guint line = base_line;
    for (gsize i = 0; i < offset && text[i] != '\0'; i++) {
        if (text[i] == '\n')
            line++;
    }
    return line;
}

static gchar *compile_error(const char *path, guint line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    gchar *detail = g_strdup_vprintf(fmt, args);
    va_end(args);
    gchar *message = g_strdup_printf("%s:%u: %s", path, line, detail);
    g_free(detail);
    return message;
}

static GPtrArray *compile_template(const char *path, const char *text,
                                   guint base_line, gchar **out_message)
{
    GPtrArray *root = g_ptr_array_new_with_free_func(node_free_func);
    GPtrArray *stack = g_ptr_array_new(); /* CompileFrame* */
    GString *buffer = g_string_new(NULL);

    gsize i = 0;
    gboolean ok = TRUE;
    while (ok && text[i] != '\0') {
        if (text[i] != '%') {
            g_string_append_c(buffer, text[i]);
            i++;
            continue;
        }

        const char *close = strchr(text + i + 1, '%');
        if (close == NULL) {
            *out_message = compile_error(path, line_at(text, i, base_line),
                                         "模板标签缺少结束的 '%%'");
            ok = FALSE;
            break;
        }

        gsize tag_start = i;
        gsize tag_len = (gsize)(close - (text + i + 1));
        gchar *tag = g_strndup(text + i + 1, tag_len);
        g_strstrip(tag);
        guint line = line_at(text, tag_start, base_line);

        /* 先把累积的文本节点落盘 */
        if (buffer->len > 0) {
            RhNode *node = node_new(NODE_TEXT);
            node->text = g_strdup(buffer->str);
            g_string_truncate(buffer, 0);
            CompileFrame *top = stack->len > 0
                ? g_ptr_array_index(stack, stack->len - 1) : NULL;
            GPtrArray *target = root;
            if (top != NULL)
                target = top->in_else ? top->node->else_children
                                      : top->node->children;
            g_ptr_array_add(target, node);
        }

        if (g_str_has_prefix(tag, "if:")) {
            if (stack->len >= 3) {
                *out_message = compile_error(path, line, "条件嵌套超过 3 层");
                ok = FALSE;
            } else {
                const char *name = tag + 3;
                if (!var_known(name)) {
                    *out_message = compile_error(path, line,
                                                 "未知变量 %s", name);
                    ok = FALSE;
                } else {
                    RhNode *node = node_new(NODE_IF);
                    node->text = g_strdup(name);
                    node->else_children = g_ptr_array_new();
                    CompileFrame *top = stack->len > 0
                        ? g_ptr_array_index(stack, stack->len - 1) : NULL;
                    GPtrArray *target = root;
                    if (top != NULL)
                        target = top->in_else ? top->node->else_children
                                              : top->node->children;
                    g_ptr_array_add(target, node);

                    CompileFrame *frame = g_new0(CompileFrame, 1);
                    frame->node = node;
                    g_ptr_array_add(stack, frame);
                }
            }
        } else if (g_strcmp0(tag, "else") == 0) {
            CompileFrame *top = stack->len > 0
                ? g_ptr_array_index(stack, stack->len - 1) : NULL;
            if (top == NULL || top->node->kind != NODE_IF || top->in_else) {
                *out_message = compile_error(path, line, "孤立的 %%else%%");
                ok = FALSE;
            } else {
                top->in_else = TRUE;
            }
        } else if (g_strcmp0(tag, "endif") == 0) {
            CompileFrame *top = stack->len > 0
                ? g_ptr_array_index(stack, stack->len - 1) : NULL;
            if (top == NULL || top->node->kind != NODE_IF) {
                *out_message = compile_error(path, line, "孤立的 %%endif%%");
                ok = FALSE;
            } else {
                g_free(g_ptr_array_steal_index(stack, stack->len - 1));
            }
        } else if (g_str_has_prefix(tag, "for:")) {
            const char *name = tag + 4;
            if (stack->len >= 3) {
                *out_message = compile_error(path, line, "循环嵌套超过 3 层");
                ok = FALSE;
            } else if (!loop_known(name)) {
                *out_message = compile_error(path, line,
                                             "未知循环集合 %s", name);
                ok = FALSE;
            } else {
                RhNode *node = node_new(NODE_FOR);
                node->text = g_strdup(name);
                CompileFrame *top = stack->len > 0
                    ? g_ptr_array_index(stack, stack->len - 1) : NULL;
                GPtrArray *target = root;
                if (top != NULL)
                    target = top->in_else ? top->node->else_children
                                          : top->node->children;
                g_ptr_array_add(target, node);

                CompileFrame *frame = g_new0(CompileFrame, 1);
                frame->node = node;
                g_ptr_array_add(stack, frame);
            }
        } else if (g_strcmp0(tag, "endfor") == 0) {
            CompileFrame *top = stack->len > 0
                ? g_ptr_array_index(stack, stack->len - 1) : NULL;
            if (top == NULL || top->node->kind != NODE_FOR) {
                *out_message = compile_error(path, line, "孤立的 %%endfor%%");
                ok = FALSE;
            } else {
                g_free(g_ptr_array_steal_index(stack, stack->len - 1));
            }
        } else {
            gchar **parts = g_strsplit(tag, ":", 2);
            gchar *name = parts[0];
            gchar *mod = parts[1];
            g_strstrip(name);
            if (mod != NULL)
                g_strstrip(mod);
            const char *mod_value = mod != NULL ? mod : "";
            if (!var_known(name)) {
                *out_message = compile_error(path, line, "未知变量 %s", name);
                ok = FALSE;
            } else if (!mod_known(mod_value)) {
                *out_message = compile_error(path, line,
                                             "未知转义修饰 %s", mod_value);
                ok = FALSE;
            } else {
                RhNode *node = node_new(NODE_VAR);
                node->text = g_strdup(name);
                node->mod = g_strdup(mod_value);
                CompileFrame *top = stack->len > 0
                    ? g_ptr_array_index(stack, stack->len - 1) : NULL;
                GPtrArray *target = root;
                if (top != NULL)
                    target = top->in_else ? top->node->else_children
                                          : top->node->children;
                g_ptr_array_add(target, node);
            }
            g_strfreev(parts);
        }

        g_free(tag);
        i = (gsize)(close - text) + 1;
    }

    if (ok && stack->len > 0) {
        CompileFrame *top = g_ptr_array_index(stack, stack->len - 1);
        *out_message = compile_error(path, base_line,
                                     top->node->kind == NODE_IF
                                         ? "%%if%% 缺少 %%endif%%"
                                         : "%%for%% 缺少 %%endfor%%");
        ok = FALSE;
    }
    if (ok && buffer->len > 0) {
        RhNode *node = node_new(NODE_TEXT);
        node->text = g_strdup(buffer->str);
        g_ptr_array_add(root, node);
    }

    for (guint i = 0; i < stack->len; i++)
        g_free(g_ptr_array_index(stack, i));
    g_ptr_array_unref(stack);
    g_string_free(buffer, TRUE);

    if (!ok) {
        g_ptr_array_unref(root);
        return NULL;
    }
    return root;
}

static gchar *strip_comment(const gchar *line)
{
    const gchar *semi = strchr(line, ';');
    if (semi == NULL)
        return g_strdup(line);
    return g_strndup(line, (gsize)(semi - line));
}

static void set_key(gchar **slot, const gchar *value)
{
    g_free(*slot);
    *slot = g_strdup(value);
}

RhExportSpec *rh_export_spec_load(const char *path, RhErr *out_err,
                                  gchar **out_message)
{
    if (out_err != NULL)
        *out_err = RH_OK;
    if (out_message != NULL)
        *out_message = NULL;
    if (path == NULL || path[0] == '\0') {
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return NULL;
    }

    gchar *content = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &content, &length, NULL)) {
        if (out_err != NULL)
            *out_err = RH_ERR_IO;
        if (out_message != NULL)
            *out_message = g_strdup_printf("%s: 无法读取规格文件", path);
        return NULL;
    }

    RhExportSpec *spec = g_new0(RhExportSpec, 1);
    spec->path = g_strdup(path);
    spec->escape = g_strdup("text");
    spec->media_dir = g_strdup("%platform%/media");
    spec->media_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    spec->header = g_ptr_array_new_with_free_func(node_free_func);
    spec->game = g_ptr_array_new_with_free_func(node_free_func);
    spec->footer = g_ptr_array_new_with_free_func(node_free_func);

    GString *raw = g_string_new(NULL);
    guint raw_start = 0;
    enum { SECT_NONE, SECT_FRONTEND, SECT_HEADER, SECT_GAME, SECT_FOOTER,
           SECT_IMPORT, SECT_MEDIA } section = SECT_NONE;
    gchar *spec_version = NULL;
    gboolean ok = TRUE;

    gchar **lines = g_strsplit(content, "\n", -1);
    for (guint n = 0; lines[n] != NULL && ok; n++) {
        guint line_no = n + 1;
        const gchar *line = lines[n];

        if (line[0] == '[') {
            /* 切换段：先把上一个原始段编译掉 */
            if (section == SECT_HEADER || section == SECT_GAME ||
                section == SECT_FOOTER) {
                GPtrArray **slot = section == SECT_HEADER ? &spec->header
                                 : section == SECT_GAME ? &spec->game
                                                        : &spec->footer;
                /* 先清空占位数组，编译结果替换 */
                g_ptr_array_unref(*slot);
                *slot = compile_template(path, raw->str, raw_start,
                                         out_message);
                if (*slot == NULL)
                    ok = FALSE;
                g_string_truncate(raw, 0);
            }

            if (g_strcmp0(line, "[frontend]") == 0)
                section = SECT_FRONTEND;
            else if (g_strcmp0(line, "[export.header]") == 0) {
                section = SECT_HEADER;
                raw_start = line_no + 1;
            } else if (g_strcmp0(line, "[export.game]") == 0) {
                section = SECT_GAME;
                raw_start = line_no + 1;
            } else if (g_strcmp0(line, "[export.footer]") == 0) {
                section = SECT_FOOTER;
                raw_start = line_no + 1;
            } else if (g_strcmp0(line, "[import]") == 0)
                section = SECT_IMPORT;
            else if (g_strcmp0(line, "[export.media]") == 0)
                section = SECT_MEDIA;
            else
                section = SECT_NONE;
            continue;
        }

        if (section == SECT_HEADER || section == SECT_GAME ||
            section == SECT_FOOTER) {
            g_string_append(raw, line);
            g_string_append_c(raw, '\n');
            continue;
        }

        if (section == SECT_FRONTEND || section == SECT_MEDIA) {
            gchar *clean = strip_comment(line);
            gchar *trimmed = g_strstrip(clean);
            if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
                g_free(clean);
                continue;
            }
            gchar *eq = strchr(trimmed, '=');
            if (eq == NULL) {
                if (out_message != NULL)
                    *out_message = compile_error(path, line_no,
                                                 "缺少 '=' 的键值行");
                ok = FALSE;
                g_free(clean);
                continue;
            }
            *eq = '\0';
            gchar *key = g_strstrip(trimmed);
            gchar *value = g_strstrip(eq + 1);

            if (section == SECT_FRONTEND) {
                if (g_strcmp0(key, "id") == 0)
                    set_key(&spec->id, value);
                else if (g_strcmp0(key, "name") == 0)
                    set_key(&spec->name, value);
                else if (g_strcmp0(key, "spec_version") == 0)
                    set_key(&spec_version, value);
                else if (g_strcmp0(key, "escape") == 0)
                    set_key(&spec->escape, value);
                else if (g_strcmp0(key, "layout_rom") == 0)
                    set_key(&spec->layout_rom, value);
                else if (g_strcmp0(key, "manifest") == 0)
                    set_key(&spec->manifest, value);
                else if (g_strcmp0(key, "media_dir") == 0)
                    set_key(&spec->media_dir, value);
                else if (g_strcmp0(key, "launch") == 0)
                    set_key(&spec->launch, value);
            } else {
                g_hash_table_insert(spec->media_map, g_strdup(key),
                                    g_strdup(value));
            }
            g_free(clean);
        }
    }

    /* 文件末尾仍在原始段 */
    if (ok && (section == SECT_HEADER || section == SECT_GAME ||
               section == SECT_FOOTER)) {
        GPtrArray **slot = section == SECT_HEADER ? &spec->header
                             : section == SECT_GAME ? &spec->game
                                                    : &spec->footer;
        g_ptr_array_unref(*slot);
        *slot = compile_template(path, raw->str, raw_start, out_message);
        if (*slot == NULL)
            ok = FALSE;
    }

    g_strfreev(lines);
    g_string_free(raw, TRUE);
    g_free(content);

    if (ok) {
        if (spec->id == NULL || spec->id[0] == '\0') {
            if (out_message != NULL)
                *out_message = g_strdup_printf("%s: [frontend] 缺少 id", path);
            ok = FALSE;
        } else if (spec_version != NULL && g_strcmp0(spec_version, "1") != 0) {
            if (out_message != NULL)
                *out_message = g_strdup_printf("%s: 不支持的 spec_version=%s",
                                               path, spec_version);
            ok = FALSE;
        } else if (spec->manifest == NULL || spec->manifest[0] == '\0') {
            if (out_message != NULL)
                *out_message = g_strdup_printf("%s: [frontend] 缺少 manifest", path);
            ok = FALSE;
        } else if (spec->layout_rom == NULL || spec->layout_rom[0] == '\0') {
            if (out_message != NULL)
                *out_message = g_strdup_printf("%s: [frontend] 缺少 layout_rom", path);
            ok = FALSE;
        } else if (spec->game->len == 0) {
            if (out_message != NULL)
                *out_message = g_strdup_printf("%s: [export.game] 为空", path);
            ok = FALSE;
        } else if (!mod_known(spec->escape)) {
            if (out_message != NULL)
                *out_message = g_strdup_printf("%s: 未知 escape=%s",
                                               path, spec->escape);
            ok = FALSE;
        }
    }

    g_free(spec_version);
    if (!ok) {
        rh_export_spec_free(spec);
        if (out_err != NULL)
            *out_err = RH_ERR_INVALID;
        return NULL;
    }
    if (out_err != NULL)
        *out_err = RH_OK;
    return spec;
}

void rh_export_spec_free(RhExportSpec *spec)
{
    if (spec == NULL)
        return;
    g_free(spec->path);
    g_free(spec->id);
    g_free(spec->name);
    g_free(spec->escape);
    g_free(spec->layout_rom);
    g_free(spec->manifest);
    g_free(spec->media_dir);
    g_free(spec->launch);
    if (spec->media_map != NULL)
        g_hash_table_unref(spec->media_map);
    if (spec->header != NULL)
        g_ptr_array_unref(spec->header);
    if (spec->game != NULL)
        g_ptr_array_unref(spec->game);
    if (spec->footer != NULL)
        g_ptr_array_unref(spec->footer);
    g_free(spec);
}

const char *rh_export_spec_id(const RhExportSpec *spec)
{
    return spec != NULL ? spec->id : NULL;
}

const char *rh_export_spec_name(const RhExportSpec *spec)
{
    return spec != NULL ? spec->name : NULL;
}

/* ===========================================================================
 * 渲染
 * ========================================================================= */

typedef struct {
    GHashTable *vars;   /* gchar* -> gchar* */
    GHashTable *arrays; /* gchar* -> GPtrArray<GHashTable*>；清单头尾为 NULL */
    const char *default_mod; /* 规格里的 escape，变量无修饰时使用 */
} RenderCtx;

static GString *escape_append(GString *out, const char *value, const char *mod)
{
    if (value == NULL)
        return out;

    if (g_strcmp0(mod, "xml") == 0) {
        for (const char *p = value; *p != '\0'; p++) {
            switch (*p) {
            case '&': g_string_append(out, "&amp;"); break;
            case '<': g_string_append(out, "&lt;"); break;
            case '>': g_string_append(out, "&gt;"); break;
            case '"': g_string_append(out, "&quot;"); break;
            case '\'': g_string_append(out, "&apos;"); break;
            default: g_string_append_c(out, *p);
            }
        }
    } else if (g_strcmp0(mod, "json") == 0) {
        for (const char *p = value; *p != '\0'; p++) {
            switch (*p) {
            case '"': g_string_append(out, "\\\""); break;
            case '\\': g_string_append(out, "\\\\"); break;
            case '\n': g_string_append(out, "\\n"); break;
            case '\r': g_string_append(out, "\\r"); break;
            case '\t': g_string_append(out, "\\t"); break;
            default:
                if ((guchar)*p < 0x20)
                    g_string_append_printf(out, "\\u%04x", (guchar)*p);
                else
                    g_string_append_c(out, *p);
            }
        }
    } else {
        g_string_append(out, value);
    }
    return out;
}

static gboolean truthy(const char *value)
{
    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

static void render_nodes(const GPtrArray *nodes, RenderCtx *ctx, GString *out);

static void render_node(const RhNode *node, RenderCtx *ctx, GString *out)
{
    switch (node->kind) {
    case NODE_TEXT:
        g_string_append(out, node->text);
        break;
    case NODE_VAR:
        escape_append(out, g_hash_table_lookup(ctx->vars, node->text),
                      (node->mod != NULL && node->mod[0] != '\0')
                          ? node->mod : ctx->default_mod);
        break;
    case NODE_IF: {
        const char *value = g_hash_table_lookup(ctx->vars, node->text);
        if (truthy(value))
            render_nodes(node->children, ctx, out);
        else if (node->else_children != NULL)
            render_nodes(node->else_children, ctx, out);
        break;
    }
    case NODE_FOR: {
        GPtrArray *items = ctx->arrays != NULL
            ? g_hash_table_lookup(ctx->arrays, node->text) : NULL;
        if (items == NULL)
            break;
        for (guint i = 0; i < items->len; i++) {
            GHashTable *item = g_ptr_array_index(items, i);
            gchar *old_rel = g_strdup(g_hash_table_lookup(ctx->vars, "rel"));
            gchar *old_file = g_strdup(g_hash_table_lookup(ctx->vars, "filename"));
            gchar *old_index = g_strdup(g_hash_table_lookup(ctx->vars, "index"));
            gchar *old_kind = g_strdup(g_hash_table_lookup(ctx->vars, "kind"));

            g_hash_table_insert(ctx->vars, g_strdup("rel"),
                                g_strdup(g_hash_table_lookup(item, "rel")));
            g_hash_table_insert(ctx->vars, g_strdup("filename"),
                                g_strdup(g_hash_table_lookup(item, "filename")));
            g_hash_table_insert(ctx->vars, g_strdup("index"),
                                g_strdup(g_hash_table_lookup(item, "index")));
            g_hash_table_insert(ctx->vars, g_strdup("kind"),
                                g_strdup(g_hash_table_lookup(item, "kind")));

            render_nodes(node->children, ctx, out);

            g_hash_table_insert(ctx->vars, g_strdup("rel"), old_rel);
            g_hash_table_insert(ctx->vars, g_strdup("filename"), old_file);
            g_hash_table_insert(ctx->vars, g_strdup("index"), old_index);
            g_hash_table_insert(ctx->vars, g_strdup("kind"), old_kind);
        }
        break;
    }
    }
}

static void render_nodes(const GPtrArray *nodes, RenderCtx *ctx, GString *out)
{
    for (guint i = 0; i < nodes->len; i++)
        render_node(g_ptr_array_index(nodes, i), ctx, out);
}

/* ===========================================================================
 * 导出执行
 * ========================================================================= */

static void set_var(GHashTable *vars, const char *key, const gchar *value)
{
    g_hash_table_insert(vars, g_strdup(key), g_strdup(value != NULL ? value : ""));
}

static void set_var_take(GHashTable *vars, const char *key, gchar *value)
{
    g_hash_table_insert(vars, g_strdup(key), value != NULL ? value : g_strdup(""));
}

static void set_int_var(GHashTable *vars, const char *key, gint64 value)
{
    if (value > 0)
        set_var_take(vars, key, g_strdup_printf("%" G_GINT64_FORMAT, value));
    else
        set_var(vars, key, "");
}

static gchar *expand_tokens(const char *pattern, GHashTable *vars)
{
    if (pattern == NULL)
        return g_strdup("");
    GString *out = g_string_new(NULL);
    gsize i = 0;
    while (pattern[i] != '\0') {
        if (pattern[i] != '%') {
            g_string_append_c(out, pattern[i]);
            i++;
            continue;
        }
        const char *close = strchr(pattern + i + 1, '%');
        if (close == NULL) {
            g_string_append(out, pattern + i);
            break;
        }
        gchar *name = g_strndup(pattern + i + 1, (gsize)(close - (pattern + i + 1)));
        const char *value = g_hash_table_lookup(vars, name);
        if (value != NULL)
            g_string_append(out, value);
        else
            g_string_append_len(out, pattern + i,
                                (gsize)(close - (pattern + i)) + 1);
        g_free(name);
        i = (gsize)(close - pattern) + 1;
    }
    return g_string_free(out, FALSE);
}

static RhErr ensure_parent(const char *path)
{
    gchar *dir = g_path_get_dirname(path);
    int rc = g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    return rc == 0 ? RH_OK : RH_ERR_IO;
}

static RhErr copy_file(const char *src, const char *dst)
{
    if (g_strcmp0(src, dst) == 0)
        return RH_OK;
    if (ensure_parent(dst) != RH_OK)
        return RH_ERR_IO;

    FILE *in = g_fopen(src, "rb");
    if (in == NULL)
        return RH_ERR_IO;
    FILE *out = g_fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return RH_ERR_IO;
    }

    RhErr err = RH_OK;
    char buffer[64 * 1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof buffer, in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            err = RH_ERR_IO;
            break;
        }
    }
    if (ferror(in))
        err = RH_ERR_IO;

    fclose(in);
    if (fclose(out) != 0 && err == RH_OK)
        err = RH_ERR_IO;
    return err;
}

static const char *media_prefix(const char *kind)
{
    if (g_strcmp0(kind, "covers") == 0) return "cover";
    if (g_strcmp0(kind, "screenshots") == 0) return "screenshot";
    if (g_strcmp0(kind, "videos") == 0) return "video";
    if (g_strcmp0(kind, "marquees") == 0) return "marquee";
    if (g_strcmp0(kind, "miximages") == 0) return "miximage";
    if (g_strcmp0(kind, "intro") == 0) return "intro";
    if (g_strcmp0(kind, "patcher") == 0) return "patcher";
    return kind;
}

static gchar *join_tags(const GPtrArray *tags)
{
    GString *joined = g_string_new(NULL);
    for (guint i = 0; tags != NULL && i < tags->len; i++) {
        if (i > 0)
            g_string_append_c(joined, ',');
        g_string_append(joined, g_ptr_array_index(tags, i));
    }
    return g_string_free(joined, FALSE);
}

static gchar *description_line(const char *description)
{
    if (description == NULL)
        return g_strdup("");
    gchar *copy = g_strdup(description);
    for (gchar *p = copy; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r')
            *p = ' ';
    }
    return copy;
}

static gchar *platform_extensions(const RhPlatformRules *rules, const char *platform)
{
    if (rules == NULL || platform == NULL)
        return g_strdup("");
    RhPlatform *p = rh_platform_by_id(rules, platform);
    if (p == NULL)
        return g_strdup("");
    GString *out = g_string_new(NULL);
    for (guint i = 0; i < p->suffixes->len; i++) {
        if (i > 0)
            g_string_append_c(out, ' ');
        g_string_append(out, g_ptr_array_index(p->suffixes, i));
    }
    return g_string_free(out, FALSE);
}

static void fill_platform_vars(GHashTable *vars, const RhExportSpec *spec,
                               const RhPlatformRules *rules,
                               const RhExportOptions *options,
                               const char *platform)
{
    set_var(vars, "export_dir", options->output_dir);
    set_var(vars, "platform", platform);
    set_var(vars, "platform_dir", platform);
    set_var(vars, "platform_name", platform);
    set_var(vars, "platform_export_name", platform);
    set_var_take(vars, "platform_extensions",
                 platform_extensions(rules, platform));
    set_var_take(vars, "media_dir", expand_tokens(spec->media_dir, vars));
    set_var(vars, "emulator_launch", spec->launch != NULL ? spec->launch : "");
    set_var(vars, "game_name", "");
    set_var(vars, "rom_file", "");
}

/* 按策略给出导出时使用的 ROM 名。display 模式只在能唯一命名时生效：
 * 单 ROM 或多碟（带碟号）游戏；多文件街机包回落到库内文件名。 */
static gchar *export_rom_name(const RhRomFile *rom, const RhGame *game,
                              guint rom_count, RhExportNameMode mode)
{
    if (mode == RH_EXPORT_NAMES_DAT && rom->dat_name != NULL &&
        rom->dat_name[0] != '\0')
        return g_strdup(rom->dat_name);

    if (mode == RH_EXPORT_NAMES_DISPLAY && game->name != NULL &&
        game->name[0] != '\0' && (rom_count == 1 || rom->disc_no > 0)) {
        gchar *base = rh_str_sanitize_component(game->name);
        gchar *ext = rh_str_extension(rom->filename);
        gchar *name = NULL;
        if (rom->disc_no > 0)
            name = g_strdup_printf("%s (Disc %d)%s", base, rom->disc_no,
                                   ext != NULL ? ext : "");
        else
            name = g_strdup_printf("%s%s", base, ext != NULL ? ext : "");
        g_free(ext);
        g_free(base);
        return name;
    }

    return g_strdup(rom->filename);
}

static void add_media_item(GHashTable *arrays, const char *array_name,
                           const char *kind, const char *rel,
                           const char *filename, int index)
{
    GPtrArray *items = g_hash_table_lookup(arrays, array_name);
    if (items == NULL) {
        items = g_ptr_array_new_with_free_func(
            (GDestroyNotify)g_hash_table_unref);
        g_hash_table_insert(arrays, g_strdup(array_name), items);
    }
    GHashTable *item = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert(item, g_strdup("rel"), g_strdup(rel));
    g_hash_table_insert(item, g_strdup("filename"), g_strdup(filename));
    g_hash_table_insert(item, g_strdup("kind"), g_strdup(kind));
    g_hash_table_insert(item, g_strdup("index"),
                        g_strdup_printf("%d", index));
    g_ptr_array_add(items, item);
}

RhErr rh_export_run(RhStore *store, const RhPlatformRules *rules,
                    const char *spec_path, const RhExportOptions *options,
                    RhExportProgressFn progress, gpointer user_data,
                    RhExportStats *out_stats)
{
    if (out_stats != NULL)
        memset(out_stats, 0, sizeof *out_stats);
    if (store == NULL || spec_path == NULL || options == NULL ||
        options->output_dir == NULL)
        return RH_ERR_INVALID;
    if ((options->copy_roms || options->copy_media) && options->library_root == NULL)
        return RH_ERR_INVALID;

    RhErr err = RH_OK;
    gchar *message = NULL;
    RhExportSpec *spec = rh_export_spec_load(spec_path, &err, &message);
    if (spec == NULL) {
        g_log(RH_EXPORT_DOMAIN, G_LOG_LEVEL_WARNING, "导出规格错误: %s",
              message != NULL ? message : "?");
        g_free(message);
        return err;
    }
    g_free(message);

    if (g_mkdir_with_parents(options->output_dir, 0755) != 0) {
        rh_export_spec_free(spec);
        return RH_ERR_IO;
    }

    RhGameFilter filter;
    memset(&filter, 0, sizeof filter);
    filter.limit = -1;
    filter.multi = 0;
    filter.has_patch = 0;
    filter.order_by = "name";

    GPtrArray *games = store_games_list(store, &filter, &err);
    if (err != RH_OK) {
        g_ptr_array_unref(games);
        rh_export_spec_free(spec);
        return err;
    }

    /* platform -> 已渲染的 <game> 块 */
    GHashTable *blocks = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, gstring_free_func);

    for (guint i = 0; i < games->len; i++) {
        RhGame *game = g_ptr_array_index(games, i);

        GHashTable *vars = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, g_free);
        GHashTable *arrays = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free,
            (GDestroyNotify)g_ptr_array_unref);
        RenderCtx ctx = { vars, arrays, spec->escape };

        fill_platform_vars(vars, spec, rules, options, game->platform);
        set_var(vars, "id", game->id);
        set_var(vars, "name", game->name);
        set_var(vars, "game_name", game->name);
        set_var(vars, "game_main_name", game->game_main_name);
        set_var(vars, "description", game->description);
        set_var_take(vars, "description_line",
                     description_line(game->description));
        set_var(vars, "developer", game->developer);
        set_var(vars, "publisher", game->publisher);
        set_int_var(vars, "year", game->year);
        set_var(vars, "genre", game->genre);
        set_int_var(vars, "players", game->players);
        set_int_var(vars, "disc_no", 0);
        set_var(vars, "is_multi_disc", game->is_multi_disc ? "1" : "");
        set_var(vars, "is_arcade", game->is_arcade ? "1" : "");
        set_var(vars, "rom_status", game->rom_status);
        set_var(vars, "rom_format", game->rom_format);
        set_var(vars, "dat_source", game->dat_source);
        set_var(vars, "patch_type", game->patch_type);

        if (game->rating > 0.0) {
            set_var_take(vars, "rating", g_strdup_printf("%.1f", game->rating));
            double norm = game->rating / 10.0;
            if (norm < 0.0) norm = 0.0;
            if (norm > 1.0) norm = 1.0;
            set_var_take(vars, "rating_norm", g_strdup_printf("%.6f", norm));
        } else {
            set_var(vars, "rating", "");
            set_var(vars, "rating_norm", "");
        }
        if (game->year > 0)
            set_var_take(vars, "releasedate_es",
                         g_strdup_printf("%04d0101T000000", game->year));
        else
            set_var(vars, "releasedate_es", "");

        GPtrArray *tags = store_tags_for_game(store, game->id, &err);
        if (err != RH_OK) {
            g_hash_table_unref(vars);
            g_hash_table_unref(arrays);
            if (out_stats != NULL)
                out_stats->errors++;
            continue;
        }
        set_var_take(vars, "tags", join_tags(tags));
        for (guint t = 0; t < tags->len; t++)
            add_media_item(arrays, "tags", "tag",
                           g_ptr_array_index(tags, t),
                           g_ptr_array_index(tags, t), (int)t + 1);
        g_ptr_array_unref(tags);

        GPtrArray *roms = store_roms_for_game(store, game->id, &err);
        if (err != RH_OK) {
            g_hash_table_unref(vars);
            g_hash_table_unref(arrays);
            if (out_stats != NULL)
                out_stats->errors++;
            continue;
        }

        gchar *first_name = roms->len > 0
            ? export_rom_name(g_ptr_array_index(roms, 0), game, roms->len,
                              options->name_mode)
            : g_strdup("");
        const gchar *first_rom = first_name;
        gchar *m3u_name = NULL;
        if (options->generate_m3u && game->is_multi_disc && roms->len > 0) {
            const gchar *base = (game->game_main_name != NULL &&
                                 game->game_main_name[0] != '\0')
                                    ? game->game_main_name : game->name;
            m3u_name = g_strdup_printf("%s.m3u", base);
            first_rom = m3u_name;
        }
        set_var(vars, "rom_file", first_rom);

        gchar *rom_rel = to_forward_slashes(expand_tokens(spec->layout_rom, vars));
        set_var(vars, "rom_rel", rom_rel);
        if (roms->len > 0) {
            RhRomFile *first = g_ptr_array_index(roms, 0);
            set_int_var(vars, "rom_size", first->has_size ? first->size : 0);
            set_int_var(vars, "disc_no", first->disc_no);
        } else {
            set_var(vars, "rom_size", "");
        }

        /* ROM 复制 */
        if (options->copy_roms) {
            for (guint r = 0; r < roms->len; r++) {
                RhRomFile *rom = g_ptr_array_index(roms, r);
                gchar *out_name = export_rom_name(rom, game, roms->len,
                                                  options->name_mode);
                set_var(vars, "rom_file", out_name);
                gchar *rel = expand_tokens(spec->layout_rom, vars);
                gchar *dst = g_build_filename(options->output_dir, rel, NULL);
                gchar *src = g_build_filename(options->library_root,
                                              game->dir_path, rom->filename, NULL);
                RhErr copy_err = copy_file(src, dst);
                if (copy_err == RH_OK && out_stats != NULL)
                    out_stats->roms++;
                else if (out_stats != NULL)
                    out_stats->errors++;
                g_free(src);
                g_free(dst);
                g_free(rel);
                g_free(out_name);
            }
            set_var(vars, "rom_file", first_rom);
        }

        /* m3u */
        if (m3u_name != NULL && options->copy_roms) {
            gchar *m3u_rel = g_strdup_printf("%s/%s", game->platform, m3u_name);
            gchar *dst = g_build_filename(options->output_dir, m3u_rel, NULL);
            if (ensure_parent(dst) == RH_OK) {
                GString *content = g_string_new(NULL);
                for (guint r = 0; r < roms->len; r++) {
                    RhRomFile *rom = g_ptr_array_index(roms, r);
                    gchar *out_name = export_rom_name(rom, game, roms->len,
                                                      options->name_mode);
                    g_string_append(content, out_name);
                    g_string_append_c(content, '\n');
                    g_free(out_name);
                }
                if (!g_file_set_contents(dst, content->str, (gssize)content->len,
                                         NULL) && out_stats != NULL) {
                    out_stats->errors++;
                }
                g_string_free(content, TRUE);
            }
            g_free(dst);
            g_free(m3u_rel);
        }

        /* 媒体复制与 *_rel 变量 */
        GPtrArray *media = store_media_for_game(store, game->id, NULL, &err);
        if (err == RH_OK) {
            gchar *media_dir = g_strdup(g_hash_table_lookup(vars, "media_dir"));
            for (guint m = 0; m < media->len; m++) {
                RhMediaFile *file = g_ptr_array_index(media, m);
                const char *pattern = g_hash_table_lookup(spec->media_map,
                                                          file->kind);
                gchar *base = NULL;
                if (pattern != NULL) {
                    base = expand_tokens(pattern, vars);
                } else {
                    base = g_build_filename(media_dir, file->kind,
                                            game->name, NULL);
                }
                gchar *ext = rh_str_extension(file->filename);
                gchar *target = ext != NULL
                    ? to_forward_slashes(g_strconcat(base, ext, NULL))
                    : to_forward_slashes(g_strdup(base));

                if (options->copy_media) {
                    gchar *dst = g_build_filename(options->output_dir, target, NULL);
                    gchar *src = g_build_filename(options->library_root,
                                                  game->dir_path,
                                                  file->filename, NULL);
                    if (copy_file(src, dst) == RH_OK) {
                        if (out_stats != NULL)
                            out_stats->media++;
                    } else if (out_stats != NULL) {
                        out_stats->errors++;
                    }
                    g_free(src);
                    g_free(dst);
                }

                gchar *rel = NULL;
                gchar *prefix = g_strdup_printf("%s/", game->platform);
                if (g_str_has_prefix(target, prefix))
                    rel = g_strdup(target + strlen(prefix));
                else
                    rel = g_strdup(target);

                const char *prefix_name = media_prefix(file->kind);
                gchar *rel_key = g_strdup_printf("%s_rel", prefix_name);
                if (g_hash_table_lookup(vars, rel_key) == NULL)
                    set_var(vars, rel_key, rel);
                add_media_item(arrays, file->kind, file->kind, rel,
                               file->filename, (int)m + 1);
                g_free(rel_key);
                g_free(rel);
                g_free(prefix);
                g_free(target);
                g_free(base);
                g_free(ext);
            }
            g_free(media_dir);
            g_ptr_array_unref(media);
        }

        GString *block = g_hash_table_lookup(blocks, game->platform);
        if (block == NULL) {
            block = g_string_new(NULL);
            g_hash_table_insert(blocks, g_strdup(game->platform), block);
        }
        render_nodes(spec->game, &ctx, block);

        if (out_stats != NULL)
            out_stats->games++;
        if (progress != NULL)
            progress(i + 1, games->len, game->name, user_data);

        g_hash_table_unref(vars);
        g_hash_table_unref(arrays);
        g_free(m3u_name);
        g_free(first_name);
        g_ptr_array_unref(roms);
    }

    /* 按平台写清单 */
    GList *keys = g_hash_table_get_keys(blocks);
    keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);
    for (GList *it = keys; it != NULL; it = it->next) {
        const char *platform = it->data;
        GString *block = g_hash_table_lookup(blocks, platform);

        GHashTable *vars = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, g_free);
        fill_platform_vars(vars, spec, rules, options, platform);
        RenderCtx ctx = { vars, NULL, spec->escape };

        gchar *manifest_rel = expand_tokens(spec->manifest, vars);
        gchar *manifest = g_build_filename(options->output_dir,
                                           manifest_rel, NULL);
        ensure_parent(manifest);

        GString *content = g_string_new(NULL);
        render_nodes(spec->header, &ctx, content);
        g_string_append_len(content, block->str, (gssize)block->len);
        render_nodes(spec->footer, &ctx, content);

        if (!g_file_set_contents(manifest, content->str,
                                 (gssize)content->len, NULL) && out_stats)
            out_stats->errors++;

        g_string_free(content, TRUE);
        g_hash_table_unref(vars);
        g_free(manifest);
        g_free(manifest_rel);
    }
    g_list_free(keys);

    g_hash_table_unref(blocks);
    g_ptr_array_unref(games);
    rh_export_spec_free(spec);
    return RH_OK;
}
