#include <stdio.h>
#include <string.h>

#include <glib.h>

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

static void print_utf8(FILE *stream, const char *text);

static void print_help(const char *program)
{
    gchar *text = g_strdup_printf(
        "用法: %s <命令> [选项]\n"
        "\n"
        "命令:\n"
        "  list     检索游戏（13 维筛选 / 4 种排序）\n"
        "  show     显示游戏详情（纯文本）\n"
        "  edit     编辑游戏元数据\n"
        "  delete   删除游戏记录（可选删除文件）\n"
        "  stats    库统计\n"
        "  export   按前端规格导出清单\n"
        "  help     显示本帮助\n"
        "  version  显示版本\n"
        "\n"
        "用 \"%s <命令> --help\" 查看各命令选项。\n",
        program, program);
    print_utf8(stdout, text);
    g_free(text);
}

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
        { NULL, 0, 0, 0, 0, NULL, NULL }
    };

    GOptionContext *option_ctx = g_option_context_new("- ROM 库管理器");
    g_option_context_add_main_entries(option_ctx, entries, NULL);
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
        print_utf8(stdout, RH_PROJECT_NAME " " RH_VERSION "\n");
        print_utf8(stdout, "仓库: " RH_REPO_URL "\n");
        print_utf8(stdout, "问题反馈: " RH_ISSUES_URL "\n");
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
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 ||
        strcmp(command, "-h") == 0) {
        print_help(argv[0]);
        return 0;
    }
    if (strcmp(command, "version") == 0) {
        opt_version = TRUE;
        print_utf8(stdout, RH_PROJECT_NAME " " RH_VERSION "\n");
        print_utf8(stdout, "仓库: " RH_REPO_URL "\n");
        print_utf8(stdout, "问题反馈: " RH_ISSUES_URL "\n");
        return 0;
    }

    gchar *message = g_strdup_printf("未知命令: %s（用 help 查看用法）\n", command);
    print_utf8(stderr, message);
    g_free(message);
    return 2;
}
