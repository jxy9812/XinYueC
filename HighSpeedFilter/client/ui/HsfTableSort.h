/**
 * @file       HsfTableSort.h
 * @brief      表格「双击列标题排序」的共用实现（历史页 / 日志页 / 报警页同款）。
 *
 * @details    口径与 Python 客户端 pages_other.py 的 `_header_dblclicked`
 *             **逐字一致**：
 *               · 新列  → 该列降序；
 *               · 同列降 → 升序；
 *               · 同列升 → 取消排序（回到服务端原序）。
 *             排序作用于**当前页**（服务端分页取回的那一页），不重发请求。
 *             排序键：整格能解析成数字的按数值比（数值列排在文本列之前），
 *             否则按字符串比 —— 与 Python 端 `_sort_key` 同口径。
 *
 *             [为什么自己排而不是用框架的] 框架的 XTableView_sortByColumn
 *             只**记录**排序列/序（m_sortColumn/m_sortOrder），并不真的
 *             重排 item；且框架的排序语义是"点表头"，与现场习惯的
 *             "双击表头"不同 ⇒ 在页面侧实现。
 */
#ifndef HSF_TABLE_SORT_H
#define HSF_TABLE_SORT_H

#include "XTableWidget.h"
#include "XHeaderView.h"

typedef struct HsfTableSort {
    int  m_col;    /**< 排序列下标；-1 = 不排序（服务端原序）。 */
    bool m_desc;   /**< true = 降序。 */
} HsfTableSort;

/** @brief 初始化为"不排序"。 */
void HsfTableSort_init(HsfTableSort* s);

/** @brief 双击表头：新列⇒降序；同列降⇒升序；同列升⇒取消排序。 */
void HsfTableSort_toggle(HsfTableSort* s, int col);

/** @brief 按当前状态就地重排表格行（读格→排序→写回；不动列/行数）。 */
void HsfTableSort_apply(HsfTableSort* s, XTableWidget* table);

/** @brief 刷新表头排序指示器（无排序时隐藏）。 */
void HsfTableSort_updateIndicator(const HsfTableSort* s, XTableWidget* table);

#endif /* HSF_TABLE_SORT_H */
