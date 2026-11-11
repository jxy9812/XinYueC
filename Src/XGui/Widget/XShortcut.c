/**
 * @file       XShortcut.c
 * @brief      快捷键对象实现（对标 Qt 6.8 QShortcut 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；实现为对象状态 +
 *             模块静态全局注册表（XVector<XShortcut*>），注册/注销由
 *             create/delete 自动完成，XShortcut_match 供按键路径调用。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XShortcut.h"
#include "XWidget.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XVector.h"
#include "XGuiConfig.h"

#if XWIDGET_ON

/* ==================== 全局注册表 ==================== */

/** @brief 模块静态注册表（元素为 XShortcut* 借用指针）。 */
static XVector* s_shortcutRegistry = NULL;

/** @brief 惰性创建注册表。 */
static void xshortcut_ensureRegistry(void)
{
    if (s_shortcutRegistry) return;
    s_shortcutRegistry = XVector_Create(XShortcut*);
}

/** @brief 注册表内索引；未注册返回 -1。 */
static int xshortcut_indexOf(const XShortcut* self)
{
    int64_t i;
    int64_t n;
    if (!s_shortcutRegistry || !self) return -1;
    n = XVector_size_base((const XContainer*)s_shortcutRegistry);
    for (i = 0; i < n; ++i) {
        XShortcut** item =
            (XShortcut**)XVector_at_base(s_shortcutRegistry, i);
        if (item && *item == self) return (int)i;
    }
    return -1;
}

/** @brief 发射空参信号（args 传 NULL，对标 XEmitSignal 的空参模式）。 */
static void xshortcut_emitVoidSignal(XShortcut* self, size_t signal)
{
    if (!self) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_shortcut_deinit(XShortcut* self)
{
    if (!self) return;
    XShortcut_unregister(self);
    if (self->m_whatsThis) {
        XClassDelete((XClass*)self->m_whatsThis);
        self->m_whatsThis = NULL;
    }
    if (self->m_keys) {
        XClassDelete((XClass*)self->m_keys);
        self->m_keys = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XShortcut_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XShortcut)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_shortcut_deinit);
    return XVTABLE_DEFAULT;
}

void XShortcut_init(XShortcut* self, XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    if (parent)
        XObject_setParent((XObject*)self, parent);
    XClassSetVtable(self, XShortcut);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_enabled = true;
    self->m_autoRepeat = true;
    self->m_context = XShortcutContext_WindowShortcut;
    self->m_parentWidget = parent;
    self->m_keys = XVector_Create(int);
}

XShortcut* XShortcut_create_ex(XMemoryType memory, XObject* parent)
{
    XShortcut* self =
        (XShortcut*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XShortcut_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    XShortcut_register(self);
    return self;
}

XShortcut* XShortcut_create_2_ex(XMemoryType memory, int key,
                                 XObject* parent)
{
    XShortcut* self = XShortcut_create_ex(memory, parent);
    if (!self) return NULL;
    XShortcut_setKey(self, key);
    return self;
}

/* ==================== 属性（对标 QShortcut） ==================== */

void XShortcut_setKey(XShortcut* self, int key)
{
    XShortcut_setKeys(self, &key, 1);
}

int XShortcut_key(const XShortcut* self)
{
    return XShortcut_keyAt(self, 0);
}

void XShortcut_setKeys(XShortcut* self, const int* keys, int count)
{
    int i;
    if (!self) return;
    if (!self->m_keys) {
        self->m_keys = XVector_Create(int);
        if (!self->m_keys) return;
    }
    XVector_clear_base((XContainer*)self->m_keys);
    if (!keys || count <= 0)
        return;
    for (i = 0; i < count; ++i) {
        int key = keys[i];
        XVector_push_back_1_base(self->m_keys, &key);
    }
}

int XShortcut_keyCount(const XShortcut* self)
{
    if (!self || !self->m_keys) return 0;
    return (int)XVector_size_base((const XContainer*)self->m_keys);
}

int XShortcut_keyAt(const XShortcut* self, int index)
{
    int* key;
    if (!self || !self->m_keys || index < 0 ||
        (int64_t)index >= (int64_t)XVector_size_base(
                              (const XContainer*)self->m_keys))
        return 0;
    key = (int*)XVector_at_base(self->m_keys, index);
    return key ? *key : 0;
}

void XShortcut_setEnabled(XShortcut* self, bool enabled)
{
    if (!self) return;
    self->m_enabled = enabled;
}

bool XShortcut_isEnabled(const XShortcut* self)
{
    return self ? self->m_enabled : false;
}

void XShortcut_setContext(XShortcut* self, XShortcutContext context)
{
    if (!self) return;
    self->m_context = context;
}

XShortcutContext XShortcut_context(const XShortcut* self)
{
    return self ? self->m_context : XShortcutContext_WindowShortcut;
}

void XShortcut_setAutoRepeat(XShortcut* self, bool on)
{
    if (!self) return;
    self->m_autoRepeat = on;
}

bool XShortcut_autoRepeat(const XShortcut* self)
{
    return self ? self->m_autoRepeat : false;
}

void XShortcut_setWhatsThis(XShortcut* self, const XString* text)
{
    XString* copy;
    if (!self) return;
    copy = text ? XString_create_copy(text) : NULL;
    if (self->m_whatsThis)
        XClassDelete((XClass*)self->m_whatsThis);
    self->m_whatsThis = copy;
}

void XShortcut_setWhatsThis_2(XShortcut* self, const char* utf8)
{
    XString* text;
    if (!self) return;
    text = utf8 ? XString_create_utf8(utf8) : NULL;
    XShortcut_setWhatsThis(self, text);
    if (text)
        XClassDelete((XClass*)text);
}

XString* XShortcut_whatsThis(const XShortcut* self)
{
    if (!self || !self->m_whatsThis) return NULL;
    return XString_create_copy(self->m_whatsThis);
}

/* ==================== 全局注册表与按键匹配 ==================== */

void XShortcut_register(XShortcut* self)
{
    if (!self) return;
    xshortcut_ensureRegistry();
    if (!s_shortcutRegistry) return;
    if (xshortcut_indexOf(self) >= 0) return;
    XVector_push_back_1_base(s_shortcutRegistry, &self);
}

void XShortcut_unregister(XShortcut* self)
{
    int index;
    if (!s_shortcutRegistry || !self) return;
    index = xshortcut_indexOf(self);
    if (index < 0) return;
    XVector_remove_base(s_shortcutRegistry, index, 1);
}

/** @brief 序列表是否含指定键码（多序列任一命中即算，对标
 *         QShortcutMap 对 QShortcut::keys() 逐序列登记）。 */
static bool xshortcut_hasKey(const XShortcut* sc, int key)
{
    int64_t i;
    int64_t n;
    if (!sc || !sc->m_keys) return false;
    n = XVector_size_base((const XContainer*)sc->m_keys);
    for (i = 0; i < n; ++i) {
        int* item = (int*)XVector_at_base(sc->m_keys, i);
        if (item && *item == key) return true;
    }
    return false;
}

/** @brief context 过滤谓词（对标 QShortcutMap 的可达性判定）：
 *         Application 恒匹配；Window 限焦点控件与快捷键父级同顶层；
 *         WidgetWithChildren 限焦点控件为父级自身或其后代；Widget 仅
 *         父级自身。供 match 与歧义计数共用。 */
static bool xshortcut_accepts(const XShortcut* sc, int key,
                              const XWidget* focusWidget)
{
    const XWidget* w;
    if (!sc || !sc->m_enabled || !xshortcut_hasKey(sc, key)) return false;
    if (sc->m_context == XShortcutContext_ApplicationShortcut)
        return true;
    if (!focusWidget) return false;
    if (sc->m_context == XShortcutContext_WindowShortcut) {
        XWidget* shortcutTop = NULL;
        if (sc->m_parentWidget) {
            if (!((XObject*)sc->m_parentWidget)->m_is_widget)
                return true; /* 非控件父级：同 legacy 无主宽松口径。 */
            shortcutTop = XWidget_topLevelWidget(
                (XWidget*)sc->m_parentWidget);
        }
        /* 无父级的快捷键不限定窗口（legacy 宽松口径，回归 219a
         * 契约）；有父级时要求焦点控件同顶层（对标 Qt
         * WindowShortcut 的活动窗口语义）。 */
        if (!shortcutTop)
            return true;
        return XWidget_topLevelWidget((XWidget*)focusWidget) == shortcutTop;
    }
    if (sc->m_context == XShortcutContext_WidgetWithChildrenShortcut) {
        if (!sc->m_parentWidget) return false;
        w = focusWidget;
        while (w) {
            if (w == (const XWidget*)sc->m_parentWidget)
                return true;
            w = (const XWidget*)XObject_parent((const XObject*)w);
        }
        return false;
    }
    if (sc->m_context == XShortcutContext_WidgetShortcut &&
        (const XObject*)focusWidget == sc->m_parentWidget)
        return true;
    return false;
}

XShortcut* XShortcut_match(int key, XShortcutContext context,
                           XWidget* focusWidget)
{
    int64_t i;
    int64_t n;
    (void)context; /* 保留 Qt 语义扩展点：当前简化实现不使用该参数。 */
    if (!s_shortcutRegistry) return NULL;
    n = XVector_size_base((const XContainer*)s_shortcutRegistry);
    for (i = 0; i < n; ++i) {
        XShortcut** item =
            (XShortcut**)XVector_at_base(s_shortcutRegistry, i);
        if (!item) continue;
        if (xshortcut_accepts(*item, key, focusWidget)) return *item;
    }
    return NULL;
}

/** @brief 统计同键在焦点代理视角下的命中数（歧义判定用）。 */
static int xshortcut_countMatches(int key, const XWidget* focusWidget)
{
    int64_t i;
    int64_t n;
    int count = 0;
    if (!s_shortcutRegistry) return 0;
    n = XVector_size_base((const XContainer*)s_shortcutRegistry);
    for (i = 0; i < n; ++i) {
        XShortcut** item =
            (XShortcut**)XVector_at_base(s_shortcutRegistry, i);
        if (item && xshortcut_accepts(*item, key, focusWidget))
            ++count;
    }
    return count;
}

/** @brief 歧义重放的焦点代理：取本快捷键父控件（Widget/Window/
 *         WidgetWithChildren 三类上下文的可达性都以父级为锚，以父级
 *         代焦点重放与真实焦点判定一致；父级非控件/无父级返回 NULL，
 *         Application 类上下文不受焦点影响）。 */
static XWidget* xshortcut_focusProxy(const XShortcut* self)
{
    XObject* parent;
    if (!self) return NULL;
    parent = self->m_parentWidget;
    if (parent && parent->m_is_widget) return (XWidget*)parent;
    return NULL;
}

void XShortcut_activate(XShortcut* self)
{
    XWidget* focusProxy;
    int matches;
    int key;
    if (!self) return;
    /* 对标 QShortcutMap：禁用的快捷键不参与匹配，永远不会被派发
     * （qshortcutmap.cpp:373 `&& (*it).correctContext() && (*it).enabled`；
     * QShortcut::setEnabled 注释「An enabled shortcut emits the
     * activated()...」）。旧实现把"禁用导致 0 命中"归入无歧义分支，仍会
     * 对禁用对象发 activated —— 与 Qt 相反，此处直接不发射。 */
    if (!self->m_enabled)
        return;
    /* 对标 Qt QShortcutMap：同一键序列在同上下文命中多个启用快捷键时
     * 改派 activatedAmbiguously（对每个命中者发射）且不发 activated
     * （QShortcut::activatedAmbiguously 语义；此前本实现恒发
     * activated，歧义信号零发射点）。
     * @note 歧义按**首条**序列统计：activate 没有按键形参（Qt 的
     *       QShortcutMap 以按键事件为键），单序列（绝大多数用法）与 Qt
     *       一致；多序列见 setKeys。 */
    key = XShortcut_keyAt(self, 0);
    focusProxy = xshortcut_focusProxy(self);
    matches = xshortcut_countMatches(key, focusProxy);
    if (matches > 1) {
        int64_t i;
        int64_t n;
        n = XVector_size_base((const XContainer*)s_shortcutRegistry);
        for (i = 0; i < n; ++i) {
            XShortcut** item =
                (XShortcut**)XVector_at_base(s_shortcutRegistry, i);
            if (item && xshortcut_accepts(*item, key, focusProxy))
                xshortcut_emitVoidSignal(
                    *item, (size_t)XShortcut_activatedAmbiguously_signal);
        }
        return;
    }
    xshortcut_emitVoidSignal(self, (size_t)XShortcut_activated_signal);
}

/* ==================== 信号 ==================== */

void* XShortcut_activated_signal(XShortcut* self)
{
    (void)self;
    return (void*)(size_t)XShortcut_activated_signal;
}

void* XShortcut_activatedAmbiguously_signal(XShortcut* self)
{
    (void)self;
    return (void*)(size_t)XShortcut_activatedAmbiguously_signal;
}

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */
