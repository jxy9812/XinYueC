// XAbstractNetIoRing.c
//
// 纯虚基类实现：仅提供 SQ/CQ 队列管理、全局单例、虚函数调度入口。
// 不提供 class_init 和默认虚函数实现，子类必须自行创建虚函数表。
#include "XAbstractNetIoRing.h"
#if XAbstractNetIoRing_ON
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * 全局单例
 * ================================================================ */
static XAbstractNetIoRing* g_globalRing = NULL;

XAbstractNetIoRing* XAbstractNetIoRing_global(void) {
    return g_globalRing;
}

void XAbstractNetIoRing_setGlobal(XAbstractNetIoRing* ring) {
    g_globalRing = ring;
}

/* ================================================================
 * 构造与析构
 * ================================================================ */

void XAbstractNetIoRing_init(XAbstractNetIoRing* ring) {
    if (!ring) return;

    /* 清零基类部分（子类应已先 memset 清零整个子类结构体） */
    memset(ring, 0, sizeof(XAbstractNetIoRing));

    /* 初始化 XClass 基类（设置 m_vtable=NULL, m_free=NULL） */
    XClass_init(ring);

    /* 注意：不设置虚函数表，子类必须在调用本函数后自行 XClassSetVtable */

    /* 创建 SQ/CQ 无锁队列 */
    ring->m_sq = XLockFreeQueue_create(sizeof(XAbstractNetIoRing_SQEntry), XNETIORING_SQ_CAPACITY);
    ring->m_cq = XLockFreeQueue_create(sizeof(XAbstractNetIoRing_CQEntry), XNETIORING_CQ_CAPACITY);
    ring->m_eventFd = XFD_INVALID;
    ring->m_enabled = false;
}

void XAbstractNetIoRing_cleanupQueues(XAbstractNetIoRing* ring) {
    if (!ring) return;

    /* 释放 SQ/CQ 无锁队列 */
    if (ring->m_sq) {
        XLockFreeQueue_delete_base(ring->m_sq);
        ring->m_sq = NULL;
    }
    if (ring->m_cq) {
        XLockFreeQueue_delete_base(ring->m_cq);
        ring->m_cq = NULL;
    }
    ring->m_eventFd = XFD_INVALID;
    ring->m_enabled = false;
}

/* ================================================================
 * 核心功能
 * ================================================================ */

bool XAbstractNetIoRing_pushSQ(XAbstractNetIoRing* ring, const XAbstractNetIoRing_SQEntry* entry) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(entry, "SQEntry") || !ring->m_sq)
        return false;
    return XLockFreeQueue_push_base(ring->m_sq, (void*)entry);
}

bool XAbstractNetIoRing_pushCompletion(XAbstractNetIoRing* ring, const XAbstractNetIoRing_CQEntry* entry) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(entry, "CQEntry") || !ring->m_cq)
        return false;
    return XLockFreeQueue_push_base(ring->m_cq, (void*)entry);
}

void XAbstractNetIoRing_drainCQ(XAbstractNetIoRing* ring) {
    XAbstractNetIoRing_CQEntry entry;

    if (ISNULL(ring, "XAbstractNetIoRing") || !ring->m_cq)
        return;

    /* 逐条取出 CQ 条目，通过虚函数分发到应用层 */
    while (XLockFreeQueue_receive_base(ring->m_cq, &entry)) {
        XAbstractNetIoRing_dispatchCQEntry_base(ring, &entry);
    }
}

void XAbstractNetIoRing_processReady(XAbstractNetIoRing* ring) {
    XAbstractNetIoRing_SQEntry sqEntry;

    if (ISNULL(ring, "XAbstractNetIoRing"))
        return;

    /* 1. 轮询平台 I/O（IOCP/epoll），完成事件推入 CQ */
    XAbstractNetIoRing_pollPlatform_base(ring);

    /* 2. 排空 SQ，对每条调用 ProcessSource 虚函数 */
    while (XLockFreeQueue_receive_base(ring->m_sq, &sqEntry)) {
        XAbstractNetIoRing_processSource_base(ring, &sqEntry);
    }

    /* 3. 排空 CQ，通过 DispatchCQEntry 虚函数投递完成事件到应用层 */
    XAbstractNetIoRing_drainCQ(ring);
}

bool XAbstractNetIoRing_hasSQPending(const XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || !ring->m_sq)
        return false;
    return !XLockFreeQueue_isEmpty_base(ring->m_sq);
}

bool XAbstractNetIoRing_hasCQPending(const XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || !ring->m_cq)
        return false;
    return !XLockFreeQueue_isEmpty_base(ring->m_cq);
}

bool XAbstractNetIoRing_isEnabled(const XAbstractNetIoRing* ring) {
    return ring ? ring->m_enabled : false;
}

/* ================================================================
 * 虚函数调度入口（通过 vtable 分发到平台后端实现）
 * ================================================================ */
XFd XAbstractNetIoRing_getEventFd_base(XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return XFD_INVALID;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_GetEventFd, XFd(*)(XAbstractNetIoRing*))(ring);
}

void XAbstractNetIoRing_processSource_base(XAbstractNetIoRing* ring, const XAbstractNetIoRing_SQEntry* entry) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable") || !entry)
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_ProcessSource,
        void(*)(XAbstractNetIoRing*, const XAbstractNetIoRing_SQEntry*))(ring, entry);
}

void XAbstractNetIoRing_pollPlatform_base(XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_PollPlatform, void(*)(XAbstractNetIoRing*))(ring);
}

bool XAbstractNetIoRing_hasPendingInput_base(const XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return false;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_HasPendingInput, bool(*)(const XAbstractNetIoRing*))(ring);
}

bool XAbstractNetIoRing_registerEvent_base(XAbstractNetIoRing* ring, XFd fd) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return false;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_RegisterEvent, bool(*)(XAbstractNetIoRing*, XFd))(ring, fd);
}

bool XAbstractNetIoRing_unregisterEvent_base(XAbstractNetIoRing* ring, XFd fd) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return false;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_UnregisterEvent, bool(*)(XAbstractNetIoRing*, XFd))(ring, fd);
}

void XAbstractNetIoRing_dispatchCQEntry_base(XAbstractNetIoRing* ring, const XAbstractNetIoRing_CQEntry* entry) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable") || !entry)
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_DispatchCQEntry,
        void(*)(XAbstractNetIoRing*, const XAbstractNetIoRing_CQEntry*))(ring, entry);
}

void XAbstractNetIoRing_waitForEvents_base(XAbstractNetIoRing* ring, int timeoutMs) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_WaitForEvents,
        void(*)(XAbstractNetIoRing*, int))(ring, timeoutMs);
}

void XAbstractNetIoRing_wakeUp_base(XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_WakeUp,
        void(*)(XAbstractNetIoRing*))(ring);
}

#endif /* XAbstractNetIoRing_ON */
