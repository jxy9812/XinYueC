/**
 * @file       XTextDocumentAlignTest.c
 * @brief      XTextDocument 与 Qt 6.8 QTextDocument 的对齐契约测试。
 * @details    覆盖：
 *             - 构造默认值（Qt 6.8 探针：空文档 blockCount=1、
 *               characterCount=1、lineCount=1、isEmpty=true、revision=0、
 *               modified=false、undoRedoEnabled=true、maximumBlockCount=0）；
 *             - **characterCount 的块分隔符语义**（本轮对齐：Qt 的
 *               `QTextDocument::characterCount()` 是各块 `QTextBlock::length()`
 *               之和，每块含 1 个分隔符 ⇒ "abc" 为 4、"a\nb" 为 4；
 *               文本部分本库按 UTF-8 字节计，见头文件 @note）；
 *             - setPlainText/toPlainText/toHtml 往返与 blockCount/lineCount；
 *             - find/find_ex 的大小写与起点口径 + **findCursor 的 QTextCursor
 *               语义**（position 落在匹配末尾、anchor 为起点、反向取起点
 *               严格小于 from 的最后一个匹配）；
 *             - modified 属性 + modificationChanged 信号（Qt 6.8.0 实测：
 *               setPlainText 之后 isModified()==true——装载经 insertText
 *               落文档、modified 不复位；只有 QTextEdit 的 setPlainText
 *               才由控件路径复位）、setMaximumBlockCount
 *               的裁剪与"同时关闭撤销重做"（qtextdocument.cpp:518-524）；
 *             - clone 的独立性与 undo/redo 快照栈；
 *             - baseUrl/metaInformation 往返与 baseUrlChanged 信号；
 *             - Qt 独有、本库未建的 API 项见批次 gaps（不在此断言）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XTextDocument.h"
#include "XObject.h"
#include "XClass.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XVarList.h"
#include "XMemory.h"

#include <stdio.h>
#include <string.h>

#if XTEXTDOCUMENT_ON

/* ==================== 信号计数槽 ==================== */

static int s_modified;  /**< modificationChanged 次数。 */
static int s_baseUrl;   /**< baseUrlChanged 次数。 */

static void xtd_slotModified(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_modified;
}

static void xtd_slotBaseUrl(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_baseUrl;
}

/** @brief 取 toPlainText 堆结果的借用视图（用完 XFree_System）。 */
static char* xtd_plain(const XTextDocument* doc)
{
    char* raw = XTextDocument_toPlainText(doc);
    return raw; /* toPlainText 已返回堆拷贝（调用方 XFree_System）。 */
}

/* ==================== contentsChange 精确区间记录器 ==================== */

/** @brief 一条信号事件（顺序/取值断言用）。 */
typedef struct XTDChangeEvent
{
    int kind;   /**< 0=contentsChange 1=contentsChanged 2=blockCountChanged */
    int from;   /**< contentsChange 起点。 */
    int removed;/**< contentsChange 移除数。 */
    int added;  /**< contentsChange 新增数。 */
    int count;  /**< blockCountChanged 新块数。 */
} XTDChangeEvent;

#define XTD_EVENT_MAX 32
static XTDChangeEvent s_events[XTD_EVENT_MAX];
static int s_eventN;

static void xtd_resetEvents(void) { s_eventN = 0; }

static void xtd_slotContentsChange(XObject* receiver, XVarList* args)
{
    XTDChangeEvent* ev;
    (void)receiver;
    if (s_eventN >= XTD_EVENT_MAX) return;
    ev = &s_events[s_eventN++];
    ev->kind = 0;
    ev->count = 0;
    XVarList_args_3(args, int, from, int, removed, int, added);
    ev->from = from;
    ev->removed = removed;
    ev->added = added;
}

static void xtd_slotContentsChanged(XObject* receiver, XVarList* args)
{
    XTDChangeEvent* ev;
    (void)receiver;
    (void)args;
    if (s_eventN >= XTD_EVENT_MAX) return;
    ev = &s_events[s_eventN++];
    ev->kind = 1;
    ev->from = ev->removed = ev->added = ev->count = 0;
}

static void xtd_slotBlockCountChanged(XObject* receiver, XVarList* args)
{
    XTDChangeEvent* ev;
    (void)receiver;
    if (s_eventN >= XTD_EVENT_MAX) return;
    ev = &s_events[s_eventN++];
    ev->kind = 2;
    ev->from = ev->removed = ev->added = 0;
    XVarList_args_1(args, int, count);
    ev->count = count;
}

/** @brief 挂接三个内容信号（receiver 取文档自身，槽不解引用载荷指针）。 */
static void xtd_connectContentSignals(XTextDocument* doc)
{
    xtd_resetEvents();
    XObject_connect((XObject*)doc,
                      (size_t)XTextDocument_contentsChange_signal(doc, 0, 0, 0),
                      (XObject*)doc, xtd_slotContentsChange,
                      XConnectionType_Direct);
    XObject_connect((XObject*)doc,
                      (size_t)XTextDocument_contentsChanged_signal(doc),
                      (XObject*)doc, xtd_slotContentsChanged,
                      XConnectionType_Direct);
    XObject_connect((XObject*)doc,
                      (size_t)XTextDocument_blockCountChanged_signal(doc, 0),
                      (XObject*)doc, xtd_slotBlockCountChanged,
                      XConnectionType_Direct);
}

/** @brief 断言第 i 条事件是 contentsChange(from,removed,added)。 */
static void xtd_expectCC(int i, int from, int removed, int added,
                         const char* what)
{
    if (i >= s_eventN) {
        xq_fail_at(__FILE__, __LINE__, "%s：缺少第 %d 条事件（共 %d 条）",
                   what ? what : "", i, s_eventN);
        return;
    }
    xq_check_impl(s_events[i].kind == 0 && s_events[i].from == from
                      && s_events[i].removed == removed
                      && s_events[i].added == added,
                  "contentsChange(from,removed,added)", __FILE__, __LINE__,
                  "%s：第 %d 条实际 kind=%d (%d,%d,%d)",
                  what ? what : "", i, s_events[i].kind, s_events[i].from,
                  s_events[i].removed, s_events[i].added);
}


XQ_TEST(XTextDocument, defaultsAndCharacterCount)
{
    XTextDocument* doc = XTextDocument_create();
    char* text;
    XQ_REQUIRE(doc != NULL);

    /* Qt 6.8 探针：空文档 = 1 块 + 1 个分隔符 ⇒ characterCount 1。 */
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 1);
    XQ_EQ_I64(XTextDocument_lineCount(doc), 1);
    XQ_CHECK(XTextDocument_isEmpty(doc));
    XQ_EQ_I64(XTextDocument_revision(doc), 0);
    XQ_CHECK(!XTextDocument_isModified(doc));
    XQ_CHECK(XTextDocument_isUndoRedoEnabled(doc));
    XQ_EQ_I64(XTextDocument_maximumBlockCount(doc), 0);
    XQ_EQ_STR(XTextDocument_baseUrl(doc), "");
    XQ_EQ_STR(XTextDocument_metaInformation(doc,
                                            XTDMetaInformation_DocumentTitle),
              "");

    /* "abc"：1 块（3 字节）+ 1 分隔符 = 4（Qt 探针同为 4）。 */
    XTextDocument_setPlainText(doc, "abc");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 4);
    XQ_CHECK(!XTextDocument_isEmpty(doc));
    text = xtd_plain(doc);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "abc");
    XFree_System(text);

    /* "a\nb"：2 块（2 字节）+ 2 分隔符 = 4（Qt 探针同为 4）。 */
    XTextDocument_setPlainText(doc, "a\nb");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 4);
    XQ_EQ_I64(XTextDocument_lineCount(doc), 2);

    /* UTF-8 字节口径：两字中文 6 字节 + 1 分隔符 = 7（Qt 按 QChar 为 3）。 */
    XTextDocument_setPlainText(doc, "中文");
    XQ_EQ_I64(XTextDocument_characterCount(doc), 7);

    /* 空文本：回到 1 空块 + 1 分隔符。 */
    XTextDocument_setPlainText(doc, "");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 1);
    XQ_CHECK(XTextDocument_isEmpty(doc));

    XClassDelete((XClass*)doc);
}

XQ_TEST(XTextDocument, findAndModifiedSignal)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);

    XTextDocument_setPlainText(doc, "Hello World hello");
    /* 默认大小写不敏感（QTextDocument::find 默认 FindFlags() 无
       FindCaseSensitively）；find/find_ex 返回匹配**起始**字符位置
       （便利口径），QTextCursor 语义见 findCursor（下方断言）。 */
    XQ_EQ_I64(XTextDocument_find(doc, "hello"), 0);
    XQ_EQ_I64(XTextDocument_find_ex(doc, "hello", 0,
                                    XTDFindFlag_CaseSensitively),
              12);
    XQ_EQ_I64(XTextDocument_find(doc, "zzz"), -1);
    XQ_EQ_I64(XTextDocument_find(doc, ""), -1);

    /* findCursor：Qt QTextCursor 语义（本机 Qt 6.8 探针：首个匹配
       "Hello" ⇒ position=5（末尾）、anchor=0、hasSelection=true；
       自 position 5 继续 ⇒ position=17/anchor=12；未命中 ⇒ null 游标
       position/anchor 均 -1）。 */
    {
        XTDTextCursor cur = XTextDocument_findCursor(doc, "hello", 0, 0);
        XQ_EQ_I64(cur.anchor, 0);
        XQ_EQ_I64(cur.position, 5);
        XQ_CHECK(cur.hasSelection);
        cur = XTextDocument_findCursor(doc, "hello", cur.position, 0);
        XQ_EQ_I64(cur.anchor, 12);
        XQ_EQ_I64(cur.position, 17);
        XQ_CHECK(cur.hasSelection);
        cur = XTextDocument_findCursor(doc, "zzz", 0, 0);
        XQ_EQ_I64(cur.anchor, -1);
        XQ_EQ_I64(cur.position, -1);
        XQ_CHECK(!cur.hasSelection);
    }

    /* 反向查找：Qt 口径「起点严格小于 from 的最后一个匹配」
       （本机探针 from=12 ⇒ 0..5、from=13 ⇒ 12..17）。 */
    {
        XTDTextCursor cur = XTextDocument_findCursor(
            doc, "hello", 12, (int)XTDFindFlag_Backward);
        XQ_EQ_I64(cur.anchor, 0);
        XQ_EQ_I64(cur.position, 5);
        cur = XTextDocument_findCursor(doc, "hello", 13,
                                       (int)XTDFindFlag_Backward);
        XQ_EQ_I64(cur.anchor, 12);
        XQ_EQ_I64(cur.position, 17);
    }

    /* Qt 6.8.0 实测：QTextDocument::setPlainText 之后 isModified()==true
     * （装载经 insertText 落文档，modified 不复位；只有 QTextEdit 的
     * setPlainText 才由控件路径把 modified 置回 false）。因此先断言该
     * 初始态，再把 modified 清零建立「变化才发信号」的基线。 */
    XQ_CHECK(XTextDocument_isModified(doc));
    s_modified = 0;
    XObject_connect((XObject*)doc,
                      (size_t)XTextDocument_modificationChanged_signal(doc,
                                                                      false),
                      (XObject*)doc, xtd_slotModified,
                      XConnectionType_Direct);
    XTextDocument_setModified(doc, false); /* true→false：发一次。 */
    XQ_CHECK(!XTextDocument_isModified(doc));
    XQ_EQ_I64(s_modified, 1);
    XTextDocument_setModified(doc, true); /* false→true：再发一次。 */
    XQ_CHECK(XTextDocument_isModified(doc));
    XQ_EQ_I64(s_modified, 2);
    /* 同值重复设置不再发信号（Qt setModified 的 m == modified 早退）。 */
    XTextDocument_setModified(doc, true);
    XQ_EQ_I64(s_modified, 2);

    /* baseUrl 往返 + baseUrlChanged（Qt baseUrl 属性 NOTIFY）。 */
    s_baseUrl = 0;
    XObject_connect((XObject*)doc,
                      (size_t)XTextDocument_baseUrlChanged_signal(doc),
                      (XObject*)doc, xtd_slotBaseUrl,
                      XConnectionType_Direct);
    XTextDocument_setBaseUrl(doc, "file:///tmp");
    XQ_EQ_STR(XTextDocument_baseUrl(doc), "file:///tmp");
    XQ_EQ_I64(s_baseUrl, 1);

    /* metaInformation 四类往返。 */
    XTextDocument_setMetaInformation(doc, XTDMetaInformation_DocumentTitle,
                                     "标题");
    XQ_EQ_STR(XTextDocument_metaInformation(doc,
                                            XTDMetaInformation_DocumentTitle),
              "标题");

    XClassDelete((XClass*)doc);
}

XQ_TEST(XTextDocument, maxBlockCountUndoAndClone)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocument* copy;
    char* text;
    XQ_REQUIRE(doc != NULL);

    /* Qt qtextdocument.cpp:518-524：设置上限会先裁剪、再关闭撤销重做。 */
    XTextDocument_setPlainText(doc, "1\n2\n3");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    XTextDocument_setMaximumBlockCount(doc, 2);
    XQ_EQ_I64(XTextDocument_maximumBlockCount(doc), 2);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    XQ_CHECK(!XTextDocument_isUndoRedoEnabled(doc));
    text = xtd_plain(doc);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "2\n3");
    XFree_System(text);

    /* Qt 探针：负值原样保存（setMaximumBlockCount(-4) 后返回 -4）且撤销/
       重做同样关闭；负值表示不限 ⇒ 现有块不裁剪。 */
    XTextDocument_setUndoRedoEnabled(doc, true);
    XTextDocument_setMaximumBlockCount(doc, -4);
    XQ_EQ_I64(XTextDocument_maximumBlockCount(doc), -4);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    XQ_CHECK(!XTextDocument_isUndoRedoEnabled(doc));

    /* 撤销/重做快照栈：setPlainText 清史，addFragment 记一步。 */
    XTextDocument_setMaximumBlockCount(doc, 0);
    XTextDocument_setUndoRedoEnabled(doc, true);
    XTextDocument_setPlainText(doc, "abc");
    XQ_CHECK(!XTextDocument_isUndoAvailable(doc));
    XTextDocument_appendText(doc, "d", NULL);
    XQ_EQ_I64(XTextDocument_fragmentCount(doc, 0), 2);
    XQ_CHECK(XTextDocument_isUndoAvailable(doc));
    XTextDocument_undo(doc);
    text = xtd_plain(doc);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "abc");
    XFree_System(text);
    XQ_CHECK(XTextDocument_isRedoAvailable(doc));
    XTextDocument_redo(doc);
    text = xtd_plain(doc);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "abcd");
    XFree_System(text);

    /* clone：内容独立（修改副本不影响源）。 */
    copy = XTextDocument_clone(doc);
    XQ_REQUIRE(copy != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(copy), XTextDocument_blockCount(doc));
    XQ_EQ_I64(XTextDocument_characterCount(copy),
              XTextDocument_characterCount(doc));
    XTextDocument_setPlainText(copy, "other");
    text = xtd_plain(doc);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "abcd");
    XFree_System(text);
    XClassDelete((XClass*)copy);

    /* clear：清内容 + 复位 modified/revision（Qt QTextDocument::clear）。 */
    XTextDocument_setModified(doc, true);
    XTextDocument_clear(doc);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 1);
    XQ_CHECK(XTextDocument_isEmpty(doc));
    XQ_CHECK(!XTextDocument_isModified(doc));
    XQ_EQ_I64(XTextDocument_revision(doc), 0);

    XClassDelete((XClass*)doc);
}

XQ_TEST(XTextDocument, htmlAndParent)
{
    XTextDocument* doc = XTextDocument_create();
    XObject* parent;
    XTextDocument* child;
    char* html;
    char* plain;
    XQ_REQUIRE(doc != NULL);

    XTextDocument_setHtml(doc, "<b>Hi</b>");
    plain = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "Hi");
    XFree_System(plain);
    html = XTextDocument_toHtml(doc);
    XQ_REQUIRE(html != NULL);
    XQ_CHECK_MSG(strstr(html, "Hi") != NULL, "toHtml 含正文：%s", html);
    XFree_System(html);

    /* 父子：文档归父对象（Qt QTextDocument(parent)），父析构带走。 */
    parent = XObject_create();
    XQ_REQUIRE(parent != NULL);
    child = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(child != NULL);
    XObject_setParent((XObject*)child, parent);
    XQ_CHECK(XObject_parent((XObject*)child) == parent);
    XClassDelete((XClass*)parent);

    XClassDelete((XClass*)doc);
}

XQ_TEST(XTextDocument, scalarProperties)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);

    /* Qt 6.8 探针（qtextdocument.h:231-268）：documentMargin 4.0、
       indentWidth 40.0、textWidth -1.0、useDesignMetrics false、
       isLayoutEnabled true、defaultStyleSheet 空串、
       defaultCursorMoveStyle LogicalMoveStyle=0。 */
    XQ_CHECK(XTextDocument_documentMargin(doc) == 4.0);
    XQ_CHECK(XTextDocument_indentWidth(doc) == 40.0);
    XQ_CHECK(XTextDocument_textWidth(doc) == -1.0);
    XQ_CHECK(!XTextDocument_useDesignMetrics(doc));
    XQ_CHECK(XTextDocument_isLayoutEnabled(doc));
    XQ_EQ_STR(XTextDocument_defaultStyleSheet(doc), "");
    XQ_EQ_I64(XTextDocument_defaultCursorMoveStyle(doc), 0);

    /* 属性往返；documentMargin/indentWidth 原样保存负值（探针 -3 / -1）。 */
    XTextDocument_setDocumentMargin(doc, 10.5);
    XQ_CHECK(XTextDocument_documentMargin(doc) == 10.5);
    XTextDocument_setDocumentMargin(doc, -3.0);
    XQ_CHECK(XTextDocument_documentMargin(doc) == -3.0);
    XTextDocument_setIndentWidth(doc, 12.5);
    XQ_CHECK(XTextDocument_indentWidth(doc) == 12.5);
    XTextDocument_setIndentWidth(doc, -1.0);
    XQ_CHECK(XTextDocument_indentWidth(doc) == -1.0);
    XTextDocument_setTextWidth(doc, 320.0);
    XQ_CHECK(XTextDocument_textWidth(doc) == 320.0);
    XTextDocument_setUseDesignMetrics(doc, true);
    XQ_CHECK(XTextDocument_useDesignMetrics(doc));
    XTextDocument_setLayoutEnabled(doc, false);
    XQ_CHECK(!XTextDocument_isLayoutEnabled(doc));
    XTextDocument_setDefaultStyleSheet(doc, "b{color:red}");
    XQ_EQ_STR(XTextDocument_defaultStyleSheet(doc), "b{color:red}");
    XTextDocument_setDefaultCursorMoveStyle(doc, 1);
    XQ_EQ_I64(XTextDocument_defaultCursorMoveStyle(doc), 1);

    /* clone 复制文档属性（Qt clone 同口径）。 */
    {
        XTextDocument* copy = XTextDocument_clone(doc);
        XQ_REQUIRE(copy != NULL);
        XQ_CHECK(XTextDocument_documentMargin(copy) == -3.0);
        XQ_CHECK(XTextDocument_indentWidth(copy) == -1.0);
        XQ_CHECK(XTextDocument_textWidth(copy) == 320.0);
        XQ_CHECK(XTextDocument_useDesignMetrics(copy));
        XQ_CHECK(!XTextDocument_isLayoutEnabled(copy));
        XQ_EQ_STR(XTextDocument_defaultStyleSheet(copy), "b{color:red}");
        XQ_EQ_I64(XTextDocument_defaultCursorMoveStyle(copy), 1);
        XClassDelete((XClass*)copy);
    }

    XClassDelete((XClass*)doc);
}

/* ==================== contentsChange 精确区间（块数上限裁剪） ==================== */

/**
 * @brief 块数上限裁剪的 contentsChange 精确区间 + Qt 的发射顺序。
 * @details 参考实现与探针（QXTextDocument，本机 Qt 6.8 offscreen，
 *          文档挂 layout 才有 contentsChange——Qt 的 finishEdit 判
 *          `if (lout && docChangeFrom >= 0)`，qtextdocument_p.cpp:1180）：
 *          - `setPlainText("1\n2\n3")` 于空文档 ⇒
 *            contentsChange(0,1,0) [私有 clear] + contentsChange(0,0,6)
 *            [装入] + contentsChanged + blockCountChanged(3)；
 *          - `setMaximumBlockCount(2)`（3 块裁 1 块）⇒
 *            contentsChange(0,2,0) + contentsChanged + blockCountChanged(2)
 *            （ensureMaximumBlockCount 的 selectionEnd-selectionStart 口径，
 *            qtextdocument_p.cpp:1696-1700）；
 *          - 未超限的 `setMaximumBlockCount(5)` ⇒ **零信号**；
 *          - `appendBlock` ⇒ contentsChange(旧字符数-1, 0, 1)（探针 "ab"
 *            后 insertBlock ⇒ (2,0,1)）；块尾插入文本 ⇒ (块文本尾, 0, 长度)
 *            （探针 (3,0,2)）；
 *          - 2 块文档被 `setPlainText("q")` 整体替换 ⇒ (0,4,0)+(0,0,2) +
 *            contentsChanged + blockCountChanged(1)（Qt 同形）。
 *          修复前本库只有一条全量汇总 (0, 上次字符数, 本次字符数)，且
 *          blockCountChanged 先于 contentsChange（与 Qt 相反）。
 */
XQ_TEST(XTextDocument, maxBlockCountPreciseContentsChange)
{
    XTextDocument* doc = XTextDocument_create();
    char* text;
    XQ_REQUIRE(doc != NULL);
    xtd_connectContentSignals(doc);

    /* ① 空文档 setPlainText("1\n2\n3")：clear 一条 + 装入一条 + changed + BCC。 */
    XTextDocument_setPlainText(doc, "1\n2\n3");
    XQ_EQ_I64(s_eventN, 4);
    xtd_expectCC(0, 0, 1, 0, "setPlainText 的 clear 段");
    xtd_expectCC(1, 0, 0, 6, "setPlainText 的装入段");
    XQ_CHECK_MSG(s_eventN > 2 && s_events[2].kind == 1,
                 "第三条应是 contentsChanged（Qt finishEdit 顺序）");
    XQ_CHECK_MSG(s_eventN > 3 && s_events[3].kind == 2 && s_events[3].count == 3,
                 "第四条应是 blockCountChanged(3)（Qt finishEdit 顺序）");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    XQ_EQ_I64(XTextDocument_characterCount(doc), 6);

    /* ② 块数上限裁剪：精确区间 (0, 被裁字符数, 0) = (0,"1"+分隔符,0)。 */
    xtd_resetEvents();
    XTextDocument_setMaximumBlockCount(doc, 2);
    XQ_EQ_I64(s_eventN, 3);
    xtd_expectCC(0, 0, 2, 0, "块数上限裁剪");
    XQ_CHECK_MSG(s_events[1].kind == 1, "裁剪后应发 contentsChanged");
    XQ_CHECK_MSG(s_events[2].kind == 2 && s_events[2].count == 2,
                 "裁剪后 blockCountChanged 取裁剪后的块数 2");
    text = xtd_plain(doc);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "2\n3");
    XFree_System(text);

    /* ③ 未超限的设置上限：Qt 零信号（探针：max 未触发裁剪时无任何内容信号）。 */
    xtd_resetEvents();
    XTextDocument_setMaximumBlockCount(doc, 5);
    XQ_EQ_I64(s_eventN, 0);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);

    /* ④ 整体替换：清空段 + 装入段 + changed + blockCountChanged(1)。
     *    （④之后 max=5 仍生效，无裁剪介入） */
    xtd_resetEvents();
    XTextDocument_setPlainText(doc, "q");
    XQ_EQ_I64(s_eventN, 4);
    xtd_expectCC(0, 0, 4, 0, "替换前 2 块共 4 字符");
    xtd_expectCC(1, 0, 0, 2, "装入 \"q\"：1 文本 + 1 分隔符");
    XQ_CHECK_MSG(s_events[2].kind == 1, "替换后 contentsChanged");
    XQ_CHECK_MSG(s_events[3].kind == 2 && s_events[3].count == 1,
                 "替换后 blockCountChanged(1)（Qt 只沿替换前后发一次）");

    /* ⑤ appendBlock：在文档尾插入一个块分隔符 ⇒ (旧字符数-1, 0, 1)。 */
    xtd_resetEvents();
    XTextDocument_appendBlock(doc, NULL);   /* "q" 2 字符 ⇒ (1,0,1) */
    XQ_EQ_I64(s_eventN, 3);
    xtd_expectCC(0, 1, 0, 1, "appendBlock");
    XQ_CHECK_MSG(s_events[2].kind == 2 && s_events[2].count == 2,
                 "appendBlock 后 blockCountChanged(2)");

    /* ⑥ 块尾追加文本：块 1 的文本尾 = len("q")+1 = 2 ⇒ (2,0,2)。 */
    xtd_resetEvents();
    XTextDocument_appendText(doc, "cd", NULL);
    XQ_EQ_I64(s_eventN, 2);
    xtd_expectCC(0, 2, 0, 2, "appendText");
    XQ_CHECK_MSG(s_events[1].kind == 1, "appendText 后 contentsChanged");
    text = xtd_plain(doc);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "q\ncd");
    XFree_System(text);

    /* ⑦ 裁剪后再整体替换：裁剪挪动块数组不得留下重复/悬垂块指针
     *    （修复前这里会二次释放：clear 只释放活块、填充复用尾部残留槽位）。 */
    xtd_resetEvents();
    XTextDocument_setMaximumBlockCount(doc, 1);  /* 裁掉 "q" 块 */
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XTextDocument_setMaximumBlockCount(doc, 0);  /* 回到不限，供下一步整体替换 */
    XTextDocument_setPlainText(doc, "x\ny");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    text = xtd_plain(doc);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "x\ny");
    XFree_System(text);

    XClassDelete((XClass*)doc);
}

/* ==================== begin/end 迭代协议与资源提供者（P3-07/08/09） ==================== */

/** @brief 资源提供者探针：记录收到的 userData，按 URL 返回借用载荷。 */
static void* g_xtdProvUser;      /**< 最近一次回调收到的 userData。 */
static int g_xtdProvCalls;       /**< 回调次数。 */
static XImage g_xtdProvImage;    /**< 提供者返回的借用镜像。 */

static const void* xtd_resProvider(void* userData, int type, const char* url)
{
    g_xtdProvUser = userData;
    ++g_xtdProvCalls;
    if (!url) return NULL;
    if (type == (int)XTDResourceType_ImageResource &&
        XStrcmp(url, "probe://img") == 0)
        return &g_xtdProvImage;
    if (type == (int)XTDResourceType_UserResource) {
        if (XStrcmp(url, "probe://doc") == 0) return "doc-value";
        if (XStrcmp(url, "probe://fallback") == 0) return "fallback-value";
    }
    return NULL;
}

/** @brief 回 NULL 的空提供者：验证「文档级提供者存在时不再问默认提供者」
 *         ——xtd_resProvider 对 probe://fallback 恒回 fallback-value，
 *         不能充当"回 NULL 的文档级提供者"这一角色。 */
static const void* xtd_resProviderEmpty(void* userData, int type, const char* url)
{
    (void)type;
    (void)url;
    g_xtdProvUser = userData;
    ++g_xtdProvCalls;
    return NULL;
}

/** @brief begin()/end() 的块迭代协议与 resourceProvider 的回落链。 */
XQ_TEST(XTextDocument, beginEndAndResourceProvider)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocument* other = XTextDocument_create();
    const XTDBlock* first;
    const XTDBlock* last;
    void* user = NULL;
    int docTag = 0;
    int defTag = 0;
    XQ_REQUIRE(doc != NULL && other != NULL);

    /* ---- begin()：首块借用指针，与 firstBlock 同值 ---- */
    XTextDocument_setPlainText(doc, "a\nb");
    first = XTextDocument_begin(doc);
    last = XTextDocument_lastBlock(doc);
    XQ_CHECK_PTR(first);
    XQ_CHECK(first == XTextDocument_firstBlock(doc));
    XQ_CHECK(last != NULL && last != first);

    /* ---- end()：过尾哨兵恒 NULL（Qt 的 isValid()==false 块） ---- */
    XQ_CHECK(XTextDocument_end(doc) == NULL);
    XQ_CHECK(XTextDocument_end(NULL) == NULL);
    /* 遍历契约：begin/末块都不等于过尾哨兵。 */
    XQ_CHECK(first != XTextDocument_end(doc));
    XQ_CHECK(last != XTextDocument_end(doc));
    /* 空文档（1 空块）同样有 begin，且末块 == 首块。 */
    XTextDocument_setPlainText(doc, "");
    XQ_CHECK(XTextDocument_begin(doc) != NULL);
    XQ_CHECK(XTextDocument_begin(doc) == XTextDocument_lastBlock(doc));
    /* NULL 文档：begin 返回 NULL（无效块，等价 Qt 无效 QTextBlock）。 */
    XQ_CHECK(XTextDocument_begin(NULL) == NULL);

    /* ---- 资源表未命中且无提供者 ⇒ NULL ---- */
    XQ_CHECK(XTextDocument_resource(doc, XTDResourceType_UserResource,
                                    "probe://doc") == NULL);
    XQ_CHECK(XTextDocument_resourceProvider(doc, &user) == NULL);
    XQ_CHECK(user == NULL);

    /* ---- 文档级提供者 ---- */
    g_xtdProvCalls = 0;
    g_xtdProvUser = NULL;
    XTextDocument_setResourceProvider(doc, xtd_resProvider, &docTag);
    XQ_CHECK(XTextDocument_resourceProvider(doc, &user) == xtd_resProvider);
    XQ_CHECK(user == &docTag);
    {
        const void* v = XTextDocument_resource(
            doc, XTDResourceType_UserResource, "probe://doc");
        XQ_EQ_STR((const char*)v, "doc-value");
        XQ_CHECK(g_xtdProvUser == &docTag); /* userData 原样回传 */
    }
    /* 表内命中优先于提供者（Qt：resource 先查资源表）。 */
    XTextDocument_addResource(doc, XTDResourceType_UserResource,
                              "probe://doc", "table-value");
    XQ_EQ_STR((const char*)XTextDocument_resource(
                  doc, XTDResourceType_UserResource, "probe://doc"),
              "table-value");

    /* 提供者返回的镜像/文本**不入表**：同一 URL 第二次查询仍走提供者，
       且返回值就是提供者给的那个借用指针。 */
    {
        const void* img;
        XImage_init_ex(&g_xtdProvImage, 4, 3, XImageFormat_ARGB32);
        img = XTextDocument_resource(doc, XTDResourceType_ImageResource,
                                     "probe://img");
        XQ_CHECK(img == (const void*)&g_xtdProvImage);
        img = XTextDocument_resource(doc, XTDResourceType_ImageResource,
                                     "probe://img");
        XQ_CHECK(img == (const void*)&g_xtdProvImage);
        XClassDeinit((XClass*)&g_xtdProvImage);
    }

    /* ---- 进程级默认提供者：未设文档级提供者的文档才用它 ---- */
    XTextDocument_setDefaultResourceProvider(xtd_resProvider, &defTag);
    XQ_CHECK(XTextDocument_defaultResourceProvider(&user) == xtd_resProvider);
    XQ_CHECK(user == &defTag);
    XQ_EQ_STR((const char*)XTextDocument_resource(
                  other, XTDResourceType_UserResource, "probe://fallback"),
              "fallback-value");
    XQ_CHECK(g_xtdProvUser == &defTag);
    /* 文档级提供者存在时**不再问**默认提供者（Qt 同，qtextdocument.cpp
       resource(): `if (d->resourceProvider) … else if (default…)`）：
       doc 换上对该 URL 回 NULL 的空提供者 ⇒ 结果 NULL，而不是落到
       默认提供者的值。 */
    XTextDocument_setResourceProvider(doc, xtd_resProviderEmpty, &docTag);
    XQ_CHECK(XTextDocument_resource(doc, XTDResourceType_UserResource,
                                    "probe://fallback") == NULL);
    XQ_CHECK(g_xtdProvUser == &docTag);

    /* ---- 清除：文档级与进程级各自恢复「无提供者」 ---- */
    XTextDocument_setResourceProvider(doc, NULL, NULL);
    XQ_CHECK(XTextDocument_resourceProvider(doc, &user) == NULL);
    XQ_CHECK(user == NULL);
    XTextDocument_setDefaultResourceProvider(NULL, NULL);
    XQ_CHECK(XTextDocument_defaultResourceProvider(&user) == NULL);
    XQ_CHECK(XTextDocument_resource(other, XTDResourceType_UserResource,
                                    "probe://fallback") == NULL);

    XClassDelete((XClass*)other);
    XClassDelete((XClass*)doc);
}

#endif /* XTEXTDOCUMENT_ON */
