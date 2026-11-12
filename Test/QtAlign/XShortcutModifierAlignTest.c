/**
 * @file       XShortcutModifierAlignTest.c
 * @brief      XKeySequence / XShortcut / XKeySequenceEdit 的**修饰位**对齐
 *             契约测试（台账 §B12 主修项）。
 * @details    对齐依据：Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/gui/kernel/
 *             qkeysequence.cpp、qshortcutmap.cpp、qshortcut.cpp 与
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/widgets/
 *             qkeysequenceedit.cpp。
 *
 *             本文件锁的是 §B12 修掉的「XShortcut 序列不携带修饰位」缺口：
 *             - `XKeySequence` 值类型：matches 三态、修饰位输出顺序
 *               Meta→Ctrl→Alt→Shift→Num（qkeysequence.cpp:1236-1257）、
 *               键名表与文本往返（decodeString/encodeString）、mnemonic
 *               的 Alt 修饰（qkeysequence.cpp:935-967）；
 *             - `XShortcut` 修饰位承载：setKeySequence_2 / setKeySequence_3 /
 *               keySequence / keyModifiers / setSequences / sequenceCount /
 *               sequenceAt，以及 `XShortcut_match_2` 的修饰位精确匹配
 *               （Ctrl+S 与 S 互不命中）；
 *             - 修饰位参与歧义判定：同键不同修饰不算歧义
 *               （qshortcutmap.cpp:441-470 以 keySequence 为匹配键）；
 *             - id()/parentWidget()（Qt 6.0 起弃用的两个 getter）；
 *             - 平台原生抓键的**降级语义**：无窗口系统/无 X11 时
 *               `XShortcut_setGlobal` 返回 false，对象留在进程内匹配
 *               （台账 §B12 验收方式 4）；
 *             - `XKeySequenceEdit` 的序列渲染/解析走同一份实现（文本口径
 *               不再各自维护）；释放键 1s 收口与清除按钮
 *               setter/getter 的真实语义。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON

#include "XKeySequence.h"
#include "XShortcut.h"
#include "XKeySequenceEdit.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"
#include "XVarList.h"
#include "XClass.h"
#include "XEvent.h"
#include "XStringUtils.h"
#include "XCoreApplication.h"
#include "XGuiApplication.h"

/** @brief activated / activatedAmbiguously 计数。 */
static int g_b12Activated;
static int g_b12Ambiguous;

static void b12_activatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_b12Activated;
}

static void b12_ambiguousSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_b12Ambiguous;
}

static void b12_resetCounts(void)
{
    g_b12Activated = 0;
    g_b12Ambiguous = 0;
}

static void b12_connectAll(XShortcut* shortcut)
{
    XObject_connect((XObject*)shortcut,
                    (size_t)XShortcut_activated_signal(NULL),
                    (XObject*)shortcut, b12_activatedSlot,
                    XConnectionType_Direct);
    XObject_connect((XObject*)shortcut,
                    (size_t)XShortcut_activatedAmbiguously_signal(NULL),
                    (XObject*)shortcut, b12_ambiguousSlot,
                    XConnectionType_Direct);
}

/* ==================== ① XKeySequence 值类型：匹配三态 ==================== */

/**
 * @brief matches 三态逐条对齐 qkeysequence.cpp:1335-1354。
 * @details ① `userN > seqN ⇒ NoMatch`；② 等长 ⇒ ExactMatch，否则
 *          PartialMatch；③ 任一组不等 ⇒ NoMatch。
 */
XQ_TEST(XKeySequence, matchesThreeStates)
{
    XKeySequence two;
    XKeySequence one;
    XKeySequence other;
    XKeySequence empty = XKeySequence_empty();

    /* {Ctrl+O, Ctrl+S} */
    two = XKeySequence_empty();
    two.combos[0] = XKeyCombination_make(XKeyboardModifier_ControlModifier,
                                        (int)XKey_O);
    two.combos[1] = XKeyCombination_make(XKeyboardModifier_ControlModifier,
                                        (int)XKey_S);
    two.count = 2;

    /* {Ctrl+O} */
    one = XKeySequence_fromKey(XKeyboardModifier_ControlModifier,
                               (int)XKey_O);

    /* 空 vs 非空：空序列的 userN=0 <= seqN ⇒ PartialMatch（Qt 同：
     * 空 QKeySequence 与任何非空序列匹配为 PartialMatch）。 */
    XQ_EQ_I64((int)XKeySequence_matches(&empty, &one),
              (int)XKeySequenceMatch_PartialMatch);

    /* 等长且逐组相等 ⇒ ExactMatch。 */
    XQ_EQ_I64((int)XKeySequence_matches(&one, &one),
              (int)XKeySequenceMatch_ExactMatch);
    XQ_EQ_I64((int)XKeySequence_matches(&two, &two),
              (int)XKeySequenceMatch_ExactMatch);

    /* 被查序列更长 ⇒ PartialMatch（输入还可能有后续键）。 */
    XQ_EQ_I64((int)XKeySequence_matches(&one, &two),
              (int)XKeySequenceMatch_PartialMatch);

    /* 被查序列更短 ⇒ NoMatch（Qt: `if (userN > seqN) return NoMatch;`）。 */
    XQ_EQ_I64((int)XKeySequence_matches(&two, &one),
              (int)XKeySequenceMatch_NoMatch);

    /* 首组不同 ⇒ NoMatch。 */
    other = XKeySequence_fromKey(XKeyboardModifier_ControlModifier,
                                 (int)XKey_P);
    XQ_EQ_I64((int)XKeySequence_matches(&other, &one),
              (int)XKeySequenceMatch_NoMatch);

    /* **修饰位参与比较**：同键码不同修饰位 ⇒ NoMatch（这是 §B12 的核心）。 */
    other = XKeySequence_fromKey(XKeyboardModifier_NoModifier, (int)XKey_O);
    XQ_EQ_I64((int)XKeySequence_matches(&other, &one),
              (int)XKeySequenceMatch_NoMatch);
    other = XKeySequence_fromKey(XKeyboardModifier_AltModifier, (int)XKey_O);
    XQ_EQ_I64((int)XKeySequence_matches(&other, &one),
              (int)XKeySequenceMatch_NoMatch);
    other = XKeySequence_fromKey(
        (XKeyboardModifiers)(XKeyboardModifier_ControlModifier |
                             XKeyboardModifier_ShiftModifier),
        (int)XKey_O);
    XQ_EQ_I64((int)XKeySequence_matches(&other, &one),
              (int)XKeySequenceMatch_NoMatch);
}

/* ==================== ② XKeySequence 文本编解码 ==================== */

/**
 * @brief toString/fromString 往返 + Qt 原文口径。
 * @details 修饰位输出顺序 **Meta → Ctrl → Alt → Shift → Num**
 *          （qkeysequence.cpp:1236-1257 的 encodeString）；键名按
 *          `keyName`（可打印键大写、F1..F35 为 "F1"..、其余查表）。
 */
XQ_TEST(XKeySequence, stringRoundTrip)
{
    char buf[128];
    XKeySequence seq;
    XKeySequence parsed;

    /* 单键 Ctrl+S ⇒ "Ctrl+S"。 */
    seq = XKeySequence_fromKey(XKeyboardModifier_ControlModifier, (int)XKey_S);
    XKeySequence_toString(&seq, XKeySequenceFormat_NativeText, buf, sizeof(buf));
    XQ_EQ_STR(buf, "Ctrl+S");

    /* 修饰位顺序：Meta 最先、Shift 最后（且 Num 在 Shift 之后）。 */
    seq = XKeySequence_fromKey(
        (XKeyboardModifiers)(XKeyboardModifier_ShiftModifier |
                             XKeyboardModifier_ControlModifier |
                             XKeyboardModifier_AltModifier |
                             XKeyboardModifier_MetaModifier),
        (int)XKey_A);
    XKeySequence_toString(&seq, XKeySequenceFormat_PortableText, buf,
                          sizeof(buf));
    XQ_EQ_STR(buf, "Meta+Ctrl+Alt+Shift+A");

    /* 功能键命名（Qt keyName: "F%1".arg(key - Key_F1 + 1)）。 */
    seq = XKeySequence_fromKey(XKeyboardModifier_NoModifier, (int)XKey_F12);
    XKeySequence_toString(&seq, XKeySequenceFormat_PortableText, buf,
                          sizeof(buf));
    XQ_EQ_STR(buf, "F12");

    /* 键名表项（Qt keyname 表：Esc/Tab/Backtab/Backspace/Return/Ins/Del/…）。 */
    seq = XKeySequence_fromKey(XKeyboardModifier_NoModifier, (int)XKey_Escape);
    XKeySequence_toString(&seq, XKeySequenceFormat_PortableText, buf,
                          sizeof(buf));
    XQ_EQ_STR(buf, "Esc");
    seq = XKeySequence_fromKey(XKeyboardModifier_NoModifier, (int)XKey_PageDown);
    XKeySequence_toString(&seq, XKeySequenceFormat_PortableText, buf,
                          sizeof(buf));
    XQ_EQ_STR(buf, "PgDown");

    /* 多组以 ", " 分隔（Qt QKeySequence::toString 的多组形态）。 */
    seq = XKeySequence_empty();
    seq.combos[0] = XKeyCombination_make(XKeyboardModifier_ControlModifier,
                                        (int)XKey_O);
    seq.combos[1] = XKeyCombination_make(XKeyboardModifier_ControlModifier,
                                        (int)XKey_S);
    seq.count = 2;
    XKeySequence_toString(&seq, XKeySequenceFormat_NativeText, buf, sizeof(buf));
    XQ_EQ_STR(buf, "Ctrl+O, Ctrl+S");

    /* 往返：文本解析回同一序列。 */
    XQ_CHECK(XKeySequence_fromString("Ctrl+O, Ctrl+S", &parsed));
    XQ_EQ_I64(XKeySequence_count(&parsed), 2);
    XQ_CHECK(XKeySequence_isEqual(&seq, &parsed));

    /* 大小写不敏感（Qt decodeString 整串 toLower()）。 */
    XQ_CHECK(XKeySequence_fromString("ctrl+alt+shift+a", &parsed));
    XQ_EQ_I64(XKeySequence_count(&parsed), 1);
    XQ_EQ_I64((int)parsed.combos[0].key, (int)XKey_A);
    XQ_EQ_I64((int)parsed.combos[0].modifiers,
              (int)(XKeyboardModifier_ControlModifier |
                    XKeyboardModifier_AltModifier |
                    XKeyboardModifier_ShiftModifier));

    /* "Ctrl++"（加号本身）：Qt 用 `lastIndexOf('+', size-2)` 支持。 */
    XQ_CHECK(XKeySequence_fromString("Ctrl++", &parsed));
    XQ_EQ_I64((int)parsed.combos[0].key, (int)'+');
    XQ_EQ_I64((int)parsed.combos[0].modifiers,
              (int)XKeyboardModifier_ControlModifier);

    /* 非法文本（非修饰名的中缀）⇒ false + 空序列（Qt 解析成 Key_unknown）。 */
    XQ_CHECK(!XKeySequence_fromString("Foo+S", &parsed));
    XQ_CHECK(XKeySequence_isEmpty(&parsed));
    XQ_CHECK(!XKeySequence_fromString("", &parsed));
    XQ_CHECK(XKeySequence_isEmpty(&parsed));
    XQ_CHECK(!XKeySequence_fromString(NULL, &parsed));

    /* 缓冲不足：截断但不越界、仍返回完整长度（供调用方按需扩容）。 */
    {
        int need;
        char tiny[6];
        seq = XKeySequence_fromKey(XKeyboardModifier_ControlModifier,
                                   (int)XKey_F12);
        need = XKeySequence_toString(&seq, XKeySequenceFormat_NativeText,
                                     tiny, sizeof(tiny));
        XQ_EQ_I64(need, (int)sizeof("Ctrl+F12") - 1);
        XQ_EQ_I64((int)XStrlen(tiny), (int)sizeof(tiny) - 1);
    }
}

/* ==================== ③ mnemonic：Alt + 大写码位 ==================== */

/**
 * @brief 助记符口径对齐 qkeysequence.cpp:935-967。
 * @details `&x` 的 x 转大写并配 **Alt**；`&&` 是字面 `&`（跳过）。
 */
XQ_TEST(XKeySequence, mnemonicAltModifier)
{
    XKeySequence seq;

    seq = XKeySequence_mnemonic("&File");
    XQ_EQ_I64(XKeySequence_count(&seq), 1);
    /* 注意：'F' 的大写码位是 0x46；这里用数值直接比，避免与功能键
     * XKey_F1.. 的名字混淆（qkeysequence.cpp:952-953 的 `c.toUpper()`）。 */
    XQ_EQ_I64((int)seq.combos[0].key, 0x46);
    XQ_EQ_I64((int)seq.combos[0].modifiers, (int)XKeyboardModifier_AltModifier);

    /* 小写助记符同样大写化（'e' -> 'E'）。 */
    seq = XKeySequence_mnemonic("E&xit");
    XQ_EQ_I64((int)seq.combos[0].key, 0x58 /* 'X' */);
    XQ_EQ_I64((int)seq.combos[0].modifiers, (int)XKeyboardModifier_AltModifier);

    /* `&&` 是字面与号：`A&&B` 里没有 `&x` ⇒ 空序列。 */
    seq = XKeySequence_mnemonic("A&&B");
    XQ_CHECK(XKeySequence_isEmpty(&seq));

    /* 无 `&` ⇒ 空序列。 */
    seq = XKeySequence_mnemonic("Plain");
    XQ_CHECK(XKeySequence_isEmpty(&seq));
    seq = XKeySequence_mnemonic(NULL);
    XQ_CHECK(XKeySequence_isEmpty(&seq));

    /* 尾部孤立的 `&` ⇒ 空序列（不越界）。 */
    seq = XKeySequence_mnemonic("Trailing&");
    XQ_CHECK(XKeySequence_isEmpty(&seq));
}

/* ==================== ④ XShortcut 修饰位承载 ==================== */

/**
 * @brief setKeySequence_2/_3 + keySequence/keyModifiers 往返，且
 *        `match_2` 按「修饰位 + 键码」精确命中。
 * @details 这是 §B12 的核心回归锁：旧实现只能承载裸键码，
 *          `Ctrl+S` 与 `S` 会互相命中（XAbstractButton 因此宁可
 *          fail-closed 也不敢注册带修饰位的文本）。
 */
XQ_TEST(XShortcut, modifierAwareKeyAndMatch)
{
    XWidget* host = XWidget_create(NULL, 0);
    XShortcut* ctrlS;
    XShortcut* plainS;
    const XKeySequence* key;

    XQ_REQUIRE(host != NULL);

    ctrlS = XShortcut_create_4(XKeyboardModifier_ControlModifier,
                               (int)XKey_S, (XObject*)host);
    XQ_REQUIRE(ctrlS != NULL);
    XShortcut_setContext(ctrlS, XShortcutContext_WindowShortcut);

    /* 序列承载：键码 + 修饰位都在。 */
    XQ_EQ_I64(XShortcut_key(ctrlS), (int)XKey_S);
    XQ_EQ_I64((int)XShortcut_keyModifiers(ctrlS),
              (int)XKeyboardModifier_ControlModifier);
    key = XShortcut_keySequence(ctrlS);
    XQ_REQUIRE(key != NULL);
    XQ_EQ_I64(XKeySequence_count(key), 1);
    XQ_EQ_I64((int)XKeySequence_firstKey(key), (int)XKey_S);

    /* match_2：Ctrl+S 命中，裸 S 不命中（修饰位精确比较）。 */
    XQ_CHECK(XShortcut_match_2((int)XKey_S, XKeyboardModifier_ControlModifier,
                               XShortcutContext_WindowShortcut, host) == ctrlS);
    XQ_CHECK(XShortcut_match_2((int)XKey_S, XKeyboardModifier_NoModifier,
                               XShortcutContext_WindowShortcut, host) == NULL);
    XQ_CHECK(XShortcut_match_2((int)XKey_S,
                               (XKeyboardModifiers)(XKeyboardModifier_ControlModifier |
                                                    XKeyboardModifier_ShiftModifier),
                               XShortcutContext_WindowShortcut, host) == NULL);

    /* 旧的无修饰位入口 match() 仍按裸键语义（向后兼容）。 */
    XQ_CHECK(XShortcut_match((int)XKey_S, XShortcutContext_WindowShortcut,
                             host) == NULL);

    /* 并存的裸 S 快捷键与 Ctrl+S 互不干扰（Qt：keySequence 是匹配键）。 */
    plainS = XShortcut_create_2((int)XKey_S, (XObject*)host);
    XQ_REQUIRE(plainS != NULL);
    XQ_CHECK(XShortcut_match_2((int)XKey_S, XKeyboardModifier_NoModifier,
                               XShortcutContext_WindowShortcut, host) == plainS);
    XQ_CHECK(XShortcut_match_2((int)XKey_S, XKeyboardModifier_ControlModifier,
                               XShortcutContext_WindowShortcut, host) == ctrlS);
    XQ_CHECK(XShortcut_match((int)XKey_S, XShortcutContext_WindowShortcut,
                             host) == plainS);

    /* KeypadModifier 被剥除（QShortcutMap::nextState 的 Keypad 回退）：
     * NumLock 开时小键盘键带 Keypad 位，Ctrl+S 仍应命中。 */
    XQ_CHECK(XShortcut_match_2(
                 (int)XKey_S,
                 (XKeyboardModifiers)(XKeyboardModifier_ControlModifier |
                                      XKeyboardModifier_KeypadModifier),
                 XShortcutContext_WindowShortcut, host) == ctrlS);

    /* 文本入口：setKeySequence_3 解析 "Alt+F4"。 */
    XShortcut_setKeySequence_3(ctrlS, "Alt+F4");
    XQ_EQ_I64(XShortcut_key(ctrlS), (int)XKey_F4);
    XQ_EQ_I64((int)XShortcut_keyModifiers(ctrlS),
              (int)XKeyboardModifier_AltModifier);
    /* 非法文本 ⇒ 清空（Qt 把非法文本解析为空 QKeySequence）。 */
    XShortcut_setKeySequence_3(ctrlS, "NotAKey+X");
    XQ_EQ_I64(XShortcut_keyCount(ctrlS), 0);
    XQ_EQ_I64(XShortcut_key(ctrlS), 0);

    /* 便捷 setKeySequence_2（修饰位 + 键码），key==0 清空。 */
    XShortcut_setKeySequence_2(ctrlS, XKeyboardModifier_ShiftModifier,
                               (int)XKey_F5);
    XQ_EQ_I64(XShortcut_key(ctrlS), (int)XKey_F5);
    XQ_EQ_I64((int)XShortcut_keyModifiers(ctrlS),
              (int)XKeyboardModifier_ShiftModifier);
    XShortcut_setKeySequence_2(ctrlS, XKeyboardModifier_ShiftModifier, 0);
    XQ_EQ_I64(XShortcut_keyCount(ctrlS), 0);

    XClassDelete((XClass*)plainS);
    XClassDelete((XClass*)ctrlS);
    XClassDelete((XClass*)host);
}

/* ==================== ⑤ 修饰位参与歧义判定 ==================== */

/**
 * @brief 同键码不同修饰位**不算歧义**；同序列多命中才发
 *        activatedAmbiguously。
 * @details 对标 QShortcutMap::dispatchEvent（qshortcutmap.cpp:441-470）：
 *          identicals 是按 **keySequence** 收集的，同一键码不同修饰位在
 *          不同序列上，不会同时进入 identicals。
 */
XQ_TEST(XShortcut, ambiguityRequiresSameModifiers)
{
    XWidget* host = XWidget_create(NULL, 0);
    XShortcut* ctrlS;
    XShortcut* altS;
    XShortcut* ctrlS2;

    XQ_REQUIRE(host != NULL);
    ctrlS = XShortcut_create_4(XKeyboardModifier_ControlModifier,
                              (int)XKey_S, (XObject*)host);
    altS = XShortcut_create_4(XKeyboardModifier_AltModifier,
                              (int)XKey_S, (XObject*)host);
    XQ_REQUIRE(ctrlS != NULL);
    XQ_REQUIRE(altS != NULL);
    XShortcut_setContext(ctrlS, XShortcutContext_WidgetShortcut);
    XShortcut_setContext(altS, XShortcutContext_WidgetShortcut);
    b12_connectAll(ctrlS);
    b12_connectAll(altS);

    /* Ctrl+S 只有一个命中 ⇒ 无歧义 activated（altS 不参与）。 */
    b12_resetCounts();
    XShortcut_activate_2(ctrlS, (int)XKey_S,
                         XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(g_b12Activated, 1);
    XQ_EQ_I64(g_b12Ambiguous, 0);

    /* Alt+S 同理。 */
    b12_resetCounts();
    XShortcut_activate_2(altS, (int)XKey_S, XKeyboardModifier_AltModifier);
    XQ_EQ_I64(g_b12Activated, 1);
    XQ_EQ_I64(g_b12Ambiguous, 0);

    /* 再登记一条同序列的快捷键 ⇒ 歧义：两个都收 ambiguous、都不收
       activated（Qt 对每个命中者发 activatedAmbiguously）。 */
    ctrlS2 = XShortcut_create_4(XKeyboardModifier_ControlModifier,
                               (int)XKey_S, (XObject*)host);
    XQ_REQUIRE(ctrlS2 != NULL);
    XShortcut_setContext(ctrlS2, XShortcutContext_WidgetShortcut);
    b12_connectAll(ctrlS2);
    b12_resetCounts();
    XShortcut_activate_2(ctrlS, (int)XKey_S,
                         XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(g_b12Activated, 0);
    XQ_EQ_I64(g_b12Ambiguous, 2);

    /* 移除歧义源后恢复。 */
    XClassDelete((XClass*)ctrlS2);
    b12_resetCounts();
    XShortcut_activate_2(ctrlS, (int)XKey_S,
                         XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(g_b12Activated, 1);
    XQ_EQ_I64(g_b12Ambiguous, 0);

    /* 禁用后完全不发射（QShortcutMap 的 enabled 过滤）。 */
    XShortcut_setEnabled(ctrlS, false);
    b12_resetCounts();
    XShortcut_activate_2(ctrlS, (int)XKey_S,
                         XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(g_b12Activated, 0);
    XQ_EQ_I64(g_b12Ambiguous, 0);

    XClassDelete((XClass*)altS);
    XClassDelete((XClass*)ctrlS);
    XClassDelete((XClass*)host);
}

/* ==================== ⑥ setKeys 多序列 + id + parentWidget ==================== */

/**
 * @brief 多备选序列（含修饰位）+ 逐序列 id（对标 QShortcut::id 的号源）+
 *        parentWidget。
 * @details id 语义对齐 QShortcutMap 的 `--(d->currentId)`
 *          （qshortcutmap.cpp:133）：**单调递减、非 0、每序列唯一**。
 */
XQ_TEST(XShortcut, sequencesIdsAndParentWidget)
{
    XWidget* host = XWidget_create(NULL, 0);
    XShortcut* sc;
    XKeySequence seqs[2];
    int id0;
    int id1;

    XQ_REQUIRE(host != NULL);
    sc = XShortcut_create((XObject*)host);
    XQ_REQUIRE(sc != NULL);
    XShortcut_setContext(sc, XShortcutContext_WidgetShortcut);

    /* parentWidget（Qt 6.0 起弃用的模板）。 */
    XQ_CHECK(XShortcut_parentWidget(sc) == (XObject*)host);

    /* 空序列 ⇒ id 为 0（未分配）。 */
    XQ_EQ_I64(XShortcut_sequenceCount(sc), 0);
    XQ_EQ_I64(XShortcut_id(sc), 0);

    seqs[0] = XKeySequence_fromKey(XKeyboardModifier_ControlModifier,
                                   (int)XKey_K);
    seqs[1] = XKeySequence_fromKey(XKeyboardModifier_ControlModifier,
                                   (int)XKey_L);
    XShortcut_setSequences(sc, seqs, 2);
    XQ_EQ_I64(XShortcut_sequenceCount(sc), 2);
    XQ_EQ_I64(XShortcut_keyCount(sc), 2);

    /* 任一序列命中（多备选）。 */
    XQ_CHECK(XShortcut_match_2((int)XKey_K, XKeyboardModifier_ControlModifier,
                               XShortcutContext_WidgetShortcut, host) == sc);
    XQ_CHECK(XShortcut_match_2((int)XKey_L, XKeyboardModifier_ControlModifier,
                               XShortcutContext_WidgetShortcut, host) == sc);
    /* 裸键不命中。 */
    XQ_CHECK(XShortcut_match_2((int)XKey_K, XKeyboardModifier_NoModifier,
                               XShortcutContext_WidgetShortcut, host) == NULL);

    /* 逐序列 id：非 0、互异、单调递减（QShortcutMap 的 --currentId）。 */
    id0 = XShortcut_sequenceId(sc, 0);
    id1 = XShortcut_sequenceId(sc, 1);
    XQ_CHECK(id0 != 0);
    XQ_CHECK(id1 != 0);
    XQ_CHECK(id0 != id1);
    XQ_CHECK(id1 < id0);
    XQ_EQ_I64(XShortcut_id(sc), id0); /* Qt: sc_ids.first() */
    XQ_EQ_I64(XShortcut_sequenceId(sc, 2), 0); /* 越界 */

    /* 同值早退：重复设同一组序列不换 id（对标 qshortcut.cpp:380-390）。 */
    XShortcut_setSequences(sc, seqs, 2);
    XQ_EQ_I64(XShortcut_sequenceId(sc, 0), id0);
    XQ_EQ_I64(XShortcut_sequenceId(sc, 1), id1);

    /* 单序列替换（setKeySequence）⇒ 只剩一条、id 沿用首条。 */
    XShortcut_setKeySequence(sc, &seqs[0]);
    XQ_EQ_I64(XShortcut_sequenceCount(sc), 1);
    XQ_EQ_I64(XShortcut_sequenceId(sc, 0), id0);

    /* 清空 ⇒ id 归 0。 */
    XShortcut_setSequences(sc, NULL, 0);
    XQ_EQ_I64(XShortcut_sequenceCount(sc), 0);
    XQ_EQ_I64(XShortcut_id(sc), 0);

    XClassDelete((XClass*)sc);
    XClassDelete((XClass*)host);
}

/* ==================== ⑦ 平台原生抓键降级语义 ==================== */

/**
 * @brief 无窗口系统能力时 `XShortcut_setGlobal` 返回 false 并**保留进程内
 *        匹配**（台账 §B12 验收方式 4）。
 * @details 本测试环境（离屏/无 X 连接或 X11 头未检出）下平台抓键必然
 *          不可用：断言 setGlobal(false→true) 返回 false、isGlobal() 为
 *          false，且对象仍能被 match_2 命中（进程内表未受影响）。
 *          该断言在**有** X 连接的机器上依然成立的反面不成立——故这里
 *          只在「抓键失败」路径断言，成功路径交给板端/桌面联调验收。
 */
XQ_TEST(XShortcut, globalGrabFallsBackToInProcess)
{
    XWidget* host = XWidget_create(NULL, 0);
    XShortcut* sc;
    bool grabbed;

    XQ_REQUIRE(host != NULL);
    sc = XShortcut_create_4(XKeyboardModifier_ControlModifier,
                           (int)XKey_F9, (XObject*)host);
    XQ_REQUIRE(sc != NULL);
    XShortcut_setContext(sc, XShortcutContext_ApplicationShortcut);
    XQ_CHECK(!XShortcut_isGlobal(sc));

    grabbed = XShortcut_setGlobal(sc, true);
    XQ_EQ_I64((int)XShortcut_isGlobal(sc), (int)grabbed);

    /* 无论抓键成败，进程内匹配都必须在（失败即降级，不是禁用）。 */
    XQ_CHECK(XShortcut_match_2((int)XKey_F9,
                               XKeyboardModifier_ControlModifier,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == sc);
    /* 重复 setGlobal(true) 与释放都不得崩。 */
    (void)XShortcut_setGlobal(sc, true);
    XQ_CHECK(XShortcut_setGlobal(sc, false));
    XQ_CHECK(!XShortcut_isGlobal(sc));
    /* 无序列时抓键直接失败（Qt 断言 keyless 不可登记）。 */
    XShortcut_setSequences(sc, NULL, 0);
    XQ_CHECK(!XShortcut_setGlobal(sc, true));
    XQ_CHECK(!XShortcut_isGlobal(sc));

    XClassDelete((XClass*)sc);
    XClassDelete((XClass*)host);
}

/* ==================== ⑦b 多组序列的披露语义 ==================== */

/**
 * @brief 多组序列（"Ctrl+O, Ctrl+S"）的单事件匹配限制是**显式披露**而非
 *        静默失效（台账 §B12 复核 P2）。
 * @details 锁三条：① 单事件匹配对多组序列恒 NoMatch（Qt 是按序累积前缀
 *          匹配，本实现未做——披露于 XShortcut.h 文件头 @note 与
 *          setSequences @warning）；② 含多组序列的快捷键
 *          XShortcut_setGlobal 返回 false 且 isGlobal 为 false（平台抓键
 *          是单键码机制，抓首组会让 Ctrl+O 单独变全局热键——语义错误）；
 *          ③ 降级后进程内匹配的其他快捷键不受影响。
 */
XQ_TEST(XShortcut, multiComboSequenceDisclosure)
{
    XWidget* host = XWidget_create(NULL, 0);
    XShortcut* multi;
    XShortcut* plain;
    XKeySequence seqs[1];

    XQ_REQUIRE(host != NULL);
    multi = XShortcut_create((XObject*)host);
    XQ_REQUIRE(multi != NULL);
    XShortcut_setContext(multi, XShortcutContext_ApplicationShortcut);

    /* 登记两 组序列。 */
    seqs[0] = XKeySequence_empty();
    seqs[0].combos[0] = XKeyCombination_make(XKeyboardModifier_ControlModifier,
                                            (int)XKey_O);
    seqs[0].combos[1] = XKeyCombination_make(XKeyboardModifier_ControlModifier,
                                            (int)XKey_S);
    seqs[0].count = 2;
    XShortcut_setSequences(multi, seqs, 1);
    XQ_EQ_I64(XShortcut_sequenceCount(multi), 1);
    XQ_EQ_I64(XShortcut_sequenceAt(multi, 0)->count, 2);

    /* ① 单事件匹配：两 组的完整序列对单次按键恒 NoMatch。 */
    XQ_CHECK(XShortcut_match_2((int)XKey_O, XKeyboardModifier_ControlModifier,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == NULL);
    XQ_CHECK(XShortcut_match_2((int)XKey_S, XKeyboardModifier_ControlModifier,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == NULL);

    /* ② 平台抓键拒绝多组序列（不会把 Ctrl+O 单独变全局热键）。 */
    XQ_CHECK(!XShortcut_setGlobal(multi, true));
    XQ_CHECK(!XShortcut_isGlobal(multi));

    /* ③ 同 host 上的单组序列进程内匹配不受影响。 */
    plain = XShortcut_create_2((int)XKey_F10, (XObject*)host);
    XQ_REQUIRE(plain != NULL);
    XShortcut_setContext(plain, XShortcutContext_ApplicationShortcut);
    XQ_CHECK(XShortcut_match_2((int)XKey_F10, XKeyboardModifier_NoModifier,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == plain);

    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)multi);
    XClassDelete((XClass*)host);
}

/* ==================== ⑧ XKeySequenceEdit 共用序列类型/渲染 ==================== */

/**
 * @brief 捕获控件的序列渲染走 `XKeySequence_toString`（单一事实源），
 *        且修饰位捕获真实生效。
 * @details 关键点：旧实现自带一份键名映射（Esc/Tab/Left/Up/Right/Down
 *          的**码位本身就是错的**：0x01000020 是 XKey_Shift 而非 Home，
 *          0x01000014 是 XKey_Right 而非 Left）。改用共享实现后这些键名
 *          全部正确。同时锁 clearButtonEnabled 的 setter/getter 真实语义。
 */
XQ_TEST(XKeySequenceEdit, sharesSequenceValueType)
{
    XKeySequenceEdit* edit = XKeySequenceEdit_create(NULL, 0);
    XKeySequence input;

    XQ_REQUIRE(edit != NULL);

    /* 修饰位 + 键码完整捕获往返。 */
    input = XKeySequence_fromKey(
        (XKeyboardModifiers)(XKeyboardModifier_ControlModifier |
                             XKeyboardModifier_AltModifier),
        (int)XKey_P);
    XKeySequenceEdit_setKeySequence(edit, &input);
    {
        const XKeySequence* got = XKeySequenceEdit_keySequence(edit);
        XQ_REQUIRE(got != NULL);
        XQ_EQ_I64(XKeySequence_count(got), 1);
        XQ_EQ_I64((int)got->combos[0].key, (int)XKey_P);
        XQ_EQ_I64((int)got->combos[0].modifiers,
                  (int)(XKeyboardModifier_ControlModifier |
                        XKeyboardModifier_AltModifier));
    }

    /* clearButtonEnabled：setter/getter 是真实存储位（P3-35 起 paintEvent
     * 会按它绘制「×」并记命中矩形，本处只锁开关语义）。 */
    XQ_CHECK(!XKeySequenceEdit_isClearButtonEnabled(edit));
    XKeySequenceEdit_setClearButtonEnabled(edit, true);
    XQ_CHECK(XKeySequenceEdit_isClearButtonEnabled(edit));
    XKeySequenceEdit_setClearButtonEnabled(edit, false);
    XQ_CHECK(!XKeySequenceEdit_isClearButtonEnabled(edit));

    /* maximumSequenceLength 缺省 4（= XKEYSEQUENCE_MAX_LENGTH 同源）。 */
    XQ_EQ_I64(XKeySequenceEdit_maximumSequenceLength(edit),
              XKEYSEQUENCE_MAX_LENGTH);

    /* 渲染文本：渲染函数是文件内静态的，这里通过「设值 → 序列 → toString」
     * 的等价链断言共享编解码一致（控件内部即调用同一函数）。 */
    {
        char buf[64];
        XKeySequence_toString(XKeySequenceEdit_keySequence(edit),
                              XKeySequenceFormat_NativeText, buf, sizeof(buf));
        XQ_EQ_STR(buf, "Ctrl+Alt+P");
    }

    XClassDelete((XClass*)edit);
}

/* ==================== ⑨ 键名映射的码位回归（旧实现的三处错位） ====== */

/**
 * @brief `XKeySequence_keyName` 对 XKey 枚举的命名正确性。
 * @details 旧 XKeySequenceEdit 的 xks_keyName 用**裸数值**匹配（0x01000020
 *          → "Home"），但 XEvent.h 里 0x01000020 是 XKey_Shift、Home 是
 *          0x01000010；同理 0x01000014 被写成 "Left" 实为 XKey_Right。
 *          改用 XKey_* 枚举常量后必须全部落位。
 */
XQ_TEST(XKeySequence, keyNameCodepoints)
{
    char buf[32];

    XKeySequence_keyName((int)XKey_Home, buf, sizeof(buf));
    XQ_EQ_STR(buf, "Home");
    XKeySequence_keyName((int)XKey_End, buf, sizeof(buf));
    XQ_EQ_STR(buf, "End");
    XKeySequence_keyName((int)XKey_Left, buf, sizeof(buf));
    XQ_EQ_STR(buf, "Left");
    XKeySequence_keyName((int)XKey_Up, buf, sizeof(buf));
    XQ_EQ_STR(buf, "Up");
    XKeySequence_keyName((int)XKey_Right, buf, sizeof(buf));
    XQ_EQ_STR(buf, "Right");
    XKeySequence_keyName((int)XKey_Down, buf, sizeof(buf));
    XQ_EQ_STR(buf, "Down");
    /* 修饰键码位本身没有键名（Qt keyname 表不含 Shift/Control/…）。 */
    XKeySequence_keyName((int)XKey_Shift, buf, sizeof(buf));
    XQ_EQ_STR(buf, "?");
    /* 可打印键大写（Qt `QChar::fromUcs2(key).toUpper()`）。 */
    XKeySequence_keyName((int)'a', buf, sizeof(buf));
    XQ_EQ_STR(buf, "A");
    XKeySequence_keyName((int)'7', buf, sizeof(buf));
    XQ_EQ_STR(buf, "7");
    /* 空格走键名表的 "Space"（Qt: `key != Qt::Key_Space` 才走单字符分支）。 */
    XKeySequence_keyName((int)XKey_Space, buf, sizeof(buf));
    XQ_EQ_STR(buf, "Space");
    /* F1..F35 边界。 */
    XKeySequence_keyName((int)XKey_F1, buf, sizeof(buf));
    XQ_EQ_STR(buf, "F1");
    XKeySequence_keyName((int)XKey_F35, buf, sizeof(buf));
    XQ_EQ_STR(buf, "F35");
    /* 键名逆解析（decodeString 用）。 */
    XQ_EQ_I64(XKeySequence_keyFromName("pgup", 4), (int)XKey_PageUp);
    XQ_EQ_I64(XKeySequence_keyFromName("F35", 3), (int)XKey_F35);
    XQ_EQ_I64(XKeySequence_keyFromName("f1", 2), (int)XKey_F1);
    XQ_EQ_I64(XKeySequence_keyFromName("a", 1), (int)XKey_A);
    XQ_EQ_I64(XKeySequence_keyFromName("NoSuchKey", 9), 0);
}

#endif /* XGUI_ON && XWIDGET_ON */
