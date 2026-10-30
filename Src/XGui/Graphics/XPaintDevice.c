/**
 * @file       XPaintDevice.c
 * @brief      绘制设备/引擎描述实现（对标 Qt 6.8 QPaintDevice/QPaintEngine）。
 * @details    与同名头文件的公共 API 一一对应；metric 通过接入类回调
 *             实时计算，本文件只提供查询框架与默认值。
 * @author     XinYueC 团队
 */

#if XGUI_ON

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
    /* 对标 Qt 6.8 QPaintDevice::metric 基类兜底（qpaintdevice.cpp:82-117）：
       设备未实现度量时并非一律 0——PdmDpiX/PdmDpiY 给 72、
       PdmDevicePixelRatio 给 1、PdmNumColors 给 0，其余给 0。
       本实现此前一律返回 0，令未接度量回调的设备报出 0 DPI
       （Qt 口径为 72），影响按 DPI 换算字号的调用方。 */
    switch (metric)
    {
        case XPaintDeviceMetric_PdmDpiX:
        case XPaintDeviceMetric_PdmDpiY:
            return 72;
        case XPaintDeviceMetric_PdmDevicePixelRatio:
            return 1;
        case XPaintDeviceMetric_PdmNumColors:
            return 0;
        default:
            return 0;
    }
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

int XPaintDevice_devicePixelRatioInt(const XPaintDevice* self)
{
    return XPaintDevice_metric(self, XPaintDeviceMetric_PdmDevicePixelRatio);
}

float XPaintDevice_devicePixelRatio(const XPaintDevice* self)
{
    /* 对标 QPaintDevice::devicePixelRatio()（Qt 6.8 qpaintdevice.cpp:43-57）：
       返回 qreal；Qt 由度量回调的 PdmDevicePixelRatioScaled 还原，
       未实现时回退 PdmDevicePixelRatio。本库 PdmDevicePixelRatioScaled
       的定标为「dpr×256」（见 XPaintDeviceMetric 枚举），故此处除以
       256 还原；与度量提供方（XImage/XPicture/XWidget）同口径。 */
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

#endif /* XGUI_ON */
