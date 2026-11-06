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
#include "XhsfLinkProto.h"
#include "HsfVfdProviderRemote.h"
#include "HsfLinkClient.h"
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
#include "XEvent.h"              /* XMouseEvent（自测注入点击用）。 */
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
/* ---- 调速方式（对齐 Python 客户端：频率调速 / 转速调速）---- */
#define VFD_SPEED_MODE_HZ    0       /**< 频率调速（给定单位 Hz）。 */
#define VFD_SPEED_MODE_RPM   1       /**< 转速调速（给定单位 rpm）。 */
#define VFD_RATED_RPM_DEF    1440.0  /**< 缺省额定转速（rpm；4 极 50Hz 实测）。 */
#define VFD_RATED_HZ_DEF     50.0    /**< 缺省额定频率（Hz）。 */
/** @brief 实时下发的防抖窗（ms）：拖动滑块只发最后一帧（总线不被打满）。 */
#define VFD_APPLY_DEBOUNCE_MS 200
/** @brief 自测序列的最大步数（XHSF_VFD_SELFTEST 分号分隔）。 */
#define VFD_SELFTEST_MAX_STEPS 4
/** @brief 实时下发的最大重排次数（快照未就绪时；200ms×N ≈ 2s）。 */
#define VFD_APPLY_MAX_RETRY  10
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
/** @brief 过渡态蓝（加速中/减速中/正在停机：在"动"，但不是故障也不是稳定运行）。 */
#define VFD_COLOR_PHASE      0xFF1A66D0u
#define VFD_COLOR_TRACK      0xFFC9D2DAu  /**< 仪表底轨。 */
#define VFD_COLOR_GAUGE      0xFF1F4E79u  /**< 仪表进度（品牌蓝）。 */
#define VFD_COLOR_TEXT       0xFF1A2733u  /**< 主文本。 */
#define VFD_COLOR_TEXT_DIM   0xFF5A6572u  /**< 弱文本。 */

/**
 * @brief 运行状态 → 横幅底色/文本（对齐 Python RUN_STATE_TEXT + 配色口径）。
 * @param runState 状态枚举。
 * @param running  状态字是否仍报"运行"（区分「减速中」与「正在停机」）。
 * @param color    底色输出。
 * @param text     文本输出。
 * @details [「减速中」有两种含义，要分开说] 用户 2026-11-06：「点击停止后，
 *          变频器是慢慢停机的，希望正在停机这个过程状态显示出来，还有正在
 *          加速、正在减速都显示下」。服务端把两种减速都归到 Decelerating，
 *          但操作员看到的应是不同的词：
 *            · 运行中降给定 → 「减速中」（还在转，只是变慢）
 *            · 已发停止命令 → 「正在停机」（目标就是停）
 *          判据用 running 位：状态字仍报运行 = 还在正常降速；已报停机 =
 *          那是停机过程。
 *          [配色] 过渡态用**强调蓝**（在"动"，但不是故障也不是稳定运行），
 *          稳定运行绿、故障红、停机深灰 —— 一眼分出四类（对齐 Python）。
 */
static void vfdRunStateStyle(int runState, bool running,
                             uint32_t* color, const char** text)
{
    switch (runState) {
    case XVfdRunState_RunningForward:
        *color = VFD_COLOR_RUN; *text = "\xE6\xAD\xA3\xE8\xBD\xAC\xE8\xBF\x90\xE8\xA1\x8C"; /* 正转运行 */
        break;
    case XVfdRunState_RunningReverse:
        *color = VFD_COLOR_RUN; *text = "\xE5\x8F\x8D\xE8\xBD\xAC\xE8\xBF\x90\xE8\xA1\x8C"; /* 反转运行 */
        break;
    case XVfdRunState_ConstantSpeed:
        *color = VFD_COLOR_RUN; *text = "\xE6\x81\x92\xE9\x80\x9F\xE8\xBF\x90\xE8\xA1\x8C"; /* 恒速运行 */
        break;
    case XVfdRunState_JogForward:
        *color = VFD_COLOR_RUN; *text = "\xE6\xAD\xA3\xE8\xBD\xAC\xE7\x82\xB9\xE5\x8A\xA8"; /* 正转点动 */
        break;
    case XVfdRunState_JogReverse:
        *color = VFD_COLOR_RUN; *text = "\xE5\x8F\x8D\xE8\xBD\xAC\xE7\x82\xB9\xE5\x8A\xA8"; /* 反转点动 */
        break;
    case XVfdRunState_Accelerating:
        *color = VFD_COLOR_PHASE; *text = "\xE5\x8A\xA0\xE9\x80\x9F\xE4\xB8\xAD"; /* 加速中 */
        break;
    case XVfdRunState_Decelerating:
        /* [正在停机 vs 减速中] 状态字已报停机 ⇒ 这是停机过程。 */
        if (!running) {
            *color = VFD_COLOR_PHASE;
            *text = "\xE6\xAD\xA3\xE5\x9C\xA8\xE5\x81\x9C\xE6\x9C\xBA"; /* 正在停机 */
        } else {
            *color = VFD_COLOR_PHASE; *text = "\xE5\x87\x8F\xE9\x80\x9F\xE4\xB8\xAD"; /* 减速中 */
        }
        break;
    case XVfdRunState_Fault:
        *color = VFD_COLOR_FAULT; *text = "\xE6\x95\x85\xE9\x9A\x9C";           /* 故障 */
        break;
    case XVfdRunState_Stopped:
        *color = VFD_COLOR_STOP; *text = "\xE5\x81\x9C\xE6\x9C\xBA";            /* 停机 */
        break;
    case XVfdRunState_Ready:
        *color = VFD_COLOR_READY; *text = "\xE5\xB0\xB1\xE7\xBB\xAD";           /* 就绪 */
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
/* [前置声明] 横幅自绘要用页面统一的状态入口（vfdPageStatus），其定义在
 * 本文件后部（本地后端取设备状态 / 远端后端取网络快照）。 */
static const XVfdStatus* vfdPageStatus(HsfMonitorVfd* self);
/* [前置声明] 自测动作分发：页节拍要推进自测序列的后续步骤。 */
static void vfdSelfTestDoAction(HsfMonitorVfd* self, const char* action,
                                double hz);
/* [前置声明] 实时下发的排程（下发回调里要重排）。 */
static void vfdScheduleApply(HsfMonitorVfd* self);
/* [前置声明] 实时下发的试发（页节拍重试用）。 */
static void vfdTryApplyNow(HsfMonitorVfd* self);

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
    XLabel       m_cardSet;     /**< 设定频率卡（Python 同项）。 */
    XLabel       m_cardFault;   /**< 故障卡（故障文案/无故障）。 */
    /* 区 3：给定行。 */
    XLabel       m_setLabel;    /**< 「频率给定」。 */
    XSlider      m_setSlider;   /**< 0..500（0.1Hz 定点；永远按 Hz 口径）。 */
    XSpinBox     m_setSpin;     /**< 用户量纲（频率模式 Hz / 转速模式 rpm）。 */
    XTimer*      m_applyTimer;  /**< 实时下发的防抖定时器（200ms；拖动只发最后一帧）。 */
    /* 自测序列（XHSF_VFD_SELFTEST，分号分隔多步；页节拍按秒推进）。 */
    char         m_ssAction[VFD_SELFTEST_MAX_STEPS][24];
    double       m_ssHz[VFD_SELFTEST_MAX_STEPS];
    int          m_ssCount;      /**< 已登记的步数。 */
    int          m_ssNext;       /**< 下一个待执行的步。 */
    int64_t      m_ssNextMs;     /**< 下一步的最早执行时刻（单调毫秒）。 */
    /* 区 4：操作行（方向下拉 + 启停单钮双态 + 点动/复位条件使能）。 */
    XComboBox    m_dirCombo;    /**< 运行方向（正转/反转；启动时生效）。 */
    XComboBox    m_speedMode;   /**< 调速方式（频率调速/转速调速）。 */
    XLabel       m_speedNote;   /**< 换算说明（额定点口径）。 */
    XPushButton  m_btnRun;      /**< 启动/停止（随运行态换字）。 */
    XPushButton  m_btnJog;      /**< 点动（按住生效；运行中禁用）。 */
    XTimer*      m_jogTimer;    /**< 点动心跳定时器（NULL=未点动）。 */
    bool         m_jogReverse;  /**< 本次点动方向。 */
    XPushButton  m_btnReset;    /**< 故障复位（有故障才可点）。 */
    XPushButton  m_btnFree;     /**< 自由停车（对齐 Python btn_free）。 */
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
    XLabel       m_hint;        /**< 提示行（自动只读/不可控原因）。 */
    /* ---- 数据提供者 ---- */
    const HsfVfdProvider* m_provider; /**< 函数表（借用；可 NULL=全降级）。 */
    void*        m_provUser;    /**< provider userData 缓存。 */
    /* ---- 演示驱动 ---- */
    XTimer*     m_simTimer;    /**< 200ms 模拟源驱动定时器（NULL=无）。 */
    bool         m_updating;    /**< 回显抑制：刷快照时不回写控件。 */
    bool         m_setDirty;    /**< 用户正在编辑给定（编辑期间不回写遥测）。 */
    /* [下发确认仲裁] 已下发给定、待设备回显确认（确认前不回写遥测）。 */
    bool         m_applyPending;
    double       m_appliedHz;   /**< 待确认的给定（Hz）。 */
    int          m_applyRetry;  /**< 快照未就绪时的重试次数。 */
    bool         m_applyWait;   /**< 待重试（快照未就绪时挂起）。 */
    bool         m_autoMode;    /**< 自动运行模式（true=只读）。 */
    /* ---- 服务端运行模式（顶栏统一管理，页面只读消费） ---- */
    bool         m_srvModeKnown;/**< 是否已从服务端拿到模式。 */
    bool         m_srvModeAuto; /**< 服务端是否自动模式。 */
    /* ---- 控制应答提示（远端后端；provider->commandError 增量观测） ---- */
    int          m_cmdErrSeq;      /**< 已消费的控制应答序号（0=无）。 */
    bool         m_cmdErrActive;   /**< 最近一拍应答是否带错误（持续显示到下拍）。 */
    char         m_cmdErrText[96]; /**< 最近错误文本（服务端原文 UTF-8）。 */
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
    /* [取状态走页面统一入口] 原先这里直接取 provider->device() 的本地状态：
     * 远端后端（XLink）**没有本地设备对象**（device() 恒 NULL）⇒ 横幅永远
     * 判"无数据"，而数值卡（走 vfdPageStatus → snapshot）却有值——同一页
     * 两套数据源打架。改走 vfdPageStatus：本地后端取设备状态、远端后端取
     * 网络快照，两边口径统一。 */
    st = vfdPageStatus(page);

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
        else vfdRunStateStyle(st->m_runState, st->m_running, &color, &stateText);
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

/** @brief 取当前状态快照（远端后端走 snapshot 槽；本地后端走设备）。
 *  @note  远端 provider 无本地设备对象（遥测由网络 BSON 解码而来），
 *         provider 表的 snapshot/caps 槽即为此设：非 NULL 优先，
 *         NULL 回落 device() 的 status_const/caps（老后端零改动）。 */
static const XVfdStatus* vfdPageStatus(HsfMonitorVfd* self)
{
    const XVfdDevice* dev;
    if (self->m_provider && self->m_provider->snapshot)
        return self->m_provider->snapshot(self->m_provUser);
    dev = self->m_provider ? self->m_provider->device(self->m_provUser) : NULL;
    return dev ? XVfdDevice_status_const(dev) : NULL;
}

/* [前置声明] 调速方式相关（定义在文件后部）：vfdRunSlot 等早期槽要用
 * vfdCurrentSetHz，vfdSpeedModeSlot 要用 vfdApplySpeedMode —— 缺声明会被
 * 编译器当成隐式 int 声明，再撞上真正的定义（C2371）。 */
static double vfdCurrentSetHz(HsfMonitorVfd* self);
static void vfdApplySpeedMode(HsfMonitorVfd* self);

/** @brief 取能力位图（远端后端走 caps 槽；本地后端走设备）。 */
static uint32_t vfdPageCaps(HsfMonitorVfd* self)
{
    const XVfdDevice* dev;
    if (self->m_provider && self->m_provider->caps)
        return self->m_provider->caps(self->m_provUser);
    dev = self->m_provider ? self->m_provider->device(self->m_provUser) : NULL;
    return dev ? XVfdDevice_caps(dev) : 0;
}

/** @brief 操作按钮点击：按地址分派到 provider 动作。
 *  @note  m_btnRun 双态：未运行=「启动」（按方向下拉下发 start），
 *         运行中=「停止」（下发 stop）——按钮文字由 vfdRefresh 随
 *         运行态翻转，点击行为同样按运行态分派。
 *  @note  [2026-11-04 receiver 实锅] connect_1 首参是**注册的 receiver**
 *         （按钮自身），槽直转页类型得到的是错误基址 → 整槽失效
 *         （探针实测：闸/下发/启停点击全部无效果）。与 vfdSliderChanged
 *         2026-11-03 同款教训；三钮内嵌偏移各异、offsetof 反推有歧义，
 *         故拆三枚薄槽各自反推（照 vfdSliderChanged 口径）。 */
static void vfdRunSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_btnRun));
    const HsfVfdProvider* p;
    const XVfdStatus* st;
    bool running;
    (void)args;
    if (!self || !self->m_provider) return;
    p = self->m_provider;
    st = vfdPageStatus(self);
    running = st && st->m_valid && st->m_running;
    if (running) {
        p->stop(self->m_provUser);
        return;
    }
    /* [启动：给定**捎带**在启动命令里] 对齐 Python 客户端 _on_run_stop：
     * 把界面给定（Hz 或换算后的 Hz）随 START 一起下发，服务端内部顺序执行
     * 「写 1000H → 写 2000H」。
     * [为什么不单独先发给定] 服务端为安全起见拒绝**停机状态下的独立给定**
     * ⇒ 单独发 SET 必被拒 ⇒ 启动按变频器里残留的旧给定跑（现场实测：界面
     * 设 3Hz、实际跑 25Hz = 界面在骗人）。捎带不受该闸限制。 */
    /* [启动捎带给定] 启动命令里带上界面给定量（服务端"先写给定、再发
     * 命令字"）⇒ 启动后电机就跑在界面给的那个值上，而不是变频器里残留
     * 的旧给定（"界面设 3Hz、实际跑 25Hz"＝界面在骗人）。 */
    {
        const double hzSet = vfdCurrentSetHz(self);
        p->start(self->m_provUser,
                 XComboBox_currentIndex(&self->m_dirCombo) == 1,
                 hzSet);
        /* [确认仲裁] 启动同样会带来一次"给定变更"：设备回显追上之前按住
         * 回写，避免回写把设备**旧**给定灌回滑块（用户以为没生效）。 */
        if (hzSet > 0.0) {
            self->m_applyPending = true;
            self->m_appliedHz = hzSet;
            self->m_setDirty = true;
        } else {
            self->m_applyPending = false;
            self->m_setDirty = false;
        }
    }
    /* [不再单独发一条给定] 启动命令已捎带给定（服务端 Start 先写给定再发
     * 命令字），再补一条 SET 既冗余，又可能因"停机禁给定"闸被拒（两条
     * 命令的先后由服务端调度决定），还会把刚才捎带的值覆盖掉。
     * 对齐 Python 客户端：启动只发一条 START。 */
}

/**
 * @brief 点动心跳重发（由页面 TimerEvent 分发）。
 * @note  服务端看门狗 HSF_JOG_TIMEOUT_MS(600ms) = 3× 本间隔（200ms），
 *        容忍连续丢 2 帧仍不断机。
 */
static void vfdJogTick(void* userData, XTimerData* timer)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)userData;
    (void)timer;
    if (!self || !self->m_provider || !self->m_provider->jog) return;
    {
        /* [点动捎带给定] 服务端处理 Jog 时先写给定再发命令字（对齐 Python
         * 服务端 apply_control 的 OP_JOG 分支）⇒ 按住点动按界面给定跑，
         * 而不是变频器里上一次的残留给定。 */
        bool sent = self->m_provider->jog(self->m_provUser, self->m_jogReverse,
                                           vfdCurrentSetHz(self));
        XPrintf("[vfd-jog] 心跳 rev=%d sent=%d\n",
                (int)self->m_jogReverse, (int)sent);
    }
}

/**
 * @brief 点动按钮按下：立即发一次点动 + 启动心跳重发。
 * @note  为什么不用 clicked：clicked 在松开时发，点动必须"按下即动"。
 */
static void vfdJogPressedSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_btnJog));
    (void)args;
    if (!self || !self->m_provider || !self->m_provider->jog) return;
    if (self->m_jogTimer) return;                   /* 已在点动。 */
    self->m_jogReverse = (XComboBox_currentIndex(&self->m_dirCombo) == 1);
    {
        /* [点动捎带给定] 服务端处理 Jog 时先写给定再发命令字（对齐 Python
         * 服务端 apply_control 的 OP_JOG 分支）⇒ 按住点动按界面给定跑，
         * 而不是变频器里上一次的残留给定。 */
        bool sent = self->m_provider->jog(self->m_provUser, self->m_jogReverse,
                                           vfdCurrentSetHz(self));
        XPrintf("[vfd-jog] 点动 rev=%d sent=%d\n",
                (int)self->m_jogReverse, (int)sent);
    }
    /* 心跳重发：独立 XTimer（与页面既有 m_simTimer 完全同构，
     * 该形态在本页已验证可回火）。 */
    {
        XTimer* t = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (t) {
            XTimer_setInterval(t, (int)HSF_JOG_REPEAT_MS);
            XTimer_setTimerCallback(t, vfdJogTick);
            XTimer_setUserData(t, self);
            XTimer_start_base(t);
            self->m_jogTimer = t;
        } else {
            XPrintf("[vfd-jog] 心跳定时器创建失败\n");
        }
    }
}

/**
 * @brief 点动按钮松开：停心跳 + 通知服务端立即停机。
 */
static void vfdJogReleasedSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_btnJog));
    (void)args;
    if (!self) return;
    if (self->m_jogTimer) {
        XTimer_stop_base(self->m_jogTimer);
        XClassDelete((XClass*)self->m_jogTimer);
        self->m_jogTimer = NULL;
    }
    /* [点动释放 op=101] 松手必须"立刻停"，不等服务端 600ms 看门狗；普通
     * stop 是"受控停机"语义（减速到位）。远端后端用专用 op=101（对齐
     * Python 客户端的 {"op":101}）；本地模拟后端无此槽，回落 stop()。 */
    if (self->m_provider) {
        if (self->m_provider->jogRelease)
            self->m_provider->jogRelease(self->m_provUser);
        else if (self->m_provider->stop)
            self->m_provider->stop(self->m_provUser);
    }
    XPrintf("[vfd-jog] 点动松开 → 释放（按钮 enabled=%d down=%d）\n",
            (int)XWidget_isEnabled((XWidget*)&self->m_btnJog),
            (int)XAbstractButton_isDown((XAbstractButton*)&self->m_btnJog));
}


static void vfdResetSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_btnReset));
    (void)args;
    if (!self || !self->m_provider) return;
    self->m_provider->resetFault(self->m_provUser);
}

/**
 * @brief 自由停车（对齐 Python btn_free → op=FREE_STOP）。
 * @details 自由停车 = 立即断输出（不走减速曲线），与"停止"（受控减速）
 *          是两种现场动作：急停/故障善后用前者。
 */
static void vfdFreeStopSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_btnFree));
    (void)args;
    if (!self || !self->m_provider) return;
    if (self->m_provider->freeStop)
        self->m_provider->freeStop(self->m_provUser);
}

/**
 * @brief 模式应答到达：刷新本地缓存并联动页面只读态。
 */
static void vfdModeNotify(void* userData)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)userData;
    int autoMode = 0;
    if (!self) return;
    if (!HsfLinkClient_runMode(&autoMode)) {
        XLabel_setText_2(&self->m_hint, HsfLinkClient_lastModeError());
        return;
    }
    self->m_srvModeKnown = true;
    self->m_srvModeAuto = (autoMode == 1);
    /* 服务端自动 ⇒ 页面只读（复用既有 m_autoMode 机制）。 */
    self->m_autoMode = self->m_srvModeAuto;
    vfdApplyControlState(self);
}

/**
 * @brief 额定点（rpm/Hz）：由遥测的「输出频率/换算转速」反推，缺省 1440rpm@50Hz。
 * @details [口径来源] 变频器不提供转速寄存器，转速是**频率换算值**
 *          （服务端按额定比例换算）；故比例直接取遥测里 outputFreqHz 与
 *          speedRpm 之比——与服务端换算**同一比例**，不会出现界面 1440rpm
 *          而设备换算出别的数。缺省：4 极 50Hz 实测 1440rpm。
 */
static void vfdRatedPoint(HsfMonitorVfd* self, double* outRpm, double* outHz)
{
    const XVfdStatus* st = vfdPageStatus(self);
    double rpm = VFD_RATED_RPM_DEF;
    double hz = VFD_RATED_HZ_DEF;
    if (st && st->m_valid && st->m_outputFreqHz > 0.1 && st->m_speedRpm > 0.1) {
        const double hzPerRpm = st->m_outputFreqHz / st->m_speedRpm;
        if (hzPerRpm > 0.0001) {
            hz = VFD_RATED_HZ_DEF;
            rpm = hz / hzPerRpm;
        }
    }
    if (outRpm) *outRpm = rpm;
    if (outHz) *outHz = hz;
}

/**
 * @brief 当前界面给定 → 频率（Hz）。
 * @details 频率调速直接取输入框；转速调速按额定点反解（rpm → Hz），
 *          与 Python 客户端同一口径（设备只接受频率通道，转速是换算值）。
 */
static double vfdCurrentSetHz(HsfMonitorVfd* self)
{
    double rpm = 0.0;
    double hz = 0.0;
    double raw;
    if (!self) return 0.0;
    raw = (double)XSpinBox_value(&self->m_setSpin);
    if (XComboBox_currentIndex(&self->m_speedMode) == VFD_SPEED_MODE_RPM) {
        vfdRatedPoint(self, &rpm, &hz);
        if (rpm > 0.0) return raw / rpm * hz;
    }
    return raw;
}

/**
 * @brief 频率（Hz）→ 当前模式下的**界面给定值**（频率模式 Hz / 转速模式 rpm）。
 * @details [单位口径] 输入框的单位是**用户量纲**（Hz 或 rpm），滑块是
 *          0.1Hz 定点（VFD_FREQ_MAX10=500 ⇒ 50.0Hz）。两者的换算只走
 *          本函数与 vfdCurrentSetHz 一对，杜绝"同一份值按两种单位解释"
 *          ——那种错位会让 50Hz 显示成 500Hz、1440rpm 显示成 500rpm，
 *          而且下发值还会差 10 倍。
 */
static double vfdSetValueOfHz(HsfMonitorVfd* self, double hz)
{
    double rpm = 0.0;
    double hzRef = 0.0;

    if (!self) return 0.0;
    if (XComboBox_currentIndex(&self->m_speedMode) != VFD_SPEED_MODE_RPM)
        return hz;
    vfdRatedPoint(self, &rpm, &hzRef);
    if (!(rpm > 0.0) || !(hzRef > 0.0)) return 0.0;
    return hz / hzRef * rpm;
}

/**
 * @brief 调速方式下拉切换（receiver=combo 自身；offsetof 反推页）。
 */
static void vfdSpeedModeSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_speedMode));
    (void)args;
    if (!self) return;
    vfdApplySpeedMode(self);
    XWidget_update((XWidget*)&self->m_banner);
}

/**
 * @brief 调速方式切换：改输入框量程/后缀/步进与初值（对齐 Python）。
 * @details [为什么切换时重置值] 15Hz 直接当成 15rpm 下发是荒谬的（几乎不转）。
 *          按当前「设定频率」换算给初值（而非固定值），保证切过去显示的就是
 *          "当前给定"的等价量，不产生意外跳变；无数据时退回半速。
 */
static void vfdApplySpeedMode(HsfMonitorVfd* self)
{
    double rpm = 0.0;
    double hz = 0.0;
    double baseHz;
    int v;

    if (!self) return;
    vfdRatedPoint(self, &rpm, &hz);
    {
        const XVfdStatus* st = vfdPageStatus(self);
        if (st && st->m_valid && (st->m_validMask & XVfdField_SetFreqHz))
            baseHz = st->m_setFreqHz;
        else
            baseHz = hz / 2.0;
    }
    if (XComboBox_currentIndex(&self->m_speedMode) == VFD_SPEED_MODE_RPM) {
        XSpinBox_setRange(&self->m_setSpin, 0, (int)(rpm + 0.5));
        XSpinBox_setSuffix(&self->m_setSpin, " rpm");
        XSpinBox_setSingleStep(&self->m_setSpin, 10);
        v = (int)(baseHz / hz * rpm + 0.5);
    } else {
        XSpinBox_setRange(&self->m_setSpin, 0, (int)(hz + 0.5));
        XSpinBox_setSuffix(&self->m_setSpin, " Hz");
        XSpinBox_setSingleStep(&self->m_setSpin, 1);
        v = (int)(baseHz + 0.5);
    }
    self->m_updating = true;
    XSpinBox_setValue(&self->m_setSpin, v);
    self->m_updating = false;
    /* 滑块仍按 0.1Hz 定点（与给定物理量一致），随模式换算同步。 */
    {
        double hzVal = (XComboBox_currentIndex(&self->m_speedMode) ==
                        VFD_SPEED_MODE_RPM)
                           ? (rpm > 0.0 ? (double)v / rpm * hz : 0.0)
                           : (double)v;
        int v10 = (int)(hzVal * 10.0 + 0.5);
        if (v10 < 0) v10 = 0;
        if (v10 > VFD_FREQ_MAX10) v10 = VFD_FREQ_MAX10;
        self->m_updating = true;
        XAbstractSlider_setValue((XAbstractSlider*)&self->m_setSlider, v10);
        self->m_updating = false;
    }
    self->m_setDirty = true;   /* 切换后即视为"用户已改"，遥测不再回写。 */
    /* 换算说明（对齐 Python 的提示行：输出转速为频率换算值，额定点口径）。 */
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "\xE8\xBE\x93\xE5\x87\xBA\xE8\xBD\xAC\xE9\x80\x9F"
                 "\xE4\xB8\xBA\xE9\xA2\x91\xE7\x8E\x87\xE6\x8D\xA2\xE5\x80\xBC"
                 "\xEF\xBC\x88\xE5\x8F\x98\xE9\xA2\x91\xE5\x99\xA8\xE6\x97\xA0"
                 "\xE8\xBD\xAC\xE9\x80\x9F\xE4\xBC\xA0\xE6\x84\x9F\xE5\x99\xA8"
                 "\xEF\xBC\x8C\xE9\xA2\x9D\xE7\x82\xB9\xE7\x82\xB9"
                 " %.0f rpm @ %.0f Hz\xEF\xBC\x89",
                 rpm, hz);
        XLabel_setText_2(&self->m_speedNote, buf);
    }
}

/**
 * @brief 尝试实时下发（条件不满足时挂起，由页节拍重试）。
 * @details [为什么不直接在定时器回调里重排] 框架的 singleShot 定时器是
 *          **回调返回后**才停（VXObject_timerEvent：先发 timeout 信号，
 *          再 stop）——在回调里 stop+start 重装，会被紧接着的那次 stop
 *          取消掉（实测：只重排到第 1 次就不再触发）。故"重试"改由页
 *          节拍（200ms）驱动。
 */
static void vfdTryApplyNow(HsfMonitorVfd* self)
{
    const XVfdStatus* st;
    double hz;

    if (!self || !self->m_provider || !self->m_provider->setFrequency) return;
    st = vfdPageStatus(self);
    if (!st || !st->m_valid) {
        /* [快照还没到] 刚登录/刚进页面时遥测尚未抵达（valid=0），此时丢掉
         * 这次下发＝用户的改动"没生效"。挂起，节拍重试。 */
        self->m_applyWait = true;
        XPrintf("[vfd-apply] 快照未就绪，挂起待重试\n");
        return;
    }
    if (!st->m_running) {
        /* 停机态：独立给定会被服务端「停机禁给定」闸拒（界面还会冒错误
         * 提示）；停机态的给定由下一次启动捎带（vfdRunSlot）。 */
        self->m_applyWait = false;
        XPrintf("[vfd-apply] 设备停机，跳过（给定随启动捎带）\n");
        return;
    }
    if (self->m_autoMode) {
        self->m_applyWait = false;
        XPrintf("[vfd-apply] 自动运行中，跳过\n");
        return;
    }
    hz = vfdCurrentSetHz(self);
    if (!(hz > 0.0)) {
        self->m_applyWait = false;
        XPrintf("[vfd-apply] 给定无效（%.2f Hz），跳过\n", hz);
        return;
    }
    if (!self->m_provider->setFrequency(self->m_provUser, hz)) return;
    self->m_applyWait = false;
    self->m_applyRetry = 0;
    /* [确认仲裁] 已下发但设备回显还没跟上（链路+轮询滞后 ~100ms）：
     * 此时**不能**恢复遥测回写——回写会把**旧**给定灌回滑块，用户刚
     * 拖出来的值被弹回，并触发"回写→改值→再下发"的循环（实测：实时
     * 下发 20Hz 后连发 50Hz）。故标记"待确认"，由 vfdRefresh 在设备
     * 回显与我们下发的值一致时再恢复回写。 */
    self->m_applyPending = true;
    self->m_appliedHz = hz;
    self->m_setDirty = true;
    XPrintf("[vfd-apply] 实时下发 %.2f Hz\n", hz);
}

/**
 * @brief 实时下发定时器回调（200ms 防抖到点 ⇒ 试发一次）。
 */
static void vfdApplyTimerCb(void* userData, XTimerData* timer)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)userData;
    (void)timer;
    vfdTryApplyNow(self);
}

/**
 * @brief 排一次实时下发（防抖：重装定时器＝重新计时）。
 * @param self 页对象。
 */
static void vfdScheduleApply(HsfMonitorVfd* self)
{
    if (!self || !self->m_applyTimer) return;
    /* [新一次改动 ⇒ 重试预算重置] */
    self->m_applyRetry = 0;
    XTimer_stop_base(self->m_applyTimer);
    XTimer_start_base(self->m_applyTimer);
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
    /* [单位] 滑块是 0.1Hz 定点 ⇒ 先除 10 得 Hz，再按当前模式换算成
     * 界面量纲（Hz / rpm）。旧实现把定点值直接塞进输入框，界面显示的
     * 给定比真实值大 10 倍（50Hz 显示成 500Hz / 500rpm）。 */
    {
        const double hz = (double)v / 10.0;
        const double shown = vfdSetValueOfHz(self, hz);
        self->m_updating = true;
        XSpinBox_setValue(&self->m_setSpin, (int)(shown + 0.5));
        self->m_updating = false;
    }
    /* [用户编辑中] 遥测回写不得覆盖用户刚拖出来的值（否则每 100ms 被
     * 服务端的"上次给定量"弹回 ⇒ 现场观感=滑块拖不动）。对齐 Python
     * 客户端：用户输入优先，直到实时下发完成。 */
    self->m_setDirty = true;
    /* [实时生效] 运行中拖动滑块 ⇒ 给定即刻下发（防抖 200ms，只发最后一帧）。 */
    vfdScheduleApply(self);
}

/** @brief SpinBox 编辑 → 同步 Slider（receiver=spinbox 自身）。 */
static void vfdSpinChanged(XObject* receiver, XVarList* args)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)((char*)receiver -
                                           offsetof(HsfMonitorVfd, m_setSpin));
    int v10;
    (void)args;
    if (!self || self->m_updating) return;
    /* [单位] 输入框是用户量纲（Hz / rpm）⇒ 换算成 Hz 再写成滑块的
     * 0.1Hz 定点。旧实现把 Hz 直接当定点写，滑块位置只有真实值的 1/10。 */
    {
        const double hz = vfdCurrentSetHz(self);
        v10 = (int)(hz * 10.0 + 0.5);
        if (v10 < 0) v10 = 0;
        if (v10 > VFD_FREQ_MAX10) v10 = VFD_FREQ_MAX10;
    }
    self->m_updating = true;
    XAbstractSlider_setValue((XAbstractSlider*)&self->m_setSlider, v10);
    self->m_updating = false;
    self->m_setDirty = true;   /* 用户编辑中：遥测回写让位（见 vfdSliderChanged）。 */
    /* [实时生效] 运行中改输入框 ⇒ 给定即刻下发（防抖 200ms）。 */
    vfdScheduleApply(self);
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
    uint32_t caps = vfdPageCaps(self);
    /* [2026-11-06 控制闸口径对齐 Python] 控制可用性 = **手动模式 + 已就绪**
     * （不再有本地「允许界面操作」勾选框）：
     *   · 自动模式 ⇒ 锁死（服务端也拒）；
     *   · 未连接服务端 ⇒ 锁死；
     *   · 其余 ⇒ 可控——现场点一下就动。
     * provider 的 controlEnabled 仍参与（本地 demo 后端用它表达"演示可用"），
     * 但远端后端默认开（见 HsfVfdProviderRemote 的闸缺省）。 */
    bool gate = !self->m_autoMode && self->m_provider &&
                self->m_provider->controlEnabled &&
                self->m_provider->controlEnabled(self->m_provUser);

    {
        const XVfdStatus* st2 = vfdPageStatus(self);
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
            /* [点动按钮不按运行态禁用] 对齐 Python 客户端：btn_jog 只在
             * "原因"（未连接 / 自动模式 / 权限不足）时禁用，运行中照常可用。
             * 旧实现带 !running ⇒ 点动按下的瞬间设备进运行态，200ms 刷新
             * 拍就把按钮 setEnabled(false)；而框架按 **Qt 口径** 在按钮被
             * 禁用时清掉按下态并**发 released** ⇒ 客户端 released 槽停心跳
             * + 发 op101 ⇒ 现场症状：鼠标没动，按住状态定时自己退出、
             * 变频器跟着停。 */
            {
                /* [诊断] 点动按钮的可用态变化是"长按被提前松开"的头号嫌疑：
                 * 按钮被禁用 ⇒ 框架按 Qt 口径清按下态并发 released ⇒ 点动
                 * 心跳断 + 发 op101 ⇒ 电机停。留日志以便现场一次复现定位。 */
                const bool wasEnabled =
                    XWidget_isEnabled((XWidget*)&self->m_btnJog);
                XWidget_setEnabled((XWidget*)&self->m_btnJog, gate);
                if (wasEnabled != gate)
                    XPrintf("[vfd-jog] 按钮可用态变化 %d -> %d（down=%d）\n",
                            (int)wasEnabled, (int)gate,
                            (int)XAbstractButton_isDown(
                                (XAbstractButton*)&self->m_btnJog));
            }
        }
        /* 故障复位：仅有故障时可点（对齐 Python：btn_reset 只按 reason 禁用，
         * 这里保留"有故障才可点"更安全——无故障时复位无意义）。 */
        XWidget_setEnabled((XWidget*)&self->m_btnReset, gate && faulted);
        /* 自由停车：与 Python btn_free 同口径（只按 reason 禁用）。 */
        XWidget_setEnabled((XWidget*)&self->m_btnFree, gate);
    }
    XWidget_setEnabled((XWidget*)&self->m_setSlider, gate);
    XWidget_setEnabled((XWidget*)&self->m_setSpin,   gate);
    /* 控制应答提示（远端后端）：按序号增量消费——被拒（err 非空）顶替
     * 提示行并持续显示到下一拍应答（成功 err 空 -> 自动回落闸提示）；
     * 本地后端无此槽，行为不变。 */
    if (!self->m_autoMode && self->m_provider && self->m_provider->commandError) {
        int seq = 0;
        char err[96];
        err[0] = '\0';
        if (self->m_provider->commandError(self->m_provUser, &seq, err, sizeof(err)) &&
            seq != 0) {
            if (seq != self->m_cmdErrSeq) {
                self->m_cmdErrSeq = seq;
                self->m_cmdErrActive = (err[0] != '\0');
                if (self->m_cmdErrActive)
                    snprintf(self->m_cmdErrText, sizeof(self->m_cmdErrText), "%s", err);
            }
            if (self->m_cmdErrActive) {
                XLabel_setText_2(&self->m_hint, self->m_cmdErrText);
                return;     /* 错误提示优先于闸提示。 */
            }
        }
    }
    if (self->m_srvModeAuto) {
        XLabel_setText_2(&self->m_hint,
            "\xE6\x9C\x8D\xE5\x8A\xA1\xE7\xAB\xAF\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD\xEF\xBC\x9A"
            "\xE8\xBF\x9C\xE7\xA8\x8B\xE6\x8E\xA7\xE5\x88\xB6\xE5\xB7\xB2\xE7\xA6\x81\xE7\x94\xA8"); /* 服务端自动运行中：远程控制已禁用 */
        return;
    }
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
    const XVfdStatus* st;
    if (!self) return;
    st = vfdPageStatus(self);

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
                   "\xE8\xBE\x93\xE5\x87\xBA\xE8\xBD\xAC\xE9\x80\x9F", /* 输出转速 */
                   st && (st->m_validMask & XVfdField_SpeedRpm),
                   st ? st->m_speedRpm : 0.0, "rpm");
    vfdSetCardText(&self->m_cardTorque,
                   "\xE8\xBE\x93\xE5\x87\xBA\xE8\xBD\xAC\xE7\x9F\xA9", /* 输出转矩 */
                   st && (st->m_validMask & XVfdField_TorquePercent),
                   st ? st->m_torquePercent : 0.0, "%");
    /* [设定频率] 补齐 Python 的读数项：现场要能一眼看到"我给的是多少"，
     * 而不只是"现在跑的是多少"（两者不一致就是"界面在骗人"）。 */
    vfdSetCardText(&self->m_cardSet,
                   "\xE8\xAE\xBE\xE5\xAE\x9A\xE9\xA2\x91\xE7\x8E\x87", /* 设定频率 */
                   st && (st->m_validMask & XVfdField_SetFreqHz),
                   st ? st->m_setFreqHz : 0.0, "Hz");
    /* [故障] 有故障显示故障文案（品牌表），无故障显示"无故障"——Python 同款
     * （fault_text：无故障码 ⇒ "无故障"）。 */
    {
        char buf[96];
        const char* fault = NULL;
        if (st && st->m_valid && st->m_faulted) {
            fault = (self->m_provider && self->m_provider->faultText)
                  ? self->m_provider->faultText(self->m_provUser, st->m_faultCode)
                  : NULL;
            if (!fault || !fault[0]) {
                char code[24];
                snprintf(code, sizeof(code), "0x%04X", st->m_faultCode);
                fault = code;
            }
            snprintf(buf, sizeof(buf), "%s\n%s",
                     "\xE6\x95\x85\xE9\x9A\x9C", fault);   /* 故障 */
        } else {
            snprintf(buf, sizeof(buf), "%s\n%s",
                     "\xE6\x95\x85\xE9\x9A\x9C",
                     "\xE6\x97\xA0\xE6\x95\x85\xE9\x9A\x9C"); /* 故障 / 无故障 */
        }
        XLabel_setText_2(&self->m_cardFault, buf);
    }

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

    /* 设定回显（抑制回环；**用户编辑期间不回写**——服务端 setFreqHz 是
       "上次给定量"，每拍回写会把用户刚拖出来的值弹回 ⇒ 滑块拖不动。
       用户编辑优先，直到实时下发完成（vfdApplyTimerCb 清 m_setDirty）。 */
    if (st && (st->m_validMask & XVfdField_SetFreqHz) && !self->m_updating) {
        const double hzSet = st->m_setFreqHz;
        const int v10 = (int)(hzSet * 10.0 + 0.5);
        /* [下发确认] 设备回显与我们下发的值一致 ⇒ 确认完成，恢复回写
         * （回写值与本地一致，不会打架）。不一致说明设备还没收到/还没
         * 生效 ⇒ 继续按住回写，避免把用户的值弹回旧值。 */
        if (self->m_applyPending &&
            fabs(hzSet - self->m_appliedHz) < 0.05) {
            self->m_applyPending = false;
            self->m_setDirty = false;
        }
        if (!self->m_setDirty && v10 >= 0 && v10 <= VFD_FREQ_MAX10) {
            /* [单位] 滑块收 0.1Hz 定点；输入框收**用户量纲**
             * （频率模式 Hz / 转速模式 rpm = Hz 按额定点换算）。
             * 旧实现两处都塞 0.1Hz 定点 ⇒ 输入框显示的给定比真实值大
             * 10 倍（50Hz 显示成 500Hz、1440rpm 显示成 500rpm）。 */
            const double shown = vfdSetValueOfHz(self, hzSet);
            self->m_updating = true;
            XAbstractSlider_setValue((XAbstractSlider*)&self->m_setSlider, v10);
            XSpinBox_setValue(&self->m_setSpin, (int)(shown + 0.5));
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

/** @brief 页面节拍（200ms）：推进剧情 → 设备解码 → 点动自愈巡检。 */
static void vfdSimTimerCb(void* userData, XTimerData* timer)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)userData;
    (void)timer;
#if XVFD_ON
    /* 模拟源单例推进（页面创建即订阅；同刻只有一页注册回调）。 */
    hsfVfdSimTick(NULL);
#endif
    /* [点动自愈] 心跳在跑、但按钮已不是按下态 ⇒ **松手事件丢了**
     * （窗口失焦、抓取被抢、事件被吞、页面切走时 release 没送达）。
     * 点动是"持续意图"，必须能自停：每拍比对一次按钮 down 态，不一致
     * 就按松手收口（停心跳 + 通知服务端停机）。
     * 少了这一条，任何一次丢失的 release 都会让电机一直点动下去——
     * 现场症状："点动后停不下来，松开也不停，点停止也没用"（停止会被
     * 下一拍心跳立刻覆盖）。
     * [为什么放节拍而不是刷新] 刷新只在遥测到达时跑（页面切走就没有了），
     * 而"点着点动切走页面"正是最容易丢松手的场景。 */
    if (self && self->m_jogTimer &&
        !XAbstractButton_isDown((XAbstractButton*)&self->m_btnJog)) {
        XPrintf("[vfd-jog] 检测到松手丢失 → 自愈释放\n");
        vfdJogReleasedSlot((XObject*)&self->m_btnJog, NULL);
    }

    /* [实时下发重试] 快照未就绪时挂起的下发，由节拍重试（最多 2s）。
     * [为什么不用定时器自重排] 框架的 singleShot 定时器在**回调返回后**
     * 才停 ⇒ 回调里 stop+start 重装会被紧接着的 stop 取消。 */
    if (self && self->m_applyWait &&
        self->m_applyRetry < VFD_APPLY_MAX_RETRY) {
        self->m_applyRetry++;
        vfdTryApplyNow(self);
    }

    /* [自测序列推进] 规格里的多步动作按秒推进（第一步建页即跑）。
     * 步间隔 1s：给服务端留出"启动已生效"的时间（写给定 + 命令字 + 爬坡）。 */
    if (self && self->m_ssNext < self->m_ssCount) {
        const int64_t now = (int64_t)XDateTime_currentMSecsSinceEpoch();
        if (now >= self->m_ssNextMs) {
            XPrintf("[vfd-selftest] 执行步骤 %d：action=%s hz=%.1f\n",
                    self->m_ssNext, self->m_ssAction[self->m_ssNext],
                    self->m_ssHz[self->m_ssNext]);
            vfdSelfTestDoAction(self, self->m_ssAction[self->m_ssNext],
                                self->m_ssHz[self->m_ssNext]);
            self->m_ssNext++;
            self->m_ssNextMs = now + 1000;
        }
    }
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

/** @brief 操作按钮初始化（40dp 触控高 + 点击连接；槽=各钮薄槽，
 *         receiver 实锅口径见 vfdRunSlot 注释）。 */
static void vfdInitBtn(XPushButton* btn, XWidget* parent, const char* text,
                       XSlotFunc1 slot)
{
    XPushButton_init(btn, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)btn);
    XPushButton_setText_2(btn, text);
    XWidget_setFixedHeight((XWidget*)btn, 40);
    XObject_connect_1((XObject*)btn, XSignal(XAbstractButton_clicked_signal),
                      (XObject*)btn, slot, XConnectionType_Direct);
    XWidget_show((XWidget*)btn);
}

/**
 * @brief 建一个**不连 clicked** 的按钮（点动用：动作语义在按下/松开上）。
 * @details [为什么点动不能连 clicked] 框架的松手顺序是 Qt 口径
 *          **先 released 再 clicked**（XAbstractButton.c 的 clickInternal）。
 *          若把"开始点动"挂在 clicked 上，松手就会：先发 released（停心跳
 *          + 释放），紧接着再发 clicked（**又启动一次点动 + 新心跳**）⇒
 *          电机永远停不下来，且"停止"按钮按下后被下一拍心跳立刻覆盖。
 *          实测日志：松手行后面紧跟一行"点动 rev=0 sent=1"。
 */
static void vfdInitBtnNoClick(XPushButton* btn, XWidget* parent,
                              const char* text)
{
    XPushButton_init(btn, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)btn);
    XPushButton_setText_2(btn, text);
    XWidget_setFixedHeight((XWidget*)btn, 40);
    XWidget_show((XWidget*)btn);
}

/* ==================== GUI 自测钩子（联调/CI） ==================== */

/**
 * @brief 执行一次自测动作（等价于用户操作控件）。
 * @details env `XHSF_VFD_SELFTEST` = "action[,hz]"：
 *          start/stop/freestop/jog/reset/setfreq/gate-on/gate-off/
 *          mode-auto/mode-manual。
 *          直呼槽函数，走的是与点击完全相同的代码路径
 *          （provider 函数表 → XLink → 服务端 → 设备）。
 */
/**
 * @brief 自测延迟触发（登录完成后才执行，避免控制被丢弃）。
 */
static void vfdRunSelfTest(HsfMonitorVfd* self);

static void vfdSelfTestTimer(void* userData, XTimerData* timer)
{
    HsfMonitorVfd* self = (HsfMonitorVfd*)userData;
    /* [不要在这里停定时器] 回调第二参是 **XTimerData\***（定时器的数据块），
     * 不是 XTimer\* —— 把它当 XTimer\* 传给 XTimer_stop_base 会读到空虚表
     * ⇒ 触发 XError（参数：XClassGetVtable(timer)=NULL）把客户端打死。
     * 本定时器是 singleShot（见建页处），本来就只跑一次，无需停。 */
    (void)timer;
    if (!self) return;
    vfdRunSelfTest(self);
}

/**
 * @brief 向控件注入一次"按下 + 松开"（自测用；口径同 Test/XGuiTest 的
 *        XButtonHoldTest：直接派发鼠标事件到控件）。
 * @details [为什么需要它] 点动的"按住生效"要验的是**按钮的信号接线**：
 *        松手时框架先发 released 再发 clicked（Qt 口径），若把"开始点动"
 *        挂在 clicked 上，松手就会再启动一次点动 ⇒ 电机永远停不下来。
 *        这个钩子让该缺陷可在无人工点击的情况下回归。
 * @param w 目标控件（点动按钮）。
 */
static void vfdInjectClick(XWidget* w)
{
    XRect g;
    XMouseEvent me;
    XPoint p;

    if (!w) return;
    g = XWidget_geometry(w);
    p.x = g.width / 2;
    p.y = g.height / 2;
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

/**
 * @brief 自测动作分发（一步）。
 * @param self 页对象；action 动作名；hz 该步数值（Hz）。
 */
static void vfdSelfTestDoAction(HsfMonitorVfd* self, const char* action,
                                double hz)
{
    if (!self || !action || !self->m_provider) return;

    if (strcmp(action, "mode-auto") == 0) {
        HsfLinkClient_setRunMode(true);
    } else if (strcmp(action, "mode-manual") == 0) {
        HsfLinkClient_setRunMode(false);
    } else if (strcmp(action, "setfreq") == 0) {
        if (self->m_provider->setFrequency)
            self->m_provider->setFrequency(self->m_provUser, hz);
    } else if (strcmp(action, "setfreq-ui") == 0) {
        /* [走真实 UI 路径] 直接写滑块值 ⇒ 触发 valueChanged ⇒ 与用户拖动
         * 完全同一条链（vfdSliderChanged → 实时下发防抖 → 下发）。
         * 自测要验的正是这条链，故**不能**直接调 provider->setFrequency
         * （那会绕过"实时生效"逻辑，等于没测）。 */
        const int v10 = (int)(hz * 10.0 + 0.5);
        XAbstractSlider_setValue((XAbstractSlider*)&self->m_setSlider, v10);
    } else if (strcmp(action, "start") == 0) {
        if (self->m_provider->start)
            self->m_provider->start(self->m_provUser,
                                    XComboBox_currentIndex(&self->m_dirCombo) == 1,
                                    hz);
    } else if (strcmp(action, "stop") == 0) {
        if (self->m_provider->stop) self->m_provider->stop(self->m_provUser);
    } else if (strcmp(action, "freestop") == 0) {
        if (self->m_provider->freeStop)
            self->m_provider->freeStop(self->m_provUser);
    } else if (strcmp(action, "jog") == 0) {
        if (self->m_provider->jog) self->m_provider->jog(self->m_provUser, false, hz);
    } else if (strcmp(action, "jog-hold") == 0) {
        /* 等价"按住"：走 pressed 槽（内含立即发送 + 启动心跳）。 */
        vfdJogPressedSlot((XObject*)&self->m_btnJog, NULL);
    } else if (strcmp(action, "jog-release") == 0) {
        vfdJogReleasedSlot((XObject*)&self->m_btnJog, NULL);
    } else if (strcmp(action, "jog-click") == 0) {
        /* [按钮级回归] 合成一次"按下+松开"：应看到「点动 rev=… sent=1」
         * 紧跟「点动松开 → 释放」，且**释放之后不得再出现新的"点动"行**
         * （那说明"开始点动"被挂在了 clicked 上 ⇒ 电机永远停不下来）。 */
        vfdInjectClick((XWidget*)&self->m_btnJog);
    } else if (strcmp(action, "reset") == 0) {
        if (self->m_provider->resetFault)
            self->m_provider->resetFault(self->m_provUser);
    } else {
        XPrintf("[vfd-selftest] 未知 action: %s\n", action);
    }
}

/**
 * @brief 解析自测规格并**逐步执行**。
 * @details 规格 = "动作[,数值]"，多步用 ';' 分隔：
 *          `start,50;setfreq-ui,20` = 先以 50Hz 启动，1 秒后把滑块改到
 *          20Hz（走真实 UI 路径 ⇒ 触发实时下发）。
 *          多步是必需的：单步只能做一件事，而"运行中改给定"这类场景
 *          天然是"先启动、再改"的时序。
 */
static void vfdRunSelfTest(HsfMonitorVfd* self)
{
    const char* spec = XSystem_environment("XHSF_VFD_SELFTEST");
    const char* cur;

    if (!self || !spec || !spec[0]) return;

    cur = spec;
    while (cur && *cur && self->m_ssCount < VFD_SELFTEST_MAX_STEPS) {
        const char* sep = strchr(cur, ';');
        const char* comma = strchr(cur, ',');
        char action[32];
        size_t n;
        if (comma && sep && comma > sep) comma = NULL;
        n = comma ? (size_t)(comma - cur) : strlen(cur);
        if (n >= sizeof(action)) n = sizeof(action) - 1;
        XMemcpy(action, cur, n);
        action[n] = '\0';
        XPrintf("[vfd-selftest] 登记步骤 %d：action=%s hz=%.1f\n",
                self->m_ssCount, action, comma ? atof(comma + 1) : 0.0);
        XMemcpy(self->m_ssAction[self->m_ssCount], action, n + 1);
        self->m_ssHz[self->m_ssCount] = comma ? atof(comma + 1) : 0.0;
        self->m_ssCount++;
        if (!sep) break;
        cur = sep + 1;
    }
    /* [只先跑第一步] 其余步骤由页节拍按秒推进（见 vfdSimTimerCb）——
     * 多步之间必须有间隔："先启动、1 秒后再改给定"里，若第二步紧跟第一步
     * 执行，设备还没进运行态（遥测滞后），实时下发会被跳过。 */
    if (self->m_ssCount > 0) {
        XPrintf("[vfd-selftest] 执行步骤 0：action=%s hz=%.1f\n",
                self->m_ssAction[0], self->m_ssHz[0]);
        vfdSelfTestDoAction(self, self->m_ssAction[0], self->m_ssHz[0]);
        self->m_ssNext = 1;
        self->m_ssNextMs = (int64_t)XDateTime_currentMSecsSinceEpoch() + 1000;
    }
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

    /* ---- provider 接入（登录过=远端 XLink 后端；离线=本地模拟源；
            后端换表点） ----
     * @note 主窗先建、登录后连线的排布下，建页时链路尚未 open，这里会
     *       先绑本地模拟源；登录成功后由 HsfMonitorVfd_rebindProvider
     *       切到远端表（页面不重建，数据源就地换）。 */
    self->m_provider = HsfLinkClient_isOpen() ? HsfVfdProvider_remote()
                                              : HsfVfdProvider_demo();
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
            vfdInitCard(&self->m_cardSet, cardHost);
            vfdInitCard(&self->m_cardFault, cardHost);
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
                /* 第三行：设定频率 + 故障（对齐 Python 读数项补全）。 */
                XGridLayout_addWidget(grid, (XWidget*)&self->m_cardSet,   2, 0, 0);
                XGridLayout_addWidget(grid, (XWidget*)&self->m_cardFault, 2, 1, 0);
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
    /* [调速方式] 频率调速 / 转速调速（对齐 Python 客户端）：给定值的单位
     * 由它决定，转速只是频率的换算值（设备只接受频率通道）。 */
    XComboBox_init(&self->m_speedMode, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_speedMode);
    XComboBox_addItem_2(&self->m_speedMode,
                        "\xE9\xA2\x91\xE7\x8E\x87\xE8\xB0\x83\xE9\x80\x9F"); /* 频率调速 */
    XComboBox_addItem_2(&self->m_speedMode,
                        "\xE8\xBD\xAC\xE9\x80\x9F\xE8\xB0\x83\xE9\x80\x9F"); /* 转速调速 */
    /* [缺省调速方式 = 转速调速] 现场按"转速"下达工艺要求（给定 1440rpm
     * 这类），频率是换算出来的中间量；缺省就落在转速上，需要频率时再切。
     * 建页后会调一次 vfdApplySpeedMode（见建页尾），输入框的量程/后缀/
     * 步进随这个缺省一起生效（0..额定rpm、步进 10）。 */
    XComboBox_setCurrentIndex(&self->m_speedMode, VFD_SPEED_MODE_RPM);
    XWidget_setFixedWidth((XWidget*)&self->m_speedMode, 104);
    XWidget_show((XWidget*)&self->m_speedMode);
    /* 换算说明（输出转速为频率换算值，额定点口径）。 */
    XLabel_init(&self->m_speedNote, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_speedNote);
    XLabel_setTextPixelSize(&self->m_speedNote, 11);
    XLabel_setText_2(&self->m_speedNote, " ");
    XWidget_setForegroundRole((XWidget*)&self->m_speedNote,
                              XPaletteColorRole_Mid);
    XWidget_show((XWidget*)&self->m_speedNote);
    /* 调速方式切换槽（receiver=combo 自身，offsetof 反推页）。 */
    XObject_connect_1((XObject*)&self->m_speedMode,
                      XSignal(XComboBox_currentIndexChanged_signal),
                      (XObject*)&self->m_speedMode, vfdSpeedModeSlot,
                      XConnectionType_Direct);

    XLabel_init(&self->m_setLabel, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_setLabel);
    XLabel_setText_2(&self->m_setLabel,
                     "\xE9\xA2\x91\xE7\x8E\x87\xE7\xBB\x99\xE5\xAE\x9A"); /* 频率给定 */
    XLabel_setTextPixelSize(&self->m_setLabel, 13);
    /* [固定宽] 滑块移走自己的行之后，给定行的空余宽度会被"可伸展"的
     * 控件吃掉——标签按内容宽排的话，空隙就落在标签与输入框之间（观感：
     * 控件被推散）。标签给固定宽（≈四字），多余空间交给末尾的换算说明。 */
    XWidget_setFixedWidth((XWidget*)&self->m_setLabel, 72);
    XWidget_show((XWidget*)&self->m_setLabel);

    XSlider_init(&self->m_setSlider, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_setSlider);
    XAbstractSlider_setRange((XAbstractSlider*)&self->m_setSlider, 0, VFD_FREQ_MAX10);
    /* [加高] 把手高度＝控件高−2 ⇒ 控件 48px ⇒ 把手 46px 高，触摸面
     * 够大，现场手指能拖得动（原先挤在给定行里既窄又矮）。 */
    XWidget_setFixedHeight((XWidget*)&self->m_setSlider, 48);
    XWidget_setMinimumWidth((XWidget*)&self->m_setSlider, 240);
    XWidget_show((XWidget*)&self->m_setSlider);

    XSpinBox_init(&self->m_setSpin, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_setSpin);
    /* [量程/后缀/步进随调速方式走] 频率调速：0..额定Hz 步进 1；转速调速：
     * 0..额定rpm 步进 10（见 vfdApplySpeedMode）。
     * [单位] 输入框是**用户量纲**（Hz / rpm），不是滑块那种 0.1Hz 定点
     * ——旧实现按定点给量程（0..500），界面于是能填出 500Hz（设备上限
     * 只有 50Hz）。 */
    XSpinBox_setRange(&self->m_setSpin, 0, (int)(VFD_RATED_HZ_DEF + 0.5));
    XSpinBox_setSingleStep(&self->m_setSpin, 1);
    XSpinBox_setSuffix(&self->m_setSpin, " Hz");
    XWidget_show((XWidget*)&self->m_setSpin);

    /* [「下发」按钮已移除 —— 2026-11-06 用户口径] 运行中改频率/转速
     * **实时生效**（vfdScheduleApply → 200ms 防抖后自动下发），停机态的
     * 给定由启动命令捎带（vfdRunSlot）。多一个「下发」按钮反而让人以为
     * 改完不点就不生效。 */
    {
        XTimer* t = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (t) {
            XTimer_setInterval(t, VFD_APPLY_DEBOUNCE_MS);
            /* [一次性] 框架定时器默认重复（Qt 同款）；不设 singleShot 就会
             * 每 200ms 自己再跑一次 ⇒ 与遥测回写绞在一起（回写→改值→
             * 再下发→回写…）。singleShot 的定时器触发后只停不删，可再
             * 次 start（见 vfdScheduleApply 的 stop+start 重装）。 */
            XTimer_setSingleShot(t, true);
            XTimer_setTimerCallback(t, vfdApplyTimerCb);
            XTimer_setUserData(t, self);
            self->m_applyTimer = t;   /* 不 start：按需排（见 vfdScheduleApply） */
        }
    }

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
               "\xE5\x90\xAF\xE5\x8A\xA8",              /* 启动（双态字随刷新翻） */
               vfdRunSlot);
    /* 点动"按住生效"：**不连 clicked**（松手时 clicked 会再启动一次点动，
     * 电机就永远停不下来）——只接 pressed（开始）与 released（松手）。
     * 见 vfdInitBtnNoClick 的注记。 */
    vfdInitBtnNoClick(&self->m_btnJog, &self->m_base,
                      "\xE7\x82\xB9\xE5\x8A\xA8");              /* 点动 */
    XObject_connect_1((XObject*)&self->m_btnJog,
                      XSignal(XAbstractButton_pressed_signal),
                      (XObject*)&self->m_btnJog, vfdJogPressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_btnJog,
                      XSignal(XAbstractButton_released_signal),
                      (XObject*)&self->m_btnJog, vfdJogReleasedSlot,
                      XConnectionType_Direct);
    vfdInitBtn(&self->m_btnReset, &self->m_base,
               "\xE6\x95\x85\xE9\x9A\x9C\xE5\xA4\x8D\xE4\xBD\x8D", /* 故障复位 */
               vfdResetSlot);
    /* [自由停车] 对齐 Python 客户端 btn_free（op=FREE_STOP）：立即断输出，
     * 与"停止"（受控减速）是两种现场动作。 */
    vfdInitBtn(&self->m_btnFree, &self->m_base,
               "\xE8\x87\xAA\xE7\x94\xB1\xE5\x81\x9C\xE8\xBD\xA6", /* 自由停车 */
               vfdFreeStopSlot);
    /* 远端后端：订阅模式应答并主动查一次初始模式。 */
    HsfLinkClient_setModeCallback(vfdModeNotify, self);
    if (HsfLinkClient_isOpen())
        HsfLinkClient_queryRunMode();

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

    /* [控制闸复选框已移除] 2026-11-06：本地「允许界面操作」闸让现场必须
     * 先勾选才能动变频器（观感=几乎无法控制），而 Python 客户端没有这层
     * ——控制可用性只看全局模式（手动/自动）+ 连接态。统一到 Python 口径。 */

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
        /* 给定行：调速方式 + 标签 + SpinBox + 下发 + 换算说明。 */
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 10);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_speedMode);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_setLabel);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_setSpin);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_speedNote);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)row);
        }
    }
    {
        /* 滑块**单独一行**（2026-11-06 用户实锅）：原先它与标签/输入框/
         * 下发挤在同一行，只剩几十像素宽、又是细把手，现场手指根本拖不动
         * 调速。独占一行 + 加高后：把手高度＝控件高−2，触摸面直接变大。 */
        XHBoxLayout* sliderRow = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                              XCLASS_DEFAULT_MEMORY_TYPE);
        if (sliderRow) {
            XBoxLayout_init(sliderRow, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)sliderRow, 10);
            XBoxLayout_addWidget(sliderRow, (XWidget*)&self->m_setSlider);
            XBoxLayout_setStretch(sliderRow, 0, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)sliderRow);
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
            XBoxLayout_addWidget(opRow, (XWidget*)&self->m_btnFree);
            XBoxLayout_addWidget(opRow, (XWidget*)&self->m_hint);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)opRow);
        }
    }
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_trendView);

    /* ---- 建页即按当前调速方式摆一次输入框 ----
     * [为什么建页就要做] vfdApplySpeedMode 负责"量程/后缀/步进/初值"，
     * 原本只在用户切换下拉时才跑 ⇒ **首屏的输入框永远是频率模式的量纲**
     * （后缀 Hz、量程 0..50Hz）；模式已是转速模式时，给定值会被量程夹回
     * 上限（50Hz 显示成 50，而不是 1440rpm）。建页先摆一次，首屏就与
     * 下拉显示的模式一致。
     * [m_setDirty 必须清回 false] vfdApplySpeedMode 内部把"用户已改给定"
     * 置位（切换模式后不该被遥测回写覆盖）；建页这一步不是用户动作，若
     * 不清，遥测回写会被永久挡掉 ⇒ 输入框永远停在建页那一刻的半速缺省
     * （720rpm），与设备实际给定量脱节。 */
    vfdApplySpeedMode(self);
    self->m_setDirty = false;

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
    s_vfdPage = self;
    /* 环境变量直驱（演示/自测外部门；对齐数字量页 XHSF_DIGITAL_AUTO）。 */
    if (XSystem_environment("XHSF_VFD_AUTO"))
        s_pendingAutoMode = true;
    if (self->m_autoMode || s_pendingAutoMode)
        HsfMonitorVfd_setInteractiveMode(false);   /* 幂等应用：隐藏+锁 */

    /* 自测钩子延后执行：建页时 XLink 可能尚未登录完成
     * （HsfLinkClient_isReady()==false 时控制会被丢弃）。用一次性定时器
     * 推迟 1.2s，覆盖"登录+首拍遥测"窗口。 */
    if (XSystem_environment("XHSF_VFD_SELFTEST")) {
        XTimer* st = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (st) {
            XTimer_setInterval(st, 1200);
            XTimer_setSingleShot(st, true);
            XTimer_setTimerCallback(st, vfdSelfTestTimer);
            XTimer_setUserData(st, self);
            XTimer_start_base(st);
        }
    }

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
        /* 退回手动：恢复可控（闸由全局模式决定，无需用户勾选）。 */
        if (self->m_provider && self->m_provider->setControlEnabled)
            self->m_provider->setControlEnabled(self->m_provUser, true);
    } else {
        /* 进自动运行：闸强制关（真实链路在自动程序手里，服务端也会拒）。 */
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

bool HsfMonitorVfd_rebindProvider(void)
{
    HsfMonitorVfd* self = s_vfdPage;
    const HsfVfdProvider* target;
    if (!self) return false;
    /* 登录成功后才接远端（主窗先建时建页早于连线）。 */
    target = HsfLinkClient_isOpen() ? HsfVfdProvider_remote()
                                    : HsfVfdProvider_demo();
    if (target == self->m_provider) return true;
    /* 先退旧订再换表：旧表（模拟源）在册期间会被其 tick 回调打进页面。 */
    if (self->m_provider && self->m_provider->unsubscribe)
        self->m_provider->unsubscribe(self->m_provUser, self);
    self->m_provider = target;
    self->m_provUser = target ? target->m_userData : NULL;
    if (target && target->subscribe)
        target->subscribe(self->m_provUser, vfdPageNotify, self);
    /* 远端后端：补挂模式订阅并查一次当前模式（建页时链路未开）。 */
    if (HsfLinkClient_isOpen()) {
        HsfLinkClient_setModeCallback(vfdModeNotify, self);
        HsfLinkClient_queryRunMode();
    }
    vfdRefresh(self);
    return true;
}

#endif /* XWIDGET_ON && ... && XVFD_ON */
