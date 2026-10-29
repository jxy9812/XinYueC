/**
 * @file       XFlingGestureRecognizer.h
 * @brief      甩动手势识别器公开 API（框架扩展；Qt 无独立甩动手势类，
 *             由滚动体系自持惯性）。
 * @details    抬手瞬间的速度判定：按压期间位移超过最小距离、且抬手速度
 *             高于速度阈值即判为甩动，并给出速度与方向。消费方（滚动
 *             区/画布）据此接管后续惯性滑行。
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XFLINGGESTURERECOGNIZER_H
#define XFLINGGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGestureRecognizer.h"

#if XGESTURE_ON

XCLASS_DEFINE_BEGING(XFlingGestureRecognizer)
XCLASS_DEFINE_EXTEND_END(XFlingGestureRecognizer, XGestureRecognizer)

/**
 * @brief      甩动手势识别器；m_base 必须是第一个成员。
 */
typedef struct XFlingGestureRecognizer
{
    XGestureRecognizer m_base; /**< 基类成员；必须是第一个。 */
    int m_minDistance;          /**< 最小位移（px；默认 20）。 */
    int m_minVelocity;          /**< 最小抬手速度（px/s；默认 500）。 */
    int m_pressX;               /**< 按压起点 x（控件局部坐标）。 */
    int m_pressY;               /**< 按压起点 y（控件局部坐标）。 */
    int64_t m_pressMs;          /**< 按压时刻（手势时钟域毫秒）。 */
} XFlingGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XFlingGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self 识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL。
 */
void XFlingGestureRecognizer_init(XFlingGestureRecognizer* self,
                                  XObject* parent);
/**
 * @brief 创建甩动手势识别器（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XFlingGestureRecognizer* XFlingGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XFlingGestureRecognizer_create(...) \
    XFlingGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 设置最小位移阈值（px）。
 * @param self 识别器；NULL 无操作。
 * @param minDistance 阈值像素（负值按 0 处理）。
 * @return 无返回值。
 */
void XFlingGestureRecognizer_setMinDistance(XFlingGestureRecognizer* self,
                                            int minDistance);
/**
 * @brief 设置最小抬手速度（px/s）。
 * @param self 识别器；NULL 无操作。
 * @param minVelocity 速度阈值（负值按 0 处理）。
 * @return 无返回值。
 */
void XFlingGestureRecognizer_setMinVelocity(XFlingGestureRecognizer* self,
                                            int minVelocity);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XFLINGGESTURERECOGNIZER_H */
