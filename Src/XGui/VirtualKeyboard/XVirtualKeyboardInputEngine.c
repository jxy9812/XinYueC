/**
 * @file       XVirtualKeyboardInputEngine.c
 * @brief      XVirtualKeyboardInputEngine 输入引擎实现（虚键族/装配链/
 *             候选模型/locale 工厂注册表/长按重复）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardInputEngine.h"
#include "XVirtualKeyboardInputEngine_Protected.h"
#include "XVirtualKeyboardAbstractInputMethod_Protected.h"
#include "XVirtualKeyboardSelectionListModel_Protected.h"
#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardSettings.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XPrintf.h"

/** @brief 输入模式集容量（InputMode 全集 20 + 余量）。 */
#define XVKE_INPUT_MODE_MAX 32
/** @brief 活动键文本拷贝容量（单字符键足够，含 NUL 余量）。 */
#define XVKE_ACTIVE_TEXT_MAX 32

/**
 * @brief locale 工厂注册表条目。
 */
typedef struct XvkeLocaleFactory
{
    char m_locale[32];                        /**< 区域语言（UTF-8）。 */
    XVirtualKeyboardInputMethodFactory m_factory; /**< 工厂函数。 */
} XvkeLocaleFactory;

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardInputEnginePrivate
{
    XVirtualKeyboardInputContext* m_context;  /**< 上下文（借用）。 */
    XVirtualKeyboardAbstractInputMethod* m_inputMethod; /**< 当前输入法（借用）。 */
    XConnection* m_listsConn;                 /**< 输入法 selectionListsChanged 连接。 */
    XConnection* m_listChangedConn;           /**< 输入法 selectionListChanged 连接。 */
    XConnection* m_activeChangedConn;         /**< 输入法 selectionListActiveItemChanged 连接。 */
    int m_activeKey;                          /**< 活动键（0=无）。 */
    char m_activeText[XVKE_ACTIVE_TEXT_MAX];  /**< 活动键文本拷贝。 */
    uint32_t m_activeModifiers;               /**< 活动键修饰位。 */
    int m_previousKey;                        /**< 先前键（0=无）。 */
    int m_inputModes[XVKE_INPUT_MODE_MAX];    /**< 输入模式集缓存。 */
    int m_inputModeCount;                     /**< 模式数。 */
    int m_inputMode;                          /**< 当前模式（默认 Latin）。 */
    XVirtualKeyboardSelectionListModel* m_wordCandidateListModel; /**< 候选模型（拥有）。 */
    bool m_wordCandidateListVisibleHint;      /**< 候选可见提示（默认 false）。 */
    XTimerId m_repeatTimer;                   /**< 长按重复定时器。 */
    XvkeLocaleFactory m_factories[XVIRTUALKEYBOARD_LOCALE_FACTORY_MAX]; /**< 注册表。 */
    int m_factoryCount;                       /**< 注册表条数。 */
    char m_locale[32];                        /**< 当前区域语言（默认 "zh_CN"）。 */
} XVirtualKeyboardInputEnginePrivate;

/** @brief 内部取私有块。 */
static XVirtualKeyboardInputEnginePrivate* xvke_priv(
        const XVirtualKeyboardInputEngine* self)
{
    return (self && self->m_data)
               ? (XVirtualKeyboardInputEnginePrivate*)self->m_data : NULL;
}

/** @brief 发射信号并管理参数表生命周期。 */
static void xvke_emit(XVirtualKeyboardInputEngine* self, size_t signal,
                      XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args)
        XVarList_delete(args);
}

/* 前向声明（class_init 注册与内部互调用）。 */
static void XVke_deinit(XVirtualKeyboardInputEngine* self);
static void XVke_timerEvent(XObject* object, XTimerEvent* event);
static void xvke_onSelectionListsChanged(XObject* sender, XVarList* args);
static void xvke_onSelectionListChanged(XObject* sender, XVarList* args);
static void xvke_onSelectionListActiveItemChanged(XObject* sender,
                                                  XVarList* args);

XVtable* XVirtualKeyboardInputEngine_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardInputEngine)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, XVke_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVke_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 定时器分派：长按重复节拍；其余链回基类。 */
static void XVke_timerEvent(XObject* object, XTimerEvent* event)
{
    XVirtualKeyboardInputEngine* self = (XVirtualKeyboardInputEngine*)object;
    XVirtualKeyboardInputEnginePrivate* priv;
    XTimerId id;
    if (!self || !event) return;
    priv = xvke_priv(self);
    id = (XTimerId)XTimerEvent_timerId(event);
    if (priv && id == priv->m_repeatTimer) {
        /* 长按重复节拍（Qt engine :704-711 口径）：重投输入法 + 发
         * virtualKeyClicked(isAutoRepeat=true)，随后切 50ms 稳态间隔。 */
        XVirtualKeyboardAbstractInputMethod* im = priv->m_inputMethod;
        const char* repeatText =
            priv->m_activeText[0] ? priv->m_activeText : NULL;
        bool isAutoRepeat = true;
        if (im) {
            XVirtualKeyboardAbstractInputMethod_keyEvent_base(
                im, priv->m_activeKey, repeatText,
                priv->m_activeModifiers);
        }
        xvke_emit(self, (size_t)
                      XVirtualKeyboardInputEngine_virtualKeyClicked_signal(
                          NULL, 0, NULL, 0, false),
                  XVarList_Create(XVar(int, priv->m_activeKey),
                                  XVar(const char*, repeatText),
                                  XVar(uint32_t, priv->m_activeModifiers),
                                  XVar(bool, isAutoRepeat)));
        XObject_killTimer((XObject*)self, priv->m_repeatTimer);
        priv->m_repeatTimer = XObject_startTimer_ms(
            (XObject*)self, XVIRTUALKEYBOARD_REPEAT_MS,
            XTimerType_CoarseTimer);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/** @brief 停长按重复定时器（幂等）。 */
static void xvke_stopRepeat(XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || priv->m_repeatTimer == XTIMER_INVALID_ID) return;
    XObject_killTimer((XObject*)self, priv->m_repeatTimer);
    priv->m_repeatTimer = XTIMER_INVALID_ID;
}

/** @brief 反初始化：断连接、释放模型与私有块后调父类。 */
static void XVke_deinit(XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv;
    if (!self) return;
    priv = xvke_priv(self);
    if (priv) {
        if (priv->m_listsConn) {
            XObject_disconnect_2(priv->m_listsConn);
            priv->m_listsConn = NULL;
        }
        if (priv->m_listChangedConn) {
            XObject_disconnect_2(priv->m_listChangedConn);
            priv->m_listChangedConn = NULL;
        }
        if (priv->m_activeChangedConn) {
            XObject_disconnect_2(priv->m_activeChangedConn);
            priv->m_activeChangedConn = NULL;
        }
        xvke_stopRepeat(self);
        if (priv->m_wordCandidateListModel) {
            XClassDelete(
                priv->m_wordCandidateListModel);
            priv->m_wordCandidateListModel = NULL;
        }
        XFree_System(priv);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XVirtualKeyboardInputEngine_init_ex(XVirtualKeyboardInputEngine* self,
                                         XVirtualKeyboardInputContext* context)
{
    XVirtualKeyboardInputEnginePrivate* priv;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XVirtualKeyboardInputEngine);
    self->m_data = XMalloc_System(sizeof(XVirtualKeyboardInputEnginePrivate));
    if (!self->m_data) return;
    priv = (XVirtualKeyboardInputEnginePrivate*)self->m_data;
    XMemset(priv, 0, sizeof(*priv));
    priv->m_context = context;
    priv->m_repeatTimer = XTIMER_INVALID_ID;
    priv->m_inputMode = (int)XVirtualKeyboardInputEngineInputMode_Latin;
    XStrncpy(priv->m_locale, "zh_CN", sizeof(priv->m_locale));
    /* 候选模型引擎持有、创建即有效（Qt 为惰性创建；提前创建使面板可
       无条件取用，装配后经 updateSelectionListModels 置数据源）。 */
    priv->m_wordCandidateListModel =
        XVirtualKeyboardSelectionListModel_create_engine(
            XVirtualKeyboardSelectionListModelType_WordCandidateList);
}

XVirtualKeyboardInputEngine* XVirtualKeyboardInputEngine_create_forContext(
        XVirtualKeyboardInputContext* context)
{
    XVirtualKeyboardInputEngine* self =
        (XVirtualKeyboardInputEngine*)XMalloc_System(
            sizeof(XVirtualKeyboardInputEngine));
    if (!self) return NULL;
    XVirtualKeyboardInputEngine_init_ex(self, context);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 输入模式集刷新 ==================== */

/** @brief 按当前输入法刷新输入模式集缓存并发 inputModesChanged。 */
static void xvke_refreshInputModes(XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    int count = 0;
    if (!priv) return;
    if (priv->m_inputMethod) {
        count = XVirtualKeyboardAbstractInputMethod_inputModes_base(
            priv->m_inputMethod, priv->m_locale, priv->m_inputModes,
            XVKE_INPUT_MODE_MAX);
        if (count > XVKE_INPUT_MODE_MAX) count = XVKE_INPUT_MODE_MAX;
    }
    priv->m_inputModeCount = count;
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_inputModesChanged_signal(NULL),
              NULL);
}

/* ==================== 装配链 ==================== */

bool XVirtualKeyboardInputEngine_setInputMethod(
        XVirtualKeyboardInputEngine* self,
        XVirtualKeyboardAbstractInputMethod* inputMethod)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    XObject* sender;
    if (!priv || inputMethod == priv->m_inputMethod) return self != NULL;
    /* 旧插法：clearInputMode + 断开候选信号（Qt engine :369-372 口径）。 */
    if (priv->m_inputMethod) {
        XVirtualKeyboardAbstractInputMethod_clearInputMode_base(
            priv->m_inputMethod);
        if (priv->m_listsConn) {
            XObject_disconnect_2(priv->m_listsConn);
            priv->m_listsConn = NULL;
        }
        if (priv->m_listChangedConn) {
            XObject_disconnect_2(priv->m_listChangedConn);
            priv->m_listChangedConn = NULL;
        }
        if (priv->m_activeChangedConn) {
            XObject_disconnect_2(priv->m_activeChangedConn);
            priv->m_activeChangedConn = NULL;
        }
    }
    priv->m_inputMethod = inputMethod;
    /* 新插法：setInputEngine/setInputContext + 连接候选信号
       （Qt engine :374-377 口径）。 */
    if (inputMethod) {
        XVirtualKeyboardAbstractInputMethod_setInputEngine(inputMethod, self);
        XVirtualKeyboardAbstractInputMethod_setInputContext(
            inputMethod, priv->m_context);
        sender = (XObject*)inputMethod;
        if (((XObject*)self)->m_signalSlot && sender->m_signalSlot) {
            priv->m_listsConn = XObject_connect_1(
                sender,
                (size_t)XVirtualKeyboardAbstractInputMethod_selectionListsChanged_signal(
                    NULL),
                (XObject*)self, xvke_onSelectionListsChanged,
                XConnectionType_Direct);
            priv->m_listChangedConn = XObject_connect_1(
                sender,
                (size_t)XVirtualKeyboardAbstractInputMethod_selectionListChanged_signal(
                    NULL, 0),
                (XObject*)self, xvke_onSelectionListChanged,
                XConnectionType_Direct);
            priv->m_activeChangedConn = XObject_connect_1(
                sender,
                (size_t)
                    XVirtualKeyboardAbstractInputMethod_selectionListActiveItemChanged_signal(
                        NULL, 0, 0),
                (XObject*)self, xvke_onSelectionListActiveItemChanged,
                XConnectionType_Direct);
        }
    }
    xvke_refreshInputModes(self);
    XVirtualKeyboardInputEngine_updateSelectionListModels(self);
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_inputMethodChanged_signal(NULL),
              NULL);
    return true;
}

/** @brief 输入法 selectionListsChanged 转发：重建候选模型数据源。 */
static void xvke_onSelectionListsChanged(XObject* sender, XVarList* args)
{
    XVirtualKeyboardInputEngine* self = (XVirtualKeyboardInputEngine*)sender;
    (void)args;
    XVirtualKeyboardInputEngine_updateSelectionListModels(self);
}

/** @brief 输入法 selectionListChanged 转发到候选模型。 */
static void xvke_onSelectionListChanged(XObject* sender, XVarList* args)
{
    XVirtualKeyboardInputEngine* self = (XVirtualKeyboardInputEngine*)sender;
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || !priv->m_wordCandidateListModel || !args) return;
    XVarList_args_1(args, int, listType);
    XVirtualKeyboardSelectionListModel_selectionListChanged(
        priv->m_wordCandidateListModel, listType);
}

/** @brief 输入法 selectionListActiveItemChanged 转发到候选模型。 */
static void xvke_onSelectionListActiveItemChanged(XObject* sender,
                                                  XVarList* args)
{
    XVirtualKeyboardInputEngine* self = (XVirtualKeyboardInputEngine*)sender;
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || !priv->m_wordCandidateListModel || !args) return;
    XVarList_args_2(args, int, listType, int, listIndex);
    XVirtualKeyboardSelectionListModel_selectionListActiveItemChanged(
        priv->m_wordCandidateListModel, listType, listIndex);
}

void XVirtualKeyboardInputEngine_updateSelectionListModels(
        XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    int lists[8];
    int count;
    bool hasWordCandidateList = false;
    int i;
    if (!priv) return;
    if (!priv->m_wordCandidateListModel) return;
    count = priv->m_inputMethod
                ? XVirtualKeyboardAbstractInputMethod_selectionLists_base(
                      priv->m_inputMethod, lists, 8)
                : 0;
    if (count > 8) count = 8;
    for (i = 0; i < count; ++i) {
        if (lists[i] ==
            (int)XVirtualKeyboardSelectionListModelType_WordCandidateList)
            hasWordCandidateList = true;
    }
    /* 失活的类型置空数据源（Qt engine :641-671 口径）。 */
    XVirtualKeyboardSelectionListModel_setDataSource(
        priv->m_wordCandidateListModel,
        hasWordCandidateList ? priv->m_inputMethod : NULL,
        XVirtualKeyboardSelectionListModelType_WordCandidateList);
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_wordCandidateListModelChanged_signal(
                      NULL),
              NULL);
}

/* ==================== locale 工厂注册表 ==================== */

bool XVirtualKeyboardInputEngine_registerInputMethodFactory_for(
        XVirtualKeyboardInputEngine* self, const char* locale,
        XVirtualKeyboardInputMethodFactory factory)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    int i;
    if (!priv || !locale || !locale[0] || !factory) return false;
    for (i = 0; i < priv->m_factoryCount; ++i) {
        if (XStrcmp(priv->m_factories[i].m_locale, locale) == 0) {
            priv->m_factories[i].m_factory = factory; /* 后设者胜。 */
            return true;
        }
    }
    if (priv->m_factoryCount >= XVIRTUALKEYBOARD_LOCALE_FACTORY_MAX) {
        XPrintf("[XVirtualKeyboard] registerFactory: 注册表已满 %d\n",
                (int)XVIRTUALKEYBOARD_LOCALE_FACTORY_MAX);
        return false;
    }
    XStrncpy(priv->m_factories[priv->m_factoryCount].m_locale, locale,
             sizeof(priv->m_factories[0].m_locale));
    priv->m_factories[priv->m_factoryCount].m_factory = factory;
    ++priv->m_factoryCount;
    return true;
}

bool XVirtualKeyboardInputEngine_registerInputMethodFactory(
        const char* locale, XVirtualKeyboardInputMethodFactory factory)
{
    /* 单例便捷面（构造完成后经 instanceEngine 路由）。上下文 init 期
     * 单例尚未落座，走本入口会重入 instance() 无限递归——构造链必须
     * 用 _for 直指引擎（所有权边界：注册表在引擎私有块）。 */
    return XVirtualKeyboardInputEngine_registerInputMethodFactory_for(
        XVirtualKeyboardInputContext_instanceEngine(), locale, factory);
}

XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardInputEngine_createInputMethod(const char* locale)
{
    XVirtualKeyboardInputEngine* self =
        XVirtualKeyboardInputContext_instanceEngine();
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    int i;
    if (!priv || !locale) return NULL;
    for (i = 0; i < priv->m_factoryCount; ++i) {
        if (XStrcmp(priv->m_factories[i].m_locale, locale) == 0)
            return priv->m_factories[i].m_factory();
    }
    return NULL;
}

/* ==================== 虚键族 ==================== */

void XVirtualKeyboardInputEngine_virtualKeyPress(
        XVirtualKeyboardInputEngine* self, int key, const char* text,
        uint32_t modifiers, bool repeat)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv) return;
    if (priv->m_activeKey != 0) {
        /* 上一键未释放：按 Qt 口径先行收尾（release 语义）。 */
        priv->m_previousKey = priv->m_activeKey;
        xvke_emit(self, (size_t)
                      XVirtualKeyboardInputEngine_previousKeyChanged_signal(
                          NULL, 0),
                  XVarList_Create(XVar(int, priv->m_previousKey)));
    }
    priv->m_activeKey = key;
    priv->m_activeModifiers = modifiers;
    if (text) {
        XStrncpy(priv->m_activeText, text, sizeof(priv->m_activeText));
    } else {
        priv->m_activeText[0] = '\0';
    }
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_activeKeyChanged_signal(NULL, 0),
              XVarList_Create(XVar(int, key)));
    xvke_stopRepeat(self);
    if (repeat) {
        priv->m_repeatTimer = XObject_startTimer_ms(
            (XObject*)self, XVIRTUALKEYBOARD_REPEAT_FIRST_MS,
            XTimerType_CoarseTimer);
    }
}

void XVirtualKeyboardInputEngine_virtualKeyCancel(
        XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv) return;
    xvke_stopRepeat(self);
    priv->m_activeKey = 0;
    priv->m_activeText[0] = '\0';
    priv->m_activeModifiers = 0;
}

bool XVirtualKeyboardInputEngine_virtualKeyRelease(
        XVirtualKeyboardInputEngine* self, int key, const char* text,
        uint32_t modifiers)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv) return false;
    (void)key; (void)text; (void)modifiers;
    if (priv->m_activeKey == 0) return false;
    xvke_stopRepeat(self); /* release/cancel 杀重复计时（Qt :291-294）。 */
    priv->m_previousKey = priv->m_activeKey;
    priv->m_activeKey = 0;
    priv->m_activeText[0] = '\0';
    priv->m_activeModifiers = 0;
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_previousKeyChanged_signal(
                      NULL, 0),
              XVarList_Create(XVar(int, priv->m_previousKey)));
    return true;
}

bool XVirtualKeyboardInputEngine_virtualKeyClick(
        XVirtualKeyboardInputEngine* self, int key, const char* text,
        uint32_t modifiers)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    XVirtualKeyboardAbstractInputMethod* im;
    bool consumed = false;
    bool isAutoRepeat = false;
    if (!priv) return false;
    /* 总开关关：虚键吞掉（XVIRTUALKEYBOARD 总开关行为矩阵）。 */
    if (!XVirtualKeyboardSettings_keyboardEnabled(
            XVirtualKeyboardSettings_instance()))
        return false;
    im = priv->m_inputMethod;
    if (im)
        consumed = XVirtualKeyboardAbstractInputMethod_keyEvent_base(
            im, key, text, modifiers);
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_virtualKeyClicked_signal(
                      NULL, 0, NULL, 0, false),
              XVarList_Create(XVar(int, key), XVar(const char*, text),
                              XVar(uint32_t, modifiers),
                              XVar(bool, isAutoRepeat)));
    return consumed;
}

XVirtualKeyboardTrace* XVirtualKeyboardInputEngine_traceBegin(
        XVirtualKeyboardInputEngine* self, int traceId,
        int patternRecognitionMode)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || !priv->m_inputMethod) return NULL;
    return XVirtualKeyboardAbstractInputMethod_traceBegin_base(
        priv->m_inputMethod, traceId, patternRecognitionMode);
}

bool XVirtualKeyboardInputEngine_traceEnd(XVirtualKeyboardInputEngine* self,
                                          XVirtualKeyboardTrace* trace)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || !priv->m_inputMethod || !trace) return false;
    return XVirtualKeyboardAbstractInputMethod_traceEnd_base(
        priv->m_inputMethod, trace);
}

bool XVirtualKeyboardInputEngine_reselect(
        XVirtualKeyboardInputEngine* self, int cursorPosition,
        XVirtualKeyboardInputEngineReselectFlags reselectFlags)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || !priv->m_inputMethod) return false;
    return XVirtualKeyboardAbstractInputMethod_reselect_base(
        priv->m_inputMethod, cursorPosition, reselectFlags);
}

void XVirtualKeyboardInputEngine_clickPreeditText(
        XVirtualKeyboardInputEngine* self, int cursorPosition)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || !priv->m_inputMethod) return;
    XVirtualKeyboardAbstractInputMethod_clickPreeditText_base(
        priv->m_inputMethod, cursorPosition);
}

/* ==================== 状态查询 ==================== */

XVirtualKeyboardInputContext* XVirtualKeyboardInputEngine_inputContext(
        const XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    return priv ? priv->m_context : NULL;
}

int XVirtualKeyboardInputEngine_activeKey(
        const XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    return priv ? priv->m_activeKey : 0;
}

int XVirtualKeyboardInputEngine_previousKey(
        const XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    return priv ? priv->m_previousKey : 0;
}

XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardInputEngine_inputMethod(
        const XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    return priv ? priv->m_inputMethod : NULL;
}

int XVirtualKeyboardInputEngine_inputModes(
        const XVirtualKeyboardInputEngine* self, int* outModes, int maxCount)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    int i;
    int written = 0;
    if (!priv) return 0;
    for (i = 0; i < priv->m_inputModeCount; ++i) {
        if (outModes && written < maxCount)
            outModes[written] = priv->m_inputModes[i];
        ++written;
    }
    return written;
}

int XVirtualKeyboardInputEngine_inputMode(
        const XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    return priv ? priv->m_inputMode
                : (int)XVirtualKeyboardInputEngineInputMode_Latin;
}

bool XVirtualKeyboardInputEngine_setInputMode(
        XVirtualKeyboardInputEngine* self,
        XVirtualKeyboardInputEngineInputMode inputMode)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    int i;
    bool listed = false;
    if (!priv) return false;
    if ((int)inputMode == priv->m_inputMode) return true;
    for (i = 0; i < priv->m_inputModeCount; ++i) {
        if (priv->m_inputModes[i] == (int)inputMode) {
            listed = true;
            break;
        }
    }
    if (!listed) {
        XPrintf("[XVirtualKeyboard] setInputMode: 模式 %d 不在当前输入"
                "模式集内，拒绝\n",
                (int)inputMode);
        return false;
    }
    if (priv->m_inputMethod) {
        if (!XVirtualKeyboardAbstractInputMethod_setInputMode_base(
                priv->m_inputMethod, priv->m_locale, (int)inputMode))
            return false;
    }
    priv->m_inputMode = (int)inputMode;
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_inputModeChanged_signal(NULL),
              NULL);
    return true;
}

XVirtualKeyboardSelectionListModel*
XVirtualKeyboardInputEngine_wordCandidateListModel(
        const XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    return priv ? priv->m_wordCandidateListModel : NULL;
}

bool XVirtualKeyboardInputEngine_wordCandidateListVisibleHint(
        const XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    return priv ? priv->m_wordCandidateListVisibleHint : false;
}

void XVirtualKeyboardInputEngine_setWordCandidateListVisibleHint(
        XVirtualKeyboardInputEngine* self, bool visible)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || priv->m_wordCandidateListVisibleHint == visible) return;
    priv->m_wordCandidateListVisibleHint = visible;
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_wordCandidateListVisibleHintChanged_signal(
                      NULL),
              NULL);
}

int XVirtualKeyboardInputEngine_patternRecognitionModes(
        const XVirtualKeyboardInputEngine* self, int* outModes, int maxCount)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv || !priv->m_inputMethod) return 0;
    return XVirtualKeyboardAbstractInputMethod_patternRecognitionModes_base(
        priv->m_inputMethod, outModes, maxCount);
}

void XVirtualKeyboardInputEngine_reset(XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv) return;
    if (priv->m_inputMethod)
        XVirtualKeyboardAbstractInputMethod_reset_base(priv->m_inputMethod);
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_inputMethodReset_signal(NULL),
              NULL);
}

void XVirtualKeyboardInputEngine_update(XVirtualKeyboardInputEngine* self)
{
    XVirtualKeyboardInputEnginePrivate* priv = xvke_priv(self);
    if (!priv) return;
    if (priv->m_inputMethod)
        XVirtualKeyboardAbstractInputMethod_update_base(priv->m_inputMethod);
    xvke_emit(self, (size_t)
                  XVirtualKeyboardInputEngine_inputMethodUpdate_signal(NULL),
              NULL);
}

/* ==================== 信号（纯 ID getter） ==================== */

void* XVirtualKeyboardInputEngine_virtualKeyClicked_signal(
        XVirtualKeyboardInputEngine* self, int key, const char* text,
        uint32_t modifiers, bool isAutoRepeat)
{
    (void)self; (void)key; (void)text; (void)modifiers; (void)isAutoRepeat;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_virtualKeyClicked_signal;
}

void* XVirtualKeyboardInputEngine_activeKeyChanged_signal(
        XVirtualKeyboardInputEngine* self, int key)
{
    (void)self; (void)key;
    return (void*)(size_t)XVirtualKeyboardInputEngine_activeKeyChanged_signal;
}

void* XVirtualKeyboardInputEngine_previousKeyChanged_signal(
        XVirtualKeyboardInputEngine* self, int key)
{
    (void)self; (void)key;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_previousKeyChanged_signal;
}

void* XVirtualKeyboardInputEngine_inputMethodChanged_signal(
        XVirtualKeyboardInputEngine* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_inputMethodChanged_signal;
}

void* XVirtualKeyboardInputEngine_inputMethodReset_signal(
        XVirtualKeyboardInputEngine* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_inputMethodReset_signal;
}

void* XVirtualKeyboardInputEngine_inputMethodUpdate_signal(
        XVirtualKeyboardInputEngine* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_inputMethodUpdate_signal;
}

void* XVirtualKeyboardInputEngine_inputModesChanged_signal(
        XVirtualKeyboardInputEngine* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_inputModesChanged_signal;
}

void* XVirtualKeyboardInputEngine_inputModeChanged_signal(
        XVirtualKeyboardInputEngine* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_inputModeChanged_signal;
}

void* XVirtualKeyboardInputEngine_patternRecognitionModesChanged_signal(
        XVirtualKeyboardInputEngine* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_patternRecognitionModesChanged_signal;
}

void* XVirtualKeyboardInputEngine_wordCandidateListModelChanged_signal(
        XVirtualKeyboardInputEngine* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_wordCandidateListModelChanged_signal;
}

void* XVirtualKeyboardInputEngine_wordCandidateListVisibleHintChanged_signal(
        XVirtualKeyboardInputEngine* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardInputEngine_wordCandidateListVisibleHintChanged_signal;
}

#endif /* XVIRTUALKEYBOARD_ON */
