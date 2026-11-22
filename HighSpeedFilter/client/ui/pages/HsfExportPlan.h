/**
 * @file       HsfExportPlan.h
 * @brief      数据三页「导出筛选全量」的**纯逻辑**助手：批次计划。
 *
 * @details [为什么单独成文件]
 *           导出全量取数的批次归属是契约级逻辑：应答槽只有一份，页面靠
 *           服务端回显的 (offset,limit) 认领应答——批行数一旦与页大小
 *           （10/20/50）或图表批（探测 1..8 / 正式 41..49）撞键，迟到的
 *           应答会被铺错表（历史页复审②的同源根因）。计划算错一处就是
 *           「导出重复/漏行/少批」。照 HsfChartZoom 的既定范式把它抽成
 *           不依赖 GUI 的纯函数，回归锁（hsf_regression_test.c §Q）单 TU
 *           收录逐条钉住：
 *             · 按**筛选命中的总行数**分批（不是当前页那几十行）；
 *             · 逐批**连续**取（步长=批行数），跨页不重不漏；
 *             · 命中行数超上限 ⇒ 截断且截断行数必须显式进状态行；
 *             · 计划是 (total, chunk, maxRows) 的纯函数——筛选条件变化
 *               （total 变）后重算，无任何隐藏状态。
 *
 * @note  本文件不碰控件与网络，只做整数算术 ⇒ 可在无 GUI 的测试宿主里
 *        直接调用，且零堆分配（无泄漏面）。助手本体只随 HsfClient 编译
 *        （不进任何库），测试单 TU 收录不会与库内符号撞键。
 */
#ifndef HSFEXPORTPLAN_H
#define HSFEXPORTPLAN_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 导出全量的行缓存上限（行）。
 *  @details 行缓存为整行显示文本（历史页 16 列 × 48B ≈ 0.8KB/行），
 *           12000 行 ≈ 9.5MB 峰值——对齐图表口径 HIST_CHART_MAX_ROWS 的
 *           量级，超出**显式截断**并在状态行注明（导出不抽样，截断是
 *           唯一的省略方式，不许静默丢行）。 */
#define HSF_EXPORT_PLAN_MAX_ROWS  12000

/** @brief 导出批行数轮转区间（与 HSF_CHART_ZOOM_CHUNK_* 同源：41..49）。
 *  @details [为什么禁用 50] 页大小档位 10/20/50 与导出批若同 limit，
 *           取数期间用户翻页/改筛选后在途的旧批应答会被当成页应答铺进
 *           表格（键完全相同无法区分）。41..49 与页大小、图表探测批
 *           （1..8）两两不相交——历史页的既有编译期闸
 *           （HsfDataHistory.c 的 #error 段）同款口径。 */
#define HSF_EXPORT_PLAN_CHUNK_MIN  41
#define HSF_EXPORT_PLAN_CHUNK_MAX  49

/** @brief 一轮导出取数的批次计划。 */
typedef struct HsfExportPlan {
    int  batches;   /**< 计划批数（total<=0 ⇒ 0）。 */
    int  step;      /**< 批偏移步进（行）＝批行数 ⇒ 逐批连续取、不重不漏。 */
    int  fetchRows; /**< 取数行数上限（截断时= maxRows 向下取整到整批）；
                         未截断时 == total。 */
    bool truncated; /**< total > fetchRows ⇒ 命中行数超上限，导出被截断。 */
} HsfExportPlan;

/**
 * @brief 规划一轮导出取数的批数与步进（连续取，不跳批）。
 * @param total   筛选命中的总行数（探测批应答里的 total；<=0 ⇒ 0 批）。
 * @param chunk   每批行数（本轮实际下发的 limit；<=0 按 1 处理防除零）。
 * @param maxRows 行缓存上限（<=0 = 不设上限）。
 * @param out     输出计划（可为 NULL：只返回批数）。
 * @return 计划批数（total<=0 返回 0）。
 * @details [与图表计划的分界] 图表（HsfChartZoom_planBatches）按目标点数
 *          **跳批抽样**铺满全区间；导出不抽样——必须逐批连续取回**每一行**
 *          （步长=批行数），所以批数就是 ceil(fetchRows/chunk)。
 *          [截断口径] total 超上限时把取数行数钉在「maxRows 向下取整到
 *          整批」⇒ 每批都取满 chunk 行、不产生只装一半的残批；上限比一批
 *          还小时至少取一批（残批只会让状态行少报）。
 */
int HsfExportPlan_planBatches(int total, int chunk, int maxRows,
                              HsfExportPlan* out);

#ifdef __cplusplus
}
#endif

#endif /* HSFEXPORTPLAN_H */
