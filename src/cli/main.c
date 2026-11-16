#include <glib.h>

#include "rh-version.h"

static gboolean opt_version = FALSE;
static gchar **opt_rest = NULL;

int main(int argc, char **argv)
{
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
        g_printerr("%s: %s\n", RH_PROJECT_NAME, error->message);
        g_error_free(error);
        g_option_context_free(option_ctx);
        return 2;
    }
    g_option_context_free(option_ctx);

    if (opt_version) {
        g_print("%s %s\n", RH_PROJECT_NAME, RH_VERSION);
        g_print("仓库: %s\n", RH_REPO_URL);
        g_print("问题反馈: %s\n", RH_ISSUES_URL);
        return 0;
    }

    /* M0 骨架：子命令在后续里程碑实现 */
    g_printerr("%s %s — M0 骨架，子命令尚未实现；用 --version 查看版本。\n",
               RH_PROJECT_NAME, RH_VERSION);
    return 1;
}
