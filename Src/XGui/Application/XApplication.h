/******************************************************************************
 * @file       XApplication.h
 * @brief      XApplication 控件级应用类（对标 Qt 6.8 QApplication）。
 * @details    XApplication 继承 XGuiApplication，是控件级（Widgets）应用外壳：
 *             一次性创建唯一应用实例并登记控件注册表（topLevelWidgets /
 *             widgetAt）；控件级焦点与活动窗口管理（activeWindow / focusWidget
 *             / setActiveWindow / activeModalWidget / activePopupWidget）；
 *             主事件循环（exec / quit，委托 XCoreApplication）；QApplication
 *             遗留的全局交互参数（双击间隔、滚轮行数、拖拽启动时间与距离、
 *             光标闪烁时间、键盘输入间隔，全部委托 XStyleHints）。
 *             与 Qt 6.8 一致：XApplication 不重复实现 GUI 运行时，窗口/屏幕/
 *             剪贴板等仍由 XGuiApplication 管理；本类仅在顶层增加控件语义，
 *             且不依赖任何平台 API。
 * @note       模块总开关 XAPPLICATION_ON 定义于 XGuiConfig.h；依赖
 *             XGUIAPPLICATION_ON——关闭时整体裁剪。XApplication 持有
 *             XWidget 借用指针，仅前向声明类型，避免与 XWidget.h 循环包含；
 *             XWidget.c 内部再包含本头文件完成注册表联动。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XAPPLICATION_H
#define XAPPLICATION_H
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
#include "XVector.h"
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */

/* ==================== 依赖类型前向声明（避免循环包含） ==================== */
/** @brief XWidget 控件前向声明；XApplication 只持借用指针，实体 API 由
 *  XWidget.h 提供。 */
typedef struct XWidget XWidget;

#if XGUIAPPLICATION_ON && !XSTYLEHINTS_ON
/** @brief XStyleHints 前向声明回退（开关关闭时交互参数接口返回默认值）。 */
typedef struct XStyleHints XStyleHints;
#endif /* XGUIAPPLICATION_ON && !XSTYLEHINTS_ON */

#if XAPPLICATION_ON && XGUIAPPLICATION_ON

/* ==================== 虚函数表（继承 XGuiApplication，无新增槽位） ==================== */

/** @brief 声明 XApplication 虚函数枚举：继承 XGuiApplication 全部槽位。
 * @details QApplication 相对 QGuiApplication 没有新增公开虚函数事件槽；
 *         控件注册/焦点管理均为普通注册表操作，因此本类不追加槽位。 */
XCLASS_DEFINE_BEGING(XApplication)
XCLASS_DEFINE_EXTEND_END(XApplication, XGuiApplication)

/** @brief XApplication 应用对象；m_class 必须是第一个成员（嵌 XGuiApplication）。
 * @details 字段含义：
 *          - m_topLevelWidgets：登记的全部顶层控件（XWidget* 借用指针）；
 *          - m_activeWindow：当前活动窗口控件（借用，可为 NULL）；
 *          - m_focusWidget：当前焦点控件（借用，可为 NULL）；
 *          - m_activeModalWidget：当前模态控件（借用，可为 NULL）；
 *          - m_activePopupWidget：当前弹出控件（借用，可为 NULL）。
 *          调用方不得手工修改任何字段。 */
typedef struct XApplication
{
    XGuiApplication m_class;         /**< 基类成员（必须是第一个）。 */
    XVector* m_topLevelWidgets;      /**< 顶层控件注册表（拥有）。 */
    XWidget* m_activeWindow;         /**< 活动窗口控件（借用）。 */
    XWidget* m_focusWidget;          /**< 焦点控件（借用）。 */
    XWidget* m_activeModalWidget;    /**< 模态控件（借用）。 */
    XWidget* m_activePopupWidget;    /**< 弹出控件（借用）。 */
} XApplication;

/**
 * @brief      初始化 XApplication 类虚函数表并返回共享表指针。
 * @return     XApplication 类的共享 XVtable 指针。
 */
XVtable* XApplication_class_init(void);

/**
 * @brief      初始化 XApplication（内置 XGuiApplication 实例，成为唯一应用）。
 * @details    内部调用 XGuiApplication_init 初始化基类；XCoreApplication
 *             实例指针即指向本对象，后续 XGuiApplication_instance() 可见。
 *             初始化后本对象作为进程唯一应用直至生命周期结束。
 * @param      self 待初始化对象；生命周期结束时必须成对调用
 *             XApplication_deinit_base。
 * @param      argc 命令行参数个数（可为 0）。
 * @param      argv 命令行参数数组（可为 NULL）。
 */
void XApplication_init(XApplication* self, int argc, char** argv);

/** @brief 使用默认内存类型创建唯一 XApplication；已存在其它应用时返回 NULL。 */
#define XApplication_create() XApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型创建唯一 XApplication（对标 QApplication 构造）。
 * @details    Qt 语义：只能有一个应用实例。若 XCoreApplication 已由
 *             XGuiApplication_create_ex 创建，本函数返回 NULL（调用方应改用
 *             XGuiApplication 或重新设计启动流程）；若本函数已调用过，返回
 *             原实例指针。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败或已存在其它应用返回 NULL，成功用
 *             XApplication_deinit_base 释放。
 */
XApplication* XApplication_create_ex(XMemoryType memory, int argc, char** argv);

/** @brief 通过 XClass 虚表释放 XApplication 资源（栈/外部存储对象使用）。 */
#define XApplication_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XApplication 对象。 */
#define XApplication_delete_base(self) XClass_delete_base((XClass*)(self))
/** @brief 深拷贝 XApplication 资源。 */
#define XApplication_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动 XApplication 资源。 */
#define XApplication_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))

/**
 * @brief      查询唯一 XApplication 实例（对标 QApplication::instance）。
 * @details    仅当应用以 XApplication_create_ex 创建时返回本类型指针；
 *             纯 XGuiApplication 应用返回 NULL。
 * @return     应用实例借用指针；不存在返回 NULL。
 */
XApplication* XApplication_instance(void);

/* ==================== 事件循环（直接复用父类 API） ==================== */

#define XApplication_exec XGuiApplication_exec
#define XApplication_quit XGuiApplication_quit

/* ==================== 控件注册表（对标 QApplication::widgetAt / topLevelWidgets） ==================== */

/**
 * @brief      返回位于全局坐标点的顶层控件（对标 QApplication::widgetAt）。
 * @details    按登记逆序查找包含该点的可见顶层控件；再退化为顶层窗口查询。
 * @param      point 全局坐标点。
 * @return     顶层控件借用指针；无匹配返回 NULL。
 */
XWidget* XApplication_widgetAt(const XPoint* point);

/**
 * @brief      返回全部顶层控件（对标 QApplication::topLevelWidgets）。
 * @return     新建的 XVector（元素为 XWidget* 借用指针），调用方释放；
 *             无实例返回 NULL。
 */
XVector* XApplication_topLevelWidgets(void);

/**
 * @brief      平台/测试接入钩子：登记顶层控件（对标 QApplication 内部登记）。
 * @details    幂等：同一控件重复登记为 no-op。控件销毁前调用方必须调用
 *             XApplication_unregisterTopLevelWidget，否则出现悬挂借用指针。
 * @param      widget 目标控件；可为 NULL。
 */
void XApplication_registerTopLevelWidget(XWidget* widget);

/**
 * @brief      平台/测试接入钩子：注销顶层控件。
 * @details    同时清空 activeWindow/focusWidget 中指向该控件的引用。
 * @param      widget 目标控件；可为 NULL。
 */
void XApplication_unregisterTopLevelWidget(XWidget* widget);

/* ==================== 活动窗口与焦点（对标 QApplication::activeWindow 等） ==================== */

/**
 * @brief      返回当前活动窗口控件（对标 QApplication::activeWindow）。
 * @return     活动窗口控件借用指针；无返回 NULL。
 */
XWidget* XApplication_activeWindow(void);

/**
 * @brief      设置活动窗口控件（对标 QApplication::setActiveWindow）。
 * @details    仅接受已登记的顶层控件；登记表外控件被忽略。传入 NULL 清空。
 * @param      widget 目标控件；可为 NULL。
 */
void XApplication_setActiveWindow(XWidget* widget);

/**
 * @brief      返回当前焦点控件（对标 QApplication::focusWidget）。
 * @return     焦点控件借用指针；无返回 NULL。
 */
XWidget* XApplication_focusWidget(void);

/**
 * @brief      XWidget 内部钩子：更新焦点控件（对标 QApplication 内部 focusChanged）。
 * @param      widget 新焦点控件；可为 NULL。
 */
void XApplication_setFocusWidget(XWidget* widget);

/**
 * @brief      返回当前模态控件（对标 QApplication::activeModalWidget）。
 * @return     模态控件借用指针；无返回 NULL。
 */
XWidget* XApplication_activeModalWidget(void);

/**
 * @brief      XWidget 内部钩子：登记模态控件。
 * @param      widget 目标控件；可为 NULL。
 */
void XApplication_setActiveModalWidget(XWidget* widget);

/**
 * @brief      返回当前弹出控件（对标 QApplication::activePopupWidget）。
 * @return     弹出控件借用指针；无返回 NULL。
 */
XWidget* XApplication_activePopupWidget(void);

/**
 * @brief      XWidget 内部钩子：登记弹出控件。
 * @param      widget 目标控件；可为 NULL。
 */
void XApplication_setActivePopupWidget(XWidget* widget);

/* ==================== 全局交互参数（对标 QApplication 遗留静态接口） ==================== */

/** @brief 查询/设置鼠标双击间隔（毫秒；对标 QApplication::doubleClickInterval）。 */
int  XApplication_doubleClickInterval(void);
void XApplication_setDoubleClickInterval(int ms);
/** @brief 查询/设置滚轮滚动行数（对标 QApplication::wheelScrollLines）。 */
int  XApplication_wheelScrollLines(void);
void XApplication_setWheelScrollLines(int lines);
/** @brief 查询/设置拖拽启动时间（毫秒；对标 QApplication::startDragTime）。 */
int  XApplication_startDragTime(void);
void XApplication_setStartDragTime(int ms);
/** @brief 查询/设置拖拽启动距离（像素；对标 QApplication::startDragDistance）。 */
int  XApplication_startDragDistance(void);
void XApplication_setStartDragDistance(int px);
/** @brief 查询/设置光标闪烁时间（毫秒；对标 QApplication::cursorFlashTime）。 */
int  XApplication_cursorFlashTime(void);
void XApplication_setCursorFlashTime(int ms);
/** @brief 查询/设置键盘输入间隔（毫秒；对标 QApplication::keyboardInputInterval）。 */
int  XApplication_keyboardInputInterval(void);
void XApplication_setKeyboardInputInterval(int ms);

#endif /* XAPPLICATION_ON && XGUIAPPLICATION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XAPPLICATION_H */
