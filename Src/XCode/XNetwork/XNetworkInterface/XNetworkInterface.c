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
        XString_delete_base(iface->name);
        iface->name = NULL;
    }
    
    if (iface->humanReadableName) {
        XString_delete_base(iface->humanReadableName);
        iface->humanReadableName = NULL;
    }
    
    if (iface->hardwareAddress) {
        XString_delete_base(iface->hardwareAddress);
        iface->hardwareAddress = NULL;
    }
    
    if (iface->addressEntries) {
        XVector_delete_base(iface->addressEntries);
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
            XCopy(dest->name, src->name);
        else
            dest->name = XString_create_copy(src->name);
    }
    
    if (src->humanReadableName) 
    {
        if (dest->humanReadableName)
            XCopy(dest->humanReadableName, src->humanReadableName);
        else
            dest->humanReadableName = XString_create_copy(src->humanReadableName);
    }
    
    if (src->hardwareAddress)
    {
        if (dest->hardwareAddress)
            XCopy(dest->hardwareAddress, src->hardwareAddress);
        else
            dest->hardwareAddress = XString_create_copy(src->hardwareAddress);
    }
    
    /* 复制地址条目列表 */
    if (src->addressEntries) 
    {
        if (dest->addressEntries)
            XCopy(dest->addressEntries, src->addressEntries);
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
    
    XSwap(dest, src,sizeof(XNetworkInterface));

    //// 移动所有字段
    //dest->name = src->name;
    //dest->humanReadableName = src->humanReadableName;
    //dest->hardwareAddress = src->hardwareAddress;
    //dest->index = src->index;
    //dest->mtu = src->mtu;
    //dest->flags = src->flags;
    //dest->type = src->type;
    //dest->addressEntries = src->addressEntries;
    //dest->isValid = src->isValid;
    //
    //// 清空源对象
    //src->name = NULL;
    //src->humanReadableName = NULL;
    //src->hardwareAddress = NULL;
    //src->addressEntries = NULL;
    //src->index = 0;
    //src->mtu = 0;
    //src->flags = 0;
    //src->type = XNetworkInterface_Unknown;
    //src->isValid = false;
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
    XContainerSetDataDeinitMethod(iface->addressEntries, XNetworkAddressEntry_deinit_base);
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
    
    XCopy(iface, other);
    return iface;
}

XNetworkInterface* XNetworkInterface_create_move(const XNetworkInterface* other)
{
    if (!other) return NULL;

    XNetworkInterface* iface = XNetworkInterface_create();
    if (!iface) return NULL;

    XMove(iface, other);
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
    XContainerSetDataDeinitMethod(addresses, XHostAddress_deinit_base);
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
    
    XVector_delete_base(interfaces);
    
    return addresses;
}

XVector* XNetworkInterface_allInterfaces(void)
{
    XVector* result = XVector_create(sizeof(XNetworkInterface));
    if (!result) return NULL;
    XContainerSetDataCopyMethod(result, XClass_copy_base);
    XContainerSetDataMoveMethod(result, XClass_move_base);
    XContainerSetDataDeinitMethod(result, XNetworkInterface_deinit_base);
    XDeviceNetworkInterfaceIterator iter = XDeviceNetwork_enumInterfacesBegin();
    if (!iter) {
        XVector_delete_base(result);
        return NULL;
    }   
    
    XNetworkInterface* iface;
    while ((iface = XDeviceNetwork_enumInterfacesNext(iter)) != NULL) {
        /* 接口信息已在平台层填充完毕，直接添加到结果向量 */
        XVector_push_back_move_1_base(result, iface);
        XNetworkInterface_delete_base(iface);
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
    
    XVector_delete_base(interfaces);
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
    
    XVector_delete_base(interfaces);
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
                XNetworkInterface_delete_base(iface);
                XDeviceNetwork_enumInterfacesEnd(iter);
                return index;
            }
        }
        XNetworkInterface_delete_base(iface);
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
            XNetworkInterface_delete_base(iface);
            XDeviceNetwork_enumInterfacesEnd(iter);
            return result;
        }
        XNetworkInterface_delete_base(iface);
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
#endif // XNETWORK_INTERFACE_ON
#endif /* XNETWORK_ON */
