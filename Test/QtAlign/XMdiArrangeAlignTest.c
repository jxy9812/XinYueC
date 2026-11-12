/**
 * @file       XMdiArrangeAlignTest.c
 * @brief      XMdiArea 图标排布（arrangeIcons）与 XMdiSubWindow 八向边缘
 *             缩放 / 最大化态 / AreaOption 兑现 × Qt 6.8 对齐契约测试。
 * @details    对齐依据：Qt 6.8.3 源码
 *             `src/widgets/widgets/qmdiarea.cpp` 与 `qmdisubwindow.cpp`
 *             （+ 私有头 `qmdisubwindow_p.h`）。关键取证：
 *
 *             **图标排布**——Qt 6.8 **没有**公开的 `QMdiArea::arrangeIcons`
 *             （全 Qt 源码树 grep 0 命中；qmdiarea.h:100-108 的
 *             public Q_SLOTS 只有 tileSemWindows/cascadeSubWindows 等）。
 *             该行为是**私有**的 `QMdiAreaPrivate::arrangeMinimizedSubWindows`
 *             （qmdiarea.cpp:939-943）→ `IconTiler::rearrange`
 *             （qmdiarea.cpp:329-357），由三条路径自动触发：
 *             子窗最小化（_q_processWindowStateChanged 的 windowMinimized
 *             分支 :721-723）、从最小化还原（windowRestored 分支
 *             :727-729）、区域 resize 尾段（:2324-2328）。本域把它做成
 *             显式公共入口
 *             `XMdiArea_arrangeIcons` 并让三条路径都调用它。
 *             IconTiler 原文布局：
 *               `width = qMax(widgets[0]->width(), 1)`
 *               `ncols = qMax(domain.width()/width, 1)`
 *               `nrows = n/ncols + ((n%ncols) ? 1 : 0)`
 *               `x = col*width`，`y = domain.height() - height - row*height`
 *               （**自底边向上**堆行），每格 `QRect({x,y}, widget->size())`
 *               再经 `QStyle::visualRect(ld, domain, r)`。
 *             参与集合（qmdiarea.cpp:905-911）：可见 + `isMinimized()` +
 *             `!isShaded()`，按 `subWindowList(activationOrder)` 排序。
 *             最小化图标尺寸 = `QMdiSubWindowPrivate::iconSize()`
 *             （qmdisubwindow.cpp:2117-2123）=
 *             `QSize(PM_MdiSubWindowMinimizedWidth, titleBarHeight())`。
 *
 *             **八向边缘缩放**——命中区 `getRegion`（qmdisubwindow.cpp:
 *             1531-1596）按 `PM_MdiSubWindowFrameWidth` 取带厚、四角为
 *             L 形（角区减内缩方块）；操作→变更位→光标表
 *             `initOperationMap`（:1013-1024）；几何计算 `setNewGeometry`
 *             （:1151-1213）；光标 `updateCursor`（:1063-1082）。
 *
 *             **最大化**——`setMaximizeMode`（qmdisubwindow.cpp:1324-1400）
 *             记录恢复几何 → 铺满父区 → 状态位在改几何后**重新确保**
 *             （原文注释：「QWidget::setGeometry will reset
 *             Qt::WindowMaximized so we have to update it here.」:1389-1391）。
 *
 *             **AreaOption**——`DontMaximizeSubWindowOnActivation`
 *             （qmdiarea.h:36-38，值 0x1）真兑现：清零时最大化随激活转移
 *             （qmdiarea.cpp:680-682、1019-1024），置位时不动。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XMDIAREA_ON

#include "XMdiArea.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XObject.h"
#include "XClass.h"
#include "XContainer.h"
#include "XVector.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XAbstractScrollArea.h"
#if XCURSOR_ON
#include "XCursor.h"
#endif
#if XSTYLE_ON
#include "XStyle.h"
#include "XStyleOption.h"
#endif

/* ==================== 公共夹具 ==================== */

/** @brief 建区域并给定视口尺寸（子窗挂视口，排布域=视口矩形）。
 * @note  必须 `XWidget_show` 区域：图标排布的收集条件含
 *        `isVisible()`（对标 qmdiarea.cpp:904 的 `!child->isVisible()
 *        ⇒ continue`），而 X 的子控件生效可见以父链可见为前提
 *        （XWidget_effectiveVisible）；不 show 则子窗全部被判不可见、
 *        图标行恒空。 */ 
static XMdiArea* xmarMakeArea(int w, int h)
{
    XMdiArea* area = XMdiArea_create(NULL, 0);
    if (!area) return NULL;
    XWidget_setGeometry((XWidget*)area, 0, 0, w, h);
    XWidget_show((XWidget*)area);
    return area;
}

/** @brief 加一个子窗并给定几何（用普通控件入口 ⇒ 区域建外壳并接管）。 */
static XMdiSubWindow* xmarAddSub(XMdiArea* area, int x, int y, int w, int h)
{
    XMdiSubWindow* sw;
    XWidget* content;
    if (!area) return NULL;
    content = XWidget_create(NULL, 0);
    if (!content) return NULL;
    sw = XMdiArea_addSubWindow(area, content);
    if (!sw) return NULL;
    XWidget_setGeometry((XWidget*)sw, x, y, w, h);
    return sw;
}

/** @brief 向控件直发一次鼠标事件（本地坐标，无全局坐标）。
 * @note  MDI 的移动/缩放会话以**全局**坐标差值算增量，故此助手只用于
 *        「命中判定/光标」类断言；带位移的会话用下面的 xmarMouseGlobal。 */
static void xmarMouse(XWidget* target, XEventType type, int x, int y,
                      XMouseButton button)
{
    XMouseEvent me;
    XPoint pos;
    if (!target) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, button, 0, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

/** @brief 向控件直发一次鼠标事件并注入全局坐标（会话位移用）。 */
static void xmarMouseGlobal(XWidget* target, XEventType type, int x, int y,
                            int gx, int gy, XMouseButton button,
                            XMouseButton buttons)
{
    XMouseEvent me;
    XPoint pos;
    XPoint global;
    if (!target) return;
    XPoint_init(&pos, x, y);
    XPoint_init(&global, gx, gy);
    XMouseEvent_init(&me, type, button, 0, pos);
    XMouseEvent_setGlobalPosition(&me, &global);
    XMouseEvent_setButtons(&me, buttons);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

#if XCURSOR_ON
/** @brief 当前光标形状码（-1 表示未设自定义光标/回落链无光标）。 */
static int xmarCursorShape(const XWidget* w)
{
    XCursor cur;
    int shape;
    if (!w) return -1;
    cur = XWidget_cursor(w);
    shape = (int)XCursor_shape(&cur);
    return shape;
}
#endif /* XCURSOR_ON */

/* ==================== ① 图标排布：单窗贴底 ==================== */

/* Qt IconTiler::rearrange（qmdiarea.cpp:329-357）：
 *   x = col*width = 0；y = domain.height() - height - 0*height ⇒ 贴底边。
 * 图标尺寸 = iconSize() = QSize(PM_MdiSubWindowMinimizedWidth, 标题带高)
 * （qmdisubwindow.cpp:2117-2123）。 */

XQ_TEST(XMdiArea, arrangeIconsSingleWindowSticksToBottom)
{
    XMdiArea* area = xmarMakeArea(600, 400);
    XWidget* viewport;
    XMdiSubWindow* sw;
    XRect geo;
    int pmW;
    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
    XQ_REQUIRE(viewport != NULL);
    sw = xmarAddSub(area, 10, 10, 200, 150);
    XQ_REQUIRE(sw != NULL);

    /* 最小化：尺寸应缩成图标尺寸、位置由条排布收口。 */
    XWidget_showMinimized((XWidget*)sw);
    XQ_CHECK(XWidget_isMinimized((const XWidget*)sw));
    XQ_CHECK(!XMdiSubWindow_isShaded(sw));

    XMdiArea_arrangeIcons(area);

    geo = XWidget_geometry((XWidget*)sw);
    /* ① y 贴视口底边：y + 图标高 == 视口高（Qt:
     *    y = domain.height() - height - row*height，row=0）。 */
    XQ_EQ_I64(geo.y + geo.height, XWidget_height(viewport));
    /* ② x = col*width = 0。 */
    XQ_EQ_I64(geo.x, 0);
    /* ③ 图标宽 = PM_MdiSubWindowMinimizedWidth（本库 XCommonStyle 为
     *    dpiScaled(196)），高 = 标题带 20。 */
#if XSTYLE_ON
    pmW = XStyle_pixelMetric_base(
        XWidget_style((const XWidget*)sw),
        (int)XStylePM_MdiSubWindowMinimizedWidth, NULL);
    XQ_CHECK_MSG(geo.width == pmW, "w=%d pm=%d", geo.width, pmW);
#endif
    XQ_EQ_I64(geo.height, 20);

    /* 幂等：再排一次不变（纯几何计算，无累计偏移）。前置有效性检查
     * （原 `!isMin || isMin` 是恒真空断言，复核意见⑥）：排布只作用于
     * 最小化未折叠窗，重排前必须仍处该形态，幂等比较才有意义。 */
    {
        XRect again;
        XQ_CHECK_MSG(XWidget_isMinimized((const XWidget*)sw) &&
                         !XMdiSubWindow_isShaded(sw),
                     "icon form must persist across arrange");
        XMdiArea_arrangeIcons(area);
        again = XWidget_geometry((XWidget*)sw);
        XQ_EQ_I64(again.x, geo.x);
        XQ_EQ_I64(again.y, geo.y);
        XQ_EQ_I64(again.width, geo.width);
        XQ_EQ_I64(again.height, geo.height);
    }

    XClassDelete((XClass*)area);
}

/* ==================== ② 图标排布：多窗网格（自底向上） ==================== */

/* Qt IconTiler::rearrange：ncols = max(domain.width()/width, 1)；
 * 行自底向上（row 0 最下一行）、列从左到右（col*width）；
 * 超出 ncols 后换到上一行。 */

XQ_TEST(XMdiArea, arrangeIconsBuildsBottomUpGrid)
{
    XMdiArea* area = xmarMakeArea(600, 400);
    XWidget* viewport;
    XMdiSubWindow* windows[4];
    int iconW;
    int iconH;
    int ncols;
    int vh;
    int i;
    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
    XQ_REQUIRE(viewport != NULL);
    vh = XWidget_height(viewport);

    for (i = 0; i < 4; ++i) {
        windows[i] = xmarAddSub(area, 10 + i * 10, 10 + i * 10, 200, 150);
        XQ_REQUIRE(windows[i] != NULL);
        XWidget_showMinimized((XWidget*)windows[i]);
    }
    XMdiArea_arrangeIcons(area);

    iconW = XWidget_width((const XWidget*)windows[0]);
    iconH = XWidget_height((const XWidget*)windows[0]);
    XQ_CHECK_MSG(iconW > 0 && iconH > 0, "icon=%dx%d", iconW, iconH);
    /* ncols = max(600/iconW, 1)。本库 iconW=196 ⇒ 3 列。 */
    ncols = 600 / iconW;
    if (ncols < 1) ncols = 1;
    XQ_CHECK_MSG(ncols >= 1, "ncols=%d iconW=%d", ncols, iconW);

    for (i = 0; i < 4; ++i) {
        XRect geo = XWidget_geometry((XWidget*)windows[i]);
        int row = i / ncols;
        int col = i % ncols;
        /* ① 列坐标 = col * iconW（Qt: x = col*width）。 */
        XQ_CHECK_MSG(geo.x == col * iconW, "i=%d x=%d expect=%d", i, geo.x,
                     col * iconW);
        /* ② **自底向上**：row 0 贴底、每上一行 y 减 iconH
         *    （Qt: y = domain.height() - height - row*height）。 */
        XQ_CHECK_MSG(geo.y == vh - iconH - row * iconH,
                     "i=%d y=%d expect=%d (vh=%d iconH=%d row=%d)", i,
                     geo.y, vh - iconH - row * iconH, vh, iconH, row);
        /* ③ 每格取子窗自身尺寸（不是统一格宽）。 */
        XQ_EQ_I64(geo.height, iconH);
    }
    /* ④ 第二行（i>=ncols）必须**在**第一行之上。 */
    if (ncols < 4) {
        XRect first = XWidget_geometry((XWidget*)windows[0]);
        XRect second = XWidget_geometry((XWidget*)windows[ncols]);
        XQ_CHECK_MSG(second.y < first.y, "row0.y=%d row1.y=%d", first.y,
                     second.y);
        XQ_EQ_I64(second.y, first.y - iconH);
    }

    XClassDelete((XClass*)area);
}

/* ==================== ③ 图标排布：只收最小化且非折叠窗 ==================== */

/* Qt rearrange（qmdiarea.cpp:905-917）：
 *   IconTiler 分支 `if (child->isMinimized() && !child->isShaded()) append`
 *   ——折叠窗（shaded）虽也是 WindowMinimized，但不进图标行；
 *   普通态窗不进图标行（位置不动）。 */

XQ_TEST(XMdiArea, arrangeIconsSkipsNormalAndShaded)
{
    XMdiArea* area = xmarMakeArea(600, 400);
    XMdiSubWindow* normal;
    XMdiSubWindow* shaded;
    XMdiSubWindow* iconic;
    XRect normalBefore;
    XRect shadedBefore;
    XRect iconicBefore;
    XQ_REQUIRE(area != NULL);

    normal = xmarAddSub(area, 20, 30, 220, 160);
    shaded = xmarAddSub(area, 60, 70, 220, 160);
    iconic = xmarAddSub(area, 100, 110, 220, 160);
    XQ_REQUIRE(normal != NULL && shaded != NULL && iconic != NULL);

    /* 折叠：showShaded 走 WindowMinimized，但 isShaded()=true。 */
    XMdiSubWindow_showShaded(shaded);
    XQ_CHECK(XMdiSubWindow_isShaded(shaded));
    XQ_CHECK(XWidget_isMinimized((const XWidget*)shaded));
    { XRect r; r = XWidget_geometry((XWidget*)shaded); shadedBefore = r; }

    /* 最小化（非折叠）：进图标行。 */
    XWidget_showMinimized((XWidget*)iconic);
    XQ_CHECK(!XMdiSubWindow_isShaded(iconic));
    { XRect r; r = XWidget_geometry((XWidget*)iconic); iconicBefore = r; }

    normalBefore = XWidget_geometry((XWidget*)normal);

    XMdiArea_arrangeIcons(area);
    {
        XRect normalAfter = XWidget_geometry((XWidget*)normal);
        XRect shadedAfter = XWidget_geometry((XWidget*)shaded);
        XRect iconicAfter = XWidget_geometry((XWidget*)iconic);
        /* 普通态窗位置不变（Qt 同：不在 widgets 列表里）。 */
        XQ_EQ_I64(normalAfter.x, normalBefore.x);
        XQ_EQ_I64(normalAfter.y, normalBefore.y);
        XQ_EQ_I64(normalAfter.width, normalBefore.width);
        XQ_EQ_I64(normalAfter.height, normalBefore.height);
        /* 折叠窗同样不被挪（Qt: `!child->isShaded()` 过滤掉）。 */
        XQ_EQ_I64(shadedAfter.x, shadedBefore.x);
        XQ_EQ_I64(shadedAfter.y, shadedBefore.y);
        /* 图标窗必须贴视口底边（row=0 自底向上；原「位置变了或宽度
         * 没变」的三选一在 showMinimized 已先缩成图标尺寸后恒真——
         * 复核意见⑥，换成确定性断言）。图标宽保持不变。 */
        {
            XWidget* vp =
                XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
            XQ_REQUIRE(vp != NULL);
            XQ_EQ_I64(iconicAfter.y + iconicAfter.height,
                      XWidget_height(vp));
        }
        XQ_EQ_I64(iconicAfter.width, iconicBefore.width);
        /* 图标窗 x 必为 0（单图标窗、col=0、LTR）。 */
        XQ_EQ_I64(iconicAfter.x, 0);
    }

    XClassDelete((XClass*)area);
}

/* ==================== ④ 图标排布：空列表 / NULL 安全 ==================== */

XQ_TEST(XMdiArea, arrangeIconsEmptyAndNullSafe)
{
    XMdiArea* area = xmarMakeArea(400, 300);
    XMdiSubWindow* sw;
    XRect before;
    XQ_REQUIRE(area != NULL);

    /* 无最小化窗：空排布不崩、不动几何。 */
    sw = xmarAddSub(area, 20, 20, 200, 150);
    XQ_REQUIRE(sw != NULL);
    before = XWidget_geometry((XWidget*)sw);
    XMdiArea_arrangeIcons(area);
    {
        XRect after = XWidget_geometry((XWidget*)sw);
        XQ_EQ_I64(after.x, before.x);
        XQ_EQ_I64(after.y, before.y);
    }
    /* NULL 区域：直接返回，不崩。 */
    XMdiArea_arrangeIcons(NULL);

    XClassDelete((XClass*)area);
}

/* ==================== ⑤ 最小化窗的图标尺寸 ==================== */

/* Qt setMinimizeMode → showShaded → resize(internalMinimumSize)，
 * internalMinimumSize 在最小化分支由 minimumSizeHint() 得 iconSize()
 * （qmdisubwindow.cpp:3469-3471、2117-2123）。
 * 探针：200x150@(50,50) → 196x24@(0,276)（196=PM、24=标题带高 + 边框）。 */

XQ_TEST(XMdiSubWindow, minimizedUsesIconSize)
{
    XMdiArea* area = xmarMakeArea(600, 400);
    XWidget* viewport;
    XMdiSubWindow* sw;
    XRect geo;
    XSize minHint;
    int pmW = 0;
    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
    XQ_REQUIRE(viewport != NULL);
    sw = xmarAddSub(area, 50, 50, 220, 160);
    XQ_REQUIRE(sw != NULL);

    XWidget_showMinimized((XWidget*)sw);
    geo = XWidget_geometry((XWidget*)sw);
#if XSTYLE_ON
    pmW = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sw),
                                  (int)XStylePM_MdiSubWindowMinimizedWidth,
                                  NULL);
    /* ① 宽 = PM_MdiSubWindowMinimizedWidth（Qt iconSize 公式）。 */
    XQ_CHECK_MSG(geo.width == pmW, "w=%d pm=%d", geo.width, pmW);
#endif
    /* ② 高 = 标题带高 20（Qt 为 titleBarHeight()）。 */
    XQ_EQ_I64(geo.height, 20);
    /* ③ 最小提示同样返回 iconSize（Qt :3469-3471 的最先返回分支）。 */
    minHint = XWidget_minimumSizeHint_base((const XWidget*)sw);
    XQ_EQ_I64(minHint.width, geo.width);
    XQ_EQ_I64(minHint.height, geo.height);

    /* 还原：几何回到最小化前的尺寸（Qt setNormalMode 用 restoreSize）。 */
    XWidget_showNormal((XWidget*)sw);
    XQ_CHECK(!XWidget_isMinimized((const XWidget*)sw));
    geo = XWidget_geometry((XWidget*)sw);
    XQ_EQ_I64(geo.width, 220);
    XQ_EQ_I64(geo.height, 160);

    XClassDelete((XClass*)area);
}

/* ==================== ⑥ 八向命中区与光标 ==================== */

/* Qt getRegion（qmdisubwindow.cpp:1531-1596）+ initOperationMap
 * （:1013-1024）：
 *   上/下边 ⇒ SizeVerCursor；左/右边 ⇒ SizeHorCursor；
 *   左上/右下 ⇒ SizeFDiagCursor；右上/左下 ⇒ SizeBDiagCursor。
 * 四角是 L 形（角区减内缩方块），故「角区中心靠内的那块」不是角。
 * 本库框宽 1px（见 XMdiArea.c 的 XMDI_SUBWINDOW_FRAME_WIDTH 注）。 */

XQ_TEST(XMdiSubWindow, edgeHitRegionsMatchQt)
{
    XMdiArea* area = xmarMakeArea(600, 400);
    XMdiSubWindow* sw;
    int w;
    int h;
    XQ_REQUIRE(area != NULL);
    sw = xmarAddSub(area, 40, 40, 220, 160);
    XQ_REQUIRE(sw != NULL);
    w = XWidget_width((const XWidget*)sw);
    h = XWidget_height((const XWidget*)sw);

#if XCURSOR_ON
    /* 上边（x 在 [tb, w-tb) 且 y < fw=1）⇒ SizeVer。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w / 2, 0,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_SizeVer);
    /* 下边 ⇒ SizeVer。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w / 2, h - 1,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_SizeVer);
    /* 左边（y 在 [tb, h-tb) 且 x < fw）⇒ SizeHor。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, 0, h / 2,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_SizeHor);
    /* 右边 ⇒ SizeHor。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w - 1, h / 2,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_SizeHor);
    /* 左上角（L 形带）⇒ SizeFDiag（Qt: TopLeftResize→SizeFDiag）。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, 0, 0,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_SizeFDiag);
    /* 右下角 ⇒ SizeFDiag。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w - 1, h - 1,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_SizeFDiag);
    /* 右上角 ⇒ SizeBDiag（Qt: TopRightResize→SizeBDiag）。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w - 1, 0,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_SizeBDiag);
    /* 左下角 ⇒ SizeBDiag。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, 0, h - 1,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_SizeBDiag);
    /* 标题带内 ⇒ 箭头（Move 操作，Qt cursorShape=ArrowCursor）。 */
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w / 2, 10,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_Arrow);
#else
    XQ_CHECK(true); /* XCURSOR_ON=0：无光标可断言。 */
#endif /* XCURSOR_ON */

    /* 四角 L 形判定：角区内缩方块（tb-fw）×（tb-fw）不算角。
     * 本库 tb=20、fw=1 ⇒ 内缩方块从 (1,1) 起 19×19；点 (10,10) 在方块
     * 内且落在标题带 ⇒ Move（不是 TopLeftResize）。用光标再验一次。 */
#if XCURSOR_ON
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, 10, 10,
              XMouseButton_NoButton);
    XQ_EQ_I64(xmarCursorShape((XWidget*)sw), XCursor_Arrow);
#endif

    XClassDelete((XClass*)area);
}

/* ==================== ⑦ 八向缩放会话：右/下边改尺寸 ==================== */

/* Qt setNewGeometry（qmdisubwindow.cpp:1204-1210）：
 *   右边（HResize 无 reverse）：newW = oldW + (posX - mousePressPosition.x)；
 *   下边（VResize 无 reverse）：newH = oldH + (posY - mousePressPosition.y)。
 * 再经 setNewGeometry(QRect*) 的 size.expandedTo(internalMinimumSize)。 */

XQ_TEST(XMdiSubWindow, rightBottomEdgeResizeGrowsWindow)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XMdiSubWindow* sw;
    int w0;
    int h0;
    XRect geo;
    XQ_REQUIRE(area != NULL);
    sw = xmarAddSub(area, 40, 40, 220, 160);
    XQ_REQUIRE(sw != NULL);
    w0 = XWidget_width((const XWidget*)sw);
    h0 = XWidget_height((const XWidget*)sw);

    /* 按右边中段（本地 x=w-1、y=h/2），全局基准 (1000,1000)。 */
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_PRESS, w0 - 1,
                    h0 / 2, 1000, 1000, XMouseButton_LeftButton,
                    XMouseButton_LeftButton);
    /* 右移 +40、下移 +30。 */
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w0 - 1, h0 / 2,
                    1040, 1030, XMouseButton_NoButton,
                    XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, w0 - 1,
                    h0 / 2, 1040, 1030, XMouseButton_LeftButton,
                    XMouseButton_NoButton);

    geo = XWidget_geometry((XWidget*)sw);
    /* 右边缩放只改宽度、不改 x（右边没有 HMove 位）。 */
    XQ_EQ_I64(geo.x, 40);
    XQ_CHECK_MSG(geo.width == w0 + 40, "w=%d expect=%d", geo.width, w0 + 40);
    /* 右边没有 VResize 位 ⇒ 高度不变。 */
    XQ_EQ_I64(geo.height, h0);

    /* 下边缩放只改高度、不改 y。 */
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_PRESS, w0 / 2,
                    geo.height - 1, 2000, 2000, XMouseButton_LeftButton,
                    XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w0 / 2,
                    geo.height - 1, 2000, 2030, XMouseButton_NoButton,
                    XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, w0 / 2,
                    geo.height - 1, 2000, 2030, XMouseButton_LeftButton,
                    XMouseButton_NoButton);
    geo = XWidget_geometry((XWidget*)sw);
    XQ_EQ_I64(geo.y, 40);
    XQ_CHECK_MSG(geo.height == h0 + 30, "h=%d expect=%d", geo.height,
                 h0 + 30);

    XClassDelete((XClass*)area);
}

/* ==================== ⑧ 八向缩放：左上角 ⇄ 原点与尺寸反向 ==================== */

/* Qt initOperationMap（qmdisubwindow.cpp:1018-1019）：TopLeftResize 带
 * HMove|VMove|HResize|VResize|HResizeReverse|VResizeReverse
 * ⇒ 拖向右下时原点跟进（dx>0）、尺寸反向缩小（-dx）。 */

XQ_TEST(XMdiSubWindow, topLeftCornerResizeMovesOriginAndShrinks)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XMdiSubWindow* sw;
    int x0;
    int y0;
    int w0;
    int h0;
    XRect geo;
    XQ_REQUIRE(area != NULL);
    sw = xmarAddSub(area, 100, 100, 240, 180);
    XQ_REQUIRE(sw != NULL);
    x0 = XWidget_x((XWidget*)sw);
    y0 = XWidget_y((XWidget*)sw);
    w0 = XWidget_width((const XWidget*)sw);
    h0 = XWidget_height((const XWidget*)sw);

    /* 按左上角 (0,0)，向右下拖 (+20,+15) ⇒ 原点 +20/+15、尺寸 -20/-15。 */
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 0, 0,
                    3000, 3000, XMouseButton_LeftButton,
                    XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, 0, 0, 3020, 3015,
                    XMouseButton_NoButton, XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 0, 0,
                    3020, 3015, XMouseButton_LeftButton,
                    XMouseButton_NoButton);

    geo = XWidget_geometry((XWidget*)sw);
    XQ_CHECK_MSG(geo.x == x0 + 20, "x=%d expect=%d", geo.x, x0 + 20);
    XQ_CHECK_MSG(geo.y == y0 + 15, "y=%d expect=%d", geo.y, y0 + 15);
    XQ_CHECK_MSG(geo.width == w0 - 20, "w=%d expect=%d", geo.width, w0 - 20);
    XQ_CHECK_MSG(geo.height == h0 - 15, "h=%d expect=%d", geo.height,
                 h0 - 15);

    XClassDelete((XClass*)area);
}

/* ==================== ⑨ 缩放受最小尺寸钳制 ==================== */

/* Qt setNewGeometry(QRect*)（qmdisubwindow_private.h:278-285）：
 *   `geometry->setSize(geometry->size().expandedTo(internalMinimumSize));`
 * internalMinimumSize 在有父时 = minimumSizeHint()（qmdisubwindow.cpp:
 * 1105-1107）。本库最小提示含 chrome 最小宽 50 + 内容最小尺寸。 */

XQ_TEST(XMdiSubWindow, resizeClampedToMinimumSizeHint)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XMdiSubWindow* sw;
    XSize minHint;
    XRect geo;
    XQ_REQUIRE(area != NULL);
    sw = xmarAddSub(area, 100, 100, 240, 180);
    XQ_REQUIRE(sw != NULL);
    minHint = XWidget_minimumSizeHint_base((const XWidget*)sw);
    XQ_CHECK_MSG(minHint.width > 0 && minHint.height > 0, "min=%dx%d",
                 minHint.width, minHint.height);

    /* 右下角往左上狠拖 (-500,-500)：尺寸不得小于最小提示。 */
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 239, 179,
                    4000, 4000, XMouseButton_LeftButton,
                    XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, 239, 179, 3500,
                    3500, XMouseButton_NoButton, XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 239, 179,
                    3500, 3500, XMouseButton_LeftButton,
                    XMouseButton_NoButton);

    geo = XWidget_geometry((XWidget*)sw);
    XQ_CHECK_MSG(geo.width >= minHint.width, "w=%d min=%d", geo.width,
                 minHint.width);
    XQ_CHECK_MSG(geo.height >= minHint.height, "h=%d min=%d", geo.height,
                 minHint.height);

    XClassDelete((XClass*)area);
}

/* ==================== ⑩ 标题带拖动仍只改位置 ==================== */

/* 回归锁：把拖动会话改成统一移动/缩放会话后，Move 操作不得顺带改尺寸
 * （Qt Move 的 changeFlags 只有 HMove|VMove，无 HResize/VResize）。 */

XQ_TEST(XMdiSubWindow, titleBarDragKeepsSize)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XMdiSubWindow* sw;
    int w0;
    int h0;
    XRect geo;
    XQ_REQUIRE(area != NULL);
    sw = xmarAddSub(area, 60, 60, 220, 160);
    XQ_REQUIRE(sw != NULL);
    w0 = XWidget_width((const XWidget*)sw);
    h0 = XWidget_height((const XWidget*)sw);

    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                    w0 / 2, 10, 5000, 5000, XMouseButton_LeftButton,
                    XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, w0 / 2, 10, 5040,
                    5030, XMouseButton_NoButton, XMouseButton_LeftButton);
    xmarMouseGlobal((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, w0 / 2,
                    10, 5040, 5030, XMouseButton_LeftButton,
                    XMouseButton_NoButton);

    geo = XWidget_geometry((XWidget*)sw);
    XQ_EQ_I64(geo.width, w0);
    XQ_EQ_I64(geo.height, h0);
    XQ_EQ_I64(geo.x, 60 + 40);
    XQ_EQ_I64(geo.y, 60 + 30);

    XClassDelete((XClass*)area);
}

/* ==================== ⑪ 最大化 / 还原 round-trip ==================== */

/* Qt setMaximizeMode（qmdisubwindow.cpp:1324-1400）：铺满父区
 * （setNewGeometry(parent->contentsRect())），并在改几何后**重新确保**
 * Maximized 状态位（:1389-1391 原文注释：setGeometry 会清掉该位）。
 * setNormalMode 用记录的 restoreSize/oldGeometry 还原。 */

XQ_TEST(XMdiSubWindow, maximizeRestoreRoundTrip)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XWidget* viewport;
    XMdiSubWindow* sw;
    XRect before;
    XRect maxGeo;
    XRect restored;
    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
    XQ_REQUIRE(viewport != NULL);
    sw = xmarAddSub(area, 60, 70, 220, 160);
    XQ_REQUIRE(sw != NULL);
    before = XWidget_geometry((XWidget*)sw);

    XWidget_showMaximized((XWidget*)sw);
    /* ① 状态位在改几何之后仍为 Maximized（Qt 特意补的一步）。 */
    XQ_CHECK_MSG(XWidget_isMaximized((const XWidget*)sw),
                 "state=0x%x", (unsigned)XWidget_windowState((const XWidget*)sw));
    maxGeo = XWidget_geometry((XWidget*)sw);
    /* ② 铺满视口。 */
    XQ_EQ_I64(maxGeo.x, 0);
    XQ_EQ_I64(maxGeo.y, 0);
    XQ_EQ_I64(maxGeo.width, XWidget_width(viewport));
    XQ_EQ_I64(maxGeo.height, XWidget_height(viewport));

    /* ③ 最大化期间边缘命中被禁（Qt resizeEnabled/moveEnabled=false）：
     *    右上角不设 SizeBDiag 光标。 */
#if XCURSOR_ON
    xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, maxGeo.width - 1, 0,
              XMouseButton_NoButton);
    XQ_CHECK_MSG(xmarCursorShape((XWidget*)sw) != XCursor_SizeBDiag,
                 "maximized 仍给缩放光标 shape=%d",
                 xmarCursorShape((XWidget*)sw));
#endif

    XWidget_showNormal((XWidget*)sw);
    XQ_CHECK(!XWidget_isMaximized((const XWidget*)sw));
    restored = XWidget_geometry((XWidget*)sw);
    /* ④ 还原几何与最大化前一致（Qt restoreSize/oldGeometry）。 */
    XQ_CHECK_MSG(restored.x == before.x, "x=%d before=%d", restored.x,
                 before.x);
    XQ_CHECK_MSG(restored.y == before.y, "y=%d before=%d", restored.y,
                 before.y);
    XQ_CHECK_MSG(restored.width == before.width, "w=%d before=%d",
                 restored.width, before.width);
    XQ_CHECK_MSG(restored.height == before.height, "h=%d before=%d",
                 restored.height, before.height);

    XClassDelete((XClass*)area);
}

/* ==================== ⑪b 最大化状态随父区重铺保持（复核意见①回归锁）== */

/* 复核意见①：父区 resizeEvent 的最大化重铺分支走 setGeometry，而 X 的
 * setGeometry 清 Maximized/FullScreen 位（XWidget.c:6757-6762）——缺
 * ensureWindowState 补位则之后 showNormal 因「新旧态相同」早退
 * （XWidget.c:8995）⇒ changeEvent 不派发 ⇒ 还原几何永不执行（窗口卡死
 * 最大化）。Qt 在 QMdiSubWindow::resizeEvent 尾段补
 * `if (d->isMaximizeMode) d->ensureWindowState(Qt::WindowMaximized);`
 * （qmdisubwindow.cpp:3041-3042，overrideWindowState 不派发事件）。 */

XQ_TEST(XMdiSubWindow, maximizedStateSurvivesAreaResize)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XWidget* viewport;
    XMdiSubWindow* sw;
    XRect before;
    XRect refit;
    XRect restored;
    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
    XQ_REQUIRE(viewport != NULL);
    sw = xmarAddSub(area, 30, 40, 240, 180);
    XQ_REQUIRE(sw != NULL);
    before = XWidget_geometry((XWidget*)sw);

    XWidget_showMaximized((XWidget*)sw);
    XQ_CHECK(XWidget_isMaximized((const XWidget*)sw));

    /* 父区放大：重铺分支把最大化子窗 setGeometry 成新视口尺寸。 */
    XWidget_setGeometry((XWidget*)area, 0, 0, 900, 620);
    /* ① 状态位必须在重铺后仍然保留（此前被 setGeometry 清掉）。 */
    XQ_CHECK_MSG(XWidget_isMaximized((const XWidget*)sw),
                 "state lost after area resize: 0x%x",
                 (unsigned)XWidget_windowState((const XWidget*)sw));
    refit = XWidget_geometry((XWidget*)sw);
    /* ② 几何已随新视口重铺。 */
    XQ_EQ_I64(refit.width, XWidget_width(viewport));
    XQ_EQ_I64(refit.height, XWidget_height(viewport));

    /* ③ 关键回归点：重铺后的最大化窗仍能还原（此前 showNormal 早退、
     *    窗口永久卡死在最大化尺寸）。 */
    XWidget_showNormal((XWidget*)sw);
    XQ_CHECK(!XWidget_isMaximized((const XWidget*)sw));
    restored = XWidget_geometry((XWidget*)sw);
    XQ_CHECK_MSG(restored.x == before.x, "x=%d before=%d", restored.x,
                 before.x);
    XQ_CHECK_MSG(restored.y == before.y, "y=%d before=%d", restored.y,
                 before.y);
    XQ_CHECK_MSG(restored.width == before.width, "w=%d before=%d",
                 restored.width, before.width);
    XQ_CHECK_MSG(restored.height == before.height, "h=%d before=%d",
                 restored.height, before.height);

    XClassDelete((XClass*)area);
}

/* ==================== ⑫ AreaOption：最大化随激活转移 ==================== */

/* Qt qmdiarea.cpp:680-682（_q_deactivateAllWindows 捕获）+ :1019-1024
 * （emitWindowActivated 兑现）：
 *   选项清零（缺省）⇒ 旧最大化窗 showNormal、新激活窗 showMaximized；
 *   选项置位 DontMaximizeSubWindowOnActivation ⇒ 两者都不动
 *   （:679 `if (!(options & ...) && !showActiveWindowMaximized)`）。 */

XQ_TEST(XMdiArea, dontMaximizeOptionIsHonoured)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XMdiSubWindow* a;
    XMdiSubWindow* b;
    XQ_REQUIRE(area != NULL);
    a = xmarAddSub(area, 40, 40, 220, 160);
    b = xmarAddSub(area, 80, 80, 220, 160);
    XQ_REQUIRE(a != NULL && b != NULL);

    /* --- 缺省（选项清零）：最大化随激活转移 --- */
    XMdiArea_setOption(area,
                       XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation,
                       false);
    XMdiArea_setActiveSubWindow(area, a);
    XWidget_showMaximized((XWidget*)a);
    XQ_CHECK(XWidget_isMaximized((const XWidget*)a));

    XMdiArea_setActiveSubWindow(area, b);
    /* 旧窗还原、新窗最大化（Qt showActiveWindowMaximized 语义）。 */
    XQ_CHECK_MSG(!XWidget_isMaximized((const XWidget*)a),
                 "缺省口径下旧最大化窗应 showNormal");
    XQ_CHECK_MSG(XWidget_isMaximized((const XWidget*)b),
                 "缺省口径下新激活窗应 showMaximized");

    /* --- 置位：激活不动最大化态 --- */
    XWidget_showNormal((XWidget*)b);
    XMdiArea_setOption(area,
                       XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation,
                       true);
    XMdiArea_setActiveSubWindow(area, a);
    XWidget_showMaximized((XWidget*)a);
    XQ_CHECK(XWidget_isMaximized((const XWidget*)a));

    XMdiArea_setActiveSubWindow(area, b);
    XQ_CHECK_MSG(XWidget_isMaximized((const XWidget*)a),
                 "置位后旧最大化窗必须保持最大化");
    XQ_CHECK_MSG(!XWidget_isMaximized((const XWidget*)b),
                 "置位后新激活窗不得被最大化");

    XMdiArea_setOption(area,
                       XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation,
                       false);
    XClassDelete((XClass*)area);
}

/* ==================== ⑬ 最小化 ⇒ 自动进图标行（区域 resize 路径） ==================== */

/* Qt resizeEvent 尾段（qmdiarea.cpp:2324-2328）：
 *   非平铺、无最大化窗 ⇒ `d->arrangeMinimizedSubWindows();`
 * 故区域改尺寸后图标行必须重排到新底边。 */

XQ_TEST(XMdiArea, resizeReArrangesIcons)
{
    XMdiArea* area = xmarMakeArea(600, 400);
    XWidget* viewport;
    XMdiSubWindow* sw;
    XRect geo;
    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
    XQ_REQUIRE(viewport != NULL);
    sw = xmarAddSub(area, 30, 30, 220, 160);
    XQ_REQUIRE(sw != NULL);
    XWidget_showMinimized((XWidget*)sw);
    XMdiArea_arrangeIcons(area);

    /* 区域增高 ⇒ 图标行贴新底边。 */
    XWidget_setGeometry((XWidget*)area, 0, 0, 600, 500);
    geo = XWidget_geometry((XWidget*)sw);
    XQ_CHECK_MSG(geo.y + geo.height == XWidget_height(viewport),
                 "y+h=%d vh=%d", geo.y + geo.height,
                 XWidget_height(viewport));

    XClassDelete((XClass*)area);
}

/* ==================== ⑬b 视觉矩形方向取子窗自身（复核返工锁） ========== */

/* Qt IconTiler::rearrange（qmdiarea.cpp:349）：
 *   `widget->setGeometry(QStyle::visualRect(widget->layoutDirection(),
 *    domain, newGeometry));` —— 方向取**每个子窗自身**的
 * layoutDirection（子窗未显式设置时继承区域，显式设置后该窗单独镜像）。
 * 此前实现取**区域**方向 ⇒ RTL 子窗的图标格不被镜像。RTL 镜像公式
 * （visualRect，direction=1）：x' = domain.width - x - width。 */

XQ_TEST(XMdiArea, arrangeIconsFollowsWindowLayoutDirection)
{
    XMdiArea* area = xmarMakeArea(600, 400);
    XWidget* viewport;
    XMdiSubWindow* ltr;
    XMdiSubWindow* rtl;
    int iconW;
    XRect geoLtr;
    XRect geoRtl;
    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
    XQ_REQUIRE(viewport != NULL);
    ltr = xmarAddSub(area, 10, 10, 200, 150);
    rtl = xmarAddSub(area, 40, 40, 200, 150);
    XQ_REQUIRE(ltr != NULL && rtl != NULL);
    XWidget_showMinimized((XWidget*)ltr);
    XWidget_showMinimized((XWidget*)rtl);
    /* 第二窗显式 RTL（对标子窗单独 setLayoutDirection）。 */
    XWidget_setLayoutDirection((XWidget*)rtl,
                               XWidgetLayoutDirection_RightToLeft);

    XMdiArea_arrangeIcons(area);

    iconW = XWidget_width((const XWidget*)ltr);
    XQ_CHECK_MSG(iconW > 0, "iconW=%d", iconW);
    /* 激活序 = 创建序：ltr 占 col 0（x = 0*iconW）、rtl 占 col 1
     * （逻辑 x = 1*iconW）。 */
    geoLtr = XWidget_geometry((XWidget*)ltr);
    geoRtl = XWidget_geometry((XWidget*)rtl);
    /* ① LTR 窗不镜像：x = col*width = 0。 */
    XQ_EQ_I64(geoLtr.x, 0);
    /* ② RTL 窗按自身方向镜像：x' = vw - iconW - iconW。 */
    XQ_CHECK_MSG(geoRtl.x == XWidget_width(viewport) - iconW - iconW,
                 "rtl.x=%d expect=%d (vw=%d iconW=%d)", geoRtl.x,
                 XWidget_width(viewport) - iconW - iconW,
                 XWidget_width(viewport), iconW);
    /* ③ 镜像只换 x：y/尺寸与 LTR 窗同口径（自底向上 row 0）。 */
    XQ_EQ_I64(geoRtl.y, geoLtr.y);
    XQ_EQ_I64(geoRtl.width, iconW);
    /* ④ RTL 窗的镜像位在域内（不越出右缘）。 */
    XQ_CHECK_MSG(geoRtl.x >= 0 && geoRtl.x + geoRtl.width <=
                                    XWidget_width(viewport),
                 "rtl rect out of domain: x=%d w=%d", geoRtl.x, geoRtl.width);

    XClassDelete((XClass*)area);
}

/* ==================== ⑭ 键盘交互模式（系统菜单 &Move / &Size） ========= */

/* Qt QMdiSubWindowPrivate::_q_enterInteractiveMode（qmdisubwindow.cpp:905-945）
 * + QMdiSubWindow::keyPressEvent（:3316-3375）：
 *   - &Move ⇒ currentOperation=Move、pressPos=(w/2, titleBarHeight-1)；
 *   - &Size ⇒ currentOperation=BottomRightResize（LTR）/
 *     BottomLeftResize（RTL）；
 *   - 方向键按 Shift 取 keyboardPageStep、否则 keyboardSingleStep
 *     （缺省 5 / 20）；
 *   - Esc / Enter / Return ⇒ leaveInteractiveMode（后续方向键不再动）。
 */

/** @brief 向控件直发一次按键。 */
static void xmarKey(XWidget* target, int key, XKeyboardModifiers mods)
{
    XKeyEvent ke;
    if (!target) return;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, mods);
    XObject_event_base((XObject*)target, (XEvent*)&ke);
}

/** @brief 取动作文本的 UTF-8 借用视图（无文本返回空串）。
 * @note  `XAction_text` 返回**新建副本**（须释放），`XAction_text_const`
 *        是借用版——这里用后者 + `XString_toUtf8`（XString 是内码缓冲，
 *        取 UTF-8 必须转换）。 */
static const char* xmarActionText(const XAction* action)
{
    const XString* text;
    const char* utf8;
    if (!action) return "";
    text = XAction_text_const(action);
    if (!text) return "";
    utf8 = XString_toUtf8(text);
    return utf8 ? utf8 : "";
}

/** @brief 按条目文本在系统菜单里找动作（借用；未找到 NULL）。 */
#if XMENU_ON
static XAction* xmarFindMenuAction(XMdiSubWindow* sw, const char* utf8)
{
    XMenu* menu;
    const XVector* actions;
    int64_t i;
    int64_t n;
    if (!sw) return NULL;
    menu = XMdiSubWindow_systemMenu(sw);
    if (!menu) return NULL;
    actions = XMenu_actions(menu);
    n = actions ? (int64_t)XVector_size_base((const XContainer*)actions) : 0;
    for (i = 0; i < n; ++i) {
        XAction* a = XVector_At_Base((XVector*)actions, i, XAction*);
        const char* text;
        if (!a) continue;
        text = xmarActionText(a);
        if (text && strcmp(text, utf8) == 0) return a;
    }
    return NULL;
}
#endif /* XMENU_ON */

XQ_TEST(XMdiSubWindow, interactiveMoveAndSizeByKeyboard)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XMdiSubWindow* sw;
    int x0;
    int y0;
    int w0;
    int h0;
    XRect geo;
    int step;
    XQ_REQUIRE(area != NULL);
    sw = xmarAddSub(area, 100, 100, 240, 180);
    XQ_REQUIRE(sw != NULL);
    x0 = XWidget_x((XWidget*)sw);
    y0 = XWidget_y((XWidget*)sw);
    w0 = XWidget_width((const XWidget*)sw);
    h0 = XWidget_height((const XWidget*)sw);

#if XMENU_ON
    /* ① 系统菜单含 &Move / &Size（qmdisubwindow.cpp:1038-1039 的
     *    addToSystemMenu 顺序，紧跟 &Restore 之后）。 */
    {
        XAction* moveAction = xmarFindMenuAction(sw, "&Move");
        XAction* sizeAction = xmarFindMenuAction(sw, "&Size");
        XQ_CHECK_MSG(moveAction != NULL, "系统菜单缺 &Move");
        XQ_CHECK_MSG(sizeAction != NULL, "系统菜单缺 &Size");

        /* ② &Move 下单步右移：keyboardSingleStep 缺省 5。 */
        if (moveAction) {
            XAction_trigger(moveAction);
            step = XMdiSubWindow_keyboardSingleStep(sw);
            XQ_EQ_I64(step, 5);
            xmarKey((XWidget*)sw, XKey_Right, 0);
            geo = XWidget_geometry((XWidget*)sw);
            XQ_CHECK_MSG(geo.x == x0 + step, "x=%d expect=%d", geo.x,
                         x0 + step);
            /* 尺寸不变（Move 无 resize 位）。 */
            XQ_EQ_I64(geo.width, w0);
            XQ_EQ_I64(geo.height, h0);
            /* ③ Shift+右 = pageStep（缺省 20）。 */
            xmarKey((XWidget*)sw, XKey_Right,
                    XKeyboardModifier_ShiftModifier);
            geo = XWidget_geometry((XWidget*)sw);
            XQ_CHECK_MSG(geo.x == x0 + step + XMdiSubWindow_keyboardPageStep(sw),
                         "x=%d expect=%d", geo.x,
                         x0 + step + XMdiSubWindow_keyboardPageStep(sw));
            /* ④ 上移单步（dy 负）。 */
            x0 = geo.x;
            y0 = geo.y;
            xmarKey((XWidget*)sw, XKey_Up, 0);
            geo = XWidget_geometry((XWidget*)sw);
            XQ_CHECK_MSG(geo.y == y0 - step, "y=%d expect=%d", geo.y,
                         y0 - step);
            XQ_EQ_I64(geo.x, x0);
            /* ⑤ Esc 退出：后续方向键不再移动。 */
            xmarKey((XWidget*)sw, XKey_Escape, 0);
            x0 = geo.x;
            y0 = geo.y;
            xmarKey((XWidget*)sw, XKey_Right, 0);
            xmarKey((XWidget*)sw, XKey_Down, 0);
            geo = XWidget_geometry((XWidget*)sw);
            XQ_CHECK_MSG(geo.x == x0 && geo.y == y0,
                         "Esc 后仍移动：(%d,%d) 应 (%d,%d)", geo.x, geo.y,
                         x0, y0);
        }

        /* ⑥ &Size 下单步右下缩放：宽度/高度各 +step。 */
        if (sizeAction) {
            x0 = XWidget_x((XWidget*)sw);
            y0 = XWidget_y((XWidget*)sw);
            w0 = XWidget_width((const XWidget*)sw);
            h0 = XWidget_height((const XWidget*)sw);
            XAction_trigger(sizeAction);
            step = XMdiSubWindow_keyboardSingleStep(sw);
            xmarKey((XWidget*)sw, XKey_Right, 0);
            geo = XWidget_geometry((XWidget*)sw);
            XQ_CHECK_MSG(geo.width == w0 + step, "w=%d expect=%d", geo.width,
                         w0 + step);
            xmarKey((XWidget*)sw, XKey_Down, 0);
            geo = XWidget_geometry((XWidget*)sw);
            XQ_CHECK_MSG(geo.height == h0 + step, "h=%d expect=%d", geo.height,
                         h0 + step);
            /* 右下角缩放不改原点（无 HMove/VMove 位）。 */
            XQ_EQ_I64(geo.x, x0);
            XQ_EQ_I64(geo.y, y0);
            /* Enter 也退出交互模式（Qt :3350-3353 三个键同分支）。 */
            xmarKey((XWidget*)sw, XKey_Return, 0);
            w0 = XWidget_width((const XWidget*)sw);
            xmarKey((XWidget*)sw, XKey_Right, 0);
            XQ_EQ_I64(XWidget_width((const XWidget*)sw), w0);
        }
    }
#else
    XQ_CHECK(true); /* XMENU_ON=0：无可触发的菜单项。 */
#endif /* XMENU_ON */

    XClassDelete((XClass*)area);
}

/* ==================== ⑭b 交互模式随鼠标按下退出（复核意见②回归锁）=== */

/* Qt mousePressEvent 首段（qmdisubwindow.cpp:3151-3152）：
 * `if (d->isInInteractiveMode) d->leaveInteractiveMode();`。缺了它：
 * 交互中按左键照常建立鼠标会话，release 只清会话不清 isInInteractiveMode
 * ⇒ 下一次 keyPressEvent 拿着陈旧 oldGeometry 计算，窗口跳回旧位。 */

XQ_TEST(XMdiSubWindow, interactiveModeExitsOnMousePress)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XMdiSubWindow* sw;
    XRect geo;
    int x0;
    int y0;
    XQ_REQUIRE(area != NULL);
    sw = xmarAddSub(area, 100, 100, 240, 180);
    XQ_REQUIRE(sw != NULL);
#if XMENU_ON
    {
        XAction* moveAction = xmarFindMenuAction(sw, "&Move");
        XQ_REQUIRE(moveAction != NULL);
        /* ① 进入交互模式并走一步（x: 100 → 105）。 */
        XAction_trigger(moveAction);
        xmarKey((XWidget*)sw, XKey_Right, 0);
        geo = XWidget_geometry((XWidget*)sw);
        XQ_EQ_I64(geo.x, 105);
        XQ_EQ_I64(geo.y, 100);

        /* ② 标题带内按下再释放（无位移的拖拽会话）：交互模式随之退出
         *    （修前 release 只清鼠标会话，isInInteractiveMode 残留）。 */
        xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 60, 10,
                  XMouseButton_LeftButton);
        xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 60, 10,
                  XMouseButton_LeftButton);

        /* ③ 之后方向键不再驱动窗口（判定性表现）。修前这里拿陈旧
         *    m_startGeometry 重算，窗口跳回 x=100。 */
        x0 = XWidget_x((XWidget*)sw);
        y0 = XWidget_y((XWidget*)sw);
        xmarKey((XWidget*)sw, XKey_Right, 0);
        xmarKey((XWidget*)sw, XKey_Down, 0);
        geo = XWidget_geometry((XWidget*)sw);
        XQ_CHECK_MSG(geo.x == x0 && geo.y == y0,
                     "按下后交互模式未退出：(%d,%d) 应 (%d,%d)", geo.x,
                     geo.y, x0, y0);
    }
#else
    XQ_CHECK(true); /* XMENU_ON=0：无可触发的菜单项。 */
#endif /* XMENU_ON */

    XClassDelete((XClass*)area);
}

/* ============ ⑭b 键盘交互模式中物理 MOVE 不重算光标（复核返工锁） ====== */

/* Qt mouseMoveEvent（qmdisubwindow.cpp:3295-3301）：
 *   `if ((mouseEvent->buttons() & Qt::LeftButton) ||
 *       d->isInInteractiveMode) { ...; return; }` —— 提前 return 在
 * 尾段 `currentOperation = getOperation(pos); updateCursor();`（:3309-3313）
 * **之前** ⇒ 交互模式中光标不随物理 MOVE 重算：enter 时 updateCursor
 * 设定的操作形状保持到 leave。缺了本闸：键盘 &Size 交互中指针滑过
 * 内容区会把 SizeBDiag 光标刷成箭头。 */

XQ_TEST(XMdiSubWindow, interactiveModeCursorStableOnHover)
{
    XMdiArea* area = xmarMakeArea(700, 500);
    XMdiSubWindow* sw;
    XQ_REQUIRE(area != NULL);
    sw = xmarAddSub(area, 100, 100, 240, 180);
    XQ_REQUIRE(sw != NULL);
#if XMENU_ON
    {
        XAction* sizeAction = xmarFindMenuAction(sw, "&Size");
        XQ_REQUIRE(sizeAction != NULL);
        XAction_trigger(sizeAction);
#if XCURSOR_ON
        /* ① 进入交互模式即给操作光标（Size → BottomRightResize ⇒
         *    SizeFDiag，enterInteractiveMode 的 updateCursor；Qt 原文
         *    BottomRightResize → SizeFDiagCursor，qmdisubwindow.cpp:1024）。 */
        XQ_CHECK_MSG(xmarCursorShape((XWidget*)sw) == XCursor_SizeFDiag,
                     "enter cursor shape=%d",
                     xmarCursorShape((XWidget*)sw));
        /* ② 物理未按下的 MOVE 滑过内容区（命中 None）与缩放带：
         *    交互模式中一律不重算——光标保持 SizeFDiag（修前内容区
         *    一步就刷成 Arrow）。 */
        xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, 120, 100,
                  XMouseButton_NoButton);
        XQ_CHECK_MSG(xmarCursorShape((XWidget*)sw) == XCursor_SizeFDiag,
                     "hover content reset cursor to %d",
                     xmarCursorShape((XWidget*)sw));
        xmarMouse((XWidget*)sw, XEVENT_TYPE_MOUSE_MOVE, 239, 179,
                  XMouseButton_NoButton);
        XQ_CHECK_MSG(xmarCursorShape((XWidget*)sw) == XCursor_SizeFDiag,
                     "hover edge reset cursor to %d",
                     xmarCursorShape((XWidget*)sw));
#endif /* XCURSOR_ON */
        /* ③ Esc 退出后光标复位（leaveInteractiveMode 的 updateCursor）。 */
        xmarKey((XWidget*)sw, XKey_Escape, 0);
#if XCURSOR_ON
        XQ_CHECK_MSG(xmarCursorShape((XWidget*)sw) != XCursor_SizeFDiag,
                     "cursor not reset after leave: %d",
                     xmarCursorShape((XWidget*)sw));
#endif /* XCURSOR_ON */
        /* ④ 交互模式确已退出（后续方向键不动几何）。 */
        {
            int x0 = XWidget_x((XWidget*)sw);
            int y0 = XWidget_y((XWidget*)sw);
            xmarKey((XWidget*)sw, XKey_Right, 0);
            XQ_CHECK_MSG(XWidget_x((XWidget*)sw) == x0 &&
                             XWidget_y((XWidget*)sw) == y0,
                         "moved after leave: (%d,%d) -> (%d,%d)", x0, y0,
                         XWidget_x((XWidget*)sw), XWidget_y((XWidget*)sw));
        }
    }
#else
    XQ_CHECK(true); /* XMENU_ON=0：无可触发的菜单项。 */
#endif /* XMENU_ON */

    XClassDelete((XClass*)area);
}

/* ==================== ⑮ 系统菜单条目与 Qt 同序 ========================= */

/* Qt createSystemMenu（qmdisubwindow.cpp:1038-1046）的 addToSystemMenu 序：
 *   &Restore / &Move / &Size / Mi&nimize / Ma&ximize / Stay on &Top /
 *   分隔线 / &Close
 * 本域早期版本缺 &Move / &Size（当时无交互模式、按「不留占位项」省略），
 * B9 建成交互模式后补齐。 */

XQ_TEST(XMdiSubWindow, systemMenuOrderMatchesQt)
{
    XMdiSubWindow* sw = XMdiSubWindow_create(NULL, 0);
    XQ_REQUIRE(sw != NULL);
#if XMENU_ON
    {
        XMenu* menu = XMdiSubWindow_systemMenu(sw);
        const XVector* actions;
        int64_t n;
        XQ_CHECK_PTR(menu);
        actions = menu ? XMenu_actions(menu) : NULL;
        n = actions ? (int64_t)XVector_size_base((const XContainer*)actions)
                    : 0;
        /* 8 项：6 个动作 + 1 分隔线 + 1 Close。 */
        XQ_CHECK_MSG(n >= 8, "actions=%lld", (long long)n);
        if (n >= 3) {
            XAction* restore = XVector_At_Base((XVector*)actions, 0, XAction*);
            XAction* move = XVector_At_Base((XVector*)actions, 1, XAction*);
            XAction* size = XVector_At_Base((XVector*)actions, 2, XAction*);
            /* 前三项 = Restore / Move / Size（Qt 同序）。 */
            XQ_CHECK(restore != NULL &&
                     strstr(xmarActionText(restore), "Restore") != NULL);
            XQ_CHECK_MSG(move != NULL &&
                             strstr(xmarActionText(move), "Move") != NULL,
                         "第 2 项应为 &Move，实为 %s",
                         xmarActionText(move));
            XQ_CHECK_MSG(size != NULL &&
                             strstr(xmarActionText(size), "Size") != NULL,
                         "第 3 项应为 &Size，实为 %s",
                         xmarActionText(size));
        }
    }
#else
    XQ_CHECK(XMdiSubWindow_systemMenu(sw) == NULL);
#endif
    XClassDelete((XClass*)sw);
}

#endif /* XGUI_ON && XWIDGET_ON && XMDIAREA_ON */
