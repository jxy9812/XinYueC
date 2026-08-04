// XNetworkDatagram.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XNetworkDatagram.h"
#include "XMemory.h"
#include <string.h>

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XNetworkDatagram_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkDatagram)
	XCLASS_SET_CLASS_NAME_DEFAULT("XNetworkDatagram");
    XVTABLE_INHERIT_XCLASS(XClass);

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
    
    dgram->data = NULL;
    XHostAddress_init(&dgram->senderAddress);
    XHostAddress_init(&dgram->destinationAddress);
    dgram->senderPort = 0;
    dgram->destinationPort = 0;
    dgram->hopLimit = -1;
    dgram->interfaceIndex = 0;
}

XNetworkDatagram* XNetworkDatagram_create(void)
{
    XNetworkDatagram* dgram = (XNetworkDatagram*)XMalloc_System(sizeof(XNetworkDatagram));
    if (!dgram) return NULL;
    
    XNetworkDatagram_init(dgram);
    Set_Class_MemoryFree(dgram, XFree_System);
    return dgram;
}

XNetworkDatagram* XNetworkDatagram_create_2(const XByteArray* data, const XHostAddress* destinationAddress, uint16_t port)
{
    XNetworkDatagram* dgram = XNetworkDatagram_create();
    if (!dgram) return NULL;
    
    if (data) {
        dgram->data = XByteArray_create_copy(data);
    }
    if (destinationAddress) {
        XHostAddress_setAddress(&dgram->destinationAddress, XString_toUtf8(XHostAddress_toString(destinationAddress)));
        dgram->destinationPort = port;
    }
    
    return dgram;
}

XNetworkDatagram* XNetworkDatagram_create_copy(const XNetworkDatagram* other)
{
    if (!other) return XNetworkDatagram_create();
    
    XNetworkDatagram* dgram = XNetworkDatagram_create();
    if (!dgram) return NULL;
    
    if (other->data) {
        dgram->data = XByteArray_create_copy(other->data);
    }
    
    // 复制地址
    memcpy(&dgram->senderAddress, &other->senderAddress, sizeof(XHostAddress));
    memcpy(&dgram->destinationAddress, &other->destinationAddress, sizeof(XHostAddress));
    
    dgram->senderPort = other->senderPort;
    dgram->destinationPort = other->destinationPort;
    dgram->hopLimit = other->hopLimit;
    dgram->interfaceIndex = other->interfaceIndex;
    
    return dgram;
}

void XNetworkDatagram_deinit(XNetworkDatagram* dgram)
{
    if (!dgram) return;
    
    if (dgram->data) {
        XByteArray_delete_base(dgram->data);
        dgram->data = NULL;
    }
    
    XClass_deinit_base(&dgram->m_class);
}

/* ============================================================================
 * 清空与有效性
 * ============================================================================ */

void XNetworkDatagram_clear(XNetworkDatagram* dgram)
{
    if (!dgram) return;
    
    if (dgram->data) {
        XByteArray_delete_base(dgram->data);
        dgram->data = NULL;
    }
    
    XHostAddress_deinit_base(&dgram->senderAddress);
    XHostAddress_deinit_base(&dgram->destinationAddress);
    XHostAddress_init(&dgram->senderAddress);
    XHostAddress_init(&dgram->destinationAddress);
    
    dgram->senderPort = 0;
    dgram->destinationPort = 0;
    dgram->hopLimit = -1;
    dgram->interfaceIndex = 0;
}

bool XNetworkDatagram_isValid(const XNetworkDatagram* dgram)
{
    if (!dgram) return false;
    
    // 至少有一个发送者或接收者地址
    return !XHostAddress_isNull(&dgram->senderAddress) ||
           !XHostAddress_isNull(&dgram->destinationAddress);
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
    return dgram->data;
}

void XNetworkDatagram_setData(XNetworkDatagram* dgram, const XByteArray* data)
{
    if (!dgram) return;
    
    if (dgram->data) {
        XByteArray_delete_base(dgram->data);
        dgram->data = NULL;
    }
    
    if (data) {
        dgram->data = XByteArray_create_copy(data);
    }
}

/* ============================================================================
 * 发送者信息
 * ============================================================================ */

const XHostAddress* XNetworkDatagram_senderAddress(const XNetworkDatagram* dgram)
{
    if (!dgram) return NULL;
    return &dgram->senderAddress;
}

int XNetworkDatagram_senderPort(const XNetworkDatagram* dgram)
{
    if (!dgram) return -1;
    return XHostAddress_isNull(&dgram->senderAddress) ? -1 : (int)dgram->senderPort;
}

void XNetworkDatagram_setSender(XNetworkDatagram* dgram, const XHostAddress* address, uint16_t port)
{
    if (!dgram || !address) return;
    
    memcpy(&dgram->senderAddress, address, sizeof(XHostAddress));
    dgram->senderPort = port;
}

/* ============================================================================
 * 目标信息
 * ============================================================================ */

const XHostAddress* XNetworkDatagram_destinationAddress(const XNetworkDatagram* dgram)
{
    if (!dgram) return NULL;
    return &dgram->destinationAddress;
}

int XNetworkDatagram_destinationPort(const XNetworkDatagram* dgram)
{
    if (!dgram) return -1;
    return XHostAddress_isNull(&dgram->destinationAddress) ? -1 : (int)dgram->destinationPort;
}

void XNetworkDatagram_setDestination(XNetworkDatagram* dgram, const XHostAddress* address, uint16_t port)
{
    if (!dgram || !address) return;
    
    memcpy(&dgram->destinationAddress, address, sizeof(XHostAddress));
    dgram->destinationPort = port;
}

/* ============================================================================
 * 跳数限制
 * ============================================================================ */

int XNetworkDatagram_hopLimit(const XNetworkDatagram* dgram)
{
    if (!dgram) return -1;
    return dgram->hopLimit;
}

void XNetworkDatagram_setHopLimit(XNetworkDatagram* dgram, int count)
{
    if (!dgram) return;
    
    // 接受 -1（系统选择）或 1-255
    if (count == -1 || (count >= 1 && count <= 255)) {
        dgram->hopLimit = count;
    }
}

/* ============================================================================
 * 网络接口
 * ============================================================================ */

uint32_t XNetworkDatagram_interfaceIndex(const XNetworkDatagram* dgram)
{
    if (!dgram) return 0;
    return dgram->interfaceIndex;
}

void XNetworkDatagram_setInterfaceIndex(XNetworkDatagram* dgram, uint32_t index)
{
    if (!dgram) return;
    dgram->interfaceIndex = index;
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
        reply->data = XByteArray_create_copy(payload);
    }
    
    // 发送者地址和端口 -> 目标地址和端口
    if (!XHostAddress_isNull(&dgram->senderAddress)) {
        memcpy(&reply->destinationAddress, &dgram->senderAddress, sizeof(XHostAddress));
        reply->destinationPort = dgram->senderPort;
    }
    
    // 复制接口索引
    reply->interfaceIndex = dgram->interfaceIndex;
    
    // 如果目标是 IPv6 全局地址（非多播），则复制为目标地址
    // 注意：IPv4 广播地址无法区分，所以不复制
    if (dgram->destinationAddress.protocol == XHostAddress_IPv6Protocol &&
        !XHostAddress_isMulticast(&dgram->destinationAddress) &&
        !XHostAddress_isLinkLocal(&dgram->destinationAddress)) {
        memcpy(&reply->senderAddress, &dgram->destinationAddress, sizeof(XHostAddress));
        reply->senderPort = dgram->destinationPort;
    }
    
    // 跳数限制重置为默认值
    reply->hopLimit = -1;
    
    return reply;
}
