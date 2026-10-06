// XNetworkInterface.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XNetworkInterface.h"
#include "XDeviceNetwork.h"
#include "XAlgorithm.h"
#include "XMemory.h"
#include <string.h>
#include <stdio.h>
#if XNETWORK_ON
#if XNETWORK_INTERFACE_ON

// ==================== 虚函数表 ====================

static void VXNetworkInterface_deinit(XNetworkInterface* iface);
static void VXNetworkInterface_copy(XNetworkInterface* dest, const XNetworkInterface* src);
static void VXNetworkInterface_move(XNetworkInterface* dest, XNetworkInterface* src);

XVtable* XNetworkInterface_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkInterface)
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkInterface_deinit);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkInterface_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkInterface_move);
        XCLASS_SHOW_SIZE_DEFAULT(XNetworkInterface);
        return XVTABLE_DEFAULT;
}

static void VXNetworkInterface_deinit(XNetworkInterface* iface)
{
    if (!iface) return;
    
    if (iface->name) {
        XClassDelete(iface->name);
        iface->name = NULL;
    }
    
    if (iface->humanReadableName) {
        XClassDelete(iface->humanReadableName);
        iface->humanReadableName = NULL;
    }
    
    if (iface->hardwareAddress) {
        XClassDelete(iface->hardwareAddress);
        iface->hardwareAddress = NULL;
    }
    
    if (iface->addressEntries) {
        XClassDelete(iface->addressEntries);
        iface->addressEntries = NULL;
    }
}

static void VXNetworkInterface_copy(XNetworkInterface* dest, const XNetworkInterface* src)
{
    if (!dest || !src) return;
    
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    if (XClassIsVtableNull(dest))
        XNetworkInterface_init(dest);
    // 复制基本字段
    dest->index = src->index;
    dest->mtu = src->mtu;
    dest->flags = src->flags;
    dest->type = src->type;
    dest->isValid = src->isValid;
    
    // 复制字符串
    if (src->name) 
    {
        if (dest->name)
            XClassCopy(dest->name, src->name);
        else
            dest->name = XString_create_copy(src->name);
    }
    
    if (src->humanReadableName) 
    {
        if (dest->humanReadableName)
            XClassCopy(dest->humanReadableName, src->humanReadableName);
        else
            dest->humanReadableName = XString_create_copy(src->humanReadableName);
    }
    
    if (src->hardwareAddress)
    {
        if (dest->hardwareAddress)
            XClassCopy(dest->hardwareAddress, src->hardwareAddress);
        else
            dest->hardwareAddress = XString_create_copy(src->hardwareAddress);
    }
    
    /* 复制地址条目列表 */
    if (src->addressEntries) 
    {
        if (dest->addressEntries)
            XClassCopy(dest->addressEntries, src->addressEntries);
        else
            dest->addressEntries = XVector_create_copy(src->addressEntries);
    }
}

static void VXNetworkInterface_move(XNetworkInterface* dest, XNetworkInterface* src)
{
    if (!dest || !src) return;

    // 检查目标对象是否已初始化，如果未初始化则先初始化
    if (XClassIsVtableNull(dest))
        XNetworkInterface_init(dest);

    /* [memhunt F1 修复 2026-10-06] payload-only move，禁用 XSwap 整块交换。
     * LSan 证据（XGuiWindowDemo --autotest 优雅退出 / 远程窗口页 62 调枚举）:
     * XNetworkInterface_create_ex:157 分配的接口对象本体(≈80B/接口)每次枚举
     * 全漏(160B/2objs，62 调=124obj/9920B)。根因: XSwap 连 XClass 头一起
     * 字节交换，push 进向量(allInterfaces:301)后原堆块的 is_heap/内存类型
     * 被向量槽位旧内容顶掉，调用侧 XClassDelete(iface)(:302、
     * rs_collectLocalIPv4 等)按 XClass.h:207「非堆对象仅反初始化不释放」
     * 规约失去释放资格 → 每接口每调漏本体。改为仅搬移载荷字段并把 src
     * 诸指针置 NULL: src 恢复为"空壳堆对象"，XClassDelete 走
     * VXNetworkInterface_deinit(空载析构)后正常 free 本体；载荷所有权随
     * 向量槽位，由向量析构(XClass_deinit_base)统一释放。 */
    VXNetworkInterface_deinit(dest); /* dest 旧载荷先释放(含 init 预建的 addressEntries 空向量)，防覆盖泄漏。 */

    // 移动所有字段
    dest->name = src->name;
    dest->humanReadableName = src->humanReadableName;
    dest->hardwareAddress = src->hardwareAddress;
    dest->addressEntries = src->addressEntries;
    dest->index = src->index;
    dest->mtu = src->mtu;
    dest->flags = src->flags;
    dest->type = src->type;
    dest->isValid = src->isValid;

    // 清空源对象(指针置 NULL: XClassDelete(src) 时 deinit 空载，堆块正常释放)
    src->name = NULL;
    src->humanReadableName = NULL;
    src->hardwareAddress = NULL;
    src->addressEntries = NULL;
    src->index = 0;
    src->mtu = 0;
    src->flags = 0;
    src->type = XNetworkInterface_Unknown;
    src->isValid = false;
}

// ==================== 构造函数 ====================

void XNetworkInterface_init(XNetworkInterface* iface)
{
    if (!iface) return;
    
    memset(((XClass*)iface) + 1, 0, sizeof(XNetworkInterface) - sizeof(XClass));
    XClass_init((XClass*)iface);
    XClassGetVtable(iface) = XNetworkInterface_class_init();
    iface->addressEntries = XVector_create(sizeof(XNetworkAddressEntry));
    XContainerSetDataCopyMethod(iface->addressEntries, XClass_copy_base);
    XContainerSetDataMoveMethod(iface->addressEntries, XClass_move_base);
    XContainerSetDataDeinitMethod(iface->addressEntries, XClass_deinit_base);
    iface->type = XNetworkInterface_Unknown;
    iface->isValid = false;
}

XNetworkInterface* XNetworkInterface_create_ex(XMemoryType memory)
{
    XNetworkInterface* iface = (XNetworkInterface*)XMemory_malloc(sizeof(XNetworkInterface), memory);
    if (!iface) return NULL;
    
    XNetworkInterface_init(iface);
    Set_Class_Memory(iface, memory); Set_Class_IsHeap(iface, true);
    return iface;
}

XNetworkInterface* XNetworkInterface_create_copy(const XNetworkInterface* other)
{
    if (!other) return NULL;
    
    XNetworkInterface* iface = XNetworkInterface_create();
    if (!iface) return NULL;
    
    XClassCopy(iface, other);
    return iface;
}

XNetworkInterface* XNetworkInterface_create_move(const XNetworkInterface* other)
{
    if (!other) return NULL;

    XNetworkInterface* iface = XNetworkInterface_create();
    if (!iface) return NULL;

    XClassMove(iface, other);
    return iface;
}

// ==================== 属性访问器 ====================

XString* XNetworkInterface_name(const XNetworkInterface* iface)
{
    return iface ? XString_create_copy(iface->name) : NULL;
}

const XString* XNetworkInterface_name_const(const XNetworkInterface* iface)
{
    return iface ? iface->name : NULL;
}

XString* XNetworkInterface_humanReadableName(const XNetworkInterface* iface)
{
    return iface ? XString_create_copy(iface->humanReadableName) : NULL;
}

const XString* XNetworkInterface_humanReadableName_const(const XNetworkInterface* iface)
{
    return iface ? iface->humanReadableName : NULL;
}

XString* XNetworkInterface_hardwareAddress(const XNetworkInterface* iface)
{
    return iface ? XString_create_copy(iface->hardwareAddress) : NULL;
}

const XString* XNetworkInterface_hardwareAddress_const(const XNetworkInterface* iface)
{
    return iface ? iface->hardwareAddress : NULL;
}

int XNetworkInterface_index(const XNetworkInterface* iface)
{
    return iface ? iface->index : 0;
}

int XNetworkInterface_maximumTransmissionUnit(const XNetworkInterface* iface)
{
    return iface ? iface->mtu : 0;
}

XNetworkInterface_InterfaceFlags XNetworkInterface_flags(const XNetworkInterface* iface)
{
    return iface ? iface->flags : 0;
}

XNetworkInterface_InterfaceType XNetworkInterface_type(const XNetworkInterface* iface)
{
    return iface ? iface->type : XNetworkInterface_Unknown;
}

XVector* XNetworkInterface_addressEntries(const XNetworkInterface* iface)
{
    return iface ? iface->addressEntries : NULL;
}

bool XNetworkInterface_isValid(const XNetworkInterface* iface)
{
    return iface ? iface->isValid : false;
}

// ==================== 静态工具函数 ====================

XVector* XNetworkInterface_allAddresses(void)
{
    XVector* addresses = XVector_create(sizeof(XHostAddress));
    if (!addresses) return NULL;
    XContainerSetDataCopyMethod(addresses, XClass_copy_base);
    XContainerSetDataMoveMethod(addresses, XClass_move_base);
    XContainerSetDataDeinitMethod(addresses, XClass_deinit_base);
    XVector* interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces) return addresses;
    
    size_t i, j, ifaceCount, entryCount;
    ifaceCount = XVector_size_base(interfaces);
    for (i = 0; i < ifaceCount; i++) {
        XNetworkInterface* iface = (XNetworkInterface*)XVector_at_base(interfaces, i);
        if (!iface) continue;
        
        XVector* entries = iface->addressEntries;
        if (!entries) continue;
        
        entryCount = XVector_size_base(entries);
        for (j = 0; j < entryCount; j++) {
            XNetworkAddressEntry* entry = (XNetworkAddressEntry*)XVector_at_base(entries, j);
            if (entry) {
                XHostAddress addr = entry->ip;
                XVector_push_back_move_1_base(addresses, &addr);
            }
        }
    }
    
    XClassDelete(interfaces);
    
    return addresses;
}

XVector* XNetworkInterface_allInterfaces(void)
{
    XVector* result = XVector_create(sizeof(XNetworkInterface));
    if (!result) return NULL;
    XContainerSetDataCopyMethod(result, XClass_copy_base);
    XContainerSetDataMoveMethod(result, XClass_move_base);
    XContainerSetDataDeinitMethod(result, XClass_deinit_base);
    XDeviceNetworkInterfaceIterator iter = XDeviceNetwork_enumInterfacesBegin();
    if (!iter) {
        XClassDelete(result);
        return NULL;
    }   
    
    XNetworkInterface* iface;
    while ((iface = XDeviceNetwork_enumInterfacesNext(iter)) != NULL) {
        /* 接口信息已在平台层填充完毕，直接添加到结果向量 */
        XVector_push_back_move_1_base(result, iface);
        XClassDelete(iface);
    }
    
    XDeviceNetwork_enumInterfacesEnd(iter);
    return result;
}

XNetworkInterface* XNetworkInterface_interfaceFromName(const XString* name)
{
    if (!name) return NULL;
    
    XVector* interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces) return NULL;
    XNetworkInterface* result = NULL;
    size_t i, count = XVector_size_base(interfaces);
    for (i = 0; i < count; i++) 
    {
        XNetworkInterface* iface = (XNetworkInterface*)XVector_at_base(interfaces, i);
        if (iface && iface->name) 
        {
            if (XString_compare(iface->name, name) == XCompare_Equality) 
            {
                result = XNetworkInterface_create_move(iface);
                break;
            }
        }
    }
    
    XClassDelete(interfaces);
    return result;
}

XNetworkInterface* XNetworkInterface_interfaceFromIndex(int index)
{
    if (index <= 0) return NULL;
    
    XVector* interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces) return NULL;
    XNetworkInterface* result = NULL;
    size_t i, count = XVector_size_base(interfaces);
    for (i = 0; i < count; i++) {
        XNetworkInterface* iface = (XNetworkInterface*)XVector_at_base(interfaces, i);
        if (iface && iface->index == index) {
            /* 找到匹配的接口 */
            result = XNetworkInterface_create_move(iface);
            break;
        }
    }
    
    XClassDelete(interfaces);
    return result;
}

int XNetworkInterface_interfaceIndexFromName(const XString* name)
{
    if (!name) return -1;
    
    XDeviceNetworkInterfaceIterator iter = XDeviceNetwork_enumInterfacesBegin();
    if (!iter) return -1;
    
    XNetworkInterface* iface;
    while ((iface = XDeviceNetwork_enumInterfacesNext(iter)) != NULL) {
        if (iface->name) {
            if (XString_compare(iface->name, name) == XCompare_Equality) {
                int index = iface->index;
                XClassDelete(iface);
                XDeviceNetwork_enumInterfacesEnd(iter);
                return index;
            }
        }
        XClassDelete(iface);
    }
    
    XDeviceNetwork_enumInterfacesEnd(iter);
    return -1;
}

XString* XNetworkInterface_interfaceNameFromIndex(int index)
{
    if (index <= 0) return NULL;
    
    XDeviceNetworkInterfaceIterator iter = XDeviceNetwork_enumInterfacesBegin();
    if (!iter) return NULL;
    
    XNetworkInterface* iface;
    while ((iface = XDeviceNetwork_enumInterfacesNext(iter)) != NULL) {
        if (iface->index == index) {
            XString* result = XString_create_copy(iface->name);
            XClassDelete(iface);
            XDeviceNetwork_enumInterfacesEnd(iter);
            return result;
        }
        XClassDelete(iface);
    }
    
    XDeviceNetwork_enumInterfacesEnd(iter);
    return NULL;
}

// ==================== 辅助函数 ====================

bool XNetworkInterface_isUp(const XNetworkInterface* iface)
{
    return iface ? (iface->flags & XNetworkInterface_IsUp) != 0 : false;
}

bool XNetworkInterface_isRunning(const XNetworkInterface* iface)
{
    return iface ? (iface->flags & XNetworkInterface_IsRunning) != 0 : false;
}

bool XNetworkInterface_canBroadcast(const XNetworkInterface* iface)
{
    return iface ? (iface->flags & XNetworkInterface_CanBroadcast) != 0 : false;
}

bool XNetworkInterface_isLoopBack(const XNetworkInterface* iface)
{
    return iface ? (iface->flags & XNetworkInterface_IsLoopBack) != 0 : false;
}

bool XNetworkInterface_isPointToPoint(const XNetworkInterface* iface)
{
    return iface ? (iface->flags & XNetworkInterface_IsPointToPoint) != 0 : false;
}

bool XNetworkInterface_canMulticast(const XNetworkInterface* iface)
{
    return iface ? (iface->flags & XNetworkInterface_CanMulticast) != 0 : false;
}

// ==================== 交换操作 ====================

void XNetworkInterface_swap(XNetworkInterface* iface1, XNetworkInterface* iface2)
{
    if (!iface1 || !iface2) return;

    XNetworkInterface temp;
    memcpy(&temp, iface1, sizeof(XNetworkInterface));
    memcpy(iface1, iface2, sizeof(XNetworkInterface));
    memcpy(iface2, &temp, sizeof(XNetworkInterface));
}

// ==================== 网卡配置（DHCP / 静态 IP） ====================
// XNetworkInterfaceConfig 值语义类 + 设备层查询/设置转接；平台能力与
// 安全语义见 XDeviceNetwork.h「网卡配置」节。

static void VXNetworkInterfaceConfig_deinit(XNetworkInterfaceConfig* config);
static void VXNetworkInterfaceConfig_copy(XNetworkInterfaceConfig* dest,
                                          const XNetworkInterfaceConfig* src);
static void VXNetworkInterfaceConfig_move(XNetworkInterfaceConfig* dest,
                                          XNetworkInterfaceConfig* src);

XVtable* XNetworkInterfaceConfig_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkInterfaceConfig)
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkInterfaceConfig_deinit);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkInterfaceConfig_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkInterfaceConfig_move);
        XCLASS_SHOW_SIZE_DEFAULT(XNetworkInterfaceConfig);
        return XVTABLE_DEFAULT;
}

static void VXNetworkInterfaceConfig_deinit(XNetworkInterfaceConfig* config)
{
    if (!config) return;
    if (config->m_friendlyName) {
        XClassDelete(config->m_friendlyName);
        config->m_friendlyName = NULL;
    }
    XClassDeinit(&config->m_ipv4Address);
    XClassDeinit(&config->m_ipv4Netmask);
    XClassDeinit(&config->m_ipv4Gateway);
    XClassDeinit(&config->m_ipv6Address);
    XClassDeinit(&config->m_ipv6Gateway);
    XClassDeinit(&config->m_dhcpServer);
    XClassDeinit(&config->m_dnsPrimary);
    XClassDeinit(&config->m_dnsSecondary);
}

static void VXNetworkInterfaceConfig_copy(XNetworkInterfaceConfig* dest,
                                          const XNetworkInterfaceConfig* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest))
        XNetworkInterfaceConfig_init(dest);
    if (src->m_friendlyName) {
        if (dest->m_friendlyName)
            XClassCopy(dest->m_friendlyName, src->m_friendlyName);
        else
            dest->m_friendlyName = XString_create_copy(src->m_friendlyName);
    }
    XClassCopy(&dest->m_ipv4Address, &src->m_ipv4Address);
    XClassCopy(&dest->m_ipv4Netmask, &src->m_ipv4Netmask);
    XClassCopy(&dest->m_ipv4Gateway, &src->m_ipv4Gateway);
    XClassCopy(&dest->m_ipv6Address, &src->m_ipv6Address);
    XClassCopy(&dest->m_ipv6Gateway, &src->m_ipv6Gateway);
    XClassCopy(&dest->m_dhcpServer, &src->m_dhcpServer);
    XClassCopy(&dest->m_dnsPrimary, &src->m_dnsPrimary);
    XClassCopy(&dest->m_dnsSecondary, &src->m_dnsSecondary);
    dest->m_ifIndex = src->m_ifIndex;
    dest->m_ipv6PrefixLength = src->m_ipv6PrefixLength;
    dest->m_dhcpEnabled = src->m_dhcpEnabled;
    dest->m_operUp = src->m_operUp;
}

static void VXNetworkInterfaceConfig_move(XNetworkInterfaceConfig* dest,
                                          XNetworkInterfaceConfig* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest))
        XNetworkInterfaceConfig_init(dest);
    XSwap(dest, src, sizeof(XNetworkInterfaceConfig));
}

void XNetworkInterfaceConfig_init(XNetworkInterfaceConfig* config)
{
    if (!config) return;
    memset(((XClass*)config) + 1, 0,
           sizeof(XNetworkInterfaceConfig) - sizeof(XClass));
    XClass_init((XClass*)config);
    XClassGetVtable(config) = XNetworkInterfaceConfig_class_init();
    XHostAddress_init(&config->m_ipv4Address);
    XHostAddress_init(&config->m_ipv4Netmask);
    XHostAddress_init(&config->m_ipv4Gateway);
    XHostAddress_init(&config->m_ipv6Address);
    XHostAddress_init(&config->m_ipv6Gateway);
    XHostAddress_init(&config->m_dhcpServer);
    XHostAddress_init(&config->m_dnsPrimary);
    XHostAddress_init(&config->m_dnsSecondary);
    config->m_ifIndex = 0;
    config->m_ipv6PrefixLength = 0;
    config->m_dhcpEnabled = false;
    config->m_operUp = false;
}

XNetworkInterfaceConfig* XNetworkInterfaceConfig_create_ex(XMemoryType memory)
{
    XNetworkInterfaceConfig* config =
        (XNetworkInterfaceConfig*)XMemory_malloc(
            sizeof(XNetworkInterfaceConfig), memory);
    if (!config) return NULL;
    XNetworkInterfaceConfig_init(config);
    Set_Class_Memory(config, memory); Set_Class_IsHeap(config, true);
    return config;
}

/** @brief 用设备层查询结果整体覆盖业务层快照（旧内容先释放；全拷贝语义，
 *         设备层结果随后由调用方 freeInterfaceConfig 统一释放）。 */
static void XNI_assignDeviceConfig(XNetworkInterfaceConfig* dest,
                                   const XDeviceNetworkInterfaceConfig* src)
{
    VXNetworkInterfaceConfig_deinit(dest);
    if (src->friendlyName)
        dest->m_friendlyName = XString_create_copy(src->friendlyName);
    XClassCopy(&dest->m_ipv4Address, &src->ipv4Address);
    XClassCopy(&dest->m_ipv4Netmask, &src->ipv4Netmask);
    XClassCopy(&dest->m_ipv4Gateway, &src->ipv4Gateway);
    XClassCopy(&dest->m_ipv6Address, &src->ipv6Address);
    XClassCopy(&dest->m_ipv6Gateway, &src->ipv6Gateway);
    XClassCopy(&dest->m_dhcpServer, &src->dhcpServer);
    XClassCopy(&dest->m_dnsPrimary, &src->dnsPrimary);
    XClassCopy(&dest->m_dnsSecondary, &src->dnsSecondary);
    dest->m_dhcpEnabled = src->dhcpEnabled;
    dest->m_operUp = src->operUp;
    dest->m_ipv6PrefixLength = src->ipv6PrefixLength;
}

bool XNetworkInterface_configSupported(void)
{
    return XDeviceNetwork_interfaceConfigSupported();
}

bool XNetworkInterface_queryConfig(int ifIndex,
                                   XNetworkInterfaceConfig* outConfig)
{
    XDeviceNetworkInterfaceConfig device;
    bool ok;
    if (!outConfig || ifIndex < 0) return false;
    if (XClassIsVtableNull(outConfig))
        XNetworkInterfaceConfig_init(outConfig);
    memset(&device, 0, sizeof(device));
    ok = XDeviceNetwork_queryInterfaceConfig((uint32_t)ifIndex, &device);
    if (!ok) return false;
    XNI_assignDeviceConfig(outConfig, &device);
    XDeviceNetwork_freeInterfaceConfig(&device);
    outConfig->m_ifIndex = ifIndex;
    return true;
}

bool XNetworkInterface_setDhcpMode(int ifIndex)
{
    if (ifIndex < 0) return false;
    return XDeviceNetwork_setInterfaceDhcp((uint32_t)ifIndex);
}

bool XNetworkInterface_setStaticMode(int ifIndex, const char* ipv4Address,
                                     const char* ipv4Netmask,
                                     const char* ipv4Gateway,
                                     const char* dnsPrimary,
                                     const char* dnsSecondary)
{
    if (ifIndex < 0) return false;
    return XDeviceNetwork_setInterfaceStatic((uint32_t)ifIndex,
                                             ipv4Address, ipv4Netmask,
                                             ipv4Gateway, dnsPrimary,
                                             dnsSecondary);
}

bool XNetworkInterface_setStaticIpv6(int ifIndex, const char* ipv6Address,
                                     int prefixLength,
                                     const char* ipv6Gateway)
{
    if (ifIndex < 0) return false;
    return XDeviceNetwork_setInterfaceStaticIpv6((uint32_t)ifIndex,
                                                 ipv6Address, prefixLength,
                                                 ipv6Gateway);
}
#endif // XNETWORK_INTERFACE_ON
#endif /* XNETWORK_ON */
