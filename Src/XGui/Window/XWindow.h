/******************************************************************************
 * @file       XWindow.h
 * @brief      XWindow 顶层窗口类（对标 Qt 6.8 QWindow，实现全部公开 API）。
 * @details    XWindow 继承 XObject，表示窗口系统中的一层顶层窗口，提供：
 *             窗口类型/标志（全部 Qt::WindowType 位标志）、可见性与三态
 *             Visibility、窗口状态（最小化/最大化/全屏/激活）、几何与
 *             位置（含 frameMargins/frameGeometry）、尺寸约束（最小/最大/
 *             基准尺寸与步进）、表面格式（XSurfaceFormat）、透明度、遮罩
 *             （XRegion）、图标、标题、文件路径、模态、瞬态父窗口、屏幕
 *             归属、设备像素比、内容方向、光标、键盘/鼠标抓取、全局坐标
 *             映射、19 个通知信号、21 个事件分派入口（对标 QWindow 全部
 *             protected 事件虚函数）。本模块不依赖任何平台 API，窗口属性
 *             全部程序化配置；平台句柄 XWindowPlatform* 保持不透明借用手
 *             针，未来由 XGuiApplication/平台后端接入真实窗口系统。
 * @note       模块总开关 XWINDOW_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XWindow 公共 API。依赖子开关 XSURFACEFORMAT_ON /
 *             XSCREEN_ON / XCURSOR_ON，关闭时对应子能力退化为空实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XWINDOW_H
#define XWINDOW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XGeometry.h"
#include "XEvent.h"
#include "XString.h"
#include "XIcon.h"
#include "XSurfaceFormat.h"
#include "XScreen.h"
#include "XMemory.h"

#if XCURSOR_ON
#include "XCursor.h"
#endif

#if XWINDOW_ON

/* ==================== 依赖子模块回退定义 ==================== */

/**
 * @brief      屏幕方向枚举回退定义（对标 Qt::ScreenOrientation）。
 * @details    仅当 XSCREEN_ON=0、XScreen 被裁剪时启用，保证 XWindowOrientation
 *             别名与 contentOrientation 相关 API 仍可编译；枚举数值与
 *             XScreenOrientation（1/2/4/8）完全一致。
 */
#if !XSCREEN_ON
typedef enum XScreenOrientation
{
    XScreenOrientation_Primary = 0,          /**< 主方向占位值。 */
    XScreenOrientation_Portrait = 1,         /**< 竖屏方向。 */
    XScreenOrientation_Landscape = 2,        /**< 横屏方向。 */
    XScreenOrientation_InvertedPortrait = 4, /**< 倒置竖屏方向。 */
    XScreenOrientation_InvertedLandscape = 8 /**< 倒置横屏方向。 */
} XScreenOrientation;
#endif /* !XSCREEN_ON */

/**
 * @brief      表面格式取值类型回退定义（对标 QSurfaceFormat）。
 * @details    仅当 XSURFACEFORMAT_ON=0、XSurfaceFormat 被裁剪时启用，
 *             字段名与 XSurfaceFormat 保持一致，使 XWindow_setFormat/
 *             format/requestedFormat 保持可编译；此时各缓冲字段恒为
 *             -1（未指定）、m_options 为 0，格式合成按默认值处理。XWindow.c
 *             仅在 XSURFACEFORMAT_ON=1 时调用 XSurfaceFormat 的真实 API。
 */
#if !XSURFACEFORMAT_ON
typedef uint32_t XSurfaceFormatOptions;
typedef uint32_t XSurfaceFormatSwapBehavior;
typedef uint32_t XSurfaceFormatRenderableType;
typedef uint32_t XSurfaceFormatProfile;
typedef struct XSurfaceFormat
{
    XSurfaceFormatOptions   m_options;          /**< 格式选项位组合；恒为 0。 */
    int                     m_redBufferSize;    /**< 红色缓冲位深；恒为 -1。 */
    int                     m_greenBufferSize;  /**< 绿色缓冲位深；恒为 -1。 */
    int                     m_blueBufferSize;   /**< 蓝色缓冲位深；恒为 -1。 */
    int                     m_alphaBufferSize;  /**< 透明缓冲位深；恒为 -1。 */
    int                     m_depthBufferSize;  /**< 深度缓冲大小；恒为 -1。 */
    int                     m_stencilBufferSize;/**< 模板缓冲大小；恒为 -1。 */
    int                     m_samples;          /**< 多重采样数；恒为 -1。 */
    XSurfaceFormatSwapBehavior   m_swapBehavior;    /**< 交换行为；恒为 0。 */
    XSurfaceFormatRenderableType m_renderableType;  /**< 可渲染类型；恒为 0。 */
    XSurfaceFormatProfile        m_profile;         /**< OpenGL profile；恒为 0。 */
    int                     m_majorVersion;     /**< OpenGL 主版本；恒为 2。 */
    int                     m_minorVersion;     /**< OpenGL 次版本；恒为 0。 */
    int                     m_swapInterval;     /**< 交换间隔；恒为 1。 */
    bool                    m_stereo;           /**< 是否启用立体缓冲；恒为 false。 */
    XColorSpace             m_colorSpace;       /**< 色彩空间；恒为 Unknown。 */
} XSurfaceFormat;
#endif /* !XSURFACEFORMAT_ON */

/* ==================== 虚函数表（对标 QWindow protected 虚函数） ==================== */

/**
 * @brief XWindow 虚函数表枚举。
 * @details 前 3 个槽位继承自 XClass（Copy/Move/Deinit）；XObject 的
 *          Event/EventFilter/ChildEvent/... 槽位由 XVTABLE_INHERIT_XCLASS
 *          (XObject) 继承，其中 EXWindow_Event 沿用 XObject 的 Event 槽位
 *          （XWindow 在 class_init 中重载为默认事件分发器，对标
 *          QWindow::event）；下述 21 个新槽位从 XCLASS_VTABLE_GET_SIZE
 *          (XObject) 开始追加，分别对标 QWindow 的 exposeEvent /
 *          resizeEvent / ... / nativeEvent。
 */
XCLASS_DEFINE_BEGING(XWindow)
XCLASS_DEFINE_ENUM(XWindow, ExposeEvent) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XWindow, ResizeEvent),
XCLASS_DEFINE_ENUM(XWindow, PaintEvent),
XCLASS_DEFINE_ENUM(XWindow, MoveEvent),
XCLASS_DEFINE_ENUM(XWindow, FocusInEvent),
XCLASS_DEFINE_ENUM(XWindow, FocusOutEvent),
XCLASS_DEFINE_ENUM(XWindow, ShowEvent),
XCLASS_DEFINE_ENUM(XWindow, HideEvent),
XCLASS_DEFINE_ENUM(XWindow, CloseEvent),
XCLASS_DEFINE_ENUM(XWindow, KeyPressEvent),
XCLASS_DEFINE_ENUM(XWindow, KeyReleaseEvent),
XCLASS_DEFINE_ENUM(XWindow, MousePressEvent),
XCLASS_DEFINE_ENUM(XWindow, MouseReleaseEvent),
XCLASS_DEFINE_ENUM(XWindow, MouseDoubleClickEvent),
XCLASS_DEFINE_ENUM(XWindow, MouseMoveEvent),
XCLASS_DEFINE_ENUM(XWindow, WheelEvent),
XCLASS_DEFINE_ENUM(XWindow, TouchEvent),
XCLASS_DEFINE_ENUM(XWindow, TabletEvent),
XCLASS_DEFINE_ENUM(XWindow, InputMethodEvent),
XCLASS_DEFINE_ENUM(XWindow, DragEnterEvent),
XCLASS_DEFINE_ENUM(XWindow, DragMoveEvent),
XCLASS_DEFINE_ENUM(XWindow, DragLeaveEvent),
XCLASS_DEFINE_ENUM(XWindow, DropEvent),
XCLASS_DEFINE_ENUM(XWindow, NativeEvent),
XCLASS_DEFINE_ENUM(XWindow, EnterEvent),
XCLASS_DEFINE_ENUM(XWindow, LeaveEvent),
XCLASS_DEFINE_END(XWindow)

/* ==================== 枚举类型（对标 QWindow 与 Qt::WindowType 等） ==================== */

/** @brief 窗口对用户的可见状态（对标 QWindow::Visibility）。 */
typedef enum XWindowVisibility
{
    XWindowVisibility_Hidden = 0,        /**< 窗口已隐藏。 */
    XWindowVisibility_Automatic,         /**< 自动可见（顶层同 Windowed）。 */
    XWindowVisibility_Windowed,          /**< 窗口化显示。 */
    XWindowVisibility_Minimized,         /**< 最小化。 */
    XWindowVisibility_Maximized,         /**< 最大化。 */
    XWindowVisibility_FullScreen         /**< 全屏。 */
} XWindowVisibility;

/** @brief 祖先查询模式（对标 QWindow::AncestorMode）。 */
typedef enum XWindowAncestorMode
{
    XWindowAncestor_ExcludeTransients = 0, /**< 不把瞬态父链计入祖先。 */
    XWindowAncestor_IncludeTransients  = 1  /**< 把瞬态父链计入祖先。 */
} XWindowAncestorMode;

/** @brief 表面类别（对标 QSurface::SurfaceClass）。 */
typedef enum XWindowSurfaceClass
{
    XWindowSurfaceClass_Window = 0,      /**< 窗口表面。 */
    XWindowSurfaceClass_Offscreen        /**< 离屏表面。 */
} XWindowSurfaceClass;

/** @brief 表面类型（对标 QSurface::SurfaceType）。 */
typedef enum XWindowSurfaceType
{
    XWindowSurface_Raster = 0,           /**< 软件光栅表面。 */
    XWindowSurface_OpenGL,               /**< OpenGL 表面。 */
    XWindowSurface_RasterGL,             /**< 软件光栅 + OpenGL 混合表面。 */
    XWindowSurface_OpenVG,               /**< OpenVG 表面。 */
    XWindowSurface_Vulkan,               /**< Vulkan 表面。 */
    XWindowSurface_Metal,                /**< Metal 表面。 */
    XWindowSurface_Direct3D              /**< Direct3D 表面。 */
} XWindowSurfaceType;

/** @brief 窗口类型位标志（对标 Qt 6.8 Qt::WindowType，数值完全一致）。
 * @details 类型位与风格位可组合；窗口的具体类型 = flags & WindowType_Mask。 */
typedef enum XWindowType
{
    XWindowType_Widget                      = 0x00000000, /**< Widget 类型（无窗口外壳）。 */
    XWindowType_Window                      = 0x00000001, /**< 普通顶层窗口。 */
    XWindowType_Dialog                      = 0x00000003, /**< 对话框窗口。 */
    XWindowType_Sheet                       = 0x00000005, /**< 工作表窗口（macOS）。 */
    XWindowType_Drawer                      = 0x00000007, /**< 抽屉窗口（macOS）。 */
    XWindowType_Popup                       = 0x00000009, /**< 弹出窗口。 */
    XWindowType_Tool                        = 0x0000000b, /**< 工具窗口。 */
    XWindowType_ToolTip                     = 0x00000013, /**< 工具提示窗口。 */
    XWindowType_SplashScreen                = 0x00000017, /**< 启动画面窗口。 */
    XWindowType_Desktop                     = 0x00000019, /**< 桌面窗口。 */
    XWindowType_SubWindow                   = 0x00000012, /**< 子窗口。 */
    XWindowType_ForeignWindow               = 0x00000021, /**< 外部窗口（由外来窗口系统管理）。 */
    XWindowType_CoverWindow                 = 0x00000041, /**< 覆盖窗口。 */
    XWindowType_TypeMask                    = 0x000000ff, /**< 类型位掩码。 */
    XWindowType_MSWindowsFixedSizeDialogHint= 0x00000100, /**< Windows 固定大小对话框提示。 */
    XWindowType_MSWindowsOwnDC              = 0x00000200, /**< Windows 独立设备上下文提示。 */
    XWindowType_BypassWindowManagerHint     = 0x00000400, /**< 绕过窗口管理器提示。 */
    XWindowType_FramelessWindowHint         = 0x00000800, /**< 无边框窗口提示。 */
    XWindowType_WindowTitleHint             = 0x00001000, /**< 显示标题栏提示。 */
    XWindowType_WindowSystemMenuHint        = 0x00002000, /**< 显示系统菜单提示。 */
    XWindowType_WindowMinimizeButtonHint    = 0x00004000, /**< 显示最小化按钮提示。 */
    XWindowType_WindowMaximizeButtonHint    = 0x00008000, /**< 显示最大化按钮提示。 */
    XWindowType_WindowMinMaxButtonsHint     = 0x0000c000, /**< 同时显示最小化/最大化按钮。 */
    XWindowType_WindowContextHelpButtonHint = 0x00010000, /**< 显示上下文帮助按钮提示。 */
    XWindowType_WindowShadeButtonHint       = 0x00020000, /**< 显示阴影按钮提示（macOS）。 */
    XWindowType_WindowStaysOnTopHint        = 0x00040000, /**< 窗口置顶提示。 */
    XWindowType_WindowTransparentForInput   = 0x00080000, /**< 窗口穿透鼠标输入提示。 */
    XWindowType_WindowOverridesSystemGestures = 0x00100000, /**< 覆盖系统手势提示。 */
    XWindowType_WindowDoesNotAcceptFocus    = 0x00200000, /**< 窗口不接受键盘焦点提示。 */
    XWindowType_MaximizeUsingFullscreenGeometryHint = 0x00400000, /**< 最大化使用全屏几何提示。 */
    XWindowType_CustomizeWindowHint         = 0x02000000, /**< 完全自定义窗口外观提示。 */
    XWindowType_WindowStaysOnBottomHint     = 0x04000000, /**< 窗口置底提示。 */
    XWindowType_WindowCloseButtonHint       = 0x08000000, /**< 显示关闭按钮提示。 */
    XWindowType_MacWindowToolBarButtonHint  = 0x10000000, /**< macOS 工具栏按钮提示。 */
    XWindowType_BypassGraphicsProxyWidget   = 0x20000000, /**< 绕过图形代理控件提示。 */
    XWindowType_NoDropShadowWindowHint      = 0x40000000, /**< 无窗口阴影提示。 */
    XWindowType_WindowFullscreenButtonHint  = 0x80000000  /**< 显示全屏按钮提示。 */
} XWindowType;

/** @brief 窗口标志位组合（对标 Qt::WindowFlags）。 */
typedef uint32_t XWindowFlags;

/** @brief 窗口状态位（对标 Qt::WindowState）。 */
typedef enum XWindowState
{
    XWindowState_NoState    = 0x00000000, /**< 普通状态。 */
    XWindowState_Minimized  = 0x00000001, /**< 最小化。 */
    XWindowState_Maximized  = 0x00000002, /**< 最大化。 */
    XWindowState_FullScreen = 0x00000004, /**< 全屏。 */
    XWindowState_Active     = 0x00000008  /**< 窗口激活（不可用于 setWindowStates）。 */
} XWindowState;

/** @brief 窗口状态位组合（对标 Qt::WindowStates）。 */
typedef uint32_t XWindowStates;

/** @brief 窗口模态（对标 Qt::WindowModality）。 */
typedef enum XWindowModality
{
    XWindowModality_NonModal = 0,        /**< 非模态。 */
    XWindowModality_WindowModal,         /**< 窗口模态（阻塞父窗口）。 */
    XWindowModality_ApplicationModal     /**< 应用模态（阻塞整个应用）。 */
} XWindowModality;

/** @brief 屏幕方向别名：XWindow 复用 XScreenOrientation（对标 Qt::ScreenOrientation）。 */
typedef XScreenOrientation XWindowOrientation;

/** @brief 系统窗口边缘（对标 Qt::Edges，用于 startSystemResize）。 */
typedef enum XWindowEdges
{
    XWindowEdge_Top    = 0x00001,       /**< 上边缘。 */
    XWindowEdge_Left   = 0x00002,       /**< 左边缘。 */
    XWindowEdge_Right  = 0x00004,       /**< 右边缘。 */
    XWindowEdge_Bottom = 0x00008        /**< 下边缘。 */
} XWindowEdges;

/** @brief 窗口 id（WId）类型；XScreen 已定义时复用，避免重定义。 */
#ifndef XWINDOWID_DEFINED
#define XWINDOWID_DEFINED 1
typedef uintptr_t XWindowId;
#endif

/** @brief 平台窗口句柄（对标 QWindow::handle() 返回的 QPlatformWindow*）。
 * @details 不透明借用指针，XWindow 不拥有、不释放它；无平台后端时为 NULL。 */
typedef struct XWindowPlatform XWindowPlatform;

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XWindowPrivate XWindowPrivate;

/**
 * @brief      XWindow 窗口对象；m_class 必须是第一个成员且禁止手工修改。
 * @details    窗口属性快照保存在 m_data 中；m_data 由 XWindow 拥有，
 *             调用方不得直接访问或释放。
 */
typedef struct XWindow
{
    XObject         m_class; /**< 第一个成员，由 XObject 管理，禁止手工修改。 */
    XWindowPrivate* m_data;  /**< 私有属性快照，由 XWindow 拥有，仅供实现访问。 */
} XWindow;

/** @brief 窗口事件槽函数签名；所有具体事件统一以 XEvent* 承载。 */
typedef void (*XWindowEventSlot)(XWindow* self, XEvent* event);

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化 XWindow 类虚函数表并返回共享表指针。
 * @return     XWindow 类的共享 XVtable 指针。
 */
XVtable* XWindow_class_init(void);

/**
 * @brief      初始化空 XWindow（对标 QWindow() 默认构造语义）。
 * @details    初始属性：几何 (0,0,0,0)、可见性 Hidden、窗口类型 Window、
 *             表面类型 Raster、透明度 1.0、设备像素比 1.0、内容方向 Primary、
 *             最小尺寸 (0,0)、最大尺寸 (16777215,16777215)、基准尺寸与
 *             步进 (0,0)、模态 NonModal、无父窗口/瞬态父窗口、标题与
 *             文件路径为空、平台句柄 NULL、无遮罩、光标为空、默认表面
 *             格式（XSurfaceFormat_defaultFormat）；该值反映进程级默认表面
 *             格式，窗口创建后可通过 XWindow_setFormat 覆盖。
 * @param      self 待初始化的对象指针；生命周期结束时必须成对调用
 *             XWindow_deinit_base。
 */
void XWindow_init(XWindow* self);

/**
 * @brief      以指定父窗口初始化 XWindow（对标 QWindow(QWindow* parent)）。
 * @details    父窗口借用不持有；设置父窗口后成为非顶层窗口。
 * @param      self 待初始化的对象指针。
 * @param      parent 父窗口；可为 NULL 表示顶层窗口。
 */
void XWindow_init_parent(XWindow* self, XWindow* parent);

/**
 * @brief      通过 XClass 虚表释放 XWindow 资源（栈/外部存储对象使用）。
 */
#define XWindow_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上的 XWindow 对象（虚表调用析构语义）。 */
#define XWindow_delete_base(self) XClass_delete_base((XClass*)(self))
/** @brief 深拷贝 XWindow（对标 QWindow 复制值语义的属性快照）；未初始化目标自动初始化。 */
#define XWindow_copy_base(self, other) XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动 XWindow；移动后源对象为空窗口（保留已绑定的事件连接语义除外）。 */
#define XWindow_move_base(self, other) XClass_move_base((XClass*)(self), (XClass*)(other))

/** @brief 使用默认内存类型在堆上创建空 XWindow。 @return 新对象指针；失败返回 NULL。 */
#define XWindow_create() XWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建空 XWindow。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL，调用方用 XWindow_delete_base 释放。
 */
XWindow* XWindow_create_ex(XMemoryType memory);

/**
 * @brief      深拷贝创建 XWindow（对标 QWindow 拷贝语义的属性快照）。
 * @param      other 源窗口；可为 NULL。
 * @return     新对象指针；分配失败返回 NULL，调用方负责释放。
 */
XWindow* XWindow_create_copy(const XWindow* other);

/**
 * @brief      移动创建 XWindow；移动后源对象为空窗口。
 * @param      other 源窗口；可为 NULL。
 * @return     新对象指针；分配失败返回 NULL，调用方负责释放。
 */
XWindow* XWindow_create_move(XWindow* other);

/* ==================== 表面与窗口标识（对标 QSurface 部分） ==================== */

/**
 * @brief      设置窗口表面类型（对标 QWindow::setSurfaceType）。
 * @param      self 目标窗口；可为 NULL。
 * @param      surfaceType 表面类型。
 */
void XWindow_setSurfaceType(XWindow* self, XWindowSurfaceType surfaceType);

/**
 * @brief      返回窗口表面类型（对标 QWindow::surfaceType）。
 * @param      self 目标窗口；可为 NULL。
 * @return     表面类型；入参非法返回 Raster。
 */
XWindowSurfaceType XWindow_surfaceType(const XWindow* self);

/**
 * @brief      返回窗口表面类别（对标 QSurface::surfaceClass）。
 * @param      self 目标窗口；可为 NULL。
 * @return     Window 表面类别。
 */
XWindowSurfaceClass XWindow_surfaceClass(const XWindow* self);

/**
 * @brief      判断表面是否支持 OpenGL（对标 QSurface::supportsOpenGL）。
 * @details    无平台后端时按表面类型推断：OpenGL/RasterGL 系列返回 true。
 * @param      self 目标窗口；可为 NULL。
 * @return     支持返回 true；入参非法返回 false。
 */
bool XWindow_supportsOpenGL(const XWindow* self);

/* ==================== 可见性与创建 ==================== */

/**
 * @brief      返回窗口当前是否可见（对标 QWindow::isVisible）。
 * @param      self 目标窗口；可为 NULL。
 * @return     可见返回 true；入参非法返回 false。
 */
bool XWindow_isVisible(const XWindow* self);

/**
 * @brief      返回窗口可见性（对标 QWindow::visibility）。
 * @param      self 目标窗口；可为 NULL。
 * @return     可见性枚举；入参非法返回 Hidden。
 */
XWindowVisibility XWindow_visibility(const XWindow* self);

/**
 * @brief      设置窗口可见性（对标 QWindow::setVisibility）。
 * @details    与 Qt 完全一致：Hidden→hide()；AutomaticVisibility→show()；
 *             Windowed→showNormal()；Minimized→showMinimized()；
 *             Maximized→showMaximized()；FullScreen→showFullScreen()。
 * @param      self 目标窗口；可为 NULL。
 * @param      visibility 目标可见性。
 */
void XWindow_setVisibility(XWindow* self, XWindowVisibility visibility);

/**
 * @brief      创建平台窗口资源（对标 QWindow::create）。
 * @details    由窗口内部向当前 XGuiApplication 的平台集成层请求并挂接平台
 *             窗口；无平台后端时分配递增的窗口 id（占位）。窗口仍保持隐藏，
 *             直到 setVisible(true)/show()。已创建时是 no-op。命名避开构造
 *             函数宏 XWindow_create()，采用 _createHandle 下划线后缀。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_createHandle(XWindow* self);

/**
 * @brief      返回窗口 id（对标 QWindow::winId）。
 * @details    未创建时自动创建；平台窗口创建失败返回 0。
 * @param      self 目标窗口；可为 NULL。
 * @return     窗口 id。
 */
XWindowId XWindow_winId(const XWindow* self);

/* ==================== 父窗口与顶层判断 ==================== */

/**
 * @brief      返回父窗口（对标 QWindow::parent(AncestorMode)）。
 * @details    复用 XWindowAncestorMode 枚举语义：ExcludeTransients
 *             直接返回普通父窗口；IncludeTransients 时若普通父窗口为 NULL
 *             则返回瞬态父窗口（Qt 语义：parent(IncludeTransients) 优先普通
 *             父窗口，其次瞬态父窗口）。
 * @param      self 目标窗口；可为 NULL。
 * @param      mode 祖先查询模式。
 * @return     父窗口借用指针；无父窗口或入参非法返回 NULL。
 */
XWindow* XWindow_parent(const XWindow* self, XWindowAncestorMode mode);

/**
 * @brief      设置父窗口（对标 QWindow::setParent）。
 * @details    与 Qt 6.8 QWindow::setParent 一致：父窗口借用不持有，仅拒绝
 *             Desktop 类型的父窗口（按 Qt 语义视为 NULL 使其成为顶层）；
 *             本窗口随之成为非顶层。不自动隐藏、不清空瞬态父窗口；若窗口
 *             当前可见且（父为 NULL 或父已创建平台句柄）则重新应用可见性。
 * @param      self 目标窗口；可为 NULL。
 * @param      parent 新父窗口；NULL 表示成为顶层窗口。
 */
void XWindow_setParent(XWindow* self, XWindow* parent);

/**
 * @brief      判断窗口是否为顶层窗口（对标 QWindow::isTopLevel）。
 * @param      self 目标窗口；可为 NULL。
 * @return     是顶层返回 true；入参非法返回 false。
 */
bool XWindow_isTopLevel(const XWindow* self);

/* ==================== 模态 ==================== */

/**
 * @brief      判断窗口当前是否为模态（对标 QWindow::isModal）。
 * @param      self 目标窗口；可为 NULL。
 * @return     modality 非 NonModal 返回 true。
 */
bool XWindow_isModal(const XWindow* self);

/**
 * @brief      返回窗口模态（对标 QWindow::modality）。
 * @param      self 目标窗口；可为 NULL。
 * @return     模态枚举；入参非法返回 NonModal。
 */
XWindowModality XWindow_modality(const XWindow* self);

/**
 * @brief      设置窗口模态（对标 QWindow::setModality）。
 * @details    变化时发射 modalityChanged 信号。
 * @param      self 目标窗口；可为 NULL。
 * @param      modality 目标模态。
 */
void XWindow_setModality(XWindow* self, XWindowModality modality);

/* ==================== 表面格式 ==================== */

/**
 * @brief      设置窗口请求的表面格式（对标 QWindow::setFormat）。
 * @param      self 目标窗口；可为 NULL。
 * @param      format 表面格式指针；NULL 按默认格式处理。
 */
void XWindow_setFormat(XWindow* self, const XSurfaceFormat* format);

/**
 * @brief      返回窗口当前生效的表面格式（对标 QWindow::format）。
 * @details    请求格式中未显式设置的字段回退到进程级默认格式
 *             （XSurfaceFormat_defaultFormat）。
 * @param      self 目标窗口；可为 NULL。
 * @return     生效格式值。
 */
XSurfaceFormat XWindow_format(const XWindow* self);

/**
 * @brief      返回窗口请求的表面格式（对标 QWindow::requestedFormat）。
 * @param      self 目标窗口；可为 NULL。
 * @return     请求格式值。
 */
XSurfaceFormat XWindow_requestedFormat(const XWindow* self);

/* ==================== 窗口标志与类型 ==================== */

/**
 * @brief      设置窗口标志（对标 QWindow::setFlags）。
 * @param      self 目标窗口；可为 NULL。
 * @param      flags 窗口标志位组合。
 */
void XWindow_setFlags(XWindow* self, XWindowFlags flags);

/**
 * @brief      返回窗口标志（对标 QWindow::flags）。
 * @param      self 目标窗口；可为 NULL。
 * @return     窗口标志位组合；入参非法返回 0。
 */
XWindowFlags XWindow_flags(const XWindow* self);

/**
 * @brief      按位设置/清除单个窗口标志（对标 QWindow::setFlag）。
 * @param      self 目标窗口；可为 NULL。
 * @param      flag 单个标志位。
 * @param      on true 设置、false 清除。
 */
void XWindow_setFlag(XWindow* self, XWindowType flag, bool on);

/**
 * @brief      返回窗口类型（对标 QWindow::type，即 flags & WindowType_Mask）。
 * @param      self 目标窗口；可为 NULL。
 * @return     窗口类型；入参非法返回 Widget。
 */
XWindowType XWindow_type(const XWindow* self);

/* ==================== 标题 ==================== */

/**
 * @brief      返回窗口标题（对标 QWindow::title；返回深拷贝，调用方负责释放）。
 * @param      self 目标窗口；可为 NULL。
 * @return     新 XString；失败返回空字符串对象，调用方用
 *             XString_delete_base 释放。
 */
XString* XWindow_title(const XWindow* self);

/**
 * @brief      以 XString 设置窗口标题（对标 QWindow::setTitle）。
 * @details    标题变化时发射 windowTitleChanged 信号。
 * @param      self 目标窗口；可为 NULL。
 * @param      title 新标题；NULL 视为清空。
 */
void XWindow_setTitle(XWindow* self, const XString* title);

/**
 * @brief      以 UTF-8 字符串设置窗口标题（兼容重载）。
 * @param      self 目标窗口；可为 NULL。
 * @param      title UTF-8 编码标题；NULL 视为清空。
 */
void XWindow_setTitle_2(XWindow* self, const char* title);

/* ==================== 透明度与遮罩 ==================== */

/**
 * @brief      设置窗口不透明度（对标 QWindow::setOpacity）。
 * @details    取值范围 [0,1]；变化时发射 opacityChanged 信号。
 * @param      self 目标窗口；可为 NULL。
 * @param      level 不透明度。
 */
void XWindow_setOpacity(XWindow* self, float level);

/**
 * @brief      返回窗口不透明度（对标 QWindow::opacity）。
 * @param      self 目标窗口；可为 NULL。
 * @return     不透明度；入参非法返回 1.0。
 */
float XWindow_opacity(const XWindow* self);

/**
 * @brief      设置窗口形状遮罩（对标 QWindow::setMask）。
 * @details    遮罩以 XRegion 深拷贝存储；传入 NULL 表示清除遮罩。
 * @param      self 目标窗口；可为 NULL。
 * @param      region 遮罩区域；可为 NULL 清除。
 */
void XWindow_setMask(XWindow* self, const XRegion* region);

/**
 * @brief      返回窗口遮罩（对标 QWindow::mask，输出到调用方提供的 XRegion）。
 * @details    out 必须由调用方先初始化（XRegion_init）或已 deinit；
 *             函数内部先清空再填充拷贝。
 * @param      self 目标窗口；可为 NULL。
 * @param      out 输出目标；可为 NULL 忽略。
 */
void XWindow_mask(const XWindow* self, XRegion* out);

/* ==================== 激活 / 内容方向 / 设备像素比 ==================== */

/**
 * @brief      判断窗口是否激活（对标 QWindow::isActive）。
 * @param      self 目标窗口；可为 NULL。
 * @return     激活返回 true；入参非法返回 false。
 */
bool XWindow_isActive(const XWindow* self);

/**
 * @brief      报告窗口内容方向变化（对标 QWindow::reportContentOrientationChange）。
 * @details    方向变化时发射 contentOrientationChanged 信号。
 * @param      self 目标窗口；可为 NULL。
 * @param      orientation 新内容方向。
 */
void XWindow_reportContentOrientationChange(XWindow* self,
                                            XScreenOrientation orientation);

/**
 * @brief      返回窗口内容方向（对标 QWindow::contentOrientation）。
 * @param      self 目标窗口；可为 NULL。
 * @return     内容方向；入参非法返回 Primary。
 */
XScreenOrientation XWindow_contentOrientation(const XWindow* self);

/**
 * @brief      返回窗口设备像素比（对标 QWindow::devicePixelRatio）。
 * @param      self 目标窗口；可为 NULL。
 * @return     设备像素比；入参非法返回 1.0。
 */
float XWindow_devicePixelRatio(const XWindow* self);

/* ==================== 窗口状态 ==================== */

/**
 * @brief      返回窗口生效状态（对标 QWindow::windowState）。
 * @details    生效状态按优先级取：Minimized > FullScreen > Maximized > NoState。
 * @param      self 目标窗口；可为 NULL。
 * @return     生效状态；入参非法返回 NoState。
 */
XWindowState XWindow_windowState(const XWindow* self);

/**
 * @brief      返回窗口状态组合（对标 QWindow::windowStates）。
 * @param      self 目标窗口；可为 NULL。
 * @return     状态位组合；入参非法返回 0。
 */
XWindowStates XWindow_windowStates(const XWindow* self);

/**
 * @brief      设置窗口状态（对标 QWindow::setWindowState）。
 * @param      self 目标窗口；可为 NULL。
 * @param      state 目标状态。
 */
void XWindow_setWindowState(XWindow* self, XWindowState state);

/**
 * @brief      设置窗口状态组合（对标 QWindow::setWindowStates）。
 * @details    拒绝 WindowActive 位（自动清除并忽略）；生效状态变化时发射
 *             windowStateChanged 并更新可见性。
 * @param      self 目标窗口；可为 NULL。
 * @param      states 状态位组合。
 */
void XWindow_setWindowStates(XWindow* self, XWindowStates states);

/* ==================== 瞬态父窗口与祖先查询 ==================== */

/**
 * @brief      设置瞬态父窗口（对标 QWindow::setTransientParent）。
 * @details    与 Qt 6.8 QWindow::setTransientParent 一致：只拒绝「非顶层
 *             窗口」与「自身」作为瞬态父窗口；其余入参（含顶层普通父窗口）
 *             直接接受。变化时无条件发射 transientParentChanged 信号。
 * @param      self 目标窗口；可为 NULL。
 * @param      parent 新瞬态父窗口；NULL 清除（保持弹出语义，被动隐藏）。
 */
void XWindow_setTransientParent(XWindow* self, XWindow* parent);

/**
 * @brief      返回瞬态父窗口（对标 QWindow::transientParent）。
 * @param      self 目标窗口；可为 NULL。
 * @return     瞬态父窗口借用指针；无或入参非法返回 NULL。
 */
XWindow* XWindow_transientParent(const XWindow* self);

/**
 * @brief      判断窗口是否为指定子窗口的祖先（对标 QWindow::isAncestorOf）。
 * @param      self 目标窗口；可为 NULL。
 * @param      child 待判断的子窗口；可为 NULL。
 * @param      mode 祖先查询模式（是否包含瞬态父链）。
 * @return     是祖先返回 true。
 */
bool XWindow_isAncestorOf(const XWindow* self, const XWindow* child,
                          XWindowAncestorMode mode);

/**
 * @brief      判断窗口是否已暴露（对标 QWindow::isExposed）。
 * @param      self 目标窗口；可为 NULL。
 * @return     已暴露返回 true；入参非法返回 false。
 */
bool XWindow_isExposed(const XWindow* self);

/**
 * @brief      设置窗口暴露状态（对标 QWindowPrivate::exposed）。
 * @details    该状态表示「窗口是否真的在屏幕上可见」，由平台后端在收到
 *             平台暴露/隐藏事件时经 XWindowSystemInterface_handleExposeEvent
 *             同步更新：收到非空暴露区域时置 true，收到空区域（整窗隐藏）
 *             时置 false。XWindow_setVisible 只在无平台后端时自行维护该
 *             状态以保证离线 API 语义一致；有平台后端时以后端注入为准。
 * @param      self 目标窗口；可为 NULL。
 * @param      exposed 新的暴露状态；false 表示窗口已完全隐藏。
 */
void XWindow_setExposed(XWindow* self, bool exposed);

/* ==================== 尺寸约束 ==================== */

/**
 * @brief      返回窗口最小尺寸（对标 QWindow::minimumSize）。
 * @param      self 目标窗口；可为 NULL。
 * @return     最小尺寸；入参非法返回 (0,0)。
 */
XSize XWindow_minimumSize(const XWindow* self);

/**
 * @brief      返回窗口最大尺寸（对标 QWindow::maximumSize；默认 16777215²）。
 * @param      self 目标窗口；可为 NULL。
 * @return     最大尺寸；入参非法返回 (0,0)。
 */
XSize XWindow_maximumSize(const XWindow* self);

/**
 * @brief      返回窗口基准尺寸（对标 QWindow::baseSize）。
 * @param      self 目标窗口；可为 NULL。
 * @return     基准尺寸；入参非法返回 (0,0)。
 */
XSize XWindow_baseSize(const XWindow* self);

/**
 * @brief      返回窗口尺寸步进（对标 QWindow::sizeIncrement）。
 * @param      self 目标窗口；可为 NULL。
 * @return     尺寸步进；入参非法返回 (0,0)。
 */
XSize XWindow_sizeIncrement(const XWindow* self);

/** @brief 返回窗口最小宽度（对标 QWindow::minimumWidth）。 @param self 目标窗口；可为 NULL。 @return 最小宽度。 */
int XWindow_minimumWidth(const XWindow* self);
/** @brief 返回窗口最小高度（对标 QWindow::minimumHeight）。 @param self 目标窗口；可为 NULL。 @return 最小高度。 */
int XWindow_minimumHeight(const XWindow* self);
/** @brief 返回窗口最大宽度（对标 QWindow::maximumWidth）。 @param self 目标窗口；可为 NULL。 @return 最大宽度。 */
int XWindow_maximumWidth(const XWindow* self);
/** @brief 返回窗口最大高度（对标 QWindow::maximumHeight）。 @param self 目标窗口；可为 NULL。 @return 最大高度。 */
int XWindow_maximumHeight(const XWindow* self);

/**
 * @brief      设置窗口最小尺寸（对标 QWindow::setMinimumSize）。
 * @details    使用 QWINDOWSIZE_MAX=16777215 裁剪（expandedTo(0,0) 语义）；
 *             变化时发射 minimumWidthChanged/minimumHeightChanged；若当前
 *             几何越界则自动 resize 夹紧。
 * @param      self 目标窗口；可为 NULL。
 * @param      size 新最小尺寸；NULL 按 (0,0) 处理。
 */
void XWindow_setMinimumSize(XWindow* self, const XSize* size);

/**
 * @brief      设置窗口最大尺寸（对标 QWindow::setMaximumSize）。
 * @details    使用 QWINDOWSIZE_MAX=16777215 裁剪（boundedTo 语义）；
 *             变化时发射 maximumWidthChanged/maximumHeightChanged。
 * @param      self 目标窗口；可为 NULL。
 * @param      size 新最大尺寸；NULL 按 QWINDOWSIZE_MAX 处理。
 */
void XWindow_setMaximumSize(XWindow* self, const XSize* size);

/**
 * @brief      设置窗口基准尺寸（对标 QWindow::setBaseSize）。
 * @param      self 目标窗口；可为 NULL。
 * @param      size 新基准尺寸；NULL 按 (0,0) 处理。
 */
void XWindow_setBaseSize(XWindow* self, const XSize* size);

/**
 * @brief      设置窗口尺寸步进（对标 QWindow::setSizeIncrement）。
 * @param      self 目标窗口；可为 NULL。
 * @param      size 新尺寸步进；NULL 按 (0,0) 处理。
 */
void XWindow_setSizeIncrement(XWindow* self, const XSize* size);

/** @brief 设置窗口最小宽度（对标 QWindow::setMinimumWidth）。 @param self 目标窗口；可为 NULL。 @param w 最小宽度。 */
void XWindow_setMinimumWidth(XWindow* self, int w);
/** @brief 设置窗口最小高度（对标 QWindow::setMinimumHeight）。 @param self 目标窗口；可为 NULL。 @param h 最小高度。 */
void XWindow_setMinimumHeight(XWindow* self, int h);
/** @brief 设置窗口最大宽度（对标 QWindow::setMaximumWidth）。 @param self 目标窗口；可为 NULL。 @param w 最大宽度。 */
void XWindow_setMaximumWidth(XWindow* self, int w);
/** @brief 设置窗口最大高度（对标 QWindow::setMaximumHeight）。 @param self 目标窗口；可为 NULL。 @param h 最大高度。 */
void XWindow_setMaximumHeight(XWindow* self, int h);

/* ==================== 几何与坐标 ==================== */

/**
 * @brief      返回窗口几何（对标 QWindow::geometry）。
 * @param      self 目标窗口；可为 NULL。
 * @return     几何矩形；入参非法返回 (0,0,0,0)。
 */
XRect XWindow_geometry(const XWindow* self);

/**
 * @brief      返回窗口边框边距（对标 QWindow::frameMargins）。
 * @details    无平台后端时返回零边距。
 * @param      self 目标窗口；可为 NULL。
 * @return     边距。
 */
XMargins XWindow_frameMargins(const XWindow* self);

/**
 * @brief      返回窗口框架几何（对标 QWindow::frameGeometry）。
 * @details    无平台后端时等于 geometry（零边框）。
 * @param      self 目标窗口；可为 NULL。
 * @return     框架几何。
 */
XRect XWindow_frameGeometry(const XWindow* self);

/**
 * @brief      返回窗口框架位置（对标 QWindow::framePosition）。
 * @param      self 目标窗口；可为 NULL。
 * @return     框架左上角坐标。
 */
XPoint XWindow_framePosition(const XWindow* self);

/**
 * @brief      设置窗口框架位置（对标 QWindow::setFramePosition）。
 * @details    无平台窗口时等价于 setPosition；几何变化发 x/yChanged。
 * @param      self 目标窗口；可为 NULL。
 * @param      point 新框架位置；NULL 按 (0,0) 处理。
 */
void XWindow_setFramePosition(XWindow* self, const XPoint* point);

/** @brief 返回窗口宽度（对标 QWindow::width）。 @param self 目标窗口；可为 NULL。 @return 几何宽度。 */
int XWindow_width(const XWindow* self);
/** @brief 返回窗口高度（对标 QWindow::height）。 @param self 目标窗口；可为 NULL。 @return 几何高度。 */
int XWindow_height(const XWindow* self);
/** @brief 返回窗口 X 坐标（对标 QWindow::x）。 @param self 目标窗口；可为 NULL。 @return 几何 X。 */
int XWindow_x(const XWindow* self);
/** @brief 返回窗口 Y 坐标（对标 QWindow::y）。 @param self 目标窗口；可为 NULL。 @return 几何 Y。 */
int XWindow_y(const XWindow* self);
/**
 * @brief      返回窗口尺寸（对标 QWindow::size）。
 * @param      self 目标窗口；可为 NULL。
 * @return     几何尺寸。
 */
XSize XWindow_size(const XWindow* self);
/**
 * @brief      返回窗口位置（对标 QWindow::position；几何左上角）。
 * @param      self 目标窗口；可为 NULL。
 * @return     几何左上角坐标。
 */
XPoint XWindow_position(const XWindow* self);

/**
 * @brief      按坐标点设置窗口位置（对标 QWindow::setPosition(QPoint)）。
 * @details    无平台窗口时直接更新几何并发射 x/yChanged。
 * @param      self 目标窗口；可为 NULL。
 * @param      pt 新位置；NULL 按 (0,0) 处理。
 */
void XWindow_setPosition(XWindow* self, const XPoint* pt);

/**
 * @brief      按整数坐标设置窗口位置（对标 QWindow::setPosition(int,int)）。
 * @param      self 目标窗口；可为 NULL。
 * @param      posx 新 X。
 * @param      posy 新 Y。
 */
void XWindow_setPosition_2(XWindow* self, int posx, int posy);

/**
 * @brief      按尺寸设置窗口大小（对标 QWindow::resize(QSize)）。
 * @param      self 目标窗口；可为 NULL。
 * @param      newSize 新尺寸；NULL 按 (0,0) 处理。
 */
void XWindow_resize(XWindow* self, const XSize* newSize);

/**
 * @brief      按宽高设置窗口大小（对标 QWindow::resize(int,int)）。
 * @param      self 目标窗口；可为 NULL。
 * @param      w 新宽度。
 * @param      h 新高度。
 */
void XWindow_resize_2(XWindow* self, int w, int h);

/* ==================== 文件路径 / 图标 ==================== */

/**
 * @brief      设置窗口文件路径（对标 QWindow::setFilePath）。
 * @param      self 目标窗口；可为 NULL。
 * @param      filePath 文件路径；NULL 视为清空。
 */
void XWindow_setFilePath(XWindow* self, const XString* filePath);

/**
 * @brief      以 UTF-8 字符串设置窗口文件路径（兼容重载）。
 * @param      self 目标窗口；可为 NULL。
 * @param      filePath UTF-8 路径；NULL 视为清空。
 */
void XWindow_setFilePath_2(XWindow* self, const char* filePath);

/**
 * @brief      返回窗口文件路径（对标 QWindow::filePath；返回深拷贝）。
 * @param      self 目标窗口；可为 NULL。
 * @return     新 XString；调用方用 XString_delete_base 释放。
 */
XString* XWindow_filePath(const XWindow* self);

/**
 * @brief      设置窗口图标（对标 QWindow::setIcon）。
 * @details    图标以深拷贝持有；传入 NULL 表示清空图标。
 * @param      self 目标窗口；可为 NULL。
 * @param      icon 新图标；可为 NULL。
 */
void XWindow_setIcon(XWindow* self, const XIcon* icon);

/**
 * @brief      返回窗口图标（对标 QWindow::icon；返回深拷贝，调用方负责释放）。
 * @param      self 目标窗口；可为 NULL。
 * @return     新 XIcon；无图标或失败返回 NULL，调用方用 XIcon_deinit_base
 *             释放。
 */
XIcon* XWindow_icon(const XWindow* self);

/* ==================== 销毁 / 平台句柄 / 抓取 ==================== */

/**
 * @brief      销毁平台窗口资源（对标 QWindow::destroy）。
 * @details    无平台后端时仅清空窗口 id 并复位 exposed；窗口对象本身
 *             （x/y/width/height/属性）保留。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_destroy(XWindow* self);

/**
 * @brief      返回平台窗口句柄（对标 QWindow::handle）。
 * @param      self 目标窗口；可为 NULL。
 * @return     平台句柄借用指针；无平台后端返回 NULL。
 */
XWindowPlatform* XWindow_handle(const XWindow* self);

/**
 * @brief      设置平台窗口句柄（供平台后端注入；借用不拥有）。
 * @param      self 目标窗口；可为 NULL。
 * @param      handle 平台句柄；可为 NULL 清除。
 */
void XWindow_setHandle(XWindow* self, XWindowPlatform* handle);

/**
 * @brief      标记/解除平台原生窗口挂接状态（供平台后端注入）。
 * @details    只有平台后端（Drive 的 X11/Win32 原生窗口实现）在
 *             XWindow_setHandle 成功建立真实系统窗口后才会置 true；置 false
 *             表示窗口回归纯软件虚拟 WId 模式。XWindow_createHandle /
 *             XWindow_destroy 以此门槛决定是否调用
 *             XPlatformNativeWindow_create/destroy —— 单纯持有平台句柄
 *             （例如回归测试注入的假句柄）不会创建任何真实系统窗口。
 * @param      self 目标窗口；可为 NULL。
 * @param      attached true 已挂接真实原生窗口；false 解除挂接。
 */
void XWindow_setNativeWindowAttached(XWindow* self, bool attached);

/**
 * @brief 绑定已有外部原生窗口句柄并采用 ForeignWindow 生命周期。
 * @details 仅允许尚未创建的平台窗口；成功后句柄由 Drive 登记，XWindow
 *          销毁时不会销毁调用方拥有的 X11 Window/HWND。
 */
bool XWindow_attachForeignHandle(XWindow* self, XWindowId nativeId);

/**
 * @brief      启用/禁用键盘抓取（对标 QWindow::setKeyboardGrabEnabled）。
 * @details    无平台后端时返回 false（Qt 语义：平台不支持返回 false）。
 * @param      self 目标窗口；可为 NULL。
 * @param      grab true 抓取、false 释放。
 * @return     成功返回 true；入参非法或平台不可用返回 false。
 */
bool XWindow_setKeyboardGrabEnabled(XWindow* self, bool grab);

/**
 * @brief      启用/禁用鼠标抓取（对标 QWindow::setMouseGrabEnabled）。
 * @details    无平台后端时返回 false（Qt 语义）。
 * @param      self 目标窗口；可为 NULL。
 * @param      grab true 抓取、false 释放。
 * @return     成功返回 true；入参非法或平台不可用返回 false。
 */
bool XWindow_setMouseGrabEnabled(XWindow* self, bool grab);

/* ==================== 屏幕归属 ==================== */

#if XSCREEN_ON
/**
 * @brief      返回窗口所在屏幕（对标 QWindow::screen）。
 * @details    未显式设置屏幕时返回进程主屏幕（XScreen_primaryScreen）；
 *             二者皆无则返回 NULL。
 * @param      self 目标窗口；可为 NULL。
 * @return     屏幕借用指针；不可用时返回 NULL。
 */
XScreen* XWindow_screen(const XWindow* self);

/**
 * @brief      设置窗口所在屏幕（对标 QWindow::setScreen）。
 * @details    与 Qt 6.8 QWindow::setScreen 一致：新屏幕为 NULL 时回退主屏幕
 *             （无主屏幕则 no-op）；非顶层窗口拒绝设置；屏幕变化发射
 *             screenChanged 信号并同步 devicePixelRatio。
 * @param      self 目标窗口；可为 NULL。
 * @param      screen 新屏幕；可为 NULL 回退主屏幕。
 */
void XWindow_setScreen(XWindow* self, XScreen* screen);
#else
/** @brief XSCREEN_ON 关闭时 XWindow_screen 退化为空实现。 */
XScreen* XWindow_screen(const XWindow* self);
/** @brief XSCREEN_ON 关闭时 XWindow_setScreen 退化为空实现。 */
void XWindow_setScreen(XWindow* self, XScreen* screen);
#endif

/* ==================== 坐标映射 ==================== */

/**
 * @brief      窗口局部整点坐标映射到全局（对标 QWindow::mapToGlobal(QPoint)）。
 * @details    无平台窗口时为 pos + 几何左上角。
 * @param      self 目标窗口；可为 NULL。
 * @param      pos 局部坐标；NULL 按 (0,0) 处理。
 * @return     全局坐标。
 */
XPoint XWindow_mapToGlobal(const XWindow* self, const XPoint* pos);

/**
 * @brief      全局整点坐标映射到窗口局部（对标 QWindow::mapFromGlobal(QPoint)）。
 * @details    无平台窗口时为 pos - 几何左上角。
 * @param      self 目标窗口；可为 NULL。
 * @param      pos 全局坐标；NULL 按 (0,0) 处理。
 * @return     局部坐标。
 */
XPoint XWindow_mapFromGlobal(const XWindow* self, const XPoint* pos);

/**
 * @brief      窗口局部浮点坐标映射到全局（对标 QWindow::mapToGlobal(QPointF)）。
 * @param      self 目标窗口；可为 NULL。
 * @param      pos 局部浮点坐标；NULL 按 (0,0) 处理。
 * @return     全局浮点坐标。
 */
XPointF XWindow_mapToGlobal_f(const XWindow* self, const XPointF* pos);

/**
 * @brief      全局浮点坐标映射到窗口局部（对标 QWindow::mapFromGlobal(QPointF)）。
 * @param      self 目标窗口；可为 NULL。
 * @param      pos 全局浮点坐标；NULL 按 (0,0) 处理。
 * @return     局部浮点坐标。
 */
XPointF XWindow_mapFromGlobal_f(const XWindow* self, const XPointF* pos);

/* ==================== 光标 ==================== */

#if XCURSOR_ON
/**
 * @brief      返回窗口光标（对标 QWindow::cursor；返回深拷贝，调用方负责释放）。
 * @param      self 目标窗口；可为 NULL。
 * @return     新 XCursor；无光标或入参非法返回 NULL，调用方用
 *             XCursor_deinit_base 释放。
 */
XCursor* XWindow_cursor(const XWindow* self);

/**
 * @brief      设置窗口光标（对标 QWindow::setCursor）。
 * @details    光标以深拷贝持有；传入 NULL 视为取消光标。
 * @param      self 目标窗口；可为 NULL。
 * @param      cursor 新光标；可为 NULL。
 */
void XWindow_setCursor(XWindow* self, const XCursor* cursor);

/**
 * @brief      取消窗口光标（对标 QWindow::unsetCursor）。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_unsetCursor(XWindow* self);
#else
/** @brief XCURSOR_ON 关闭时的 XCursor 前向声明，保持指针 API 可编译。 */
typedef struct XCursor XCursor;
/** @brief XCURSOR_ON 关闭时 XWindow_cursor 退化为空实现。 */
XCursor* XWindow_cursor(const XWindow* self);
/** @brief XCURSOR_ON 关闭时 XWindow_setCursor 退化为空实现。 */
void XWindow_setCursor(XWindow* self, const XCursor* cursor);
/** @brief XCURSOR_ON 关闭时 XWindow_unsetCursor 退化为空实现。 */
void XWindow_unsetCursor(XWindow* self);
#endif

/**
 * @brief      通过窗口 id 查找窗口（对标 QWindow::fromWinId）。
 * @details    无平台窗口后端时总是返回 NULL（Qt 语义：未知 id 返回 NULL）。
 * @param      id 窗口 id。
 * @return     匹配的窗口借用指针；未找到返回 NULL。
 */
XWindow* XWindow_fromWinId(XWindowId id);

/* ==================== 显示/隐藏/关闭 ==================== */

/**
 * @brief      请求激活窗口并给予键盘焦点（对标 QWindow::requestActivate）。
 * @details    窗口带 WindowDoesNotAcceptFocus 标志时忽略（Qt 相同限制）。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_requestActivate(XWindow* self);

/**
 * @brief      设置窗口可见状态（对标 QWindow::setVisible）。
 * @details    与 Qt QWindowPrivate::setVisible 完全一致：可见性变化时先发
 *             visibleChanged，再 updateVisibility()（发 visibilityChanged）；
 *             首次显示自动 create() 并发送 ShowEvent；隐藏发送 HideEvent。
 * @param      self 目标窗口；可为 NULL。
 * @param      visible true 显示、false 隐藏。
 */
void XWindow_setVisible(XWindow* self, bool visible);

/**
 * @brief      显示窗口（对标 QWindow::show）。
 * @details    有父窗口时走 showNormal()；否则按默认状态 showNormal()。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_show(XWindow* self);

/**
 * @brief      隐藏窗口（对标 QWindow::hide；等价 setVisible(false)）。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_hide(XWindow* self);

/**
 * @brief      最小化显示窗口（对标 QWindow::showMinimized）。
 * @details    等价 setWindowStates(Minimized) + setVisible(true)。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_showMinimized(XWindow* self);

/**
 * @brief      最大化显示窗口（对标 QWindow::showMaximized）。
 * @details    等价 setWindowStates(Maximized) + setVisible(true)。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_showMaximized(XWindow* self);

/**
 * @brief      全屏显示窗口（对标 QWindow::showFullScreen）。
 * @details    等价 setWindowStates(FullScreen) + setVisible(true) +
 *             requestActivate()。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_showFullScreen(XWindow* self);

/**
 * @brief      以普通状态显示窗口（对标 QWindow::showNormal）。
 * @details    等价 setWindowStates(NoState) + setVisible(true)。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_showNormal(XWindow* self);

/**
 * @brief      关闭窗口（对标 QWindow::close）。
 * @details    非顶层窗口返回 false；无平台窗口直接返回 true；否则分发
 *             CloseEvent（由事件处理决定是否实际关闭并销毁）。
 * @param      self 目标窗口；可为 NULL。
 * @return     成功返回 true；入参非法或非顶层返回 false。
 */
bool XWindow_close(XWindow* self);

/**
 * @brief      将窗口提升到 Z 序顶部（对标 QWindow::raise）。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_raise(XWindow* self);

/**
 * @brief      将窗口降到 Z 序底部（对标 QWindow::lower）。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_lower(XWindow* self);

/**
 * @brief      启动系统拖动调整大小（对标 QWindow::startSystemResize）。
 * @details    edges 必须为单条边或两条相邻直角边组成的合法组合（Qt 断言语义），
 *             否则返回 false；无平台窗口后端时返回 false。
 * @param      self 目标窗口；可为 NULL。
 * @param      edges 目标边缘位组合。
 * @return     成功启动返回 true。
 */
bool XWindow_startSystemResize(XWindow* self, XWindowEdges edges);

/**
 * @brief      启动系统窗口移动（对标 QWindow::startSystemMove）。
 * @details    需要窗口已可见且有平台窗口后端，否则返回 false。
 * @param      self 目标窗口；可为 NULL。
 * @return     成功启动返回 true。
 */
bool XWindow_startSystemMove(XWindow* self);

/* ==================== 几何属性便捷设置（对标 QWindow 对应 setter） ==================== */

/** @brief 设置窗口 X 坐标（对标 QWindow::setX）。 @param self 目标窗口；可为 NULL。 @param value 新 X。 */
void XWindow_setX(XWindow* self, int value);
/** @brief 设置窗口 Y 坐标（对标 QWindow::setY）。 @param self 目标窗口；可为 NULL。 @param value 新 Y。 */
void XWindow_setY(XWindow* self, int value);
/** @brief 设置窗口宽度（对标 QWindow::setWidth）。 @param self 目标窗口；可为 NULL。 @param value 新宽度。 */
void XWindow_setWidth(XWindow* self, int value);
/** @brief 设置窗口高度（对标 QWindow::setHeight）。 @param self 目标窗口；可为 NULL。 @param value 新高度。 */
void XWindow_setHeight(XWindow* self, int value);

/**
 * @brief      按四参数设置窗口几何（对标 QWindow::setGeometry(int,int,int,int)）。
 * @param      self 目标窗口；可为 NULL。
 * @param      posx 新 X。
 * @param      posy 新 Y。
 * @param      w 新宽度。
 * @param      h 新高度。
 */
void XWindow_setGeometry(XWindow* self, int posx, int posy, int w, int h);

/**
 * @brief      按矩形设置窗口几何（对标 QWindow::setGeometry(QRect)）。
 * @details    无平台窗口时直接更新几何并按逐字段变化发射 x/y/width/height
 *             Changed 信号；设置位置后 positionAutomatic 置 false。
 * @param      self 目标窗口；可为 NULL。
 * @param      rect 新几何；NULL 按 (0,0,0,0) 处理。
 */
void XWindow_setGeometry_rect(XWindow* self, const XRect* rect);

/* ==================== 通知与更新请求 ==================== */

/**
 * @brief      让窗口在桌面上闪烁以提示用户（对标 QWindow::alert）。
 * @details    无平台后端时仅记录请求（msec <= 0 清除提示状态）。
 * @param      self 目标窗口；可为 NULL。
 * @param      msec 闪烁时长（毫秒）；<=0 停止闪烁。
 */
void XWindow_alert(XWindow* self, int msec);

/**
 * @brief      请求窗口内容重新绘制（对标 QWindow::requestUpdate）。
 * @details    无平台后端时置 expose 待更新标志；未来平台后端据此投递
 *             UpdateRequest 事件。
 * @param      self 目标窗口；可为 NULL。
 */
void XWindow_requestUpdate(XWindow* self);

/**
 * @brief      返回辅助功能根对象（对标 QWindow::accessibleRoot）。
 * @details    XACCESSIBLE_ON 开启时返回窗口拥有的辅助功能根对象；关闭
 *             或窗口无效时返回 NULL。
 * @param      self 目标窗口；可为 NULL。
 * @return     XAccessible* 借用指针；不可用时返回 NULL。
 */
void* XWindow_accessibleRoot(const XWindow* self);

/**
 * @brief      返回当前焦点对象（对标 QWindow::focusObject）。
 * @details    默认返回窗口自身（Qt 语义：窗口即焦点对象）。
 * @param      self 目标窗口；可为 NULL。
 * @return     窗口自身强转的 XObject*；入参非法返回 NULL。
 */
XObject* XWindow_focusObject(const XWindow* self);

/* ==================== 通知信号（对标 QWindow 全部 19 个信号） ==================== */

/** @brief 窗口所在屏幕变化信号（对标 QWindow::screenChanged）。 @param self 目标窗口。 @param screen 新屏幕；可为 NULL。 */
void* XWindow_screenChanged_signal(XWindow* self, XScreen* screen);
/** @brief 模态变化信号（对标 QWindow::modalityChanged）。 @param self 目标窗口。 @param modality 新模态。 */
void* XWindow_modalityChanged_signal(XWindow* self, XWindowModality modality);
/** @brief 窗口状态变化信号（对标 QWindow::windowStateChanged）。 @param self 目标窗口。 @param windowState 新的生效状态。 */
void* XWindow_windowStateChanged_signal(XWindow* self, XWindowState windowState);
/** @brief 窗口标题变化信号（对标 QWindow::windowTitleChanged）。 @param self 目标窗口。 @param title 新标题（借用指针，仅信号期间有效）。 */
void* XWindow_windowTitleChanged_signal(XWindow* self, const XString* title);
/** @brief X 坐标变化信号（对标 QWindow::xChanged）。 @param self 目标窗口。 @param value 新 X。 */
void* XWindow_xChanged_signal(XWindow* self, int value);
/** @brief Y 坐标变化信号（对标 QWindow::yChanged）。 @param self 目标窗口。 @param value 新 Y。 */
void* XWindow_yChanged_signal(XWindow* self, int value);
/** @brief 宽度变化信号（对标 QWindow::widthChanged）。 @param self 目标窗口。 @param value 新宽度。 */
void* XWindow_widthChanged_signal(XWindow* self, int value);
/** @brief 高度变化信号（对标 QWindow::heightChanged）。 @param self 目标窗口。 @param value 新高度。 */
void* XWindow_heightChanged_signal(XWindow* self, int value);
/** @brief 最小宽度变化信号（对标 QWindow::minimumWidthChanged）。 @param self 目标窗口。 @param value 新最小宽度。 */
void* XWindow_minimumWidthChanged_signal(XWindow* self, int value);
/** @brief 最小高度变化信号（对标 QWindow::minimumHeightChanged）。 @param self 目标窗口。 @param value 新最小高度。 */
void* XWindow_minimumHeightChanged_signal(XWindow* self, int value);
/** @brief 最大宽度变化信号（对标 QWindow::maximumWidthChanged）。 @param self 目标窗口。 @param value 新最大宽度。 */
void* XWindow_maximumWidthChanged_signal(XWindow* self, int value);
/** @brief 最大高度变化信号（对标 QWindow::maximumHeightChanged）。 @param self 目标窗口。 @param value 新最大高度。 */
void* XWindow_maximumHeightChanged_signal(XWindow* self, int value);
/** @brief 可见状态变化信号（对标 QWindow::visibleChanged）。 @param self 目标窗口。 @param visible 是否可见。 */
void* XWindow_visibleChanged_signal(XWindow* self, bool visible);
/** @brief 可见性变化信号（对标 QWindow::visibilityChanged）。 @param self 目标窗口。 @param visibility 新可见性。 */
void* XWindow_visibilityChanged_signal(XWindow* self, XWindowVisibility visibility);
/** @brief 激活状态变化信号（对标 QWindow::activeChanged）。 @param self 目标窗口。 */
void* XWindow_activeChanged_signal(XWindow* self);
/** @brief 内容方向变化信号（对标 QWindow::contentOrientationChanged）。 @param self 目标窗口。 @param orientation 新内容方向。 */
void* XWindow_contentOrientationChanged_signal(XWindow* self, XScreenOrientation orientation);
/** @brief 焦点对象变化信号（对标 QWindow::focusObjectChanged）。 @param self 目标窗口。 @param object 新焦点对象；可为 NULL。 */
void* XWindow_focusObjectChanged_signal(XWindow* self, XObject* object);
/** @brief 不透明度变化信号（对标 QWindow::opacityChanged）。 @param self 目标窗口。 @param opacity 新不透明度。 */
void* XWindow_opacityChanged_signal(XWindow* self, float opacity);
/** @brief 瞬态父窗口变化信号（对标 QWindow::transientParentChanged）。 @param self 目标窗口。 @param transientParent 新瞬态父窗口；可为 NULL。 */
void* XWindow_transientParentChanged_signal(XWindow* self, XWindow* transientParent);

/* ==================== 事件分发（对标 QWindow protected 事件虚函数） ==================== */
/*
 * QWindow::event / exposeEvent / resizeEvent / ... / dropEvent / nativeEvent
 * 在 Qt 中均属 protected 虚函数接口。XWindow 的对应入口
 * （XWindow_event_base 宏、XWindow_*Event_base、XWindow_nativeEvent_base）
 * 已迁移至保护头文件 XWindow_Protected.h，供子类与内部实现使用。
 */

#endif /* XWINDOW_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWINDOW_H */
