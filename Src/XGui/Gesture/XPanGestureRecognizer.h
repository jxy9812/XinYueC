/**
 * @file       XPanGestureRecognizer.h
 * @brief      平移手势识别器公开 API（对标 Qt 6.8 QPanGesture）。
 * @details    按住拖动的跟手平移识别：位移越过阈值即触发（Started），
 *             后续每帧产出增量位移（Updated），抬手收口（Finished）。
 *             与 Qt 的差异：Qt 默认参与 2 指（可用环境变量覆盖），本
 *             框架默认 1 指（移动端拖动的常规形态），指头数可配。
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XPANGESTURERECOGNIZER_H
#define XPANGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGestureRecognizer.h"

#if XGESTURE_ON

XCLASS_DEFINE_BEGING(XPanGestureRecognizer)
XCLASS_DEFINE_EXTEND_END(XPanGestureRecognizer, XGestureRecognizer)

/**
 * @brief      平移手势识别器；m_base 必须是第一个成员。
 */
typedef struct XPanGestureRecognizer
{
    XGestureRecognizer m_base; /**< 基类成员；必须是第一个。 */
    int m_threshold;           /**< 触发阈值（px，任一轴绝对值超过即触发；
                                   默认 10，对标 Qt QPanGesture 的 10px）。 */
    int m_pointCount;          /**< 参与平移的指头数量（默认 1；Qt 默认 2）。 */
    int m_pressX;              /**< 按压起点 x（控件局部坐标）。 */
    int m_pressY;              /**< 按压起点 y（控件局部坐标）。 */
    int m_lastX;               /**< 上一帧主触点 x（增量基准）。 */
    int m_lastY;               /**< 上一帧主触点 y（增量基准）。 */
    int m_totalX;              /**< 累计位移 x（自按压起点）。 */
    int m_totalY;              /**< 累计位移 y（自按压起点）。 */
    int64_t m_pressMs;         /**< 按压时刻（手势时钟域毫秒）。 */
    bool m_started;            /**< 手势已触发（进行中）。 */
} XPanGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XPanGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self 识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL。
 */
void XPanGestureRecognizer_init(XPanGestureRecognizer* self, XObject* parent);
/**
 * @brief 创建平移手势识别器（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XPanGestureRecognizer* XPanGestureRecognizer_create_ex(XMemoryType memory,
                                                        XObject* parent);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XPanGestureRecognizer_create(...) \
    XPanGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 设置触发阈值（px）。
 * @param self 识别器；NULL 无操作。
 * @param threshold 阈值像素（负值按 0 处理）。
 * @return 无返回值。
 */
void XPanGestureRecognizer_setThreshold(XPanGestureRecognizer* self,
                                        int threshold);
/**
 * @brief 设置参与平移的指头数量。
 * @param self 识别器；NULL 无操作。
 * @param count 指头数量（1~8；超出按 1~8 钳位）。
 * @return 无返回值。
 */
void XPanGestureRecognizer_setPointCount(XPanGestureRecognizer* self,
                                         int count);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XPANGESTURERECOGNIZER_H */
