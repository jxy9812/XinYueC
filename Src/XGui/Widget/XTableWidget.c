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
#include "XEvent.h"
#include "XPainter.h"
#include "XWindowEvent.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XAlgorithm.h"
#include "XAtomic.h" /* XATOMIC_THREAD_LOCAL：DisplayRole 数值文本化的借用缓冲。 */

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
/** @brief Stretch 模式：按内容区宽均分列宽（实现见列宽模式节）。 */
static void xtw_stretchColumnsToViewport(XTableWidget* self);
/** @brief 调色板取色助手。 */
static uint32_t xtw_color(const XTableWidget* self, XPaletteColorRole role);
/** @brief 坐标 → 单元格（返回行列；表头/越界返回 -1）。 */
static void xtw_cellAt(const XTableWidget* self, int x, int y,
                       int* row, int* col);
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
/** @brief 释放一个条目（含其 role 数据）。 */
static void xtw_itemDelete(XTableWidgetItem* item);

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
{ return self ? (self->m_selected != 0) : false; }

void XTableWidgetItem_setSelected(XTableWidgetItem* self, bool select)
{
    if (!self) return;
    /* 对齐 Qt 6.8 QTableWidgetItem::setSelected（qtablewidget.cpp:1082-1111
     * 的 item 侧入口）：**条目必须先插入表格**（view 非空）才可选中；
     * 游离条目 setSelected 是空操作（Qt 的 item->view==nullptr 分支直接
     * return）。gap 296。表内选中态仍以 m_selected 位承载（本库的模型是
     * 条目矩阵；选中路径在 XTableWidget 内直接写位）。 */
    if (!self->m_view) return;
    if (self->m_selected == (select ? 1u : 0u)) return;
    self->m_selected = select ? 1 : 0;
    XWidget_update((XWidget*)self->m_view);
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
        for (c = oldCols; c >= column; --c)
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
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_tableWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_tableWidget_mouseMoveEvent);
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
        XAbstractItemView_setModel(&self->m_base.m_base, self->m_model);
    /* 对齐 Qt：model dataChanged→itemChanged/cellChanged 转发连接
       （qtablewidget.cpp:1631-1632）；槽首参=receiver=本表格。 */
    if (self->m_model)
        XObject_connect_1((XObject*)self->m_model,
                          (size_t)XAbstractItemModel_dataChanged_signal,
                          (XObject*)self, xtw_modelDataChangedSlot,
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
    self->m_rows = (int16_t)rows;
    if (self->m_base.m_base.m_currentRow >= rows)
        self->m_base.m_base.m_currentRow = rows - 1;
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
    self->m_columns = (int16_t)columns;
    if (self->m_base.m_base.m_currentColumn >= columns)
        self->m_base.m_base.m_currentColumn = columns - 1;
    xtw_syncHeaders(self);
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

int XTableWidget_columnCount(const XTableWidget* self)
{ return self ? self->m_columns : 0; }

/* ==================== 列宽模式辅助 ==================== */

/** @brief Stretch 模式：按内容区宽均分列宽（视口宽-行头宽）。
 *  @details 对齐 Qt QHeaderViewPrivate::resizeSections（Stretch 分支以
 *           viewport 宽为可用空间）：段尺寸写 header（事实源）并镜像到视图
 *           数组（本框架绘制/定位/内容宽读数组）。 */
static void xtw_stretchColumnsToViewport(XTableWidget* self)
{
    XHeaderView* hh;
    XWidget* viewport;
    int avail;
    int each;
    int i;
    int vis = 0;
    int seen = 0;
    if (!self || self->m_columns <= 0) return;
    hh = self->m_base.m_horizontalHeader;
    if (!hh) return;
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (!viewport) return;
    /* 隐藏列不参与拉伸分配（对标 Qt：Stretch 只在可见段间均分）。 */
    for (i = 0; i < self->m_columns; ++i) {
        if (!xtw_colHidden(self, i)) ++vis;
    }
    if (vis <= 0) return;
    avail = XWidget_width(viewport) - self->m_headerWidth;
    if (avail < vis) avail = vis; /* 每列至少 1px。 */
    each = avail / vis;
    if (each < 1) each = 1;
    for (i = 0; i < self->m_columns; ++i) {
        /* 尾列收余数（Qt Stretch 均分同样把零头留在末段）。 */
        int w;
        if (xtw_colHidden(self, i)) continue;
        ++seen;
        w = (seen == vis) ? (avail - each * (vis - 1)) : each;
        if (w < 1) w = 1;
        if (self->m_base.m_colWidths[i] != w) {
            int old = self->m_base.m_colWidths[i];
            XAbstractScrollArea* area = (XAbstractScrollArea*)self;
            self->m_base.m_colWidths[i] = w;
            XHeaderView_resizeSection(hh, i, w);
            /* 差量同步内容宽（同 XTableView_setColumnWidth 根修口径）。 */
            XAbstractScrollArea_setContentSize(
                area, area->m_contentWidth + (w - old),
                area->m_contentHeight);
        }
    }
    XWidget_update((XWidget*)self);
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
    xtw_cwOnRowInserted(self, row); /* 挂载表随行号平移。 */
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
    xtw_cwOnColumnInserted(self, column); /* 挂载表随列号平移。 */
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
    xtw_cwOnRowRemoved(self, row); /* 挂载表随行号平移/解除。 */
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
    if (self->m_base.m_base.m_currentRow > row)
        self->m_base.m_base.m_currentRow--;
    else if (self->m_base.m_base.m_currentRow == row)
        self->m_base.m_base.m_currentRow = -1;
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
    xtw_cwOnColumnRemoved(self, column); /* 挂载表随列号平移/解除。 */
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
    if (self->m_base.m_base.m_currentColumn > column)
        self->m_base.m_base.m_currentColumn--;
    else if (self->m_base.m_base.m_currentColumn == column)
        self->m_base.m_base.m_currentColumn = -1;
    xtw_syncHeaders(self);
    xtw_updateContentSize(self);
    xtw_cellWidgetRelayout(self);
    XWidget_update((XWidget*)self);
    xtw_syncModelDimension(self);
}

/* ==================== 单元格 ==================== */

/** @brief 单格矩形失效：几何与 paintEvent 的格子映射同式。
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

/** @brief 整行搬移（把 from 行整体移到 to 行，其余行顺移）。 */
static void xtw_moveRow(XTableWidget* self, int from, int to)
{
    XTableWidgetItem** moved;
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
}

/** @brief setItem 的排序自动插入（对齐 QTableModel::setItem 的 sorted 分支）。
 *  @details 仅当 sortingEnabled 且水平表头的排序指示段等于 column 时生效：
 *           取该列「前导非空段」（Qt columnItems 遇到 NULL 即停），把新条目
 *           按 DisplayRole 用 lower_bound 定位后整行搬移；搬移后**不**发
 *           dataChanged（对齐 Qt 走 rowsMoved 语义）。
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
    int row;
    int col;
    bool changed = false;
    if (!self || !range) return;
    /* 对齐 Qt（qtablewidget.cpp:2362-2373）：两个角都必须合法，否则整体忽略。 */
    if (range->m_top < 0 || range->m_bottom >= self->m_rows ||
        range->m_left < 0 || range->m_right >= self->m_columns)
        return;
    if (range->m_top > range->m_bottom || range->m_left > range->m_right)
        return; /* 倒置范围：Qt 由 QItemSelection 构造归一化，此处按空范围忽略。 */
    for (row = range->m_top; row <= range->m_bottom; ++row) {
        for (col = range->m_left; col <= range->m_right; ++col) {
            XTableWidgetItem* item = xtw_cell(self, row, col);
            if (!item || item->m_selected == (select ? 1u : 0u)) continue;
            item->m_selected = select ? 1 : 0;
            changed = true;
        }
    }
    /* 当前跟踪选区同步（Qt 由 selectionModel 驱动）。 */
    if (select) {
        self->m_selectionRow = (int16_t)range->m_top;
        self->m_selectionColumn = (int16_t)range->m_left;
    }
    if (!changed) return;
    xtw_emit0(self, (size_t)XTableWidget_itemSelectionChanged_signal);
    XWidget_update((XWidget*)self);
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

void XTableWidget_setSortingEnabled(XTableWidget* self, bool enable)
{
    XHeaderView* hh;
    if (!self) return;
    self->m_headerSortEnabled = enable;
    hh = XTableView_horizontalHeader(&self->m_base);
    if (!hh) return;
    /* Qt：开了排序才让表头可点（点击排序）；否则表头只用于拖分隔线。 */
    XHeaderView_setSectionsClickable(hh, enable);
    if (enable) {
        XObject_connect_1((XObject*)hh,
                          XSignal(XHeaderView_sectionClicked_signal),
                          (XObject*)self, xtwHeaderClicked,
                          XConnectionType_Direct);
    }
}

void XTableWidget_sortItems(XTableWidget* self, int column, int order)
{
    int i;
    int j;
    if (!self || column < 0 || column >= self->m_columns) return;
    /* 对齐 QTableModel::sort：整行（含 verticalHeaderItems 指针）一起重排；
     * 该列为空的行走「不可排序」组，保持相对顺序排在最后。 */
    for (i = 0; i < self->m_rows; ++i) {
        for (j = 0; j < self->m_rows - 1 - i; ++j) {
            const XTableWidgetItem* a = xtw_cell(self, j, column);
            const XTableWidgetItem* b = xtw_cell(self, j + 1, column);
            bool swap;
            /* 空值下沉：a 空且 b 非空 → 交换。 */
            if (!a && b) {
                swap = true;
            } else if (a && !b) {
                swap = false;
            } else {
                swap = (order == 0) ? XTableWidgetItem_lessThan(b, a)
                                    : XTableWidgetItem_lessThan(a, b);
            }
            if (swap) {
                XTableWidgetItem** tmp = self->m_cells[j];
                self->m_cells[j] = self->m_cells[j + 1];
                self->m_cells[j + 1] = tmp;
            }
        }
    }
    self->m_base.m_sortColumn = (int16_t)column;
    self->m_base.m_sortOrder = (int16_t)(order ? 1 : 0);
    if (self->m_base.m_horizontalHeader)
        XHeaderView_setSortIndicator(self->m_base.m_horizontalHeader, column,
                                     order ? 1 : 0);
    XWidget_update((XWidget*)self);
}

void XTableWidget_clear(XTableWidget* self)
{
    int i;
    if (!self) return;
    /* 对齐 Qt（qtablewidget.cpp:2578）：先 selectionModel()->clear()。 */
    XAbstractItemView_clearSelection(&self->m_base.m_base);
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
    if (!self) return;
    /* 对齐 Qt（qtablewidget.cpp:2592）：先 selectionModel()->clear()。 */
    XAbstractItemView_clearSelection(&self->m_base.m_base);
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

/** @brief 施加选择命令到条目选中位（Qt selectionModel->select 的条目级
 *         等价物；返回选区是否实际变化）。
 *
 *        语义对齐 Qt 6.8 QItemSelectionModel 的平坦表等价：Rows/Columns 先
 *        展开成整行/整列（qitemselectionmodel.cpp expandSelection 口径），
 *        Clear 先清空既有选区，随后 Select/Deselect/Toggle 逐格施加
 *        （已处于目标态为幂等）。空格（无条目）无法承载选中位，跳过。 */
static bool xtw_applySelectionCommand(XTableWidget* self, int row, int column,
                                      int command)
{
    int topRow;
    int bottomRow;
    int leftCol;
    int rightCol;
    int r;
    int c;
    bool changed = false;
    if (!self || command == XItemSelectionModelSelectionFlag_NoUpdate)
        return false;
    if (command & XItemSelectionModelSelectionFlag_Clear) {
        for (r = 0; r < self->m_rows; ++r) {
            for (c = 0; c < self->m_columns; ++c) {
                XTableWidgetItem* cell = xtw_cell(self, r, c);
                if (!cell || !cell->m_selected) continue;
                cell->m_selected = 0;
                changed = true;
            }
        }
        self->m_selectionRow = -1;
        self->m_selectionColumn = -1;
    }
    if (row < 0 || column < 0) return changed;
    topRow = row;
    bottomRow = row;
    leftCol = column;
    rightCol = column;
    if ((command & XItemSelectionModelSelectionFlag_Rows) &&
        self->m_columns > 0) {
        leftCol = 0;
        rightCol = self->m_columns - 1;
    }
    if ((command & XItemSelectionModelSelectionFlag_Columns) &&
        self->m_rows > 0) {
        topRow = 0;
        bottomRow = self->m_rows - 1;
    }
    for (r = topRow; r <= bottomRow; ++r) {
        for (c = leftCol; c <= rightCol; ++c) {
            XTableWidgetItem* cell = xtw_cell(self, r, c);
            bool want;
            if (!cell) continue;
            if (command & XItemSelectionModelSelectionFlag_Toggle) {
                want = (cell->m_selected == 0);
            } else if (command & XItemSelectionModelSelectionFlag_Select) {
                want = true;
            } else if (command & XItemSelectionModelSelectionFlag_Deselect) {
                want = false;
            } else {
                continue;
            }
            if ((cell->m_selected != 0) == want) continue;
            cell->m_selected = want ? 1 : 0;
            changed = true;
        }
    }
    return changed;
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
    int prevR;
    int prevC;
    XTableWidgetItem* prevItem;
    XTableWidgetItem* curItem;
    bool selChanged;
    bool trackChanged;
    if (!self) return;
    prevR = self->m_base.m_base.m_currentRow;
    prevC = self->m_base.m_base.m_currentColumn;
    prevItem = xtw_cell(self, prevR, prevC);
    /* 越界收敛（对齐 Qt：无效索引清除当前项）。 */
    if (row < 0 || row >= self->m_rows ||
        column < 0 || column >= self->m_columns) {
        row = -1;
        column = -1;
    }
    /* 变化门禁：同格重设不移动、不发 currentChanged 族信号——但选择命令
     * 仍施加（对齐 Qt setCurrentIndex：索引未变时依旧按 command 改变选区，
     * qitemselectionmodel.cpp:1389-1409）。 */
    if (row == prevR && column == prevC) {
        if (xtw_applySelectionCommand(self, row, column, command)) {
            curItem = xtw_cell(self, row, column);
            if (curItem && curItem->m_selected) {
                self->m_selectionRow = (int16_t)row;
                self->m_selectionColumn = (int16_t)column;
            } else {
                self->m_selectionRow = -1;
                self->m_selectionColumn = -1;
            }
            xtw_emit0(self, (size_t)XTableWidget_itemSelectionChanged_signal);
            {
                XRect rect = XTableWidget_visualItemRect(self, curItem);
                if (!XRect_isEmpty(&rect))
                    XWidget_updateRect((XWidget*)self, &rect);
            }
        }
        return;
    }
    self->m_base.m_base.m_currentRow = (int16_t)row;
    self->m_base.m_base.m_currentColumn = (int16_t)column;
    if (row >= 0)
        XAbstractItemView_scrollToHint(&self->m_base.m_base, row, column,
                                       XAbstractItemViewScrollHint_EnsureVisible);
    curItem = xtw_cell(self, row, column);
    /* 对齐 Qt（qtablewidget.cpp:1700-1709）：仅当新旧至少一个 item 非空
     * 时才发 currentItemChanged。 */
    if (prevItem || curItem) {
        XVarList* args = XVarList_Create(
            XVar(const XTableWidgetItem*, curItem),
            XVar(const XTableWidgetItem*, prevItem));
        if (args) {
            if (((XObject*)self)->m_signalSlot)
                XObject_emitSignal(
                    (XObject*)self,
                    (size_t)XTableWidget_currentItemChanged_signal, args,
                    NULL, NULL, XEVENT_PRIORITY_NORMAL);
            else
                XVarList_delete(args);
        }
    }
    /* 对齐 Qt：currentCellChanged 携带前后两组行列，无效为 -1。 */
    {
        int vr = row;
        int vc = column;
        int pr = prevR;
        int pc = prevC;
        XVarList* args = XVarList_Create(
            XVar(int, vr), XVar(int, vc), XVar(int, pr), XVar(int, pc));
        if (args) {
            if (((XObject*)self)->m_signalSlot)
                XObject_emitSignal(
                    (XObject*)self,
                    (size_t)XTableWidget_currentCellChanged_signal, args,
                    NULL, NULL, XEVENT_PRIORITY_NORMAL);
            else
                XVarList_delete(args);
        }
    }
    /* 选择命令（2 参入口缺省 ClearAndSelect：当前格并入并清掉旧选区）。 */
    selChanged = xtw_applySelectionCommand(self, row, column, command);
    /* 跟踪选区与 Qt 选区同口径：只有当前格**确实处于选中态**时才作为
     * 选区补录点（Qt 的 selectedItems 只返回真选中条目；此前无条件跟踪
     * 会让 NoUpdate 路径把当前格误报为选中）。 */
    {
        XTableWidgetItem* cell = xtw_cell(self, row, column);
        bool curSelected = cell && (cell->m_selected != 0);
        if (curSelected) {
            trackChanged = (self->m_selectionRow != row ||
                            self->m_selectionColumn != column);
            self->m_selectionRow = (int16_t)row;
            self->m_selectionColumn = (int16_t)column;
        } else {
            trackChanged = (self->m_selectionRow != -1 ||
                            self->m_selectionColumn != -1);
            self->m_selectionRow = -1;
            self->m_selectionColumn = -1;
        }
    }
    if (selChanged || trackChanged)
        xtw_emit0(self, (size_t)XTableWidget_itemSelectionChanged_signal);
    /* 单格失效（脏区收窄）：当前格/选区高亮只影响新旧两格矩形。 */
    {
        XRect rect = XTableWidget_visualItemRect(self, prevItem);
        if (!XRect_isEmpty(&rect))
            XWidget_updateRect((XWidget*)self, &rect);
        rect = XTableWidget_visualItemRect(self, curItem);
        if (!XRect_isEmpty(&rect))
            XWidget_updateRect((XWidget*)self, &rect);
    }
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

const XTableWidgetItem* XTableWidget_currentItem_const(const XTableWidget* self)
{ return XTableWidget_currentItem(self); }

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

int XTableWidget_findItems(const XTableWidget* self, const char* text,
                           int flags, XTableWidgetItem** out, int maxCount)
{
    int total = 0;
    int written = 0;
    int row;
    int col;
    if (!self) return 0;
    /* 对齐 Qt（qtablewidget.cpp:2424-2437）：**列优先**遍历——外层列、
     * 内层行，逐个 match。 */
    for (col = 0; col < self->m_columns; ++col) {
        for (row = 0; row < self->m_rows; ++row) {
            XTableWidgetItem* cell = xtw_cell(self, row, col);
            const char* cellText;
            bool hit;
            if (!cell) continue; /* 对齐 Qt：空条目无 DisplayRole，不参与匹配。 */
            cellText = XTableWidgetItem_text_toUtf8(cell);
            if (!cellText) cellText = "";
            if (flags == 1) /* 精确相等（对齐 Qt::MatchExactly）。 */
                hit = (XStrcmp(cellText, text ? text : "") == 0);
            else            /* 包含子串（对齐 Qt::MatchContains）。 */
                hit = (XStrstr(cellText, text ? text : "") != NULL);
            if (!hit) continue;
            if (out && written < maxCount) {
                out[written] = cell;
                ++written;
            }
            ++total;
        }
    }
    return total;
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
     * cellAt=(0,0) 却被吸附成 (0,1)，itemAt 命中错格。 */
    if (XTableView_rowSpan(&self->m_base, row, col) > 1 ||
        XTableView_columnSpan(&self->m_base, row, col) > 1) {
        int orow = row;
        int ocol = col;
        if (XTableView_indexAt(&self->m_base, x, y, &orow, &ocol)) {
            row = orow;
            col = ocol;
        }
    }
    return xtw_cell(self, row, col);
}

int XTableWidget_selectedItems(const XTableWidget* self,
                               XTableWidgetItem** out, int maxCount)
{
    int total = 0;
    int written = 0;
    int row;
    int col;
    bool trackedListed = false;
    if (!self) return 0;
    /* 对齐 Qt：只返回实际存在条目的选中格。 */
    for (row = 0; row < self->m_rows; ++row) {
        for (col = 0; col < self->m_columns; ++col) {
            XTableWidgetItem* cell = xtw_cell(self, row, col);
            if (!cell || !cell->m_selected) continue;
            if (row == self->m_selectionRow &&
                col == self->m_selectionColumn)
                trackedListed = true;
            if (out && written < maxCount) {
                out[written] = cell;
                ++written;
            }
            ++total;
        }
    }
    /* 当前跟踪选区未被标记覆盖时补录（点击/键盘选择路径）。 */
    if (!trackedListed && self->m_selectionRow >= 0 &&
        self->m_selectionRow < self->m_rows &&
        self->m_selectionColumn >= 0 &&
        self->m_selectionColumn < self->m_columns) {
        XTableWidgetItem* cell = xtw_cell(self, self->m_selectionRow,
                                          self->m_selectionColumn);
        if (cell) {
            if (out && written < maxCount) {
                out[written] = cell;
                ++written;
            }
            ++total;
        }
    }
    return total;
}

int XTableWidget_selectedRanges(const XTableWidget* self,
                                XTableWidgetSelectionRange* out, int maxCount)
{
    int total = 0;
    int row;
    int col;
    bool trackedListed = false;
    int lastLeft = -1;
    int lastBottom = -2;
    int lastRight = -1;
    if (!self) return 0;
    /* 逐行归并：行内取 [最小,最大] 选中列为跨度，行连续且跨度一致者合并。 */
    for (row = 0; row < self->m_rows; ++row) {
        int left = -1;
        int right = -1;
        for (col = 0; col < self->m_columns; ++col) {
            const XTableWidgetItem* cell = xtw_cell(self, row, col);
            if (!cell || !cell->m_selected) continue;
            if (left < 0) left = col;
            right = col;
            if (row == self->m_selectionRow &&
                col == self->m_selectionColumn)
                trackedListed = true;
        }
        if (left < 0) continue;
        if (lastBottom == row - 1 && lastLeft == left && lastRight == right) {
            /* 与最近范围可合并：扩展结束行（已写出者同步改写）。 */
            lastBottom = row;
            if (out && total - 1 >= 0 && total - 1 < maxCount)
                out[total - 1].m_bottom = row;
            continue;
        }
        lastLeft = left;
        lastBottom = row;
        lastRight = right;
        if (out && total < maxCount)
            out[total] = XTableWidgetSelectionRange_create(row, left, row,
                                                           right);
        ++total;
    }
    /* 当前跟踪选区未被标记覆盖时补录 1x1 范围。 */
    if (!trackedListed && self->m_selectionRow >= 0 &&
        self->m_selectionRow < self->m_rows &&
        self->m_selectionColumn >= 0 &&
        self->m_selectionColumn < self->m_columns &&
        xtw_cell(self, self->m_selectionRow, self->m_selectionColumn)) {
        if (out && total < maxCount)
            out[total] = XTableWidgetSelectionRange_create(
                self->m_selectionRow, self->m_selectionColumn,
                self->m_selectionRow, self->m_selectionColumn);
        ++total;
    }
    return total;
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
    /* 对齐 Qt visualItemRect → QTableView::visualRect：合并区间原点给整块。 */
    return XTableView_visualRect(&self->m_base, row, column);
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

/** @brief 计算列 x 起点（含水平滚动偏移；隐藏列占宽 0）。 */
static int xtw_colX(const XTableWidget* self, int col)
{
    int i;
    int x = xtw_vHeaderW(self);
    for (i = 0; i < col; ++i) {
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
        for (col = tw->m_columns - 1; col >= 0; --col) {
            if (!xtw_colHidden(tw, col)) { lastVis = col; break; }
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
    for (col = 0; col < tw->m_columns; ++col) {
        int cx;
        int cw;
        XStyle* style = NULL;
        XString* label;
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
             * position 只按**正在绘制**的段判定：首列（col==0）为
             * Beginning、末列（col==columns-1）为 End、唯一列为
             * OnlyOneSection——与 Qt 的 isFirstVisibleSection/
             * isLastVisibleSection 同向（本库表头无隐藏段概念，隐藏列
             * 由列宽 0 表达，见 xtw_colX）。
             *
             * 段语义只影响 QSS 的 :first/:last 选择，**不影响列宽与
             * 点选排序**：列宽的事实源仍是 m_base.m_colWidths（Fixed
             * 模式由 XHeaderView 承载），排序仍走 XTableWidget 的
             * sectionClicked 路径（下方命中测试与
             * XTableView_horizontalHeader 的 sortIndicator）。 */
            XStyleOptionHeader hs;
            XStyleOptionHeader_init(&hs, XStyleCE_HeaderSection);
            XRect_init(&hs.m_base.m_rect, cx, 0, cw, tw->m_headerHeight);
            hs.m_base.m_state = XWidget_isEnabled((XWidget*)tw)
                ? XStyleState_Enabled : 0;
            hs.m_base.m_state |= XStyleState_Horizontal;
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
             * isLastVisibleSection；隐藏列不参与 ::first/::last 语义）。 */
            {
                int firstVis = -1;
                int lastVis = -1;
                int k;
                for (k = 0; k < tw->m_columns; ++k) {
                    if (xtw_colHidden(tw, k)) continue;
                    if (firstVis < 0) firstVis = k;
                    lastVis = k;
                }
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
            }
            XStyle_drawControl(style, XStyleCE_HeaderSection,
                               (const XStyleOption*)&hs,
                               &painter, (XWidget*)tw);
            /* CE_HeaderLabel：QSS 代理命中 ::section 时已在一趟内画完
             * 标签（含 font-weight/padding，对齐 Qt 两处共用同一
             * subRule），未命中则回底层居中文本，见
             * VXStyleSheetStyle_drawControl 的表头段分支。 */
            hs.m_base.m_type = XStyleCE_HeaderLabel;
            XStyle_drawControl(style, XStyleCE_HeaderLabel,
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
        for (col = 0; col < tw->m_columns; ++col) {
            XTableWidgetItem* cell;
            int cx;
            int cw;
            XRect cellRect;
            bool sel;
            XColor cellBg;
            XColor cellFg;
            bool hasBg;
            bool hasFg;
            if (xtw_colHidden(tw, col)) continue; /* 隐藏列 0 宽、不绘制 */
            cell = xtw_cell(tw, row, col);
            cx = xtw_colX(tw, col) - ho;
            cw = tw->m_base.m_colWidths[col];
            XRect_init(&cellRect, cx, cy, cw, rh);
            if (cellRect.x + cellRect.width < 0 || cellRect.x > w) continue;
            sel = cell && (cell->m_selected ||
                (row == tw->m_base.m_base.m_currentRow &&
                 col == tw->m_base.m_base.m_currentColumn));
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
}

/** @brief 坐标 → 单元格（返回行列；表头/越界返回 -1）。 */
static void xtw_cellAt(const XTableWidget* self, int x, int y,
                       int* row, int* col)
{
    int i;
    int acc;
    int rh;
    *row = -1; *col = -1;
    if (!self || x < xtw_vHeaderW(self) || y < self->m_headerHeight) return;
    /* 命中与绘制同源：隐藏列宽 0（不可命中、不占位），隐藏行高 0。 */
    acc = xtw_vHeaderW(self) - xtw_hOffset(self);
    for (i = 0; i < self->m_columns; ++i) {
        int cw;
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
    if (pos.y >= tw->m_headerHeight) return false;   /* 不在表头带 */
    hh = XTableView_horizontalHeader(&tw->m_base);
    if (!hh) return false;
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
 *  与表头高（headerHeight）偏移、实际列宽/行高、滚动偏移。 */
static bool VX_tableWidget_visualRect(const XAbstractItemView* self,
                                      int row, int col, XRect* out)
{
    const XTableWidget* tw = (const XTableWidget*)self;
    XRect rect;
    if (!tw || !out || row < 0 || col < 0 ||
        row >= tw->m_rows || col >= tw->m_columns)
        return false;
    XRect_init(&rect,
               xtw_colX(tw, col) - xtw_hOffset(tw),
               tw->m_headerHeight + row * (int)tw->m_base.m_rowHeight
                   - xtw_vOffset(tw),
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
