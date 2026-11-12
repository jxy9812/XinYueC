/**
 * @file       XModelIndexSelectionAlignTest.c
 * @brief      XModelIndex / XItemSelection 值对象 × Qt 6.8 对齐契约测试。
 * @details    对齐依据：
 *             - Qt 6.8 qabstractitemmodel.h（QModelIndex 值语义：row/col/
 *               internalId/model 四字段；isValid = r>=0 && c>=0 && m!=NULL；
 *               parent()/sibling()/data()/flags()/multiData() 经 m 委托）；
 *             - Qt 6.8 qitemselectionmodel.h / .cpp（QItemSelectionRange 的
 *               intersects/intersected/contains/indexes；QItemSelection 的
 *               select/contains/indexes/merge/split 代数）；
 *             - Qt 6.8 qabstractitemmodel.cpp（itemData 逐 role 遍历、
 *               mimeTypes 缺省 application/x-qabstractitemmodeldatalist、
 *               supportedDropActions 缺省 CopyAction、roleNames 默认集合、
 *               树形 index/parent/rowCount(parent)/hasChildren 重载口径）。
 *
 *             覆盖：XModelIndex 值语义与 NULL 守卫、parent/sibling 委托、
 *             data/flags/multiData 经模型委托；XItemSelectionRange 的几何
 *             代数（isValid/isEmpty/contains/intersects/intersected/equal/
 *             indexes）；XItemSelection 的 select/merge/split/contains；
 *             XAbstractItemModel §B2 树形层级（parentIndex/indexTree/
 *             rowCountAt/hasChildrenAtIndex/appendTreeNode/removeTreeNode）
 *             与多角色/MIME 族（itemData/setItemData/clearItemData/
 *             multiData/mimeTypes/mimeData/canDropMimeData/dropMimeData/
 *             supportedDropActions/supportedDragActions/roleNames）。
 * @note       C99；所有新建对象显式 XClassDelete/XItemSelection_deinit，
 *             漏删即运行器判 [LEAK]。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XAbstractItemModel.h"
#include "XModelIndex.h"
#include "XItemSelection.h"
#include "XItemSelectionModel.h"
#include "XAbstractItemView.h"
#include "XPlatformDrag.h"
#include "XObject.h"

/* ==================== XModelIndex 值语义 ==================== */

XQ_TEST(XModelIndex, valueSemantics)
{
    XModelIndex a = XModelIndex_make(2, 3, 7, NULL);
    XQ_EQ_I64(XModelIndex_row(&a), 2);
    XQ_EQ_I64(XModelIndex_column(&a), 3);
    XQ_EQ_I64((long long)XModelIndex_internalId(&a), 7);
    XQ_EQ_I64((long long)(uintptr_t)XModelIndex_internalPointer(&a), 7);
    XQ_EQ_I64((long long)(uintptr_t)XModelIndex_constInternalPointer(&a), 7);
    XQ_CHECK(XModelIndex_model(&a) == NULL);

    /* isValid：r>=0 && c>=0 && m!=NULL（internalId 不参与，对齐 Qt）。 */
    XQ_CHECK(!XModelIndex_isValid(&a));                       /* m == NULL */
    XModelIndex b = XModelIndex_make(-1, -1, 0, NULL);
    XQ_CHECK(!XModelIndex_isValid(&b));                       /* 默认无效 */
    XQ_CHECK(XModelIndex_equal(&b, &b));
    XQ_CHECK(!XModelIndex_equal(&a, &b));

    /* NULL 守卫安全。 */
    XQ_EQ_I64(XModelIndex_row(NULL), -1);
    XQ_EQ_I64(XModelIndex_column(NULL), -1);
    XQ_EQ_I64((long long)XModelIndex_internalId(NULL), 0);
    XQ_CHECK(XModelIndex_model(NULL) == NULL);
    XQ_CHECK(!XModelIndex_isValid(NULL));

    /* compareThreeWay：r→c→i→m 次序。 */
    XModelIndex c1 = XModelIndex_make(0, 0, 0, (const XAbstractItemModel*)1);
    XModelIndex c2 = XModelIndex_make(0, 1, 0, (const XAbstractItemModel*)1);
    XQ_EQ_I64(XModelIndex_compare(&c1, &c2), -1);
    XQ_EQ_I64(XModelIndex_compare(&c2, &c1), 1);
    XQ_EQ_I64(XModelIndex_compare(&c1, &c1), 0);
}

/* ==================== XModelIndex 经模型委托 ==================== */

XQ_TEST(XModelIndex, delegatesToModel)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    XAbstractItemModel_setDimension(m, 3, 2);
    XQ_CHECK(XAbstractItemModel_setData_2(m, 1, 0, "hello"));

    XModelIndex idx = XModelIndex_make(1, 0, 0, (const XAbstractItemModel*)m);
    /* data(DisplayRole) 经 m 委托。 */
    XQ_EQ_STR(XString_toUtf8(XModelIndex_data(&idx, XItemDataRole_DisplayRole)),
              "hello");
    /* flags 经 m 委托（基类 Selectable|Enabled）。 */
    XQ_EQ_I64(XModelIndex_flags(&idx),
              XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled);
    /* multiData：有效索引填 m_data，非法 m 填 NULL。 */
    XModelRoleData span[2];
    span[0].m_role = XItemDataRole_DisplayRole;
    span[1].m_role = XItemDataRole_DisplayRole;
    XModelIndex_multiData(&idx, span, 2);
    XQ_CHECK(span[0].m_data != NULL &&
             strcmp(XString_toUtf8(span[0].m_data), "hello") == 0);
    /* 无效索引 multiData 全部 NULL。 */
    XModelIndex bad = XModelIndex_make(-1, -1, 0, NULL);
    XModelRoleData span2[1];
    span2[0].m_role = XItemDataRole_DisplayRole;
    XModelIndex_multiData(&bad, span2, 1);
    XQ_CHECK(span2[0].m_data == NULL);

    /* sibling/siblingAtColumn/siblingAtRow 委托（Qt qabstractitemmodel.h:508-515）：
       换 row/column 后**同行同父**语义保留，列/行按参数替换。 */
    {
        XModelIndex s = XModelIndex_sibling(&idx, 2, 1);
        XQ_EQ_I64(XModelIndex_row(&s), 2);
        XQ_EQ_I64(XModelIndex_column(&s), 1);
        XQ_CHECK(XModelIndex_model(&s) == (const XAbstractItemModel*)m);
        XQ_EQ_I64((long long)XModelIndex_internalId(&s),
                  (long long)XModelIndex_internalId(&idx));
        XModelIndex sc = XModelIndex_siblingAtColumn(&idx, 1);
        XQ_EQ_I64(XModelIndex_row(&sc), 1);
        XQ_EQ_I64(XModelIndex_column(&sc), 1);
        XModelIndex sr = XModelIndex_siblingAtRow(&idx, 2);
        XQ_EQ_I64(XModelIndex_row(&sr), 2);
        XQ_EQ_I64(XModelIndex_column(&sr), 0);
        /* 无模型 ⇒ 全部退化无效（不产生“野”索引）。 */
        {
            XModelIndex s2 = XModelIndex_sibling(&bad, 0, 0);
            XQ_CHECK(!XModelIndex_isValid(&bad) &&
                     !XModelIndex_isValid(&s2));
        }
    }

    /* parent 委托：平铺模型（无树节点）恒无效，对齐 Qt parent(root)。
       —— 与既有 XAbstractItemModel_parent == -1 的锁同口径。 */
    {
        XModelIndex p = XModelIndex_parent(&idx);
        XQ_CHECK(!XModelIndex_isValid(&p));
        XQ_EQ_I64(XAbstractItemModel_parent(m, 1, 0), -1);
    }

    /* withRowColumn：同父换位（split/intersected 的构造底座）。 */
    {
        XModelIndex w = XModelIndex_withRowColumn(&idx, 2, 1);
        XQ_EQ_I64(XModelIndex_row(&w), 2);
        XQ_EQ_I64(XModelIndex_column(&w), 1);
        XQ_CHECK(XModelIndex_model(&w) == (const XAbstractItemModel*)m);
    }

    XClassDelete((XClass*)m);
}

/* ==================== XItemSelectionRange 几何代数 ==================== */

XQ_TEST(XItemSelectionRange, geometry)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    /* isEmpty 的判定要逐格查 flags（qitemselectionmodel.cpp:296-312）；
       空模型（0×0）里所有索引都无效 ⇒ 恒判空。故先给出 8×8 维度。 */
    XAbstractItemModel_setDimension(m, 8, 8);
    XModelIndex tl = XModelIndex_make(1, 1, 0, (const XAbstractItemModel*)m);
    XModelIndex br = XModelIndex_make(3, 4, 0, (const XAbstractItemModel*)m);
    XItemSelectionRange r = XItemSelectionRange_make(tl, br);

    XQ_EQ_I64(XItemSelectionRange_top(&r), 1);
    XQ_EQ_I64(XItemSelectionRange_left(&r), 1);
    XQ_EQ_I64(XItemSelectionRange_bottom(&r), 3);
    XQ_EQ_I64(XItemSelectionRange_right(&r), 4);
    XQ_EQ_I64(XItemSelectionRange_width(&r), 4);   /* left..right 含端点 */
    XQ_EQ_I64(XItemSelectionRange_height(&r), 3);
    /* parent() = tl.parent()（Qt 6.8 原文 qitemselectionmodel.h:40）——
     * tl 是顶层索引 ⇒ 范围 parent 是**无效索引**（m_model 为 NULL）；
     * 模型经 model() 取。 */
    {
        XModelIndex par = XItemSelectionRange_parent(&r);
        XQ_CHECK(!XModelIndex_isValid(&par));
    }
    XQ_CHECK(XItemSelectionRange_model(&r) == (const XAbstractItemModel*)m);
    XQ_CHECK(XItemSelectionRange_isValid(&r));
    /* isEmpty：有效范围内条目均可选可用 ⇒ false；无效范围 ⇒ true
       （qitemselectionmodel.cpp:296-312）。 */
    XQ_CHECK(!XItemSelectionRange_isEmpty(&r));
    {
        XItemSelectionRange empty;
        empty.m_topLeft = XModelIndex_invalid();
        empty.m_bottomRight = XModelIndex_invalid();
        XQ_CHECK(XItemSelectionRange_isEmpty(&empty));
    }

    /* 1×1 范围（fromIndex）。 */
    XItemSelectionRange one =
        XItemSelectionRange_fromIndex(XModelIndex_make(
            0, 0, 0, (const XAbstractItemModel*)m));
    XQ_EQ_I64(XItemSelectionRange_width(&one), 1);
    XQ_EQ_I64(XItemSelectionRange_height(&one), 1);
    XQ_CHECK(XItemSelectionRange_isValid(&one));

    /* 倒序角标（top>bottom 或 left>right）⇒ 无效。 */
    XItemSelectionRange bad = XItemSelectionRange_make(
        XModelIndex_make(3, 1, 0, (const XAbstractItemModel*)m),
        XModelIndex_make(1, 4, 0, (const XAbstractItemModel*)m));
    XQ_CHECK(!XItemSelectionRange_isValid(&bad));

    /* contains / containsCell。 */
    XModelIndex mid = XModelIndex_make(2, 2, 0, (const XAbstractItemModel*)m);
    XQ_CHECK(XItemSelectionRange_contains(&r, mid));
    XQ_CHECK(!XItemSelectionRange_contains(&r, XModelIndex_make(
                 0, 0, 0, (const XAbstractItemModel*)m)));
    /* containsCell 的第四参是「格的父索引」，必须与范围 parent() 相等
     * （Qt 6.8 原文 qitemselectionmodel.h:52 `parent() == parentIndex`）。
     * 顶层范围的父无效：传 NULL（按无效处理）⇒ 同为无效父 ⇒ 命中；
     * 传有效索引 tl/br 都 ≠ 无效父 ⇒ 不命中。 */
    XQ_CHECK(XItemSelectionRange_containsCell(&r, 2, 2,
             NULL));  /* 同父：范围父与格父同为无效。 */
    XQ_CHECK(!XItemSelectionRange_containsCell(&r, 2, 2,
             &tl));  /* tl 是有效索引，非范围的（无效）父。 */
    XQ_CHECK(!XItemSelectionRange_containsCell(&r, 2, 2,
             &br));  /* 异父（br 非父）。 */

    /* indexes 枚举（行主序，含端点）。 */
    XModelIndex buf[16];
    int total = XItemSelectionRange_indexes(&r, buf, 16);
    XQ_EQ_I64(total, 12);  /* 3 行 × 4 列 */
    XQ_EQ_I64(XModelIndex_row(&buf[0]), 1);
    XQ_EQ_I64(XModelIndex_column(&buf[0]), 1);
    XQ_EQ_I64(XModelIndex_row(&buf[11]), 3);
    XQ_EQ_I64(XModelIndex_column(&buf[11]), 4);

    XClassDelete((XClass*)m);
}

XQ_TEST(XItemSelectionRange, intersect)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    const XAbstractItemModel* cm = (const XAbstractItemModel*)m;
    XItemSelectionRange a = XItemSelectionRange_make(
        XModelIndex_make(0, 0, 0, cm), XModelIndex_make(4, 4, 0, cm));
    XItemSelectionRange b = XItemSelectionRange_make(
        XModelIndex_make(2, 2, 0, cm), XModelIndex_make(6, 6, 0, cm));
    XItemSelectionRange c = XItemSelectionRange_make(
        XModelIndex_make(5, 5, 0, cm), XModelIndex_make(7, 7, 0, cm));

    XQ_CHECK(XItemSelectionRange_intersects(&a, &b));
    XQ_CHECK(!XItemSelectionRange_intersects(&a, &c));

    XItemSelectionRange inter = XItemSelectionRange_intersected(&a, &b);
    XQ_CHECK(XItemSelectionRange_isValid(&inter));
    XQ_EQ_I64(XItemSelectionRange_top(&inter), 2);
    XQ_EQ_I64(XItemSelectionRange_left(&inter), 2);
    XQ_EQ_I64(XItemSelectionRange_bottom(&inter), 4);
    XQ_EQ_I64(XItemSelectionRange_right(&inter), 4);

    /* 不相交 ⇒ 无效范围。 */
    XItemSelectionRange none = XItemSelectionRange_intersected(&a, &c);
    XQ_CHECK(!XItemSelectionRange_isValid(&none));

    XClassDelete((XClass*)m);
}

XQ_TEST(XItemSelectionRange, split)
{
    /* split(range, other)：把 range 中扣掉 other 的矩形碎片（上/下/左/右）
       追加到 result（对齐 Qt QItemSelection::split）。 */
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    /* 2026-11-15 返工补全夹具：indexesFromRange 的 flags 门禁
       （qitemselectionmodel.cpp:263-278 + qabstractitemmodel.cpp:2457-2464
       越界格 NoItemFlags）要求范围格真实落在模型维度内（Qt 的 range 一律
       由 model->index() 的有效索引构造）；无维度则 16 格全被过滤。 */
    XAbstractItemModel_setDimension(m, 8, 8);
    const XAbstractItemModel* cm = (const XAbstractItemModel*)m;
    XItemSelectionRange whole = XItemSelectionRange_make(
        XModelIndex_make(0, 0, 0, cm), XModelIndex_make(4, 4, 0, cm));
    /* other 是中心 2×2（行 1..3，列 1..3）。 */
    XItemSelectionRange other = XItemSelectionRange_make(
        XModelIndex_make(1, 1, 0, cm), XModelIndex_make(3, 3, 0, cm));
    XItemSelection result;
    XQ_CHECK(XItemSelection_init(&result));

    XItemSelection_split(whole, other, &result);
    /* 期望 4 块：上(行0)、下(行4)、左(列0)、右(列4)，共 4 个不相交碎片。 */
    XQ_EQ_I64(XItemSelection_count(&result), 4);
    int total = XItemSelection_indexes(&result, NULL, 0);
    XQ_EQ_I64(total, 5 + 5 + 3 + 3);  /* 行0(5)+行4(5)+列0(去角3)+列4(去角3) */

    XItemSelection_deinit(&result);
    XClassDelete((XClass*)m);
}

/* ==================== XItemSelection 代数 ==================== */

XQ_TEST(XItemSelection, selectAndContains)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    /* 夹具补全同 split 用例：contains 的 flags 门禁要求格在维度内。 */
    XAbstractItemModel_setDimension(m, 8, 8);
    const XAbstractItemModel* cm = (const XAbstractItemModel*)m;
    XItemSelection sel;
    XQ_CHECK(XItemSelection_init(&sel));

    XQ_CHECK(XItemSelection_isEmpty(&sel));
    XQ_EQ_I64(XItemSelection_count(&sel), 0);

    /* select 追加矩形范围（topLeft 在 bottomRight 之下/右时归一四边）。 */
    XQ_CHECK(XItemSelection_select(&sel,
        XModelIndex_make(0, 0, 0, cm),
        XModelIndex_make(1, 1, 0, cm)));
    XQ_EQ_I64(XItemSelection_count(&sel), 1);
    XQ_CHECK(!XItemSelection_isEmpty(&sel));

    /* contains：任一范围命中即 true。 */
    XQ_CHECK(XItemSelection_contains(&sel,
        XModelIndex_make(0, 1, 0, cm)));
    XQ_CHECK(!XItemSelection_contains(&sel,
        XModelIndex_make(2, 2, 0, cm)));

    /* at 返回值副本。 */
    XItemSelectionRange r0 = XItemSelection_at(&sel, 0);
    XQ_EQ_I64(XItemSelectionRange_top(&r0), 0);
    XQ_EQ_I64(XItemSelectionRange_bottom(&r0), 1);

    /* removeAt / clear。 */
    XItemSelection_removeAt(&sel, 0);
    XQ_EQ_I64(XItemSelection_count(&sel), 0);
    XItemSelection_select(&sel, XModelIndex_make(0, 0, 0, cm),
                          XModelIndex_make(1, 1, 0, cm));
    XItemSelection_clear(&sel);
    XQ_CHECK(XItemSelection_isEmpty(&sel));

    XItemSelection_deinit(&sel);
    XClassDelete((XClass*)m);
}

XQ_TEST(XItemSelection, merge)
{
    /* merge(self, other, Select)：把 other 与 self 相交部分切成不相交碎片
       后并入 self（对齐 Qt QItemSelection::merge）。Qt 不把矩形合并成单个
       大矩形，而是保持不相交的碎矩形集合——故并入后应为若干不相交矩形，
       并集索引总数等于两集合并（无重复）。 */
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    /* 夹具补全同 split 用例：merge 的并集统计走 indexes() 的 flags 门禁。 */
    XAbstractItemModel_setDimension(m, 8, 8);
    const XAbstractItemModel* cm = (const XAbstractItemModel*)m;
    XItemSelection a;
    XItemSelection b;
    XQ_CHECK(XItemSelection_init(&a));
    XQ_CHECK(XItemSelection_init(&b));

    /* a = [0,0]-[3,3]（16 格），b = [2,2]-[5,5]（16 格），交 [2,2]-[3,3]（4 格）。
       并集 = 16+16-4 = 28 格。 */
    XItemSelection_select(&a, XModelIndex_make(0, 0, 0, cm),
                          XModelIndex_make(3, 3, 0, cm));
    XItemSelection_select(&b, XModelIndex_make(2, 2, 0, cm),
                          XModelIndex_make(5, 5, 0, cm));
    XQ_CHECK(XItemSelection_merge(&a, &b, XItemSelectionModelSelectionFlag_Select));
    XQ_CHECK(XItemSelection_count(&a) > 0);
    /* 并集索引总数 = 28（碎片不相交，无重复）。 */
    XQ_EQ_I64(XItemSelection_indexes(&a, NULL, 0), 28);
    /* 并集边界点均被覆盖。 */
    XQ_CHECK(XItemSelection_contains(&a, XModelIndex_make(0, 0, 0, cm)));
    XQ_CHECK(XItemSelection_contains(&a, XModelIndex_make(3, 3, 0, cm)));
    XQ_CHECK(XItemSelection_contains(&a, XModelIndex_make(5, 5, 0, cm)));
    XQ_CHECK(XItemSelection_contains(&a, XModelIndex_make(2, 2, 0, cm)));
    /* 两集合外的点不被覆盖。 */
    XQ_CHECK(!XItemSelection_contains(&a, XModelIndex_make(5, 0, 0, cm)));

    /* Deselect：从 self 中扣掉 other 区域（切碎片后不并入）。 */
    XItemSelection c;
    XQ_CHECK(XItemSelection_init(&c));
    XItemSelection_select(&c, XModelIndex_make(0, 0, 0, cm),
                          XModelIndex_make(4, 4, 0, cm));   /* 25 格 */
    XItemSelection d;
    XQ_CHECK(XItemSelection_init(&d));
    XItemSelection_select(&d, XModelIndex_make(2, 2, 0, cm),
                          XModelIndex_make(4, 4, 0, cm));   /* 9 格，全在 c 内 */
    XQ_CHECK(XItemSelection_merge(&c, &d, XItemSelectionModelSelectionFlag_Deselect));
    /* 扣掉右下 [2,2]-[4,4] ⇒ 上(行0-1,5列=10) + 左(行2-4,列0-1=6) = 16 格，
       两碎片（Qt 原文 qitemselectionmodel.cpp:527-555 逐条同口径）。 */
    XQ_EQ_I64(XItemSelection_count(&c), 2);
    int total = XItemSelection_indexes(&c, NULL, 0);
    XQ_EQ_I64(total, 16);
    XQ_CHECK(XItemSelection_contains(&c, XModelIndex_make(0, 0, 0, cm)));
    XQ_CHECK(!XItemSelection_contains(&c, XModelIndex_make(3, 3, 0, cm)));

    XItemSelection_deinit(&a);
    XItemSelection_deinit(&b);
    XItemSelection_deinit(&c);
    XItemSelection_deinit(&d);
    XClassDelete((XClass*)m);
}

/* ==================== 派生模型（flags 门禁回归锁，2026-11-15 返工） ==================== */

XCLASS_DEFINE_BEGING(XQGateModel)
XCLASS_DEFINE_EXTEND_END(XQGateModel, XAbstractItemModel)

/** @brief 最小派生模型：把 (0,0) 格 flags 置 NoItemFlags（不可选不可用）。 */
typedef struct XQGateModel
{
    XAbstractItemModel m_base;
} XQGateModel;

static int xqGateModel_flags(const XAbstractItemModel* self, int row, int col)
{
    /* 槽内回落父类口径走 XClass_Parent（同 XAbstractItemModelAlignTest.c
       的 XQEditableModel 写法）。 */
    int base = XClass_Parent(XAbstractItemModel, EXAbstractItemModel_Flags,
                             int (*)(const XAbstractItemModel*, int, int))
                   (self, row, col);
    if (base == XItemFlag_NoItemFlags) return base;
    /* (0,0) 禁选：Qt isSelectableAndEnabled（qitemselectionmodel.cpp:257）
       同时要求 Selectable|Enabled，缺一即被门禁拦下。 */
    if (row == 0 && col == 0) return XItemFlag_NoItemFlags;
    return base;
}

/* 命名必须与 XClassSetVtable(XQGateModel) 展开的符号逐字一致。 */
static XVtable* XQGateModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQGateModel)
    XVTABLE_INHERIT_XCLASS(XAbstractItemModel);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_Flags, xqGateModel_flags);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化派生模型（栈对象，2x2；析构用 XClassDeinit）。 */
static void xqGateModel_init(XQGateModel* self)
{
    if (!self) return;
    XAbstractItemModel_init(&self->m_base);
    XClassSetVtable(self, XQGateModel);
    XAbstractItemModel_setDimension(&self->m_base, 2, 2);
}

XQ_TEST(XItemSelectionRange, flagsGate)
{
    /* Qt 6.8 三处 flags 门禁（2026-11-15 返工锁）：
       ① QItemSelection::contains 以 isSelectableAndEnabled(index.flags())
          短路整次查找（qitemselectionmodel.cpp:415-417）；
       ② indexesFromRange 只枚举 Selectable|Enabled 的格（:263-278）；
       ③ isEmpty 逐格同判据（:296-312）。 */
    XQGateModel g;
    const XAbstractItemModel* cm;
    XModelIndex idx00;
    XItemSelectionRange whole;
    XItemSelectionRange only00;
    XItemSelection sel;
    XModelIndex buf[4];
    int total;
    xqGateModel_init(&g);
    cm = (const XAbstractItemModel*)&g;

    /* flags 委托经虚槽分派：派生模型禁选格拿到 NoItemFlags。 */
    idx00 = XModelIndex_make(0, 0, 0, cm);
    XQ_EQ_I64(XModelIndex_flags(&idx00), XItemFlag_NoItemFlags);

    /* ① contains：几何上在已选范围内，但 (0,0) 禁选 ⇒ false；
       相邻可选格 (0,1) ⇒ true。 */
    XQ_CHECK(XItemSelection_init(&sel));
    XQ_CHECK(XItemSelection_select(&sel, XModelIndex_make(0, 0, 0, cm),
                                   XModelIndex_make(1, 1, 0, cm)));
    XQ_CHECK(!XItemSelection_contains(&sel, idx00));
    XQ_CHECK(XItemSelection_contains(&sel, XModelIndex_make(0, 1, 0, cm)));

    /* ② indexes：4 格范围只枚举 3 格（(0,0) 被过滤），行主序保持。 */
    whole = XItemSelectionRange_make(XModelIndex_make(0, 0, 0, cm),
                                     XModelIndex_make(1, 1, 0, cm));
    total = XItemSelectionRange_indexes(&whole, buf, 4);
    XQ_EQ_I64(total, 3);
    XQ_EQ_I64(XModelIndex_row(&buf[0]), 0);
    XQ_EQ_I64(XModelIndex_column(&buf[0]), 1);
    XQ_EQ_I64(XModelIndex_row(&buf[2]), 1);
    XQ_EQ_I64(XModelIndex_column(&buf[2]), 1);

    /* ③ isEmpty：只盖 (0,0) ⇒ true；只盖 (0,1) ⇒ false。 */
    only00 = XItemSelectionRange_fromIndex(XModelIndex_make(0, 0, 0, cm));
    XQ_CHECK(XItemSelectionRange_isEmpty(&only00));
    {
        XItemSelectionRange only01 = XItemSelectionRange_fromIndex(
            XModelIndex_make(0, 1, 0, cm));
        XQ_CHECK(!XItemSelectionRange_isEmpty(&only01));
    }

    XItemSelection_deinit(&sel);
    XClassDeinit((XClass*)&g);
}

/* ==================== XAbstractItemModel §B2 树形层级 ==================== */

XQ_TEST(XAbstractItemModel, treeHierarchy)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    XAbstractItemModel_setDimension(m, 2, 2);

    /* 平铺模型（无树节点）：indexTree/parentIndex 恒无效，根行数 = m_rows。 */
    XModelIndex idx = XAbstractItemModel_indexTree(m, 0, 0, NULL);
    XQ_CHECK(!XModelIndex_isValid(&idx));
    XQ_EQ_I64(XAbstractItemModel_rowCountAt(m, NULL), 2);
    XQ_EQ_I64(XAbstractItemModel_rowCountOfId(m, -1), 2);
    XQ_EQ_I64(XAbstractItemModel_columnCountAt(m, NULL), 2);
    XQ_CHECK(XAbstractItemModel_hasChildrenAtIndex(m, NULL) ==
             (2 > 0 && 2 > 0));

    /* 登记一棵子树：根(0,0) 下挂两子 (0,0)、(1,0)。 */
    int idRoot = XAbstractItemModel_appendTreeNode(m, NULL, 0, 0);
    XQ_CHECK(idRoot >= 0);
    XModelIndex rootIdx = XModelIndex_make(0, 0, (uintptr_t)idRoot, m);
    int idC0 = XAbstractItemModel_appendTreeNode(m, &rootIdx, 0, 0);
    int idC1 = XAbstractItemModel_appendTreeNode(m, &rootIdx, 1, 0);
    XQ_CHECK(idC0 >= 0 && idC1 >= 0);

    /* indexTree 经树节点表解码。 */
    XModelIndex root = XAbstractItemModel_indexTree(m, 0, 0, NULL);
    XQ_CHECK(XModelIndex_isValid(&root));
    XQ_EQ_I64((long long)XModelIndex_internalId(&root), idRoot);
    XModelIndex child0 = XAbstractItemModel_indexTree(
        m, 0, 0, &root);  /* parent = root */
    XQ_CHECK(XModelIndex_isValid(&child0));
    XQ_EQ_I64((long long)XModelIndex_internalId(&child0), idC0);

    /* parentIndex 解码真实父级（对标 QModelIndex::parent）。 */
    XModelIndex p0 = XAbstractItemModel_parentIndex(m, &child0);
    XQ_CHECK(XModelIndex_isValid(&p0));
    XQ_EQ_I64((long long)XModelIndex_internalId(&p0), idRoot);
    /* 根的父 = 无效（root 的 parent 是根级 ⇒ 无效索引）。 */
    XModelIndex pRoot = XAbstractItemModel_parentIndex(m, &root);
    XQ_CHECK(!XModelIndex_isValid(&pRoot));

    /* rowCountAt/hasChildrenAtIndex 经树节点表。 */
    XQ_EQ_I64(XAbstractItemModel_rowCountAt(m, &root), 2);
    XQ_CHECK(XAbstractItemModel_hasChildrenAtIndex(m, &root));
    XQ_CHECK(!XAbstractItemModel_hasChildrenAtIndex(m, &child0));

    /* 级联删除：删根 ⇒ 两子一并移除。 */
    int removed = XAbstractItemModel_removeTreeNode(m, idRoot);
    XQ_EQ_I64(removed, 3);
    XModelIndex gone = XAbstractItemModel_indexTree(m, 0, 0, &rootIdx);
    XQ_CHECK(!XModelIndex_isValid(&gone));

    XClassDelete((XClass*)m);
}

XQ_TEST(XAbstractItemModel, treeNodeReindex)
{
    /* 删中间节点后表内自洽：internalId 必须与向量下标同步重排，父链不得
       错位；且级联不得误删无关的根级兄弟（m_parentId == -1 的节点）。 */
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    XAbstractItemModel_setDimension(m, 4, 2);

    int idRoot = XAbstractItemModel_appendTreeNode(m, NULL, 0, 0);   /* 0 */
    XModelIndex rootIdx = XModelIndex_make(0, 0, (uintptr_t)idRoot, m);
    int idC0 = XAbstractItemModel_appendTreeNode(m, &rootIdx, 0, 0); /* 1 */
    int idC1 = XAbstractItemModel_appendTreeNode(m, &rootIdx, 1, 0); /* 2 */
    int idOther = XAbstractItemModel_appendTreeNode(m, NULL, 3, 0);  /* 3 无关根级 */
    XQ_CHECK(idRoot == 0 && idC0 == 1 && idC1 == 2 && idOther == 3);

    /* 删 c0（下标 1）⇒ 后续节点 internalId/parentId 各减 1：
       c1 变 1、other 变 2；root 的 parentId 仍 -1。 */
    XQ_EQ_I64(XAbstractItemModel_removeTreeNode(m, idC0), 1);

    /* c1 现应以 (root,row=1) 命中，且 internalId 已重排为 1。 */
    {
        XModelIndex c1 = XAbstractItemModel_indexTree(m, 1, 0, &rootIdx);
        XQ_CHECK(XModelIndex_isValid(&c1));
        XQ_EQ_I64((long long)XModelIndex_internalId(&c1), 1);
        /* 父链仍解到 root（internalId 0），未因重排错位。 */
        XModelIndex pc1 = XAbstractItemModel_parentIndex(m, &c1);
        XQ_CHECK(XModelIndex_isValid(&pc1));
        XQ_EQ_I64((long long)XModelIndex_internalId(&pc1), 0);
        XQ_EQ_I64(XAbstractItemModel_rowCountAt(m, &rootIdx), 1);
    }
    /* 无关根级兄弟 other 必须还在（旧实现会把它当 m_parentId==-1 后代删掉）。 */
    {
        XModelIndex o = XAbstractItemModel_indexTree(m, 3, 0, NULL);
        XQ_CHECK(XModelIndex_isValid(&o));
        XQ_EQ_I64((long long)XModelIndex_internalId(&o), 2);
        {
            XModelIndex po = XAbstractItemModel_parentIndex(m, &o);
            XQ_CHECK(!XModelIndex_isValid(&po));
        }
    }
    /* 删根 ⇒ 仅自身 + c1 消失，other 存活。 */
    XQ_EQ_I64(XAbstractItemModel_removeTreeNode(m, idRoot), 2);
    {
        XModelIndex o = XAbstractItemModel_indexTree(m, 3, 0, NULL);
        XQ_CHECK(XModelIndex_isValid(&o));
        XQ_EQ_I64((long long)XModelIndex_internalId(&o), 0);
    }

    XClassDelete((XClass*)m);
}

/* ==================== XAbstractItemModel §B2 多角色 / MIME ==================== */

XQ_TEST(XAbstractItemModel, dataRolesAndMime)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);
    XAbstractItemModel_setDimension(m, 1, 1);
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 0, "cellA"));

    /* dataRole：仅 Display/Edit 有承载，其余角色返回 NULL。 */
    XQ_EQ_STR(XString_toUtf8(XAbstractItemModel_dataRole(
                  m, 0, 0, XItemDataRole_DisplayRole)),
              "cellA");
    XQ_EQ_STR(XString_toUtf8(XAbstractItemModel_dataRole(
                  m, 0, 0, XItemDataRole_EditRole)),
              "cellA");
    XQ_CHECK(XAbstractItemModel_dataRole(
                 m, 0, 0, XItemDataRole_ToolTipRole) == NULL);

    /* itemData：逐 role < UserRole 遍历，有效者入表（本内存模型含 Display/Edit）。 */
    XMap* id = XAbstractItemModel_itemData(m, 0, 0);
    XQ_CHECK(id != NULL);
    XQ_EQ_I64((long long)XMap_size_base((XMapBase*)id), 2);
    XClassDelete((XClass*)id);

    /* multiData：有效索引填 m_data，无效索引 NULL（由 XModelIndex_multiData 已验）。 */
    XModelRoleData span[1];
    span[0].m_role = XItemDataRole_DisplayRole;
    XAbstractItemModel_multiData(m, 0, 0, span, 1);
    XQ_CHECK(span[0].m_data != NULL &&
             strcmp(XString_toUtf8(span[0].m_data), "cellA") == 0);

    /* setItemData：仅承载角色可写；含非承载角色整体失败。 */
    XMap* good = XAbstractItemModel_itemData(m, 0, 0);
    XQ_CHECK(XAbstractItemModel_setItemData(m, 0, 0, good));
    XClassDelete((XClass*)good);

    XMap* bad = XMap_create(sizeof(int), sizeof(const XString*), Xint_compare);
    int role = XItemDataRole_ToolTipRole;
    XString* tip = XString_create_utf8("tip");
    XMap_insert_base((XMapBase*)bad, &role, &tip);
    XQ_CHECK(!XAbstractItemModel_setItemData(m, 0, 0, bad));  /* 无承载 ⇒ false */
    XClassDelete((XClass*)tip);
    XClassDelete((XClass*)bad);

    /* clearItemData：清单元格文本。 */
    XQ_CHECK(XAbstractItemModel_clearItemData(m, 0, 0));
    XQ_CHECK(XString_toUtf8(XAbstractItemModel_data(m, 0, 0)) == NULL ||
             XString_toUtf8(XAbstractItemModel_data(m, 0, 0))[0] == '\0');

    /* mimeTypes：缺省单元素 application/x-qabstractitemmodeldatalist。 */
    XStringList* types = XAbstractItemModel_mimeTypes(m);
    XQ_CHECK(types != NULL);
    XQ_EQ_I64((long long)XStringList_size_base(types), 1);
    XClassDelete(types);

    /* supportedDropActions 缺省 Copy；supportedDragActions == drop。
     * （2026-11-15 B6R 返工③：两函数提为虚槽，公开入口改 _base 分派——
     * 借用口径/缺省值不变，Qt 侧两函数即 virtual，qabstractitemmodel.h:
     * 294/:298。） */
    XQ_EQ_I64(XAbstractItemModel_supportedDropActions_base(m),
              XPlatformDragAction_Copy);
    XQ_EQ_I64(XAbstractItemModel_supportedDragActions_base(m),
              XAbstractItemModel_supportedDropActions_base(m));

    /* roleNames：逐项对齐 Qt 6.8 缺省集合（qabstractitemmodel.cpp:741-750）：
       恰 6 项 display/decoration/edit/toolTip/statusTip/whatsThis。 */
    {
        static const struct { int role; const char* name; } defs[] = {
            { XItemDataRole_DisplayRole,    "display"    },
            { XItemDataRole_DecorationRole, "decoration" },
            { XItemDataRole_EditRole,       "edit"       },
            { XItemDataRole_ToolTipRole,    "toolTip"    },
            { XItemDataRole_StatusTipRole,  "statusTip"  },
            { XItemDataRole_WhatsThisRole,  "whatsThis"  },
        };
        XMap* rn = XAbstractItemModel_roleNames(m);
        size_t k;
        XQ_CHECK(rn != NULL);
        XQ_EQ_I64((long long)XMap_size_base((XMapBase*)rn), 6);
        for (k = 0; k < sizeof(defs) / sizeof(defs[0]); ++k) {
            const char* const* nm =
                (const char* const*)XMap_value_base((XMapBase*)rn,
                                                  &defs[k].role);
            XQ_CHECK(nm != NULL && *nm != NULL);
            XQ_EQ_STR(nm ? *nm : NULL, defs[k].name);
        }
        XClassDelete((XClass*)rn);
    }

    XClassDelete((XClass*)m);
}

XQ_TEST(XAbstractItemModel, mimeRoundTrip)
{
    /* mimeData 编码 → canDropMimeData 校验 → dropMimeData 解码写回
       （对齐 Qt 的 decodeData/encodeData 回路）。 */
    XAbstractItemModel* src = XAbstractItemModel_create();
    XAbstractItemModel* dst = XAbstractItemModel_create();
    XQ_REQUIRE(src != NULL && dst != NULL);
    XAbstractItemModel_setDimension(src, 2, 2);
    XAbstractItemModel_setDimension(dst, 4, 4);
    XQ_CHECK(XAbstractItemModel_setData_2(src, 0, 0, "AA"));
    XQ_CHECK(XAbstractItemModel_setData_2(src, 0, 1, "BB"));

    int rows[2] = { 0, 0 };
    int cols[2] = { 0, 1 };
    XMimeData* data = XAbstractItemModel_mimeData(src, rows, cols, 2);
    XQ_CHECK(data != NULL);

    /* canDropMimeData：支持的动作 + 匹配的格式 ⇒ true。 */
    XQ_CHECK(XAbstractItemModel_canDropMimeData(
        dst, data, XPlatformDragAction_Copy, 1, 0, NULL));

    /* dropMimeData：解码写回 dst（空目标 → 写入对应行列）。 */
    XQ_CHECK(XAbstractItemModel_dropMimeData(
        dst, data, XPlatformDragAction_Copy, 1, 0, NULL));
    XQ_EQ_STR(XString_toUtf8(XAbstractItemModel_data(dst, 1, 0)), "AA");
    XQ_EQ_STR(XString_toUtf8(XAbstractItemModel_data(dst, 1, 1)), "BB");

    XClassDelete((XClass*)data);
    XClassDelete((XClass*)src);
    XClassDelete((XClass*)dst);
}

XQ_TEST(XAbstractItemModel, mimeRowSemantics)
{
    /* 拖放落点与占位剔除（对齐 Qt qabstractitemmodel.cpp:2226-2231 归一 +
       :2790-2804 行重排 + encodeData:2756-2763 只编码调用方给的索引）。 */
    XAbstractItemModel* src = XAbstractItemModel_create();
    XAbstractItemModel* dst = XAbstractItemModel_create();
    XQ_REQUIRE(src != NULL && dst != NULL);
    XAbstractItemModel_setDimension(src, 2, 2);
    XAbstractItemModel_setDimension(dst, 5, 5);
    XQ_CHECK(XAbstractItemModel_setData_2(src, 0, 0, "AA"));
    XQ_CHECK(XAbstractItemModel_setData_2(src, 1, 0, "BB"));

    /* ① 不可拖索引以 (-1,-1) 传入（XAbstractItemView.c:3159 的既有口径）
       ⇒ 编码侧剔除，不得进流（否则对端 drop 触发负下标）。 */
    {
        int rows[3] = { 0, -1, 1 };
        int cols[3] = { 0, -1, 0 };
        XMimeData* d = XAbstractItemModel_mimeData(src, rows, cols, 3);
        XQ_CHECK(d != NULL);
        /* 剔除 -1 后只剩两条 ⇒ 落点只用 row=2 两个格。 */
        XQ_CHECK(XAbstractItemModel_dropMimeData(
            dst, d, XPlatformDragAction_Copy, 2, 0, NULL));
        /* Qt 行重排：top=0，两行连号，落点 2 ⇒ 写 dst(2,0)/(3,0)。 */
        XQ_EQ_STR(XString_toUtf8(XAbstractItemModel_data(dst, 2, 0)), "AA");
        XQ_EQ_STR(XString_toUtf8(XAbstractItemModel_data(dst, 3, 0)), "BB");
        XClassDelete((XClass*)d);
    }

    /* ② 落点归一：row 超出父级行数 ⇒ 夹到末行；column==-1 ⇒ 0
       （qabstractitemmodel.cpp:2226-2231）。 */
    {
        /* 取有数据的那一格（源 (0,0)），列 -1 归一后落到列 0。 */
        int rows[1] = { 0 };
        int cols[1] = { 0 };
        XMimeData* d = XAbstractItemModel_mimeData(src, rows, cols, 1);
        XQ_REQUIRE(d != NULL);
        int before = XAbstractItemModel_rowCount(dst);
        XQ_CHECK(XAbstractItemModel_dropMimeData(
            dst, d, XPlatformDragAction_Copy, 9999, -1, NULL));
        /* 9999 被夹到原行数 ⇒ 追加一行，不越界；列 -1 ⇒ 0。 */
        XQ_EQ_I64(XAbstractItemModel_rowCount(dst), before + 1);
        XQ_EQ_STR(XString_toUtf8(XAbstractItemModel_data(dst, before, 0)),
                  "AA");
        XClassDelete((XClass*)d);
    }

    XClassDelete((XClass*)src);
    XClassDelete((XClass*)dst);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
