#include "XFileDevice.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 虚函数实现
 * ============================================================================ */

// 默认 fileName 实现 - 返回空字符串
static const XString* VXFileDevice_fileName(const XFileDevice* device)
{
    (void)device;
    static XString emptyString;
    static bool initialized = false;
    if (!initialized) {
        XString_init(&emptyString);
        initialized = true;
    }
    return &emptyString;
}

// 默认 resize 实现 - 返回 false
static bool VXFileDevice_resize(XFileDevice* device, int64_t sz)
{
    (void)device;
    (void)sz;
    return false;
}

// 默认 permissions 实现 - 返回 0
static XFilePermissions VXFileDevice_permissions(const XFileDevice* device)
{
    (void)device;
    return 0;
}

// 默认 setPermissions 实现 - 返回 false
static bool VXFileDevice_setPermissions(XFileDevice* device, XFilePermissions permissions)
{
    (void)device;
    (void)permissions;
    return false;
}

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XFileDevice_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XFileDevice))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XIODevice);

    // 设置默认虚函数实现
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_FileName, VXFileDevice_fileName);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_Resize, VXFileDevice_resize);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_Permissions, VXFileDevice_permissions);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_SetPermissions, VXFileDevice_setPermissions);

#if SHOWCONTAINERSIZE
    printf("XFileDevice vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif

    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

XFileDevice* XFileDevice_create(void)
{
    XFileDevice* device = (XFileDevice*)XMalloc_System(sizeof(XFileDevice));
    if (!device) return NULL;

    XFileDevice_init(device);
    Set_Class_MemoryFree(device, XFree_System);
    return device;
}

void XFileDevice_init(XFileDevice* device)
{
    if (!device) return;

    // 初始化基类
    XIODevice_init(&device->m_parent);
    XClassSetVtable(device, XFileDevice);

    // 初始化成员
    device->m_error = XFileDevice_NoError;
    device->m_fileHandle = -1;
    device->m_handleFlags = XFileDevice_DontCloseHandle;
}

/* ============================================================================
 * 错误处理
 * ============================================================================ */

XFileDeviceError XFileDevice_error(const XFileDevice* device)
{
    if (!device) return XFileDevice_UnspecifiedError;
    return device->m_error;
}

void XFileDevice_unsetError(XFileDevice* device)
{
    if (device) {
        device->m_error = XFileDevice_NoError;
    }
}

/* ============================================================================
 * 文件信息（虚函数调用）
 * ============================================================================ */

const XString* XFileDevice_fileName_base(const XFileDevice* device)
{
    if (!device) return NULL;
    return XClassGetVirtualFunc(device, EXFileDevice_FileName, const XString * (*)(const XFileDevice*))(device);
}

/* ============================================================================
 * 文件操作（虚函数调用）
 * ============================================================================ */

bool XFileDevice_resize_base(XFileDevice* device, int64_t sz)
{
    if (!device) return false;
    return XClassGetVirtualFunc(device, EXFileDevice_Resize, bool (*)(XFileDevice*, int64_t))(device, sz);
}

XFilePermissions XFileDevice_permissions_base(const XFileDevice* device)
{
    if (!device) return 0;
    return XClassGetVirtualFunc(device, EXFileDevice_Permissions, XFilePermissions(*)(const XFileDevice*))(device);
}

bool XFileDevice_setPermissions_base(XFileDevice* device, XFilePermissions permissions)
{
    if (!device) return false;
    return XClassGetVirtualFunc(device, EXFileDevice_SetPermissions, bool (*)(XFileDevice*, XFilePermissions))(device, permissions);
}

/* ============================================================================
 * 非虚函数 - 依赖平台实现
 * ============================================================================ */

// 以下函数在 Drive/windows/XFileDevice_win32.c 或
// Drive/linux/XFileDevice_linux.c 中实现

// bool XFileDevice_flush(XFileDevice* device);
// int XFileDevice_handle(const XFileDevice* device);
// XDateTime XFileDevice_fileTime(const XFileDevice* device, XFileDeviceFileTime time);
// bool XFileDevice_setFileTime(XFileDevice* device, const XDateTime* newDate, XFileDeviceFileTime time);
// void* XFileDevice_map(XFileDevice* device, int64_t offset, int64_t size, XFileDeviceMemoryMapFlags flags);
// bool XFileDevice_unmap(XFileDevice* device, void* address);
