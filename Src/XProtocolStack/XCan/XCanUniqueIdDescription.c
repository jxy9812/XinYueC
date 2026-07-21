#include "XCanUniqueIdDescription.h"
#include <string.h>

void XCanUniqueIdDescription_init(XCanUniqueIdDescription* desc)
{
    if (!desc) return;
    memset(desc, 0, sizeof(XCanUniqueIdDescription));
    desc->m_source = XCanBus_Payload;
    desc->m_startBit = 0;
    desc->m_bitLength = 0;
    desc->m_endian = 1; // 默认大端
}

bool XCanUniqueIdDescription_isValid(const XCanUniqueIdDescription* desc)
{
    if (!desc) return false;
    return desc->m_bitLength > 0 && desc->m_bitLength <= 64;
}

XCanBus_DataSource XCanUniqueIdDescription_source(const XCanUniqueIdDescription* desc)
{
    return desc ? desc->m_source : XCanBus_Payload;
}

void XCanUniqueIdDescription_setSource(XCanUniqueIdDescription* desc, XCanBus_DataSource source)
{
    if (desc) desc->m_source = source;
}

uint16_t XCanUniqueIdDescription_startBit(const XCanUniqueIdDescription* desc)
{
    return desc ? desc->m_startBit : 0;
}

void XCanUniqueIdDescription_setStartBit(XCanUniqueIdDescription* desc, uint16_t bit)
{
    if (desc) desc->m_startBit = bit;
}

uint8_t XCanUniqueIdDescription_bitLength(const XCanUniqueIdDescription* desc)
{
    return desc ? desc->m_bitLength : 0;
}

void XCanUniqueIdDescription_setBitLength(XCanUniqueIdDescription* desc, uint8_t length)
{
    if (desc) desc->m_bitLength = length;
}

uint8_t XCanUniqueIdDescription_endian(const XCanUniqueIdDescription* desc)
{
    return desc ? desc->m_endian : 1;
}

void XCanUniqueIdDescription_setEndian(XCanUniqueIdDescription* desc, uint8_t endian)
{
    if (desc) desc->m_endian = endian;
}
