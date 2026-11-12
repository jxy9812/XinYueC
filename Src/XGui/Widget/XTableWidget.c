/**
 * @file       XTableWidget.c
 * @brief      表格控件实现（对齐 Qt 6.8 QTableWidget / QTableWidgetItem）。
 * @details    条目对象（XTableWidgetItem）由表格持有所有权并按 (row,column)
 *             索引；role→数据存放在条目内部的 XVector 中，值深拷贝持有。
 *             实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XGUI_ON
#include "XTableWidget.h"

#include "XStringUtils.h"
#include <string.h>

#include "XVariant.h"
#include "XDataStream.h" /* [序列化] 条目 read/write。 */
#include "XColor.h"
#include "XFont.h"
#include "XGeometry.h"
#include "XIcon.h"
#include "XMemory.h"
#include "XWidget_Protected.h"
#include "XHeaderView_Protected.h" /* [K-68/69/73 2026-11-14] forwardMove/
                                   * gestureActive/pressedSection/
                                   * setStretchViewportLength——保护接口按
                                   * 约定显式 include，不依赖传递（XTabBar.c
                                   * 与 XHeaderView.c 同款纪律）。 */
#include "XEvent.h"
#include "XPainter.h"
#include "XWindowEvent.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XItemDelegate.h"      /* 绘制面查委托（§B5/P3-46）：paint_base/
                                    * paintOverridden。 */
#include "XAlgorithm.h"
#include "XSort.h" /* [B15perf] XMergeSort：排序本体走框架稳定归并
                    * （等键保序，对齐 Qt QTableModel::sort 的
                    * std::stable_sort）。 */
#include "XAtomic.h" /* XATOMIC_THREAD_LOCAL：DisplayRole 数值文本化的借用缓冲。 */
#include "XItemSelection.h"      /* §selrange：selectedRanges 直读范围表（B2 值对象）。 */
#include "XRegularExpression.h"  /* §selrange：findItems 正则/通配复用框架正则引擎。 */

#if XTABLEWIDGET_ON

#define XTW_DEFAULT_COL_WIDTH  90
#define XTW_DEFAULT_ROW_HEIGHT 24
#define XTW_HEADER_H           24
#define XTW_HEADER_W           40

/** @brief 条目缺省标志位（对齐 Qt QTableWidgetItem 构造的缺省 flags）。 */
#define XTW_DEFAULT_ITEM_FLAGS \
    ((uint32_t)(XItemFlag_ItemIsEditable | XItemFlag_ItemIsSelectable | \
                XItemFlag_ItemIsUserCheckable | XItemFlag_ItemIsEnabled | \
                XItemFlag_ItemIsDragEnabled | XItemFlag_ItemIsDropEnabled))

/* role 渲染消费常量（对齐 QStyledItemDelegate::paint 的条目布局子集，
 * 勾选框/装饰与文本的间距，像素）。 */
#define XTW_ROLE_CHECK_BOX 12
#define XTW_ROLE_CONTENT_GAP 4

/* DisplayRole 数值文本化的借用缓冲（见 XTableWidgetItem_text_toUtf8）。
 * 线程局部 4 槽环：同一表达式里最多 4 个借用文本同时有效；需要更长生命
 * 周期用 XTableWidgetItem_text（返回新副本）。 */
static XATOMIC_THREAD_LOCAL char g_xtwTextRing[4][64];
static XATOMIC_THREAD_LOCAL unsigned g_xtwTextRingIdx;

/* ==================== 内部辅助（前向声明） ==================== */

static void VX_tableWidget_paintEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mousePressEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static bool xtw_forwardHeaderBand(XTableWidget* tw, XEvent* event);
static bool VX_tableWidget_visualRect(const XAbstractItemView* self,
                                      int row, int col, XRect* out);
static bool VX_tableWidget_shouldEdit(const XAbstractItemView* self,
                                      int row, int col);
static void VX_tableWidget_mouseMoveEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_keyPressEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_scrollContentsBy(XAbstractScrollArea* self,
                                            int dx, int dy);
static void VX_tableWidget_resizeEvent(XWidget* self, XEvent* event);
/** [K-73a 2026-11-14] 视口事件覆写：滚动条显隐等改变视口几何时重推
 *  列布局可用长（实现见 resizeEvent 旁）。 */
static bool VX_tableWidget_viewportEvent(XAbstractScrollArea* self,
                                         XEvent* event);
/** @brief 排序本体虚槽覆写（P0-33）：转条目矩阵排序（实现见行为节）。 */
static void VXTableWidget_sortByColumn(XTableView* self, int column,
                                       int order);
/** @brief Stretch 模式：按内容区宽均分列宽（实现见列宽模式节）。 */
static void xtw_stretchColumnsToViewport(XTableWidget* self);
/** [K-73 2026-11-14] 列布局可用长推送（表头 stretchLastSection 引擎入口）。 */
static void xtw_pushStretchViewport(XTableWidget* self);
/** @brief 调色板取色助手。 */
static uint32_t xtw_color(const XTableWidget* self, XPaletteColorRole role);
/** @brief 坐标 → 单元格（返回行列；表头/越界返回 -1）。 */
static void xtw_cellAt(const XTableWidget* self, int x, int y,
                       int* row, int* col);
/** [K-68 2026-11-14] 逻辑列 → 视觉序（表格体唯一映射入口；实现见
 *  渲染与交互节）。 */
static int xtw_logicalToVisual(const XTableWidget* self, int col);
/** [K-68 2026-11-14] 视觉序 → 逻辑列（唯一映射入口的反查；实现见
 *  渲染与交互节）。 */
static int xtw_visualToLogical(const XTableWidget* self, int visual);
/** @brief 列 x 起点（visualItemRect 使用；实现见渲染与交互节）。 */
static int xtw_colX(const XTableWidget* self, int col);
/** @brief 行 y 起点（内容坐标；隐藏行占高 0；实现见渲染与交互节）。 */
static int xtw_rowY(const XTableWidget* self, int row);
/** @brief 滚动偏移（visualItemRect 使用；实现见渲染与交互节）。 */
static int xtw_vOffset(const XTableWidget* self);
static int xtw_hOffset(const XTableWidget* self);
/** @brief 按当前行列尺寸同步滚动范围（对齐 QTableViewPrivate::updateScrollBars）。 */
static void xtw_updateContentSize(XTableWidget* self);

/**
 * @brief 垂直表头（行号列）占宽：隐藏时为 0（单元格左移贴边）。
 * @details 对标 Qt `verticalHeader()->setVisible(false)`：隐藏是**整列
 *          不占宽**，不是留一条空白窄带。凡是"表体左缘起点"的计算都要
 *          走这里（列起点、内容宽、命中测试、行号绘制）。
 */
static int xtw_vHeaderW(const XTableWidget* self)
{
    return (self && self->m_vHeaderVisible) ? self->m_headerWidth : 0;
}

/**
 * @brief 列是否隐藏（对标 Qt：隐藏状态由 horizontalHeader 段承载）。
 * @details XTableWidget 的绘制/命中/内容尺寸全部读本类自己的
 *          m_colWidths / m_base.m_rowHeight，不走 XTableView 的几何入口，
 *          因此必须**显式**消费基类的隐藏状态表——否则经
 *          XTableView_setColumnHidden 下发的隐藏不产生任何视觉效果
 *          （gap 160：历史/报警页列筛选 HsfDataHistory.c:989 失效）。
 */
static bool xtw_colHidden(const XTableWidget* self, int col)
{
    return self && col >= 0 && XTableView_isColumnHidden(&self->m_base, col);
}

/** @brief 行是否隐藏（同 xtw_colHidden，消费基类行隐藏表）。 */
static bool xtw_rowHidden(const XTableWidget* self, int row)
{
    return self && row >= 0 && XTableView_isRowHidden(&self->m_base, row);
}

/** @brief 统一行高（隐藏行由调用方按 xtw_rowHidden 整行跳过，不靠高度 0：
 *         本表绘制按统一行高推进 y，逐行 0 高会与基类 rowHeight 的
 *         隐藏段 0 口径混淆）。 */
static int xtw_rowHeight(const XTableWidget* self)
{
    int rh = self ? (int)self->m_base.m_rowHeight : 0;
    return rh > 0 ? rh : XTW_DEFAULT_ROW_HEIGHT;
}
/** @brief 确保垂直表头指针数组容量（新增区域清零）。 */
static bool xtw_ensureVHeaders(XTableWidget* self, int count);
/** @brief 挂载部件表：按 (row,col) 查找条目下标；无则返回 -1。 */
static int xtw_cwFind(const XTableWidget* self, int row, int col);
/** @brief 重摆全部挂件（滚动/结构变化后；实现见单元格挂载节）。 */
static void xtw_cellWidgetRelayout(XTableWidget* self);
/** @brief 表头几何变化 ⇒ 挂件重摆（K-68 残留 b；实现见行为节）。 */
static void xtwHeaderGeometriesChanged(XObject* receiver, XVarList* args);
/** @brief 释放一个条目（含其 role 数据）。 */
static void xtw_itemDelete(XTableWidgetItem* item);
/* [§B4 2026-11-15] 选择模型 ↔ 条目选中位双向桥（内部入口；实现见桥接节）。 */
static XItemSelectionModel* xtw_selModel(const XTableWidget* self);
static void xtw_selArm(XTableWidget* self);
static void xtw_selDisarm(XTableWidget* self);
static void xtw_selChangedSlot(XObject* receiver, XVarList* args);
static void xtw_selCurrentChangedSlot(XObject* receiver, XVarList* args);
static bool xtw_selMirror(XTableWidget* self, XRect* outDirty);
static bool xtw_selDiffSnapshot(XTableWidget* self, XRect* outDirty);
static void xtw_selSyncDone(XTableWidget* self, bool forceSel);
static void xtw_selRemap(XTableWidget* self, int axis, int at, int delta);
static void xtw_selFollowRowPermutation(XTableWidget* self, const int* perm,
                                        int rows, bool emitSignals);
static bool xtw_selTrimAxis(XTableWidget* self, int axis, int keep);
static void xtw_selSnapshotReshape(XTableWidget* self, int oldRows,
                                   int oldCols);
static bool xtw_selCurrentRangeRemoved(XTableWidget* self, int axis,
                                       int start, int end);

/* ==================== XTableWidgetItem 实现 ==================== */

/** @brief 在 m_values 中查找 role 槽位；未找到返回 NULL。 */
static XTableWidgetItemData* xtw_itemSlot(const XTableWidgetItem* item, int role)
{
    size_t i;
    size_t n;
    if (!item) return NULL;
    n = XVector_size_base(&item->m_values);
    for (i = 0; i < n; ++i) {
        XTableWidgetItemData* d =
            (XTableWidgetItemData*)XVector_at_base(&item->m_values, (int64_t)i);
        if (d && d->role == role) return d;
    }
    return NULL;
}

/** @brief m_values 元素深拷贝回调（XTableWidgetItemData 复制）。 */
static void xtw_itemDataCopy(void* data, const void* sData)
{
    const XTableWidgetItemData* src = (const XTableWidgetItemData*)sData;
    XTableWidgetItemData* dst = (XTableWidgetItemData*)data;
    if (!dst) return;
    dst->role = src ? src->role : 0;
    dst->value = (src && src->value) ? XVariant_create_copy(src->value) : NULL;
}

/** @brief m_values 元素释放回调（释放内部 XVariant）。 */
static void xtw_itemDataDeinit(void* data)
{
    XTableWidgetItemData* d = (XTableWidgetItemData*)data;
    if (d && d->value) {
        XClassDelete((XClass*)d->value);
        d->value = NULL;
    }
}

/** @brief 初始化条目拥有的 role 容器。 */
static void xtw_itemValuesInit(XTableWidgetItem* item)
{
    XVector_init(&item->m_values, sizeof(XTableWidgetItemData), false);
    XContainerSetDataCopyMethod(&item->m_values, xtw_itemDataCopy);
    XContainerSetDataDeinitMethod(&item->m_values, xtw_itemDataDeinit);
}

static void VXTableWidgetItem_deinit(XTableWidgetItem* self)
{
    if (!self) return;
    /* [根修 2026-11-03] XClassDeinit 在 XVector 上**不逐元素调
     * m_dataDeinitMethod**（只有 resize/clear 才会），直接 deinit
     * 会把每个 role 里的 XVariant* 整块泄漏。
     * 先 clear（触发 xtw_itemDataDeinit 逐项释放）再收容器。 */
    XVector_clear_base((XVector*)&self->m_values);
    XClassDeinit(&self->m_values);
    self->m_view = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXTableWidgetItem_copy(XTableWidgetItem* self,
                                   const XTableWidgetItem* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTableWidgetItem_init(self);
    /* 不调用 XObject 的 EXClass_Copy：Qt 的 QTableWidgetItem **不是**
     * QObject（QObject 不可拷贝，qobject.h Q_DISABLE_COPY），本类型是按值
     * 语义的条目对象，拷贝只涉及 role 表/标量，继承链上的对象身份
     * （子对象、信号槽、线程亲和）一律不搬。
     * 本槽位对标 Qt `operator=`（qtablewidget.cpp:1536-1544）：只拷
     * values 与 itemFlags，**不碰** rtti（左侧 type 保留）、view 与选中
     * 态。拷贝构造语义（rtti(Type)、view(nullptr)、未选中）由
     * XTableWidgetItem_create_copy 在**新建的 Type 条目**上调用本槽位自然
     * 得到——与 Qt 的 `QTableWidgetItem(const QTableWidgetItem&)` 同构。 */
    XVector_clear_base((XVector*)&self->m_values);
    XClassDeinit(&self->m_values);
    xtw_itemValuesInit(self);
    XClassCopy(&self->m_values, &other->m_values);
    self->m_itemFlags = other->m_itemFlags;
}

static void VXTableWidgetItem_move(XTableWidgetItem* self,
                                   XTableWidgetItem* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTableWidgetItem_init(self);
    /* 同 copy：条目不继承 QObject 身份，值语义迁移不走 XObject::move。 */
    XVector_clear_base((XVector*)&self->m_values);
    XClassDeinit(&self->m_values);
    xtw_itemValuesInit(self);
    XClassMove(&self->m_values, &other->m_values);
    self->m_view = other->m_view;
    self->m_type = other->m_type;
    self->m_itemFlags = other->m_itemFlags;
    self->m_selected = other->m_selected;
    other->m_view = NULL;
    other->m_type = XTableWidgetItem_Type;
    other->m_itemFlags = XTW_DEFAULT_ITEM_FLAGS;
    other->m_selected = 0;
}

/** @brief 基类克隆实现（拷贝构造，m_view 置 NULL——对齐 Qt clone 语义）。 */
static XTableWidgetItem* VXTableWidgetItem_clone(const XTableWidgetItem* self)
{
    XTableWidgetItem* copy;
    if (!self) return NULL;
    copy = XTableWidgetItem_create_copy(self);
    if (copy) copy->m_view = NULL;
    return copy;
}

XVtable* XTableWidgetItem_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTableWidgetItem)
    XVTABLE_INHERIT_XCLASS(XObject);
    {
        void* table[] = { VXTableWidgetItem_clone };
        XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    }
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTableWidgetItem_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTableWidgetItem_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTableWidgetItem_move);
    return XVTABLE_DEFAULT;
}

void XTableWidgetItem_init(XTableWidgetItem* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XTableWidgetItem);
    xtw_itemValuesInit(self);
    self->m_view = NULL;
    self->m_type = XTableWidgetItem_Type;
    self->m_itemFlags = XTW_DEFAULT_ITEM_FLAGS;
    self->m_selected = 0;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
}

void XTableWidgetItem_init_type(XTableWidgetItem* self, int type)
{
    if (!self) return;
    XTableWidgetItem_init(self);
    self->m_type = type;
}

void XTableWidgetItem_init_text(XTableWidgetItem* self, const XString* text,
                                int type)
{
    if (!self) return;
    XTableWidgetItem_init(self);
    self->m_type = type;
    XTableWidgetItem_setText(self, text);
}

void XTableWidgetItem_init_iconText(XTableWidgetItem* self, const XIcon* icon,
                                    const XString* text, int type)
{
    if (!self) return;
    XTableWidgetItem_init(self);
    self->m_type = type;
    XTableWidgetItem_setIcon(self, icon);
    XTableWidgetItem_setText(self, text);
}

XTableWidgetItem* XTableWidgetItem_create_ex(XMemoryType memory)
{
    XTableWidgetItem* self =
        (XTableWidgetItem*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTableWidgetItem_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XTableWidgetItem* XTableWidgetItem_create(void)
{
    return XTableWidgetItem_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
}

XTableWidgetItem* XTableWidgetItem_create_text(const XString* text, int type)
{
    XTableWidgetItem* self = XTableWidgetItem_create();
    if (!self) return NULL;
    self->m_type = type;
    XTableWidgetItem_setText(self, text);
    return self;
}

XTableWidgetItem* XTableWidgetItem_create_utf8(const char* utf8, int type)
{
    XTableWidgetItem* self = XTableWidgetItem_create();
    if (!self) return NULL;
    self->m_type = type;
    XTableWidgetItem_setText_utf8(self, utf8);
    return self;
}

XTableWidgetItem* XTableWidgetItem_create_iconText(const XIcon* icon,
                                                   const XString* text,
                                                   int type)
{
    XTableWidgetItem* self = XTableWidgetItem_create();
    if (!self) return NULL;
    self->m_type = type;
    XTableWidgetItem_setIcon(self, icon);
    XTableWidgetItem_setText(self, text);
    return self;
}

XTableWidgetItem* XTableWidgetItem_create_copy(const XTableWidgetItem* other)
{
    XTableWidgetItem* self;
    if (!other) return NULL;
    self = XTableWidgetItem_create();
    if (!self) return NULL;
    XClassCopy(self, other);
    return self;
}

XTableWidgetItem* XTableWidgetItem_clone_base(const XTableWidgetItem* self)
{
    if (!self) return NULL;
    if (ISNULL(XClassGetVtable(self), "XTableWidgetItem")) return NULL;
    if (!XClassGetVirtualFunc(self, EXTableWidgetItem_Clone, void*)) {
        /* 未实现克隆槽：回落基类拷贝。 */
        return XTableWidgetItem_create_copy(self);
    }
    return XClassGetVirtualFunc(self, EXTableWidgetItem_Clone,
                                XTableWidgetItem*(*)(const XTableWidgetItem*))(
        self);
}

int XTableWidgetItem_type(const XTableWidgetItem* self)
{ return self ? self->m_type : 0; }

XTableWidget* XTableWidgetItem_tableWidget(const XTableWidgetItem* self)
{ return self ? self->m_view : NULL; }

int XTableWidgetItem_row(const XTableWidgetItem* self)
{
    if (!self || !self->m_view) return -1;
    return XTableWidget_indexFromItem(self->m_view, self, NULL);
}

int XTableWidgetItem_column(const XTableWidgetItem* self)
{
    if (!self || !self->m_view) return -1;
    return XTableWidget_indexFromItem(self->m_view, self, NULL) < 0
               ? -1
               : XTableWidget_column(self->m_view, self);
}

uint32_t XTableWidgetItem_flags(const XTableWidgetItem* self)
{ return self ? self->m_itemFlags : 0; }

/* 前向声明：setFlags 的 Qt 通知路径在下方定义（同 setData 共用）。 */
static void xtw_itemNotifyChanged(XTableWidgetItem* self, int role);

void XTableWidgetItem_setFlags(XTableWidgetItem* self, uint32_t flags)
{
    if (!self) return;
    /* 对齐 Qt QTableWidgetItem::setFlags（qtablewidget.cpp:1129-1135）：
     * 无条件写入并 `model->itemChanged(this)`——**同值也通知**
     * （Qt 无同值早退）。此前同值直接返回，场上"重复设同一 flags
     * 不发 itemChanged"与 Qt 不一致。 */
    self->m_itemFlags = flags;
    if (self->m_view) {
        xtw_itemNotifyChanged(self, -1);
        /* Qt 经 dataChanged 触发视图重绘；本库 itemChanged 信号不驱动
         * 视图失效，补一次显式 update 保持观感（同值也刷，Qt 同）。 */
        XWidget_update((XWidget*)self->m_view);
    }
}

bool XTableWidgetItem_isSelected(const XTableWidgetItem* self)
{
    XItemSelectionModel* sm;
    XTableWidget* view;
    int row;
    int col;
    if (!self) return false;
    /* [§B4 2026-11-15 / 复核修订] 对齐 Qt QTableWidgetItem::isSelected
     * （qtablewidget.cpp:1087-1096）：事实源是选择模型；条目位是它的
     * **派生镜像**。
     * **性能**（复核意见：从 O(1) 位测试退化成 O(R·C)）：旧实现在 false
     * 分支也调 XTableWidget_indexFromItem（条目矩阵无反向索引 ⇒ 全表地址
     * 扫描），「遍历所有条目判选中」因此是 O((R·C)²)（探针实测 4000 项
     * 315ms）。现在：镜像一旦建立（`m_selSnapshot` 非空 ⇒ 桥已至少收尾过
     * 一次、全表位已按模型刷齐），直接返回镜像位——O(1)，与 Qt 的
     * QItemSelectionModel 区间查询同量级。仅在镜像尚未建立（条目刚入表、
     * 从未有过选区变化）时才回落模型精确查询，保证语义正确。
     * 游离条目（view==NULL）恒 false（Qt 同）。 */
    view = self->m_view;
    if (!view) return false;
    if (view->m_selSnapshot) return self->m_selected != 0;
    sm = xtw_selModel(view);
    if (!sm) return self->m_selected != 0;
    row = XTableWidget_indexFromItem(view, self, &col);
    if (row < 0 || col < 0) return false;
    return XItemSelectionModel_isSelected(sm, row, col);
}

void XTableWidgetItem_setSelected(XTableWidgetItem* self, bool select)
{
    XTableWidget* view;
    int row;
    int col;
    if (!self) return;
    /* 对齐 Qt 6.8 QTableWidgetItem::setSelected（qtablewidget.cpp:1100-1112）：
     * **条目必须先插入表格**（view 非空）才可选中；游离条目是空操作。
     * 写入路径同样直走选择模型
     * `view->selectionModel()->select(index, Select|Deselect)`——模型随后经
     * 桥槽把结果镜像回条目位并发射 itemSelectionChanged（Qt 同走
     * QTableModel 的 dataChanged/选择信号链）。
     * 同值闸：目标态已成立时直接早退（Qt 的 select 对已选中为幂等、不发
     * 信号；本机 Qt 6.8.3 实测 setSelected(false) 于未选中格信号数 0）。 */
    view = self->m_view;
    if (!view) return;
    row = XTableWidget_indexFromItem(view, self, &col);
    if (row < 0 || col < 0) return;
    {
        XItemSelectionModel* sm = xtw_selModel(view);
        if (!sm) return;
        if (XItemSelectionModel_isSelected(sm, row, col) == select)
            return; /* 同值闸：不回环、不多发信号。 */
        ++view->m_selSyncDepth;
        (void)XItemSelectionModel_select(
            sm, row, col,
            select ? XItemSelectionModelSelectionFlag_Select
                   : XItemSelectionModelSelectionFlag_Deselect);
        xtw_selSyncDone(view, false);
        --view->m_selSyncDepth;
    }
}

const XVariant* XTableWidgetItem_data(const XTableWidgetItem* self, int role)
{
    const XTableWidgetItemData* d;
    /* 对齐 Qt：EditRole 归一化为 DisplayRole。 */
    if (role == XItemDataRole_EditRole) role = XItemDataRole_DisplayRole;
    d = xtw_itemSlot(self, role);
    return d ? d->value : NULL;
}

/* ==================== 条目 role 写入的表格同步 ==================== */

static void xtw_emitCellSignal(XTableWidget* self, size_t signal, int a, int b);
static void xtw_emitItemSignal(XTableWidget* self, size_t signal,
                               const XTableWidgetItem* item);

/** @brief 把条目 role 变化同步到内建模型并发射 itemChanged/cellChanged。
 *  @details 对齐 Qt `QTableWidgetItem::setData` → `QTableModel::itemChanged`
 *           → `QTableWidgetPrivate::emitItemChanged` 的链路。对
 *           DisplayRole 变化同时写模型文本，保证绘制/编辑/查找一致。 */
static void xtw_itemNotifyChanged(XTableWidgetItem* self, int role)
{
    XTableWidget* view;
    int row;
    int col;
    bool viaModel = false;
    if (!self) return;
    view = self->m_view;
    if (!view) return;
    row = XTableWidget_indexFromItem(view, self, &col);
    if (row < 0 || col < 0) return;
    if (role == XItemDataRole_DisplayRole) {
        if (view->m_model) {
            XAbstractItemModel_setData_2(view->m_model, row, col,
                                         XTableWidgetItem_text_toUtf8(self));
            /* 模型写入必发 dataChanged（XAbstractItemModel_setData 无同值
             * 早退）⇒ 视图桥 xtw_modelDataChangedSlot 已转发 itemChanged/
             * cellChanged。对齐 Qt：一次改动只发一次（qtablewidget.cpp:
             * 1631-1632 的 dataChanged 转发是**唯一**发射点），故此处不再
             * 重发，避免一次 setText 计两次 itemChanged。 */
            viaModel = true;
        }
    }
    if (viaModel) return;
    xtw_emitItemSignal(view, (size_t)XTableWidget_itemChanged_signal, self);
    xtw_emitCellSignal(view, (size_t)XTableWidget_cellChanged_signal, row, col);
}

/* ==================== 条目值语义助手（对标 Qt 的 QVariant 比较/转文本） ==================== */

/**
 * @brief 值类型分档（对标 Qt 6.8 qabstractitemmodel.cpp:804-828
 *        `typeOfVariant`）：0=整型族、1=浮点族、2=其他。
 * @details QTableWidgetItem::operator< 经 `variantLessThan`
 *          （qtablewidget.cpp:1441-1444 → qabstractitemmodel.cpp:828-839）：
 *          两值取**较宽**的档位，整型/浮点走数值比较，其他走
 *          `QString::localeAwareCompare`。
 */
static int xtw_itemValueClass(const XVariant* value)
{
    int type;
    if (!value) return 2; /* 缺失值按"其他"参与字符串比较（Qt UnknownType
                           * 的 toString() 为空串）。 */
    type = XVariant_type((XVariant*)value);
    switch (type) {
    case XVariantType_Uint8:
    case XVariantType_Uint16:
    case XVariantType_Uint32:
    case XVariantType_Uint64:
    case XVariantType_Int8:
    case XVariantType_Int16:
    case XVariantType_Int32:
    case XVariantType_Int64:
    case XVariantType_Bool:
    case XVariantType_Char:
    case XVariantType_UChar:
    case XVariantType_Int:
    case XVariantType_Size_t:
        return 0;
    case XVariantType_Float:
    case XVariantType_Double:
        return 1;
    default:
        return 2;
    }
}

/** @brief 同值判定（对齐 Qt `QVariant::operator==` 的常用分支）。
 * @details 字符串按内容比较（XString 内嵌于 XVariant，不能按裸字节比）；
 *          其余同类型同尺寸的 POD 按字节比较。 */
static bool xtw_itemVariantEquals(const XVariant* a, const XVariant* b)
{
    int type;
    size_t size;
    if (a == b) return true;
    if (!a || !b) return false;
    type = XVariant_type((XVariant*)a);
    if (type != XVariant_type((XVariant*)b)) return false;
    if (type == XVariantType_String) {
        const XString* sa = XString_fromVariant_const(a);
        const XString* sb = XString_fromVariant_const(b);
        if (!sa || !sb) return sa == sb;
        return XString_equals(sa, sb, XChar_CaseSensitive);
    }
    size = XVariant_dataSize((XVariant*)a);
    if (size != XVariant_dataSize((XVariant*)b)) return false;
    if (size == 0) return true;
    return XMemcmp(XVariant_data((XVariant*)a), XVariant_data((XVariant*)b),
                   size) == 0;
}

/** @brief 取值的文本视图（对标 QVariant::toString 的常用分支）。
 * @details String 直接借用；Bool 为 "true"/"false"（Qt 同）；QChar 族为
 *          单字符；整型/浮点按十进制格式化；缺失或其他类型为空串。
 *          临时串经 temp 承载，*tempInited 为真时调用方负责 XClassDeinit。
 */
static const XString* xtw_itemVariantAsString(const XVariant* value,
                                              XString* temp, bool* tempInited)
{
    int type;
    int cls;
    if (tempInited) *tempInited = false;
    XString_init(temp);
    if (tempInited) *tempInited = true;
    if (!value) {
        XString_assign_utf8(temp, "");
        return temp;
    }
    type = XVariant_type((XVariant*)value);
    if (type == XVariantType_String) {
        const XString* text = XString_fromVariant_const(value);
        if (text) {
            /* 借用值对象内的字符串：放弃刚建的临时对象。 */
            if (tempInited) *tempInited = false;
            XClassDeinit(temp);
            return text;
        }
        return temp;
    }
    if (type == XVariantType_Bool) {
        XString_assign_utf8(temp, XVariant_toInt(value) ? "true" : "false");
        return temp;
    }
    if (type == XVariantType_Char || type == XVariantType_UChar) {
        XString_assign_fmt_utf8(temp, "%c", XVariant_toInt(value));
        return temp;
    }
    cls = xtw_itemValueClass(value);
    if (cls == 0) {
        XString_assign_fmt_utf8(temp, "%lld",
                                (long long)XVariant_toInt64(value));
    } else if (cls == 1) {
        /* Qt QString::number(double) 缺省 'g' 6 位有效数字。 */
        XString_assign_fmt_utf8(temp, "%g", XVariant_toDouble(value));
    } else {
        XString_assign_utf8(temp, "");
    }
    return temp;
}

void XTableWidgetItem_setData(XTableWidgetItem* self, int role,
                              const XVariant* value)
{
    XTableWidgetItemData* d;
    if (!self) return;
    /* 对齐 Qt：EditRole 归一化为 DisplayRole。 */
    if (role == XItemDataRole_EditRole) role = XItemDataRole_DisplayRole;
    d = xtw_itemSlot(self, role);
    if (!value) {
        /* NULL 值 = 移除该角色数据。 */
        if (d) {
            XVector_remove_base(&self->m_values,
                                (int64_t)(d - (XTableWidgetItemData*)
                                             XVector_data(&self->m_values)), 1);
        }
        xtw_itemNotifyChanged(self, role);
        return;
    }
    if (d) {
        /* 对齐 Qt 的同值早退（qtablewidget.cpp:1399-1412：
         * `if (values[i].value == value) return;`）——重复写同一值不发
         * dataChanged/itemChanged/cellChanged。此前每次都重设并通知，
         * 一次 setText 相同文本也会多计一次 itemChanged。 */
        if (d->value && xtw_itemVariantEquals(d->value, value)) return;
        if (d->value) XClassDelete((XClass*)d->value);
        d->value = XVariant_create_copy(value);
    } else {
        XTableWidgetItemData fresh;
        fresh.role = role;
        fresh.value = XVariant_create_copy(value);
        if (!fresh.value) return;
        /* [根修 2026-11-03] push_back_1_base 走 m_dataCopyMethod
         * （xtw_itemDataCopy）**再深拷一份**入容器，
         * 因此 fresh.value 必须释放，否则每次 setData
         * 漏一个 XVariant 及其载荷（实测 192B/次）。 */
        if (!XVector_push_back_1_base(&self->m_values, &fresh)) {
            XClassDelete((XClass*)fresh.value);
            return;
        }
        XClassDelete((XClass*)fresh.value);
        fresh.value = NULL;
    }
    xtw_itemNotifyChanged(self, role);
}

/* ==================== DisplayRole 文本化（gap 295） ====================
 * [对齐依据] QTableWidgetItem::text() = data(DisplayRole).toString()
 * （qtablewidget.h:76-80）。本机 Qt 6.8.0 实测：DisplayRole 为 int 42 时
 * text()=="42"、double 3.5→"3.5"、bool true→"true"、
 * 3.14159265358979→"3.14159265358979"、1e-07→"1e-07"（double 用最短
 * 往返表示，QVariant::toString 口径）。旧实现只认 String 型 ⇒ 数值型
 * DisplayRole 渲染为空串。
 * [借用缓冲] text_toUtf8 返回 const char*（借用），数值转换需要落脚存储：
 * 用线程局部 4 槽环缓冲（同线程连续 4 次调用有效，GUI 单线程足够）；
 * 需要独立生命周期请用 text()（返回新副本）。 */

/** @brief 把 double 写成最短可往返的十进制文本（对齐 QVariant::toString）。
 *  @param value 值。 @param buf 输出缓冲。 @param cap 缓冲容量。
 *  @return 无。 */
static void xtw_formatDouble(double value, char* buf, size_t cap)
{
    int prec;
    for (prec = 1; prec <= 17; ++prec) {
        XSnprintf(buf, cap, "%.*g", prec, value);
        if (XStrtod(buf, NULL) == value) break;
    }
}

/** @brief 把 float 写成最短可往返的十进制文本。 */
static void xtw_formatFloat(float value, char* buf, size_t cap)
{
    int prec;
    for (prec = 1; prec <= 9; ++prec) {
        XSnprintf(buf, cap, "%.*g", prec, (double)value);
        if ((float)XStrtod(buf, NULL) == value) break;
    }
}

/**
 * @brief 按 XVariant 类型把 DisplayRole **非文本标量**转成 UTF-8 文本
 *        （对齐 QVariant::toString 的标量分支）。
 * @param v 值（借用）。
 * @param buf 输出缓冲。
 * @param cap 缓冲容量。
 * @return 可转换返回 true；**String 型不在此处理**（调用方整串借用/拷贝，
 *         避免定长缓冲截断）；容器/几何等无文本表示的类型返回 false
 *         （Qt 对无 toString 支持的 QVariant 返回空 QString，调用方按空串）。
 */
static bool xtw_variantToUtf8(const XVariant* v, char* buf, size_t cap)
{
    int type;
    if (!v || !buf || cap == 0) return false;
    type = XVariant_type((XVariant*)v); /* 只读查询，接口为非 const 形参。 */
    switch (type) {
    case XVariantType_String:
        /* 文本型不走定长缓冲（防截断）；调用方直接用 XString 借用/拷贝。 */
        return false;
    case XVariantType_Bool:
        XSnprintf(buf, cap, "%s",
                  XVariant_toBool(v) ? "true" : "false");
        return true;
    case XVariantType_Int8:
    case XVariantType_Int16:
    case XVariantType_Int32:
    case XVariantType_Int64:
    case XVariantType_Int:
        XSnprintf(buf, cap, "%lld",
                  (long long)XVariant_toInt64(v));
        return true;
    case XVariantType_Uint8:
    case XVariantType_Uint16:
    case XVariantType_Uint32:
    case XVariantType_Uint64:
    case XVariantType_Size_t:
        XSnprintf(buf, cap, "%llu",
                  (unsigned long long)XVariant_toUint64(v));
        return true;
    case XVariantType_Float:
        xtw_formatFloat(XVariant_toFloat(v), buf, cap);
        return true;
    case XVariantType_Double:
        xtw_formatDouble(XVariant_toDouble(v), buf, cap);
        return true;
    default:
        return false;
    }
}

XString* XTableWidgetItem_text(const XTableWidgetItem* self)
{
    const XVariant* v = XTableWidgetItem_data(self, XItemDataRole_DisplayRole);
    const XString* s;
    char conv[64];
    if (!v) return XString_create();
    /* 文本型走整串拷贝（不限长）；其余标量按 QVariant::toString 文本化。 */
    s = XString_fromVariant_const(v);
    if (s) return XString_create_copy(s);
    if (xtw_variantToUtf8(v, conv, sizeof(conv)))
        return XString_create_utf8(conv);
    return XString_create();
}

const XString* XTableWidgetItem_text_const(const XTableWidgetItem* self)
{
    const XVariant* v = XTableWidgetItem_data(self, XItemDataRole_DisplayRole);
    /* 借用形态只覆盖 String 型（无存储承载数值转换结果）；需要文本化的
     * 数值请用 XTableWidgetItem_text（返回值归调用方）。 */
    return v ? XString_fromVariant_const(v) : NULL;
}

const char* XTableWidgetItem_text_toUtf8(const XTableWidgetItem* self)
{
    const XVariant* v = XTableWidgetItem_data(self, XItemDataRole_DisplayRole);
    const XString* s;
    if (!v) return "";
    s = XString_fromVariant_const(v);
    if (s) {
        const char* utf8 = XString_toUtf8(s);
        return utf8 ? utf8 : "";
    }
    /* 数值等可文本化类型：写入线程局部 4 槽环缓冲（借用返回值的落脚点；
     * 同线程连续 4 次调用内有效，GUI 单线程足够；需独立生命周期用 text()）。 */
    {
        char* slot = g_xtwTextRing[g_xtwTextRingIdx];
        g_xtwTextRingIdx = (g_xtwTextRingIdx + 1u) % 4u;
        if (xtw_variantToUtf8(v, slot, sizeof(g_xtwTextRing[0])))
            return slot;
    }
    /* 无文本表示的类型（容器/几何等）按 Qt 口径给空串。 */
    return "";
}

void XTableWidgetItem_setText(XTableWidgetItem* self, const XString* text)
{
    XVariant* v;
    if (!self) return;
    if (!text) text = XTableWidgetItem_text_const(self);
    v = text ? XString_toVariant(text) : NULL;
    if (!v) {
        /* 空文本：移除 DisplayRole。 */
        XTableWidgetItem_setData(self, XItemDataRole_DisplayRole, NULL);
        return;
    }
    XTableWidgetItem_setData(self, XItemDataRole_DisplayRole, v);
    XClassDelete((XClass*)v);
}

void XTableWidgetItem_setText_utf8(XTableWidgetItem* self, const char* utf8)
{
    XVariant* v;
    if (!self) return;
    if (!utf8) utf8 = "";
    v = XString_toVariant_utf8(utf8);
    if (!v) return;
    XTableWidgetItem_setData(self, XItemDataRole_DisplayRole, v);
    XClassDelete((XClass*)v);
}

/* -------------------- 便捷 role 访问器 -------------------- */

/** @brief 读取文本型 role（新建副本）；无值返回空串对象。 */
static XString* xtw_itemRoleText(const XTableWidgetItem* self, int role)
{
    const XVariant* v = XTableWidgetItem_data(self, role);
    const XString* s = v ? XString_fromVariant_const(v) : NULL;
    if (!s) return XString_create();
    return XString_create_copy(s);
}

/** @brief 读取文本型 role 的 UTF-8 借用视图。 */
static const char* xtw_itemRoleUtf8(const XTableWidgetItem* self, int role)
{
    const XVariant* v = XTableWidgetItem_data(self, role);
    const XString* s = v ? XString_fromVariant_const(v) : NULL;
    const char* utf8;
    if (!s) return "";
    utf8 = XString_toUtf8(s);
    return utf8 ? utf8 : "";
}

/** @brief 写入文本型 role；text 为 NULL 时移除该 role。 */
static void xtw_itemSetRoleText(XTableWidgetItem* self, int role,
                                const XString* text)
{
    XVariant* v;
    if (!self) return;
    if (!text) {
        XTableWidgetItem_setData(self, role, NULL);
        return;
    }
    v = XString_toVariant(text);
    if (!v) return;
    XTableWidgetItem_setData(self, role, v);
    XClassDelete((XClass*)v);
}

/** @brief 写入文本型 role（UTF-8）；utf8 为 NULL 时移除该 role。 */
static void xtw_itemSetRoleUtf8(XTableWidgetItem* self, int role,
                                const char* utf8)
{
    XVariant* v;
    if (!self) return;
    if (!utf8) {
        XTableWidgetItem_setData(self, role, NULL);
        return;
    }
    v = XString_toVariant_utf8(utf8);
    if (!v) return;
    XTableWidgetItem_setData(self, role, v);
    XClassDelete((XClass*)v);
}

const XIcon* XTableWidgetItem_icon(const XTableWidgetItem* self)
{
    const XVariant* v = XTableWidgetItem_data(self,
                                              XItemDataRole_DecorationRole);
    const XIcon* const* slot;
    if (!v || XVariant_type((XVariant*)v) != XVariantType_Ptr)
        return NULL;
    slot = (const XIcon* const*)XVariant_data((XVariant*)v);
    return slot ? *slot : NULL;
}

void XTableWidgetItem_setIcon(XTableWidgetItem* self, const XIcon* icon)
{
    XVariant* v;
    if (!self) return;
    if (!icon) {
        XTableWidgetItem_setData(self, XItemDataRole_DecorationRole, NULL);
        return;
    }
    v = XVariant_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (void*)&icon,
                           sizeof(const XIcon*), XVariantType_Ptr);
    if (!v) return;
    XTableWidgetItem_setData(self, XItemDataRole_DecorationRole, v);
    XClassDelete((XClass*)v);
}

XString* XTableWidgetItem_statusTip(const XTableWidgetItem* self)
{ return xtw_itemRoleText(self, XItemDataRole_StatusTipRole); }
const char* XTableWidgetItem_statusTip_toUtf8(const XTableWidgetItem* self)
{ return xtw_itemRoleUtf8(self, XItemDataRole_StatusTipRole); }
void XTableWidgetItem_setStatusTip(XTableWidgetItem* self, const XString* tip)
{ xtw_itemSetRoleText(self, XItemDataRole_StatusTipRole, tip); }
void XTableWidgetItem_setStatusTip_utf8(XTableWidgetItem* self, const char* utf8)
{ xtw_itemSetRoleUtf8(self, XItemDataRole_StatusTipRole, utf8); }

XString* XTableWidgetItem_toolTip(const XTableWidgetItem* self)
{ return xtw_itemRoleText(self, XItemDataRole_ToolTipRole); }
const char* XTableWidgetItem_toolTip_toUtf8(const XTableWidgetItem* self)
{ return xtw_itemRoleUtf8(self, XItemDataRole_ToolTipRole); }
void XTableWidgetItem_setToolTip(XTableWidgetItem* self, const XString* tip)
{ xtw_itemSetRoleText(self, XItemDataRole_ToolTipRole, tip); }
void XTableWidgetItem_setToolTip_utf8(XTableWidgetItem* self, const char* utf8)
{ xtw_itemSetRoleUtf8(self, XItemDataRole_ToolTipRole, utf8); }

XString* XTableWidgetItem_whatsThis(const XTableWidgetItem* self)
{ return xtw_itemRoleText(self, XItemDataRole_WhatsThisRole); }
const char* XTableWidgetItem_whatsThis_toUtf8(const XTableWidgetItem* self)
{ return xtw_itemRoleUtf8(self, XItemDataRole_WhatsThisRole); }
void XTableWidgetItem_setWhatsThis(XTableWidgetItem* self, const XString* text)
{ xtw_itemSetRoleText(self, XItemDataRole_WhatsThisRole, text); }
void XTableWidgetItem_setWhatsThis_utf8(XTableWidgetItem* self, const char* utf8)
{ xtw_itemSetRoleUtf8(self, XItemDataRole_WhatsThisRole, utf8); }

const XFont* XTableWidgetItem_font(const XTableWidgetItem* self)
{
    const XVariant* v = XTableWidgetItem_data(self, XItemDataRole_FontRole);
    const XFont* const* slot;
    if (!v || XVariant_type((XVariant*)v) != XVariantType_Ptr)
        return NULL;
    /* Ptr 型值的载荷是「指针值本身」：XVariant_data 返回载荷地址，
     * 必须再解引用一层（此前直接强转返回的是指向指针槽的地址，
     * 调用方读到的是栈/堆上的指针变量地址而不是传入的字体对象）。 */
    slot = (const XFont* const*)XVariant_data((XVariant*)v);
    return slot ? *slot : NULL;
}

void XTableWidgetItem_setFont(XTableWidgetItem* self, const XFont* font)
{
    XVariant* v;
    if (!self) return;
    if (!font) {
        XTableWidgetItem_setData(self, XItemDataRole_FontRole, NULL);
        return;
    }
    v = XVariant_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (void*)&font,
                           sizeof(const XFont*), XVariantType_Ptr);
    if (!v) return;
    XTableWidgetItem_setData(self, XItemDataRole_FontRole, v);
    XClassDelete((XClass*)v);
}

int XTableWidgetItem_textAlignment(const XTableWidgetItem* self)
{
    const XVariant* v = XTableWidgetItem_data(self,
                                              XItemDataRole_TextAlignmentRole);
    return v ? XVariant_toInt(v) : 0;
}

void XTableWidgetItem_setTextAlignment(XTableWidgetItem* self, int alignment)
{
    XVariant* v;
    if (!self) return;
    if (alignment == 0) {
        XTableWidgetItem_setData(self, XItemDataRole_TextAlignmentRole, NULL);
        return;
    }
    v = XVariant_create_int(alignment);
    if (!v) return;
    XTableWidgetItem_setData(self, XItemDataRole_TextAlignmentRole, v);
    XClassDelete((XClass*)v);
}

bool XTableWidgetItem_background(const XTableWidgetItem* self, XColor* out)
{
    const XVariant* v = XTableWidgetItem_data(self,
                                              XItemDataRole_BackgroundRole);
    if (!v || !out) return false;
    if (v->m_type != XVariantType_Size && v->m_dataSize != sizeof(XColor))
        return false;
    XMemmove(out, XVariant_data((XVariant*)v), sizeof(XColor));
    return true;
}

void XTableWidgetItem_setBackground(XTableWidgetItem* self, const XColor* color)
{
    XVariant* v;
    if (!self) return;
    if (!color) {
        XTableWidgetItem_setData(self, XItemDataRole_BackgroundRole, NULL);
        return;
    }
    v = XVariant_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (void*)color,
                           sizeof(XColor), XVariantType_Size);
    if (!v) return;
    XTableWidgetItem_setData(self, XItemDataRole_BackgroundRole, v);
    XClassDelete((XClass*)v);
}

bool XTableWidgetItem_foreground(const XTableWidgetItem* self, XColor* out)
{
    const XVariant* v = XTableWidgetItem_data(self,
                                              XItemDataRole_ForegroundRole);
    if (!v || !out) return false;
    if (v->m_dataSize != sizeof(XColor)) return false;
    XMemmove(out, XVariant_data((XVariant*)v), sizeof(XColor));
    return true;
}

void XTableWidgetItem_setForeground(XTableWidgetItem* self, const XColor* color)
{
    XVariant* v;
    if (!self) return;
    if (!color) {
        XTableWidgetItem_setData(self, XItemDataRole_ForegroundRole, NULL);
        return;
    }
    v = XVariant_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (void*)color,
                           sizeof(XColor), XVariantType_Size);
    if (!v) return;
    XTableWidgetItem_setData(self, XItemDataRole_ForegroundRole, v);
    XClassDelete((XClass*)v);
}

int XTableWidgetItem_checkState(const XTableWidgetItem* self)
{
    const XVariant* v = XTableWidgetItem_data(self,
                                              XItemDataRole_CheckStateRole);
    return v ? XVariant_toInt(v) : XItemCheckState_Unchecked;
}

void XTableWidgetItem_setCheckState(XTableWidgetItem* self, int state)
{
    XVariant* v;
    if (!self) return;
    /* 对齐 Qt 6.8 QTableWidgetItem::setCheckState（qtablewidget.h:141-147）：
     * 原样存 enum，不做 0..2 范围校验（本机 Qt 6.8.0 实测
     * setCheckState((Qt::CheckState)7) 后 checkState()==7、-3 同理；
     * gap 297）。渲染只消费 Checked/PartiallyChecked 两态。 */
    v = XVariant_create_int(state);
    if (!v) return;
    XTableWidgetItem_setData(self, XItemDataRole_CheckStateRole, v);
    XClassDelete((XClass*)v);
}

bool XTableWidgetItem_sizeHint(const XTableWidgetItem* self, XSize* out)
{
    const XVariant* v = XTableWidgetItem_data(self, XItemDataRole_SizeHintRole);
    if (!v || !out) return false;
    if (v->m_dataSize != sizeof(XSize)) return false;
    XMemmove(out, XVariant_data((XVariant*)v), sizeof(XSize));
    return true;
}

void XTableWidgetItem_setSizeHint(XTableWidgetItem* self, const XSize* size)
{
    XVariant* v;
    if (!self) return;
    /* 对齐 Qt（qtablewidget.h `setSizeHint`：`size.isValid()` 才存值）：
     * QSize::isValid() == `w >= 0 && h >= 0` ⇒ (0,0) 是**有效**尺寸，
     * 只有负宽/负高才清空 SizeHintRole。此前把 0 也当无效清除。 */
    if (!size || size->width < 0 || size->height < 0) {
        XTableWidgetItem_setData(self, XItemDataRole_SizeHintRole, NULL);
        return;
    }
    v = XVariant_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (void*)size,
                           sizeof(XSize), XVariantType_Size);
    if (!v) return;
    XTableWidgetItem_setData(self, XItemDataRole_SizeHintRole, v);
    XClassDelete((XClass*)v);
}

bool XTableWidgetItem_lessThan(const XTableWidgetItem* lhs,
                               const XTableWidgetItem* rhs)
{
    const XVariant* a = XTableWidgetItem_data(lhs, XItemDataRole_DisplayRole);
    const XVariant* b = XTableWidgetItem_data(rhs, XItemDataRole_DisplayRole);
    int classA = xtw_itemValueClass(a);
    int classB = xtw_itemValueClass(b);
    int cls = classA > classB ? classA : classB;
    /* 对齐 Qt 的 variantLessThan（qabstractitemmodel.cpp:828-839，
     * QTableWidgetItem::operator< 在 qtablewidget.cpp:1441-1444 调用）：
     *   整型档 → 双方 toLongLong 比较；
     *   浮点档 → 双方 toReal 比较；
     *   其他档 → QString::localeAwareCompare（**区域感知**，不是字节序）。
     * 缺失 DisplayRole 的条目：Qt 的 QVariant 无效 ⇒ toString() 为空串，
     * 参与字符串比较（排在任意非空字符串之前），此前实现同此；
     * 但**数值条目此前被当成"无值"**（XString_fromVariant_const 只认
     * String），int 3 与 int 5 比较双方都得 NULL ⇒ 判为相等，
     * std::sort 下顺序不确定——本轮按 Qt 分档修正。 */
    if (cls == 0)
        return XVariant_toInt64(a) < XVariant_toInt64(b);
    if (cls == 1)
        return XVariant_toDouble(a) < XVariant_toDouble(b);
    {
        XString tempA;
        XString tempB;
        bool initedA = false;
        bool initedB = false;
        const XString* sa = xtw_itemVariantAsString(a, &tempA, &initedA);
        const XString* sb = xtw_itemVariantAsString(b, &tempB, &initedB);
        int32_t cmp = XString_localeAwareCompare(sa, sb);
        if (initedA) XClassDeinit(&tempA);
        if (initedB) XClassDeinit(&tempB);
        return cmp < 0;
    }
}

/* ==================== 序列化（对齐 QTableWidgetItem::write/read） ==================== */

/** @brief 写出单个 role 的 XVariant。
 *  @details 线格式：`quint32 type` + `qint64 byteLen` + 负载。
 *           - `XVariantType_String`：负载按 XDataStream_writeString 的
 *             文本格式（UTF-16 码元序列），与其余类型区分开，避免把
 *             XString 对象头当裸字节写出去。
 *           - 其余类型：`XVariant_data/dataSize` 原样拷贝（均为主值
 *             语义的 POD 或已注册按值承载的类型）。
 *  @return 成功 true。 */
static bool xtw_writeVariant(XDataStream* stream, const XVariant* value)
{
    int type;
    size_t size;
    void* data;
    if (!stream || !value) return false;
    type = XVariant_type((XVariant*)value);
    if (!XDataStream_writeInt32(stream, type)) return false;
    if (type == XVariantType_String) {
        const XString* str = XString_fromVariant_const(value);
        return XDataStream_writeString(stream, str);
    }
    size = XVariant_dataSize((XVariant*)value);
    data = XVariant_data((XVariant*)value);
    if (!XDataStream_writeInt64(stream, (int64_t)size)) return false;
    if (size > 0 && data)
        return XDataStream_writeRawData(stream, (const char*)data,
                                        (int64_t)size) == (int64_t)size;
    return true;
}

/** @brief 读回单个 role 的 XVariant（与 xtw_writeVariant 对称）。
 *  @return 新建 XVariant，归调用方以 XClassDelete 释放；失败返回 NULL。 */
static XVariant* xtw_readVariant(XDataStream* stream)
{
    int32_t type = 0;
    int64_t size = 0;
    if (!stream) return NULL;
    if (!XDataStream_readInt32(stream, &type)) return NULL;
    if (type == XVariantType_String) {
        XString* str = XDataStream_readString(stream);
        XVariant* out;
        if (!str) return NULL;
        out = XString_toVariant(str);
        XClassDelete((XClass*)str);
        return out;
    }
    if (!XDataStream_readInt64(stream, &size)) return NULL;
    if (size < 0) return NULL;
    if (size == 0) return XVariant_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             NULL, 0, type);
    {
        char* buf = (char*)XMalloc_System((size_t)size);
        XVariant* out;
        if (!buf) return NULL;
        if (XDataStream_readRawData(stream, buf, size) != size) {
            XFree_System(buf);
            return NULL;
        }
        out = XVariant_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, buf,
                                 (size_t)size, type);
        XFree_System(buf);
        return out;
    }
}

bool XTableWidgetItem_write(const XTableWidgetItem* self, XDataStream* stream)
{
    size_t i;
    size_t n;
    if (!self || !stream) return false;
    n = XVector_size_base(&self->m_values);
    /* roleCount 先写：读取端据此迭代（对齐 Qt 的 values 列表先写长度）。 */
    if (!XDataStream_writeUInt32(stream, (uint32_t)n)) return false;
    for (i = 0; i < n; ++i) {
        const XTableWidgetItemData* d =
            (const XTableWidgetItemData*)XVector_at_base(&self->m_values,
                                                         (int64_t)i);
        if (!d) continue;
        if (!XDataStream_writeInt32(stream, d->role)) return false;
        if (!d->value) {
            /* 该 role 无值：用 NULL 类型占位（读取端跳过）。 */
            if (!XDataStream_writeInt32(stream, XVariantType_NULL)) {
                return false;
            }
            if (!XDataStream_writeInt64(stream, 0)) return false;
            continue;
        }
        if (!xtw_writeVariant(stream, d->value)) return false;
    }
    if (!XDataStream_writeInt32(stream, self->m_type)) return false;
    if (!XDataStream_writeUInt32(stream, self->m_itemFlags)) return false;
    return true;
}

bool XTableWidgetItem_read(XTableWidgetItem* self, XDataStream* stream)
{
    uint32_t count = 0;
    uint32_t i;
    int32_t type = 0;
    uint32_t flags = 0;
    if (!self || !stream) return false;
    /* 先读到临时容器：任何一步失败都不破坏原条目（保证「失败时不变」）。 */
    if (!XDataStream_readUInt32(stream, &count)) return false;
    /* 防御：role 数量上限（每 role 至少 8 字节，超过 4096 视为损坏）。 */
    if (count > 4096u) {
        XDataStream_setStatus(stream,
                              (int)XDataStream_Status_SizeLimitExceeded);
        return false;
    }
    {
        XVector fresh;
        bool ok = true;
        int32_t itemType = 0;
        uint32_t itemFlags = 0;
        XVector_init(&fresh, sizeof(XTableWidgetItemData), false);
        XContainerSetDataCopyMethod(&fresh, xtw_itemDataCopy);
        XContainerSetDataDeinitMethod(&fresh, xtw_itemDataDeinit);
        for (i = 0; i < count && ok; ++i) {
            int32_t role = 0;
            XVariant* value;
            XTableWidgetItemData entry;
            if (!XDataStream_readInt32(stream, &role)) { ok = false; break; }
            value = xtw_readVariant(stream);
            if (!value) { ok = false; break; }
            if (XVariant_type(value) == XVariantType_NULL) {
                /* NULL 占位：该 role 无值，跳过。 */
                XClassDelete((XClass*)value);
                continue;
            }
            entry.role = role;
            entry.value = value;
            if (!XVector_push_back_1_base(&fresh, &entry)) {
                XClassDelete((XClass*)value);
                ok = false;
                break;
            }
            /* push_back_1_base 经 m_dataCopyMethod（xtw_itemDataCopy）深拷
             * 入容器；临时 value 的壳必须释放，否则每读成功一个 role
             * 漏一个 XVariant（本机实测 3 role ≈ 472B/次）。 */
            XClassDelete((XClass*)value);
        }
        if (ok && !XDataStream_readInt32(stream, &itemType)) ok = false;
        if (ok && !XDataStream_readUInt32(stream, &itemFlags)) ok = false;
        if (!ok) {
            /* XClassDeinit 不回调元素 deinit ⇒ 先 clear 逐项释放已读入的
             * role 值，再收容器（同 VXTableWidgetItem_deinit 口径）。 */
            XVector_clear_base(&fresh);
            XClassDeinit(&fresh);
            return false;
        }
        /* 提交：整体替换旧 role 表与标量（m_view 保持）。旧值必须逐个
         * 释放——XClassDeinit 不回调元素 deinit，直接 deinit 会漏掉旧
         * role 的 XVariant。 */
        XVector_clear_base(&self->m_values);
        XClassDeinit(&self->m_values);
        XClassMove(&self->m_values, &fresh);
        XClassDeinit(&fresh); /* 资源已转出，外壳收尾。 */
        self->m_type = itemType;
        self->m_itemFlags = itemFlags;
        type = itemType;
        flags = itemFlags;
        (void)type;
        (void)flags;
    }
    return true;
}

/* ==================== XTableWidgetSelectionRange ==================== */

XTableWidgetSelectionRange XTableWidgetSelectionRange_create(
    int top, int left, int bottom, int right)
{
    XTableWidgetSelectionRange r;
    r.m_top = top;
    r.m_left = left;
    r.m_bottom = bottom;
    r.m_right = right;
    return r;
}

int XTableWidgetSelectionRange_topRow(const XTableWidgetSelectionRange* self)
{ return self ? self->m_top : -1; }
int XTableWidgetSelectionRange_bottomRow(const XTableWidgetSelectionRange* self)
{ return self ? self->m_bottom : -1; }
int XTableWidgetSelectionRange_leftColumn(const XTableWidgetSelectionRange* self)
{ return self ? self->m_left : -1; }
int XTableWidgetSelectionRange_rightColumn(const XTableWidgetSelectionRange* self)
{ return self ? self->m_right : -1; }

int XTableWidgetSelectionRange_rowCount(const XTableWidgetSelectionRange* self)
{
    int n;
    if (!self) return 0;
    n = self->m_bottom - self->m_top + 1;
    return n > 0 ? n : 0;
}

int XTableWidgetSelectionRange_columnCount(const XTableWidgetSelectionRange* self)
{
    int n;
    if (!self) return 0;
    n = self->m_right - self->m_left + 1;
    return n > 0 ? n : 0;
}

bool XTableWidgetSelectionRange_equals(const XTableWidgetSelectionRange* lhs,
                                       const XTableWidgetSelectionRange* rhs)
{
    if (!lhs || !rhs) return lhs == rhs;
    return lhs->m_top == rhs->m_top && lhs->m_left == rhs->m_left &&
           lhs->m_bottom == rhs->m_bottom && lhs->m_right == rhs->m_right;
}

/* ==================== XTableWidget 内部辅助 ==================== */

/** @brief 释放一个条目（含 role 数据）；NULL 安全。 */
static void xtw_itemDelete(XTableWidgetItem* item)
{
    if (!item) return;
    item->m_view = NULL;
    XClassDelete((XClass*)item);
}

/** @brief 确保行指针数组容量（保留现有行内容；成功 true）。 */
static bool xtw_ensureRows(XTableWidget* self, int rows)
{
    XTableWidgetItem*** p;
    int cap = self->m_rowCapacity > 0 ? self->m_rowCapacity : 4;
    if (rows <= self->m_rowCapacity) return true;
    while (cap < rows) cap *= 2;
    p = (XTableWidgetItem***)XRealloc_System(
        self->m_cells, sizeof(XTableWidgetItem**) * (size_t)cap);
    if (!p) return false;
    /* 扩容区必须清零：realloc 复用块里的旧内容是野行指针（曾实测为已释放
       字符串的残余字节），后续插入/释放路径读到即破坏堆。 */
    XMemset(p + self->m_rows, 0,
            sizeof(XTableWidgetItem**) * (size_t)(cap - self->m_rows));
    self->m_cells = p;
    self->m_rowCapacity = (int16_t)cap;
    return true;
}

/** @brief 确保列容量：扩列宽数组/表头/每行单元格数组（成功 true）。
 *         任一分配失败不推进 m_colCapacity（已扩容的数组只会偏大，
 *         安全；下次调用对失败数组重试），调用方不得按新列下标写入。 */
static bool xtw_ensureCols(XTableWidget* self, int cols)
{
    int* w;
    XString** hh;
    int cap = self->m_base.m_colCapacity > 0 ? self->m_base.m_colCapacity : 4;
    int i;
    bool ok = true;
    if (cols <= self->m_base.m_colCapacity) return true;
    while (cap < cols) cap *= 2;
    w = (int*)XRealloc_System(self->m_base.m_colWidths,
                              sizeof(int) * (size_t)cap);
    if (w) {
        self->m_base.m_colWidths = w;
        for (i = self->m_base.m_colCapacity; i < cap; ++i)
            self->m_base.m_colWidths[i] = XTW_DEFAULT_COL_WIDTH;
    } else ok = false;
    hh = (XString**)XRealloc_System(self->m_hHeaders,
                                    sizeof(XString*) * (size_t)cap);
    if (hh) {
        self->m_hHeaders = hh;
        for (i = self->m_base.m_colCapacity; i < cap; ++i)
            self->m_hHeaders[i] = NULL;
    } else ok = false;
    for (i = 0; i < self->m_rows; ++i) {
        XTableWidgetItem** row = self->m_cells ? self->m_cells[i] : NULL;
        XTableWidgetItem** nr = (XTableWidgetItem**)XRealloc_System(
            row, sizeof(XTableWidgetItem*) * (size_t)cap);
        if (nr) {
            self->m_cells[i] = nr;
            XMemset(nr + self->m_base.m_colCapacity, 0,
                    sizeof(XTableWidgetItem*) *
                        (size_t)(cap - self->m_base.m_colCapacity));
        } else ok = false;
    }
    if (!ok) return false;
    self->m_base.m_colCapacity = (int16_t)cap;
    return true;
}

/** @brief 分配一个全零（无条目）单元格行。 */
static XTableWidgetItem** xtw_newRow(int cols)
{
    XTableWidgetItem** row;
    if (cols <= 0) cols = 1;
    row = (XTableWidgetItem**)XMalloc_System(
        sizeof(XTableWidgetItem*) * (size_t)cols);
    if (row) XMemset(row, 0, sizeof(XTableWidgetItem*) * (size_t)cols);
    return row;
}

/** @brief 确保垂直表头指针数组容量（新增区域清零；成功 true）。 */
static bool xtw_ensureVHeaders(XTableWidget* self, int count)
{
    int cap;
    XString** vh;
    if (count <= self->m_vHeaderCapacity) return true;
    cap = self->m_vHeaderCapacity > 0 ? self->m_vHeaderCapacity : 256;
    while (cap < count) cap *= 2;
    vh = (XString**)XRealloc_System(self->m_vHeaders,
                                    sizeof(XString*) * (size_t)cap);
    if (!vh) return false;
    /* 新增区域必须清零：调用方以 m_vHeaders[i] 是否为 NULL 判定
       首次创建；realloc 的未初始化内存是野指针，直接复用会崩溃。 */
    XMemset(vh + self->m_vHeaderCapacity, 0,
            sizeof(XString*) * (size_t)(cap - self->m_vHeaderCapacity));
    self->m_vHeaders = vh;
    self->m_vHeaderCapacity = (int16_t)cap;
    return true;
}

/** @brief 取单元格条目（越界返回 NULL）。 */
static XTableWidgetItem* xtw_cell(const XTableWidget* self, int row, int col)
{
    if (!self || row < 0 || row >= self->m_rows ||
        col < 0 || col >= self->m_columns || !self->m_cells) return NULL;
    return self->m_cells[row] ? self->m_cells[row][col] : NULL;
}

/** @brief 取单元格槽位（越界返回 NULL；槽位内可能为 NULL 条目）。 */
static XTableWidgetItem** xtw_cellSlot(const XTableWidget* self, int row,
                                       int col)
{
    if (!self || row < 0 || row >= self->m_rows ||
        col < 0 || col >= self->m_columns || !self->m_cells) return NULL;
    return self->m_cells[row] ? &self->m_cells[row][col] : NULL;
}

/* ==================== 挂载部件表助手（对象拥有语义） ==================== */

/** @brief 按 (row,col) 查找挂载条目下标；无则返回 -1。 */
static int xtw_cwFind(const XTableWidget* self, int row, int col)
{
    int i;
    for (i = 0; i < self->m_cellWidgetCount; ++i)
        if (self->m_cellWidgets[i].row == row &&
            self->m_cellWidgets[i].column == col) return i;
    return -1;
}

/** @brief 确保挂载条目容量至少 need（成功 true）。 */
static bool xtw_cwReserve(XTableWidget* self, int need)
{
    XTableWidgetCellWidget* p;
    int cap = self->m_cellWidgetCapacity > 0 ? self->m_cellWidgetCapacity : 4;
    if (need <= self->m_cellWidgetCapacity) return true;
    while (cap < need) cap *= 2;
    p = (XTableWidgetCellWidget*)XRealloc_System(
        self->m_cellWidgets, sizeof(XTableWidgetCellWidget) * (size_t)cap);
    if (!p) return false;
    self->m_cellWidgets = p;
    self->m_cellWidgetCapacity = (int16_t)cap;
    return true;
}

/** @brief 删除下标 i 处挂载条目（后续条目前移；部件由调用方决定是否释放）。 */
static void xtw_cwRemoveAt(XTableWidget* self, int i)
{
    for (; i < self->m_cellWidgetCount - 1; ++i)
        self->m_cellWidgets[i] = self->m_cellWidgets[i + 1];
    self->m_cellWidgetCount--;
}

/** @brief 行插入后平移挂载表：行号 >= row 的条目行号加一。 */
static void xtw_cwOnRowInserted(XTableWidget* self, int row)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_cellWidgetCount; ++i)
        if (self->m_cellWidgets[i].row >= row) self->m_cellWidgets[i].row++;
}

/** @brief 列插入后平移挂载表：列号 >= column 的条目列号加一。 */
static void xtw_cwOnColumnInserted(XTableWidget* self, int column)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_cellWidgetCount; ++i)
        if (self->m_cellWidgets[i].column >= column)
            self->m_cellWidgets[i].column++;
}

/** @brief 行删除后平移挂载表：row 行挂件解除并释放（所有权归表），
 *         >row 行条目行号减一。 */
static void xtw_cwOnRowRemoved(XTableWidget* self, int row)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i) {
        if (self->m_cellWidgets[i].row == row) {
            XWidget* old = self->m_cellWidgets[i].widget;
            xtw_cwRemoveAt(self, i);
            if (old) XObject_deleteLater((XObject*)old);
        } else if (self->m_cellWidgets[i].row > row) {
            self->m_cellWidgets[i].row--;
        }
    }
}

/** @brief 列删除后平移挂载表：column 列挂件解除并释放（同上），
 *         >column 列条目列号减一。 */
static void xtw_cwOnColumnRemoved(XTableWidget* self, int column)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i) {
        if (self->m_cellWidgets[i].column == column) {
            XWidget* old = self->m_cellWidgets[i].widget;
            xtw_cwRemoveAt(self, i);
            if (old) XObject_deleteLater((XObject*)old);
        } else if (self->m_cellWidgets[i].column > column) {
            self->m_cellWidgets[i].column--;
        }
    }
}

/** @brief 单元格数据随列插入右移：[column..oldCols) → [column+1..oldCols+1)，
 *         新列清零。对齐 QTableWidget::insertColumn 的空列插入语义；
 *         调用方已先把 m_columns +1。 */
static void xtw_shiftColumnsRight(XTableWidget* self, int column)
{
    int i;
    int c;
    int oldCols = self->m_columns - 1; /* 增列前的逻辑列数。 */
    for (i = 0; i < self->m_rows; ++i) {
        XTableWidgetItem** row = self->m_cells[i];
        if (!row) continue;
        /* 从 oldCols-1 起移：半开区间 [column..oldCols) 整体 +1，最高写
           row[oldCols] == 行容量-1（insertColumn 已 ensureCols 扩到
           增列后列数）。旧写法从 c=oldCols 起步，首笔就写 row[oldCols+1]
           ——越过行容量 1 槽（ASan heap-buffer-overflow WRITE 8 实证，
           2026-11-15 B2R 返工跨域修）。 */
        for (c = oldCols - 1; c >= column; --c)
            row[c + 1] = row[c];
        row[column] = NULL;
    }
}

/** @brief 单元格数据随列删除左移：释放被删列条目，[column+1..oldCols) →
 *         [column..oldCols-1)，末位清零。调用方已先把 m_columns -1。 */
static void xtw_shiftColumnsLeft(XTableWidget* self, int column)
{
    int i;
    int c;
    int oldCols = self->m_columns + 1; /* 减列前的逻辑列数。 */
    for (i = 0; i < self->m_rows; ++i) {
        XTableWidgetItem** row = self->m_cells[i];
        if (!row) continue;
        xtw_itemDelete(row[column]);
        for (c = column + 1; c < oldCols; ++c)
            row[c - 1] = row[c];
        row[oldCols - 1] = NULL;
    }
}

/** @brief 行数收缩：行号 >= keepRows 的挂件解除并释放。 */
static void xtw_cwTruncateRows(XTableWidget* self, int keepRows)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i) {
        if (self->m_cellWidgets[i].row < keepRows) continue;
        {
            XWidget* old = self->m_cellWidgets[i].widget;
            xtw_cwRemoveAt(self, i);
            if (old) XObject_deleteLater((XObject*)old);
        }
    }
}

/** @brief 列数收缩：列号 >= keepColumns 的挂件解除并释放。 */
static void xtw_cwTruncateColumns(XTableWidget* self, int keepColumns)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i) {
        if (self->m_cellWidgets[i].column < keepColumns) continue;
        {
            XWidget* old = self->m_cellWidgets[i].widget;
            xtw_cwRemoveAt(self, i);
            if (old) XObject_deleteLater((XObject*)old);
        }
    }
}

/** @brief 全部挂件解除并释放（clear/clearContents 模型 reset 路径）。 */
static void xtw_cwReleaseAll(XTableWidget* self)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i) {
        XWidget* old = self->m_cellWidgets[i].widget;
        xtw_cwRemoveAt(self, i);
        if (old) XObject_deleteLater((XObject*)old);
    }
}

/** @brief 发射 (int,int) 双整型信号（cellClicked/currentCellChanged）。 */
static void xtw_emitCellSignal(XTableWidget* self, size_t signal, int a, int b)
{
    int va = a;
    int vb = b;
    XVarList* args = XVarList_Create(XVar(int, va), XVar(int, vb));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 item 载荷信号。 */
static void xtw_emitItemSignal(XTableWidget* self, size_t signal,
                               const XTableWidgetItem* item)
{
    XVarList* args = XVarList_Create(XVar(const XTableWidgetItem*, item));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

static void xtw_updateCell(const XTableWidget* self, int row, int column);
/** @brief 整行区间标脏（行移动后内容整体位移；见 xtw_moveRow 调用点）。 */
static void xtw_updateRowSpan(const XTableWidget* self, int rowA, int rowB);

/** @brief 模型 dataChanged→itemChanged/cellChanged 转发槽。
 *  对齐 QTableWidget 构造的 connections：model dataChanged→
 *  QTableWidgetPrivate::emitItemChanged（qtablewidget.cpp:1631-1632），
 *  槽内先 itemChanged(item) 后 cellChanged(row,col)（:1691-1697）。 */
static void xtw_modelDataChangedSlot(XObject* receiver, XVarList* args)
{
    XTableWidget* self = (XTableWidget*)receiver;
    int row = -1;
    int col = -1;
    if (!self || !args) return;
    /* [args_N 遮蔽陷阱] 解包整段放块内，同块取值拷出。 */
    {
        XVarList_args_2(args, int, vr, int, vc);
        row = vr;
        col = vc;
    }
    /* 模型→条目同步：编辑提交链（基类 setItemText→模型 setData）只落
     * 模型，而条目才是本控件的显示事实源（对齐 Qt：QTableModel 即唯一
     * 存储；本框架为「条目+模型」双存储）。桥在此补同步（差量幂等），
     * 且**先于信号发射**——cellChanged 消费方（落库槽）读到的必须是新值。 */
    if (self->m_model) {
        XTableWidgetItem* item = xtw_cell(self, row, col);
        const char* text = XAbstractItemModel_data_2(self->m_model, row, col);
        if (!item && text && text[0]) {
            /* 空格被写值：按 Qt 语义创建新条目。 */
            item = XTableWidgetItem_create_utf8(text, XTableWidgetItem_Type);
            if (item) {
                XTableWidgetItem** slot = xtw_cellSlot(self, row, col);
                if (slot) {
                    item->m_view = self;
                    *slot = item;
                } else {
                    xtw_itemDelete(item);
                    item = NULL;
                }
            }
        } else if (item && text) {
            const char* cur = XTableWidgetItem_text_toUtf8(item);
            if (!cur || strcmp(cur, text) != 0) {
                /* 直写 role 但抑制回环：调用方已同步过模型。 */
                XTableWidgetItem_setText_utf8(item, text);
                xtw_updateCell(self, row, col);
            }
        }
    }
    xtw_emitItemSignal(self, (size_t)XTableWidget_itemChanged_signal,
                       xtw_cell(self, row, col));
    xtw_emitCellSignal(self, (size_t)XTableWidget_cellChanged_signal,
                       row, col);
}

/** @brief 发射无载荷信号。 */
static void xtw_emit0(XTableWidget* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/* ==================== 生命周期 ==================== */

static void VXTableWidget_deinit(XTableWidget* self);
static void VXTableWidget_copy(XTableWidget* self, const XTableWidget* other);
static void VXTableWidget_move(XTableWidget* self, XTableWidget* other);

XVtable* XTableWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTableWidget)
    XVTABLE_INHERIT_XCLASS(XTableView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTableWidget_deinit);
    /* [根修 2026-11-03] 表格层必须自己重载 copy/move：继承的
     * VXTableView_copy 只搬视图层属性与平行状态表，不碰 m_cells
     * （条目矩阵）/ 表头文本 / 内建模型——此前 XClassCopy 一个表格
     * 会把全部单元格数据丢掉，move 还会泄漏目标的旧矩阵。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTableWidget_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTableWidget_move);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_tableWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_tableWidget_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_tableWidget_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VX_tableWidget_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_VisualRect,
                             VX_tableWidget_visualRect);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_ShouldEdit,
                             VX_tableWidget_shouldEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VX_tableWidget_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy,
                             VX_tableWidget_scrollContentsBy);
    /* [K-73a 2026-11-14] 视口事件覆写：滚动条显隐等改变视口几何时重推
     * 列布局可用长（实现见 resizeEvent 旁，VX_tableWidget_viewportEvent；
     * 接管基类 XTableView 的裸视口宽推送——宿主可用长扣行号列宽）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ViewportEvent,
                             VX_tableWidget_viewportEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_tableWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_tableWidget_mouseMoveEvent);
    /* [P0-33 2026-11-12] 排序本体虚槽覆写：XTableWidget 的条目矩阵
     * （m_cells）与内建模型（m_model）是两份存储，只排模型会让 m_cells
     * 与模型错位 ⇒ 必须排条目矩阵（XTableWidget_sortItems 内部同时回写
     * 排序列/序与表头指示器）。基类缺省实现排模型
     * （VXTableView_sortByColumn）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXTableView_SortByColumn,
                             VXTableWidget_sortByColumn);
    return XVTABLE_DEFAULT;
}

/** @brief 释放一行单元格（含条目对象）。 */
static void xtw_freeRowItems(XTableWidgetItem** row, int cols)
{
    int i;
    if (!row) return;
    for (i = 0; i < cols; ++i) {
        if (row[i]) {
            xtw_itemDelete(row[i]);
            row[i] = NULL;
        }
    }
    XFree_System(row);
}

static void VXTableWidget_deinit(XTableWidget* self)
{
    int i;
    if (!self) return;
    /* [§B4 2026-11-15] 先断选择模型桥（句柄在模型析构前注销；模型本身由
     * 基类 XAbstractItemView 持有并随后释放），再释放选中集合快照位图。 */
    xtw_selDisarm(self);
    if (self->m_selSnapshot) {
        XClassDelete((XClass*)self->m_selSnapshot);
        self->m_selSnapshot = NULL;
    }
    if (self->m_cells) {
        for (i = 0; i < self->m_rows; ++i)
            xtw_freeRowItems(self->m_cells[i], self->m_base.m_colCapacity);
        XFree_System(self->m_cells);
        self->m_cells = NULL;
    }
    if (self->m_hHeaders) {
        for (i = 0; i < self->m_base.m_colCapacity; ++i) {
            if (self->m_hHeaders[i]) {
                XClassDelete((XClass*)self->m_hHeaders[i]);
                self->m_hHeaders[i] = NULL;
            }
        }
        XFree_System(self->m_hHeaders);
        self->m_hHeaders = NULL;
    }
    if (self->m_vHeaders) {
        for (i = 0; i < self->m_vHeaderCapacity; ++i) {
            if (self->m_vHeaders[i]) {
                XClassDelete((XClass*)self->m_vHeaders[i]);
                self->m_vHeaders[i] = NULL;
            }
        }
        XFree_System(self->m_vHeaders);
        self->m_vHeaders = NULL;
    }
    if (self->m_model) {
        XClassDelete((XClass*)self->m_model);
        self->m_model = NULL;
    }
    xtw_itemDelete(self->m_itemPrototype);
    self->m_itemPrototype = NULL;
    if (self->m_cellWidgets) {
        /* 对齐 Qt：挂件随视图析构释放（父控件机制亦会回收，此处仅释放
         * 承载数组；部件本身经 deleteLater 走对象生命周期）。 */
        for (i = 0; i < self->m_cellWidgetCount; ++i)
            if (self->m_cellWidgets[i].widget)
                XObject_deleteLater((XObject*)self->m_cellWidgets[i].widget);
        XFree_System(self->m_cellWidgets);
        self->m_cellWidgets = NULL;
    }
    self->m_cellWidgetCount = 0;
    self->m_cellWidgetCapacity = 0;
    XClass_Deinit_Parent(XTableView, (XTableView*)self);
}

/** @brief 内建模型与单元格维度同步的公共收尾。 */
static void xtw_syncModelDimension(XTableWidget* self)
{
    if (self && self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

/** @brief 行/列表头段数与段尺寸同步。
 *  @note 段尺寸必须与 m_colWidths 同源：表头是段尺寸事实源（段移动/
 *        sectionPosition/logicalIndexAt 与表头带命中都按段几何），而本表
 *        绘制/命中按 m_colWidths。只同步段数会让段几何停在
 *        defaultSectionSize（水平缺省 100），与绘制列宽分叉——表头带
 *        双击/单击命中的列与实际画出的列错位。 */
static void xtw_syncHeaders(XTableWidget* self)
{
    int i;
    if (!self) return;
    if (self->m_base.m_verticalHeader)
        XHeaderView_setCount(self->m_base.m_verticalHeader, self->m_rows);
    if (self->m_base.m_horizontalHeader) {
        XHeaderView* hh = self->m_base.m_horizontalHeader;
        XHeaderView_setCount(hh, self->m_columns);
        for (i = 0; i < self->m_columns; ++i) {
            if (!self->m_base.m_colWidths) break;
            if (XHeaderView_sectionSize(hh, i) == self->m_base.m_colWidths[i])
                continue;
            /* 隐藏段写入尺寸表项（show 后恢复）；resizeSection 对 >0 值
             * 钳制到 [min,max]，与 XTableView_setColumnWidth 同口径。 */
            XHeaderView_resizeSection(hh, i, self->m_base.m_colWidths[i]);
        }
    }
    if (self->m_base.m_verticalHeader && self->m_base.m_rowHeight > 0) {
        XHeaderView* vh = self->m_base.m_verticalHeader;
        for (i = 0; i < self->m_rows; ++i) {
            if (XHeaderView_sectionSize(vh, i) ==
                (int)self->m_base.m_rowHeight)
                continue;
            XHeaderView_resizeSection(vh, i, (int)self->m_base.m_rowHeight);
        }
    }
}

/** @brief 释放本表全部单元格矩阵与表头文本（保留容量字段复位）。
 *  @note 条目对象、表头 XString 均归表格所有，逐项 XClassDelete。 */
static void xtw_releaseCellsAndHeaders(XTableWidget* self)
{
    int i;
    if (!self) return;
    if (self->m_cells) {
        for (i = 0; i < self->m_rows; ++i)
            xtw_freeRowItems(self->m_cells[i], self->m_base.m_colCapacity);
        XFree_System(self->m_cells);
        self->m_cells = NULL;
    }
    self->m_rowCapacity = 0;
    if (self->m_hHeaders) {
        for (i = 0; i < self->m_base.m_colCapacity; ++i) {
            if (self->m_hHeaders[i]) {
                XClassDelete((XClass*)self->m_hHeaders[i]);
                self->m_hHeaders[i] = NULL;
            }
        }
    }
    if (self->m_vHeaders) {
        for (i = 0; i < self->m_vHeaderCapacity; ++i) {
            if (self->m_vHeaders[i]) {
                XClassDelete((XClass*)self->m_vHeaders[i]);
                self->m_vHeaders[i] = NULL;
            }
        }
    }
    self->m_rows = 0;
    self->m_columns = 0;
}

/** @brief 深拷贝：基类拷贝后重建条目矩阵、表头文本与挂载部件条目。
 *  @details 条目按 `XTableWidgetItem_create_copy` 逐格深拷贝（role 表
 *           独立）；新表头文本经 `XString_create_copy`。
 *           内建模型不共享（各自持有），拷贝后按维度重建。
 *           挂载部件（widget）为借用指针：只复制承载条目，不复制部件
 *           本身，部件归属仍是原表——对齐 Qt「indexWidget 不随模型拷贝」
 *           语义，此处显式不复制（保留空表更安全）。 */
static void VXTableWidget_copy(XTableWidget* self, const XTableWidget* other)
{
    int r, c;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTableWidget_init(self, NULL, 0);
    /* 先做基类拷贝（视图层属性 + 平行状态表 + 头段尺寸）。 */
    XClass_Parent(XTableView, EXClass_Copy,
                  void(*)(XTableView*, const XTableView*))(
                      (XTableView*)self, (const XTableView*)other);
    /* 基类拷贝不动行列计数（那是表格层字段），先清空本表已有内容。 */
    xtw_releaseCellsAndHeaders(self);
    self->m_rows = 0;
    self->m_columns = 0;
    self->m_selectionRow = other->m_selectionRow;
    self->m_selectionColumn = other->m_selectionColumn;
    self->m_enteredRow = other->m_enteredRow;
    self->m_enteredColumn = other->m_enteredColumn;
    self->m_headerHeight = other->m_headerHeight;
    self->m_headerWidth = other->m_headerWidth;
    self->m_vHeaderVisible = other->m_vHeaderVisible;
    self->m_itemPrototype = NULL;
    if (other->m_itemPrototype)
        self->m_itemPrototype = XTableWidgetItem_create_copy(
            other->m_itemPrototype);
    if (other->m_rows <= 0 || other->m_columns <= 0) {
        self->m_rows = other->m_rows;
        self->m_columns = other->m_columns;
        return;
    }
    /* 重建维度（走 setter 保证容量/表头段/模型同步）。 */
    /* 注：上面已把 m_rows/m_columns 归零，此处 setter 才会生效——
     * setter 对「新旧值相等」直接 early-return，若沿用源维度
     * 则矩阵根本不建（copy 读不到任何条目）。 */
    XTableWidget_setRowCount(self, other->m_rows);
    XTableWidget_setColumnCount(self, other->m_columns);
    if (self->m_rows != other->m_rows || self->m_columns != other->m_columns)
        return; /* 分配失败：保持部分结果，不越界写。 */
    /* 逐格深拷贝条目。 */
    for (r = 0; r < other->m_rows; ++r) {
        for (c = 0; c < other->m_columns; ++c) {
            const XTableWidgetItem* src = xtw_cell(other, r, c);
            if (!src) continue;
            {
                XTableWidgetItem* dst = XTableWidgetItem_create_copy(src);
                if (dst) XTableWidget_setItem(self, r, c, dst);
            }
        }
    }
    /* 表头文本深拷贝。 */
    for (c = 0; c < other->m_columns && other->m_hHeaders &&
                c < other->m_base.m_colCapacity; ++c) {
        if (!other->m_hHeaders[c]) continue;
        if (c < self->m_base.m_colCapacity && self->m_hHeaders) {
            if (!self->m_hHeaders[c])
                self->m_hHeaders[c] = XString_create();
            if (self->m_hHeaders[c])
                XClassCopy(self->m_hHeaders[c], other->m_hHeaders[c]);
        }
    }
    for (r = 0; r < other->m_rows && other->m_vHeaders &&
                r < other->m_vHeaderCapacity; ++r) {
        if (!other->m_vHeaders[r]) continue;
        if (!xtw_ensureVHeaders(self, r + 1)) break;
        if (!self->m_vHeaders[r])
            self->m_vHeaders[r] = XString_create();
        if (self->m_vHeaders[r])
            XClassCopy(self->m_vHeaders[r], other->m_vHeaders[r]);
    }
}

/** @brief 移动语义：基类移动后转移条目矩阵/表头/容量，源归空。
 *  @note  先释放目标自有资源（否则整块泄漏），再接管源指针。 */
static void VXTableWidget_move(XTableWidget* self, XTableWidget* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTableWidget_init(self, NULL, 0);
    XClass_Parent(XTableView, EXClass_Move,
                  void(*)(XTableView*, XTableView*))(
                      (XTableView*)self, (XTableView*)other);
    /* 释放目标旧内容（含旧内建模型之外的全部自有资源）。 */
    xtw_releaseCellsAndHeaders(self);
    if (self->m_cellWidgets) {
        XFree_System(self->m_cellWidgets);
        self->m_cellWidgets = NULL;
    }
    self->m_cellWidgetCount = 0;
    self->m_cellWidgetCapacity = 0;
    xtw_itemDelete(self->m_itemPrototype);
    self->m_itemPrototype = NULL;
    /* 接管源资源。 */
    self->m_cells = other->m_cells;
    self->m_hHeaders = other->m_hHeaders;
    self->m_vHeaders = other->m_vHeaders;
    self->m_cellWidgets = other->m_cellWidgets;
    self->m_itemPrototype = other->m_itemPrototype;
    self->m_rows = other->m_rows;
    self->m_columns = other->m_columns;
    self->m_rowCapacity = other->m_rowCapacity;
    self->m_vHeaderCapacity = other->m_vHeaderCapacity;
    self->m_selectionRow = other->m_selectionRow;
    self->m_selectionColumn = other->m_selectionColumn;
    self->m_enteredRow = other->m_enteredRow;
    self->m_enteredColumn = other->m_enteredColumn;
    self->m_cellWidgetCount = other->m_cellWidgetCount;
    self->m_cellWidgetCapacity = other->m_cellWidgetCapacity;
    self->m_headerHeight = other->m_headerHeight;
    self->m_headerWidth = other->m_headerWidth;
    self->m_vHeaderVisible = other->m_vHeaderVisible;
    /* 源归空（可安全析构）。 */
    other->m_cells = NULL;
    other->m_hHeaders = NULL;
    other->m_vHeaders = NULL;
    other->m_cellWidgets = NULL;
    other->m_itemPrototype = NULL;
    other->m_rows = 0;
    other->m_columns = 0;
    other->m_rowCapacity = 0;
    other->m_vHeaderCapacity = 0;
    other->m_cellWidgetCount = 0;
    other->m_cellWidgetCapacity = 0;
    /* 内建模型：各表自有，不转移（源表仍可独立使用）。 */
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

void XTableWidget_init(XTableWidget* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XTableView_init(&self->m_base, parent, flags);
    self->m_model = XAbstractItemModel_create();
    if (self->m_model)
        XAbstractItemView_setModel_base(&self->m_base.m_base, self->m_model);
    /* 对齐 Qt：model dataChanged→itemChanged/cellChanged 转发连接
       （qtablewidget.cpp:1631-1632）；槽首参=receiver=本表格。 */
    if (self->m_model)
        XObject_connect((XObject*)self->m_model,
                          (size_t)XAbstractItemModel_dataChanged_signal,
                          (XObject*)self, xtw_modelDataChangedSlot,
                          XConnectionType_Direct);
    /* [K-68 残留 b 2026-11-14] 对标 Qt：horizontalHeader::geometriesChanged
     * → QTableView::updateGeometries（qtableview.cpp:1394-1395）——
     * QAbstractItemView::updateGeometries 尾段承载 editor/index-widget
     * （setCellWidget 挂件）几何。本库段移动/调宽/段数都发射
     * geometriesChanged ⇒ 拖拽换位后挂件重摆到**视觉**位置
     * （visualItemRect 已走本类 visualRect 虚槽的视觉序几何）。
     * 只挂内建表头；setHorizontalHeader 换头由 Qt 的 setHorizontalHeader
     * 重接（qtableview.cpp:1394-1395），本类未覆写该入口。**XTableWidget
     * 实例上换头零调用点**（全库 XTableView_setHorizontalHeader 的 9 处
     * 调用都在裸 XTableView 实例上：Test/XGuiDemo/xgui_demo_apitest_views.c:
     * 3118/3123/3126——注意同文件 :3155 另有同名 `XTableWidget table;`，
     * 但那是 :3151 起的第 8 节代码块、与换头调用的 :2935 裸视图块互不相干；
     * Test/QtAlign/XTableViewAlignTest.c:357/372/607/615/622/635 全在
     * XTableView* tv 上。裸视图无本钩、不受影响）——登记差异，不改行为。 */
    if (self->m_base.m_horizontalHeader)
        XObject_connect((XObject*)self->m_base.m_horizontalHeader,
                          XSignal(XHeaderView_geometriesChanged_signal),
                          (XObject*)self, xtwHeaderGeometriesChanged,
                          XConnectionType_Direct);
    XClassSetVtable(self, XTableWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_base.m_base.m_currentRow = -1;
    self->m_base.m_base.m_currentColumn = -1;
    self->m_base.m_rowHeight = XTW_DEFAULT_ROW_HEIGHT;
    self->m_headerHeight = XTW_HEADER_H;
    self->m_headerWidth = XTW_HEADER_W;
    self->m_vHeaderVisible = 1;
    self->m_base.m_gridVisible = 1;
    self->m_base.m_sortOrder = 0;
    self->m_enteredRow = -2;
    self->m_enteredColumn = -2;
    self->m_selectionRow = -1;
    self->m_selectionColumn = -1;
    self->m_itemPrototype = NULL;
    /* [§B4 2026-11-15] 选择模型双向桥接线：对标 Qt QTableWidget 构造里的
     * 选择信号接线（qtablewidget.cpp:1634-1637：currentChanged →
     * emitCurrentItemChanged、selectionChanged → itemSelectionChanged）。
     * 连接用 Unique 去重，重复调用不产生第二份（XSignalSlot.c:259-267）。 */
    xtw_selArm(self);
    /* [K-73] 建表即推送列布局可用长（可用口径与 Stretch 分配器一致；
     * 引擎只在 stretchLastSection 开启时消费）。 */
    xtw_pushStretchViewport(self);
}

XTableWidget* XTableWidget_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags)
{
    XTableWidget* self = (XTableWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTableWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XTableWidget* XTableWidget_create_size(XMemoryType memory, int rows,
                                       int columns, XWidget* parent,
                                       XWidgetFlags flags)
{
    XTableWidget* self = XTableWidget_create_ex(memory, parent, flags);
    if (!self) return NULL;
    /* 对齐 Qt QTableWidget(rows, columns, parent)：构造即设定维度，不触发
     * rowsInserted/columnsInserted 等结构变更信号。 */
    if (rows > 0) XTableWidget_setRowCount(self, rows);
    if (columns > 0) XTableWidget_setColumnCount(self, columns);
    return self;
}

/* ==================== 尺寸 ==================== */

XAbstractItemModel* XTableWidget_model(const XTableWidget* self)
{
    return self ? self->m_model : NULL;
}

void XTableWidget_setVerticalHeaderVisible(XTableWidget* self, bool visible)
{
    if (!self) return;
    if (self->m_vHeaderVisible == (visible ? 1u : 0u)) return;
    self->m_vHeaderVisible = visible ? 1u : 0u;
    /* 占宽变了 ⇒ 内容尺寸与滚动范围要重算（隐藏后整表宽一列的位置）。 */
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    /* [K-73] 行号列显隐改变列布局可用长 ⇒ 重推（表头引擎随即重排末段）。 */
    xtw_pushStretchViewport(self);
}

void XTableWidget_setRowCount(XTableWidget* self, int rows)
{
    int i;
    /* 对齐 Qt（qtablewidget.cpp:353）：负值或同值直接返回。 */
    if (!self || rows < 0 || rows == self->m_rows) return;
    if (rows > self->m_rows) {
        int start;
        /* 扩容失败不得按旧容量下标写新行（越界写且行块脱离登记无法
           回收）：直接放弃本次增长，保持原行数与模型一致。 */
        if (!xtw_ensureRows(self, rows)) return;
        if (!xtw_ensureCols(self, self->m_columns > 0 ? self->m_columns : 1))
            return;
        start = self->m_rows;
        for (i = start; i < rows; ++i) {
            self->m_cells[i] = xtw_newRow(self->m_base.m_colCapacity > 0 ?
                                         self->m_base.m_colCapacity : 1);
            if (!self->m_cells[i]) {
                /* 新行分配失败：回收本轮已建空行（单元格尚为空），
                   m_rows 不变，全部状态可供下次调用重试。 */
                while (i > start) {
                    --i;
                    xtw_freeRowItems(self->m_cells[i],
                                     self->m_base.m_colCapacity);
                    self->m_cells[i] = NULL;
                }
                return;
            }
        }
    } else {
        /* 收缩：删除尾部行的条目。对齐 Qt：被删条目直接 delete。 */
        for (i = rows; i < self->m_rows; ++i) {
            xtw_freeRowItems(self->m_cells[i], self->m_base.m_colCapacity);
            self->m_cells[i] = NULL;
        }
        xtw_cwTruncateRows(self, rows);
        /* 行头条目随行删除（对齐 QTableModel::removeRows）。 */
        for (i = rows; self->m_vHeaders && i < self->m_vHeaderCapacity; ++i) {
            if (self->m_vHeaders[i]) {
                XClassDelete((XClass*)self->m_vHeaders[i]);
                self->m_vHeaders[i] = NULL;
            }
        }
    }
    {
        int oldRows = self->m_rows;
        bool rebuilt;
        self->m_rows = (int16_t)rows;
        /* [§B4 返工④⑤] 收缩=删除尾部区间：选区裁剪 + 当前格就近迁移走
         * 与删行同一套路径（Qt QTableModel::setRowCount(收缩) =
         * removeRows(rows, rc-rows)，qtablewidget.cpp:353-361）。
         * ① Reshape 先把快照重排到新维度布局（扩容时快照唯一跟随点；
         *    收缩时先裁越界位，与后续收口幂等）；
         * ② trim 裁剪选择模型 + ③ 当前格就近迁移在同一事务内**一次收口**
         *    （信号合并：Qt 一次 begin/endRemoveRows 只发一次
         *    currentChanged，值即最终迁移值；拆开发会先发一个「迁移前」
         *    的收敛值）。旧视图直写 clamp（rows-1、不发信号）已删。 */
        xtw_selSnapshotReshape(self, oldRows, self->m_columns);
        rebuilt = false;
        if (rows < oldRows) {
            ++self->m_selSyncDepth;
            rebuilt = xtw_selTrimAxis(self, 0, rows);
            xtw_selCurrentRangeRemoved(self, 0, rows, oldRows - 1);
            xtw_selSyncDone(self, rebuilt);
            --self->m_selSyncDepth;
        }
    }
    xtw_syncHeaders(self);
    xtw_updateContentSize(self);
    /* 行数变化改变内容高/滚动范围：挂件随视口重摆（滚入恢复显示、
       滚出藏起），与 scrollContentsBy 同一口径。 */
    xtw_cellWidgetRelayout(self);
    /* [对齐 Qt 2026-11-03] 行数变化 ⇒ 条目尺寸可能变化，同步 singleStep。 */
    XAbstractItemView_syncScrollSteps(&self->m_base.m_base);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

int XTableWidget_rowCount(const XTableWidget* self)
{ return self ? self->m_rows : 0; }

void XTableWidget_setColumnCount(XTableWidget* self, int columns)
{
    int i;
    /* 对齐 Qt（qtablewidget.cpp:364）：负值或同值直接返回。 */
    if (!self || columns < 0 || columns == self->m_columns) return;
    if (!xtw_ensureRows(self, self->m_rows > 0 ? self->m_rows : 1)) return;
    if (columns > self->m_columns) {
        /* 列容量未扩成（m_colCapacity 未推进）时不得清零未扩的行块：
           保持原列数，状态可供下次调用重试。 */
        if (!xtw_ensureCols(self, columns)) return;
        for (i = 0; i < self->m_rows; ++i) {
            if (self->m_cells[i] == NULL) {
                /* 行块按全局列容量分配（同 setRowCount 口径）：
                   deinit/clear 按 m_colCapacity 释放每行。 */
                self->m_cells[i] = xtw_newRow(self->m_base.m_colCapacity);
                if (!self->m_cells[i]) return; /* 行块分配失败：保持原列数。 */
            }
            XMemset(&self->m_cells[i][self->m_columns], 0,
                    sizeof(XTableWidgetItem*) *
                        (size_t)(columns - self->m_columns));
        }
    } else {
        /* 收缩：删除尾部列的条目与列头条目（对齐 QTableModel::removeColumns）。 */
        for (i = 0; i < self->m_rows; ++i) {
            int c;
            XTableWidgetItem** row = self->m_cells[i];
            if (!row) continue;
            for (c = columns; c < self->m_columns; ++c) {
                xtw_itemDelete(row[c]);
                row[c] = NULL;
            }
        }
        for (i = columns; self->m_hHeaders && i < self->m_base.m_colCapacity;
             ++i) {
            if (self->m_hHeaders[i]) {
                XClassDelete((XClass*)self->m_hHeaders[i]);
                self->m_hHeaders[i] = NULL;
            }
        }
        xtw_cwTruncateColumns(self, columns);
    }
    {
        int oldCols = self->m_columns;
        bool rebuilt;
        self->m_columns = (int16_t)columns;
        /* [§B4 返工④⑤] 同 setRowCount：Reshape 快照到新布局（列数变必
         * 错位）→ 收缩时 trim + 当前格就近迁移在同一事务一次收口（信号
         * 合并，Qt removeColumns 语义）。旧视图直写 clamp 已删。 */
        xtw_selSnapshotReshape(self, self->m_rows, oldCols);
        rebuilt = false;
        if (columns < oldCols) {
            ++self->m_selSyncDepth;
            rebuilt = xtw_selTrimAxis(self, 1, columns);
            xtw_selCurrentRangeRemoved(self, 1, columns, oldCols - 1);
            xtw_selSyncDone(self, rebuilt);
            --self->m_selSyncDepth;
        }
    }
    xtw_syncHeaders(self);
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

int XTableWidget_columnCount(const XTableWidget* self)
{ return self ? self->m_columns : 0; }

/* ==================== 列宽模式辅助 ==================== */

/** @brief Stretch 模式：按内容区宽均分列宽（视口宽-行头宽）。
 *  @details 对齐 Qt QHeaderViewPrivate::resizeSections 的全局 Stretch 池
 *           分支（useGlobalMode，qheaderview.cpp:3552-3595 池统计与
 *           hint/floor/余数、3619-3626 视觉序应用）：
 *           ① **余数按视觉序逐段前置分摊**（pixelReminder 从视觉首段起
 *           每段 +1 直至分尽，:3624-3626）——旧口径把整份余数收给逻辑
 *           末列属有意偏差，已按 Qt 消除（本机 6.8.3 探针：638 视口 4 段
 *           = 160/160/159/159；旧口径是 159/159/159/161）；
 *           ② 段长下限 = max(余量/段数, minimumSectionSize)（:3592-3593）；
 *           ③ 余量 <= 0 整体跳过、保现宽（:3591 门；旧口径把 avail 夹到
 *           列数强分，已撤）；
 *           ④ 快照末段取 max(stretchSectionLength, lastSectionSize)
 *           （:3619-3623）——快照仅在 stretchLastSection 开启时受信任
 *           （qheaderview_p.h:71），经 XHeaderView_lastSectionSnapshot
 *           取 K-73 引擎态，快照逻辑段被放置时才生效。
 *           段尺寸写 header（事实源）并镜像到视图数组（本框架绘制/
 *           定位/内容宽读数组）；快照段走 setSectionSize 直写，绕开
 *           resizeSection 的末段快照维护钩（Qt 池应用走
 *           createSectionItems、不经 resizeSection ⇒ 快照不被拉伸值
 *           覆写，与引擎池路径同款口径）。恒等映射（未拖拽）下除余数
 *           分布外与旧口径逐位一致。 */
static void xtw_stretchColumnsToViewport(XTableWidget* self)
{
    XHeaderView* hh;
    XWidget* viewport;
    int avail;
    int count;
    int v;
    int stretchLen;
    int reminder;
    int snapLogical = -1;
    int snapSize = -1;
    if (!self || self->m_columns <= 0) return;
    hh = self->m_base.m_horizontalHeader;
    if (!hh) return;
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (!viewport) return;
    /* 可见列计数（隐藏列不参与拉伸池；对标 isVisualIndexHidden 跳过）。 */
    count = 0;
    for (v = 0; v < self->m_columns; ++v)
        if (!xtw_colHidden(self, v)) ++count;
    if (count <= 0) return;
    avail = XWidget_width(viewport) - self->m_headerWidth;
    /* Qt :3591 门：无余量整体跳过（池成员保现宽；不把 avail 夹到列数）。 */
    if (avail <= 0) return;
    stretchLen = avail / count;
    if (stretchLen < XHeaderView_minimumSectionSize(hh))
        stretchLen = XHeaderView_minimumSectionSize(hh); /* :3593 floor */
    reminder = avail % count;
    /* 末段下限（:3619-3623）：快照逻辑段被放置时 qMax 快照宽；无快照
     * （stretchLast 关闭/未立账）两输出为 -1 ⇒ 下限不生效。 */
    XHeaderView_lastSectionSnapshot(hh, &snapLogical, &snapSize);
    /* 视觉序逐段应用（对标 :3604-3648 外层视觉序循环；换算后的列是
     * 逻辑列，与绘制/命中同一映射入口 xtw_visualToLogical）。 */
    for (v = 0; v < self->m_columns; ++v) {
        int i = xtw_visualToLogical(self, v);
        int w;
        int old;
        if (i < 0 || i >= self->m_columns) continue;
        if (xtw_colHidden(self, i)) continue;
        w = stretchLen;
        if (i == snapLogical && snapSize > w) w = snapSize;
        if (reminder > 0) { ++w; --reminder; } /* 余数前置分摊 :3624-3626 */
        if (self->m_base.m_colWidths[i] == w) continue;
        old = self->m_base.m_colWidths[i];
        self->m_base.m_colWidths[i] = w;
        if (i == snapLogical)
            XHeaderView_setSectionSize(hh, i, w); /* 快照段直写（见头注） */
        else
            XHeaderView_resizeSection(hh, i, w);
        /* 差量同步内容宽（同 XTableView_setColumnWidth 根修口径）。 */
        XAbstractScrollArea_setContentSize(
            (XAbstractScrollArea*)self,
            ((XAbstractScrollArea*)self)->m_contentWidth + (w - old),
            ((XAbstractScrollArea*)self)->m_contentHeight);
    }
    XWidget_update((XWidget*)self);
}

/* [K-73 stretchLastSection 2026-11-14] 把列布局可用长推给表头引擎。
 * 对标 Qt：QHeaderView 自持 QAbstractScrollArea 视口、表头 Resize 事件即
 * resizeSections()（qheaderview.cpp:2817-2827），stretchLastSection 的
 * 「末段 = 视口长 − 其余可见段合计」因此生效；本表头是隐藏子件（表头带
 * 由表格自绘），视口在表格侧 ⇒ 宿主在尺寸事实变化时推送同一事实。
 * 推送点：init（建表即可用）、resizeEvent（视口宽变）、行号列显隐
 * （偏移变）。列数/列宽/隐藏变化的引擎触发走表头侧
 * resizeSection/hideSection/setCount 尾钩（K-73 注），无需此处重复。 */
static void xtw_pushStretchViewport(XTableWidget* self)
{
    XHeaderView* hh;
    if (!self) return;
    hh = XTableView_horizontalHeader(&self->m_base);
    if (!hh) return;
    /* 可用长 = 表格视口宽 − 行号列宽（与 Stretch 分配器同一可用口径：
     * xtw_stretchColumnsToViewport 的 avail = viewport − m_headerWidth）。
     * 引擎只在 stretchLastSection 开启时消费，未开启时推送仅存值。
     * [K-73a 2026-11-14] 走 syncViewportLength 同值闸：同长冗余推送
     * （视口事件钩与既有四处推送互叠）不再触发引擎，仅长度真变重排。 */
    XHeaderView_syncViewportLength(
        hh, XWidget_width(XAbstractScrollArea_viewport(
                (XAbstractScrollArea*)self)) -
            xtw_vHeaderW(self));
}

/* ==================== 行列增删 ==================== */

void XTableWidget_insertRow(XTableWidget* self, int row)
{
    int i;
    XTableWidgetItem** fresh;
    if (!self || row < 0 || row > self->m_rows) return;
    /* 扩容/新行分配失败直接放弃：不移动任何既有行指针，
       保持原行数（避免越界写与行块脱离登记无法回收）。 */
    if (!xtw_ensureRows(self, self->m_rows + 1)) return;
    if (!xtw_ensureCols(self, self->m_columns > 0 ? self->m_columns : 1))
        return;
    /* 行指针整体后移后，新行只落在插入位一处：
       不可先在 m_cells[m_rows] 预置新行——移位/追加两条路径都会把它覆盖，纯泄漏。 */
    fresh = xtw_newRow(self->m_base.m_colCapacity);
    if (!fresh) return;
    for (i = self->m_rows; i > row; --i) self->m_cells[i] = self->m_cells[i-1];
    self->m_cells[row] = fresh;
    self->m_rows++;
    /* [P0-32 2026-11-12] 合并区间随行插入迁移（对标 Qt setModel 里接的
     * rowsInserted → QTableViewPrivate::updateSpanInsertedRows，
     * qtableview.cpp:978-982 → QSpanCollection::updateInsertedRows）。 */
    XTableView_shiftSpansForInsert(&self->m_base, true, row, row);
    xtw_cwOnRowInserted(self, row); /* 挂载表随行号平移。 */
    /* [§B4] 选区随行插入后移（模型侧持久索引平移的重映射）；
     * Reshape 先跟快照到新布局（remap 因无选中早退时它就是唯一跟随点）。 */
    xtw_selSnapshotReshape(self, self->m_rows - 1, self->m_columns);
    xtw_selRemap(self, 0, row, 1);
    /* 对齐 Qt：行头条目随行插入（新槽为 NULL）。 */
    if (xtw_ensureVHeaders(self, self->m_rows) && self->m_vHeaders) {
        for (i = self->m_rows - 1; i > row; --i)
            self->m_vHeaders[i] = self->m_vHeaders[i - 1];
        self->m_vHeaders[row] = NULL;
    }
    xtw_syncHeaders(self);
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

void XTableWidget_insertColumn(XTableWidget* self, int column)
{
    int i;
    if (!self || column < 0 || column > self->m_columns) return;
    /* 列容量未扩成时不得推进 m_columns（否则 m_columns 越过
       m_colCapacity，后续移列/读写越界）：保持原列数。 */
    if (!xtw_ensureCols(self, self->m_columns + 1)) return;
    self->m_columns++;
    xtw_shiftColumnsRight(self, column); /* 单元格数据右移，新列置空。 */
    /* [P0-32 2026-11-12] 合并区间随列插入迁移（对标 Qt columnsInserted →
     * updateSpanInsertedColumns，qtableview.cpp:988-992）。 */
    XTableView_shiftSpansForInsert(&self->m_base, false, column, column);
    xtw_cwOnColumnInserted(self, column); /* 挂载表随列号平移。 */
    /* [§B4] 选区随列插入后移（Reshape 先跟快照到新列布局）。 */
    xtw_selSnapshotReshape(self, self->m_rows, self->m_columns - 1);
    xtw_selRemap(self, 1, column, 1);
    /* 对齐 Qt：列头条目随列插入（新槽为 NULL）。 */
    if (self->m_hHeaders) {
        for (i = self->m_columns - 1; i > column; --i)
            self->m_hHeaders[i] = self->m_hHeaders[i - 1];
        self->m_hHeaders[column] = NULL;
    }
    xtw_syncHeaders(self);
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

void XTableWidget_removeRow(XTableWidget* self, int row)
{
    int i;
    if (!self || row < 0 || row >= self->m_rows) return;
    /* 先释放被删行自身的单元格数组，再做指针前移：
       若先移位后释放 m_cells[m_rows-1]，该槽位经移位后是 m_cells[m_rows-2]
       的别名，会释放仍被引用的活行（UAF）。 */
    xtw_freeRowItems(self->m_cells[row], self->m_base.m_colCapacity);
    for (i = row; i < self->m_rows - 1; ++i)
        self->m_cells[i] = self->m_cells[i+1];
    self->m_cells[self->m_rows - 1] = NULL;
    self->m_rows--;
    /* [P0-32 2026-11-12] 合并区间随行删除迁移（对标 Qt rowsRemoved →
     * QTableViewPrivate::updateSpanRemovedRows，qtableview.cpp:998-1002 →
     * QSpanCollection::updateRemovedRows）。 */
    XTableView_shiftSpansForDelete(&self->m_base, true, row, row);
    xtw_cwOnRowRemoved(self, row); /* 挂载表随行号平移/解除。 */
    /* [§B4] 选区随行删除前移/丢弃（模型侧持久索引平移的重映射）；
     * Reshape 先跟快照到新布局。 */
    xtw_selSnapshotReshape(self, self->m_rows + 1, self->m_columns);
    xtw_selRemap(self, 0, row, -1);
    /* 对齐 Qt：行头条目随行删除并前移。 */
    if (self->m_vHeaders) {
        if (self->m_vHeaders[row]) {
            XClassDelete((XClass*)self->m_vHeaders[row]);
            self->m_vHeaders[row] = NULL;
        }
        for (i = row; i < self->m_rows; ++i)
            self->m_vHeaders[i] = self->m_vHeaders[i + 1];
        self->m_vHeaders[self->m_rows] = NULL;
    }
    /* [§B4 复核修订 2026-11-15 / 返工⑤] 当前格迁移收口到 Qt 就近迁移
     * 语义（qitemselectionmodel.cpp:654-670）：仅当前格**在被删区间**
     * 才动——迁上邻（row>0）/下邻/无效；区间外不动（QModelIndex 行号
     * 重解释，Qt 不持久平移）。旧「>=row 全平移」块与视图 m_currentRow
     * 直写已删——视图/快照由 xtw_selSyncDone 从模型派生，单一事实源
     * （探针：当前格 (4,1) → removeRow(0) 旧实现视图 (3,1)、模型 (4,1)
     * 分叉，此后点 (3,1)/(4,1) 都不发 currentCellChanged——两边各按
     * 自己的 prev 判同格）。迁移发生才收口（独立调用点无 trim 同事务，
     * 自行包一次）。 */
    if (xtw_selCurrentRangeRemoved(self, 0, row, row)) {
        ++self->m_selSyncDepth;
        xtw_selSyncDone(self, false);
        --self->m_selSyncDepth;
    }
    xtw_syncHeaders(self);
    xtw_updateContentSize(self);
    xtw_cellWidgetRelayout(self);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

void XTableWidget_removeColumn(XTableWidget* self, int column)
{
    int i;
    if (!self || column < 0 || column >= self->m_columns) return;
    self->m_columns--;
    xtw_shiftColumnsLeft(self, column); /* 释放被删列条目并左移补位。 */
    /* [P0-32 2026-11-12] 合并区间随列删除迁移（对标 Qt columnsRemoved →
     * updateSpanRemovedColumns，qtableview.cpp:1008-1012）。 */
    XTableView_shiftSpansForDelete(&self->m_base, false, column, column);
    xtw_cwOnColumnRemoved(self, column); /* 挂载表随列号平移/解除。 */
    /* [§B4] 选区随列删除前移/丢弃（Reshape 先跟快照到新列布局）。 */
    xtw_selSnapshotReshape(self, self->m_rows, self->m_columns + 1);
    xtw_selRemap(self, 1, column, -1);
    /* 对齐 Qt：列头条目随列删除并左移。 */
    if (self->m_hHeaders) {
        if (self->m_hHeaders[column]) {
            XClassDelete((XClass*)self->m_hHeaders[column]);
            self->m_hHeaders[column] = NULL;
        }
        for (i = column; i < self->m_columns; ++i)
            self->m_hHeaders[i] = self->m_hHeaders[i + 1];
        self->m_hHeaders[self->m_columns] = NULL;
    }
    /* [§B4 复核修订 / 返工⑤] 当前列就近迁移（Qt columnsAboutToBeRemoved
     * :735-751 同款；区间外不动，视图/快照由 selSyncDone 派生）。
     * 旧「>=column 全平移」与视图直写块已删（同 removeRow）。 */
    if (xtw_selCurrentRangeRemoved(self, 1, column, column)) {
        ++self->m_selSyncDepth;
        xtw_selSyncDone(self, false);
        --self->m_selSyncDepth;
    }
    xtw_syncHeaders(self);
    xtw_updateContentSize(self);
    xtw_cellWidgetRelayout(self);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

/* ==================== 单元格 ==================== */

/** @brief 单格矩形失效：几何与 paintEvent 的格子映射同式。
 *  @details 列换算（dataChanged → updateRect）走 xtw_colX——与绘制/
 *           命中同一视觉序映射入口（[K-68 2026-11-14] 拖拽换位后失效
 *           矩形跟随视觉位置），不另设口径。
 *  @note 隐藏行/列不刷新（0 尺寸；对标 Qt 隐藏条目无视觉矩形）。 */
static void xtw_updateCell(const XTableWidget* self, int row, int column)
{
    XRect cell;
    if (!self || row < 0 || column < 0 || column >= self->m_columns)
        return;
    if (xtw_colHidden(self, column) || xtw_rowHidden(self, row)) return;
    XRect_init(&cell,
               xtw_colX(self, column) - xtw_hOffset(self),
               self->m_headerHeight + xtw_rowY(self, row)
                   - xtw_vOffset(self),
               self->m_base.m_colWidths[column],
               xtw_rowHeight(self));
    XWidget_updateRect((XWidget*)self, &cell);
}

/** @brief 整行区间标脏（闭区间；行移动后 [min,max] 内每行的内容都变了）。
 *  @details [B15perf 2026-11-17] 排序插入搬移一行时，from..to 之间的行
 *           整体顺移 ⇒ 这些行的**所有列**都换了内容。旧实现只在 setItem
 *           尾部按「搬移前的 (row,column)」标一格 ⇒ 位移区不重绘、界面
 *           停着旧数据（下次别处标脏才自愈）。矩形与 xtw_updateCell 同式：
 *           x 从 0（含行号列）起、宽取控件宽，y 用同一 header+vOffset
 *           口径；越界行由调用方校验，这里再夹一次防负高。
 *  @note 关闭 AGENTS.md 重绘纪律：按**元素矩形**标脏（整行区间），不整表
 *        XWidget_update。 */
static void xtw_updateRowSpan(const XTableWidget* self, int rowA, int rowB)
{
    XRect rows;
    int top;
    int bottom;
    int t;
    if (!self) return;
    if (rowA > rowB) {
        t = rowA;
        rowA = rowB;
        rowB = t;
    }
    if (rowA < 0) rowA = 0;
    if (rowB >= self->m_rows) rowB = self->m_rows - 1;
    if (rowA > rowB) return;
    top = self->m_headerHeight + xtw_rowY(self, rowA) - xtw_vOffset(self);
    bottom = self->m_headerHeight + xtw_rowY(self, rowB)
             - xtw_vOffset(self) + xtw_rowHeight(self);
    if (bottom <= top) return;
    XRect_init(&rows, 0, top, XWidget_width((XWidget*)self), bottom - top);
    XWidget_updateRect((XWidget*)self, &rows);
}

/** @brief 整行搬移（把 from 行整体移到 to 行，其余行顺移）。
 *  @details [B15perf 2026-11-17] 条目矩阵搬移的同时，把选择模型的选区
 *           与**当前格**按同一「from→to」置换迁移（对齐 Qt 的持久索引
 *           迁移：`QTableModel::sort` 的 changePersistentIndexList
 *           （qtablewidget.cpp:533-537）、`ensureSorted` 的
 *           updateRowIndexes(oldRow,newRow)（qtablewidget.cpp:600-611）；
 *           Qt 的 setItem 排序插入走 moveRows，见 qtablewidget.cpp:194-197）。
 *           此前只搬行指针 ⇒ 选区/当前格留在行号上，高亮落到别的数据行。
 *  @note **不发射信号**：Qt 的 rowsMoved 路径只搬持久索引、不发
 *        selectionChanged / currentChanged——本机 Qt 6.8.0 offscreen 探针
 *        （sortingEnabled + setItem 触发行移动）实测 rowsMoved=1、
 *        itemSelectionChanged=0、currentCellChanged=0；静默迁移的实现见
 *        xtw_selFollowRowPermutation 的 emitSignals 形参。 */
static void xtw_moveRow(XTableWidget* self, int from, int to)
{
    XTableWidgetItem** moved;
    int* perm;
    int i;
    if (!self || from == to || from < 0 || from >= self->m_rows ||
        to < 0 || to >= self->m_rows) return;
    moved = self->m_cells[from];
    if (from < to) {
        for (i = from; i < to; ++i) self->m_cells[i] = self->m_cells[i + 1];
    } else {
        for (i = from; i > to; --i) self->m_cells[i] = self->m_cells[i - 1];
    }
    self->m_cells[to] = moved;
    /* 无选择模型 ⇒ 从未有过选区/当前格（本库两者都以选择模型为事实源，
     * 选择模型由首次查询/选区操作惰性建），无需迁移，省一次 O(R) 分配。 */
    if (!self->m_base.m_base.m_selectionModel) return;
    /* 行置换表 perm[新行] = 旧行（与 Qt updateRowIndexes 同式）：
     * from<to ⇒ (from,to] 上移一位、from 落 to；
     * from>to ⇒ [to,from) 下移一位、from 落 to。 */
    perm = (int*)XMalloc_System(sizeof(int) * (size_t)self->m_rows);
    if (!perm) return; /* 分配失败：矩阵已搬完，选区保持不变（旧行为）。 */
    for (i = 0; i < self->m_rows; ++i) perm[i] = i;
    if (from < to) {
        for (i = from; i < to; ++i) perm[i] = i + 1;
    } else {
        for (i = to + 1; i <= from; ++i) perm[i] = i - 1;
    }
    perm[to] = from;
    xtw_selFollowRowPermutation(self, perm, self->m_rows, false);
    XFree_System(perm);
}

/** @brief setItem 的排序自动插入（对齐 QTableModel::setItem 的 sorted 分支）。
 *  @details 仅当 sortingEnabled 且水平表头的排序指示段等于 column 时生效：
 *           取该列「前导非空段」（Qt columnItems 遇到 NULL 即停），把新条目
 *           按 DisplayRole 用 lower_bound 定位后整行搬移；搬移后**不**发
 *           dataChanged（对齐 Qt 走 rowsMoved 语义），选区/当前格由
 *           xtw_moveRow 的持久索引迁移跟随（静默，Qt 同）。
 *  @return 发生了搬移返回 true。 */
static bool xtw_sortedInsert(XTableWidget* self, int row, int column)
{
    XTableWidgetItem* probe;
    int sortedRow;
    int i;
    int count = 0;
    if (!self || !self->m_base.m_sortingEnabled) return false;
    if (!self->m_base.m_horizontalHeader) return false;
    if (XHeaderView_sortIndicatorSection(self->m_base.m_horizontalHeader)
        != column)
        return false;
    probe = xtw_cell(self, row, column);
    /* 统计前导非空段长度（对齐 Qt columnItems：遇 NULL 即 break）。 */
    for (i = 0; i < self->m_rows; ++i) {
        if (!xtw_cell(self, i, column)) break;
        ++count;
    }
    if (!probe) {
        sortedRow = count; /* 空值排到非空段之后。 */
    } else {
        int order =
            XHeaderView_sortIndicatorOrder(self->m_base.m_horizontalHeader);
        sortedRow = 0;
        for (i = 0; i < count; ++i) {
            const XTableWidgetItem* cur = xtw_cell(self, i, column);
            if (cur == probe) continue;
            /* lower_bound 语义：等于时插到等值段之前。 */
            if (order == 0 ? XTableWidgetItem_lessThan(cur, probe)
                           : XTableWidgetItem_lessThan(probe, cur))
                sortedRow = i + 1;
            else
                break;
        }
    }
    /* 把 row 之前的占位从计数中排除（row 自身不计入 colItems）。 */
    if (probe && sortedRow > row) sortedRow--;
    if (sortedRow < 0) sortedRow = 0;
    if (sortedRow >= self->m_rows) sortedRow = self->m_rows - 1;
    if (sortedRow == row) return false;
    xtw_moveRow(self, row, sortedRow);
    /* [B15perf] 搬移后 from..to 之间每行内容整体顺移 ⇒ 整段标脏
     * （旧实现只在 setItem 尾部按搬移前的 row 标一格，位移区留旧像素）。 */
    xtw_updateRowSpan(self, row, sortedRow);
    return true;
}

void XTableWidget_setItem(XTableWidget* self, int row, int column,
                          XTableWidgetItem* item)
{
    XTableWidgetItem** slot;
    if (!self) return;
    /* 对齐 Qt（qtablewidget.cpp:2021-2029）：
     *  - item 非空且已属于别的表格 → 忽略（Qt 打 warning 后什么都不做）；
     *  - item 非空且坐标越界 → 仍把 view 指向本表但不入表（Qt 同）；
     *  - item 为 NULL → 等价 takeItem + delete（清空该格）。 */
    if (item) {
        if (item->m_view && item->m_view != self) return;
        item->m_view = self;
    }
    slot = xtw_cellSlot(self, row, column);
    if (!slot) return;
    if (*slot) xtw_itemDelete(*slot);
    *slot = item;
    /* [§B4 返工② 2026-11-15] **镜像补刷**：选择模型里的选中态挂在索引上
     * （对空格同样成立），条目 m_selected 位是它的派生镜像——而镜像只在
     * 选区事务收尾（xtw_selSyncDone）刷新，setItem 不在其中。先选中空格
     * 再 setItem 时，新条目位是 0、模型是 1 ⇒ isSelected/绘制高亮/镜像
     * 三方失真，直到下一次选区变化才自愈。Qt 的 isSelected 直读
     * selectionModel（qtablewidget.cpp:1087-1096）无此窗口。此处入格后
     * 按模型刷位（反向失真——复用条目带残留位——一并纠正）；
     * takeItem 侧不需要：离表条目 isSelected 恒 false（view==NULL），
     * 重入表时经这里对齐。
     * **取数走快照位 O(1)**（与绘制路径同源，返工②性能口径）：快照由
     * 桥收尾与维度 Reshape 保持新布局；仅快照未建立（从无选区变化 ⇒
     * 模型选中向量必空）才回落模型查询，均为 O(1)——若直接逐格查模型
     * 线性向量，「全选后重填表」会退化成 O((R·C)²)。闸内滞后窗口由
     * 收尾镜像自愈。 */
    if (item) {
        uint32_t want;
        if (self->m_selSnapshot) {
            want = XBitArray_getBit(
                self->m_selSnapshot,
                (size_t)row * (size_t)self->m_columns + (size_t)column)
                       ? 1u : 0u;
        } else {
            XItemSelectionModel* sm = xtw_selModel(self);
            want = (sm && XItemSelectionModel_isSelected(sm, row, column))
                       ? 1u : 0u;
        }
        if (item->m_selected != want) {
            item->m_selected = want;
            xtw_updateCell(self, row, column); /* 位翻转才补标脏。 */
        }
    }
    if (item && !xtw_sortedInsert(self, row, column)) {
        /* 未发生搬移：对齐 Qt 发 dataChanged（模型桥转出 itemChanged /
         * cellChanged）。搬移路径不发（Qt 走 rowsMoved）。 */
        if (self->m_model)
            XAbstractItemModel_setData_2(
                self->m_model, row, column,
                item ? XTableWidgetItem_text_toUtf8(item) : "");
    } else if (!item && self->m_model) {
        XAbstractItemModel_setData_2(self->m_model, row, column, "");
    }
    xtw_updateCell(self, row, column);
}

XTableWidgetItem* XTableWidget_item(const XTableWidget* self, int row,
                                    int column)
{
    return xtw_cell(self, row, column);
}

const XTableWidgetItem* XTableWidget_item_const(const XTableWidget* self,
                                                int row, int column)
{
    return xtw_cell(self, row, column);
}

XTableWidgetItem* XTableWidget_takeItem(XTableWidget* self, int row, int column)
{
    XTableWidgetItem** slot = xtw_cellSlot(self, row, column);
    XTableWidgetItem* taken;
    if (!slot || !*slot) return NULL;
    taken = *slot;
    *slot = NULL;
    taken->m_view = NULL;
    xtw_updateCell(self, row, column);
    /* 对齐 Qt takeItem：发 dataChanged（经桥转出 cellChanged）；
     * itemChanged 因该格已空而不发（Qt emitItemChanged 对空格早退）。 */
    if (self->m_model)
        XAbstractItemModel_setData_2(self->m_model, row, column, "");
    xtw_emitCellSignal(self, (size_t)XTableWidget_cellChanged_signal, row,
                       column);
    return taken;
}

XTableWidgetItem* XTableWidget_itemPrototype(const XTableWidget* self)
{ return self ? self->m_itemPrototype : NULL; }

void XTableWidget_setItemPrototype(XTableWidget* self,
                                   XTableWidgetItem* prototype)
{
    if (!self) return;
    /* 对齐 Qt（qtablewidget.cpp:2525）：接管所有权，旧原型 delete。 */
    if (prototype == self->m_itemPrototype) return;
    xtw_itemDelete(self->m_itemPrototype);
    self->m_itemPrototype = prototype;
}

void XTableWidget_setRangeSelected(XTableWidget* self,
                                   const XTableWidgetSelectionRange* range,
                                   bool select)
{
    XItemSelectionModel* sm;
    int top;
    int left;
    int bottom;
    int right;
    if (!self || !range) return;
    /* 对齐 Qt（qtablewidget.cpp:2362-2373）：两角都必须合法（hasIndex
     * 双向校验），否则**整体忽略**。 */
    if (range->m_top < 0 || range->m_bottom >= self->m_rows ||
        range->m_left < 0 || range->m_right >= self->m_columns)
        return;
    /* [§B4 2026-11-15] 倒置区间**归一化后施加**：Qt 的
     * `QItemSelection(topLeft, bottomRight)` 构造经 QItemSelection::select
     * 把倒置角归一（qitemselectionmodel.cpp:387-406，本机 Qt 6.8.3 实测
     * setRangeSelected(2,2,1,1) 选中 4 格）。旧实现按「倒置即空范围忽略」
     * 处理，与 Qt 不符，此处对齐。 */
    top = range->m_top;
    left = range->m_left;
    bottom = range->m_bottom;
    right = range->m_right;
    if (top > bottom) { int t = top; top = bottom; bottom = t; }
    if (left > right) { int t = left; left = right; right = t; }
    sm = xtw_selModel(self);
    if (!sm) return;
    /* 对称闭区间 → selectSelection 的矩形参数；命令只做 Select/Deselect
     * （不会清除既有选区，对齐 Qt）。 */
    ++self->m_selSyncDepth;
    (void)XItemSelectionModel_selectSelection(
        sm, top, left, bottom, right,
        select ? XItemSelectionModelSelectionFlag_Select
               : XItemSelectionModelSelectionFlag_Deselect);
    xtw_selSyncDone(self, false);
    --self->m_selSyncDepth;
}

/* ==================== 单元格部件挂载（对齐 setCellWidget） ==================== */

/** @brief 摆放挂件到 (row,col) 单元格可视矩形（viewport 局部坐标）。 */
static void xtw_cellWidgetPlace(XTableWidget* self, int row, int column)
{
    XWidget* viewport;
    XWidget* widget;
    XRect rect;
    int vh;
    if (!self || row < 0 || row >= self->m_rows ||
        column < 0 || column >= self->m_columns) return;
    widget = (XWidget*)XTableWidget_cellWidget(self, row, column);
    if (!widget) return;
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (!viewport) return;
    vh = XWidget_height(viewport);
    rect = XTableWidget_visualItemRect(self, xtw_cell(self, row, column));
    /* 顶入表头带或底越视口：藏起（同 xtw_cellWidgetRelayout 口径）。 */
    if (rect.width <= 0 || rect.height <= 0 ||
        rect.y < self->m_headerHeight || rect.y >= vh) {
        XWidget_setVisible(widget, false);
        return;
    }
    XWidget_setGeometryRect(widget, &rect);
    XWidget_setVisible(widget, true);
}

/** @brief 重摆全部挂件（滚动/结构变化后调用；不可见格隐藏，可摆的摆位）。 */
static void xtw_cellWidgetRelayout(XTableWidget* self)
{
    XWidget* viewport;
    int vh;
    int i;
    if (!self) return;
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (!viewport) return;
    vh = XWidget_height(viewport);
    for (i = 0; i < self->m_cellWidgetCount; ++i) {
        int row = self->m_cellWidgets[i].row;
        int col = self->m_cellWidgets[i].column;
        XWidget* widget = self->m_cellWidgets[i].widget;
        XRect rect;
        if (!widget) continue;
        if (row < 0 || row >= self->m_rows ||
            col < 0 || col >= self->m_columns) {
            XWidget_setVisible(widget, false); /* 越界挂件藏起。 */
            continue;
        }
        rect = XTableWidget_visualItemRect(self, xtw_cell(self, row, col));
        /* 顶入表头带（含整行滚出上缘）或底越视口：藏起。Qt 由视口
           裁剪（视口不含表头）；本框架视口覆盖表头带，重叠即改藏，
           视觉等价且防挂件盖住表头文字。 */
        if (rect.width <= 0 || rect.height <= 0 ||
            rect.y < self->m_headerHeight || rect.y >= vh) {
            XWidget_setVisible(widget, false);
            continue;
        }
        XWidget_setGeometryRect(widget, &rect);
        XWidget_setVisible(widget, true);
    }
}

void XTableWidget_setCellWidget(XTableWidget* self, int row, int column,
                                XWidget* widget)
{
    XWidget* viewport;
    XWidget* old;
    if (!self || !xtw_cellSlot(self, row, column)) return;
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (!viewport) return;
    old = (XWidget*)XTableWidget_cellWidget(self, row, column);
    if (old == widget) return; /* 同指针幂等（Qt 同）。 */
    if (old) {
        /* 对齐 Qt：同格重挂时旧件 deleteLater（挂件生命周期归表）。 */
        xtw_cwRemoveAt(self, xtw_cwFind(self, row, column));
        XObject_deleteLater((XObject*)old);
    }
    if (!widget) return; /* NULL 等价 removeCellWidget。 */
    if (!xtw_cwReserve(self, self->m_cellWidgetCount + 1)) return;
    self->m_cellWidgets[self->m_cellWidgetCount].row = (int16_t)row;
    self->m_cellWidgets[self->m_cellWidgetCount].column = (int16_t)column;
    self->m_cellWidgets[self->m_cellWidgetCount].widget = widget;
    self->m_cellWidgetCount++;
    /* 对齐 Qt setIndexWidget：接管所有权（reparent 到视口）+摆位+显示。 */
    XWidget_setParent(widget, viewport, 0);
    xtw_cellWidgetPlace(self, row, column);
    XWidget_setVisible(widget, true);
}

XWidget* XTableWidget_cellWidget(const XTableWidget* self, int row, int column)
{
    int idx = self ? xtw_cwFind(self, row, column) : -1;
    return idx >= 0 ? self->m_cellWidgets[idx].widget : NULL;
}

void XTableWidget_removeCellWidget(XTableWidget* self, int row, int column)
{
    int idx;
    XWidget* old;
    if (!self) return;
    idx = xtw_cwFind(self, row, column);
    if (idx < 0) return;
    old = self->m_cellWidgets[idx].widget;
    xtw_cwRemoveAt(self, idx);
    if (old) XObject_deleteLater((XObject*)old); /* 所有权归表（Qt 同）。 */
}

/* ==================== 表头 ==================== */

/** @brief 内建 1 基序号文本（对齐 QTableModel::headerData 无 header item
 *         时的 DisplayRole = section + 1）。 */
static void xtw_headerIndexText(int section, char* buf, size_t size)
{
    XSnprintf(buf, size, "%d", section + 1);
}

void XTableWidget_setHorizontalHeaderLabels(XTableWidget* self,
                                            const char* const* labels,
                                            int count)
{
    int i;
    if (!self || !labels) return;
    xtw_ensureCols(self, count);
    for (i = 0; i < count && i < self->m_base.m_colCapacity; ++i) {
        if (!self->m_hHeaders[i])
            self->m_hHeaders[i] = XString_create();
        if (self->m_hHeaders[i])
            XString_assign_utf8(self->m_hHeaders[i], labels[i]);
        if (self->m_model)
            XAbstractItemModel_setHeaderData_2(self->m_model, i, 0,
                                               labels[i]);
    }
    XWidget_update((XWidget*)self);
}

void XTableWidget_setVerticalHeaderLabels(XTableWidget* self,
                                          const char* const* labels, int count)
{
    int i;
    if (!self || !labels) return;
    if (!xtw_ensureVHeaders(self, count)) return;
    for (i = 0; i < count; ++i) {
        if (!self->m_vHeaders[i])
            self->m_vHeaders[i] = XString_create();
        if (self->m_vHeaders[i])
            XString_assign_utf8(self->m_vHeaders[i], labels[i]);
    }
    XWidget_update((XWidget*)self);
}

XString* XTableWidget_horizontalHeaderItem(const XTableWidget* self,
                                           int column)
{
    XString* out;
    if (!self || column < 0 || column >= self->m_columns)
        return XString_create();
    if (self->m_hHeaders && column < self->m_base.m_colCapacity &&
        self->m_hHeaders[column])
        return XString_create_copy(self->m_hHeaders[column]);
    /* 对齐 Qt：无 header item 时 DisplayRole 返回 section + 1。 */
    out = XString_create();
    if (out) {
        char buf[16];
        xtw_headerIndexText(column, buf, sizeof(buf));
        XString_assign_utf8(out, buf);
    }
    return out;
}

XString* XTableWidget_verticalHeaderItem(const XTableWidget* self, int row)
{
    XString* out;
    if (!self || row < 0 || row >= self->m_rows) return XString_create();
    if (self->m_vHeaders && row < self->m_vHeaderCapacity &&
        self->m_vHeaders[row])
        return XString_create_copy(self->m_vHeaders[row]);
    out = XString_create();
    if (out) {
        char buf[16];
        xtw_headerIndexText(row, buf, sizeof(buf));
        XString_assign_utf8(out, buf);
    }
    return out;
}

void XTableWidget_setHorizontalHeaderItem(XTableWidget* self, int column,
                                          const XString* text)
{
    const char* utf8;
    /* 对齐 Qt（qtablewidget.cpp:2096-2105）：越界 section 直接忽略。 */
    if (!self || column < 0 || column >= self->m_columns) return;
    if (!self->m_hHeaders || column >= self->m_base.m_colCapacity) return;
    if (!self->m_hHeaders[column])
        self->m_hHeaders[column] = XString_create();
    if (!self->m_hHeaders[column]) return;
    utf8 = text ? XString_toUtf8(text) : NULL; /* NULL 等价清空该列。 */
    XString_assign_utf8(self->m_hHeaders[column], utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setHeaderData_2(self->m_model, column, 0,
                                           utf8 ? utf8 : "");
}

void XTableWidget_setVerticalHeaderItem(XTableWidget* self, int row,
                                        const XString* text)
{
    const char* utf8;
    if (!self || row < 0 || row >= self->m_rows) return;
    if (!xtw_ensureVHeaders(self, row + 1)) return;
    if (!self->m_vHeaders[row])
        self->m_vHeaders[row] = XString_create();
    if (!self->m_vHeaders[row]) return;
    utf8 = text ? XString_toUtf8(text) : NULL; /* NULL 等价清空该行。 */
    XString_assign_utf8(self->m_vHeaders[row], utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
}

XString* XTableWidget_takeHorizontalHeaderItem(XTableWidget* self, int column)
{
    XString* taken;
    if (!self || column < 0 || !self->m_hHeaders ||
        column >= self->m_base.m_colCapacity || !self->m_hHeaders[column])
        return NULL;
    taken = self->m_hHeaders[column];
    self->m_hHeaders[column] = NULL; /* 所有权移交调用方（XClassDelete 释放）。 */
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setHeaderData_2(self->m_model, column, 0, "");
    return taken;
}

XString* XTableWidget_takeVerticalHeaderItem(XTableWidget* self, int row)
{
    XString* taken;
    if (!self || row < 0 || !self->m_vHeaders ||
        row >= self->m_vHeaderCapacity || !self->m_vHeaders[row])
        return NULL;
    taken = self->m_vHeaders[row];
    self->m_vHeaders[row] = NULL; /* 所有权移交调用方（XClassDelete 释放）。 */
    XWidget_update((XWidget*)self);
    return taken;
}

/* ==================== 行为 ==================== */

/** @brief 表头单击 ⇒ 按该列排序（Qt 口径：setSortingEnabled 后
 *         sectionClicked → sortByColumn）。 */
static void xtwHeaderClicked(XObject* receiver, XVarList* args)
{
    XTableWidget* self = (XTableWidget*)receiver;
    XHeaderView* hh;
    int col = -1;
    if (!self) return;
    if (args) {
        XVarList_start(args);
        col = XVarList_arg(args, int);
    }
    hh = XTableView_horizontalHeader(&self->m_base);
    if (!hh || col < 0) return;
    XTableWidget_sortItems(self, col, XHeaderView_sortIndicatorOrder(hh));
}

/** @brief 表头几何变化（段移动/调宽/段数）⇒ 挂件重摆。
 *  [K-68 残留 b 2026-11-14] 对标 Qt horizontalHeader::geometriesChanged →
 *  QTableView::updateGeometries（qtableview.cpp:1394-1395；updateGeometries
 *  尾段的 QAbstractItemView::updateGeometries 承载 editor/index-widget
 *  几何）：拖拽换位后挂件跟随**视觉**位置——visualItemRect 已走本类
 *  visualRect 虚槽的视觉序几何，本槽补上「谁来重摆」的触发链。无挂件
 *  时空跑近零开销；调宽引发的批量重排每列各发一次，重摆是幂等摆位。 */
static void xtwHeaderGeometriesChanged(XObject* receiver, XVarList* args)
{
    XTableWidget* self = (XTableWidget*)receiver;
    (void)args;
    if (!self) return;
    xtw_cellWidgetRelayout(self);
    XWidget_update((XWidget*)self);
}

void XTableWidget_setSortingEnabled(XTableWidget* self, bool enable)
{
    XHeaderView* hh;
    if (!self) return;
    /* [P0-76 2026-11-12] ① 状态同步到基类字段：查询走继承的
     * XTableView_isSortingEnabled（读 m_base.m_sortingEnabled），此前只写
     * m_headerSortEnabled ⇒ 开启后查询恒 false；同一字段还是
     * xtw_sortedInsert 的门闸（:2377）⇒ setItem 的自动排序插入永不生效。
     * 一处同步修好两个症状。 */
    self->m_headerSortEnabled = enable;
    self->m_base.m_sortingEnabled = enable;
    hh = XTableView_horizontalHeader(&self->m_base);
    if (!hh) return;
    /* [P0-76 2026-11-12] ② 连接生命周期：XObject_connect_1 不去重，此前
     * 每次 enable 都新接一条 ⇒ 重复开关叠加槽。先断后接，窄化为单条。 */
    XObject_disconnect((XObject*)hh,
                         XSignal(XHeaderView_sectionClicked_signal),
                         (XObject*)self, xtwHeaderClicked);
    /* Qt：开了排序才让表头可点（点击排序）；否则表头只用于拖分隔线。 */
    XHeaderView_setSectionsClickable(hh, enable);
    if (enable) {
        XObject_connect((XObject*)hh,
                          XSignal(XHeaderView_sectionClicked_signal),
                          (XObject*)self, xtwHeaderClicked,
                          XConnectionType_Direct);
        /* Qt setSortingEnabled(true) 立即按当前指示段排一次
         * （qtableview.cpp:2744-2746 注释与实现），本类排序走条目矩阵
         * （XTableWidget_sortItems），故直接调它。指示段未设置（-1）时
         * 由 sortItems 的列号校验早退。 */
        if (XHeaderView_sortIndicatorSection(hh) >= 0)
            XTableWidget_sortItems(
                self, XHeaderView_sortIndicatorSection(hh),
                XHeaderView_sortIndicatorOrder(hh));
    }
}

/** @brief 排序键（稳定排序的元素：旧行号 + DisplayRole 比较键）。
 *  @details Qt 的 `QTableModel::sort` 把「条目 + 行号」装进
 *           `QList<std::pair<QTableWidgetItem*,int>>` 后 `std::stable_sort`
 *           （qtablewidget.cpp:503-514），空条目行走 unsortable 列表**按
 *           原序追加在末尾**（qtablewidget.cpp:506-511、526-529）。
 *           本库逐元素携带旧行号：排完即得「新行 → 旧行」置换，直接支撑
 *           选区/当前格的持久索引迁移（Qt changePersistentIndexList，
 *           qtablewidget.cpp:533-537）。desc 随元素携带，使比较器在
 *           XSORT_ASC 口径下也能烘焙降序（空值恒沉底，不随方向翻转）。 */
typedef struct XtwSortKey
{
    int row;                     /**< 旧行号（排序后即 perm[新行]）。 */
    int desc;                    /**< 0=升序、1=降序（烘焙进比较器）。 */
    const XTableWidgetItem* key; /**< 该列条目；NULL=不可排序，恒沉底。 */
} XtwSortKey;

/** @brief XMergeSort 比较器：返回「最终顺序」的 Less/Equality/Greater。
 *  @details 空键恒排在非空键之后（与排序方向无关，对齐 Qt 把 unsortable
 *           行追加到末尾的口径）；等键返回 Equality ⇒ 归并稳定
 *           （XMergeSort 遇 Equality 先取左段，Src/XCore/XCode/XAlgorithm/
 *           XSort/XMergeSort.c:47-49，与 Qt std::stable_sort 同语义）。
 *           比较器只依赖元素自身（desc 在元素里），无隐式全局状态。 */
static int32_t xtw_sortKeyCompare(const void* lhs, const void* rhs)
{
    const XtwSortKey* a = (const XtwSortKey*)lhs;
    const XtwSortKey* b = (const XtwSortKey*)rhs;
    bool ab;
    bool ba;
    if (!a->key || !b->key) {
        if (!a->key && !b->key) return XCompare_Equality;
        return a->key ? XCompare_Less : XCompare_Greater;
    }
    ab = XTableWidgetItem_lessThan(a->key, b->key);
    ba = XTableWidgetItem_lessThan(b->key, a->key);
    if (ab) return a->desc ? XCompare_Greater : XCompare_Less;
    if (ba) return a->desc ? XCompare_Less : XCompare_Greater;
    return XCompare_Equality;
}

void XTableWidget_sortItems(XTableWidget* self, int column, int order)
{
    XtwSortKey* keys = NULL;
    XTableWidgetItem*** rowsTmp = NULL;
    int* perm = NULL;
    int i;
    bool moved = false; /* false ⇒ 恒等置换（选区/当前格不跟随、不发射）。 */
    if (!self || column < 0 || column >= self->m_columns) return;
    /* [§B4 复核修订 2026-11-15] 排序必须**把选区与当前格随条目一起搬移**：
     * 选中态此前存在条目 m_selected 位上（随行指针搬移自然跟随）；改为
     * selectionModel 的 (row<<16)|col 索引后，若只交换 m_cells 行指针而
     * 不动选区索引，高亮会留在行号上 ⇒ 排到别的数据行（现场：
     * HsfTableSort_apply（HighSpeedFilter/client/ui/HsfTableSort.c:47-52）
     * 每次排序都被 HsfDataHistory/HsfDataLog 的刷新调用）。Qt 的做法是
     * changePersistentIndexList 让选区跟条目走（qtablewidget.cpp:533-537）。
     * [B15perf 2026-11-17] 排序本体换**稳定归并**（框架 XMergeSort，
     * O(R log R)）：旧实现是 O(R²) 邻位交换（500 行×100 次实测 27.9ms/次，
     * 见 §B4 台账）。Qt 用 std::stable_sort（qtablewidget.cpp:512）⇒
     * 等键保持相对顺序；归并遇 Equality 先取左段 ⇒ 同稳定语义。
     * 该列为空的行走「不可排序」组，保持相对顺序排在最后。
     * 对齐 QTableModel::sort：只重排**条目矩阵**——Qt 的 sort 恰恰**不动**
     * verticalHeaderItems（行头停在原行号位，与行头显示行号的语义一致）。 */
    if (self->m_rows > 1 && self->m_cells) {
        keys = (XtwSortKey*)XMalloc_System(sizeof(XtwSortKey) *
                                           (size_t)self->m_rows);
        rowsTmp = (XTableWidgetItem***)XMalloc_System(
            sizeof(XTableWidgetItem**) * (size_t)self->m_rows);
        /* 分配失败：fail-closed，矩阵与选区一概不动（不半途改数据）。
         * XMergeSort 自身也要等大临时缓冲，同类失败在其内部同样早退。 */
        if (!keys || !rowsTmp) {
            if (keys) XFree_System(keys);
            if (rowsTmp) XFree_System(rowsTmp);
            return;
        }
        for (i = 0; i < self->m_rows; ++i) {
            keys[i].row = i;
            keys[i].desc = order ? 1 : 0;
            keys[i].key = xtw_cell(self, i, column);
        }
        XMergeSort(keys, (size_t)self->m_rows, sizeof(XtwSortKey),
                   xtw_sortKeyCompare, XSORT_ASC);
        for (i = 0; i < self->m_rows; ++i) {
            rowsTmp[i] = self->m_cells[keys[i].row];
            if (keys[i].row != i) moved = true;
        }
        memcpy(self->m_cells, rowsTmp,
               sizeof(XTableWidgetItem**) * (size_t)self->m_rows);
        XFree_System(rowsTmp);
        rowsTmp = NULL;
        perm = (int*)XMalloc_System(sizeof(int) * (size_t)self->m_rows);
        if (perm) {
            for (i = 0; i < self->m_rows; ++i) perm[i] = keys[i].row;
        }
        XFree_System(keys);
        keys = NULL;
    }
    /* 选区/当前格随条目搬移（per-row 置换）。恒等置换（已序表重复排序）
     * 早退：选区/当前格的位集一字未变，跟随与强制发射都会凭空发一次
     * itemSelectionChanged（返工①回归锁 sortFollowsMultiCellSelection
     * 的幂等断言）。
     * emitSignals=true：**既有契约**（§B4 回归锁锁死排序后一次
     * itemSelectionChanged）。本机 Qt 6.8.0 探针显示 QTableWidget::sortItems
     * 实测三族信号 0（选区仍跟条目）⇒ 本库此处比 Qt 多一次选区通知，
     * 属 §B4 既定语义词的保留项（未在 B15perf 改动，见 openNotes）；
     * xtw_moveRow 的排序插入路径则按 Qt 静默（emitSignals=false）。 */
    if (perm) {
        if (moved) xtw_selFollowRowPermutation(self, perm, self->m_rows, true);
        XFree_System(perm);
    }
    self->m_base.m_sortColumn = (int16_t)column;
    self->m_base.m_sortOrder = (int16_t)(order ? 1 : 0);
    if (self->m_base.m_horizontalHeader)
        XHeaderView_setSortIndicator(self->m_base.m_horizontalHeader, column,
                                     order ? 1 : 0);
    XWidget_update((XWidget*)self);
}

/** @brief 排序本体虚槽覆写（P0-33）：排条目矩阵而非模型。
 *  @details 对标 Qt `QTableView::sortByColumn` 尾段的 `model->sort(column,
 *           order)`——Qt 的 QTableModel 即唯一存储，故排模型等于排条目；
 *           本框架「条目矩阵 + 内建模型」双存储，只排模型会让 m_cells
 *           与模型错位（绘制/命中读 m_cells），因此覆写为
 *           XTableWidget_sortItems。column < 0（恢复自然序）由
 *           sortItems 的列号校验早退（本库无「自然序快照」承载，
 *           与 XTableWidget_sortItems 现有口径一致）。 */
static void VXTableWidget_sortByColumn(XTableView* self, int column,
                                       int order)
{
    if (!self) return;
    XTableWidget_sortItems((XTableWidget*)self, column, order);
}

void XTableWidget_clear(XTableWidget* self)
{
    int i;
    XItemSelectionModel* sm;
    if (!self) return;
    /* 对齐 Qt（qtablewidget.cpp:2578）：先 selectionModel()->clear()
     * ——clear = clearSelection() + clearCurrentIndex()（Qt 实测 clear()
     * 后 currentRow/currentColumn 均为 -1、选区为空）。
     * [§B4 2026-11-15] 走选择模型而非仅 clearSelection：旧实现的
     * XAbstractItemView_clearSelection 只清选区、当前索引仍留在选择模型里，
     * 会在随后的桥槽里把旧当前格写回。这里按 Qt 用 clear() 一并复位。 */
    sm = xtw_selModel(self);
    if (sm) {
        /* [§B4 复核修订 2026-11-15] **不得**把 clear 包在事务闸里：那会让
         * 桥槽直接 return，itemSelectionChanged / currentItemChanged /
         * currentCellChanged 一个都不发（探针：clearContents 后 0/0/0）。
         * Qt 的 clear() = clearSelection()（发 selectionChanged）+
         * clearCurrentIndex()（发 currentChanged），qitemselectionmodel.cpp
         * :1335-1354；QTableWidget::clear/clearContents 正是先调它
         * （qtablewidget.cpp:2578/2592）。故此处闸外调用，由桥槽逐信号收口。 */
        XItemSelectionModel_clear(sm);
    }
    /* [§B4 返工⑥ 2026-11-15] 旧「全表逐格清 m_selected 位」循环已删：
     * clear 的 XItemSelectionModel_clear 在闸外发 selectionChanged ⇒ 桥槽
     * selSyncDone 已把全表镜像刷成模型值（全 0）⇒ 该循环冗余且 O(R·C)；
     * 条目随后整体释放（freeRowItems），位残留无观察点。模型缺失的分配
     * 失败兜底路径条目同样即将释放。 */
    if (self->m_cells) {
        for (i = 0; i < self->m_rows; ++i) {
            xtw_freeRowItems(self->m_cells[i], self->m_base.m_colCapacity);
            self->m_cells[i] = NULL;
        }
        XFree_System(self->m_cells);
        self->m_cells = NULL;
        self->m_rowCapacity = 0;
    }
    /* 表头文本一并清空（对齐 Qt clear()：同时移除表头）；容量保留。 */
    if (self->m_hHeaders) {
        for (i = 0; i < self->m_base.m_colCapacity; ++i) {
            if (self->m_hHeaders[i]) {
                XClassDelete((XClass*)self->m_hHeaders[i]);
                self->m_hHeaders[i] = NULL;
            }
        }
    }
    if (self->m_vHeaders) {
        for (i = 0; i < self->m_vHeaderCapacity; ++i) {
            if (self->m_vHeaders[i]) {
                XClassDelete((XClass*)self->m_vHeaders[i]);
                self->m_vHeaders[i] = NULL;
            }
        }
    }
    self->m_rows = 0;
    self->m_columns = 0;
    /* [§B4 返工⑤/⑥] 幂等兜底（非第二事实源）：XItemSelectionModel_clear
     * 闸外发 currentChanged ⇒ 桥槽 selSyncDone 已按模型把视图字段与
     * m_selectionRow/Column 快照写成 -1；此处再写一遍只兜「模型缺失的
     * 分配失败」路径，正常路径值恒等、无分叉。clearContents 尾部同。 */
    self->m_base.m_base.m_currentRow = -1;
    self->m_base.m_base.m_currentColumn = -1;
    self->m_selectionRow = -1;
    self->m_selectionColumn = -1;
    xtw_cwReleaseAll(self);         /* 挂件所有权归表：全部解除并释放。 */
    /* 对齐 Qt：clear() **不**清除 spans（只有 clearSpans 清除）。 */
    xtw_syncHeaders(self);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

void XTableWidget_clearContents(XTableWidget* self)
{
    int i;
    XItemSelectionModel* sm;
    if (!self) return;
    /* 对齐 Qt（qtablewidget.cpp:2592）：先 selectionModel()->clear()
     * （clear = clearSelection + clearCurrentIndex）。
     * [§B4 复核修订 2026-11-15] **闸外**调用：包在事务闸里会让桥槽直接
     * return，三信号全静默（探针 0/0/0，Qt 为 1/1/1）。理由同
     * XTableWidget_clear。 */
    sm = xtw_selModel(self);
    if (sm) XItemSelectionModel_clear(sm);
    /* 挂件所有权归表：clearContents 触发模型 reset，对齐 Qt reset()
       逐编辑器释放。 */
    xtw_cwReleaseAll(self);
    for (i = 0; i < self->m_rows; ++i) {
        int k;
        if (!self->m_cells[i]) continue;
        for (k = 0; k < self->m_base.m_colCapacity; ++k) {
            if (self->m_cells[i][k]) {
                xtw_itemDelete(self->m_cells[i][k]);
                self->m_cells[i][k] = NULL;
            }
        }
    }
    XWidget_update((XWidget*)self);
    if (self->m_model) {
        /* 同步内建模型：逐格写 NULL 清空文本。维度与表头保持不变
           （对齐 Qt clearContents 不改行列数与表头）。 */
        int r;
        int c;
        for (r = 0; r < self->m_rows; ++r)
            for (c = 0; c < self->m_columns; ++c)
                XAbstractItemModel_setData(self->m_model, r, c, NULL);
    }
    self->m_base.m_base.m_currentRow = -1;
    self->m_base.m_base.m_currentColumn = -1;
    self->m_selectionRow = -1;
    self->m_selectionColumn = -1;
}

void XTableWidget_scrollToItem(XTableWidget* self, const XTableWidgetItem* item,
                               int hint)
{
    int row;
    int col;
    if (!self || !item) return;
    row = XTableWidget_indexFromItem(self, item, &col);
    if (row < 0 || col < 0) return;
    XAbstractItemView_scrollToHint(&self->m_base.m_base, row, col, hint);
}

/* ==================== [§B4 2026-11-15] selectionModel 双向桥 ==================== */

/** @brief 取选择模型（惰性创建；视图基类在 init 已建，缺失时补建）。
 *  @details 对标 Qt：`QTableWidgetItem::isSelected/setSelected` 与
 *           `QTableWidget::selectedRanges/selectedItems/setRangeSelected`
 *           全部经 `view->selectionModel()` 一途（qtablewidget.cpp:1087-1112、
 *           2381-2392、2408-2422、2362-2373）。本库选择模型由
 *           XAbstractItemView 持有（XAbstractItemView.c:375 懒创建）。
 *  @param self 目标表格；可为 NULL。
 *  @return 选择模型借用指针；NULL 表示不可用（调用方须跳过）。 */
static XItemSelectionModel* xtw_selModel(const XTableWidget* self)
{
    XAbstractItemView* view;
    XItemSelectionModel* sm;
    if (!self) return NULL;
    view = (XAbstractItemView*)&self->m_base.m_base;
    if (!view->m_selectionModel) {
        view->m_selectionModel = XItemSelectionModel_create();
        if (!view->m_selectionModel) return NULL;
        /* 新模型必须绑到本表内建模型：XItemSelectionModel_select 对未绑定
         * 模型是空操作（对齐 Qt qitemselectionmodel.cpp:1284-1290）。 */
        XItemSelectionModel_setModel(view->m_selectionModel, view->m_model);
    }
    sm = view->m_selectionModel;
    /* [§B4 复核修订 2026-11-15] 换模型自愈。`XAbstractItemView_setSelectionModel`
     * 会 XClassDelete 旧模型再装新模型（XAbstractItemView.c:2103-2105），
     * 旧模型的桥连接随其信号槽表一并销毁；该入口不在本类内且不是虚槽，
     * 无法在彼处挂钩（Qt 在 setSelectionModel 里直接重连，
     * qabstractitemview.cpp:790-860 ⇒ 已提 sharedEdit 请求该钩子）。
     * **判据不能只比指针**：实测 `setSelectionModel(NULL)` 里
     * `XClassDelete(旧)` 与随后的 `XItemSelectionModel_create()` 会复用同一
     * 地址（探针：旧/新均 0x…e690），指针相等但连接已随旧槽表消失。
     * 故判据改为「本表在 sm 上的桥连接是否仍登记」，用 Unique 语义探测：
     * 已登记则 connect 返回 NULL（不新增），未登记则新建。 */
    {
        XTableWidget* selfMut = (XTableWidget*)self;
        XConnection* probe;
        probe = XObject_connect(
            (XObject*)sm,
            XSignal(XItemSelectionModel_selectionChanged_signal),
            (XObject*)selfMut, xtw_selChangedSlot,
            (XConnectionType)(XConnectionType_Direct |
                              XConnectionType_Unique));
        if (probe) {
            /* 未登记 ⇒ 新建成功：这说明刚才没有桥（换模型/首次），补齐
             * currentChanged 侧并刷新簿记。 */
            selfMut->m_smSelConn = probe;
            selfMut->m_smCurConn = XObject_connect(
                (XObject*)sm,
                XSignal(XItemSelectionModel_currentChanged_signal),
                (XObject*)selfMut, xtw_selCurrentChangedSlot,
                (XConnectionType)(XConnectionType_Direct |
                                  XConnectionType_Unique));
            selfMut->m_smArmed = sm;
        }
    }
    return sm;
}

/** @brief 判定选中集合自上次收尾以来是否真变化（含只有空格的选区）。
 *  @details 意见修订（2026-11-15）：原实现拿 `xtw_selMirror` 的返回值当
 *           「选区变了」——镜像只翻**有条目**的格位，故「选中一个空格」或
 *           「两个纯空格选区之间切换」全程静默。Qt 的 selectionChanged 由
 *           范围变化驱动、与格上有没有 item 无关（qtablewidget.cpp:1636-1637
 *           直连）。这里改为比对 R×C 选中位图快照（`m_selSnapshot`）。
 *  @param self 目标表格；可为 NULL（返回 false 且不写快照）。
 *  @param outDirty 旧/新选区差异格的包围盒（视口坐标；无差异置空矩形）。
 *  @return 选中集合与上次已发射的快照不同返回 true。
 */
static bool xtw_selDiffSnapshot(XTableWidget* self, XRect* outDirty)
{
    XItemSelectionModel* sm;
    XBitArray* now;
    int* rows = NULL;
    int* cols = NULL;
    int n;
    int i;
    int row;
    int col;
    bool changed = false;
    int minRow = -1;
    int minCol = -1;
    int maxRow = -1;
    int maxCol = -1;
    size_t total;
    if (!self) return false;
    if (outDirty) XRect_init(outDirty, 0, 0, 0, 0);
    sm = xtw_selModel(self);
    if (!sm) return false;
    total = (size_t)(self->m_rows > 0 ? self->m_rows : 1) *
            (size_t)(self->m_columns > 0 ? self->m_columns : 1);
    now = XBitArray_create(total);
    if (!now) return false;
    n = XItemSelectionModel_selectedCount(sm);
    if (n > 0) {
        rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        if (!rows || !cols) {
            if (rows) XFree_System(rows);
            if (cols) XFree_System(cols);
            XClassDelete((XClass*)now);
            return false;
        }
        n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
        for (i = 0; i < n; ++i) {
            if (rows[i] < 0 || rows[i] >= self->m_rows ||
                cols[i] < 0 || cols[i] >= self->m_columns) continue;
            (void)XBitArray_setBit(
                now, (size_t)rows[i] * (size_t)self->m_columns +
                         (size_t)cols[i], true);
        }
        XFree_System(rows);
        XFree_System(cols);
    }
    /* 逐格比对旧快照（容量不足视为「全未选中」，与重建语义一致）。 */
    for (row = 0; row < self->m_rows; ++row) {
        for (col = 0; col < self->m_columns; ++col) {
            size_t idx = (size_t)row * (size_t)self->m_columns + (size_t)col;
            bool a = XBitArray_getBit(now, idx);
            bool b = self->m_selSnapshot ?
                         XBitArray_getBit(self->m_selSnapshot, idx) : false;
            if (a == b) continue;
            changed = true;
            if (minRow < 0 || row < minRow) minRow = row;
            if (row > maxRow) maxRow = row;
            if (minCol < 0 || col < minCol) minCol = col;
            if (col > maxCol) maxCol = col;
        }
    }
    /* 快照换新（空选区直接释放，省一块内存）。 */
    if (self->m_selSnapshot) XClassDelete((XClass*)self->m_selSnapshot);
    self->m_selSnapshot = now;
    if (changed && outDirty) {
        XRect r0 = XTableWidget_visualItemRect(
            self, xtw_cell(self, minRow, minCol));
        XRect r1 = XTableWidget_visualItemRect(
            self, xtw_cell(self, maxRow, maxCol));
        if (!XRect_isEmpty(&r0) && !XRect_isEmpty(&r1)) {
            int x1 = r0.x < r1.x ? r0.x : r1.x;
            int y1 = r0.y < r1.y ? r0.y : r1.y;
            int x2 = (r0.x + r0.width > r1.x + r1.width)
                         ? r0.x + r0.width : r1.x + r1.width;
            int y2 = (r0.y + r0.height > r1.y + r1.height)
                         ? r0.y + r0.height : r1.y + r1.height;
            XRect_init(outDirty, x1, y1, x2 - x1, y2 - y1);
        }
    }
    return changed;
}

/** @brief 把选择模型的选中集合镜像进条目 m_selected 位（单向、带同值闸）。
 *  @details 这是**模型 → 条目**方向唯一写入点。逐格比较后才写：
 *           ① 同值不写（参考 XTreeWidget 列 0 桥的同值闸教训——模型层无
 *              同值早退，重复写会产生多余的一拍 dataChanged/重绘）；
 *           ② 条目选中的真值以模型为准，条目位只服务绘制/旧 API 读回。
 *           空格（无条目）无位可承载 ⇒ 跳过（Qt 的 QTableModel 里空格
 *           仍是可选中索引，但 isSelected 经条目返回必然为 false）。
 *           复杂度：一次取选中集 O(N) + 全表位比较 O(R·C)，见下方注释。
 *  @param self     目标表格；可为 NULL。
 *  @param outDirty 变化格包围盒（视口坐标；无变化置空矩形）；可 NULL。
 *  @return 有条目位实际变化返回 true。
 */
static bool xtw_selMirror(XTableWidget* self, XRect* outDirty)
{
    XItemSelectionModel* sm;
    XBitArray* selBits;
    int* rows = NULL;
    int* cols = NULL;
    int n;
    int i;
    int row;
    int col;
    bool changed = false;
    int minRow = -1;
    int minCol = -1;
    int maxRow = -1;
    int maxCol = -1;
    if (!self) return false;
    sm = xtw_selModel(self);
    if (!sm) return false;
    n = XItemSelectionModel_selectedCount(sm);
    /* 意见修订（2026-11-15）：原实现「全表 R×C 逐格调 isSelected」是
     * O(R·C·N)（isSelected 内部线性扫选中向量），绘制/镜像都会爆。现改为
     * 「一次取选中集（O(N)）→ 填位图 → 全表按位比较（O(R·C)）」，总体
     * O(N + R·C)。位图用框架 XBitArray（禁止手搓缓冲）。 */
    if (n > 0) {
        rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        selBits = XBitArray_create((size_t)(self->m_rows > 0 ?
                                            self->m_rows : 1) *
                                   (size_t)(self->m_columns > 0 ?
                                            self->m_columns : 1));
        if (!rows || !cols || !selBits) {
            if (rows) XFree_System(rows);
            if (cols) XFree_System(cols);
            if (selBits) XClassDelete((XClass*)selBits);
            return false; /* 分配失败：本次不镜像，下一次变化会重试。 */
        }
        n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
        for (i = 0; i < n; ++i) {
            if (rows[i] < 0 || rows[i] >= self->m_rows ||
                cols[i] < 0 || cols[i] >= self->m_columns) continue;
            (void)XBitArray_setBit(
                selBits, (size_t)rows[i] * (size_t)self->m_columns +
                             (size_t)cols[i], true);
        }
    } else {
        selBits = NULL;
    }
    for (row = 0; row < self->m_rows; ++row) {
        if (!self->m_cells || !self->m_cells[row]) continue;
        for (col = 0; col < self->m_columns; ++col) {
            XTableWidgetItem* cell = self->m_cells[row][col];
            uint32_t want;
            if (!cell) continue;
            want = (selBits &&
                    XBitArray_getBit(selBits,
                                     (size_t)row * (size_t)self->m_columns +
                                         (size_t)col)) ? 1u : 0u;
            if (cell->m_selected == want) continue; /* 同值闸。 */
            cell->m_selected = want;
            changed = true;
            if (minRow < 0 || row < minRow) minRow = row;
            if (row > maxRow) maxRow = row;
            if (minCol < 0 || col < minCol) minCol = col;
            if (col > maxCol) maxCol = col;
        }
    }
    if (rows) XFree_System(rows);
    if (cols) XFree_System(cols);
    if (selBits) XClassDelete((XClass*)selBits);
    /* 脏区由变化格的包围盒决定（AGENTS.md 重绘纪律：按元素矩形标脏，
     * 不要整表 XWidget_update）。 */
    if (outDirty) {
        if (changed) {
            XRect r0 = XTableWidget_visualItemRect(
                self, xtw_cell(self, minRow, minCol));
            XRect r1 = XTableWidget_visualItemRect(
                self, xtw_cell(self, maxRow, maxCol));
            if (XRect_isEmpty(&r0) || XRect_isEmpty(&r1)) {
                XRect_init(outDirty, 0, 0, 0, 0);
            } else {
                outDirty->x = r0.x < r1.x ? r0.x : r1.x;
                outDirty->y = r0.y < r1.y ? r0.y : r1.y;
                outDirty->width = (r0.x + r0.width > r1.x + r1.width
                                       ? r0.x + r0.width
                                       : r1.x + r1.width) - outDirty->x;
                outDirty->height = (r0.y + r0.height > r1.y + r1.height
                                        ? r0.y + r0.height
                                        : r1.y + r1.height) - outDirty->y;
            }
        } else {
            XRect_init(outDirty, 0, 0, 0, 0);
        }
    }
    return changed;
}

/** @brief 选区同步后统一收尾：镜像 + 三个信号按 Qt 次序发射 + 脏区。
 *  @details 本机 Qt 6.8.3 实测一次 `setCurrentCell(r,c)` 的信号次序为
 *           **itemSelectionChanged → currentItemChanged → currentCellChanged**
 *           （各一次）。本库选择模型在 select 内部依次发 selectionChanged
 *           与 currentChanged，故本函数在事务尾部按 Qt 次序统一收口三信号：
 *           ① 镜像条目位（同值闸，返回是否有变化）；
 *           ② 有变化 ⇒ itemSelectionChanged 一次；
 *           ③ 当前格与旧值不同 ⇒ currentItemChanged（新旧至少一项非空才发）
 *              + currentCellChanged（恒发）。
 *           `m_selectionRow/Column` 在此作为「上一次已发射的当前格」快照。
 *  @param self 目标表格；可为 NULL（无操作）。
 *  @param forceSel 返工③④：**批量重建点的强制发射**。remap/trim 走
 *           rebuildSelection（保序批量装载）时「选中集合必已变化」（轴
 *           变更处非空 ⇒ 平移/裁剪必然改位集），但 diffSnapshot 的快照
 *           比对按 R×C 位布局——维度变更后旧快照的位下标与新维度错位，
 *           会把真实差异算成「无变化」而漏发（实测：3×3 选整行 →
 *           setRowCount(1) → setColumnCount(1)，快照 bit1/bit2 落在新
 *           维度外 ⇒ changed=false ⇒ 0 发射）。forceSel=true 时仍换新
 *           快照（对齐新维度布局）但发射判据恒真。perm 路径不适用
 *           （恒等置换已在 sortItems 入口早退，见该处 swap 计数）。
 */
static void xtw_selSyncDone(XTableWidget* self, bool forceSel)
{
    XItemSelectionModel* sm;
    XRect dirty;
    bool changed;
    int curRow = -1;
    int curCol = -1;
    int prevRow;
    int prevCol;
    if (!self) return;
    sm = xtw_selModel(self);
    prevRow = self->m_selectionRow;
    prevCol = self->m_selectionColumn;
    /* 选区「真变化」判定走选中集合位图快照（含只有空格的选区；
     * 镜像只翻有条目的格位，不能用作判定依据）。两边都算，脏区取并集。 */
    changed = xtw_selDiffSnapshot(self, &dirty);
    if (forceSel) changed = true;
    (void)xtw_selMirror(self, NULL);
    /* 当前索引以选择模型为事实源（Qt 同）；缺失时回落视图当前格。 */
    if (sm && !XItemSelectionModel_currentIndex(sm, &curRow, &curCol)) {
        curRow = -1;
        curCol = -1;
    }
    if (curRow < 0 || curRow >= self->m_rows ||
        curCol < 0 || curCol >= self->m_columns) {
        curRow = -1;
        curCol = -1;
    }
    if (changed)
        xtw_emit0(self, (size_t)XTableWidget_itemSelectionChanged_signal);
    /* 当前格变化族：只在快照与实际不同时发（同格重设不发，Qt 同）。 */
    if (curRow != prevRow || curCol != prevCol) {
        XTableWidgetItem* curItem = xtw_cell(self, curRow, curCol);
        XTableWidgetItem* prevItem = xtw_cell(self, prevRow, prevCol);
        self->m_selectionRow = (int16_t)curRow;
        self->m_selectionColumn = (int16_t)curCol;
        /* 对齐 Qt（qtablewidget.cpp:1700-1709）：仅当新旧至少一个 item
         * 非空时才发 currentItemChanged。 */
        if (curItem || prevItem) {
            XVarList* a = XVarList_Create(
                XVar(const XTableWidgetItem*, curItem),
                XVar(const XTableWidgetItem*, prevItem));
            if (a) {
                if (((XObject*)self)->m_signalSlot)
                    XObject_emitSignal(
                        (XObject*)self,
                        (size_t)XTableWidget_currentItemChanged_signal, a,
                        NULL, NULL, XEVENT_PRIORITY_NORMAL);
                else
                    XVarList_delete(a);
            }
        }
        {
            int vr = curRow;
            int vc = curCol;
            int pr = prevRow;
            int pc = prevCol;
            XVarList* a = XVarList_Create(
                XVar(int, vr), XVar(int, vc), XVar(int, pr), XVar(int, pc));
            if (a) {
                if (((XObject*)self)->m_signalSlot)
                    XObject_emitSignal(
                        (XObject*)self,
                        (size_t)XTableWidget_currentCellChanged_signal, a,
                        NULL, NULL, XEVENT_PRIORITY_NORMAL);
                else
                    XVarList_delete(a);
            }
        }
    }
    if (changed) {
        /* 选区变化 ⇒ **按元素矩形**标脏（AGENTS.md 重绘纪律：不要整表
         * XWidget_update；2000 行表上一次单击整表重绘不可接受）。脏区为
         * 旧/新选中集合差异格的包围盒。 */
        if (!XRect_isEmpty(&dirty))
            XWidget_updateRect((XWidget*)self, &dirty);
        else
            XWidget_update((XWidget*)self); /* 包围盒不可算（如格被滚出）兜底。 */
    }
}

/** @brief selectionChanged 桥槽：模型选区变化 → 条目位镜像。
 *  @details 对齐 Qt QTableWidgetPrivate::setup 的选择信号接线
 *           （qtablewidget.cpp:1636-1637：`selectionChanged` 直连
 *           `QTableWidget::itemSelectionChanged`）；本库另外必须把条目位
 *           写成与模型一致（Qt 的 QTableModel 是唯一存储，无第二份位）。
 *           重入闸：镜像期间条目写入不再回写模型，故本槽可安全重入。
 *  @param receiver 接收者（恒为 XTableWidget*）。
 *  @param args     未使用（selectSelection 可携带起止区间，本槽按全量对齐）。
 */
static void xtw_selChangedSlot(XObject* receiver, XVarList* args)
{
    XTableWidget* self = (XTableWidget*)receiver;
    (void)args;
    if (!self) return;
    if (self->m_selSyncDepth > 0) {
        /* 已在一次同步事务内：由事务尾部的 xtw_selSyncDone 统一收口，
         * 避免重复镜像与重复发射。 */
        return;
    }
    ++self->m_selSyncDepth;
    xtw_selSyncDone(self, false);
    --self->m_selSyncDepth;
}

/** @brief currentChanged 桥槽：模型当前索引变化 → 表格当前格 + 两个信号。
 *  @details 对齐 Qt QTableWidgetPrivate::emitCurrentItemChanged
 *           （qtablewidget.cpp:1700-1709）：先 `currentItemChanged(item,
 *           prevItem)`（仅当前/前项至少一个非空时才发），再
 *           `currentCellChanged(curRow, curCol, prevRow, prevCol)`（恒发）。
 *           本机 Qt 6.8.3 实测一次 setCurrentCell 的信号次序：
 *           itemSelectionChanged → currentItemChanged → currentCellChanged。
 *           本库模型在 select 期间先发 selectionChanged、再发 currentChanged
 *           （qitemselectionmodel.cpp:1397-1409 同序），故本槽在事务闸内
 *           **只记录当前格**，把两个信号的发射推迟到事务尾部
 *           （xtw_selSyncDone 按 Qt 次序统一发），保证次序与 Qt 一致。
 *  @param receiver 接收者（恒为 XTableWidget*）。
 *  @param args     (curRow, curCol, prevRow, prevCol) 四整型。
 */
static void xtw_selCurrentChangedSlot(XObject* receiver, XVarList* args)
{
    XTableWidget* self = (XTableWidget*)receiver;
    int curRow;
    int curCol;
    if (!self || !args) return;
    {
        /* [args_N 遮蔽陷阱] 解包整段放块内，同块取值拷出。 */
        XVarList_args_4(args, int, vr, int, vc, int, pr, int, pc);
        curRow = vr;
        curCol = vc;
        (void)pr;
        (void)pc;
    }
    /* 越界收敛：模型可能带着出界当前索引（如删行后未裁剪）。 */
    if (curRow < 0 || curRow >= self->m_rows ||
        curCol < 0 || curCol >= self->m_columns) {
        curRow = -1;
        curCol = -1;
    }
    /* 反写视图当前格（绘制高亮与 currentRow/currentColumn 查询的事实源）。 */
    self->m_base.m_base.m_currentRow = (int16_t)curRow;
    self->m_base.m_base.m_currentColumn = (int16_t)curCol;
    if (self->m_selSyncDepth > 0) return; /* 事务内：尾部统一发射。 */
    ++self->m_selSyncDepth;
    xtw_selSyncDone(self, false);
    --self->m_selSyncDepth;
}

/** @brief 挂接选择模型的 selectionChanged/currentChanged（幂等）。
 *  @details 对标 Qt QAbstractItemView::setSelectionModel 里的
 *           connect(selectionChanged/currentChanged)
 *           （qabstractitemview.cpp:805-812）；本库在 init 调一次，换选择
 *           模型时由 xtw_selModel 的指针比对自愈重挂（见该函数注释）。
 *           连接用 Unique 位去重，重复调用不产生第二份连接
 *           （XSignalSlot.c:259-267 的 UniqueConnection）。
 *  @param self 目标表格；可为 NULL（无操作）。
 */
static void xtw_selArm(XTableWidget* self)
{
    XItemSelectionModel* sm;
    if (!self) return;
    sm = self->m_base.m_base.m_selectionModel;
    if (!sm) return;
    /* Unique 语义探测：已登记则返回 NULL（幂等），未登记才新建。 */
    if (!self->m_smSelConn) {
        self->m_smSelConn = XObject_connect(
            (XObject*)sm,
            XSignal(XItemSelectionModel_selectionChanged_signal),
            (XObject*)self, xtw_selChangedSlot,
            (XConnectionType)(XConnectionType_Direct |
                              XConnectionType_Unique));
    }
    if (!self->m_smCurConn) {
        self->m_smCurConn = XObject_connect(
            (XObject*)sm,
            XSignal(XItemSelectionModel_currentChanged_signal),
            (XObject*)self, xtw_selCurrentChangedSlot,
            (XConnectionType)(XConnectionType_Direct |
                              XConnectionType_Unique));
    }
    self->m_smArmed = sm;
}

/** @brief 断接选择模型的两个桥连接（析构/换模型前置）。
 *  @param self 目标表格；可为 NULL（无操作）。 */
static void xtw_selDisarm(XTableWidget* self)
{
    XItemSelectionModel* sm;
    if (!self) return;
    /* 以「已挂桥的模型」为准注销：换模型后视图字段已指向新模型，按它
     * 去 disconnect 会找不到旧连接（旧模型随 setSelectionModel 一起被
     * XClassDelete，其信号槽表已释放，此处只需清本地簿记）。 */
    sm = self->m_smArmed;
    if (sm && self->m_smSelConn)
        XObject_disconnect((XObject*)sm,
                           XSignal(XItemSelectionModel_selectionChanged_signal),
                           (XObject*)self, xtw_selChangedSlot);
    if (sm && self->m_smCurConn)
        XObject_disconnect((XObject*)sm,
                           XSignal(XItemSelectionModel_currentChanged_signal),
                           (XObject*)self, xtw_selCurrentChangedSlot);
    self->m_smSelConn = NULL;
    self->m_smCurConn = NULL;
    self->m_smArmed = NULL;
}

/** @brief 选区行列重映射（插入/删除行列后保持选中语义）。
 *  @details 对标 Qt：QItemSelectionModel 监听模型的 rowsInserted/
 *           rowsRemoved/columnsInserted/columnsRemoved 并持久索引平移
 *           （qitemselectionmodel.cpp:646-836）。本库选择模型只存
 *           (row<<16)|col 编码（XItemSelectionModel.h:68），维度变更是本表
 *           自己做的，故由本表显式重映射。
 *           规则：删除时被删轴上的选中格丢弃、其后索引前移；插入时该轴
 *           及之后索引后移。
 *           **一次性事务 + 静默批量重建**（2026-11-15 返工③）：整个重映射
 *           在 m_selSyncDepth>0 内完成——桥槽只登记不发射；写回走
 *           XItemSelectionModel_rebuildSelection（清空 + 保序批量装载 +
 *           单次 selectionChanged），总体 O(N)。旧实现逐格 select（模型
 *           内部线性查重 ⇒ O(N²)，探针 500×16 全选后 insertRow 达 125ms）
 *           且逐格发信号 ⇒ 信号风暴；中版聚段 selectSelection 仍把查重
 *           留在逐格路径。Qt 的持久索引迁移是 O(n) 批量（changePersistent
 *           IndexList，qabstractitemmodel.cpp:2228-2260），本入口即其对位。
 *  @param self  目标表格；可为 NULL（无操作）。
 *  @param axis  0=行、1=列。
 *  @param at    变更位置（插入位/删除位）。
 *  @param delta +n 插入、-1 删除。
 */
static void xtw_selRemap(XTableWidget* self, int axis, int at, int delta)
{
    XItemSelectionModel* sm;
    int* rows = NULL;
    int* cols = NULL;
    int n;
    int i;
    if (!self || (axis != 0 && axis != 1) || delta == 0) return;
    sm = self->m_base.m_base.m_selectionModel;
    if (!sm) return;
    n = XItemSelectionModel_selectedCount(sm);
    if (n <= 0) return;
    rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    if (!rows || !cols) {
        if (rows) XFree_System(rows);
        if (cols) XFree_System(cols);
        return;
    }
    n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
    /* 整个重映射收进一次事务：重建在闸内静默完成（rebuildSelection 尾部
     * 的一次 selectionChanged 也被闸吞掉），尾部 xtw_selSyncDone 统一
     * 镜像 + 一次 itemSelectionChanged。 */
    ++self->m_selSyncDepth;
    /* 逐格算重映射后的坐标，丢弃被删轴上的格（就地压缩；保持原相对
     * 顺序——与 Qt 持久索引迁移的保序性质一致）。 */
    {
        int m = 0; /* 重映射后仍有效的格数（rows/cols 前 m 项）。 */
        for (i = 0; i < n; ++i) {
            int nr = rows[i];
            int nc = cols[i];
            if (axis == 0) {
                if (delta < 0) {
                    if (nr == at) continue;
                    if (nr > at) nr += delta;
                } else if (nr >= at) {
                    nr += delta;
                }
            } else {
                if (delta < 0) {
                    if (nc == at) continue;
                    if (nc > at) nc += delta;
                } else if (nc >= at) {
                    nc += delta;
                }
            }
            rows[m] = nr;
            cols[m] = nc;
            ++m;
        }
        (void)XItemSelectionModel_rebuildSelection(sm, rows, cols, m);
    }
    xtw_selSyncDone(self, true); /* 批量重建点：位集必已变，强制发射。 */
    --self->m_selSyncDepth;
    XFree_System(rows);
    XFree_System(cols);
}

/** @brief 选区与当前格跟随**行置换**搬移（排序后高亮跟着同一行数据走）。
 *  @details 对标 Qt `QTableModel::sort` 的持久索引迁移（qtablewidget.cpp:
 *           533-537：`changePersistentIndexList` 让 selectionModel 的索引
 *           跟着条目走，而不是留在行号上）。本库选择模型存 (row<<16)|col
 *           编码，故按置换表重写行分量。两条调用路径的发射口径不同：
 *           - `XTableWidget_sortItems`（emitSignals=true）：§B4 既有契约
 *             要求排序后发一次 itemSelectionChanged（回归锁
 *             sortFollowsMultiCellSelection 锁死）；
 *           - `xtw_moveRow` 的排序插入（emitSignals=false）：**静默迁移**，
 *             对齐 Qt 的 rowsMoved 路径——本机 Qt 6.8.0 offscreen 探针
 *             （sortingEnabled + setItem 触发行移动）实测 rowsMoved=1、
 *             itemSelectionChanged=0、currentCellChanged=0。静默分支仍须
 *             换新位图快照与条目镜像（否则下一次真实选区变化按旧快照比对
 *             会漏发），只是不发射、直接对齐「上次已发射」快照。
 *  @param self        目标表格；可为 NULL（无操作）。
 *  @param perm        perm[新行] = 旧行；长度 rows。
 *  @param rows        行数。
 *  @param emitSignals 是否在事务尾部发射三族选区信号（见上）。
 */
static void xtw_selFollowRowPermutation(XTableWidget* self, const int* perm,
                                        int rows, bool emitSignals)
{
    XItemSelectionModel* sm;
    int* inv = NULL; /* inv[旧行] = 新行 */
    int* selRows = NULL;
    int* selCols = NULL;
    int n;
    int i;
    if (!self || !perm || rows <= 0) return;
    sm = self->m_base.m_base.m_selectionModel;
    if (!sm) return;
    inv = (int*)XMalloc_System(sizeof(int) * (size_t)rows);
    if (!inv) return;
    for (i = 0; i < rows; ++i) {
        if (perm[i] >= 0 && perm[i] < rows) inv[perm[i]] = i;
    }
    n = XItemSelectionModel_selectedCount(sm);
    if (n > 0) {
        selRows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        selCols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    }
    if (n > 0 && (!selRows || !selCols)) {
        if (selRows) XFree_System(selRows);
        if (selCols) XFree_System(selCols);
        XFree_System(inv);
        return;
    }
    if (n > 0) n = XItemSelectionModel_selectedIndexes(sm, selRows, selCols, n);
    /* 选区随置换批量搬移（返工①③同口径：rebuildSelection 保序批量装载，
     * O(N)——旧逐格 select 在模型内部线性查重 ⇒ O(N²)，全选大表排序卡顿；
     * Qt 的 changePersistentIndexList 也是一次批量迁移，
     * qtablewidget.cpp:533-537）。 */
    ++self->m_selSyncDepth;
    for (i = 0; i < n; ++i) {
        int r = selRows[i];
        if (r < 0 || r >= rows) {
            selRows[i] = -1; /* 越界格整格作废（rebuild 跳过负坐标）。 */
            selCols[i] = -1;
            continue;
        }
        selRows[i] = inv[r];
    }
    (void)XItemSelectionModel_rebuildSelection(sm, selRows, selCols, n);
    /* 当前索引同步搬移（否则当前格高亮留在旧行号上，Qt 由同一
     * changePersistentIndexList 一并迁移）。 */
    {
        int curRow = -1;
        int curCol = -1;
        if (XItemSelectionModel_currentIndex(sm, &curRow, &curCol) &&
            curRow >= 0 && curRow < rows) {
            XItemSelectionModel_setCurrentIndex(
                sm, inv[curRow], curCol,
                XItemSelectionModelSelectionFlag_NoUpdate);
        }
    }
    if (emitSignals) {
        /* 恒等置换已在 sortItems 入口早退（moved 计数），到此处置换必非
         * 恒等 ⇒ 位集必变 ⇒ 强制发射（diff 快照与维度布局无关的重建点
         * 同口径）。 */
        xtw_selSyncDone(self, true);
    } else {
        int curRow = -1;
        int curCol = -1;
        /* 静默迁移（Qt rowsMoved 口径）：换新快照 + 刷条目镜像 + 标脏，
         * 不发信号。`m_selectionRow/Column` 是「上一次已发射的当前格」
         * 快照（见 xtw_selSyncDone），此处当前格已随之搬移 ⇒ 必须同步，
         * 否则下一次真实 currentChanged 会带着搬移前的旧值当 prev。
         * 仅在**有选中格**时做快照/镜像重扫：行置换把条目连同选中态一起
         * 搬走，条目 m_selected 位天然正确；空选区时快照恒全 0（每次收尾
         * 按模型重建）且行置换不改变全 0 ⇒ 可跳过 O(R·C) 重扫，避免
         * 「无选区 + sortingEnabled 批量 setItem」每插一行扫全表。 */
        if (n > 0) {
            XRect dirty;
            (void)xtw_selDiffSnapshot(self, &dirty);
            (void)xtw_selMirror(self, NULL);
            if (!XRect_isEmpty(&dirty))
                XWidget_updateRect((XWidget*)self, &dirty);
        }
        if (!XItemSelectionModel_currentIndex(sm, &curRow, &curCol) ||
            curRow < 0 || curRow >= self->m_rows || curCol < 0 ||
            curCol >= self->m_columns) {
            curRow = -1;
            curCol = -1;
        }
        self->m_selectionRow = (int16_t)curRow;
        self->m_selectionColumn = (int16_t)curCol;
    }
    --self->m_selSyncDepth;
    if (selRows) XFree_System(selRows);
    if (selCols) XFree_System(selCols);
    XFree_System(inv);
}

/** @brief 收缩维度后裁剪选区（丢弃越界索引；**静默重建，不收口**）。
 *  @details 对标 Qt 模型 rowsRemoved/columnsRemoved 时 QItemSelectionModel
 *           丢弃越界索引（qitemselectionmodel.cpp:646-836）。本表
 *           setRowCount/setColumnCount 收缩走这里（Qt 的
 *           QTableModel::setRowCount = removeRows(rows, rc-rows)，
 *           qtablewidget.cpp:353-361 ⇒ 与删行同一 remap 路径，返工④）。
 *           写回走 XItemSelectionModel_rebuildSelection（O(N) 批量，
 *           返工③同口径；旧逐格 select 为 O(N²)）。
 *           **不收口**（返工⑤信号合并）：选区裁剪与当前格迁移须在同一
 *           事务内由调用方一次收口——分开收口会把 currentCellChanged
 *           拆成两次，且第一次带着「迁移前」的收敛值（实测：current=(4,0)
 *           setRowCount(2)，trim 收口发 (‑1,‑1,4,0)、迁移收口再发
 *           (1,0,‑1,‑1)；Qt 只有一次 (1,0,4,0)，qitemselectionmodel.cpp:
 *           654-670）。
 *  @param self 目标表格；可为 NULL（无操作）。
 *  @param axis 0=行、1=列。
 *  @param keep 保留的数量（>=keep 的索引全部丢弃）。
 *  @return 选中集合有实际重建返回 true（调用方据此决定收口时是否
 *          强制选区发射——重建点位集必变，diff 快照可能因维度布局而
 *          漏判，见 xtw_selSyncDone 的 forceSel 注）。
 */
static bool xtw_selTrimAxis(XTableWidget* self, int axis, int keep)
{
    XItemSelectionModel* sm;
    int* rows = NULL;
    int* cols = NULL;
    int n;
    int i;
    int m = 0;
    bool dropped = false;
    bool rebuilt = false;
    if (!self || keep < 0) return false;
    sm = self->m_base.m_base.m_selectionModel;
    if (!sm) return false;
    n = XItemSelectionModel_selectedCount(sm);
    if (n <= 0) return false;
    rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    if (!rows || !cols) {
        if (rows) XFree_System(rows);
        if (cols) XFree_System(cols);
        return false;
    }
    n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
    for (i = 0; i < n; ++i) {
        if (((axis == 0) ? rows[i] : cols[i]) >= keep) { dropped = true; break; }
    }
    if (!dropped) {
        XFree_System(rows);
        XFree_System(cols);
        return false;
    }
    /* 保留格就地压缩（保序），批量重建（调用方事务闸内静默）。 */
    for (i = 0; i < n; ++i) {
        if (((axis == 0) ? rows[i] : cols[i]) >= keep) continue;
        rows[m] = rows[i];
        cols[m] = cols[i];
        ++m;
    }
    rebuilt = XItemSelectionModel_rebuildSelection(sm, rows, cols, m);
    XFree_System(rows);
    XFree_System(cols);
    return rebuilt;
}

/** @brief 维度变更后把选区快照**重排**到新 R×C 位布局（返工④⑤配套）。
 *  @details m_selSnapshot 的位下标按「行 × 当前列数」布局，diffSnapshot
 *           以**当前维度**逐格读取——行列数一变，同一位下标的 (row,col)
 *           语义就平移/越界，后续 diff 会把真实差异误判成「无变化」或
 *           凭空报差异。本助手把旧布局中仍在新维度内的选中位搬到新布局
 *           对应下标（越界位丢弃）。不碰选择模型——模型侧由 remap/trim
 *           负责；这里只修「快照跟随维度」这一件事。
 *           调用点：setRowCount/setColumnCount 的维度推进之后（扩容不走
 *           trim/remap ⇒ 快照只有这里能跟上；收缩随后还有 trim+force 的
 *           二次刷新，本助手先裁越界位，幂等无害）。remap 类路径（insert/
 *           remove 行列）维度变更后必走 selSyncDone(true) 全量换新快照，
 *           不需要这里。
 *  @param self     目标表格；可为 NULL（无操作）。
 *  @param oldRows  变更前行数。
 *  @param oldCols  变更前列数。
 */
static void xtw_selSnapshotReshape(XTableWidget* self, int oldRows,
                                   int oldCols)
{
    XBitArray* reshaped;
    int r;
    int c;
    size_t total;
    if (!self || !self->m_selSnapshot) return;
    total = (size_t)(self->m_rows > 0 ? self->m_rows : 1) *
            (size_t)(self->m_columns > 0 ? self->m_columns : 1);
    reshaped = XBitArray_create(total);
    if (!reshaped) return; /* 分配失败：保留旧快照（下次 diff 兜底口径）。 */
    for (r = 0; r < oldRows && r < self->m_rows; ++r) {
        for (c = 0; c < oldCols && c < self->m_columns; ++c) {
            if (!XBitArray_getBit(self->m_selSnapshot,
                                  (size_t)r * (size_t)oldCols + (size_t)c))
                continue;
            (void)XBitArray_setBit(reshaped,
                                   (size_t)r * (size_t)self->m_columns +
                                       (size_t)c, true);
        }
    }
    XClassDelete((XClass*)self->m_selSnapshot);
    self->m_selSnapshot = reshaped;
}

/** @brief 删除区间 [start,end] 后的**当前格就近迁移**（返工⑤：当前格
 *         单一事实源 + 信号不丢）。
 *  @details 对标 Qt 6.8 直译：`QItemSelectionModelPrivate::
 *           rowsAboutToBeRemoved`（qitemselectionmodel.cpp:654-670 行版 /
 *           `columnsAboutToBeRemoved`:735-751 列版）——
 *           ① **仅当**当前索引落在被删区间内才处理；区间外**不动**
 *              （QModelIndex 按删除后的行号重解释，Qt 不做持久平移）；
 *           ② 区间内迁移目标取**上邻** `start-1`（左侧还有保留段时），
 *              否则取**下邻**（删除后坐标系里即 `start` 位；Qt 判据
 *              `end < 旧维度-1` 与本表 `start < 新维度` 等价），整轴已空
 *              才置无效（对齐 659-666 的三分支）；
 *           ③ 迁移发射 currentChanged（Qt :667-670）——本函数只做**迁移
 *              写入**（闸内静默），收口（currentItemChanged +
 *              currentCellChanged）由调用方统一一次完成；独立调用的
 *              removeRow/removeColumn 按返回值自行包一次收口。与
 *              xtw_selTrimAxis 同一事务时（setRowCount/setColumnCount
 *              收缩）信号合并为一次，值即最终迁移值（Qt 口径）。
 *           旧实现（clampCurrent「越界清 -1」+ removeRow「>=row 全平移」
 *           的视图直写）是第二事实源：删当前行把当前格清没了（Qt 保留
 *           上邻）、setRowCount 收缩只清不发、模型/视图/快照三处分叉后
 *           同格判定吞掉 currentCellChanged。三处调用点（setRowCount/
 *           setColumnCount/removeRow/removeColumn）现统一走本入口。
 *  @param self  目标表格；可为 NULL（无操作）。
 *  @param axis  0=行、1=列。
 *  @param start 被删区间起点（含；删除后坐标系）。
 *  @param end   被删区间终点（含）。
 *  @return 当前索引被迁移或清空返回 true（区间外/无效恒 false——调用方
 *          无须为 false 值收口）。
 */
static bool xtw_selCurrentRangeRemoved(XTableWidget* self, int axis,
                                       int start, int end)
{
    XItemSelectionModel* sm;
    int r = -1;
    int c = -1;
    int curOnAxis;
    int target;
    int otherAxis;
    if (!self || (axis != 0 && axis != 1) || start > end) return false;
    sm = self->m_base.m_base.m_selectionModel;
    if (!sm) return false;
    if (!XItemSelectionModel_currentIndex(sm, &r, &c)) return false;
    curOnAxis = (axis == 0) ? r : c;
    if (curOnAxis < start || curOnAxis > end)
        return false; /* 区间外：不动（Qt 同）。 */
    {
        int keepCount = (axis == 0) ? self->m_rows : self->m_columns;
        if (start > 0)
            target = start - 1; /* 上邻（不受删除影响）。 */
        else if (keepCount > start)
            target = start; /* 下邻在删除后坐标系里的位置。 */
        else
            target = -1; /* 整轴删空。 */
    }
    if (target < 0) {
        XItemSelectionModel_clearCurrentIndex(sm); /* 发 currentChanged（闸内静默）。 */
        return true;
    }
    otherAxis = (axis == 0) ? c : r;
    /* NoUpdate：只移当前索引、不触选区（选区已由 remap/trim 平移完）。 */
    if (axis == 0)
        XItemSelectionModel_setCurrentIndex(sm, target, otherAxis,
                                            XItemSelectionModelSelectionFlag_NoUpdate);
    else
        XItemSelectionModel_setCurrentIndex(sm, otherAxis, target,
                                            XItemSelectionModelSelectionFlag_NoUpdate);
    return true;
}

/** @brief 施加选择命令（唯一事实源＝选择模型；返回选区是否实际变化）。
 *
 *        [§B4 2026-11-15] 对标 Qt 6.8：`QTableWidget` 的选择状态全部由
 *        `selectionModel()` 承载（`QTableWidgetItem::isSelected/setSelected`
 *        直读/直写选择模型，qtablewidget.cpp:1087-1112；
 *        `setCurrentCell(_,_,command)` 走
 *        `d->selectionModel->setCurrentIndex(...)`，qtablewidget.cpp:
 *        2237-2242）。此前本表的选中态以条目 `m_selected` 位为事实源，
 *        选择模型从不参与；现在命令**直接施加到选择模型**（Rows/Columns
 *        展开、Clear 先清、Select/Deselect/Toggle 逐格，语义见
 *        XItemSelectionModel_selectSelection），条目位由
 *        `xtw_selMirror` 单向镜像刷新——两向同源、同参。
 *
 *        防回环：镜像与命令写模型都收在 `m_selSyncDepth>0` 的事务闸内
 *        （条目位是模型的单向派生镜像，不存在「条目位写入反推模型」的
 *        回路）；模型侧的 selectionChanged 由 `xtw_selChangedSlot` 收口为
 *        一次镜像 + 一次 itemSelectionChanged 发射（本机 Qt 6.8.3 实测：
 *        一次选区变化只发一次 itemSelectionChanged）。
 *
 *        空格（无条目）在 Qt 的 QTableModel 里仍是可选中的索引；本库
 *        `xtw_selMirror` 对空格跳过（无位可承载），故空格不改变
 *        `selectedItems()` 结果，与 Qt 一致（qtablewidget.cpp:2408-2422
 *        只收集恰有 item 的索引）。
 *
 * @param self    目标表格。
 * @param row     目标行（<0 表示无目标格，仅 Clear 生效）。
 * @param column  目标列（<0 同上）。
 * @param command XItemSelectionModelSelectionFlag 位组合。
 * @return 选中集合有实际变化返回 true。
 */
static bool xtw_applySelectionCommand(XTableWidget* self, int row, int column,
                                      int command)
{
    XItemSelectionModel* sm;
    if (!self || command == XItemSelectionModelSelectionFlag_NoUpdate)
        return false;
    sm = xtw_selModel(self);
    if (!sm) return false;
    /* 事务闸：模型信号在 select/selectSelection 内部同步发射；本函数只做
     * 模型写入，镜像与 itemSelectionChanged 由调用方在事务尾部经
     * xtw_selSyncDone 统一收口（避免逐格/多次发射）。
     * 模型入口的返回值就是「集合是否实际变化」——不能用计数差判，
     * 等量换格（ClearAndSelect 从 A 格换到 B 格）计数不变但集合已变。 */
    if (row < 0 || column < 0) {
        if (command & XItemSelectionModelSelectionFlag_Clear)
            return XItemSelectionModel_clearSelection(sm);
        return false;
    }
    return XItemSelectionModel_select(sm, row, column, command);
}

/** @brief 2 参 setCurrentCell 的缺省选择命令（对齐 Qt
 *         QAbstractItemViewPrivate::selectionCommand 的缺省口径）：
 *         NoSelection ⇒ NoUpdate；SelectRows/SelectColumns ⇒ 对应展开位
 *         + ClearAndSelect；其余 ⇒ ClearAndSelect。 */
static int xtw_currentSelectionCommand(const XTableWidget* self)
{
    int mode;
    int behavior;
    if (!self) return XItemSelectionModelSelectionFlag_NoUpdate;
    mode = XAbstractItemView_selectionMode(&self->m_base.m_base);
    if (mode == XAbstractItemViewSelectionMode_NoSelection)
        return XItemSelectionModelSelectionFlag_NoUpdate;
    behavior = XAbstractItemView_selectionBehavior(&self->m_base.m_base);
    if (behavior == XAbstractItemViewSelectionBehavior_SelectRows)
        return XItemSelectionModelSelectionFlag_ClearAndSelect |
               XItemSelectionModelSelectionFlag_Rows;
    if (behavior == XAbstractItemViewSelectionBehavior_SelectColumns)
        return XItemSelectionModelSelectionFlag_ClearAndSelect |
               XItemSelectionModelSelectionFlag_Columns;
    return XItemSelectionModelSelectionFlag_ClearAndSelect;
}

void XTableWidget_setCurrentCell(XTableWidget* self, int row, int column)
{
    XTableWidget_setCurrentCell_2(self, row, column,
                                  xtw_currentSelectionCommand(self));
}

void XTableWidget_setCurrentCell_2(XTableWidget* self, int row, int column,
                                   int command)
{
    XItemSelectionModel* sm;
    int prevR;
    int prevC;
    int cmd;
    if (!self) return;
    sm = xtw_selModel(self);
    prevR = self->m_base.m_base.m_currentRow;
    prevC = self->m_base.m_base.m_currentColumn;
    /* 越界收敛（对齐 Qt：无效索引清除当前项）。 */
    if (row < 0 || row >= self->m_rows ||
        column < 0 || column >= self->m_columns) {
        row = -1;
        column = -1;
    }
    if (!sm) return;
    /* [§B4 2026-11-15] 对齐 Qt 6.8：`QTableWidget::setCurrentCell(row, col,
     * command)` 直走 `d->selectionModel->setCurrentIndex(model()->index(...),
     * command)`（qtablewidget.cpp:2237-2242）。选择命令与当前索引因此
     * **由选择模型一次事务完成**，模型随后同步回抛
     * currentChanged 与 selectionChanged；桥槽在事务闸（m_selSyncDepth>0）
     * 内只登记当前格，三信号的次序与发射统一由尾部 xtw_selSyncDone 收口
     * ——对标本机 Qt 6.8.3 实测次序 itemSelectionChanged →
     * currentItemChanged → currentCellChanged（各一次）。
     * 命令为 NoUpdate 时不触碰选区（Qt 同：只移当前索引）。 */
    cmd = command;
    /* [§B4 复核修订 2026-11-15] 「同格」必须以**选择模型的当前索引**为
     * 基准（单一事实源），不能拿视图字段比：删行/删列/收缩后视图与模型
     * 可能一方已前移，按视图 prevR 判同格会与 XItemSelectionModel_
     * setCurrentIndex 内部的派 prevRow 判定分叉，两边都不发信号。 */
    {
        int modelRow = -1;
        int modelCol = -1;
        if (XItemSelectionModel_currentIndex(sm, &modelRow, &modelCol)) {
            prevR = modelRow;
            prevC = modelCol;
        } else {
            prevR = -1;
            prevC = -1;
        }
    }
    ++self->m_selSyncDepth;
    if (row == prevR && column == prevC) {
        /* 同格重设：索引未变，仍按 command 改变选区（Qt
         * qitemselectionmodel.cpp:1389-1409）；currentChanged 族不发射
         * （快照与当前格相同，xtw_selSyncDone 不触发该族）。 */
        (void)xtw_applySelectionCommand(self, row, column, cmd);
        xtw_selSyncDone(self, false);
        --self->m_selSyncDepth;
        return;
    }
    /* 当前索引经选择模型写入（内部按 command 先行施加选择命令，再发
     * currentChanged 族；qitemselectionmodel.cpp:1397-1409）。 */
    XItemSelectionModel_setCurrentIndex(sm, row, column, cmd);
    if (row >= 0)
        XAbstractItemView_scrollToHint(&self->m_base.m_base, row, column,
                                       XAbstractItemViewScrollHint_EnsureVisible);
    /* 视图当前格兜底同步（桥槽已写，幂等）。 */
    self->m_base.m_base.m_currentRow = (int16_t)row;
    self->m_base.m_base.m_currentColumn = (int16_t)column;
    xtw_selSyncDone(self, false);
    --self->m_selSyncDepth;
}

int XTableWidget_currentRow(const XTableWidget* self)
{ return self ? self->m_base.m_base.m_currentRow : -1; }
int XTableWidget_currentColumn(const XTableWidget* self)
{ return self ? self->m_base.m_base.m_currentColumn : -1; }

XTableWidgetItem* XTableWidget_currentItem(const XTableWidget* self)
{
    if (!self) return NULL;
    return xtw_cell(self, self->m_base.m_base.m_currentRow,
                    self->m_base.m_base.m_currentColumn);
}

void XTableWidget_setCurrentItem(XTableWidget* self,
                                 const XTableWidgetItem* item)
{
    int row;
    int col;
    if (!self) return;
    if (!item || item->m_view != self) {
        /* 对齐 Qt：非本表条目 → 无效索引 → 清除当前项。 */
        XTableWidget_setCurrentCell(self, -1, -1);
        return;
    }
    row = XTableWidget_indexFromItem(self, item, &col);
    if (row < 0 || col < 0) {
        XTableWidget_setCurrentCell(self, -1, -1);
        return;
    }
    XTableWidget_setCurrentCell(self, row, col);
}

void XTableWidget_setCurrentItem_2(XTableWidget* self,
                                   const XTableWidgetItem* item, int command)
{
    int row;
    int col;
    if (!self) return;
    if (!item || item->m_view != self) {
        /* 对齐 Qt：非本表条目 ⇒ 无效索引（当前项清除），命令照施。 */
        XTableWidget_setCurrentCell_2(self, -1, -1, command);
        return;
    }
    row = XTableWidget_indexFromItem(self, item, &col);
    if (row < 0 || col < 0) {
        XTableWidget_setCurrentCell_2(self, -1, -1, command);
        return;
    }
    XTableWidget_setCurrentCell_2(self, row, col, command);
}

/* ==================== 便捷族（对齐 QTableWidget 便捷接口） ==================== */

#if XRegularExpression_ON
/** @brief [§selrange] 对一段 UTF-8 主题串执行一次正则/通配命中判定
 *         （对标 Qt `t.contains(rx)`，qabstractitemmodel.cpp:2572-2577）。
 *  @param rx       已编译的正则（借用；可为 NULL/无效 ⇒ 恒不命中）。
 *  @param rxValid  编译是否有效（无效 pattern 全部返回 false，对齐 Qt
 *                  「QString::contains() called on an invalid QRegularExpression
 *                  object」的 0 条结果，本机 Qt 6.8 实测）。
 *  @param subject  UTF-8 主题串（空格按空串，同 Qt `v.toString()`）。
 *  @return 命中返回 true。
 *  @note 匹配对象由本函数创建并释放（所有权账：XRegularExpression_match_utf8
 *        返回新堆对象，调用方 XClassDelete）。
 */
static bool xtw_regexHit(const XRegularExpression* rx, bool rxValid,
                         const char* subject)
{
    XRegularExpressionMatch* m;
    bool hit;
    if (!rxValid || !rx) return false;
    m = XRegularExpression_match_utf8(
        rx, subject ? subject : "", 0,
        XRegularExpression_NormalMatch, XRegularExpression_NoMatchOption);
    if (!m) return false;
    hit = XRegularExpressionMatch_isValid(m) &&
          XRegularExpressionMatch_hasMatch(m);
    XClassDelete((XClass*)m);
    return hit;
}
#endif /* XRegularExpression_ON */

int XTableWidget_findItems(const XTableWidget* self, const char* text,
                           int flags, XTableWidgetItem** out, int maxCount)
{
    int total = 0;
    int written = 0;
    int row;
    int col;
    int mode;
    bool caseSensitive;
    XChar_CaseSensitivity cs;
    const char* needle;
    bool isRxMode;
#if XRegularExpression_ON
    XRegularExpression* rx = NULL;  /* 本函数拥有；出口统一 XClassDelete。 */
    bool rxValid = false;
#endif
    if (!self) return 0;
    /* [§B4 2026-11-15] 对齐 Qt 6.8 QTableWidget::findItems
     * （qtablewidget.cpp:2424-2437）：逐列 match，列优先输出。
     * flags 语义**逐位对齐 Qt::MatchFlag**（qnamespace.h:1530-1541）：
     *   类型 = flags & MatchTypeMask(0x0F)；MatchExactly=0、Contains=1、
     *   StartsWith=2、EndsWith=3、FixedString=8；MatchCaseSensitive=0x10。
     *   匹配口径按 QAbstractItemModel::match（qabstractitemmodel.cpp:
     *   2513-2611）：MatchExactly 走值相等（文本即**区分大小写**，不受
     *   MatchCaseSensitive 影响）；其余文本模式缺省**不区分大小写**，带
     *   MatchCaseSensitive 才敏感。
     *   本机 Qt 6.8.3 实测（Alpha/alphaBeta/betaAlpha 三行）：
     *     Contains("Alpha")=3、Contains|CS=2、StartsWith=2、StartsWith|CS=1、
     *     EndsWith=2、Exactly("Alpha")=1、Exactly("alpha")=0、
     *     FixedString("alpha")=1、FixedString|CS=0。
     *   [§selrange 2026-11-16] MatchRegularExpression(4)/MatchWildcard(5)
     *   接框架现成的 XRegularExpression（Src/XCore/XCode/XRegularExpression/），
     *   口径逐条对齐 Qt QAbstractItemModel::match（qabstractitemmodel.cpp:
     *   2545-2577）：正则 = `QRegularExpression(value.toString())` + CS 位
     *   控制 CaseInsensitiveOption、`t.contains(rx)` **部分匹配**；通配 =
     *   `QRegularExpression::wildcardToRegularExpression(value,
     *   NonPathWildcardConversion)`（**完整锚定**）+ CS 位。非法 pattern ⇒
     *   QRegularExpression 无效、contains 恒 false ⇒ 0 条（本机 Qt 6.8 实测）。
     *   空 needle 走引擎（不套用下面「非 Exactly 恒命中」短路）：正则空
     *   pattern 命中所有格（实测 6/6），通配空 pattern 全锚定只命中空串格
     *   （实测 3/6）。旧实现（无正则子系统）返回 0 条已退役。
     *   旧实现只认 flags==1 为精确、其余为包含，与 Qt 数值语义**相反**，
     *   属矩阵 :3871 登记的偏差，已对齐。 */
    mode = flags & XMatchFlag_MatchTypeMask;
    isRxMode = (mode == XMatchFlag_MatchRegularExpression ||
                mode == XMatchFlag_MatchWildcard);
#if !XRegularExpression_ON
    /* 极端裁剪构建（无正则引擎）：保持旧「不降级子串」口径 ⇒ 0 条。 */
    if (isRxMode) return 0;
#endif
    caseSensitive = (flags & XMatchFlag_MatchCaseSensitive) != 0;
    cs = caseSensitive ? XChar_CaseSensitive : XChar_CaseInsensitive;
    needle = text ? text : "";
#if XRegularExpression_ON
    if (mode == XMatchFlag_MatchRegularExpression) {
        /* Qt :2546-2553：以文本为模式；无 CS 位 ⇒ 忽略大小写。 */
        rx = XRegularExpression_create_utf8(
            needle, caseSensitive
                        ? XRegularExpression_NoPatternOption
                        : XRegularExpression_CaseInsensitiveOption);
        rxValid = rx && XRegularExpression_isValid(rx);
    } else if (mode == XMatchFlag_MatchWildcard) {
        /* Qt :2555-2562：wildcardToRegularExpression(NonPathWildcardConversion)
         * 完整锚定；fromWildcard 一并由 caseSensitivity 加忽略大小写选项。 */
        XString* wstr = XString_create_utf8(needle);
        if (wstr) {
            XStringView view = XStringView_create_string(wstr);
            rx = XRegularExpression_fromWildcard(
                &view, cs, XRegularExpression_NonPathWildcardConversion);
            XClassDelete((XClass*)wstr);
        }
        rxValid = rx && XRegularExpression_isValid(rx);
    }
#endif
    /* 对齐 Qt（qtablewidget.cpp:2424-2437）：**列优先**遍历——外层列、
     * 内层行，逐个 match。 */
    for (col = 0; col < self->m_columns; ++col) {
        for (row = 0; row < self->m_rows; ++row) {
            XTableWidgetItem* cell = xtw_cell(self, row, col);
            const XString* cellStr;
            const char* cellText;
            bool hit = false;
            if (!cell) {
                /* 空格：Qt 的 match 把无效值 toString() 成空串。
                 * §selrange：正则/通配走引擎对空串判定（不套用下面的
                 * 「空 needle 恒命中」短路）——本机 Qt 6.8 实测：空正则
                 * pattern 命中所有空格、空通配 pattern 全锚定亦命中空串、
                 * 非法 pattern 不命中；其余模式沿用旧口径（非 Exactly
                 * 下空 needle 命中，Exactly 下不命中）。 */
                bool hitEmpty = false;
#if XRegularExpression_ON
                if (isRxMode) {
                    hitEmpty = xtw_regexHit(rx, rxValid, "");
                } else
#endif
                if (needle[0] == '\0' && mode != XMatchFlag_MatchExactly) {
                    hitEmpty = true;
                }
                if (hitEmpty) {
                    if (out && written < maxCount) {
                        out[written] = cell;
                        ++written;
                    }
                    ++total;
                }
                continue;
            }
            cellStr = XTableWidgetItem_text_const(cell);
            cellText = XTableWidgetItem_text_toUtf8(cell);
            if (!cellText) cellText = "";
            /* 空 needle 必须显式处理：框架的 XString_contains_utf8/
             * startsWith_utf8/endsWith_utf8 底层 XStringView_indexOf 对空
             * 子串返回 -1（XStringView.c:373-376），而 Qt 6.8 的
             * QString::contains("")/startsWith("")/endsWith("") 恒为 true
             * ——本机 Qt 6.8.3 实测 findItems("", MatchContains) 在 3x2
             * 全空格表返回 6。故此处按 Qt 口径短路：非 Exactly 模式下
             * 空 needle 恒命中（含**有文本的格**与空格两种分支）。
             * §selrange：正则/通配**不套用此短路**——它们由引擎决定
             * （空正则部分匹配所有串、空通配全锚定只匹配空串、非法 pattern
             * 全不命中），走下面 switch 的正则 case。 */
            if (!isRxMode && needle[0] == '\0' &&
                mode != XMatchFlag_MatchExactly) {
                if (out && written < maxCount) {
                    out[written] = cell;
                    ++written;
                }
                ++total;
                continue;
            }
            /* [§B4 复核修订 2026-11-15] 所有非 Exactly 分支必须走
             * **文本化后的 cellText**：Qt 的 match 对所有非 Exactly 分支
             * 一律用 `v.toString()`（qabstractitemmodel.cpp:2569-2595），
             * 故数值列（DisplayRole=int32(42)）在 Contains("4") 下必须命中。
             * 旧实现用 cellStr（XTableWidgetItem_text_const 只覆盖 String 型，
             * 数值返回 NULL）⇒ 数值列在非 Exactly 模式永不命中，与本函数
             * 自己的注释矛盾。此处把 cellText 包成借用 XString 再走框架比较
             * 助手（避免手搓 ASCII 折叠）。 */
            if (isRxMode) {
                /* §selrange：正则/通配直接对文本化后的 cellText 走引擎
                 * （对齐 Qt `t.contains(rx)` 的 `v.toString()`，同 numerical
                 * 列语义）。非法 pattern ⇒ xtw_regexHit 恒 false ⇒ 0 条。 */
#if XRegularExpression_ON
                hit = xtw_regexHit(rx, rxValid, cellText);
#else
                hit = false; /* 开关关闭时上面已 return 0，此分支不可达。 */
#endif
            } else {
                XString tmp;
                bool useTmp = false;
                if (!cellStr) {
                    XString_init(&tmp);
                    XString_assign_utf8(&tmp, cellText);
                    cellStr = &tmp;
                    useTmp = true;
                }
                switch (mode) {
                case XMatchFlag_MatchStartsWith:
                    hit = XString_startsWith_utf8(cellStr, needle, cs);
                    break;
                case XMatchFlag_MatchEndsWith:
                    hit = XString_endsWith_utf8(cellStr, needle, cs);
                    break;
                case XMatchFlag_MatchFixedString:
                    hit = XString_equals_utf8(cellStr, needle, cs);
                    break;
                case XMatchFlag_MatchExactly:
                    /* MatchExactly 走 QVariant 相等（文本即区分大小写，
                     * 不受 MatchCaseSensitive 影响）。 */
                    hit = (XStrcmp(cellText, needle) == 0);
                    break;
                case XMatchFlag_MatchContains:
                default:
                    hit = XString_contains_utf8(cellStr, needle, cs);
                    break;
                }
                if (useTmp) XClassDeinit(&tmp);
            }
            if (!hit) continue;
            if (out && written < maxCount) {
                out[written] = cell;
                ++written;
            }
            ++total;
        }
    }
#if XRegularExpression_ON
    if (rx) XClassDelete((XClass*)rx); /* 本函数拥有的正则对象，出口一次释放。 */
#endif
    return total;
}

/** [K-68 2026-11-14] 合并区间覆盖格 → 原点（纯数据反查，无几何）。
 *  @details 对标 Qt QTableView::indexAt 的吸附段（qtableview.cpp:1695-
 *           1710：d->span(r,c) 把覆盖格反查回 span.top()/left()，反查在
 *           1702-1706）——span 表按**逻辑** (row,col) 存储、与几何无关。
 *           此前吸附经 XTableView_indexAt 重算坐标，其基类几何
 *           （XTV_HEADER_H=20、columnAt 当时按逻辑序累加列宽、无视觉
 *           映射）与本表不同源：moveSection 拖拽后覆盖格的吸附会反查
 *           到错误原点列（视觉位置 vs 逻辑位置错位，复核②）。现在命中
 *           格来自 xtw_cellAt（视觉序感知），吸附在本表 span 表上直接
 *           反查，与命中同源。[K-68 残留 a 2026-11-14] 裸视图的
 *           columnAt/列几何此后也补齐了视觉序映射（XTableView.c:1280-1314），
 *           两端同源；本表吸附仍走纯数据反查（Qt 同款，不经几何）。
 *  @return (row,col) 落在某合并区间覆盖内返回 true 并写原点；否则
 *          返回 false（调用方保持原格）。 */
static bool xtw_spanOrigin(const XTableWidget* self, int row, int col,
                           int* orow, int* ocol)
{
    int i;
    int n;
    if (!self) return false;
    n = self->m_base.m_spanCount;
    for (i = 0; i < n; ++i) {
        const XTableViewSpan* s = &self->m_base.m_spans[i];
        if (row >= s->row && row < s->row + s->rowSpan &&
            col >= s->column && col < s->column + s->columnSpan) {
            if (orow) *orow = s->row;
            if (ocol) *ocol = s->column;
            return true;
        }
    }
    return false;
}

XTableWidgetItem* XTableWidget_itemAt(const XTableWidget* self, int x, int y)
{
    int row = -1;
    int col = -1;
    if (!self) return NULL;
    xtw_cellAt(self, x, y, &row, &col);
    if (row < 0 || col < 0) return NULL;
    /* 对标 Qt itemAt：命中落在合并区间覆盖格时返回区间原点条目。
     * [根修 2026-11-05] 仅当命中格确在合并区间内才吸附——此前无条件经
     * XTableView_indexAt 重算，而其表头高（XTV_HEADER_H=20）/列宽口径与
     * XTableWidget 的 m_headerHeight/m_colWidths 不同源，(80,36) 在
     * cellAt=(0,0) 却被吸附成 (0,1)，itemAt 命中错格。
     * [K-68 2026-11-14] 吸附改走 xtw_spanOrigin 纯数据反查：命中格
     * (row,col) 已是视觉序感知的逻辑格，不再经基类几何二次命中
     * （columnAt 逻辑序累加、拖拽后与视觉位置错位——该基类缺口已由
     * 残留 a 补齐视觉序映射）；吸附语义与 Qt indexAt 的 span 反查段
     * 同构（qtableview.cpp:1695-1710，反查在 1702-1706）。 */
    if (XTableView_rowSpan(&self->m_base, row, col) > 1 ||
        XTableView_columnSpan(&self->m_base, row, col) > 1) {
        int orow = row;
        int ocol = col;
        if (xtw_spanOrigin(self, row, col, &orow, &ocol)) {
            row = orow;
            col = ocol;
        }
    }
    return xtw_cell(self, row, col);
}

int XTableWidget_selectedItems(const XTableWidget* self,
                               XTableWidgetItem** out, int maxCount)
{
    XItemSelectionModel* sm;
    int* rows;
    int* cols;
    int n;
    int i;
    int total = 0;
    int written = 0;
    if (!self) return 0;
    /* [§B4 2026-11-15] 对齐 Qt 6.8 QTableWidget::selectedItems
     * （qtablewidget.cpp:2408-2422）：取 `selectionModel()->selectedIndexes()`
     * 后逐索引映射回条目，**只返回实际存在条目的格**（空格跳过）、
     * 隐藏索引跳过（Qt 用 isIndexHidden 过滤）。顺序=选择模型的索引顺序
     * （选区并入顺序，XItemSelectionModel.h:219-233）。
     * 旧实现按行主序扫条目位并「补录当前跟踪格」，与 Qt 两处不符
     * （顺序 + 补录幻影），现按模型口径重写。 */
    sm = xtw_selModel(self);
    if (!sm) return 0;
    n = XItemSelectionModel_selectedCount(sm);
    if (n <= 0) return 0;
    rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    if (!rows || !cols) {
        if (rows) XFree_System(rows);
        if (cols) XFree_System(cols);
        return 0;
    }
    n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
    for (i = 0; i < n; ++i) {
        int r = rows[i];
        int c = cols[i];
        XTableWidgetItem* cell;
        /* 对齐 Qt：隐藏行/列上的索引经 isIndexHidden 过滤掉。 */
        if (r < 0 || r >= self->m_rows || c < 0 || c >= self->m_columns)
            continue;
        if (xtw_rowHidden(self, r) || xtw_colHidden(self, c)) continue;
        cell = xtw_cell(self, r, c);
        if (!cell) continue; /* 空格：Qt 只收集有 item 的索引。 */
        if (out && written < maxCount) {
            out[written] = cell;
            ++written;
        }
        ++total;
    }
    XFree_System(rows);
    XFree_System(cols);
    return total;
}

/** @brief 选中范围收集（§selrange：直读选择模型的范围表）。
 *  @details 对齐 Qt 6.8 `QTableWidget::selectedRanges`
 *           （qtablewidget.cpp:2381-2392）：Qt 直接返回
 *           `selectionModel()->selection()` 的真实范围表——
 *           `result.append({r.top(), r.left(), r.bottom(), r.right()})`，
 *           **不补洞、不重建**。本库 §selrange 改造后
 *           `XItemSelectionModel_selection()` 返回与 Qt 同款命令历史
 *           切分（ranges + merge(currentSelection,currentCommand)），
 *           故此处逐 range 直读转 `XTableWidgetSelectionRange`：
 *           ① 选 (0,0)+(0,2) ⇒ 2 条；② 2×2 全选 ⇒ 1 条；
 *           ③ 洞形（3×3 减中心）⇒ 4 条（split 的上/下/左/右碎片顺序）；
 *           ④ L 形/十字形 ⇒ 保留 Qt 命令历史切分（本机 Qt 6.8 探针：
 *              先整列 0 再整行 0 ⇒ [1,0,4,0] + [0,0,0,4] 两条），
 *              旧的「逐行重建」做不到这种切分。
 *           §B4 的「不跳过隐藏行列」口径不变（Qt 原文同：只有
 *           selectedItems 才过滤 isIndexHidden）。
 *  @param self     目标表格（可空）。
 *  @param out      输出数组（可空=只计数）。
 *  @param maxCount 输出容量。
 *  @return 选区总数。
 */
static int xtw_collectRanges(const XTableWidget* self,
                             XTableWidgetSelectionRange* out, int maxCount)
{
    XItemSelectionModel* sm;
    XItemSelection view;
    int i;
    int n;
    if (!self) return 0;
    sm = self->m_base.m_base.m_selectionModel;
    if (!sm) return 0;
    (void)XItemSelection_init(&view);
    n = XItemSelectionModel_selection(sm, &view);
    for (i = 0; i < n; ++i) {
        XItemSelectionRange r = XItemSelection_at(&view, i);
        if (out && i < maxCount)
            out[i] = XTableWidgetSelectionRange_create(
                XItemSelectionRange_top(&r), XItemSelectionRange_left(&r),
                XItemSelectionRange_bottom(&r),
                XItemSelectionRange_right(&r));
    }
    XItemSelection_deinit(&view);
    return n;
}

int XTableWidget_selectedRanges(const XTableWidget* self,
                                XTableWidgetSelectionRange* out, int maxCount)
{
    return xtw_collectRanges(self, out, maxCount);
}

int XTableWidget_indexFromItem(const XTableWidget* self,
                               const XTableWidgetItem* item, int* column)
{
    int row;
    int col;
    if (column) *column = -1;
    if (!self || !item || !self->m_cells) return -1;
    /* 单元格条目按 (row,column) 独立存放：逐格地址比对定位。 */
    for (row = 0; row < self->m_rows; ++row) {
        if (!self->m_cells[row]) continue;
        for (col = 0; col < self->m_columns; ++col) {
            if (self->m_cells[row][col] == item) {
                if (column) *column = col;
                return row;
            }
        }
    }
    return -1;
}

int XTableWidget_column(const XTableWidget* self,
                        const XTableWidgetItem* item)
{
    int col;
    /* 对齐 Qt column(item) = index(item).column()。 */
    if (!self || !item) return -1;
    XTableWidget_indexFromItem(self, item, &col);
    return col;
}

int XTableWidget_row(const XTableWidget* self,
                     const XTableWidgetItem* item)
{
    /* 对齐 Qt row(item) = index(item).row()。 */
    if (!self || !item) return -1;
    return XTableWidget_indexFromItem(self, item, NULL);
}

XTableWidgetItem* XTableWidget_itemFromIndex(const XTableWidget* self,
                                             int row, int column)
{
    return xtw_cell(self, row, column);
}

static int xtw_indexOf(const XTableWidget* self, const XTableWidgetItem* item,
                       int* outColumn)
{
    return XTableWidget_indexFromItem(self, item, outColumn);
}

void XTableWidget_editItem(XTableWidget* self, const XTableWidgetItem* item)
{
    int row;
    int col;
    if (!self || !item) return;
    row = xtw_indexOf(self, item, &col);
    if (row < 0 || col < 0) return;
    (void)XAbstractItemView_edit(&self->m_base.m_base, row, col);
}

void XTableWidget_openPersistentEditor(XTableWidget* self,
                                       const XTableWidgetItem* item)
{
    int row;
    int col;
    if (!self || !item) return;
    row = xtw_indexOf(self, item, &col);
    if (row < 0 || col < 0) return;
    XAbstractItemView_openPersistentEditor(&self->m_base.m_base, row, col);
}

void XTableWidget_closePersistentEditor(XTableWidget* self,
                                        const XTableWidgetItem* item)
{
    int row;
    int col;
    if (!self || !item) return;
    row = xtw_indexOf(self, item, &col);
    if (row < 0 || col < 0) return;
    XAbstractItemView_closePersistentEditor(&self->m_base.m_base, row, col);
}

bool XTableWidget_isPersistentEditorOpen(const XTableWidget* self,
                                         const XTableWidgetItem* item)
{
    int row;
    int col;
    if (!self || !item) return false;
    row = xtw_indexOf(self, item, &col);
    if (row < 0 || col < 0) return false;
    return XAbstractItemView_isPersistentEditorOpen(&self->m_base.m_base,
                                                    row, col);
}

/* ==================== 视觉序与几何 ==================== */

int XTableWidget_visualRow(const XTableWidget* self, int logicalRow)
{
    /* 对齐 Qt：verticalHeader()->visualIndex(logicalRow)。 */
    if (!self || logicalRow < 0) return -1;
    if (self->m_base.m_verticalHeader)
        return XHeaderView_visualIndex(self->m_base.m_verticalHeader,
                                       logicalRow);
    return logicalRow;
}

int XTableWidget_visualColumn(const XTableWidget* self, int logicalColumn)
{
    /* 对齐 Qt：horizontalHeader()->visualIndex(logicalColumn)。 */
    if (!self || logicalColumn < 0) return -1;
    if (self->m_base.m_horizontalHeader)
        return XHeaderView_visualIndex(self->m_base.m_horizontalHeader,
                                       logicalColumn);
    return logicalColumn;
}

XRect XTableWidget_visualItemRect(const XTableWidget* self,
                                  const XTableWidgetItem* item)
{
    XRect rect;
    int column = -1;
    int row;
    XRect_init(&rect, 0, 0, 0, 0);
    if (!self || !item) return rect;
    row = XTableWidget_indexFromItem(self, item, &column);
    if (row < 0 || column < 0) return rect;
    /* 对齐 Qt visualItemRect → QTableView::visualRect：合并区间原点给整块。
     * [K-68 残留 b 2026-11-14] 改走**本类** visualRect 虚槽（_base 契约唯一
     * 公开入口 XAbstractItemView_visualRect_base → VX_tableWidget_visualRect）：
     * 此前直调基类裸视图实现 XTableView_visualRect——裸视图无行号列偏移、
     * 表头带高用 XTV_HEADER_H(20) 而本表是 24、列几何按**逻辑序**累加
     * ⇒ 拖拽换位后本函数返回逻辑序位置（挂件定位/单格失效/滚动对位全错位），
     * 且首行挂件 y=20 < m_headerHeight(24) 被 xtw_cellWidgetPlace 判「顶入
     * 表头带」永藏。本类虚槽内部走 xtw_colX/xtw_rowY（视觉序 + 行号列/
     * 表头带偏移），与绘制/命中同源同参；隐藏行/列返回空矩形（Qt
     * isIndexHidden 口径，hiddenGeometry 锁同款）。 */
    if (!XAbstractItemView_visualRect_base(&self->m_base.m_base, row, column,
                                           &rect))
        XRect_init(&rect, 0, 0, 0, 0);
    return rect;
}

/* ==================== 信号 ==================== */

void* XTableWidget_cellClicked_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellClicked_signal; }
void* XTableWidget_cellDoubleClicked_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellDoubleClicked_signal; }
void* XTableWidget_currentCellChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_currentCellChanged_signal; }
void* XTableWidget_itemChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemChanged_signal; }
void* XTableWidget_itemClicked_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemClicked_signal; }
void* XTableWidget_itemDoubleClicked_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemDoubleClicked_signal; }
void* XTableWidget_itemPressed_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemPressed_signal; }
void* XTableWidget_itemEntered_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemEntered_signal; }
void* XTableWidget_itemActivated_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemActivated_signal; }
void* XTableWidget_cellPressed_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellPressed_signal; }
void* XTableWidget_cellEntered_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellEntered_signal; }
void* XTableWidget_cellActivated_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellActivated_signal; }
void* XTableWidget_cellChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellChanged_signal; }
void* XTableWidget_currentItemChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_currentItemChanged_signal; }
void* XTableWidget_itemSelectionChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemSelectionChanged_signal; }


/* ==================== 渲染与交互 ==================== */

/** [K-68 2026-11-14] 逻辑列 → 视觉序：表格体（几何/命中/绘制/单格失效）
 *  唯一映射入口。
 *  @details 对标 Qt：QTableView 的列几何/命中/绘制全部经表头
 *           visualIndex ↔ logicalIndex 换算——sectionPosition(logical) =
 *           headerSectionPosition(visualIndex(logical))（qheaderview.cpp:
 *           625-633），paintEvent 外层按视觉序遍历后 logicalIndex 换算
 *           （qtableview.cpp:1600,1610-1611），命中 logicalIndexAt =
 *           visualIndexAt + logicalIndex（qheaderview.cpp:585-591）。
 *           映射事实源 = 水平表头 m_visualToLogical（K-68 表头层
 *           moveSection 真改映射；尺寸/隐藏仍按逻辑段存放），经
 *           XHeaderView_visualIndex 公开接口取。表头缺席恒等；段数失步
 *           （结构变化瞬间 visualIndex 返回 -1）回落恒等映射，保证渲染/
 *           命中不因瞬时失步断链。未拖拽时映射未建 ⇒ 表头接口本身恒等
 *           返回，与旧逻辑序口径逐位一致。 */
static int xtw_logicalToVisual(const XTableWidget* self, int col)
{
    int v;
    if (!self || col < 0) return -1;
    if (!self->m_base.m_horizontalHeader) return col;
    v = XHeaderView_visualIndex(self->m_base.m_horizontalHeader, col);
    return v >= 0 ? v : col;
}

/** [K-68 2026-11-14] 视觉序 → 逻辑列（xtw_logicalToVisual 的反查；
 *  回落口径同上）。 */
static int xtw_visualToLogical(const XTableWidget* self, int visual)
{
    int col;
    if (!self || visual < 0) return -1;
    if (!self->m_base.m_horizontalHeader) return visual;
    col = XHeaderView_logicalIndex(self->m_base.m_horizontalHeader, visual);
    return col >= 0 ? col : visual;
}

/** @brief 计算列 x 起点（含水平滚动偏移；隐藏列占宽 0）。
 *  @details [K-68 2026-11-14] 几何按**视觉序**累加：参数 col 是**逻辑
 *           列**，返回其**视觉序位置**的 x 起点 = 行号列宽 + Σ（视觉序
 *           在本列之前的逻辑列宽）——对标 Qt
 *           QHeaderView::sectionViewportPosition/sectionPosition 按
 *           visualIndex 定位、recalcSectionStartPos 按视觉序累加
 *           （qheaderview.cpp:625-633/3890-3898）。列宽事实源仍是
 *           m_colWidths（按逻辑索引，xtw_syncHeaders 镜像到表头段），
 *           视觉序只决定排列；隐藏列 0 宽跳过（既有口径）。未拖拽时
 *           映射恒等 ⇒ 与旧逻辑序累加逐位一致；col 越界按恒等口径全
 *           累加（与改前一致）。 */
static int xtw_colX(const XTableWidget* self, int col)
{
    int v;
    int x = xtw_vHeaderW(self);
    int selfVisual;
    if (!self || col < 0) return x;
    selfVisual = xtw_logicalToVisual(self, col);
    for (v = 0; v < self->m_columns && v < selfVisual; ++v) {
        int i = xtw_visualToLogical(self, v);
        if (i < 0 || i >= self->m_columns) continue;
        if (xtw_colHidden(self, i)) continue;
        x += self->m_base.m_colWidths[i];
    }
    return x;
}

/** @brief 行 y 起点（内容坐标，不含滚动偏移/表头高度；隐藏行占高 0）。 */
static int xtw_rowY(const XTableWidget* self, int row)
{
    int i;
    int y = 0;
    int rh = xtw_rowHeight(self);
    for (i = 0; i < row; ++i) {
        if (xtw_rowHidden(self, i)) continue;
        y += rh;
    }
    return y;
}

/** @brief 取滚动偏移（垂直/水平条值）。 */
static int xtw_vOffset(const XTableWidget* self)
{
    XScrollBar* bar = XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)self);
    return bar ? XAbstractSlider_value((XAbstractSlider*)bar) : 0;
}

static int xtw_hOffset(const XTableWidget* self)
{
    XScrollBar* bar = XAbstractScrollArea_horizontalScrollBar(
        (XAbstractScrollArea*)self);
    return bar ? XAbstractSlider_value((XAbstractSlider*)bar) : 0;
}

/** @brief 按当前行列尺寸同步滚动范围（隐藏行列 0 尺寸、不占内容尺寸）。 */
static void xtw_updateContentSize(XTableWidget* self)
{
    int w;
    int h;
    int i;
    int rh;
    if (!self) return;
    rh = xtw_rowHeight(self);
    h = self->m_headerHeight;
    for (i = 0; i < self->m_rows; ++i) {
        if (xtw_rowHidden(self, i)) continue;
        h += rh;
    }
    w = xtw_vHeaderW(self);
    for (i = 0; i < self->m_columns; ++i) {
        if (xtw_colHidden(self, i)) continue;
        w += self->m_base.m_colWidths[i];
    }
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)self, w, h);
    /* [K-73] 内容尺寸落地（滚动条显隐联立两遍法同步收敛）后视口宽已
     * 定 ⇒ 重推列布局可用长：段数/列宽/隐藏变化都经本函数，推送随
     * 事实源一并收敛，不另在六个调用点重复挂钩。 */
    xtw_pushStretchViewport(self);
}

/* ==================== role 叠加渲染消费（对齐 QStyledItemDelegate::paint） ==================== */

/**
 * @brief 绘制简笔勾选框（CheckStateRole 渲染消费；对齐 Qt
 *        PE_IndicatorCheckBox 的简化笔画：外框 + 选中勾线/半选中横线）。
 * @param painter 绘制器。
 * @param cellX   单元格左缘 x。
 * @param cellY   单元格上缘 y。
 * @param cellW   单元格宽（未用；绘制宽度由 XTW_ROLE_CHECK_BOX 决定）。
 * @param cellH   单元格高（勾选框垂直居中）。
 * @param checkState XItemCheckState（0/1/2）。
 * @return 勾选框占用的内容宽度（框 + 右侧间距；供文本区推进）。
 */
static int xtw_drawCheckIndicator(XPainter* painter, int cellX, int cellY,
                                  int cellW, int cellH, int checkState)
{
    int box = XTW_ROLE_CHECK_BOX;
    int x = cellX + 2;
    int y = cellY + (cellH - box) / 2;
    XRect frame;
    if (!painter) return 0;
    (void)cellW;
    XRect_init(&frame, x, y, box, box);
    XPainter_setPen(painter, 0xFF666666u);
    XPainter_drawRect(painter, &frame);
    if (checkState == XItemCheckState_Checked) {
        /* 选中：框内两段折线勾。 */
        XPainter_setPen(painter, 0xFF207F20u);
        XPainter_drawLine(painter, x + 2, y + box / 2,
                          x + box / 2, y + box - 3);
        XPainter_drawLine(painter, x + box / 2, y + box - 3,
                          x + box - 2, y + 2);
    } else if (checkState == XItemCheckState_PartiallyChecked) {
        /* 半选：框内中横线。 */
        XPainter_setPen(painter, 0xFF207F20u);
        XPainter_drawLine(painter, x + 2, y + box / 2,
                          x + box - 2, y + box / 2);
    }
    return box + XTW_ROLE_CONTENT_GAP;
}

/**
 * @brief 绘制 DecorationRole 图像（渲染消费；对齐 QStyledItemDelegate
 *        的 decoration 子集——原尺寸左置、随行高垂直居中）。
 * @param painter    绘制器。
 * @param cellX      单元格左缘 x。
 * @param cellY      单元格上缘 y。
 * @param cellW      单元格宽。
 * @param cellH      单元格高。
 * @param decoration XIcon 借用指针（XTableWidgetItem_icon 返回值）；可为 NULL。
 * @return 占用宽度（图像宽 + 右侧间距；未设置/越界/超出行高返回 0）。
 */
static int xtw_drawDecoration(XPainter* painter, int cellX, int cellY,
                              int cellW, int cellH, const void* decoration)
{
    const XIcon* icon = (const XIcon*)decoration;
    XPixmap pixmap;
    XImage image;
    int w;
    int h;
    if (!painter || !icon || XIcon_isNull(icon)) return 0;
    /* XIcon → XPixmap → XImage：取实际尺寸（0 表示用图标原生尺寸）。 */
    XPixmap_init(&pixmap);
    XIcon_pixmap(icon, 0, 0, XIconMode_Normal, XIconState_Off, &pixmap);
    XImage_init(&image);
    XPixmap_toImage(&pixmap, &image);
    w = XImage_width(&image);
    h = XImage_height(&image);
    /* 超出单元格的装饰不绘制（避免越格覆盖）。 */
    if (w <= 0 || h <= 0 || h > cellH || w > cellW) {
        XClassDeinit((XClass*)&image);
        XClassDeinit((XClass*)&pixmap);
        return 0;
    }
    XPainter_drawImage(painter, &image, cellX + 2, cellY + (cellH - h) / 2);
    XClassDeinit((XClass*)&image);
    XClassDeinit((XClass*)&pixmap);
    return w + XTW_ROLE_CONTENT_GAP;
}

/** @brief 绘制：表头 → 网格 → 单元格文本 → 选中高亮 → 当前框。 */
static void VX_tableWidget_paintEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect dirty;
    XRect dataClip;
    uint32_t base;
    uint32_t dark;
    uint32_t highlight;
    uint32_t highlightedText;
    uint32_t windowText;
    uint32_t button;
    int vo;
    int ho;
    int w;
    int h;
    int row;
    int col;
    int v;
    int rh;
    if (!tw || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    if (w <= 2 || h <= 2) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    base  = xtw_color(tw, XPaletteColorRole_Base);
    dark  = xtw_color(tw, XPaletteColorRole_Dark);
    highlight = xtw_color(tw, XPaletteColorRole_Highlight);
    highlightedText = xtw_color(tw, XPaletteColorRole_HighlightedText);
    windowText = xtw_color(tw, XPaletteColorRole_WindowText);
    button = xtw_color(tw, XPaletteColorRole_Button);
    vo = xtw_vOffset(tw);
    ho = xtw_hOffset(tw);
    /* 统一行高取本视图字段（不取 XTableView_rowHeight(0)：row 0 被隐藏时
     * 基类返回 0，会把整表绘制压成 0 高；隐藏行由下方循环整行跳过）。 */
    rh = xtw_rowHeight(tw);
    /* 脏区裁剪（对齐 Qt drawWidget 的 systemClip）：非 PAINT 入口
       （repaint/grab）按整控件处理。稳态小脏区刷新不再全表重绘。 */
    if (XEvent_type(event) == XEVENT_TYPE_PAINT)
        dirty = XPaintEvent_rect((const XPaintEvent*)event);
    else
        XRect_init(&dirty, 0, 0, w, h);
#if XPAINTER_CLIP_ON
    XPainter_setClipRect(&painter, &dirty,
                         XPainterClipOperation_ReplaceClip);
#endif /* XPAINTER_CLIP_ON */
    /* 1) 底色（裁剪已把填充限制在脏区内）。 */
    XPainter_fillRect(&painter, &dirty, base);
    /* 2) 水平表头：段带（角按钮+列段）止于末段右缘；其外的超长区域
       保持 1) 的底色、不铺任何表头色也不画线——空区若铺 Qt 字面的
       CE_HeaderEmptyArea=window 色，默认调色板下 window=button 同为
       #efefef，空区仍读作"无标题的灰第三列"（用户裁定不收）。 */
    {
        int bandEnd = xtw_vHeaderW(tw); /* 角按钮区恒属段带。 */
        int lastVis = -1;
        /* [K-68 2026-11-14] 末段按**视觉序**从尾反查（对标 Qt
         * QHeaderView::paintEvent 段带止于视觉末段右缘、其外铺
         * CE_HeaderEmptyArea）；未拖拽时视觉序==逻辑序，结果与改前一致。 */
        for (v = tw->m_columns - 1; v >= 0; --v) {
            int i = xtw_visualToLogical(tw, v);
            if (i >= 0 && i < tw->m_columns && !xtw_colHidden(tw, i)) {
                lastVis = i;
                break;
            }
        }
        /* 隐藏列不占段带宽度（对标 Qt：hideSection 后该段 0 宽且不绘制）。 */
        if (lastVis >= 0)
            bandEnd = xtw_colX(tw, lastVis)
                + tw->m_base.m_colWidths[lastVis] - ho;
        if (bandEnd < xtw_vHeaderW(tw)) bandEnd = xtw_vHeaderW(tw);
        if (bandEnd > w) bandEnd = w;
        XPainter_fillRect(&painter,
            &(XRect){0, 0, bandEnd, tw->m_headerHeight}, button);
        /* 末段右侧空区铺 window 色（对齐 Qt 6.8 QHeaderView::paintEvent
         * CE_HeaderEmptyArea = palette.window()）。 */
        if (bandEnd < w)
            XPainter_fillRect(&painter,
                &(XRect){bandEnd, 0, w - bandEnd, tw->m_headerHeight},
                xtw_color(tw, XPaletteColorRole_Window));
        XPainter_setPen(&painter, dark);
        XPainter_drawLine(&painter, 0, tw->m_headerHeight - 1,
                          w - 1, tw->m_headerHeight - 1);
    }
    if (dirty.y < tw->m_headerHeight) {
    /* [K-68 2026-11-14] 段带按**视觉序**遍历（对标 Qt QHeaderView::
     * paintEvent：外层视觉序 i、logicalIndex 换算取段语义、几何
     * sectionViewportPosition 按视觉序累加）。首/末可见段按视觉序判定
     * （Qt initStyleOptionForIndex 走 isFirstVisibleSection/isLastVisible
     * Section(visual)，qheaderview.cpp:3448-3462——size>0 且起/止==0/
     * length），隐藏段不参与 ::first/::last；预计算成**逻辑号**，
     * 循环内按逻辑列比对。 */
    {
        int firstVis = -1;
        int lastVis = -1;
        for (v = 0; v < tw->m_columns; ++v) {
            int i = xtw_visualToLogical(tw, v);
            if (i < 0 || i >= tw->m_columns || xtw_colHidden(tw, i)) continue;
            if (firstVis < 0) firstVis = i;
            lastVis = i;
        }
        for (v = 0; v < tw->m_columns; ++v) {
        int cx;
        int cw;
        XStyle* style = NULL;
        XString* label;
        col = xtw_visualToLogical(tw, v);
        if (col < 0 || col >= tw->m_columns) continue;
        if (xtw_colHidden(tw, col)) continue;  /* 隐藏列不绘制段带（gap 160） */
        cx = xtw_colX(tw, col) - ho;
        cw = tw->m_base.m_colWidths[col];
        if (cx + cw < 0 || cx > w) continue;
#if XSTYLE_ON
        style = XWidget_style((const XWidget*)self);
#endif
        label = XTableWidget_horizontalHeaderItem(tw, col);
        if (style != NULL) {
            /* Fusion/公共风格接管：列头走 CE_HeaderSection/HeaderLabel。
             *
             * [QSS ::section 2026-11-07] 改用 XStyleOptionHeader 承载段语义：
             * `m_optionType == XStyleOptionType_Header` 让 QSS 代理
             * （XStyleSheetStyle）认出这是表头段并按 `::section` 伪元素
             * 查规则，`m_position` 决定 `:first/:middle/:last/:only-one`
             * ——对齐 Qt QHeaderView::initStyleOptionForIndex 的
             * position 派生（qheaderview.cpp:2922-2934）与
             * QStyleSheetStyle 的 PseudoElement_HeaderViewSection 查询
             * （qstylesheetstyle.cpp:4103-4119）。
             *
             * position 只按**正在绘制**的段判定：视觉序首/末可见段为
             * Beginning/End、唯一可见段为 OnlyOneSection——对齐 Qt
             * isFirstVisibleSection/isLastVisibleSection 的视觉序口径
             * （[K-68 2026-11-14] 拖拽换位后跟随视觉首末；本库表头无
             * 隐藏段概念，隐藏列由列宽 0 表达，见 xtw_colX）。
             *
             * 段语义只影响 QSS 的 :first/:last 选择，**不影响列宽与
             * 点选排序**：列宽的事实源仍是 m_base.m_colWidths（Fixed
             * 模式由 XHeaderView 承载），排序仍走 XTableWidget 的
             * sectionClicked 路径（下方命中测试与
             * XTableView_horizontalHeader 的 sortIndicator）。 */
            XStyleOptionHeader hs;
            XHeaderView* hh;
            XStyleOptionHeader_init(&hs, XStyleCE_HeaderSection);
            XRect_init(&hs.m_base.m_rect, cx, 0, cw, tw->m_headerHeight);
            hs.m_base.m_state = XWidget_isEnabled((XWidget*)tw)
                ? XStyleState_Enabled : 0;
            hs.m_base.m_state |= XStyleState_Horizontal;
            /* [K-69 highlightSections 2026-11-14] 对标 Qt
             * initStyleOptionForIndex 的选中段 State 位（qheaderview.cpp:
             * 2868-2878）：clickable 开启时，按压段置 Sunken；否则
             * highlightSections 开启时按选择模型补位——列内**相交**即
             * State_On、**整列选中**再补 State_Sunken。选择模型经视图的
             * selectionModel() 取（无则跳过），不改样式侧。 */
            hh = XTableView_horizontalHeader(&tw->m_base);
            if (hh && XHeaderView_sectionsClickable(hh)) {
                XItemSelectionModel* sm =
                    XAbstractItemView_selectionModel(&tw->m_base.m_base);
                if (col == XHeaderView_pressedSection(hh)) {
                    hs.m_base.m_state |= XStyleState_Sunken;
                } else if (XHeaderView_highlightSections(hh) && sm) {
                    if (XItemSelectionModel_columnIntersectsSelection(
                            sm, col))
                        hs.m_base.m_state |= XStyleState_On;
                    if (XItemSelectionModel_isColumnSelected(sm, col))
                        hs.m_base.m_state |= XStyleState_Sunken;
                }
            }
            hs.m_base.m_text = label ? XString_toUtf8(label) : "";
#if XPALETTE_ON
            hs.m_base.m_palette = XWidget_palette((XWidget*)tw);
#endif
            hs.m_section = col;
            hs.m_text = hs.m_base.m_text;
            /* [与单元格路径同源 2026-11-10] 表头省略模式取**视图的**
             * textElideMode——与单元格省略（XAbstractItemView_elidedText
             * 消费的同一个字段）同一来源，表头与单元格永远同一套省略口径
             * （Qt 6.8 同款：QHeaderView 的表头文本与视图项文本共用
             * textElideMode）。当前全库无人调
             * XAbstractItemView_setTextElideMode ⇒ 实际恒为缺省
             * ElideRight（**本库**视图缺省；Qt 的 QHeaderView 表头缺省是
             * ElideNone 即裁剪，qheaderview.cpp:314——本库统一按省略号
             * 收口，表头与单元格同口径）。 */
            hs.m_textElideMode = (XAbstractItemViewTextElideMode)
                XAbstractItemView_textElideMode(&tw->m_base.m_base);
            /* 水平表头缺省居中（Qt QHeaderViewPrivate::setDefaultValues，
             * qheaderview_p.h:152）。 */
            hs.m_textAlignment = XPAINTER_TEXT_ALIGN_HCENTER |
                                 XPAINTER_TEXT_ALIGN_VCENTER;
            hs.m_orientation = 0; /* Qt::Horizontal。 */
            /* 段位置按**可见段**首末判定（对齐 Qt isFirstVisibleSection/
             * isLastVisibleSection 的视觉序口径，qheaderview.cpp:3448-3462；
             * 隐藏列不参与 ::first/::last 语义）。首/末可见段已按视觉序
             * 预计算（逻辑号），拖拽换位后 Beginning/End 跟随视觉首末。 */
            if (firstVis == lastVis)
                hs.m_position =
                    XStyleOptionHeaderSectionPosition_OnlyOneSection;
            else if (col == firstVis)
                hs.m_position =
                    XStyleOptionHeaderSectionPosition_Beginning;
            else if (col == lastVis)
                hs.m_position = XStyleOptionHeaderSectionPosition_End;
            else
                hs.m_position = XStyleOptionHeaderSectionPosition_Middle;
            XStyle_drawControl_base(style, XStyleCE_HeaderSection,
                                    (const XStyleOption*)&hs,
                                    &painter, (XWidget*)tw);
            /* CE_HeaderLabel：QSS 代理命中 ::section 时已在一趟内画完
             * 标签（含 font-weight/padding，对齐 Qt 两处共用同一
             * subRule），未命中则回底层居中文本，见
             * VXStyleSheetStyle_drawControl 的表头段分支。 */
            hs.m_base.m_type = XStyleCE_HeaderLabel;
            XStyle_drawControl_base(style, XStyleCE_HeaderLabel,
                                    (const XStyleOption*)&hs,
                                    &painter, (XWidget*)tw);
            if (label) XClassDelete((XClass*)label);
            continue;
        }
        XPainter_drawText(&painter, cx + 4, tw->m_headerHeight - 8,
                          label ? XString_toUtf8(label) : "", windowText);
        if (label) XClassDelete((XClass*)label);
        XPainter_drawLine(&painter, cx + cw - 1, 0,
                          cx + cw - 1, tw->m_headerHeight - 1);
        }
    } /* [K-68] 首末可见段预计算块收口 */
    } /* 脏区不在表头带时整段列头跳过 */
    /* 3) 数据区：裁剪到表头带之下——滚出视口顶的行（部分行）只画
       出表头以下部分，不得覆盖表头文字（对齐 Qt 表头/内容分域）。 */
    XRect_init(&dataClip, 0, tw->m_headerHeight, w,
               h - tw->m_headerHeight);
#if XPAINTER_CLIP_ON
    if (dataClip.height > 0)
        XPainter_setClipRect(&painter, &dataClip,
                             XPainterClipOperation_IntersectClip);
#endif /* XPAINTER_CLIP_ON */
    if (dataClip.height > 0) {
    {
        /* 可视行范围按脏区 y 计算：小脏区刷新不再遍历全部行。
         * 有隐藏行时必须从 0 起按可见行累加 y（隐藏行 0 高、整行跳过），
         * 无隐藏行保持原来的算术快路径（与 2026-11-08 的重绘优化同口径）。 */
        int anyHiddenRow = 0;
        int visRows = 0;
        int first;
        int last;
        int cy;
        int i;
        for (i = 0; i < tw->m_rows; ++i) {
            if (xtw_rowHidden(tw, i)) { anyHiddenRow = 1; continue; }
            ++visRows;
        }
        if (!anyHiddenRow) {
            first = (dirty.y + vo - tw->m_headerHeight) /
                    (rh > 0 ? rh : 1);
            last = (dirty.y + dirty.height + vo - tw->m_headerHeight) /
                   (rh > 0 ? rh : 1);
            if (first < 0) first = 0;
            if (last > tw->m_rows - 1) last = tw->m_rows - 1;
            row = first;
            cy = tw->m_headerHeight + row * rh - vo;
        } else {
            row = 0;
            cy = tw->m_headerHeight - vo;
            last = tw->m_rows - 1;
        }
        for (; row <= last && row < tw->m_rows; ++row) {
        if (xtw_rowHidden(tw, row)) continue;  /* 隐藏行不绘制也不占高（gap 160） */
        if (cy + rh < tw->m_headerHeight) { cy += rh; continue; }
        if (cy > h) break;
        /* [K-68 2026-11-14] 数据格按**视觉序**遍历（对标 Qt
         * QTableView::paintEvent 的 visualColumnIndex 外层 + logicalIndex
         * 换算 + columnViewportPosition 取格几何，qtableview.cpp:1600,
         * 1610-1611）；换算后的 col 是逻辑列，条目/选中/role 全按逻辑索引取，
         * 与命中（xtw_cellAt）、几何（xtw_colX）同一映射入口。 */
        for (v = 0; v < tw->m_columns; ++v) {
            XTableWidgetItem* cell;
            int cx;
            int cw;
            XRect cellRect;
            bool sel;
            XColor cellBg;
            XColor cellFg;
            bool hasBg;
            bool hasFg;
            col = xtw_visualToLogical(tw, v);
            if (col < 0 || col >= tw->m_columns) continue;
            if (xtw_colHidden(tw, col)) continue; /* 隐藏列 0 宽、不绘制 */
            cell = xtw_cell(tw, row, col);
            cx = xtw_colX(tw, col) - ho;
            cw = tw->m_base.m_colWidths[col];
            XRect_init(&cellRect, cx, cy, cw, rh);
            if (cellRect.x + cellRect.width < 0 || cellRect.x > w) continue;
            /* [§B4 复核修订 2026-11-15] 选中判定读**派生镜像位**而不是选择
             * 模型的线性查询：Qt 的 isSelected 走 QItemSelectionModel 的
             * 区间表（近似 O(1)），本库模型是平铺选中向量 ⇒ 逐格线性扫是
             * O(640×N)（探针：N=800 时一帧 3.87ms、N=32000 时 49.20ms，
             * 远超 AGENTS.md「帧耗时 0.0x~2ms」判据）。镜像位由
             * xtw_selMirror 在每次选区变化后单向、带同值闸地刷入，是模型的
             * 精确派生态（含空格：空格无位但也不绘制选中底，视觉等价）。
             * 「当前格」高亮口径保原样（既有视觉口径把当前格并入底色）。 */
            sel = (cell && cell->m_selected) ||
                (row == tw->m_base.m_base.m_currentRow &&
                 col == tw->m_base.m_base.m_currentColumn);
            if (sel) XPainter_fillRect(&painter, &cellRect, highlight);
            /* 对齐 Qt：条目 BackgroundRole 优先于选中高亮之上的单元格底色。 */
            hasBg = cell && XTableWidgetItem_background(cell, &cellBg);
            if (hasBg) XPainter_fillRect(&painter, &cellRect,
                                         XColor_rgba(&cellBg));
            if (cell) {
                const char* text = XTableWidgetItem_text_toUtf8(cell);
                uint32_t ink;
                char elideBuf[256];
                const XFont* roleFont = XTableWidgetItem_font(cell);
                int roleAlign = XTableWidgetItem_textAlignment(cell);
                XSize sizeHint;
                int contentX = cellRect.x;
                int availW = cw - 8;

                /* 绘制面查委托（§B5/P3-46）：每格解析生效委托，**仅当其真
                 * 覆写了 Paint 虚槽**时才构造选项并调用 paint_base；缺省委托
                 * 走在 paintOverridden 闸之前就短路（避免每格 952 字节选项
                 * 清零 + 712 字节调色板拷贝的纯白开销），视图既有 item role
                 * 自绘保持不变（像素零漂移）。自定义委托在此处叠加绘制——
                 * XTableWidget 的 item 级 bg/fg/勾选/装饰由视图自绘承担，全权
                 * 接管需先把 item role 桥接进 XStyleOptionViewItem（见
                 * openNotes）。 */
                {
                    XItemDelegate* twDelegate =
                        XAbstractItemView_resolveDelegate(&tw->m_base.m_base,
                                                          row, col);
                    if (twDelegate &&
                        XItemDelegate_paintOverridden(twDelegate)) {
                        XStyleOptionViewItem twOpt;
                        /* 第二参 = m_type（本次绘制针对的元素）；条目传
                         * XStyleCE_ItemViewItem，传 0 会落成 XStylePE_Frame。 */
                        XStyleOptionViewItem_init(&twOpt, XStyleCE_ItemViewItem);
                        twOpt.m_base.m_rect = cellRect;
                        twOpt.m_base.m_state =
                            (sel ? XStyleState_Selected : XStyleState_None) |
                            XStyleState_Enabled | XStyleState_Item;
                        twOpt.m_base.m_palette = XWidget_palette((XWidget*)tw);
                        /* m_font 属于 XStyleOptionViewItem 自身（不在 m_base）。 */
                        twOpt.m_font = roleFont;
                        twOpt.m_textElideMode =
                            XAbstractItemView_textElideMode(&tw->m_base.m_base);
                        twOpt.m_displayAlignment = (uint32_t)roleAlign;
                        XItemDelegate_paint_base(twDelegate, &painter, &twOpt,
                                                &tw->m_base.m_base, row, col);
                    }
                }

                /* [role 消费 2026-11-03] 对齐 QStyledItemDelegate::paint 的
                 * 条目布局：CheckStateRole 勾选框最左 → DecorationRole 图标
                 * 其次 → SizeHintRole 收窄文本区 → 剩余矩形承载文本
                 * （TextAlignmentRole 对齐、FontRole 字体）。
                 * 与 XTableView 的同名消费同口径（那边数据源自 view role
                 * 表，这边源自 XTableWidgetItem 的 role 存储）。 */
                hasFg = XTableWidgetItem_foreground(cell, &cellFg);
                if (sel) ink = highlightedText;
                else if (hasFg) ink = XColor_rgba(&cellFg);
                else ink = windowText;

                /* CheckStateRole：仅当条目**显式设置**了该 role 才画框
                 * （未设置时 checkState() 返回 Unchecked，不能据此画）。 */
                if (XTableWidgetItem_data(cell,
                                          XItemDataRole_CheckStateRole)) {
                    contentX += xtw_drawCheckIndicator(
                        &painter, cellRect.x, cellRect.y, cw, rh,
                        XTableWidgetItem_checkState(cell));
                }
                contentX += xtw_drawDecoration(&painter, cellRect.x,
                                               cellRect.y, cw, rh,
                                               XTableWidgetItem_icon(cell));
                if (contentX > cellRect.x) {
                    availW = cellRect.x + cw - 4 - contentX;
                    if (availW < 0) availW = 0;
                }
                /* SizeHintRole：条目声明的宽优先收窄文本可用宽。 */
                if (XTableWidgetItem_sizeHint(cell, &sizeHint) &&
                    sizeHint.width > 0 && sizeHint.width < availW)
                    availW = sizeHint.width;

                if (text && text[0]) {
                    if (roleFont) XPainter_setFont(&painter, roleFont);
                    if (roleAlign != 0) {
                        /* TextAlignmentRole：走矩形文本绘制（支持水平/垂直
                         * 对齐），缺省方位补左/底与历史基线一致。 */
                        XRect textRect;
                        uint32_t flags = (uint32_t)(roleAlign &
                            (XPAINTER_TEXT_ALIGN_HORIZONTAL_MASK |
                             XPAINTER_TEXT_ALIGN_VERTICAL_MASK));
                        if (!(flags & XPAINTER_TEXT_ALIGN_HORIZONTAL_MASK))
                            flags |= XPAINTER_TEXT_ALIGN_LEFT;
                        if (!(flags & XPAINTER_TEXT_ALIGN_VERTICAL_MASK))
                            flags |= XPAINTER_TEXT_ALIGN_BOTTOM;
                        /* [省略消费] 绘制前按 textElideMode 省略。 */
                        text = XAbstractItemView_elidedText(
                            &tw->m_base.m_base, elideBuf,
                            (int)sizeof(elideBuf), text, availW,
                            XPainter_font(&painter));
                        XRect_init(&textRect, contentX, cellRect.y, availW, rh);
                        XPainter_drawTextRect(&painter, &textRect, flags, text,
                                              ink);
                    } else {
                        /* 历史快速路径（像素零漂移）：左缘 contentX+4、基线
                         * y+rh-8。无任何 role 时 contentX==cellRect.x，
                         * availW==cw-8，与改造前逐位一致。 */
                        text = XAbstractItemView_elidedText(
                            &tw->m_base.m_base, elideBuf,
                            (int)sizeof(elideBuf), text, availW,
                            XPainter_font(&painter));
                        XPainter_drawText(&painter, contentX + 4,
                                          cellRect.y + rh - 8, text, ink);
                    }
                    if (roleFont) XPainter_setFont(&painter, NULL);
                }
            }
            if (tw->m_base.m_gridVisible) {
                XPainter_setPen(&painter, dark);
                XPainter_drawLine(&painter, cellRect.x,
                                  cellRect.y + cellRect.height - 1,
                                  cellRect.x + cellRect.width - 1,
                                  cellRect.y + cellRect.height - 1);
                XPainter_drawLine(&painter, cellRect.x + cellRect.width - 1,
                                  cellRect.y, cellRect.x + cellRect.width - 1,
                                  cellRect.y + cellRect.height - 1);
            }
        }
        /* 垂直表头在单元格之后绘制：横向滚动时单元格平移不会盖住
           行号列（行号列钉在内容区左缘，对齐 Qt 表头子控件层级）。
           [隐藏时整段跳过] 对标 Qt verticalHeader()->setVisible(false)：
           行号列不画、也不占宽（单元格已左移贴边）。 */
        if (tw->m_vHeaderVisible) {
            XPainter_fillRect(&painter,
                &(XRect){0, cy, tw->m_headerWidth, rh}, button);
            {
                /* 对齐 Qt 6.8 QTableModel::headerData：无显式 header item 时
                 * Vertical+DisplayRole 返回 section+1（1 基行号）。 */
                XString* vlabel = XTableWidget_verticalHeaderItem(tw, row);
                XPainter_drawText(&painter, 4, cy + rh - 8,
                                  vlabel ? XString_toUtf8(vlabel) : "",
                                  windowText);
                if (vlabel) XClassDelete((XClass*)vlabel);
            }
            XPainter_drawLine(&painter, 0, cy + rh - 1,
                              tw->m_headerWidth - 1, cy + rh - 1);
        }
        cy += rh;  /* 行 y 按可见行累加（隐藏行 continue 跳过、不占高） */
    }
        /* ---- 空区收口（对齐 Qt 6.8）----
         * 1) QTableView::paintEvent：单元格与网格线均裁到末行/末列；
         * 2) QHeaderView 是独立子控件、整列高覆盖：垂直表头在末行之下
         *     仍是段带底色（button 族），空段无 headerData；
         * 3) 水平表头末段右侧空区 = CE_HeaderEmptyArea（上方已铺）。 */
        {
            int emptyTop = tw->m_headerHeight + visRows * rh - vo;
            if (emptyTop < tw->m_headerHeight)
                emptyTop = tw->m_headerHeight;
            if (emptyTop < h)
                XPainter_fillRect(&painter,
                    &(XRect){0, emptyTop, xtw_vHeaderW(tw),
                             h - emptyTop}, button);
        }
    }
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 滚动内容变化：重绘（网格随滚动条偏移平移）。 */
static void VX_tableWidget_scrollContentsBy(XAbstractScrollArea* self,
                                            int dx, int dy)
{
    (void)dx; (void)dy;
    if (!self) return;
    /* 滚动后重摆挂件（对齐 Qt updateGeometries 的 editor 联动）。 */
    xtw_cellWidgetRelayout((XTableWidget*)self);
    XWidget_update((XWidget*)self);
}

/** @brief 视口事件覆写（K-73a 2026-11-14）：滚动条显隐等布局收敛改变
 *         视口几何时重推列布局可用长。
 *  @details 对标 Qt：滚动区布局改变视口矩形 ⇒ 视口 Resize 事件；
 *           QTableView::updateGeometries 以**视口几何**为事实源摆表头
 *           （qtableview.cpp:2301,2309），表头几何变 ⇒ Resize ⇒
 *           resizeSections（qheaderview.cpp:2817-2827）。本表头带由
 *           表格自绘，视口 Resize 是同一事实的事件形态：滚动条出现/
 *           消失（AsNeeded 阈值跨越/策略切换）不带表格 resize、不带
 *           内容尺寸变化——既有四处推送（init/resizeEvent/行号列显隐/
 *           内容尺寸落地）都覆盖不到这条路径；视口事件经本区 init 装
 *           的过滤器（xasa_installViewportFilter）进
 *           EXAbstractScrollArea_ViewportEvent 虚槽。
 *           [K-73b 同构] 裸 XTableView 有同款覆写推**裸视口宽**——
 *           本覆写在派生类表上接管该槽（宿主可用长要扣行号列宽），
 *           不链父类推送。重推走 XHeaderView_syncViewportLength 同值闸
 *           （仅长度真变时重排）；其余视口事件一律不消费（false 照旧
 *           沿父链派发）。 */
static bool VX_tableWidget_viewportEvent(XAbstractScrollArea* self,
                                         XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    if (!tw) return false;
    if (event && XEvent_type(event) == XEVENT_TYPE_RESIZE)
        xtw_pushStretchViewport(tw);
    return false;
}

/** @brief resizeEvent：Stretch 模式下按新视口重分列宽。 */
static void VX_tableWidget_resizeEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    if (!tw) return;
    XClass_Parent(XTableView, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    if (tw->m_base.m_horizontalHeader &&
        XHeaderView_sectionResizeMode(tw->m_base.m_horizontalHeader)
            == (int)XHeaderViewResizeMode_Stretch)
        xtw_stretchColumnsToViewport(tw);
    /* [K-73] 视口宽变即推送可用长（对标 Qt 表头 Resize 事件的重排语义）。 */
    xtw_pushStretchViewport(tw);
}

/** @brief 坐标 → 单元格（返回行列；表头/越界返回 -1）。 */
static void xtw_cellAt(const XTableWidget* self, int x, int y,
                       int* row, int* col)
{
    int i;
    int v;
    int acc;
    int rh;
    *row = -1; *col = -1;
    if (!self || x < xtw_vHeaderW(self) || y < self->m_headerHeight) return;
    /* 命中与绘制同源：按**视觉序**遍历列段、命中返回**逻辑列**（对标
     * Qt logicalIndexAt = visualIndexAt + logicalIndex，qheaderview.cpp:
     * 585-591——拖拽换位后屏幕位置跟随视觉序，数据/信号仍按逻辑索引）。
     * 隐藏列宽 0（不可命中、不占位），隐藏行高 0。 */
    acc = xtw_vHeaderW(self) - xtw_hOffset(self);
    for (v = 0; v < self->m_columns; ++v) {
        int cw;
        i = xtw_visualToLogical(self, v);
        if (i < 0 || i >= self->m_columns) continue;
        if (xtw_colHidden(self, i)) continue;
        cw = self->m_base.m_colWidths[i];
        if (x >= acc && x < acc + cw) { *col = i; break; }
        acc += cw;
    }
    rh = xtw_rowHeight(self);
    acc = self->m_headerHeight - xtw_vOffset(self);
    for (i = 0; i < self->m_rows; ++i) {
        if (xtw_rowHidden(self, i)) continue;
        if (y >= acc && y < acc + rh) { *row = i; break; }
        acc += rh;
    }
}

/** @brief 按下：选中单元格 + cellClicked。 */
/** @brief 命中在**表头带**（y < headerHeight）⇒ 把事件换算成表头局部
 *  坐标转发给水平表头（真实点击路径：段按压/释放/双击的发射点都在表头
 *  自己的虚槽里；表头子件是隐藏的、事件先落在表格上）。
 *  @return 事件已被表头消费（调用方应 accept 并返回，不再走单元格逻辑）。
 *  [坐标换算] x 扣除行号列宽（列段从行号列右缘起），y 不变（带贴顶）。
 *  [实现位置] XHeaderView_forwardPress/Release/DoubleClick（框架层），
 *  内部直呼表头自身鼠标虚槽——与真实事件同一套语义，无第二套判定。 */
static bool xtw_forwardHeaderBand(XTableWidget* tw, XEvent* event)
{
    XHeaderView* hh;
    XMouseEvent local;
    XMouseEvent* me;
    XPoint pos;
    if (!tw || !event) return false;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    hh = XTableView_horizontalHeader(&tw->m_base);
    if (!hh) return false;
    /* [K-68 2026-11-14] 手势（调宽/拖段换位）进行中 ⇒ 带外的 move/release
     * 也要转发：对标 Qt 表头 grabMouse 后事件直达表头、与光标位置无关
     * （拖到数据区仍继续算目标、松手才收口；本表头是隐藏子件、
     * XWidget_grabMouse 对 !m_visible 直接返回，转发由表格承担）。 */
    if (pos.y >= tw->m_headerHeight) {               /* 不在表头带 */
        if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_RELEASE &&
            XHeaderView_gestureActive(hh)) {
            /* 释放落在带外：照常转发收口（换位提交/调宽落定）。 */
        } else if (XEvent_type(event) == XEVENT_TYPE_MOUSE_MOVE &&
                   XHeaderView_gestureActive(hh)) {
            /* 拖拽路过数据区：继续机算目标视觉序。 */
        } else {
            return false;
        }
    }
    local = *me;                                     /* 原事件为底版 */
    pos.x -= xtw_vHeaderW(tw);
    XMouseEvent_init(&local, XEvent_type(event),
                     XMouseEvent_button(me),
                     XMouseEvent_modifiers(me), pos);
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        XHeaderView_forwardPress(hh, (XEvent*)&local);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        XHeaderView_forwardRelease(hh, (XEvent*)&local);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
        XHeaderView_forwardDoubleClick(hh, (XEvent*)&local);
        break;
    case XEVENT_TYPE_MOUSE_MOVE:
        /* [K-68 2026-11-14] 移动段经保护入口转发（XHeaderView_
         * Protected.h）：悬停段发射/光标提示/拖拽目标机算全在表头
         * 自己的移动虚槽里，与真实事件同一套语义。 */
        XHeaderView_forwardMove(hh, (XEvent*)&local);
        break;
    default:
        return false;
    }
    return true;
}

static void VX_tableWidget_mousePressEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int row;
    int col;
    if (!tw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    /* [表头带转发 2026-11-10] 段按压/双击排序的发射点在表头虚槽里。 */
    if (xtw_forwardHeaderBand(tw, event)) {
        XEvent_accept(event);
        return;
    }
    /* 左键按压交付键盘焦点（对齐 Qt giveFocusAccordingToFocusPolicy；
     * 窗口型视图不抢——弹层焦点归组合框自身机制）。 */
    if (!self->m_isWindow) XWidget_setFocus(self);
    xtw_cellAt(tw, XMouseEvent_position(me).x, XMouseEvent_position(me).y,
               &row, &col);
    if (row >= 0 && col >= 0) {
        XTableWidgetItem* cell;
        XTableWidget_setCurrentCell(tw, row, col);
        cell = xtw_cell(tw, row, col);
        xtw_emitCellSignal(tw,
            (size_t)XTableWidget_cellPressed_signal, row, col);
        xtw_emitItemSignal(tw,
            (size_t)XTableWidget_itemPressed_signal, cell);
        xtw_emitItemSignal(tw,
            (size_t)XTableWidget_itemClicked_signal, cell);
        xtw_emitCellSignal(tw,
            (size_t)XTableWidget_cellClicked_signal, row, col);
    }
    XEvent_accept(event);
}

/** @brief 释放：表头带转发（指示器翻转/段点击收口在表头虚槽内）；
 *  单元格区暂无释放语义（与改前行为一致），接受即可。 */
static void VX_tableWidget_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    if (!tw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (XMouseEvent_button((XMouseEvent*)event) != XMouseButton_LeftButton)
        return;
    if (xtw_forwardHeaderBand(tw, event)) {
        XEvent_accept(event);
        return;
    }
    XEvent_accept(event);
}

/** @brief 移动：进入新单元格发射 cellEntered/itemEntered。 */
static void VX_tableWidget_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int row;
    int col;
    if (!tw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    /* [K-68/悬停 2026-11-14] 表头带内（或拖拽手势中）的移动先转发表头：
     * 悬停段发射/分隔线光标提示/段拖拽换位的目标机算都在表头虚槽里；
     * 带内不产生 cellEntered（cellAt 对 y<headerHeight 返回 -1，与转发
     * 前的单元格行为一致）。 */
    if (xtw_forwardHeaderBand(tw, event)) {
        XEvent_accept(event);
        return;
    }
    xtw_cellAt(tw, XMouseEvent_position(me).x, XMouseEvent_position(me).y,
               &row, &col);
    if (row >= 0 && col >= 0) {
        if (row != tw->m_enteredRow || col != tw->m_enteredColumn) {
            tw->m_enteredRow = (int16_t)row;
            tw->m_enteredColumn = (int16_t)col;
            xtw_emitCellSignal(tw,
                (size_t)XTableWidget_cellEntered_signal, row, col);
            xtw_emitItemSignal(tw,
                (size_t)XTableWidget_itemEntered_signal,
                xtw_cell(tw, row, col));
        }
    }
    XEvent_accept(event);
}

/** @brief 双击：先基类 doubleClicked 再 cellDoubleClicked，并补编辑触发位。 */
static void VX_tableWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int row;
    int col;
    if (!tw || !event || XEvent_type(event) !=
        XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    /* [表头带转发] 双击列标题 ⇒ sectionDoubleClicked（排序正源）。 */
    if (xtw_forwardHeaderBand(tw, event)) {
        XEvent_accept(event);
        return;
    }
    xtw_cellAt(tw, XMouseEvent_position(me).x, XMouseEvent_position(me).y,
               &row, &col);
    if (row >= 0 && col >= 0) {
        XTableWidgetItem* cell = xtw_cell(tw, row, col);
        /* 对齐 Qt mouseDoubleClickEvent：先基类 doubleClicked(row,col)
           再 QTableWidget 层 cellDoubleClicked/itemDoubleClicked。 */
        XAbstractItemView_doubleClicked_signal(&tw->m_base.m_base, row, col);
        /* DoubleClicked 触发位：本覆写不经基类事件路径，判定在此补齐
           （edit(row,col) 自带 NoEditTriggers/itemsEditable/单编辑器会话门禁）。 */
        if (tw->m_base.m_base.m_editTriggers &
                XAbstractItemViewEditTrigger_DoubleClicked)
            XAbstractItemView_edit(&tw->m_base.m_base, row, col);
        xtw_emitCellSignal(tw,
            (size_t)XTableWidget_cellDoubleClicked_signal, row, col);
        xtw_emitItemSignal(tw,
            (size_t)XTableWidget_itemDoubleClicked_signal, cell);
    }
    XEvent_accept(event);
}

/** @brief 条目几何（基类 edit()/滚动对位等内部路径的坐标来源）。
 *  对齐 QTableView::visualRect：表格局部坐标，含行号列宽（headerWidth）
 *  与表头高（headerHeight）偏移、实际列宽/行高、滚动偏移。
 *  [K-68 残留 b 2026-11-14] 隐藏行/列 0 宽口径修正：Qt
 *  `isIndexHidden(index)` ⇒ 返回空矩形（qtableview.cpp:2875-2877 的
 *  isIndexHidden 门），与基类 XTableView_visualRect 的隐藏判空同口径
 *  （hiddenGeometry 锁依据）。此前覆写不判隐藏——隐藏列读
 *  m_colWidths 原宽返回非空矩形、隐藏行 y 又不含隐藏行跳过，挂件
 *  定位（xtw_cellWidgetPlace/Relayout 经 visualItemRect 走本虚槽）
 *  会对隐藏格摆件。行 y 同步改为 xtw_rowY（隐藏行不占高的累计口径，
 *  与 paintEvent/xtw_updateContentSize 同源；无隐藏行时与旧
 *  row*m_rowHeight 逐位一致）。 */
static bool VX_tableWidget_visualRect(const XAbstractItemView* self,
                                      int row, int col, XRect* out)
{
    const XTableWidget* tw = (const XTableWidget*)self;
    XRect rect;
    if (!tw || !out || row < 0 || col < 0 ||
        row >= tw->m_rows || col >= tw->m_columns)
        return false;
    if (xtw_rowHidden(tw, row) || xtw_colHidden(tw, col)) return false;
    XRect_init(&rect,
               xtw_colX(tw, col) - xtw_hOffset(tw),
               tw->m_headerHeight + xtw_rowY(tw, row) - xtw_vOffset(tw),
               tw->m_base.m_colWidths[col],
               (int)tw->m_base.m_rowHeight);
    if (rect.width <= 0 || rect.height <= 0) return false;
    *out = rect;
    return true;
}

/** @brief 条目可编辑判定（对齐 Qt model flags(index) & ItemIsEditable）。
 *  @details 空格继承 Qt 的缺省 flags（含 ItemIsEditable）；有条目时读
 *           `XTableWidgetItem_flags` 的 ItemIsEditable 位；视图总闸
 *           `m_itemsEditable` 始终参与合取。 */
static bool VX_tableWidget_shouldEdit(const XAbstractItemView* self,
                                      int row, int col)
{
    const XTableWidget* tw = (const XTableWidget*)self;
    const XTableWidgetItem* cell;
    if (!tw || !tw->m_base.m_base.m_itemsEditable) return false;
    cell = xtw_cell(tw, row, col);
    if (!cell) {
        /* 对齐 Qt QTableModel::flags：空格返回含 ItemIsEditable 的缺省集合。 */
        return (XTW_DEFAULT_ITEM_FLAGS & XItemFlag_ItemIsEditable) != 0;
    }
    return (cell->m_itemFlags & XItemFlag_ItemIsEditable) != 0;
}

/** @brief 键盘：方向键移动当前单元格（对齐 QTableView 键盘导航）。 */
static void VX_tableWidget_keyPressEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XKeyEvent* ke = (XKeyEvent*)event;
    int row;
    int col;
    int key;
    if (!tw || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    key = XKeyEvent_key(ke);
    row = tw->m_base.m_base.m_currentRow;
    col = tw->m_base.m_base.m_currentColumn;
    /* 无当前格起步原点（对齐 QTableView 无当前索引时方向键移至首格）。 */
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= tw->m_rows) row = tw->m_rows - 1;
    if (col >= tw->m_columns) col = tw->m_columns - 1;
    switch (key) {
    case XKey_Up:    row = row > 0 ? row - 1 : 0; break;
    case XKey_Down:  row = row < tw->m_rows - 1 ? row + 1 : row; break;
    case XKey_Left:  col = col > 0 ? col - 1 : 0; break;
    case XKey_Right: col = col < tw->m_columns - 1 ? col + 1 : col; break;
    case XKey_Return:
    case XKey_Enter:
        /* 对齐 QAbstractItemView::keyPressEvent：Return/Enter 在有效
           当前格上触发 activated（无当前格或越界忽略）。 */
        row = tw->m_base.m_base.m_currentRow;
        col = tw->m_base.m_base.m_currentColumn;
        if (row >= 0 && col >= 0 && row < tw->m_rows &&
            col < tw->m_columns) {
            XTableWidgetItem* cell = xtw_cell(tw, row, col);
            xtw_emitItemSignal(tw,
                (size_t)XTableWidget_itemActivated_signal, cell);
            xtw_emitCellSignal(tw,
                (size_t)XTableWidget_cellActivated_signal, row, col);
        }
        XEvent_accept(event);
        return;
    default: return;
    }
    XTableWidget_setCurrentCell(tw, row, col);
    XEvent_accept(event);
}

/** @brief 调色板取色助手。 */
static uint32_t xtw_color(const XTableWidget* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self; (void)role;
    return 0xFF000000u;
#endif
}

#endif /* XTABLEWIDGET_ON */

#endif /* XGUI_ON */
