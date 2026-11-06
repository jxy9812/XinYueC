/**
 * @file       HsfMonitorFlowMeter.c
 * @brief      电磁流量计监视页实现：状态横幅 + 主读数 + 弧形仪表 + 数值卡 + 趋势。
 * @details    [层次]（TopToBottom）
 *               1) 状态横幅（自绘胶囊：状态色块 + 大字状态 + 报警摘要）
 *               2) 主读数区（大号瞬时流量 + 单位；副标题为流速）
 *               3) 仪表行（两枚 270° 弧形仪表：流量占量程比 / 信号强度）
 *               4) 数值卡行（2×2：流速 / 正向累积 / 反向累积 / 报警详情）
 *               5) 趋势图（XChartView+XLineSeries 瞬时流量 60 点滚动窗）
 *               6) 底注（数据源：B 侧 485 / COM18 / 站号 5）
 *
 *             [数据路径] HsfFlowMeterProvider 函数表：subscribe 注册回调，
 *             快照更新时回调 → fmRefresh() 读 snapshot() 刷全部控件。
 *             页面不认任何链路类型。
 *
 *             [只读] 没有操作行/控制闸：流量计无可下发动作。界面全部控件
 *             都是展示性质，无 setEnabled 联动。
 *
 *             [质量] 快照 m_valid 已由 provider 叠加「服务端有效 && 本地未
 *             断流」双重判定；无效时数值显 "--" 且横幅转灰。
 */
#include "HsfMonitorFlowMeter.h"

#include "HsfTheme.h"
#include "HsfFlowMeterProvider.h"
#include "HsfLinkClient.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XAbstractButton.h"
#include "XBoxLayout.h"
#include "XGridLayout.h"
#include "XChartView.h"
#include "XChart.h"
#include "XLineSeries.h"
#include "XXYSeries.h"
#include "XValueAxis.h"
#include "XObject.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XFont.h"
#include "XPainter.h"
#include "XString.h"
#include "XDateTime.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/* ==================== 常量与配色 ==================== */

/** @brief 流量计所在侧（现场：B 侧 485 / COM18 / 站号 5）。 */
#define FM_SIDE              1
#define FM_BANNER_H          56
#define FM_MAIN_H            110
#define FM_GAUGE_H           150
#define FM_CARD_H            64
#define FM_TREND_H           170
#define FM_TREND_POINTS      60
#define FM_GAUGE_MIN_W       130
#define FM_ARC_START_DEG     135.0f   /**< 弧起角（左下）。 */
#define FM_ARC_SWEEP_DEG     270.0f   /**< 弧扫角（顺时针到右下）。 */

/* 状态色（与变频器页同一套 WCAG 对比度校过的配色）。 */
#define FM_COLOR_OK          0xFF2E9E44u  /**< 正常绿。 */
#define FM_COLOR_IDLE        0xFF8A98A6u  /**< 无数据灰。 */
#define FM_COLOR_ALARM       0xFFD13438u  /**< 报警红。 */
#define FM_COLOR_WARN        0xFFCA8A04u  /**< 警告橙。 */
#define FM_COLOR_TRACK       0xFFC9D2DAu  /**< 仪表底轨。 */
#define FM_COLOR_GAUGE       0xFF1F4E79u  /**< 仪表进度（品牌蓝）。 */
#define FM_COLOR_GAUGE_2     0xFF0E8F8Fu  /**< 第二枚仪表（青色，区分）。 */
#define FM_COLOR_TEXT        0xFF1A2733u  /**< 主文本。 */
#define FM_COLOR_TEXT_DIM    0xFF5A6572u  /**< 弱文本。 */

/* 报警位（与服务端 XFlowMeterAlarm 对齐）。 */
#define FM_ALARM_UPPER       (1 << 0)
#define FM_ALARM_LOWER       (1 << 1)
#define FM_ALARM_EMPTY_PIPE  (1 << 2)
#define FM_ALARM_SYSTEM      (1 << 3)

/* ==================== 页上下文 ==================== */

struct HsfFmPage;
typedef struct HsfFmPage HsfFmPage;

/**
 * @brief 弧形仪表组（自绘 + 三个标签；owner+slot 分派 paintEvent）。
 */
typedef struct HsfFmGauge {
    XWidget  m_canvas;      /**< 自绘画布（本结构首成员；类虚表在下面定义）。 */
    HsfFmPage* m_owner;     /**< 所属页（取实时值）。 */
    int       m_slot;       /**< 0=流量比 1=信号强度。 */
    XLabel   m_title;       /**< 顶部标题。 */
    XLabel   m_value;       /**< 弧内主值。 */
    XLabel   m_unit;        /**< 弧内单位。 */
} HsfFmGauge;

/**
 * @brief 状态横幅（自绘胶囊）。
 */
typedef struct HsfFmBanner {
    XWidget   m_base;       /**< 自绘画布（首成员）。 */
    HsfFmPage* m_owner;     /**< 所属页。 */
} HsfFmBanner;

/**
 * @brief 页面上下文（堆分配；挂主窗级联释放）。
 */
struct HsfFmPage {
    XWidget m_root;                     /**< 页根。 */
    const HsfFlowMeterProvider* m_prov; /**< 数据提供者（借用；可为 NULL）。 */
    HsfFlowMeterSnapshot m_snap;        /**< 最近快照（本地副本，避免借用失效）。 */

    XBoxLayout m_mainLayout;            /**< 纵向主布局。 */
    HsfFmBanner m_banner;               /**< 状态横幅。 */
    XLabel     m_bannerLabel;           /**< 横幅文字（子标签，随横幅定位）。 */

    /* 主读数区。 */
    XWidget    m_mainHost;              /**< 主读数容器。 */
    XBoxLayout m_mainHostLayout;        /**< 容器布局（竖排）。 */
    XLabel     m_mainValue;             /**< 大号瞬时流量。 */
    XLabel     m_mainSub;               /**< 副行：流速 + 单位说明。 */

    /* 仪表行。 */
    XWidget    m_gaugeRow;              /**< 仪表行容器。 */
    XBoxLayout m_gaugeRowLayout;        /**< 横排/纵排（adapt 切换）。 */
    HsfFmGauge m_gaugeFlow;             /**< 流量占量程比。 */
    HsfFmGauge m_gaugeSignal;           /**< 信号强度（电导比示意）。 */

    /* 数值卡行。 */
    XWidget    m_cardHost;              /**< 卡片容器。 */
    XGridLayout m_cardGrid;             /**< 2×2 网格。 */
    XLabel     m_cardSpeed;             /**< 瞬时流速。 */
    XLabel     m_cardForward;           /**< 正向累积。 */
    XLabel     m_cardReverse;           /**< 反向累积。 */
    XLabel     m_cardAlarm;             /**< 报警详情。 */

    /* 趋势图。 */
    XChartView m_trendView;             /**< 图表视图。 */
    XChart*    m_trendChart;            /**< 图表（create 所有权在页）。 */
    XLineSeries* m_trendSeries;         /**< 瞬时流量曲线。 */
    XValueAxis* m_axisX;                /**< 时间轴。 */
    XValueAxis* m_axisY;                /**< 数值轴。 */
    double     m_trendT;                /**< 当前时间戳（秒，单调）。 */
    uint64_t   m_trendT0;               /**< 页创建时刻（单调毫秒）。 */

    XLabel     m_foot;                  /**< 底注（数据源说明）。 */

    /* ---- 水质块：浊度仪 ×2 + 进水泵（同页，对齐 Python「水质监测」页）---- */
    XWidget    m_waterHost;             /**< 水质块容器（标题 + 两行 + 泵行）。 */
    XBoxLayout m_waterLayout;           /**< 水质块纵向布局。 */
    XLabel     m_turbTitle;             /**< 「浊度仪」小标题。 */
    XLabel     m_turbValue[HSF_TURBIDITY_COUNT]; /**< 各台浊度值行（名称 + 值）。 */
    XLabel     m_pumpTitle;             /**< 「进水泵」小标题。 */
    XLabel     m_pumpState;             /**< 泵状态行。 */
    XPushButton m_pumpStart;            /**< 启动泵。 */
    XPushButton m_pumpStop;             /**< 停止泵。 */
    XLabel     m_pumpNote;              /**< 泵说明（无运行反馈点提示）。 */
    void (*m_pumpWrite)(void*, bool);  /**< 泵控制出口（on=true 启动）。 */
    void* m_pumpUser;                  /**< 泵出口令牌。 */

    /* 字段量程（用于弧形仪表比例；流量计不报量程，按常见工艺值取）。 */
    double     m_flowSpan;              /**< 流量满量程（用于比例）。 */
    double     m_signalRef;             /**< 信号强度参考值。 */
};

/** @brief 已建页借用指针（模块单例；未建 NULL）。 */
static HsfFmPage* s_fmPage = NULL;

/* ==================== 类虚表（自绘控件） ==================== */

/* 无自定义虚槽：两者都只重载 XWidget 的 PaintEvent（与变频器页同款）。
 * 注意宏必须 BEGING + EXTEND_END 配对——只写 BEGING 会缺 END_SIZE，
 * XVTABLE_INIT_DEFAULT 里 vtable_data 数组大小无法确定（编译错误）。 */
XCLASS_DEFINE_BEGING(HsfFmGauge)
XCLASS_DEFINE_EXTEND_END(HsfFmGauge, XWidget)

XCLASS_DEFINE_BEGING(HsfFmBanner)
XCLASS_DEFINE_EXTEND_END(HsfFmBanner, XWidget)

/* ==================== 工具 ==================== */

/**
 * @brief UTF-8 中文常量（避免源文件编码在不同编译器下失真）。
 */
#define FM_TXT_NO_DATA   "\xE6\x97\xA0\xE6\x95\xB0\xE6\x8D\xAE"          /* 无数据 */
#define FM_TXT_NORMAL    "\xE6\xAD\xA3\xE5\xB8\xB8"                      /* 正常 */
#define FM_TXT_ALARM     "\xE6\x8A\xA5\xE8\xAD\xA6"                      /* 报警 */
#define FM_TXT_EMPTY     "\xE7\xA9\xBA\xE7\xAE\xA1"                      /* 空管 */
#define FM_TXT_UPPER     "\xE4\xB8\x8A\xE9\x99\x90"                      /* 上限 */
#define FM_TXT_LOWER     "\xE4\xB8\x8B\xE9\x99\x90"                      /* 下限 */
#define FM_TXT_SYSTEM    "\xE7\xB3\xBB\xE7\xBB\x9F"                      /* 系统 */
#define FM_TXT_FLOW_UNIT "\xE6\xB5\x81\xE9\x87\x8F"                      /* 流量 */
#define FM_TXT_SPEED     "\xE6\xB5\x81\xE9\x80\x9F"                      /* 流速 */
#define FM_TXT_FWD_TOTAL "\xE6\xAD\xA3\xE5\x90\x91\xE7\xB4\xAF\xE8\xAE\xA1" /* 正向累计 */
#define FM_TXT_REV_TOTAL "\xE5\x8F\x8D\xE5\x90\x91\xE7\xB4\xAF\xE8\xAE\xA1" /* 反向累计 */
#define FM_TXT_SIGNAL    "\xE4\xBF\xA1\xE5\x8F\xB7"                      /* 信号 */
#define FM_TXT_RATIO     "\xE9\x87\x8F\xE7\xA8\x8B\xE6\xAF\x94"          /* 量程比 */

/** @brief 瞬时流量单位代码 → 文本（协议表 3-1）。 */
static const char* fmFlowUnitText(int code)
{
    static const char* const names[] = {
        "L/S", "L/M", "L/H", "M3/S", "M3/M", "M3/H",
        "T/S", "T/M", "T/H", "GPS", "GPM", "GPH"
    };
    if (code < 0 || code >= (int)(sizeof(names) / sizeof(names[0]))) return "";
    return names[code];
}

/** @brief 累积总量单位代码 → 文本（协议表 4-2 B 型口径）。 */
static const char* fmTotalUnitText(int code)
{
    static const char* const names[] = { "L", "M3", "T", "USG" };
    if (code < 0) return "";
    if (code < (int)(sizeof(names) / sizeof(names[0]))) return names[code];
    /* C 型 0~11：每 4 个一组，基础单位取模。 */
    if (code < 12) return names[code % 4];
    return "";
}

/**
 * @brief 报警位 → 中文摘要（空格分隔；无报警返回"正常"）。
 * @param bits 报警位掩码。
 * @param buf 输出缓冲。
 * @param cap 容量。
 */
static void fmAlarmText(int bits, char* buf, size_t cap)
{
    size_t n = 0;
    if (!buf || cap == 0) return;
    buf[0] = '\0';
    if (bits == 0) {
        snprintf(buf, cap, "%s", FM_TXT_NORMAL);
        return;
    }
#define FM_APPEND(txt) \
    do { if (n) { buf[n++] = ' '; buf[n] = '\0'; } \
         n += (size_t)snprintf(buf + n, (n < cap) ? cap - n : 0, "%s", (txt)); \
         if (n >= cap) n = cap - 1; } while (0)
    if (bits & FM_ALARM_EMPTY_PIPE) FM_APPEND(FM_TXT_EMPTY);
    if (bits & FM_ALARM_UPPER)      FM_APPEND(FM_TXT_UPPER);
    if (bits & FM_ALARM_LOWER)      FM_APPEND(FM_TXT_LOWER);
    if (bits & FM_ALARM_SYSTEM)     FM_APPEND(FM_TXT_SYSTEM);
#undef FM_APPEND
}

/** @brief 当前单调秒（趋势图横轴用）。 */
static double fmNowSeconds(HsfFmPage* p)
{
    return (double)(XDateTime_currentMSecsSinceEpoch() - p->m_trendT0) / 1000.0;
}

/* ==================== 自绘：弧形仪表 ==================== */

/**
 * @brief 画一段弧形带（自绘；内外半径之间按 0.5px 弦长步进 AA 填充）。
 * @details 与变频器页 vfdDrawArcBand 同款做法：角度步距取"外缘弦长≈0.25px"，
 *          相邻 AA 线羽化带充分重叠，带内覆盖率均匀无网纹。
 *          角度为数学坐标系（0°=右，逆时针为正），起点 135°、扫 -270°
 *          即"左下 → 顶部 → 右下"。
 * @param painter 画师。
 * @param cx/cy 圆心（像素）。
 * @param rInner/rOuter 内外半径。
 * @param startDeg 起始角（度）。
 * @param sweepDeg 扫角（度；负=顺时针）。
 */
static void fmDrawArcBand(XPainter* painter, int cx, int cy,
                          int rInner, int rOuter,
                          double startDeg, double sweepDeg)
{
    int i;
    int segments;
    double a0 = startDeg * 3.14159265358979323846 / 180.0;
    double sweepRad = sweepDeg * 3.14159265358979323846 / 180.0;
    double step;
    if (!painter) return;
    if (rOuter < 1 || sweepDeg == 0.0 || rOuter <= rInner) return;
    segments = (int)(2.0 * 3.14159265358979323846 * rOuter *
                     ((sweepDeg > 0 ? sweepDeg : -sweepDeg) / 360.0) * 4.0);
    if (segments < 360) segments = 360;
    if (segments > 4320) segments = 4320;
    step = sweepRad / segments;
    for (i = 0; i <= segments; ++i) {
        double a = a0 + step * i;
        double c = cos(a);
        double s = sin(a);
        XPainter_drawLine_3(painter,
                            (float)(cx + rInner * c), (float)(cy - rInner * s),
                            (float)(cx + rOuter * c), (float)(cy - rOuter * s));
    }
}

/** @brief 取某仪表槽的 0..1 比例。 */
static double fmGaugeRatio(const HsfFmPage* p, int slot)
{
    if (!p->m_snap.m_valid) return 0.0;
    if (slot == 0) {
        double span = (p->m_flowSpan > 0.0) ? p->m_flowSpan : 1.0;
        double r = fabs(p->m_snap.m_flow) / span;
        if (r < 0.0) r = 0.0;
        if (r > 1.0) r = 1.0;
        return r;
    }
    /* 信号槽：电导比服务端快照未带，用「数据有效即 0.75 弧」示意
     * （画满整圈看不出是仪表，也不像"信号强度"）。 */
    return p->m_snap.m_valid ? 0.75 : 0.0;
}

/* ==================== 自绘：仪表/横幅 paintEvent ==================== */

static void VHsfFmGauge_paintEvent(XWidget* self, XEvent* event);
static void VHsfFmBanner_paintEvent(XWidget* self, XEvent* event);

static XVtable* fmGaugeClassInit(void)
{
    XVTABLE_INIT_DEFAULT(HsfFmGauge)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfFmGauge_paintEvent);
    XCLASS_SHOW_SIZE_DEFAULT(HsfFmGauge);
    return XVTABLE_DEFAULT;
}

static XVtable* fmBannerClassInit(void)
{
    XVTABLE_INIT_DEFAULT(HsfFmBanner)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfFmBanner_paintEvent);
    XCLASS_SHOW_SIZE_DEFAULT(HsfFmBanner);
    return XVTABLE_DEFAULT;
}

/**
 * @brief 仪表 paintEvent：底盘 + 底轨带 + 进度带 + 两端刻度。
 */
static void VHsfFmGauge_paintEvent(XWidget* self, XEvent* event)
{
    HsfFmGauge* g = (HsfFmGauge*)self;
    XPainter painter;
    XImage* image;
    double ratio;
    int w;
    int h;
    int cx;
    int cy;
    int r;
    int bandInner;
    int bandOuter;
    uint32_t color;

    if (!g || !g->m_owner) return;
    if (!event) return;
    XEvent_accept(event);

    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }

    w = XWidget_width(self);
    h = XWidget_height(self);
    if (w <= 8 || h <= 8) {
        XPainter_deinit(&painter);
        return;
    }

    XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);
    /* [关键] 必须按 paintOffset 平移：paintImage 给的是整窗后备图像，
     * 画师原点在窗口左上角；不平移会把整套表盘画到窗口原点、被别的
     * 控件盖住（变频器页同款做法）。 */
    {
        XPoint off = XWidget_paintOffset(self);
        if (off.x != 0 || off.y != 0)
            XPainter_translate(&painter, (float)off.x, (float)off.y);
    }

    /* 弧心略偏下：给弧内数值留出上方空间。 */
    cx = w / 2;
    cy = (int)(h * 0.58f);
    r = ((w < h * 5 / 4) ? (w / 2) : (h / 2)) - 12;
    if (r < 20) r = 20;
    bandInner = r - 9;
    bandOuter = r;

    ratio = fmGaugeRatio(g->m_owner, g->m_slot);
    color = (g->m_slot == 0) ? FM_COLOR_GAUGE : FM_COLOR_GAUGE_2;

    /* ① 底盘（浅色填充，与卡片同底）。 */
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter, 0xFFF7F9FB);
    XPainter_setPen(&painter, 0xFFF7F9FB);
    fmDrawArcBand(&painter, cx, cy, 0, r, 0.0, 360.0);

    /* ② 底轨（整弧，浅灰）。 */
    XPainter_setPen(&painter, FM_COLOR_TRACK);
    fmDrawArcBand(&painter, cx, cy, bandInner, bandOuter, 135.0, -270.0);

    /* ③ 进度带（按比例；从起点顺时针）。 */
    if (ratio > 0.001) {
        XPainter_setPen(&painter, color);
        fmDrawArcBand(&painter, cx, cy, bandInner - 1, bandOuter + 1,
                      135.0, -270.0 * ratio);
    }

    /* ④ 两端小刻度（视觉锚点）。 */
    {
        int i;
        for (i = 0; i < 2; ++i) {
            double deg = (i == 0) ? 135.0 : -135.0;
            double rad = deg * 3.14159265358979323846 / 180.0;
            float x0 = (float)(cx + (r - 14) * cos(rad));
            float y0 = (float)(cy - (r - 14) * sin(rad));
            float x1 = (float)(cx + (r + 2) * cos(rad));
            float y1 = (float)(cy - (r + 2) * sin(rad));
            XPainter_setPenWidth(&painter, 2);
            XPainter_setPen(&painter, FM_COLOR_TEXT_DIM);
            XPainter_drawLine_3(&painter, x0, y0, x1, y1);
        }
    }

    XPainter_deinit(&painter);
}

/**
 * @brief 横幅 paintEvent：圆角胶囊底色（状态文字由子 XLabel 承载）。
 */
static void VHsfFmBanner_paintEvent(XWidget* self, XEvent* event)
{
    HsfFmBanner* b = (HsfFmBanner*)self;
    XPainter painter;
    XImage* image;
    XRect rect;
    uint32_t color;
    int w;
    int h;
    int radius;

    if (!b || !b->m_owner) return;
    if (!event) return;
    XEvent_accept(event);

    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }

    w = XWidget_width(self);
    h = XWidget_height(self);
    if (w > 4 && h > 4) {
        XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);
        {
            XPoint off = XWidget_paintOffset(self);
            if (off.x != 0 || off.y != 0)
                XPainter_translate(&painter, (float)off.x, (float)off.y);
        }
        radius = (h / 2) > 14 ? 14 : (h / 2);

        if (!b->m_owner->m_snap.m_valid)
            color = FM_COLOR_IDLE;
        else if (b->m_owner->m_snap.m_alarms != 0)
            color = FM_COLOR_ALARM;
        else
            color = FM_COLOR_OK;

        rect.x = 0;
        rect.y = 0;
        rect.width = w;
        rect.height = h;
        XPainter_setBrush(&painter, color);
        XPainter_setPen(&painter, color);
        XPainter_setPenWidth(&painter, 1);
        XPainter_drawRoundedRect(&painter, &rect, radius, radius);
    }

    XPainter_deinit(&painter);
}

/* ==================== 控件构建助手 ==================== */

/** @brief 统一字体+像素尺寸设置。 */
static void fmSetFont(XWidget* w, int px, bool bold, bool dim)
{
    XFont f;
    HsfTheme_applyDefaultFont(w);
    f = XWidget_font(w);
    XFont_setBold(&f, bold);
    XWidget_setFont(w, &f);
    XClassDeinit(&f);
    XLabel_setTextPixelSize((XLabel*)w, px);
    if (dim)
        XWidget_setForegroundRole(w, XPaletteColorRole_Mid);
}

/** @brief 建一个展示标签（左对齐/居中由 align 决定）。 */
static void fmMakeLabel(XLabel* lb, XWidget* parent, const char* text,
                        int px, bool bold, bool dim, int align)
{
    XLabel_init(lb, parent, 0);
    fmSetFont((XWidget*)lb, px, bold, dim);
    XLabel_setText_2(lb, text);
    XLabel_setAlignment(lb, align);
    XWidget_show((XWidget*)lb);
}

/** @brief 初始化一枚弧形仪表（画布 + 三个子标签）。 */
static void fmGaugeInit(HsfFmGauge* g, HsfFmPage* owner, XWidget* parent,
                        int slot, const char* title, const char* unit)
{
    memset(g, 0, sizeof(*g));
    XWidget_init(&g->m_canvas, parent, 0);
    XClassGetVtable(&g->m_canvas) = fmGaugeClassInit();
    g->m_owner = owner;
    g->m_slot = slot;
    HsfTheme_applyDefaultFont(&g->m_canvas);
    XWidget_setFixedHeight(&g->m_canvas, FM_GAUGE_H);
    XWidget_setMinimumWidth(&g->m_canvas, FM_GAUGE_MIN_W);
    /* 水平 Expanding：不声明时布局按 sizeHint(0) 给宽，画布宽度 0 →
     * 弧画不出来、三个子标签全挤在左上角。 */
    XWidget_setSizePolicy(&g->m_canvas, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Fixed);

    fmMakeLabel(&g->m_title, &g->m_canvas, title, 13, false, true,
                XAlignment_HCenter | XAlignment_VCenter);
    fmMakeLabel(&g->m_value, &g->m_canvas, "--", 18, true, false,
                XAlignment_HCenter | XAlignment_VCenter);
    fmMakeLabel(&g->m_unit, &g->m_canvas, unit, 12, false, true,
                XAlignment_HCenter | XAlignment_VCenter);
    XWidget_show(&g->m_canvas);
}

/** @brief 仪表内部标签重排（画布尺寸变化时调）。 */
static void fmGaugeRelayout(HsfFmGauge* g)
{
    int w;
    int h;
    int cx;
    int vTop;
    if (!g) return;
    w = XWidget_width(&g->m_canvas);
    h = XWidget_height(&g->m_canvas);
    if (w <= 0) w = 140;
    if (h <= 0) h = FM_GAUGE_H;
    cx = w / 2;
    XWidget_move((XWidget*)&g->m_title, cx - 50, 6);
    XWidget_resize((XWidget*)&g->m_title, 100, 20);
    /* 数值放在弧心稍上（弧心在 58% 高度）。 */
    vTop = (int)(h * 0.58f) - 24;
    XWidget_move((XWidget*)&g->m_value, cx - 56, vTop);
    XWidget_resize((XWidget*)&g->m_value, 112, 30);
    XWidget_move((XWidget*)&g->m_unit, cx - 44, vTop + 30);
    XWidget_resize((XWidget*)&g->m_unit, 88, 18);
}

/** @brief 数值卡初始化（两行文本：名称 + 值）。 */
static void fmMakeCard(XLabel* card, XWidget* parent, const char* text)
{
    XLabel_init(card, parent, 0);
    fmSetFont((XWidget*)card, 13, false, false);
    XLabel_setText_2(card, text);
    XLabel_setAlignment(card, XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setMinimumHeight((XWidget*)card, FM_CARD_H);
    /* 卡片底色：走主题的 Base 角色（与其它页卡片同色），加内边距避免贴边。 */
    XWidget_setBackgroundRole((XWidget*)card, XPaletteColorRole_Base);
    XWidget_setAutoFillBackground((XWidget*)card, true);
    XWidget_setContentsMargins((XWidget*)card, 8, 6, 8, 6);
    XWidget_setSizePolicy((XWidget*)card, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    XWidget_show((XWidget*)card);
}

/* ==================== 刷新（数据 → UI） ==================== */

/**
 * @brief 把 provider 快照刷到全部控件。
 * @param p 页上下文。
 * @note  无效数据全部显 "--"，横幅转灰；有效则按报警位取色。
 */
static void fmRefresh(HsfFmPage* p)
{
    const HsfFlowMeterSnapshot* src;
    char buf[128];
    char alarmBuf[96];

    if (!p) return;
    memset(&p->m_snap, 0, sizeof(p->m_snap));
    p->m_snap.m_flowUnit = 0xFF;
    p->m_snap.m_totalUnit = 0xFF;
    if (p->m_prov && p->m_prov->snapshot) {
        src = p->m_prov->snapshot(p->m_prov->m_userData);
        if (src) p->m_snap = *src;      /* 值语义拷贝：不持有借用指针。 */
    }

    /* ---- 横幅 ---- */
    if (!p->m_snap.m_valid) {
        XLabel_setText_2(&p->m_bannerLabel, FM_TXT_NO_DATA);
    } else if (p->m_snap.m_alarms != 0) {
        fmAlarmText(p->m_snap.m_alarms, alarmBuf, sizeof(alarmBuf));
        snprintf(buf, sizeof(buf), "%s: %s", FM_TXT_ALARM, alarmBuf);
        XLabel_setText_2(&p->m_bannerLabel, buf);
    } else {
        XLabel_setText_2(&p->m_bannerLabel, FM_TXT_NORMAL);
    }
    XWidget_update((XWidget*)&p->m_banner.m_base);

    /* ---- 主读数 ---- */
    if (p->m_snap.m_valid) {
        const char* un = fmFlowUnitText(p->m_snap.m_flowUnit);
        snprintf(buf, sizeof(buf), "%.3f", p->m_snap.m_flow);
        XLabel_setText_2(&p->m_mainValue, buf);
        snprintf(buf, sizeof(buf), "%s   %s %.3f m/s", un, FM_TXT_SPEED,
                 p->m_snap.m_speed);
        XLabel_setText_2(&p->m_mainSub, buf);
    } else {
        XLabel_setText_2(&p->m_mainValue, "--");
        XLabel_setText_2(&p->m_mainSub, "");
    }

    /* ---- 仪表 ---- */
    if (p->m_snap.m_valid) {
        snprintf(buf, sizeof(buf), "%.1f%%",
                 fmGaugeRatio(p, 0) * 100.0);
        XLabel_setText_2(&p->m_gaugeFlow.m_value, buf);
        XLabel_setText_2(&p->m_gaugeSignal.m_value,
                         p->m_snap.m_valid ? "OK" : "--");
    } else {
        XLabel_setText_2(&p->m_gaugeFlow.m_value, "--");
        XLabel_setText_2(&p->m_gaugeSignal.m_value, "--");
    }
    XWidget_update(&p->m_gaugeFlow.m_canvas);
    XWidget_update(&p->m_gaugeSignal.m_canvas);

    /* ---- 数值卡 ---- */
    {
        const char* tun = fmTotalUnitText(p->m_snap.m_totalUnit);
        if (p->m_snap.m_valid) {
            snprintf(buf, sizeof(buf), "%s\n%.3f m/s", FM_TXT_SPEED,
                     p->m_snap.m_speed);
            XLabel_setText_2(&p->m_cardSpeed, buf);
            snprintf(buf, sizeof(buf), "%s\n%.3f %s", FM_TXT_FWD_TOTAL,
                     p->m_snap.m_forwardTotal, tun);
            XLabel_setText_2(&p->m_cardForward, buf);
            snprintf(buf, sizeof(buf), "%s\n%.3f %s", FM_TXT_REV_TOTAL,
                     p->m_snap.m_reverseTotal, tun);
            XLabel_setText_2(&p->m_cardReverse, buf);
            fmAlarmText(p->m_snap.m_alarms, alarmBuf, sizeof(alarmBuf));
            snprintf(buf, sizeof(buf), "%s\n%s", FM_TXT_ALARM, alarmBuf);
            XLabel_setText_2(&p->m_cardAlarm, buf);
        } else {
            XLabel_setText_2(&p->m_cardSpeed, FM_TXT_SPEED "\n--");
            XLabel_setText_2(&p->m_cardForward, FM_TXT_FWD_TOTAL "\n--");
            XLabel_setText_2(&p->m_cardReverse, FM_TXT_REV_TOTAL "\n--");
            XLabel_setText_2(&p->m_cardAlarm, FM_TXT_ALARM "\n--");
        }
    }

    /* ---- 趋势：只有有效数据才推点，避免断流时画出假的 0 值 ---- */
    if (p->m_snap.m_valid) {
        p->m_trendT = fmNowSeconds(p);
        if (p->m_trendSeries) {
            XXYSeries_append((XXYSeries*)p->m_trendSeries, p->m_trendT,
                             p->m_snap.m_flow);
            /* 滚动窗：超量从头部删（与变频器页同款口径）。 */
            if (XXYSeries_count((XXYSeries*)p->m_trendSeries) > FM_TREND_POINTS) {
                XXYSeries_removePoints((XXYSeries*)p->m_trendSeries, 0,
                    XXYSeries_count((XXYSeries*)p->m_trendSeries) - FM_TREND_POINTS);
            }
        }
        XChartView_updateChart(&p->m_trendView);
    }
}

/**
 * @brief provider 回调：快照更新 → 刷 UI。
 */
static void fmOnProviderNotify(const struct HsfFlowMeterProvider* provider,
                               void* page)
{
    (void)provider;
    fmRefresh((HsfFmPage*)page);
}

/* ==================== 建页 ==================== */

/* ==================== 水质块（浊度仪 ×2 + 进水泵） ==================== */

/**
 * @brief 泵控制按钮点击（启动/停止）。
 * @note  [泵没有反馈] 现场只有接触器输出：按钮点下去就是"命令已发出"，
 *        状态行显示的是**命令状态**（我方最后写的值），不代表泵在转。
 */
static void fmPumpStartSlot(XObject* receiver, XVarList* args)
{
    HsfFmPage* p = (HsfFmPage*)((char*)receiver -
                                 offsetof(HsfFmPage, m_pumpStart));
    (void)args;
    if (!p || !p->m_pumpWrite) return;
    p->m_pumpWrite(p->m_pumpUser, true);
}

static void fmPumpStopSlot(XObject* receiver, XVarList* args)
{
    HsfFmPage* p = (HsfFmPage*)((char*)receiver -
                                 offsetof(HsfFmPage, m_pumpStop));
    (void)args;
    if (!p || !p->m_pumpWrite) return;
    p->m_pumpWrite(p->m_pumpUser, false);
}

/**
 * @brief 构建水质块（标题 + 浊度两行 + 泵行）。
 * @details [为什么与流量同页] 流量计、浊度仪、进水泵同属一条水质工艺链
 *          （原水浊度 → 过滤 → 出水浊度 → 流量计量 → 补水泵），现场是并排
 *          看的一组数；对齐 Python 客户端的「水质监测」页布局。
 */
static void fmBuildWaterBlock(HsfFmPage* p)
{
    int i;

    XWidget_init(&p->m_waterHost, &p->m_root, 0);
    XBoxLayout_init(&p->m_waterLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&p->m_waterLayout, 6);
    XWidget_setLayout(&p->m_waterHost, (XLayout*)&p->m_waterLayout);

    fmMakeLabel(&p->m_turbTitle, &p->m_waterHost,
                "\xE6\xB8\xA0\xE5\xBA\xA6\xE4\xBB\xAA",  /* 浊度仪 */
                13, true, false, XAlignment_Left | XAlignment_VCenter);
    XBoxLayout_addWidget(&p->m_waterLayout, (XWidget*)&p->m_turbTitle);
    for (i = 0; i < HSF_TURBIDITY_COUNT; ++i) {
        fmMakeLabel(&p->m_turbValue[i], &p->m_waterHost, "--", 13, false, false,
                    XAlignment_Left | XAlignment_VCenter);
        XBoxLayout_addWidget(&p->m_waterLayout,
                            (XWidget*)&p->m_turbValue[i]);
    }
    fmMakeLabel(&p->m_pumpTitle, &p->m_waterHost,
                "\xE8\xBF\x9B\xE6\xB0\xB4\xE6\xB3\xB5",  /* 进水泵 */
                13, true, false, XAlignment_Left | XAlignment_VCenter);
    XBoxLayout_addWidget(&p->m_waterLayout, (XWidget*)&p->m_pumpTitle);
    fmMakeLabel(&p->m_pumpState, &p->m_waterHost, "--", 13, false, false,
                XAlignment_Left | XAlignment_VCenter);
    XBoxLayout_addWidget(&p->m_waterLayout, (XWidget*)&p->m_pumpState);
    {
        XWidget* row = XWidget_create(&p->m_waterHost, 0);
        XPushButton_init(&p->m_pumpStart, row, 0);
        XPushButton_init(&p->m_pumpStop, row, 0);
        XPushButton_setText_2(&p->m_pumpStart,
                            "\xE5\x90\xAF\xE5\x8A\xA8");  /* 启动 */
        XPushButton_setText_2(&p->m_pumpStop,
                            "\xE5\x81\x9C\xE6\xAD\xA2");  /* 停止 */
        XWidget_setFixedHeight((XWidget*)&p->m_pumpStart, 34);
        XWidget_setFixedHeight((XWidget*)&p->m_pumpStop, 34);
        XObject_connect_1((XObject*)&p->m_pumpStart,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&p->m_pumpStart, fmPumpStartSlot,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)&p->m_pumpStop,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&p->m_pumpStop, fmPumpStopSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)row);
        XBoxLayout_addWidget(&p->m_waterLayout, row);
    }
    fmMakeLabel(&p->m_pumpNote, &p->m_waterHost,
                "\xE7\x8E\xB0\xE5\x9C\xBA\xE4\xB8\x8A\xE6\x9C\x89\xE6\x9C\x89"
                "\xE8\xA7\xA6\xE5\x99\xA8\xE8\xBE\x93\xE5\x87\xBA\xE3\x80\x81"
                "\xE6\x97\xA0\xE8\xBF\x90\xE8\xA1\x8C\xE5\x8F\x8D\xE9\xA6\x88"
                "\xE7\x82\xB9\xEF\xBC\x9A\xE6\xAD\xA4\xE5\x8F\xB3\xE6\x98\xAF"
                "\xE8\xBE\x93\xE5\x87\xBA\xE5\x91\xBD\xE4\xBB\xA4\xE7\x8A\xB6\xE6\x80\x81"
                "\xEF\xBC\x8C\xE4\xB8\x8D\xE4\xBB\xA3\xE8\xA1\x8C\xE8\xB7\x91\xE5\x8A\xA8\xE3\x80\x82",
                /* 现场仅有接触器输出、无运行反馈点：此处显示输出命令状态，
                   不代表泵确实在转。 */
                11, false, true, XAlignment_Left | XAlignment_VCenter);
    XBoxLayout_addWidget(&p->m_waterLayout, (XWidget*)&p->m_pumpNote);
    XWidget_show(&p->m_waterHost);
}

XWidget* HsfMonitorFlowMeter_create(XWidget* parent, HsfMenuNavFn navFn,
                                    void* userData)
{
    HsfFmPage* p;
    (void)navFn;
    (void)userData;

    p = (HsfFmPage*)XMemory_calloc(1, sizeof(HsfFmPage),
                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    p->m_flowSpan = 100.0;      /* 满量程占位（流量计不报量程）。 */
    p->m_signalRef = 1.0;
    p->m_trendT0 = XDateTime_currentMSecsSinceEpoch();
    s_fmPage = p;

    if (!parent) {
        XMemory_free(p, XCLASS_DEFAULT_MEMORY_TYPE);
        return NULL;
    }
    XWidget_init(&p->m_root, parent, 0);
    /* 与变频器页同款：堆块必须登记内存类型与堆标记，否则框架按栈对象
     * 处理（不参与级联释放、几何/布局通知链可能不完整）。 */
    Set_Class_Memory(p, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(p, true);
    HsfTheme_applyDefaultFont(&p->m_root);

    /* ---- 主布局（纵向） ----
     * [关键] XWidget_setLayout 必须调：只 addWidget 而不把布局挂到页根，
     * 布局不参与几何计算，子控件全部落在 0×0（只有自带尺寸的趋势图可见）。 */
    XBoxLayout_init(&p->m_mainLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&p->m_mainLayout, 20, 14, 20, 14);
    XLayout_setSpacing((XLayout*)&p->m_mainLayout, 10);

    /* ---- ① 状态横幅 ---- */
    {
        XWidget_init(&p->m_banner.m_base, &p->m_root, 0);
        XClassGetVtable(&p->m_banner.m_base) = fmBannerClassInit();
        p->m_banner.m_owner = p;
        HsfTheme_applyDefaultFont(&p->m_banner.m_base);
        XWidget_setFixedHeight(&p->m_banner.m_base, FM_BANNER_H);
        fmMakeLabel(&p->m_bannerLabel, &p->m_banner.m_base, FM_TXT_NO_DATA,
                    20, true, false,
                    XAlignment_HCenter | XAlignment_VCenter);
        XWidget_show(&p->m_banner.m_base);
    }

    /* ---- ② 主读数区 ---- */
    XWidget_init(&p->m_mainHost, &p->m_root, 0);
    XBoxLayout_init(&p->m_mainHostLayout, XBoxLayoutDirection_TopToBottom);
    XWidget_setLayout(&p->m_mainHost, (XLayout*)&p->m_mainHostLayout);
    XLayout_setSpacing((XLayout*)&p->m_mainHostLayout, 2);
    XWidget_setFixedHeight(&p->m_mainHost, FM_MAIN_H);
    XWidget_setSizePolicy(&p->m_mainHost, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    fmMakeLabel(&p->m_mainValue, &p->m_mainHost, "--", 44, true, false,
                XAlignment_HCenter | XAlignment_VCenter);
    fmMakeLabel(&p->m_mainSub, &p->m_mainHost, "", 15, false, true,
                XAlignment_HCenter | XAlignment_VCenter);
    /* 两个标签都要登记进容器布局；只 init 挂成子控件的话它们不受布局
     * 管理，各自按 sizeHint 停在容器原点 —— 大号数字会溢出压住横幅。 */
    XBoxLayout_addWidget(&p->m_mainHostLayout, (XWidget*)&p->m_mainValue);
    XBoxLayout_addWidget(&p->m_mainHostLayout, (XWidget*)&p->m_mainSub);
    XWidget_show(&p->m_mainHost);

    /* ---- ③ 仪表行 ---- */
    XWidget_init(&p->m_gaugeRow, &p->m_root, 0);
    XBoxLayout_init(&p->m_gaugeRowLayout, XBoxLayoutDirection_LeftToRight);
    XWidget_setLayout(&p->m_gaugeRow, (XLayout*)&p->m_gaugeRowLayout);
    XLayout_setSpacing((XLayout*)&p->m_gaugeRowLayout, 12);
    fmGaugeInit(&p->m_gaugeFlow, p, &p->m_gaugeRow, 0, FM_TXT_RATIO, "");
    fmGaugeInit(&p->m_gaugeSignal, p, &p->m_gaugeRow, 1, FM_TXT_SIGNAL, "");
    /* [关键] 画布必须**登记进布局**：只 XWidget_init 挂成子控件而不
     * addWidget，布局不知道它的存在 —— 控件永远停在 sizeHint 大小，
     * 弧画不出来、三个子标签挤在左上角（本页首版实锅）。 */
    XBoxLayout_addWidget(&p->m_gaugeRowLayout,
                         &p->m_gaugeFlow.m_canvas);
    XBoxLayout_addWidget(&p->m_gaugeRowLayout,
                         &p->m_gaugeSignal.m_canvas);
    XBoxLayout_setStretch(&p->m_gaugeRowLayout, 0, 1);
    XBoxLayout_setStretch(&p->m_gaugeRowLayout, 1, 1);
    XWidget_setSizePolicy(&p->m_gaugeRow, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    XWidget_show(&p->m_gaugeRow);

    /* ---- ④ 数值卡行（2×2） ---- */
    XWidget_init(&p->m_cardHost, &p->m_root, 0);
    XGridLayout_init(&p->m_cardGrid);
    XWidget_setLayout(&p->m_cardHost, (XLayout*)&p->m_cardGrid);
    XWidget_setSizePolicy(&p->m_cardHost, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    fmMakeCard(&p->m_cardSpeed, &p->m_cardHost, FM_TXT_SPEED "\n--");
    fmMakeCard(&p->m_cardForward, &p->m_cardHost, FM_TXT_FWD_TOTAL "\n--");
    fmMakeCard(&p->m_cardReverse, &p->m_cardHost, FM_TXT_REV_TOTAL "\n--");
    fmMakeCard(&p->m_cardAlarm, &p->m_cardHost, FM_TXT_ALARM "\n--");
    XGridLayout_addWidget(&p->m_cardGrid, (XWidget*)&p->m_cardSpeed, 0, 0, 0);
    XGridLayout_addWidget(&p->m_cardGrid, (XWidget*)&p->m_cardForward, 0, 1, 0);
    XGridLayout_addWidget(&p->m_cardGrid, (XWidget*)&p->m_cardReverse, 1, 0, 0);
    XGridLayout_addWidget(&p->m_cardGrid, (XWidget*)&p->m_cardAlarm, 1, 1, 0);
    XWidget_show(&p->m_cardHost);

    /* ---- ⑤ 趋势图 ---- */
    XChartView_init(&p->m_trendView, &p->m_root, 0);
    XWidget_setMinimumHeight(&p->m_trendView.m_base, FM_TREND_H);
    p->m_trendChart = XChart_create();
    if (p->m_trendChart) {
        XChart_setTitle_2(p->m_trendChart, FM_TXT_FLOW_UNIT);
        p->m_trendSeries = XLineSeries_create();
        if (p->m_trendSeries) {
            XChart_addSeries(p->m_trendChart, p->m_trendSeries,
                             XChartSeriesType_Line);
        }
        XChart_createDefaultAxes(p->m_trendChart);
        p->m_axisX = (XValueAxis*)XChart_axisX(p->m_trendChart);
        p->m_axisY = (XValueAxis*)XChart_axisY(p->m_trendChart);
        if (p->m_axisY) {
            XValueAxis_setRange(p->m_axisY, 0.0, 100.0);
            /* 刻度数收紧到 5（默认 6）：图高 170 时尚可，窄/矮视口下
             * 6 个标签会挤成一团（竖屏 560x900 实测 Y 轴数字重叠）。 */
            XValueAxis_setTickCount(p->m_axisY, 5);
        }
        XChartView_setChart(&p->m_trendView, p->m_trendChart);
    }
    XChartView_setPanEnabled(&p->m_trendView, true);
    XChartView_setWheelZoomXOnly(&p->m_trendView, true);
    XChartView_setTouchGesturesEnabled(&p->m_trendView, true);
    XChartView_setTimeAxisMode(&p->m_trendView, true);
    XWidget_show(&p->m_trendView.m_base);

    XWidget_setLayout(&p->m_root, (XLayout*)&p->m_mainLayout);
    /* [关键] 页根必须双向 Expanding：HsfStack 用 XStackedLayout 托管页面，
     * 该布局按子控件 sizeHint 落尺寸 —— 不声明 Expanding 时页根只拿到
     * 内容 hint 大小（实测 100x30），内部一切布局都在 100x30 里算，
     * 于是横幅/读数/仪表/卡片全部挤成一团画不出来。 */
    XWidget_setSizePolicy(&p->m_root, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);

    /* ---- ⑥ 底注 ---- */
    fmMakeLabel(&p->m_foot, &p->m_root,
                "B \xE4\xBE\xA7 485 / COM18 / \xE7\xAB\x99\xE5\x8F\xB7 5", /* B 侧 485 / COM18 / 站号 5 */
                12, false, true, XAlignment_HCenter | XAlignment_VCenter);

    /* ---- 把主要区块挂进主布局 ---- */
    XBoxLayout_addWidget(&p->m_mainLayout, (XWidget*)&p->m_banner.m_base);
    XBoxLayout_addWidget(&p->m_mainLayout, &p->m_mainHost);
    XBoxLayout_addWidget(&p->m_mainLayout, &p->m_gaugeRow);
    XBoxLayout_addWidget(&p->m_mainLayout, &p->m_cardHost);
    XBoxLayout_addWidget(&p->m_mainLayout, (XWidget*)&p->m_trendView.m_base);
    XBoxLayout_addWidget(&p->m_mainLayout, (XWidget*)&p->m_foot);
    /* 水质块（浊度 + 泵）：与流量同属一条工艺链，放一页看。 */
    fmBuildWaterBlock(p);
    XBoxLayout_addWidget(&p->m_mainLayout, &p->m_waterHost);

    XWidget_show(&p->m_root);

    /* ---- 接数据源 ---- */
    p->m_prov = HsfFlowMeterProvider_remote(FM_SIDE);
    if (p->m_prov) {
        if (p->m_prov->subscribe)
            p->m_prov->subscribe(p->m_prov->m_userData, fmOnProviderNotify, p);
        if (p->m_prov->requestSnapshot)
            p->m_prov->requestSnapshot(p->m_prov->m_userData);
    }
    fmRefresh(p);

    /* [滚动壳协作] HsfUi 给每页套了 XScrollArea（widgetResizable，横向
     * 滚动条永远关）——页内容比视口高时**必须靠页根的最小高度**撑出来，
     * 否则滚动壳按视口高度压缩页根，下方内容直接看不见（矮窗 800x480
     * 实测：仪表只露一半、趋势图整块消失）。这里按各区块实际高度累加。
     * 竖向留白 = 6 个区块间距(10) + 上下边距(14+14)。 */
    XWidget_setMinimumHeight(&p->m_root,
                             FM_BANNER_H + FM_MAIN_H + FM_GAUGE_H +
                             FM_CARD_H * 2 + FM_TREND_H + 18 /*底注*/ +
                             6 * 10 + 14 + 14);

    return &p->m_root;
}

/* ==================== 自适应 ==================== */

void HsfMonitorFlowMeter_adapt(XWidget* page, bool compact)
{
    HsfFmPage* p;
    if (!page) return;
    p = (HsfFmPage*)page;       /* m_root 是首成员，指针即上下文。 */

    XBoxLayout_setDirection(&p->m_gaugeRowLayout,
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);
    fmGaugeRelayout(&p->m_gaugeFlow);
    fmGaugeRelayout(&p->m_gaugeSignal);

    XLabel_setTextPixelSize(&p->m_mainValue, compact ? 34 : 44);
    XLabel_setTextPixelSize(&p->m_bannerLabel, compact ? 17 : 20);
    XWidget_setMinimumHeight(&p->m_trendView.m_base,
                             compact ? 120 : FM_TREND_H);

    /* 页根最小高度随形态重算：竖屏时仪表改纵排（两枚 150 高 → 300），
     * 趋势图压到 120，其余不变。滚动壳按此撑出滚范围，矮视口下才能
     * 滚到全部内容。 */
    if (compact) {
        XWidget_setMinimumHeight(&p->m_root,
                                 FM_BANNER_H + FM_MAIN_H + FM_GAUGE_H * 2 +
                                 FM_CARD_H * 2 + 120 + 18 +
                                 6 * 10 + 14 + 14);
    } else {
        XWidget_setMinimumHeight(&p->m_root,
                                 FM_BANNER_H + FM_MAIN_H + FM_GAUGE_H +
                                 FM_CARD_H * 2 + FM_TREND_H + 18 +
                                 6 * 10 + 14 + 14);
    }
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

/* ==================== 水质块公开 API ==================== */

/**
 * @brief 设某台浊度仪的读数。
 * @param index 台序（0 起）。
 * @param name 仪器名（服务端给；NULL=用"浊度仪N"占位）。
 * @param ntu 浊度值（NTU 原值；客户端内部按 ×100 定点口径换算）。
 * @param online 该台是否在线。
 * @note  ≥20 NTU 标红（现场水质告警线，对齐 Python 客户端口径）。
 */
void HsfMonitorFlowMeter_setTurbidity(int index, const char* name,
                                      double ntu, bool online)
{
    HsfFmPage* p = s_fmPage;
    char buf[96];

    if (!p || index < 0 || index >= HSF_TURBIDITY_COUNT) return;
    if (!online) {
        snprintf(buf, sizeof(buf), "%s  --",
                 (name && name[0]) ? name : "浊度仪");
        XLabel_setText_2(&p->m_turbValue[index], buf);
        XWidget_setForegroundRole((XWidget*)&p->m_turbValue[index],
                                  XPaletteColorRole_Mid);
        return;
    }
    snprintf(buf, sizeof(buf), "%s  %.2f NTU",
             (name && name[0]) ? name : "浊度仪", ntu);
    XLabel_setText_2(&p->m_turbValue[index], buf);
    /* [≥20 NTU 标红] 现场水质告警线（对齐 Python 客户端 TURBIDITY_WARN_NTU）。 */
    XWidget_setForegroundRole((XWidget*)&p->m_turbValue[index],
                              (ntu >= 20.0) ? XPaletteColorRole_BrightText
                                             : XPaletteColorRole_Text);
}

/**
 * @brief 设进水泵状态（命令状态）。
 * @param name 泵名（服务端给；NULL=占位）。
 * @param on 输出是否已开（命令状态）。
 * @param ready 输出是否可控（点位已配且模块在线）。
 * @param text 说明（未配置点位/模块离线…）。
 */
void HsfMonitorFlowMeter_setPump(const char* name, bool on, bool ready,
                                  const char* text)
{
    HsfFmPage* p = s_fmPage;
    char buf[128];

    if (!p) return;
    if (!ready) {
        snprintf(buf, sizeof(buf), "%s  %s",
                 (name && name[0]) ? name : "进水泵",
                 (text && text[0]) ? text : "不可控");
        XLabel_setText_2(&p->m_pumpState, buf);
    } else if (on) {
        snprintf(buf, sizeof(buf), "%s  运行（输出已开）",
                 (name && name[0]) ? name : "进水泵");
        XLabel_setText_2(&p->m_pumpState, buf);
    } else {
        snprintf(buf, sizeof(buf), "%s  停止（输出已关）",
                 (name && name[0]) ? name : "进水泵");
        XLabel_setText_2(&p->m_pumpState, buf);
    }
    /* [幂等置灰] 已生效的一侧按钮禁用（对齐 Python 客户端：已启动则"启动"
     * 置灰、已停止则"停止"置灰）。 */
    XWidget_setEnabled((XWidget*)&p->m_pumpStart, ready && !on);
    XWidget_setEnabled((XWidget*)&p->m_pumpStop, ready && on);
}

/**
 * @brief 注册泵控制出口（on=true 启动 / false 停止）。
 */
void HsfMonitorFlowMeter_setPumpWriteHandler(void (*fn)(void*, bool),
                                             void* userData)
{
    HsfFmPage* p = s_fmPage;
    if (!p) return;
    p->m_pumpWrite = fn;
    p->m_pumpUser = userData;
}
