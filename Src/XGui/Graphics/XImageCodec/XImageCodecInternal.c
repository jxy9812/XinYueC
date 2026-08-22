/*****************************************************************************/
#include "XImageCodecInternal.h"
#include "XImageCodec_config.h"
#include <string.h>

#if XIMAGECODEC_ON

uint16_t XImageCodecInternal_readU16LE(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t XImageCodecInternal_readU32LE(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t XImageCodecInternal_readU16BE(const uint8_t* p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

void XImageCodecInternal_writeU16BE(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

uint32_t XImageCodecInternal_readU32BE(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void XImageCodecInternal_writeU16LE(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

void XImageCodecInternal_writeU32LE(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

void XImageCodecInternal_writeU32BE(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

bool XImageCodecInternal_appendBytes(XByteArray* out,
                                     const void* data, size_t size)
{
    size_t oldSize;
    uint8_t* dst;
    if (!out || (!data && size)) return false;
    oldSize = XByteArray_size_base((const XContainer*)out);
    if (size > SIZE_MAX - oldSize ||
        !XByteArray_resize_base((XVector*)out, oldSize + size)) return false;
    dst = XByteArray_data(out);
    if (size) memcpy(dst + oldSize, data, size);
    return true;
}

#endif /* XIMAGECODEC_ON */
