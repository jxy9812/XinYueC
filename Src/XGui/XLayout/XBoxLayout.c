/******************************************************************************
 * @file       XBoxLayout.c
 * @brief      XBoxLayout 盒式布局实现（对标 Qt 6.8 QBoxLayout 算法）。
 * @details    本文件实现 XBoxLayout 的：
 *             - 类虚函数表（继承 XLayout 全部槽位，不新增虚函数）与
 *               生命周期（Deinit 释放伸展数组；Copy 复制配置；Move
 *               转移条目与伸展数组所有权）；
 *             - 条目管理：add/insert 系列（控件条目/子布局/固定
 *               空白/可伸展空白/任意条目）与伸展数组并行维护，并覆盖
 *               ReplaceItemAt 原位替换（保留伸展因子，对标
 *               QBoxLayoutPrivate::replaceAt）；
 *             - 尺寸协商：sizeHint/minimumSize/maximumSize/
 *               expandingDirections/hasHeightForWidth/heightForWidth/
 *               minimumHeightForWidth（主轴求和、交叉轴取最大、空条目
 *               尺寸为 0、结果附加内容边距，与 Qt setupGeom 一致）；
 *             - 几何分配：setGeometry 先按布局自身对齐收拢分配矩形，
 *               再去边距得到内部矩形，主轴用与 qGeomCalc 语义一致的三
 *               段分配（不足→按最小逐项给满、不压到最小以下；介于最
 *               小/首选→按差值比例收缩；有余→按 stretch 比例、无
 *               stretch 时按可伸展条目、再按全部条目均分），交叉轴铺满
 *               （条目自身对齐由 XWidgetItem 等条目实现叠加）；
 *             - 方向：RightToLeft/BottomToTop 镜像摆放；setDirection
 *               翻转水平/垂直主轴时按 Qt 语义翻转 magic 空白
 *               （addSpacing/addStretch/addStrut/insertSpacerItem 自动
 *               挂接的内部空白）的横纵尺寸与策略；
 *             - 对齐/伸展重载：addWidgetEx/insertWidgetEx（stretch+
 *               alignment）、addLayoutEx（stretch）、addStrut 交叉轴
 *               最小尺寸、addSpacerItem/insertSpacerItem（受
 *               XLAYOUT_SPACER_ON 门控）；
 *             - 便捷工厂 XHBoxLayout_create/XVBoxLayout_create/
 *               XBoxLayout_create。
 * @note       伸展数组 m_stretches 与 m_items 一一平行，追加/插入/取出
 *             条目时同步维护；二维数组仅按条目数存储，不额外引入平台
 *             类型。本文件不依赖任何平台 API，嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XBoxLayout.h"
#include "XLayout_Internal.h"
#include "XMemory.h"
#include <string.h>

#if XLAYOUT_ON && XLAYOUT_BOX_ON

/* ==================== 内部几何描述（对齐 QLayoutStruct） ==================== */

/** @brief 主轴几何快照（对齐 Qt 6.8 QLayoutStruct）。
 * @details 每个条目在主轴/交叉轴的尺寸约束与伸展属性：
 *          - hint/min/max：主轴方向首选/最小/最大尺寸（空条目也保留其
 *            真实尺寸，对齐 Qt——空条目仍参与 qGeomCalc 求和，但不占用
 *            周期间距；isEmpty 仅表示“不参与间距与几何应用”，不表示
 *            尺寸置 0，与 QSpacerItem::isEmpty 真机行为一致）；
 *          - crossHint/crossMin/crossMax：交叉轴方向尺寸约束；
 *          - stretch：伸展因子（显式伸展值优先，否则取控件的水平/垂直
 *            伸展因子，对齐 QBoxLayoutItem::hStretch/vStretch）；
 *          - spacing：本条目之后到下一个非空条目之间的间距；
 *          - expanding/crossExpanding：主轴/交叉轴是否可伸展；
 *          - empty：条目是否为空（对标 isEmpty()）；
 *          - hiddenWidget：空且承载控件（隐藏控件），交叉轴累计跳过；
 *          - done/pos/size：qGeomCalc 运行期临时与结果字段。
 * @note     XWIDGET_MAX_SIZE 对应 Qt QLAYOUTSIZE_MAX。 */
typedef struct XBoxGeom
{
    int   hint;            /**< 首选尺寸（主轴）。 */
    int   min;             /**< 最小尺寸（主轴）。 */
    int   max;             /**< 最大尺寸（主轴）。 */
    int   crossHint;       /**< 首选尺寸（交叉轴）。 */
    int   crossMin;        /**< 最小尺寸（交叉轴）。 */
    int   crossMax;        /**< 最大尺寸（交叉轴）。 */
    int   stretch;         /**< 伸展因子。 */
    int   spacing;         /**< 与下一非空条目的间距。 */
    bool  expanding;       /**< 主轴方向是否可伸展。 */
    bool  crossExpanding;  /**< 交叉轴方向是否可伸展。 */
    bool  empty;           /**< 是否为空条目（isEmpty）。 */
    bool  hiddenWidget;    /**< 空条目且承载控件（隐藏控件，交叉轴忽略）。 */
    bool  done;            /**< 运行期：是否已定稿。 */
    int   pos;             /**< 分配结果：起点。 */
    int   size;            /**< 分配结果：长度。 */
} XBoxGeom;

/* ==================== 静态辅助函数 ==================== */

/** @brief 是否水平方向。 */
static bool XBoxLayout_isHorizontal(const XBoxLayout* self)
{
    return self->m_direction == XBoxLayoutDirection_LeftToRight ||
           self->m_direction == XBoxLayoutDirection_RightToLeft;
}

/** @brief 读主轴分量：水平读宽度，垂直读高度。 */
static int XBoxLayout_axisValue(const XBoxLayout* self, const XSize* size)
{
    return XBoxLayout_isHorizontal(self) ? size->width : size->height;
}

/** @brief 读交叉轴分量。 */
static int XBoxLayout_crossValue(const XBoxLayout* self, const XSize* size)
{
    return XBoxLayout_isHorizontal(self) ? size->height : size->width;
}

/** @brief 按数值写主轴分量。 */
static void XBoxLayout_setAxisValue(const XBoxLayout* self, XSize* size, int v)
{
    if (XBoxLayout_isHorizontal(self))
        size->width = v;
    else
        size->height = v;
}

/** @brief 按数值写交叉轴分量。 */
static void XBoxLayout_setCrossValue(const XBoxLayout* self, XSize* size, int v)
{
    if (XBoxLayout_isHorizontal(self))
        size->height = v;
    else
        size->width = v;
}

/** @brief 条目标记为 magic 空白（内部；setDirection 翻转主轴时按空白处理）。
 * @details XSpacerItem 的 m_isMagic 由盒式布局内部维护，对标 Qt
 *          QBoxLayoutItem::magic；addSpacing/addStretch/addStrut/
 *          insertSpacerItem 挂接的空白置 1，调用方手工创建的空白
 *          （addItem 传入）不置位。 */
static void XBoxLayout_markMagic(XLayoutItem* item)
{
    XSpacerItem* sp;
    if (!item) return;
    sp = XLayoutItem_spacerItem_base(item);
    if (sp) sp->m_isMagic = 1;
}

/** @brief 伸展数组扩容（按 XBOXLAYOUT_STRETCH_CHUNK 整块增长）。 */
static bool XBoxLayout_growStretches(XBoxLayout* self, int need)
{
    int newCap;
    int* arr;
    if (!self) return false;
    if (need <= self->m_stretchCapacity) return true;
    newCap = self->m_stretchCapacity
                 ? self->m_stretchCapacity
                 : XBOXLAYOUT_STRETCH_CHUNK;
    while (newCap < need) newCap += XBOXLAYOUT_STRETCH_CHUNK;
    arr = (int*)XRealloc_System(self->m_stretches,
                                (size_t)newCap * sizeof(int));
    if (!arr) return false;
    memset(arr + self->m_stretchCapacity, 0,
           (size_t)(newCap - self->m_stretchCapacity) * sizeof(int));
    self->m_stretches = arr;
    self->m_stretchCapacity = newCap;
    return true;
}

/** @brief 在指定索引插入伸展插槽（与条目插入平行；内部由 insert* 使用）。 */
static bool XBoxLayout_insertStretchSlot(XBoxLayout* self, int index, int stretch)
{
    int idx;
    if (!self) return false;
    if (index < 0) index = (int)self->m_base.m_itemCount;
    if (index > (int)self->m_base.m_itemCount) index = (int)self->m_base.m_itemCount;
    if (!XBoxLayout_growStretches(self, self->m_base.m_itemCount + 1))
        return false;
    idx = index;
    memmove(&self->m_stretches[idx + 1], &self->m_stretches[idx],
            (size_t)(self->m_base.m_itemCount - idx) * sizeof(int));
    self->m_stretches[idx] = stretch < 0 ? 0 : stretch;
    return true;
}


/** @brief 交叉轴最大尺寸累计（对齐 Qt 6.8 qMaxExpCalc，qlayoutengine_p.h）。
 * @details 维护累计态 (max, exp, empty)：
 *          - 已有条目可伸展：与新增可伸展条目取最大；
 *          - 否则：新增可伸展、或累计为空且（新条目非空或累计最大为
 *            0）时取新条目最大尺寸；两者情况一致时取较小值。
 *          exp 吸收新增条目的可伸展位；empty 为两者且运算。这是 Qt
 *          setupGeom 交叉轴最大尺寸的精确定义。 */
static void XBoxLayout_qMaxExpCalc(int* max, bool* exp, bool* empty,
                                   int boxmax, bool boxexp, bool boxempty)
{
    if (*exp) {
        if (boxexp && boxmax > *max)
            *max = boxmax;
    } else {
        if (boxexp || ((*empty) && (!boxempty || *max == 0)))
            *max = boxmax;
        else if ((*empty) == boxempty && boxmax < *max)
            *max = boxmax;
    }
    *exp = *exp || boxexp;
    *empty = *empty && boxempty;
}

/** @brief 主轴智能首选尺寸（对齐 QLayoutStruct::smartSizeHint）。
 * @details 显式伸展因子 > 0 的条目以最小尺寸参与分配（多余空间全部
 *          按伸展因子分配，不再按首选尺寸均摊）。 */
static int XBoxLayout_smartSizeHint(const XBoxGeom* g)
{
    return (g->stretch > 0) ? g->min : g->hint;
}

/** @brief 统一间距解析（对齐 QLayoutStruct::effectiveSpacer）。 */
static int XBoxLayout_effectiveSpacer(int uniformSpacer, int spacing)
{
    return (uniformSpacer >= 0) ? uniformSpacer : spacing;
}

/* ==================== 几何收集（对齐 QBoxLayoutPrivate::setupGeom） ==================== */

/** @brief 收集全部条目的主/交叉轴几何快照并返回有效间距。
 * @details 对齐 Qt setupGeom：
 *          - 空条目保留真实尺寸（strut 的交叉尺寸因此参与交叉轴最小/
 *            首选累计；addSpacing/addStretch 等 magic 空白的主轴尺寸
 *            参与 qGeomCalc 求和与分配），对齐 Qt 6.8 实机行为；
 *          - spacing 记在“前一非空条目”上，连续非空条目之间有且仅有
 *            一份间距；
 *          - 隐藏控件（empty 且 widget()!=NULL）置 hiddenWidget，
 *            交叉轴 qMaxExpCalc 累计时跳过（对齐 Qt setupGeom 的
 *            ignore 语义）；
 *          - stretch 取显式伸展值 m_stretches[i]，否则取控件 sizePolicy
 *            的 horizontalStretch/verticalStretch（对齐
 *            QBoxLayoutItem::hStretch/vStretch）；
 *          - 主轴/交叉轴 hint/min 在上层已按 max 收敛，避免越界。 */
static int XBoxLayout_collectGeom(const XBoxLayout* self, XBoxGeom* out)
{
    int i;
    int spacing;
    int previousNonEmpty = -1;
    bool hExp = false;
    bool vExp = false;
    bool dummy = true;
    int maxW = XWIDGET_MAX_SIZE;
    int maxH = XWIDGET_MAX_SIZE;
    const XLayoutItem** items;
    if (!self) return 0;
    if (!self->m_base.m_items)
        return XLayout_effectiveSpacing((const XLayout*)self);
    items = (const XLayoutItem**)self->m_base.m_items;
    if (XBoxLayout_isHorizontal(self))
        maxH = 0;
    else
        maxW = 0;
    spacing = XLayout_effectiveSpacing((const XLayout*)self);
    for (i = 0; i < (int)self->m_base.m_itemCount; ++i) {
        XBoxGeom* g = &out[i];
        XLayoutItem* item = self->m_base.m_items[i];
        XSize hint;
        XSize min;
        XSize max;
        XLayoutExpandingDirections exp;
        bool empty;
        bool mainExpand;
        memset(g, 0, sizeof(XBoxGeom));
        g->spacing = 0;
        if (!item) { g->empty = true; continue; }
        hint = XLayoutItem_sizeHint_base(item);
        min = XLayoutItem_minimumSize_base(item);
        max = XLayoutItem_maximumSize_base(item);
        exp = XLayoutItem_expandingDirections_base(item);
        empty = XLayoutItem_isEmpty_base(item);
        if (hint.width < 0) hint.width = 0;
        if (hint.height < 0) hint.height = 0;
        if (min.width < 0) min.width = 0;
        if (min.height < 0) min.height = 0;
        if (max.width < 0 || max.width > XWIDGET_MAX_SIZE)
            max.width = XWIDGET_MAX_SIZE;
        if (max.height < 0 || max.height > XWIDGET_MAX_SIZE)
            max.height = XWIDGET_MAX_SIZE;
        if (hint.width > max.width) hint.width = max.width;
        if (hint.height > max.height) hint.height = max.height;
        if (min.width > max.width) min.width = max.width;
        if (min.height > max.height) min.height = max.height;
        if (hint.width < min.width) hint.width = min.width;
        if (hint.height < min.height) hint.height = min.height;

        if (XBoxLayout_isHorizontal(self)) {
            g->hint = hint.width;
            g->min = min.width;
            g->max = max.width;
            g->crossHint = hint.height;
            g->crossMin = min.height;
            g->crossMax = max.height;
            g->crossExpanding =
                (exp & XLayoutExpandingDirection_Vertical) != 0;
            mainExpand =
                (exp & XLayoutExpandingDirection_Horizontal) != 0;
        } else {
            g->hint = hint.height;
            g->min = min.height;
            g->max = max.height;
            g->crossHint = hint.width;
            g->crossMin = min.width;
            g->crossMax = max.width;
            g->crossExpanding =
                (exp & XLayoutExpandingDirection_Horizontal) != 0;
            mainExpand =
                (exp & XLayoutExpandingDirection_Vertical) != 0;
        }
        if (self->m_stretches && i < self->m_stretchCapacity &&
            self->m_stretches[i] > 0)
            mainExpand = true;
        g->expanding = mainExpand;
        g->empty = empty;
        if (empty && XLayoutItem_widget_base(item))
            g->hiddenWidget = true;

        /* 间距记在前一非空条目上（对齐 setupGeom）。 */
        if (!empty) {
            int s = 0;
            if (previousNonEmpty >= 0) {
                s = spacing;
                out[previousNonEmpty].spacing = s;
            }
            previousNonEmpty = i;
        }

        /* 伸展因子：显式值优先，否则控件 sizePolicy 伸展因子。 */
        if (self->m_stretches && i < self->m_stretchCapacity &&
            self->m_stretches[i] > 0) {
            g->stretch = self->m_stretches[i];
        } else if (XLayoutItem_widget_base(item)) {
            XWidgetSizePolicy pol = XWidget_sizePolicy(
                XLayoutItem_widget_base(item));
            if (XBoxLayout_isHorizontal(self))
                g->stretch = XWidgetSizePolicy_horizontalStretch(&pol);
            else
                g->stretch = XWidgetSizePolicy_verticalStretch(&pol);
        }
        if (g->stretch < 0) g->stretch = 0;
        if (g->stretch > 0)
            g->expanding = true;
    }
    (void)hExp;
    (void)vExp;
    (void)dummy;
    (void)maxW;
    (void)maxH;
    return spacing;
}

/* ==================== 主轴分配算法（移植 Qt 6.8 qGeomCalc） ==================== */

/** @brief 固定点比例运算（对齐 Qt qlayoutengine.cpp Fixed64）。
 * @details 256 倍定点累加并四舍五入，逐条目传播余数，与 Qt 的
 *          toFixed/fRound 完全一致，保证像素级分配结果一致。 */
typedef long long XBoxFixed64;

static XBoxFixed64 XBoxLayout_toFixed(int i)
{
    return (XBoxFixed64)i * 256;
}

static int XBoxLayout_fRound(XBoxFixed64 i)
{
    return (i % 256 < 128) ? (int)(i / 256) : (int)(1 + i / 256);
}

/** @brief 主轴几何分配（移植 Qt 6.8 qGeomCalc，qlayoutengine.cpp）。
 * @param chain  几何数组（输入 hint/min/max/stretch/empty/expanding/
 *               spacing；输出 pos/size）。
 * @param start  起始索引。
 * @param count  条目数。
 * @param pos    分配区间起点。
 * @param space  分配区间长度。
 * @param spacer 统一间距；<0 表示使用逐条 spacing（Qt 夹具调用处传 -1）。
 * @details 三段分配：
 *          1) 空间低于最小总宽：Qt 会排序缩放最小尺寸与间距。本项目
 *             保留既有“按最小逐项给满、间距完整、超界自然溢出”的
 *             语义——布局激活时窗口会放大到布局最小总宽，与 Qt 的可
 *             观察行为一致（见 VXBoxLayout_setGeometry 注释）；
 *          2) 空间介于最小/首选之间：先给 min>=smart 的固定项，其余
 *             按余量均分收缩，跌破最小即定稿；
 *          3) 有余空间：先定稿 max<=smart 或（非全空非扩张时的）空
 *             条目，再做固定点试分配：按伸展因子、可伸展条目、全部
 *             条目三种权重依次取舍；deficit 优先补足不足项、surplus
 *             优先没收超出最大项，迭代到收敛；最后把多余空间均匀分
 *             给链首/链尾/条目之间的间距空档（只分给非空条目之间的
 *             位置，关间隔位置由 empty 条件跳过）。 */
static void XBoxLayout_qGeomCalc(XBoxGeom* chain, int start, int count,
                                 int pos, int space, int spacer)
{
    int cHint = 0;
    int cMin = 0;
    int sumStretch = 0;
    int sumSpacing = 0;
    int expandingCount = 0;
    bool allEmptyNonstretch = true;
    int pendingSpacing = -1;
    int spacerCount = 0;
    int extraspace = 0;
    int i;
    bool useSpacing;

    if (!chain) return;
    useSpacing = (spacer >= 0);
    for (i = start; i < start + count; ++i) {
        XBoxGeom* data = &chain[i];
        int es;
        data->done = false;
        cHint += XBoxLayout_smartSizeHint(data);
        cMin += data->min;
        sumStretch += data->stretch;
        if (!data->empty) {
            es = XBoxLayout_effectiveSpacer(spacer, data->spacing);
            if (pendingSpacing >= 0) {
                sumSpacing += pendingSpacing;
                ++spacerCount;
            }
            pendingSpacing = es;
        }
        if (data->expanding)
            expandingCount++;
        allEmptyNonstretch = allEmptyNonstretch && data->empty &&
                             !data->expanding && data->stretch <= 0;
    }

    if (space < cMin + sumSpacing) {
        /* 空间不足最小总宽：按最小逐项给满（保留本项目既有语义）。 */
        int p = pos;
        for (i = start; i < start + count; ++i) {
            XBoxGeom* data = &chain[i];
            data->done = true;
            data->size = data->min;
            data->pos = p;
            p += data->min;
            if (!data->empty)
                p += data->spacing;
        }
        return;
    }

    if (space < cHint + sumSpacing) {
        /* 空间介于最小与首选之间：先给固定项，再逐轮均分收缩。 */
        int n = count;
        int space_left = space - sumSpacing;
        int overdraft = cHint - space_left;

        for (i = start; i < start + count; ++i) {
            XBoxGeom* data = &chain[i];
            if (!data->done && data->min >= XBoxLayout_smartSizeHint(data)) {
                data->size = XBoxLayout_smartSizeHint(data);
                data->done = true;
                space_left -= data->size;
                n--;
            }
        }
        {
            bool finished = (n == 0);
            while (!finished) {
                XBoxFixed64 fp_over = XBoxLayout_toFixed(overdraft);
                XBoxFixed64 fp_w = 0;
                finished = true;
                for (i = start; i < start + count; ++i) {
                    XBoxGeom* data = &chain[i];
                    int w;
                    if (data->done) continue;
                    fp_w += fp_over / n;
                    w = XBoxLayout_fRound(fp_w);
                    data->size = XBoxLayout_smartSizeHint(data) - w;
                    fp_w -= XBoxLayout_toFixed(w);
                    if (data->size < data->min) {
                        data->done = true;
                        data->size = data->min;
                        finished = false;
                        overdraft -= XBoxLayout_smartSizeHint(data) - data->min;
                        n--;
                        break;
                    }
                }
            }
        }
        extraspace = 0;
    } else {
        /* 有余空间。 */
        int n = count;
        int space_left = space - sumSpacing;

        for (i = start; i < start + count; ++i) {
            XBoxGeom* data = &chain[i];
            if (!data->done &&
                (data->max <= XBoxLayout_smartSizeHint(data) ||
                 (!allEmptyNonstretch && data->empty &&
                  !data->expanding && data->stretch == 0))) {
                data->size = XBoxLayout_smartSizeHint(data);
                data->done = true;
                space_left -= data->size;
                sumStretch -= data->stretch;
                if (data->expanding)
                    expandingCount--;
                n--;
            }
        }
        extraspace = space_left;
        {
            int surplus;
            int deficit;
            do {
                XBoxFixed64 fp_space = XBoxLayout_toFixed(space_left);
                XBoxFixed64 fp_w = 0;
                surplus = deficit = 0;
                for (i = start; i < start + count; ++i) {
                    XBoxGeom* data = &chain[i];
                    int w;
                    if (data->done) continue;
                    extraspace = 0;
                    if (sumStretch > 0) {
                        fp_w += (fp_space * (XBoxFixed64)data->stretch) /
                                sumStretch;
                    } else if (expandingCount > 0) {
                        fp_w += (fp_space *
                                 (XBoxFixed64)(data->expanding ? 1 : 0)) /
                                expandingCount;
                    } else {
                        fp_w += fp_space * 1 / n;
                    }
                    w = XBoxLayout_fRound(fp_w);
                    data->size = w;
                    fp_w -= XBoxLayout_toFixed(w);
                    if (w < XBoxLayout_smartSizeHint(data)) {
                        deficit += XBoxLayout_smartSizeHint(data) - w;
                    } else if (w > data->max) {
                        surplus += w - data->max;
                    }
                }
                if (deficit > 0 && surplus <= deficit) {
                    for (i = start; i < start + count; ++i) {
                        XBoxGeom* data = &chain[i];
                        if (!data->done && data->size <
                                              XBoxLayout_smartSizeHint(data)) {
                            data->size = XBoxLayout_smartSizeHint(data);
                            data->done = true;
                            space_left -= data->size;
                            sumStretch -= data->stretch;
                            if (data->expanding)
                                expandingCount--;
                            n--;
                        }
                    }
                }
                if (surplus > 0 && surplus >= deficit) {
                    for (i = start; i < start + count; ++i) {
                        XBoxGeom* data = &chain[i];
                        if (!data->done && data->size > data->max) {
                            data->size = data->max;
                            data->done = true;
                            space_left -= data->max;
                            sumStretch -= data->stretch;
                            if (data->expanding)
                                expandingCount--;
                            n--;
                        }
                    }
                }
            } while (n > 0 && surplus != deficit);
            if (n == 0)
                extraspace = space_left;
        }
    }

    /* 收尾：多余空间均匀分给间距空档（含链首/链尾），逐条目定稿位置。 */
    {
        int extra = extraspace / (spacerCount + 2);
        int p = pos + extra;
        for (i = start; i < start + count; ++i) {
            XBoxGeom* data = &chain[i];
            data->pos = p;
            p += data->size;
            if (!data->empty)
                p += XBoxLayout_effectiveSpacer(spacer, data->spacing) + extra;
        }
    }
}

/* ==================== 交叉轴/整体度量（对齐 QBoxLayoutPrivate::setupGeom） ==================== */

/** @brief 主轴/交叉轴合计度量（对齐 QBoxLayoutPrivate::setupGeom）。
 * @details 对齐 Qt setupGeom：
 *          - 主轴最小/首选：全部条目逐项求和，每项附上由上一非空条目
 *            记录的间距（空条目计入自身尺寸但不引发间距）；
 *          - 交叉轴最小/首选：各条目取最大（空条目也参与；隐藏控件
 *            仍按条目自身尺寸参与这两个指标）；
 *          - 主轴最大：全部条目求和（含空条目），上限 XWIDGET_MAX_SIZE，
 *            并保证不小于交叉轴最大；
 *          - 交叉轴最大：用 qMaxExpCalc 累计（跳过隐藏控件条目）；
 *          - max 不小于 min；hint 收敛到 [min,max]；最后附加内容边距。
 * @param minOut/hintOut/maxOut 可空：输出最小/首选/最大尺寸。
 * @param mainExpanding/crossExpanding 可空：输出主轴/交叉轴是否可伸展。
 */
static void XBoxLayout_calcMetrics(const XBoxLayout* self, XSize* minOut,
                                   XSize* hintOut, XSize* maxOut,
                                   bool* mainExpanding, bool* crossExpanding)
{
    XBoxGeom* geom;
    XMargins margins;
    int i;
    int count;
    int spacing;
    int mainMin = 0;
    int mainHint = 0;
    int mainMax = 0;
    int crossMin = 0;
    int crossHint = 0;
    int crossMax = 0;
    bool mainExp = false;
    bool hExpAcc = false;
    bool vExpAcc = false;
    bool hDummy = true;
    bool vDummy = true;
    if (minOut) XSize_init(minOut, 0, 0);
    if (hintOut) XSize_init(hintOut, 0, 0);
    if (maxOut) XSize_init(maxOut, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    if (!self || !self->m_base.m_items) return;
    count = (int)self->m_base.m_itemCount;
    geom = (XBoxGeom*)XMalloc_Hybrid((size_t)(count ? count : 1) *
                                     sizeof(XBoxGeom));
    if (!geom) return;
    spacing = XBoxLayout_collectGeom(self, geom);
    for (i = 0; i < count; ++i) {
        XBoxGeom* g = &geom[i];
        int gap;
        (void)spacing;
        gap = (g->empty || i == 0) ? 0 : geom[i - 1].spacing;
        mainMin += gap;
        mainHint += gap;
        mainMax += gap;
        mainMin += g->min;
        mainHint += g->hint;
        mainMax += g->max;
        if (mainMin > XWIDGET_MAX_SIZE) mainMin = XWIDGET_MAX_SIZE;
        if (mainHint > XWIDGET_MAX_SIZE) mainHint = XWIDGET_MAX_SIZE;
        if (mainMax > XWIDGET_MAX_SIZE) mainMax = XWIDGET_MAX_SIZE;
        if (g->expanding) mainExp = true;
        if (g->crossMin > crossMin) crossMin = g->crossMin;
        if (g->crossHint > crossHint) crossHint = g->crossHint;
    }
    /* 交叉轴最大：水平累计垂直向 maxH、垂直累计水平向 maxW
     * （均为交叉轴；qMaxExpCalc 会跳过大小方向累积误差）。 */
    for (i = 0; i < count; ++i) {
        XBoxGeom* g = &geom[i];
        if (g->hiddenWidget) continue;
        if (XBoxLayout_isHorizontal(self)) {
            XBoxLayout_qMaxExpCalc(&crossMax, &vExpAcc, &vDummy,
                                   g->crossMax, g->crossExpanding, g->empty);
        } else {
            XBoxLayout_qMaxExpCalc(&crossMax, &hExpAcc, &hDummy,
                                   g->crossMax, g->crossExpanding, g->empty);
        }
    }
    /* 首选交叉尺寸（尤其 addStrut）是布局的硬提示，即使条目本身
     * 标记为空，也不能被交叉轴 maximum 的 qMaxExpCalc 结果截断。 */
    if (crossMax < crossHint) crossMax = crossHint;
    if (crossMax > XWIDGET_MAX_SIZE) crossMax = XWIDGET_MAX_SIZE;
    if (mainMax < crossMax) mainMax = crossMax;
    if (crossMin > crossMax) crossMin = crossMax;
    if (crossHint > crossMax) crossHint = crossMax;
    if (crossHint < crossMin) crossHint = crossMin;
    if (mainMin > mainMax) mainMin = mainMax;
    if (mainHint > mainMax) mainHint = mainMax;
    if (mainHint < mainMin) mainHint = mainMin;
    margins = XLayout_contentsMargins((const XLayout*)self);
    mainMin += margins.left + margins.right;
    mainHint += margins.left + margins.right;
    mainMax += margins.left + margins.right;
    crossMin += margins.top + margins.bottom;
    crossHint += margins.top + margins.bottom;
    crossMax += margins.top + margins.bottom;
    if (mainMax > XWIDGET_MAX_SIZE) mainMax = XWIDGET_MAX_SIZE;
    if (crossMax > XWIDGET_MAX_SIZE) crossMax = XWIDGET_MAX_SIZE;
    if (mainMin > XWIDGET_MAX_SIZE) mainMin = XWIDGET_MAX_SIZE;
    if (mainHint > XWIDGET_MAX_SIZE) mainHint = XWIDGET_MAX_SIZE;
    if (crossMin > XWIDGET_MAX_SIZE) crossMin = XWIDGET_MAX_SIZE;
    if (crossHint > XWIDGET_MAX_SIZE) crossHint = XWIDGET_MAX_SIZE;
    if (XBoxLayout_isHorizontal(self)) {
        if (minOut) { minOut->width = mainMin; minOut->height = crossMin; }
        if (hintOut) { hintOut->width = mainHint; hintOut->height = crossHint; }
        if (maxOut) { maxOut->width = mainMax; maxOut->height = crossMax; }
        if (mainExpanding) *mainExpanding = mainExp;
        if (crossExpanding) *crossExpanding = vExpAcc;
    } else {
        if (minOut) { minOut->width = crossMin; minOut->height = mainMin; }
        if (hintOut) { hintOut->width = crossHint; hintOut->height = mainHint; }
        if (maxOut) { maxOut->width = crossMax; maxOut->height = mainMax; }
        if (mainExpanding) *mainExpanding = mainExp;
        if (crossExpanding) *crossExpanding = hExpAcc;
    }
    XFree_Hybrid(geom);
}

static XSize VXBoxLayout_sizeHint(const XLayoutItem* item)
{
    XSize out;
    XSize_init(&out, 0, 0);
    XBoxLayout_calcMetrics((const XBoxLayout*)item, NULL, &out, NULL, NULL, NULL);
    return out;
}

/** @brief 盒式布局最小尺寸。 */
static XSize VXBoxLayout_minimumSize(const XLayoutItem* item)
{
    XSize out;
    XSize_init(&out, 0, 0);
    XBoxLayout_calcMetrics((const XBoxLayout*)item, &out, NULL, NULL, NULL, NULL);
    return out;
}

/** @brief 盒式布局最大尺寸；设置自身对齐后对应轴放开（对标 Qt）。 */
static XSize VXBoxLayout_maximumSize(const XLayoutItem* item)
{
    const XBoxLayout* self = (const XBoxLayout*)item;
    XSize out;
    XLayoutAlignments align;
    XSize_init(&out, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    XBoxLayout_calcMetrics(self, NULL, NULL, &out, NULL, NULL);
    align = XLayoutItem_alignment(item);
    if (align & XLayoutAlignment_HorizontalMask)
        out.width = XWIDGET_MAX_SIZE;
    if (align & XLayoutAlignment_VerticalMask)
        out.height = XWIDGET_MAX_SIZE;
    return out;
}

/** @brief 盒式布局伸展方向：主轴/交叉轴任一子条目可伸展即返回该方向。 */
static XLayoutExpandingDirections VXBoxLayout_expandingDirections(
    const XLayoutItem* item)
{
    const XBoxLayout* self = (const XBoxLayout*)item;
    bool mainExp = false;
    bool crossExp = false;
    XLayoutExpandingDirections out = XLayoutExpandingDirection_None;
    if (!item) return out;
    XBoxLayout_calcMetrics(self, NULL, NULL, NULL, &mainExp, &crossExp);
    if (XBoxLayout_isHorizontal(self)) {
        if (mainExp) out |= XLayoutExpandingDirection_Horizontal;
        if (crossExp) out |= XLayoutExpandingDirection_Vertical;
    } else {
        if (mainExp) out |= XLayoutExpandingDirection_Vertical;
        if (crossExp) out |= XLayoutExpandingDirection_Horizontal;
    }
    return out;
}

/** @brief 盒式布局空判定：完全委托基类（没有任何条目即为空）。 */
static bool VXBoxLayout_isEmpty(const XLayoutItem* item)
{
    if (!item) return true;
    return ((const XLayout*)item)->m_itemCount == 0;
}

/** @brief 查询盒式布局是否支持 heightForWidth。 */
static bool VXBoxLayout_hasHeightForWidth(const XLayoutItem* item)
{
    const XBoxLayout* self = (const XBoxLayout*)item;
    int i;
    if (!self) return false;
    for (i = 0; i < (int)self->m_base.m_itemCount; ++i) {
        if (self->m_base.m_items[i] &&
            XLayoutItem_hasHeightForWidth_base(self->m_base.m_items[i]))
            return true;
    }
    return false;
}

/** @brief 垂直盒式 heightForWidth：所有条目在给定宽度下的 hfw 高度求和。
 *  水平盒式：按主轴分配宽度后再取各条目的 hfw 高度最大值。
 * @details 对齐 Qt QBoxLayout::heightForWidth/minimumHeightForWidth：
 *          - 水平盒：qGeomCalc 按内宽分配各条目主轴尺寸，随后取条目
 *            heightForWidth(分配宽) 的最大值；
 *          - 垂直盒：所有条目在相同内宽下取 hfw 高度求和，非空条目
 *            之间按条目间距累加；
 *          - 结果再附加上下内容边距，且不小于布局 minimumSize 高度
 *            （对标 QLayout 的 qSmartMinSize 语义）。 */
static int XBoxLayout_doHeightForWidth(const XBoxLayout* self, int width,
                                       bool minMode)
{
    XBoxGeom* geom;
    int i;
    int n;
    int spacing;
    int marginsL;
    int marginsR;
    int inner;
    int result = 0;
    XMargins margin;
    if (!self) return -1;
    if (!VXBoxLayout_hasHeightForWidth((const XLayoutItem*)self)) return -1;
    n = (int)self->m_base.m_itemCount;
    geom = (XBoxGeom*)XMalloc_Hybrid((size_t)(n ? n : 1) * sizeof(XBoxGeom));
    if (!geom) return -1;
    spacing = XBoxLayout_collectGeom(self, geom);
    margin = XLayout_contentsMargins((const XLayout*)self);
    marginsL = margin.left;
    marginsR = margin.right;
    inner = width - marginsL - marginsR;
    if (inner < 0) inner = 0;
    if (XBoxLayout_isHorizontal(self)) {
        int* sizes = (int*)XMalloc_Hybrid((size_t)(n ? n : 1) * sizeof(int));
        if (!sizes) { XFree_Hybrid(geom); return -1; }
        XBoxLayout_qGeomCalc(geom, 0, n, 0, inner, -1);
        for (i = 0; i < n; ++i) {
            int h;
            if (geom[i].empty) continue;
            if (minMode)
                h = XLayoutItem_hasHeightForWidth_base(self->m_base.m_items[i])
                        ? XLayoutItem_minimumHeightForWidth_base(
                              self->m_base.m_items[i], geom[i].size)
                        : 0;
            else
                h = XLayoutItem_hasHeightForWidth_base(self->m_base.m_items[i])
                        ? XLayoutItem_heightForWidth_base(
                              self->m_base.m_items[i], geom[i].size)
                        : 0;
            if (h < 0) h = 0;
            if (h > result) result = h;
        }
        XFree_Hybrid(sizes);
    } else {
        int countActive = 0;
        for (i = 0; i < n; ++i) {
            int h;
            int gap = 0;
            if (geom[i].empty) continue;
            countActive++;
            if (geom[i].spacing > 0)
                gap = geom[i].spacing;
            (void)gap;
            if (minMode)
                h = XLayoutItem_hasHeightForWidth_base(self->m_base.m_items[i])
                        ? XLayoutItem_minimumHeightForWidth_base(
                              self->m_base.m_items[i], inner)
                        : geom[i].min;
            else
                h = XLayoutItem_hasHeightForWidth_base(self->m_base.m_items[i])
                        ? XLayoutItem_heightForWidth_base(
                              self->m_base.m_items[i], inner)
                        : geom[i].hint;
            if (h < 0) h = 0;
            result += h;
            if (countActive > 1) result += spacing;
        }
    }
    XFree_Hybrid(geom);
    result += margin.top + margin.bottom;
    {
        XSize minSize = XLayoutItem_minimumSize_base((const XLayoutItem*)self);
        if (result < minSize.height)
            result = minSize.height;
    }
    return result;
}
/** @brief 盒式布局首选高度（给定宽度）。 */
static int VXBoxLayout_heightForWidth(const XLayoutItem* item, int width)
{
    if (!item) return -1;
    return XBoxLayout_doHeightForWidth((const XBoxLayout*)item, width, false);
}

/** @brief 盒式布局最小高度（给定宽度）。 */
static int VXBoxLayout_minimumHeightForWidth(const XLayoutItem* item, int width)
{
    if (!item) return -1;
    return XBoxLayout_doHeightForWidth((const XBoxLayout*)item, width, true);
}

/** @brief 追加条目：默认追加到末尾，stretch 0（对标 QBoxLayout::addItem）。 */
static void VXBoxLayout_addItem(XLayout* self, XLayoutItem* item)
{
    XBoxLayout* box;
    int idx;
    if (!self || !item) return;
    box = (XBoxLayout*)self;
    idx = XLayout_appendItem(self, item, false);
    if (idx >= 0)
        XBoxLayout_insertStretchSlot(box, idx, 0);
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

/** @brief 取出条目：先同步伸展数组，再调用父类实现完成条目数组收缩。 */
static XLayoutItem* VXBoxLayout_takeAt(XLayout* self, int index)
{
    XLayoutItem* item;
    XBoxLayout* box;
    if (!self || index < 0 || index >= (int)self->m_itemCount) return NULL;
    box = (XBoxLayout*)self;
    item = XClass_Parent(XLayout, EXLayout_TakeAt,
                         XLayoutItem*(*)(XLayout*, int))(self, index);
    if (box->m_stretches && index < box->m_stretchCapacity) {
        memmove(&box->m_stretches[index], &box->m_stretches[index + 1],
                (size_t)(self->m_itemCount - index) * sizeof(int));
        box->m_stretches[self->m_itemCount] = 0;
    }
    return item;
}

/** @brief 原位替换条目（对标 QBoxLayoutPrivate::replaceAt）。
 * @details 只替换条目数组项，伸展数组与索引对应关系不变（stretch 保留）；
 *         旧条目所有权转移给调用方（owned 清 0、子布局解绑父布局），
 *         新条目标记为布局所有并经 XLayout_linkItem 接入布局链。
 *         不触发整列收缩，几何在下次 setGeometry 时重算。 */
static XLayoutItem* VXBoxLayout_replaceItemAt(XLayout* self, int index,
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

/** @brief 盒式布局几何分配（核心算法见文件头说明）。
 * @details 对齐 Qt QBoxLayout::setGeometry：
 *          1. 先按布局自身对齐收拢分配矩形，再去内容边距得到内部矩形；
 *          2. 垂直盒且存在 hfw 条目：把该条目首选/最小高度替换为
 *             heightForWidth(宽度)，宽度按 qBound(min.width, 内宽,
 *             max.width)（对齐 Qt 6.8 setGeometry 的 qBound 用法）；
 *          3. qGeomCalc 用内部矩形的主轴 pos/space 分配全部条目（含
 *             空条目，输出每条的 pos/size）；
 *          4. 逐条目摆放：交叉轴铺满，主轴按 pos/size；RightToLeft /
 *             BottomToTop 用 Qt 镜像公式 2*inner 边界 - pos - size；
 *          5. 空条目同样按 pos/size 应用几何（QSpacerItem 空但分配
 *             真实空白尺寸；隐藏控件条目尺寸为 0）。 */
static void VXBoxLayout_setGeometry(XLayout* self, const XRect* rect)
{
    XBoxLayout* box;
    XBoxGeom* geom;
    int i;
    int n;
    XRect cr;
    XRect inner;
    int mainPos;
    int mainSpace;
    int crossPos;
    int crossSpace;
    XRect oldRect;
    bool reverse;
    bool horizontal;
    if (!self || !rect) return;
    box = (XBoxLayout*)self;
    /* Qt 在写入新几何前保留旧矩形；逆序更新避免方向镜像时产生
     * 不必要的视觉跳动，尤其是窗口尺寸收缩场景。 */
    oldRect = XLayoutItem_geometry_base((XLayoutItem*)self);
    ((XLayoutItem*)self)->m_geometry = *rect;
    cr = XLayoutItem_alignment((XLayoutItem*)self)
             ? XLayout_alignmentRect((const XLayout*)self, rect)
             : *rect;
    inner = XLayout_contentsRectForRect((const XLayout*)self, &cr);
    n = (int)self->m_itemCount;
    geom = (XBoxGeom*)XMalloc_Hybrid((size_t)(n ? n : 1) * sizeof(XBoxGeom));
    if (!geom) return;
    (void)XBoxLayout_collectGeom((const XBoxLayout*)self, geom);
    horizontal = XBoxLayout_isHorizontal(box);
    /* 垂直盒 + hfw：把支持 hfw 的条目的首选/最小高度替换为
     * heightForWidth(实际宽度)（对齐 QBoxLayout::setGeometry）。 */
    if (!horizontal && VXBoxLayout_hasHeightForWidth((const XLayoutItem*)self)) {
        for (i = 0; i < n; ++i) {
            XLayoutItem* item = self->m_items[i];
            XSize min;
            XSize max;
            int availW;
            int w;
            int h;
            if (!item || geom[i].empty) continue;
            if (!XLayoutItem_hasHeightForWidth_base(item)) continue;
            min = XLayoutItem_minimumSize_base(item);
            max = XLayoutItem_maximumSize_base(item);
            availW = inner.width;
            if (availW < 0) availW = 0;
            w = min.width;
            if (availW > w) w = availW;
            if (w > max.width) w = max.width;
            h = XLayoutItem_heightForWidth_base(item, w);
            if (h < 0) h = 0;
            geom[i].min = h;
            geom[i].hint = h;
        }
    }
    mainPos = horizontal ? inner.x : inner.y;
    mainSpace = horizontal ? inner.width : inner.height;
    crossPos = horizontal ? inner.y : inner.x;
    crossSpace = horizontal ? inner.height : inner.width;
    XBoxLayout_qGeomCalc(geom, 0, n, mainPos, mainSpace, -1);
    /* 与 Qt QBoxLayout::setGeometry 保持一致：水平布局在 RTL 下按右边界
     * 的增长方向决定是否逆序，垂直布局按下边界增长方向决定。 */
    reverse = horizontal
                  ? ((XRect_right(rect) > XRect_right(&oldRect)) !=
                     (box->m_direction == XBoxLayoutDirection_RightToLeft))
                  : (XRect_bottom(rect) > XRect_bottom(&oldRect));
    /* 逐条目摆放：交叉轴铺满，主轴按 pos/size，RTL/BTT 镜像（对齐 Qt：
     *   x = inner.left() + inner.right() - pos - size + 1
     *     = 2 * inner.x + inner.width - pos - size）。 */
    for (i = 0; i < n; ++i) {
        int index = reverse ? (n - i - 1) : i;
        XLayoutItem* item;
        XRect cell;
        item = self->m_items[index];
        if (!item) continue;
        if (horizontal) {
            int x;
            if (box->m_direction == XBoxLayoutDirection_RightToLeft)
                x = 2 * inner.x + inner.width - geom[index].pos -
                    geom[index].size;
            else
                x = geom[index].pos;
            XRect_init(&cell, x, crossPos, geom[index].size, crossSpace);
        } else {
            int y;
            if (box->m_direction == XBoxLayoutDirection_BottomToTop)
                y = 2 * inner.y + inner.height - geom[index].pos -
                    geom[index].size;
            else
                y = geom[index].pos;
            XRect_init(&cell, crossPos, y, crossSpace, geom[index].size);
        }
        XLayoutItem_setGeometry_base(item, &cell);
    }
    XFree_Hybrid(geom);
    self->m_isDirty = 0;
    self->m_activated = 1;
}
/** @brief 释放盒式布局资源。 */
static void VXBoxLayout_deinit(XBoxLayout* self)
{
    if (!self) return;
    XFree_System(self->m_stretches);
    self->m_stretches = NULL;
    self->m_stretchCapacity = 0;
    /* 条目数组/挂接由父类释放。 */
    XClass_Deinit_Parent(XLayout, (XLayout*)self);
}

/** @brief 拷贝盒式布局配置（不复制条目树；伸展数组按源长度复制）。 */
static void VXBoxLayout_copy(XBoxLayout* self, const XBoxLayout* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XBoxLayout_init(self, XBoxLayoutDirection_LeftToRight);
    XClass_Parent(XLayout, EXClass_Copy,
                  void(*)(XLayout*, const XLayout*))((XLayout*)self,
                                                     (const XLayout*)other);
    self->m_direction = other->m_direction;
    XFree_System(self->m_stretches);
    self->m_stretches = NULL;
    self->m_stretchCapacity = 0;
    if (other->m_stretches && other->m_base.m_itemCount > 0) {
        int need = (int)other->m_base.m_itemCount;
        if (XBoxLayout_growStretches(self, need))
            for (i = 0; i < need; ++i)
                self->m_stretches[i] = other->m_stretches[i];
    }
}

/** @brief 移动盒式布局：转移条目与伸展数组所有权。 */
static void VXBoxLayout_move(XBoxLayout* self, XBoxLayout* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XBoxLayout_init(self, XBoxLayoutDirection_LeftToRight);
    XClass_Parent(XLayout, EXClass_Move,
                  void(*)(XLayout*, XLayout*))((XLayout*)self, (XLayout*)other);
    self->m_direction = other->m_direction;
    XFree_System(self->m_stretches);
    self->m_stretches = other->m_stretches;
    self->m_stretchCapacity = other->m_stretchCapacity;
    other->m_stretches = NULL;
    other->m_stretchCapacity = 0;
}

XVtable* XBoxLayout_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBoxLayout)
    XVTABLE_INHERIT_XCLASS(XLayout);
    XVTABLE_OVERLOAD_DEFAULT(EXLayout_AddItem, VXBoxLayout_addItem);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SizeHint, VXBoxLayout_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumSize, VXBoxLayout_minimumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MaximumSize, VXBoxLayout_maximumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_ExpandingDirections,
                             VXBoxLayout_expandingDirections);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_IsEmpty, VXBoxLayout_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SetGeometry, VXBoxLayout_setGeometry);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HasHeightForWidth,
                             VXBoxLayout_hasHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HeightForWidth,
                             VXBoxLayout_heightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumHeightForWidth,
                             VXBoxLayout_minimumHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayout_TakeAt, VXBoxLayout_takeAt);
    XVTABLE_OVERLOAD_DEFAULT(EXLayout_ReplaceItemAt, VXBoxLayout_replaceItemAt);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBoxLayout_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXBoxLayout_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXBoxLayout_move);
    return XVTABLE_DEFAULT;
}

void XBoxLayout_init(XBoxLayout* self, XBoxLayoutDirection direction)
{
    if (!self) return;
    memset(self, 0, sizeof(XBoxLayout));
    XLayout_init((XLayout*)self);
    XClassSetVtable(self, XBoxLayout);
    self->m_direction = direction;
    self->m_stretches = NULL;
    self->m_stretchCapacity = 0;
}

static XBoxLayout* XBoxLayout_createInternal(XBoxLayoutDirection direction,
                                             XWidget* parent)
{
    XBoxLayout* self = (XBoxLayout*)XMalloc_System(sizeof(XBoxLayout));
    if (!self) return NULL;
    memset(self, 0, sizeof(XBoxLayout));
    XBoxLayout_init(self, direction);
    Set_Class_IsHeap(self, true);
    if (parent)
        XWidget_setLayout(parent, (XLayout*)self);
    return self;
}

XHBoxLayout* XHBoxLayout_create(XWidget* parent)
{
    return XBoxLayout_createInternal(XBoxLayoutDirection_LeftToRight, parent);
}

XVBoxLayout* XVBoxLayout_create(XWidget* parent)
{
    return XBoxLayout_createInternal(XBoxLayoutDirection_TopToBottom, parent);
}

XBoxLayout* XBoxLayout_create(XBoxLayoutDirection direction, XWidget* parent)
{
    return XBoxLayout_createInternal(direction, parent);
}

/* ==================== 方向访问 ==================== */

void XBoxLayout_setDirection(XBoxLayout* self, XBoxLayoutDirection direction)
{
    int i;
    bool oldHoriz;
    bool newHoriz;
    if (!self) return;
    if ((int)self->m_direction == (int)direction) return;
    oldHoriz = XBoxLayout_isHorizontal(self);
    newHoriz = (direction == XBoxLayoutDirection_LeftToRight ||
                direction == XBoxLayoutDirection_RightToLeft);
    /* 只在水平/垂直互转时翻转 magic 空白（对标 Qt setDirection）。 */
    if (oldHoriz != newHoriz) {
        for (i = 0; i < (int)self->m_base.m_itemCount; ++i) {
            XLayoutItem* item;
            XSpacerItem* sp;
            item = self->m_base.m_items[i];
            if (!item) continue;
            sp = XLayoutItem_spacerItem_base(item);
            if (!sp || !sp->m_isMagic) continue;
            if (!(sp->m_hPolicy & 0x02u) && !(sp->m_vPolicy & 0x02u)) {
                /* 固定空白/strut：交换横纵尺寸并把 Fixed 转到新主轴。 */
                XSize tmp;
                XSize_init(&tmp, sp->m_size.height, sp->m_size.width);
                sp->m_size = tmp;
                sp->m_hPolicy = newHoriz
                                    ? (uint8_t)XWidgetSizePolicy_Fixed
                                    : (uint8_t)XWidgetSizePolicy_Minimum;
                sp->m_vPolicy = newHoriz
                                    ? (uint8_t)XWidgetSizePolicy_Minimum
                                    : (uint8_t)XWidgetSizePolicy_Fixed;
            } else {
                /* 可伸展空白：清空尺寸并把 Expanding 转到新主轴。 */
                XSize_init(&sp->m_size, 0, 0);
                sp->m_hPolicy = newHoriz
                                    ? (uint8_t)XWidgetSizePolicy_Expanding
                                    : (uint8_t)XWidgetSizePolicy_Minimum;
                sp->m_vPolicy = newHoriz
                                    ? (uint8_t)XWidgetSizePolicy_Minimum
                                    : (uint8_t)XWidgetSizePolicy_Expanding;
            }
        }
    }
    self->m_direction = direction;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

XBoxLayoutDirection XBoxLayout_direction(const XBoxLayout* self)
{
    if (!self) return XBoxLayoutDirection_LeftToRight;
    return self->m_direction;
}

/* ==================== 追加条目（对标 QBoxLayout add* 系列） ==================== */

void XBoxLayout_addWidget(XBoxLayout* self, XWidget* widget)
{
    if (!self) return;
    XBoxLayout_insertWidget(self, -1, widget, 0);
}

void XBoxLayout_addLayout(XBoxLayout* self, XLayout* child)
{
    if (!self) return;
    XBoxLayout_insertLayout(self, -1, child, 0);
}

void XBoxLayout_addSpacing(XBoxLayout* self, int size)
{
    if (!self) return;
    XBoxLayout_insertSpacing(self, -1, size);
}

void XBoxLayout_addStretch(XBoxLayout* self, int stretch)
{
    if (!self) return;
    XBoxLayout_insertStretch(self, -1, stretch);
}

void XBoxLayout_addItem(XBoxLayout* self, XLayoutItem* item)
{
    if (!self) return;
    XBoxLayout_insertItem(self, -1, item, 0);
}

/* ==================== 插入条目（对标 QBoxLayout insert* 系列） ==================== */

void XBoxLayout_insertWidget(XBoxLayout* self, int index, XWidget* widget,
                             int stretch)
{
    XLayoutItem* item;
    int idx;
    if (!self || !widget) return;
    item = XLayoutItem_createWidgetItem(widget);
    if (!item) return;
    idx = XLayout_insertItemAt((XLayout*)self, index, item, true);
    if (idx >= 0) {
        XBoxLayout_insertStretchSlot(self, idx, stretch);
        XLayoutItem_invalidate_base((XLayoutItem*)self);
    } else {
        XLayoutItem_delete_base(item);
    }
}

void XBoxLayout_insertLayout(XBoxLayout* self, int index, XLayout* child,
                             int stretch)
{
    int idx;
    XLayout* oldParent;
    if (!self || !child) return;
    oldParent = child->m_parentLayout;
    idx = XLayout_insertItemAt((XLayout*)self, index,
                               (XLayoutItem*)child, false);
    if (idx >= 0) {
        XBoxLayout_insertStretchSlot(self, idx, stretch);
        (void)oldParent;
        XLayoutItem_invalidate_base((XLayoutItem*)self);
    }
}

void XBoxLayout_insertSpacing(XBoxLayout* self, int index, int size)
{
    XSize s;
    XLayoutItem* item;
    int idx;
    if (!self) return;
    if (size < 0) size = 0;
    if (XBoxLayout_isHorizontal(self))
        XSize_init(&s, size, 0);
    else
        XSize_init(&s, 0, size);
    if (XBoxLayout_isHorizontal(self))
        item = XLayoutItem_createSpacerItem(&s,
                                            XWidgetSizePolicy_Fixed,
                                            XWidgetSizePolicy_Minimum);
    else
        item = XLayoutItem_createSpacerItem(&s,
                                            XWidgetSizePolicy_Minimum,
                                            XWidgetSizePolicy_Fixed);
    if (!item) return;
    XBoxLayout_markMagic(item);
    idx = XLayout_insertItemAt((XLayout*)self, index, item, true);
    if (idx >= 0) {
        XBoxLayout_insertStretchSlot(self, idx, 0);
        XLayoutItem_invalidate_base((XLayoutItem*)self);
    } else {
        XLayoutItem_delete_base(item);
    }
}

void XBoxLayout_insertStretch(XBoxLayout* self, int index, int stretch)
{
    XSize s;
    XLayoutItem* item;
    int idx;
    if (!self) return;
    XSize_init(&s, 0, 0);
    if (XBoxLayout_isHorizontal(self))
        item = XLayoutItem_createSpacerItem(&s,
                                            XWidgetSizePolicy_Expanding,
                                            XWidgetSizePolicy_Minimum);
    else
        item = XLayoutItem_createSpacerItem(&s,
                                            XWidgetSizePolicy_Minimum,
                                            XWidgetSizePolicy_Expanding);
    if (!item) return;
    XBoxLayout_markMagic(item);
    idx = XLayout_insertItemAt((XLayout*)self, index, item, true);
    if (idx >= 0) {
        XBoxLayout_insertStretchSlot(self, idx, stretch);
        XLayoutItem_invalidate_base((XLayoutItem*)self);
    } else {
        XLayoutItem_delete_base(item);
    }
}

void XBoxLayout_insertItem(XBoxLayout* self, int index, XLayoutItem* item,
                           int stretch)
{
    int idx;
    if (!self || !item) return;
    idx = XLayout_insertItemAt((XLayout*)self, index, item, false);
    if (idx >= 0) {
        XBoxLayout_insertStretchSlot(self, idx, stretch);
        XLayoutItem_invalidate_base((XLayoutItem*)self);
    }
}

/* ==================== 伸展因子（对标 QBoxLayout） ==================== */

void XBoxLayout_setStretch(XBoxLayout* self, int index, int stretch)
{
    if (!self) return;
    if (index < 0 || index >= (int)self->m_base.m_itemCount) return;
    if (index >= self->m_stretchCapacity) {
        if (!XBoxLayout_growStretches(self, index + 1)) return;
    }
    self->m_stretches[index] = stretch < 0 ? 0 : stretch;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

int XBoxLayout_stretch(const XBoxLayout* self, int index)
{
    if (!self) return 0;
    if (index < 0 || index >= (int)self->m_base.m_itemCount) return 0;
    if (!self->m_stretches || index >= self->m_stretchCapacity) return 0;
    return self->m_stretches[index];
}

bool XBoxLayout_setStretchFactorWidget(XBoxLayout* self, XWidget* widget,
                                       int stretch)
{
    int i;
    if (!self || !widget) return false;
    for (i = 0; i < (int)self->m_base.m_itemCount; ++i) {
        if (XLayoutItem_widget_base(self->m_base.m_items[i]) == widget) {
            XBoxLayout_setStretch(self, i, stretch);
            return true;
        }
    }
    return false;
}

bool XBoxLayout_setStretchFactorLayout(XBoxLayout* self, XLayout* child,
                                       int stretch)
{
    int i;
    if (!self || !child) return false;
    for (i = 0; i < (int)self->m_base.m_itemCount; ++i) {
        if (XLayoutItem_layout_base(self->m_base.m_items[i]) == child) {
            XBoxLayout_setStretch(self, i, stretch);
            return true;
        }
    }
    return false;
}

/* ==================== 对齐/伸展重载（对标 QBoxLayout） ==================== */

/** @brief 在指定索引插入控件并带伸展因子与对齐（实现共同路径）。 */
void XBoxLayout_insertWidgetEx(XBoxLayout* self, int index, XWidget* widget,
                               int stretch, XLayoutAlignments alignment)
{
    XLayoutItem* item;
    int idx;
    if (!self || !widget) return;
    item = XLayoutItem_createWidgetItem(widget);
    if (!item) return;
    if (alignment != 0)
        XLayoutItem_setAlignment(item, alignment);
    idx = XLayout_insertItemAt((XLayout*)self, index, item, true);
    if (idx >= 0) {
        XBoxLayout_insertStretchSlot(self, idx, stretch);
        XLayoutItem_invalidate_base((XLayoutItem*)self);
    } else {
        XLayoutItem_delete_base(item);
    }
}

/** @brief 追加控件并带伸展因子与对齐（对标 QBoxLayout::addWidget 重载）。
 * @details 伸展因子只作用于主轴方向；对齐标志作用于交叉轴，0=填满。 */
void XBoxLayout_addWidgetEx(XBoxLayout* self, XWidget* widget, int stretch,
                            XLayoutAlignments alignment)
{
    XBoxLayout_insertWidgetEx(self, -1, widget, stretch, alignment);
}

/** @brief 追加子布局并带伸展因子（对标 QBoxLayout::addLayout 重载）。 */
void XBoxLayout_addLayoutEx(XBoxLayout* self, XLayout* child, int stretch)
{
    XBoxLayout_insertLayout(self, -1, child, stretch);
}

/** @brief 限制交叉轴最小尺寸（对标 QBoxLayout::addStrut）。
 * @details 水平盒强制交叉轴（高度）不小于 size，垂直盒强制交叉轴
 *          （宽度）不小于 size；创建内部 magic 固定空白（归布局所有）。 */
void XBoxLayout_addStrut(XBoxLayout* self, int size)
{
    XSize s;
    XLayoutItem* item;
    int idx;
    bool horizontal;
    if (!self) return;
    if (size < 0) size = 0;
    horizontal = XBoxLayout_isHorizontal(self);
    if (horizontal)
        XSize_init(&s, 0, size);
    else
        XSize_init(&s, size, 0);
    item = XLayoutItem_createSpacerItem(&s,
                                        horizontal
                                            ? XWidgetSizePolicy_Fixed
                                            : XWidgetSizePolicy_Minimum,
                                        horizontal
                                            ? XWidgetSizePolicy_Minimum
                                            : XWidgetSizePolicy_Fixed);
    if (!item) return;
    XBoxLayout_markMagic(item);
    idx = XLayout_insertItemAt((XLayout*)self, -1, item, true);
    if (idx >= 0) {
        XBoxLayout_insertStretchSlot(self, idx, 0);
        XLayoutItem_invalidate_base((XLayoutItem*)self);
    } else {
        XLayoutItem_delete_base(item);
    }
}

#if XLAYOUT_SPACER_ON

/** @brief 在指定索引插入手动创建的空白条目（对标 QBoxLayout::
 *  insertSpacerItem）。空白对象所有权转移给布局并按 Qt 语义标记为
 *  magic 空白。 */
void XBoxLayout_insertSpacerItem(XBoxLayout* self, int index, XSpacerItem* item)
{
    int idx;
    if (!self || !item) return;
    idx = XLayout_insertItemAt((XLayout*)self, index, (XLayoutItem*)item, true);
    if (idx >= 0) {
        item->m_isMagic = 1;
        XBoxLayout_insertStretchSlot(self, idx, 0);
        XLayoutItem_invalidate_base((XLayoutItem*)self);
    }
}

/** @brief 追加手动创建的空白条目（对标 QBoxLayout::addSpacerItem）。 */
void XBoxLayout_addSpacerItem(XBoxLayout* self, XSpacerItem* item)
{
    XBoxLayout_insertSpacerItem(self, -1, item);
}

#endif /* XLAYOUT_SPACER_ON */

#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
