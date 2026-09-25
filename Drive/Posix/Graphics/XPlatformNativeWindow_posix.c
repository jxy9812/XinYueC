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
 *               XPutImage 提交（XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1
 *               时后备缓冲为 RGB16/565，与 depth-16/565 视觉按 2 字节/
 *               像素直通上屏）；
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
#include "XCursor.h"
#include "XGuiApplication.h"
#include "XClipboard.h"
#include "XPlatformDrag.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XBitmap.h"
#include "XString.h"
#include "XGeometry.h"
#include "XMemory.h"
#include "XPlatformDisplayDriver.h" /* 单屏互斥检查（XPLATFORM_FBDEV_ON=0 时为空头）。 */
#include <stdio.h>

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
#include <X11/Xresource.h>
#include <X11/keysym.h>
/* 光标形状字形常量（XC_*，仅宏/枚举无类型冲突）。 */
#include <X11/cursorfont.h>
#if defined(XINYUE_C_HAS_XRANDR)
/* 屏幕接入：本机仅有 RandR 协议头 randr.h 与运行库 libXrandr.so.2，无
 * libxrandr-dev 的 Xrandr.h，故这里按上游 libXrandr 1.5.2 头逐字声明
 * 本文件用到的最小 ABI 面（结构体布局/函数签名与上游一致，运行时经
 * libXrandr 解析）。若未来系统装上 Xrandr.h，此段与真实头声明等价。 */
#include <X11/extensions/randr.h>
/* 安装的 randr.h 为 1.2 前的协议子集，未定义 RROutput；按上游 Xrandr.h
   补齐（XID 别名）。 */
typedef XID RROutput;
typedef struct {
    Atom name;          /* 监视器名原子（如 "HDMI-1"）。 */
    Bool primary;       /* 是否主监视器。 */
    Bool automatic;     /* 是否自动配置。 */
    int noutput;        /* outputs 数组长度。 */
    int x, y;           /* 虚拟桌面像素位置。 */
    int width, height;  /* 像素尺寸。 */
    int mwidth, mheight;/* 物理尺寸（毫米，EDID；虚拟显示器可为 0）。 */
    RROutput* outputs;  /* 关联输出（不拥有）。 */
} XRRMonitorInfo;
typedef struct {
    int type;            /* 事件基址偏移后的类型值。 */
    unsigned long serial;
    Bool send_event;
    Display* display;
    Window window;       /* 选择该事件的窗口。 */
    Window root;         /* 变化屏幕的根窗口。 */
    Time timestamp;
    Time config_timestamp;
    SizeID size_index;
    SubpixelOrder subpixel_order;
    Rotation rotation;
    int width, height;
    int mwidth, mheight;
} XRRScreenChangeNotifyEvent;
extern Bool XRRQueryExtension(Display* dpy, int* event_base_return,
                              int* error_base_return);
extern void XRRSelectInput(Display* dpy, Window window, int mask);
extern XRRMonitorInfo* XRRGetMonitors(Display* dpy, Window window,
                                      Bool get_active, int* nmonitors);
extern void XRRFreeMonitors(XRRMonitorInfo* monitors);
extern int XRRUpdateConfiguration(X11_XEvent* event);
#endif /* XINYUE_C_HAS_XRANDR */

/* ==================== XI2 触摸接入（方案 A 最小接入，§8.0g23 后续） ====================
 * 本机无 libxi-dev 的 XInput2.h，但有运行库 libXi.so.6 与协议常量头 XI2.h
 *（xorgproto）。以下结构/原型按上游 libXi 1.8.1 的 XInput2.h 逐字声明
 *（仅触摸接入所需子集）；事件类型/掩码常量经 XI2.h 取得。XI2 不可用
 * 时运行时回退核心协议事件（原行为零变化）。 */
#ifdef XINYUE_C_HAS_XI2
#include <X11/extensions/XI2.h>

typedef struct
{
    int    base;
    int    latched;
    int    locked;
    int    effective;
} XGuiXIModifierState;

typedef struct {
    int           mask_len;
    unsigned char *mask;
} XGuiXIButtonState;

typedef struct {
    int           mask_len;
    unsigned char *mask;
    double        *values;
} XGuiXIValuatorState;

typedef struct
{
    int                 deviceid;
    int                 mask_len;
    unsigned char*      mask;
} XGuiXIEventMask;

typedef struct {
    int           type;         /* GenericEvent */
    unsigned long serial;
    Bool          send_event;
    Display       *display;
    int           extension;
    int           evtype;
    XID           cookie;
    Time          time;
    int           deviceid;
    int           sourceid;
    int           detail;
    Window        root;
    Window        event;
    Window        child;
    double        root_x;
    double        root_y;
    double        event_x;
    double        event_y;
    int           flags;
    XGuiXIButtonState   buttons;
    XGuiXIValuatorState valuators;
    XGuiXIModifierState mods;
    XGuiXIModifierState group;
} XGuiXIDeviceEvent;

extern Status XIQueryVersion(Display* dpy, int* major_inout,
                             int* minor_inout);
extern int XISelectEvents(Display* dpy, Window win,
                          XGuiXIEventMask* masks, int num_masks);
#endif /* XINYUE_C_HAS_XI2 */
#include <dbus/dbus.h>
#undef XImage
#undef XPoint
#undef XEvent
#undef XColor
#undef XKeyEvent
#undef XExposeEvent
#define XFree XMemory_free /* 恢复公共内存释放宏别名。 */
#include <poll.h>
#include <unistd.h>
#include <locale.h>
#include <string.h>
#include <time.h>
#include <errno.h>
/* X11 连接 fd 监视线程（主循环统一等待桥，见 xpwn_x11WakeWatch 注释）：
 * 平台层用 POSIX 原生线程原语即可，无需引入公共层 XThread 体系。 */
#include <pthread.h>

#if XAbstractNetIoRing_ON
#include "XAbstractNetIoRing.h" /* 主循环双源等待：ring 事件 fd 与 processReady。 */
#endif

/** @brief 进程内原生窗口注册表容量（静态表，单线程使用）。 */
#define XPWN_MAX_WINDOWS 64

/** @brief 首选 32 位 TrueColor 视觉深度；失败回退 24。 */
#define XPWN_DEPTH_32 32
#define XPWN_DEPTH_24 24
/** @brief 16 位 TrueColor 视觉深度（仅 RGB16 直拷分支使用，见下）。 */
#define XPWN_DEPTH_16 16

/* 后备缓冲像素格式选择器：0 = 现状 ARGB32（4 字节/像素），1 = RGB16
 * （565，2 字节/像素，与 Qt QRgb16 同构）。统一定义在
 * Src/XGui/XGuiConfig.h（并行批次落地）；此处 #ifndef 兜底为 0，保证
 * 配置项尚未落地时本文件按现状行为编译，两种取值下行为均经编译期
 * 门控（#if），互不掺入。 */
#ifndef XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
#define XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 0
#endif

/* [ime-dbg] XIM 输入法协商诊断输出编译开关（默认关）：逐条 locale/
 * style/XCreateIC 日志属调试残留，生产构建对标 Qt 平台插件静默
 * （Qt 的 XIM 诊断走 QLoggingCategory，默认级别不输出）。排查输入法
 * 协商问题时以 -DXPWN_IME_DEBUG=1 重编本文件即可恢复全部输出。 */
#ifndef XPWN_IME_DEBUG
#define XPWN_IME_DEBUG 0
#endif

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
    Visual* m_visual;    /**< 本窗口创建所用视觉（普通窗口 = g_xpwnVisual；
                              瞬态弹层 = 屏幕默认视觉，见 create；present
                              按本值创建匹配深度的 XImage；NULL 回落
                              g_xpwnVisual）。 */
    int m_depth;         /**< 本窗口创建所用深度（0 表示未登记，回落
                              g_xpwnDepth）。 */
    X11_XImage* m_presentImage; /**< 复用的 XPutImage 描述符（拥有）。 */
    uint8_t* m_presentBuffer;   /**< 复用的上屏转换缓冲（拥有）。 */
    int m_presentWidth;         /**< 描述符对应图像宽度。 */
    int m_presentHeight;        /**< 描述符对应图像高度。 */
    int m_presentBytesPerLine;  /**< 描述符对应的缓冲行跨度。 */
    bool m_presentDirect;       /**< 是否可以按后备缓冲原生布局直拷（ARGB32
                                     模式为 BGRA8888；RGB16 模式为 565）。 */
    XRect m_client;      /**< 本后端最近一次记录的客户端几何（去重用）。 */
    bool m_keyPressed[256];    /**< 各键码当前按下状态（X11 键码 8..255），用于识别自动重复。 */
    unsigned long m_lastPressTime;    /**< 最近一次非滚轮按键的时间戳（X11 毫秒节拍）。 */
    XMouseButton m_lastPressButton;   /**< 最近一次非滚轮按键的按键。 */
    XPoint m_lastPressPos;            /**< 最近一次非滚轮按键的位置。 */
    XIC m_inputContext;               /**< XIM 输入上下文（拥有；可为 NULL）。 */
    XFontSet m_fontSet;               /**< PreeditPosition 风格 IC 的预编辑字体集
                                           （拥有；§8.0g7：XCreateIC 不接管所有权，
                                           须在 IC 销毁后 XFreeFontSet，此前逐窗口
                                           泄漏 ~3.7KB）。 */
    DBusConnection* m_imeBus;         /**< fcitx5 DBus 输入法会话连接（进程共享）。 */
    char* m_imeIcPath;                /**< fcitx5 DBus IC 对象路径（拥有；NULL=无）。 */
    char* m_imeKeybuf;                /**< CreateInputContext 返回的密钥（拥有）。 */
    XPoint m_spot;                    /**< 预编辑光标位置（客户端坐标，聚焦时同步）。 */
    char m_preedit[1024];             /**< 当前 UTF-8 组合文本。 */
    Window m_dragSource;               /**< 当前 XDND 源窗口；0 表示无会话。 */
    Atom m_dragTarget;                 /**< 已协商的 XDND 数据类型。 */
    XPoint m_dragPosition;             /**< 最近一次拖放位置（窗口坐标）。 */
    bool m_dropPending;                /**< 已请求 Selection，等待 SelectionNotify。 */
    bool m_deferredActivation;         /**< 窗口未映射期间的激活请求挂起
                                            （对齐 QXcbWindow::m_deferredActivation，
                                            MapNotify 后补激活）。 */
} XWNPendingEntry;

/** @brief 每进程 X11 连接状态。 */
static Display* g_xpwnDisplay;    /**< X11 连接；NULL 表示未连接/连接失败。 */
static int g_xpwnScreenNumber;    /**< 默认屏幕号。 */
static Visual* g_xpwnVisual;      /**< 选定 TrueColor 视觉。 */
static int g_xpwnDepth;           /**< 选定视觉深度（32 或 24）。 */
static Colormap g_xpwnColormap;   /**< 进程共享色彩映射表（拥有）。 */
/* 屏幕默认视觉/深度/色彩映射表（对标 Qt 屏幕的 root_visual/root_depth/
 * screen default colormap；仅引用不拥有，连接建立后恒定）。override-
 * redirect 瞬态弹层（Popup/ToolTip/SplashScreen）按默认视觉创建用——
 * 见 XPlatformNativeWindow_create 内注释（弹层上屏根修）。 */
static Visual* g_xpwnDefaultVisual;   /**< 屏幕默认视觉（引用）。 */
static int g_xpwnDefaultDepth;        /**< 屏幕默认深度。 */
static Colormap g_xpwnDefaultColormap; /**< 屏幕默认色彩映射表（引用）。 */
static Atom g_xpwnWmDelete;       /**< WM_DELETE_WINDOW 协议原子。 */
static Atom g_xpwnWmProtocols;   /**< WM_PROTOCOLS 协议原子（ClientMessage 载体）。 */
static Atom g_xpwnNetWmName;      /**< _NET_WM_NAME 原子（可能 None）。 */
static Atom g_xpwnUtf8String;     /**< UTF8_STRING 原子（可能 None）。 */
/* EWMH 窗口状态/标志原子（对标 QXcbWindow::setWindowFlags 维护的
 * _NET_WM_STATE/_NET_WM_HINTS 协议，见 setWindowFlags 实现注释）。 */
static Atom g_xpwnNetWmState;          /**< _NET_WM_STATE 原子。 */
static Atom g_xpwnNetWmStateAbove;     /**< _NET_WM_STATE_ABOVE 原子。 */
static Atom g_xpwnNetWmStateBelow;     /**< _NET_WM_STATE_BELOW 原子。 */
static Atom g_xpwnNetWmStateSkipTaskbar; /**< _NET_WM_STATE_SKIP_TASKBAR 原子。 */
static Atom g_xpwnNetWmStateSkipPager;   /**< _NET_WM_STATE_SKIP_PAGER 原子。 */
/* 窗口状态原子（对标 QXcbWindow::setWindowState 维护的三个 EWMH 状态位：
 * 最大化在 X11 拆为 VERT/HORZ 两原子同时请求，全屏/最大化由 WM 回写
 * _NET_WM_STATE，客户端经 PropertyNotify 观察闭环）。 */
static Atom g_xpwnNetWmStateMaximizedVert; /**< _NET_WM_STATE_MAXIMIZED_VERT 原子。 */
static Atom g_xpwnNetWmStateMaximizedHorz; /**< _NET_WM_STATE_MAXIMIZED_HORZ 原子。 */
static Atom g_xpwnNetWmStateFullscreen;  /**< _NET_WM_STATE_FULLSCREEN 原子。 */
static Atom g_xpwnWmChangeState;         /**< WM_CHANGE_STATE 原子（ICCCM 4.1.4
                                              最小化状态迁移请求载体）。 */
static Atom g_xpwnWmState;               /**< WM_STATE 原子（WM 托管状态属性，
                                              IconicState 检测用）。 */
/* _NET_WM_WINDOW_TYPE 原子表（对标 QXcbWindow::setWindowType 的 atom()
 * 集；COMBO 在公共层 XWindowType 枚举暂无对应值，先登记保持原子表与
 * Qt xcb 对齐）。 */
static Atom g_xpwnNetWmWindowType;       /**< _NET_WM_WINDOW_TYPE 属性原子。 */
static Atom g_xpwnNetWmTypeNormal;       /**< _NET_WM_WINDOW_TYPE_NORMAL。 */
static Atom g_xpwnNetWmTypeDialog;       /**< _NET_WM_WINDOW_TYPE_DIALOG。 */
static Atom g_xpwnNetWmTypeUtility;      /**< _NET_WM_WINDOW_TYPE_UTILITY。 */
static Atom g_xpwnNetWmTypeSplash;       /**< _NET_WM_WINDOW_TYPE_SPLASH。 */
static Atom g_xpwnNetWmTypeTooltip;      /**< _NET_WM_WINDOW_TYPE_TOOLTIP。 */
static Atom g_xpwnNetWmTypeCombo;        /**< _NET_WM_WINDOW_TYPE_COMBO。 */
static Atom g_xpwnNetWmTypePopupMenu;    /**< _NET_WM_WINDOW_TYPE_POPUP_MENU。 */
static Atom g_xpwnMotifWmHints;        /**< _MOTIF_WM_HINTS 原子（装饰提示）。 */
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
static Atom g_xpwnClipboard;      /* CLIPBOARD 原子。 */
static Atom g_xpwnTargets;        /* TARGETS 原子（Selection 目标询问）。 */
static Atom g_xpwnTimestamp;      /* TIMESTAMP 原子（所有权时间询问）。 */
static Atom g_xpwnClipProp;       /* 剪贴板数据传输用属性原子。 */
static Atom g_xpwnTextHtml;       /* text/html 目标原子（mime html 协商）。 */
static Atom g_xpwnIncr;           /* INCR 原子（ICCM 2.5 增量传输协议头类型）。 */
static Atom g_xpwnSaveTargets;    /* SAVE_TARGETS 原子（剪贴板管理器保存询问）。 */
static Atom g_xpwnMultiple;       /* MULTIPLE 原子（ICCCM 2.6.2 批量转换询问）。 */

/* ==================== 主循环统一等待：X11 fd → ring 唤醒桥 ====================
 * 根因（对标缺陷：XEventLoop::exec → XAbstractEventDispatcher::processEvents
 * 的主线程阻塞分支只阻塞在 XAbstractNetIoRing 的等待原语上——Linux 端
 * XNetIoRingPosix_waitForEvents 仅 poll{ring fd/epoll fd, 唤醒 eventfd}，
 * X11 连接 fd 不在其中；双源 poll{X11, ring}（XPlatformNativeWindow_
 * waitForEvents）只有 XGuiApplication_waitForEvents 自绘主循环可达）。
 * 后果：exec 标准主循环在有长定时器挂起时，X11 按键要等定时器到期才被
 * 泵出；无定时器时受 20ms 心跳量化。
 * 方案（对标 Qt：QEventDispatcherGlib 把 X11 连接 fd 并入 GMainContext
 * 统一等待）：这里选"把 X11 fd 并入 ring 统一等待"目标，但经 ring 既有
 * 的跨线程唤醒通道在平台层内实现——XAbstractNetIoRing_registerEvent 在
 * Posix 后端是空桩（XNetIoRingPosix.c：epoll 兴趣仅随网络 SQE 提交登记，
 * 无被动 fd 分发通道），字面注册不会让 fd 进入等待集；而 dispatcher 侧
 * 增加"额外等待 fd"通道超出本文件所有权。故由一个常驻监视线程 poll(X11
 * 连接 fd)，可读即 XAbstractNetIoRing_wakeUp（ring 的跨线程 eventfd，
 * Posix 后端 wakeUp/wakeFd 本为多线程设计）——dispatcher 的阻塞等待立即
 * 返回，下一轮 processEvents 的轮询回调把 X11 事件泵空。监视线程只做
 * poll 与 eventfd 写、绝不在子线程触碰 Display*（Xlib 单线程约定不破）；
 * 自身阻塞在 poll(-1) 上零 CPU，无忙等。全局 ring 生命周期为进程级
 * （dispatcher 析构仅清指针不销毁），监视线程持有后不会悬空。 */
static int          g_xpwnWatchXfd = -1;    /**< 受监视的 X11 连接 fd。 */
static pthread_t    g_xpwnWatchThread;      /**< 监视线程句柄（分离态）。 */
static bool         g_xpwnWatchStarted = false; /**< 监视线程已启动（主线程写）。 */
static Window g_xpwnClipWin = None;      /* 专用剪贴板窗口（对标 QXcbClipboard::m_window，CLIPBOARD/PRIMARY 共用）。 */
static Window g_xpwnClipReqWin = None;   /* 专用请求者窗口（对标 QXcbClipboard::m_requestor，读方向传输属性挂此窗口并监听 PropertyNotify）。 */

/** @brief 单个 mime 格式的镜像条目（对标 QXcbClipboard::m_owner[mode]
 *  中 QMimeData 的一个 format：mime 名 ↔ X11 TARGETS 原子双向映射，
 *  字节流原样保存，serve 时按 format=8 回给请求方）。 */
#define XPWN_CLIP_MAX_FORMATS 8
typedef struct XpwClipFormatEntry
{
    char           m_mime[64];    /**< mime 格式名（"text/plain"/"text/html"/"image/png"…）。 */
    Atom           m_target;      /**< 对应 X11 目标原子（text/plain→UTF8_STRING）。 */
    unsigned char* m_data;        /**< 格式字节（拥有；PNG 等二进制直存，不再编码）。 */
    int            m_len;         /**< 字节长度。 */
} XpwClipFormatEntry;

/** @brief 单个 X11 选择区的镜像与所有权状态（对标 QXcbClipboard 的
 *  OwnerData：CLIPBOARD 与 PRIMARY 各自独立维护，互不串扰）。 */
typedef struct XpwClipOwnerState
{
    bool   m_dataValid;  /**< 镜像是否有有效数据。 */
    char*  m_text;       /**< 认领期间保存的选择区文本（拥有；既有 text 通道镜像，与 text/plain 格式条目互通）。 */
    int    m_textLen;    /**< 文本字节长度。 */
    Window m_serveWin;   /**< 认领所有权的窗口（专用剪贴板窗口）。 */
    Time   m_timestamp;  /**< 认领所有权的服务器时间戳。 */
    XpwClipFormatEntry m_formats[XPWN_CLIP_MAX_FORMATS]; /**< 多格式镜像（对标 QMimeData 多格式并存）。 */
    int    m_formatCount; /**< 当前镜像条目数。 */
    unsigned char* m_recv; /**< 外部读取接收缓冲（拥有；mimeData 回调借用语义的数据落点）。 */
    int    m_recvLen;     /**< 接收缓冲字节长度。 */
} XpwClipOwnerState;

/* [0]=CLIPBOARD、[1]=PRIMARY；原子与槽位的映射见
 * xpw_clipStateForSelection（对标 QXcbClipboard::m_owner[mode]）。 */
static XpwClipOwnerState g_xpwnClipStates[2];

/* ==================== X11 选择区镜像/模式映射工具 ==================== */

/* 释放单个选择区的镜像（对标 QXcbClipboard ownerData 复位）。 */
static void xpw_clipClearMirror(XpwClipOwnerState* st)
{
    int i;
    if (!st) return;
    if (st->m_text) { XFree_System(st->m_text); st->m_text = NULL; }
    st->m_textLen = 0;
    for (i = 0; i < st->m_formatCount; ++i) {
        if (st->m_formats[i].m_data) {
            XFree_System(st->m_formats[i].m_data);
            st->m_formats[i].m_data = NULL;
        }
        st->m_formats[i].m_len = 0;
        st->m_formats[i].m_target = None;
        st->m_formats[i].m_mime[0] = '\0';
    }
    st->m_formatCount = 0;
    if (st->m_recv) { XFree_System(st->m_recv); st->m_recv = NULL; }
    st->m_recvLen = 0;
    st->m_dataValid = false;
    st->m_serveWin = None;
}

/* 按 X11 选择区原子取状态槽；未知选择区返回 NULL。 */
static XpwClipOwnerState* xpw_clipStateForSelection(Atom selection)
{
    if (selection == g_xpwnClipboard) return &g_xpwnClipStates[0];
    if (selection == XA_PRIMARY) return &g_xpwnClipStates[1];
    return NULL;
}

/* 按后端模式取状态槽：Clipboard→CLIPBOARD、Selection→PRIMARY，
 * 其余模式（FindBuffer 等）不支持系统选择区，统一视为 Clipboard 槽
 * 由调用方先行拒绝。 */
#if XCLIPBOARD_ON
static XpwClipOwnerState* xpw_clipStateForMode(int mode)
{
    if (mode == (int)XClipboardMode_Selection) return &g_xpwnClipStates[1];
    return &g_xpwnClipStates[0];
}

/* 按后端模式取选择区原子（对标 QXcbClipboard::atomForMode）；
 * 不支持的模式返回 None（FindBuffer 仅进程内存储）。 */
static Atom xpw_clipAtomForMode(int mode)
{
    if (mode == (int)XClipboardMode_Selection) return XA_PRIMARY;
    if (mode == (int)XClipboardMode_Clipboard) return g_xpwnClipboard;
    return None;
}
#endif /* XCLIPBOARD_ON */

/* 从镜像条目构建可服务目标原子数组（TARGETS/SAVE_TARGETS 共用）：
 * 依次报告各格式条目的目标原子，text/plain 条目额外补 XA_STRING
 * （对标 Qt 同时提供 UTF8_STRING 与 STRING 两个文本目标）；与既有
 * TARGETS 分支逐字节同序，仅抽出共享并补容量护栏。返回写入个数。 */
static int xpw_clipBuildTargetAtoms(const XpwClipOwnerState* st,
                                    Atom* targets, int max)
{
    int n = 0;
    int fi;
    if (!st || !targets || max <= 0) return 0;
    for (fi = 0; fi < st->m_formatCount && n < max; ++fi) {
        if (st->m_formats[fi].m_target == None || !st->m_formats[fi].m_data)
            continue;
        targets[n++] = st->m_formats[fi].m_target;
        if (n >= max) break;
        if (strncmp(st->m_formats[fi].m_mime, "text/plain",
                    sizeof(st->m_formats[fi].m_mime)) == 0)
            targets[n++] = XA_STRING;
    }
    return n;
}

/* 前向：SelectionClear 反向通知（实现在文件尾 X11 剪贴板后端小节，
 * 经后端契约的 selectionRevoked 可选回调分发）。 */
static void xpw_clipNotifyRevoked(int mode);

/* ==================== X11 INCR 大数据增量传输（ICCCM 2.5，对标
 * QXcbClipboardTransaction） ====================
 * 数据超过服务器单次请求/单属性承载上限时，选择区所有权方向（serve）以
 * type=INCR 分片传输：协议头（format=32 总长度）→ 先回 SelectionNotify →
 * 每当 requestor 删除属性（PropertyNotify state=PropertyDelete）写下一片
 * （PropModeReplace）→ 最后一片被取走后写零长度属性终结。请求方向
 * （读）见 xpw_clipCollectIncr。小数据路径维持单次 XChangeProperty 不变。 */

/** @brief INCR 会话表容量（有界；表满时新的大数据请求按协议以
 *  property=None 礼貌拒绝，对标 Qt 按窗口键的 QMap 加超时自毁）。 */
#define XPWN_CLIP_INCR_MAX_SESSIONS 8
/** @brief INCR 读方向整体超时（毫秒；对端死亡时不至于挂死）。
 *  参数化：默认 XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS，经后端契约
 *  setIncrTimeoutMs 由 XClipboard_setIncrTimeoutMs 调整（§8.2 协议补边：
 *  慢生产者/大载荷场景可调大）。仅约束读方向；serve 方向由闲置回收治理。 */
static int g_xpwnClipIncrTimeoutMs = XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS;
/** @brief 会话闲置超时（毫秒）：对端中途退出后删除通知永不再来，借
 *  事件泵扫描回收快照（Qt abort timer 的无定时器等价实现）。 */
#define XPWN_CLIP_INCR_SESSION_IDLE_MS 30000
/** @brief 262144：常见 X 服务器单属性数据上限口径（阈值双保险上限）。 */
#define XPWN_CLIP_INCR_CHUNK_CAP 262144
/** @brief 增量收集总量防御上限（256MB；异常所有者/伪造总长保护）。 */
#define XPWN_CLIP_INCR_MAX_TOTAL (256u * 1024u * 1024u)

/** @brief 单个进行中的 INCR 服务会话（对标 QXcbClipboardTransaction：
 *  requestor/property/数据/偏移 + 对端事件监听）。数据为认领镜像的
 *  深拷贝快照，镜像中途被覆盖或清除不影响在途传输的一致性。 */
typedef struct XpwClipIncrSession
{
    bool           m_active;    /**< 槽位在用。 */
    Window         m_requestor; /**< 请求者窗口（会话键）。 */
    Atom           m_property;  /**< 传输属性。 */
    Atom           m_selection; /**< 所属选择区（SelectionClear 时中止）。 */
    Atom           m_type;      /**< 数据目标原子（各分片保持一致，ICCCM 2.5）。 */
    unsigned char* m_data;      /**< 数据快照（拥有）。 */
    int            m_total;     /**< 总字节长度。 */
    int            m_offset;    /**< 已写出的字节数。 */
    unsigned long  m_lastMs;    /**< 最近一次活动的单调毫秒（闲置回收）。 */
} XpwClipIncrSession;

static XpwClipIncrSession g_xpwnClipIncrSessions[XPWN_CLIP_INCR_MAX_SESSIONS];

/* 前向：INCR 会话管理（实现在「X11 INCR 会话管理」小节）；
 * SelectionRequest 处理与事件泵位于其定义之前，需先行声明。 */
static void xpw_clipIncrAbortForSelection(Atom selection);
static void xpw_clipIncrAbortForWindow(Window requestor);
static int xpw_clipIncrChunkLimit(void);
static bool xpw_clipIncrStart(Window requestor, Atom property, Atom selection,
                              Atom type, const unsigned char* data, int len);
static bool xpw_clipHandleIncrDelete(const XPropertyEvent* pev);
/* EWMH 窗口状态回读（定义在 EWMH 窗口状态小节；PropertyNotify 分支
 * 用作 WM 状态回写 -> XWindow_reportWindowStateChanged 的换算入口）。 */
static XWindowState xpwn_queryWmWindowState(Display* display, Window win);
static void xpw_clipIncrSweepIdle(void);

static Atom g_xpwnXdndData;
static XWNPendingEntry g_xpwnEntries[XPWN_MAX_WINDOWS]; /**< 窗口注册表。 */

static void xpwn_releasePresentImage(XWNPendingEntry* entry)
{
    if (!entry) return;
    if (entry->m_presentImage) {
        /* XDestroyImage 默认会释放 data；缓冲由 XinYueC 的 Hybrid
           分配器管理，因此先摘除指针再销毁 Xlib 描述符。 */
        entry->m_presentImage->data = NULL;
        XDestroyImage(entry->m_presentImage);
        entry->m_presentImage = NULL;
    }
    XFree_Hybrid(entry->m_presentBuffer);
    entry->m_presentBuffer = NULL;
    entry->m_presentWidth = 0;
    entry->m_presentHeight = 0;
    entry->m_presentBytesPerLine = 0;
    entry->m_presentDirect = false;
}

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

/* Xlib 异步错误处理器。Xlib 默认处理器会把协议错误当致命错误 exit() 进程；
 * 而跨进程剪贴板/XDND 传输的协议对端窗口随时可能销毁（典型：INCR 分片
 * 传输中途对端退出，XChangeProperty 到已销毁窗口产生 BadWindow），此类
 * 可预期异步错误必须非致命化。对标 Qt xcb：X 协议错误是普通事件
 * （QXcbConnection 打印后继续），这里对齐该语义——打印并吞掉。 */
static int xpwn_xlibErrorHandler(Display* display, XErrorEvent* err)
{
    (void)display;
    fprintf(stderr, "[xpwn] X11 async error ignored: code=%d request=%d\n",
            err->error_code, err->request_code);
    return 0; /* 返回 0 表示错误已被处理，不交回默认致命处理器。 */
}

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
    XMemcpy(out, text->string.multi_byte, n);
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
    if (insertedLength) XMemcpy(entry->m_preedit + first, inserted, insertedLength);
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
            XMemcpy(data, raw, (size_t)itemCount);
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
/* ==================== fcitx5 DBus 输入法前端（对标 fcitx5-qt） ====================
 * XIM 遗留协议在本环境（fcitx5 + imdkit）下 IC 无法在核心注册（按键透传）。
 * 转用 fcitx5 的 DBus text-input 协议（org.fcitx.Fcitx.InputMethod1，Qt 应用
 * 同款路径）：CreateInputContext -> IC 对象（FocusIn/ProcessKeyEvent/
 * CommitString 信号）。libdbus 已是项目链接依赖。 */

/** @brief 进程级 IME DBus 连接与状态（所有窗口共享一个输入上下文通道）。 */
static DBusConnection* g_xpwnImeBus = NULL;      /**< session DBus 连接。 */
static char* g_xpwnImeIcPath = NULL;             /**< IC 对象路径（拥有）。 */
static char* g_xpwnImeKeybuf = NULL;             /**< CreateInputContext 密钥（拥有）。 */
static char* g_xpwnImeFocusWindow = NULL;        /**< 当前 IME 绑定的 X 窗口 id 串（拥有）。 */

/** @brief 构造发往 fcitx5 的方法调用消息（fire-and-forget 便捷式）。 */
static void xpwn_imeCall(const char* method, int firstVarArgType,
                         ...);

static DBusHandlerResult xpwn_imeFilter(DBusConnection* connection,
                                        DBusMessage* message, void* user_data);
/** @brief 抽取并分发 DBus 消息（事件泵每轮非阻塞调用）。 */
static void xpwn_imePump(void)
{
    DBusMessage* msg;
    if (!g_xpwnImeBus) return;
    /* 读 socket（0 超时 = 非阻塞）后逐条取出交给过滤器。 */
    dbus_connection_read_write(g_xpwnImeBus, 0);
    while ((msg = dbus_connection_pop_message(g_xpwnImeBus)) != NULL) {
        xpwn_imeFilter(g_xpwnImeBus, msg, NULL);
        dbus_message_unref(msg);
    }
    dbus_connection_flush(g_xpwnImeBus);
}

/** @brief IME 消息过滤器：CommitString -> XInputMethodEvent 上屏。 */
static DBusHandlerResult xpwn_imeFilter(DBusConnection* connection,
                                        DBusMessage* message, void* user_data)
{
    (void)connection;
    (void)user_data;
    /* 逐信号诊断输出收进 XPWN_IME_DEBUG（默认 0）：本过滤器每条 DBus
       信号都会进入，属生产路径残留（对标 Qt 平台插件静默，QLogging-
       Category 默认级别不输出）。失败诊断（bus/IC 创建失败）保留为
       进程一次性的启动告警，与 XOpenIM 失败提示同策略。 */
#if XPWN_IME_DEBUG
    XPrintf("[ime-dbus] signal: type=%d path=%s ifc=%s member=%s\n",
            (int)dbus_message_get_type(message),
            dbus_message_get_path(message) ? dbus_message_get_path(message) : "?",
            dbus_message_get_interface(message) ? dbus_message_get_interface(message) : "?",
            dbus_message_get_member(message) ? dbus_message_get_member(message) : "?");
#endif
    if (dbus_message_is_signal(message, "org.fcitx.Fcitx.InputContext1",
                               "CommitString")) {
        DBusError err;
        char* text = NULL;
#if XPWN_IME_DEBUG
        XPrintf("[ime-dbus] CommitString 分支进入\n");
#endif
        dbus_error_init(&err);
        if (dbus_message_get_args(message, &err, DBUS_TYPE_STRING, &text,
                                  DBUS_TYPE_INVALID) && text && text[0]) {
            /* 转发到当前 IME 绑定窗口（复用现有 IME 事件路径）。 */
            XWNPendingEntry* entry = NULL;
            if (g_xpwnImeFocusWindow) {
                Window wid = (Window)strtoul(g_xpwnImeFocusWindow, NULL, 0);
                entry = xpwn_findByNativeWindow(wid);
            }
            if (entry && entry->m_window)
                (void)XWindowSystemInterface_handleInputMethodEvent(
                    entry->m_window, "", text, 0, 0, -1, -1);
        }
        if (dbus_error_is_set(&err)) dbus_error_free(&err);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_signal(message, "org.fcitx.Fcitx.InputContext1",
                               "PreeditString")) {
        /* 预编辑显示为后续扩展（当前直接消费避免落入默认处理）。 */
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * @brief      判定 fcitx5 DBus 输入法前端是否应挂接（进程一次）。
 * @details    遵守 X11 输入法选择约定（问题 #32 第五轮收官根修）：
 *             - 环境变量 XPWN_IME 显式开关优先：none/off/0 强制停用，
 *               其余非空值强制挂接（夜间 Xvfb 分道/CI 用）。
 *             - 否则按 XMODIFIERS：显式 @im=none = 用户声明「不要输入
 *               法」→ 停用（此前被无视：DBus 前端照常 CreateInputContext，
 *               fcitx5 中文态对每个 KeyPress 的 ProcessKeyEvent 回
 *               consumed=true——裸拉丁字母恰是拼音组合起始键，全部被
 *               吞（keyPress 不产出、日志仅 keyRelease），XShortcut 字
 *               母键/编辑框字母插入全灭；组合完成后又以 CommitString 回
 *               投 CJK（复扫-5 lane4「nihao→你好」、lane3「'3'→'去'」
 *               实证闭环）。对标 Qt：QXcbIntegration 仅在输入法插件被
 *               环境选中时才装 inputContext，@im=none 下按键绝无过滤层，
 *               qxcbkeyboard.cpp handleKeyEvent 直接产出 KEY_PRESS）。
 *             - XMODIFIERS 未设或选中 fcitx：维持挂接（桌面中文主路径，
 *               目标④零回退）。
 * @return     true=挂接 DBus 前端；false=全程跳过（g_xpwnImeBus 保持
 *             NULL，ProcessKeyEvent/Pump/Focus 各调用点已有空守卫自然
 *             旁路，西文按键直达 keysym→KEY_PRESS）。
 */
static bool xpwn_imeWanted(void)
{
    const char* overrideEnv = getenv("XPWN_IME");
    const char* modifiers = getenv("XMODIFIERS");
    if (overrideEnv && overrideEnv[0]) {
        if (strcmp(overrideEnv, "none") == 0 ||
            strcmp(overrideEnv, "off") == 0 ||
            strcmp(overrideEnv, "0") == 0)
            return false;
        return true;
    }
    if (modifiers && strcmp(modifiers, "@im=none") == 0) return false;
    return true;
}

/**
 * @brief      初始化 fcitx5 DBus 输入法（进程一次）。
 * @details    连接 session 总线 -> CreateInputContext(程序名,桌面) ->
 *             保存 IC 路径与密钥 -> 订阅该 IC 的信号。失败静默降级
 *             （无中文输入，西文不受影响）。是否挂接由 xpwn_imeWanted()
 *             按环境约定判定（XMODIFIERS=@im=none / XPWN_IME=none 时
 *             全程跳过）。
 * @return     无返回值。
 */
static void xpwn_imeInit(void)
{
    DBusError err;
    DBusMessage* msg = NULL;
    DBusMessage* reply = NULL;
    DBusMessageIter iter;
    DBusMessageIter sub;
    dbus_bool_t ok = FALSE;
    if (g_xpwnImeBus) return;
    if (!xpwn_imeWanted()) {
#if XPWN_IME_DEBUG
        XPrintf("[ime-dbus] 输入法前端停用（XMODIFIERS=@im=none 或"
                " XPWN_IME=none）——按键不过滤直达应用\n");
#endif
        return;
    }
    dbus_error_init(&err);
    g_xpwnImeBus = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!g_xpwnImeBus) {
        XPrintf("[ime-dbus] session bus 失败: %s\n",
                err.message ? err.message : "?");
        dbus_error_free(&err);
        return;
    }
    msg = dbus_message_new_method_call("org.freedesktop.portal.Fcitx",
                                       "/inputmethod",
                                       "org.fcitx.Fcitx.InputMethod1",
                                       "CreateInputContext");
    if (!msg) return;
    dbus_message_iter_init_append(msg, &iter);
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ss)",
                                          &sub)) {
        dbus_message_unref(msg);
        return;
    }
    {
        DBusMessageIter st;
        const char* program = "XGuiWindowDemo";
        const char* desktop = "Deepin";
        dbus_message_iter_open_container(&sub, DBUS_TYPE_STRUCT, NULL, &st);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &program);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &desktop);
        dbus_message_iter_close_container(&sub, &st);
    }
    dbus_message_iter_close_container(&iter, &sub);
    reply = dbus_connection_send_with_reply_and_block(g_xpwnImeBus, msg, 500,
                                                      &err);
    dbus_message_unref(msg);
    if (!reply) {
        XPrintf("[ime-dbus] CreateInputContext 失败: %s\n",
                err.message ? err.message : "?");
        dbus_error_free(&err);
        return;
    }
    /* 应答：(o ay) —— IC 路径 + 密钥字节数组。 */
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH) {
        const char* path = NULL;
        dbus_message_iter_get_basic(&iter, &path);
        g_xpwnImeIcPath = strdup(path ? path : "");
        dbus_message_iter_next(&iter);
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
            DBusMessageIter arr;
            dbus_message_iter_recurse(&iter, &arr);
            {
                char* buf = NULL;
                size_t len = 0;
                while (dbus_message_iter_get_arg_type(&arr) ==
                       DBUS_TYPE_BYTE) {
                    unsigned char b = 0;
                    dbus_message_iter_get_basic(&arr, &b);
                    buf = (char*)realloc(buf, len + 1);
                    buf[len++] = (char)b;
                    dbus_message_iter_next(&arr);
                }
                if (buf) {
                    /* 追加终止符：扩容 1 字节保证 buf[len] 可写。 */
                    buf = (char*)realloc(buf, len + 1);
                    if (buf) buf[len] = '\0';
                }
                g_xpwnImeKeybuf = buf;
            }
        }
        ok = g_xpwnImeIcPath[0] != '\0';
    }
    dbus_message_unref(reply);
    if (!ok) {
        XPrintf("[ime-dbus] CreateInputContext 应答异常\n");
        return;
    }
    /* 订阅本 IC 的信号。 */
    {
        char rule[512];
        snprintf(rule, sizeof(rule),
                 "type='signal',path='%s',"
                 "interface='org.fcitx.Fcitx.InputContext1'",
                 g_xpwnImeIcPath);
        dbus_bus_add_match(g_xpwnImeBus, rule, &err);
        if (dbus_error_is_set(&err)) {
            XPrintf("[ime-dbus] add_match 失败: %s\n",
                    err.message ? err.message : "?");
            dbus_error_free(&err);
        }
        dbus_connection_flush(g_xpwnImeBus); /* match 必须送达总线。 */
        dbus_connection_add_filter(g_xpwnImeBus, xpwn_imeFilter, NULL, NULL);
        dbus_connection_flush(g_xpwnImeBus);
    }
#if XPWN_IME_DEBUG
    XPrintf("[ime-dbus] IC=%s（DBus 输入法就绪）唯一连接名=%s\n",
            g_xpwnImeIcPath, dbus_bus_get_unique_name(g_xpwnImeBus));
#endif
}

/**
 * @brief      转发按键给 fcitx5（ProcessKeyEvent）。
 * @details    返回 true 表示 fcitx 消费了该键（调用方丢弃，不产出
 *             XKey）；false/超时/未初始化 = 正常键盘处理。
 * @param      keyval  X11 keysym。
 * @param      keycode X11 键码。
 * @param      state   修饰键掩码。
 * @param      release 是否释放事件。
 * @return     true=已消费。
 */
static bool xpwn_imeProcessKey(int keyval, unsigned keycode,
                               unsigned state, bool release)
{
    DBusMessage* msg;
    DBusMessage* reply;
    DBusError err;
    dbus_bool_t consumed = FALSE;
    if (!g_xpwnImeBus || !g_xpwnImeIcPath) return false;
    msg = dbus_message_new_method_call("org.freedesktop.portal.Fcitx",
                                       g_xpwnImeIcPath,
                                       "org.fcitx.Fcitx.InputContext1",
                                       "ProcessKeyEvent");
    if (!msg) return false;
    dbus_error_init(&err);
    {
        dbus_uint32_t kv = (dbus_uint32_t)keyval;
        dbus_uint32_t kc = (dbus_uint32_t)keycode;
        dbus_uint32_t st = (dbus_uint32_t)state;
        dbus_bool_t rel = release ? TRUE : FALSE;
        dbus_uint32_t t = 0;
        if (!dbus_message_append_args(msg, DBUS_TYPE_UINT32, &kv,
                                      DBUS_TYPE_UINT32, &kc,
                                      DBUS_TYPE_UINT32, &st,
                                      DBUS_TYPE_BOOLEAN, &rel,
                                      DBUS_TYPE_UINT32, &t,
                                      DBUS_TYPE_INVALID)) {
            dbus_message_unref(msg);
            return false;
        }
    }
    reply = dbus_connection_send_with_reply_and_block(g_xpwnImeBus, msg, 100,
                                                      &err);
    dbus_message_unref(msg);
    if (reply) {
        if (dbus_message_get_args(reply, &err, DBUS_TYPE_BOOLEAN, &consumed,
                                  DBUS_TYPE_INVALID))
            dbus_message_unref(reply);
    }
    if (dbus_error_is_set(&err)) dbus_error_free(&err);
    /* 逐键诊断输出收进 XPWN_IME_DEBUG（默认 0）：ProcessKeyEvent 每次按键
       都会走到这里，直接 fprintf(stderr) 属生产路径残留（对标 Qt 平台
       插件静默，QLoggingCategory 默认级别不输出）。 */
#if XPWN_IME_DEBUG
    fprintf(stderr, "[ime-dbus] ProcessKeyEvent -> consumed=%d\n",
            (int)consumed);
#endif
    return consumed != FALSE;
}

/**
 * @brief      焦点变化时同步 fcitx5（FocusIn/FocusOut）。
 * @param      xwin  原生窗口 id（用于 Commit 回投）。
 * @param      focusIn true=获得焦点。
 * @return     无返回值。
 */
static void xpwn_imeFocus(Window xwin, bool focusIn)
{
    DBusMessage* msg;
    const char* method = focusIn ? "FocusIn" : "FocusOut";
    char widBuf[32];
    if (!g_xpwnImeBus || !g_xpwnImeIcPath) return;
    if (focusIn) {
        snprintf(widBuf, sizeof(widBuf), "%lu", (unsigned long)xwin);
        free(g_xpwnImeFocusWindow);
        g_xpwnImeFocusWindow = strdup(widBuf);
    }
    msg = dbus_message_new_method_call("org.freedesktop.portal.Fcitx",
                                       g_xpwnImeIcPath,
                                       "org.fcitx.Fcitx.InputContext1",
                                       method);
    if (!msg) return;
    dbus_message_set_no_reply(msg, TRUE);
    dbus_connection_send(g_xpwnImeBus, msg, NULL);
    dbus_message_unref(msg);
    if (focusIn) xpwn_imePump();
}

/* ==================== 屏幕接入与 DPI 回填（对标 QXcbConnection/QXcbScreen） ==================== */

/* 屏幕接入上限（RandR 监视器数远小于窗口数；超出部分忽略并保留日志）。 */
#define XPWN_MAX_SCREENS 8

/** @brief RandR 扩展事件基址；-1 表示扩展不可用（未编译/查询失败）。 */
static int g_xpwnRrEventBase = -1;
/** @brief 平台创建并登记的屏幕对象表（拥有，经 XScreen_delete_base 释放）。 */
static XScreen* g_xpwnScreens[XPWN_MAX_SCREENS];
/** @brief 已登记屏幕数量。 */
static int g_xpwnScreenCount;
/** @brief 屏幕枚举是否已执行（惰性连接只初始化一次）。 */
static bool g_xpwnScreensInitDone;

/**
 * @brief      逻辑 DPI 回填（对标 QXcbScreen::logicalDpi 的 Xft 资源读取）。
 * @details    优先级：RESOURCE_MANAGER 根窗口属性中的 Xft.dpi 资源 >
 *             XGetDefault（含 ~/.Xdefaults 回落）> 96。每帧直读根窗口
 *             属性而非依赖 XGetDefault 的 Xlib 资源库缓存——XGetDefault
 *             首次调用后把资源库缓存在 Display 上，xrdb 重载后不再可见，
 *             运行期刷新（XPlatformNativeWindow_refreshScreenLogicalDpi）
 *             需要每次真实读取（对标 Qt xcb 每次 get_property 的口径）。
 *             解析失败或值非正时一律 96（Qt 平台默认值），结果恒 > 0。
 * @return     逻辑 DPI（水平与垂直同值）。
 */
static float xpwn_screenLogicalDpi(void)
{
    Atom type;
    int format;
    unsigned long extra = 0;
    unsigned long count = 0;
    unsigned char* data = NULL;
    float dpi = 0.0f;
    if (XGetWindowProperty(g_xpwnDisplay,
                           RootWindow(g_xpwnDisplay, g_xpwnScreenNumber),
                           XA_RESOURCE_MANAGER, 0, 1 << 16, False,
                           XA_STRING, &type, &format, &count, &extra,
                           &data) == Success &&
        data && type == XA_STRING && format == 8)
    {
        XrmDatabase db = XrmGetStringDatabase((char*)data);
        XrmValue value;
        char* valueType = NULL;
        if (db &&
            XrmGetResource(db, "Xft.dpi", "Xft.Dpi",
                           &valueType, &value) &&
            value.addr && value.addr[0])
            sscanf(value.addr, "%f", &dpi);
        if (db) XrmDestroyDatabase(db);
    }
    if (data) xpwn_xFree(data); /* XFree 已被宏替换为 XMemory_free。 */
    if (!(dpi > 0.0f))
    {
        /* 属性缺失/无 Xft.dpi 资源：回落 XGetDefault（含 ~/.Xdefaults
           路径），再回落 96。 */
        const char* resource = XGetDefault(g_xpwnDisplay, "Xft", "dpi");
        if (resource && resource[0]) {
            if (sscanf(resource, "%f", &dpi) == 1 && dpi > 0.0f)
                return dpi;
        }
        return 96.0f;
    }
    return dpi;
}

/**
 * @brief      回填屏幕物理尺寸（毫米）；物理 DPI 由 XScreen 按几何联动换算。
 * @param      screen 目标屏幕。
 * @param      widthMm 物理宽（毫米）。
 * @param      heightMm 物理高（毫米）。
 */
static void xpwn_screenFillPhysical(XScreen* screen, float widthMm,
                                    float heightMm)
{
    XSizeF physicalSize;
    physicalSize.width = widthMm;
    physicalSize.height = heightMm;
    XScreen_setPhysicalSize(screen, &physicalSize);
}

/**
 * @brief      按平台观测值回填一块 XScreen 的静态属性。
 * @details    geometry 为虚拟桌面像素矩形；physicalSize 为 EDID 毫米尺寸
 *             （物理 DPI 由 XScreen 按 pixels/(mm/25.4) 换算；毫米为 0 的
 *             虚拟显示器保留 0，不伪造，Qt xcb 同样不编造 EDID）。
 *             devicePixelRatio 不回填、保持 1.0：X11 无 HiDPI 缩放管道，
 *             与 Qt xcb（QXcbScreen::devicePixelRatio 无强制时恒为 1）一致。
 * @param      screen 目标屏幕。
 * @param      name 屏幕名（监视器名或 Screen<N>）；可为 NULL。
 * @param      geometry 像素几何。
 * @param      widthMm 物理宽（毫米）。
 * @param      heightMm 物理高（毫米）。
 * @param      dpi 逻辑 DPI。
 */
static void xpwn_screenFill(XScreen* screen, const char* name,
                            const XRect* geometry,
                            float widthMm, float heightMm, float dpi)
{
    XScreen_setName_2(screen, name ? name : "Screen");
    XScreen_setDepth(screen, DefaultDepth(g_xpwnDisplay, g_xpwnScreenNumber));
    XScreen_setGeometry(screen, geometry);
    xpwn_screenFillPhysical(screen, widthMm, heightMm);
    XScreen_setLogicalDotsPerInch(screen, dpi, dpi);
}

/**
 * @brief      注销并释放一块平台屏幕（对标 Qt screenRemoved 处理链）。
 * @details    顺序与 QGuiApplicationPrivate::processScreenRemoved 一致：
 *              1) handleScreenRemoved：从 XScreen 注册表注销并发射
 *                 screenRemoved 信号；若移除的是主屏，XGuiApplication 内部
 *                 先把主屏晋升为剩余第一块（并发射 primaryScreenChanged）。
 *              2) 驻留被移除屏幕的顶层窗口迁移到（新）主屏：对标 Qt 的
 *                 QWindow::setScreen(newPrimary) 迁移语义，发既有
 *                 screenChanged 信号；几何钳位回主屏并经 WSI
 *                 handleGeometryChange 持久化/投递 Resize，最后经平台入口
 *                 同步移动原生 X11 窗口（最小实现，Qt 中由平台层
 *                 QXcbWindow::setGeometry 承担）。
 *              3) 释放屏幕对象（所有权归平台层，Qt 删除 QScreen 同点）。
 * @param      screen 被移除屏幕；可为 NULL（no-op）。
 */
static void xpwn_screenRemove(XScreen* screen)
{
    XScreen* target;
    XVector* windows;
    size_t k;
    if (!screen) return;
    /* 1) 注册表注销 + screenRemoved 信号（主屏晋升在内部完成）。 */
    XWindowSystemInterface_handleScreenRemoved(screen);
    /* 2) 窗口迁移：屏幕已不在注册表，XWindow_screen 仍指向它的顶层窗口
       全部迁往现主屏；无主屏（全移除）时跳过。 */
    target = XScreen_primaryScreen();
    windows = XGuiApplication_allWindows();
    for (k = 0; windows && k < XVector_size_base((const XContainer*)windows);
         ++k) {
        XWindow* window = XVector_At_Base(windows, (int64_t)k, XWindow*);
        if (!window || XWindow_screen(window) != screen) continue;
        if (target) {
            XRect g = XWindow_geometry(window);
            XRect pg = XScreen_geometry(target);
            /* 几何钳位到主屏：宽度/高度先压到主屏内，再拉回越界偏移。 */
            if (g.width > pg.width) g.width = pg.width;
            if (g.height > pg.height) g.height = pg.height;
            if (g.x < pg.x) g.x = pg.x;
            if (g.y < pg.y) g.y = pg.y;
            if (g.x + g.width > pg.x + pg.width)
                g.x = pg.x + pg.width - g.width;
            if (g.y + g.height > pg.y + pg.height)
                g.y = pg.y + pg.height - g.height;
            XWindow_setScreen(window, target); /* 发 screenChanged 信号。 */
            XWindowSystemInterface_handleGeometryChange(window, &g);
            XPlatformNativeWindow_setGeometry(window, &g); /* 原生同步。 */
        } else {
            XWindow_setScreen(window, NULL); /* 无屏可迁，回退主屏语义。 */
        }
    }
    if (windows) XVector_delete_base((XClass*)windows);
    /* 3) 平台层持有所有权，负责释放。 */
    XScreen_delete_base((XClass*)screen);
}

/**
 * @brief      按监视器覆盖情况差分注销已登记屏幕（xpwn_screenRemove）。
 * @param      matched 长度 XPWN_MAX_SCREENS；true 表示该槽位屏幕仍存在。
 */
static void xpwn_screensRemoveMissing(const bool* matched)
{
    int i;
    for (i = g_xpwnScreenCount - 1; i >= 0; --i) {
        XScreen* screen;
        if (matched[i] || !g_xpwnScreens[i]) continue;
        screen = g_xpwnScreens[i];
        g_xpwnScreens[i] = g_xpwnScreens[g_xpwnScreenCount - 1];
        g_xpwnScreens[g_xpwnScreenCount - 1] = NULL;
        --g_xpwnScreenCount;
        xpwn_screenRemove(screen);
    }
}

/**
 * @brief      主屏重选：包含原点 (0,0) 的监视器，否则取第一块登记屏幕。
 * @details    对标 QXcbConnection::updateScreen 的主屏更新语义：RandR
 *             primary 输出变化（含几何挪动使 (0,0) 落到别的监视器）后
 *             重选主屏；经 XGuiApplication_setPrimaryScreen 专用入口，
 *             变化时照发 primaryScreenChanged 信号。
 */
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

/**
 * @brief      登记（或差分刷新）平台屏幕列表：优先 RandR 监视器，回落 X 屏幕。
 * @details    枚举路径：
 *              1) RandR 1.5 XRRGetMonitors：逐监视器一屏（对标 Qt
 *                 QXcbConnection::getMonitors 的多监视器语义），几何/毫米
 *                 尺寸取监视器字段；并订阅 RRScreenChangeNotifyMask 供
 *                 事件泵刷新。
 *              2) RandR 扩展不可用时回落 XScreenOfDisplay：每个 X 屏幕
 *                 一屏，几何取 DisplayWidth/Height，毫米取
 *                 DisplayWidthMM/HeightMM。
 *             登记统一经 XWindowSystemInterface_handleScreenAdded（注册表
 *             + screenAdded 信号）。
 *             热刷新（refresh=true）为与注册表的差分（对标 Qt
 *             QXcbConnection::updateScreens 的增删语义）：
 *              - 按监视器名（RandR output name）匹配既有屏幕，命中者仅
 *                差分回填几何/物理尺寸（对标 QXcbScreen::updateGeometry，
 *                内部变化才发 changed 信号）；
 *              - 新出现的监视器经 handleScreenAdded 登记（对标
 *                QGuiApplication::screenAdded）；
 *              - 消失的监视器经 xpwn_screenRemove 注销（对标
 *                screenRemoved，含驻留窗口迁移）。
 * @param      refresh true 表示热刷新（差分增删既有注册表）。
 */
static void xpwn_screensEnumerate(bool refresh)
{
    float dpi = xpwn_screenLogicalDpi();
    int i;
#if defined(XINYUE_C_HAS_XRANDR)
    if (g_xpwnRrEventBase >= 0) {
        Window root = RootWindow(g_xpwnDisplay, g_xpwnScreenNumber);
        int monitorCount = 0;
        XRRMonitorInfo* monitors = XRRGetMonitors(g_xpwnDisplay, root,
                                                  True, &monitorCount);
        bool matched[XPWN_MAX_SCREENS];
        memset(matched, 0, sizeof(matched));
        if (monitors && monitorCount > 0) {
            for (i = 0; i < monitorCount; ++i) {
                XRect geometry;
                char* monitorName;
                geometry.x = monitors[i].x;
                geometry.y = monitors[i].y;
                geometry.width = monitors[i].width;
                geometry.height = monitors[i].height;
                monitorName = XGetAtomName(g_xpwnDisplay, monitors[i].name);
                if (refresh) {
                    /* 差分：按监视器名匹配既有屏幕（Qt 以 output name
                       标识 QXcbScreen，同名即同一块屏，只做几何回填）。 */
                    XScreen* existing = NULL;
                    int j;
                    for (j = 0; j < g_xpwnScreenCount; ++j) {
                        const char* known;
                        if (matched[j] || !g_xpwnScreens[j]) continue;
                        known = XScreen_name_2(g_xpwnScreens[j]);
                        if (known && monitorName &&
                            strcmp(known, monitorName) == 0) {
                            existing = g_xpwnScreens[j];
                            matched[j] = true;
                            break;
                        }
                    }
                    if (existing) {
                        /* 对标 QXcbScreen::updateGeometry：几何经 WSI 入口
                           差分同步（变化才发 changed 信号），物理尺寸随行
                           刷新（内部联动 physicalDotsPerInchChanged）。 */
                        XWindowSystemInterface_handleScreenGeometryChange(
                            existing, &geometry, NULL);
                        xpwn_screenFillPhysical(existing,
                                                (float)monitors[i].mwidth,
                                                (float)monitors[i].mheight);
                    } else if (g_xpwnScreenCount < XPWN_MAX_SCREENS) {
                        /* 新监视器热接入：对标 QGuiApplication::screenAdded。 */
                        XScreen* screen = XScreen_create();
                        if (screen) {
                            xpwn_screenFill(screen, monitorName, &geometry,
                                            (float)monitors[i].mwidth,
                                            (float)monitors[i].mheight, dpi);
                            XWindowSystemInterface_handleScreenAdded(screen);
                            /* 新槽位视为已匹配：避免本轮注销阶段把刚登记
                               的屏幕误删（差分自反性）。 */
                            matched[g_xpwnScreenCount] = true;
                            g_xpwnScreens[g_xpwnScreenCount++] = screen;
                        }
                    }
                } else if (i < XPWN_MAX_SCREENS) {
                    /* 首次枚举：直接登记（上限内）。 */
                    XScreen* screen = XScreen_create();
                    if (!screen) {
                        if (monitorName) xpwn_xFree(monitorName);
                        break;
                    }
                    xpwn_screenFill(screen, monitorName, &geometry,
                                    (float)monitors[i].mwidth,
                                    (float)monitors[i].mheight, dpi);
                    XWindowSystemInterface_handleScreenAdded(screen);
                    g_xpwnScreens[g_xpwnScreenCount++] = screen;
                }
                if (monitorName) xpwn_xFree(monitorName);
            }
        }
        /* 消失的监视器（含 monitorCount<=0 的全拔出）→ 差分注销。 */
        if (refresh) xpwn_screensRemoveMissing(matched);
        if (monitors) XRRFreeMonitors(monitors);
        /* 首次枚举时订阅屏幕变化通知（几何/模式变化 → RRScreenChangeNotify，
           由事件泵统一刷新）。 */
        if (!refresh)
            XRRSelectInput(g_xpwnDisplay, root, RRScreenChangeNotifyMask);
        if (g_xpwnScreenCount > 0 || refresh) {
            /* RandR 路径完成；主屏重选由调用方（init/refresh）统一执行。 */
            return;
        }
    }
#endif /* XINYUE_C_HAS_XRANDR */
    if (refresh) return; /* 无 RandR 时不重复回落枚举。 */
    /* 回落路径：XScreenOfDisplay 每屏一屏（单屏 X 服务即 1 块屏幕）。 */
    {
        int screenCount = ScreenCount(g_xpwnDisplay);
        int limit = screenCount < XPWN_MAX_SCREENS ? screenCount : XPWN_MAX_SCREENS;
        for (i = 0; i < limit; ++i) {
            XScreen* screen = XScreen_create();
            XRect geometry;
            char name[32];
            if (!screen) break;
            snprintf(name, sizeof(name), "Screen%d", i);
            geometry.x = 0;
            geometry.y = 0;
            geometry.width = DisplayWidth(g_xpwnDisplay, i);
            geometry.height = DisplayHeight(g_xpwnDisplay, i);
            xpwn_screenFill(screen, name, &geometry,
                            (float)DisplayWidthMM(g_xpwnDisplay, i),
                            (float)DisplayHeightMM(g_xpwnDisplay, i), dpi);
            XWindowSystemInterface_handleScreenAdded(screen);
            g_xpwnScreens[g_xpwnScreenCount++] = screen;
        }
    }
}

/** @brief 首次枚举并选定主屏幕（幂等；由惰性连接建立后调用）。 */
static void xpwn_screensInit(void)
{
    if (g_xpwnScreensInitDone) return;
    g_xpwnScreensInitDone = true;
#if defined(XINYUE_C_HAS_XRANDR)
    {
        int rrErrorBase = 0;
        if (XRRQueryExtension(g_xpwnDisplay, &g_xpwnRrEventBase, &rrErrorBase) &&
            g_xpwnRrEventBase >= 0) {
            /* RandR 扩展可用：事件基址已记录，供事件泵识别
               RRScreenChangeNotify（枚举内再订阅掩码）。 */
        } else {
            g_xpwnRrEventBase = -1; /* 扩展查询失败，回落 XScreenOfDisplay。 */
        }
    }
#endif /* XINYUE_C_HAS_XRANDR */
    xpwn_screensEnumerate(false);
    /* 主屏幕：包含原点 (0,0) 的监视器（对标 QXcbConnection 主屏判定，
       经 setPrimaryScreen 专用入口，变化时发射 primaryScreenChanged）。 */
    xpwn_screensReselectPrimary();
}

/**
 * @brief      屏幕热刷新（事件泵收到 RRScreenChangeNotify 时调用）。
 * @details    先 XRRUpdateConfiguration 刷新 Xlib 缓存的 DisplayWidth 等
 *             服务器几何，再按监视器重枚举并与注册表差分：既有屏幕回填
 *             几何/物理尺寸（内部按变化发射 geometryChanged/
 *             physicalDotsPerInchChanged 等），新监视器经 handleScreenAdded
 *             登记、消失的监视器经 xpwn_screenRemove 注销（含驻留窗口迁移
 *             到主屏）；最后按 (0,0) 规则重选主屏（对标 QXcbConnection
 *             updateScreens 的增删 + 主屏更新语义，信号照发）。
 * @param      event 原生 RRScreenChangeNotify 事件；可为 NULL（跳过
 *             Xlib 缓存刷新，仅重枚举）。
 */
/**
 * @brief      重读 Xft.dpi 并差分回填全部已登记屏幕（对标 Qt 的
 *             QXcbScreen 处理 Xft.dpi 变更的路径——xcb 没有资源变更
 *             推送事件，Qt 同样依赖轮询/手工触发而非服务端通知）。
 * @details    每块屏幕经 WSI 入口 XWindowSystemInterface_-
 *             handleScreenLogicalDotsPerInchChange 单值回填 X/Y；
 *             XScreen_setLogicalDotsPerInch 内部做浮点近等差分，值未
 *             变化时不发射 logicalDotsPerInchChanged。
 * @return     本轮是否有屏幕的逻辑 DPI 实际发生变化。
 */
static bool xpwn_screensApplyLogicalDpi(void)
{
    float dpi = xpwn_screenLogicalDpi();
    bool changed = false;
    int i;
    for (i = 0; i < g_xpwnScreenCount; ++i) {
        XScreen* screen = g_xpwnScreens[i];
        if (!screen) continue;
        /* 快路径跳过：与当前平均值完全相等时无需回填（XScreen 内部
           还有浮点近等差分兜底，重复值不会发信号）。 */
        if (XScreen_logicalDotsPerInch(screen) == dpi) continue;
        XWindowSystemInterface_handleScreenLogicalDotsPerInchChange(screen,
                                                                    dpi);
        changed = true;
    }
    return changed;
}

static void xpwn_screensRefresh(X11_XEvent* event)
{
#if defined(XINYUE_C_HAS_XRANDR)
    if (g_xpwnRrEventBase < 0) return;
    if (event) XRRUpdateConfiguration(event);
    xpwn_screensEnumerate(true);
    xpwn_screensReselectPrimary();
    /* 分辨率变化常伴 xrdb 重载（桌面缩放设置往往一并改 Xft.dpi）：顺带
       重读逻辑 DPI 并差分回填（值未变时 XScreen 内部不发信号）。xcb 无
       Xft.dpi 变更推送，Qt 亦靠轮询/手工触发，此处随 RR 通知轮询一次。 */
    xpwn_screensApplyLogicalDpi();
#else
    (void)event;
#endif
}

/* ==================== 光标后端（XCursor 平台钩子，对标 Qt QPlatformCursor） ==================== */

/** @brief 形状字体光标缓存槽容量（XCursor_Arrow..XCursor_Last + Blank 专用）。 */
#define XPWN_CURSOR_CACHE_SIZE 25

/** @brief 形状→字体光标缓存（进程期持有；XCloseDisplay 时随连接释放）。 */
static struct
{
    bool   m_valid;  /**< 该槽是否已创建过 Cursor。 */
    Cursor m_cursor; /**< X11 字体/像素图光标资源。 */
} g_xpwnCursorCache[XPWN_CURSOR_CACHE_SIZE];

/** @brief Blank（隐藏光标）专用 1x1 空像素图光标；创建一次复用。 */
static Cursor g_xpwnBlankCursor = None;
static bool g_xpwnBlankCursorCreated = false;

/**
 * @brief      XGui 光标形状 → X11 cursor font 字形映射。
 * @details    对标 Qt qcursor_x11 的形状表：Qt 标准形状中进入字体映射的
 *             子集逐一对齐；Qt 用位图自绘的形状（Forbidden/Busy/手型/拖拽
 *             系列）取语义最近的字体字形并注明近似。SplitV/SplitH 沿用
 *             Qt 的历史选择（SplitV→水平双箭头：垂直分割条沿水平方向
 *             拖动）。返回 -1 表示字体无对应（Blank 走专用空像素图；
 *             Bitmap/Custom 不进本表——走 XCreatePixmapCursor 1bit 位图
 *             通道，见 xpwn_cursorAcquireBitmapCursor/
 *             xpwn_cursorAcquirePixmapCursor，不再回落左箭头）。
 * @return     cursorfont 字形值；无对应字形返回 -1。
 */
static int xpwn_cursorShapeToFontGlyph(XCursorShape shape)
{
    switch (shape) {
    case XCursor_Arrow:        return XC_left_ptr;           /* 68 */
    case XCursor_UpArrow:      return XC_center_ptr;         /* 22 */
    case XCursor_Cross:        return XC_crosshair;          /* 34 */
    case XCursor_Wait:         return XC_watch;              /* 150 */
    case XCursor_IBeam:        return XC_xterm;              /* 152 */
    case XCursor_SizeVer:      return XC_sb_v_double_arrow;  /* 116 */
    case XCursor_SizeHor:      return XC_sb_h_double_arrow;  /* 108 */
    case XCursor_SizeBDiag:    return XC_top_right_corner;   /* 136 */
    case XCursor_SizeFDiag:    return XC_top_left_corner;    /* 134 */
    case XCursor_SizeAll:      return XC_fleur;              /* 52 */
    case XCursor_SplitV:       return XC_sb_h_double_arrow;  /* 108（同 Qt）。 */
    case XCursor_SplitH:       return XC_sb_v_double_arrow;  /* 116（同 Qt）。 */
    case XCursor_PointingHand: return XC_hand2;              /* 60 */
    case XCursor_Forbidden:    return XC_pirate;             /* 88（近似）。 */
    case XCursor_WhatsThis:    return XC_question_arrow;     /* 92 */
    case XCursor_Busy:         return XC_watch;              /* 150（近似）。 */
    case XCursor_OpenHand:     return XC_hand1;              /* 58（近似）。 */
    case XCursor_ClosedHand:   return XC_hand1;              /* 58（共用近似）。 */
    case XCursor_DragCopy:     return XC_plus;               /* 90（近似）。 */
    case XCursor_DragMove:     return XC_fleur;              /* 52（近似）。 */
    case XCursor_DragLink:     return XC_question_arrow;     /* 92（近似）。 */
    default:                   return -1;                    /* Blank/Bitmap/Custom。 */
    }
}

/** @brief 取（或创建）形状对应的 X11 光标资源；失败返回 None。 */
static Cursor xpwn_cursorAcquireShapeCursor(XCursorShape shape)
{
    int glyph;
    int slot;
    if (shape == XCursor_Blank) {
        /* 隐藏光标：字体无对应，用 1x1 透明像素图构建（Qt 同思路）。 */
        if (!g_xpwnBlankCursorCreated) {
            Pixmap blank;
            XColor dummy;
            static char bits[8] = {0};
            g_xpwnBlankCursorCreated = true;
            blank = XCreateBitmapFromData(g_xpwnDisplay,
                                          RootWindow(g_xpwnDisplay,
                                                     g_xpwnScreenNumber),
                                          bits, 1, 1);
            if (blank != None) {
                XMemset(&dummy, 0, sizeof(dummy));
                g_xpwnBlankCursor = XCreatePixmapCursor(g_xpwnDisplay, blank,
                                                        blank, &dummy, &dummy,
                                                        0, 0);
                XFreePixmap(g_xpwnDisplay, blank);
            }
        }
        return g_xpwnBlankCursor;
    }
    if ((int)shape < 0 || (int)shape >= XPWN_CURSOR_CACHE_SIZE)
        return None;
    slot = (int)shape;
    if (g_xpwnCursorCache[slot].m_valid)
        return g_xpwnCursorCache[slot].m_cursor;
    glyph = xpwn_cursorShapeToFontGlyph(shape);
    if (glyph < 0)
        return None; /* 字体无对应（Bitmap/Custom 已由 apply 分流到位图
                        通道）：不再回落左箭头，返回 None 交上层静默。 */
    g_xpwnCursorCache[slot].m_cursor =
        XCreateFontCursor(g_xpwnDisplay, (unsigned int)glyph);
    g_xpwnCursorCache[slot].m_valid = g_xpwnCursorCache[slot].m_cursor != None;
    return g_xpwnCursorCache[slot].m_cursor;
}

/* ==================== 位图/像素图光标（XCreatePixmapCursor 1bit 通道） ====================
 * 对标 Qt QCursor(QBitmap/QPixmap) 的 X11 落地：XCreatePixmapCursor 只
 * 承载双色模型——source 位选前景/背景色（此处黑/白，与 Qt 位图光标的
 * color1=黑→前景、color0=白→背景一致），mask 位选透写（1=绘制、0=
 * 透明），遮罩可选（无遮罩以 source 自身代之——整体不透明）。全彩
 * ARGB 像素图光标需 RENDER 扩展（对标 Qt xcb 的 XRenderCreateCursor
 * 路径），本批按 1bit 传统通道落地。 */

/** @brief 取 XBitmap 的 1bit 紧凑位数据（(w+7)/8 字节/行、字节内 LSB
 *  对应行内最左像素，与 XCreateBitmapFromData 的 XYBitmap/LSBFirst
 *  逐字节约定一致；对标 Qt QBitmap 的 MonoLSB 数据直拷进 X 位图）。
 *  Mono（MSB 序）存储则逐字节位反转归一。
 *  @return 成功返回 XMemory 分配缓冲（调用方 XFree_System 释放），
 *          非单色位图/空位图/分配失败返回 NULL。 */
static unsigned char* xpwn_cursorBitmapBits(const XBitmap* bmp, int* outW,
                                            int* outH)
{
    XImage img;
    XImageFormat fmt;
    unsigned char* packed;
    int w, h, lineBytes, stride, row;
    if (!bmp || !outW || !outH) return NULL;
    XImage_init(&img);
    XPixmap_toImage(&bmp->m_class, &img);
    fmt = XImage_format(&img);
    w = XImage_width(&img);
    h = XImage_height(&img);
    if ((fmt != XImageFormat_Mono && fmt != XImageFormat_MonoLSB) ||
        w <= 0 || h <= 0) {
        XImage_deinit_base(&img);
        return NULL;
    }
    lineBytes = (w + 7) / 8;
    stride = XImage_bytesPerLine(&img);
    packed = (unsigned char*)XMemory_malloc((size_t)lineBytes * (size_t)h,
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!packed) {
        XImage_deinit_base(&img);
        return NULL;
    }
    for (row = 0; row < h; ++row) {
        const unsigned char* src = XImage_scanLine(&img, row);
        unsigned char* dst = packed + (size_t)row * (size_t)lineBytes;
        int col;
        if (!src || stride < lineBytes) {
            XFree_System(packed);
            XImage_deinit_base(&img);
            return NULL;
        }
        if (fmt == XImageFormat_MonoLSB) {
            /* MonoLSB（XBitmap 固定存储序）：位排列已一致，逐行直拷。 */
            XMemcpy(dst, src, (size_t)lineBytes);
        } else {
            /* Mono（MSB 序）：逐字节位反转成 LSB 序。 */
            for (col = 0; col < lineBytes; ++col) {
                unsigned char b = src[col];
                unsigned char r = 0;
                int bit;
                for (bit = 0; bit < 8; ++bit)
                    if (b & (unsigned char)(1u << bit))
                        r |= (unsigned char)(1u << (7 - bit));
                dst[col] = r;
            }
        }
    }
    XImage_deinit_base(&img);
    *outW = w;
    *outH = h;
    return packed;
}

/* 位数据 → X11 双色光标：source/mask 各为 (w+7)/8 字节/行的 LSB 位图。
 * 前景黑、背景白（对标 Qt 位图光标 X11 落地的前景/背景取值）；热点
 * 未设置（负值）用中心，越界钳制进图内（X11 要求热点落在光标内）。
 * mask 为 NULL 时以 source 代之（整体不透明的黑白光标）。临时 Pixmap
 * 与 Cursor 即用即毁：XDefineCursor 后窗口持有服务器侧引用，按 Xlib
 * 手册 XFreeCursor 仅解除资源号关联、存储待无引用后才释放（对标 Qt
 * 在 QCursor 析构时统一 XFreeCursor；形状字体光标仍走进程级缓存）。
 * 失败返回 None。 */
static Cursor xpwn_cursorCreateFromBits(const unsigned char* srcBits,
                                        const unsigned char* maskBits,
                                        int w, int h, int hotX, int hotY)
{
    Pixmap srcPix;
    Pixmap maskPix;
    X11_XColor fg;
    X11_XColor bg;
    Cursor cur = None;
    Window root;
    if (!srcBits || w <= 0 || h <= 0) return None;
    root = RootWindow(g_xpwnDisplay, g_xpwnScreenNumber);
    srcPix = XCreateBitmapFromData(g_xpwnDisplay, root,
                                   (const char*)srcBits,
                                   (unsigned int)w, (unsigned int)h);
    maskPix = None;
    if (maskBits)
        maskPix = XCreateBitmapFromData(g_xpwnDisplay, root,
                                        (const char*)maskBits,
                                        (unsigned int)w, (unsigned int)h);
    if (maskPix == None)
        maskPix = srcPix; /* 遮罩可选：无遮罩/创建失败以 source 代之。 */
    if (srcPix != None) {
        /* 黑前景（位 1 = Qt::color1）+ 白背景（位 0 = Qt::color0）。
         * XCreatePixmapCursor 直接取 red/green/blue 原值，无需 colormap。 */
        XMemset(&fg, 0, sizeof(fg));
        XMemset(&bg, 0, sizeof(bg));
        bg.red = bg.green = bg.blue = 0xffff;
        if (hotX < 0) hotX = w / 2; /* (-1,-1)=中心（XCursor 头文件约定）。 */
        if (hotY < 0) hotY = h / 2;
        if (hotX >= w) hotX = w - 1;
        if (hotY >= h) hotY = h - 1;
        cur = XCreatePixmapCursor(g_xpwnDisplay, srcPix, maskPix,
                                  &fg, &bg, (unsigned int)hotX,
                                  (unsigned int)hotY);
    }
    if (srcPix != None)
        XFreePixmap(g_xpwnDisplay, srcPix);
    if (maskPix != None && maskPix != srcPix)
        XFreePixmap(g_xpwnDisplay, maskPix);
    return cur;
}

/** @brief Bitmap 形状 → X11 光标（对标 Qt QCursor(QBitmap,QBitmap,hotX,
 *  hotY)）：source=XCursor_bitmap、可选 mask=XCursor_mask（尺寸不一致
 *  时按无遮罩处理），经 XCreateBitmapFromData + XCreatePixmapCursor
 *  生成 1bit 深度光标。失败返回 None（apply 静默）。 */
static Cursor xpwn_cursorAcquireBitmapCursor(const XCursor* cursor)
{
    const XBitmap* src;
    const XBitmap* mask;
    unsigned char* srcBits;
    unsigned char* maskBits = NULL;
    int w = 0, h = 0, mw = 0, mh = 0;
    XPoint hot;
    Cursor cur;
    if (!cursor) return None;
    src = XCursor_bitmap(cursor);
    if (!src) return None;
    srcBits = xpwn_cursorBitmapBits(src, &w, &h);
    if (!srcBits) return None;
    mask = XCursor_mask(cursor);
    if (mask) {
        maskBits = xpwn_cursorBitmapBits(mask, &mw, &mh);
        if (maskBits && (mw != w || mh != h)) {
            /* source/mask 必须同尺寸（X11 约定）：不一致视为无效遮罩，
               按整体不透明处理。 */
            XFree_System(maskBits);
            maskBits = NULL;
        }
    }
    hot = XCursor_hotSpot(cursor);
    cur = xpwn_cursorCreateFromBits(srcBits, maskBits, w, h, hot.x, hot.y);
    XFree_System(srcBits);
    if (maskBits) XFree_System(maskBits);
    return cur;
}

/** @brief Custom 形状（像素图）→ X11 光标（对标 Qt QCursor(QPixmap,hotX,
 *  hotY) 的 X11 位图化落地）：转 ARGB32 后按 1bit 双色模型逐像素判定——
 *  mask 位 = alpha≥128（无 alpha 信息即整体不透明），source 位 = 不透明
 *  且亮度 <128（暗部→黑前景，亮部→白背景）。XCreatePixmapCursor 只承载
 *  双色，全彩光标需 RENDER 扩展（对标 Qt xcb 的 XRenderCreateCursor），
 *  为后续增强。失败返回 None（apply 静默）。 */
static Cursor xpwn_cursorAcquirePixmapCursor(const XCursor* cursor)
{
    const XPixmap* pix;
    XImage img;
    unsigned char* srcBits = NULL;
    unsigned char* maskBits = NULL;
    int w, h, lineBytes, row, col;
    size_t bufBytes;
    XPoint hot;
    Cursor cur = None;
    if (!cursor) return None;
    pix = XCursor_pixmap(cursor);
    if (!pix) return None;
    XImage_init(&img);
    XPixmap_toImage(pix, &img);
    if (!XImage_convertToFormatInPlace(&img, XImageFormat_ARGB32, 0)) {
        XImage_deinit_base(&img);
        return None;
    }
    w = XImage_width(&img);
    h = XImage_height(&img);
    if (w <= 0 || h <= 0) {
        XImage_deinit_base(&img);
        return None;
    }
    lineBytes = (w + 7) / 8;
    bufBytes = (size_t)lineBytes * (size_t)h;
    srcBits = (unsigned char*)XMemory_malloc(bufBytes,
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    maskBits = (unsigned char*)XMemory_malloc(bufBytes,
                                              XCLASS_DEFAULT_MEMORY_TYPE);
    if (srcBits && maskBits) {
        XMemset(srcBits, 0, bufBytes);
        XMemset(maskBits, 0, bufBytes);
        for (row = 0; row < h; ++row) {
            for (col = 0; col < w; ++col) {
                uint32_t p = XImage_pixel(&img, col, row);
                unsigned int alpha = (p >> 24) & 0xffu;
                unsigned int r, g, b, luma;
                if (alpha < 128)
                    continue; /* 透明：mask=0（该位已零初始化）。 */
                maskBits[(size_t)row * lineBytes + (col >> 3)] |=
                    (unsigned char)(1u << (col & 7));
                r = (p >> 16) & 0xffu;
                g = (p >> 8) & 0xffu;
                b = p & 0xffu;
                luma = (r * 77u + g * 151u + b * 28u) >> 8; /* 0..255。 */
                if (luma < 128)
                    srcBits[(size_t)row * lineBytes + (col >> 3)] |=
                        (unsigned char)(1u << (col & 7)); /* 暗部→黑。 */
            }
        }
        hot = XCursor_hotSpot(cursor);
        cur = xpwn_cursorCreateFromBits(srcBits, maskBits, w, h,
                                        hot.x, hot.y);
    }
    XFree_System(srcBits);
    XFree_System(maskBits);
    XImage_deinit_base(&img);
    return cur;
}

static bool xpwn_cursorBackendQueryPos(int* x, int* y)
{
    Window root, child;
    int rootX, rootY, winX, winY;
    unsigned int mask;
    if (!g_xpwnDisplay)
        return false;
    if (!XQueryPointer(g_xpwnDisplay,
                       RootWindow(g_xpwnDisplay, g_xpwnScreenNumber),
                       &root, &child, &rootX, &rootY, &winX, &winY, &mask))
        return false; /* 指针在其他屏幕（Xinerama）等场景。 */
    if (x) *x = rootX;
    if (y) *y = rootY;
    return true;
}

static bool xpwn_cursorBackendWarpPos(int x, int y)
{
    if (!g_xpwnDisplay)
        return false;
    /* 全局（根窗口）坐标定位：src_w=None + dest_w=根窗口。 */
    XWarpPointer(g_xpwnDisplay, None,
                 RootWindow(g_xpwnDisplay, g_xpwnScreenNumber),
                 0, 0, 0, 0, x, y);
    XFlush(g_xpwnDisplay);
    return true;
}

static bool xpwn_cursorBackendApplyWindowCursor(uintptr_t nativeWindowId,
                                                const XCursor* cursor)
{
    Cursor native;
    XCursorShape shape;
    if (!g_xpwnDisplay || nativeWindowId == 0)
        return false;
    /* 无光标对象时按默认箭头恢复（XUndefineCursor 语义的等价简化）。 */
    if (!cursor) {
        XUndefineCursor(g_xpwnDisplay, (Window)nativeWindowId);
        XFlush(g_xpwnDisplay);
        return true;
    }
    shape = XCursor_shape(cursor);
    if (shape == XCursor_Bitmap)
        /* 位图光标（XCursor_create_bitmap）：1bit 双色
           XCreatePixmapCursor 通道，遮罩可选（对标 Qt
           QCursor(QBitmap,QBitmap,hotX,hotY)）。 */
        native = xpwn_cursorAcquireBitmapCursor(cursor);
    else if (shape == XCursor_Custom)
        /* 像素图光标（XCursor_create_pixmap）：ARGB32 采样降为 1bit
           source+mask 后 XCreatePixmapCursor（对标 Qt
           QCursor(QPixmap,hotX,hotY) 的 X11 位图化落地；全彩需 RENDER
           扩展，后续增强）。 */
        native = xpwn_cursorAcquirePixmapCursor(cursor);
    else
        native = xpwn_cursorAcquireShapeCursor(shape);
    if (native == None)
        return false; /* 创建失败静默（位图无效/连接异常等）。 */
    /* 窗口未映射时 XDefineCursor 合法：光标在映射后生效（X11 语义）。 */
    XDefineCursor(g_xpwnDisplay, (Window)nativeWindowId, native);
    XFlush(g_xpwnDisplay);
    if (shape == XCursor_Bitmap || shape == XCursor_Custom) {
        /* 位图通道的 Cursor 即用即毁：XDefineCursor 后窗口持有服务器
           侧引用，XFreeCursor 仅解除资源号关联、存储待无引用后释放
           （Xlib 手册语义；形状字体光标走进程级缓存，不在此释放）。 */
        XFreeCursor(g_xpwnDisplay, native);
    }
    return true;
}

static bool xpwn_cursorBackendClearWindowCursor(uintptr_t nativeWindowId)
{
    if (!g_xpwnDisplay || nativeWindowId == 0)
        return false;
    XUndefineCursor(g_xpwnDisplay, (Window)nativeWindowId);
    XFlush(g_xpwnDisplay);
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

static bool xpwn_ensureConnection(void);
static bool xpwn_screensApplyLogicalDpi(void);

/**
 * @brief      X11 连接 fd 监视线程主体（主循环统一等待桥，见文件头部
 *             g_xpwnWatchXfd 处的方案说明）。
 * @details    阻塞 poll(X11 fd)：可读（键盘/鼠标/Expose/WM 状态回写等
 *             协议数据到达）即经 XAbstractNetIoRing_wakeUp 唤醒 exec
 *             标准主循环的阻塞等待——事件数据本身不在此消费，由被唤醒的
 *             主线程经轮询回调泵空（Xlib 单线程访问约定不破）。
 *             可读性为 level-triggered：主线程尚未泵空期间重 poll 会立即
 *             返回造成空转，故唤醒后先做一次至多 4ms 的限幅 poll——新
 *             数据一到即返回（唤醒延迟不受影响），把重唤醒频率封顶在
 *             250 次/秒；随后回到无限期阻塞。POLLERR/HUP/NVAL（连接
 *             死亡）退出线程，退化回主循环既有心跳节拍。
 * @param      arg 未用（pthread 签名要求）。
 * @return     NULL。
 */
#if XAbstractNetIoRing_ON
static void* xpwn_x11WakeWatch(void* arg)
{
    struct pollfd pfd;
    int r;
    (void)arg;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = g_xpwnWatchXfd;
    pfd.events = POLLIN;
    for (;;) {
        do {
            r = poll(&pfd, 1, -1);
        } while (r < 0 && errno == EINTR);
        if (r <= 0)
            continue; /* 伪唤醒（无 EINTR 之外错误）：重新阻塞。 */
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            break; /* 连接失效：退出监视线程（GUI 事件源已不存在）。 */
        {
            XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
            /* ring 未启用（如 createPlatform 失败）时保持静默：主循环
               本就只剩心跳节拍，唤醒通道缺席属既有行为。 */
            if (ring && XAbstractNetIoRing_isEnabled(ring))
                XAbstractNetIoRing_wakeUp_base(ring);
        }
        /* 限幅再武装（见函数头注释）：至多 4ms，数据保持期间逐次重醒。 */
        do {
            r = poll(&pfd, 1, 4);
        } while (r < 0 && errno == EINTR);
    }
    return NULL;
}
#endif /* XAbstractNetIoRing_ON */

/** @brief 连接建立后惰性启动 X11 fd 监视线程（幂等；失败静默退化）。
 *  @note  网络模块裁剪（XAbstractNetIoRing_ON=0）时 ring 阻塞等待同样
 *         整体缺席（dispatcher 主线程分支条件不成立），唤醒通道无意义，
 *         不起线程。 */
static void xpwn_x11WakeWatchStart(void)
{
#if XAbstractNetIoRing_ON
    if (g_xpwnWatchStarted || g_xpwnDisplay == NULL) return;
    g_xpwnWatchXfd = XConnectionNumber(g_xpwnDisplay);
    if (g_xpwnWatchXfd < 0) return;
    if (pthread_create(&g_xpwnWatchThread, NULL, xpwn_x11WakeWatch, NULL) != 0)
        return; /* 线程创建失败：静默退化回心跳节拍（与修复前一致）。 */
    pthread_detach(g_xpwnWatchThread); /* 进程级常驻，无 join 语义。 */
    g_xpwnWatchStarted = true;
#else
    (void)g_xpwnWatchStarted; /* 裁剪变体：静态保持未用。 */
#endif /* XAbstractNetIoRing_ON */
}

/**
 * @brief      逻辑 DPI 运行期刷新公开入口（对标 Qt 中 xcb 后端对
 *             Xft.dpi 变更的轮询/手工刷新路径）。
 * @details    重新读取 RESOURCE_MANAGER 中的 Xft.dpi 资源并对全部已
 *             登记屏幕差分回填：值变化的屏幕发射 logicalDotsPerInch-
 *             Changed（平均值），未变化的保持静默。xrdb -merge 重载后
 *             由应用择机调用（xcb 无变更推送，Qt 亦无推送、靠轮询或
 *             手工触发）；RRScreenChangeNotify 处理路径亦会顺带重读。
 * @return     连接建立且刷新执行完毕返回 true（无论值是否变化）；连接
 *             失败返回 false。
 */
bool XPlatformNativeWindow_refreshScreenLogicalDpi(void)
{
    if (!xpwn_ensureConnection()) return false;
    xpwn_screensApplyLogicalDpi();
    return true;
}

static bool xpwn_ensureConnection(void)
{
    XVisualInfo vinfo;
    XVisualInfo* fallback = NULL;
    int fallbackDone = 0;
    if (g_xpwnDisplay) return true;
    if (g_xpwnDisplay == NULL && g_xpwnDepth != 0) return false; /* 已失败。 */
    g_xpwnDisplay = XOpenDisplay(NULL);
    if (!g_xpwnDisplay) return false;
    /* 协议错误非致命化（见 xpwn_xlibErrorHandler 注释；须在首个请求前
       安装，Xlib 惯例在连接建立后立即设置）。 */
    XSetErrorHandler(xpwn_xlibErrorHandler);
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
    /* 屏幕默认视觉三元组（只引用不拥有；对标 QXcbScreen 持有的
       root_visual/root_depth 与 X 屏自带 colormap）。 */
    g_xpwnDefaultVisual = DefaultVisual(g_xpwnDisplay, g_xpwnScreenNumber);
    g_xpwnDefaultDepth = DefaultDepth(g_xpwnDisplay, g_xpwnScreenNumber);
    g_xpwnDefaultColormap = DefaultColormap(g_xpwnDisplay, g_xpwnScreenNumber);
    g_xpwnWmDelete = XInternAtom(g_xpwnDisplay, "WM_DELETE_WINDOW", False);
    g_xpwnWmProtocols = XInternAtom(g_xpwnDisplay, "WM_PROTOCOLS", False);
    g_xpwnUtf8String = XInternAtom(g_xpwnDisplay, "UTF8_STRING", False);
    g_xpwnNetWmName = XInternAtom(g_xpwnDisplay, "_NET_WM_NAME", False);
    g_xpwnNetWmState = XInternAtom(g_xpwnDisplay, "_NET_WM_STATE", False);
    g_xpwnNetWmStateAbove = XInternAtom(g_xpwnDisplay,
                                        "_NET_WM_STATE_ABOVE", False);
    g_xpwnNetWmStateBelow = XInternAtom(g_xpwnDisplay,
                                        "_NET_WM_STATE_BELOW", False);
    g_xpwnNetWmStateSkipTaskbar = XInternAtom(g_xpwnDisplay,
                                              "_NET_WM_STATE_SKIP_TASKBAR",
                                              False);
    g_xpwnNetWmStateSkipPager = XInternAtom(g_xpwnDisplay,
                                            "_NET_WM_STATE_SKIP_PAGER",
                                            False);
    g_xpwnMotifWmHints = XInternAtom(g_xpwnDisplay, "_MOTIF_WM_HINTS", False);
    /* 窗口状态/类型原子（对标 QXcbWindow::setWindowState/setWindowType
       的 atom() 初始化；AnyPropertyType 语义下 None 返回值在写入端判空
       跳过，无 WM 的最小会话同样安全）。 */
    g_xpwnNetWmStateMaximizedVert = XInternAtom(g_xpwnDisplay,
                                    "_NET_WM_STATE_MAXIMIZED_VERT", False);
    g_xpwnNetWmStateMaximizedHorz = XInternAtom(g_xpwnDisplay,
                                    "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    g_xpwnNetWmStateFullscreen = XInternAtom(g_xpwnDisplay,
                                             "_NET_WM_STATE_FULLSCREEN", False);
    g_xpwnWmChangeState = XInternAtom(g_xpwnDisplay, "WM_CHANGE_STATE", False);
    g_xpwnWmState = XInternAtom(g_xpwnDisplay, "WM_STATE", False);
    g_xpwnNetWmWindowType = XInternAtom(g_xpwnDisplay,
                                        "_NET_WM_WINDOW_TYPE", False);
    g_xpwnNetWmTypeNormal = XInternAtom(g_xpwnDisplay,
                                        "_NET_WM_WINDOW_TYPE_NORMAL", False);
    g_xpwnNetWmTypeDialog = XInternAtom(g_xpwnDisplay,
                                        "_NET_WM_WINDOW_TYPE_DIALOG", False);
    g_xpwnNetWmTypeUtility = XInternAtom(g_xpwnDisplay,
                                         "_NET_WM_WINDOW_TYPE_UTILITY", False);
    g_xpwnNetWmTypeSplash = XInternAtom(g_xpwnDisplay,
                                        "_NET_WM_WINDOW_TYPE_SPLASH", False);
    g_xpwnNetWmTypeTooltip = XInternAtom(g_xpwnDisplay,
                                         "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    g_xpwnNetWmTypeCombo = XInternAtom(g_xpwnDisplay,
                                       "_NET_WM_WINDOW_TYPE_COMBO", False);
    g_xpwnNetWmTypePopupMenu = XInternAtom(g_xpwnDisplay,
                                           "_NET_WM_WINDOW_TYPE_POPUP_MENU",
                                           False);
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
    g_xpwnClipboard = XInternAtom(g_xpwnDisplay, "CLIPBOARD", False);
    g_xpwnClipProp = XInternAtom(g_xpwnDisplay, "XIN_YUE_CLIP_DATA", False);
    (void)setlocale(LC_CTYPE, "");
    /* XIM_OPEN 协议携带 locale 的语言名，fcitx5 依此为 IC 分配输入
       引擎（"en"/C.UTF-8 → 英文引擎 → 按键透传无法中文）。非 zh
       locale 一律强制 zh_CN.UTF-8（Deepin 中文环境必有该 locale）。 */
    {
        const char* cur = setlocale(LC_CTYPE, NULL);
        if (!cur || strncmp(cur, "zh_CN", 5) != 0) {
            if (setlocale(LC_CTYPE, "zh_CN.UTF-8"))
                (void)XSetLocaleModifiers("@im=fcitx");
        }
    }
#if XPWN_IME_DEBUG
    XPrintf("[ime-dbg] locale=%s XSupportsLocale=%d XMODIFIERS=%s\n",
            setlocale(LC_CTYPE, NULL),
            (int)XSupportsLocale(),
            getenv("XMODIFIERS") ? getenv("XMODIFIERS") : "(unset)");
#endif
    /* 依次尝试常见输入法桥（fcitx/ibus/XIM 默认），环境变量
       XMODIFIERS 优先；XOpenIM 全部失败时中文输入不可用（西文不受
       影响），启动日志给出提示。XMODIFIERS=@im=none 是显式声明
       「不要输入法」：XIM 不再回退试探 fcitx/默认修饰（对标 xterm
       等经典 X 客户端——XMODIFIERS 即输入法选择器），DBus 前端同由
       xpwn_imeWanted() 停用（问题 #32 第五轮收官，详见该函数注释）。 */
    {
        const char* envMods = getenv("XMODIFIERS");
        const char* candidates[3];
        const bool imNone =
            envMods && strcmp(envMods, "@im=none") == 0;
        int ci;
        candidates[0] = (envMods && envMods[0]) ? envMods : "@im=ibus";
        candidates[1] = "@im=fcitx";
        candidates[2] = "";
        for (ci = 0; !imNone && ci < 3 && !g_xpwnInputMethod; ++ci) {
            (void)XSetLocaleModifiers(candidates[ci]);
            g_xpwnInputMethod = XOpenIM(g_xpwnDisplay, NULL, NULL, NULL);
#if XPWN_IME_DEBUG
            XPrintf("[ime-dbg] XSetLocaleModifiers(%s) -> XOpenIM %s\n",
                    candidates[ci],
                    g_xpwnInputMethod ? "OK" : "failed");
#endif
        }
        if (imNone)
            XPrintf("XPlatformNativeWindow: XMODIFIERS=@im=none——XIM 与"
                    " DBus 输入法前端均不挂接（西文按键/字母快捷键直达"
                    "应用；XPWN_IME=fcitx 可显式强开输入法前端）\n");
        else if (!g_xpwnInputMethod)
            XPrintf("XPlatformNativeWindow: XOpenIM 失败——XIM 不可用"
                    "（西文不受影响；中文走 DBus 输入法前端）\n");
    }
    xpwn_imeInit(); /* fcitx5 DBus 输入法（中文主路径）。 */
    /* 光标后端注册：连接可用后 XCursor 的 pos/setPos/窗口光标接口即
       走真实 X11 通道（XQueryPointer/XWarpPointer/XDefineCursor）。 */
    xpwn_cursorBackendInstall();
    /* 主循环统一等待桥：X11 fd 监视线程随连接建立惰性启动（幂等），
       使 exec 标准主循环的 ring 阻塞等待能被 X11 输入即时唤醒（对标
       QEventDispatcherGlib 把 X11 fd 并入主循环统一等待）。 */
    xpwn_x11WakeWatchStart();
    /* 屏幕接入不在这里初始化：连接可能早在 XGuiApplication 构造期间被
       平台集成（字体/主题等）间接建立，此时 GUI 单例尚未发布，
       screenAdded 的登记会丢失；改为首次事件泵时惰性接入（见
       XPlatformNativeWindow_processPendingEvents），彼时应用单例必然
       有效（Qt 中平台屏幕接入同样发生在 QGuiApplication 构造完成之后）。 */
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

/** @brief 把 ARGB32 缓冲的一行/一矩形直拷进 XPutImage 缓冲。
 *  @note   仅 ARGB32 后备缓冲模式使用；RGB16 模式下直拷走
 *          xpwn_copyRect16（本函数无调用点，随 #if 收起避免空转）。 */
#if !XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
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
        XMemcpy(dst + (int64_t)(srect->y + row) * dstBpl + (int64_t)srect->x * 4,
               sbuf + (int64_t)(srect->y + row) * bpl + (int64_t)srect->x * 4,
               (size_t)srect->width * 4u);
    }
}
#endif /* !XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 */

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

#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
/** @brief 把 RGB16（565，2 字节/像素）后备缓冲的一行/一矩形直拷进
 *         depth-16 ZPixmap 上屏缓冲。
 *  @details 源与目标同为 565 布局（R 高 5 位/G 中 6 位/B 低 5 位，
 *           小端 2 字节单元，内存布局与 XPutImage 的 ZPixmap 一致），
 *           故逐行做字节拷贝即可（对标 QXcbBackingStore 对 rgb565
 *           视觉的 memcpy 快路径）。函数形态与 xpwn_copyRect24 对齐：
 *           逐行、入参防护、返回是否拷贝。行跨度按 2 字节/像素换算。 */
static bool xpwn_copyRect16(const XImage* src, const XRect* srect,
                            uint8_t* dst, int dstBpl)
{
    const uint8_t* sbuf;
    int bpl;
    int row;
    if (!src || !srect || !dst || dstBpl <= 0) return false;
    sbuf = XImage_constBits(src);
    bpl = XImage_bytesPerLine(src);
    if (!sbuf || bpl <= 0) return false;
    for (row = 0; row < srect->height; ++row) {
        XMemcpy(dst + (int64_t)(srect->y + row) * dstBpl +
                    (int64_t)srect->x * 2,
                sbuf + (int64_t)(srect->y + row) * bpl +
                    (int64_t)srect->x * 2,
                (size_t)srect->width * 2u);
    }
    return true;
}
#endif /* XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 */

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
    case XK_KP_Space:    return ' ';
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
static XKeyboardModifiers xpwn_translateModifiers(unsigned int state,
                                                  KeySym keysym)
{
    XKeyboardModifiers modifiers = XKeyboardModifier_NoModifier;
    if (state & ShiftMask) modifiers |= XKeyboardModifier_ShiftModifier;
    if (state & ControlMask) modifiers |= XKeyboardModifier_ControlModifier;
    if (state & Mod1Mask) modifiers |= XKeyboardModifier_AltModifier;
    if (state & Mod4Mask) modifiers |= XKeyboardModifier_MetaModifier;
    /* NumLock（Mod2）只对真正的小键盘键表达 KeypadModifier：按 keysym
     * 是否落在 KP 区（XK_KP_Space=0xff80..XK_KP_Equal=0xffbd）判定。
     * 此前按 Mod2Mask 全键表加位——NumLock 开启时主键区
     * Backspace/Delete/方向键全带 Keypad 位，壳与控制器的"无修饰键"
     * 判断（plainMods/matchKey==NoModifier）全部失效，多行编辑
     * Delete 无效的根因（2026-09-19）。鼠标等无 keysym 的路径传
     * NoSymbol，恒不带 Keypad。 */
    if (keysym >= (KeySym)0xff80 && keysym <= (KeySym)0xffbd)
        modifiers |= XKeyboardModifier_KeypadModifier;
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

/* ==================== X11 SelectionRequest serve（单目标 + MULTIPLE 批量）
 * ==================== 对标 QXcbClipboard::handleSelectionRequest：单目标
 * serve 与 ICCCM 2.6.2 MULTIPLE 的逐对分流共用同一实现（Qt 对 MULTIPLE
 * 属性里的每个 (target,property) 对做等价的单目标 serve，最后统一一条
 * SelectionNotify）。两个函数均只写属性、不回 Notify——回发由
 * xpwn_dispatchEvent 的 SelectionRequest 分支统一完成（保证「一次
 * notify」的协议约束只在一个位置落实）。 */

/* 单目标 serve：按 req->target 把转换结果写进传输属性并返回是否成功
 * （served）。req->property 为 None 时按 ICCCM 2.5 约定以 target 名代之。
 * 分支逻辑与此前 SelectionRequest 分支的内联实现逐字节一致，抽出共用：
 * TARGETS/SAVE_TARGETS 报告镜像目标集合（text/plain 条目额外补
 * XA_STRING）、TIMESTAMP 回认领时间戳、数据目标按镜像格式条目命中回数，
 * 大数据（超 xpw_clipIncrChunkLimit 阈值）改走 ICCCM 2.5 INCR 增量传输
 * ——会话表容量由 xpw_clipIncrStart 内部复核（先闲置回收再取槽，表满
 * 返回 false 按协议拒绝）。镜像无效/目标不匹配时不写属性、返回 false。 */
static bool xpw_clipServeTarget(XSelectionRequestEvent* req)
{
    XpwClipOwnerState* st = xpw_clipStateForSelection(req->selection);
    Atom prop = req->property != None ? req->property : req->target;
    bool served = false;
    if (st && st->m_dataValid && st->m_serveWin != None) {
        if (req->target == g_xpwnTargets) {
            /* TARGETS 询问：报告镜像实际持有的格式原子集合（对标
             * QXcbClipboard::handleSelectionRequest 按 QMimeData
             * formats() 报告）；text/plain 额外补 XA_STRING（对标
             * Qt 同时提供 UTF8_STRING 与 STRING 两个文本目标）。
             * TIMESTAMP 可直接请求但不进列表——部分剪贴板管理器
             * 遇未知目标会中止取数。不支持的格式不出现在列表。
             * MULTIPLE 协议目标由 xpw_clipServeMultiple 承接（ICCCM
             * 2.6.2），并按 ICCCM 广播进列表（§8.2 协议补边：请求方
             * 据此可用一次批量转换取齐多格式）；数组容量 +1 护栏
             * 即为此预留。 */
            Atom targets[XPWN_CLIP_MAX_FORMATS * 2 + 1];
            int n = xpw_clipBuildTargetAtoms(st, targets,
                                             (int)(sizeof(targets) / sizeof(targets[0])));
            if (g_xpwnMultiple != None &&
                n < (int)(sizeof(targets) / sizeof(targets[0])))
                targets[n++] = g_xpwnMultiple;
            XChangeProperty(g_xpwnDisplay, req->requestor, prop,
                            XA_ATOM, 32, PropModeReplace,
                            (const unsigned char*)targets, n);
            served = true;
        } else if (g_xpwnSaveTargets != None &&
                   req->target == g_xpwnSaveTargets) {
            /* SAVE_TARGETS 询问（freedesktop 剪贴板管理器协议，对标
             * QXcbClipboard 对剪贴板管理器 SAVE_TARGETS 请求的应答）：
             * Klipper 等管理器接管所有权前先以 SAVE_TARGETS 询问旧
             * owner「愿意保存哪些目标」，旧 owner 以 XA_ATOM 数组回
             * 当前镜像支持的转换目标并 SelectionNotify，管理器随后逐
             * 目标取数暂存。此处复用 TARGETS 的目标集合应答（轻量
             * 落地：不做逐目标延迟序列化，镜像数据本已全量在内存）。 */
            Atom targets[XPWN_CLIP_MAX_FORMATS * 2 + 1];
            int n = xpw_clipBuildTargetAtoms(st, targets,
                                             (int)(sizeof(targets) / sizeof(targets[0])));
            XChangeProperty(g_xpwnDisplay, req->requestor, prop,
                            XA_ATOM, 32, PropModeReplace,
                            (const unsigned char*)targets, n);
            served = true;
        } else if (req->target == g_xpwnTimestamp) {
            /* TIMESTAMP 询问：报告认领所有权的时间戳（按选择区取
             * 各自认领时的时间）。 */
            long t = (long)st->m_timestamp;
            XChangeProperty(g_xpwnDisplay, req->requestor, prop,
                            XA_INTEGER, 32, PropModeReplace,
                            (const unsigned char*)&t, 1);
            served = true;
        } else {
            /* 数据目标：按请求原子匹配镜像格式条目（text/plain 条目
             * 同时响应 UTF8_STRING 与 XA_STRING）；命中即把保存的
             * 字节流按 format=8 原样回（image/png 等二进制不再编码，
             * 对标 Qt 平台层直接透传 QMimeData 保存的字节）。 */
            int fi, hit = -1;
            Atom type = req->target;
            for (fi = 0; fi < st->m_formatCount; ++fi) {
                if (req->target == st->m_formats[fi].m_target) {
                    hit = fi;
                    break;
                }
                if (req->target == XA_STRING &&
                    strncmp(st->m_formats[fi].m_mime, "text/plain",
                            sizeof(st->m_formats[fi].m_mime)) == 0) {
                    hit = fi;
                    type = XA_STRING;
                    break;
                }
            }
            if (hit < 0 && st->m_text &&
                (req->target == g_xpwnUtf8String || req->target == XA_STRING)) {
                /* 兼容旧文本镜像：仅 setText 通道写入时仍可服务。 */
                hit = -2;
                type = (req->target == XA_STRING) ? XA_STRING : g_xpwnUtf8String;
            }
            if (hit >= 0) {
                const unsigned char* bytes = (hit == -2)
                    ? (const unsigned char*)st->m_text
                    : st->m_formats[hit].m_data;
                int len = (hit == -2) ? st->m_textLen
                                      : st->m_formats[hit].m_len;
                if (bytes && len >= 0) {
                    if (len > xpw_clipIncrChunkLimit()) {
                        /* 大数据（超 xpw_clipIncrChunkLimit 阈值）：改走
                         * ICCCM 2.5 INCR 增量传输——写 type=INCR 协议头
                         * 并登记分片会话；SelectionNotify（由调用方统一
                         * 回发）按协议先于首批数据分片，且其 property
                         * 指向传输属性，请求方由协议头得知 INCR（对标
                         * QXcbClipboard::handleSelectionRequest 的 INCR
                         * 分支）。会话表满则按协议拒绝。 */
                        served = xpw_clipIncrStart(req->requestor, prop,
                                                   req->selection, type,
                                                   bytes, len);
                    } else {
                        /* 小数据/空数据：既有单次写入路径逐字节不变
                         * （空文本仍服务零长度属性，保持原语义）。 */
                        XChangeProperty(g_xpwnDisplay, req->requestor,
                                        prop, type, 8, PropModeReplace,
                                        bytes, len);
                        served = true;
                    }
                }
            }
        }
    }
    return served;
}

/** @brief MULTIPLE 属性内 (target,property) 对数组的防御性对数上限
 *  （超出视为畸形请求整体拒绝；正常客户端的批量转换远低于此）。 */
#define XPWN_CLIP_MULTIPLE_MAX_PAIRS 1024

/* MULTIPLE serve（ICCCM 2.6.2，对标 QXcbClipboard::handleSelectionRequest
 * 的 MULTIPLE 分支）：请求方把 (target,property) 原子对数组放在传输属性
 * （type=ATOM、format=32）里请求 MULTIPLE。逐对以 xpw_clipServeTarget
 * 分流 serve（各对结果写入对内自己的 property；对内 property=None 时以
 * target 名代之，与单请求约定一致）；某对失败则把数组内该对的 property
 * 原子改写为 None 作为逐对失败标记（ICCCM 2.6.2；仅当有失败才回写属性，
 * 全成功零额外往返）。target=None 终止遍历（ICCCM：请求方可借此截断
 * 列表）。处理完返回 true，由调用方对同一请求只回一条 SelectionNotify
 * （target/property 回 MULTIPLE 属性本身）。属性读取失败、类型/格式
 * 不符、对数不成偶或超防御上限 → 返回 false，调用方按协议以
 * property=None 拒绝（对标 Qt "Invalid multiple request" 拒绝路径）。 */
static bool xpw_clipServeMultiple(XSelectionRequestEvent* req)
{
    Atom prop = req->property != None ? req->property : req->target;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* data = NULL;
    unsigned long* pairs;
    unsigned long i, pairCount;
    bool modified = false;
    if (XGetWindowProperty(g_xpwnDisplay, req->requestor, prop,
                           0, 0xFFFFFF, False, XA_ATOM,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after, &data) != Success)
        return false;
    if (actual_type != XA_ATOM || actual_format != 32 || !data ||
        nitems < 2 || (nitems & 1ul) != 0ul ||
        nitems / 2 > (unsigned long)XPWN_CLIP_MULTIPLE_MAX_PAIRS) {
        /* 畸形 MULTIPLE 属性：不逐对尝试，整体按协议拒绝。 */
        if (data) xpwn_xFree(data);
        return false;
    }
    /* Xlib 约定：format=32 属性数据按 long 数组返回，元素即 Atom。 */
    pairs = (unsigned long*)data;
    pairCount = nitems / 2;
    for (i = 0; i < pairCount; ++i) {
        XSelectionRequestEvent sub = *req;
        sub.target = (Atom)pairs[i * 2];
        if (sub.target == None)
            break; /* ICCCM 2.6.2：None 目标终止列表（后续对不处理）。 */
        sub.property = (Atom)pairs[i * 2 + 1];
        if (sub.property == None)
            sub.property = sub.target; /* 对内 None：以 target 名代之。 */
        if (!xpw_clipServeTarget(&sub)) {
            pairs[i * 2 + 1] = (unsigned long)None; /* 逐对失败标记。 */
            modified = true;
        }
    }
    if (modified)
        XChangeProperty(g_xpwnDisplay, req->requestor, prop, XA_ATOM, 32,
                        PropModeReplace, (const unsigned char*)pairs,
                        (int)nitems);
    xpwn_xFree(data);
    return true;
}

/* ==================== 事件泵（平台后端提供） ==================== */

/** @brief 单条 XEvent 翻译为窗口事件注入；返回是否注入了事件。 */
#ifdef XINYUE_C_HAS_XI2
/* XI2 触摸事件选择与分派（方案 A 最小接入）。探测一次：XI 2.2 起支
   持触摸事件；对主设备（XIAllMasterDevices）选择 Touch 三类掩码后，
   服务器对该窗口不再投递触摸模拟出的核心指针事件（去重自动化），
   真实鼠标事件仍走核心协议——两路并存不双投。 */
static int g_xpwnXi2Opcode = -2; /* -2 未探测，-1 不可用，>=0 opcode */

static void xpwn_xi2SelectTouch(Window win)
{
    static int probed = 0;
    int major = 2, minor = 2;
    int event_base = 0, error_base = 0;
    unsigned char bits[4] = { 0, 0, 0, 0 };
    XGuiXIEventMask mask;
    if (!probed)
    {
        probed = 1;
        if (XQueryExtension(g_xpwnDisplay, "XInputExtension",
                            &g_xpwnXi2Opcode, &event_base, &error_base) &&
            XIQueryVersion(g_xpwnDisplay, &major, &minor) == Success)
        {
            fprintf(stderr, "xpwn: XI2 touch enabled (v%d.%d)\n",
                    major, minor);
        }
        else
        {
            fprintf(stderr, "xpwn: XI2 unavailable (ext=%d ver=%d.%d)\n",
                    g_xpwnXi2Opcode, major, minor);
            g_xpwnXi2Opcode = -1; /* 无 XI2：回退核心协议输入。 */
        }
    }
    if (g_xpwnXi2Opcode < 0) return;
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(bits);
    mask.mask = bits;
    bits[XI_TouchBegin / 8] |= (unsigned char)(1u << (XI_TouchBegin % 8));
    bits[XI_TouchUpdate / 8] |= (unsigned char)(1u << (XI_TouchUpdate % 8));
    bits[XI_TouchEnd / 8] |= (unsigned char)(1u << (XI_TouchEnd % 8));
    XISelectEvents(g_xpwnDisplay, win, &mask, 1);
}

/** @brief XI2 触摸事件转译：Touch 三类 → handleTouchEvent_ex（主点，
 *         pointCount=1；tracking id 留待多点方案 B）。非触摸 XI2 事
 *         件按已消费忽略。 */
static bool xpwn_dispatchXi2TouchEvent(const X11_XEvent* ev)
{
    XGenericEventCookie* cookie;
    XGuiXIDeviceEvent* dev;
    XWNPendingEntry* entry;
    XEventType type;
    XPoint local;
    XPoint global;
    if (ev->type != GenericEvent || g_xpwnXi2Opcode < 0 ||
        ev->xcookie.extension != g_xpwnXi2Opcode)
        return false;
    cookie = (XGenericEventCookie*)&ev->xcookie;
    if (cookie->evtype != XI_TouchBegin &&
        cookie->evtype != XI_TouchUpdate &&
        cookie->evtype != XI_TouchEnd)
        return true; /* 其他 XI2 事件：消费但不处理。 */
    if (!XGetEventData(g_xpwnDisplay, cookie)) return true;
    dev = (XGuiXIDeviceEvent*)cookie->data;
    entry = xpwn_findByNativeWindow(dev->event);
    if (entry && entry->m_window)
    {
        switch (cookie->evtype)
        {
        case XI_TouchBegin: type = XEVENT_TYPE_TOUCH_BEGIN; break;
        case XI_TouchUpdate: type = XEVENT_TYPE_TOUCH_UPDATE; break;
        default: type = XEVENT_TYPE_TOUCH_END; break;
        }
        local.x = (short)dev->event_x;
        local.y = (short)dev->event_y;
        global.x = (short)dev->root_x;
        global.y = (short)dev->root_y;
        XWindowSystemInterface_handleTouchEvent_ex(
            entry->m_window, type, local, &global, 1,
            (uint32_t)(dev->time & 0xffffffffu));
    }
    XFreeEventData(g_xpwnDisplay, cookie);
    return true;
}
#endif /* XINYUE_C_HAS_XI2 */

/** @brief 判定 IME 提交串是否为「直映键字符」（问题 #3/#40 防双插入口径）。
 *  @details 对标 Qt 平台层文本分工：非组合的直映字符键以按键事件交付
 *           （文本随 QKeyEvent 走，qxcbkeyboard.cpp handleKeyEvent:
 *           :877 lookupString 取串 → :884-885 组装 QKeyEvent →
 *           :914-916 handleExtendedKeyEvent 交付），inputMethodEvent
 *           提交串只承载 IME 组合产物（qinputmethod.cpp commitString
 *           语义）。XGui 侧直映 ASCII 的文本插入由控件 keyPress 路径
 *           承担（XLineControl_processKeyEvent 尾段 unknown→
 *           xlc_isAcceptableInput→XLineControl_insert，文本由
 *           xlc_keyToText 按键值推导；XTextControl xtc_keyPressEvent
 *           同有 0x20-0x7E 插入分支）——若「提交串照旧 + 再补发可插入
 *           的 KEY_PRESS」则同一字符双写（任务书明令禁止）；且
 *           XKeyEvent 无文本/标记字段（XEvent.h:327-339 契约头冻结），
 *           快捷键层（XWidget_dispatchKeyEvent 先于控件派发调
 *           XShortcut_match）之后不存在可挂「仅快捷键消费」标记的派发
 *           级。故直映键按 Qt 分工「只走按键事件」：不提交、不吞，
 *           落回 keysym→KEY_PRESS/KEY_RELEASE 自然产出，插入/快捷键/
 *           按钮激活同源单写。
 *           判定（须全部满足）：单字节 UTF-8（bytes==1，IME 真组合
 *           ——中文/全角/死键——均为多字节，天然不命中，提交路径
 *           行为不变）；可打印 ASCII（[0x20,0x7E]）；拉丁字母纳入
 *           直映集（问题 #32 收官，第四轮：控件层文本推导已按 Shift
 *           派生大小写——XLineControl.c xlc_keyToText、XTextControl.c
 *           xtc_keyPressEvent 可打印分支；对标 Qt
 *           qxcbkeyboard.cpp handleKeyEvent:865-866 字母恒按键事件且
 *           文本并行交付、qinputcontrol.cpp:21-60
 *           isAcceptableInput 消费 event->text()——按键路径不再把
 *           't' 写成 'T'）。唯 CapsLock（LockMask）例外：锁存态字母
 *           留提交通道携带真字符——XKeyboardModifiers 无 Lock 位可
 *           承载（XEvent.h:129-137 契约冻结），控件层无法从键值/
 *           修饰位复原锁存大小写（Qt 侧由 xkb lookupString 原生
 *           承载，qxcbkeyboard.cpp:866）。无 Shift（Shift+数字/
 *           符号的列 0 keysym 是未移位码位，按键路径会写错字符，
 *           留提交通道携带真字符）。Ctrl 已由外层 P1 守卫排除。
 *           Tab（问题 #40/41①，第三轮）：0x09 在可打印区之外，但
 *           同为键盘直映产物而非 IME 组合输出——XIM 对无组合的 Tab
 *           回 XLookupChars 单字节 "\t"（有文本无 keysym，故上方
 *           ≥0xff00 功能键防护不拦），落入提交通道后 KEY_PRESS 被
 *           吞（复扫铁证：文本控件聚焦期间 Tab 仅 keyRelease，
 *           40_demo.log/41c_demo.log；非文本控件聚焦时 Tab 成对）。
 *           对标 Qt：Tab 恒为按键事件——qxkbcommon.cpp:52-53
 *           XKB_KEY_Tab→Key_Tab、XKB_KEY_ISO_Left_Tab→Key_Backtab，
 *           qxcbkeyboard.cpp handleKeyEvent 全部键（含 Tab）经
 *           :866 lookupString 携带文本组装 QKeyEvent 交付，绝无
 *           「Tab 转提交串」通道。故 Tab 纳入直映集：不提交、不吞，
 *           落回 keysym→XKey_Tab/XKey_Backtab 自然产出。置于 Shift
 *           守卫之前（Shift+Tab 的列 0 keysym 是 ISO_Left_Tab/Tab
 *           功能键而非未移位码位，按键路径不会写错字符，无双重插入
 *           之虞）；消费侧闭环：单行编辑 xlc_keyToText 只认
 *           [0x20,0x7E]（XLineControl.c:3325-3363）永不插 Tab，
 *           多行编辑 xtc_keyPressEvent 可打印分支同区间
 *           （XTextControl.c:2352-2397）——两处均 ignore 后由
 *           XWidget event() 的 Tab/Backtab 焦点遍历分支消费
 *           （XWidget.c:2844-2855，对标 qwidget.cpp
 *           focusNextPrevChild），Shift+Tab 反向遍历经
 *           xpwn_translateKey 的 XK_ISO_Left_Tab→XKey_Backtab 同样
 *           可达。Enter(0xff0d)/Esc(0xff1b)/方向键(0xff51-0xff54)
 *           keysym 均 ≥0xff00：XLookupBoth 时由上方功能键防护放行
 *           （40_demo.log Up/Down 成对实证），行为不变。 */
static bool xpwn_imeCommitIsDirectKey(const char* committed, int bytes,
                                      unsigned int state)
{
    char ch;
    if (!committed || bytes != 1) return false;
    ch = committed[0];
    if (ch == 0x09) return true; /* Tab/Shift+Tab：恒为按键事件（见上）。 */
    if (ch < 0x20 || ch > 0x7e) return false;
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))
        /* 拉丁字母纳入直映集（问题 #32 收官）：恒为按键事件，大小写由
           控件层按 Shift 派生（keyToText Shift 位推导），快捷键层按归一
           大写键值精确匹配；CapsLock 锁存态（LockMask）例外留提交通道
           携带真字符。验收夹具已跟新契约（ac_type 大写字母携带 Shift）。
           注：本分支只决定「有 XIM 提交串时的分流」；真机字母被吞的
           上游根因在 fcitx5 DBus 前端无视 XMODIFIERS=@im=none 照常
           ProcessKeyEvent 消费——已由 xpwn_imeWanted() 根修（第五轮
           复扫 lane3 R4「Shift+X 得 x」实为 fcitx 中文态回投小写，非
           本直映路径缺陷；xpwn_translateModifiers 对 ShiftMask 本就
           原样携带修饰位）。 */
        return (state & LockMask) == 0;
    if (state & ShiftMask) return false;
    return true;
}

static bool xpwn_dispatchEvent(const X11_XEvent* ev)
{
    /* XI2 触摸事件（GenericEvent cookie）：先于核心事件分派。 */
#ifdef XINYUE_C_HAS_XI2
    if (ev->type == GenericEvent && g_xpwnXi2Opcode >= 0 &&
        ev->xcookie.extension == g_xpwnXi2Opcode)
        return xpwn_dispatchXi2TouchEvent(ev);
#endif /* XINYUE_C_HAS_XI2 */
    XWNPendingEntry* entry;
    XEventType type;
    XFocusEvent* focusEvent;
    bool accepted;
    bool delivered = false;
    if (!ev) return false;
    if (xpwn_handleDragSelectionRequest(ev)) return true;
    switch (ev->type) {
    case MapNotify:
        /* 对齐 QXcbWindow：窗口映射完成后补做挂起的激活请求。 */
        entry = xpwn_findByNativeWindow(ev->xmap.window);
        if (entry && entry->m_window && entry->m_deferredActivation) {
            entry->m_deferredActivation = false;
            XSetInputFocus(g_xpwnDisplay, entry->m_win,
                           RevertToParent, CurrentTime);
            delivered = true;
        }
        /* 对标 Qt xcb：新映射的顶层窗口置顶。无 WM 环境下 show() 后
         * 的对话框若不主动 raise，将停在主窗口之下被永久遮盖——实测
         * 弹窗「透明、啥都没有」（输入/文件/颜色对话框全部命中）。
         * XMapWindow 本应置顶，但此前挂起的激活路径未覆盖无挂起场
         * 景，这里无条件补一次置顶（幂等）。 */
        if (entry && entry->m_win) {
            XRaiseWindow(g_xpwnDisplay, entry->m_win);
            XFlush(g_xpwnDisplay);
        }
        /* 映射完成后补一次全窗 expose：show()->首绘->flush 可能早于
         * 服务器完成映射（map 请求异步），首帧 XPutImage 落在未完成
         * 映射的窗口上内容丢失，且此后无脏区不再重绘——表现为顶层
         * 对话框「透明、啥都没有」（用户实测弹窗透明根因）。映射完
         * 成后再请求一次全窗重绘，保证首帧上屏。 */
        if (entry && entry->m_window) {
            XRegion mapRegion;
            XRect mapRect;
            XRegion_init(&mapRegion);
            mapRect.x = 0;
            mapRect.y = 0;
            mapRect.width = XWindow_width(entry->m_window);
            mapRect.height = XWindow_height(entry->m_window);
            XRegion_addRect(&mapRegion, &mapRect);
            XWindowSystemInterface_handleExposeEvent(entry->m_window,
                                                     &mapRegion);
            XRegion_deinit(&mapRegion);
            delivered = true;
        }
        break;
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
            if (entry->m_inputContext) {
                XSetICFocus(entry->m_inputContext);
                {
                    XPoint spot;
                    XPoint_init(&spot, 8, 8);
                    (void)XSetICValues(entry->m_inputContext,
                                       XNSpotLocation, &spot, NULL);
                }
            }
            xpwn_imeFocus(ev->xfocus.window, true);
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
            xpwn_imeFocus(ev->xfocus.window, false);
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
            /* 标准 XIM 流程：按键先经输入法过滤器（组合键/输入法
               热键由 IME 内部消费，返回 True 时事件不再下发）。 */
            if (entry->m_inputContext &&
                XFilterEvent((X11_XEvent*)ev, entry->m_window)) {
#if XPWN_IME_DEBUG
                XPrintf("[ime-dbg] XFilterEvent consumed key\n");
#endif
                delivered = true;
                break;
            }
            if (ev->type == KeyPress && entry->m_inputContext) {
                Status status;
                KeySym imeKeysym = NoSymbol;
                char committed[1024];
                int bytes = Xutf8LookupString(entry->m_inputContext,
                    (X11_XKeyEvent*)&ev->xkey, committed,
                    (int)sizeof(committed) - 1, &imeKeysym, &status);
                if (bytes > 0) {
                    committed[bytes] = '\0';
                    /* Chars=IME 组合确认（中文），直接提交。Both 时按
                       keysym 判断：可打印区（<0xff00）信文本（输入法
                       直接提交的西文/数字），功能键区（>=0xff00，
                       Backspace/方向等——fcitx 透传时会错给空格文本）
                       不信文本、交下方 keysym -> KEY_PRESS 正确处理。 */
                    /* 功能键防护（必须）：Backspace/方向等 fcitx 透传
                       时会错给空白文本——不信其文本，交 KEY_PRESS。 */
                    if (status == XLookupBoth && imeKeysym >= (KeySym)0xff00) {
#if XPWN_IME_DEBUG
                        XPrintf("[ime-dbg] fnkey text='%s' keysym=0x%lx"
                                "（走 KEY_PRESS）\n", committed,
                                (unsigned long)imeKeysym);
#endif
                    }
                    else if (status == XLookupChars ||
                             (status == XLookupBoth &&
                              imeKeysym < (KeySym)0xff00)) {
                        /* Ctrl 守卫（问题 #42/#32）：XIM IC 存在时
                           Xutf8LookupString 对 Ctrl+可打印 Latin 键回的
                           是控制字节"文本"（Ctrl+Z→\x1A），落入本提交
                           分支后 KEY_PRESS 永不产出（应用层 Ctrl+A/Z 快
                           捷键全失效的根因）。对标 Qt 平台层：Ctrl+字母
                           是带 Control 修饰位的按键而非输入法提交文本
                           （QXcbKeyboard::handleKeyEvent，qxcbkeyboard.cpp:836
                           —— qtcode/modifiers 常在，xkb 文本对 Ctrl 组合
                           为空，绝不经 IME 提交吞键）。判定用
                           ev->xkey.state & ControlMask，与
                           xpwn_translateModifiers 同一口径：xkey.state 中
                           Control 恒以 ControlMask 呈现，物理修饰位即便
                           被 remap 到 Mod* 也不影响；keysym 取
                           XLookupBoth 的 imeKeysym、否则回查第一列。
                           命中守卫不提交、不 break，落回下方
                           keysym→KEY_PRESS 正常产出（修饰位齐全）；
                           无 Ctrl 的文本提交路径（中文组合/西文直输）
                           行为不变。 */
                        KeySym guardKeysym =
                            (status == XLookupBoth)
                                ? imeKeysym
                                : XLookupKeysym((X11_XKeyEvent*)&ev->xkey, 0);
                        if ((ev->xkey.state & ControlMask) == 0 ||
                            guardKeysym < (KeySym)0x20 ||
                            guardKeysym > (KeySym)0xff) {
                            /* 直映键不提交、不吞（问题 #3/#40）：Space/
                               数字/符号的单字节 ASCII 提交是键盘直映产物
                               而非 IME 组合输出，按 Qt 分工改由按键事件
                               交付（xpwn_imeCommitIsDirectKey 注释）——
                               此处不调 handleInputMethodEvent、不 break，
                               落回下方 keysym→KEY_PRESS/KEY_RELEASE 自然
                               产出（焦点链：快捷键层先行消费，否则焦点
                               控件 keyPress 路径单次插入）。第一轮实测
                               「Space 仅 keyRelease、按钮键盘激活失效」
                               （台账 #3，XAbstractButton.c:908-945 依赖
                               Press/Release 成对）的根因即本分支吞键；
                               KeyRelease 本就不进本分支（直达下方正常
                               路径），Press 补齐后成对。中文组合/全角
                               （多字节）与 Shift+数字/符号（列 0 为未
                               移位码位）照旧提交；Shift+字母随 #32
                               收官改走按键路径（大小写由控件层按 Shift
                               派生，xpwn_imeCommitIsDirectKey 注释），
                               CapsLock 锁存态字母亦照旧提交。 */
                            if (!xpwn_imeCommitIsDirectKey(
                                    committed, bytes, ev->xkey.state)) {
#if XPWN_IME_DEBUG
                                XPrintf("[ime-dbg] commit text='%s'\n",
                                        committed);
#endif
                                (void)XWindowSystemInterface_handleInputMethodEvent(
                                    entry->m_window, "", committed, 0, 0, -1,
                                    -1);
                                if (ev->type == KeyPress) {
                                    delivered = true;
                                    break;
                                }
                            }
#if XPWN_IME_DEBUG
                            else {
                                XPrintf("[ime-dbg] direct key '%c' falls"
                                        " through to KEY_PRESS\n",
                                        committed[0]);
                            }
#endif
                        }
                    }
                }
            }
            xpwn_imePump(); /* 抽取 fcitx 的 CommitString 等信号。 */
            /* fcitx5 DBus 按键拦截：消费则该键不产出 XKey（中文
               组合在 fcitx 内部，最终以 CommitString 信号回投）。 */
            if (ev->type == KeyPress &&
                xpwn_imeProcessKey((int)XKeycodeToKeysym(
                                       g_xpwnDisplay, ev->xkey.keycode, 0),
                                   ev->xkey.keycode,
                                   (unsigned)ev->xkey.state, false)) {
                delivered = true;
                break;
            }
            keysym = XLookupKeysym((X11_XKeyEvent*)&ev->xkey, 0);
            /* NumLock 开启时小键盘键的数字 keysym 可能落在第二列；
               若第一列给出 KP 方向键且 NumLock 修饰有效，再取第二列。 */
            if ((ev->xkey.state & Mod2Mask) != 0 &&
                keysym >= (KeySym)0xff80 && keysym <= (KeySym)0xffb9) {
                KeySym alt = XLookupKeysym((X11_XKeyEvent*)&ev->xkey, 1);
                if (alt != NoSymbol)
                    keysym = alt;
            }
            /* 大写归一（问题 #32）：拉丁字母键（0x41-0x5A/0x61-0x7A）
               统一以大写 keysym 上报。XKey 枚举即大写口径（XEvent.h
               XKey_T=0x54），而 XShortcut_match 按键值精确相等
               （XShortcut.c：sc->m_key != key 即跳过），列 0 的小写
               keysym 使字母快捷键经真实按键永不命中（台账 it32：按
               t/Shift+T 均产出 0x74，注册的 XKey_T=0x54 永不匹配）。
               Shift 状态保留在修饰位（xpwn_translateModifiers 原样
               携带，键值不随大小写），对标 Qt：Qt::Key 字母键值不分
               大小写（qxcbkeyboard.cpp handleKeyEvent 的 keysymToQtKey
               产物 Key_T 恒 0x54），字符大小写由 text()/Shift 修饰位
               表达。上方 Ctrl 守卫的区间判定 [0x20,0xff] 与字母大小写
               无关，守卫逻辑保持不变；Ctrl+字母（如 Ctrl+Z→0x5A）经
               此归一后与 XKey_Z 注册值一致，快捷键层可命中。 */
            if (keysym >= (KeySym)0x61 && keysym <= (KeySym)0x7a)
                keysym -= (KeySym)0x20;
            key = xpwn_translateKey(keysym);
            modifiers = xpwn_translateModifiers(ev->xkey.state, keysym);
            if (ev->type == KeyPress) {
                /* 自动重复识别：同一键码已在按下状态时，X11 转入重复节奏
                   （xkey 无显式标志，用按下表去重）。 */
                autoRepeat = entry->m_keyPressed[ev->xkey.keycode & 0xff];
                entry->m_keyPressed[ev->xkey.keycode & 0xff] = true;
                XWindowSystemInterface_handleKeyEvent_ex(
                    entry->m_window, XEVENT_TYPE_KEY_PRESS, key, modifiers,
                    autoRepeat, (uint32_t)ev->xkey.keycode,
                    (uint32_t)ev->xkey.time);
            } else {
                entry->m_keyPressed[ev->xkey.keycode & 0xff] = false;
                XWindowSystemInterface_handleKeyEvent_ex(
                    entry->m_window, XEVENT_TYPE_KEY_RELEASE, key, modifiers,
                    false, (uint32_t)ev->xkey.keycode,
                    (uint32_t)ev->xkey.time);
            }
            delivered = true;
        }
        break;
    }
    case ButtonPress:
        entry = xpwn_findByNativeWindow(ev->xbutton.window);
        if (entry && entry->m_window) {
            XPoint position;
            XPoint globalPosition;
            XMouseButton button;
            XMouseButton buttons;
            XKeyboardModifiers modifiers;
            position.x = ev->xbutton.x;
            position.y = ev->xbutton.y;
            /* 根坐标与事件时间（ms）随完整负载通道透传（对标 Qt 全局
               位置/时间戳语义）。 */
            globalPosition.x = ev->xbutton.x_root;
            globalPosition.y = ev->xbutton.y_root;
            modifiers = xpwn_translateModifiers(ev->xbutton.state, NoSymbol);
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
                        XWindowSystemInterface_handleMouseEvent_ex(
                            entry->m_window, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK,
                            button, buttons | button, modifiers, position,
                            &globalPosition, (uint32_t)ev->xbutton.time);
                    } else {
                        XWindowSystemInterface_handleMouseEvent_ex(
                            entry->m_window, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                            button, buttons | button, modifiers, position,
                            &globalPosition, (uint32_t)ev->xbutton.time);
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
            XPoint globalPosition;
            XMouseButton button;
            XKeyboardModifiers modifiers;
            position.x = ev->xbutton.x;
            position.y = ev->xbutton.y;
            globalPosition.x = ev->xbutton.x_root;
            globalPosition.y = ev->xbutton.y_root;
            modifiers = xpwn_translateModifiers(ev->xbutton.state, NoSymbol);
            button = xpwn_translateButton(ev->xbutton.button);
            if (button != XMouseButton_NoButton) {
                /* 释放时 state 已不含本键，按下集合直接采用状态位。 */
                XMouseButton buttons = xpwn_translateButtonMask(ev->xbutton.state);
                XWindowSystemInterface_handleMouseEvent_ex(
                    entry->m_window, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                    button, buttons, modifiers, position,
                    &globalPosition, (uint32_t)ev->xbutton.time);
            }
            delivered = true;
        }
        break;
    case MotionNotify:
        entry = xpwn_findByNativeWindow(ev->xmotion.window);
        if (entry && entry->m_window) {
            XPoint position;
            XPoint globalPosition;
            XMouseButton buttons;
            XKeyboardModifiers modifiers;
            position.x = ev->xmotion.x;
            position.y = ev->xmotion.y;
            globalPosition.x = ev->xmotion.x_root;
            globalPosition.y = ev->xmotion.y_root;
            buttons = xpwn_translateButtonMask(ev->xmotion.state);
            modifiers = xpwn_translateModifiers(ev->xmotion.state, NoSymbol);
            XWindowSystemInterface_handleMouseEvent_ex(
                entry->m_window, XEVENT_TYPE_MOUSE_MOVE,
                XMouseButton_NoButton, buttons, modifiers, position,
                &globalPosition, (uint32_t)ev->xmotion.time);
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
                        XMemcpy(offered, raw, offeredCount * sizeof(Atom));
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
    case SelectionRequest:
    {
        /* 对标 Qt QXcbClipboard::handleSelectionRequest：其他应用请求
         * 我们认领的剪贴板内容时，提供文本并发送 SelectionNotify。
         * 无论能否满足都必须回复（property=None 表示拒绝），否则
         * 请求方会一直阻塞等待 Notify。按事件里的 selection 原子分流：
         * CLIPBOARD/PRIMARY 各取对应镜像（对标按 mode 分派的
         * QXcbClipboard::clipboardSource）。 */
        XSelectionRequestEvent* req = (XSelectionRequestEvent*)
                                      &ev->xselectionrequest;
        XSelectionEvent notify;
        Atom prop = req->property != None ? req->property : req->target;
        bool served = false;
        if (g_xpwnMultiple != None && req->target == g_xpwnMultiple) {
            /* ICCCM 2.6.2 MULTIPLE 批量转换（收口此前「批次二十三评估
             * 维持 P2 缓做」项）：传输属性内为 (target,property) 原子对
             * 数组，逐对分流单目标 serve（复用 xpw_clipServeTarget 的
             * TARGETS/TIMESTAMP/数据/INCR 路径，INCR 会话表容量由
             * xpw_clipIncrStart 复核）；处理完只回本分支末尾这一条
             * SelectionNotify（对标 Qt 对 MULTIPLE 逐对 serve 后统一
             * notify）。整体畸形（属性缺失/类型格式不符/对数不成偶）
             * 才以 property=None 拒绝；镜像无效时逐对失败、数组内标记
             * None 后仍回 MULTIPLE 属性，请求方可逐对得知结果。 */
            served = xpw_clipServeMultiple(req);
        } else {
            /* 单目标请求：TARGETS/SAVE_TARGETS/TIMESTAMP/数据目标共用
             * 逻辑抽为 xpw_clipServeTarget（分支与既有内联实现逐字节
             * 一致），成功与否仍由本分支统一回发 SelectionNotify。 */
            served = xpw_clipServeTarget(req);
        }
        memset(&notify, 0, sizeof(notify));
        notify.type = SelectionNotify;
        notify.display = g_xpwnDisplay;
        notify.requestor = req->requestor;
        notify.selection = req->selection;
        notify.target = req->target;
        notify.time = req->time;
        notify.property = served ? prop : None;
        XSendEvent(g_xpwnDisplay, req->requestor, False, 0,
                   (X11_XEvent*)&notify);
        XFlush(g_xpwnDisplay);
        delivered = true;
        break;
    }
    case SelectionClear:
    {
#if XCLIPBOARD_ON
        /* 其他应用认领了选择区：清除该选择区的本地镜像，并经后端契约
         * 的 selectionRevoked 反向通知上层（对标 QXcbClipboard 的
         * handleSelectionClearRequest：清 ownerData 并向上发射变化）。 */
        Atom sel = ev->xselectionclear.selection;
        XpwClipOwnerState* st = xpw_clipStateForSelection(sel);
        if (st) {
            int mode = (sel == XA_PRIMARY) ? (int)XClipboardMode_Selection
                                           : (int)XClipboardMode_Clipboard;
            /* 所有权易主：该选择区在途的 INCR 分片会话一并中止（快照数据
               已失效，继续传输只会误导旧请求方）。 */
            xpw_clipIncrAbortForSelection(sel);
            xpw_clipClearMirror(st);
            xpw_clipNotifyRevoked(mode);
            delivered = true;
        }
#endif /* XCLIPBOARD_ON */
        break;
    }
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
    case PropertyNotify:
        /* 剪贴板 INCR 分片会话推进：requestor 每删一次传输属性（GetProperty
         * delete=True 取走上一片）即写下一片（ICCCM 2.5，对标 Qt
         * QXcbClipboard::handlePropertyNotifyEvent 转发事务）。此处监听的
         * 是外部请求者窗口（serve 会话）与本进程请求窗口（读收集期间不会
         * 走到主泵，见 xpw_clipCollectIncr 自有循环）。不算框架窗口事件。 */
        if (xpw_clipHandleIncrDelete(&ev->xproperty))
            xpw_clipIncrSweepIdle();
        /* WM 状态回写观察（对标 QXcbWindow::handlePropertyNotifyEvent 对
         * _NET_WM_STATE/WM_STATE 的处理）：setWindowState 请求最大化/
         * 全屏/最小化后，WM 执行结果经属性回写体现——增删 _NET_WM_STATE
         * 原子或改 WM_STATE 图标态。此处换算成 XWindowState 上报公共层
         * （XWindow_reportWindowStateChanged 发射 windowStateChanged 且
         * 不回写平台层，无注入回环），showMaximized 等实测状态闭环。 */
        else if (ev->xproperty.state == PropertyNewValue &&
                 (ev->xproperty.atom == g_xpwnNetWmState ||
                  ev->xproperty.atom == g_xpwnWmState)) {
            entry = xpwn_findByNativeWindow(ev->xproperty.window);
            if (entry && entry->m_window)
                XWindow_reportWindowStateChanged(
                    entry->m_window,
                    xpwn_queryWmWindowState(g_xpwnDisplay, entry->m_win));
        }
        break;
    default:
        break;
    }
    (void)type;
    return delivered;
}

/* ==================== X11 INCR 会话管理（serve 方向核心） ==================== */

/* 单调时钟毫秒（会话闲置扫描用）。 */
static unsigned long xpw_clipMonotonicMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000ul +
           (unsigned long)ts.tv_nsec / 1000000ul;
}

/* INCR 触发阈值/单分片上限（字节）。口径：
 * - XExtendedMaxRequestSize/XMaxRequestSize 返回以 4 字节字为单位的服务器
 *   最大请求长度（Qt qclipboard_x11.cpp 同以 *4 折算字节）；BIG-REQUESTS
 *   不可用（XExtendedMaxRequestSize 返回 0）时回退 XMaxRequestSize。
 * - 折算字节后除 4：ICCCM 2.5 要求单分片应明显小于 maximum-request-size，
 *   为协议头/属性名等开销预留余量。
 * - 再与 XPWN_CLIP_INCR_CHUNK_CAP（262144，常见服务器单属性上限）取小，
 *   避免现代服务器扩展上限很大时单分片直接顶到单属性极限。
 * 同一数值兼任「数据超过该长度即改走 INCR」的触发阈值：小于它的单次
 * XChangeProperty 在有 BIG-REQUESTS 的服务器上必然一次承载成功。 */
static int xpw_clipIncrChunkLimit(void)
{
    long extWords;
    long maxBytes;
    long limit;
    if (!g_xpwnDisplay) return XPWN_CLIP_INCR_CHUNK_CAP;
    extWords = XExtendedMaxRequestSize(g_xpwnDisplay);
    maxBytes = (extWords > 0 ? extWords
                             : (long)XMaxRequestSize(g_xpwnDisplay)) * 4;
    limit = maxBytes / 4;
    if (limit > (long)XPWN_CLIP_INCR_CHUNK_CAP)
        limit = XPWN_CLIP_INCR_CHUNK_CAP;
    if (limit < 1024) limit = 1024; /* 极小 max-request 服务器的保底分片。 */
    return (int)limit;
}

/* 中止会话：解除对 requestor 窗口的属性监听并释放快照（对标
 * QXcbClipboardTransaction 结束时恢复 NO_EVENT 事件掩码并自毁）。 */
static void xpw_clipIncrAbort(XpwClipIncrSession* s)
{
    if (!s || !s->m_active) return;
    s->m_active = false;
    if (g_xpwnDisplay && s->m_requestor != None)
        XSelectInput(g_xpwnDisplay, s->m_requestor, NoEventMask);
    if (s->m_data) { XFree_System(s->m_data); s->m_data = NULL; }
    s->m_requestor = None;
    s->m_property = None;
    s->m_selection = None;
    s->m_type = None;
    s->m_total = 0;
    s->m_offset = 0;
    s->m_lastMs = 0;
}

static void xpw_clipIncrAbortForSelection(Atom selection)
{
    int i;
    for (i = 0; i < XPWN_CLIP_INCR_MAX_SESSIONS; ++i) {
        XpwClipIncrSession* s = &g_xpwnClipIncrSessions[i];
        if (s->m_active && s->m_selection == selection)
            xpw_clipIncrAbort(s);
    }
}

static void xpw_clipIncrAbortForWindow(Window requestor)
{
    int i;
    for (i = 0; i < XPWN_CLIP_INCR_MAX_SESSIONS; ++i) {
        XpwClipIncrSession* s = &g_xpwnClipIncrSessions[i];
        if (s->m_active && s->m_requestor == requestor)
            xpw_clipIncrAbort(s);
    }
}

/* 闲置会话回收（对端死亡后 PropertyDelete 永不再来）：在新会话建立与
 * 事件泵空闲时扫描，快照与槽位占用随 30s 闲置自动归还（Qt
 * QXcbClipboardTransaction abort timer 的无定时器等价）。 */
static void xpw_clipIncrSweepIdle(void)
{
    unsigned long now = xpw_clipMonotonicMs();
    int i;
    for (i = 0; i < XPWN_CLIP_INCR_MAX_SESSIONS; ++i) {
        XpwClipIncrSession* s = &g_xpwnClipIncrSessions[i];
        if (s->m_active && now - s->m_lastMs > XPWN_CLIP_INCR_SESSION_IDLE_MS)
            xpw_clipIncrAbort(s);
    }
}

/* 写下一分片。由 requestor 窗口传输属性上的 PropertyNotify
 * state=PropertyDelete 驱动（请求方每次用 GetProperty delete=True 取走
 * 分片，删除事件即「已取走」信号）。分片一律 PropModeReplace（ICCCM 2.5：
 * 请求方每次整读该属性；对标 QXcbClipboardTransaction 的 REPLACE）。
 * 最后一片被取走后再收一次 Delete，此时写零长度属性作为终结信号——
 * ICCCM 2.5：请求方读到零长度分片即知传输完成。 */
static void xpw_clipIncrWriteNext(XpwClipIncrSession* s)
{
    int remain = s->m_total - s->m_offset;
    if (remain > 0) {
        int limit = xpw_clipIncrChunkLimit();
        int chunk = remain < limit ? remain : limit;
        XChangeProperty(g_xpwnDisplay, s->m_requestor, s->m_property,
                        s->m_type, 8, PropModeReplace,
                        s->m_data + s->m_offset, chunk);
        s->m_offset += chunk;
        s->m_lastMs = xpw_clipMonotonicMs();
        return;
    }
    XChangeProperty(g_xpwnDisplay, s->m_requestor, s->m_property,
                    s->m_type, 8, PropModeReplace,
                    (const unsigned char*)"", 0);
    xpw_clipIncrAbort(s);
}

/* 事件泵挂接：INCR 会话推进（仅响应在会话 requestor 窗口/属性上的删除
 * 通知）。返回是否命中任一会话。 */
static bool xpw_clipHandleIncrDelete(const XPropertyEvent* pev)
{
    int i;
    bool handled = false;
    if (pev->state != PropertyDelete) return false;
    for (i = 0; i < XPWN_CLIP_INCR_MAX_SESSIONS; ++i) {
        XpwClipIncrSession* s = &g_xpwnClipIncrSessions[i];
        if (!s->m_active || s->m_requestor != pev->window ||
            s->m_property != pev->atom)
            continue;
        xpw_clipIncrWriteNext(s);
        handled = true;
    }
    return handled;
}

/* 建立 INCR 会话并发协议头（SelectionRequest 数据目标超阈值时调用）。
 * 序列对标 ICCCM 2.5 与 QXcbClipboardTransaction 构造：
 *  1) 对 requestor 窗口挂 PropertyChangeMask（X11 允许监听其他客户端
 *     窗口的属性变化；GTK gdkselectionoutputstream-x11.c 同法）；
 *  2) 写 type=INCR、format=32、值=总字节长度的协议头属性；
 *  3) SelectionNotify 由调用方先行回发（notify.property 指向传输属性；
 *     ICCCM 2.5 顺序为「INCR 头 → notify → 等 requestor 删头 → 逐片」），
 *     此后本会话由 PropertyNotify Delete 逐片驱动。
 * 表满返回 false，调用方按协议以 property=None 拒绝该请求。 */
static bool xpw_clipIncrStart(Window requestor, Atom property, Atom selection,
                              Atom type, const unsigned char* data, int len)
{
    XpwClipIncrSession* s = NULL;
    long header;
    int i;
    if (requestor == None || property == None || !data || len <= 0)
        return false;
    xpw_clipIncrSweepIdle();
    /* 同一 requestor+property 的旧会话（对端异常重入）先行让位。 */
    for (i = 0; i < XPWN_CLIP_INCR_MAX_SESSIONS; ++i) {
        XpwClipIncrSession* cur = &g_xpwnClipIncrSessions[i];
        if (cur->m_active && cur->m_requestor == requestor &&
            cur->m_property == property)
            xpw_clipIncrAbort(cur);
        if (!cur->m_active && !s)
            s = cur;
    }
    if (!s) return false; /* 会话表满：礼貌拒绝。 */
    s->m_data = (unsigned char*)XMemory_malloc((size_t)len,
                                               XCLASS_DEFAULT_MEMORY_TYPE);
    if (!s->m_data) return false;
    memcpy(s->m_data, data, (size_t)len);
    s->m_requestor = requestor;
    s->m_property = property;
    s->m_selection = selection;
    s->m_type = type;
    s->m_total = len;
    s->m_offset = 0;
    s->m_lastMs = xpw_clipMonotonicMs();
    s->m_active = true;
    XSelectInput(g_xpwnDisplay, requestor, PropertyChangeMask);
    header = (long)len; /* format=32 按 long 数组传递（Xlib 统一转换）。 */
    XChangeProperty(g_xpwnDisplay, requestor, property, g_xpwnIncr, 32,
                    PropModeReplace, (const unsigned char*)&header, 1);
    return true;
}

/* ==================== 可用性与生命周期（平台后端提供） ==================== */

#if XCLIPBOARD_ON
/* ==================== X11 CLIPBOARD 后端（Selection 协议） ====================
 * 对标 QXcbClipboard：应用复制时认领 CLIPBOARD 选择区所有权并存储
 * 文本；其他应用请求时经 SelectionRequest/SelectionNotify 协议提供。
 * 粘贴时如果其他应用持有所有权，经 XConvertSelection 请求后等待
 * SelectionNotify 到来再读取。 */

static bool xpw_clipEnsureAtoms(void)
{
    if (!g_xpwnDisplay) return false;
    /* 各原子独立判空：g_xpwnClipboard/g_xpwnClipProp 可能已被连接
     * 初始化提前 intern（若只以 clipboard 判空会跳过其余原子）。 */
    if (g_xpwnClipboard == None)
        g_xpwnClipboard = XInternAtom(g_xpwnDisplay, "CLIPBOARD", False);
    if (g_xpwnClipProp == None)
        g_xpwnClipProp = XInternAtom(g_xpwnDisplay, "XIN_YUE_CLIP_DATA", False);
    if (g_xpwnTargets == None)
        g_xpwnTargets = XInternAtom(g_xpwnDisplay, "TARGETS", False);
    if (g_xpwnTimestamp == None)
        g_xpwnTimestamp = XInternAtom(g_xpwnDisplay, "TIMESTAMP", False);
    if (g_xpwnUtf8String == None)
        g_xpwnUtf8String = XInternAtom(g_xpwnDisplay, "UTF8_STRING", False);
    if (g_xpwnTextHtml == None)
        g_xpwnTextHtml = XInternAtom(g_xpwnDisplay, "text/html", False);
    if (g_xpwnIncr == None)
        g_xpwnIncr = XInternAtom(g_xpwnDisplay, "INCR", False);
    if (g_xpwnSaveTargets == None)
        g_xpwnSaveTargets = XInternAtom(g_xpwnDisplay, "SAVE_TARGETS", False);
    if (g_xpwnMultiple == None)
        g_xpwnMultiple = XInternAtom(g_xpwnDisplay, "MULTIPLE", False);
    return g_xpwnClipboard != None;
}

/* 确保存在专用剪贴板窗口（1×1、永不映射）。选择区所有权挂在独立
 * 小窗口上，与业务窗口生命周期解耦，PropertyChangeMask 仅用于
 * 服务器时间戳获取。对标 QXcbClipboard::m_window。 */
static bool xpw_clipEnsureOwnerWindow(void)
{
    if (g_xpwnClipWin != None) return true;
    if (!xpwn_ensureConnection() || !xpw_clipEnsureAtoms()) return false;
    g_xpwnClipWin = XCreateSimpleWindow(g_xpwnDisplay,
                                        DefaultRootWindow(g_xpwnDisplay),
                                        -1, -1, 1, 1, 0, 0, 0);
    if (g_xpwnClipWin == None) return false;
    XSelectInput(g_xpwnDisplay, g_xpwnClipWin, PropertyChangeMask);
    return true;
}

/* 确保存在专用请求者窗口（1×1、永不映射、PropertyChangeMask）。读方向
 * 的传输属性与 SelectionNotify 都挂在本窗口：INCR 增量收集需要接收该
 * 窗口的 PropertyNotify(NewValue) 事件，且不污染业务窗口的既有事件
 * 掩码（窗口不存在时也可发起跨进程读取）。对标 QXcbClipboard::
 * m_requestor。 */
static bool xpw_clipEnsureRequestorWindow(void)
{
    if (g_xpwnClipReqWin != None) return true;
    if (!xpw_clipEnsureOwnerWindow()) return false;
    g_xpwnClipReqWin = XCreateSimpleWindow(g_xpwnDisplay,
                                           DefaultRootWindow(g_xpwnDisplay),
                                           -1, -1, 1, 1, 0, 0, 0);
    if (g_xpwnClipReqWin == None) return false;
    XSelectInput(g_xpwnDisplay, g_xpwnClipReqWin, PropertyChangeMask);
    return true;
}

/* XIfEvent 谓词：专用剪贴板窗口上的 PropertyNotify。 */
static Bool xpwn_isPropertyNotifyOnClipWin(Display* display, X11_XEvent* event,
                                           XPointer arg)
{
    (void)display; (void)arg;
    return event->type == PropertyNotify &&
           event->xproperty.window == g_xpwnClipWin;
}

/* 获取 X 服务器当前时间戳：对专用窗口做一次零长度属性变更，
 * 从随后的 PropertyNotify 事件中读取（Qt 同样经事件时间戳取真值，
 * 避免使用无歧义的 CurrentTime）。 */
static Time xpw_clipServerTimestamp(void)
{
    X11_XEvent ev;
    if (!xpw_clipEnsureOwnerWindow()) return CurrentTime;
    XChangeProperty(g_xpwnDisplay, g_xpwnClipWin, g_xpwnClipProp,
                    XA_STRING, 8, PropModeReplace, (const unsigned char*)"", 0);
    XFlush(g_xpwnDisplay);
    /* XIfEvent 会把不匹配事件按原顺序放回队列，不干扰主事件泵。 */
    XIfEvent(g_xpwnDisplay, &ev, xpwn_isPropertyNotifyOnClipWin, NULL);
    return ev.xproperty.time;
}

/* ICCCM 2.5 请求方增量收集循环（对标 Qt QXcbClipboard::getSelection 的
 * INCR 分支）。前置：xpw_clipWaitNotifyRaw 已用 GetProperty(delete=True)
 * 取走并删除 type=INCR 的协议头——该删除即 ICCCM 的「开始」信号，所有者
 * 由此写第一分片。循环等待本窗口传输属性上的 PropertyNotify
 * state=PropertyNewValue，读分片（delete=True：既取走数据又生成驱动
 * 所有者下一片的删除通知），读到零长度分片即完成（ICCCM 2.5：所有者以
 * 零长度属性收尾；请求方自身 delete=True 读取产生的 PropertyDelete 不可
 * 当作终结信号）。g_xpwnClipIncrTimeoutMs 整体超时兜底。totalLen 为
 * 协议头给出的总长（仅作下界与初始容量参考）。 */
static unsigned char* xpw_clipCollectIncr(Window req_win, Atom prop,
                                          unsigned long totalLen, int* outLen,
                                          bool* timeout)
{
    unsigned char* result;
    int have = 0;
    int cap;
    unsigned long startMs = xpw_clipMonotonicMs();
    *timeout = false;
    *outLen = 0;
    cap = (totalLen > 0 && totalLen < (unsigned long)XPWN_CLIP_INCR_MAX_TOTAL)
              ? (int)totalLen + 1 : 65536;
    result = (unsigned char*)XMemory_malloc((size_t)cap,
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!result) return NULL;
    for (;;) {
        bool gotNewValue = false;
        while (XPending(g_xpwnDisplay) > 0) {
            X11_XEvent event;
            XNextEvent(g_xpwnDisplay, &event);
            if (event.type == PropertyNotify &&
                event.xproperty.window == req_win &&
                event.xproperty.atom == prop &&
                event.xproperty.state == PropertyNewValue) {
                gotNewValue = true;
                break;
            }
            /* 非目标事件交回框架分派（并行的 INCR serve 会话因此持续
               推进，见 xpwn_dispatchEvent 的 PropertyNotify 分支）。 */
            xpwn_dispatchEvent(&event);
        }
        if (!gotNewValue) {
            if (xpw_clipMonotonicMs() - startMs >
                    (unsigned long)g_xpwnClipIncrTimeoutMs) {
                *timeout = true;
                break;
            }
            usleep(50000); /* 50ms */
            continue;
        }
        {
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            unsigned char* data = NULL;
            if (XGetWindowProperty(g_xpwnDisplay, req_win, prop,
                                   0, 0xFFFFFF, True, AnyPropertyType,
                                   &actual_type, &actual_format,
                                   &nitems, &bytes_after, &data) != Success)
                break; /* 服务器侧异常：按失败收尾。 */
            if (nitems > 0 && data) {
                unsigned long elemBytes = (actual_format == 32)
                    ? sizeof(long) : (unsigned long)(actual_format / 8);
                unsigned long chunk = nitems * elemBytes;
                if ((unsigned long)have + chunk >
                    (unsigned long)XPWN_CLIP_INCR_MAX_TOTAL) {
                    xpwn_xFree(data);
                    break; /* 防御异常所有者。 */
                }
                if ((unsigned long)have + chunk > (unsigned long)cap) {
                    unsigned long newCap = (unsigned long)cap * 2;
                    unsigned char* grown;
                    while (newCap < (unsigned long)have + chunk)
                        newCap *= 2;
                    grown = (unsigned char*)XRealloc_System(result,
                                                            (size_t)newCap);
                    if (!grown) { xpwn_xFree(data); break; }
                    result = grown;
                    cap = (int)newCap;
                }
                memcpy(result + have, data, chunk);
                have += (int)chunk;
            }
            /* Xlib 自行分配的缓冲必须用真实 XFree 释放。 */
            xpwn_xFree(data);
            if (nitems == 0) {
                /* 零长度分片：传输完成（本次 delete=True 读取同时删掉
                   零长度属性，ICCCM 2.5 的收尾握手闭合）。 */
                *outLen = have;
                return result;
            }
        }
    }
    /* 超时/失败清场：删除残留属性（向所有者发最后的删除信号，令其在途
       会话得以推进/收敛），释放半成品。 */
    XDeleteProperty(g_xpwnDisplay, req_win, prop);
    XFree_System(result);
    return NULL;
}

/* 等待 SelectionNotify 并读取数据属性（一次目标转换尝试，二进制安全：
 * 返回拥有方原始数据，*outLen 为元素个数——format=8 字节流时即字节
 * 数，format=32 时为元素数；供 TARGETS/多格式读取复用）。
 * 返回值：成功时为拥有方数据（调用方释放，不补 NUL——image/png 等
 * 二进制可能含 0）；refused=true 表示所有者明确拒绝（property=None）；
 * timeout=true 表示等待超时。属性实际类型为 INCR 时透明进入 ICCCM 2.5
 * 增量收集（xpw_clipCollectIncr），小数据路径逐字节保持原行为。 */
static unsigned char* xpw_clipWaitNotifyRaw(Window req_win, Atom prop,
                                            Atom selection, int* outLen,
                                            bool* refused, bool* timeout)
{
    X11_XEvent event;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* data = NULL;
    unsigned char* result = NULL;
    int i;
    *refused = false;
    *timeout = false;
    *outLen = 0;
    /* 事件泵：等 SelectionNotify（最长 20 × 50ms = 1s） */
    for (i = 0; i < 20; ++i) {
        while (XPending(g_xpwnDisplay) > 0) {
            XNextEvent(g_xpwnDisplay, &event);
            if (event.type == SelectionNotify &&
                event.xselection.selection == selection) {
                if (event.xselection.property == None) {
                    *refused = true;
                    return NULL;
                }
                goto got_notify;
            }
            /* 非剪贴板事件交回框架分派（可能内嵌 serving 往返） */
            xpwn_dispatchEvent(&event);
        }
        usleep(50000); /* 50ms */
    }
    *timeout = true;
    return NULL;
got_notify:
    /* 从窗口属性读数据（最后一个参数 Delete=True 读后即清）。 */
    if (XGetWindowProperty(g_xpwnDisplay, req_win, prop,
                           0, 0xFFFFFF, True, AnyPropertyType,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after, &data) != Success)
        return NULL;
    if (g_xpwnIncr != None && actual_type == g_xpwnIncr) {
        /* ICCCM 2.5 增量传输：首属性 type=INCR、format=32、值=总字节
           长度（下界）。上面的 Delete=True 读取同时删除了协议头属性——
           即请求方「删除属性以开始传输」的协议动作，所有者由此写第一
           分片。进入增量收集循环拼装完整数据（对标 Qt QXcbClipboard::
           getSelection 的 INCR 分支）。 */
        unsigned long totalLen =
            (data && nitems > 0) ? (unsigned long)*(long*)data : 0;
        xpwn_xFree(data);
        return xpw_clipCollectIncr(req_win, prop, totalLen, outLen, timeout);
    }
    if (data && nitems > 0) {
        /* 拷贝字节数按 Xlib 返回缓冲的元素宽度换算：format=8 字节流时
         * 即 nitems 字节；format=32 时 Xlib 把 32 位线上数据转成 long
         * 数组返回，每元素 sizeof(long) 字节（TARGETS 原子表按 Atom/
         * unsigned long 解析必须整体拷贝，截短拷贝会把下个元素的线上
         * 字节拼进高位读到垃圾原子——对标 Xlib 手册 long 数组惯例）。
         * *outLen 始终为元素个数。 */
        unsigned long elemBytes = (actual_format == 32)
            ? sizeof(long) : (unsigned long)(actual_format / 8);
        unsigned long copyBytes = nitems * elemBytes;
        result = (unsigned char*)XMemory_malloc(copyBytes,
                                                XCLASS_DEFAULT_MEMORY_TYPE);
        if (result) {
            XMemcpy(result, data, copyBytes);
            *outLen = (int)nitems;
        }
    }
    /* Xlib 自行分配的缓冲必须用真实 XFree 释放（此处宏已被还原）。 */
    xpwn_xFree(data);
    return result;
}

/* 等待 SelectionNotify 并读取文本（旧 text 通道：在原始字节之上补
 * NUL 便于按 C 串使用）。 */
static char* xpw_clipWaitNotify(Window req_win, Atom prop, Atom selection,
                                bool* refused, bool* timeout)
{
    unsigned char* raw;
    int rawLen = 0;
    char* result;
    raw = xpw_clipWaitNotifyRaw(req_win, prop, selection, &rawLen,
                                refused, timeout);
    if (!raw) return NULL;
    result = (char*)XMemory_malloc((size_t)rawLen + 1,
                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (result) {
        XMemcpy(result, raw, (size_t)rawLen);
        result[rawLen] = '\0';
    }
    XFree_System(raw);
    return result;
}

/* 读取指定 X11 选择区（CLIPBOARD/PRIMARY）当前所有者的文本
 * （跨进程粘贴核心路径）。优先 UTF8_STRING，被拒绝时回退 XA_STRING；
 * 本框架自己持有该选择区所有权时直接返回本地镜像，避免协议往返。 */
static char* xpw_clipReadSelection(Atom selection)
{
    XpwClipOwnerState* st = xpw_clipStateForSelection(selection);
    Window req_win, owner;
    Atom utf8, prop;
    char* result;
    bool refused, timeout;
    if (!st || !xpwn_ensureConnection() || !xpw_clipEnsureAtoms()) return NULL;
    owner = XGetSelectionOwner(g_xpwnDisplay, selection);
    if (owner == None) return NULL; /* 选择区为空，快速返回不阻塞 */
    if (owner == st->m_serveWin) {
        /* 我们自己持有所有权：直接返回本地镜像。 */
        if (!st->m_text || !st->m_dataValid) return NULL;
        result = (char*)XMemory_malloc((size_t)st->m_textLen + 1,
                                       XCLASS_DEFAULT_MEMORY_TYPE);
        if (result) {
            XMemcpy(result, st->m_text, (size_t)st->m_textLen);
            result[st->m_textLen] = '\0';
        }
        return result;
    }
    /* 专用请求窗口承载传输属性与 SelectionNotify（不再借用业务窗口，
       空窗口应用亦可跨进程读取；对标 QXcbClipboard::m_requestor）。 */
    if (!xpw_clipEnsureRequestorWindow()) return NULL;
    req_win = g_xpwnClipReqWin;
    if (req_win == None) return NULL;
    utf8 = XInternAtom(g_xpwnDisplay, "UTF8_STRING", False);
    prop = g_xpwnClipProp;
    /* 先请求 UTF8_STRING；所有者不支持时回退 XA_STRING 重试。 */
    XConvertSelection(g_xpwnDisplay, selection, utf8, prop,
                      req_win, CurrentTime);
    XFlush(g_xpwnDisplay);
    result = xpw_clipWaitNotify(req_win, prop, selection, &refused, &timeout);
    if (!result && refused) {
        XConvertSelection(g_xpwnDisplay, selection, XA_STRING, prop,
                          req_win, CurrentTime);
        XFlush(g_xpwnDisplay);
        result = xpw_clipWaitNotify(req_win, prop, selection,
                                    &refused, &timeout);
    }
    return result;
}

/* ==================== X11 mime 多格式协商（P0-3） ====================
 * 对标 QXcbClipboard：QMimeData 各格式 ↔ X11 TARGETS 原子双向映射，
 * setMimeData 时多格式并存镜像，SelectionRequest 按目标原子回数；
 * 读方向先 TARGETS 枚举再按需 XConvertSelection。（请求者窗口统一用
 * 专用请求窗口 xpw_clipEnsureRequestorWindow，见其注释。） */

/* mime 格式名 → X11 目标原子（对标 QXcbClipboard 的 mime/target 映射：
 * text/plain 服务为 UTF8_STRING；其余格式以 MIME 名作原子名，与
 * Qt/Chromium 等主流实现对外提供的 text/html、image/png 原子一致）。 */
static Atom xpw_clipTargetForFormat(const char* mime)
{
    if (!mime || !g_xpwnDisplay) return None;
    if (strcmp(mime, "text/plain") == 0)
        return g_xpwnUtf8String;
    return XInternAtom(g_xpwnDisplay, mime, False);
}

/* 写/更新一个格式条目（多格式并存镜像；text/plain 同步既有 text 通道
 * 镜像，使 setText 与 setMimeData 两条写入路径互通，对标 Qt 文本与
 * mime 数据同源）。 */
static bool xpw_clipStoreFormat(XpwClipOwnerState* st, const char* mime,
                                const unsigned char* data, int len)
{
    Atom target;
    int i, idx = -1;
    unsigned char* buf;
    if (!st || !mime || !data || len < 0 || !g_xpwnDisplay) return false;
    target = xpw_clipTargetForFormat(mime);
    if (target == None) return false;
    for (i = 0; i < st->m_formatCount; ++i) {
        if (strncmp(st->m_formats[i].m_mime, mime,
                    sizeof(st->m_formats[i].m_mime)) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (st->m_formatCount >= XPWN_CLIP_MAX_FORMATS) return false;
        idx = st->m_formatCount++;
    }
    buf = (unsigned char*)XRealloc_System(st->m_formats[idx].m_data,
                                          (size_t)len + 1);
    if (!buf) return false;
    XMemcpy(buf, data, (size_t)len);
    buf[len] = '\0'; /* 文本格式可按 C 串使用；二进制以 m_len 为准。 */
    st->m_formats[idx].m_data = buf;
    st->m_formats[idx].m_len = len;
    st->m_formats[idx].m_target = target;
    strncpy(st->m_formats[idx].m_mime, mime,
            sizeof(st->m_formats[idx].m_mime) - 1);
    st->m_formats[idx].m_mime[sizeof(st->m_formats[idx].m_mime) - 1] = '\0';
    st->m_dataValid = true;
    if (strcmp(mime, "text/plain") == 0) {
        /* 互通：text/plain 写入同步 text 通道镜像（本进程内粘贴回环
         * 与旧 text 快速路径仍走 m_text）。 */
        char* t = (char*)XRealloc_System(st->m_text, (size_t)len + 1);
        if (t) {
            XMemcpy(t, data, (size_t)len);
            t[len] = '\0';
            st->m_text = t;
            st->m_textLen = len;
        }
    }
    return true;
}

/* 认领选择区所有权（专用剪贴板窗口 + 真实服务器时间戳；setText 与
 * setMimeData 两条写入路径共用，对标 QXcbClipboard 认领逻辑）。 */
static bool xpw_clipClaimOwnership(XpwClipOwnerState* st, Atom selection)
{
    if (!xpw_clipEnsureOwnerWindow()) return false;
    st->m_timestamp = xpw_clipServerTimestamp();
    XSetSelectionOwner(g_xpwnDisplay, selection,
                       g_xpwnClipWin, st->m_timestamp);
    st->m_serveWin = g_xpwnClipWin;
    XFlush(g_xpwnDisplay);
    return true;
}

/* 外部所有者 TARGETS 枚举：请求 TARGETS 后把原子名映射为 mime 格式名
 * （对标 QXcbClipboard::formats 的系统剪贴板路径）。已知代理目标
 * （UTF8_STRING/STRING/TEXT/COMPOUND_TEXT）归一为 "text/plain"；
 * TARGETS/TIMESTAMP/MULTIPLE/SAVE_TARGETS/INCR 等协议目标跳过；
 * 其余原子（text/html、image/png 等）按原子名原样作为 mime 名。
 * image/png 原样透传，解码在 XClipboard_image 经 XImageCodec 完成。 */
static int xpw_clipQueryExternalFormats(Atom selection,
                                        char outFormats[][64], int max)
{
    Window reqWin;
    Atom prop;
    bool refused, timeout;
    unsigned char* raw;
    unsigned long i, n;
    int len = 0, count = 0;
    bool havePlain = false;
    if (!xpw_clipEnsureRequestorWindow()) return 0;
    reqWin = g_xpwnClipReqWin;
    prop = g_xpwnClipProp;
    XConvertSelection(g_xpwnDisplay, selection, g_xpwnTargets, prop,
                      reqWin, CurrentTime);
    XFlush(g_xpwnDisplay);
    raw = xpw_clipWaitNotifyRaw(reqWin, prop, selection, &len,
                                &refused, &timeout);
    if (!raw || len <= 0) {
        if (raw) XFree_System(raw);
        return 0;
    }
    /* 注意：rawLen 是 XGetWindowProperty 的 nitems（元素个数），TARGETS
     * 固定为 XA_ATOM/32 → 元素即 Atom，无需再除以 sizeof(Atom)。 */
    n = (unsigned long)len;
    for (i = 0; i < n && count < max; ++i) {
        Atom atom = ((Atom*)raw)[i];
        char* name;
        const char* mapped = NULL;
        if (atom == None) continue;
        name = XGetAtomName(g_xpwnDisplay, atom);
        if (!name) continue;
        if (strcmp(name, "UTF8_STRING") == 0 ||
            strcmp(name, "STRING") == 0 ||
            strcmp(name, "TEXT") == 0 ||
            strcmp(name, "COMPOUND_TEXT") == 0) {
            if (!havePlain) { mapped = "text/plain"; havePlain = true; }
        } else if (strcmp(name, "TARGETS") == 0 ||
                   strcmp(name, "TIMESTAMP") == 0 ||
                   strcmp(name, "MULTIPLE") == 0 ||
                   strcmp(name, "SAVE_TARGETS") == 0 ||
                   strcmp(name, "INCR") == 0) {
            mapped = NULL; /* 协议目标不作为数据格式暴露。 */
        } else {
            mapped = name;
        }
        if (mapped) {
            strncpy(outFormats[count], mapped, 63);
            outFormats[count][63] = '\0';
            ++count;
        }
        /* Xlib 分配的原子名必须用真实 XFree 释放。 */
        xpwn_xFree(name);
    }
    XFree_System(raw);
    return count;
}

/* 后端 formats 回调：本进程所有→镜像条目；外部所有→TARGETS 枚举。 */
static int xpw_clipBackendFormats(void* ud, int mode,
                                  char outFormats[][64], int max)
{
    Atom selection;
    XpwClipOwnerState* st;
    Window owner;
    int i, count = 0;
    (void)ud;
    selection = xpw_clipAtomForMode(mode);
    if (selection == None || !outFormats || max <= 0 ||
        !xpwn_ensureConnection() || !xpw_clipEnsureAtoms())
        return 0;
    st = xpw_clipStateForMode(mode);
    owner = XGetSelectionOwner(g_xpwnDisplay, selection);
    if (owner == None) return 0; /* 选择区为空，快速返回不阻塞。 */
    if (owner == st->m_serveWin) {
        for (i = 0; i < st->m_formatCount && count < max; ++i) {
            if (!st->m_formats[i].m_data || st->m_formats[i].m_target == None)
                continue;
            strncpy(outFormats[count], st->m_formats[i].m_mime,
                    XCLIPBOARD_FORMAT_NAME_MAX - 1);
            outFormats[count][XCLIPBOARD_FORMAT_NAME_MAX - 1] = '\0';
            ++count;
        }
        return count;
    }
    return xpw_clipQueryExternalFormats(selection, outFormats, max);
}

/* 后端 mimeData 回调：借用语义——*data 指向镜像条目（本进程所有）或
 * 接收缓冲（外部读取，数据在下次后端调用前有效），免拷贝；对标 Qt
 * 平台 mimeData 直接借用 QMimeData。image/png 等图像格式按原子名取回
 * 原始字节，PNG 解码在 XClipboard_image 经 XImageCodec 完成。 */
static bool xpw_clipBackendMimeData(void* ud, int mode, const char* format,
                                    const unsigned char** data, int* len)
{
    Atom selection;
    XpwClipOwnerState* st;
    Window owner;
    int i;
    (void)ud;
    if (!data || !len || !format) return false;
    *data = NULL;
    *len = 0;
    selection = xpw_clipAtomForMode(mode);
    if (selection == None || !xpwn_ensureConnection() || !xpw_clipEnsureAtoms())
        return false;
    st = xpw_clipStateForMode(mode);
    owner = XGetSelectionOwner(g_xpwnDisplay, selection);
    if (owner == None) return false;
    if (owner == st->m_serveWin) {
        /* 本进程所有：直接借用本地镜像字节。 */
        int hit = -1;
        for (i = 0; i < st->m_formatCount; ++i) {
            if (strcmp(st->m_formats[i].m_mime, format) == 0) {
                hit = i;
                break;
            }
        }
        if (hit >= 0 && st->m_formats[hit].m_data) {
            *data = st->m_formats[hit].m_data;
            *len = st->m_formats[hit].m_len;
            return true;
        }
        if (strcmp(format, "text/plain") == 0 && st->m_text) {
            /* 无 text/plain 条目时回退旧 text 通道镜像（互通）。 */
            *data = (const unsigned char*)st->m_text;
            *len = st->m_textLen;
            return true;
        }
        return false;
    }
    /* 外部所有者：按目标原子按需读取（text/plain 复用既有 UTF8_STRING
     * →XA_STRING 回退路径；其余格式以 mime 名作原子直接请求）。 */
    {
        Window reqWin;
        Atom prop = g_xpwnClipProp;
        bool refused, timeout;
        unsigned char* raw = NULL;
        int rawLen = 0;
        if (!xpw_clipEnsureRequestorWindow()) return false;
        reqWin = g_xpwnClipReqWin;
        if (strcmp(format, "text/plain") == 0) {
            char* text = xpw_clipReadSelection(selection);
            if (!text) return false;
            rawLen = (int)strlen(text);
            raw = (unsigned char*)text; /* 接收缓冲接管（同一分配器）。 */
        } else {
            Atom target = xpw_clipTargetForFormat(format);
            if (target == None) return false;
            XConvertSelection(g_xpwnDisplay, selection, target, prop,
                              reqWin, CurrentTime);
            XFlush(g_xpwnDisplay);
            raw = xpw_clipWaitNotifyRaw(reqWin, prop, selection, &rawLen,
                                        &refused, &timeout);
            if (!raw) return false;
        }
        /* 存入接收缓冲（借用语义的数据落点），旧的先释放。 */
        if (st->m_recv) XFree_System(st->m_recv);
        st->m_recv = raw;
        st->m_recvLen = rawLen;
        *data = st->m_recv;
        *len = st->m_recvLen;
        return true;
    }
}

/* 后端 setMimeData 回调：写镜像 + 认领所有权（对标 QXcbClipboard::
 * setMimeData 逐格式登记并认领对应选择区）。 */
static bool xpw_clipBackendSetMimeData(void* ud, int mode, const char* format,
                                       const unsigned char* data, int len)
{
    XpwClipOwnerState* st;
    Atom selection;
    (void)ud;
    selection = xpw_clipAtomForMode(mode);
    if (selection == None || !format || !data || len < 0 ||
        !xpwn_ensureConnection() || !xpw_clipEnsureAtoms())
        return false;
    st = xpw_clipStateForMode(mode);
    if (!xpw_clipStoreFormat(st, format, data, len))
        return false;
    xpw_clipClaimOwnership(st, selection);
    return true;
}

static bool xpw_clipBackendSetText(void* ud, int mode, const char* text)
{
    XpwClipOwnerState* st;
    Atom selection;
    int len;
    (void)ud;
    /* 按模式分流：Clipboard→CLIPBOARD、Selection→PRIMARY（对标
     * QXcbClipboard::setMimeData 经 atomForMode 取目标选择区）；
     * 不支持的模式（FindBuffer）不落系统选择区。 */
    selection = xpw_clipAtomForMode(mode);
    if (selection == None || !text ||
        !xpwn_ensureConnection() || !xpw_clipEnsureAtoms())
        return false;
    st = xpw_clipStateForMode(mode);
    len = (int)strlen(text);
    {
        char* updated = (char*)XRealloc_System(st->m_text, (size_t)len + 1);
        if (!updated) return false;
        st->m_text = updated;
        XMemcpy(st->m_text, text, (size_t)len + 1);
        st->m_textLen = len;
        st->m_dataValid = true;
    }
    /* 认领对应选择区所有权：挂在专用剪贴板窗口上并用真实服务器
     * 时间戳，使剪贴板管理器可正确探测/采信（对标 Qt）。 */
    if (xpw_clipEnsureOwnerWindow()) {
        st->m_timestamp = xpw_clipServerTimestamp();
        XSetSelectionOwner(g_xpwnDisplay, selection,
                           g_xpwnClipWin, st->m_timestamp);
        st->m_serveWin = g_xpwnClipWin;
        XFlush(g_xpwnDisplay);
    }
    return true;
}

static bool xpw_clipBackendClear(void* ud, int mode)
{
    (void)ud;
    /* 仅释放对应选择区的镜像；不支持的模式保持无操作。 */
    if (xpw_clipAtomForMode(mode) == None) return true;
    xpw_clipClearMirror(xpw_clipStateForMode(mode));
    return true;
}

/** @brief 后端 text 回调：经 X11 Selection 协议读取指定模式选择区内容。
 *  支持跨进程——从本框架或其他应用的对应选择区所有者读取。 */
static bool xpw_clipBackendText(void* ud, int mode, char** outText)
{
    char* text;
    (void)ud;
    if (xpw_clipAtomForMode(mode) == None) return false;
    text = xpw_clipReadSelection(xpw_clipAtomForMode(mode));
    if (!text) return false;
    *outText = text;
    return true;
}

/* INCR 读超时参数化（后端契约可选回调）：ms<=0 恢复默认；仅影响读方向
 * 的整体兜底超时，serve 会话的闲置回收不受影响。 */
static void xpw_clipBackendSetIncrTimeout(void* ud, int ms)
{
    (void)ud;
    g_xpwnClipIncrTimeoutMs =
        (ms > 0) ? ms : XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS;
}

static XClipboardBackend g_xpwnClipBackend = {
    NULL,                    /* ud（平台用户数据） */
    xpw_clipBackendText,     /* text */
    xpw_clipBackendSetText,  /* setText */
    xpw_clipBackendClear,    /* clear */
    true,                    /* supportsSelection（X11 接入 PRIMARY 选择区） */
    XClipboard_backendSelectionRevoked, /* selectionRevoked（反向通知入口） */
    xpw_clipBackendFormats,  /* formats（mime 多格式枚举） */
    xpw_clipBackendMimeData, /* mimeData（按格式借用读取） */
    xpw_clipBackendSetMimeData, /* setMimeData（逐格式写入镜像） */
    xpw_clipBackendSetIncrTimeout /* setIncrTimeoutMs（INCR 读超时参数化） */
};

/* SelectionClear 反向通知：经后端契约可选回调通知 XClipboard 层对应
 * 模式所有权被夺（未注册时保持旧行为，零回归）。 */
static void xpw_clipNotifyRevoked(int mode)
{
    if (g_xpwnClipBackend.selectionRevoked)
        g_xpwnClipBackend.selectionRevoked(g_xpwnClipBackend.ud, mode);
}

void XPlatformNativeWindow_installClipboardBackend(void)
{
    XClipboard_installBackend(&g_xpwnClipBackend);
}

#else
/* 剪贴板模块裁剪（XCLIPBOARD_ON=0）：后端整体不编译；事件路径仍引用
 * 的撤销通知退化为空桩（无平台后端即无所有权撤销语义）。 */
static void xpw_clipNotifyRevoked(int mode)
{
    (void)mode;
}
#endif /* XCLIPBOARD_ON */

bool XPlatformNativeWindow_isAvailable(void)
{
    return xpwn_ensureConnection();
}

/** @brief 按窗口 flags 提示位组装写 _MOTIF_WM_HINTS（定义见
 *         XPlatformNativeWindow_setWindowFlags 前的 MOTIF 小节）。 */
static void xpwn_applyMotifHints(Display* display, Window win,
                                 uint32_t flags);

/**
 * @brief      按 XWindow 类型写 _NET_WM_WINDOW_TYPE（对标
 *             QXcbWindow::setWindowType：EWMH 窗口类型提示，WM 据此
 *             决定装饰、任务栏分组、焦点策略与定位层级；此前本后端
 *             缺失该属性，对话框/工具窗口在 EWMH 合规 WM 上被当普通
 *             顶层窗口对待）。
 * @details    映射与 Qt xcb 同构：
 *             - Dialog/Sheet -> _NET_WM_WINDOW_TYPE_DIALOG；
 *             - Tool/Drawer  -> _NET_WM_WINDOW_TYPE_UTILITY；
 *             - SplashScreen -> _NET_WM_WINDOW_TYPE_SPLASH；
 *             - ToolTip      -> _NET_WM_WINDOW_TYPE_TOOLTIP；
 *             - Popup        -> _NET_WM_WINDOW_TYPE_POPUP_MENU
 *               （Qt 对 Qt::PopupMenu/Menu 同样写 POPUP_MENU；本框架
 *               Popup 即弹出族）；
 *             - 其余（普通顶层 Window 等）-> _NET_WM_WINDOW_TYPE_NORMAL
 *               （EWMH 建议普通顶层显式写 NORMAL，部分 WM 以"缺类型"
 *               做旧式识别，显式写避免误判）。
 *             COMBO 原子已登记但公共层 XWindowType 枚举暂无对应值，
 *             现阶段不可达。属性在创建（映射前）一次写入；Qt 同样在
 *             create 阶段落实类型。
 */
static void xpwn_applyWindowType(Display* display, Window win,
                                 XWindowType type)
{
    Atom typeAtom;
    switch (type) {
    case XWindowType_Dialog:
    case XWindowType_Sheet:
        typeAtom = g_xpwnNetWmTypeDialog;
        break;
    case XWindowType_Tool:
    case XWindowType_Drawer:
        typeAtom = g_xpwnNetWmTypeUtility;
        break;
    case XWindowType_SplashScreen:
        typeAtom = g_xpwnNetWmTypeSplash;
        break;
    case XWindowType_ToolTip:
        typeAtom = g_xpwnNetWmTypeTooltip;
        break;
    case XWindowType_Popup:
        typeAtom = g_xpwnNetWmTypePopupMenu;
        break;
    default:
        typeAtom = g_xpwnNetWmTypeNormal;
        break;
    }
    if (g_xpwnNetWmWindowType == None || typeAtom == None) return;
    XChangeProperty(display, win, g_xpwnNetWmWindowType, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)&typeAtom, 1);
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
#if XGUI_ON && XPLATFORM_FBDEV_ON
    /* 单屏互斥（嵌入式无窗口系统模型）：显示驱动已注册时拒绝创建 X11
     * 窗口并告警。检查必须在惰性 X 连接建立之前——嵌入式无 X 环境不能
     * 被 xpwn_ensureConnection 无辜建连（见 XPlatformFramebuffer_posix.h
     * 头注约束）。拒绝后 XWindow_createHandle 回落虚拟 WId，后备存储
     * flush 走显示驱动直写路径（XPlatformBackingStore_posix.c）。 */
    if (XPlatformDisplayDriver_active())
    {
        const XPlatformDisplayDriverOps* activeOps =
            XPlatformDisplayDriver_active();
        fprintf(stderr,
                "XPlatformNativeWindow_create: display driver \"%s\" active, "
                "refusing X11 window creation (single-screen fbdev model)\n",
                activeOps->m_name ? activeOps->m_name : "unknown");
        return false;
    }
#endif
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
    /* 本窗口实际使用的视觉/深度（普通窗口 = 进程选定 TrueColor 视觉；
     * override-redirect 瞬态弹层在下方按屏幕默认视觉改选——present 路径
     * 按本值创建匹配深度的 XImage，见 xpwn_preparePresentImage）。 */
    Visual* entryVisual = g_xpwnVisual;
    int entryDepth = g_xpwnDepth;
    {
    /* 仅 Qt 同款集合按 override-redirect 创建（对标 QXcbWindow::create：
     * Popup/ToolTip/SplashScreen 及携带 X11BypassWindowManagerHint 的窗口
     * ——瞬态弹出族生命周期极短、位置由应用给定，绕过 WM 的装饰/摆放/
     * 聚焦策略换即时弹出；Bypass 语义即"完全绕过 WM"）。FramelessWindow-
     * Hint 不再触发 override-redirect（对标 Qt：无边框窗口仍受 WM 管理，
     * 只经 _MOTIF_WM_HINTS decorations=0 抑制装饰，见 xpwn_applyMotifHints；
     * 此前把 Frameless 并入 override-redirect 会使无边框窗口绕过 WM——
     * 不进任务栏、不参与 Alt-Tab、失焦不回落，且 override_redirect 创建后
     * 不可改，语义被永久钉死）。Bypass 的差异说明：Qt 对创建后增删该提示
     * 经 re-create 改 override_redirect，本框架无 re-create，动态增删仍走
     * setWindowFlags 的 EWMH 近似（SKIP_TASKBAR/SKIP_PAGER，见该函数
     * 注释）；本判定只在创建时刻采纳创建时的提示位。 */
        XWindowType winType = XWindow_type(window);
        uint32_t winFlags = (uint32_t)XWindow_flags(window);
        bool winTransient = winType == XWindowType_Popup ||
                            winType == XWindowType_ToolTip ||
                            winType == XWindowType_SplashScreen;
        if (winTransient ||
            (winFlags & (uint32_t)XWindowType_BypassWindowManagerHint) != 0u) {
            attr.override_redirect = True;
        }
#if !XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
        /* 弹层上屏根修（#58/59）：瞬态弹层改用屏幕默认视觉/深度创建
         * （对标 QXcbWindow::create → createVisual →
         * QXcbVirtualDesktop::visualForFormat，qxcbwindow.cpp:315/2332 +
         * qxcbscreen.cpp:422——Qt 只为请求了 alpha 通道的窗口选 ARGB32
         * 视觉，普通/弹层窗口一律屏幕默认视觉）。
         *
         * 根因（xtrace + 最小客户端矩阵实测，本机 Xvfb/Xorg 均适用）：
         * 同为 depth-32（非根深度）的兄弟窗口，若主窗先创建、先上屏
         * （PutImage），后映射的 depth-32 弹层（Popup：override-
         * redirect）虽 IsViewable、Z 序在顶、服务器端窗口缓冲含完整
         * 不透明内容（xwd -id 可见），根屏合成却把它排除在外——移动出
         * 主窗范围即显示，移回即被主窗内容覆盖；XRaiseWindow/重映射均
         * 无效。把弹层降到屏幕默认深度（通常 24）后，各组合（d24 弹层
         * over d32/d24 主窗、连续重绘下）全部正常上屏；对照矩阵见
         * /tmp/k2_min3~8 实验记录。本分支只在「默认深度 != 进程选定
         * 深度」时改选（默认视觉本就是 32 位的屏、或进程本就运行在
         * 24 位视觉下时零变化）。 */
        if (winTransient && g_xpwnDefaultDepth != g_xpwnDepth) {
            entryVisual = g_xpwnDefaultVisual;
            entryDepth = g_xpwnDefaultDepth;
            attr.colormap = g_xpwnDefaultColormap;
        }
#endif
    }
    /* 输入事件掩码：键盘/鼠标按键/指针移动/进出均需在创建窗口时声明，
       否则 X 服务器不会向本窗口投递对应事件。滚轮事件(Button4/5)走
       ButtonPress 通道，进入/离开用于 Qt 对齐的 enter/leave 语义。
       PropertyChangeMask：观察 WM 对 _NET_WM_STATE/WM_STATE 的回写
       （setWindowState 请求最大化/全屏/最小化后，WM 增删原子/改 WM_STATE
       都经属性回写体现——对标 QXcbWindow::handlePropertyNotifyEvent 的
       状态观察入口，实测状态经 XWindow_reportWindowStateChanged 上报）。 */
    attr.event_mask = ExposureMask | StructureNotifyMask | FocusChangeMask
                   | KeyPressMask | KeyReleaseMask
                   | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
                   | EnterWindowMask | LeaveWindowMask | PropertyChangeMask;
    xwin = XCreateWindow(g_xpwnDisplay,
                         RootWindow(g_xpwnDisplay, g_xpwnScreenNumber),
                         geom.x, geom.y, (unsigned)w, (unsigned)h, 0,
                         entryDepth, InputOutput, entryVisual,
                         CWBackPixel | CWBorderPixel | CWColormap |
                             CWEventMask | CWOverrideRedirect,
                         &attr);
    if (!xwin) return false;

    entry->m_win = xwin;
    entry->m_window = window;
    entry->m_gc = XCreateGC(g_xpwnDisplay, xwin, 0, NULL);
    entry->m_client = geom;
    entry->m_visual = entryVisual;
    entry->m_depth = entryDepth;
    /* 窗口类型提示（对标 QXcbWindow::setWindowType：创建时按类型写
       _NET_WM_WINDOW_TYPE，映射前生效）。 */
    xpwn_applyWindowType(g_xpwnDisplay, xwin, XWindow_type(window));
    if (g_xpwnXdndAware != None) {
        unsigned long version = 5;
        XChangeProperty(g_xpwnDisplay, xwin, g_xpwnXdndAware, XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)&version, 1);
    }
    if (g_xpwnInputMethod) {
        XPoint spot;
        XIMCallback startCallback;
        XIMCallback doneCallback;
        XIMCallback drawCallback;
        /* 预编辑位置（客户端窗口坐标）。fcitx5 对 XIMPreeditNothing
           风格直接透传按键（无组合状态，拼音直上屏）——必须优先用
           preedit 风格（Position 兼容性最好，GTK/Qt 同款）。 */
        XPoint_init(&spot, 8, 8);
        startCallback.client_data = (XPointer)entry;
        startCallback.callback = (XIMProc)xpwn_preeditStart;
        doneCallback.client_data = (XPointer)entry;
        doneCallback.callback = (XIMProc)xpwn_preeditDone;
        drawCallback.client_data = (XPointer)entry;
        drawCallback.callback = (XIMProc)xpwn_preeditDraw;
        /* 0) 风格协商（对标 xterm/Xt 的 supported styles 交集）：
           查询输入法服务器支持的 style 列表，与客户端优先级表求
           交集，用交集里最优的创建 IC——硬编码组合若不在服务器列
           表中，XCreateIC 虽可能成功但服务器端无效（fcitx5 下表现为
           按键透传、无法进入中文组合）。 */
        {
            XIMStyles* serverStyles = NULL;
            char* miss = NULL;
            static const XIMStyle kPrefs[] = {
                /* fcitx5 的 StatusArea 组合在 XCreateIC 内部崩溃
                   （double free）——优先 StatusNothing 组合。 */
                XIMPreeditPosition  | XIMStatusNothing,
                XIMPreeditPosition  | XIMStatusArea,
                XIMPreeditCallbacks | XIMStatusNothing,
                XIMPreeditNothing   | XIMStatusNothing,
                XIMPreeditNone      | XIMStatusNone
            };
            static const char* const kNames[] = {
                "Position|StatusNothing", "Position|StatusArea",
                "Callbacks|StatusNothing", "Nothing|StatusNothing",
                "None|None"
            };
            XIMStyle chosen = 0;
            const char* chosenName = NULL;
            int pi;
            XGetIMValues(g_xpwnInputMethod, XNQueryInputStyle,
                         &serverStyles, XGetIMValues, &miss, NULL);
            if (serverStyles) {
                int s;
                for (pi = 0; pi < 5; ++pi) {
                    for (s = 0; s < (int)serverStyles->count_styles; ++s) {
                        if (serverStyles->supported_styles[s] == kPrefs[pi]) {
                            chosen = kPrefs[pi];
                            chosenName = kNames[pi];
                            break;
                        }
                    }
                    if (chosen != 0) break;
                }
                /* 打印服务器支持的全部 style（诊断）。 */
#if XPWN_IME_DEBUG
                for (s = 0; s < (int)serverStyles->count_styles; ++s)
                    XPrintf("[ime-dbg]   server style 0x%04lX\n",
                            (unsigned long)serverStyles->supported_styles[s]);
                XPrintf("[ime-dbg] 服务器支持 %u 种 style；选定 %s\n",
                        (unsigned)serverStyles->count_styles,
                        chosenName ? chosenName : "(无交集)");
#endif
#undef XFree
                /* supported_styles 与 XIMStyles 结构同块分配，只 Free
                   结构体一次（两次 Free 即 double free 崩溃）。 */
                XFree(serverStyles);
#define XFree XMemory_free
            }
            else if (miss) {
#if XPWN_IME_DEBUG
                XPrintf("[ime-dbg] style 查询缺失 %s\n", miss);
#endif
            }
            if (chosen == (XIMPreeditPosition | XIMStatusArea) ||
                chosen == (XIMPreeditPosition | XIMStatusNothing)) {
                /* PreeditPosition 风格必需 XNFontSet（预编辑绘制字体），
                   缺失则 XCreateIC 失败。 */
                char* missingList = NULL;
                int missingCount = 0;
                char* defString = NULL;
                XFontSet fontSet = XCreateFontSet(
                    g_xpwnDisplay,
                    "fixed",
                    &missingList, &missingCount, &defString);
                if (!fontSet)
                    fontSet = XCreateFontSet(
                        g_xpwnDisplay,
                        "-*-*-*-*-*-*-16-*-*-*-*-*-*-*",
                        &missingList, &missingCount, &defString);
#if XPWN_IME_DEBUG
                XPrintf("[ime-dbg] XCreateFontSet = %s\n",
                        fontSet ? "OK" : "FAILED");
#endif
                /* 平铺传法（嵌套列表 XVaCreateNestedList 在本环境对
                   XNFontSet 处理崩溃——见回归 SegFault 定位；平铺
                   版稳定但 fcitx5 下 XCreateIC 返回 NULL，中文输入
                   待后续换 PreeditCallbacks 自绘 preedit 方案）。 */
                entry->m_inputContext = XCreateIC(
                    g_xpwnInputMethod,
                    XNInputStyle, chosen,
                    XNClientWindow, xwin,
                    XNFocusWindow, xwin,
                    XNFontSet, fontSet,
                    XNSpotLocation, &spot,
                    NULL);
                if (missingList) XFreeStringList(missingList);
                /* 字体集为 entry 所有（XCreateIC 不接管；IC 存续期间须保持
                 * 有效，随窗口销毁在 XDestroyIC 后释放）。 */
                entry->m_fontSet = fontSet;
            }
            else if (chosen == (XIMPreeditCallbacks | XIMStatusNothing)) {
                entry->m_inputContext = XCreateIC(
                    g_xpwnInputMethod,
                    XNInputStyle, chosen,
                    XNClientWindow, xwin,
                    XNFocusWindow, xwin,
                    XNPreeditStartCallback, &startCallback,
                    XNPreeditDoneCallback, &doneCallback,
                    XNPreeditDrawCallback, &drawCallback,
                    XNSpotLocation, &spot,
                    NULL);
            }
            else if (chosen != 0) {
                entry->m_inputContext = XCreateIC(
                    g_xpwnInputMethod,
                    XNInputStyle, chosen,
                    XNClientWindow, xwin,
                    XNFocusWindow, xwin,
                    NULL);
            }
#if XPWN_IME_DEBUG
            XPrintf("[ime-dbg] XCreateIC(style=%s) = %s\n",
                    chosenName ? chosenName : "无",
                    entry->m_inputContext ? "OK" : "FAILED");
#endif
            entry->m_spot = spot;
        }
    }
    /* 初始标题同步（公共层 createHandle 后也会再同步，这里是兜底）。 */
    title = XWindow_title(window);
    xpwn_applyTitle(xwin, title);
    if (title) XString_delete_base((XClass*)title);
    /* 初始装饰提示（对标 Qt xcb：创建时即按 flags 写 _MOTIF_WM_HINTS；
       默认窗口无提示位 → DECOR_ALL + FUNC_ALL，与 WM 默认装饰等价）。
       此后的提示位变化经 setWindowFlags 重写。 */
    xpwn_applyMotifHints(g_xpwnDisplay, xwin, (uint32_t)XWindow_flags(window));
    /* 注册 WM_DELETE_WINDOW 协议，窗口装饰栏关闭按钮经 WM 送达本泵。 */
    XSetWMProtocols(g_xpwnDisplay, xwin, &g_xpwnWmDelete, 1);
    XFlush(g_xpwnDisplay);
#ifdef XINYUE_C_HAS_XI2
    xpwn_xi2SelectTouch(xwin); /* XI2 可用：补选触摸三类掩码。 */
#endif /* XINYUE_C_HAS_XI2 */
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
#ifdef XINYUE_C_HAS_XI2
    xpwn_xi2SelectTouch(entry->m_win); /* XI2 可用：补选触摸三类掩码。 */
#endif /* XINYUE_C_HAS_XI2 */
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
    /* PreeditPosition 风格 IC 的字体集：IC 已销毁方可释放（§8.0g7）。 */
    if (entry->m_fontSet) {
        XFreeFontSet(g_xpwnDisplay, entry->m_fontSet);
        entry->m_fontSet = NULL;
    }
    xpwn_releasePresentImage(entry);
    if (entry->m_gc) XFreeGC(g_xpwnDisplay, entry->m_gc);
    /* 窗口销毁时取消已定义的窗口光标（XUndefineCursor；形状字体光标
       资源本身由进程期缓存持有，随 XCloseDisplay 统一释放）。 */
    if (entry->m_win)
        XUndefineCursor(g_xpwnDisplay, entry->m_win);
    /* 该窗口若是跨进程剪贴板读取的请求者或 INCR serve 的对端，清理其
       在途会话（防御；常规请求窗口为专用窗口不随业务窗口销毁）。 */
    xpw_clipIncrAbortForWindow(entry->m_win);
    /* 外部窗口只解除登记，不取得其 X11 资源的销毁所有权。 */
    if (XWindow_type(window) != XWindowType_ForeignWindow)
        XDestroyWindow(g_xpwnDisplay, entry->m_win);
    entry->m_win = 0;
    entry->m_window = NULL;
    entry->m_gc = NULL;
    entry->m_visual = NULL;
    entry->m_depth = 0;
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

/* ==================== EWMH 窗口状态（对标 QXcbWindow::setWindowState） ==================== */

/**
 * @brief      Withdrawn（未映射）窗口的 _NET_WM_STATE 属性路径：读-改-写
 *             单个状态原子。EWMH：已映射窗口的 _NET_WM_STATE 归 WM 所有，
 *             客户端只能经 ClientMessage 请求变更；属性直写仅对
 *             Withdrawn 窗口在下次映射时由 WM 采纳（对标
 *             QXcbWindow::setNetWmState 的 xcb_change_property
 *             PropModeReplace 分支）。
 */
static void xpwn_writeNetWmStateAtom(Display* display, Window win,
                                     Atom stateAtom, bool add)
{
    Atom merged[48];
    Atom* current = NULL;
    Atom actualType;
    int actualFormat;
    unsigned long nItems = 0;
    unsigned long bytesAfter = 0;
    unsigned long i;
    int mergedCount = 0;
    bool present = false;
    if (g_xpwnNetWmState == None || stateAtom == None) return;
    XGetWindowProperty(display, win, g_xpwnNetWmState, 0, 1024, False,
                       XA_ATOM, &actualType, &actualFormat, &nItems,
                       &bytesAfter, (unsigned char**)&current);
    if (current) {
        for (i = 0; i < nItems && mergedCount < 48; ++i) {
            if (current[i] == stateAtom) {
                present = true;
                if (!add) continue; /* REMOVE：剔除该原子。 */
            }
            merged[mergedCount++] = current[i];
        }
        xpwn_xFree(current); /* Xlib 属性缓冲须用真实 XFree 释放。 */
    }
    if (add && !present && mergedCount < 48)
        merged[mergedCount++] = stateAtom;
    XChangeProperty(display, win, g_xpwnNetWmState, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)merged, mergedCount);
}

/**
 * @brief      从 WM 托管属性回读窗口实测状态（对标 QXcbWindow::
 *             handlePropertyNotifyEvent 的状态解析：WM 增删
 *             _NET_WM_STATE 原子或改写 WM_STATE 后回写属性，平台层把
 *             属性内容换算成 Qt 状态位供上报公共层）。
 * @return     XWindowState 位组合（FullScreen/Maximized/Minimized）。
 */
static XWindowState xpwn_queryWmWindowState(Display* display, Window win)
{
    XWindowState state = XWindowState_NoState;
    Atom actualType;
    int actualFormat;
    unsigned long nItems = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = NULL;
    unsigned long i;
    if (win == None) return state;
    /* _NET_WM_STATE：XA_ATOM 数组。最大化在 X11 拆为 VERT/HORZ 两原子，
       须同时在场（对标 Qt 的 Maximized 双原子换算）。 */
    if (g_xpwnNetWmState != None &&
        XGetWindowProperty(display, win, g_xpwnNetWmState, 0, 1024, False,
                           XA_ATOM, &actualType, &actualFormat, &nItems,
                           &bytesAfter, &data) == Success &&
        data != NULL && actualFormat == 32) {
        bool maxVert = false;
        bool maxHorz = false;
        for (i = 0; i < nItems; ++i) {
            Atom atom = ((const Atom*)data)[i];
            if (atom == g_xpwnNetWmStateFullscreen)
                state |= XWindowState_FullScreen;
            else if (atom == g_xpwnNetWmStateMaximizedVert) maxVert = true;
            else if (atom == g_xpwnNetWmStateMaximizedHorz) maxHorz = true;
        }
        if (maxVert && maxHorz) state |= XWindowState_Maximized;
        xpwn_xFree(data); /* Xlib 属性缓冲须用真实 XFree 释放。 */
    }
    /* WM_STATE：data32[0] 为 ICCCM 状态（IconicState=3 -> 最小化，
       对标 Qt 对 WM_STATE 图标化状态的检测）。 */
    data = NULL;
    if (g_xpwnWmState != None &&
        XGetWindowProperty(display, win, g_xpwnWmState, 0, 2, False,
                           g_xpwnWmState, &actualType, &actualFormat,
                           &nItems, &bytesAfter, &data) == Success &&
        data != NULL && actualFormat == 32 && nItems >= 1) {
        if (((const long*)data)[0] == IconicState)
            state |= XWindowState_Minimized;
        xpwn_xFree(data);
    }
    return state;
}

/* ==================== EWMH 窗口标志同步（对标 QXcbWindow::setWindowFlags） ==================== */

/** @brief 查询 X11 窗口当前是否已映射可见（IsViewable）。 */
static bool xpwn_windowViewable(Display* display, Window win)
{
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, win, &attrs)) return false;
    return attrs.map_state == IsViewable;
}

/**
 * @brief      发送 _NET_WM_STATE ClientMessage 到根窗口。
 * @details    EWMH 规定：窗口已映射后其状态变更必须经 WM 处理
 *             （ClientMessage），直接改 _NET_WM_STATE 属性只对
 *             Withdrawn（未映射）状态的窗口在下次映射时生效——
 *             对标 QXcbWindow::setNetWmState 的映射态处理。
 */
static void xpwn_sendNetWmStateMessage(Display* display, Window win,
                                       Atom stateAtom, bool add)
{
    X11_XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = win;
    event.xclient.message_type = g_xpwnNetWmState;
    event.xclient.format = 32;
    /* l[0]：动作，1 = _NET_WM_STATE_ADD，0 = _NET_WM_STATE_REMOVE；
       l[1]：状态原子；l[3]：来源标识 1 = application（EWMH 规范）。 */
    event.xclient.data.l[0] = add ? 1 : 0;
    event.xclient.data.l[1] = (long)stateAtom;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 1;
    event.xclient.data.l[4] = 0;
    XSendEvent(display, RootWindow(display, g_xpwnScreenNumber), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

/**
 * @brief      请求 WM 变更窗口状态（对标 QXcbWindow::setWindowState）。
 * @details    最大化/全屏走 EWMH _NET_WM_STATE ClientMessage（已映射），
 *             动作按目标状态逐原子取 ADD/REMOVE——目标未置位的原子要
 *             显式 REMOVE（对标 Qt setWindowState 先撤旧态再落新态）；
 *             最大化按 X11 惯例同时请求 MAXIMIZED_VERT + MAXIMIZED_HORZ
 *             两原子。Withdrawn（未映射）窗口改走 _NET_WM_STATE 属性
 *             直写（映射时由 WM 采纳，见 xpwn_writeNetWmStateAtom）。
 *             最小化走 ICCCM 4.1.4 WM_CHANGE_STATE(IconicState)
 *             ClientMessage——EWMH 未定义最小化状态原子，Qt xcb 同以
 *             WM_CHANGE_STATE 请求图标化。状态实际落定由 WM 回写
 *             _NET_WM_STATE/WM_STATE 体现，经 PropertyNotify ->
 *             xpwn_queryWmWindowState 回读并 XWindow_reportWindowState-
 *             Changed 上报公共层（闭环，见 xpwn_dispatchEvent）。
 * @return     请求已发出返回 true；窗口未登记/连接不可用返回 false。
 */
bool XPlatformNativeWindow_setWindowState(XWindow* window, uint32_t state)
{
    XWNPendingEntry* entry;
    bool fullScreen;
    bool maximized;
    struct
    {
        Atom atom; /**< _NET_WM_STATE 状态原子。 */
        bool on;   /**< 目标状态是否要求该原子在场（ADD）或不在场（REMOVE）。 */
    } netStates[3];
    int netCount = 0;
    int i;
    if (!window || !xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    fullScreen = (state & (uint32_t)XWindowState_FullScreen) != 0;
    maximized = (state & (uint32_t)XWindowState_Maximized) != 0;

    /* 最小化：WM_CHANGE_STATE(IconicState) ClientMessage 发往根窗口
       （ICCCM 4.1.4：客户端经根窗口 SubstructureRedirect 请求 WM 把
       自己转入图标态）。 */
    if ((state & (uint32_t)XWindowState_Minimized) != 0) {
        X11_XEvent event;
        memset(&event, 0, sizeof(event));
        event.xclient.type = ClientMessage;
        event.xclient.display = g_xpwnDisplay;
        event.xclient.window = entry->m_win;
        event.xclient.message_type = g_xpwnWmChangeState;
        event.xclient.format = 32;
        event.xclient.data.l[0] = IconicState;
        XSendEvent(g_xpwnDisplay,
                   RootWindow(g_xpwnDisplay, g_xpwnScreenNumber), False,
                   SubstructureRedirectMask | SubstructureNotifyMask,
                   &event);
        XFlush(g_xpwnDisplay);
        return true;
    }

    netStates[netCount].atom = g_xpwnNetWmStateFullscreen;
    netStates[netCount++].on = fullScreen;
    netStates[netCount].atom = g_xpwnNetWmStateMaximizedVert;
    netStates[netCount++].on = maximized;
    netStates[netCount].atom = g_xpwnNetWmStateMaximizedHorz;
    netStates[netCount++].on = maximized;
    if (xpwn_windowViewable(g_xpwnDisplay, entry->m_win)) {
        for (i = 0; i < netCount; ++i)
            xpwn_sendNetWmStateMessage(g_xpwnDisplay, entry->m_win,
                                       netStates[i].atom, netStates[i].on);
    } else {
        for (i = 0; i < netCount; ++i)
            xpwn_writeNetWmStateAtom(g_xpwnDisplay, entry->m_win,
                                     netStates[i].atom, netStates[i].on);
    }
    XFlush(g_xpwnDisplay);
    return true;
}

/* ==================== MOTIF 装饰提示（对标 QXcbWindow::setMotifWindowFlags） ==================== */

/*
 * OSF/Motif mwm 协议的 _MOTIF_WM_HINTS 载体：标准 5 字段（flags/functions/
 * decorations/input_mode/status），与 Xm/MwmUtil.h 一致。为避免引入 xpm/
 * Motif 头依赖（本仓库不链接 Xm），结构体与位常量就地定义；数值为 ICCCM
 * 及所有主流 WM 公认的协议常量，非本实现私有。
 */
#define XXPWN_MWM_HINTS_FUNCTIONS   (1L << 0)  /**< functions 字段有效。 */
#define XXPWN_MWM_HINTS_DECORATIONS (1L << 1)  /**< decorations 字段有效。 */
/* functions 位域。 */
#define XXPWN_MWM_FUNC_ALL      (1L << 0)      /**< 全部按钮/功能。 */
#define XXPWN_MWM_FUNC_RESIZE   (1L << 1)      /**< 允许调整尺寸。 */
#define XXPWN_MWM_FUNC_MOVE     (1L << 2)      /**< 允许移动。 */
#define XXPWN_MWM_FUNC_MINIMIZE (1L << 3)      /**< 最小化按钮。 */
#define XXPWN_MWM_FUNC_MAXIMIZE (1L << 4)      /**< 最大化按钮。 */
#define XXPWN_MWM_FUNC_CLOSE    (1L << 5)      /**< 关闭按钮。 */
/* decorations 位域。 */
#define XXPWN_MWM_DECOR_ALL      (1L << 0)     /**< 全部装饰。 */
#define XXPWN_MWM_DECOR_BORDER   (1L << 1)     /**< 边框。 */
#define XXPWN_MWM_DECOR_RESIZEH  (1L << 2)     /**< 尺寸调节柄。 */
#define XXPWN_MWM_DECOR_TITLE    (1L << 3)     /**< 标题栏。 */
#define XXPWN_MWM_DECOR_MENU     (1L << 4)     /**< 系统菜单按钮。 */
#define XXPWN_MWM_DECOR_MINIMIZE (1L << 5)     /**< 最小化按钮。 */
#define XXPWN_MWM_DECOR_MAXIMIZE (1L << 6)     /**< 最大化按钮。 */

/** @brief _MOTIF_WM_HINTS 属性载体（标准 5 字段，格式 32、nelements=5）。 */
typedef struct XpwnMotifWmHints
{
    unsigned long flags;        /**< 哪些字段有效（XXPWN_MWM_HINTS_*）。 */
    unsigned long functions;    /**< 窗口功能按钮位域（XXPWN_MWM_FUNC_*）。 */
    unsigned long decorations;  /**< 窗口装饰位域（XXPWN_MWM_DECOR_*）。 */
    long input_mode;            /**< 输入模式（本实现恒 0）。 */
    unsigned long status;       /**< 状态位域（仅 mwm 内部使用，恒 0）。 */
} XpwnMotifWmHints;

/**
 * @brief      按 XWindow flags 的装饰提示位组装并写入 _MOTIF_WM_HINTS。
 * @details    对标 QXcbWindow::setMotifWindowFlags：请求 WM 调整标题栏
 *             按钮组合。组装规则：
 *             - 无任何装饰提示位（未 Customize、无 Title/SystemMenu/
 *               Min/Max/Close 提示、未 Frameless/固定尺寸）：DECOR_ALL +
 *               FUNC_ALL —— 普通窗口保持 WM 默认装饰（存量窗口零回归）；
 *             - FramelessWindowHint：decorations=0（Qt 语义无边框）；
 *             - 显式提示模式（CustomizeWindowHint 或任一装饰提示位出现）：
 *               按提示位逐位组装——TitleHint→DECOR_TITLE、SystemMenuHint→
 *               DECOR_MENU、Minimize/MaximizeButtonHint→DECOR_MINIMIZE/
 *               MAXIMIZE + FUNC_MINIMIZE/MAXIMIZE、CloseButtonHint→
 *               FUNC_CLOSE；MSWindowsFixedSizeDialogHint 抑制 BORDER/
 *               RESIZEH/RESIZE（固定尺寸无调节柄）。提示位变化时经
 *               XWindow_setFlags → 本函数整体重写（属性替换写）。
 *             无 WM/属性写失败时静默：XChangeProperty 对无 WM 会话同样
 *             成功落属性（Withdrawn 布局值），行为与 Qt xcb 一致。
 */
static void xpwn_applyMotifHints(Display* display, Window win,
                                 uint32_t flags)
{
    XpwnMotifWmHints hints;
    uint32_t decorateBits = (uint32_t)(
        (uint32_t)XWindowType_CustomizeWindowHint |
        (uint32_t)XWindowType_WindowTitleHint |
        (uint32_t)XWindowType_WindowSystemMenuHint |
        (uint32_t)XWindowType_WindowMinimizeButtonHint |
        (uint32_t)XWindowType_WindowMaximizeButtonHint |
        (uint32_t)XWindowType_WindowCloseButtonHint |
        (uint32_t)XWindowType_MSWindowsFixedSizeDialogHint);
    bool explicitHints = (flags & decorateBits) != 0u;
    hints.flags = XXPWN_MWM_HINTS_FUNCTIONS | XXPWN_MWM_HINTS_DECORATIONS;
    hints.input_mode = 0;
    hints.status = 0;
    if (!explicitHints) {
        /* 默认：全装饰 + 全功能（对标 Qt 未定制窗口的 MWM 默认）。 */
        hints.functions = XXPWN_MWM_FUNC_ALL;
        hints.decorations = XXPWN_MWM_DECOR_ALL;
    } else if (flags & (uint32_t)XWindowType_FramelessWindowHint) {
        /* 无边框提示优先于其它装饰位（对标 Qt FramelessWindowHint）。 */
        hints.functions = XXPWN_MWM_FUNC_MOVE | XXPWN_MWM_FUNC_CLOSE;
        hints.decorations = 0;
    } else {
        bool fixedSize =
            (flags & (uint32_t)XWindowType_MSWindowsFixedSizeDialogHint) != 0u;
        hints.functions = XXPWN_MWM_FUNC_MOVE;
        hints.decorations = 0;
        if (flags & (uint32_t)XWindowType_WindowTitleHint)
            hints.decorations |= XXPWN_MWM_DECOR_TITLE;
        if (flags & (uint32_t)XWindowType_WindowSystemMenuHint)
            hints.decorations |= XXPWN_MWM_DECOR_MENU;
        if (flags & (uint32_t)XWindowType_WindowMinimizeButtonHint) {
            hints.decorations |= XXPWN_MWM_DECOR_MINIMIZE;
            hints.functions |= XXPWN_MWM_FUNC_MINIMIZE;
        }
        if (flags & (uint32_t)XWindowType_WindowMaximizeButtonHint) {
            hints.decorations |= XXPWN_MWM_DECOR_MAXIMIZE;
            hints.functions |= XXPWN_MWM_FUNC_MAXIMIZE;
        }
        if (flags & (uint32_t)XWindowType_WindowCloseButtonHint)
            hints.functions |= XXPWN_MWM_FUNC_CLOSE;
        if (!fixedSize) {
            /* 非固定尺寸：保留边框与尺寸调节柄（按钮提示不剥夺轮廓）。 */
            hints.decorations |= XXPWN_MWM_DECOR_BORDER | XXPWN_MWM_DECOR_RESIZEH;
            hints.functions |= XXPWN_MWM_FUNC_RESIZE;
        }
    }
    /* 属性类型与属性名同名（_MOTIF_WM_HINTS），格式 32、5 个元素；
       mwm 兼容 WM（Mutter/KWin/DDE/XFCE）据此调整标题栏按钮组合。 */
    XChangeProperty(display, win, g_xpwnMotifWmHints, g_xpwnMotifWmHints,
                    32, PropModeReplace, (unsigned char*)&hints,
                    (int)(sizeof(hints) / sizeof(long)));
}

bool XPlatformNativeWindow_setWindowFlags(XWindow* window, uint32_t flags)
{
    XWNPendingEntry* entry;
    Atom managed[4];             /**< 本实现管理的 _NET_WM_STATE 原子。 */
    bool managedOn[4];           /**< 各原子按当前 flags 的期望开关。 */
    Atom merged[48];             /**< 读-改-写后的 _NET_WM_STATE 全集。 */
    Atom* current = NULL;        /**< 窗口现有 _NET_WM_STATE 列表。 */
    Atom actualType;
    int actualFormat;
    unsigned long nItems = 0;
    unsigned long bytesAfter = 0;
    unsigned long i;
    int managedCount = 0;
    int mergedCount = 0;
    int k;
    if (!window || !xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;

    /* 提示位 → EWMH 映射（位值取自 XWindow.h 的 XWindowType_*，与
     * Qt::WindowFlags 数值完全一致；对标 QXcbWindow::setWindowFlags
     * 组装 _NET_WM_STATE）：
     *   WindowStaysOnTopHint(0x00040000)    -> _NET_WM_STATE_ABOVE
     *   WindowStaysOnBottomHint(0x04000000) -> _NET_WM_STATE_BELOW
     *   BypassWindowManagerHint(0x00000400) -> _NET_WM_STATE_SKIP_TASKBAR
     *                                        + _NET_WM_STATE_SKIP_PAGER
     * 近似差异说明：Qt xcb 对 BypassWindowManagerHint 的真实落地是
     * re-create 原生窗口（override_redirect=True，完全绕过 WM）；
     * re-create 会破坏本框架 XWindow<->原生窗口登记、GC 与后备缓冲
     * 绑定，故按 EWMH 语义用「不进任务栏/分页器」近似表达，差异为
     * 窗口仍受 WM 管理装饰与定位。 */
    if (flags & (uint32_t)XWindowType_WindowStaysOnTopHint) {
        managed[managedCount] = g_xpwnNetWmStateAbove;
        managedOn[managedCount++] = true;
    } else {
        managed[managedCount] = g_xpwnNetWmStateAbove;
        managedOn[managedCount++] = false;
    }
    if (flags & (uint32_t)XWindowType_WindowStaysOnBottomHint) {
        managed[managedCount] = g_xpwnNetWmStateBelow;
        managedOn[managedCount++] = true;
    } else {
        managed[managedCount] = g_xpwnNetWmStateBelow;
        managedOn[managedCount++] = false;
    }
    if (flags & (uint32_t)XWindowType_BypassWindowManagerHint) {
        managed[managedCount] = g_xpwnNetWmStateSkipTaskbar;
        managedOn[managedCount++] = true;
        managed[managedCount] = g_xpwnNetWmStateSkipPager;
        managedOn[managedCount++] = true;
    } else {
        managed[managedCount] = g_xpwnNetWmStateSkipTaskbar;
        managedOn[managedCount++] = false;
        managed[managedCount] = g_xpwnNetWmStateSkipPager;
        managedOn[managedCount++] = false;
    }

    /* 读-改-写 _NET_WM_STATE（对标 QXcbWindow::setNetWmState 的
     * xcb_change_property PropModeReplace）：保留 WM 写入的非本实现
     * 管理的状态原子（如 FULLSCREEN/HIDDEN），只按当前 flags 增删
     * 本实现管理的四个原子。EWMH 规定 _NET_WM_STATE 属性对已映射
     * 窗口归 WM 所有：实测（DDE/KWin 类 WM）在映射态下客户端直接
     * 改写会被 WM 以内部状态回写冲掉，因此属性写只用于 Withdrawn
     * （未映射）窗口在映射时生效；已映射窗口仅发 ClientMessage，
     * 由 WM 增删后回写属性（见 xpwn_sendNetWmStateMessage）。 */
    if (!xpwn_windowViewable(g_xpwnDisplay, entry->m_win)) {
        XGetWindowProperty(g_xpwnDisplay, entry->m_win, g_xpwnNetWmState,
                           0, 1024, False, XA_ATOM, &actualType,
                           &actualFormat, &nItems, &bytesAfter,
                           (unsigned char**)&current);
        if (current) {
            for (i = 0; i < nItems && mergedCount < 48; ++i) {
                bool ours = false;
                for (k = 0; k < managedCount; ++k) {
                    if (current[i] == managed[k]) { ours = true; break; }
                }
                if (!ours) merged[mergedCount++] = current[i];
            }
            xpwn_xFree(current); /* Xlib 属性缓冲须用真实 XFree 释放。 */
        }
        for (k = 0; k < managedCount && mergedCount < 48; ++k) {
            if (managedOn[k]) merged[mergedCount++] = managed[k];
        }
        XChangeProperty(g_xpwnDisplay, entry->m_win, g_xpwnNetWmState,
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)merged, mergedCount);
    } else {
        /* 已映射窗口经 ClientMessage 请求 WM 即时生效。注意实测本机
         * WM（DDE/KWin 类）对刚映射、尚未完全托管的窗口有短暂的消息
         * 丢弃期（约数百毫秒），期间发送的状态变更可能不生效；窗口
         * settle 后行为稳定，属 WM 侧行为，与 Qt xcb 同类。 */
        for (k = 0; k < managedCount; ++k) {
            xpwn_sendNetWmStateMessage(g_xpwnDisplay, entry->m_win,
                                       managed[k], managedOn[k]);
        }
    }

    /* WindowDoesNotAcceptFocus -> _NET_WM_HINTS 的 input=False（对标
     * QXcbWindow::setWindowFlags 的 wmInputFlag 维护：不接受输入时
     * 清 input 提示位）。清除标志时恢复 input=True（X 默认接受输入）。 */
    {
        XWMHints stackHints;
        XWMHints* hints = XGetWMHints(g_xpwnDisplay, entry->m_win);
        if (!hints) {
            memset(&stackHints, 0, sizeof(stackHints));
            hints = &stackHints;
        }
        hints->flags |= InputHint;
        hints->input = (flags & (uint32_t)XWindowType_WindowDoesNotAcceptFocus)
                           ? False : True;
        XSetWMHints(g_xpwnDisplay, entry->m_win, hints);
        if (hints != &stackHints) xpwn_xFree(hints);
    }

    /* 已映射窗口经 ClientMessage 请求 WM 即时生效（见
     * xpwn_sendNetWmStateMessage 的 EWMH 依据）；未映射窗口只改属性。 */
    if (xpwn_windowViewable(g_xpwnDisplay, entry->m_win)) {
        for (k = 0; k < managedCount; ++k) {
            xpwn_sendNetWmStateMessage(g_xpwnDisplay, entry->m_win,
                                       managed[k], managedOn[k]);
        }
    }
    /* 窗口类型提示随 flags 变化重写（对标 QXcbWindow::setWindowFlags 内部
     * 调用 setWindowType：类型是 flags 的 TypeMask 位段，提示位增删可能
     * 连带类型迁移（如 Widget→Dialog）；_NET_WM_WINDOW_TYPE 为单值属性，
     * 整体替换写。XWindow_setFlags 已先更新 m_flags 再进入本函数，此处
     * XWindow_type 取到的是新类型）。 */
    xpwn_applyWindowType(g_xpwnDisplay, entry->m_win, XWindow_type(window));
    XFlush(g_xpwnDisplay);

    /* 装饰提示落地：WindowMinMaxButtonsHint/WindowTitleHint/SystemMenu/
     * CloseButtonHint/Frameless 等按提示位组装 _MOTIF_WM_HINTS 请求 WM
     * 调整标题栏按钮组合（对标 Qt xcb setMotifWindowFlags）；提示位变化
     * 时随本函数重写；无 WM 时属性仍落窗口、无副作用（静默）。 */
    xpwn_applyMotifHints(g_xpwnDisplay, entry->m_win, flags);
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
    XWindowAttributes attrs;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    /* 对齐 QXcbWindow::requestActivateWindow：窗口尚未完全映射
       （unmapped/unviewable，例如 show() 的映射仍是异步未完成、或
       父窗口链未映射）时，XSetInputFocus 会产生 BadMatch，因此不发
       任何 X 请求，改为挂起激活，待 MapNotify 后补激活。 */
    attrs.map_state = IsUnmapped;
    if (!XGetWindowAttributes(g_xpwnDisplay, entry->m_win, &attrs))
        return false;
    if (attrs.map_state != IsViewable) {
        entry->m_deferredActivation = true;
        return false;
    }
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
        XImage_deinit_base(&image);
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
    /* 首次泵时惰性接入屏幕（枚举/登记/DPI 回填）；此后幂等。见
       xpwn_ensureConnection 注释——放在泵而非连接期，保证应用单例已发布。 */
    xpwn_screensInit();
    xpwn_imePump(); /* 每帧抽取 fcitx5 的 CommitString 等信号。 */
    while (XPending(g_xpwnDisplay) > 0) {
        XNextEvent(g_xpwnDisplay, &event);
#if defined(XINYUE_C_HAS_XRANDR)
        if (g_xpwnRrEventBase >= 0 &&
            event.type == g_xpwnRrEventBase + RRScreenChangeNotify) {
            /* 对标 QXcbConnection 的 RandR 屏幕事件分支：刷新 Xlib 服务器
               几何缓存后重枚举监视器，经 WSI 入口差分回填（内部按变化
               发射 geometryChanged/physicalDotsPerInchChanged 等）。 */
            xpwn_screensRefresh(&event);
            delivered = true;
            continue;
        }
#endif
        if (xpwn_dispatchEvent(&event)) delivered = true;
    }
    return delivered;
}

/* ==================== 主循环双源等待（对标 QEventDispatcherUNIX 的
 * 统一 poll）：X11 连接 fd 与 XAbstractNetIoRing 全局 ring 的事件 fd
 * （io_uring ring fd / epoll fd）同数组 poll，谁就绪处理谁——
 * X11 就绪泵原生事件，ring 就绪经 processReady 拉一轮完成包（内部
 * pollPlatform -> 排空 SQ -> drainCQ -> dispatchCQEntry）。
 * 网络模块裁剪（XAbstractNetIoRing_ON=0）或 ring 未启用/无事件 fd 时
 * 自动退化为单源等待，行为与既往完全一致。定时器 deadline 由调用方
 * （XGuiApplication_waitForEvents 的使用模式）经 maxMilliseconds 传入，
 * XDeviceTimer 时间轮不需要 pollfd 槽位。 */
bool XPlatformNativeWindow_waitForEvents(int maxMilliseconds)
{
    struct pollfd fds[2];
    int xfd;
    nfds_t count = 0;
    int result;
    bool x11Ready = false;
    bool ringReady = false;
    if (!xpwn_ensureConnection()) return false;
    /* 前置快查：X11 已有积压事件时直接泵，不让 ring 就绪插队。 */
    if (XPending(g_xpwnDisplay) > 0)
        return XPlatformNativeWindow_processPendingEvents();
    xfd = XConnectionNumber(g_xpwnDisplay);
    if (xfd < 0) return false;
    memset(fds, 0, sizeof(fds));
    /* 源 0：X11 连接（有协议数据可读即有待处理事件）。 */
    fds[count].fd = xfd;
    fds[count].events = POLLIN;
    ++count;
#if XAbstractNetIoRing_ON
    /* 源 1：全局 ring 事件 fd（io_uring 完成入队 / epoll 就绪列表
     * 变化 / wakeUp eventfd 写入都会使其可读）。 */
    {
        XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
        if (ring && XAbstractNetIoRing_isEnabled(ring)) {
            XFd ringFd = XAbstractNetIoRing_getEventFd_base(ring);
            if (ringFd != XFD_INVALID) {
                fds[count].fd = (int)(intptr_t)ringFd;
                fds[count].events = POLLIN;
                ++count;
            }
        }
    }
#endif /* XAbstractNetIoRing_ON */
    result = poll(fds, count, maxMilliseconds);
    if (result <= 0) return false; /* 超时或出错：无事件可处理。 */
    /* 分源判定：spurious wakeup（revents 为 0）时两边都不动。 */
    if ((fds[0].revents & (POLLIN | POLLERR | POLLHUP)) != 0)
        x11Ready = true;
#if XAbstractNetIoRing_ON
    if (count > 1 &&
        (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) != 0)
        ringReady = true;
#endif /* XAbstractNetIoRing_ON */
#if XAbstractNetIoRing_ON
    if (ringReady) {
        XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
        /* processReady 内部按 pollPlatform -> SQ -> CQ 顺序拉取并
           经 dispatchCQEntry 投递完成事件到应用层（postEvent 语义，
           与主循环后续 processEvents 自然衔接）。 */
        if (ring && XAbstractNetIoRing_isEnabled(ring))
            XAbstractNetIoRing_processReady(ring);
    }
#endif /* XAbstractNetIoRing_ON */
    if (x11Ready)
        return XPlatformNativeWindow_processPendingEvents();
    /* 仅 ring 就绪：网络事件已经投递，本次无可泵的 GUI 事件。 */
    return false;
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

static bool xpwn_preparePresentImage(XWNPendingEntry* entry,
                                      int imgW, int imgH)
{
    X11_XImage* ximg;
    uint8_t* buffer;
    size_t bufferSize;
    int bufBpl;
    int wantBpl = 0; /* 0 = 交由 XCreateImage 按 bitmap_pad 自算行跨度。 */
    bool direct;
    /* 按窗口实际视觉/深度准备上屏描述符（#58/59 弹层上屏根修配套：
       瞬态弹层以屏幕默认视觉/深度创建，present 必须生成同深度的
       XImage，否则 XPutImage BadMatch。未登记时回落进程选定视觉，
       与旧行为一致。） */
    Visual* visual = entry && entry->m_visual ? entry->m_visual : g_xpwnVisual;
    int depth = entry && entry->m_depth ? entry->m_depth : g_xpwnDepth;
    if (!entry || imgW <= 0 || imgH <= 0)
        return false;
    if (entry->m_presentImage && entry->m_presentBuffer &&
        entry->m_presentWidth == imgW && entry->m_presentHeight == imgH)
        return true;

    xpwn_releasePresentImage(entry);
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    /* RGB16(565)直拷视觉预判：depth-16 + 565 掩码时显式传行跨度
       w*2 —— XCreateImage 的自算值按 bitmap_pad=32 圆整为
       (w*2+3)&~3，与 2 字节/像素直拷缓冲不一致；bits_per_pixel/
       字节序待创建后复核（见下方 direct 判定）。 */
    if (depth == XPWN_DEPTH_16 &&
        visual->red_mask == 0x0000f800u &&
        visual->green_mask == 0x000007e0u &&
        visual->blue_mask == 0x0000001fu)
        wantBpl = imgW * 2;
#endif
    ximg = XCreateImage(g_xpwnDisplay, visual, depth, ZPixmap, 0,
                        NULL, imgW, imgH, 32, wantBpl);
    if (!ximg)
        return false;
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    /* RGB16 模式后备缓冲已是 565（2 字节/像素，小端 16 位单元）：
       仅当视觉同为 depth-16/565 且按小端读像素时逐行字节直通。
       depth-24/32 视觉需要 565->32 展开，本层当前不提供该转换，
       上屏直接失败 —— 绝不落入 24 位重排路径（xpwn_copyRect24 按
       4 字节/像素读取 565 缓冲会越界/花屏）。（该构建下 create 不
       改选弹层视觉，depth 恒等于进程选定深度，行为与既往一致。） */
    direct = depth == XPWN_DEPTH_16 &&
             ximg->bits_per_pixel == 16 && ximg->byte_order == LSBFirst &&
             ImageByteOrder(g_xpwnDisplay) == LSBFirst &&
             visual->red_mask == 0x0000f800u &&
             visual->green_mask == 0x000007e0u &&
             visual->blue_mask == 0x0000001fu;
    if (!direct) {
        ximg->data = NULL;
        XDestroyImage(ximg);
        return false;
    }
    bufBpl = imgW * 2;
#else
    /* 直拷条件：真 32 位像素 + 标准 BGRA8888 掩码 + 小端字节序（与
       ARGB32 小端内存布局 [B,G,R,A] 相同）。24 位深含 32 位填充的视觉
       也能直拷（高 8 位 Alpha 被服务器忽略）。真 24 位视觉（弹层默认
       视觉根修改选后）bits_per_pixel==24 → direct=false → 走
       xpwn_copyRect24 重排（ARGB32 源缓冲，合法）。 */
    direct = ximg->bits_per_pixel == 32 && ximg->byte_order == LSBFirst &&
             ImageByteOrder(g_xpwnDisplay) == LSBFirst &&
             visual->red_mask == 0x00ff0000u &&
             visual->green_mask == 0x0000ff00u &&
             visual->blue_mask == 0x000000ffu;
    bufBpl = direct ? imgW * 4 : (imgW * 3 + 3) & ~3;
#endif
    if (bufBpl <= 0 || (size_t)imgH > SIZE_MAX / (size_t)bufBpl) {
        ximg->data = NULL;
        XDestroyImage(ximg);
        return false;
    }
    bufferSize = (size_t)bufBpl * (size_t)imgH;
    buffer = (uint8_t*)XMalloc_Hybrid(bufferSize);
    if (!buffer) {
        ximg->data = NULL;
        XDestroyImage(ximg);
        return false;
    }
    ximg->data = (char*)buffer;
    entry->m_presentImage = ximg;
    entry->m_presentBuffer = buffer;
    entry->m_presentWidth = imgW;
    entry->m_presentHeight = imgH;
    entry->m_presentBytesPerLine = bufBpl;
    entry->m_presentDirect = direct;
    return true;
}

bool XPlatformNativeWindow_present(XWindow* window, const XImage* image,
                                   const XRegion* region,
                                   const XPoint* offset)
{
    XWNPendingEntry* entry;
    X11_XImage* ximg;
    XPoint zero;
    const XPoint* off;
    XRect full;
    const XRect* rects;
    int rectCount;
    bool direct;
    uint8_t* buffer;
    int bufBpl;
    int imgW, imgH;
    int i;
    bool any = false;
    if (!window || !image) return false;
    if (!xpwn_ensureConnection()) return false;
    entry = xpwn_findByXWindow(window);
    if (!entry || !entry->m_win) return false;
    imgW = XImage_width(image);
    imgH = XImage_height(image);
    if (imgW <= 0 || imgH <= 0) return false;
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    /* RGB16 模式契约：后备缓冲即 RGB16/565。非 RGB16 输入属配置错配，
       拒绝上屏（copyRect16 按 2 字节/像素搬运，误拷 4 字节缓冲会越界）。 */
    if (XImage_format(image) != XImageFormat_RGB16) return false;
#endif

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

    if (!xpwn_preparePresentImage(entry, imgW, imgH)) {
        return false;
    }
    ximg = entry->m_presentImage;
    buffer = entry->m_presentBuffer;
    bufBpl = entry->m_presentBytesPerLine;
    direct = entry->m_presentDirect;
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
        drect.width = srect.width;
        drect.height = srect.height;
        if (direct) {
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
            /* RGB16 模式：direct 仅在「缓冲 565 + 视觉 depth-16/565」
               时成立（见 xpwn_preparePresentImage），按 2 字节/像素
               直拷进 ZPixmap 缓冲后提交。 */
            xpwn_copyRect16(image, &srect, buffer, bufBpl);
#else
            xpwn_copyRectDirect(image, &srect, buffer, bufBpl);
#endif
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
