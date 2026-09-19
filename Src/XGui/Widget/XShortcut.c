/**
 * @file       XShortcut.c
 * @brief      快捷键对象实现（对标 Qt 6.8 QShortcut 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；实现为对象状态 +
 *             模块静态全局注册表（XVector<XShortcut*>），注册/注销由
 *             create/delete 自动完成，XShortcut_match 供按键路径调用。
 * @author     XinYueC 团队
 */

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
        XString_delete_base((XClass*)self->m_whatsThis);
        self->m_whatsThis = NULL;
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
    self->m_key = key;
    return self;
}

/* ==================== 属性（对标 QShortcut） ==================== */

void XShortcut_setKey(XShortcut* self, int key)
{
    if (!self) return;
    self->m_key = key;
}

int XShortcut_key(const XShortcut* self)
{
    return self ? self->m_key : 0;
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
        XString_delete_base((XClass*)self->m_whatsThis);
    self->m_whatsThis = copy;
}

void XShortcut_setWhatsThis_2(XShortcut* self, const char* utf8)
{
    XString* text;
    if (!self) return;
    text = utf8 ? XString_create_utf8(utf8) : NULL;
    XShortcut_setWhatsThis(self, text);
    if (text)
        XString_delete_base((XClass*)text);
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
        XShortcut* sc;
        const XWidget* w;
        if (!item) continue;
        sc = *item;
        if (!sc || !sc->m_enabled || sc->m_key != key) continue;
        /* context 过滤（对标 QShortcutMap 的可达性判定）：
         * Application 恒匹配；Window 限焦点控件与快捷键父级同顶层；
         * WidgetWithChildren 限焦点控件为父级自身或其后代；
         * Widget 仅父级自身。 */
        if (sc->m_context == XShortcutContext_ApplicationShortcut)
            return sc;
        if (!focusWidget) continue;
        if (sc->m_context == XShortcutContext_WindowShortcut) {
            XWidget* shortcutTop = sc->m_parentWidget
                ? XWidget_topLevelWidget(sc->m_parentWidget) : NULL;
            /* 无父级的快捷键不限定窗口（legacy 宽松口径，回归 219a
             * 契约）；有父级时要求焦点控件同顶层（对标 Qt
             * WindowShortcut 的活动窗口语义）。 */
            if (!shortcutTop ||
                XWidget_topLevelWidget(focusWidget) == shortcutTop)
                return sc;
            continue;
        }
        if (sc->m_context == XShortcutContext_WidgetWithChildrenShortcut) {
            if (!sc->m_parentWidget) continue;
            w = focusWidget;
            while (w) {
                if ((const XWidget*)w ==
                    (const XWidget*)sc->m_parentWidget)
                    return sc;
                w = (const XWidget*)XObject_parent((const XObject*)w);
            }
            continue;
        }
        if (sc->m_context == XShortcutContext_WidgetShortcut &&
            (XObject*)focusWidget == sc->m_parentWidget)
            return sc;
    }
    return NULL;
}

void XShortcut_activate(XShortcut* self)
{
    if (!self) return;
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
