/*
 * @file       XColorSpace.c
 * @brief      XColorSpace 色彩空间值类型实现
 */
#include "XColorSpace.h"

XColorSpace XColorSpace_create(void)
{
    XColorSpace result = { XColorSpacePrimaries_Unknown,
                           XColorSpaceTransfer_Unknown, 0.0f, false };
    return result;
}

XColorSpace XColorSpace_create_ex(XColorSpacePrimaries primaries,
                                  XColorSpaceTransferFunction transferFunction,
                                  float gamma)
{
    XColorSpace result = { primaries, transferFunction, gamma, true };
    if (primaries == XColorSpacePrimaries_Unknown ||
        transferFunction == XColorSpaceTransfer_Unknown)
        result.m_valid = false;
    if (transferFunction == XColorSpaceTransfer_Gamma22 && gamma <= 0.0f)
        result.m_gamma = 2.2f;
    else if (transferFunction == XColorSpaceTransfer_Gamma28 && gamma <= 0.0f)
        result.m_gamma = 2.8f;
    return result;
}

XColorSpace XColorSpace_sRgb(void)
{
    return XColorSpace_create_ex(XColorSpacePrimaries_SRgb,
                                 XColorSpaceTransfer_SRgb, 0.0f);
}

XColorSpace XColorSpace_sRgbLinear(void)
{
    return XColorSpace_create_ex(XColorSpacePrimaries_SRgbLinear,
                                 XColorSpaceTransfer_Linear, 1.0f);
}

XColorSpace XColorSpace_adobeRgb(void)
{
    return XColorSpace_create_ex(XColorSpacePrimaries_AdobeRgb,
                                 XColorSpaceTransfer_Gamma22, 2.2f);
}

XColorSpace XColorSpace_dciP3(void)
{
    return XColorSpace_create_ex(XColorSpacePrimaries_DciP3,
                                 XColorSpaceTransfer_Gamma22, 2.6f);
}

bool XColorSpace_isValid(const XColorSpace* self)
{
    return self && self->m_valid;
}

bool XColorSpace_isSRgb(const XColorSpace* self)
{
    return XColorSpace_isValid(self) && self->m_primaries == XColorSpacePrimaries_SRgb &&
           self->m_transferFunction == XColorSpaceTransfer_SRgb;
}

bool XColorSpace_equals(const XColorSpace* left, const XColorSpace* right)
{
    if (!left || !right) return left == right;
    return left->m_valid == right->m_valid &&
           left->m_primaries == right->m_primaries &&
           left->m_transferFunction == right->m_transferFunction &&
           left->m_gamma == right->m_gamma;
}
