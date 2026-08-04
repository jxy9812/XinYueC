// XNetworkAddressEntry.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XNetworkAddressEntry.h"
#include "XMemory.h"
#include "XHostAddress.h"
#include <string.h>

// ==================== 虚函数表 ====================

static void VXNetworkAddressEntry_deinit(XNetworkAddressEntry* entry)
{
    if (!entry) return;
    // 释放内嵌的 XHostAddress 对象
    XHostAddress_deinit_base(&entry->ip);
    XHostAddress_deinit_base(&entry->netmask);
    XHostAddress_deinit_base(&entry->broadcast);
}

static void VXNetworkAddressEntry_copy(XNetworkAddressEntry* dest, const XNetworkAddressEntry* src)
{
    if (!dest || !src) return;
    
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    if (XClassIsVtableNull(dest))
        XNetworkAddressEntry_init(dest);
    
    // 先释放目标对象的旧值
    XHostAddress_deinit_base(&dest->ip);
    XHostAddress_deinit_base(&dest->netmask);
    XHostAddress_deinit_base(&dest->broadcast);
    
    // 复制源对象的值
    XHostAddress_copy_base(&dest->ip, &src->ip);
    XHostAddress_copy_base(&dest->netmask, &src->netmask);
    XHostAddress_copy_base(&dest->broadcast, &src->broadcast);
    dest->broadcastIsValid = src->broadcastIsValid;
    
    dest->preferredLifetime = src->preferredLifetime;
    dest->validityLifetime = src->validityLifetime;
    dest->lifetimeKnown = src->lifetimeKnown;
    
    dest->dnsEligibilityStatus = src->dnsEligibilityStatus;
    dest->isPermanent = src->isPermanent;
}

static void VXNetworkAddressEntry_move(XNetworkAddressEntry* dest, XNetworkAddressEntry* src)
{
    if (!dest || !src) return;
    
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    if (XClassIsVtableNull(dest))
        XNetworkAddressEntry_init(dest);
   
    // 移动语义：转移资源
    XHostAddress_move_base(&dest->ip, &src->ip);
    XHostAddress_move_base(&dest->netmask, &src->netmask);
    XHostAddress_move_base(&dest->broadcast, &src->broadcast);
    dest->broadcastIsValid = src->broadcastIsValid;
    
    dest->preferredLifetime = src->preferredLifetime;
    dest->validityLifetime = src->validityLifetime;
    dest->lifetimeKnown = src->lifetimeKnown;
    
    dest->dnsEligibilityStatus = src->dnsEligibilityStatus;
    dest->isPermanent = src->isPermanent;
    
    // 清空源对象
    src->broadcastIsValid = false;
    src->preferredLifetime = -1;
    src->validityLifetime = -1;
    src->lifetimeKnown = false;
    src->dnsEligibilityStatus = XNetworkAddressEntry_DnsEligibilityUnknown;
    src->isPermanent = true;
}

XVtable* XNetworkAddressEntry_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkAddressEntry)
	XCLASS_SET_CLASS_NAME_DEFAULT("XNetworkAddressEntry");
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
    XHostAddress_init(&entry->ip);
    XHostAddress_init(&entry->netmask);
    XHostAddress_init(&entry->broadcast);
    
    // 默认值
    entry->broadcastIsValid = false;
    entry->preferredLifetime = -1;  // 永久
    entry->validityLifetime = -1;   // 永久
    entry->lifetimeKnown = false;
    entry->dnsEligibilityStatus = XNetworkAddressEntry_DnsEligibilityUnknown;
    entry->isPermanent = true;  // 默认为永久地址
}

XNetworkAddressEntry* XNetworkAddressEntry_create(void)
{
    XNetworkAddressEntry* entry = (XNetworkAddressEntry*)XMalloc_System(sizeof(XNetworkAddressEntry));
    if (!entry) return NULL;
    
    XNetworkAddressEntry_init(entry);
    Set_Class_MemoryFree(entry, XFree_System);
    return entry;
}

XNetworkAddressEntry* XNetworkAddressEntry_createWithIp(const XHostAddress* ip)
{
    XNetworkAddressEntry* entry = XNetworkAddressEntry_create();
    if (!entry) return NULL;
    
    if (ip) {
        XHostAddress_copy_base(&entry->ip, ip);
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
        XHostAddress_copy_base(&entry->ip, ip);
    }
    
    if (netmask) {
        XHostAddress_copy_base(&entry->netmask, netmask);
    }
    
    if (broadcast) {
        XHostAddress_copy_base(&entry->broadcast, broadcast);
        entry->broadcastIsValid = true;
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
    return entry ? &entry->ip : NULL;
}

void XNetworkAddressEntry_setIp(XNetworkAddressEntry* entry, const XHostAddress* ip)
{
    if (!entry || !ip) return;
    XHostAddress_copy_base(&entry->ip, ip);
}

const XHostAddress* XNetworkAddressEntry_netmask(const XNetworkAddressEntry* entry)
{
    return entry ? &entry->netmask : NULL;
}

void XNetworkAddressEntry_setNetmask(XNetworkAddressEntry* entry, const XHostAddress* netmask)
{
    if (!entry || !netmask) return;
    XHostAddress_copy_base(&entry->netmask, netmask);
    //memcpy(&entry->netmask, netmask, sizeof(XHostAddress));
}

const XHostAddress* XNetworkAddressEntry_broadcast(const XNetworkAddressEntry* entry)
{
    if (!entry || !entry->broadcastIsValid) return NULL;
    return &entry->broadcast;
}

void XNetworkAddressEntry_setBroadcast(XNetworkAddressEntry* entry, const XHostAddress* broadcast)
{
    if (!entry) return;
    
    if (broadcast) {
        XHostAddress_copy_base(&entry->broadcast, broadcast);
        entry->broadcastIsValid = true;
    } else {
        entry->broadcastIsValid = false;
    }
}

bool XNetworkAddressEntry_isBroadcastValid(const XNetworkAddressEntry* entry)
{
    return entry ? entry->broadcastIsValid : false;
}

void XNetworkAddressEntry_clearBroadcast(XNetworkAddressEntry* entry)
{
    if (!entry) return;
    entry->broadcastIsValid = false;
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
    XHostAddress_NetworkLayerProtocol protocol = XHostAddress_protocol(&entry->netmask);
    
    if (protocol == XHostAddress_IPv4Protocol) {
        /* IPv4: 4 字节 */
        uint32_t ip = XHostAddress_toIPv4Address(&entry->netmask);
        uint8_t data[4];
        data[0] = (ip >> 24) & 0xFF;
        data[1] = (ip >> 16) & 0xFF;
        data[2] = (ip >> 8) & 0xFF;
        data[3] = ip & 0xFF;
        return countLeadingOnes(data, 4);
    } else if (protocol == XHostAddress_IPv6Protocol) {
        /* IPv6: 16 字节 */
        uint8_t data[16];
        XHostAddress_toIPv6Address(&entry->netmask, data);
        return countLeadingOnes(data, 16);
    }
    
    return -1;
}

void XNetworkAddressEntry_setPrefixLength(XNetworkAddressEntry* entry, int length)
{
    int i;
    if (!entry) return;
    
    XHostAddress_NetworkLayerProtocol protocol = XHostAddress_protocol(&entry->ip);
    
    if (protocol == XHostAddress_IPv4Protocol) {
        /* IPv4: 0-32 */
        if (length < 0 || length > 32) {
            length = -1;  /* 无效值 */
        }
        
        if (length < 0) {
            /* 清除掩码 */
            memset(&entry->netmask, 0, sizeof(XHostAddress));
            return;
        }
        
        /* 计算掩码值 */
        uint32_t mask = 0;
        if (length > 0) {
            mask = 0xFFFFFFFFu << (32 - length);
        }
        
        XHostAddress_setAddressIPv4(&entry->netmask, mask);
    } else if (protocol == XHostAddress_IPv6Protocol) {
        /* IPv6: 0-128 */
        if (length < 0 || length > 128) {
            length = -1;
        }
        
        if (length < 0) {
            memset(&entry->netmask, 0, sizeof(XHostAddress));
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
        
        XHostAddress_setAddressIPv6(&entry->netmask, data);
    }
}

// ==================== 地址生命周期 ====================

int64_t XNetworkAddressEntry_preferredLifetime(const XNetworkAddressEntry* entry)
{
    return entry ? entry->preferredLifetime : -1;
}

int64_t XNetworkAddressEntry_validityLifetime(const XNetworkAddressEntry* entry)
{
    return entry ? entry->validityLifetime : -1;
}

bool XNetworkAddressEntry_isLifetimeKnown(const XNetworkAddressEntry* entry)
{
    return entry ? entry->lifetimeKnown : false;
}

void XNetworkAddressEntry_setAddressLifetime(XNetworkAddressEntry* entry, 
                                              int64_t preferred, int64_t validity)
{
    if (!entry) return;
    
    entry->preferredLifetime = preferred;
    entry->validityLifetime = validity;
    entry->lifetimeKnown = true;
}

void XNetworkAddressEntry_clearAddressLifetime(XNetworkAddressEntry* entry)
{
    if (!entry) return;
    
    entry->preferredLifetime = -1;
    entry->validityLifetime = -1;
    entry->lifetimeKnown = false;
}

// ==================== DNS 资格 ====================

XNetworkAddressEntry_DnsEligibilityStatus XNetworkAddressEntry_dnsEligibility(const XNetworkAddressEntry* entry)
{
    return entry ? entry->dnsEligibilityStatus : XNetworkAddressEntry_DnsEligibilityUnknown;
}

void XNetworkAddressEntry_setDnsEligibility(XNetworkAddressEntry* entry, 
                                             XNetworkAddressEntry_DnsEligibilityStatus status)
{
    if (entry) {
        entry->dnsEligibilityStatus = status;
    }
}

// ==================== 地址类型 ====================

bool XNetworkAddressEntry_isPermanent(const XNetworkAddressEntry* entry)
{
    return entry ? entry->isPermanent : true;
}

bool XNetworkAddressEntry_isTemporary(const XNetworkAddressEntry* entry)
{
    return entry ? !entry->isPermanent : false;
}

void XNetworkAddressEntry_setPermanent(XNetworkAddressEntry* entry, bool permanent)
{
    if (entry) {
        entry->isPermanent = permanent;
    }
}

// ==================== 比较操作 ====================

bool XNetworkAddressEntry_equals(const XNetworkAddressEntry* entry1, 
                                  const XNetworkAddressEntry* entry2)
{
    if (!entry1 || !entry2) return entry1 == entry2;
    
    /* 比较 IP 地址 */
    if (!XHostAddress_operator_equal(&entry1->ip, &entry2->ip)) return false;
    
    /* 比较子网掩码 */
    if (!XHostAddress_operator_equal(&entry1->netmask, &entry2->netmask)) return false;
    
    /* 比较广播地址 */
    if (entry1->broadcastIsValid != entry2->broadcastIsValid) return false;
    if (entry1->broadcastIsValid && 
        !XHostAddress_operator_equal(&entry1->broadcast, &entry2->broadcast)) {
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