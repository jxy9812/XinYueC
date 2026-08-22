/*
 * @file       XColorSpace.h
 * @brief      XColorSpace 色彩空间值类型（对标 Qt QColorSpace）
 */
#ifndef XCOLORSPACE_H
#define XCOLORSPACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief 色彩空间原色集合（对标 QColorSpace::Primaries）。 */
typedef enum XColorSpacePrimaries
{
    XColorSpacePrimaries_Unknown = 0,
    XColorSpacePrimaries_SRgb,
    XColorSpacePrimaries_SRgbLinear,
    XColorSpacePrimaries_AdobeRgb,
    XColorSpacePrimaries_DciP3,
    XColorSpacePrimaries_ProPhotoRgb
} XColorSpacePrimaries;

/** @brief 色彩空间传递函数（对标 QColorSpace::TransferFunction）。 */
typedef enum XColorSpaceTransferFunction
{
    XColorSpaceTransfer_Unknown = 0,
    XColorSpaceTransfer_Linear,
    XColorSpaceTransfer_SRgb,
    XColorSpaceTransfer_Gamma22,
    XColorSpaceTransfer_Gamma28
} XColorSpaceTransferFunction;

/**
 * @brief 轻量色彩空间值类型。
 * @note 目前以原色集合和传递函数描述色彩空间，不持有外部资源；未知空间
 *       不会被隐式当作 sRGB 处理。当前 XImage 的转换实现完整处理传递函数，
 *       原色集合用于描述和保留，跨原色集合的色度适配由上层色彩管理器负责。
 */
typedef struct XColorSpace
{
    XColorSpacePrimaries m_primaries; /**< 原色集合。 */
    XColorSpaceTransferFunction m_transferFunction; /**< 传递函数。 */
    float m_gamma; /**< 自定义伽马值；非伽马传递函数时为 0。 */
    bool m_valid; /**< 是否为有效色彩空间。 */
} XColorSpace;

/** @brief 颜色变换描述（用于 XImage_applyColorTransform）。 */
typedef struct XColorTransform
{
    XColorSpace m_source; /**< 源色彩空间。 */
    XColorSpace m_target; /**< 目标色彩空间。 */
} XColorTransform;

/**
 * @brief 创建无效的未知色彩空间。
 * @return 初始化后的未知色彩空间值。
 */
XColorSpace XColorSpace_create(void);

/**
 * @brief 按原色集合、传递函数和伽马值创建色彩空间。
 * @param primaries 原色集合。
 * @param transferFunction 传递函数。
 * @param gamma 自定义伽马值；使用标准传递函数时可传 0。
 * @return 初始化后的色彩空间值。
 */
XColorSpace XColorSpace_create_ex(XColorSpacePrimaries primaries,
                                  XColorSpaceTransferFunction transferFunction,
                                  float gamma);

/**
 * @brief 获取标准 sRGB 色彩空间。
 * @return 标准 sRGB 色彩空间值。
 */
XColorSpace XColorSpace_sRgb(void);

/**
 * @brief 获取线性 sRGB 色彩空间。
 * @return 线性 sRGB 色彩空间值。
 */
XColorSpace XColorSpace_sRgbLinear(void);

/**
 * @brief 获取 Adobe RGB 色彩空间。
 * @return Adobe RGB 色彩空间值。
 */
XColorSpace XColorSpace_adobeRgb(void);

/**
 * @brief 获取 DCI-P3 色彩空间。
 * @return DCI-P3 色彩空间值。
 */
XColorSpace XColorSpace_dciP3(void);

/**
 * @brief 判断色彩空间是否有效。
 * @param self 待检查的色彩空间指针。
 * @return 有效返回 true，否则返回 false。
 */
bool XColorSpace_isValid(const XColorSpace* self);

/**
 * @brief 判断色彩空间是否为标准 sRGB。
 * @param self 待检查的色彩空间指针。
 * @return 为标准 sRGB 返回 true，否则返回 false。
 */
bool XColorSpace_isSRgb(const XColorSpace* self);

/**
 * @brief 比较两个色彩空间是否完全相等。
 * @param left 左侧色彩空间指针。
 * @param right 右侧色彩空间指针。
 * @return 两者属性相等返回 true，否则返回 false。
 */
bool XColorSpace_equals(const XColorSpace* left, const XColorSpace* right);

#ifdef __cplusplus
}
#endif

#endif
