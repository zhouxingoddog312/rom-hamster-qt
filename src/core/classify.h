/*
 * classify.h —— 单文件分类（ARCHITECTURE.md §5）
 *
 * 边界：只判定，不搬文件、不建目录、不写库。任何写操作属于 import 的执行阶段。
 * 纯函数：相同输入必得相同结果，可在任意线程调用。
 */
#ifndef RH_CLASSIFY_H
#define RH_CLASSIFY_H

#include <glib.h>

#include "platform.h"

typedef enum {
    RH_CLASSIFY_ROM,
    RH_CLASSIFY_ARCHIVE,
    RH_CLASSIFY_PATCH,
    RH_CLASSIFY_MEDIA,
    RH_CLASSIFY_UNKNOWN
} RhClassifyKind;

typedef struct {
    RhClassifyKind kind;
    gchar     *extension;      /* 最后一段后缀，含点、小写 */
    gchar     *media_kind;     /* MEDIA 时有效，见 RhMediaKind */
    gchar     *patch_platform; /* PATCH 时可能为 NULL（通用补丁） */
    GPtrArray *candidates;     /* RhPlatform*，借用规则库，不归调用方释放 */
    gboolean   ambiguous;      /* 命中多个平台，或后缀在该平台 ambiguous_formats 中 */
} RhClassifyResult;

RhClassifyResult *rh_classify_file(const RhPlatformRules *rules, const char *filename);
void rh_classify_result_free(RhClassifyResult *result);

/* 压缩包六级分流（ARCHITECTURE.md §5.2） */
typedef enum {
    RH_ARCHIVE_PSV = 0,    /* PSV 整库：整体入库，不解压 */
    RH_ARCHIVE_PATCHES,    /* 纯补丁包 */
    RH_ARCHIVE_ARCADE,     /* 街机 ZIP：多文件同属一个游戏 */
    RH_ARCHIVE_AMBIGUOUS,  /* 平台或类型无法唯一确定 */
    RH_ARCHIVE_SINGLE_ROM, /* 恰好一个 ROM，无其他内容 */
    RH_ARCHIVE_MIXED       /* ROM 与补丁/媒体/未知混装 */
} RhArchiveBranch;

typedef struct {
    RhArchiveBranch branch;
    gchar     *platform;    /* ROM 候选唯一时给出平台 ID，否则 NULL */
    gboolean   ambiguous;   /* 平台候选不唯一或后缀二义 */
    guint      rom_count;
    guint      patch_count;
    guint      media_count;
    guint      unknown_count;
} RhArchiveClassifyResult;

/*
 * 只根据条目名判定分流，不读压缩包。entry_paths 为 gchar* 数组，
 * 目录条目可以带或不带结尾 '/'。
 */
RhArchiveClassifyResult *rh_classify_archive(const RhPlatformRules *rules,
                                             const GPtrArray *entry_paths);
void rh_archive_classify_free(RhArchiveClassifyResult *result);

/* 仅按媒体后缀判断类型；未命中返回 NULL */
const gchar *rh_classify_media_kind(const RhPlatformRules *rules, const char *filename);

#endif /* RH_CLASSIFY_H */
