/**
 * @file       XSwipeGestureRecognizer.h
 * @brief      轻扫手势识别器公开 API（对标 Qt 6.8 QSwipeGesture）。
 * @details    快速轻扫判定：位移越过距离阈值、时长不超过上限、抬手速度
 *             高于速度阈值即判为轻扫，并给出方向与方向角。对标 Qt 的
 *             50px 位移阈值与方向反转取消口径。
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XSWIPEGESTURERECOGNIZER_H
#define XSWIPEGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGestureRecognizer.h"

#if XGESTURE_ON

XCLASS_DEFINE_BEGING(XSwipeGestureRecognizer)
XCLASS_DEFINE_EXTEND_END(XSwipeGestureRecognizer, XGestureRecognizer)

/**
 * @brief      轻扫手势识别器；m_base 必须是第一个成员。
 */
typedef struct XSwipeGestureRecognizer
{
    XGestureRecognizer m_base; /**< 基类成员；必须是第一个。 */
    int m_minDistance;          /**< 最小位移（px；默认 50，对标 Qt
                                   QSwipeGesture 的 50px）。 */
    int m_minVelocity;          /**< 最小抬手速度（px/s；默认 300）。 */
    int m_maxDuration;          /**< 最大按压时长（ms；默认 300——超时的
                                   长按/拖动不算轻扫）。 */
    int m_pressX;               /**< 按压起点 x（控件局部坐标）。 */
    int m_pressY;               /**< 按压起点 y（控件局部坐标）。 */
    int64_t m_pressMs;          /**< 按压时刻（手势时钟域毫秒）。 */
    int m_lastX;                /**< 上一帧主触点 x。 */
    int m_lastY;                /**< 上一帧主触点 y。 */
    bool m_moved;               /**< 已越位移阈值（进入轻扫判定）。 */
    bool m_fired;               /**< 轻扫已产出（一次序列只一次）。 */
} XSwipeGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XSwipeGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self 识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL。
 */
void XSwipeGestureRecognizer_init(XSwipeGestureRecognizer* self,
                                  XObject* parent);
/**
 * @brief 创建轻扫手势识别器（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XSwipeGestureRecognizer* XSwipeGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XSwipeGestureRecognizer_create(...) \
    XSwipeGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 设置最小位移阈值（px）。
 * @param self 识别器；NULL 无操作。
 * @param minDistance 阈值像素（负值按 0 处理）。
 * @return 无返回值。
 */
void XSwipeGestureRecognizer_setMinDistance(XSwipeGestureRecognizer* self,
                                            int minDistance);
/**
 * @brief 设置最小抬手速度（px/s）。
 * @param self 识别器；NULL 无操作。
 * @param minVelocity 速度阈值（负值按 0 处理）。
 * @return 无返回值。
 */
void XSwipeGestureRecognizer_setMinVelocity(XSwipeGestureRecognizer* self,
                                            int minVelocity);
/**
 * @brief 设置最大按压时长（ms）。
 * @param self 识别器；NULL 无操作。
 * @param maxDuration 时长毫秒（负值按 0 处理）。
 * @return 无返回值。
 */
void XSwipeGestureRecognizer_setMaxDuration(XSwipeGestureRecognizer* self,
                                            int maxDuration);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XSWIPEGESTURERECOGNIZER_H */
