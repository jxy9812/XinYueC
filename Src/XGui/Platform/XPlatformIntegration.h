/******************************************************************************
 * @file       XPlatformIntegration.h
 * @brief      XPlatformIntegration 平台集成层类（对标 Qt 6.8 QPlatformIntegration
 *             全部公共 API）。
 * @details    XPlatformIntegration 继承 XObject，是进程内唯一的嵌入式平台
 *             集成层单后端：
 *             - 能力位：hasCapability() 按内部能力位掩码查询，默认开启
 *               ThreadedPixmaps / WindowMasks / MultipleWindows /
 *               ApplicationState / NonFullScreenWindows / WindowManagement /
 *               WindowActivation / SyncState / ApplicationIcon / PaintEvents，
 *               OpenGL/RHI/外部窗口能力按 Drive 运行时探测，共享图形缓存等
 *               未建立独立资源层的能力关闭；
 *               BackingStoreStaticContents 随 XBackingStore 实现默认开启
 *               （受 XBACKINGSTORE_ON 总开关约束，关闭时该能力位不置位）；
 *             - 窗口句柄：createPlatformWindow() 幂等地为 XWindow 创建
 *               XPlatformWindow 并挂接到 XWindow_setHandle（借用表登记），
 *               createForeignWindow() 在 Linux X11/Windows Win32 上挂接外部
 *               原生窗口，其它后端返回 NULL；
 *             - 后备存储/图形上下文：createPlatformBackingStore() 创建
 *               XPlatformBackingStore（由 Drive 下 Linux/Windows 后端实现，
 *               其它平台回落 NULL）；GPU 抽象层通过 XPlatformGraphics 转发
 *               Linux GLX/Vulkan 与 Windows WGL/Vulkan；
 *             - 子单例：nativeInterface() / inputContext() 返回本层拥有的
 *               XPlatformNativeInterface / XPlatformInputContext；
 *               clipboard() / styleHints() 返回借用指针（由
 *               XGuiApplication 创建的进程内单例）；fontDatabase()、services()
 *               返回平台快照/服务对象；drag() / keyMapper() 仍按事件路径或
 *               键盘翻译器能力决定是否为空；
 *               accessibility() 返回 XPlatformAccessibility 平台桥接对象；
 *             - 样式提示：styleHint() 按 StyleHint 枚举映射
 *               XStyleHints 状态或返回嵌入式默认值（新分配 XVariant）；
 *             - 平台元信息：themeNames() 返回含 "embedded" 的列表；
 *               状态：setApplicationIcon / setApplicationBadge 进程内存值；
 *               sync() 空实现；beep() 恒 false；quit() 转发
 *               XCoreApplication_quit()。
 *             本模块不依赖任何平台 API，系统窗口栈、图形栈或输入法框架均由
 *             Drive 后端实现；平台句柄由集成层登记，调用方不得释放。
 * @note       模块总开关 XPLATFORMINTEGRATION_ON 定义于 XGuiConfig.h；
 *             置 0 时裁剪整个公共 API，XGuiApplication 的
 *             platformNativeInterface()/inputMethod() 返回 NULL。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMINTEGRATION_H
#define XPLATFORMINTEGRATION_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XTypes.h"
#include "XGeometry.h"
#include "XString.h"
#include "XVector.h"
#include "XIcon.h"
#if XWINDOW_ON
#include "XWindow.h"
#else /* !XWINDOW_ON */
/** @brief XWINDOW_ON=0 时的 XWindow 前向声明，保持指针 API 可编译。 */
typedef struct XWindow XWindow;
/** @brief 窗口 id 类型回退定义（对标 XWindowId）。 */
typedef uintptr_t XWindowId;
/** @brief 窗口状态回退定义（对标 XWindowState 枚举值集，仅保留普通状态）。 */
typedef enum XWindowStateFallback
{
    XWindowState_NoState = 0x00000000 /**< 普通状态。 */
} XWindowState;
/** @brief 窗口标志位组合回退定义（对标 XWindowFlags）。 */
typedef uint32_t XWindowFlags;
#endif /* XWINDOW_ON */

#if XPLATFORMNATIVEINTERFACE_ON
#include "XPlatformNativeInterface.h"
#else /* !XPLATFORMNATIVEINTERFACE_ON */
/** @brief XPlatformNativeInterface 前向声明回退（开关关闭时 nativeInterface() 返回 NULL）。 */
typedef struct XPlatformNativeInterface XPlatformNativeInterface;
#endif /* XPLATFORMNATIVEINTERFACE_ON */

#if XPLATFORMINPUTCTX_ON
#include "XPlatformInputContext.h"
#else /* !XPLATFORMINPUTCTX_ON */
/** @brief XPlatformInputContext 前向声明回退（开关关闭时 inputContext() 返回 NULL）。 */
typedef struct XPlatformInputContext XPlatformInputContext;
#endif /* XPLATFORMINPUTCTX_ON */

#if XPLATFORMWINDOW_ON
#include "XPlatformWindow.h"
#else /* !XPLATFORMWINDOW_ON */
/** @brief XPlatformWindow 前向声明回退（开关关闭时 createPlatformWindow() 返回 NULL）。 */
typedef struct XPlatformWindow XPlatformWindow;
#endif /* XPLATFORMWINDOW_ON */

#if XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#endif /* XPLATFORMNATIVEWINDOW_ON */

#include "XPlatformGraphics.h"
#include "XPlatformFontDatabase.h"
#include "XPlatformTheme.h"
#include "XPlatformServices.h"
#include "XPlatformDrag.h"

#if XSTYLEHINTS_ON
#include "XStyleHints.h"
#else /* !XSTYLEHINTS_ON */
/** @brief XStyleHints 前向声明回退（开关关闭时 styleHints 相关查询走默认值）。 */
typedef struct XStyleHints XStyleHints;
#endif /* XSTYLEHINTS_ON */

#if XCLIPBOARD_ON
#include "XClipboard.h"
#else /* !XCLIPBOARD_ON */
/** @brief XClipboard 前向声明回退（开关关闭时 clipboard() 返回 NULL）。 */
typedef struct XClipboard XClipboard;
#endif /* XCLIPBOARD_ON */

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
#include "XPlatformBackingStore.h"
#else /* !XBACKINGSTORE_ON || !XPLATFORMBACKINGSTORE_ON */
/** @brief XPlatformBackingStore 前向声明回退（开关关闭时 createPlatformBackingStore() 返回 NULL）。 */
typedef struct XPlatformBackingStore XPlatformBackingStore;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */

#if XPLATFORMINTEGRATION_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XPlatformIntegrationPrivate XPlatformIntegrationPrivate;/** @brief 声明 XPlatformIntegration 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XPlatformIntegration)
XCLASS_DEFINE_EXTEND_END(XPlatformIntegration, XObject)



/* ==================== 枚举类型（对标 QPlatformIntegration） ==================== */

/**
 * @brief      平台集成层能力位（对标 Qt 6.8 QPlatformIntegration::Capability，
 *             取值从 1 起连续）。
 * @details    能力位存储为 uint64_t 位掩码，本层默认开启嵌入式能力，
 *             其余（OpenGL/共享图形缓存/光栅 GL 表面/后端存储静态内容等）
 *             待对应模块实现后开启。
 */
typedef enum XPlatformIntegrationCapability
{
    XPlatformIntegrationCapability_ThreadedPixmaps              = 1,  /**< 支持线程化位图。 */
    XPlatformIntegrationCapability_OpenGL,                             /**< 支持 OpenGL。 */
    XPlatformIntegrationCapability_ThreadedOpenGL,                     /**< 支持线程化 OpenGL。 */
    XPlatformIntegrationCapability_SharedGraphicsCache,                /**< 支持共享图形缓存。 */
    XPlatformIntegrationCapability_BufferQueueingOpenGL,               /**< 支持 OpenGL 缓冲队列。 */
    XPlatformIntegrationCapability_WindowMasks,                        /**< 支持窗口遮罩。 */
    XPlatformIntegrationCapability_MultipleWindows,                    /**< 支持多窗口。 */
    XPlatformIntegrationCapability_ApplicationState,                   /**< 支持应用状态。 */
    XPlatformIntegrationCapability_ForeignWindows,                     /**< 支持外部窗口。 */
    XPlatformIntegrationCapability_NonFullScreenWindows,               /**< 支持非全屏窗口。 */
    XPlatformIntegrationCapability_NativeWidgets,                      /**< 支持原生控件。 */
    XPlatformIntegrationCapability_WindowManagement,                   /**< 支持窗口管理。 */
    XPlatformIntegrationCapability_WindowActivation,                   /**< 支持窗口激活（requestActivate）。 */
    XPlatformIntegrationCapability_SyncState,                          /**< 支持 sync 状态冲刷。 */
    XPlatformIntegrationCapability_RasterGLSurface,                    /**< 支持栅格 GL 表面。 */
    XPlatformIntegrationCapability_AllGLFunctionsQueryable,            /**< 支持全部 GL 函数查询。 */
    XPlatformIntegrationCapability_ApplicationIcon,                    /**< 支持应用图标。 */
    XPlatformIntegrationCapability_SwitchableWidgetComposition,        /**< 支持可切换控件合成。 */
    XPlatformIntegrationCapability_TopStackedNativeChildWindows,      /**< 支持顶层原生子窗口堆叠。 */
    XPlatformIntegrationCapability_OpenGLOnRasterSurface,              /**< 支持栅格表面的 OpenGL。 */
    XPlatformIntegrationCapability_MaximizeUsingFullscreenGeometry,    /**< 最大化使用全屏几何。 */
    XPlatformIntegrationCapability_PaintEvents,                        /**< 支持绘制事件。 */
    XPlatformIntegrationCapability_RhiBasedRendering,                  /**< 支持 RHI 渲染。 */
    XPlatformIntegrationCapability_ScreenWindowGrabbing,               /**< 支持屏幕窗口抓取。 */
    XPlatformIntegrationCapability_BackingStoreStaticContents          /**< 支持后备存储静态内容。 */
} XPlatformIntegrationCapability;

/**
 * @brief      平台样式提示枚举（对标 Qt 6.8 QPlatformIntegration::StyleHint，
 *             取值从 0 起连续）。
 * @details    styleHint() 按此枚举映射 XStyleHints 状态；无对应状态项的
 *             嵌入式默认值在实现中逐项说明。
 */
typedef enum XPlatformIntegrationStyleHint
{
    XPlatformIntegrationStyleHint_CursorFlashTime = 0,          /**< 光标闪烁时间（毫秒）。 */
    XPlatformIntegrationStyleHint_KeyboardInputInterval,        /**< 键盘输入重复间隔。 */
    XPlatformIntegrationStyleHint_MouseDoubleClickInterval,     /**< 鼠标双击时间间隔。 */
    XPlatformIntegrationStyleHint_StartDragDistance,            /**< 开始拖拽距离（像素）。 */
    XPlatformIntegrationStyleHint_StartDragTime,                /**< 开始拖拽时间。 */
    XPlatformIntegrationStyleHint_KeyboardAutoRepeatRate,       /**< 键盘自动重复速率。 */
    XPlatformIntegrationStyleHint_ShowIsFullScreen,             /**< show() 是否全屏。 */
    XPlatformIntegrationStyleHint_PasswordMaskDelay,            /**< 密码掩码延迟。 */
    XPlatformIntegrationStyleHint_FontSmoothingGamma,           /**< 字体平滑伽马。 */
    XPlatformIntegrationStyleHint_StartDragVelocity,            /**< 开始拖拽速度。 */
    XPlatformIntegrationStyleHint_UseRtlExtensions,             /**< 是否使用 RTL 扩展。 */
    XPlatformIntegrationStyleHint_PasswordMaskCharacter,        /**< 密码掩码字符。 */
    XPlatformIntegrationStyleHint_SetFocusOnTouchRelease,       /**< 触摸释放时设置焦点。 */
    XPlatformIntegrationStyleHint_ShowIsMaximized,              /**< show() 是否最大化。 */
    XPlatformIntegrationStyleHint_MousePressAndHoldInterval,    /**< 鼠标按住间隔。 */
    XPlatformIntegrationStyleHint_TabFocusBehavior,             /**< Tab 焦点行为。 */
    XPlatformIntegrationStyleHint_ReplayMousePressOutsidePopup, /**< 弹窗外重放鼠标按下。 */
    XPlatformIntegrationStyleHint_ItemViewActivateItemOnSingleClick, /**< 单击激活项视图。 */
    XPlatformIntegrationStyleHint_UiEffects,                    /**< UI 特效位。 */
    XPlatformIntegrationStyleHint_WheelScrollLines,             /**< 滚轮滚动行数。 */
    XPlatformIntegrationStyleHint_ShowShortcutsInContextMenus,  /**< 上下文菜单显示快捷键。 */
    XPlatformIntegrationStyleHint_MouseQuickSelectionThreshold, /**< 鼠标快速选中阈值。 */
    XPlatformIntegrationStyleHint_MouseDoubleClickDistance,     /**< 鼠标双击距离。 */
    XPlatformIntegrationStyleHint_FlickStartDistance,           /**< 轻扫开始距离。 */
    XPlatformIntegrationStyleHint_FlickMaximumVelocity,         /**< 轻扫最大速度。 */
    XPlatformIntegrationStyleHint_FlickDeceleration,            /**< 轻扫减速度。 */
    XPlatformIntegrationStyleHint_UnderlineShortcut             /**< 是否给快捷键加下划线。 */
} XPlatformIntegrationStyleHint;

/**
 * @brief      平台集成本体对象；m_class 必须为第一个成员。
 * @details    集成层拥有的子对象（原生接口/输入上下文）与窗口句柄登记表均
 *             保存在 m_data 私有块中；调用方不得直接访问任何字段。
 */
typedef struct XPlatformIntegration
{
    XObject                        m_class; /**< 第一个成员，由 XObject 管理。 */
    XPlatformIntegrationPrivate*   m_data;  /**< 私有数据块，由 XPlatformIntegration 拥有。 */
} XPlatformIntegration;

/* ==================== 生命周期管理 ==================== */

/**
 * @brief      初始化 XPlatformIntegration 类虚函数表并返回共享表指针。
 * @return     XPlatformIntegration 类的共享 XVtable 指针。
 */
XVtable* XPlatformIntegration_class_init(void);

/**
 * @brief      初始化嵌入式平台集成层（默认能力位/主题名 "embedded"）。
 * @param      self 待初始化对象；必须与 XPlatformIntegration_deinit_base 成对调用。
 */
void XPlatformIntegration_init(XPlatformIntegration* self);

/**
 * @brief      使用默认内存类型在堆上创建 XPlatformIntegration。
 * @return     新对象指针；失败返回 NULL，调用方用
 *             XPlatformIntegration_delete_base 释放。
 */
#define XPlatformIntegration_create() \
    XPlatformIntegration_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建 XPlatformIntegration。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XPlatformIntegration* XPlatformIntegration_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放 XPlatformIntegration 资源（栈/外部存储对象使用）。 */
#define XPlatformIntegration_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XPlatformIntegration 对象。 */
#define XPlatformIntegration_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 能力查询（对标 hasCapability） ==================== */

/**
 * @brief      查询集成层是否支持指定能力位。
 * @param      self 目标对象；可为 NULL。
 * @param      cap 能力位。
 * @return     true 支持；false 不支持或入参非法。
 */
bool XPlatformIntegration_hasCapability(const XPlatformIntegration* self,
                                        XPlatformIntegrationCapability cap);

/* ==================== 平台对象创建（对标 QPlatformIntegration 工厂） ==================== */

/**
 * @brief      创建/取得与 XWindow 关联的平台窗口句柄（对标 createPlatformWindow）。
 * @details    幂等：同一 XWindow 已登记时直接返回已有 XPlatformWindow（借用）；
 *             否则创建新句柄并挂接到 XWindow_setHandle（借用登记）。
 * @param      self 目标对象；可为 NULL。
 * @param      window 目标 XWindow 借用指针；可为 NULL。
 * @return     平台窗口句柄（借用，归集成层所有）；失败或 XPLATFORMWINDOW_ON=0
 *             返回 NULL。
 */
XPlatformWindow* XPlatformIntegration_createPlatformWindow(
        XPlatformIntegration* self, XWindow* window);

/**
 * @brief      为外部原生句柄创建平台窗口（对标 createForeignWindow）。
 * @details    Linux X11 与 Windows Win32 后端将现有句柄挂接到 XWindow，且不
 *             取得外部窗口的销毁所有权；无系统窗口连接时返回 NULL。
 * @param      self 目标对象；可为 NULL。
 * @param      window 目标 XWindow 借用指针；可为 NULL。
 * @param      nativeHandle 外部原生句柄（X11 Window 或 Win32 HWND）。
 * @return     XPlatformWindow* 借用指针；句柄无效或后端不可用时返回 NULL。
 */
XPlatformWindow* XPlatformIntegration_createForeignWindow(
        XPlatformIntegration* self, XWindow* window, XWindowId nativeHandle);

/**
 * @brief      创建平台后备存储（对标 createPlatformBackingStore）。
 * @details    转发到 Drive 平台后端 XPlatformBackingStore_create()：
 *             Linux 提供纯软件 XImage 缓冲，Windows 提供 GDI DIB + BitBlt，
 *             其它平台（FreeRTOS/裸机等）返回 NULL（XBackingStore 安全
 *             退化为空后端）。创建后初始尺寸为 0×0，须先 resize 再使用；
 *             句柄归调用方（XBackingStore）所有，本层不登记不缓存。
 * @param      self 目标对象；可为 NULL。
 * @param      window 目标 XWindow 借用指针；可为 NULL（纯离屏缓冲）。
 * @return     平台后备存储句柄（归调用方所有）；后端不可用或开关关闭
 *             返回 NULL。
 */
XPlatformBackingStore* XPlatformIntegration_createPlatformBackingStore(
        XPlatformIntegration* self, XWindow* window);

/**
 * @brief      查询平台位图数据工厂（对标 createPlatformPixmap）。
 * @details    XPixmap 由进程内位图承载，无独立平台位图数据层，恒返回 NULL。
 * @param      self 目标对象；可为 NULL。
 * @param      pixelType 像素类型占位；忽略。
 * @return     恒 NULL。
 */
void* XPlatformIntegration_createPlatformPixmap(
        XPlatformIntegration* self, int pixelType);

/**
 * @brief      创建平台 OpenGL 上下文（对标 createPlatformOpenGLContext）。
 * @details    context 解释为已创建原生表面的 XWindow*，返回其 OpenGL
 *             平台上下文（调用方以 XPlatformOpenGLContext_destroy 释放）。
 *             无桌面图形驱动或窗口尚无原生表面时返回 NULL。
 * @param      self 目标对象；可为 NULL。
 * @param      context XWindow* 借用指针（为兼容既有 void* 工厂签名）。
 * @return     XPlatformOpenGLContext*；失败返回 NULL。
 */
void* XPlatformIntegration_createPlatformOpenGLContext(
        XPlatformIntegration* self, void* context);

/**
 * @brief      创建共享图形缓存（对标 createPlatformSharedGraphicsCache）。
 * @details    嵌入式未实现共享图形缓存，恒返回 NULL；普通进程内 XPixmap
 *             已自带平台数据，不需要额外工厂。
 * @param      self 目标对象；可为 NULL。
 * @param      cacheId 缓存 id；忽略。
 * @return     恒 NULL。
 */
void* XPlatformIntegration_createPlatformSharedGraphicsCache(
        XPlatformIntegration* self, const char* cacheId);

/**
 * @brief      创建图像绘制引擎（对标 createImagePaintEngine）。
 * @details    绘制由 XPainter 进程内实现，恒返回 NULL。
 * @return     恒 NULL。
 */
void* XPlatformIntegration_createImagePaintEngine(
        XPlatformIntegration* self, void* paintDevice);

/**
 * @brief      创建事件分发器（对标 createEventDispatcher）。
 * @details    返回统一的 XinYueC 事件分发器；原生窗口事件由
 *             processNativeEvents() 先泵入，调用方不得释放返回对象。
 * @return     借用的事件分发器指针；事件子系统关闭时返回 NULL。
 */
void* XPlatformIntegration_createEventDispatcher(
        XPlatformIntegration* self);

/**
 * @brief      平台初始化钩子（对标 initialize；空后端 no-op）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformIntegration_initialize(XPlatformIntegration* self);

/**
 * @brief      平台销毁钩子（对标 destroy；空后端 no-op，子对象由析构统一回收）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformIntegration_destroy(XPlatformIntegration* self);

/* ==================== 原生窗口事件泵（平台事件源转发） ==================== */

/**
 * @brief      查询当前进程是否已连接可用原生窗口系统。
 * @details    转发 XPlatformNativeWindow_isAvailable：X11 后端在
 *             XOpenDisplay 成功后返回 true，Win32 后端注册 GUI 窗口类即可用；
 *             嵌入式/无窗口系统返回 false（XWindow 保持纯软件虚拟 WId）。
 * @param      self 目标对象；可为 NULL。
 * @return     true 已连接窗口系统；false 未连接或模块关闭。
 */
bool XPlatformIntegration_isNativeWindowAvailable(
        const XPlatformIntegration* self);

/**
 * @brief      非阻塞泵空当前全部待决原生窗口事件。
 * @details    转发 XPlatformNativeWindow_processPendingEvents：X11 用
 *             XPending/XNextEvent，Win32 用 PeekMessage(PM_REMOVE)，翻译后
 *             经 WSI 注入 XWindow。XGuiApplication_processEvents 在每次
 *             公共事件循环前自动调用本入口；应用一般无需直接调用。
 * @param      self 目标对象；可为 NULL。
 * @return     true 本次处理并注入了至少一个事件；false 无事件或模块关闭。
 */
bool XPlatformIntegration_processNativeEvents(
        const XPlatformIntegration* self);

/**
 * @brief      阻塞等待原生窗口事件，就绪后处理一批并返回。
 * @details    转发 XPlatformNativeWindow_waitForEvents：X11 用
 *             poll(ConnectionNumber)，Win32 用 MsgWaitForMultipleObjects。
 *             供自治主循环通过 XGuiApplication_waitForEvents 使用，避免
 *             忙轮询 CPU 空转。
 * @param      self 目标对象；可为 NULL。
 * @param      maxMilliseconds 最大阻塞毫秒；0 表示只做一次立即探测。
 * @return     true 就绪并注入了事件；false 超时、被打断或模块关闭。
 */
bool XPlatformIntegration_waitForNativeEvents(
        const XPlatformIntegration* self, int maxMilliseconds);

/* ==================== 子单例访问（对标 QPlatformIntegration） ==================== */

/**
 * @brief      返回平台字体数据库（对标 fontDatabase）。
 * @return     XPlatformFontDatabase* 借用指针；字体驱动不可用时为非活动
 *             空数据库或 NULL。
 */
void* XPlatformIntegration_fontDatabase(const XPlatformIntegration* self);

/**
 * @brief      返回平台剪贴板后端（对标 clipboard；进程内单例借用指针）。
 * @return     XClipboard* 借用指针；XCLIPBOARD_ON=0 返回 NULL。
 */
XClipboard* XPlatformIntegration_clipboard(const XPlatformIntegration* self);

/**
 * @brief      设置平台剪贴板后端借用指针（由 XGuiApplication 创建单例后注入）。
 * @param      self 目标对象；可为 NULL。
 * @param      clipboard 剪贴板借用指针；可为 NULL 解除。
 */
void XPlatformIntegration_setClipboard(XPlatformIntegration* self,
                                       XClipboard* clipboard);

/**
 * @brief      返回平台拖拽访问器（对标 drag）。
 * @details    Linux 使用 XDND 源端，Windows 使用 Drive 的 OLE 后端；嵌入式
 *             后端返回 NULL。
 * @return     XPlatformDrag* 借用指针；无出站拖放能力时返回 NULL。
 */
void* XPlatformIntegration_drag(const XPlatformIntegration* self);

/**
 * @brief      返回平台输入上下文（对标 inputContext；本层拥有）。
 * @return     XPlatformInputContext* 借用指针；XPLATFORMINPUTCTX_ON=0 返回 NULL。
 */
XPlatformInputContext* XPlatformIntegration_inputContext(
        const XPlatformIntegration* self);

/**
 * @brief      返回平台无障碍访问器（对标 accessibility）。
 * @return     XPlatformAccessibility* 借用指针；辅助功能关闭时返回 NULL，
 *             后端不可用时返回非活动存根。
 */
void* XPlatformIntegration_accessibility(const XPlatformIntegration* self);

/**
 * @brief      返回平台原生接口（对标 nativeInterface；本层拥有）。
 * @return     XPlatformNativeInterface* 借用指针；XPLATFORMNATIVEINTERFACE_ON=0 返回 NULL。
 */
XPlatformNativeInterface* XPlatformIntegration_nativeInterface(
        const XPlatformIntegration* self);

/**
 * @brief      返回平台服务访问器（对标 services）。
 * @return     XPlatformServices* 借用指针；嵌入式无桌面服务时可为 NULL。
 */
void* XPlatformIntegration_services(const XPlatformIntegration* self);

/* ==================== 样式提示 / 输入状态（对标 styleHint 等） ==================== */

/**
 * @brief      查询平台样式提示（对标 styleHint）。
 * @details    优先映射 XStyleHints 状态（借用的进程内单例）；无对应项的
 *             嵌入式默认值：ReplayMousePressOutsidePopup=false、
 *             ItemViewActivateItemOnSingleClick=false、UiEffects=0、
 *             FlickStartDistance=30、FlickMaximumVelocity=3000、
 *             FlickDeceleration=1500、UnderlineShortcut=0、
 *             MouseDoubleClickDistance=5。
 * @param      self 目标对象；可为 NULL。
 * @param      hint 样式提示枚举。
 * @return     新建 XVariant（调用方用 XVariant 删除接口释放）；入参非法返回 NULL。
 */
XVariant* XPlatformIntegration_styleHint(const XPlatformIntegration* self,
                                         XPlatformIntegrationStyleHint hint);

/**
 * @brief      根据窗口标志返回默认窗口状态（对标 defaultWindowState）。
 * @details    嵌入式无窗口管理器策略，恒返回 XWindowState_NoState。
 * @return     XWindowState_NoState。
 */
XWindowState XPlatformIntegration_defaultWindowState(
        const XPlatformIntegration* self, XWindowFlags flags);

/**
 * @brief      查询当前键盘修饰键（对标 queryKeyboardModifiers）。
 * @details    优先经 XPlatformNativeWindow 后端查询输入设备的即时状态；无
 *             原生后端时回退到 XGuiApplication 最后派发事件的缓存状态。
 * @return     当前修饰键位组合。
 */
XKeyboardModifiers XPlatformIntegration_queryKeyboardModifiers(
        const XPlatformIntegration* self);

/**
 * @brief      返回按键事件可能的键值列表（对标 possibleKeys）。
 * @return     恒 NULL（无键盘码表协议）。
 */
void* XPlatformIntegration_possibleKeys(const XPlatformIntegration* self,
                                        void* keyEvent);

/**
 * @brief      返回平台键映射器（对标 keyMapper）。
 * @return     恒 NULL。
 */
void* XPlatformIntegration_keyMapper(const XPlatformIntegration* self);

/* ==================== 平台主题（对标 themeNames 等） ==================== */

/**
 * @brief      返回可用主题名列表（对标 themeNames）。
 * @details    返回新建 XVector（元素为借用 XString*，恒含 "embedded"）；
 *             调用方用 XVector 删除接口释放，元素由集成层拥有不释放。
 * @return     新建 XVector；失败返回 NULL。
 */
XVector* XPlatformIntegration_themeNames(const XPlatformIntegration* self);

/**
 * @brief      返回当前主题名（对标 Qt 6.8 之前 QPlatformIntegration::themeName）。
 * @details    与 themeNames() 内 "embedded" 对应的便捷取值；返回新建 XString。
 * @return     新建 XString；失败返回 NULL。
 */
XString* XPlatformIntegration_themeName(const XPlatformIntegration* self);

/**
 * @brief      创建指定名称的平台主题（对标 createPlatformTheme）。
 * @details    Linux/Windows 返回主题快照；嵌入式返回名称为 "embedded" 的
 *             快照，不持有系统主题句柄。
 * @return     XPlatformTheme* 调用方负责 XPlatformTheme_destroy；失败返回 NULL。
 */
void* XPlatformIntegration_createPlatformTheme(
        const XPlatformIntegration* self, const XString* name);

/**
 * @brief      创建平台离屏表面（对标 createPlatformOffscreenSurface）。
 * @return     Linux X11/Windows WGL 可用时返回离屏表面；其它后端返回 NULL。
 */
void* XPlatformIntegration_createPlatformOffscreenSurface(
        XPlatformIntegration* self, void* surface);

/**
 * @brief      创建平台会话管理器（对标 createPlatformSessionManager）。
 * @return     恒 NULL（嵌入式不接会话管理器）。
 */
void* XPlatformIntegration_createPlatformSessionManager(
        XPlatformIntegration* self, const XString* id, const XString* key);

/* ==================== 同步 / 平台行为 ==================== */

/**
 * @brief      冲刷窗口系统命令队列（对标 sync）。
 * @details    空后端无真实窗口栈：恒 no-op。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformIntegration_sync(XPlatformIntegration* self);

/**
 * @brief      返回 OpenGL 模块类型（对标 openGLModuleType）。
 * @details    当前无 GPU 抽象层，恒返回 0。
 * @return     恒 0。
 */
int XPlatformIntegration_openGLModuleType(XPlatformIntegration* self);

/**
 * @brief      设置应用图标（对标 setApplicationIcon；进程内深拷贝保存）。
 * @param      self 目标对象；可为 NULL。
 * @param      icon 图标借用指针；可为 NULL 清除。
 */
void XPlatformIntegration_setApplicationIcon(XPlatformIntegration* self,
                                             const XIcon* icon);

/**
 * @brief      设置应用徽标数（对标 setApplicationBadge；进程内保存）。
 * @param      self 目标对象；可为 NULL。
 * @param      number 徽标数。
 */
void XPlatformIntegration_setApplicationBadge(XPlatformIntegration* self,
                                              int64_t number);

/**
 * @brief      播放系统提示音（对标 beep；嵌入式无系统提示音，恒 false）。
 * @param      self 目标对象；可为 NULL。
 * @return     恒 false。
 */
bool XPlatformIntegration_beep(XPlatformIntegration* self);

/**
 * @brief      请求退出应用（对标 quit；转发 XCoreApplication_quit()）。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformIntegration_quit(XPlatformIntegration* self);

/**
 * @brief      创建平台 Vulkan 实例（对标 createPlatformVulkanInstance）。
 * @details    返回 XPlatformVulkanInstance*；实例创建后已完成物理设备
 *             枚举，调用方以 XPlatformVulkanInstance_destroy 释放。
 * @return     XPlatformVulkanInstance*；无驱动或创建失败返回 NULL。
 */
void* XPlatformIntegration_createPlatformVulkanInstance(
        XPlatformIntegration* self, void* instance);

/* ==================== 内部状态访问（供 XGuiApplication 对接） ==================== */

/**
 * @brief      注入样式提示借用指针（由 XGuiApplication_styleHints() 调用）。
 * @details    styleHint() 查询时若已注入则优先映射 XStyleHints 状态。
 * @param      self 目标对象；可为 NULL。
 * @param      hints 样式提示借用指针；可为 NULL 解除。
 */
void XPlatformIntegration_setStyleHints(XPlatformIntegration* self,
                                        XStyleHints* hints);

#ifdef __cplusplus
}
#endif

#endif /* XPLATFORMINTEGRATION_ON */
#endif /* XPLATFORMINTEGRATION_H */
