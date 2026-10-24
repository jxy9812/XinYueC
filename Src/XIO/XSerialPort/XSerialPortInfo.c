/**
 * @file       XSerialPortInfo.c
 * @brief      串口信息类实现，对齐 Qt QSerialPortInfo。
 * @details    平台差异经设备层的 XDeviceSerialPort_enumeratePorts 适配；本文件
 *          只负责把设备层的借用条目转成持久的 XSerialPortInfo 值对象。
 */
#include "XSerialPortInfo.h"
#include "XDeviceSerialPort.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include <string.h>

#if XDEVICE_SERIALPORT_ON

/* ========================================================================== */
/*                        内部工具函数                                         */
/* ========================================================================== */

/**
 * @brief 释放对象持有的四个字符串成员并整体清零。
 * @param info 对象指针；可为 NULL。
 */
static void xserialportinfo_clearStrings(XSerialPortInfo* info)
{
    if (!info) return;
    if (info->m_portName) { XClassDelete(info->m_portName); info->m_portName = NULL; }
    if (info->m_description) { XClassDelete(info->m_description); info->m_description = NULL; }
    if (info->m_manufacturer) { XClassDelete(info->m_manufacturer); info->m_manufacturer = NULL; }
    if (info->m_serialNumber) { XClassDelete(info->m_serialNumber); info->m_serialNumber = NULL; }
}

/* ========================================================================== */
/*                        虚函数实现（static，仅供 class_init 注册）            */
/* ========================================================================== */

static void VXSerialPortInfo_copy(XSerialPortInfo* dest, const XSerialPortInfo* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest)) XSerialPortInfo_init(dest);

    xserialportinfo_clearStrings(dest);
    if (src->m_portName) dest->m_portName = XString_create_copy(src->m_portName);
    if (src->m_description) dest->m_description = XString_create_copy(src->m_description);
    if (src->m_manufacturer) dest->m_manufacturer = XString_create_copy(src->m_manufacturer);
    if (src->m_serialNumber) dest->m_serialNumber = XString_create_copy(src->m_serialNumber);
    dest->m_vendorId = src->m_vendorId;
    dest->m_productId = src->m_productId;
    dest->m_busy = src->m_busy;
    dest->m_valid = src->m_valid;
}

static void VXSerialPortInfo_move(XSerialPortInfo* dest, XSerialPortInfo* src)
{
    if (!dest || !src) return;
    if (XClassIsVtableNull(dest)) XSerialPortInfo_init(dest);

    xserialportinfo_clearStrings(dest);
    dest->m_portName = src->m_portName; src->m_portName = NULL;
    dest->m_description = src->m_description; src->m_description = NULL;
    dest->m_manufacturer = src->m_manufacturer; src->m_manufacturer = NULL;
    dest->m_serialNumber = src->m_serialNumber; src->m_serialNumber = NULL;
    dest->m_vendorId = src->m_vendorId;
    dest->m_productId = src->m_productId;
    dest->m_busy = src->m_busy;
    dest->m_valid = src->m_valid;
}

static void VXSerialPortInfo_deinit(XSerialPortInfo* info)
{
    if (!info) return;
    xserialportinfo_clearStrings(info);
}

XVtable* XSerialPortInfo_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSerialPortInfo)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSerialPortInfo_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSerialPortInfo_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSerialPortInfo_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XSerialPortInfo);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造与析构                                           */
/* ========================================================================== */

void XSerialPortInfo_init(XSerialPortInfo* info)
{
    if (!info) return;
    memset(((unsigned char*)info) + sizeof(XClass), 0, sizeof(*info) - sizeof(XClass));
    XClass_init((XClass*)info);
    XClassSetVtable(info, XSerialPortInfo);
}

XSerialPortInfo* XSerialPortInfo_create_ex(XMemoryType memory)
{
    XSerialPortInfo* info = (XSerialPortInfo*)XMemory_malloc(sizeof(XSerialPortInfo), memory);
    if (!info) return NULL;
    memset(info, 0, sizeof(XSerialPortInfo));
    XSerialPortInfo_init(info);
    Set_Class_Memory(info, memory); Set_Class_IsHeap(info, true);
    return info;
}

XSerialPortInfo* XSerialPortInfo_create_copy(const XSerialPortInfo* other)
{
    XSerialPortInfo* info;
    if (!other) return NULL;
    info = XSerialPortInfo_create();
    if (!info) return NULL;
    XClassCopy(info, other);
    return info;
}

/* ========================================================================== */
/*                        属性访问器                                           */
/* ========================================================================== */

const XString* XSerialPortInfo_portName(const XSerialPortInfo* info)
{
    return info ? info->m_portName : NULL;
}

const XString* XSerialPortInfo_description(const XSerialPortInfo* info)
{
    return info ? info->m_description : NULL;
}

const XString* XSerialPortInfo_manufacturer(const XSerialPortInfo* info)
{
    return info ? info->m_manufacturer : NULL;
}

const XString* XSerialPortInfo_serialNumber(const XSerialPortInfo* info)
{
    return info ? info->m_serialNumber : NULL;
}

uint32_t XSerialPortInfo_vendorId(const XSerialPortInfo* info)
{
    return info ? info->m_vendorId : 0;
}

uint32_t XSerialPortInfo_productId(const XSerialPortInfo* info)
{
    return info ? info->m_productId : 0;
}

bool XSerialPortInfo_isBusy(const XSerialPortInfo* info)
{
    return info && info->m_busy;
}

bool XSerialPortInfo_isValid(const XSerialPortInfo* info)
{
    return info && info->m_valid;
}

/* ========================================================================== */
/*                        静态工具函数                                         */
/* ========================================================================== */

/**
 * @brief 把设备层的串口列表转成 XSerialPortInfo 列表。
 * @param ports 设备层列表；不可为 NULL。
 * @return 新分配的列表；失败返回 NULL。
 */
static XSerialPortInfoList* xserialportinfo_fromDeviceList(const XDeviceSerialPortPortList* ports)
{
    XSerialPortInfoList* list;
    size_t i;
    if (!ports) return NULL;
    list = XVector_create(sizeof(XSerialPortInfo*));
    if (!list) return NULL;
    for (i = 0; i < ports->m_count; ++i) {
        const XDeviceSerialPortPortInfo* from = &ports->m_ports[i];
        XSerialPortInfo* info;
        if (!from->m_portName) continue;
        info = XSerialPortInfo_create();
        if (!info) break;
        info->m_portName = XString_create_utf8(from->m_portName);
        if (from->m_description) info->m_description = XString_create_utf8(from->m_description);
        if (from->m_manufacturer) info->m_manufacturer = XString_create_utf8(from->m_manufacturer);
        if (from->m_serialNumber) info->m_serialNumber = XString_create_utf8(from->m_serialNumber);
        info->m_vendorId = from->m_vendorId;
        info->m_productId = from->m_productId;
        info->m_busy = from->m_busy ? 1 : 0;
        info->m_valid = 1;
        if (!XVector_push_back_1_base(list, &info)) {
            XClassDelete(info);
            break;
        }
    }
    return list;
}

/**
 * @brief 取列表中第 index 个元素。
 * @param list 列表；不可为 NULL。
 * @param index 元素下标。
 * @return 元素指针；越界或为空时返回 NULL。
 * @note 列表元素是 XSerialPortInfo* 指针，XVector_at_base 返回的是元素槽地址，
 *       故需再解引用一层（对照 XHostInfo 的 g_lookupRequests 取法）。
 */
static XSerialPortInfo* xserialportinfo_at(XSerialPortInfoList* list, size_t index)
{
    XSerialPortInfo** slot;
    if (!list) return NULL;
    slot = (XSerialPortInfo**)XVector_at_base(list, (int64_t)index);
    return slot ? *slot : NULL;
}

/**
 * @brief 释放列表及其全部元素。
 * @param list 列表；可为 NULL。
 */
static void xserialportinfo_freeList(XSerialPortInfoList* list)
{
    size_t count;
    size_t i;
    if (!list) return;
    count = XVector_size_base((const XContainer*)list);
    for (i = 0; i < count; ++i) {
        XSerialPortInfo* info = xserialportinfo_at(list, i);
        if (info) XClassDelete(info);
    }
    XClassDelete(list);
}

XSerialPortInfoList* XSerialPortInfo_availablePorts(void)
{
    XDeviceSerialPortOpenOptions options;
    XDeviceSerialPortPortList* ports;
    XSerialPortInfoList* list;
    XFd fd;
    int error = XDeviceError_None;

    /* 枚举模式打开：只建立用于查询可用串口属性的设备上下文，不打开任何端口。 */
    XDeviceSerialPortOpenOptions_init(&options);
    options.m_enumerate = 1;
    fd = XDeviceSerialPort_open(&options, &error);
    if (fd == XFD_INVALID) return NULL;

    /* 便捷 API 内部读取 XDeviceSerialPortProperty_AvailablePorts 属性。 */
    ports = XDeviceSerialPort_availablePorts(fd);
    XDeviceSerialPort_close(fd);
    if (!ports) return NULL;

    list = xserialportinfo_fromDeviceList(ports);
    XDeviceSerialPortPortList_delete(ports);
    if (!list) return NULL;
    return list;
}

XSerialPortInfo* XSerialPortInfo_fromName(const char* name)
{
    XSerialPortInfoList* list;
    XSerialPortInfo* found = NULL;
    size_t count;
    size_t i;

    if (!name || *name == '\0') return NULL;
    list = XSerialPortInfo_availablePorts();
    if (!list) return NULL;

    count = XVector_size_base((const XContainer*)list);
    for (i = 0; i < count; ++i) {
        XSerialPortInfo* info = xserialportinfo_at(list, i);
        if (!info || !info->m_portName) continue;
        if (XString_equals_utf8(info->m_portName, name, XChar_CaseSensitive)) {
            found = XSerialPortInfo_create_copy(info);
            break;
        }
    }
    xserialportinfo_freeList(list);
    return found;
}

#endif /* XDEVICE_SERIALPORT_ON */
