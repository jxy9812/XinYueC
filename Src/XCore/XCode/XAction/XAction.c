/**
 * @file       XAction.c
 * @brief      XAction 动作类实现（对标 Qt 6.8 QAction）。
 * @details    对齐 Qt 6.8 QAction 的公共属性、状态机、激活与信号语义：
 *             文本族、checkable/checked、enabled（含 resetEnabled 与
 *             visible 联动）、visible、separator、priority、menuRole、
 *             data、trigger/activate/hover，以及
 *             changed/enabledChanged/checkableChanged/visibleChanged/
 *             triggered/hovered/toggled 七个信号。本实现只使用 XinYueC 的
 *             XObject/XClass/XString/XVariant 抽象层，不依赖 XGui 与任何
 *             平台 API；XObject 基类不提供拷贝/移动语义，因此本类的
 *             Copy/Move 仅复制属性字段，不复制信号连接与父子关系。
 */
#include "XAction.h"
#include "XMemory.h"
#include "XVariant.h"

#include <string.h>

#if XACTION_ON

/* ==================== 信号发射辅助 ==================== */

static void xaction_emitVoid(XAction* self, size_t signal)
{
    XVarList* args = XVarList_create(0);

    if (!args)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xaction_emitBool(XAction* self, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));

    if (!args)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射 changed 信号；所有属性真正变化后统一调用。 */
static void xaction_sendChanged(XAction* self)
{
    xaction_emitVoid(self, (size_t)XAction_changed_signal);
}

/* ==================== 字符串字段辅助 ==================== */

/*
 * 深拷贝替换 XString 字段：内容相同返回 false 且不修改；否则释放旧值、
 * 接管新副本并返回 true。value 为 NULL 时按空字符串处理。
 */
static bool xaction_replaceString(XString** field, const XString* value)
{
    XString* copy;
    XString* current;

    if (!field)
        return false;
    current = *field;
    copy = value ? XString_create_copy(value) : XString_create();
    if (!copy)
        return false;
    if (current && XString_equals(current, copy, XChar_CaseSensitive)) {
        XClassDelete((XClass*)copy);
        return false;
    }
    *field = copy;
    if (current)
        XClassDelete((XClass*)current);
    return true;
}

/* 新建一个字段的深拷贝；未设置字段返回 NULL。 */
static XString* xaction_copyString(const XString* field)
{
    if (!field)
        return NULL;
    return XString_create_copy(field);
}

/*
 * 生成「XString 版本 setter + UTF-8 版本 setter」：内容真正变化时发射
 * changed 信号；_2 变体先按 UTF-8 解码再走 XString 版本，返回后不保留
 * 调用方字符缓冲区。
 */
#define XACTION_DEFINE_TEXT_SET(Name, Field)                                  \
    void XAction_set##Name(XAction* self, const XString* text)               \
    {                                                                         \
        if (!self)                                                            \
            return;                                                           \
        if (xaction_replaceString(&self->Field, text))                        \
            xaction_sendChanged(self);                                        \
    }                                                                         \
    void XAction_set##Name##_2(XAction* self, const char* utf8)              \
    {                                                                         \
        XString* str;                                                         \
        if (!self)                                                            \
            return;                                                           \
        str = XString_create_utf8(utf8 ? utf8 : "");                          \
        if (!str)                                                             \
            return;                                                           \
        XAction_set##Name(self, str);                                         \
        XClassDelete((XClass*)str);                                    \
    }

XACTION_DEFINE_TEXT_SET(Text, m_text)
XACTION_DEFINE_TEXT_SET(IconText, m_iconText)
XACTION_DEFINE_TEXT_SET(ToolTip, m_toolTip)
XACTION_DEFINE_TEXT_SET(Icon, m_iconPath)
XACTION_DEFINE_TEXT_SET(StatusTip, m_statusTip)
XACTION_DEFINE_TEXT_SET(WhatsThis, m_whatsThis)

#undef XACTION_DEFINE_TEXT_SET

/* ==================== 文本族属性 ==================== */

XString* XAction_text(const XAction* self)
{
    return self ? xaction_copyString(self->m_text) : NULL;
}

const XString* XAction_text_const(const XAction* self)
{
    return self ? self->m_text : NULL;
}

XString* XAction_iconText(const XAction* self)
{
    return self ? xaction_copyString(self->m_iconText) : NULL;
}

const XString* XAction_iconText_const(const XAction* self)
{
    return self ? self->m_iconText : NULL;
}

XString* XAction_toolTip(const XAction* self)
{
    return self ? xaction_copyString(self->m_toolTip) : NULL;
}

const XString* XAction_toolTip_const(const XAction* self)
{
    return self ? self->m_toolTip : NULL;
}

XString* XAction_icon(const XAction* self)
{
    return self ? xaction_copyString(self->m_iconPath) : NULL;
}
const XString* XAction_icon_const(const XAction* self)
{
    return self ? self->m_iconPath : NULL;
}

XString* XAction_statusTip(const XAction* self)
{
    return self ? xaction_copyString(self->m_statusTip) : NULL;
}

const XString* XAction_statusTip_const(const XAction* self)
{
    return self ? self->m_statusTip : NULL;
}

XString* XAction_whatsThis(const XAction* self)
{
    return self ? xaction_copyString(self->m_whatsThis) : NULL;
}

const XString* XAction_whatsThis_const(const XAction* self)
{
    return self ? self->m_whatsThis : NULL;
}

/* ==================== 选中状态 ==================== */

bool XAction_isCheckable(const XAction* self)
{
    return self ? self->m_checkable : false;
}

void XAction_setCheckable(XAction* self, bool checkable)
{
    if (!self || self->m_checkable == checkable)
        return;

    self->m_checkable = checkable;
    xaction_sendChanged(self);
    xaction_emitBool(self, (size_t)XAction_checkableChanged_signal,
                     checkable);
    /* 与 Qt 6.8 qaction.cpp:834-844 一致：原始 checked 位为真时，
     * 再补发一次 toggled(checkable) 通知。 */
    if (self->m_checked)
        xaction_emitBool(self, (size_t)XAction_toggled_signal, checkable);
}

bool XAction_isChecked(const XAction* self)
{
    return self ? (self->m_checked && self->m_checkable) : false;
}

void XAction_setChecked(XAction* self, bool checked)
{
    if (!self || self->m_checked == checked)
        return;

    self->m_checked = checked;
    if (!self->m_checkable)
        return;
    xaction_sendChanged(self);
    xaction_emitBool(self, (size_t)XAction_toggled_signal, checked);
}

void XAction_toggle(XAction* self)
{
    if (!self)
        return;
    XAction_setChecked(self, !self->m_checked);
}

/* ==================== 启用/可见 ==================== */

/*
 * 重算有效启用状态（对标 QActionPrivate::setEnabled，qaction.cpp:940-951）：
 * ① 不可见动作强制禁用；
 * ② 非组路径（byGroup=false）且所属组被禁用时，启用被压回禁用
 *    （显式启用不能越过组禁用）；
 * ③ 组路径（byGroup=true）且动作显式设置过启用态时，按显式值裁决
 *    （显式禁用的成员不会被组启用覆盖；组禁用已在 ①② 之前由组自身的
 *    m_enabled 决定传入值，无需在此再判）；
 * 有效状态真正变化时发射 changed 与 enabledChanged，返回是否发生变化。
 */
static bool xaction_updateEnabled(XAction* self, bool enable, bool byGroup)
{
    bool old;

    if (!self)
        return false;
    old = self->m_enabled;
    if (enable && !self->m_visible)
        enable = false;
    if (enable && !byGroup && self->m_group && !self->m_groupEnabled)
        enable = false;
    if (enable && byGroup && self->m_explicitEnabled)
        enable = self->m_explicitEnabledValue;
    if (enable == old)
        return false;

    self->m_enabled = enable;
    xaction_sendChanged(self);
    xaction_emitBool(self, (size_t)XAction_enabledChanged_signal, enable);
    return true;
}

bool XAction_isEnabled(const XAction* self)
{
    return self ? self->m_enabled : false;
}

void XAction_setEnabled(XAction* self, bool enabled)
{
    if (!self)
        return;
    if (self->m_explicitEnabledValue == enabled && self->m_explicitEnabled)
        return;
    self->m_explicitEnabledValue = enabled;
    self->m_explicitEnabled = true;
    xaction_updateEnabled(self, enabled, false);
}

void XAction_resetEnabled(XAction* self)
{
    if (!self || !self->m_explicitEnabled)
        return;
    self->m_explicitEnabled = false;
    xaction_updateEnabled(self, true, false);
}

void XAction_setDisabled(XAction* self, bool disabled)
{
    XAction_setEnabled(self, !disabled);
}

void XAction_setEnabledByGroup(XAction* self, bool enabled)
{
    if (!self)
        return;
    /* 组路径不写显式位：显式启用/禁用只由 XAction_setEnabled 记录，
     * 组的启用动作本身不得把成员「提级」为显式设置（否则组禁用/启用
     * 往返会把用户从未显式设置过的成员永久钉在某个值上）。
     * 同时同步「组 enabled」缓存位：本函数的唯一调用方
     * XActionGroup_setEnabled/addAction 传入的正是组当前状态，非组路径
     * 的 XAction_setEnabled 与 XAction_activate 依赖该缓存判定组禁用。 */
    self->m_groupEnabled = enabled;
    xaction_updateEnabled(self, enabled, true);
}

struct XActionGroup* XAction_actionGroup(const XAction* self)
{
    return self ? self->m_group : NULL;
}

void XAction_bindGroup(XAction* self, struct XActionGroup* group,
                       bool groupEnabled, bool groupExclusive,
                       bool groupChecked)
{
    bool changed;

    if (!self)
        return;
    changed = self->m_group != group;
    self->m_group = group;
    /* 组状态的事实源在 XActionGroup；本类只缓存组 enabled / 组策略
     * Exclusive / 组选中位，供 XAction_setEnabled（非组路径）与
     * XAction_activate（P0-38 反选守卫）判定使用，避免 XCore 反向调用
     * XGui。脱离组后这些缓存清位、不再参与裁决。 */
    self->m_groupEnabled = group ? groupEnabled : true;
    self->m_groupExclusive = group ? groupExclusive : false;
    self->m_groupChecked = group ? groupChecked : false;
    if (group) {
        /* 加入组（qactiongroup.cpp:135 `setEnabled(d->enabled, true)`）：
         * 组启用时允许成员恢复启用（显式禁用者仍禁用），组禁用时一律禁用。 */
        xaction_updateEnabled(self, groupEnabled, true);
        /* 对齐 addAction 中 group 变化时的 sendDataChanged()。 */
        if (changed)
            xaction_sendChanged(self);
        return;
    }
    /* 脱离组：恢复被组强制修改前的启用态——显式值优先，否则默认启用；
     * 仍受 visible 约束。Qt 6.8 的 removeAction 只把 group 置空、不重算
     * 启用态（本机探针实测：组禁用期间移除成员后成员保持禁用），此处的
     * 重算是 XGui 的有意扩展，见 XAction.h 的 XAction_bindGroup @details。 */
    xaction_updateEnabled(self,
                          self->m_explicitEnabled
                              ? self->m_explicitEnabledValue
                              : true,
                          false);
}

void XAction_setGroupExclusiveByGroup(XAction* self, bool exclusive)
{
    /* 组路径缓存同步（P0-38）：不发射信号——Qt 的 setExclusionPolicy
     * 只记策略（qactiongroup.cpp:239-245），守卫在 activate 时即时生效。 */
    if (!self)
        return;
    self->m_groupExclusive = exclusive;
}

void XAction_setGroupCheckedByGroup(XAction* self, bool checked)
{
    /* 组路径缓存同步（P0-38）：组选中缓存写入/清空时由组下发，不发射
     * 信号（对标 Qt 的 d->current 维护同样不发动作通知）。 */
    if (!self)
        return;
    self->m_groupChecked = checked;
}

void XAction_detachGroupQuiet(XAction* self)
{
    if (!self)
        return;
    self->m_group = NULL;
    self->m_groupEnabled = true;
    /* P0-38：组析构同步清反选守卫缓存（脱离组后守卫条件不再成立）。 */
    self->m_groupExclusive = false;
    self->m_groupChecked = false;
    /* 直接重算而非走 xaction_updateEnabled：组析构路径不得发射信号
     * （外部槽可能重入组操作，令析构方的成员表迭代失效）。 */
    self->m_enabled = self->m_visible &&
                      (!self->m_explicitEnabled ||
                       self->m_explicitEnabledValue);
}

bool XAction_isVisible(const XAction* self)
{
    return self ? self->m_visible : false;
}

void XAction_setVisible(XAction* self, bool visible)
{
    bool enable;

    if (!self || self->m_visible == visible)
        return;
    self->m_visible = visible;
    /* 可见性变化后重算有效启用：可见动作按其显式目标值（未显式设置则
     * 默认启用），不可见动作由 xaction_updateEnabled 强制禁用。 */
    enable = visible;
    if (enable && self->m_explicitEnabled)
        enable = self->m_explicitEnabledValue;
    if (!xaction_updateEnabled(self, enable, false))
        xaction_sendChanged(self);
    xaction_emitVoid(self, (size_t)XAction_visibleChanged_signal);
}

/* ==================== 其它属性 ==================== */

bool XAction_isSeparator(const XAction* self)
{
    return self ? self->m_separator : false;
}

void XAction_setSeparator(XAction* self, bool separator)
{
    if (!self || self->m_separator == separator)
        return;
    self->m_separator = separator;
    xaction_sendChanged(self);
}

XActionPriority XAction_priority(const XAction* self)
{
    return self ? self->m_priority : XActionPriority_Normal;
}

void XAction_setPriority(XAction* self, XActionPriority priority)
{
    if (!self || self->m_priority == priority)
        return;
    self->m_priority = priority;
    xaction_sendChanged(self);
}

XActionMenuRole XAction_menuRole(const XAction* self)
{
    return self ? self->m_menuRole : XActionMenuRole_TextHeuristicRole;
}

void XAction_setMenuRole(XAction* self, XActionMenuRole role)
{
    if (!self || self->m_menuRole == role)
        return;
    self->m_menuRole = role;
    xaction_sendChanged(self);
}

bool XAction_isIconVisibleInMenu(const XAction* self)
{
    return self ? self->m_iconVisibleInMenu : false;
}

void XAction_setIconVisibleInMenu(XAction* self, bool visible)
{
    if (!self || self->m_iconVisibleInMenu == visible)
        return;
    self->m_iconVisibleInMenu = visible;
    xaction_sendChanged(self);
}

bool XAction_isShortcutVisibleInContextMenu(const XAction* self)
{
    return self ? self->m_shortcutVisibleInContextMenu : false;
}

void XAction_setShortcutVisibleInContextMenu(XAction* self, bool show)
{
    if (!self || self->m_shortcutVisibleInContextMenu == show)
        return;
    self->m_shortcutVisibleInContextMenu = show;
    xaction_sendChanged(self);
}

/* ==================== 用户数据 ==================== */

XVariant* XAction_data(const XAction* self)
{
    return self ? self->m_data : NULL;
}

void XAction_setData(XAction* self, const XVariant* data)
{
    XVariant* copy;

    if (!self)
        return;
    if (!data && !self->m_data)
        return;
    copy = data ? XVariant_create_copy(data) : NULL;
    if (data && !copy)
        return;                 /* 拷贝失败保持原数据不变 */
    if (self->m_data)
        XClassDelete(self->m_data);
    self->m_data = copy;
    xaction_sendChanged(self);
}

void XAction_setData_move(XAction* self, XVariant* data)
{
    if (!self || self->m_data == data)
        return;
    if (self->m_data)
        XClassDelete(self->m_data);
    self->m_data = data;        /* 接管调用者指针所有权（_move 语义） */
    xaction_sendChanged(self);
}

/* ==================== 菜单关联 ==================== */

/*
 * 关联菜单销毁槽：XAction_setMenu 连接菜单的 destroyed 信号到本槽，
 * 菜单销毁时把 m_menu 置空，避免悬挂指针。
 */
static void xaction_menuDestroyedSlot(XObject* receiver, XVarList* args)
{
    XAction* self = (XAction*)receiver;
    XObject* sender = XObject_sender(receiver);

    (void)args;
    if (self && sender && self->m_menu == (XMenu*)sender)
        self->m_menu = NULL;
}

XMenu* XAction_menu(const XAction* self)
{
    return self ? self->m_menu : NULL;
}

void XAction_setMenu(XAction* self, XMenu* menu)
{
    if (!self || self->m_menu == menu)
        return;
    if (self->m_menu) {
        XObject_disconnect((XObject*)self->m_menu,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xaction_menuDestroyedSlot);
    }
    self->m_menu = menu;
    if (menu) {
        XObject_connect((XObject*)menu,
                          XSignal(XObject_destroyed_signal),
                          (XObject*)self, xaction_menuDestroyedSlot,
                          XConnectionType_Direct);
    }
    xaction_sendChanged(self);
}

/* ==================== 激活行为 ==================== */

void XAction_activate(XAction* self, XActionEvent event)
{
    if (!self)
        return;

    if (event == XActionEvent_Trigger) {
        /* 对标 Qt 6.8 qaction.cpp:1089-1091 activate(Trigger)：
         * 「显式禁用」或「所属组被禁用」时忽略任何触发；可选中动作先翻转
         * checked（内部经 setChecked 发 toggled/changed），再发射
         * triggered(checked)。组启用态取缓存位（组状态由 XActionGroup 同步，
         * XCore 不反向依赖 XGui）。 */
        if (self->m_explicitEnabled && !self->m_explicitEnabledValue)
            return;
        if (self->m_group && !self->m_groupEnabled)
            return;
        if (self->m_checkable) {
            /* P0-38 反选守卫（qaction.cpp:1092-1099 原文口径）：
             * 「互斥组的当前选中动作不可经 activate 反选」——
             * `checked && group && group->exclusionPolicy()==Exclusive &&
             *  group->checkedAction()==this` 时**不翻转** checked，
             * 只发 triggered(true) 后返回。守卫的两个组侧条件取缓存位
             * （组策略/组选中由 XActionGroup 同步），等效于 Qt 的
             * group->exclusionPolicy() 与 group->checkedAction()==this。 */
            if (self->m_checked && self->m_groupExclusive &&
                self->m_groupChecked) {
                XAction_triggered_signal(self, true);
                return;
            }
            XAction_setChecked(self, !self->m_checked);
        }
        XAction_triggered_signal(self, self->m_checked);
    } else if (event == XActionEvent_Hover) {
        XAction_hovered_signal(self);
    }
}

void XAction_trigger(XAction* self)
{
    XAction_activate(self, XActionEvent_Trigger);
}

void XAction_hover(XAction* self)
{
    XAction_activate(self, XActionEvent_Hover);
}

/* ==================== 公开信号 ==================== */

void* XAction_changed_signal(XAction* self)
{
    if (!self)
        return (void*)(size_t)XAction_changed_signal;
    xaction_emitVoid(self, (size_t)XAction_changed_signal);
    return (void*)(size_t)XAction_changed_signal;
}

void* XAction_enabledChanged_signal(XAction* self, bool enabled)
{
    if (!self)
        return (void*)(size_t)XAction_enabledChanged_signal;
    xaction_emitBool(self, (size_t)XAction_enabledChanged_signal, enabled);
    return (void*)(size_t)XAction_enabledChanged_signal;
}

void* XAction_checkableChanged_signal(XAction* self, bool checkable)
{
    if (!self)
        return (void*)(size_t)XAction_checkableChanged_signal;
    xaction_emitBool(self, (size_t)XAction_checkableChanged_signal,
                     checkable);
    return (void*)(size_t)XAction_checkableChanged_signal;
}

void* XAction_visibleChanged_signal(XAction* self)
{
    if (!self)
        return (void*)(size_t)XAction_visibleChanged_signal;
    xaction_emitVoid(self, (size_t)XAction_visibleChanged_signal);
    return (void*)(size_t)XAction_visibleChanged_signal;
}

void* XAction_triggered_signal(XAction* self, bool checked)
{
    if (!self)
        return (void*)(size_t)XAction_triggered_signal;
    xaction_emitBool(self, (size_t)XAction_triggered_signal, checked);
    return (void*)(size_t)XAction_triggered_signal;
}

void* XAction_hovered_signal(XAction* self)
{
    if (!self)
        return (void*)(size_t)XAction_hovered_signal;
    xaction_emitVoid(self, (size_t)XAction_hovered_signal);
    return (void*)(size_t)XAction_hovered_signal;
}

void* XAction_toggled_signal(XAction* self, bool checked)
{
    if (!self)
        return (void*)(size_t)XAction_toggled_signal;
    xaction_emitBool(self, (size_t)XAction_toggled_signal, checked);
    return (void*)(size_t)XAction_toggled_signal;
}

/* ==================== 生命周期 ==================== */

static void xaction_freeStrings(XAction* self)
{
    if (!self)
        return;
    if (self->m_text) {
        XClassDelete((XClass*)self->m_text);
        self->m_text = NULL;
    }
    if (self->m_iconText) {
        XClassDelete((XClass*)self->m_iconText);
        self->m_iconText = NULL;
    }
    if (self->m_iconPath) {
        XClassDelete((XClass*)self->m_iconPath);
        self->m_iconPath = NULL;
    }
    if (self->m_toolTip) {
        XClassDelete((XClass*)self->m_toolTip);
        self->m_toolTip = NULL;
    }
    if (self->m_statusTip) {
        XClassDelete((XClass*)self->m_statusTip);
        self->m_statusTip = NULL;
    }
    if (self->m_whatsThis) {
        XClassDelete((XClass*)self->m_whatsThis);
        self->m_whatsThis = NULL;
    }
}

static void xaction_freeData(XAction* self)
{
    if (!self)
        return;
    if (self->m_data) {
        XClassDelete(self->m_data);
        self->m_data = NULL;
    }
}

/* XObject 基类不提供拷贝/移动语义（XClass 默认槽位为空操作），
 * 因此这里只复制动作属性字段，不复制信号连接与父子关系。 */
static void VXAction_copy(XAction* self, const XAction* other)
{
    if (!self || !other || self == other)
        return;

    if (XClassIsVtableNull(self))
        XAction_init(self);

    xaction_freeStrings(self);
    xaction_freeData(self);
    self->m_text = xaction_copyString(other->m_text);
    self->m_iconText = xaction_copyString(other->m_iconText);
    self->m_toolTip = xaction_copyString(other->m_toolTip);
    self->m_statusTip = xaction_copyString(other->m_statusTip);
    self->m_whatsThis = xaction_copyString(other->m_whatsThis);
    self->m_data = other->m_data ? XVariant_create_copy(other->m_data) : NULL;
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
    self->m_enabled = other->m_enabled;
    self->m_explicitEnabled = other->m_explicitEnabled;
    self->m_explicitEnabledValue = other->m_explicitEnabledValue;
    self->m_visible = other->m_visible;
    self->m_separator = other->m_separator;
    self->m_iconVisibleInMenu = other->m_iconVisibleInMenu;
    self->m_shortcutVisibleInContextMenu =
        other->m_shortcutVisibleInContextMenu;
    self->m_priority = other->m_priority;
    self->m_menuRole = other->m_menuRole;
    /* 组回指不随拷贝复制：组只登记原指针，副本不是组成员。副本按「无组」
     * 重算有效启用态，避免继承源对象因组禁用产生的非显式禁用态
     * （否则副本既不在组内、又无法经 resetEnabled 恢复）。P0-38 守卫缓存
     * 同理清零（副本不是组成员，反选守卫对其永不成立）。 */
    self->m_group = NULL;
    self->m_groupEnabled = true;
    self->m_groupExclusive = false;
    self->m_groupChecked = false;
    self->m_enabled = self->m_visible &&
                      (!self->m_explicitEnabled ||
                       self->m_explicitEnabledValue);
    /* 菜单为借用指针，本类不复制信号连接，但副本必须自行向该菜单注册
     * destroyed 监听（与 XAction_setMenu 相同），否则菜单先销毁时只有
     * 源对象收到通知，副本 m_menu 悬垂；目标原本关联其他菜单时先断开。 */
    if (self->m_menu) {
        XObject_disconnect((XObject*)self->m_menu,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xaction_menuDestroyedSlot);
    }
    self->m_menu = other->m_menu;
    if (self->m_menu) {
        XObject_connect((XObject*)self->m_menu,
                          XSignal(XObject_destroyed_signal),
                          (XObject*)self, xaction_menuDestroyedSlot,
                          XConnectionType_Direct);
    }
}

static void VXAction_move(XAction* self, XAction* other)
{
    if (!self || !other || self == other)
        return;

    if (XClassIsVtableNull(self))
        XAction_init(self);

    xaction_freeStrings(self);
    xaction_freeData(self);
    self->m_text = other->m_text;
    other->m_text = NULL;
    self->m_iconText = other->m_iconText;
    other->m_iconText = NULL;
    self->m_toolTip = other->m_toolTip;
    other->m_toolTip = NULL;
    self->m_statusTip = other->m_statusTip;
    other->m_statusTip = NULL;
    self->m_whatsThis = other->m_whatsThis;
    other->m_whatsThis = NULL;
    self->m_data = other->m_data;
    other->m_data = NULL;
    self->m_checkable = other->m_checkable;
    other->m_checkable = false;
    self->m_checked = other->m_checked;
    other->m_checked = false;
    self->m_enabled = other->m_enabled;
    other->m_enabled = true;
    self->m_explicitEnabled = other->m_explicitEnabled;
    other->m_explicitEnabled = false;
    self->m_explicitEnabledValue = other->m_explicitEnabledValue;
    other->m_explicitEnabledValue = true;
    self->m_visible = other->m_visible;
    other->m_visible = true;
    self->m_separator = other->m_separator;
    other->m_separator = false;
    self->m_iconVisibleInMenu = other->m_iconVisibleInMenu;
    other->m_iconVisibleInMenu = false;
    self->m_shortcutVisibleInContextMenu =
        other->m_shortcutVisibleInContextMenu;
    other->m_shortcutVisibleInContextMenu = false;
    self->m_priority = other->m_priority;
    other->m_priority = XActionPriority_Normal;
    self->m_menuRole = other->m_menuRole;
    other->m_menuRole = XActionMenuRole_TextHeuristicRole;
    /* 源对象仍是组成员（组的成员表指向它），复位后的有效启用态必须继续
     * 服从组禁用缓存，否则源壳会在禁用组里显示为启用。 */
    other->m_enabled = other->m_visible &&
                       (!other->m_explicitEnabled ||
                        other->m_explicitEnabledValue) &&
                       (!other->m_group || other->m_groupEnabled);
    /* 组回指不随移动转移：组的成员表仍指向源对象 other；目标不在组内，
     * 按「无组」重算有效启用态（源对象保持其组成员身份与缓存位）。
     * P0-38 守卫缓存同口径：目标清零；源 other 的 Exclusive/组选中缓存
     * 不动（组成员身份未变，缓存对源仍有效）。 */
    self->m_group = NULL;
    self->m_groupEnabled = true;
    self->m_groupExclusive = false;
    self->m_groupChecked = false;
    self->m_enabled = self->m_visible &&
                      (!self->m_explicitEnabled ||
                       self->m_explicitEnabledValue);
    /* 菜单为借用指针：move 同步迁移 destroyed 监听——先断开目标旧菜单与
     * 源对象的既有连接，再把借用的菜单改挂到目标并重新注册（与
     * XAction_setMenu/VXAction_copy 保持同一注册不变式），否则菜单先销毁
     * 时源对象的旧槽只会清空源，目标 m_menu 悬垂。 */
    if (self->m_menu) {
        XObject_disconnect((XObject*)self->m_menu,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xaction_menuDestroyedSlot);
    }
    self->m_menu = other->m_menu;
    other->m_menu = NULL;
    if (self->m_menu) {
        XObject_disconnect((XObject*)self->m_menu,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)other, xaction_menuDestroyedSlot);
        XObject_connect((XObject*)self->m_menu,
                          XSignal(XObject_destroyed_signal),
                          (XObject*)self, xaction_menuDestroyedSlot,
                          XConnectionType_Direct);
    }
}

static void VXAction_deinit(XAction* self)
{
    if (!self)
        return;

    /* 断开与关联菜单的 destroyed 连接，避免菜单随后销毁时回调已释放
     * 的接收者；菜单已先销毁时 m_menu 已被置空，此处为空操作。 */
    if (self->m_menu) {
        XObject_disconnect((XObject*)self->m_menu,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xaction_menuDestroyedSlot);
        self->m_menu = NULL;
    }
    xaction_freeStrings(self);
    xaction_freeData(self);
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XAction_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAction)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAction_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAction_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAction_deinit);
    return XVTABLE_DEFAULT;
}

void XAction_init(XAction* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(XAction));
    XObject_init(&self->m_class);
    XClassSetVtable(self, XAction);
    self->m_enabled = true;
    self->m_groupEnabled = true;   /* 无组时缓存不参与裁决 */
    self->m_visible = true;
    self->m_priority = XActionPriority_Normal;
    self->m_menuRole = XActionMenuRole_TextHeuristicRole;
}

void XAction_init_2(XAction* self, XObject* parent, const char* utf8Text)
{
    if (!self)
        return;
    XAction_init(self);
    if (utf8Text && utf8Text[0])
        XAction_setText_2(self, utf8Text);
    if (parent)
        XObject_setParent((XObject*)self, parent);
}

XAction* XAction_create(void)
{
    return XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
}

XAction* XAction_create_ex(XMemoryType memory, XObject* parent,
                           const char* utf8Text)
{
    XAction* self = (XAction*)XMemory_malloc(sizeof(XAction), memory);

    if (!self)
        return NULL;
    memset(self, 0, sizeof(XAction));
    XAction_init_2(self, parent, utf8Text);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XAction* XAction_create_copy(const XAction* other)
{
    XAction* self;

    if (!other)
        return NULL;
    self = XAction_create();
    if (!self)
        return NULL;
    XClassCopy(self, other);
    return self;
}

XAction* XAction_create_move(XAction* other)
{
    XAction* self;

    if (!other)
        return NULL;
    self = XAction_create();
    if (!self)
        return NULL;
    XClassMove(self, other);
    return self;
}

#endif /* XACTION_ON */
