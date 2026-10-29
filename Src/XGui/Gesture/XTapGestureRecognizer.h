/**
 * @file       XTapGestureRecognizer.h
 * @brief      点按手势识别器公开 API（对标 Qt 6.8 QTapGesture）。
 * @details    轻点判定：按下与抬起之间位移不超过容差即判为点按。对标
 *             Qt QTapGesture 的 40px 曼哈顿容差（Qt 不设按压时长上限）。
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XTAPGESTURERECOGNIZER_H
#define XTAPGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGestureRecognizer.h"

#if XGESTURE_ON

XCLASS_DEFINE_BEGING(XTapGestureRecognizer)
XCLASS_DEFINE_EXTEND_END(XTapGestureRecognizer, XGestureRecognizer)

/**
 * @brief      点按手势识别器；m_base 必须是第一个成员。
 */
typedef struct XTapGestureRecognizer
{
    XGestureRecognizer m_base; /**< 基类成员；必须是第一个。 */
    int m_maxDistance;          /**< 按压期间允许的最大位移（px，曼哈顿；
                                   默认 40，对标 Qt QTapGesture）。 */
    int m_pressX;               /**< 按压起点 x（控件局部坐标）。 */
    int m_pressY;               /**< 按压起点 y（控件局部坐标）。 */
} XTapGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XTapGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self 识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL。
 */
void XTapGestureRecognizer_init(XTapGestureRecognizer* self, XObject* parent);
/**
 * @brief 创建点按手势识别器（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XTapGestureRecognizer* XTapGestureRecognizer_create_ex(XMemoryType memory,
                                                        XObject* parent);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XTapGestureRecognizer_create(...)     XTapGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 设置按压期间允许的最大位移（px，曼哈顿距离）。
 * @param self 识别器；NULL 无操作。
 * @param maxDistance 容差像素（负值按 0 处理）。
 * @return 无返回值。
 */
void XTapGestureRecognizer_setMaxDistance(XTapGestureRecognizer* self,
                                          int maxDistance);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XTAPGESTURERECOGNIZER_H */
