#include "XFileDevice.h"
#include "XFileSystem.h"
#include "XIODevice_Protected.h"  /* XIODevice_setFd */
#include "XIODevicePrivate.h"   /* XIODevicePrivate_getOrCreateReadBuffer */
#include "XRingBuffer.h"        /* XRingBuffer_available */
#include "XFileDescriptor.h"   /* XFd_setCtx */
#include "XAbstractNetIoRing.h"  /* XAbstractNetIoRing_global, registerEvent_base */

#include <stdlib.h>
#include <string.h>
#if XFILE_ON
#if XFILEDEVICE_ON

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
    if (XIODevice_fd(&device->m_parent) >= 0) {
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
    if (!device || XIODevice_fd(&device->m_parent) < 0) {
        return 0;
    }
    int64_t rawPos = XFileSystem_pos(XIODevice_fd(&device->m_parent));
    /* Qt 行为: pos 需要减去读缓冲区中尚未消费的数据量 */
    XIODevice* io = (XIODevice*)&device->m_parent;
    if (io->m_d) {
        struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(io->m_d, io->m_currentReadChannel);
        if (readBuf) {
            int64_t buffered = (int64_t)XRingBuffer_available(readBuf);
            rawPos -= buffered;
            if (rawPos < 0) rawPos = 0;
        }
    }
    return rawPos;
}

/**
 * @brief 虚函数：获取文件大小
 */
static int64_t VXFileDevice_size(const XFileDevice* device)
{
    if (!device || XIODevice_fd(&device->m_parent) < 0) {
        return -1;
    }
    /* 外部进程可在设备保持打开时追加文件，不能以旧缓存作为当前大小。 */
    XFileStat stat;
    if (!XFileSystem_fstat(XIODevice_fd(&device->m_parent), &stat)) return -1;
    ((XFileDevice*)device)->m_cachedSize = stat.size;
    return stat.size;
}

/**
 * @brief 虚函数：定位文件指针
 */
static bool VXFileDevice_seek(XFileDevice* device, int64_t pos)
{
    if (!device || XIODevice_fd(&device->m_parent) < 0 || pos < 0) {
        return false;
    }
    
    // 顺序设备不支持 seek
    if (XFileDevice_isSequential_base(device)) {
        return false;
    }
    

    return XFileSystem_seek(XIODevice_fd(&device->m_parent), pos, XSeekSet) >= 0;
}

/**
 * @brief 虚函数：判断是否到达文件末尾
 */
static bool VXFileDevice_atEnd(const XFileDevice* device)
{
    if (!device || XIODevice_fd(&device->m_parent) < 0) {
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
    if (!device || XIODevice_fd(&device->m_parent) < 0) {
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
    if (!device || XIODevice_fd(&device->m_parent) < 0) {
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
    
    if (XIODevice_fd(&device->m_parent) < 0) {
        return -1;
    }
    
    if (!XIODevice_isReadable(&device->m_parent)) {
        return -1;
    }

    return XFileSystem_read(XIODevice_fd(&device->m_parent), data, maxlen);
}

/**
 * @brief 虚函数：写入数据
 */
static int64_t VXFileDevice_writeData(XFileDevice* device, const char* data, int64_t len)
{
    if (!device || !data || len <= 0) {
        return -1;
    }
    
    if (XIODevice_fd(&device->m_parent) < 0) {
        return -1;
    }
    
    if (!XIODevice_isWritable(&device->m_parent)) {
        return -1;
    }
    
    return XFileSystem_write(XIODevice_fd(&device->m_parent), data, len);
}

/**
 * @brief 虚函数：读取一行数据
 */
static int64_t VXFileDevice_readLineData(XFileDevice* device, char* data, int64_t maxlen)
{
    if (!device || !data || maxlen <= 0) {
        return -1;
    }
    
    if (XIODevice_fd(&device->m_parent) < 0) {
        return -1;
    }
    
    // 逐字节读取直到遇到换行符或达到最大长度
        int64_t bytesRead = 0;  // 声明并初始化
        char c;
    
        while (bytesRead < maxlen - 1) {
        int64_t n = XFileSystem_read(XIODevice_fd(&device->m_parent), &c, 1);
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
    
    if (XIODevice_fd(&device->m_parent) < 0) {
        return 0;
    }
    
    if (XFileDevice_isSequential_base(device)) {
        // 顺序设备：需要读取并丢弃数据
        char temp[4096];
        int64_t skipped = 0;
        
        while (skipped < maxSize) {
            int64_t remaining = maxSize - skipped;
            int64_t toRead = (remaining > (int64_t)sizeof(temp)) ? (int64_t)sizeof(temp) : remaining;

            int64_t n = XFileSystem_read(XIODevice_fd(&device->m_parent), temp, toRead);
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
        XFileSystem_seek(XIODevice_fd(&device->m_parent), newPos, XSeekSet);
    }
    
    return actualSkip;
}

/* ============================================================================
 * mmap 跟踪: 维护 address -> size 映射
 * 注: 真实 Qt 用 QFileEnginePrivate 跟踪, 这里使用进程级简单线性表
 * ============================================================================ */
#define XFILE_MMAP_TRACK_MAX 32
typedef struct XFileMmapEntry { void* addr; int64_t size; bool inUse; } XFileMmapEntry;
static XFileMmapEntry g_mmapEntries[XFILE_MMAP_TRACK_MAX] = {0};

static void mmapTrack(void* addr, int64_t size) {
    if (!addr) return;
    for (int i = 0; i < XFILE_MMAP_TRACK_MAX; ++i) {
        if (!g_mmapEntries[i].inUse) {
            g_mmapEntries[i].addr = addr;
            g_mmapEntries[i].size = size;
            g_mmapEntries[i].inUse = true;
            return;
        }
    }
}

static int64_t mmapLookupSize(void* addr) {
    if (!addr) return 0;
    for (int i = 0; i < XFILE_MMAP_TRACK_MAX; ++i) {
        if (g_mmapEntries[i].inUse && g_mmapEntries[i].addr == addr) {
            return g_mmapEntries[i].size;
        }
    }
    return 0;
}

static void mmapUntrack(void* addr) {
    if (!addr) return;
    for (int i = 0; i < XFILE_MMAP_TRACK_MAX; ++i) {
        if (g_mmapEntries[i].inUse && g_mmapEntries[i].addr == addr) {
            g_mmapEntries[i].inUse = false;
            g_mmapEntries[i].addr = NULL;
            g_mmapEntries[i].size = 0;
            return;
        }
    }
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
    XVTABLE_INIT_DEFAULT(XFileDevice)
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

    XCLASS_SHOW_SIZE_DEFAULT(XFileDevice);

    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

XFileDevice* XFileDevice_create_ex(XMemoryType memory)
{
    XFileDevice* device = (XFileDevice*)XMemory_malloc(sizeof(XFileDevice), memory);
    if (!device) return NULL;

    XFileDevice_init(device);
    Set_Class_Memory(device, memory); Set_Class_IsHeap(device, true);
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
    XIODevice_setFd(&device->m_parent, XFD_INVALID);
    device->m_handleFlags = XFileDevice_DontCloseHandle;
    device->m_cachedSize = -1;  // -1 表示无缓存
}

/* ============================================================================
 * IoRing 异步注册（将 fd 关联到全局 XAbstractNetIoRing）
 * ============================================================================ */

void XFileDevice_registerAsync(XFileDevice* device)
{
#if XAbstractNetIoRing_ON
    if (!device) return;
    XFd fd = XIODevice_fd(&device->m_parent);
    if (fd < 0) return;
    /* 设置 ctx 为 device 指针，供 IoRing 完成事件分发查找 owner */
    XFd_setCtx(fd, (void*)device);
    XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
    if (ring) {
        XAbstractNetIoRing_registerEvent_base(ring, fd);
    }
#endif
}

void XFileDevice_unregisterAsync(XFileDevice* device)
{
#if XAbstractNetIoRing_ON
    if (!device) return;
    XFd fd = XIODevice_fd(&device->m_parent);
    if (fd < 0) return;
    XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
    if (ring) {
        XAbstractNetIoRing_unregisterEvent_base(ring, fd);
    }
#endif
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
 * 非虚函数 - 使用 XFileSystem API 实现
 * ============================================================================ */

bool XFileDevice_flush(XFileDevice* device)
{
    if (!device || XIODevice_fd(&device->m_parent) < 0) return false;
    return XFileSystem_flush(XIODevice_fd(&device->m_parent));
}

XFd XFileDevice_handle(const XFileDevice* device)
{
    if (!device) return XFD_INVALID;
    return XIODevice_fd(&device->m_parent);
}

XDateTime XFileDevice_fileTime(const XFileDevice* device, XFileTime time)
{
    XDateTime result = XDateTime_create();
    if (!device || XIODevice_fd(&device->m_parent) < 0) return result;
    
    XFileStat stat;
    if (!XFileSystem_fstat(XIODevice_fd(&device->m_parent), &stat)) return result;
    
    int64_t timestamp = 0;
    switch (time) {
        case XFile_BirthTime: timestamp = stat.birthTime; break;
        case XFile_MetadataChangeTime: timestamp = stat.metadataChangeTime; break;
        case XFile_ModificationTime: timestamp = stat.modificationTime; break;
        case XFile_AccessTime: timestamp = stat.accessTime; break;
    }
    
    if (timestamp > 0) {
        XDateTime_setSecsSinceEpoch(&result, timestamp);
    }
    return result;
}

bool XFileDevice_setFileTime(XFileDevice* device, const XDateTime* newDate, XFileTime time)
{
    if (!device || XIODevice_fd(&device->m_parent) < 0 || !newDate) return false;
    /* 平台层 XFileSystem_setFileTime 接受 Unix 时间戳 (int64_t 秒).
       XDateTime 的语义值 (秒数) 由 XDateTime_toSecsSinceEpoch 派生,
       上层调到这里仍然是 XDateTime*, 转换在边界一次性完成. */
    int64_t timestamp = XDateTime_toSecsSinceEpoch(newDate);
    XFd fd = XIODevice_fd(&device->m_parent);

    /* 优先使用 fd 版：直接操作已打开的句柄，无需路径。
       Win32 后端通过 SetFileTime(HANDLE) 实现；
       FatFs 后端无 f_futime 接口，返回 false。*/
    if (XFileSystem_setFileTime(fd, time, timestamp))
        return true;

    /* 回退到路径版：打开文件→设置时间→关闭 */
    const XString* fileName = XFileDevice_fileName_base(device);
    if (!fileName) return false;
    int openErr = 0;
    XFd tempFd = XFileSystem_open(fileName, XFileSystem_WriteOnly, &openErr);
    if (tempFd == XFD_INVALID) return false;
    bool result = XFileSystem_setFileTime(tempFd, time, timestamp);
    XFileSystem_close(tempFd);
    return result;
}

void* XFileDevice_map(XFileDevice* device, int64_t offset, int64_t size, XFileDeviceMemoryMapFlags flags)
{
    if (!device || XIODevice_fd(&device->m_parent) < 0) return NULL;
    /* Qt 行为: mapping 的可写性继承自打开模式, 但 MapPrivateOption 强制可写 */
    bool writable = (device->m_parent.m_openMode & XIODevice_WriteOnly) != 0;
    int mapFlags = (flags & XFileDevice_MapPrivateOption) ? 0x1 : 0x0;
    if (flags & XFileDevice_MapPrivateOption) writable = true;
    if (writable) mapFlags |= 0x2;
    void* addr = XFileSystem_map(XIODevice_fd(&device->m_parent), offset, size, mapFlags);
    if (addr) mmapTrack(addr, size);
    return addr;
}

bool XFileDevice_unmap(XFileDevice* device, void* address)
{
    if (!device || !address) return false;
    int64_t sz = mmapLookupSize(address);
    bool ok = XFileSystem_unmap(address, sz);
    if (ok) mmapUntrack(address);
    return ok;
}
#endif // XFILEDEVICE_ON
#endif /* XFILE_ON */
