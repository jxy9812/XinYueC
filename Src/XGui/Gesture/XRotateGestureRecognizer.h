/**
 * @file       XRotateGestureRecognizer.h
 * @brief      双指旋转手势识别器公开 API（框架扩展；Qt 在 QPinchGesture
 *             内以 rotationAngle 附带旋转，无独立旋转手势类）。
 * @details    恰好两指的旋转判定：本帧角度差=当前两指连线角 − 上一帧
 *             角，累计角度自手势起始累加（单位：度）。对标 Qt 的
 *             angleDelta 与 totalRotationAngle 口径。
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XROTATEGESTURERECOGNIZER_H
#define XROTATEGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGestureRecognizer.h"

#if XGESTURE_ON

XCLASS_DEFINE_BEGING(XRotateGestureRecognizer)
XCLASS_DEFINE_EXTEND_END(XRotateGestureRecognizer, XGestureRecognizer)

/**
 * @brief      双指旋转手势识别器；m_base 必须是第一个成员。
 */
typedef struct XRotateGestureRecognizer
{
    XGestureRecognizer m_base; /**< 基类成员；必须是第一个。 */
    float m_prevAngle;          /**< 上一帧两指连线角（度）。 */
    float m_totalAngle;         /**< 累计旋转角（度）。 */
    float m_minAngle;           /**< 触发阈值（度；默认 1.0，小于该值的
                                   抖动步丢弃）。 */
    bool m_started;             /**< 手势已触发。 */
} XRotateGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XRotateGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self 识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL。
 */
void XRotateGestureRecognizer_init(XRotateGestureRecognizer* self,
                                   XObject* parent);
/**
 * @brief 创建双指旋转手势识别器（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XRotateGestureRecognizer* XRotateGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XRotateGestureRecognizer_create(...) \
    XRotateGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 设置触发阈值（度）。
 * @param self 识别器；NULL 无操作。
 * @param minAngle 阈值角度（负值按 0 处理）。
 * @return 无返回值。
 */
void XRotateGestureRecognizer_setMinAngle(XRotateGestureRecognizer* self,
                                           float minAngle);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XROTATEGESTURERECOGNIZER_H */
