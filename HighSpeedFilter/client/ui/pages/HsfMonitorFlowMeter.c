/**
 * @file       HsfMonitorFlowMeter.c
 * @brief      电磁流量计监视页实现：状态横幅 + 主读数 + 弧形仪表 + 数值卡 + 趋势。
 * @details    [层次]（TopToBottom）
 *               1) 状态横幅（自绘胶囊：状态色块 + 大字状态 + 报警摘要）
 *               2) 主读数区（大号瞬时流量 + 单位；副标题为流速）
 *               3) 仪表行（两枚 270° 弧形仪表：流量占量程比 / 信号强度；
 *                  主读数并入行首同排）
 *               4) 数值卡行（1 行 4 卡：流速 / 正向累积 / 反向累积 / 报警详情）
 *               5) 水质块（浊度仪两路 + 进水泵状态/启停，单行平铺）
 *               6) 趋势图（XChartView+XLineSeries 瞬时流量 60 点滚动窗；
 *                  纵向 Expanding 吸收页余量贴底）
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
#include "HsfSettingsStore.h"
#include "HsfUi.h"
#include "HsfPages.h"
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
#include "XImage.h"
#include "XSystem.h"     /* XSystem_environment：泵点击诊断开关（XHSF_PUMP_TRACE）。 */
#include "XFont.h"
#include "XPainter.h"
#include "XString.h"
#include "XDateTime.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief 本机时区偏移（epoch ↔ 本地日历互转用；算法同历史页 histTzOffsetMs）。
 * @note  [2026-11-11] 归整分钟抹掉两次采样相减的 ±1ms 噪声（整秒时刻
 *         少 1 秒 / 图表 X 轴标签跳秒），口径与 HsfLinkHistory.c 一致。
 */
static int64_t fmTzOffsetMs(void)
{
    XDateTime now;
    int64_t raw;
    int64_t half;
    now = XDateTime_currentDateTime();
    raw = XDateTime_toMSecsSinceEpoch(&now) -
          XDateTime_currentWallMSecsSinceEpoch();
    half = (raw >= 0) ? 30000LL : -30000LL;
    return ((raw + half) / 60000LL) * 60000LL;
}

/* ==================== 常量与配色 ==================== */

/* [侧号不再由页面指定 2026-11-10] 流量计挂在哪条 485 上是**服务端的
 * 装配事实**（现场：B 侧 / COM18 / 站号 5）：服务端按装配位置回答并在
 * 帧里带 side，客户端据此知道活动侧。早先这里写死 FM_SIDE=1 ⇒ 现场
 * 换接线就得改客户端；现在页面只管要数据。 */
#define FM_BANNER_H          44
#define FM_MAIN_H            84
#define FM_GAUGE_H           132
#define FM_CARD_H            54
#define FM_TREND_H           148
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

/* [随主题取套 2026-11-11 双主题走查] 表盘自绘面/进度弧按当前主题取
 * 亮/深套：亮套=上表现行值零改动；深套=面板 #23282F + 软描边 #39414C
 * （HsfTheme.h 深色令牌）——原白盘常量在深色页面上成白块、盘内「--」
 * 弱字（调色板/弱色）落白盘不可读。刻度锚点用次级文本深套保可读。 */
static uint32_t fmThemeClr(uint32_t light, uint32_t dark)
{
    return HsfTheme_isDarkSkin() ? dark : light;
}

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
    /* [静态层缓存 2026-11-18] 表盘上只有**进度带**随值变化；底盘圆、底轨
     * 带与两端刻度对给定尺寸是常量。此前每帧重画全部：底盘 =
     * 2π·r·4 ≈ 2500 条 AA 径向线 + 底轨 ~1900 条，两只表合计一帧
     * **229ms**（探针实测，离屏）⇒ 切到本页「卡顿一会儿才打开」。
     * 与变频器页同款做法（2026-11-08，见 HsfMonitorVfd.c vfdGaugeRenderStatic）：
     * 静态部分渲染到离屏图一次，每帧只 blit + 画进度带。 */
    XImage*  m_staticCache; /**< 静态层（NULL=未建；随画布析构释放）。 */
    int      m_cacheW;      /**< 缓存宽（0=失效）。 */
    int      m_cacheH;      /**< 缓存高。 */
    bool     m_cacheDark;   /**< 缓存渲染时的主题（与当前主题不符即重建）。 */
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
    XGridLayout m_waterGrid;            /**< 水质块网格（4 列 × 2 行，2026-11-11 改）。 */
    XLabel     m_turbTitle;             /**< 「浊度仪」小标题。 */
    XLabel     m_turbValue[HSF_TURBIDITY_COUNT]; /**< 各台浊度值行（名称 + 值）。 */
    XLabel     m_pumpTitle;             /**< 「进水泵」小标题。 */
    XLabel     m_pumpState;             /**< 泵状态行。 */
    XPushButton m_pumpStart;            /**< 启动泵。 */
    XPushButton m_pumpStop;             /**< 停止泵。 */
    XLabel     m_pumpNote;              /**< 泵说明（无运行反馈点提示）。 */
    void (*m_pumpWrite)(void*, bool);  /**< 泵控制出口（on=true 启动）。 */
    void* m_pumpUser;                  /**< 泵出口令牌。 */
    bool       m_ctrlEnabled;          /**< 外部控制闸（false=按钮全灰）。 */
    char       m_ctrlReason[96];       /**< 闸门原因（页上说明）。 */
    bool       m_pumpOn;               /**< 最近一拍的泵命令状态。 */
    bool       m_pumpReady;            /**< 最近一拍的泵点位就绪。 */

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

/**
 * @brief 趋势图 X 轴刻度标签：把「相对建页时刻的秒数」渲染成本地 时:分:秒。
 * @details [口径 2026-11-10 与变频器页统一] 本页 X 轴原用缺省 "%g"（图上只
 *          是 0/6/12 这样的秒数，读不出时刻）。数据点 X =
 *          (now - m_trendT0)/1000，反向换算 m_trendT0 + value*1000 即该刻墙钟
 *          毫秒，再补本地时区偏移展开成日历。
 * @note 定义在页结构体之后（要读 m_trendT0）。
 */
static void fmTrendXLabelText(const XValueAxis* axis, double value, char* buf,
                              int cap, void* userData)
{
    HsfFmPage* p = (HsfFmPage*)userData;
    XDateTime dt;
    XTime t;
    int64_t ms;

    (void)axis;
    if (!buf || cap <= 0) return;
    buf[0] = '\0';
    if (!p || p->m_trendT0 == 0) return;
    ms = (int64_t)p->m_trendT0 + (int64_t)(value * 1000.0) + fmTzOffsetMs();
    if (!XDateTime_setMSecsSinceEpoch(&dt, ms)) return;
    t = XDateTime_time(&dt);
    snprintf(buf, (size_t)cap, "%02d:%02d:%02d",
             XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
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
    XPainterPath path;
    int i;
    int segments;
    double a0 = startDeg * 3.14159265358979323846 / 180.0;
    double sweepRad = sweepDeg * 3.14159265358979323846 / 180.0;
    double step;
    if (!painter) return;
    if (rOuter < 1 || sweepDeg == 0.0 || rOuter <= rInner) return;
    /* [2026-11-18 性能根修，对齐变频器页同款] 原实现是「逐角度画数千条
     * AA 径向线」——拿"线"模拟"面"：每条 AA 线各自走一次覆盖率光栅化
     * （各建一张 4×4 子采样的临时覆盖图），代价随周长线性堆叠。实测：
     * 底盘 ~2500 条 + 底轨 ~1900 条 ⇒ 水质/流量计页单帧 229ms、首次显形
     * 静态层构建 ~130ms ⇒ 切到本页「卡顿一会儿才打开」。
     * 改为**一条闭合路径填充**（arcTo 外弧 → 内弧反向 → closeSubpath →
     * fillPath）：一次覆盖图光栅化整块扇环；弧点按 1px 弦长取样（拐点数
     * 与周长同阶，但只光栅化一次），圆缘的 1px 羽化由覆盖图自身给出。
     * 与变频器页 HsfMonitorVfd.c 的 vfdFillArcBand 同款实现、同款口径。 */
    segments = (int)(2.0 * 3.14159265358979323846 * rOuter *
                     ((sweepDeg > 0 ? sweepDeg : -sweepDeg) / 360.0) * 1.0);
    if (segments < 24) segments = 24;
    if (segments > 720) segments = 720;
    step = sweepRad / (double)segments;
    XPainterPath_init(&path);
    XPainterPath_setFillRule(&path, XPainterFillRule_OddEven);
    /* 外弧（起点 → 终点）。 */
    XPainterPath_moveTo(&path,
                        (float)(cx + rOuter * cos(a0)),
                        (float)(cy - rOuter * sin(a0)));
    for (i = 1; i <= segments; ++i) {
        double a = a0 + step * (double)i;
        XPainterPath_lineTo(&path,
                            (float)(cx + rOuter * cos(a)),
                            (float)(cy - rOuter * sin(a)));
    }
    /* 内弧（终点 → 起点，反向闭合出扇环）。 */
    for (i = segments; i >= 0; --i) {
        double a = a0 + step * (double)i;
        XPainterPath_lineTo(&path,
                            (float)(cx + rInner * cos(a)),
                            (float)(cy - rInner * sin(a)));
    }
    XPainterPath_closeSubpath(&path);
    /* 填充用画刷（弧带的"面色"）；描边不在本函数职责内——圆缘的 1px
     * 羽化由覆盖图自身给出（与变频器页同口径）。 */
    XPainter_fillPath(painter, &path);
    XPainterPath_deinit(&path);
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

/** @brief 仪表析构：释放静态层缓存（画布随页级联析构时走到这里）。 */
static void VHsfFmGauge_deinit(XObject* obj)
{
    HsfFmGauge* g = (HsfFmGauge*)obj;
    if (g && g->m_staticCache) {
        XClassDelete((XClass*)g->m_staticCache);
        g->m_staticCache = NULL;
    }
    XClass_Deinit_Parent(XWidget, obj);
}

static XVtable* fmGaugeClassInit(void)
{
    XVTABLE_INIT_DEFAULT(HsfFmGauge)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfFmGauge_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfFmGauge_deinit);
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
 * @brief 把表盘**静态部分**（底盘圆 + 底轨带）渲染进缓存。
 * @details 两端刻度只 2 条线、成本可忽略，且要与进度带保持层序（刻度在上），
 *          故留在每帧路径；静态层只缓存两块「大件」（底盘 ≈2500 条 AA 线）。
 * @details 静态层只与尺寸/主题有关；进度带单独每帧画。缓存图用 ARGB32
 *          且背景全透明：blit 到画布上时圆外区域保持透明（页面底色透出），
 *          与原先"直接在画布上画"的合成结果一致（变频器页同款做法）。
 */
static void fmGaugeRenderStatic(HsfFmGauge* g, int w, int h,
                                int cx, int cy, int r,
                                int bandOuter, int bandInner)
{
    XPainter painter;
    if (!g) return;
    if (!g->m_staticCache) {
        g->m_staticCache = XImage_create();
        if (!g->m_staticCache) return;
    }
    if (g->m_cacheW != w || g->m_cacheH != h) {
        XImage_reinit_ex(g->m_staticCache, w, h, XImageFormat_ARGB32);
        g->m_cacheW = w;
        g->m_cacheH = h;
    }
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, g->m_staticCache)) {
        XPainter_deinit(&painter);
        return;
    }
    /* [底透明] 图内容未定义 → 先以 Source 合成模式整幅写 0（全透明）。
     * 不能用普通 fillRect：默认 SourceOver 下 alpha=0 的源不改变目标，
     * 未初始化像素会被保留（变频器页同款做法）。 */
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Source);
    XPainter_fillRect(&painter,
                      (const XRect*)&(XRect){0, 0, w, h}, 0x00000000u);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_SourceOver);
    XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);

    /* ① 底盘（浅色填充，与卡片同底；深套翻面板色，见 fmThemeClr 注）。
     * [底盘改「填充椭圆」2026-11-18] 原实现是 2π·r·4 ≈ 2500 条 AA 径向线
     * （圆心→圆缘，合计 ~25 万像素；单表静态层 ≈ 60ms ⇒ 切到本页第一次
     * 要付 ~130ms）。改为「一次扫描线填充（O(面积) ≈ 3 万 px）+ 32 段
     * AA 描边」后成本降两个数量级，视觉差异仅在圆缘 1px 内（描边覆盖）。 */
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter, fmThemeClr(0xFFF7F9FB, HSF_COLOR_PANEL_DARK));
    XPainter_setPen(&painter, fmThemeClr(0xFFF7F9FB, HSF_COLOR_PANEL_DARK));
    XPainter_drawEllipse_2(&painter, (float)cx, (float)cy, (float)r, (float)r);

    /* ② 底轨（整弧，浅灰）。[弧带改路径填充 ⇒ 上色走画刷，见 fmDrawArcBand 注] */
    XPainter_setBrush(&painter,
                    fmThemeClr(FM_COLOR_TRACK, HSF_COLOR_SOFT_BORDER_DARK));
    fmDrawArcBand(&painter, cx, cy, bandInner, bandOuter, 135.0, -270.0);

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    /* 缓存键补主题维（只在成功渲染后记账）：主题切换只触发重绘不触发
     * resize，旧键（仅尺寸）会让亮盘缓存残留到深色页（变频器页同款）。 */
    g->m_cacheDark = HsfTheme_isDarkSkin();
}

/**
 * @brief 仪表 paintEvent：静态层 blit + 进度带（每帧只画随值变化的部分）。
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

    w = XWidget_width(self);
    h = XWidget_height(self);
    if (w <= 8 || h <= 8) return;

    /* 弧心略偏下：给弧内数值留出上方空间。 */
    cx = w / 2;
    cy = (int)(h * 0.58f);
    r = ((w < h * 5 / 4) ? (w / 2) : (h / 2)) - 12;
    if (r < 20) r = 20;
    bandInner = r - 9;
    bandOuter = r;

    /* [静态层缓存 2026-11-18] 按「尺寸+主题」惰性重建（两者都不变才复用）。 */
    if (g->m_cacheW != w || g->m_cacheH != h ||
        g->m_cacheDark != HsfTheme_isDarkSkin())
        fmGaugeRenderStatic(g, w, h, cx, cy, r, bandOuter, bandInner);

    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
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

    ratio = fmGaugeRatio(g->m_owner, g->m_slot);
    /* [进度弧随主题 2026-11-11 双主题走查] 第一枚品牌蓝 #1F4E79 落深
     * 面板仅 1.72:1（非文本件 <3，弧几乎不可见）——对齐变频器页同位
     * 口径（vfdGaugePaint 的 arcColor）翻深套强调蓝 #4DA3FF（vs 深面板
     * 5.65 ≥3）。第二枚青 #0E8F8F vs 深面板 3.77 / vs 白 3.93 双 ≥3
     * 达标，两主题同值保留（区分两枚仪表的色彩语义）。 */
    color = (g->m_slot == 0)
        ? fmThemeClr(FM_COLOR_GAUGE, HSF_COLOR_ACCENT_DARK)
        : FM_COLOR_GAUGE_2;

    /* 静态层 blit（圆外透明，页面底色透出）。 */
    if (g->m_staticCache)
        XPainter_drawImage(&painter, g->m_staticCache, 0, 0);

    /* ③ 进度带（按比例；从起点顺时针）。 */
    if (ratio > 0.001) {
        XPainter_setPenWidth(&painter, 1);
        XPainter_setBrush(&painter, color);
        fmDrawArcBand(&painter, cx, cy, bandInner - 1, bandOuter + 1,
                      135.0, -270.0 * ratio);
    }

    /* ④ 两端小刻度（视觉锚点）：画在进度带**之上**（与原实现同层序——
     * 原实现在同一趟里先带后刻度；这里刻度留在每帧路径，静态层只缓存
     * 底盘与底轨这两块「大件」）。 */
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
            XPainter_setPen(&painter,
                            fmThemeClr(FM_COLOR_TEXT_DIM,
                                       HSF_COLOR_TEXT_SECONDARY_DARK));
            XPainter_drawLine_3(&painter, x0, y0, x1, y1);
        }
    }

    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/**
 * @brief 横幅 paintEvent：圆角胶囊底色（状态文字由子 XLabel 承载）。
 * @details [随主题取套 2026-11-11 双主题走查] 亮套维持实心状态色胶囊
 *          （原口径零改动）。深套改「面板底 + 状态色描边」：深色盘的
 *          横幅文字走调色板 WindowText #E8EAED，落在亮灰/中亮状态色
 *          实心底上只有 1.9~2.5:1（走查实锅，#E8EAED×#8A98A6 ≈2.45）；
 *          面板 #23282F 上文字 12.3:1，状态语义由描边承载（深套状态色
 *          vs 面板 3.78~5.71，均 ≥3 非文本件档，登记见 HsfTheme.h），
 *          且横幅文字本身即状态文案（双通道）。
 */
static void VHsfFmBanner_paintEvent(XWidget* self, XEvent* event)
{
    HsfFmBanner* b = (HsfFmBanner*)self;
    XPainter painter;
    XImage* image;
    XRect rect;
    uint32_t fill;
    uint32_t edge;
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

        if (!b->m_owner->m_snap.m_valid) {
            fill = fmThemeClr(FM_COLOR_IDLE,
                              HSF_COLOR_PANEL_DARK);
            edge = fmThemeClr(FM_COLOR_IDLE,
                              HSF_COLOR_STATE_OFFLINE_DARK);
        } else if (b->m_owner->m_snap.m_alarms != 0) {
            fill = fmThemeClr(FM_COLOR_ALARM,
                              HSF_COLOR_PANEL_DARK);
            edge = fmThemeClr(FM_COLOR_ALARM,
                              HSF_COLOR_STATE_FAULT_FILL_DARK);
        } else {
            fill = fmThemeClr(FM_COLOR_OK,
                              HSF_COLOR_PANEL_DARK);
            edge = fmThemeClr(FM_COLOR_OK,
                              HSF_COLOR_STATE_OK_DARK);
        }

        rect.x = 0;
        rect.y = 0;
        rect.width = w;
        rect.height = h;
        XPainter_setBrush(&painter, fill);
        XPainter_setPen(&painter, edge);
        XPainter_setPenWidth(&painter, 1);
        XPainter_drawRoundedRect(&painter, &rect, radius, radius);
    }

    XPainter_deinit(&painter);
}

/* ==================== 控件构建助手 ==================== */

/**
 * @brief 页字体缩放折算（本地设置 FontScale/Page6，50–200%）。
 * @details 字号与配套高度（横幅/仪表/卡/水质块/趋势最小高、仪表内部
 *          排布）统一走它：字号放大时高度同步放大，避免文字被裁。
 *          改动在下次建页生效（与刷新周期同口径）。
 */
static int fmH(int px)
{
    return hsfUi_pageFontPx(HSF_PAGE_MONITOR_FLOWMETER, px);
}

/** @brief 统一字体+像素尺寸设置（字号按页缩放折算）。 */
static void fmSetFont(XWidget* w, int px, bool bold, bool dim)
{
    XFont f;
    HsfTheme_applyDefaultFont(w);
    f = XWidget_font(w);
    XFont_setBold(&f, bold);
    XWidget_setFont(w, &f);
    XClassDeinit(&f);
    XLabel_setTextPixelSize((XLabel*)w, fmH(px));
    if (dim)
        HsfTheme_applySecondaryText(w);
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
    XWidget_setFixedHeight(&g->m_canvas, fmH(FM_GAUGE_H));
    XWidget_setMinimumWidth(&g->m_canvas, FM_GAUGE_MIN_W);
    /* 水平 Expanding：不声明时布局按 sizeHint(0) 给宽，画布宽度 0 →
     * 弧画不出来、三个子标签全挤在左上角。 */
    XWidget_setSizePolicy(&g->m_canvas, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Fixed);

    fmMakeLabel(&g->m_title, &g->m_canvas, title, 20, false, true,
                XAlignment_HCenter | XAlignment_VCenter);
    fmMakeLabel(&g->m_value, &g->m_canvas, "--", 28, true, false,
                XAlignment_HCenter | XAlignment_VCenter);
    fmMakeLabel(&g->m_unit, &g->m_canvas, unit, 16, false, true,
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
    if (h <= 0) h = fmH(FM_GAUGE_H);
    cx = w / 2;
    /* 标签盒随页字体缩放折算（字号放大时同步放大，避免裁字）。 */
    XWidget_move((XWidget*)&g->m_title, cx - fmH(50), 6);
    XWidget_resize((XWidget*)&g->m_title, fmH(100), fmH(20));
    /* 数值放在弧心稍上（弧心在 58% 高度）。 */
    vTop = (int)(h * 0.58f) - fmH(24);
    XWidget_move((XWidget*)&g->m_value, cx - fmH(56), vTop);
    XWidget_resize((XWidget*)&g->m_value, fmH(112), fmH(30));
    XWidget_move((XWidget*)&g->m_unit, cx - fmH(44), vTop + fmH(30));
    XWidget_resize((XWidget*)&g->m_unit, fmH(88), fmH(18));
}

/** @brief 数值卡初始化（两行文本：名称 + 值）。 */
static void fmMakeCard(XLabel* card, XWidget* parent, const char* text)
{
    XLabel_init(card, parent, 0);
    fmSetFont((XWidget*)card, 20, false, false);
    XLabel_setText_2(card, text);
    XLabel_setAlignment(card, XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setMinimumHeight((XWidget*)card, fmH(FM_CARD_H));
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
    /* [泵点击诊断 XHSF_PUMP_TRACE] 点击链路静默失败时没有任何痕迹，现场
     * 「点了没反应」无从定位。开关式日志把三环（槽进来 / 出口已注册 /
     * 出口被调用）各打一行，能一眼区分「信号没连上」「回调没注册」
     * 「回调里发了但服务端没响应」。 */
    if (XSystem_environment("XHSF_PUMP_TRACE"))
        XPrintf("[pump] start slot: page=%p write=%p user=%p\n",
                (void*)p, p ? (void*)p->m_pumpWrite : NULL,
                p ? p->m_pumpUser : NULL);
    if (!p || !p->m_pumpWrite) return;
    p->m_pumpWrite(p->m_pumpUser, true);
}

static void fmPumpStopSlot(XObject* receiver, XVarList* args)
{
    HsfFmPage* p = (HsfFmPage*)((char*)receiver -
                                 offsetof(HsfFmPage, m_pumpStop));
    (void)args;
    if (XSystem_environment("XHSF_PUMP_TRACE"))
        XPrintf("[pump] stop slot: page=%p write=%p user=%p\n",
                (void*)p, p ? (void*)p->m_pumpWrite : NULL,
                p ? p->m_pumpUser : NULL);
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
    /* [2026-11-07 单行平铺] 浊度仪两路 + 进水泵状态/按钮 全平铺一行，
     * 卡片化（Base 底）与数值卡统一观感。
     * [改两行网格 2026-11-11 根修] 单行平铺在**字号放大后必然溢出**：
     * 全局缩放 140% × 页缩放 140% = 1.96 倍时，这一行的需要宽度
     * （浊度仪标题 + 两个「浊度仪N x.xx NTU xx.x℃」+ 进水泵 + 状态 +
     * 两个按钮）已超过视口宽 ⇒ 右侧被裁（现场截图：右端「停止（停…」
     * 断掉），滚动到底也补不回来（横向滚动是关的）。
     * 改成 4 列 × 2 行网格：第 0 行放浊度仪标题与两台读数，第 1 行放
     * 进水泵标题/状态/启停按钮 ⇒ 需要宽度降到最宽的一行，且随字号
     * 放大仍有裕度。 */
    XGridLayout_init(&p->m_waterGrid);
    XLayout_setSpacing((XLayout*)&p->m_waterGrid, 12);
    /* [2026-11-08 退出段错误根修] 边距属于**布局**，不是控件（曾误把
     * 控件当布局强转，把 16 字节 XMargins 写到控件对象上、覆盖了两个
     * XVector*，退出时必现 AV）。此处布局已换成网格，边距同样给布局。 */
    XLayout_setContentsMargins((XLayout*)&p->m_waterGrid, 12, 6, 12, 6);
    XWidget_setLayout(&p->m_waterHost, (XLayout*)&p->m_waterGrid);
    /* [横向撑满 2026-11-11 根修] 水质块此前未声明横向策略 ⇒ 按内容
     * sizeHint 给宽；而 sizeHint 是**建页时**算的（浊度值还是「--」，
     * 很短）⇒ 块宽只有 ~580px。运行期文本变长（「出水浊度仪 0.04 NTU
     * 19.35℃」）后超出块宽被裁（现场截图右端断字）。声明 Expanding 后
     * 块宽=页宽，网格列宽按可用宽分配，长文本放得下。 */
    XWidget_setSizePolicy((XWidget*)&p->m_waterHost,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    XWidget_setBackgroundRole((XWidget*)&p->m_waterHost, XPaletteColorRole_Base);
    XWidget_setAutoFillBackground((XWidget*)&p->m_waterHost, true);
    /* 两行网格：高度交给布局算（固定高度会裁字），只给最小高。 */
    XWidget_setMinimumHeight((XWidget*)&p->m_waterHost, fmH(90));

    fmMakeLabel(&p->m_turbTitle, &p->m_waterHost,
                "\xE6\xB5\x8A\xE5\xBA\xA6\xE4\xBB\xAA",  /* 浊度仪 */
                20, true, false, XAlignment_Left | XAlignment_VCenter);
    XGridLayout_addWidget(&p->m_waterGrid, (XWidget*)&p->m_turbTitle, 0, 0, 0);
    for (i = 0; i < HSF_TURBIDITY_COUNT; ++i) {
        fmMakeLabel(&p->m_turbValue[i], &p->m_waterHost, "--", 20, false, false,
                    XAlignment_Left | XAlignment_VCenter);
        XGridLayout_addWidget(&p->m_waterGrid, (XWidget*)&p->m_turbValue[i],
                               0, i + 1, 0);
    }
    fmMakeLabel(&p->m_pumpTitle, &p->m_waterHost,
                "\xE8\xBF\x9B\xE6\xB0\xB4\xE6\xB3\xB5",  /* 进水泵 */
                20, true, false, XAlignment_Left | XAlignment_VCenter);
    XGridLayout_addWidget(&p->m_waterGrid, (XWidget*)&p->m_pumpTitle, 1, 0, 0);
    fmMakeLabel(&p->m_pumpState, &p->m_waterHost, "--", 20, false, false,
                XAlignment_Left | XAlignment_VCenter);
    XGridLayout_addWidget(&p->m_waterGrid, (XWidget*)&p->m_pumpState, 1, 1, 0);
    XPushButton_init(&p->m_pumpStart, &p->m_waterHost, 0);
    XPushButton_init(&p->m_pumpStop, &p->m_waterHost, 0);
    XPushButton_setText_2(&p->m_pumpStart,
                        "\xE5\x90\xAF\xE5\x8A\xA8");  /* 启动 */
    XPushButton_setText_2(&p->m_pumpStop,
                        "\xE5\x81\x9C\xE6\xAD\xA2");  /* 停止 */
    XWidget_setFixedSize((XWidget*)&p->m_pumpStart, fmH(88), fmH(32));
    XWidget_setFixedSize((XWidget*)&p->m_pumpStop, fmH(88), fmH(32));
    XObject_connect((XObject*)&p->m_pumpStart,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&p->m_pumpStart, fmPumpStartSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)&p->m_pumpStop,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&p->m_pumpStop, fmPumpStopSlot,
                      XConnectionType_Direct);
    XGridLayout_addWidget(&p->m_waterGrid, (XWidget*)&p->m_pumpStart, 1, 2, 0);
    XGridLayout_addWidget(&p->m_waterGrid, (XWidget*)&p->m_pumpStop, 1, 3, 0);
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
    XLayout_setContentsMargins((XLayout*)&p->m_mainLayout, 20, 10, 20, 10);
    XLayout_setSpacing((XLayout*)&p->m_mainLayout, 7);

    /* ---- ① 状态横幅 ---- */
    {
        XWidget_init(&p->m_banner.m_base, &p->m_root, 0);
        XClassGetVtable(&p->m_banner.m_base) = fmBannerClassInit();
        p->m_banner.m_owner = p;
        HsfTheme_applyDefaultFont(&p->m_banner.m_base);
        XWidget_setFixedHeight(&p->m_banner.m_base, fmH(FM_BANNER_H));
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
    /* [2026-11-07 并入仪表行] 主读数与两枚仪表同排，高度随仪表行，
     * 内容加前后 stretch 垂直居中。 */
    XWidget_setFixedHeight(&p->m_mainHost, fmH(FM_GAUGE_H));
    XWidget_setSizePolicy(&p->m_mainHost, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    fmMakeLabel(&p->m_mainValue, &p->m_mainHost, "--", 56, true, false,
                XAlignment_HCenter | XAlignment_VCenter);
    fmMakeLabel(&p->m_mainSub, &p->m_mainHost, "", 22, false, true,
                XAlignment_HCenter | XAlignment_VCenter);
    /* 两个标签都要登记进容器布局；只 init 挂成子控件的话它们不受布局
     * 管理，各自按 sizeHint 停在容器原点 —— 大号数字会溢出压住横幅。 */
    XBoxLayout_addStretch(&p->m_mainHostLayout, 1);
    XBoxLayout_addWidget(&p->m_mainHostLayout, (XWidget*)&p->m_mainValue);
    XBoxLayout_addWidget(&p->m_mainHostLayout, (XWidget*)&p->m_mainSub);
    XBoxLayout_addStretch(&p->m_mainHostLayout, 1);
    XWidget_show(&p->m_mainHost);

    /* ---- ③ 仪表行（主读数在行首，stretch 读数 2 / 仪表 1） ---- */
    XWidget_init(&p->m_gaugeRow, &p->m_root, 0);
    XBoxLayout_init(&p->m_gaugeRowLayout, XBoxLayoutDirection_LeftToRight);
    XWidget_setLayout(&p->m_gaugeRow, (XLayout*)&p->m_gaugeRowLayout);
    XLayout_setSpacing((XLayout*)&p->m_gaugeRowLayout, 12);
    fmGaugeInit(&p->m_gaugeFlow, p, &p->m_gaugeRow, 0, FM_TXT_RATIO, "");
    fmGaugeInit(&p->m_gaugeSignal, p, &p->m_gaugeRow, 1, FM_TXT_SIGNAL, "");
    /* [关键] 画布必须**登记进布局**：只 XWidget_init 挂成子控件而不
     * addWidget，布局不知道它的存在 —— 控件永远停在 sizeHint 大小，
     * 弧画不出来、三个子标签挤在左上角（本页首版实锅）。 */
    XBoxLayout_addWidget(&p->m_gaugeRowLayout, &p->m_mainHost);
    XBoxLayout_addWidget(&p->m_gaugeRowLayout,
                         &p->m_gaugeFlow.m_canvas);
    XBoxLayout_addWidget(&p->m_gaugeRowLayout,
                         &p->m_gaugeSignal.m_canvas);
    XBoxLayout_setStretch(&p->m_gaugeRowLayout, 0, 2);
    XBoxLayout_setStretch(&p->m_gaugeRowLayout, 1, 1);
    XBoxLayout_setStretch(&p->m_gaugeRowLayout, 2, 1);
    XWidget_setSizePolicy(&p->m_gaugeRow, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    XWidget_show(&p->m_gaugeRow);

    /* ---- ④ 数值卡行（2×2） ---- */
    XWidget_init(&p->m_cardHost, &p->m_root, 0);
    XGridLayout_init(&p->m_cardGrid);
    XWidget_setLayout(&p->m_cardHost, (XLayout*)&p->m_cardGrid);
    XWidget_setSizePolicy((XWidget*)&p->m_cardHost, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    fmMakeCard(&p->m_cardSpeed, &p->m_cardHost, FM_TXT_SPEED "\n--");
    fmMakeCard(&p->m_cardForward, &p->m_cardHost, FM_TXT_FWD_TOTAL "\n--");
    fmMakeCard(&p->m_cardReverse, &p->m_cardHost, FM_TXT_REV_TOTAL "\n--");
    fmMakeCard(&p->m_cardAlarm, &p->m_cardHost, FM_TXT_ALARM "\n--");
    /* [真 2×2 排布 2026-11-11 根修] 注释一直写「2×2 网格」，代码却把四张卡
       全塞在第 0 行（1×4）⇒ 每卡只分到 1/4 视口宽，字号放大后卡内文本
       （如「正向累计 / 0.00 M3」）超出卡片框 ⇒ **相邻卡片文字叠字**
       （现场截图：「0.0」与「00 M3」重叠）。改成真 2×2：每卡拿到约一半
       宽度，字号放大也放得下。 */
    XGridLayout_addWidget(&p->m_cardGrid, (XWidget*)&p->m_cardSpeed, 0, 0, 0);
    XGridLayout_addWidget(&p->m_cardGrid, (XWidget*)&p->m_cardForward, 0, 1, 0);
    XGridLayout_addWidget(&p->m_cardGrid, (XWidget*)&p->m_cardReverse, 1, 0, 0);
    XGridLayout_addWidget(&p->m_cardGrid, (XWidget*)&p->m_cardAlarm, 1, 1, 0);
    XWidget_show(&p->m_cardHost);

    /* ---- ⑤ 趋势图 ---- */
    XChartView_init(&p->m_trendView, &p->m_root, 0);
    /* [贴底] 纵向 Expanding：主布局余量全部归图，图撑到页底
     * （不再尾部 stretch 沉底留白）。横向同 Expanding 随页宽。 */
    XWidget_setSizePolicy(&p->m_trendView.m_base,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
    XWidget_setMinimumHeight(&p->m_trendView.m_base, fmH(FM_TREND_H));
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
        /* [X 轴显示时钟时间 2026-11-10] 轴值是"相对建页时刻的秒数"，标签
         * 渲染成本地 时:分:秒（与变频器页同口径；缺省 "%g" 只有秒数，现场
         * 读不出是几点几分）。 */
        if (p->m_axisX) {
            XValueAxis_setLabelTextCallback(p->m_axisX, fmTrendXLabelText, p);
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

    /* [2026-11-07 删除] 底注 label 整个移除（fmMakeLabel 自带 show，
     * 只摘 addWidget 会残留在 0,0 悬浮——实测盖住横幅左上角）。 */

    /* ---- 把主要区块挂进主布局 ---- */
    XBoxLayout_addWidget(&p->m_mainLayout, (XWidget*)&p->m_banner.m_base);
    XBoxLayout_addWidget(&p->m_mainLayout, &p->m_gaugeRow);
    XBoxLayout_addWidget(&p->m_mainLayout, &p->m_cardHost);
    /* 水质块（浊度 + 泵）：与流量同属一条工艺链，挪到趋势图上方。 */
    fmBuildWaterBlock(p);
    XBoxLayout_addWidget(&p->m_mainLayout, &p->m_waterHost);
    /* 趋势图收尾（最后一件，纵向 Expanding 吸收全部余量贴底）：
     * [2026-11-07 改] 原尾部 addStretch 把余量沉到图下方——图底与页底
     * 之间留一大条空白（用户实拍）。去掉 stretch 后余量归图本身；
     * 仪表行纵向是 Minimum 不会被撑高，行距不受影响。 */
    XBoxLayout_addWidget(&p->m_mainLayout, (XWidget*)&p->m_trendView.m_base);

    XWidget_show(&p->m_root);

    /* ---- 接数据源 ---- */
    p->m_prov = HsfFlowMeterProvider_remote();
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
     * 竖向留白 = 区块间距(7) + 上下边距(10+10)；[2026-11-07] 主读数并入
     * 仪表行、卡 1 行、底注移除后重算。 */
    XWidget_setMinimumHeight(&p->m_root,
                             fmH(FM_BANNER_H) + fmH(FM_GAUGE_H) +
                             /* 卡片行改真 2×2（2026-11-11）⇒ 两行高 */
                             (fmH(FM_CARD_H) * 2 + 8) + fmH(90) +
                             fmH(FM_TREND_H) + 6 * 7 + 10 + 10);

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

    XLabel_setTextPixelSize(&p->m_mainValue, fmH(compact ? 44 : 56));
    XLabel_setTextPixelSize(&p->m_bannerLabel, fmH(compact ? 15 : 17));
    XWidget_setMinimumHeight(&p->m_trendView.m_base,
                             fmH(compact ? 120 : FM_TREND_H));

    /* 页根最小高度随形态重算：竖屏时仪表改纵排（两枚 132 高 → 264），
     * 趋势图压到 120，其余不变。滚动壳按此撑出滚范围，矮视口下才能
     * 滚到全部内容。[2026-11-07] 主读数并入仪表行、卡 1 行、底注移除；
     * 字号/高度统一过 fmH 页缩放折算。 */
    if (compact) {
        XWidget_setMinimumHeight(&p->m_root,
                                 fmH(FM_BANNER_H) + fmH(FM_GAUGE_H) * 2 +
                                 (fmH(FM_CARD_H) * 2 + 8) + fmH(90) +
                                 fmH(120) + 6 * 7 + 10 + 10);
    } else {
        XWidget_setMinimumHeight(&p->m_root,
                                 fmH(FM_BANNER_H) + fmH(FM_GAUGE_H) +
                                 (fmH(FM_CARD_H) * 2 + 8) + fmH(90) +
                                 fmH(FM_TREND_H) + 6 * 7 + 10 + 10);
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
 * @param hasTemp 该台是否带温度（2026-11-10：站3 ZHEYI-5100ZDY 一帧
 *                0x0000×4 同刻带出；站1 WZT-701A 无温度）。
 * @param tempC 水温（℃ 原值；hasTemp=false 时无意义）。
 * @note  ≥20 NTU 标红（现场水质告警线，对齐 Python 客户端口径）。
 */
void HsfMonitorFlowMeter_setTurbidity(int index, const char* name,
                                      double ntu, bool online,
                                      bool hasTemp, double tempC)
{
    HsfFmPage* p = s_fmPage;
    char buf[96];

    if (!p || index < 0 || index >= HSF_TURBIDITY_COUNT) return;
    if (!online) {
        snprintf(buf, sizeof(buf), "%s  --",
                 (name && name[0]) ? name : "浊度仪");
        XLabel_setText_2(&p->m_turbValue[index], buf);
        /* 离线态弱显：主题感知次级文本（原 Mid 落深底不可读，走查实锅）。
         * 联机后的 Text/BrightText 角色分支不变；自身盘被本入口钉成当前
         * 应用盘副本（只覆 WindowText），角色取色仍随盘。 */
        HsfTheme_applySecondaryText((XWidget*)&p->m_turbValue[index]);
        return;
    }
    if (hasTemp) {
        /* [带温度的机型] 浊度与水温同刻（一帧读出），并列显示。 */
        snprintf(buf, sizeof(buf), "%s  %.2f NTU  %.2f \xE2\x84\x83",
                 (name && name[0]) ? name : "浊度仪", ntu, tempC);
    } else {
        snprintf(buf, sizeof(buf), "%s  %.2f NTU",
                 (name && name[0]) ? name : "浊度仪", ntu);
    }
    XLabel_setText_2(&p->m_turbValue[index], buf);
    /* [文本变长 ⇒ 抬最小宽 2026-11-11 根修] 水质块改成网格后，**列宽按
       子件最小宽分配**：浊度值标签的最小宽默认 0 ⇒ 列宽只由同列别的
       控件（启动钮）撑出，运行期变长的文本（「出水浊度仪 0.04 NTU 19.35℃」
       ≈ 470px）超出列宽 ⇒ **右端被裁**（现场截图：只显示「出水浊度仪 0」）。
       同一列的「值1」看似完整，只是蹭了同列泵状态标签的宽度。
       修法：设完文本后把该标签的最小宽抬到自身 sizeHint 宽 ⇒ 列宽必然
       容得下文本（XLabel 的 sizeHint 按**当前文本**实时计算，不走缓存）。 */
    {
        const int w = XWidget_sizeHint((XWidget*)&p->m_turbValue[index]).width;
        if (w > 0)
            XWidget_setMinimumWidth((XWidget*)&p->m_turbValue[index], w);
    }
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
    /* 记下最近一拍的泵状态：闸门变化时要按它重算按钮使能（见
     * HsfMonitorFlowMeter_setControlEnabled）。 */
    p->m_pumpOn = on;
    p->m_pumpReady = ready;
    /* [状态行不再重复泵名] 泵名由上方独立的 m_pumpTitle 标签承担；这里原先
     * 又在状态文案前拼一遍 name，界面上显示成「进水泵  进水泵 停止（输出
     * 已关）」（实测截图）。状态行只留状态本身。 */
    if (!ready) {
        snprintf(buf, sizeof(buf), "%s",
                 (text && text[0]) ? text : "不可控");
        XLabel_setText_2(&p->m_pumpState, buf);
    } else if (on) {
        XLabel_setText_2(&p->m_pumpState, "运行（输出已开）");
    } else {
        XLabel_setText_2(&p->m_pumpState, "停止（输出已关）");
    }
    /* name 保留在签名里（调用方按服务端通道名传入，将来可能用于多泵），
     * 本函数当前不再拼它，显式消费以免 C4100。 */
    (void)name;
    /* 闸门原因优先于 ready/on：自动模式/未连接/权限不足时状态行要说明
     * 原因（否则用户看到"停止（输出已关）"却点不动启动，以为是 bug）。 */
    if (!p->m_ctrlEnabled && p->m_ctrlReason[0])
        XLabel_setText_2(&p->m_pumpState, p->m_ctrlReason);
    /* [幂等置灰 + 外部闸] 已生效的一侧禁用（对齐 Python：已启动则"启动"
     * 置灰）；m_ctrlEnabled=false 时两侧一律置灰（服务端 hsfAppControlGate
     * 会拒自动模式下的设备控制，客户端必须提前反映，不能等点了才被拒）。 */
    XWidget_setEnabled((XWidget*)&p->m_pumpStart,
                       p->m_ctrlEnabled && ready && !on);
    XWidget_setEnabled((XWidget*)&p->m_pumpStop,
                       p->m_ctrlEnabled && ready && on);
}

/**
 * @brief 控制闸：整体启停泵按钮（对齐 HsfMonitorValve_setControlEnabled）。
 * @details 闸门只看外部条件（连接/运行模式/权限），与点位 ready 求交在
 *          setPump 里做。调用点：切页、模式变更、权限/连接变化。
 */
void HsfMonitorFlowMeter_setControlEnabled(bool enabled,
                                           const char* blockedReason)
{
    HsfFmPage* p = s_fmPage;
    if (!p) return;
    p->m_ctrlEnabled = enabled;
    if (!enabled && blockedReason && blockedReason[0])
        snprintf(p->m_ctrlReason, sizeof(p->m_ctrlReason), "%s", blockedReason);
    else
        p->m_ctrlReason[0] = '\0';
    if (!enabled && p->m_ctrlReason[0])
        XLabel_setText_2(&p->m_pumpState, p->m_ctrlReason);
    XWidget_setEnabled((XWidget*)&p->m_pumpStart,
                       enabled && p->m_pumpReady && !p->m_pumpOn);
    XWidget_setEnabled((XWidget*)&p->m_pumpStop,
                       enabled && p->m_pumpReady && p->m_pumpOn);
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
