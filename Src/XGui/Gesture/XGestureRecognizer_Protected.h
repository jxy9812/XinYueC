/**
 * @file       XGestureRecognizer_Protected.h
 * @brief      手势识别器保护接口（仅供识别器子类与框架内部使用）。
 * @details    本文件集中声明识别器派生类在 Recognize 虚槽内所需的
 *             子类专用接口：输出数据填写（位置/增量/速度/缩放/旋转/
 *             方向）与前帧触点快照读取。普通应用代码不应直接包含或
 *             调用本文件中的接口。
 * @note       本文件依赖 XGestureRecognizer.h；模块总开关 XGESTURE_ON
 *             关闭时，以下保护接口全部裁剪。
 * @author     XinYueC 团队
 */
#ifndef XGESTURERECOGNIZER_PROTECTED_H
#define XGESTURERECOGNIZER_PROTECTED_H

#if XGUI_ON

#ifdef __cplusplus
extern "C" {
#endif

#include "XGestureRecognizer.h"

#if XGESTURE_ON

/* ==================== 子类输出数据填写（对标 Qt 各手势类数据面） ======== */

/**
 * @brief 填写手势当前位置与起始位置（控件局部坐标）。
 * @param self 识别器；NULL 无操作。
 * @param position 当前主触点位置；可为 NULL（不修改）。
 * @param startPosition 手势起始位置；可为 NULL（不修改）。
 * @return 无返回值。
 */
void XGestureRecognizer_setOutputPosition(XGestureRecognizer* self,
                                          const XPoint* position,
                                          const XPoint* startPosition);
/**
 * @brief 填写手势增量位移与上一帧位置。
 * @param self 识别器；NULL 无操作。
 * @param delta 本帧增量位移；可为 NULL（不修改）。
 * @param lastPosition 上一帧主触点位置；可为 NULL（不修改）。
 * @return 无返回值。
 */
void XGestureRecognizer_setOutputDelta(XGestureRecognizer* self,
                                        const XPoint* delta,
                                        const XPoint* lastPosition);
/**
 * @brief 填写手势速度（px/s）。
 * @param self 识别器；NULL 无操作。
 * @param velocityX 水平速度（正=向右）。
 * @param velocityY 纵向速度（正=向下）。
 * @return 无返回值。
 */
void XGestureRecognizer_setOutputVelocity(XGestureRecognizer* self,
                                           float velocityX, float velocityY);
/**
 * @brief 填写缩放数据（无量纲比例）。
 * @param self 识别器；NULL 无操作。
 * @param scaleFactor 本帧缩放比例（1.0=未缩放）。
 * @param totalScaleFactor 累计缩放比例。
 * @return 无返回值。
 */
void XGestureRecognizer_setOutputScale(XGestureRecognizer* self,
                                        float scaleFactor,
                                        float totalScaleFactor);
/**
 * @brief 填写旋转数据（度）。
 * @param self 识别器；NULL 无操作。
 * @param rotationAngle 本帧旋转角增量（度）。
 * @param totalRotationAngle 累计旋转角（度）。
 * @return 无返回值。
 */
void XGestureRecognizer_setOutputRotation(XGestureRecognizer* self,
                                           float rotationAngle,
                                           float totalRotationAngle);
/**
 * @brief 填写手势方向与方向角（度，相对 x 轴，顺时针为正）。
 * @param self 识别器；NULL 无操作。
 * @param direction 手势方向（存 XGestureDirection）。
 * @param angle 方向角（度）。
 * @return 无返回值。
 */
void XGestureRecognizer_setOutputDirection(XGestureRecognizer* self,
                                            int direction, float angle);
/**
 * @brief 填写参与手势的触点数量。
 * @param self 识别器；NULL 无操作。
 * @param count 触点数量（0~255）。
 * @return 无返回值。
 */
void XGestureRecognizer_setOutputFingerCount(XGestureRecognizer* self,
                                             int count);

/* ==================== 前帧快照读取 ==================== */

/**
 * @brief 读取上一帧触点快照（缩放/旋转类手势需要帧间距离与角度差）。
 * @param self 识别器；不可为 NULL。
 * @param outCount 输出参数：回填快照触点数量；可为 NULL。
 * @return 快照触点数组（借用，内部存储）；无快照或 self 为 NULL 时
 *         返回 NULL。
 */
const XTouchPoint* XGestureRecognizer_prevPoints(
    const XGestureRecognizer* self, int* outCount);
/**
 * @brief 读取手势起始时刻（手势时钟域毫秒）。
 * @param self 识别器；可为 NULL。
 * @return 起始时刻；未开始或 self 为 NULL 时返回 0。
 */
int64_t XGestureRecognizer_startMs(const XGestureRecognizer* self);
/**
 * @brief 读取最近一帧时刻（手势时钟域毫秒）。
 * @param self 识别器；可为 NULL。
 * @return 最近帧时刻；self 为 NULL 时返回 0。
 */
int64_t XGestureRecognizer_lastMs(const XGestureRecognizer* self);
/**
 * @brief 读取手势时钟域当前时刻（毫秒）。
 * @details 触摸时间戳域优先（同一序列各帧同域可比，对标 Qt 触摸事件
 *          timestamp 一致性）；无同步触摸派发（时间戳 0）时回退单调
 *          毫秒墙钟（约束文档时间源规则，见 XDateTime）。
 * @return 当前时刻（毫秒）。
 */
int64_t XGestureRecognizer_nowMs(void);

#endif /* XGESTURE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XGESTURERECOGNIZER_PROTECTED_H */
