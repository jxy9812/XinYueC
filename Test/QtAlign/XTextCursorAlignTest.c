/**
 * @file       XTextCursorAlignTest.c
 * @brief      XTextCursor 可编辑光标 × Qt 6.8 QTextCursor 对齐契约测试
 *             （§B16/W2-a 文本光标）。
 * @details    对齐依据（Qt 6.8.3 原文行号 + 本机 Qt 6.8.0 offscreen 探针
 *             实测值；期望值逐条标注探针编号 P、N、R 系，探针源码与输出见
 *             台账「W2-a 光标对齐」附录）：
 *             - 位置/选区：setPosition 越界忽略（qtextcursor.cpp:1121-1144，
 *               探针 P10）；KeepAnchor 只动 position（探针 P1/N6）；
 *               selectionStart/End 取 min/max（:1651-1669）；clearSelection
 *               折叠（:1620-1626）；跨块 selectedText 块分隔符 = U+2029
 *               （:1691-1696 + 探针 P1 [0062 2029 0063 0064]——任务书 '\n'
 *               表述与 Qt 真语义冲突点，按 Qt 实装，台账登记）；
 *             - 移动：movePosition 全枚举（探针 P4/qtprobe2 逐值对拍；
 *               qtextcursor.cpp:304-633）；Up/Down 经 XTextLayout 视觉行
 *               （colToX/xToCol，sticky x 一次计算跨次保持，:412-447/
 *               :520-557）；词移动词分隔符集合 qtextengine.cpp:2845-2886、
 *               块界跨到分隔符位 qtextdocument_p.cpp:1332-1357；
 *             - 插入：有选区先删（:1408-1414）；格式继承 charFormat 派生
 *               （:1857-1889 + 探针 P2/P2b/P2c/P2d/P13）；'\n'/U+2029/
 *               '\r\n' 拆块（:1441-1457 + 探针 P3/N4）；
 *             - 删除：removeSelectedText 折叠到选区起点（:114-142 + 探针
 *               P6/P6b）；deleteChar 光标不动（:1473-1489 探针 P5a/R4）；
 *               deletePreviousChar 回退（:1497-1526 探针 P5a/R6）；边界
 *               no-op；
 *             - 格式：setCharFormat 选区整段替换（探针 P11 三段）；merge
 *               属性空间合并（:1917-1929）；块格式作用于选区内所有块
 *               （:743-784 + 探针 P12）；内联字段与格式池影子一致；
 *             - 撤销：beginEditBlock/endEditBlock 合并一步（qtextdocument_p
 *               .cpp:1149-1175 + 探针 P8/N8）；undo/redo 后活动光标按
 *               adjustPosition（qtextcursor.cpp:57-94）映射（探针 P7/N1/
 *               N2a/N2c/N2d/R1-R6）。
 * @note       C99；栈光标 XTextCursor_deinit、文档 XClassDelete——漏删即
 *             运行器判 [LEAK]（--leak-tolerance=0 由运行器统一执行）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XTEXTDOCUMENT_ON

#include "XTextDocument.h"
#include "XTextCursor.h"
#include "XTextFormat.h"
#include "XTextLayout.h"
#include "XMemory.h"
#include "XObject.h"

#include <string.h>

/* ==================== 信号计数槽（同 XTextDocumentAlignTest 套路） ==================== */

/** @brief 一条内容信号事件。 */
typedef struct XTCEvent
{
    int kind;   /**< 0=contentsChange 1=contentsChanged 2=blockCountChanged。 */
    int from;
    int removed;
    int added;
    int count;
} XTCEvent;

#define XTC_EVENT_MAX 32
static XTCEvent s_tcEvents[XTC_EVENT_MAX];
static int s_tcEventN;

static void xtc_resetEvents(void) { s_tcEventN = 0; }

static void xtc_slotCC(XObject* receiver, XVarList* args)
{
    XTCEvent* ev;
    (void)receiver;
    if (s_tcEventN >= XTC_EVENT_MAX) return;
    ev = &s_tcEvents[s_tcEventN++];
    ev->kind = 0;
    ev->count = 0;
    XVarList_args_3(args, int, from, int, removed, int, added);
    ev->from = from;
    ev->removed = removed;
    ev->added = added;
}

static void xtc_slotCCh(XObject* receiver, XVarList* args)
{
    XTCEvent* ev;
    (void)receiver;
    (void)args;
    if (s_tcEventN >= XTC_EVENT_MAX) return;
    ev = &s_tcEvents[s_tcEventN++];
    ev->kind = 1;
    ev->from = ev->removed = ev->added = ev->count = 0;
}

static void xtc_slotBCC(XObject* receiver, XVarList* args)
{
    XTCEvent* ev;
    (void)receiver;
    if (s_tcEventN >= XTC_EVENT_MAX) return;
    ev = &s_tcEvents[s_tcEventN++];
    ev->kind = 2;
    ev->from = ev->removed = ev->added = 0;
    XVarList_args_1(args, int, count);
    ev->count = count;
}

static void xtc_connect(XTextDocument* doc)
{
    xtc_resetEvents();
    XObject_connect((XObject*)doc,
                    (size_t)XTextDocument_contentsChange_signal(doc, 0, 0, 0),
                    (XObject*)doc, xtc_slotCC, XConnectionType_Direct);
    XObject_connect((XObject*)doc,
                    (size_t)XTextDocument_contentsChanged_signal(doc),
                    (XObject*)doc, xtc_slotCCh, XConnectionType_Direct);
    XObject_connect((XObject*)doc,
                    (size_t)XTextDocument_blockCountChanged_signal(doc, 0),
                    (XObject*)doc, xtc_slotBCC, XConnectionType_Direct);
}

static void xtc_expectCC(int i, int from, int removed, int added,
                         const char* what)
{
    if (i >= s_tcEventN) {
        xq_fail_at(__FILE__, __LINE__, "%s：缺少第 %d 条事件（共 %d 条）",
                   what ? what : "", i, s_tcEventN);
        return;
    }
    xq_check_impl(s_tcEvents[i].kind == 0 && s_tcEvents[i].from == from &&
                      s_tcEvents[i].removed == removed &&
                      s_tcEvents[i].added == added,
                  "contentsChange(from,removed,added)", __FILE__, __LINE__,
                  "%s：第 %d 条实际 kind=%d (%d,%d,%d)", what ? what : "", i,
                  s_tcEvents[i].kind, s_tcEvents[i].from, s_tcEvents[i].removed,
                  s_tcEvents[i].added);
}

/* ==================== 夹具 ==================== */

/** @brief toPlainText + 断言 + 释放三合一（防堆结果漏释放）。 */
static void xtc_plainIs(XTextDocument* doc, const char* expect,
                        const char* what)
{
    char* t = XTextDocument_toPlainText(doc);
    xq_check_impl(t != NULL, "toPlainText != NULL", __FILE__, __LINE__,
                  "%s", what ? what : "");
    if (!t) return;
    {
        bool same = t && strcmp(t, expect ? expect : "") == 0;
        xq_check_impl(same, "plain text", __FILE__, __LINE__,
                      "%s：实际 \"%s\" 期望 \"%s\"", what ? what : "", t,
                      expect ? expect : "");
    }
    XFree_System(t);
}

/** @brief 释放 charFormatAt/charFormatInline 输出的字符串字段（契约：字段
 *         由读函数新建、归调用方释放）。 */
static void xtc_freeFmt(XTDCharFormat* f)
{
    if (!f) return;
    if (f->fontFamily) {
        XClassDelete(f->fontFamily);
        f->fontFamily = NULL;
    }
    if (f->anchorHref) {
        XClassDelete(f->anchorHref);
        f->anchorHref = NULL;
    }
}

/** @brief 断言第 bi 块第 fi 片段的 bold（内联字段口径——渲染路径读它）。 */
static void xtc_fragBold(const XTextDocument* doc, int bi, int fi, int bold,
                         const char* what)
{
    const XTDFragment* f = XTextDocument_fragment(doc, bi, fi);
    if (!f) {
        xq_fail_at(__FILE__, __LINE__, "%s：片段(%d,%d)不存在", what, bi, fi);
        return;
    }
    xq_check_impl((int)f->fmt.bold == bold, "fragment bold", __FILE__,
                  __LINE__, "%s：块%d片段%d bold=%d 期望 %d", what, bi, fi,
                  (int)f->fmt.bold, bold);
}

/** @brief 断言块 bi 的片段数（格式边界 = Qt 探针 textFormats 的段数）。 */
static void xtc_fragCount(const XTextDocument* doc, int bi, int n,
                          const char* what)
{
    xq_check_impl(XTextDocument_fragmentCount(doc, bi) == n,
                  "fragmentCount", __FILE__, __LINE__,
                  "%s：块%d片段数=%d 期望 %d", what, bi,
                  XTextDocument_fragmentCount(doc, bi), n);
}

/* ==================== ① 插入 / 删除 / 跨块删除与块合并 ==================== */

XQ_TEST(XTextCursor, insertDeleteAndCrossBlockMerge)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor cur;
    XQ_REQUIRE(doc != NULL);
    xtc_connect(doc);

    /* 文档级 insertTextAt：'\n' 拆块（Qt insertText 的块分隔符口径，
     * 探针 P3：charCount = 2+1+2 = 6、内容新增 5）。精确区间 (0,0,5)。 */
    XQ_EQ_I64(XTextDocument_insertTextAt(doc, 0, "ab\ncd"), 5);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 6);
    xtc_plainIs(doc, "ab\ncd", "insertTextAt ab\\ncd");
    xtc_expectCC(0, 0, 0, 5, "insertTextAt precise range");

    /* 跨块删除（探针 N1："abc\\ndef" 删 2..5 ⇒ "abef" 单块；Qt undo 后
     * pos=5、块复原）。探针序列 = removeSelectedText（position 在选区尾
     * ⇒ op=MoveCursor）；活动光标停删除起点（removeSelectedText 的折叠位）。 */
    XTextDocument_setPlainText(doc, "abc\ndef");
    XQ_EQ_I64(XTextDocument_removeRangeOp(doc, 2, 3, (int)XTDUndoOp_MoveCursor),
              3);
    xtc_plainIs(doc, "abef", "cross-block remove");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XTextDocument_setCursorPosition(doc, 2);
    XTextDocument_undo(doc);
    xtc_plainIs(doc, "abc\ndef", "cross-block remove undo");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    /* Qt 探针 N1 afterUndo pos=5（删除尾随 MoveCursor：undo 后活动光标
     * 落删除起点 + 回填长度）。 */
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 5);

    /* 选区删除折叠到起点，两方向同值（探针 P6/P6b）。 */
    XTextDocument_setPlainText(doc, "abcdef");
    XTextCursor_init(&cur, doc);
    XTextCursor_setPosition(&cur, 3, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_KeepAnchor);
    XQ_CHECK(XTextCursor_hasSelection(&cur));
    XTextCursor_removeSelectedText(&cur);
    XQ_EQ_I64(XTextCursor_position(&cur), 1);
    XQ_EQ_I64(XTextCursor_anchor(&cur), 1);
    xtc_plainIs(doc, "adef", "removeSelectedText reverse side");
    XTextCursor_deinit(&cur);

    /* deleteChar/deletePreviousChar 边界（探针 P5a）。 */
    XTextDocument_setPlainText(doc, "abcd");
    XTextCursor_init(&cur, doc);
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_deleteChar(&cur);
    XQ_EQ_I64(XTextCursor_position(&cur), 1); /* 前向删除光标不动。 */
    xtc_plainIs(doc, "acd", "deleteChar");
    XTextCursor_deletePreviousChar(&cur);
    XQ_EQ_I64(XTextCursor_position(&cur), 0); /* 回格回退一位。 */
    xtc_plainIs(doc, "cd", "deletePreviousChar");
    XTextCursor_movePosition(&cur, XTextCursorMove_Start,
                             XTextCursorMode_MoveAnchor, 1);
    XTextCursor_deletePreviousChar(&cur); /* 文档首 no-op。 */
    XQ_EQ_I64(XTextCursor_position(&cur), 0);
    XTextCursor_movePosition(&cur, XTextCursorMove_End,
                             XTextCursorMode_MoveAnchor, 1);
    XTextCursor_deleteChar(&cur); /* 文档尾 no-op。 */
    XQ_EQ_I64(XTextCursor_position(&cur), 2);
    xtc_plainIs(doc, "cd", "boundary deletes are no-op");

    /* insertBlock：中缝拆块、两块继承段属性（探针 N3/N3b）。 */
    XTextDocument_setPlainText(doc, "abcd");
    XTextDocument_setBlockAlignment(doc, 0, (int)XTDAlignment_HCenter);
    XQ_EQ_I64(XTextDocument_insertBlockAt(doc, 2), 1);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    XQ_EQ_I64(XTextDocument_blockAlignment(doc, 0), (int)XTDAlignment_HCenter);
    XQ_EQ_I64(XTextDocument_blockAlignment(doc, 1), (int)XTDAlignment_HCenter);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 6);

    /* [复核 2026-11-16 P2] insertBlock 有选区先删选区（Qt
     * qtextcursor.cpp:2040-2042 无条件 beginEditBlock→d->remove()→
     * insertBlock；Qt 探针 select(1,4)+insertBlock ⇒ "a\nef"、pos=2）。 */
    XTextDocument_setPlainText(doc, "abcdef");
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 4, XTextCursorMode_KeepAnchor);
    XTextCursor_insertBlock(&cur);
    xtc_plainIs(doc, "a\nef", "insertBlock removes selection");
    XQ_EQ_I64(XTextCursor_position(&cur), 2); /* 选区起点+1 = 新块块首。 */
    XQ_EQ_I64(XTextCursor_anchor(&cur), 2);
    XQ_CHECK(!XTextCursor_hasSelection(&cur));
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    XTextCursor_deinit(&cur);

    /* 删除起点块保留、后续前移（探针 N1c：起点块对齐保留）。 */
    XTextDocument_setPlainText(doc, "ab\ncd");
    XTextDocument_setBlockAlignment(doc, 1, (int)XTDAlignment_Right);
    XQ_EQ_I64(XTextDocument_removeRange(doc, 1, 4), 4);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    xtc_plainIs(doc, "a", "head-block survives merge");
    /* 起点块的对齐是自身原值（未设 = 0），右对齐随被删块消失。 */
    XQ_EQ_I64(XTextDocument_blockAlignment(doc, 0), 0);

    XClassDelete((XClass*)doc);
}

/* ==================== ② 选区跨块 selectedText ==================== */

XQ_TEST(XTextCursor, selectedTextCrossBlock)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor cur;
    char* sel;
    XQ_REQUIRE(doc != NULL);

    /* 块内（探针 P1：sel(2,4) = "cd"）。 */
    XTextDocument_setPlainText(doc, "abcdef");
    XTextCursor_init(&cur, doc);
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 4, XTextCursorMode_KeepAnchor);
    XQ_EQ_I64(XTextCursor_selectionStart(&cur), 2);
    XQ_EQ_I64(XTextCursor_selectionEnd(&cur), 4);
    sel = XTextCursor_selectedText(&cur);
    XQ_REQUIRE(sel != NULL);
    XQ_EQ_STR(sel, "cd");
    XFree_System(sel);

    /* 跨块（探针 P1：sel(1,5) = [0062 2029 0063 0064]——块分隔符 U+2029，
     * Qt 真语义；任务书 '\n' 表述冲突点按 Qt 实装并登记台账）。 */
    XTextDocument_setPlainText(doc, "ab\ncd");
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 5, XTextCursorMode_KeepAnchor);
    sel = XTextCursor_selectedText(&cur);
    XQ_REQUIRE(sel != NULL);
    XQ_EQ_STR(sel, "b\xE2\x80\xA9" "cd");
    XFree_System(sel);
    /* 复杂选区：无表格子集恒 false（Qt 空表分支真值）。 */
    XQ_CHECK(!XTextCursor_hasComplexSelection(&cur));
    /* clearSelection 折叠 anchor（探针 P1 afterClear pos=5 anchor=5）。 */
    XTextCursor_clearSelection(&cur);
    XQ_EQ_I64(XTextCursor_position(&cur), 5);
    XQ_EQ_I64(XTextCursor_anchor(&cur), 5);
    XQ_CHECK(!XTextCursor_hasSelection(&cur));
    XQ_CHECK(XTextCursor_selectedText(&cur) == NULL); /* 无选区=空串。 */
    XTextCursor_deinit(&cur);
    XClassDelete((XClass*)doc);
}

/* ==================== ③ movePosition 全枚举（探针 qtprobe2 逐值对拍） === */

XQ_TEST(XTextCursor, movePositionAllOps)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor cur;
    /* 探针文本 "abc def\ngh ij\nklm"：b0 [0..7] sep=7、b1 [8..12] sep=13、
     * b2 [14..16] sep=17；charCount=18。 */
    XTextDocument_setPlainText(doc, "abc def\ngh ij\nklm");
    XTextCursor_init(&cur, doc);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 18);

    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_Start,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 0); /* 已在起点：moved=false。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Right,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 1);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_EndOfWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 3); /* "abc|"。 */
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_EndOfWord,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 3); /* 词尾原地：false。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_NextWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 4);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_NextWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 7);
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_EndOfLine,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 7); /* 行尾原地。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_StartOfLine,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 0);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_EndOfBlock,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 7); /* 分隔符前。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_NextBlock,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 8);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_NextBlock,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 14);
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_NextBlock,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 14); /* 末块：false。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_End,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 17);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_PreviousCharacter,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 16);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_PreviousWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 14); /* "klm" 词首。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_PreviousWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 13); /* 跨块界到分隔符位。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_PreviousWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 11); /* "ij" 词首。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_PreviousWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 8);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_PreviousBlock,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 0); /* 上一块块首。 */

    /* StartOfWord/EndOfWord 边界（探针 qtprobe2）。 */
    XTextCursor_setPosition(&cur, 12, XTextCursorMode_MoveAnchor);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_StartOfWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 11);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_EndOfWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 13);
    XTextCursor_setPosition(&cur, 13, XTextCursorMode_MoveAnchor);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_StartOfWord,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 11); /* 分隔符位→前词首。 */
    XTextCursor_setPosition(&cur, 8, XTextCursorMode_MoveAnchor);
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_StartOfWord,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 8); /* 块首原地。 */
    XTextCursor_setPosition(&cur, 13, XTextCursorMode_MoveAnchor);
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_EndOfWord,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 13); /* 块尾：false。 */

    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Start,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 0);
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_PreviousBlock,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_PreviousCharacter,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 0);

    /* Up/Down 视觉行（无折行子集=块界；探针 P4 Down/Up 逐值）。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Down,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 8); /* b0 col0 → b1 col0。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Down,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 14);
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_Down,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 14); /* 末块：false。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Up,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 8);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Up,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 0);
    XQ_CHECK(!XTextCursor_movePosition(&cur, XTextCursorMove_Up,
                                       XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 0); /* 首块：false。 */

    /* KeepAnchor + 折叠（探针 P4 RightKeep3 / PrevCharCollapse）。 */
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Right,
                                      XTextCursorMode_KeepAnchor, 3));
    XQ_EQ_I64(XTextCursor_position(&cur), 5);
    XQ_EQ_I64(XTextCursor_anchor(&cur), 2);
    XQ_CHECK(XTextCursor_hasSelection(&cur));
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_PreviousCharacter,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 2); /* 折叠到左端。 */
    XQ_EQ_I64(XTextCursor_anchor(&cur), 2);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_NoMove,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 2); /* NoMove 恒 true 不动。 */
    XTextCursor_setPosition(&cur, 5, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_KeepAnchor);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Right,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 5); /* 折叠到右端（qMax）。 */
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 5, XTextCursorMode_KeepAnchor);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Left,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), 2); /* 折叠到左端（qMin）。 */
    XTextCursor_deinit(&cur);

    /* setPosition 越界忽略（探针 P10）；KeepAnchor 锚不动（探针 N6）。 */
    XTextDocument_setPlainText(doc, "ab");
    XTextCursor_init(&cur, doc);
    XTextCursor_setPosition(&cur, 3, XTextCursorMode_MoveAnchor);
    XQ_EQ_I64(XTextCursor_position(&cur), 0); /* pos==charCount 越界忽略。 */
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XQ_EQ_I64(XTextCursor_position(&cur), 2); /* charCount-1 合法（文档尾）。 */
    XTextCursor_setPosition(&cur, -1, XTextCursorMode_MoveAnchor);
    XQ_EQ_I64(XTextCursor_position(&cur), 2);
    XTextDocument_setPlainText(doc, "abcdef");
    XTextCursor_setPosition(&cur, 4, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_KeepAnchor);
    XQ_EQ_I64(XTextCursor_position(&cur), 1);
    XQ_EQ_I64(XTextCursor_anchor(&cur), 4);
    XTextCursor_setPosition(&cur, 5, XTextCursorMode_KeepAnchor);
    XQ_EQ_I64(XTextCursor_position(&cur), 5);
    XQ_EQ_I64(XTextCursor_anchor(&cur), 4);
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_KeepAnchor);
    XQ_EQ_I64(XTextCursor_position(&cur), 2);
    XQ_EQ_I64(XTextCursor_anchor(&cur), 4);
    XTextCursor_deinit(&cur);
    XClassDelete((XClass*)doc);
}

/** @brief Up/Down 的布局路径契约：列映射必须等于
 *         xToCol(目标块, colToX(当前块, 当前列))，sticky x 一次计算跨次
 *         保持（Qt adjustX=false 口径）——期望值由同一 XTextLayout 引擎
 *         现算（不写死像素，度量随字体自洽）。 */
XQ_TEST(XTextCursor, upDownUsesLayoutColumnKeeping)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor cur;
    XTextLayout* lay0;
    XTextLayout* lay1;
    XTextLayout* lay2;
    int x;
    int expectCol1;
    int expectCol2;
    int p1;
    XQ_REQUIRE(doc != NULL);
    XTextDocument_setPlainText(doc, "abcdefgh\nijklmnop\nqrstuvwx");
    XTextCursor_init(&cur, doc);
    lay0 = XTextLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    lay1 = XTextLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    lay2 = XTextLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(lay0 != NULL);
    XQ_REQUIRE(lay1 != NULL);
    XQ_REQUIRE(lay2 != NULL);
    XTextLayout_setText_utf8(lay0, "abcdefgh", -1);
    XTextLayout_setText_utf8(lay1, "ijklmnop", -1);
    XTextLayout_setText_utf8(lay2, "qrstuvwx", -1);
    x = XTextLayout_colToX(lay0, 0, 3);
    expectCol1 = XTextLayout_xToCol(lay1, 0, x);
    expectCol2 = XTextLayout_xToCol(lay2, 0, x);

    XTextCursor_setPosition(&cur, 3, XTextCursorMode_MoveAnchor);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Down,
                                      XTextCursorMode_MoveAnchor, 1));
    p1 = XTextCursor_position(&cur);
    XQ_EQ_I64(p1, 9 + expectCol1); /* b1 起点 9（8 文本+1 分隔符）+ 同 x 列。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Down,
                                      XTextCursorMode_MoveAnchor, 1));
    /* sticky x 保持：b2（起点 18）取同一 x 的列。 */
    XQ_EQ_I64(XTextCursor_position(&cur), 18 + expectCol2);
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Up,
                                      XTextCursorMode_MoveAnchor, 1));
    XQ_EQ_I64(XTextCursor_position(&cur), p1); /* Up 回同 x 列。 */
    XQ_CHECK(XTextCursor_movePosition(&cur, XTextCursorMove_Up,
                                      XTextCursorMode_MoveAnchor, 1));
    /* 回 b0：同一 x 在本块的列（引擎往返自洽 ⇒ 回原列 3）。 */
    XQ_EQ_I64(XTextCursor_position(&cur),
              XTextLayout_xToCol(lay0, 0, x));
    XClassDelete((XClass*)lay0);
    XClassDelete((XClass*)lay1);
    XClassDelete((XClass*)lay2);
    XTextCursor_deinit(&cur);
    XClassDelete((XClass*)doc);
}

/* ==================== ④ 格式继承（探针 P2/P2b/P2c/P2d/P13/N5） ========== */

XQ_TEST(XTextCursor, insertFormatInheritance)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor cur;
    XTextCharFormat bold;
    XQ_REQUIRE(doc != NULL);
    XTextCharFormat_init(&bold);
    XTextCharFormat_setFontWeight(&bold, 700);

    /* 块中间插入继承前一字符（探针 P2：AxB 三字节单片段全 bold）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_init(&cur, doc);
    XTextCursor_insertTextFormat(&cur, "AB", &bold);
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "x");
    xtc_plainIs(doc, "AxB", "mid insert");
    xtc_fragCount(doc, 0, 1, "mid-insert merges equal format");
    xtc_fragBold(doc, 0, 0, 1, "mid-insert inherits bold");

    /* 块首继承块首字符（探针 P2b：Xab 全 bold）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertTextFormat(&cur, "ab", &bold);
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "X");
    xtc_plainIs(doc, "Xab", "block-start insert");
    xtc_fragBold(doc, 0, 0, 1, "block-start inherits first char");

    /* 块尾继承前一字符（探针 P2d：abc 单片段 bold）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertTextFormat(&cur, "ab", &bold);
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "c");
    xtc_fragCount(doc, 0, 1, "end-insert merges");
    xtc_fragBold(doc, 0, 0, 1, "end-insert inherits last char");

    /* 位置 0 / 空文档：先继承文档缺省（探针 P2c：空文档 charFormat
     * bold=0；setCharFormat 缓存后插入 bold）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    {
        XTDCharFormat fmtAt;
        XTextDocument_charFormatAt(doc, 0, &fmtAt);
        XQ_EQ_I64((int)fmtAt.bold, 0); /* 缺省非粗体。 */
        xtc_freeFmt(&fmtAt);
    }
    XTextCursor_setCharFormat(&cur, &bold); /* 空选区=插入格式缓存。 */
    XTextCursor_insertText(&cur, "hi");
    xtc_fragBold(doc, 0, 0, 1, "cached insert format bold");

    /* 移动后缓存失效（探针 N5：setPosition 后按真实片段重算）。 */
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "!"); /* 继承块首字符（bold）。 */
    xtc_plainIs(doc, "!hi", "cache invalidated by setPosition");
    xtc_fragCount(doc, 0, 1, "recomputed inherit merges");
    xtc_fragBold(doc, 0, 0, 1, "recomputed inherit bold");

    /* [复核 2026-11-16 P3] clearSelection 无条件失效插入格式缓存
     * （Qt qtextcursor.cpp:1620-1626 的 currentCharFormat=-1）：清选区后
     * 立即插入按位置继承，不吃旧缓存。 */
    XTextDocument_setPlainText(doc, "ab");
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_setCharFormat(&cur, &bold); /* 缓存 bold。 */
    XTextCursor_clearSelection(&cur);       /* 缓存失效。 */
    XTextCursor_insertText(&cur, "Z");      /* 继承前邻 'a'（非 bold）。 */
    xtc_plainIs(doc, "aZb", "clearSelection invalidates insert cache");
    xtc_fragBold(doc, 0, 0, 0, "Z inherits plain, not stale cache");

    /* 空块继承前块末字符（探针 P13：insertBlock 后 charFormat bold=1，
     * 随后 cd 落 bold 片段）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertTextFormat(&cur, "AB", &bold);
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XTextCursor_insertBlock(&cur);
    XQ_EQ_I64(XTextCursor_position(&cur), 3); /* 新块块首（探针 P9/N3）。 */
    XTextCursor_insertText(&cur, "cd");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xtc_fragBold(doc, 1, 0, 1, "empty-block inherits prev last char");
    XTextCursor_deinit(&cur);
    XTextFormat_deinit(&bold);
    XClassDelete((XClass*)doc);
}

/* ==================== ⑤ setCharFormat / mergeCharFormat（探针 P11/P12） == */

XQ_TEST(XTextCursor, setMergeCharFormatAndPoolSync)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor cur;
    XTextCharFormat bold;
    XTextCharFormat red;
    XQ_REQUIRE(doc != NULL);
    XTextCharFormat_init(&bold);
    XTextCharFormat_setFontWeight(&bold, 700);
    XTextCharFormat_init(&red);
    XTextCharFormat_setForeground(&red, 0xFFAA0000u);

    /* 选区 1..4 整段替换（探针 P11：三段 [0..1)普通 [1..4)粗 [4..6)普通）。 */
    XTextDocument_setPlainText(doc, "abcdef");
    XTextCursor_init(&cur, doc);
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 4, XTextCursorMode_KeepAnchor);
    xtc_connect(doc); /* 只记录本次格式操作的信号。 */
    XTextCursor_setCharFormat(&cur, &bold);
    xtc_fragCount(doc, 0, 3, "setCharFormat splits at both ends");
    xtc_fragBold(doc, 0, 0, 0, "head stays plain");
    xtc_fragBold(doc, 0, 1, 1, "middle bold");
    xtc_fragBold(doc, 0, 2, 0, "tail stays plain");
    XQ_EQ_I64(XTextCursor_position(&cur), 4); /* 选区保持（探针 P11）。 */
    XQ_EQ_I64(XTextCursor_anchor(&cur), 1);
    /* 内联与池影子一致：片段已入池（>=0）且渲染读内联字段。 */
    XQ_CHECK(XTextDocument_fragmentPoolIndex(doc, 0, 1) >= 0);
    /* contentsChange 精确区间 (1, 3, 3)（Qt CharFormatChanged 口径）。 */
    xtc_expectCC(0, 1, 3, 3, "setCharFormat signal");

    /* merge：属性空间合并——选区内片段染色、选区外（"ef"）不动。 */
    XTextCursor_mergeCharFormat(&cur, &red);
    {
        const XTDFragment* f1 = XTextDocument_fragment(doc, 0, 1);
        const XTDFragment* f2 = XTextDocument_fragment(doc, 0, 2);
        XQ_REQUIRE(f1 != NULL);
        XQ_REQUIRE(f2 != NULL);
        XQ_EQ_I64((int)f1->fmt.bold, 1);
        XQ_EQ_I64((long long)(unsigned)f1->fmt.fgColor, 0xFFAA0000ll);
        XQ_EQ_I64((long long)(unsigned)f2->fmt.fgColor, 0); /* 选区外不动。 */
        XQ_EQ_I64((int)f2->fmt.bold, 0);
    }
    XQ_CHECK(XTextDocument_fragmentPoolIndex(doc, 0, 1) >= 0);

    /* mergeCharFormat 空选区：并入插入格式缓存（Qt :1921-1926），随插入
     * 落片段。位置 4 = "d|e" 之间；插入格式与 "bcd" 片段同格式 ⇒ Qt unite
     * 口径并入前邻（探针 P2 合并语义）。 */
    XTextCursor_clearSelection(&cur);
    XTextCursor_mergeCharFormat(&cur, &bold);
    XTextCursor_insertText(&cur, "Z");
    xtc_plainIs(doc, "abcdZef", "merge cache insert");
    {
        const XTDFragment* fz = XTextDocument_fragment(doc, 0, 1);
        XQ_REQUIRE(fz != NULL);
        XQ_EQ_I64((int)fz->fmt.bold, 1);
        xtc_fragCount(doc, 0, 3, "same-format insert unites");
    }
    XTextCursor_deinit(&cur);

    /* 块格式作用于选区内所有块（探针 P12：三块同改对齐）。 */
    {
        XTextBlockFormat bf;
        int i;
        XTextBlockFormat_init(&bf);
        XTextBlockFormat_setAlignment(&bf, (int)XTDAlignment_HCenter);
        XTextDocument_setPlainText(doc, "ab\ncd\nef");
        XTextCursor_init(&cur, doc);
        XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
        XTextCursor_setPosition(&cur, 8, XTextCursorMode_KeepAnchor);
        XTextCursor_setBlockFormat(&cur, &bf);
        for (i = 0; i < 3; ++i)
            XQ_EQ_I64(XTextDocument_blockAlignment(doc, i),
                      (int)XTDAlignment_HCenter);
        /* 读回：blockFormat 载荷对齐一致。 */
        {
            XTextBlockFormat back;
            XTextCursor_blockFormat(&cur, &back);
            XQ_EQ_I64(XTextBlockFormat_alignment(&back),
                      (int)XTDAlignment_HCenter);
            XTextFormat_deinit(&back);
        }
        XTextFormat_deinit(&bf);
        XTextCursor_deinit(&cur);
    }
    XTextFormat_deinit(&bold);
    XTextFormat_deinit(&red);
    XClassDelete((XClass*)doc);
}

/* ==================== ⑥⑦ 编辑块合并撤销 + undo/redo 光标（探针 P8/N8/
 *    N1/N2a/N2c/N2d/R1/R4/R5/R6） ==================== */

XQ_TEST(XTextCursor, editBlockAndUndoRedoCursor)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor cur;
    XQ_REQUIRE(doc != NULL);

    /* ⑥ 编辑块：两次插入合并一步撤销（探针 P8 steps=1）；信号在块尾按
     * 合并区间发一次（探针 N8 一次 contentsChange）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_init(&cur, doc);
    xtc_connect(doc); /* 只记录编辑块的信号。 */
    XTextCursor_beginEditBlock(&cur);
    XTextCursor_insertText(&cur, "ab");
    XTextCursor_insertText(&cur, "cd");
    XTextCursor_endEditBlock(&cur);
    XQ_EQ_I64(XTextDocument_availableUndoSteps(doc), 1);
    XQ_EQ_I64(XTextCursor_position(&cur), 4);
    xtc_plainIs(doc, "abcd", "edit block content");
    /* 编辑块内挂起：endEditBlock 才发一次（精确合并区间 (0,0,4)）+
     * contentsChanged（块数未变无 blockCountChanged ⇒ 共 2 条）。 */
    xtc_expectCC(0, 0, 0, 4, "edit block merged contentsChange");
    XQ_EQ_I64(s_tcEventN, 2);
    XTextDocument_undo(doc);
    xtc_plainIs(doc, "", "edit block single undo clears all");
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 0); /* undo 插入→插入点。 */

    /* 空编辑块不占撤销步、不发信号（探针 P8c steps=0）。 */
    xtc_resetEvents();
    XTextDocument_beginEditBlock(doc);
    XTextDocument_endEditBlock(doc);
    XQ_EQ_I64(XTextDocument_availableUndoSteps(doc), 0);
    XQ_EQ_I64(s_tcEventN, 0);
    XQ_CHECK(!XTextDocument_isUndoAvailable(doc));

    /* [复核 2026-11-16 P2] 标脏口径双向一致：块外与块内编辑都无条件
     * markContentsDirty（Qt insert/remove 无条件 documentChange；
     * W3 布局消费脏区时两条路径等价）。setPlainText 不标脏也不复位脏区
     * （既有口径）⇒ 先显式复位再断言。 */
    XTextDocument_setPlainText(doc, "");
    XTextDocument_resetContentsDirty(doc);
    XQ_CHECK(!XTextDocument_isContentsDirty(doc));
    XTextDocument_insertTextAt(doc, 0, "ab"); /* 块外编辑。 */
    XQ_CHECK(XTextDocument_isContentsDirty(doc));
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), 0);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 2);
    XTextDocument_resetContentsDirty(doc);
    XQ_CHECK(!XTextDocument_isContentsDirty(doc));
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XTextDocument_beginEditBlock(doc);
    XTextCursor_insertText(&cur, "cd"); /* 块内编辑。 */
    XTextDocument_endEditBlock(doc);
    XQ_CHECK(XTextDocument_isContentsDirty(doc)); /* 块内同款标脏。 */
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), 2);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 2);
    XTextDocument_resetContentsDirty(doc);

    /* 块外两次不相邻编辑 = 两步（探针 N2a steps=2；undo 一次只回一步）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "ab");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "X");
    XQ_EQ_I64(XTextDocument_availableUndoSteps(doc), 2);
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 1);
    XTextDocument_undo(doc);
    xtc_plainIs(doc, "ab", "N2a afterUndo"); /* Qt 探针同值。 */
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 0); /* N2a pos=0。 */

    /* ⑦ undo/redo 光标映射（Qt 探针逐值）：
     * - undo 前向删除（KeepCursor）→ 光标原位（N2c/R4）；
     * - undo 回格（MoveCursor）→ 回到删除前位置（N2d/R6）；
     * - undo 替换（位置在选区尾）→ 光标落回填尾（R1=3）；
     * - redo 镜像（R1 afterRedo=3、R4/R6 afterRedo=1）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "abcd");
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_deleteChar(&cur);
    XQ_EQ_I64(XTextCursor_position(&cur), 1);
    XTextDocument_undo(doc);
    xtc_plainIs(doc, "abcd", "R4 afterUndo");
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 1); /* 原位（KeepCursor）。 */
    XTextDocument_redo(doc);
    xtc_plainIs(doc, "acd", "R4 afterRedo");
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 1); /* R4 afterRedo=1。 */

    /* R6 用独立文本（Qt 探针 R6 是新文档 "abcd"；R4 结束时文本是 "acd"）。 */
    XTextDocument_setPlainText(doc, "abcd");
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XTextCursor_deletePreviousChar(&cur);
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 1);
    xtc_plainIs(doc, "acd", "R6 backspace");
    XTextDocument_undo(doc);
    xtc_plainIs(doc, "abcd", "R6 afterUndo");
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 2); /* R6 afterUndo=2。 */
    XTextDocument_redo(doc);
    xtc_plainIs(doc, "acd", "R6 afterRedo");
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 1); /* R6 afterRedo=1。 */

    /* 替换选区（R1：position 在选区尾）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "abcdef");
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 3, XTextCursorMode_KeepAnchor);
    XTextCursor_insertText(&cur, "XY");
    xtc_plainIs(doc, "aXYdef", "R1 replace");
    XQ_EQ_I64(XTextCursor_position(&cur), 3);
    XTextDocument_undo(doc);
    xtc_plainIs(doc, "abcdef", "R1 afterUndo");
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 3); /* R1 afterUndo=3。 */
    XTextDocument_redo(doc);
    xtc_plainIs(doc, "aXYdef", "R1 afterRedo");
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 3); /* R1 afterRedo=3。 */

    /* insertBlock 的 undo（探针 R5：pos 3→2、块数复原）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "abcd");
    XTextCursor_setPosition(&cur, 2, XTextCursorMode_MoveAnchor);
    XTextCursor_insertBlock(&cur);
    XQ_EQ_I64(XTextCursor_position(&cur), 3);
    XTextDocument_undo(doc);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    xtc_plainIs(doc, "abcd", "R5 afterUndo");
    XQ_EQ_I64(XTextDocument_cursorPosition(doc), 2); /* R5 afterUndo=2。 */
    XTextCursor_deinit(&cur);
    XClassDelete((XClass*)doc);
}

/* ==================== ⑧ 与 Qt 探针同序列对拍（拆块 + UTF-8 + 池影子） == */

XQ_TEST(XTextCursor, probeParitySequence)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor cur;
    char* sel;
    XQ_REQUIRE(doc != NULL);

    /* 探针 P3：insertText("ab\ncd") → pos=5、两块。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_init(&cur, doc);
    XTextCursor_insertText(&cur, "ab\ncd");
    XQ_EQ_I64(XTextCursor_position(&cur), 5);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    /* 探针 P9：setPosition(1)+insertBlock → b0="a"、新块 "b"、pos=2。 */
    XTextCursor_setPosition(&cur, 1, XTextCursorMode_MoveAnchor);
    XTextCursor_insertBlock(&cur);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xtc_plainIs(doc, "a\nb\ncd", "P9 insertBlock mid-text");
    XQ_EQ_I64(XTextCursor_position(&cur), 2);

    /* 探针 N4：尾部 '\n' 造空块；'\r\n' 合并一个分隔符。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "ab\ncd\n");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    XQ_EQ_I64(XTextCursor_position(&cur), 6);
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "ab\r\ncd");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xtc_plainIs(doc, "ab\ncd", "N4b crlf single separator");

    /* UTF-8：框架 UTF-8 字节位置口径的插入/删除/选区（一个码点一步）。 */
    XTextDocument_setPlainText(doc, "");
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
    XTextCursor_insertText(&cur, "\xE4\xB8\xAD\xE6\x96\x87"); /* 中文 */
    XQ_EQ_I64(XTextDocument_characterCount(doc), 7); /* 6 字节 + 分隔符。 */
    XTextCursor_setPosition(&cur, 3, XTextCursorMode_MoveAnchor);
    XTextCursor_deletePreviousChar(&cur); /* 回退一个码点（3 字节）。 */
    XQ_EQ_I64(XTextCursor_position(&cur), 0);
    xtc_plainIs(doc, "\xE6\x96\x87", "utf8 backspace removes one codepoint");
    XTextCursor_setPosition(&cur, 3, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&cur, 0, XTextCursorMode_KeepAnchor);
    sel = XTextCursor_selectedText(&cur);
    XQ_REQUIRE(sel != NULL);
    XQ_EQ_STR(sel, "\xE6\x96\x87");
    XFree_System(sel);

    /* 格式池影子与片段内联一致（insertTextFormat → 池去重 upsert）。 */
    {
        XTextCharFormat bold;
        XTextFormat_init(&bold, XTextFormatType_CharFormat);
        XTextCharFormat_setFontWeight(&bold, 700);
        XTextCursor_setPosition(&cur, 0, XTextCursorMode_MoveAnchor);
        XTextCursor_insertTextFormat(&cur, "B", &bold);
        XQ_CHECK(XTextDocument_fragmentPoolIndex(doc, 0, 0) >= 0);
        xtc_fragBold(doc, 0, 0, 1, "probe parity bold fragment");
        XTextFormat_deinit(&bold);
    }
    XTextCursor_deinit(&cur);
    XClassDelete((XClass*)doc);
}

/* ==================== 值对象四件套（copy/move/deinit 账目） ==================== */

XQ_TEST(XTextCursor, valueSemantics)
{
    XTextDocument* doc = XTextDocument_create();
    XTextCursor a;
    XTextCursor b;
    XTextCursor c;
    XTextCharFormat bold;
    XQ_REQUIRE(doc != NULL);
    XTextDocument_setPlainText(doc, "hello");
    XTextCharFormat_init(&bold);
    XTextCharFormat_setFontWeight(&bold, 700);

    XTextCursor_init(&a, doc);
    XTextCursor_setPosition(&a, 3, XTextCursorMode_MoveAnchor);
    XTextCursor_setPosition(&a, 1, XTextCursorMode_KeepAnchor);
    XTextCursor_setCharFormat(&a, &bold); /* 缓存 bold（深拷贝入光标）。 */

    XTextCursor_init(&b, doc);
    XTextCursor_copy(&b, &a); /* 深拷贝（格式字符串字段独立）。 */
    XQ_EQ_I64(XTextCursor_position(&b), 1);
    XQ_EQ_I64(XTextCursor_anchor(&b), 3);
    XQ_CHECK(XTextCursor_hasSelection(&b));
    XTextCursor_deinit(&b);

    XTextCursor_init(&c, doc);
    XTextCursor_move(&c, &a); /* 载荷转移；a 复位空壳。 */
    XQ_EQ_I64(XTextCursor_position(&c), 1);
    XQ_CHECK(XTextCursor_isNull(&a)); /* 空壳（文档指针清零）。 */
    XTextCursor_deinit(&a);           /* 空壳重复 deinit 幂等。 */
    XTextCursor_deinit(&c);

    XTextCursor_init(&a, NULL); /* null 光标：查询缺省不崩、编辑 no-op。 */
    XQ_CHECK(XTextCursor_isNull(&a));
    XQ_EQ_I64(XTextCursor_position(&a), -1);
    XQ_EQ_I64(XTextCursor_blockNumber(&a), 0);
    XQ_CHECK(!XTextCursor_hasSelection(&a));
    XQ_CHECK(!XTextCursor_movePosition(&a, XTextCursorMove_Right,
                                       XTextCursorMode_MoveAnchor, 1));
    XTextCursor_insertText(&a, "x"); /* no-op。 */
    XTextCursor_deinit(&a);

    /* blockNumber 口径：位置 6 = "hello" 末分隔符位 → 块 0。 */
    XTextCursor_init(&b, doc);
    XTextCursor_setPosition(&b, 8, XTextCursorMode_MoveAnchor); /* 越界忽略。 */
    XQ_EQ_I64(XTextCursor_position(&b), 0);
    XTextCursor_setPosition(&b, 6, XTextCursorMode_MoveAnchor);
    XQ_EQ_I64(XTextCursor_blockNumber(&b), 0);
    XTextCursor_deinit(&b);
    XTextFormat_deinit(&bold);
    XClassDelete((XClass*)doc);
}

#endif /* XGUI_ON && XTEXTDOCUMENT_ON */
