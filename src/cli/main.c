#include <stdio.h>

#include <glib.h>

#include "rh-path.h"
#include "rh-version.h"

#ifdef _WIN32
#include <windows.h>
#endif

static gboolean opt_version = FALSE;
static gchar **opt_rest = NULL;

/* g_print()/g_printerr() end up in GLib's g_fputs() (glib/gprint.c), which
 * converts UTF-8 to the charset of the *current* locale whenever the stream
 * is a terminal, falling back to '?' for characters that charset cannot
 * represent. Nothing in this program calls setlocale(), so the process stays
 * in the "C" locale (ASCII) and every CJK character becomes '?'. Piped output
 * skips that branch and stays UTF-8, i.e. the same command printed different
 * bytes depending on whether stdout was a terminal.
 *
 * The CLI always emits UTF-8 (ARCHITECTURE.md 15), so write the bytes as-is. */
static void print_utf8(FILE *stream, const char *text)
{
    fputs(text, stream);
    fflush(stream);
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

    /* Windows 的 argv 是 ANSI/GBK；GLib 与后续路径处理都要求 UTF-8（§15）。
     * 必须在任何解析之前统一转换，否则含中文的路径参数会出错。 */
    for (int i = 0; i < argc; i++) {
        char *utf8 = rh_path_utf8_from_native(argv[i]);
        if (utf8 != NULL)
            argv[i] = utf8;
    }

    GOptionEntry entries[] = {
        { "version", 'V', 0, G_OPTION_ARG_NONE, &opt_version,
          "显示版本信息后退出", NULL },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_rest,
          NULL, "COMMAND" },
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *option_ctx = g_option_context_new("- ROM 库管理器");
    g_option_context_add_main_entries(option_ctx, entries, NULL);

    GError *error = NULL;
    if (!g_option_context_parse(option_ctx, &argc, &argv, &error)) {
        fprintf(stderr, "%s: %s\n", RH_PROJECT_NAME, error->message);
        g_error_free(error);
        g_option_context_free(option_ctx);
        return 2;
    }
    g_option_context_free(option_ctx);

    if (opt_version) {
        print_utf8(stdout, RH_PROJECT_NAME " " RH_VERSION "\n");
        print_utf8(stdout, "仓库: " RH_REPO_URL "\n");
        print_utf8(stdout, "问题反馈: " RH_ISSUES_URL "\n");
        return 0;
    }

    /* M0 骨架：子命令在后续里程碑实现 */
    fprintf(stderr, "%s %s — M0 骨架，子命令尚未实现；用 --version 查看版本。\n",
            RH_PROJECT_NAME, RH_VERSION);
    return 1;
}
