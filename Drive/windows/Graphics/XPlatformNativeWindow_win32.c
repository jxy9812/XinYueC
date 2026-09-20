/******************************************************************************
 * @file       XPlatformNativeWindow_win32.c
 * @brief      Windows Win32 平台原生窗口后端（对标 Qt 6.8 的 windows
 *             平台窗口插件）。
 * @details    本文件实现 XPlatformNativeWindow 契约的 Windows Win32 端，
 *             与 Drive/Posix/Graphics/XPlatformNativeWindow_posix.c 保持完全相同的
 *             平台边界语义，源码级对齐（同一份公共契约、同一套注册表、
 *             同一套事件翻译规则）：
 *             - 进程内单例窗口类：RegisterClassExW 注册
 *               L"XinYueCNativeWindowClass"（CS_HREDRAW|CS_VREDRAW），
 *               首个窗口创建时惰性完成；静态 64 槽注册表 HWND <->
 *               XWindow* 双向查找，用于 winId()/windowForWinId() 与
 *               WndProc 事件路由；
 *             - 窗口生命周期：CreateWindowExW(WS_OVERLAPPEDWINDOW) 创建
 *               但保持隐藏，wm map 由 setVisible(true) 的 ShowWindow(SW_SHOW)
 *               完成；CreateWindowExW 的 lpCreateParams 携带 XWindow*，
 *               WM_NCCREATE 存入 GWLP_USERDATA，后续消息经
 *               GetWindowLongPtrW 恢复窗口对象；
 *             - 事件翻译（对标 X11 后端对应消息）：WM_PAINT -> 以
 *               BeginPaint 的 rcPaint 为脏区注入 handleExposeEvent（重绘）；
 *               WM_SIZE/WM_MOVE -> 先按 GetClientRect+ClientToScreen 得到
 *               客户端屏幕几何并更新本后端记录再注入 handleGeometryChange
 *               （防 setGeometry 回环，与 X11 ConfigureNotify 处理同构）；
 *               WM_SETFOCUS -> handleFocusWindowChanged(ActiveWindow)；
 *               WM_KILLFOCUS -> 直接自发投递 FOCUS_OUT（WSI 无 FocusOut
 *               注入入口，与 X11 FocusOut 处理约定一致）；WM_CLOSE ->
 *               handleCloseEvent 接受后隐藏并销毁原生窗口；WM_SHOWWINDOW
 *               仅记录最后映射状态供事件去抖；WM_ERASEBKGND 恒返回 1
 *               避免 GDI 闪烁；
 *             - 消息泵：processPendingEvents 用 PeekMessage(PM_REMOVE)
 *               非阻塞泵空全部待决窗口消息（TranslateMessage +
 *               DispatchMessage），WM_QUIT 只记录不派发；waitForEvents 用
 *               MsgWaitForMultipleObjects(QS_ALLINPUT) 阻塞等待事件就绪后
 *               再泵一批，形成自绘主循环事件源；
 *             - 上屏：present 把 ARGB32 预乘 Alpha 软件缓冲的脏区按行拷贝
 *               进与脏区等宽的自顶向下 32 位 DIB（BI_RGB，BGRA 字节序与
 *               XImage 小端 [B,G,R,A] 直配），再经 SetDIBitsToDevice 提交
 *               到窗口 DC；DIB 负高度表示 top-down，无需像素转换；
 *             - 标题同步：XString_toUtf16 得到 UTF-16 缓冲后
 *               SetWindowTextW，未设置标题时用空串。
 *             - 剪贴板后端：installClipboardBackend 安装 Win32 系统剪贴板
 *               后端（文本走 CF_UNICODETEXT、HTML 走 "HTML Format"、
 *               image/png 走注册格式透传），专用隐藏窗口认领所有权并
 *               监听 WM_CLIPBOARDUPDATE，外部应用认领时经
 *               selectionRevoked 反向通知上层（受 XCLIPBOARD_ON 约束）。
 *             窗口映射/几何/标题同步全部围绕 XWindow 驱动，setGeometry 按
 *             本后端记录客户端几何去重，杜绝 WM_SIZE/WM_MOVE 与 setGeometry
 *             互相触发造成递归震荡。公共契约头不包含任何 Windows API。
 * @note       本文件只在 _WIN32 编译目标参与编译（宏由驱动器条件守护），
 *             并受 XPLATFORMNATIVEWINDOW_ON 与
 *             XPLATFORMNATIVEWINDOW_WIN32_ON 两个配置开关约束；其余平台/
 *             配置由 XPlatformNativeWindow_unsupported.c 兜底。
 *             本文件为单线程主循环设计（GUI 线程持有消息队列），WndProc
 *             内不进行跨线程同步。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformNativeWindow.h"

#if XPLATFORMNATIVEWINDOW_ON && XPLATFORMNATIVEWINDOW_WIN32_ON && XWINDOW_ON

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "XWindow.h"
#include "XWindowSystemInterface.h"
#include "XWindowEvent.h"
#include "XGuiApplication.h"
#include "XClipboard.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XString.h"
#include "XGeometry.h"
#include "XMemory.h"
#include "XAbstractNetIoRing.h"
#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <string.h>

#if XAbstractNetIoRing_ON
/* 主循环双源等待：IOCP 端口句柄与 processReady（与 POSIX 端
 * waitForEvents 的 X11 fd + ring fd 双源 poll 语义镜像）。本头自带
 * windows.h 守卫，CXinYueConfig 经 XGuiConfig -> XGuiConfig.h 可见。 */
#include "XNetIoRingWin32.h"
#endif

/** @brief 进程内原生窗口注册表容量（静态表，单线程使用）。 */
#define XPWN_MAX_WINDOWS 64

/** @brief 本后端登记的原生窗口类名（宽字符，进程内唯一）。 */
#define XPWN_CLASS_NAME L"XinYueCNativeWindowClass"

/** @brief 原生窗口注册表槽位（HWND 与公共 XWindow 对象双向登记）。 */
typedef struct XWNPendingEntry
{
    HWND m_hwnd;         /**< Win32 原生窗口句柄（NULL 表示空槽）。 */
    XWindow* m_window;   /**< 公共窗口对象借用指针；槽位为空时 NULL。 */
    XRect m_client;      /**< 本后端最近一次记录的客户端屏幕几何（去重用）。 */
    bool m_visible;      /**< WM_SHOWWINDOW 最后记录的映射状态。 */
    bool m_mouseInside;  /**< 指针是否位于客户区内（进入/离开追踪，Win32 无原生 enter 消息）。 */
    WNDPROC m_oldProc;   /**< 外部窗口子类化前的过程；内部窗口为 NULL。 */
} XWNPendingEntry;

/** @brief 每进程 Win32 连接状态。 */
static HINSTANCE g_xpwnInstance;          /**< 进程实例句柄（GetModuleHandleW(NULL)）。 */
static bool g_xpwnClassRegistered;        /**< 窗口类是否已注册（幂等标志）。 */
static bool g_xpwnQuitReceived;           /**< 已收到 WM_QUIT（只记录不派发）。 */
static XWNPendingEntry g_xpwnEntries[XPWN_MAX_WINDOWS]; /**< 窗口注册表。 */

#if XCLIPBOARD_ON
/** @brief 剪贴板专用隐藏窗口（属主 + 监听宿主；定义见剪贴板后端节）。 */
static HWND g_xpwnClipHwnd;
/** @brief 剪贴板外部认领处理（WM_CLIPBOARDUPDATE；定义见剪贴板后端节）。 */
static void xpwn_clipHandleClipboardUpdate(void);
#endif /* XCLIPBOARD_ON */

/** @brief 把系统 UTF-16 输入法文本转成临时 UTF-8 串（调用方负责释放）。 */
static char* xpwn_imeUtf8(const wchar_t* text, int wcharCount)
{
    char* utf8;
    int bytes;
    if (!text || wcharCount <= 0) return NULL;
    bytes = WideCharToMultiByte(CP_UTF8, 0, text, wcharCount,
                                NULL, 0, NULL, NULL);
    if (bytes <= 0) return NULL;
    utf8 = (char*)XMalloc_Hybrid((size_t)bytes + 1u);
    if (!utf8) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, text, wcharCount, utf8, bytes,
                            NULL, NULL) != bytes) {
        XFree_Hybrid(utf8);
        return NULL;
    }
    utf8[bytes] = '\0';
    return utf8;
}

/** @brief 读取 WM_IME_COMPOSITION 指定的 UTF-16 组合/提交字符串。 */
static char* xpwn_imeCompositionString(HIMC context, DWORD index)
{
    LONG bytes;
    wchar_t* wide;
    char* utf8;
    if (!context) return NULL;
    bytes = ImmGetCompositionStringW(context, index, NULL, 0);
    if (bytes <= 0) return NULL;
    wide = (wchar_t*)XMalloc_Hybrid((size_t)bytes + sizeof(wchar_t));
    if (!wide) return NULL;
    if (ImmGetCompositionStringW(context, index, wide, (DWORD)bytes) != bytes) {
        XFree_Hybrid(wide);
        return NULL;
    }
    wide[bytes / (LONG)sizeof(wchar_t)] = L'\0';
    utf8 = xpwn_imeUtf8(wide, bytes / (LONG)sizeof(wchar_t));
    XFree_Hybrid(wide);
    return utf8;
}

/** @brief 把 WM_DROPFILES 的路径集合转成 text/uri-list UTF-8 载荷。 */
static char* xpwn_dropFilesUriList(HDROP drop)
{
    UINT count;
    UINT i;
    size_t used = 0;
    size_t capacity = 1;
    char* result;
    count = DragQueryFileW(drop, 0xffffffffu, NULL, 0);
    for (i = 0; i < count; ++i) {
        UINT length = DragQueryFileW(drop, i, NULL, 0);
        capacity += (size_t)length * 4u + 12u;
    }
    result = (char*)XMalloc_Hybrid(capacity);
    if (!result) return NULL;
    result[0] = '\0';
    for (i = 0; i < count; ++i) {
        UINT length = DragQueryFileW(drop, i, NULL, 0);
        wchar_t* path = (wchar_t*)XMalloc_Hybrid(((size_t)length + 1u) * sizeof(wchar_t));
        char* utf8;
        if (!path) continue;
        DragQueryFileW(drop, i, path, length + 1u);
        utf8 = xpwn_imeUtf8(path, (int)length);
        XFree_Hybrid(path);
        if (!utf8) continue;
        if (used + strlen(utf8) + 10u >= capacity) {
            XFree_Hybrid(utf8);
            break;
        }
        XMemcpy(result + used, "file:///", 8u);
        used += 8u;
        XMemcpy(result + used, utf8, strlen(utf8));
        used += strlen(utf8);
        result[used++] = '\r';
        result[used++] = '\n';
        result[used] = '\0';
        XFree_Hybrid(utf8);
    }
    return result;
}

/* ==================== 内部工具 ==================== */

/** @brief 空槽查找；注册表满时返回 NULL。 */
static XWNPendingEntry* xpwn_findFreeSlot(void)
{
    size_t i;
    for (i = 0; i < XPWN_MAX_WINDOWS; ++i) {
        if (g_xpwnEntries[i].m_hwnd == NULL) return &g_xpwnEntries[i];
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

/** @brief 按 HWND 查找槽位。 */
static XWNPendingEntry* xpwn_findByNativeWindow(HWND hwnd)
{
    size_t i;
    if (!hwnd) return NULL;
    for (i = 0; i < XPWN_MAX_WINDOWS; ++i) {
        if (g_xpwnEntries[i].m_hwnd == hwnd) return &g_xpwnEntries[i];
    }
    return NULL;
}

/** @brief 将未处理消息转回外部窗口原过程；内部窗口使用默认过程。 */
static LRESULT xpwn_callPreviousProc(const XWNPendingEntry* entry, HWND hwnd,
                                     UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (entry && entry->m_oldProc)
        return CallWindowProcW(entry->m_oldProc, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/** @brief 窗口过程前向声明（xpwn_ensureInstance 的 RegisterClassExW 需要）。 */
static LRESULT CALLBACK xpwn_wndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam);

/** @brief 建立进程级 Win32 连接（幂等；注册窗口类失败即整体不可用）。 */
static bool xpwn_ensureInstance(void)
{
    WNDCLASSEXW wc;
    if (g_xpwnInstance && g_xpwnClassRegistered) return true;
    g_xpwnInstance = GetModuleHandleW(NULL);
    if (!g_xpwnInstance) return false;
    if (g_xpwnClassRegistered) return true;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS; /* CS_DBLCLKS：接收双击系列消息。 */
    wc.lpfnWndProc = xpwn_wndProc;
    wc.hInstance = g_xpwnInstance;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = NULL; /* 自绘窗口：不占用系统画刷，避免擦除闪烁。 */
    wc.lpszClassName = XPWN_CLASS_NAME;
    if (!RegisterClassExW(&wc)) {
        /* ERROR_CLASS_ALREADY_EXISTS：同一进程注册两次视为成功（幂等）。 */
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }
    g_xpwnClassRegistered = true;
    return true;
}

/** @brief 取窗口客户端区域屏幕几何（XWindow 几何语义：客户区坐标即窗口位置）。 */
static bool xpwn_getClientGeometry(HWND hwnd, XRect* out)
{
    RECT rc;
    POINT pt;
    if (!hwnd || !out) return false;
    if (!GetClientRect(hwnd, &rc)) return false;
    pt.x = 0;
    pt.y = 0;
    if (!ClientToScreen(hwnd, &pt)) return false;
    out->x = pt.x;
    out->y = pt.y;
    out->width = rc.right - rc.left;
    out->height = rc.bottom - rc.top;
    return true;
}

/** @brief 把按窗口样式调整后的窗口矩形转换为客户端恰好等于目标几何。 */
static DWORD xpwn_windowStyle(const XWindow* window)
{
    if (window && XWindow_type(window) == XWindowType_Popup)
        return WS_POPUP;
    return WS_OVERLAPPEDWINDOW;
}

static void xpwn_adjustWindowRect(const XWindow* window,
                                  const XRect* geometry, RECT* rc)
{
    DWORD style;
    if (!geometry || !rc) return;
    rc->left = geometry->x;
    rc->top = geometry->y;
    rc->right = geometry->x + geometry->width;
    rc->bottom = geometry->y + geometry->height;
    style = xpwn_windowStyle(window);
    AdjustWindowRectEx(rc, style, FALSE, 0);
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

/** @brief 同步标题到原生窗口（UTF-8 -> UTF-16 -> SetWindowTextW）。 */
static void xpwn_applyTitle(HWND hwnd, const XString* title)
{
    const uint16_t* utf16;
    if (!hwnd) return;
    utf16 = title ? XString_toUtf16(title) : NULL;
    SetWindowTextW(hwnd, (LPCWSTR)(utf16 ? utf16 : L""));
}

/* ==================== 键鼠翻译工具（Win32 -> 无关键码） ==================== */

/** @brief 把 Win32 虚拟键码翻译为与平台无关的按键码（XKey 枚举或 ASCII 码位）。
 * @details 与 X11 后端共用同一套无关键码约定：可打印字符直接取 ASCII 码位
 *          （字母固定用大写，与 X11 键盘映射语义一致），特殊功能键按
 *          XKey 枚举映射，取值与 Qt::Key 对齐；无法识别的键返回 XKey_None。
 * @param vk     虚拟键码（WM_KEYDOWN/UP 的 wParam）。
 * @param lParam 与键消息配套的 lParam（扩展键位 0x01000000 用于区分
 *               小键盘回车与主键盘回车）。
 */
static int xpwn_translateKey(WPARAM vk, LPARAM lParam)
{
    switch (vk) {
    case 0x08: return XKey_Backspace;
    case 0x09: return XKey_Tab;
    case 0x0d: return (lParam & 0x01000000L) ? XKey_Enter : XKey_Return;
    case 0x1b: return XKey_Escape;
    case 0x2d: return XKey_Insert;
    case 0x2e: return XKey_Delete;
    case 0x24: return XKey_Home;
    case 0x23: return XKey_End;
    case 0x25: return XKey_Left;
    case 0x26: return XKey_Up;
    case 0x27: return XKey_Right;
    case 0x28: return XKey_Down;
    case 0x21: return XKey_PageUp;
    case 0x22: return XKey_PageDown;
    case 0x13: return XKey_Pause;
    case 0x2c: return XKey_Print;
    case 0x0c: return XKey_Clear;
    case 0x10: return XKey_Shift;
    case 0x11: return XKey_Control;
    case 0x12: return XKey_Alt;
    case 0x5b:
    case 0x5c: return XKey_Meta;          /* Win/Super。 */
    case 0x14: return XKey_CapsLock;
    case 0x90: return XKey_NumLock;
    case 0x91: return XKey_ScrollLock;
    case 0x20: return XKey_Space;
    /* OEM 标点：返回基础字形（不随 Shift 变化；按键身份与 Qt::Key 一致）。 */
    case 0xba: return XKey_Semicolon;
    case 0xbb: return XKey_Equal;
    case 0xbc: return XKey_Comma;
    case 0xbd: return XKey_Minus;
    case 0xbe: return XKey_Period;
    case 0xbf: return XKey_Slash;
    case 0xc0: return XKey_QuoteLeft;
    case 0xdb: return XKey_BracketLeft;
    case 0xdc: return XKey_Backslash;
    case 0xdd: return XKey_BracketRight;
    case 0xde: return XKey_Apostrophe;
    case 0xe2: return XKey_Backslash;     /* OEM102（欧洲布局）。 */
    /* 小键盘：数字/符号复用主键盘码位（KeypadModifier 由调用方补充）。 */
    case 0x60: return XKey_0;
    case 0x61: return XKey_1;
    case 0x62: return XKey_2;
    case 0x63: return XKey_3;
    case 0x64: return XKey_4;
    case 0x65: return XKey_5;
    case 0x66: return XKey_6;
    case 0x67: return XKey_7;
    case 0x68: return XKey_8;
    case 0x69: return XKey_9;
    case 0x6a: return XKey_Asterisk;
    case 0x6b: return XKey_Plus;
    case 0x6c: return XKey_Comma;         /* 分隔符（多为逗号）。 */
    case 0x6d: return XKey_Minus;
    case 0x6e: return XKey_Period;        /* 小数点。 */
    case 0x6f: return XKey_Slash;
    default:
        /* 功能键 F1..F24（VK_F1=0x70..VK_F24=0x87）。 */
        if (vk >= 0x70 && vk <= 0x87) return XKey_F1 + (int)(vk - 0x70);
        /* 基本 ASCII 字母/数字。 */
        if (vk >= 'A' && vk <= 'Z') return (int)vk;
        if (vk >= '0' && vk <= '9') return (int)vk;
        return XKey_None;
    }
}

/** @brief 查询当前全局修饰键状态为 XKeyboardModifiers。 */
static XKeyboardModifiers xpwn_translateModifiers(void)
{
    XKeyboardModifiers modifiers = XKeyboardModifier_NoModifier;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= XKeyboardModifier_ShiftModifier;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= XKeyboardModifier_ControlModifier;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= XKeyboardModifier_AltModifier;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
        modifiers |= XKeyboardModifier_MetaModifier;
    return modifiers;
}

/** @brief 从鼠标消息 wParam 的 MK_* 位掩码翻译为按下按键集合。 */
static XMouseButton xpwn_translateButtons(WPARAM wParam)
{
    XMouseButton buttons = XMouseButton_NoButton;
    if (wParam & MK_LBUTTON) buttons |= XMouseButton_LeftButton;
    if (wParam & MK_MBUTTON) buttons |= XMouseButton_MiddleButton;
    if (wParam & MK_RBUTTON) buttons |= XMouseButton_RightButton;
    if (wParam & MK_XBUTTON1) buttons |= XMouseButton_BackButton;
    if (wParam & MK_XBUTTON2) buttons |= XMouseButton_ForwardButton;
    return buttons;
}

/** @brief 从消息 lParam 取客户区/全局（屏幕）坐标。
 * @details 鼠标按下/移动类消息的 lParam 为客户区坐标；滚轮类消息为屏幕
 *          坐标，调用方传入 isScreen=true 时先把全局坐标转换回客户区。 */
static void xpwn_getMousePos(HWND hwnd, LPARAM lParam, bool isScreen,
                             XPoint* position, XPoint* globalPosition)
{
    POINT pt;
    pt.x = (int)(short)LOWORD(lParam);
    pt.y = (int)(short)HIWORD(lParam);
    if (globalPosition) {
        globalPosition->x = pt.x;
        globalPosition->y = pt.y;
    }
    if (isScreen && hwnd) ScreenToClient(hwnd, &pt);
    if (position) {
        position->x = pt.x;
        position->y = pt.y;
    }
}

/** @brief 从 Win32 鼠标按键消息推导事件类型与触发按键。
 * @details 左/中/右三键按消息号直接确定；WM_XBUTTON* 的附加键由
 *          wParam 的 HIWORD（XBUTTON1=后退、XBUTTON2=前进）确定。
 * @param msg      消息号（WM_L/R/M/XBUTTON* 系列）。
 * @param wParam   消息附加参数（WM_XBUTTON* 的 HIWORD 为具体键）。
 * @param outButton 输出触发按键；无法识别时置 XMouseButton_NoButton。
 * @return 对应事件类型（按下/释放/双击）；无法识别时返回 XEVENT_TYPE_NONE。
 */
static XEventType xpwn_mouseMessageEvent(UINT msg, WPARAM wParam,
                                         XMouseButton* outButton)
{
    XEventType type = XEVENT_TYPE_NONE;
    XMouseButton button = XMouseButton_NoButton;
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        button = XMouseButton_LeftButton;
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        button = XMouseButton_RightButton;
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        button = XMouseButton_MiddleButton;
        break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        /* 附加键：HIWORD 为 XBUTTON1（后退）/XBUTTON2（前进）。 */
        button = (HIWORD(wParam) == XBUTTON1)
                     ? XMouseButton_BackButton
                     : XMouseButton_ForwardButton;
        break;
    default:
        break;
    }
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        type = XEVENT_TYPE_MOUSE_BUTTON_PRESS;
        break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
        type = XEVENT_TYPE_MOUSE_BUTTON_RELEASE;
        break;
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK:
        type = XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK;
        break;
    default:
        break;
    }
    if (outButton) *outButton = button;
    return type;
}

/* ==================== WndProc（原生事件 -> WSI 注入） ==================== */

/** @brief 窗口过程：翻译 Win32 窗口消息为窗口事件并经 WSI 注入。 */
static LRESULT CALLBACK xpwn_wndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam)
{
    XWNPendingEntry* entry;
    XWindow* window = NULL;
    XEvent* ev;
    bool accepted;

    /* WM_NCCREATE 最先到达：从创建参数恢复 XWindow* 并存入用户数据。 */
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)(cs ? cs->lpCreateParams : NULL));
        return TRUE;
    }
    if (msg == WM_NCDESTROY) {
        WNDPROC oldProc = NULL;
        entry = xpwn_findByNativeWindow(hwnd);
        if (entry) {
            oldProc = entry->m_oldProc;
            entry->m_hwnd = NULL;
            entry->m_window = NULL;
            entry->m_oldProc = NULL;
            entry->m_visible = false;
            entry->m_mouseInside = false;
            entry->m_client = (XRect){0, 0, 0, 0};
        }
        /* 窗口被销毁（无论谁发起）：清除用户数据，预防悬挂指针。 */
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
        if (oldProc)
            return CallWindowProcW(oldProc, hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    window = (XWindow*)(void*)(uintptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    entry = window ? xpwn_findByXWindow(window) : NULL;
    if (!entry) entry = xpwn_findByNativeWindow(hwnd);
    if (entry) window = entry->m_window;

    switch (msg) {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT r;
        XRegion region;
        XRect rect;
        if (BeginPaint(hwnd, &ps) != 0) {
            r = ps.rcPaint;
            /* EndPaint must validate the system update region before XGui
             * submits its persistent backing DIB. Presenting with a second
             * DC while BeginPaint is active leaves the original update region
             * pending on some Win32 paths and continuously re-enters
             * WM_PAINT. */
            EndPaint(hwnd, &ps);
            if (entry && entry->m_window && !IsRectEmpty(&r)) {
                XRegion_init(&region);
                rect.x = r.left;
                rect.y = r.top;
                rect.width = r.right - r.left;
                rect.height = r.bottom - r.top;
                XRegion_addRect(&region, &rect);
                /* 注入暴露：XBackingStore/绘制槽通过 paintEvent 重绘并上屏。 */
                XWindowSystemInterface_handleExposeEvent(entry->m_window,
                                                         &region);
                XRegion_deinit(&region);
            }
        }
        return 0;
    }
    case WM_ERASEBKGND:
        /* 自绘窗口：返回非 0 抑制系统背景擦除，消除闪烁。 */
        return 1;
    case WM_SIZE:
    case WM_MOVE:
    {
        XRect client;
        if (xpwn_getClientGeometry(hwnd, &client)) {
            if (entry) {
                /* 先更新本后端记录，再注入几何变化：setGeometry 去重比对
                   以此为准，从源头切断「setGeometry -> WM_SIZE/MOVE ->
                   handleGeometryChange -> setGeometry」回环（与 X11
                   ConfigureNotify 处理同构）。 */
                entry->m_client = client;
                if (entry->m_window) {
                    XWindowSystemInterface_handleGeometryChange(
                        entry->m_window, &client);
                }
            }
        }
        return 0;
    }
    case WM_SETFOCUS:
        if (entry && entry->m_window) {
            XWindowSystemInterface_handleFocusWindowChanged(
                entry->m_window, XFocusReason_ActiveWindow);
        }
        return 0;
    case WM_KILLFOCUS:
        if (entry && entry->m_window) {
            /* WSI 无 FocusOut 注入入口（Qt 只有 handleFocusWindowChanged），
               这里直接自发投递 FOCUS_OUT 事件（与 X11 约定一致）。 */
            ev = (XEvent*)XFocusEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                XEVENT_TYPE_FOCUS_OUT,
                                                XFocusReason_ActiveWindow);
            if (ev) {
                XGuiApplication_sendSpontaneousEvent((XObject*)entry->m_window,
                                                     ev);
                XEvent_delete_base((XClass*)ev);
            }
        }
        return 0;
    case WM_CLOSE:
        if (entry && entry->m_window) {
            accepted = XWindowSystemInterface_handleCloseEvent(
                entry->m_window);
            if (accepted) {
                /* Qt：WM_CLOSE 被接受后即视为窗口关闭，隐藏并销毁原生
                   资源（XWindow_destroy 幂等）。 */
                XWindow_setVisible(entry->m_window, false);
                XWindow_destroy(entry->m_window);
            }
            return 0;
        }
        break;
    case WM_DROPFILES:
        if (entry && entry->m_window) {
            HDROP drop = (HDROP)wParam;
            POINT point;
            POINT global;
            char* uriList;
            point.x = 0;
            point.y = 0;
            (void)DragQueryPoint(drop, &point);
            global = point;
            ClientToScreen(hwnd, &global);
            uriList = xpwn_dropFilesUriList(drop);
            (void)XWindowSystemInterface_handleDropEvent(
                entry->m_window, XEVENT_TYPE_DROP,
                (XPoint){ point.x, point.y },
                &(XPoint){ global.x, global.y }, "text/uri-list",
                uriList ? uriList : "");
            if (uriList) XFree_Hybrid(uriList);
            DragFinish(drop);
        }
        return 0;
    /* ============ 输入法组合/提交（IMM32 -> XInputMethodEvent） ============ */
    case WM_IME_STARTCOMPOSITION:
        return 0;
    case WM_IME_COMPOSITION:
        if (entry && entry->m_window) {
            HIMC imeContext = ImmGetContext(hwnd);
            char* preedit = NULL;
            char* commit = NULL;
            int cursor = -1;
            if (imeContext) {
                if (lParam & GCS_COMPSTR)
                    preedit = xpwn_imeCompositionString(imeContext, GCS_COMPSTR);
                if (lParam & GCS_RESULTSTR)
                    commit = xpwn_imeCompositionString(imeContext, GCS_RESULTSTR);
                if (lParam & GCS_CURSORPOS)
                    cursor = (int)ImmGetCompositionStringW(
                        imeContext, GCS_CURSORPOS, NULL, 0);
                (void)XWindowSystemInterface_handleInputMethodEvent(
                    entry->m_window, preedit ? preedit : "",
                    commit ? commit : "", 0, 0, cursor, cursor);
                if (preedit) XFree_Hybrid(preedit);
                if (commit) XFree_Hybrid(commit);
                ImmReleaseContext(hwnd, imeContext);
            }
        }
        return 0;
    case WM_IME_ENDCOMPOSITION:
        if (entry && entry->m_window)
            (void)XWindowSystemInterface_handleInputMethodEvent(
                entry->m_window, "", "", 0, 0, -1, -1);
        return 0;
    /* ============ 键盘事件（对标 Qt qwindowswindow.cpp 键消息翻译） ============ */
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    {
        int key;
        XKeyboardModifiers modifiers;
        bool autoRepeat;
        if (entry && entry->m_window) {
            key = xpwn_translateKey(wParam, lParam);
            if (key != XKey_None) {
                modifiers = xpwn_translateModifiers();
                /* 小键盘键（VK 0x60..0x6f 数字区）或扩展回车补充 Keypad
                   标记：Win32 不自动携带键源，用 VK 区间与扩展位识别，
                   与 X11 后端 Mod2Mask->Keypad 的翻译约定一致。 */
                if ((wParam >= 0x60 && wParam <= 0x6f) ||
                    (wParam == 0x0d && (lParam & 0x01000000L)))
                    modifiers |= XKeyboardModifier_KeypadModifier;
                if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
                    /* bit30 由系统在按住键期间标记自动重复节奏。 */
                    autoRepeat = (lParam & 0x40000000L) != 0;
                    XWindowSystemInterface_handleKeyEvent(
                        entry->m_window, XEVENT_TYPE_KEY_PRESS,
                        key, modifiers, autoRepeat);
                } else {
                    XWindowSystemInterface_handleKeyEvent(
                        entry->m_window, XEVENT_TYPE_KEY_RELEASE,
                        key, modifiers, false);
                }
            }
        }
        return 0;
    }

    /* ============ 鼠标按键（含系统双击，对标 Qt 键鼠消息翻译） ============ */
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    {
        XMouseButton button;
        XEventType type;
        XPoint position;
        XMouseButton buttons;
        XKeyboardModifiers modifiers;
        if (entry && entry->m_window) {
            type = xpwn_mouseMessageEvent(msg, wParam, &button);
            if (type != XEVENT_TYPE_NONE &&
                button != XMouseButton_NoButton) {
                position.x = (int)(short)LOWORD(lParam);
                position.y = (int)(short)HIWORD(lParam);
                /* 按下集合：消息 wParam 的 MK_* 位并上触发键（释放时
                   wParam 不含本键，OR 不改变集合），与 X11 后端一致。 */
                buttons = xpwn_translateButtons(wParam) | button;
                modifiers = xpwn_translateModifiers();
                XWindowSystemInterface_handleMouseEvent(
                    entry->m_window, type, button, buttons, modifiers,
                    position);
            }
        }
        return 0;
    }

    /* ============ 鼠标移动与进入/离开追踪（对标 Qt Windows 后端） ============ */
    case WM_MOUSEMOVE:
        if (entry && entry->m_window) {
            XPoint position;
            XPoint globalPosition;
            POINT pt;
            position.x = (int)(short)LOWORD(lParam);
            position.y = (int)(short)HIWORD(lParam);
            if (!entry->m_mouseInside) {
                /* 指针首次进入客户区：先注入进入事件，再开启一次性离开
                   追踪（TME_LEAVE），收到 WM_MOUSELEAVE 后清标记。 */
                TRACKMOUSEEVENT tme;
                pt.x = position.x;
                pt.y = position.y;
                if (ClientToScreen(hwnd, &pt)) {
                    globalPosition.x = pt.x;
                    globalPosition.y = pt.y;
                } else {
                    globalPosition.x = position.x;
                    globalPosition.y = position.y;
                }
                XWindowSystemInterface_handleEnterEvent(
                    entry->m_window, position, &globalPosition);
                entry->m_mouseInside = true;
                memset(&tme, 0, sizeof(tme));
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            XWindowSystemInterface_handleMouseEvent(
                entry->m_window, XEVENT_TYPE_MOUSE_MOVE,
                XMouseButton_NoButton, xpwn_translateButtons(wParam),
                xpwn_translateModifiers(), position);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (entry) entry->m_mouseInside = false;
        if (entry && entry->m_window) {
            XWindowSystemInterface_handleLeaveEvent(entry->m_window);
        }
        return 0;

    /* ============ 滚轮事件（垂直/水平，Qt 约定 ±120/格） ============ */
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    {
        XPoint position;
        XPoint globalPosition;
        XPoint angleDelta;
        short delta;
        if (entry && entry->m_window) {
            /* 滚轮消息 lParam 为屏幕坐标：先转回客户区作为局部坐标。 */
            xpwn_getMousePos(hwnd, lParam, true, &position, &globalPosition);
            delta = (short)HIWORD(wParam);
            angleDelta.x = 0;
            angleDelta.y = 0;
            if (msg == WM_MOUSEWHEEL) {
                /* 远离用户滚动为正（向上）：Qt 约定 y 正向。 */
                angleDelta.y = delta > 0 ? 120 : -120;
            } else {
                /* WM_MOUSEHWHEEL 正值为向右倾斜：Qt 约定 x 正向。 */
                angleDelta.x = delta > 0 ? 120 : -120;
            }
            XWindowSystemInterface_handleWheelEvent(
                entry->m_window, xpwn_translateButtons(wParam),
                xpwn_translateModifiers(), position, &angleDelta);
        }
        return 0;
    }

#if XCLIPBOARD_ON
    case WM_CLIPBOARDUPDATE:
        /* 剪贴板内容变化通知：监听只挂在专用剪贴板窗口上，外部应用
           认领系统剪贴板时在此反向通知上层（本进程写入触发的更新由
           处理函数按属主判断忽略）。 */
        if (hwnd == g_xpwnClipHwnd) {
            xpwn_clipHandleClipboardUpdate();
            return 0;
        }
        break;
#endif /* XCLIPBOARD_ON */
    case WM_SHOWWINDOW:
        if (entry) entry->m_visible = (wParam != FALSE);
        return 0;
    default:
        break;
    }
    (void)window;
    return xpwn_callPreviousProc(entry, hwnd, msg, wParam, lParam);
}

/* ==================== 事件泵（平台后端提供） ==================== */

bool XPlatformNativeWindow_processPendingEvents(void)
{
    MSG msg;
    bool delivered = false;
    BOOL got;
    if (!g_xpwnClassRegistered) return false;
    while ((got = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) != 0) {
        if (got == -1) break; /* 出错：终止本次泵空。 */
        if (msg.message == WM_QUIT) {
            /* WM_QUIT 不派发；仅记录，供未来退出策略使用。 */
            g_xpwnQuitReceived = true;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        delivered = true;
    }
    return delivered;
}

/* 主循环双源等待（对标 QEventDispatcher 的统一等待点，与 POSIX 端
 * waitForEvents 的 X11 fd + ring fd 双源 poll 语义镜像）：消息队列经
 * QS_ALLINPUT 参与，全局 ring 的 IOCP 端口句柄作为可等待对象参与
 * （完成包入队即变信号态）。醒来分源处理：IOCP 就绪先非阻塞批量
 * drain（processReady 内部 pollPlatform -> 排空 SQ -> drainCQ ->
 * dispatchCQEntry），消息就绪再泵空——先 IOCP 后消息，避免高频消息
 * 反复抢占饿死网络完成。网络模块裁剪（XAbstractNetIoRing_ON=0）或
 * ring 未启用时退化为单源等待，行为与既往一致。约定：启用双源等待
 * 期间，ring 自身的 waitForEvents 阻塞路径不得另处使用（单线程模型
 * 下自然成立；两处竞争同一 IOCP 队列会互偷完成包）。 */
bool XPlatformNativeWindow_waitForEvents(int maxMilliseconds)
{
    DWORD rc;
    DWORD msec;
#if XAbstractNetIoRing_ON
    HANDLE handles[1];
    DWORD handleCount = 0;
    bool ringReady = false;
    XAbstractNetIoRing* ring = NULL;
    ring = XAbstractNetIoRing_global();
    if (ring && XAbstractNetIoRing_isEnabled(ring)) {
        HANDLE iocp = XNetIoRingWin32_iocpHandle(
            (XNetIoRingWin32*)ring);
        if (iocp && iocp != INVALID_HANDLE_VALUE) {
            handles[handleCount++] = iocp;
        }
    }
#endif /* XAbstractNetIoRing_ON */
    if (!xpwn_ensureInstance()) return false;
    msec = maxMilliseconds < 0 ? INFINITE : (DWORD)maxMilliseconds;
#if XAbstractNetIoRing_ON
    rc = MsgWaitForMultipleObjects(handleCount, handleCount ? handles : NULL,
                                   FALSE, msec, QS_ALLINPUT);
    if (handleCount && rc == WAIT_OBJECT_0) {
        /* IOCP 就绪：批量 drain 完成包并投递事件（非阻塞语义由
           processReady 内部 pollPlatform 的 GQCS(timeout=0) 保证）。 */
        ring = XAbstractNetIoRing_global();
        if (ring && XAbstractNetIoRing_isEnabled(ring)) {
            XAbstractNetIoRing_processReady(ring);
            ringReady = true;
        }
    }
    if (rc == WAIT_OBJECT_0 + handleCount || rc == WAIT_TIMEOUT) {
        /* 消息信号（WAIT_OBJECT_0 + nCount）或超时（超时也可能是 IOCP
           句柄在 msec 内未入包）：消息侧一律再尝试泵一轮，保持与
           单源版本相同的唤醒后必泵语义。 */
        return XPlatformNativeWindow_processPendingEvents();
    }
    if (rc > WAIT_OBJECT_0 && rc <= WAIT_OBJECT_0 + handleCount) {
        /* 句柄区间其他索引（当前仅 1 个句柄，防御性处理）。 */
        ring = XAbstractNetIoRing_global();
        if (ring && XAbstractNetIoRing_isEnabled(ring)) {
            XAbstractNetIoRing_processReady(ring);
            ringReady = true;
        }
        return XPlatformNativeWindow_processPendingEvents();
    }
    if (rc == WAIT_FAILED) return false;
    /* ringReady 分支未泵消息：网络事件已投递，本次无 GUI 事件可泵，
       与 POSIX 版「仅 ring 就绪返回 false」的语义一致。 */
    (void)ringReady;
    return false;
#else
    rc = MsgWaitForMultipleObjects(0u, NULL, FALSE, msec, QS_ALLINPUT);
    if (rc != WAIT_OBJECT_0) return false;
    return XPlatformNativeWindow_processPendingEvents();
#endif /* XAbstractNetIoRing_ON */
}

bool XPlatformNativeWindow_queryKeyboardModifiers(
        XKeyboardModifiers* outModifiers)
{
    if (!outModifiers || !xpwn_ensureInstance()) return false;
    *outModifiers = xpwn_translateModifiers();
    return true;
}

/* ==================== Win32 CLIPBOARD 后端（系统剪贴板 API） ====================
 * 对标 QWindowsClipboard：应用复制时 OpenClipboard + EmptyClipboard
 * 认领系统剪贴板并立即渲染数据（不做延迟渲染，无需 WM_RENDERFORMAT），
 * 其他应用粘贴由系统直接回数；本进程属主状态经 WM_CLIPBOARDUPDATE
 * 监听，外部应用认领时经 selectionRevoked 反向通知上层（对标
 * QXcbClipboard 的 SelectionClear 通知路径）。文本走 CF_UNICODETEXT
 * （UTF-8 <-> UTF-16 转换，旧程序 ANSI 文本回退），HTML 走
 * "HTML Format"（CF_HTML 0.9 协议头，片段偏移按 UTF-8 字节计），
 * image/png 走注册格式 "PNG"/"image/png" 原样透传（解码在
 * XClipboard_image 经 XImageCodec 完成，与 X11 后端透传策略一致）。
 * 分配器纪律：text 回调结果按契约以 XMalloc_System 分配（调用方
 * XFree_System 释放），mimeData 借用接收缓冲同为 System 族，各分配
 * 与释放严格同族配对；GlobalAlloc 块在 SetClipboardData 成功后归
 * 系统所有，失败路径自行 GlobalFree。 */
#if XCLIPBOARD_ON

/** @brief 本进程写入会话的 mime 格式名单容量（与 X11 后端镜像同规模）。 */
#define XPWN_CLIP_MAX_FORMATS 8

/** @brief 剪贴板属主状态（写入会话）：mime 名单 + 持有标志。 */
static bool g_xpwnClipOwnSession; /**< 本进程当前持有系统剪贴板。 */
static char g_xpwnClipFormatNames[XPWN_CLIP_MAX_FORMATS]
                                 [XCLIPBOARD_FORMAT_NAME_MAX];
static int g_xpwnClipFormatCount; /**< 会话名单实际个数。 */

/** @brief mimeData 借用语义接收缓冲（System 分配；数据在下次后端调用
 *  前有效，对标 X11 后端 m_recv）。 */
static unsigned char* g_xpwnClipRecv;
static int g_xpwnClipRecvLen;

/** @brief 注册格式句柄缓存（RegisterClipboardFormatW 进程内幂等）。 */
static UINT g_xpwnClipFmtHtml;
static UINT g_xpwnClipFmtPng;
static UINT g_xpwnClipFmtPngMime;

/** @brief 复位写入会话（仅本地名单与标志；不动系统剪贴板）。 */
static void xpwn_clipResetSession(void)
{
    g_xpwnClipFormatCount = 0;
    g_xpwnClipOwnSession = false;
}

/** @brief 定长拷贝 mime 格式名（含结束符；避免 MSVC strn* 安全告警）。 */
static void xpwn_clipCopyFormatName(char* dst, const char* src)
{
    int i;
    for (i = 0; i + 1 < XCLIPBOARD_FORMAT_NAME_MAX && src[i]; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

/** @brief 在会话名单登记 mime 格式（已存在或名单满则跳过）。 */
static void xpwn_clipSessionAdd(const char* mime)
{
    int i;
    if (!mime || !mime[0]) return;
    for (i = 0; i < g_xpwnClipFormatCount; ++i) {
        if (strcmp(g_xpwnClipFormatNames[i], mime) == 0) return;
    }
    if (g_xpwnClipFormatCount >= XPWN_CLIP_MAX_FORMATS) return;
    xpwn_clipCopyFormatName(g_xpwnClipFormatNames[g_xpwnClipFormatCount],
                            mime);
    ++g_xpwnClipFormatCount;
}

/** @brief 惰性创建剪贴板专用隐藏窗口并注册更新监听（幂等）。 */
static bool xpwn_clipEnsureWindow(void)
{
    if (g_xpwnClipHwnd) return true;
    if (!xpwn_ensureInstance()) return false;
    /* 专用隐藏弹窗：不进入窗口注册表（lpCreateParams 为空，
       GWLP_USERDATA 恒 NULL），消息经 WndProc default 分支走
       DefWindowProc，对既有窗口路由零影响（对标 QWindowsClipboard
       的专用 clipboard 窗口）。 */
    g_xpwnClipHwnd = CreateWindowExW(0, XPWN_CLASS_NAME, L"XinYueCClipboard",
                                     WS_POPUP, 0, 0, 0, 0,
                                     NULL, NULL, g_xpwnInstance, NULL);
    if (!g_xpwnClipHwnd) return false;
    AddClipboardFormatListener(g_xpwnClipHwnd);
    return true;
}

/** @brief 打开剪贴板（独占资源，与其他进程竞争时短退避重试）。 */
static bool xpwn_clipOpen(void)
{
    int attempt;
    for (attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(g_xpwnClipHwnd)) return true;
        Sleep(2);
    }
    return false;
}

/** @brief 取注册格式句柄（惰性注册并缓存；失败返回 0）。 */
static UINT xpwn_clipRegisteredFormat(LPCWSTR name, UINT* cache)
{
    if (*cache == 0) *cache = RegisterClipboardFormatW(name);
    return *cache;
}

/** @brief 全局块内 UTF-16 文本的有效长度（按第一个 NUL 截断，返回
 *  wchar 元素个数；GlobalSize 为分配粒度，可能大于实际文本）。 */
static int xpwn_clipWideCount(const wchar_t* wide, SIZE_T bytes)
{
    SIZE_T i;
    SIZE_T count = bytes / sizeof(wchar_t);
    for (i = 0; i < count; ++i) {
        if (wide[i] == L'\0') break;
    }
    return (int)i;
}

/** @brief UTF-16 转 UTF-8（System 分配器分配；clipboard text 契约要求
 *  调用方 XFree_System 释放，与 Hybrid 系分配器不可混用）。 */
static char* xpwn_clipWideToUtf8System(const wchar_t* wide, int wcharCount)
{
    char* utf8;
    int bytes;
    if (!wide || wcharCount <= 0) return NULL;
    bytes = WideCharToMultiByte(CP_UTF8, 0, wide, wcharCount,
                                NULL, 0, NULL, NULL);
    if (bytes <= 0) return NULL;
    utf8 = (char*)XMalloc_System((size_t)bytes + 1u);
    if (!utf8) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, wcharCount,
                            utf8, bytes, NULL, NULL) != bytes) {
        XFree_System(utf8);
        return NULL;
    }
    utf8[bytes] = '\0';
    return utf8;
}

/** @brief CF_TEXT（ANSI）转 UTF-8：CP_ACP -> UTF-16 两跳（旧程序仅登记
 *  ANSI 文本的兜底路径）。 */
static char* xpwn_clipAnsiToUtf8System(const char* ansi)
{
    int wchars;
    wchar_t* wide;
    char* utf8;
    if (!ansi || !ansi[0]) return NULL;
    wchars = MultiByteToWideChar(CP_ACP, 0, ansi, -1, NULL, 0); /* 含 NUL。 */
    if (wchars <= 1) return NULL;
    wide = (wchar_t*)XMalloc_Hybrid(sizeof(wchar_t) * (size_t)wchars);
    if (!wide) return NULL;
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, wchars);
    utf8 = xpwn_clipWideToUtf8System(wide, wchars - 1); /* 去掉 NUL。 */
    XFree_Hybrid(wide);
    return utf8;
}

/** @brief 字节串复制进 GMEM_MOVEABLE 全局块（SetClipboardData 前置；
 *  成功提交后所有权移交系统，失败路径由调用方 GlobalFree）。 */
static HGLOBAL xpwn_clipRawGlobal(const unsigned char* data, int len)
{
    HGLOBAL handle;
    void* dst;
    if (len <= 0) return NULL;
    handle = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len);
    if (!handle) return NULL;
    dst = GlobalLock(handle);
    if (!dst) {
        GlobalFree(handle);
        return NULL;
    }
    XMemcpy(dst, data, (size_t)len);
    GlobalUnlock(handle);
    return handle;
}

/** @brief 写 10 位定宽十进制（CF_HTML 偏移字段，含前导零）。 */
static void xpwn_clipWriteOffset10(char* dst, int value)
{
    int i;
    for (i = 9; i >= 0; --i) {
        dst[i] = (char)('0' + value % 10);
        value /= 10;
    }
}

/** @brief 构造 "HTML Format"（CF_HTML 0.9 协议）全局块；布局对标
 *  QWindowsMimeHtml::convertFromMime：定宽偏移头 + 固定包裹骨架，
 *  偏移按 UTF-8 字节计。失败返回 NULL。 */
static HGLOBAL xpwn_clipBuildHtmlFormat(const unsigned char* html, int len)
{
    static const char kHead[] =
        "Version:0.9\r\n"
        "StartHTML:0000000000\r\nEndHTML:0000000000\r\n"
        "StartFragment:0000000000\r\nEndFragment:0000000000\r\n";
    static const char kPre[] = "<html><body>\r\n<!--StartFragment-->";
    static const char kPost[] = "<!--EndFragment-->\r\n</body>\r\n</html>";
    const int headLen = (int)sizeof(kHead) - 1;
    const int startHtml = headLen;
    const int fragStart = startHtml + (int)(sizeof(kPre) - 1);
    const int fragEnd = fragStart + len;
    const int endHtml = fragEnd + (int)(sizeof(kPost) - 1);
    HGLOBAL handle;
    char* payload;
    char* dst;
    if (len <= 0) return NULL;
    handle = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)endHtml + 1u);
    if (!handle) return NULL;
    payload = (char*)GlobalLock(handle);
    if (!payload) {
        GlobalFree(handle);
        return NULL;
    }
    XMemcpy(payload, kHead, (size_t)headLen);
    dst = payload + headLen;
    XMemcpy(dst, kPre, sizeof(kPre) - 1);
    dst += (int)(sizeof(kPre) - 1);
    XMemcpy(dst, html, (size_t)len);
    dst += len;
    XMemcpy(dst, kPost, sizeof(kPost) - 1);
    /* 回填偏移：Version 行后依次 StartHTML/EndHTML/StartFragment/
       EndFragment 的 10 位数字段（段长为协议定长，逐段推进填写）。 */
    dst = payload;
    dst += sizeof("Version:0.9\r\n") - 1;
    dst += sizeof("StartHTML:") - 1;
    xpwn_clipWriteOffset10(dst, startHtml);
    dst += 10 + (int)(sizeof("\r\n") - 1);
    dst += sizeof("EndHTML:") - 1;
    xpwn_clipWriteOffset10(dst, endHtml);
    dst += 10 + (int)(sizeof("\r\n") - 1);
    dst += sizeof("StartFragment:") - 1;
    xpwn_clipWriteOffset10(dst, fragStart);
    dst += 10 + (int)(sizeof("\r\n") - 1);
    dst += sizeof("EndFragment:") - 1;
    xpwn_clipWriteOffset10(dst, fragEnd);
    GlobalUnlock(handle);
    return handle;
}

/** @brief 在 CF_HTML 头部找 "Key:<十进制>" 偏移值（缺省返回 -1）。 */
static int xpwn_clipHtmlHeaderValue(const char* payload, int payloadLen,
                                    const char* key)
{
    int keyLen = (int)strlen(key);
    int limit = payloadLen - keyLen;
    int i;
    if (limit > 512) limit = 512; /* 协议头区域恒在载荷前部。 */
    for (i = 0; i < limit; ++i) {
        if (payload[i] == key[0] &&
            memcmp(payload + i, key, (size_t)keyLen) == 0) {
            int value = 0;
            int j = i + keyLen;
            if (j >= payloadLen || payload[j] != ':') continue;
            for (++j; j < payloadLen && payload[j] >= '0'
                      && payload[j] <= '9'; ++j) {
                value = value * 10 + (payload[j] - '0');
            }
            return value;
        }
    }
    return -1;
}

/** @brief 把字节串落入接收缓冲并返回借用指针（mimeData 借用语义落点；
 *  缓冲复用/扩容均在 System 族内配对）。 */
static const unsigned char* xpwn_clipRecvStore(const unsigned char* data,
                                               int len)
{
    unsigned char* buf;
    if (len <= 0) return NULL;
    buf = (unsigned char*)XRealloc_System(g_xpwnClipRecv, (size_t)len);
    if (!buf) return NULL;
    XMemcpy(buf, data, (size_t)len);
    g_xpwnClipRecv = buf;
    g_xpwnClipRecvLen = len;
    return buf;
}

/* 后端 mimeData 回调：按 mime 名读系统剪贴板并落入接收缓冲（借用
 * 语义：*data 仅在下次后端调用前有效）。text/plain 读 CF_UNICODETEXT
 * （回退 CF_TEXT）；text/html 解析 CF_HTML 片段偏移后截取片段；
 * image/png 读注册格式原始字节。 */
static bool xpwn_clipBackendMimeData(void* ud, int mode, const char* format,
                                     const unsigned char** data, int* len)
{
    const unsigned char* payload = NULL;
    int payloadLen = 0;
    (void)ud;
    if (!data || !len || !format ||
        mode != (int)XClipboardMode_Clipboard)
        return false;
    *data = NULL;
    *len = 0;
    if (!xpwn_clipEnsureWindow()) return false;
    if (strcmp(format, "text/plain") == 0) {
        HGLOBAL handle;
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT) &&
            !IsClipboardFormatAvailable(CF_TEXT))
            return false;
        if (!xpwn_clipOpen()) return false;
        handle = (HGLOBAL)GetClipboardData(CF_UNICODETEXT);
        if (handle) {
            const wchar_t* wide = (const wchar_t*)GlobalLock(handle);
            if (wide) {
                SIZE_T bytes = GlobalSize(handle);
                char* utf8 = NULL;
                if (bytes >= sizeof(wchar_t))
                    utf8 = xpwn_clipWideToUtf8System(
                        wide, xpwn_clipWideCount(wide, bytes));
                GlobalUnlock(handle);
                if (utf8) {
                    payloadLen = (int)strlen(utf8);
                    payload = xpwn_clipRecvStore((const unsigned char*)utf8,
                                                 payloadLen);
                    XFree_System(utf8);
                }
            }
        } else {
            handle = (HGLOBAL)GetClipboardData(CF_TEXT);
            if (handle) {
                const char* ansi = (const char*)GlobalLock(handle);
                if (ansi) {
                    char* utf8 = xpwn_clipAnsiToUtf8System(ansi);
                    GlobalUnlock(handle);
                    if (utf8) {
                        payloadLen = (int)strlen(utf8);
                        payload = xpwn_clipRecvStore(
                            (const unsigned char*)utf8, payloadLen);
                        XFree_System(utf8);
                    }
                }
            }
        }
        CloseClipboard();
    } else if (strcmp(format, "text/html") == 0) {
        UINT fmt = xpwn_clipRegisteredFormat(L"HTML Format",
                                             &g_xpwnClipFmtHtml);
        HGLOBAL handle;
        if (!fmt || !IsClipboardFormatAvailable(fmt)) return false;
        if (!xpwn_clipOpen()) return false;
        handle = (HGLOBAL)GetClipboardData(fmt);
        if (handle) {
            const char* raw = (const char*)GlobalLock(handle);
            if (raw) {
                SIZE_T bytes = GlobalSize(handle);
                int start = xpwn_clipHtmlHeaderValue(raw, (int)bytes,
                                                     "StartFragment");
                int end = xpwn_clipHtmlHeaderValue(raw, (int)bytes,
                                                   "EndFragment");
                int from;
                int to;
                GlobalUnlock(handle);
                /* 头部解析成功取片段字节（CF_HTML 片段偏移按字节计）；
                   缺头/越界回退整段载荷。 */
                if (start >= 0 && end > start && end <= (int)bytes) {
                    from = start;
                    to = end;
                } else {
                    from = 0;
                    to = (int)bytes;
                }
                payloadLen = to - from;
                payload = xpwn_clipRecvStore((const unsigned char*)raw + from,
                                             payloadLen);
            }
        }
        CloseClipboard();
    } else if (strcmp(format, "image/png") == 0) {
        UINT fmt = xpwn_clipRegisteredFormat(L"PNG", &g_xpwnClipFmtPng);
        HGLOBAL handle;
        if (!fmt || !IsClipboardFormatAvailable(fmt))
            fmt = xpwn_clipRegisteredFormat(L"image/png",
                                            &g_xpwnClipFmtPngMime);
        if (!fmt || !IsClipboardFormatAvailable(fmt)) return false;
        if (!xpwn_clipOpen()) return false;
        handle = (HGLOBAL)GetClipboardData(fmt);
        if (handle) {
            const unsigned char* raw = (const unsigned char*)GlobalLock(handle);
            if (raw) {
                /* 注册格式载荷按 GlobalSize 透传（PNG 解码止于 IEND 块，
                   分配粒度补零不影响；对标 X11 原样透传策略）。 */
                payloadLen = (int)GlobalSize(handle);
                payload = xpwn_clipRecvStore(raw, payloadLen);
                GlobalUnlock(handle);
            }
        }
        CloseClipboard();
    } else {
        return false;
    }
    if (!payload || payloadLen <= 0) return false;
    *data = payload;
    *len = payloadLen;
    return true;
}

/* 后端 setMimeData 回调：认领所有权（首次 EmptyClipboard）后逐格式
 * 写系统板（对标 QXcbClipboard::setMimeData 逐格式登记；XClipboard
 * 上层先行的 clear 后端回调已 Empty，此处对独立调用兜底）。 */
static bool xpwn_clipBackendSetMimeData(void* ud, int mode, const char* format,
                                        const unsigned char* data, int len)
{
    UINT winFmt;
    HGLOBAL handle = NULL;
    bool ok = false;
    (void)ud;
    if (!format || !data || len <= 0 ||
        mode != (int)XClipboardMode_Clipboard)
        return false;
    if (!xpwn_clipEnsureWindow() || !xpwn_clipOpen()) return false;
    if (!g_xpwnClipOwnSession || GetClipboardOwner() != g_xpwnClipHwnd) {
        /* 首次认领（或外部期间被夺）：清空系统板并把属主设为专用
           窗口，同时开启本进程写入会话。 */
        EmptyClipboard();
        xpwn_clipResetSession();
        g_xpwnClipOwnSession = true;
    }
    if (strcmp(format, "text/plain") == 0) {
        /* UTF-8 -> UTF-16 后按 CF_UNICODETEXT 登记（Windows 文本标准
           格式，须 NUL 结尾；对标 QWindowsMimeText）。 */
        int wchars = MultiByteToWideChar(CP_UTF8, 0, (const char*)data,
                                         len, NULL, 0);
        if (wchars > 0) {
            handle = GlobalAlloc(GMEM_MOVEABLE,
                                 sizeof(wchar_t) * ((size_t)wchars + 1u));
            if (handle) {
                wchar_t* wide = (wchar_t*)GlobalLock(handle);
                if (wide) {
                    MultiByteToWideChar(CP_UTF8, 0, (const char*)data,
                                        len, wide, wchars);
                    wide[wchars] = L'\0';
                    GlobalUnlock(handle);
                } else {
                    GlobalFree(handle);
                    handle = NULL;
                }
            }
        }
        winFmt = CF_UNICODETEXT;
    } else if (strcmp(format, "text/html") == 0) {
        winFmt = xpwn_clipRegisteredFormat(L"HTML Format",
                                           &g_xpwnClipFmtHtml);
        handle = xpwn_clipBuildHtmlFormat(data, len);
    } else if (strcmp(format, "image/png") == 0) {
        /* 注册格式 "PNG"（Chromium/Office 等的剪贴板约定名）原样透传。 */
        winFmt = xpwn_clipRegisteredFormat(L"PNG", &g_xpwnClipFmtPng);
        handle = xpwn_clipRawGlobal(data, len);
    } else {
        /* 其余 mime 名原样注册为 Windows 注册格式后透传（对标 X11
           后端以 mime 名作目标原子）。 */
        winFmt = RegisterClipboardFormatA(format);
        if (winFmt == 0) {
            CloseClipboard();
            return false;
        }
        handle = xpwn_clipRawGlobal(data, len);
    }
    if (handle) {
        /* SetClipboardData 成功后 HGLOBAL 归系统所有；失败必须自释放。 */
        if (SetClipboardData(winFmt, handle)) {
            xpwn_clipSessionAdd(format);
            ok = true;
        } else {
            GlobalFree(handle);
        }
    }
    CloseClipboard();
    return ok;
}

/* 后端 text 回调：读系统剪贴板文本（CF_UNICODETEXT 优先，回退
 * CF_TEXT）转 UTF-8 返回；结果按契约以 System 分配器分配（调用方
 * XFree_System 释放）。不支持模式（Selection/FindBuffer）返回 false，
 * 与 X11 后端原子为 None 的分支一致。 */
static bool xpwn_clipBackendText(void* ud, int mode, char** outText)
{
    char* utf8 = NULL;
    (void)ud;
    if (!outText || mode != (int)XClipboardMode_Clipboard) return false;
    *outText = NULL;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) &&
        !IsClipboardFormatAvailable(CF_TEXT))
        return false;
    if (!xpwn_clipEnsureWindow() || !xpwn_clipOpen()) return false;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HGLOBAL handle = (HGLOBAL)GetClipboardData(CF_UNICODETEXT);
        const wchar_t* wide =
            handle ? (const wchar_t*)GlobalLock(handle) : NULL;
        if (wide) {
            SIZE_T bytes = GlobalSize(handle);
            if (bytes >= sizeof(wchar_t))
                utf8 = xpwn_clipWideToUtf8System(
                    wide, xpwn_clipWideCount(wide, bytes));
            GlobalUnlock(handle);
        }
    } else {
        HGLOBAL handle = (HGLOBAL)GetClipboardData(CF_TEXT);
        const char* ansi = handle ? (const char*)GlobalLock(handle) : NULL;
        if (ansi) {
            utf8 = xpwn_clipAnsiToUtf8System(ansi);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    if (!utf8) return false;
    *outText = utf8;
    return true;
}

/* 后端 setText 回调：归一到 setMimeData 的 text/plain 写入路径（与
 * X11 后端 setText/setMimeData 镜像互通设计一致）。 */
static bool xpwn_clipBackendSetText(void* ud, int mode, const char* text)
{
    if (!text) return false;
    return xpwn_clipBackendSetMimeData(ud, mode, "text/plain",
                                       (const unsigned char*)text,
                                       (int)strlen(text));
}

/* 后端 clear 回调：EmptyClipboard 清空数据并把属主设为本进程专用
 * 窗口（空板同样登记为本进程会话，使后续逐格式写入不再重复 Empty）；
 * 不支持模式无操作（对齐 X11 后端原子为 None 的分支）。 */
static bool xpwn_clipBackendClear(void* ud, int mode)
{
    (void)ud;
    if (mode != (int)XClipboardMode_Clipboard) return true;
    if (!xpwn_clipEnsureWindow() || !xpwn_clipOpen()) return false;
    EmptyClipboard();
    xpwn_clipResetSession();
    g_xpwnClipOwnSession = true;
    CloseClipboard();
    return true;
}

/* 后端 formats 回调：本进程会话→回报名单；外部所有→枚举系统格式并
 * 把已知代理名映射为 mime（text/plain、text/html、image/png；未知
 * 注册格式暂不暴露，对标 Qt 平台映射已知格式集合）。 */
static int xpwn_clipBackendFormats(void* ud, int mode,
                                   char outFormats[][64], int max)
{
    int count = 0;
    (void)ud;
    if (!outFormats || max <= 0 || mode != (int)XClipboardMode_Clipboard)
        return 0;
    if (!xpwn_clipEnsureWindow()) return 0;
    if (g_xpwnClipOwnSession && GetClipboardOwner() == g_xpwnClipHwnd) {
        int i;
        for (i = 0; i < g_xpwnClipFormatCount && count < max; ++i) {
            xpwn_clipCopyFormatName(outFormats[count],
                                    g_xpwnClipFormatNames[i]);
            ++count;
        }
        return count;
    }
    if (CountClipboardFormats() == 0) return 0; /* 空板快速返回。 */
    if (!xpwn_clipOpen()) return 0;
    {
        UINT fmt = 0;
        bool havePlain = false;
        wchar_t name[XCLIPBOARD_FORMAT_NAME_MAX];
        while ((fmt = EnumClipboardFormats(fmt)) != 0 && count < max) {
            const char* mapped = NULL;
            if (fmt == CF_UNICODETEXT || fmt == CF_TEXT) {
                if (!havePlain) {
                    mapped = "text/plain";
                    havePlain = true;
                }
            } else if (fmt >= 0xC000u) {
                /* 注册格式：已知代理名映射为 mime（对标 X11 TARGETS
                   的原子名映射）。 */
                if (GetClipboardFormatNameW(
                        fmt, name,
                        (int)(sizeof(name) / sizeof(name[0]) - 1)) > 0) {
                    if (wcscmp(name, L"HTML Format") == 0)
                        mapped = "text/html";
                    else if (wcscmp(name, L"PNG") == 0 ||
                             wcscmp(name, L"image/png") == 0)
                        mapped = "image/png";
                }
            }
            if (mapped) {
                xpwn_clipCopyFormatName(outFormats[count], mapped);
                ++count;
            }
        }
    }
    CloseClipboard();
    return count;
}

/** @brief WM_CLIPBOARDUPDATE 处理：外部应用认领系统剪贴板时复位本
 *  进程会话并经后端契约反向通知上层（对标 QXcbClipboard 的
 *  SelectionClear -> handleSelectionClearRequest：复位 ownerData 并
 *  发射 changed）。本进程写入触发的更新（属主仍为专用窗口）忽略。
 *  定义位于 g_xpwnClipBackend 之后（引用其成员）。 */
static XClipboardBackend g_xpwnClipBackend = {
    NULL,                               /* ud（平台用户数据）。 */
    xpwn_clipBackendText,               /* text */
    xpwn_clipBackendSetText,            /* setText */
    xpwn_clipBackendClear,              /* clear */
    false,                              /* supportsSelection（Win32 无
                                           PRIMARY 选择区，Selection 模式
                                           保持进程内语义）。 */
    XClipboard_backendSelectionRevoked, /* selectionRevoked（反向通知
                                           入口）。 */
    xpwn_clipBackendFormats,            /* formats（mime 多格式枚举）。 */
    xpwn_clipBackendMimeData,           /* mimeData（按格式借用读取）。 */
    xpwn_clipBackendSetMimeData         /* setMimeData（逐格式写系统板）。 */
};

static void xpwn_clipHandleClipboardUpdate(void)
{
    if (GetClipboardOwner() == g_xpwnClipHwnd) return;
    if (!g_xpwnClipOwnSession) return;
    xpwn_clipResetSession();
    if (g_xpwnClipBackend.selectionRevoked)
        g_xpwnClipBackend.selectionRevoked(g_xpwnClipBackend.ud,
                                           (int)XClipboardMode_Clipboard);
}

void XPlatformNativeWindow_installClipboardBackend(void)
{
    XClipboard_installBackend(&g_xpwnClipBackend);
}

#endif /* XCLIPBOARD_ON */

/* ==================== 可用性与生命周期（平台后端提供） ==================== */

bool XPlatformNativeWindow_isAvailable(void)
{
    return xpwn_ensureInstance();
}

bool XPlatformNativeWindow_create(XWindow* window)
{
    XWNPendingEntry* entry;
    HWND hwnd;
    XRect geom;
    RECT rc;
    int w, h;
    XString* title;
    if (!window) return false;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (entry) return true; /* 幂等：已登记直接成功。 */
    entry = xpwn_findFreeSlot();
    if (!entry) return false;

    geom = XWindow_geometry(window);
    w = geom.width < 1 ? 1 : geom.width;
    h = geom.height < 1 ? 1 : geom.height;
    xpwn_adjustWindowRect(window, &geom, &rc);
    hwnd = CreateWindowExW(0, XPWN_CLASS_NAME, L"",
                           xpwn_windowStyle(window),
                           rc.left, rc.top,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, NULL, g_xpwnInstance, window);
    if (!hwnd) return false;

    entry->m_hwnd = hwnd;
    entry->m_window = window;
    entry->m_visible = false;
    if (!xpwn_getClientGeometry(hwnd, &entry->m_client))
    entry->m_client = geom;
    DragAcceptFiles(hwnd, TRUE);
    /* 初始标题同步（公共层 createHandle 后也会再同步，这里是兜底）。 */
    title = XWindow_title(window);
    xpwn_applyTitle(hwnd, title);
    if (title) XString_delete_base((XClass*)title);
    return true;
}

void XPlatformNativeWindow_destroy(XWindow* window)
{
    XWNPendingEntry* entry;
    HWND hwnd;
    if (!window) return;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return;
    hwnd = entry->m_hwnd;
    if (entry->m_oldProc && IsWindow(hwnd))
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)entry->m_oldProc);
    entry->m_hwnd = NULL;
    entry->m_window = NULL;
    entry->m_visible = false;
    entry->m_mouseInside = false;
    entry->m_client = (XRect){0, 0, 0, 0};
    /* 外部窗口只恢复过程并解除登记，不销毁调用方拥有的 HWND。 */
    if (hwnd && IsWindow(hwnd) &&
        XWindow_type(window) != XWindowType_ForeignWindow)
        DestroyWindow(hwnd);
}

bool XPlatformNativeWindow_attachForeign(XWindow* window, XWindowId nativeId)
{
    XWNPendingEntry* entry;
    HWND hwnd = (HWND)(uintptr_t)nativeId;
    WNDPROC oldProc;
    if (!window || !hwnd || !IsWindow(hwnd) || !xpwn_ensureInstance()) return false;
    if (xpwn_findByXWindow(window)) return true;
    entry = xpwn_findFreeSlot();
    if (!entry) return false;
    SetLastError(0);
    oldProc = (WNDPROC)(uintptr_t)SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, (LONG_PTR)xpwn_wndProc);
    if (!oldProc && GetLastError() != 0) return false;
    memset(entry, 0, sizeof(*entry));
    entry->m_hwnd = hwnd;
    entry->m_window = window;
    entry->m_oldProc = oldProc;
    (void)xpwn_getClientGeometry(hwnd, &entry->m_client);
    DragAcceptFiles(hwnd, TRUE);
    return true;
}

/* ==================== 属性同步（平台后端提供） ==================== */

bool XPlatformNativeWindow_setVisible(XWindow* window, bool visible)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    ShowWindow(entry->m_hwnd, visible ? SW_SHOW : SW_HIDE);
    return true;
}

/**
 * @brief      同步窗口状态到 Win32 原生窗口（对标 QPlatformWindow::setWindowState）。
 * @details    Qt 的 windows 平台插件在 setWindowState 里按状态选择
 *             ShowWindow 命令（SW_MAXIMIZE/SW_MINIMIZE/SW_RESTORE）或
 *             对全屏单独处理。这里按 XWindowState_* 位掩码（与 XWindow.h
 *             取值一致：Minimized=0x1、Maximized=0x2、FullScreen=0x4）
 *             选择同一组命令。ShowWindow 同步派发 WM_SIZE，原生几何
 *             记录随之更新；框架侧的窗口几何经既有 WM_SIZE 通路回写。
 *             仅对已创建的原生窗口生效，未创建时安全 no-op。
 * @param      window 目标窗口；可为 NULL。
 * @param      state  状态位掩码；0 恢复普通态。
 * @return     true 已同步；false 未创建/平台不可用。
 */
bool XPlatformNativeWindow_setWindowState(XWindow* window, uint32_t state)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    /* 位值与 XWindowState_* 对齐：Minimized 0x1 / Maximized 0x2 /
       FullScreen 0x4。优先级沿用 XWindow_effectiveState：
       Minimized > FullScreen > Maximized > 普通。 */
    if (state & 0x1u)
        ShowWindow(entry->m_hwnd, SW_MINIMIZE);
    else if (state & 0x4u)
        ShowWindow(entry->m_hwnd, SW_SHOWMAXIMIZED);
    else if (state & 0x2u)
        ShowWindow(entry->m_hwnd, SW_MAXIMIZE);
    else
        ShowWindow(entry->m_hwnd, SW_RESTORE);
    return true;
}

bool XPlatformNativeWindow_setWindowFlags(XWindow* window, uint32_t flags)
{
    /* 能力不足的标志子集默认 no-op 保持链接（TODO：对标
     * QWindowsWindow::setWindowFlags，用 SetWindowLong 重设
     * WS_OVERLAPPEDWINDOW/WS_EX_TOOLWINDOW/WS_EX_TOPMOST/
     * WS_EX_TRANSPARENT/WS_EX_NOACTIVATE 等风格位落地提示位）。 */
    (void)window; (void)flags;
    return false;
}

bool XPlatformNativeWindow_setGeometry(XWindow* window, const XRect* geometry)
{
    XWNPendingEntry* entry;
    RECT rc;
    if (!geometry) return false;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    /* 去重：与最近一次记录/应用的客户端几何一致则跳过（防
       WM_SIZE/WM_MOVE 回环）。 */
    if (geometry->x == entry->m_client.x &&
        geometry->y == entry->m_client.y &&
        geometry->width == entry->m_client.width &&
        geometry->height == entry->m_client.height)
        return true;
    xpwn_adjustWindowRect(window, geometry, &rc);
    SetWindowPos(entry->m_hwnd, NULL, rc.left, rc.top,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    /* SetWindowPos 同步派发 WM_SIZE/WM_MOVE，本后端记录已随之更新；
       若消息被裁剪（例如窗口尚未显示），这里用名义几何兜底。 */
    if (entry->m_client.width == 0)
        entry->m_client = *geometry;
    return true;
}

bool XPlatformNativeWindow_setTitle(XWindow* window, const XString* title)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    xpwn_applyTitle(entry->m_hwnd, title);
    return true;
}

bool XPlatformNativeWindow_setKeyboardGrabEnabled(XWindow* window, bool grab)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    if (grab) {
        SetFocus(entry->m_hwnd);
        return GetFocus() == entry->m_hwnd;
    }
    if (GetFocus() == entry->m_hwnd) SetFocus(NULL);
    return true;
}

bool XPlatformNativeWindow_setMouseGrabEnabled(XWindow* window, bool grab)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    if (grab) {
        SetCapture(entry->m_hwnd);
        return GetCapture() == entry->m_hwnd;
    }
    if (GetCapture() == entry->m_hwnd) ReleaseCapture();
    return true;
}

bool XPlatformNativeWindow_requestActivate(XWindow* window)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    if (!SetForegroundWindow(entry->m_hwnd)) return false;
    SetFocus(entry->m_hwnd);
    return true;
}

XPixmap* XPlatformNativeWindow_grabWindow(XWindowId window,
                                          int x, int y, int w, int h)
{
    HWND hwnd = NULL;
    HDC sourceDc;
    HDC memoryDc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap;
    BITMAPINFO info;
    uint8_t* pixels;
    XImage image;
    XPixmap captured;
    XPixmap* result;
    RECT client;
    int targetWidth;
    int targetHeight;
    int width;
    int height;
    int row;
    int col;
    if (!xpwn_ensureInstance()) return NULL;
    if (window != 0) {
        hwnd = (HWND)(void*)(uintptr_t)window;
        if (!IsWindow(hwnd)) return NULL;
        if (!GetClientRect(hwnd, &client)) return NULL;
        targetWidth = client.right - client.left;
        targetHeight = client.bottom - client.top;
    } else {
        targetWidth = GetSystemMetrics(SM_CXSCREEN);
        targetHeight = GetSystemMetrics(SM_CYSCREEN);
    }
    width = w < 0 ? targetWidth - x : w;
    height = h < 0 ? targetHeight - y : h;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x >= targetWidth || y >= targetHeight || width <= 0 || height <= 0)
        return NULL;
    if (x + width > targetWidth) width = targetWidth - x;
    if (y + height > targetHeight) height = targetHeight - y;
    if (width <= 0 || height <= 0) return NULL;
    sourceDc = GetDC(hwnd);
    if (!sourceDc) return NULL;
    memoryDc = CreateCompatibleDC(sourceDc);
    bitmap = memoryDc ? CreateCompatibleBitmap(sourceDc, width, height) : NULL;
    if (!memoryDc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memoryDc) DeleteDC(memoryDc);
        ReleaseDC(hwnd, sourceDc);
        return NULL;
    }
    oldBitmap = SelectObject(memoryDc, bitmap);
    if (!BitBlt(memoryDc, 0, 0, width, height, sourceDc, x, y,
                SRCCOPY | CAPTUREBLT)) {
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(hwnd, sourceDc);
        return NULL;
    }
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    pixels = (uint8_t*)XMalloc_Hybrid((size_t)width * (size_t)height * 4u);
    if (!pixels || GetDIBits(memoryDc, bitmap, 0, (UINT)height, pixels,
                             &info, DIB_RGB_COLORS) != (UINT)height) {
        if (pixels) XFree_Hybrid(pixels);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(hwnd, sourceDc);
        return NULL;
    }
    XImage_init_ex(&image, width, height, XImageFormat_ARGB32_Premultiplied);
    if (XImage_isNull(&image)) {
        XImage_deinit_base(&image);
        XFree_Hybrid(pixels);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(hwnd, sourceDc);
        return NULL;
    }
    for (row = 0; row < height; ++row) {
        for (col = 0; col < width; ++col) {
            const uint8_t* pixel = pixels + ((size_t)row * (size_t)width +
                                            (size_t)col) * 4u;
            XImage_setPixel(&image, col, row,
                            0xff000000u | ((uint32_t)pixel[2] << 16) |
                            ((uint32_t)pixel[1] << 8) | pixel[0]);
        }
    }
    XFree_Hybrid(pixels);
    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(hwnd, sourceDc);
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
    XMove(result, &captured);
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
    if (!entry || !entry->m_hwnd) return 0;
    return (uintptr_t)(void*)entry->m_hwnd;
}

XWindow* XPlatformNativeWindow_windowForWinId(XWindowId id)
{
    XWNPendingEntry* entry;
    HWND hwnd = (HWND)(void*)(uintptr_t)id;
    if (!hwnd) return NULL;
    entry = xpwn_findByNativeWindow(hwnd);
    return entry ? entry->m_window : NULL;
}

/* ==================== 上屏（平台后端提供） ==================== */

/** @brief 把 XImage 的一块矩形按行重排为等宽 packed 32bpp DIB 并提交。 */
static bool xpwn_presentRect(XWNPendingEntry* entry, const XImage* image,
                             const XRect* srect, int dstX, int dstY)
{
    const uint8_t* sbuf;
    int srcBpl;
    int w, h, row;
    BITMAPINFO bmi;
    HDC hdc;
    uint8_t* buf;
    if (!entry || !entry->m_hwnd || !image || !srect) return false;
    w = srect->width;
    h = srect->height;
    if (w <= 0 || h <= 0) return false;
    sbuf = XImage_constBits(image);
    srcBpl = XImage_bytesPerLine(image);
    if (!sbuf || srcBpl <= 0) return false;
    buf = (uint8_t*)XMalloc_Hybrid((size_t)w * 4u * (size_t)h);
    if (!buf) return false;
    /* 按行拷贝：目标 packed（行宽 w*4），源行宽可任意（含 4 字节对齐垫）。 */
    for (row = 0; row < h; ++row) {
        XMemcpy(buf + (size_t)row * (size_t)w * 4u,
               sbuf + (size_t)(srect->y + row) * (size_t)srcBpl +
                      (size_t)srect->x * 4u,
               (size_t)w * 4u);
    }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    /* 负高度 = 自顶向下 DIB：与 XImage 每行自顶向下的内存布局一致。 */
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    hdc = GetDC(entry->m_hwnd);
    if (!hdc) {
        XFree_Hybrid(buf);
        return false;
    }
    SetDIBitsToDevice(hdc, dstX, dstY, (DWORD)w, (DWORD)h,
                      0, 0, 0, (UINT)h, buf, &bmi, DIB_RGB_COLORS);
    ReleaseDC(entry->m_hwnd, hdc);
    XFree_Hybrid(buf);
    return true;
}

bool XPlatformNativeWindow_present(XWindow* window, const XImage* image,
                                   const XRegion* region,
                                   const XPoint* offset)
{
    XWNPendingEntry* entry;
    XPoint zero;
    const XPoint* off;
    XRect full;
    const XRect* rects;
    int rectCount;
    int imgW, imgH;
    int i;
    bool any = false;
    if (!window || !image) return false;
    if (!xpwn_ensureInstance()) {
        return false;
    }
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) {
        return false;
    }
    imgW = XImage_width(image);
    imgH = XImage_height(image);
    if (imgW <= 0 || imgH <= 0) {
        return false;
    }

    /* 裁剪脏区：region 为 NULL/空按整幅；offset 为缓冲相对窗口偏移。 */
    if (!offset) {
        XPoint_init(&zero, 0, 0);
        off = &zero;
    } else {
        off = offset;
    }
    if (region && region->rects && region->count > 0) {
        rects = region->rects;
        rectCount = region->count;
    } else {
        full.x = 0; full.y = 0;
        full.width = imgW; full.height = imgH;
        rects = &full;
        rectCount = 1;
    }
    for (i = 0; i < rectCount; ++i) {
        XRect srect;
        XRect drect;
        srect.x = rects[i].x - off->x;
        srect.y = rects[i].y - off->y;
        srect.width = rects[i].width;
        srect.height = rects[i].height;
        if (!xpwn_clipRectToImage(&srect, imgW, imgH, &srect)) continue;
        drect.x = srect.x + off->x;
        drect.y = srect.y + off->y;
        if (xpwn_presentRect(entry, image, &srect, drect.x, drect.y)) any = true;
    }
    return any;
}

/* ==================== 原生连接（平台后端提供） ==================== */

void* XPlatformNativeWindow_nativeConnection(
        XPlatformNativeWindowConnectionType* outType)
{
    if (outType) *outType = XPlatformNativeWindowConnection_None;
    if (!xpwn_ensureInstance()) return NULL;
    if (outType) *outType = XPlatformNativeWindowConnection_Win32;
    return (void*)g_xpwnInstance;
}

#endif /* defined(_WIN32) */
#endif /* XPLATFORMNATIVEWINDOW_ON && XPLATFORMNATIVEWINDOW_WIN32_ON */
