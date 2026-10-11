/**
 * @file       XEventDispatcher.h
 * @brief      平台事件分发器创建 API。
 * @details    对标 Qt QAbstractEventDispatcher 的平台工厂入口；声明由平台层实现的
 *             XEventDispatcher_create_ex 工厂函数，供 XThreadData 等内部模块为线程
 *             创建事件分发器使用。实现位于 XAbstractEventDispatcher.c（自
 *             XEventDispatcher_win32.c 平台无关化迁移后）。
 */

#ifndef XEVENTDISPATCHER_H
#define XEVENTDISPATCHER_H
#include "XSync_config.h"
#if XSYNC_ON
#if XTHREADDATA_ON

#include "XAbstractEventDispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建平台事件分发器（需平台层实现）
 * @param memory 内存分配器类型
 * @param parent 父对象，可为 NULL
 * @return 新创建的事件分发器，失败返回 NULL
 */
XAbstractEventDispatcher* XEventDispatcher_create_ex(XMemoryType memory,  XObject* parent);

#ifdef __cplusplus
}
#endif

#endif // XTHREADDATA_ON
#endif /* XSYNC_ON */

/* XClass create API default-memory wrappers. */
#undef XEventDispatcher_create
#define XEventDispatcher_create(...) XEventDispatcher_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XEVENTDISPATCHER_H
