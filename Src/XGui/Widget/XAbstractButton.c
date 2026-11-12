/**
 * @file       XAbstractButton.c
 * @brief      XAbstractButton 抽象按钮基类实现。
 * @details    对齐 Qt 6.8 QAbstractButton 的公共状态、激活、自动重复、
 *             自动互斥和输入事件语义；具体外观、尺寸提示和专有属性由
 *             派生控件实现。本实现只使用 XinYueC 的 XWidget、XObject
 *             和 XClass 抽象层，不调用 Win32、POSIX、Qt 或其他平台 API。
 */
#if XGUI_ON

#include "XAbstractButton.h"

#include "XAlgorithm.h"
#include <stdio.h>
#include "XStringUtils.h"
#include "XAbstractButton_Protected.h"
#include "XStyle.h"            /* XStyle_defaultStyle/XStyle_pixelMetric 声明——
                               * 缺它即 C 隐式声明：指针实参按 int 截断
                               * （实证 0x569b00fcb600→0xfcb600 段错误）。
                               * XStylePM_ButtonIconSize 取自下者。 */
#include "XStyleOption.h"      /* XStylePM_ButtonIconSize（D1 iconSize 缺省对齐）。 */
#include "XButtonGroup.h"
#include "XShortcut.h"
#include "XWidget_Protected.h"
#if XWINDOW_ON && XACCESSIBLE_ON
#include "XPlatformAccessibility.h" /* [无障碍补全] StateChanged 通知。 */
#endif
#include "XMemory.h"
#include "XVarList.h"
#include "XVector.h"


#if XWIDGET_ON && XABSTRACTBUTTON_ON

/* ==================== 按压键集合（对标 Qt ButtonPressKeys） ==================== */

/*
 * Qt 6.8 Qt::Key_Select = 0x01010000（QtCore/qnamespace.h:1016，
 * 「Keypad navigation keys」段）。XEvent.h 的 XKey 枚举由核心事件
 * 批次维护，当前没有 Select 项；按键码在事件里本就以 int 承载，这里
 * 按同值常量参与匹配（核心批次补枚举后可直接替换为 XKey_Select）。
 * Qt 平台主题的缺省按压键集 = {Key_Space, Key_Select}
 * （qtbase/src/gui/kernel/qplatformtheme.cpp:637-638）。
 * [共享帮助函数 2026-11-11] 此前是本文件静态的 abstractbutton_isPressKey；
 * 同族控件（XGroupBox 的键盘激活，qcgroupbox.cpp 的 QEvent::KeyPress 分支）
 * 需要同一键集，提为公开帮助函数避免两处各抄一份键集。
 */
#define ABSTRACTBUTTON_KEY_SELECT 0x01010000

bool XAbstractButton_isButtonPressKey(int key)
{
    return key == XKey_Space || key == ABSTRACTBUTTON_KEY_SELECT;
}

/* ==================== 派生类登记 ==================== */

/*
 * QAbstractButton 的派生类没有 RTTI；登记静态虚表使自动互斥组可以从
 * XObject 子列表中识别所有按钮派生对象。该表随进程存在，只在 GUI
 * 线程的 class_init 路径写入。
 */
static XVector* g_abstractButtonClasses;

static bool abstractbutton_vtableRegistered(const XVtable* vtable)
{
    int64_t count;
    int64_t index;

    if (!vtable)
        return false;
    if (vtable == XAbstractButton_class_init())
        return true;
    if (!g_abstractButtonClasses)
        return false;

    count = (int64_t)XVector_size_base(
        (const XContainer*)g_abstractButtonClasses);
    for (index = 0; index < count; ++index) {
        XVtable** item = (XVtable**)XVector_at_base(
            g_abstractButtonClasses, index);
        if (item && *item == vtable)
            return true;
    }
    return false;
}

void XAbstractButton_registerClass(XVtable* vtable)
{
    int64_t count;
    int64_t index;

    if (!vtable || vtable == XAbstractButton_class_init() ||
        XVtable_size(vtable) < XCLASS_VTABLE_GET_SIZE(XAbstractButton))
        return;

    if (!g_abstractButtonClasses) {
        g_abstractButtonClasses = XVector_Create(XVtable*);
        if (!g_abstractButtonClasses)
            return;
    }

    count = (int64_t)XVector_size_base(
        (const XContainer*)g_abstractButtonClasses);
    for (index = 0; index < count; ++index) {
        XVtable** item = (XVtable**)XVector_at_base(
            g_abstractButtonClasses, index);
        if (item && *item == vtable)
            return;
    }
    XVector_push_back_1_base(g_abstractButtonClasses, &vtable);
}

bool XAbstractButton_isInstance(const XObject* object)
{
    if (!object || !XClassGetVtable(object))
        return false;
    return abstractbutton_vtableRegistered(XClassGetVtable(object));
}

/* ==================== 内部状态与信号工具 ==================== */

static void abstractbutton_stopRepeatTimer(XAbstractButton* self);
static void abstractbutton_startRepeatTimer(XAbstractButton* self,
                                            int interval);
static void abstractbutton_stopAnimateTimer(XAbstractButton* self);
static void abstractbutton_startAnimateTimer(XAbstractButton* self);
static void abstractbutton_setShortcutInternal(XAbstractButton* self,
                                               const XString* shortcut,
                                               bool registerBinding);

static void abstractbutton_emitVoid(XAbstractButton* self, size_t signal)
{
    XVarList* arguments = XVarList_create(0);

    if (!arguments)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

static void abstractbutton_emitBool(XAbstractButton* self, size_t signal,
                                    bool value)
{
    XVarList* arguments = XVarList_Create(XVar(bool, value));

    if (!arguments)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

static void abstractbutton_stopRepeatTimer(XAbstractButton* self)
{
    if (!self || self->m_repeatTimer == XTIMER_INVALID_ID)
        return;
    XObject_killTimer((XObject*)self, self->m_repeatTimer);
    self->m_repeatTimer = XTIMER_INVALID_ID;
}

static void abstractbutton_startRepeatTimer(XAbstractButton* self,
                                            int interval)
{
    uint64_t intervalMs;

    if (!self || !self->m_autoRepeat || !self->m_down)
        return;
    abstractbutton_stopRepeatTimer(self);
    /*
     * 当前事件调度器拒绝 0ms 定时器；属性本身仍保留调用者传入值，
     * 仅在实际注册时收敛到 1ms。
     */
    intervalMs = interval > 0 ? (uint64_t)interval : 1u;
    self->m_repeatTimer = XObject_startTimer(
        (XObject*)self, intervalMs, XTimerType_PreciseTimer);
}

static void abstractbutton_stopAnimateTimer(XAbstractButton* self)
{
    if (!self || self->m_animateTimer == XTIMER_INVALID_ID)
        return;
    XObject_killTimer((XObject*)self, self->m_animateTimer);
    self->m_animateTimer = XTIMER_INVALID_ID;
}

static void abstractbutton_startAnimateTimer(XAbstractButton* self)
{
    if (!self)
        return;
    abstractbutton_stopAnimateTimer(self);
    self->m_animateTimer = XObject_startTimer(
        (XObject*)self, 100u, XTimerType_PreciseTimer);
}

/*
 * 输入事件使用本函数同步 down、自动重复和信号；公共 setDown() 不能
 * 发射 pressed/released，因此单独保留该内部路径。
 */
static void abstractbutton_setDownFromInput(XAbstractButton* self, bool down)
{
    if (!self || self->m_down == down)
        return;

    self->m_down = down;
    XWidget_repaint((XWidget*)self);
    if (down && self->m_autoRepeat)
        abstractbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!down)
        abstractbutton_stopRepeatTimer(self);

    if (down)
        XAbstractButton_pressed_signal(self);
    else
        XAbstractButton_released_signal(self);
}

/* ==================== 快捷键绑定（对标 Qt grabShortcut 登记） ==================== */

/* 快捷键激活槽（Qt QEvent::Shortcut → animateClick 的等价路径；无头/
 * 无定时器循环环境退化为同步 click，见 setShortcut 处注释）。 */
static void abstractbutton_shortcutActivatedSlot(XObject* receiver,
                                                 XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractButton_click((XAbstractButton*)receiver);
}

/** @brief 回收 setShortcut 注册的 XShortcut 绑定（断连后删除，删除路径
 *         自动从全局注册表注销）。 */
static void abstractbutton_shortcutDetach(XAbstractButton* self)
{
    XShortcut* binding;

    if (!self || !self->m_shortcutBinding)
        return;
    binding = (XShortcut*)self->m_shortcutBinding;
    XObject_disconnect((XObject*)binding,
                         XSignal(XShortcut_activated_signal),
                         (XObject*)self,
                         abstractbutton_shortcutActivatedSlot);
    XClassDelete((XClass*)binding);
    self->m_shortcutBinding = NULL;
}

/**
 * @brief      从快捷键文本解析单键键码（Qt QKeySequence 全量解析的轻量等价物）。
 * @details    取末个 '+' 之后的键名段：单字节 ASCII 直接用码位（XKey 的
 *             0x20..0x7e 与 ASCII 对齐），"F1".."F12" 映射功能键码。
 * @note       **含修饰位前缀的文本一律返回 0（fail-closed）**：XShortcut
 *             注册表只按单键码匹配、没有修饰位承载，把 "Ctrl+S" 的末段
 *             `S` 当**裸键**登记会让普通 `S` 键也命中该按钮——比不登记更
 *             糟（同文件助记符路径正为避免这一点才传 registerBinding=false，
 *             见 setText 处注释）。因此这类文本只保存承载、不登记绑定；
 *             待 XShortcut 建模修饰位后再谈 Qt 级的 Ctrl/Alt/Shift/Meta
 *             语义。无法解析的键名同样返回 0（调用方不注册）。
 */
static int abstractbutton_shortcutKeyFromUtf8(const char* utf8)
{
    static const struct
    {
        const char* name;
        int key;
    } kNamedKeys[] = {
        { "F1", XKey_F1 },   { "F2", XKey_F2 },   { "F3", XKey_F3 },
        { "F4", XKey_F4 },   { "F5", XKey_F5 },   { "F6", XKey_F6 },
        { "F7", XKey_F7 },   { "F8", XKey_F8 },   { "F9", XKey_F9 },
        { "F10", XKey_F10 }, { "F11", XKey_F11 }, { "F12", XKey_F12 },
    };
    const char* segment;
    int64_t i;
    size_t length;

    if (!utf8)
        return 0;
    /* 取最后一个 '+' 之后的段（无 '+' 用整串）。 */
    segment = NULL;
    for (i = 0; utf8[i] != '\0'; ++i) {
        if (utf8[i] == '+')
            segment = &utf8[i + 1];
    }
    /*
     * 最后一个 '+' 之前存在非空前缀 ⇒ 修饰位组合（"Ctrl+S"/"F5+F6"/
     * "C++" 的 "C+" 等）：只保存文本、不登记任何绑定。前缀为空
     * （"+S"，'+' 本身是首字符）不算修饰位，仍按裸键解析。
     */
    if (segment && segment > utf8 + 1)
        return 0;
    if (!segment || *segment == '\0')
        segment = utf8;
    if (*segment == '\0')
        return 0;
    length = XStrlen(segment);
    if (length == 1 && (unsigned char)segment[0] >= 0x20u &&
        (unsigned char)segment[0] <= 0x7eu) {
        return (int)(unsigned char)segment[0];
    }
    for (i = 0; i < (int64_t)(sizeof(kNamedKeys) / sizeof(kNamedKeys[0]));
         ++i) {
        if (XStrcmp(kNamedKeys[i].name, segment) == 0)
            return kNamedKeys[i].key;
    }
    return 0;
}

/** @brief 按当前文本承载重建 XShortcut 绑定（Qt grabShortcut 的等价登记）。 */
static void abstractbutton_shortcutAttach(XAbstractButton* self)
{
    int key;
    XShortcut* binding;

    abstractbutton_shortcutDetach(self);
    if (!self->m_shortcut)
        return;
    key = abstractbutton_shortcutKeyFromUtf8(XString_toUtf8(self->m_shortcut));
    if (key == 0)
        return;
    /* 无父级登记（不进 XObject 父子链，生命周期完全由按钮管理）；
     * create 路径自动注册进全局注册表。 */
    binding = XShortcut_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE, key, NULL);
    if (!binding)
        return;
    XObject_connect((XObject*)binding,
                      XSignal(XShortcut_activated_signal),
                      (XObject*)self,
                      abstractbutton_shortcutActivatedSlot,
                      XConnectionType_Direct);
    self->m_shortcutBinding = binding;
}

static void abstractbutton_refresh(XAbstractButton* self)
{
    if (!self)
        return;
    /*
     * 内容（文本/图标/图标尺寸）真正变化后，先让派生按钮通过虚表刷新
     * 自己的内容相关存储（例如 sizeHint 存储位），再通知布局与重绘。
     */
    XAbstractButton_contentChanged_base(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/* ==================== 自动互斥（对标 Qt queryButtonList/queryCheckedButton） ==================== */

/**
 * @brief 递归收集 parent 子树里的按钮对象（Qt findChildren 的等价）。
 * @details 对标 QAbstractButtonPrivate::queryButtonList
 *          （qabstractbutton.cpp:153-174）：Qt 用
 *          `parent->findChildren<QAbstractButton *>()` **递归**收集，
 *          因此「父控件下深层容器里的按钮」与「直接子按钮」同属一个
 *          候选集（注意：起点是该按钮自己的**直接父控件**，所以两边
 *          嵌套深度不同的按钮看到的候选集不一定对称，与 Qt 逐位一致）。
 */
static void abstractbutton_collectDescendantButtons(const XObject* parent,
                                                    XVector* out)
{
    const XVector* children;
    int64_t count;
    int64_t index;

    if (!parent || !out)
        return;
    children = XObject_children(parent);
    if (!children)
        return;

    count = (int64_t)XVector_size_base((const XContainer*)children);
    for (index = 0; index < count; ++index) {
        XObject** entry = (XObject**)XVector_at_base(children, index);
        XObject* object = entry ? *entry : NULL;

        if (!object)
            continue;
        if (XObject_isWidgetType(object) && XAbstractButton_isInstance(object))
            XVector_push_back_1_base(out, &object);
        /* Qt 的 findChildren 会继续深入任意 QObject 子树，这里同样
         * 递归所有子对象（按钮的父链只可能是控件，但容器对象可能先
         * 出现在子表里）。 */
        abstractbutton_collectDescendantButtons(object, out);
    }
}

/**
 * @brief 计算按钮的候选成员表（新建 XVector，调用方负责 XClassDelete）。
 * @details 对标 Qt QAbstractButtonPrivate::queryButtonList：
 *          - 已登记 XButtonGroup ⇒ 返回组成员（与父链无关）；
 *          - 未登记组 ⇒ 从**直接父控件**递归 findChildren；
 *          - autoExclusive ⇒ 过滤出「自身 autoExclusive 且未登记显式组」
 *            的成员（Qt 的 isNoMemberOfMyAutoExclusiveGroup 谓词）。
 *          返回空表（非 NULL）表示没有候选；分配失败返回 NULL。
 */
static XVector* abstractbutton_queryButtonList(const XAbstractButton* self)
{
    XVector* list;

    if (!self)
        return NULL;
    list = XVector_Create(XAbstractButton*);
    if (!list)
        return NULL;

    if (self->m_group) {
        const XVector* members = XButtonGroup_buttons(
            (const XButtonGroup*)self->m_group);
        int64_t count;
        int64_t index;

        if (members) {
            count = (int64_t)XVector_size_base((const XContainer*)members);
            for (index = 0; index < count; ++index) {
                XAbstractButton** item = (XAbstractButton**)XVector_at_base(
                    members, index);
                if (item && *item)
                    XVector_push_back_1_base(list, item);
            }
        }
    } else {
        XWidget* parent = XWidget_parentWidget((const XWidget*)self);

        if (parent)
            abstractbutton_collectDescendantButtons((const XObject*)parent,
                                                    list);
        if (self->m_autoExclusive) {
            int64_t index;

            for (index = (int64_t)XVector_size_base(
                     (const XContainer*)list) - 1;
                 index >= 0; --index) {
                XAbstractButton** item = (XAbstractButton**)XVector_at_base(
                    list, index);
                if (!item || !*item || !(*item)->m_autoExclusive ||
                    (*item)->m_group) {
                    XVector_remove_base(list, index, 1);
                }
            }
        }
    }
    return list;
}

/** @brief 候选表是否包含指定按钮（借用比较）。 */
static bool abstractbutton_listContains(const XVector* list,
                                        const XAbstractButton* button)
{
    int64_t count;
    int64_t index;

    if (!list || !button)
        return false;
    count = (int64_t)XVector_size_base((const XContainer*)list);
    for (index = 0; index < count; ++index) {
        XAbstractButton** item = (XAbstractButton**)XVector_at_base(list,
                                                                    index);
        if (item && *item == button)
            return true;
    }
    return false;
}

/**
 * @brief 独占组当前选中项（Qt QAbstractButtonPrivate::queryCheckedButton）。
 * @details qabstractbutton.cpp:176-194：
 *          - 显式组 ⇒ group->checkedButton；
 *          - 未登记组且非 autoExclusive，或候选表只有自己 ⇒ NULL
 *            （单按钮不构成组）；
 *          - 否则返回第一个**其它**已选中成员；没有则返回自己
 *            （checked 时）。
 */
static XAbstractButton* abstractbutton_queryCheckedButton(
    const XAbstractButton* self)
{
    XVector* list;
    XAbstractButton* result = NULL;
    int64_t count;
    int64_t index;

    if (!self)
        return NULL;
    if (self->m_group)
        return XButtonGroup_checkedButton((const XButtonGroup*)self->m_group);
    /* Qt 的 `if (!autoExclusive ...) return nullptr` 提前分支：普通
     * checkable 按钮（复选框/工具按钮）不必为每次 setChecked 走一遍
     * 子树收集（等价语义、零分配快路）。 */
    if (!self->m_autoExclusive)
        return NULL;

    list = abstractbutton_queryButtonList(self);
    if (!list)
        return NULL;
    count = (int64_t)XVector_size_base((const XContainer*)list);
    /* Qt：`if (!autoExclusive || buttonList.size() == 1) return nullptr;`
     * ——单成员 autoExclusive 不构成组，必须返回 NULL，否则「唯一选中项
     * 不可反选」的守卫会挡住单按钮反选（本机 Qt 探针 E：单 radio
     * setChecked(false) 生效）。 */
    if (!self->m_autoExclusive || count == 1) {
        XClassDelete((XClass*)list);
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        XAbstractButton** item = (XAbstractButton**)XVector_at_base(list,
                                                                    index);
        if (item && *item && *item != self && (*item)->m_checked) {
            result = *item;
            break;
        }
    }
    XClassDelete((XClass*)list);
    if (result)
        return result;
    return self->m_checked ? (XAbstractButton*)self : NULL;
}

/**
 * @brief 选中后的互斥通知（Qt QAbstractButtonPrivate::notifyChecked）。
 * @details qabstractbutton.cpp:196-211：显式组由 QButtonGroup 自己维护
 *          tracked/反选（本库 XButtonGroup 的 toggled 槽承担）；未登记组
 *          的 autoExclusive 只反选 queryCheckedButton() 找到的**第一颗**
 *          其它选中按钮（不是全部——Qt 同款，见探针对照）。
 */
static void abstractbutton_notifyChecked(XAbstractButton* self)
{
    XAbstractButton* other;

    if (!self || self->m_group || !self->m_autoExclusive)
        return;
    other = abstractbutton_queryCheckedButton(self);
    if (other && other != self)
        XAbstractButton_setChecked(other, false);
}

/**
 * @brief 当前按钮是否为「独占组内的当前选中项」。
 * @details 对标 Qt QAbstractButtonPrivate::queryCheckedButton()==this 与
 *          随后的 `group ? group->exclusive : autoExclusive` 判定
 *          （qabstractbutton.cpp:176-194 + 351-362 + 593-604）：
 *          - 已登记 XButtonGroup：仅当组的 checkedButton 是自己且组为
 *            独占时才成立（**非独占组里 autoExclusive 不参与**——Qt 的
 *            三目在 group 存在时只取组的 exclusive）；
 *          - 未登记组：autoExclusive 且候选表里没有其它选中项。
 *          候选表按 Qt 递归 findChildren 计算（见 queryButtonList），
 *          setChecked 的反选早退与 click 的 changeState 闸共用本判定。
 */
static bool abstractbutton_isExclusiveChecked(const XAbstractButton* self)
{
    if (!self || !self->m_checked)
        return false;
    if (abstractbutton_queryCheckedButton(self) != self)
        return false;
    if (self->m_group)
        return XButtonGroup_exclusive((const XButtonGroup*)self->m_group);
    return self->m_autoExclusive;
}

/* ==================== 组内方向键移焦（对标 QAbstractButtonPrivate::moveFocus） ==================== */

/** @brief 整数绝对值（避免引入平台 math 依赖）。 */
static int abstractbutton_absInt(int value)
{
    return value < 0 ? -value : value;
}

/**
 * @brief 在互斥组内按方向键移动焦点（Qt QAbstractButtonPrivate::moveFocus）。
 * @details 逐条对标 qabstractbutton.cpp:213-300：
 *          - 候选表 = queryButtonList；exclusive = group?group->exclusive
 *            : autoExclusive；
 *          - 以**当前焦点控件**为参照（不是 self），焦点控件必须属于
 *            候选表，否则不移动；
 *          - 候选过滤：非焦点自身、同一窗口、启用、未隐藏，且
 *            （exclusive 或焦点策略含 TabFocus）；
 *          - 打分：同列/同行（按方向轴）优先取主轴距离，否则用
 *            (1<<30)+距离平方；只接受方向正确的一侧；
 *          - exclusive 且当前焦点按钮已选中且候选可选中 ⇒ 先 click
 *            候选（选中随焦点迁移）；
 *          - 最后 setFocus：Up/Left 用 Backtab，Down/Right 用 Tab。
 */
static void abstractbutton_moveFocus(XAbstractButton* self, int key)
{
    XVector* list;
    bool exclusive;
    bool hasCandidate = false;
    XAbstractButton* candidate = NULL;
    XWidget* focus;
    XAbstractButton* fb;
    XRect target;
    XPoint goalPoint;
    int64_t bestScore = -1;
    int64_t count;
    int64_t index;

    if (!self)
        return;
    list = abstractbutton_queryButtonList(self);
    if (!list)
        return;
    exclusive = self->m_group
                    ? XButtonGroup_exclusive(
                          (const XButtonGroup*)self->m_group)
                    : self->m_autoExclusive;

    focus = XWidget_focusWidget((const XWidget*)self);
    fb = (focus && XAbstractButton_isInstance((const XObject*)focus))
             ? (XAbstractButton*)focus
             : NULL;
    if (!fb || !abstractbutton_listContains(list, fb)) {
        XClassDelete((XClass*)list);
        return;
    }

    target = XWidget_rect(focus);
    {
        XPoint origin;

        XPoint_init(&origin, 0, 0);
        origin = XWidget_mapToGlobal(focus, &origin);
        target.x = origin.x;
        target.y = origin.y;
    }
    goalPoint = XRect_center(&target);

    count = (int64_t)XVector_size_base((const XContainer*)list);
    for (index = 0; index < count; ++index) {
        XAbstractButton** item = (XAbstractButton**)XVector_at_base(list,
                                                                    index);
        XAbstractButton* button;
        XRect rect;
        XPoint center;
        int64_t score;
        int dx;
        int dy;

        if (!item || !*item)
            continue;
        button = *item;
        if (button == fb)
            continue;
        if (XWidget_topLevelWidget((const XWidget*)button) !=
            XWidget_topLevelWidget(focus))
            continue;
        if (!XWidget_isEnabled((const XWidget*)button) ||
            XWidget_isHidden((const XWidget*)button))
            continue;
        if (!exclusive &&
            (XWidget_focusPolicy((const XWidget*)button) &
             XWidgetFocusPolicy_TabFocus) == 0)
            continue;

        rect = XWidget_rect((const XWidget*)button);
        {
            XPoint origin;

            XPoint_init(&origin, 0, 0);
            origin = XWidget_mapToGlobal((const XWidget*)button, &origin);
            rect.x = origin.x;
            rect.y = origin.y;
        }
        center = XRect_center(&rect);
        dx = center.x - goalPoint.x;
        dy = center.y - goalPoint.y;

        if ((rect.x < XRect_right(&target) &&
             target.x < XRect_right(&rect)) &&
            (key == XKey_Up || key == XKey_Down)) {
            /* 与目标同列：主轴（纵向）距离优先。 */
            score = ((int64_t)abstractbutton_absInt(dy) << 16) +
                    abstractbutton_absInt(dx);
        } else if ((rect.y < XRect_bottom(&target) &&
                    target.y < XRect_bottom(&rect)) &&
                   (key == XKey_Left || key == XKey_Right)) {
            /* 与目标同行：主轴（横向）距离优先。 */
            score = ((int64_t)abstractbutton_absInt(dx) << 16) +
                    abstractbutton_absInt(dy);
        } else {
            score = (int64_t)(1 << 30) + (int64_t)dy * dy + (int64_t)dx * dx;
        }
        /* Qt：已有候选且分数更大 ⇒ 跳过（分数小者优先）。 */
        if (score > bestScore && hasCandidate)
            continue;

        switch (key) {
        case XKey_Up:
            if (center.y < goalPoint.y) {
                candidate = button;
                bestScore = score;
                hasCandidate = true;
            }
            break;
        case XKey_Down:
            if (center.y > goalPoint.y) {
                candidate = button;
                bestScore = score;
                hasCandidate = true;
            }
            break;
        case XKey_Left:
            if (center.x < goalPoint.x) {
                candidate = button;
                bestScore = score;
                hasCandidate = true;
            }
            break;
        case XKey_Right:
            if (center.x > goalPoint.x) {
                candidate = button;
                bestScore = score;
                hasCandidate = true;
            }
            break;
        default:
            break;
        }
    }

    if (exclusive && candidate && fb->m_checked && candidate->m_checkable)
        XAbstractButton_click(candidate);
    if (candidate) {
        XWidget_setFocus_2(
            (XWidget*)candidate,
            (key == XKey_Up || key == XKey_Left) ? XFocusReason_Backtab
                                                 : XFocusReason_Tab);
    }
    XClassDelete((XClass*)list);
}

/**
 * @brief 方向键分派（Qt QAbstractButton::keyPressEvent 的 Up/Left/Right/Down 分支）。
 * @return 事件应被接受（消费）返回 true；应 ignore 交父链返回 false。
 * @details 对标 qabstractbutton.cpp:1024-1077：
 *          - autoExclusive 或已登记组（Qt 还有「父控件是 item view
 *            viewport」的兼容 hack，本库暂不建模）⇒ moveFocus；焦点
 *            没动（hasFocus 仍成立）时 ignore 让事件上抛；
 *          - 否则走普通焦点遍历：Up/Left 向前、Down/Right 向后（RTL 下
 *            Left/Right 互换），经 XWidget 的焦点链实现。
 */
static bool abstractbutton_handleArrowKey(XAbstractButton* self, int key)
{
    if (!self)
        return false;
    if (self->m_autoExclusive || self->m_group) {
        abstractbutton_moveFocus(self, key);
        return !XWidget_hasFocus((XWidget*)self);
    }
    {
        XWidget* parent = XWidget_parentWidget((const XWidget*)self);
        XWidget* layoutOwner = parent ? parent : (XWidget*)self;
        bool reverse = XWidget_layoutDirection(layoutOwner) ==
                       XWidgetLayoutDirection_RightToLeft;
        bool next = true;

        if (key == XKey_Up)
            next = false;
        else if ((key == XKey_Left && !reverse) ||
                 (key == XKey_Right && reverse))
            next = false;
        if (next)
            XWidget_focusNextChild((XWidget*)self);
        else
            XWidget_focusPreviousChild((XWidget*)self);
        return true;
    }
}

/* ==================== 激活流程 ==================== */

static void abstractbutton_repeatTimeout(XAbstractButton* self)
{
    if (!self || !self->m_down || !XWidget_isEnabled((XWidget*)self))
        return;

    /*
     * 对标 Qt QAbstractButton::timerEvent（qabstractbutton.cpp:1101-1125）：
     * 重复拍无条件调 nextCheckState（二态翻转由基类默认实现自己判
     * checkable；QToolButton 借此把翻转交给默认动作）。
     */
    XAbstractButton_nextCheckState_base(self);
    XAbstractButton_released_signal(self);
    XAbstractButton_clicked_signal(self, self->m_checked);
    if (self->m_down)
        XAbstractButton_pressed_signal(self);
}

static void abstractbutton_clickInternal(XAbstractButton* self,
                                         bool emitPressed)
{
    if (!self || !XWidget_isEnabled((XWidget*)self))
        return;

    if (emitPressed) {
        /*
         * QAbstractButton::click() 即使调用前处于 down 状态也重放一对
         * pressed/released；这里不经 setDown()，避免程序化 click 注册
         * 自动重复定时器。
         */
        self->m_down = true;
        XWidget_repaint((XWidget*)self);
        XAbstractButton_pressed_signal(self);
    }

    abstractbutton_stopRepeatTimer(self);
    self->m_down = false;
    self->m_pressed = false;
    XWidget_repaint((XWidget*)self);
    /*
     * 对标 QAbstractButtonPrivate::click（qabstractbutton.cpp:344-375）：
     * changeState 闸——独占组内当前选中项点击时**不进入** nextCheckState，
     * 状态与 toggled 都不变（组内最后一颗不能被点掉）。非独占/无组时
     * nextCheckState 无条件分派，二态由基类默认实现判 checkable，
     * QToolButton 之类「动作承担翻转」的派生类才能拿到回调。
     */
    if (!abstractbutton_isExclusiveChecked(self))
        XAbstractButton_nextCheckState_base(self);
    XAbstractButton_released_signal(self);
    XAbstractButton_clicked_signal(self, self->m_checked);
}

/* ==================== 公开信号 ==================== */

void* XAbstractButton_pressed_signal(XAbstractButton* self)
{
    if (!self)
        return (void*)(size_t)XAbstractButton_pressed_signal;
    abstractbutton_emitVoid(self, (size_t)XAbstractButton_pressed_signal);
    return (void*)(size_t)XAbstractButton_pressed_signal;
}

void* XAbstractButton_released_signal(XAbstractButton* self)
{
    if (!self)
        return (void*)(size_t)XAbstractButton_released_signal;
    abstractbutton_emitVoid(self, (size_t)XAbstractButton_released_signal);
    return (void*)(size_t)XAbstractButton_released_signal;
}

void* XAbstractButton_clicked_signal(XAbstractButton* self, bool checked)
{
    if (!self)
        return (void*)(size_t)XAbstractButton_clicked_signal;
    abstractbutton_emitBool(self, (size_t)XAbstractButton_clicked_signal,
                            checked);
    return (void*)(size_t)XAbstractButton_clicked_signal;
}

void* XAbstractButton_toggled_signal(XAbstractButton* self, bool checked)
{
    if (!self)
        return (void*)(size_t)XAbstractButton_toggled_signal;
    abstractbutton_emitBool(self, (size_t)XAbstractButton_toggled_signal,
                            checked);
    return (void*)(size_t)XAbstractButton_toggled_signal;
}

/* ==================== 助记符文本（对标 Qt TextShowMnemonic/QKeySequence::mnemonic） ==================== */

/** @brief 取 UTF-8 首字节对应的序列长度（非法首字节按 1 字节推进）。 */
static size_t abstractbutton_utf8SequenceLength(unsigned char lead)
{
    if (lead < 0x80u)
        return 1u;
    if ((lead & 0xE0u) == 0xC0u)
        return 2u;
    if ((lead & 0xF0u) == 0xE0u)
        return 3u;
    if ((lead & 0xF8u) == 0xF0u)
        return 4u;
    return 1u;
}

/**
 * @brief 生成显示文本（Qt TextShowMnemonic 的等价）。
 * @details 逐字节扫描 UTF-8：`&&` 折叠为一个字面 `&`；单个 `&`（包括
 *          行尾孤立的一个）是助记符标记，从显示文本中移除。本机 Qt 6.8
 *          探针（QFontMetrics::size(Qt::TextShowMnemonic)）实测：
 *          `&File`≈`File`、`&&`≈`&`、`File&`≈`File`、`A&&B`≈`A&B`。
 *          多字节序列字节均 ≥0x80，与 `&`（0x26）不冲突，按字节剔除
 *          不会切断 UTF-8 序列。
 * @return 新建 XString（调用方拥有）；输入为空返回 NULL。
 */
static XString* abstractbutton_stripMnemonic(const char* utf8)
{
    size_t length;
    size_t input;
    size_t output = 0;
    char* buffer;
    XString* result;

    if (!utf8)
        return NULL;
    length = XStrlen(utf8);
    buffer = (char*)XMemory_malloc(length + 1,
                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (!buffer)
        return NULL;
    for (input = 0; input < length;) {
        if (utf8[input] == '&') {
            if (input + 1 < length && utf8[input + 1] == '&') {
                buffer[output++] = '&';
                input += 2;
            } else {
                ++input; /* 单个 '&'（含行尾）是助记符标记，不显示。 */
            }
            continue;
        }
        buffer[output++] = utf8[input++];
    }
    buffer[output] = '\0';
    result = XString_create_utf8(buffer);
    XFree_System(buffer);
    return result;
}

/**
 * @brief 从文本提取助记符快捷键文本（Qt QKeySequence::mnemonic 的等价）。
 * @details qkeysequence.cpp:935-967：扫描 `&`，`&&` 跳过；首个「`&` +
 *          可打印字符」生成 `Alt+<字符>`（字符转大写），后续重复出现
 *          在 release 构建里被忽略（Qt 只在 debug 打警告）。规则与本机
 *          Qt 6.8 探针一致：`&File`→`Alt+F`、`&&File`→空、
 *          `File&`→空、`a&&b&c`→`Alt+C`、`&&&A`→`Alt+A`。
 * @return 新建 "Alt+X" XString（调用方拥有）；无助记符返回 NULL。
 */
static XString* abstractbutton_mnemonicFromText(const XString* text)
{
    const char* utf8;
    size_t input = 0;
    char result[12]; /* "Alt+" + 最多 4 字节 UTF-8 + NUL。 */

    if (!text)
        return NULL;
    utf8 = XString_toUtf8(text);
    if (!utf8)
        return NULL;
    while (utf8[input] != '\0') {
        size_t length;

        if (utf8[input] != '&') {
            input += abstractbutton_utf8SequenceLength(
                (unsigned char)utf8[input]);
            continue;
        }
        if (utf8[input + 1] == '&') {
            input += 2;
            continue;
        }
        if (utf8[input + 1] == '\0')
            break;
        length = abstractbutton_utf8SequenceLength(
            (unsigned char)utf8[input + 1]);
        if ((unsigned char)utf8[input + 1] < 0x20u) {
            /* 控制字符不是可打印助记符（Qt isPrint 判定）；跳过这个
             * '&' 继续扫描，注意推进游标以免原地死循环。 */
            ++input;
            continue;
        }
        XMemcpy(result, "Alt+", 4);
        XMemcpy(result + 4, utf8 + input + 1, length);
        if (length == 1 && result[4] >= 'a' && result[4] <= 'z')
            result[4] = (char)(result[4] - 'a' + 'A');
        result[4 + length] = '\0';
        return XString_create_utf8(result);
    }
    return NULL;
}

const char* XAbstractButton_displayTextUtf8(const XAbstractButton* self,
                                            XString** storage)
{
    const char* utf8;
    XString* stripped;

    if (storage)
        *storage = NULL;
    if (!self || !self->m_text)
        return NULL;
    utf8 = XString_toUtf8(self->m_text);
    if (!utf8 || !XStrchr(utf8, '&'))
        return utf8;
    stripped = abstractbutton_stripMnemonic(utf8);
    if (!stripped)
        return utf8; /* 分配失败：退化为带 '&' 的原文，不崩。 */
    if (!storage) {
        /* 必须能回传所有权；无出参时释放并回退原文（调用点都会传）。 */
        XClassDelete((XClass*)stripped);
        return utf8;
    }
    *storage = stripped;
    return XString_toUtf8(stripped);
}

/* ==================== 公开属性 ==================== */

const XString* XAbstractButton_text(const XAbstractButton* self)
{
    return self ? self->m_text : NULL;
}

void XAbstractButton_setText(XAbstractButton* self, const XString* text)
{
    XString* copy;
    XString* mnemonic;

    if (!self)
        return;
    if (self->m_text && text &&
        XString_equals(self->m_text, text, XChar_CaseSensitive)) {
        return;
    }

    copy = text ? XString_create_copy(text) : XString_create();
    if (!copy)
        return;
    if (self->m_text)
        XClassDelete((XClass*)self->m_text);
    self->m_text = copy;
    /*
     * 对标 Qt 6.8 QAbstractButton::setText（qabstractbutton.cpp:482-506）：
     * 文本生效后用 QKeySequence::mnemonic(text) 生成助记符并调
     * setShortcut——**文本没有 '&' 时助记符为空，会把既有快捷键清掉**。
     * 本库 XShortcut 注册表只按单键码匹配、不承载修饰位，助记符是
     * Alt+<字符>，按裸键登记会让普通字母键误触按钮（比不注册更糟），
     * 故助记符只写 shortcut 文本、不登记绑定；Alt+键的激活路径待
     * XShortcut/QKeySequence 子系统补修饰键后接上（见头文件 @note）。
     */
    mnemonic = abstractbutton_mnemonicFromText(copy);
    abstractbutton_setShortcutInternal(self, mnemonic, false);
    if (mnemonic)
        XClassDelete((XClass*)mnemonic);
    abstractbutton_refresh(self);
}

void XAbstractButton_setText_2(XAbstractButton* self, const char* utf8)
{
    XString* text;

    if (!self)
        return;
    text = XString_create_utf8(utf8 ? utf8 : "");
    if (!text)
        return;
    XAbstractButton_setText(self, text);
    XClassDelete((XClass*)text);
}

XIcon XAbstractButton_icon(const XAbstractButton* self)
{
    XIcon icon;

    XIcon_init(&icon);
    if (self)
        XClassCopy(&icon, &self->m_icon);
    return icon;
}

void XAbstractButton_setIcon(XAbstractButton* self, const XIcon* icon)
{
    if (!self || icon == &self->m_icon)
        return;

    XClassDeinit(&self->m_icon);
    if (icon && !XIcon_isNull(icon))
        XClassCopy(&self->m_icon, icon);
    abstractbutton_refresh(self);
}

XSize XAbstractButton_iconSize(const XAbstractButton* self)
{
    XSize size;

    XSize_init(&size, 0, 0);
    if (self)
        size = self->m_iconSize;
    /* [Qt 对齐 D1 2026-10-30] 未显式设置时返回样式生效值而非 (0,0)
     * （qabstractbutton.cpp:1234-1241：iconSize 无效时取
     * style()->pixelMetric(PM_ButtonIconSize)，桌面通常 16x16）——
     * (0,0) 会让调用方按「无图标尺寸」分支误判。 */
    if (size.width == 0 && size.height == 0 && XWidget_style((const XWidget*)self) != NULL) {
        int pm = XStyle_pixelMetric(XWidget_style((const XWidget*)self),
                                    XStylePM_ButtonIconSize, NULL);
        if (pm > 0)
            XSize_init(&size, pm, pm);
    }
    return size;
}

void XAbstractButton_setIconSize(XAbstractButton* self, const XSize* size)
{
    XSize value;

    if (!self)
        return;
    XSize_init(&value, 0, 0);
    if (size && size->width > 0 && size->height > 0)
        value = *size;
    if (self->m_iconSize.width == value.width &&
        self->m_iconSize.height == value.height) {
        return;
    }
    self->m_iconSize = value;
    abstractbutton_refresh(self);
}

bool XAbstractButton_isCheckable(const XAbstractButton* self)
{
    return self ? self->m_checkable : false;
}

void XAbstractButton_setCheckable(XAbstractButton* self, bool checkable)
{
    if (!self || self->m_checkable == checkable)
        return;

    self->m_checkable = checkable;
    /*
     * 对标 Qt 6.8.3 qabstractbutton.cpp:560-568：无条件置 checked=false
     * （不区分开关方向），不回调 checkStateSet 虚槽、不发 toggled
     * （Qt 的 setCheckable 只改私有位；中间态同步由下次 setChecked 的
     * 早退路径补做）。
     */
    self->m_checked = false;
    XWidget_update((XWidget*)self);
}

bool XAbstractButton_isChecked(const XAbstractButton* self)
{
    return self ? self->m_checked : false;
}

void XAbstractButton_setChecked(XAbstractButton* self, bool checked)
{
    bool old;

    if (!self)
        return;
    old = self->m_checked;
    /*
     * 对标 Qt 6.8.3 qabstractbutton.cpp:584-593：!checkable 与新旧值
     * 相同两条早退路径都先走 checkStateSet() 虚槽再返回，让派生类同步
     * 中间态（QCheckBox 借此清 noChange 并按 publishedState 去重发布
     * checkStateChanged；blockRefresh 门控由 XCheckBox 槽内自持的
     * m_blockRefresh 承担，与 Qt 把标志放在 QAbstractButtonPrivate 的
     * 净效果一致）。互斥组 tracked 反选拒绝的守卫早退仍静默返回，
     * 不回调虚槽（同 Qt）。
     */
    if (!self->m_checkable || old == checked) {
        XAbstractButton_checkStateSet_base(self);
        return;
    }
    if (!checked && old) {
        /* 对标 Qt：互斥组内**当前选中**按钮不可经 setChecked(false)
           反选（qabstractbutton.cpp:593-604 的 queryCheckedButton 守卫；
           守卫只保护组 tracked 的 checkedButton，其余成员可正常取消，
           组互斥取消正依赖此路径）。组承载与 autoExclusive 两种形态
           合并到 abstractbutton_isExclusiveChecked（同 Qt 一处判定）。 */
        if (abstractbutton_isExclusiveChecked(self))
            return;
    }

    self->m_checked = checked;
    XAbstractButton_checkStateSet_base(self);
    XWidget_update((XWidget*)self);
    if (checked)
        abstractbutton_notifyChecked(self);
    XAbstractButton_toggled_signal(self, checked);
#if XWINDOW_ON && XACCESSIBLE_ON
    /* [无障碍补全 2026-10-07] 对标 QAccessibleStateChangeEvent（Qt 每次
     * setChecked 后通知选中态变更）；通道由 m_active 门控幂等。 */
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_StateChanged,
                                        (XWidget*)self);
#endif
}

void XAbstractButton_toggle(XAbstractButton* self)
{
    /*
     * 对标 Qt 6.8.3 qabstractbutton.cpp:843-851：toggle() 是非虚槽，
     * 直接 setChecked(!checked)（QCheckBox 未重载 toggle，三态复选框
     * 的 toggle 只在 Unchecked↔Checked 间跳变）；(state+1)%3 三态循环
     * 仅属于 nextCheckState 虚槽（点击/自动重复路径，qcheckbox.cpp:
     * 347-356）。不可选中按钮由 setChecked 内部早退拦截。
     */
    if (!self)
        return;
    XAbstractButton_setChecked(self, !self->m_checked);
}

bool XAbstractButton_isDown(const XAbstractButton* self)
{
    return self ? self->m_down : false;
}

void XAbstractButton_setDown(XAbstractButton* self, bool down)
{
    if (!self || self->m_down == down)
        return;

    self->m_down = down;
    XWidget_repaint((XWidget*)self);
    if (down && self->m_autoRepeat)
        abstractbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!down)
        abstractbutton_stopRepeatTimer(self);
}

bool XAbstractButton_autoRepeat(const XAbstractButton* self)
{
    return self ? self->m_autoRepeat : false;
}

void XAbstractButton_setAutoRepeat(XAbstractButton* self, bool repeat)
{
    if (!self || self->m_autoRepeat == repeat)
        return;

    self->m_autoRepeat = repeat;
    if (repeat && self->m_down)
        abstractbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!repeat)
        abstractbutton_stopRepeatTimer(self);
}

int XAbstractButton_autoRepeatDelay(const XAbstractButton* self)
{
    return self ? self->m_autoRepeatDelay : 0;
}

void XAbstractButton_setAutoRepeatDelay(XAbstractButton* self, int delay)
{
    if (self)
        self->m_autoRepeatDelay = delay;
}

int XAbstractButton_autoRepeatInterval(const XAbstractButton* self)
{
    return self ? self->m_autoRepeatInterval : 0;
}

void XAbstractButton_setAutoRepeatInterval(XAbstractButton* self,
                                           int interval)
{
    if (self)
        self->m_autoRepeatInterval = interval;
}

bool XAbstractButton_autoExclusive(const XAbstractButton* self)
{
    return self ? self->m_autoExclusive : false;
}

void XAbstractButton_setAutoExclusive(XAbstractButton* self, bool exclusive)
{
    if (self)
        self->m_autoExclusive = exclusive;
}

/* ==================== 快捷键与按钮组 ==================== */

/**
 * @brief 设置快捷键文本并按需登记绑定（公共 setShortcut 与 setText 助记符共用）。
 * @param registerBinding true = 向 XShortcut 注册表单键绑定；false = 只写
 *        承载（文本助记符 Alt+键 因注册表无修饰位而不登记）。
 * @details 同文本早退时若调用方要求登记且当前**没有**绑定（例如先由
 *          setText 写了 mnemonic 文本、随后显式 setShortcut 同一文本），
 *          仍补一次 attach——Qt 的 setShortcut 每次都 release+grab。
 */
static void abstractbutton_setShortcutInternal(XAbstractButton* self,
                                               const XString* shortcut,
                                               bool registerBinding)
{
    XString* copy;

    if (!self)
        return;
    if (self->m_shortcut && shortcut &&
        XString_equals(self->m_shortcut, shortcut, XChar_CaseSensitive)) {
        if (registerBinding && !self->m_shortcutBinding)
            abstractbutton_shortcutAttach(self);
        return;
    }

    copy = shortcut ? XString_create_copy(shortcut) : NULL;
    if (shortcut && !copy)
        return; /* 分配失败保持原快捷键文本不变。 */
    abstractbutton_shortcutDetach(self);
    if (self->m_shortcut)
        XClassDelete((XClass*)self->m_shortcut);
    self->m_shortcut = copy;
    if (registerBinding && copy)
        abstractbutton_shortcutAttach(self);
}

void XAbstractButton_setShortcut(XAbstractButton* self,
                                 const XString* shortcut)
{
    /*
     * 对标 Qt 6.8.3 qabstractbutton.cpp:536-545：setShortcut 同时
     * grabShortcut 注册全局快捷键，按键命中激活时触发按钮
     * （qabstractbutton.cpp:917-929 走 animateClick；本实现 activated
     * →同步 click()，适配无定时器循环的无头/嵌入式环境）。注册表为
     * 单键码模型：**只有无修饰位的单键文本**（"S"/"F5"）才登记绑定；
     * 含修饰位的组合（"Ctrl+S"）只保存文本承载、不登记任何绑定——
     * 把末段当裸键登记会让普通 S 键误触按钮（见
     * abstractbutton_shortcutKeyFromUtf8 的 @note）。QKeySequence 的
     * 修饰位语义待 XShortcut 注册表扩展。
     */
    abstractbutton_setShortcutInternal(self, shortcut, true);
}

void XAbstractButton_setShortcut_2(XAbstractButton* self, const char* utf8)
{
    XString* shortcut;

    if (!self)
        return;
    if (!utf8) {
        XAbstractButton_setShortcut(self, NULL);
        return;
    }
    shortcut = XString_create_utf8(utf8);
    if (!shortcut)
        return;
    XAbstractButton_setShortcut(self, shortcut);
    XClassDelete((XClass*)shortcut);
}

const XString* XAbstractButton_shortcut(const XAbstractButton* self)
{
    return self ? self->m_shortcut : NULL;
}

void* XAbstractButton_group(const XAbstractButton* self)
{
    /*
     * 对标 QAbstractButton::group()：返回登记该按钮的 QButtonGroup。
     * 以 void* 不透明承载；XButtonGroup_addButton 时回写、
     * removeButton 时清空。
     */
    return self ? self->m_group : NULL;
}

void XAbstractButton_click(XAbstractButton* self)
{
    abstractbutton_clickInternal(self, true);
}

void XAbstractButton_animateClick(XAbstractButton* self)
{
    if (!self || !XWidget_isEnabled((XWidget*)self))
        return;

    /* [Qt 对齐 D5 2026-10-30] Qt：checkable 且 ClickFocus 策略的按钮先
     * setFocus（qabstractbutton.cpp:805-806：
     *   if (d->checkable && focusPolicy() & Qt::ClickFocus) setFocus();）
     * ——组内互斥选中需焦点随点击迁移，缺它则 animateClick 后焦点仍留
     * 原控件，Tab 序与焦点高亮错位。 */
    if (self->m_checkable &&
        ((XWidget*)self)->m_focusPolicy & XWidgetFocusPolicy_ClickFocus)
        XWidget_setFocus((XWidget*)self);

    XAbstractButton_setDown(self, true);
    if (self->m_animateTimer == XTIMER_INVALID_ID)
        XAbstractButton_pressed_signal(self);
    abstractbutton_startAnimateTimer(self);
}

/* ==================== 保护虚函数 ==================== */

static void VXAbstractButton_checkStateSet(XAbstractButton* self)
{
    (void)self;
}

void XAbstractButton_checkStateSet_base(XAbstractButton* self)
{
    XAbstractButtonCheckStateSetSlot slot;

    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") ||
        XVtable_size(XClassGetVtable(self)) <=
            EXAbstractButton_CheckStateSet) {
        return;
    }
    slot = XClassGetVirtualFunc(self, EXAbstractButton_CheckStateSet,
                                XAbstractButtonCheckStateSetSlot);
    if (slot)
        slot(self);
}

void XAbstractButton_nextCheckState(XAbstractButton* self)
{
    if (!self || !self->m_checkable)
        return;
    XAbstractButton_setChecked(self, !self->m_checked);
}

void XAbstractButton_nextCheckState_base(XAbstractButton* self)
{
    XAbstractButtonNextCheckStateSlot slot;

    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") ||
        XVtable_size(XClassGetVtable(self)) <=
            EXAbstractButton_NextCheckState) {
        return;
    }
    slot = XClassGetVirtualFunc(self, EXAbstractButton_NextCheckState,
                                XAbstractButtonNextCheckStateSlot);
    if (slot)
        slot(self);
}

bool XAbstractButton_hitButton(const XAbstractButton* self,
                               const XPoint* pos)
{
    XRect rect;

    if (!self || !pos)
        return false;
    rect = XWidget_rect((const XWidget*)self);
    return XRect_contains(&rect, pos->x, pos->y);
}

bool XAbstractButton_hitButton_base(const XAbstractButton* self,
                                    const XPoint* pos)
{
    XAbstractButtonHitButtonSlot slot;

    if (ISNULL(self, "") || !pos || ISNULL(XClassGetVtable(self), "") ||
        XVtable_size(XClassGetVtable(self)) <= EXAbstractButton_HitButton) {
        return false;
    }
    slot = XClassGetVirtualFunc(self, EXAbstractButton_HitButton,
                                XAbstractButtonHitButtonSlot);
    return slot ? slot(self, pos) : false;
}

static void VXAbstractButton_contentChanged(XAbstractButton* self)
{
    (void)self;
}

void XAbstractButton_contentChanged_base(XAbstractButton* self)
{
    XAbstractButtonContentChangedSlot slot;

    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "") ||
        XVtable_size(XClassGetVtable(self)) <=
            EXAbstractButton_ContentChanged) {
        return;
    }
    slot = XClassGetVirtualFunc(self, EXAbstractButton_ContentChanged,
                                XAbstractButtonContentChangedSlot);
    if (slot)
        slot(self);
}

/* ==================== XWidget 事件虚槽 ==================== */

static bool abstractbutton_ignoreDisabledEvent(XWidget* self, XEvent* event)
{
    XEventType type;

    if (!self || XWidget_isEnabled(self))
        return false;
    type = event ? XEvent_type(event) : XEVENT_TYPE_NONE;
    switch (type) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
    case XEVENT_TYPE_MOUSE_MOVE:
    case XEVENT_TYPE_CONTEXT_MENU:
    case XEVENT_TYPE_HOVER_ENTER:
    case XEVENT_TYPE_HOVER_LEAVE:
    case XEVENT_TYPE_HOVER_MOVE:
        if (event)
            XEvent_accept(event);
        return true;
    default:
        return false;
    }
}

static bool VXAbstractButton_event(XWidget* self, XEvent* event)
{
    if (abstractbutton_ignoreDisabledEvent(self, event))
        return true;
    return XClass_Parent(XWidget, EXObject_Event,
                         bool(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXAbstractButton_changeEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button;
    XEventType type;

    if (!self || !event)
        return;
    button = (XAbstractButton*)self;
    type = XEvent_type(event);
    if (type == XEVENT_TYPE_ENABLED_CHANGE &&
        !XWidget_isEnabled(self) && button->m_down) {
        button->m_pressed = false;
        abstractbutton_setDownFromInput(button, false);
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXAbstractButton_mousePressCommon(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XMouseEvent* mouseEvent;
    XPoint position;

    if (!button || !event)
        return;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) {
        return;
    }

    mouseEvent = (XMouseEvent*)event;
    if (XMouseEvent_button(mouseEvent) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }

    position = XMouseEvent_position(mouseEvent);
    if (XAbstractButton_hitButton_base(button, &position)) {
        /*
         * 点击移焦（隔夜台账猎获④：此前点击按钮焦点原位、按 t 仍进
         * 原编辑框，仅 Shift+Tab 能移焦）。对标 Qt 6.8.3：自发鼠标
         * 按压在事件分发前经 QApplicationPrivate::
         * giveFocusAccordingToFocusPolicy 以 Qt::MouseFocusReason 移焦
         * （qapplication.cpp:2734-2737 调用点 → 3661 实现，Press/
         * DblClick 分支），focusInEvent 先于 mousePressEvent；
         * QPushButton 默认策略 = SH_Button_FocusPolicy =
         * Qt::StrongFocus（0xB，含 ClickFocus 位；
         * qabstractbutton.cpp:329 + qcommonstyle.cpp:5224-5226），本类
         * init 已对齐。判定按任务口径"NoFocus 跳过"（Qt 的
         * ClickFocus 位判定对 StrongFocus 结果等价）；
         * XWidget_setFocus_2 对禁用态有门禁，与 Qt 禁用控件不收
         * 鼠标输入一致。点中按钮之外的区域不聚焦（hitButton 未命中
         * 不进入本分支），保留"点空白不移焦"的 Qt 语义。
         */
        if (XWidget_focusPolicy(self) != XWidgetFocusPolicy_NoFocus)
            XWidget_setFocus_2(self, XFocusReason_Mouse);
        button->m_pressed = true;
        abstractbutton_setDownFromInput(button, true);
        /* [不再显式 grabMouse] 「按住拖出按钮仍保持按下」由框架的**隐式
         * 抓取**（g_buttonDownWidget = Qt 的 qt_button_down）保证：按下时
         * 登记按压靶，其后的 MOVE/RELEASE 一律改投该靶（见 XWidget.c 的
         * 事件分派）。
         * [为什么撤掉早先的显式抓取] 显式抓取是**全局单抓取者**语义，且
         * 只有真正收到 RELEASE 时才会被释放——按钮在按住期间被禁用
         * （changeEvent → setDownFromInput(false)）时不会走到释放分支，
         * 抓取就**留在原地**：此后所有鼠标事件都被投给那个已禁用的按钮，
         * 界面整体"点不动"（现场症状：点动后连停止按钮都按不动）。
         * 隐式抓取没有这个问题：RELEASE 一定回到按压靶并当场清除。 */
        XEvent_accept(event);
        return;
    }

    button->m_pressed = false;
    if (button->m_down)
        abstractbutton_setDownFromInput(button, false);
    XEvent_ignore(event);
}

static void VXAbstractButton_mousePressEvent(XWidget* self, XEvent* event)
{
    VXAbstractButton_mousePressCommon(self, event);
}

static void VXAbstractButton_mouseDoubleClickEvent(XWidget* self,
                                                    XEvent* event)
{
    VXAbstractButton_mousePressCommon(self, event);
}

static void VXAbstractButton_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XMouseEvent* mouseEvent;
    XPoint position;

    if (!button || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        return;
    }

    mouseEvent = (XMouseEvent*)event;
    if (XMouseEvent_button(mouseEvent) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }

    button->m_pressed = false;
    abstractbutton_stopRepeatTimer(button);
    if (!button->m_down) {
        XEvent_ignore(event);
        return;
    }

    position = XMouseEvent_position(mouseEvent);
    if (XAbstractButton_hitButton_base(button, &position)) {
        /*
         * 按下阶段已经发射 pressed；释放阶段只完成 nextCheckState、
         * released 和 clicked。
         */
        abstractbutton_clickInternal(button, false);
        XEvent_accept(event);
        return;
    }

    abstractbutton_setDownFromInput(button, false);
    XEvent_ignore(event);
}

static void VXAbstractButton_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XMouseEvent* mouseEvent;
    XPoint position;
    bool hit;

    if (!button || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;

    mouseEvent = (XMouseEvent*)event;
    if ((XMouseEvent_buttons(mouseEvent) & XMouseButton_LeftButton) == 0 ||
        !button->m_pressed) {
        XEvent_ignore(event);
        return;
    }

    position = XMouseEvent_position(mouseEvent);
    hit = XAbstractButton_hitButton_base(button, &position);
    /* [Qt 口径：移出不释放] 对标 QAbstractButton：按下后即使鼠标拖出按钮，
     * 按钮**仍保持按下态**（pressed 已发），直到松手才发 released；
     * "松手时鼠标不在按钮内" ⇒ 不发 clicked（由 mouseReleaseEvent 的
     * hitButton 判定负责）。
     * 原实现在这里调 setDownFromInput(false) —— 它会**立刻发 released**，
     * 于是"按住不动、鼠标轻微抖动出按钮"就被当成松手 ⇒ 现场症状：点动
     * 按钮长按，电机随即停（服务端还没到看门狗超时就先收到了点动释放）。
     * 现在移出只保持按下态、不动信号。 */
    if (hit != button->m_down) {
        /* 视觉上仍按下（Qt 口径），故不 repaint 状态；仅吞掉事件。 */
        XEvent_accept(event);
        return;
    }

    if (hit)
        XEvent_accept(event);
    else
        XEvent_ignore(event);
}

static void VXAbstractButton_keyPressEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XKeyEvent* keyEvent;
    int key;

    if (!button || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS)
        return;

    keyEvent = (XKeyEvent*)event;
    key = XKeyEvent_key(keyEvent);
    /*
     * 对标 QAbstractButton::keyPressEvent（qabstractbutton.cpp:1007-1079）：
     * ① 平台按压键集合 {Key_Space, Key_Select}（非自动重复）先进入
     *    down+pressed；② 方向键在互斥组内移焦 / 组外走焦点遍历；
     * ③ Cancel（Escape）在按下态只释放、不点击；其余按键 ignore 上抛。
     */
    if (XAbstractButton_isButtonPressKey(key) &&
        !XKeyEvent_autoRepeat(keyEvent)) {
        button->m_pressed = true;
        abstractbutton_setDownFromInput(button, true);
        XEvent_accept(event);
        return;
    }
    switch (key) {
    case XKey_Up:
    case XKey_Left:
    case XKey_Right:
    case XKey_Down:
        if (abstractbutton_handleArrowKey(button, key))
            XEvent_accept(event);
        else
            XEvent_ignore(event);
        return;
    default:
        break;
    }
    if (key == XKey_Escape && button->m_down) {
        button->m_pressed = false;
        abstractbutton_setDownFromInput(button, false);
        XEvent_accept(event);
        return;
    }

    XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXAbstractButton_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XKeyEvent* keyEvent;

    if (!button || !event || XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE)
        return;

    keyEvent = (XKeyEvent*)event;
    /*
     * 对标 QAbstractButton::keyReleaseEvent（qabstractbutton.cpp:1081-1099）：
     * 任意非自动重复的按键释放都先停自动重复表，再判是否为「按压键」
     * （{Key_Space, Key_Select}，同 keyPressEvent）；否则按住 Space 的
     * 自动重复释放会把重复定时器漏留着。
     */
    if (!XKeyEvent_autoRepeat(keyEvent))
        abstractbutton_stopRepeatTimer(button);
    if (XAbstractButton_isButtonPressKey(XKeyEvent_key(keyEvent)) &&
        !XKeyEvent_autoRepeat(keyEvent) && button->m_down) {
        button->m_pressed = false;
        abstractbutton_clickInternal(button, false);
        XEvent_accept(event);
        return;
    }

    XEvent_ignore(event);
}

static void VXAbstractButton_focusInEvent(XWidget* self, XEvent* event)
{
    if (self && event) {
        XClass_Parent(XWidget, EXWidget_FocusInEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    }
}

static void VXAbstractButton_focusOutEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XFocusReason reason = XFocusReason_Other;
    bool popupFocus = false;

    if (!self || !event)
        return;
#if XWINDOWEVENT_ON
    if (XFocusEvent_lostFocus((const XFocusEvent*)event))
        reason = XFocusEvent_reason((const XFocusEvent*)event);
#endif /* XWINDOWEVENT_ON */

    popupFocus = reason == XFocusReason_Popup;
    if (button && button->m_down && !popupFocus) {
        button->m_pressed = false;
        abstractbutton_setDownFromInput(button, false);
    }
    XClass_Parent(XWidget, EXWidget_FocusOutEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== XClass 生命周期虚槽 ==================== */

static void VXAbstractButton_copy(XAbstractButton* self,
                                  const XAbstractButton* other)
{
    if (!self || !other || self == other)
        return;

    abstractbutton_stopRepeatTimer(self);
    abstractbutton_stopAnimateTimer(self);
    if (XClassIsVtableNull(self))
        XAbstractButton_init(self, NULL, 0);

    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))(
                      (XWidget*)self, (const XWidget*)other);
    if (self->m_text) {
        XClassDelete((XClass*)self->m_text);
        self->m_text = NULL;
    }
    XClassDeinit(&self->m_icon);
    self->m_text = other->m_text ? XString_create_copy(other->m_text)
                                 : XString_create();
    XClassCopy(&self->m_icon, &other->m_icon);
    self->m_iconSize = other->m_iconSize;
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
    self->m_down = other->m_down;
    self->m_pressed = other->m_pressed;
    self->m_autoRepeat = other->m_autoRepeat;
    self->m_autoExclusive = other->m_autoExclusive;
    self->m_autoRepeatDelay = other->m_autoRepeatDelay;
    self->m_autoRepeatInterval = other->m_autoRepeatInterval;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;
    /* 快捷键文本为对象拥有，深拷贝；组承载与快捷键绑定均不随拷贝
     * 继承（对齐 Qt：新按钮不属于任何 QButtonGroup，也无 grabShortcut
     * 登记）。 */
    if (self->m_shortcut) {
        XClassDelete((XClass*)self->m_shortcut);
        self->m_shortcut = NULL;
    }
    self->m_shortcut = other->m_shortcut
                           ? XString_create_copy(other->m_shortcut)
                           : NULL;
    self->m_shortcutBinding = NULL;
    self->m_group = NULL;
}

static void VXAbstractButton_move(XAbstractButton* self,
                                  XAbstractButton* other)
{
    if (!self || !other || self == other)
        return;

    abstractbutton_stopRepeatTimer(self);
    abstractbutton_stopAnimateTimer(self);
    abstractbutton_stopRepeatTimer(other);
    abstractbutton_stopAnimateTimer(other);
    if (XClassIsVtableNull(self))
        XAbstractButton_init(self, NULL, 0);

    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                                (XWidget*)other);
    if (self->m_text) {
        XClassDelete((XClass*)self->m_text);
        self->m_text = NULL;
    }
    XClassDeinit(&self->m_icon);
    self->m_text = other->m_text;
    other->m_text = XString_create();
    XClassMove(&self->m_icon, &other->m_icon);
    self->m_iconSize = other->m_iconSize;
    XSize_init(&other->m_iconSize, 0, 0);
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
    self->m_down = other->m_down;
    self->m_pressed = other->m_pressed;
    self->m_autoRepeat = other->m_autoRepeat;
    self->m_autoExclusive = other->m_autoExclusive;
    self->m_autoRepeatDelay = other->m_autoRepeatDelay;
    self->m_autoRepeatInterval = other->m_autoRepeatInterval;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;
    /* 快捷键文本所有权随移动转移；快捷键绑定的 activated 连接以按钮
     * 对象为接收者，不可随移动换主，就地回收（与组承载同口径不继承）。
     */
    abstractbutton_shortcutDetach(self);
    abstractbutton_shortcutDetach(other);
    self->m_shortcut = other->m_shortcut;
    other->m_shortcut = NULL;
    self->m_group = NULL;

    other->m_checkable = false;
    other->m_checked = false;
    other->m_down = false;
    other->m_pressed = false;
    other->m_autoRepeat = false;
    other->m_autoExclusive = false;
    other->m_autoRepeatDelay = 300;
    other->m_autoRepeatInterval = 100;
    other->m_repeatTimer = XTIMER_INVALID_ID;
    other->m_animateTimer = XTIMER_INVALID_ID;
}

static void VXAbstractButton_deinit(XAbstractButton* self)
{
    if (!self)
        return;

    abstractbutton_stopRepeatTimer(self);
    abstractbutton_stopAnimateTimer(self);
    abstractbutton_shortcutDetach(self);
    if (self->m_text) {
        XClassDelete((XClass*)self->m_text);
        self->m_text = NULL;
    }
    if (self->m_shortcut) {
        XClassDelete((XClass*)self->m_shortcut);
        self->m_shortcut = NULL;
    }
    self->m_group = NULL;
    XClassDeinit(&self->m_icon);
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

static void VXAbstractButton_timerEvent(XObject* object,
                                        XTimerEvent* event)
{
    XAbstractButton* self = (XAbstractButton*)object;

    if (self && event &&
        XTimerEvent_timerId(event) == self->m_repeatTimer) {
        XTimerId timerId = self->m_repeatTimer;

        XObject_killTimer((XObject*)self, timerId);
        self->m_repeatTimer = XTIMER_INVALID_ID;
        if (self->m_down && self->m_autoRepeat)
            abstractbutton_startRepeatTimer(self,
                                            self->m_autoRepeatInterval);
        abstractbutton_repeatTimeout(self);
        XEvent_accept((XEvent*)event);
        return;
    }

    if (self && event &&
        XTimerEvent_timerId(event) == self->m_animateTimer) {
        XTimerId timerId = self->m_animateTimer;

        XObject_killTimer((XObject*)self, timerId);
        self->m_animateTimer = XTIMER_INVALID_ID;
        /*
         * 对标 QAbstractButton::timerEvent（qabstractbutton.cpp:1119-1121）：
         * 动画释放定时器到点即 d->click()——同一次点击收尾（含独占组
         * changeState 闸、down 复位、released/clicked），不要在这里重复
         * 一份收尾逻辑。
         */
        abstractbutton_clickInternal(self, false);
        XEvent_accept((XEvent*)event);
        return;
    }

    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 类初始化与生命周期 ==================== */

XVtable* XAbstractButton_class_init(void)
{
    void* protectedSlots[] = {
        VXAbstractButton_checkStateSet,
        XAbstractButton_nextCheckState,
        XAbstractButton_hitButton,
        VXAbstractButton_contentChanged
    };

    XVTABLE_INIT_DEFAULT(XAbstractButton)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(protectedSlots);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXAbstractButton_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent,
                             VXAbstractButton_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXAbstractButton_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXAbstractButton_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXAbstractButton_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXAbstractButton_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXAbstractButton_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent,
                             VXAbstractButton_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent,
                             VXAbstractButton_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent,
                             VXAbstractButton_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent,
                             VXAbstractButton_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractButton_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractButton_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractButton_deinit);
    return XVTABLE_DEFAULT;
}

void XAbstractButton_init(XAbstractButton* self, XWidget* parent,
                          XWidgetFlags flags)
{
    if (!self)
        return;

    XMemset(self, 0, sizeof(XAbstractButton));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XAbstractButton);
    self->m_text = XString_create();
    XIcon_init(&self->m_icon);
    XSize_init(&self->m_iconSize, 0, 0);
    self->m_autoRepeatDelay = 300;
    self->m_autoRepeatInterval = 100;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;
    /* 快捷键文本承载默认未设置（绑定未注册）；按钮组承载默认未登记。 */
    self->m_shortcut = NULL;
    self->m_shortcutBinding = NULL;
    self->m_group = NULL;
    XWidget_setForegroundRole((XWidget*)self,
                              XPaletteColorRole_ButtonText);
    XWidget_setBackgroundRole((XWidget*)self, XPaletteColorRole_Button);
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
    /* 悬停外观 opt-in（对标 Qt QFusionStyle::polish 的 WA_Hover 收
     * 口）：按钮族绘制按 State_MouseOver 出悬停高亮，ENTER/LEAVE 须
     * 标脏自矩形；未声明该位的控件不因指针进出重绘（见
     * VXWidget_event 的 ENTER/LEAVE 分支）。子类经本 init 继承。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);
}

XAbstractButton* XAbstractButton_create_ex(XMemoryType memory,
                                           XWidget* parent,
                                           XWidgetFlags flags)
{
    XAbstractButton* self = (XAbstractButton*)XMemory_malloc(
        sizeof(XAbstractButton), memory);

    if (!self)
        return NULL;
    XMemset(self, 0, sizeof(XAbstractButton));
    XAbstractButton_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON */

#endif /* XGUI_ON */
