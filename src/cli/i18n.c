/*
 * i18n.c —— CLI 的 gettext 初始化
 *
 * .mo 查找顺序：
 *   1. 环境变量 ROM_HAMSTER_LOCALEDIR（测试与特殊布局用）
 *   2. 可执行文件旁的 ../share/locale（zip / tarball / deb 都是这个布局）
 *   3. 编译期安装前缀下的 share/locale
 *
 * 只调用 setlocale(LC_MESSAGES)，不改 LC_CTYPE：CLI 始终按 UTF-8 写字节
 * （§15），若让 LC_CTYPE 跟随系统区域设置，GLib 在终端上会把中文转成 '?'。
 */
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>

#include "i18n.h"

#ifndef RH_LOCALEDIR
#define RH_LOCALEDIR "/usr/share/locale"
#endif

#ifdef _WIN32
#include <windows.h>

#include "rh-path.h"
#else
#include <unistd.h>
#endif

static char *exe_dir(void)
{
#ifdef _WIN32
    wchar_t wide[32768];
    DWORD len = GetModuleFileNameW(NULL, wide, G_N_ELEMENTS(wide));
    if (len == 0 || len >= G_N_ELEMENTS(wide))
        return NULL;
    char *utf8 = rh_path_from_wide(wide);
    if (utf8 == NULL)
        return NULL;
    char *dir = g_path_get_dirname(utf8);
    g_free(utf8);
    return dir;
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (len <= 0)
        return NULL;
    buf[len] = '\0';
    return g_path_get_dirname(buf);
#endif
}

static char *locale_dir(void)
{
    const char *env = g_getenv("ROM_HAMSTER_LOCALEDIR");
    if (env != NULL && *env != '\0')
        return g_strdup(env);

    char *base = exe_dir();
    if (base != NULL) {
        char *candidate = g_build_filename(base, "..", "share", "locale", NULL);
        g_free(base);
        if (g_file_test(candidate, G_FILE_TEST_IS_DIR))
            return g_canonicalize_filename(candidate, NULL);
        g_free(candidate);
    }
    return g_strdup(RH_LOCALEDIR);
}

void rh_i18n_init(void)
{
#ifdef _WIN32
    /* Windows 的 libintl 读 LANG/LC_ALL；中文用户的系统里通常没设，按界面
     * 语言补一个 zh_CN，否则会一直显示英文 msgid。 */
    if (g_getenv("LANG") == NULL && g_getenv("LC_ALL") == NULL
        && g_getenv("LC_MESSAGES") == NULL) {
        LANGID langid = GetUserDefaultUILanguage();
        if ((langid & 0xff) == LANG_CHINESE)
            g_setenv("LANG", "zh_CN.UTF-8", TRUE);
    }
#endif

    /* Windows 的 MSVCRT 不支持 LC_MESSAGES，失败也不影响 libintl。 */
    setlocale(LC_MESSAGES, "");

    char *dir = locale_dir();
    bindtextdomain(GETTEXT_PACKAGE, dir);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    g_free(dir);
}
