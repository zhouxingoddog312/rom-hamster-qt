#include "rh-ctx.h"

struct RhCtx {
    char *rom_root;
    char *store_path;
};

RhCtx *rh_ctx_new(void)
{
    return g_new0(RhCtx, 1);
}

void rh_ctx_free(RhCtx *ctx)
{
    if (ctx == NULL)
        return;
    g_free(ctx->rom_root);
    g_free(ctx->store_path);
    g_free(ctx);
}

void rh_ctx_set_rom_root(RhCtx *ctx, const char *rom_root)
{
    if (ctx == NULL)
        return;
    g_free(ctx->rom_root);
    ctx->rom_root = g_strdup(rom_root);
}

const char *rh_ctx_rom_root(const RhCtx *ctx)
{
    return ctx != NULL ? ctx->rom_root : NULL;
}

void rh_ctx_set_store_path(RhCtx *ctx, const char *store_path)
{
    if (ctx == NULL)
        return;
    g_free(ctx->store_path);
    ctx->store_path = g_strdup(store_path);
}

const char *rh_ctx_store_path(const RhCtx *ctx)
{
    return ctx != NULL ? ctx->store_path : NULL;
}
