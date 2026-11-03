/**
 * @file       XBoxSet.h
 * @brief      XBoxSet 盒须图数据组（对标 Qt Charts 6.8 QBoxSet）。
 * @details    一个盒须组固定 5 个值：下极限、下四分位、中位数、上四分位、
 *             上极限（顺序由 XBoxSet_ValuePositions 定义，数值与 Qt 一致）。
 *             Qt 的 QPen/QBrush 在本库以「颜色 + 线宽」参数化承载。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBOXSET_H
#define XBOXSET_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"

#if XCHARTS_ON

/**
 * @brief      盒须组取值位置（对标 Qt Charts 6.8 QBoxSet::ValuePositions，
 *             数值一致；共 5 个固定槽位，不可按位组合）。
 */
typedef enum XBoxSet_ValuePositions
{
    XBoxSet_ValuePositions_LowerExtreme = 0,  /**< 下极限（对标 LowerExtreme）。 */
    XBoxSet_ValuePositions_LowerQuartile = 1, /**< 下四分位（对标 LowerQuartile）。 */
    XBoxSet_ValuePositions_Median = 2,        /**< 中位数（对标 Median）。 */
    XBoxSet_ValuePositions_UpperQuartile = 3, /**< 上四分位（对标 UpperQuartile）。 */
    XBoxSet_ValuePositions_UpperExtreme = 4   /**< 上极限（对标 UpperExtreme）。 */
} XBoxSet_ValuePositions;

/** @brief 盒须组固定值个数（对标 Qt 的 5 槽位）。 */
#define XBOXSET_VALUE_COUNT 5

XCLASS_DEFINE_BEGING(XBoxSet)
XCLASS_DEFINE_EXTEND_END(XBoxSet, XObject)

/**
 * @brief 盒须组对象（对标 QBoxSet）。
 *
 *        继承 XObject 以支持交互信号；m_values 为 5 槽位定长数组，
 *        未赋值的槽位保持 0。
 */
typedef struct XBoxSet
{
    XObject m_base;            /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XString* m_label;          /**< 组标签（对象拥有；默认空）。 */
    double m_values[XBOXSET_VALUE_COUNT]; /**< 5 槽位取值（下标见 ValuePositions）。 */
    double m_penWidth;         /**< 画笔线宽（像素；默认 1）。 */

    /* ---- 4 字节对齐组 ---- */
    uint32_t m_penColor;       /**< 画笔颜色（ARGB；0=主题默认）。 */
    uint32_t m_brushColor;     /**< 画刷填充颜色（ARGB；0=主题默认）。 */

    /* ---- 1 字节窄整型组 ---- */
    uint8_t m_valueCount;      /**< 已赋值槽位数（0..5）。 */
} XBoxSet;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XBoxSet_class_init(void);

/**
 * @brief 初始化嵌入式盒须组（空标签、全 0 值）。
 * @param self 目标盒须组指针，不能为空。
 * @return 无返回值。
 */
void XBoxSet_init(XBoxSet* self);

/**
 * @brief 初始化并设置标签（XString 主版本；对标 QBoxSet(label)）。
 * @param self 目标盒须组指针。
 * @param label 借用 XString*；可为 NULL（空标签）。
 * @return 无返回值。
 */
void XBoxSet_init_ex(XBoxSet* self, const XString* label);

/**
 * @brief 初始化并一次性设置 5 个值（对标 QBoxSet(le,lq,m,uq,ue,label)）。
 * @param self 目标盒须组指针。
 * @param le 下极限。
 * @param lq 下四分位。
 * @param m 中位数。
 * @param uq 上四分位。
 * @param ue 上极限。
 * @param label 借用 XString*；可为 NULL。
 * @return 无返回值。
 */
void XBoxSet_init_ex_5(XBoxSet* self, double le, double lq, double m, double uq,
                       double ue, const XString* label);

/**
 * @brief 堆上创建盒须组（对标 QBoxSet 构造）。
 * @param memory 内存类型。
 * @param label 借用 XString*；可为 NULL。
 * @return 盒须组指针；分配失败返回 NULL。
 */
XBoxSet* XBoxSet_create_ex(XMemoryType memory, const XString* label);
/** @brief 堆上创建盒须组（UTF-8 标签重载）。 @param memory 内存类型。 @param label UTF-8 标签。 @return 新建对象；失败 NULL。 */
XBoxSet* XBoxSet_create_ex_2(XMemoryType memory, const char* label);
#define XBoxSet_create(label) XBoxSet_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, label)

/* ==================== 标签（对标 QBoxSet） ==================== */

/** @brief 设置组标签（XString 主版本；对标 setLabel）。 @param self 目标盒须组指针。 @param label 借用 XString*；NULL 清空。 @return 无返回值。 */
void XBoxSet_setLabel(XBoxSet* self, const XString* label);
/** @brief 设置组标签（UTF-8 编码版本）。 @param self 目标盒须组指针。 @param label UTF-8 标签（借用）。 @return 无返回值。 */
void XBoxSet_setLabel_utf8(XBoxSet* self, const char* label);
/** @brief 设置组标签（UTF-8 兼容别名）。 @param self 目标盒须组指针。 @param label UTF-8 标签（借用）。 @return 无返回值。 */
void XBoxSet_setLabel_2(XBoxSet* self, const char* label);
/** @brief 查询组标签（借用；对标 label）。 @param self 目标盒须组指针。 @return 借用 XString*；调用者不得释放。 */
const XString* XBoxSet_label_const(const XBoxSet* self);
/** @brief 查询组标签（兼容别名）。 @param self 目标盒须组指针。 @return 借用 XString*。 */
const XString* XBoxSet_label(const XBoxSet* self);
/** @brief 查询组标签（UTF-8 借用视图）。 @param self 目标盒须组指针。 @return UTF-8 串。 */
const char* XBoxSet_label_toUtf8(const XBoxSet* self);

/* ==================== 取值（对标 QBoxSet） ==================== */

/** @brief 设置指定槽位取值（对标 setValue）。
 * @param self 目标盒须组指针。
 * @param index 槽位（0..4，见 XBoxSet_ValuePositions；越界为空操作）。
 * @param value 取值。
 * @return 无返回值（变化时发射 valueChanged）。
 */
void XBoxSet_setValue(XBoxSet* self, int index, double value);
/** @brief 读取指定槽位取值（对标 at / operator[]）。
 * @param self 目标盒须组指针。
 * @param index 槽位（0..4）。
 * @return 取值；越界返回 0。
 */
double XBoxSet_at(const XBoxSet* self, int index);
/** @brief 清空全部取值（对标 clear）。 @param self 目标盒须组指针。 @return 无返回值。 */
void XBoxSet_clear(XBoxSet* self);
/** @brief 查询已赋值槽位数（对标 count）。 @param self 目标盒须组指针。 @return 槽位数（0..5）。 */
int XBoxSet_count(const XBoxSet* self);

/* ==================== 画笔 / 画刷（对标 QBoxSet） ==================== */

/** @brief 设置画笔（颜色 + 线宽；对标 setPen）。 @param self 目标盒须组指针。 @param color ARGB。 @param width 线宽（像素）。 @return 无返回值。 */
void XBoxSet_setPen(XBoxSet* self, uint32_t color, double width);
/** @brief 读取画笔（参数化；对标 pen）。 @param self 目标盒须组指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XBoxSet_pen(const XBoxSet* self, uint32_t* color, double* width);
/** @brief 设置画刷颜色（对标 setBrush）。 @param self 目标盒须组指针。 @param color ARGB。 @return 无返回值。 */
void XBoxSet_setBrush(XBoxSet* self, uint32_t color);
/** @brief 读取画刷颜色（对标 brush）。 @param self 目标盒须组指针。 @return ARGB。 */
uint32_t XBoxSet_brush(const XBoxSet* self);

/* ==================== 信号（对标 QBoxSet Q_SIGNALS） ==================== */

/** @brief clicked 信号（无载荷）。 */
void* XBoxSet_clicked_signal(XBoxSet* self);
/** @brief hovered 信号（载荷：状态）。 */
void* XBoxSet_hovered_signal(XBoxSet* self, bool status);
/** @brief pressed 信号（无载荷）。 */
void* XBoxSet_pressed_signal(XBoxSet* self);
/** @brief released 信号（无载荷）。 */
void* XBoxSet_released_signal(XBoxSet* self);
/** @brief doubleClicked 信号（无载荷）。 */
void* XBoxSet_doubleClicked_signal(XBoxSet* self);
/** @brief penChanged 信号（载荷：颜色, 线宽）。 */
void* XBoxSet_penChanged_signal(XBoxSet* self, uint32_t color, double width);
/** @brief brushChanged 信号（载荷：颜色）。 */
void* XBoxSet_brushChanged_signal(XBoxSet* self, uint32_t color);
/** @brief valuesChanged 信号（无载荷）。 */
void* XBoxSet_valuesChanged_signal(XBoxSet* self);
/** @brief valueChanged 信号（载荷：槽位下标）。 */
void* XBoxSet_valueChanged_signal(XBoxSet* self, int index);
/** @brief cleared 信号（无载荷）。 */
void* XBoxSet_cleared_signal(XBoxSet* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XBOXSET_H */
