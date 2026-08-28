/*
 * @file       XColorSpace.h
 * @brief      XColorSpace 色彩空间值类型（对标 Qt 6.8 QColorSpace）
 */
#ifndef XCOLORSPACE_H
#define XCOLORSPACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "XGeometry.h"

/**
 * @brief QColorSpace 的命名色彩空间。
 * @details 数值从 1 开始，0 表示没有命名色彩空间；这与 Qt 的
 *          NamedColorSpace 枚举保持一致。
 */
typedef enum XColorSpaceNamedColorSpace
{
    XColorSpaceNamed_Unknown = 0,
    XColorSpaceNamed_SRgb = 1,
    XColorSpaceNamed_SRgbLinear,
    XColorSpaceNamed_AdobeRgb,
    XColorSpaceNamed_DisplayP3,
    XColorSpaceNamed_ProPhotoRgb,
    XColorSpaceNamed_Bt2020,
    XColorSpaceNamed_Bt2100Pq,
    XColorSpaceNamed_Bt2100Hlg
} XColorSpaceNamedColorSpace;

/**
 * @brief 色彩空间原色集合（对标 QColorSpace::Primaries）。
 * @details Custom 仅表示原色需要由白点和三个原色坐标描述，不代表无效。
 */
typedef enum XColorSpacePrimaries
{
    XColorSpacePrimaries_Custom = 0,
    XColorSpacePrimaries_SRgb,
    XColorSpacePrimaries_AdobeRgb,
    XColorSpacePrimaries_DciP3D65,
    XColorSpacePrimaries_ProPhotoRgb,
    XColorSpacePrimaries_Bt2020
} XColorSpacePrimaries;

/**
 * @brief 色彩空间传递函数（对标 QColorSpace::TransferFunction）。
 * @details Gamma22/Gamma28 是 XinYueC 早期接口的兼容值，新的代码应使用
 *          Gamma 并通过伽马字段传递精确值。
 */
typedef enum XColorSpaceTransferFunction
{
    XColorSpaceTransfer_Custom = 0,
    XColorSpaceTransfer_Linear,
    XColorSpaceTransfer_Gamma,
    XColorSpaceTransfer_SRgb,
    XColorSpaceTransfer_ProPhotoRgb,
    XColorSpaceTransfer_Bt2020,
    XColorSpaceTransfer_St2084,
    XColorSpaceTransfer_Hlg,
    XColorSpaceTransfer_Gamma22,
    XColorSpaceTransfer_Gamma28
} XColorSpaceTransferFunction;

/** @brief 色彩变换处理模型（对标 QColorSpace::TransformModel）。 */
typedef enum XColorSpaceTransformModel
{
    XColorSpaceTransform_ThreeComponentMatrix = 0,
    XColorSpaceTransform_ElementListProcessing
} XColorSpaceTransformModel;

/** @brief 色彩数据模型（对标 QColorSpace::ColorModel）。 */
typedef enum XColorSpaceColorModel
{
    XColorSpaceModel_Undefined = 0,
    XColorSpaceModel_Rgb = 1,
    XColorSpaceModel_Gray = 2,
    XColorSpaceModel_Cmyk = 3
} XColorSpaceColorModel;

/**
 * @brief 色彩空间的四组色度坐标。
 * @param m_whitePoint 白点的 CIE xy 坐标。
 * @param m_redPoint 红色原色的 CIE xy 坐标。
 * @param m_greenPoint 绿色原色的 CIE xy 坐标。
 * @param m_bluePoint 蓝色原色的 CIE xy 坐标。
 */
typedef struct XColorSpacePrimariesData
{
    XPointF m_whitePoint;
    XPointF m_redPoint;
    XPointF m_greenPoint;
    XPointF m_bluePoint;
} XColorSpacePrimariesData;

/**
 * @brief 轻量、可按值复制的色彩空间描述。
 * @details 该结构不持有堆资源，因此可以安全地嵌入 XImage、XSurfaceFormat
 *          等值对象并直接复制。当前实现覆盖 Qt 的三分量矩阵模型以及
 *          Gray/RGB 模型的基础元数据；ICC 原始字节和逐通道 LUT 需要资源
 *          容器后才能无歧义地实现，暂不放入这个值类型。
 */
typedef struct XColorSpace
{
    XColorSpacePrimaries m_primaries; /**< 原色集合。 */
    XColorSpaceTransferFunction m_transferFunction; /**< 传递函数。 */
    float m_gamma; /**< Gamma 传递函数的伽马值；其他函数为约定近似值或 0。 */
    bool m_valid; /**< 是否为有效色彩空间。 */
    XColorSpaceNamedColorSpace m_namedColorSpace; /**< 命名空间；未知时为 0。 */
    XColorSpaceTransformModel m_transformModel; /**< 色彩变换模型。 */
    XColorSpaceColorModel m_colorModel; /**< RGB、Gray、CMYK 或未知模型。 */
    XColorSpacePrimariesData m_primariesData; /**< 自定义或预定义原色坐标。 */
    char m_description[64]; /**< 可选短描述；超长内容会被截断。 */
} XColorSpace;

/**
 * @brief 颜色变换描述（用于 XImage_applyColorTransform）。
 * @param m_source 源色彩空间；无效时由调用方使用图像元数据。
 * @param m_target 目标色彩空间；必须是有效目标。
 */
typedef struct XColorTransform
{
    XColorSpace m_source;
    XColorSpace m_target;
} XColorTransform;

/* 旧枚举名称兼容宏；它们不改变 Qt 对齐后的枚举语义。 */
#define XColorSpacePrimaries_Unknown XColorSpacePrimaries_Custom
#define XColorSpacePrimaries_SRgbLinear XColorSpacePrimaries_SRgb
#define XColorSpacePrimaries_DciP3 XColorSpacePrimaries_DciP3D65
#define XColorSpaceTransfer_Unknown XColorSpaceTransfer_Custom

/** @brief 创建无效的未知色彩空间。 @return 未知色彩空间值。 */
XColorSpace XColorSpace_create(void);

/**
 * @brief 按原色集合、传递函数和伽马值创建 RGB 色彩空间。
 * @param primaries 原色集合；Custom 需要使用 XColorSpace_create_custom。
 * @param transferFunction 传递函数；Custom 在没有 LUT 时会得到无效空间。
 * @param gamma Gamma 函数的伽马值；其他函数可传 0 使用 Qt 默认近似值。
 * @return 初始化后的色彩空间值。
 */
XColorSpace XColorSpace_create_ex(XColorSpacePrimaries primaries,
                                  XColorSpaceTransferFunction transferFunction,
                                  float gamma);

/**
 * @brief 按命名空间创建预定义色彩空间。
 * @param namedColorSpace Qt NamedColorSpace 对应的命名值。
 * @return 合法命名值返回预定义空间，非法值返回无效空间。
 */
XColorSpace XColorSpace_create_named(XColorSpaceNamedColorSpace namedColorSpace);

/**
 * @brief 按原色坐标和传递函数创建自定义 RGB 色彩空间。
 * @param primaries 四组白点及原色 CIE xy 坐标。
 * @param transferFunction 传递函数。
 * @param gamma Gamma 函数的伽马值。
 * @return 坐标和传递函数均有效时返回有效空间，否则返回无效空间。
 */
XColorSpace XColorSpace_create_custom(const XColorSpacePrimariesData* primaries,
                                      XColorSpaceTransferFunction transferFunction,
                                      float gamma);

/**
 * @brief 创建自定义灰度色彩空间。
 * @param whitePoint 灰度空间白点 CIE xy 坐标。
 * @param transferFunction 传递函数。
 * @param gamma Gamma 函数的伽马值。
 * @return 初始化后的灰度色彩空间值。
 */
XColorSpace XColorSpace_create_gray(XPointF whitePoint,
                                    XColorSpaceTransferFunction transferFunction,
                                    float gamma);

/** @brief 获取标准 sRGB 色彩空间。 @return 标准 sRGB 值。 */
XColorSpace XColorSpace_sRgb(void);

/** @brief 获取线性 sRGB 色彩空间。 @return 线性 sRGB 值。 */
XColorSpace XColorSpace_sRgbLinear(void);

/** @brief 获取 Adobe RGB 色彩空间。 @return Adobe RGB 值。 */
XColorSpace XColorSpace_adobeRgb(void);

/** @brief 获取 Display P3 色彩空间。 @return Display P3 值。 */
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
 * @brief 查询原色集合。
 * @param self 待查询的色彩空间指针。
 * @return 原色集合；空指针按 Custom 返回。
 */
XColorSpacePrimaries XColorSpace_primaries(const XColorSpace* self);

/**
 * @brief 查询传递函数。
 * @param self 待查询的色彩空间指针。
 * @return 传递函数；空指针按 Custom 返回。
 */
XColorSpaceTransferFunction XColorSpace_transferFunction(const XColorSpace* self);

/**
 * @brief 查询传递函数的 Gamma 值。
 * @param self 待查询的色彩空间指针。
 * @return Gamma 值；无空间时返回 0。
 */
float XColorSpace_gamma(const XColorSpace* self);

/**
 * @brief 查询变换模型。
 * @param self 待查询的色彩空间指针。
 * @return 空指针按 ThreeComponentMatrix 返回。
 */
XColorSpaceTransformModel XColorSpace_transformModel(const XColorSpace* self);

/**
 * @brief 查询颜色模型。
 * @param self 待查询的色彩空间指针。
 * @return 空指针或未知空间返回 Undefined。
 */
XColorSpaceColorModel XColorSpace_colorModel(const XColorSpace* self);

/**
 * @brief 查询自定义或预定义原色坐标。
 * @param self 待查询的色彩空间指针。
 * @param out 输出坐标；不可为空。
 * @return 成功取得有效坐标返回 true，否则返回 false。
 */
bool XColorSpace_primariesData(const XColorSpace* self,
                               XColorSpacePrimariesData* out);

/**
 * @brief 查询短描述文本。
 * @param self 待查询的色彩空间指针。
 * @return 描述字符串；没有描述或参数为空时返回空字符串。
 */
const char* XColorSpace_description(const XColorSpace* self);

/**
 * @brief 设置色彩空间短描述文本。
 * @param self 待修改的色彩空间指针。
 * @param description UTF-8 描述文本；为空时清除描述。
 */
void XColorSpace_setDescription(XColorSpace* self, const char* description);

/**
 * @brief 比较两个色彩空间是否相等。
 * @param left 左侧色彩空间指针。
 * @param right 右侧色彩空间指针。
 * @return 两者属性相等返回 true，否则返回 false。
 */
bool XColorSpace_equals(const XColorSpace* left, const XColorSpace* right);

#ifdef __cplusplus
}
#endif

#endif
