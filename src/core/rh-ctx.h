/*
 * rh-ctx.h —— 核心上下文（ARCHITECTURE.md §2.1、§6）
 *
 * 所有状态经 RhCtx 传递，核心库不持有全局可变状态。
 * 本文件在 M0 只落地最少的字段，配置、日志与缓存目录在后续里程碑补齐。
 */
#ifndef RH_CTX_H
#define RH_CTX_H

#include <glib.h>

typedef struct RhCtx RhCtx;

RhCtx *rh_ctx_new(void);
void rh_ctx_free(RhCtx *ctx);

/* ROM 收藏根目录（UTF-8，内部统一用 '/'）。传入 NULL 表示清除。 */
void rh_ctx_set_rom_root(RhCtx *ctx, const char *rom_root);
const char *rh_ctx_rom_root(const RhCtx *ctx);

/* 库文件路径；默认由 store_open 的入参决定，此处仅供前端记录。 */
void rh_ctx_set_store_path(RhCtx *ctx, const char *store_path);
const char *rh_ctx_store_path(const RhCtx *ctx);

#endif /* RH_CTX_H */
