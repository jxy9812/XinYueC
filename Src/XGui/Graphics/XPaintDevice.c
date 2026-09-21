/**
 * @file       XPaintDevice.c
 * @brief      绘制设备/引擎描述实现（对标 Qt 6.8 QPaintDevice/QPaintEngine）。
 * @details    与同名头文件的公共 API 一一对应；metric 通过接入类回调
 *             实时计算，本文件只提供查询框架与默认值。
 * @author     XinYueC 团队
 */

#include "XPaintDevice.h"

#if XPAINTDEVICE_ON

/* ==================== 引擎 ==================== */

void XPaintEngine_init(XPaintEngine* self, int type, uint32_t features)
{
    if (!self) return;
    self->m_type = type;
    self->m_active = false;
    self->m_features = features;
}

int XPaintEngine_type(const XPaintEngine* self)
{
    return self ? self->m_type : XPaintEngineType_Raster;
}

bool XPaintEngine_isActive(const XPaintEngine* self)
{
    return self ? self->m_active : false;
}

bool XPaintEngine_hasFeature(const XPaintEngine* self, uint32_t feature)
{
    return self ? (self->m_features & feature) == feature : false;
}

/* ==================== 设备 ==================== */

void XPaintDevice_init(XPaintDevice* self, int devType, void* userData,
                       XPaintDeviceMetricFunc metric, int engineType,
                       uint32_t features)
{
    if (!self) return;
    self->m_devType = devType;
    self->m_userData = userData;
    self->m_metric = metric;
    self->m_beginPainter = NULL; /* 默认不开放；接入类经 setter 启用。 */
    XPaintEngine_init(&self->m_engine, engineType, features);
}

void XPaintDevice_setBeginPainter(XPaintDevice* self,
                                  XPaintDeviceBeginFunc begin)
{
    if (self) self->m_beginPainter = begin;
}

int XPaintDevice_devType(const XPaintDevice* self)
{
    return self ? self->m_devType : 0;
}

XPaintEngine* XPaintDevice_paintEngine(XPaintDevice* self)
{
    return self ? &self->m_engine : NULL;
}

int XPaintDevice_metric(const XPaintDevice* self, int metric)
{
    if (!self) return 0;
    if (self->m_metric && self->m_userData)
        return self->m_metric(self->m_userData, metric);
    return 0;
}

int XPaintDevice_width(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmWidth);
}

int XPaintDevice_height(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmHeight);
}

int XPaintDevice_widthMM(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmWidthMM);
}

int XPaintDevice_heightMM(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmHeightMM);
}

int XPaintDevice_logicalDpiX(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmDpiX);
}

int XPaintDevice_logicalDpiY(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmDpiY);
}

int XPaintDevice_physicalDpiX(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmPhysicalDpiX);
}

int XPaintDevice_physicalDpiY(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmPhysicalDpiY);
}

int XPaintDevice_devicePixelRatio(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmDevicePixelRatio);
}

float XPaintDevice_devicePixelRatioF(const XPaintDevice* self)
{
    if (!self) return 1.0f;
    if (self->m_metric && self->m_userData)
        return (float)self->m_metric(
                   self->m_userData,
                   XPaintDeviceMetric_PdmDevicePixelRatioScaled) / 256.0f;
    return 1.0f;
}

int XPaintDevice_colorCount(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmNumColors);
}

int XPaintDevice_depth(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmDepth);
}

#endif /* XPAINTDEVICE_ON */
