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
 * 平台相关核心函数实现
 * ============================================================================ */

/**
 * @brief 获取当前文件位置
 */
int64_t XFileDevice_pos_impl(const XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        return 0;
    }
    
    // 使用 _lseeki64 获取当前位置
    int64_t pos = _lseeki64(device->m_fileHandle, 0, SEEK_CUR);
    if (pos == -1LL) {
        return 0;
    }
    
    return pos;
}

/**
 * @brief 获取文件大小
 */
int64_t XFileDevice_size_impl(const XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        return -1;
    }
    
    // 获取文件大小
    struct _stati64 st;
    if (_fstati64(device->m_fileHandle, &st) != 0) {
        return -1;
    }
    
    return st.st_size;
}

/**
 * @brief 定位文件指针
 */
bool XFileDevice_seek_impl(XFileDevice* device, int64_t pos)
{
    if (!device || device->m_fileHandle < 0 || pos < 0) {
        return false;
    }
    
    int64_t result = _lseeki64(device->m_fileHandle, pos, SEEK_SET);
    if (result == -1LL) {
        device->m_error = XFileDevice_PositionError;
        return false;
    }
    
    return true;
}

/**
 * @brief 读取数据
 */
int64_t XFileDevice_readData_impl(XFileDevice* device, char* data, int64_t maxlen)
{
    if (!device || !data || maxlen <= 0 || device->m_fileHandle < 0) {
        return -1;
    }
    
    // 处理文本模式下的换行符转换
    if (device->m_parent.m_textModeEnabled) {
        // 文本模式读取
        DWORD bytesRead = 0;
        HANDLE hFile = (HANDLE)_get_osfhandle(device->m_fileHandle);
        if (hFile == INVALID_HANDLE_VALUE) {
            device->m_error = XFileDevice_ReadError;
            return -1;
        }
        
        if (!ReadFile(hFile, data, (DWORD)maxlen, &bytesRead, NULL)) {
            device->m_error = XFileDevice_ReadError;
            return -1;
        }
        
        return (int64_t)bytesRead;
    }
    
    // 二进制模式读取
    int64_t totalRead = 0;
    while (totalRead < maxlen) {
        int64_t toRead = maxlen - totalRead;
        if (toRead > UINT_MAX) toRead = UINT_MAX;
        
        size_t bytesRead = _read(device->m_fileHandle, data + totalRead, (unsigned int)toRead);
        if (bytesRead < 0) {
            device->m_error = XFileDevice_ReadError;
            return -1;
        }
        if (bytesRead == 0) {
            break; // EOF
        }
        totalRead += bytesRead;
    }
    
    return totalRead;
}

/**
 * @brief 写入数据
 */
int64_t XFileDevice_writeData_impl(XFileDevice* device, const char* data, int64_t len)
{
    if (!device || !data || len <= 0 || device->m_fileHandle < 0) {
        return -1;
    }
    
    // 处理文本模式下的换行符转换
    if (device->m_parent.m_textModeEnabled) {
        // 文本模式写入
        DWORD bytesWritten = 0;
        HANDLE hFile = (HANDLE)_get_osfhandle(device->m_fileHandle);
        if (hFile == INVALID_HANDLE_VALUE) {
            device->m_error = XFileDevice_WriteError;
            return -1;
        }
        
        if (!WriteFile(hFile, data, (DWORD)len, &bytesWritten, NULL)) {
            device->m_error = XFileDevice_WriteError;
            return -1;
        }
        
        return (int64_t)bytesWritten;
    }
    
    // 二进制模式写入
    int64_t totalWritten = 0;
    while (totalWritten < len) {
        int64_t toWrite = len - totalWritten;
        if (toWrite > UINT_MAX) toWrite = UINT_MAX;
        
        size_t bytesWritten = _write(device->m_fileHandle, data + totalWritten, (unsigned int)toWrite);
        if (bytesWritten < 0) {
            device->m_error = XFileDevice_WriteError;
            return -1;
        }
        if (bytesWritten == 0) {
            break;
        }
        totalWritten += bytesWritten;
    }
    
    return totalWritten;
}

/* ============================================================================
 * 刷新缓冲区
 * ============================================================================ */

bool XFileDevice_flush(XFileDevice* device)
{
    if (!device || device->m_fileHandle < 0) {
        if (device) device->m_error = XFileDevice_ResourceError;
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
    
    FILETIME ftCreate, ftAccess, ftWrite;
    if (!GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite)) {
        return result;
    }
    
    FILETIME* ft = NULL;
    switch (time) {
        case XFile_AccessTime:
            ft = &ftAccess;
            break;
        case XFile_BirthTime:
            ft = &ftCreate;
            break;
        case XFile_MetadataChangeTime:
        case XFile_ModificationTime:
            ft = &ftWrite;
            break;
    }
    
    if (ft) {
        // 转换 FILETIME 到 Unix 时间戳
        ULARGE_INTEGER ul;
        ul.LowPart = ft->dwLowDateTime;
        ul.HighPart = ft->dwHighDateTime;
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
    
    FILETIME* ftCreate = NULL;
    FILETIME* ftAccess = NULL;
    FILETIME* ftWrite = NULL;
    
    switch (time) {
        case XFile_AccessTime:
            ftAccess = &ft;
            break;
        case XFile_BirthTime:
            ftCreate = &ft;
            break;
        case XFile_MetadataChangeTime:
        case XFile_ModificationTime:
            ftWrite = &ft;
            break;
    }
    
    if (!SetFileTime(hFile, ftCreate, ftAccess, ftWrite)) {
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