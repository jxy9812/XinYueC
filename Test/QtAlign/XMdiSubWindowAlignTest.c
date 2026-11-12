/**
 * @file       XMdiSubWindowAlignTest.c
 * @brief      XMdiSubWindow × Qt 6.8 QMdiSubWindow 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/
 *               qmdisubwindow.h（SubWindowOption 0x1/0x2/0x4/0x8、
 *               setWidget/widget、setOption/testOption、
 *               setKeyboardSingleStep/PageStep、isShaded/showShaded、
 *               setSystemMenu/systemMenu、mdiArea、sizeHint/
 *               minimumSizeHint、maximizedButtonsWidget 等）；
 *             - Qt 6.8 源码 src/widgets/widgets/qmdisubwindow.cpp 原文
 *               （cdn.jsdelivr.net/gh/qt/qtbase@v6.8.0）：
 *               * setKeyboardSingleStep/PageStep（:2402-2407、:2429-2434）
 *                 显式注明"不做下界检查"：0=不动、负值=反向，原样存储；
 *               * setWidget(nullptr) = removeBaseWidget（内容件交还调用方、
 *                 不删除，:2257-2265）；同指针重设告警后原样返回；
 *               * setSystemMenu 取得所有权，旧菜单先删（:2455-2478）；
 *               * minimumSizeHint（:3465-3510）：无父窗口时只取内容件
 *                 最小尺寸（sizeParameters 的 margin/minWidth 均为 0），
 *                 有父窗口时=标题带+内容件最小尺寸；折叠态只留标题带；
 *               * 标题条文本即 QWidget::windowTitle()（:994、:3086-3138），
 *                 QMdiSubWindow 没有独立标题字段。
 *             - 本机 Qt 6.8.0 offscreen 探针实测：
 *               ① setKeyboardSingleStep(0)/setKeyboardPageStep(-3) 读回
 *                  0/-3；
 *               ② 独立 QMdiSubWindow setWindowTitle("via-base") 后
 *                  windowTitle() 同值；
 *               ③ 独立子窗 minimumSizeHint()==0x0；
 *               ④ 内容件 setMinimumSize(120,80) 入 QMdiArea 后
 *                  minimumSizeHint()==128x108（Fusion margin=4、
 *                  titleBarHeight=24）。
 *
 *             **本轮已对齐**（Qt 6.8 原文 + 本机探针）：
 *             ① showShaded 无父直接返回、已折叠再调也返回；取消折叠走
 *                showNormal/setWindowState(NoState)（探针：shaded 后
 *                300x200→300x24、内容件隐藏，setWindowState(NoState)
 *                恢复 300x200、内容件复显）；
 *             ② 内容件标题动态跟随（eventFilter 的 WindowTitleChange，
 *                qmdisubwindow.cpp:2704-2711）；
 *             ③ 构造即建标准系统菜单（qmdisubwindow.cpp:2202-2210）；
 *             ④ sizeHint 无 200x150 下限（qmdisubwindow.cpp:3451-3462；
 *                探针独立子窗 sizeHint=0x0）；
 *             ⑤ 标题带关闭/最小化按钮接命中（CC_MdiControls 的
 *                hitTestComplexControl + _q_close/_q_minimize，
 *                qmdisubwindow.cpp:519-529、617-627）。
 * @note       C99；子窗口/内容件全部释放（父析构带走子对象），
 *             漏删由运行器 `[LEAK]` 判失败。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON

#include "XMdiArea.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XObject.h"
#include "XClass.h"
#include "XVarList.h"
#include "XContainer.h"
#include "XVector.h"
#include "XString.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#if XMENU_ON
#include "XMenu.h"
#include "XAction.h"
#endif

/* ==================== 信号探针 ==================== */

static int g_swStateChanged; /**< windowStateChanged 命中次数。 */
static int g_swStateOld;     /**< 最近一次旧状态位。 */
static int g_swStateNew;     /**< 最近一次新状态位。 */

static void xswTestReset(void)
{
    g_swStateChanged = 0;
    g_swStateOld = -1;
    g_swStateNew = -1;
}

static void xswTestStateSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, oldState, int, newState);
    ++g_swStateChanged;
    g_swStateOld = oldState;
    g_swStateNew = newState;
}

static void xswTestConnect(XMdiSubWindow* sw)
{
    XObject_connect((XObject*)sw,
                      XSignal(XMdiSubWindow_windowStateChanged_signal),
                      (XObject*)sw, xswTestStateSlot,
                      XConnectionType_Direct);
}

/* ==================== ① 缺省值 ==================== */

XQ_TEST(XMdiSubWindow, defaults)
{
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    const XString* baseTitle;
    XSize hint;
    XSize minHint;
    XQ_REQUIRE(sw != NULL);

    /* Qt QMdiSubWindowPrivate 缺省值：5 / 20（qmdisubwindow.cpp:2397、
     * 2424 的 property 默认）。 */
    XQ_EQ_I64(XMdiSubWindow_keyboardSingleStep(sw), 5);
    XQ_EQ_I64(XMdiSubWindow_keyboardPageStep(sw), 20);
    XQ_CHECK(XMdiSubWindow_widget(sw) == NULL);
#if XMENU_ON
    /* 构造即建标准系统菜单（qmdisubwindow.cpp:2202-2210 调
     * createSystemMenu；Qt 文档『By default, each QMdiSubWindow has a
     * standard system menu』）。 */
    XQ_CHECK_PTR(XMdiSubWindow_systemMenu(sw));
#else
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) == NULL);
#endif
    XQ_CHECK(!XMdiSubWindow_isShaded(sw));
    /* 窗口标志缺省（本机 Qt 6.8.0 探针：独立 QMdiSubWindow flags=
     * 0x8800f001 = Window 类型 + Title/SystemMenu/Minimize/Maximize/
     * Close 提示位；经 addSubWindow 接管后类型位转 SubWindow）。 */
    {
        XWidgetFlags flags = XWidget_windowFlags((XWidget*)sw);
        XQ_EQ_I64((long long)(flags & XWindowType_TypeMask),
                  (long long)XWindowType_Window);
        XQ_CHECK((flags & (XWidgetFlags)XWindowType_WindowMinimizeButtonHint)
                 != 0);
        XQ_CHECK((flags & (XWidgetFlags)XWindowType_WindowCloseButtonHint)
                 != 0);
    }
    /* 独立子窗（无 XMdiArea 祖先）反查为 NULL（Qt 同）。 */
    XQ_CHECK(XMdiSubWindow_mdiArea(sw) == NULL);
    /* Qt 标记 internal 的两处 API：本库无独立按钮控件实例。 */
    XQ_CHECK(XMdiSubWindow_maximizedButtonsWidget(sw) == NULL);
    XQ_CHECK(XMdiSubWindow_maximizedSystemMenuIconWidget(sw) == NULL);

    /* 标题走基类 QWidget::windowTitle（Qt 无独立标题字段）。 */
    baseTitle = XWidget_windowTitle((XWidget*)sw);
    XQ_CHECK(baseTitle == NULL ||
             XString_size((const XContainer*)baseTitle) == 0);
    XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "");

    /* 独立子窗 minimumSizeHint 只由内容件决定，无内容件即 0x0
     * （Qt 6.8.0 探针实测 `solo min=0x0`）。 */
    minHint = XWidget_minimumSizeHint_base((const XWidget*)sw);
    XQ_EQ_I64(minHint.width, 0);
    XQ_EQ_I64(minHint.height, 0);

    /* sizeHint：无 200x150 下限（Qt qmdisubwindow.cpp:3451-3462 只把
     * 装饰 + 内容 sizeHint 展开到 minimumSizeHint；本机探针独立
     * QMdiSubWindow sizeHint=0x0、minimumSizeHint=0x0）。 */
    hint = XWidget_sizeHint_base((const XWidget*)sw);
    XQ_EQ_I64(hint.width, 0);
    XQ_EQ_I64(hint.height, 0);

    XClassDelete((XClass*)sw);
}

/* ==================== ② 键盘步进原样存取（0/负值） ==================== */

XQ_TEST(XMdiSubWindow, keyboardStepsStoreRawValues)
{
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    XQ_REQUIRE(sw != NULL);

    /* Qt 原文注释：负值=反向、0=不动，均不做下界检查
     * （qmdisubwindow.cpp:2402-2407、2429-2434）。 */
    XMdiSubWindow_setKeyboardSingleStep(sw, 0);
    XQ_EQ_I64(XMdiSubWindow_keyboardSingleStep(sw), 0);
    XMdiSubWindow_setKeyboardSingleStep(sw, -3);
    XQ_EQ_I64(XMdiSubWindow_keyboardSingleStep(sw), -3);
    XMdiSubWindow_setKeyboardPageStep(sw, 0);
    XQ_EQ_I64(XMdiSubWindow_keyboardPageStep(sw), 0);
    XMdiSubWindow_setKeyboardPageStep(sw, -7);
    XQ_EQ_I64(XMdiSubWindow_keyboardPageStep(sw), -7);

    /* 正常值往返（既有回归锁定的 7/42 口径仍成立）。 */
    XMdiSubWindow_setKeyboardSingleStep(sw, 7);
    XMdiSubWindow_setKeyboardPageStep(sw, 42);
    XQ_EQ_I64(XMdiSubWindow_keyboardSingleStep(sw), 7);
    XQ_EQ_I64(XMdiSubWindow_keyboardPageStep(sw), 42);

    XClassDelete((XClass*)sw);
}

/* ==================== ③ 标题 = 基类 windowTitle ==================== */

XQ_TEST(XMdiSubWindow, windowTitleIsBaseProperty)
{
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    XWidget* content = XWidget_create(NULL, 0);
    XWidget* content2 = XWidget_create(NULL, 0);
    const XString* baseTitle;
    XQ_REQUIRE(sw != NULL && content != NULL && content2 != NULL);

    /* ① 经基类 API 设置的标题，子窗 API 必须读回（Qt 标题条绘制
     *    读的就是 QWidget::windowTitle()）。 */
    XWidget_setWindowTitle_2((XWidget*)sw, "基类标题");
    XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "基类标题");

    /* ② 经子窗 API 设置的标题，基类 API 必须读回（同一属性）。 */
    XMdiSubWindow_setWindowTitle_2(sw, "子窗标题");
    baseTitle = XWidget_windowTitle((XWidget*)sw);
    XQ_CHECK_PTR(baseTitle);
    XQ_EQ_STR(baseTitle ? XString_toUtf8(baseTitle) : "", "子窗标题");

    /* ③ setWidget 时子窗标题为空 ⇒ 采纳内容件 windowTitle
     *    （qmdisubwindow.cpp:994 的标题同步链）。 */
    XMdiSubWindow_setWindowTitle_2(sw, "");
    XWidget_setWindowTitle_2(content, "内容标题");
    XMdiSubWindow_setWidget(sw, content);
    XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "内容标题");

    /* ④ 子窗标题仍跟随旧内容件（与旧内容件标题相同）⇒ removeBaseWidget
     *    先清空子窗标题（qmdisubwindow.cpp:997-1001），setWidget 再采纳
     *    新内容件标题（:2292-2299）。 */
    XWidget_setWindowTitle_2(content2, "另一标题");
    XMdiSubWindow_setWidget(sw, content2);
    XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "另一标题");
    /* 旧内容件被摘链（交还调用方，不删除）。 */
    XQ_CHECK(XWidget_parentWidget(content) == NULL);
    XClassDelete((XClass*)content);

    /* ④b 子窗标题是用户自定义（≠ 旧内容件标题）⇒ 替换内容件不改标题
     *     （removeBaseWidget 的相等判定不成立，:997-1001）。 */
    {
        XWidget* content3 = XWidget_create(NULL, 0);
        XQ_REQUIRE(content3 != NULL);
        XMdiSubWindow_setWindowTitle_2(sw, "自定义标题");
        XWidget_setWindowTitle_2(content3, "第三标题");
        XMdiSubWindow_setWidget(sw, content3);
        XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "自定义标题");
        /* content2 被摘链交还调用方，content3 随子窗释放。 */
        XQ_CHECK(XWidget_parentWidget(content2) == NULL);
        XClassDelete((XClass*)content2);
    }

    XClassDelete((XClass*)sw); /* content3 随子窗释放 */
}

/* ==================== ④ setWidget 所有权与替换语义 ==================== */

XQ_TEST(XMdiSubWindow, setWidgetOwnership)
{
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    XWidget* c1 = XWidget_create(NULL, 0);
    XWidget* c2 = XWidget_create(NULL, 0);
    XRect geo;
    XQ_REQUIRE(sw != NULL && c1 != NULL && c2 != NULL);

    XMdiSubWindow_setWidget(sw, c1);
    XQ_CHECK(XMdiSubWindow_widget(sw) == c1);
    XQ_CHECK(XWidget_parentWidget(c1) == (XWidget*)sw);
    /* 内容件内收 1px、标题带 20px 之下挂载。 */
    geo = XWidget_geometry(c1);
    XQ_EQ_I64(geo.x, 1);
    XQ_EQ_I64(geo.y, 20);

    /* 同指针重设：Qt 告警后原样返回（幂等，不摘链）。 */
    XMdiSubWindow_setWidget(sw, c1);
    XQ_CHECK(XMdiSubWindow_widget(sw) == c1);
    XQ_CHECK(XWidget_parentWidget(c1) == (XWidget*)sw);

    /* 替换：旧内容件 setParent(nullptr) 交还调用方、不删除。 */
    XMdiSubWindow_setWidget(sw, c2);
    XQ_CHECK(XMdiSubWindow_widget(sw) == c2);
    XQ_CHECK(XWidget_parentWidget(c1) == NULL);
    XQ_CHECK(XWidget_parentWidget(c2) == (XWidget*)sw);
    XClassDelete((XClass*)c1);

    /* setWidget(NULL) = removeBaseWidget：摘链不销毁。 */
    XMdiSubWindow_setWidget(sw, NULL);
    XQ_CHECK(XMdiSubWindow_widget(sw) == NULL);
    XQ_CHECK(XWidget_parentWidget(c2) == NULL);
    XClassDelete((XClass*)c2);

    XClassDelete((XClass*)sw);
}

/* ==================== ⑤ 选项位（0x1/0x2/0x4/0x8 独立） ==================== */

XQ_TEST(XMdiSubWindow, optionsRoundTrip)
{
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    XQ_REQUIRE(sw != NULL);

    XQ_CHECK(!XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_AllowOutsideAreaHorizontally));
    XQ_CHECK(!XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_AllowOutsideAreaVertically));
    XQ_CHECK(!XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_RubberBandResize));
    XQ_CHECK(!XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_RubberBandMove));

    /* 四个位互不串扰（Qt QFlags::setFlag 单 bit 语义）。 */
    XMdiSubWindow_setOption(sw, XMdiSubWindowOption_RubberBandResize, true);
    XQ_CHECK(XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_RubberBandResize));
    XQ_CHECK(!XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_RubberBandMove));
    XMdiSubWindow_setOption(sw, XMdiSubWindowOption_RubberBandMove, true);
    XMdiSubWindow_setOption(sw, XMdiSubWindowOption_RubberBandResize, false);
    XQ_CHECK(!XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_RubberBandResize));
    XQ_CHECK(XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_RubberBandMove));
    XMdiSubWindow_setOption(sw,
        XMdiSubWindowOption_AllowOutsideAreaHorizontally, true);
    XQ_CHECK(XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_AllowOutsideAreaHorizontally));
    XMdiSubWindow_setOption(sw,
        XMdiSubWindowOption_AllowOutsideAreaHorizontally, false);

    XClassDelete((XClass*)sw);
}

/* ==================== ⑥ 系统菜单所有权 ==================== */

XQ_TEST(XMdiSubWindow, systemMenuOwnership)
{
#if XMENU_ON
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    XMenu* menu1 = XMenu_create();
    XMenu* menu2 = XMenu_create();
    XQ_REQUIRE(sw != NULL && menu1 != NULL && menu2 != NULL);

    /* 构造即有标准系统菜单（qmdisubwindow.cpp:2202-2210）；设置用户
     * 菜单时旧菜单（含标准菜单）被删除（qmdisubwindow.cpp:2455-2478）。 */
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) != NULL);
    XMdiSubWindow_setSystemMenu(sw, menu1);
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) == menu1);
    /* 同指针重设：Qt 告警后原样返回，不删不换。 */
    XMdiSubWindow_setSystemMenu(sw, menu1);
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) == menu1);
    /* 替换：旧菜单归子窗删除（Qt delete d->systemMenu）。 */
    XMdiSubWindow_setSystemMenu(sw, menu2);
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) == menu2);
    /* 置空：删除并清空。 */
    XMdiSubWindow_setSystemMenu(sw, NULL);
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) == NULL);

    XClassDelete((XClass*)sw);
#else
    XQ_CHECK(true); /* XMENU_ON 关闭时无 XMenu 实现，跳过。 */
#endif
}

/* ==================== ⑦ 折叠状态与 windowStateChanged ==================== */

XQ_TEST(XMdiSubWindow, shadedStateAndSignal)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw;
    XQ_REQUIRE(area != NULL);
    /* 内容件带父创建 ⇒ 显式隐藏位为 0（无父创建的首个顶层控件带
     * WState_Hidden，属 XWidget 换父语义的既有差异），折叠隐藏/复显
     * 才能完整观测。 */
    sw = XMdiArea_addSubWindow(area, XWidget_create((XWidget*)area, 0));
    XQ_REQUIRE(sw != NULL);

    xswTestReset();
    xswTestConnect(sw);

    XQ_CHECK(!XMdiSubWindow_isShaded(sw));
    XMdiSubWindow_showShaded(sw);
    XQ_CHECK(XMdiSubWindow_isShaded(sw));
    /* 折叠只留标题带 20px（本库 chrome 口径）。 */
    XQ_EQ_I64(XWidget_height((XWidget*)sw), 20);
    XQ_EQ_I64(g_swStateChanged, 1);
    XQ_EQ_I64(g_swStateOld, 0);
    XQ_EQ_I64(g_swStateNew, XMdiSubWindowState_Shaded);

    /* Qt 6.8 的 showShaded 在已折叠时直接返回
     * （qmdisubwindow.cpp:2553-2561 `if (... && isShaded()) return;`），
     * **不是**开关：再调仍是折叠、无新信号。 */
    XMdiSubWindow_showShaded(sw);
    XQ_CHECK(XMdiSubWindow_isShaded(sw));
    XQ_EQ_I64(g_swStateChanged, 1);

    /* 取消折叠走 showNormal（Qt setWindowState(NoState) → setNormalMode，
     * qmdisubwindow.cpp:1246-1288；本机探针恢复原尺寸并复显内容件）。 */
    XWidget_showNormal((XWidget*)sw);
    XQ_CHECK(!XMdiSubWindow_isShaded(sw));
    XQ_EQ_I64(g_swStateChanged, 2);
    XQ_EQ_I64(g_swStateOld, XMdiSubWindowState_Shaded);
    XQ_EQ_I64(g_swStateNew, 0);
    XQ_CHECK(XWidget_height((XWidget*)sw) > 20);
    XQ_CHECK(!XWidget_isHidden(XMdiSubWindow_widget(sw)));

    /* 独立子窗（无父）：Qt 的 showShaded 直接返回
     * （`if (!parent()) return;`，本机探针 solo isShaded=0、尺寸不变）。 */
    {
        XMdiSubWindow* solo = XMdiSubWindow_create(NULL, 0);
        XQ_REQUIRE(solo != NULL);
        XQ_EQ_I64(XWidget_height((XWidget*)solo), 150);
        XMdiSubWindow_showShaded(solo);
        XQ_CHECK(!XMdiSubWindow_isShaded(solo));
        XQ_EQ_I64(XWidget_height((XWidget*)solo), 150);
        XClassDelete((XClass*)solo);
    }

    XClassDelete((XClass*)area);
}

/* ==================== ⑧ minimumSizeHint 计入内容件最小尺寸 ============ */

XQ_TEST(XMdiSubWindow, minimumSizeHintTracksContent)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XWidget* content = XWidget_create(NULL, 0);
    XMdiSubWindow* sw;
    XSize contentMin;
    XSize hint;
    XQ_REQUIRE(area != NULL && content != NULL);

    /* 内容件最小尺寸 (120,80)（Qt 探针用 QWidget::setMinimumSize(120,80)
     * 得到 128x108：margin=4×2、titleBarHeight=24）。本库 chrome 为
     * 1px 边框 + 20px 标题带 ⇒ 最小宽 >= 122、最小高 >= 101。 */
    XSize_init(&contentMin, 120, 80);
    XWidget_setMinimumSizeHint(content, &contentMin);

    sw = XMdiArea_addSubWindow(area, content);
    XQ_REQUIRE(sw != NULL);
    hint = XWidget_minimumSizeHint_base((const XWidget*)sw);
    XQ_CHECK_MSG(hint.width >= 122, "minW=%d (<120+2)", hint.width);
    XQ_CHECK_MSG(hint.height >= 101, "minH=%d (<20+1+80)", hint.height);

    /* 折叠态只保留标题带高度、宽度不小于 chrome 最小宽 50。
     * （偏差：本库 showShaded 是开关语义，见上一用例注释。） */
    XMdiSubWindow_showShaded(sw);
    hint = XWidget_minimumSizeHint_base((const XWidget*)sw);
    XQ_EQ_I64(hint.height, 20);
    XQ_CHECK(hint.width >= 50);

    XClassDelete((XClass*)area);
}

/* ==================== ⑨ 归属反查与父子链 ==================== */

XQ_TEST(XMdiSubWindow, mdiAreaOwnership)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw;
    XMdiSubWindow* solo;
    XQ_REQUIRE(area != NULL);

    sw = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    XQ_REQUIRE(sw != NULL);
    /* 子窗挂在 area 视口下，反查按父链 + 视口判定归属。 */
    XQ_CHECK(XMdiSubWindow_mdiArea(sw) == area);

    solo = XMdiSubWindow_create(NULL, 0);
    XQ_REQUIRE(solo != NULL);
    XQ_CHECK(XMdiSubWindow_mdiArea(solo) == NULL);
    /* 独立子窗亦可挂内容件并随自身析构释放。 */
    XMdiSubWindow_setWidget(solo, XWidget_create(NULL, 0));
    XQ_CHECK(XMdiSubWindow_widget(solo) != NULL);
    XClassDelete((XClass*)solo);

    /* removeSubWindow(子窗口) 只摘链不删除（既有 XMdiArea 契约）。 */
    XMdiArea_removeSubWindow(area, (XWidget*)sw);
    XQ_CHECK(XMdiSubWindow_mdiArea(sw) == NULL);
    XClassDelete((XClass*)sw);
    XClassDelete((XClass*)area);
}

/* ==================== ⑩ 内容件标题动态跟随 ==================== */

XQ_TEST(XMdiSubWindow, titleFollowsWidgetTitleChanges)
{
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    XWidget* content = XWidget_create(NULL, 0);
    XQ_REQUIRE(sw != NULL && content != NULL);

    /* setWidget 时子窗标题为空 ⇒ 采纳内容件标题并记录
     * lastChildWindowTitle（qmdisubwindow.cpp:2292-2299）。 */
    XWidget_setWindowTitle_2(content, "alpha");
    XMdiSubWindow_setWidget(sw, content);
    XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "alpha");

    /* 内容件标题变化 ⇒ 子窗继续跟随（eventFilter 的 WindowTitleChange
     * 分支，qmdisubwindow.cpp:2704-2711；本机 Qt 探针同）。 */
    XWidget_setWindowTitle_2(content, "beta");
    XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "beta");

    /* 子窗被改成自定义标题后，内容件再变也不覆盖
     * （updateWindowTitle 的 lastChildWindowTitle 判定，
     * qmdisubwindow.cpp:1852-1854）。 */
    XMdiSubWindow_setWindowTitle_2(sw, "custom");
    XWidget_setWindowTitle_2(content, "gamma");
    XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw), "custom");

    /* removeBaseWidget：子窗标题仍等于内容件标题时清空子窗标题
     * （qmdisubwindow.cpp:997-1001）。 */
    {
        XMdiSubWindow* sw2 = XMdiSubWindow_create(NULL, 0);
        XWidget* c2 = XWidget_create(NULL, 0);
        XQ_REQUIRE(sw2 != NULL && c2 != NULL);
        XWidget_setWindowTitle_2(c2, "same");
        XMdiSubWindow_setWidget(sw2, c2);
        XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw2), "same");
        XMdiSubWindow_setWidget(sw2, NULL);
        XQ_EQ_STR(XMdiSubWindow_windowTitle_2(sw2), "");
        XClassDelete((XClass*)c2); /* 内容件交还调用方 */
        XClassDelete((XClass*)sw2);
    }

    XClassDelete((XClass*)sw); /* content 随子窗释放 */
}

/* ==================== ⑪ 缺省系统菜单 ==================== */

XQ_TEST(XMdiSubWindow, defaultSystemMenuAndActions)
{
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    XQ_REQUIRE(sw != NULL);
#if XMENU_ON
    {
        XMenu* menu = XMdiSubWindow_systemMenu(sw);
        const XVector* actions;
        int64_t n;
        XQ_CHECK_PTR(menu);
        /* 标准条目 Restore/Minimize/Maximize/StayOnTop/分隔/Close。 */
        actions = menu ? XMenu_actions(menu) : NULL;
        XQ_CHECK_PTR(actions);
        n = actions ? (int64_t)XVector_size_base((const XContainer*)actions)
                    : 0;
        XQ_CHECK_MSG(n >= 6, "actions=%lld", (long long)n);
        if (n >= 4) {
            XAction* stay =
                XVector_At_Base((XVector*)actions, 3, XAction*);
            XQ_CHECK(stay != NULL && XAction_isCheckable(stay));
        }
    }
    /* setSystemMenu(NULL) 删除标准菜单（Qt：delete d->systemMenu）。 */
    XMdiSubWindow_setSystemMenu(sw, NULL);
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) == NULL);
#else
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) == NULL);
#endif
    XClassDelete((XClass*)sw);
}

/* ==================== ⑫ 标题带关闭/最小化按钮命中 ==================== */

/** @brief 向控件直发一次鼠标左键事件（本地坐标）。 */
static void xsw_mousePress(XWidget* target, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!target) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

XQ_TEST(XMdiSubWindow, titleBarButtonsCloseAndMinimize)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw;
    int w;
    XQ_REQUIRE(area != NULL);

    /* 关闭钮：命中矩形 w-26..w-10、y=2..18（与 paintEvent 绘制同源）。 */
    sw = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    XQ_REQUIRE(sw != NULL);
    w = XWidget_width((XWidget*)sw);
    XQ_CHECK_MSG(w > 58, "w=%d", w);
    xsw_mousePress((XWidget*)sw, w - 18, 10);
    /* 内部创建的带 WA_DeleteOnClose：close 接受后移出列表并延迟回收
     * （对标 ControllerWidget::_q_close → mdiChild->close() 与
     * WA_DeleteOnClose，qmdisubwindow.cpp:701/617、qmdiarea.cpp:1962）。 */
    XQ_EQ_I64(XMdiArea_subWindowCount(area), 0);
    XQ_CHECK(XWidget_isHidden((XWidget*)sw));
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);

    /* 最小化钮：命中矩形 w-42..w-26、y=2..18 → showMinimized，
     * 缩成标题带一条、isShaded 保持 false（对标 setMinimizeMode，
     * qmdisubwindow.cpp:1218-1246；本机探针 200x150@(50,50) →
     * 196x24@(0,276)、isShaded=0）。 */
    sw = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    XQ_REQUIRE(sw != NULL);
    w = XWidget_width((XWidget*)sw);
    xsw_mousePress((XWidget*)sw, w - 34, 10);
    XQ_CHECK(XWidget_isMinimized((XWidget*)sw));
    XQ_CHECK(!XMdiSubWindow_isShaded(sw));
    XQ_EQ_I64(XWidget_height((XWidget*)sw), 20);
    /* showNormal 复原几何（Qt setWindowState(NoState) → setNormalMode）。 */
    XWidget_showNormal((XWidget*)sw);
    XQ_CHECK(!XWidget_isMinimized((XWidget*)sw));
    XQ_EQ_I64(XWidget_width((XWidget*)sw), w);
    XQ_EQ_I64(XWidget_height((XWidget*)sw), 150);

    XClassDelete((XClass*)area);
}

/* ==================== ⑬ sizeHint 无 200x150 下限（有父窗口） ========== */

XQ_TEST(XMdiSubWindow, sizeHintNoLegacyFloor)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw;
    XSize hint;
    XQ_REQUIRE(area != NULL);
    sw = XMdiArea_addSubWindow(area, XWidget_create(NULL, 0));
    XQ_REQUIRE(sw != NULL);
    hint = XWidget_sizeHint_base((const XWidget*)sw);
    /* 旧实现下限 200x170；Qt 只用装饰 + 内容 sizeHint 再并到
     * minimumSizeHint（qmdisubwindow.cpp:3451-3462）。本框架 chrome
     * 档最小宽 50、高 21，远小于旧下限。 */
    XQ_CHECK_MSG(hint.width < 200 && hint.height < 170,
                 "sizeHint=%dx%d", hint.width, hint.height);
    XQ_CHECK(hint.width >= 50 && hint.height >= 21);
    XClassDelete((XClass*)area);
}

/* ==================== ⑭ 接管路径的显式 flags（P0-52） ================= */
/* Qt qmdiarea.cpp:1955：`child->setParent(viewport(), windowFlags ?
 * windowFlags : child->windowFlags())`——显式 flags 优先，随后经 QWidget
 * 换父的类型位规范化（Window → SubWindow，提示位保留）。 */

XQ_TEST(XMdiSubWindow, addSubWindowFlagsOnAdoptedPath)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    XMdiSubWindow* sw;
    XMdiSubWindow* ret;
    XWidgetFlags flags;
    XQ_REQUIRE(area != NULL);
    sw = XMdiSubWindow_create(NULL, 0);
    XQ_REQUIRE(sw != NULL);
    /* 独立子窗构造：Window 类型位 + 提示位（无父实测 0x8800f001 口径）。 */
    XQ_CHECK(XWidget_isWindow((XWidget*)sw));
    flags = XWidget_windowFlags((XWidget*)sw);
    XQ_EQ_I64((long long)(flags & XWindowType_TypeMask),
              (long long)XWindowType_Window);

    /* 显式 flags 优先：只带 FramelessWindowHint（无提示位），接管后
     * 类型位规范化为 SubWindow、Frameless 保留、其余提示位不补（Qt 用
     * 调用方给的那一份，不叠加 child 原有位）。 */
    ret = XMdiArea_addSubWindow_2(
        area, (XWidget*)sw, (XWidgetFlags)XWindowType_FramelessWindowHint);
    XQ_CHECK(ret == sw);
    XQ_CHECK(XMdiSubWindow_mdiArea(sw) == area);
    XQ_CHECK(!XWidget_isWindow((XWidget*)sw));
    flags = XWidget_windowFlags((XWidget*)sw);
    XQ_CHECK((flags & (XWidgetFlags)XWindowType_FramelessWindowHint) != 0);
    XQ_EQ_I64((long long)(flags & XWindowType_TypeMask),
              (long long)XWindowType_SubWindow);
    XQ_CHECK((flags & (XWidgetFlags)XWindowType_WindowSystemMenuHint) == 0);
    XQ_CHECK((flags & (XWidgetFlags)XWindowType_WindowCloseButtonHint) == 0);

    XClassDelete((XClass*)area); /* 接管来的子窗随区域级联释放 */
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON */
