#ifndef XABSTRACTITEMVIEW_H
#define XABSTRACTITEMVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractScrollArea.h"

#if XTABLEWIDGET_ON || 1

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

XCLASS_DEFINE_BEGING(XAbstractItemView)
XCLASS_DEFINE_EXTEND_END(XAbstractItemView, XAbstractScrollArea)

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

/** @brief 设置当前单元格。 @param self 目标视图指针。 @param row 行号。 @param column 列号。 @return 无返回值。 */
void XAbstractItemView_setCurrentIndex(XAbstractItemView* self, int row,
                                       int column);
/** @brief 查询当前行。 @param self 目标视图指针。 @return 行号；无当前项返回 -1。 */
int XAbstractItemView_currentRow(const XAbstractItemView* self);
/** @brief 查询当前列。 @param self 目标视图指针。 @return 列号；无当前项返回 -1。 */
int XAbstractItemView_currentColumn(const XAbstractItemView* self);

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

/* ==================== 滚动 ==================== */

/** @brief 滚动到指定单元格（对标 QAbstractItemView::scrollTo）。 @param self 目标视图指针。 @param row 行号。 @param column 列号。 @return 无返回值。 */
void XAbstractItemView_scrollTo(XAbstractItemView* self, int row, int column);

#endif /* XTABLEWIDGET_ON || 1 */
#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTITEMVIEW_H */
