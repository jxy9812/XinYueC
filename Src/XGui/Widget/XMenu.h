/**
 * @file       XMenu.h
 * @brief      XMenu 弹出菜单公开 API（对标 Qt 6.8 QMenu）。
 * @details    XMenu 继承 XWidget，实现 QMenu 的核心语义：
 *             - 动作容器：addAction/addMenu/addSeparator/clear/isEmpty、
 *               actions()、actionAt()、menuAction()、menuInAction()；
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

#if XGUI_ON

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
    /* ---- 指针/句柄组（8 字节对齐，置顶） ---- */
    XString*     m_title;                 /**< 菜单标题（对标 QMenu::title）；对象拥有。 */
    XString*     m_icon;                  /**< 菜单图标路径（对标 QMenu::icon）；对象拥有；可为 NULL；P3-18 起为 menuAction 图标的镜像（事实源在 m_menuAction，getter 以其为准）。 */
    XVector*     m_actions;               /**< 动作列表（XAction*，对标 QMenu::actions）；对象拥有。 */
    XVector*     m_actionOwned;           /**< 逐项所有权标记（bool，与 m_actions
                                           对齐；true=addAction/addMenu 等
                                           便捷路径自建（菜单拥有，析构/
                                           clear 级联删除），false=
                                           insertAction 注入的外部动作
                                           （仅借用，不取所有权，对标
                                           QWidget::addAction））。 */
    XAction*     m_menuAction;            /**< 代表本菜单的动作（对标 QMenu::menuAction）；懒创建。 */
    XMenu*       m_parentMenu;            /**< 父菜单借用指针；NULL 表示顶层菜单。 */
    XAction*     m_defaultAction;         /**< 默认动作（对标 QMenu::defaultAction）；借用指针。 */
    XAction*     m_activeAction;          /**< 当前高亮动作（对标 QMenu::activeAction）；借用指针。 */
    XAction*     m_execResult;            /**< exec() 返回的被选动作；仅供内部使用。 */
    XWidget*     m_noReplayFor;           /**< 不重放点击的目标控件（对标 setNoReplayFor）；借用。 */
    void*        m_platformMenu;          /**< 平台菜单句柄（对标 setPlatformMenu）；不透明。 */
    XTimerId     m_grabTimer;             /**< 弹出后延迟执行平台鼠标抓取的定时器；仅供内部使用。 */
    /* ---- 2 字节组 ---- */
    int16_t      m_actionHeight;          /**< 每个动作条目的渲染高度（像素，
                                               初值 20）；仅供内部使用。 */
    /* ---- 位域组（5 个布尔打包 1 字节） ---- */
    uint8_t      m_popupActive : 1;       /**< 弹出窗口是否打开；仅供内部使用。 */
    uint8_t      m_separatorsCollapsible : 1; /**< 相邻分隔条是否合并（对标 QMenu::separatorsCollapsible）。 */
    uint8_t      m_toolTipsVisible : 1;   /**< 是否显示动作工具提示（对标 QMenu::toolTipsVisible）。 */
    uint8_t      m_tearOffEnabled : 1;    /**< 是否允许撕离（对标 QMenu::tearOffEnabled）；仅存储位。 */
    uint8_t      m_tearOffMenuVisible : 1;/**< 撕离菜单是否可见（对标 isTearOffMenuVisible）。 */
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
 *             对象由调用方拥有，必须使用 XClassDelete 释放。
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

/* P2-13/14：XMenu_create_copy / XMenu_create_move 公开包装已删
 * （Qt QObject 禁止拷贝/移动语义不对外）；XClass 泛型拷贝/移动仍经
 * VXMenu_copy / VXMenu_move 虚槽（EXClass_Copy / EXClass_Move）承载，
 * 槽内子菜单深拷贝就地 create + XClassCopy。 */

/**
 * @brief      通过当前 XClass 虚表释放菜单对象所拥有的资源。
 * @param      self 已初始化的栈对象或外部存储对象；可为 NULL。
 * @return     无返回值；堆对象必须使用 XClassDelete。
 */

/**
 * @brief      释放菜单对象资源并按对象所有权删除其存储空间。
 * @param      self 由 XMenu_create 系列返回的堆对象；可为 NULL。
 * @return     无返回值；栈对象请使用 XClassDeinit。
 */

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
 * @details    直接复用子菜单**自己的** menuAction（`XMenu_menuAction(menu)`，
 *             对标 qmenu.cpp:1866-1873 的 `addAction(menu->menuAction())`），
 *             并把 menu 的父菜单设为本菜单、登记为子控件（父菜单释放时
 *             级联释放子菜单）。menu 可为 NULL，NULL 时仅创建分隔动作占位。
 *             menuAction 归子菜单所有，本菜单仅借用登记：子菜单
 *             `setTitle` 会同步该动作文本（QMenu::setTitle 写 menuAction），
 *             故父菜单条目文本随子菜单标题联动。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      menu 子菜单借用指针；可为 NULL。
 * @return     子菜单的 menuAction 借用指针（归子菜单所有）；self 为 NULL
 *             或创建失败返回 NULL。
 */
XAction* XMenu_addMenu(XMenu* self, XMenu* menu);

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
 * @brief      追加一个节标题动作（对标 QMenu::addSection(text)）。
 * @details    Qt 6.8 的节标题 = **带文本的分隔条**且保持 enabled
 *             （qmenu.cpp:1936-1942 `new QAction(text,this);
 *             action->setSeparator(true); addAction(action);`；本机探针
 *             实测 isSeparator=true/isEnabled=true/visible=true）。节标题
 *             文本由样式决定是否绘制（本库 Fusion 路径绘制文本 + 分隔线，
 *             见 XMenu.c 的静态助手 xmenu_drawSeparator——P2-16 已降
 *             static，公开面无此名）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      text 节标题借用指针；可为 NULL 表示空文本（退化为普通分隔条）。
 * @return     新建的节动作指针（由菜单拥有）；失败返回 NULL。
 */
XAction* XMenu_addSection(XMenu* self, const XString* text);
/**
 * @brief      使用 UTF-8 字符串追加节标题（UTF-8 兼容重载）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL。
 * @return     新建的节动作指针；失败返回 NULL。
 */
XAction* XMenu_addSection_2(XMenu* self, const char* utf8);
/**
 * @brief      加入已有动作（对标 QMenu 经 `using QWidget::addAction`
 *             继承的 addAction(QAction*)，qmenu.h:42 + qwidget.h:559-563）。
 * @details    等价 insertAction(NULL, action)：追加到末尾。动作仅为
 *             借用——菜单不取得所有权（Qt 同），析构/clear 不删除该
 *             动作；动作被外部释放时经 destroyed 自摘出列表。这是把
 *             XToolBar/XActionGroup 现成动作复用进菜单的标准途径。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      action 已有动作借用指针；不能为 NULL。
 * @return     无返回值。
 */
void XMenu_addActionAction(XMenu* self, XAction* action);
/**
 * @brief      在 before 动作之前插入已有动作（对标 QMenu::insertAction，
 *             即 QWidget::insertAction，qwidget.h:559-563）。
 * @details    before 为 NULL 或不在菜单内时等价追加（Qt 同 pos<0 分支）；
 *             动作已在菜单内时先摘除再插入（移动语义，对标 Qt 的
 *             contains+removeAction 口径）。借用语义：菜单不取得所有
 *             权，removeAction 摘除后所有权仍归调用方。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      before 参考动作借用指针；可为 NULL。
 * @param      action 已有动作借用指针；不能为 NULL。
 * @return     无返回值。
 */
void XMenu_insertAction(XMenu* self, XAction* before, XAction* action);
/**
 * @brief      从菜单摘除动作但不释放（对标 QMenu::removeAction，即
 *             QWidget::removeAction）。
 * @details    仅把动作移出菜单列表并清理触发转发挂接；不删除对象，
 *             所有权归还调用方。动作不在菜单内时为无操作。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      action 目标动作借用指针；可为 NULL。
 * @return     无返回值。
 */
void XMenu_removeAction(XMenu* self, XAction* action);
/**
 * @brief      在 before 动作之前插入子菜单（对标 QMenu::insertMenu）。
 * @details    before 为 NULL 时等价追加；直接复用 menu->menuAction()
 *             （qmenu.cpp:1975-1981），动作归子菜单所有。before 为 NULL
 *             或不在菜单内时等价追加。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      before 参考动作借用指针；可为 NULL。
 * @param      menu 子菜单借用指针；可为 NULL（创建分隔占位）。
 * @return     子菜单的 menuAction 借用指针（归子菜单所有）；失败返回 NULL。
 */
XAction* XMenu_insertMenu(XMenu* self, XAction* before, XMenu* menu);
/**
 * @brief      在 before 动作之前插入分隔条（对标 QMenu::insertSeparator）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      before 参考动作借用指针；可为 NULL（等价追加）。
 * @return     新建的分隔动作指针；失败返回 NULL。
 */
XAction* XMenu_insertSeparator(XMenu* self, XAction* before);
/**
 * @brief      在 before 动作之前插入节标题（对标 QMenu::insertSection）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      before 参考动作借用指针；可为 NULL（等价追加）。
 * @param      text 节标题借用指针；可为 NULL。
 * @return     新建的节动作指针；失败返回 NULL。
 */
XAction* XMenu_insertSection(XMenu* self, XAction* before,
                             const XString* text);
/**
 * @brief      使用 UTF-8 字符串在 before 前插入节标题（UTF-8 兼容重载）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      before 参考动作借用指针；可为 NULL。
 * @param      utf8 节标题（UTF-8）；可为 NULL。
 * @return     新建的节动作指针；失败返回 NULL。
 */
XAction* XMenu_insertSection_2(XMenu* self, XAction* before,
                               const char* utf8);
/**
 * @brief      返回动作在菜单局部坐标中的几何（对标 QMenu::actionGeometry）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      action 目标动作借用指针；可为 NULL。
 * @return     命中返回动作矩形；未命中或参数无效返回空矩形 (0,0,0,0)。
 */
XRect XMenu_actionGeometry(const XMenu* self, XAction* action);
/**
 * @brief      设置不重放点击的目标控件（对标 QMenu::setNoReplayFor）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      widget 目标控件借用指针；可为 NULL 表示清除。
 * @return     无返回值。
 */
void XMenu_setNoReplayFor(XMenu* self, XWidget* widget);
/**
 * @brief      设置平台菜单句柄（对标 QMenu::setPlatformMenu）。
 * @note       本项目的平台菜单句柄为不透明存储，不参与渲染。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      platformMenu 平台菜单句柄；可为 NULL 表示清除。
 * @return     无返回值。
 */
void XMenu_setPlatformMenu(XMenu* self, void* platformMenu);
/**
 * @brief      查询平台菜单句柄（对标 QMenu::platformMenu）。
 * @param      self 目标菜单对象；可为 NULL。
 * @return     不透明句柄；未设置返回 NULL。
 */
void* XMenu_platformMenu(const XMenu* self);
/**
 * @brief      设置为 Dock 菜单（对标 QMenu::setAsDockMenu；macOS 专用）。
 * @note       本项目为跨平台裁剪：无 macOS Dock 概念，函数仅记录。
 * @param      self 目标菜单对象；可为 NULL。
 * @return     无返回值。
 */
void XMenu_setAsDockMenu(XMenu* self);
/**
 * @brief      显示撕离菜单（对标 QMenu::showTearOffMenu）。
 * @note       本项目无平台撕离窗口；仅记录可见位并重绘。
 * @param      self 目标菜单对象；可为 NULL。
 * @return     无返回值。
 */
void XMenu_showTearOffMenu(XMenu* self);
/**
 * @brief      隐藏撕离菜单（对标 QMenu::hideTearOffMenu）。
 * @param      self 目标菜单对象；可为 NULL。
 * @return     无返回值。
 */
void XMenu_hideTearOffMenu(XMenu* self);
/**
 * @brief      查询撕离菜单是否可见（对标 QMenu::isTearOffMenuVisible）。
 * @param      self 目标菜单对象；可为 NULL。
 * @return     可见返回 true。
 */
bool XMenu_isTearOffMenuVisible(const XMenu* self);

/**
 * @brief      清空菜单全部动作与子菜单（对标 QMenu::clear）。
 * @param      self 目标菜单对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XMenu_clear(XMenu* self);

/**
 * @brief      查询菜单是否为空（对标 QMenu::isEmpty）。
 * @details    Qt 口径（qmenu.cpp:2199-2211）：当且仅当动作表里**没有
 *             可见且非分隔条**的动作时为 true——只有分隔条/节标题、
 *             或动作全部被 setVisible(false) 隐藏时同样为 true；不是
 *             「动作表为空」的等价物。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     无可见非分隔条动作返回 true；self 为 NULL 时返回 true。
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
 * @details    首次调用时创建并缓存；创建时即 `XAction_setMenu(action,
 *             self)` 回指本菜单（对标 QMenuPrivate 的
 *             `menuAction->setMenu(q)`，qmenu.cpp:165-166）。父容器
 *             （XMenu::addMenu / XMenuBar::addMenu）直接复用该动作，
 *             setTitle 会同步其文本。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     菜单自身动作借用指针（由菜单拥有）；失败返回 NULL。
 */
XAction* XMenu_menuAction(XMenu* self);

/**
 * @brief      返回动作承载的子菜单（对标 QMenu::menuInAction 静态接口）。
 * @details    Qt 中为 qobject_cast<QMenu*>(action->menuObject())：动作
 *             的关联对象是菜单时返回该菜单自身，否则返回 NULL。本项目
 *             XAction 的菜单承载位（XAction_menu）只存放 XMenu*，不存在
 *             第二种对象类型，因此省去 qobject_cast 类型鉴别，直接返回
 *             动作当前关联的菜单（"菜单自身或 NULL"两种结果）。
 * @note       简化差异：无运行时类型鉴别，返回值即动作登记的 XMenu；
 *             菜单所有权归其父菜单或创建方，调用方不得释放。
 * @param      action 目标动作借用指针；可为 NULL。
 * @return     动作关联的子菜单借用指针；未关联或 action 为 NULL 返回
 *             NULL。
 */
XMenu* XMenu_menuInAction(const XAction* action);

/* ==================== 属性（对标 QMenu） ==================== */

/**
 * @brief      获取菜单标题拷贝（对标 QMenu::title）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XClassDelete；未设置或 self 为 NULL 时返回 NULL。
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
 * @brief      设置菜单图标（对标 QMenu::setIcon；XString 主版本）。
 * @details    P3-18：图标写入本菜单的 menuAction（Qt 即
 *             `menuAction->setIcon`，qmenu.cpp:1112-1115）——父容器
 *             （菜单栏/父菜单条目）经该动作取图标；菜单侧 m_icon 只作
 *             镜像。图标以路径字符串承载。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      icon 图标路径借用指针；NULL 清除图标。
 * @return     无返回值。
 */
void XMenu_setIcon(XMenu* self, const XString* icon);
/**
 * @brief      设置菜单图标（UTF-8 路径兼容重载，转发 XString 主版本）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      utf8 图标路径（UTF-8）；NULL 清除图标。
 * @return     无返回值。
 */
void XMenu_setIcon_2(XMenu* self, const char* utf8);
/**
 * @brief      查询菜单图标（对标 QMenu::icon）。
 * @details    P3-19：以 menuAction 的图标为准（qmenu.cpp:1107-1110
 *             `return d_func()->menuAction->icon();`），与 m_icon 镜像
 *             同步（setIcon 成对写入）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     借用 menuAction 的路径 XString 指针；未设置、menuAction
 *             未创建或 self 为 NULL 时返回 NULL；禁止释放或修改。
 */
const XString* XMenu_icon(const XMenu* self);

/**
 * @brief      查询默认动作（对标 QMenu::defaultAction）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     默认动作借用指针；未设置或 self 为 NULL 时返回 NULL。
 */
XAction* XMenu_defaultAction(const XMenu* self);

/**
 * @brief      设置默认动作（对标 QMenu::setDefaultAction）。
 * @details    P3-16：缺省动作有视觉线索——条目按 DefaultItem 类型用
 *             粗体绘制（样式层 CE_MenuItem 消费 m_menuItemType；自绘
 *             回退分支直接 XFont_setBold）。
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
 * @details    缺省 true。开启时布局**真的折叠**（对标
 *             QMenuPrivate::updateActionRects，qmenu.cpp:392-403）：
 *             开头的分隔条不占行、相邻纯分隔条只保留第一条、尾部分隔条
 *             不占行；被折叠条目在 actionGeometry 中返回空矩形且不参与
 *             actionAt 命中。带文本的节标题（addSection）不参与折叠
 *             （Qt Fusion 的 SH_Menu_SupportsSections=1）。
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
 * @details    P3-20：置真后菜单条目高亮变化时按动作 toolTip 弹出提示
 *             （XToolTip_showText_2 锚条目矩形）；移出条目/关闭菜单即
 *             收起。口径差异：Qt 走 QEvent::ToolTip，X 平台无该事件，
 *             改 hover 驱动（台账 P3-20 定案）。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      visible true 显示，false 隐藏。
 * @return     无返回值。
 */
void XMenu_setToolTipsVisible(XMenu* self, bool visible);

/**
 * @brief      查询是否允许撕离（对标 QMenu::isTearOffEnabled 属性访问器）。
 * @details    P1-27：本体即 Qt 名 isTearOffEnabled；旧名 tearOffEnabled
 *             降为过渡宏（见下）。
 * @param      self 菜单对象借用指针；可为 NULL。
 * @return     允许返回 true；self 为 NULL 时返回 false。
 */
bool XMenu_isTearOffEnabled(const XMenu* self);

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
 *             关闭。等价 `XMenu_popup_2(self, pos, NULL)`（Qt 的
 *             `popup(pos, at = nullptr)`）。
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
 * @brief      带锚定动作的弹出重载（对标
 *             `QMenu::popup(const QPoint&, QAction *at)`，qmenu.h:93）。
 * @details    at 不是「预高亮项」而是**定位锚**：菜单上移 at 之前各
 *             布局行的高度，使 at 的条目正好落在 pos 处（对标
 *             QMenuPrivate::popup 的 atAction 分支，qmenu.cpp:2426-2450；
 *             本机 Qt 6.8 探针：popup((100,100), 第二项) 后菜单 y=75、
 *             第二行 y=25、activeAction 不变）。at 为 NULL 或不在菜单内
 *             时等价 XMenu_popup。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      pos 全局屏幕坐标借用指针；可为 NULL。
 * @param      at 定位锚动作借用指针；可为 NULL。
 * @return     无返回值。
 */
void XMenu_popup_2(XMenu* self, const XPoint* pos, XAction* at);

/**
 * @brief      阻塞执行菜单并返回被选中的动作（对标 QMenu::exec）。
 * @details    调用 popup 后进入阻塞事件循环，直到动作被选择或菜单关闭；
 *             Escape/外部关闭返回 NULL。必须在事件循环环境调用，否则
 *             会阻塞。
 * @param      self 目标菜单对象；可为 NULL。
 * @return     被选中的 XAction 借用指针；关闭未选择返回 NULL。
 */
XAction* XMenu_exec(XMenu* self);

/**
 * @brief      带位置与锚定动作的阻塞执行重载（对标 `QMenu::exec(const
 *             QPoint&, QAction *at)`，qmenu.h:146）。
 * @details    形状对齐 XMenu_popup_2：pos 为全局屏幕坐标，at 是**定位锚**
 *             （语义同 XMenu_popup_2，为 NULL 或不在菜单内时按原位弹出）。
 *             阻塞行为与 XMenu_exec 相同——进入嵌套事件循环直至动作被
 *             选择或菜单关闭；DeleteOnClose 暂存口径也一致（exec 期间
 *             不自删，退出时按原状态兑现）。P0-15（XToolButton.showMenu
 *             改走阻塞 exec）以本接口承载「按钮正下方阻塞弹出」。
 * @param      self 目标菜单对象；可为 NULL。
 * @param      pos 全局屏幕坐标借用指针；可为 NULL（等价 XMenu_exec）。
 * @param      at 定位锚动作借用指针；可为 NULL。
 * @return     被选中的 XAction 借用指针；关闭未选择返回 NULL。
 */
XAction* XMenu_exec_2(XMenu* self, const XPoint* pos, XAction* at);

/* ==================== 尺寸（对标 QMenu::sizeHint） ==================== */

/* sizeHint/minimumSizeHint 是 XWidget 虚函数（对标 QMenu::sizeHint
 * 覆写；minimumSizeHint 回落基类默认）：业务体在 XMenu.c 的 static
 * VXMenu_sizeHint/minimumSizeHint 槽内并经 class_init 注册虚表，对外
 * 统一走基类 `*_base` 入口虚分派，本类不再另设无后缀公开函数
 * （《api命名风格》`*_base` 契约）。 */
#define XMenu_sizeHint_base(self) \
    XWidget_sizeHint_base((const XWidget*)(self))
#define XMenu_minimumSizeHint_base(self) \
    XWidget_minimumSizeHint_base((const XWidget*)(self))

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

#endif /* XGUI_ON */

#endif /* XMENU_H */
