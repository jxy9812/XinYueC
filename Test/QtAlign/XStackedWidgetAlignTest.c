/**
 * @file       XStackedWidgetAlignTest.c
 * @brief      XStackedWidget × Qt 6.8 QStackedWidget 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/
 *               qstackedwidget.h（addWidget/insertWidget/removeWidget、
 *               currentIndex/currentWidget/widget/indexOf/count、
 *               setCurrentIndex/setCurrentWidget 槽、
 *               currentChanged(int)/widgetRemoved(int)）；
 *             - Qt 6.8.3 源码 qstackedlayout.cpp：
 *               insertWidget（index<0 或 > count ⇒ 追加；index <= 当前页
 *               ⇒ 当前索引 ++ 但控件不变）、
 *               setCurrentIndex（`if (!next || next == prev) return;`
 *               ⇒ 越界索引被拒且**不发** currentChanged）、
 *               takeAt（移出当前页 ⇒ 先 setCurrentIndex(新索引) 发
 *               currentChanged，列表空则发 currentChanged(-1)，随后发
 *               widgetRemoved(原索引)）；
 *             - 本机 Qt 6.8.0 + offscreen 实测：setCurrentIndex(99)/(-2)
 *               当前页不变且 currentChanged 计数 0；移除当前页（唯一
 *               剩余页）后 currentChanged(0) 先于 widgetRemoved(0)。
 * @note       所有权口径与 Qt 有已登记差异（Qt 保留父对象，本框架摘父链
 *             归还调用方，既有回归用例锁定），见 BatchResult gaps。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && \
    XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON

#include "XStackedWidget.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVarList.h"
#include "XClass.h"
#include "XVector.h" /* 断言 Z 序：父 children 数组顺序。 */

/** @brief 信号序列记录：C=currentChanged、R=widgetRemoved。 */
static char g_xswOrder[16];
static int g_xswOrderLen;
static int g_xswCurrentCount;
static int g_xswCurrentLast;
static int g_xswRemovedCount;
static int g_xswRemovedLast;

static void xsw_reset(void)
{
    g_xswOrderLen = 0;
    g_xswCurrentCount = 0;
    g_xswCurrentLast = -99;
    g_xswRemovedCount = 0;
    g_xswRemovedLast = -99;
}

static void xsw_currentSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, index);
    ++g_xswCurrentCount;
    g_xswCurrentLast = index;
    if (g_xswOrderLen < (int)sizeof(g_xswOrder))
        g_xswOrder[g_xswOrderLen++] = 'C';
}

static void xsw_removedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, index);
    ++g_xswRemovedCount;
    g_xswRemovedLast = index;
    if (g_xswOrderLen < (int)sizeof(g_xswOrder))
        g_xswOrder[g_xswOrderLen++] = 'R';
}

static void xsw_hook(XStackedWidget* sw)
{
    XObject_connect((XObject*)sw,
                      (size_t)XStackedWidget_currentChanged_signal(sw, 0),
                      (XObject*)sw, xsw_currentSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)sw,
                      (size_t)XStackedWidget_widgetRemoved_signal(sw, 0),
                      (XObject*)sw, xsw_removedSlot,
                      XConnectionType_Direct);
}

/* ==================== ① 缺省值 + 页增删与索引钳制 ==================== */

XQ_TEST(XStackedWidget, defaultsAndIndexClamp)
{
    XStackedWidget* sw = XStackedWidget_create(NULL, 0);
    XWidget* w1;
    XWidget* w2;
    XWidget* w3;
    XWidget* w4;

    XQ_REQUIRE(sw != NULL);
    w1 = XWidget_create(NULL, 0);
    w2 = XWidget_create(NULL, 0);
    w3 = XWidget_create(NULL, 0);
    w4 = XWidget_create(NULL, 0);
    XQ_REQUIRE(w1 && w2 && w3 && w4);
    xsw_reset();
    xsw_hook(sw);

    /* 空容器：count=0、currentIndex=-1、currentWidget=NULL（Qt 同）。 */
    XQ_EQ_I64(XStackedWidget_count(sw), 0);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), -1);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == NULL);
    XQ_CHECK(XStackedWidget_widget(sw, 0) == NULL);
    XQ_EQ_I64(XStackedWidget_indexOf(sw, w1), -1);

    /* 首页加入 ⇒ 索引 0、成为当前页、发 currentChanged(0)（Qt 同）。 */
    XQ_EQ_I64(XStackedWidget_addWidget(sw, w1), 0);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 0);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == w1);
    XQ_EQ_I64(g_xswCurrentCount, 1);
    XQ_EQ_I64(g_xswCurrentLast, 0);

    /* 追加不改当前页、不补发信号。 */
    XQ_EQ_I64(XStackedWidget_addWidget(sw, w2), 1);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 0);
    XQ_EQ_I64(g_xswCurrentCount, 1);

    /* 前插：当前索引顺移、当前控件不变（qstackedlayout.cpp d->index++）。 */
    XQ_EQ_I64(XStackedWidget_insertWidget(sw, 0, w3), 0);
    XQ_EQ_I64(XStackedWidget_indexOf(sw, w3), 0);
    XQ_EQ_I64(XStackedWidget_indexOf(sw, w1), 1);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 1);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == w1);
    XQ_EQ_I64(g_xswCurrentCount, 1);

    /* 越界索引一律追加（index<0 或 > count），返回实际索引
     * （qstackedlayout.cpp:192-194 `index = qMin(index, size); if (index<0)
     * index = size;`）。 */
    XQ_EQ_I64(XStackedWidget_insertWidget(sw, 99, w4), 3);
    XQ_EQ_I64(XStackedWidget_insertWidget(sw, -3, XWidget_create(NULL, 0)),
              4);
    XQ_EQ_I64(XStackedWidget_addWidget(sw, NULL), -1);
    XQ_EQ_I64(XStackedWidget_insertWidget(sw, 0, NULL), -1);

    /* setCurrentIndex 越界：布局层拒绝且**不发信号**（Qt 同；修复前会
     * 发出不存在的页索引）。 */
    g_xswCurrentCount = 0;
    XStackedWidget_setCurrentIndex(sw, 99);
    XStackedWidget_setCurrentIndex(sw, -1);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 1);
    XQ_EQ_I64(g_xswCurrentCount, 0);

    /* 合法切换发一次；同值不重发。 */
    XStackedWidget_setCurrentIndex(sw, 2);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 2);
    XQ_EQ_I64(g_xswCurrentCount, 1);
    XQ_EQ_I64(g_xswCurrentLast, 2);
    XStackedWidget_setCurrentIndex(sw, 2);
    XQ_EQ_I64(g_xswCurrentCount, 1);

    /* setCurrentWidget：按控件迁移；非本容器页忽略（Qt 同）。 */
    XStackedWidget_setCurrentWidget(sw, w3);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 0);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == w3);

    XClassDelete((XClass*)sw); /* 页随容器级联释放。 */
    return;
}

/* ==================== ② removeWidget：当前页迁移与信号顺序 ============== */

XQ_TEST(XStackedWidget, removeCurrentPageSignals)
{
    XStackedWidget* sw = XStackedWidget_create(NULL, 0);
    XWidget* w1;
    XWidget* w2;
    XWidget* w3;

    XQ_REQUIRE(sw != NULL);
    w1 = XWidget_create(NULL, 0);
    w2 = XWidget_create(NULL, 0);
    w3 = XWidget_create(NULL, 0);
    XQ_REQUIRE(w1 && w2 && w3);
    xsw_reset();
    xsw_hook(sw);
    XStackedWidget_addWidget(sw, w1);
    XStackedWidget_addWidget(sw, w2);
    XStackedWidget_addWidget(sw, w3);
    XStackedWidget_setCurrentIndex(sw, 1);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == w2);

    /* 移除当前页：Qt takeAt 先 setCurrentIndex(新索引) 发
     * currentChanged，再发 widgetRemoved(原索引)
     * （qstackedlayout.cpp:231-252：newIndex =
     * (index == 新列表长度) ? index-1 : index）。
     * 本例移除 index=1（3 页移后剩 2 页，1 != 2）⇒ 新索引仍是 1，
     * 原 index=2 的 w3 左移补位成为当前页。本机 Qt 6.8 探针实测：
     * currentChanged(1) → widgetRemoved(1)，currentIndex=1、
     * currentWidget=w3。此前按「回退到上一页」写成 0/w1，与 Qt 不符。 */
    xsw_reset();
    XStackedWidget_removeWidget(sw, w2);
    XQ_EQ_I64(XStackedWidget_count(sw), 2);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 1);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == w3);
    XQ_EQ_I64(g_xswCurrentCount, 1);
    XQ_EQ_I64(g_xswCurrentLast, 1);
    XQ_EQ_I64(g_xswRemovedCount, 1);
    XQ_EQ_I64(g_xswRemovedLast, 1);
    XQ_CHECK(g_xswOrderLen == 2 && g_xswOrder[0] == 'C' && g_xswOrder[1] == 'R');
    /* Qt 口径（gap #177）：removeWidget 只摘布局条目 + hide，**父对象仍是
     * 堆叠容器**（qstackedwidget.cpp removeWidget 文档「Parent object and
     * parent widget of widget will remain the QStackedWidget」；
     * QStackedLayout::takeAt 不 reparent）。 */
    XQ_CHECK(XWidget_parentWidget(w2) == (XWidget*)sw);
    XQ_CHECK(!XWidget_isVisible(w2));
    XQ_CHECK(XWidget_isHidden(w2)); /* takeAt 末尾 widget->hide()。 */
    XClassDelete((XClass*)w2);

    /* 移除非当前页：不迁移当前页、不发 currentChanged，只发
     * widgetRemoved（原索引）。 */
    XStackedWidget_setCurrentIndex(sw, 0);
    xsw_reset();
    XStackedWidget_removeWidget(sw, w3);
    XQ_EQ_I64(XStackedWidget_count(sw), 1);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 0);
    XQ_EQ_I64(g_xswCurrentCount, 0);
    XQ_EQ_I64(g_xswRemovedCount, 1);
    XQ_EQ_I64(g_xswRemovedLast, 1);
    XClassDelete((XClass*)w3);

    /* 移空列表：发 currentChanged(-1)（Qt takeAt `emit currentChanged(-1)`），
     * 随后仍发 widgetRemoved(0)。 */
    xsw_reset();
    XStackedWidget_removeWidget(sw, w1);
    XQ_EQ_I64(XStackedWidget_count(sw), 0);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), -1);
    XQ_EQ_I64(g_xswCurrentCount, 1);
    XQ_EQ_I64(g_xswCurrentLast, -1);
    XQ_EQ_I64(g_xswRemovedCount, 1);
    XQ_EQ_I64(g_xswRemovedLast, 0);
    XQ_CHECK(g_xswOrderLen == 2 && g_xswOrder[0] == 'C' && g_xswOrder[1] == 'R');
    XClassDelete((XClass*)w1);

    /* 重复移除同一控件为无操作（不重发信号）。 */
    xsw_reset();
    XStackedWidget_removeWidget(sw, w1);
    XQ_EQ_I64(g_xswRemovedCount, 0);

    XClassDelete((XClass*)sw);
}

/* ==================== ③ 页挂父链 + 插入降 Z 序（gap #177/#178） ====== */

XQ_TEST(XStackedWidget, pagesReparentAndLowered)
{
    XStackedWidget* sw = XStackedWidget_create(NULL, 0);
    XWidget* w1;
    XWidget* w2;
    XWidget* w3;
    const XVector* children;
    int idx1;
    int idx2;
    int idx3;

    XQ_REQUIRE(sw != NULL);
    w1 = XWidget_create(NULL, 0);
    w2 = XWidget_create(NULL, 0);
    w3 = XWidget_create(NULL, 0);
    XQ_REQUIRE(w1 && w2 && w3);

    /* 对标 QLayout::addChildWidget：insertWidget 首行把页 reparent 到容器
     * （qstackedlayout.cpp:190 `addChildWidget(widget)`）。 */
    XQ_EQ_I64(XStackedWidget_addWidget(sw, w1), 0);
    XQ_CHECK(XWidget_parentWidget(w1) == (XWidget*)sw);
    XQ_EQ_I64(XStackedWidget_addWidget(sw, w2), 1);
    XQ_CHECK(XWidget_parentWidget(w2) == (XWidget*)sw);
    XQ_EQ_I64(XStackedWidget_insertWidget(sw, 1, w3), 1);
    XQ_CHECK(XWidget_parentWidget(w3) == (XWidget*)sw);

    /* 对标 qstackedlayout.cpp:207：非首件插入后 `widget->lower()` —— 新页
     * 压到兄弟栈底（X 的 Z 序 = 父 children 数组顺序，越前越底）。 */
    children = XObject_children((XObject*)sw);
    XQ_REQUIRE(children != NULL);
    idx1 = XVector_indexOf((XVector*)children, &w1, 0);
    idx2 = XVector_indexOf((XVector*)children, &w2, 0);
    idx3 = XVector_indexOf((XVector*)children, &w3, 0);
    XQ_CHECK(idx1 >= 0 && idx2 >= 0 && idx3 >= 0);
    /* 初次 addWidget(w2)：old>=0 ⇒ w2 lowered（在 w1 之前）。 */
    XQ_CHECK_MSG(idx2 < idx1, "后加入的页应被 lower()（idx2=%d idx1=%d）",
                 idx2, idx1);
    /* insertWidget(1, w3)：同样 lowered ⇒ 现在在 w1 之前。 */
    XQ_CHECK_MSG(idx3 < idx1, "插入的页应被 lower()（idx3=%d idx1=%d）",
                 idx3, idx1);

    /* 移除页后父对象仍是容器（见 removeCurrentPageSignals 用例），此处
     * 顺带验证其仍可见性为隐藏。 */
    XStackedWidget_removeWidget(sw, w3);
    XQ_CHECK(XWidget_parentWidget(w3) == (XWidget*)sw);
    XQ_CHECK(XWidget_isHidden(w3));

    XClassDelete((XClass*)sw); /* w1/w2/w3 随容器级联释放。 */
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON */
