/**
 * @file       XAction.h
 * @brief      XAction 动作类公开 API（对标 Qt 6.8 QAction）。
 * @details    XAction 继承 XObject，实现 QAction 的核心语义：
 *             - 文本族：text、iconText、toolTip、statusTip、whatsThis；
 *             - 状态属性：checkable/checked、enabled（含 resetEnabled 与
 *               visible 联动）、visible、separator、priority、menuRole、
 *               iconVisibleInMenu、shortcutVisibleInContextMenu；
 *             - 用户数据：data/setData（XVariant* 所有权转移）；
 *             - 激活行为：trigger/activate(Trigger|Hover)/hover；
 *             - 信号：changed、enabledChanged(bool)、checkableChanged(bool)、
 *               visibleChanged()、triggered(bool)、hovered()、toggled(bool)。
 *             本类不提供任何函数指针回调字段；「测试菜单动作绑定并运行
 *             测试函数」由 Test/XTestMenu 层的 XTestMenu_setActionFunction
 *             通过 triggered 信号完成。
 * @note       模块总开关 XACTION_ON 定义于 CXinYueConfig.h；关闭时裁剪
 *             本头文件全部公共声明。对象字段由 XClass 生命周期管理，不
 *             保证线程安全，调用方应在所属事件线程访问。
 * @note       按项目约束，XCode 层不依赖 XGui：本类通过前向声明的
 *             XMenu* 提供 menu/setMenu 关联（对标 QAction::menu/setMenu，
 *             菜单对象由 XGui 层创建）；QAction 的 icon、font、
 *             shortcut(s)、actionGroup/associatedObjects 与 showStatusText
 *             不在本类提供，作为裁剪边界在 XGui.md 记录。
 * @author     XinYueC 团队
 */
#ifndef XACTION_H
#define XACTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "CXinYueConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XTypes.h"

#if XACTION_ON

/* ==================== 枚举（对标 Qt 6.8 QAction） ==================== */

/**
 * @brief      动作激活事件类型（对标 Qt 6.8 QAction::ActionEvent）。
 * @details    与 QAction::ActionEvent 取值一致；Trigger 触发动作，
 *             Hover 表示动作被高亮（悬停），二者不可按位组合。
 */
typedef enum XActionEvent
{
    XActionEvent_Trigger = 0,             /**< 触发动作（对标 QAction::Trigger）。 */
    XActionEvent_Hover   = 1              /**< 动作被悬停高亮（对标 QAction::Hover）。 */
} XActionEvent;

/**
 * @brief      菜单角色（对标 Qt 6.8 QAction::MenuRole，取值一致）。
 * @details    用于 macOS 应用菜单的角色分类；其余平台仅作为属性存储，
 *             不影响触发行为。取值与 QAction::MenuRole 逐项一致。
 */
typedef enum XActionMenuRole
{
    XActionMenuRole_NoRole = 0,           /**< 无角色（对标 QAction::NoRole）。 */
    XActionMenuRole_TextHeuristicRole = 1,/**< 按文本启发式分配（默认，对标 QAction::TextHeuristicRole）。 */
    XActionMenuRole_ApplicationSpecificRole = 2, /**< 应用特定角色（对标 QAction::ApplicationSpecificRole）。 */
    XActionMenuRole_AboutQtRole = 3,      /**< 关于 Qt 角色（对标 QAction::AboutQtRole）。 */
    XActionMenuRole_AboutRole = 4,        /**< 关于应用角色（对标 QAction::AboutRole）。 */
    XActionMenuRole_PreferencesRole = 5,  /**< 偏好设置角色（对标 QAction::PreferencesRole）。 */
    XActionMenuRole_QuitRole = 6          /**< 退出角色（对标 QAction::QuitRole）。 */
} XActionMenuRole;

/**
 * @brief      动作优先级（对标 Qt 6.8 QAction::Priority，取值一致）。
 * @details    用于工具栏溢出策略；本类仅作为属性存储并参与 changed 通知。
 */
typedef enum XActionPriority
{
    XActionPriority_Low = 0,              /**< 低优先级（对标 QAction::LowPriority）。 */
    XActionPriority_Normal = 128,         /**< 正常优先级（默认，对标 QAction::NormalPriority）。 */
    XActionPriority_High = 256            /**< 高优先级（对标 QAction::HighPriority）。 */
} XActionPriority;

/* ==================== 类虚函数表 ==================== */

/**
 * @brief      XAction 类虚函数表。
 * @details    XAction 不新增虚函数槽位，直接继承 XObject 的事件槽位，
 *             并重载 XClass 的 Copy/Move/Deinit 以管理文本/数据资源。
 */
XCLASS_DEFINE_BEGING(XAction)
XCLASS_DEFINE_EXTEND_END(XAction, XObject)

/* ==================== 动作对象（对标 Qt 6.8 QAction） ==================== */

/**
 * @brief      XAction 动作对象。
 * @details    m_base 是第一个成员，因此对象可向上转换为 XObject；文本
 *             族字段为对象拥有的 XString，m_data 为对象拥有的 XVariant
 *             （随 setData 所有权转移）。所有成员均属于实现状态，调用方
 *             不得直接修改，应通过本文件声明的 API 访问。
 */
typedef struct XAction
{
    XObject      m_base;                  /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XString*     m_text;                  /**< 主文本（对标 QAction::text）；对象拥有。 */
    XString*     m_iconText;              /**< 图标文本（对标 QAction::iconText）；对象拥有。 */
    XString*     m_toolTip;               /**< 工具提示（对标 QAction::toolTip）；对象拥有。 */
    XString*     m_statusTip;             /**< 状态栏提示（对标 QAction::statusTip）；对象拥有。 */
    XString*     m_whatsThis;             /**< What's This 帮助文本（对标 QAction::whatsThis）；对象拥有。 */
    XVariant*    m_data;                  /**< 用户数据（对标 QAction::data）；对象拥有，setData 转移所有权。 */
    XMenu*       m_menu;                  /**< 关联弹出菜单（对标 QAction::menu）；借用指针，不拥有。 */
    bool         m_checkable;             /**< 是否可选中（对标 QAction::checkable）。 */
    bool         m_checked;               /**< 原始选中位；仅 m_checkable 为 true 时对外有效。 */
    bool         m_enabled;               /**< 有效启用状态（对标 QAction::enabled 有效值）。 */
    bool         m_explicitEnabled;       /**< 是否经 setEnabled/setDisabled 显式设置。 */
    bool         m_explicitEnabledValue;  /**< 显式设置记录的目标启用值。 */
    bool         m_visible;               /**< 是否可见（对标 QAction::visible）。 */
    bool         m_separator;             /**< 是否为分隔条（对标 QAction::separator）。 */
    bool         m_iconVisibleInMenu;     /**< 菜单中是否显示图标（对标 QAction::iconVisibleInMenu）。 */
    bool         m_shortcutVisibleInContextMenu; /**< 上下文菜单中是否显示快捷键（对标 QAction::shortcutVisibleInContextMenu）。 */
    XActionPriority m_priority;           /**< 优先级（对标 QAction::priority）。 */
    XActionMenuRole  m_menuRole;          /**< 菜单角色（对标 QAction::menuRole）。 */
} XAction;

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化并返回 XAction 类的共享虚函数表。
 * @return     类共享的 XVtable 指针；虚表创建或注册失败时返回 NULL。
 * @note       返回指针具有静态生命周期，调用方不得释放、修改或保存为
 *             可写的派生虚表。
 */
XVtable* XAction_class_init(void);

/**
 * @brief      默认初始化嵌入式 XAction 对象（对标 QAction 默认构造）。
 * @details    初始化 XObject 基类并设置默认属性值：enabled/visible=true、
 *             priority=XActionPriority_Normal、
 *             menuRole=XActionMenuRole_TextHeuristicRole，文本族为空、
 *             checkable/checked/separator 等为 false、data 为 NULL。
 * @param      self 待初始化的可写对象存储；不可为 NULL，且必须尚未初始化。
 * @return     无返回值；self 不满足初始化前提时调用方不得继续使用对象。
 * @note       init 不分配 self；初始化后的栈/外部存储对象应使用
 *             XAction_deinit_base，堆对象应使用 XAction_delete_base。
 */
void XAction_init(XAction* self);

/**
 * @brief      带父对象与初始文本的初始化重载（对标 QAction 构造）。
 * @details    在默认初始化基础上建立与 parent 的父子关系（parent 释放
 *             时级联释放本堆对象，栈对象仅 deinit）并以 UTF-8 文本作为
 *             初始主文本。
 * @param      self 待初始化的可写对象存储；不可为 NULL，且必须尚未初始化。
 * @param      parent 父对象借用指针；可为 NULL，动作不取得其所有权。
 * @param      utf8Text 初始文本；按 UTF-8 解码，可为 NULL 表示空文本，
 *             函数复制内容，返回后不保留调用方缓冲。
 * @return     无返回值；self 不满足初始化前提时调用方不得继续使用对象。
 */
void XAction_init_2(XAction* self, XObject* parent, const char* utf8Text);

/**
 * @brief      使用默认内存类型创建并初始化动作对象（对标 QAction 默认
 *             构造）。
 * @return     新建的已初始化对象指针；分配失败返回 NULL。成功返回的
 *             对象由调用方拥有，必须使用 XAction_delete_base 释放。
 */
XAction* XAction_create(void);

/**
 * @brief      使用指定内存类型创建并初始化动作对象（对标 QAction 构造）。
 * @param      memory 对象分配所使用的 XMemoryType；只影响对象分配与释放。
 * @param      parent 父对象借用指针；可为 NULL，函数不取得其所有权。
 * @param      utf8Text 初始文本；按 UTF-8 解码，可为 NULL 表示空文本。
 * @return     新建的已初始化对象指针；分配或初始化失败返回 NULL。成功
 *             返回的堆对象由调用方拥有，必须使用 XAction_delete_base
 *             释放。
 */
XAction* XAction_create_ex(XMemoryType memory, XObject* parent,
                           const char* utf8Text);

/**
 * @brief      拷贝创建动作对象（对标 QAction 值语义拷贝）。
 * @param      other 源动作对象借用指针；不可为 NULL。
 * @return     新建的已初始化对象指针；分配失败或 other 为 NULL 时返回
 *             NULL。成功返回的堆对象由调用方拥有，必须使用
 *             XAction_delete_base 释放。
 * @note       只复制属性字段，不复制信号连接与父子关系（XObject 基类
 *             无拷贝语义）。
 */
XAction* XAction_create_copy(const XAction* other);

/**
 * @brief      移动创建动作对象（资源所有权转移）。
 * @param      other 源动作对象借用指针；不可为 NULL，移动后其属性字段
 *             复位为默认值，仍可由创建者按分配方式释放。
 * @return     新建的已初始化对象指针；分配失败或 other 为 NULL 时返回
 *             NULL。成功返回的堆对象由调用方拥有，必须使用
 *             XAction_delete_base 释放。
 */
XAction* XAction_create_move(XAction* other);

/**
 * @brief      通过当前 XClass 虚表释放动作对象所拥有的资源。
 * @param      self 已初始化的栈对象或外部存储对象；可为 NULL，NULL 时
 *             不执行操作。
 * @return     无返回值；函数不会释放 self 指向的存储空间，堆对象必须
 *             使用 XAction_delete_base。
 */
#define XAction_deinit_base(self)  XClass_deinit_base((XClass*)(self))

/**
 * @brief      释放动作对象资源并按对象所有权删除其存储空间。
 * @param      self 由 XAction_create 系列返回的堆对象；可为 NULL，NULL
 *             时不执行操作。
 * @return     无返回值；堆对象会先执行虚表析构，再由创建时的内存方法
 *             释放；栈对象不应使用此宏，栈对象请使用 XAction_deinit_base。
 */
#define XAction_delete_base(self)  XClass_delete_base((XClass*)(self))

/* 复制/移动统一使用全局 XCopy/XMove（经虚表分派到 VXAction_copy/
 * VXAction_move），本类不再单独声明 *_copy_base/*_move_base 宏。 */

/* ==================== 文本族属性（对标 QAction） ==================== */

/**
 * @brief      获取动作主文本的拷贝（对标 QAction::text）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；self 为 NULL 或未设置文本时返回 NULL。
 */
XString* XAction_text(const XAction* self);

/**
 * @brief      获取动作主文本的内部借用指针（对标 QAction::text 的只读
 *             访问）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     内部文本借用指针；self 为 NULL 或未设置文本时返回 NULL。
 *             返回指针不能释放、不能修改，生命周期与 self 及下一次文本
 *             修改前的对象状态相关。
 */
const XString* XAction_text_const(const XAction* self);

/**
 * @brief      设置动作主文本（对标 QAction::setText）。
 * @details    深拷贝 text，不取得调用方字符串所有权；内容真正变化时
 *             替换并发射 changed 信号。XString 内容按 UTF-16 代码单元
 *             处理。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      text 源文本借用指针；可为 NULL 表示空文本，函数返回后
 *             调用方仍可释放或修改源对象。
 * @return     无返回值；内存分配失败或 self 为 NULL 时保持原文本不变。
 */
void XAction_setText(XAction* self, const XString* text);

/**
 * @brief      使用 UTF-8 字符串设置动作主文本（UTF-8 兼容重载）。
 * @details    utf8 按 UTF-8 解码为 XString 后转发 XAction_setText；函数
 *             复制解码结果，返回后不保留调用方字符缓冲区。NULL 按空
 *             文本处理。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL 表示空文本。
 * @return     无返回值；解码或内存分配失败时保持原文本不变。
 */
void XAction_setText_2(XAction* self, const char* utf8);

/**
 * @brief      获取图标文本的拷贝（对标 QAction::iconText）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；self 为 NULL 或未设置时返回 NULL。
 */
XString* XAction_iconText(const XAction* self);

/**
 * @brief      获取图标文本的内部借用指针。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     内部文本借用指针；未设置或 self 为 NULL 时返回 NULL，不得
 *             释放或修改。
 */
const XString* XAction_iconText_const(const XAction* self);

/**
 * @brief      设置图标文本（对标 QAction::setIconText）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      text 源文本借用指针；可为 NULL 表示空文本。
 * @return     无返回值；内容真正变化时发射 changed 信号。
 */
void XAction_setIconText(XAction* self, const XString* text);

/**
 * @brief      使用 UTF-8 字符串设置图标文本（UTF-8 兼容重载）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL 表示空文本。
 * @return     无返回值；内容真正变化时发射 changed 信号。
 */
void XAction_setIconText_2(XAction* self, const char* utf8);

/**
 * @brief      获取工具提示的拷贝（对标 QAction::toolTip）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；self 为 NULL 或未设置时返回 NULL。
 */
XString* XAction_toolTip(const XAction* self);

/**
 * @brief      获取工具提示的内部借用指针。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     内部文本借用指针；未设置或 self 为 NULL 时返回 NULL，不得
 *             释放或修改。
 */
const XString* XAction_toolTip_const(const XAction* self);

/**
 * @brief      设置工具提示（对标 QAction::setToolTip）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      tip 源文本借用指针；可为 NULL 表示空文本。
 * @return     无返回值；内容真正变化时发射 changed 信号。
 */
void XAction_setToolTip(XAction* self, const XString* tip);

/**
 * @brief      使用 UTF-8 字符串设置工具提示（UTF-8 兼容重载）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL 表示空文本。
 * @return     无返回值；内容真正变化时发射 changed 信号。
 */
void XAction_setToolTip_2(XAction* self, const char* utf8);

/**
 * @brief      获取状态栏提示的拷贝（对标 QAction::statusTip）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；self 为 NULL 或未设置时返回 NULL。
 */
XString* XAction_statusTip(const XAction* self);

/**
 * @brief      获取状态栏提示的内部借用指针。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     内部文本借用指针；未设置或 self 为 NULL 时返回 NULL，不得
 *             释放或修改。
 */
const XString* XAction_statusTip_const(const XAction* self);

/**
 * @brief      设置状态栏提示（对标 QAction::setStatusTip）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      tip 源文本借用指针；可为 NULL 表示空文本。
 * @return     无返回值；内容真正变化时发射 changed 信号。
 */
void XAction_setStatusTip(XAction* self, const XString* tip);

/**
 * @brief      使用 UTF-8 字符串设置状态栏提示（UTF-8 兼容重载）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL 表示空文本。
 * @return     无返回值；内容真正变化时发射 changed 信号。
 */
void XAction_setStatusTip_2(XAction* self, const char* utf8);

/**
 * @brief      获取 What's This 帮助文本的拷贝（对标 QAction::whatsThis）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；self 为 NULL 或未设置时返回 NULL。
 */
XString* XAction_whatsThis(const XAction* self);

/**
 * @brief      获取 What's This 帮助文本的内部借用指针。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     内部文本借用指针；未设置或 self 为 NULL 时返回 NULL，不得
 *             释放或修改。
 */
const XString* XAction_whatsThis_const(const XAction* self);

/**
 * @brief      设置 What's This 帮助文本（对标 QAction::setWhatsThis）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      text 源文本借用指针；可为 NULL 表示空文本。
 * @return     无返回值；内容真正变化时发射 changed 信号。
 */
void XAction_setWhatsThis(XAction* self, const XString* text);

/**
 * @brief      使用 UTF-8 字符串设置 What's This 帮助文本（UTF-8 兼容
 *             重载）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL 表示空文本。
 * @return     无返回值；内容真正变化时发射 changed 信号。
 */
void XAction_setWhatsThis_2(XAction* self, const char* utf8);

/* ==================== 选中状态（对标 QAction） ==================== */

/**
 * @brief      查询动作是否可选中（对标 QAction::isCheckable）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     可选中返回 true；self 为 NULL 或不可选中返回 false。
 */
bool XAction_isCheckable(const XAction* self);

/**
 * @brief      设置动作是否可选中（对标 QAction::setCheckable）。
 * @details    状态真正变化时发射 changed 与 checkableChanged(bool)；按 Qt
 *             语义保留 m_checked 原始位不变（isChecked 需可选中才为 true），
 *             不隐式清除选中位。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      checkable true 表示允许选中，false 表示禁止选中。
 * @return     无返回值；self 为 NULL 时保持所有对象状态不变。
 */
void XAction_setCheckable(XAction* self, bool checkable);

/**
 * @brief      查询动作当前是否选中（对标 QAction::isChecked）。
 * @details    与 Qt 一致：只有可选中动作才返回 true。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     当前选中状态；self 为 NULL、不可选中或未选中时返回 false。
 */
bool XAction_isChecked(const XAction* self);

/**
 * @brief      设置动作选中状态（对标 QAction::setChecked）。
 * @details    只有可选中动作接受状态变更；状态真正变化时发射 changed 与
 *             toggled(bool)。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      checked 目标选中状态。
 * @return     无返回值；动作不可选中、状态未变化或 self 为 NULL 时，原
 *             状态保持不变且不重复发射信号。
 */
void XAction_setChecked(XAction* self, bool checked);

/**
 * @brief      反转动作选中状态（对标 QAction::toggle）。
 * @param      self 待操作的动作对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；不可选中动作不改变状态；状态变化时发射 toggled。
 */
void XAction_toggle(XAction* self);

/* ==================== 启用/可见（对标 QAction） ==================== */

/**
 * @brief      查询动作有效启用状态（对标 QAction::isEnabled）。
 * @details    返回有效启用值；按 Qt 语义，动作不可见时有效启用被强制为
 *             false。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     有效启用状态；self 为 NULL 时返回 false。
 */
bool XAction_isEnabled(const XAction* self);

/**
 * @brief      设置动作显式启用状态（对标 QAction::setEnabled）。
 * @details    记录显式目标值并重算有效启用状态；有效状态真正变化时发射
 *             changed 与 enabledChanged(bool)。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      enabled true 表示启用，false 表示禁用。
 * @return     无返回值；self 为 NULL 时保持所有对象状态不变。
 */
void XAction_setEnabled(XAction* self, bool enabled);

/**
 * @brief      清除显式启用设置，恢复到默认启用（对标 QAction::resetEnabled）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；有效状态变化时发射 changed 与 enabledChanged。
 */
void XAction_resetEnabled(XAction* self);

/**
 * @brief      便捷禁用接口（对标 QAction::setDisabled）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      disabled true 表示禁用，false 表示启用。
 * @return     无返回值；等价于 XAction_setEnabled(self, !disabled)。
 */
void XAction_setDisabled(XAction* self, bool disabled);

/**
 * @brief      查询动作是否可见（对标 QAction::isVisible）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     可见返回 true；self 为 NULL 时返回 false。
 */
bool XAction_isVisible(const XAction* self);

/**
 * @brief      设置动作是否可见（对标 QAction::setVisible）。
 * @details    可见性变化会联动重算有效启用状态（不可见则强制禁用），并
 *             发射 visibleChanged；有效启用变化时同时发射 changed 与
 *             enabledChanged。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      visible true 表示可见，false 表示不可见。
 * @return     无返回值；self 为 NULL 时保持所有对象状态不变。
 */
void XAction_setVisible(XAction* self, bool visible);

/* ==================== 其它属性（对标 QAction） ==================== */

/**
 * @brief      查询是否为分隔条（对标 QAction::isSeparator）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     是分隔条返回 true；self 为 NULL 时返回 false。
 */
bool XAction_isSeparator(const XAction* self);

/**
 * @brief      设置是否为分隔条（对标 QAction::setSeparator）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      separator true 表示分隔条，false 表示普通动作。
 * @return     无返回值；状态真正变化时发射 changed 信号。
 */
void XAction_setSeparator(XAction* self, bool separator);

/**
 * @brief      查询动作优先级（对标 QAction::priority）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     优先级枚举值；self 为 NULL 时返回 XActionPriority_Normal。
 */
XActionPriority XAction_priority(const XAction* self);

/**
 * @brief      设置动作优先级（对标 QAction::setPriority）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      priority 目标优先级枚举值。
 * @return     无返回值；值真正变化时发射 changed 信号。
 */
void XAction_setPriority(XAction* self, XActionPriority priority);

/**
 * @brief      查询动作菜单角色（对标 QAction::menuRole）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     菜单角色枚举值；self 为 NULL 时返回
 *             XActionMenuRole_TextHeuristicRole。
 */
XActionMenuRole XAction_menuRole(const XAction* self);

/**
 * @brief      设置动作菜单角色（对标 QAction::setMenuRole）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      role 目标菜单角色枚举值。
 * @return     无返回值；值真正变化时发射 changed 信号。
 */
void XAction_setMenuRole(XAction* self, XActionMenuRole role);

/**
 * @brief      查询菜单中是否显示图标（对标 QAction::isIconVisibleInMenu）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     显示图标返回 true；self 为 NULL 时返回 false。
 */
bool XAction_isIconVisibleInMenu(const XAction* self);

/**
 * @brief      设置菜单中是否显示图标（对标 QAction::setIconVisibleInMenu）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      visible true 显示图标，false 隐藏图标。
 * @return     无返回值；状态真正变化时发射 changed 信号。
 */
void XAction_setIconVisibleInMenu(XAction* self, bool visible);

/**
 * @brief      查询上下文菜单中是否显示快捷键（对标
 *             QAction::isShortcutVisibleInContextMenu）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     显示快捷键返回 true；self 为 NULL 时返回 false。
 */
bool XAction_isShortcutVisibleInContextMenu(const XAction* self);

/**
 * @brief      设置上下文菜单中是否显示快捷键（对标
 *             QAction::setShortcutVisibleInContextMenu）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      show true 显示快捷键，false 隐藏。
 * @return     无返回值；状态真正变化时发射 changed 信号。
 */
void XAction_setShortcutVisibleInContextMenu(XAction* self, bool show);

/* ==================== 用户数据（对标 QAction::data） ==================== */

/**
 * @brief      查询动作用户数据（对标 QAction::data）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     内部数据借用指针；未设置或 self 为 NULL 时返回 NULL。返回
 *             指针归动作对象所有，调用方不得释放。
 */
XVariant* XAction_data(const XAction* self);

/**
 * @brief      设置动作用户数据（对标 QAction::setData）。
 * @details    按项目惯例采用所有权转移语义：释放旧数据并接管新 data
 *             指针（Qt 为 QVariant 值拷贝，本实现以指针所有权记录差异，
 *             见 XGui.md）。指针发生变化时发射 changed 信号。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      data 新数据指针；所有权转移给动作对象，可为 NULL 表示
 *             清空。
 * @return     无返回值；self 为 NULL 时保持原数据不变。
 */
void XAction_setData(XAction* self, XVariant* data);

/* ==================== 菜单关联（对标 QAction::menu/setMenu） ==================== */

/**
 * @brief      查询关联的弹出菜单（对标 QAction::menu）。
 * @param      self 动作对象的借用指针；可为 NULL。
 * @return     关联菜单借用指针；未设置或 self 为 NULL 时返回 NULL。返回
 *             指针归菜单所有，调用方不得释放。
 */
XMenu* XAction_menu(const XAction* self);

/**
 * @brief      关联一个弹出菜单（对标 QAction::setMenu）。
 * @details    仅保存菜单借用指针，不取得所有权；菜单销毁时本动作自动
 *             解除关联。该关联用于菜单项的子菜单入口（XMenu_addMenu）。
 * @param      self 待修改的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      menu 目标菜单借用指针；可为 NULL 表示解除关联。
 * @return     无返回值；指针变化时发射 changed 信号。
 */
void XAction_setMenu(XAction* self, XMenu* menu);

/* ==================== 激活行为（对标 QAction） ==================== */

/**
 * @brief      触发动作（对标 QAction::trigger）。
 * @details    等价于 XAction_activate(self, XActionEvent_Trigger)：显式
 *             禁用时忽略触发；可选中动作先翻转 checked（发 toggled/
 *             changed），再发射 triggered(bool)。
 * @param      self 待触发的动作对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XAction_trigger(XAction* self);

/**
 * @brief      高亮动作（对标 QAction::hover）。
 * @details    等价于 XAction_activate(self, XActionEvent_Hover)：发射
 *             hovered 信号。
 * @param      self 待高亮的动作对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XAction_hover(XAction* self);

/**
 * @brief      激活动作（对标 QAction::activate）。
 * @param      self 待激活的动作对象；可为 NULL，NULL 时不执行操作。
 * @param      event 激活事件类型：XActionEvent_Trigger 或
 *             XActionEvent_Hover。
 * @return     无返回值。
 */
void XAction_activate(XAction* self, XActionEvent event);

/* ==================== 信号（对标 QAction signals） ==================== */

/**
 * @brief      发射 changed 信号（对标 QAction::changed）。
 * @details    文本族、data、checkable、enabled、visible、separator、
 *             priority、menuRole 等属性变化时发射，无参数。
 * @param      self 发射信号的动作对象；可为 NULL。
 * @return     不透明的 changed 信号标识；返回值不指向可释放对象，也不得
 *             解引用。
 */
void* XAction_changed_signal(XAction* self);

/**
 * @brief      发射 enabledChanged(bool) 信号（对标 QAction::enabledChanged）。
 * @param      self 发射信号的动作对象；可为 NULL。
 * @param      enabled 新的有效启用状态。
 * @return     不透明的 enabledChanged 信号标识。
 */
void* XAction_enabledChanged_signal(XAction* self, bool enabled);

/**
 * @brief      发射 checkableChanged(bool) 信号（对标 QAction::checkableChanged）。
 * @param      self 发射信号的动作对象；可为 NULL。
 * @param      checkable 新的可选中状态。
 * @return     不透明的 checkableChanged 信号标识。
 */
void* XAction_checkableChanged_signal(XAction* self, bool checkable);

/**
 * @brief      发射 visibleChanged 信号（对标 QAction::visibleChanged）。
 * @param      self 发射信号的动作对象；可为 NULL。
 * @return     不透明的 visibleChanged 信号标识。
 */
void* XAction_visibleChanged_signal(XAction* self);

/**
 * @brief      发射 triggered(bool) 信号（对标 QAction::triggered）。
 * @details    trigger 时发射；可选中动作的 bool 参数为翻转后的选中状态。
 * @param      self 发射信号的动作对象；可为 NULL。
 * @param      checked 触发完成后的选中状态。
 * @return     不透明的 triggered 信号标识。
 */
void* XAction_triggered_signal(XAction* self, bool checked);

/**
 * @brief      发射 hovered 信号（对标 QAction::hovered）。
 * @param      self 发射信号的动作对象；可为 NULL。
 * @return     不透明的 hovered 信号标识。
 */
void* XAction_hovered_signal(XAction* self);

/**
 * @brief      发射 toggled(bool) 信号（对标 QAction::toggled）。
 * @details    可选中动作选中状态变化时发射。
 * @param      self 发射信号的动作对象；可为 NULL。
 * @param      checked 新的选中状态。
 * @return     不透明的 toggled 信号标识。
 */
void* XAction_toggled_signal(XAction* self, bool checked);

#endif /* XACTION_ON */

#ifdef __cplusplus
}
#endif

#endif /* XACTION_H */
