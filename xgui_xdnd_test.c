/**
 * @file        xgui_xdnd_test.c
 * @brief       Linux X11 XDND 真实跨客户端端到端测试。
 * @details     该测试以第二个 X11 客户端模拟拖放源，向 XGui 原生窗口依次
 *              发送 XdndEnter、XdndPosition、XdndDrop，并响应目标窗口的
 *              SelectionRequest。它验证 XGui 的 XDND 接收端会回送
 *              XdndStatus/XdndFinished，且 text/uri-list 最终以 XDropEvent
 *              交给 XWindow，而非仅验证公共事件构造函数。
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "CXinYueConfig.h"
#include "XGuiApplication.h"
#include "XPlatformIntegration.h"
#include "XPlatformNativeInterface.h"
#include "XPlatformNativeWindow.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "XEvent.h"
#include "XMemory.h"
#include "XString.h"

#if defined(__linux__) && defined(XINYUE_C_HAS_X11) && \
    XGUIAPPLICATION_ON && XWINDOW_ON && XPLATFORMINTEGRATION_ON && \
    XPLATFORMNATIVEWINDOW_ON

/* Xlib 与 XGui 公共类型同名；此处与 Drive/Posix 后端采用相同隔离方式。 */
#undef XFree
#define XImage X11_XImage
#define XPoint X11_XPoint
#define XEvent X11_XEvent
#define XColor X11_XColor
#define XKeyEvent X11_XKeyEvent
#define XExposeEvent X11_XExposeEvent
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#undef XImage
#undef XPoint
#undef XEvent
#undef XColor
#undef XKeyEvent
#undef XExposeEvent
#define XFree XMemory_free

XCLASS_DEFINE_BEGING(XdndProbeWindow)
XCLASS_DEFINE_EXTEND_END(XdndProbeWindow, XWindow)

typedef struct XdndProbeWindow
{
    XWindow m_base;
    int dragEnterCount;
    int dragMoveCount;
    int dropCount;
    XPoint dropPosition;
    char mime[64];
    char data[256];
} XdndProbeWindow;

static void xdnd_acceptEvent(XWindow* self, XEvent* event)
{
    (void)self;
    XEvent_accept(event);
}

static void xdnd_dropEvent(XWindow* self, XEvent* event)
{
    XdndProbeWindow* probe = (XdndProbeWindow*)self;
    XDropEvent* drop = (XDropEvent*)event;
    XString* mime = XDropEvent_mimeType(drop);
    XString* data = XDropEvent_data(drop);
    ++probe->dropCount;
    probe->dropPosition = XDropEvent_position(drop);
    if (mime) {
        strncpy(probe->mime, XString_toUtf8(mime), sizeof(probe->mime) - 1u);
        XString_delete_base((XClass*)mime);
    }
    if (data) {
        strncpy(probe->data, XString_toUtf8(data), sizeof(probe->data) - 1u);
        XString_delete_base((XClass*)data);
    }
    XEvent_accept(event);
}

static void xdnd_dragEnterEvent(XWindow* self, XEvent* event)
{
    ++((XdndProbeWindow*)self)->dragEnterCount;
    xdnd_acceptEvent(self, event);
}

static void xdnd_dragMoveEvent(XWindow* self, XEvent* event)
{
    ++((XdndProbeWindow*)self)->dragMoveCount;
    xdnd_acceptEvent(self, event);
}

static XVtable* XdndProbeWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XdndProbeWindow)
    XVTABLE_INHERIT_XCLASS(XWindow);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_DragEnterEvent, xdnd_dragEnterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_DragMoveEvent, xdnd_dragMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_DropEvent, xdnd_dropEvent);
    return XVTABLE_DEFAULT;
}

static XdndProbeWindow* xdnd_probeCreate(void)
{
    XdndProbeWindow* probe = (XdndProbeWindow*)XMemory_malloc(
        sizeof(*probe), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!probe) return NULL;
    memset(probe, 0, sizeof(*probe));
    XWindow_init(&probe->m_base);
    XClassSetVtable(probe, XdndProbeWindow);
    Set_Class_Memory(probe, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(probe, true);
    return probe;
}

static void xdnd_sendClientMessage(Display* display, Window destination,
                                   Atom messageType, long d0, long d1,
                                   long d2, long d3, long d4)
{
    X11_XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = destination;
    event.xclient.message_type = messageType;
    event.xclient.format = 32;
    event.xclient.data.l[0] = d0;
    event.xclient.data.l[1] = d1;
    event.xclient.data.l[2] = d2;
    event.xclient.data.l[3] = d3;
    event.xclient.data.l[4] = d4;
    XSendEvent(display, destination, False, NoEventMask, &event);
    XFlush(display);
}

static bool xdnd_serveSelection(Display* sourceDisplay, Atom uriList,
                                Atom xdndStatus, Atom xdndFinished,
                                int* statusCount, int* finishedCount)
{
    bool served = false;
    const char payload[] = "file:///tmp/xgui-xdnd-e2e.txt\r\n";
    while (XPending(sourceDisplay) > 0) {
        X11_XEvent event;
        XNextEvent(sourceDisplay, &event);
        if (event.type == SelectionRequest) {
            XSelectionRequestEvent* request = &event.xselectionrequest;
            X11_XEvent reply;
            memset(&reply, 0, sizeof(reply));
            reply.xselection.type = SelectionNotify;
            reply.xselection.display = request->display;
            reply.xselection.requestor = request->requestor;
            reply.xselection.selection = request->selection;
            reply.xselection.target = request->target;
            reply.xselection.time = request->time;
            reply.xselection.property = None;
            if (request->target == uriList && request->property != None) {
                XChangeProperty(sourceDisplay, request->requestor,
                                request->property, uriList, 8,
                                PropModeReplace,
                                (const unsigned char*)payload,
                                (int)(sizeof(payload) - 1u));
                reply.xselection.property = request->property;
                served = true;
            }
            XSendEvent(sourceDisplay, request->requestor, False, NoEventMask,
                       &reply);
            XFlush(sourceDisplay);
        } else if (event.type == ClientMessage) {
            if (event.xclient.message_type == xdndStatus) ++*statusCount;
            if (event.xclient.message_type == xdndFinished) ++*finishedCount;
        }
    }
    return served;
}

static void xdnd_countReplies(Display* sourceDisplay, Atom xdndStatus,
                              Atom xdndFinished, int* statusCount,
                              int* finishedCount)
{
    while (XPending(sourceDisplay) > 0) {
        X11_XEvent event;
        XNextEvent(sourceDisplay, &event);
        if (event.type != ClientMessage) continue;
        if (event.xclient.message_type == xdndStatus) ++*statusCount;
        if (event.xclient.message_type == xdndFinished) ++*finishedCount;
    }
}

int main(int argc, char** argv)
{
    XGuiApplication* app;
    XPlatformNativeInterface* nativeInterface;
    XPlatformIntegration* integration;
    XdndProbeWindow* target;
    Display* sourceDisplay;
    Window source;
    XWindowId targetId;
    Atom xdndEnter;
    Atom xdndPosition;
    Atom xdndDrop;
    Atom xdndStatus;
    Atom xdndFinished;
    Atom xdndSelection;
    Atom uriList;
    int statusCount = 0;
    int finishedCount = 0;
    bool selectionServed = false;
    int round;
    int result = 1;

    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        fprintf(stderr, "XGui XDND: cannot create XGuiApplication\n");
        return 1;
    }
    nativeInterface = XGuiApplication_platformNativeInterface();
    integration = nativeInterface ?
        XPlatformNativeInterface_integration(nativeInterface) : NULL;
    if (!integration || !XPlatformIntegration_isNativeWindowAvailable(integration)) {
        printf("XGui XDND: skipped (no usable X11 display)\n");
        result = 0;
        goto done_app;
    }
    target = xdnd_probeCreate();
    if (!target || !XPlatformIntegration_createPlatformWindow(
                       integration, (XWindow*)target)) {
        fprintf(stderr, "XGui XDND: cannot create target window\n");
        goto done_target;
    }
    XWindow_setGeometry((XWindow*)target, 30, 30, 240, 160);
    XWindow_showNormal((XWindow*)target);
    XPlatformNativeWindow_processPendingEvents();
    targetId = XWindow_winId((XWindow*)target);
    if (targetId == 0) {
        fprintf(stderr, "XGui XDND: target has no native X11 window\n");
        goto done_target;
    }

    sourceDisplay = XOpenDisplay(NULL);
    if (!sourceDisplay) {
        fprintf(stderr, "XGui XDND: cannot open source X11 client\n");
        goto done_target;
    }
    source = XCreateSimpleWindow(sourceDisplay,
                                 RootWindow(sourceDisplay,
                                            DefaultScreen(sourceDisplay)),
                                 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(sourceDisplay, source, PropertyChangeMask);
    xdndEnter = XInternAtom(sourceDisplay, "XdndEnter", False);
    xdndPosition = XInternAtom(sourceDisplay, "XdndPosition", False);
    xdndDrop = XInternAtom(sourceDisplay, "XdndDrop", False);
    xdndStatus = XInternAtom(sourceDisplay, "XdndStatus", False);
    xdndFinished = XInternAtom(sourceDisplay, "XdndFinished", False);
    xdndSelection = XInternAtom(sourceDisplay, "XdndSelection", False);
    uriList = XInternAtom(sourceDisplay, "text/uri-list", False);
    XSetSelectionOwner(sourceDisplay, xdndSelection, source, CurrentTime);
    XSync(sourceDisplay, False);
    if (XGetSelectionOwner(sourceDisplay, xdndSelection) != source) {
        fprintf(stderr, "XGui XDND: source did not become selection owner\n");
        goto done_source;
    }

    xdnd_sendClientMessage(sourceDisplay, (Window)targetId, xdndEnter,
                           (long)source, 5L << 24, (long)uriList, None, None);
    xdnd_sendClientMessage(sourceDisplay, (Window)targetId, xdndPosition,
                           (long)source, 0, (24L << 16) | 36L,
                           CurrentTime, None);
    /* 让源客户端确认服务器已接收 Enter/Position；仅 XFlush 时，繁忙
       回归进程中目标事件泵可能在请求尚未进入服务器队列前就结束探测。 */
    XSync(sourceDisplay, False);
    for (round = 0; round < 32 && statusCount == 0; ++round) {
        (void)XPlatformNativeWindow_waitForEvents(20);
        usleep(2000);
        xdnd_countReplies(sourceDisplay, xdndStatus, xdndFinished,
                          &statusCount, &finishedCount);
    }
    if (target->dragEnterCount != 1 || target->dragMoveCount != 1 ||
        statusCount != 1) {
        fprintf(stderr, "XGui XDND: enter=%d move=%d status=%d\n",
                target->dragEnterCount, target->dragMoveCount, statusCount);
        goto done_source;
    }

    xdnd_sendClientMessage(sourceDisplay, (Window)targetId, xdndDrop,
                           (long)source, 0, CurrentTime, 0, 0);
    XSync(sourceDisplay, False);
    for (round = 0; round < 128 && target->dropCount == 0; ++round) {
        (void)XPlatformNativeWindow_waitForEvents(20);
        /* SelectionRequest 由另一 X11 client 产生；给服务器一次调度机会，
           否则紧密的无阻塞轮询可能在事件抵达前提前结束。 */
        usleep(5000);
        if (xdnd_serveSelection(sourceDisplay, uriList,
                                xdndStatus, xdndFinished,
                                &statusCount, &finishedCount))
            selectionServed = true;
        (void)XPlatformNativeWindow_processPendingEvents();
        xdnd_countReplies(sourceDisplay, xdndStatus, xdndFinished,
                          &statusCount, &finishedCount);
    }
    /* DROP 事件是同步派发的，XdndFinished 则是随后送回源客户端的独立
       ClientMessage；不要因目标窗口已收到数据而提前结束验证。 */
    for (round = 0; round < 32 && finishedCount == 0; ++round) {
        usleep(2000);
        xdnd_countReplies(sourceDisplay, xdndStatus, xdndFinished,
                          &statusCount, &finishedCount);
    }
    if (!selectionServed || target->dropCount != 1 || finishedCount != 1 ||
        target->dropPosition.x != 24 || target->dropPosition.y != 36 ||
        strcmp(target->mime, "text/uri-list") != 0 ||
        strcmp(target->data, "file:///tmp/xgui-xdnd-e2e.txt\r\n") != 0) {
        fprintf(stderr,
                "XGui XDND: served=%d drop=%d finished=%d pos=(%d,%d) mime='%s' data='%s'\n",
                selectionServed ? 1 : 0, target->dropCount, finishedCount,
                target->dropPosition.x, target->dropPosition.y,
                target->mime, target->data);
        goto done_source;
    }
    printf("XGui XDND: XdndEnter/Position/Drop selection transfer passed\n");
    result = 0;

done_source:
    XDestroyWindow(sourceDisplay, source);
    XCloseDisplay(sourceDisplay);
done_target:
    if (target) XWindow_delete_base((XClass*)target);
done_app:
    XGuiApplication_delete_base(app);
    return result;
}

#else

int main(void)
{
    printf("XGui XDND: skipped (not a Linux X11 build)\n");
    return 0;
}

#endif
