/******************************************************************************
 * @file       XGuiApplication.h
 * @brief      XGuiApplication GUI 应用类（对标 Qt 6.8 QGuiApplication 全部公开 API）。
 * @details    XGuiApplication 继承 XCoreApplication，是进程内唯一的 GUI 应用
 *             单例（xGuiApp 宏），统一管理：窗口注册表（allWindows /
 *             topLevelWindows / topLevelAt）、屏幕（primaryScreen / screens /
 *             screenAt / devicePixelRatio）、焦点（focusWindow / focusObject /
 *             modalWindow）、光标覆盖栈（setOverrideCursor 系列）、字体/
 *             调色板（font / palette）、样式提示与剪贴板单例（styleHints /
 *             clipboard）、键盘鼠标状态、布局方向、应用状态、DPI 取整策略、
 *             会话信息，以及 Qt QGuiApplication 的全部 14 个信号。
 *             本模块不依赖任何平台 API：窗口/屏幕由平台接入钩子
 *             （XGuiApplication_addWindow / screenAdded 等）程序化登记，
 *             事件分发沿用 XCoreApplication 虚表（notify/event）。
 *             平台层：inputMethod()/platformNativeInterface()/
 *             platformFunction()/sync() 对接 XPlatformIntegration
 *             （XPLATFORMINTEGRATION_ON 关闭时按 Qt 允许返回 NULL 的语义
 *             退化为 NULL/空实现）。
 * @note       模块总开关 XGUIAPPLICATION_ON 定义于 XGuiConfig.h；置 0 时
 *             裁剪整个 XGuiApplication 公共 API。子模块 XSTYLEHINTS_ON /
 *             XCLIPBOARD_ON / XMIMEDATA_ON / XPALETTE_ON / XCURSOR_ON 关闭
 *             时对应能力退化为空实现/返回 NULL；XWINDOW_ON / XSCREEN_ON 关闭
 *             时窗口/屏幕 API 退化为空实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGUIAPPLICATION_H
#define XGUIAPPLICATION_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XGeometry.h"
#include "XFont.h"
#include "XString.h"
#include "XVector.h"
#include "XWindow.h"
#include "XScreen.h"
#include "XIcon.h"
#include "XStyleHints.h"
#include "XClipboard.h"
#include "XPalette.h"
#if XPLATFORMINTEGRATION_ON
#include "XPlatformIntegration.h"
#else /* !XPLATFORMINTEGRATION_ON */
/** @brief XPlatformIntegration 前向声明回退（开关关闭时平台接口返回 NULL）。 */
typedef struct XPlatformIntegration XPlatformIntegration;
/** @brief XPlatformNativeInterface 前向声明回退（开关关闭时 platformNativeInterface() 返回 NULL）。 */
typedef struct XPlatformNativeInterface XPlatformNativeInterface;
#endif /* XPLATFORMINTEGRATION_ON */

#if XINPUTMETHOD_ON
#include "XInputMethod.h"
#else /* !XINPUTMETHOD_ON */
/** @brief XInputMethod 前向声明回退（开关关闭时 inputMethod() 返回 NULL）。 */
typedef struct XInputMethod XInputMethod;
#endif /* XINPUTMETHOD_ON */

#if XCURSOR_ON
#include "XCursor.h"
#endif

#if XGUIAPPLICATION_ON

/* ==================== 依赖子模块回退定义 ==================== *//** @brief 声明 XGuiApplication 虚函数枚举：继承 XCoreApplication 全部槽位。 */
XCLASS_DEFINE_BEGING(XGuiApplication)
XCLASS_DEFINE_EXTEND_END(XGuiApplication, XCoreApplication)



/**
 * @brief      XWindow 不透明前向声明回退。
 * @details    仅当 XWINDOW_ON=0 时启用，使 XGuiApplication.h 中的窗口指针
 *             类型仍可编译；此时窗口相关 API 在 XGuiApplication.c 中退化为
 *             空实现。
 */
#if !XWINDOW_ON
typedef struct XWindow XWindow;
#endif /* !XWINDOW_ON */

/**
 * @brief      XScreen 不透明前向声明回退。
 * @details    仅当 XSCREEN_ON=0 时启用，使 XGuiApplication.h 中的屏幕指针
 *             类型仍可编译；此时屏幕相关 API 在 XGuiApplication.c 中退化为
 *             空实现。
 */
#if !XSCREEN_ON
typedef struct XScreen XScreen;
#endif /* !XSCREEN_ON */

/**
 * @brief      XCursor 不透明前向声明回退。
 * @details    仅当 XCURSOR_ON=0 时启用，使覆盖光标 API 的指针类型仍可编译；
 *             此时光标相关 API 在 XGuiApplication.c 中退化为空实现。
 */
#if XGUIAPPLICATION_ON && !XCURSOR_ON
typedef struct XCursor XCursor;
#endif /* XGUIAPPLICATION_ON && !XCURSOR_ON */

/**
 * @brief      XStyleHints / XClipboard 不透明前向声明回退。
 * @details    仅当子开关关闭时启用，使结构体成员指针类型仍可编译；此时
 *             styleHints()/clipboard() 在 XGuiApplication.c 中返回 NULL。
 */
#if XGUIAPPLICATION_ON && !XSTYLEHINTS_ON
typedef struct XStyleHints XStyleHints;
#endif /* XGUIAPPLICATION_ON && !XSTYLEHINTS_ON */
#if XGUIAPPLICATION_ON && !XCLIPBOARD_ON
typedef struct XClipboard XClipboard;
#endif /* XGUIAPPLICATION_ON && !XCLIPBOARD_ON */


/* ==================== 枚举类型（对标 Qt::LayoutDirection 等） ==================== */

/** @brief 布局方向（对标 Qt 6.8 Qt::LayoutDirection，取值一致）。 */
typedef enum XGuiLayoutDirection
{
    XGuiLayoutDirection_LeftToRight = 0, /**< 从左到右。 */
    XGuiLayoutDirection_RightToLeft,     /**< 从右到左。 */
    XGuiLayoutDirection_Auto             /**< 自动（跟随区域设置）。 */
} XGuiLayoutDirection;

/** @brief 应用状态（对标 Qt 6.8 Qt::ApplicationState，取值一致，可与位运算）。 */
typedef enum XGuiApplicationState
{
    XGuiApplicationState_Suspended = 0x00000000, /**< 应用挂起。 */
    XGuiApplicationState_Hidden    = 0x00000001, /**< 应用隐藏。 */
    XGuiApplicationState_Inactive  = 0x00000002, /**< 应用非活动。 */
    XGuiApplicationState_Active    = 0x00000004  /**< 应用活动。 */
} XGuiApplicationState;

/** @brief 高分屏缩放因子取整策略（对标 Qt 6.8 Qt::HighDpiScaleFactorRoundingPolicy）。 */
typedef enum XGuiDpiRoundingPolicy
{
    XGuiDpiRoundingPolicy_Unset = 0,          /**< 未指定（由平台决定）。 */
    XGuiDpiRoundingPolicy_Round,              /**< 四舍五入。 */
    XGuiDpiRoundingPolicy_Ceil,               /**< 向上取整。 */
    XGuiDpiRoundingPolicy_Floor,              /**< 向下取整。 */
    XGuiDpiRoundingPolicy_RoundPreferFloor,   /**< 四舍五入且优先向下。 */
    XGuiDpiRoundingPolicy_PassThrough         /**< 透传（不缩放）。 */
} XGuiDpiRoundingPolicy;

/**
 * @brief      会话管理器不透明句柄（对标 QSessionManager）。
 * @details    当前仅作为 commitDataRequest / saveStateRequest 信号的参数
 *             类型占位；本实现不接会话管理器，发射时传 NULL。
 */
typedef struct XSessionManager XSessionManager;

/**
 * @brief      XGuiApplication GUI 应用对象；m_class 必须是第一个成员。
 * @details    XCoreApplication 已处理的字段见 XCoreApplication 定义；尾部
 *             GUI 字段由本类拥有并释放。调用方不得手工修改任何字段。
 */
typedef struct XGuiApplication
{
    XCoreApplication m_class;             /**< 基类成员（必须是第一个）。 */
    XString* m_displayName;               /**< 应用显示名（拥有）。 */
    XString* m_desktopFileName;           /**< 桌面文件名（拥有）。 */
    XString* m_platformName;              /**< 平台名（拥有，恒 "xiniyue-embedded"）。 */
    XString* m_sessionId;                 /**< 会话 ID（拥有）。 */
    XString* m_sessionKey;                /**< 会话键（拥有）。 */
    XIcon*   m_windowIcon;                /**< 应用窗口图标（拥有堆拷贝）。 */
    XFont*   m_font;                      /**< 应用字体（拥有堆拷贝）。 */
#if XPALETTE_ON
    XPalette m_palette;                   /**< 应用调色板（值类型）。 */
#endif /* XPALETTE_ON */
    XVector* m_overrideStack;             /**< 光标覆盖栈（XCursor* 堆拷贝）。 */
    XVector* m_windows;                   /**< 窗口注册表（XWindow* 借用指针）。 */
    XWindow* m_focusWindow;               /**< 焦点窗口（借用指针）。 */
    XWindow* m_modalWindow;               /**< 模态窗口（借用指针）。 */
    XObject* m_focusObject;               /**< 焦点对象（借用指针）。 */
    XGuiLayoutDirection  m_layoutDirection;    /**< 当前有效布局方向（恒为 LTR 或 RTL）。 */
    XGuiLayoutDirection  m_requestedLayoutDirection; /**< 调用方请求方向；Auto 时由平台语言解析。 */
    XGuiApplicationState m_applicationState;   /**< 应用状态。 */
    XGuiDpiRoundingPolicy m_dpiPolicy;         /**< 高分屏缩放取整策略。 */
    XKeyboardModifiers   m_keyboardModifiers;  /**< 当前键盘修饰键。 */
    XMouseButton         m_mouseButtons;       /**< 当前按下的鼠标键。 */
    int64_t  m_badgeNumber;               /**< 应用徽标数。 */
    bool m_desktopSettingsAware;          /**< 是否感知桌面设置（默认 true）。 */
    bool m_quitOnLastWindowClosed;        /**< 最后窗口关闭时退出（默认 true）。 */
    bool m_isSessionRestored;             /**< 是否从会话恢复。 */
    bool m_isSavingSession;               /**< 是否正在保存会话。 */
    XStyleHints* m_styleHints;            /**< 样式提示惰性单例（拥有）。 */
    XClipboard*  m_clipboard;             /**< 剪贴板惰性单例（拥有）。 */
#if XPLATFORMINTEGRATION_ON
    XPlatformIntegration* m_platformIntegration; /**< 平台集成层（拥有，init 时创建）。 */
    XHandle m_nativeEventPump; /**< 主事件分发器中的原生事件泵登记句柄。 */
#endif /* XPLATFORMINTEGRATION_ON */
#if XINPUTMETHOD_ON
    XInputMethod* m_inputMethod;          /**< 输入法惰性单例（拥有，与集成层双向绑定）。 */
#endif /* XINPUTMETHOD_ON */
} XGuiApplication;

/** @brief 获取全局 XGuiApplication 实例的便捷宏（对标 qGuiApp）。 */
#define xGuiApp XGuiApplication_instance()

/* ==================== 生命周期管理 ==================== */

/**
 * @brief      初始化 XGuiApplication 类虚函数表（继承 XCoreApplication 全部
 *             虚槽，仅重载析构；notify/event 沿用父类分发）。
 * @return     共享 XVtable 指针。
 */
XVtable* XGuiApplication_class_init(void);

/**
 * @brief      获取全局 XGuiApplication 实例（对标 QGuiApplication::instance）。
 * @return     全局实例指针；未初始化时返回 NULL。
 */
XGuiApplication* XGuiApplication_instance(void);

/**
 * @brief      创建 XGuiApplication 实例（堆分配；进程内唯一）。
 * @param      memory 对象内存类型。
 * @param      argc 命令行参数个数。
 * @param      argv 命令行参数数组。
 * @return     新实例指针；已存在实例时返回已有实例，失败返回 NULL。
 */
XGuiApplication* XGuiApplication_create_ex(XMemoryType memory, int argc, char** argv);

/**
 * @brief      初始化 XGuiApplication 实例（栈分配用；进程内唯一）。
 * @param      app  待初始化指针。
 * @param      argc 命令行参数个数。
 * @param      argv 命令行参数数组。
 * @note       必须先初始化 XCoreApplication 基类再套用本类虚表，本函数内部
 *             完成该顺序；释放时用 XGuiApplication_deinit_base。
 */
void XGuiApplication_init(XGuiApplication* app, int argc, char** argv);

/** @brief 通过 XClass 虚表释放 XGuiApplication 资源（栈/外部存储对象使用）。 */
#define XGuiApplication_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XGuiApplication 对象。 */
#define XGuiApplication_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 应用元信息（对标 QGuiApplication） ==================== */

/**
 * @brief      设置应用显示名（对标 QGuiApplication::setApplicationDisplayName）。
 * @param      name UTF-8 显示名；可为 NULL（清空）。
 */
void XGuiApplication_setApplicationDisplayName(const XString* name);

/**
 * @brief      获取应用显示名（对标 QGuiApplication::applicationDisplayName）。
 * @return     内部借用指针；未设置时返回 NULL。
 */
const XString* XGuiApplication_applicationDisplayName(void);

/**
 * @brief      设置桌面文件名（对标 QGuiApplication::setDesktopFileName）。
 * @param      name UTF-8 文件名（可带 .desktop 后缀）；可为 NULL。
 */
void XGuiApplication_setDesktopFileName(const XString* name);

/**
 * @brief      获取桌面文件名（对标 QGuiApplication::desktopFileName）。
 * @return     内部借用指针；未设置时返回 NULL。
 */
const XString* XGuiApplication_desktopFileName(void);

/**
 * @brief      获取平台名（对标 QGuiApplication::platformName）。
 * @return     内部借用指针；恒为 "xiniyue-embedded"。
 */
const XString* XGuiApplication_platformName(void);

/**
 * @brief      设置应用徽标数（对标 QGuiApplication::setBadgeNumber）。
 * @param      number 徽标数；0 表示清除徽标。
 */
void XGuiApplication_setBadgeNumber(int64_t number);

/**
 * @brief      获取应用徽标数（对标 QGuiApplication::badgeNumber）。
 * @return     徽标数。
 */
int64_t XGuiApplication_badgeNumber(void);

/* ==================== 窗口注册表（对标 QGuiApplication::allWindows 等） ==================== */

/**
 * @brief      返回全部已登记窗口（对标 QGuiApplication::allWindows）。
 * @note       含子窗口；顺序按登记时间。
 * @return     新建的 XVector（元素为 XWindow* 借用指针），调用方用
 *             XVector_delete_base 释放；无窗口时返回空列表。
 */
XVector* XGuiApplication_allWindows(void);

/**
 * @brief      返回全部顶层窗口（对标 QGuiApplication::topLevelWindows）。
 * @details    过滤无父窗口的登记窗口（XWindow_parent 无结果）。
 * @return     新建的 XVector（元素为 XWindow* 借用指针），调用方释放。
 */
XVector* XGuiApplication_topLevelWindows(void);

/**
 * @brief      返回指定点所在的顶层窗口（对标 QGuiApplication::topLevelAt）。
 * @details    按登记逆序查找包含该点的顶层窗口（后登记的视为更上层），
 *             优先可见窗口；不可见窗口查到也返回（勿释放）。
 * @param      pos 全局坐标点。
 * @return     顶层窗口借用指针；无匹配返回 NULL。
 */
XWindow* XGuiApplication_topLevelAt(const XPoint* pos);

/**
 * @brief      平台/测试接入钩子：登记窗口（对标 QGuiApplication 内部 addWindow）。
 * @details    幂等：同一窗口重复登记是 no-op。窗口销毁前调用方必须
 *             调用 XGuiApplication_removeWindow，否则出现悬挂借用指针。
 * @param      win 目标窗口；可为 NULL。
 */
void XGuiApplication_addWindow(XWindow* win);

/** @brief 将注册表中的借用窗口指针原子替换为新对象（移动语义使用）。 */
bool XGuiApplication_replaceWindow(XWindow* oldWindow, XWindow* newWindow);

/**
 * @brief      平台/测试接入钩子：注销窗口（对标 QGuiApplication 内部 removeWindow）。
 * @details    移除最后一个顶层窗口时发射 lastWindowClosed；若
 *             quitOnLastWindowClosed 为 true，再请求退出事件循环。
 * @param      win 目标窗口；可为 NULL。
 */
void XGuiApplication_removeWindow(XWindow* win);

/* ==================== 图标（对标 QGuiApplication::setWindowIcon / windowIcon） ==================== */

/**
 * @brief      设置应用窗口图标（对标 QGuiApplication::setWindowIcon）。
 * @param      icon 源图标；可为 NULL（清空）。
 */
void XGuiApplication_setWindowIcon(const XIcon* icon);

/**
 * @brief      获取应用窗口图标（对标 QGuiApplication::windowIcon）。
 * @return     新建 XIcon 堆拷贝；未设置返回 NULL，调用方用 XIcon_delete_base
 *             释放。
 */
XIcon* XGuiApplication_windowIcon(void);

/* ==================== 焦点 / 模态（对标 QGuiApplication::focusWindow 等） ==================== */

/**
 * @brief      获取焦点窗口（对标 QGuiApplication::focusWindow）。
 * @return     借用指针；无焦点窗口返回 NULL。
 */
XWindow* XGuiApplication_focusWindow(void);

/**
 * @brief      获取焦点对象（对标 QGuiApplication::focusObject）。
 * @return     借用指针；无焦点对象返回 NULL。
 */
XObject* XGuiApplication_focusObject(void);

/**
 * @brief      获取顶层模态窗口（对标 QGuiApplication::modalWindow）。
 * @return     借用指针；无模态窗口返回 NULL。
 */
XWindow* XGuiApplication_modalWindow(void);

/**
 * @brief      平台接入钩子：设置焦点窗口与焦点对象。
 * @details    变更时依次发射 focusWindowChanged / focusObjectChanged；
 *             focusObject 可为 NULL（默认取窗口自身，与 XWindow_focusObject
 *             语义一致）。
 * @param      window 焦点窗口；可为 NULL。
 * @param      object 焦点对象；可为 NULL。
 */
void XGuiApplication_setFocusWindow(XWindow* window, XObject* object);

/**
 * @brief      平台接入钩子：设置顶层模态窗口。
 * @param      window 模态窗口；NULL 清除。
 */
void XGuiApplication_setModalWindow(XWindow* window);

/* ==================== 屏幕（对标 QGuiApplication::primaryScreen / screens 等） ==================== */

/**
 * @brief      获取主屏幕（对标 QGuiApplication::primaryScreen）。
 * @return     借用指针；未设置返回 NULL。
 */
XScreen* XGuiApplication_primaryScreen(void);

/**
 * @brief      获取全部注册屏幕（对标 QGuiApplication::screens）。
 * @return     新建的 XVector（元素为 XScreen* 借用指针），调用方释放。
 */
XVector* XGuiApplication_screens(void);

/**
 * @brief      返回包含指定点的屏幕（对标 QGuiApplication::screenAt）。
 * @param      pos 全局坐标点；可为 NULL。
 * @return     借用指针；无匹配返回 NULL。
 */
XScreen* XGuiApplication_screenAt(const XPoint* pos);

/**
 * @brief      获取应用设备像素比（对标 QGuiApplication::devicePixelRatio）。
 * @details    返回当前所有屏幕 devicePixelRatio 的最大值；未登记屏幕时返回 1.0。
 * @return     应用 devicePixelRatio。
 */
float XGuiApplication_devicePixelRatio(void);

/**
 * @brief      平台接入钩子：登记屏幕并发射 screenAdded（对标 QGuiApplication::screenAdded）。
 * @details    内部调用 XScreen_register 保持与 XScreen 注册表一致。
 * @param      screen 目标屏幕；可为 NULL。
 */
void XGuiApplication_screenAdded(XScreen* screen);

/**
 * @brief      平台接入钩子：注销屏幕并发射 screenRemoved。
 * @param      screen 目标屏幕；可为 NULL。
 */
void XGuiApplication_screenRemoved(XScreen* screen);

/**
 * @brief      平台接入钩子：设置主屏幕并发射 primaryScreenChanged。
 * @param      screen 目标屏幕；可为 NULL（清空主屏）。
 */
void XGuiApplication_setPrimaryScreen(XScreen* screen);

/* ==================== 光标覆盖栈（对标 QGuiApplication::overrideCursor 等） ==================== */

/**
 * @brief      获取当前覆盖光标（对标 QGuiApplication::overrideCursor）。
 * @return     栈顶借用指针（勿释放）；栈空返回 NULL。
 */
XCursor* XGuiApplication_overrideCursor(void);

/**
 * @brief      压入覆盖光标（对标 QGuiApplication::setOverrideCursor）。
 * @details    深拷贝入栈；恢复顺序与入栈相反。
 * @param      cursor 源光标；可为 NULL（等价入栈默认箭头）。
 */
void XGuiApplication_setOverrideCursor(const XCursor* cursor);

/**
 * @brief      替换栈顶覆盖光标（对标 QGuiApplication::changeOverrideCursor）。
 * @details    栈非空时替换栈顶；栈空时等价 setOverrideCursor。
 * @param      cursor 源光标；可为 NULL。
 */
void XGuiApplication_changeOverrideCursor(const XCursor* cursor);

/**
 * @brief      弹出覆盖光标（对标 QGuiApplication::restoreOverrideCursor）。
 * @details    栈空时 no-op。
 */
void XGuiApplication_restoreOverrideCursor(void);

/* ==================== 字体 / 调色板（对标 QGuiApplication::font / palette） ==================== */

/**
 * @brief      设置应用字体（对标 QGuiApplication::setFont）。
 * @details    深拷贝保存并发射已弃用的 fontChanged 信号。
 * @param      font 源字体；可为 NULL（清空）。
 */
void XGuiApplication_setFont(const XFont* font);

/**
 * @brief      获取应用字体（对标 QGuiApplication::font）。
 * @return     新建 XFont 堆拷贝；未设置返回 NULL，调用方用 XFont_delete_base
 *             释放。
 */
XFont* XGuiApplication_font(void);

#if XPALETTE_ON
/**
 * @brief      设置应用调色板（对标 QGuiApplication::setPalette）。
 * @details    值拷贝保存并发射已弃用的 paletteChanged 信号。
 * @param      palette 源调色板；可为 NULL（等价重置为默认浅色主题）。
 */
void XGuiApplication_setPalette(const XPalette* palette);

/**
 * @brief      获取应用调色板（对标 QGuiApplication::palette）。
 * @return     调色板值副本。
 */
XPalette XGuiApplication_palette(void);
#endif /* XPALETTE_ON */

/* ==================== 输入状态（对标 QGuiApplication::keyboardModifiers 等） ==================== */

/**
 * @brief      获取当前键盘修饰键（对标 QGuiApplication::keyboardModifiers）。
 * @return     修饰键位掩码。
 */
XKeyboardModifiers XGuiApplication_keyboardModifiers(void);

/**
 * @brief      查询键盘修饰键（对标 QGuiApplication::queryKeyboardModifiers）。
 * @details    经 XPlatformIntegration 查询原生输入设备的即时状态；无原生
 *             后端或后端不可用时回退到最后一个已派发事件的程序化状态。
 * @return     修饰键位掩码。
 */
XKeyboardModifiers XGuiApplication_queryKeyboardModifiers(void);

/**
 * @brief      设置键盘修饰键（平台注入接口，无对应 Qt 公开 setter）。
 * @param      modifiers 修饰键位掩码。
 */
void XGuiApplication_setKeyboardModifiers(XKeyboardModifiers modifiers);

/**
 * @brief      获取当前按下的鼠标键（对标 QGuiApplication::mouseButtons）。
 * @return     鼠标键位掩码。
 */
XMouseButton XGuiApplication_mouseButtons(void);

/**
 * @brief      设置当前按下的鼠标键（平台注入接口）。
 * @param      buttons 鼠标键位掩码。
 */
void XGuiApplication_setMouseButtons(XMouseButton buttons);

/* ==================== 布局方向（对标 QGuiApplication::layoutDirection 等） ==================== */

/**
 * @brief      设置布局方向（对标 QGuiApplication::setLayoutDirection）。
 * @details    Auto 不作为可观察的布局结果保存；它会按平台输入上下文的当前
 *             语言解析为有效 LTR/RTL，并且仅在有效方向变化时发射信号。
 * @param      direction 目标方向。
 */
void XGuiApplication_setLayoutDirection(XGuiLayoutDirection direction);

/**
 * @brief      通知平台输入方向已改变。
 * @details    仅由 XPlatformInputContext 等平台抽象层调用。当应用请求方向为
 *             Auto 时，重新解析有效 LTR/RTL，并且仅在有效方向变化时发射
 *             layoutDirectionChanged；显式 LTR/RTL 请求不会受此通知影响。
 *             应用业务代码不应直接调用本函数。
 */
void XGuiApplication_notifyPlatformInputDirectionChanged(void);

/**
 * @brief      获取布局方向（对标 QGuiApplication::layoutDirection）。
 * @return     当前有效方向，恒为 LeftToRight 或 RightToLeft。
 */
XGuiLayoutDirection XGuiApplication_layoutDirection(void);

/** @brief 是否从右到左（对标 isRightToLeft）。 */
bool XGuiApplication_isRightToLeft(void);

/** @brief 是否从左到右（对标 isLeftToRight）。 */
bool XGuiApplication_isLeftToRight(void);

/* ==================== 样式提示 / 剪贴板 / 输入法（对标 QGuiApplication 单例访问器） ==================== */

/**
 * @brief      获取样式提示单例（对标 QGuiApplication::styleHints）。
 * @return     内部单例借用指针；XSTYLEHINTS_ON=0 时返回 NULL，本对象由
 *             XGuiApplication 拥有并可随应用销毁。
 */
XStyleHints* XGuiApplication_styleHints(void);

/**
 * @brief      获取剪贴板单例（对标 QGuiApplication::clipboard）。
 * @return     内部单例借用指针；XCLIPBOARD_ON=0 时返回 NULL，本对象由
 *             XGuiApplication 拥有并可随应用销毁。
 */
XClipboard* XGuiApplication_clipboard(void);

/**
 * @brief      获取输入法对象（对标 QGuiApplication::inputMethod；惰性单例）。
 * @details    首次调用创建 XInputMethod 并与集成层输入上下文双向绑定；
 *             网络层转发经 XPlatformInputContext 承载。
 * @return     XInputMethod* 借用指针；未初始化或 XINPUTMETHOD_ON=0 返回 NULL。
 */
XInputMethod* XGuiApplication_inputMethod(void);

/* ==================== 平台接口（对标 QGuiApplication::platformNativeInterface 等） ==================== */

/**
 * @brief      获取平台原生接口（对标 QGuiApplication::platformNativeInterface）。
 * @details    转发集成层内建的 XPlatformNativeInterface；资源表/窗口属性
 *             均可用（见 XPlatformNativeInterface.h）。
 * @return     XPlatformNativeInterface* 借用指针；未初始化或平台开关关闭返回 NULL。
 */
XPlatformNativeInterface* XGuiApplication_platformNativeInterface(void);

/**
 * @brief      按名称解析平台函数指针（对标 QGuiApplication::platformFunction）。
 * @param      functionName UTF-8 函数名；可为 NULL。
 * @return     已注册函数指针；未注册或平台接口不可用时返回 NULL。
 */
void* XGuiApplication_platformFunction(const char* functionName);

/* ==================== 桌面设置 / 退出策略（对标 QGuiApplication） ==================== */

/**
 * @brief      设置是否感知桌面设置（对标 QGuiApplication::setDesktopSettingsAware）。
 * @param      on 布尔值。
 */
void XGuiApplication_setDesktopSettingsAware(bool on);

/**
 * @brief      查询是否感知桌面设置（对标 QGuiApplication::desktopSettingsAware）。
 * @return     布尔值。
 */
bool XGuiApplication_desktopSettingsAware(void);

/**
 * @brief      设置最后窗口关闭时是否退出（对标 QGuiApplication::setQuitOnLastWindowClosed）。
 * @param      quit 布尔值。
 */
void XGuiApplication_setQuitOnLastWindowClosed(bool quit);

/**
 * @brief      查询最后窗口关闭时是否退出（对标 QGuiApplication::quitOnLastWindowClosed）。
 * @return     布尔值。
 */
bool XGuiApplication_quitOnLastWindowClosed(void);

/* ==================== 应用状态 / DPI 策略（对标 QGuiApplication） ==================== */

/**
 * @brief      设置应用状态（平台注入接口；对标 Qt 内部 setApplicationState）。
 * @details    变更时发射 applicationStateChanged。
 * @param      state 目标状态。
 */
void XGuiApplication_setApplicationState(XGuiApplicationState state);

/**
 * @brief      获取应用状态（对标 QGuiApplication::applicationState）。
 * @return     当前状态。
 */
XGuiApplicationState XGuiApplication_applicationState(void);

/**
 * @brief      设置高分屏缩放因子取整策略（对标
 *             QGuiApplication::setHighDpiScaleFactorRoundingPolicy）。
 * @param      policy 目标策略。
 */
void XGuiApplication_setHighDpiScaleFactorRoundingPolicy(XGuiDpiRoundingPolicy policy);

/**
 * @brief      获取高分屏缩放因子取整策略（对标
 *             QGuiApplication::highDpiScaleFactorRoundingPolicy）。
 * @return     当前策略。
 */
XGuiDpiRoundingPolicy XGuiApplication_highDpiScaleFactorRoundingPolicy(void);

/* ==================== 事件循环 / 通知（对标 QGuiApplication::exec / notify） ==================== */

/** @brief 复用 XCoreApplication 的事件循环 API，不重复声明转发函数。 */
#define XGuiApplication_exec XCoreApplication_exec
#define XGuiApplication_quit XCoreApplication_quit

/** @brief 复用 XCoreApplication 的通知入口。 */
#define XGuiApplication_notify XCoreApplication_notify_base

/* ==================== 事件发送/投递（直接复用 XCoreApplication） ==================== */

#define XGuiApplication_sendEvent XCoreApplication_sendEvent
#define XGuiApplication_postEvent XCoreApplication_postEvent
#define XGuiApplication_sendPostedEvents XCoreApplication_sendPostedEvents
#define XGuiApplication_removePostedEvents XCoreApplication_removePostedEvents
#define XGuiApplication_sendSpontaneousEvent XCoreApplication_sendSpontaneousEvent

/**
 * @brief      处理等待中的事件（对标 QCoreApplication::processEvents）。
 * @details    先泵空平台原生事件源（X11 XNextEvent / Win32 PeekMessage），
 *             再处理 XCoreApplication 公共事件队列，形成「平台注入 -> 事件
 *             分发 -> 窗口重绘（XBackingStore）-> 上屏」闭环。
 * @param      flags 处理标志位组合（XEventLoopProcessEventsFlags）。
 */
void XGuiApplication_processEvents(XEventLoopProcessEventsFlags flags);

/**
 * @brief      阻塞等待平台原生事件就绪并处理一批（对标事件分发器的
 *             processEvents(WaitForMoreEvents)）。
 * @details    X11 用 poll(XConnectionNumber)，Win32 用
 *             MsgWaitForMultipleObjects(QS_ALLINPUT)；提供给自绘窗口主循环
 *             作为「无系统事件循环」时的阻塞事件源，避免忙轮询空转 CPU。
 *             超时/无窗口系统返回 false，就绪返回 true（已泵空一批并注入
 *             窗口事件）。返回后调用方一般应立即调用 XGuiApplication_
 *             processEvents 完成重绘/上屏处理。
 * @param      maxMilliseconds 最大阻塞毫秒；0 表示只做一次立即探测，
 *             负数表示无限等待（不推荐用于 UI 主循环）。
 * @return     true 已就绪并处理平台原生事件；false 超时、被打断或平台
 *             窗口系统不可用。
 */
bool XGuiApplication_waitForEvents(int maxMilliseconds);

/* ==================== 会话（对标 QGuiApplication::isSessionRestored 等） ==================== */

/** @brief 是否从会话恢复（对标 isSessionRestored）。 */
bool XGuiApplication_isSessionRestored(void);

/** @brief 获取会话 ID（对标 sessionId）；返回内部借用指针。 */
const XString* XGuiApplication_sessionId(void);

/** @brief 获取会话键（对标 sessionKey）；返回内部借用指针。 */
const XString* XGuiApplication_sessionKey(void);

/** @brief 是否正在保存会话（对标 isSavingSession）。 */
bool XGuiApplication_isSavingSession(void);

/**
 * @brief      平台注入：设置会话恢复/保存状态。
 * @param      restored 是否从会话恢复。
 * @param      saving   是否正在保存会话。
 * @param      id       会话 ID（UTF-8，内部深拷贝）。
 * @param      key      会话键（UTF-8，内部深拷贝）。
 */
void XGuiApplication_setSessionState(bool restored, bool saving, const char* id, const char* key);

/* ==================== 同步（对标 QGuiApplication::sync） ==================== */

/**
 * @brief      同步窗口系统状态（对标 QGuiApplication::sync）。
 * @details    依次处理应用事件、调用具备 SyncState 能力的平台同步、再次
 *             处理应用事件并冲刷窗口系统事件队列，与 Qt 6.8 的 sync 顺序
 *             一致；无平台同步能力时仅完成首轮应用事件处理。
 */
void XGuiApplication_sync(void);

/* ==================== 信号（14 个，对标 QGuiApplication 全部信号） ==================== */

/** @brief 字体数据库变化信号（对标 fontDatabaseChanged）。 */
void* XGuiApplication_fontDatabaseChanged_signal(XGuiApplication* app);

/** @brief 屏幕登记信号（对标 screenAdded）。 */
void* XGuiApplication_screenAdded_signal(XGuiApplication* app, XScreen* screen);

/** @brief 屏幕注销信号（对标 screenRemoved）。 */
void* XGuiApplication_screenRemoved_signal(XGuiApplication* app, XScreen* screen);

/** @brief 主屏幕变化信号（对标 primaryScreenChanged）。 */
void* XGuiApplication_primaryScreenChanged_signal(XGuiApplication* app, XScreen* screen);

/** @brief 最后窗口关闭信号（对标 lastWindowClosed）。 */
void* XGuiApplication_lastWindowClosed_signal(XGuiApplication* app);

/** @brief 焦点对象变化信号（对标 focusObjectChanged）。 */
void* XGuiApplication_focusObjectChanged_signal(XGuiApplication* app, XObject* focusObject);

/** @brief 焦点窗口变化信号（对标 focusWindowChanged）。 */
void* XGuiApplication_focusWindowChanged_signal(XGuiApplication* app, XWindow* focusWindow);

/** @brief 应用状态变化信号（对标 applicationStateChanged）。 */
void* XGuiApplication_applicationStateChanged_signal(XGuiApplication* app, XGuiApplicationState state);

/** @brief 布局方向变化信号（对标 layoutDirectionChanged）。 */
void* XGuiApplication_layoutDirectionChanged_signal(XGuiApplication* app, XGuiLayoutDirection direction);

/** @brief 提交数据请求信号（对标 commitDataRequest；参数恒为 NULL）。 */
void* XGuiApplication_commitDataRequest_signal(XGuiApplication* app, XSessionManager* sessionManager);

/** @brief 保存状态请求信号（对标 saveStateRequest；参数恒为 NULL）。 */
void* XGuiApplication_saveStateRequest_signal(XGuiApplication* app, XSessionManager* sessionManager);

/** @brief 应用显示名变化信号（对标 applicationDisplayNameChanged）。 */
void* XGuiApplication_applicationDisplayNameChanged_signal(XGuiApplication* app);

/** @brief 调色板变化信号（对标已弃用的 paletteChanged）。 */
void* XGuiApplication_paletteChanged_signal(XGuiApplication* app, XPalette* palette);

/** @brief 字体变化信号（对标已弃用的 fontChanged）。 */
void* XGuiApplication_fontChanged_signal(XGuiApplication* app, XFont* font);

#endif /* XGUIAPPLICATION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XGUIAPPLICATION_H */
