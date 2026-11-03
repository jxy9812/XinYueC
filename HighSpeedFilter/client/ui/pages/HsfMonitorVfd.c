/**
 * @file       HsfMonitorVfd.c
 * @brief      变频器监视页实现：自绘仪表 + 状态横幅 + 数值卡 + 给定/操作 + 趋势。
 * @details    [层次]（TopToBottom）
 *               1) 状态横幅（自绘胶囊：状态色块 + 大字状态 + 故障码/名）
 *               2) 仪表行（三枚 270° 弧形仪表自绘控件：频率/电流/电压）
 *               3) 数值卡行（2×2：功率/母线电压/转速/转矩；validMask 显 "--"）
 *               4) 给定行（Slider 0..500 ×0.1Hz + SpinBox + 下发）
 *               5) 操作行（正转/停止/反转/点动/复位；caps 裁剪 + 闸联动）
 *               6) 趋势图（XChartView+XLineSeries 输出频率 60 点滚动窗）
 *               7) 底行（控制闸 CheckBox「允许界面操作」）
 *
 *             [数据路径] provider->subscribe 注册回调；模拟源经 200ms XTimer
 *             驱动 hsfVfdSimTick → 设备解码 → statusUpdated → 回调刷新。
 *             页面只读 XVfdDevice_status_const()，控制只走 provider 动作槽。
 *
 *             [闸联动] controlEnabled=false ⇒ 操作按钮/下发全 setEnabled(false)
 *             （Qt 风格禁用态）；开关自身不受闸控制。
 *
 *             [精度映射] 频率 0.1Hz 定点 → 界面乘 10 用整数控件承载（数值
 *             精度零损失）；显示时除回 10。
 */
#include "HsfMonitorVfd.h"

#include "HsfTheme.h"
#include "HsfVfdProvider.h"
#if XVFD_ON
#include "HsfVfdDemoSim.h"
#endif
#include "XLabel.h"
#include "XPushButton.h"
#include "XComboBox.h"
#include "XCheckBox.h"
#include "XSpinBox.h"
#include "XSlider.h"
#include "XBoxLayout.h"
#include "XGridLayout.h"
#include "XChartView.h"
#include "XChart.h"
#include "XLineSeries.h"
#include "XValueAxis.h"
#include "XObject.h"
#include "XTimer.h"
#include "XSystem.h"               /* XSystem_environment（XHSF_VFD_AUTO 外部门）。 */
#include "XMemory.h"
#include "XFont.h"
#include "XPainter.h"
#include "XWidget_Protected.h"   /* XWidget_paintOffset（自绘平移用）。 */
#include "XString.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XVFD_ON

/* ==================== 常量与配色 ==================== */

#define VFD_GAUGE_H          150      /**< 仪表控件高度。 */
#define VFD_BANNER_H         56       /**< 状态横幅高度。 */
#define VFD_CARD_H           64       /**< 数值卡高度。 */
#define VFD_TREND_H          160      /**< 趋势图高度。 */
#define VFD_FREQ_MAX10       500      /**< 频率给定上限（0.1Hz 定点 = 50.0Hz）。 */
#define VFD_TREND_POINTS     60       /**< 趋势滚动窗点数。 */
#define VFD_TREND_WIN_DEF    30.0     /**< 时间窗默认宽（秒）。 */
#define VFD_TREND_WIN_MIN    1.0      /**< 时间窗最小宽（秒；再窄无意义）。 */
#define VFD_TREND_WIN_MAX    600.0    /**< 时间窗最大宽（秒）。 */
#define VFD_TREND_CATCHUP    0.05     /**< 恢复跟随的右缘容差（秒；≈1px）。 */

/* 状态色（WCAG 对比度校过；横幅深底白字）。 */
#define VFD_COLOR_RUN        0xFF2E9E44u  /**< 运行绿。 */
#define VFD_COLOR_STOP       0xFF8A98A6u  /**< 停机灰。 */
#define VFD_COLOR_FAULT      0xFFD13438u  /**< 故障红。 */
#define VFD_COLOR_WARN       0xFFCA8A04u  /**< 警告橙。 */
#define VFD_COLOR_READY      0xFF1F6FB2u  /**< 就绪蓝。 */
#define VFD_COLOR_TRACK      0xFFC9D2DAu  /**< 仪表底轨。 */
#define VFD_COLOR_GAUGE      0xFF1F4E79u  /**< 仪表进度（品牌蓝）。 */
#define VFD_COLOR_TEXT       0xFF1A2733u  /**< 主文本。 */
#define VFD_COLOR_TEXT_DIM   0xFF5A6572u  /**< 弱文本。 */

/** @brief 运行状态 → 横幅底色/文本。 */
static void vfdRunStateStyle(int runState, uint32_t* color, const char** text)
{
    switch (runState) {
    case XVfdRunState_RunningForward:
    case XVfdRunState_RunningReverse:
    case XVfdRunState_ConstantSpeed:
    case XVfdRunState_Accelerating:
    case XVfdRunState_Decelerating:
    case XVfdRunState_JogForward:
    case XVfdRunState_JogReverse:
        *color = VFD_COLOR_RUN;  *text = "\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD"; /* 运行中 */
        break;
    case XVfdRunState_Fault:
        *color = VFD_COLOR_FAULT; *text = "\xE6\x95\x85\xE9\x9A\x9C";           /* 故障 */
        break;
    case XVfdRunState_Stopped:
        *color = VFD_COLOR_STOP; *text = "\xE5\x81\x9C\xE6\x9C\xBA";            /* 停机 */
        break;
    case XVfdRunState_Ready:
        *color = VFD_COLOR_READY; *text = "\xE5\xB0\xB1\xE7\xBB\xAA";           /* 就绪 */
        break;
    case XVfdRunState_DcBraking:
        *color = VFD_COLOR_WARN; *text = "\xE7\x9B\xB4\xE6\xB5\x81\xE5\x88\xB6\xE5\x8A\xA8"; /* 直流制动 */
        break;
    case XVfdRunState_FreeStopping:
        *text = "\xE8\x87\xAA\xE7\x94\xB1\xE5\x81\x9C\xE8\xBD\xA6";             /* 自由停车 */
        *color = VFD_COLOR_WARN;
        break;
    case XVfdRunState_QuickStopping:
        *text = "\xE5\xBF\xAB\xE9\x80\x9F\xE5\x81\x9C\xE6\x9C\xBA";             /* 快速停机 */
        *color = VFD_COLOR_WARN;
        break;
    default:
        *color = VFD_COLOR_STOP; *text = "\xE6\x9C\xAA\xE7\x9F\xA5";            /* 未知 */
        break;
    }
}

/* ==================== 仪表组类型（先于页对象：页结构体内嵌） ==================== */

/* 前置声明（类初始化先于绘制函数定义）。 */
typedef struct HsfMonitorVfd HsfMonitorVfd;
static void VHsfVfdGauge_paintEvent(XWidget* self, XEvent* event);
static void VHsfVfdBanner_paintEvent(XWidget* self, XEvent* event);

XCLASS_DEFINE_BEGING(HsfVfdGaugeWidget)
XCLASS_DEFINE_EXTEND_END(HsfVfdGaugeWidget, XWidget)

/**
 * @brief 仪表画布（弧线自绘；标题/数值/单位为叠放其上的 XLabel 子控件，
 *        由框架托管几何与裁剪——2026-11-03 用户实锅：滚动壳嵌套下自绘
 *        字不在布局树里，坐标/裁剪不可靠，拉伸时才偶现）。
 *        分派用 m_owner+m_slot（2026-11-03 实锅：offsetof 反推对非首成员
 *        画布算出错误页指针 → provider 读垃圾 → 恒走空轨早退）。
 */
typedef struct HsfVfdGaugeWidget {
    XWidget m_base;                     /**< 基类子对象（第一成员）。 */
    HsfMonitorVfd* m_owner;             /**< 回属页（分派+取数）。 */
    int m_slot;                         /**< 仪表槽位（0=频率 1=电流 2=电压）。 */
} HsfVfdGaugeWidget;

/**
 * @brief 仪表组（画布 + 三枚叠放标签：标题/数值/单位）。
 * @note  标签固定在画布内的设计位置（gauge 固定高、最小宽，宽向伸缩时
 *        画布宽变 → 标签用 setGeometry 由 vfdGaugeRelayout 逐次跟随）。
 */
typedef struct HsfVfdGauge {
    HsfVfdGaugeWidget m_canvas;         /**< 弧线画布（自绘表盘）。 */
    XLabel m_title;                     /**< 顶部标题（如 输出频率）。 */
    XLabel m_value;                     /**< 中心大字数值。 */
    XLabel m_unit;                      /**< 数值下单位（Hz/A/V）。 */
} HsfVfdGauge;

/** @brief 标签跟随画布几何（画布 resize/自适应后调；幂等）。 */
static void vfdGaugeRelayout(HsfVfdGauge* g)
{
    int w = XWidget_width(&g->m_canvas.m_base);
    int h = XWidget_height(&g->m_canvas.m_base);
    if (w <= 0 || h <= 0) return;
    XWidget_setGeometry((XWidget*)&g->m_title, 0, 6, w, 18);
    XWidget_setGeometry((XWidget*)&g->m_value, 0, h / 2 - 12, w, 22);
    XWidget_setGeometry((XWidget*)&g->m_unit, 0, h / 2 + 12, w, 16);
}

XVtable* HsfVfdGauge_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfVfdGaugeWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfVfdGauge_paintEvent);
    XCLASS_SHOW_SIZE_DEFAULT(HsfVfdGaugeWidget);
    return XVTABLE_DEFAULT;
}

/* ==================== 页对象 ==================== */

/** @brief 变频器页对象；m_base 必须是第一个成员。 */
typedef struct HsfMonitorVfd
{
    XWidget      m_base;        /**< 页容器；必须是第一个成员。 */
    XVBoxLayout  m_layout;      /**< 页内垂直布局。 */

    /* 区 1：状态横幅（自绘）。 */
    XWidget      m_banner;      /**< 横幅画布。 */
    /* 区 2：仪表行（三枚仪表组 + 卡片列）。 */
    XHBoxLayout  m_gaugeRow;    /**< 仪表行布局。 */
    HsfVfdGauge  m_gaugeFreq;   /**< 输出频率仪表组。 */
    HsfVfdGauge  m_gaugeCur;    /**< 输出电流仪表组。 */
    HsfVfdGauge  m_gaugeVolt;   /**< 输出电压仪表组。 */
    XWidget*     m_cardHost;    /**< 数值卡宿主（grid 布局挂接）。 */
    XLabel       m_cardPower;   /**< 功率卡（两行文本）。 */
    XLabel       m_cardBus;     /**< 母线电压卡。 */
    XLabel       m_cardRpm;     /**< 转速卡。 */
    XLabel       m_cardTorque;  /**< 转矩卡。 */
    /* 区 3：给定行。 */
    XLabel       m_setLabel;    /**< 「频率给定」。 */
    XSlider      m_setSlider;   /**< 0..500（0.1Hz 定点）。 */
    XSpinBox     m_setSpin;     /**< 0..500（0.1Hz 定点）。 */
    XPushButton  m_setApply;    /**< 「下发」。 */
    /* 区 4：操作行（方向下拉 + 启停单钮双态 + 点动/复位条件使能）。 */
    XComboBox    m_dirCombo;    /**< 运行方向（正转/反转；启动时生效）。 */
    XPushButton  m_btnRun;      /**< 启动/停止（随运行态换字）。 */
    XPushButton  m_btnJog;      /**< 点动（运行中禁用）。 */
    XPushButton  m_btnReset;    /**< 故障复位（有故障才可点）。 */
    /* 区 5：趋势图。 */
    XChartView   m_trendView;   /**< 图表视图。 */
    XChart*      m_trendChart;  /**< 图表（create 所有权在页）。 */
    XLineSeries* m_trendSeries; /**< 频率序列。 */
    XValueAxis*  m_axisX;       /**< X 轴（时间：相对秒）。 */
    XValueAxis*  m_axisY;       /**< Y 轴（Hz；自动量程）。 */
    int64_t      m_trendStartMs;/**< 趋势计时起点（单调毫秒）。 */
    double       m_trendWinW;   /**< 时间窗宽（秒；用户缩放后随之变化）。 */
    bool         m_trendUserView;/**< 视口归用户（用户缩放/平移后页面停写 X 轴）。 */
    /* 控制闸（并入给定行；不单独占行）。 */
    XCheckBox    m_gate;        /**< 控制闸「允许界面操作」。 */
    XLabel       m_hint;        /**< 提示行（闸关闭/自动只读）。 */
    /* ---- 数据提供者 ---- */
    const HsfVfdProvider* m_provider; /**< 函数表（借用；可 NULL=全降级）。 */
    void*        m_provUser;    /**< provider userData 缓存。 */
    /* ---- 演示驱动 ---- */
    XTimer*     m_simTimer;    /**< 200ms 模拟源驱动定时器（NULL=无）。 */
    bool         m_updating;    /**< 回显抑制：刷快照时不回写控件。 */
    bool         m_autoMode;    /**< 自动运行模式（true=只读+隐藏闸 UI）。 */
    bool         m_gateRemembered; /**< 退回手动时恢复的闸值（此前状态）。 */
} HsfMonitorVfd;

/* ==================== 工具 ==================== */

/** @brief 数值卡两行文本回填（无效掩码显 "--"；%.1f 精度）。 */
static void vfdSetCardText(XLabel* card, const char* name, bool valid,
                           double value, const char* unit)
{
    char buf[80];
    char val[24];
    if (!card) return;
    if (valid) {
        snprintf(val, sizeof(val), "%.1f %s", value, unit);
    } else {
        val[0] = '-'; val[1] = '-'; val[2] = '\0';
    }
    snprintf(buf, sizeof(buf), "%s\n%s", name, val);
    XLabel_setText_2(card, buf);
}

/** @brief 字符串转义：UTF-8 中文字面（源文件统一 \xNN 转义，见仓库规约）。 */

/* ==================== 自绘控件（vtable 重载） ==================== */

/* ---- 仪表：270° 弧带（AA 径向线扇带；锯齿终修） ---- */

/**
 * @brief 扇环（弧带）绘制：内/外半径 + 起止角 → 逐角画**径向线**。
 * @details [2026-11-03 锯齿终修] 前代扫描线填充零空洞但边缘是硬边
 *          （用户放大看锯齿；Qt QPainter::Antialiasing 对标）。本框架
 *          AA 能力齐备（XPainterRenderHint_Antialiasing + 斜线浮点
 *          覆盖率光栅化 painterRaster_drawLineAntialiased，Qt raster
 *          同源），此前未用是页面侧没开 hint。
 *          现做法：paintEvent 开 Antialiasing → 每个角度步画一根
 *          rInner→rOuter 的**斜径向线**（AA 只对斜线生效，正好全中），
 *          角度步距按外缘弦长 0.5px 加密 —— 相邻 AA 线的半像素羽化
 *          边互相融合，扇带内部饱和、内外圆缘平滑（覆盖率渐变）。
 *          代替前代硬边扫描线：均匀性由 0.5px 步距 + AA 半透明叠加
 *          保证（同色多叠不影响饱和度上限）。
 */
static void vfdDrawArcBand(XPainter* painter, int cx, int cy,
                           int rInner, int rOuter,
                           double startDeg, double sweepDeg)
{
    int i;
    int segments;
    double a0 = startDeg * 3.14159265358979323846 / 180.0;
    double sweepRad = sweepDeg * 3.14159265358979323846 / 180.0;
    double step;
    if (rOuter < 1 || sweepDeg == 0.0 || rOuter <= rInner) return;
    /* 角度步距：外缘弦长 ≈ 0.25px（相邻 AA 线羽化带充分重叠，
     * 带内覆盖率均匀无网纹；上限护栏防超大半径慢绘）。 */
    segments = (int)(2.0 * 3.14159265358979323846 * rOuter *
                     ((sweepDeg > 0 ? sweepDeg : -sweepDeg) / 360.0) * 4.0);
    if (segments < 720) segments = 720;
    if (segments > 4320) segments = 4320;
    step = sweepRad / segments;
    for (i = 0; i <= segments; ++i) {
        double a = a0 + step * i;
        double c = cos(a);
        double s = sin(a);
        /* 亚像素端点（QLineF 口径）：外缘精确落圆周，AA 覆盖率连续。 */
        XPainter_drawLine_3(painter,
                            (float)(cx + rInner * c), (float)(cy - rInner * s),
                            (float)(cx + rOuter * c), (float)(cy - rOuter * s));
    }
}

/**
 * @brief 圆缘描边：沿圆周以 ≤0.7px 弦长步进画 AA 短线段链。
 * @details [2026-11-03 锯齿根修] 扇带径向线端点整数化（(int)r·cos），
 *          内外圆缘呈 1px 阶梯——AA 只柔化线的**两侧**，管不住**端点
 *          量化**。修法＝给圆缘补一根 AA 描边：弦长小于线宽时相邻段
 *          端点落在同像素邻域，AA 覆盖率连续 → 圆缘成 1px 渐变带
 *          （Qt drawArc + Antialiasing 的观感对齐）。半径 0 时整圆。
 */
static void vfdDrawArcOutline(XPainter* painter, int cx, int cy, int r,
                              double startDeg, double sweepDeg)
{
    int i;
    int segments;
    double a0 = startDeg * 3.14159265358979323846 / 180.0;
    double sweepRad = sweepDeg * 3.14159265358979323846 / 180.0;
    double step;
    int px, py;
    if (r < 1 || sweepDeg == 0.0) return;
    segments = (int)(2.0 * 3.14159265358979323846 * r *
                     ((sweepDeg > 0 ? sweepDeg : -sweepDeg) / 360.0) * 1.5);
    if (segments < 240) segments = 240;
    if (segments > 2160) segments = 2160;
    step = sweepRad / segments;
    px = cx + (int)(r * cos(a0));
    py = cy - (int)(r * sin(a0));
    for (i = 1; i <= segments; ++i) {
        double a = a0 + step * i;
        /* 亚像素端点（QLineF 口径）：AA 覆盖率沿圆缘连续渐变。 */
        float nx = (float)(cx + r * cos(a));
        float ny = (float)(cy - r * sin(a));
        XPainter_drawLine_3(painter, (float)px, (float)py, nx, ny);
        px = (int)nx;
        py = (int)ny;
    }
}

/**
 * @brief 仪表绘制（三个 gauge 共用；只画表盘几何，文字归叠放 XLabel）。
 * @details 表盘：底盘（扫描线填充）+ 270° 底轨带 + 进度带（故障转红）。
 *          角度：数学坐标系，起点 135°（左下），顺时针经顶部扫 270°
 *          到 -45°（右下）——sweep 为负即顺时针；开口朝正下方。
 */
static void vfdGaugePaint(XWidget* self, XEvent* event, double value, double maxV,
                          bool faultColor)
{
    XImage* image;
    XPoint offset;
    XPainter painter;
    int w = XWidget_width(self);
    int h = XWidget_height(self);
    int d = (w < h ? w : h) - 36;
    int cx = w / 2;
    int cy = h / 2 + 6;
    int r = d / 2;
    int bandOuter = (r > 6) ? r - 4 : r;
    int bandInner = (bandOuter > 10) ? bandOuter - 9 : bandOuter / 2;
    double frac = (maxV > 0.0) ? (value / maxV) : 0.0;
    uint32_t arcColor = faultColor ? VFD_COLOR_FAULT : VFD_COLOR_GAUGE;

    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;

    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    /* 抗锯齿（Qt QPainter::Antialiasing 对标）：斜径向线走框架浮点
     * 覆盖率光栅化，扇带内外圆缘平滑（用户放大看锯齿的终修）。 */
    XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);

    /* 底盘：AA 斜线径向扇（整圆）；轴对齐扫描线硬边同弃——圆缘要平滑。 */
    XPainter_setPen(&painter, 0xFFF7F9FB);
    vfdDrawArcBand(&painter, cx, cy, 0, r, 0.0, 360.0);
    /* 底盘圆缘 AA 描边（同色；羽化圆缘，消除扫描/径向端点阶梯）。 */
    vfdDrawArcOutline(&painter, cx, cy, r, 0.0, 360.0);
    /* 底轨带（270°：135° 起、顺时针 270°）+ 内外圆缘描边。 */
    XPainter_setPenWidth(&painter, 1);
    XPainter_setPen(&painter, VFD_COLOR_TRACK);
    vfdDrawArcBand(&painter, cx, cy, bandInner, bandOuter, 135.0, -270.0);
    vfdDrawArcOutline(&painter, cx, cy, bandOuter, 135.0, -270.0);
    vfdDrawArcOutline(&painter, cx, cy, bandInner, 135.0, -270.0);
    /* 进度带（主题色/故障红；从起点起 frac 比例，略厚盖在底轨上）。 */
    if (frac > 0.003) {
        int progInner = (bandInner > 0) ? bandInner - 1 : bandInner;
        XPainter_setPen(&painter, arcColor);
        vfdDrawArcBand(&painter, cx, cy, progInner, bandOuter + 1,
                       135.0, -270.0 * frac);
        vfdDrawArcOutline(&painter, cx, cy, bandOuter + 1, 135.0, -270.0 * frac);
        vfdDrawArcOutline(&painter, cx, cy, progInner, 135.0, -270.0 * frac);
    }

    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 三仪表 paintEvent 分派（owner+slot 正经分派；取实时值）。 */
static void VHsfVfdGauge_paintEvent(XWidget* self, XEvent* event)
{
    HsfVfdGaugeWidget* gauge;
    HsfMonitorVfd* page;
    const XVfdStatus* st;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    gauge = (HsfVfdGaugeWidget*)self;
    page = gauge->m_owner;
    if (!page) return;
    st = XVfdDevice_status_const(page->m_provider
                                 ? page->m_provider->device(page->m_provUser)
                                 : NULL);
    switch (gauge->m_slot) {
    case 0:     /* 输出频率（故障时弧转红）。 */
        vfdGaugePaint(self, event,
                      st ? st->m_outputFreqHz : 0.0, 60.0,
                      st ? st->m_faulted : false);
        break;
    case 1:     /* 输出电流。 */
        vfdGaugePaint(self, event,
                      st ? st->m_outputCurrentA : 0.0, 60.0, false);
        break;
    default:    /* 输出电压。 */
        vfdGaugePaint(self, event,
                      st ? st->m_outputVoltageV : 0.0, 400.0, false);
        break;
    }
}

/* ---- 状态横幅类（XWidget 直派生；只重载 paintEvent） ---- */

XCLASS_DEFINE_BEGING(HsfVfdBannerWidget)
XCLASS_DEFINE_EXTEND_END(HsfVfdBannerWidget, XWidget)

/**
 * @brief 状态横幅画布（深底胶囊自绘）。
 */
typedef struct HsfVfdBannerWidget {
    XWidget m_base;                     /**< 基类子对象（第一成员）。 */
} HsfVfdBannerWidget;

XVtable* HsfVfdBanner_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfVfdBannerWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfVfdBanner_paintEvent);
    XCLASS_SHOW_SIZE_DEFAULT(HsfVfdBannerWidget);
    return XVTABLE_DEFAULT;
}

/* ---- 状态横幅 ---- */

/** @brief 横幅绘制：状态色胶囊 + 状态文本 + 右侧故障信息。 */
static void VHsfVfdBanner_paintEvent(XWidget* self, XEvent* event)
{
    HsfMonitorVfd* page;
    const XVfdStatus* st;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    XFont bannerFont;                   /**< 控件主题字体（测量/画字同源）。 */
    uint32_t color = VFD_COLOR_STOP;
    uint32_t faultColor = 0xFF9FB4C7u;
    const char* stateText = "";
    char faultBuf[64];
    int w, h;

    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    page = (HsfMonitorVfd*)((char*)self - offsetof(HsfMonitorVfd, m_banner));
    st = XVfdDevice_status_const(page->m_provider
                                 ? page->m_provider->device(page->m_provUser)
                                 : NULL);

    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    bannerFont = XWidget_font(self);
    XPainter_setFont(&painter, &bannerFont);
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    w = XWidget_width(self);
    h = XWidget_height(self);

    /* 底：圆角深色横幅。 */
    XRect_init(&rect, 0, 0, w, h);
    XPainter_setPenWidth(&painter, 1);
    XPainter_setPen(&painter, 0xFFD8DEE4);
    XPainter_setBrush(&painter, 0xFF24313F);
    XPainter_drawRoundedRect(&painter, &rect, 10, 10);

    /* 左：状态胶囊（圆点 + 状态字）。 */
    if (st && st->m_valid) {
        if (st->m_faulted) { color = VFD_COLOR_FAULT; stateText = "\xE6\x95\x85\xE9\x9A\x9C"; }
        else if (st->m_warning) { color = VFD_COLOR_WARN; stateText = "\xE8\xAD\xA6\xE5\x91\x8A"; }
        else vfdRunStateStyle(st->m_runState, &color, &stateText);
    } else {
        stateText = "\xE6\x97\xA0\xE6\x95\xB0\xE6\x8D\xAE";  /* 无数据 */
        color = VFD_COLOR_STOP;
    }
    XPainter_setBrush(&painter, color);
    XPainter_setPen(&painter, color);
    {
        XRect dot;
        dot.x = 20; dot.y = h / 2 - 8; dot.width = 16; dot.height = 16;
        XPainter_drawEllipse(&painter, &dot);
    }
    XPainter_drawText(&painter, 48, h / 2 + 8, stateText, 0xFFFFFFFFu);

    /* 右：故障码 + 品牌文本（按自身文本宽右对齐——固定偏移在窄幅下与
     * 左侧状态字相撞，2026-11-03 600x900 实锅；窄幅放不下整体让位）。 */
    faultBuf[0] = '\0';
    if (st && st->m_faulted && st->m_faultCode != 0) {
        const char* name = NULL;
        if (page->m_provider && page->m_provider->faultText)
            name = page->m_provider->faultText(page->m_provUser, st->m_faultCode);
        if (name)
            snprintf(faultBuf, sizeof(faultBuf), "%s (0x%04X)", name, st->m_faultCode);
        else
            snprintf(faultBuf, sizeof(faultBuf), "0x%04X", st->m_faultCode);
        faultColor = 0xFFFFC7C9u;
    } else if (page->m_provider && !page->m_provider->controlEnabled(page->m_provUser)) {
        snprintf(faultBuf, sizeof(faultBuf),
                 "\xE7\x95\x8C\xE9\x9D\xA2\xE6\x93\x8D\xE4\xBD\x9C\xE5\xB7\xB2\xE9\x94\x81"); /* 界面操作已锁 */
        faultColor = 0xFFFFD88Au;
    } else {
        snprintf(faultBuf, sizeof(faultBuf),
                 "\xE9\x80\x9A\xE8\xAE\xAF\xE7\x9B\x91\xE8\xA7\x86\xE4\xB8\xAD"); /* 通讯监视中 */
        faultColor = 0xFF9FB4C7u;
    }
    if (faultBuf[0] != '\0') {
        /* [2026-11-03 实锅] 测量必须用控件自身主题字体（painter 默认
         * NULL 字体在 outline 链下 pixelSize 退化为 unitsPerEm → 宽度
         * 膨胀数百 px → 防叠印守卫把提示整体吞掉——1000x640 横幅右侧
         * 空白即此）。画字路径已 setFont(控件字体)，测量与之一致。 */
        int stateW = 48 + XPainter_textWidth(&bannerFont, stateText);
        int fx = w - 20 - XPainter_textWidth(&bannerFont, faultBuf);
        /* 与左侧状态字相撞（窄幅）→ 放不下就不画（防叠印）。 */
        if (fx > stateW + 12)
            XPainter_drawText(&painter, fx, h / 2 + 8, faultBuf, faultColor);
    }

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XClassDeinit(&bannerFont);          /* XWidget_font 契约：用后释放。 */
}

/* ==================== 槽 ==================== */

/** @brief provider 快照通知 → 全页刷新（含重绘自绘区、趋势追点）。 */
static void vfdPageNotify(const HsfVfdProvider* provider, void* pageToken);

/** @brief 闸/能力 → 按钮与给定行启用态（槽与刷新共用；先声明）。 */
static void vfdApplyControlState(HsfMonitorVfd* self);
static void vfdRefresh(HsfMonitorVfd* self);

/** @brief 操作按钮点击：按地址分派到 provider 动作。
 *  @note  m_btnRun 双态：未运行=「启动」（按方向下拉下发 start），
 *         运行中=「停止」（下发 stop）——按钮文字由 vfdRefresh 随
 *         运行态翻转，点击行为同样按运行态分派。 */
static void vfdBtnSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)receiver;
    const HsfVfdProvider* p;
    const XVfdDevice* dev;
    bool running;
    (void)args;
    if (!self || !self->m_provider) return;
    p = self->m_provider;
    if (receiver == (XObject*)&self->m_btnRun) {
        dev = p->device(self->m_provUser);
        running = dev && XVfdDevice_status_const(dev) && XVfdDevice_status_const(dev)->m_running;
        if (running) p->stop(self->m_provUser);
        else p->start(self->m_provUser,
                      XComboBox_currentIndex(&self->m_dirCombo) == 1);
    }
    else if (receiver == (XObject*)&self->m_btnJog)    p->jog(self->m_provUser, false);
    else if (receiver == (XObject*)&self->m_btnReset)  p->resetFault(self->m_provUser);
}

/** @brief 下发按钮：频率给定（0.1Hz 定点 → Hz）。 */
static void vfdApplySlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)receiver;
    (void)args;
    if (!self || !self->m_provider || !self->m_provider->setFrequency) return;
    self->m_provider->setFrequency(self->m_provUser,
                                   (double)XSpinBox_value(&self->m_setSpin) / 10.0);
}

/** @brief 控制闸切换：透传 provider；联动按钮可用态。
 *  @note  自动运行模式下勾选框已隐藏，此槽不应触达（双保险：触达则
 *         静默回滚勾选态，不透传 provider）。 */
static void vfdGateSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)receiver;
    bool checked = false;
    (void)args;
    if (!self || !self->m_provider) return;
    if (self->m_autoMode) {
        XCheckBox_setChecked(&self->m_gate, false);   /* 自动态恒锁。 */
        return;
    }
    checked = XCheckBox_isChecked(&self->m_gate);
    self->m_gateRemembered = checked;                 /* 记忆用户意图。 */
    if (self->m_provider->setControlEnabled)
        self->m_provider->setControlEnabled(self->m_provUser, checked);
    vfdApplyControlState(self);
    XWidget_update(&self->m_banner);        /* 横幅右侧提示联动。 */
}

/** @brief Slider 拖动 → 同步 SpinBox（receiver=slider 自身；分派照
 *         HsfHomePage 静态转发口径——connect_1 首参是**注册的 receiver**
 *         不是 sender，2026-11-03 拖动被回滚实锅：旧代码以 self 为
 *         receiver 又按「receiver==slider」分派，恒走 else 分支把
 *         slider 弹回 SpinBox 旧值）。 */
static void vfdSliderChanged(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_setSlider));
    int v;
    (void)args;
    if (!self || self->m_updating) return;
    v = XAbstractSlider_value((XAbstractSlider*)&self->m_setSlider);
    self->m_updating = true;
    XSpinBox_setValue(&self->m_setSpin, v);
    self->m_updating = false;
}

/** @brief SpinBox 编辑 → 同步 Slider（receiver=spinbox 自身）。 */
static void vfdSpinChanged(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_setSpin));
    int v;
    (void)args;
    if (!self || self->m_updating) return;
    v = XSpinBox_value(&self->m_setSpin);
    self->m_updating = true;
    XAbstractSlider_setValue((XAbstractSlider*)&self->m_setSlider, v);
    self->m_updating = false;
}

/**
 * @brief 跟随写窗：右缘贴最新追点，左缘＝右缘−窗宽（钳 0 不空滚）。
 * @param self  页对象。
 * @param xNow  当前追点 x（相对秒）。
 */
static void vfdTrendFollowWindow(HsfMonitorVfd* self, double xNow)
{
    double winW;
    double lo;
    double hi;
    if (!self || !self->m_axisX) return;
    winW = self->m_trendWinW;
    if (winW < VFD_TREND_WIN_MIN) winW = VFD_TREND_WIN_MIN;
    if (winW > VFD_TREND_WIN_MAX) winW = VFD_TREND_WIN_MAX;
    self->m_trendWinW = winW;
    lo = xNow - winW;
    if (lo < 0.0) lo = 0.0;
    hi = (lo + winW > xNow) ? (lo + winW) : xNow;
    XValueAxis_setRange(self->m_axisX, lo, hi);
}

/** @brief 趋势图视口变更（用户缩放/平移/复位）→ 视口归属仲裁。
 *  @note  载荷＝XChartView_ViewportChange（缩放/平移/复位）：
 *         - 平移：视口归用户（页面停写 X 轴，拖到哪停在哪）；
 *         - 缩放：窗宽取图表当前 X 域宽（用户选的时间分辨率），位置仍跟随；
 *         - 复位：窗宽回默认并把视口交还页面（恢复自动跟随）。 */
static void vfdTrendViewportSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)receiver;
    int reason = XChartView_ViewportChange_Zoom;
    if (!self) return;
    if (args) {
        int r = XChartView_ViewportChange_Zoom;
        XVarList_start(args);
        r = XVarList_arg(args, int);
        reason = r;
    }
    switch (reason) {
    case XChartView_ViewportChange_Pan:
        self->m_trendUserView = true;   /* 视口归用户：页面停写 X 轴。 */
        break;
    case XChartView_ViewportChange_Reset:
        self->m_trendUserView = false;  /* 复位：交还页面，恢复跟随。 */
        self->m_trendWinW = VFD_TREND_WIN_DEF;
        break;
    case XChartView_ViewportChange_Zoom:
    default: {
        /* 缩放：窗宽＝图表当前 X 域宽（光标锚点缩放后的宽度）。 */
        double w = 0.0;
        double xNow;
        if (self->m_axisX)
            w = self->m_axisX->m_base.m_max - self->m_axisX->m_base.m_min;
        if (w > 0.0) self->m_trendWinW = w;
        if (!self->m_trendUserView) {
            /* 跟随态：按新窗宽立即重写窗（右缘贴追点）——光标锚点的
               中间态在同一帧内被覆盖，用户只看到时间轴刻度变密，无
               「缩放后窗口跳到右缘」的突兀感。 */
            xNow = (double)(XDateTime_currentMSecsSinceEpoch() -
                            self->m_trendStartMs) / 1000.0;
            vfdTrendFollowWindow(self, xNow);
        }
        break;
    }
    }
}

/* ==================== 刷新 ==================== */

/* ==================== 交互模式（自动运行锁）状态 ==================== */
/* 对齐 HsfMonitorDigital_setInteractiveMode 同款契约（API 定义在文件尾）：
 *   setInteractiveMode(false)  进自动运行：隐藏勾选框 + 强制只读（API 也锁）
 *   setInteractiveMode(true)   退回手动：重显勾选框，闸值回到此前状态
 * 单页单实例；create 前调用只记意图，create 时按 s_pendingAutoMode 初始化。 */
static HsfMonitorVfd* s_vfdPage = NULL;        /**< 活动页（create 登记）。 */
static bool s_pendingAutoMode = false;         /**< 建页前的模式意图。 */

/** @brief 闸/能力 → 按钮与给定行的启用态。
 *  @note  自动运行模式（m_autoMode）下闸强制视为关：全部操作控件禁用、
 *         提示行显示「自动运行中」，勾选框已隐藏（见 setInteractiveMode）。 */
static void vfdApplyControlState(HsfMonitorVfd* self)
{
    const XVfdDevice* dev = self->m_provider ? self->m_provider->device(self->m_provUser) : NULL;
    uint32_t caps = dev ? XVfdDevice_caps(dev) : 0;
    bool gate = !self->m_autoMode && self->m_provider && self->m_provider->controlEnabled
                ? self->m_provider->controlEnabled(self->m_provUser) : false;

    {
        const XVfdDevice* dev2 = self->m_provider ? self->m_provider->device(self->m_provUser) : NULL;
        const XVfdStatus* st2 = XVfdDevice_status_const(dev2);
        bool running = st2 && st2->m_valid && st2->m_running;
        bool faulted = st2 && st2->m_valid && st2->m_faulted;
        bool hasJog = (caps & XVfdCap_Jog) != 0;

        /* 启动/停止单钮双态：恒可点（闸开时），动作按运行态分派。 */
        XWidget_setEnabled((XWidget*)&self->m_btnRun, gate);
        XAbstractButton_setText_2((XAbstractButton*)&self->m_btnRun,
            running ? "\xE5\x81\x9C\xE6\xAD\xA2"     /* 停止 */
                    : "\xE5\x90\xAF\xE5\x8A\xA8");   /* 启动 */
        /* 方向下拉：可随时预选（方向随**本次启动**下发，运行中改选
           不影响正在运行的电机；下一次启动生效）。此前运行中整控件
           置灰，点击无弹窗——用户反馈「正转/反转单击无切换弹窗」：
           Qt 口径下选择器只读态应**看得见**（灰显），而这里是既要
           预选又要可点，故改为不置灰、语义只在下次启动生效。 */
        XWidget_setEnabled((XWidget*)&self->m_dirCombo, gate);
        /* 点动：无能力（如 ATV61 无通讯点动）直接隐藏（Qt 风格：无
         * 功能不展示）；有能力则非运行态可点（运行中点动冲突）。 */
        if (!hasJog)
            XWidget_hide((XWidget*)&self->m_btnJog);
        else {
            XWidget_show((XWidget*)&self->m_btnJog);
            XWidget_setEnabled((XWidget*)&self->m_btnJog, gate && !running);
        }
        /* 故障复位：仅有故障时可点。 */
        XWidget_setEnabled((XWidget*)&self->m_btnReset, gate && faulted);
    }
    XWidget_setEnabled((XWidget*)&self->m_setApply,  gate);
    XWidget_setEnabled((XWidget*)&self->m_setSlider, gate);
    XWidget_setEnabled((XWidget*)&self->m_setSpin,   gate);
    if (self->m_autoMode) {
        XLabel_setText_2(&self->m_hint,
            "\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD\xEF\xBC\x9A"
            "\xE7\x95\x8C\xE9\x9D\xA2\xE5\x8F\xAA\xE8\xAF\xBB"); /* 自动运行中：界面只读 */
    } else {
        XLabel_setText_2(&self->m_hint,
            gate ? ""
                 : "\xE7\x95\x8C\xE9\x9D\xA2\xE6\x93\x8D\xE4\xBD\x9C\xE5\xB7\xB2\xE9\x94\x81\xEF\xBC\x8C"
                   "\xE5\x8B\xBE\xE9\x80\x89\xE9\xA2\x91\xE7\x8E\x87\xE7\xBB\x99\xE5\xAE\x9A\xE8\xA1\x8C\xE5\xBC\x80\xE5\x85\xB3\xE8\xA7\xA3\xE9\x94\x81"); /* 界面操作已锁，勾选频率给定行开关解锁 */
    }
}

/** @brief 快照 → 全页控件回填（标签/卡/仪表重绘/趋势追点）。 */
static void vfdRefresh(HsfMonitorVfd* self)
{
    const XVfdDevice* dev;
    const XVfdStatus* st;
    if (!self) return;
    dev = self->m_provider ? self->m_provider->device(self->m_provUser) : NULL;
    st = XVfdDevice_status_const(dev);

    /* 数值卡（validMask 显 "--"）。 */
    vfdSetCardText(&self->m_cardPower,
                   "\xE8\xBE\x93\xE5\x87\xBA\xE5\x8A\x9F\xE7\x8E\x87", /* 输出功率 */
                   st && (st->m_validMask & XVfdField_OutputPowerKw),
                   st ? st->m_outputPowerKw : 0.0, "kW");
    vfdSetCardText(&self->m_cardBus,
                   "\xE6\xAF\x8D\xE7\xBA\xBF\xE7\x94\xB5\xE5\x8E\x8B", /* 母线电压 */
                   st && (st->m_validMask & XVfdField_BusVoltage),
                   st ? st->m_busVoltageV : 0.0, "V");
    vfdSetCardText(&self->m_cardRpm,
                   "\xE7\x94\xB5\xE6\x9C\xBA\xE8\xBD\xAC\xE9\x80\x9F", /* 电机转速 */
                   st && (st->m_validMask & XVfdField_SpeedRpm),
                   st ? st->m_speedRpm : 0.0, "rpm");
    vfdSetCardText(&self->m_cardTorque,
                   "\xE8\xBE\x93\xE5\x87\xBA\xE8\xBD\xAC\xE7\x9F\xA9", /* 输出转矩 */
                   st && (st->m_validMask & XVfdField_TorquePercent),
                   st ? st->m_torquePercent : 0.0, "%");

    /* 仪表数值标签回填（文字=框架托管 XLabel；画布只负责弧线）。 */
    {
        char buf[24];
        bool freqValid = st && (st->m_validMask & XVfdField_OutputFreqHz);
        bool curValid = st && (st->m_validMask & XVfdField_OutputCurrent);
        bool voltValid = st && (st->m_validMask & XVfdField_OutputVoltage);
        snprintf(buf, sizeof(buf), freqValid ? "%.1f" : "--",
                 freqValid ? st->m_outputFreqHz : 0.0);
        XLabel_setText_2(&self->m_gaugeFreq.m_value, buf);
        snprintf(buf, sizeof(buf), curValid ? "%.1f" : "--",
                 curValid ? st->m_outputCurrentA : 0.0);
        XLabel_setText_2(&self->m_gaugeCur.m_value, buf);
        snprintf(buf, sizeof(buf), voltValid ? "%.1f" : "--",
                 voltValid ? st->m_outputVoltageV : 0.0);
        XLabel_setText_2(&self->m_gaugeVolt.m_value, buf);
    }

    /* 自绘区重绘。 */
    XWidget_update(&self->m_banner);
    XWidget_update(&self->m_gaugeFreq.m_canvas.m_base);
    XWidget_update(&self->m_gaugeCur.m_canvas.m_base);
    XWidget_update(&self->m_gaugeVolt.m_canvas.m_base);

    /* 趋势追点（输出频率；X=相对秒，60s 滚动窗；缩放/平移不打断追点）。 */
    if (self->m_trendSeries && st && (st->m_validMask & XVfdField_OutputFreqHz)) {
        double xNow = (double)(XDateTime_currentMSecsSinceEpoch() -
                               self->m_trendStartMs) / 1000.0;
        double minV = st->m_outputFreqHz;
        double maxV = st->m_outputFreqHz;
        double winL = self->m_axisX->m_base.m_min;   /* 当前 X 窗左缘。 */
        static int s_traceCnt = 0;
        XXYSeries_append((XXYSeries*)self->m_trendSeries, xNow,
                         st->m_outputFreqHz);
        if (s_traceCnt++ % 10 == 0)
            XPrintf("[vfd-trend] pts=%d xNow=%.1f winL=%.1f\n",
                    (int)XXYSeries_count((XXYSeries*)self->m_trendSeries),
                    xNow, winL);
        /* 按点数近似裁剪：点距 0.2s，保留最近 450 点（90s 数据余量）。 */
        {
            static const int kKeep = 450;
            int count = XXYSeries_count((XXYSeries*)self->m_trendSeries);
            if (count > kKeep)
                XXYSeries_removePoints((XXYSeries*)self->m_trendSeries,
                                       0, count - kKeep);
        }
        /* 视口仲裁（用户交互 vs 自动跟随最新数据）：
         * - 默认跟随：右缘贴最新追点，窗宽＝m_trendWinW（用户选的分辨率）；
         * - 用户平移（viewportChanged/Pan）→ 视口归用户：页面**停写 X 轴**，
         *   拖到哪停在哪（放大后可左右拖拽看历史）；
         * - 用户缩放（viewportChanged/Zoom）→ 窗宽取图表当前 X 域宽，
         *   位置仍跟随（趋势图跟最新数据的常规口径）；
         * - 用户把窗右缘推回实时点（容差 VFD_TREND_CATCHUP）→ 恢复跟随；
         * - 复位（viewportChanged/Reset）→ 窗宽回默认并恢复跟随。 */
        {
            double winR = self->m_axisX->m_base.m_max;
            if (self->m_trendUserView && winR >= xNow - VFD_TREND_CATCHUP) {
                /* 视口右缘已追到实时点 → 交还页面自动跟随。 */
                self->m_trendUserView = false;
            }
            if (!self->m_trendUserView) {
                /* 跟随：右缘贴最新追点；左缘钳 0（数据起点前不空滚），
                 * 钳位时右缘 = max(xNow, lo+winW) 保窗宽不塌。 */
                vfdTrendFollowWindow(self, xNow);
                winL = self->m_axisX->m_base.m_min;
                winR = self->m_axisX->m_base.m_max;
            }
        }
        /* 纵轴自动量程：扫窗内可见点 min/max（下限 0 起步，上限留 15%
         * 余量；数据全零给最小跨度兜底）。时间轴模式保证 Y 永不被
         * 滚轮/捏合缩放，故这里每快照重算即可。 */
        {
            int count = XXYSeries_count((XXYSeries*)self->m_trendSeries);
            int i;
            for (i = count - 1; i >= 0; --i) {
                const XPointF* pt = XXYSeries_at((XXYSeries*)self->m_trendSeries, i);
                if (!pt) break;
                if ((double)pt->x < winL) break;   /* 更早的更小，提前收。 */
                if (pt->y < minV) minV = pt->y;
                if (pt->y > maxV) maxV = pt->y;
            }
            if (maxV - minV < 1.0) maxV = minV + 1.0;    /* 最小跨度兜底。 */
            XValueAxis_setRange(self->m_axisY, 0.0, maxV * 1.15);
        }
        /* 模型变更通知：追加/裁点/改轴后让视图失效重绘（Qt Charts
         * 数据模型变更口径；此前不通知——旧版靠偶发布局重绘波及出线，
         * 时间轴改版后无人刷新即空图，2026-11-03 实锅）。 */
        XChartView_updateChart(&self->m_trendView);
    }

    /* 设定回显（抑制回环；快照侧只同步 SpinBox，用户编辑不被覆盖——
       m_updating 只在程序侧 setValue 时置位）。 */
    if (st && (st->m_validMask & XVfdField_SetFreqHz) && !self->m_updating) {
        int v = (int)(st->m_setFreqHz * 10.0 + 0.5);
        if (v >= 0 && v <= VFD_FREQ_MAX10) {
            self->m_updating = true;
            XAbstractSlider_setValue((XAbstractSlider*)&self->m_setSlider, v);
            XSpinBox_setValue(&self->m_setSpin, v);
            self->m_updating = false;
        }
    }

    vfdApplyControlState(self);
}

/** @brief provider 通知 → vfdRefresh。 */
static void vfdPageNotify(const HsfVfdProvider* provider, void* pageToken)
{
    (void)provider;
    vfdRefresh((HsfMonitorVfd*)pageToken);
}

/** @brief 模拟源驱动拍（200ms）：推进剧情 → 设备解码 → 信号桥通知页刷新。 */
static void vfdSimTimerCb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
#if XVFD_ON
    /* 模拟源单例推进（页面创建即订阅；同刻只有一页注册回调）。 */
    hsfVfdSimTick(NULL);
#endif
}

/* ==================== 组装 ==================== */

/**
 * @brief 初始化一个仪表组（自绘画布 + 三枚叠放标签）。
 * @param g 仪表组；@param parent 页容器。
 * @note  标签是**画布的子控件**（框架托管几何/裁剪）；值文本由
 *        vfdRefresh 按快照回填。画布重绘即叠面刷新。
 */
static void vfdInitGauge(HsfVfdGauge* g, XWidget* parent, const char* title,
                         const char* unit, HsfMonitorVfd* owner, int slot)
{
    memset(g, 0, sizeof(*g));
    XWidget_init(&g->m_canvas.m_base, parent, 0);
    XClassGetVtable(&g->m_canvas) = HsfVfdGauge_class_init();
    g->m_canvas.m_owner = owner;        /* paintEvent 分派依据。 */
    g->m_canvas.m_slot = slot;
    HsfTheme_applyDefaultFont(&g->m_canvas.m_base);
    XWidget_setFixedHeight(&g->m_canvas.m_base, VFD_GAUGE_H);
    XWidget_setMinimumWidth(&g->m_canvas.m_base, 130);

    XLabel_init(&g->m_title, &g->m_canvas.m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&g->m_title);
    XLabel_setText_2(&g->m_title, title);
    XLabel_setTextPixelSize(&g->m_title, 13);
    XLabel_setAlignment(&g->m_title, XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setForegroundRole((XWidget*)&g->m_title, XPaletteColorRole_Mid);

    XLabel_init(&g->m_value, &g->m_canvas.m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&g->m_value);
    XLabel_setText_2(&g->m_value, "--");
    {
        XFont f = XWidget_font((XWidget*)&g->m_value);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&g->m_value, &f);
        XClassDeinit(&f);
    }
    XLabel_setTextPixelSize(&g->m_value, 18);
    XLabel_setAlignment(&g->m_value, XAlignment_HCenter | XAlignment_VCenter);

    XLabel_init(&g->m_unit, &g->m_canvas.m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&g->m_unit);
    XLabel_setText_2(&g->m_unit, unit);
    XLabel_setTextPixelSize(&g->m_unit, 12);
    XLabel_setAlignment(&g->m_unit, XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setForegroundRole((XWidget*)&g->m_unit, XPaletteColorRole_Mid);

    vfdGaugeRelayout(g);
    XWidget_show((XWidget*)&g->m_title);
    XWidget_show((XWidget*)&g->m_value);
    XWidget_show((XWidget*)&g->m_unit);
    XWidget_show(&g->m_canvas.m_base);
}

/** @brief 数值卡标签初始化（两行：名称弱色值亮——单标签多行居中）。 */
static void vfdInitCard(XLabel* card, XWidget* parent)
{
    XLabel_init(card, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)card);
    XLabel_setAlignment(card, XAlignment_HCenter | XAlignment_VCenter);
    XLabel_setTextPixelSize(card, 13);
    XWidget_setFixedHeight((XWidget*)card, VFD_CARD_H);
    XWidget_show((XWidget*)card);
}

/** @brief 操作按钮初始化（40dp 触控高 + 点击连接）。 */
static void vfdInitBtn(XPushButton* btn, XWidget* parent, const char* text)
{
    XPushButton_init(btn, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)btn);
    XPushButton_setText_2(btn, text);
    XWidget_setFixedHeight((XWidget*)btn, 40);
    XObject_connect_1((XObject*)btn, XSignal(XAbstractButton_clicked_signal),
                      (XObject*)btn, vfdBtnSlot, XConnectionType_Direct);
    XWidget_show((XWidget*)btn);
}

XWidget* HsfMonitorVfd_create(XWidget* parent, HsfMenuNavFn navFn, void* userData)
{
    HsfMonitorVfd* self;
    (void)navFn; (void)userData;    /* 监视页无返回钮（叶子监视页口径）。 */

    if (!parent) return NULL;
    self = (HsfMonitorVfd*)XMemory_malloc(sizeof(HsfMonitorVfd),
                                          XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(HsfMonitorVfd));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    HsfTheme_applyDefaultFont(&self->m_base);

    /* ---- provider 接入（模拟源；后端换表点） ---- */
    self->m_provider = HsfVfdProvider_demo();
    self->m_provUser = self->m_provider ? self->m_provider->m_userData : NULL;
    if (self->m_provider && self->m_provider->subscribe)
        self->m_provider->subscribe(self->m_provUser, vfdPageNotify, self);

    /* ---- 区 1：状态横幅（自绘） ---- */
    memset(&self->m_banner, 0, sizeof(self->m_banner));
    XWidget_init(&self->m_banner, &self->m_base, 0);
    XClassGetVtable(&self->m_banner) = HsfVfdBanner_class_init();
    HsfTheme_applyDefaultFont(&self->m_banner);
    XWidget_setFixedHeight(&self->m_banner, VFD_BANNER_H);
    XWidget_show(&self->m_banner);

    /* ---- 区 2：仪表行（3 gauge + 2×2 卡片） ---- */
    vfdInitGauge(&self->m_gaugeFreq, &self->m_base,
                 "\xE8\xBE\x93\xE5\x87\xBA\xE9\xA2\x91\xE7\x8E\x87", "Hz", /* 输出频率 */
                 self, 0);
    vfdInitGauge(&self->m_gaugeCur, &self->m_base,
                 "\xE8\xBE\x93\xE5\x87\xBA\xE7\x94\xB5\xE6\xB5\x81", "A", /* 输出电流 */
                 self, 1);
    vfdInitGauge(&self->m_gaugeVolt, &self->m_base,
                 "\xE8\xBE\x93\xE5\x87\xBA\xE7\x94\xB5\xE5\x8E\x8B", "V", /* 输出电压 */
                 self, 2);
    {
        /* 卡片宿主先行创建：卡片必须以宿主为父（父=页容器会脱管悬浮——
           2026-11-03 出图实锅：卡面画到横幅上）。 */
        XWidget* cardHost = XWidget_create(&self->m_base, 0);
        XGridLayout* grid;
        if (cardHost) {
            HsfTheme_applyDefaultFont(cardHost);
            self->m_cardHost = cardHost;
            vfdInitCard(&self->m_cardPower, cardHost);
            vfdInitCard(&self->m_cardBus, cardHost);
            vfdInitCard(&self->m_cardRpm, cardHost);
            vfdInitCard(&self->m_cardTorque, cardHost);
            grid = (XGridLayout*)XMemory_malloc(sizeof(XGridLayout),
                                                XCLASS_DEFAULT_MEMORY_TYPE);
            if (grid) {
                XGridLayout_init(grid);
                XLayout_setContentsMargins((XLayout*)grid, 0, 0, 0, 0);
                XLayout_setSpacing((XLayout*)grid, 8);
                XGridLayout_addWidget(grid, (XWidget*)&self->m_cardPower, 0, 0, 0);
                XGridLayout_addWidget(grid, (XWidget*)&self->m_cardBus,   0, 1, 0);
                XGridLayout_addWidget(grid, (XWidget*)&self->m_cardRpm,   1, 0, 0);
                XGridLayout_addWidget(grid, (XWidget*)&self->m_cardTorque, 1, 1, 0);
                XWidget_setLayout(cardHost, (XLayout*)grid);
            }
            XWidget_show(cardHost);
        }
    }

    XBoxLayout_init(&self->m_gaugeRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_gaugeRow, 12);
    XBoxLayout_addWidget(&self->m_gaugeRow, &self->m_gaugeFreq.m_canvas.m_base);
    XBoxLayout_addWidget(&self->m_gaugeRow, &self->m_gaugeCur.m_canvas.m_base);
    XBoxLayout_addWidget(&self->m_gaugeRow, &self->m_gaugeVolt.m_canvas.m_base);

    /* ---- 区 3：给定行 ---- */
    XLabel_init(&self->m_setLabel, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_setLabel);
    XLabel_setText_2(&self->m_setLabel,
                     "\xE9\xA2\x91\xE7\x8E\x87\xE7\xBB\x99\xE5\xAE\x9A"); /* 频率给定 */
    XLabel_setTextPixelSize(&self->m_setLabel, 13);
    XWidget_show((XWidget*)&self->m_setLabel);

    XSlider_init(&self->m_setSlider, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_setSlider);
    XAbstractSlider_setRange((XAbstractSlider*)&self->m_setSlider, 0, VFD_FREQ_MAX10);
    XWidget_show((XWidget*)&self->m_setSlider);

    XSpinBox_init(&self->m_setSpin, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_setSpin);
    XSpinBox_setRange(&self->m_setSpin, 0, VFD_FREQ_MAX10);
    XSpinBox_setSingleStep(&self->m_setSpin, 5);   /* 0.5Hz 步进。 */
    XSpinBox_setSuffix(&self->m_setSpin, " (0.1Hz)");
    XWidget_show((XWidget*)&self->m_setSpin);

    XPushButton_init(&self->m_setApply, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_setApply);
    XPushButton_setText_2(&self->m_setApply,
                          "\xE4\xB8\x8B\xE5\x8F\x91"); /* 下发 */
    XWidget_setFixedHeight((XWidget*)&self->m_setApply, 36);
    XObject_connect_1((XObject*)&self->m_setApply,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_setApply, vfdApplySlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_setApply);

    /* ---- 区 4：操作行（方向下拉 + 启停双态钮 + 点动/复位条件使能；
       caps 裁剪在 vfdApplyControlState 随快照刷新） ---- */
    XComboBox_init(&self->m_dirCombo, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_dirCombo);
    XComboBox_addItem_2(&self->m_dirCombo,
                        "\xE6\xAD\xA3\xE8\xBD\xAC");   /* 正转 */
    XComboBox_addItem_2(&self->m_dirCombo,
                        "\xE5\x8F\x8D\xE8\xBD\xAC");   /* 反转 */
    XComboBox_setCurrentIndex(&self->m_dirCombo, 0);
    XWidget_setFixedHeight((XWidget*)&self->m_dirCombo, 40);
    XWidget_setMinimumWidth((XWidget*)&self->m_dirCombo, 110);
    XWidget_show((XWidget*)&self->m_dirCombo);
    vfdInitBtn(&self->m_btnRun, &self->m_base,
               "\xE5\x90\xAF\xE5\x8A\xA8");             /* 启动（双态字随刷新翻） */
    vfdInitBtn(&self->m_btnJog, &self->m_base,
               "\xE7\x82\xB9\xE5\x8A\xA8");             /* 点动 */
    vfdInitBtn(&self->m_btnReset, &self->m_base,
               "\xE6\x95\x85\xE9\x9A\x9C\xE5\xA4\x8D\xE4\xBD\x8D"); /* 故障复位 */

    /* ---- 区 5：趋势图（X=时间秒，60s 窗；拖拽平移 + 滚轮/捏合缩 X；
       纵轴自动量程随快照重算） ---- */
    XChartView_init(&self->m_trendView, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_trendView);
    self->m_trendStartMs = XDateTime_currentMSecsSinceEpoch();
    self->m_trendWinW = VFD_TREND_WIN_DEF;   /* 默认 30s 时间窗。 */
    self->m_trendUserView = false;            /* 默认跟随最新数据。 */
    self->m_trendChart = XChart_create();
    self->m_trendSeries = XLineSeries_create();
    if (self->m_trendChart && self->m_trendSeries) {
        /* Qt 无 setTitleVisible；隐藏标题 = 标题文本置空（对标 QChart::setTitle("")）。 */
        XChart_setTitle_2(self->m_trendChart, "");
        XAbstractSeries_setName_2((XAbstractSeries*)self->m_trendSeries,
                                  "\xE8\xBE\x93\xE5\x87\xBA\xE9\xA2\x91\xE7\x8E\x87"); /* 输出频率 */
        XChart_addSeries(self->m_trendChart, self->m_trendSeries,
                         XChartSeriesType_Line);
        XChart_createDefaultAxes(self->m_trendChart);
        self->m_axisX = (XValueAxis*)XChart_axisX(self->m_trendChart);
        self->m_axisY = (XValueAxis*)XChart_axisY(self->m_trendChart);
        if (self->m_axisX) {
            XValueAxis_setRange(self->m_axisX, 0.0, 30.0);
            /* 轴标签经 XSnprintf(fmt, double) 渲染：须用 %f 族（%d 读
             * double 是 UB，恒出 0——2026-11-03 出图实锅）。 */
            XValueAxis_setLabelFormat_2(self->m_axisX, "%.0f s");
        }
        if (self->m_axisY) XValueAxis_setRange(self->m_axisY, 0.0, 60.0);
        XChartView_setChart(&self->m_trendView, self->m_trendChart);
    }
    /* 交互三件套（时间轴口径）：拖拽/单指拖＝左右平移横轴看历史
       + 滚轮/双指捏合＝缩放横轴（纵轴自动量程不被破）
       + 触屏手势（移动端；框架手势链）。
       视口变更信号接页面仲裁（用户平移→视口归用户；复位→交还跟随）。 */
    XChartView_setPanEnabled(&self->m_trendView, true);
    XChartView_setWheelZoomXOnly(&self->m_trendView, true);
    XChartView_setTouchGesturesEnabled(&self->m_trendView, true);
    XChartView_setTimeAxisMode(&self->m_trendView, true);
    XObject_connect_1((XObject*)&self->m_trendView,
                      XSignal(XChartView_viewportChanged_signal),
                      (XObject*)self, vfdTrendViewportSlot,
                      XConnectionType_Direct);
    XWidget_setFixedHeight((XWidget*)&self->m_trendView, VFD_TREND_H);
    XWidget_show((XWidget*)&self->m_trendView);

    /* ---- 控制闸（并入给定行；不再单独占底行） ---- */
    XCheckBox_init(&self->m_gate, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_gate);
    XCheckBox_setText_2(&self->m_gate,
                        "\xE5\x85\x81\xE8\xAE\xB8\xE7\x95\x8C\xE9\x9D\xA2\xE6\x93\x8D\xE4\xBD\x9C"); /* 允许界面操作 */
    {
        const HsfVfdProvider* p = self->m_provider;
        bool on = p && p->controlEnabled ? p->controlEnabled(self->m_provUser) : false;
        XCheckBox_setChecked(&self->m_gate, on);
    }
    XObject_connect_1((XObject*)&self->m_gate,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_gate, vfdGateSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_gate);

    XLabel_init(&self->m_hint, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_hint);
    XLabel_setTextPixelSize(&self->m_hint, 12);
    XWidget_setForegroundRole((XWidget*)&self->m_hint, XPaletteColorRole_Mid);
    XWidget_show((XWidget*)&self->m_hint);

    /* ---- 页内布局 ---- */
    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 24, 16, 24, 16);
    XLayout_setSpacing((XLayout*)&self->m_layout, 10);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);
    XBoxLayout_addWidget(&self->m_layout, &self->m_banner);
    /* 仪表行 = 3 gauge + 数值卡宿主（卡占右列；gauge 等分余宽）。 */
    if (self->m_cardHost) {
        XBoxLayout_addWidget(&self->m_gaugeRow, self->m_cardHost);
        XBoxLayout_setStretch(&self->m_gaugeRow, 0, 1);  /* gauge 频率。 */
        XBoxLayout_setStretch(&self->m_gaugeRow, 1, 1);  /* gauge 电流。 */
        XBoxLayout_setStretch(&self->m_gaugeRow, 2, 1);  /* gauge 电压。 */
        XBoxLayout_setStretch(&self->m_gaugeRow, 3, 2);  /* 卡片列更宽。 */
    }
    XBoxLayout_addLayout(&self->m_layout, (XLayout*)&self->m_gaugeRow);
    {
        /* 给定行：标签 + 闸（允许界面操作）+ 滑块 + SpinBox + 下发。 */
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 10);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_setLabel);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_gate);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_setSlider);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_setSpin);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_setApply);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        }
    }
    {
        /* 操作行：方向下拉 + 启停双态钮 + 点动 + 故障复位 + 提示右靠。 */
        XHBoxLayout* opRow = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                          XCLASS_DEFAULT_MEMORY_TYPE);
        if (opRow) {
            XBoxLayout_init(opRow, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)opRow, 10);
            XBoxLayout_addWidget(opRow, (XWidget*)&self->m_dirCombo);
            XBoxLayout_addWidget(opRow, (XWidget*)&self->m_btnRun);
            XBoxLayout_addWidget(opRow, (XWidget*)&self->m_btnJog);
            XBoxLayout_addWidget(opRow, (XWidget*)&self->m_btnReset);
            XBoxLayout_addWidget(opRow, (XWidget*)&self->m_hint);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)opRow);
        }
    }
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_trendView);

    /* ---- 模拟源驱动定时器（200ms；XTimer 独立对象，照 HsfNetworkPage 例） ---- */
    {
        XTimer* t = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (t) {
            XTimer_setInterval(t, 200);
            XTimer_setTimerCallback(t, vfdSimTimerCb);
            XTimer_setUserData(t, self);
            XTimer_start_base(t);
        }
        self->m_simTimer = t;
    }

    /* ---- Slider/SpinBox 双向同步连接（receiver=控件自身；槽内
       offsetof 反推页，与按钮静态转发同口径） ---- */
    XObject_connect_1((XObject*)&self->m_setSlider,
                      XSignal(XAbstractSlider_valueChanged_signal),
                      (XObject*)&self->m_setSlider, vfdSliderChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_setSpin,
                      XSignal(XSpinBox_valueChanged_signal),
                      (XObject*)&self->m_setSpin, vfdSpinChanged,
                      XConnectionType_Direct);

    /* ---- 交互模式登记：单页单实例；create 前的 setInteractiveMode
       意图在此生效（对齐 HsfMonitorDigital 的建页尾应用口径） ---- */
    self->m_autoMode = s_pendingAutoMode;
    self->m_gateRemembered = XCheckBox_isChecked(&self->m_gate);
    s_vfdPage = self;
    /* 环境变量直驱（演示/自测外部门；对齐数字量页 XHSF_DIGITAL_AUTO）。 */
    if (XSystem_environment("XHSF_VFD_AUTO"))
        s_pendingAutoMode = true;
    if (self->m_autoMode || s_pendingAutoMode)
        HsfMonitorVfd_setInteractiveMode(false);   /* 幂等应用：隐藏+锁 */

    XWidget_show(&self->m_base);
    return &self->m_base;
}

/** @brief 内容宽分档阈值：页根宽低于此值按窄排布（仪表/卡片纵排）。
 *  @details 全局 HSF_COMPACT_MAX_W(600) 只看窗口宽，宽屏形态下导航占
 *           176px+分隔条 → 窗口 700 时内容区只剩 ~510，横排仪表行
 *           (3×130 gauge + 2 列卡 + 间距 ≈ 800) 必然横向裁剪
 *           （2026-11-03 用户实锅：~695px 窗口右列字被裁）。各页内容
 *           需求不同，全局阈值不宜整体抬高——本页按**实际页根宽**
 *           分档（HsfNetworkPage 窄宽分档先例；帧泵每循环调，幂等廉价）。 */
#define VFD_NARROW_PAGE_W    620

/** @brief 形态自适应：按页根实际宽度分档（全局 compact 信号作或）。
 *  @details 窄档（页宽 < 620 或全局竖屏形态）：仪表纵排、卡片宿主
 *           整宽；宽档：仪表横排三等分 + 卡片占右列。 */
void HsfMonitorVfd_adapt(XWidget* page, bool compact)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)page;
    bool narrow;
    int pw;
    if (!self) return;
    pw = XWidget_width(page);
    narrow = compact || (pw > 0 && pw < VFD_NARROW_PAGE_W);
    XBoxLayout_setDirection(&self->m_gaugeRow,
                            narrow ? XBoxLayoutDirection_TopToBottom
                                   : XBoxLayoutDirection_LeftToRight);
    /* 卡片宿主：窄档占满行宽（卡片 2 列随宽伸缩）；宽档伸到仪表行右列。
       布局方向切换后 stretch 语义随行向变化，两档各设一次（幂等）。 */
    if (self->m_cardHost) {
        if (!narrow) {
            XBoxLayout_setStretch(&self->m_gaugeRow, 0, 1);
            XBoxLayout_setStretch(&self->m_gaugeRow, 1, 1);
            XBoxLayout_setStretch(&self->m_gaugeRow, 2, 1);
            XBoxLayout_setStretch(&self->m_gaugeRow, 3, 2);
        } else {
            XBoxLayout_setStretch(&self->m_gaugeRow, 0, 1);
            XBoxLayout_setStretch(&self->m_gaugeRow, 1, 0);
            XBoxLayout_setStretch(&self->m_gaugeRow, 2, 0);
            XBoxLayout_setStretch(&self->m_gaugeRow, 3, 0);
        }
    }
    XLayout_activate((XLayout*)&self->m_layout);
    /* 画布宽随行伸缩 → 叠放标签跟随（幂等；帧泵每循环调用）。 */
    vfdGaugeRelayout(&self->m_gaugeFreq);
    vfdGaugeRelayout(&self->m_gaugeCur);
    vfdGaugeRelayout(&self->m_gaugeVolt);
}

/* ==================== 交互模式（自动运行锁） ==================== */
/* 契约见文件中段状态区注释（s_vfdPage/s_pendingAutoMode 定义处）。 */

void HsfMonitorVfd_setInteractiveMode(bool interactive)
{
    HsfMonitorVfd* self = s_vfdPage;
    bool autoMode = !interactive;
    if (!self) {
        /* 页未建：只记意图（create 时按此初始化并应用）。 */
        s_pendingAutoMode = autoMode;
        return;
    }
    if (self->m_autoMode == autoMode) return;
    self->m_autoMode = autoMode;
    if (interactive) {
        /* 退回手动：重显勾选框，闸值回到此前状态（用户/API 设定）。 */
        XCheckBox_setChecked(&self->m_gate, self->m_gateRemembered);
        XWidget_show((XWidget*)&self->m_gate);
        if (self->m_provider && self->m_provider->setControlEnabled)
            self->m_provider->setControlEnabled(self->m_provUser,
                                                self->m_gateRemembered);
    } else {
        /* 进自动运行：闸强制关（真实链路在自动程序手里）+ 隐藏
           勾选框（界面无从再开）。此前闸值先记忆，退手动时还原。 */
        self->m_gateRemembered = XCheckBox_isChecked(&self->m_gate);
        XCheckBox_setChecked(&self->m_gate, false);
        XWidget_hide((XWidget*)&self->m_gate);
        if (self->m_provider && self->m_provider->setControlEnabled)
            self->m_provider->setControlEnabled(self->m_provUser, false);
    }
    vfdApplyControlState(self);
    XWidget_update(&self->m_banner);            /* 横幅右侧提示联动。 */
}

bool HsfMonitorVfd_interactiveMode(void)
{
    return s_vfdPage ? !s_vfdPage->m_autoMode : !s_pendingAutoMode;
}

#endif /* XWIDGET_ON && ... && XVFD_ON */
