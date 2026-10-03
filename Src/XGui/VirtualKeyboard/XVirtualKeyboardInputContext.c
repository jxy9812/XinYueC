/**
 * @file       XVirtualKeyboardInputContext.c
 * @brief      XVirtualKeyboardInputContext 输入上下文实现（进程单例/
 *             焦点查询桥/hints OR 叠加/ShiftHandler 状态机/commit 落
 *             地公共信号）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardInputContext_Protected.h"
#include "XVirtualKeyboardInputEngine_Protected.h"
#include "XVirtualKeyboardSettings.h"
#include "XVirtualKeyboardObserver.h"
#include "XVirtualKeyboardPlainInputMethod.h"
#include "XVirtualKeyboardPinyinInputMethod.h"
#include "XInputMethod.h"
#include "XWidget.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XStyleHints.h"
#include "XGuiApplication.h"

/** @brief 组串缓冲容量（拼音组串 <=15 字母；余量给通用 preedit）。 */
#define XVKC_PREEDIT_MAX 255

/** @brief 进程单例指针（惰性创建；对标 QML singleton InputContext）。 */
static XVirtualKeyboardInputContext* s_xvkcInstance = NULL;

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardInputContextPrivate
{
    XVirtualKeyboardInputEngine* m_engine;    /**< 引擎（拥有）。 */
    XObject* m_focusObject;                   /**< 焦点对象（借用）。 */
    XConnection* m_focusConn;                 /**< 焦点对象 destroyed 防悬垂
                                                   连接（借用；NULL=未挂）。 */
    char m_preedit[XVKC_PREEDIT_MAX + 1];     /**< 组串文本（UTF-8）。 */
    uint32_t m_widgetHints;                   /**< 控件级 hints 缓存。 */
    uint32_t m_effectiveHints;                /**< 生效 hints 缓存（含叠加）。 */
    int m_cursorPosition;                     /**< 光标位置缓存。 */
    int m_anchorPosition;                     /**< 锚点位置缓存。 */
    XRectF m_cursorRectangle;                 /**< 光标矩形缓存。 */
    XRectF m_anchorRectangle;                 /**< 锚点矩形缓存。 */
    bool m_shiftActive;                       /**< 临时 shift。 */
    bool m_capsLockActive;                    /**< 大写锁。 */
    int64_t m_lastToggleMs;                   /**< 上次 shift 切换时刻
                                                   （单调毫秒，双击判定）。 */
    bool m_animating;                         /**< 动画态。 */
    char m_locale[32];                        /**< 区域语言（默认 zh_CN）。 */
} XVirtualKeyboardInputContextPrivate;

/** @brief 内部取私有块。 */
static XVirtualKeyboardInputContextPrivate* xvkc_priv(
        const XVirtualKeyboardInputContext* self)
{
    return (self && self->m_data)
               ? (XVirtualKeyboardInputContextPrivate*)self->m_data : NULL;
}

/** @brief 发射信号并管理参数表生命周期。 */
static void xvkc_emit(XVirtualKeyboardInputContext* self, size_t signal,
                      XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args)
        XVarList_delete(args);
}

/* 前向声明（class_init 注册与内部互调用）。 */
static void XVkc_deinit(XVirtualKeyboardInputContext* self);
static void xvkc_setShiftActive(XVirtualKeyboardInputContext* self,
                                bool active);
static void xvkc_setCapsLockActive(XVirtualKeyboardInputContext* self,
                                   bool active);
void XVirtualKeyboardInputContext_init(XVirtualKeyboardInputContext* self);
XVirtualKeyboardInputContext*
XVirtualKeyboardInputContext_create_ex(XMemoryType memory);

XVtable* XVirtualKeyboardInputContext_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardInputContext)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVkc_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：释放引擎与私有块后调父类。 */
static void XVkc_deinit(XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv;
    if (!self) return;
    priv = xvkc_priv(self);
    if (priv) {
        if (priv->m_focusConn) {
            XObject_disconnect_2(priv->m_focusConn);
            priv->m_focusConn = NULL;
        }
        if (priv->m_engine) {
            XClassDelete(priv->m_engine);
            priv->m_engine = NULL;
        }
        XFree_System(priv);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XVirtualKeyboardInputContext_init(XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XVirtualKeyboardInputContext);
    self->m_data = XMalloc_System(sizeof(XVirtualKeyboardInputContextPrivate));
    if (!self->m_data) return;
    priv = (XVirtualKeyboardInputContextPrivate*)self->m_data;
    XMemset(priv, 0, sizeof(*priv));
    XStrncpy(priv->m_locale, "zh_CN", sizeof(priv->m_locale));
    /* 引擎由上下文创建（Qt 私有构造口径）。 */
    priv->m_engine = XVirtualKeyboardInputEngine_create_forContext(self);
    if (priv->m_engine) {
        /* 内置语言插件静态注册（QML 插件机制的引擎内等价物；注册表
           在引擎私有块，进程单例创建即装）。注意必须走 _for 直指
           自有引擎：本函数在单例构造链上执行（instance() 尚未把新
           实例落座），经单例路由的 registerInputMethodFactory 会重
           入 instance() 无限递归（栈溢出），且即便递归被掐断注册也
           会落进重入产生的另一实例、自有注册表仍为空。 */
#if XKEYBOARD_IME_ON
        XVirtualKeyboardInputEngine_registerInputMethodFactory_for(
            priv->m_engine, "zh_CN",
            XVirtualKeyboardPinyinInputMethod_factory);
#endif
        XVirtualKeyboardInputEngine_registerInputMethodFactory_for(
            priv->m_engine, "en", XVirtualKeyboardPlainInputMethod_factory);
        XVirtualKeyboardInputEngine_registerInputMethodFactory_for(
            priv->m_engine, "C", XVirtualKeyboardPlainInputMethod_factory);
        /* 默认装配 Plain（拉丁直通）；拼音经面板 setImeEnabled 切装。 */
        XVirtualKeyboardInputEngine_setInputMethod(
            priv->m_engine,
            (XVirtualKeyboardAbstractInputMethod*)
                XVirtualKeyboardPlainInputMethod_create());
    }
}

XVirtualKeyboardInputContext*
XVirtualKeyboardInputContext_create_ex(XMemoryType memory)
{
    XVirtualKeyboardInputContext* self =
        (XVirtualKeyboardInputContext*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XVirtualKeyboardInputContext_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XVirtualKeyboardInputContext* XVirtualKeyboardInputContext_instance(void)
{
    if (!s_xvkcInstance)
        s_xvkcInstance = XVirtualKeyboardInputContext_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE);
    return s_xvkcInstance;
}

XVirtualKeyboardInputEngine* XVirtualKeyboardInputContext_instanceEngine(
        void)
{
    XVirtualKeyboardInputContext* ctx = XVirtualKeyboardInputContext_instance();
    return ctx ? XVirtualKeyboardInputContext_inputEngine(ctx) : NULL;
}

/* ==================== 焦点与更新驱动（保护头契约） ==================== */

/** @brief 焦点对象 destroyed 防悬垂槽：焦点对象析构即摘借用指针并清
 *         缓存（悬野指针后续 update 查询会读已释放内存——hints/包围文
 *         本查询读到垃圾值且构成崩溃面）。 */
static void xvkc_focusDestroyedSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboardInputContext* self = (XVirtualKeyboardInputContext*)receiver;
    XVirtualKeyboardInputContextPrivate* priv;
    (void)args;
    if (!self) return;
    priv = xvkc_priv(self);
    if (!priv) return;
    priv->m_focusObject = NULL;
    if (priv->m_focusConn) {
        XObject_disconnect_2(priv->m_focusConn);
        priv->m_focusConn = NULL;
    }
    XVirtualKeyboardInputContext_update(self, 0);
}

void XVirtualKeyboardInputContext_setFocusObject(
        XVirtualKeyboardInputContext* self, XObject* focusObject)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv || priv->m_focusObject == focusObject) return;
    if (priv->m_focusConn) {
        XObject_disconnect_2(priv->m_focusConn);
        priv->m_focusConn = NULL;
    }
    priv->m_focusObject = focusObject;
    if (focusObject && !XClassIsVtableNull(focusObject))
        priv->m_focusConn = XObject_connect_1(
            focusObject, XSignal(XObject_destroyed_signal), (XObject*)self,
            xvkc_focusDestroyedSlot, XConnectionType_Direct);
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_inputItemChanged_signal(NULL),
              NULL);
    XVirtualKeyboardInputContext_update(self, 0);
}

/** @brief 向焦点控件查询整型项；无结果返回 0。 */
static int xvkc_queryInt(XVirtualKeyboardInputContextPrivate* priv,
                         XInputMethodQuery query)
{
    XVariant* value;
    int out = 0;
    if (!priv || !priv->m_focusObject) return 0;
    value = XInputMethod_defaultQueryHandler(priv->m_focusObject, query,
                                             NULL, NULL);
    if (value) {
        out = XVariant_toInt32(value);
        XClassDelete(value);
    }
    return out;
}

/** @brief 向焦点控件查询矩形项；无结果置零。 */
static XRectF xvkc_queryRect(XVirtualKeyboardInputContextPrivate* priv,
                             XInputMethodQuery query)
{
    XVariant* value;
    XRectF out;
    XMemset(&out, 0, sizeof(out));
    if (!priv || !priv->m_focusObject) return out;
    value = XInputMethod_defaultQueryHandler(priv->m_focusObject, query,
                                             NULL, NULL);
    if (value) {
        void* ref = XVariant_toRef(value, XVariantType_User);
        if (ref && XVariant_dataSize(value) >= sizeof(XRectF))
            out = *(const XRectF*)ref;
        XClassDelete(value);
    }
    return out;
}

void XVirtualKeyboardInputContext_update(XVirtualKeyboardInputContext* self,
                                         uint32_t queries)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    uint32_t widgetHints;
    uint32_t effective;
    int cursorPos;
    int anchorPos;
    XRectF cursorRect;
    XRectF anchorRect;
    if (!priv) return;
    (void)queries; /* 本轮恒全量刷新（查询廉价；增量位掩码无消费面）。 */
    /* hints：控件级查询 + Settings OR 叠加（Qt context_p :406-410 口径）。 */
    widgetHints = (uint32_t)xvkc_queryInt(priv, XInputMethodQuery_ImHints);
    effective = widgetHints |
                XVirtualKeyboardSettings_inputMethodHints(
                    XVirtualKeyboardSettings_instance());
    if (effective != priv->m_effectiveHints ||
        widgetHints != priv->m_widgetHints) {
        priv->m_widgetHints = widgetHints;
        priv->m_effectiveHints = effective;
        /* hints 变化→reset（Qt :463-465 口径）。 */
        if (priv->m_engine) XVirtualKeyboardInputEngine_reset(priv->m_engine);
        xvkc_emit(self, (size_t)
                      XVirtualKeyboardInputContext_inputMethodHintsChanged_signal(
                          NULL),
                  NULL);
    }
    cursorPos = xvkc_queryInt(priv, XInputMethodQuery_ImCursorPosition);
    anchorPos = xvkc_queryInt(priv, XInputMethodQuery_ImAnchorPosition);
    cursorRect = xvkc_queryRect(priv, XInputMethodQuery_ImCursorRectangle);
    anchorRect = xvkc_queryRect(priv, XInputMethodQuery_ImAnchorRectangle);
    if (cursorPos != priv->m_cursorPosition) {
        priv->m_cursorPosition = cursorPos;
        xvkc_emit(self, (size_t)
                      XVirtualKeyboardInputContext_cursorPositionChanged_signal(
                          NULL),
                  NULL);
    }
    if (anchorPos != priv->m_anchorPosition) {
        priv->m_anchorPosition = anchorPos;
        xvkc_emit(self, (size_t)
                      XVirtualKeyboardInputContext_anchorPositionChanged_signal(
                          NULL),
                  NULL);
    }
    if (XMemcmp(&cursorRect, &priv->m_cursorRectangle, sizeof(XRectF)) != 0) {
        priv->m_cursorRectangle = cursorRect;
        xvkc_emit(self, (size_t)
                      XVirtualKeyboardInputContext_cursorRectangleChanged_signal(
                          NULL),
                  NULL);
    }
    if (XMemcmp(&anchorRect, &priv->m_anchorRectangle, sizeof(XRectF)) != 0) {
        priv->m_anchorRectangle = anchorRect;
        xvkc_emit(self, (size_t)
                      XVirtualKeyboardInputContext_anchorRectangleChanged_signal(
                          NULL),
                  NULL);
    }
}

void XVirtualKeyboardInputContext_reset(XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv) return;
    if (priv->m_preedit[0]) {
        priv->m_preedit[0] = '\0';
        xvkc_emit(self, (size_t)
                      XVirtualKeyboardInputContext_preeditTextChanged_signal(
                          NULL),
                  NULL);
    }
    xvkc_setShiftActive(self, false);
    if (priv->m_engine) XVirtualKeyboardInputEngine_reset(priv->m_engine);
}

/* ==================== ShiftHandler（Qt shifthandler.cpp 口径） ==================== */

static void xvkc_setShiftActive(XVirtualKeyboardInputContext* self,
                                bool active)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv || priv->m_shiftActive == active) return;
    priv->m_shiftActive = active;
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_shiftActiveChanged_signal(NULL),
              NULL);
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_uppercaseChanged_signal(NULL),
              NULL);
}

static void xvkc_setCapsLockActive(XVirtualKeyboardInputContext* self,
                                   bool active)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv || priv->m_capsLockActive == active) return;
    priv->m_capsLockActive = active;
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_capsLockActiveChanged_signal(
                      NULL),
              NULL);
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_uppercaseChanged_signal(NULL),
              NULL);
}

/** @brief autoCapitalizationEnabled 判据（shifthandler.cpp:244-247 口
 *         径：8 位 hints 滤波 + Pinyin 系模式滤波）。 */
static bool xvkc_autoCapAllowed(const XVirtualKeyboardInputContextPrivate* priv)
{
    uint32_t hints = priv->m_effectiveHints;
    if (hints & (XInputMethodHint_NoAutoUppercase |
                 XInputMethodHint_UppercaseOnly |
                 XInputMethodHint_LowercaseOnly |
                 XInputMethodHint_EmailCharactersOnly |
                 XInputMethodHint_UrlCharactersOnly |
                 XInputMethodHint_DialableCharactersOnly |
                 XInputMethodHint_FormattedNumbersOnly |
                 XInputMethodHint_DigitsOnly))
        return false;
    /* noAutoUppercaseInputModeFilter：Pinyin 系模式关自动大写。 */
    if (priv->m_engine) {
        int mode = XVirtualKeyboardInputEngine_inputMode(priv->m_engine);
        if (mode == (int)XVirtualKeyboardInputEngineInputMode_Pinyin ||
            mode ==
                (int)XVirtualKeyboardInputEngineInputMode_FullwidthLatin)
            return false;
    }
    return true;
}

void XVirtualKeyboardInputContext_toggleShift(
        XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    int64_t now;
    int interval;
    uint32_t hints;
    if (!priv) return;
    now = XDateTime_currentMSecsSinceEpoch();
    interval = 400;
    {
        XStyleHints* styleHints = XGuiApplication_styleHints();
        if (styleHints)
            interval = XStyleHints_mouseDoubleClickInterval(styleHints);
    }
    hints = priv->m_effectiveHints;
    /* 双击判定（数据源=XStyleHints；shifthandler.cpp:216-220 口径）。 */
    if (priv->m_lastToggleMs != 0 &&
        now - priv->m_lastToggleMs <= (int64_t)interval) {
        xvkc_setCapsLockActive(self, true);
        xvkc_setShiftActive(self, false);
        priv->m_lastToggleMs = 0; /* 双击消费，避免三击连判。 */
        return;
    }
    priv->m_lastToggleMs = now;
    if (priv->m_capsLockActive) {
        xvkc_setCapsLockActive(self, false); /* capsLock 下单击解除。 */
        return;
    }
    /* toggleShiftEnabled = !(UppercaseOnly|LowercaseOnly)（:248）。 */
    if (hints & (XInputMethodHint_UppercaseOnly |
                 XInputMethodHint_LowercaseOnly))
        return;
    xvkc_setShiftActive(self, !priv->m_shiftActive);
}

void XVirtualKeyboardInputContext_autoCapitalize(
        XVirtualKeyboardInputContext* self, int cursorPosition,
        bool composing)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    uint32_t hints;
    if (!priv) return;
    hints = priv->m_effectiveHints;
    if (!xvkc_autoCapAllowed(priv)) return;
    if (composing) {
        xvkc_setShiftActive(self, false); /* 组串非空→shift 关（:276）。 */
        return;
    }
    if (hints & XInputMethodHint_PreferLowercase) return;
    if (cursorPosition == 0) {
        xvkc_setShiftActive(self, true); /* 光标 0→开（:280）。 */
        return;
    }
    /* 句末字符 ".!?¡¿" + 空格→再开大写（:283-290）；经环绕文本查询。 */
    {
        XVariant* value = NULL;
        if (priv->m_focusObject)
            value = XInputMethod_defaultQueryHandler(
                priv->m_focusObject, XInputMethodQuery_ImSurroundingText,
                NULL, NULL);
        if (value) {
            const XString* text = XVariant_toString_const(value);
            const char* utf8 = text ? XString_toUtf8(text) : NULL;
            int len = utf8 ? (int)XStrlen(utf8) : 0;
            if (len > 0 && cursorPosition > 0 && cursorPosition <= len) {
                char prev = utf8[cursorPosition - 1];
                char prev2 = cursorPosition >= 2 ? utf8[cursorPosition - 2]
                                                 : '\0';
                if (prev == ' ' &&
                    (prev2 == '.' || prev2 == '!' || prev2 == '?' ||
                     prev2 == (char)0xC2 /* ¡¿ UTF-8 首字节 */))
                    xvkc_setShiftActive(self, true);
                else
                    xvkc_setShiftActive(self, false);
            } else {
                xvkc_setShiftActive(self, false);
            }
            XClassDelete(value);
        }
    }
}

/* ==================== 状态查询 ==================== */

bool XVirtualKeyboardInputContext_isShiftActive(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? priv->m_shiftActive : false;
}

bool XVirtualKeyboardInputContext_isCapsLockActive(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? priv->m_capsLockActive : false;
}

bool XVirtualKeyboardInputContext_isUppercase(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? (priv->m_shiftActive || priv->m_capsLockActive) : false;
}

int XVirtualKeyboardInputContext_anchorPosition(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? priv->m_anchorPosition : 0;
}

int XVirtualKeyboardInputContext_cursorPosition(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? priv->m_cursorPosition : 0;
}

uint32_t XVirtualKeyboardInputContext_inputMethodHints(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? priv->m_effectiveHints
                : XVirtualKeyboardSettings_inputMethodHints(
                      XVirtualKeyboardSettings_instance());
}

XString* XVirtualKeyboardInputContext_preeditText(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv) return NULL;
    return XString_create_utf8(priv->m_preedit);
}

int XVirtualKeyboardInputContext_preeditTextAttributes(
        const XVirtualKeyboardInputContext* self)
{
    (void)self;
    return 0; /* 无属性面（本轮 scopeOut）。 */
}

void XVirtualKeyboardInputContext_setPreeditText_2(
        XVirtualKeyboardInputContext* self, const char* text)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv) return;
    if (!text) text = "";
    if (XStrcmp(priv->m_preedit, text) == 0) return;
    XStrncpy(priv->m_preedit, text, sizeof(priv->m_preedit));
    priv->m_preedit[XVKC_PREEDIT_MAX] = '\0';
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_preeditTextChanged_signal(NULL),
              NULL);
}

XString* XVirtualKeyboardInputContext_surroundingText(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    XVariant* value;
    XString* out = NULL;
    if (!priv || !priv->m_focusObject) return XString_create_utf8("");
    value = XInputMethod_defaultQueryHandler(
        priv->m_focusObject, XInputMethodQuery_ImSurroundingText, NULL, NULL);
    if (value) {
        const XString* text = XVariant_toString_const(value);
        if (text) out = XString_create_copy(text);
        XClassDelete(value);
    }
    return out ? out : XString_create_utf8("");
}

XString* XVirtualKeyboardInputContext_selectedText(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    XVariant* value;
    XString* out = NULL;
    if (!priv || !priv->m_focusObject) return XString_create_utf8("");
    value = XInputMethod_defaultQueryHandler(
        priv->m_focusObject, XInputMethodQuery_ImCurrentSelection, NULL, NULL);
    if (value) {
        const XString* text = XVariant_toString_const(value);
        if (text) out = XString_create_copy(text);
        XClassDelete(value);
    }
    return out ? out : XString_create_utf8("");
}

XRectF XVirtualKeyboardInputContext_anchorRectangle(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    XRectF zero;
    XMemset(&zero, 0, sizeof(zero));
    return priv ? priv->m_anchorRectangle : zero;
}

XRectF XVirtualKeyboardInputContext_cursorRectangle(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    XRectF zero;
    XRectF_init(&zero, 0.0f, 0.0f, 0.0f, 0.0f);
    return priv ? priv->m_cursorRectangle : zero;
}

bool XVirtualKeyboardInputContext_isAnimating(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? priv->m_animating : false;
}

void XVirtualKeyboardInputContext_setAnimating(
        XVirtualKeyboardInputContext* self, bool isAnimating)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv || priv->m_animating == isAnimating) return;
    priv->m_animating = isAnimating;
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_animatingChanged_signal(NULL),
              NULL);
}

XString* XVirtualKeyboardInputContext_locale(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return XString_create_utf8(priv ? priv->m_locale : "zh_CN");
}

XObject* XVirtualKeyboardInputContext_inputItem(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? priv->m_focusObject : NULL;
}

XVirtualKeyboardInputEngine* XVirtualKeyboardInputContext_inputEngine(
        const XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    return priv ? priv->m_engine : NULL;
}

bool XVirtualKeyboardInputContext_isSelectionControlVisible(
        const XVirtualKeyboardInputContext* self)
{
    (void)self;
    return false; /* 无选区基建（scopeOut）。 */
}

bool XVirtualKeyboardInputContext_anchorRectIntersectsClipRect(
        const XVirtualKeyboardInputContext* self)
{
    (void)self;
    return false; /* 无裁剪交集基建；零矩形恒不相交。 */
}

bool XVirtualKeyboardInputContext_cursorRectIntersectsClipRect(
        const XVirtualKeyboardInputContext* self)
{
    (void)self;
    return false;
}

XVirtualKeyboardObserver* XVirtualKeyboardInputContext_keyboardObserver(
        const XVirtualKeyboardInputContext* self)
{
    (void)self;
    return XVirtualKeyboardObserver_instance();
}

/* ==================== 操作 ==================== */

void XVirtualKeyboardInputContext_sendKeyClick(
        XVirtualKeyboardInputContext* self, int key, const char* text,
        uint32_t modifiers)
{
    if (!self) return;
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_keyEventRequested_signal(
                      NULL, 0, NULL, 0),
              XVarList_Create(XVar(int, key), XVar(const char*, text),
                              XVar(uint32_t, modifiers)));
}

void XVirtualKeyboardInputContext_commit(XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv || !priv->m_preedit[0]) return;
    XVirtualKeyboardInputContext_commit_2(self, priv->m_preedit);
    priv->m_preedit[0] = '\0';
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_preeditTextChanged_signal(NULL),
              NULL);
}

void XVirtualKeyboardInputContext_commit_2(
        XVirtualKeyboardInputContext* self, const char* text)
{
    if (!self || !text || !text[0]) return;
    /* 【commit 落地契约】公共信号 → 已持 m_target 的默认面板执行既有
       写入链；无面板连接时丢弃（文档化，Qt 无平台集成层同型）。 */
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_commitRequested_signal(NULL,
                                                                      NULL),
              XVarList_Create(XVar(const char*, text)));
}

void XVirtualKeyboardInputContext_clear(XVirtualKeyboardInputContext* self)
{
    XVirtualKeyboardInputContextPrivate* priv = xvkc_priv(self);
    if (!priv || !priv->m_preedit[0]) return;
    priv->m_preedit[0] = '\0';
    xvkc_emit(self, (size_t)
                  XVirtualKeyboardInputContext_preeditTextChanged_signal(NULL),
              NULL);
}

void XVirtualKeyboardInputContext_setSelectionOnFocusObject(
        XVirtualKeyboardInputContext* self, const XPointF* anchorPos,
        const XPointF* cursorPos)
{
    (void)self; (void)anchorPos; (void)cursorPos;
    /* 无选区基建（scopeOut）：登记入参后空操作。 */
}

/* ==================== 信号（纯 ID getter） ==================== */

void* XVirtualKeyboardInputContext_preeditTextChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_preeditTextChanged_signal;
}

void* XVirtualKeyboardInputContext_inputMethodHintsChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_inputMethodHintsChanged_signal;
}

void* XVirtualKeyboardInputContext_surroundingTextChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_surroundingTextChanged_signal;
}

void* XVirtualKeyboardInputContext_selectedTextChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_selectedTextChanged_signal;
}

void* XVirtualKeyboardInputContext_anchorPositionChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_anchorPositionChanged_signal;
}

void* XVirtualKeyboardInputContext_cursorPositionChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_cursorPositionChanged_signal;
}

void* XVirtualKeyboardInputContext_anchorRectangleChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_anchorRectangleChanged_signal;
}

void* XVirtualKeyboardInputContext_cursorRectangleChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_cursorRectangleChanged_signal;
}

void* XVirtualKeyboardInputContext_shiftActiveChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_shiftActiveChanged_signal;
}

void* XVirtualKeyboardInputContext_capsLockActiveChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_capsLockActiveChanged_signal;
}

void* XVirtualKeyboardInputContext_uppercaseChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_uppercaseChanged_signal;
}

void* XVirtualKeyboardInputContext_animatingChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_animatingChanged_signal;
}

void* XVirtualKeyboardInputContext_localeChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_localeChanged_signal;
}

void* XVirtualKeyboardInputContext_inputItemChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_inputItemChanged_signal;
}

void* XVirtualKeyboardInputContext_selectionControlVisibleChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_selectionControlVisibleChanged_signal;
}

void* XVirtualKeyboardInputContext_anchorRectIntersectsClipRectChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_anchorRectIntersectsClipRectChanged_signal;
}

void* XVirtualKeyboardInputContext_cursorRectIntersectsClipRectChanged_signal(
        XVirtualKeyboardInputContext* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_cursorRectIntersectsClipRectChanged_signal;
}

void* XVirtualKeyboardInputContext_commitRequested_signal(
        XVirtualKeyboardInputContext* self, const char* text)
{
    (void)self; (void)text;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_commitRequested_signal;
}

void* XVirtualKeyboardInputContext_keyEventRequested_signal(
        XVirtualKeyboardInputContext* self, int key, const char* text,
        uint32_t modifiers)
{
    (void)self; (void)key; (void)text; (void)modifiers;
    return (void*)(size_t)
        XVirtualKeyboardInputContext_keyEventRequested_signal;
}

#endif /* XVIRTUALKEYBOARD_ON */
