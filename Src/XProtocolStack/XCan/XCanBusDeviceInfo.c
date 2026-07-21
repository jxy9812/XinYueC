#include "XCanBusDeviceInfo.h"
#include "XMemory.h"
#include <string.h>

// =============== 初始化与清理 ===============

void XCanBusDeviceInfo_init(XCanBusDeviceInfo* info)
{
    if (!info) return;
    memset(info, 0, sizeof(XCanBusDeviceInfo));
    info->m_channel = 0;
    info->m_hasFlexibleDataRate = false;
    info->m_isVirtual = false;
}

void XCanBusDeviceInfo_deinit(XCanBusDeviceInfo* info)
{
    if (!info) return;
    if (info->m_plugin) {
        XString_delete_base(info->m_plugin);
        info->m_plugin = NULL;
    }
    if (info->m_name) {
        XString_delete_base(info->m_name);
        info->m_name = NULL;
    }
    if (info->m_description) {
        XString_delete_base(info->m_description);
        info->m_description = NULL;
    }
    if (info->m_serialNumber) {
        XString_delete_base(info->m_serialNumber);
        info->m_serialNumber = NULL;
    }
    if (info->m_alias) {
        XString_delete_base(info->m_alias);
        info->m_alias = NULL;
    }
}

void XCanBusDeviceInfo_copy(XCanBusDeviceInfo* dest, const XCanBusDeviceInfo* src)
{
    if (!dest || !src) return;

    // 先释放目标原有资源
    XCanBusDeviceInfo_deinit(dest);

    // 深拷贝
    if (src->m_plugin)
        dest->m_plugin = XString_create_copy(src->m_plugin);
    if (src->m_name)
        dest->m_name = XString_create_copy(src->m_name);
    if (src->m_description)
        dest->m_description = XString_create_copy(src->m_description);
    if (src->m_serialNumber)
        dest->m_serialNumber = XString_create_copy(src->m_serialNumber);
    if (src->m_alias)
        dest->m_alias = XString_create_copy(src->m_alias);
    dest->m_channel = src->m_channel;
    dest->m_hasFlexibleDataRate = src->m_hasFlexibleDataRate;
    dest->m_isVirtual = src->m_isVirtual;
}

// =============== 属性访问 ===============

XString* XCanBusDeviceInfo_plugin(const XCanBusDeviceInfo* info)
{
    if (!info) return XString_create();
    if (info->m_plugin)
        return XString_create_copy(info->m_plugin);
    return XString_create();
}

XString* XCanBusDeviceInfo_name(const XCanBusDeviceInfo* info)
{
    if (!info) return XString_create();
    if (info->m_name)
        return XString_create_copy(info->m_name);
    return XString_create();
}

XString* XCanBusDeviceInfo_description(const XCanBusDeviceInfo* info)
{
    if (!info) return XString_create();
    if (info->m_description)
        return XString_create_copy(info->m_description);
    return XString_create();
}

XString* XCanBusDeviceInfo_serialNumber(const XCanBusDeviceInfo* info)
{
    if (!info) return XString_create();
    if (info->m_serialNumber)
        return XString_create_copy(info->m_serialNumber);
    return XString_create();
}

XString* XCanBusDeviceInfo_alias(const XCanBusDeviceInfo* info)
{
    if (!info) return XString_create();
    if (info->m_alias)
        return XString_create_copy(info->m_alias);
    return XString_create();
}

int XCanBusDeviceInfo_channel(const XCanBusDeviceInfo* info)
{
    return info ? info->m_channel : 0;
}

bool XCanBusDeviceInfo_hasFlexibleDataRate(const XCanBusDeviceInfo* info)
{
    return info ? info->m_hasFlexibleDataRate : false;
}

bool XCanBusDeviceInfo_isVirtual(const XCanBusDeviceInfo* info)
{
    return info ? info->m_isVirtual : false;
}
