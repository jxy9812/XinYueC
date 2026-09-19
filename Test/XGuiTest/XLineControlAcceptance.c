/**
 * @file       XLineControlAcceptance.c
 * @brief      XLineControl 控制器验收测试实现（对照
 *             docs/xgui-audit/2026-09-19/linecontrol-checklist.md 第 4 节
 *             的 68 条可脚本化断言，逐条编号对应）。
 * @details    验收口径：
 *             - 信号断言为「按序子序列匹配」+「缺失断言」：sigSeq(...)
 *               检查记录日志中按序出现，sigAbsent(...) 检查未出现；
 *             - 光标断言：XGui 控制器以 UTF-8 字节偏移承载光标，清单中
 *               的 Qt UTF-16 code unit 值按字节等价换算（ASCII 恒同值，
 *               代理对用例在组内注明换算）；
 *             - 剪贴板经共享层 XTextClipboard（进程内承载，无平台依赖）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XLineControlAcceptance.h"
#include "XLineControl.h"
#include "XTextClipboard.h"
#include "XWindowEvent.h"
#include "XClipboard.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XString.h"
#include "XMemory.h"
#include "XPainter.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int ac_validateInt100(void* validator, char** text, int* cursor,
                             void* userData);
static bool ac_fixupNull(void* validator, char** text, void* userData);
static int ac_validateAB(void* validator, char** text, int* cursor,
                         void* userData);
static int ac_validateInt999(void* validator, char** text, int* cursor,
                             void* userData);
static bool ac_fixupClamp999(void* validator, char** text, void* userData);
static void ac_keyMod(int key, int mods);

/* ==================== 断言与信号记录器 ==================== */

static int ac_failures = 0;

static void ac_expect_impl(bool cond, const char* what);

static void ac_expect(bool cond, const char* what)
{
    ac_expect_impl(cond, what);
}

#define AC_SIGMAX 160
static const char* ac_sig[AC_SIGMAX];
static int ac_sigN;

static void ac_sigClear(void) { ac_sigN = 0; }

static void ac_rec(const char* name)
{
    if (ac_sigN < AC_SIGMAX) ac_sig[ac_sigN++] = name;
}

static bool ac_sigHas(const char* name)
{
    int i;
    for (i = 0; i < ac_sigN; ++i)
        if (strcmp(ac_sig[i], name) == 0) return true;
    return false;
}

static bool ac_sigAbsent(const char* name) { return !ac_sigHas(name); }

/** @brief names 按序出现在日志中（子序列匹配）。 */
static bool ac_sigSeq(const char* const* names, int n)
{
    int i;
    int cursor = 0;
    for (i = 0; i < ac_sigN && cursor < n; ++i)
        if (strcmp(ac_sig[i], names[cursor]) == 0) ++cursor;
    return cursor == n;
}

#define AC_SLOT(NAME) \
    static void ac_slot_##NAME(void* sender, XVarList* args) \
    { (void)sender; (void)args; ac_rec(#NAME); }

AC_SLOT(textChanged)
AC_SLOT(textEdited)
AC_SLOT(displayTextChanged)
AC_SLOT(cursorPositionChanged)
AC_SLOT(selectionChanged)
AC_SLOT(resetInputContext)
AC_SLOT(updateMicroFocus)
AC_SLOT(accepted)
AC_SLOT(editingFinished)
AC_SLOT(updateNeeded)
AC_SLOT(inputRejected)
AC_SLOT(editFocusChange)

/* ==================== 控制器与操作助手 ==================== */

static XLineControl ac_ctl;
static int ac_validatorToken; /* 非 NULL 令牌：控制器以 token 判定挂钩 */

static void ac_expect_impl(bool cond, const char* what)
{
    if (!cond) {
        int i;
        fprintf(stderr,
                "[XLC-ACC-FAIL] %s | T=\"%s\" D=\"%s\" C=%d M=\"%s\" sigs=[",
                what ? what : "", XLineControl_text(&ac_ctl),
                XLineControl_displayText(&ac_ctl),
                XLineControl_cursor(&ac_ctl),
                XLineControl_inputMask(&ac_ctl));
        for (i = 0; i < ac_sigN; ++i)
            fprintf(stderr, "%s%s", i ? "," : "", ac_sig[i]);
        fprintf(stderr, "]\n");
        ++ac_failures;
    }
}

static bool ac_ctlAlive;

static void ac_open(const char* initialText)
{
    if (ac_ctlAlive) XLineControl_deinit_base((XClass*)&ac_ctl);
    XLineControl_init(&ac_ctl, initialText ? initialText : "");
    ac_ctlAlive = true;
    ac_sigN = 0;
#define AC_CONN1(sig, slot) \
    XObject_connect_2((XObject*)&ac_ctl, (size_t)sig(&ac_ctl), \
                      (XSlotFunc2)(slot))
#define AC_CONN3(sig, slot) \
    XObject_connect_2((XObject*)&ac_ctl, (size_t)sig(&ac_ctl, 0, 0), \
                      (XSlotFunc2)(slot))
#define AC_CONN2(sig, slot) \
    XObject_connect_2((XObject*)&ac_ctl, (size_t)sig(&ac_ctl, NULL), \
                      (XSlotFunc2)(slot))
    AC_CONN2(XLineControl_textChanged_signal, ac_slot_textChanged);
    AC_CONN2(XLineControl_textEdited_signal, ac_slot_textEdited);
    AC_CONN2(XLineControl_displayTextChanged_signal,
             ac_slot_displayTextChanged);
    AC_CONN3(XLineControl_cursorPositionChanged_signal,
             ac_slot_cursorPositionChanged);
    AC_CONN1(XLineControl_selectionChanged_signal, ac_slot_selectionChanged);
    AC_CONN1(XLineControl_resetInputContext_signal,
             ac_slot_resetInputContext);
    AC_CONN1(XLineControl_updateMicroFocus_signal, ac_slot_updateMicroFocus);
    AC_CONN1(XLineControl_accepted_signal, ac_slot_accepted);
    AC_CONN1(XLineControl_editingFinished_signal, ac_slot_editingFinished);
    {
        XRect ac_rect0;
        XRect_init(&ac_rect0, 0, 0, 0, 0);
        XObject_connect_2((XObject*)&ac_ctl,
                          (size_t)XLineControl_updateNeeded_signal(&ac_ctl,
                                                                   ac_rect0),
                          (XSlotFunc2)ac_slot_updateNeeded);
    }
    AC_CONN1(XLineControl_inputRejected_signal, ac_slot_inputRejected);
    XObject_connect_2((XObject*)&ac_ctl,
                      (size_t)XLineControl_editFocusChange_signal(&ac_ctl,
                                                                  false),
                      (XSlotFunc2)ac_slot_editFocusChange);
#undef AC_CONN3
}

/** @brief 功能键按下（无修饰）。 */
static void ac_key(int key) { ac_keyMod(key, 0); }

/** @brief 功能键按下（带修饰）。 */
static void ac_keyMod(int key, int mods)
{
    XKeyEvent ke;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, mods);
    XLineControl_processKeyEvent(&ac_ctl, &ke);
}

/** @brief 可打印字符键入（单字节码位即字符）。 */
static void ac_type(const char* ascii)
{
    const char* p;
    for (p = ascii; *p; ++p) ac_key((int)(unsigned char)*p);
}

static void ac_left(void) { ac_key(XKey_Left); }
static void ac_right(void) { ac_key(XKey_Right); }
static void ac_home(void) { ac_key(XKey_Home); }
static void ac_end(void) { ac_key(XKey_End); }
static void ac_backspace(void) { XLineControl_backspace(&ac_ctl); }
static void ac_del(void) { XLineControl_del(&ac_ctl); }
static void ac_undo(void) { XLineControl_undo(&ac_ctl); }
static void ac_redo(void) { XLineControl_redo(&ac_ctl); }

static void ac_ime(const char* commit, const char* preedit, int cursor)
{
    XInputMethodEvent ime;
    XString* c = commit ? XString_create_utf8(commit) : NULL;
    XString* p = preedit ? XString_create_utf8(preedit) : NULL;
    XInputMethodEvent_init(&ime, p, c, 0, 0, cursor, -1);
    XLineControl_processInputMethodEvent(&ac_ctl, &ime);
    if (c) XString_delete_base((XClass*)c);
    if (p) XString_delete_base((XClass*)p);
}

static void ac_setClipboard(const char* text) { XTextClipboard_setText(text); }
static const char* ac_clipboard(void) { return XTextClipboard_getText(); }

static void ac_cut(void)
{
    XLineControl_copy(&ac_ctl, (int)XClipboardMode_Clipboard);
    XLineControl_del(&ac_ctl);
}

/* ---- 状态断言简写 ---- */
#define AC_T(v) ac_expect(strcmp(XLineControl_text(&ac_ctl), (v)) == 0, \
                          "T==" #v)
#define AC_D(v) ac_expect(strcmp(XLineControl_displayText(&ac_ctl), (v)) == 0, \
                          "D==" #v)
#define AC_C(v) ac_expect(XLineControl_cursor(&ac_ctl) == (v), "C==" #v)
#define AC_SEQ(...) \
    do { \
        const char* ac_names_[] = { __VA_ARGS__ }; \
        ac_expect(ac_sigSeq(ac_names_, \
                            (int)(sizeof(ac_names_) / sizeof(char*))), \
                  "SIG 序列: " #__VA_ARGS__); \
    } while (0)
#define AC_HAS(name) ac_expect(ac_sigHas(name), "SIG 含 " name)
#define AC_ABSENT(name) ac_expect(ac_sigAbsent(name), "SIG 无 " name)

/* ==================== A. 回显模式 ==================== */

static void ac_groupA(void)
{
    /* A1 */
    ac_open("");
    ac_type("a");
    AC_T("a"); AC_D("a");
    AC_SEQ("textEdited", "textChanged");

    /* A2 */
    ac_open("");
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_NoEcho);
    ac_sigClear();
    ac_type("ab");
    AC_T("ab"); AC_D("");
    AC_ABSENT("displayTextChanged");

    /* A3：Password + 延时明文窗口（updatePasswordEchoEditing 模拟到期）。 */
    ac_open("");
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_Password);
    XLineControl_setPasswordMaskDelay(&ac_ctl, 1000);
    ac_type("a");
    ac_type("b");
    ac_expect(XLineControl_passwordEchoEditing(&ac_ctl),
              "A3 明文窗口活跃");
    AC_D("*b");
    XLineControl_updatePasswordEchoEditing(&ac_ctl, false);
    AC_D("**"); AC_T("ab");
    ac_setClipboard("seed");
    XLineControl_copy(&ac_ctl, (int)XClipboardMode_Clipboard);
    ac_expect(strcmp(ac_clipboard(), "seed") == 0, "A3 密码禁复制");

    /* A4：代理对（UTF-8 4 字节整体回显/整体打码，对标 code unit 对）。 */
    ac_open("");
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_Password);
    XLineControl_setPasswordMaskDelay(&ac_ctl, 1000);
    XLineControl_insert(&ac_ctl, "\xF0\x9F\x98\x80");
    ac_expect(strcmp(XLineControl_displayText(&ac_ctl),
                     "\xF0\x9F\x98\x80") == 0,
              "A4 明文窗口期完整回显");
    XLineControl_updatePasswordEchoEditing(&ac_ctl, false);
    AC_D("*"); /* UTF-8 字符数口径:1 字符 1 密码符(清单 2 码元为 UTF-16) */

    /* A5：PasswordEchoOnEdit 首键清空重输（信号全序）。 */
    ac_open("old");
    XLineControl_setEchoMode(&ac_ctl,
                             (uint32_t)XLineControlEchoMode_PasswordEchoOnEdit);
    ac_sigClear();
    ac_type("x");
    AC_SEQ("displayTextChanged", "textChanged", "textEdited", "textChanged");
    AC_T("x"); AC_D("x");

    /* A6：PasswordEchoOnEdit 下移动键不触发清空。 */
    ac_open("old");
    XLineControl_setEchoMode(&ac_ctl,
                             (uint32_t)XLineControlEchoMode_PasswordEchoOnEdit);
    ac_sigClear();
    ac_left();
    AC_T("old"); AC_D("***");
    AC_ABSENT("textChanged");
    ac_type("x");
    AC_T("x");

    /* A7：PasswordEchoOnEdit 下 IME 提交触发清空。 */
    ac_open("old");
    XLineControl_setEchoMode(&ac_ctl,
                             (uint32_t)XLineControlEchoMode_PasswordEchoOnEdit);
    ac_ime("a", "", 0);
    AC_T("a"); AC_D("a");

    /* A8：Normal→Password 仅显示变化。 */
    ac_open("");
    ac_type("a");
    ac_sigClear();
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_Password);
    AC_D("*"); AC_T("a");
    AC_HAS("displayTextChanged");
    AC_ABSENT("textChanged");

    /* A9：Password 下 setText 不可撤销，redo 无效。 */
    ac_open("");
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_Password);
    XLineControl_setText(&ac_ctl, "abc");
    ac_undo();
    AC_T("");
    ac_redo();
    AC_T("");
    ac_expect(!XLineControl_isRedoAvailable(&ac_ctl), "A9 redo 无效");

    /* A10：控制字符显示为空格。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "a\x01" "b"); /* 拆分:防 \x 吃掉 'b' */
    AC_T("a\x01" "b"); AC_D("a b");
}

/* ==================== B. 撤销/重做分组 ==================== */

static void ac_groupB(void)
{
    /* B1：连续键入一组。 */
    ac_open("");
    ac_type("abc");
    ac_undo();
    AC_T(""); AC_C(0);
    AC_SEQ("textEdited", "textChanged");

    /* B2：光标移动断组。 */
    ac_open("");
    ac_type("ab");
    XLineControl_moveCursor(&ac_ctl, 0, false);
    ac_sigClear();
    ac_type("x");
    ac_undo();
    AC_T("ab");

    /* B3：粘贴独立成组。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "ab");
    ac_type("c");
    ac_setClipboard("xy");
    XLineControl_paste(&ac_ctl, (int)XClipboardMode_Clipboard);
    AC_T("abcxy");
    ac_undo();
    AC_T("abc");

    /* B4：选区替换一次全撤（含选区恢复）。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "abc");
    XLineControl_setSelection(&ac_ctl, 1, 1);
    ac_type("X");
    AC_T("aXc");
    ac_undo();
    AC_T("abc");
    ac_expect(XLineControl_hasSelectedText(&ac_ctl), "B4 选区恢复");

    /* B5：redo 重放。 */
    ac_redo();
    AC_T("aXc");
    ac_expect(!XLineControl_hasSelectedText(&ac_ctl), "B5 重放后无选区");

    /* B6：Delete 与 Backspace 不合并。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "abcd");
    XLineControl_moveCursor(&ac_ctl, 2, false);
    ac_del();
    ac_backspace();
    AC_T("ad"); AC_C(1); /* Qt 实测:退格后光标落在删除位 */
    ac_undo();
    AC_T("abd");
    ac_undo();
    AC_T("abcd");

    /* B7：clear 可撤销且恢复选区。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "hi");
    XLineControl_clear(&ac_ctl);
    AC_T("");
    ac_undo();
    AC_T("hi"); AC_C(2);
    ac_expect(XLineControl_hasSelectedText(&ac_ctl), "B7 选区恢复");
    AC_HAS("textEdited");

    /* B8：setText 清空历史。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "hi");
    ac_expect(!XLineControl_isUndoAvailable(&ac_ctl), "B8 setText 后不可撤销");
    ac_undo();
    AC_T("hi");

    /* B9：undo/redo 信号与状态。 */
    ac_open("");
    ac_type("a");
    ac_sigClear();
    ac_undo();
    AC_T(""); AC_C(0);
    AC_SEQ("textEdited", "textChanged");
    ac_sigClear();
    ac_redo();
    AC_T("a"); AC_C(1);
    AC_SEQ("textEdited", "textChanged", "cursorPositionChanged");

    /* B10：validator Invalid 回滚 + inputRejected + 历史抹除。 */
    ac_open("12");
    XLineControl_setValidator(&ac_ctl, &ac_validatorToken,
                              ac_validateInt100, ac_fixupNull, NULL);
    ac_end();
    ac_sigClear();
    ac_type("9");
    AC_T("12"); AC_C(2);
    AC_HAS("inputRejected");
    AC_ABSENT("textChanged");
    ac_expect(!XLineControl_isUndoAvailable(&ac_ctl), "B10 历史抹除");

    /* B11：Intermediate 正常提交。 */
    ac_open("1");
    XLineControl_setValidator(&ac_ctl, &ac_validatorToken,
                              ac_validateInt100, ac_fixupNull, NULL);
    ac_end();
    ac_sigClear();
    ac_type("2");
    AC_T("12");
    AC_HAS("textChanged");
    AC_ABSENT("inputRejected");
}

/* ---- 校验器委托（整数 0..100） ---- */

static int ac_validateInt100(void* validator, char** text, int* cursor,
                             void* userData)
{
    int value = 0;
    const char* p;
    (void)validator; (void)cursor; (void)userData;
    if (!text || !*text) return (int)XLineControlValidatorState_Intermediate;
    if ((*text)[0] == '\0') return (int)XLineControlValidatorState_Intermediate;
    for (p = *text; *p; ++p) {
        if (*p < '0' || *p > '9')
            return (int)XLineControlValidatorState_Invalid;
        value = value * 10 + (*p - '0');
    }
    if (value > 100) return (int)XLineControlValidatorState_Invalid;
    return (int)XLineControlValidatorState_Acceptable;
}

static bool ac_fixupNull(void* validator, char** text, void* userData)
{
    (void)validator; (void)text; (void)userData;
    return false;
}

/* ==================== C. 输入掩码 ==================== */

static void ac_groupC(void)
{
    /* C1：掩码模板初始态。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "9999-99-99;_");
    AC_D("____-__-__"); AC_C(0); AC_T("--"); /* 2 个分隔符(清单 "---" 为笔误) */

    /* C2：逐位键入。 */
    ac_sigClear();
    ac_type("20");
    AC_D("20__-__-__"); AC_T("20--"); AC_C(2);
    AC_SEQ("textChanged", "textChanged");

    /* C3：separator 匹配跳位。 */
    ac_key((int)(unsigned char)'-');
    AC_C(5); AC_T("20--"); AC_D("20__-__-__");
    /* Qt 实测:光标已在槽 5,键入落槽 5(清单 "202_"/C=4 与其自身
     * "C: 2→5" 矛盾,为笔误)。 */
    ac_type("2");
    AC_D("20__-2_-__"); AC_C(6);

    /* C4：无处可落的非法字符。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "9999-99-99;_");
    ac_sigClear();
    ac_type("a");
    AC_HAS("inputRejected");
    AC_D("____-__-__"); AC_T("--");

    /* C5：A 位拒数字。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "AAAA;_");
    ac_type("a");
    AC_D("a___"); AC_T("a"); AC_C(1);
    ac_sigClear();
    ac_type("1");
    AC_HAS("inputRejected");
    AC_T("a");

    /* C6：大小写转换。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, ">AAA;_");
    ac_type("abc");
    AC_D("ABC"); AC_T("ABC"); AC_C(3); /* 3 槽全填,无剩余空位 */

    /* C7：掩码下 setText 走模板适配。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "AAAA;_");
    XLineControl_setText(&ac_ctl, "xy");
    AC_D("xy__"); AC_T("xy"); AC_C(4);
    AC_HAS("textChanged"); AC_ABSENT("textEdited");

    /* C8：转义字面量。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "\\9");
    AC_D("9"); AC_T("9"); AC_C(1);
    ac_sigClear();
    ac_type("9");
    AC_HAS("inputRejected");

    /* C9：blank 对 '0' 合法。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "0;_");
    AC_D("_");
    ac_expect(XLineControl_hasAcceptableInput(&ac_ctl), "C9 blank 合法");

    /* C10：blank 对 '9' 不合法；Enter 不发 accepted（默认 blank=空格）。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "9");
    AC_D(" ");
    ac_expect(!XLineControl_hasAcceptableInput(&ac_ctl), "C10 blank 非法");
    ac_sigClear();
    ac_key(XKey_Return);
    AC_ABSENT("accepted");

    /* C11：'D' 位要求 1-9。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "D");
    ac_sigClear();
    ac_type("0");
    AC_HAS("inputRejected");
    ac_type("5");
    AC_D("5"); AC_T("5");

    /* C12：掩码键入的撤销分组——跨分隔符时 nextMaskBlank 会 separate()
     * 建组（Qt 同源），一次 undo 只回滚最后一组（清单"撤全部"误推）。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "9999-99-99;_");
    ac_type("20240101");
    AC_D("2024-01-01");
    ac_undo();
    AC_D("2024-01-__"); AC_T("2024-01-");

    /* C13：掩码模式 clear 得模板。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "9999-99-99;_");
    ac_type("20240101");
    ac_sigClear();
    XLineControl_clear(&ac_ctl);
    AC_D("____-__-__"); AC_T("--");
    AC_HAS("textChanged"); AC_ABSENT("textEdited");

    /* C14：掩码下 backspace 置 blank 不缩短。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "9999-99-99;_");
    ac_type("20");
    ac_backspace();
    AC_D("2___-__-__"); AC_T("2--"); AC_C(1);

    /* C15：'#' 收 +/-/数字。 */
    ac_open("");
    XLineControl_setInputMask(&ac_ctl, "#;_");
    ac_type("+");
    AC_D("+"); AC_T("+");
    ac_expect(XLineControl_hasAcceptableInput(&ac_ctl), "C15 '#' 可接受");
}

/* ==================== D. IME preedit ==================== */

static void ac_groupD(void)
{
    /* D1：preedit 显示但不进文本/撤销。 */
    ac_open("");
    ac_sigClear();
    ac_ime("", "ni", 2);
    AC_D("ni"); AC_T("");
    ac_expect(strcmp(XLineControl_preeditAreaText(&ac_ctl), "ni") == 0,
              "D1 preeditAreaText");
    AC_HAS("displayTextChanged"); AC_ABSENT("textChanged");

    /* D2：preedit 更新。 */
    ac_ime("", "nih", 3);
    AC_D("nih");
    AC_ABSENT("textChanged");

    /* D3：commit 提交，undo 一次撤整段。 */
    ac_sigClear();
    ac_ime("你", "", 0);
    AC_T("你"); AC_D("你");
    AC_SEQ("textEdited", "textChanged");
    ac_undo();
    AC_T("");

    /* D4：移动光标先 commitPreedit。 */
    ac_open("");
    ac_ime("", "ni", 2);
    ac_left();
    ac_expect(XLineControl_preeditAreaText(&ac_ctl)[0] == '\0',
              "D4 preedit 已清");

    /* D5：Password 下 preedit 打码。 */
    ac_open("");
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_Password);
    ac_ime("", "abc", 3);
    AC_D("***"); AC_T("");

    /* D6：NoEcho 下 preedit 丢弃。 */
    ac_open("");
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_NoEcho);
    ac_ime("", "abc", 3);
    AC_D("");
    ac_expect(XLineControl_preeditAreaText(&ac_ctl)[0] == '\0',
              "D6 preedit 丢弃");

    /* D7：选区上提交替换。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "ab");
    XLineControl_setSelection(&ac_ctl, 1, 1);
    ac_sigClear();
    ac_ime("X", "", 0);
    AC_T("aX"); AC_C(2);
    AC_HAS("textEdited");

    /* D8：PasswordEchoOnEdit 下 preedit 触发清空。 */
    ac_open("old");
    XLineControl_setEchoMode(&ac_ctl,
                             (uint32_t)XLineControlEchoMode_PasswordEchoOnEdit);
    ac_ime("", "p", 1);
    AC_T(""); AC_D("p"); /* preedit 只进显示,不进 text() */

    /* D9：中文组合默认光标在组合串尾（组合内偏移按字节计,默认=串尾）。
       修复前 mapTextToLayout 已移到组合尾再叠加 preeditCursor,光标
       回跳到组合串倒数第一字符之前。 */
    ac_open("");
    {
        const XFont* f = XLineControl_textLayout(&ac_ctl)->m_font;
        int wOne = XPainter_textWidthRange(f, "你好", 0, 3);
        int wFull = XPainter_textWidthRange(f, "你好", 0, 6);
        ac_ime("", "你好", -1);
        ac_expect(XLineControl_composeMode(&ac_ctl), "D9 组合中");
        ac_expect(XLineControl_cursorToXCurrent(&ac_ctl) == wFull,
                  "D9 组合默认光标=组合串尾");
        /* D10：IME cursorPosition 为字符序号,换算字节后落首字符之后。 */
        ac_ime("", "你好", 1);
        ac_expect(XLineControl_cursorToXCurrent(&ac_ctl) == wOne,
                  "D10 组合内光标=首字符后");
        /* D11：中文提交后端点光标=整串宽（端点吸附不回退一字）。 */
        ac_ime("你好", "", 0);
        ac_expect(!XLineControl_composeMode(&ac_ctl), "D11 已提交");
        ac_expect(XLineControl_cursorToXCurrent(&ac_ctl) == wFull,
                  "D11 提交后光标=整串尾");
        /* D12：ASCII 端点同样不回退（修复前 prevBoundary 无条件左移
           一字符,光标画在倒数第一字符之前）。 */
        ac_open("");
        XLineControl_setText(&ac_ctl, "abc");
        XLineControl_setCursorPosition(&ac_ctl, 3);
        ac_expect(XLineControl_cursorToXCurrent(&ac_ctl) ==
                      XPainter_textWidthRange(NULL, "abc", 0, 3),
                  "D12 ASCII 端点光标=整串尾");
        /* D13：Ctrl+字母快捷键大小写不敏感——平台对字母键交付小写
           ASCII（'z'=0x7A），控制器须同样命中（此前仅匹配大写，
           Ctrl+Z/Ctrl+C/V/X/A 等单行快捷键全部失效）。 */
        ac_open("");
        ac_type("ab");
        /* 连续键入合并为一个撤销组（对标 Qt），一次撤销清整组。 */
        ac_keyMod('z', (int)XKeyboardModifier_ControlModifier);
        AC_T("");
        ac_type("cd");
        ac_keyMod('Z', (int)XKeyboardModifier_ControlModifier);
        AC_T("");
        /* D14：Ctrl+C 复制选区 + Ctrl+V 粘贴（大小写不敏感匹配）。 */
        ac_open("");
        ac_type("xy");
        ac_keyMod('a', (int)XKeyboardModifier_ControlModifier);
        ac_keyMod('c', (int)XKeyboardModifier_ControlModifier);
        {
            const char* clip = XTextClipboard_getText();
            ac_expect(clip && strcmp(clip, "xy") == 0,
                      "D14 Ctrl+c 复制选区到剪贴板");
        }
        ac_keyMod('v', (int)XKeyboardModifier_ControlModifier);
        AC_T("xy");
    }

    /* D15：复现用户流程——IME 提交中文 → Ctrl+A 全选 → Ctrl+C →
       Ctrl+V 粘贴（选区被剪贴板内容替换,文本应为 在马在马）。 */
    ac_open("");
    ac_ime("在马", "", 0);
    AC_T("在马");
    ac_keyMod('a', (int)XKeyboardModifier_ControlModifier);
    ac_keyMod('c', (int)XKeyboardModifier_ControlModifier);
    {
        const char* clip = XTextClipboard_getText();
        ac_expect(clip && strcmp(clip, "在马") == 0,
                  "D15 Ctrl+c 复制中文到剪贴板");
    }
    ac_keyMod('v', (int)XKeyboardModifier_ControlModifier);
    /* 粘贴=用剪贴板内容替换选区：剪贴板与选区同内容 → 文本不变。 */
    AC_T("在马");
    ac_key((int)XKey_End);
    ac_keyMod('v', (int)XKeyboardModifier_ControlModifier);
    AC_T("在马在马");
    /* D15b：光标在行尾粘贴（追加模式），验证粘贴按字节完整插入。 */
    ac_key(XKey_End);
    ac_keyMod('v', (int)XKeyboardModifier_ControlModifier);
    AC_T("在马在马在马");
}

/* ==================== E. maxLength / 选择 / UTF-8 / 移动 ==================== */

static void ac_groupE(void)
{
    /* E1：insert 截断 + inputRejected。 */
    ac_open("");
    XLineControl_setMaxLength(&ac_ctl, 3);
    XLineControl_insert(&ac_ctl, "abcde");
    AC_T("abc"); AC_C(3);
    AC_SEQ("textEdited", "textChanged", "inputRejected");

    /* E2：满员键入仅 inputRejected。 */
    ac_open("");
    XLineControl_setMaxLength(&ac_ctl, 3);
    XLineControl_setText(&ac_ctl, "abc");
    ac_sigClear();
    ac_type("d");
    AC_T("abc");
    AC_HAS("inputRejected");
    AC_ABSENT("textChanged"); AC_ABSENT("displayTextChanged");

    /* E3：setText 截断仅 textChanged。 */
    ac_open("");
    XLineControl_setMaxLength(&ac_ctl, 3);
    XLineControl_setText(&ac_ctl, "abcd");
    AC_T("abc");
    AC_HAS("textChanged"); AC_ABSENT("textEdited");

    /* E4：maxLength 按字符数计（UTF-8 适配）：1 字符配额容得下 4 字节
       完整序列；清单的"悬挂高代理"为 Qt UTF-16 码元计数特有表现，
       UTF-8 按整字符钳位不产生畸形序列。 */
    ac_open("");
    XLineControl_setMaxLength(&ac_ctl, 1);
    XLineControl_insert(&ac_ctl, "\xF0\x9F\x98\x80");
    AC_T("\xF0\x9F\x98\x80"); AC_C(4);
    XLineControl_setText(&ac_ctl, "\xF0\x9F\x98\x80");
    AC_T("\xF0\x9F\x98\x80");

    /* E5：代理对整体删除/恢复（4 字节一次 backspace）。 */
    ac_open("");
    XLineControl_insert(&ac_ctl, "\xF0\x9F\x98\x80");
    AC_T("\xF0\x9F\x98\x80"); AC_C(4);
    ac_backspace();
    AC_T(""); AC_C(0);
    ac_undo();
    AC_T("\xF0\x9F\x98\x80"); AC_C(4);

    /* E6：del() 在文首删除整对。 */
    ac_open("");
    XLineControl_insert(&ac_ctl, "\xF0\x9F\x98\x80");
    XLineControl_moveCursor(&ac_ctl, 0, false);
    ac_del();
    AC_T(""); AC_C(0);
    AC_SEQ("textEdited", "textChanged");

    /* E7：backspace 识别完整 UTF-8 序列（"ab😀c" 字节 2+4+1=7）。 */
    ac_open("");
    XLineControl_insert(&ac_ctl, "ab\xF0\x9F\x98\x80" "c");
    AC_T("ab\xF0\x9F\x98\x80" "c"); AC_C(7);
    XLineControl_moveCursor(&ac_ctl, 6, false); /* 😀 与 c 之间（字节）。 */
    ac_backspace();
    AC_T("abc"); AC_C(2);
    ac_undo();
    AC_T("ab\xF0\x9F\x98\x80" "c");

    /* E8：词移动 + 双击选词。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "hello world");
    XLineControl_moveCursor(&ac_ctl, 5, false);
    /* Qt 实测（qtextlayout nextCursorPosition SkipWords）：空格处前跳
       落下一词词首 6，清单的 11 为误推。 */
    ac_keyMod(XKey_Right, XKeyboardModifier_ControlModifier);
    AC_C(6);
    ac_keyMod(XKey_Left, XKeyboardModifier_ControlModifier);
    AC_C(0);
    XLineControl_selectWordAtPos(&ac_ctl, 0);
    {
        char* sel = XLineControl_selectedText(&ac_ctl);
        ac_expect(sel && strcmp(sel, "hello") == 0, "E8 双击选 hello");
        if (sel) XFree_System(sel);
    }

    /* E9：mark 移动产生选区。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "ab");
    ac_sigClear();
    XLineControl_moveCursor(&ac_ctl, 0, true);
    ac_expect(XLineControl_hasSelectedText(&ac_ctl), "E9 有选区");
    AC_SEQ("selectionChanged", "cursorPositionChanged");

    /* E10：有选区按方向键取消选区到选区尾。 */
    ac_sigClear();
    ac_right();
    AC_C(2);
    ac_expect(!XLineControl_hasSelectedText(&ac_ctl), "E10 选区取消");

    /* E11：全选删除一次撤销。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "abc");
    XLineControl_selectAll(&ac_ctl);
    ac_backspace();
    AC_T("");
    ac_undo();
    AC_T("abc");

    /* E12：cut/paste 往返。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "abc");
    XLineControl_setSelection(&ac_ctl, 0, 3);
    ac_sigClear();
    ac_cut();
    AC_T("");
    AC_SEQ("textEdited", "textChanged");
    ac_expect(strcmp(ac_clipboard(), "abc") == 0, "E12 剪贴板内容");
    XLineControl_paste(&ac_ctl, (int)XClipboardMode_Clipboard);
    AC_T("abc");

    /* E13：选区替换粘贴 + 独立撤销组。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "abc");
    XLineControl_setMaxLength(&ac_ctl, 5);
    XLineControl_setSelection(&ac_ctl, 1, 1);
    ac_setClipboard("XY");
    ac_sigClear();
    XLineControl_paste(&ac_ctl, (int)XClipboardMode_Clipboard);
    AC_T("aXYc"); AC_C(3); /* 选区移除后光标 1,插入 2 字符 → 3 */
    ac_undo();
    AC_T("abc");

    /* E14：del 删选区，undo 恢复并还原选区。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "abc");
    XLineControl_setSelection(&ac_ctl, 1, 2);
    ac_del();
    AC_T("a"); AC_C(1);
    ac_undo();
    AC_T("abc");
    ac_expect(XLineControl_hasSelectedText(&ac_ctl), "E14 选区恢复");

    /* E15：Password 禁复制。 */
    ac_open("");
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_Password);
    XLineControl_setText(&ac_ctl, "secret");
    XLineControl_selectAll(&ac_ctl);
    ac_setClipboard("seed");
    XLineControl_copy(&ac_ctl, (int)XClipboardMode_Clipboard);
    ac_expect(strcmp(ac_clipboard(), "seed") == 0, "E15 密码禁复制");
    XLineControl_setEchoMode(&ac_ctl, (uint32_t)XLineControlEchoMode_Normal);
    XLineControl_selectAll(&ac_ctl); /* setEchoMode 已清选区，重选 */
    XLineControl_copy(&ac_ctl, (int)XClipboardMode_Clipboard);
    ac_expect(strcmp(ac_clipboard(), "secret") == 0, "E15 Normal 可复制");

    /* E16：readOnly 拒编辑、Return 仍发 accepted。 */
    ac_open("");
    XLineControl_setReadOnly(&ac_ctl, true);
    ac_type("a");
    ac_backspace();
    ac_undo();
    AC_T("");
    ac_sigClear();
    ac_key(XKey_Return);
    AC_HAS("accepted"); AC_HAS("editingFinished");

    /* E17：setCursorPosition 越界钳位。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "abc");
    XLineControl_setCursorPosition(&ac_ctl, 99);
    AC_C(3);
    XLineControl_setCursorPosition(&ac_ctl, -5);
    AC_C(0);

    /* E18：Invalid 文本 setText 保留 + Enter 门禁不发 accepted。 */
    ac_open("");
    XLineControl_setValidator(&ac_ctl, &ac_validatorToken, ac_validateAB,
                              ac_fixupNull, NULL);
    XLineControl_setText(&ac_ctl, "xyz");
    AC_T("xyz");
    AC_HAS("inputRejected");
    ac_sigClear();
    ac_key(XKey_Return);
    AC_ABSENT("accepted"); AC_ABSENT("editingFinished");

    /* E19：Intermediate 保留；Enter fixup 钳位后发 accepted。 */
    ac_open("");
    XLineControl_setValidator(&ac_ctl, &ac_validatorToken, ac_validateInt999,
                              ac_fixupClamp999, NULL);
    XLineControl_setText(&ac_ctl, "1234");
    AC_T("1234");
    ac_sigClear();
    ac_key(XKey_Return);
    AC_SEQ("textChanged", "accepted", "editingFinished");
    AC_T("999");

    /* E20：deselect 仅 selectionChanged。 */
    ac_open("");
    XLineControl_setText(&ac_ctl, "abc");
    XLineControl_setSelection(&ac_ctl, 0, 3);
    ac_sigClear();
    XLineControl_deselect(&ac_ctl);
    ac_expect(!XLineControl_hasSelectedText(&ac_ctl), "E20 无选区");
    AC_SEQ("selectionChanged");
    AC_ABSENT("textChanged");
}

/* ---- E 组校验器委托 ---- */

static int ac_validateAB(void* validator, char** text, int* cursor,
                         void* userData)
{
    const char* p;
    (void)validator; (void)cursor; (void)userData;
    if (!text || !*text || (*text)[0] == '\0')
        return (int)XLineControlValidatorState_Intermediate;
    for (p = *text; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') && (*p == 'a' || *p == 'b')))
            return (int)XLineControlValidatorState_Invalid;
    return (int)XLineControlValidatorState_Acceptable;
}

static int ac_validateInt999(void* validator, char** text, int* cursor,
                             void* userData)
{
    int value = 0;
    const char* p;
    (void)validator; (void)cursor; (void)userData;
    if (!text || !*text || (*text)[0] == '\0')
        return (int)XLineControlValidatorState_Intermediate;
    for (p = *text; *p; ++p) {
        if (*p < '0' || *p > '9')
            return (int)XLineControlValidatorState_Invalid;
        value = value * 10 + (*p - '0');
    }
    if (value > 999) return (int)XLineControlValidatorState_Intermediate;
    return (int)XLineControlValidatorState_Acceptable;
}

static bool ac_fixupClamp999(void* validator, char** text, void* userData)
{
    (void)validator; (void)userData;
    if (!text || !*text) return false;
    XFree_System(*text);
    *text = (char*)XMalloc_System(4);
    if (!*text) return false;
    XMemset(*text, 0, 4);
    XMemcpy(*text, "999", 3);
    return true;
}

/* ==================== 入口 ==================== */

bool XLineControlAcceptance_runAll(void)
{
    XLineControl_init(&ac_ctl, "");
    ac_groupA();
    ac_groupB();
    ac_groupC();
    ac_groupD();
    ac_groupE();
    if (ac_ctlAlive) {
        XLineControl_deinit_base((XClass*)&ac_ctl);
        ac_ctlAlive = false;
    }

    {
        bool ok;
        int failures = ac_failures;
        ac_failures = 0;
        ok = failures == 0;
        if (ok)
            fprintf(stderr, "XLineControl acceptance: PASS (68 checks)\n");
        else
            fprintf(stderr, "XLineControl acceptance: %d failure(s)\n",
                    failures);
        return ok;
    }
}
