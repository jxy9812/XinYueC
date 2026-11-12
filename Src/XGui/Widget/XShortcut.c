/**
 * @file       XShortcut.c
 * @brief      快捷键对象实现（对标 Qt 6.8 QShortcut 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；实现为对象状态 +
 *             模块静态全局注册表（XVector<XShortcut*>），注册/注销由
 *             create/delete 自动完成，XShortcut_match/_2 供按键路径调用。
 *
 *             **修饰位承载**（台账 §B12 主修项）：序列元素类型从
 *             `int`（XKey 裸键码）换成 `XKeySequence` 值类型（修饰位 +
 *             键码 + 多组）。旧的 int 入口保留为「裸键便捷视图」：
 *             `XShortcut_setKey(self, key)` ⇒
 *             `XShortcut_setKeySequence_2(self, NoModifier, key)`。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XShortcut.h"
#include "XWidget.h"
#include "XGuiApplication.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XVector.h"
#include "XGuiConfig.h"

#if XWIDGET_ON

/* ==================== 全局注册表 ==================== */

/** @brief 模块静态注册表（元素为 XShortcut* 借用指针）。 */
static XVector* s_shortcutRegistry = NULL;

/** @brief 全局 id 分配器：单调递减，与 QShortcutMap 的 `--(d->currentId)`
 *         同号源（qshortcutmap.cpp:133）。0 保留为「未分配」，
 *         故从 -1 起向下分配、跳过 0。 */
static int s_shortcutNextId = 0;

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

/** @brief 分配一个新全局 id（跳过 0）。 */
static int xshortcut_allocId(void)
{
    --s_shortcutNextId;
    if (s_shortcutNextId == 0)
        --s_shortcutNextId;
    return s_shortcutNextId;
}

/** @brief 发射空参信号（args 传 NULL，对标 XEmitSignal 的空参模式）。 */
static void xshortcut_emitVoidSignal(XShortcut* self, size_t signal)
{
    if (!self) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/* ==================== 序列存储辅助 ==================== */

/** @brief 取第 index 条序列（借用）；越界返回 NULL。 */
static const XKeySequence* xshortcut_sequenceAtInternal(const XShortcut* self,
                                                        int index)
{
    if (!self || !self->m_keys || index < 0) return NULL;
    if ((int64_t)index >=
        (int64_t)XVector_size_base((const XContainer*)self->m_keys))
        return NULL;
    return (const XKeySequence*)XVector_at_base(self->m_keys, index);
}

/** @brief 取第 index 条序列的 id；越界返回 0。 */
static int xshortcut_sequenceIdInternal(const XShortcut* self, int index)
{
    int* id;
    if (!self || !self->m_ids || index < 0) return 0;
    if ((int64_t)index >=
        (int64_t)XVector_size_base((const XContainer*)self->m_ids))
        return 0;
    id = (int*)XVector_at_base(self->m_ids, index);
    return id ? *id : 0;
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_shortcut_deinit(XShortcut* self)
{
    if (!self) return;
    XShortcut_setGlobal(self, false); /* 平台抓键先释放（对象还完整）。 */
    XShortcut_unregister(self);
    if (self->m_whatsThis) {
        XClassDelete((XClass*)self->m_whatsThis);
        self->m_whatsThis = NULL;
    }
    if (self->m_keys) {
        XClassDelete((XClass*)self->m_keys);
        self->m_keys = NULL;
    }
    if (self->m_ids) {
        XClassDelete((XClass*)self->m_ids);
        self->m_ids = NULL;
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
    self->m_global = false;
    self->m_parentWidget = parent;
    self->m_keys = XVector_Create(XKeySequence);
    self->m_ids = XVector_Create(int);
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

XShortcut* XShortcut_create_3_ex(XMemoryType memory,
                                 const XKeySequence* sequence,
                                 XObject* parent)
{
    XShortcut* self = XShortcut_create_ex(memory, parent);
    if (!self) return NULL;
    XShortcut_setKeySequence(self, sequence);
    return self;
}

XShortcut* XShortcut_create_4_ex(XMemoryType memory,
                                 XKeyboardModifiers modifiers, int key,
                                 XObject* parent)
{
    XShortcut* self = XShortcut_create_ex(memory, parent);
    if (!self) return NULL;
    XShortcut_setKeySequence_2(self, modifiers, key);
    return self;
}

/* ==================== 序列设置（核心） ==================== */

void XShortcut_setSequences(XShortcut* self, const XKeySequence* sequences,
                            int count)
{
    int i;
    int kept = 0;
    bool same;
    if (!self) return;
    if (!self->m_keys) {
        self->m_keys = XVector_Create(XKeySequence);
        if (!self->m_keys) return;
    }
    if (!self->m_ids) {
        self->m_ids = XVector_Create(int);
        if (!self->m_ids) return;
    }
    /* 同值早退（对标 qshortcut.cpp:380-390 的 `if (d->sc_sequences == keys)
     * return;`）：逐条序列比较，全等即不重抓键、更不换 id。 */
    same = (count == (int)XVector_size_base(
                          (const XContainer*)self->m_keys));
    if (same && count > 0 && !sequences) same = false;
    if (same && sequences) {
        for (i = 0; i < count; ++i) {
            if (!XKeySequence_isEqual(xshortcut_sequenceAtInternal(self, i),
                                      &sequences[i])) {
                same = false;
                break;
            }
        }
    }
    if (same) return;

    /* id 与序列一一对应：先把旧 id 逐条搬到临时栈区（最多 4 条），
     * 重排时同位置序列沿用旧 id（对标 QShortcut::setKey 换键不换 id 的
     * 实际表现——Qt 的 redoGrab 按 sc_sequences 下标复用 sc_ids）。 */
    {
        int oldIds[XKEYSEQUENCE_MAX_LENGTH];
        int oldCount = (int)XVector_size_base(
                           (const XContainer*)self->m_ids);
        for (i = 0; i < XKEYSEQUENCE_MAX_LENGTH; ++i) {
            int* v = (i < oldCount)
                         ? (int*)XVector_at_base(self->m_ids, i) : NULL;
            oldIds[i] = v ? *v : 0;
        }
        /* 已全局抓键：先释放旧序列在平台层的抓键（Qt 的 redoGrab 前半段）。
         * 注意先把标志存到局部：setGlobal(false) 会把 m_global 清掉，直接
         * 之后再用 self->m_global 判断会永远为假、热键永不重抓。 */
        {
            bool wasGlobal = self->m_global;
            if (wasGlobal)
                XShortcut_setGlobal(self, false);

            XVector_clear_base((XContainer*)self->m_keys);
            XVector_clear_base((XContainer*)self->m_ids);
            if (sequences && count > 0) {
                for (i = 0; i < count; ++i) {
                    int id;
                    const XKeySequence* item = &sequences[i];
                    if (XKeySequence_isEmpty(item)) continue;
                    XVector_push_back_1_base(self->m_keys, (void*)item);
                    /* 逐条保留旧 id；不足则新分配（新序列拿新号）。 */
                    id = (kept < oldCount && oldIds[kept] != 0)
                             ? oldIds[kept] : xshortcut_allocId();
                    XVector_push_back_1_base(self->m_ids, &id);
                    ++kept;
                }
            }
            if (wasGlobal)
                XShortcut_setGlobal(self, true); /* 重抓（失败即静默降级）。 */
        }
    }
}

void XShortcut_setKey(XShortcut* self, int key)
{
    XShortcut_setKeySequence_2(self, XKeyboardModifier_NoModifier, key);
}

void XShortcut_setKeySequence_2(XShortcut* self,
                                XKeyboardModifiers modifiers, int key)
{
    XKeySequence seq;
    if (!self) return;
    if (key == 0) {
        /* Qt: `setKey(QKeySequence())` ⇒ `setKeys({})`（qshortcut.cpp:356-362）。 */
        XShortcut_setSequences(self, NULL, 0);
        return;
    }
    seq = XKeySequence_fromKey(modifiers, key);
    XShortcut_setSequences(self, &seq, 1);
}

void XShortcut_setKeySequence(XShortcut* self, const XKeySequence* sequence)
{
    if (!self) return;
    if (!sequence || XKeySequence_isEmpty(sequence)) {
        XShortcut_setSequences(self, NULL, 0);
        return;
    }
    XShortcut_setSequences(self, sequence, 1);
}

void XShortcut_setKeySequence_3(XShortcut* self, const char* utf8)
{
    XKeySequence seq = XKeySequence_empty();
    if (!self) return;
    /* 非法文本 ⇒ 空序列 ⇒ 清空（Qt 把非法文本解析成空 QKeySequence）。 */
    if (utf8 && XKeySequence_fromString(utf8, &seq) && !XKeySequence_isEmpty(&seq))
        XShortcut_setSequences(self, &seq, 1);
    else
        XShortcut_setSequences(self, NULL, 0);
}

void XShortcut_setKeys(XShortcut* self, const int* keys, int count)
{
    XKeySequence local[4];
    int i;
    int n = 0;
    if (!self) return;
    if (!keys || count <= 0) {
        XShortcut_setSequences(self, NULL, 0);
        return;
    }
    for (i = 0; i < count && n < XKEYSEQUENCE_MAX_LENGTH; ++i) {
        if (keys[i] == 0) continue;
        local[n++] = XKeySequence_fromKey(XKeyboardModifier_NoModifier,
                                          keys[i]);
    }
    XShortcut_setSequences(self, local, n);
}

/* ==================== 序列查询 ==================== */

const XKeySequence* XShortcut_keySequence(const XShortcut* self)
{
    return xshortcut_sequenceAtInternal(self, 0);
}

const XKeySequence* XShortcut_sequenceAt(const XShortcut* self, int index)
{
    return xshortcut_sequenceAtInternal(self, index);
}

int XShortcut_sequenceCount(const XShortcut* self)
{
    if (!self || !self->m_keys) return 0;
    return (int)XVector_size_base((const XContainer*)self->m_keys);
}

int XShortcut_keyCount(const XShortcut* self)
{
    return XShortcut_sequenceCount(self);
}

int XShortcut_key(const XShortcut* self)
{
    return XKeySequence_firstKey(xshortcut_sequenceAtInternal(self, 0));
}

int XShortcut_keyAt(const XShortcut* self, int index)
{
    return XKeySequence_firstKey(xshortcut_sequenceAtInternal(self, index));
}

XKeyboardModifiers XShortcut_keyModifiers(const XShortcut* self)
{
    const XKeySequence* seq = xshortcut_sequenceAtInternal(self, 0);
    if (!seq || XKeySequence_isEmpty(seq)) return XKeyboardModifier_NoModifier;
    return seq->combos[0].modifiers;
}

int XShortcut_sequenceId(const XShortcut* self, int index)
{
    if (XShortcut_sequenceCount(self) <= index) return 0;
    return xshortcut_sequenceIdInternal(self, index);
}

int XShortcut_id(const XShortcut* self)
{
    return XShortcut_sequenceId(self, 0);
}

XObject* XShortcut_parentWidget(const XShortcut* self)
{
    return self ? self->m_parentWidget : NULL;
}

/* ==================== 属性 ==================== */

void XShortcut_setEnabled(XShortcut* self, bool enabled)
{
    if (!self) return;
    /* 同值早退（对标 qshortcut.cpp:430-434）。 */
    if (self->m_enabled == enabled) return;
    self->m_enabled = enabled;
    /* 已全局抓键：同步改平台层启用位（对标 setShortcutEnabled）。 */
    if (self->m_global) {
        int i;
        int n = XShortcut_sequenceCount(self);
        for (i = 0; i < n; ++i) {
            const XKeySequence* seq = xshortcut_sequenceAtInternal(self, i);
            if (!seq || XKeySequence_isEmpty(seq)) continue;
            XGuiApplication_setShortcutEnabled(
                seq->combos[0].key, seq->combos[0].modifiers, enabled);
        }
    }
}

bool XShortcut_isEnabled(const XShortcut* self)
{
    return self ? self->m_enabled : false;
}

void XShortcut_setContext(XShortcut* self, XShortcutContext context)
{
    if (!self) return;
    /* 同值早退（对标 qshortcut.cpp:459-466）。 */
    if (self->m_context == (uint32_t)context) return;
    self->m_context = context;
    /* 已全局抓键：上下文变化重新抓键（Qt 的 redoGrab 同义）。 */
    if (self->m_global) {
        XShortcut_setGlobal(self, false);
        XShortcut_setGlobal(self, true);
    }
}

XShortcutContext XShortcut_context(const XShortcut* self)
{
    return self ? (XShortcutContext)self->m_context
                : XShortcutContext_WindowShortcut;
}

void XShortcut_setAutoRepeat(XShortcut* self, bool on)
{
    if (!self) return;
    /* [P3-33 2026-11-11] 存储位 + 快捷键匹配后的激活守卫：
       XWidget_dispatchKeyEvent 对本标志为 false 的快捷键跳过"自动重复
       键"的激活（对标 QShortcutMap::tryShortcut，
       qshortcutmap.cpp:588-591：`e->isAutoRepeat() && !next->autorepeat`
       ⇒ 不派发）。同值早退（对标 qshortcut.cpp:484-494）。 */
    if (self->m_autoRepeat == on) return;
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

/* ==================== 平台原生抓键 ==================== */

bool XShortcut_setGlobal(XShortcut* self, bool on)
{
    int i;
    int n;
    if (!self) return false;
    if (!on) {
        if (!self->m_global) return true; /* 幂等：本未抓键。 */
        n = XShortcut_sequenceCount(self);
        for (i = 0; i < n; ++i) {
            const XKeySequence* seq = xshortcut_sequenceAtInternal(self, i);
            if (!seq || XKeySequence_isEmpty(seq)) continue;
            XShortcut_releaseNative(seq->combos[0].modifiers, seq->combos[0].key,
                                    NULL);
        }
        self->m_global = false;
        return true;
    }
    n = XShortcut_sequenceCount(self);
    if (n <= 0) return false; /* 无序列无可抓（Qt 断言 Cannot add keyless）。 */
    if (self->m_global) return true; /* 幂等：已抓键（序列变更路径先关再开）。 */
    /* 平台抓键（XGrabKey/RegisterHotKey）是**单键码**机制：多组序列
     * （如 "Ctrl+O, Ctrl+S"）没有对应的单一组合可抓——若只抓首组会把
     * Ctrl+O 单独变成全局热键（登记了 A 触发的是 A 的前缀，语义错误）。
     * 故凡含多组序列一律返回 false，对象留在进程内匹配（Qt 的全局热键
     * 同样不支持多键序列；单组限制见 XShortcut.h @note 披露）。 */
    for (i = 0; i < n; ++i) {
        const XKeySequence* seq = xshortcut_sequenceAtInternal(self, i);
        if (seq && XKeySequence_count(seq) > 1) return false;
    }
    /* 全部序列都得抓到才算成功：任一失败即回滚已抓的，返回 false 让调用方
     * 退回进程内匹配（台账 §B12 验收方式 2：冲突注册返回失败、不崩、
     * 不抢已注册组合）。 */
    for (i = 0; i < n; ++i) {
        const XKeySequence* seq = xshortcut_sequenceAtInternal(self, i);
        if (!seq || XKeySequence_isEmpty(seq)) continue;
        if (!XShortcut_grabNative(seq->combos[0].modifiers, seq->combos[0].key,
                                  NULL)) {
            int k;
            for (k = 0; k < i; ++k) {
                const XKeySequence* prev =
                    xshortcut_sequenceAtInternal(self, k);
                if (!prev || XKeySequence_isEmpty(prev)) continue;
                XShortcut_releaseNative(prev->combos[0].modifiers,
                                        prev->combos[0].key, NULL);
            }
            return false;
        }
    }
    self->m_global = true;
    if (!self->m_enabled) {
        /* 抓键成功但对象已禁用：把平台启用位落成 false。 */
        for (i = 0; i < n; ++i) {
            const XKeySequence* seq = xshortcut_sequenceAtInternal(self, i);
            if (!seq || XKeySequence_isEmpty(seq)) continue;
            XGuiApplication_setShortcutEnabled(
                seq->combos[0].key, seq->combos[0].modifiers, false);
        }
    }
    return true;
}

bool XShortcut_isGlobal(const XShortcut* self)
{
    return self ? self->m_global : false;
}

bool XShortcut_grabNative(XKeyboardModifiers modifiers, int key,
                          void* nativeWindow)
{
    return XGuiApplication_grabShortcut(key, modifiers, nativeWindow);
}

bool XShortcut_releaseNative(XKeyboardModifiers modifiers, int key,
                             void* nativeWindow)
{
    return XGuiApplication_releaseShortcut(key, modifiers, nativeWindow);
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

/** @brief 序列是否与「键码 + 修饰位」完全匹配（多序列任一命中即算）。
 *  @details 比较前两侧都剥掉 KeypadModifier（对齐 QShortcutMap::nextState
 *           的 `find(e, Qt::KeypadModifier)` 回退）。 */
static bool xshortcut_hasKey(const XShortcut* sc, int key,
                             XKeyboardModifiers modifiers)
{
    int64_t i;
    int64_t n;
    XKeySequence probe;
    XKeyboardModifiers mask = ~(XKeyboardModifiers)XKeyboardModifier_KeypadModifier;
    if (!sc || !sc->m_keys || key == 0) return false;
    probe = XKeySequence_fromKey(modifiers & mask, key);
    n = XVector_size_base((const XContainer*)sc->m_keys);
    for (i = 0; i < n; ++i) {
        const XKeySequence* item =
            (const XKeySequence*)XVector_at_base(sc->m_keys, i);
        XKeySequence normalized;
        if (!item || XKeySequence_isEmpty(item)) continue;
        normalized = *item;
        normalized.combos[0].modifiers &= mask;
        if (XKeySequence_isEqual(&normalized, &probe)) return true;
    }
    return false;
}

/** @brief context 过滤谓词（对标 QShortcutMap 的可达性判定）：
 *         Application 恒匹配；Window 限焦点控件与快捷键父级同顶层；
 *         WidgetWithChildren 限焦点控件为父级自身或其后代；Widget 仅
 *         父级自身。供 match 与歧义计数共用。 */
static bool xshortcut_accepts(const XShortcut* sc, int key,
                              XKeyboardModifiers modifiers,
                              const XWidget* focusWidget)
{
    const XWidget* w;
    if (!sc || !sc->m_enabled || !xshortcut_hasKey(sc, key, modifiers))
        return false;
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

XShortcut* XShortcut_match_2(int key, XKeyboardModifiers modifiers,
                             XShortcutContext context, XWidget* focusWidget)
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
        if (xshortcut_accepts(*item, key, modifiers, focusWidget))
            return *item;
    }
    return NULL;
}

XShortcut* XShortcut_match(int key, XShortcutContext context,
                           XWidget* focusWidget)
{
    return XShortcut_match_2(key, XKeyboardModifier_NoModifier, context,
                             focusWidget);
}

/** @brief 统计同「键码 + 修饰位」在焦点代理视角下的命中数（歧义判定用）。 */
static int xshortcut_countMatches(int key, XKeyboardModifiers modifiers,
                                  const XWidget* focusWidget)
{
    int64_t i;
    int64_t n;
    int count = 0;
    if (!s_shortcutRegistry) return 0;
    n = XVector_size_base((const XContainer*)s_shortcutRegistry);
    for (i = 0; i < n; ++i) {
        XShortcut** item =
            (XShortcut**)XVector_at_base(s_shortcutRegistry, i);
        if (item && xshortcut_accepts(*item, key, modifiers, focusWidget))
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

void XShortcut_activate_2(XShortcut* self, int key,
                          XKeyboardModifiers modifiers)
{
    XWidget* focusProxy;
    int matches;
    if (!self) return;
    /* 对标 QShortcutMap：禁用的快捷键不参与匹配，永远不会被派发
     * （qshortcutmap.cpp:373 `&& (*it).correctContext() && (*it).enabled`；
     * QShortcut::setEnabled 注释「An enabled shortcut emits the
     * activated()...」）。 */
    if (!self->m_enabled)
        return;
    focusProxy = xshortcut_focusProxy(self);
    matches = xshortcut_countMatches(key, modifiers, focusProxy);
    if (matches > 1) {
        int64_t i;
        int64_t n;
        n = XVector_size_base((const XContainer*)s_shortcutRegistry);
        for (i = 0; i < n; ++i) {
            XShortcut** item =
                (XShortcut**)XVector_at_base(s_shortcutRegistry, i);
            if (item && xshortcut_accepts(*item, key, modifiers, focusProxy))
                xshortcut_emitVoidSignal(
                    *item, (size_t)XShortcut_activatedAmbiguously_signal);
        }
        return;
    }
    xshortcut_emitVoidSignal(self, (size_t)XShortcut_activated_signal);
}

void XShortcut_activate(XShortcut* self)
{
    const XKeySequence* seq;
    if (!self) return;
    /* 旧入口（无按键形参）：以首条序列的首组组合作为匹配键——单序列
     * 单组（绝大多数用法）与 Qt 一致；带修饰位的判定见 activate_2。 */
    seq = xshortcut_sequenceAtInternal(self, 0);
    if (!seq || XKeySequence_isEmpty(seq)) {
        /* 空序列：无「键」可统计歧义；行为退化为直接派发（legacy）。 */
        if (!self->m_enabled) return;
        xshortcut_emitVoidSignal(self, (size_t)XShortcut_activated_signal);
        return;
    }
    XShortcut_activate_2(self, seq->combos[0].key, seq->combos[0].modifiers);
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
