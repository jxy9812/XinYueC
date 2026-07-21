/******************************************************************************
 * @file       XGuiTypes.h
 * @brief      XGui 基础几何类型定义（对标 Qt 6.8 QPoint/QSize/QRect/QRegion）
 * @author     XinYueC 团队
 * @note       提供 GUI 模块常用的点、尺寸、矩形、区域等基础几何类型
 ******************************************************************************/
#ifndef XGUITYPES_H
#define XGUITYPES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XData/XGeometry/XPoint.h"

/* ========== 基础几何类型 ========== */

/**
 * @brief      二维整数尺寸（对标 Qt 6.8 QSize）
 * @note       使用整数表示的宽度和高度
 */
typedef struct XSize
{
    int width;   /**< 宽度 */
    int height;  /**< 高度 */
}XSize;

/**
 * @brief      二维浮点尺寸（对标 Qt 6.8 QSizeF）
 * @note       使用浮点数表示的宽度和高度
 */
typedef struct XSizeF
{
    float width;   /**< 宽度 */
    float height;  /**< 高度 */
}XSizeF;

/**
 * @brief      二维整数矩形（对标 Qt 6.8 QRect）
 * @note       使用整数坐标表示的矩形区域，包含左上角坐标和宽高
 */
typedef struct XRect
{
    int x;      /**< 左上角 X 坐标 */
    int y;      /**< 左上角 Y 坐标 */
    int width;  /**< 宽度 */
    int height; /**< 高度 */
}XRect;

/**
 * @brief      二维浮点矩形（对标 Qt 6.8 QRectF）
 * @note       使用浮点数坐标表示的矩形区域
 */
typedef struct XRectF
{
    float x;      /**< 左上角 X 坐标 */
    float y;      /**< 左上角 Y 坐标 */
    float width;  /**< 宽度 */
    float height; /**< 高度 */
}XRectF;

/**
 * @brief      区域类型（对标 Qt 6.8 QRegion）
 * @note       表示一个复杂区域，由矩形列表组成
 */
typedef struct XRegion
{
    XRect* rects;     /**< 矩形数组 */
    int    count;     /**< 矩形数量 */
    int    capacity;  /**< 矩形数组容量 */
}XRegion;

/* ========== 辅助函数 ========== */

/**
 * @brief      初始化 XSize 对象
 * @param self   目标 XSize 对象指针
 * @param w  宽度
 * @param h 高度
 */
void XSize_init(XSize* self, int w, int h);

/**
 * @brief      初始化 XRect 对象
 * @param self   目标 XRect 对象指针
 * @param x      X 坐标
 * @param y      Y 坐标
 * @param w  宽度
 * @param h 高度
 */
void XRect_init(XRect* self, int x, int y, int w, int h);

/**
 * @brief      判断矩形是否为空（宽度或高度 <= 0）
 * @param self 目标 XRect 对象指针
 * @return 为空返回 true
 */
bool XRect_isEmpty(const XRect* self);

/**
 * @brief      判断矩形是否包含指定点
 * @param self 目标 XRect 对象指针
 * @param x    点的 X 坐标
 * @param y    点的 Y 坐标
 * @return 包含返回 true
 */
bool XRect_contains(const XRect* self, int x, int y);

/**
 * @brief      初始化 XSizeF 对象
 * @param self   目标 XSizeF 对象指针
 * @param w  宽度
 * @param h 高度
 */
void XSizeF_init(XSizeF* self, float w, float h);

/**
 * @brief      初始化 XRegion 对象
 * @param self 目标 XRegion 对象指针
 */
void XRegion_init(XRegion* self);

/**
 * @brief      释放 XRegion 资源
 * @param self 目标 XRegion 对象指针
 */
void XRegion_deinit(XRegion* self);

/**
 * @brief      向区域中添加矩形
 * @param self 目标 XRegion 对象指针
 * @param rect 待添加的矩形指针
 */
void XRegion_addRect(XRegion* self, const XRect* rect);

#ifdef __cplusplus
}
#endif
#endif /* XGUITYPES_H */
