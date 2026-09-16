/**
 * @file       XPaintDevice.h
 * @brief      XPaintDevice 绘制设备基类 + XPaintEngine 引擎描述
 *             （对标 Qt 6.8 QPaintDevice / QPaintEngine 全部公共 API）。
 * @details    功能范围：
 *             - XPaintDeviceMetric 枚举（PdmWidth..PdmDevicePixelRatioScaled，
 *               数值与 Qt::QPaintDevice::PaintDeviceMetric 一致）；
 *             - XPaintDevice：设备度量查询（devType/paintEngine/width/
 *               height/widthMM/heightMM/logicalDpiX/logicalDpiY/
 *               physicalDpiX/physicalDpiY/devicePixelRatio/
 *               devicePixelRatioF/colorCount/depth/metric）；metric 为
 *               可覆写虚语义（接入类提供回调）；
 *             - XPaintEngineType / XPaintEngineFeature 枚举（数值与
 *               QPaintEngine 一致）；XPaintEngine 描述对象（type/
 *               isActive/hasFeature）；绘制能力由 XPainter 承担（XGui
 *               渲染引擎即 XPainter，本项目不实现 QPaintEngine 的绘制
 *               命令接口，头文件注明）。
 * @note       模块总开关 XPAINTDEVICE_ON 定义于 XGuiConfig.h；XImage/
 *             XPixmap/XBitmap/XPicture/XWidget 通过内嵌字段接入。
 * @author     XinYueC 团队
 */
#ifndef XPAINTDEVICE_H
#define XPAINTDEVICE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"

#if XPAINTDEVICE_ON

/** @brief 绘制设备度量（对标 QPaintDevice::PaintDeviceMetric，数值一致）。 */
typedef enum XPaintDeviceMetric
{
    XPaintDeviceMetric_PdmWidth = 1,               /**< 设备宽度（像素）。 */
    XPaintDeviceMetric_PdmHeight = 2,              /**< 设备高度（像素）。 */
    XPaintDeviceMetric_PdmWidthMM = 3,             /**< 物理宽度（毫米）。 */
    XPaintDeviceMetric_PdmHeightMM = 4,            /**< 物理高度（毫米）。 */
    XPaintDeviceMetric_PdmNumColors = 5,           /**< 颜色数。 */
    XPaintDeviceMetric_PdmDepth = 6,               /**< 位深度。 */
    XPaintDeviceMetric_PdmDpiX = 7,                /**< 逻辑 X DPI。 */
    XPaintDeviceMetric_PdmDpiY = 8,                /**< 逻辑 Y DPI。 */
    XPaintDeviceMetric_PdmPhysicalDpiX = 9,        /**< 物理 X DPI。 */
    XPaintDeviceMetric_PdmPhysicalDpiY = 10,       /**< 物理 Y DPI。 */
    XPaintDeviceMetric_PdmDevicePixelRatio = 11,   /**< 设备像素比（整数）。 */
    XPaintDeviceMetric_PdmDevicePixelRatioScaled = 12 /**< 设备像素比×256（精度保留）。 */
} XPaintDeviceMetric;

/** @brief 设备类型码（XGui 内部约定，区别于 Qt devType 的平台数值）。 */
typedef enum XPaintDeviceType
{
    XPaintDeviceType_Widget = 0,  /**< 控件（XWidget）。 */
    XPaintDeviceType_Image = 1,   /**< 图像（XImage）。 */
    XPaintDeviceType_Pixmap = 2,  /**< 像素图（XPixmap）。 */
    XPaintDeviceType_Bitmap = 3,  /**< 位图（XBitmap）。 */
    XPaintDeviceType_Picture = 4  /**< 图片（XPicture）。 */
} XPaintDeviceType;

/** @brief 绘制引擎类型（对标 QPaintEngine::Type，数值一致）。 */
typedef enum XPaintEngineType
{
    XPaintEngineType_X11 = 0,          /**< X11。 */
    XPaintEngineType_Windows = 1,      /**< Windows。 */
    XPaintEngineType_QuickDraw = 2,    /**< QuickDraw。 */
    XPaintEngineType_CoreGraphics = 3, /**< CoreGraphics。 */
    XPaintEngineType_MacPrinter = 4,   /**< MacPrinter。 */
    XPaintEngineType_QWindowSystem = 5,/**< QWindowSystem。 */
    XPaintEngineType_OpenGL = 6,       /**< OpenGL。 */
    XPaintEngineType_Picture = 7,      /**< Picture。 */
    XPaintEngineType_SVG = 8,          /**< SVG。 */
    XPaintEngineType_Raster = 9,       /**< Raster（软件光栅，XGui 默认）。 */
    XPaintEngineType_Direct3D = 10,    /**< Direct3D。 */
    XPaintEngineType_Pdf = 11,         /**< PDF。 */
    XPaintEngineType_OpenVG = 12,      /**< OpenVG。 */
    XPaintEngineType_OpenGL2 = 13,     /**< OpenGL2。 */
    XPaintEngineType_PaintBuffer = 14, /**< PaintBuffer。 */
    XPaintEngineType_Blitter = 15,     /**< Blitter。 */
    XPaintEngineType_Direct2D = 16,    /**< Direct2D。 */
    XPaintEngineType_User = 50,        /**< 用户类型起点。 */
    XPaintEngineType_MaxUser = 100     /**< 用户类型终点。 */
} XPaintEngineType;

/** @brief 绘制引擎能力位（对标 QPaintEngine::PaintEngineFeature，数值一致）。 */
typedef enum XPaintEngineFeature
{
    XPaintEngineFeature_PrimitiveTransform = 0x00000001,          /**< 可变换原语画刷。 */
    XPaintEngineFeature_PatternTransform = 0x00000002,            /**< 可变换图案画刷。 */
    XPaintEngineFeature_PixmapTransform = 0x00000004,             /**< 可变换像素图。 */
    XPaintEngineFeature_PatternBrush = 0x00000008,                /**< 可填充像素图/标准图案。 */
    XPaintEngineFeature_LinearGradientFill = 0x00000010,          /**< 线性渐变。 */
    XPaintEngineFeature_RadialGradientFill = 0x00000020,          /**< 径向渐变。 */
    XPaintEngineFeature_ConicalGradientFill = 0x00000040,         /**< 锥形渐变。 */
    XPaintEngineFeature_AlphaBlend = 0x00000080,                  /**< 源上 alpha 混合。 */
    XPaintEngineFeature_PorterDuff = 0x00000100,                  /**< 常规 Porter-Duff。 */
    XPaintEngineFeature_PainterPaths = 0x00000200,                /**< 路径填充/描边/裁剪。 */
    XPaintEngineFeature_Antialiasing = 0x00000400,                /**< 抗锯齿线。 */
    XPaintEngineFeature_BrushStroke = 0x00000800,                 /**< 画刷笔。 */
    XPaintEngineFeature_ConstantOpacity = 0x00001000,             /**< 恒定不透明度。 */
    XPaintEngineFeature_MaskedBrush = 0x00002000,                 /**< 带掩码/透明通道纹理。 */
    XPaintEngineFeature_PerspectiveTransform = 0x00004000,        /**< 透视变换。 */
    XPaintEngineFeature_BlendModes = 0x00008000,                  /**< 扩展合成模式。 */
    XPaintEngineFeature_ObjectBoundingModeGradients = 0x00010000, /**< 对象包围模式渐变。 */
    XPaintEngineFeature_RasterOpModes = 0x00020000,               /**< 逻辑光栅操作。 */
    XPaintEngineFeature_PaintOutsidePaintEvent = 0x20000000,      /**< 绘制事件外绘制。 */
    XPaintEngineFeature_AllFeatures = 0xffffffff                  /**< 全部能力。 */
} XPaintEngineFeature;

/** @brief XPaintDevice 前向声明。 */
typedef struct XPaintDevice XPaintDevice;

/**
 * @brief      设备度量回调（对标 QPaintDevice::metric 虚函数）。
 * @param      userData 接入对象指针（图像数据/控件指针）。
 * @param      metric 度量码（XPaintDeviceMetric）。
 * @return     度量值。
 */
typedef int (*XPaintDeviceMetricFunc)(void* userData, int metric);

/** @brief 绘制引擎描述（对标 QPaintEngine 的数据查询部分）。 */
typedef struct XPaintEngine
{
    int m_type;      /**< 引擎类型（XPaintEngineType）。 */
    bool m_active;   /**< 是否活动（对标 isActive）。 */
    uint32_t m_features; /**< 能力位组合（XPaintEngineFeature）。 */
} XPaintEngine;

/** @brief 绘制设备（对标 QPaintDevice；接入类内嵌并回调度量）。 */
typedef struct XPaintDevice
{
    int m_devType;              /**< 设备类型码（XPaintDeviceType）。 */
    void* m_userData;           /**< 接入对象指针（借用）。 */
    XPaintDeviceMetricFunc m_metric; /**< 度量回调（可为 NULL=默认 0）。 */
    XPaintEngine m_engine;      /**< 引擎描述（paintEngine() 返回）。 */
} XPaintDevice;

/* ==================== 引擎 ==================== */

/** @brief 初始化引擎描述。
 * @param self 目标引擎描述。
 * @param type 引擎类型（XPaintEngineType）。
 * @param features 能力位组合。
 * @return 无返回值。
 */
void XPaintEngine_init(XPaintEngine* self, int type, uint32_t features);
/** @brief 查询引擎类型（对标 QPaintEngine::type）。 */
int XPaintEngine_type(const XPaintEngine* self);
/** @brief 查询引擎是否活动（对标 QPaintEngine::isActive）。 */
bool XPaintEngine_isActive(const XPaintEngine* self);
/** @brief 查询能力位（对标 QPaintEngine::hasFeature）。 */
bool XPaintEngine_hasFeature(const XPaintEngine* self,
                             uint32_t feature);

/* ==================== 设备 ==================== */

/** @brief 初始化绘制设备描述。
 * @param self 目标绘制设备。
 * @param devType 设备类型码（XPaintDeviceType）。
 * @param userData 接入对象指针（借用）。
 * @param metric 度量回调；NULL 表示全 0 默认。
 * @param engineType 引擎类型（XPaintEngineType）。
 * @param features 引擎能力位组合。
 * @return 无返回值。
 */
void XPaintDevice_init(XPaintDevice* self, int devType, void* userData,
                       XPaintDeviceMetricFunc metric, int engineType,
                       uint32_t features);
/** @brief 查询设备类型码（对标 QPaintDevice::devType）。
 * @param self 目标绘制设备；可为 NULL。
 * @return 设备类型码。
 */
int XPaintDevice_devType(const XPaintDevice* self);
/** @brief 返回引擎描述（对标 QPaintDevice::paintEngine）。
 * @param self 目标绘制设备；可为 NULL。
 * @return 引擎描述借用指针；无效返回 NULL。
 */
XPaintEngine* XPaintDevice_paintEngine(XPaintDevice* self);
/** @brief 设备宽度（对标 width()）。 */
int XPaintDevice_width(const XPaintDevice* self);
/** @brief 设备高度（对标 height()）。 */
int XPaintDevice_height(const XPaintDevice* self);
/** @brief 物理宽度毫米（对标 widthMM()）。 */
int XPaintDevice_widthMM(const XPaintDevice* self);
/** @brief 物理高度毫米（对标 heightMM()）。 */
int XPaintDevice_heightMM(const XPaintDevice* self);
/** @brief 逻辑 X DPI（对标 logicalDpiX()）。 */
int XPaintDevice_logicalDpiX(const XPaintDevice* self);
/** @brief 逻辑 Y DPI（对标 logicalDpiY()）。 */
int XPaintDevice_logicalDpiY(const XPaintDevice* self);
/** @brief 物理 X DPI（对标 physicalDpiX()）。 */
int XPaintDevice_physicalDpiX(const XPaintDevice* self);
/** @brief 物理 Y DPI（对标 physicalDpiY()）。 */
int XPaintDevice_physicalDpiY(const XPaintDevice* self);
/** @brief 设备像素比（对标 devicePixelRatio()）。 */
int XPaintDevice_devicePixelRatio(const XPaintDevice* self);
/** @brief 设备像素比浮点（对标 devicePixelRatioF()）。 */
float XPaintDevice_devicePixelRatioF(const XPaintDevice* self);
/** @brief 颜色数（对标 colorCount()）。 */
int XPaintDevice_colorCount(const XPaintDevice* self);
/** @brief 位深度（对标 depth()）。 */
int XPaintDevice_depth(const XPaintDevice* self);
/** @brief 度量查询（对标 QPaintDevice::metric）。
 * @param self 目标绘制设备；可为 NULL。
 * @param metric 度量码（XPaintDeviceMetric）。
 * @return 度量值；无效参数返回 0。
 */
int XPaintDevice_metric(const XPaintDevice* self, int metric);

#endif /* XPAINTDEVICE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPAINTDEVICE_H */
