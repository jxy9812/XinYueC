/*
 * @file       XColorSpace.c
 * @brief      XColorSpace 色彩空间值类型实现
 * @note       本文件只处理不持有堆资源的色彩空间元数据，便于 C99 值复制。
 */
#include "XColorSpace.h"

#include <math.h>
#include <string.h>

static XPointF xcolorspace_point(float x, float y)
{
    XPointF result = {x, y};
    return result;
}

static bool xcolorspace_valid_xy(XPointF point)
{
    return isfinite((double)point.x) && isfinite((double)point.y) &&
           point.x >= 0.0f && point.y >= 0.0f &&
           point.x <= 1.0f && point.y <= 1.0f &&
           point.x + point.y <= 1.0f;
}

static bool xcolorspace_valid_primaries(const XColorSpacePrimariesData* data)
{
    return data && xcolorspace_valid_xy(data->m_whitePoint) &&
           xcolorspace_valid_xy(data->m_redPoint) &&
           xcolorspace_valid_xy(data->m_greenPoint) &&
           xcolorspace_valid_xy(data->m_bluePoint) &&
           data->m_whitePoint.y > 0.0f && data->m_redPoint.y > 0.0f &&
           data->m_greenPoint.y > 0.0f && data->m_bluePoint.y > 0.0f;
}

static XColorSpacePrimariesData xcolorspace_predefined_primaries(
    XColorSpacePrimaries primaries)
{
    XColorSpacePrimariesData result;
    result.m_whitePoint = xcolorspace_point(0.0f, 0.0f);
    result.m_redPoint = xcolorspace_point(0.0f, 0.0f);
    result.m_greenPoint = xcolorspace_point(0.0f, 0.0f);
    result.m_bluePoint = xcolorspace_point(0.0f, 0.0f);
    switch (primaries)
    {
        case XColorSpacePrimaries_SRgb:
            result.m_whitePoint = xcolorspace_point(0.3127f, 0.3290f);
            result.m_redPoint = xcolorspace_point(0.640f, 0.330f);
            result.m_greenPoint = xcolorspace_point(0.300f, 0.600f);
            result.m_bluePoint = xcolorspace_point(0.150f, 0.060f);
            break;
        case XColorSpacePrimaries_AdobeRgb:
            result.m_whitePoint = xcolorspace_point(0.3127f, 0.3290f);
            result.m_redPoint = xcolorspace_point(0.640f, 0.330f);
            result.m_greenPoint = xcolorspace_point(0.210f, 0.710f);
            result.m_bluePoint = xcolorspace_point(0.150f, 0.060f);
            break;
        case XColorSpacePrimaries_DciP3D65:
            result.m_whitePoint = xcolorspace_point(0.3127f, 0.3290f);
            result.m_redPoint = xcolorspace_point(0.680f, 0.320f);
            result.m_greenPoint = xcolorspace_point(0.265f, 0.690f);
            result.m_bluePoint = xcolorspace_point(0.150f, 0.060f);
            break;
        case XColorSpacePrimaries_ProPhotoRgb:
            result.m_whitePoint = xcolorspace_point(0.3457f, 0.3585f);
            result.m_redPoint = xcolorspace_point(0.7347f, 0.2653f);
            result.m_greenPoint = xcolorspace_point(0.1596f, 0.8404f);
            result.m_bluePoint = xcolorspace_point(0.0366f, 0.0001f);
            break;
        case XColorSpacePrimaries_Bt2020:
            result.m_whitePoint = xcolorspace_point(0.3127f, 0.3290f);
            result.m_redPoint = xcolorspace_point(0.708f, 0.292f);
            result.m_greenPoint = xcolorspace_point(0.170f, 0.797f);
            result.m_bluePoint = xcolorspace_point(0.131f, 0.046f);
            break;
        default:
            break;
    }
    return result;
}

static float xcolorspace_default_gamma(XColorSpaceTransferFunction transfer)
{
    switch (transfer)
    {
        case XColorSpaceTransfer_Linear: return 1.0f;
        case XColorSpaceTransfer_SRgb: return 2.31f;
        case XColorSpaceTransfer_ProPhotoRgb: return 1.8f;
        case XColorSpaceTransfer_Bt2020: return 2.1f;
        case XColorSpaceTransfer_Gamma22: return 2.2f;
        case XColorSpaceTransfer_Gamma28: return 2.8f;
        default: return 0.0f;
    }
}

static bool xcolorspace_valid_transfer(XColorSpaceTransferFunction transfer,
                                        float gamma)
{
    switch (transfer)
    {
        case XColorSpaceTransfer_Linear:
        case XColorSpaceTransfer_SRgb:
        case XColorSpaceTransfer_ProPhotoRgb:
        case XColorSpaceTransfer_Bt2020:
        case XColorSpaceTransfer_St2084:
        case XColorSpaceTransfer_Hlg:
            return true;
        case XColorSpaceTransfer_Gamma:
            return isfinite((double)gamma) && gamma > 0.0f;
        case XColorSpaceTransfer_Gamma22:
        case XColorSpaceTransfer_Gamma28:
            return true;
        default:
            return false;
    }
}

static void xcolorspace_set_description(XColorSpace* self, const char* text)
{
    size_t length;
    if (!self) return;
    self->m_description[0] = '\0';
    if (!text) return;
    length = strlen(text);
    if (length >= sizeof(self->m_description))
        length = sizeof(self->m_description) - 1u;
    memcpy(self->m_description, text, length);
    self->m_description[length] = '\0';
}

XColorSpace XColorSpace_create(void)
{
    XColorSpace result;
    memset(&result, 0, sizeof(result));
    result.m_primaries = XColorSpacePrimaries_Custom;
    result.m_transferFunction = XColorSpaceTransfer_Custom;
    result.m_namedColorSpace = XColorSpaceNamed_Unknown;
    result.m_transformModel = XColorSpaceTransform_ThreeComponentMatrix;
    result.m_colorModel = XColorSpaceModel_Undefined;
    return result;
}

XColorSpace XColorSpace_create_ex(XColorSpacePrimaries primaries,
                                  XColorSpaceTransferFunction transferFunction,
                                  float gamma)
{
    XColorSpace result = XColorSpace_create();
    result.m_primaries = primaries;
    result.m_transferFunction = transferFunction;
    result.m_colorModel = XColorSpaceModel_Rgb;
    result.m_primariesData = xcolorspace_predefined_primaries(primaries);
    if (transferFunction == XColorSpaceTransfer_Gamma22 ||
        transferFunction == XColorSpaceTransfer_Gamma28)
    {
        if (!(gamma > 0.0f)) gamma = xcolorspace_default_gamma(transferFunction);
    }
    else if (!(gamma > 0.0f))
    {
        gamma = xcolorspace_default_gamma(transferFunction);
    }
    result.m_gamma = gamma;
    result.m_valid = primaries != XColorSpacePrimaries_Custom &&
                     xcolorspace_valid_primaries(&result.m_primariesData) &&
                     xcolorspace_valid_transfer(transferFunction, gamma);
    if (result.m_valid && primaries == XColorSpacePrimaries_SRgb &&
        transferFunction == XColorSpaceTransfer_SRgb)
    {
        result.m_namedColorSpace = XColorSpaceNamed_SRgb;
        xcolorspace_set_description(&result, "sRGB");
    }
    return result;
}

XColorSpace XColorSpace_create_named(XColorSpaceNamedColorSpace namedColorSpace)
{
    XColorSpace result = XColorSpace_create();
    switch (namedColorSpace)
    {
        case XColorSpaceNamed_SRgb:
            result = XColorSpace_create_ex(XColorSpacePrimaries_SRgb,
                                           XColorSpaceTransfer_SRgb, 0.0f);
            xcolorspace_set_description(&result, "sRGB");
            break;
        case XColorSpaceNamed_SRgbLinear:
            result = XColorSpace_create_ex(XColorSpacePrimaries_SRgb,
                                           XColorSpaceTransfer_Linear, 1.0f);
            xcolorspace_set_description(&result, "Linear sRGB");
            break;
        case XColorSpaceNamed_AdobeRgb:
            result = XColorSpace_create_ex(XColorSpacePrimaries_AdobeRgb,
                                           XColorSpaceTransfer_Gamma,
                                           2.19921875f);
            xcolorspace_set_description(&result, "Adobe RGB");
            break;
        case XColorSpaceNamed_DisplayP3:
            result = XColorSpace_create_ex(XColorSpacePrimaries_DciP3D65,
                                           XColorSpaceTransfer_SRgb, 0.0f);
            xcolorspace_set_description(&result, "Display P3");
            break;
        case XColorSpaceNamed_ProPhotoRgb:
            result = XColorSpace_create_ex(XColorSpacePrimaries_ProPhotoRgb,
                                           XColorSpaceTransfer_ProPhotoRgb, 0.0f);
            xcolorspace_set_description(&result, "ProPhoto RGB");
            break;
        case XColorSpaceNamed_Bt2020:
            result = XColorSpace_create_ex(XColorSpacePrimaries_Bt2020,
                                           XColorSpaceTransfer_Bt2020, 0.0f);
            xcolorspace_set_description(&result, "BT.2020");
            break;
        case XColorSpaceNamed_Bt2100Pq:
            result = XColorSpace_create_ex(XColorSpacePrimaries_Bt2020,
                                           XColorSpaceTransfer_St2084, 0.0f);
            xcolorspace_set_description(&result, "BT.2100(PQ)");
            break;
        case XColorSpaceNamed_Bt2100Hlg:
            result = XColorSpace_create_ex(XColorSpacePrimaries_Bt2020,
                                           XColorSpaceTransfer_Hlg, 0.0f);
            xcolorspace_set_description(&result, "BT.2100(HLG)");
            break;
        default:
            break;
    }
    if (result.m_valid) result.m_namedColorSpace = namedColorSpace;
    return result;
}

XColorSpace XColorSpace_create_custom(const XColorSpacePrimariesData* primaries,
                                      XColorSpaceTransferFunction transferFunction,
                                      float gamma)
{
    XColorSpace result = XColorSpace_create();
    if (!xcolorspace_valid_primaries(primaries) ||
        !xcolorspace_valid_transfer(transferFunction, gamma))
        return result;
    result.m_primaries = XColorSpacePrimaries_Custom;
    result.m_transferFunction = transferFunction;
    result.m_gamma = gamma > 0.0f ? gamma : xcolorspace_default_gamma(transferFunction);
    result.m_valid = true;
    result.m_colorModel = XColorSpaceModel_Rgb;
    result.m_primariesData = *primaries;
    return result;
}

XColorSpace XColorSpace_create_gray(XPointF whitePoint,
                                    XColorSpaceTransferFunction transferFunction,
                                    float gamma)
{
    XColorSpace result = XColorSpace_create();
    if (!xcolorspace_valid_xy(whitePoint) || whitePoint.y <= 0.0f ||
        !xcolorspace_valid_transfer(transferFunction, gamma))
        return result;
    result.m_transferFunction = transferFunction;
    result.m_gamma = gamma > 0.0f ? gamma : xcolorspace_default_gamma(transferFunction);
    result.m_valid = true;
    result.m_colorModel = XColorSpaceModel_Gray;
    result.m_primariesData.m_whitePoint = whitePoint;
    return result;
}

XColorSpace XColorSpace_sRgb(void)
{
    return XColorSpace_create_named(XColorSpaceNamed_SRgb);
}

XColorSpace XColorSpace_sRgbLinear(void)
{
    return XColorSpace_create_named(XColorSpaceNamed_SRgbLinear);
}

XColorSpace XColorSpace_adobeRgb(void)
{
    return XColorSpace_create_named(XColorSpaceNamed_AdobeRgb);
}

XColorSpace XColorSpace_dciP3(void)
{
    return XColorSpace_create_named(XColorSpaceNamed_DisplayP3);
}

bool XColorSpace_isValid(const XColorSpace* self)
{
    return self && self->m_valid;
}

bool XColorSpace_isSRgb(const XColorSpace* self)
{
    return XColorSpace_isValid(self) &&
           self->m_primaries == XColorSpacePrimaries_SRgb &&
           self->m_transferFunction == XColorSpaceTransfer_SRgb;
}

XColorSpacePrimaries XColorSpace_primaries(const XColorSpace* self)
{
    return self ? self->m_primaries : XColorSpacePrimaries_Custom;
}

XColorSpaceTransferFunction XColorSpace_transferFunction(const XColorSpace* self)
{
    return self ? self->m_transferFunction : XColorSpaceTransfer_Custom;
}

float XColorSpace_gamma(const XColorSpace* self)
{
    return self ? self->m_gamma : 0.0f;
}

XColorSpaceTransformModel XColorSpace_transformModel(const XColorSpace* self)
{
    return self ? self->m_transformModel : XColorSpaceTransform_ThreeComponentMatrix;
}

XColorSpaceColorModel XColorSpace_colorModel(const XColorSpace* self)
{
    return self ? self->m_colorModel : XColorSpaceModel_Undefined;
}

bool XColorSpace_primariesData(const XColorSpace* self,
                               XColorSpacePrimariesData* out)
{
    if (!self || !out || !self->m_valid ||
        self->m_colorModel != XColorSpaceModel_Rgb)
        return false;
    *out = self->m_primariesData;
    return xcolorspace_valid_primaries(out);
}

const char* XColorSpace_description(const XColorSpace* self)
{
    return self ? self->m_description : "";
}

void XColorSpace_setDescription(XColorSpace* self, const char* description)
{
    xcolorspace_set_description(self, description);
}

bool XColorSpace_equals(const XColorSpace* left, const XColorSpace* right)
{
    const float gammaTolerance = 1.0f / 512.0f;
    if (!left || !right) return left == right;
    if (left->m_valid != right->m_valid || left->m_colorModel != right->m_colorModel ||
        left->m_transformModel != right->m_transformModel)
        return false;
    if (!left->m_valid)
        return left->m_primaries == right->m_primaries &&
               left->m_transferFunction == right->m_transferFunction;
    if (left->m_primaries != XColorSpacePrimaries_Custom &&
        right->m_primaries != XColorSpacePrimaries_Custom)
    {
        if (left->m_primaries != right->m_primaries) return false;
    }
    else if (memcmp(&left->m_primariesData, &right->m_primariesData,
                    sizeof(left->m_primariesData)) != 0)
        return false;
    if (left->m_transferFunction != right->m_transferFunction) return false;
    if (left->m_transferFunction == XColorSpaceTransfer_Gamma ||
        left->m_transferFunction == XColorSpaceTransfer_Gamma22 ||
        left->m_transferFunction == XColorSpaceTransfer_Gamma28)
        return fabsf(left->m_gamma - right->m_gamma) <= gammaTolerance;
    return true;
}
