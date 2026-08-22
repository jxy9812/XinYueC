/******************************************************************************
 * @file       XPlatformDrag.h
 * @brief      平台出站拖放契约（对标 Qt QPlatformDrag）。
 * @details    公共层仅传递 XMimeData 与动作掩码；Linux XDND、Windows OLE
 *             等系统协议全部位于 Drive，嵌入式后端安全返回 Unsupported。
 ******************************************************************************/
#ifndef XPLATFORMDRAG_H
#define XPLATFORMDRAG_H

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

typedef struct XPlatformDrag XPlatformDrag;

typedef enum XPlatformDragAction
{
    XPlatformDragAction_Copy = 1u,
    XPlatformDragAction_Move = 2u,
    XPlatformDragAction_Link = 4u
} XPlatformDragAction;

typedef enum XPlatformDragResult
{
    XPlatformDragResult_Unsupported = 0,
    XPlatformDragResult_Cancelled = 1,
    XPlatformDragResult_Copied = 2,
    XPlatformDragResult_Moved = 3,
    XPlatformDragResult_Linked = 4
} XPlatformDragResult;

XPlatformDrag* XPlatformDrag_create(void);
void XPlatformDrag_delete(XPlatformDrag* self);
bool XPlatformDrag_isAvailable(const XPlatformDrag* self);
XPlatformDragResult XPlatformDrag_exec(XPlatformDrag* self,
                                       XWindow* source,
                                       const XMimeData* data,
                                       uint32_t actions);

#endif
#endif
