/*
 * rh-err.h —— 错误码与可读描述（ARCHITECTURE.md §2.1 L0）
 *
 * 核心库不打印面向用户的文字：只返回错误码，由前端决定如何呈现。
 */
#ifndef RH_ERR_H
#define RH_ERR_H

#include <glib.h>

typedef enum {
    RH_OK = 0,
    RH_ERR_INVALID = 1,   /* 参数非法 */
    RH_ERR_NOMEM = 2,     /* 内存不足 */
    RH_ERR_IO = 3,        /* 文件读写失败 */
    RH_ERR_DB = 4,        /* SQLite 打开/执行失败 */
    RH_ERR_SCHEMA_TOO_NEW = 5, /* 库由更高版本的 rom-hamster 创建 */
    RH_ERR_MIGRATE = 6,   /* schema 迁移失败 */
    RH_ERR_UNSUPPORTED = 7 /* 平台或功能不支持 */
} RhErr;

/* 稳定的英文短描述，供日志与 CLI 使用；核心不据此拼用户文案。 */
const char *rh_err_str(RhErr err);

#endif /* RH_ERR_H */
