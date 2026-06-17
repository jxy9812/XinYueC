#include "XFileDevice.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 平台相关函数声明（在 Drive/windows/XFileDevice_win32.c 或
 * Drive/linux/XFileDevice_linux.c 中实现）
 * ============================================================================ */

extern int64_t XFileDevice_pos_impl(const XFileDevice* device);
extern int64_t XFileDevice_size_impl(const XFileDevice* device);
extern bool XFileDevice_seek_impl(XFileDevice* device, int64_t pos);
extern int64_t XFileDevice_readData_impl(XFileDevice* device, char* data, int64_t maxlen);
extern int64_t XFileDevice_writeData_impl(XFileDevice* device, const char* data, int64_t len);

/* ============================================================================
 * 虚函数实现（重写父类虚函数）
 * ============================================================================ */

/**
 * @brief 虚函数：析构
 */
static void VXFileDevice_deinit(XFileDevice* device)
{
    if (!device) return;

    // 关闭文件（调用 close 虚函数，由子类实现具体关闭逻辑）
    if (device->m_fileHandle >= 0) {
        XIODevice_close_base(&device->m_parent);
    }

    // 释放父对象
    XClass_Deinit_Parent(XIODevice, device);
}

/**
 * @brief 虚函数：判断是否为顺序设备
 * @return 文件设备始终返回 false（支持随机访问）
 */
static bool VXFileDevice_isSequential(const XFileDevice* device)
{
    (void)device;
    return false;
}

/**
 * @brief 虚函数：获取当前文件位置
 */
static int64_t VXFileDevice_pos(const XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        return 0;
    }
    return XFileDevice_pos_impl(device);
}

/**
 * @brief 虚函数：获取文件大小
 */
static int64_t VXFileDevice_size(const XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        return -1;
    }
    
    // 如果有缓存的大小（包括0），直接返回
    if (device->m_cachedSize >= 0) {
        return device->m_cachedSize;
    }
    
    return XFileDevice_size_impl(device);
}

/**
 * @brief 虚函数：定位文件指针
 */
static bool VXFileDevice_seek(XFileDevice* device, int64_t pos)
{
    if (!device || device->m_fileHandle < 0 || pos < 0) {
        return false;
    }
    
    // 顺序设备不支持 seek
    if (XFileDevice_isSequential_base(device)) {
        return false;
    }
    
    return XFileDevice_seek_impl(device, pos);
}

/**
 * @brief 虚函数：判断是否到达文件末尾
 */
static bool VXFileDevice_atEnd(const XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        return true;
    }
    
    if (!XIODevice_isOpen(&device->m_parent)) {
        return true;
    }
    
    int64_t currentPos = XFileDevice_pos_base(device);
    int64_t fileSize = XFileDevice_size_base(device);
    
    return currentPos >= fileSize;
}

/**
 * @brief 虚函数：重置文件指针到开头
 */
static bool VXFileDevice_reset(XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        return false;
    }
    
    if (XFileDevice_isSequential_base(device)) {
        return false;
    }
    
    return XFileDevice_seek_base(device, 0);
}

/**
 * @brief 虚函数：获取可读取的字节数
 */
static int64_t VXFileDevice_bytesAvailable(const XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        return 0;
    }
    
    int64_t fileSize = XFileDevice_size_base(device);
    int64_t currentPos = XFileDevice_pos_base(device);
    
    return fileSize - currentPos;
}

/**
 * @brief 虚函数：获取待写入的字节数
 * @return 文件设备通常不需要写缓冲，返回 0
 */
static int64_t VXFileDevice_bytesToWrite(const XFileDevice* device)
{
    (void)device;
    return 0;
}

/**
 * @brief 虚函数：读取数据
 */
static int64_t VXFileDevice_readData(XFileDevice* device, char* data, int64_t maxlen)
{
    if (!device || !data || maxlen <= 0) {
        return -1;
    }
    
    if (device->m_fileHandle < 0) {
        return -1;
    }
    
    if (!XIODevice_isReadable(&device->m_parent)) {
        return -1;
    }
    
    return XFileDevice_readData_impl(device, data, maxlen);
}

/**
 * @brief 虚函数：写入数据
 */
static int64_t VXFileDevice_writeData(XFileDevice* device, const char* data, int64_t len)
{
    if (!device || !data || len <= 0) {
        return -1;
    }
    
    if (device->m_fileHandle < 0) {
        return -1;
    }
    
    if (!XIODevice_isWritable(&device->m_parent)) {
        return -1;
    }
    
    return XFileDevice_writeData_impl(device, data, len);
}

/**
 * @brief 虚函数：读取一行数据
 */
static int64_t VXFileDevice_readLineData(XFileDevice* device, char* data, int64_t maxlen)
{
    if (!device || !data || maxlen <= 0) {
        return -1;
    }
    
    if (device->m_fileHandle < 0) {
        return -1;
    }
    
    if (!XIODevice_isReadable(&device->m_parent)) {
        return -1;
    }
    
    // 逐字节读取直到遇到换行符或达到最大长度
    int64_t bytesRead = 0;
    char c;
    
    while (bytesRead < maxlen - 1) {
        int64_t n = XFileDevice_readData_impl(device, &c, 1);
        if (n <= 0) {
            break;
        }
        data[bytesRead++] = c;
        if (c == '\n') {
            break;
        }
    }
    
    if (bytesRead > 0) {
        data[bytesRead] = '\0';
    }
    
    return bytesRead;
}

/**
 * @brief 虚函数：跳过指定字节数
 */
static int64_t VXFileDevice_skipData(XFileDevice* device, int64_t maxSize)
{
    if (!device || maxSize <= 0) {
        return 0;
    }
    
    if (device->m_fileHandle < 0) {
        return 0;
    }
    
    if (XFileDevice_isSequential_base(device)) {
        // 顺序设备：需要读取并丢弃数据
        char temp[4096];
        int64_t skipped = 0;
        
        while (skipped < maxSize) {
            int64_t remaining = maxSize - skipped;
            int64_t toRead = (remaining > (int64_t)sizeof(temp)) ? (int64_t)sizeof(temp) : remaining;
            int64_t n = XFileDevice_readData_impl(device, temp, toRead);
            if (n <= 0) break;
            skipped += n;
        }
        
        return skipped;
    }
    
    // 随机访问设备：直接 seek
    int64_t currentPos = XFileDevice_pos_base(device);
    int64_t fileSize = XFileDevice_size_base(device);
    
    int64_t newPos = currentPos + maxSize;
    if (newPos > fileSize) {
        newPos = fileSize;
    }
    
    int64_t actualSkip = newPos - currentPos;
    if (actualSkip > 0) {
        XFileDevice_seek_impl(device, newPos);
    }
    
    return actualSkip;
}

/* ============================================================================
 * XFileDevice 特有虚函数实现
 * ============================================================================ */

/**
 * @brief 虚函数：获取文件名（默认返回空字符串）
 */
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

/**
 * @brief 虚函数：调整文件大小（默认返回 false）
 */
static bool VXFileDevice_resize(XFileDevice* device, int64_t sz)
{
    (void)device;
    (void)sz;
    return false;
}

/**
 * @brief 虚函数：获取文件权限（默认返回 0）
 */
static XFilePermissions VXFileDevice_permissions(const XFileDevice* device)
{
    (void)device;
    return 0;
}

/**
 * @brief 虚函数：设置文件权限（默认返回 false）
 */
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

    // 重写 XIODevice 虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFileDevice_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, VXFileDevice_isSequential);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Pos, VXFileDevice_pos);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Size, VXFileDevice_size);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Seek, VXFileDevice_seek);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_AtEnd, VXFileDevice_atEnd);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Reset, VXFileDevice_reset);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, VXFileDevice_bytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesToWrite, VXFileDevice_bytesToWrite);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, VXFileDevice_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VXFileDevice_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadLineData, VXFileDevice_readLineData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SkipData, VXFileDevice_skipData);

    // 设置 XFileDevice 特有虚函数的默认实现
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
    device->m_cachedSize = -1;  // -1 表示无缓存
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
// Drive/linux/XFileDevice_linux.c 中实现：
// - XFileDevice_flush()
// - XFileDevice_handle()
// - XFileDevice_fileTime()
// - XFileDevice_setFileTime()
// - XFileDevice_map()
// - XFileDevice_unmap()
// - XFileDevice_pos_impl()
// - XFileDevice_size_impl()
// - XFileDevice_seek_impl()
// - XFileDevice_readData_impl()
// - XFileDevice_writeData_impl()
