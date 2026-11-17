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
