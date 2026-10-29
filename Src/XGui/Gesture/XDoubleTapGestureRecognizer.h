/**
 * @file       XDoubleTapGestureRecognizer.h
 * @brief      双击手势识别器公开 API（框架扩展，对标 Qt 走鼠标
 *             DBL_CLICK 合成、无线上手势类）。
 * @details    两次点按的间隔与距离均在阈值内即判为双击。对标 Qt 的
 *             QStyleHints::mouseDoubleClickInterval（默认 400ms）与
 *             touchDoubleTapDistance（默认 40px）。
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XDOUBLETAPGESTURERECOGNIZER_H
#define XDOUBLETAPGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGestureRecognizer.h"

#if XGESTURE_ON

XCLASS_DEFINE_BEGING(XDoubleTapGestureRecognizer)
XCLASS_DEFINE_EXTEND_END(XDoubleTapGestureRecognizer, XGestureRecognizer)

/**
 * @brief      双击手势识别器；m_base 必须是第一个成员。
 */
typedef struct XDoubleTapGestureRecognizer
{
    XGestureRecognizer m_base; /**< 基类成员；必须是第一个。 */
    int m_interval;             /**< 两次点按的最大间隔（ms；默认 400，
                                   对标 Qt mouseDoubleClickInterval）。 */
    int m_maxDistance;          /**< 两次点按的最大间距（px；默认 40，
                                   对标 Qt touchDoubleTapDistance）。 */
    int m_pressX;               /**< 本次按压起点 x（控件局部坐标）。 */
    int m_pressY;               /**< 本次按压起点 y（控件局部坐标）。 */
    int m_firstX;               /**< 上一次点按位置 x。 */
    int m_firstY;               /**< 上一次点按位置 y。 */
    int64_t m_firstMs;          /**< 上一次点按收口时刻（毫秒；0=无）。 */
    bool m_pressed;             /**< 本次按下已发生（等抬起判定）。 */
} XDoubleTapGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XDoubleTapGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self 识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL。
 */
void XDoubleTapGestureRecognizer_init(XDoubleTapGestureRecognizer* self,
                                      XObject* parent);
/**
 * @brief 创建双击手势识别器（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XDoubleTapGestureRecognizer* XDoubleTapGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XDoubleTapGestureRecognizer_create(...)     XDoubleTapGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 设置两次点按的最大间隔（ms）。
 * @param self 识别器；NULL 无操作。
 * @param interval 间隔毫秒（负值按 0 处理）。
 * @return 无返回值。
 */
void XDoubleTapGestureRecognizer_setInterval(
    XDoubleTapGestureRecognizer* self, int interval);
/**
 * @brief 设置两次点按的最大间距（px）。
 * @param self 识别器；NULL 无操作。
 * @param maxDistance 间距像素（负值按 0 处理）。
 * @return 无返回值。
 */
void XDoubleTapGestureRecognizer_setMaxDistance(
    XDoubleTapGestureRecognizer* self, int maxDistance);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XDOUBLETAPGESTURERECOGNIZER_H */
