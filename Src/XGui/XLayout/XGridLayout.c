/******************************************************************************
 * @file       XGridLayout.c
 * @brief      XGridLayout 网格布局实现（对标 Qt 6.8 QGridLayout 算法）。
 * @details    本文件实现 XGridLayout 的：
 *             - 类虚函数表（继承 XLayout 全部槽位，不新增虚函数）与
 *               生命周期（Deinit 释放单元格/行列数组；Copy 仅复制配置；
 *               Move 转移条目与全部网格数组所有权）；
 *             - 单元格管理：m_cells 与条目数组一一平行，追加/取出同步；
 *               添加时按最大占用行列扩容，移除时保留现有行列数；
 *             - 行列尺寸收集：单格条目直接取宽高，跨行跨列条目按
 *               被占用行列按 Qt distributeMultiBox 语义回填；
 *             - 主轴分配：与盒式布局同款三段算法（不足→按最小缩小、
 *               中间→按 (hint-min) 差值比例收缩、有余→stretch 权重/
 *               expanding 均分/全部均分），逐线夹到最大尺寸；
 *             - 几何分配：去边距→行列各自分配→跨格合并→原点角
 *               镜像（TopRight/BottomLeft/BottomRight）→逐条目
 *               setGeometry（条目自身对齐在控件条目内完成）；
 *             - 尺寸协商：sizeHint/minimumSize/maximumSize/
 *               expandingDirections、heightForWidth 体系（先列分配
 *               再以 hfw 高度回填行高）。
 *             本文件不依赖任何平台 API，嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGridLayout.h"
#include "XLayout_Internal.h"
#include "XWidget.h"
#include "XMemory.h"
#include <string.h>

#if XLAYOUT_ON && XLAYOUT_GRID_ON

/* ==================== 内部结构 ==================== */

/** @brief 单条行/列线的尺寸协商数据（分配输入）。 */
typedef struct XGridGeom
{
    int  min;        /**< 最小尺寸。 */
    int  hint;       /**< 首选尺寸。 */
    int  max;        /**< 最大尺寸。 */
    int  stretch;    /**< 伸展因子（0=不参与多余空间分配）。 */
    bool expanding;  /**< 该线是否可伸展（任意条目 expanding）。 */
    bool empty;      /**< 无任何可见条目的空线。 */
} XGridGeom;

/* ==================== 数组扩容助手 ==================== */

/** @brief 通用堆数组扩容；返回是否成功。 */
static bool XGridLayout_growArray(void** data, int* capacity, int need,
                                  size_t elemSize)
{
    int newCap;
    void* p;
    if (!data || !capacity) return false;
    if (need <= *capacity) return true;
    newCap = *capacity ? *capacity : XGRIDLAYOUT_CHUNK;
    while (newCap < need) newCap <<= 1;
    p = XRealloc_System(*data, (size_t)newCap * elemSize);
    if (!p) return false;
    memset((char*)p + (size_t)(*capacity) * elemSize, 0,
           (size_t)(newCap - *capacity) * elemSize);
    *data = p;
    *capacity = newCap;
    return true;
}

/** @brief 确保行列属性数组（伸展/最小尺寸）能容纳指定行/列数。 */
static bool XGridLayout_ensureLineArrays(XGridLayout* self, int rows, int cols)
{
    if (!XGridLayout_growArray((void**)&self->m_rowStretch,
                               &self->m_rowStretchCapacity, rows, sizeof(int)))
        return false;
    if (!XGridLayout_growArray((void**)&self->m_columnStretch,
                               &self->m_columnStretchCapacity, cols, sizeof(int)))
        return false;
    if (!XGridLayout_growArray((void**)&self->m_rowMinHeight,
                               &self->m_rowMinHeightCapacity, rows, sizeof(int)))
        return false;
    if (!XGridLayout_growArray((void**)&self->m_columnMinWidth,
                               &self->m_columnMinWidthCapacity, cols, sizeof(int)))
        return false;
    return true;
}

/** @brief 确保单元格数组容纳指定条目数。 */
static bool XGridLayout_ensureCells(XGridLayout* self, int need)
{
    return XGridLayout_growArray((void**)&self->m_cells, &self->m_cellCapacity,
                                 need, sizeof(XGridLayoutCell));
}

/** @brief 收回全部网格数组内存（deinit 使用）。 */
static void XGridLayout_freeArrays(XGridLayout* self)
{
    XFree_System(self->m_cells);
    XFree_System(self->m_rowStretch);
    XFree_System(self->m_columnStretch);
    XFree_System(self->m_rowMinHeight);
    XFree_System(self->m_columnMinWidth);
    XFree_System(self->m_cacheColPos);
    XFree_System(self->m_cacheColSize);
    XFree_System(self->m_cacheRowPos);
    XFree_System(self->m_cacheRowSize);
    self->m_cells = NULL;
    self->m_rowStretch = NULL;
    self->m_columnStretch = NULL;
    self->m_rowMinHeight = NULL;
    self->m_columnMinWidth = NULL;
    self->m_cacheColPos = NULL;
    self->m_cacheColSize = NULL;
    self->m_cacheRowPos = NULL;
    self->m_cacheRowSize = NULL;
    self->m_cellCapacity = 0;
    self->m_rowStretchCapacity = 0;
    self->m_columnStretchCapacity = 0;
    self->m_rowMinHeightCapacity = 0;
    self->m_columnMinWidthCapacity = 0;
    self->m_cacheColPosCount = 0;
    self->m_cacheColSizeCount = 0;
    self->m_cacheRowPosCount = 0;
    self->m_cacheRowSizeCount = 0;
    /* m_rowCount/m_columnCount 由调用方依据条目重算。 */
}

/* ==================== 行列属性访问（内部） ==================== */

/** @brief 返回行伸展因子（越界返回 0）。 */
static int XGridLayout_rowStretchConst(const XGridLayout* self, int row)
{
    if (!self || row < 0 || row >= self->m_rowStretchCapacity || !self->m_rowStretch)
        return 0;
    return self->m_rowStretch[row];
}

/** @brief 返回列伸展因子（越界返回 0）。 */
static int XGridLayout_columnStretchConst(const XGridLayout* self, int column)
{
    if (!self || column < 0 || column >= self->m_columnStretchCapacity ||
        !self->m_columnStretch)
        return 0;
    return self->m_columnStretch[column];
}

/** @brief 返回行最小高度（越界返回 0）。 */
static int XGridLayout_rowMinHeightConst(const XGridLayout* self, int row)
{
    if (!self || row < 0 || row >= self->m_rowMinHeightCapacity ||
        !self->m_rowMinHeight)
        return 0;
    return self->m_rowMinHeight[row];
}

/** @brief 返回列最小宽度（越界返回 0）。 */
static int XGridLayout_columnMinWidthConst(const XGridLayout* self, int column)
{
    if (!self || column < 0 || column >= self->m_columnMinWidthCapacity ||
        !self->m_columnMinWidth)
        return 0;
    return self->m_columnMinWidth[column];
}

/* ==================== 网格尺寸核算 ==================== */

/** @brief 校验/规范化起始行列与跨度；span<0 表示延伸到当前网格边界。 */
static void XGridLayout_normalizeCell(XGridLayout* self,
                                      int* row, int* column,
                                      int* rowSpan, int* columnSpan)
{
    if (*row < 0) *row = 0;
    if (*column < 0) *column = 0;
    if (*rowSpan < 0)
        *rowSpan = (self->m_rowCount > *row) ? (self->m_rowCount - *row) : 1;
    if (*columnSpan < 0)
        *columnSpan = (self->m_columnCount > *column)
                          ? (self->m_columnCount - *column) : 1;
    if (*rowSpan <= 0) *rowSpan = 1;
    if (*columnSpan <= 0) *columnSpan = 1;
}

/* Defined below with the other grid-dimension helpers. */
static void XGridLayout_expand(XGridLayout* self, int rows, int cols);

/** @brief 追加条目并登记单元格（内部核心入口）。 */
static int XGridLayout_appendCellItem(XGridLayout* self, XLayoutItem* item,
                                      bool owned,
                                      int row, int column,
                                      int rowSpan, int columnSpan,
                                      XLayoutAlignments alignment)
{
    int idx;
    XGridLayoutCell* cell;
    if (!self || !item) return -1;
    XGridLayout_normalizeCell(self, &row, &column, &rowSpan, &columnSpan);
    idx = XLayout_appendItem((XLayout*)self, item, owned);
    if (idx < 0) return -1;
    if (idx >= self->m_cellCapacity) {
        if (!XGridLayout_ensureCells(self, idx + 1)) {
            /* 退回条目挂接：仅移除条目数组项，释放责任不变。 */
            (void)XLayout_takeAt_base((XLayout*)self, idx);
            if (owned) XLayoutItem_delete_base(item);
            return -1;
        }
    }
    cell = &self->m_cells[idx];
    memset(cell, 0, sizeof(*cell));
    cell->m_row = row;
    cell->m_column = column;
    cell->m_rowSpan = row + rowSpan - 1;
    cell->m_columnSpan = column + columnSpan - 1;
    if (alignment != 0)
        XLayoutItem_setAlignment(item, alignment);
    /* Qt expands the grid when an item is added and never shrinks it as a
     * side effect of later removals. Keep existing dimensions intact. */
    XGridLayout_expand(self, cell->m_rowSpan + 1,
                       cell->m_columnSpan + 1);
    XLayoutItem_invalidate_base((XLayoutItem*)self);
    return idx;
}

/** @brief 推进默认定位游标（对标 Qt 6.8 QGridLayoutPrivate::setNextPosAfter）。
 *  @details 仅当新位置在游标之后（按当前方向的行/列次序）才推进；
 *  越界自动绕行（先横后竖：列满换行；先竖后横：行满换列）。row/col
 *  传入跨格条目的含结束行列，天然覆盖 span 规范化后的末尾格。 */
static void XGridLayout_setNextPosAfter(XGridLayout* self, int row, int col)
{
    if (!self) return;
    if (self->m_addVertical) {
        if (col > self->m_nextC ||
            (col == self->m_nextC && row >= self->m_nextR)) {
            self->m_nextR = row + 1;
            self->m_nextC = col;
            if (self->m_nextR >= self->m_rowCount) {
                self->m_nextR = 0;
                self->m_nextC++;
            }
        }
    } else {
        if (row > self->m_nextR ||
            (row == self->m_nextR && col >= self->m_nextC)) {
            self->m_nextR = row;
            self->m_nextC = col + 1;
            if (self->m_nextC >= self->m_columnCount) {
                self->m_nextC = 0;
                self->m_nextR++;
            }
        }
    }
}

/** @brief 把网格扩展为至少 rows 行、cols 列（对标 Qt expand：扩容
 *  行列属性数组并同步 m_rowCount/m_columnCount；不足的维按原计数
 *  取最大值，只增不减）。 */
static void XGridLayout_expand(XGridLayout* self, int rows, int cols)
{
    if (!self) return;
    if (rows < self->m_rowCount) rows = self->m_rowCount;
    if (cols < self->m_columnCount) cols = self->m_columnCount;
    if (!XGridLayout_ensureLineArrays(self, rows, cols)) return;
    self->m_rowCount = rows;
    self->m_columnCount = cols;
}

/* ==================== 尺寸分配（与盒式同款三段算法） ==================== */

/* 早期版本曾按最小尺寸比例压缩（shrinkToMin），但 Qt 的 qGeomCalc 从不把
 * 线尺寸压到最小以下（窗口会自动放大到布局最小尺寸）。该逻辑已被 allocate 中
 * “空间不足时按最小逐线给满”的分支取代，这里不再保留死代码。 */

/** @brief 空间介于最小/首选之间：按 (hint-min) 差值比例收缩。 */
static void XGridLayout_shrinkHint(const XGridGeom* g, int n, int available,
                                   int* out)
{
    int i;
    int hintSum = 0;
    int minSum = 0;
    int deficit;
    for (i = 0; i < n; ++i) {
        if (g[i].empty) continue;
        hintSum += g[i].hint;
        minSum += g[i].min;
    }
    deficit = hintSum - available;
    if (hintSum == minSum || deficit <= 0) {
        for (i = 0; i < n; ++i) out[i] = g[i].empty ? 0 : g[i].hint;
        return;
    }
    for (i = 0; i < n; ++i) {
        long long room;
        int cut;
        if (g[i].empty) { out[i] = 0; continue; }
        room = (long long)(g[i].hint - g[i].min);
        cut = (int)((room * deficit) / (hintSum - minSum));
        out[i] = g[i].hint - cut;
        if (out[i] < g[i].min) out[i] = g[i].min;
    }
    {
        int used = 0;
        int remaining = deficit;
        for (i = 0; i < n; ++i) {
            if (g[i].empty) continue;
            used += g[i].hint - out[i];
        }
        remaining -= used;
        i = 0;
        while (remaining > 0 && i < n) {
            int room;
            if (g[i].empty) { i++; continue; }
            room = g[i].hint - g[i].min - (g[i].hint - out[i]);
            if (room > 0) { out[i]--; remaining--; }
            i++;
        }
    }
}

/** @brief 有余空间：stretch 权重 → expanding 均分 → 全部均分，收敛到 max。 */
static void XGridLayout_growExtra(const XGridGeom* g, int n, int extra, int* out,
                                  int active)
{
    int i;
    int left = extra;
    if (!g || !out || active <= 0 || left <= 0) return;
    for (i = 0; i < n; ++i) out[i] = 0;
    while (left > 0) {
        int eligible = 0;
        int sumStretch = 0;
        int expCount = 0;
        bool useStretch = false;
        bool useExpanding = false;
        int added = 0;
        for (i = 0; i < n; ++i) {
            int room;
            if (g[i].empty) continue;
            room = g[i].max - g[i].hint - out[i];
            if (room <= 0) continue;
            eligible++;
            if (g[i].stretch > 0) sumStretch += g[i].stretch;
            if (g[i].expanding) expCount++;
        }
        if (eligible == 0) break;
        useStretch = sumStretch > 0;
        useExpanding = !useStretch && expCount > 0;
        for (i = 0; i < n; ++i) {
            int room;
            int take = 0;
            if (g[i].empty) continue;
            room = g[i].max - g[i].hint - out[i];
            if (room <= 0) continue;
            if (useStretch) {
                if (g[i].stretch <= 0) continue;
                take = (int)((long long)left * g[i].stretch / sumStretch);
            } else if (useExpanding) {
                if (!g[i].expanding) continue;
                take = left / expCount;
            } else {
                take = left / eligible;
            }
            if (take > room) take = room;
            if (take > 0) {
                out[i] += take;
                added += take;
            }
        }
        left -= added;
        if (left <= 0) break;
        /* Integer division can leave a remainder or produce zero for small
           spans. Distribute one pixel at a time in deterministic order. */
        for (i = 0; i < n && left > 0; ++i) {
            int room;
            if (g[i].empty) continue;
            room = g[i].max - g[i].hint - out[i];
            if (room <= 0) continue;
            if (useStretch && g[i].stretch <= 0) continue;
            if (useExpanding && !g[i].expanding) continue;
            out[i]++;
            left--;
        }
        if (added == 0 && left > 0) {
            /* No eligible line accepted a proportional share; the one-pixel
               pass above is the only progress possible. */
            continue;
        }
    }
}

/** @brief 单条线集核心分配：返回各线尺寸（空线恒 0）。 */
static void XGridLayout_allocate(const XGridGeom* g, int n, int spacing,
                                 int space, int* out)
{
    int i;
    int active = 0;
    int minSum = 0;
    int hintSum = 0;
    int available;
    if (!g || !out) return;
    for (i = 0; i < n; ++i) {
        out[i] = 0;
        if (g[i].empty) continue;
        active++;
        minSum += g[i].min;
        hintSum += g[i].hint;
    }
    if (active == 0 || space <= 0) return;
    available = space - spacing * (active - 1);
    if (available < 0) available = 0;
    if (available < minSum) {
        /* 空间不足最小总宽：按最小逐线给满（对标 Qt qGeomCalc：不压到最小以下）。 */
        for (i = 0; i < n; ++i) out[i] = g[i].empty ? 0 : g[i].min;
    } else if (available < hintSum) {
        XGridLayout_shrinkHint(g, n, available, out);
    } else {
        XGridLayout_growExtra(g, n, available - hintSum, out, active);
        for (i = 0; i < n; ++i)
            if (!g[i].empty) out[i] += g[i].hint;
    }
    for (i = 0; i < n; ++i) {
        if (g[i].empty) out[i] = 0;
        if (out[i] < 0) out[i] = 0;
        if (g[i].max >= 0 && out[i] > g[i].max) out[i] = g[i].max;
    }
}

/* ==================== 行列尺寸收集 ==================== */

/** @brief 返回条目在主轴的伸展因子（控件的 horizontal/verticalStretch）。 */
static int XGridLayout_itemStretch(const XLayoutItem* item, bool isColumn)
{
    XWidget* widget;
    XWidgetSizePolicy policy;
    widget = XLayoutItem_widget_base(item);
    if (!widget) return 0;
    policy = XWidget_sizePolicy(widget);
    if (isColumn)
        return XWidgetSizePolicy_horizontalStretch(&policy);
    return XWidgetSizePolicy_verticalStretch(&policy);
}

/** @brief 单条线分配前置声明（跨线回填复用同一 qGeomCalc 兼容分配器）。 */
static void XGridLayout_allocate(const XGridGeom* g, int n, int spacing,
                                 int space, int* out);

/** @brief 把跨线条目的 min/hint/max 按 Qt distributeMultiBox 语义回填。 */
static void XGridLayout_distributeSpan(XGridGeom* out,
                                       int start, int end,
                                       int minSize, int hintSize,
                                       int spanStretch, int spacing)
{
    int n;
    int i;
    long long sumMin = 0;
    long long sumHint = 0;
    long long sumMax = 0;
    XGridGeom* temp;
    int* sizes;
    if (!out || start > end) return;
    n = end - start + 1;
    if (n <= 0) return;
    temp = (XGridGeom*)XMalloc_Hybrid((size_t)n * sizeof(XGridGeom));
    sizes = (int*)XMalloc_Hybrid((size_t)n * sizeof(int));
    if (!temp || !sizes) {
        XFree_Hybrid(temp);
        XFree_Hybrid(sizes);
        return;
    }
    for (i = 0; i < n; ++i) {
        temp[i] = out[start + i];
        temp[i].empty = false;
        if (temp[i].stretch <= 0 && spanStretch > 0)
            temp[i].stretch = spanStretch;
        sumMin += temp[i].min;
        sumHint += temp[i].hint;
        sumMax += temp[i].max;
    }
    if (spacing > 0) {
        sumMin += (long long)spacing * (n - 1);
        sumHint += (long long)spacing * (n - 1);
        sumMax += (long long)spacing * (n - 1);
    }

    /* Qt raises maximum sizes first when the span cannot fit in its current
       maximums, then raises minimums and finally size hints. */
    if (minSize > 0 && sumMax < minSize) {
        for (i = 0; i < n; ++i) temp[i].max = XWIDGET_MAX_SIZE;
        XGridLayout_allocate(temp, n, spacing, minSize, sizes);
        for (i = 0; i < n; ++i) {
            if (out[start + i].min < sizes[i]) out[start + i].min = sizes[i];
            if (out[start + i].max < out[start + i].min)
                out[start + i].max = out[start + i].min;
        }
        sumMin = minSize;
    } else if (minSize > 0 && sumMin < minSize) {
        XGridLayout_allocate(temp, n, spacing, minSize, sizes);
        for (i = 0; i < n; ++i)
            if (out[start + i].min < sizes[i]) out[start + i].min = sizes[i];
    }
    if (hintSize > 0 && sumHint < hintSize) {
        for (i = 0; i < n; ++i) {
            temp[i] = out[start + i];
            temp[i].empty = false;
        }
        XGridLayout_allocate(temp, n, spacing, hintSize, sizes);
        for (i = 0; i < n; ++i) {
            if (out[start + i].hint < sizes[i]) out[start + i].hint = sizes[i];
            if (out[start + i].hint < out[start + i].min)
                out[start + i].hint = out[start + i].min;
            if (out[start + i].stretch <= 0 && spanStretch > 0)
                out[start + i].stretch = spanStretch;
        }
    } else if (spanStretch > 0) {
        for (i = start; i <= end; ++i)
            if (out[i].stretch <= 0) out[i].stretch = spanStretch;
    }
    XFree_Hybrid(temp);
    XFree_Hybrid(sizes);
}

/** @brief 收集一行/列线的尺寸数据（isColumn=true 收集列）。 */
static void XGridLayout_collectLines(const XGridLayout* self, bool isColumn,
                                     XGridGeom* out, int count)
{
    int i;
    int j;
    int lineSpacing;
    if (!self || !out) return;
    lineSpacing = XLayout_effectiveSpacing((const XLayout*)self);
    if (isColumn && self->m_hSpacing >= 0) lineSpacing = self->m_hSpacing;
    if (!isColumn && self->m_vSpacing >= 0) lineSpacing = self->m_vSpacing;
    for (j = 0; j < count; ++j) {
        int minSize;
        int stretch;
        minSize = isColumn ? XGridLayout_columnMinWidthConst(self, j)
                           : XGridLayout_rowMinHeightConst(self, j);
        stretch = isColumn ? XGridLayout_columnStretchConst(self, j)
                           : XGridLayout_rowStretchConst(self, j);
        if (minSize < 0) minSize = 0;
        out[j].min = minSize;
        out[j].hint = minSize;
        out[j].max = (stretch > 0 || minSize <= 0)
                         ? XWIDGET_MAX_SIZE : minSize;
        out[j].stretch = (stretch > 0) ? stretch : 0;
        out[j].expanding = (stretch > 0);
        out[j].empty = true;
    }
    for (i = 0; i < (int)self->m_base.m_itemCount; ++i) {
        const XLayoutItem* item = self->m_base.m_items[i];
        const XGridLayoutCell* cell = self->m_cells ? &self->m_cells[i] : NULL;
        XSize min;
        XSize hint;
        XSize max;
        XLayoutExpandingDirections dirs;
        int start;
        int end;
        int spanStretch;
        if (!item || !cell) continue;
        if (XLayoutItem_isEmpty_base(item)) continue;
        min = XLayoutItem_minimumSize_base(item);
        hint = XLayoutItem_sizeHint_base(item);
        max = XLayoutItem_maximumSize_base(item);
        dirs = XLayoutItem_expandingDirections_base(item);
        start = isColumn ? cell->m_column : cell->m_row;
        end = isColumn ? cell->m_columnSpan : cell->m_rowSpan;
        spanStretch = XGridLayout_itemStretch(item, isColumn);
        if (start == end) {
            XGridGeom* line;
            if (start < 0 || start >= count) continue;
            line = &out[start];
            if (isColumn) {
                if (line->min < min.width) line->min = min.width;
                if (line->hint < hint.width) line->hint = hint.width;
                if (max.width >= 0 && line->max > max.width && line->stretch <= 0)
                    line->max = max.width;
                if (max.width >= XWIDGET_MAX_SIZE)
                    line->max = XWIDGET_MAX_SIZE;
                if (dirs & XLayoutExpandingDirection_Horizontal)
                    line->expanding = true;
            } else {
                if (line->min < min.height) line->min = min.height;
                if (line->hint < hint.height) line->hint = hint.height;
                if (max.height >= 0 && line->max > max.height && line->stretch <= 0)
                    line->max = max.height;
                if (max.height >= XWIDGET_MAX_SIZE)
                    line->max = XWIDGET_MAX_SIZE;
                if (dirs & XLayoutExpandingDirection_Vertical)
                    line->expanding = true;
            }
            if (line->stretch <= 0 && spanStretch > 0)
                line->stretch = spanStretch;
            line->empty = false;
        } else {
            int s;
            int e;
            int k;
            s = start < 0 ? 0 : start;
            if (s >= count) continue;
            e = end >= count ? count - 1 : end;
            if (e < s) continue;
            XGridLayout_distributeSpan(out, s, e,
                                       isColumn ? min.width : min.height,
                                       isColumn ? hint.width : hint.height,
                                       spanStretch, lineSpacing);
            if (isColumn && (dirs & XLayoutExpandingDirection_Horizontal))
                for (k = s; k <= e; ++k) out[k].expanding = true;
            if (!isColumn && (dirs & XLayoutExpandingDirection_Vertical))
                for (k = s; k <= e; ++k) out[k].expanding = true;
            for (k = s; k <= e; ++k) out[k].empty = false;
        }
    }
}

/** @brief 计算行/列分配结果的位置数组（空线位置不动、不进间距）。 */
static void XGridLayout_computePositions(const int* sizes, const XGridGeom* g,
                                         int n, int spacing, int start,
                                         int* pos)
{
    int i;
    int p = start;
    bool prevVisible = false;
    for (i = 0; i < n; ++i) {
        if (g[i].empty) {
            pos[i] = p;
            continue;
        }
        if (prevVisible) p += spacing;
        pos[i] = p;
        p += sizes[i];
        prevVisible = true;
    }
}

/** @brief 把行列位置/尺寸写入持久缓存（cellRect 查询使用）。 */
static void XGridLayout_updateCache(XGridLayout* self,
                                    const int* colPos, const int* colSize, int cols,
                                    const int* rowPos, const int* rowSize, int rows)
{
    if (!self) return;
    if (!XGridLayout_growArray((void**)&self->m_cacheColPos,
                               &self->m_cacheColPosCount, cols, sizeof(int)))
        return;
    if (!XGridLayout_growArray((void**)&self->m_cacheColSize,
                               &self->m_cacheColSizeCount, cols, sizeof(int)))
        return;
    if (!XGridLayout_growArray((void**)&self->m_cacheRowPos,
                               &self->m_cacheRowPosCount, rows, sizeof(int)))
        return;
    if (!XGridLayout_growArray((void**)&self->m_cacheRowSize,
                               &self->m_cacheRowSizeCount, rows, sizeof(int)))
        return;
    if (cols > 0) {
        memcpy(self->m_cacheColPos, colPos, (size_t)cols * sizeof(int));
        memcpy(self->m_cacheColSize, colSize, (size_t)cols * sizeof(int));
    }
    if (rows > 0) {
        memcpy(self->m_cacheRowPos, rowPos, (size_t)rows * sizeof(int));
        memcpy(self->m_cacheRowSize, rowSize, (size_t)rows * sizeof(int));
    }
}

/* ==================== 虚函数表与生命周期 ==================== */

/** @brief 网格布局首选/最小尺寸计算的前置声明（定义见下方实现段）。 */
static XSize XGridLayout_computeHint(const XGridLayout* self, bool minMode);



/** @brief 网格布局首选尺寸。 */
static XSize VXGridLayout_sizeHint(const XLayoutItem* item)
{
    const XGridLayout* self = (const XGridLayout*)item;
    XSize out;
    XSize_init(&out, 0, 0);
    if (!self) return out;
    out = XGridLayout_computeHint(self, false);
    return out;
}

/** @brief 网格布局最小尺寸。 */
static XSize VXGridLayout_minimumSize(const XLayoutItem* item)
{
    const XGridLayout* self = (const XGridLayout*)item;
    XSize out;
    XSize_init(&out, 0, 0);
    if (!self) return out;
    out = XGridLayout_computeHint(self, true);
    return out;
}

/** @brief 网格布局最大尺寸。 */
static XSize VXGridLayout_maximumSize(const XLayoutItem* item)
{
    const XGridLayout* self = (const XGridLayout*)item;
    XSize out;
    XSize_init(&out, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    if (!self) return out;
    /* 有子条目时按行列最大尺寸求和（对齐掩码放开对应轴）。 */
    if (self->m_base.m_itemCount > 0) {
        int cols = self->m_columnCount;
        int rows = self->m_rowCount;
        XGridGeom* cg = (XGridGeom*)XMalloc_Hybrid(
            (size_t)(cols ? cols : 1) * sizeof(XGridGeom));
        XGridGeom* rg = (XGridGeom*)XMalloc_Hybrid(
            (size_t)(rows ? rows : 1) * sizeof(XGridGeom));
        int hs = XLayout_effectiveSpacing((const XLayout*)self);
        int vs = hs;
        if (self->m_hSpacing >= 0) hs = self->m_hSpacing;
        if (self->m_vSpacing >= 0) vs = self->m_vSpacing;
        if (cg && rg) {
            int j;
            int sum = 0;
            int active = 0;
            long long acc;
            XGridLayout_collectLines(self, true, cg, cols);
            XGridLayout_collectLines(self, false, rg, rows);
            acc = 0;
            active = 0;
            for (j = 0; j < cols; ++j) {
                if (cg[j].empty) continue;
                active++;
                if (cg[j].max >= XWIDGET_MAX_SIZE) { acc = XWIDGET_MAX_SIZE; break; }
                acc += cg[j].max;
            }
            if (acc < XWIDGET_MAX_SIZE && active > 1)
                acc += (long long)hs * (active - 1);
            sum = (int)(acc < XWIDGET_MAX_SIZE ? acc : XWIDGET_MAX_SIZE);
            if (sum < 0) sum = 0;
            out.width = sum;
            acc = 0;
            active = 0;
            for (j = 0; j < rows; ++j) {
                if (rg[j].empty) continue;
                active++;
                if (rg[j].max >= XWIDGET_MAX_SIZE) { acc = XWIDGET_MAX_SIZE; break; }
                acc += rg[j].max;
            }
            if (acc < XWIDGET_MAX_SIZE && active > 1)
                acc += (long long)vs * (active - 1);
            sum = (int)(acc < XWIDGET_MAX_SIZE ? acc : XWIDGET_MAX_SIZE);
            if (sum < 0) sum = 0;
            out.height = sum;
        }
        XFree_Hybrid(cg);
        XFree_Hybrid(rg);
    }
    return out;
}

/** @brief 网格布局伸展方向：任一列可伸展→水平，任一行可伸展→垂直。 */
static XLayoutExpandingDirections VXGridLayout_expandingDirections(
    const XLayoutItem* item)
{
    const XGridLayout* self = (const XGridLayout*)item;
    XLayoutExpandingDirections out = XLayoutExpandingDirection_None;
    XGridGeom* cg = NULL;
    XGridGeom* rg = NULL;
    int j;
    if (!self) return out;
    cg = (XGridGeom*)XMalloc_Hybrid((size_t)(self->m_columnCount ? self->m_columnCount : 1)
                                    * sizeof(XGridGeom));
    rg = (XGridGeom*)XMalloc_Hybrid((size_t)(self->m_rowCount ? self->m_rowCount : 1)
                                    * sizeof(XGridGeom));
    if (cg)
        XGridLayout_collectLines(self, true, cg, self->m_columnCount);
    if (rg)
        XGridLayout_collectLines(self, false, rg, self->m_rowCount);
    if (cg)
        for (j = 0; j < self->m_columnCount; ++j)
            if (cg[j].expanding) { out |= XLayoutExpandingDirection_Horizontal; break; }
    if (rg)
        for (j = 0; j < self->m_rowCount; ++j)
            if (rg[j].expanding) { out |= XLayoutExpandingDirection_Vertical; break; }
    XFree_Hybrid(cg);
    XFree_Hybrid(rg);
    return out;
}

/** @brief 网格布局空判定：没有任何条目即为空。 */
static bool VXGridLayout_isEmpty(const XLayoutItem* item)
{
    if (!item) return true;
    return ((const XLayout*)item)->m_itemCount == 0;
}

/** @brief 查询网格布局是否支持 heightForWidth。 */
static bool VXGridLayout_hasHeightForWidth(const XLayoutItem* item)
{
    const XLayout* self = (const XLayout*)item;
    int i;
    if (!self) return false;
    for (i = 0; i < (int)self->m_itemCount; ++i) {
        if (self->m_items[i] &&
            XLayoutItem_hasHeightForWidth_base(self->m_items[i]))
            return true;
    }
    return false;
}

/** @brief 列数记法便捷定义：把网格列宽分配与行高分配统一封装。 */
static void XGridLayout_prepareColumns(const XGridLayout* self, int width,
                                       int hSpacing, int* colSizes,
                                       int* colPos, int cols)
{
    XGridGeom* cg;
    int j;
    if (!self || !colSizes || cols <= 0) return;
    cg = (XGridGeom*)XMalloc_Hybrid((size_t)cols * sizeof(XGridGeom));
    if (!cg) {
        for (j = 0; j < cols; ++j) { colSizes[j] = 0; if (colPos) colPos[j] = 0; }
        return;
    }
    XGridLayout_collectLines(self, true, cg, cols);
    XGridLayout_allocate(cg, cols, hSpacing, width, colSizes);
    if (colPos)
        XGridLayout_computePositions(colSizes, cg, cols, hSpacing, 0, colPos);
    XFree_Hybrid(cg);
}

/* ==================== 几何分配（核心） ==================== */

/** @brief 网格布局几何分配。 */
static void VXGridLayout_setGeometry(XLayout* self, const XRect* rect)
{
    XGridLayout* grid = (XGridLayout*)self;
    XRect cr;
    XRect inner;
    int cols;
    int rows;
    int hSpacing;
    int vSpacing;
    XGridGeom* colGeom = NULL;
    XGridGeom* rowGeom = NULL;
    int* colSizes = NULL;
    int* rowSizes = NULL;
    int* colPos = NULL;
    int* rowPos = NULL;
    int i;
    bool hReversed;
    bool vReversed;
    if (!self || !rect) return;
    ((XLayoutItem*)self)->m_geometry = *rect;
    cr = XLayoutItem_alignment((XLayoutItem*)self)
             ? XLayout_alignmentRect((const XLayout*)self, rect)
             : *rect;
    inner = XLayout_contentsRectForRect((const XLayout*)self, &cr);
    cols = grid->m_columnCount;
    rows = grid->m_rowCount;
    hSpacing = (grid->m_hSpacing >= 0) ? grid->m_hSpacing
                                       : XLayout_effectiveSpacing(self);
    vSpacing = (grid->m_vSpacing >= 0) ? grid->m_vSpacing
                                       : XLayout_effectiveSpacing(self);
    if (cols > 0) {
        colGeom = (XGridGeom*)XMalloc_Hybrid((size_t)cols * sizeof(XGridGeom));
        colSizes = (int*)XMalloc_Hybrid((size_t)cols * sizeof(int));
        colPos = (int*)XMalloc_Hybrid((size_t)cols * sizeof(int));
    }
    if (rows > 0) {
        rowGeom = (XGridGeom*)XMalloc_Hybrid((size_t)rows * sizeof(XGridGeom));
        rowSizes = (int*)XMalloc_Hybrid((size_t)rows * sizeof(int));
        rowPos = (int*)XMalloc_Hybrid((size_t)rows * sizeof(int));
    }
    if ((cols > 0 && (!colGeom || !colSizes || !colPos)) ||
        (rows > 0 && (!rowGeom || !rowSizes || !rowPos))) {
        XFree_Hybrid(colGeom);
        XFree_Hybrid(colSizes);
        XFree_Hybrid(colPos);
        XFree_Hybrid(rowGeom);
        XFree_Hybrid(rowSizes);
        XFree_Hybrid(rowPos);
        return;
    }
    if (cols > 0) {
        XGridLayout_collectLines(grid, true, colGeom, cols);
        XGridLayout_allocate(colGeom, cols, hSpacing, inner.width, colSizes);
        XGridLayout_computePositions(colSizes, colGeom, cols, hSpacing,
                                     inner.x, colPos);
    }
    if (rows > 0) {
        XGridLayout_collectLines(grid, false, rowGeom, rows);
        XGridLayout_allocate(rowGeom, rows, vSpacing, inner.height, rowSizes);
        XGridLayout_computePositions(rowSizes, rowGeom, rows, vSpacing,
                                     inner.y, rowPos);
    }
    XGridLayout_updateCache(grid, colPos, colSizes, cols, rowPos, rowSizes, rows);
    hReversed = (grid->m_originCorner == XGridLayoutOriginCorner_TopRight ||
                 grid->m_originCorner == XGridLayoutOriginCorner_BottomRight);
    vReversed = (grid->m_originCorner == XGridLayoutOriginCorner_BottomLeft ||
                 grid->m_originCorner == XGridLayoutOriginCorner_BottomRight);
    for (i = 0; i < (int)self->m_itemCount; ++i) {
        XLayoutItem* item = self->m_items[i];
        const XGridLayoutCell* cell = grid->m_cells ? &grid->m_cells[i] : NULL;
        XRect r;
        int x;
        int y;
        int w;
        int h;
        if (!item || !cell) continue;
        x = colPos[cell->m_column];
        y = rowPos[cell->m_row];
        w = colPos[cell->m_columnSpan] + colSizes[cell->m_columnSpan] - x;
        h = rowPos[cell->m_rowSpan] + rowSizes[cell->m_rowSpan] - y;
        if (hReversed)
            x = inner.x + inner.width - (x - inner.x) - w;
        if (vReversed)
            y = inner.y + inner.height - (y - inner.y) - h;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        XRect_init(&r, x, y, w, h);
        XLayoutItem_setGeometry_base(item, &r);
    }
    XFree_Hybrid(colGeom);
    XFree_Hybrid(colSizes);
    XFree_Hybrid(colPos);
    XFree_Hybrid(rowGeom);
    XFree_Hybrid(rowSizes);
    XFree_Hybrid(rowPos);
    self->m_isDirty = 0;
    self->m_activated = 1;
}

/** @brief 网格布局首选/最小尺寸计算（minMode=true 取最小）。 */
static XSize XGridLayout_computeHint(const XGridLayout* self, bool minMode)
{
    XSize out;
    XSize_init(&out, 0, 0);
    if (!self) return out;
    {
        int cols = self->m_columnCount;
        int rows = self->m_rowCount;
        XGridGeom* cg = NULL;
        XGridGeom* rg = NULL;
        int hSpacing = (self->m_hSpacing >= 0) ? self->m_hSpacing
                                               : XLayout_effectiveSpacing((XLayout*)self);
        int vSpacing = (self->m_vSpacing >= 0) ? self->m_vSpacing
                                               : XLayout_effectiveSpacing((XLayout*)self);
        long long acc;
        int j;
        int active;
        if (cols > 0)
            cg = (XGridGeom*)XMalloc_Hybrid((size_t)cols * sizeof(XGridGeom));
        if (rows > 0)
            rg = (XGridGeom*)XMalloc_Hybrid((size_t)rows * sizeof(XGridGeom));
        if ((cols > 0 && !cg) || (rows > 0 && !rg)) {
            XFree_Hybrid(cg);
            XFree_Hybrid(rg);
            return out;
        }
        if (cg) XGridLayout_collectLines(self, true, cg, cols);
        if (rg) XGridLayout_collectLines(self, false, rg, rows);
        acc = 0;
        active = 0;
        for (j = 0; j < cols; ++j) {
            if (cg[j].empty) continue;
            active++;
            acc += minMode ? cg[j].min : cg[j].hint;
        }
        if (active > 1) acc += (long long)hSpacing * (active - 1);
        if (acc < 0) acc = 0;
        if (acc > XWIDGET_MAX_SIZE) acc = XWIDGET_MAX_SIZE;
        out.width = (int)acc;
        acc = 0;
        active = 0;
        for (j = 0; j < rows; ++j) {
            if (rg[j].empty) continue;
            active++;
            acc += minMode ? rg[j].min : rg[j].hint;
        }
        if (active > 1) acc += (long long)vSpacing * (active - 1);
        if (acc < 0) acc = 0;
        if (acc > XWIDGET_MAX_SIZE) acc = XWIDGET_MAX_SIZE;
        out.height = (int)acc;
        XFree_Hybrid(cg);
        XFree_Hybrid(rg);
    }
    {
        /* 内容边距解析：未设置（-1）按 0 处理，与 Qt 无样式默认一致。 */
        XMargins m = XLayout_contentsMargins((const XLayout*)self);
        out.width += m.left + m.right;
        out.height += m.top + m.bottom;
    }
    return out;
}

/* ==================== heightForWidth 体系 ==================== */

/** @brief 网格 heightForWidth：先按列分配，再以 hfw 高度回填行高并求和。 */
static int XGridLayout_doHeightForWidth(const XLayout* self, int width,
                                        bool minMode)
{
    const XGridLayout* grid = (const XGridLayout*)self;
    int cols;
    int rows;
    int hSpacing;
    int vSpacing;
    int inner;
    int* colSizes = NULL;
    int* colPos = NULL;
    XGridGeom* rowGeom = NULL;
    int* rowSizes = NULL;
    int i;
    int j;
    long long acc;
    int active;
    int result;
    if (!grid) return -1;
    if (!VXGridLayout_hasHeightForWidth((const XLayoutItem*)self)) return -1;
    cols = grid->m_columnCount;
    rows = grid->m_rowCount;
    hSpacing = (grid->m_hSpacing >= 0) ? grid->m_hSpacing
                                       : XLayout_effectiveSpacing(self);
    vSpacing = (grid->m_vSpacing >= 0) ? grid->m_vSpacing
                                       : XLayout_effectiveSpacing(self);
    {
        XMargins margin = XLayout_contentsMargins(self);
        inner = width - margin.left - margin.right;
    }
    if (inner < 0) inner = 0;
    if (cols > 0) {
        colSizes = (int*)XMalloc_Hybrid((size_t)cols * sizeof(int));
        colPos = (int*)XMalloc_Hybrid((size_t)cols * sizeof(int));
    }
    if (rows > 0)
        rowGeom = (XGridGeom*)XMalloc_Hybrid((size_t)rows * sizeof(XGridGeom));
    if ((cols > 0 && (!colSizes || !colPos)) || (rows > 0 && !rowGeom)) {
        XFree_Hybrid(colSizes);
        XFree_Hybrid(colPos);
        XFree_Hybrid(rowGeom);
        return -1;
    }
    if (cols > 0)
        XGridLayout_prepareColumns(grid, inner, hSpacing, colSizes, colPos, cols);
    if (rows > 0) {
        for (j = 0; j < rows; ++j) {
            int minSize = XGridLayout_rowMinHeightConst(grid, j);
            int stretch = XGridLayout_rowStretchConst(grid, j);
            if (minSize < 0) minSize = 0;
            rowGeom[j].min = minSize;
            rowGeom[j].hint = minSize;
            rowGeom[j].max = (stretch > 0 || minSize <= 0)
                                 ? XWIDGET_MAX_SIZE : minSize;
            rowGeom[j].stretch = (stretch > 0) ? stretch : 0;
            rowGeom[j].expanding = (stretch > 0);
            rowGeom[j].empty = true;
        }
        for (i = 0; i < (int)self->m_itemCount; ++i) {
            const XLayoutItem* item = self->m_items[i];
            const XGridLayoutCell* cell = grid->m_cells ? &grid->m_cells[i] : NULL;
            int w;
            int h;
            int start;
            int end;
            if (!item || !cell) continue;
            if (XLayoutItem_isEmpty_base(item)) continue;
            w = colPos[cell->m_columnSpan] + colSizes[cell->m_columnSpan] -
                colPos[cell->m_column];
            if (w < 0) w = 0;
            if (minMode) {
                if (XLayoutItem_hasHeightForWidth_base(item))
                    h = XLayoutItem_minimumHeightForWidth_base(item, w);
                else
                    h = XLayoutItem_minimumSize_base(item).height;
            } else {
                if (XLayoutItem_hasHeightForWidth_base(item))
                    h = XLayoutItem_heightForWidth_base(item, w);
                else
                    h = XLayoutItem_sizeHint_base(item).height;
            }
            if (h < 0) h = 0;
            start = cell->m_row;
            end = cell->m_rowSpan;
            if (start < 0 || start >= rows) continue;
            if (end >= rows) end = rows - 1;
            if (start == end) {
                if (minMode) {
                    if (rowGeom[start].min < h) rowGeom[start].min = h;
                } else {
                    if (rowGeom[start].hint < h) rowGeom[start].hint = h;
                }
                rowGeom[start].empty = false;
            } else {
                XGridLayout_distributeSpan(rowGeom, start, end,
                                           minMode ? h : 0,
                                           minMode ? 0 : h,
                                           XGridLayout_itemStretch(item, false),
                                           vSpacing);
                for (j = start; j <= end; ++j) rowGeom[j].empty = false;
            }
        }
        {
            long long lineTotal = 0;
            rowSizes = (int*)XMalloc_Hybrid((size_t)rows * sizeof(int));
            if (!rowSizes) {
                XFree_Hybrid(colSizes);
                XFree_Hybrid(colPos);
                XFree_Hybrid(rowGeom);
                return -1;
            }
            /* heightForWidth 只有宽度输入：行高按“首选所需高度”协商，
             * 传入 lineTotal 使分配走 growExtra(extra=0) 分支，
             * 结果即为各行首选高度（对标 Qt QGridLayout::heightForWidth）。 */
            for (j = 0; j < rows; ++j) {
                if (rowGeom[j].empty) continue;
                lineTotal += minMode ? (long long)rowGeom[j].min
                                     : (long long)rowGeom[j].hint;
            }
            XGridLayout_allocate(rowGeom, rows, vSpacing, (int)lineTotal,
                                 rowSizes);
        }
    } else {
        rowSizes = NULL;
    }
    acc = 0;
    active = 0;
    for (j = 0; j < rows; ++j) {
        if (rowGeom[j].empty) continue;
        active++;
        acc += rowSizes[j];
    }
    if (active > 1) acc += (long long)vSpacing * (active - 1);
    result = (int)(acc > XWIDGET_MAX_SIZE ? XWIDGET_MAX_SIZE : acc);
    if (result < 0) result = 0;
    {
        XMargins margin = XLayout_contentsMargins(self);
        result += margin.top + margin.bottom;
    }
    XFree_Hybrid(colSizes);
    XFree_Hybrid(colPos);
    XFree_Hybrid(rowGeom);
    XFree_Hybrid(rowSizes);
    return result;
}

/** @brief 网格布局首选高度（给定宽度）。 */
static int VXGridLayout_heightForWidth(const XLayoutItem* item, int width)
{
    if (!item) return -1;
    return XGridLayout_doHeightForWidth((const XLayout*)item, width, false);
}

/** @brief 网格布局最小高度（给定宽度）。 */
static int VXGridLayout_minimumHeightForWidth(const XLayoutItem* item, int width)
{
    if (!item) return -1;
    return XGridLayout_doHeightForWidth((const XLayout*)item, width, true);
}

/** @brief 按默认定位游标追加条目（对标 QGridLayout::addItem）。 */
static void VXGridLayout_addItem(XLayout* self, XLayoutItem* item)
{
    XGridLayout* grid;
    int idx;
    if (!self || !item) return;
    grid = (XGridLayout*)self;
    idx = XGridLayout_appendCellItem(grid, item, false,
                                     grid->m_nextR, grid->m_nextC, 1, 1, 0);
    if (idx >= 0)
        XGridLayout_setNextPosAfter(grid, grid->m_cells[idx].m_rowSpan,
                                    grid->m_cells[idx].m_columnSpan);
}

/** @brief 原位替换指定索引条目：保留该位置单元格（对标 Qt 6.8
 *  QGridLayoutPrivate::replaceAt）。单元格数组与条目数组平行，索引
 *  不变即单元格自动保留；伸展因子/行列最小尺寸等行/列属性天然按
 *  位置保留，无需额外搬运。 */
static XLayoutItem* VXGridLayout_replaceItemAt(XLayout* self, int index,
                                               XLayoutItem* item)
{
    XLayoutItem* old;
    if (!self || !item) return NULL;
    if (index < 0 || index >= (int)self->m_itemCount) return NULL;
    old = self->m_items[index];
    self->m_items[index] = item;
    if (old) {
        old->m_ownedByLayout = 0;
        if (XLayoutItem_layout_base(old))
            ((XLayout*)old)->m_parentLayout = NULL;
    }
    item->m_ownedByLayout = 1;
    XLayout_linkItem(self, item);
    self->m_isDirty = 1;
    return old;
}

/** @brief 取出条目：先同步单元格数组，再调用父类实现。 */
static XLayoutItem* VXGridLayout_takeAt(XLayout* self, int index)
{
    XLayoutItem* item;
    XGridLayout* grid;
    if (!self || index < 0 || index >= (int)self->m_itemCount) return NULL;
    grid = (XGridLayout*)self;
    if (grid->m_cells && index < grid->m_cellCapacity) {
        memmove(&grid->m_cells[index], &grid->m_cells[index + 1],
                (size_t)(self->m_itemCount - index - 1) *
                    sizeof(XGridLayoutCell));
        if (grid->m_cellCapacity > 0)
            memset(&grid->m_cells[self->m_itemCount - 1], 0,
                   sizeof(XGridLayoutCell));
    }
    item = XClass_Parent(XLayout, EXLayout_TakeAt,
                         XLayoutItem*(*)(XLayout*, int))(self, index);
    /* QGridLayout keeps its row/column dimensions after takeAt(). */
    return item;
}

/** @brief 释放网格布局资源。 */
static void VXGridLayout_deinit(XGridLayout* self)
{
    if (!self) return;
    XGridLayout_freeArrays(self);
    XClass_Deinit_Parent(XLayout, (XLayout*)self);
}

/** @brief 拷贝网格布局配置（不复制条目树；行列属性与间距复制）。 */
static void VXGridLayout_copy(XGridLayout* self, const XGridLayout* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XGridLayout_init(self);
    XClass_Parent(XLayout, EXClass_Copy,
                  void(*)(XLayout*, const XLayout*))((XLayout*)self,
                                                     (const XLayout*)other);
    self->m_hSpacing = other->m_hSpacing;
    self->m_vSpacing = other->m_vSpacing;
    self->m_originCorner = other->m_originCorner;
    XGridLayout_freeArrays(self);
    if (other->m_rowStretchCapacity > 0 || other->m_columnStretchCapacity > 0 ||
        other->m_rowMinHeightCapacity > 0 ||
        other->m_columnMinWidthCapacity > 0) {
        if (XGridLayout_growArray((void**)&self->m_rowStretch,
                                  &self->m_rowStretchCapacity,
                                  other->m_rowStretchCapacity, sizeof(int)))
            memcpy(self->m_rowStretch, other->m_rowStretch,
                   (size_t)other->m_rowStretchCapacity * sizeof(int));
        if (XGridLayout_growArray((void**)&self->m_columnStretch,
                                  &self->m_columnStretchCapacity,
                                  other->m_columnStretchCapacity, sizeof(int)))
            memcpy(self->m_columnStretch, other->m_columnStretch,
                   (size_t)other->m_columnStretchCapacity * sizeof(int));
        if (XGridLayout_growArray((void**)&self->m_rowMinHeight,
                                  &self->m_rowMinHeightCapacity,
                                  other->m_rowMinHeightCapacity, sizeof(int)))
            memcpy(self->m_rowMinHeight, other->m_rowMinHeight,
                   (size_t)other->m_rowMinHeightCapacity * sizeof(int));
        if (XGridLayout_growArray((void**)&self->m_columnMinWidth,
                                  &self->m_columnMinWidthCapacity,
                                  other->m_columnMinWidthCapacity, sizeof(int)))
            memcpy(self->m_columnMinWidth, other->m_columnMinWidth,
                   (size_t)other->m_columnMinWidthCapacity * sizeof(int));
    }
    self->m_nextR = other->m_nextR;
    self->m_nextC = other->m_nextC;
    self->m_addVertical = other->m_addVertical;
    self->m_rowCount = 0;
    self->m_columnCount = 0;
    /* 条目树不复制，行/列数依据条目重算（拷贝后为 0）。 */
}

/** @brief 移动网格布局：转移条目、单元格与全部属性数组所有权。 */
static void VXGridLayout_move(XGridLayout* self, XGridLayout* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XGridLayout_init(self);
    XClass_Parent(XLayout, EXClass_Move,
                  void(*)(XLayout*, XLayout*))((XLayout*)self, (XLayout*)other);
    XGridLayout_freeArrays(self);
    self->m_cells = other->m_cells;
    self->m_cellCapacity = other->m_cellCapacity;
    self->m_rowStretch = other->m_rowStretch;
    self->m_rowStretchCapacity = other->m_rowStretchCapacity;
    self->m_columnStretch = other->m_columnStretch;
    self->m_columnStretchCapacity = other->m_columnStretchCapacity;
    self->m_rowMinHeight = other->m_rowMinHeight;
    self->m_rowMinHeightCapacity = other->m_rowMinHeightCapacity;
    self->m_columnMinWidth = other->m_columnMinWidth;
    self->m_columnMinWidthCapacity = other->m_columnMinWidthCapacity;
    self->m_cacheColPos = other->m_cacheColPos;
    self->m_cacheColSize = other->m_cacheColSize;
    self->m_cacheRowPos = other->m_cacheRowPos;
    self->m_cacheRowSize = other->m_cacheRowSize;
    self->m_cacheColPosCount = other->m_cacheColPosCount;
    self->m_cacheColSizeCount = other->m_cacheColSizeCount;
    self->m_cacheRowPosCount = other->m_cacheRowPosCount;
    self->m_cacheRowSizeCount = other->m_cacheRowSizeCount;
    self->m_hSpacing = other->m_hSpacing;
    self->m_vSpacing = other->m_vSpacing;
    self->m_originCorner = other->m_originCorner;
    self->m_nextR = other->m_nextR;
    self->m_nextC = other->m_nextC;
    self->m_addVertical = other->m_addVertical;
    self->m_rowCount = other->m_rowCount;
    self->m_columnCount = other->m_columnCount;
    other->m_nextR = 0;
    other->m_nextC = 0;
    other->m_addVertical = 0;
    other->m_cells = NULL;
    other->m_rowStretch = NULL;
    other->m_columnStretch = NULL;
    other->m_rowMinHeight = NULL;
    other->m_columnMinWidth = NULL;
    other->m_cacheColPos = NULL;
    other->m_cacheColSize = NULL;
    other->m_cacheRowPos = NULL;
    other->m_cacheRowSize = NULL;
    other->m_cellCapacity = 0;
    other->m_rowStretchCapacity = 0;
    other->m_columnStretchCapacity = 0;
    other->m_rowMinHeightCapacity = 0;
    other->m_columnMinWidthCapacity = 0;
    other->m_cacheColPosCount = 0;
    other->m_cacheColSizeCount = 0;
    other->m_cacheRowPosCount = 0;
    other->m_cacheRowSizeCount = 0;
    other->m_rowCount = 0;
    other->m_columnCount = 0;
}

XVtable* XGridLayout_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGridLayout)
    XVTABLE_INHERIT_XCLASS(XLayout);
    XVTABLE_OVERLOAD_DEFAULT(EXLayout_AddItem, VXGridLayout_addItem);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SizeHint, VXGridLayout_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumSize, VXGridLayout_minimumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MaximumSize, VXGridLayout_maximumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_ExpandingDirections,
                             VXGridLayout_expandingDirections);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_IsEmpty, VXGridLayout_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SetGeometry, VXGridLayout_setGeometry);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HasHeightForWidth,
                             VXGridLayout_hasHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HeightForWidth,
                             VXGridLayout_heightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumHeightForWidth,
                             VXGridLayout_minimumHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayout_TakeAt, VXGridLayout_takeAt);
    XVTABLE_OVERLOAD_DEFAULT(EXLayout_ReplaceItemAt,
                             VXGridLayout_replaceItemAt);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGridLayout_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXGridLayout_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXGridLayout_move);
    return XVTABLE_DEFAULT;
}

void XGridLayout_init(XGridLayout* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XGridLayout));
    XLayout_init((XLayout*)self);
    XClassSetVtable(self, XGridLayout);
    self->m_hSpacing = -1;
    self->m_vSpacing = -1;
    self->m_originCorner = XGridLayoutOriginCorner_TopLeft;
    /* Match QGridLayout's constructor: an empty layout starts as 1x1. */
    XGridLayout_expand(self, 1, 1);
}

XGridLayout* XGridLayout_create(XWidget* parent)
{
    XGridLayout* self = (XGridLayout*)XMalloc_System(sizeof(XGridLayout));
    if (!self) return NULL;
    memset(self, 0, sizeof(XGridLayout));
    XGridLayout_init(self);
    Set_Class_IsHeap(self, true);
    if (parent)
        XWidget_setLayout(parent, (XLayout*)self);
    return self;
}

/* ==================== 间距（对标 QGridLayout） ==================== */

void XGridLayout_setHorizontalSpacing(XGridLayout* self, int spacing)
{
    if (!self) return;
    if (self->m_hSpacing == spacing) return;
    self->m_hSpacing = spacing;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

int XGridLayout_horizontalSpacing(const XGridLayout* self)
{
    if (!self) return -1;
    return self->m_hSpacing;
}

void XGridLayout_setVerticalSpacing(XGridLayout* self, int spacing)
{
    if (!self) return;
    if (self->m_vSpacing == spacing) return;
    self->m_vSpacing = spacing;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

int XGridLayout_verticalSpacing(const XGridLayout* self)
{
    if (!self) return -1;
    return self->m_vSpacing;
}

void XGridLayout_setSpacing(XGridLayout* self, int spacing)
{
    if (!self) return;
    if (self->m_hSpacing == spacing && self->m_vSpacing == spacing) return;
    self->m_hSpacing = spacing;
    self->m_vSpacing = spacing;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

int XGridLayout_spacing(const XGridLayout* self)
{
    int h;
    if (!self) return -1;
    h = XGridLayout_horizontalSpacing(self);
    if (h == XGridLayout_verticalSpacing(self))
        return h;
    return -1;
}

/* ==================== 行列伸缩（对标 QGridLayout） ==================== */

void XGridLayout_setRowStretch(XGridLayout* self, int row, int stretch)
{
    if (!self || row < 0 || stretch < 0) return;
    if (!XGridLayout_ensureLineArrays(self, row + 1, self->m_columnCount))
        return;
    if (self->m_rowStretch[row] == stretch) return;
    self->m_rowStretch[row] = stretch;
    if (self->m_rowCount < row + 1) self->m_rowCount = row + 1;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

int XGridLayout_rowStretch(const XGridLayout* self, int row)
{
    if (!self) return 0;
    return XGridLayout_rowStretchConst(self, row);
}

void XGridLayout_setColumnStretch(XGridLayout* self, int column, int stretch)
{
    if (!self || column < 0 || stretch < 0) return;
    if (!XGridLayout_ensureLineArrays(self, self->m_rowCount, column + 1))
        return;
    if (self->m_columnStretch[column] == stretch) return;
    self->m_columnStretch[column] = stretch;
    if (self->m_columnCount < column + 1) self->m_columnCount = column + 1;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

int XGridLayout_columnStretch(const XGridLayout* self, int column)
{
    if (!self) return 0;
    return XGridLayout_columnStretchConst(self, column);
}

/* ==================== 行列最小尺寸（对标 QGridLayout） ==================== */

void XGridLayout_setRowMinimumHeight(XGridLayout* self, int row, int minSize)
{
    if (!self || row < 0 || minSize < 0) return;
    if (!XGridLayout_ensureLineArrays(self, row + 1, self->m_columnCount))
        return;
    if (self->m_rowMinHeight[row] == minSize) return;
    self->m_rowMinHeight[row] = minSize;
    if (self->m_rowCount < row + 1) self->m_rowCount = row + 1;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

int XGridLayout_rowMinimumHeight(const XGridLayout* self, int row)
{
    if (!self) return 0;
    return XGridLayout_rowMinHeightConst(self, row);
}

void XGridLayout_setColumnMinimumWidth(XGridLayout* self, int column, int minSize)
{
    if (!self || column < 0 || minSize < 0) return;
    if (!XGridLayout_ensureLineArrays(self, self->m_rowCount, column + 1))
        return;
    if (self->m_columnMinWidth[column] == minSize) return;
    self->m_columnMinWidth[column] = minSize;
    if (self->m_columnCount < column + 1) self->m_columnCount = column + 1;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

int XGridLayout_columnMinimumWidth(const XGridLayout* self, int column)
{
    if (!self) return 0;
    return XGridLayout_columnMinWidthConst(self, column);
}

/* ==================== 行列数 ==================== */

int XGridLayout_columnCount(const XGridLayout* self)
{
    if (!self) return 0;
    return self->m_columnCount;
}

int XGridLayout_rowCount(const XGridLayout* self)
{
    if (!self) return 0;
    return self->m_rowCount;
}

/* ==================== 添加条目（对标 QGridLayout add* 系列） ==================== */

void XGridLayout_addWidgetAuto(XGridLayout* self, XWidget* widget)
{
    XLayoutItem* item;
    int idx;
    if (!self || !widget) return;
    item = XLayoutItem_createWidgetItem(widget);
    if (!item) return;
    idx = XGridLayout_appendCellItem(self, item, true,
                                     self->m_nextR, self->m_nextC, 1, 1, 0);
    if (idx < 0) {
        XLayoutItem_delete_base(item);
        return;
    }
    XGridLayout_setNextPosAfter(self, self->m_cells[idx].m_rowSpan,
                                self->m_cells[idx].m_columnSpan);
}

void XGridLayout_addWidget(XGridLayout* self, XWidget* widget,
                           int row, int column, XLayoutAlignments alignment)
{
    XGridLayout_addWidgetSpan(self, widget, row, column, 1, 1, alignment);
}

void XGridLayout_addWidgetSpan(XGridLayout* self, XWidget* widget,
                               int row, int column, int rowSpan, int columnSpan,
                               XLayoutAlignments alignment)
{
    XLayoutItem* item;
    int idx;
    if (!self || !widget) return;
    item = XLayoutItem_createWidgetItem(widget);
    if (!item) return;
    idx = XGridLayout_appendCellItem(self, item, true, row, column,
                                     rowSpan, columnSpan, alignment);
    if (idx < 0) {
        XLayoutItem_delete_base(item);
        return;
    }
    XGridLayout_setNextPosAfter(self, self->m_cells[idx].m_rowSpan,
                                self->m_cells[idx].m_columnSpan);
}

void XGridLayout_addLayout(XGridLayout* self, XLayout* child,
                           int row, int column, XLayoutAlignments alignment)
{
    XGridLayout_addLayoutSpan(self, child, row, column, 1, 1, alignment);
}

void XGridLayout_addLayoutSpan(XGridLayout* self, XLayout* child,
                               int row, int column, int rowSpan, int columnSpan,
                               XLayoutAlignments alignment)
{
    int idx;
    if (!self || !child) return;
    idx = XGridLayout_appendCellItem(self, (XLayoutItem*)child, false, row, column,
                                     rowSpan, columnSpan, alignment);
    if (idx < 0) return;   /* 借用子布局：失败时责任不变。 */
    XGridLayout_setNextPosAfter(self, self->m_cells[idx].m_rowSpan,
                                self->m_cells[idx].m_columnSpan);
}

void XGridLayout_addItem(XGridLayout* self, XLayoutItem* item)
{
    if (!self || !item) return;
    VXGridLayout_addItem((XLayout*)self, item);
}

void XGridLayout_addItemAt(XGridLayout* self, XLayoutItem* item,
                           int row, int column, int rowSpan, int columnSpan,
                           XLayoutAlignments alignment)
{
    int idx;
    if (!self || !item) return;
    idx = XGridLayout_appendCellItem(self, item, false, row, column,
                                     rowSpan, columnSpan, alignment);
    if (idx < 0) return;
    XGridLayout_setNextPosAfter(self, self->m_cells[idx].m_rowSpan,
                                self->m_cells[idx].m_columnSpan);
}

/* ==================== 查询（对标 QGridLayout） ==================== */

XRect XGridLayout_cellRect(const XGridLayout* self, int row, int column)
{
    XRect out;
    XRect_init(&out, 0, 0, 0, 0);
    if (!self) return out;
    if (row < 0 || row >= self->m_cacheRowPosCount ||
        row >= self->m_cacheRowSizeCount || !self->m_cacheRowPos ||
        !self->m_cacheRowSize)
        return out;
    if (column < 0 || column >= self->m_cacheColPosCount ||
        column >= self->m_cacheColSizeCount || !self->m_cacheColPos ||
        !self->m_cacheColSize)
        return out;
    XRect_init(&out, self->m_cacheColPos[column], self->m_cacheRowPos[row],
               self->m_cacheColSize[column], self->m_cacheRowSize[row]);
    return out;
}

XLayoutItem* XGridLayout_itemAtPosition(const XGridLayout* self,
                                        int row, int column)
{
    int i;
    XLayoutItem* found = NULL;
    if (!self) return NULL;
    for (i = 0; i < (int)self->m_base.m_itemCount; ++i) {
        const XGridLayoutCell* cell = self->m_cells ? &self->m_cells[i] : NULL;
        if (!cell) continue;
        if (row >= cell->m_row && row <= cell->m_rowSpan &&
            column >= cell->m_column && column <= cell->m_columnSpan)
            found = self->m_base.m_items[i];
    }
    return found;
}

bool XGridLayout_getItemPosition(const XGridLayout* self, int index,
                                 int* row, int* column,
                                 int* rowSpan, int* columnSpan)
{
    const XGridLayoutCell* cell;
    if (!self || index < 0 || index >= (int)self->m_base.m_itemCount)
        return false;
    cell = self->m_cells ? &self->m_cells[index] : NULL;
    if (!cell) return false;
    if (row) *row = cell->m_row;
    if (column) *column = cell->m_column;
    if (rowSpan) *rowSpan = cell->m_rowSpan - cell->m_row + 1;
    if (columnSpan) *columnSpan = cell->m_columnSpan - cell->m_column + 1;
    return true;
}

/* ==================== 默认定位（对标 QGridLayout） ==================== */

void XGridLayout_setDefaultPositioning(XGridLayout* self, int n,
                                       XOrientation orient)
{
    if (!self) return;
    if (n < 1) n = 1;
    if (orient == XOrientation_Horizontal) {
        XGridLayout_expand(self, 1, n);
        self->m_addVertical = 0;
    } else {
        XGridLayout_expand(self, n, 1);
        self->m_addVertical = 1;
    }
}

/* ==================== 原点角 ==================== */

void XGridLayout_setOriginCorner(XGridLayout* self,
                                 XGridLayoutOriginCorner corner)
{
    if (!self) return;
    if ((int)self->m_originCorner == (int)corner) return;
    self->m_originCorner = corner;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

XGridLayoutOriginCorner XGridLayout_originCorner(const XGridLayout* self)
{
    if (!self) return XGridLayoutOriginCorner_TopLeft;
    return self->m_originCorner;
}

#endif /* XLAYOUT_ON && XLAYOUT_GRID_ON */
