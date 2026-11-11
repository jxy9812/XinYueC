/**
 * @file       XCompleterAlignTest.c
 * @brief      XCompleter 与 Qt 6.8 QCompleter 的对齐契约测试。
 * @details    覆盖：
 *             - 构造默认值（Qt 6.8 探针：completionMode PopupCompletion、
 *               modelSorting UnsortedModel、completionColumn 0、
 *               maxVisibleItems 7、wrapAround true、caseSensitivity
 *               CaseSensitive、completionRole EditRole=2、filterMode
 *               MatchStartsWith=2、popup() 懒建默认弹层非空）；
 *             - **带模型构造即完成**（本轮对齐：Qt `QCompleter(model)` 经
 *               setModel 立即把代理当前行置 0，空前缀命中全部行——
 *               本机 Qt 6.8 探针：构造后 completionCount=模型行数、
 *               currentRow=0、currentCompletion 为第 0 行文本）；
 *             - 前缀过滤（StartsWith/Contains/EndsWith）+ 大小写敏感性；
 *             - currentCompletion/currentRow/currentIndex/路径族；
 *             - setCurrentRow 的越界返回 false（QCompletionModel::
 *               setCurrentRow 不环绕，qcompleter.cpp:256-269）；
 *             - complete(const QRect&) 的弹层定位与 completionRole 参与读取；
 *             - highlighted 信号在命中首项变化时发射；
 *             - setWidget/setPopup 的借用挂接与父子析构。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XCompleter.h"
#include "XLineEdit.h"
#include "XAbstractItemModel.h"
#include "XObject.h"
#include "XClass.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XVarList.h"
#include "XWidget.h"
#include "XMemory.h"

#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 信号计数槽 ==================== */

static int s_highlighted;   /**< highlighted/textHighlighted 次数。 */
static int s_lastRow;       /**< highlightedRow 载荷。 */

static void xc_slotHighlighted(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_highlighted;
}

static void xc_slotHighlightedRow(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_highlighted;
    if (args) s_lastRow = XVarList_arg(args, int);
}

/** @brief 建 3 行 2 列模型（列 0 词条、列 1 类别）。 */
static XAbstractItemModel* xc_makeModel(void)
{
    static const char* const kWords[3] = {"alpha", "beta", "gamma"};
    static const char* const kKinds[3] = {"one", "two", "three"};
    XAbstractItemModel* m = XAbstractItemModel_create();
    int i;
    if (!m) return NULL;
    XAbstractItemModel_setDimension(m, 3, 2);
    for (i = 0; i < 3; ++i) {
        XAbstractItemModel_setData_2(m, i, 0, kWords[i]);
        XAbstractItemModel_setData_2(m, i, 1, kKinds[i]);
    }
    return m;
}

/** @brief XString → UTF-8 借用视图；空串归一为 ""（XString_toUtf8 对空串
 *         返回 NULL，断言比较语义而不是底层指针约定）。 */
static const char* xc_u8(const XString* s)
{
    const char* u8;
    if (!s) return "";
    u8 = XString_toUtf8(s);
    return u8 ? u8 : "";
}

/** @brief 取 currentCompletion 文本的借用视图（调用方 XFree_System）。 */
static char* xc_current(const XCompleter* c)
{
    XString* s = XCompleter_currentCompletion(c);
    const char* u8;
    char* out;
    size_t n;
    if (!s) return NULL;
    u8 = XString_toUtf8(s);
    if (!u8) u8 = "";
    n = XStrlen(u8);
    out = (char*)XMalloc_System(n + 1);
    if (out) {
        if (n) XMemcpy(out, u8, n);
        out[n] = '\0';
    }
    XClassDelete((XClass*)s);
    return out;
}

XQ_TEST(XCompleter, defaultsAndConstructionWithModel)
{
    XAbstractItemModel* model = xc_makeModel();
    XCompleter* c;
    char* text;
    XQ_REQUIRE(model != NULL);

    c = XCompleter_create(NULL);
    XQ_REQUIRE(c != NULL);
    /* Qt 6.8 探针默认值。 */
    XQ_CHECK(XCompleter_model(c) == NULL);
    XQ_CHECK(XCompleter_widget(c) == NULL);
    /* Qt 的 popup() 首次查询即懒建默认 QListView（qcompleter.cpp:
       1264-1279；本机探针 `QCompleter c; c.popup()` 非空）。 */
    XQ_CHECK(XCompleter_popup(c) != NULL);
    XQ_EQ_I64(XCompleter_completionMode(c),
              XCompleterCompletionMode_PopupCompletion);
    XQ_EQ_I64(XCompleter_modelSorting(c),
              XCompleterModelSorting_UnsortedModel);
    XQ_EQ_I64(XCompleter_completionColumn(c), 0);
    XQ_EQ_I64(XCompleter_maxVisibleItems(c), 7);
    XQ_CHECK(XCompleter_wrapAround(c));
    XQ_EQ_I64(XCompleter_caseSensitivity(c), XChar_CaseSensitive);
    /* Qt::MatchStartsWith == 2（qcompleter.h:71-73；本机探针 filterMode==2）。 */
    XQ_EQ_I64(XCompleter_filterMode(c), XCompleterFilterMode_StartsWith);
    XQ_EQ_I64((int)XCompleter_filterMode(c), 2);
    /* Qt::EditRole == 2（qcompleter.cpp:794；本机探针 role==2）。 */
    XQ_EQ_I64(XCompleter_completionRole(c), 2);
    XQ_EQ_I64(XCompleter_completionCount(c), 0);
    XQ_EQ_I64(XCompleter_currentRow(c), -1);
    XQ_CHECK(XCompleter_currentCompletion(c) == NULL);
    XQ_CHECK(XCompleter_completionPrefix(c) == NULL);
    XClassDelete((XClass*)c);

    /* 带模型构造：Qt 经 setModel 立即完成（空前缀命中全部）。 */
    c = XCompleter_create_2(model, NULL);
    XQ_REQUIRE(c != NULL);
    XQ_CHECK(XCompleter_model(c) == model);
    XQ_EQ_I64(XCompleter_completionCount(c), 3);
    XQ_EQ_I64(XCompleter_currentRow(c), 0);
    XQ_EQ_I64(XCompleter_currentIndex(c), 0);
    text = xc_current(c);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "alpha");
    XFree_System(text);
    XClassDelete((XClass*)c);

    XClassDelete((XClass*)model);
}

XQ_TEST(XCompleter, prefixFiltering)
{
    XAbstractItemModel* model = xc_makeModel();
    XCompleter* c;
    char* text;
    XQ_REQUIRE(model != NULL);
    c = XCompleter_create_2(model, NULL);
    XQ_REQUIRE(c != NULL);

    s_highlighted = 0;
    s_lastRow = -1;
    XObject_connect_1((XObject*)c,
                      (size_t)XCompleter_highlighted_signal(NULL, NULL),
                      (XObject*)c, xc_slotHighlighted,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)c,
                      (size_t)XCompleter_highlightedRow_signal(NULL, 0),
                      (XObject*)c, xc_slotHighlightedRow,
                      XConnectionType_Direct);

    XCompleter_setCompletionPrefix_2(c, "b");
    XQ_EQ_I64(XCompleter_completionCount(c), 1);
    XQ_EQ_I64(XCompleter_currentRow(c), 0);
    XQ_EQ_I64(XCompleter_currentIndex(c), 1); /* 源模型第 1 行。 */
    text = xc_current(c);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "beta");
    XFree_System(text);
    /* 命中首项由 alpha 变 beta ⇒ highlighted 系列发射。 */
    XQ_CHECK(s_highlighted > 0);

    /* 无命中：完成列表空、当前行 -1、当前补全为空。 */
    XCompleter_setCompletionPrefix_2(c, "zz");
    XQ_EQ_I64(XCompleter_completionCount(c), 0);
    XQ_EQ_I64(XCompleter_currentRow(c), -1);
    XQ_CHECK(XCompleter_currentCompletion(c) == NULL);

    /* 过滤模式：StartsWith 不命中、Contains/EndsWith 命中。 */
    XCompleter_setCompletionPrefix_2(c, "ta");
    XQ_EQ_I64(XCompleter_completionCount(c), 0);
    XCompleter_setFilterMode(c, XCompleterFilterMode_Contains);
    XQ_EQ_I64(XCompleter_completionCount(c), 1);
    XCompleter_setFilterMode(c, XCompleterFilterMode_EndsWith);
    XQ_EQ_I64(XCompleter_completionCount(c), 1);
    XCompleter_setFilterMode(c, XCompleterFilterMode_StartsWith);
    XQ_EQ_I64(XCompleter_completionCount(c), 0);

    /* 大小写：默认敏感（未命中），切不敏感后命中。 */
    XCompleter_setCompletionPrefix_2(c, "Al");
    XQ_EQ_I64(XCompleter_completionCount(c), 0);
    XCompleter_setCaseSensitivity(c, XChar_CaseInsensitive);
    XQ_EQ_I64(XCompleter_completionCount(c), 1);

    /* 补全列：第 1 列类别名（one/two/three）同样参与前缀过滤。 */
    XCompleter_setCompletionColumn(c, 1);
    XCompleter_setCompletionPrefix_2(c, "tw");
    XQ_EQ_I64(XCompleter_completionCount(c), 1);
    text = xc_current(c);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "two");
    XFree_System(text);

    XCompleter_setCompletionPrefix_2(c, "t");
    {
        XString* prefix = XCompleter_completionPrefix(c);
        XQ_REQUIRE(prefix != NULL);
        XQ_EQ_STR(xc_u8(prefix), "t");
        XClassDelete((XClass*)prefix);
    }

    XClassDelete((XClass*)c);
    XClassDelete((XClass*)model);
}

XQ_TEST(XCompleter, currentRowAndPath)
{
    XAbstractItemModel* model = xc_makeModel();
    XCompleter* c;
    XString* dir;
    XString* file;
    XQ_REQUIRE(model != NULL);
    c = XCompleter_create_2(model, NULL);
    XQ_REQUIRE(c != NULL);

    /* setCurrentRow：越界一律 false（QCompletionModel 不环绕）。 */
    XQ_CHECK(XCompleter_setCurrentRow(c, 0));
    XQ_EQ_I64(XCompleter_currentRow(c), 0);
    XQ_CHECK(XCompleter_setCurrentRow(c, 2));
    XQ_EQ_I64(XCompleter_currentRow(c), 2);
    XQ_CHECK(!XCompleter_setCurrentRow(c, 3));
    XQ_CHECK(!XCompleter_setCurrentRow(c, -1));
    XQ_EQ_I64(XCompleter_currentRow(c), 2);

    /* pathFromIndex：取该行补全列文本。 */
    {
        XString* p = XCompleter_pathFromIndex(c, 1);
        XQ_REQUIRE(p != NULL);
        XQ_EQ_STR(xc_u8(p), "beta");
        XClassDelete((XClass*)p);
    }

    /* splitPath：目录/文件名拆分（Qt 子集：按最后一个 '/'）。 */
    XCompleter_splitPath_2(c, "dir/file.txt", &dir, &file);
    XQ_REQUIRE(dir != NULL && file != NULL);
    XQ_EQ_STR(xc_u8(dir), "dir");
    XQ_EQ_STR(xc_u8(file), "file.txt");
    XClassDelete((XClass*)dir);
    XClassDelete((XClass*)file);
    XCompleter_splitPath_2(c, "plain.txt", &dir, &file);
    XQ_REQUIRE(dir != NULL && file != NULL);
    XQ_EQ_STR(xc_u8(dir), "");
    XQ_EQ_STR(xc_u8(file), "plain.txt");
    XClassDelete((XClass*)dir);
    XClassDelete((XClass*)file);

    /* 借用挂接：widget 只存指针；popup 外接时替换内部默认弹层，
       setPopup(NULL) 后 popup() 按 Qt 重新懒建默认弹层。 */
    XCompleter_setWidget(c, (XWidget*)c);
    XQ_CHECK(XCompleter_widget(c) == (XWidget*)c);
    XCompleter_setWidget(c, NULL);
    XQ_CHECK(XCompleter_widget(c) == NULL);
    {
        XWidget* fake = XWidget_create(NULL, 0);
        XQ_REQUIRE(fake != NULL);
        XCompleter_setPopup(c, fake);
        XQ_CHECK(XCompleter_popup(c) == fake);
        XCompleter_setPopup(c, NULL);
        XQ_CHECK(XCompleter_popup(c) != NULL);
        XQ_CHECK(XCompleter_popup(c) != fake);
        XClassDelete((XClass*)fake);
    }

    XClassDelete((XClass*)c);
    XClassDelete((XClass*)model);
}

XQ_TEST(XCompleter, completeWithRectAndRole)
{
    XAbstractItemModel* model = xc_makeModel();
    XCompleter* c;
    XLineEdit* editor;
    XWidget* popup;
    XRect rect;
    XQ_REQUIRE(model != NULL);
    c = XCompleter_create_2(model, NULL);
    editor = XLineEdit_create(NULL, 0);
    XQ_REQUIRE(c != NULL && editor != NULL);

    XCompleter_setWidget(c, (XWidget*)editor);
    /* 弹层可见性按「有效可见」计算（祖先链含顶层），顶层未 show 时恒假
       （同 xgui_demo_apitest_input 的补全用例注释）。 */
    XWidget_show((XWidget*)editor);
    /* complete(const QRect&)：rect 有效时用其宽并把弹层锚到 bottomLeft
       （qcompleter.cpp:886-905）；本库坐标为编辑框局部。编辑框是顶层控件
       时编辑器原点偏移 0 ⇒ 弹层 x=rect.x、y=rect.y+rect.height。 */
    rect.x = 5;
    rect.y = 3;
    rect.width = 200;
    rect.height = 20;
    XCompleter_complete_2(c, &rect);
    popup = XCompleter_popup(c);
    XQ_REQUIRE(popup != NULL);
    XQ_EQ_I64(XWidget_x(popup), 5);
    XQ_EQ_I64(XWidget_y(popup), 23);
    XQ_EQ_I64(XWidget_width(popup), 200);
    XQ_CHECK(XWidget_isVisible(popup));

    /* 无矩形：退回编辑框几何（Qt 的无效 QRect 分支）。 */
    XWidget_setGeometry((XWidget*)editor, 10, 20, 120, 30);
    XCompleter_complete(c);
    /* 弹层位置 = 编辑器在顶层中的偏移 + 编辑器高度。 */
    XQ_EQ_I64(XWidget_x(popup), 0);
    XQ_EQ_I64(XWidget_y(popup), 30);
    XQ_EQ_I64(XWidget_width(popup), 160);

    XCompleter_hidePopup(c);
    XQ_CHECK(!XWidget_isVisible(popup));

    /* completionRole 参与读取：非 Display/Edit 角色 ⇒ 无候选
       （模型无该角色数据，等价 Qt 取到无效 QVariant）。 */
    XCompleter_setCompletionRole(c, 5);
    XQ_EQ_I64(XCompleter_completionCount(c), 0);
    XCompleter_setCompletionRole(c, 0);
    XQ_EQ_I64(XCompleter_completionCount(c), 3);

    XClassDelete((XClass*)editor);
    XClassDelete((XClass*)c);
    XClassDelete((XClass*)model);
}

XQ_TEST(XCompleter, parentChild)
{
    XObject* parent = XObject_create();
    XCompleter* c;
    XQ_REQUIRE(parent != NULL);
    c = XCompleter_create(parent);
    XQ_REQUIRE(c != NULL);
    XQ_CHECK(XObject_parent((XObject*)c) == parent);
    XQ_EQ_I64(XCompleter_completionCount(c), 0);
    XClassDelete((XClass*)parent);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
