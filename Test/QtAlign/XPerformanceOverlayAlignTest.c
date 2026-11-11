/**
 * @file       XPerformanceOverlayAlignTest.c
 * @brief      XPerformanceOverlay 内部一致性与所有权契约测试。
 * @details    本机 Qt 6.8 没有对应类（性能悬浮层是 XinYueC 的 XLabel 派生
 *             扩展），因此本文件不做 API 对表，而是锁定：
 *             - 构造默认值 / 几何 / 标签基类属性（margin=4、Left|Top）；
 *             - 值语义 copy/move：格式模板深拷贝、move 后源对象归默认值；
 *             - **刷新节流**：updateNetwork 只落采样值、不刷新文本；
 *               updateFrame 仅在统计窗口（XGUI_PERFORMANCE_OVERLAY_UPDATE_MS）
 *               到期时刷新一次文本 —— 对齐 AGENTS.md「重绘/帧数」节
 *               的「悬浮层对外只剩 1Hz 一次更新、不得持续标脏」红线；
 *             - autoFitSize 的 8px 宽度量化（防文本宽度抖动引发重锚/整窗脏区）；
 *             - 拖拽会话（beginDrag/dragTo/endDrag）与 fixed/movable 闸；
 *             - 九宫格预置位置；
 *             - 父子所有权（父析构带走悬浮层，泄漏门由运行器判定）。
 * @note       刷新节流断言用"文本内容不变"作为可观察证据：setText 有同值
 *             早退，若每拍都调 updateText 且值变化，文本必然改变。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XPerformanceOverlay.h"
#include "XLabel.h"
#include "XWidget.h"
#include "XString.h"
#include "XObject.h"
#include "XPainter.h"
#include "XImage.h"

#include <string.h>

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON

/** @brief 悬浮层标签文本的 UTF-8 借用指针。 */
static const char* xqo_text(const XPerformanceOverlay* overlay)
{
    const XString* text = XLabel_text(&overlay->m_base);
    return text ? XString_toUtf8(text) : "";
}

/** @brief 把文本快照到本地缓冲（后续断言按快照比较）。 */
static void xqo_snapshot(const XPerformanceOverlay* overlay, char* buf,
                         size_t cap)
{
    const char* text = xqo_text(overlay);
    if (!buf || cap == 0u) return;
    buf[0] = '\0';
    if (text) {
        strncpy(buf, text, cap - 1u);
        buf[cap - 1u] = '\0';
    }
}

/* ==================== ① 默认值 + copy/move 所有权 ==================== */

XQ_TEST(XPerformanceOverlay, defaultsAndValueSemantics)
{
    XPerformanceOverlay overlay;
    XPerformanceOverlay copy;
    XRect geo;

    XPerformanceOverlay_init(&overlay, NULL, 0);

    /* Qt 无对应类：此处锁定本类自身构造契约（init 内的显式缺省值）。 */
    XQ_CHECK(XPerformanceOverlay_isFpsVisible(&overlay));
    XQ_CHECK(XPerformanceOverlay_isFrameTimeVisible(&overlay));
    XQ_CHECK(XPerformanceOverlay_isNetworkVisible(&overlay));
    XQ_CHECK(XPerformanceOverlay_isSysStatVisible(&overlay));
    XQ_CHECK(XPerformanceOverlay_isMemoryVisible(&overlay));
    XQ_EQ_I64(XPerformanceOverlay_memoryDisplay(&overlay),
              XPerformanceOverlayMemoryDisplay_Both);
    XQ_EQ_I64(XPerformanceOverlay_memorySource(&overlay),
              XPerformanceOverlayMemorySource_Auto);
    XQ_CHECK(XPerformanceOverlay_isMovable(&overlay));
    XQ_CHECK(!XPerformanceOverlay_isFixed(&overlay));
    XQ_CHECK(!XPerformanceOverlay_isDragging(&overlay));
    XQ_CHECK(!XPerformanceOverlay_isAutoFitSize(&overlay));
    XQ_CHECK(XPerformanceOverlay_format(&overlay) == NULL);
    XQ_EQ_I64(XPerformanceOverlay_backgroundColor(&overlay),
              XPERFORMANCEOVERLAY_BACKGROUND_DEFAULT);

    geo = XPerformanceOverlay_geometry(&overlay);
    XQ_EQ_I64(geo.x, 0);
    XQ_EQ_I64(geo.y, 0);
    XQ_EQ_I64(geo.width, 180);
    XQ_EQ_I64(geo.height, 60);
    XQ_EQ_I64(XLabel_margin(&overlay.m_base), 4);
    XQ_EQ_I64(XLabel_alignment(&overlay.m_base),
              XAlignment_Left | XAlignment_Top);
    /* reset() 经 updateText 写入首屏文本 ⇒ 文本标签模式。 */
    XQ_CHECK(overlay.m_base.m_isTextLabel);
    XQ_CHECK(strstr(xqo_text(&overlay), "FPS") != NULL);

    /* 底板色内部一致性：可写可读、同值早退（不产生额外脏区）。 */
    XPerformanceOverlay_setBackgroundColor(&overlay, 0x80112233u);
    XQ_EQ_I64(XPerformanceOverlay_backgroundColor(&overlay), 0x80112233u);
    XPerformanceOverlay_setBackgroundColor(&overlay, 0x80112233u);
    XQ_EQ_I64(XPerformanceOverlay_backgroundColor(&overlay), 0x80112233u);

    /* 值语义 copy：模板深拷贝，改副本不影响源。 */
    XPerformanceOverlay_setFormat(&overlay, "FPS {fps}");
    XPerformanceOverlay_init(&copy, NULL, 0);
    XClassCopy(&copy, &overlay);
    XQ_EQ_STR(XPerformanceOverlay_format(&copy), "FPS {fps}");
    XQ_EQ_I64(XPerformanceOverlay_backgroundColor(&copy), 0x80112233u);
    XPerformanceOverlay_setFormat(&copy, "COPY");
    XQ_EQ_STR(XPerformanceOverlay_format(&overlay), "FPS {fps}");
    XQ_EQ_STR(XPerformanceOverlay_format(&copy), "COPY");

    /* 值语义 move：内容转移、源对象归默认值（含底板色与格式）。 */
    {
        XPerformanceOverlay moved;
        XPerformanceOverlay_init(&moved, NULL, 0);
        XClassMove(&moved, &copy);
        XQ_EQ_STR(XPerformanceOverlay_format(&moved), "COPY");
        XQ_EQ_I64(XPerformanceOverlay_backgroundColor(&moved), 0x80112233u);
        XQ_CHECK(XPerformanceOverlay_format(&copy) == NULL);
        XQ_EQ_I64(XPerformanceOverlay_backgroundColor(&copy),
                  XPERFORMANCEOVERLAY_BACKGROUND_DEFAULT);
        XClassDeinit(&moved);
    }

    XClassDeinit(&copy);
    XClassDeinit(&overlay);
}

/* ==================== ② 刷新节流（不得持续标脏） ==================== */

XQ_TEST(XPerformanceOverlay, refreshThrottle)
{
    XPerformanceOverlay overlay;
    char before[256];
    char after[256];
    int i;

    XPerformanceOverlay_init(&overlay, NULL, 0);
    /* 只留 FPS 行，文本口径确定（"FPS 0.0"）。 */
    XPerformanceOverlay_setFrameTimeVisible(&overlay, false);
    XPerformanceOverlay_setNetworkVisible(&overlay, false);
    XPerformanceOverlay_setSysStatVisible(&overlay, false);
    XPerformanceOverlay_setMemoryVisible(&overlay, false);
    XQ_EQ_STR(xqo_text(&overlay), "FPS 0.0");

    /* 统计窗口未满：updateFrame 只累积样本，不刷新文本。 */
    XPerformanceOverlay_updateFrame(&overlay, 1000000, 1016000);
    XQ_EQ_STR(xqo_text(&overlay), "FPS 0.0");
    xqo_snapshot(&overlay, before, sizeof(before));
    for (i = 1; i <= 5; ++i) {
        XPerformanceOverlay_updateFrame(&overlay,
                                        1016000 + i * 16000,
                                        1032000 + i * 16000);
    }
    xqo_snapshot(&overlay, after, sizeof(after));
    XQ_CHECK_MSG(strcmp(before, after) == 0,
                 "窗口内多拍不得刷新文本（1Hz 节流）：[%s] vs [%s]",
                 before, after);

    /* 跨过统计窗口：刷新一次，FPS 由本窗口样本算出。 */
    XPerformanceOverlay_updateFrame(&overlay, 1200000, 2500000);
    xqo_snapshot(&overlay, after, sizeof(after));
    XQ_CHECK_MSG(strcmp(before, after) != 0,
                 "窗口到期应刷新一次文本：[%s]", after);
    XQ_CHECK(strstr(after, "FPS ") != NULL);

    /* updateNetwork 只落采样值、不刷新文本（AGENTS 红线：文本刷新统一
     * 归 updateFrame 的统计窗口，否则每秒两次文本变化 = 两帧）。 */
    XPerformanceOverlay_setNetworkVisible(&overlay, true);
    xqo_snapshot(&overlay, before, sizeof(before));
    XPerformanceOverlay_updateNetwork(&overlay, true, 1024u * 100u,
                                      1024u * 50u, 2600000);
    xqo_snapshot(&overlay, after, sizeof(after));
    XQ_CHECK_MSG(strcmp(before, after) == 0,
                 "updateNetwork 不得直接刷新文本：[%s] vs [%s]",
                 before, after);
    /* 下一个统计窗口把网络行带出来。 */
    XPerformanceOverlay_updateFrame(&overlay, 2600000, 3700000);
    XQ_CHECK_MSG(strstr(xqo_text(&overlay), "下载") != NULL,
                 "统计窗口到期应带出网络行：[%s]", xqo_text(&overlay));

    XClassDeinit(&overlay);
}

/* ==================== ③ autoFitSize（8px 量化） ==================== */

XQ_TEST(XPerformanceOverlay, autoFitSize)
{
    XPerformanceOverlay overlay;
    XRect narrow;
    XRect geo;
    int i;

    XPerformanceOverlay_init(&overlay, NULL, 0);
    /* 先关掉两行指标，让"打开更多指标后文本变长"成为真实变化。 */
    XPerformanceOverlay_setSysStatVisible(&overlay, false);
    XPerformanceOverlay_setMemoryVisible(&overlay, false);
    XQ_CHECK(!XPerformanceOverlay_isAutoFitSize(&overlay));
    XPerformanceOverlay_setAutoFitSize(&overlay, true);
    XQ_CHECK(XPerformanceOverlay_isAutoFitSize(&overlay));
    narrow = XPerformanceOverlay_geometry(&overlay);
    XQ_CHECK_MSG(narrow.width > 0 && narrow.height > 0,
                 "开启 autoFit 后应收到文本尺寸：%dx%d",
                 narrow.width, narrow.height);
    /* 宽度按 8px 网格量化（[宽度按 8px 量化 2026-11-08]：文本宽度抖动
     * 不得直接引发重锚/整窗脏区）。 */
    XQ_EQ_I64(narrow.width % 8, 0);

    /* 文本变长（打开更多指标）后仍按 8px 网格收框。 */
    XPerformanceOverlay_setSysStatVisible(&overlay, true);
    XPerformanceOverlay_setMemoryVisible(&overlay, true);
    for (i = 0; i < 3; ++i)
        XPerformanceOverlay_updateFrame(&overlay, 1000000 + i * 400000,
                                        2200000 + i * 400000);
    geo = XPerformanceOverlay_geometry(&overlay);
    XQ_EQ_I64(geo.width % 8, 0);
    XQ_CHECK(geo.height > 0 && geo.width > 0);
#if XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON || XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    XQ_CHECK_MSG(geo.height > narrow.height,
                 "多出指标行应收高：%d vs %d", geo.height, narrow.height);
#endif

    XPerformanceOverlay_setAutoFitSize(&overlay, false);
    XQ_CHECK(!XPerformanceOverlay_isAutoFitSize(&overlay));

    XClassDeinit(&overlay);
}

/* ==================== ④ 拖拽会话与九宫格预置位置 ==================== */

XQ_TEST(XPerformanceOverlay, dragAndPreset)
{
    XPerformanceOverlay overlay;
    XPoint position;

    XPerformanceOverlay_init(&overlay, NULL, 0);
    XPerformanceOverlay_setSize(&overlay, 100, 50);
    XPerformanceOverlay_setPosition(&overlay, 30, 40);
    XQ_CHECK(!XPerformanceOverlay_isDragging(&overlay));

    XQ_CHECK(XPerformanceOverlay_beginDrag(&overlay, 35, 45));
    XQ_CHECK(XPerformanceOverlay_isDragging(&overlay));
    /* 拖拽偏移 (5,5)：目标 (200,180) ⇒ 原点 (195,175)，钳进 200x200
     * 客户区的 (100,150)。 */
    XQ_CHECK(XPerformanceOverlay_dragTo(&overlay, 200, 180, 200, 200));
    position = XPerformanceOverlay_position(&overlay);
    XQ_EQ_I64(position.x, 100);
    XQ_EQ_I64(position.y, 150);
    XPerformanceOverlay_endDrag(&overlay);
    XQ_CHECK(!XPerformanceOverlay_isDragging(&overlay));

    /* fixed 闸：锁定后拒绝开始拖拽；解除后恢复。 */
    XPerformanceOverlay_setFixed(&overlay, true);
    XQ_CHECK(!XPerformanceOverlay_beginDrag(&overlay, 105, 155));
    XPerformanceOverlay_setFixed(&overlay, false);
    /* movable 闸：关闭时拒绝；setMovable(false) 也会结束进行中的会话。 */
    XQ_CHECK(XPerformanceOverlay_beginDrag(&overlay, 105, 155));
    XPerformanceOverlay_setMovable(&overlay, false);
    XQ_CHECK(!XPerformanceOverlay_isMovable(&overlay));
    XQ_CHECK(!XPerformanceOverlay_isDragging(&overlay));
    XQ_CHECK(!XPerformanceOverlay_beginDrag(&overlay, 105, 155));
    XPerformanceOverlay_setMovable(&overlay, true);
    XQ_CHECK(XPerformanceOverlay_isMovable(&overlay));

    /* 九宫格：右下角 = 客户区右上角 - 尺寸 - margin。 */
    XPerformanceOverlay_setSize(&overlay, 80, 40);
    XPerformanceOverlay_setPresetPosition(
        &overlay, XPerformanceOverlayPosition_BottomRight, 400, 300, 10);
    position = XPerformanceOverlay_position(&overlay);
    XQ_EQ_I64(position.x, 310);
    XQ_EQ_I64(position.y, 250);
    XPerformanceOverlay_setPresetPosition(
        &overlay, XPerformanceOverlayPosition_Center, 400, 300, 10);
    position = XPerformanceOverlay_position(&overlay);
    XQ_EQ_I64(position.x, 160);
    XQ_EQ_I64(position.y, 130);
    /* 越界锚点钳到客户区内（窗口比悬浮层小 ⇒ 原点 0）。 */
    XPerformanceOverlay_setPresetPosition(
        &overlay, XPerformanceOverlayPosition_BottomRight, 60, 30, 4);
    position = XPerformanceOverlay_position(&overlay);
    XQ_EQ_I64(position.x, 0);
    XQ_EQ_I64(position.y, 0);

    XClassDeinit(&overlay);
}

/* ==================== ⑤ 内存行配置 + 父子所有权 ==================== */

XQ_TEST(XPerformanceOverlay, memoryConfigAndOwnership)
{
    XWidget* top = XWidget_create(NULL, 0);
    XPerformanceOverlay* overlay;
    const XVector* children;

    XQ_REQUIRE(top != NULL);
    overlay = XPerformanceOverlay_create(top, 0);
    XQ_REQUIRE(overlay != NULL);

#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    /* 越界枚举按缺省值归一（内部一致性：setter 永不存非法值）。 */
    XPerformanceOverlay_setMemoryDisplay(
        overlay, (XPerformanceOverlayMemoryDisplay)99);
    XQ_EQ_I64(XPerformanceOverlay_memoryDisplay(overlay),
              XPerformanceOverlayMemoryDisplay_Both);
    XPerformanceOverlay_setMemorySource(
        overlay, (XPerformanceOverlayMemorySource)99);
    XQ_EQ_I64(XPerformanceOverlay_memorySource(overlay),
              XPerformanceOverlayMemorySource_Auto);
    XPerformanceOverlay_setMemoryDisplay(
        overlay, XPerformanceOverlayMemoryDisplay_PercentOnly);
    XQ_EQ_I64(XPerformanceOverlay_memoryDisplay(overlay),
              XPerformanceOverlayMemoryDisplay_PercentOnly);
    XPerformanceOverlay_setMemorySource(
        overlay, XPerformanceOverlayMemorySource_Library);
    XQ_EQ_I64(XPerformanceOverlay_memorySource(overlay),
              XPerformanceOverlayMemorySource_Library);
    XPerformanceOverlay_setMemoryVisible(overlay, false);
    XQ_CHECK(!XPerformanceOverlay_isMemoryVisible(overlay));
    XPerformanceOverlay_setMemoryVisible(overlay, true);
    XQ_CHECK(XPerformanceOverlay_isMemoryVisible(overlay));
#endif /* XGUI_PERFORMANCE_OVERLAY_MEMORY_ON */

    /* 显隐开关内部一致性（每个 setter 都应与 getter 往返一致）。 */
    XPerformanceOverlay_setFpsVisible(overlay, false);
    XQ_CHECK(!XPerformanceOverlay_isFpsVisible(overlay));
    XPerformanceOverlay_setFpsVisible(overlay, true);
    XQ_CHECK(XPerformanceOverlay_isFpsVisible(overlay));
    XPerformanceOverlay_setFrameTimeVisible(overlay, false);
    XQ_CHECK(!XPerformanceOverlay_isFrameTimeVisible(overlay));
    XPerformanceOverlay_setNetworkVisible(overlay, false);
    XQ_CHECK(!XPerformanceOverlay_isNetworkVisible(overlay));
    XPerformanceOverlay_setSysStatVisible(overlay, false);
    XQ_CHECK(!XPerformanceOverlay_isSysStatVisible(overlay));

    /* 父子所有权：悬浮层是 top 的子对象，父析构带走它。 */
    children = XObject_children((XObject*)top);
    XQ_CHECK_PTR(children);
    if (children)
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 1u);

    XClassDelete((XClass*)top); /* overlay 随父释放（泄漏门判定） */
}

/* ==================== ⑥ 底板色真实落到绘制输出 ==================== */

XQ_TEST(XPerformanceOverlay, backgroundPaints)
{
    XPerformanceOverlay overlay;
    XImage image;
    XPainter painter;
    XRect geo;
    uint32_t px;

    XPerformanceOverlay_init(&overlay, NULL, 0);
    XImage_init_ex(&image, 80, 50, XImageFormat_ARGB32);
    XImage_fill(&image, 0u);

    /* 控件几何留在图像内：draw() 按 geometry 平移，验证底板填的是
     * 控件矩形而不是整张目标图。 */
    XPerformanceOverlay_setPosition(&overlay, 5, 6);
    XPerformanceOverlay_setSize(&overlay, 60, 30);
    XPerformanceOverlay_setBackgroundColor(&overlay, 0xFF102030u);
    /* 只留一行短文本（"0.0"）：断言像素取在右下角，确保不落在字形上。 */
    XPerformanceOverlay_setFormat(&overlay, "{fps}");
    geo = XPerformanceOverlay_geometry(&overlay);
    XQ_EQ_I64(geo.width, 60);
    XQ_EQ_I64(geo.height, 30);

    XPainter_init(&painter, NULL);
    XQ_CHECK(XPainter_begin_image(&painter, &image));
    XPerformanceOverlay_draw(&overlay, &painter);
    XQ_CHECK(XPainter_end(&painter));
    XPainter_deinit(&painter);

    /* 文本只占左上 margin=4 起的前几行：右下角必为纯底板色。 */
    px = XImage_pixel(&image, 5 + 60 - 1, 6 + 30 - 1);
    XQ_CHECK_MSG(px == 0xFF102030u, "底板应填满控件矩形：0x%08x", px);
    /* 控件矩形之外保持原样（目标图未被整张污染）。 */
    XQ_EQ_I64(XImage_pixel(&image, 0, 0), 0u);
    XQ_EQ_I64(XImage_pixel(&image, 79, 49), 0u);

    /* 同值早退：绘制后内容缓存干净，再设同色不得置脏（AGENTS 红线：
     * 悬浮层每拍刷新不得产生持续标脏）。换色才置脏。 */
    XQ_CHECK_MSG(!overlay.m_base.m_base.m_base.m_contentCacheDirty,
                 "绘制完成后内容缓存应为干净态");
    XPerformanceOverlay_setBackgroundColor(&overlay, 0xFF102030u);
    XQ_CHECK_MSG(!overlay.m_base.m_base.m_base.m_contentCacheDirty,
                 "同色 setBackgroundColor 应早退、不标脏");
    XPerformanceOverlay_setBackgroundColor(&overlay, 0xFFA0B0C0u);
    XQ_CHECK_MSG(overlay.m_base.m_base.m_base.m_contentCacheDirty,
                 "换色应置脏、下一帧重建内容缓存");

    /* 换色后重绘必须生效：setBackgroundColor → XWidget_update 置
     * m_contentCacheDirty，下一帧重渲染缓存而不是回放旧色。 */
    XPainter_init(&painter, NULL);
    XQ_CHECK(XPainter_begin_image(&painter, &image));
    XPerformanceOverlay_draw(&overlay, &painter);
    XQ_CHECK(XPainter_end(&painter));
    XPainter_deinit(&painter);
    px = XImage_pixel(&image, 5 + 60 - 1, 6 + 30 - 1);
    XQ_CHECK_MSG(px == 0xFFA0B0C0u, "换色后重绘应更新底板：0x%08x", px);

    /* NULL 语义（内部一致性：getter 缺省、setter 空操作）。 */
    XQ_EQ_I64(XPerformanceOverlay_backgroundColor(NULL),
              XPERFORMANCEOVERLAY_BACKGROUND_DEFAULT);
    XPerformanceOverlay_setBackgroundColor(NULL, 0xFF00FF00u);

    XClassDeinit(&image);
    XClassDeinit(&overlay);
}

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON */
