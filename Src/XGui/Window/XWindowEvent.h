/******************************************************************************
 * @file       XWindowEvent.h
 * @brief      窗口事件类合集（对标 Qt 6.8 qevent.h 中的 QResizeEvent /
 *             QExposeEvent / QPaintEvent / QCloseEvent / QShowEvent /
 *             QHideEvent / QFocusEvent）。
 * @details    本文件为 GUI 事件投递闭环提供「带负载」的具体事件类：
 *             - XResizeEvent：承载调整大小前后尺寸（size/oldSize），
 *               与 XWindow_resizeEvent_base 配套，对标 QResizeEvent；
 *             - XExposeEvent：承载平台暴露（需要重绘）的区域 region，
 *               与 XWindow_exposeEvent_base 配套，对标 QExposeEvent；
 *             - XPaintEvent：承载绘制区域 region 及其外接矩形 rect，
 *               与 XWindow_paintEvent_base 配套，对标 QPaintEvent；
 *             - XCloseEvent：窗口关闭事件，与 XWindow_closeEvent_base
 *               配套，对标 QCloseEvent；
 *             - XShowEvent / XHideEvent：显示/隐藏通知事件，与
 *               XWindow_showEvent_base / XWindow_hideEvent_base 配套，
 *               对标 QShowEvent / QHideEvent；
 *             - XFocusEvent：焦点进入/离开事件（携带焦点原因 reason，
 *               类型为 XEVENT_TYPE_FOCUS_IN / XEVENT_TYPE_FOCUS_OUT），
 *               与 XWindow_focusInEvent_base / XWindow_focusOutEvent_base
 *               配套，对标 QFocusEvent；
 *             - XWheelEvent：滚轮事件（携带局部/全局坐标、角度增量、按键
 *               与修饰键），与 XWindow_wheelEvent_base 配套，对标
 *               QWheelEvent；
 *             - XEnterEvent：指针进入事件（携带局部/全局坐标），与
 *               XWindow_enterEvent_base 配套，对标 QEnterEvent；指针离开
 *               为无负载的普通 XEvent（XEVENT_TYPE_LEAVE），与
 *               XWindow_leaveEvent_base 配套。
 *             所有事件类均继承 XEvent（m_class 为首成员），因此可直接
 *             强制转换成 XEvent* 送入 XCoreApplication_sendEvent /
 *             postEvent / sendSpontaneousEvent 投递，也可由窗口事件槽
 *             （形如 void slot(XWindow*, XEvent*)）向下转型读取负载。
 *             带动态区域的事件（XExposeEvent / XPaintEvent）实现了
 *             deinit/clone 虚槽：析构时释放内部 XRegion，克隆时深拷贝
 *             区域，保证队列/克隆路径无悬挂指针、无共享内存。
 * @note       模块总开关 XWINDOWEVENT_ON 定义于 XGuiConfig.h；置 0 时
 *             裁剪本文件全部公共 API。本模块只依赖 XEvent / XGeometry，
 *             不依赖任何平台 API，嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XWINDOWEVENT_H
#define XWINDOWEVENT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGeometry.h"
#include "XString.h"
#if XWINDOWEVENT_ON

/* ========================================================================== */
/*           XFocusReason 焦点原因枚举（对标 Qt 6.8 Qt::FocusReason）          */
/* ========================================================================== */
/** @brief 焦点变化原因（对标 Qt::FocusReason）。
 * @details 与 Qt::FocusReason 取值一一对应：鼠标操作、Tab 键、反向 Tab、
 *          窗口激活、弹出窗口、快捷键、菜单栏、其他程序原因、无原因。 */
typedef enum XFocusReason
{
    XFocusReason_Mouse = 0,        /**< 鼠标点击引起的焦点变化。 */
    XFocusReason_Tab,              /**< Tab 键向前导航引起的焦点变化。 */
    XFocusReason_Backtab,          /**< 反向 Tab（Shift+Tab）导航引起的焦点变化。 */
    XFocusReason_ActiveWindow,     /**< 活动窗口切换引起的焦点变化。 */
    XFocusReason_Popup,            /**< 弹出窗口切换引起的焦点变化。 */
    XFocusReason_Shortcut,         /**< 快捷键引起的焦点变化。 */
    XFocusReason_MenuBar,          /**< 菜单栏导航引起的焦点变化。 */
    XFocusReason_Other,            /**< 其他程序原因。 */
    XFocusReason_NoReason          /**< 无原因。 */
} XFocusReason;

/* ========================================================================== */
/*              XInputMethodEvent 输入法事件（对标 QInputMethodEvent）         */
/* ========================================================================== */
/** @brief 声明输入法事件虚函数：字符串成员需要深拷贝、析构和克隆。 */
XCLASS_DEFINE_BEGING(XInputMethodEvent)
XCLASS_DEFINE_EXTEND_END(XInputMethodEvent, XEvent)

/**
 * @brief 输入法组合/提交事件。
 * @details preeditString 表示仍可修改的组合文本；commitString 表示已确认
 *          应插入的文本。replacementStart/replacementLength 以当前光标为
 *          基准表达替换范围，cursorPosition/anchorPosition 位于 preedit 内。
 */
typedef struct XInputMethodEvent
{
    XEvent   m_class;             /**< 继承 XEvent；必须为第一个成员。 */
    XString* m_preeditString;     /**< 组合文本（事件拥有，可为空）。 */
    XString* m_commitString;      /**< 已提交文本（事件拥有，可为空）。 */
    int      m_replacementStart;  /**< 相对当前光标的替换起始。 */
    int      m_replacementLength; /**< 替换长度。 */
    int      m_cursorPosition;    /**< preedit 内光标位置；-1 表示未知。 */
    int      m_anchorPosition;    /**< preedit 内锚点位置；-1 表示未知。 */
} XInputMethodEvent;

XVtable* XInputMethodEvent_class_init(void);
XInputMethodEvent* XInputMethodEvent_create_ex(
        XMemoryType memory, const XString* preeditString,
        const XString* commitString, int replacementStart,
        int replacementLength, int cursorPosition, int anchorPosition);
#define XInputMethodEvent_create(preeditString, commitString, replacementStart, \
                                 replacementLength, cursorPosition, anchorPosition) \
    XInputMethodEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (preeditString), \
        (commitString), (replacementStart), (replacementLength), \
        (cursorPosition), (anchorPosition))
void XInputMethodEvent_init(XInputMethodEvent* event,
                            const XString* preeditString,
                            const XString* commitString,
                            int replacementStart, int replacementLength,
                            int cursorPosition, int anchorPosition);
/** @brief 返回 preedit 的深拷贝；调用方负责释放。 */
XString* XInputMethodEvent_preeditString(const XInputMethodEvent* event);
/** @brief 返回 commit 的深拷贝；调用方负责释放。 */
XString* XInputMethodEvent_commitString(const XInputMethodEvent* event);
int XInputMethodEvent_replacementStart(const XInputMethodEvent* event);
int XInputMethodEvent_replacementLength(const XInputMethodEvent* event);
int XInputMethodEvent_cursorPosition(const XInputMethodEvent* event);
int XInputMethodEvent_anchorPosition(const XInputMethodEvent* event);
#define XInputMethodEvent_delete_base XEvent_delete_base
#define XInputMethodEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XDropEvent 拖放事件（对标 QDropEvent 家族）                 */
/* ========================================================================== */
XCLASS_DEFINE_BEGING(XDropEvent)
XCLASS_DEFINE_EXTEND_END(XDropEvent, XEvent)

/**
 * @brief 拖放事件负载。
 * @details type 只能为 DRAG_ENTER/DRAG_MOVE/DRAG_LEAVE/DROP；MIME 类型和
 *          数据均为 UTF-8 字符串，适配 `text/uri-list`、`text/plain` 等
 *          桌面互操作格式。字符串由事件拥有。
 */
typedef struct XDropEvent
{
    XEvent   m_class;
    XPoint   m_position;
    XPoint   m_globalPosition;
    XString* m_mimeType;
    XString* m_data;
} XDropEvent;

XVtable* XDropEvent_class_init(void);
XDropEvent* XDropEvent_create_ex(XMemoryType memory, XEventType type,
                                 const XPoint* position,
                                 const XPoint* globalPosition,
                                 const XString* mimeType,
                                 const XString* data);
#define XDropEvent_create(type, position, globalPosition, mimeType, data) \
    XDropEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (type), (position), \
        (globalPosition), (mimeType), (data))
void XDropEvent_init(XDropEvent* event, XEventType type,
                     const XPoint* position, const XPoint* globalPosition,
                     const XString* mimeType, const XString* data);
XPoint XDropEvent_position(const XDropEvent* event);
XPoint XDropEvent_globalPosition(const XDropEvent* event);
XString* XDropEvent_mimeType(const XDropEvent* event);
XString* XDropEvent_data(const XDropEvent* event);
#define XDropEvent_delete_base XEvent_delete_base
#define XDropEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XResizeEvent 调整大小事件（对标 QResizeEvent）               */
/* ========================================================================== */
/** @brief 声明 XResizeEvent 虚函数枚举：继承 XEvent（新增 Clone）。 */
XCLASS_DEFINE_BEGING(XResizeEvent)
XCLASS_DEFINE_EXTEND_END(XResizeEvent, XEvent)

/** @brief 调整大小事件对象；m_class 必须为第一个成员。 */
typedef struct XResizeEvent
{
    XEvent m_class;       /**< 继承 XEvent；必须为第一个成员。 */
    XSize  m_size;        /**< 事件产生后的新尺寸。 */
    XSize  m_oldSize;     /**< 事件产生前的旧尺寸。 */
    XSize  m_normalSize;  /**< 去除窗口边框后的正常尺寸；默认等于新尺寸。 */
    XSize  m_normalOldSize; /**< 去除窗口边框后的旧正常尺寸；默认等于旧尺寸。 */
} XResizeEvent;

/**
 * @brief      创建调整大小事件（对标 QResizeEvent(const QSize&, const QSize&)）。
 * @param      memory  内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type    事件类型；通常为 XEVENT_TYPE_RESIZE。
 * @param      size    新尺寸；可为 NULL（此时按 0x0）。
 * @param      oldSize 旧尺寸；可为 NULL（此时按 0x0）。
 * @return     新事件对象；内存分配失败返回 NULL。
 * @note       返回对象由调用者拥有；投递成功后由事件队列取得所有权。
 */
XResizeEvent* XResizeEvent_create_ex(XMemoryType memory, XEventType type,
                                     const XSize* size, const XSize* oldSize);
/**
 * @brief      初始化调用者提供的调整大小事件存储。
 * @param      event   待初始化存储；不可为 NULL。
 * @param      type    事件类型；通常为 XEVENT_TYPE_RESIZE。
 * @param      size    新尺寸；可为 NULL。
 * @param      oldSize 旧尺寸；可为 NULL。
 */
void XResizeEvent_init(XResizeEvent* event, XEventType type,
                       const XSize* size, const XSize* oldSize);
/** @brief 获取新尺寸（对标 QResizeEvent::size）；event 为 NULL 时返回 0x0。 */
XSize XResizeEvent_size(const XResizeEvent* event);
/** @brief 获取旧尺寸（对标 QResizeEvent::oldSize）；event 为 NULL 时返回 0x0。 */
XSize XResizeEvent_oldSize(const XResizeEvent* event);
/** @brief 获取去除边框后的正常新尺寸（对标 QResizeEvent::normalSize）。 */
XSize XResizeEvent_normalSize(const XResizeEvent* event);
/** @brief 获取去除边框后的正常旧尺寸（对标 QResizeEvent::normalOldSize）。 */
XSize XResizeEvent_normalOldSize(const XResizeEvent* event);
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XResizeEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XResizeEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XExposeEvent 暴露事件（对标 QExposeEvent）                   */
/* ========================================================================== */
/** @brief 声明 XExposeEvent 虚函数枚举：继承 XEvent（新增 Clone/Deinit）。 */
XCLASS_DEFINE_BEGING(XExposeEvent)
XCLASS_DEFINE_EXTEND_END(XExposeEvent, XEvent)

/** @brief 暴露事件对象；m_class 必须为第一个成员。 */
typedef struct XExposeEvent
{
    XEvent  m_class;  /**< 继承 XEvent；必须为第一个成员。 */
    XRegion m_region; /**< 需要重绘的暴露区域（内部深拷贝，事件拥有）。 */
} XExposeEvent;

/**
 * @brief      创建暴露事件（对标 QExposeEvent(const QRegion&)）。
 * @param      memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type   事件类型；通常为 XEVENT_TYPE_EXPOSE。
 * @param      region 暴露区域；可为 NULL（表示空区域）。内部深拷贝。
 * @return     新事件对象；内存分配失败返回 NULL。
 * @note       返回对象由调用者拥有；投递成功后由事件队列取得所有权。
 */
XExposeEvent* XExposeEvent_create_ex(XMemoryType memory, XEventType type,
                                     const XRegion* region);
/**
 * @brief      初始化调用者提供的暴露事件存储。
 * @param      event  待初始化存储；不可为 NULL。
 * @param      type   事件类型；通常为 XEVENT_TYPE_EXPOSE。
 * @param      region 暴露区域；可为 NULL。内部深拷贝。
 */
void XExposeEvent_init(XExposeEvent* event, XEventType type,
                       const XRegion* region);
/**
 * @brief      获取暴露区域（对标 QExposeEvent::region）。
 * @param      event 目标事件。
 * @return     区域的深拷贝；调用方负责 XRegion_deinit。
 * @note       与 Qt 语义一致：返回副本，事件内部区域不受调用方修改影响。
 */
XRegion XExposeEvent_region(const XExposeEvent* event);
/** @brief 释放方式沿用 XEvent；事件内部 XRegion 由 deinit 虚槽释放。 */
#define XExposeEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent；事件内部 XRegion 由 deinit 虚槽释放。 */
#define XExposeEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XPaintEvent 绘制事件（对标 QPaintEvent）                     */
/* ========================================================================== */
/** @brief 声明 XPaintEvent 虚函数枚举：继承 XEvent（新增 Clone/Deinit）。 */
XCLASS_DEFINE_BEGING(XPaintEvent)
XCLASS_DEFINE_EXTEND_END(XPaintEvent, XEvent)

/** @brief 绘制事件对象；m_class 必须为第一个成员。 */
typedef struct XPaintEvent
{
    XEvent  m_class;  /**< 继承 XEvent；必须为第一个成员。 */
    XRegion m_region; /**< 需要重绘的绘制区域（内部深拷贝，事件拥有）。 */
    XRect   m_rect;   /**< 绘制区域的外接矩形；init 时由区域边界计算。 */
} XPaintEvent;

/**
 * @brief      创建绘制事件（对标 QPaintEvent(const QRegion&)）。
 * @param      memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type   事件类型；通常为 XEVENT_TYPE_PAINT。
 * @param      region 绘制区域；可为 NULL（表示空区域）。内部深拷贝。
 * @return     新事件对象；内存分配失败返回 NULL。
 * @note       返回对象由调用者拥有；投递成功后由事件队列取得所有权。
 */
XPaintEvent* XPaintEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XRegion* region);
/**
 * @brief      初始化调用者提供的绘制事件存储。
 * @param      event  待初始化存储；不可为 NULL。
 * @param      type   事件类型；通常为 XEVENT_TYPE_PAINT。
 * @param      region 绘制区域；可为 NULL。内部深拷贝。
 */
void XPaintEvent_init(XPaintEvent* event, XEventType type, const XRegion* region);
/**
 * @brief      获取绘制区域（对标 QPaintEvent::region）。
 * @param      event 目标事件。
 * @return     区域的深拷贝；调用方负责 XRegion_deinit。
 */
XRegion XPaintEvent_region(const XPaintEvent* event);
/** @brief 获取绘制区域的外接矩形（对标 QPaintEvent::rect）。 */
XRect XPaintEvent_rect(const XPaintEvent* event);
/** @brief 释放方式沿用 XEvent；事件内部 XRegion 由 deinit 虚槽释放。 */
#define XPaintEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent；事件内部 XRegion 由 deinit 虚槽释放。 */
#define XPaintEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XCloseEvent 关闭事件（对标 QCloseEvent）                     */
/* ========================================================================== */
/** @brief 声明 XCloseEvent 虚函数枚举：继承 XEvent（新增 Clone）。 */
XCLASS_DEFINE_BEGING(XCloseEvent)
XCLASS_DEFINE_EXTEND_END(XCloseEvent, XEvent)

/** @brief 关闭事件对象；m_class 必须为第一个成员。 */
typedef struct XCloseEvent
{
    XEvent m_class; /**< 继承 XEvent；必须为第一个成员。 */
} XCloseEvent;

/**
 * @brief      创建关闭事件（对标 QCloseEvent()）。
 * @param      memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type   事件类型；通常为 XEVENT_TYPE_CLOSE。
 * @return     新事件对象；内存分配失败返回 NULL。
 * @note       返回对象由调用者拥有；投递成功后由事件队列取得所有权。
 */
XCloseEvent* XCloseEvent_create_ex(XMemoryType memory, XEventType type);
/** @brief 初始化调用者提供的关闭事件存储。 @param event 待初始化存储。 @param type 事件类型；通常为 XEVENT_TYPE_CLOSE。 */
void XCloseEvent_init(XCloseEvent* event, XEventType type);
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XCloseEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XCloseEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XShowEvent 显示事件（对标 QShowEvent）                       */
/* ========================================================================== */
/** @brief 声明 XShowEvent 虚函数枚举：继承 XEvent（新增 Clone）。 */
XCLASS_DEFINE_BEGING(XShowEvent)
XCLASS_DEFINE_EXTEND_END(XShowEvent, XEvent)

/** @brief 显示事件对象；m_class 必须为第一个成员。 */
typedef struct XShowEvent
{
    XEvent m_class; /**< 继承 XEvent；必须为第一个成员。 */
} XShowEvent;

/**
 * @brief      创建显示事件（对标 QShowEvent()）。
 * @param      memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type   事件类型；通常为 XEVENT_TYPE_SHOW。
 * @return     新事件对象；内存分配失败返回 NULL。
 */
XShowEvent* XShowEvent_create_ex(XMemoryType memory, XEventType type);
/** @brief 初始化调用者提供的显示事件存储。 @param event 待初始化存储。 @param type 事件类型；通常为 XEVENT_TYPE_SHOW。 */
void XShowEvent_init(XShowEvent* event, XEventType type);
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XShowEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XShowEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XHideEvent 隐藏事件（对标 QHideEvent）                       */
/* ========================================================================== */
/** @brief 声明 XHideEvent 虚函数枚举：继承 XEvent（新增 Clone）。 */
XCLASS_DEFINE_BEGING(XHideEvent)
XCLASS_DEFINE_EXTEND_END(XHideEvent, XEvent)

/** @brief 隐藏事件对象；m_class 必须为第一个成员。 */
typedef struct XHideEvent
{
    XEvent m_class; /**< 继承 XEvent；必须为第一个成员。 */
} XHideEvent;

/**
 * @brief      创建隐藏事件（对标 QHideEvent()）。
 * @param      memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type   事件类型；通常为 XEVENT_TYPE_HIDE。
 * @return     新事件对象；内存分配失败返回 NULL。
 */
XHideEvent* XHideEvent_create_ex(XMemoryType memory, XEventType type);
/** @brief 初始化调用者提供的隐藏事件存储。 @param event 待初始化存储。 @param type 事件类型；通常为 XEVENT_TYPE_HIDE。 */
void XHideEvent_init(XHideEvent* event, XEventType type);
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XHideEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XHideEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XFocusEvent 焦点事件（对标 QFocusEvent）                     */
/* ========================================================================== */
/** @brief 声明 XFocusEvent 虚函数枚举：继承 XEvent（新增 Clone）。 */
XCLASS_DEFINE_BEGING(XFocusEvent)
XCLASS_DEFINE_EXTEND_END(XFocusEvent, XEvent)

/** @brief 焦点事件对象；m_class 必须为第一个成员。 */
typedef struct XFocusEvent
{
    XEvent      m_class;  /**< 继承 XEvent；必须为第一个成员。 */
    XFocusReason m_reason; /**< 焦点变化原因。 */
} XFocusEvent;

/**
 * @brief      创建焦点事件（对标 QFocusEvent(Type, Qt::FocusReason)）。
 * @param      memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type   事件类型；必须为 XEVENT_TYPE_FOCUS_IN 或 XEVENT_TYPE_FOCUS_OUT。
 * @param      reason 焦点变化原因。
 * @return     新事件对象；内存分配失败返回 NULL。
 */
XFocusEvent* XFocusEvent_create_ex(XMemoryType memory, XEventType type,
                                   XFocusReason reason);
/** @brief 初始化调用者提供的焦点事件存储。 @param event 待初始化存储。 @param type 焦点进出类型。 @param reason 焦点变化原因。 */
void XFocusEvent_init(XFocusEvent* event, XEventType type, XFocusReason reason);
/** @brief 是否获得焦点（对标 QFocusEvent::gotFocus）。 */
bool XFocusEvent_gotFocus(const XFocusEvent* event);
/** @brief 是否失去焦点（对标 QFocusEvent::lostFocus）。 */
bool XFocusEvent_lostFocus(const XFocusEvent* event);
/** @brief 获取焦点变化原因（对标 QFocusEvent::reason）。 */
XFocusReason XFocusEvent_reason(const XFocusEvent* event);
/** @brief 设置焦点变化原因（对标 QFocusEvent::setReason）。 */
void XFocusEvent_setReason(XFocusEvent* event, XFocusReason reason);
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XFocusEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XFocusEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XWheelEvent 滚轮事件（对标 QWheelEvent）                     */
/* ========================================================================== */
/** @brief 声明 XWheelEvent 虚函数枚举：继承 XEvent（新增 Copy/Clone）。 */
XCLASS_DEFINE_BEGING(XWheelEvent)
XCLASS_DEFINE_EXTEND_END(XWheelEvent, XEvent)

/** @brief 滚轮事件对象；m_class 必须为第一个成员。 */
typedef struct XWheelEvent
{
    XEvent m_class;               /**< 继承 XEvent；必须为第一个成员。 */
    XPoint m_position;            /**< 事件源对象局部坐标（对标 QWheelEvent::position）。 */
    XPoint m_globalPosition;      /**< 屏幕全局坐标（对标 QWheelEvent::globalPosition）。 */
    XPoint m_angleDelta;          /**< 滚动角度增量；垂直滚轮 ±120/格，水平滚轮在 x 轴（对标 QWheelEvent::angleDelta）。 */
    XMouseButton m_buttons;       /**< 事件发生时按下的鼠标按键位掩码。 */
    XKeyboardModifiers m_modifiers; /**< 事件发生时按下的键盘修饰键。 */
} XWheelEvent;

/**
 * @brief      创建滚轮事件（对标 QWheelEvent(pos, globalPos, angleDelta,
 *             buttons, modifiers)）。
 * @details    Qt 6.8 约定：angleDelta 以 1/8 度为基本单位，普通刻度滚轮
 *             每次滚动 ±120；垂直滚动在 y 轴、水平滚动在 x 轴，向上/向右
 *             为正。像素增量（pixelDelta）在非平滑滚轮上由角度增量换算，
 *             本简化实现不单独承载（可用角度增量推导）。
 * @param      memory        内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type          事件类型；必须为 XEVENT_TYPE_WHEEL。
 * @param      position      局部坐标；可为 NULL（按 0,0）。
 * @param      globalPosition 屏幕全局坐标；可为 NULL（按 0,0）。
 * @param      angleDelta    角度增量；可为 NULL（按 0,0）。
 * @param      buttons       事件发生时按下的鼠标按键位掩码。
 * @param      modifiers     事件发生时按下的键盘修饰键。
 * @return     新事件对象；内存分配失败返回 NULL。
 */
XWheelEvent* XWheelEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XPoint* position,
                                   const XPoint* globalPosition,
                                   const XPoint* angleDelta,
                                   XMouseButton buttons,
                                   XKeyboardModifiers modifiers);
/** @brief 初始化调用者提供的滚轮事件存储（参数语义同 create_ex）。 */
void XWheelEvent_init(XWheelEvent* event, XEventType type,
                      const XPoint* position, const XPoint* globalPosition,
                      const XPoint* angleDelta, XMouseButton buttons,
                      XKeyboardModifiers modifiers);
/** @brief 获取局部坐标（对标 QWheelEvent::position）。 */
XPoint XWheelEvent_position(const XWheelEvent* event);
/** @brief 获取屏幕全局坐标（对标 QWheelEvent::globalPosition）。 */
XPoint XWheelEvent_globalPosition(const XWheelEvent* event);
/** @brief 获取角度增量（对标 QWheelEvent::angleDelta）。 */
XPoint XWheelEvent_angleDelta(const XWheelEvent* event);
/** @brief 获取事件发生时按下的鼠标按键（对标 QWheelEvent::buttons）。 */
XMouseButton XWheelEvent_buttons(const XWheelEvent* event);
/** @brief 获取键盘修饰键（对标 QWheelEvent::modifiers）。 */
XKeyboardModifiers XWheelEvent_modifiers(const XWheelEvent* event);
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XWheelEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XWheelEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*                XEnterEvent 指针进入事件（对标 QEnterEvent）                 */
/* ========================================================================== */
/** @brief 声明 XEnterEvent 虚函数枚举：继承 XEvent（新增 Copy/Clone）。 */
XCLASS_DEFINE_BEGING(XEnterEvent)
XCLASS_DEFINE_EXTEND_END(XEnterEvent, XEvent)

/** @brief 指针进入事件对象；m_class 必须为第一个成员。
 *         指针离开（XEVENT_TYPE_LEAVE）无负载，直接投递普通 XEvent。 */
typedef struct XEnterEvent
{
    XEvent m_class;          /**< 继承 XEvent；必须为第一个成员。 */
    XPoint m_position;       /**< 事件源对象局部坐标（对标 QEnterEvent::position）。 */
    XPoint m_globalPosition; /**< 屏幕全局坐标（对标 QEnterEvent::globalPosition）。 */
} XEnterEvent;

/**
 * @brief      创建指针进入事件（对标 QEnterEvent(pos, globalPos)）。
 * @param      memory        内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type          事件类型；必须为 XEVENT_TYPE_ENTER。
 * @param      position      局部坐标；可为 NULL（按 0,0）。
 * @param      globalPosition 屏幕全局坐标；可为 NULL（按 0,0）。
 * @return     新事件对象；内存分配失败返回 NULL。
 */
XEnterEvent* XEnterEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XPoint* position,
                                   const XPoint* globalPosition);
/** @brief 初始化调用者提供的进入事件存储（参数语义同 create_ex）。 */
void XEnterEvent_init(XEnterEvent* event, XEventType type,
                      const XPoint* position, const XPoint* globalPosition);
/** @brief 获取局部坐标（对标 QEnterEvent::position）。 */
XPoint XEnterEvent_position(const XEnterEvent* event);
/** @brief 获取屏幕全局坐标（对标 QEnterEvent::globalPosition）。 */
XPoint XEnterEvent_globalPosition(const XEnterEvent* event);
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XEnterEvent_delete_base XEvent_delete_base
/** @brief 释放方式沿用 XEvent（无动态成员）。 */
#define XEnterEvent_deinit_base XEvent_deinit_base

/* ========================================================================== */
/*      XClass create API default-memory wrappers（默认内存池快速创建）。      */
/* ========================================================================== */
#undef XResizeEvent_create
#define XResizeEvent_create(...) XResizeEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XExposeEvent_create
#define XExposeEvent_create(...) XExposeEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XPaintEvent_create
#define XPaintEvent_create(...) XPaintEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XCloseEvent_create
#define XCloseEvent_create(...) XCloseEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XShowEvent_create
#define XShowEvent_create(...) XShowEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XHideEvent_create
#define XHideEvent_create(...) XHideEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XFocusEvent_create
#define XFocusEvent_create(...) XFocusEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XWheelEvent_create
#define XWheelEvent_create(...) XWheelEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XEnterEvent_create
#define XEnterEvent_create(...) XEnterEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

#endif /* XWINDOWEVENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWINDOWEVENT_H */
