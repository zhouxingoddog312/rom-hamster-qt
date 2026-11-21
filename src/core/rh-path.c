#include "rh-path.h"

gboolean rh_path_is_utf8(const char *s)
{
    return s == NULL || g_utf8_validate(s, -1, NULL);
}

#ifdef _WIN32

#include <windows.h>

wchar_t *rh_path_to_wide(const char *utf8)
{
    if (utf8 == NULL)
        return NULL;

    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0)
        return NULL;

    wchar_t *wide = g_new(wchar_t, (gsize)len);
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len) <= 0) {
        g_free(wide);
        return NULL;
    }
    return wide;
}

char *rh_path_from_wide(const wchar_t *wide)
{
    if (wide == NULL)
        return NULL;

    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
        return NULL;

    char *utf8 = g_new(char, (gsize)len);
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, len, NULL, NULL) <= 0) {
        g_free(utf8);
        return NULL;
    }
    return utf8;
}

char *rh_path_utf8_from_native(const char *native)
{
    if (native == NULL)
        return NULL;

    int len = MultiByteToWideChar(CP_ACP, 0, native, -1, NULL, 0);
    if (len <= 0)
        return g_strdup(native);

    wchar_t *wide = g_new(wchar_t, (gsize)len);
    if (MultiByteToWideChar(CP_ACP, 0, native, -1, wide, len) <= 0) {
        g_free(wide);
        return g_strdup(native);
    }

    char *utf8 = rh_path_from_wide(wide);
    g_free(wide);
    return utf8;
}

char *rh_path_current_dir(void)
{
    DWORD len = GetCurrentDirectoryW(0, NULL);
    if (len == 0)
        return NULL;

    wchar_t *wide = g_new(wchar_t, (gsize)len);
    char *utf8 = NULL;
    if (GetCurrentDirectoryW(len, wide) > 0)
        utf8 = rh_path_from_wide(wide);
    g_free(wide);
    return utf8;
}

#else /* POSIX */

char *rh_path_utf8_from_native(const char *native)
{
    return g_strdup(native);
}

char *rh_path_current_dir(void)
{
    return g_get_current_dir();
}

#endif /* _WIN32 */

#ifndef RH_DATA_DIR
#define RH_DATA_DIR ""
#endif

static char *executable_dir(void)
{
#ifdef _WIN32
    DWORD size = MAX_PATH;
    wchar_t *buffer = g_new(wchar_t, (gsize)size);
    DWORD len = 0;
    for (;;) {
        len = GetModuleFileNameW(NULL, buffer, size);
        if (len == 0) {
            g_free(buffer);
            return NULL;
        }
        if (len < size - 1)
            break;
        size *= 2;
        buffer = g_renew(wchar_t, buffer, (gsize)size);
    }
    char *utf8 = rh_path_from_wide(buffer);
    g_free(buffer);
    if (utf8 == NULL)
        return NULL;
#else
    char *utf8 = g_file_read_link("/proc/self/exe", NULL);
    if (utf8 == NULL)
        return NULL;
#endif

    char *slash = NULL;
    for (char *p = utf8; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\')
            slash = p;
    }

    char *dir = NULL;
    if (slash == NULL)
        dir = g_strdup(utf8);
    else if (slash == utf8)
        dir = g_strndup(utf8, 1);
    else
        dir = g_strndup(utf8, (gsize)(slash - utf8));
    g_free(utf8);
    return dir;
}

static void add_dir(GPtrArray *dirs, const char *path)
{
    if (path == NULL || path[0] == '\0')
        return;
    if (g_ptr_array_find_with_equal_func(dirs, path, (GEqualFunc)g_str_equal, NULL))
        return;
    g_ptr_array_add(dirs, g_strdup(path));
}

GPtrArray *rh_path_data_dir_candidates(void)
{
    GPtrArray *dirs = g_ptr_array_new_with_free_func(g_free);

    add_dir(dirs, g_getenv("ROM_HAMSTER_DATA_DIR"));

    char *exe_dir = executable_dir();
    if (exe_dir != NULL) {
        const char *suffixes[] = {
            "../share/rom-hamster",
            "share/rom-hamster",
            "../../share/rom-hamster"
        };
        for (gsize i = 0; i < G_N_ELEMENTS(suffixes); i++) {
            char *joined = g_build_filename(exe_dir, suffixes[i], NULL);
            char *clean = g_canonicalize_filename(joined, NULL);
            add_dir(dirs, clean);
            g_free(clean);
            g_free(joined);
        }
        g_free(exe_dir);
    }

    /* 源码树兜底：仅供开发构建，安装布局优先。 */
    add_dir(dirs, RH_DATA_DIR);

#ifndef _WIN32
    add_dir(dirs, "/usr/share/rom-hamster");
#endif
    return dirs;
}
