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
 *               但保持隐藏，扩展样式按 xpwn_windowExStyle 默认附加
 *               WS_EX_COMPOSITED（DWM 双缓冲合成根治交互闪烁，
 *               XGPU_WS_COMPOSITED=0 回退），wm map 由 setVisible(true) 的
 *               ShowWindow(SW_SHOW) 完成；CreateWindowExW 的
 *               lpCreateParams 携带 XWindow*，
 *               WM_NCCREATE 存入 GWLP_USERDATA，后续消息经
 *               GetWindowLongPtrW 恢复窗口对象；
 *             - 事件翻译（对标 X11 后端对应消息）：WM_PAINT -> 以
 *               BeginPaint 的 rcPaint 为脏区（÷dpr 出框）注入
 *               handleExposeEvent（重绘）；
 *               WM_SIZE/WM_MOVE -> 先按 GetClientRect+ClientToScreen 得到
 *               客户端屏幕几何（物理口径）并按窗口 dpr ÷出框更新本后端
 *               记录（逻辑口径）再注入 handleGeometryChange
 *               （防 setGeometry 回环，与 X11 ConfigureNotify 处理同构；
 *               handler 头部先经 xpwn_maintainScreenAssignment 收敛屏幕
 *               归属——先维护后换算）；
 *               WM_DPICHANGED -> 每监视器 DPI 变化五步链（归一化回填→
 *               归属维护→建议矩形 SetWindowPos→WM_SIZE 链出框）；
 *               WM_DISPLAYCHANGE -> 屏幕拓扑差分刷新（同步迁移+槽位压实）；
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
 *             - 上屏：present 把软件缓冲的脏区按行重排进与脏区等宽的
 *               自顶向下 DIB，再经 SetDIBitsToDevice（dpr>1 时经
 *               StretchDIBits 放大）提交到窗口 DC；
 *               DIB 负高度表示 top-down。表面格式随
 *               XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16（XGuiConfig 定义，
 *               默认 0）：0=ARGB32（BI_RGB，BGRA 字节序与 XImage 小端
 *               [B,G,R,A] 直配）、1=RGB16 565（BI_BITFIELDS，行内存即
 *               565 编码）；两种模式均为直配字节序，无需像素转换；
 *             - 标题同步：XString_toUtf16 得到 UTF-16 缓冲后
 *               SetWindowTextW，未设置标题时用空串。
 *             - 剪贴板后端：installClipboardBackend 安装 Win32 系统剪贴板
 *               后端（文本走 CF_UNICODETEXT、HTML 走 "HTML Format"、
 *               image/png 走注册格式透传），专用隐藏窗口认领所有权并
 *               监听 WM_CLIPBOARDUPDATE，外部应用认领时经
 *               selectionRevoked 反向通知上层（受 XCLIPBOARD_ON 约束）。
 *             - 光标后端：xpwn_cursorBackendInstall 经
 *               XCursor_installPlatformBackend 注册 Win32 光标后端
 *               （GetCursorPos/SetCursorPos 查询/定位，形状→LoadCursorW
 *               共享句柄映射，WM_SETCURSOR HTCLIENT 分流应用/清除，
 *               受 XCURSOR_ON 约束）。
 *             - DPI（PMv2）：进程感知经 xpwn_dpiAwarenessInit 在首个
 *               HWND/DC 创建点之前声明（PMv2 → shcore PER_MONITOR →
 *               SYSTEM_AWARE 回退链 + R7 失败反查 + XPWN_DPI_AWARENESS
 *               退出开关）；屏幕注册表逐监视器登记 dpr（GetDpiForMonitor）
 *               与归一化 logicalDpi（R4：round(rawDpi/dpr)）与 ÷dpr 逻辑
 *               几何；窗口归属经 xpwn_maintainScreenAssignment 先维护后
 *               换算（首派 heal 走 xpwn_setGeometryForced 去重豁免通道）；
 *               几何出口物理=逻辑×dpr（同一 round 函数，dpr==1 直通）；
 *               present ×dpr 为唯一放大点（scale 内部读
 *               XWindow_devicePixelRatio）。
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
#include "XPlatformScreen.h"

#if XPLATFORMNATIVEWINDOW_ON && XPLATFORMNATIVEWINDOW_WIN32_ON && XWINDOW_ON

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "XWindow.h"
#include "XWindow_Protected.h" /* CSD 抑制位读取（XWindow_isCsdFrameSuppressed，仅供平台层/内部实现）。 */
#include "XWindowSystemInterface.h"
#include "XWindowEvent.h"
#include "XGuiApplication.h"
#include "XCursor.h" /* XCursor 平台光标后端钩子表（本文件光标后端节，受 XCURSOR_ON 约束）。 */
#include "XSystem.h"
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

#ifndef XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
/* XGuiConfig 统一定义，此处兜底供独立编译。 */
#define XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 0
#endif

/* 上屏表面每像素字节数：ARGB32=4、RGB16(565)=2。缓冲内像素编码由绘制
   内核负责写入，本层只把字节按对应 bpp 的 DIB 语义交给 GDI，不做任何
   颜色转换（对标 QWindowsBackingStore 的图像格式跟随策略）。 */
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
#define XPWN_PIXEL_BYTES 2u
/* GDI 对 16bpp DIB 同样按 DWORD 对齐取行；此宏与公共层
   XImageFormat_bytesPerLine(width, XImageFormat_RGB16) 的 4 字节行
   对齐规则一致，保证重排后目标行距与 GDI 读取语义吻合。 */
#define XPWN_DIB_ROW_STRIDE(pixelWidth) \
    ((((size_t)(pixelWidth) * XPWN_PIXEL_BYTES) + 3u) & ~(size_t)3u)
#else
#define XPWN_PIXEL_BYTES 4u
#define XPWN_DIB_ROW_STRIDE(pixelWidth) \
    ((size_t)(pixelWidth) * XPWN_PIXEL_BYTES)
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
    XRect m_client;      /**< 最近一次记录的客户端屏幕几何（逻辑口径：
                              物理 ÷dpr，与框架几何同空间；setGeometry 去重
                              与 WM_SIZE 回写共用同一 round 函数，R8-③）。 */
    bool m_visible;      /**< WM_SHOWWINDOW 最后记录的映射状态。 */
    bool m_mouseInside;  /**< 指针是否位于客户区内（进入/离开追踪，Win32 无原生 enter 消息）。 */
    WNDPROC m_oldProc;   /**< 外部窗口子类化前的过程；内部窗口为 NULL。 */
    HCURSOR m_hcursor;   /**< 框架侧生效光标（LoadCursorW 共享句柄，不销毁）。 */
    bool m_cursorSet;    /**< 框架是否已接管窗口光标（WM_SETCURSOR 分流键）。 */
#if XSCREEN_ON
    XScreen* m_screen;   /**< 后端侧指派屏簿记（非 dpr 缓存——dpr 单源仍是
                              XWindow_devicePixelRatio）；NULL=尚未指派。
                              【R20 生命周期】create/attachForeign 置 NULL、
                              destroy 与 WM_NCDESTROY 清零链增补、屏移除压实
                              后复查——复用槽陈旧非 NULL 使 outboundDpr 误走
                              「已指派」分支读到新框架对象缺省 1.0（对照
                              m_oldProc 槽位投毒前科，见 destroy 清零链注）。 */
#endif
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

/* ==================== 进程 DPI 感知（PMv2 回退链 + R7 反查 + 退出开关） ====================
 * 未声明感知时窗口消息坐标为系统虚拟化口径（96 基准位图拉伸，与被虚拟
 * 化的 GetDeviceCaps 返回值一致）；声明 PerMonitorV2 后全部 WM 消息坐标
 * 变每监视器物理像素，入框/出框换算见本文件 xpwn_nativeRectToLogical 等。
 * 【时序契约（R19）】全部 HWND/DC 创建点——xpwn_ensureInstance 链（原生
 * 窗口/剪贴板窗）、xpwn_screensInit 屏幕枚举 DC、XPlatformGraphics_win32
 * 离屏渲染窗——必须先经 xpwn_dpiAwarenessInit 完成声明：首个窗口创建会
 * 锁死进程感知态，之后声明静默失效。 */

/** @brief WM_DPICHANGED 消息号（winuser.h 需 _WIN32_WINNT >= 0x0603，本地兜底）。 */
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

/** @brief Shcore 感知 API 常量（不引入 shcore.h，动态装载用本地兜底）。 */
#ifndef MDT_EFFECTIVE_DPI
#define MDT_EFFECTIVE_DPI 0
#endif
#ifndef PROCESS_PER_MONITOR_DPI_AWARE
#define PROCESS_PER_MONITOR_DPI_AWARE 2
#endif

/** @brief DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 句柄值 -4（Win10
 *         1703+；winuser.h 声明同样需高版本宏，本地兜底）。 */
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

/** @brief DPI_AWARENESS 枚举的 PER_MONITOR_AWARE 取值（R7 反查判据）。 */
#define XPWN_DPI_AWARENESS_PER_MONITOR 2

/**
 * @brief      退出开关 XPWN_DPI_AWARENESS 读取（静态缓存一次，读取纪律同
 *             XGPU_PRESENT_MICRO，见 xpwn_windowExStyle 注）。
 * @details    未设或 "permonitorv2"=新行为（声明感知 + 全链换算）；
 *             "unaware"=旧行为（不声明感知、dpr 恒 1.0、全链直通）。
 */
static int xpwn_dpiAwarenessRequested(void)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* v = XSystem_environment("XPWN_DPI_AWARENESS");
        requested = (v && strcmp(v, "unaware") == 0) ? 0 : 1;
    }
    return requested;
}

/** @brief 进程感知态缓存：<0 未初始化；0=未感知（dpr=1 旧链）；1=已感知。 */
static int g_xpwnDpiAwareness = -1;

/**
 * @brief      进程 DPI 感知一次性声明（XPWN_DPI_AWARENESS 退出开关）。
 * @details    PMv2 优先（Win10 1703+，GetProcAddress 动态装载）；逐级回落
 *             shcore SetProcessDpiAwareness(PER_MONITOR_AWARE) → user32
 *             SetProcessDPIAware（SYSTEM_AWARE）。
 *             【声明失败 ≠ 未感知（R7）】E_ACCESSDENIED 两种成因：本进程
 *             已设置过（无害），或 EXE 清单/兼容性设置已声明感知——必须
 *             反查真实态收口：GetAwarenessFromDpiAwarenessContext(
 *             GetThreadDpiAwarenessContext())（动态装载，不可用回落 shcore
 *             GetProcessDpiAwareness）；PER_MONITOR_AWARE(_V2) 按启用分支
 *             处理，SYSTEM_AWARE/UNAWARE 才走 dpr=1 旧链。结果进程级缓存。
 */
static void xpwn_dpiAwarenessInit(void)
{
    typedef BOOL(WINAPI* PFN_SetCtx)(HANDLE);
    typedef HANDLE(WINAPI* PFN_ThreadCtx)(void);
    typedef int(WINAPI* PFN_AwarenessFromCtx)(HANDLE);
    typedef HRESULT(WINAPI* PFN_SetAwareness)(int);
    typedef HRESULT(WINAPI* PFN_ProcessAwareness)(HANDLE, int*);
    PFN_SetCtx setContext;
    PFN_ThreadCtx threadContext;
    PFN_AwarenessFromCtx awarenessFromContext;
    PFN_SetAwareness setAwareness;
    PFN_ProcessAwareness processAwareness;
    HMODULE user32;
    HMODULE shcore;
    if (g_xpwnDpiAwareness >= 0) return;
    g_xpwnDpiAwareness = 0; /* 缺省未感知；下文各分支按实际结果收敛。 */
    if (!xpwn_dpiAwarenessRequested()) return; /* unaware 退出开关：旧行为。 */
    user32 = GetModuleHandleW(L"user32.dll");
    shcore = GetModuleHandleW(L"Shcore.dll");
    if (!shcore) shcore = LoadLibraryW(L"Shcore.dll");
    setContext = user32
                     ? (PFN_SetCtx)GetProcAddress(user32, "SetProcessDpiAwarenessContext")
                     : NULL;
    threadContext = user32
                        ? (PFN_ThreadCtx)GetProcAddress(user32, "GetThreadDpiAwarenessContext")
                        : NULL;
    awarenessFromContext = user32
                               ? (PFN_AwarenessFromCtx)GetProcAddress(
                                     user32, "GetAwarenessFromDpiAwarenessContext")
                               : NULL;
    setAwareness = shcore
                       ? (PFN_SetAwareness)GetProcAddress(shcore, "SetProcessDpiAwareness")
                       : NULL;
    processAwareness = shcore
                           ? (PFN_ProcessAwareness)GetProcAddress(shcore, "GetProcessDpiAwareness")
                           : NULL;
    /* 1) PMv2（Win10 1703+）。 */
    if (setContext &&
        setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        g_xpwnDpiAwareness = 1;
        return;
    }
    /* 2) shcore：PER_MONITOR_AWARE（Win8.1+）。声明失败（含 E_ACCESSDENIED
       ——已设置过/清单已声明）落 R7 反查。 */
    if (setAwareness &&
        SUCCEEDED(setAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) {
        g_xpwnDpiAwareness = 1;
        return;
    }
    /* 3) 最低回落 SYSTEM_AWARE（无更高声明路径的旧系统；坐标仍为系统
       虚拟化口径，R7 定版：非 PER_MONITOR 一律按未感知 dpr=1 旧链）。 */
    if (user32) (void)SetProcessDPIAware();
    /* R7 反查真实态收口。 */
    if (threadContext && awarenessFromContext) {
        if (awarenessFromContext(threadContext()) ==
            XPWN_DPI_AWARENESS_PER_MONITOR)
            g_xpwnDpiAwareness = 1;
        return;
    }
    if (processAwareness) {
        int awareness = 0;
        if (SUCCEEDED(processAwareness(NULL, &awareness)) &&
            awareness == (int)PROCESS_PER_MONITOR_DPI_AWARE)
            g_xpwnDpiAwareness = 1;
    }
}

/** @brief 进程是否已按每监视器口径感知（进程级缓存；惰性触发声明）。 */
static bool xpwn_dpiAwarenessEnabled(void)
{
    if (g_xpwnDpiAwareness < 0) xpwn_dpiAwarenessInit();
    return g_xpwnDpiAwareness > 0;
}

#if XCURSOR_ON
/** @brief 光标后端注册前向声明（定义见光标后端节；xpwn_ensureInstance
 *  在连接建好后调用一次）。 */
static void xpwn_cursorBackendInstall(void);
#endif /* XCURSOR_ON */

/** @brief 建立进程级 Win32 连接（幂等；注册窗口类失败即整体不可用）。 */
static bool xpwn_ensureInstance(void)
{
    WNDCLASSEXW wc;
    if (g_xpwnInstance && g_xpwnClassRegistered) return true;
    /* R19 时序契约：本函数链覆盖三个 HWND/DC 创建点之一（原生窗口/
       剪贴板窗），感知声明必须先于窗口类注册完成。 */
    xpwn_dpiAwarenessInit();
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
#if XCURSOR_ON
    /* 光标后端注册：连接（窗口类）建好后 XCursor 的 pos/setPos/窗口
       光标接口即走真实 Win32 通道（GetCursorPos/SetCursorPos/SetCursor，
       对标 posix 连接建好后的 xpwn_cursorBackendInstall）；本函数幂等，
       仅首次注册成功路径到达此处。 */
    xpwn_cursorBackendInstall();
#endif /* XCURSOR_ON */
    return true;
}

/** @brief 取窗口客户区域屏幕几何（【物理像素口径】：PMv2 下虚拟桌面坐标
 *         为每监视器物理像素；÷dpr 出框见 xpwn_nativeRectToLogical 调用
 *         点。XWindow 几何语义：客户区坐标即窗口位置）。 */
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

/* ==================== dpr 换算（物理 = 逻辑 × dpr，全链同一 round） ==================== */

/** @brief 换算取整：几何出框/入框/去重记账/WM_SIZE 回写全链唯一 round
 *         函数（R8-③），dpr==1.0f 直通。 */
static int xpwn_dprRound(float value)
{
    return (int)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

/** @brief 逻辑 → 物理（×dpr；dpr==1.0f 直通）。 */
static int xpwn_dprMul(int value, float dpr)
{
    return dpr == 1.0f ? value : xpwn_dprRound((float)value * dpr);
}

/** @brief 物理 → 逻辑（÷dpr；dpr==1.0f 直通）。 */
static int xpwn_dprDiv(int value, float dpr)
{
    return dpr == 1.0f ? value : xpwn_dprRound((float)value / dpr);
}

/** @brief 每监视器有效 DPI → dpr（两位小数吸附：96→1.0、120→1.25、
 *         144→1.5；非法/亚 1.0 回落 1.0 直通——F3 守卫口径不支持缩小）。
 *         强制 DPI（XGUI_FORCE_DPI>0）：经统一切口替换原生读数
 *         （调试/特定屏放大，XPlatformScreen_applyDpiOverride）。 */
static float xpwn_dprFromDpi(int dpi)
{
    float raw;
    float snapped;
    if (dpi <= 0) return XPlatformScreen_applyDpiOverride(1.0f);
    raw = (float)dpi / 96.0f;
    snapped = (float)((int)(raw * 100.0f + 0.5f)) / 100.0f;
    return XPlatformScreen_applyDpiOverride(snapped >= 1.0f ? snapped : 1.0f);
}

/** @brief 物理矩形 → 逻辑（÷dpr；dpr==1.0f 原样直通）。 */
static XRect xpwn_nativeRectToLogical(const XRect* native, float dpr)
{
    XRect logical;
    logical.x = xpwn_dprDiv(native->x, dpr);
    logical.y = xpwn_dprDiv(native->y, dpr);
    logical.width = xpwn_dprDiv(native->width, dpr);
    logical.height = xpwn_dprDiv(native->height, dpr);
    return logical;
}

/** @brief 逻辑矩形 → 物理（×dpr；dpr==1.0f 原样直通）。 */
static XRect xpwn_logicalRectToNative(const XRect* logical, float dpr)
{
    XRect native;
    native.x = xpwn_dprMul(logical->x, dpr);
    native.y = xpwn_dprMul(logical->y, dpr);
    native.width = xpwn_dprMul(logical->width, dpr);
    native.height = xpwn_dprMul(logical->height, dpr);
    return native;
}

/* ==================== DPI/屏幕归属（定义见屏幕枚举节） ====================
 * 前向声明：窗口过程与几何出口先于定义使用；XSCREEN_ON 等裁剪配置下由
 * 同节空壳实现兜底（全链 dpr=1 直通）。 */
static float xpwn_dprForHwnd(HWND hwnd);
static float xpwn_outboundDpr(XWNPendingEntry* entry,
                              const XRect* requestedLogical);
static void xpwn_maintainScreenAssignment(XWNPendingEntry* entry);
static void xpwn_screenDpiChanged(HWND hwnd, UINT dpiX, UINT dpiY);
static void xpwn_screensRefresh(void);
static bool xpwn_primaryScreenNativeRect(RECT* out);
static float xpwn_primaryScreenDpr(void);
static void xpwn_mousePosToLogical(HWND hwnd, LPARAM lParam, bool isScreen,
                                   XPoint* position, XPoint* globalPosition);

/** @brief 把按窗口样式调整后的窗口矩形转换为客户端恰好等于目标几何。 */
static DWORD xpwn_windowExStyle(const XWindow* window);

/** @brief 取窗口原生样式（创建/动态落地共用单一来源；客户端区恰好等
 *         于目标几何的换算见 xpwn_adjustWindowRect）。 */
static DWORD xpwn_windowStyle(const XWindow* window)
{
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (window && XWindow_type(window) == XWindowType_Popup)
        return WS_POPUP;
    /* CSD 激活（框架自绘标题栏接管，标记经 XWindow_setCsdFrameSuppressed
     * 置位）时按无边框组装：WS_POPUP 去除 WS_CAPTION/WS_THICKFRAME，
     * 抑制原生 WM 装饰、消除双重标题栏（对标 posix 管线创建期把
     * FramelessWindowHint 强制折入装饰组装；Qt 无边框窗同为 WS_POPUP）。
     * 缩放/拖拽由装饰层自有缩放区接管（XWindowDecoration xwd_resize-
     * ZoneAt），任务栏/Alt-Tab 不受 WS_POPUP 影响（对标 Qt 无边框窗仍
     * 受 WM 管理语义）；无边框最大化的工作区钳制见 wndProc WM_GETMINMAXINFO。 */
    if (window && XWindow_isCsdFrameSuppressed(window))
        return WS_POPUP;
    if (window && XWindow_type(window) == XWindowType_Tool)
    {
        /* 对标 Qt::Tool（qdockwidget.cpp:1203 浮动停靠面板窗口旗标）：
           工具窗 = 普通框架去掉最小/最大化盒，原生标题只剩关闭钮。 */
        style &= ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        return style;
    }
    /* 对标 Qt::MSWindowsFixedSizeDialogHint（QWindowsWindow::setWindow
       * 的固定尺寸对话框映射）：固定尺寸对话框 hint 去掉可拖拽改尺寸
       的厚边框与最大化盒，保留标题栏/系统菜单/关闭钮——消息盒默认
       hint 含此位（qmessagebox.cpp:838），拖拽边框不得改变尺寸。 */
    if (window && (XWindow_flags(window) &
                   (XWindowFlags)XWindowType_MSWindowsFixedSizeDialogHint))
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    return style;
}

static void xpwn_adjustWindowRect(const XWindow* window,
                                  const XRect* geometry, RECT* rc)
{
    DWORD style;
    DWORD exStyle;
    if (!geometry || !rc) return;
    rc->left = geometry->x;
    rc->top = geometry->y;
    rc->right = geometry->x + geometry->width;
    rc->bottom = geometry->y + geometry->height;
    style = xpwn_windowStyle(window);
    /* exStyle 必须与建窗一致：WS_EX_TOOLWINDOW 工具窗标题高度小于普通
       标题，传 0 会按普通标题扩边，客户区比请求矮一截。 */
    exStyle = xpwn_windowExStyle(window);
    AdjustWindowRectEx(rc, style, FALSE, exStyle);
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

/* ==================== 光标后端（XCursor 平台钩子，对标 Qt QPlatformCursor） ====================
 * XCursor 公共层经 XCursor_applyToWindow/XCursor_clearForWindow 转发窗口
 * 光标应用/清除（XWidget::setCursor 生效链），XCursor_pos/XCursor_setPos
 * 转发进程级光标查询/定位；本节把四钩子落到 Win32 通道：
 * GetCursorPos/SetCursorPos 与 SetCursor（WM_SETCURSOR HTCLIENT 分流，
 * 见 xpwn_wndProc）。钩子表为静态存储期（XCursor_installPlatformBackend
 * 只保存指针，不拷贝）。 */
#if XCURSOR_ON

/**
 * @brief      XGui 光标形状 → Win32 系统光标映射（LoadCursorW 共享句柄）。
 * @details    逐项对齐 posix 后端 xpwn_cursorShapeToFontGlyph 的字形口径
 *             （同 Qt 形状表）：SizeBDiag→IDC_SIZENESW（"/"，同 posix
 *             XC_top_right_corner）、SizeFDiag→IDC_SIZENWSE（"\"，同 posix
 *             XC_top_left_corner）；SplitV→IDC_SIZEWE、SplitH→IDC_SIZENS
 *             沿用 posix/Qt 的历史选择（分割条按拖动方向取双向尺寸光标）。
 *             OpenHand/ClosedHand/DragCopy/DragMove/DragLink 在 Win32 系统
 *             光标集内无原生对应，以 IDC_ARROW 近似（posix 端同为语义最
 *             近字形的近似口径）。LoadCursorW 返回进程共享句柄，归系统
 *             所有，无需也绝不可 DestroyCursor。Blank 返回 NULL：
 *             SetCursor(NULL) 即客户区隐藏光标。Bitmap/Custom 暂以
 *             IDC_ARROW 兜底（TODO 见 case 注）。
 * @param      shape 内置光标形状。
 * @return     共享 HCURSOR；Blank/加载失败返回 NULL。
 */
static HCURSOR xpwn_cursorHandleForShape(XCursorShape shape)
{
    switch (shape) {
    case XCursor_Arrow:        return LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    case XCursor_UpArrow:      return LoadCursorW(NULL, (LPCWSTR)IDC_UPARROW);
    case XCursor_Cross:        return LoadCursorW(NULL, (LPCWSTR)IDC_CROSS);
    case XCursor_Wait:         return LoadCursorW(NULL, (LPCWSTR)IDC_WAIT);
    case XCursor_IBeam:        return LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM);
    case XCursor_SizeVer:      return LoadCursorW(NULL, (LPCWSTR)IDC_SIZENS);
    case XCursor_SizeHor:      return LoadCursorW(NULL, (LPCWSTR)IDC_SIZEWE);
    case XCursor_SizeBDiag:    return LoadCursorW(NULL, (LPCWSTR)IDC_SIZENESW); /* "/"（同 posix XC_top_right_corner）。 */
    case XCursor_SizeFDiag:    return LoadCursorW(NULL, (LPCWSTR)IDC_SIZENWSE); /* "\"（同 posix XC_top_left_corner）。 */
    case XCursor_SizeAll:      return LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL);
    case XCursor_SplitV:       return LoadCursorW(NULL, (LPCWSTR)IDC_SIZEWE);   /* 水平双箭头（同 posix/Qt 口径）。 */
    case XCursor_SplitH:       return LoadCursorW(NULL, (LPCWSTR)IDC_SIZENS);   /* 垂直双箭头（同 posix/Qt 口径）。 */
    case XCursor_PointingHand: return LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    case XCursor_Forbidden:    return LoadCursorW(NULL, (LPCWSTR)IDC_NO);
    case XCursor_WhatsThis:    return LoadCursorW(NULL, (LPCWSTR)IDC_HELP);
    case XCursor_Busy:         return LoadCursorW(NULL, (LPCWSTR)IDC_APPSTARTING);
    /* 手型/拖拽系列：Win32 系统集无原生对应，取箭头近似（同 posix 的
       语义最近字形近似口径）。 */
    case XCursor_OpenHand:
    case XCursor_ClosedHand:
    case XCursor_DragCopy:
    case XCursor_DragMove:
    case XCursor_DragLink:     return LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    case XCursor_Blank:        return NULL; /* SetCursor(NULL)：客户区隐藏。 */
    /* TODO：Bitmap/Custom 经 CreateIconIndirect 由 XBitmap/XPixmap 构造
       真彩 HCURSOR（对标 posix xpwn_cursorAcquireBitmapCursor/
       xpwn_cursorAcquirePixmapCursor），暂以箭头兜底。 */
    case XCursor_Bitmap:
    case XCursor_Custom:       return LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    default:                   return LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    }
}

static bool xpwn_cursorBackendQueryPos(int* x, int* y)
{
    POINT pt;
    if (!GetCursorPos(&pt)) return false;
    if (x) *x = pt.x;
    if (y) *y = pt.y;
    return true;
}

static bool xpwn_cursorBackendWarpPos(int x, int y)
{
    /* 全局（屏幕）坐标定位；失败（桌面切换/无输入桌面等）返回 false，
       由 XCursor_setPos 保持缓存值兜底（公共层契约）。 */
    return SetCursorPos(x, y) != 0;
}

/** @brief 槽位光标复位（清除语义）：解除框架接管并交还系统默认箭头。 */
static void xpwn_cursorResetEntry(XWNPendingEntry* entry)
{
    entry->m_hcursor = NULL;
    entry->m_cursorSet = false;
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
}

static bool xpwn_cursorBackendApplyWindowCursor(uintptr_t nativeWindowId,
                                                const XCursor* cursor)
{
    XWNPendingEntry* entry;
    XCursorShape shape;
    HCURSOR handle;
    if (nativeWindowId == 0) return false;
    entry = xpwn_findByNativeWindow((HWND)(void*)nativeWindowId);
    if (!entry) return false; /* 非本后端登记窗口（外部/已销毁）：静默失败。 */
    if (!cursor) {
        /* 无光标对象按清除语义：恢复默认箭头（XUndefineCursor 的
           Win32 等价）。 */
        xpwn_cursorResetEntry(entry);
        return true;
    }
    shape = XCursor_shape(cursor);
    handle = xpwn_cursorHandleForShape(shape);
    if (!handle && shape != XCursor_Blank)
        return false; /* LoadCursorW 失败（预定义共享光标几乎不失败）。 */
    entry->m_hcursor = handle;
    entry->m_cursorSet = true;
    /* 立即 SetCursor 生效：WM_SETCURSOR 仅在指针移动时派发，指针已在
       客户区内时这里即时切换；离开/进入后由 xpwn_wndProc 的 WM_SETCURSOR
       分流维持。 */
    SetCursor(handle); /* NULL=Blank 隐藏语义。 */
    return true;
}

static bool xpwn_cursorBackendClearWindowCursor(uintptr_t nativeWindowId)
{
    XWNPendingEntry* entry;
    if (nativeWindowId == 0) return false;
    entry = xpwn_findByNativeWindow((HWND)(void*)nativeWindowId);
    if (!entry) return false;
    xpwn_cursorResetEntry(entry);
    return true;
}

/** @brief XCursor 平台后端钩子表（静态存储期；后端只保存指针）。 */
static const XCursorPlatformBackend g_xpwnCursorBackend = {
    xpwn_cursorBackendQueryPos,
    xpwn_cursorBackendWarpPos,
    xpwn_cursorBackendApplyWindowCursor,
    xpwn_cursorBackendClearWindowCursor
};

/** @brief 连接建立后向 XCursor 注册平台光标后端（幂等）。 */
static void xpwn_cursorBackendInstall(void)
{
    XCursor_installPlatformBackend(&g_xpwnCursorBackend);
}

#endif /* XCURSOR_ON */

/* GPU 直通模式下每帧脏区 BitBlt/SetDIBitsToDevice（dpr>1 经 Stretch 系
   拉伸）直投窗口 DC（本文件
   xpwn_presentRect 与 XPlatformBackingStore_win32 的零拷贝路径）。未开
   DWM 双缓冲合成时，远程显示栈（RDP/OrayIdd）可在一次脏区提交中途取样
   窗口重定向表面，交互/切换页期间表现为整窗黑帧/旧帧闪烁。WS_EX_COMPOSITED
   让 DWM 经双重缓冲重定向表面合成自绘窗口，多次脏区提交对外呈原子整帧，
   是经典 BitBlt 闪烁根治手段。
   【默认关闭（2026-09-26 四波 S 路实锤回归）】本机 OrayIdd 虚拟显示 +
   AMD 22.20 驱动栈上 WS_EX_COMPOSITED 令窗口重定向表面不被合成——客户
   区整体透底（桌面像素外露，GPU/软件双模式同病，屏幕零渲染），换驱动或
   物理显示栈验证前不得默认启用；XGPU_WS_COMPOSITED=1 显式启用（物理机
   验证抗闪收益用）。与 XGPU_PRESENT_MAX_FPS 同族（呈现链路开关，静态
   缓存一次读取）。 */
static DWORD xpwn_windowExStyle(const XWindow* window)
{
    static int cached = -1;
    DWORD exStyle;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_WS_COMPOSITED");
        cached = (v && v[0] == '1' && v[1] == '\0') ? 1 : 0;
    }
    exStyle = cached ? WS_EX_COMPOSITED : 0;
    /* 对标 Qt::Tool（QWidgetWindow 工具窗旗标映射）：工具窗口不进任务
       栏/Alt-Tab，标题条为细条仅含关闭钮（qdockwidget.cpp:1203 浮动
       停靠面板走此形态）。 */
    if (window && XWindow_type(window) == XWindowType_Tool)
        exStyle |= WS_EX_TOOLWINDOW;
    return exStyle;
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
            entry->m_hcursor = NULL;
            entry->m_cursorSet = false;
#if XSCREEN_ON
            entry->m_screen = NULL; /* R20-③：指派簿记随清零链一并复位。 */
#endif
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
                float dpr;
                XRegion_init(&region);
                /* rcPaint 为物理像素口径（PMv2）：÷dpr 出框为逻辑脏区
                   （dpr==1 直通）。 */
                dpr = xpwn_outboundDpr(entry, NULL);
                rect.x = xpwn_dprDiv(r.left, dpr);
                rect.y = xpwn_dprDiv(r.top, dpr);
                rect.width = xpwn_dprDiv(r.right - r.left, dpr);
                rect.height = xpwn_dprDiv(r.bottom - r.top, dpr);
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
        float dpr;
        if (!entry) return 0;
        /* 先维护后换算（R13）：归属/dpr 收敛在几何出框之前——跨屏拖动/
           WM_DPICHANGED 后首个几何事件在此完成指派（heal 经
           xpwn_setGeometryForced 豁免通道，R18）。 */
        xpwn_maintainScreenAssignment(entry);
        if (!xpwn_getClientGeometry(hwnd, &client)) return 0;
        dpr = xpwn_outboundDpr(entry, NULL);
        client = xpwn_nativeRectToLogical(&client, dpr); /* dpr==1 直通。 */
        /* 先更新本后端记录（逻辑口径），再注入几何变化：setGeometry 去重
           比对以此为准，从源头切断「setGeometry -> WM_SIZE/MOVE ->
           handleGeometryChange -> setGeometry」回环（与 X11
           ConfigureNotify 处理同构）。 */
        entry->m_client = client;
        if (entry->m_window) {
            XWindowSystemInterface_handleGeometryChange(
                entry->m_window, &client);
        }
        return 0;
    }
    case WM_SETCURSOR:
        /* 框架光标生效链：XWidget/XWindow_setCursor → 光标后端记录 →
           本处 HTCLIENT 命中应用（DefWindowProc 每次移动重置为类光标，
           必须截获才能稳定生效）；NC 命中（系统条模式原生帧缘）走默认
           过程取系统尺寸光标。 */
        if (LOWORD(lParam) == HTCLIENT && entry && entry->m_cursorSet) {
            SetCursor(entry->m_hcursor); /* NULL=Blank 隐藏语义。 */
            return TRUE;
        }
        break;
    case WM_GETMINMAXINFO:
    {
        /* CSD 无边框窗（WS_POPUP）最大化默认盖住任务栏：Win32 只对带
         * WS_CAPTION 的窗口按工作区最大化。对标 QWindowsWindow::get-
         * SizeHints，把最大化尺寸/位置钳制到最近显示器工作区，其余
         * 字段（最小/最大追踪尺寸等）仍走默认过程。entry 可为空
         * （WM_GETMINMAXINFO 先于 WM_NCCREATE 到达，用户数据尚未登
         * 记），空窗直接交默认过程。
         * 【R8 口径审计】MonitorFromWindow/rcWork/MINMAXINFO 两侧同为
         * 物理像素口径（PMv2）或同为虚拟化口径（未感知），坐标系一致，
         * 无需 ÷dpr。 */
        if (entry && entry->m_window &&
            XWindow_isCsdFrameSuppressed(entry->m_window)) {
            HMONITOR monitor;
            MONITORINFO info;
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            info.cbSize = sizeof(info);
            monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (monitor && GetMonitorInfoW(monitor, &info) && mmi) {
                DefWindowProcW(hwnd, msg, wParam, lParam);
                mmi->ptMaxPosition.x = info.rcWork.left;
                mmi->ptMaxPosition.y = info.rcWork.top;
                mmi->ptMaxSize.x = info.rcWork.right - info.rcWork.left;
                mmi->ptMaxSize.y = info.rcWork.bottom - info.rcWork.top;
                return 0;
            }
        }
        break;
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
        /* HDROP 由 shell 分配，必须 DragFinish 释放且与是否找到目标窗口
           无关：else 路径（entry/m_window 为空）不释放即每次漏 150~500B。 */
        {
            HDROP drop = (HDROP)wParam;
            if (entry && entry->m_window) {
                POINT point;
                POINT global;
                char* uriList;
                float dpr = xpwn_dprForHwnd(hwnd);
                point.x = 0;
                point.y = 0;
                (void)DragQueryPoint(drop, &point);
                global = point;
                ClientToScreen(hwnd, &global);
                uriList = xpwn_dropFilesUriList(drop);
                /* 拖放点物理口径 ÷dpr 出框（与鼠标四路同规则）。 */
                (void)XWindowSystemInterface_handleDropEvent(
                    entry->m_window, XEVENT_TYPE_DROP,
                    (XPoint){ xpwn_dprDiv(point.x, dpr),
                              xpwn_dprDiv(point.y, dpr) },
                    &(XPoint){ xpwn_dprDiv(global.x, dpr),
                               xpwn_dprDiv(global.y, dpr) }, "text/uri-list",
                    uriList ? uriList : "");
                if (uriList) XFree_Hybrid(uriList);
            }
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
    /* ============ 非客户区左键：Tool 窗标题条转译为控件事件 ============ */
    /* 对标 Qt QWidgetWindow::handleNonClientAreaEvent → QDockWidget
       nativeDeco 路径（qdockwidget.cpp:1097-1128）：浮动工具窗（Tool
       窗型，当前仅浮动停靠面板使用）标题条按下启动控件拖拽（拖回宿主
       可落位停靠）、双击切换浮/停。坐标换算：NC 消息 lParam 为屏幕坐
       标，转客户区后标题条位于负 y 带（控件侧以 pos.y<0 识别）。吞掉
       消息不走 DefWindowProc，避免进入系统移动循环（拖拽由控件抓取鼠
       标接管）。仅限 Tool 窗且命中 HTCAPTION：普通窗口/边框/关闭钮等
       保持原生行为。 */
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (entry && entry->m_window &&
            XWindow_type(entry->m_window) == XWindowType_Tool &&
            DefWindowProcW(hwnd, WM_NCHITTEST, 0, lParam) == HTCAPTION) {
            XPoint position;
            /* NC 消息 lParam 为屏幕坐标：R3 收拢点——客户区出口经
               ScreenToClient（物理口径执行）后 ÷dpr（标题条位于负 y 带，
               控件侧以 pos.y<0 识别）。 */
            xpwn_mousePosToLogical(hwnd, lParam, true, &position, NULL);
            XWindowSystemInterface_handleMouseEvent(
                entry->m_window,
                msg == WM_NCLBUTTONDOWN
                    ? XEVENT_TYPE_MOUSE_BUTTON_PRESS
                    : XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK,
                XMouseButton_LeftButton, XMouseButton_LeftButton,
                xpwn_translateModifiers(), position);
            return 0;
        }
        break;

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
        XPoint globalPosition;
        XMouseButton buttons;
        XKeyboardModifiers modifiers;
        if (entry && entry->m_window) {
            type = xpwn_mouseMessageEvent(msg, wParam, &button);
            if (type != XEVENT_TYPE_NONE &&
                button != XMouseButton_NoButton) {
                /* 按键消息 lParam 为客户区坐标：R3 收拢点——客户区取
                   raw、全局经 ClientToScreen（物理口径执行），两出口统一
                   ÷dpr。全局坐标逐事件换算：装饰拖拽/改尺寸增量锚按全局
                   系计算（缺省 (0,0) 会使按下锚失真、CSD 拖动首跳后自指
                   归零——拖动/缩放双双无效根因）。时间戳取 GetMessageTime
                   （消息入队时刻，Qt Windows 后端同源）。 */
                xpwn_mousePosToLogical(hwnd, lParam, false,
                                       &position, &globalPosition);
                /* 按下集合：消息 wParam 的 MK_* 位并上触发键（释放时
                   wParam 不含本键，OR 不改变集合），与 X11 后端一致。 */
                buttons = xpwn_translateButtons(wParam) | button;
                modifiers = xpwn_translateModifiers();
                XWindowSystemInterface_handleMouseEvent_ex(
                    entry->m_window, type, button, buttons, modifiers,
                    position, &globalPosition, (uint32_t)GetMessageTime());
            }
        }
        return 0;
    }

    /* ============ 鼠标移动与进入/离开追踪（对标 Qt Windows 后端） ============ */
    case WM_MOUSEMOVE:
        if (entry && entry->m_window) {
            XPoint position;
            XPoint globalPosition;
            /* 移动消息 lParam 为客户区坐标：R3 收拢点（同按键路径）。 */
            xpwn_mousePosToLogical(hwnd, lParam, false,
                                   &position, &globalPosition);
            if (!entry->m_mouseInside) {
                /* 指针首次进入客户区：先注入进入事件，再开启一次性离开
                   追踪（TME_LEAVE），收到 WM_MOUSELEAVE 后清标记。 */
                TRACKMOUSEEVENT tme;
                XWindowSystemInterface_handleEnterEvent(
                    entry->m_window, position, &globalPosition);
                entry->m_mouseInside = true;
                memset(&tme, 0, sizeof(tme));
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            XWindowSystemInterface_handleMouseEvent_ex(
                entry->m_window, XEVENT_TYPE_MOUSE_MOVE,
                XMouseButton_NoButton, xpwn_translateButtons(wParam),
                xpwn_translateModifiers(), position, &globalPosition,
                (uint32_t)GetMessageTime());
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
            /* 滚轮消息 lParam 为屏幕坐标：R3 收拢点——全局取 raw、客户区
               经 ScreenToClient（物理口径执行），两出口统一 ÷dpr。 */
            xpwn_mousePosToLogical(hwnd, lParam, true,
                                   &position, &globalPosition);
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

    /* ============ 每监视器 DPI 变化（PMv2；R2 建议矩形 / R9 LOWORD） ============ */
    case WM_DPICHANGED:
    {
        RECT* suggested = (RECT*)lParam;
        UINT dpiX;
        UINT dpiY;
        if (!xpwn_dpiAwarenessEnabled()) break; /* 未感知不收到；防御兜底。 */
        dpiX = LOWORD(wParam);
        dpiY = HIWORD(wParam);
        /* 固定次序（R2 五步）：
           ① dpr 取 X 向 LOWORD（R9；Y 向差 >1px 的各向异性缩放取均值）；
           ② 屏差分回填（dpr + 归一化 logicalDpi + 几何，R4 铁律同式）；
           ③ 先维护后换算：跨屏拖动即此切换指派（heal 走
              xpwn_setGeometryForced，R18）；同屏仅 DPI 变化时 setScreen
              no-op，dpr 经 setter 信号→公共层槽→逐窗推送+请求重绘；
           ④ R2：PMv2 下系统只发建议矩形不代劳 resize——以建议矩形
              SetWindowPos（物理口径）；DPI 已变，不会再次触发
              WM_DPICHANGED，无循环；
           ⑤ SetWindowPos 同步派发 WM_SIZE/WM_MOVE → handler（维护已完成）
              ÷新 dpr 出框 → handleGeometryChange，框架逻辑几何单一来源。 */
        xpwn_screenDpiChanged(hwnd, dpiX, dpiY);
        if (entry) xpwn_maintainScreenAssignment(entry);
        if (suggested && entry && !IsRectEmpty(suggested))
            SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        return 0;
    }
    /* ============ 显示拓扑变化（R6 差分 / R14 同步迁移 / R22 压实） ============ */
    case WM_DISPLAYCHANGE:
        /* 基于注册表重跑 EnumDisplayMonitors 差分：新增登记、消失走
           xpwn_screenRemove（同步迁移+槽位压实）；未感知（XPWN_DPI_
           AWARENESS=unaware）保持旧行为。广播消息多窗重复触发，刷新
           幂等无害。 */
        xpwn_screensRefresh();
        break;

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

/* ==================== 屏幕枚举与 DPI（对标 QWindowsScreenManager） ==================== */
#if XSCREEN_ON && XGUIAPPLICATION_ON && XWINDOWSYSTEMINTERFACE_ON

/** @brief 单轮枚举登记上限（桌面场景远达不到；防回调失控）。 */
#define XPWN_MAX_SCREENS 8

static bool g_xpwnScreensInitDone = false;

/** @brief win32 屏幕注册表（平台层所有；对标 posix g_xpwnScreens
 *         posix.c:1203-1211 的数组管理方式，槽位压实见 xpwn_screenRemove）。 */
static XScreen* g_xpwnScreens[XPWN_MAX_SCREENS];          /**< 已登记屏幕（拥有）。 */
static wchar_t g_xpwnScreenDevices[XPWN_MAX_SCREENS][32]; /**< 匹配键：MONITORINFOEXW.szDevice。 */
static RECT g_xpwnScreenNativeRects[XPWN_MAX_SCREENS];    /**< 登记时整屏原生矩形（物理虚拟桌面
                                                               坐标；R21 仿射折算基准，差分
                                                               刷新/DPICHANGED 随动更新）。 */
static int g_xpwnScreenCount;

/** @brief EnumDisplayMonitors 差分枚举状态（首轮登记与刷新共用）。 */
typedef struct XPwnScreenEnumState
{
    bool m_matched[XPWN_MAX_SCREENS]; /**< 槽位屏幕本轮仍存在的差分标记。 */
    int m_registered;                 /**< 本轮新登记的屏幕数。 */
    int m_seen;                       /**< 本轮见到的监视器数（0=枚举整体失败）。 */
} XPwnScreenEnumState;

static void xpwn_screenRemove(XScreen* screen);
static void xpwn_screensReselectPrimary(void);
static bool xpwn_setGeometryForced(XWindow* window, const XRect* logical);

/** @brief 定宽拷贝设备名（含结束符；避开 MSVC strn/wcsn 系安全告警）。 */
static void xpwn_screenCopyDeviceName(wchar_t* dst, const wchar_t* src)
{
    size_t i;
    size_t capacity = sizeof(g_xpwnScreenDevices[0]) /
                      sizeof(g_xpwnScreenDevices[0][0]);
    for (i = 0; i + 1 < capacity && src && src[i]; ++i) dst[i] = src[i];
    dst[i] = L'\0';
}

/** @brief 屏幕注册（登记表尾追加；表满静默丢弃——调用方先行判定）。
 *  @return 登记槽位下标；表满未登记返回 -1（差分刷新据此置 matched）。 */
static int xpwn_screenRegister(XScreen* screen, const wchar_t* deviceName,
                               const RECT* nativeRect)
{
    int index;
    if (g_xpwnScreenCount >= XPWN_MAX_SCREENS) return -1;
    index = g_xpwnScreenCount++;
    g_xpwnScreens[index] = screen;
    xpwn_screenCopyDeviceName(g_xpwnScreenDevices[index], deviceName);
    g_xpwnScreenNativeRects[index] = nativeRect ? *nativeRect : (RECT){0, 0, 0, 0};
    return index;
}

/** @brief 按设备名查既有屏（命中返回借用指针，未命中 NULL）。 */
static XScreen* xpwn_screenForDevice(const wchar_t* deviceName)
{
    int i;
    if (!deviceName || !deviceName[0]) return NULL;
    for (i = 0; i < g_xpwnScreenCount; ++i) {
        if (g_xpwnScreens[i] &&
            wcscmp(g_xpwnScreenDevices[i], deviceName) == 0)
            return g_xpwnScreens[i];
    }
    return NULL;
}

/** @brief 按屏幕对象反查注册表槽位（未登记返回 -1）。 */
static int xpwn_screenIndexOf(const XScreen* screen)
{
    int i;
    if (!screen) return -1;
    for (i = 0; i < g_xpwnScreenCount; ++i) {
        if (g_xpwnScreens[i] == screen) return i;
    }
    return -1;
}

/** @brief 按 HWND 定位所属屏（MonitorFromWindow(MONITOR_DEFAULTTONEAREST)
 *         → MONITORINFOEXW.szDevice → 注册表查找）。 */
static XScreen* xpwn_screenForHwnd(HWND hwnd)
{
    HMONITOR monitor;
    MONITORINFOEXW info;
    if (!hwnd) return NULL;
    monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor) return NULL;
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, (MONITORINFO*)&info)) return NULL;
    return xpwn_screenForDevice(info.szDevice);
}

/** @brief      按请求几何定位所属屏（R13/R21，两段式定版）。
 *  @details    ①先按注册表【逻辑】几何包含判定（请求几何中心为逻辑坐标，
 *              注册表屏幕几何同为逻辑口径——同空间直接判定，混合 DPI 无
 *              歧义；接缝/越界按最近逻辑矩形取）；②未命中才 MonitorFromPoint
 *              兜底——其入参是【物理】虚拟桌面坐标，须先把逻辑点经最近
 *              逻辑矩形 S 仿射折算：物理 = 逻辑 − S.logOrigin + S.natOrigin，
 *              禁止逻辑点直传（物理桌面比逻辑「长」，混合 DPI 直传落错屏）。
 *              未命中且无注册表（枚举失败极端环境）回落主屏。 */
static XScreen* xpwn_screenForPoint(int logicalCenterX, int logicalCenterY,
                                    int logicalW, int logicalH)
{
    XScreen* best = NULL;
    int bestIndex = -1;
    long bestDistance = 0;
    int i;
    (void)logicalW; /* 定版以中心点判定；宽高为签名契约预留。 */
    (void)logicalH;
    /* ① 注册表逻辑几何包含/最近判定（同空间，无歧义）。 */
    for (i = 0; i < g_xpwnScreenCount; ++i) {
        XRect g;
        long dx;
        long dy;
        long distance;
        if (!g_xpwnScreens[i]) continue;
        g = XScreen_geometry(g_xpwnScreens[i]);
        dx = 0;
        dy = 0;
        if (logicalCenterX < g.x)
            dx = (long)g.x - logicalCenterX;
        else if (logicalCenterX > g.x + g.width - 1)
            dx = (long)logicalCenterX - (g.x + g.width - 1);
        if (logicalCenterY < g.y)
            dy = (long)g.y - logicalCenterY;
        else if (logicalCenterY > g.y + g.height - 1)
            dy = (long)logicalCenterY - (g.y + g.height - 1);
        if (dx == 0 && dy == 0) return g_xpwnScreens[i]; /* 中心包含。 */
        distance = dx * dx + dy * dy;
        if (!best || distance < bestDistance) {
            best = g_xpwnScreens[i];
            bestIndex = i;
            bestDistance = distance;
        }
    }
    if (!best) return XScreen_primaryScreen(); /* 无注册表：回落主屏。 */
    /* ② MonitorFromPoint 兜底：逻辑点经最近矩形仿射折算成物理点。 */
    {
        XRect logical = XScreen_geometry(best);
        POINT point;
        HMONITOR monitor;
        MONITORINFOEXW info;
        point.x = logicalCenterX - logical.x +
                  g_xpwnScreenNativeRects[bestIndex].left;
        point.y = logicalCenterY - logical.y +
                  g_xpwnScreenNativeRects[bestIndex].top;
        monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
        if (monitor) {
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, (MONITORINFO*)&info)) {
                XScreen* screen = xpwn_screenForDevice(info.szDevice);
                if (screen) return screen;
            }
        }
    }
    return best; /* 兜底未命中：最近逻辑矩形即归属。 */
}

/** @brief GetDpiForMonitor 动态装载缓存（Shcore.dll，Win8.1+）。 */
static int xpwn_monitorDpi(const wchar_t* deviceName, HMONITOR monitor)
{
    typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
    static PFN_GetDpiForMonitor getDpiForMonitor;
    static bool probed = false;
    UINT dpiX = 96;
    UINT dpiY = 96;
    HDC dc;
    int dpi;
    if (!probed) {
        HMODULE shcore = GetModuleHandleW(L"Shcore.dll");
        if (!shcore) shcore = LoadLibraryW(L"Shcore.dll");
        getDpiForMonitor = shcore
                               ? (PFN_GetDpiForMonitor)GetProcAddress(
                                     shcore, "GetDpiForMonitor")
                               : NULL;
        probed = true;
    }
    /* 每监视器有效 DPI（物理口径，含显示缩放）。 */
    if (getDpiForMonitor && monitor &&
        SUCCEEDED(getDpiForMonitor(monitor, MDT_EFFECTIVE_DPI,
                                   &dpiX, &dpiY)) &&
        dpiX > 0)
        return (int)dpiX;
    /* 回落：显示器 DC 的 LOGPIXELSX（PMv2 下非虚拟化）。 */
    dc = CreateDCW(deviceName, NULL, NULL, NULL);
    if (!dc) return 96;
    dpi = GetDeviceCaps(dc, LOGPIXELSX);
    DeleteDC(dc);
    return dpi > 0 ? dpi : 96;
}

/** @brief      用一块显示器的信息回填 XScreen。
 *  @param      screen 目标屏幕对象。
 *  @param      deviceName 显示器设备名（MONITORINFOEXW::szDevice）。
 *  @param      monitor 该监视器句柄（真实 DPI 查询用；兜底路径可为
 *              MonitorFromPoint 结果）。
 *  @param      monitorRect 整屏几何（rcMonitor，物理虚拟桌面坐标）。
 *  @param      work 可用工作区（rcWork，剔除任务栏后的可用区）。
 *  @details    感知启用分支：dpr=round2(effectiveDpi/96)、几何/可用区
 *              ÷dpr 出框为逻辑口径、logicalDpi 按 R4 归一化铁律上报
 *              round(rawDpi/dpr)（整数缩放档恒 96——缩放由 dpr 单独承载，
 *              防止 dpr×(rawDpi/96) 复合爆炸，H1 同源）；感知未启用分支
 *              与历史逐位一致（虚拟化口径、dpr 缺省 1.0 全直通）。 */
static void xpwn_screenFill(XScreen* screen, const wchar_t* deviceName,
                            HMONITOR monitor, const RECT* monitorRect,
                            const RECT* work)
{
    XRect geometry;
    XRect available;
    XSizeF physical;
    char name[64];
    HDC dc;
    int dpiX = 96;
    int dpiY = 96;
    int depth = 32;
    float dpr = 1.0f;

    geometry.x = monitorRect->left;
    geometry.y = monitorRect->top;
    geometry.width = monitorRect->right - monitorRect->left;
    geometry.height = monitorRect->bottom - monitorRect->top;
    available.x = work->left;
    available.y = work->top;
    available.width = work->right - work->left;
    available.height = work->bottom - work->top;
    if (xpwn_dpiAwarenessEnabled()) {
        /* 真实每监视器口径：物理 DPI/矩形 ÷dpr 出框为逻辑。 */
        int dpi = xpwn_monitorDpi(deviceName, monitor);
        dpr = xpwn_dprFromDpi(dpi);
        dpiX = dpi;
        dpiY = dpi;
        if (dpr > 1.0f) {
            geometry = xpwn_nativeRectToLogical(&geometry, dpr);
            available = xpwn_nativeRectToLogical(&available, dpr);
        }
    }
    /* 设备名落 UTF-8 并剥去 \\.\ 前缀（对标 QWindowsScreen 命名）。 */
    WideCharToMultiByte(CP_UTF8, 0, deviceName, -1,
                        name, (int)sizeof(name) - 1, NULL, NULL);
    name[sizeof(name) - 1] = '\0';
    if (name[0] == '\\' && name[1] == '\\' &&
        name[2] == '.' && name[3] == '\\')
        memmove(name, name + 4, strlen(name + 4) + 1);
    XScreen_setName_2(screen, name);
    XScreen_setGeometry(screen, &geometry);
    XScreen_setAvailableGeometry(screen, &available);
    /* 物理尺寸/位深取自该显示器的设备 DC：HORZSIZE/VERTSIZE/BITSPIXEL 为
       显示器物理属性，与进程感知态无关；LOGPIXELS 仅在未感知分支消费
       （虚拟化口径，与同进程窗口坐标体系一致——历史行为）。 */
    dc = CreateDCW(deviceName, NULL, NULL, NULL);
    if (dc) {
        if (dpr <= 1.0f) {
            dpiX = GetDeviceCaps(dc, LOGPIXELSX);
            dpiY = GetDeviceCaps(dc, LOGPIXELSY);
        }
        depth = GetDeviceCaps(dc, BITSPIXEL);
        if (depth <= 0) depth = 32;
        physical.width = (float)GetDeviceCaps(dc, HORZSIZE);
        physical.height = (float)GetDeviceCaps(dc, VERTSIZE);
        DeleteDC(dc);
        XScreen_setPhysicalSize(screen, &physical);
        if (dpr <= 1.0f)
            XScreen_setLogicalDotsPerInch(screen, (float)dpiX, (float)dpiY);
    }
    if (dpr > 1.0f) {
        /* 归一化铁律（R4）：dpr>1 时 logicalDpi 上报 round(rawDpi/dpr)，
           运行期回填（WM_DPICHANGED）同式。 */
        int logical = xpwn_dprRound((float)dpiX / dpr);
        XScreen_setDevicePixelRatio(screen, dpr);
        XScreen_setLogicalDotsPerInch(screen, (float)logical, (float)logical);
    }
    XScreen_setDepth(screen, depth);
}

/** @brief EnumDisplayMonitors 回调：差分登记/回填（首轮与刷新共用）。 */
static BOOL CALLBACK xpwn_monitorEnumProc(HMONITOR monitor, HDC dc,
                                          LPRECT rect, LPARAM lParam)
{
    XPwnScreenEnumState* state = (XPwnScreenEnumState*)lParam;
    MONITORINFOEXW info;
    XScreen* screen;
    int index;
    (void)dc;
    (void)rect;
    if (!state) return TRUE;
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, (MONITORINFO*)&info)) return TRUE;
    ++state->m_seen;
    screen = xpwn_screenForDevice(info.szDevice);
    if (screen) {
        /* 已登记：差分回填（setter 内部差分，静默无信号），原生矩形簿记
           随动（R21 仿射折算基准）。 */
        index = xpwn_screenIndexOf(screen);
        if (index >= 0) {
            state->m_matched[index] = true;
            g_xpwnScreenNativeRects[index] = info.rcMonitor;
        }
        xpwn_screenFill(screen, info.szDevice, monitor,
                        &info.rcMonitor, &info.rcWork);
        return TRUE;
    }
    if (g_xpwnScreenCount >= XPWN_MAX_SCREENS)
        return TRUE; /* 表满：跳过该监视器（其余继续，防回调失控）。 */
    screen = XScreen_create();
    if (!screen) return FALSE; /* 分配失败：停止枚举。 */
    xpwn_screenFill(screen, info.szDevice, monitor,
                    &info.rcMonitor, &info.rcWork);
    /* 登记统一经 WSI 入口（注册表 + screenAdded 信号，对标
       QGuiApplication::screenAdded）。返回 false=未登记（无应用单例/
       注册失败），所有权仍在平台层：此处回收新建对象，防泄漏。 */
    if (!XWindowSystemInterface_handleScreenAdded(screen)) {
        XScreen_delete_base(screen);
        return TRUE;
    }
    /* 本轮新登记：立即置差分标记，防 xpwn_screensRefresh 的移除循环把
       刚登记的屏当消失监视器误删（对标 Qt QWindowsScreenManager::
       handleScreenChange——仅把本轮未出现的屏入 removed 集）。 */
    index = xpwn_screenRegister(screen, info.szDevice, &info.rcMonitor);
    if (index >= 0)
        state->m_matched[index] = true;
    if (info.dwFlags & MONITORINFOF_PRIMARY)
        XGuiApplication_setPrimaryScreen(screen);
    ++state->m_registered;
    return TRUE;
}

/** @brief      枚举系统监视器并登记屏幕注册表（幂等；对标
 *              QWindowsScreenManager::initializeScreens）。
 *  @details    首个原生窗口创建与事件泵入口逐监视器登记（几何取 rcMonitor
 *              ÷dpr、可用区取 rcWork ÷dpr、dpr/logicalDpi 取
 *              GetDpiForMonitor 真实口径、物理尺寸/位深取显示器 DC），
 *              MONITORINFOF_PRIMARY 者设为主屏；枚举失败回落 GetSystemMetrics
 *              单屏（SPI_GETWORKAREA 供可用区）。多屏热切换经 WM_DISPLAYCHANGE
 *              差分刷新（xpwn_screensRefresh）。 */
static void xpwn_screensInit(void)
{
    XPwnScreenEnumState state;
    memset(&state, 0, sizeof(state));
    if (g_xpwnScreensInitDone) return;
    g_xpwnScreensInitDone = true;
    /* R19 时序契约：本函数创建屏幕 DC（xpwn_screenFill → CreateDCW），
       感知声明必须先于它完成（「无窗先查屏」路径无 ensureInstance 前置）。 */
    xpwn_dpiAwarenessInit();
    EnumDisplayMonitors(NULL, NULL, xpwn_monitorEnumProc, (LPARAM)&state);
    if (state.m_registered > 0) return;
    /* 回落路径：主显示器单屏（EnumDisplayMonitors 不可用的极端环境）。
       GetSystemMetrics 口径审计（R8）：兜底屏 dpr 取 MonitorFromPoint({0,0})
       主屏值，几何仍取 SM_CXSCREEN（未感知等价，感知环境注册表通常已
       建成不走此路径）。 */
    {
        XScreen* screen = XScreen_create();
        if (screen) {
            HMONITOR monitor;
            POINT origin;
            RECT work;
            RECT monitorRect;
            work.left = 0;
            work.top = 0;
            work.right = 0;
            work.bottom = 0;
            monitorRect.left = 0;
            monitorRect.top = 0;
            monitorRect.right = GetSystemMetrics(SM_CXSCREEN);
            monitorRect.bottom = GetSystemMetrics(SM_CYSCREEN);
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            origin.x = 0;
            origin.y = 0;
            monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
            xpwn_screenFill(screen, L"\\\\.\\DISPLAY1", monitor,
                            &monitorRect, &work);
            /* 返回 false=未登记（所有权仍在平台层）：回收防泄漏。 */
            if (XWindowSystemInterface_handleScreenAdded(screen)) {
                xpwn_screenRegister(screen, L"\\\\.\\DISPLAY1", &monitorRect);
                XGuiApplication_setPrimaryScreen(screen);
            } else {
                XScreen_delete_base(screen);
            }
        }
    }
}

/** @brief 显示拓扑/DPI 变化差分刷新（WM_DISPLAYCHANGE；R6/R14/R22）。
 *  @details 重跑 EnumDisplayMonitors：新增监视器登记、已登记差分回填；
 *           消失监视器走 xpwn_screenRemove（同步迁移+槽位压实）。本轮
 *           一个监视器都没见到（枚举整体失败）时不做移除，防瞬时失败
 *           清空注册表。未感知（退出开关）保持旧行为（不跟踪）。 */
static void xpwn_screensRefresh(void)
{
    XPwnScreenEnumState state;
    int i;
    if (!g_xpwnScreensInitDone) return; /* 未登记过：交给惰性 init。 */
    if (!xpwn_dpiAwarenessEnabled()) return; /* unaware 旧行为。 */
    memset(&state, 0, sizeof(state));
    EnumDisplayMonitors(NULL, NULL, xpwn_monitorEnumProc, (LPARAM)&state);
    if (state.m_seen <= 0) return;
    /* 消失监视器：倒序移除+压实，保证 memmove 不影响未处理槽位。 */
    for (i = g_xpwnScreenCount - 1; i >= 0; --i) {
        XScreen* screen = g_xpwnScreens[i];
        if (!screen || state.m_matched[i]) continue;
        xpwn_screenRemove(screen);
    }
    xpwn_screensReselectPrimary();
}

/** @brief      主屏重选：包含原点 (0,0) 的监视器，否则取第一块登记屏幕。
 *  @details    对标 posix xpwn_screensReselectPrimary（与 QXcbConnection::
 *              updateScreen 同语义）；handleScreenRemoved 内部已晋升，此处
 *              兜底几何挪动使 (0,0) 落到别的监视器的场景。 */
static void xpwn_screensReselectPrimary(void)
{
    XScreen* best = NULL;
    int i;
    for (i = 0; i < g_xpwnScreenCount; ++i) {
        XRect geometry;
        if (!g_xpwnScreens[i]) continue;
        geometry = XScreen_geometry(g_xpwnScreens[i]);
        if (geometry.x <= 0 && geometry.y <= 0 &&
            geometry.x + geometry.width > 0 &&
            geometry.y + geometry.height > 0) {
            best = g_xpwnScreens[i];
            break;
        }
    }
    if (!best && g_xpwnScreenCount > 0) best = g_xpwnScreens[0];
    if (best) XGuiApplication_setPrimaryScreen(best);
}

/** @brief      移除一块屏幕（热拔/拓扑变化；R14 同步迁移序 + R22 压实）。
 *  @details    ①注册表注销 + screenRemoved 信号（主屏晋升在内部完成）；
 *              ②同步迁移其上全部窗口（遍历 allWindows 取 XWindow_screen
 *              ==被移除屏者：几何钳位 → setScreen → handleGeometryChange →
 *              xpwn_setGeometryForced 原生重落地——钳位后几何常与原客户区
 *              相同，普通 setGeometry 会被 :2313 去重吞掉，统一走豁免通道；
 *              禁止异步等 WM_MOVE，隐藏窗口可永不收，悬空 m_screen 被消费
 *              方解引用即 UAF）；③最后释放屏幕对象；④注册表槽位压实
 *              （memmove 前移 + count−−，防固定表反复拔插耗尽与陈旧
 *              szDevice 错配）；⑤压实后复查全部 entry->m_screen 无指向
 *              已释放屏（R20-④；迁移经 XWindow_setScreen 直改框架归属，
 *              未经 maintain——清扫仍指向已释放屏的残留簿记，NULL=未指派，
 *              下个事件由 maintain 重解析）。 */
static void xpwn_screenRemove(XScreen* screen)
{
    XScreen* target;
    XVector* windows;
    size_t k;
    int index;
    if (!screen) return;
    index = xpwn_screenIndexOf(screen);
    if (index < 0) return; /* 非本注册表对象：不处理。 */
    /* 1) 注册表注销 + screenRemoved 信号（主屏晋升在内部完成）。 */
    XWindowSystemInterface_handleScreenRemoved(screen);
    /* 2) 窗口同步迁移：屏幕已不在注册表，XWindow_screen 仍指向它的顶层
       窗口全部迁往现主屏；无主屏（全移除）时跳过。 */
    target = XScreen_primaryScreen();
    windows = XGuiApplication_allWindows();
    for (k = 0; windows && k < XVector_size_base((const XContainer*)windows);
         ++k) {
        XWindow* window = XVector_At_Base(windows, (int64_t)k, XWindow*);
        if (!window || XWindow_screen(window) != screen) continue;
        if (target) {
            XRect g = XWindow_geometry(window);
            XRect pg = XScreen_geometry(target);
            /* 几何钳位到主屏（逻辑口径）：宽高先压入，再拉回越界偏移。 */
            if (g.width > pg.width) g.width = pg.width;
            if (g.height > pg.height) g.height = pg.height;
            if (g.x < pg.x) g.x = pg.x;
            if (g.y < pg.y) g.y = pg.y;
            if (g.x + g.width > pg.x + pg.width)
                g.x = pg.x + pg.width - g.width;
            if (g.y + g.height > pg.y + pg.height)
                g.y = pg.y + pg.height - g.height;
            XWindow_setScreen(window, target); /* 发 screenChanged + dpr 直同步。 */
            XWindowSystemInterface_handleGeometryChange(window, &g);
            /* R18：框架侧重落地走去重豁免通道。 */
            xpwn_setGeometryForced(window, &g);
        } else {
            XWindow_setScreen(window, NULL); /* 无屏可迁，回退主屏语义。 */
        }
    }
    if (windows) XVector_delete_base((XClass*)windows);
    /* 3) 平台层持有所有权，负责释放。 */
    XScreen_delete_base((XClass*)screen);
    /* 4) R22 槽位压实：尾段前移 + count−−（对标 posix 数组管理）。 */
    if (index < g_xpwnScreenCount - 1) {
        memmove(&g_xpwnScreens[index], &g_xpwnScreens[index + 1],
                sizeof(XScreen*) * (size_t)(g_xpwnScreenCount - 1 - index));
        memmove(&g_xpwnScreenDevices[index], &g_xpwnScreenDevices[index + 1],
                sizeof(g_xpwnScreenDevices[0]) *
                    (size_t)(g_xpwnScreenCount - 1 - index));
        memmove(&g_xpwnScreenNativeRects[index],
                &g_xpwnScreenNativeRects[index + 1],
                sizeof(RECT) * (size_t)(g_xpwnScreenCount - 1 - index));
    }
    --g_xpwnScreenCount;
    g_xpwnScreens[g_xpwnScreenCount] = NULL;
    memset(&g_xpwnScreenDevices[g_xpwnScreenCount], 0,
           sizeof(g_xpwnScreenDevices[0]));
    memset(&g_xpwnScreenNativeRects[g_xpwnScreenCount], 0, sizeof(RECT));
    /* 5) R20-④ 复查簿记：残留 m_screen 清零（防复用槽投毒）。 */
    for (k = 0; k < XPWN_MAX_WINDOWS; ++k) {
        if (g_xpwnEntries[k].m_screen == screen)
            g_xpwnEntries[k].m_screen = NULL;
    }
}

/** @brief      WM_DPICHANGED 屏差分回填（五步之②；R4 归一化同式）。
 *  @details    dpr=X 向 LOWORD/96（R9；Y 向差 >1px 的各向异性缩放取均值）；
 *              命中注册表屏时同步：dpr + 归一化 logicalDpi + 几何/可用区
 *              （新物理 ÷ 新 dpr）+ 原生矩形簿记。 */
static void xpwn_screenDpiChanged(HWND hwnd, UINT dpiX, UINT dpiY)
{
    XScreen* screen = xpwn_screenForHwnd(hwnd);
    HMONITOR monitor;
    MONITORINFOEXW info;
    float dpr;
    int logical;
    int index;
    if (!screen) return;
    if (dpiY > dpiX + 1 || dpiX > dpiY + 1)
        dpr = xpwn_dprFromDpi((int)((dpiX + dpiY) / 2)); /* R9 均值兜底。 */
    else
        dpr = xpwn_dprFromDpi((int)dpiX);
    /* 归一化铁律（R4）：与 xpwn_screenFill 静态回填同式。 */
    logical = (dpr > 1.0f) ? xpwn_dprRound((float)dpiX / dpr) : (int)dpiX;
    monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, (MONITORINFO*)&info)) {
        XRect native;
        XRect nativeWork;
        native.x = info.rcMonitor.left;
        native.y = info.rcMonitor.top;
        native.width = info.rcMonitor.right - info.rcMonitor.left;
        native.height = info.rcMonitor.bottom - info.rcMonitor.top;
        nativeWork.x = info.rcWork.left;
        nativeWork.y = info.rcWork.top;
        nativeWork.width = info.rcWork.right - info.rcWork.left;
        nativeWork.height = info.rcWork.bottom - info.rcWork.top;
        index = xpwn_screenIndexOf(screen);
        XScreen_setDevicePixelRatio(screen, dpr);
        if (dpr > 1.0f) {
            XRect geometry = xpwn_nativeRectToLogical(&native, dpr);
            XRect available = xpwn_nativeRectToLogical(&nativeWork, dpr);
            XScreen_setGeometry(screen, &geometry);
            XScreen_setAvailableGeometry(screen, &available);
        } else {
            XScreen_setGeometry(screen, &native);
            XScreen_setAvailableGeometry(screen, &nativeWork);
        }
        if (index >= 0) g_xpwnScreenNativeRects[index] = info.rcMonitor;
    } else {
        XScreen_setDevicePixelRatio(screen, dpr);
    }
    XScreen_setLogicalDotsPerInch(screen, (float)logical, (float)logical);
}

/** @brief 出框换算 dpr 源（按 HWND 定位；R13）。
 *  @details 已指派窗口读 XWindow_devicePixelRatio（单源）；未指派期按
 *           窗口所在屏直查 XScreen_devicePixelRatio，禁止读缺省 1.0 快照。 */
static float xpwn_dprForHwnd(HWND hwnd)
{
    XWNPendingEntry* entry = xpwn_findByNativeWindow(hwnd);
    XScreen* screen;
    if (entry && entry->m_screen && entry->m_window)
        return XWindow_devicePixelRatio(entry->m_window);
    screen = xpwn_screenForHwnd(hwnd);
    if (screen) return XScreen_devicePixelRatio(screen);
    return 1.0f;
}

/** @brief      出框换算的 dpr 源（R13 单点定版）。
 *  @details    entry->m_screen 非空（已指派）→ XWindow_devicePixelRatio；
 *              NULL（create/首几何期）→ 按请求几何所在屏直查
 *              XScreen_devicePixelRatio；无请求几何（事件路径）→ 按窗口
 *              所在屏。禁止读缺省 1.0 快照做未指派期换算。 */
static float xpwn_outboundDpr(XWNPendingEntry* entry,
                              const XRect* requestedLogical)
{
    XScreen* screen;
    if (entry && entry->m_screen && entry->m_window)
        return XWindow_devicePixelRatio(entry->m_window);
    if (requestedLogical && requestedLogical->width > 0 &&
        requestedLogical->height > 0) {
        screen = xpwn_screenForPoint(
            requestedLogical->x + requestedLogical->width / 2,
            requestedLogical->y + requestedLogical->height / 2,
            requestedLogical->width, requestedLogical->height);
        return screen ? XScreen_devicePixelRatio(screen) : 1.0f;
    }
    if (entry && entry->m_hwnd) return xpwn_dprForHwnd(entry->m_hwnd);
    return 1.0f;
}

/** @brief      窗口→屏幕归属维护（WM_SIZE/WM_MOVE/WM_DPICHANGED handler
 *              头部、几何换算之前调用——先维护后换算）。
 *  @details    xpwn_screenForHwnd 与 entry->m_screen 比对：首次指派或变化
 *              时 XWindow_setScreen（screenChanged 信号 + dpr 快照直同步）
 *              并同步 entry->m_screen。【首次指派 heal（R13+R18）】指派
 *              变化时若落地口径与现屏 dpr 不一致（mis-pick 双向：×1.0
 *              落入 1.5 屏或反之，检测式=物理客户区 ≠ 簿记逻辑 × 现 dpr），
 *              以框架几何经 xpwn_setGeometryForced 原生重落地——普通
 *              setGeometry 的去重守卫在两方向下都逐字段相等，heal 会被
 *              静默吞掉。最小化/最大化窗口跳过 heal（客户区不代表框架
 *              几何；跨屏 DPICHANGED 的建议矩形链负责）。仅顶层（setScreen
 *              契约）；entry 无 window 借用时跳过。 */
static void xpwn_maintainScreenAssignment(XWNPendingEntry* entry)
{
    XScreen* screen;
    XWindow* window;
    float dpr;
    XRect native;
    bool changed;
    if (!entry || !entry->m_hwnd || !entry->m_window) return;
    window = entry->m_window;
    screen = xpwn_screenForHwnd(entry->m_hwnd);
    if (!screen) return;
    changed = (entry->m_screen != screen);
    if (changed) {
        entry->m_screen = screen;
        /* 首派/变化即 setScreen：发 screenChanged + dpr 快照直同步
           （XWindow_setScreen 尾部，等值短路）。 */
        XWindow_setScreen(window, screen);
    }
    if (!changed) return;
    dpr = XWindow_devicePixelRatio(window);
    /* 不按 dpr==1.0 早退：mis-pick 双向（×1.0 落入 1.5 屏或 ×1.5 落入
       1.0 屏），dpr==1.0 时检测式同样能命中反向口径差（R13+R18）。 */
    if (IsIconic(entry->m_hwnd) || IsZoomed(entry->m_hwnd)) return;
    if (!xpwn_getClientGeometry(entry->m_hwnd, &native)) return;
    if (entry->m_client.width > 0 &&
        native.width == xpwn_dprMul(entry->m_client.width, dpr) &&
        native.height == xpwn_dprMul(entry->m_client.height, dpr))
        return; /* 口径一致：无需 heal。 */
    {
        XRect logical = XWindow_geometry(window);
        /* heal 重落地：嵌套 WM_SIZE/WM_MOVE 链（handler 内维护已完成）
           以新 dpr 出框注入真实落地结果，簿记自然收敛。 */
        xpwn_setGeometryForced(window, &logical);
    }
}

/** @brief      去重豁免的重落地（R18；仅 heal 与屏移除迁移使用）。
 *  @details    跳过 xpwn_setGeometry 的 m_client 逐字段去重（相等即跳过
 *              落地），其余同链：xpwn_logicalRectToNative(×当前 dpr) →
 *              xpwn_adjustWindowRect（chrome 增量物理口径）→ SetWindowPos
 *              → 嵌套 WM_SIZE/WM_MOVE 同步派发记账收敛；消息被裁剪（如
 *              窗口未显示）时 m_client 仍为空或整场未被本次 SetWindowPos
 *              更新（陈旧值），名义写 m_client 收口（R18）。 */
static bool xpwn_setGeometryForced(XWindow* window, const XRect* logical)
{
    XWNPendingEntry* entry;
    XRect native;
    RECT rc;
    XRect before;
    float dpr;
    if (!logical) return false;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    dpr = xpwn_outboundDpr(entry, logical);
    native = xpwn_logicalRectToNative(logical, dpr);
    xpwn_adjustWindowRect(window, &native, &rc);
    before = entry->m_client;
    SetWindowPos(entry->m_hwnd, NULL, rc.left, rc.top,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    /* 名义兜底：SetWindowPos 消息被裁剪时（窗口未显示等）簿记不悬空——
       m_client 仍为空，或嵌套 WM_SIZE/WM_MOVE 根本没跑（与入场快照逐
       字段相等=未更新，含陈旧非零值场景）都名义写；真实落地后本式恰为
       幂等（落地客户区=logical×dpr÷dpr）。 */
    if (entry->m_client.width == 0 ||
        (entry->m_client.x == before.x && entry->m_client.y == before.y &&
         entry->m_client.width == before.width &&
         entry->m_client.height == before.height))
        entry->m_client = *logical;
    return true;
}

/** @brief 主屏 dpr（grabWindow 全屏路径用；无屏回落 1.0 直通）。 */
static float xpwn_primaryScreenDpr(void)
{
    XScreen* screen = XScreen_primaryScreen();
    return screen ? XScreen_devicePixelRatio(screen) : 1.0f;
}

/** @brief 主屏整屏原生矩形（物理虚拟桌面坐标；注册表空返回 false）。 */
static bool xpwn_primaryScreenNativeRect(RECT* out)
{
    XScreen* screen;
    int index;
    if (!out) return false;
    screen = XScreen_primaryScreen();
    if (!screen) return false;
    index = xpwn_screenIndexOf(screen);
    if (index < 0) return false;
    *out = g_xpwnScreenNativeRects[index];
    return true;
}

#else /* !XSCREEN_ON || !XGUIAPPLICATION_ON || !XWINDOWSYSTEMINTERFACE_ON */

static void xpwn_screensInit(void) { }
static void xpwn_screensRefresh(void) { }
static void xpwn_screenDpiChanged(HWND hwnd, UINT dpiX, UINT dpiY)
{ (void)hwnd; (void)dpiX; (void)dpiY; }
static float xpwn_dprForHwnd(HWND hwnd) { (void)hwnd; return 1.0f; }
static float xpwn_outboundDpr(XWNPendingEntry* entry, const XRect* rect)
{ (void)entry; (void)rect; return 1.0f; }
static void xpwn_maintainScreenAssignment(XWNPendingEntry* entry) { (void)entry; }
static bool xpwn_primaryScreenNativeRect(RECT* out) { (void)out; return false; }
static float xpwn_primaryScreenDpr(void) { return 1.0f; }

#endif /* XSCREEN_ON && XGUIAPPLICATION_ON && XWINDOWSYSTEMINTERFACE_ON */

/** @brief      从鼠标消息 lParam 取客户区/全局坐标并统一 ÷dpr 出框
 *              （R3 四路收拢点：滚轮/按键/移动/NC Tool 共用；dpr==1 直通）。
 *  @details    屏幕类消息（滚轮/NC）lParam 为全局物理坐标：全局出口取
 *              raw、客户区出口经 ScreenToClient；客户类消息（按键/移动）
 *              相反，全局经 ClientToScreen——换算保持物理口径执行，仅
 *              出口 ÷dpr。失败时坐标不被改写，按输入口径兜底（与既有
 *              分支同语义）。装饰层拖拽/改尺寸的增量锚按全局系计算
 *              （客户区系随窗口移动自指——posix 同教训）。 */
static void xpwn_mousePosToLogical(HWND hwnd, LPARAM lParam, bool isScreen,
                                   XPoint* position, XPoint* globalPosition)
{
    POINT pt;
    POINT global;
    float dpr = xpwn_dprForHwnd(hwnd);
    pt.x = (int)(short)LOWORD(lParam);
    pt.y = (int)(short)HIWORD(lParam);
    global = pt;
    if (hwnd) {
        if (isScreen)
            ScreenToClient(hwnd, &pt);
        else
            ClientToScreen(hwnd, &global);
    }
    if (position) {
        position->x = xpwn_dprDiv(pt.x, dpr);
        position->y = xpwn_dprDiv(pt.y, dpr);
    }
    if (globalPosition) {
        globalPosition->x = xpwn_dprDiv(global.x, dpr);
        globalPosition->y = xpwn_dprDiv(global.y, dpr);
    }
}

/* ==================== 事件泵（平台后端提供） ==================== */

bool XPlatformNativeWindow_processPendingEvents(void)
{
    MSG msg;
    bool delivered = false;
    BOOL got;
    if (!g_xpwnClassRegistered) return false;
    /* 首次泵时惰性登记屏幕（枚举/主屏选定；此后幂等）。与 X11 后端
       同构：放在泵而非连接期，保证应用单例已发布、信号有接收方。 */
    xpwn_screensInit();
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
    XRect native;
    XRect client;
    RECT rc;
    int w, h;
    XString* title;
    float createDpr;
    if (!window) return false;
    if (!xpwn_ensureInstance()) return false;
    /* 窗口几何落位前保证屏幕注册表就绪：首个窗口之前 Widget 层可能
       查询主屏做首显居中（xpwn_screensInit 幂等）。 */
    xpwn_screensInit();
    entry = xpwn_findByXWindow(window);
    if (entry) return true; /* 幂等：已登记直接成功。 */
    entry = xpwn_findFreeSlot();
    if (!entry) return false;

    geom = XWindow_geometry(window);
    w = geom.width < 1 ? 1 : geom.width;
    h = geom.height < 1 ? 1 : geom.height;
    /* R13：create 期 dpr 按请求几何所在屏直查（未指派期禁读缺省 1.0
       快照）；chrome 增量在 ×dpr 之后调用（物理口径）。 */
    createDpr = xpwn_outboundDpr(NULL, &geom);
    native = xpwn_logicalRectToNative(&geom, createDpr);
    xpwn_adjustWindowRect(window, &native, &rc);
    /* Owner 归属（对标 qwindowswindow.cpp:784-786「Parent: Use transient
       parent for top levels」+ 921-923 传入 CreateWindowEx）：transient
       parent 作为 hWndParent——对话框合并进父窗任务栏项、保持「对话框
       在父之上」Z 序、父窗销毁连带收掉 owned 窗（与 XWindow_destroy
       次序天然对齐）。未设 transient parent 时保持 NULL（独立顶层）。 */
    {
        XWindow* tp = XWindow_transientParent(window);
        XWNPendingEntry* tpe = tp ? xpwn_findByXWindow(tp) : NULL;
        hwnd = CreateWindowExW(xpwn_windowExStyle(window), XPWN_CLASS_NAME,
                               L"", xpwn_windowStyle(window),
                               rc.left, rc.top,
                               rc.right - rc.left, rc.bottom - rc.top,
                               (tpe && tpe->m_hwnd && IsWindow(tpe->m_hwnd))
                                   ? tpe->m_hwnd
                                   : NULL,
                               NULL, g_xpwnInstance, window);
    }
    if (!hwnd) return false;

    entry->m_hwnd = hwnd;
    entry->m_window = window;
    entry->m_visible = false;
    /* 内部窗口无回链（头文件字段契约「内部窗口为 NULL」）：槽位复用时
     * 显式落盘，杜绝继承回池槽位的陈旧 m_oldProc。 */
    entry->m_oldProc = NULL;
    entry->m_hcursor = NULL;   /* 新窗口无框架光标：类光标（箭头）兜底。 */
    entry->m_cursorSet = false;
#if XSCREEN_ON
    /* R20-①：create 期未指派（NULL）；首个 WM_SIZE/MOVE 由
       xpwn_maintainScreenAssignment 收敛指派（mis-pick 经 heal 重落地）。 */
    entry->m_screen = NULL;
#endif
    /* 客户区簿记为逻辑口径：create 物理客户区 ÷ create 期 dpr（与请求
       几何同空间，R8-③/R18）；读取失败按请求几何兜底。 */
    if (xpwn_getClientGeometry(hwnd, &client))
        entry->m_client = xpwn_nativeRectToLogical(&client, createDpr);
    else
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
#if XSCREEN_ON
    /* R20-②：指派簿记随槽位回池一并清零（与 WM_NCDESTROY 分支同口径；
       陈旧非 NULL 使复用槽新窗 outboundDpr 误走「已指派」分支）。 */
    entry->m_screen = NULL;
#endif
    /* 槽位即回池：必须连同 m_oldProc 一并清零（与 WM_NCDESTROY 分支同
     * 口径）。否则旧值残留在可复用槽上，内部建窗路径不写 m_oldProc，
     * 新窗口继承旧链——若旧值为 xpwn_wndProc（对外挂接内部窗口的产物），
     * 未处理消息在 wndProc 尾端经 callPreviousProc 自链，栈溢出（实测
     * 崩溃：dock setFloating 新窗命中被 foreign 测试毒化的槽位）。 */
    entry->m_oldProc = NULL;
    /* 光标接管随窗口脱钩：共享句柄无资源可放，仅复位状态（attachForeign
       走 memset 已覆盖）。 */
    entry->m_hcursor = NULL;
    entry->m_cursorSet = false;
    /* 拖放注销：与 create/attachForeign 的 DragAcceptFiles(hwnd, TRUE)
       成对；外部窗口存活脱钩时不经 DestroyWindow，必须显式 FALSE。 */
    if (hwnd && IsWindow(hwnd))
        DragAcceptFiles(hwnd, FALSE);
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
    XRect client;
    if (!window || !hwnd || !IsWindow(hwnd) || !xpwn_ensureInstance()) return false;
    if (xpwn_findByXWindow(window)) return true;
    entry = xpwn_findFreeSlot();
    if (!entry) return false;
    SetLastError(0);
    oldProc = (WNDPROC)(uintptr_t)SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, (LONG_PTR)xpwn_wndProc);
    if (!oldProc && GetLastError() != 0) return false;
    if (oldProc == xpwn_wndProc) {
        /* 目标窗口本就用本类过程（内部窗口被二次认领/重复挂接）：链回
         * 自己必然在未处理消息的尾端转发里无限递归（callPreviousProc→
         * CallWindowProcW→本过程→…栈溢出），按内部窗口口径不设回链。 */
        oldProc = NULL;
    }
    /* memset 全零覆盖含 R20-① 的 m_screen（attachForeign 语义=全新簿记）。 */
    memset(entry, 0, sizeof(*entry));
    entry->m_hwnd = hwnd;
    entry->m_window = window;
    entry->m_oldProc = oldProc;
    /* 客户区簿记为逻辑口径（物理 ÷ 窗口所在屏 dpr；未指派期直查）。 */
    if (xpwn_getClientGeometry(hwnd, &client))
        entry->m_client = xpwn_nativeRectToLogical(
            &client, xpwn_outboundDpr(entry, NULL));
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
    /* 装饰相关风格位落地：目标样式由窗口类型与 CSD 抑制位共同决定
     * （xpwn_windowStyle，与创建期同一来源），抑制位/类型已由调用方
     * 先于本函数刷新（XWidget_setWindowFlags 先置 CSD 抑制位，XWindow_
     * setFlags 先更新 m_flags 再进入本函数），动态切 FramelessWindow-
     * Hint/自定义条挂摘后原生样式即时生效（对标 posix 管线 setWindow-
     * Flags 折入 _MOTIF_WM_HINTS 重写；QWindowsWindow::setWindowFlags
     * 的 SetWindowLong 通路）。其余提示位（工具窗/置顶/穿透等）落地
     * 仍属 TODO（见下）。 */
    XWNPendingEntry* entry;
    DWORD wantStyle;
    DWORD exStyle;
    RECT rc;
    (void)flags;
    if (!window || !xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    wantStyle = xpwn_windowStyle(window);
    if ((DWORD)GetWindowLongPtrW(entry->m_hwnd, GWL_STYLE) == wantStyle)
        return true;
    exStyle = (DWORD)GetWindowLongPtrW(entry->m_hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(entry->m_hwnd, GWL_STYLE, (LONG_PTR)wantStyle);
    /* 风格位改变外框换算：以现客户端几何（逻辑口径）×dpr 还原物理客户
     * 矩形后按新样式重算外框（chrome 增量物理口径）并 SWP_FRAMECHANGED
     * 落地（如去 WS_CAPTION 后外框收缩而客户区变大），客户端几何保持
     * 不变，杜绝 WM_SIZE 回环造成布局跳动。 */
    {
        XRect native = xpwn_logicalRectToNative(
            &entry->m_client, xpwn_outboundDpr(entry, NULL));
        rc.left = native.x;
        rc.top = native.y;
        rc.right = native.x + native.width;
        rc.bottom = native.y + native.height;
    }
    AdjustWindowRectEx(&rc, wantStyle, FALSE, exStyle);
    SetWindowPos(entry->m_hwnd, NULL, rc.left, rc.top,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                 SWP_FRAMECHANGED);
    /* TODO：对标 QWindowsWindow::setWindowFlags，剩余提示位经
       SetWindowLong 落地 WS_EX_TOOLWINDOW/WS_EX_TOPMOST/WS_EX_TRANSPARENT/
       WS_EX_NOACTIVATE 等；落地后此函数返回值与公共层语义对齐。 */
    return true;
}

bool XPlatformNativeWindow_setGeometry(XWindow* window, const XRect* geometry)
{
    XWNPendingEntry* entry;
    XRect native;
    RECT rc;
    float dpr;
    if (!geometry) return false;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    /* 去重：与最近一次记录/应用的客户端几何（逻辑口径）一致则跳过（防
       WM_SIZE/WM_MOVE 回环）。heal/屏移除迁移类同值重落地必须走
       xpwn_setGeometryForced 豁免通道（R18），禁经本入口。 */
    if (geometry->x == entry->m_client.x &&
        geometry->y == entry->m_client.y &&
        geometry->width == entry->m_client.width &&
        geometry->height == entry->m_client.height)
        return true;
    dpr = xpwn_outboundDpr(entry, geometry);
    native = xpwn_logicalRectToNative(geometry, dpr);
    xpwn_adjustWindowRect(window, &native, &rc);
    SetWindowPos(entry->m_hwnd, NULL, rc.left, rc.top,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    /* SetWindowPos 同步派发 WM_SIZE/WM_MOVE，本后端记录已随之更新；
       若消息被裁剪（例如窗口尚未显示），这里用名义几何兜底。 */
    if (entry->m_client.width == 0)
        entry->m_client = *geometry;
    return true;
}

bool XPlatformNativeWindow_deferGeometry(XWindow* window, bool deferred)
{
    /* Win32 增量改尺寸管线（SetWindowPos 同步派发 WM_SIZE）无「服务器先
       黑、客户端后补」窗口期，本任务不引入挂起批语义，恒 no-op。 */
    (void)window; (void)deferred;
    return false;
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

bool XPlatformNativeWindow_raise(XWindow* window)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    /* 对标 QWindowsWindow::raise（qwindowswindow.cpp
     * setWindowZorder→SetWindowPos(HWND_TOP)）：SWP_NOACTIVATE 保持 Z
     * 序与激活解耦（复扫 R-35 口径），NOMOVE/NOSIZE 只动堆叠。 */
    return SetWindowPos(entry->m_hwnd, HWND_TOP, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
}

bool XPlatformNativeWindow_lower(XWindow* window)
{
    XWNPendingEntry* entry;
    if (!xpwn_ensureInstance()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_hwnd) return false;
    return SetWindowPos(entry->m_hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
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
    RECT nativeRect;
    float dpr;
    int originX;
    int originY;
    int targetWidth;
    int targetHeight;
    int width;
    int height;
    int row;
    int col;
    if (!xpwn_ensureInstance()) return NULL;
    originX = 0;
    originY = 0;
    if (window != 0) {
        hwnd = (HWND)(void*)(uintptr_t)window;
        if (!IsWindow(hwnd)) return NULL;
        if (!GetClientRect(hwnd, &client)) return NULL;
        dpr = xpwn_dprForHwnd(hwnd);
        targetWidth = client.right - client.left;
        targetHeight = client.bottom - client.top;
    } else {
        /* GetSystemMetrics 口径审计（R8）：PMv2 下 SM_CXSCREEN 为系统 DPI
           折算口径而非物理像素——全屏抓取按主屏原生矩形取物理口径，
           注册表不可用（极端环境）回落 GetSystemMetrics 直通。 */
        dpr = xpwn_primaryScreenDpr();
        if (xpwn_primaryScreenNativeRect(&nativeRect)) {
            targetWidth = nativeRect.right - nativeRect.left;
            targetHeight = nativeRect.bottom - nativeRect.top;
            originX = nativeRect.left;
            originY = nativeRect.top;
        } else {
            targetWidth = GetSystemMetrics(SM_CXSCREEN);
            targetHeight = GetSystemMetrics(SM_CYSCREEN);
        }
    }
    /* x/y/w/h 为框架逻辑口径（对标 QScreen::grabWindow）：×dpr 取物理区
       （dpr==1 直通，算术与既往逐位一致）。 */
    x = xpwn_dprMul(x, dpr);
    y = xpwn_dprMul(y, dpr);
    width = w < 0 ? targetWidth - x : xpwn_dprMul(w, dpr);
    height = h < 0 ? targetHeight - y : xpwn_dprMul(h, dpr);
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
    /* 窗口抓取源坐标相对客户区原点；全屏抓取经 GetDC(NULL) 的屏幕 DC，
       源坐标补主屏原生原点偏移（虚拟桌面多屏场景）。 */
    if (!BitBlt(memoryDc, 0, 0, width, height, sourceDc, originX + x,
                originY + y, SRCCOPY | CAPTUREBLT)) {
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

/** @brief 把 XImage 的一块矩形按行重排为等宽 packed DIB 并提交。
 *  @details dstX/dstY 为窗口客户区【逻辑】坐标；scale=dpr（窗口已指派时
 *           单源 XWindow_devicePixelRatio）——dpr==1.0f 参数与既往逐位
 *           一致（SetDIBitsToDevice 1:1）；dpr>1 时目标原点/尺寸 ×scale
 *           放大（present 唯一放大点）——SetDIBitsToDevice 仅做同尺寸
 *           块拷贝，放大必须走 StretchDIBits（COLORONCOLOR）。 */
static bool xpwn_presentRect(XWNPendingEntry* entry, const XImage* image,
                             const XRect* srect, int dstX, int dstY,
                             float scale)
{
    const uint8_t* sbuf;
    int srcBpl;
    int w, h, row;
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    /* RGB16(565)：BI_BITFIELDS 要求在 BITMAPINFOHEADER 之后跟随 3 个
       DWORD 掩码，而 BITMAPINFO 自身只有 1 个颜色表槽，故用扩展布局
       装配（成员名与 BITMAPINFO 保持一致，共用下方装配代码）。 */
    struct
    {
        BITMAPINFOHEADER bmiHeader;
        DWORD bmiMasks[3];
    } bmi;
#else
    BITMAPINFO bmi;
#endif
    HDC hdc;
    uint8_t* buf;
    if (!entry || !entry->m_hwnd || !image || !srect) return false;
    w = srect->width;
    h = srect->height;
    if (w <= 0 || h <= 0) return false;
    sbuf = XImage_constBits(image);
    srcBpl = XImage_bytesPerLine(image);
    if (!sbuf || srcBpl <= 0) return false;
    buf = (uint8_t*)XMalloc_Hybrid(XPWN_DIB_ROW_STRIDE(w) * (size_t)h);
    if (!buf) return false;
    /* 按行拷贝：目标行距按 DIB DWORD 对齐（packed 32bpp 时即 w*4），
       源行宽可任意（含对齐垫）；像素编码与 DIB 一致，仅重排行距。 */
    for (row = 0; row < h; ++row) {
        XMemcpy(buf + (size_t)row * XPWN_DIB_ROW_STRIDE(w),
               sbuf + (size_t)(srect->y + row) * (size_t)srcBpl +
                      (size_t)srect->x * XPWN_PIXEL_BYTES,
               (size_t)w * XPWN_PIXEL_BYTES);
    }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    /* 负高度 = 自顶向下 DIB：与 XImage 每行自顶向下的内存布局一致。 */
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    /* 像素已是 565 布局，GDI 端以 BI_BITFIELDS 声明 565 掩码直传。 */
    bmi.bmiHeader.biBitCount = 16;
    bmi.bmiHeader.biCompression = BI_BITFIELDS;
    bmi.bmiMasks[0] = 0xF800u; /* R：高 5 位。 */
    bmi.bmiMasks[1] = 0x07E0u; /* G：中 6 位。 */
    bmi.bmiMasks[2] = 0x001Fu; /* B：低 5 位。 */
    bmi.bmiHeader.biSizeImage = (DWORD)XPWN_DIB_ROW_STRIDE(w) * (DWORD)h;
#else
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
#endif
    hdc = GetDC(entry->m_hwnd);
    if (!hdc) {
        XFree_Hybrid(buf);
        return false;
    }
    if (scale == 1.0f)
        SetDIBitsToDevice(hdc, xpwn_dprMul(dstX, scale), xpwn_dprMul(dstY, scale),
                          (DWORD)xpwn_dprMul(w, scale),
                          (DWORD)xpwn_dprMul(h, scale),
                          0, 0, 0, (UINT)h, buf, (BITMAPINFO*)&bmi,
                          DIB_RGB_COLORS);
    else
    {
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(hdc, xpwn_dprMul(dstX, scale), xpwn_dprMul(dstY, scale),
                      xpwn_dprMul(w, scale), xpwn_dprMul(h, scale),
                      0, 0, w, h, buf, (BITMAPINFO*)&bmi,
                      DIB_RGB_COLORS, SRCCOPY);
    }
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
    float scale;
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
    /* R1：present 放大 scale 平台内部读取（共享契约签名零改动）；dpr
       单源=XWindow_devicePixelRatio（框架内部恒逻辑像素，缓冲即逻辑
       分辨率，物理放大全在平台 present 层）。 */
    scale = XWindow_devicePixelRatio(window);

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
        if (xpwn_presentRect(entry, image, &srect, drect.x, drect.y,
                             scale))
            any = true;
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
