/**
 * @file       HsfAlarmLogic.c
 * @brief      报警页纯逻辑助手实现（见 HsfAlarmLogic.h）。
 * @note       零堆分配、零 GUI 依赖 ⇒ 可被回归锁单 TU 收录直接调用。
 */
#include "HsfAlarmLogic.h"

#include <string.h>

bool HsfAlarmLogic_rowVisible(const HsfAlarmFilter* filter, int64_t ts,
                              const char* kindText, const char* name,
                              const char* state)
{
    const char* kw;
    if (!filter) return true;   /* 无条件 = 全部可见。 */
    /* 1. 类型全等（空条件不限）。 */
    if (filter->m_kindText && filter->m_kindText[0]) {
        if (!kindText || strcmp(kindText, filter->m_kindText) != 0)
            return false;
    }
    /* 2. 时间窗（界 <=0 不限；记录无时刻 ts<=0 时不参与时间窗淘汰）。 */
    if (ts > 0) {
        if (filter->m_loTs > 0 && ts < filter->m_loTs) return false;
        if (filter->m_hiTs > 0 && ts > filter->m_hiTs) return false;
    }
    /* 3. 关键字：设备名/类型/状态任一列包含即命中（不含时间列）。 */
    kw = filter->m_kw;
    if (kw && kw[0]) {
        if (!(name && strstr(name, kw)) &&
            !(kindText && strstr(kindText, kw)) &&
            !(state && strstr(state, kw)))
            return false;
    }
    return true;
}

int HsfAlarmLogic_pageCount(int totalRows, int pageSize)
{
    if (pageSize <= 0) return 1;
    if (totalRows <= 0) return 1;   /* 空全集也是 1 页（第 1/1 页）。 */
    return (totalRows + pageSize - 1) / pageSize;
}

int HsfAlarmLogic_pageStart(int totalRows, int pageSize, int pageIndex)
{
    int pages;
    /* <=0 按 1 处理必须**先归一再算页数/夹取**：pageCount 对 <=0 恒回 1，
     * 先夹取会把任意页号压成 0 ⇒ 起点=0，违反 HsfAlarmLogic.h:81 的形参
     * 契约（<=0 按 1 ⇒ 起点=夹取后页号）与回归锁 R6 的断言。 */
    if (pageSize <= 0) pageSize = 1;
    pages = HsfAlarmLogic_pageCount(totalRows, pageSize);
    if (pageIndex < 0) pageIndex = 0;
    if (pageIndex >= pages) pageIndex = pages - 1;
    return pageIndex * pageSize;
}
