/******************************************************************************
 * @file       XPlatformNativeWindow_posix.c
 * @brief      Linux X11 平台原生窗口后端（对标 Qt 6.8 的 xcb 平台窗口插件）。
 * @details    本文件实现 XPlatformNativeWindow 契约的 Linux X11 端：
 *             - 每进程单 Display* 连接（首用惰性 XOpenDisplay(NULL)），
 *               连接失败即整体不可用，XWindow 回落嵌入式虚拟 WId；
 *             - 静态 64 槽窗口注册表，Window <-> XWindow* 双向查找，
 *              用于 winId()/windowForWinId() 与事件路由；
 *             - 视觉选择：优先 32 位 TrueColor（RGBA8888），失败回退 24
 *               位 TrueColor；上屏按服务器字节序直拷或 24 位重排后
 *               XPutImage 提交；
 *             - 事件：Expose -> handleExposeEvent（重绘），
 *               ConfigureNotify -> 先更新本后端几何记录再
 *               handleGeometryChange（防回环），FocusIn/Out -> 焦点注入，
 *               WM_DELETE_WINDOW ClientMessage -> handleCloseEvent 接受后
 *               隐藏并销毁原生窗口；
 *             - 阻塞等待用 poll(XConnectionNumber(dpy))，避免 busy loop；
 *             - WM 属性：XStoreName + _NET_WM_NAME(UTF8_STRING)。
 *             窗口映射/几何/标题同步全部围绕 XWindow 驱动，
 *             setGeometry 按本后端记录几何去重，杜绝 ConfigureNotify
 *             与 setGeometry 互相触发造成递归震荡。
 * @note       本文件只在「Linux + 已检出 X11 头/库」时参与编译（宏
 *             XINYUE_C_HAS_X11 由 CMake 在 find_package(X11) 成功后注入），
 *             并受 XPLATFORMNATIVEWINDOW_ON 与
 *             XPLATFORMNATIVEWINDOW_X11_ON 两个配置开关约束；其余平台/
 *             配置由 XPlatformNativeWindow_unsupported.c 兜底。
 *             本文件为单线程主循环设计（主线程持有 Display），进程内部
 *             XOpenDisplay 后不再加锁（Qt xcb 同理单线程访问连接）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformNativeWindow.h"

#if XPLATFORMNATIVEWINDOW_ON && XPLATFORMNATIVEWINDOW_X11_ON && XWINDOW_ON

#if defined(__linux__) && defined(XINYUE_C_HAS_X11)

#include "XWindow.h"
#include "XWindowSystemInterface.h"
#include "XWindowEvent.h"
#include "XGuiApplication.h"
#include "XPlatformDrag.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XString.h"
#include "XGeometry.h"
#include "XMemory.h"

/* Xlib 与 XinYueC 公共层存在以下命名冲突：
 *   - XImage/XPoint/XEvent/XColor/XKeyEvent/XExposeEvent：Xlib 与公共
 *     Src 头同名 typedef，直接同时可见会触发 conflicting types；
 *   - XMemory.h 把 XFree 定义为 XMemory_free 宏别名，会吞并 Xlib 的
 *     extern int XFree() 声明。
 * 这里在包含 X11 头期间用预处理宏把 Xlib 侧符号统一改名（X11_Xxx），
 * 确保公共层类型名保持不变、X11 头内部一致性不受影响；包含结束后
 * 立即 #undef 并恢复公共 XFree 宏别名。本文件内使用 X11 原生类型处
 * 一律写改名后的 X11_XImage/X11_XEvent。 */
#undef XFree
#define XImage X11_XImage
#define XPoint X11_XPoint
#define XEvent X11_XEvent
#define XColor X11_XColor
#define XKeyEvent X11_XKeyEvent
#define XExposeEvent X11_XExposeEvent
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#undef XImage
#undef XPoint
#undef XEvent
#undef XColor
#undef XKeyEvent
#undef XExposeEvent
#define XFree XMemory_free /* 恢复公共内存释放宏别名。 */
#include <poll.h>
#include <locale.h>
#include <string.h>
#include <time.h>

/** @brief 进程内原生窗口注册表容量（静态表，单线程使用）。 */
#define XPWN_MAX_WINDOWS 64

/** @brief 首选 32 位 TrueColor 视觉深度；失败回退 24。 */
#define XPWN_DEPTH_32 32
#define XPWN_DEPTH_24 24

/** @brief 原生窗口注册表槽位（X11 Window 与公共 XWindow 对象双向登记）。 */
/** @brief 双击判定时间间隔（毫秒）；移植沿用 Qt 默认约 400ms。 */
#define XPWN_DOUBLE_CLICK_INTERVAL_MS 400
/** @brief 双击判定位置偏差上限（像素）；两次按下坐标差异在此范围内视为双击。 */
#define XPWN_DOUBLE_CLICK_DISTANCE     4

typedef struct XWNPendingEntry
{
    Window m_win;        /**< X11 原生窗口 id（0 表示空槽）。 */
    XWindow* m_window;   /**< 公共窗口对象借用指针；槽位为空时 NULL。 */
    GC m_gc;             /**< 该窗口专用图形上下文（拥有）。 */
    XRect m_client;      /**< 本后端最近一次记录的客户端几何（去重用）。 */
    bool m_keyPressed[256];    /**< 各键码当前按下状态（X11 键码 8..255），用于识别自动重复。 */
    unsigned long m_lastPressTime;    /**< 最近一次非滚轮按键的时间戳（X11 毫秒节拍）。 */
    XMouseButton m_lastPressButton;   /**< 最近一次非滚轮按键的按键。 */
    XPoint m_lastPressPos;            /**< 最近一次非滚轮按键的位置。 */
    XIC m_inputContext;               /**< XIM 输入上下文（拥有；可为 NULL）。 */
    char m_preedit[1024];             /**< 当前 UTF-8 组合文本。 */
    Window m_dragSource;               /**< 当前 XDND 源窗口；0 表示无会话。 */
    Atom m_dragTarget;                 /**< 已协商的 XDND 数据类型。 */
    XPoint m_dragPosition;             /**< 最近一次拖放位置（窗口坐标）。 */
    bool m_dropPending;                /**< 已请求 Selection，等待 SelectionNotify。 */
} XWNPendingEntry;

/** @brief 每进程 X11 连接状态。 */
static Display* g_xpwnDisplay;    /**< X11 连接；NULL 表示未连接/连接失败。 */
static int g_xpwnScreenNumber;    /**< 默认屏幕号。 */
static Visual* g_xpwnVisual;      /**< 选定 TrueColor 视觉。 */
static int g_xpwnDepth;           /**< 选定视觉深度（32 或 24）。 */
static Colormap g_xpwnColormap;   /**< 进程共享色彩映射表（拥有）。 */
static Atom g_xpwnWmDelete;       /**< WM_DELETE_WINDOW 协议原子。 */
static Atom g_xpwnWmProtocols;   /**< WM_PROTOCOLS 协议原子（ClientMessage 载体）。 */
static Atom g_xpwnNetWmName;      /**< _NET_WM_NAME 原子（可能 None）。 */
static Atom g_xpwnUtf8String;     /**< UTF8_STRING 原子（可能 None）。 */
static XIM g_xpwnInputMethod;     /**< X11 输入法方法（不可用时为 NULL）。 */
static Atom g_xpwnXdndAware;
static Atom g_xpwnXdndEnter;
static Atom g_xpwnXdndPosition;
static Atom g_xpwnXdndStatus;
static Atom g_xpwnXdndLeave;
static Atom g_xpwnXdndDrop;
static Atom g_xpwnXdndFinished;
static Atom g_xpwnXdndSelection;
static Atom g_xpwnXdndTypeList;
static Atom g_xpwnXdndActionCopy;
static Atom g_xpwnTextUriList;
static Atom g_xpwnTextPlain;
static Atom g_xpwnXdndData;
static XWNPendingEntry g_xpwnEntries[XPWN_MAX_WINDOWS]; /**< 窗口注册表。 */

/* 出站 XDND 会话只在 XPlatformDrag_exec 的同步调用期间存在。X11 的
 * SelectionRequest 必须由同一事件泵响应，因此把会话数据暂存在连接级状态。 */
static bool g_xpwnDragActive;
static bool g_xpwnDragAccepted;
static bool g_xpwnDragFinished;
static Window g_xpwnDragSource;
static Window g_xpwnDragTarget;
static XWindow* g_xpwnDragWindow;
static const XMimeData* g_xpwnDragMime;
static Atom g_xpwnDragFormat;

/* ==================== 内部工具 ==================== */

/** @brief 空槽查找；注册表满时返回 NULL。 */
static XWNPendingEntry* xpwn_findFreeSlot(void)
{
    size_t i;
    for (i = 0; i < XPWN_MAX_WINDOWS; ++i) {
        if (g_xpwnEntries[i].m_win == 0) return &g_xpwnEntries[i];
    }
    return NULL;
}

/** @brief 按公共窗口对象查找已登记槽位。 */
static XWNPendingEntry* xpwn_findByXWindow(const XWindow* window)
{
    size_t i;
    if (!window) return NULL;
    for (i = 0; i < XPWN_MAX_WINDOWS; ++i) {
        if (g_xpwnEntries[i].m_window == window) return &g_xpwnEntries[i];
    }
    return NULL;
}

/** @brief 按 X11 Window id 查找槽位。 */
static XWNPendingEntry* xpwn_findByNativeWindow(Window xwin)
{
    size_t i;
    if (xwin == 0) return NULL;
    for (i = 0; i < XPWN_MAX_WINDOWS; ++i) {
        if (g_xpwnEntries[i].m_win == xwin) return &g_xpwnEntries[i];
    }
    return NULL;
}

/** @brief 把 XIM 回调文本复制到固定 UTF-8 预编辑缓冲，超长安全截断。 */
static size_t xpwn_preeditText(const XIMText* text, char* out, size_t capacity)
{
    size_t n;
    if (!out || capacity == 0) return 0;
    out[0] = '\0';
    if (!text || !text->string.multi_byte || text->length <= 0) return 0;
    n = (size_t)text->length;
    if (n >= capacity) n = capacity - 1;
    memcpy(out, text->string.multi_byte, n);
    out[n] = '\0';
    return n;
}

static int xpwn_preeditStart(XIC inputContext, XPointer clientData,
                             XPointer callData)
{
    XWNPendingEntry* entry = (XWNPendingEntry*)clientData;
    (void)inputContext;
    (void)callData;
    if (entry) entry->m_preedit[0] = '\0';
    return -1;
}

static void xpwn_preeditDone(XIC inputContext, XPointer clientData,
                             XPointer callData)
{
    XWNPendingEntry* entry = (XWNPendingEntry*)clientData;
    (void)inputContext;
    (void)callData;
    if (!entry || !entry->m_window) return;
    entry->m_preedit[0] = '\0';
    (void)XWindowSystemInterface_handleInputMethodEvent(
        entry->m_window, "", "", 0, 0, -1, -1);
}

static void xpwn_preeditDraw(XIC inputContext, XPointer clientData,
                             XPointer callData)
{
    XWNPendingEntry* entry = (XWNPendingEntry*)clientData;
    XIMPreeditDrawCallbackStruct* draw =
        (XIMPreeditDrawCallbackStruct*)callData;
    char inserted[1024];
    size_t oldLength;
    size_t first;
    size_t removed;
    size_t insertedLength;
    size_t available;
    (void)inputContext;
    if (!entry || !entry->m_window || !draw) return;
    oldLength = strlen(entry->m_preedit);
    first = draw->chg_first < 0 ? 0u : (size_t)draw->chg_first;
    if (first > oldLength) first = oldLength;
    removed = draw->chg_length < 0 ? 0u : (size_t)draw->chg_length;
    if (removed > oldLength - first) removed = oldLength - first;
    insertedLength = xpwn_preeditText(draw->text, inserted, sizeof(inserted));
    available = sizeof(entry->m_preedit) - 1u - first;
    if (insertedLength > available) insertedLength = available;
    if (removed > available - insertedLength) removed = available - insertedLength;
    memmove(entry->m_preedit + first + insertedLength,
            entry->m_preedit + first + removed,
            oldLength - first - removed + 1u);
    if (insertedLength) memcpy(entry->m_preedit + first, inserted, insertedLength);
    (void)XWindowSystemInterface_handleInputMethodEvent(
        entry->m_window, entry->m_preedit, "", 0, 0,
        draw->caret, draw->caret);
}

/* ==================== XDND 拖放协议 ==================== */

static void xpwn_xFree(void* pointer)
{
    if (!pointer) return;
#undef XFree
    XFree(pointer);
#define XFree XMemory_free
}

static Atom xpwn_pickXdndType(const Atom* types, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (types[i] == g_xpwnTextUriList) return g_xpwnTextUriList;
    }
    for (i = 0; i < count; ++i) {
        if (types[i] == g_xpwnTextPlain || types[i] == g_xpwnUtf8String)
            return types[i];
    }
    return None;
}

static void xpwn_sendXdndStatus(const XWNPendingEntry* entry, bool accepted)
{
    X11_XEvent response;
    if (!entry || !entry->m_dragSource) return;
    memset(&response, 0, sizeof(response));
    response.xclient.type = ClientMessage;
    response.xclient.display = g_xpwnDisplay;
    response.xclient.window = entry->m_dragSource;
    response.xclient.message_type = g_xpwnXdndStatus;
    response.xclient.format = 32;
    response.xclient.data.l[0] = (long)entry->m_win;
    response.xclient.data.l[1] = accepted ? 1L : 0L;
    response.xclient.data.l[4] = accepted ? (long)g_xpwnXdndActionCopy : None;
    XSendEvent(g_xpwnDisplay, entry->m_dragSource, False, NoEventMask, &response);
    XFlush(g_xpwnDisplay);
}

static void xpwn_sendXdndFinished(const XWNPendingEntry* entry, bool accepted)
{
    X11_XEvent response;
    if (!entry || !entry->m_dragSource) return;
    memset(&response, 0, sizeof(response));
    response.xclient.type = ClientMessage;
    response.xclient.display = g_xpwnDisplay;
    response.xclient.window = entry->m_dragSource;
    response.xclient.message_type = g_xpwnXdndFinished;
    response.xclient.format = 32;
    response.xclient.data.l[0] = (long)entry->m_win;
    response.xclient.data.l[1] = accepted ? 1L : 0L;
    response.xclient.data.l[2] = accepted ? (long)g_xpwnXdndActionCopy : None;
    XSendEvent(g_xpwnDisplay, entry->m_dragSource, False, NoEventMask, &response);
    XFlush(g_xpwnDisplay);
}

static char* xpwn_readXdndData(Window window, Atom property)
{
    Atom actualType;
    int format;
    unsigned long itemCount;
    unsigned long bytesAfter;
    unsigned char* raw = NULL;
    char* data = NULL;
    if (XGetWindowProperty(g_xpwnDisplay, window, property, 0, 1 << 20,
                           True, AnyPropertyType, &actualType, &format,
                           &itemCount, &bytesAfter, &raw) != Success)
        return NULL;
    (void)actualType;
    (void)bytesAfter;
    if (raw && format == 8) {
        data = (char*)XMalloc_Hybrid((size_t)itemCount + 1u);
        if (data) {
            memcpy(data, raw, (size_t)itemCount);
            data[itemCount] = '\0';
        }
    }
    xpwn_xFree(raw);
    return data;
}

static void xpwn_resetXdnd(XWNPendingEntry* entry)
{
    if (!entry) return;
    entry->m_dragSource = 0;
    entry->m_dragTarget = None;
    entry->m_dragPosition = (XPoint){0, 0};
    entry->m_dropPending = false;
}

/** @brief 沿 X11 父链寻找声明 XdndAware 的顶层窗口。 */
static Window xpwn_findXdndTarget(Window child)
{
    Window root = 0, parent = 0, *children = NULL;
    unsigned int count = 0;
    Window current = child;
    Atom actual = None;
    int format = 0;
    unsigned long items = 0, after = 0;
    unsigned char* value = NULL;
    while (current && current != DefaultRootWindow(g_xpwnDisplay)) {
        if (XGetWindowProperty(g_xpwnDisplay, current, g_xpwnXdndAware,
                               0, 1, False, AnyPropertyType, &actual,
                               &format, &items, &after, &value) == Success) {
            xpwn_xFree(value);
            if (items > 0) return current;
        }
        value = NULL;
        if (!XQueryTree(g_xpwnDisplay, current, &root, &parent, &children,
                        &count)) break;
        xpwn_xFree(children);
        current = parent;
    }
    return 0;
}

/** @brief 处理出站 XDND 的 SelectionRequest。 */
static bool xpwn_handleDragSelectionRequest(const X11_XEvent* ev)
{
    XSelectionRequestEvent* request;
    XSelectionEvent response;
    XString* value = NULL;
    const char* utf8;
    Atom property;
    if (!g_xpwnDragActive || !ev || ev->type != SelectionRequest)
        return false;
    request = (XSelectionRequestEvent*)&ev->xselectionrequest;
    if (request->owner != g_xpwnDragSource ||
        request->selection != g_xpwnXdndSelection)
        return false;
    property = request->property != None ? request->property : request->target;
    if ((request->target == g_xpwnDragFormat ||
         request->target == g_xpwnUtf8String || request->target == XA_STRING) &&
        g_xpwnDragMime) {
        const char* format = (g_xpwnDragFormat == g_xpwnTextUriList) ?
                             "text/uri-list" : "text/plain";
        value = XMimeData_data(g_xpwnDragMime, format);
        if (!value && g_xpwnDragFormat == g_xpwnTextUriList)
            value = XMimeData_text(g_xpwnDragMime);
    }
    memset(&response, 0, sizeof(response));
    response.type = SelectionNotify;
    response.display = g_xpwnDisplay;
    response.requestor = request->requestor;
    response.selection = request->selection;
    response.target = request->target;
    response.time = request->time;
    response.property = None;
    if (value) {
        utf8 = XString_toUtf8(value);
        if (utf8) {
            XChangeProperty(g_xpwnDisplay, request->requestor, property,
                            request->target == XA_STRING ? XA_STRING :
                            g_xpwnDragFormat, 8, PropModeReplace,
                            (const unsigned char*)utf8,
                            (int)strlen(utf8));
            response.property = property;
        }
        XString_delete_base((XClass*)value);
    }
    XSendEvent(g_xpwnDisplay, request->requestor, False, 0,
               (X11_XEvent*)&response);
    XFlush(g_xpwnDisplay);
    return true;
}

/** @brief 建立进程级 X11 连接（幂等；失败后不再重试）。 */
static bool xpwn_ensureConnection(void)
{
    XVisualInfo vinfo;
    XVisualInfo* fallback = NULL;
    int fallbackDone = 0;
    if (g_xpwnDisplay) return true;
    if (g_xpwnDisplay == NULL && g_xpwnDepth != 0) return false; /* 已失败。 */
    g_xpwnDisplay = XOpenDisplay(NULL);
    if (!g_xpwnDisplay) return false;
    /* 注意：连接成功后把深度置非 0 作为「已尝试成功」标记；失败路径在
       上面已 return。这里先选视觉。 */
    g_xpwnScreenNumber = DefaultScreen(g_xpwnDisplay);
    g_xpwnDepth = XPWN_DEPTH_32;
    if (!XMatchVisualInfo(g_xpwnDisplay, g_xpwnScreenNumber,
                          XPWN_DEPTH_32, TrueColor, &vinfo)) {
        g_xpwnDepth = XPWN_DEPTH_24;
        if (!XMatchVisualInfo(g_xpwnDisplay, g_xpwnScreenNumber,
                              XPWN_DEPTH_24, TrueColor, &vinfo)) {
            /* 最后回退默认视觉（多数无 GPU 虚拟屏为 24 位）。 */
            vinfo.visual = DefaultVisual(g_xpwnDisplay, g_xpwnScreenNumber);
            vinfo.depth = DefaultDepth(g_xpwnDisplay, g_xpwnScreenNumber);
            vinfo.red_mask = vinfo.visual->red_mask;
            vinfo.green_mask = vinfo.visual->green_mask;
            vinfo.blue_mask = vinfo.visual->blue_mask;
            g_xpwnDepth = vinfo.depth;
            fallbackDone = 1;
        }
    }
    (void)fallback; (void)fallbackDone;
    g_xpwnVisual = vinfo.visual;
    g_xpwnColormap = XCreateColormap(g_xpwnDisplay,
                                     RootWindow(g_xpwnDisplay, g_xpwnScreenNumber),
                                     g_xpwnVisual, AllocNone);
    g_xpwnWmDelete = XInternAtom(g_xpwnDisplay, "WM_DELETE_WINDOW", False);
    g_xpwnWmProtocols = XInternAtom(g_xpwnDisplay, "WM_PROTOCOLS", False);
    g_xpwnUtf8String = XInternAtom(g_xpwnDisplay, "UTF8_STRING", False);
    g_xpwnNetWmName = XInternAtom(g_xpwnDisplay, "_NET_WM_NAME", False);
    g_xpwnXdndAware = XInternAtom(g_xpwnDisplay, "XdndAware", False);
    g_xpwnXdndEnter = XInternAtom(g_xpwnDisplay, "XdndEnter", False);
    g_xpwnXdndPosition = XInternAtom(g_xpwnDisplay, "XdndPosition", False);
    g_xpwnXdndStatus = XInternAtom(g_xpwnDisplay, "XdndStatus", False);
    g_xpwnXdndLeave = XInternAtom(g_xpwnDisplay, "XdndLeave", False);
    g_xpwnXdndDrop = XInternAtom(g_xpwnDisplay, "XdndDrop", False);
    g_xpwnXdndFinished = XInternAtom(g_xpwnDisplay, "XdndFinished", False);
    g_xpwnXdndSelection = XInternAtom(g_xpwnDisplay, "XdndSelection", False);
    g_xpwnXdndTypeList = XInternAtom(g_xpwnDisplay, "XdndTypeList", False);
    g_xpwnXdndActionCopy = XInternAtom(g_xpwnDisplay, "XdndActionCopy", False);
    g_xpwnTextUriList = XInternAtom(g_xpwnDisplay, "text/uri-list", False);
    g_xpwnTextPlain = XInternAtom(g_xpwnDisplay, "text/plain", False);
    g_xpwnXdndData = XInternAtom(g_xpwnDisplay, "XIN_YUE_C_XDND_DATA", False);
    (void)setlocale(LC_CTYPE, "");
    (void)XSetLocaleModifiers("");
    g_xpwnInputMethod = XOpenIM(g_xpwnDisplay, NULL, NULL, NULL);
    return true;
}

/** @brief 把矩形裁剪到图像范围；空矩形返回 false。 */
static bool xpwn_clipRectToImage(const XRect* rect, int w, int h, XRect* out)
{
    int x0, y0, x1, y1;
    if (!rect || !out) return false;
    if (rect->width <= 0 || rect->height <= 0) return false;
    x0 = rect->x;                  y0 = rect->y;
    x1 = rect->x + rect->width;    y1 = rect->y + rect->height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x1 <= x0 || y1 <= y0) return false;
    out->x = x0; out->y = y0;
    out->width = x1 - x0; out->height = y1 - y0;
    return true;
}

/** @brief 应用 _NET_WM_NAME/UTF8_STRING 与 XStoreName 到原生窗口。 */
static void xpwn_applyTitle(Window xwin, const XString* title)
{
    const char* utf8;
    size_t len;
    if (!xwin) return;
    utf8 = title ? XString_toUtf8(title) : "";
    if (!utf8) utf8 = "";
    len = title ? XString_toUtf8_length(title) : 0;
    XStoreName(g_xpwnDisplay, xwin, utf8);
    if (g_xpwnNetWmName != None && g_xpwnUtf8String != None) {
        XChangeProperty(g_xpwnDisplay, xwin, g_xpwnNetWmName,
                        g_xpwnUtf8String, 8, PropModeReplace,
                        (const unsigned char*)utf8, (int)len);
    }
}

/** @brief 把 ARGB32 缓冲的一行/一矩形直拷进 XPutImage 缓冲。 */
static void xpwn_copyRectDirect(const XImage* src, const XRect* srect,
                                uint8_t* dst, int dstBpl)
{
    const uint8_t* sbuf;
    int bpl;
    int row;
    if (!src || !srect || !dst) return;
    sbuf = XImage_constBits(src);
    bpl = XImage_bytesPerLine(src);
    if (!sbuf || bpl <= 0) return;
    for (row = 0; row < srect->height; ++row) {
        memcpy(dst + (int64_t)(srect->y + row) * dstBpl + (int64_t)srect->x * 4,
               sbuf + (int64_t)(srect->y + row) * bpl + (int64_t)srect->x * 4,
               (size_t)srect->width * 4u);
    }
}

/** @brief 把 ARGB32 缓冲重排为 24 位（丢弃高 8 位 Alpha）后入 XPutImage。 */
static void xpwn_copyRect24(const XImage* src, const XRect* srect,
                            uint8_t* dst, int dstBpl)
{
    const uint8_t* sbuf;
    int bpl;
    int row;
    int col;
    if (!src || !srect || !dst) return;
    sbuf = XImage_constBits(src);
    bpl = XImage_bytesPerLine(src);
    if (!sbuf || bpl <= 0) return;
    for (row = 0; row < srect->height; ++row) {
        const uint8_t* srow = sbuf + (int64_t)(srect->y + row) * bpl +
                              (int64_t)srect->x * 4;
        uint8_t* drow = dst + (int64_t)(srect->y + row) * dstBpl +
                        (int64_t)srect->x * 3;
        for (col = 0; col < srect->width; ++col) {
            drow[col * 3 + 0] = srow[col * 4 + 0]; /* B */
            drow[col * 3 + 1] = srow[col * 4 + 1]; /* G */
            drow[col * 3 + 2] = srow[col * 4 + 2]; /* R */
        }
    }
}

/* ==================== 键鼠翻译工具（X11 -> 无关键码） ==================== */

/** @brief 把 X11 KeySym 翻译为与平台无关的按键码（XKey 枚举或 ASCII 码位）。
 * @details 可打印 ASCII/Latin-1（0x20..0xff）直接取码位；特殊功能键
 *          按 XKey 枚举映射（取值与 Qt::Key 对齐）。无法识别的 KeySym
 *          返回 XKey_None。 */
static int xpwn_translateKey(KeySym keysym)
{
    if (keysym >= 0x20 && keysym < 0x100)
        return (int)keysym;   /* 可打印字符：ASCII/Latin-1 码位。 */
    switch (keysym) {
    case XK_BackSpace:   return XKey_Backspace;
    case XK_Tab:         return XKey_Tab;
    case XK_ISO_Left_Tab:return XKey_Backtab;
    case XK_Return:      return XKey_Return;
    case XK_Escape:      return XKey_Escape;
    case XK_Delete:      return XKey_Delete;
    case XK_Insert:      return XKey_Insert;
    case XK_Home:        return XKey_Home;
    case XK_End:         return XKey_End;
    case XK_Left:        return XKey_Left;
    case XK_Up:          return XKey_Up;
    case XK_Right:       return XKey_Right;
    case XK_Down:        return XKey_Down;
    case XK_Page_Up:     return XKey_PageUp;
    case XK_Page_Down:   return XKey_PageDown;
    case XK_Pause:       return XKey_Pause;
    case XK_Print:       return XKey_Print;
    case XK_Sys_Req:     return XKey_SysReq;
    case XK_Clear:       return XKey_Clear;
    case XK_Caps_Lock:   return XKey_CapsLock;
    case XK_Num_Lock:    return XKey_NumLock;
    case XK_Scroll_Lock: return XKey_ScrollLock;
    case XK_Shift_L:
    case XK_Shift_R:     return XKey_Shift;
    case XK_Control_L:
    case XK_Control_R:   return XKey_Control;
    case XK_Meta_L:
    case XK_Meta_R:      return XKey_Meta;
    case XK_Alt_L:
    case XK_Alt_R:       return XKey_Alt;
    case XK_ISO_Level3_Shift: return XKey_AltGr;
    /* 小键盘：语义复用到主键盘键码（KeypadModifier 由状态位补充）。 */
    case XK_KP_Enter:    return XKey_Enter;
    case XK_KP_Left:     return XKey_Left;
    case XK_KP_Up:       return XKey_Up;
    case XK_KP_Right:    return XKey_Right;
    case XK_KP_Down:     return XKey_Down;
    case XK_KP_Home:     return XKey_Home;
    case XK_KP_End:      return XKey_End;
    case XK_KP_Page_Up:  return XKey_PageUp;
    case XK_KP_Page_Down:return XKey_PageDown;
    case XK_KP_Insert:   return XKey_Insert;
    case XK_KP_Delete:   return XKey_Delete;
    case XK_KP_0: return '0';
    case XK_KP_1: return '1';
    case XK_KP_2: return '2';
    case XK_KP_3: return '3';
    case XK_KP_4: return '4';
    case XK_KP_5: return '5';
    case XK_KP_6: return '6';
    case XK_KP_7: return '7';
    case XK_KP_8: return '8';
    case XK_KP_9: return '9';
    case XK_KP_Decimal:  return '.';
    case XK_KP_Separator:return ',';
    case XK_KP_Add:      return '+';
    case XK_KP_Subtract: return '-';
    case XK_KP_Multiply: return '*';
    case XK_KP_Divide:   return '/';
    case XK_KP_Equal:    return '=';
    default:
        /* 功能键 F1..F35（XK_F1=0xffbe..XK_F35=0xffe0）。 */
        if (keysym >= XK_F1 && keysym <= XK_F35)
            return XKey_F1 + (int)(keysym - XK_F1);
        return XKey_None;
    }
}

/** @brief 把 X11 修饰键状态位掩码翻译为 XKeyboardModifiers。
 * @details 常见桌面映射：ShiftMask->Shift、ControlMask->Control、
 *          Mod1Mask->Alt、Mod4Mask->Meta、Mod2Mask->NumLock（Keypad
 *          标志）。不同桌面对 Mod1/Mod4 的指派可能有差异，此表为默认
 *          约定（Qt xcb 采用相同映射）。 */
static XKeyboardModifiers xpwn_translateModifiers(unsigned int state)
{
    XKeyboardModifiers modifiers = XKeyboardModifier_NoModifier;
    if (state & ShiftMask) modifiers |= XKeyboardModifier_ShiftModifier;
    if (state & ControlMask) modifiers |= XKeyboardModifier_ControlModifier;
    if (state & Mod1Mask) modifiers |= XKeyboardModifier_AltModifier;
    if (state & Mod4Mask) modifiers |= XKeyboardModifier_MetaModifier;
    if (state & Mod2Mask) modifiers |= XKeyboardModifier_KeypadModifier;
    return modifiers;
}

/** @brief 把 X11 物理按键编号翻译为 XMouseButton（滚轮键 4..7 除外）。 */
static XMouseButton xpwn_translateButton(unsigned int button)
{
    switch (button) {
    case Button1: return XMouseButton_LeftButton;
    case Button2: return XMouseButton_MiddleButton;
    case Button3: return XMouseButton_RightButton;
    case 8:       return XMouseButton_BackButton;    /* 常见后退键。 */
    case 9:       return XMouseButton_ForwardButton; /* 常见前进键。 */
    default:      return XMouseButton_NoButton;
    }
}

/** @brief 把 X11 按键状态掩码翻译为按下按键集合。
 * @details X11 的 ButtonNMask = 1<<(N+7)；Button8Mask/Button9Mask 在部分
 *          头文件缺失，这里直接用移位表达式，并兼容 8/9 号键为前进/后退。 */
static XMouseButton xpwn_translateButtonMask(unsigned int state)
{
    XMouseButton buttons = XMouseButton_NoButton;
    if (state & Button1Mask) buttons |= XMouseButton_LeftButton;
    if (state & Button2Mask) buttons |= XMouseButton_MiddleButton;
    if (state & Button3Mask) buttons |= XMouseButton_RightButton;
    if (state & (1u << 15)) buttons |= XMouseButton_BackButton;    /* Button8Mask。 */
    if (state & (1u << 16)) buttons |= XMouseButton_ForwardButton; /* Button9Mask。 */
    return buttons;
}

/* ==================== 事件泵（平台后端提供） ==================== */

/** @brief 单条 XEvent 翻译为窗口事件注入；返回是否注入了事件。 */
static bool xpwn_dispatchEvent(const X11_XEvent* ev)
{
    XWNPendingEntry* entry;
    XEventType type;
    XFocusEvent* focusEvent;
    bool accepted;
    bool delivered = false;
    if (!ev) return false;
    if (xpwn_handleDragSelectionRequest(ev)) return true;
    switch (ev->type) {
    case Expose:
        entry = xpwn_findByNativeWindow(ev->xexpose.window);
        if (entry && entry->m_window && ev->xexpose.count == 0) {
            /* count==0 表示该区域 Expose 已完成（X11 语义），此时把整块
               区域一次性注入，Qt 平台层同样合并多次 Expose。 */
            XRegion region;
            XRect rect;
            XRegion_init(&region);
            rect.x = ev->xexpose.x;
            rect.y = ev->xexpose.y;
            rect.width = ev->xexpose.width;
            rect.height = ev->xexpose.height;
            XRegion_addRect(&region, &rect);
            XWindowSystemInterface_handleExposeEvent(entry->m_window, &region);
            XRegion_deinit(&region);
            delivered = true;
        }
        break;
    case ConfigureNotify:
        entry = xpwn_findByNativeWindow(ev->xconfigure.window);
        if (entry && entry->m_window) {
            XRect client;
            /* 先更新本后端记录，再注入几何变化：setGeometry 去重比对以
               此为准，从源头切断「setGeometry -> ConfigureNotify ->
               handleGeometryChange -> setGeometry」回环。 */
            client.x = ev->xconfigure.x;
            client.y = ev->xconfigure.y;
            client.width = ev->xconfigure.width;
            client.height = ev->xconfigure.height;
            entry->m_client = client;
            XWindowSystemInterface_handleGeometryChange(entry->m_window, &client);
            delivered = true;
        }
        break;
    case FocusIn:
        /* 忽略纯指针跟随/占位焦点事件，避免误把鼠标悬停当窗口焦点。 */
        if (ev->xfocus.detail == NotifyPointer ||
            ev->xfocus.detail == NotifyPointerRoot ||
            ev->xfocus.detail == NotifyDetailNone)
            break;
        entry = xpwn_findByNativeWindow(ev->xfocus.window);
        if (entry && entry->m_window) {
            if (entry->m_inputContext) XSetICFocus(entry->m_inputContext);
            XWindowSystemInterface_handleFocusWindowChanged(
                entry->m_window, XFocusReason_ActiveWindow);
            delivered = true;
        }
        break;
    case FocusOut:
        if (ev->xfocus.detail == NotifyPointer ||
            ev->xfocus.detail == NotifyPointerRoot ||
            ev->xfocus.detail == NotifyDetailNone)
            break;
        entry = xpwn_findByNativeWindow(ev->xfocus.window);
        if (entry && entry->m_window) {
            if (entry->m_inputContext) XUnsetICFocus(entry->m_inputContext);
            /* WSI 无 FocusOut 注入入口（Qt 只有 handleFocusWindowChanged），
               这里直接自发投递 FOCUS_OUT 事件（与回归测试同一约定）。 */
            focusEvent = XFocusEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               XEVENT_TYPE_FOCUS_OUT,
                                               XFocusReason_ActiveWindow);
            if (focusEvent) {
                XGuiApplication_sendSpontaneousEvent((XObject*)entry->m_window,
                                                     (XEvent*)focusEvent);
                XEvent_delete_base((XClass*)focusEvent);
                delivered = true;
            }
        }
        break;
    case KeyPress:
    case KeyRelease:
    {
        KeySym keysym;
        int key;
        XKeyboardModifiers modifiers;
        bool autoRepeat;
        entry = xpwn_findByNativeWindow(ev->xkey.window);
        if (entry && entry->m_window) {
            if (ev->type == KeyPress && entry->m_inputContext) {
                Status status;
                KeySym imeKeysym = NoSymbol;
                char committed[1024];
                int bytes = Xutf8LookupString(entry->m_inputContext,
                    (X11_XKeyEvent*)&ev->xkey, committed,
                    (int)sizeof(committed) - 1, &imeKeysym, &status);
                if (bytes > 0) {
                    committed[bytes] = '\0';
                    if (status == XLookupChars || status == XLookupBoth) {
                        (void)XWindowSystemInterface_handleInputMethodEvent(
                            entry->m_window, "", committed, 0, 0, -1, -1);
                    }
                }
            }
            keysym = XLookupKeysym((X11_XKeyEvent*)&ev->xkey, 0);
            key = xpwn_translateKey(keysym);
            modifiers = xpwn_translateModifiers(ev->xkey.state);
            if (ev->type == KeyPress) {
                /* 自动重复识别：同一键码已在按下状态时，X11 转入重复节奏
                   （xkey 无显式标志，用按下表去重）。 */
                autoRepeat = entry->m_keyPressed[ev->xkey.keycode & 0xff];
                entry->m_keyPressed[ev->xkey.keycode & 0xff] = true;
                XWindowSystemInterface_handleKeyEvent(
                    entry->m_window, XEVENT_TYPE_KEY_PRESS, key, modifiers,
                    autoRepeat);
            } else {
                entry->m_keyPressed[ev->xkey.keycode & 0xff] = false;
                XWindowSystemInterface_handleKeyEvent(
                    entry->m_window, XEVENT_TYPE_KEY_RELEASE, key, modifiers,
                    false);
            }
            delivered = true;
        }
        break;
    }
    case ButtonPress:
        entry = xpwn_findByNativeWindow(ev->xbutton.window);
        if (entry && entry->m_window) {
            XPoint position;
            XMouseButton button;
            XMouseButton buttons;
            XKeyboardModifiers modifiers;
            position.x = ev->xbutton.x;
            position.y = ev->xbutton.y;
            modifiers = xpwn_translateModifiers(ev->xbutton.state);
            buttons = xpwn_translateButtonMask(ev->xbutton.state);
            if (ev->xbutton.button >= 4 && ev->xbutton.button <= 7) {
                /* 滚轮 4 上 / 5 下 / 6 左 / 7 右：Qt 约定 ±120/格。 */
                XPoint angleDelta;
                angleDelta.x = 0;
                angleDelta.y = 0;
                if (ev->xbutton.button == 4) angleDelta.y = 120;
                else if (ev->xbutton.button == 5) angleDelta.y = -120;
                else if (ev->xbutton.button == 6) angleDelta.x = -120;
                else if (ev->xbutton.button == 7) angleDelta.x = 120;
                XWindowSystemInterface_handleWheelEvent(
                    entry->m_window, buttons, modifiers, position,
                    &angleDelta);
            } else {
                bool isDoubleClick = false;
                unsigned long now = ev->xbutton.time;
                button = xpwn_translateButton(ev->xbutton.button);
                if (button != XMouseButton_NoButton) {
                    /* 双击识别：同一按键、时间窗口内、位置偏差在阈值内。 */
                    if (button == entry->m_lastPressButton &&
                        (now - entry->m_lastPressTime) <
                            XPWN_DOUBLE_CLICK_INTERVAL_MS &&
                        entry->m_lastPressPos.x >= position.x - XPWN_DOUBLE_CLICK_DISTANCE &&
                        entry->m_lastPressPos.x <= position.x + XPWN_DOUBLE_CLICK_DISTANCE &&
                        entry->m_lastPressPos.y >= position.y - XPWN_DOUBLE_CLICK_DISTANCE &&
                        entry->m_lastPressPos.y <= position.y + XPWN_DOUBLE_CLICK_DISTANCE) {
                        isDoubleClick = true;
                    }
                    if (isDoubleClick) {
                        XWindowSystemInterface_handleMouseEvent(
                            entry->m_window, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK,
                            button, buttons | button, modifiers, position);
                    } else {
                        XWindowSystemInterface_handleMouseEvent(
                            entry->m_window, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                            button, buttons | button, modifiers, position);
                    }
                    entry->m_lastPressTime = now;
                    entry->m_lastPressButton = button;
                    entry->m_lastPressPos = position;
                }
            }
            delivered = true;
        }
        break;
    case ButtonRelease:
        entry = xpwn_findByNativeWindow(ev->xbutton.window);
        if (entry && entry->m_window) {
            XPoint position;
            XMouseButton button;
            XKeyboardModifiers modifiers;
            position.x = ev->xbutton.x;
            position.y = ev->xbutton.y;
            modifiers = xpwn_translateModifiers(ev->xbutton.state);
            button = xpwn_translateButton(ev->xbutton.button);
            if (button != XMouseButton_NoButton) {
                /* 释放时 state 已不含本键，按下集合直接采用状态位。 */
                XMouseButton buttons = xpwn_translateButtonMask(ev->xbutton.state);
                XWindowSystemInterface_handleMouseEvent(
                    entry->m_window, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                    button, buttons, modifiers, position);
            }
            delivered = true;
        }
        break;
    case MotionNotify:
        entry = xpwn_findByNativeWindow(ev->xmotion.window);
        if (entry && entry->m_window) {
            XPoint position;
            XMouseButton buttons;
            XKeyboardModifiers modifiers;
            position.x = ev->xmotion.x;
            position.y = ev->xmotion.y;
            buttons = xpwn_translateButtonMask(ev->xmotion.state);
            modifiers = xpwn_translateModifiers(ev->xmotion.state);
            XWindowSystemInterface_handleMouseEvent(
                entry->m_window, XEVENT_TYPE_MOUSE_MOVE,
                XMouseButton_NoButton, buttons, modifiers, position);
            delivered = true;
        }
        break;
    case EnterNotify:
        /* 与 Qt xcb 一致：忽略 grab/ungrab 模式与"进入子窗口"细分，
           仅把指针真正进入本窗口的时刻注入 enter 事件。 */
        if (ev->xcrossing.mode == NotifyNormal &&
            ev->xcrossing.detail != NotifyInferior &&
            ev->xcrossing.detail != NotifyPointerRoot &&
            ev->xcrossing.detail != NotifyDetailNone) {
            entry = xpwn_findByNativeWindow(ev->xcrossing.window);
            if (entry && entry->m_window) {
                XPoint position;
                XPoint globalPosition;
                position.x = ev->xcrossing.x;
                position.y = ev->xcrossing.y;
                globalPosition.x = ev->xcrossing.x_root;
                globalPosition.y = ev->xcrossing.y_root;
                XWindowSystemInterface_handleEnterEvent(
                    entry->m_window, position, &globalPosition);
                delivered = true;
            }
        }
        break;
    case LeaveNotify:
        /* 忽略 grab/ungrab 模式与"进入子窗口"细分；指向离开子窗口
           （NotifyVirtual）时视为仍在本窗口内，不产生 leave。 */
        if (ev->xcrossing.mode == NotifyNormal &&
            ev->xcrossing.detail != NotifyVirtual &&
            ev->xcrossing.detail != NotifyPointerRoot &&
            ev->xcrossing.detail != NotifyDetailNone) {
            entry = xpwn_findByNativeWindow(ev->xcrossing.window);
            if (entry && entry->m_window) {
                XWindowSystemInterface_handleLeaveEvent(entry->m_window);
                delivered = true;
            }
        }
        break;
    case ClientMessage:
        if (ev->xclient.message_type == g_xpwnXdndEnter) {
            Atom offered[3];
            size_t offeredCount = 0;
            unsigned long flags = (unsigned long)ev->xclient.data.l[1];
            entry = xpwn_findByNativeWindow((Window)ev->xclient.window);
            if (entry && entry->m_window) {
                entry->m_dragSource = (Window)ev->xclient.data.l[0];
                if (flags & 1u) {
                    Atom actualType;
                    int format;
                    unsigned long count;
                    unsigned long after;
                    unsigned char* raw = NULL;
                    if (XGetWindowProperty(g_xpwnDisplay, entry->m_dragSource,
                                           g_xpwnXdndTypeList, 0, 3, False,
                                           XA_ATOM, &actualType, &format,
                                           &count, &after, &raw) == Success &&
                        raw && format == 32) {
                        offeredCount = count > 3 ? 3 : (size_t)count;
                        memcpy(offered, raw, offeredCount * sizeof(Atom));
                    }
                    xpwn_xFree(raw);
                } else {
                    offered[0] = (Atom)ev->xclient.data.l[2];
                    offered[1] = (Atom)ev->xclient.data.l[3];
                    offered[2] = (Atom)ev->xclient.data.l[4];
                    offeredCount = 3;
                }
                entry->m_dragTarget = xpwn_pickXdndType(offered, offeredCount);
                (void)XWindowSystemInterface_handleDropEvent(
                    entry->m_window, XEVENT_TYPE_DRAG_ENTER,
                    entry->m_dragPosition, NULL,
                    entry->m_dragTarget == g_xpwnTextUriList ? "text/uri-list" :
                    "text/plain", "");
                delivered = true;
            }
        } else if (ev->xclient.message_type == g_xpwnXdndPosition) {
            entry = xpwn_findByNativeWindow((Window)ev->xclient.window);
            if (entry && entry->m_window) {
                bool accepted;
                unsigned long packed = (unsigned long)ev->xclient.data.l[2];
                entry->m_dragPosition.x = (short)(packed >> 16);
                entry->m_dragPosition.y = (short)(packed & 0xffffu);
                accepted = entry->m_dragTarget != None &&
                    XWindowSystemInterface_handleDropEvent(
                        entry->m_window, XEVENT_TYPE_DRAG_MOVE,
                        entry->m_dragPosition, NULL,
                        entry->m_dragTarget == g_xpwnTextUriList ? "text/uri-list" :
                        "text/plain", "");
                xpwn_sendXdndStatus(entry, accepted);
                delivered = true;
            }
        } else if (ev->xclient.message_type == g_xpwnXdndLeave) {
            entry = xpwn_findByNativeWindow((Window)ev->xclient.window);
            if (entry && entry->m_window) {
                (void)XWindowSystemInterface_handleDropEvent(
                    entry->m_window, XEVENT_TYPE_DRAG_LEAVE,
                    entry->m_dragPosition, NULL, "", "");
                xpwn_resetXdnd(entry);
                delivered = true;
            }
        } else if (ev->xclient.message_type == g_xpwnXdndDrop) {
            entry = xpwn_findByNativeWindow((Window)ev->xclient.window);
            if (entry && entry->m_window && entry->m_dragSource &&
                entry->m_dragTarget != None) {
                XConvertSelection(g_xpwnDisplay, g_xpwnXdndSelection,
                                  entry->m_dragTarget, g_xpwnXdndData,
                                  entry->m_win,
                                  (Time)ev->xclient.data.l[2]);
                entry->m_dropPending = true;
                XFlush(g_xpwnDisplay);
                delivered = true;
            }
        } else if (ev->xclient.message_type == g_xpwnXdndStatus) {
            if (g_xpwnDragActive &&
                (Window)ev->xclient.window == g_xpwnDragTarget) {
                g_xpwnDragAccepted = ev->xclient.data.l[1] != 0;
                delivered = true;
            }
        } else if (ev->xclient.message_type == g_xpwnXdndFinished) {
            if (g_xpwnDragActive &&
                (Window)ev->xclient.window == g_xpwnDragTarget) {
                g_xpwnDragFinished = ev->xclient.data.l[1] != 0;
                g_xpwnDragAccepted = g_xpwnDragFinished;
                delivered = true;
            }
        } else if (ev->xclient.message_type == g_xpwnWmProtocols &&
                   (Atom)ev->xclient.data.l[0] == g_xpwnWmDelete) {
        /* 协议约定：WM_DELETE_WINDOW 以 WM_PROTOCOLS 为 message_type，
           WM_DELETE_WINDOW 原子放在 data.l[0]；这里先判载体再判载荷，
           避免误把任意 ClientMessage 当关闭请求。 */
            entry = xpwn_findByNativeWindow((Window)ev->xclient.window);
            if (entry && entry->m_window) {
                accepted = XWindowSystemInterface_handleCloseEvent(
                    entry->m_window);
                if (accepted) {
                    /* Qt：WM_DELETE 被接受后即视为窗口关闭，隐藏并销毁
                       原生资源（XWindow_destroy 幂等）。 */
                    XWindow_setVisible(entry->m_window, false);
                    XWindow_destroy(entry->m_window);
                }
                delivered = true;
            }
        }
        break;
    case SelectionNotify:
        entry = xpwn_findByNativeWindow(ev->xselection.requestor);
        if (entry && entry->m_window && entry->m_dropPending) {
            char* dropped = NULL;
            bool accepted = false;
            if (ev->xselection.property != None)
                dropped = xpwn_readXdndData(entry->m_win,
                                             ev->xselection.property);
            if (dropped) {
                accepted = XWindowSystemInterface_handleDropEvent(
                    entry->m_window, XEVENT_TYPE_DROP, entry->m_dragPosition,
                    NULL,
                    entry->m_dragTarget == g_xpwnTextUriList ? "text/uri-list" :
                    "text/plain", dropped);
                XFree_Hybrid(dropped);
            }
            xpwn_sendXdndFinished(entry, accepted);
            xpwn_resetXdnd(entry);
            delivered = true;
        }
        break;
    default:
        break;
    }
    (void)type;
    return delivered;
}

/* ==================== 可用性与生命周期（平台后端提供） ==================== */

bool XPlatformNativeWindow_isAvailable(void)
{
    return xpwn_ensureConnection();
}

bool XPlatformNativeWindow_create(XWindow* window)
{
    XWNPendingEntry* entry;
    XSetWindowAttributes attr;
    Window xwin;
    XRect geom;
    XString* title;
    int w, h;
    if (!window) return false;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (entry) return true; /* 幂等：已登记直接成功。 */
    entry = xpwn_findFreeSlot();
    if (!entry) return false;

    geom = XWindow_geometry(window);
    w = geom.width < 1 ? 1 : geom.width;
    h = geom.height < 1 ? 1 : geom.height;
    memset(&attr, 0, sizeof(attr));
    attr.background_pixel = 0u;
    attr.border_pixel = 0u;
    attr.colormap = g_xpwnColormap;
    /* 输入事件掩码：键盘/鼠标按键/指针移动/进出均需在创建窗口时声明，
       否则 X 服务器不会向本窗口投递对应事件。滚轮事件(Button4/5)走
       ButtonPress 通道，进入/离开用于 Qt 对齐的 enter/leave 语义。 */
    attr.event_mask = ExposureMask | StructureNotifyMask | FocusChangeMask
                   | KeyPressMask | KeyReleaseMask
                   | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
                   | EnterWindowMask | LeaveWindowMask;
    xwin = XCreateWindow(g_xpwnDisplay,
                         RootWindow(g_xpwnDisplay, g_xpwnScreenNumber),
                         geom.x, geom.y, (unsigned)w, (unsigned)h, 0,
                         g_xpwnDepth, InputOutput, g_xpwnVisual,
                         CWBackPixel | CWBorderPixel | CWColormap | CWEventMask,
                         &attr);
    if (!xwin) return false;

    entry->m_win = xwin;
    entry->m_window = window;
    entry->m_gc = XCreateGC(g_xpwnDisplay, xwin, 0, NULL);
    entry->m_client = geom;
    if (g_xpwnXdndAware != None) {
        unsigned long version = 5;
        XChangeProperty(g_xpwnDisplay, xwin, g_xpwnXdndAware, XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)&version, 1);
    }
    if (g_xpwnInputMethod) {
        XIMCallback startCallback;
        XIMCallback doneCallback;
        XIMCallback drawCallback;
        startCallback.client_data = (XPointer)entry;
        startCallback.callback = (XIMProc)xpwn_preeditStart;
        doneCallback.client_data = (XPointer)entry;
        doneCallback.callback = (XIMProc)xpwn_preeditDone;
        drawCallback.client_data = (XPointer)entry;
        drawCallback.callback = (XIMProc)xpwn_preeditDraw;
        entry->m_inputContext = XCreateIC(
            g_xpwnInputMethod,
            XNInputStyle, XIMPreeditCallbacks | XIMStatusNothing,
            XNClientWindow, xwin,
            XNFocusWindow, xwin,
            XNPreeditStartCallback, &startCallback,
            XNPreeditDoneCallback, &doneCallback,
            XNPreeditDrawCallback, &drawCallback,
            NULL);
    }
    /* 初始标题同步（公共层 createHandle 后也会再同步，这里是兜底）。 */
    title = XWindow_title(window);
    xpwn_applyTitle(xwin, title);
    if (title) XString_delete_base((XClass*)title);
    /* 注册 WM_DELETE_WINDOW 协议，窗口装饰栏关闭按钮经 WM 送达本泵。 */
    XSetWMProtocols(g_xpwnDisplay, xwin, &g_xpwnWmDelete, 1);
    XFlush(g_xpwnDisplay);
    return true;
}

bool XPlatformNativeWindow_attachForeign(XWindow* window, XWindowId nativeId)
{
    XWNPendingEntry* entry;
    XWindowAttributes attrs;
    Window child;
    int rootX, rootY;
    if (!window || nativeId == 0 || !xpwn_ensureConnection()) return false;
    /* 已登记的窗口可能拥有另一个真实句柄；外部挂接不能悄悄改写它。 */
    if (xpwn_findByXWindow(window)) return false;
    entry = xpwn_findFreeSlot();
    if (!entry || !XGetWindowAttributes(g_xpwnDisplay, (Window)nativeId, &attrs))
        return false;
    if (!XTranslateCoordinates(g_xpwnDisplay, (Window)nativeId,
                               RootWindow(g_xpwnDisplay, g_xpwnScreenNumber),
                               0, 0, &rootX, &rootY, &child))
        return false;
    memset(entry, 0, sizeof(*entry));
    entry->m_win = (Window)nativeId;
    entry->m_window = window;
    entry->m_gc = XCreateGC(g_xpwnDisplay, entry->m_win, 0, NULL);
    if (!entry->m_gc) {
        entry->m_win = 0;
        entry->m_window = NULL;
        entry->m_client = (XRect){0, 0, 0, 0};
        return false;
    }
    entry->m_client = (XRect){rootX, rootY, attrs.width, attrs.height};
    XSelectInput(g_xpwnDisplay, entry->m_win,
                 ExposureMask | StructureNotifyMask | FocusChangeMask |
                 KeyPressMask | KeyReleaseMask | ButtonPressMask |
                 ButtonReleaseMask | PointerMotionMask | EnterWindowMask |
                 LeaveWindowMask);
    XFlush(g_xpwnDisplay);
    return true;
}

void XPlatformNativeWindow_destroy(XWindow* window)
{
    XWNPendingEntry* entry;
    if (!window) return;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return;
    if (entry->m_inputContext) {
        XDestroyIC(entry->m_inputContext);
        entry->m_inputContext = NULL;
    }
    if (entry->m_gc) XFreeGC(g_xpwnDisplay, entry->m_gc);
    /* 外部窗口只解除登记，不取得其 X11 资源的销毁所有权。 */
    if (XWindow_type(window) != XWindowType_ForeignWindow)
        XDestroyWindow(g_xpwnDisplay, entry->m_win);
    entry->m_win = 0;
    entry->m_window = NULL;
    entry->m_gc = NULL;
    entry->m_client = (XRect){0, 0, 0, 0};
    memset(entry->m_keyPressed, 0, sizeof(entry->m_keyPressed));
    entry->m_lastPressTime = 0;
    entry->m_lastPressButton = XMouseButton_NoButton;
    entry->m_lastPressPos = (XPoint){0, 0};
    entry->m_preedit[0] = '\0';
    XFlush(g_xpwnDisplay);
}

/* ==================== 属性同步（平台后端提供） ==================== */

bool XPlatformNativeWindow_setVisible(XWindow* window, bool visible)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    if (visible) {
        XMapWindow(g_xpwnDisplay, entry->m_win);
    } else {
        XUnmapWindow(g_xpwnDisplay, entry->m_win);
    }
    XFlush(g_xpwnDisplay);
    return true;
}

bool XPlatformNativeWindow_setGeometry(XWindow* window, const XRect* geometry)
{
    XWNPendingEntry* entry;
    int w, h;
    if (!geometry) return false;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    /* 去重：与最近一次记录/应用的几何一致则跳过（防 ConfigureNotify 回环）。 */
    if (geometry->x == entry->m_client.x &&
        geometry->y == entry->m_client.y &&
        geometry->width == entry->m_client.width &&
        geometry->height == entry->m_client.height)
        return true;
    w = geometry->width < 1 ? 1 : geometry->width;
    h = geometry->height < 1 ? 1 : geometry->height;
    XMoveResizeWindow(g_xpwnDisplay, entry->m_win,
                      geometry->x, geometry->y, (unsigned)w, (unsigned)h);
    entry->m_client = *geometry;
    XFlush(g_xpwnDisplay);
    return true;
}

bool XPlatformNativeWindow_setTitle(XWindow* window, const XString* title)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    xpwn_applyTitle(entry->m_win, title);
    XFlush(g_xpwnDisplay);
    return true;
}

bool XPlatformNativeWindow_setKeyboardGrabEnabled(XWindow* window, bool grab)
{
    XWNPendingEntry* entry;
    int status;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    if (grab) {
        status = XGrabKeyboard(g_xpwnDisplay, entry->m_win, True,
                               GrabModeAsync, GrabModeAsync, CurrentTime);
        XFlush(g_xpwnDisplay);
        return status == GrabSuccess;
    }
    XUngrabKeyboard(g_xpwnDisplay, CurrentTime);
    XFlush(g_xpwnDisplay);
    return true;
}

bool XPlatformNativeWindow_setMouseGrabEnabled(XWindow* window, bool grab)
{
    XWNPendingEntry* entry;
    int status;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    if (grab) {
        status = XGrabPointer(g_xpwnDisplay, entry->m_win, True,
                              ButtonPressMask | ButtonReleaseMask |
                              PointerMotionMask | EnterWindowMask |
                              LeaveWindowMask, GrabModeAsync, GrabModeAsync,
                              None, None, CurrentTime);
        XFlush(g_xpwnDisplay);
        return status == GrabSuccess;
    }
    XUngrabPointer(g_xpwnDisplay, CurrentTime);
    XFlush(g_xpwnDisplay);
    return true;
}

bool XPlatformNativeWindow_requestActivate(XWindow* window)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    XRaiseWindow(g_xpwnDisplay, entry->m_win);
    XSetInputFocus(g_xpwnDisplay, entry->m_win, RevertToParent, CurrentTime);
    XFlush(g_xpwnDisplay);
    return true;
}

/** @brief 从 X11 visual mask 提取并归一化一个 8 位颜色通道。 */
static uint32_t xpwn_maskChannel(unsigned long pixel, unsigned long mask)
{
    unsigned long value;
    unsigned long maxValue;
    unsigned int shift = 0;
    if (!mask) return 0;
    while (((mask >> shift) & 1ul) == 0ul && shift < sizeof(mask) * 8u)
        ++shift;
    value = (pixel & mask) >> shift;
    maxValue = mask >> shift;
    return maxValue ? (uint32_t)((value * 255ul + maxValue / 2ul) / maxValue) : 0;
}

XPixmap* XPlatformNativeWindow_grabWindow(XWindowId window,
                                          int x, int y, int w, int h)
{
    X11_XImage* source;
    XPixmap* result;
    XPixmap captured;
    XImage image;
    Window drawable;
    Window root;
    unsigned int targetWidth;
    unsigned int targetHeight;
    unsigned int borderWidth;
    unsigned int depth;
    int rootX;
    int rootY;
    int width;
    int height;
    int row;
    int col;
    if (!xpwn_ensureConnection()) return NULL;
    if (window == 0) {
        drawable = RootWindow(g_xpwnDisplay, g_xpwnScreenNumber);
        targetWidth = (unsigned int)DisplayWidth(g_xpwnDisplay, g_xpwnScreenNumber);
        targetHeight = (unsigned int)DisplayHeight(g_xpwnDisplay, g_xpwnScreenNumber);
    } else {
        drawable = (Window)window;
        if (!XGetGeometry(g_xpwnDisplay, drawable, &root, &rootX, &rootY,
                          &targetWidth, &targetHeight, &borderWidth, &depth))
            return NULL;
    }
    width = w < 0 ? (int)targetWidth - x : w;
    height = h < 0 ? (int)targetHeight - y : h;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x >= (int)targetWidth || y >= (int)targetHeight ||
        width <= 0 || height <= 0)
        return NULL;
    if (x + width > (int)targetWidth) width = (int)targetWidth - x;
    if (y + height > (int)targetHeight) height = (int)targetHeight - y;
    if (width <= 0 || height <= 0) return NULL;
    source = XGetImage(g_xpwnDisplay, drawable, x, y,
                       (unsigned)width, (unsigned)height,
                       AllPlanes, ZPixmap);
    if (!source) return NULL;
    XImage_init_ex(&image, width, height, XImageFormat_ARGB32_Premultiplied);
    if (XImage_isNull(&image)) {
        XDestroyImage(source);
        return NULL;
    }
    for (row = 0; row < height; ++row) {
        for (col = 0; col < width; ++col) {
            unsigned long pixel = XGetPixel(source, col, row);
            uint32_t red = xpwn_maskChannel(pixel, source->red_mask);
            uint32_t green = xpwn_maskChannel(pixel, source->green_mask);
            uint32_t blue = xpwn_maskChannel(pixel, source->blue_mask);
            XImage_setPixel(&image, col, row,
                            0xff000000u | (red << 16) |
                            (green << 8) | blue);
        }
    }
    XDestroyImage(source);
    /* 先在栈上构造像素图，再移动到堆对象；XPixmap_init_image() 的公开
     * 初始化约定允许未初始化栈对象，不能直接覆盖 create() 返回值，
     * 否则会清掉 delete_base 所需的堆标志。 */
    XPixmap_init_image(&captured, &image, 0);
    result = XPixmap_create();
    if (!result) {
        XPixmap_deinit_base(&captured);
        XImage_deinit_base(&image);
        return NULL;
    }
    XPixmap_move_base(result, &captured);
    XPixmap_deinit_base(&captured);
    XImage_deinit_base(&image);
    return result;
}

/* ==================== 原生句柄与反查（平台后端提供） ==================== */

XWindowId XPlatformNativeWindow_winId(const XWindow* window)
{
    XWNPendingEntry* entry;
    if (!window) return 0;
    entry = xpwn_findByXWindow(window);
    return entry ? (XWindowId)(uintptr_t)entry->m_win : 0;
}

XWindow* XPlatformNativeWindow_windowForWinId(XWindowId id)
{
    XWNPendingEntry* entry;
    if (id == 0) return NULL;
    entry = xpwn_findByNativeWindow((Window)id);
    return entry ? entry->m_window : NULL;
}

/* ==================== 原生事件泵（平台后端提供） ==================== */

bool XPlatformNativeWindow_processPendingEvents(void)
{
    X11_XEvent event;
    bool delivered = false;
    if (!xpwn_ensureConnection()) return false;
    while (XPending(g_xpwnDisplay) > 0) {
        XNextEvent(g_xpwnDisplay, &event);
        if (xpwn_dispatchEvent(&event)) delivered = true;
    }
    return delivered;
}

bool XPlatformNativeWindow_waitForEvents(int maxMilliseconds)
{
    struct pollfd pfd;
    int fd;
    int result;
    if (!xpwn_ensureConnection()) return false;
    if (XPending(g_xpwnDisplay) > 0)
        return XPlatformNativeWindow_processPendingEvents();
    fd = XConnectionNumber(g_xpwnDisplay);
    if (fd < 0) return false;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLIN;
    result = poll(&pfd, 1u, maxMilliseconds);
    if (result <= 0) return false;
    if ((pfd.revents & (POLLIN | POLLERR | POLLHUP)) == 0) return false;
    return XPlatformNativeWindow_processPendingEvents();
}

bool XPlatformNativeWindow_queryKeyboardModifiers(
        XKeyboardModifiers* outModifiers)
{
    char keymap[32];
    KeyCode keyCode;
    XKeyboardModifiers modifiers = XKeyboardModifier_NoModifier;
    if (!outModifiers || !xpwn_ensureConnection()) return false;
    XQueryKeymap(g_xpwnDisplay, keymap);
#define XPWN_KEYSYM_PRESSED(symbol) \
    ((keyCode = XKeysymToKeycode(g_xpwnDisplay, (symbol))) != 0 && \
     (((unsigned char)keymap[keyCode >> 3] & (unsigned char)(1u << (keyCode & 7))) != 0))
    if (XPWN_KEYSYM_PRESSED(XK_Shift_L) || XPWN_KEYSYM_PRESSED(XK_Shift_R))
        modifiers |= XKeyboardModifier_ShiftModifier;
    if (XPWN_KEYSYM_PRESSED(XK_Control_L) || XPWN_KEYSYM_PRESSED(XK_Control_R))
        modifiers |= XKeyboardModifier_ControlModifier;
    if (XPWN_KEYSYM_PRESSED(XK_Alt_L) || XPWN_KEYSYM_PRESSED(XK_Alt_R))
        modifiers |= XKeyboardModifier_AltModifier;
    if (XPWN_KEYSYM_PRESSED(XK_Meta_L) || XPWN_KEYSYM_PRESSED(XK_Meta_R) ||
        XPWN_KEYSYM_PRESSED(XK_Super_L) || XPWN_KEYSYM_PRESSED(XK_Super_R))
        modifiers |= XKeyboardModifier_MetaModifier;
#undef XPWN_KEYSYM_PRESSED
    *outModifiers = modifiers;
    return true;
}

/* ==================== 出站拖放（XDND 源端） ==================== */

struct XPlatformDrag { int unused; };

XPlatformDrag* XPlatformDrag_create(void)
{
    XPlatformDrag* drag;
    if (!xpwn_ensureConnection()) return NULL;
    drag = (XPlatformDrag*)XMalloc_System(sizeof(*drag));
    if (drag) memset(drag, 0, sizeof(*drag));
    return drag;
}

void XPlatformDrag_delete(XPlatformDrag* self)
{
    if (self) XFree_System(self);
}

bool XPlatformDrag_isAvailable(const XPlatformDrag* self)
{
    (void)self;
    return xpwn_ensureConnection();
}

static void xpwn_sendXdndClientMessage(Window target, Atom message,
                                       long d0, long d1, long d2,
                                       long d3, long d4)
{
    X11_XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = g_xpwnDisplay;
    event.xclient.window = target;
    event.xclient.message_type = message;
    event.xclient.format = 32;
    event.xclient.data.l[0] = d0;
    event.xclient.data.l[1] = d1;
    event.xclient.data.l[2] = d2;
    event.xclient.data.l[3] = d3;
    event.xclient.data.l[4] = d4;
    XSendEvent(g_xpwnDisplay, target, False, NoEventMask, &event);
}

XPlatformDragResult XPlatformDrag_exec(XPlatformDrag* self, XWindow* source,
                                       const XMimeData* data, uint32_t actions)
{
    Window sourceId;
    Window root, child, target;
    int rootX, rootY, winX, winY;
    unsigned int mask;
    Atom format;
    bool sentDrop = false;
    time_t deadline;
    XPlatformDragResult result = XPlatformDragResult_Cancelled;
    (void)self;
#if !XMIMEDATA_ON
    (void)source; (void)data; (void)actions;
    return XPlatformDragResult_Unsupported;
#else
    if (!source || !data || !actions || !xpwn_ensureConnection())
        return XPlatformDragResult_Cancelled;
    sourceId = (Window)XPlatformNativeWindow_winId(source);
    if (!sourceId) return XPlatformDragResult_Cancelled;
    if (XMimeData_hasFormat(data, "text/uri-list"))
        format = g_xpwnTextUriList;
    else if (XMimeData_hasText(data))
        format = g_xpwnTextPlain;
    else
        return XPlatformDragResult_Cancelled;
    if (!XQueryPointer(g_xpwnDisplay,
                       RootWindow(g_xpwnDisplay, g_xpwnScreenNumber),
                       &root, &child, &rootX, &rootY, &winX, &winY, &mask))
        return XPlatformDragResult_Cancelled;
    target = xpwn_findXdndTarget(child);
    if (!target || target == sourceId) return XPlatformDragResult_Cancelled;

    g_xpwnDragActive = true;
    g_xpwnDragAccepted = false;
    g_xpwnDragFinished = false;
    g_xpwnDragSource = sourceId;
    g_xpwnDragTarget = target;
    g_xpwnDragWindow = source;
    g_xpwnDragMime = data;
    g_xpwnDragFormat = format;
    XSetSelectionOwner(g_xpwnDisplay, g_xpwnXdndSelection, sourceId,
                       CurrentTime);
    if (XGetSelectionOwner(g_xpwnDisplay, g_xpwnXdndSelection) != sourceId)
        goto cleanup;
    xpwn_sendXdndClientMessage(target, g_xpwnXdndEnter,
                               (long)sourceId, (5L << 24), (long)format, 0L, 0L);
    xpwn_sendXdndClientMessage(target, g_xpwnXdndPosition,
                               (long)sourceId, 0L,
                               ((long)(rootX & 0xffff) << 16) |
                               (long)(rootY & 0xffff),
                               CurrentTime, (long)g_xpwnXdndActionCopy);
    XFlush(g_xpwnDisplay);
    deadline = time(NULL) + 5;
    while (time(NULL) <= deadline && !g_xpwnDragAccepted)
        XPlatformNativeWindow_waitForEvents(50);
    if (!g_xpwnDragAccepted) {
        xpwn_sendXdndClientMessage(target, g_xpwnXdndLeave,
                                   (long)sourceId, 0L, 0L, 0L, 0L);
        XFlush(g_xpwnDisplay);
        goto cleanup;
    }
    xpwn_sendXdndClientMessage(target, g_xpwnXdndDrop,
                               (long)sourceId, 0L, CurrentTime, 0L, 0L);
    XFlush(g_xpwnDisplay);
    sentDrop = true;
    deadline = time(NULL) + 5;
    while (time(NULL) <= deadline && !g_xpwnDragFinished)
        XPlatformNativeWindow_waitForEvents(50);
    if (g_xpwnDragFinished) {
        if ((actions & XPlatformDragAction_Move) &&
            !(actions & XPlatformDragAction_Copy))
            result = XPlatformDragResult_Moved;
        else if ((actions & XPlatformDragAction_Link) &&
                 !(actions & (XPlatformDragAction_Move | XPlatformDragAction_Copy)))
            result = XPlatformDragResult_Linked;
        else
            result = XPlatformDragResult_Copied;
    }
cleanup:
    if (g_xpwnDragActive && !sentDrop && g_xpwnDragAccepted) {
        xpwn_sendXdndClientMessage(target, g_xpwnXdndLeave,
                                   (long)sourceId, 0L, 0L, 0L, 0L);
        XFlush(g_xpwnDisplay);
    }
    XSetSelectionOwner(g_xpwnDisplay, g_xpwnXdndSelection, None, CurrentTime);
    g_xpwnDragActive = false;
    g_xpwnDragSource = 0;
    g_xpwnDragTarget = 0;
    g_xpwnDragWindow = NULL;
    g_xpwnDragMime = NULL;
    g_xpwnDragFormat = None;
    return result;
#endif
}

/* ==================== 上屏（平台后端提供） ==================== */

bool XPlatformNativeWindow_present(XWindow* window, const XImage* image,
                                   const XRegion* region,
                                   const XPoint* offset)
{
    XWNPendingEntry* entry;
    X11_XImage* ximg;
    XPoint zero;
    const XPoint* off;
    XRegion effective;
    XRect full;
    bool direct;
    uint8_t* buffer;
    int bufBpl;
    int imgW, imgH;
    int i;
    bool any = false;
    XRegion_init(&effective);
    if (!window || !image) return false;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    imgW = XImage_width(image);
    imgH = XImage_height(image);
    if (imgW <= 0 || imgH <= 0) return false;

    /* 裁剪脏区：region 为 NULL/空按整幅；offset 为缓冲相对窗口偏移。 */
    if (!offset) {
        XPoint_init(&zero, 0, 0);
        off = &zero;
    } else {
        off = offset;
    }
    if (region && !XRegion_isEmpty(region)) {
        XRegion_copy(region, &effective);
    } else {
        XRegion_init(&effective);
        full.x = 0; full.y = 0;
        full.width = imgW; full.height = imgH;
        XRegion_addRect(&effective, &full);
    }
    if (XRegion_isEmpty(&effective)) {
        XRegion_deinit(&effective);
        return false;
    }

    /* XPutImage 载体：32 位对齐位宽。 */
    ximg = XCreateImage(g_xpwnDisplay, g_xpwnVisual, g_xpwnDepth, ZPixmap, 0,
                        NULL, imgW, imgH, 32, 0);
    if (!ximg) {
        XRegion_deinit(&effective);
        return false;
    }
    /* 直拷条件：真 32 位像素 + 标准 BGRA8888 掩码 + 小端字节序（与
       ARGB32 小端内存布局 [B,G,R,A] 相同）。24 位深含 32 位填充的视觉
       也能直拷（高 8 位 Alpha 被服务器忽略）。 */
    direct = ximg->bits_per_pixel == 32 && ximg->byte_order == LSBFirst &&
             ImageByteOrder(g_xpwnDisplay) == LSBFirst &&
             g_xpwnVisual->red_mask == 0x00ff0000u &&
             g_xpwnVisual->green_mask == 0x0000ff00u &&
             g_xpwnVisual->blue_mask == 0x000000ffu;
    bufBpl = direct ? imgW * 4 : (imgW * 3 + 3) & ~3;
    buffer = (uint8_t*)XMalloc_Hybrid((size_t)bufBpl * (size_t)imgH);
    /* 构造临时 XImage：借出 buffer 计算偏移后立即回收指针，避免 Xlib
       擅自 free 调用方缓冲。 */
    ximg->data = (char*)buffer;
    for (i = 0; i < effective.count; ++i) {
        XRect srect;
        XRect drect;
        srect.x = effective.rects[i].x - off->x;
        srect.y = effective.rects[i].y - off->y;
        srect.width = effective.rects[i].width;
        srect.height = effective.rects[i].height;
        if (!xpwn_clipRectToImage(&srect, imgW, imgH, &srect)) continue;
        drect.x = srect.x + off->x;
        drect.y = srect.y + off->y;
        drect.width = srect.width;
        drect.height = srect.height;
        if (direct) {
            xpwn_copyRectDirect(image, &srect, buffer, bufBpl);
            XPutImage(g_xpwnDisplay, entry->m_win, entry->m_gc, ximg,
                      srect.x, srect.y, drect.x, drect.y,
                      (unsigned)srect.width, (unsigned)srect.height);
        } else {
            xpwn_copyRect24(image, &srect, buffer, bufBpl);
            XPutImage(g_xpwnDisplay, entry->m_win, entry->m_gc, ximg,
                      srect.x, srect.y, drect.x, drect.y,
                      (unsigned)srect.width, (unsigned)srect.height);
        }
        any = true;
    }
    ximg->data = NULL;
    XDestroyImage(ximg);
    XFree_Hybrid(buffer);
    XRegion_deinit(&effective);
    if (any) XFlush(g_xpwnDisplay);
    return any;
}

/* ==================== 原生连接（平台后端提供） ==================== */

void* XPlatformNativeWindow_nativeConnection(
        XPlatformNativeWindowConnectionType* outType)
{
    if (outType) *outType = XPlatformNativeWindowConnection_None;
    if (!xpwn_ensureConnection()) return NULL;
    if (outType) *outType = XPlatformNativeWindowConnection_X11;
    return (void*)g_xpwnDisplay;
}

#endif /* defined(__linux__) && defined(XINYUE_C_HAS_X11) */
#endif /* XPLATFORMNATIVEWINDOW_ON && XPLATFORMNATIVEWINDOW_X11_ON */
