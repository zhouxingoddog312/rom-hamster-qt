#include "cli-common.h"

#include <glib.h>

void rh_cli_print(FILE *stream, const char *text)
{
    if (text == NULL)
        return;
    fputs(text, stream);
    fflush(stream);
}

char *rh_cli_default_db(void)
{
    const char *env = g_getenv("ROM_HAMSTER_DB");
    if (env != NULL && env[0] != '\0')
        return g_strdup(env);
    return g_strdup("romhamster.db");
}

RhStore *rh_cli_open_store(const char *db, RhErr *out_err)
{
    RhErr err = RH_OK;
    RhStore *store = store_open(db, &err);
    if (store == NULL) {
        gchar *message = g_strdup_printf("无法打开数据库 %s：%s\n", db,
                                         rh_err_str(err));
        rh_cli_print(stderr, message);
        g_free(message);
    }
    if (out_err != NULL)
        *out_err = err;
    return store;
}

int rh_cli_error(const char *message)
{
    gchar *line = g_strdup_printf("%s\n", message);
    rh_cli_print(stderr, line);
    g_free(line);
    return 1;
}
