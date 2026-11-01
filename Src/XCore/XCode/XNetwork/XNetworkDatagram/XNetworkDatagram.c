// XNetworkDatagram.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XNetworkDatagram.h"
#include "XMemory.h"
#include "XString.h" /* XString_toUtf8（防 C4013 隐式声明，64 位指针截断）。 */
#include <string.h>
#if XNETWORK_ON
#if XNETWORK_DATAGRAM_ON

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

/* 反初始化：释放对象自持有的数据报内容，否则 XClassDelete 只走基类空 deinit，
 * m_data 指向的 XByteArray 永远无人释放。 */
static void VXNetworkDatagram_deinit(XNetworkDatagram* dgram)
{
    if (!dgram) return;

    if (dgram->m_data) {
        XClassDelete(dgram->m_data);
        dgram->m_data = NULL;
    }
}

XVtable* XNetworkDatagram_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkDatagram)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkDatagram_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XNetworkDatagram);
    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

void XNetworkDatagram_init(XNetworkDatagram* dgram)
{
    if (!dgram) return;
    
    XClass_init(&dgram->m_class);
    XClassSetVtable(dgram, XNetworkDatagram);
    
    dgram->m_data = NULL;
    XHostAddress_init(&dgram->m_senderAddress);
    XHostAddress_init(&dgram->m_destinationAddress);
    dgram->m_senderPort = 0;
    dgram->m_destinationPort = 0;
    dgram->m_hopLimit = -1;
    dgram->m_interfaceIndex = 0;
}

XNetworkDatagram* XNetworkDatagram_create_ex(XMemoryType memory)
{
    XNetworkDatagram* dgram = (XNetworkDatagram*)XMemory_malloc(sizeof(XNetworkDatagram), memory);
    if (!dgram) return NULL;
    
    XNetworkDatagram_init(dgram);
    Set_Class_Memory(dgram, memory); Set_Class_IsHeap(dgram, true);
    return dgram;
}

XNetworkDatagram* XNetworkDatagram_create_2(const XByteArray* data, const XHostAddress* destinationAddress, uint16_t port)
{
    XNetworkDatagram* dgram = XNetworkDatagram_create();
    if (!dgram) return NULL;
    
    if (data) {
        dgram->m_data = XByteArray_create_copy(data);
    }
    if (destinationAddress) {
        /* toString 返回新建 XString（需调用方释放），不能内联消费后丢弃 */
        XString* addrStr = XHostAddress_toString(destinationAddress);
        if (addrStr) {
            XHostAddress_setAddress(&dgram->m_destinationAddress, XString_toUtf8(addrStr));
            XClassDelete(addrStr);
        }
        dgram->m_destinationPort = port;
    }
    
    return dgram;
}

XNetworkDatagram* XNetworkDatagram_create_copy(const XNetworkDatagram* other)
{
    if (!other) return XNetworkDatagram_create();
    
    XNetworkDatagram* dgram = XNetworkDatagram_create();
    if (!dgram) return NULL;
    
    if (other->m_data) {
        dgram->m_data = XByteArray_create_copy(other->m_data);
    }
    
    // 复制地址（多态深拷贝，禁止 memcpy 整对象浅拷贝）
    XClassCopy(&dgram->m_senderAddress, &other->m_senderAddress);
    XClassCopy(&dgram->m_destinationAddress, &other->m_destinationAddress);
    
    dgram->m_senderPort = other->m_senderPort;
    dgram->m_destinationPort = other->m_destinationPort;
    dgram->m_hopLimit = other->m_hopLimit;
    dgram->m_interfaceIndex = other->m_interfaceIndex;
    
    return dgram;
}

/* ============================================================================
 * 清空与有效性
 * ============================================================================ */

void XNetworkDatagram_clear(XNetworkDatagram* dgram)
{
    if (!dgram) return;
    
    if (dgram->m_data) {
        XClassDelete(dgram->m_data);
        dgram->m_data = NULL;
    }
    
    XClassDeinit(&dgram->m_senderAddress);
    XClassDeinit(&dgram->m_destinationAddress);
    XHostAddress_init(&dgram->m_senderAddress);
    XHostAddress_init(&dgram->m_destinationAddress);
    
    dgram->m_senderPort = 0;
    dgram->m_destinationPort = 0;
    dgram->m_hopLimit = -1;
    dgram->m_interfaceIndex = 0;
}

bool XNetworkDatagram_isValid(const XNetworkDatagram* dgram)
{
    if (!dgram) return false;
    
    // 至少有一个发送者或接收者地址
    return !XHostAddress_isNull(&dgram->m_senderAddress) ||
           !XHostAddress_isNull(&dgram->m_destinationAddress);
}

bool XNetworkDatagram_isNull(const XNetworkDatagram* dgram)
{
    return !XNetworkDatagram_isValid(dgram);
}

/* ============================================================================
 * 数据访问
 * ============================================================================ */

XByteArray* XNetworkDatagram_data(const XNetworkDatagram* dgram)
{
    if (!dgram) return NULL;
    return dgram->m_data;
}

void XNetworkDatagram_setData(XNetworkDatagram* dgram, const XByteArray* data)
{
    if (!dgram) return;
    
    if (dgram->m_data) {
        XClassDelete(dgram->m_data);
        dgram->m_data = NULL;
    }
    
    if (data) {
        dgram->m_data = XByteArray_create_copy(data);
    }
}

/* ============================================================================
 * 发送者信息
 * ============================================================================ */

const XHostAddress* XNetworkDatagram_senderAddress(const XNetworkDatagram* dgram)
{
    if (!dgram) return NULL;
    return &dgram->m_senderAddress;
}

int XNetworkDatagram_senderPort(const XNetworkDatagram* dgram)
{
    if (!dgram) return -1;
    return XHostAddress_isNull(&dgram->m_senderAddress) ? -1 : (int)dgram->m_senderPort;
}

void XNetworkDatagram_setSender(XNetworkDatagram* dgram, const XHostAddress* address, uint16_t port)
{
    if (!dgram || !address) return;

    /* 多态深拷贝（禁止 memcpy 整对象浅拷贝） */
    XClassCopy(&dgram->m_senderAddress, address);
    dgram->m_senderPort = port;
}

/* ============================================================================
 * 目标信息
 * ============================================================================ */

const XHostAddress* XNetworkDatagram_destinationAddress(const XNetworkDatagram* dgram)
{
    if (!dgram) return NULL;
    return &dgram->m_destinationAddress;
}

int XNetworkDatagram_destinationPort(const XNetworkDatagram* dgram)
{
    if (!dgram) return -1;
    return XHostAddress_isNull(&dgram->m_destinationAddress) ? -1 : (int)dgram->m_destinationPort;
}

void XNetworkDatagram_setDestination(XNetworkDatagram* dgram, const XHostAddress* address, uint16_t port)
{
    if (!dgram || !address) return;

    /* 多态深拷贝（禁止 memcpy 整对象浅拷贝） */
    XClassCopy(&dgram->m_destinationAddress, address);
    dgram->m_destinationPort = port;
}

/* ============================================================================
 * 跳数限制
 * ============================================================================ */

int XNetworkDatagram_hopLimit(const XNetworkDatagram* dgram)
{
    if (!dgram) return -1;
    return dgram->m_hopLimit;
}

void XNetworkDatagram_setHopLimit(XNetworkDatagram* dgram, int count)
{
    if (!dgram) return;
    
    // 接受 -1（系统选择）或 1-255
    if (count == -1 || (count >= 1 && count <= 255)) {
        dgram->m_hopLimit = count;
    }
}

/* ============================================================================
 * 网络接口
 * ============================================================================ */

uint32_t XNetworkDatagram_interfaceIndex(const XNetworkDatagram* dgram)
{
    if (!dgram) return 0;
    return dgram->m_interfaceIndex;
}

void XNetworkDatagram_setInterfaceIndex(XNetworkDatagram* dgram, uint32_t index)
{
    if (!dgram) return;
    dgram->m_interfaceIndex = index;
}

/* ============================================================================
 * 交换
 * ============================================================================ */

void XNetworkDatagram_swap(XNetworkDatagram* a, XNetworkDatagram* b)
{
    if (!a || !b) return;
    
    XNetworkDatagram temp;
    memcpy(&temp, a, sizeof(XNetworkDatagram));
    memcpy(a, b, sizeof(XNetworkDatagram));
    memcpy(b, &temp, sizeof(XNetworkDatagram));
}

/* ============================================================================
 * 回复
 * ============================================================================ */

XNetworkDatagram* XNetworkDatagram_makeReply(const XNetworkDatagram* dgram, const XByteArray* payload)
{
    if (!dgram) return NULL;
    
    XNetworkDatagram* reply = XNetworkDatagram_create();
    if (!reply) return NULL;
    
    // 设置回复数据
    if (payload) {
        reply->m_data = XByteArray_create_copy(payload);
    }
    
    // 发送者地址和端口 -> 目标地址和端口（多态深拷贝，禁止 memcpy 整对象浅拷贝）
    if (!XHostAddress_isNull(&dgram->m_senderAddress)) {
        XClassCopy(&reply->m_destinationAddress, &dgram->m_senderAddress);
        reply->m_destinationPort = dgram->m_senderPort;
    }
    
    // 复制接口索引
    reply->m_interfaceIndex = dgram->m_interfaceIndex;
    
    // 如果目标是 IPv6 全局地址（非多播），则复制为目标地址
    // 注意：IPv4 广播地址无法区分，所以不复制
    if (dgram->m_destinationAddress.protocol == XHostAddress_IPv6Protocol &&
        !XHostAddress_isMulticast(&dgram->m_destinationAddress) &&
        !XHostAddress_isLinkLocal(&dgram->m_destinationAddress)) {
        XClassCopy(&reply->m_senderAddress, &dgram->m_destinationAddress);
        reply->m_senderPort = dgram->m_destinationPort;
    }
    
    // 跳数限制重置为默认值
    reply->m_hopLimit = -1;
    
    return reply;
}
#endif // XNETWORK_DATAGRAM_ON
#endif /* XNETWORK_ON */
