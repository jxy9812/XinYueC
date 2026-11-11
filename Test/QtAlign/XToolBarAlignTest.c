/**
 * @file       XToolBarAlignTest.c
 * @brief      XToolBar × Qt 6.8 QToolBar 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtoolbar.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/widgets/qtoolbar.cpp
 *             （字段缺省见 qtoolbar_p.h:37-39）。本机 Qt 6.8 探针
 *             （QT_QPA_PLATFORM=offscreen）实测：
 *             - `addWidget(&w)` 返回非空 QAction*，`insertWidget(a,&w)`
 *               同样返回 QAction*（再次登记同一控件得到**另一个**动作，
 *               动作表 +1）；
 *             - `widgetForAction(动作)` 返回登记的控件；
 *             - 默认 movable=true、floatable=true、orientation=Horizontal、
 *               allowedAreas=AllToolBarAreas、isFloating=isWindow()
 *               （无父工具栏为 true）、toolButtonStyle=ToolButtonIconOnly、
 *               iconSize 为宽高可不同的 QSize；
 *             - `isAreaAllowed` 是 `(allowedAreas() & area) == area`，
 *               多位掩码要求全部位命中（qtoolbar.h 内联实现）；
 *             - `clear()` 只 removeAction，不删除动作对象（qtoolbar.cpp:
 *               743-748；动作仍归 QObject 父/调用方）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTOOLBAR_ON && XACTION_ON && XTOOLBUTTON_ON

#include "XToolBar.h"
#include "XAction.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"
#include "XGeometry.h"
#include "XVarList.h"
#include "XClass.h"

/** @brief movableChanged 到达计数。 */
static int g_xtbMovableChanged;

static void xtb_movableSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xtbMovableChanged;
}

/* ==================== ① 默认值 + 停靠区判定 ==================== */

XQ_TEST(XToolBar, defaultsAndAreaRules)
{
    XToolBar* bar = XToolBar_create(NULL, 0);

    XQ_REQUIRE(bar != NULL);

    /* qtoolbar_p.h:37-39 字段缺省。 */
    XQ_CHECK(XToolBar_isMovable(bar));
    XQ_CHECK(XToolBar_isFloatable(bar));
    XQ_EQ_I64(XToolBar_orientation(bar), 1); /* Qt::Horizontal */
    XQ_EQ_I64(XToolBar_allowedAreas(bar),
              (int)XToolBarArea_Left | (int)XToolBarArea_Right |
              (int)XToolBarArea_Top | (int)XToolBarArea_Bottom);
    /* isFloating == isWindow()（qtoolbar.cpp:599-602）：无父构造的工具栏
     * 是顶层窗口 ⇒ 浮动。挂在父控件下的工具栏不浮动。 */
    XQ_CHECK(XWidget_isWindow((XWidget*)bar));
    XQ_CHECK(XToolBar_isFloating(bar));
    {
        XWidget* host = XWidget_create(NULL, 0);
        XToolBar* childBar;
        XQ_REQUIRE(host != NULL);
        childBar = XToolBar_create(host, 0);
        XQ_REQUIRE(childBar != NULL);
        XQ_CHECK(!XWidget_isWindow((XWidget*)childBar));
        XQ_CHECK(!XToolBar_isFloating(childBar));
        XQ_CHECK(!XToolBar_isFloating(NULL));
        XClassDelete((XClass*)childBar);
        XClassDelete((XClass*)host);
    }
    /* 缺省按钮风格 IconOnly（qtoolbar_p.h:39；探针 toolButtonStyle()==0）。 */
    XQ_EQ_I64(XToolBar_toolButtonStyle(bar),
              (int)XToolButtonStyle_IconOnly);
    {
        int w = 0;
        int h = 0;
        XToolBar_iconSize_2(bar, &w, &h);
        XQ_EQ_I64(w, 16);
        XQ_EQ_I64(h, 16);
        /* NULL 查询与 int 视图同口径（16×16），不是 (0,0)。 */
        w = -1;
        h = -1;
        XToolBar_iconSize_2(NULL, &w, &h);
        XQ_EQ_I64(w, 16);
        XQ_EQ_I64(h, 16);
        XQ_EQ_I64(XToolBar_iconSize(NULL), 16);
    }
    XQ_EQ_I64(XToolBar_actionCount(bar), 0);

    /* isAreaAllowed：(allowedAreas & area) == area（qtoolbar.h 内联）。 */
    XQ_CHECK(XToolBar_isAreaAllowed(bar, XToolBarArea_Top));
    XQ_CHECK(XToolBar_isAreaAllowed(bar, (int)XToolBarArea_Top |
                                            (int)XToolBarArea_Bottom));
    XToolBar_setAllowedAreas(bar, (int)XToolBarArea_Top);
    XQ_CHECK(XToolBar_isAreaAllowed(bar, XToolBarArea_Top));
    XQ_CHECK(!XToolBar_isAreaAllowed(bar, XToolBarArea_Bottom));
    /* 多位掩码只命中一位时**不放行**（旧实现按 !=0 会误判为允许）。 */
    XQ_CHECK(!XToolBar_isAreaAllowed(bar, (int)XToolBarArea_Top |
                                           (int)XToolBarArea_Bottom));
    XQ_CHECK(!XToolBar_isAreaAllowed(NULL, XToolBarArea_Top));

    XClassDelete((XClass*)bar);
}

/* ==================== ② 动作/控件管理与信号时机 ==================== */

XQ_TEST(XToolBar, actionWidgetManagement)
{
    XToolBar* bar = XToolBar_create(NULL, 0);
    XWidget* hosted;
    XAction* widgetAction;
    XAction* widgetAction2;
    XAction* textAction;
    XAction* sep;
    XAction* toggle;

    XQ_REQUIRE(bar != NULL);
    XWidget_resize((XWidget*)bar, 240, 32);

    /* addAction(text)：动作归工具栏所有，计入动作表。 */
    textAction = XToolBar_addAction_2(bar, "打开");
    XQ_REQUIRE(textAction != NULL);
    XQ_EQ_I64(XToolBar_actionCount(bar), 1);
    XQ_CHECK(XToolBar_action(bar, 0) == textAction);
    XQ_CHECK(XToolBar_action(bar, 99) == NULL);
    XQ_EQ_STR(XString_toUtf8((XString*)XAction_text_const(textAction)),
              "打开");

    /* addWidget：返回占位动作（Qt 返回 QWidgetAction*），
       widgetForAction 可反查控件。 */
    hosted = XWidget_create(NULL, 0);
    XQ_REQUIRE(hosted != NULL);
    widgetAction = XToolBar_addWidget(bar, hosted);
    XQ_CHECK_PTR(widgetAction);
    XQ_EQ_I64(XToolBar_actionCount(bar), 2);
    XQ_CHECK(XToolBar_widgetForAction(bar, widgetAction) == hosted);
    XQ_CHECK(XToolBar_widgetForAction(bar, textAction) == NULL);
    /* 控件已归工具栏（Qt：The toolbar takes ownership of widget）。 */
    XQ_CHECK(XObject_parent((XObject*)hosted) == (XObject*)bar);
    XQ_CHECK(XToolBar_actionAt(NULL, NULL) == NULL);
    XQ_CHECK(XToolBar_actionGeometry(NULL, NULL).width == 0);

    /* insertWidget(before)：占位动作插到 before 之前并返回该动作。 */
    {
        XWidget* hosted2 = XWidget_create(NULL, 0);
        XQ_REQUIRE(hosted2 != NULL);
        widgetAction2 = XToolBar_insertWidget(bar, textAction, hosted2);
        XQ_CHECK_PTR(widgetAction2);
        XQ_EQ_I64(XToolBar_actionCount(bar), 3);
        XQ_CHECK(XToolBar_action(bar, 0) == widgetAction2);
        XQ_CHECK(XToolBar_widgetForAction(bar, widgetAction2) == hosted2);
        XQ_CHECK(XToolBar_widgetForAction(bar, widgetAction) == hosted);
    }

    /* addSeparator/insertSeparator：分隔动作。 */
    sep = XToolBar_addSeparator(bar);
    XQ_REQUIRE(sep != NULL);
    XQ_CHECK(XAction_isSeparator(sep));
    XQ_EQ_I64(XToolBar_actionCount(bar), 4);
    {
        XAction* insSep = XToolBar_insertSeparator(bar, textAction);
        XQ_CHECK_PTR(insSep);
        XQ_CHECK(XAction_isSeparator(insSep));
        XQ_EQ_I64(XToolBar_actionCount(bar), 5);
    }

    /* removeAction：只摘除，不删除动作（Qt QWidget::removeAction 语义；
       借用动作仍可用，工具栏自建动作也仍以工具栏为 QObject 父存活，
       随工具栏析构回收）。 */
    {
        XAction* external = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             NULL, "外部");
        XQ_REQUIRE(external != NULL);
        XToolBar_addAction(bar, external);
        XQ_EQ_I64(XToolBar_actionCount(bar), 6);
        XToolBar_removeAction(bar, external);
        XQ_EQ_I64(XToolBar_actionCount(bar), 5);
        XAction_setText_2(external, "仍可写");
        XQ_EQ_STR(XString_toUtf8((XString*)XAction_text_const(external)),
                  "仍可写");
        XClassDelete((XClass*)external);
    }
    /* 摘除工具栏自建动作：动作表同步收缩，但**动作对象未被删除**
       （gaps 200：Qt 的 clear/removeAction 只移除动作，不 delete）。 */
    XToolBar_removeAction(bar, textAction);
    XQ_EQ_I64(XToolBar_actionCount(bar), 4);
    XQ_EQ_STR(XString_toUtf8((XString*)XAction_text_const(textAction)),
              "打开"); /* 对象仍存活，可继续读取 */

    /* toggleViewAction：可选中动作，触发切换工具栏可见性
       （QToolBarPrivate::_q_toggleView；checked ⟺ 显式显示）。顶层的
       工具栏构造后是"显式隐藏"（Qt 同：顶层控件未 show 前 isHidden），
       故初始 checked=false，触发一次后 show。 */
    toggle = XToolBar_toggleViewAction(bar);
    XQ_REQUIRE(toggle != NULL);
    XQ_CHECK(XToolBar_toggleViewAction(bar) == toggle); /* 缓存同一动作 */
    XQ_CHECK(XAction_isCheckable(toggle));
    XQ_CHECK(!XWidget_isVisible((XWidget*)bar));
    XQ_CHECK(!XAction_isChecked(toggle));
    XAction_trigger(toggle);
    XQ_CHECK(XAction_isChecked(toggle));
    XQ_CHECK(XWidget_isVisible((XWidget*)bar));
    XAction_trigger(toggle);
    XQ_CHECK(!XAction_isChecked(toggle));
    XQ_CHECK(!XWidget_isVisible((XWidget*)bar));

    /* 属性 setter 的同值早退 + 信号时机（qtoolbar.cpp:557-565）。 */
    g_xtbMovableChanged = 0;
    XObject_connect_1((XObject*)bar,
                      (size_t)XToolBar_movableChanged_signal(NULL, false),
                      (XObject*)bar, xtb_movableSlot,
                      XConnectionType_Direct);
    XToolBar_setMovable(bar, true); /* 同值：不发射 */
    XQ_EQ_I64(g_xtbMovableChanged, 0);
    XToolBar_setMovable(bar, false);
    XQ_EQ_I64(g_xtbMovableChanged, 1);
    XQ_CHECK(!XToolBar_isMovable(bar));
    XToolBar_setMovable(bar, false); /* 同值：不再发射 */
    XQ_EQ_I64(g_xtbMovableChanged, 1);

    XToolBar_setOrientation(bar, 2);
    XQ_EQ_I64(XToolBar_orientation(bar), 2);
    /* iconSize 是 QSize（宽高可不同，gaps 202）；int 版是方形视图。 */
    XToolBar_setIconSize(bar, 24);
    {
        int w = 0;
        int h = 0;
        XToolBar_iconSize_2(bar, &w, &h);
        XQ_EQ_I64(w, 24);
        XQ_EQ_I64(h, 24);
        XQ_EQ_I64(XToolBar_iconSize(bar), 24);
    }
    XToolBar_setIconSize_2(bar, 32, 20);
    {
        int w = 0;
        int h = 0;
        XToolBar_iconSize_2(bar, &w, &h);
        XQ_EQ_I64(w, 32);
        XQ_EQ_I64(h, 20);
        XQ_EQ_I64(XToolBar_iconSize(bar), 32); /* 非方形时 int 视图取宽 */
        /* 非法尺寸（任一轴 <= 0）忽略，保持上次值（方边入口 0 亦忽略）。 */
        XToolBar_setIconSize_2(bar, 0, 18);
        XToolBar_setIconSize_2(bar, 18, 0);
        XToolBar_setIconSize(bar, 0);
        XToolBar_iconSize_2(bar, &w, &h);
        XQ_EQ_I64(w, 32);
        XQ_EQ_I64(h, 20);
        XToolBar_iconSize_2(bar, NULL, NULL); /* 输出参数可为 NULL */
    }
    XToolBar_setFloatable(bar, false);
    XQ_CHECK(!XToolBar_isFloatable(bar));

    /* clear：动作表清空，但动作对象不删除（qtoolbar.cpp:743-748；
       gaps 200）。这里的动作仍以工具栏为 QObject 父，随工具栏析构。 */
    {
        XAction* kept = XToolBar_addAction_2(bar, "保留");
        XQ_REQUIRE(kept != NULL);
        XToolBar_clear(bar);
        XQ_EQ_I64(XToolBar_actionCount(bar), 0);
        XQ_CHECK(XToolBar_action(bar, 0) == NULL);
        XQ_EQ_STR(XString_toUtf8((XString*)XAction_text_const(kept)),
                  "保留"); /* 动作未被 delete */
    }

    XClassDelete((XClass*)bar);
}

#endif /* XGUI_ON && XWIDGET_ON && XTOOLBAR_ON && XACTION_ON && XTOOLBUTTON_ON */
