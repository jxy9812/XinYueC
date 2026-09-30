/**
 * @file       XVirtualKeyboardSettings.c
 * @brief      XVirtualKeyboardSettings 虚拟键盘设置单例实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardSettings.h"
#include "XVirtualKeyboardInputContext_Protected.h" /* setInputMethodHints → 上下文生效 hints 缓存刷新 */
#include "XStringUtils.h"
#include "XMemory.h"

/** @brief 进程单例指针。 */
static XVirtualKeyboardSettings* s_xvksInstance = NULL;

/** @brief 可用区域常量表（本轮 zh_CN+latin 一套；42 locale 集合
 *         scopeOut）。 */
static const char* const s_xvksAvailableLocales[] = { "zh_CN", "en", NULL };

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardSettingsPrivate
{
    XString* m_styleName;      /**< 样式名（存储-only）。 */
    XString* m_locale;         /**< 区域语言。 */
    XString* m_layoutPath;     /**< 布局路径（存储-only）。 */
    XString* m_userDataPath;   /**< 用户数据路径（存储-only）。 */
    XString* m_activeLocales[8]; /**< 激活区域（拷贝；<=8）。 */
    int m_activeLocaleCount;   /**< 激活区域数。 */
    int m_wclAutoHideDelay;    /**< 候选条自动隐藏延时（ms）。 */
    bool m_wclAlwaysVisible;   /**< 候选条常显。 */
    bool m_wclAutoCommitWord;  /**< 候选自动提交（存储-only）。 */
    bool m_fullScreenMode;     /**< 全屏模式（存储-only）。 */
    int m_hwrTimeoutAlphabetic; /**< 字母手写超时（存储-only）。 */
    int m_hwrTimeoutCjk;       /**< CJK 手写超时（存储-only）。 */
    uint32_t m_inputMethodHints; /**< 设置级 hints。 */
    bool m_handwritingDisabled; /**< 手写禁用（存储-only）。 */
    bool m_defaultInputMethodDisabled; /**< 默认输入法禁用。 */
    bool m_defaultDictionaryDisabled;  /**< 默认词典禁用。 */
    uint32_t m_visibleFunctionKeys;    /**< 可见功能键位集（存储-only）。 */
    bool m_closeOnReturn;      /**< 回车收面板。 */
    bool m_keyboardEnabled;    /**< 键盘总开关（XGui 扩展）。 */
} XVirtualKeyboardSettingsPrivate;

/** @brief 内部取私有块。 */
static XVirtualKeyboardSettingsPrivate* xvks_priv(
        const XVirtualKeyboardSettings* self)
{
    return (self && self->m_data)
               ? (XVirtualKeyboardSettingsPrivate*)self->m_data : NULL;
}

/** @brief 发射信号（无参）。 */
static void xvks_emit0(XVirtualKeyboardSettings* self, size_t signal)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/** @brief 字符串成员写入（拷贝语义；相同内容不写）。 */
static void xvks_setString(XVirtualKeyboardSettings* self, XString** slot,
                           const XString* value)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || !slot) return;
    if (*slot && value &&
        XStrcmp(XString_toUtf8(*slot), XString_toUtf8(value)) == 0)
        return;
    if (*slot) {
        XString_delete_base(*slot);
        *slot = NULL;
    }
    *slot = value ? XString_create_copy(value) : NULL;
}

/* 前向声明。 */
static void XVks_deinit(XVirtualKeyboardSettings* self);
void XVirtualKeyboardSettings_init(XVirtualKeyboardSettings* self);

XVtable* XVirtualKeyboardSettings_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardSettings)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVks_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：释放字符串成员与私有块后调父类。 */
static void XVks_deinit(XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv;
    int i;
    if (!self) return;
    priv = xvks_priv(self);
    if (priv) {
        if (priv->m_styleName) XString_delete_base(priv->m_styleName);
        if (priv->m_locale) XString_delete_base(priv->m_locale);
        if (priv->m_layoutPath) XString_delete_base(priv->m_layoutPath);
        if (priv->m_userDataPath) XString_delete_base(priv->m_userDataPath);
        for (i = 0; i < priv->m_activeLocaleCount; ++i) {
            if (priv->m_activeLocales[i])
                XString_delete_base(priv->m_activeLocales[i]);
        }
        XFree_System(priv);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XVirtualKeyboardSettings_init(XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv;
    int i;
    int n = 0;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XVirtualKeyboardSettings);
    self->m_data = XMalloc_System(sizeof(XVirtualKeyboardSettingsPrivate));
    if (!self->m_data) return;
    priv = (XVirtualKeyboardSettingsPrivate*)self->m_data;
    XMemset(priv, 0, sizeof(*priv));
    priv->m_wclAutoHideDelay = 5000;
    priv->m_hwrTimeoutAlphabetic = 500;
    priv->m_hwrTimeoutCjk = 500;
    priv->m_keyboardEnabled = true;
    /* 激活区域默认=可用列表全量。 */
    while (s_xvksAvailableLocales[n] && n < 8) {
        priv->m_activeLocales[n] =
            XString_create_utf8(s_xvksAvailableLocales[n]);
        if (priv->m_activeLocales[n]) ++n;
    }
    priv->m_activeLocaleCount = n;
    (void)i;
}

XVirtualKeyboardSettings* XVirtualKeyboardSettings_instance(void)
{
    if (!s_xvksInstance) {
        XVirtualKeyboardSettings* self =
            (XVirtualKeyboardSettings*)XMalloc_System(
                sizeof(XVirtualKeyboardSettings));
        if (!self) return NULL;
        XVirtualKeyboardSettings_init(self);
        Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
        Set_Class_IsHeap(self, true);
        s_xvksInstance = self;
    }
    return s_xvksInstance;
}

/* ==================== 字符串属性 ==================== */

XString* XVirtualKeyboardSettings_styleName(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return XString_create_copy(priv && priv->m_styleName ? priv->m_styleName
                                                         : NULL);
}

void XVirtualKeyboardSettings_setStyleName(XVirtualKeyboardSettings* self,
                                           const XString* styleName)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv) return;
    xvks_setString(self, &priv->m_styleName, styleName);
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_styleNameChanged_signal(NULL));
}

void XVirtualKeyboardSettings_setStyleName_2(XVirtualKeyboardSettings* self,
                                             const char* styleName)
{
    XString* tmp = styleName ? XString_create_utf8(styleName) : NULL;
    XVirtualKeyboardSettings_setStyleName(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

XString* XVirtualKeyboardSettings_locale(const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return XString_create_copy(priv && priv->m_locale ? priv->m_locale : NULL);
}

void XVirtualKeyboardSettings_setLocale(XVirtualKeyboardSettings* self,
                                        const XString* locale)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv) return;
    xvks_setString(self, &priv->m_locale, locale);
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_localeChanged_signal(NULL));
}

void XVirtualKeyboardSettings_setLocale_2(XVirtualKeyboardSettings* self,
                                          const char* locale)
{
    XString* tmp = locale ? XString_create_utf8(locale) : NULL;
    XVirtualKeyboardSettings_setLocale(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

XString* XVirtualKeyboardSettings_layoutPath(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return XString_create_copy(priv && priv->m_layoutPath ? priv->m_layoutPath
                                                          : NULL);
}

void XVirtualKeyboardSettings_setLayoutPath(XVirtualKeyboardSettings* self,
                                            const XString* layoutPath)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv) return;
    xvks_setString(self, &priv->m_layoutPath, layoutPath);
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_layoutPathChanged_signal(NULL));
}

void XVirtualKeyboardSettings_setLayoutPath_2(
        XVirtualKeyboardSettings* self, const char* layoutPath)
{
    XString* tmp = layoutPath ? XString_create_utf8(layoutPath) : NULL;
    XVirtualKeyboardSettings_setLayoutPath(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

XString* XVirtualKeyboardSettings_userDataPath(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return XString_create_copy(priv && priv->m_userDataPath
                                   ? priv->m_userDataPath : NULL);
}

void XVirtualKeyboardSettings_setUserDataPath(
        XVirtualKeyboardSettings* self, const XString* userDataPath)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv) return;
    xvks_setString(self, &priv->m_userDataPath, userDataPath);
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_userDataPathChanged_signal(NULL));
}

void XVirtualKeyboardSettings_setUserDataPath_2(
        XVirtualKeyboardSettings* self, const char* userDataPath)
{
    XString* tmp = userDataPath ? XString_create_utf8(userDataPath) : NULL;
    XVirtualKeyboardSettings_setUserDataPath(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

/* ==================== 区域列表 ==================== */

int XVirtualKeyboardSettings_availableLocales(
        const XVirtualKeyboardSettings* self, const char** outLocales,
        int maxCount)
{
    int i;
    int written = 0;
    (void)self;
    for (i = 0; s_xvksAvailableLocales[i]; ++i) {
        if (outLocales && written < maxCount)
            outLocales[written] = s_xvksAvailableLocales[i];
        ++written;
    }
    return written;
}

int XVirtualKeyboardSettings_activeLocales(
        const XVirtualKeyboardSettings* self, const char** outLocales,
        int maxCount)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    int i;
    int written = 0;
    if (!priv) return 0;
    for (i = 0; i < priv->m_activeLocaleCount; ++i) {
        if (outLocales && written < maxCount)
            outLocales[written] =
                XString_toUtf8(priv->m_activeLocales[i]);
        ++written;
    }
    return written;
}

bool XVirtualKeyboardSettings_setActiveLocales(
        XVirtualKeyboardSettings* self, const char* const* locales,
        int count)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    int i;
    int j;
    int n = 0;
    if (!priv) return false;
    if (count > 8) count = 8;
    for (i = 0; locales && i < count; ++i) {
        bool available = false;
        for (j = 0; s_xvksAvailableLocales[j]; ++j) {
            if (XStrcmp(s_xvksAvailableLocales[j], locales[i]) == 0) {
                available = true;
                break;
            }
        }
        if (!available) return false; /* 不在可用列表：整体拒绝。 */
    }
    for (i = 0; i < priv->m_activeLocaleCount; ++i) {
        if (priv->m_activeLocales[i])
            XString_delete_base(priv->m_activeLocales[i]);
        priv->m_activeLocales[i] = NULL;
    }
    for (i = 0; locales && i < count; ++i) {
        priv->m_activeLocales[n] = XString_create_utf8(locales[i]);
        if (priv->m_activeLocales[n]) ++n;
    }
    priv->m_activeLocaleCount = n;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_activeLocalesChanged_signal(NULL));
    return true;
}

/* ==================== 数值/布尔属性 ==================== */

int XVirtualKeyboardSettings_wclAutoHideDelay(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_wclAutoHideDelay : 5000;
}

void XVirtualKeyboardSettings_setWclAutoHideDelay(
        XVirtualKeyboardSettings* self, int delayMs)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_wclAutoHideDelay == delayMs) return;
    priv->m_wclAutoHideDelay = delayMs;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_wclAutoHideDelayChanged_signal(
                       NULL));
}

bool XVirtualKeyboardSettings_wclAlwaysVisible(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_wclAlwaysVisible : false;
}

void XVirtualKeyboardSettings_setWclAlwaysVisible(
        XVirtualKeyboardSettings* self, bool enabled)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_wclAlwaysVisible == enabled) return;
    priv->m_wclAlwaysVisible = enabled;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_wclAlwaysVisibleChanged_signal(
                       NULL));
}

bool XVirtualKeyboardSettings_wclAutoCommitWord(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_wclAutoCommitWord : false;
}

void XVirtualKeyboardSettings_setWclAutoCommitWord(
        XVirtualKeyboardSettings* self, bool enabled)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_wclAutoCommitWord == enabled) return;
    priv->m_wclAutoCommitWord = enabled;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_wclAutoCommitWordChanged_signal(
                       NULL));
}

bool XVirtualKeyboardSettings_fullScreenMode(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_fullScreenMode : false;
}

void XVirtualKeyboardSettings_setFullScreenMode(
        XVirtualKeyboardSettings* self, bool enabled)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_fullScreenMode == enabled) return;
    priv->m_fullScreenMode = enabled;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_fullScreenModeChanged_signal(NULL));
}

int XVirtualKeyboardSettings_hwrTimeoutForAlphabetic(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_hwrTimeoutAlphabetic : 500;
}

void XVirtualKeyboardSettings_setHwrTimeoutForAlphabetic(
        XVirtualKeyboardSettings* self, int timeoutMs)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_hwrTimeoutAlphabetic == timeoutMs) return;
    priv->m_hwrTimeoutAlphabetic = timeoutMs;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_hwrTimeoutForAlphabeticChanged_signal(
                       NULL));
}

int XVirtualKeyboardSettings_hwrTimeoutForCjk(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_hwrTimeoutCjk : 500;
}

void XVirtualKeyboardSettings_setHwrTimeoutForCjk(
        XVirtualKeyboardSettings* self, int timeoutMs)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_hwrTimeoutCjk == timeoutMs) return;
    priv->m_hwrTimeoutCjk = timeoutMs;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_hwrTimeoutForCjkChanged_signal(
                       NULL));
}

uint32_t XVirtualKeyboardSettings_inputMethodHints(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_inputMethodHints : 0;
}

void XVirtualKeyboardSettings_setInputMethodHints(
        XVirtualKeyboardSettings* self, uint32_t hints)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_inputMethodHints == hints) return;
    priv->m_inputMethodHints = hints;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_inputMethodHintsChanged_signal(
                       NULL));
#if XVIRTUALKEYBOARD_ON
    /* 生效 hints=控件级 OR 叠加本设置（InputContext update 缓存）。控
     * 件级变更经 XWidget_setInputMethodHints 触发查询链刷新；本设置
     * 侧变更同源刷新——同控件已持焦时无焦点边沿，不刷则叠加换档失
     * 灵（缓存陈旧）。 */
    {
        XVirtualKeyboardInputContext* ctx =
            XVirtualKeyboardInputContext_instance();
        if (ctx) XVirtualKeyboardInputContext_update(ctx, 0);
    }
#endif
}

bool XVirtualKeyboardSettings_handwritingModeDisabled(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_handwritingDisabled : false;
}

void XVirtualKeyboardSettings_setHandwritingModeDisabled(
        XVirtualKeyboardSettings* self, bool disabled)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_handwritingDisabled == disabled) return;
    priv->m_handwritingDisabled = disabled;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_handwritingModeDisabledChanged_signal(
                       NULL));
}

bool XVirtualKeyboardSettings_defaultInputMethodDisabled(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_defaultInputMethodDisabled : false;
}

void XVirtualKeyboardSettings_setDefaultInputMethodDisabled(
        XVirtualKeyboardSettings* self, bool disabled)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_defaultInputMethodDisabled == disabled) return;
    priv->m_defaultInputMethodDisabled = disabled;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_defaultInputMethodDisabledChanged_signal(
                       NULL));
}

bool XVirtualKeyboardSettings_defaultDictionaryDisabled(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_defaultDictionaryDisabled : false;
}

void XVirtualKeyboardSettings_setDefaultDictionaryDisabled(
        XVirtualKeyboardSettings* self, bool disabled)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_defaultDictionaryDisabled == disabled) return;
    priv->m_defaultDictionaryDisabled = disabled;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_defaultDictionaryDisabledChanged_signal(
                       NULL));
}

uint32_t XVirtualKeyboardSettings_visibleFunctionKeys(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_visibleFunctionKeys : 0;
}

void XVirtualKeyboardSettings_setVisibleFunctionKeys(
        XVirtualKeyboardSettings* self, uint32_t functionKeys)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_visibleFunctionKeys == functionKeys) return;
    priv->m_visibleFunctionKeys = functionKeys;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_visibleFunctionKeysChanged_signal(
                       NULL));
}

bool XVirtualKeyboardSettings_closeOnReturn(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_closeOnReturn : false;
}

void XVirtualKeyboardSettings_setCloseOnReturn(
        XVirtualKeyboardSettings* self, bool enabled)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_closeOnReturn == enabled) return;
    priv->m_closeOnReturn = enabled;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_closeOnReturnChanged_signal(NULL));
}

bool XVirtualKeyboardSettings_keyboardEnabled(
        const XVirtualKeyboardSettings* self)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    return priv ? priv->m_keyboardEnabled : true;
}

void XVirtualKeyboardSettings_setKeyboardEnabled(
        XVirtualKeyboardSettings* self, bool enabled)
{
    XVirtualKeyboardSettingsPrivate* priv = xvks_priv(self);
    if (!priv || priv->m_keyboardEnabled == enabled) return;
    priv->m_keyboardEnabled = enabled;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_keyboardEnabledChanged_signal(
                       NULL));
}

void XVirtualKeyboardSettings_userDataReset(XVirtualKeyboardSettings* self)
{
    if (!self) return;
    xvks_emit0(self, (size_t)
                   XVirtualKeyboardSettings_userDataReset_signal(NULL));
}

/* ==================== 信号（纯 ID getter） ==================== */

void* XVirtualKeyboardSettings_styleNameChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboardSettings_styleNameChanged_signal;
}

void* XVirtualKeyboardSettings_localeChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboardSettings_localeChanged_signal;
}

void* XVirtualKeyboardSettings_availableLocalesChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_availableLocalesChanged_signal;
}

void* XVirtualKeyboardSettings_activeLocalesChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_activeLocalesChanged_signal;
}

void* XVirtualKeyboardSettings_layoutPathChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboardSettings_layoutPathChanged_signal;
}

void* XVirtualKeyboardSettings_wclAutoHideDelayChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_wclAutoHideDelayChanged_signal;
}

void* XVirtualKeyboardSettings_wclAlwaysVisibleChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_wclAlwaysVisibleChanged_signal;
}

void* XVirtualKeyboardSettings_wclAutoCommitWordChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_wclAutoCommitWordChanged_signal;
}

void* XVirtualKeyboardSettings_fullScreenModeChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_fullScreenModeChanged_signal;
}

void* XVirtualKeyboardSettings_userDataPathChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboardSettings_userDataPathChanged_signal;
}

void* XVirtualKeyboardSettings_hwrTimeoutForAlphabeticChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_hwrTimeoutForAlphabeticChanged_signal;
}

void* XVirtualKeyboardSettings_hwrTimeoutForCjkChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_hwrTimeoutForCjkChanged_signal;
}

void* XVirtualKeyboardSettings_inputMethodHintsChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_inputMethodHintsChanged_signal;
}

void* XVirtualKeyboardSettings_handwritingModeDisabledChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_handwritingModeDisabledChanged_signal;
}

void* XVirtualKeyboardSettings_defaultInputMethodDisabledChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_defaultInputMethodDisabledChanged_signal;
}

void* XVirtualKeyboardSettings_defaultDictionaryDisabledChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_defaultDictionaryDisabledChanged_signal;
}

void* XVirtualKeyboardSettings_visibleFunctionKeysChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_visibleFunctionKeysChanged_signal;
}

void* XVirtualKeyboardSettings_closeOnReturnChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_closeOnReturnChanged_signal;
}

void* XVirtualKeyboardSettings_keyboardEnabledChanged_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSettings_keyboardEnabledChanged_signal;
}

void* XVirtualKeyboardSettings_userDataReset_signal(
        XVirtualKeyboardSettings* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboardSettings_userDataReset_signal;
}

#endif /* XVIRTUALKEYBOARD_ON */
