/**
 * @file       XShortcutAlignTest.c
 * @brief      XShortcut × Qt 6.8 QShortcut 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtGui/qshortcut.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/gui/kernel/qshortcut.cpp、
 *             qshortcutmap.cpp。
 *             关键语义：
 *             - 构造默认 enabled=true、autoRepeat=true、context=WindowShortcut、
 *               空序列（qshortcut.cpp:333-360、418-470）；
 *             - setKey 的 key 相等即替换序列；匹配由 QShortcutMap 统一管理，
 *               只有 **enabled** 的条目参与（qshortcutmap.cpp:373
 *               `(*it).enabled`）；
 *             - 同键同上下文多命中 ⇒ 派发 activatedAmbiguously 而非
 *               activated（QShortcutMap::dispatch）；
 *             - 上下文过滤：Application 恒可达；Widget 仅父控件自身聚焦；
 *               WidgetWithChildren 含后代；Window 同顶层窗口。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON

#include "XShortcut.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"
#include "XVarList.h"
#include "XClass.h"
#include "XEvent.h"           /* XKeyEvent_init/setAutoRepeat（P3-33 事件形）。 */
#include "XCoreApplication.h" /* sendEvent：把按键投给桥接窗口走派发。 */

/** @brief activated / activatedAmbiguously 计数。 */
static int g_scActivated;
static int g_scAmbiguous;

static void sc_activatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_scActivated;
}

static void sc_ambiguousSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_scAmbiguous;
}

static void sc_reset(void)
{
    g_scActivated = 0;
    g_scAmbiguous = 0;
}

static void sc_connectAll(XShortcut* shortcut)
{
    XObject_connect((XObject*)shortcut,
                      (size_t)XShortcut_activated_signal(NULL),
                      (XObject*)shortcut, sc_activatedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)shortcut,
                      (size_t)XShortcut_activatedAmbiguously_signal(NULL),
                      (XObject*)shortcut, sc_ambiguousSlot,
                      XConnectionType_Direct);
}

/* ==================== ① 默认值 + 注册表 + 启用位 ==================== */

XQ_TEST(XShortcut, defaultsRegistryAndEnabled)
{
    XWidget* host = XWidget_create(NULL, 0);
    XShortcut* shortcut;

    XQ_REQUIRE(host != NULL);
    shortcut = XShortcut_create_2((int)XKey_F5, (XObject*)host);
    XQ_REQUIRE(shortcut != NULL);

    /* 构造默认（qshortcut.cpp:333-360、418-470）。 */
    XQ_EQ_I64(XShortcut_key(shortcut), (int)XKey_F5);
    XQ_CHECK(XShortcut_isEnabled(shortcut));
    XQ_CHECK(XShortcut_autoRepeat(shortcut));
    XQ_EQ_I64((int)XShortcut_context(shortcut),
              (int)XShortcutContext_WindowShortcut);
    XQ_CHECK(XShortcut_whatsThis(shortcut) == NULL);

    /* create 即进全局注册表（module 静态 XVector），match 可命中。 */
    XQ_CHECK(XShortcut_match((int)XKey_F5,
                             XShortcutContext_WindowShortcut, host) ==
             shortcut);
    XQ_CHECK(XShortcut_match((int)XKey_F7,
                             XShortcutContext_WindowShortcut, host) == NULL);

    /* 禁用：不再参与匹配（qshortcutmap.cpp:373 的 enabled 位）。 */
    XShortcut_setEnabled(shortcut, false);
    XQ_CHECK(!XShortcut_isEnabled(shortcut));
    XQ_CHECK(XShortcut_match((int)XKey_F5,
                             XShortcutContext_WindowShortcut, host) == NULL);
    XShortcut_setEnabled(shortcut, true);
    XQ_CHECK(XShortcut_match((int)XKey_F5,
                             XShortcutContext_WindowShortcut, host) ==
             shortcut);

    /* 换键往返（新键不与其他用例的按键冲突）。 */
    XShortcut_setKey(shortcut, (int)XKey_F7);
    XQ_EQ_I64(XShortcut_key(shortcut), (int)XKey_F7);
    XQ_CHECK(XShortcut_match((int)XKey_F5,
                             XShortcutContext_WindowShortcut, host) == NULL);

    /* 多序列（对标 Qt 6 QShortcut::keys()/setKeys()，gaps 208）：
     * 任一备选序列命中即匹配；key() 取首条，setKey 等价 setKeys 单条。 */
    {
        int keys[2];
        keys[0] = (int)XKey_F5;
        keys[1] = (int)XKey_F6;
        XShortcut_setKeys(shortcut, keys, 2);
        XQ_EQ_I64(XShortcut_keyCount(shortcut), 2);
        XQ_EQ_I64(XShortcut_keyAt(shortcut, 0), (int)XKey_F5);
        XQ_EQ_I64(XShortcut_keyAt(shortcut, 1), (int)XKey_F6);
        XQ_EQ_I64(XShortcut_key(shortcut), (int)XKey_F5); /* keys().value(0) */
        XQ_EQ_I64(XShortcut_keyAt(shortcut, 2), 0);       /* 越界 */
        XQ_EQ_I64(XShortcut_keyAt(shortcut, -1), 0);
        XQ_CHECK(XShortcut_match((int)XKey_F5,
                                 XShortcutContext_WindowShortcut, host) ==
                 shortcut);
        XQ_CHECK(XShortcut_match((int)XKey_F6,
                                 XShortcutContext_WindowShortcut, host) ==
                 shortcut);
        XQ_CHECK(XShortcut_match((int)XKey_F7,
                                 XShortcutContext_WindowShortcut, host) ==
                 NULL);
        /* setKey 是单序列替换（清空其它备选）。 */
        XShortcut_setKey(shortcut, (int)XKey_F7);
        XQ_EQ_I64(XShortcut_keyCount(shortcut), 1);
        XQ_EQ_I64(XShortcut_keyAt(shortcut, 0), (int)XKey_F7);
        /* setKeys(NULL,0) 清空 → 不再命中。 */
        XShortcut_setKeys(shortcut, NULL, 0);
        XQ_EQ_I64(XShortcut_keyCount(shortcut), 0);
        XQ_CHECK(XShortcut_match((int)XKey_F7,
                                 XShortcutContext_WindowShortcut, host) ==
                 NULL);
        XShortcut_setKey(shortcut, (int)XKey_F7);
    }

    /* context / autoRepeat 往返。 */
    XShortcut_setContext(shortcut, XShortcutContext_ApplicationShortcut);
    XQ_EQ_I64((int)XShortcut_context(shortcut),
              (int)XShortcutContext_ApplicationShortcut);
    XQ_CHECK(XShortcut_match((int)XKey_F7,
                             XShortcutContext_ApplicationShortcut,
                             NULL) == shortcut); /* 应用级不依赖焦点 */
    XShortcut_setContext(shortcut, XShortcutContext_WidgetShortcut);
    XQ_CHECK(XShortcut_match((int)XKey_F7, XShortcutContext_WidgetShortcut,
                             host) == shortcut);
    XQ_CHECK(XShortcut_match((int)XKey_F7, XShortcutContext_WidgetShortcut,
                             NULL) == NULL);
    XShortcut_setAutoRepeat(shortcut, false);
    XQ_CHECK(!XShortcut_autoRepeat(shortcut));
    XShortcut_setAutoRepeat(shortcut, true);
    XQ_CHECK(XShortcut_autoRepeat(shortcut));

    /* whatsThis：拷贝创建、深拷贝读取（借用/拷贝所有权契约）。 */
    XShortcut_setWhatsThis_2(shortcut, "帮助文本");
    {
        XString* text = XShortcut_whatsThis(shortcut);
        XQ_REQUIRE(text != NULL);
        XQ_EQ_STR(XString_toUtf8(text), "帮助文本");
        XClassDelete((XClass*)text);
    }
    XShortcut_setWhatsThis(shortcut, NULL);
    XQ_CHECK(XShortcut_whatsThis(shortcut) == NULL);

    /* 对象释放即注销注册表。 */
    XClassDelete((XClass*)shortcut);
    XQ_CHECK(XShortcut_match((int)XKey_F7, XShortcutContext_WidgetShortcut,
                             host) == NULL);
    XClassDelete((XClass*)host);
}

/* ==================== ② 激活：启用闸 + 歧义派发 ==================== */

XQ_TEST(XShortcut, activationAndAmbiguity)
{
    XWidget* host = XWidget_create(NULL, 0);
    XShortcut* s1;
    XShortcut* s2;

    XQ_REQUIRE(host != NULL);
    s1 = XShortcut_create_2((int)XKey_F7, (XObject*)host);
    XQ_REQUIRE(s1 != NULL);
    XShortcut_setContext(s1, XShortcutContext_WidgetShortcut);
    sc_reset();
    sc_connectAll(s1);

    /* 禁用的快捷键不发射任何信号（QShortcutMap 的 enabled 过滤；
       旧实现会误发 activated）。 */
    XShortcut_setEnabled(s1, false);
    XShortcut_activate(s1);
    XQ_EQ_I64(g_scActivated, 0);
    XQ_EQ_I64(g_scAmbiguous, 0);

    /* 启用且无歧义 ⇒ activated。 */
    XShortcut_setEnabled(s1, true);
    XShortcut_activate(s1);
    XQ_EQ_I64(g_scActivated, 1);
    XQ_EQ_I64(g_scAmbiguous, 0);

    /* 同键同上下文双命中 ⇒ 两者都收 activatedAmbiguously、都不收
       activated（Qt 歧义派发）。 */
    s2 = XShortcut_create_2((int)XKey_F7, (XObject*)host);
    XQ_REQUIRE(s2 != NULL);
    XShortcut_setContext(s2, XShortcutContext_WidgetShortcut);
    sc_connectAll(s2);
    sc_reset();
    XShortcut_activate(s1);
    XQ_EQ_I64(g_scActivated, 0);
    XQ_EQ_I64(g_scAmbiguous, 2);

    /* 移除其一后歧义消解。 */
    XClassDelete((XClass*)s2);
    sc_reset();
    XShortcut_activate(s1);
    XQ_EQ_I64(g_scActivated, 1);
    XQ_EQ_I64(g_scAmbiguous, 0);

    /* 手动注册/注销往返（头文件公开入口）。 */
    XShortcut_unregister(s1);
    XQ_CHECK(XShortcut_match((int)XKey_F7,
                             XShortcutContext_WidgetShortcut, host) == NULL);
    XShortcut_register(s1);
    XQ_CHECK(XShortcut_match((int)XKey_F7,
                             XShortcutContext_WidgetShortcut, host) == s1);

    XClassDelete((XClass*)s1);
    XClassDelete((XClass*)host);
}

/* ==================== ③ autoRepeat 守卫（P3-33/P3-34） ==================== */

/**
 * @brief 自动重复键对 autoRepeat=false 的快捷键**不激活**；true（缺省）
 *        时照常激活；非重复键不受影响。
 * @details 对齐 Qt QShortcutMap::tryShortcut（qshortcutmap.cpp:588-591）：
 *              // Don't trigger shortcut if we're autorepeating and the
 *              // shortcut is grabbed with not accepting autorepeats.
 *              if (!next || (e->isAutoRepeat() && !next->autorepeat))
 *                  return;
 *          X 的派发点在 XWidget_dispatchKeyEvent（KEY_PRESS）：命中快捷键
 *          且「事件 autoRepeat && !快捷键 autoRepeat」⇒ 跳过激活，按键
 *          继续走下方焦点控件投递（Qt 那个 return 也只放弃快捷键激活，
 *          QKeyEvent 照常发给控件）。事件形 XKeyEvent::m_autoRepeat 由
 *          平台后端端到端携带（XKeyEvent_setAutoRepeat），无需新事件。
 *          【P3-33 2026-11-11】此前 XShortcut_autoRepeat 仅存位、零消费；
 *          P3-34 的 getter 随之获得真实语义。
 * @note    按键必须经**桥接窗口**投递（XWidget_nativeWindow(top)）：
 *          快捷键表只在 VXWidgetWindow_event → XWidget_dispatchKeyEvent
 *          这一段参与，直投控件（XObject_event_base）不经过它。
 *          上下文用 ApplicationShortcut，不依赖焦点控件；键码用 F9（本
 *          测试套其它用例未占用，避免注册表残留造成的歧义派发）。
 */
XQ_TEST(XShortcut, autoRepeatGuard)
{
    XWidget* top = XWidget_create(NULL, 0);
    XShortcut* shortcut = NULL;
    XKeyEvent key;
    XQ_REQUIRE(top != NULL);
    XWidget_resize(top, 200, 120);
    XWidget_show(top); /* 惰性建桥接窗（内存对象；无平台后端时仅存储）。 */
    XQ_REQUIRE(XWidget_nativeWindow(top) != NULL);

    shortcut = XShortcut_create_2((int)XKey_F9, (XObject*)top);
    XQ_REQUIRE(shortcut != NULL);
    XShortcut_setContext(shortcut, XShortcutContext_ApplicationShortcut);
    sc_connectAll(shortcut);
    sc_reset();

    /* ① 缺省 autoRepeat=true：自动重复键照常激活（P3-34 getter 读真值）。 */
    XQ_CHECK(XShortcut_autoRepeat(shortcut));
    XKeyEvent_init(&key, XEVENT_TYPE_KEY_PRESS, (int)XKey_F9, 0);
    XKeyEvent_setAutoRepeat(&key, true);
    XCoreApplication_sendEvent((XObject*)XWidget_nativeWindow(top),
                               (XEvent*)&key);
    XQ_EQ_I64(g_scActivated, 1);
    XClassDeinit((XClass*)&key);

    /* ② autoRepeat=false：自动重复键不激活（计数不再增长）。 */
    XShortcut_setAutoRepeat(shortcut, false);
    XQ_CHECK(!XShortcut_autoRepeat(shortcut));
    XKeyEvent_init(&key, XEVENT_TYPE_KEY_PRESS, (int)XKey_F9, 0);
    XKeyEvent_setAutoRepeat(&key, true);
    XCoreApplication_sendEvent((XObject*)XWidget_nativeWindow(top),
                               (XEvent*)&key);
    XQ_EQ_I64(g_scActivated, 1);
    XClassDeinit((XClass*)&key);

    /* ③ 同一禁用重复的快捷键：**首次按下**（非 autoRepeat）必须激活。 */
    XKeyEvent_init(&key, XEVENT_TYPE_KEY_PRESS, (int)XKey_F9, 0);
    XCoreApplication_sendEvent((XObject*)XWidget_nativeWindow(top),
                               (XEvent*)&key);
    XQ_EQ_I64(g_scActivated, 2);
    XClassDeinit((XClass*)&key);

    /* ④ autoRepeat=false 置回 true 后：重复键恢复激活（标志双向有效）。 */
    XShortcut_setAutoRepeat(shortcut, true);
    XKeyEvent_init(&key, XEVENT_TYPE_KEY_PRESS, (int)XKey_F9, 0);
    XKeyEvent_setAutoRepeat(&key, true);
    XCoreApplication_sendEvent((XObject*)XWidget_nativeWindow(top),
                               (XEvent*)&key);
    XQ_EQ_I64(g_scActivated, 3);
    XClassDeinit((XClass*)&key);

    XClassDelete((XClass*)shortcut);
    XClassDelete((XClass*)top);
}

#endif /* XGUI_ON && XWIDGET_ON */
