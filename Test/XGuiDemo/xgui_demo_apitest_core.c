/* xgui_demo_apitest_core.c —— 控件 API 测试族：core（基类与样式）。
 *
 * 覆盖控件（对标 Qt 6.8.3）：XWidget 基类（几何/显隐/启用/焦点/工具提示/
 * 字体/palette 角色/attribute 标志/windowTitle/minimumSize）+ XStyle +
 * XPalette（颜色角色往返）+ XGraphicsEffect / XGraphicsOpacityEffect /
 * XGraphicsBlurEffect / XGraphicsDropShadowEffect（挂摘/参数往返/类型）。
 *
 * 测试口径（见 xgui_demo_apitest.h 契约）：
 *  - 属性 setter/getter 往返一致；文档/实现确认的默认值直接断言，
 *    不确定的写注释不硬断言（防误报）；
 *  - 无头语义：控件不 show 直接调 API（顶层控件全程不 show，避免无头
 *    环境创建平台桥接窗口；焦点链类断言只 show 子控件，子控件显隐不建
 *    平台窗口）；焦点断言经 XWidget_setFocus 内部的 XObject_event_base
 *    直发 FOCUS_IN 事件（与真实输入同路径）；
 *  - 每条断言中文注释标对标 Qt 的哪个行为；渲染/视觉效果不在断言职责
 *    （图形效果只断言挂摘/参数/包围盒 API 状态，不判像素）。
 */
#include "xgui_demo_apitest.h"

#include <string.h>

#include "XObject.h"
#include "XWidget.h"
#include "XString.h"
#include "XFont.h"
#include "XColor.h"
#include "XVarList.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif
#if XSTYLE_ON
#include "XStyle.h"
#include "XAlignment.h"
#endif
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif
#include "XGraphicsEffect.h"
#include "XGraphicsOpacityEffect.h"
#include "XGraphicsBlurEffect.h"
#include "XGraphicsDropShadowEffect.h"

/* ==================== 信号记录器（对标 QSignalSpy 的最小等价物） ==================== */

/** @brief core 族信号计数与最近载荷（断言前重置）。 */
static struct
{
    int titleChanged;      /**< XWidget::windowTitleChanged 次数。 */
    char titleText[64];    /**< 最近 windowTitleChanged 文本（UTF-8）。 */
    int effectEnabled;     /**< XGraphicsEffect::enabledChanged 次数。 */
    int effectEnabledOn;   /**< 最近 enabledChanged 载荷（0/1）。 */
} g_core_sig;

static void core_sig_reset(void)
{
    memset(&g_core_sig, 0, sizeof(g_core_sig));
}

/** @brief windowTitleChanged(title) 载荷为 XString*（见 XWidget.c 发射端）。 */
static void core_titleChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    {
        XVarList_args_1(args, XString*, title);
        ++g_core_sig.titleChanged;
        g_core_sig.titleText[0] = '\0';
        if (title) {
            const char* utf8 = XString_c_str(title);
            size_t i;
            if (utf8) {
                for (i = 0; i + 1 < sizeof(g_core_sig.titleText) && utf8[i];
                     ++i)
                    g_core_sig.titleText[i] = utf8[i];
                g_core_sig.titleText[i] = '\0';
            }
        }
    }
}

/** @brief enabledChanged(bool) 载荷为 bool（见 XGraphicsEffect.c 发射端）。 */
static void core_effectEnabledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    {
        XVarList_args_1(args, bool, enabled);
        ++g_core_sig.effectEnabled;
        g_core_sig.effectEnabledOn = enabled ? 1 : 0;
    }
}

/* ==================== 入口 ==================== */

int xapi_core_run(void)
{
    int failures = 0;

#if XWIDGET_ON

    XWidget* top;    /* 顶层控件（全程不 show：避免无头建平台窗口）。 */
    XWidget* child;  /* 子控件 A（几何/属性/焦点主载体）。 */
    XWidget* childB; /* 子控件 B（焦点次载体/效果挂载载体）。 */
    XFont font;
    XFont fontCopy;
    XPalette palette;
    XColor color;
#if XSTYLE_ON
    XStyle* style;
    XRect bounding;
    XRect logical;
    XRect visual;
    XRect aligned;
    XSize content;
#endif
    XGraphicsEffect* baseEffect;
    XGraphicsOpacityEffect* opacity;
    XGraphicsBlurEffect* blur;
    XGraphicsDropShadowEffect* shadow;
    XRectF srcF;
    XRectF grownF;

    core_sig_reset();

    /* ---- 公共夹具：顶层 + 两个子控件（堆创建，顶层析构级联释放）。 ---- */
    top = XWidget_create(NULL, 0);
    child = XWidget_create(top, 0);
    childB = XWidget_create(top, 0);
    if (!top || !child || !childB) {
        XPrintf("XGuiApiTest: [FAIL] core 夹具创建失败（内存不足）\n");
        if (top) XWidget_delete_base(top);
        if (child) XWidget_delete_base(child);
        if (childB) XWidget_delete_base(childB);
        return 1;
    }

    /* ================================================================
     * 1. XWidget 几何：默认值/往返/move/resize/钳位/内容边距/固定尺寸。
     * ================================================================ */

    /* ---- 默认几何（Qt：QWidgetPrivate::init 预置几何——子控件
     *      100x30、顶层 640x480，首个 setGeometry 前的默认值）。 ---- */
    XAPI_EXPECT(XWidget_x(child) == 0 && XWidget_y(child) == 0 &&
                    XWidget_width(child) == 100 && XWidget_height(child) == 30,
                "子控件默认几何 100x30 @(0,0)");
    XAPI_EXPECT(XWidget_width(top) == 640 && XWidget_height(top) == 480,
                "顶层控件默认几何 640x480");

    /* ---- setGeometry 四参往返（对标 QWidget::setGeometry(int,int,int,int)）。 ---- */
    XWidget_setGeometry(child, 10, 20, 120, 50);
    XAPI_EXPECT(XWidget_x(child) == 10 && XWidget_y(child) == 20 &&
                    XWidget_width(child) == 120 && XWidget_height(child) == 50,
                "setGeometry(10,20,120,50) 往返一致");
    /* pos/size 读取（对标 QWidget::pos/size 均来自同一几何矩形）。 */
    XAPI_EXPECT(XWidget_pos(child).x == 10 && XWidget_pos(child).y == 20,
                "pos()=(10,20) 与 geometry 同源");
    XAPI_EXPECT(XWidget_size(child).width == 120 &&
                    XWidget_size(child).height == 50,
                "size()=120x50 与 geometry 同源");
    /* rect 恒以 (0,0) 为原点（对标 QWidget::rect 语义）。 */
    XAPI_EXPECT(XWidget_rect(child).x == 0 && XWidget_rect(child).y == 0 &&
                    XWidget_rect(child).width == 120 &&
                    XWidget_rect(child).height == 50,
                "rect()=(0,0,120,50) 恒从原点起");

    /* ---- move 保持尺寸（对标 QWidget::move：仅位置变化）。 ---- */
    XWidget_move(child, 33, 44);
    XAPI_EXPECT(XWidget_x(child) == 33 && XWidget_y(child) == 44 &&
                    XWidget_width(child) == 120 && XWidget_height(child) == 50,
                "move(33,44) 位置更新且尺寸不变");
    /* move(QPoint) 值类型重载（对标 QWidget::move(const QPoint&)）。 */
    {
        XPoint p;
        XPoint_init(&p, 7, 8);
        XWidget_movePoint(child, &p);
        XAPI_EXPECT(XWidget_x(child) == 7 && XWidget_y(child) == 8,
                    "movePoint(QPoint) 值重载往返");
    }

    /* ---- resize 保持位置（对标 QWidget::resize：仅尺寸变化）。 ---- */
    XWidget_resize(child, 88, 66);
    XAPI_EXPECT(XWidget_width(child) == 88 && XWidget_height(child) == 66 &&
                    XWidget_x(child) == 7 && XWidget_y(child) == 8,
                "resize(88,66) 尺寸更新且位置不变");
    /* resize(QSize) 值类型重载（对标 QWidget::resize(const QSize&)）。 */
    {
        XSize s;
        XSize_init(&s, 60, 40);
        XWidget_resizeSize(child, &s);
        XAPI_EXPECT(XWidget_width(child) == 60 && XWidget_height(child) == 40,
                    "resizeSize(QSize) 值重载往返");
    }
    /* setGeometry(QRect) 值类型重载（对标 setGeometry(const QRect&)）。 */
    {
        XRect r;
        XRect_init(&r, 12, 14, 90, 46);
        XWidget_setGeometryRect(child, &r);
        XAPI_EXPECT(XWidget_x(child) == 12 && XWidget_y(child) == 14 &&
                        XWidget_width(child) == 90 &&
                        XWidget_height(child) == 46,
                    "setGeometryRect(QRect) 值重载往返");
    }

    /* ---- 子控件无框架差异：frameGeometry==geometry（对标 QWidget
     *      文档 frameGeometry()：普通子控件与 geometry() 相同）。 ---- */
    XAPI_EXPECT(XWidget_frameGeometry(child).x == 12 &&
                    XWidget_frameGeometry(child).width == 90,
                "子控件 frameGeometry==geometry");
    XAPI_EXPECT(XWidget_frameSize(child).width == 90 &&
                    XWidget_frameSize(child).height == 46,
                "frameSize 与几何同源");

    /* ---- 内容边距（对标 QWidget::setContentsMargins/contentsRect：
     *      contentsRect = rect 扣除四边边距）。 ---- */
    XAPI_EXPECT(XWidget_contentsMargins(child).left == 0 &&
                    XWidget_contentsMargins(child).top == 0 &&
                    XWidget_contentsMargins(child).right == 0 &&
                    XWidget_contentsMargins(child).bottom == 0,
                "内容边距默认全 0");
    XWidget_setContentsMargins(child, 2, 4, 6, 8);
    XAPI_EXPECT(XWidget_contentsMargins(child).left == 2 &&
                    XWidget_contentsMargins(child).bottom == 8,
                "setContentsMargins(2,4,6,8) 往返");
    XAPI_EXPECT(XWidget_contentsRect(child).x == 2 &&
                    XWidget_contentsRect(child).y == 4 &&
                    XWidget_contentsRect(child).width == 90 - 8 &&
                    XWidget_contentsRect(child).height == 46 - 12,
                "contentsRect 扣除四边边距");
    XWidget_unsetContentsMargins(child);
    XAPI_EXPECT(XWidget_contentsMargins(child).left == 0 &&
                    XWidget_contentsMargins(child).right == 0,
                "unsetContentsMargins 复位全 0");

    /* ---- 尺寸约束默认值（Qt：minimum (0,0)、maximum QWIDGETSIZE_MAX
     *      =16777215、base (-1,-1)、increment (0,0)、hint 无效 (-1,-1)）。 ---- */
    XAPI_EXPECT(XWidget_minimumWidth(child) == 0 &&
                    XWidget_minimumHeight(child) == 0,
                "minimumSize 默认 (0,0)");
    XAPI_EXPECT(XWidget_maximumWidth(child) == XWIDGET_MAX_SIZE &&
                    XWidget_maximumHeight(child) == XWIDGET_MAX_SIZE,
                "maximumSize 默认 16777215（QWIDGETSIZE_MAX）");
    XAPI_EXPECT(XWidget_baseSize(child).width == -1 &&
                    XWidget_baseSize(child).height == -1,
                "baseSize 默认 (-1,-1)");
    XWidget_setBaseSize(child, 30, 20);
    XAPI_EXPECT(XWidget_baseSize(child).width == 30 &&
                    XWidget_baseSize(child).height == 20,
                "setBaseSize 往返");
    XAPI_EXPECT(XWidget_sizeIncrement(child).width == 0 &&
                    XWidget_sizeIncrement(child).height == 0,
                "sizeIncrement 默认 (0,0)");
    XWidget_setSizeIncrement(child, 4, 6);
    XAPI_EXPECT(XWidget_sizeIncrement(child).width == 4 &&
                    XWidget_sizeIncrement(child).height == 6,
                "setSizeIncrement 往返");
    XAPI_EXPECT(XWidget_sizeHint(child).width == -1 &&
                    XWidget_sizeHint(child).height == -1,
                "sizeHint 默认无效 (-1,-1)");
    XAPI_EXPECT(XWidget_minimumSizeHint(child).width == -1 &&
                    XWidget_minimumSizeHint(child).height == -1,
                "minimumSizeHint 默认无效 (-1,-1)");

    /* ---- 尺寸策略默认 Preferred/Preferred（对标 QSizePolicy 默认
     *      构造），setSizePolicy 往返。 ---- */
    XAPI_EXPECT(XWidget_sizePolicy(child).m_horizontalPolicy ==
                        XWidgetSizePolicy_Preferred &&
                    XWidget_sizePolicy(child).m_verticalPolicy ==
                        XWidgetSizePolicy_Preferred,
                "sizePolicy 默认 Preferred/Preferred");
    XWidget_setSizePolicy(child, XWidgetSizePolicy_Fixed,
                          XWidgetSizePolicy_Expanding);
    XAPI_EXPECT(XWidget_sizePolicy(child).m_horizontalPolicy ==
                    XWidgetSizePolicy_Fixed,
                "setSizePolicy 水平策略往返");
    XAPI_EXPECT(XWidget_sizePolicy(child).m_verticalPolicy ==
                    XWidgetSizePolicy_Expanding,
                "setSizePolicy 垂直策略往返");
    XWidget_setSizePolicy(child, XWidgetSizePolicy_Preferred,
                          XWidgetSizePolicy_Preferred);

    /* ---- setMinimumSize：当前几何过小时自动增大（对标 Qt 文档
     *      "the widget will be resized if necessary"）。 ---- */
    XWidget_setGeometry(child, 10, 20, 100, 80);
    XWidget_setMinimumSize(child, 150, 90);
    XAPI_EXPECT(XWidget_minimumWidth(child) == 150 &&
                    XWidget_minimumHeight(child) == 90,
                "setMinimumSize(150,90) 往返");
    XAPI_EXPECT(XWidget_width(child) == 150 && XWidget_height(child) == 90,
                "最小尺寸超过当前几何时自动增大");

    /* ---- setMaximumSize：当前几何超过上限时自动缩小（对称语义）。 ---- */
    XWidget_setMaximumSize(child, 160, 100);
    XAPI_EXPECT(XWidget_maximumWidth(child) == 160 &&
                    XWidget_maximumHeight(child) == 100,
                "setMaximumSize(160,100) 往返");

    /* ---- setGeometry 请求值按 min/max 钳位（对标 setGeometry 文档
     *      "will be adjusted"）。 ---- */
    XWidget_setGeometry(child, 0, 0, 50, 40);
    XAPI_EXPECT(XWidget_width(child) == 150 && XWidget_height(child) == 90,
                "setGeometry 请求过小被最小尺寸托起");
    XWidget_setGeometry(child, 0, 0, 999, 999);
    XAPI_EXPECT(XWidget_width(child) == 160 && XWidget_height(child) == 100,
                "setGeometry 请求过大被最大尺寸压回");

    /* ---- 单轴接口不影响另一轴（对标 setMinimumWidth/setMaximumHeight
     *      仅改对应维度）。 ---- */
    XWidget_setMinimumWidth(child, 155);
    XAPI_EXPECT(XWidget_minimumWidth(child) == 155 &&
                    XWidget_minimumHeight(child) == 90,
                "setMinimumWidth 只改宽度下限");
    XWidget_setMaximumHeight(child, 95);
    XAPI_EXPECT(XWidget_maximumHeight(child) == 95 &&
                    XWidget_maximumWidth(child) == 160,
                "setMaximumHeight 只改高度上限");
    /* QSize 值版本重载（setMinimumSizeSize/setMaximumSizeSize）。 */
    {
        XSize smin;
        XSize smax;
        XSize_init(&smin, 120, 80);
        XSize_init(&smax, 300, 200);
        XWidget_setMinimumSizeSize(child, &smin);
        XWidget_setMaximumSizeSize(child, &smax);
        XAPI_EXPECT(XWidget_minimumWidth(child) == 120 &&
                        XWidget_minimumHeight(child) == 80 &&
                        XWidget_maximumWidth(child) == 300 &&
                        XWidget_maximumHeight(child) == 200,
                    "setMinimum/MaximumSizeSize(QSize) 值重载往返");
    }

    /* ---- 边界值：QWIDGETSIZE_MAX 语义映射为未设置（对标 Qt 文档
     *      setMinimumSize：传 QWIDGETSIZE_MAX 重置最小尺寸）；负值钳 0。 ---- */
    XWidget_setMinimumSize(child, XWIDGET_MAX_SIZE, 55);
    XAPI_EXPECT(XWidget_minimumWidth(child) == 0 &&
                    XWidget_minimumHeight(child) == 55,
                "最小宽传 QWIDGETSIZE_MAX 映射未设置(0)");
    XWidget_setMinimumSize(child, -8, -9);
    XAPI_EXPECT(XWidget_minimumWidth(child) == 0 &&
                    XWidget_minimumHeight(child) == 0,
                "负值最小尺寸钳为 0");
    XWidget_setMaximumSize(child, -1, 400);
    XAPI_EXPECT(XWidget_maximumWidth(child) == 0 &&
                    XWidget_maximumHeight(child) == 400,
                "负值最大宽钳为 0、高度单设生效");
    /* 重复 set 同值幂等（往返稳定，不产生状态漂移）。 */
    XWidget_setMinimumSize(child, 40, 30);
    XWidget_setMinimumSize(child, 40, 30);
    XAPI_EXPECT(XWidget_minimumWidth(child) == 40 &&
                    XWidget_minimumHeight(child) == 30,
                "重复 setMinimumSize 幂等");
    /* 复位约束到默认，供后续段落使用干净状态。 */
    XWidget_setMinimumSize(child, 0, 0);
    XWidget_setMaximumSize(child, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);

    /* ---- setFixedSize：min=max=size 且尺寸生效（对标 setFixedSize）。 ---- */
    XWidget_setFixedSize(child, 90, 70);
    XAPI_EXPECT(XWidget_minimumWidth(child) == 90 &&
                    XWidget_maximumWidth(child) == 90 &&
                    XWidget_minimumHeight(child) == 70 &&
                    XWidget_maximumHeight(child) == 70,
                "setFixedSize 后 min=max=固定值");
    XAPI_EXPECT(XWidget_width(child) == 90 && XWidget_height(child) == 70,
                "setFixedSize 调整当前尺寸生效");
    XWidget_resize(child, 500, 500);
    XAPI_EXPECT(XWidget_width(child) == 90 && XWidget_height(child) == 70,
                "固定尺寸下 resize 请求被钳回");
    /* setFixedWidth：宽度固定、高度约束不变（对标 setFixedWidth）。 ---- */
    XWidget_setFixedWidth(child, 120);
    XAPI_EXPECT(XWidget_minimumWidth(child) == 120 &&
                    XWidget_maximumWidth(child) == 120 &&
                    XWidget_minimumHeight(child) == 70 &&
                    XWidget_width(child) == 120,
                "setFixedWidth(120) 宽固定高约束不变");
    XWidget_setMinimumSize(child, 0, 0);
    XWidget_setMaximumSize(child, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);

    /* ---- adjustSize：无有效提示且无子控件时保持当前尺寸（实现文档
     *      与 Qt adjustedSize 回退路径一致）。 ---- */
    XWidget_setGeometry(child, 10, 20, 123, 45);
    XWidget_adjustSize(child);
    XAPI_EXPECT(XWidget_width(child) == 123 && XWidget_height(child) == 45,
                "adjustSize 无提示无子控件保持尺寸");

    /* ================================================================
     * 2. XWidget 显隐/窗口类型：isVisible/isHidden/isVisibleTo/close/
     *    isWindow/window()/parentWidget()/childAt/isAncestorOf。
     * ================================================================ */

    /* ---- 默认可见性（Qt：新子控件未显式隐藏，但父未 show 前整体
     *      不可见；顶层初始 Hidden 需显式 show）。 ---- */
    XAPI_EXPECT(!XWidget_isVisible(child), "子控件默认不可见（父未显示）");
    XAPI_EXPECT(!XWidget_isHidden(child),
                "新子控件默认未显式隐藏（isHidden=false）");
    XAPI_EXPECT(XWidget_isHidden(top), "顶层控件默认 Hidden（对标 Qt）");

    /* ---- setVisible(true)：相对父控件可见（对标 isVisibleTo），但
     *      生效可见性仍受父链牵制（对标 isVisible 含父链）。 ---- */
    XWidget_setVisible(child, true);
    XAPI_EXPECT(XWidget_isVisibleTo(child, top),
                "setVisible(true) 后 isVisibleTo(父)=true");
    XAPI_EXPECT(!XWidget_isVisible(child),
                "父不可见时 isVisible 仍为 false（可见性含父链）");

    /* ---- hide/show 与 isHidden（对标 QWidget::hide 置显式隐藏）。 ---- */
    XWidget_hide(child);
    XAPI_EXPECT(XWidget_isHidden(child), "hide() 后 isHidden=true");
    XWidget_show(child);
    XAPI_EXPECT(!XWidget_isHidden(child), "show() 后 isHidden=false");
    /* setHidden(true) 等价 hide（对标 setHidden 方便接口）。 */
    XWidget_setHidden(child, true);
    XAPI_EXPECT(XWidget_isHidden(child), "setHidden(true) 等价 hide");
    XWidget_setHidden(child, false);
    XAPI_EXPECT(!XWidget_isHidden(child), "setHidden(false) 等价 show");

    /* ---- close：默认 closeEvent 接受（对标 Qt QCloseEvent 默认接受，
     *      close() 隐藏控件并返回 true）。 ---- */
    XAPI_EXPECT(XWidget_close(child), "close() 默认接受关闭事件返回 true");
    XAPI_EXPECT(XWidget_isHidden(child), "close() 后控件隐藏");
    XWidget_show(child);

    /* ---- 窗口类型（对标 QWidget::isWindow/isTopLevel/windowType）。 ---- */
    XAPI_EXPECT(XWidget_isWindow(top) && !XWidget_isWindow(child),
                "顶层 isWindow=true、子控件=false");
    XAPI_EXPECT(XWidget_isTopLevel(top) == XWidget_isWindow(top),
                "isTopLevel 与 isWindow 同义（历史拼写）");
    XAPI_EXPECT(XWidget_windowType(top) == XWindowType_Window &&
                    XWidget_windowType(child) == XWindowType_Widget,
                "windowType：顶层 Window、子控件 Widget");
    XAPI_EXPECT(XWidget_window(child) == top && XWidget_parentWidget(child) == top,
                "window() 返回顶层、parentWidget() 返回父控件");

    /* ---- childAt 命中测试（对标 QWidget::childAt，父坐标系入参）。
     *      Qt 6.8.3 口径（qwidget.cpp childAtRecursiveHelper）：仅跳过
     *      isHidden()/窗口型/WA_TransparentForMouseEvents 子控件，按几何
     *      命中，不要求生效可见（父未 show 也命中）。childB 从未隐藏且
     *      默认矩形 100x30 @(0,0) 含 (5,5)，故"未命中"探针点须避开全部
     *      未隐藏子矩形。 ---- */
    XWidget_setGeometry(child, 10, 20, 120, 50);
    XAPI_EXPECT(XWidget_childAt_2(top, 50, 40) == child,
                "childAt 命中子控件矩形内部");
    XAPI_EXPECT(XWidget_childAt_2(top, 5, 5) == childB,
                "childAt 命中未显式隐藏的 childB（Qt isHidden 过滤口径）");
    XAPI_EXPECT(XWidget_childAt_2(top, 150, 200) == NULL,
                "childAt 未命中（两子矩形之外）返回 NULL");
    /* isAncestorOf（对标 QWidget::isAncestorOf：含自身）。 ---- */
    XAPI_EXPECT(XWidget_isAncestorOf(top, child) &&
                    XWidget_isAncestorOf(top, top) &&
                    !XWidget_isAncestorOf(child, top),
                "isAncestorOf：祖先判定含自身、反向为 false");

    /* ================================================================
     * 3. XWidget 启用/焦点：isEnabled/传播/isEnabledTo/focusPolicy/
     *    setFocus/clearFocus/focusProxy/Tab 链（子控件 show 后走
     *    focusChainCandidate 可聚焦口径）。
     * ================================================================ */

    XAPI_EXPECT(XWidget_isEnabled(child), "新控件默认启用");
    XWidget_setEnabled(child, false);
    XAPI_EXPECT(!XWidget_isEnabled(child), "setEnabled(false) 禁用");
    XAPI_EXPECT(XWidget_testAttribute(child, XWidgetAttribute_Disabled),
                "禁用置 WA_Disabled（isEnabled=!WA_Disabled 的实现口径）");
    XWidget_setDisabled(child, false); /* setDisabled(false) → setEnabled(true)。 */
    XAPI_EXPECT(XWidget_isEnabled(child), "setDisabled(false) 等价启用");

    /* ---- 启用传播（Qt：禁用父控件传播到未显式禁用的子控件，父恢复
     *      时一并恢复）。 ---- */
    XWidget_setEnabled(top, false);
    XAPI_EXPECT(!XWidget_isEnabled(child),
                "父禁用传播到子控件（isEnabled=false）");
    XAPI_EXPECT(XWidget_isEnabledTo(child, top),
                "isEnabledTo(祖先) 只看显式禁用位（子未显式禁用为 true）");
    XWidget_setEnabled(top, true);
    XAPI_EXPECT(XWidget_isEnabled(child), "父恢复时未显式禁用子一并恢复");

    /* ---- 显式禁用子（WA_ForceDisabled）：父禁用再启用后子保持禁用
     *      （对标 Qt setEnabled 文档的 ForceDisabled 语义）。 ---- */
    XWidget_setEnabled(child, false);
    XAPI_EXPECT(!XWidget_isEnabledTo(child, top),
                "子显式禁用时 isEnabledTo(祖先)=false");
    XWidget_setEnabled(top, false);
    XWidget_setEnabled(top, true);
    XAPI_EXPECT(!XWidget_isEnabled(child),
                "父禁用再启用后显式禁用子保持禁用");
    XWidget_setEnabled(child, true);
    XAPI_EXPECT(XWidget_isEnabled(child), "子恢复启用");

    /* ---- 焦点策略默认 NoFocus（对标 QWidget 默认 focusPolicy）。 ---- */
    XAPI_EXPECT(XWidget_focusPolicy(child) == XWidgetFocusPolicy_NoFocus,
                "focusPolicy 默认 NoFocus");
    XWidget_setFocusPolicy(child, XWidgetFocusPolicy_StrongFocus);
    XWidget_setFocusPolicy(childB, XWidgetFocusPolicy_TabFocus);
    XAPI_EXPECT(XWidget_focusPolicy(child) == XWidgetFocusPolicy_StrongFocus,
                "setFocusPolicy 往返");
    /* 焦点链候选要求已显示（实现口径）：子控件 show 不建平台窗口，
     * 无头安全（顶层始终不 show）。 */
    XWidget_show(child);
    XWidget_show(childB);

    /* ---- setFocus/hasFocus/focusWidget（对标 QWidget 焦点三件套；
     *      setFocus 内部经 XObject_event_base 直发 FOCUS_IN）。 ---- */
    XWidget_setFocus(child);
    XAPI_EXPECT(XWidget_hasFocus(child), "setFocus 后 hasFocus=true");
    XAPI_EXPECT(XWidget_focusWidget(top) == child,
                "focusWidget(顶层) 返回窗口内焦点控件");
    XWidget_setFocus(childB);
    XAPI_EXPECT(!XWidget_hasFocus(child) && XWidget_hasFocus(childB),
                "新焦点抢走旧焦点（单焦点语义）");
    XWidget_clearFocus(childB);
    XAPI_EXPECT(!XWidget_hasFocus(childB) && XWidget_focusWidget(top) == NULL,
                "clearFocus 后无焦点控件");

    /* ---- 焦点代理（对标 setFocusProxy：setFocus 落在最深代理）。 ---- */
    XWidget_setFocusProxy(childB, child);
    XAPI_EXPECT(XWidget_focusProxy(childB) == child, "focusProxy 往返");
    XWidget_setFocus(childB);
    XAPI_EXPECT(XWidget_hasFocus(child),
                "setFocus 经代理落到代理控件（Qt setFocusProxy 语义）");
    XWidget_clearFocus(child);
    XWidget_setFocusProxy(childB, NULL);
    XAPI_EXPECT(XWidget_focusProxy(childB) == NULL, "focusProxy 可清除");

    /* ---- Tab 链（对标 QWidget::setTabOrder + next/previousInFocusChain）。 ---- */
    XWidget_setTabOrder(child, childB);
    XAPI_EXPECT(XWidget_nextInFocusChain(child) == childB,
                "setTabOrder 后 nextInFocusChain(first)=second");
    XAPI_EXPECT(XWidget_previousInFocusChain(childB) == child,
                "previousInFocusChain(second)=first（对称）");
    XAPI_EXPECT(XWidget_focusNextChild(top) &&
                    XWidget_focusWidget(top) != NULL,
                "focusNextChild(顶层) 聚焦到文档序候选（回绕起点语义，"
                "不锚定具体候选防误报）");
    XWidget_clearFocus(child);

    /* ================================================================
     * 4. XWidget 提示/标题/字体/不透明度（含 windowTitleChanged 信号）。
     * ================================================================ */

    /* ---- 工具提示（对标 QWidget::toolTip：默认空；NULL 清除）。 ---- */
    XAPI_EXPECT(XWidget_toolTip(child) == NULL, "toolTip 默认空");
    {
        XString* tip = XString_create_utf8("提示文本");
        XWidget_setToolTip(child, tip);
        XString_delete_base((XClass*)tip);
        XAPI_EXPECT(XWidget_toolTip(child) != NULL &&
                        XString_equals_utf8(XWidget_toolTip(child), "提示文本",
                                            XChar_CaseSensitive),
                    "setToolTip 往返一致");
    }
    {
        XString* empty = XString_create_utf8("");
        XWidget_setToolTip(child, empty);
        XString_delete_base((XClass*)empty);
        XAPI_EXPECT(XWidget_toolTip(child) != NULL &&
                        XString_isEmpty_base(
                            (const XContainer*)XWidget_toolTip(child)),
                    "空串工具提示合法（Qt 空提示）");
    }
    XWidget_setToolTip(child, NULL);
    XAPI_EXPECT(XWidget_toolTip(child) == NULL, "setToolTip(NULL) 清除");
    /* 时长：默认 0=系统默认（对标 toolTipDuration 文档）。 */
    XAPI_EXPECT(XWidget_toolTipDuration(child) == 0,
                "toolTipDuration 默认 0（系统默认）");
    XWidget_setToolTipDuration(child, 800);
    XAPI_EXPECT(XWidget_toolTipDuration(child) == 800, "时长往返 800ms");
    XWidget_setToolTipDuration(child, 0);

    /* ---- 状态提示/What's This/无障碍/样式表/输入法提示（QWidget
     *      文本族属性的存储往返）。 ---- */
    XAPI_EXPECT(XWidget_statusTip(child) == NULL, "statusTip 默认空");
    {
        XString* s = XString_create_utf8("就绪");
        XWidget_setStatusTip(child, s);
        XString_delete_base((XClass*)s);
        XAPI_EXPECT(XWidget_statusTip(child) != NULL &&
                        XString_equals_utf8(XWidget_statusTip(child), "就绪",
                                            XChar_CaseSensitive),
                    "setStatusTip 往返");
    }
    {
        XString* s = XString_create_utf8("这是帮助");
        XWidget_setWhatsThis(child, s);
        XString_delete_base((XClass*)s);
        XAPI_EXPECT(XWidget_whatsThis(child) != NULL &&
                        XString_equals_utf8(XWidget_whatsThis(child), "这是帮助",
                                            XChar_CaseSensitive),
                    "setWhatsThis 往返");
    }
    {
        XString* s = XString_create_utf8("无障碍名");
        XWidget_setAccessibleName(child, s);
        XString_delete_base((XClass*)s);
        XAPI_EXPECT(XWidget_accessibleName(child) != NULL &&
                        XString_equals_utf8(XWidget_accessibleName(child),
                                            "无障碍名", XChar_CaseSensitive),
                    "setAccessibleName 往返（Qt 6.8 QWidget 无障碍属性）");
    }
    XAPI_EXPECT(XWidget_inputMethodHints(child) == 0,
                "inputMethodHints 默认 ImhNone(0)");
    XWidget_setInputMethodHints(child, XInputMethodHint_PreferNumbers |
                                           XInputMethodHint_NoPredictiveText);
    XAPI_EXPECT(XWidget_inputMethodHints(child) ==
                    (XInputMethodHint_PreferNumbers |
                     XInputMethodHint_NoPredictiveText),
                "setInputMethodHints 位组合往返");

    /* ---- 窗口标题 + windowTitleChanged 信号（对标 Qt：setWindowTitle
     *      变化时发射 windowTitleChanged(title)）。 ---- */
    XAPI_EXPECT(XWidget_windowTitle(child) == NULL, "windowTitle 默认空");
    XObject_connect_1((XObject*)child, XSignal(XWidget_windowTitleChanged_signal),
                      (XObject*)child, core_titleChangedSlot,
                      XConnectionType_Direct);
    {
        XString* t = XString_create_utf8("核心测试");
        XWidget_setWindowTitle(child, t);
        XString_delete_base((XClass*)t);
        XAPI_EXPECT(XWidget_windowTitle(child) != NULL &&
                        XString_equals_utf8(XWidget_windowTitle(child),
                                            "核心测试", XChar_CaseSensitive),
                    "setWindowTitle 往返一致");
        XAPI_EXPECT(g_core_sig.titleChanged == 1 &&
                        strcmp(g_core_sig.titleText, "核心测试") == 0,
                    "标题变化发射 windowTitleChanged(新标题)");
        /* 同值重复 set 不再发射（Qt：值不变不发射）。 */
        {
            XString* t2 = XString_create_utf8("核心测试");
            XWidget_setWindowTitle(child, t2);
            XString_delete_base((XClass*)t2);
            XAPI_EXPECT(g_core_sig.titleChanged == 1,
                        "重复同标题不重复发射（变化才发射）");
        }
    }
    XWidget_setWindowTitle(child, NULL);
    XAPI_EXPECT(XWidget_windowTitle(child) == NULL,
                "setWindowTitle(NULL) 清空标题");

    /* ---- 窗口不透明度：默认 1.0、钳制 [0,1]（对标 Qt
     *      windowOpacity 默认不透明且 setter 钳制）。 ---- */
    XAPI_EXPECT(XWidget_windowOpacity(top) == 1.0, "windowOpacity 默认 1.0");
    XWidget_setWindowOpacity(top, 0.5);
    XAPI_EXPECT(XWidget_windowOpacity(top) == 0.5, "setWindowOpacity 往返");
    XWidget_setWindowOpacity(top, 2.0);
    XAPI_EXPECT(XWidget_windowOpacity(top) == 1.0, "不透明度上限钳 1.0");
    XWidget_setWindowOpacity(top, -0.5);
    XAPI_EXPECT(XWidget_windowOpacity(top) == 0.0, "不透明度下限钳 0.0");

    /* ---- 窗口修改标志（对标 setWindowModified，经
     *      WA_WindowModified 属性承载）。 ---- */
    XAPI_EXPECT(!XWidget_isWindowModified(child),
                "isWindowModified 默认 false");
    XWidget_setWindowModified(child, true);
    XAPI_EXPECT(XWidget_isWindowModified(child),
                "setWindowModified(true) 经 WA_WindowModified 生效");
    XWidget_setWindowModified(child, false);

    /* ---- 字体（对标 QWidget::setFont/font：副本深拷贝契约，用后
     *      XFont_deinit_base；默认值随 XFONT_DEFAULT_* 配置不确定，
     *      不硬断言默认家族/字号）。 ---- */
    XFont_init(&font);
    XFont_setFamily(&font, "ApiTestFont");
    XFont_setPixelSize(&font, 20);
    XFont_setBold(&font, true);
    XFont_setItalic(&font, true);
    XWidget_setFont(child, &font);
    XFont_deinit_base((XClass*)&font);
    fontCopy = XWidget_font(child);
    XAPI_EXPECT(strcmp(xapi_cstr(XFont_family(&fontCopy)), "ApiTestFont") == 0,
                "setFont/font 家族往返");
    XAPI_EXPECT(XFont_pixelSize(&fontCopy) == 20, "setFont 字号往返");
    XAPI_EXPECT(XFont_bold(&fontCopy) && XFont_italic(&fontCopy),
                "QFont setBold/setItalic 往返");
    /* fontMetrics/fontInfo 按实现裁定返回与 font 相同的 XFont 副本。 */
    {
        XFont fm = XWidget_fontMetrics(child);
        XAPI_EXPECT(XFont_pixelSize(&fm) == 20,
                    "fontMetrics 与控件字体一致（XFont 值拷贝方案）");
        XFont_deinit_base((XClass*)&fm);
    }
    XFont_deinit_base((XClass*)&fontCopy);

    /* ================================================================
     * 5. XWidget palette 角色 + XPalette 颜色角色往返。
     * ================================================================ */

    /* ---- 角色默认值（对标 QWidget：backgroundRole 默认 Window、
     *      foregroundRole 默认 NoRole）。 ---- */
    XAPI_EXPECT(XWidget_backgroundRole(child) == XPaletteColorRole_Window,
                "backgroundRole 默认 Window");
    XAPI_EXPECT(XWidget_foregroundRole(child) == XPaletteColorRole_NoRole,
                "foregroundRole 默认 NoRole");
    XWidget_setBackgroundRole(child, XPaletteColorRole_Base);
    XWidget_setForegroundRole(child, XPaletteColorRole_Highlight);
    XAPI_EXPECT(XWidget_backgroundRole(child) == XPaletteColorRole_Base &&
                    XWidget_foregroundRole(child) ==
                        XPaletteColorRole_Highlight,
                "背景/前景角色 setter 往返");

    /* ---- 未显式 setPalette 时继承应用调色板（对标 Qt palette()
     *      回落应用调色板；无应用/调色板裁剪时不硬断言，防误报）。 ---- */
#if XPALETTE_ON
    palette = XWidget_palette(child);
#if XGUIAPPLICATION_ON
    {
        XPalette appPalette = XGuiApplication_palette();
        XAPI_EXPECT(XPalette_isEqual(&palette, &appPalette),
                    "未 setPalette 时 palette()=应用调色板");
    }
#else
    {
        XPalette defPalette = XPalette_create();
        XAPI_EXPECT(XPalette_isEqual(&palette, &defPalette),
                    "无应用时 palette()=默认调色板");
    }
#endif
#else
    palette = XWidget_palette(child); /* 调色板裁剪：仅保持赋值无断言。 */
#endif

    /* ---- setPalette 逐角色独立（对标 QPalette::setColor/setColor：
     *      只动目标角色，其余保持默认）。 ---- */
#if XPALETTE_ON
    XPalette_init_default(&palette);
    XColor_init_rgb(&color, 255, 0, 0, 255);
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Button, color);
    XWidget_setPalette(child, &palette);
    {
        XPalette effective = XWidget_palette(child);
        XColor button = XPalette_color(&effective, XPaletteColorGroup_Active,
                                       XPaletteColorRole_Button);
        XColor window = XPalette_color(&effective, XPaletteColorGroup_Active,
                                       XPaletteColorRole_Window);
        XAPI_EXPECT(XColor_red(&button) == 255 && XColor_green(&button) == 0,
                    "setPalette 后 Button 角色读回红色");
        XAPI_EXPECT(XColor_red(&window) == 239 && XColor_green(&window) == 239,
                    "其余角色保持默认（Window 仍 #efefef）");
        /* Current 组写入映射 Active（Qt：setColor(Current) 落 Active）。 */
        XColor_init_rgb(&color, 0, 0, 255, 255);
        XPalette_setColor(&effective, XPaletteColorGroup_Current,
                          XPaletteColorRole_Text, color);
        color = XPalette_color(&effective, XPaletteColorGroup_Active,
                               XPaletteColorRole_Text);
        XAPI_EXPECT(XColor_blue(&color) == 255,
                    "Current 组写入映射到 Active（Qt 语义）");
        /* NoRole 读写不落盘（Qt：color(NoRole) 返回无效色）。 */
        color = XPalette_color(&effective, XPaletteColorGroup_Active,
                               XPaletteColorRole_NoRole);
        XAPI_EXPECT(!XColor_isValid(&color), "color(NoRole) 返回无效颜色");
    }
#endif /* XPALETTE_ON */

    /* ---- XPalette 默认浅色（对标 Qt 6.8 qt_fusionPalette 浅色分支）。 ---- */
#if XPALETTE_ON
    {
        XPalette light = XPalette_create();
        XPalette dark = XPalette_create_dark();
        XPalette copy;
        XColor c;
        c = XPalette_color(&light, XPaletteColorGroup_Active,
                           XPaletteColorRole_Window);
        XAPI_EXPECT(XColor_red(&c) == 239 && XColor_green(&c) == 239 &&
                        XColor_blue(&c) == 239,
                    "浅色 Window=#efefef（fusion 浅色）");
        c = XPalette_color(&light, XPaletteColorGroup_Active,
                           XPaletteColorRole_Base);
        XAPI_EXPECT(XColor_red(&c) == 255 && XColor_green(&c) == 255 &&
                        XColor_blue(&c) == 255,
                    "浅色 Base=白");
        c = XPalette_color(&light, XPaletteColorGroup_Active,
                           XPaletteColorRole_Highlight);
        XAPI_EXPECT(XColor_red(&c) == 48 && XColor_green(&c) == 140 &&
                        XColor_blue(&c) == 198,
                    "浅色 Highlight=#308cc6");
        c = XPalette_color(&light, XPaletteColorGroup_Disabled,
                           XPaletteColorRole_WindowText);
        XAPI_EXPECT(XColor_red(&c) == 190 && XColor_green(&c) == 190,
                    "浅色禁用组 WindowText=#bebebe");
        /* Active/Inactive 独立存储（Qt：两组各自可设）。 */
        XColor_init_rgb(&c, 10, 20, 30, 255);
        XPalette_setColor(&light, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        c = XPalette_color(&light, XPaletteColorGroup_Active,
                           XPaletteColorRole_Window);
        XAPI_EXPECT(XColor_red(&c) == 239,
                    "Inactive 组写入不影响 Active（组间独立）");
        /* 拷贝相等/修改不等（对标 QPalette::operator==）。 */
        XPalette_copy(&copy, &light);
        XAPI_EXPECT(XPalette_isEqual(&copy, &light), "XPalette_copy 逐单元相等");
        XColor_init_rgb(&c, 0, 255, 0, 255);
        XPalette_setColor(&copy, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XAPI_EXPECT(!XPalette_isEqual(&copy, &light),
                    "修改单角色后 operator== 不等");
        /* 深色（对标 Qt 6.5+ StandardPalette 深色语义）。 */
        c = XPalette_color(&dark, XPaletteColorGroup_Active,
                           XPaletteColorRole_Window);
        XAPI_EXPECT(XColor_red(&c) == 50 && XColor_green(&c) == 50 &&
                        XColor_blue(&c) == 50,
                    "深色 Window=#323232");
        c = XPalette_color(&dark, XPaletteColorGroup_Active,
                           XPaletteColorRole_Base);
        XAPI_EXPECT(XColor_red(&c) == 35 && XColor_green(&c) == 35,
                    "深色 Base=#232323");
        c = XPalette_color(&dark, XPaletteColorGroup_Active,
                           XPaletteColorRole_Text);
        XAPI_EXPECT(XColor_red(&c) == 240 && XColor_green(&c) == 240,
                    "深色 Text=#f0f0f0");
    }
#endif /* XPALETTE_ON */

    /* ================================================================
     * 6. XWidget attribute 标志（往返 + 便捷查询联动 + NULL 安全）。
     * ================================================================ */

    /* WA_MouseTracking 默认关（Qt：新控件无鼠标跟踪）。 */
    XAPI_EXPECT(!XWidget_testAttribute(child, XWidgetAttribute_MouseTracking) &&
                    !XWidget_hasMouseTracking(child),
                "WA_MouseTracking 默认 false 且便捷查询一致");
    XWidget_setAttribute(child, XWidgetAttribute_MouseTracking, true);
    XAPI_EXPECT(XWidget_testAttribute(child, XWidgetAttribute_MouseTracking) &&
                    XWidget_hasMouseTracking(child),
                "setAttribute(MouseTracking) 与 hasMouseTracking 联动");
    XWidget_setMouseTracking(child, false);
    XAPI_EXPECT(!XWidget_testAttribute(child, XWidgetAttribute_MouseTracking),
                "setMouseTracking(false) 反向同步属性位");

    /* WA_AcceptDrops 默认关（Qt：默认不接受拖放）。 */
    XAPI_EXPECT(!XWidget_acceptDrops(child) &&
                    !XWidget_testAttribute(child, XWidgetAttribute_AcceptDrops),
                "acceptDrops 默认 false");
    XWidget_setAcceptDrops(child, true);
    XAPI_EXPECT(XWidget_testAttribute(child, XWidgetAttribute_AcceptDrops),
                "setAcceptDrops(true) 同步 WA_AcceptDrops");

    /* 纯属性位往返：Hover/TransparentForMouseEvents/OpaquePaintEvent。 */
    XAPI_EXPECT(!XWidget_testAttribute(child, XWidgetAttribute_Hover),
                "WA_Hover 默认 false");
    XWidget_setAttribute(child, XWidgetAttribute_Hover, true);
    XWidget_setAttribute(child, XWidgetAttribute_TransparentForMouseEvents,
                         true);
    XAPI_EXPECT(XWidget_testAttribute(child, XWidgetAttribute_Hover) &&
                    XWidget_testAttribute(child,
                                          XWidgetAttribute_TransparentForMouseEvents),
                "Hover/TransparentForMouseEvents 置位往返");
    XWidget_setAttribute(child, XWidgetAttribute_Hover, false);
    XWidget_setAttribute(child, XWidgetAttribute_TransparentForMouseEvents,
                         false);
    XAPI_EXPECT(!XWidget_testAttribute(child, XWidgetAttribute_Hover),
                "属性位可关闭（复位生效）");

    XWidget_setAttribute(child, XWidgetAttribute_OpaquePaintEvent, true);
    XAPI_EXPECT(XWidget_testAttribute(child, XWidgetAttribute_OpaquePaintEvent),
                "WA_OpaquePaintEvent 往返");
    XWidget_setAttribute(child, XWidgetAttribute_OpaquePaintEvent, false);

    /* WA_DeleteOnClose 存储位（Qt：关闭即删除的开关属性）。 */
    XWidget_setAttribute(child, XWidgetAttribute_DeleteOnClose, true);
    XAPI_EXPECT(XWidget_testAttribute(child, XWidgetAttribute_DeleteOnClose),
                "WA_DeleteOnClose 存储往返");
    XWidget_setAttribute(child, XWidgetAttribute_DeleteOnClose, false);

    /* updatesEnabled 与 WA_UpdatesDisabled 双向联动（Qt：setUpdatesEnabled
     * 置 WA_UpdatesDisabled）。 */
    XAPI_EXPECT(XWidget_updatesEnabled(child), "updatesEnabled 默认 true");
    XWidget_setUpdatesEnabled(child, false);
    XAPI_EXPECT(!XWidget_updatesEnabled(child) &&
                    XWidget_testAttribute(child,
                                          XWidgetAttribute_UpdatesDisabled),
                "setUpdatesEnabled(false) 置 WA_UpdatesDisabled");
    XWidget_setUpdatesEnabled(child, true);
    XAPI_EXPECT(XWidget_updatesEnabled(child) &&
                    !XWidget_testAttribute(child,
                                           XWidgetAttribute_UpdatesDisabled),
                "恢复更新使能后属性位清除");
    /* WA_TabletTracking 便捷接口（对标 setTabletTracking）。 */
    XWidget_setTabletTracking(child, true);
    XAPI_EXPECT(XWidget_hasTabletTracking(child), "板绘跟踪往返");
    XWidget_setTabletTracking(child, false);
    /* NULL 安全（契约：控件可为 NULL，查询返回 false/不操作）。 */
    XWidget_setAttribute(NULL, XWidgetAttribute_Hover, true);
    XAPI_EXPECT(!XWidget_testAttribute(NULL, XWidgetAttribute_Hover),
                "NULL 控件 setAttribute 不操作、testAttribute 返回 false");

    /* ================================================================
     * 7. XStyle：标识/代理/标准调色板/静态工具（对标 QStyle 公共面）。
     * ================================================================ */
#if XSTYLE_ON
    XAPI_EXPECT(XStyle_defaultStyle() != NULL,
                "全局默认样式存在（对标 QApplication::setStyle 体系）");
    style = XStyle_create();
    if (style) {
        XAPI_EXPECT(strcmp(xapi_cstr(XStyle_name(style)), "XStyle") == 0,
                    "XStyle::name 返回虚表类名（对标 QStyle::name）");
        XAPI_EXPECT(XStyle_proxy(style) == NULL,
                    "普通样式无代理（对标 QStyle::proxy）");
        /* NULL 安全（契约：样式指针可为 NULL）。 */
        XAPI_EXPECT(strcmp(xapi_cstr(XStyle_name(NULL)), "") == 0 &&
                        XStyle_proxy(NULL) == NULL,
                    "name(NULL)/proxy(NULL) 返回空串/空指针");
        /* standardPalette 基类回退基准色（对标 QStyle::standardPalette）。 */
        {
            XPalette sp = XStyle_standardPalette(style);
            XColor c = XPalette_color(&sp, XPaletteColorGroup_Active,
                                      XPaletteColorRole_Window);
            XAPI_EXPECT(XColor_red(&c) == 212 && XColor_green(&c) == 208,
                        "standardPalette Window=#D4D0C8 基准色");
            c = XPalette_color(&sp, XPaletteColorGroup_Active,
                               XPaletteColorRole_Base);
            XAPI_EXPECT(XColor_red(&c) == 255 && XColor_green(&c) == 255,
                        "standardPalette Base=白");
            c = XPalette_color(&sp, XPaletteColorGroup_Active,
                               XPaletteColorRole_WindowText);
            XAPI_EXPECT(XColor_red(&c) == 0 && XColor_green(&c) == 0,
                        "standardPalette WindowText=黑");
        }
        /* 基类无 LayoutSpacing 实现：合并间距返回 0（实现文档裁定，
         * 对标 Qt qMax(-1,0) 的下限行为）。 */
        XAPI_EXPECT(XStyle_combinedLayoutSpacing(style, 0, 0, 0, NULL, NULL) == 0,
                    "无实现注册时 combinedLayoutSpacing=0");
        /* polish/unpolish 基类空实现：调用不崩、控件状态不受扰。 */
        XStyle_polish(style, child);
        XStyle_unpolish(style, child);
        XAPI_EXPECT(XWidget_isEnabled(child),
                    "基类 polish/unpolish 空实现不改变控件状态");
        XStyle_delete_base(style);
    }
    else {
        XAPI_EXPECT(0, "XStyle_create 失败");
    }

    /* ---- 静态工具：visualRect/alignedRect/滑块换算（对标 QStyle
     *      同名静态函数）。 ---- */
    XRect_init(&bounding, 0, 0, 100, 50);
    XRect_init(&logical, 10, 20, 30, 5);
    /* LTR 下视觉矩形与逻辑矩形一致（对标 visualRect(LeftToRight) 恒等）。 */
    visual = XStyle_visualRect(XWidgetLayoutDirection_LeftToRight, &bounding,
                               &logical);
    XAPI_EXPECT(visual.x == 10 && visual.y == 20 && visual.width == 30,
                "visualRect LTR 恒等");
    /* RTL 水平镜像：x' = bounding.x + bounding.w - logical.x - logical.w。 */
    visual = XStyle_visualRect(XWidgetLayoutDirection_RightToLeft, &bounding,
                               &logical);
    XAPI_EXPECT(visual.x == 60 && visual.y == 20 && visual.width == 30,
                "visualRect RTL 水平镜像");
    /* visualPos RTL（对标 QStyle::visualPos：x' = bounding.right()-x）。 */
    {
        XPoint lp;
        XPoint rp;
        XPoint_init(&lp, 10, 20);
        rp = XStyle_visualPos(XWidgetLayoutDirection_RightToLeft, &bounding,
                              &lp);
        XAPI_EXPECT(rp.x == 99 - 10 && rp.y == 20,
                    "visualPos RTL 以包围盒右缘为基准镜像");
    }
    XSize_init(&content, 10, 5);
    aligned = XStyle_alignedRect(XWidgetLayoutDirection_LeftToRight,
                                 XAlignment_Right | XAlignment_VCenter,
                                 &content, &bounding);
    /* y 按本实现 h/2 - s/2 的整数除法次序为 23（Qt 的 (h-s)/2 单次
     * 除法为 22，奇数差 1 的实现口径差异如实记录，不按 Qt 硬造）。 */
    XAPI_EXPECT(aligned.x == 90 && aligned.y == 23 && aligned.width == 10 &&
                    aligned.height == 5,
                "alignedRect 右对齐垂直居中（x=90 右贴齐）");
    XAPI_EXPECT(XStyle_sliderPositionFromValue(0, 10, 5, 100, false) == 50,
                "sliderPositionFromValue 线性映射（值 5/10 → 50px）");
    XAPI_EXPECT(XStyle_sliderValueFromPosition(0, 10, 50, 100, false) == 5,
                "sliderValueFromPosition 逆映射（50px → 值 5）");
#endif /* XSTYLE_ON */

    /* ================================================================
     * 8. 图形效果：基类状态/信号/包围盒 + 三效果参数往返 + 挂摘与类型。
     * ================================================================ */

    /* ---- 基类 XGraphicsEffect（对标 QGraphicsEffect：默认启用、
     *      enabledChanged(bool) 信号、无源包围盒为空）。 ---- */
    baseEffect = XGraphicsEffect_create();
    if (baseEffect) {
        XObject_connect_1(
            (XObject*)baseEffect,
            XSignal(XGraphicsEffect_enabledChanged_signal),
            (XObject*)baseEffect, core_effectEnabledSlot,
            XConnectionType_Direct);
        XAPI_EXPECT(XGraphicsEffect_isEnabled(baseEffect),
                    "效果默认启用（Qt QGraphicsEffect 默认）");
        XGraphicsEffect_setEnabled(baseEffect, false);
        XAPI_EXPECT(!XGraphicsEffect_isEnabled(baseEffect),
                    "setEnabled(false) 状态迁移");
        XAPI_EXPECT(g_core_sig.effectEnabled == 1 &&
                        g_core_sig.effectEnabledOn == 0,
                    "禁用发射 enabledChanged(false)");
        XGraphicsEffect_setEnabled(baseEffect, true);
        XAPI_EXPECT(g_core_sig.effectEnabled == 2 &&
                        g_core_sig.effectEnabledOn == 1,
                    "启用发射 enabledChanged(true)");
        XGraphicsEffect_setEnabled(baseEffect, true);
        XAPI_EXPECT(g_core_sig.effectEnabled == 2,
                    "同值 setEnabled 不重复发射（状态不变）");
        XAPI_EXPECT(XGraphicsEffect_source(baseEffect) == NULL,
                    "未挂接效果无 source");
        {
            XRectF bounds = XGraphicsEffect_boundingRect(baseEffect);
            XAPI_EXPECT(bounds.width == 0.0f && bounds.height == 0.0f,
                        "无源 boundingRect 为空矩形（Qt 语义）");
        }
        {
            XRectF in;
            XRectF out;
            in.x = 10.0f;
            in.y = 20.0f;
            in.width = 100.0f;
            in.height = 50.0f;
            out = XGraphicsEffect_boundingRectFor(baseEffect, &in);
            XAPI_EXPECT(out.x == 10.0f && out.y == 20.0f &&
                            out.width == 100.0f && out.height == 50.0f,
                        "基类 boundingRectFor 原样返回源矩形（Qt 基类一致）");
        }
        XGraphicsEffect_delete_base(baseEffect);
    }
    else {
        XAPI_EXPECT(0, "XGraphicsEffect_create 失败");
    }
    XAPI_EXPECT(!XGraphicsEffect_isEnabled(NULL),
                "isEnabled(NULL) 返回 false（NULL 安全契约）");

    /* ---- 类型身份（XGui 以共享虚表指针承载类型信息；对标 Qt 的
     *      qgraphicsobject_cast 类型判别诉求）。 ---- */
    opacity = XGraphicsOpacityEffect_create();
    blur = XGraphicsBlurEffect_create();
    shadow = XGraphicsDropShadowEffect_create();
    XAPI_EXPECT(opacity && blur && shadow, "三种内置效果创建成功");
    if (opacity && blur && shadow) {
        XAPI_EXPECT(XClassGetVtable(opacity) ==
                        XGraphicsOpacityEffect_class_init(),
                    "不透明度效果类型身份（共享虚表指针）");
        XAPI_EXPECT(XClassGetVtable(blur) == XGraphicsBlurEffect_class_init(),
                    "模糊效果类型身份");
        XAPI_EXPECT(XClassGetVtable(shadow) ==
                        XGraphicsDropShadowEffect_class_init(),
                    "投影效果类型身份");
        XAPI_EXPECT(XClassGetVtable(opacity) != XGraphicsEffect_class_init(),
                    "派生效果与基类类型可区分");

        /* ---- XGraphicsOpacityEffect：默认 1.0、钳制 [0,1]（对标
         *      QGraphicsOpacityEffect）。 ---- */
        XAPI_EXPECT(XGraphicsOpacityEffect_opacity(opacity) == 1.0f,
                    "不透明度默认 1.0（Qt 默认）");
        XGraphicsOpacityEffect_setOpacity(opacity, 0.3f);
        XAPI_EXPECT(XGraphicsOpacityEffect_opacity(opacity) == 0.3f,
                    "setOpacity(0.3) 往返");
        XGraphicsOpacityEffect_setOpacity(opacity, -1.0f);
        XAPI_EXPECT(XGraphicsOpacityEffect_opacity(opacity) == 0.0f,
                    "setOpacity 负值钳 0（Qt 钳 [0,1]）");
        XGraphicsOpacityEffect_setOpacity(opacity, 2.0f);
        XAPI_EXPECT(XGraphicsOpacityEffect_opacity(opacity) == 1.0f,
                    "setOpacity 超界钳 1（Qt 钳 [0,1]）");
        XGraphicsOpacityEffect_setOpacity(opacity, 0.75f);

        /* ---- XGraphicsBlurEffect：默认 1.0、非负钳制；半径仅 API
         *      对齐（固定 3x3 盒式核，Qt 为随半径变化的高斯近似——
         *      实现声明差异，不判渲染像素）。 ---- */
        XAPI_EXPECT(XGraphicsBlurEffect_blurRadius(blur) == 1.0f,
                    "模糊半径默认 1.0（Qt 默认）");
        XGraphicsBlurEffect_setBlurRadius(blur, 5.0f);
        XAPI_EXPECT(XGraphicsBlurEffect_blurRadius(blur) == 5.0f,
                    "setBlurRadius(5) 往返（API 对齐）");
        XGraphicsBlurEffect_setBlurRadius(blur, -2.0f);
        XAPI_EXPECT(XGraphicsBlurEffect_blurRadius(blur) == 0.0f,
                    "setBlurRadius 负值钳 0");
        XGraphicsBlurEffect_setBlurRadius(blur, 2.0f);
        /* 包围盒外扩生长（对标 boundingRectFor 覆盖语义：随核扩散）。 */
        srcF.x = 10.0f;
        srcF.y = 20.0f;
        srcF.width = 100.0f;
        srcF.height = 50.0f;
        grownF = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &srcF);
        XAPI_EXPECT(grownF.x < srcF.x && grownF.y < srcF.y,
                    "模糊包围盒四周外扩生长");
        XAPI_EXPECT(grownF.width == srcF.width + 2.0f * (srcF.x - grownF.x) &&
                        grownF.height ==
                            srcF.height + 2.0f * (srcF.y - grownF.y),
                    "模糊包围盒宽高对称生长");

        /* ---- XGraphicsDropShadowEffect：默认 offset(8,8)/blurRadius
         *      1.0/color(63,63,63,180)（对标 Qt 6.8 默认值）。 ---- */
        {
            XPointF off = XGraphicsDropShadowEffect_offset(shadow);
            XColor sc;
            XAPI_EXPECT(off.x == 8.0f && off.y == 8.0f,
                        "投影默认 offset=(8,8)（Qt 6.8 默认）");
            XAPI_EXPECT(XGraphicsDropShadowEffect_blurRadius(shadow) == 1.0f,
                        "投影默认 blurRadius=1.0");
            sc = XGraphicsDropShadowEffect_color(shadow);
            XAPI_EXPECT(XColor_red(&sc) == 63 && XColor_green(&sc) == 63 &&
                            XColor_blue(&sc) == 63 && XColor_alpha(&sc) == 180,
                        "投影默认 color=QColor(63,63,63,180)");
            /* 参数往返（对标 setOffset/setBlurRadius/setColor）。 */
            {
                XPointF off2;
                XPointF_init(&off2, -3.5f, 12.0f);
                XGraphicsDropShadowEffect_setOffset(shadow, off2);
                off2 = XGraphicsDropShadowEffect_offset(shadow);
                XAPI_EXPECT(off2.x == -3.5f && off2.y == 12.0f,
                            "setOffset 往返（含负偏移）");
            }
            XGraphicsDropShadowEffect_setBlurRadius(shadow, 4.0f);
            XAPI_EXPECT(XGraphicsDropShadowEffect_blurRadius(shadow) == 4.0f,
                        "投影 setBlurRadius 往返");
            XGraphicsDropShadowEffect_setBlurRadius(shadow, -1.0f);
            XAPI_EXPECT(XGraphicsDropShadowEffect_blurRadius(shadow) == 0.0f,
                        "投影 setBlurRadius 负值钳 0");
            {
                XColor c2;
                XColor_init_rgb(&c2, 255, 0, 0, 200);
                XGraphicsDropShadowEffect_setColor(shadow, c2);
                sc = XGraphicsDropShadowEffect_color(shadow);
                XAPI_EXPECT(XColor_red(&sc) == 255 && XColor_alpha(&sc) == 200,
                            "setColor 往返");
            }
        }

        /* ---- 挂摘（对标 QWidget::setGraphicsEffect：控件取得所有权、
         *      挂接建立 source() 关联、重复设置同效果不动作、换装删除
         *      旧效果、NULL 摘除）。 ---- */
        XGraphicsOpacityEffect_setOpacity(opacity, 0.5f);
        XWidget_setGraphicsEffect(childB, (XGraphicsEffect*)opacity);
        XAPI_EXPECT(XWidget_graphicsEffect(childB) == (XGraphicsEffect*)opacity,
                    "setGraphicsEffect 安装后 graphicsEffect 返回同指针");
        XAPI_EXPECT(XGraphicsEffect_source((XGraphicsEffect*)opacity) == childB,
                    "挂接建立 source() 关联（Qt 语义）");
        XWidget_setGraphicsEffect(childB, (XGraphicsEffect*)opacity);
        XAPI_EXPECT(XWidget_graphicsEffect(childB) == (XGraphicsEffect*)opacity,
                    "重复设置同一效果不动作（指针不变）");
        /* 换装：旧效果（opacity）由框架释放（Qt：删除已装效果），此后
         * 不得再触碰 opacity 指针；blur 顶替成为当前效果。 */
        XWidget_setGraphicsEffect(childB, (XGraphicsEffect*)blur);
        XAPI_EXPECT(XWidget_graphicsEffect(childB) == (XGraphicsEffect*)blur,
                    "换装后当前效果为新效果");
        XAPI_EXPECT(XGraphicsEffect_source((XGraphicsEffect*)blur) == childB,
                    "新效果挂接同样建立 source 关联");
        /* NULL 摘除：blur 由框架释放，graphicsEffect 复位 NULL。 */
        XWidget_setGraphicsEffect(childB, NULL);
        XAPI_EXPECT(XWidget_graphicsEffect(childB) == NULL,
                    "setGraphicsEffect(NULL) 摘除效果");
        /* 二次挂摘闭环（投影效果；确认摘除后可再挂新效果）。 */
        XWidget_setGraphicsEffect(childB, (XGraphicsEffect*)shadow);
        XAPI_EXPECT(XGraphicsEffect_source((XGraphicsEffect*)shadow) == childB,
                    "投影效果挂接 source 关联");
        XWidget_setGraphicsEffect(childB, NULL);
        XAPI_EXPECT(XWidget_graphicsEffect(childB) == NULL,
                    "投影效果摘除复位");
    }
    else {
        /* 创建失败路径：释放已建部分，避免泄漏。 */
        if (opacity) XGraphicsOpacityEffect_delete_base(opacity);
        if (blur) XGraphicsBlurEffect_delete_base(blur);
        if (shadow) XGraphicsDropShadowEffect_delete_base(shadow);
        XAPI_EXPECT(0, "内置效果创建分配失败");
    }

    /* ---- 收尾：清焦点后级联析构（焦点全局登记不悬空）。 ---- */
    XWidget_clearFocus(child);
    XWidget_clearFocus(childB);
    XWidget_delete_base(top);

#else /* XWIDGET_ON */

    /* 基类与样式族整体裁剪时的非空翻译单元哨兵。 */
    typedef int xgui_demo_apitest_core_disabled_sentinel;

#endif /* XWIDGET_ON */

    XPrintf("XGuiApiTest: [基类与样式族 core] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
