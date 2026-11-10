/**
 * @file       HsfTableSort.c
 * @brief      表格「双击列标题排序」共用实现（详见 HsfTableSort.h）。
 */
#include "HsfTableSort.h"

#include "XMemory.h"
#include "XPrintf.h"
#include <stdlib.h>
#include <string.h>

/* ==================== 状态 ==================== */

void HsfTableSort_init(HsfTableSort* s)
{
    if (!s) return;
    s->m_col = -1;
    s->m_desc = true;
}

void HsfTableSort_toggle(HsfTableSort* s, int col)
{
    if (!s || col < 0) return;
    if (s->m_col == col) {
        if (s->m_desc) {
            s->m_desc = false;          /* 降序 → 升序 */
        } else {
            s->m_col = -1;             /* 升序 → 取消排序 */
            s->m_desc = true;
        }
    } else {
        s->m_col = col;
        s->m_desc = true;              /* 新列 → 降序 */
    }
}

/* ==================== 排序 ==================== */

/* [为什么直接用框架的 XTableWidget_sortItems]
 * 框架已按 Qt 口径（QTableModel::sort）实现了整行重排：空值下沉、
 * 稳定交换排序、连同 verticalHeaderItems 一起搬。app 层只该管
 * "点哪列 / 什么次序 / 取消后怎么办"这件**策略**事，重排交给框架，
 * 免得两边各写一套排序（口径迟早漂）。
 * [层次] 排序**原语**在框架（Qt 有）；"双击表头 ⇒ 三态切换"这个
 * **行为** Qt 没有，属应用层扩展，故本文件在 app 侧。 */

void HsfTableSort_apply(HsfTableSort* s, XTableWidget* table)
{
    if (!s || !table || s->m_col < 0) return;
    /* order: 0=升序 / 1=降序（框架口径同 Qt::SortOrder）。 */
    XTableWidget_sortItems(table, s->m_col, s->m_desc ? 1 : 0);
}

void HsfTableSort_updateIndicator(const HsfTableSort* s, XTableWidget* table)
{
    XHeaderView* hh;
    if (!s || !table) return;
    hh = XTableView_horizontalHeader(&table->m_base);
    if (!hh) return;
    if (s->m_col < 0) {
        XHeaderView_setSortIndicatorShown(hh, false);
        return;
    }
    XHeaderView_setSortIndicatorShown(hh, true);
    XHeaderView_setSortIndicator(hh, s->m_col,
                                 s->m_desc ? XHeaderViewSortOrder_Descending
                                            : XHeaderViewSortOrder_Ascending);
}
