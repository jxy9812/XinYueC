#ifndef XABSTRACTITEMVIEW_H
#define XABSTRACTITEMVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractScrollArea.h"
#include "XAbstractItemModel.h"
#include "XItemSelectionModel.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/**
 * @brief      编辑触发（对标 Qt 6.8 QAbstractItemView::EditTrigger 位组合）。
 */
typedef enum XAbstractItemViewEditTrigger
{
    XAbstractItemViewEditTrigger_NoEditTriggers = 0,      /**< 不进入编辑。 */
    XAbstractItemViewEditTrigger_CurrentChanged = 1 << 0, /**< 当前项变化即编辑。 */
    XAbstractItemViewEditTrigger_DoubleClicked = 1 << 1,  /**< 双击编辑。 */
    XAbstractItemViewEditTrigger_SelectedClicked = 1 << 2,/**< 选中项单击编辑。 */
    XAbstractItemViewEditTrigger_EditKeyPressed = 1 << 3, /**< 编辑键按下编辑。 */
    XAbstractItemViewEditTrigger_AnyKeyPressed = 1 << 4,  /**< 任意键编辑。 */
    XAbstractItemViewEditTrigger_AllEditTriggers = 0x1f   /**< 全部触发。 */
} XAbstractItemViewEditTrigger;

/**
 * @brief      选择模式（对标 Qt 6.8 QAbstractItemView::SelectionMode）。
 */
typedef enum XAbstractItemViewSelectionMode
{
    XAbstractItemViewSelectionMode_NoSelection = 0,   /**< 禁止选择。 */
    XAbstractItemViewSelectionMode_SingleSelection = 1, /**< 单选。 */
    XAbstractItemViewSelectionMode_MultiSelection = 2,  /**< 多选（无快捷键）。 */
    XAbstractItemViewSelectionMode_ExtendedSelection = 3, /**< 扩展选择（默认）。 */
    XAbstractItemViewSelectionMode_ContiguousSelection = 4 /**< 连续选择。 */
} XAbstractItemViewSelectionMode;

/**
 * @brief      选择行为（对标 Qt 6.8 QAbstractItemView::SelectionBehavior）。
 */
typedef enum XAbstractItemViewSelectionBehavior
{
    XAbstractItemViewSelectionBehavior_SelectItems = 0,    /**< 逐项选择。 */
    XAbstractItemViewSelectionBehavior_SelectRows = 1,     /**< 整行选择。 */
    XAbstractItemViewSelectionBehavior_SelectColumns = 2   /**< 整列选择。 */
} XAbstractItemViewSelectionBehavior;

/**
 * @brief      拖放模式（对标 Qt 6.8 QAbstractItemView::DragDropMode，数值一致）。
 */
typedef enum XAbstractItemViewDragDropMode
{
    XAbstractItemViewDragDropMode_NoDragDrop = 0,   /**< 不支持拖放（默认）。 */
    XAbstractItemViewDragDropMode_DragOnly = 1,     /**< 仅可拖出。 */
    XAbstractItemViewDragDropMode_DropOnly = 2,     /**< 仅可接收。 */
    XAbstractItemViewDragDropMode_DragDrop = 3,     /**< 可拖出亦可接收。 */
    XAbstractItemViewDragDropMode_InternalMove = 4  /**< 仅视图内部移动。 */
} XAbstractItemViewDragDropMode;

/**
 * @brief      滚动模式（对标 Qt 6.8 QAbstractItemView::ScrollMode，数值一致）。
 */
typedef enum XAbstractItemViewScrollMode
{
    XAbstractItemViewScrollMode_ScrollPerItem = 0,  /**< 按条目步进滚动（默认）。 */
    XAbstractItemViewScrollMode_ScrollPerPixel = 1  /**< 按像素平滑滚动。 */
} XAbstractItemViewScrollMode;

/**
 * @brief      滚动提示（对标 Qt 6.8 QAbstractItemView::ScrollHint，数值一致）。
 */
typedef enum XAbstractItemViewScrollHint
{
    XAbstractItemViewScrollHint_EnsureVisible = 0,   /**< 仅保证可见（默认）。 */
    XAbstractItemViewScrollHint_PositionAtTop = 1,   /**< 定位到视口顶部。 */
    XAbstractItemViewScrollHint_PositionAtBottom = 2,/**< 定位到视口底部。 */
    XAbstractItemViewScrollHint_PositionAtCenter = 3 /**< 定位到视口中央。 */
} XAbstractItemViewScrollHint;

/**
 * @brief      文本省略模式（对标 Qt 6.8 Qt::TextElideMode，数值一致）。
 */
typedef enum XAbstractItemViewTextElideMode
{
    XAbstractItemViewTextElideMode_ElideLeft = 0,   /**< 省略号置于左侧。 */
    XAbstractItemViewTextElideMode_ElideMiddle = 1, /**< 省略号置于中间。 */
    XAbstractItemViewTextElideMode_ElideRight = 2,  /**< 省略号置于右侧（默认）。 */
    XAbstractItemViewTextElideMode_ElideNone = 3    /**< 不省略。 */
} XAbstractItemViewTextElideMode;

XCLASS_DEFINE_BEGING(XAbstractItemView)
XCLASS_DEFINE_ENUM(XAbstractItemView, IndexAt) = XCLASS_VTABLE_GET_SIZE(XAbstractScrollArea),
XCLASS_DEFINE_END(XAbstractItemView)

/**
 * @brief 抽象条目视图基类（对标 Qt 6.8 QAbstractItemView）。
 *
 *        承载当前索引、选择模式/行为、编辑触发等通用视图属性；
 *        具体视图（表格/列表/树）由派生类实现数据呈现。
 */
typedef struct XAbstractItemView
{
    XAbstractScrollArea m_base;      /**< 基类成员；必须是第一个。 */
    int m_currentRow;                /**< 当前行；-1=无。 */
    int m_currentColumn;             /**< 当前列；-1=无。 */
    int m_selectionMode;             /**< XAbstractItemViewSelectionMode。 */
    int m_selectionBehavior;         /**< XAbstractItemViewSelectionBehavior。 */
    int m_editTriggers;              /**< XAbstractItemViewEditTrigger 位组合。 */
    bool m_alternatingRowColors;     /**< 交替行色（默认 false）。 */
    bool m_autoScroll;               /**< 自动滚动（默认 true）。 */
    XAbstractItemModel* m_model;     /**< 数据模型（借用；可为 NULL）。 */
    XItemSelectionModel* m_selectionModel; /**< 选择模型（对象拥有；懒创建）。 */
    int m_rootRow;                   /**< 根索引行（预留树；默认 -1=根）。 */
    int m_rootCol;                   /**< 根索引列（预留树；默认 -1=根）。 */
    int m_iconW;                     /**< 图标尺寸宽（默认 16）。 */
    int m_iconH;                     /**< 图标尺寸高（默认 16）。 */
    bool m_keyboardSearch;           /**< 键盘搜索开关（默认 true）。 */
    bool m_tabKeyNavigation;         /**< Tab 键焦点导航（默认 false）。 */
    int m_autoScrollMargin;          /**< 自动滚动判定边距（像素；默认 16）。 */
    bool m_showDropIndicator;        /**< 是否显示拖放指示器（默认 true）。 */
    int m_defaultDropAction;         /**< 默认拖放动作（数值对齐 Qt::DropAction；默认 0=IgnoreAction）。 */
    int m_dragDropMode;              /**< 拖放模式（XAbstractItemViewDragDropMode；默认 NoDragDrop）。 */
    bool m_dragEnabled;              /**< 允许拖出条目（默认 false）。 */
    bool m_dragDropOverwriteMode;    /**< 拖放覆盖模式（基类默认 false，见 Qt 文档）。 */
    int m_textElideMode;             /**< 文本省略模式（XAbstractItemViewTextElideMode；默认 ElideRight）。 */
    int m_verticalScrollMode;        /**< 垂直滚动模式（XAbstractItemViewScrollMode；默认 ScrollPerItem）。 */
    int m_horizontalScrollMode;      /**< 水平滚动模式（XAbstractItemViewScrollMode；默认 ScrollPerItem）。 */
    void* m_itemDelegate;            /**< 条目委托不透明指针（委托体系未建，仅承载；默认 NULL）。 */
    bool* m_persistentFlags;         /**< 持久编辑器打开标记平行表（行主序扁平数组；按需扩容；NULL=未分配）。 */
    int m_persistentRows;            /**< 持久编辑器表已分配行数（0=未分配）。 */
    int m_persistentCols;            /**< 持久编辑器表已分配列数（0=未分配）。 */
    void** m_indexWidgets;           /**< 条目控件指针平行表（行主序扁平数组；按需扩容；NULL=未分配）。 */
    int m_indexWidgetRows;           /**< 条目控件表已分配行数（0=未分配）。 */
    int m_indexWidgetCols;           /**< 条目控件表已分配列数（0=未分配）。 */
    void** m_columnDelegates;        /**< 列级委托不透明指针表（按列号索引；按需扩容；NULL=未分配）。 */
    int m_columnDelegateCount;       /**< 列级委托表已分配容量（0=未分配）。 */
    void** m_rowDelegates;           /**< 行级委托不透明指针表（按行号索引；按需扩容；NULL=未分配）。 */
    int m_rowDelegateCount;          /**< 行级委托表已分配容量（0=未分配）。 */
} XAbstractItemView;

XVtable* XAbstractItemView_class_init(void);

/**
 * @brief 初始化嵌入式抽象条目视图。
 *
 * @param self 目标视图指针，不能为空。
 * @param parent 父控件（可空）。
 * @param flags 控件标志位。
 * @return 无返回值。
 */
void XAbstractItemView_init(XAbstractItemView* self, XWidget* parent,
                            XWidgetFlags flags);

/**
 * @brief 堆上创建抽象条目视图。
 *
 * @param memory 内存类型。
 * @param parent 父控件（可空）。
 * @param flags 控件标志位。
 * @return 视图指针；分配失败返回 NULL。
 */
XAbstractItemView* XAbstractItemView_create_ex(XMemoryType memory,
                                               XWidget* parent,
                                               XWidgetFlags flags);
#define XAbstractItemView_create(parent, flags) \
    XAbstractItemView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))

/** @brief 析构入口（查表分派父类析构）。 */
#define XAbstractItemView_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上视图（查表分派析构并释放内存）。 */
#define XAbstractItemView_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/* ==================== 当前索引（对标 QAbstractItemView） ==================== */

/**
 * @brief 设置当前单元格（对标 setCurrentIndex；SelectCurrent 语义）。
 *
 *        选区联动：选择模型已挂接时同步其当前索引；选择模式非 NoSelection
 *        且 (row,column) 有效时同时选中该单元格（同 Qt 的 current 变更
 *        携带 Select 命令）。无选择模型（未懒创建/被外部置空）时仅更新
 *        视图自身当前索引。
 *
 * @param self 目标视图指针。
 * @param row 行号（-1=无当前项，仅同步清除选择模型当前索引）。
 * @param column 列号。
 * @return 无返回值。
 */
void XAbstractItemView_setCurrentIndex(XAbstractItemView* self, int row,
                                       int column);
/** @brief 查询当前行。 @param self 目标视图指针。 @return 行号；无当前项返回 -1。 */
int XAbstractItemView_currentRow(const XAbstractItemView* self);
/** @brief 查询当前列。 @param self 目标视图指针。 @return 列号；无当前项返回 -1。 */
int XAbstractItemView_currentColumn(const XAbstractItemView* self);
/**
 * @brief 查询当前索引（对标 currentIndex()；本库以 (row,column) 平面承载）。
 * @param self 目标视图指针。
 * @param outRow 输出行号（可空；无当前项或 self 为空置 -1）。
 * @param outCol 输出列号（可空；无当前项或 self 为空置 -1）。
 * @return 当前索引有效（行、列均 >=0）返回 true。
 */
bool XAbstractItemView_currentIndex(const XAbstractItemView* self,
                                    int* outRow, int* outCol);

/* ==================== 重置与布局（对标 QAbstractItemView） ==================== */

/**
 * @brief 重置视图内部状态（对标 reset()；@note 模型重建场景调用）。
 *
 *        语义（对齐 Qt 6.8 reset）：清除视图当前索引（行/列复位 -1）、
 *        同步清除选择模型的当前索引与全部选中、根索引复位为无效；
 *        同时清空持久编辑器打开标记与条目控件承载（持久编辑器/条目控件
 *        的索引随模型重建失效，同 Qt 关闭全部编辑器），已分配的平行表
 *        容量保留复用。最后请求一次全量重绘。
 *
 * @param self 目标视图指针。
 * @return 无返回值。
 */
void XAbstractItemView_reset(XAbstractItemView* self);
/**
 * @brief 触发一次条目全量布局与重绘（对标 doItemsLayout()）。
 *
 * @param self 目标视图指针。
 * @return 无返回值。
 *
 * @note 平铺模型简化：条目几何由固定网格（visualRect 行高 24/列宽 80）
 *       推导，无独立布局阶段；本实现仅对齐 Qt 的全量重绘效果请求一次
 *       update，滚动条范围仍由派生视图维护。
 */
void XAbstractItemView_doItemsLayout(XAbstractItemView* self);

/* ==================== 选择属性 ==================== */

/** @brief 设置选择模式。 @param self 目标视图指针。 @param mode 选择模式枚举。 @return 无返回值。 */
void XAbstractItemView_setSelectionMode(XAbstractItemView* self, int mode);
/** @brief 查询选择模式。 @param self 目标视图指针。 @return 选择模式枚举。 */
int XAbstractItemView_selectionMode(const XAbstractItemView* self);
/** @brief 设置选择行为。 @param self 目标视图指针。 @param behavior 选择行为枚举。 @return 无返回值。 */
void XAbstractItemView_setSelectionBehavior(XAbstractItemView* self,
                                            int behavior);
/** @brief 查询选择行为。 @param self 目标视图指针。 @return 选择行为枚举。 */
int XAbstractItemView_selectionBehavior(const XAbstractItemView* self);

/* ==================== 编辑属性 ==================== */

/** @brief 设置编辑触发。 @param self 目标视图指针。 @param triggers 位组合。 @return 无返回值。 */
void XAbstractItemView_setEditTriggers(XAbstractItemView* self, int triggers);
/** @brief 查询编辑触发。 @param self 目标视图指针。 @return 位组合。 */
int XAbstractItemView_editTriggers(const XAbstractItemView* self);
/**
 * @brief 编辑触发句柄（对标 edit(const QModelIndex&)；缺省以
 *        AllEditTriggers 进入编辑判定）。
 *
 *        语义（对齐 Qt 6.8 edit(index) → edit(index, AllEditTriggers,
 *        nullptr)）：self 为空、(row,column) 无效或编辑触发为
 *        NoEditTriggers 时直接返回 false（判定不通过）；否则进入
 *        编辑器创建阶段。
 *
 * @param self 目标视图指针。
 * @param row 行号（<0 视为无效索引）。
 * @param column 列号（<0 视为无效索引）。
 * @return 编辑会话成功开启返回 true；未开启返回 false。
 *
 * @note 编辑器体系未建：委托的 createEditor/提交回写（commitData、
 *       closeEditor）尚未实现，本句柄完成触发合法性判定后预留返回
 *       false（当前无编辑器可开，同 Qt 无委托时的失败路径）。
 */
bool XAbstractItemView_edit(XAbstractItemView* self, int row, int column);

/* ==================== 委托（对标 QAbstractItemView） ==================== */

/**
 * @brief 设置条目委托（对标 setItemDelegate(QAbstractItemDelegate*)）。
 *
 * @param self 目标视图指针。
 * @param delegate 委托不透明指针（借用；可为 NULL 恢复默认）。
 * @return 无返回值（变化时请求一次重绘，同 Qt 委托变化后刷新视口）。
 *
 * @note 委托体系未建：仅以 void* 不透明指针承载，不析构、不虚分派，
 *       由后续批次的委托/编辑实现读取生效。
 */
void XAbstractItemView_setItemDelegate(XAbstractItemView* self,
                                       void* delegate);
/**
 * @brief 查询条目委托（对标 itemDelegate()）。
 * @param self 目标视图指针。
 * @return 委托不透明指针；未设置返回 NULL。
 */
void* XAbstractItemView_itemDelegate(const XAbstractItemView* self);
/**
 * @brief 设置列级条目委托（对标 setItemDelegateForColumn(column, delegate)）。
 *
 *        该列全部条目优先使用此委托（行级委托优先级更高，见
 *        XAbstractItemView_itemDelegateForRow）；传 NULL 清除该列覆盖
 *        （回落到视图默认委托，同 Qt）。
 *
 * @param self 目标视图指针。
 * @param column 列号（<0 忽略）。
 * @param delegate 委托不透明指针（借用；可为 NULL 清除）。
 * @return 无返回值（变化时请求一次重绘，同 Qt 委托变化后刷新视口）。
 *
 * @note 列级委托体系未建：仅以 void* 不透明指针承载（按列号平行表，
 *       按需扩容），不析构、不虚分派，也不参与条目绘制/编辑分派，
 *       由后续批次的委托/编辑实现读取生效。
 */
void XAbstractItemView_setItemDelegateForColumn(XAbstractItemView* self,
                                                int column, void* delegate);
/**
 * @brief 查询列级条目委托（对标 itemDelegateForColumn(column)）。
 * @param self 目标视图指针。
 * @param column 列号。
 * @return 该列委托不透明指针；未设置、列号 <0 或 self 为空返回 NULL。
 */
void* XAbstractItemView_itemDelegateForColumn(const XAbstractItemView* self,
                                              int column);
/**
 * @brief 设置行级条目委托（对标 setItemDelegateForRow(row, delegate)）。
 *
 *        该行全部条目优先使用此委托（行级优先于列级，同 Qt 的
 *        itemDelegate(index) 解析顺序）；传 NULL 清除该行覆盖。
 *
 * @param self 目标视图指针。
 * @param row 行号（<0 忽略）。
 * @param delegate 委托不透明指针（借用；可为 NULL 清除）。
 * @return 无返回值（变化时请求一次重绘，同 Qt 委托变化后刷新视口）。
 *
 * @note 行级委托体系未建：仅以 void* 不透明指针承载（按行号平行表，
 *       按需扩容），不析构、不虚分派，也不参与条目绘制/编辑分派，
 *       由后续批次的委托/编辑实现读取生效。
 */
void XAbstractItemView_setItemDelegateForRow(XAbstractItemView* self,
                                             int row, void* delegate);
/**
 * @brief 查询行级条目委托（对标 itemDelegateForRow(row)）。
 * @param self 目标视图指针。
 * @param row 行号。
 * @return 该行委托不透明指针；未设置、行号 <0 或 self 为空返回 NULL。
 */
void* XAbstractItemView_itemDelegateForRow(const XAbstractItemView* self,
                                           int row);
/**
 * @brief 综合解析条目委托（对标 itemDelegate(const QModelIndex&)）。
 *
 *        解析优先级（同 Qt）：行级委托 → 列级委托 → 视图默认委托；
 *        逐级未设置时依次回落。
 *
 * @param self 目标视图指针。
 * @param row 行号。
 * @param col 列号。
 * @return 命中层级的委托不透明指针（借用）；全部未设置、行/列号 <0
 *         或 self 为空返回 NULL。
 *
 * @note 委托体系未建：本接口仅做指针解析（不析构、不虚分派），
 *       供后续批次的委托/编辑实现按索引取用生效。
 */
void* XAbstractItemView_itemDelegateForIndex(const XAbstractItemView* self,
                                             int row, int col);

/* ==================== 持久编辑器与条目控件（对标 QAbstractItemView） ==================== */

/**
 * @brief 为单元格打开持久编辑器（对标 openPersistentEditor(index)）。
 *
 *        持久编辑器不随当前索引/选中变化关闭，须显式 close。
 *
 * @param self 目标视图指针。
 * @param row 行号（<0 忽略）。
 * @param col 列号（<0 忽略）。
 * @return 无返回值（状态变化时请求一次重绘）。
 *
 * @note 简化承载：编辑器本体与绘制不在范围，仅按 (row,col) 维护
 *       "打开标记"平行表（bool 按需扩容）；不校验模型维度。
 */
void XAbstractItemView_openPersistentEditor(XAbstractItemView* self,
                                            int row, int col);
/**
 * @brief 关闭单元格持久编辑器（对标 closePersistentEditor(index)）。
 * @param self 目标视图指针。
 * @param row 行号（<0 或越界忽略）。
 * @param col 列号（<0 或越界忽略）。
 * @return 无返回值（状态变化时请求一次重绘）。
 */
void XAbstractItemView_closePersistentEditor(XAbstractItemView* self,
                                             int row, int col);
/**
 * @brief 查询单元格持久编辑器是否打开（对标 isPersistentEditorOpen(index)）。
 * @param self 目标视图指针。
 * @param row 行号。
 * @param col 列号。
 * @return 打开返回 true；未打开、越界或 self 为空返回 false。
 */
bool XAbstractItemView_isPersistentEditorOpen(const XAbstractItemView* self,
                                              int row, int col);
/**
 * @brief 设置单元格条目控件（对标 setIndexWidget(index, widget)）。
 *
 *        传入 NULL 清除该格控件（同 Qt）。已设置同款指针时为空操作。
 *
 * @param self 目标视图指针。
 * @param row 行号（<0 忽略）。
 * @param col 列号（<0 忽略）。
 * @param widget 控件不透明指针（借用；不转移所有权，不为空时挂到视口）。
 * @return 无返回值（变化时请求一次重绘）。
 *
 * @note 简化承载：仅以平行表存储 widget 指针（按需扩容），不接管所有权、
 *       不做几何摆放；模型变化（setModel）时承载被清空，指针仍归调用方。
 */
void XAbstractItemView_setIndexWidget(XAbstractItemView* self,
                                      int row, int col, void* widget);
/**
 * @brief 查询单元格条目控件（对标 indexWidget(index)）。
 * @param self 目标视图指针。
 * @param row 行号。
 * @param col 列号。
 * @return 控件指针；未设置、越界或 self 为空返回 NULL。
 */
void* XAbstractItemView_indexWidget(const XAbstractItemView* self,
                                    int row, int col);

/* ==================== 键盘与导航（对标 QAbstractItemView） ==================== */

/**
 * @brief 设置键盘搜索开关（对标 setKeyboardSearch）。
 *
 *        键盘搜索指直接键入文字时按前缀渐进匹配并跳转到对应条目。
 *        行为本体见 XAbstractItemView_keyboardSearch_2。
 *
 * @param self 目标视图指针。
 * @param enable true 启用（默认）。
 * @return 无返回值。
 */
void XAbstractItemView_setKeyboardSearch(XAbstractItemView* self, bool enable);
/**
 * @brief 查询键盘搜索开关（对标 keyboardSearch 状态语义）。
 * @param self 目标视图指针。
 * @return 启用返回 true。
 */
bool XAbstractItemView_keyboardSearch(const XAbstractItemView* self);
/**
 * @brief 键盘搜索行为本体（对标 Qt 6.8 keyboardSearch(const QString&)）。
 *
 *        语义：2000ms 内连续调用时将 text 追加为累积前缀（超时则重置为
 *        text）；随后从当前行的下一行起（环形回绕）在当前列逐行取模型
 *        文本做前缀匹配（XStrstr 命中起始位置即前缀匹配）；命中行执行
 *        setCurrentIndex（含选区联动）并 scrollTo 滚动至可见。累积前缀
 *        无命中时回退为仅本次 text 重新搜索（同 Qt 行为）。
 *
 * @param self 目标视图指针。
 * @param text 本次键入的文本；NULL 或空串仅重置累积前缀。
 * @return 命中并移动当前索引返回 true；无模型、开关关闭或未命中返回 false。
 *
 * @note 简化：累积前缀存于全库共享的静态缓冲（非每视图状态），容量
 *       64 字节，超出容量的追加字符被丢弃；interval 固定 2000ms
 *       （Qt 可经 keyboardInputInterval 配置，本库暂不承载）。
 */
bool XAbstractItemView_keyboardSearch_2(XAbstractItemView* self,
                                        const char* text);
/**
 * @brief 设置 Tab 键焦点导航（对标 setTabKeyNavigation）。
 *
 *        启用后 Tab/Backtab 在视图条目间移动当前索引而非切换焦点。
 *
 * @param self 目标视图指针。
 * @param enable true 启用（默认关闭，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note Tab/Backtab 按键处理逻辑不在本批范围，仅承载状态。
 */
void XAbstractItemView_setTabKeyNavigation(XAbstractItemView* self,
                                           bool enable);
/**
 * @brief 查询 Tab 键焦点导航。
 * @param self 目标视图指针。
 * @return 启用返回 true。
 */
bool XAbstractItemView_tabKeyNavigation(const XAbstractItemView* self);

/* ==================== 外观属性 ==================== */

/** @brief 设置交替行色。 @param self 目标视图指针。 @param enable true 启用。 @return 无返回值。 */
void XAbstractItemView_setAlternatingRowColors(XAbstractItemView* self,
                                               bool enable);
/** @brief 查询交替行色。 @param self 目标视图指针。 @return 启用返回 true。 */
bool XAbstractItemView_alternatingRowColors(const XAbstractItemView* self);
/** @brief 设置自动滚动。 @param self 目标视图指针。 @param enable true 启用。 @return 无返回值。 */
void XAbstractItemView_setAutoScroll(XAbstractItemView* self, bool enable);
/** @brief 查询自动滚动。 @param self 目标视图指针。 @return 启用返回 true。 */
bool XAbstractItemView_hasAutoScroll(const XAbstractItemView* self);
/**
 * @brief 设置自动滚动判定边距（对标 setAutoScrollMargin）。
 *
 *        拖动/拖放进入距视口边缘 margin 像素内时触发自动滚动。
 *
 * @param self 目标视图指针。
 * @param margin 边距（像素；默认 16，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note 自动滚动触发逻辑（拖放期间边缘判定与定时滚动）不在本批范围，仅承载状态。
 */
void XAbstractItemView_setAutoScrollMargin(XAbstractItemView* self,
                                           int margin);
/**
 * @brief 查询自动滚动判定边距。
 * @param self 目标视图指针。
 * @return 边距（像素）。
 */
int XAbstractItemView_autoScrollMargin(const XAbstractItemView* self);
/**
 * @brief 设置文本省略模式（对标 setTextElideMode）。
 * @param self 目标视图指针。
 * @param mode XAbstractItemViewTextElideMode 枚举（默认 ElideRight，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note 省略号绘制由派生视图/委托在绘制条目时读取该值生效。
 */
void XAbstractItemView_setTextElideMode(XAbstractItemView* self, int mode);
/**
 * @brief 查询文本省略模式。
 * @param self 目标视图指针。
 * @return XAbstractItemViewTextElideMode 枚举。
 */
int XAbstractItemView_textElideMode(const XAbstractItemView* self);
/**
 * @brief 设置垂直滚动模式（对标 setVerticalScrollMode）。
 * @param self 目标视图指针。
 * @param mode XAbstractItemViewScrollMode 枚举（默认 ScrollPerItem，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note 派生视图实现滚动时应按该模式选择条目步进或像素平滑滚动。
 */
void XAbstractItemView_setVerticalScrollMode(XAbstractItemView* self,
                                             int mode);
/**
 * @brief 查询垂直滚动模式。
 * @param self 目标视图指针。
 * @return XAbstractItemViewScrollMode 枚举。
 */
int XAbstractItemView_verticalScrollMode(const XAbstractItemView* self);
/**
 * @brief 设置水平滚动模式（对标 setHorizontalScrollMode）。
 * @param self 目标视图指针。
 * @param mode XAbstractItemViewScrollMode 枚举（默认 ScrollPerItem，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note 派生视图实现滚动时应按该模式选择条目步进或像素平滑滚动。
 */
void XAbstractItemView_setHorizontalScrollMode(XAbstractItemView* self,
                                               int mode);
/**
 * @brief 查询水平滚动模式。
 * @param self 目标视图指针。
 * @return XAbstractItemViewScrollMode 枚举。
 */
int XAbstractItemView_horizontalScrollMode(const XAbstractItemView* self);
/**
 * @brief 恢复垂直滚动模式缺省值（对标 resetVerticalScrollMode()）。
 *
 *        Qt 经样式 Hint（Sh_QListViewScrollMode 等）取缺省；本库缺省
 *        恒为 ScrollPerItem（字段初值），本函数将其复位为该缺省。
 *
 * @param self 目标视图指针。
 * @return 无返回值（模式变化时请求一次重绘）。
 */
void XAbstractItemView_resetVerticalScrollMode(XAbstractItemView* self);
/**
 * @brief 恢复水平滚动模式缺省值（对标 resetHorizontalScrollMode()）。
 *
 *        语义同 XAbstractItemView_resetVerticalScrollMode（复位为
 *        ScrollPerItem 缺省）。
 *
 * @param self 目标视图指针。
 * @return 无返回值（模式变化时请求一次重绘）。
 */
void XAbstractItemView_resetHorizontalScrollMode(XAbstractItemView* self);

/* ==================== 拖放属性（对标 QAbstractItemView） ==================== */

/**
 * @brief 设置是否允许拖出条目（对标 setDragEnabled）。
 * @param self 目标视图指针。
 * @param enable true 允许拖出（默认 false，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note 拖放本体（dragEnterEvent/dragMoveEvent/dropEvent 与数据编码）
 *       不在本批范围，仅承载状态；生效需配合 XAbstractItemViewDragDropMode_DragOnly
 *       或 DragDrop 模式。
 */
void XAbstractItemView_setDragEnabled(XAbstractItemView* self, bool enable);
/**
 * @brief 查询是否允许拖出条目。
 * @param self 目标视图指针。
 * @return 允许返回 true。
 */
bool XAbstractItemView_dragEnabled(const XAbstractItemView* self);
/**
 * @brief 设置拖放模式（对标 setDragDropMode）。
 * @param self 目标视图指针。
 * @param mode XAbstractItemViewDragDropMode 枚举（默认 NoDragDrop，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note 拖放本体不在本批范围，仅状态承载；设置 DragOnly/DropOnly/DragDrop
 *       后由后续批次的拖放事件处理读取生效。
 */
void XAbstractItemView_setDragDropMode(XAbstractItemView* self, int mode);
/**
 * @brief 查询拖放模式。
 * @param self 目标视图指针。
 * @return XAbstractItemViewDragDropMode 枚举。
 */
int XAbstractItemView_dragDropMode(const XAbstractItemView* self);
/**
 * @brief 设置拖放覆盖模式（对标 setDragDropOverwriteMode）。
 *
 *        true=放下时覆盖既有条目数据；false=作为新条目插入。
 *        Qt 基类默认 false，QTableView 子类为 true。
 *
 * @param self 目标视图指针。
 * @param overwrite 覆盖模式（默认 false，与 Qt 基类一致）。
 * @return 无返回值。
 *
 * @note 放下数据的写模型逻辑不在本批范围，仅承载状态。
 */
void XAbstractItemView_setDragDropOverwriteMode(XAbstractItemView* self,
                                                bool overwrite);
/**
 * @brief 查询拖放覆盖模式。
 * @param self 目标视图指针。
 * @return 覆盖模式返回 true。
 */
bool XAbstractItemView_dragDropOverwriteMode(const XAbstractItemView* self);
/**
 * @brief 设置默认拖放动作（对标 setDefaultDropAction）。
 * @param self 目标视图指针。
 * @param action 动作位值；数值对齐 Qt::DropAction / XDropAction
 *               （0=IgnoreAction、1=CopyAction、2=MoveAction、4=LinkAction；
 *               默认 0=IgnoreAction，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note 拖放本体不在本批范围，仅状态承载。
 */
void XAbstractItemView_setDefaultDropAction(XAbstractItemView* self,
                                            int action);
/**
 * @brief 查询默认拖放动作。
 * @param self 目标视图指针。
 * @return 动作位值（数值对齐 Qt::DropAction）。
 */
int XAbstractItemView_defaultDropAction(const XAbstractItemView* self);
/**
 * @brief 设置是否显示拖放指示器（对标 setDropIndicatorShown）。
 * @param self 目标视图指针。
 * @param enable true 显示（默认 true，与 Qt 一致）。
 * @return 无返回值。
 *
 * @note 指示器绘制不在本批范围，仅承载状态。
 */
void XAbstractItemView_setDropIndicatorShown(XAbstractItemView* self,
                                             bool enable);
/**
 * @brief 查询是否显示拖放指示器。
 * @param self 目标视图指针。
 * @return 显示返回 true。
 */
bool XAbstractItemView_showDropIndicator(const XAbstractItemView* self);

/* ==================== 尺寸提示（对标 QAbstractItemView） ==================== */

/**
 * @brief 计算列尺寸提示：遍历该列全部条目返回最大宽度
 *        （对标 sizeHintForColumn(int)）。
 * @param self 目标视图指针。
 * @param column 列号。
 * @return 最大条目宽度；视图为空、无模型或列号越界返回 -1（与 Qt 一致）。
 *
 * @note 基类基于 visualRect 网格几何统计，且 visualRect 当前非虚分派；
 *       派生视图如需精确尺寸提示应重写本函数。
 */
int XAbstractItemView_sizeHintForColumn(const XAbstractItemView* self,
                                        int column);
/**
 * @brief 计算行尺寸提示：遍历该行全部条目返回最大高度
 *        （对标 sizeHintForRow(int)）。
 * @param self 目标视图指针。
 * @param row 行号。
 * @return 最大条目高度；视图为空、无模型或行号越界返回 -1（与 Qt 一致）。
 *
 * @note 基类基于 visualRect 网格几何统计，且 visualRect 当前非虚分派；
 *       派生视图如需精确尺寸提示应重写本函数。
 */
int XAbstractItemView_sizeHintForRow(const XAbstractItemView* self, int row);
/**
 * @brief 单元格尺寸提示：返回该条目建议的宽高
 *        （对标 sizeHintForIndex(const QModelIndex&)）。
 *
 *        Qt 返回 QSize；本库以双 int 输出（项目惯例，宽/高分离承载）。
 *
 * @param self 目标视图指针。
 * @param row 行号。
 * @param column 列号。
 * @param outWidth 输出建议宽（可空）。
 * @param outHeight 输出建议高（可空）。
 * @return 计算成功返回 true；视图为空、行/列号 <0 或条目几何无效
 *         返回 false（输出置 0）。
 *
 * @note 基类基于 visualRect 固定网格几何（80x24）返回，且 visualRect
 *       当前非虚分派；派生视图如需精确尺寸提示应重写本函数。
 */
bool XAbstractItemView_sizeHintForIndex(const XAbstractItemView* self,
                                        int row, int column,
                                        int* outWidth, int* outHeight);

/* ==================== 滚动 ==================== */

/**
 * @brief 滚动到指定单元格并保证可见（对标 scrollTo(index, EnsureVisible)）。
 *
 *        基类实现：经垂直/水平滚动条按 visualRect 像素几何滚动；目标在
 *        可视区上方（IndexAbove）时对齐视口顶端，在下方（IndexBelow）时
 *        对齐视口底端。无滚动条或条目无效时为空操作。
 *
 * @param self 目标视图指针。
 * @param row 行号（<0 忽略）。
 * @param column 列号（<0 忽略）。
 * @return 无返回值。
 *
 * @note 滚动范围依赖内容尺寸（setContentSize）由派生视图/调用方维护；
 *       ScrollPerItem/ScrollPerPixel 模式差异仍为状态承载（本实现按像素）。
 */
void XAbstractItemView_scrollTo(XAbstractItemView* self, int row, int column);
/**
 * @brief 按提示定位滚动到指定单元格（对标 scrollTo(index, hint) 完整形态）。
 * @param self 目标视图指针。
 * @param row 行号（<0 忽略）。
 * @param column 列号（<0 忽略）。
 * @param hint XAbstractItemViewScrollHint 枚举（EnsureVisible/PositionAtTop/
 *             PositionAtBottom/PositionAtCenter）。
 * @return 无返回值。
 */
void XAbstractItemView_scrollToHint(XAbstractItemView* self, int row,
                                    int column, int hint);
/**
 * @brief 滚动到顶部（对标 scrollToTop()）。
 *
 *        将垂直滚动条 value 置为 minimum（setValue 内部收敛进
 *        [minimum, maximum]）；无垂直滚动条时为空操作。
 *
 * @param self 目标视图指针。
 * @return 无返回值。
 */
void XAbstractItemView_scrollToTop(XAbstractItemView* self);
/**
 * @brief 滚动到底部（对标 scrollToBottom()）。
 *
 *        将垂直滚动条 value 置为 maximum；无垂直滚动条时为空操作。
 *
 * @param self 目标视图指针。
 * @return 无返回值。
 */
void XAbstractItemView_scrollToBottom(XAbstractItemView* self);

/* ==================== 模型与选择（对标 QAbstractItemView） ==================== */

/** @brief 读取数据模型。 @param self 目标视图指针。 @return 模型借用指针；未设置 NULL。 */
XAbstractItemModel* XAbstractItemView_model(const XAbstractItemView* self);
/** @brief 设置数据模型（借用；视图重绘并同步选择模型维度）。
 * @param self 目标视图指针。
 * @param model 模型借用指针；可为 NULL（清空）。
 * @return 无返回值。
 */
void XAbstractItemView_setModel(XAbstractItemView* self,
                                XAbstractItemModel* model);
/** @brief 读取选择模型。 @param self 目标视图指针。 @return 选择模型借用指针。 */
XItemSelectionModel* XAbstractItemView_selectionModel(
    const XAbstractItemView* self);
/** @brief 设置选择模型（替换旧模型并接管所有权）。
 * @param self 目标视图指针。
 * @param selectionModel 选择模型；可为 NULL（重新懒创建）。
 * @return 无返回值。
 */
void XAbstractItemView_setSelectionModel(XAbstractItemView* self,
                                         XItemSelectionModel* selectionModel);
/**
 * @brief 清除全部选中（对标 clearSelection()）。
 *
 *        转发至选择模型的 clear；当前索引保持不变（同 Qt）。
 *
 * @param self 目标视图指针。
 * @return 无返回值（有选中变化时由选择模型发射 selectionChanged）。
 */
void XAbstractItemView_clearSelection(XAbstractItemView* self);
/**
 * @brief 全选（对标 selectAll()）。
 *
 *        语义：NoSelection 忽略；SingleSelection 仅选中当前项（无当前项
 *        时选 (0,0)，单选约束下的最大可选范围）；其余模式选中模型全部
 *        单元格（整行/整列行为下全选结果等同全网格）。
 *
 * @param self 目标视图指针。
 * @return 无返回值（由选择模型发射 selectionChanged）。
 */
void XAbstractItemView_selectAll(XAbstractItemView* self);
/** @brief 读取根索引行（预留树）。 @param self 目标视图指针。 @return 根行；-1=根。 */
int XAbstractItemView_rootRow(const XAbstractItemView* self);
/** @brief 读取根索引列。 @param self 目标视图指针。 @return 根列；-1=根。 */
int XAbstractItemView_rootColumn(const XAbstractItemView* self);
/** @brief 设置根索引（预留树；当前扁平模型仅存根偏移）。
 * @param self 目标视图指针。
 * @param row 根行；-1=根。
 * @param col 根列；-1=根。
 * @return 无返回值。
 */
void XAbstractItemView_setRootIndex(XAbstractItemView* self, int row, int col);
/**
 * @brief 查询根索引（对标 rootIndex()；本库以 (row,column) 平面承载）。
 *
 *        平铺模型无层级，根索引恒为 (0,0)（同 Qt 平铺模型下
 *        rootIndex() 覆盖全部顶层条目的语义）。
 *
 * @param self 目标视图指针。
 * @param outRow 输出根行号（可空；恒 0，self 为空置 0）。
 * @param outCol 输出根列号（可空；恒 0，self 为空置 0）。
 * @return self 非空返回 true；空指针返回 false。
 *
 * @note 树形层级未建：XAbstractItemView_setRootIndex 存储的根偏移为
 *       树预留状态（rootRow/rootColumn 查询），不影响本查询的恒
 *       (0,0) 结果；后续树形视图实现后改为返回存储的根索引。
 */
bool XAbstractItemView_rootIndex(const XAbstractItemView* self,
                                 int* outRow, int* outCol);
/** @brief 命中测试：视图坐标 → (row,col)（对标 indexAt；虚槽分派入口）。
 * @param self 目标视图指针。
 * @param x 视图坐标 X。
 * @param y 视图坐标 Y。
 * @param outRow 输出行号（未命中置 -1）。
 * @param outCol 输出列号（未命中置 -1）。
 * @return 命中返回 true。
 */
bool XAbstractItemView_indexAt_base(const XAbstractItemView* self, int x, int y,
                                    int* outRow, int* outCol);
/** @brief 条目几何：单元格 → 视图矩形（对标 visualRect；未实现时返回 false）。
 * @param self 目标视图指针。
 * @param row 行号。
 * @param col 列号。
 * @param out 输出矩形。
 * @return 计算成功返回 true。
 */
bool XAbstractItemView_visualRect(const XAbstractItemView* self,
                                  int row, int col, XRect* out);
/** @brief 设置图标尺寸（对标 setIconSize(QSize)）。
 * @param self 目标视图指针。
 * @param w 宽（像素）。
 * @param h 高（像素）。
 * @return 无返回值（变化时发射 iconSizeChanged）。
 */
void XAbstractItemView_setIconSize(XAbstractItemView* self, int w, int h);
/** @brief 查询图标宽。 @param self 目标视图指针。 @return 宽。 */
int XAbstractItemView_iconWidth(const XAbstractItemView* self);
/** @brief 查询图标高。 @param self 目标视图指针。 @return 高。 */
int XAbstractItemView_iconHeight(const XAbstractItemView* self);
/**
 * @brief 查询图标尺寸（双输出组合查询；对标 iconSize() 的 QSize 承载）。
 * @param self 目标视图指针。
 * @param outWidth 输出宽（可空；self 为空置 0）。
 * @param outHeight 输出高（可空；self 为空置 0）。
 * @return 无返回值。
 *
 * @note 组合 getter：与 iconWidth/iconHeight 单项查询同一字段
 *       （m_iconW/m_iconH，默认 16x16）；尺寸变化经 setIconSize
 *       发射 iconSizeChanged。
 */
void XAbstractItemView_iconSize(const XAbstractItemView* self,
                                int* outWidth, int* outHeight);

/* ==================== 信号（对标 QAbstractItemView） ==================== */

/** @brief pressed(row,col) 信号（按下时发射）。 */
void* XAbstractItemView_pressed_signal(XAbstractItemView* self,
                                       int row, int col);
/** @brief clicked(row,col) 信号（点击释放时发射）。 */
void* XAbstractItemView_clicked_signal(XAbstractItemView* self,
                                       int row, int col);
/** @brief doubleClicked(row,col) 信号（双击时发射）。 */
void* XAbstractItemView_doubleClicked_signal(XAbstractItemView* self,
                                             int row, int col);
/** @brief activated(row,col) 信号（激活时发射；单击释放或当前项上按下 Enter/Return）。 */
void* XAbstractItemView_activated_signal(XAbstractItemView* self,
                                         int row, int col);
/** @brief entered(row,col) 信号（悬停进入时发射）。 */
void* XAbstractItemView_entered_signal(XAbstractItemView* self,
                                       int row, int col);
/** @brief viewportEntered() 信号（悬停进入视口时发射）。 */
void* XAbstractItemView_viewportEntered_signal(XAbstractItemView* self);
/** @brief iconSizeChanged(int,int) 信号（图标尺寸变化时发射）。 */
void* XAbstractItemView_iconSizeChanged_signal(XAbstractItemView* self,
                                               int width, int height);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTITEMVIEW_H */
