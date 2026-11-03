/**
 * @file       XLinearGradient.h
 * @brief      XLinearGradient 线性渐变（对标 Qt 6.8 QLinearGradient）。
 * @details    继承 XGradient（对应 QLinearGradient : QGradient）；在基类的
 *             停止点/扩散/坐标模式之上，持有渐变的起点与终点。
 * @note       模块总开关 XGUI_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLINEARGRADIENT_H
#define XLINEARGRADIENT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGradient.h"

XCLASS_DEFINE_BEGING(XLinearGradient)
XCLASS_DEFINE_EXTEND_END(XLinearGradient, XGradient)

/** @brief 线性渐变（m_base 必须是第一个成员；对标 QLinearGradient）。 */
typedef struct XLinearGradient
{
    XGradient m_base;   /**< 基类成员；必须是第一个（停止点/扩散/坐标模式）。 */
    XPointF m_start;    /**< 渐变起点（对标 QLinearGradient::start）。 */
    XPointF m_finalStop;/**< 渐变终点（对标 QLinearGradient::finalStop）。 */
} XLinearGradient;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XLinearGradient_class_init(void);

/**
 * @brief 初始化嵌入式线性渐变（起点/终点为 (0,0)/(1,0)，与 Qt 默认一致）。
 * @param self 目标渐变指针。
 * @return 无返回值。
 */
void XLinearGradient_init(XLinearGradient* self);

/**
 * @brief 以起点/终点初始化（对标 QLinearGradient(start, finalStop)）。
 * @param self 目标渐变指针。
 * @param x1 起点 X。
 * @param y1 起点 Y。
 * @param x2 终点 X。
 * @param y2 终点 Y。
 * @return 无返回值。
 */
void XLinearGradient_init_ex(XLinearGradient* self, double x1, double y1,
                             double x2, double y2);

/**
 * @brief 堆上创建线性渐变。
 * @param memory 内存类型。
 * @return 新建对象；失败返回 NULL。
 */
XLinearGradient* XLinearGradient_create_ex(XMemoryType memory);
#define XLinearGradient_create() \
    XLinearGradient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief 堆上创建线性渐变并设置起止点（对标 QLinearGradient(x1,y1,x2,y2)）。
 * @param memory 内存类型。
 * @param x1 起点 X。
 * @param y1 起点 Y。
 * @param x2 终点 X。
 * @param y2 终点 Y。
 * @return 新建对象；失败返回 NULL。
 */
XLinearGradient* XLinearGradient_create_ex_2(XMemoryType memory, double x1,
                                             double y1, double x2, double y2);

/* ==================== 几何（对标 QLinearGradient） ==================== */

/** @brief 设置起点（对标 setStart）。 @param self 目标渐变指针。 @param x 起点 X。 @param y 起点 Y。 @return 无返回值。 */
void XLinearGradient_setStart(XLinearGradient* self, double x, double y);
/** @brief 读取起点（对标 start）。 @param self 目标渐变指针。 @return 起点坐标（按值）。 */
XPointF XLinearGradient_start(const XLinearGradient* self);
/** @brief 设置终点（对标 setFinalStop）。 @param self 目标渐变指针。 @param x 终点 X。 @param y 终点 Y。 @return 无返回值。 */
void XLinearGradient_setFinalStop(XLinearGradient* self, double x, double y);
/** @brief 读取终点（对标 finalStop）。 @param self 目标渐变指针。 @return 终点坐标（按值）。 */
XPointF XLinearGradient_finalStop(const XLinearGradient* self);

/* ==================== 绘制层桥接 ==================== */

/**
 * @brief 转换为绘制层渐变描述（含起止几何 + 停止点）。
 * @param self 目标渐变指针。
 * @param out 输出绘制层渐变；调用方提供存储。
 * @return 成功返回 true；self 或 out 为空返回 false。
 */
bool XLinearGradient_toPainterGradient(const XLinearGradient* self,
                                       XPainterGradient* out);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XLINEARGRADIENT_H */
