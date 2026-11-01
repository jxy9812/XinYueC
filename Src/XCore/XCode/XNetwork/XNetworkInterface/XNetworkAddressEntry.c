// XNetworkAddressEntry.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XNetworkAddressEntry.h"
#include "XMemory.h"
#include "XHostAddress.h"
#include <string.h>
#if XNETWORK_ON
#if XNETWORK_INTERFACE_ON

// ==================== 虚函数表 ====================

static void VXNetworkAddressEntry_deinit(XNetworkAddressEntry* entry)
{
    if (!entry) return;
    // 释放内嵌的 XHostAddress 对象
    XClassDeinit(&entry->m_ip);
    XClassDeinit(&entry->m_netmask);
    XClassDeinit(&entry->m_broadcast);
}

static void VXNetworkAddressEntry_copy(XNetworkAddressEntry* dest, const XNetworkAddressEntry* src)
{
    if (!dest || !src) return;
    if (dest == src) return;  /* 自拷贝直接返回，避免先释放自身成员再读取已释放内容 */

    // 检查目标对象是否已初始化，如果未初始化则先初始化
    if (XClassIsVtableNull(dest))
        XNetworkAddressEntry_init(dest);
    
    // 先释放目标对象的旧值
    XClassDeinit(&dest->m_ip);
    XClassDeinit(&dest->m_netmask);
    XClassDeinit(&dest->m_broadcast);
    
    // 复制源对象的值
    XClassCopy(&dest->m_ip, &src->m_ip);
    XClassCopy(&dest->m_netmask, &src->m_netmask);
    XClassCopy(&dest->m_broadcast, &src->m_broadcast);
    dest->m_broadcastIsValid = src->m_broadcastIsValid;
    
    dest->m_preferredLifetime = src->m_preferredLifetime;
    dest->m_validityLifetime = src->m_validityLifetime;
    dest->m_lifetimeKnown = src->m_lifetimeKnown;
    
    dest->m_dnsEligibilityStatus = src->m_dnsEligibilityStatus;
    dest->m_isPermanent = src->m_isPermanent;
}

static void VXNetworkAddressEntry_move(XNetworkAddressEntry* dest, XNetworkAddressEntry* src)
{
    if (!dest || !src) return;
    if (dest == src) return;  /* 自移动直接返回，避免清空自身内容 */

    // 检查目标对象是否已初始化，如果未初始化则先初始化
    if (XClassIsVtableNull(dest))
        XNetworkAddressEntry_init(dest);
   
    // 移动语义：转移资源
    XClassMove(&dest->m_ip, &src->m_ip);
    XClassMove(&dest->m_netmask, &src->m_netmask);
    XClassMove(&dest->m_broadcast, &src->m_broadcast);
    dest->m_broadcastIsValid = src->m_broadcastIsValid;
    
    dest->m_preferredLifetime = src->m_preferredLifetime;
    dest->m_validityLifetime = src->m_validityLifetime;
    dest->m_lifetimeKnown = src->m_lifetimeKnown;
    
    dest->m_dnsEligibilityStatus = src->m_dnsEligibilityStatus;
    dest->m_isPermanent = src->m_isPermanent;
    
    // 清空源对象
    src->m_broadcastIsValid = false;
    src->m_preferredLifetime = -1;
    src->m_validityLifetime = -1;
    src->m_lifetimeKnown = false;
    src->m_dnsEligibilityStatus = XNetworkAddressEntry_DnsEligibilityUnknown;
    src->m_isPermanent = true;
}

XVtable* XNetworkAddressEntry_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkAddressEntry)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkAddressEntry_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkAddressEntry_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkAddressEntry_move);
    return XVTABLE_DEFAULT;
}

// ==================== 构造函数 ====================

void XNetworkAddressEntry_init(XNetworkAddressEntry* entry)
{
    if (!entry) return;
    memset(entry, 0, sizeof(XNetworkAddressEntry));
    XClass_init(entry);
    // 初始化基类虚表
    XClassSetVtable(entry, XNetworkAddressEntry);
    
    // 初始化内嵌的 XHostAddress 对象
    XHostAddress_init(&entry->m_ip);
    XHostAddress_init(&entry->m_netmask);
    XHostAddress_init(&entry->m_broadcast);
    
    // 默认值
    entry->m_broadcastIsValid = false;
    entry->m_preferredLifetime = -1;  // 永久
    entry->m_validityLifetime = -1;   // 永久
    entry->m_lifetimeKnown = false;
    entry->m_dnsEligibilityStatus = XNetworkAddressEntry_DnsEligibilityUnknown;
    entry->m_isPermanent = true;  // 默认为永久地址
}

XNetworkAddressEntry* XNetworkAddressEntry_create_ex(XMemoryType memory)
{
    XNetworkAddressEntry* entry = (XNetworkAddressEntry*)XMemory_malloc(sizeof(XNetworkAddressEntry), memory);
    if (!entry) return NULL;
    
    XNetworkAddressEntry_init(entry);
    Set_Class_Memory(entry, memory); Set_Class_IsHeap(entry, true);
    return entry;
}

XNetworkAddressEntry* XNetworkAddressEntry_createWithIp(const XHostAddress* ip)
{
    XNetworkAddressEntry* entry = XNetworkAddressEntry_create();
    if (!entry) return NULL;
    
    if (ip) {
        XClassCopy(&entry->m_ip, ip);
    }
    
    return entry;
}

XNetworkAddressEntry* XNetworkAddressEntry_createFull(const XHostAddress* ip,
                                                       const XHostAddress* netmask,
                                                       const XHostAddress* broadcast)
{
    XNetworkAddressEntry* entry = XNetworkAddressEntry_create();
    if (!entry) return NULL;
    
    if (ip) {
        XClassCopy(&entry->m_ip, ip);
    }
    
    if (netmask) {
        XClassCopy(&entry->m_netmask, netmask);
    }
    
    if (broadcast) {
        XClassCopy(&entry->m_broadcast, broadcast);
        entry->m_broadcastIsValid = true;
    }
    
    return entry;
}

XNetworkAddressEntry* XNetworkAddressEntry_create_copy(const XNetworkAddressEntry* other)
{
    if (!other) return NULL;
    
    XNetworkAddressEntry* entry = XNetworkAddressEntry_create();
    if (!entry) return NULL;
    
    VXNetworkAddressEntry_copy(entry, other);
    return entry;
}

// ==================== 属性访问器 ====================

const XHostAddress* XNetworkAddressEntry_ip(const XNetworkAddressEntry* entry)
{
    return entry ? &entry->m_ip : NULL;
}

void XNetworkAddressEntry_setIp(XNetworkAddressEntry* entry, const XHostAddress* ip)
{
    if (!entry || !ip) return;
    XClassCopy(&entry->m_ip, ip);
}

const XHostAddress* XNetworkAddressEntry_netmask(const XNetworkAddressEntry* entry)
{
    return entry ? &entry->m_netmask : NULL;
}

void XNetworkAddressEntry_setNetmask(XNetworkAddressEntry* entry, const XHostAddress* netmask)
{
    if (!entry || !netmask) return;
    XClassCopy(&entry->m_netmask, netmask);
    //memcpy(&entry->m_netmask, netmask, sizeof(XHostAddress));
}

const XHostAddress* XNetworkAddressEntry_broadcast(const XNetworkAddressEntry* entry)
{
    if (!entry || !entry->m_broadcastIsValid) return NULL;
    return &entry->m_broadcast;
}

void XNetworkAddressEntry_setBroadcast(XNetworkAddressEntry* entry, const XHostAddress* broadcast)
{
    if (!entry) return;
    
    if (broadcast) {
        XClassCopy(&entry->m_broadcast, broadcast);
        entry->m_broadcastIsValid = true;
    } else {
        entry->m_broadcastIsValid = false;
    }
}

bool XNetworkAddressEntry_isBroadcastValid(const XNetworkAddressEntry* entry)
{
    return entry ? entry->m_broadcastIsValid : false;
}

void XNetworkAddressEntry_clearBroadcast(XNetworkAddressEntry* entry)
{
    if (!entry) return;
    entry->m_broadcastIsValid = false;
}

// ==================== 前缀长度计算 ====================

/**
 * @brief 计算掩码中连续1的位数
 * @param data 掩码数据
 * @param len 数据长度
 * @return 连续1的位数
 */
static int countLeadingOnes(const uint8_t* data, int len)
{
    int count = 0;
    for (int i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int bit = 7; bit >= 0; bit--) {
            if (byte & (1 << bit)) {
                count++;
            } else {
                return count;
            }
        }
    }
    return count;
}

int XNetworkAddressEntry_prefixLength(const XNetworkAddressEntry* entry)
{
    if (!entry) return -1;
    
    /* 根据协议类型确定掩码数据长度 */
    XHostAddress_NetworkLayerProtocol protocol = XHostAddress_protocol(&entry->m_netmask);
    
    if (protocol == XHostAddress_IPv4Protocol) {
        /* IPv4: 4 字节 */
        uint32_t ip = XHostAddress_toIPv4Address(&entry->m_netmask);
        uint8_t data[4];
        data[0] = (ip >> 24) & 0xFF;
        data[1] = (ip >> 16) & 0xFF;
        data[2] = (ip >> 8) & 0xFF;
        data[3] = ip & 0xFF;
        return countLeadingOnes(data, 4);
    } else if (protocol == XHostAddress_IPv6Protocol) {
        /* IPv6: 16 字节 */
        uint8_t data[16];
        XHostAddress_toIPv6Address(&entry->m_netmask, data);
        return countLeadingOnes(data, 16);
    }
    
    return -1;
}

void XNetworkAddressEntry_setPrefixLength(XNetworkAddressEntry* entry, int length)
{
    int i;
    if (!entry) return;
    
    XHostAddress_NetworkLayerProtocol protocol = XHostAddress_protocol(&entry->m_ip);
    
    if (protocol == XHostAddress_IPv4Protocol) {
        /* IPv4: 0-32 */
        if (length < 0 || length > 32) {
            length = -1;  /* 无效值 */
        }
        
        if (length < 0) {
            /* 清除掩码 */
            memset(&entry->m_netmask, 0, sizeof(XHostAddress));
            return;
        }
        
        /* 计算掩码值 */
        uint32_t mask = 0;
        if (length > 0) {
            mask = 0xFFFFFFFFu << (32 - length);
        }
        
        XHostAddress_setAddressIPv4(&entry->m_netmask, mask);
    } else if (protocol == XHostAddress_IPv6Protocol) {
        /* IPv6: 0-128 */
        if (length < 0 || length > 128) {
            length = -1;
        }
        
        if (length < 0) {
            memset(&entry->m_netmask, 0, sizeof(XHostAddress));
            return;
        }
        
        /* 计算 IPv6 掩码 */
        uint8_t data[16] = {0};
        int fullBytes = length / 8;
        int remainingBits = length % 8;
        
        for (i = 0; i < fullBytes; i++) {
            data[i] = 0xFF;
        }
        
        if (remainingBits > 0 && fullBytes < 16) {
            data[fullBytes] = 0xFF << (8 - remainingBits);
        }
        
        XHostAddress_setAddressIPv6(&entry->m_netmask, data);
    }
}

// ==================== 地址生命周期 ====================

int64_t XNetworkAddressEntry_preferredLifetime(const XNetworkAddressEntry* entry)
{
    return entry ? entry->m_preferredLifetime : -1;
}

int64_t XNetworkAddressEntry_validityLifetime(const XNetworkAddressEntry* entry)
{
    return entry ? entry->m_validityLifetime : -1;
}

bool XNetworkAddressEntry_isLifetimeKnown(const XNetworkAddressEntry* entry)
{
    return entry ? entry->m_lifetimeKnown : false;
}

void XNetworkAddressEntry_setAddressLifetime(XNetworkAddressEntry* entry, 
                                              int64_t preferred, int64_t validity)
{
    if (!entry) return;
    
    entry->m_preferredLifetime = preferred;
    entry->m_validityLifetime = validity;
    entry->m_lifetimeKnown = true;
}

void XNetworkAddressEntry_clearAddressLifetime(XNetworkAddressEntry* entry)
{
    if (!entry) return;
    
    entry->m_preferredLifetime = -1;
    entry->m_validityLifetime = -1;
    entry->m_lifetimeKnown = false;
}

// ==================== DNS 资格 ====================

XNetworkAddressEntry_DnsEligibilityStatus XNetworkAddressEntry_dnsEligibility(const XNetworkAddressEntry* entry)
{
    return entry ? entry->m_dnsEligibilityStatus : XNetworkAddressEntry_DnsEligibilityUnknown;
}

void XNetworkAddressEntry_setDnsEligibility(XNetworkAddressEntry* entry, 
                                             XNetworkAddressEntry_DnsEligibilityStatus status)
{
    if (entry) {
        entry->m_dnsEligibilityStatus = status;
    }
}

// ==================== 地址类型 ====================

bool XNetworkAddressEntry_isPermanent(const XNetworkAddressEntry* entry)
{
    return entry ? entry->m_isPermanent : true;
}

bool XNetworkAddressEntry_isTemporary(const XNetworkAddressEntry* entry)
{
    return entry ? !entry->m_isPermanent : false;
}

void XNetworkAddressEntry_setPermanent(XNetworkAddressEntry* entry, bool permanent)
{
    if (entry) {
        entry->m_isPermanent = permanent;
    }
}

// ==================== 比较操作 ====================

bool XNetworkAddressEntry_equals(const XNetworkAddressEntry* entry1, 
                                  const XNetworkAddressEntry* entry2)
{
    if (!entry1 || !entry2) return entry1 == entry2;
    
    /* 比较 IP 地址 */
    if (!XHostAddress_operator_equal(&entry1->m_ip, &entry2->m_ip)) return false;
    
    /* 比较子网掩码 */
    if (!XHostAddress_operator_equal(&entry1->m_netmask, &entry2->m_netmask)) return false;
    
    /* 比较广播地址 */
    if (entry1->m_broadcastIsValid != entry2->m_broadcastIsValid) return false;
    if (entry1->m_broadcastIsValid && 
        !XHostAddress_operator_equal(&entry1->m_broadcast, &entry2->m_broadcast)) {
        return false;
    }
    
    return true;
}

bool XNetworkAddressEntry_notEquals(const XNetworkAddressEntry* entry1, 
                                     const XNetworkAddressEntry* entry2)
{
    return !XNetworkAddressEntry_equals(entry1, entry2);
}

// ==================== 交换操作 ====================

void XNetworkAddressEntry_swap(XNetworkAddressEntry* entry1, XNetworkAddressEntry* entry2)
{
    if (!entry1 || !entry2) return;
    
    XNetworkAddressEntry temp;
    memcpy(&temp, entry1, sizeof(XNetworkAddressEntry));
    memcpy(entry1, entry2, sizeof(XNetworkAddressEntry));
    memcpy(entry2, &temp, sizeof(XNetworkAddressEntry));
}
#endif // XNETWORK_INTERFACE_ON
#endif /* XNETWORK_ON */
