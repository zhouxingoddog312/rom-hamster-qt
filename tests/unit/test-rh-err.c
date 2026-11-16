#include <glib.h>

#include "rh-err.h"

static void test_err_str(void)
{
    g_assert_cmpstr(rh_err_str(RH_OK), ==, "ok");
    g_assert_cmpstr(rh_err_str(RH_ERR_INVALID), ==, "invalid argument");
    g_assert_cmpstr(rh_err_str(RH_ERR_SCHEMA_TOO_NEW), ==,
                    "database schema is newer than this build");
    g_assert_cmpstr(rh_err_str((RhErr)999), ==, "unknown error");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/err/str", test_err_str);
    return g_test_run();
}
