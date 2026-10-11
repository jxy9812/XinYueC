/**
 * @file XNetIoRingWin32.c
 * @brief XAbstractNetIoRing Windows IOCP ????
 *
 * ?? XAbstractNetIoRing,????????? Windows IOCP socket/file ?? I/O?
 * ?????? IOCP ????,??? lwIP pcap ??
 * (??? XAbstractNetIoRing.c ? pollLwip() ??)?
 *
 * ????:
 *   pollPlatform:
 *     GetQueuedCompletionStatus(timeout=0) ??
 *       -> XEventContext_IOCP ?? eventMask / fd / bytes
 *       -> ??? CQ ???? CQ
 *
 *   dispatchCQEntry(??????):
 *     ? CQ ???? XEventSockAct / XEventSockClose
 *       -> XCoreApplication_postEvent ??????
 *       -> 0 ?? + Read + IOCP ?? => XEventSockClose(?? FIN)
 *
 *   waitForEvents:
 *     GetQueuedCompletionStatus(timeout) ????
 *       -> wakeUp ?? PostQueuedCompletionStatus(NULL overlapped) ??
 *
 * ????:
 *   ???? XAbstractNetIoRing(? class_init + 9 ????????),
 *   ??? 6 ? IOCP ?????:GetEventFd?PollPlatform?RegisterEvent?
 *   WaitForEvents?WakeUp?Deinit????????????
 */

#include "CXinYueConfig.h"
#if XAbstractNetIoRing_ON

#if XPLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "XAbstractNetIoRing.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XSocketDescriptor.h"
#include <winsock2.h>
#include "XNetIoRingWin32.h"
#include <string.h>
#include <stdlib.h>

/* ?? pollPlatform ????? IOCP ???,???????? */
#ifndef XNETIORING_WIN32_POLL_BATCH
#define XNETIORING_WIN32_POLL_BATCH  64
#endif

/* ================================================================
 * XNetIoRingWin32 ???(?????? + ???????)
 * ?? API ????????? XNetIoRingWin32.h
 * ================================================================ */

/* ??????:?? XAbstractNetIoRing,??????? */
XCLASS_DEFINE_BEGING(XNetIoRingWin32)
XCLASS_DEFINE_EXTEND_END(XNetIoRingWin32, XAbstractNetIoRing)

typedef struct XNetIoRingWin32 {
    XAbstractNetIoRing m_class;   /**< ??(???????) */
    HANDLE  m_iocp;               /**< IOCP ?????? */
    bool    m_ownsIocp;           /**< ???????? IOCP(false=??????) */
    uint8_t m_pad[7];             /**< ???? */
} XNetIoRingWin32;

/* ==================== ???? ==================== */
static XFd   VXNetIoRingWin32_getEventFd(XAbstractNetIoRing* self);
static void  VXNetIoRingWin32_pollPlatform(XAbstractNetIoRing* self);
static bool  VXNetIoRingWin32_registerEvent(XAbstractNetIoRing* self, XFd fd);
static void  VXNetIoRingWin32_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void  VXNetIoRingWin32_wakeUp(XAbstractNetIoRing* self);
static void  VXNetIoRingWin32_deinit(XAbstractNetIoRing* obj);

/* ================================================================
 * IOCP ????(??????)
 * ================================================================ */

/* ??? IOCP ????? CQ ?????????????
 * pollPlatform(?????)? waitForEvents(????)??? */
static void processOneCompletion(XAbstractNetIoRing* self, BOOL success,
                                  DWORD bytesTransferred, ULONG_PTR completionKey,
                                  LPOVERLAPPED overlapped, DWORD lastError) {
    XEventContext* ctx = (XEventContext*)overlapped;
    XEventContext_IOCP* ioCtx = (XEventContext_IOCP*)overlapped;
    XEventContextCompletionCallback completionCallback = NULL;
    void* completionUserData = NULL;

    if (ctx->type == XEventContextType_Type_Timer) {
        /* ?????:????????????? */
        if (completionKey) {
            XTimerEvent* timerEv = XTimerEvent_create((XTimerId)ctx->fd);
            XEvent* timerEvent = (XEvent*)timerEv;
            if (timerEvent) {
                timerEvent->m_posted = true;
                timerEvent->m_spontaneous = true;
                XCoreApplication_postEvent((XObject*)completionKey,
                                           timerEvent, XEVENT_PRIORITY_NORMAL);
            }
        }
    }
    else if (ctx->type == XEventContextType_Type_Socket ||
             ctx->type == XEventContextType_Type_File) {
        bool shouldDispatch = true;
        completionCallback = ioCtx->completionCallback;
        completionUserData = ioCtx->completionUserData;

        /* Socket/File I/O ??:??? CQ ?? */
        XAbstractNetIoRing_CQEntry cqEntry;
        memset(&cqEntry, 0, sizeof(cqEntry));

        cqEntry.m_fd = ctx->fd;
        cqEntry.m_bytes = bytesTransferred;
        cqEntry.m_error = (int)lastError;
        cqEntry.m_sourceType = XAbstractNetIoRing_Source_NativeIO;  /* ?? I/O ??(??? lwIP ? Source_Netif) */
    cqEntry.m_fdType = XFd_type(ctx->fd);  /* ? fd ?????(Socket/File) */

        /* IOCP eventMask (FD_*) -> XSocketActType ??
         * ??:FD_CONNECT(16) != XSocketActType_Connect(4),????????? */
        {
            uint32_t events = 0;
            if (ioCtx->eventMask & FD_READ)    events |= XSocketActType_Read;     /* FD_READ(1)     -> Read(1)    */
            if (ioCtx->eventMask & FD_WRITE)   events |= XSocketActType_Write;    /* FD_WRITE(2)    -> Write(2)   */
            if (ioCtx->eventMask & FD_CONNECT) events |= XSocketActType_Connect;  /* FD_CONNECT(16) -> Connect(4) */
            if (ioCtx->eventMask & FD_ACCEPT)  events |= XSocketActType_Accept;   /* FD_ACCEPT(8)   -> Accept(8)  */
            cqEntry.m_events = events;
        }

        /* I/O ???:??????????? Connect ?? */
        if (!success && cqEntry.m_events == 0) {
            cqEntry.m_events = XSocketActType_Connect;
        }

        ioCtx->finishedBytes = bytesTransferred;
        ioCtx->nativeError = success ? ERROR_SUCCESS : lastError;
        ioCtx->completed = success != FALSE;

        /* The callback may release the allocation containing ioCtx.  Copy
         * every needed field first and do not touch ioCtx afterwards.  A
         * canceled operation whose owner is being destroyed is consumed
         * here without dispatching an event through a potentially reused fd. */
        if (completionCallback)
            shouldDispatch = completionCallback(ioCtx, completionUserData);
        if (shouldDispatch)
            XAbstractNetIoRing_pushCompletion(self, &cqEntry);
    }
}

/* ================================================================
 * ???????(? IOCP ??,??????????)
 * ================================================================ */

/* ?? IOCP ????(???),???????? CQ ?? */
static void VXNetIoRingWin32_pollPlatform(XAbstractNetIoRing* self) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = NULL;
    int batchCount = 0;

    if (!win || !win->m_iocp) return;

    /* ??????? IOCP ??? */
    while (batchCount < XNETIORING_WIN32_POLL_BATCH) {
        BOOL success = GetQueuedCompletionStatus(
            win->m_iocp, &bytesTransferred, &completionKey, &overlapped, 0);

        /* ??????(??/???),???? */
        if (overlapped == NULL) break;

        DWORD lastError = success ? 0 : GetLastError();
        processOneCompletion(self, success, bytesTransferred,
                             completionKey, overlapped, lastError);
        batchCount++;
    }
}

/* ?????? fd:?? IOCP ????????? */
static XFd VXNetIoRingWin32_getEventFd(XAbstractNetIoRing* self) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    if (!win || !win->m_iocp) return XFD_INVALID;
    return (XFd)(intptr_t)win->m_iocp;
}

/* ?????:?? socket/file HANDLE ? IOCP ?? */
static bool VXNetIoRingWin32_registerEvent(XAbstractNetIoRing* self, XFd fd) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    XFileDescriptor* desc;

    if (!win || !win->m_iocp) return false;

    desc = XFd_get(fd);
    if (!desc || !desc->m_deviceCtx) return false;
    if (desc->m_type == XFD_TYPE_CONSOLE) return false;

    return CreateIoCompletionPort((HANDLE)desc->m_deviceCtx, win->m_iocp,
                                  (ULONG_PTR)desc->object, 0) != NULL;
}

/* ??????(???) */
static void VXNetIoRingWin32_waitForEvents(XAbstractNetIoRing* self, int timeoutMs) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = NULL;

    if (!win || !win->m_iocp) return;

    DWORD winTimeout = (timeoutMs < 0) ? INFINITE : (DWORD)timeoutMs;
    BOOL success = GetQueuedCompletionStatus(
        win->m_iocp, &bytesTransferred, &completionKey, &overlapped, winTimeout);

    /* overlapped==NULL ????? wakeUp ??,?????? */
    if (overlapped == NULL) return;

    DWORD lastError = success ? 0 : GetLastError();
    processOneCompletion(self, success, bytesTransferred,
                         completionKey, overlapped, lastError);
}

/* ?????? WaitForEvents(???) */
static void VXNetIoRingWin32_wakeUp(XAbstractNetIoRing* self) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    if (!win || !win->m_iocp) return;
    PostQueuedCompletionStatus(win->m_iocp, 0, 0, NULL);
}

/* ================================================================
 * deinit ????
 *
 * ????:
 *   1. Win32 ????(IOCP ??)
 *   2. ???? deinit(?? SQ/CQ ?? + XClass ??)
 * ================================================================ */
static void VXNetIoRingWin32_deinit(XAbstractNetIoRing* obj) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)obj;

    if (!win) return;

    /* ?? IOCP ??(?????????) */
    if (win->m_ownsIocp && win->m_iocp) {
        CloseHandle(win->m_iocp);
    }
    win->m_iocp = NULL;
    win->m_ownsIocp = false;

    /* ???? deinit(?? SQ/CQ ?? + XClass ??) */
    XClass_Deinit_Parent(XAbstractNetIoRing, obj);
}

/* ================================================================
 * ???????
 *
 * ???? XAbstractNetIoRing ?????(? 9 ?????),
 * ??? 6 ? IOCP ??????
 * ================================================================ */
XVtable* XNetIoRingWin32_class_init(void) {
    XVTABLE_INIT_DEFAULT(XNetIoRingWin32)
    /* ?? XAbstractNetIoRing ????(?????) */
    XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing);

    /* ?? IOCP ????? */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_GetEventFd,    VXNetIoRingWin32_getEventFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_PollPlatform,  VXNetIoRingWin32_pollPlatform);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterEvent, VXNetIoRingWin32_registerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WaitForEvents, VXNetIoRingWin32_waitForEvents);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WakeUp,        VXNetIoRingWin32_wakeUp);

    /* ?? deinit */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetIoRingWin32_deinit);

    return XVTABLE_DEFAULT;
}

/* ================================================================
 * ??????
 * ================================================================ */
void XNetIoRingWin32_init(XNetIoRingWin32* ring) {
    if (!ring) return;

    /* ????????? */
    memset(ring, 0, sizeof(XNetIoRingWin32));

    /* ?????(???????? + ?? SQ/CQ ??) */
    XAbstractNetIoRing_init((XAbstractNetIoRing*)ring);

    /* ??????????(???????? + IOCP ??) */
    XClassSetVtable(ring, XNetIoRingWin32);

    /* ?? IOCP ???? */
    ring->m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    ring->m_ownsIocp = (ring->m_iocp != NULL);

    /* ???? */
    ((XAbstractNetIoRing*)ring)->m_enabled = (ring->m_iocp != NULL);
}

XNetIoRingWin32* XNetIoRingWin32_create_ex(XMemoryType memory) {
    XNetIoRingWin32* ring = (XNetIoRingWin32*)XMemory_malloc(sizeof(XNetIoRingWin32), memory);
    if (!ring) return NULL;

    XNetIoRingWin32_init(ring);
    Set_Class_Memory(ring, memory); Set_Class_IsHeap(ring, true);
    return ring;
}

/* ================================================================
 * Win32 ?? API
 * ================================================================ */
HANDLE XNetIoRingWin32_iocpHandle(const XNetIoRingWin32* ring) {
    return ring ? ring->m_iocp : NULL;
}

bool XNetIoRingWin32_assocHandle(XNetIoRingWin32* ring, HANDLE handle,
                                  ULONG_PTR completionKey) {
    if (!ring || !ring->m_iocp || !handle) return false;
    return CreateIoCompletionPort(handle, ring->m_iocp, completionKey, 0) != NULL;
}

bool XNetIoRingWin32_postCompletion(XNetIoRingWin32* ring,
                                     DWORD bytesTransferred,
                                     ULONG_PTR completionKey,
                                     LPOVERLAPPED overlapped) {
    if (!ring || !ring->m_iocp) return false;
    return PostQueuedCompletionStatus(ring->m_iocp, bytesTransferred,
                                      completionKey, overlapped) != 0;
}

/* ================================================================
 * IOCP ????(? XDeviceNetwork_win32.c / XSerialPortWin32.c ??)
 * ================================================================ */
HANDLE IOCP_getGlobalPort(void) {
    XNetIoRingWin32* ring = (XNetIoRingWin32*)XAbstractNetIoRing_global();
    return ring ? XNetIoRingWin32_iocpHandle(ring) : NULL;
}

bool IOCP_bind(XSocketDescriptor socket, XObject* obj) {
    XNetIoRingWin32* ring = (XNetIoRingWin32*)XAbstractNetIoRing_global();
    if (!ring) return false;
    return XNetIoRingWin32_assocHandle(ring,
        (HANDLE)XSocketDescriptor_toIntptr(socket), (ULONG_PTR)obj);
}

/* ================================================================
 * ????:?? Windows IOCP ??
 * ================================================================ */
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return (XAbstractNetIoRing*)XNetIoRingWin32_create();
}

#endif /* XPLATFORM_WINDOWS */

#endif /* XAbstractNetIoRing_ON */
