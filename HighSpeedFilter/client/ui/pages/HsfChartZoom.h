/**
 * @file       HsfChartZoom.h
 * @brief      历史页「图表缩放取数」的**纯逻辑**助手（批次计划 / 视口换算 /
 *             窗口覆盖判定 / 取数 limit 轮转）。
 *
 * @details [为什么单独成文件]
 *           这几段是缩放取数的**契约核心**：算错一处就是现场那三种症状
 *           （曲线消失 / 曲线错位 / 尖角）。原先它们埋在页面大文件里，
 *           静态函数没法被回归锁直接钉住；抽成不依赖 GUI 的纯函数后，
 *           回归测试可单 TU 收录本文件（同 HsfTableSort.c 的做法）逐条钉：
 *             · 窗口模式窗内**逐行**取（不跳批、不丢段）；
 *             · 全量模式仍是跨区间跳批抽样（未缩放时看全貌）；
 *             · 可见窗 ⇄ 绝对时刻的换算与基准（数据与轴域同基准）；
 *             · "已取窗是否覆盖可见窗"（决定要不要再取）；
 *             · 取数 limit 轮转（相邻两轮键必不相等 ⇒ 迟到应答不串味）。
 *
 * @note  本文件**不碰任何控件与网络**，只做整数/双精度算术 ⇒ 可在无 GUI
 *        的测试宿主里直接调用，且零堆分配（无泄漏面）。
 */
#ifndef HSFCHARTZOOM_H
#define HSFCHARTZOOM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 正式批（真实数据批）的行数区间（每轮取数在区间内轮转取值）。
 *  @details [为什么不是服务端上限 50] 应答归属只能靠服务端回显的
 *           (offset,limit) 认领（应答槽只有一份，见 histTimerCb）。页大小
 *           可选 10/20/50；正式批若也用 50，则"取消图表累加"后同 offset 的
 *           在途批与随后的页请求**完全同键**，迟到的批会被当成表格应答
 *           铺进表格。故正式批取 41..49，与页大小两两不相交。 */
#define HSF_CHART_ZOOM_CHUNK_MIN  41
#define HSF_CHART_ZOOM_CHUNK_MAX  49
/** @brief 探测批（只为了拿 total）的行数区间。 */
#define HSF_CHART_ZOOM_PROBE_MIN  1
#define HSF_CHART_ZOOM_PROBE_MAX  8
/** @brief 窗口模式下"窗内逐行全分辨率"的行数上限。
 *  @details 超过 ⇒ 窗内改跳批抽样（曲线铺满视窗但仍尖角，状态行注明）。 */
#define HSF_CHART_ZOOM_WIN_FULL_ROWS_DEFAULT  12000
/** @brief 判定"可见窗 ≈ 全量域"的比例门限（90%）。 */
#define HSF_CHART_ZOOM_FULL_VIEW_NUM  9
#define HSF_CHART_ZOOM_FULL_VIEW_DEN  10
/** @brief 判定"已取窗够密、不必为连续度重取"的比例门限（1/4）。 */
#define HSF_CHART_ZOOM_RESAMPLE_NUM  1
#define HSF_CHART_ZOOM_RESAMPLE_DEN  4

/** @brief 一轮取数的批次计划。 */
typedef struct HsfChartZoomPlan {
    int  batches;  /**< 计划批数（>=1）。 */
    int  step;     /**< 批偏移步进（行）：批 b 的 offset = b * step。 */
    bool sampled;  /**< true=窗内抽样（窗内行数超上限，曲线仍尖角）。 */
} HsfChartZoomPlan;

/**
 * @brief 规划一轮取数的批数与步进。
 * @param total        本轮要取的总行数（服务端 COUNT）。
 * @param chunk        每批行数（本轮实际使用的 limit）。
 * @param windowMode   true=窗口模式（按可见时间窗取）；false=全量模式。
 * @param winFullRows  窗口模式下"窗内逐行取"的行数上限。
 * @param wantPoints   全量模式的目标点数（每图例上限；决定批数）。
 * @param maxRows      内存硬上限（行；批数不得乘出它）。
 * @param out          输出（可为 NULL）。
 * @return 计划批数（>=1；out 为 NULL 时只返回值）。
 * @details [窗口模式 = 连续取] 窗内行数不超上限时，步进 = 批行数 ⇒ 逐行
 *          取回窗内**每一行**（不跳批、不丢段）⇒ 曲线是窗内全分辨率
 *          （用户要的"连续"）。窗内行数超上限时**用满内存上限**做均匀
 *          跳批（不是按目标点数只取十来个簇——簇间空档会让曲线依旧断续），
 *          并置 sampled 标记供状态行注明。[全量模式 = 跨区间跳批] 未缩放
 *          时看全貌，按目标点数铺满全区间。
 */
int HsfChartZoom_planBatches(int total, int chunk, bool windowMode,
                             int winFullRows, int wantPoints, int maxRows,
                             HsfChartZoomPlan* out);

/**
 * @brief 一次「缩放视图」：**轴域与取数窗的唯一事实源**。
 * @details [为什么必须收成一个结构] 缩放取数的判据是「取回的数据点必须与
 *          当前坐标轴的轴域同源同参」。轴域（相对基准秒）与取数窗（绝对
 *          epoch ms）若各算一遍，任何一处取整/外扩/基准不同都会让点落到
 *          轴域外（现场「缩放后曲线挤在左边缘一小段」）。本结构由
 *          HsfChartZoom_view **一次算出**四件东西：
 *            · xLo/xHi     —— 轴域（绘图与视口重放都用这一对值）；
 *            · viewLo/Hi   —— 轴域换算出的绝对时刻（判定/文案用）；
 *            · fetchLo/Hi  —— 取数窗（= view 外扩 1%、最小 1s）。
 *          页面只许把 fetchLo/Hi 发给服务端，把 xLo/xHi 留在轴域上：
 *          两者天然同基同参，不存在"另算一遍"的错位。
 */
typedef struct HsfChartZoomView {
    bool    valid;    /**< 换算成功（基准已定 + 域合法）。 */
    double  xLo;      /**< 轴域下界（相对基准的秒数）——绘图/视口重放用。 */
    double  xHi;      /**< 轴域上界。 */
    int64_t viewLo;   /**< 轴域对应的绝对时刻下界（epoch ms，不外扩）。 */
    int64_t viewHi;   /**< 轴域对应的绝对时刻上界。 */
    int64_t fetchLo;  /**< 取数窗下界（绝对 ms；= viewLo 外扩 1%、最小 1s）。 */
    int64_t fetchHi;  /**< 取数窗上界。 */
} HsfChartZoomView;

/**
 * @brief 由轴域 + 基准算出「缩放视图」（轴域/绝对窗/取数窗一次算齐）。
 * @param baseTs X 基准时刻（epoch ms；<=0 = 未定 ⇒ 返回 false）。
 * @param xlo    轴域下界（相对秒）。
 * @param xhi    轴域上界（相对秒）。
 * @param out    输出（可为 NULL：只判有效性）。
 * @return 换出有效视图返回 true。
 */
bool HsfChartZoom_view(int64_t baseTs, double xlo, double xhi,
                       HsfChartZoomView* out);

/**
 * @brief 取第 i 条数据的绝对时间戳（epoch ms；数组须按 ts 升序）。
 * @details 用访问器而不是裸数组：图表的点是 `{x, y, ts}` 结构（页面侧），
 *          回归锁手里是纯 ts 数组——两边共用同一段统计逻辑，又不强迫
 *          页面为统计另建临时数组。
 */
typedef int64_t (*HsfChartZoomTsAt)(const void* user, int index);

/**
 * @brief 视口内（含两端）的数据点数。
 * @param view 缩放视图（viewLo/viewHi 为视口）。
 * @param at   取 ts 的访问器。
 * @param user 访问器上下文。
 * @param n    数据条数。
 * @return 落在 [viewLo, viewHi] 内的条数。
 */
int HsfChartZoom_countInView(const HsfChartZoomView* view,
                             HsfChartZoomTsAt at, const void* user, int n);

/**
 * @brief 数据是否铺满视口两端（首末点在视口内且跨视口 ≥98%）。
 * @details [判什么] 缩放取数取回的行必须覆盖整个视口——只覆盖一半时曲线
 *          看着"从中间断掉"（用户口径"不够连续"）。本函数只看首末两点
 *          的跨度：≥98% 视口宽视为铺满（留 2% 给末端采样间隔）。
 * @return 铺满返回 true；无点/只有一点/跨度不足返回 false。
 */
bool HsfChartZoom_viewFilled(const HsfChartZoomView* view,
                             HsfChartZoomTsAt at, const void* user, int n);

/** @brief 窗口状态行要素（页面与回归锁共用同一文案）。 */
typedef struct HsfChartZoomStatus {
    int  rowsHeld;   /**< 已取窗内总行数（服务端窗口 total；0=没有行）。 */
    int  rowsInView; /**< 落在视口内的点数。 */
    bool sampled;    /**< 窗内抽样（窗内行数超上限）。 */
    bool filled;     /**< 数据铺满视口两端。 */
} HsfChartZoomStatus;

/**
 * @brief 拼「图表数据」状态行（窗口口径；不得复用全量区间的文案）。
 * @param whenLo 视口下界文本（本地时间，如 "11:03:12"；NULL→"--"）。
 * @param whenHi 视口上界文本。
 * @param st     统计要素（NULL 或 rowsHeld<=0 ⇒ "该时间段无记录"）。
 * @param buf    输出缓冲。
 * @param cap    容量。
 * @return 写入长度（同 snprintf 口径）。
 */
int HsfChartZoom_statusText(const char* whenLo, const char* whenHi,
                            const HsfChartZoomStatus* st, char* buf, int cap);

/**
 * @brief 可见 X 域（相对基准的秒数）→ 绝对时间窗（epoch ms）。
 * @param baseTs X 基准时刻（epoch ms；<=0 = 未定）。
 * @param xlo    可见域下界（相对秒）。
 * @param xhi    可见域上界（相对秒）。
 * @param outLo  输出下界（可为 NULL）。
 * @param outHi  输出上界（可为 NULL）。
 * @return 换出有效窗返回 true（baseTs<=0 / 域非法 / hi<=lo → false）。
 * @note  数据点的 X 值与轴域用的是**同一个基准** ⇒ 换算出的绝对窗就是
 *        "服务端该按什么时间窗取行"的唯一事实源。
 */
bool HsfChartZoom_visibleWindow(int64_t baseTs, double xlo, double xhi,
                                int64_t* outLo, int64_t* outHi);

/**
 * @brief 可见窗是否已覆盖全量域（≥90%）——即"用户没缩放，看的是全貌"。
 */
bool HsfChartZoom_isFullView(int64_t visLo, int64_t visHi,
                             int64_t fullLo, int64_t fullHi);

/**
 * @brief 已取窗是否覆盖可见窗（覆盖 ⇒ 不用再取数，只本地重采样）。
 */
bool HsfChartZoom_windowCovers(int64_t winLo, int64_t winHi,
                               int64_t visLo, int64_t visHi);

/**
 * @brief 取数窗 = 可见窗各外扩 1%（最小 1s）——边界行也取回，曲线才铺满两端。
 */
void HsfChartZoom_expandWindow(int64_t visLo, int64_t visHi,
                               int64_t* outLo, int64_t* outHi);

/**
 * @brief 本轮取数的两个 limit（探测批 / 正式批），按轮转序号取值。
 * @details [为什么要轮转] 相邻两轮取数若用同一个 limit，上一轮**被顶掉**
 *           的在途应答会被下一轮按同键 (offset,limit) 认领 ⇒ 旧窗的行
 *           混进新窗（缩放取数最忌讳：数据与视口错位）。轮转取值让相邻
 *           两轮的键必不相等；迟到应答会被判"无主"丢弃。
 */
void HsfChartZoom_nextLimits(int seq, int* probe, int* chunk);

#ifdef __cplusplus
}
#endif

#endif /* HSFCHARTZOOM_H */
