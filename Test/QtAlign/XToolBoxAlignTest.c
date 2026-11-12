/**
 * @file       XToolBoxAlignTest.c
 * @brief      XToolBox × Qt 6.8 QToolBox 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtoolbox.h
 *               （addItem/insertItem/removeItem、itemText/itemEnabled、
 *               widget/indexOf/count、currentIndex/currentWidget、
 *               setCurrentIndex/setCurrentWidget 槽、currentChanged(int)；
 *               protected virtual itemInserted/itemRemoved）；
 *             - Qt 6.8.3 源码 qtoolbox.cpp：
 *               insertItem（:308-356 index<0 或 ≥count ⇒ 追加；首页
 *               `if (index == 0) setCurrentIndex(index)`；前插落在当前页
 *               之前 ⇒ 按同一控件新索引重选并重发；末尾 itemInserted）、
 *               setCurrentIndex（:387-403 只拦「无效索引」与「同页」，
 *               不拦禁用条目）、
 *               setItemEnabled（:544-571 禁用当前页时先向下再向上找最近
 *               启用页并切换）、
 *               removeItem（:453-462 → _q_widgetDestroyed :423-449：
 *               控件不删除、`w->setParent(this)`；列表清空发
 *               currentChanged(-1)，移出当前页时 `setCurrentIndex(0)`）、
 *             - 本机 Qt 6.8.0 + offscreen 实测：3 项时移除当前项（index 2）
 *               后 currentIndex 变 0 并发 currentChanged(0)；清空后发
 *               currentChanged(-1)；被移除页 parentWidget() 仍是工具箱。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON

#include "XToolBox.h"
#include "XToolBox_Protected.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h" /* XObject_event_base（鼠标事件注入）。 */
#include "XVarList.h"
#include "XString.h"
#include "XClass.h"
#include "XEvent.h"   /* XMouseEvent */
#include "XImage.h"   /* [P0-80] 离屏抓图版式断言。 */
#include "XThread.h"  /* [P0-36] 提示淡出等待。 */
#include "XToolTip.h" /* [P0-36] 条目提示悬停弹出。 */
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
#include "XScrollArea.h"
#endif
#include "XCoreApplication.h"

/** @brief currentChanged 计数与最近索引。 */
static int g_xtbChanged;
static int g_xtbLast;

static void xtb_changedSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, int, index);
    (void)receiver;
    ++g_xtbChanged;
    g_xtbLast = index;
}

static void xtb_hook(XToolBox* box)
{
    XObject_connect((XObject*)box,
                      (size_t)XToolBox_currentChanged_signal(box, 0),
                      (XObject*)box, xtb_changedSlot,
                      XConnectionType_Direct);
}

/** @brief 向控件直发一次鼠标移动（同 XGroupBoxAlignTest 的注入口径）。 */
static void xtb_mouseMove(XWidget* target, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!target) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                     XKeyboardModifier_NoModifier, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

/** @brief 等提示淡出到点（hideText 恒 300ms 延迟隐藏；上限 2s 防呆）。 */
static bool xtb_waitToolTipHidden(int maxMs)
{
    int waited = 0;
    while (XToolTip_isVisible() && waited < maxMs) {
        XThread_msleep(10);
        (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
        waited += 10;
    }
    return !XToolTip_isVisible();
}

/** @brief 取 XToolTip_text() 的副本比对后释放。 */
static bool xtb_toolTipTextEquals(const char* expect)
{
    XString* text = XToolTip_text();
    const char* utf8;
    bool same;
    if (!text) return expect == NULL;
    utf8 = XString_toUtf8(text);
    same = (utf8 == NULL) ? (expect == NULL)
                          : (expect != NULL && strcmp(utf8, expect) == 0);
    XClassDelete((XClass*)text);
    return same;
}

/** @brief 两张抓图中矩形区的差异像素数（用于版式位移断言）。 */
static int xtb_countDiff(const XImage* a, const XImage* b, const XRect* r)
{
    int x;
    int y;
    int n = 0;
    if (!a || !b || !r) return 0;
    for (y = r->y; y < r->y + r->height; ++y) {
        for (x = r->x; x < r->x + r->width; ++x) {
            if (XImage_pixel(a, x, y) != XImage_pixel(b, x, y)) ++n;
        }
    }
    return n;
}

/* ==================== ① 缺省值 + 页增删与索引钳制 ==================== */

XQ_TEST(XToolBox, defaultsAndItemClamp)
{
    XToolBox* box = XToolBox_create(NULL, 0);
    XWidget* p1;
    XWidget* p2;
    XWidget* p3;
    XWidget* p4;

    XQ_REQUIRE(box != NULL);
    p1 = XWidget_create(NULL, 0);
    p2 = XWidget_create(NULL, 0);
    p3 = XWidget_create(NULL, 0);
    p4 = XWidget_create(NULL, 0);
    XQ_REQUIRE(p1 && p2 && p3 && p4);
    XWidget_resize(p1, 160, 50);
    XWidget_resize(p2, 160, 50);
    XWidget_resize(p3, 160, 50);
    XWidget_resize(p4, 160, 50);
    XWidget_setGeometry((XWidget*)box, 0, 0, 160, 220);
    g_xtbChanged = 0;
    g_xtbLast = -99;
    xtb_hook(box);

    /* 空工具箱（qtoolbox.h:48-52 文档：count=0、currentIndex=-1）。 */
    XQ_EQ_I64(XToolBox_count(box), 0);
    XQ_EQ_I64(XToolBox_currentIndex(box), -1);
    XQ_CHECK(XToolBox_currentWidget(box) == NULL);
    XQ_CHECK(XToolBox_widget(box, 0) == NULL);
    XQ_EQ_I64(XToolBox_indexOf(box, p1), -1);
    XQ_EQ_STR(XToolBox_itemText(box, 0), "");
    XQ_CHECK(!XToolBox_isItemEnabled(box, 0));

    /* 首页加入 ⇒ 索引 0 且成为当前页并发 currentChanged(0)
     * （qtoolbox.cpp:337-339）。 */
    XQ_EQ_I64(XToolBox_addItem(box, p1, "一"), 0);
    XQ_EQ_I64(XToolBox_currentIndex(box), 0);
    XQ_CHECK(XToolBox_currentWidget(box) == p1);
    XQ_EQ_I64(g_xtbChanged, 1);
    XQ_EQ_I64(g_xtbLast, 0);
    /* 追加不改当前页、不补发信号（qtoolbox.cpp:340-352）。 */
    XQ_EQ_I64(XToolBox_addItem(box, p2, "二"), 1);
    XQ_EQ_I64(XToolBox_currentIndex(box), 0);
    XQ_EQ_I64(g_xtbChanged, 1);

    /* 越界索引一律追加：insertItem(99) 与 addItem 系列（index=-1）同口径
     * （qtoolbox.h:77-83 addItem 即 insertItem(-1, ...)）。 */
    XQ_EQ_I64(XToolBox_insertItem(box, 99, p3, "三"), 2);
    XQ_EQ_I64(XToolBox_insertItem(box, -1, p4, "四"), 3);
    XQ_EQ_I64(XToolBox_count(box), 4);
    XQ_CHECK(XToolBox_widget(box, 99) == NULL); /* 越界返回 NULL */
    XQ_EQ_I64(XToolBox_indexOf(box, p4), 3);
    XQ_EQ_STR(XToolBox_itemText(box, 3), "四");

    /* 文本往返 + 条目启用往返（禁用非当前条目不影响当前页）。 */
    XToolBox_setItemText(box, 0, "壹");
    XQ_EQ_STR(XToolBox_itemText(box, 0), "壹");
    XToolBox_setItemEnabled(box, 3, false);
    XQ_CHECK(!XToolBox_isItemEnabled(box, 3));
    XQ_EQ_I64(XToolBox_currentIndex(box), 0);
    XQ_EQ_I64(g_xtbChanged, 1);
    /* setCurrentIndex 不拦禁用条目（qtoolbox.cpp:387-403 只拦无效/同页）。 */
    XToolBox_setCurrentIndex(box, 3);
    XQ_EQ_I64(XToolBox_currentIndex(box), 3);
    XQ_EQ_I64(g_xtbChanged, 2);

    /* 越界 setCurrentIndex 为无操作且不发信号。 */
    g_xtbChanged = 0;
    XToolBox_setCurrentIndex(box, 99);
    XToolBox_setCurrentIndex(box, -1);
    XQ_EQ_I64(XToolBox_currentIndex(box), 3);
    XQ_EQ_I64(g_xtbChanged, 0);

    XClassDelete((XClass*)box); /* 页随容器级联释放。 */
}

/* ==================== ② 禁用当前页自动迁移 + removeItem 语义 ============ */

XQ_TEST(XToolBox, disableCurrentAndRemove)
{
    XToolBox* box = XToolBox_create(NULL, 0);
    XWidget* p1;
    XWidget* p2;
    XWidget* p3;

    XQ_REQUIRE(box != NULL);
    p1 = XWidget_create(NULL, 0);
    p2 = XWidget_create(NULL, 0);
    p3 = XWidget_create(NULL, 0);
    XQ_REQUIRE(p1 && p2 && p3);
    XWidget_resize(p1, 160, 50);
    XWidget_resize(p2, 160, 50);
    XWidget_resize(p3, 160, 50);
    XWidget_setGeometry((XWidget*)box, 0, 0, 160, 220);
    g_xtbChanged = 0;
    g_xtbLast = -99;
    xtb_hook(box);
    XQ_EQ_I64(XToolBox_addItem(box, p1, "一"), 0);
    XQ_EQ_I64(XToolBox_addItem(box, p2, "二"), 1);
    XQ_EQ_I64(XToolBox_addItem(box, p3, "三"), 2);

    /* 禁用当前页（index 2 之后无页可下移，回到 1）——qtoolbox.cpp:544-571
     * 的「先向下后向上找最近启用页」。 */
    XToolBox_setCurrentIndex(box, 2);
    g_xtbChanged = 0;
    XToolBox_setItemEnabled(box, 2, false);
    XQ_EQ_I64(XToolBox_currentIndex(box), 1);
    XQ_CHECK(XToolBox_currentWidget(box) == p2);
    XQ_EQ_I64(g_xtbChanged, 1);
    XQ_EQ_I64(g_xtbLast, 1);

    /* removeItem 非当前页：只出表，不迁移当前页、不发 currentChanged。 */
    g_xtbChanged = 0;
    XToolBox_removeItem(box, 0);
    XQ_EQ_I64(XToolBox_count(box), 2);
    XQ_EQ_I64(XToolBox_currentIndex(box), 0);
    XQ_CHECK(XToolBox_currentWidget(box) == p2);
    XQ_EQ_I64(g_xtbChanged, 0);
    /* 被移除的页不销毁、父对象仍是工具箱（Qt `w->setParent(this)`），
     * 且已隐藏（不再参与工具箱绘制）。 */
    XQ_CHECK(XWidget_parentWidget(p1) == (XWidget*)box);
    XQ_CHECK(!XWidget_isVisible(p1));
    /* 移除当前页：新当前页固定落 0 并发 currentChanged(0)
     * （qtoolbox.cpp:441-442 `q->setCurrentIndex(0)`）。 */
    g_xtbChanged = 0;
    XToolBox_removeItem(box, 0);
    XQ_EQ_I64(XToolBox_count(box), 1);
    XQ_EQ_I64(XToolBox_currentIndex(box), 0);
    XQ_CHECK(XToolBox_currentWidget(box) == p3);
    XQ_EQ_I64(g_xtbChanged, 1);
    XQ_EQ_I64(g_xtbLast, 0);
    XQ_CHECK(XWidget_parentWidget(p2) == (XWidget*)box);

    /* 清空：发 currentChanged(-1)（qtoolbox.cpp:435-436）。 */
    g_xtbChanged = 0;
    XToolBox_removeItem(box, 0);
    XQ_EQ_I64(XToolBox_count(box), 0);
    XQ_EQ_I64(XToolBox_currentIndex(box), -1);
    XQ_EQ_I64(g_xtbChanged, 1);
    XQ_EQ_I64(g_xtbLast, -1);
    XQ_CHECK(XToolBox_currentWidget(box) == NULL);

    /* 保护虚槽缺省实现可直接调用（空操作；子类经虚表重载）。 */
    XToolBox_itemInserted(box, 0);
    XToolBox_itemRemoved(box, 0);
    XToolBox_itemInserted_base(box, 1);
    XToolBox_itemRemoved_base(box, 1);

    XClassDelete((XClass*)box); /* 已移除的 p1/p2/p3 仍是子控件，随容器释放。 */
    /* removeItem 的页容器走 deleteLater；冲刷延迟删除让泄漏门看到真实回收。 */
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ③ 每页装在滚动区里（gap #183） ==================== */

XQ_TEST(XToolBox, pageHostedInScrollArea)
{
    XToolBox* box = XToolBox_create(NULL, 0);
    XWidget* page;
    XWidget* host;
    XWidget* viewport;

    XQ_REQUIRE(box != NULL);
    page = XWidget_create(NULL, 0);
    XQ_REQUIRE(page != NULL);
    XWidget_resize(page, 160, 50);
    XWidget_setGeometry((XWidget*)box, 0, 0, 160, 220);
    XQ_EQ_I64(XToolBox_addItem(box, page, "页"), 0);

    /* 对标 qtoolbox.cpp:322-336：每页装在 QScrollArea 里 ⇒ 页的
     * parentWidget() 是滚动区的 viewport，不是工具箱本体；滚动区再挂
     * 工具箱（Qt 本机 6.8 实测 parentClass=QWidget、grandparent=QScrollArea）。 */
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    viewport = XWidget_parentWidget(page);
    XQ_CHECK_MSG(viewport != (XWidget*)box,
                 "页不应直接挂到工具箱（Qt 装在滚动区里）");
    XQ_REQUIRE(viewport != NULL);
    host = XWidget_parentWidget(viewport);
    XQ_REQUIRE(host != NULL);
    XQ_CHECK(XWidget_parentWidget(host) == (XWidget*)box);
    XQ_CHECK(XScrollArea_widget((XScrollArea*)host) == page);
    XQ_CHECK(XAbstractScrollArea_viewport(
                  (const XAbstractScrollArea*)host) == viewport);
    /* 当前页容器对当前条目处于「未显式隐藏」态（工具箱是顶层控件，构造后
     * 尚未 show ⇒ isVisible 恒 false；判据取显式隐藏位，同库内其它用例）。 */
    XQ_CHECK(!XWidget_isHidden(host));
    /* 页随容器伸缩（widgetResizable=true，对标 Qt sv->setWidgetResizable(true)）。 */
    XQ_CHECK(XScrollArea_widgetResizable((XScrollArea*)host));
#else
    XQ_CHECK(XWidget_parentWidget(page) == (XWidget*)box);
    (void)host;
    (void)viewport;
#endif

    /* removeItem：先把页从滚动区摘回、再回收容器——页仍是工具箱子控件
     * （Qt `w->setParent(this)`）且隐藏。 */
    XToolBox_removeItem(box, 0);
    XQ_CHECK(XWidget_parentWidget(page) == (XWidget*)box);
    XQ_CHECK(!XWidget_isVisible(page));
    XQ_CHECK(XToolBox_widget(box, 0) == NULL);

    XClassDelete((XClass*)box);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ④ 条目提示悬停弹出（P0-36） ==================== */

/** @brief 悬停页头弹条目提示、换条目/进内容区/离开收起。
 * @details 命中映射必须与 layout/paint 同源（xtb2_headerIndexAt）：
 *          激活页之前的页头每 22px 一个、激活页之下是内容区（不命中）、
 *          其后页头继续顺排——提示不得挂到错误条目。 */
XQ_TEST(XToolBox, itemToolTipHoverPopup)
{
    XToolBox* box = XToolBox_create(NULL, 0);
    XWidget* p1;
    XWidget* p2;
    XEvent leave;

    XQ_REQUIRE(box != NULL);
    p1 = XWidget_create(NULL, 0);
    p2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(p1 && p2);
    XWidget_resize(p1, 160, 50);
    XWidget_resize(p2, 160, 50);
    XWidget_setGeometry((XWidget*)box, 0, 0, 160, 220);
    XWidget_show((XWidget*)box);
    XQ_EQ_I64(XToolBox_addItem(box, p1, "一"), 0);
    XQ_EQ_I64(XToolBox_addItem(box, p2, "二"), 1);
    XToolBox_setItemToolTip_2(box, 0, "第一条提示");
    XQ_EQ_STR(XToolBox_itemToolTip(box, 0)
                  ? XString_toUtf8(XToolBox_itemToolTip(box, 0)) : NULL,
              "第一条提示");
    XToolTip_hideText();
    XQ_CHECK(xtb_waitToolTipHidden(2000));

    /* 悬停激活页页头（y=5 落在第 0 条页头行）⇒ 弹出且文本正确。 */
    xtb_mouseMove((XWidget*)box, 40, 5);
    XQ_CHECK_MSG(XToolTip_isVisible(), "悬停页头应弹出条目提示（P0-36）");
    XQ_CHECK(xtb_toolTipTextEquals("第一条提示"));

    /* 进入激活页内容区（页头正下方，非页头）⇒ 收起。 */
    xtb_mouseMove((XWidget*)box, 40, 40);
    XQ_CHECK(xtb_waitToolTipHidden(2000));

    /* 再悬停第 0 条 ⇒ 重新弹出（悬停索引已复位为无，命中变化即重弹）。 */
    xtb_mouseMove((XWidget*)box, 40, 5);
    XQ_CHECK_MSG(XToolTip_isVisible(), "离开内容区回到页头应重新弹出提示");
    XQ_CHECK(xtb_toolTipTextEquals("第一条提示"));

    /* 离开工具箱（LEAVE）⇒ 收起并清空文本。 */
    XEvent_init(&leave, XEVENT_TYPE_LEAVE);
    XObject_event_base((XObject*)box, &leave);
    XQ_CHECK(xtb_waitToolTipHidden(2000));
    XQ_CHECK(XToolTip_text() == NULL);

    XClassDelete((XClass*)box);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== ⑤ 页头图标版式（P0-80） ==================== */

/** @brief setItemIcon 的图标位进入绘制版式：带图标时文本右移
 *         「图标宽(16) + 间距(4)」，无图标保持原版式。
 * @details 断言用页头带左段的抓图差异（同 XDialAlignTest 的
 *          XWidget_grab 离屏口径）。图标文件不必真实存在：版式位移由
 *          **路径非空**决定（解码失败只少画图标本身，文本仍右移），
 *          故本断言与测试机是否带该资源文件无关。
 *
 *          【顶层窗口装饰遮蔽 2026-11-12】本控件必须挂到宿主之下（同
 *          XScrollBarAlignTest.pressedButtonDirection 的子控件口径）：
 *          顶层窗口在 XGUI_CSD 自绘标题栏下，XWidget_grab 的完整
 *          paintTree 会把 28px 的 XTitleBar 装饰条带（xcs_drawTitleBar
 *          的 Button 底 + x=6 文本）画到窗口顶部，正好盖住断言采样的
 *          页头条（y 0..21）⇒ 带/不带图标两态抓图被同一装饰覆盖、
 *          diff 恒 0。挂为子控件后 XWindowDecoration_activeFor=0，
 *          装饰不参与绘制，图标位移才进图。此处只改装配方式，不改
 *          断言口径（不删覆盖）。 */
XQ_TEST(XToolBox, itemIconShiftsHeaderText)
{
    XWidget* host = XWidget_create(NULL, 0);
    XToolBox* box = XToolBox_create(host, 0);
    XWidget* page;
    XImage* plain;
    XImage* withIcon;
    XRect band;

    XQ_REQUIRE(host != NULL);
    XQ_REQUIRE(box != NULL);
    page = XWidget_create(NULL, 0);
    XQ_REQUIRE(page != NULL);
    XWidget_resize(host, 160, 220);
    XWidget_resize(page, 160, 50);
    XWidget_setGeometry((XWidget*)box, 0, 0, 160, 220);
    XWidget_show(host);
    XQ_EQ_I64(XToolBox_addItem(box, page, "页"), 0);

    plain = XWidget_grab((const XWidget*)box);
    XQ_REQUIRE(plain != NULL);

    XToolBox_setItemIcon_2(box, 0, "xq-no-such-icon.png");
    XQ_CHECK(XToolBox_itemIcon(box, 0) != NULL);
    withIcon = XWidget_grab((const XWidget*)box);
    XQ_REQUIRE(withIcon != NULL);

    /* 页头带左段（x 0..26、y 0..21）：文本起笔从 x=6 移到 x=26 ⇒ 必有差异。 */
    XRect_init(&band, 0, 0, 26, 22);
    XQ_CHECK_MSG(xtb_countDiff(plain, withIcon, &band) > 0,
                 "带图标时页头文本应右移（图标宽 + 间距）");

    /* 清除图标 ⇒ 版式回到原样（同 plain）。 */
    XToolBox_setItemIcon(box, 0, NULL);
    XQ_CHECK(XToolBox_itemIcon(box, 0) == NULL);
    {
        XImage* restored = XWidget_grab((const XWidget*)box);
        XQ_REQUIRE(restored != NULL);
        XQ_EQ_I64(xtb_countDiff(plain, restored, &band), 0);
        XClassDelete((XClass*)restored);
    }

    XClassDelete((XClass*)withIcon);
    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)box);
    XClassDelete((XClass*)host);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON */
