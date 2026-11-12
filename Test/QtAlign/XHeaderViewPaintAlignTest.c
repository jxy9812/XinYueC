/**
 * @file       XHeaderViewPaintAlignTest.c
 * @brief      XHeaderView §B3 自绘虚槽 + K-64 offset 联动 + K-74 级联 + 批量余数池 对齐契约测试。
 * @details    对齐依据：
 *             - Qt 6.8.3 源码（/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/
 *               widgets/itemviews/qheaderview.cpp）：
 *               ① paintSection（:2958-2982）/ sectionSizeFromContents
 *                  （:2995-3020）：两 protected 虚函数，子类可覆写自绘/
 *                  自测内容；sectionSizeFromContents 交 CT_HeaderSection 的
 *                  sizeFromContents；
 *               ② sectionViewportPosition = sectionPosition − headerOffset
 *                  （:645-656）；
 *               ③ setOffsetToLastSection：offset = length − 视口宽（可为负，
 *                  :446-452）；visualIndexAt 先加 headerOffset 再反查
 *                  （:563-571）；
 *               ④ cascadingSectionResizes（cascadingResize，:3737-3842）：
 *                  交互拖拽触底时向后续 Interactive 段级联收缩（每段压到
 *                  minimumSectionSize），delta<0 优先归还旧段、触底向前借；
 *               ⑤ resizeSections 全局 Stretch 池（:3588-3632）：余数按视觉序
 *                  前置分摊、末段 qMax(stretchLen, 末段快照宽)、余量<=0 保现宽、
 *                  下限 minimumSectionSize。
 *             - 本机 Qt 6.8.3 offscreen 探针（/tmp/qsprobe，2026-11-15）逐项核对：
 *               段宽 100/150/200/缺省 100 ⇒ 638 视口末段 188；resizeSections
 *               (Stretch) 可见 4 段 638 ⇒ 各 159（余数 2 前置给首两段：
 *               160/160/159/159）；cascading 拖拽把段 0 由 100 拉到 150 ⇒
 *               段 1 由 150 收缩到 100（触底）。
 * @note       C99；表头与模型全部释放，漏删即运行器判 [LEAK]。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XHeaderView.h"
#include "XHeaderView_Protected.h" /* setStretchViewportLength（K-64 视口宽复用）。 */
#include "XEvent.h"            /* XMouseEvent / XPoint 构造（拖拽事件注入）。 */
#include "XEventType.h"        /* XEVENT_TYPE_MOUSE_BUTTON_PRESS/RELEASE/MOVE。 */
#include "XWindowEvent.h"       /* XPaintEvent_create（独立摆放 PaintEvent 注入）。 */
#include "XImage.h"             /* XWidget_grab 出图 + XImage_pixel 像素断言。 */
#include "XObject_Protected.h" /* XObject_event_base（事件注入路径）。 */
#include "XAbstractItemModel.h"/* 模型：headerData_2 / setHeaderData_2 供内容测算。 */
#include "XVarList.h"          /* XVarList_args_3（sectionResized 载荷读取）。 */

/* ==================== 信号探针 ==================== */

static int    g_prHits;       /**< sectionResized 命中次数（resizeSections 池）。 */
static int    g_prLogical;     /**< sectionResized 的逻辑段号载荷。 */
static int    g_prOld;         /**< sectionResized 的旧宽载荷。 */
static int    g_prNew;         /**< sectionResized 的新宽载荷。 */
static int    g_geomHits;      /**< geometriesChanged 命中次数。 */

/** @brief sectionResized(logical,old,new) 槽。 */
static void xqp_sectionResized(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_prHits;
    if (!args) return;
    XVarList_args_3(args, int, logical, int, oldSz, int, newSz);
    g_prLogical = logical;
    g_prOld = oldSz;
    g_prNew = newSz;
}

/** @brief geometriesChanged() 槽。 */
static void xqp_geomSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_geomHits;
}

/** @brief 向表头直发一次鼠标事件（坐标=表头本地坐标）。 */
static void xqp_mouse(XHeaderView* header, XEventType type, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!header) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)header, (XEvent*)&me);
}

/* ==================== §B3 内容测算：无模型回落缺省 ==================== */

XQ_TEST(XHeaderViewPaint, sectionSizeHintNoModelFallsToDefault)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);
    XHeaderView_setDefaultSectionSize(hh, 88);
    /* 无模型 ⇒ 内容测算回落缺省段尺寸（保持旧口径可用值）。 */
    XQ_EQ_I64(XHeaderView_sectionSizeHint(hh, 0), 88);
    XQ_EQ_I64(XHeaderView_sectionSizeHint(hh, 2), 88);
    /* 越界 ⇒ -1。 */
    XQ_EQ_I64(XHeaderView_sectionSizeHint(hh, 3), -1);
    XClassDelete((XClass*)hh);
}

XQ_TEST(XHeaderViewPaint, sectionSizeHintHiddenIsZero)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);
    XHeaderView_hideSection(hh, 1);
    XQ_EQ_I64(XHeaderView_sectionSizeHint(hh, 1), 0);
    XClassDelete((XClass*)hh);
}

/* ==================== §B3 内容测算：有模型走内容 ==================== */

XQ_TEST(XHeaderViewPaint, sectionSizeFromContentsHasModel)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XAbstractItemModel* model;
    XSize cjk;
    XSize ascii;
    XSize empty;
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);
    model = XAbstractItemModel_create();
    XQ_REQUIRE(model != NULL);
    XAbstractItemModel_setDimension(model, 1, 3);
    XAbstractItemModel_setHeaderData_2(model, 0, 0, "名称");
    XAbstractItemModel_setHeaderData_2(model, 1, 0, "M");
    /* 段 2 不设文本 ⇒ 空格文本（内容宽只应剩左右 margin）。 */
    XHeaderView_setModel(hh, model);
    cjk = XHeaderView_sectionSizeFromContents_base(hh, 0);
    ascii = XHeaderView_sectionSizeFromContents_base(hh, 1);
    empty = XHeaderView_sectionSizeFromContents_base(hh, 2);
    /* ① 文本必须**真进入测算**：只画 margin 的空段宽（8px 级）必须严格小于
     *    有文本的段——这条锁掉「基类 m_text 未赋值 ⇒ textW 恒 0」的缺陷
     *    （XStyleOptionHeader::m_text 与 XStyleOption::m_text 是两个成员，
     *    样式侧只读基类那个，见 XHeaderView.c xhv_initSectionOption @note）。 */
    XQ_CHECK_MSG(cjk.width > empty.width,
                 "CJK 段宽必须大于空段（%d vs %d）", cjk.width, empty.width);
    XQ_CHECK_MSG(ascii.width > empty.width,
                 "ASCII 段宽必须大于空段（%d vs %d）", ascii.width, empty.width);
    /* ② CJK「名称」= 2 码点 × 2 单位；「M」= 1 单位 ⇒ CJK 更宽（按 UTF-8
     *    码点计权，不是旧「字节数×8」——旧口径下 6 字节会把 CJK 估成 48px、
     *    单字节 ASCII 估成 8px，比值 6:1；新口径约 4:1 且都随字体缩放）。 */
    XQ_CHECK_MSG(cjk.width > ascii.width,
                 "CJK 段宽应大于单字符 ASCII 段（%d vs %d）",
                 cjk.width, ascii.width);
    /* ③ 高度取字体度量（非 0）。 */
    XQ_CHECK(cjk.height > 0);
    /* ④ sectionSizeHint 走同一内容口径，且被钳到 >= minimumSectionSize。 */
    XQ_CHECK(XHeaderView_sectionSizeHint(hh, 0) >=
             XHeaderView_minimumSectionSize(hh));
    /* ⑤ 文本宽度变化必须**反映到测算值**（复核 2026-11-15「真断言」口径：
     *    只锁「有 vs 无」不够——文本内容变了测算值必须跟着变，否则测算
     *    与文本脱钩）。段 0 换更长文本后重测，宽必须严格变大。 */
    XAbstractItemModel_setHeaderData_2(model, 0, 0,
                                       "名称加长后更宽的表头文本");
    {
        XSize longer = XHeaderView_sectionSizeFromContents_base(hh, 0);
        XQ_CHECK_MSG(longer.width > cjk.width,
                     "文本加长后测算宽必须变大（%d vs %d）",
                     longer.width, cjk.width);
    }
    XClassDelete((XClass*)hh);
    XClassDelete((XClass*)model);
}

/* ==================== §B3 自绘路径：独立摆放可见 ==================== */

/** @brief 统计矩形内 alpha 非零（已着色）的像素数。 */
static int xqp_opaqueCount(const XImage* img, int x0, int y0, int x1, int y1)
{
    int n = 0;
    int x;
    int y;
    int w;
    int h;
    if (!img) return -1;
    w = XImage_width(img);
    h = XImage_height(img);
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x)
            if ((XImage_pixel(img, x, y) >> 24) != 0) ++n;
    return n;
}

/** @brief 统计两图逐像素不同的点数（尺寸取两图较小者，防越界）。 */
static int xqp_diffPixels(const XImage* a, const XImage* b)
{
    int n = 0;
    int x;
    int y;
    int w;
    int h;
    if (!a || !b) return -1;
    w = XImage_width(a);
    if (XImage_width(b) < w) w = XImage_width(b);
    h = XImage_height(a);
    if (XImage_height(b) < h) h = XImage_height(b);
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            if (XImage_pixel(a, x, y) != XImage_pixel(b, x, y)) ++n;
    return n;
}

XQ_TEST(XHeaderViewPaint, paintSectionActuallyDrawsUnderGrab)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XAbstractItemModel* model;
    XImage* shot;
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 200);
    XHeaderView_setSectionSize(hh, 3, 100);
    XWidget_setGeometry((XWidget*)hh, 0, 0, 550, 30);
    model = XAbstractItemModel_create();
    if (model) {
        XAbstractItemModel_setDimension(model, 1, 4);
        XAbstractItemModel_setHeaderData_2(model, 0, 0, "列一");
        XAbstractItemModel_setHeaderData_2(model, 1, 0, "列二");
        XAbstractItemModel_setHeaderData_2(model, 2, 0, "列三");
        XAbstractItemModel_setHeaderData_2(model, 3, 0, "列四");
        XHeaderView_setModel(hh, model);
    }
    /* **真驱动自绘**：XWidget_grab 路径 2（XWidget.c:14586-14592）对任何
     * 控件走 xwidget_renderSubtree ⇒ 离屏重定向 ⇒ XWidget_paintImage 返回
     * 重定向画布（XWidget.c:12569-12573）⇒ VXHeaderView_paintEvent 不会在
     * 「无后备存储」处早退（早期版本的用例直接注入 PAINT 事件即止步于此，
     * 属空壳验收——表头段文本一字不画也照样"通过"）。 */
    shot = XWidget_grab((XWidget*)hh);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(XImage_width(shot), 550);
    XQ_EQ_I64(XImage_height(shot), 30);
    /* ① 段带必须**被画出来**：4 段合计 550×30 面积里，应有成片已着色像素
     *    （只画底色不画字也算；本断言锁"paintSection 真被执行"）。 */
    XQ_CHECK_MSG(xqp_opaqueCount(shot, 0, 0, 550, 30) > 550 * 30 / 2,
                 "段带未被绘制（着色像素 %d）",
                 xqp_opaqueCount(shot, 0, 0, 550, 30));
    /* ② 段 0 与段 1 之间应有分隔线差异：分隔线画在段矩形右缘
     *    x+w-1 = 99（xcs_drawHeaderSection / 自绘回落两路同口径），
     *    故取分隔列 99 与其左侧底色列 98 对比（98/100 两列都是
     *    Button 底色，跨线采样采不到线——复核 2026-11-15）。 */
    {
        uint32_t lhs = XImage_pixel(shot, 98, 15);
        uint32_t rhs = XImage_pixel(shot, 99, 15);
        XQ_CHECK_MSG(lhs != rhs,
                     "段 0/1 交界处无分隔线差异（%08x vs %08x）", lhs, rhs);
    }
    /* ②′ 段文本必须**真上屏**（复核 2026-11-15 假绿订正——上一版只锁
     *    「底色画了」：把 xhv_initSectionOption 的基类 m_text 写入注释掉
     *    精确复现「base/derived 文本字段分置」缺陷后，本用例 4 项断言
     *    全数通过、实测字一个没画（2026-11-15 缺陷注入实验）。现补孪生
     *    对比：同 count/同段宽/同几何、**无模型**（无文本）的孪生表头
     *    grab 一张，与主图逐像素对比——底色/分隔线两图同源，唯一差异
     *    来源就是段文本 ⇒ 差异像素必须 > 0。锁样式侧
     *    xcs_drawHeaderLabel 只读 XStyleOption::m_text 基类字段
     *    （XCommonStyle.c:3092）的读取链。 */
    {
        XHeaderView* bare = XHeaderView_create(NULL, 0, 0);
        XImage* bareShot;
        int diffs;
        XQ_REQUIRE(bare != NULL);
        XHeaderView_setCount(bare, 4);
        XHeaderView_setSectionSize(bare, 0, 100);
        XHeaderView_setSectionSize(bare, 1, 150);
        XHeaderView_setSectionSize(bare, 2, 200);
        XHeaderView_setSectionSize(bare, 3, 100);
        XWidget_setGeometry((XWidget*)bare, 0, 0, 550, 30);
        bareShot = XWidget_grab((XWidget*)bare);
        XQ_REQUIRE(bareShot != NULL);
        diffs = xqp_diffPixels(shot, bareShot);
        XQ_CHECK_MSG(diffs > 0,
                     "段文本未上屏：有文本/无文本孪生图零差异（%d 像素）",
                     diffs);
        XClassDelete((XClass*)bareShot);
        XClassDelete((XClass*)bare);
    }
    XClassDelete((XClass*)shot);
    /* ③ 附加：直接注入 PAINT 事件也必须不崩（无后备存储时早退，不算验收
     *    主证，但锁住"事件进来不越界"）。 */
    {
        XRegion reg;
        XRect full;
        XEvent* pe;
        XRect_init(&full, 0, 0, 550, 30);
        XRegion_init(&reg);
        XRegion_addRect(&reg, &full);
        pe = (XEvent*)XPaintEvent_create(XEVENT_TYPE_PAINT, &reg);
        if (pe) {
            XObject_event_base((XObject*)hh, pe);
            XClassDelete((XClass*)pe);
        }
        /* 局部 region 的矩形数组是堆分配，用毕必须 XRegion_deinit
         * （普通函数、非 XClass 虚入口；漏拆即复核遍 +64，运行器判 [LEAK]）。 */
        XRegion_deinit(&reg);
    }
    XClassDelete((XClass*)hh);
    if (model) XClassDelete((XClass*)model);
}

/* ==================== K-64 offset 联动 ==================== */

XQ_TEST(XHeaderViewPaint, sectionViewportPositionMinusOffset)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 200);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 0), 0);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 1), 100);
    XQ_EQ_I64(XHeaderView_sectionPosition(hh, 2), 250);
    /* offset==0 时视口坐标与段坐标恒等（旧口径）。 */
    XQ_EQ_I64(XHeaderView_sectionViewportPosition(hh, 0), 0);
    XQ_EQ_I64(XHeaderView_sectionViewportPosition(hh, 2), 250);
    /* 设 offset=60 ⇒ 视口坐标 = position − offset。 */
    XHeaderView_setOffset(hh, 60);
    XQ_EQ_I64(XHeaderView_offset(hh), 60);
    XQ_EQ_I64(XHeaderView_sectionViewportPosition(hh, 0), -60);
    XQ_EQ_I64(XHeaderView_sectionViewportPosition(hh, 2), 190);
    XClassDelete((XClass*)hh);
}

XQ_TEST(XHeaderViewPaint, offsetToLastSectionUsesViewportWidth)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 200);
    XHeaderView_setSectionSize(hh, 3, 100);
    /* 未推送视口宽：视口宽按 0 参与 ⇒ offset 退化为 length（550）。 */
    XHeaderView_setOffsetToLastSection(hh);
    XQ_EQ_I64(XHeaderView_offset(hh), 550);
    /* 推送视口宽 300：offset = length − 视口宽 = 250（Qt 6.8 原文
     * qheaderview.cpp:446-452 `position = length() - viewportSize`，
     * 无末段加项、可为负）。 */
    XHeaderView_setStretchViewportLength(hh, 300);
    XHeaderView_setOffsetToLastSection(hh);
    XQ_EQ_I64(XHeaderView_offset(hh), 250);
    XQ_EQ_I64(XHeaderView_sectionViewportPosition(hh, 3),
              XHeaderView_sectionPosition(hh, 3) - 250);
    XClassDelete((XClass*)hh);
}

XQ_TEST(XHeaderViewPaint, visualIndexAtAddsOffset)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 3);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 100);
    XHeaderView_setSectionSize(hh, 2, 100);
    /* 无 offset：位置 150 命中视觉序 1。 */
    XQ_EQ_I64(XHeaderView_visualIndexAt(hh, 150), 1);
    /* 设 offset=50：position+offset = 200 ⇒ 视觉序 2。 */
    XHeaderView_setOffset(hh, 50);
    XQ_EQ_I64(XHeaderView_visualIndexAt(hh, 150), 2);
    XQ_EQ_I64(XHeaderView_visualIndexAt(hh, 250), -1); /* >length ⇒ -1。 */
    XClassDelete((XClass*)hh);
}

/* ==================== K-74 级联收缩 ==================== */

XQ_TEST(XHeaderViewPaint, cascadingResizeShrinksNeighbors)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 200);
    XHeaderView_setSectionSize(hh, 3, 100);
    XHeaderView_setMinimumSectionSize(hh, 40);
    XHeaderView_setSectionResizeModeAt(hh, 0, XHeaderViewResizeMode_Interactive);
    XHeaderView_setSectionResizeModeAt(hh, 1, XHeaderViewResizeMode_Interactive);
    XHeaderView_setSectionResizeModeAt(hh, 2, XHeaderViewResizeMode_Interactive);
    XHeaderView_setSectionResizeModeAt(hh, 3, XHeaderViewResizeMode_Interactive);
    XHeaderView_setCascadingSectionResizes(hh, true);
    XQ_CHECK(XHeaderView_cascadingSectionResizes(hh));
    /* 拖拽段 0 手柄由 100 拉到 150（+50）⇒ 段 1 由 150 收缩到 100（触底不
     * 再压，delta 余 0）；关闭级联时只会改段 0、段 1 不动。 */
    xqp_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 100, 0); /* 段 0 尾线命中。 */
    xqp_mouse(hh, XEVENT_TYPE_MOUSE_MOVE, 150, 0);          /* 拉 +50。 */
    xqp_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 150, 0);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 150);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 100);
    XClassDelete((XClass*)hh);
}

XQ_TEST(XHeaderViewPaint, nonCascadingResizeKeepsNeighbor)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 200);
    XHeaderView_setSectionSize(hh, 3, 100);
    XHeaderView_setSectionResizeModeAt(hh, 0, XHeaderViewResizeMode_Interactive);
    XHeaderView_setSectionResizeModeAt(hh, 1, XHeaderViewResizeMode_Interactive);
    XHeaderView_setSectionResizeModeAt(hh, 2, XHeaderViewResizeMode_Interactive);
    XHeaderView_setSectionResizeModeAt(hh, 3, XHeaderViewResizeMode_Interactive);
    XHeaderView_setCascadingSectionResizes(hh, false);
    XQ_CHECK(!XHeaderView_cascadingSectionResizes(hh));
    xqp_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 100, 0);
    xqp_mouse(hh, XEVENT_TYPE_MOUSE_MOVE, 150, 0);
    xqp_mouse(hh, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 150, 0);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 150);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 150); /* 邻居不动（旧口径）。 */
    XClassDelete((XClass*)hh);
}

/* ==================== 批量池余数分摊 + 逐段 sectionResized ==================== */

XQ_TEST(XHeaderViewPaint, resizeSectionsPoolRemainderAndPerSectionSignal)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XObject* obj;
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 200);
    XHeaderView_setSectionSize(hh, 3, 100);
    /* 探针**先**连（setSectionResizeMode 内部即调度 resizeSections，
     * 那才是本用例要观测的切模式重排）；计数器在连接后清零。 */
    obj = (XObject*)hh;
    XObject_connect(obj, XSignal(XHeaderView_sectionResized_signal), NULL,
                    xqp_sectionResized, XConnectionType_Direct);
    XObject_connect(obj, XSignal(XHeaderView_geometriesChanged_signal), NULL,
                    xqp_geomSlot, XConnectionType_Direct);
    g_prHits = 0; g_geomHits = 0;
    XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Stretch);
    /* 可见 4 段共 550 ⇒ 各 137、余数 2 前置给视觉序首两段：
     * 138/138/137/137（Qt :3592-3594 + :3624-3626）。 */
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 138);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 138);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 137);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 137);
    /* 池应用逐段发 sectionResized（切模式后视图侧 m_colWidths 即时镜像，
     * 对标 Qt :3642-3643 的逐段发射）；几何真变只补发一次
     * geometriesChanged（AGENTS 重绘纪律：不做逐段信号风暴）。 */
    XQ_CHECK(g_prHits == 4);
    XQ_CHECK(g_geomHits == 1);
    XClassDelete((XClass*)hh);
}

XQ_TEST(XHeaderViewPaint, resizeSectionsPoolNoChangeNoSignal)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XObject* obj;
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 2);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 100);
    /* setSectionResizeMode 内部已跑过一轮池分配：2 段共 200 ⇒ 各 100 无变化。 */
    XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Stretch);
    obj = (XObject*)hh;
    XObject_connect(obj, XSignal(XHeaderView_sectionResized_signal), NULL,
                    xqp_sectionResized, XConnectionType_Direct);
    XObject_connect(obj, XSignal(XHeaderView_geometriesChanged_signal), NULL,
                    xqp_geomSlot, XConnectionType_Direct);
    g_prHits = 0; g_geomHits = 0;
    /* 同参再跑一次：段宽不变 ⇒ 逐段 sectionResized 早退、geometriesChanged
     * 也不发（对标 Qt :3642-3643 的 `newSectionLength != oldSectionLength`
     * 门 ⇒ 零发射；AGENTS 重绘纪律禁逐段信号风暴）。 */
    XHeaderView_resizeSections(hh, XHeaderViewResizeMode_Stretch);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 100);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 100);
    XQ_CHECK(g_prHits == 0);
    XQ_CHECK(g_geomHits == 0);
    XClassDelete((XClass*)hh);
}

/* ==================== 末段快照 qMax 下限（与 K-73 一致） ==================== */

XQ_TEST(XHeaderViewPaint, resizeSectionsHonorsLastSectionSnapshot)
{
    XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
    XObject* obj;
    XQ_REQUIRE(hh != NULL);
    XHeaderView_setCount(hh, 4);
    XHeaderView_setSectionSize(hh, 0, 100);
    XHeaderView_setSectionSize(hh, 1, 150);
    XHeaderView_setSectionSize(hh, 2, 200);
    XHeaderView_setSectionSize(hh, 3, 300);   /* 末段快照托底 300。 */
    XHeaderView_setStretchLastSection(hh, true); /* 快照 = 末段现宽 300。 */
    obj = (XObject*)hh;
    XObject_connect(obj, XSignal(XHeaderView_sectionResized_signal), NULL,
                    xqp_sectionResized, XConnectionType_Direct);
    XObject_connect(obj, XSignal(XHeaderView_geometriesChanged_signal), NULL,
                    xqp_geomSlot, XConnectionType_Direct);
    g_prHits = 0; g_geomHits = 0;
    /* 全局 Stretch 批量重排：本例的**可用长**是可见段现宽和
     * 100+150+200+300 = 750（本库批量路径沿用既有简化——Qt 用
     * viewport->width()，见 XHeaderView.c resizeSections 块头注 ①）⇒
     * 750/4 = 187 余 2 ⇒ 视觉序前三段吃余数 188/188/187（余数落完即止），
     * 末段走 qMax(187, 快照 300) = 300（Qt :3619-3623 末段下限）。 */
    XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Stretch);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 188);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 1), 188);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 2), 187);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 3), 300);
    /* sectionResized 只为**真变**的段发射（Qt 6.8 原文 qheaderview.cpp
     * :3643 `if (newSectionLength != oldSectionLength) emit`）；末段
     * qMax(187, 快照 300) = 300 与现宽相同 ⇒ 不发。100→188 / 150→188 /
     * 200→187 共 3 次。 */
    XQ_CHECK(g_prHits == 3);
    XClassDelete((XClass*)hh);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
