/**
 * @file       XAbstractItemModelAlignTest.c
 * @brief      XAbstractItemModel × Qt 6.8 QAbstractItemModel 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtCore/qabstractitemmodel.h
 *               （index/hasIndex/sibling/insertRow…/rowCount/columnCount/
 *               data/flags/headerData 与 rows/columns 结构信号）；
 *             - 本机 Qt 6.8.0 实测（QStringListModel 探针）：hasIndex 的
 *               负值/越界判定、hasChildren(root)、parent(root) 无效、
 *               sibling 与 index 同源、insertRow/removeRow/insertColumn/
 *               removeColumn 的单行/列形态。
 *
 *             覆盖：缺省维度、setDimension 的**行主序重排**（扩/缩列后
 *             列 0 数据不错位）、index/hasIndex/sibling 契约、单行/列
 *             便捷操作、行/列结构信号（rowsInserted/rowsRemoved/
 *             columnsInserted/columnsRemoved 的发射次数与载荷区间）、
 *             dataChanged/headerDataChanged、空模型插行后行头可用、
 *             0 列模型的行操作、flags/hasChildren/parent；
 *             **2026-11-11 补齐对齐缺口**：moveRow/moveColumn 的转发语义
 *             （P3-10/P3-11）、checkIndex 的 CheckIndexOption 四分支
 *             （P3-13/P3-14）、layout 族信号的 hint 末位载荷
 *             （P3-12，按列排序 ⇒ VerticalSortHint）。
 * @note       C99；对象全部显式释放（模型无父对象），漏删即运行器判
 *             `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XAbstractItemModel.h"
#include "XObject.h"

/* ==================== 派生模型（验证 flags 虚槽可重载，gap 137） ==================== */

XCLASS_DEFINE_BEGING(XQEditableModel)
XCLASS_DEFINE_EXTEND_END(XQEditableModel, XAbstractItemModel)

/** @brief 最小派生模型：只重载 flags 虚槽，给出 ItemIsEditable。 */
typedef struct XQEditableModel
{
    XAbstractItemModel m_base;
} XQEditableModel;

static int xqEditableModel_flags(const XAbstractItemModel* self, int row,
                                 int col)
{
    int base = XAbstractItemModel_flags_base(self, row, col);
    if (base == XItemFlag_NoItemFlags) return base;
    return base | XItemFlag_ItemIsEditable;
}

static void xqEditableModel_deinit(XQEditableModel* self)
{
    if (!self) return;
    XClass_Parent(XAbstractItemModel, EXClass_Deinit,
                  void (*)(XAbstractItemModel*))(&self->m_base);
}

/* 命名必须与 XClassSetVtable(XQEditableModel) 展开的符号逐字一致：
 * 宏展开 `XQEditableModel_class_init()`，故不能用小写前缀（否则隐式
 * 声明 + 链接期 undefined reference）。 */
static XVtable* XQEditableModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQEditableModel)
    XVTABLE_INHERIT_XCLASS(XAbstractItemModel);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xqEditableModel_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_Flags,
                             xqEditableModel_flags);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化派生模型（栈对象，维度 1x1；析构用 XClassDeinit）。 */
static void xqEditableModel_init(XQEditableModel* self)
{
    if (!self) return;
    XAbstractItemModel_init(&self->m_base);
    XClassSetVtable(self, XQEditableModel);
    XAbstractItemModel_setDimension(&self->m_base, 1, 1);
}

/* ==================== 信号探针 ==================== */

/** @brief 结构变化事件的累计快照（rowsInserted 等共用同一槽）。 */
typedef struct XQModelSignals
{
    int rowsInserted;      /**< rowsInserted 次数。 */
    int rowsRemoved;       /**< rowsRemoved 次数。 */
    int columnsInserted;   /**< columnsInserted 次数。 */
    int columnsRemoved;    /**< columnsRemoved 次数。 */
    int dataChanged;       /**< dataChanged 次数。 */
    int headerChanged;     /**< headerDataChanged 次数。 */
    int first;             /**< 最近一次载荷首值（row/column/section）。 */
    int last;              /**< 最近一次载荷末值（闭区间 last / section）。 */
    int aboutToRowsInserted;  /**< rowsAboutToBeInserted 次数。 */
    int aboutToRowsRemoved;   /**< rowsAboutToBeRemoved 次数。 */
    int aboutToColsInserted;  /**< columnsAboutToBeInserted 次数。 */
    int aboutToColsRemoved;   /**< columnsAboutToBeRemoved 次数。 */
    int aboutToReset;         /**< modelAboutToBeReset 次数。 */
    int modelReset;           /**< modelReset 次数。 */
    int layoutAboutTo;        /**< layoutAboutToBeChanged 次数。 */
    int layoutChanged;        /**< layoutChanged 次数。 */
    int layoutHint;           /**< 最近一次 layout 族信号的 hint 载荷。 */
    int rowsMoved;            /**< rowsMoved 次数。 */
    int columnsMoved;         /**< columnsMoved 次数。 */
    int movedFirst;           /**< rowsMoved/columnsMoved 首值。 */
    int movedLast;            /**< rowsMoved/columnsMoved 末值。 */
    int movedDest;            /**< rowsMoved/columnsMoved 目标位。 */
    int roles;             /**< dataChanged 角色位掩码。 */
    int bottomRow;         /**< dataChanged 右下行。 */
    int bottomCol;         /**< dataChanged 右下列。 */
    int dataTopRow;        /**< dataChanged 左上行（前两参与 bottom 区分）。 */
    int dataTopCol;        /**< dataChanged 左上列。 */
} XQModelSignals;

static XQModelSignals g_qm;

/** @brief 双 int 载荷槽（rows/columns 结构信号与 dataChanged 前缀共用）。 */
static void xqModel_int2Slot(XObject* receiver, XVarList* args)
{
    long long a = -1;
    long long b = -1;
    (void)receiver;
    if (!args) return;
    XVarList_args_2(args, int, v1, int, v2);
    a = v1;
    b = v2;
    g_qm.first = (int)a;
    g_qm.last = (int)b;
}

/** @brief rowsInserted 计数槽。 */
static void xqModel_rowsInserted(XObject* r, XVarList* args)
{ ++g_qm.rowsInserted; xqModel_int2Slot(r, args); }
/** @brief rowsRemoved 计数槽。 */
static void xqModel_rowsRemoved(XObject* r, XVarList* args)
{ ++g_qm.rowsRemoved; xqModel_int2Slot(r, args); }
/** @brief columnsInserted 计数槽。 */
static void xqModel_columnsInserted(XObject* r, XVarList* args)
{ ++g_qm.columnsInserted; xqModel_int2Slot(r, args); }
/** @brief columnsRemoved 计数槽。 */
static void xqModel_columnsRemoved(XObject* r, XVarList* args)
{ ++g_qm.columnsRemoved; xqModel_int2Slot(r, args); }
/** @brief dataChanged 计数槽（Qt 五参数：左上、右下、roles）。 */
static void xqModel_dataChanged(XObject* r, XVarList* args)
{
    (void)r;
    ++g_qm.dataChanged;
    if (!args) return;
    XVarList_args_5(args, int, topRow, int, topCol, int, bottomRow, int,
                    bottomCol, int, roles);
    g_qm.dataTopRow = topRow;
    g_qm.dataTopCol = topCol;
    g_qm.first = topRow;
    g_qm.last = topCol;
    g_qm.bottomRow = bottomRow;
    g_qm.bottomCol = bottomCol;
    g_qm.roles = roles;
}
/** @brief headerDataChanged 计数槽（三载荷：只记首末段）。 */
static void xqModel_headerChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_qm.headerChanged;
    if (!args) return;
    XVarList_args_3(args, int, orient, int, first, int, last);
    (void)orient;
    g_qm.first = first;
    g_qm.last = last;
}

/** @brief rowsAboutToBeInserted 计数槽。 */
static void xqModel_rowsAboutToInserted(XObject* r, XVarList* args)
{ ++g_qm.aboutToRowsInserted; xqModel_int2Slot(r, args); }
/** @brief rowsAboutToBeRemoved 计数槽。 */
static void xqModel_rowsAboutToRemoved(XObject* r, XVarList* args)
{ ++g_qm.aboutToRowsRemoved; xqModel_int2Slot(r, args); }
/** @brief columnsAboutToBeInserted 计数槽。 */
static void xqModel_colsAboutToInserted(XObject* r, XVarList* args)
{ ++g_qm.aboutToColsInserted; xqModel_int2Slot(r, args); }
/** @brief columnsAboutToBeRemoved 计数槽。 */
static void xqModel_colsAboutToRemoved(XObject* r, XVarList* args)
{ ++g_qm.aboutToColsRemoved; xqModel_int2Slot(r, args); }
/** @brief modelAboutToBeReset 计数槽。 */
static void xqModel_aboutToReset(XObject* r, XVarList* args)
{ (void)r; (void)args; ++g_qm.aboutToReset; }
/** @brief modelReset 计数槽。 */
static void xqModel_modelReset(XObject* r, XVarList* args)
{ (void)r; (void)args; ++g_qm.modelReset; }
/** @brief layoutAboutToBeChanged(hint) 计数槽（P3-12：hint 末位形参）。 */
static void xqModel_layoutAboutTo(XObject* r, XVarList* args)
{
    (void)r;
    ++g_qm.layoutAboutTo;
    if (!args) return;
    XVarList_args_1(args, int, hint);
    g_qm.layoutHint = hint;
}
/** @brief layoutChanged(hint) 计数槽（P3-12：hint 末位形参）。 */
static void xqModel_layoutChanged(XObject* r, XVarList* args)
{
    (void)r;
    ++g_qm.layoutChanged;
    if (!args) return;
    XVarList_args_1(args, int, hint);
    g_qm.layoutHint = hint;
}
/** @brief rowsMoved/columnsMoved 三载荷槽。 */
static void xqModel_movedSlot(XObject* r, XVarList* args)
{
    (void)r;
    if (!args) return;
    XVarList_args_3(args, int, first, int, last, int, dest);
    g_qm.movedFirst = first;
    g_qm.movedLast = last;
    g_qm.movedDest = dest;
}
static void xqModel_rowsMoved(XObject* r, XVarList* args)
{ ++g_qm.rowsMoved; xqModel_movedSlot(r, args); }
static void xqModel_columnsMoved(XObject* r, XVarList* args)
{ ++g_qm.columnsMoved; xqModel_movedSlot(r, args); }

/** @brief 清零探针。 */
static void xqModel_resetSignals(void)
{
    memset(&g_qm, 0, sizeof(g_qm));
}

/** @brief 挂接全部结构/数据信号到计数槽（每模型一次）。 */
static void xqModel_connect(XAbstractItemModel* m)
{
    XObject* o = (XObject*)m;
    XObject_connect(o, XSignal(XAbstractItemModel_rowsInserted_signal), o,
                      xqModel_rowsInserted, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_rowsRemoved_signal), o,
                      xqModel_rowsRemoved, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_columnsInserted_signal), o,
                      xqModel_columnsInserted, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_columnsRemoved_signal), o,
                      xqModel_columnsRemoved, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_dataChanged_signal), o,
                      xqModel_dataChanged, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_headerDataChanged_signal),
                      o, xqModel_headerChanged, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_rowsAboutToBeInserted_signal),
                      o, xqModel_rowsAboutToInserted,
                      XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_rowsAboutToBeRemoved_signal),
                      o, xqModel_rowsAboutToRemoved, XConnectionType_Direct);
    XObject_connect(o,
                      XSignal(XAbstractItemModel_columnsAboutToBeInserted_signal),
                      o, xqModel_colsAboutToInserted, XConnectionType_Direct);
    XObject_connect(o,
                      XSignal(XAbstractItemModel_columnsAboutToBeRemoved_signal),
                      o, xqModel_colsAboutToRemoved, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_modelAboutToBeReset_signal),
                      o, xqModel_aboutToReset, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_modelReset_signal), o,
                      xqModel_modelReset, XConnectionType_Direct);
    XObject_connect(o,
                      XSignal(XAbstractItemModel_layoutAboutToBeChanged_signal),
                      o, xqModel_layoutAboutTo, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_layoutChanged_signal), o,
                      xqModel_layoutChanged, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_rowsMoved_signal), o,
                      xqModel_rowsMoved, XConnectionType_Direct);
    XObject_connect(o, XSignal(XAbstractItemModel_columnsMoved_signal), o,
                      xqModel_columnsMoved, XConnectionType_Direct);
}

/* ==================== 用例 ==================== */

/** @brief 缺省维度与 index/hasIndex/sibling 契约（qabstractitemmodel.h:270）。 */
XQ_TEST(XAbstractItemModel, indexContract)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    int row = -9;
    int col = -9;
    XQ_REQUIRE(m != NULL);

    /* 空模型：Qt 新建模型 rowCount=columnCount=0，任何 (row,col) 都无索引。 */
    XQ_EQ_I64(XAbstractItemModel_rowCount(m), 0);
    XQ_EQ_I64(XAbstractItemModel_columnCount(m), 0);
    XQ_CHECK(!XAbstractItemModel_hasIndex(m, 0, 0));
    XQ_CHECK(!XAbstractItemModel_index(m, 0, 0, &row, &col));
    XQ_CHECK(row == -1 && col == -1); /* 无效索引输出 (-1,-1)，同 Qt row()/-1。 */

    XAbstractItemModel_setDimension(m, 2, 3);
    /* Qt 6.8 实测：hasIndex 负值一律无效、越界无效、范围内有效。 */
    XQ_CHECK(XAbstractItemModel_hasIndex(m, 0, 0));
    XQ_CHECK(XAbstractItemModel_hasIndex(m, 1, 2));
    XQ_CHECK(!XAbstractItemModel_hasIndex(m, -1, 0));
    XQ_CHECK(!XAbstractItemModel_hasIndex(m, 0, -1));
    XQ_CHECK(!XAbstractItemModel_hasIndex(m, 2, 0));
    XQ_CHECK(!XAbstractItemModel_hasIndex(m, 0, 3));

    XQ_CHECK(XAbstractItemModel_index(m, 1, 2, &row, &col));
    XQ_EQ_I64(row, 1);
    XQ_EQ_I64(col, 2);

    /* sibling：平面模型父恒为根 ⇒ 与 index 同源（Qt 实测 sibling(0,0) 有效）。 */
    XQ_CHECK(XAbstractItemModel_sibling(m, 0, 1, 1, 2, &row, &col));
    XQ_EQ_I64(row, 0);
    XQ_EQ_I64(col, 1);
    XQ_CHECK(!XAbstractItemModel_sibling(m, 5, 0, 0, 0, &row, &col));
    XQ_CHECK(row == -1 && col == -1);

    /* 平面模型 parent 恒为根（Qt 实测 parent(root) 无效）。 */
    XQ_EQ_I64(XAbstractItemModel_parent(m, 1, 1), -1);
    /* hasChildren(root)：行、列均大于 0（Qt 实测 hasChildren(root)=true）。 */
    XQ_CHECK(XAbstractItemModel_hasChildren(m));

    /* flags：有效索引 = Selectable|Enabled（Qt 基类缺省，qabstractitemmodel.h:321）；
       无效索引 = NoItemFlags。 */
    XQ_EQ_I64(XAbstractItemModel_flags(m, 0, 0),
              XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled);
    XQ_EQ_I64(XAbstractItemModel_flags(m, 9, 9), XItemFlag_NoItemFlags);

    XClassDelete((XClass*)m);
}

/** @brief setDimension 的行主序重排：扩/缩列后列 0 数据不串位。 */
XQ_TEST(XAbstractItemModel, setDimensionReordersRowMajor)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);

    /* 2 行 1 列：a/b 落在 (0,0)/(1,0)。 */
    XAbstractItemModel_setDimension(m, 2, 1);
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 0, "a"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 1, 0, "b"));

    /* 扩到 2x2：原始数据必须按 (row,col) 搬到新行距位置——(0,0)=a、(1,0)=b，
       新增 (0,1)/(1,1) 为空（Qt 扩列不改变既有单元格内容）。 */
    XAbstractItemModel_setDimension(m, 2, 2);
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "a");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 1, 0), "b");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 1), "");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 1, 1), "");

    /* 列 1 写值后再缩回 1 列：列 0 数据仍在 (row,0)，被裁的列 1 消失。 */
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 1, "c"));
    XAbstractItemModel_setDimension(m, 2, 1);
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "a");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 1, 0), "b");
    XQ_EQ_I64(XAbstractItemModel_columnCount(m), 1);

    /* 行扩展/收缩：缩掉的尾部行释放，新增行空白。 */
    XAbstractItemModel_setDimension(m, 3, XAbstractItemModel_columnCount(m));
    XQ_EQ_I64(XAbstractItemModel_rowCount(m), 3);
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 0), "");
    XAbstractItemModel_setDimension(m, 1, XAbstractItemModel_columnCount(m));
    XQ_EQ_I64(XAbstractItemModel_rowCount(m), 1);

    XClassDelete((XClass*)m);
}

/** @brief 单行/列便捷操作与结构信号（rows/columns 族发射次数与载荷）。 */
XQ_TEST(XAbstractItemModel, structuralSignals)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);

    xqModel_resetSignals();
    xqModel_connect(m);

    /* insertRow = insertRows(row,1)（qabstractitemmodel.h:310）。载荷为
       **闭区间 first..last**（Qt rowsInserted(parent,first,last)）⇒ 0..0；
       aboutTo 先于改动发射。 */
    XQ_CHECK(XAbstractItemModel_insertRow(m, 0));
    XQ_EQ_I64(g_qm.rowsInserted, 1);
    XQ_EQ_I64(g_qm.aboutToRowsInserted, 1);
    XQ_EQ_I64(g_qm.first, 0);
    XQ_EQ_I64(g_qm.last, 0);
    XQ_EQ_I64(XAbstractItemModel_rowCount(m), 1);

    /* 无列模型也能插行（Qt 的 rowCount/columnCount 相互独立）。 */
    XQ_EQ_I64(XAbstractItemModel_columnCount(m), 0);

    /* 补两列：发 columnsAboutToBeInserted/columnsInserted（区间 0..1）。 */
    XQ_CHECK(XAbstractItemModel_insertColumns(m, 0, 2));
    XQ_EQ_I64(g_qm.columnsInserted, 1);
    XQ_EQ_I64(g_qm.aboutToColsInserted, 1);
    XQ_EQ_I64(g_qm.first, 0);
    XQ_EQ_I64(g_qm.last, 1);
    XQ_EQ_I64(XAbstractItemModel_columnCount(m), 2);

    /* removeRow / removeColumn 对称（区间闭区间，aboutTo 计数同步）。 */
    XQ_CHECK(XAbstractItemModel_removeRow(m, 0));
    XQ_EQ_I64(g_qm.rowsRemoved, 1);
    XQ_EQ_I64(g_qm.aboutToRowsRemoved, 1);
    XQ_EQ_I64(XAbstractItemModel_rowCount(m), 0);
    XQ_CHECK(XAbstractItemModel_removeColumn(m, 1));
    XQ_EQ_I64(g_qm.columnsRemoved, 1);
    XQ_EQ_I64(g_qm.aboutToColsRemoved, 1);
    XQ_EQ_I64(XAbstractItemModel_columnCount(m), 1);

    /* 越界与非法 count：一律 false 且不发信号（aboutTo 也不发）。 */
    xqModel_resetSignals();
    XQ_CHECK(!XAbstractItemModel_insertRow(m, 99));
    XQ_CHECK(!XAbstractItemModel_removeRow(m, 5));
    XQ_CHECK(!XAbstractItemModel_insertColumn(m, 99));
    XQ_CHECK(!XAbstractItemModel_removeColumn(m, 5));
    XQ_EQ_I64(g_qm.rowsInserted + g_qm.rowsRemoved +
              g_qm.columnsInserted + g_qm.columnsRemoved, 0);
    XQ_EQ_I64(g_qm.aboutToRowsInserted + g_qm.aboutToRowsRemoved +
              g_qm.aboutToColsInserted + g_qm.aboutToColsRemoved, 0);

    /* setDimension 的列变化同样发列族信号（Qt setColumnCount 等价路径），
       区间 1..2（旧列数 1 起增 2 列）。 */
    xqModel_resetSignals();
    XAbstractItemModel_setDimension(m, 1, 3);
    XQ_EQ_I64(g_qm.columnsInserted, 1);
    XQ_EQ_I64(g_qm.aboutToColsInserted, 1);
    XQ_EQ_I64(g_qm.first, 1);
    XQ_EQ_I64(g_qm.last, 2);
    xqModel_resetSignals();
    XAbstractItemModel_setDimension(m, 1, 1);
    XQ_EQ_I64(g_qm.columnsRemoved, 1);
    XQ_EQ_I64(g_qm.aboutToColsRemoved, 1);

    XClassDelete((XClass*)m);
}

/** @brief data/headerData 通路与空模型插行后行头可用。 */
XQ_TEST(XAbstractItemModel, dataAndHeader)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);

    xqModel_resetSignals();
    xqModel_connect(m);

    /* 空模型插行：行头表必须随行数就位（此前 m_rows>0 门限使然，
       空模型插行后 headerData(row,1) 永远越界）。 */
    XQ_CHECK(XAbstractItemModel_insertRow(m, 0));
    XQ_CHECK(XAbstractItemModel_setHeaderData_2(m, 0, 1, "行一"));
    XQ_EQ_STR(XAbstractItemModel_headerData_2(m, 0, 1), "行一");
    XQ_EQ_I64(g_qm.headerChanged, 1);
    XQ_EQ_I64(g_qm.first, 0);  /* headerDataChanged(orientation,section,section)。 */
    XQ_EQ_I64(g_qm.last, 0);

    /* 单元格读写：借用的 XString* 与 UTF-8 双通路同源。 */
    XAbstractItemModel_setDimension(m, 1, 2);
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 0, "甲"));
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "甲");
    XQ_CHECK_PTR(XAbstractItemModel_data(m, 0, 0));
    XQ_EQ_I64(g_qm.dataChanged, 1);
    XQ_EQ_I64(g_qm.first, 0);
    XQ_EQ_I64(g_qm.last, 0);
    /* Qt 五参数 dataChanged：单格写入时右下==左上，roles = Display|Edit
       （bit0|bit2 = 0x5；0 表示"未指明角色"）。 */
    XQ_EQ_I64(g_qm.dataTopRow, 0);
    XQ_EQ_I64(g_qm.dataTopCol, 0);
    XQ_EQ_I64(g_qm.bottomRow, 0);
    XQ_EQ_I64(g_qm.bottomCol, 0);
    XQ_EQ_I64(g_qm.roles, 0x5);

    /* 越界读写：读返回 NULL/空串、写返回 false（不发射 dataChanged）。 */
    XQ_CHECK(XAbstractItemModel_data(m, 5, 0) == NULL);
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 5, 0), "");
    XQ_CHECK(!XAbstractItemModel_setData_2(m, 5, 0, "x"));
    XQ_EQ_I64(g_qm.dataChanged, 1);

    /* 列头：按列号承载，越界写失败。 */
    XQ_CHECK(XAbstractItemModel_setHeaderData_2(m, 1, 0, "乙列"));
    XQ_EQ_STR(XAbstractItemModel_headerData_2(m, 1, 0), "乙列");
    XQ_CHECK(!XAbstractItemModel_setHeaderData_2(m, 9, 0, "越界"));

    XClassDelete((XClass*)m);
}

/** @brief 行/列移动、match、增量取数与其它的 Qt 基类缺省（gap 135）。 */
XQ_TEST(XAbstractItemModel, moveSearchAndBaseDefaults)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    int rowsOut[8];
    int colsOut[8];
    int hits;
    int outRow = -9;
    int outCol = -9;
    int spanR = -9;
    int spanC = -9;
    XQ_REQUIRE(m != NULL);

    /* 4x2：列 0 = 行号文本，列 1 = a..d。 */
    XAbstractItemModel_setDimension(m, 4, 2);
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 0, "0"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 1, 0, "1"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 2, 0, "2"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 3, 0, "3"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 1, "a"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 1, 1, "b"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 2, 1, "c"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 3, 1, "d"));

    /* hasChildren(index)：平铺模型任何有效索引无子行（Qt 缺省语义）。 */
    XQ_CHECK(!XAbstractItemModel_hasChildrenAt(m, 0, 0));
    /* 无效父索引（根）⇒ 根查询。 */
    XQ_CHECK(XAbstractItemModel_hasChildrenAt(m, -1, -1));

    xqModel_resetSignals();
    xqModel_connect(m);

    /* moveRows：源 [0,1] 搬到 destinationChild=4（移动前下标）⇒ 2,3,0,1。 */
    XQ_CHECK(XAbstractItemModel_moveRows(m, 0, 2, 4));
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "2");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 1, 0), "3");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 0), "0");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 3, 0), "1");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 1), "a");
    XQ_EQ_I64(g_qm.rowsMoved, 1);
    XQ_EQ_I64(g_qm.movedFirst, 0);
    XQ_EQ_I64(g_qm.movedLast, 1);
    XQ_EQ_I64(g_qm.movedDest, 4);

    /* 空操作目标（==源首 / ==源尾）⇒ false（Qt beginMoveRows 判定）。 */
    XQ_CHECK(!XAbstractItemModel_moveRows(m, 1, 1, 1));
    XQ_CHECK(!XAbstractItemModel_moveRows(m, 1, 1, 2));
    XQ_CHECK(!XAbstractItemModel_moveRows(m, 9, 1, 0));
    XQ_EQ_I64(g_qm.rowsMoved, 1);

    /* moveRows 反向（dest < src）：把行 2 搬到 0 ⇒ 0,2,3,1。 */
    XQ_CHECK(XAbstractItemModel_moveRows(m, 2, 1, 0));
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "0");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 1, 0), "2");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 0), "3");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 3, 0), "1");
    XQ_EQ_I64(g_qm.movedDest, 0);

    /* moveColumns：2 列扩到 3 列后把列 0 搬到 2（dest>src）。 */
    XAbstractItemModel_insertColumn(m, 2);
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 1, "x"));
    xqModel_resetSignals(); /* 信号已连接，只清计数（勿重复 connect）。 */
    XQ_CHECK(XAbstractItemModel_moveColumns(m, 0, 1, 2));
    /* 原列序 (0,1,2) → (1,0,2)：destinationChild 按移动前下标解释，
       插入位 = dest-count = 1。行 0 原为 ("0","x","")。 */
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "x");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 1), "0");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 2), "");
    XQ_EQ_I64(g_qm.columnsMoved, 1);
    XQ_EQ_I64(g_qm.movedDest, 2);
    XQ_CHECK(!XAbstractItemModel_moveColumns(m, 0, 1, 0));

    /* match：Contains + 不区分大小写（Qt 缺省大小写不敏感）。 */
    {
        XAbstractItemModel* s = XAbstractItemModel_create();
        XQ_REQUIRE(s != NULL);
        XAbstractItemModel_setDimension(s, 2, 2);
        XQ_CHECK(XAbstractItemModel_setData_2(s, 0, 0, "Alpha"));
        XQ_CHECK(XAbstractItemModel_setData_2(s, 0, 1, "beta"));
        XQ_CHECK(XAbstractItemModel_setData_2(s, 1, 0, "alphabet"));
        XQ_CHECK(XAbstractItemModel_setData_2(s, 1, 1, "Gamma"));

        hits = XAbstractItemModel_match(
            s, 0, 0, 0, "alpha", XMatchFlag_MatchExactly, rowsOut, colsOut,
            8);
        XQ_EQ_I64(hits, 0); /* 缺省不区分大小写，但 Exactly 要求全等。 */
        hits = XAbstractItemModel_match(
            s, 0, 0, 0, "Alpha", XMatchFlag_MatchExactly, rowsOut, colsOut,
            8);
        XQ_EQ_I64(hits, 1);
        XQ_EQ_I64(rowsOut[0], 0);
        XQ_EQ_I64(colsOut[0], 0);
        hits = XAbstractItemModel_match(
            s, 0, 0, 0, "alpha", XMatchFlag_MatchContains, rowsOut, colsOut,
            8);
        XQ_EQ_I64(hits, 2); /* Alpha + alphabet。 */
        hits = XAbstractItemModel_match(
            s, 0, 0, 0, "gt", XMatchFlag_MatchEndsWith |
                               XMatchFlag_MatchCaseSensitive,
            rowsOut, colsOut, 8);
        XQ_EQ_I64(hits, 0);
        hits = XAbstractItemModel_match(
            s, 0, 0, 0, "ha", XMatchFlag_MatchEndsWith, rowsOut, colsOut, 8);
        XQ_EQ_I64(hits, 1); /* Alpha 以 ha 结尾（大小写不敏感）。 */
        /* match 只扫 start 所在**列**（Qt 6.8 qabstractitemmodel.cpp:2529）：
           从 (1,0) 起不回绕扫不到 (0,0) 的 Alpha（本列只有 alphabet），
           带 MatchWrap 才回绕命中 (0,0)。列 1 不含 Alpha ⇒ 恒 0 条。 */
        hits = XAbstractItemModel_match(
            s, 1, 0, 0, "Alpha", XMatchFlag_MatchExactly, rowsOut, colsOut, 8);
        XQ_EQ_I64(hits, 0);
        hits = XAbstractItemModel_match(
            s, 1, 0, 0, "Alpha",
            XMatchFlag_MatchExactly | XMatchFlag_MatchWrap, rowsOut, colsOut,
            8);
        XQ_EQ_I64(hits, 1);
        XQ_EQ_I64(rowsOut[0], 0);
        XQ_EQ_I64(colsOut[0], 0);
        XQ_EQ_I64(XAbstractItemModel_match(
                      s, 0, 1, 0, "Alpha", XMatchFlag_MatchExactly, NULL,
                      NULL, 0), 0);
        /* 非文本角色无承载 ⇒ 0 条；正则/通配需正则子系统 ⇒ 0 条。 */
        XQ_EQ_I64(XAbstractItemModel_match(
                      s, 0, 0, 1, "Alpha", XMatchFlag_MatchExactly, NULL,
                      NULL, 0), 0);
        XQ_EQ_I64(XAbstractItemModel_match(
                      s, 0, 0, 0, "A.*", XMatchFlag_MatchRegularExpression,
                      NULL, NULL, 0), 0);
        XClassDelete((XClass*)s);
    }

    /* span（缺省 1x1）、buddy（无伙伴）、submit/revert。 */
    XQ_CHECK(XAbstractItemModel_span(m, 0, 0, &spanR, &spanC));
    XQ_CHECK(spanR == 1 && spanC == 1);
    XQ_CHECK(!XAbstractItemModel_span(m, 99, 0, &spanR, &spanC));
    XQ_CHECK(!XAbstractItemModel_buddy(m, 0, 0, &outRow, &outCol));
    XQ_CHECK(outRow == -1 && outCol == -1);
    XQ_CHECK(XAbstractItemModel_submit(m));
    XAbstractItemModel_revert(m);
    /* 增量取数：基类缺省 false / 空操作。 */
    XQ_CHECK(!XAbstractItemModel_canFetchMore(m, -1, -1));
    XAbstractItemModel_fetchMore(m, -1, -1);

    XClassDelete((XClass*)m);
}

/** @brief 单行/单列移动转发（P3-10/P3-11）与 checkIndex（P3-13/P3-14）。 */
XQ_TEST(XAbstractItemModel, moveRowColumnAndCheckIndex)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(m != NULL);

    /* 3x2：列 0 = 行号文本、列 1 = 字母。 */
    XAbstractItemModel_setDimension(m, 3, 2);
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 0, "0"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 1, 0, "1"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 2, 0, "2"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 1, "a"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 1, 1, "b"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 2, 1, "c"));

    xqModel_resetSignals();
    xqModel_connect(m);

    /* P3-10 moveRow(source, dest) = moveRows(source, 1, dest)（Qt 自身
       就是这一行 inline 转发）：把行 0 搬到行尾（dest=3，按移动前下标）
       ⇒ 1,2,0；整行内容（含列 1）随动；信号载荷 first==last==0。 */
    XQ_CHECK(XAbstractItemModel_moveRow(m, 0, 3));
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "1");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 1, 0), "2");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 0), "0");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 1), "b");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 1), "a");
    XQ_EQ_I64(g_qm.rowsMoved, 1);
    XQ_EQ_I64(g_qm.movedFirst, 0);
    XQ_EQ_I64(g_qm.movedLast, 0);
    XQ_EQ_I64(g_qm.movedDest, 3);

    /* 反向：行 2 搬到 0 ⇒ 0,1,2（dest < source 分支）。 */
    xqModel_resetSignals();
    XQ_CHECK(XAbstractItemModel_moveRow(m, 2, 0));
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "0");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 1, 0), "1");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 0), "2");
    XQ_EQ_I64(g_qm.rowsMoved, 1);
    XQ_EQ_I64(g_qm.movedDest, 0);

    /* 空操作：dest == source / dest == source+1 ⇒ false 且不发信号
       （Qt beginMoveRows 判定，转发层不得吞掉该语义）。 */
    xqModel_resetSignals();
    XQ_CHECK(!XAbstractItemModel_moveRow(m, 1, 1));
    XQ_CHECK(!XAbstractItemModel_moveRow(m, 1, 2));
    XQ_CHECK(!XAbstractItemModel_moveRow(m, 9, 0));
    XQ_EQ_I64(g_qm.rowsMoved, 0);

    /* P3-11 moveColumn(source, dest) = moveColumns(source, 1, dest)：
       列 0 搬到行尾（dest=2）⇒ 原列序 (0,1) → (1,0)。 */
    xqModel_resetSignals();
    XQ_CHECK(XAbstractItemModel_moveColumn(m, 0, 2));
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "a");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 1), "0");
    XQ_EQ_I64(g_qm.columnsMoved, 1);
    XQ_EQ_I64(g_qm.movedFirst, 0);
    XQ_EQ_I64(g_qm.movedLast, 0);
    XQ_EQ_I64(g_qm.movedDest, 2);
    /* 反向：把末列搬到列首。 */
    XQ_CHECK(XAbstractItemModel_moveColumn(m, 1, 0));
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "0");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 1), "a");
    xqModel_resetSignals();
    XQ_CHECK(!XAbstractItemModel_moveColumn(m, 1, 1));
    XQ_CHECK(!XAbstractItemModel_moveColumn(m, 0, 1));
    XQ_EQ_I64(g_qm.columnsMoved, 0);

    /* P3-13/P3-14：checkIndex 逐条对齐 Qt 6.8
       （qabstractitemmodel.cpp:3586-3641）。 */
    /* 有效索引 + 默认选项：通过。 */
    XQ_CHECK(XAbstractItemModel_checkIndex(m, 0, 0,
                                           XCheckIndexOption_NoOption));
    XQ_CHECK(XAbstractItemModel_checkIndex(m, 2, 1,
                                           XCheckIndexOption_NoOption));
    /* 越界：默认选项做父索引（根）的 rowCount/columnCount 区间校验 ⇒ 拒绝。 */
    XQ_CHECK(!XAbstractItemModel_checkIndex(m, 3, 0,
                                            XCheckIndexOption_NoOption));
    XQ_CHECK(!XAbstractItemModel_checkIndex(m, 0, 2,
                                            XCheckIndexOption_NoOption));
    XQ_CHECK(!XAbstractItemModel_checkIndex(m, 3, 0,
                                            XCheckIndexOption_IndexIsValid));
    /* DoNotUseParent：Qt 把区间校验整段放在该 if 内 ⇒ 越界也放过。 */
    XQ_CHECK(XAbstractItemModel_checkIndex(m, 3, 0,
                                           XCheckIndexOption_DoNotUseParent));
    /* 无效索引（本库以 (-1,-1) 表达）：默认选项**放过**（Qt 无条件信任
       无效索引），声明 IndexIsValid 才拒绝。 */
    XQ_CHECK(XAbstractItemModel_checkIndex(m, -1, -1,
                                           XCheckIndexOption_NoOption));
    XQ_CHECK(!XAbstractItemModel_checkIndex(m, -1, -1,
                                            XCheckIndexOption_IndexIsValid));
    /* ParentIsInvalid：平铺模型 parent 恒为根（-1）⇒ 恒满足。 */
    XQ_CHECK(XAbstractItemModel_checkIndex(m, 0, 0,
                                           XCheckIndexOption_ParentIsInvalid));
    XQ_CHECK(XAbstractItemModel_checkIndex(
        m, 0, 0, XCheckIndexOption_DoNotUseParent |
                     XCheckIndexOption_ParentIsInvalid));
    /* NULL 模型：默认选项无行/列数可验 ⇒ 拒绝；DoNotUseParent 跳过校验
       ⇒ 放过（与 Qt 的「索引不属于本模型」分支同向：无可校验即不信）。 */
    XQ_CHECK(!XAbstractItemModel_checkIndex(NULL, 0, 0,
                                            XCheckIndexOption_NoOption));
    XQ_CHECK(XAbstractItemModel_checkIndex(NULL, 0, 0,
                                           XCheckIndexOption_DoNotUseParent));
    XQ_CHECK(!XAbstractItemModel_checkIndex(NULL, 0, 0,
                                            XCheckIndexOption_IndexIsValid));

    XClassDelete((XClass*)m);
}

/** @brief 重置/布局协议与 flags 虚槽重载（gap 133/137）。 */
XQ_TEST(XAbstractItemModel, resetLayoutAndFlagsSlot)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQEditableModel em;
    XQ_REQUIRE(m != NULL);

    xqModel_resetSignals();
    xqModel_connect(m);
    /* beginResetModel/endResetModel 成对发 aboutTo + 事后信号（只发信号，
       不改数据）。 */
    XAbstractItemModel_beginResetModel(m);
    XQ_EQ_I64(g_qm.aboutToReset, 1);
    XQ_EQ_I64(g_qm.modelReset, 0);
    XAbstractItemModel_endResetModel(m);
    XQ_EQ_I64(g_qm.modelReset, 1);

    /* sort：layoutAboutToBeChanged 先于 layoutChanged。 */
    XAbstractItemModel_setDimension(m, 2, 1);
    XQ_CHECK(XAbstractItemModel_setData_2(m, 0, 0, "b"));
    XQ_CHECK(XAbstractItemModel_setData_2(m, 1, 0, "a"));
    xqModel_resetSignals();
    XAbstractItemModel_sort(m, 0, 0);
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "a");
    XQ_EQ_I64(g_qm.layoutAboutTo, 1);
    XQ_EQ_I64(g_qm.layoutChanged, 1);
    /* P3-12：layout 族的 hint 末位形参真的带了载荷（不是只加枚举的
       假实现）。按列重排行 = Qt 的「竖向排序」⇒ VerticalSortHint
       （qsortfilterproxymodel.cpp:609/619 同款）。 */
    XQ_EQ_I64(g_qm.layoutHint, XLayoutChangeHint_VerticalSortHint);

    /* flags 虚槽：基类 Selectable|Enabled（33）；派生重载后并上
       ItemIsEditable（2）⇒ 35。无效索引不受重载影响（NoItemFlags）。 */
    XQ_EQ_I64(XAbstractItemModel_flags(m, 0, 0),
              XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled);
    xqEditableModel_init(&em);
    XQ_EQ_I64(XAbstractItemModel_flags(&em.m_base, 0, 0),
              XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled |
                  XItemFlag_ItemIsEditable);
    XQ_EQ_I64(XAbstractItemModel_flags(&em.m_base, 9, 9),
              XItemFlag_NoItemFlags);
    /* 基类缺省实现保持原口径（派生可静态回调父类）。 */
    XQ_EQ_I64(XAbstractItemModel_flags_base(&em.m_base, 0, 0),
              XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled);
    XClassDeinit((XClass*)&em);

    XClassDelete((XClass*)m);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
