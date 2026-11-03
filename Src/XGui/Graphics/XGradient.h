/**
 * @file       XGradient.h
 * @brief      XGradient 渐变基类（对标 Qt 6.8 QGradient）。
 * @details    定义渐变的公共属性：类型、扩散方式、坐标模式、插值模式与
 *             停止点集合。XLinearGradient / XRadialGradient /
 *             XConicalGradient 以 m_base 组合继承本类。
 *
 *             本类同时提供与绘制层的桥接：XGradient_toPainterGradient
 *             把公共描述转成 XPainterGradient，供 XPainter 直接消费。
 * @note       模块总开关 XGUI_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRADIENT_H
#define XGRADIENT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XGeometry.h"
#include "XPainter.h"

/** @brief 渐变停止点最大数量（与绘制层 XPainterGradient 容量一致）。 */
#define XGRADIENT_MAX_STOPS XPAINTER_GRADIENT_MAX_STOPS

/**
 * @brief      渐变类型（对标 Qt 6.8 QGradient::Type，数值一致）。
 */
typedef enum XGradient_Type
{
    XGradient_Type_Linear = 0,   /**< 线性渐变（对标 LinearGradient）。 */
    XGradient_Type_Radial = 1,   /**< 径向渐变（对标 RadialGradient）。 */
    XGradient_Type_Conical = 2,  /**< 锥形渐变（对标 ConicalGradient）。 */
    XGradient_Type_NoGradient = 3 /**< 无渐变（对标 NoGradient）。 */
} XGradient_Type;

/**
 * @brief      扩散方式（对标 Qt 6.8 QGradient::Spread，数值一致）。
 * @details    控制渐变区之外的填充方式；可按位不组合。
 */
typedef enum XGradient_Spread
{
    XGradient_Spread_Pad = 0,     /**< 边界外取端点色（对标 PadSpread）。 */
    XGradient_Spread_Reflect = 1, /**< 边界外镜像重复（对标 ReflectSpread）。 */
    XGradient_Spread_Repeat = 2   /**< 边界外平铺重复（对标 RepeatSpread）。 */
} XGradient_Spread;

/**
 * @brief      坐标模式（对标 Qt 6.8 QGradient::CoordinateMode，数值一致）。
 */
typedef enum XGradient_CoordinateMode
{
    XGradient_CoordinateMode_Logical = 0,    /**< 逻辑坐标（对标 LogicalMode）。 */
    XGradient_CoordinateMode_StretchToDevice = 1, /**< 拉伸到设备（对标 StretchToDeviceMode）。 */
    XGradient_CoordinateMode_ObjectBounding = 2,  /**< 对象包围盒（对标 ObjectBoundingMode）。 */
    XGradient_CoordinateMode_Object = 3       /**< 对象坐标（对标 ObjectMode）。 */
} XGradient_CoordinateMode;

/**
 * @brief      插值模式（对标 Qt 6.8 QGradient::InterpolationMode，数值一致）。
 */
typedef enum XGradient_InterpolationMode
{
    XGradient_InterpolationMode_Color = 0,     /**< 颜色空间插值（对标 ColorInterpolation）。 */
    XGradient_InterpolationMode_Component = 1  /**< 分量插值（对标 ComponentInterpolation）。 */
} XGradient_InterpolationMode;

/** @brief 渐变停止点（对标 QGradientStop 的 QPair<qreal,QColor>）。 */
typedef struct XGradientStop
{
    double m_position;   /**< 位置 0.0~1.0。 */
    uint32_t m_color;    /**< ARGB 颜色。 */
} XGradientStop;

XCLASS_DEFINE_BEGING(XGradient)
XCLASS_DEFINE_EXTEND_END(XGradient, XClass)

/** @brief 渐变基类（对标 QGradient）。 */
typedef struct XGradient
{
    XClass m_class;                            /**< 基类成员；必须是第一个。 */
    XGradientStop m_stops[XGRADIENT_MAX_STOPS];/**< 停止点表（对象拥有；定长）。 */
    int m_stopCount;                           /**< 有效停止点数。 */
    int m_type;                                /**< XGradient_Type。 */
    int m_spread;                              /**< XGradient_Spread。 */
    int m_coordinateMode;                      /**< XGradient_CoordinateMode。 */
    int m_interpolationMode;                   /**< XGradient_InterpolationMode。 */
} XGradient;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XGradient_class_init(void);
/** @brief 初始化嵌入式渐变（类型置 NoGradient，无停止点）。 @param self 目标渐变指针。 @return 无返回值。 */
void XGradient_init(XGradient* self);
/** @brief 堆上创建渐变。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XGradient* XGradient_create_ex(XMemoryType memory);
#define XGradient_create() XGradient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 公共属性（对标 QGradient） ==================== */

/** @brief 查询渐变类型（对标 type）。 @param self 目标渐变指针。 @return XGradient_Type。 */
int XGradient_type(const XGradient* self);
/** @brief 查询扩散方式（对标 spread）。 @param self 目标渐变指针。 @return XGradient_Spread。 */
int XGradient_spread(const XGradient* self);
/** @brief 查询坐标模式（对标 coordinateMode）。 @param self 目标渐变指针。 @return XGradient_CoordinateMode。 */
int XGradient_coordinateMode(const XGradient* self);
/** @brief 设置坐标模式（对标 setCoordinateMode）。 @param self 目标渐变指针。 @param mode 坐标模式。 @return 无返回值。 */
void XGradient_setCoordinateMode(XGradient* self, int mode);
/** @brief 查询插值模式（对标 interpolationMode）。 @param self 目标渐变指针。 @return XGradient_InterpolationMode。 */
int XGradient_interpolationMode(const XGradient* self);
/** @brief 设置插值模式（对标 setInterpolationMode）。 @param self 目标渐变指针。 @param mode 插值模式。 @return 无返回值。 */
void XGradient_setInterpolationMode(XGradient* self, int mode);

/* ==================== 停止点（对标 QGradient） ==================== */

/**
 * @brief 设置某位置的停止色（对标 setColorAt）。
 * @details 已存在同位置则覆盖其颜色；否则按位置升序插入。表满时忽略。
 * @param self 目标渐变指针。
 * @param pos 位置（0..1，越界钳位）。
 * @param color ARGB 颜色。
 * @return 无返回值。
 */
void XGradient_setColorAt(XGradient* self, double pos, uint32_t color);

/**
 * @brief 批量设置停止点（对标 setStops）。
 * @param self 目标渐变指针。
 * @param stops 停止点数组（借用，不转移所有权）；NULL 清空。
 * @param count 数量（超出 XGRADIENT_MAX_STOPS 部分丢弃）。
 * @return 无返回值。
 */
void XGradient_setStops(XGradient* self, const XGradientStop* stops, int count);

/** @brief 查询停止点数量（对标 stops().size()）。 @param self 目标渐变指针。 @return 数量。 */
int XGradient_stopCount(const XGradient* self);

/**
 * @brief 按下标读取停止点（对标 stops().at()）。
 * @param self 目标渐变指针。
 * @param index 下标。
 * @param out 输出停止点；调用方提供存储。
 * @return 越界返回 false；成功返回 true。
 */
bool XGradient_stopAt(const XGradient* self, int index, XGradientStop* out);

/**
 * @brief 清空全部停止点。
 * @param self 目标渐变指针。
 * @return 无返回值。
 */
void XGradient_clearStops(XGradient* self);

/* ==================== 绘制层桥接 ==================== */

/**
 * @brief 转换为绘制层渐变描述（供 XPainter_setBrushGradient 消费）。
 * @details 按 m_type 分派；线性/径向/锥形的几何参数由具体派生类提供，
 *          本基类只能填充公共部分（类型 + 停止点）。派生类（如
 *          XLinearGradient）提供带几何参数的版本以完整转换。
 * @param self 目标渐变指针。
 * @param out 输出绘制层渐变；调用方提供存储。
 * @return 成功返回 true；self 或 out 为空返回 false。
 */
bool XGradient_toPainterGradient(const XGradient* self, XPainterGradient* out);

/**
 * @brief 把停止点写入绘制层渐变（内部辅助；供派生类复用）。
 * @param self 目标渐变指针。
 * @param out 目标绘制层渐变；调用方提供存储。
 * @return 无返回值。
 */
void XGradient_applyStopsToPainter(const XGradient* self,
                                   XPainterGradient* out);

/* ==================== 比较 ==================== */

/**
 * @brief 判断两个渐变是否等价（对标 operator==）。
 * @details 比较类型/扩散/坐标模式/插值模式与全部停止点。
 * @param a 左侧渐变。
 * @param b 右侧渐变。
 * @return 等价返回 true；任一为 NULL 返回 false。
 */
bool XGradient_equals(const XGradient* a, const XGradient* b);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XGRADIENT_H */
