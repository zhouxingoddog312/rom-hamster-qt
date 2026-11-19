/*
 * core-bridge.h —— C++ 侧包含核心 C 头文件的唯一入口
 *
 * 核心是 C11 编译的（无 extern "C" 包裹），C++ 前端必须显式声明 C 链接，
 * 否则符号名会被改写导致链接失败。
 */
#ifndef RH_GUI_CORE_BRIDGE_H
#define RH_GUI_CORE_BRIDGE_H

#include <glib.h>

extern "C" {
#include "detail.h"
#include "import.h"
#include "platform.h"
#include "query.h"
#include "rh-ctx.h"
#include "rh-err.h"
#include "rh-path.h"
#include "rh-records.h"
#include "rh-ui.h"
#include "store.h"
}

#endif /* RH_GUI_CORE_BRIDGE_H */
