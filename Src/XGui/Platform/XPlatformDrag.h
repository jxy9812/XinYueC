/******************************************************************************
 * @file       XPlatformDrag.h
 * @brief      平台出站拖放契约（对标 Qt QPlatformDrag）。
 * @details    公共层仅传递 XMimeData 与动作掩码；Linux XDND、Windows OLE
 *             等系统协议全部位于 Drive，嵌入式后端安全返回 Unsupported。
 ******************************************************************************/
#ifndef XPLATFORMDRAG_H
#define XPLATFORMDRAG_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"

#if XMIMEDATA_ON
#include "XMimeData.h"
#else
typedef struct XMimeData XMimeData;
#endif
typedef struct XWindow XWindow;

#if XPLATFORMINTEGRATION_ON

/** @brief 平台出站拖放句柄；不透明结构，具体定义位于 Drive 平台后端。 */
typedef struct XPlatformDrag XPlatformDrag;

/**
 * @brief      拖放动作位掩码（对标 Qt::DropAction 的平台层原始值；可按位或）。
 * @details    上层经 XDropAction 转换为 Qt 语义动作（见 XPlatformDragResult）。
 */
typedef enum XPlatformDragAction
{
    XPlatformDragAction_Copy = 1u, /**< 复制动作（源数据保留）。 */
    XPlatformDragAction_Move = 2u, /**< 移动动作（源数据删除）。 */
    XPlatformDragAction_Link = 4u  /**< 链接动作（引用源数据）。 */
} XPlatformDragAction;

/** @brief 拖放执行结果（XGui 独立数值，不直接等于 Qt::DropAction；
 *        上层经 XDropAction 转换：Copied=CopyAction、Moved=MoveAction、
 *        Linked=LinkAction——Task 2.16 文档化）。 */
typedef enum XPlatformDragResult
{
    XPlatformDragResult_Unsupported = 0,
    XPlatformDragResult_Cancelled = 1,
    XPlatformDragResult_Copied = 2,
    XPlatformDragResult_Moved = 3,
    XPlatformDragResult_Linked = 4
} XPlatformDragResult;

/**
 * @brief      创建平台出站拖放句柄（对标 QPlatformDrag）。
 * @return     新句柄；平台不支持或分配失败返回 NULL，调用方用
 *             XPlatformDrag_delete 释放。
 */
XPlatformDrag* XPlatformDrag_create(void);
/**
 * @brief      释放平台出站拖放句柄。
 * @param      self 目标句柄；可为 NULL（不执行任何操作）。
 * @return     无。
 */
void XPlatformDrag_delete(XPlatformDrag* self);
/**
 * @brief      查询当前平台是否支持出站拖放。
 * @param      self 目标句柄；可为 NULL。
 * @return     平台后端可用返回 true；不支持（嵌入式后端）恒返回 false。
 */
bool XPlatformDrag_isAvailable(const XPlatformDrag* self);
/**
 * @brief      执行出站拖放并返回结果（对标 QDrag::exec）。
 * @details    Linux XDND、Windows OLE 等系统协议由 Drive 后端实现；
 *             不支持的平台安全返回 Unsupported。
 * @param      self 目标句柄。
 * @param      source 拖拽源窗口借用指针。
 * @param      data 拖拽数据借用指针；生命周期须覆盖整个拖放过程。
 * @param      actions 允许的动作位掩码（XPlatformDragAction 按位或）。
 * @return     拖放执行结果（XPlatformDragResult 枚举值）。
 */
XPlatformDragResult XPlatformDrag_exec(XPlatformDrag* self,
                                       XWindow* source,
                                       const XMimeData* data,
                                       uint32_t actions);

#endif

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif
