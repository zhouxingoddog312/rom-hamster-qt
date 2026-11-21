#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "i18n.h"
#include "rh-path.h"
#include "rh-version.h"

#ifdef _WIN32
#include <windows.h>
#endif

static gboolean opt_version = FALSE;

int rh_cmd_list(int argc, char **argv);
int rh_cmd_show(int argc, char **argv);
int rh_cmd_delete(int argc, char **argv);
int rh_cmd_edit(int argc, char **argv);
int rh_cmd_stats(int argc, char **argv);
int rh_cmd_export(int argc, char **argv);
int rh_cmd_dat(int argc, char **argv);

static void print_utf8(FILE *stream, const char *text);

static void print_help(const char *program)
{
    gchar *text = g_strdup_printf(
        _("Usage: %s <command> [options]\n"
          "\n"
          "Commands:\n"
          "  list     Search games (13 filters, 4 sort orders)\n"
          "  show     Show game details as plain text\n"
          "  edit     Edit game metadata\n"
          "  delete   Delete a game record (optionally its files)\n"
          "  stats    Library statistics\n"
          "  export   Export a manifest for a front-end\n"
          "  dat      Import, fetch and inspect DAT indexes\n"
          "  help     Show this help\n"
          "  version  Show version information\n"
          "\n"
          "Run \"%s <command> --help\" for command options.\n"),
        program, program);
    print_utf8(stdout, text);
    g_free(text);
}

/* g_print()/g_printerr() end up in GLib's g_fputs() (glib/gprint.c), which
 * converts UTF-8 to the charset of the *current* locale whenever the stream
 * is a terminal, falling back to '?' for characters that charset cannot
 * represent. rh_i18n_init() only touches LC_MESSAGES, so LC_CTYPE stays "C"
 * (ASCII) and every CJK character would become '?' on a terminal. Piped
 * output skips that branch and stays UTF-8, i.e. the same command printed
 * different bytes depending on whether stdout was a terminal.
 *
 * The CLI always emits UTF-8 (ARCHITECTURE.md 15), so write the bytes as-is. */
static void print_utf8(FILE *stream, const char *text)
{
    fputs(text, stream);
    fflush(stream);
}

static void print_version(void)
{
    print_utf8(stdout, RH_PROJECT_NAME " " RH_VERSION "\n");
    gchar *line = g_strdup_printf("%s%s\n", _("Repository: "), RH_REPO_URL);
    print_utf8(stdout, line);
    g_free(line);
    line = g_strdup_printf("%s%s\n", _("Report issues: "), RH_ISSUES_URL);
    print_utf8(stdout, line);
    g_free(line);
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    /* Raw UTF-8 bytes are written below, so the console must decode them as
     * UTF-8. Without this the console uses its legacy code page (437/936) and
     * shows mojibake, while redirected output would still be correct UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    rh_i18n_init();

    /* Windows 的 argv 是 ANSI/GBK；GLib 与后续路径处理都要求 UTF-8（§15）。
     * 必须在任何解析之前统一转换，否则含中文的路径参数会出错。 */
    for (int i = 0; i < argc; i++) {
        char *utf8 = rh_path_utf8_from_native(argv[i]);
        if (utf8 != NULL)
            argv[i] = utf8;
    }

    GOptionEntry entries[] = {
        { "version", 'V', 0, G_OPTION_ARG_NONE, &opt_version,
          N_("Show version information and exit"), NULL },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *option_ctx = g_option_context_new(_("- ROM library manager"));
    g_option_context_add_main_entries(option_ctx, entries, NULL);
    /* 让 GLib 用同一个翻译域处理 N_() 标记的选项说明 */
    g_option_context_set_translation_domain(option_ctx, GETTEXT_PACKAGE);
    /* 遇到第一个非选项参数（子命令）就停止解析，选项交给子命令处理 */
    g_option_context_set_strict_posix(option_ctx, TRUE);

    GError *error = NULL;
    if (!g_option_context_parse(option_ctx, &argc, &argv, &error)) {
        fprintf(stderr, "%s: %s\n", RH_PROJECT_NAME, error->message);
        g_error_free(error);
        g_option_context_free(option_ctx);
        return 2;
    }
    g_option_context_free(option_ctx);

    if (opt_version) {
        print_version();
        return 0;
    }

    /* 子命令分发；argv[0] 仍是程序名，子命令参数从 argv+1 开始 */
    if (argc < 2) {
        print_help(argv[0]);
        return 2;
    }

    const char *command = argv[1];
    int sub_argc = argc - 1;
    char **sub_argv = argv + 1;

    if (strcmp(command, "list") == 0 || strcmp(command, "search") == 0)
        return rh_cmd_list(sub_argc, sub_argv);
    if (strcmp(command, "show") == 0)
        return rh_cmd_show(sub_argc, sub_argv);
    if (strcmp(command, "edit") == 0)
        return rh_cmd_edit(sub_argc, sub_argv);
    if (strcmp(command, "delete") == 0)
        return rh_cmd_delete(sub_argc, sub_argv);
    if (strcmp(command, "stats") == 0)
        return rh_cmd_stats(sub_argc, sub_argv);
    if (strcmp(command, "export") == 0)
        return rh_cmd_export(sub_argc, sub_argv);
    if (strcmp(command, "dat") == 0)
        return rh_cmd_dat(sub_argc, sub_argv);
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 ||
        strcmp(command, "-h") == 0) {
        print_help(argv[0]);
        return 0;
    }
    if (strcmp(command, "version") == 0) {
        print_version();
        return 0;
    }

    gchar *message = g_strdup_printf(_("Unknown command: %s (see 'help' for usage)\n"),
                                     command);
    print_utf8(stderr, message);
    g_free(message);
    return 2;
}
