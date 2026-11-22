/**
 * @file       HsfExportPlan.c
 * @brief      数据三页「导出筛选全量」批次计划实现（见 HsfExportPlan.h）。
 * @note       零堆分配、零 GUI 依赖 ⇒ 可被回归锁单 TU 收录直接调用。
 */
#include "HsfExportPlan.h"

int HsfExportPlan_planBatches(int total, int chunk, int maxRows,
                              HsfExportPlan* out)
{
    int fetchRows;
    int batches;

    if (out) {
        out->batches = 0;
        out->step = 0;
        out->fetchRows = 0;
        out->truncated = false;
    }
    if (total <= 0) return 0;
    if (chunk <= 0) chunk = 1;
    fetchRows = total;
    if (maxRows > 0 && total > maxRows) {
        /* [截断行数 = 上限向下取整到整批] 每批都取满 chunk 行，不产生
         * 只装一半的残批；上限比一批还小时至少取一批（否则一行都取不到，
         * "截断"会退化成"没导出"）。 */
        fetchRows = (maxRows / chunk) * chunk;
        if (fetchRows <= 0) fetchRows = chunk;
    }
    batches = (fetchRows + chunk - 1) / chunk;
    if (batches < 1) batches = 1;
    if (out) {
        out->batches = batches;
        out->step = chunk;      /* 连续取：步长=批行数 ⇒ 跨页不重不漏。 */
        out->fetchRows = fetchRows;
        out->truncated = fetchRows < total;
    }
    return batches;
}
