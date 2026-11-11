/**
 * @file       HsfDataHistory.c
 * @brief      数据-历史数据页：服务端历史分页（0x0118）→ 表格 + 翻页。
 * @details    [数据源] 服务端 SQLite 落盘（history 表），经 0x0118 分页查询
 *             拉取；与 Python 客户端同接口同键名（offset/limit/beforeTs →
 *             total/cols/rows/…）。
 *             [显示口径] 服务端只发原始值，显示格式化在
 *             `HsfLinkHistory.c`（与服务端口径一致），本页只负责摆表 +
 *             翻页 + 状态行。
 *             [轮询] 应答是异步的（请求-应答），故页面用 200ms 定时器看
 *             结果有没有变（offset/行数）→ 有变就重填表格（不整页重建，
 *             避免闪）。
 */
#include "HsfDataHistory.h"

#include "HsfSettingsLocal.h" /* 轮询周期=本地设置「界面刷新周期」 */

#include "HsfLinkClient.h"
#include "HsfLinkHistory.h"
#include "HsfDataBar.h"
#include "HsfExport.h"      /* 共享导出模块（CSV / 真 XLSX）。 */
#include "HsfTableSort.h"
#include "HsfTheme.h"
#include "HsfUi.h"   /* hsfUi_pageFontPx（本地设置·按页字号） */
#include "XBoxLayout.h"
#include "XChart.h"
#include "XChartView.h"
#include "XComboBox.h"
#include "XFileDialog.h"  /* 导出走框架文件对话框（格式=过滤器档位） */
#include "XHeaderView.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XLineSeries.h"
#include "HsfChartZoom.h"  /* 缩放取数的纯逻辑助手（批次计划/视口换算） */
#include "XObject.h"     /* XObject_sender / XSignal / XObject_connect_1 */
#include "XPushButton.h"
#include "XTableWidget.h"
#include "XTableView.h"     /* 列筛选（setColumnHidden） */
#include "XToolTip.h"      /* 图表悬浮气泡（对标 QToolTip）。 */
#include "XDateTime.h"     /* 气泡/轴刻度的绝对时间格式化。 */
#include "XFont.h"         /* XFont（列宽实测用：XPainter_textWidth 的入参）。 */
#include "XPainter.h"      /* XPainter_textWidth（按当前字体现量表头宽度）。 */
#include "XTimer.h"
#include "XValueAxis.h"
#include "XVector.h"     /* 图表全量筛选结果的累加容器（框架组件） */
#include "XMemory.h"
#include "XPrintf.h"
#include "XXYSeries.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON && XPUSHBUTTON_ON && XLAYOUT_ON

/** @brief 本页字号折算（本地设置 FontScale/Page9）。 */
static int histPx(int px)
{
    return hsfUi_pageFontPx(HSF_PAGE_DATA_HISTORY, px);
}

/** @brief 每页行数缺省（与 Python HISTORY_PAGE_DEFAULT 同值；可选 10/20/50）。 */
#define HIST_PAGE_LIMIT   20
/** @brief 页大小可选档（与 Python PAGE_SIZES 同值）。 */
static const int kHistPageSizes[3] = { 10, 20, 50 };

/** @brief 数值列区间（3 输出频率 … 13 电箱湿度；小数位重排/画曲线只动这段）。
 *  @note  2026-11-10 插入「水温(℃)」显示列（第 8 位）⇒ 右端 12→13。 */
#define HIST_NUM_FIRST   3
#define HIST_NUM_LAST    13
/** @brief 折线序列数（=数值列数）。 */
#define HIST_SERIES_MAX  (HIST_NUM_LAST - HIST_NUM_FIRST + 1)

/**
 * @brief 可见列下标数组类型（**容量 == 显示列数**）。
 * @details [为什么要typedef 而不是到处写数字] 2026-11-10 实锅：
 *          `HSF_HISTORY_DISPLAY_COLS` 从 15 增到 16（新增「水温」列），
 *          本文件三处栈数组仍写死 `uint32_t cols[15]` ⇒
 *          `histVisibleCols()` 按新宏写 16 个元素 ⇒ **越界写 4 字节踩坏栈**
 *          ⇒ MSVC /RTC1 报「Stack around the variable 'cols' was corrupted」
 *          ⇒ 客户端窗口一起来就崩（历史页是启动时逐页建出来的）。
 *          改成 typedef 后**加列只改宏一处**，调用点不可能再漏。
 */
typedef uint32_t HistCols[HSF_HISTORY_DISPLAY_COLS];

/**
 * @brief 编译期断言：可见列数组的元素个数必须 == 显示列数。
 * @details [为什么用「负数组长度」而不是 _Static_assert] 本工程 C 标准是
 *          C99（CMakeLists.txt:109），MSVC 默认 C 模式不认 C11 的
 *          `_Static_assert`（需 /std:c11+），裸用会被当隐式函数声明
 *          ⇒ sizeof 前语法错误（同 XPlatformOrientation.c 的
 *          XORI_STATIC_ASSERT 注记）。负数组长度是 C89 起就有的老写法，
 *          两边编译器都在**编译期**报错。
 *          [它拦的是什么] 有人把 HistCols 改回字面量 15、而宏仍是 16 ⇒
 *          负长度 ⇒ 编译失败，不再拖到运行期变成栈越界。
 *          [为什么比的是 sizeof(HistCols) 而不是 sizeof(HistCols[0])]
 *          **C 语言里不能对 typedef 名取下标**（typedef 名不是指针），
 *          写 `sizeof(HistCols[0])` 会报「负下标」C2118（实测 2026-11-10）。
 *          故文件级断言按「字节数 == 元素数 × 元素大小」比。
 */
typedef char hist_cols_size_assert[
    (sizeof(HistCols) == sizeof(uint32_t) * HSF_HISTORY_DISPLAY_COLS)
        ? 1 : -1];

/**
 * @brief 调用点的编译期断言：可见列数组元素数必须 == 显示列数。
 * @details [为什么要每处都断言] typedef 只能保证「谁用 HistCols 谁就是
 *          对的容量」；但若有人绕过 typedef 写 `uint32_t cols[15]`，
 *          文件级断言拦不住（它只看 HistCols）。故在**每个调用点**再断言
 *          一次：把该点真实的 `sizeof(cols)/sizeof(cols[0])` 与宏比对，
 *          谁漏改谁当场编译失败。
 *          [这里可以取下标] 与文件级断言不同，`cols` 是**真变量**（不是
 *          typedef 名）⇒ `cols[0]` 在 C 里合法，元素数可精确数出来。
 * @param cols 调用点的可见列数组。
 */
/* [两级宏展开] `##` 的操作数不做宏展开 ⇒ 直接写 `##__LINE__` 会粘出
 * 字面量 "__LINE__" 而不是行号 ⇒ 必须先经一层普通宏展开。 */
#define HIST_COLS_ASSERT_CAT_(a, b) a##b
#define HIST_COLS_ASSERT_CAT(a, b)  HIST_COLS_ASSERT_CAT_(a, b)
#define HIST_COLS_SIZE_ASSERT(cols)                                        \
    typedef char HIST_COLS_ASSERT_CAT(hist_cols_size_assert_at_,          \
                                      __LINE__)[                           \
        (sizeof(cols) / sizeof((cols)[0]) == HSF_HISTORY_DISPLAY_COLS)     \
            ? 1 : -1]

/** @brief 历史数据列头（与 Python HistoryPage.COLUMNS 逐字一致，含单位）。 */
static const char* const kHistoryHeaders[HSF_HISTORY_DISPLAY_COLS] = {
    "时间",
    "模式",
    "搅拌状态",
    "输出频率(Hz)",
    "输出电流(A)",
    "搅拌转速(rpm)",
    "浊度1(NTU)",
    "浊度2(NTU)",
    "水温(℃)",
    "瞬时流量",
    "正累计",
    "压差(MPa)",
    "电箱温度(℃)",
    "电箱湿度(%RH)",
    "DI(32位)",
    "DO(32位)"
};

/**
 * @brief 各显示列的**列宽下限**（像素；与 kHistoryHeaders 严格同序）。
 * @details [为什么单列一张表] 列宽原先散在两处：建表时逐列 setColumnWidth
 *          一遍、adapt 时又写一张 kW[] 表。两份手工清单必然漂移——2026-11-10
 *          加「水温」列时只改了其中一份 ⇒ 建表 15 列 / adapt 15 列，与
 *          16 列的表头对不上（列宽错位）。合到一张表后，加列只改这里。
 *          [但这只是**下限**] 真列宽由 histCalcColWidths 按当前字体实测
 *          表头文字得出（见该函数）；本表只兜底"别太窄"。
 */
static const int kHistoryColWidths[HSF_HISTORY_DISPLAY_COLS] = {
    185,  /* 时间 */
    80,   /* 模式 */
    90,   /* 搅拌状态 */
    110,  /* 输出频率(Hz) */
    110,  /* 输出电流(A) */
    120,  /* 搅拌转速(rpm) */
    100,  /* 浊度1(NTU) */
    100,  /* 浊度2(NTU) */
    100,  /* 水温(℃) */
    110,  /* 瞬时流量 */
    110,  /* 正累计 */
    100,  /* 压差(MPa) */
    110,  /* 电箱温度(℃) */
    110,  /* 电箱湿度(%RH) */
    110,  /* DI(32位) */
    110   /* DO(32位) */
};

/**
 * @brief 列宽的额外余量（像素）：QSS 表头段的内缩 + 边框 + 粗体余量。
 * @details 客户端 QSS 的 `XTableWidget::section` 规则带
 *          `padding: 6px 8px` 与 `border-right: 1px`（见 HsfThemeCss.h），
 *          且 `font-weight: bold` ⇒ 表头文字比常规字宽一截。列宽若只按
 *          常规字量，文字放不进内缩后的标签矩形 ⇒ 表头被截/空白。
 *          8+8+1 = 17 是 QSS 的硬内缩；再加 10% 给粗体。
 */
#define HIST_HEADER_EXTRA_PX  17
#define HIST_HEADER_BOLD_NUM  10   /* 粗体余量 = 文字宽 × 该值 / 100 */

/** @brief 图表全量数据点（一条图例上的一个原始样本）。
 *  @note  m_x 用**相对基准时刻的秒数**（base = 本批筛选结果首条 ts）：
 *         ① 序列点存在 XPointF（float）里，epoch 毫秒/秒这类 1e9~1e12 量级
 *            的绝对值在 float 下只剩几十秒~两分钟分辨率，曲线会被压成阶梯；
 *            相对秒数在"几天~几个月"的时间窗里仍是亚秒精度。
 *         ② 刻度显示由 X 轴的自定义标签回调（histXLabelText）把秒数换算
 *            成本地时间，因此界面上**不会**再出现旧实现的 "-150270 s"
 *            （服务端按 ts 降序取页 ⇒ 早期数据为负数，以前直接按秒打印）。
 *         m_ts 保留同一条的原始 epoch 毫秒（去重/气泡显示用）。 */
typedef struct HsfHistPoint {
    double m_x;   /**< 相对基准时刻的秒数（时间差，不是页内下标）。 */
    double m_y;   /**< 原值（对应数值列）。 */
    int64_t m_ts; /**< 原始 epoch 毫秒（去重/显示用）。 */
} HsfHistPoint;

/** @brief 历史数据页对象。 */
typedef struct HsfDataHistory {
    XWidget      m_base;        /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;      /**< 页根竖排布局。 */
    XLabel       m_title;       /**< 页标题。 */
    XWidget*     m_bar;         /**< 通用工具条（本页自己一套；借用）。 */
    XLabel       m_status;      /**< 状态行（共 N 条 · 第 x/y 页）。 */
    XTableWidget m_table;       /**< 数据表。 */
    /** @brief 各显示列的**实测列宽**（像素；100% 字号基准）。
     *  @note  由 histCalcColWidths 按**当前字体**实测表头文字得出——写死的
     *        像素只在量它的那个字号下成立（Python 端 auto_min_texts 同款
     *        问题：15 列里 12 列表头被截成"出频率(H"、"度1(NTU"）。 */
    int          m_colWidths[HSF_HISTORY_DISPLAY_COLS];
    HsfTableSort m_sort;      /**< 双击表头排序状态（当前页内）。 */
    XChartView   m_chartView;   /**< 图表视图（与表格二选一显隐）。 */
    XChart*      m_chart;       /**< 折线图（create 所有权在页）。 */
    XLineSeries* m_series[HIST_SERIES_MAX]; /**< 逐数值列的折线。 */
    XValueAxis*  m_axisY;       /**< 数值轴（量程按可见数据算）。 */
    int          m_viewMode;    /**< 0=表格 1=图表。 */
    int          m_digits;      /**< 显示小数位（工具条）。 */
    bool         m_pickersSynced; /**< 时间框已按实际数据区间回填。 */
    int64_t      m_loTs;        /**< 时间窗下界（<=0 不限）。 */
    int64_t      m_hiTs;        /**< 时间窗上界（<=0 不限）。 */
    int64_t      m_tzOffsetMs;  /**< 本地时区偏移（epoch 与日历显示互转用）。 */
    XPushButton  m_prev;        /**< 上一页。 */
    XPushButton  m_next;        /**< 下一页。 */
    /* ---- 全量导出（按当前筛选条件逐批拉全，与图表全量取数同路） ----
     * [定格语义] 发起导出那一刻把 筛选条件（关键字/时间窗）、可见列集合、
     * 落地路径与格式全部定格进 m_export* 快照；后续每一批请求都按快照
     * 下发——等待期间用户改界面（翻页/改筛选/改列）不影响在途导出的
     * 内容，绝不出现"半份旧条件 + 半份新条件"的文件。 */
    bool         m_exportFetching; /**< 全量导出累加中（逐批在途）。 */
    int          m_exportOffset;   /**< 在途导出批的 offset（应答归属校验）。 */
    int          m_exportLimit;    /**< 在途导出批的 limit（恒 HIST_EXPORT_CHUNK）。 */
    int          m_exportTotal;    /**< 首批应答记下的筛选总行数（0=未知）。 */
    int          m_exportWritten;  /**< 已写进行文件的行数。 */
    int          m_exportIdle;     /**< 在途批等应答的拍数（看门狗）。 */
    int          m_exportRetries;  /**< 当前批已重发次数（超限放弃）。 */
    bool         m_exportTruncated;/**< XLSX 触上限截断（状态行注明）。 */
    HsfExportWriter m_exportWriter;/**< 流式写出器（open..finish 生命周期）。 */
    /* [导出列定格] 发导出请求那一刻的可见列集合（异步应答回来时按它写盘，
     * 不回头读下拉——等待期间用户改列筛选不该影响已发出的导出）。 */
    uint32_t     m_exportCols[HSF_HISTORY_DISPLAY_COLS]; /**< 本次导出的列下标（0..N-1）。 */
    int          m_exportColCount; /**< m_exportCols 有效长度（0=未定格）。 */
    /** @brief 本次导出的格式（发起那一刻按文件对话框选中的过滤器档定格）。
     *  @note  导出是异步的（多批请求→逐批写盘），写盘时才看界面会让
     *         落地格式与按下「导出」时选的不一致，故发起时定格。 */
    HsfExportFormat m_exportFormat;
    char         m_exportPath[HSF_EXPORT_PATH_CAP]; /**< 本次导出的落地路径前缀（无扩展名）。 */
    char         m_exportKw[160];  /**< 本次导出冻结的关键字（快照）。 */
    int64_t      m_exportLoTs;     /**< 本次导出冻结的时间窗下界（快照）。 */
    int64_t      m_exportHiTs;     /**< 本次导出冻结的时间窗上界（快照）。 */
    XLineEdit    m_kw;          /**< 关键字输入（下推服务端）。 */
    XComboBox    m_pageSizeBox; /**< 每页条数（10/20/50）。 */
    char         m_kwText[160]; /**< 当前生效的关键字（为空=不筛）。 */
    int          m_pageSize;    /**< 当前页大小。 */
    XTimer*      m_timer;       /**< 应答轮询定时器（200ms）。 */
    int          m_offset;      /**< 当前页起始偏移。 */
    int          m_shownOffset; /**< 已填表的偏移（变了就重填）。 */
    int          m_shownCount;  /**< 已填表的行数。 */
    int          m_shownTotal;  /**< 已填表那一页的筛选总行数（翻页判界用）。 */
    uint32_t     m_shownSeq;    /**< 已填表那一页的应答序号（重排小数位时校验用）。 */
    uint32_t     m_seenSeq;     /**< 已处置（填表或丢弃）的应答序号（HsfHistoryPage.m_seq）。 */
    /* ---- 在途请求归属（应答槽只有一个；页面拿不到 reqId） ----
     * [协议事实] XLink 帧头有 m_requestId（XLinkProto.h:253），Response 会
     * 原样回带对端 id（同文件 :43/:111）；缺的是 HsfLinkClient 没把它随
     * HsfHistoryPage 交给页面（hsfLcDispatch 手里有 frame，HsfLinkClient.c:772）。
     * 在补上这条链路之前，页面只能按服务端回显的 offset/limit 认领，且必须
     * 靠**构造**让不同消费者的 limit 两两不相交（见 HIST_CHART_CHUNK 注释），
     * 否则同 offset 的在途图表批会被当成页应答。 */
    int          m_pageReqOffset;  /**< 在途表格页/导出请求的 offset。 */
    int          m_pageReqLimit;   /**< 在途表格页/导出请求的 limit。 */
    bool         m_pageReqValid;   /**< 有在途表格页/导出请求，等它那一条应答。 */
    int          m_chartReqOffset; /**< 在途图表批请求的 offset。 */
    int          m_chartReqLimit;  /**< 在途图表批请求的 limit（探测批=1..8，正式批=41..49；见 HIST_CHART_CHUNK）。 */
    bool         m_chartReqValid;  /**< 有在途图表批请求，等它那一条应答。 */
    int          m_rowBaseH;    /**< 建页时行高（随字号缩放）。 */

    /* ---- 图表全量筛选结果（按当前筛选条件累加，非当前页） ---- */
    XVector*     m_chartPts[HIST_SERIES_MAX]; /**< 每列一个 XVector<HsfHistPoint>。 */
    int          m_chartRootCount;      /**< 根行数（= 已累加的服务端筛选行数）。 */
    int          m_chartFetchOffset;    /**< 累加分页游标（已取到第几行）。 */
    bool         m_chartFetching;       /**< 累加中（防重入重复发请求）。 */
    bool         m_chartDataDirty;      /**< 全量数据已过期（筛选/刷新后需重取）。 */
    bool         m_chartRebuilding;     /**< 图表重建中（防 viewportChanged 重入）。 */
    bool         m_xLabelFull;          /**< X 轴标签用带年份秒级的完整格式。 */
    int64_t      m_chartBaseTs;         /**< 图表 X 基准时刻（首个有效 ts；0=未定）。 */
    /* ---- 缩放取数（窗口模式）状态 ----
     * [两种取数口径]
     *   · 全量模式：跨区间跳批抽样（~N 点铺满全区间）——未缩放时看全貌；
     *   · 窗口模式：按**可见时间窗**向服务端重取该窗的行（窗内全分辨率）
     *     ——缩放后看细节，曲线连续不尖角。
     * 两种口径的 X 值都以 m_chartBaseTs 为基准（相对秒数），故**窗口模式
     * 换数据不换基准**：换了基准，用户视口与数据 X 值就脱钩，点会落到
     * 轴域外（现场"缩放后只剩左边缘几条竖线"）。 */
    int64_t      m_chartFullLo;         /**< 全量筛选域下界（绝对 epoch ms；0=未知）。 */
    int64_t      m_chartFullHi;         /**< 全量筛选域上界（绝对 epoch ms）。 */
    int64_t      m_chartWinLo;          /**< 已持有数据的窗口下界（绝对 ms；0=全量域数据）。 */
    int64_t      m_chartWinHi;          /**< 已持有数据的窗口上界（绝对 ms）。 */
    bool         m_chartWinMode;        /**< 已持有数据是"窗口模式"（窗内取数）。 */
    bool         m_chartWinSampled;     /**< 窗口数据是窗内抽样（窗内行数超上限时）。 */
    int64_t      m_chartFetchLo;        /**< 在途取数的窗口下界（绝对 ms；0=全量域取数）。 */
    int64_t      m_chartFetchHi;        /**< 在途取数的窗口上界（绝对 ms）。 */
    bool         m_chartFetchWin;       /**< 在途取数是窗口态。 */
    bool         m_chartWantPending;    /**< 有待发起的取数（防抖/在途时排队）。 */
    int64_t      m_chartWantLo;         /**< 待取窗口下界（绝对 ms；0=全量域）。 */
    int64_t      m_chartWantHi;         /**< 待取窗口上界（绝对 ms）。 */
    bool         m_chartWantWin;        /**< 待取是窗口态。 */
    double       m_chartViewLo;         /**< 本轮窗口取数的**轴域**下界（相对基准秒）——
                                             取数窗由它换算、收尾按同一对值重放轴域
                                             （见 HsfChartZoom_view：同源同参）。 */
    double       m_chartViewHi;         /**< 本轮窗口取数的轴域上界。 */
    uint64_t     m_chartWinReqMs;       /**< 上次发起取数的时刻（滚轮防抖）。 */
    int          m_chartLimitSeq;       /**< 取数 limit 轮转序号（防迟到应答串味）。 */
    int          m_chartChunkLimit;     /**< 本轮正式批行数（轮转取值）。 */
    int          m_chartProbeLimit;     /**< 本轮探测批行数（轮转取值）。 */
    int          m_chartTotal;          /**< 服务端本次取数总行数（首批应答记下）。 */
    int          m_chartStep;           /**< 跨区间跳跃步长（行）；0=连续取。 */
    int          m_chartBatches;        /**< 计划批数（覆盖全区间用）。 */
    int          m_chartBatchDone;      /**< 已完成批数。 */
} HsfDataHistory;

/**
 * @brief 按**当前字体实测**表头文字宽度，算出各列的列宽（写入 m_colWidths）。
 * @details [为什么必须实测] 列宽写死像素只在量它的那个字号下成立。客户端
 *          按本地设置放大字号后（板端 ui_scale 1.15+），表头文字比列宽长
 *          ⇒ 放不进 QSS 内缩后的标签矩形 ⇒ 表头被截断甚至整段空白。
 *          Python 端 2026-11-10 踩过同一个坑（历史页 15 列里 12 列表头被
 *          截成"出频率(H"、"度1(NTU"），修法是 DataTable 的 auto_min_texts
 *          ——按当前字体现量，本页同款。
 *          [时间列按**内容**量] 表头只有"时间"两字，实际要装
 *          "2026-11-10 00:00:00"（19 字符）⇒ 用代表内容量。
 *          [余量] 叠 HIST_HEADER_EXTRA_PX（QSS padding + 边框）与粗体余量。
 */
static void histCalcColWidths(HsfDataHistory* self)
{
    XFont font;
    int c;
    if (!self) return;
    font = XWidget_font((const XWidget*)&self->m_table.m_base);
    for (c = 0; c < HSF_HISTORY_DISPLAY_COLS; ++c) {
        const char* sample = (c == 0) ? "2026-11-10 00:00:00"
                                     : kHistoryHeaders[c];
        const int textW = XPainter_textWidth(&font, sample);
        int w = textW + (textW * HIST_HEADER_BOLD_NUM) / 100
                + HIST_HEADER_EXTRA_PX;
        if (w < kHistoryColWidths[c]) w = kHistoryColWidths[c];
        self->m_colWidths[c] = w;
    }
    XClassDeinit(&font);
}

/** @brief 取数内存硬上限（行）。step 采样后实际行数 ≈ 目标点数，
 *         本值只作异常情况（服务端 total 谎报/step 算错）的兜底。
 *  @note  窗口模式要按"窗内每一行"取（曲线连续），行数比全量抽样模式
 *         大一个量级 ⇒ 上限随之抬高。每行 10 个数值列各存一份
 *         HsfHistPoint（24B）⇒ 12000 行约 2.9MB，可接受。 */
#define HIST_CHART_MAX_ROWS   12000
/** @brief 窗口模式下"窗内逐行全分辨率"的行数上限。
 *  @details [为什么有上限] 窗口模式要取**窗内每一行**（不跳批、不丢段），
 *         行数越多批越多、应答槽是**单份静态缓冲**（见 histTimerCb），
 *         一批一应答地轮询取回。行数无上限 ⇒ 取数时间随窗宽线性增长
 *         （库里最密的时段约 400 行/小时，缩放到 22 小时就有近万行）。
 *         故：
 *           · 窗内行数 ≤ 本上限 ⇒ 逐行取（曲线连续、不尖角）；
 *           · 超过 ⇒ 窗内按跳批抽样（曲线铺满视窗但仍尖角），
 *             并在状态行注明"窗内抽样"。
 *         上限与内存上限 HIST_CHART_MAX_ROWS 同值（逐行取的行数不能
 *         超过内存上限，否则会被中途截断成半截窗）。 */
#define HIST_CHART_WIN_FULL_ROWS  HSF_CHART_ZOOM_WIN_FULL_ROWS_DEFAULT
/** @brief 正式批行数（轮转区间见 HsfChartZoom.h 的 HSF_CHART_ZOOM_CHUNK_*）。 */
#define HIST_CHART_CHUNK      HSF_CHART_ZOOM_CHUNK_MAX
/* 编译期兜底：批大小不得与页大小（kHistPageSizes）撞键，探测批与正式批
 * 也不得互撞。改了 HSF_HISTORY_PAGE_MAX 或页大小档位时这里会直接拦住。 */
#if (HSF_CHART_ZOOM_CHUNK_MIN <= 10) || (HSF_CHART_ZOOM_CHUNK_MAX >= 50) || \
    (HSF_CHART_ZOOM_PROBE_MAX >= 10) || \
    (HSF_CHART_ZOOM_PROBE_MAX >= HSF_CHART_ZOOM_CHUNK_MIN)
#error "chart batch limits must be disjoint from page sizes 10/20/50 and from each other"
#endif
/** @brief 取数期间的应答轮询周期（毫秒）。
 *  @details [为什么要临时提速] 应答槽是单份静态缓冲，页面只能靠轮询
 *         取回；页面默认轮询周期＝界面刷新周期（缺省 200ms）⇒ 一批
 *         200ms，近万行的窗要等 40 多秒（用户观感＝"点了没反应"）。
 *         取数期间临时降到本值（取完恢复），一批一跳，几千行秒级到。 */
#define HIST_CHART_FETCH_POLL_MS   25
/** @brief 窗口模式的重绘节奏（每 N 批重画一次）。
 *  @details [为什么要攒批再画] 每批都要"排序+去重+重画"，而窗口模式
 *         可能上百批：每批都排一遍是 O(n log n) 的大头（近万行 × 10 列
 *         × 上百批 ⇒ CPU 全耗在排序上，界面反而卡住）。攒 8 批画一次，
 *         观感上是"曲线一点点铺满"，CPU 只花 1/8。 */
#define HIST_CHART_WIN_REDRAW_BATCH  8
/**
 * @brief 有效墙钟下界（2020-01-01；与服务端 HSF_STORE_VALID_TS_MIN 同值）。
 * @details [为什么图表要单独过滤] 历史库里残留着旧版本误写的 monotonic
 *          坏行（ts≈开机毫秒，落在 1970 年）。服务端"最早/最新"统计已
 *          过滤它们，但**分页查询本身不过滤**——跨全区间抽样会把最近
 *          区尾和这些 1970 年行一起取到，X 轴被拉成 1.8e9 秒（约 58 年），
 *          现场看到的就是"一条压在底部的直线"（实测 2026-11-07）。
 *          图表侧统一丢弃 ts < 本门限的行。
 */
#define HIST_CHART_VALID_TS_MIN  1577836800000LL

/** @brief 全量导出拉取的单批行数（固定值，独占一档 limit）。 */
#define HIST_EXPORT_CHUNK   40
/** @brief 导出批的应答看门狗拍数（25ms × 40 ≈ 1s 没等到就重发）。 */
#define HIST_EXPORT_WATCHDOG_TICKS  40
/** @brief 导出批最多重发次数（连续不回才放弃）。 */
#define HIST_EXPORT_MAX_RETRIES     3
/* 编译期兜底：导出批 limit 必须与页大小 10/20/50、图表探测批 1..8、
 * 图表正式批 41..49 两两不相交（应答槽单份，页面只能按服务端回显的
 * (offset,limit) 认领，见 histTimerCb）。档位动了这里直接编译失败，
 * 不把串台拖到运行期。 */
#if (HIST_EXPORT_CHUNK <= 10) || (HIST_EXPORT_CHUNK == 20) || \
    (HIST_EXPORT_CHUNK == 50) || (HIST_EXPORT_CHUNK >= HIST_CHART_CHUNK) || \
    (HIST_EXPORT_CHUNK <= HSF_CHART_ZOOM_PROBE_MAX)
#error "export chunk limit must be disjoint from page sizes and chart batch ranges"
#endif

/* ==================== 填表 ==================== */

/** @brief 数值格解析：纯数字才认（"--"/文本 → ok=false）。 */
static double histCellValue(const char* text, bool* ok)
{
    char* end = NULL;
    double v;
    if (ok) *ok = false;
    if (!text || !text[0] || strcmp(text, "--") == 0) return 0.0;
    v = strtod(text, &end);
    if (!end || *end != '\0') return 0.0;
    if (ok) *ok = true;
    return v;
}

/** @brief 按「显示小数位数」重排一个格（只动数值列；文本列原样）。 */
static void histFormatCell(int col, const char* src, int digits,
                           char* dst, size_t cap)
{
    bool ok = false;
    double v;
    if (!dst || cap == 0) return;
    if (col < HIST_NUM_FIRST || col > HIST_NUM_LAST || digits < 0) {
        snprintf(dst, cap, "%s", src ? src : "");
        return;
    }
    v = histCellValue(src, &ok);
    if (!ok) {
        snprintf(dst, cap, "%s", src ? src : "");
        return;
    }
    if (digits > 6) digits = 6;
    snprintf(dst, cap, "%.*f", digits, v);
}

/** @brief 本地时区偏移（毫秒）：日历型 XDateTime − 墙钟 epoch。
 *  @note  历史表的单元格文本按本地时间拼（HsfLinkHistory 用"现在 − 年龄"），
 *         而 setMSecsSinceEpoch 只做 UTC 日历展开；两边不合的话时间框
 *         会比表里少 8 小时（实测）。 */
static int64_t histTzOffsetMs(void)
{
    XDateTime now = XDateTime_currentDateTime();
    return XDateTime_toMSecsSinceEpoch(&now) -
           XDateTime_currentWallMSecsSinceEpoch();
}

/** @brief 用**本页自己的那份应答**重填表格 + 状态行。
 *  @param self 页面。
 *  @param page 已按 offset/limit 归属确认为"本页那一页"的应答（见 histTimerCb）。
 *  @details [为什么必须传指针而不是内部读共享槽] 历史应答只有一份静态缓冲
 *           （HsfLinkClient_history()），随时会被图表批/导出的应答覆盖；
 *           铺表时再回头读它，就可能把图表批的 50 行当成表格页（复审②的
 *           另一个入口）。调用方负责确认归属。 */

/**
 * @brief 双击表头：排序当前页（口径与 Python 客户端 _header_dblclicked 一致：
 *        新列⇒降序；同列降⇒升序；同列升⇒取消排序）。
 */
/* [前向声明] 取消排序分支要回调请求函数（定义在下方）。 */
static void histRequest(HsfDataHistory* self, int offset);

static void HsfDataHistoryHeaderDblClicked(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)receiver;
    int col = -1;
    if (!self) return;
    if (args) {
        XVarList_start(args);
        col = XVarList_arg(args, int);
    }
    HsfTableSort_toggle(&self->m_sort, col);
    if (self->m_sort.m_col < 0) {
        /* [取消排序 ⇒ 回到服务端原序] 表里此刻已是排过序的行，本地没有
         * 原序可还原 ⇒ 重取当前页（服务端按 ts 降序给回原序）。 */
        histRequest(self, self->m_offset);
        return;
    }
    HsfTableSort_apply(&self->m_sort, &self->m_table);
    HsfTableSort_updateIndicator(&self->m_sort, &self->m_table);
}

static void histFillTable(HsfDataHistory* self, const HsfHistoryPage* page)
{
    int i;
    int c;

    if (!self || !page || !page->m_valid) return;
    XTableWidget_setRowCount(&self->m_table, page->m_count);
    for (i = 0; i < page->m_count; ++i) {
        for (c = 0; c < HSF_HISTORY_DISPLAY_COLS; ++c) {
            char text[48];
            XTableWidgetItem* it;
            histFormatCell(c, page->m_rows[i].m_cells[c], self->m_digits,
                           text, sizeof(text));
            it = XTableWidgetItem_create_utf8(text, 0);
            if (!it) continue;
            XTableWidget_setItem(&self->m_table, i, c, it);
            /* [异常行高亮] 采样那刻存在异常（故障/浊度越限/压差越限）——
             * 与 Python 客户端同口径：整行浅红底，现场一眼扫出异常段。 */
            if (page->m_rows[i].m_abnormal) {
                XColor bg;
                XColor_init_rgb(&bg, 0xFD, 0xE7, 0xE9, 0xFF);
                XTableWidgetItem_setBackground(it, &bg);
            }
        }
    }
    self->m_shownOffset = page->m_offset;
    self->m_shownCount = page->m_count;
    self->m_shownSeq = page->m_seq;
    /* [翻页判界的事实源] 页码/总行数存本页自己的快照：共用应答槽里随时可能
     * 被别的请求（图表批/导出）的应答覆盖，翻页判界不能回头读它。 */
    self->m_shownTotal = page->m_total;
    {
        const int limit = (page->m_limit > 0) ? page->m_limit : self->m_pageSize;
        const int cur = (limit > 0) ? (page->m_offset / limit + 1) : 1;
        const int pages = (limit > 0) ? ((page->m_total + limit - 1) / limit) : 1;
        char buf[192];
        /* [与 Python 同口径] 共 N 条（本页 M）· 第 x/y 页 · 库内共 K 条。 */
        snprintf(buf, sizeof(buf),
                 "共 %d 条（本页 %d） · 第 %d/%d 页 · 库内共 %lld 条%s",
                 page->m_total, page->m_count, cur, pages,
                 (long long)page->m_dbRows,
                 page->m_count ? "" : "（无数据）");
        XLabel_setText_2(&self->m_status, buf);
    }
    if (self->m_bar)
        HsfDataBar_setTotalRows(self->m_bar, page->m_total);
    /* [时间框回填] 未手动筛过时，把起止框回填成实际数据区间——
     * 看得见的就是当前视图的全部，不让人误以为"只看了一瞬间"。 */
    if (!self->m_pickersSynced && self->m_bar && page->m_earliestTs > 0) {
        XDateTime dt;
        XDateTime_setMSecsSinceEpoch(&dt, page->m_earliestTs + self->m_tzOffsetMs);
        HsfDataBar_setStartDateTime(self->m_bar, &dt);
        if (page->m_latestTs > 0)
            XDateTime_setMSecsSinceEpoch(&dt,
                                         page->m_latestTs + self->m_tzOffsetMs);
        HsfDataBar_setEndDateTime(self->m_bar, &dt);
        self->m_pickersSynced = true;
    }
    XWidget_setEnabled((XWidget*)&self->m_prev, self->m_shownOffset > 0);
    XWidget_setEnabled((XWidget*)&self->m_next,
                       self->m_shownOffset + self->m_shownCount < page->m_total);
    XWidget_update((XWidget*)&self->m_table);
}

/* ==================== 翻页 ==================== */

/** @brief 当前「可见列」集合（与 Python `visible_columns()` 同口径）。
 *  @details [单一口径] 本页的列显示是**下拉单选**（0=全部列，
 *           1..HSF_HISTORY_DISPLAY_COLS=列 0..N-1），不像 Python 是多选
 *           复选；但语义完全一致：可见列同时决定三件事——① 表格铺哪几列
 *           （histApplyColumnFilter 的 setColumnHidden）；② 关键字在哪些列
 *           里比对（服务端 kwCols）；③ 导出写哪几列。
 *           [为什么必须收成一个函数] 旧实现三处各写一份：表格走下拉、
 *           关键字走下拉、导出**硬编码 15 列全量**。任何一处改了另一处
 *           就漂移——实锅：用户把 12 列收成 1 列再导出，CSV 仍是 15 列
 *           全量（与 Python `_write_export` 按 `visible_columns()` 取列
 *           的"导出=看到的样子"口径相反）。
 *  @param self 页面。
 *  @param out  输出数组（容量 >= HSF_HISTORY_DISPLAY_COLS）。
 *  @return 写入的列数（至少 1：下拉为空/全部时回退全部列，与 Python
 *          `return cols or list(range(len(self.COLUMNS)))` 一致）。 */
static int histVisibleCols(const HsfDataHistory* self, uint32_t* out)
{
    int idx;
    int n = 0;
    if (!out) return 0;
    idx = self && self->m_bar ? HsfDataBar_columnFilterIndex(self->m_bar) : 0;
    if (idx > 0 && idx <= HSF_HISTORY_DISPLAY_COLS) {
        /* 下拉 0="全部列"，1..N=列 0..N-1 */
        out[n++] = (uint32_t)(idx - 1);
        return n;
    }
    while (n < HSF_HISTORY_DISPLAY_COLS) { out[n] = (uint32_t)n; ++n; }
    return n;
}

/** @brief 组装关键字比对列下标（= 可见列）。
 *  @details [筛选无效根因之二] 原实现恒发 cols[0..14]（全部 15 列），
 *           用户在「列筛选」里选了某一列、再按关键字搜，服务端仍按全部
 *           列匹配 ⇒ 命中面比用户预期大得多；更严重的是这等于把"列筛选"
 *           完全排除在查询条件之外（只做了客户端 setColumnHidden）。
 *           Python 端 `kw_cols = self.visible_columns()`，故此处直接复用
 *           可见列口径，不另立一套。 */
static int histKwCols(const HsfDataHistory* self, uint32_t* out)
{
    return histVisibleCols(self, out);
}

/** @brief 按当前取数状态设置应答轮询周期（取数期间提速，空闲恢复）。
 *  @details [为什么收口成一个函数] 应答槽是单份静态缓冲，页面靠轮询取
 *           回（见 histTimerCb）；图表累加与全量导出期间临时降到
 *           HIST_CHART_FETCH_POLL_MS，一批一跳。三处写周期的调用点
 *           （中止图表/取数收尾/导出收尾）必须算**同一个**口径——否则
 *           一方收尾把另一方在途的提速覆盖回慢拍，取数观感"点了没反应"。
 */
static void histTimerIntervalApply(HsfDataHistory* self)
{
    if (!self || !self->m_timer) return;
    XTimer_setInterval(self->m_timer,
                       (self->m_chartFetching || self->m_exportFetching)
                           ? HIST_CHART_FETCH_POLL_MS
                           : HsfSettingsLocal_refreshMs());
}

/** @brief 中止图表全量累加（数据作废、在途批应答无主、轮询周期重算）。
 *  @details [串扰防护 + 中止即作废] 图表全量累加与表格分页/全量导出共用
 *           同一个应答槽（HsfLinkClient_history()）。若图表还在累加时
 *           用户翻页/改筛选/点导出，新应答会被 histTimerCb 的图表分支
 *           当成图表批次吞掉，别的消费者就饿死了。故任何其它请求发出前
 *           先走这里中止图表累加。
 *           [为什么必须标脏] 已经累加进来的那几批只是**半截数据**（例如
 *           10 批里只取到第 1 批的 50 行）。旧实现只清 m_chartFetching
 *           不标脏，于是 histChartFetchStart 命中「未过期且有数据」的
 *           早退分支，直接把半截曲线当完整结果显示（复审①：「生成折线
 *           图→翻页→再生成」画出半截并标「共 50 行」）。中止 = 数据作废，
 *           下次生成必须整批重取。 */
static void histChartAbort(HsfDataHistory* self)
{
    if (!self || !self->m_chartFetching) return;
    self->m_chartFetching = false;
    self->m_chartDataDirty = true;
    self->m_chartReqValid = false;   /* 在途图表批的应答从此无主 */
    histTimerIntervalApply(self);
}

/** @brief 发起一页请求（offset 为该页起点；关键字/列/页大小随请求下发）。 */
static void histRequest(HsfDataHistory* self, int offset)
{
    HistCols cols;
    int n;
    HIST_COLS_SIZE_ASSERT(cols);
    if (!self) return;
    if (offset < 0) offset = 0;
    if (self->m_pageSize <= 0) self->m_pageSize = HIST_PAGE_LIMIT;
    n = histKwCols(self, cols);
    self->m_offset = offset;
    /* 任何普通分页请求发出即中止图表累加（见 histChartAbort 的串扰防护
     * 注记）；全量导出不中止——它的内容已定格、批 limit 与页请求不相交，
     * 在途导出批的应答归属不受影响（看门狗兜住被顶掉的那一拍，见
     * histTimerCb）。 */
    histChartAbort(self);
    /* [数据窗随筛选作废] 筛选条件/时间窗变了 ⇒ 已持有的图表数据窗属于
     * 旧条件，必须清掉（否则下次取数还按旧窗要行）；全量域也要重新学
     * （旧跨度是旧筛选条件的跨度）。 */
    self->m_chartWinLo = 0;
    self->m_chartWinHi = 0;
    self->m_chartWinMode = false;
    self->m_chartWinSampled = false;
    self->m_chartViewLo = 0.0;
    self->m_chartViewHi = 0.0;
    self->m_chartFullLo = 0;
    self->m_chartFullHi = 0;
    self->m_chartWantPending = false;
    self->m_chartWantWin = false;
    self->m_chartWantLo = 0;
    self->m_chartWantHi = 0;
    self->m_chartFetchWin = false;
    self->m_chartFetchLo = 0;
    self->m_chartFetchHi = 0;
    /* 在途图表批的应答从此无主（见 histTimerCb 的归属判定）：作废它的
     * 描述符；且图表批 limit=41..49 与页大小 10/20/50 恒不相等（见该宏
     * 注释），晚到的批次应答不可能再通过 isPageReply 铺进表格
     * （复审②的根因）。 */
    self->m_chartReqValid = false;
    self->m_pageReqValid = false;
    if (!HsfLinkClient_historyQueryWindow(offset, self->m_pageSize, false,
                                          self->m_kwText, cols, n,
                                          self->m_loTs, self->m_hiTs)) {
        /* [失败终局可观测] 连接未就绪/会话缺失时请求根本发不出去：
         * 以前静默 return，界面停在"筛选中…"看不出原因。 */
        XLabel_setText_2(&self->m_status, "查询失败：服务端未连接");
        XPrintf("HsfDataHistory: history query failed (offset=%d, limit=%d)\n",
                offset, self->m_pageSize);
        return;
    }
    /* 登记在途分页请求（服务端把 offset/limit 原样回显，应答据此认领）。 */
    self->m_pageReqOffset = offset;
    self->m_pageReqLimit = self->m_pageSize;
    self->m_pageReqValid = true;
}

/** @brief 上一页。 */
static void histPrevSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)((char*)receiver -
                                            offsetof(HsfDataHistory, m_prev));
    (void)args;
    if (!self) return;
    if (self->m_offset <= 0) return;
    histRequest(self, self->m_offset - self->m_pageSize);
}

/** @brief 历史页导出取格上下文（网格列=可见列序 → 行内格）。 */
typedef struct HistExportCtx {
    const HsfHistoryPage* page;   /**< 服务端返回的那一页。 */
    const uint32_t* cols;         /**< 可见列下标（升序）。 */
    int              colCount;    /**< cols 长度。 */
} HistExportCtx;

/** @brief 导出取格回调：网格列（可见列序）→ 行内格文本（UTF-8）。 */
static bool histExportCell(void* user, int row, int col, char* buf, int cap)
{
    const HistExportCtx* ctx = (const HistExportCtx*)user;
    int c;
    if (buf && cap > 0) buf[0] = '\0';
    if (!ctx || !ctx->page || !ctx->cols || !buf || cap <= 0) return false;
    if (row < 0 || row >= ctx->page->m_count) return false;
    if (col < 0 || col >= ctx->colCount) return false;
    c = (int)ctx->cols[col];
    if (c < 0 || c >= HSF_HISTORY_DISPLAY_COLS) return false;
    snprintf(buf, (size_t)cap, "%s", ctx->page->m_rows[row].m_cells[c]);
    return true;
}

/* ==================== 全量导出（文件对话框 + 逐批拉全） ====================
 * [口径] 导出 = **按当前筛选条件的全部行**（与图表全量取数同一条路：同
 * 一组 WHERE 逐批拉），不是表格当前页那 10/20/50 行。取数与落盘解耦：
 *  · 格式由**文件对话框的过滤器档位**决定（"Excel 工作簿 (*.xlsx);;
 *    CSV (*.csv)"，与 Python 客户端同串同序）——工具条上不再有「导出
 *    格式」下拉（那颗下拉曾是选了不兑现的死控件）。
 *  · 行走共享模块的流式写出器（HsfExportWriter）：来一批写一批，
 *    CSV 常驻内存 O(1)；XLSX 收尾才 saveAs。
 *  · 筛选条件/列集合/路径/格式在发起那一刻定格（m_export* 快照），
 *    等待期间用户改界面不影响在途导出的内容。 */

/** @brief XLSX 全量导出的行数上限（in-memory 工作簿的内存预算）。
 *  @details [为什么 XLSX 有上限而 CSV 没有] CSV 是流式写盘（写出器只
 *           持有一行缓冲），多少行都不涨内存；XLSX 的 zip 包必须整体
 *           写出（finish 时才 saveAs），取数期间整个工作簿都在内存——
 *           行数无上限会把板端内存吃穿。上限对齐图表全量取数的内存预算
 *           （HIST_CHART_MAX_ROWS）。触顶按**已取部分**收尾导出，状态行
 *           注明截断——绝不静默少行。CSV 无此上限（全量到底）。 */
#define HIST_EXPORT_XLSX_MAX_ROWS  HIST_CHART_MAX_ROWS

/** @brief 按定格列集合拼表头数组（写文件的第一行）。 */
static void histExportHeaders(const uint32_t* cols, int colCount,
                              const char** headers, int cap)
{
    int k;
    for (k = 0; k < cap; ++k) {
        const int c = (k < colCount) ? (int)cols[k] : -1;
        headers[k] = (c >= 0 && c < HSF_HISTORY_DISPLAY_COLS)
                         ? kHistoryHeaders[c] : "";
    }
}

/** @brief 收尾导出（写完 or 中途放弃）：落盘/清理 + 状态行 + 周期重算。
 *  @param complete true=按计划写完（finish 收尾：XLSX saveAs/CSV 关闭）；
 *                  false=中途放弃（abort：删半成品 CSV/丢弃 XLSX 文档）。
 *  @param failNote 失败文案（complete=false 时用；NULL=保留状态行现文——
 *                  服务端失败包络的原文已由调用方写好，别盖掉）。 */
static void histExportDone(HsfDataHistory* self, bool complete,
                           const char* failNote)
{
    char buf[300];
    bool ok = false;
    if (!self) return;
    if (complete) ok = HsfExportWriter_finish(&self->m_exportWriter);
    else HsfExportWriter_abort(&self->m_exportWriter);
    self->m_exportFetching = false;
    self->m_exportIdle = 0;
    self->m_exportRetries = 0;
    histTimerIntervalApply(self);
    if (!complete) {
        if (failNote) XLabel_setText_2(&self->m_status, failNote);
        return;
    }
    if (!ok) {
        XLabel_setText_2(&self->m_status, "导出失败：文件写入错误");
        return;
    }
    snprintf(buf, sizeof(buf), "已导出 %d 行 × %d 列 → %s%s",
             self->m_exportWritten, self->m_exportColCount,
             HsfExportWriter_path_const(&self->m_exportWriter),
             self->m_exportTruncated ? "（超出 Excel 上限，已截断）" : "");
    XLabel_setText_2(&self->m_status, buf);
}

/** @brief 发一批导出请求（按**定格**的筛选/列集合；offset 原样回显认领）。
 *  @return 请求已发出返回 true；发出失败由调用方收尾。 */
static bool histExportRequest(HsfDataHistory* self, int offset)
{
    if (!self) return false;
    if (!HsfLinkClient_historyQueryWindow(offset, HIST_EXPORT_CHUNK, true,
                                          self->m_exportKw,
                                          self->m_exportCols,
                                          self->m_exportColCount,
                                          self->m_exportLoTs,
                                          self->m_exportHiTs))
        return false;
    /* [应答归属] 服务端把 offset/limit 原样回显；导出批 limit 恒
     * HIST_EXPORT_CHUNK（40），与页 10/20/50、图表批 1..8/41..49 两两
     * 不相交（编译期锁见 HIST_EXPORT_CHUNK），晚到的别家应答认不走。 */
    self->m_exportOffset = offset;
    self->m_exportLimit = HIST_EXPORT_CHUNK;
    self->m_exportIdle = 0;
    return true;
}

/** @brief 导出批应答到达：写一批 → 推进下一批 → 取完收尾（连续取，不跳批）。
 *  @details [判界] 短批（got < limit）= 服务端在这个 offset 已到数据尾
 *           ⇒ 本批即末批；written >= total（首批记下）同理。导出与图表
 *           取数不同：**不采样**，offset 按行数连续推进，一行不落。 */
static void histExportContinue(HsfDataHistory* self,
                               const HsfHistoryPage* page)
{
    HistExportCtx ctx;
    char prog[160];
    int got;
    int i;
    bool done;
    if (!self || !page || !page->m_valid) {
        histExportDone(self, false, "导出失败：查询失败");
        return;
    }
    self->m_exportIdle = 0;
    got = page->m_count;
    if (got > 0) {
        ctx.page = page;
        ctx.cols = self->m_exportCols;
        ctx.colCount = self->m_exportColCount;
        for (i = 0; i < got; ++i) {
            /* [XLSX 内存闸] 工作簿在收尾前都在内存，行数超上限按已取
             * 部分收尾（状态行注明截断）；CSV 不设限。 */
            if (self->m_exportFormat == HsfExportFormat_Xlsx &&
                self->m_exportWritten >= HIST_EXPORT_XLSX_MAX_ROWS) {
                self->m_exportTruncated = true;
                break;
            }
            if (!HsfExportWriter_writeRow(&self->m_exportWriter, i,
                                          histExportCell, &ctx)) {
                histExportDone(self, false, "导出失败：文件写入错误");
                return;
            }
            ++self->m_exportWritten;
        }
    }
    if (self->m_exportTotal <= 0 && page->m_total > 0)
        self->m_exportTotal = page->m_total;
    done = self->m_exportTruncated || got <= 0 ||
           got < self->m_exportLimit ||
           (self->m_exportTotal > 0 &&
            self->m_exportWritten >= self->m_exportTotal);
    if (done) {
        histExportDone(self, true, NULL);
        return;
    }
    snprintf(prog, sizeof(prog), "正在导出… 已写 %d 行 / 共 %d 行",
             self->m_exportWritten, self->m_exportTotal);
    XLabel_setText_2(&self->m_status, prog);
    if (!histExportRequest(self, self->m_exportOffset + got)) {
        histExportDone(self, false, "导出失败：服务端未连接");
    }
}

/** @brief 发起导出（工具条「导出」）：无权限→状态行拒绝；有权限→弹
 *  文件对话框（格式=过滤器档位）→ 按当前筛选条件全量逐批拉取落盘。
 *  @details [闸在服务端] 界面只做提示，真正验权在服务端（每一批都带
 *           export=true，帧会被拒）——绕过界面直发帧也拿不到数据。
 *  @details [对话框取消不误伤] 用户在保存框按了取消（返回空串）＝本次
 *           不导出，界面保持原样（连状态行都不动），不发任何请求。 */
static void histExportStart(HsfDataHistory* self)
{
    HistCols cols;
    const char* headers[HSF_HISTORY_DISPLAY_COLS];
    XString* chosen = NULL;
    int filterIdx = 0;
    int n;
    char defName[96];
    XDateTime now;
    XDate d;
    XTime t;
    HIST_COLS_SIZE_ASSERT(cols);
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_DOWNLOAD)) {
        XLabel_setText_2(&self->m_status,
                         "权限不足：下载数据需要普通用户及以上账户");
        return;
    }
    if (self->m_exportFetching) {
        XLabel_setText_2(&self->m_status, "正在导出中，请等本轮完成");
        return;
    }
    /* [图表累加让路] 同一应答槽单飞：先中止图表累加（histChartAbort
     * 作废其描述符并标脏），导出收尾后图表按需整批重取。 */
    histChartAbort(self);
    /* [格式=对话框过滤器档位] 框架文件对话框（Qt 同名 API 面）；过滤器
     * 串与 Python 客户端同串同序：0=Excel 工作簿、1=CSV。 */
    now = XDateTime_currentDateTime();
    d = XDateTime_date(&now);
    t = XDateTime_time(&now);
    snprintf(defName, sizeof(defName),
             "./hsf_history_%04d%02d%02d_%02d%02d%02d",
             XDate_year(&d), XDate_month(&d), XDate_day(&d),
             XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
    chosen = XFileDialog_getSaveFileName_2(
        (XWidget*)&self->m_base, "导出历史数据", defName,
        "Excel 工作簿 (*.xlsx);;CSV (*.csv)", &filterIdx);
    if (chosen) {
        const char* u8 = XString_toUtf8(chosen);
        snprintf(self->m_exportPath, sizeof(self->m_exportPath), "%s",
                 u8 ? u8 : "");
        XClassDelete(chosen);
    } else {
        self->m_exportPath[0] = '\0';
    }
    if (!self->m_exportPath[0]) return;   /* 用户取消：静默收场 */
    /* 剥掉用户输入的尾缀，按定格格式追加规范扩展名（HsfExport_writer） */
    HsfExport_stripKnownExt(self->m_exportPath);
    self->m_exportFormat = HsfExport_formatFromFilterIndex(filterIdx);
    /* [定格] 列集合 + 筛选条件快照（后续批次全按快照下发）。 */
    n = histVisibleCols(self, cols);
    if (n <= 0) return;
    {
        int k;
        for (k = 0; k < n && k < HSF_HISTORY_DISPLAY_COLS; ++k)
            self->m_exportCols[k] = cols[k];
        self->m_exportColCount = n;
    }
    snprintf(self->m_exportKw, sizeof(self->m_exportKw), "%s", self->m_kwText);
    self->m_exportLoTs = self->m_loTs;
    self->m_exportHiTs = self->m_hiTs;
    /* 打开写出器（BOM/建文档 + 表头）。 */
    histExportHeaders(self->m_exportCols, n, headers,
                      HSF_HISTORY_DISPLAY_COLS);
    if (!HsfExportWriter_open(&self->m_exportWriter, self->m_exportFormat,
                              self->m_exportPath, "历史数据", headers, n)) {
        XLabel_setText_2(&self->m_status, "导出失败：文件无法创建");
        return;
    }
    self->m_exportFetching = true;
    self->m_exportTotal = 0;
    self->m_exportWritten = 0;
    self->m_exportTruncated = false;
    self->m_exportIdle = 0;
    self->m_exportRetries = 0;
    XLabel_setText_2(&self->m_status, "正在导出…");
    histTimerIntervalApply(self);   /* 取数期间提速轮询（一批一跳） */
    if (!histExportRequest(self, 0))
        histExportDone(self, false, "导出失败：服务端未连接");
}

static void histApplyFilter(HsfDataHistory* self);

static void histNextSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)((char*)receiver -
                                            offsetof(HsfDataHistory, m_next));
    (void)args;
    if (!self) return;
    /* [判界用本页快照] 共享应答槽最后落进来的可能是图表批/导出的应答，
     * 拿它的行数/总数判"还有没有下一页"会误判（复审②里页码跳到第 401 页
     * 就是按图表批的 offset 算出来的）。m_shown* 是铺表那一刻的事实。 */
    if (self->m_shownCount <= 0) return;
    if (self->m_shownOffset + self->m_shownCount >= self->m_shownTotal) return;
    histRequest(self, self->m_offset + self->m_pageSize);
}

/* ==================== 轮询 ==================== */

/** @brief 应用「列筛选」下拉：只显示选中的那一列（0/-1=全部列）。 */
static void histApplyColumnFilter(HsfDataHistory* self)
{
    int idx;
    int c;
    if (!self || !self->m_bar) return;
    idx = HsfDataBar_columnFilterIndex(self->m_bar);
    for (c = 0; c < HSF_HISTORY_DISPLAY_COLS; ++c) {
        const bool hide = (idx > 0 && c != idx - 1);
        if (XTableView_isColumnHidden(&self->m_table.m_base, c) != hide)
            XTableView_setColumnHidden(&self->m_table.m_base, c, hide);
    }
}

/** @brief 筛选：工具条时间窗 + 关键字 + 列 → 服务端筛，回第 1 页。 */
static void histApplyFilter(HsfDataHistory* self)
{
    XDateTime lo;
    XDateTime hi;
    int64_t loMs;
    int64_t hiMs;
    const char* text;
    if (!self || !self->m_bar) return;
    lo = HsfDataBar_startDateTime(self->m_bar);
    hi = HsfDataBar_endDateTime(self->m_bar);
    loMs = XDateTime_isValid(&lo) ? XDateTime_toMSecsSinceEpoch(&lo) : 0;
    hiMs = XDateTime_isValid(&hi) ? XDateTime_toMSecsSinceEpoch(&hi) : 0;
    /* [本地日历 → 墙钟 epoch] 时间框存的是本地日历时刻，而库里 ts 是
     * UTC epoch 毫秒（XDateTime_toMSecsSinceEpoch 只做日历展开，不带
     * 时区）——差一个本地时区偏移。与回填路径（m_earliestTs +
     * m_tzOffsetMs 显示）互逆，故此处减。 */
    if (loMs > 0) loMs -= self->m_tzOffsetMs;
    if (hiMs > 0) hiMs -= self->m_tzOffsetMs;
    if (loMs > 0 && hiMs > 0 && loMs > hiMs) {
        XLabel_setText_2(&self->m_status, "起始时间晚于结束时间，未筛选");
        return;
    }
    self->m_loTs = loMs;
    self->m_hiTs = hiMs;
    self->m_pickersSynced = true;
    text = XLineEdit_text(&self->m_kw);
    snprintf(self->m_kwText, sizeof(self->m_kwText), "%s", text ? text : "");
    histApplyColumnFilter(self);
    /* [图表全量数据随筛选作废] 图表口径=筛选后的全部数据，条件变了
     * 就必须重拉（否则图表还画着上一组条件的结果）。 */
    self->m_chartDataDirty = true;
    histRequest(self, 0);   /* 筛选后回第 1 页（与 Python _page_index=0 同）。 */
    XLabel_setText_2(&self->m_status, "筛选中…");
}

/** @brief 清除筛选：清关键字 + 时间窗回填成实际数据全区间（不设界）。
 *  @note  时间框回填成实际数据区间（看得见的就是全部），不复位成
 *         "过去 24h"——那会让人以为只在看一天。
 *  @note  [不误覆盖用户输入] 与 Python `_clear_filter` 同口径：清的是
 *         时间/关键字这些**筛选条件**。两类东西不能顺带复位：
 *         ① "显示小数位数"是**显示设置**（用户调好的小数位被"清除筛选"
 *            改回 2 位，与"我只想清时间范围，结果列也全回来了"同一类
 *            莫名行为）；
 *         ② **列选择**同理——Python `_clear_filter` 的注释写得明白：
 *            "列多选是视图设置（看哪些列），不是筛选条件；点「清除筛选」
 *            把用户的列选择一并复位会让人莫名其妙"。本页旧实现调
 *            `setColumnFilterIndex(0)` 强行复位成"全部列"，与 Python
 *            相反（实锅：只清时间范围，结果列的收窄也一起消失了）。 */
static void histClearFilter(HsfDataHistory* self)
{
    const HsfHistoryPage* page;
    XDateTime dt;
    XDateTime now;
    if (!self) return;
    self->m_loTs = 0;
    self->m_hiTs = 0;
    self->m_kwText[0] = '\0';
    self->m_pickersSynced = true;
    XLineEdit_setText(&self->m_kw, "");
    if (self->m_bar) {
        page = HsfLinkClient_history();
        now = XDateTime_currentDateTime();
        if (page && page->m_valid && page->m_latestTs > 0) {
            XDateTime_setMSecsSinceEpoch(&dt,
                                         page->m_latestTs + self->m_tzOffsetMs);
            HsfDataBar_setEndDateTime(self->m_bar, &dt);
            if (page->m_earliestTs > 0) {
                XDateTime_setMSecsSinceEpoch(&dt, page->m_earliestTs +
                                                 self->m_tzOffsetMs);
                HsfDataBar_setStartDateTime(self->m_bar, &dt);
            }
        } else {
            HsfDataBar_setEndDateTime(self->m_bar, &now);
        }
    }
    /* 列筛选保持用户当前选择（见上方 ②）——但表格隐藏态仍是既有值，
     * 这里不需要再调 histApplyColumnFilter（它是幂等的视图同步，与本
     * 次"清筛选"无关）。 */
    self->m_chartDataDirty = true;
    histRequest(self, 0);
    /* [文案别谎报] 列筛选没复位，说"显示全部"会让人以为列也回来了。 */
    XLabel_setText_2(&self->m_status, "已清除筛选（时间/关键字）");
}

/** @brief 关键字输入框回车 = 应用筛选（沿用当前时间窗）。 */
static void histKwReturnSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)((char*)receiver -
                                            offsetof(HsfDataHistory, m_kw));
    (void)args;
    histApplyFilter(self);
}

/** @brief 改每页条数：回第 1 页（行号口径变了，停在原页号没意义）。 */
static void histPageSizeSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)((char*)receiver -
                                            offsetof(HsfDataHistory, m_pageSizeBox));
    int idx;
    (void)args;
    if (!self) return;
    idx = XComboBox_currentIndex(&self->m_pageSizeBox);
    if (idx < 0 || idx >= 3) return;
    if (kHistPageSizes[idx] == self->m_pageSize) return;
    self->m_pageSize = kHistPageSizes[idx];
    histRequest(self, 0);
}

/* ==================== 图表：全量筛选结果 + 等距降采样 ==================== */

/** @brief 释放/清空全量图表数据（幂等；任何重建前先调）。
 *  @param self      页面。
 *  @param keepBase  true=保留 X 基准时刻（窗口模式换数据不换基准：
 *                  换了基准，用户视口与数据 X 值脱钩 ⇒ 点落到轴域外，
 *                  现场"缩放后只剩左边缘几条竖线"就是这个错位）；
 *                  false=清掉基准（全量模式重新取基准）。
 */
static void histChartDataClear(HsfDataHistory* self, bool keepBase)
{
    int k;
    if (!self) return;
    for (k = 0; k < HIST_SERIES_MAX; ++k) {
        if (self->m_chartPts[k])
            XVector_clear_base((XContainer*)self->m_chartPts[k]);
    }
    self->m_chartRootCount = 0;
    self->m_chartFetchOffset = 0;
    self->m_chartFetching = false;
    self->m_chartReqValid = false;   /* 在途图表批的描述符随之作废 */
    if (!keepBase) self->m_chartBaseTs = 0;
    self->m_chartTotal = 0;
    self->m_chartStep = 0;
    self->m_chartBatches = 0;
    self->m_chartBatchDone = 0;
}

/** @brief 保证 10 个列容器已就绪（惰性建；随页对象释放）。 */
static int histPointCompare(const void* a, const void* b);
static bool histChartDataEnsure(HsfDataHistory* self)
{
    int k;
    if (!self) return false;
    for (k = 0; k < HIST_SERIES_MAX; ++k) {
        if (!self->m_chartPts[k]) {
            self->m_chartPts[k] = XVector_create(sizeof(HsfHistPoint));
            if (!self->m_chartPts[k]) return false;
            /* 排序比较器（XVector_sort_base 读容器上的 XContainerCompare）。 */
            XContainerSetCompare(self->m_chartPts[k], histPointCompare);
        }
    }
    return true;
}

/** @brief 把一页应答的数值原值累加进全量容器（X=相对基准时刻的秒数，
 *         见 HsfHistPoint 的 m_x 注释；m_ts 才是原始 epoch 毫秒）。 */
static void histChartDataAppendPage(HsfDataHistory* self,
                                    const HsfHistoryPage* page)
{
    int i;
    int k;
    if (!self || !page || !page->m_valid) return;
    if (!histChartDataEnsure(self)) return;
    for (i = 0; i < page->m_count; ++i) {
        const HsfHistoryRow* row = &page->m_rows[i];
        if (row->m_ts <= 0) continue;
        /* 丢弃 1970 年坏行（旧版本 monotonic ts 残留在库里；见
         * HIST_CHART_VALID_TS_MIN 注释）。 */
        if (row->m_ts < HIST_CHART_VALID_TS_MIN) continue;
        if (self->m_chartRootCount >= HIST_CHART_MAX_ROWS) break;
        /* 基准时刻 = 首条有效行（服务端按 ts 降序取页、客户端累加顺序即
         * 时间倒序；取首个即"最新一条"作为 0 点，往左为更早）。X 轴刻度
         * 由 histXLabelText 换算回本地时间，页面上看不到负数。 */
        if (self->m_chartBaseTs <= 0) self->m_chartBaseTs = row->m_ts;
        for (k = 0; k < HIST_SERIES_MAX; ++k) {
            HsfHistPoint pt;
            if (!row->m_hasNum[k]) continue;
            pt.m_x = (double)(row->m_ts - self->m_chartBaseTs) / 1000.0;
            pt.m_y = row->m_num[k];
            pt.m_ts = row->m_ts;
            (void)XVector_push_back_1_base(self->m_chartPts[k], &pt);
        }
        ++self->m_chartRootCount;
    }
}

/** @brief 等距抽样上界：把 n 个点压到 <= limit 个，且首尾必留。
 *  @details 索引公式 `idx(i) = round(i * (n-1) / (m-1))`，i=0..m-1，
 *           m=min(n,limit)：等距覆盖整个区间（不是截前 N 个、不是随机），
 *           趋势与极值位置都保留。单点/两点退化为全画。 */
static int histSampleCount(int n, int limit)
{
    if (n <= 0) return 0;
    if (limit < 2) limit = 2;
    return (n <= limit) ? n : limit;
}

/** @brief 等距第 i 个源索引（调用方保证 0<=i<m、m>=2、n>=m）。 */
static int histSampleIndex(int i, int n, int m)
{
    if (m <= 1) return 0;
    return (int)(((int64_t)i * (int64_t)(n - 1) + (m - 1) / 2) / (m - 1));
}

/** @brief 当前可见 X 域（图表 X 轴范围，单位=相对基准的秒数）；退化返回 false。
 *  @note  走 XChart_xRange（泛化轴域读取）：值轴/日期轴通用，避免页面
 *         依赖 XChart_axisX 的数值槽。 */
static bool histVisibleX(const HsfDataHistory* self, double* lo, double* hi)
{
    double a = 0.0;
    double b = 0.0;
    if (lo) *lo = 0.0;
    if (hi) *hi = 0.0;
    if (!self || !self->m_chart) return false;
    if (!XChart_xRange(self->m_chart, &a, &b)) return false;
    if (!(b > a)) return false;
    if (lo) *lo = a;
    if (hi) *hi = b;
    return true;
}

/** @brief 当前视口的「缩放视图」：轴域 + 绝对窗 + 取数窗**一次算齐**。
 *  @details [单一事实源] 缩放取数的判据是「取回的数据点必须与当前坐标轴的
 *           轴域同源同参」。这里把 轴域(xLo/xHi) → 绝对窗(viewLo/Hi) →
 *           取数窗(fetchLo/Hi) 一次算完（HsfChartZoom_view），页面**只许**
 *           把 fetchLo/Hi 发给服务端、把 xLo/xHi 留在轴域上；任何一处另算
 *           一遍就是"数据与轴域不同参"的入口（现场错位根因）。
 *  @note  图表 X 值是「相对基准时刻（m_chartBaseTs）的秒数」，绝对窗 =
 *         base + x*1000；换算全在 HsfChartZoom_view 里（纯函数，回归锁 §Z3
 *         /§Z9 逐条钉住）。
 *  @return 基准/轴域可用返回 true。
 */
static bool histChartViewNow(const HsfDataHistory* self, HsfChartZoomView* out)
{
    double xlo = 0.0;
    double xhi = 0.0;
    if (!self || !out) return false;
    if (self->m_chartBaseTs <= 0) return false;
    if (!histVisibleX(self, &xlo, &xhi)) return false;
    return HsfChartZoom_view(self->m_chartBaseTs, xlo, xhi, out);
}

/** @brief 当前是否「锁定视口」（窗口模式 / 窗口取数在途）。
 *  @details [锁什么] 锁定期间**轴域只由用户视口决定**：取数过程中的预览
 *           重绘、收尾重建一律按可见域裁数据，不许改写轴域。旧实现让
 *           "全量态重绘"（useVisible=false）在窗口取数在途时跑，把用户的
 *           缩放域改成"已到达那几行的时间跨度" ⇒ 请求窗与轴域脱钩，
 *           取回的点被裁掉两端（现场「缩放后曲线不连续、贴左边缘」）。
 */
static bool histChartViewLocked(const HsfDataHistory* self)
{
    if (!self) return false;
    return self->m_chartWinMode ||
           (self->m_chartFetching && self->m_chartFetchWin);
}

/* [前向声明] 状态行按视口重算（实现在重建/取数段之后）。 */
static void histChartStatusRefresh(HsfDataHistory* self);

/** @brief X 轴刻度标签：相对秒 → 本地时间文本（"MM-dd HH:mm" / 带年份秒级）。
 *  @details 轴值是「相对基准时刻（m_chartBaseTs）的秒数」，服务端按 ts
 *           降序取页 ⇒ 早期数据是负数；这里换算成绝对时刻再按本地时间
 *           格式化，界面上只会看到时间（不再有 "-150270 s"）。
 *           [为什么用标签回调而不是 XDateTimeAxis] 曲线点存在 XPointF
 *           （float）：epoch 毫秒在 float 下只有约 2 分钟分辨率，绝对值
 *           直接当轴值会把曲线压成阶梯。回调在渲染侧做换算，轴值域保持
 *           相对秒数的小量级，缩放/平移/抽样全都不受影响。
 *  @note  m_tzOffsetMs 与表格时间列同口径；数据未到时写空串（不画标签）。 */
static void histXLabelText(const XValueAxis* axis, double value, char* buf,
                           int cap, void* userData)
{
    const HsfDataHistory* self = (const HsfDataHistory*)userData;
    XDateTime dt;
    XDate d;
    XTime t;
    int64_t ts;
    (void)axis;
    if (!buf || cap <= 0) return;
    buf[0] = '\0';
    if (!self || self->m_chartBaseTs <= 0) return;
    ts = self->m_chartBaseTs + (int64_t)llround(value * 1000.0) +
         self->m_tzOffsetMs;
    if (!XDateTime_setMSecsSinceEpoch(&dt, ts)) return;
    d = XDateTime_date(&dt);
    t = XDateTime_time(&dt);
    if (self->m_xLabelFull) {
        snprintf(buf, (size_t)cap, "%04d-%02d-%02d %02d:%02d:%02d",
                 XDate_year(&d), XDate_month(&d), XDate_day(&d),
                 XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
    } else {
        /* [补秒 2026-11-10 用户口径「X 轴要时分秒」] 窄窗原为「MM-dd HH:mm」，
         * 秒位缺失——历史数据一行一秒，缩放后同一分钟内的先后看不出来。
         * 11→14 字符（"MM-dd HH:mm:ss" ≈ 98px），窄窗 4 个间隔仍放得下。 */
        snprintf(buf, (size_t)cap, "%02d-%02d %02d:%02d:%02d",
                 XDate_month(&d), XDate_day(&d),
                 XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
    }
}

/** @brief 按绘图区宽度选刻度标签详略（宽窗给到"年-月-日 时:分:秒"）。
 *  @details 本页 X 轴设 5 个刻度（4 个间隔），19 字符的完整时间约需
 *           152px：绘图区宽 ≥700px 时才用完整格式，窄窗退到
 *           "MM-dd HH:mm"（11 字符）。标志只影响标签文本，不改轴值域，
 *           也不触发重排。 */
static void histSyncXAxisLabels(HsfDataHistory* self)
{
    XRectF plot;
    if (!self || !self->m_chart) return;
    plot = XChart_plotArea(self->m_chart);
    self->m_xLabelFull = (plot.width >= 700.0f);
}

/** @brief 重建折线：每条图例按「可见域内全部数据」等距抽样到上限点数。
 *  @param self      页面。
 *  @param useVisible true=按当前 X 轴可见域过滤（缩放/平移重采用）；
 *                    false=用全量域并重设 X 轴范围（首次生成图表）。
 *  @note  [口径] 用户要求：图表画**筛选后的全部数据**（不是当前表格那
 *         一页），且每图例最多 N 点（可设置）、超限等距抽样、缩放/平移
 *         后按新可见域重新抽样。series 每次重建前 clear（防累积）。
 *  @note  [逐列归一化，复审③] 10 个数值列的量纲互不相干（频率 0~50Hz、
 *         转速 0~1500rpm、压差 0~1MPa…）。旧实现把**所有可见列**的
 *         vmin/vmax 并成一个量程设给唯一那根 Y 轴 ⇒ 小量纲的列被压成
 *         一条直线。Python 参照实现（pages_other.py:1499-1502 注释）明确
 *         "不做双 Y 轴：每条线按自己可见区间的 min/max 归一化到绘图区
 *         高度，图例标出该列 min~max"。本框架一张图只有一根 Y 轴
 *         （XChartView 的 xcv_mapPoint 只读 chart->m_axisY，
 *         见 XChartView.c:1750-1758），故此处按 Python 同一口径做**逐列
 *         归一化**：多列同屏时每列的值映射到自身可见量程的 0~100%，
 *         Y 轴刻度读作"占该列自身量程的百分比"，该列的真实量程写进
 *         图例名（"列名 min~max"，与 Python `_paint_legend` 同款）。
 *         只画一列时（工具条选中单通道，或只有一列有数据）保持原值 +
 *         真实 Y 轴——单列不存在量纲冲突，读真值比读百分比有用。 */
static void histBuildChartEx(HsfDataHistory* self, bool useVisible)
{
    int sel;
    int k;
    int limit;
    double xlo = 0.0;
    double xhi = 0.0;
    bool haveX = false;
    bool normalize;                 /* 多列同屏 ⇒ 逐列归一化到 0~100 */
    int drawn = 0;                  /* 本次实际要画点的列数 */
    /* 逐列绘制参数（第一遍定，第二遍用；HIST_SERIES_MAX=10，栈上足够）。 */
    int firsts[HIST_SERIES_MAX];    /**< 可见域首点下标。 */
    int counts[HIST_SERIES_MAX];    /**< 可见域点数（抽样前）。 */
    int samples[HIST_SERIES_MAX];   /**< 抽样后点数。 */
    double normLo[HIST_SERIES_MAX]; /**< 归一化下限（可见量程含 8% 留白）。 */
    double normHi[HIST_SERIES_MAX]; /**< 归一化上限。 */
    double realLo[HIST_SERIES_MAX]; /**< 该列可见域真实最小值（图例用）。 */
    double realHi[HIST_SERIES_MAX]; /**< 该列可见域真实最大值。 */
    bool has[HIST_SERIES_MAX];      /**< 该列本次有可画点。 */
    char title[160];

    if (!self || !self->m_chart) return;
    if (self->m_chartRebuilding) return;   /* 防 viewportChanged 重入 */
    self->m_chartRebuilding = true;
    /* 重建（缩放/平移/新数据）后旧气泡的定位已失效 ⇒ 先收起，下一次
     * 悬停会按新坐标系重开。 */
    XToolTip_hideText();

    limit = self->m_bar ? HsfDataBar_maxPoints(self->m_bar)
                        : HSFDATABAR_MAX_POINTS_DEFAULT;
    if (limit < HSFDATABAR_MAX_POINTS_MIN) limit = HSFDATABAR_MAX_POINTS_MIN;

    for (k = 0; k < HIST_SERIES_MAX; ++k)
        if (self->m_series[k]) XXYSeries_clear((XXYSeries*)self->m_series[k]);

    if (useVisible && histVisibleX(self, &xlo, &xhi)) haveX = true;

    sel = self->m_bar ? HsfDataBar_channelIndex(self->m_bar) : 0;
    for (k = 0; k < HIST_SERIES_MAX; ++k) {
        const XVector* vec = self->m_chartPts[k];
        const HsfHistPoint* base;
        int total;
        int first = 0;
        int last;
        int n;
        int m;
        int i;
        double lo;
        double hi;
        double pad;
        has[k] = false;
        if (sel > 0 && sel - 1 != k) continue;
        if (!self->m_series[k] || !vec) continue;
        total = (int)XVector_size_base((const XContainer*)vec);
        if (total <= 0) continue;
        base = (const HsfHistPoint*)XVector_data((XVector*)vec);
        if (!base) continue;
        /* ---- 可见域过滤：X 单调（升序，见 histChartDataDedup）→ 首尾二分定界。
         * [为什么不用框架的 XFind_binarySearch] 它是**等值查找**
         * （XFind.c:16 `compare(...)==XCompare_Equality` 才返回，否则
         * 返回 NULL），只回答"这个值在不在"；这里要的是**区间边界**
         * ——lower_bound（第一个 >= xlo）与 upper_bound（第一个 > xhi），
         * 两者在目标值不存在时必须返回插入位（缩放到两个采样点之间时
         * xlo/xhi 必然命中不到任何点），等值查找做不到。XVector 侧也只有
         * 线性查找：XVector_find_base（XVector.c:1161，落到 VXVector_find
         * 的逐元素比较）与 XVector_indexOf（XVector.c:1169），均 O(n)，
         * 而这里每帧缩放回调都要跑，几千点会明显卡。故就近写两段 12 行的
         * 二分：与 Qt/STL 的 lower_bound/upper_bound 同语义，且直接读 m_x
         * 不必构造比较器与临时 key。 */
        last = total - 1;
        if (haveX) {
            int loIdx = 0;
            int hiIdx = total;
            /* lower_bound(x >= xlo) */
            while (loIdx < hiIdx) {
                int mid = loIdx + (hiIdx - loIdx) / 2;
                if (base[mid].m_x < xlo) loIdx = mid + 1;
                else hiIdx = mid;
            }
            first = loIdx;
            /* upper_bound(x <= xhi) */
            loIdx = first;
            hiIdx = total;
            while (loIdx < hiIdx) {
                int mid = loIdx + (hiIdx - loIdx) / 2;
                if (base[mid].m_x <= xhi) loIdx = mid + 1;
                else hiIdx = mid;
            }
            last = loIdx - 1;
            if (last < first) continue;   /* 该列在此可见域内无点 */
        }
        n = last - first + 1;
        m = histSampleCount(n, limit);
        if (m <= 0) continue;
        /* ---- 本列可见量程（逐列归一化的分母，复审③）。
         * [为什么按可见域算而不是全量] 与 Python `_y_scale` 同口径：缩放
         * 只作用于时间轴、纵向跟着可见区间自适应（pages_other.py:1512）。
         * 8% 留白同 Python `_y_scale`（vmax==vmin 时退化为 ±1）。 */
        lo = hi = base[first].m_y;
        for (i = 1; i <= last - first; ++i) {
            const double y = base[first + i].m_y;
            if (y < lo) lo = y;
            if (y > hi) hi = y;
        }
        if (hi > lo) {
            pad = (hi - lo) * 0.08;
        } else {
            pad = (fabs(hi) > 1e-9) ? fabs(hi) * 0.08 : 1.0;
            if (pad <= 0.0) pad = 1.0;
        }
        firsts[k] = first;
        counts[k] = n;
        samples[k] = m;
        realLo[k] = lo;
        realHi[k] = hi;
        normLo[k] = lo - pad;
        normHi[k] = hi + pad;
        has[k] = true;
        ++drawn;
    }

    /* [是否归一化] 多列同屏才有量纲冲突；单列保持原值可读真数。
     * "全部通道" 下拉 + 只有一列有数据时（sel==0、drawn==1）同样不归一化：
     * 只有一条线时百分比刻度反而看不出量级。 */
    normalize = (drawn > 1);

    for (k = 0; k < HIST_SERIES_MAX; ++k) {
        const HsfHistPoint* base;
        double lo;
        double span;
        int i;
        int m;
        int n;
        if (!has[k] || !self->m_series[k] || !self->m_chartPts[k]) continue;
        /* [先判有数据再读量程] normLo/normHi 只在该列 has[k] 时写过；
         * 若在守卫前取用，未命中列读到的是栈上未初始化值（/RTC1 下虽
         * 多半是 0xCC，但属于 UB）。赋值挪到 continue 之后。 */
        lo = normLo[k];
        span = normHi[k] - normLo[k];
        base = (const HsfHistPoint*)XVector_data((XVector*)self->m_chartPts[k]);
        if (!base) continue;
        n = counts[k];
        m = samples[k];
        /* ---- 等距抽样：idx(i)=round(i*(n-1)/(m-1))。
         * [归一化] 多列时映射到 0~100（占该列自身量程的百分比）。 */
        for (i = 0; i < m; ++i) {
            const HsfHistPoint* p =
                &base[firsts[k] + ((m == n) ? i : histSampleIndex(i, n, m))];
            const double y = normalize
                ? ((span > 0.0) ? ((p->m_y - lo) * 100.0 / span) : 50.0)
                : p->m_y;
            XXYSeries_append((XXYSeries*)self->m_series[k], p->m_x, y);
        }
        /* [图例标注真实量程] 归一化后纵轴是百分比，量级只能从图例读。
         * 与 Python `_paint_legend` 的 "列名 min~max" 同款；单列时恢复纯
         * 列名（旧图例文字不该残留上一次归一化的区间）。 */
        if (normalize) {
            char legend[96];
            snprintf(legend, sizeof(legend), "%s %.4g~%.4g",
                     kHistoryHeaders[HIST_NUM_FIRST + k],
                     realLo[k], realHi[k]);
            XAbstractSeries_setName_2((XAbstractSeries*)self->m_series[k],
                                      legend);
        } else {
            XAbstractSeries_setName_2((XAbstractSeries*)self->m_series[k],
                                      kHistoryHeaders[HIST_NUM_FIRST + k]);
        }
    }

    /* Y 轴量程：归一化时固定 0~100（百分比）；单列按该列原值 ± 留白。
     * X 轴只在「全量视图」重设：锁定视口（窗口模式/窗口取数在途）时轴域
     * 归用户视口所有——这里改它就把取数窗与轴域拆成两份不同参的值。 */
    if (self->m_axisY) {
        if (normalize) {
            XValueAxis_setRange(self->m_axisY, 0.0, 100.0);
            XValueAxis_setLabelFormat_2(self->m_axisY, "%.0f%%");
        } else {
            int only = -1;
            for (k = 0; k < HIST_SERIES_MAX; ++k)
                if (has[k]) { only = k; break; }
            if (only >= 0)
                XValueAxis_setRange(self->m_axisY,
                                    normLo[only], normHi[only]);
            else
                XValueAxis_setRange(self->m_axisY, 0.0, 1.0);
            XValueAxis_setLabelFormat_2(self->m_axisY, "%g");
        }
    }
    if (!useVisible && !histChartViewLocked(self)) {
        if (self->m_chartRootCount > 0) {
            double a = 0.0;
            double b = 0.0;
            bool got = false;
            /* X 域取所有列首尾的并集（各列行集合可能因缺值不同）。 */
            for (k = 0; k < HIST_SERIES_MAX; ++k) {
                const XVector* vec = self->m_chartPts[k];
                const HsfHistPoint* base;
                int total;
                if (!vec) continue;
                total = (int)XVector_size_base((const XContainer*)vec);
                if (total <= 0) continue;
                base = (const HsfHistPoint*)XVector_data((XVector*)vec);
                if (!base) continue;
                if (!got) { a = base[0].m_x; b = base[total - 1].m_x; got = true; }
                else {
                    if (base[0].m_x < a) a = base[0].m_x;
                    if (base[total - 1].m_x > b) b = base[total - 1].m_x;
                }
            }
            if (got) {
                /* [留白按秒域] 域宽 2% 留白，最小 1s（单点/同刻数据用）。
                 * X 轴是值轴、域单位=相对秒数，刻度文本由
                 * histXLabelText 换算成本地时间。 */
                double pad = (b - a) * 0.02;
                if (pad <= 0.0) pad = 1.0;
                XChart_setXRange(self->m_chart, a - pad, b + pad);
            }
        }
    }

    if (sel > 0 && sel <= HIST_SERIES_MAX)
        snprintf(title, sizeof(title), "%s",
                 kHistoryHeaders[HIST_NUM_FIRST + sel - 1]);
    else {
        /* [标题行数口径 = 筛选总行数] 取的是**同一条 WHERE 下的服务端筛选
         * total**：状态行（histFillTable 的「共 N 条」）取 page->m_total，
         * 标题取 m_chartTotal（图表探测批记下的同一个 total），二者在同一条
         * WHERE 下应相等。旧实现标题用 m_chartRootCount——那是**已累加行数**
         * （受 HIST_CHART_MAX_ROWS 上限与跨区间跳批约束），于是同屏出现
         * 「共 1200 行」（标题）与「筛选共 202118 行」（状态行）两个数，
         * 看图的人无从判断到底筛出多少。total 未知（首批未回）时才退回
         * 累加量。
         * [归一化口径写在标题里] Python 的注释要求"这一点在标题下写明，
         * 避免误读"（pages_other.py:1328-1331）：多列各自归一化后纵轴是
         * 百分比，不说清楚会被当成真实值读。 */
        const int rows = self->m_chartTotal > 0 ? self->m_chartTotal
                                                : self->m_chartRootCount;
        if (normalize)
            snprintf(title, sizeof(title),
                     "历史数据折线（共 %d 行 · 各列按自身量程归一化，图例标 min~max）",
                     rows);
        else
            snprintf(title, sizeof(title), "历史数据折线（共 %d 行）", rows);
    }
    XChart_setTitle_2(self->m_chart, title);
    /* [刻度详略随可用宽度] 每次重建都同步一次（只改标签文本，不动轴域）。 */
    histSyncXAxisLabels(self);
    XChartView_updateChart(&self->m_chartView);
    /* 重建闸在这里收口。 */
    self->m_chartRebuilding = false;
}

/** @brief 生成图表入口：全量域重建（首次/生成按钮/筛选变更）。 */
static void histBuildChart(HsfDataHistory* self)
{
    histBuildChartEx(self, false);
}

/** @brief 按当前取数模式重建曲线。
 *  @details [两种口径的轴域归属]
 *           · 全量模式（m_chartWinMode=false 且无窗口取数在途）：轴域 = 数据
 *             跨度（用户没缩放时看全貌；"生成折线图"后轴域自动铺满）。
 *           · 锁定视口（窗口模式 / 窗口取数在途，见 histChartViewLocked）：
 *             轴域 = **用户当前的视口**，页面不动它（动了就把用户的缩放
 *             吃掉）；只按可见域重采样。取数窗由同一对轴域值换算
 *             （HsfChartZoom_view）⇒ 数据与轴域同基同参，曲线铺满视窗。 */
static void histChartRebuild(HsfDataHistory* self)
{
    if (!self) return;
    histBuildChartEx(self, histChartViewLocked(self));
}

/* [前置声明] 视口变化 → 取数 → 重建，三者互相调用（取数收尾后还要再
 * 判一次视口够不够数据），故先声明后定义。 */
static bool histChartFetchBegin(HsfDataHistory* self, bool winMode,
                                int64_t winLo, int64_t winHi);
static bool histChartUpdateWant(HsfDataHistory* self);
static void histChartTryStart(HsfDataHistory* self);

/** @brief 当前持有数据覆盖的绝对时间范围（epoch ms；无数据返回 false）。
 *  @details 全量模式下它就是"筛选域跨度"，用来判"用户是不是缩放了"
 *           （见 histChartUpdateWant）。按行里的 m_ts 取极值，不依赖
 *           X 基准（换基准不影响这个范围）。 */
static bool histChartDataRange(const HsfDataHistory* self,
                                int64_t* lo, int64_t* hi)
{
    int k;
    int64_t mn = 0;
    int64_t mx = 0;
    bool got = false;
    if (lo) *lo = 0;
    if (hi) *hi = 0;
    if (!self) return false;
    for (k = 0; k < HIST_SERIES_MAX; ++k) {
        const XVector* vec = self->m_chartPts[k];
        const HsfHistPoint* base;
        int total;
        int i;
        if (!vec) continue;
        total = (int)XVector_size_base((const XContainer*)vec);
        if (total <= 0) continue;
        base = (const HsfHistPoint*)XVector_data((XVector*)vec);
        if (!base) continue;
        for (i = 0; i < total; ++i) {
            if (base[i].m_ts <= 0) continue;
            if (!got) { mn = base[i].m_ts; mx = base[i].m_ts; got = true; continue; }
            if (base[i].m_ts < mn) mn = base[i].m_ts;
            if (base[i].m_ts > mx) mx = base[i].m_ts;
        }
    }
    if (!got) return false;
    if (lo) *lo = mn;
    if (hi) *hi = mx;
    return true;
}

/** @brief 视口变化后判断"还缺什么数据"，并登记待取任务。
 *  @return true=需要取数（待取任务已登记进 m_chartWant*）。
 *  @details [判定口径]
 *           ① 全量域 = 筛选结果的整段时间跨度（m_chartFullLo/Hi；全量
 *              取数完成后由数据跨度得出；筛选条件变更后清零重学）。
 *           ② 可见窗（轴域按基准换算回绝对时刻）≥ 全量域的 90% ⇒ 按
 *              **全量视图**处理（未缩放）：手里已是全量数据就只本地
 *              重采样；手里是窗口数据就换回全量数据。
 *           ③ 可见窗明显小于全量域 ⇒ **窗口视图**：要可见窗那一段的
 *              全部行（窗内全分辨率 ⇒ 曲线连续不尖角）。
 *              · 可见窗落在已取窗内且该窗已是全分辨率 ⇒ 不用再取；
 *              · 可见窗落在已取窗内但该窗是"窗内抽样"（行数超上限），
 *                且可见窗比它小 4 倍以上 ⇒ 重取更小的窗（换连续度）；
 *              · 可见窗越出已取窗（缩小/平移出界）⇒ 重取可见窗。
 *  @note  [为什么不能只靠本地重采样] 本地数据是**跨区间跳批抽样**的
 *         （见 histChartFetchContinue），簇与簇之间根本没有行；缩到
 *         空档里一个点都没有 ⇒ 曲线只剩两条长直线，现场"缩放后曲线
 *         几乎消失、只剩左边缘几条竖线"就是这么来的。 */
static bool histChartUpdateWant(HsfDataHistory* self)
{
    HsfChartZoomView view;
    int64_t fLo;
    int64_t fHi;
    if (!self || !self->m_chart) return false;
    /* [轴域与取数窗同一份计算] 可见窗/取数窗都从 histChartViewNow 的同一个
     * 视图里取；取数发起时只把 fetchLo/Hi 发出去，轴域一个字节都不动。 */
    if (!histChartViewNow(self, &view)) return false;
    fLo = self->m_chartFullLo;
    fHi = self->m_chartFullHi;
    if (fLo <= 0 || fHi <= fLo) {
        /* 全量域未知（还没取过全量数据）⇒ 用筛选时间窗兜底；没筛时间
         * 时兜底成可见窗 ⇒ 判不出"缩放态" ⇒ 本轮不取数（等全量数据
         * 到手，第一次缩放就能判）。 */
        fLo = (self->m_loTs > 0) ? self->m_loTs : view.viewLo;
        fHi = (self->m_hiTs > 0) ? self->m_hiTs : view.viewHi;
    }
    if (fHi <= fLo) return false;

    if (HsfChartZoom_isFullView(view.viewLo, view.viewHi, fLo, fHi)) {
        /* 可见窗 ≈ 全量域 ⇒ 全量视图 */
        if (!self->m_chartWinMode) return false;  /* 全量数据在手：本地重采样即可 */
        self->m_chartWantPending = true;
        self->m_chartWantWin = false;
        self->m_chartWantLo = 0;
        self->m_chartWantHi = 0;
        self->m_chartViewLo = 0.0;
        self->m_chartViewHi = 0.0;
        return true;
    }

    /* 窗口视图 */
    if (self->m_chartWinMode &&
        HsfChartZoom_windowCovers(self->m_chartWinLo, self->m_chartWinHi,
                                  view.viewLo, view.viewHi)) {
        /* 可见窗在已取窗内 */
        if (!self->m_chartWinSampled) return false;  /* 已是窗内全分辨率 */
        if ((view.viewHi - view.viewLo) * HSF_CHART_ZOOM_RESAMPLE_DEN >=
            (self->m_chartWinHi - self->m_chartWinLo) *
                HSF_CHART_ZOOM_RESAMPLE_NUM)
            return false;   /* 已取窗不算大，抽样也够密：不必重取 */
    }
    /* 取数窗 = 视图里的 fetchLo/Hi（= 可见窗各外扩 1%、最小 1s）⇒ 边界行
     * 也在内，曲线能铺满视窗两端。[轴域] 记下同一视图的 xLo/xHi：收尾按
     * 这一对值重放轴域，保证"取回的点与轴域同参"（见 histChartFetchFinish）。 */
    self->m_chartWantPending = true;
    self->m_chartWantWin = true;
    self->m_chartWantLo = view.fetchLo;
    self->m_chartWantHi = view.fetchHi;
    self->m_chartViewLo = view.xLo;
    self->m_chartViewHi = view.xHi;
    return true;
}

/** @brief 尝试发起登记好的取数（防抖 + 在途判定）。
 *  @details [为什么不立刻取] 滚轮连发会每帧触发视口变化 ⇒ 400ms 防抖；
 *         已有在途取数时不并发（应答槽只有一份，见 histTimerCb 的归属
 *         判定）。[谁来补发] 页面轮询定时器每拍调一次 ⇒ 防抖到期自动
 *         发，不必等下一次视口事件（用户停下滚轮也能把数取回来）。 */
static void histChartTryStart(HsfDataHistory* self)
{
    uint64_t now;
    if (!self || !self->m_chartWantPending) return;
    if (self->m_chartFetching) return;          /* 在途：等它收尾再发 */
    if (self->m_exportFetching) return;         /* 导出批在途：收尾后下一拍再发 */
    now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
    if (now - self->m_chartWinReqMs < 400u) return;   /* 防抖 */
    self->m_chartWinReqMs = now;
    self->m_chartWantPending = false;
    if (!histChartFetchBegin(self, self->m_chartWantWin,
                             self->m_chartWantLo, self->m_chartWantHi)) {
        /* 发起失败（连接断/内存不足）⇒ 保留待取任务，下一拍再试。 */
        self->m_chartWantPending = true;
    }
}

/** @brief 回到全量域视图（框架复位 / 用户缩回全域）。
 *  @details 手里有全量数据就直接重画（轴域回到数据跨度）；只有窗口数据
 *           就重新取全量（在途时排队，收尾后自动发）。 */
static void histChartResetFull(HsfDataHistory* self)
{
    if (!self) return;
    self->m_chartWantPending = false;
    self->m_chartWantWin = false;
    self->m_chartWantLo = 0;
    self->m_chartWantHi = 0;
    if (!self->m_chartWinMode && self->m_chartRootCount > 0 &&
        !self->m_chartDataDirty) {
        self->m_chartWinLo = 0;
        self->m_chartWinHi = 0;
        self->m_chartWinSampled = false;
        self->m_chartViewLo = 0.0;
        self->m_chartViewHi = 0.0;
        histBuildChart(self);
        histChartStatusRefresh(self);
        return;
    }
    self->m_chartWantPending = true;   /* 取全量 */
    histChartTryStart(self);
}

/** @brief viewportChanged：缩放/平移/复位后按当前可见 X 域重采样或重取数。
 *  @note  Qt 对应语义=QChartView 轴范围变化后由应用侧重采样；框架信号
 *         载荷为 XChartView_ViewportChange（Zoom/Pan/Reset）。
 *  @note  [复位走全量域] 框架的 zoomReset 把轴域复位到图表创建时的默认
 *         值（0..10 秒，约等于 1970 年），对本页毫无意义（曲线空白）。
 *         收到 Reset 就回到全量域（等价"重新生成图表"）。 */
static void histViewportSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)receiver;
    int reason = (int)XChartView_ViewportChange_Zoom;
    if (!self || self->m_viewMode != 1) return;
    if (self->m_chartRebuilding) return;
    if (self->m_chartRootCount <= 0) return;
    if (args) {
        XVarList_start(args);
        reason = XVarList_arg(args, int);
    }
    if (reason == (int)XChartView_ViewportChange_Reset) {
        histChartResetFull(self);
        return;
    }

    /* ---------- 缩放取数：按可见时间窗向服务端重取该段行 ----------
     *
     * [为什么必须取数、不能只重采样] 本页的图表数据是**跨区间跳批抽样**
     * （batches 批 × CHUNK 行铺满全区间，见 histChartFetchContinue）：
     * 库里 18039 行只取到 ~500 行，分成若干段 49 行的簇，中间是**空的**
     * ——那些行压根没取过。缩放只做本地重采样（histBuildChartEx(true)）
     * 时，缩到簇与簇之间的空档里，窗内一个点都没有，曲线只剩两条长直
     * 线段连成的斜线；缩到簇内部也只画那 49 行（用户 2026-11-09 实测：
     * "图表缩放后不够连续"）。故缩放后要**按可见窗重新向服务端取数**，
     * 取到的才是这一段的全部行（窗口模式逐行取，见
     * histChartFetchContinue 的"窗口态·逐行取"分支）。
     */
    if (histChartUpdateWant(self)) {
        /* [缺数据 ⇒ 先别重采样] 手里的数据是抽样来的，按新视口重采样会
         * 把窗内的点滤没 ⇒ 曲线"几乎消失、只剩左边缘几条竖线"（现场
         * 截图）。故缺数据时**保持原曲线不动**，等取回的窗内数据到位
         * 再重画（histChartFetchFinish 里重建）。 */
        histChartTryStart(self);
        return;
    }
    /* [够用 ⇒ 本地重采样] 轴域不动（用它自己的那对值裁点）。重采样后状态行
     * 必须按**当前窗口**重算：缩放态的"窗内 N 行"要随视口变，不能停在上
     * 一次取数的文案（用户口径：状态行与可视窗口不一致）。 */
    histBuildChartEx(self, true);
    histChartStatusRefresh(self);
}

/* ==================== 图表悬浮气泡（鼠标悬停 / 触摸点按） ====================
 * 用户口径「悬浮气泡显示当前鼠标或触摸位置的信息」。框架侧能力：
 *   · XChartView 在 mouseMove 命中曲线点时发射 XXYSeries_hovered_signal
 *     （载荷 x,y 为数据域坐标），离开/换点发 state=false；
 *   · 触摸单指拖拽经 touch→mouse 仿真同样走 mouseMove（拖拽平移时气泡
 *     跟着最近点走，松手即消失——不额外拦截平移）；
 *   · 触摸点按命中曲线点时 xcv 发 XXYSeries_pressed_signal，用它给
 *     "点一下看数值"补一个 3 秒气泡（触摸没有 hover 概念）。
 * 显示用框架 XToolTip（对标 QToolTip），坐标由绘图区反算后 mapToGlobal。 */

/** @brief 绝对时间格式化（epoch ms → "YYYY-MM-DD HH:MM:SS"，本地时间）。
 *  @param tsMs       墙钟 epoch 毫秒（UTC 刻度）。
 *  @param tzOffsetMs 本地时区偏移（毫秒）；XDateTime 的日历展开只做 UTC，
 *                    补上偏移才是本地时间（表格时间列同口径）。
 *  @note  这里手工拼而不用 XDateTime_toString_format：库内另一处
 *         （HsfLinkHistory.c 的 hsfHistFormatTs）也手工拼同一格式，两边
 *         输出必须逐字一致（气泡与表格的同一时刻不能显示成两种写法）。 */
static void histFormatTsAbs(int64_t tsMs, int64_t tzOffsetMs,
                            char* out, int cap)
{
    XDateTime dt;
    XDate d;
    XTime t;
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (tsMs <= 0) {
        snprintf(out, (size_t)cap, "--");
        return;
    }
    dt = XDateTime_create();
    if (!XDateTime_setMSecsSinceEpoch(&dt, tsMs + tzOffsetMs)) {
        snprintf(out, (size_t)cap, "--");
        return;
    }
    d = XDateTime_date(&dt);
    t = XDateTime_time(&dt);
    snprintf(out, (size_t)cap, "%04d-%02d-%02d %02d:%02d:%02d",
             XDate_year(&d), XDate_month(&d), XDate_day(&d),
             XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
}

/** @brief ts 访问器：HsfHistPoint 数组的 m_ts（给 HsfChartZoom 的区间统计用）。
 *  @note  用访问器而不是临时数组：统计与绘制读的是**同一批点**（不必为
 *         了状态行另建/排序一份 ts 数组，也不会有"统计口径与绘制的点
 *         不一致"的第二事实源）。 */
static int64_t histChartPointTsAt(const void* user, int index)
{
    const HsfHistPoint* pts = (const HsfHistPoint*)user;
    return pts ? pts[index].m_ts : 0;
}

/** @brief 当前图表的"行集"向量（取第一条有点的列）。
 *  @details 各列的行集相同（同一批行按 m_hasNum 逐列入点，缺值的列少几条）；
 *           统计视口行数只看一条列即可，不逐列累加（否则同一个行被数 N 遍）。 */
static const XVector* histChartRowsVec(const HsfDataHistory* self)
{
    int k;
    if (!self) return NULL;
    for (k = 0; k < HIST_SERIES_MAX; ++k) {
        const XVector* vec = self->m_chartPts[k];
        if (vec && XVector_size_base((const XContainer*)vec) > 0) return vec;
    }
    return NULL;
}

/** @brief 图表状态行：按**当前视口**重算（不得残留全量区间的文案）。
 *  @details [两套口径]
 *           · 全量视图（未缩放）：整段筛选结果的抽样曲线；
 *           · 锁定视口（缩放态）：窗口起止时刻 + 窗内行数 + 视口行数 +
 *             逐行/抽样 + 是否铺满视口。
 *           [为什么必须按视口重算] 缩放后用户看到的是**一段时间窗**，
 *           状态行若还写"筛选共 18039 行"（全量区间），与可视窗口对不上
 *           （用户口径："状态行文字仍是全量区间的描述"）。
 *           [文案与回归锁共用] 窗口口径的拼装在 HsfChartZoom_statusText，
 *           断言在 Test/XProtocolTest/hsf_chart_zoom_test.c §Z11。 */
static void histChartStatusRefresh(HsfDataHistory* self)
{
    char buf[256];
    char whenLo[32];
    char whenHi[32];
    int rows;
    if (!self) return;
    rows = self->m_chartTotal > 0 ? self->m_chartTotal : self->m_chartRootCount;
    if (!histChartViewLocked(self)) {
        /* 全量视图：整段筛选结果的抽样曲线（与全量取数收尾口径一致）。 */
        if (rows <= 0)
            snprintf(buf, sizeof(buf), "图表数据：筛选结果为 0 行");
        else
            snprintf(buf, sizeof(buf),
                     "图表数据：筛选共 %d 行，已按全区间抽样", rows);
        XLabel_setText_2(&self->m_status, buf);
        return;
    }
    {
        HsfChartZoomView view;
        HsfChartZoomStatus st;
        const XVector* vec;
        const HsfHistPoint* base = NULL;
        int n = 0;
        if (!histChartViewNow(self, &view)) return;
        histFormatTsAbs(view.viewLo, self->m_tzOffsetMs, whenLo,
                        (int)sizeof(whenLo));
        histFormatTsAbs(view.viewHi, self->m_tzOffsetMs, whenHi,
                        (int)sizeof(whenHi));
        memset(&st, 0, sizeof(st));
        st.rowsHeld = rows;
        st.sampled = self->m_chartWinSampled;
        vec = histChartRowsVec(self);
        if (vec) {
            n = (int)XVector_size_base((const XContainer*)vec);
            base = (const HsfHistPoint*)XVector_data((XVector*)vec);
        }
        st.rowsInView = HsfChartZoom_countInView(&view, histChartPointTsAt,
                                                 base, n);
        st.filled = HsfChartZoom_viewFilled(&view, histChartPointTsAt,
                                            base, n);
        (void)HsfChartZoom_statusText(whenLo, whenHi, &st, buf,
                                      (int)sizeof(buf));
        XLabel_setText_2(&self->m_status, buf);
    }
}

/** @brief 某列在给定 X 附近最近的真实点（列向量按 m_x 升序，二分）。
 *  @return 借用指针；该列无点返回 NULL。 */
static const HsfHistPoint* histNearestPoint(const HsfDataHistory* self,
                                            int k, double x)
{
    const XVector* vec;
    const HsfHistPoint* base;
    int lo = 0;
    int hi;
    if (!self || k < 0 || k >= HIST_SERIES_MAX) return NULL;
    vec = self->m_chartPts[k];
    if (!vec) return NULL;
    hi = (int)XVector_size_base((const XContainer*)vec) - 1;
    if (hi < 0) return NULL;
    base = (const HsfHistPoint*)XVector_data((XVector*)vec);
    if (!base) return NULL;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (base[mid].m_x < x) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 0) {
        const double d0 = fabs(base[lo].m_x - x);
        const double d1 = fabs(base[lo - 1].m_x - x);
        if (d1 < d0) --lo;
    }
    return &base[lo];
}

/** @brief 显示气泡：命中列 k 的最近点 → "时间\\n列名: 值"，定位到该点右上。
 *  @note  X 域单位是「相对基准的秒数」，故时刻 = m_chartBaseTs + x*1000；
 *         找不到最近点时按同一换算取时刻（不再有另一种基准口径）。 */
static void histShowBubble(HsfDataHistory* self, int k, double x, double y,
                           int msec)
{
    XValueAxis* ay;
    XRectF plot;
    XPoint pt;
    XPoint global;
    const HsfHistPoint* p;
    char when[32];
    char buf[160];
    int64_t ts;
    double val;
    double xmin;
    double xmax;
    double ymin;
    double ymax;
    if (!self || !self->m_chart || k < 0 || k >= HIST_SERIES_MAX) return;
    if (!self->m_series[k]) return;
    p = histNearestPoint(self, k, x);
    ts = p ? p->m_ts
           : (self->m_chartBaseTs + (int64_t)llround(x * 1000.0));
    val = p ? p->m_y : y;
    histFormatTsAbs(ts, self->m_tzOffsetMs, when, (int)sizeof(when));
    snprintf(buf, sizeof(buf), "%s\n%s: %g", when,
             kHistoryHeaders[HIST_NUM_FIRST + k], val);
    /* 数据域 → 视图局部像素 → 全局（气泡放在点右上方）。 */
    ay = XChart_axisY(self->m_chart);
    plot = XChart_plotArea(self->m_chart);
    if (!ay || plot.width <= 1.0f || plot.height <= 1.0f ||
        !XChart_xRange(self->m_chart, &xmin, &xmax)) {
        XToolTip_showText_2(0, 0, buf, (XWidget*)&self->m_chartView,
                            NULL, msec);
        return;
    }
    ymin = XValueAxis_min(ay);
    ymax = XValueAxis_max(ay);
    if (!(xmax > xmin) || !(ymax > ymin)) return;
    pt.x = (int)(plot.x + (x - xmin) / (xmax - xmin) * plot.width) + 12;
    pt.y = (int)(plot.y + (1.0 - (y - ymin) / (ymax - ymin)) * plot.height)
           - 40;
    global = XWidget_mapToGlobal((const XWidget*)&self->m_chartView, &pt);
    XToolTip_showText_2(global.x, global.y, buf,
                        (XWidget*)&self->m_chartView, NULL, msec);
}

/** @brief 悬停槽：进入/换点显示，离开隐藏（duration 长，随移动刷新）。 */
static void histHoverSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)receiver;
    XObject* sender = XObject_sender(receiver);
    double x = 0.0;
    double y = 0.0;
    int state = 0;
    int k;
    if (!self || !args) return;
    XVarList_start(args);
    x = XVarList_arg(args, double);
    y = XVarList_arg(args, double);
    state = XVarList_arg(args, int);
    if (!state) {
        XToolTip_hideText();
        return;
    }
    for (k = 0; k < HIST_SERIES_MAX; ++k)
        if (self->m_series[k] && (XObject*)self->m_series[k] == sender) break;
    if (k >= HIST_SERIES_MAX) return;
    histShowBubble(self, k, x, y, 60000);
}

/** @brief 触摸点按槽：无 hover 概念，命中点给 3 秒气泡。 */
static void histPressedSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)receiver;
    XObject* sender = XObject_sender(receiver);
    double x = 0.0;
    double y = 0.0;
    int k;
    if (!self || !args) return;
    XVarList_start(args);
    x = XVarList_arg(args, double);
    y = XVarList_arg(args, double);
    for (k = 0; k < HIST_SERIES_MAX; ++k)
        if (self->m_series[k] && (XObject*)self->m_series[k] == sender) break;
    if (k >= HIST_SERIES_MAX) return;
    histShowBubble(self, k, x, y, 3000);
}

/** @brief 确保全量筛选数据已就绪（累加分批；未完成则续拉）。
 *  @details [为什么要分批] 单次请求 limit 协议上限 50（服务端还会夹到
 *           [1,50]；图表批实际取 41..49，见 HIST_CHART_CHUNK 注释），
 *           而"筛选后全部数据"可达上千行。这里用**同一组筛选条件**
 *           （m_kwText/m_loTs/m_hiTs/列筛选）按批游标累加，直到取满
 *           服务端本次应答的 total 或触发 HIST_CHART_MAX_ROWS 安全上限。
 *           累加数据存 m_chartPts[k]（XVector，随页对象释放）。
 *  @note  累加期间库在持续入库：HsfLinkClient 每帧的 beforeTs 重取"现在"，
 *         相邻批可能重叠/漏行。图表只画趋势，故累加完成后按 X 升序去重
 *         （histChartDataDedup），不追求与分页表格逐行一致。 */

/** @brief 图表点按时间升序比较器（供 XVector_sort_base 用）。 */
static int histPointCompare(const void* a, const void* b)
{
    const HsfHistPoint* pa = (const HsfHistPoint*)a;
    const HsfHistPoint* pb = (const HsfHistPoint*)b;
    if (pa->m_ts < pb->m_ts) return -1;
    if (pa->m_ts > pb->m_ts) return 1;
    return 0;
}

/** @brief 全量数据规整：按时间升序排序 + 去重（相邻批重叠兜底）。
 *  @details 服务端按 ts 降序取页 ⇒ 累加顺序是**时间倒序**；采样的可见域
 *           二分与 X 轴范围计算都要求 **X 单调升序**。这里用框架的
 *           XVector_sort_base（容器比较器已设，见 histChartDataEnsure）
 *           排一次，再单趟去重（同一 ts 只留一条）。
 *           比较走 m_ts（整数毫秒），不靠浮点相等。 */
static void histChartDataDedup(HsfDataHistory* self)
{
    int k;
    if (!self) return;
    for (k = 0; k < HIST_SERIES_MAX; ++k) {
        XVector* vec = self->m_chartPts[k];
        HsfHistPoint* base;
        int total;
        int i;
        int w = 0;
        if (!vec) continue;
        total = (int)XVector_size_base((const XContainer*)vec);
        if (total <= 0) continue;
        if (total > 1) XVector_sort_base(vec, XSORT_ASC);
        base = (HsfHistPoint*)XVector_data(vec);
        if (!base) continue;
        /* 升序后同 ts 相邻：保留第一条。 */
        for (i = 1; i < total; ++i) {
            if (base[i].m_ts == base[w].m_ts) continue;
            ++w;
            if (w != i) base[w] = base[i];
        }
        if (w + 1 < total) (void)XVector_resize_base(vec, (size_t)(w + 1));
    }
}

/** @brief 发一批图表取数请求，并登记"这是一个图表批"的应答归属。
 *  @details [为什么登记 offset/limit] HsfLinkClient 只有一份静态历史应答
 *           缓冲（HsfLinkClient_history()），且页面拿不到 reqId（协议帧头
 *           有 m_requestId，HsfLinkClient 未暴露，见结构体字段注释）；服务端
 *           把夹取后的 offset/limit 原样回显（夹取 HsfServerApp.c:1927-1928，
 *           回显同文件 :1946-1947），这是页面目前唯一能用来认领"这条应答是
 *           谁的"的信息。仅有它还不够，故 limit 按消费者分档且两两不相交
 *           （见 HIST_CHART_CHUNK）。
 *  @param self   页面。
 *  @param offset 本批起点。
 *  @param limit  本批行数（探测批 1..8 / 正式批 41..49；见
 *                HIST_CHART_CHUNK 的轮转说明）。
 *  @return 请求已发出返回 true。 */
static bool histChartRequest(HsfDataHistory* self, int offset, int limit)
{
    HistCols cols;
    int n;
    int64_t loTs;
    int64_t hiTs;
    HIST_COLS_SIZE_ASSERT(cols);
    if (!self) return false;
    n = histKwCols(self, cols);
    /* [取数窗] 窗口态按**本轮取数的窗快照**取（m_chartFetchLo/Hi，
     * 不是"当前想取的窗"——一轮取数必须自始至终同一个窗，否则中途换窗
     * 会把两个窗的行混在一批里）；全量态按筛选条件的时间窗取。
     * 两者都带同一份关键字与列集 ⇒ 缩放窗里的行与表格口径一致。 */
    loTs = (self->m_chartFetchLo > 0) ? self->m_chartFetchLo : self->m_loTs;
    hiTs = (self->m_chartFetchHi > 0) ? self->m_chartFetchHi : self->m_hiTs;
    if (!HsfLinkClient_historyQueryWindow(offset, limit, false,
                                          self->m_kwText, cols, n,
                                          loTs, hiTs))
        return false;
    self->m_chartReqOffset = offset;
    self->m_chartReqLimit = limit;
    self->m_chartReqValid = true;
    return true;
}

/** @brief 本轮取数的批行数（轮转取值；相邻两轮必不相等）。
 *  @details [为什么要轮转] 应答归属只能靠服务端回显的 (offset,limit)
 *           （应答槽只有一份，见 histTimerCb）。相邻两轮取数若用同一个
 *           limit，上一轮**被顶掉**的在途应答会被下一轮按同键认领 ⇒
 *           旧窗的行混进新窗（缩放取数最忌讳：数据与视口错位）。
 *           正式批取 41..49、探测批取 1..8（都与页大小 10/20/50 不撞，
 *           探测批与正式批也不撞）；轮转规则见 HsfChartZoom_nextLimits。 */
static void histChartNextLimits(HsfDataHistory* self, int* probe, int* chunk)
{
    int seq;
    if (!self) return;
    seq = self->m_chartLimitSeq;
    self->m_chartLimitSeq = (seq + 1) % 72;   /* 8×9：探测/正式两档轮转周期 */
    HsfChartZoom_nextLimits(seq, probe, chunk);
}

/** @brief 发起一轮图表取数（全量模式 or 窗口模式）。
 *  @param winMode true=按窗口取（窗内逐行全分辨率）；false=按筛选域跨
 *                 区间跳批抽样（未缩放时的全貌曲线）。
 *  @param winLo   窗口下界（绝对 epoch ms；仅 winMode=true 时有效）。
 *  @param winHi   窗口上界（绝对 epoch ms）。
 *  @return 请求已发出返回 true。
 *  @details [基准保持] 窗口模式**沿用旧基准**（m_chartBaseTs 不变）：
 *           用户视口（轴域）与数据 X 值都以它为基准，换了基准两者就
 *           脱钩 ⇒ 数据点落到轴域外（现场"缩放后只剩左边缘几条竖线"
 *           就是这个错位）。全量模式重新取基准（数据整体换了，轴域
 *           随重建一起换）。 */
static bool histChartFetchBegin(HsfDataHistory* self, bool winMode,
                                int64_t winLo, int64_t winHi)
{
    int probeLimit = HSF_CHART_ZOOM_PROBE_MIN;
    int chunkLimit = HIST_CHART_CHUNK;
    if (!self || !self->m_chart) return false;
    if (self->m_chartFetching) return false;
    /* [导出批在途不并发] 应答槽只有一份：导出与图表同时取会互相顶掉
     * 应答。导出期间缩放取数保持登记（m_chartWant*），导出收尾后由
     * histTimerCb 的补发口自动接续。 */
    if (self->m_exportFetching) return false;
    /* [窗口态保留已持有窗口的元信息] 取完才落地新窗（见
     * histChartFetchFinish），这里不动 m_chartWin*。 */
    histChartDataClear(self, winMode);
    if (!histChartDataEnsure(self)) {
        XLabel_setText_2(&self->m_status, "图表失败：内存不足");
        return false;
    }
    self->m_chartFetching = true;
    self->m_chartFetchOffset = 0;
    self->m_chartFetchWin = winMode;
    self->m_chartFetchLo = winMode ? winLo : 0;
    self->m_chartFetchHi = winMode ? winHi : 0;
    /* [轴域只属于窗口取数] 全量取数没有"锁定轴域"这回事（轴域随数据跨度
     * 重设）：把上一轮窗口的轴域快照与模式一起清掉，进度重绘与最终结果
     * 才是同一口径（全量视图）。 */
    if (!winMode) {
        self->m_chartViewLo = 0.0;
        self->m_chartViewHi = 0.0;
        self->m_chartWinMode = false;
    }
    histChartNextLimits(self, &probeLimit, &chunkLimit);
    self->m_chartProbeLimit = probeLimit;
    self->m_chartChunkLimit = chunkLimit;
    XLabel_setText_2(&self->m_status,
                     winMode ? "正在读取缩放窗口（图表）…"
                             : "正在读取筛选结果（图表）…");
    /* [取数期间提速轮询] 一批一应答、应答槽单份 ⇒ 轮询周期直接决定取数
     * 耗时（页面默认 200ms ⇒ 一批 200ms，几百行要等几十秒）。周期统一
     * 由 histTimerIntervalApply 按状态算（导出也在途时仍保持提速）。 */
    histTimerIntervalApply(self);
    /* 首批只拿几行就够算 total（服务端 COUNT 与页内容同一条 WHERE） */
    if (!histChartRequest(self, 0, probeLimit)) {
        self->m_chartFetching = false;
        self->m_chartDataDirty = true;   /* 没取到数：保持"需重取"，别当成已就绪 */
        XLabel_setText_2(&self->m_status, "图表失败：服务端未连接");
        XPrintf("HsfDataHistory: chart fetch failed to start\n");
        histTimerIntervalApply(self);
        return false;
    }
    return true;
}

/** @brief 取全量数据入口（生成图表按钮 / 切图表视图）。 */
static void histChartFetchStart(HsfDataHistory* self)
{
    if (!self || !self->m_chart) return;
    /* [锁定视口时不许"回全量重建"] 缩放态（或窗口取数在途）下点"生成折线图"，
     * 走 histBuildChart（useVisible=false）会把用户视口改写成数据跨度——
     * 与取数窗脱钩。锁定期间交给 histChartResetFull 的语义处理。 */
    if (!self->m_chartDataDirty && self->m_chartRootCount > 0 &&
        !histChartViewLocked(self)) {
        /* 全量数据已在手：直接重建（轴域回到数据跨度） */
        self->m_chartWantPending = false;
        self->m_chartWinLo = 0;
        self->m_chartWinHi = 0;
        self->m_chartWinSampled = false;
        self->m_chartViewLo = 0.0;
        self->m_chartViewHi = 0.0;
        histBuildChart(self);
        histChartStatusRefresh(self);
        return;
    }
    /* 需要重新取全量（数据脏 / 手里只有窗口数据） */
    self->m_chartWantPending = false;
    self->m_chartWantWin = false;
    self->m_chartWantLo = 0;
    self->m_chartWantHi = 0;
    self->m_chartViewLo = 0.0;
    self->m_chartViewHi = 0.0;
    (void)histChartFetchBegin(self, false, 0, 0);
}

/** @brief 累加结束：清 fetching 态并按全量数据重建图表（幂等）。
 *  @param self      页面。
 *  @param note      状态行文案（NULL=不改）。
 *  @param complete  true=按计划取完（数据完整，清 dirty）；false=中途放弃
 *                   （数据只是半截，必须保持 dirty，下次生成整批重取）。
 *  @details [为什么把"完整/半截"显式传进来] 旧实现无论取完还是断链中止
 *           都只清 m_chartFetching：半截数据留在容器里且 m_chartDataDirty
 *           仍为 false，于是下一次「生成折线图」命中 histChartFetchStart 的
 *           「未过期且有数据」早退分支，把 50 行当全量画出去（复审①）。 */
static void histChartFetchFinish(HsfDataHistory* self, const char* note,
                                 bool complete)
{
    if (!self) return;
    self->m_chartReqValid = false;   /* 在途批的应答：到此为止 */
    /* 取完 ⇒ 数据可用（清 dirty）；半截 ⇒ 保持"需重取"（下见上）。 */
    self->m_chartDataDirty = !complete;
    /* [轮询周期恢复] 取数期间提速过，这里按状态重算（导出若在途仍提速）。 */
    histTimerIntervalApply(self);
    if (complete) {
        if (self->m_chartFetchWin) {
            /* 窗口态落地：已持有数据 = 本轮取的那个窗（绝对时刻）。 */
            self->m_chartWinLo = self->m_chartFetchLo;
            self->m_chartWinHi = self->m_chartFetchHi;
            self->m_chartWinMode = true;
            /* [轴域按同一对值重放] 取数窗就是由 m_chartViewLo/Hi 这一对轴域
             * 值换算出来的（HsfChartZoom_view），收尾时把轴域**按原值放回**：
             * 取数期间任何路径动过轴域，取回的点都会与轴域不同参（旧实现
             * 的"全量态重绘"就把轴域改成了已到达几行的时间跨度 ⇒ 现场
             * "缩放后曲线不连续、贴左边缘"）。
             * [不覆盖用户的新视口] 用户在取数期间自己缩放过 ⇒ 轴域已是
             * 另一个值，这里不覆盖（下方 UpdateWant 会按新视口补数据）。 */
            {
                double curLo = 0.0;
                double curHi = 0.0;
                if (histVisibleX(self, &curLo, &curHi) &&
                    fabs(curLo - self->m_chartViewLo) <= 1e-6 &&
                    fabs(curHi - self->m_chartViewHi) <= 1e-6)
                    XChart_setXRange(self->m_chart, self->m_chartViewLo,
                                     self->m_chartViewHi);
            }
        } else {
            /* 全量态落地：已持有数据 = 筛选域的抽样曲线；
             * 全量域 = 数据的时间跨度（用来判"用户是不是缩放了"）。 */
            self->m_chartWinLo = 0;
            self->m_chartWinHi = 0;
            self->m_chartWinMode = false;
            self->m_chartWinSampled = false;
            self->m_chartViewLo = 0.0;
            self->m_chartViewHi = 0.0;
            {
                int64_t dLo = 0;
                int64_t dHi = 0;
                if (histChartDataRange(self, &dLo, &dHi)) {
                    self->m_chartFullLo = dLo;
                    self->m_chartFullHi = dHi;
                }
            }
        }
    } else {
        /* [半截 ⇒ 窗口元信息作废] 数据只取了一部分，与"已持有窗口"对不上
         * （否则下一次缩放会以为该窗已取齐、实际是半截）。退回全量口径，
         * 全量域也一并作废（下次全量取数后重新学）。 */
        self->m_chartWinLo = 0;
        self->m_chartWinHi = 0;
        self->m_chartWinMode = false;
        self->m_chartWinSampled = false;
        self->m_chartFullLo = 0;
        self->m_chartFullHi = 0;
        self->m_chartViewLo = 0.0;
        self->m_chartViewHi = 0.0;
        /* [锁也要撤] 中止后手里是半截数据（全量口径），轴域必须回到数据跨度
         * ⇒ 释放"窗口取数在途"这项锁定，否则重建会按老视口裁半截数据。 */
        self->m_chartFetchWin = false;
    }
    /* [在途位最后清] 上面刚落地"本轮取的是窗口还是全量"：锁定判定
     * （histChartViewLocked）要看到新状态，下面的重建与状态行才走对口径
     * （窗口收尾 ⇒ 按可见域裁点、不改轴域；全量收尾 ⇒ 轴域回到数据跨度）。 */
    self->m_chartFetching = false;
    /* [收尾规整] 采样/二分/轴范围都要求 X 升序（服务端按 ts 降序返回，
     * 跨批跳跃后整体无序）→ 收尾统一排序 + 去重（窗口模式攒批重绘，
     * 这里最后一次全量规整）。 */
    histChartDataDedup(self);

    histChartRebuild(self);
    /* [状态行按当前视口重算] note 只用于异常终局（断链/半截）；正常收尾
     * 一律由 histChartStatusRefresh 按**当前窗口**出新文案——不能把全量
     * 区间的"筛选共 N 行"留在缩放态（用户口径：状态行与可视窗口不一致）。 */
    if (note) XLabel_setText_2(&self->m_status, note);
    else histChartStatusRefresh(self);
    /* [收尾后补取] 用户在取数期间又缩放了 ⇒ 视口要的窗可能与刚取到的
     * 不同 ⇒ 再判一次，缺就排队再取一轮（不取就会出现空图）。 */
    if (histChartUpdateWant(self)) histChartTryStart(self);
}

/** @brief 每批应答到达：累加 → 推进下一批 → 取完重建。
 *  @details [两种取数计划]
 *           ① **全量模式**（未缩放）：跨全区间等距跳批。每图例上限 N 点，
 *              而筛选结果可达 20 万行（实测 Python 参照库 202118 行）。
 *              若只连续取前 5000 行，图画的是"最近一小段"而不是"全部
 *              筛选结果的平均取点"——用户口径明确要后者。故：
 *                a) 首批拿 total（服务端同一 WHERE 的计数）；
 *                b) 计划批数 batches = min(total/chunk, 上限)，步长
 *                   step = total / batches（行）；
 *                c) 第 b 批 offset = b * step，取 chunk 行；
 *                d) 各批拼起来天然覆盖 [0, total) 全区间，再交给
 *                   等距抽样（histBuildChartEx）压到每图例 N 点。
 *              总请求数=batches（受内存上限约束），内存 ≤ batches*chunk 行，
 *              与库大小无关。
 *           ② **窗口模式**（缩放后按可见时间窗取）：窗内行数不超
 *              HIST_CHART_WIN_FULL_ROWS 时**连续取**（步长 = 批行数），
 *              取回窗内**每一行** ⇒ 曲线是窗内全分辨率（连续、不尖角）；
 *              超过上限则退回①的跳批（窗内抽样，状态行注明）。 */
static void histChartFetchContinue(HsfDataHistory* self,
                                   const HsfHistoryPage* page)
{
    int got;
    const int chunk = (self && self->m_chartChunkLimit > 0)
                          ? self->m_chartChunkLimit
                          : HIST_CHART_CHUNK;
    if (!self || !page || !page->m_valid) return;
    got = page->m_count;

    /* ---- 探测批（limit=1..8）：只取 total，用于规划取数批。 ---- */
    if (self->m_chartBatches == 0) {
        int total = page->m_total > 0 ? page->m_total : got;
        HsfChartZoomPlan plan;
        int want;
        if (total <= 0) {
            /* 筛选结果确实为 0 行：这是一个**完整**的答案（不是半截）。
             * [文案交给状态行统一拼] 窗口态要说"该时间段无记录"（窗口口径），
             * 不能复用全量区间的"筛选结果为 0 行"。 */
            histChartFetchFinish(self, NULL, true);
            return;
        }
        self->m_chartTotal = total;
        /* [批次计划] 窗口态＝窗内逐行取（不跳批、不丢段 ⇒ 曲线连续）；
         * 全量态＝跨区间跳批抽样（未缩放时看全貌）。计划规则见
         * HsfChartZoom_planBatches（纯函数，回归锁逐条钉住）。 */
        want = self->m_bar ? HsfDataBar_maxPoints(self->m_bar)
                           : HSFDATABAR_MAX_POINTS_DEFAULT;
        if (want < HSFDATABAR_MAX_POINTS_MIN) want = HSFDATABAR_MAX_POINTS_MIN;
        (void)HsfChartZoom_planBatches(total, chunk,
                                        self->m_chartFetchWin,
                                        HIST_CHART_WIN_FULL_ROWS, want,
                                        HIST_CHART_MAX_ROWS, &plan);
        self->m_chartBatches = plan.batches > 0 ? plan.batches : 1;
        self->m_chartStep = plan.step > 0 ? plan.step : chunk;
        self->m_chartWinSampled = plan.sampled;
        self->m_chartBatchDone = 0;
        self->m_chartFetchOffset = 0;
        if (!histChartRequest(self, 0, chunk)) {
            XPrintf("HsfDataHistory: chart fetch aborted at offset=0\n");
            /* 首批就没发出去：一行都没有 ⇒ 半截（数据不可用），保持 dirty。 */
            histChartFetchFinish(self, "图表数据：连接中断", false);
        }
        return;
    }

    /* ---- 正式批 ---- */
    histChartDataAppendPage(self, page);
    self->m_chartFetchOffset += got;
    self->m_chartBatchDone++;
    if (got <= 0) {
        /* 本批 0 行：服务端在这个 offset 已经没数据（总数比探测时更小），
         * 计划内的区间取不到更多了 ⇒ 按"取完"收尾（不是断链）。
         * [收尾文案交给状态行统一拼] 以前传 NULL 不清状态，界面会停在上一拍
         * 的"读取筛选结果（图表）… 第 k/N 批"，看上去还在取数；现在由
         * histChartStatusRefresh 按当前窗口出终局文案。 */
        histChartFetchFinish(self, NULL, true);
        return;
    }
    /* [规整成 X 升序] 采样/二分/轴范围都要求单调升序（服务端按 ts 降序
     * 返回，跨批跳跃后整体无序）→ 每批排序 + 去重。
     * [重绘节奏] 窗口模式可能上百批，每批都排序+重画是 O(n log n) 大头
     * ⇒ 攒 HIST_CHART_WIN_REDRAW_BATCH 批画一次（数据仍在追加，画的是
     * 预览）；全量模式批数少，照旧每批都画。 */
    {
        char prog[160];
        const int total = self->m_chartTotal > 0 ? self->m_chartTotal : page->m_total;
        snprintf(prog, sizeof(prog),
                 "%s… 第 %d/%d 批 · 共 %d 行",
                 self->m_chartFetchWin ? "读取缩放窗口（图表）"
                                       : "读取筛选结果（图表）",
                 self->m_chartBatchDone, self->m_chartBatches, total);
        XLabel_setText_2(&self->m_status, prog);
        if (!self->m_chartFetchWin ||
            (self->m_chartBatchDone % HIST_CHART_WIN_REDRAW_BATCH) == 0) {
            histChartDataDedup(self);
            /* [按模式重画] 全量态：轴域 = 数据跨度（此刻数据还只到一半，
             * 轴域会跟着长，最后一批到齐时统一定）；窗口态：**不动轴域**
             * （用户的视口要保住），只按可见域重画。 */
            histChartRebuild(self);
        }
    }
    /* 终止：批数用尽 / 已取到区尾 / 触及内存上限。 */
    if (self->m_chartBatchDone >= self->m_chartBatches ||
        got < chunk ||
        self->m_chartRootCount >= HIST_CHART_MAX_ROWS) {
        /* [文案一律按窗口重算] 窗口态要写"窗口 HH:MM~HH:MM · 窗内 N 行（视口 M 行）"，
         * 不能停在"筛选共 N 行"这种全量区间口径（用户口径：状态行与可视窗口
         * 不一致）。拼装在 histChartStatusRefresh / HsfChartZoom_statusText。 */
        histChartFetchFinish(self, NULL, true);
        return;
    }
    {
        const int nextOffset = self->m_chartStep * self->m_chartBatchDone;
        if (!histChartRequest(self, nextOffset, chunk)) {
            /* [失败终局可观测] 断链/背压拒发：保留已累加部分照常出图，
             * 并留一行日志（不反复重试，避免把连接打进死循环）。 */
            XPrintf("HsfDataHistory: chart fetch aborted at offset=%d\n",
                    nextOffset);
            /* 只取到一部分 ⇒ 保持 dirty：下次「生成折线图」整批重取，
             * 而不是把"已画已取部分"当成完整结果（复审①的同类根因）。 */
            histChartFetchFinish(self, "图表数据：连接中断（已画已取部分）",
                                 false);
            return;
        }
        self->m_chartFetchOffset = nextOffset;
    }
}

/** @brief 切表格/图表视图（两视图同格二选一；显隐自动触发父布局失效）。 */
static void histSetView(HsfDataHistory* self, int mode)
{
    if (!self) return;
    self->m_viewMode = (mode == 1) ? 1 : 0;
    XWidget_setVisible((XWidget*)&self->m_table, self->m_viewMode == 0);
    XWidget_setVisible(&self->m_chartView.m_base, self->m_viewMode == 1);
    if (self->m_bar)
        HsfDataBar_setViewMode(self->m_bar, self->m_viewMode);
}

/** @brief 工具条动作落地（本页自己那套的接线口）。 */
static void histBarAction(void* userData, int action)
{
    HsfDataHistory* self = (HsfDataHistory*)userData;
    if (!self) return;
    switch (action) {
    case HsfDataBarAction_Filter:
        histApplyFilter(self);
        break;
    case HsfDataBarAction_ClearFilter:
        histClearFilter(self);
        break;
    case HsfDataBarAction_Export:
        histExportStart(self);
        break;
    case HsfDataBarAction_Reload:
        histRequest(self, self->m_offset);
        XLabel_setText_2(&self->m_status, "重新加载…");
        break;
    case HsfDataBarAction_GenerateChart:
        histChartFetchStart(self);   /* 取筛选后的全部数据再画 */
        histSetView(self, 1);
        break;
    case HsfDataBarAction_ViewTable:
        histSetView(self, 0);
        break;
    case HsfDataBarAction_ViewChart:
        histChartFetchStart(self);
        histSetView(self, 1);
        break;
    case HsfDataBarAction_ChannelChanged:
        /* [通道切换 = 只重画] 走 histChartRebuild（锁定视口时按可见域裁点、
         * 不动轴域）；旧实现走 histBuildChart（useVisible=false）会把缩放
         * 态的轴域改写成数据跨度 ⇒ 取数窗与轴域脱钩。 */
        if (self->m_viewMode == 1) {
            histChartRebuild(self);
            histChartStatusRefresh(self);
        }
        break;
    case HsfDataBarAction_ColumnFilterChanged:
        histApplyColumnFilter(self);
        /* [列筛选：视图设置 + （关键字非空时的）查询条件] 与 Python
         * `_on_columns_changed`（pages_other.py:1012-1039）同口径分两路：
         *   · 关键字为空 ⇒ kwCols 不进服务端 WHERE（histKwCols 只在 kw 非空
         *     时随帧下发，见 HsfLinkClient.c:2018 `if (kw && kw[0])`），列只是
         *     "看哪几列"——本地重铺显隐即可，**不发网络往返**；
         *   · 关键字非空 ⇒ 可见列就是 kwCols（histKwCols），改列会改变
         *     WHERE 与结果集，必须**回第 1 页**按新列集合重取——Python 同
         *     分支就是 `self._page_index = 0` + `_request_page(0)`。
         * [为什么不只置 dirty] 旧实现只 `m_chartDataDirty = true` 不发请求：
         * 表格继续显示按**旧**列集合筛出的行，图表也只脏不拉（拉取入口
         * histTimerCb 的 dirty 分支要等"下一条页应答"到达才跑，没有请求就
         * 永远等不到），与 Python 相反。histRequest 内部还会清
         * m_chartFetching、置 dirty、作废图表批描述符——已发出的旧批是
         * 服务端按**旧** kwCols 匹配的，晚到应答随即被判无主丢弃，不会把
         * 新旧条件混合的半截数据画出去。 */
        if (self->m_kwText[0]) {
            /* [图表视图的重取由页应答接手，不在这里直接拉] 这里只发**一个**
             * 请求（表格页）并置脏；histTimerCb 在页应答到达后的 dirty 分支
             * 会按新列集合整批重取图表（与 histApplyFilter/histClearFilter
             * 完全同一路径）。不在此处再调 histChartFetchStart 的原因：应答
             * 槽只有一个，而轮询颗粒是界面刷新周期（缺省 200ms）——
             * HsfLinkClient.c:772 hsfLcDispatch 每收到一帧就直接写
             * s_lc.m_history，同一拍内两条应答到达时先到的那条会被覆盖
             * （m_seq 连跳两格、内容只剩最后一条），表格应答丢了就正好退化
             * 成"表格停在旧列集合"这个原始症状。 */
            self->m_chartDataDirty = true;
            histRequest(self, 0);   /* 结果集变了：回第 1 页（Python 同口径） */
        }
        break;
    case HsfDataBarAction_DecimalsChanged:
        self->m_digits = HsfDataBar_decimalDigits(self->m_bar);
        /* [只重排"当前显示的那一页"] 共用应答槽里可能已换成图表批/别的页的
         * 应答；能对得上（同一应答序号 + 同一页）就地按新小数位重排，否则
         * 重取当前页（按新小数位重排一次即可，不能拿别的页铺表）。 */
        {
            const HsfHistoryPage* page = HsfLinkClient_history();
            if (page && page->m_valid && page->m_seq == self->m_shownSeq &&
                page->m_offset == self->m_shownOffset &&
                page->m_count == self->m_shownCount)
                histFillTable(self, page);
            else
                histRequest(self, self->m_shownOffset);
        }
        break;
    case HsfDataBarAction_MaxPointsChanged:
        /* 点数上限变了：按当前可见域立即重采样（不重新拉数据）。 */
        if (self->m_viewMode == 1) histBuildChartEx(self, true);
        break;
    default:
        break;
    }
}

/** @brief 界面刷新周期拍：应答到达 → 按**归属**分派（表格页 / 图表批 / 导出批）。
 *  @details [根因②：单一应答槽] HsfLinkClient 只有一份静态历史应答缓冲
 *           （HsfLinkClient_history()）。协议帧头其实带 m_requestId，Response
 *           会原样回带对端 id（XLinkProto.h:43/:111/:253），只是 HsfLinkClient
 *           目前没把它随 HsfHistoryPage 交给页面（hsfLcDispatch 手里有 frame，
 *           见 HsfLinkClient.c:772）。在补上这条链路之前，页面只能按服务端
 *           回显的 offset/limit 认领，故必须靠**构造**保证各消费者的 limit
 *           两两不相交（探测批 1..8 / 图表正式批 41..49 / 导出批 40 / 页
 *           10·20·50，见 HIST_CHART_CHUNK 与 HIST_EXPORT_CHUNK 注释）：
 *             · 分页（limit=页大小） → 填表；
 *             · 图表探测批（limit=1..8）/ 正式批（limit=41..49） → 喂累加器；
 *             · 导出批（limit=40） → 逐批写文件；
 *             · 谁都对不上（被取消/已被新请求取代的迟到应答） → 丢弃。
 *           旧实现只看 m_chartFetching 标志猜归属：取消图表累加后，已在途
 *           的那一批应答一到，标志已是 false ⇒ 被 histFillTable 当表格页
 *           铺进去（表格显示图表批内容、页码按图表 offset 跳成第 401 页，
 *           复审②）。现在取消时连描述符一并作废，且各消费者批与页请求的
 *           limit 恒不相交，迟到批只可能"对不上"。
 *           [导出批看门狗] 导出批的应答可能被同拍到达的页应答顶出单份
 *           缓冲（用户在导出期间翻了页）。顶掉 ≠ 丢终局：同一批按同一
 *           (offset,limit) 重发即可（数据是死的）；约 1s 没等到就重发，
 *           连续 3 次不回才放弃。页应答被顶掉仍是既有接受的残余局限
 *           （用户再点一次即恢复）。
 *  @note  [残余局限] 键相同的重复请求（连点两次同一页、筛选后立刻再筛选
 *         ——页与页同用页大小档）仍无法按 offset/limit 区分，那需要 reqId
 *         链路。修法已明确：HsfLinkClient 记录 XLinkSession_sendRequest_ex
 *         的 reqId（hsfLcHistoryQuery 现在拿到就丢）并在 hsfLcDispatch 把
 *         frame->m_requestId 写进 HsfHistoryPage，本函数改成 reqId 比对
 *         即可，分派结构不变。 */
static void histTimerCb(void* userData, XTimerData* timer)
{
    HsfDataHistory* self = (HsfDataHistory*)userData;
    const HsfHistoryPage* page;
    (void)timer;
    if (!self) return;
    /* [导出批看门狗] 在途导出批的应答若被别的应答顶出单份缓冲，这里
     * 按 1s 节奏重发同一批；连续不回才终局（服务端断了/被拒了）。 */
    if (self->m_exportFetching) {
        ++self->m_exportIdle;
        if (self->m_exportIdle > HIST_EXPORT_WATCHDOG_TICKS) {
            self->m_exportIdle = 0;
            if (++self->m_exportRetries > HIST_EXPORT_MAX_RETRIES) {
                histExportDone(self, false, "导出失败：服务端应答超时");
            } else if (!histExportRequest(self, self->m_exportOffset)) {
                histExportDone(self, false, "导出失败：服务端未连接");
            }
        }
    }
    /* [待发取数先补发] 视口变化登记的取数任务（缩放取数）在此补发：
     *   · 防抖到期（滚轮连发后用户停手，不再有新的视口事件）；
     *   · 上一轮在途取数刚收尾（histChartFetchFinish 排的任务）。
     * 不必等下一次视口事件 ⇒ 用户停下滚轮也能把窗内数据取回来。 */
    histChartTryStart(self);
    page = HsfLinkClient_history();
    if (!page) return;
    /* [服务端拒绝要显示出来 2026-11-10] 落盘关闭（storage/enabled=0）等
     * 情况下服务端回的是失败包络 ⇒ 结果结构 m_valid=false 且带 m_error
     * 原文。此前进到这里直接 return ⇒ 状态行永远停在「读取中」、表格空
     * ⇒ 现场看着像「表格数据没了」，看不到真正原因。现在把服务端给的
     * 原文显示在状态行上（XLabel 同值早退，不会每拍标脏）。 */
    if (!page->m_valid) {
        if (page->m_error[0]) {
            char ebuf[224];
            snprintf(ebuf, sizeof(ebuf), "查询失败：%s", page->m_error);
            XLabel_setText_2(&self->m_status, ebuf);
        }
        /* [失败包络也是导出批的终局] 导出批被拒（权限/落盘关闭）＝本轮
         * 导出就此放弃：abort 写出器（删半成品）、状态行保留上面的服务端
         * 原文（failNote=NULL 不覆盖）。旧实现此处直接 return，导出标志
         * 永挂 ⇒ 后续同键页应答会被误当导出批写盘。 */
        if (self->m_exportFetching)
            histExportDone(self, false, NULL);
        return;
    }
    /* [同一应答只处置一次] m_seq 每次成功解析 +1；重复读到同一条就返回，
     * 否则每拍都会把同一批再累加一遍。 */
    if (page->m_seq == self->m_seenSeq) return;

    {
        const bool isChartReply =
            self->m_chartReqValid &&
            page->m_offset == self->m_chartReqOffset &&
            page->m_limit == self->m_chartReqLimit;
        const bool isPageReply =
            self->m_pageReqValid &&
            page->m_offset == self->m_pageReqOffset &&
            page->m_limit == self->m_pageReqLimit;
        const bool isExportReply =
            self->m_exportFetching && self->m_exportColCount > 0 &&
            page->m_offset == self->m_exportOffset &&
            page->m_limit == self->m_exportLimit;

        /* [导出批] 只认自己那条（offset/limit 与发出时一致），逐批写盘。
         * 导出批与页/图表批的 limit 恒不相交 ⇒ 一条应答只会落进一个分支，
         * 不再像旧实现那样"导出应答顺带填表"（那是导出=当前页重拉时代
         * 的同键产物；现在导出有自己的键）。 */
        if (isExportReply) {
            self->m_seenSeq = page->m_seq;
            histExportContinue(self, page);
            return;
        }

        if (isChartReply) {
            self->m_seenSeq = page->m_seq;
            self->m_chartReqValid = false;
            histChartFetchContinue(self, page);
            return;
        }
        if (!isPageReply) {
            /* [无主应答 = 丢弃] 被取消/被取代的请求（图表批、旧页、被顶掉
             * 的导出）晚到的应答：绝不能铺表，否则就是复审②的错页。
             * 记下 seenSeq 防同一应答每拍重判。 */
            self->m_seenSeq = page->m_seq;
            XPrintf("HsfDataHistory: drop unattributed reply "
                    "(offset=%d, limit=%d, seq=%u)\n",
                    page->m_offset, page->m_limit, (unsigned)page->m_seq);
            return;
        }
        self->m_seenSeq = page->m_seq;
        self->m_pageReqValid = false;
    }

    histFillTable(self, page);
    if (self->m_viewMode == 1) {
        /* 图表数据未因筛选作废时，只按可见域重采样（不重拉）；
         * 作废（筛选/清除/重新加载）则整批重取。 */
        if (self->m_chartDataDirty) histChartFetchStart(self);
        else {
            histBuildChartEx(self, true);
            /* [状态行口径跟着视图走] 上面 histFillTable 刚把状态行写成
             * "共 N 条（本页 M）· 第 x/y 页"——那是**表格页**的文案。图表
             * 视图下要按图表口径（全量抽样 / 缩放窗口）重算回来。 */
            histChartStatusRefresh(self);
        }
    }
}

static void histApplyFonts(XWidget* page, int pct);
static void histPageDestroyedSlot(XObject* sender, XVarList* args);

XWidget* HsfDataHistory_create(XWidget* parent, HsfMenuNavFn navFn,
                               void* userData)
{
    HsfDataHistory* self;
    (void)navFn; (void)userData;

    if (!parent) return NULL;
    self = (HsfDataHistory*)XMemory_malloc(sizeof(HsfDataHistory),
                                          XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    HsfTheme_applyDefaultFont(&self->m_base);

    self->m_tzOffsetMs = histTzOffsetMs();

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title,
                     "历史数据"); /* 历史数据 */
    XLabel_setTextPixelSize(&self->m_title, histPx(16));
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* 工具条（本页自己一套；与日志/报警页同款，各自一份实例）。 */
    self->m_digits = 2;
    self->m_bar = HsfDataBar_create(&self->m_base);
    if (self->m_bar) {
        const char* channelItems[HIST_SERIES_MAX + 1];
        /* [项数 = 1（「全部列」）+ 显示列数] 列筛选下拉每一项对应一个显示列
         * （0=全部列，1..N=列 0..N-1），故数组与项数都要 +1。 */
        const char* colItems[HSF_HISTORY_DISPLAY_COLS + 1];
        XDateTime now;
        int i;
        /* 通道 = 数值列（全部通道 + 逐列；画折线图用）。 */
        channelItems[0] = "全部通道";
        for (i = 0; i < HIST_SERIES_MAX; ++i)
            channelItems[i + 1] = kHistoryHeaders[HIST_NUM_FIRST + i];
        HsfDataBar_setChannelItems(self->m_bar, channelItems,
                                   HIST_SERIES_MAX + 1);
        HsfDataBar_setChannelIndex(self->m_bar, 0);
        /* 列筛选 = 显示列（全部列 + 逐列）；改动即时生效。 */
        colItems[0] = "全部列";
        for (i = 0; i < HSF_HISTORY_DISPLAY_COLS; ++i)
            colItems[i + 1] = kHistoryHeaders[i];
        HsfDataBar_setColumnFilterItems(self->m_bar, colItems,
                                        HSF_HISTORY_DISPLAY_COLS + 1);
        HsfDataBar_setColumnFilterIndex(self->m_bar, 0);
        HsfDataBar_setDecimalDigits(self->m_bar, 2);
        now = XDateTime_currentDateTime();
        HsfDataBar_setEndDateTime(self->m_bar, &now);
        HsfDataBar_setActionCallback(self->m_bar, histBarAction, self);
        /* [按页裁剪：删「导出格式」下拉 2026-11-10] 格式改由文件对话框的
         * 过滤器档位决定（histExportStart），工具条上那颗下拉既没有存在
         * 的意义、还曾是"选了不兑现"的死控件——按界面铁律直接藏掉，别
         * 留着骗人。 */
        HsfDataBar_setFeatures(self->m_bar,
            HsfDataBarFeature_All & ~(unsigned int)HsfDataBarFeature_ExportFmt);
        XBoxLayout_addWidget(&self->m_layout, self->m_bar);
    }

    XLabel_init(&self->m_status, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_status);
    XLabel_setTextPixelSize(&self->m_status, histPx(12));
    HsfTheme_applySecondaryText((XWidget*)&self->m_status);
    XLabel_setText_2(&self->m_status,
                     "读取中"); /* 读取中 */
    XWidget_show((XWidget*)&self->m_status);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_status);

    XTableWidget_init(&self->m_table, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_table);
    XTableWidget_setColumnCount(&self->m_table, HSF_HISTORY_DISPLAY_COLS);
    XTableWidget_setHorizontalHeaderLabels(&self->m_table, kHistoryHeaders,
                                           HSF_HISTORY_DISPLAY_COLS);
    XTableWidget_setRowCount(&self->m_table, 0);
    self->m_rowBaseH = XTableView_rowHeight(&self->m_table.m_base, 0);
    /* [列宽按当前字体实测] 必须在字体设好之后算（量的就是这份字体）。 */
    histCalcColWidths(self);
    {
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        if (hh)
            XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Fixed);
        /* [列宽统一取自 m_colWidths] 按当前字体实测表头文字得出（见
         * histCalcColWidths）；建表与 adapt 共用一份，不再两处手写。 */
        {
            int cw;
            for (cw = 0; cw < HSF_HISTORY_DISPLAY_COLS; ++cw)
                XTableView_setColumnWidth(&self->m_table.m_base, cw,
                                          self->m_colWidths[cw]);
        }
        /* [观感对齐 Python] 无行号列、无网格线。 */
        XTableWidget_setVerticalHeaderVisible(&self->m_table, false);
        XTableView_setShowGrid(&self->m_table.m_base, false);    {
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        HsfTableSort_init(&self->m_sort);
        if (hh) {
            /* [单击口径 2026-11-11] 框架对齐 Qt 后，QTableView 自建表头的
             * sectionsClickable 缺省为 true（Qt qtableview.cpp:577-589），
             * 单击即翻转排序指示器。本页排序是**双击三态**（HsfTableSort，
             * 指示器由页面在 DBL 槽里同步）⇒ 显式关掉单击可点，否则一次
             * 单击就把指示器挪到未排序的列上（数据没排、指示器却变了）。
             * 双击发射不受 sectionsClickable 门控（Qt qheaderview.cpp:2759
             * 无条件发），双击排序照常。 */
            XHeaderView_setSectionsClickable(hh, false);
            /* [双击列标题排序] 与 Python 客户端同口径；作用于当前页。 */
            XObject_connect_1((XObject*)hh,
                              XSignal(XHeaderView_sectionDoubleClicked_signal),
                              (XObject*)self, HsfDataHistoryHeaderDblClicked,
                              XConnectionType_Direct);
        }
    }

    }
    XWidget_show((XWidget*)&self->m_table);
    XBoxLayout_addWidgetEx(&self->m_layout, (XWidget*)&self->m_table, 1, 0);

    /* 图表视图（与表格同格二选一；数据来自当前页 + 工具条通道）。 */
    XChartView_init(&self->m_chartView, &self->m_base, 0);
    XWidget_setSizePolicy(&self->m_chartView.m_base,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
    self->m_chart = XChart_create();
    if (self->m_chart) {
        int i;
        XChart_createDefaultAxes(self->m_chart);
        self->m_axisY = (XValueAxis*)XChart_axisY(self->m_chart);
        /* [X 轴刻度=本地时间 2026-11-08] 曲线点存在 XPointF（float）里，
         * epoch 毫秒在 float 下只有约 2 分钟分辨率，直接把绝对值当轴值
         * 会把曲线压成阶梯；因此 X 轴仍是数值轴、域单位是「相对基准
         * 时刻的秒数」（历史页原来的口径），但刻度文本交给框架新增的
         * 自定义标签回调（XValueAxis_setLabelTextCallback）换算成本地
         * 时间——旧实现按 "%.0f s" 打印，服务端降序取页使早期数据成
         * 负数，界面显示 "-150270 s"。
         * 默认域给"最近一小时"（相对秒），数据到达后按实际区间重设。 */
        {
            XValueAxis* axX = (XValueAxis*)XChart_axisX(self->m_chart);
            if (axX) {
                XValueAxis_setLabelTextCallback(axX, histXLabelText, self);
                /* 5 个刻度（对齐 Qt QDateTimeAxis 默认 tickCount=5）：完整
                 * 时间标签 19 字符约 152px，5 刻度/4 间隔在 700px 宽绘图区
                 * 刚好排开（见 histSyncXAxisLabels 的 700px 阈值）。 */
                XValueAxis_setTickCount(axX, 5);
                XValueAxis_setRange(axX, -3600.0, 0.0);
            }
        }
        for (i = 0; i < HIST_SERIES_MAX; ++i) {
            XLineSeries* s = XLineSeries_create();
            if (!s) continue;
            XAbstractSeries_setName_2((XAbstractSeries*)s,
                                      kHistoryHeaders[HIST_NUM_FIRST + i]);
            XChart_addSeries(self->m_chart, s, XChartSeriesType_Line);
            self->m_series[i] = s;
            /* [悬浮气泡] 悬停（鼠标/触摸拖拽仿真 move）与点按（触摸）
             * 都显示该点的时间+真实值；接收者=页面（随页析构作废）。 */
            XObject_connect_1((XObject*)s,
                              XSignal(XXYSeries_hovered_signal),
                              (XObject*)self, histHoverSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)s,
                              XSignal(XXYSeries_pressed_signal),
                              (XObject*)self, histPressedSlot,
                              XConnectionType_Direct);
        }
        XChartView_setChart(&self->m_chartView, self->m_chart);
    }
    XChartView_setPanEnabled(&self->m_chartView, true);
    XChartView_setTouchGesturesEnabled(&self->m_chartView, true);
    /* [滚轮=缩 X 轴 2026-11-08 实锅] 本页 X 轴是**时间**（相对秒数，刻度
     * 由标签回调显示成本地时间），纵轴是归一化百分比/单列量程——只该缩
     * 时间轴。原先没开时间轴模式也没开 wheelZoomXOnly，滚轮走框架通用
     * 分支 `XChart_scroll(dx,dy)`：鼠标滚轮只有 delta.y ⇒ dx=0、dy=5%
     * 域高 ⇒ 纵向滚动（用户实测「滚轮在缩 Y 轴」）。与变频器/流量计趋势图
     * （HsfMonitorVfd.c:2390 / HsfMonitorFlowMeter.c:953 同款两行）对齐，
     * 开时间轴后滚轮=锚定光标的 X 轴缩放，且会发 viewportChanged(Zoom)
     * 让本页按可见域重采样。 */
    XChartView_setTimeAxisMode(&self->m_chartView, true);
    XChartView_setWheelZoomXOnly(&self->m_chartView, true);
    /* [缩放/平移后按可见域重采样] 对标 Qt 里"轴 rangeChanged → 应用侧
     * 重采样"的接法；框架已提供 viewportChanged(Zoom/Pan/Reset) 信号
     * （XChartView.h:238）。receiver=页面对象（与 HsfMonitorVfd.c:2138
     * 同款：接收者即持有状态的对象，不以视图为 receiver，页面析构时
     * XObject_deinit 会把它登记的全部连接一并作废）。 */
    XObject_connect_1((XObject*)&self->m_chartView,
                      XSignal(XChartView_viewportChanged_signal),
                      (XObject*)self, histViewportSlot,
                      XConnectionType_Direct);
    XWidget_hide(&self->m_chartView.m_base);
    XBoxLayout_addWidgetEx(&self->m_layout, &self->m_chartView.m_base, 1, 0);

    /* [分页/关键字控件] 建页时以页根为父，但**布局位置**挂进顶部工具条
     * 第二行——原先这排控件自成一条 XHBoxLayout 追加在表格下方，表格高度
     * 被白吃掉一整行。attachRow2 内部走布局挂接，XLayout_linkItem 会沿
     * 布局链上溯并 XWidget_setParent_2 到工具条（对标 Qt addWidget 的
     * reparent 语义）——**挂接后父对象是工具条，不是页根**，随工具条
     * 级联析构。控件是页面结构体的内嵌成员，地址不随父链变化，故信号
     * receiver 继续接控件自身（不能改成工具条，见各 connect）。 */
    XPushButton_init(&self->m_prev, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_prev);
    XPushButton_setText_2(&self->m_prev,
                         "上一页"); /* 上一页 */
    XWidget_setFixedHeight((XWidget*)&self->m_prev, 28);
    XObject_connect_1((XObject*)&self->m_prev,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_prev, histPrevSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_prev);

    XPushButton_init(&self->m_next, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_next);
    XPushButton_setText_2(&self->m_next,
                         "下一页"); /* 下一页 */
    XWidget_setFixedHeight((XWidget*)&self->m_next, 28);
    XObject_connect_1((XObject*)&self->m_next,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_next, histNextSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_next);

    /* 每页条数（与 Python PAGE_SIZES 同档口径）。 */
    XComboBox_init(&self->m_pageSizeBox, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_pageSizeBox);
    XComboBox_addItem_2(&self->m_pageSizeBox, "10 条");
    XComboBox_addItem_2(&self->m_pageSizeBox, "20 条");
    XComboBox_addItem_2(&self->m_pageSizeBox, "50 条");
    XComboBox_setCurrentIndex(&self->m_pageSizeBox, 1);
    XWidget_setFixedHeight((XWidget*)&self->m_pageSizeBox, 28);
    XWidget_setMinimumWidth((XWidget*)&self->m_pageSizeBox, 92);
    XObject_connect_1((XObject*)&self->m_pageSizeBox,
                      XSignal(XComboBox_currentIndexChanged_signal),
                      (XObject*)&self->m_pageSizeBox, histPageSizeSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_pageSizeBox);

    /* 关键字筛选（下推服务端；与 Python 同键名 kw/kwCols）。
     * 时间窗/列筛选/导出/折线图在顶部工具条（本页自己那套）。 */
    XLineEdit_init(&self->m_kw, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_kw);
    XLineEdit_setPlaceholderText(&self->m_kw, "关键字（全部列）");
    XLineEdit_setClearButtonEnabled(&self->m_kw, true);
    XWidget_setFixedHeight((XWidget*)&self->m_kw, 28);
    XWidget_setMinimumWidth((XWidget*)&self->m_kw, 160);
    XObject_connect_1((XObject*)&self->m_kw,
                      XSignal(XLineEdit_returnPressed_signal),
                      (XObject*)&self->m_kw, histKwReturnSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_kw);

    if (self->m_bar) {
        XWidget* row2[4];
        row2[0] = (XWidget*)&self->m_prev;
        row2[1] = (XWidget*)&self->m_next;
        row2[2] = (XWidget*)&self->m_pageSizeBox;
        row2[3] = (XWidget*)&self->m_kw;
        HsfDataBar_attachRow2(self->m_bar, row2, 4, NULL, 0);
    }

    self->m_pageSize = HIST_PAGE_LIMIT;
    self->m_kwText[0] = '\0';

    /* 图表全量数据容器（惰性建 10 条列向量；随页对象释放）。 */
    (void)histChartDataEnsure(self);
    self->m_chartDataDirty = true;   /* 首屏数据到达后按筛选条件取一遍 */

    /* 应答轮询（200ms）+ 首屏请求。 */
    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        /* 轮询周期取本地设置「界面刷新周期」（数据刷新卡；缺省 200ms）。 */
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, histTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }
    histRequest(self, 0);

    hsfUi_registerPageFontApplier(HSF_PAGE_DATA_HISTORY, &self->m_base,
                                  histApplyFonts);
    /* [页销毁收尾] ① 停并释放应答轮询定时器（它无父对象、userData=self，
     * 不停就会在页面释放后继续回调 → UAF）；② 释放 m_chartPts[k] 那 10 条
     * XVector 堆对象，否则每进一次历史页泄漏一次。destroyed 在
     * VXObject_deinit 早期发出（此时页面字段仍有效），用 connect_2
     * （无 receiver）把 self 作为上下文闭包。 */
    XObject_connect_2((XObject*)&self->m_base,
                      XSignal(XObject_destroyed_signal),
                      histPageDestroyedSlot);
    XWidget_show(&self->m_base);
    return &self->m_base;
}

/** @brief 页销毁：停并释放应答轮询定时器 + 释放图表全量容器。
 *  @note  用 connect_2（无 receiver）：槽首参=信号 sender，即页对象本身，
 *         与本模块"receiver 可以是 sender 自身"的既有写法一致。destroyed
 *         在 VXObject_deinit 开头发出，此时页面字段仍有效，且早于子控件
 *         级联析构——向量里只存 POD（HsfHistPoint），不引用任何控件，故
 *         释放次序安全。
 *  @note  [定时器必须在这里收] `m_timer` 是 `XTimer_create_ex` 造出来的
 *         **独立堆对象、无父对象**（XTimer_create_ex 不挂 parent），页
 *         字段是它唯一的持有者。旧实现页面销毁只清 m_chartPts，于是
 *         ① 定时器对象本身泄漏；② 更致命的是它继续按 200ms 触发，回调
     *         `histTimerCb(void* userData=self)` 拿着**已释放的页对象**去读
     *         m_exportFetching/m_chartFetching 并写 m_status——标准 UAF。
 *         `VXObject_deinit` 里的 unregisterTimers 救不了：它按**注册者**
 *         反查（这里注册者是那个 XTimer 对象），不是按 userData 反查，
 *         所以页面析构时那张表项纹丝不动。故必须显式 stop + delete
 *         （delete 内的 VXTimer_deinit 也会 stop，此处先 stop 是为把
 *         "停表 → 释放"之间的窗口压到零，防止释放瞬间还有一拍排进队列）。 */
static void histPageDestroyedSlot(XObject* sender, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)sender;
    int k;
    (void)args;
    if (!self) return;
    if (self->m_timer) {
        XTimer_stop_base(self->m_timer);
        XClassDelete(self->m_timer);
        self->m_timer = NULL;
    }
    /* [导出在途收尾] 关掉 CSV 句柄/丢弃 XLSX 文档（半成品 CSV 一并尽力
     * 删除），别把句柄与行缓冲随页一起带走（泄漏）。 */
    if (self->m_exportFetching)
        HsfExportWriter_abort(&self->m_exportWriter);
    for (k = 0; k < HIST_SERIES_MAX; ++k) {
        if (self->m_chartPts[k]) {
            XClassDelete(self->m_chartPts[k]);
            self->m_chartPts[k] = NULL;
        }
    }
    self->m_chartFetching = false;
    self->m_chartRootCount = 0;
}

/** @brief 字号实时重放（本地设置改动时由 HsfUi 调回）。 */
static void histApplyFonts(XWidget* page, int pct)
{
    HsfDataHistory* self = (HsfDataHistory*)page;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title, 16 * pct / 100);
    XLabel_setTextPixelSize(&self->m_status, 12 * pct / 100);
    /* 行高随字号同步（仅当前页已填的行）。 */
    if (self->m_rowBaseH > 0) {
        const int rh = (self->m_rowBaseH * pct + 50) / 100;
        int r;
        for (r = 0; r < self->m_shownCount; ++r)
            XTableView_setRowHeight(&self->m_table.m_base, r, rh);
    }
    if (self->m_bar) HsfDataBar_applyFont(self->m_bar, pct);
    /* [行2 页级控件随字号] 分页/关键字已并入工具条行 2：固定高与字号
     * 必须跟工具条同一比例重放，否则字号放大后这排控件比同行矮一截。 */
    {
        XWidget* row2[4];
        int i;
        const int h = 28 * pct / 100;
        row2[0] = (XWidget*)&self->m_prev;
        row2[1] = (XWidget*)&self->m_next;
        row2[2] = (XWidget*)&self->m_pageSizeBox;
        row2[3] = (XWidget*)&self->m_kw;
        for (i = 0; i < 4; ++i) XWidget_setFixedHeight(row2[i], h);
    }
    {
        /* [列宽同源] 与建表共用 m_colWidths（实测值，不再另抄一份 kW[]）。 */
        int c;
        for (c = 0; c < HSF_HISTORY_DISPLAY_COLS; ++c)
            XTableView_setColumnWidth(&self->m_table.m_base, c,
                                      self->m_colWidths[c] * pct / 100);
    }
}

void HsfDataHistory_adapt(XWidget* page, bool compact)
{
    HsfDataHistory* self = (HsfDataHistory*)page;
    if (!self) return;
    /* 竖屏/窄窗：页边距 16,12→8,6 腾内容宽（与本页 create 边距同源）。
     * 幂等（帧泵每循环调用）。本页是自建根结构（非 HsfDataTablePage），
     * 必须走自己的 adapt——历史上误挂 HsfDataTablePage_adapt 曾把本页
     * 强转成异型结构读 m_bar 偏移（垃圾指针）导致启动段错误。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 8 : 16, compact ? 6 : 12,
                               compact ? 8 : 16, compact ? 6 : 12);
    if (self->m_bar)
        HsfDataBar_adapt(self->m_bar, compact);
    /* [页级行 2 控件同步收最小宽 2026-11-08] 工具条自己那半段已经按可用宽
     * 分级收窄（HsfDataBar_adapt），但「上一页/下一页/每页条数/关键字」是
     * 本页挂进去的，工具条管不到。1024 宽窗实测：工具条收到底后行 2 仍差
     * 8px，关键字框右端被切。这里按页宽把两个可变宽控件的最小宽同步下调
     * （160→80 / 92→72）；宽窗回填原值。幂等（setMinimumWidth 同值早退）。 */
    {
        const int pw = XWidget_width(page);
        const bool tight = (pw > 0 && pw < 1100);
        XWidget_setMinimumWidth((XWidget*)&self->m_kw, tight ? 64 : 160);
        /* [「每页条数」下限按实测 2026-11-11 走查] 紧缩档写死 64px：字号
         * 实测「50 条」文本 + 箭头列 ~24 之后 ≈70 > 64 ⇒ 档位显示成
         * 「20…」（10/20/50 分不清；1024x768 走查实锅，判低危）。改走
         * 工具条同款 HsfDataBar_comboMinWidth（当前字体实测最宽项 +
         * 箭头/框边，与 floorW 取大）——选项是固定三档，量一次随字号
         * 重放自适应；宽档 floor 92 口径不变。 */
        XWidget_setMinimumWidth(
            (XWidget*)&self->m_pageSizeBox,
            HsfDataBar_comboMinWidth(&self->m_pageSizeBox, tight ? 64 : 92));
    }
    XLayout_activate((XLayout*)&self->m_layout);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON && XPUSHBUTTON_ON && XLAYOUT_ON */
