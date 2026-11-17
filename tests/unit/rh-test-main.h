/*
 * rh-test-main.h —— 测试入口的统一初始化
 *
 * Windows 下 argv 是 ANSI/GBK；GLib 的 g_test_init 会据当前目录/argv[0] 推测
 * G_TEST_SRCDIR / G_TEST_BUILDDIR 并调用 g_setenv，非 UTF-8 值会触发
 * "g_setenv: assertion 'g_utf8_validate (value, -1, NULL)' failed"。
 * 这里先把参数转成 UTF-8，并预先写入 UTF-8 的目录变量，避免走到 GLib 的推测分支。
 */
#ifndef RH_TEST_MAIN_H
#define RH_TEST_MAIN_H

#include <glib.h>

#include "rh-path.h"

static inline void rh_test_init(int *argc, char ***argv)
{
#ifdef _WIN32
    for (int i = 0; i < *argc; i++) {
        char *utf8 = rh_path_utf8_from_native((*argv)[i]);
        if (utf8 != NULL)
            (*argv)[i] = utf8;
    }

    char *cwd = rh_path_current_dir();
    if (cwd != NULL) {
        if (g_getenv("G_TEST_SRCDIR") == NULL)
            g_setenv("G_TEST_SRCDIR", cwd, TRUE);
        if (g_getenv("G_TEST_BUILDDIR") == NULL)
            g_setenv("G_TEST_BUILDDIR", cwd, TRUE);
        g_free(cwd);
    }
#endif

    g_test_init(argc, argv, NULL);
}

#endif /* RH_TEST_MAIN_H */
