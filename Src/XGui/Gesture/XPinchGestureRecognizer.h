/**
 * @file       XPinchGestureRecognizer.h
 * @brief      双指缩放手势识别器公开 API（对标 Qt 6.8 QPinchGesture）。
 * @details    恰好两指的缩放判定：单步比例=当前两指间距/上一帧两指间距，
 *             累计比例自手势起始累乘（起始 1.0）。对标 Qt 的单步比值
 *             [0.1, 2.0] 抖动丢弃口径与 centerPoint（两指中点）语义。
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XPINCHGESTURERECOGNIZER_H
#define XPINCHGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGestureRecognizer.h"

#if XGESTURE_ON

XCLASS_DEFINE_BEGING(XPinchGestureRecognizer)
XCLASS_DEFINE_EXTEND_END(XPinchGestureRecognizer, XGestureRecognizer)

/**
 * @brief      双指缩放手势识别器；m_base 必须是第一个成员。
 */
typedef struct XPinchGestureRecognizer
{
    XGestureRecognizer m_base; /**< 基类成员；必须是第一个。 */
    float m_minScale;           /**< 单步比例下界（默认 0.1，对标 Qt）。 */
    float m_maxScale;           /**< 单步比例上界（默认 2.0，对标 Qt）。 */
    float m_prevDistance;       /**< 上一帧两指间距（px）。 */
    float m_totalScale;         /**< 累计缩放比例（起始 1.0）。 */
    bool m_started;             /**< 手势已触发。 */
} XPinchGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XPinchGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self 识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL。
 */
void XPinchGestureRecognizer_init(XPinchGestureRecognizer* self,
                                  XObject* parent);
/**
 * @brief 创建双指缩放手势识别器（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XPinchGestureRecognizer* XPinchGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XPinchGestureRecognizer_create(...) \
    XPinchGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 设置单步比例的有效区间（抖动丢弃）。
 * @param self 识别器；NULL 无操作。
 * @param minScale 下界（<=0 时按 0.01 处理）。
 * @param maxScale 上界（<=minScale 时按 minScale 处理）。
 * @return 无返回值。
 */
void XPinchGestureRecognizer_setScaleRange(XPinchGestureRecognizer* self,
                                            float minScale, float maxScale);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XPINCHGESTURERECOGNIZER_H */
