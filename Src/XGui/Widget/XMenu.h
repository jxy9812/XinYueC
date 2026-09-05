/**
 * @file       XMenu.h
 * @brief      XMenu 弹出菜单公开 API（对标 Qt 6.8 QMenu）。
 * @details    XMenu 继承 XWidget，实现 QMenu 的核心语义：
 *             - 动作容器：addAction/addMenu/addSeparator/clear/isEmpty、
 *               actions()、actionAt()、menuAction()；
 *             - 属性：title/setTitle、defaultAction/setDefaultAction、
 *               activeAction/setActiveAction、separatorsCollapsible、
 *               toolTipsVisible、tearOffEnabled；
 *             - 弹出：popup(pos)/exec()（作为顶层窗口显示，点击条目触发
 *               动作后关闭）；
 *             - 信号：aboutToShow、aboutToHide、triggered(XAction*)、
 *               hovered(XAction*)。
 *             动作由菜单拥有，菜单释放时级联释放；通过 XAction_setMenu
 *             关联子菜单，父菜单释放时级联释放子菜单 Widget。
 * @note       模块总开关 XMENU_ON 定义于 XGuiConfig.h；关闭时裁剪本头
 *             文件全部公共声明。对象字段由 XClass 生命周期管理，调用方
 *             应在 GUI 线程访问。
 * @note       按项目约束，本类只依赖 XWidget 与 XAction；QMenu 的快捷键
 *             （QKeySequence）、tear-off 平台菜单、滚动与样式表不在本类
 *             提供，作为裁剪边界在 XGui.md 记录。
 * @author     XinYueC 团队
 */
#ifndef XMENU_H
#define XMENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "XGuiConfig.h"
#include "XWidget.h"
#include "XAction.h"
#include "XString.h"
#include "XGeometry.h"
#include "XPainter.h"

#if XWIDGET_ON && XMENU_ON

/* ==================== 类虚函数表 ==================== */

/**
 * @brief      XMenu 类虚函数表。
 * @details    XMenu 继承 XWidget 的全部事件槽位，并重载 XClass 的
 *             Copy/Move/Deinit 以管理动作与子菜单资源。
 */
XCLASS_DEFINE_BEGING(XMenu)
XCLASS_DEFINE_EXTEND_END(XMenu, XWidget)

/* ==================== 菜单对象（对标 Qt 6.8 QMenu） ==================== */

/**
 * @brief      XMenu 弹出菜单对象。
 * @details    m_base 是第一个成员，因此对象可向上转换为 XWidget/XObject；
 *             m_actions 为对象拥有的 XAction 列表（含分隔条与子菜单
 *             menuAction），m_menuAction 是代表本菜单自身、用于嵌入父
 *             菜单的动作，m_parentMenu 为父菜单借用指针。所有成员均属于
 *             实现状态，调用方不得直接修改。
 */
typedef struct XMenu
{
    XWidget      m_base;                  /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XString*     m_title;                 /**< 菜单标题（对标 QMenu::title）；对象拥有。 */
    XVector*     m_actions;               /**< 动作列表（XAction*，对标 QMenu::actions）；对象拥有。 */
    XAction*     m_menuAction;            /**< 代表本菜单的动作（对标 QMenu::menuAction）；懒创建。 */
    XMenu*       m_parentMenu;            /**< 父菜单借用指针；NULL 表示顶层菜单。 */
    XAction*     m_defaultAction;         /**< 默认动作（对标 QMenu::defaultAction）；借用指针。 */
    XAction*     m_activeAction;          /**< 当前高亮动作（对标 QMenu::activeAction）；借用指针。 */
    bool         m_popupActive;           /**< 弹出窗口是否打开；仅供内部使用。 */
    XAction*     m_execResult;            /**< exec() 返回的被选动作；仅供内部使用。 */
    XTimerId     m_grabTimer;             /**< 弹出后延迟执行平台鼠标抓取的定时器；仅供内部使用。 */
    bool         m_separatorsCollapsible; /**< 相邻分隔条是否合并（对标 QMenu::separatorsCollapsible）。 */
    bool         m_toolTipsVisible;       /**< 是否显示动作工具提示（对标 QMenu::toolTipsVisible）。 */
    bool         m_tearOffEnabled;        /**< 是否允许撕离（对标 QMenu::tearOffEnabled）；仅存储位。 */
    int          m_actionHeight;          /**< 每个动作条目的渲染高度；仅供内部使用。 */
} XMenu;

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化并返回 XMenu 类的共享虚函数表。
 * @return     类共享的 XVtable 指针；虚表创建或注册失败时返回 NULL。
 */
XVtable* XMenu_class_init(void);

/**
 * @brief      默认初始化嵌入式 XMenu 对象（对标 QMenu 默认构造）。
 * @param      self 待初始化的可写对象存储；不可为 NULL，且必须尚未初始化。
 * @return     无返回值；self 不满足初始化前提时调用方不得继续使用对象。
 */
void XMenu_init(XMenu* self, XWidget* parent);

/**
 * @brief      带标题的初始化重载（对标 QMenu(title, parent) 构造）。
 * @param      self 待初始化的可写对象存储；不可为 NULL，且必须尚未初始化。
 * @param      parent 父控件借用指针；可为 NULL，函数不取得其所有权。
 * @param      utf8Title 标题；按 UTF-8 解码，可为 NULL 表示空标题。
 * @return     无返回值。
 */
void XMenu_init_2(XMenu* self, XWidget* parent, const char* utf8Title);

/**
 * @brief      使用默认内存类型创建并初始化菜单对象。
 * @return     新建的已初始化对象指针；分配失败返回 NULL。成功返回的
 *             对象由调用方拥有，必须使用 XMenu_delete_base 释放。
 */
XMenu* XMenu_create(void);

/**
 * @brief      使用指定内存类型创建并初始化菜单对象（对标 QMenu 构造）。
 * @param      memory 对象分配所使用的 XMemoryType；只影响对象分配与释放。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      utf8Title 标题；按 UTF-8 解码，可为 NULL 表示空标题。
 * @return     新建的已初始化对象指针；分配失败返回 NULL。
 */
XMenu* XMenu_create_ex(XMemoryType memory, XWidget* parent,
                       const char* utf8Title);

/**
 * @brief      拷贝创建菜单对象。
 * @param      other 源菜单借用指针；不可为 NULL。
 * @return     新建的已初始化对象指针；分配失败或 other 为 NULL 时返回
 *             NULL。只复制标题与动作数据，不复制信号连接。
 */
XMenu* XMenu_create_copy(const XMenu* other);

/**
 * @brief      移动创建菜单对象（资源所有权转移）。
 * @param      other 源菜单借用指针；移动后其字段复位，仍由创建者释放。
 * @return     新建的已初始化对象指针；分配失败或 other 为 NULL 时返回
 *             NULL。
 */
XMenu* XMenu_create_move(XMenu* other);

/**
 * @brief      通过当前 XClass 虚表释放菜单对象所拥有的资源。
 * @param      self 已初始化的栈对象或外部存储对象；可为 NULL。
 * @return     无返回值；堆对象必须使用 XMenu_delete_base。
 */
#define XMenu_deinit_base(self)  XClass_deinit_base((XClass*)(self))

/**
 * @brief      释放菜单对象资源并按对象所有权删除其存储空间。
 * @param      self 由 XMenu_create 系列返回的堆对象；可为 NULL。
 * @return     无返回值；栈对象请使用 XMenu_deinit_base。
 */
#define XMenu_delete_base(self)  XClass_delete_base((XClass*)(self))

/* ==================== 动作容器（对标 QMenu） ==================== */

/**
 * @brief      向菜单追加一个文本动作（对标 QMenu::addAction(text)）。
 * @details    创建 XAction 并设置文本后加入菜单动作列表；动作由菜单
 *             拥有，菜单释放时级联释放。动作触发时菜单会转发
 *             triggered(action) 信号。
 * @param      self 目标菜单对象；可为 NULL，NULL 时不执行操作。
 * @param      text 源文本借用指针；可为 NULL 表示空文本，函数深拷贝。
 * @return     新建的 XAction 指针（由菜单拥有，调用方不得释放）；分配
 *             失败或 self 为 NULL 时返回 NULL。
 */
XAction* XMenu_addAction(XMenu* self, const XString* text);

/**
 * @brief      使用 UTF-8 字符串追加文本动作（UTF-8 兼容重载）。
 * @param      self 目标菜单对象；可为 NULL，NULL 时不执行操作。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL 表示空文本。
 * @return     新建的 XAction 指针（由菜单拥有，调用方不得释放）；失败
 *             时返回 NULL。
 */
XAction* XMenu_addAction_2(XMenu* self, const char* utf8);

/**
 * @brief      把已有菜单作为子菜单加入（对标 QMenu::addMenu(QMenu*)）。
 * @details    为本菜单创建代表子菜单的 menuAction 并关联 menu，把
 *             menu 的父菜单设为本菜单、登记为子控件（父菜单释放时级联
 *             释放子菜单）。menu 可为 NULL，NULL 时仅创建分隔动作占位。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      menu 子菜单借用指针；可为 NULL。
 * @return     成功加入返回 true；self 为 NULL 或动作创建失败返回 false。
 */
bool XMenu_addMenu(XMenu* self, XMenu* menu);

/**
 * @brief      创建并加入一个带标题的子菜单（对标 QMenu::addMenu(title)）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      utf8Title 子菜单标题；按 UTF-8 解码，可为 NULL。
 * @return     新建的子菜单指针（调用方不得释放，由本菜单级联管理）；
 *             失败返回 NULL。
 */
XMenu* XMenu_addMenu_2(XMenu* self, const char* utf8Title);

/**
 * @brief      追加一个分隔条（对标 QMenu::addSeparator）。
 * @param      self 目标菜单对象；可为 NULL。
 * @return     新建的分隔动作指针（由菜单拥有）；失败返回 NULL。
 */
XAction* XMenu_addSeparator(XMenu* self);

/**
 * @brief      清空菜单全部动作与子菜单（对标 QMenu::clear）。
 * @param      self 目标菜单对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XMenu_clear(XMenu* self);

/**
 * @brief      查询菜单是否为空（对标 QMenu::isEmpty）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     actions 列表为空返回 true；self 为 NULL 时返回 true。
 */
bool XMenu_isEmpty(const XMenu* self);

/**
 * @brief      获取菜单动作列表（对标 QMenu::actions）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     动作列表借用指针（XAction* 元素）；self 为 NULL 时返回
 *             NULL。返回指针归菜单所有，调用方不得释放。
 */
const XVector* XMenu_actions(const XMenu* self);

/**
 * @brief      返回局部坐标处的动作（对标 QMenu::actionAt）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @param      pos 局部坐标借用指针；可为 NULL。
 * @return     命中条目返回对应 XAction*（分隔条也返回）；未命中或参数
 *             无效返回 NULL。
 */
XAction* XMenu_actionAt(const XMenu* self, const XPoint* pos);

/**
 * @brief      返回代表本菜单的动作（对标 QMenu::menuAction）。
 * @details    首次调用时创建并缓存；该动作可嵌入父菜单作为子菜单入口。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     菜单自身动作借用指针（由菜单拥有）；失败返回 NULL。
 */
XAction* XMenu_menuAction(XMenu* self);

/* ==================== 属性（对标 QMenu） ==================== */

/**
 * @brief      获取菜单标题拷贝（对标 QMenu::title）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；未设置或 self 为 NULL 时返回 NULL。
 */
XString* XMenu_title(const XMenu* self);

/**
 * @brief      获取菜单标题借用指针。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     内部标题借用指针；未设置或 self 为 NULL 时返回 NULL，不得
 *             释放或修改。
 */
const XString* XMenu_title_const(const XMenu* self);

/**
 * @brief      设置菜单标题（对标 QMenu::setTitle）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      title 源文本借用指针；可为 NULL 表示空标题。
 * @return     无返回值；内容真正变化时重绘并更新菜单自身动作文本。
 */
void XMenu_setTitle(XMenu* self, const XString* title);

/**
 * @brief      使用 UTF-8 字符串设置菜单标题（UTF-8 兼容重载）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL。
 * @return     无返回值。
 */
void XMenu_setTitle_2(XMenu* self, const char* utf8);

/**
 * @brief      查询默认动作（对标 QMenu::defaultAction）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     默认动作借用指针；未设置或 self 为 NULL 时返回 NULL。
 */
XAction* XMenu_defaultAction(const XMenu* self);

/**
 * @brief      设置默认动作（对标 QMenu::setDefaultAction）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      action 目标动作借用指针；可为 NULL 表示清除。
 * @return     无返回值。
 */
void XMenu_setDefaultAction(XMenu* self, XAction* action);

/**
 * @brief      查询当前高亮动作（对标 QMenu::activeAction）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     高亮动作借用指针；未设置或 self 为 NULL 时返回 NULL。
 */
XAction* XMenu_activeAction(const XMenu* self);

/**
 * @brief      设置当前高亮动作（对标 QMenu::setActiveAction）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      action 目标动作借用指针；可为 NULL 表示清除。
 * @return     无返回值；高亮变化时重绘。
 */
void XMenu_setActiveAction(XMenu* self, XAction* action);

/**
 * @brief      查询相邻分隔条是否合并（对标 QMenu::separatorsCollapsible）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     合并返回 true；self 为 NULL 时返回 false。
 */
bool XMenu_separatorsCollapsible(const XMenu* self);

/**
 * @brief      设置相邻分隔条是否合并（对标 QMenu::setSeparatorsCollapsible）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      collapse true 合并，false 不合并。
 * @return     无返回值；值变化时重绘。
 */
void XMenu_setSeparatorsCollapsible(XMenu* self, bool collapse);

/**
 * @brief      查询是否显示动作工具提示（对标 QMenu::toolTipsVisible）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     显示返回 true；self 为 NULL 时返回 false。
 */
bool XMenu_toolTipsVisible(const XMenu* self);

/**
 * @brief      设置是否显示动作工具提示（对标 QMenu::setToolTipsVisible）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      visible true 显示，false 隐藏。
 * @return     无返回值。
 */
void XMenu_setToolTipsVisible(XMenu* self, bool visible);

/**
 * @brief      查询是否允许撕离（对标 QMenu::tearOffEnabled）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     允许返回 true；self 为 NULL 时返回 false。
 */
bool XMenu_tearOffEnabled(const XMenu* self);

/**
 * @brief      设置是否允许撕离（对标 QMenu::setTearOffEnabled）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      enable true 允许，false 禁止。
 * @return     无返回值；仅存储位，不实现平台撕离窗口。
 */
void XMenu_setTearOffEnabled(XMenu* self, bool enable);

/* ==================== 弹出（对标 QMenu） ==================== */

/**
 * @brief      在屏幕坐标位置弹出菜单（对标 QMenu::popup）。
 * @details    把菜单作为顶层窗口显示在指定位置并置顶；菜单打开时发射
 *             aboutToShow，关闭时发射 aboutToHide。点击条目触发对应
 *             动作并关闭，键盘 Up/Down 移动高亮、Enter 触发、Escape
 *             关闭。
 * @note       不在 show() 后立即设置输入焦点：X11 下对未完成映射的窗口
 *             调用 XSetInputFocus 会触发 BadMatch，焦点交由平台在用户与
 *             菜单窗口交互时自然授予；有窗口系统环境下键盘导航依赖该
 *             焦点授予，无窗口环境不受影响。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      pos 全局屏幕坐标借用指针；可为 NULL（默认使用当前鼠标
 *             位置）。
 * @return     无返回值。
 */
void XMenu_popup(XMenu* self, const XPoint* pos);

/**
 * @brief      阻塞执行菜单并返回被选中的动作（对标 QMenu::exec）。
 * @details    调用 popup 后进入阻塞事件循环，直到动作被选择或菜单关闭；
 *             Escape/外部关闭返回 NULL。必须在事件循环环境调用，否则
 *             会阻塞。
 * @param      self 目标菜单对象；可为 NULL。
 * @return     被选中的 XAction 借用指针；关闭未选择返回 NULL。
 */
XAction* XMenu_exec(XMenu* self);

/* ==================== 尺寸（对标 QMenu::sizeHint） ==================== */

/**
 * @brief      计算菜单建议尺寸。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     建议尺寸；self 为 NULL 时返回 (0, 0)。
 */
XSize XMenu_sizeHint(const XMenu* self);

/* ==================== 离屏绘制 ==================== */

/**
 * @brief      绘制菜单内容到指定绘制器（离屏渲染/测试用）。
 * @details    与 paintEvent 使用同一绘制体：背景、动作文本（高亮/禁用
 *             着色）、分隔条与子菜单箭头。调用方负责 begin/end 与坐标
 *             平移。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @param      painter 目标绘制器借用指针；可为 NULL。
 * @return     无返回值。
 */
void XMenu_drawContents(XMenu* self, XPainter* painter);

/* ==================== 信号（对标 QMenu signals） ==================== */

/**
 * @brief      发射 aboutToShow 信号（对标 QMenu::aboutToShow）。
 * @param      self 发射信号的菜单对象；可为 NULL。
 * @return     不透明的 aboutToShow 信号标识。
 */
void* XMenu_aboutToShow_signal(XMenu* self);

/**
 * @brief      发射 aboutToHide 信号（对标 QMenu::aboutToHide）。
 * @param      self 发射信号的菜单对象；可为 NULL。
 * @return     不透明的 aboutToHide 信号标识。
 */
void* XMenu_aboutToHide_signal(XMenu* self);

/**
 * @brief      发射 triggered(XAction*) 信号（对标 QMenu::triggered）。
 * @details    菜单中任一动作被选择时发射。
 * @param      self 发射信号的菜单对象；可为 NULL。
 * @param      action 被选择的动作借用指针。
 * @return     不透明的 triggered 信号标识。
 */
void* XMenu_triggered_signal(XMenu* self, XAction* action);

/**
 * @brief      发射 hovered(XAction*) 信号（对标 QMenu::hovered）。
 * @details    高亮动作变化时发射。
 * @param      self 发射信号的菜单对象；可为 NULL。
 * @param      action 新高亮动作借用指针。
 * @return     不透明的 hovered 信号标识。
 */
void* XMenu_hovered_signal(XMenu* self, XAction* action);

#endif /* XWIDGET_ON && XMENU_ON */

#ifdef __cplusplus
}
#endif

#endif /* XMENU_H */
