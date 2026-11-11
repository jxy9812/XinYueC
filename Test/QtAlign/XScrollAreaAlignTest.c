/**
 * @file       XScrollAreaAlignTest.c
 * @brief      XScrollArea × Qt 6.8 QScrollArea 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qscrollarea.h；
 *             - Qt 6.8.3 源码 qscrollarea.cpp：
 *               setWidget（:231-252 删旧件、双条归零、reparent 到视口、
 *               未置 WA_Resized 时 resize(sizeHint)、末尾 show）、
 *               takeWidget（:257-265 setParent(nullptr)）、
 *               sizeHint（:466-487 帧 + 内容尺寸或 12h×8h + AlwaysOn 条厚、
 *               boundedTo(36h,24h)）、viewportSizeHint（:496-504）、
 *               ensureVisible（:433-456）、ensureWidgetVisible（:360-392
 *               已可见早退 / 超视口居中 / 右下缘 +1）、
 *               构造里双条 setSingleStep(20)（:108-109/121-122）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测：空 QScrollArea
 *               sizeHint=206x138（h=17 ⇒ 2+12h × 2+8h）、有 120x90 内容
 *               且 resizable=false 时 sizeHint=122x92；takeWidget 后控件
 *               隐藏、hint 回到 206x138。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && \
    XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON

#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XScrollBar.h"
#include "XPainter.h"
#include "XClass.h"
#include "XObject.h"
#include "XCoreApplication.h"

/** @brief 字体行高（与实现同源：XWidget_font + XPainter_textHeight）。 */
static int xsaTest_fontHeight(const XWidget* widget)
{
    XFont font = XWidget_font(widget);
    int h = XPainter_textHeight(&font);
    XClassDeinit(&font);
    return h > 0 ? h : 1;
}

/* ==================== ① 缺省值 + 尺寸提示（Qt 公式） ==================== */

XQ_TEST(XScrollArea, defaultsAndSizeHints)
{
    XScrollArea* sa = XScrollArea_create(NULL, 0);
    XScrollBar* vbar;
    XScrollBar* hbar;
    XSize hint;
    XSize vpHint;
    int h;
    int frame;

    XQ_REQUIRE(sa != NULL);
    XQ_CHECK(XScrollArea_widget(sa) == NULL);
    XQ_CHECK(!XScrollArea_widgetResizable(sa));
    XQ_EQ_I64(XScrollArea_alignment(sa), 0); /* Qt 缺省 Qt::Alignment() = 0 */

    vbar = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)sa);
    hbar = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)sa);
    XQ_REQUIRE(vbar && hbar);
    /* 构造里双条 singleStep(20)（qscrollarea.cpp:108-109）。 */
    XQ_EQ_I64(XAbstractSlider_singleStep((const XAbstractSlider*)vbar), 20);
    XQ_EQ_I64(XAbstractSlider_singleStep((const XAbstractSlider*)hbar), 20);

    XWidget_resize((XWidget*)sa, 300, 300);
    h = xsaTest_fontHeight((XWidget*)sa);
    frame = 2 * XFrame_frameWidth((const XFrame*)sa);

    /* 空滚动区：sizeHint = 帧 + 12h × 8h（qscrollarea.cpp:479-481），
     * 再 boundedTo(36h, 24h)。 */
    hint = XScrollArea_sizeHint(sa);
    XQ_EQ_I64(hint.width, frame + 12 * h);
    XQ_EQ_I64(hint.height, frame + 8 * h);
    XQ_CHECK(hint.width <= 36 * h && hint.height <= 24 * h);
    /* 空视口建议尺寸 = 6h × 4h（qscrollarea.cpp:503-504）。 */
    vpHint = XScrollArea_viewportSizeHint(sa);
    XQ_EQ_I64(vpHint.width, 6 * h);
    XQ_EQ_I64(vpHint.height, 4 * h);
    /* 基类虚槽走子类重载：XAbstractScrollArea 查询同一函数返回值。 */
    XQ_EQ_I64(XAbstractScrollArea_viewportSizeHint(
                  (const XAbstractScrollArea*)sa).width, 6 * h);

    XClassDelete((XClass*)sa);
}

/* ==================== ② setWidget / takeWidget 所有权 ==================== */

XQ_TEST(XScrollArea, widgetOwnership)
{
    XScrollArea* sa = XScrollArea_create(NULL, 0);
    XWidget* viewport;
    XWidget* page;
    XWidget* page2;
    XWidget* taken;
    XSize hint;
    int frame;

    XQ_REQUIRE(sa != NULL);
    XWidget_resize((XWidget*)sa, 300, 300);
    viewport = XAbstractScrollArea_viewport(
        (const XAbstractScrollArea*)sa);
    XQ_REQUIRE(viewport != NULL);
    frame = 2 * XFrame_frameWidth((const XFrame*)sa);

    page = XWidget_create(NULL, 0);
    XQ_REQUIRE(page != NULL);
    XWidget_resize(page, 500, 300); /* 显式定尺 ⇒ 不触发 sizeHint 自适应 */
    XScrollArea_setWidget(sa, page);
    XQ_CHECK(XScrollArea_widget(sa) == page);
    /* 内容挂到视口（不是滚动区本体）——QScrollArea 的父子层级。 */
    XQ_CHECK(XWidget_parentWidget(page) == viewport);
    /* Qt 的 setWidget 末尾调用 widget->show()（qscrollarea.cpp:252），但
     * 滚动区自身未 show 时 page->isVisible() 仍为 false（生效可见要求
     * 顶层可见；本机 Qt 6.8 探针实测 isVisible=0 / isHidden=0）。
     * X 侧等价判据是显式隐藏位：show 后必须不处于「显式隐藏」态。 */
    XQ_CHECK(!XWidget_isHidden(page));
    /* 有内容时 sizeHint = 帧 + 内容现尺寸（resizable=false），
     * 且被 boundedTo(36h, 24h) 收口（本例 500x300 未触顶）。 */
    hint = XScrollArea_sizeHint(sa);
    XQ_EQ_I64(hint.width, frame + 500);
    XQ_EQ_I64(hint.height, frame + 300);
    /* 视口建议尺寸同取内容现尺寸（qscrollarea.cpp:499-501），且不受
     * sizeHint 的 36h/24h 上限约束。 */
    XQ_EQ_I64(XScrollArea_viewportSizeHint(sa).width, 500);
    XQ_EQ_I64(XScrollArea_viewportSizeHint(sa).height, 300);
    /* 内容宽超出视口 ⇒ 横条有范围。 */
    XQ_CHECK(XScrollBar_maximum(XAbstractScrollArea_horizontalScrollBar(
                  (const XAbstractScrollArea*)sa)) > 0);

    /* 换装：旧内容被销毁（Qt `delete d->widget`），视口里只剩新内容。 */
    page2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(page2 != NULL);
    XWidget_resize(page2, 400, 300);
    XScrollArea_setWidget(sa, page2);
    XQ_CHECK(XScrollArea_widget(sa) == page2);
    {
        const XVector* children = XObject_children((XObject*)viewport);
        XQ_EQ_SIZE(children ? XVector_size_base(
                          (const XContainer*)children) : 0, 1);
    }

    /* takeWidget：归还所有权（parent 置 NULL）并按 Qt 的父变更语义隐藏。 */
    taken = XScrollArea_takeWidget(sa);
    XQ_CHECK(taken == page2);
    XQ_CHECK(XScrollArea_widget(sa) == NULL);
    XQ_CHECK(XWidget_parentWidget(taken) == NULL);
    XQ_CHECK(!XWidget_isVisible(taken));
    /* 交还后 sizeHint 回到空区口径（12h × 8h）。 */
    {
        int h = xsaTest_fontHeight((XWidget*)sa);
        hint = XScrollArea_sizeHint(sa);
        XQ_EQ_I64(hint.width, frame + 12 * h);
    }
    XClassDelete((XClass*)taken);
    XClassDelete((XClass*)sa);
}

/* ==================== ④ 首选尺寸缓存 widgetSize（gap #176） ============== */

XQ_TEST(XScrollArea, widgetSizeCache)
{
    XScrollArea* sa = XScrollArea_create(NULL, 0);
    XWidget* page;
    XSize hint;
    int frame;

    XQ_REQUIRE(sa != NULL);
    XWidget_resize((XWidget*)sa, 300, 300);
    frame = 2 * XFrame_frameWidth((const XFrame*)sa);

    page = XWidget_create(NULL, 0);
    XQ_REQUIRE(page != NULL);
    XWidget_resize(page, 400, 200); /* 定尺 ⇒ 不触发 sizeHint 自适应 */
    XScrollArea_setWidget(sa, page);

    /* 首次查询把内容尺寸算进缓存（qscrollarea.cpp:470-472
     * `if (!d->widgetSize.isValid()) d->widgetSize = ...`）。 */
    hint = XScrollArea_sizeHint(sa);
    XQ_EQ_I64(hint.width, frame + 400);
    XQ_EQ_I64(hint.height, frame + 200);

    /* 内容此后改尺寸：sizeHint 不再跟随（Qt 的缓存只在 setWidget 失效，
     * qscrollarea.cpp:246；滚动区的首选尺寸不随内容漂移）。 */
    XWidget_resize(page, 800, 600);
    hint = XScrollArea_sizeHint(sa);
    XQ_EQ_I64(hint.width, frame + 400);
    XQ_EQ_I64(hint.height, frame + 200);

    /* 但 viewportSizeHint 是现算的（Qt 也重算，qscrollarea.cpp:496-504）。 */
    XQ_EQ_I64(XScrollArea_viewportSizeHint(sa).width, 800);
    XQ_EQ_I64(XScrollArea_viewportSizeHint(sa).height, 600);

    /* setWidget 换内容 ⇒ 缓存失效，按新内容重算一次。 */
    {
        XWidget* page2 = XWidget_create(NULL, 0);
        XQ_REQUIRE(page2 != NULL);
        XWidget_resize(page2, 250, 120);
        XScrollArea_setWidget(sa, page2);
    }
    hint = XScrollArea_sizeHint(sa);
    XQ_EQ_I64(hint.width, frame + 250);
    XQ_EQ_I64(hint.height, frame + 120);

    XClassDelete((XClass*)sa);
}

/* ==================== ③ ensureVisible / ensureWidgetVisible 算术 ======== */

XQ_TEST(XScrollArea, ensureVisibleArithmetic)
{
    XScrollArea* sa = XScrollArea_create(NULL, 0);
    XWidget* viewport;
    XWidget* content;
    XWidget* child;
    XScrollBar* vsb;
    XScrollBar* hsb;
    int pw;
    int ph;
    int before;
    XRect geom;

    XQ_REQUIRE(sa != NULL);
    XWidget_resize((XWidget*)sa, 300, 300);
    viewport = XAbstractScrollArea_viewport(
        (const XAbstractScrollArea*)sa);
    XQ_REQUIRE(viewport != NULL);
    content = XWidget_create(NULL, 0);
    XQ_REQUIRE(content != NULL);
    XWidget_resize(content, 500, 800);
    XScrollArea_setWidget(sa, content);
    child = XWidget_create(content, 0);
    XQ_REQUIRE(child != NULL);
    XWidget_setGeometry(child, 300, 500, 40, 30);

    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)sa);
    hsb = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)sa);
    XQ_REQUIRE(vsb && hsb);
    pw = XWidget_width(viewport);
    ph = XWidget_height(viewport);
    XQ_CHECK(pw > 0 && ph > 0);

    /* 目标完全可见 ⇒ 不动（qscrollarea.cpp:377-378 `contains` 早退）。 */
    XScrollBar_setValue(vsb, 0);
    XScrollBar_setValue(hsb, 0);
    XScrollArea_ensureWidgetVisible(sa, child, 10, 10);
    /* 目标在 (300,500,40,30)：QRect 右/下为**含端点**坐标
     * （right = x+width-1 = 339、bottom = 529），adjust(±10) 后
     * right=349、bottom=539。右缘越界 ⇒ value = right - pw + 1；
     * 下缘同理（qscrollarea.cpp:456-476）。Qt 的 visibleRect
     * 左/上界 = 内容相对视口的负偏移 = value。
     * 本机 Qt 6.8 探针：视口 284x284 时 hbar=66、vbar=256，即
     * 349-284+1 / 539-284+1（此前按 x+width=350/540 算会多 1）。 */
    XQ_EQ_I64(XScrollBar_value(hsb), 349 - pw + 1);
    XQ_EQ_I64(XScrollBar_value(vsb), 539 - ph + 1);

    /* 第二次调用：目标已完整可见 ⇒ 两个值都不再变化。 */
    before = XScrollBar_value(vsb);
    XScrollArea_ensureWidgetVisible(sa, child, 10, 10);
    XQ_EQ_I64(XScrollBar_value(vsb), before);
    geom = XWidget_geometry(child);
    XQ_CHECK(geom.x == 300 && geom.y == 500); /* 只滚滚动条，不动内容几何 */

    /* ensureVisible(0,0,50,50)：点已在视口左上时回滚到 0
     * （qscrollarea.cpp:443-445 `x - xmargin < value` ⇒ max(0, x-50)）。 */
    XScrollBar_setValue(vsb, 200);
    XScrollBar_setValue(hsb, 100);
    XScrollArea_ensureVisible(sa, 0, 0, 50, 50);
    XQ_EQ_I64(XScrollBar_value(vsb), 0);
    XQ_EQ_I64(XScrollBar_value(hsb), 0);

    XClassDelete((XClass*)sa);
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON */
