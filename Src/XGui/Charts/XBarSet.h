/**
 * @file       XBarSet.h
 * @brief      XBarSet 柱组对象（对标 Qt Charts 6.8 QBarSet）。
 * @details    一组柱 = 一个标签 + 一组数值（每类别一个值）；多组柱并列
 *             成组显示。QPen/QBrush/QFont 以 C 参数化颜色/线宽/字体族
 *             承载：pen=画笔（边框颜色+线宽）、brush=画刷（填充颜色）、
 *             labelBrush=标签画刷颜色、labelFont=标签字体族+字号；
 *             颜色统一 uint32_t ARGB，0=主题默认（透明画刷语义）。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBARSET_H
#define XBARSET_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XBarSet)
XCLASS_DEFINE_EXTEND_END(XBarSet, XObject)

/**
 * @brief 柱组对象（对标 QBarSet）。
 *
 *        继承 XObject 以支持属性/数据/交互信号；m_values 为一组柱的
 *        数值数组（每类别一个值，缺失值按 0 渲染），m_selectedBars 为
 *        选中下标集合（升序数组，对象拥有）。
 */
typedef struct XBarSet
{
    XObject m_base;               /**< 基类成员；必须是第一个。 */
    XString* m_label;             /**< 柱组标签（对象拥有；默认空）。 */
    double* m_values;             /**< 数值数组（堆；默认空）。 */
    int m_count;                  /**< 数值个数。 */
    int m_capacity;               /**< 数值容量。 */
    int* m_selectedBars;          /**< 选中下标集合（升序；堆；默认空）。 */
    int m_selectedCount;          /**< 选中个数。 */
    int m_selectedCapacity;       /**< 选中集合容量。 */
    uint32_t m_penColor;          /**< 画笔颜色（ARGB；0=主题默认）。 */
    double m_penWidth;            /**< 画笔线宽（像素；默认 2）。 */
    uint32_t m_brushColor;        /**< 画刷填充颜色（ARGB；0=主题默认）。 */
    uint32_t m_labelBrushColor;   /**< 标签画刷颜色（ARGB；0=主题默认）。 */
    XString* m_labelFontFamily;   /**< 标签字体族（对象拥有；默认空）。 */
    int m_labelFontSize;          /**< 标签字号（磅；0=主题默认）。 */
    uint32_t m_selectedColor;     /**< 选中柱填充颜色（ARGB；0=用 color）。 */
} XBarSet;

XVtable* XBarSet_class_init(void);

/**
 * @brief 初始化嵌入式柱组（空标签、空数值、未选中）。
 *
 * @param self 目标柱组指针，不能为空。
 * @return 无返回值。
 */
void XBarSet_init(XBarSet* self);

/**
 * @brief 初始化嵌入式柱组并设置标签（XString 主版本；对标 QBarSet(label)）。
 *
 * @param self  目标柱组指针。
 * @param label 借用 XString*；可为 NULL（空标签）。
 * @return 无返回值。
 */
void XBarSet_init_ex(XBarSet* self, const XString* label);
/** @brief 初始化嵌入式柱组并设置标签（UTF-8 兼容重载，转发主版本）。 */
void XBarSet_init_ex_2(XBarSet* self, const char* label);

/**
 * @brief 堆上创建柱组（XString 主版本；对标 QBarSet(label)）。
 *
 * @param memory 内存类型。
 * @param label  借用 XString*；可为 NULL（空标签）。
 * @return 柱组指针；分配失败返回 NULL。
 */
XBarSet* XBarSet_create_ex(XMemoryType memory, const XString* label);
/** @brief 堆上创建柱组（UTF-8 兼容重载，转发主版本）。 */
XBarSet* XBarSet_create_ex_2(XMemoryType memory, const char* label);
#define XBarSet_create(label)     XBarSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, label)

/** @brief 析构入口（查表分派父类析构）。 */
#define XBarSet_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上柱组（查表分派析构并释放内存）。 */
#define XBarSet_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 标签（对标 QBarSet） ==================== */

/**
 * @brief 设置柱组标签（XString 主版本；对标 QBarSet::setLabel）。
 *
 * @param self  目标柱组指针。
 * @param label 借用 XString*；可为 NULL（清空）。
 * @return 无返回值。
 */
void XBarSet_setLabel(XBarSet* self, const XString* label);
/** @brief 设置柱组标签（UTF-8 兼容重载，转发主版本）。 */
void XBarSet_setLabel_2(XBarSet* self, const char* label);
/** @brief 读取柱组标签（内部借用 XString*；不得释放）。 */
const XString* XBarSet_label(const XBarSet* self);
/** @brief 读取柱组标签（UTF-8 借用；未设置返回空串）。 */
const char* XBarSet_label_2(const XBarSet* self);

/* ==================== 数值（对标 QBarSet values） ==================== */

/** @brief 追加一个数值。 @param self 目标柱组指针。 @param value 数值。 @return 无返回值。 */
void XBarSet_append(XBarSet* self, double value);
/** @brief 批量追加数值（对标 append(QList<qreal>)）。 @param self 目标柱组指针。 @param values 数值数组。 @param count 数量。 @return 无返回值。 */
void XBarSet_appendValues(XBarSet* self, const double* values, int count);
/** @brief 在指定位置插入数值。 @param self 目标柱组指针。 @param index 插入位置（越界钳位）。 @param value 数值。 @return 无返回值。 */
void XBarSet_insert(XBarSet* self, int index, double value);
/** @brief 移除一段数值（对标 remove(index, count)）。 @param self 目标柱组指针。 @param index 起始下标。 @param count 数量（默认 1；越界裁剪）。 @return 无返回值。 */
void XBarSet_remove(XBarSet* self, int index, int count);
/** @brief 按下标替换数值。 @param self 目标柱组指针。 @param index 下标。 @param value 新值。 @return 越界返回 false。 */
bool XBarSet_replace(XBarSet* self, int index, double value);
/** @brief 按下标读取数值。 @param self 目标柱组指针。 @param index 下标。 @return 数值；越界返回 0。 */
double XBarSet_at(const XBarSet* self, int index);
/** @brief 查询数值个数。 @param self 目标柱组指针。 @return 个数。 */
int XBarSet_count(const XBarSet* self);
/** @brief 数值求和。 @param self 目标柱组指针。 @return 总和。 */
double XBarSet_sum(const XBarSet* self);
/** @brief 批量读取数值快照（对标 QML values）。 @param self 目标柱组指针。 @param out 输出缓冲。 @param maxCount 容量。 @return 实际个数。 */
int XBarSet_values(const XBarSet* self, double* out, int maxCount);

/* ==================== 画笔/画刷/字体（C 参数化） ==================== */

/** @brief 设置画笔（颜色+线宽，对标 setPen(QPen)）。 @param self 目标柱组指针。 @param color 画笔颜色 ARGB。 @param width 线宽（像素）。 @return 无返回值。 */
void XBarSet_setPen(XBarSet* self, uint32_t color, double width);
/** @brief 读取画笔（参数化）。 @param self 目标柱组指针。 @param color 输出颜色。 @param width 输出线宽。 @return 无返回值。 */
void XBarSet_pen(const XBarSet* self, uint32_t* color, double* width);
/** @brief 设置画刷（填充颜色，对标 setBrush(QBrush)）。 @param self 目标柱组指针。 @param color ARGB（0=主题默认）。 @return 无返回值。 */
void XBarSet_setBrush(XBarSet* self, uint32_t color);
/** @brief 读取画刷颜色。 @param self 目标柱组指针。 @return ARGB。 */
uint32_t XBarSet_brush(const XBarSet* self);
/** @brief 设置标签画刷（颜色）。 @param self 目标柱组指针。 @param color ARGB。 @return 无返回值。 */
void XBarSet_setLabelBrush(XBarSet* self, uint32_t color);
/** @brief 读取标签画刷颜色。 @param self 目标柱组指针。 @return ARGB。 */
uint32_t XBarSet_labelBrush(const XBarSet* self);
/** @brief 设置标签字体（XString 主版本；对标 setLabelFont(QFont)）。
 * @param self 目标柱组指针。
 * @param family 借用 XString*；NULL 保持。
 * @param pointSize 字号（0 保持）。
 * @return 无返回值。 */
void XBarSet_setLabelFont(XBarSet* self, const XString* family,
                          int pointSize);
/** @brief 设置标签字体（UTF-8 兼容重载，转发主版本）。 */
void XBarSet_setLabelFont_2(XBarSet* self, const char* family,
                            int pointSize);
/** @brief 读取标签字体族（内部借用 XString*；不得释放）。 */
const XString* XBarSet_labelFont(const XBarSet* self);
/** @brief 读取标签字体族（UTF-8 借用）。 */
const char* XBarSet_labelFont_2(const XBarSet* self);
/** @brief 读取标签字号。 @param self 目标柱组指针。 @return 字号（磅）。 */
int XBarSet_labelFontSize(const XBarSet* self);

/* ==================== 颜色便捷（对标 QBarSet color 族） ==================== */

/** @brief 设置填充色（对标 setColor；等价改画刷颜色并发射 brushChanged+colorChanged）。 @param self 目标柱组指针。 @param color ARGB。 @return 无返回值。 */
void XBarSet_setColor(XBarSet* self, uint32_t color);
/** @brief 读取填充色。 @param self 目标柱组指针。 @return ARGB。 */
uint32_t XBarSet_color(const XBarSet* self);
/** @brief 设置边框色（对标 setBorderColor；等价改画笔颜色并发射 penChanged+borderColorChanged）。 @param self 目标柱组指针。 @param color ARGB。 @return 无返回值。 */
void XBarSet_setBorderColor(XBarSet* self, uint32_t color);
/** @brief 读取边框色。 @param self 目标柱组指针。 @return ARGB。 */
uint32_t XBarSet_borderColor(const XBarSet* self);
/** @brief 设置标签色（对标 setLabelColor）。 @param self 目标柱组指针。 @param color ARGB。 @return 无返回值。 */
void XBarSet_setLabelColor(XBarSet* self, uint32_t color);
/** @brief 读取标签色。 @param self 目标柱组指针。 @return ARGB。 */
uint32_t XBarSet_labelColor(const XBarSet* self);
/** @brief 设置选中柱颜色。 @param self 目标柱组指针。 @param color ARGB（0=用 color）。 @return 无返回值。 */
void XBarSet_setSelectedColor(XBarSet* self, uint32_t color);
/** @brief 读取选中柱颜色。 @param self 目标柱组指针。 @return ARGB。 */
uint32_t XBarSet_selectedColor(const XBarSet* self);

/* ==================== 选中状态（对标 QBarSet 6.2 选择 API） ==================== */

/** @brief 查询柱是否选中。 @param self 目标柱组指针。 @param index 下标。 @return 选中返回 true。 */
bool XBarSet_isBarSelected(const XBarSet* self, int index);
/** @brief 选中一柱。 @param self 目标柱组指针。 @param index 下标。 @return 无返回值。 */
void XBarSet_selectBar(XBarSet* self, int index);
/** @brief 取消选中一柱。 @param self 目标柱组指针。 @param index 下标。 @return 无返回值。 */
void XBarSet_deselectBar(XBarSet* self, int index);
/** @brief 设置柱选中状态。 @param self 目标柱组指针。 @param index 下标。 @param selected 是否选中。 @return 无返回值。 */
void XBarSet_setBarSelected(XBarSet* self, int index, bool selected);
/** @brief 全选。 @param self 目标柱组指针。 @return 无返回值。 */
void XBarSet_selectAllBars(XBarSet* self);
/** @brief 全不选。 @param self 目标柱组指针。 @return 无返回值。 */
void XBarSet_deselectAllBars(XBarSet* self);
/** @brief 批量选中。 @param self 目标柱组指针。 @param indexes 下标数组。 @param count 数量。 @return 无返回值。 */
void XBarSet_selectBars(XBarSet* self, const int* indexes, int count);
/** @brief 批量取消选中。 @param self 目标柱组指针。 @param indexes 下标数组。 @param count 数量。 @return 无返回值。 */
void XBarSet_deselectBars(XBarSet* self, const int* indexes, int count);
/** @brief 翻转选中状态。 @param self 目标柱组指针。 @param indexes 下标数组。 @param count 数量。 @return 无返回值。 */
void XBarSet_toggleSelection(XBarSet* self, const int* indexes, int count);
/** @brief 读取全部选中下标。 @param self 目标柱组指针。 @param out 输出缓冲。 @param maxCount 容量。 @return 选中数量。 */
int XBarSet_selectedBars(const XBarSet* self, int* out, int maxCount);

/* ==================== 信号（对标 QBarSet Q_SIGNALS） ==================== */

/** @brief clicked 信号地址（载荷：index）。 */
void* XBarSet_clicked_signal(XBarSet* self, int index);
/** @brief hovered 信号地址（载荷：状态, index）。 */
void* XBarSet_hovered_signal(XBarSet* self, bool status, int index);
/** @brief pressed 信号地址（载荷：index）。 */
void* XBarSet_pressed_signal(XBarSet* self, int index);
/** @brief released 信号地址（载荷：index）。 */
void* XBarSet_released_signal(XBarSet* self, int index);
/** @brief doubleClicked 信号地址（载荷：index）。 */
void* XBarSet_doubleClicked_signal(XBarSet* self, int index);
/** @brief labelChanged 信号地址（无载荷）。 */
void* XBarSet_labelChanged_signal(XBarSet* self);
/** @brief penChanged 信号地址（无载荷）。 */
void* XBarSet_penChanged_signal(XBarSet* self);
/** @brief brushChanged 信号地址（无载荷）。 */
void* XBarSet_brushChanged_signal(XBarSet* self);
/** @brief labelBrushChanged 信号地址（无载荷）。 */
void* XBarSet_labelBrushChanged_signal(XBarSet* self);
/** @brief labelFontChanged 信号地址（载荷：字体族, 字号）。 */
void* XBarSet_labelFontChanged_signal(XBarSet* self, const char* family,
                                      int pointSize);
/** @brief colorChanged 信号地址（载荷：颜色）。 */
void* XBarSet_colorChanged_signal(XBarSet* self, uint32_t color);
/** @brief borderColorChanged 信号地址（载荷：颜色）。 */
void* XBarSet_borderColorChanged_signal(XBarSet* self, uint32_t color);
/** @brief labelColorChanged 信号地址（载荷：颜色）。 */
void* XBarSet_labelColorChanged_signal(XBarSet* self, uint32_t color);
/** @brief selectedColorChanged 信号地址（载荷：颜色）。 */
void* XBarSet_selectedColorChanged_signal(XBarSet* self, uint32_t color);
/** @brief valuesAdded 信号地址（载荷：起始下标, 数量）。 */
void* XBarSet_valuesAdded_signal(XBarSet* self, int index, int count);
/** @brief valuesRemoved 信号地址（载荷：起始下标, 数量）。 */
void* XBarSet_valuesRemoved_signal(XBarSet* self, int index, int count);
/** @brief valueChanged 信号地址（载荷：下标）。 */
void* XBarSet_valueChanged_signal(XBarSet* self, int index);
/** @brief selectedBarsChanged 信号地址（载荷：选中下标数组, 数量）。 */
void* XBarSet_selectedBarsChanged_signal(XBarSet* self, const int* indexes,
                                         int count);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XBARSET_H */
