/**
 * @file       XScrollBar.c
 * @brief      滚动条控件实现（对标 Qt 6.8 QScrollBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XScrollBar.h"
#include "XAbstractSlider_Protected.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XHoverEvent.h"
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */
#include "XPainter.h"
#include "XVarList.h"
#include "XString.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#if XMENU_ON
#include "XMenu.h"
#endif /* XMENU_ON */
#include "XWidget_Protected.h"

#if XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON

#if !XSTYLE_ON
/* 无样式层构建：XStyleOption.h 的枚举整体不可用（:45-1689 守卫），但滚动条
 * 的子控件标识（按下/悬停状态机的键）不是样式层概念——Qt 把 SC_* 枚举放在
 * 常编译的 qstyle.h 正是这个道理。这里以**同值同名**宏补滚动条子集，数值
 * 与 XStyleOption.h:264-274 的枚举逐项一致（同为 Qt 6.8 qstyle.h 的
 * QStyle::SubControl 位）；启用样式层时本块整体消失，走真枚举。 */
#define XStyleSC_None                 0x00000000u
#define XStyleSC_ScrollBarAddLine     0x00000001u
#define XStyleSC_ScrollBarSubLine     0x00000002u
#define XStyleSC_ScrollBarAddPage     0x00000004u
#define XStyleSC_ScrollBarSubPage     0x00000008u
#define XStyleSC_ScrollBarFirst       0x00000010u
#define XStyleSC_ScrollBarLast        0x00000020u
#define XStyleSC_ScrollBarSlider      0x00000040u
#define XStyleSC_ScrollBarGroove      0x00000080u
#define XStylePM_MaximumDragDistance  8
/* 同上（XStyleOption.h 枚举序数；无样式层时 hint 查询走
 * xsb_styleHintBool 的 false 回落，这些序数只作为查询键传参——同为
 * Qt 6.8 qstyle.h 的枚举序号：ContextMenu=62/RollBetweenButtons=63/
 * Transient=96（XStyleOption.h:501-526）与 MiddleClickAbsolutePosition=2/
 * ScrollWhenPointerLeavesControl=3/LeftClickAbsolutePosition=39（:463-490），
 * 启用样式层时本块整体消失，走真枚举。 */
#define XStyleSH_ScrollBar_ContextMenu 62
#define XStyleSH_ScrollBar_RollBetweenButtons 63
#define XStyleSH_ScrollBar_Transient  96
#define XStyleSH_ScrollBar_MiddleClickAbsolutePosition 2
#define XStyleSH_ScrollBar_ScrollWhenPointerLeavesControl 3
#define XStyleSH_ScrollBar_LeftClickAbsolutePosition 39
#endif /* !XSTYLE_ON */

/* ==================== 内部工具 ==================== */

/** @brief 是否水平方向。 */
static int xsb_horizontal(const XScrollBar* self)
{
    return self->m_base.m_orientation ==
           (int)XGuiOrientation_Horizontal;
}

/** @brief 取调色板角色颜色（无调色板能力时回退灰阶）。 */
static uint32_t xsb_color(const XScrollBar* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

#if XSTYLE_ON
/** @brief 填充本控件口径的 CC_ScrollBar 样式选项（几何/提示共用）。
 *  @note 只填标量字段（不取字体度量）：本函数也在命中测试/每次事件里
 *        调用，XStyleOption_initFrom 的字体度量副本有分配代价。
 *  @details activeSubControls 的口径照 Qt QScrollBar::paintEvent
 *        （qscrollbar.cpp:511-517）：
 *          - 有按下子控件（m_pressedControl 非 None）⇒ 填按下控件；且
 *            指针**未移出**该控件时加 State_Sunken
 *            （`if (!d->pointerOutsidePressedControl) opt.state |= State_Sunken`）；
 *          - 否则 ⇒ 填悬停子控件（Qt `opt.activeSubControls = d->hoverControl`）。
 *        Qt 的 sliderMin/Max 也参与 hitTestComplexControl 的子控件几何，
 *        因此 hover 命中与绘制几何天然同源。 */
static void xsb_initOption(const XScrollBar* self, XStyleOption* opt)
{
    if (!opt) return;
    XStyleOption_init(opt, XStyleCC_ScrollBar);
    opt->m_styleObject = (XObject*)self;
    opt->m_state = XWidget_isEnabled((const XWidget*)self)
                       ? XStyleState_Enabled : 0;
    if (XWidget_hasFocus((const XWidget*)self))
        opt->m_state |= XStyleState_HasFocus;
    if (XWidget_underMouse((const XWidget*)self) &&
        XWidget_isEnabled((const XWidget*)self))
        opt->m_state |= XStyleState_MouseOver;
    opt->m_horizontal = xsb_horizontal(self) ? 1 : 0;
    opt->m_sliderMin = self->m_base.m_min;
    opt->m_sliderMax = self->m_base.m_max;
    opt->m_sliderValue = self->m_base.m_value;
    /* P0-18：滑块几何以句柄位置为准（对标 QScrollBar::initStyleOption
     * 的 `option->sliderPosition = d->position`，qscrollbar.cpp:287）；
     * tracking=false 拖动时位置≠值，画面必须跟手。 */
    opt->m_sliderPosition = self->m_base.m_position;
    opt->m_sliderPositionSet = 1;
    opt->m_sliderSingleStep = self->m_base.m_singleStep;
    opt->m_sliderPageStep = self->m_base.m_pageStep;
    /* P0-17：外观翻转（对标 qscrollbar.cpp:291 `upsideDown =
     * d->invertedAppearance`）。 */
    opt->m_upsideDown = self->m_base.m_invertedAppearance;
    opt->m_scrollSubLine = self->m_showButtons ? 1 : 0;
    opt->m_scrollAddLine = self->m_showButtons ? 1 : 0;
    /* P0-67：m_scrollActiveSub/Dir **只承载按下**（0 无/1 SubLine/2
     * AddLine）——样式层语义是「Sub+dir 决定哪个按钮画按下凹面」（
     * XCommonStyle.c:2203-2213，dir=0 视为 SubLine 按下）。悬停**不得**
     * 借这两个通道表达：Qt 用 activeSubControls 同一位掩码同时表达按下
     * 与 hover 高亮（qscrollbar.cpp:301-306 `opt.activeSubControls =
     * d->pressedControl; else = d->hoverControl`），而 Fusion 的 hover
     * 走 highlightedGradient（qfusionstyle.cpp:2503-2504）——本框架样式
     * 层没有 hover 高亮通道，把 hover 塞进按下通道会让「悬停滑块」把
     * SubLine 画成按下（回归锁：hoverDoesNotDarkenButton）。hover 子控件
     * 追踪由 m_hoverControl + XScrollBar_hoverControl 公开 API 承载，
     * 高亮视觉待样式层补 hover 通道（登记 openNotes 独立批次）。 */
    opt->m_scrollActiveSub = self->m_activeSub ? 1 : 0;
    opt->m_scrollActiveDir = self->m_activeSub
        ? (uint8_t)(self->m_activeIsAdd ? 2 : 1) : 0;
    /* transient flash 点亮 ⇒ State_On（对标 QScrollBar::initStyleOption
     * `if ((d->flashed || !d->transient) &&
     *  styleHint(SH_ScrollBar_Transient, option, this)) opt->state |= State_On`，
     * qscrollbar.cpp:294-295）。
     * @note 这里**直接**查样式提示而不复用 xsb_transientHint()：后者内部
     *       会再建一个本选项（xsb_initOption），自递归。Qt 原文传的就是
     *       正在构建的 `option` 本身，此处同口径。 */
    if ((self->m_flashed || !self->m_transient) &&
        XStyle_styleHint_base(XWidget_style((const XWidget*)self),
                              XStyleSH_ScrollBar_Transient, opt,
                              (const XWidget*)self) != 0)
        opt->m_state |= XStyleState_On;
}
#endif /* XSTYLE_ON */

/** @brief 无样式时的按钮厚度回退（XCommonStyle 缺省 extent 为 16）。 */
#define XSCROLLBAR_BUTTON_W 16
/** @brief 无样式时的滑块最小长度回退。 */
#define XSCROLLBAR_SLIDER_MIN 16

/* ==================== 三枚滚动条点击/驻留提示（已入枚举） ====================
 * SH_ScrollBar_MiddleClickAbsolutePosition=2 /
 * SH_ScrollBar_ScrollWhenPointerLeavesControl=3 /
 * SH_ScrollBar_LeftClickAbsolutePosition=39 已正式收录进
 * XStyleOption.h 的 XStyleStyleHint（XStyleOption.h:463-464/:490，数值 =
 * Qt 6.8 qstyle.h 枚举序号）；XCommonStyle 对三枚显式 return false
 * （qcommonstyle.cpp:5441/:5442/:5464 归 false 组）。本文件一律经
 * xsb_styleHintBool 走框架 styleHint 查询，不再持有本地序数宏。 */

/** @brief 两端按钮厚度（对标 PM_ScrollBarExtent；隐藏按钮时 0）。
 *  @note 命中几何必须与样式层 CC_ScrollBar 的绘制几何同源（
 *        XCommonStyle/XFusionStyle 的 SC_ScrollBarSubLine 用
 *        PM_ScrollBarExtent），否则按钮可见但点击落进轨道翻页。 */
static int xsb_buttonLen(const XScrollBar* self)
{
    int extent = 0;
    if (!self || !self->m_showButtons) return 0;
#if XSTYLE_ON
    {
        XStyleOption opt;
        xsb_initOption(self, &opt);
        extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)self),
                                         XStylePM_ScrollBarExtent, &opt);
    }
#endif /* XSTYLE_ON */
    if (extent <= 0) extent = XSCROLLBAR_BUTTON_W;
    return extent;
}

/** @brief 滑块最小长度（对标 PM_ScrollBarSliderMin）。 */
static int xsb_sliderMin(const XScrollBar* self)
{
    int minLen = 0;
#if XSTYLE_ON
    {
        XStyleOption opt;
        xsb_initOption(self, &opt);
        minLen = XStyle_pixelMetric_base(XWidget_style((const XWidget*)self),
                                         XStylePM_ScrollBarSliderMin, &opt);
    }
#else
    (void)self;
#endif /* XSTYLE_ON */
    if (minLen <= 0) minLen = XSCROLLBAR_SLIDER_MIN;
    return minLen;
}

/** @brief 该内容长度下的实际按钮厚度：不超过内容一半
 *         （对标样式层 SC_ScrollBarSubLine/AddLine 的
 *         `bw = qMin(rect.size() / 2, extent)`，防极小控件两按钮重叠）。 */
static int xsb_buttonLenAt(const XScrollBar* self, int contentLen)
{
    int bw = xsb_buttonLen(self);
    if (bw <= 0) return 0;
    if (contentLen / 2 < bw) bw = contentLen / 2;
    return bw;
}

/** @brief 滑轨区间原点（按钮之后）。 */
static int xsb_sliderOrigin(const XScrollBar* self, int contentLen)
{
    return xsb_buttonLenAt(self, contentLen);
}

/** @brief 滑轨长度（扣除两端按钮）。 */
static int xsb_sliderLen(const XScrollBar* self, int contentLen)
{
    int btn = xsb_buttonLenAt(self, contentLen) * 2;
    int len = contentLen - btn;
    return len > 0 ? len : 0;
}

/** @brief 命中起始按钮（坐标位于滑轨起点之前）。 */
static bool xsb_hitSub(const XScrollBar* self, int pos, int contentLen)
{
    return self->m_showButtons && pos < xsb_buttonLenAt(self, contentLen);
}

/** @brief 命中结束按钮。 */
static bool xsb_hitAdd(const XScrollBar* self, int pos, int contentLen)
{
    return self->m_showButtons &&
           pos >= contentLen - xsb_buttonLenAt(self, contentLen);
}

/** @brief 滑块长度（在**滑轨长度**上按 page/(range+page) 折算，
 *         下限 PM_ScrollBarSliderMin——与样式层 CC_ScrollBar 同口径）。 */
static int xsb_handleLength(const XScrollBar* self, int sliderLen)
{
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    int page = XAbstractSlider_pageStep((const XAbstractSlider*)self);
    int len;
    if (range <= 0) return sliderLen;
    len = sliderLen * page / (range + page);
    if (len < xsb_sliderMin(self)) len = xsb_sliderMin(self);
    if (len > sliderLen) len = sliderLen;
    return len;
}

/** @brief 滑块原点像素（相对滑轨原点；0 = 滑轨起点）。
 *  @note 以**句柄位置**（sliderPosition）为准（P0-18）：tracking=false
 *        拖动期间画面跟手；invertedAppearance（= 样式选项 upsideDown，
 *        对标 QScrollBar::initStyleOption `upsideDown =
 *        d->invertedAppearance`）时从另一端数（P0-17）。 */
static int xsb_handlePos(const XScrollBar* self, int sliderLen)
{
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    int travel = sliderLen - xsb_handleLength(self, sliderLen);
    int pos = XAbstractSlider_sliderPosition((const XAbstractSlider*)self);
    if (range <= 0 || travel <= 0) return 0;
    if (self->m_base.m_invertedAppearance)
        return (XAbstractSlider_maximum((const XAbstractSlider*)self) - pos) *
               travel / range;
    return (pos - XAbstractSlider_minimum((const XAbstractSlider*)self)) *
           travel / range;
}

/** @brief 像素位置 → 值（对标 QScrollBarPrivate::pixelPosToRangeValue，
 *        qscrollbar.cpp:664-683）。
 *  @note  pos 是**滑轨内**像素坐标（调用方已扣滑轨原点）；按下偏移同样由
 *         调用方扣减后传入——与 Qt 调用形态逐点一致：拖动分支
 *         `pixelPosToRangeValue(pos - clickOffset)`（qscrollbar.cpp:620）、
 *         绝对定位分支 `pixelPosToRangeValue(pos - sliderLength/2)`
 *         （qscrollbar.cpp:510）。**本函数不得再读 m_pressOffset**，
 *         否则与调用方形成双重扣减（回归锁：拖动 round-trip，
 *         XScrollBarHoverAlignTest scrollDragPixelRoundTrip）。
 *         反解按 invertedAppearance 镜像（`sliderValueFromPosition(...,
 *         upsideDown)`）。 */
static int xsb_posToValue(const XScrollBar* self, int pos, int sliderLen,
                          int* outValue)
{
    int range = XAbstractSlider_maximum((XAbstractSlider*)self) -
                XAbstractSlider_minimum((XAbstractSlider*)self);
    int travel = sliderLen - xsb_handleLength(self, sliderLen);
    int handlePos = pos;
    int rel;
    if (range <= 0 || travel <= 0) {
        *outValue = XAbstractSlider_minimum((XAbstractSlider*)self);
        return 1;
    }
    if (handlePos < 0) handlePos = 0;
    if (handlePos > travel) handlePos = travel;
    rel = (int)(((long long)handlePos * range + travel / 2) / travel);
    *outValue = self->m_base.m_invertedAppearance
        ? (XAbstractSlider_maximum((XAbstractSlider*)self) - rel)
        : (XAbstractSlider_minimum((XAbstractSlider*)self) + rel);
    return 1;
}

/** @brief 样式 transient 提示（SH_ScrollBar_Transient，Qt Fusion=0）。 */
static bool xsb_transientHint(const XScrollBar* self)
{
#if XSTYLE_ON
    XStyleOption opt;
    xsb_initOption(self, &opt);
    return XStyle_styleHint_base(XWidget_style((const XWidget*)self),
                                 XStyleSH_ScrollBar_Transient, &opt,
                                 (const XWidget*)self) != 0;
#else
    (void)self;
    return false; /* 无样式层：按 Fusion 非 transient 口径。 */
#endif /* XSTYLE_ON */
}

/** @brief 样式提示布尔查询（0＝假；XSTYLE_ON 关闭时按 Qt QCommonStyle 的
 *         缺省值 0 口径，见 qcommonstyle.cpp:5441/:5442/:5464 三枚滚动条
 *         点击/驻留提示归 false 组、组尾 `ret = false`（:5475））。 */
static bool xsb_styleHintBool(const XScrollBar* self, int hint)
{
#if XSTYLE_ON
    XStyleOption opt;
    xsb_initOption(self, &opt);
    return XStyle_styleHint_base(XWidget_style((const XWidget*)self),
                                 hint, &opt, (const XWidget*)self) != 0;
#else
    (void)self;
    (void)hint;
    return false;
#endif /* XSTYLE_ON */
}

/** @brief 样式像素度量（XSTYLE_ON 关闭或取到非正值时回落 fallback）。 */
static int xsb_pixelMetric(const XScrollBar* self, int pm, int fallback)
{
    int v = 0;
#if XSTYLE_ON
    XStyleOption opt;
    xsb_initOption(self, &opt);
    v = XStyle_pixelMetric_base(XWidget_style((const XWidget*)self), pm,
                                &opt);
#else
    (void)self;
    (void)pm;
#endif /* XSTYLE_ON */
    return v > 0 ? v : fallback;
}

#if XSTYLE_ON
/** @brief 构造带控件整幅矩形的样式选项（绘制/命中测试共用）。
 *  @details xsb_initOption 刻意不填 m_rect（几何计算不需要），而样式层的
 *           subControlRect / hitTestComplexControl 完全依赖 `opt->m_rect`
 *           （VXCommonStyle_subControlRect 的 CC_ScrollBar 分支以它为
 *           整条范围），故命中与绘制两路都必须先补上。调色板同理：
 *           Fusion 的滑块/槽取色读它。 */
static void xsb_initOptionRect(const XScrollBar* self, XStyleOption* opt)
{
    if (!opt) return;
    xsb_initOption(self, opt);
    opt->m_rect = XWidget_rect((const XWidget*)self);
#if XPALETTE_ON
    opt->m_palette = XWidget_palette((const XWidget*)self);
#endif
}
#endif /* XSTYLE_ON */

/* ==================== 悬停子控件追踪（对标 QScrollBarPrivate） ==================== */

/* 前向声明：xsb_newHoverControl/xsb_hitTestComplex 要用这两个
 * （定义在其后——回退几何依赖 xsb_handleLength/xsb_handlePos 等）。 */
static uint32_t xsb_hitControl(const XScrollBar* self, int x, int y);
static XRect xsb_subControlRect(const XScrollBar* self, int sc);
static uint32_t xsb_normalizeControl(const XScrollBar* self,
                                     uint32_t control);

/** @brief 纯命中测试（**不改**悬停状态）。对标
 *         `style()->hitTestComplexControl(CC_ScrollBar, opt, pos, this)`：
 *         同样的样式层入口，但不写 hoverControl/hoverRect——
 *         QScrollBar::mousePressEvent（qscrollbar.cpp:542）用的就是它，
 *         而只有 newHoverControl（:175-187）才写悬停状态。 */
static uint32_t xsb_hitTestComplex(const XScrollBar* self, int x, int y)
{
#if XSTYLE_ON
    XStyleOption opt;
    xsb_initOptionRect(self, &opt);
    return xsb_normalizeControl(
        self, (uint32_t)XStyle_hitTestComplexControl_base(
                  XWidget_style((const XWidget*)self), XStyleCC_ScrollBar,
                  &opt, x, y, (const XWidget*)self));
#else
    return xsb_hitControl(self, x, y);
#endif /* XSTYLE_ON */
}

/** @brief 中键绝对定位是否启用（对标
 *         `SH_ScrollBar_MiddleClickAbsolutePosition`）。
 *  @details Qt 的取值：Fusion = **1**（qfusionstyle.cpp:3541；
 *           本机 Qt 6.8.0 offscreen 探针实测 mid=1、中键点轨道 value 跳
 *           50 且 isSliderDown=1），QCommonStyle = false
 *           （qcommonstyle.cpp:5441 归入 `ret = false` 那组；同探针实测
 *           mid=0、中键点轨道 value 不变且 isSliderDown=0——事件被
 *           QScrollBar::mousePressEvent 首段守卫整体 return）。
 *           本框架缺省样式是 XCommonStyle（XStyle.c:779-786）⇒ 中键
 *           点击被忽略（B8 终裁 2026-11-15）；中键绝对定位只有装了
 *           返回该提示=1 的样式（如 Fusion 系）才会生效。
 *  @param      self 滚动条对象；可为 NULL（返回 false）。
 *  @return     true 表示中键点击轨道时滑块中心绝对定位。 */
static bool xsb_middleClickAbsolute(const XScrollBar* self)
{
    if (!self) return false;
    return xsb_styleHintBool(self,
                             XStyleSH_ScrollBar_MiddleClickAbsolutePosition);
}

/** @brief 命中结果规整：两端按钮被 X 扩展关闭时，按钮带并入相邻页区。
 *  @details 样式层 CC_ScrollBar 的 subControlRect **不**看
 *           m_scrollSubLine/m_scrollAddLine（Qt 侧该几何只受
 *           SH_ScrollBar_Transient 影响，见 qcommonstyle.cpp:4122-4123），
 *           故 X 扩展 `XScrollBar_setShowButtons(false)` 之后样式层仍会给出
 *           按钮矩形。此时按钮**没有画出来**，按下那里必须按「槽/页区」
 *           处理——与绘制（xsb_sliderOrigin 在无按钮时 = 0）同口径。
 *  @param  self    滚动条（读 m_showButtons）。
 *  @param  control 样式层命中值。
 *  @return 规整后的控件位。 */
static uint32_t xsb_normalizeControl(const XScrollBar* self, uint32_t control)
{
    if (!self || self->m_showButtons) return control;
    if (control == (uint32_t)XStyleSC_ScrollBarSubLine)
        return (uint32_t)XStyleSC_ScrollBarSubPage;
    if (control == (uint32_t)XStyleSC_ScrollBarAddLine)
        return (uint32_t)XStyleSC_ScrollBarAddPage;
    return control;
}

/** @brief 重算悬停命中子控件并写回状态（对标
 *         QScrollBarPrivate::newHoverControl，qscrollbar.cpp:175-187）。
 *  @details 命中结果写入 m_hoverControl；无命中时 m_hoverRect 清空，否则
 *           取该子控件的矩形（供「旧矩形 ∪ 新矩形」两段标脏）。返回新
 *           命中值，调用方与旧值比较判定是否变化。 */
static uint32_t xsb_newHoverControl(XScrollBar* self, int x, int y)
{
    self->m_hoverControl = xsb_hitTestComplex(self, x, y);
    self->m_hoverRect = xsb_subControlRect(self, (int)self->m_hoverControl);
    return self->m_hoverControl;
}

/** @brief 子控件矩形（对标 QStyle::subControlRect(CC_ScrollBar, sc)）。
 *  @details 与样式层绘制同源；无样式层时返回空矩形（调用方按「无脏区」
 *           处理，不猜几何）。 */
static XRect xsb_subControlRect(const XScrollBar* self, int sc)
{
    XRect r;
    XRect_init(&r, 0, 0, 0, 0);
#if XSTYLE_ON
    {
        XStyleOption opt;
        xsb_initOptionRect(self, &opt);
        r = XStyle_subControlRect_base(XWidget_style((const XWidget*)self),
                                       XStyleCC_ScrollBar, &opt, sc,
                                       (const XWidget*)self);
    }
#else
    (void)self;
    (void)sc;
#endif /* XSTYLE_ON */
    return r;
}

/** @brief 子控件位 → 滑块动作（对标 QScrollBarPrivate::activateControl 的
 *         switch，qscrollbar.cpp:224-245）。First/Last 由
 *         XStyleSC_ScrollBarFirst/Last 承载但 QCommonStyle 不给矩形（永不
 *         命中），此处仍按 Qt 完整映射。 */
static int xsb_actionForControl(uint32_t control)
{
    switch (control) {
    case (uint32_t)XStyleSC_ScrollBarAddPage:
        return XAbstractSliderSliderAction_PageStepAdd;
    case (uint32_t)XStyleSC_ScrollBarSubPage:
        return XAbstractSliderSliderAction_PageStepSub;
    case (uint32_t)XStyleSC_ScrollBarAddLine:
        return XAbstractSliderSliderAction_SingleStepAdd;
    case (uint32_t)XStyleSC_ScrollBarSubLine:
        return XAbstractSliderSliderAction_SingleStepSub;
    case (uint32_t)XStyleSC_ScrollBarFirst:
        return XAbstractSliderSliderAction_ToMinimum;
    case (uint32_t)XStyleSC_ScrollBarLast:
        return XAbstractSliderSliderAction_ToMaximum;
    default:
        return XAbstractSliderSliderAction_NoAction;
    }
}

/** @brief 激活按下子控件（对标 QScrollBarPrivate::activateControl，
 *         qscrollbar.cpp:221-251）：布防连发并立即触发一次动作。
 *  @param threshold 连发初始延迟（Qt 的 initialDelay=500；滚动按钮间
 *                   重命中传 0＝立刻开始连发）。 */
static void xsb_activateControl(XScrollBar* self, uint32_t control,
                                int threshold)
{
    int action = xsb_actionForControl(control);
    if (action == XAbstractSliderSliderAction_NoAction) return;
    XAbstractSlider_setRepeatAction((XAbstractSlider*)self, action,
                                    threshold, 50);
    XAbstractSlider_triggerAction((XAbstractSlider*)self, action);
}

/** @brief 收掉按下与连发（对标 QScrollBarPrivate::stopRepeatAction，
 *         qscrollbar.cpp:253-266）：清 pressedControl、收连发；先前按下
 *         若是滑块则抬 sliderDown；最后重绘该子控件矩形。 */
static void xsb_stopRepeatAction(XScrollBar* self)
{
    uint32_t tmp = (uint32_t)self->m_pressedControl;
    XRect r;
    XAbstractSlider_setRepeatAction((XAbstractSlider*)self,
                                   XAbstractSliderSliderAction_NoAction,
                                   0, 0);
    self->m_pressedControl = (int)XStyleSC_None;
    self->m_pointerOutside = false;
    self->m_activeSub = false;
    if (tmp == (uint32_t)XStyleSC_ScrollBarSlider &&
        XAbstractSlider_isSliderDown((const XAbstractSlider*)self))
        XAbstractSlider_setSliderDown((XAbstractSlider*)self, false);
    if (tmp == (uint32_t)XStyleSC_None) return;
    r = xsb_subControlRect(self, (int)tmp);
    if (r.width > 0 && r.height > 0) XWidget_updateRect((XWidget*)self, &r);
    else XWidget_update((XWidget*)self);
}

/** @brief 点命中的子控件（**本控件自身绘制几何**为准，0=SC_None）。
 *  @details 与 VXScrollBar_paintEvent 的非样式回退路径同源（xsb_hitSub/
 *           xsb_hitAdd/xsb_handlePos 一套公式）。无样式层时按下/悬停都用
 *           它；有样式层时按下与悬停都走样式层命中（Qt 口径：
 *           qscrollbar.cpp:542 与 :181 都是 hitTestComplexControl）。
 *  @note   本函数是 `XSTYLE_ON=0` 裁剪构建的**唯一**命中路径
 *           （xsb_hitTestComplex 的 `#else` 分支）——默认构建下它没有
 *           运行时调用点，属编译期分支，不是死代码。
 *           另注意它与 xsb_newHoverControl 的差别：后者按样式层几何（滑动条
 *           绘制几何），前者按本控件回退绘制几何。二者在**有样式层**时
 *           不必相同（样式层 PM_ScrollBarExtent 可与 XSCROLLBAR_BUTTON_W
 *           不同），因此不可互相替代。 */
static uint32_t xsb_hitControl(const XScrollBar* self, int x, int y)
{
    int contentLen;
    int pos;
    int sliderLen;
    int handleLen;
    int handlePos;
    if (!self) return (uint32_t)XStyleSC_None;
    contentLen = xsb_horizontal(self) ? XWidget_width((const XWidget*)self)
                                      : XWidget_height((const XWidget*)self);
    pos = xsb_horizontal(self) ? x : y;
    if (xsb_hitSub(self, pos, contentLen))
        return (uint32_t)XStyleSC_ScrollBarSubLine;
    if (xsb_hitAdd(self, pos, contentLen))
        return (uint32_t)XStyleSC_ScrollBarAddLine;
    sliderLen = xsb_sliderLen(self, contentLen);
    handleLen = xsb_handleLength(self, sliderLen);
    handlePos = xsb_sliderOrigin(self, contentLen) +
                xsb_handlePos(self, sliderLen);
    if (pos >= handlePos && pos < handlePos + handleLen)
        return (uint32_t)XStyleSC_ScrollBarSlider;
    return pos < handlePos ? (uint32_t)XStyleSC_ScrollBarSubPage
                           : (uint32_t)XStyleSC_ScrollBarAddPage;
}

/* ==================== transient 淡出/淡入（对标 QScrollbarStyleAnimation） ==================== */

/** @brief 淡入/淡出步长（毫秒）。 */
#define XSB_FADE_STEP_MS 25
/** @brief 淡入/淡出动画步数（200ms，对标 qstyleanimation.cpp:12
 *         `ScrollBarFadeOutDuration = 200.0`）。 */
#define XSB_FADE_STEPS 8
/** @brief 淡出前静置步数（450ms，对标 qstyleanimation.cpp:13
 *         `ScrollBarFadeOutDelay = 450.0`；25ms × 18 = 450ms）。 */
#define XSB_FADE_DELAY_STEPS 18

/** @brief 淡出/淡入阶段。 */
typedef enum XsbFadeMode
{
    XsbFadeMode_Idle = 0,    /**< 未在动（不透明度已到端点）。 */
    XsbFadeMode_Activating,  /**< 0→1（对标 Mode::Activating）。 */
    XsbFadeMode_Delaying,    /**< 淡出前的静置延迟（Deactivating 的 delay）。 */
    XsbFadeMode_Deactivating /**< 1→0（对标 Mode::Deactivating 的动画段）。 */
} XsbFadeMode;

/** @brief 停掉淡出/淡入定时器并把阶段置 Idle。 */
static void xsb_stopFade(XScrollBar* self)
{
    if (!self) return;
    if (self->m_fadeTimerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_fadeTimerId);
        self->m_fadeTimerId = XTIMER_INVALID_ID;
    }
    self->m_fadeMode = (uint8_t)XsbFadeMode_Idle;
}

/** @brief 布防淡出/淡入定时器（已在跑则只重置进度，不重复起表）。 */
static void xsb_startFadeTimer(XScrollBar* self)
{
    if (!self) return;
    if (self->m_fadeTimerId != XTIMER_INVALID_ID) return;
    self->m_fadeTimerId = XObject_startTimer((XObject*)self,
                                             (uint64_t)XSB_FADE_STEP_MS,
                                             XTimerType_CoarseTimer);
}

/** @brief 淡入到完全不透明（Qt Mode::Activating：200ms 0→1）。
 *  @details 只在样式提示 SH_ScrollBar_Transient 为真时生效——非 transient
 *           样式下滚动条恒不透明，动画无意义（Qt 同样以
 *           `styleHint(SH_ScrollBar_Transient)` 为门，qfusionstyle.cpp:2277）。 */
static void xsb_fadeIn(XScrollBar* self)
{
    if (!self) return;
    if (!xsb_transientHint(self)) {
        self->m_transientOpacity = 1.0f;
        xsb_stopFade(self);
        return;
    }
    if (self->m_transientOpacity >= 1.0f &&
        self->m_fadeMode != (uint8_t)XsbFadeMode_Deactivating &&
        self->m_fadeMode != (uint8_t)XsbFadeMode_Delaying) {
        return; /* 已完全不透明且不在淡出 ⇒ 无事可做（同值早退）。 */
    }
    self->m_fadeMode = (uint8_t)XsbFadeMode_Activating;
    if (self->m_fadeStep > XSB_FADE_STEPS || self->m_fadeStep == 0)
        self->m_fadeStep = XSB_FADE_STEPS;
    xsb_startFadeTimer(self);
}

/** @brief 开始淡出（Qt Mode::Deactivating：先静置 450ms 再 200ms 1→0）。 */
static void xsb_fadeOut(XScrollBar* self)
{
    if (!self) return;
    if (!xsb_transientHint(self)) {
        self->m_transientOpacity = 1.0f;
        xsb_stopFade(self);
        return;
    }
    self->m_fadeMode = (uint8_t)XsbFadeMode_Delaying;
    self->m_fadeStep = XSB_FADE_DELAY_STEPS;
    xsb_startFadeTimer(self);
}

/** @brief 淡出/淡入一拍（对标 QScrollbarStyleAnimation::updateCurrentTime
 *         的 0→1 / 1→0 线性插值）。 */
static void xsb_fadeTick(XScrollBar* self)
{
    float v;
    if (!self) return;
    switch ((XsbFadeMode)self->m_fadeMode) {
    case XsbFadeMode_Idle:
        xsb_stopFade(self);
        return;
    case XsbFadeMode_Delaying:
        /* 静置段（Qt 的 450ms delay）：不改变不透明度、不标脏。静置耗尽后
         * 切到动画段并 **return**——下一拍由 Deactivating 分支开始推进
         * （Qt 的 delay 结束到动画首帧同样相隔一个动画 tick）。 */
        if (self->m_fadeStep > 0) {
            --self->m_fadeStep;
            if (self->m_fadeStep == 0)
                self->m_fadeMode = (uint8_t)XsbFadeMode_Deactivating;
            return;
        }
        /* 防御：m_fadeStep 已为 0 却仍是 Delaying（状态被外部破坏）⇒
         * 直接进入动画段，避免原地空转。 */
        self->m_fadeMode = (uint8_t)XsbFadeMode_Deactivating;
        return;
    case XsbFadeMode_Deactivating:
        if (self->m_fadeStep > 0) --self->m_fadeStep;
        v = (float)self->m_fadeStep / (float)XSB_FADE_STEPS;
        if (v != self->m_transientOpacity) {
            self->m_transientOpacity = v;
            XWidget_update((XWidget*)self);
        }
        if (self->m_fadeStep == 0) xsb_stopFade(self);
        return;
    case XsbFadeMode_Activating:
    default:
        if (self->m_fadeStep < (uint32_t)XSB_FADE_STEPS) ++self->m_fadeStep;
        v = (float)self->m_fadeStep / (float)XSB_FADE_STEPS;
        if (v != self->m_transientOpacity) {
            self->m_transientOpacity = v;
            XWidget_update((XWidget*)self);
        }
        if (self->m_fadeStep >= (uint32_t)XSB_FADE_STEPS) xsb_stopFade(self);
        return;
    }
}

/** @brief 按方向/样式重算 sizeHint（对标 QScrollBar::sizeHint
 *         qscrollbar.cpp:411-425）：extent*2+sliderMin，横竖互换。 */
static void xsb_updateSizeHint(XScrollBar* self)
{
    XSize hint;
#if XSTYLE_ON
    XSize content;
#endif /* XSTYLE_ON */
    int extent = XSCROLLBAR_BUTTON_W;
    int sliderMin = XSCROLLBAR_SLIDER_MIN;
    int w;
    int h;
    if (!self) return;
#if XSTYLE_ON
    {
        XStyleOption opt;
        xsb_initOption(self, &opt);
        extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)self),
                                         XStylePM_ScrollBarExtent, &opt);
        if (extent <= 0) extent = XSCROLLBAR_BUTTON_W;
        sliderMin = XStyle_pixelMetric_base(XWidget_style((const XWidget*)self),
                                            XStylePM_ScrollBarSliderMin, &opt);
        if (sliderMin <= 0) sliderMin = XSCROLLBAR_SLIDER_MIN;
        if (xsb_horizontal(self)) {
            w = extent * 2 + sliderMin;
            h = extent;
        } else {
            w = extent;
            h = extent * 2 + sliderMin;
        }
        XSize_init(&content, w, h);
        hint = XStyle_sizeFromContents_base(XWidget_style((const XWidget*)self),
                                            XStyleCT_ScrollBar, &opt, content);
    }
#else
    if (xsb_horizontal(self)) {
        w = extent * 2 + sliderMin;
        h = extent;
    } else {
        w = extent;
        h = extent * 2 + sliderMin;
    }
    XSize_init(&hint, w, h);
#endif /* XSTYLE_ON */
    XWidget_setSizeHint((XWidget*)self, &hint);
}

/* ==================== 事件处理 ==================== */

/** @brief paintEvent：槽体 + 滑块矩形（水平/垂直）。 */
static void VXScrollBar_paintEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    int w = XWidget_width(self);
    int h = XWidget_height(self);
    uint32_t groove;
    uint32_t handle;
    XRect r;
    if (!sb || !event) return;
    if (w <= 2 || h <= 2) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共风格接管：渐变槽 + 渐变滑块 + 双层描边走
         * CC_ScrollBar（完整对标 QFusionStyle 非 transient 路径）。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        /* 选项全量走 xsb_initOptionRect（含 rect/调色板/悬停与按下子控件），
         * 与命中测试同源——否则 hovering 判定与几何两路不同源。 */
        xsb_initOptionRect(sb, &opt);
        /* state **追加**而非整体赋值：initStyleOption 已按控件实况填了
         * Enabled/HasFocus/MouseOver/State_On（qscrollbar.cpp:284-296），
         * Qt 的 paintEvent 不重置 state，只追加 Sunken（:511-515）。整体
         * 赋值会把 State_On 拍掉 ⇒ transient flash 收口的那次重绘画不出
         * 亮点。Raised 是滚动条常态基线，只在启用时补。 */
        if (XWidget_isEnabled(self))
            opt.m_state |= XStyleState_Enabled | XStyleState_Raised;
        /* 对标 QScrollBar::paintEvent `if (d->pressedControl) { ... if
         * (!d->pointerOutsidePressedControl) opt.state |= State_Sunken; }`
         * （qscrollbar.cpp:511-515）：按下且指针未移出按下子控件时才画凹面，
         * 移出后保持凸起（Qt 的「移出即松开」视觉反馈）。 */
        if (sb->m_pressedControl != (int)XStyleSC_None &&
            !sb->m_pointerOutside)
            opt.m_state |= XStyleState_Sunken;
        /* transient 淡出/淡入不透明度（对标 QFusionStyle 的
         * `painter->setOpacity(opacity)`，qfusionstyle.cpp:2359）：
         * 只有样式确为 transient 时才施加，非 transient 恒 1.0。 */
        if (sb->m_transient &&
            sb->m_transientOpacity < 1.0f &&
            XStyle_styleHint_base(style, XStyleSH_ScrollBar_Transient, &opt,
                                  self) != 0)
            XPainter_setOpacity(&painter, sb->m_transientOpacity);
        XStyle_drawComplexControl_base(style, XStyleCC_ScrollBar, &opt,
                                       &painter, self);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    groove = xsb_color(sb, XPaletteColorRole_Window);
    handle = xsb_color(sb, XPaletteColorRole_Mid);
    if (sb->m_showButtons) {
        /* 按钮区（原路径：Button 底 + Mid 箭头）。 */
        uint32_t btnC = xsb_color(sb, XPaletteColorRole_Button);
        int bw = xsb_buttonLenAt(sb, xsb_horizontal(sb) ? w : h);
        if (bw <= 0) bw = XSCROLLBAR_BUTTON_W;
        if (xsb_horizontal(sb)) {
            XRect b1, b2;
            XRect_init(&b1, 0, 0, bw, h);
            XRect_init(&b2, w - bw, 0, bw, h);
            XPainter_fillRect(&painter, &b1, btnC);
            XPainter_fillRect(&painter, &b2, btnC);
            XPainter_setPen(&painter, handle);
            XPainter_drawLine(&painter, bw / 2 + 3, h / 2,
                              bw / 2 - 3, h / 2 - 3);
            XPainter_drawLine(&painter, bw / 2 - 3, h / 2 - 3,
                              bw / 2 - 3, h / 2 + 3);
            XPainter_drawLine(&painter, w - bw / 2 - 3, h / 2,
                              w - bw / 2 + 3, h / 2 - 3);
            XPainter_drawLine(&painter, w - bw / 2 + 3, h / 2 - 3,
                              w - bw / 2 + 3, h / 2 + 3);
        } else {
            XRect b1, b2;
            XRect_init(&b1, 0, 0, w, bw);
            XRect_init(&b2, 0, h - bw, w, bw);
            XPainter_fillRect(&painter, &b1, btnC);
            XPainter_fillRect(&painter, &b2, btnC);
            XPainter_setPen(&painter, handle);
            XPainter_drawLine(&painter, w / 2, bw / 2 + 3,
                              w / 2 - 3, bw / 2 - 3);
            XPainter_drawLine(&painter, w / 2 - 3, bw / 2 - 3,
                              w / 2 + 3, bw / 2 - 3);
            XPainter_drawLine(&painter, w / 2, h - bw / 2 - 3,
                              w / 2 - 3, h - bw / 2 + 3);
            XPainter_drawLine(&painter, w / 2 - 3, h - bw / 2 + 3,
                              w / 2 + 3, h - bw / 2 + 3);
        }
    }
    if (xsb_horizontal(sb)) {
        int sliderLen = xsb_sliderLen(sb, w);
        int origin = xsb_sliderOrigin(sb, w);
        XRect_init(&r, origin, (h - 8) / 2, sliderLen, 8);
        XPainter_fillRect(&painter, &r, groove);
        XRect_init(&r, origin + xsb_handlePos(sb, sliderLen),
                   (h - 8) / 2, xsb_handleLength(sb, sliderLen), 8);
        XPainter_fillRect(&painter, &r, handle);
    } else {
        int sliderLen = xsb_sliderLen(sb, h);
        int origin = xsb_sliderOrigin(sb, h);
        XRect_init(&r, (w - 8) / 2, origin, 8, sliderLen);
        XPainter_fillRect(&painter, &r, groove);
        XRect_init(&r, (w - 8) / 2, origin + xsb_handlePos(sb, sliderLen),
                   8, xsb_handleLength(sb, sliderLen));
        XPainter_fillRect(&painter, &r, handle);
    }
    XPainter_deinit(&painter);
}

/** @brief 鼠标按下：按命中的子控件走 Qt 的 pressedControl 状态机
 *         （对标 QScrollBar::mousePressEvent qscrollbar.cpp:524-583）。
 *  @details Qt 6.8 语义（逐条对照）：
 *           - `if (d->repeatActionTimer.isActive()) d->stopRepeatAction();`
 *             （:528-529）——上一次连发未收先收；
 *           - 首部守卫：maximum==minimum（:537）、已有别的键按下（:538）、
 *             按键不是左键/（中键且 SH_ScrollBar_MiddleClickAbsolutePosition
 *             =1，:531-532、:539）；
 *           - `pressedControl = style()->hitTestComplexControl(...)`（:542）；
 *           - 按 Add/SubPage 且（中键绝对定位 或 左键绝对定位提示）⇒ 把
 *             滑块中心定位到点击处并转成**滑块按下**（:556-566）；
 *           - `activateControl(pressedControl, 500)`（:570）+ 重绘该子控件
 *             （:571）；
 *           - 按下滑块 ⇒ `setSliderDown(true)`（:581-582）。
 *         X 侧的 m_pressedControl 承担 Qt 同名私有成员的角色；m_activeSub/
 *         m_activeIsAdd（P0-67 的按钮按下着色）与 m_dragging 由其派生，
 *         保持既有绘制契约不变。 */
static void VXScrollBar_mousePressEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XMouseButton button;
    int pos;
    int contentLen;
    int sliderLen;
    XRect pressedRect;
    bool midAbsPos;
    bool leftAbsPos;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    button = XMouseEvent_button(me);
    /* Qt :528-529：上一次连发未收先收（重复按下的第二次 press）。 */
    if (XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb) !=
        XAbstractSliderSliderAction_NoAction)
        xsb_stopRepeatAction(sb);
    midAbsPos = xsb_middleClickAbsolute(sb);
    leftAbsPos = xsb_styleHintBool(sb,
                                   XStyleSH_ScrollBar_LeftClickAbsolutePosition);
    /* Qt :537-540 三段守卫合一。 */
    if (XAbstractSlider_maximum((const XAbstractSlider*)sb) ==
            XAbstractSlider_minimum((const XAbstractSlider*)sb) ||
        (XMouseEvent_buttons(me) & ~(XMouseButton)button) ||
        !(button == XMouseButton_LeftButton ||
          (midAbsPos && button == XMouseButton_MiddleButton))) {
        XEvent_ignore(event);
        return;
    }
    contentLen = xsb_horizontal(sb) ? XWidget_width(self)
                                    : XWidget_height(self);
    pos = xsb_horizontal(sb) ? XMouseEvent_position(me).x
                             : XMouseEvent_position(me).y;
    /* Qt :542：按样式层命中（与绘制同源），而非自算几何。 */
    sb->m_pressedControl = (int)xsb_hitTestComplex(
        sb, XMouseEvent_position(me).x, XMouseEvent_position(me).y);
    sb->m_pointerOutside = false;
    sb->m_snapBackPosition = XAbstractSlider_sliderPosition(
        (const XAbstractSlider*)sb);
    if (sb->m_pressedControl == (int)XStyleSC_None) {
        /* 命中空白（Qt 在按钮被隐藏时的极端几何下也可能发生）：不接受
         * 事件、不进任何状态（Qt 的 activateControl 对 None 是 no-op）。 */
        XEvent_ignore(event);
        return;
    }
    /* Qt :556-566：页区按下 + 绝对定位键（中键 midAbsPos，或左键
     * leftAbsPos）⇒ 滑块中心贴到点击处，pressedControl 改写为滑块。 */
    if (((uint32_t)sb->m_pressedControl ==
             (uint32_t)XStyleSC_ScrollBarAddPage ||
         (uint32_t)sb->m_pressedControl ==
             (uint32_t)XStyleSC_ScrollBarSubPage) &&
        ((midAbsPos && button == XMouseButton_MiddleButton) ||
         (leftAbsPos && button == XMouseButton_LeftButton))) {
        int sliderLength;
        int value = 0;
        sliderLen = xsb_sliderLen(sb, contentLen);
        sliderLength = xsb_handleLength(sb, sliderLen);
        xsb_posToValue(sb, pos - xsb_sliderOrigin(sb, contentLen) -
                       sliderLength / 2, sliderLen, &value);
        XAbstractSlider_setSliderPosition((XAbstractSlider*)sb, value);
        sb->m_pressedControl = (int)XStyleSC_ScrollBarSlider;
        sb->m_pressOffset = sliderLength / 2;
    } else if ((uint32_t)sb->m_pressedControl ==
               (uint32_t)XStyleSC_ScrollBarSlider) {
        /* Qt :551-553：clickOffset = 点击沿滚动轴分量 − 滑块起点。 */
        int sliderLen = xsb_sliderLen(sb, contentLen);
        int handlePos = xsb_sliderOrigin(sb, contentLen) +
                        xsb_handlePos(sb, sliderLen);
        sb->m_pressOffset = pos - handlePos;
    }
    /* P0-67 按钮按下着色：只有 Add/SubLine 计入 m_activeSub（滑块/页区由
     * m_dragging / m_pressedControl 表达，样式层不给它们画按钮底色）。 */
    if ((uint32_t)sb->m_pressedControl ==
            (uint32_t)XStyleSC_ScrollBarAddLine) {
        sb->m_activeSub = true;
        sb->m_activeIsAdd = 1;
    } else if ((uint32_t)sb->m_pressedControl ==
               (uint32_t)XStyleSC_ScrollBarSubLine) {
        sb->m_activeSub = true;
        sb->m_activeIsAdd = 0;
    }
    /* Qt :567-571：activateControl(pressedControl, initialDelay=500) 并重绘
     * 该子控件矩形。 */
    xsb_activateControl(sb, (uint32_t)sb->m_pressedControl, 500);
    pressedRect = xsb_subControlRect(sb, sb->m_pressedControl);
    if (pressedRect.width > 0 && pressedRect.height > 0)
        XWidget_updateRect(self, &pressedRect);
    else
        XWidget_update(self);
    /* Qt :581-582：按下滑块 ⇒ setSliderDown(true)（发 sliderPressed）
     * 并抓鼠标——释放时指针可能已移出控件，不抓取会导致 RELEASE 路由给
     * 别的控件、拖动状态卡死（对标 QWidget::grabMouse 拖动语义）。 */
    if ((uint32_t)sb->m_pressedControl ==
        (uint32_t)XStyleSC_ScrollBarSlider) {
        sb->m_dragging = true;
        XAbstractSlider_setSliderDown((XAbstractSlider*)sb, true);
        XWidget_grabMouse((XWidget*)sb);
    }
    XEvent_accept(event);
}

/** @brief 鼠标移动（对标 QScrollBar::mouseMoveEvent，qscrollbar.cpp:605-660）。
 *  @details 三条分支（顺序与 Qt 一致）：
 *           ① `pressedControl == SC_ScrollBarSlider` ⇒ 按像素映射更新句柄
 *              位置；超过 PM_MaximumDragDistance 时回弹到按下时的位置
 *              （:618-628）；
 *           ② 非滑块、且 SH_ScrollBar_ScrollWhenPointerLeavesControl=0
 *              （Qt QCommonStyle 缺省 0，qcommonstyle.cpp:5442）⇒ 指针
 *              离开按下控件即**收连发**、移回即**恢复连发**
 *              （:647-658）；
 *           ③ 在 ② 之上，SH_ScrollBar_RollBetweenButtons=1 时（Qt 缺省 0，
 *              qcommonstyle.cpp:5290-5291，Fusion 未覆盖）按下在两端按钮
 *              之间滚动时可直接换按下按钮（:629-645）。
 *           ②③ 是 P0-E10 的兑现点（台账 :290/:4046 deferred）。
 *  @note Qt 首部 `if (!d->pressedControl) return;`（:608-609）与
 *        `if (!(buttons & (Left|Middle&&midAbsPos))) return;`（:613-616）
 *        是本函数的前置门；X 侧不抓鼠标时也可能收到 MOVE，故照抄。 */
static void VXScrollBar_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int pos;
    int contentLen;
    int value = 0;
    int x;
    int y;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    /* Qt :608-609：没有按下控件 ⇒ 什么也不做（悬停由 HoverMove 负责）。 */
    if (sb->m_pressedControl == (int)XStyleSC_None) return;
    me = (XMouseEvent*)event;
    /* Qt :613-616：必须是「左键按住」或「中键按住且中键绝对定位」。 */
    {
        XMouseButton btns = XMouseEvent_buttons(me);
        bool midAbsPos = xsb_middleClickAbsolute(sb);
        if (!((btns & XMouseButton_LeftButton) ||
              ((btns & XMouseButton_MiddleButton) && midAbsPos)))
            return;
    }
    contentLen = xsb_horizontal(sb) ? XWidget_width(self)
                                    : XWidget_height(self);
    x = XMouseEvent_position(me).x;
    y = XMouseEvent_position(me).y;
    pos = xsb_horizontal(sb) ? x : y;
    if ((uint32_t)sb->m_pressedControl ==
        (uint32_t)XStyleSC_ScrollBarSlider) {
        /* ① 拖动滑块（Qt :618-628）。xsb_posToValue 是纯映射——滑轨原点
         * 与按下偏移都在这里扣（Qt `pixelPosToRangeValue(pos - clickOffset)`，
         * qscrollbar.cpp:620），函数内部不得再扣，双重扣减会让滑块自己跳。 */
        int sliderLen = xsb_sliderLen(sb, contentLen);
        int maxDrag = xsb_pixelMetric(sb, XStylePM_MaximumDragDistance, -1);
        xsb_posToValue(sb, pos - xsb_sliderOrigin(sb, contentLen) -
                       sb->m_pressOffset, sliderLen, &value);
        if (maxDrag >= 0) {
            XRect r = XWidget_rect(self);
            r.x -= maxDrag;
            r.y -= maxDrag;
            r.width += maxDrag * 2;
            r.height += maxDrag * 2;
            if (!XRect_contains(&r, x, y))
                value = sb->m_snapBackPosition;
        }
        XAbstractSlider_setSliderPosition((XAbstractSlider*)sb, value);
        XEvent_accept(event);
        return;
    }
    /* ②③ 非滑块按下（Qt :629-658）。 */
    if (xsb_styleHintBool(sb,
                          XStyleSH_ScrollBar_ScrollWhenPointerLeavesControl)) {
        /* 提示为真 ⇒ 指针离开控件仍继续滚动（Qt 该分支整体跳过）。 */
        XEvent_accept(event);
        return;
    }
    if (xsb_styleHintBool(sb, XStyleSH_ScrollBar_RollBetweenButtons) &&
        ((uint32_t)sb->m_pressedControl ==
             (uint32_t)XStyleSC_ScrollBarAddLine ||
         (uint32_t)sb->m_pressedControl ==
             (uint32_t)XStyleSC_ScrollBarSubLine)) {
        /* ③ 在两端按钮之间滚动（Qt :631-645）——纯命中，不写悬停状态。 */
        uint32_t newSc = xsb_hitTestComplex(sb, x, y);
        XRect scRect;
        XRect oldRect;
        XRect dirty;
        int oldPressed = sb->m_pressedControl;
        if (newSc == (uint32_t)sb->m_pressedControl &&
            !sb->m_pointerOutside) {
            XEvent_accept(event);
            return; /* 命中未变且指针在内：无事可做。 */
        }
        if (newSc == (uint32_t)XStyleSC_ScrollBarAddLine ||
            newSc == (uint32_t)XStyleSC_ScrollBarSubLine) {
            sb->m_pointerOutside = false;
            scRect = xsb_subControlRect(sb, (int)newSc);
            oldRect = xsb_subControlRect(sb, oldPressed);
            dirty = XRect_united(&scRect, &oldRect);
            sb->m_pressedControl = (int)newSc;
            sb->m_activeSub = true;
            sb->m_activeIsAdd =
                (newSc == (uint32_t)XStyleSC_ScrollBarAddLine) ? 1 : 0;
            /* Qt :641 activateControl(pressedControl, 0)：立刻开始连发。 */
            xsb_activateControl(sb, newSc, 0);
            if (dirty.width > 0 && dirty.height > 0)
                XWidget_updateRect(self, &dirty);
            else
                XWidget_update(self);
            XEvent_accept(event);
            return;
        }
    }
    /* ② 指针离开按下控件 ⇒ 收连发；移回 ⇒ 恢复连发（Qt :647-658）。 */
    {
        XRect pr = xsb_subControlRect(sb, sb->m_pressedControl);
        bool inside = (pr.width > 0 && pr.height > 0)
                          ? XRect_contains(&pr, x, y) : false;
        if (inside == sb->m_pointerOutside) {
            /* Qt `if (pr.contains(pos) == d->pointerOutsidePressedControl)`：
             * 两者相等即需要翻转（在内且标记已移出 ⇒ 移回；在外且未标记 ⇒
             * 移出）。 */
            sb->m_pointerOutside = !sb->m_pointerOutside;
            if (sb->m_pointerOutside) {
                sb->m_pointerOutside = true;
                XAbstractSlider_setRepeatAction(
                    (XAbstractSlider*)sb,
                    XAbstractSliderSliderAction_NoAction, 0, 0);
                if (pr.width > 0 && pr.height > 0)
                    XWidget_updateRect(self, &pr);
                else
                    XWidget_update(self);
            } else {
                /* Qt :656 activateControl(pressedControl)（缺省阈值 500）。 */
                xsb_activateControl(sb, (uint32_t)sb->m_pressedControl, 500);
            }
        }
    }
    XEvent_accept(event);
}

/** @brief 鼠标释放（对标 QScrollBar::mouseReleaseEvent，qscrollbar.cpp:589-599）：
 *         没有按下控件或还有别的键按着 ⇒ 直接返回；否则 stopRepeatAction()
 *         统一收口（含滑块 sliderDown 抬起与按下子控件重绘）。
 *  @note  Qt 的 stopRepeatAction 是**唯一**收口点：它同时清 pressedControl、
 *         收连发、抬 sliderDown（若先前按下的是滑块）并重绘该子控件矩形。
 *         P0-E10 之后 X 侧照此单点收口，不再各处手写 m_activeSub/m_dragging
 *         复位。 */
static void VXScrollBar_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XMouseButton button;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    button = XMouseEvent_button(me);
    /* Qt :592-593：没有按下控件 ⇒ 直接返回。 */
    if (sb->m_pressedControl == (int)XStyleSC_None) return;
    /* Qt :595-596：还有别的键按着 ⇒ 直接返回（等最后一个键释放）。 */
    if (XMouseEvent_buttons(me) & ~(XMouseButton)button) return;
    /* Qt :598：单点收口。 */
    xsb_stopRepeatAction(sb);
    /* 拖动的鼠标抓取必须释放（Qt 无显式 grab 配对，X 侧 grabMouse 必须
     * releaseMouse；顺序放在收口之后，避免 releaseMouse 期间再入）。 */
    if (sb->m_dragging) {
        sb->m_dragging = false;
        XWidget_releaseMouse((XWidget*)sb);
    }
    XEvent_accept(event);
}

/* ==================== 悬停事件槽 / timerEvent（§B8） ==================== */

/** @brief HoverEnter/HoverLeave/HoverMove 槽（对标 QScrollBar::event 的
 *         hover 分支，qscrollbar.cpp:441-446）：
 *         `if (const QHoverEvent *he = static_cast<const QHoverEvent *>(event))
 *              d_func()->updateHoverControl(he->position().toPoint());`
 *  @details Qt 6.8 三条 hover 事件都带坐标，故三条走同一段逻辑；HoverLeave
 *           的 position 是 (-1,-1)（qapplication.cpp:2094-2095），命中必然
 *           落到 SC_None ⇒ 清空悬停态，与「指针离开」等价。
 *           处理完即 **accept**（与实现一致）：Qt 的 QWidget::event 对
 *           HoverEnter/Leave/Move 有独立分支，消费后不再转 enterEvent/
 *           leaveEvent；X 侧同口径——只做子控件追踪后接受，不上抛父链
 *           （event() 返回值与 accept 是两回事：X 的 XObject_event_base
 *           返回 false 仅表示「父链无槽消费」，事件本体已 accept）。 */
static void VXScrollBar_hoverEventSlot(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XHoverEvent* he = (XHoverEvent*)event;
    XPoint pos;
    if (!sb || !event) return;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_HOVER_ENTER:
    case XEVENT_TYPE_HOVER_LEAVE:
    case XEVENT_TYPE_HOVER_MOVE:
        break;
    default:
        return;
    }
    pos = XHoverEvent_position(he);
    XScrollBar_updateHoverControl(sb, pos.x, pos.y);
    XEvent_accept(event);
}

/** @brief 定时器事件：flash 到期清位重绘（对标 QScrollBar::event 的 Timer
 *         分支，qscrollbar.cpp:453-464）+ transient 淡出/淡入推进。
 *  @details Qt 原文：
 *           `if (timerId == d->flashTimer) {
 *                initStyleOption(&opt);
 *                if (d->flashed && styleHint(SH_ScrollBar_Transient, &opt, this)) {
 *                    d->flashed = false;  update();
 *                }
 *                killTimer(d->flashTimer);  d->flashTimer = 0;
 *            }`
 *           其余定时器交父类（长按连发在 XAbstractSlider 的 TimerEvent 槽，
 *           本槽必须显式回落，否则连发哑掉）。 */
static void VXScrollBar_timerEvent(XObject* self, XTimerEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XTimerId id;
    if (!sb || !event) return;
    id = XTimerEvent_timerId(event);
    if (id != XTIMER_INVALID_ID && id == sb->m_flashTimerId) {
#if XSTYLE_ON
        /* Qt :454-459：flashed && styleHint(Transient) ⇒ 清 flashed + 重绘；
         * 定时器一律收掉（flash 是一次性 0ms 闸门，qscrollbar.cpp:1440）。 */
        XStyleOption opt;
        xsb_initOption(sb, &opt);
        if (sb->m_flashed &&
            XStyle_styleHint_base(XWidget_style((const XWidget*)self),
                                  XStyleSH_ScrollBar_Transient, &opt,
                                  (const XWidget*)self) != 0) {
            sb->m_flashed = false;
            XWidget_update((XWidget*)self);
        }
#endif /* XSTYLE_ON */
        XObject_killTimer(self, sb->m_flashTimerId);
        sb->m_flashTimerId = XTIMER_INVALID_ID;
        return;
    }
    if (id != XTIMER_INVALID_ID && id == sb->m_fadeTimerId) {
        xsb_fadeTick(sb);
        return;
    }
    /* 连发/单步定时器归 XAbstractSlider（repeatTimerId 的到期推进在
     * VXAbstractSlider_timerEvent，XAbstractSlider.c:226-247）。回落必须
     * 指名**直接父类**：XClass_Parent(Type, ...) 取的是 Type 自身 class_init
     * 注册的槽，写 XObject 会跳过父类实现——长按连发整条哑掉
     * （先例：XToolButton.c:1014-1016）。 */
    XClass_Parent(XAbstractSlider, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(self, event);
}

/** @brief 隐藏：收掉按压连发与拖动闸（对标 QScrollBar::hideEvent
 *         qscrollbar.cpp:692-700：pressedControl 清空 + SliderNoAction）。
 *  @note  Qt 侧只用 pressedControl 表达按压，故 hideEvent 不动
 *         sliderDown；X 侧按压态拆成 m_dragging + m_activeSub + sliderDown
 *         三处，隐藏时若不把 sliderDown 一并抬起，控件会带着「按下」
 *         状态回来（Qt 无此中间态）。
 *         [§B8] 另按 Qt 的「隐藏即不再悬停」把 hover 态与 flash 一并清掉
 *         （Qt 侧 hoverControl 由 hover 事件流自然失效；X 侧控件隐藏后
 *         收不到 HoverLeave，必须在此显式清）。 */
static void VXScrollBar_hideEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    if (!sb) return;
    /* Qt 的条件是 pressedControl 非空（滑块/轨道/按钮任一按下）；
     * X 侧该信息散在 m_dragging、m_activeSub 与重复动作三处，逐项判定
     * 后统一收闸。 */
    if (sb->m_dragging || sb->m_activeSub ||
        sb->m_pressedControl != (int)XStyleSC_None ||
        XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb) !=
            XAbstractSliderSliderAction_NoAction) {
        sb->m_dragging = false;
        sb->m_activeSub = false;
        sb->m_pressedControl = (int)XStyleSC_None;
        sb->m_pointerOutside = false;
        XAbstractSlider_setRepeatAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_NoAction, 0, 0);
        if (XAbstractSlider_isSliderDown((const XAbstractSlider*)sb)) {
            XAbstractSlider_setSliderDown((XAbstractSlider*)sb, false);
            XWidget_releaseMouse(self);
        }
    }
    /* 悬停态与 flash 一并清（隐藏后不再有 hover 事件流）。 */
    sb->m_hoverControl = (uint32_t)XStyleSC_None;
    XRect_init(&sb->m_hoverRect, 0, 0, 0, 0);
    if (sb->m_flashTimerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, sb->m_flashTimerId);
        sb->m_flashTimerId = XTIMER_INVALID_ID;
    }
    sb->m_flashed = false;
    XClass_Parent(XWidget, EXWidget_HideEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== 右键标准菜单动作执行 ==================== */

/* 上下文目标记录：菜单为模态弹出，同一时刻至多一个滚动条菜单在途。
 * 记录拥有者与右击沿滚动方向分量（"滚动到此处"的目标坐标——此前
 * 右击位置被丢弃、动作零连接），动作触发时校验拥有者防串扰。 */
static const XScrollBar* xsb_ctxOwner = NULL;
static int xsb_ctxPos = 0;

/** @brief "滚动到此处"：右击点沿滑轨映射为滑块原点值并定位
 *         （映射公式与拖动 VXScrollBar_mouseMoveEvent 同源；xsb_posToValue
 *         为纯映射，本分支**不扣** pressOffset——与 HEAD 一致：拖动中的
 *         clickOffset 只属于拖动会话，右击路径不消费）。 */
static void xsb_actScrollHereSlot(XObject* receiver, XVarList* args)
{
    XScrollBar* sb = (XScrollBar*)receiver;
    int contentLen;
    int value = 0;
    (void)args;
    if (!sb || (const XScrollBar*)sb != xsb_ctxOwner) return;
    contentLen = xsb_horizontal(sb) ? XWidget_width((XWidget*)sb)
                                    : XWidget_height((XWidget*)sb);
    xsb_posToValue(sb, xsb_ctxPos - xsb_sliderOrigin(sb, contentLen),
                   xsb_sliderLen(sb, contentLen), &value);
    XAbstractSlider_setValue((XAbstractSlider*)sb, value);
}

/** @brief "顶部/左缘"：滚动到最小值。 */
static void xsb_actMinimumSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_setValue((XAbstractSlider*)receiver,
                                 XAbstractSlider_minimum(
                                     (const XAbstractSlider*)receiver));
}

/** @brief "底部/右缘"：滚动到最大值（本库滚动条最大值即可滚跨度，
 *         对标 QScrollArea 的 range 收敛）。 */
static void xsb_actMaximumSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_setValue((XAbstractSlider*)receiver,
                                 XAbstractSlider_maximum(
                                     (const XAbstractSlider*)receiver));
}

/** @brief "向上(左)翻页"。 */
static void xsb_actPageSubSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_triggerAction((XAbstractSlider*)receiver,
            XAbstractSliderSliderAction_PageStepSub);
}

/** @brief "向下(右)翻页"。 */
static void xsb_actPageAddSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_triggerAction((XAbstractSlider*)receiver,
            XAbstractSliderSliderAction_PageStepAdd);
}

/** @brief "向上(左)滚动"（单步）。 */
static void xsb_actStepSubSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_triggerAction((XAbstractSlider*)receiver,
            XAbstractSliderSliderAction_SingleStepSub);
}

/** @brief "向下(右)滚动"（单步）。 */
static void xsb_actStepAddSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_triggerAction((XAbstractSlider*)receiver,
            XAbstractSliderSliderAction_SingleStepAdd);
}

/** @brief 右键标准菜单（对标 QScrollBar::contextMenuEvent）：滚动到此处/
 *         上(左)缘/下(右)缘/翻页/单步，条目文本随方向变化；
 *         popup + DeleteOnClose 呈现。 */
static void VXScrollBar_contextMenuEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XContextMenuEvent* ctx = (XContextMenuEvent*)event;
    XMenu* menu;
    XPoint global;
    bool horiz;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_CONTEXT_MENU) return;
    menu = XScrollBar_createStandardContextMenu(sb);
    if (!menu) return;
    horiz = xsb_horizontal(sb);
    /* 记录右击位置："滚动到此处"动作以该局部坐标为目标（此前坐标被
     * 丢弃）。[§B8 返工] 不再清 m_pressOffset：xsb_posToValue 已改为纯
     * 映射、"滚动到此处"槽自己扣滑轨原点不读偏移（Qt 6.8.3 的
     * contextMenuEvent 同样直接 pixelPosToRangeValue(pos)、不碰
     * clickOffset）——拖动会话进行中弹出右键菜单时不得改写拖动基线。 */
    if (ctx) {
        XPoint pos = XContextMenuEvent_position(ctx);
        xsb_ctxOwner = sb;
        xsb_ctxPos = horiz ? pos.x : pos.y;
    }
    global = XContextMenuEvent_globalPosition(ctx);
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    XMenu_popup(menu, &global);
    XEvent_accept(event);
}

/* ==================== 滚轮 / 事件 ==================== */

/** @brief 滚轮：对标 QScrollBar::wheelEvent（qscrollbar.cpp:475-497）。
 *  @details 与基类 wheelEvent 的差别（这正是此前「跨轴方向与 Qt 相反」
 *           的根因）：
 *           1) 按 **|dx| > |dy|** 选轴（基类只看 y 是否非零）；
 *           2) 水平轨道上若 y 不是主轴、但 x 非零 → **不消费**（Qt 原文
 *              "This is to avoid chaotic scrolling on touchpads"）；
 *           3) delta 取 `horizontal ? -dx : dy`，再交给方向感知的
 *              scrollByDelta（其内部对水平轴再取负一次，等价 Qt 的
 *              双重取负 + scrollbar 缺省 invertedControls=true）。
 *           实测锚点：水平滚动条 dx=+120（value=50）→ 47；竖直滚动条
 *           dx=+120 → 47；水平滚动条同时带 dx=+120/dy=+120 → 忽略。
 *           [§B8] 尾部按 Qt :494-497 处理滚动阶段：
 *           `if (event->phase() == Qt::ScrollBegin) d->setTransient(false);
 *            else if (event->phase() == Qt::ScrollEnd) d->setTransient(true);`
 *           ——手势滚动期间滚动条常驻（非 transient），结束后回到 transient
 *           收缩态。 */
static void VXScrollBar_wheelEvent(XWidget* self, XEvent* event)
{
#if XWINDOWEVENT_ON
    XScrollBar* sb = (XScrollBar*)self;
    XWheelEvent* we;
    XPoint delta;
    int horizontal;
    int d;
    XKeyboardModifiers mods;
    if (!sb || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
    we = (XWheelEvent*)event;
    XEvent_ignore(event);
    delta = XWheelEvent_angleDelta(we);
    horizontal = (delta.x < 0 ? -delta.x : delta.x) >
                 (delta.y < 0 ? -delta.y : delta.y);
    if (!horizontal && delta.x != 0 &&
        XAbstractSlider_orientation((const XAbstractSlider*)sb) ==
            XGuiOrientation_Horizontal)
        return; /* 触摸板混沌滚动守卫：保持 ignore。 */
    mods = XWheelEvent_modifiers(we);
    d = horizontal ? -delta.x : delta.y;
    if (XAbstractSlider_scrollByDelta(
            (XAbstractSlider*)sb,
            horizontal ? XGuiOrientation_Horizontal
                       : XGuiOrientation_Vertical,
            d, mods)) {
        XEvent_accept(event);
    } else {
        /* 高分辨率滚轮：angleDelta 为 0 时回落 pixelDelta（X 扩展）。 */
        XPoint pixel = XWheelEvent_pixelDelta(we);
        int pd = (pixel.y != 0) ? pixel.y : pixel.x;
        if (pd != 0 &&
            XAbstractSlider_scrollByDelta(
                (XAbstractSlider*)sb,
                horizontal ? XGuiOrientation_Horizontal
                           : XGuiOrientation_Vertical,
                pd, mods))
            XEvent_accept(event);
    }
    /* Qt :494-497：滚动阶段切换 transient 态。放在滚动处理之后、与 Qt
     * 原文同序（Qt 也是在 scrollByDelta 之后判断 phase）。 */
    {
        int phase = XWheelEvent_phase(we);
        if (phase == (int)XWheelEventPhase_ScrollBegin)
            XScrollBar_setTransient(sb, false);
        else if (phase == (int)XWheelEventPhase_ScrollEnd)
            XScrollBar_setTransient(sb, true);
    }
#else
    XEvent_ignore(event);
#endif /* XWINDOWEVENT_ON */
}

/** @brief 事件入口：对标 QScrollBar::event（qscrollbar.cpp:437-469）。
 *  @details 三个分支：
 *           - HoverEnter/HoverLeave/HoverMove ⇒ 子控件悬停追踪
 *             （qscrollbar.cpp:441-446）。X 的悬停合成器
 *             （XWindowSystemInterface）只合成 ENTER/LEAVE，不合成 hover
 *             事件族，故这里**兼作兜底**：ENOENT 时把 ENTER/MOUSE_MOVE 的
 *             坐标也喂给 updateHoverControl（见下方 XEVENT_TYPE_ENTER /
 *             XEVENT_TYPE_MOUSE_MOVE 分支）——这是本框架的生成点补齐，
 *             语义与 Qt 的 dispatchEnterLeave/HoverMove 合成一致；
 *           - StyleChange ⇒ `setTransient(styleHint(SH_ScrollBar_Transient))`
 *             （qscrollbar.cpp:447-451）+ 重算 sizeHint。X 扩展的
 *             `setShowButtons` 显式覆盖仍优先（m_showButtonsExplicit）；
 *           - Timer ⇒ flash 到期清位（在 VXScrollBar_timerEvent 里，Qt 把
 *             它放在 event() 的 Timer 分支；X 的 TimerEvent 是独立虚槽，
 *             故落到 TimerEvent 槽）。
 *  @note  Qt 的 `case QEvent::Timer`（qscrollbar.cpp:453-464）在 X 侧由
 *         EXObject_TimerEvent 槽承担——X 的 XObject 事件分发把 TIMER 事件
 *         直接路由到 timerEvent 虚槽，不进 Event 槽（XObject.c:372），
 *         故本函数不再重复处理 Timer。 */
static bool VXScrollBar_event(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    if (!sb || !event)
        return XClass_Parent(XWidget, EXObject_Event,
                             bool(*)(XObject*, XEvent*))((XObject*)self,
                                                         event);
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_HOVER_ENTER:
    case XEVENT_TYPE_HOVER_LEAVE:
    case XEVENT_TYPE_HOVER_MOVE:
        VXScrollBar_hoverEventSlot(self, event);
        break;
    case XEVENT_TYPE_ENTER:
        /* 兜底：本框架的悬停合成器只产 Enter/Leave、不产 hover 事件族，
         * 故指针进入本控件时以 Enter 携带的局部坐标刷新一次悬停子控件
         * （等价 Qt dispatchEnterLeave 给 WA_Hover 控件补发的 HoverEnter，
         * qapplication.cpp:2112-2118）。 */
#if XWINDOWEVENT_ON
        {
            XPoint p = XEnterEvent_position((const XEnterEvent*)event);
            XScrollBar_updateHoverControl(sb, p.x, p.y);
        }
#endif /* XWINDOWEVENT_ON */
        break;
    case XEVENT_TYPE_MOUSE_MOVE:
        /* 兜底：指针在控件内移动（无按键）时按 MOUSE_MOVE 坐标刷新悬停
         * （Qt 由 qapplication.cpp:2793-2804 合成 HoverMove）。 */
        if (sb->m_pressedControl == (int)XStyleSC_None) {
            XMouseEvent* me = (XMouseEvent*)event;
            XPoint p = XMouseEvent_position(me);
            XScrollBar_updateHoverControl(sb, p.x, p.y);
        }
        break;
    case XEVENT_TYPE_STYLE_CHANGE: {
#if XSTYLE_ON
        XStyleOption opt;
        xsb_initOption(sb, &opt);
        XScrollBar_setTransient(
            sb, XStyle_styleHint_base(XWidget_style((const XWidget*)self),
                                      XStyleSH_ScrollBar_Transient, &opt,
                                      (const XWidget*)self) != 0);
#else
        /* 无样式层：transient 恒 false（同 xsb_transientHint 口径），
         * 按钮显隐仍要落到缺省（显示）。 */
        XScrollBar_setTransient(sb, false);
#endif /* XSTYLE_ON */
        if (!sb->m_showButtonsExplicit) {
            bool show = !sb->m_transient;
            if (sb->m_showButtons != show) {
                sb->m_showButtons = show;
                XWidget_update(self);
            }
        }
        /* 样式切换会改变 PM_ScrollBarExtent/SliderMin，sizeHint 必须刷新并
         * 通知布局（QScrollBar::event 的 StyleChange 分支本意同此）。 */
        xsb_updateSizeHint(sb);
        XWidget_updateGeometry(self);
        break;
    }
    default:
        break;
    }
    return XClass_Parent(XWidget, EXObject_Event,
                         bool(*)(XObject*, XEvent*))((XObject*)self, event);
}

/** @brief sizeHint 虚槽（对标 QScrollBar::sizeHint）：按当前方向与样式
 *         度量现场重算，方向切换后自动横竖互换。 */
static XSize VXScrollBar_sizeHintSlot(const XWidget* self)
{
    if (!self) {
        XSize out;
        XSize_init(&out, -1, -1);
        return out;
    }
    xsb_updateSizeHint((XScrollBar*)self);
    return XClass_Parent(XWidget, EXWidget_SizeHint,
                         XSize (*)(const XWidget*))(self);
}

/* ==================== 生命周期与虚表 ==================== */

XVtable* XScrollBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollBar)
    XVTABLE_INHERIT_XCLASS(XAbstractSlider);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXScrollBar_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXScrollBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXScrollBar_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXScrollBar_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXScrollBar_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VXScrollBar_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXScrollBar_event);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXScrollBar_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXScrollBar_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent,
                             VXScrollBar_contextMenuEvent);
    return XVTABLE_DEFAULT;
}

void XScrollBar_init(XScrollBar* self, XWidget* parent, XWidgetFlags flags)
{
    XScrollBar_init_2(self, (int)XGuiOrientation_Vertical,
                      parent, flags);
}

void XScrollBar_init_2(XScrollBar* self, int orientation,
                       XWidget* parent, XWidgetFlags flags)
{
    XWidgetSizePolicy policy;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSlider_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XScrollBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XAbstractSlider_setOrientation((XAbstractSlider*)self, orientation);
    /* 对标 QScrollBarPrivate::init (qscrollbar.cpp:345)：
     * invertedControls = true（"since the scrollbar is inverted by
     * default"）——裸滚动条自带的滚轮方向因此与基类缺省相反（Qt
     * scrollByDelta 末尾按 invertedControls 取负）。XAbstractScrollArea
     * 的滚轮走自身 wheelEvent（不经本虚槽），不受影响。 */
    XAbstractSlider_setInvertedControls((XAbstractSlider*)self, true);
    /* 两端按钮缺省可见（对标 qscrollbar.cpp:353：transient 取
     * SH_ScrollBar_Transient 原值，Fusion=0 ⇒ !transient ⇒ 画
     * Add/SubLine 按钮）。 */
    self->m_showButtons = !xsb_transientHint(self);
    self->m_showButtonsExplicit = false;
    /* sizeHint（qscrollbar.cpp:411-425）：extent*2+sliderMin，横竖互换。
     * minimumSizeHint **不设置**——Qt 未覆写，基类返回无效尺寸
     * (-1,-1)（本机实测），布局按 Minimum 策略回落 sizeHint。 */
    xsb_updateSizeHint(self);
    /* 对标 QScrollBarPrivate::init（qscrollbar.cpp:357-361）：
     * QSizePolicy(Minimum, Fixed, Slider)，竖直转置；随后清
     * OwnSizePolicy 位（Qt 同函数），setOrientation 才能继续转置。 */
    policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Minimum,
                                         XWidgetSizePolicy_Fixed,
                                         XWidgetSizePolicyControl_Slider);
    if (orientation == XGuiOrientation_Vertical)
        policy = XWidgetSizePolicy_transposed(&policy);
    XWidget_setSizePolicyFull((XWidget*)self, &policy);
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_WState_OwnSizePolicy, false);
    /* 悬停外观 opt-in：绘制按 State_MouseOver 出悬停高亮，ENTER/
     * LEAVE 须标脏自矩形（对标 Qt polish 的 WA_Hover 收口）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);
    /* [§B8] 对标 QScrollBarPrivate::init（qscrollbar.cpp:345-355）：
     *   pressedControl = hoverControl = SC_None;      → 已由 XMemset 清零
     *   pointerOutsidePressedControl = false;         → 同上
     *   transient = styleHint(SH_ScrollBar_Transient); → 下方按未显式
     *      覆盖口径取 !m_showButtons（两者互为反相，见上面 m_showButtons
     *      的赋值注释）
     *   flashed = false; flashTimer = 0;              → 下方显式置位
     * 定时器 ID 在 XMemset 后为 0，而 0 是**合法**定时器 id（XTypes.h：
     * XTIMER_INVALID_ID = -1），必须显式置无效，否则 timerEvent 里
     * `id == m_flashTimerId` 会把 fd 0 的其它定时器误认成 flash。 */
    self->m_transient = !self->m_showButtons;
    self->m_flashed = false;
    self->m_flashTimerId = XTIMER_INVALID_ID;
    self->m_fadeTimerId = XTIMER_INVALID_ID;
    self->m_fadeMode = (uint8_t)XsbFadeMode_Idle;
    self->m_fadeStep = (uint32_t)XSB_FADE_STEPS;
    self->m_transientOpacity = 1.0f;
    self->m_pressedControl = (int)XStyleSC_None;
    self->m_hoverControl = (uint32_t)XStyleSC_None;
    self->m_pointerOutside = false;
    self->m_snapBackPosition = 0;
    XRect_init(&self->m_hoverRect, 0, 0, 0, 0);
}

void XScrollBar_setShowButtons(XScrollBar* self, bool show)
{
    if (!self || self->m_showButtons == show) {
        if (self) self->m_showButtonsExplicit = true;
        return;
    }
    self->m_showButtons = show;
    self->m_showButtonsExplicit = true;
    XWidget_update((XWidget*)self);
}

bool XScrollBar_showButtons(const XScrollBar* self)
{ return self ? self->m_showButtons : false; }

/* ==================== 子控件悬停 / transient ==================== */

int XScrollBar_hoverControl(const XScrollBar* self)
{
    return self ? (int)self->m_hoverControl : (int)XStyleSC_None;
}

bool XScrollBar_updateHoverControl(XScrollBar* self, int x, int y)
{
    uint32_t lastHoverControl;
    XRect lastHoverRect;
    bool doesHover;
    if (!self) return false;
    /* Qt :164-166 快照旧值 + 读 WA_Hover。 */
    lastHoverControl = self->m_hoverControl;
    lastHoverRect = self->m_hoverRect;
    doesHover = XWidget_testAttribute((const XWidget*)self,
                                      XWidgetAttribute_Hover);
    if (lastHoverControl != xsb_newHoverControl(self, x, y) && doesHover) {
        /* Qt :168-170：先 update(旧矩形) 再 update(新矩形)——两段标脏。
         * 空矩形按 XWidget_updateRect 的 addDirty 语义（宽或高 <=0 不入账），
         * 故直接调不会产生伪脏区。 */
        if (lastHoverRect.width > 0 && lastHoverRect.height > 0)
            XWidget_updateRect((XWidget*)self, &lastHoverRect);
        if (self->m_hoverRect.width > 0 && self->m_hoverRect.height > 0)
            XWidget_updateRect((XWidget*)self, &self->m_hoverRect);
        return true;
    }
    /* Qt :172：`return !doesHover;`——未声明 Hover 属性时返回 true（调用方
     * 据此不额外标脏）。 */
    return !doesHover;
}

void XScrollBar_flash(XScrollBar* self)
{
    if (!self) return;
    /* Qt :210-216：仅在未点亮且样式为 transient 时点亮；不可见则 show，
     * 可见则 update。transient 提示查询统一走 xsb_transientHint（内含
     * XSTYLE_ON 守卫与 false 回落）。 */
    if (!self->m_flashed && xsb_transientHint(self)) {
        self->m_flashed = true;
        if (!XWidget_isVisible((const XWidget*)self))
            XWidget_show((XWidget*)self);
        else
            XWidget_update((XWidget*)self);
    }
    /* Qt :217-218：`if (!flashTimer) flashTimer = q->startTimer(0);`
     * 0 毫秒语义在 X 侧等价于「下一轮事件循环尽快触发」（XObject_startTimer
     * 的 0 间隔按 1ns 下限注册，见 XObject.h 的 @note ①）。 */
    if (self->m_flashTimerId == XTIMER_INVALID_ID) {
        self->m_flashTimerId = XObject_startTimer((XObject*)self, 0,
                                                  XTimerType_CoarseTimer);
    }
    /* flash 同时把不透明度拉回完全不透明并取消淡出（Qt 的 flash 语义是
     * 「闪一下提示用户」，若正处于淡出中必须先拉回可见）。 */
    if (self->m_transientOpacity < 1.0f) {
        self->m_transientOpacity = 1.0f;
        self->m_fadeStep = (uint32_t)XSB_FADE_STEPS;
        xsb_stopFade(self);
        XWidget_update((XWidget*)self);
    }
}

bool XScrollBar_flashed(const XScrollBar* self)
{ return self ? self->m_flashed : false; }

bool XScrollBar_transient(const XScrollBar* self)
{ return self ? self->m_transient : false; }

void XScrollBar_setTransient(XScrollBar* self, bool transient)
{
    if (!self || self->m_transient == transient) return;
    self->m_transient = transient;
    if (XWidget_isVisible((const XWidget*)self)) {
        /* Qt :194-198：仅在样式提示为 transient 时 update（非 transient
         * 样式下 transient 位翻转没有视觉后果）。同 flash，查询走
         * xsb_transientHint（XSTYLE_ON 守卫内联于此）。 */
        if (xsb_transientHint(self)) {
            XWidget_update((XWidget*)self);
            if (transient) xsb_fadeOut(self);
            else xsb_fadeIn(self);
        }
    } else if (!transient) {
        /* Qt :199-201：不可见且切到非 transient ⇒ show。 */
        XWidget_show((XWidget*)self);
    }
}

float XScrollBar_transientOpacity(const XScrollBar* self)
{ return self ? self->m_transientOpacity : 1.0f; }

XScrollBar* XScrollBar_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XScrollBar* self = (XScrollBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XScrollBar_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XScrollBar* XScrollBar_create_ex_2(XMemoryType memory, int orientation,
                                   XWidget* parent, XWidgetFlags flags)
{
    XScrollBar* self = (XScrollBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XScrollBar_init_2(self, orientation, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 右键标准菜单 ==================== */

/** @brief 追加动作并接通触发槽（动作由菜单拥有，连接随任一侧析构
 *         自动摘除；此前动作零连接、选择无效果）。 */
static void xsb_addWiredAction(XMenu* menu, const char* utf8, XScrollBar* sb,
                               XSlotFunc1 slot)
{
    XAction* action = XMenu_addAction_2(menu, utf8);
    if (!action) return;
    XObject_connect((XObject*)action,
                      XSignal(XAction_triggered_signal),
                      (XObject*)sb, slot, XConnectionType_Direct);
}

XMenu* XScrollBar_createStandardContextMenu(XScrollBar* self)
{
    XMenu* menu;
    bool horiz;
    if (!self) return NULL;
    menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "ctx");
    if (!menu) return NULL;
    horiz = xsb_horizontal(self);
    xsb_addWiredAction(menu, "滚动到此处", self, xsb_actScrollHereSlot);
    XMenu_addSeparator(menu);
    xsb_addWiredAction(menu, horiz ? "左缘" : "顶部", self,
                       xsb_actMinimumSlot);
    xsb_addWiredAction(menu, horiz ? "右缘" : "底部", self,
                       xsb_actMaximumSlot);
    XMenu_addSeparator(menu);
    xsb_addWiredAction(menu, horiz ? "向左翻页" : "向上翻页", self,
                       xsb_actPageSubSlot);
    xsb_addWiredAction(menu, horiz ? "向右翻页" : "向下翻页", self,
                       xsb_actPageAddSlot);
    XMenu_addSeparator(menu);
    xsb_addWiredAction(menu, horiz ? "向左滚动" : "向上滚动", self,
                       xsb_actStepSubSlot);
    xsb_addWiredAction(menu, horiz ? "向右滚动" : "向下滚动", self,
                       xsb_actStepAddSlot);
    return menu;
}

#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON */

#endif /* XGUI_ON */
