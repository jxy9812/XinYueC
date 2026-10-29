/**
 * @file       XLongPressGestureRecognizer.h
 * @brief      长按手势识别器公开 API（对标 Qt 6.8 QTapAndHoldGesture）。
 * @details    按住不放达长按时长即触发（长按）；按压期间位移超过容差即
 *             取消。对标 Qt 的 700ms 超时与 40px 曼哈顿容差。
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XLONGPRESSGESTURERECOGNIZER_H
#define XLONGPRESSGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGestureRecognizer.h"

#if XGESTURE_ON

XCLASS_DEFINE_BEGING(XLongPressGestureRecognizer)
XCLASS_DEFINE_EXTEND_END(XLongPressGestureRecognizer, XGestureRecognizer)

/**
 * @brief      长按手势识别器；m_base 必须是第一个成员。
 */
typedef struct XLongPressGestureRecognizer
{
    XGestureRecognizer m_base; /**< 基类成员；必须是第一个。 */
    int m_duration;             /**< 长按时长（ms；默认 700，对标 Qt
                                   QTapAndHoldGesture 的 700ms）。 */
    int m_maxDistance;          /**< 按压期间允许的最大位移（px，曼哈顿；
                                   默认 40，对标 Qt）。 */
    int m_pressX;               /**< 按压起点 x（控件局部坐标）。 */
    int m_pressY;               /**< 按压起点 y（控件局部坐标）。 */
    int64_t m_pressMs;          /**< 按压时刻（手势时钟域毫秒）。 */
    bool m_fired;               /**< 长按已触发（本次序列只触发一次）。 */
} XLongPressGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XLongPressGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self 识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL。
 */
void XLongPressGestureRecognizer_init(XLongPressGestureRecognizer* self,
                                      XObject* parent);
/**
 * @brief 创建长按手势识别器（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XLongPressGestureRecognizer* XLongPressGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XLongPressGestureRecognizer_create(...)     XLongPressGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 设置长按时长（ms）。
 * @param self 识别器；NULL 无操作。
 * @param duration 时长毫秒（负值按 0 处理）。
 * @return 无返回值。
 */
void XLongPressGestureRecognizer_setDuration(
    XLongPressGestureRecognizer* self, int duration);
/**
 * @brief 设置按压期间允许的最大位移（px，曼哈顿距离）。
 * @param self 识别器；NULL 无操作。
 * @param maxDistance 容差像素（负值按 0 处理）。
 * @return 无返回值。
 */
void XLongPressGestureRecognizer_setMaxDistance(
    XLongPressGestureRecognizer* self, int maxDistance);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XLONGPRESSGESTURERECOGNIZER_H */
