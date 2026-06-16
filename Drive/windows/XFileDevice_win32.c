#ifdef _WIN32
#include "XFileDevice.h"
#include "XMemory.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ============================================================================
 * 文件句柄操作
 * ============================================================================ */

int XFileDevice_handle(const XFileDevice* device)
{
    if (!device) return -1;
    return device->m_fileHandle;
}

/* ============================================================================
 * 刷新缓冲区
 * ============================================================================ */

bool XFileDevice_flush(XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        device->m_error = XFileDevice_ResourceError;
        return false;
    }
    
    // Windows 下使用 FlushFileBuffers
    HANDLE hFile = (HANDLE)_get_osfhandle(device->m_fileHandle);
    if (hFile == INVALID_HANDLE_VALUE) {
        device->m_error = XFileDevice_ResourceError;
        return false;
    }
    
    if (!FlushFileBuffers(hFile)) {
        device->m_error = XFileDevice_WriteError;
        return false;
    }
    
    return true;
}

/* ============================================================================
 * 文件时间操作
 * ============================================================================ */

XDateTime XFileDevice_fileTime(const XFileDevice* device, XFileTime time)
{
    XDateTime result = XDateTime_create();
    
    if (!device || device->m_fileHandle < 0) {
        return result;
    }
    
    HANDLE hFile = (HANDLE)_get_osfhandle(device->m_fileHandle);
    if (hFile == INVALID_HANDLE_VALUE) {
        return result;
    }
    
    FILETIME ft;
    BOOL success = FALSE;
    
    switch (time) {
        case XFile_AccessTime:
            success = GetFileTime(hFile, NULL, NULL, &ft);
            break;
        case XFile_BirthTime:
            success = GetFileTime(hFile, &ft, NULL, NULL);
            break;
        case XFile_MetadataChangeTime:
        case XFile_ModificationTime:
            success = GetFileTime(hFile, NULL, NULL, &ft);
            break;
    }
    
    if (success) {
        // 转换 FILETIME 到 Unix 时间戳
        ULARGE_INTEGER ul;
        ul.LowPart = ft.dwLowDateTime;
        ul.HighPart = ft.dwHighDateTime;
        int64_t timestamp = (int64_t)((ul.QuadPart - 116444736000000000LL) / 10000000);
        XDateTime_setSecsSinceEpoch(&result, timestamp);
    }
    
    return result;
}

bool XFileDevice_setFileTime(XFileDevice* device, const XDateTime* newDate, XFileTime time)
{
    if (!device || device->m_fileHandle < 0 || !newDate) {
        if (device) device->m_error = XFileDevice_ResourceError;
        return false;
    }
    
    HANDLE hFile = (HANDLE)_get_osfhandle(device->m_fileHandle);
    if (hFile == INVALID_HANDLE_VALUE) {
        device->m_error = XFileDevice_ResourceError;
        return false;
    }
    
    // 获取 Unix 时间戳并转换为 FILETIME
    int64_t timestamp = XDateTime_toSecsSinceEpoch(newDate);
    ULARGE_INTEGER ul;
    ul.QuadPart = (timestamp * 10000000LL) + 116444736000000000LL;
    
    FILETIME ft;
    ft.dwLowDateTime = ul.LowPart;
    ft.dwHighDateTime = ul.HighPart;
    
    BOOL success = FALSE;
    
    switch (time) {
        case XFile_AccessTime:
            success = SetFileTime(hFile, NULL, &ft, NULL);
            break;
        case XFile_BirthTime:
            success = SetFileTime(hFile, &ft, NULL, NULL);
            break;
        case XFile_MetadataChangeTime:
        case XFile_ModificationTime:
            success = SetFileTime(hFile, NULL, NULL, &ft);
            break;
    }
    
    if (!success) {
        device->m_error = XFileDevice_WriteError;
        return false;
    }
    
    return true;
}

/* ============================================================================
 * 内存映射
 * ============================================================================ */

void* XFileDevice_map(XFileDevice* device, int64_t offset, int64_t size, XFileDeviceMemoryMapFlags flags)
{
    if (!device || device->m_fileHandle < 0 || size <= 0) {
        if (device) device->m_error = XFileDevice_ResourceError;
        return NULL;
    }
    
    HANDLE hFile = (HANDLE)_get_osfhandle(device->m_fileHandle);
    if (hFile == INVALID_HANDLE_VALUE) {
        device->m_error = XFileDevice_ResourceError;
        return NULL;
    }
    
    // 确定保护属性
    DWORD protect = PAGE_READONLY;
    if (device->m_parent.m_openMode & XIODevice_WriteOnly) {
        if (flags & XFileDevice_MapPrivateOption) {
            protect = PAGE_WRITECOPY;
        } else {
            protect = PAGE_READWRITE;
        }
    }
    
    // 创建文件映射对象
    DWORD sizeHigh = (DWORD)(size >> 32);
    DWORD sizeLow = (DWORD)(size & 0xFFFFFFFF);
    
    HANDLE hMap = CreateFileMappingA(hFile, NULL, protect, sizeHigh, sizeLow, NULL);
    if (!hMap) {
        device->m_error = XFileDevice_ResourceError;
        return NULL;
    }
    
    // 确定映射访问模式
    DWORD access = FILE_MAP_READ;
    if (device->m_parent.m_openMode & XIODevice_WriteOnly) {
        if (flags & XFileDevice_MapPrivateOption) {
            access = FILE_MAP_COPY;
        } else {
            access = FILE_MAP_WRITE;
        }
    }
    
    // 映射视图
    DWORD offsetHigh = (DWORD)(offset >> 32);
    DWORD offsetLow = (DWORD)(offset & 0xFFFFFFFF);
    
    void* address = MapViewOfFile(hMap, access, offsetHigh, offsetLow, (SIZE_T)size);
    
    // 关闭映射句柄（视图仍然有效）
    CloseHandle(hMap);
    
    if (!address) {
        device->m_error = XFileDevice_ResourceError;
        return NULL;
    }
    
    return address;
}

bool XFileDevice_unmap(XFileDevice* device, void* address)
{
    if (!device || !address) {
        if (device) device->m_error = XFileDevice_ResourceError;
        return false;
    }
    
    if (!UnmapViewOfFile(address)) {
        device->m_error = XFileDevice_ResourceError;
        return false;
    }
    
    return true;
}

#endif // _WIN32