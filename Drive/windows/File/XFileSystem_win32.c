#ifdef _WIN32

#include "XFileSystem_config.h"  /* 文件系统配置 */

/* 仅在启用平台API模式时编译此文件 */
#if defined(XFILE_USE_PLATFORM_API)

#include "XFileSystem.h"
#include "XFileDevice.h"
#include "XMemory.h"
#include "XString.h"
#include "XFileDescriptor.h"  /* XFd_alloc, XFd_free, XFd_handle, XFd_type */
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>
#include <direct.h>
#include <ctype.h>
#include <initguid.h>
#include <shobjidl.h>
#include <objbase.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

/* 重解析点结构体 */
#pragma pack(push, 1)
typedef struct _REPARSE_DATA_BUFFER {
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG Flags;
            WCHAR PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
            UCHAR DataBuffer[1];
        } GenericReparseBuffer;
    };
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#pragma pack(pop)

#ifndef SE_FILE_OBJECT
#define SE_FILE_OBJECT 1
#endif

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief 将 XString 转换为 Windows 宽字符路径
 */
static wchar_t* XStringToWidePath(const XString* path)
{
    if (!path) return NULL;
    
    const char* utf8Path = XString_toUtf8(path);
    if (!utf8Path) return NULL;
    
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, NULL, 0);
    if (len <= 0) return NULL;
    
    wchar_t* wpath = (wchar_t*)XMalloc_System(len * sizeof(wchar_t));
    if (!wpath) return NULL;
    
    MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, wpath, len);
    return wpath;
}

/**
 * @brief 将 XString 转换为 UTF-8 字符串指针
 */
static const char* XStringToUtf8(const XString* str)
{
    if (!str) return NULL;
    return XString_toUtf8(str);
}

/**
 * @brief 将 Windows 文件时间转换为 Unix 时间戳
 */
static int64_t fileTimeToUnixTime(const FILETIME* ft)
{
    if (!ft) return 0;
    ULARGE_INTEGER ul;
    ul.LowPart = ft->dwLowDateTime;
    ul.HighPart = ft->dwHighDateTime;
    return (int64_t)((ul.QuadPart - 116444736000000000LL) / 10000000);
}

/**
 * @brief 通过 XFd 获取底层 Windows HANDLE（XFileDescriptor 表中存储的是 HANDLE）
 */
static HANDLE XW32_getFile(XFd fd)
{
    if (fd < 0) return INVALID_HANDLE_VALUE;
    HANDLE h = (HANDLE)XFd_handle(fd);
    if (h == INVALID_HANDLE_VALUE || !h) return INVALID_HANDLE_VALUE;
    if (XFd_type(fd) != XFD_TYPE_FILE) return INVALID_HANDLE_VALUE;
    return h;
}

/**
 * @brief 获取文件属性并填充 XFileStat（内部函数，使用UTF-8路径）
 */
static bool fillFileStatUtf8(const char* path, WIN32_FILE_ATTRIBUTE_DATA* attrData, XFileStat* stat)
{
    if (!path || !stat) return false;
    
    memset(stat, 0, sizeof(XFileStat));
    
    /* 转换为宽字符 */
    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (len <= 0) return false;
    
    wchar_t* wpath = (wchar_t*)XMalloc_System(len * sizeof(wchar_t));
    if (!wpath) return false;
    
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, len);
    
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, attrData)) {
        XFree_System(wpath);
        stat->exists = false;
        return false;
    }
    XFree_System(wpath);
    
    stat->exists = true;
    stat->isDir = (attrData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    stat->isFile = !stat->isDir;
    stat->isHidden = (attrData->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    stat->isSymLink = (attrData->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    stat->isJunction = false;
    stat->isShortcut = false;
    
    ULARGE_INTEGER fileSize;
    fileSize.LowPart = attrData->nFileSizeLow;
    fileSize.HighPart = attrData->nFileSizeHigh;
    stat->size = (int64_t)fileSize.QuadPart;
    
    stat->birthTime = fileTimeToUnixTime(&attrData->ftCreationTime);
    stat->modificationTime = fileTimeToUnixTime(&attrData->ftLastWriteTime);
    stat->accessTime = fileTimeToUnixTime(&attrData->ftLastAccessTime);
    stat->metadataChangeTime = stat->modificationTime;
    
    stat->isReadable = true;
    stat->isWritable = (attrData->dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;
    
    if (stat->isFile) {
        const char* ext = strrchr(path, '.');
        if (ext) {
            stat->isExecutable = (_stricmp(ext, ".exe") == 0 ||
                                   _stricmp(ext, ".bat") == 0 ||
                                   _stricmp(ext, ".cmd") == 0 ||
                                   _stricmp(ext, ".com") == 0);
        }
    }
    
    stat->permissions = 0;
    if (stat->isReadable) stat->permissions |= XFile_ReadOwner;
    if (stat->isWritable) stat->permissions |= XFile_WriteOwner;
    if (stat->isExecutable) stat->permissions |= XFile_ExeOwner;
    
    return true;
}

/**
 * @brief 获取文件属性并填充 XFileStat
 */
static bool fillFileStat(const XString* path, WIN32_FILE_ATTRIBUTE_DATA* attrData, XFileStat* stat)
{
    const char* utf8Path = XStringToUtf8(path);
    if (!utf8Path) return false;
    return fillFileStatUtf8(utf8Path, attrData, stat);
}

/* ============================================================================
 * 核心文件操作
 * ============================================================================ */

XFd XFileSystem_open(const XString* path, int mode, int* error)
{
    wchar_t* wpath = XStringToWidePath(path);
    if (!wpath) {
        if (error) *error = XFileDevice_ResourceError;
        return XFD_INVALID;
    }
    
    DWORD desiredAccess = 0;
    DWORD shareMode = FILE_SHARE_READ;
    DWORD creationDisposition = OPEN_EXISTING;
    
    switch (mode & 0x3) {
    case XFileSystem_ReadOnly:
        desiredAccess |= GENERIC_READ;
        break;
    case XFileSystem_WriteOnly:
        desiredAccess |= GENERIC_WRITE;
        break;
    case XFileSystem_ReadWrite:
        desiredAccess |= GENERIC_READ | GENERIC_WRITE;
        break;
    default:
        break;
    }
    
    if (mode & XFileSystem_Append) {
        desiredAccess |= GENERIC_WRITE;
        creationDisposition = OPEN_ALWAYS;
    } else if (mode & XFileSystem_Truncate) {
        creationDisposition = CREATE_ALWAYS;
    } else if (mode & XFileSystem_NewOnly) {
        creationDisposition = CREATE_NEW;
    } else if (mode & XFileSystem_Existing) {
        creationDisposition = OPEN_EXISTING;
    } else if (mode & XFileSystem_Create) {
        creationDisposition = OPEN_ALWAYS;
    } else if (mode & XFileSystem_WriteOnly) {
        creationDisposition = OPEN_ALWAYS;
    }
    
    HANDLE hFile = CreateFileW(wpath, desiredAccess, shareMode, NULL,
                                creationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    XFree_System(wpath);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) *error = XFileDevice_OpenError;
        return XFD_INVALID;
    }
    
    /* 将 HANDLE 存入 XFileDescriptor 统一 fd 表 */
    XFd fd = XFd_alloc(XFD_TYPE_FILE, hFile, NULL);
    if (fd == XFD_INVALID) {
        CloseHandle(hFile);
        if (error) *error = XFileDevice_ResourceError;
        return XFD_INVALID;
    }
    
    if (mode & XFileSystem_Append) {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        SetFilePointerEx(hFile, zero, NULL, FILE_END);
    }
    
    if (error) *error = XFileDevice_NoError;
    return fd;
}

XFd XFileSystem_openStandardInput(int* error)
{
    HANDLE source;
    HANDLE duplicate = INVALID_HANDLE_VALUE;
    DWORD type;
    if (!GetStdHandle(STD_INPUT_HANDLE) ||
        GetStdHandle(STD_INPUT_HANDLE) == INVALID_HANDLE_VALUE) {
        if (error) *error = XFileDevice_OpenError;
        return XFD_INVALID;
    }
    source = GetStdHandle(STD_INPUT_HANDLE);
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
                         &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        if (error) *error = XFileDevice_OpenError;
        return XFD_INVALID;
    }
    type = GetFileType(duplicate);
    if (type == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR) {
        CloseHandle(duplicate);
        if (error) *error = XFileDevice_OpenError;
        return XFD_INVALID;
    }
    if (error) *error = XFileDevice_NoError;
    return XFd_alloc(XFD_TYPE_FILE, duplicate, NULL);
}

void XFileSystem_close(XFd fd)
{
    HANDLE h = XW32_getFile(fd);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
    XFd_free(fd);
}

int64_t XFileSystem_seek(XFd fd, int64_t offset, XSeekWhence whence)
{
    HANDLE h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD method;
    switch (whence) {
        case XSeekSet: method = FILE_BEGIN; break;
        case XSeekCur: method = FILE_CURRENT; break;
        case XSeekEnd: method = FILE_END; break;
        default: return -1;
    }
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    LARGE_INTEGER newpos;
    if (!SetFilePointerEx(h, li, &newpos, method)) return -1;
    return (int64_t)newpos.QuadPart;
}

int64_t XFileSystem_read(XFd fd, void* buf, int64_t len)
{
    HANDLE h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE || !buf || len <= 0) return -1;

    /* 管道和重定向输入先探测可读量，避免事件线程等待数据。 */
    if (GetFileType(h) == FILE_TYPE_PIPE) {
        DWORD available = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &available, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) return 0;
            return -1;
        }
        if (available == 0) return 0;
        if ((uint64_t)len > available) len = available;
    }
    
    int64_t totalRead = 0;
    while (totalRead < len) {
        int64_t toRead = len - totalRead;
        if (toRead > 0xFFFFFFFF) toRead = 0xFFFFFFFF;
        
        DWORD bytesRead = 0;
        if (!ReadFile(h, (char*)buf + totalRead, (DWORD)toRead, &bytesRead, NULL)) {
            return -1;
        }
        if (bytesRead == 0) break;
        totalRead += bytesRead;
    }
    return totalRead;
}

int64_t XFileSystem_readStandardInput(XFd fd, void* buf, int64_t len)
{
    HANDLE h = XW32_getFile(fd);
    DWORD available = 0;
    DWORD bytesRead = 0;
    DWORD request;
    if (h == INVALID_HANDLE_VALUE || !buf || len <= 0) return -2;
    if (GetFileType(h) == FILE_TYPE_PIPE) {
        if (!PeekNamedPipe(h, NULL, 0, NULL, &available, NULL)) {
            return GetLastError() == ERROR_BROKEN_PIPE ? -1 : -2;
        }
        if (available == 0) return 0;
        request = (DWORD)((uint64_t)len < available ? (uint64_t)len : available);
    } else if (GetFileType(h) == FILE_TYPE_CHAR) {
        DWORD events = 0;
        if (!GetNumberOfConsoleInputEvents(h, &events)) return -2;
        if (events == 0) return 0;
        request = (DWORD)((uint64_t)len > UINT32_MAX ? UINT32_MAX : (uint64_t)len);
        if (!ReadConsoleA(h, buf, request, &bytesRead, NULL)) return -2;
        return bytesRead ? (int64_t)bytesRead : 0;
    } else {
        request = (DWORD)((uint64_t)len > UINT32_MAX ? UINT32_MAX : (uint64_t)len);
    }
    if (!ReadFile(h, buf, request, &bytesRead, NULL)) return -2;
    return bytesRead ? (int64_t)bytesRead : -1;
}

bool XFileSystem_setStandardInputEcho(XFd fd, bool enabled)
{
    HANDLE h = XW32_getFile(fd);
    DWORD mode;

    /* 管道和重定向句柄没有控制台回显位，不能伪造成功状态。 */
    if (h == INVALID_HANDLE_VALUE || GetFileType(h) != FILE_TYPE_CHAR) return false;
    if (!GetConsoleMode(h, &mode)) return false;
    if (enabled) {
        mode |= ENABLE_ECHO_INPUT;
    } else {
        mode &= (DWORD)~ENABLE_ECHO_INPUT;
    }
    return SetConsoleMode(h, mode) != 0;
}

int64_t XFileSystem_write(XFd fd, const void* buf, int64_t len)
{
    HANDLE h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE || !buf || len <= 0) return -1;
    
    int64_t totalWritten = 0;
    while (totalWritten < len) {
        int64_t toWrite = len - totalWritten;
        if (toWrite > 0xFFFFFFFF) toWrite = 0xFFFFFFFF;
        
        DWORD bytesWritten = 0;
        if (!WriteFile(h, (const char*)buf + totalWritten, (DWORD)toWrite, &bytesWritten, NULL)) {
            return -1;
        }
        if (bytesWritten == 0) break;
        totalWritten += bytesWritten;
    }
    return totalWritten;
}

bool XFileSystem_flush(XFd fd)
{
    HANDLE h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    return FlushFileBuffers(h) != 0;
}

bool XFileSystem_resize(XFd fd, int64_t size)
{
    HANDLE h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE || size < 0) return false;
    
    /* 保存当前位置 */
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    LARGE_INTEGER oldPos;
    if (!SetFilePointerEx(h, zero, &oldPos, FILE_CURRENT)) return false;
    
    /* 定位到目标大小 */
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)size;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN)) return false;
    
    if (!SetEndOfFile(h)) {
        /* 恢复原位置 */
        SetFilePointerEx(h, oldPos, NULL, FILE_BEGIN);
        return false;
    }
    
    /* 恢复原位置 */
    SetFilePointerEx(h, oldPos, NULL, FILE_BEGIN);
    return true;
}

/* ============================================================================
 * 文件属性操作
 * ============================================================================ */

bool XFileSystem_stat(const XString* path, XFileStat* stat)
{
    if (!path || !stat) return false;
    
    WIN32_FILE_ATTRIBUTE_DATA attrData;
    return fillFileStat(path, &attrData, stat);
}

bool XFileSystem_fstat(XFd fd, XFileStat* stat)
{
    HANDLE h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE || !stat) return false;
    
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) return false;
    
    memset(stat, 0, sizeof(XFileStat));
    stat->exists = true;
    stat->isDir = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    stat->isFile = !stat->isDir;
    stat->isHidden = (info.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    stat->isSymLink = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    stat->isJunction = false;
    stat->isShortcut = false;
    
    ULARGE_INTEGER fileSize;
    fileSize.LowPart = info.nFileSizeLow;
    fileSize.HighPart = info.nFileSizeHigh;
    stat->size = (int64_t)fileSize.QuadPart;
    
    stat->birthTime = fileTimeToUnixTime(&info.ftCreationTime);
    stat->modificationTime = fileTimeToUnixTime(&info.ftLastWriteTime);
    stat->accessTime = fileTimeToUnixTime(&info.ftLastAccessTime);
    stat->metadataChangeTime = stat->modificationTime;
    
    stat->isReadable = true;
    stat->isWritable = (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;
    
    return true;
}


bool XFileSystem_remove(const XString* path, XRemoveMode mode, XString* trashPath)
{
    if (!path) return false;

    if (mode == XRemoveMode_Trash) {
        wchar_t* wpath = XStringToWidePath(path);
        if (!wpath) return false;
        size_t len = wcslen(wpath);
        wchar_t* wpathDouble = (wchar_t*)XMalloc_System((len + 2) * sizeof(wchar_t));
        if (!wpathDouble) { XFree_System(wpath); return false; }
        wcscpy(wpathDouble, wpath);
        wpathDouble[len] = 0;
        wpathDouble[len + 1] = 0;
        XFree_System(wpath);

        SHFILEOPSTRUCTW shfos;
        memset(&shfos, 0, sizeof(shfos));
        shfos.wFunc = FO_DELETE;
        shfos.pFrom = wpathDouble;
        shfos.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
        int rc = SHFileOperationW(&shfos);
        XFree_System(wpathDouble);
        if (rc == 0 && !shfos.fAnyOperationsAborted) {
            if (trashPath) XString_assign_utf8(trashPath, "");
            return true;
        }
        /* 回收站不可用时退化为永久删除 */
    }

    wchar_t* wpath = XStringToWidePath(path);
    if (!wpath) return false;

    BOOL result = DeleteFileW(wpath);
    if (!result) {
        result = RemoveDirectoryW(wpath);
    }

    XFree_System(wpath);
    return result != 0;
}

bool XFileSystem_rename(const XString* oldPath, const XString* newPath)
{
    if (!oldPath || !newPath) return false;
    
    wchar_t* wold = XStringToWidePath(oldPath);
    wchar_t* wnew = XStringToWidePath(newPath);
    
    if (!wold || !wnew) {
        if (wold) XFree_System(wold);
        if (wnew) XFree_System(wnew);
        return false;
    }
    
    BOOL result = MoveFileW(wold, wnew);
    XFree_System(wold);
    XFree_System(wnew);
    
    return result != 0;
}

bool XFileSystem_copy(const XString* srcPath, const XString* dstPath)
{
    if (!srcPath || !dstPath) return false;
    
    wchar_t* wsrc = XStringToWidePath(srcPath);
    wchar_t* wdst = XStringToWidePath(dstPath);
    
    if (!wsrc || !wdst) {
        if (wsrc) XFree_System(wsrc);
        if (wdst) XFree_System(wdst);
        return false;
    }
    
    BOOL result = CopyFileW(wsrc, wdst, TRUE);
    XFree_System(wsrc);
    XFree_System(wdst);
    
    return result != 0;
}

/* ============================================================================
 * 目录操作
 * ============================================================================ */

bool XFileSystem_mkdir(const XString* path, bool recursive)
{
    if (!path) return false;
    
    const char* utf8Path = XStringToUtf8(path);
    if (!utf8Path) return false;
    
    if (!recursive) {
        wchar_t* wpath = XStringToWidePath(path);
        if (!wpath) return false;
        BOOL result = CreateDirectoryW(wpath, NULL);
        XFree_System(wpath);
        return result != 0;
    }
    
    /* 递归创建目录 */
    char* pathCopy = XMemory_strdup(utf8Path);
    if (!pathCopy) return false;
    
    for (char* p = pathCopy; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    
    char* p = pathCopy;
    if (isalpha((unsigned char)p[0]) && p[1] == ':') p += 2;
    
    bool result = true;
    while (*p) {
        if (*p == '\\') {
            *p = '\0';
            wchar_t* wpath = XStringToWidePath(path);
            if (wpath) {
                if (GetFileAttributesW(wpath) == INVALID_FILE_ATTRIBUTES) {
                    if (!CreateDirectoryW(wpath, NULL)) result = false;
                }
                XFree_System(wpath);
            }
            *p = '\\';
        }
        p++;
    }
    
    if (result) {
        wchar_t* wpath = XStringToWidePath(path);
        if (wpath) {
            if (GetFileAttributesW(wpath) == INVALID_FILE_ATTRIBUTES) {
                result = CreateDirectoryW(wpath, NULL) != 0;
            }
            XFree_System(wpath);
        }
    }
    
    XFree_System(pathCopy);
    return result;
}

bool XFileSystem_rmdir(const XString* path, bool recursive) {
    if (!path) return false;
    wchar_t* wpath = XStringToWidePath(path);
    if (!wpath) return false;
    if (recursive) {
        size_t len = wcslen(wpath);
        wchar_t* wpathDouble = (wchar_t*)XMalloc_System((len + 2) * sizeof(wchar_t));
        if (!wpathDouble) { XFree_System(wpath); return false; }
        wcscpy(wpathDouble, wpath);
        wpathDouble[len] = 0;
        wpathDouble[len + 1] = 0;
        XFree_System(wpath);
        SHFILEOPSTRUCTW shfos = {0};
        shfos.wFunc = FO_DELETE;
        shfos.pFrom = wpathDouble;
        shfos.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
        int result = SHFileOperationW(&shfos);
        XFree_System(wpathDouble);
        return result == 0;
    }
    BOOL result = RemoveDirectoryW(wpath);
    XFree_System(wpath);
    return result != 0;
}

struct DirIteratorData {
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    bool firstEntry;
};

XDirIterator XFileSystem_opendir(const XString* path)
{
    if (!path) return NULL;
    
    /* 构造搜索通配符路径 "path\*" */
    XString* searchPathStr = XString_create_copy(path);
    if (!searchPathStr) return NULL;
    XString_append_utf8(searchPathStr, "\\*");
    
    wchar_t* wpath = XStringToWidePath(searchPathStr);
    XString_delete_base(searchPathStr);
    if (!wpath) return NULL;
    
    struct DirIteratorData* iter = (struct DirIteratorData*)XMalloc_System(sizeof(struct DirIteratorData));
    if (!iter) { XFree_System(wpath); return NULL; }
    
    iter->hFind = FindFirstFileW(wpath, &iter->findData);
    XFree_System(wpath);
    
    if (iter->hFind == INVALID_HANDLE_VALUE) {
        XFree_System(iter);
        return NULL;
    }
    
    iter->firstEntry = true;
    return (XDirIterator)iter;
}

bool XFileSystem_readdir(XDirIterator iter, XDirEntry* entry)
{
    if (!iter || !entry) return false;
    struct DirIteratorData* data = (struct DirIteratorData*)iter;
    
    BOOL result;
    if (data->firstEntry) {
        result = TRUE;
        data->firstEntry = false;
    } else {
        result = FindNextFileW(data->hFind, &data->findData);
    }
    
    if (!result) return false;
    
    /* 将宽字符文件名转换为UTF-8并设置到XString */
    char utf8Buf[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, data->findData.cFileName, -1, utf8Buf, MAX_PATH, NULL, NULL);
    if (entry->name) {
        XString_assign_utf8(entry->name, utf8Buf);
    }
    
    entry->isDir = (data->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    entry->isFile = !entry->isDir;
    entry->isSymLink = (data->findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    entry->isHidden = (data->findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    return true;
}

void XFileSystem_closedir(XDirIterator iter)
{
    if (!iter) return;
    struct DirIteratorData* data = (struct DirIteratorData*)iter;
    FindClose(data->hFind);
    XFree_System(data);
}

/* ============================================================================
 * 路径操作
 * ============================================================================ */

bool XFileSystem_resolvePath(const XString* path, XString* result, XPathStyle style)
{
    if (!path || !result) return false;
    
    const char* utf8Path = XStringToUtf8(path);
    if (!utf8Path) return false;
    
    char absPath[MAX_PATH];
    if (!_fullpath(absPath, utf8Path, MAX_PATH)) return false;
    
    if (style == XPathStyle_Canonical) {
        wchar_t* wpath = XStringToWidePath(path);
        if (!wpath) return false;
        
        DWORD attrs = GetFileAttributesW(wpath);
        XFree_System(wpath);
        
        if (attrs == INVALID_FILE_ATTRIBUTES) return false;
        
        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
            wchar_t* wpath2 = XStringToWidePath(path);
            if (wpath2) {
                HANDLE hFile = CreateFileW(wpath2, 0, FILE_SHARE_READ, NULL,
                                            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    wchar_t targetPath[MAX_PATH];
                    DWORD len = GetFinalPathNameByHandleW(hFile, targetPath, MAX_PATH, VOLUME_NAME_DOS);
                    CloseHandle(hFile);
                    
                    if (len > 0 && len < MAX_PATH) {
                        char utf8Target[MAX_PATH];
                        WideCharToMultiByte(CP_UTF8, 0, targetPath, -1, utf8Target, MAX_PATH, NULL, NULL);
                        XString_assign_utf8(result, utf8Target);
                        return true;
                    }
                }
                XFree_System(wpath2);
            }
        }
    }
    
    XString_assign_utf8(result, absPath);
    return true;
}

/* ============================================================================
 * 六、特殊路径
 * ============================================================================ */

bool XFileSystem_getSpecialPath(XSpecialPath type, XString* path)
{
    if (!path) return false;
    switch (type) {
    case XSpecialPath_Current: {
        wchar_t wPath[MAX_PATH];
        if (GetCurrentDirectoryW(MAX_PATH, wPath) == 0) return false;
        char utf8Path[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, wPath, -1, utf8Path, MAX_PATH, NULL, NULL);
        XString_assign_utf8(path, utf8Path);
        return true;
    }
    case XSpecialPath_Home: {
        wchar_t wPath[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, wPath) != S_OK) return false;
        char utf8Path[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, wPath, -1, utf8Path, MAX_PATH, NULL, NULL);
        XString_assign_utf8(path, utf8Path);
        return true;
    }
    case XSpecialPath_Root: {
        wchar_t wSysPath[MAX_PATH];
        if (GetSystemDirectoryW(wSysPath, MAX_PATH) == 0) {
            XString_assign_utf8(path, "C:\\");
            return true;
        }
        char utf8Path[4];
        utf8Path[0] = (char)wSysPath[0];
        utf8Path[1] = ':';
        utf8Path[2] = '\\';
        utf8Path[3] = '\0';
        XString_assign_utf8(path, utf8Path);
        return true;
    }
    case XSpecialPath_Temp: {
        wchar_t wTempPath[MAX_PATH];
        DWORD len = GetTempPathW(MAX_PATH, wTempPath);
        if (len == 0 || len >= MAX_PATH) return false;
        char utf8Path[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, wTempPath, -1, utf8Path, MAX_PATH, NULL, NULL);
        XString_assign_utf8(path, utf8Path);
        return true;
    }
    default:
        return false;
    }
}

bool XFileSystem_setCurrentPath(const XString* path)
{
    if (!path) return false;
    wchar_t* wpath = XStringToWidePath(path);
    if (!wpath) return false;
    BOOL result = SetCurrentDirectoryW(wpath);
    XFree_System(wpath);
    return result != 0;
}

/* ============================================================================
 * 符号链接操作
 * ============================================================================ */

bool XFileSystem_link(const XString* targetPath, const XString* linkPath, XLinkType type)
{
    if (!targetPath || !linkPath) return false;

    wchar_t* wtarget = XStringToWidePath(targetPath);
    wchar_t* wlink = XStringToWidePath(linkPath);

    if (!wtarget || !wlink) {
        if (wtarget) XFree_System(wtarget);
        if (wlink) XFree_System(wlink);
        return false;
    }

    BOOL result;
    if (type == XLinkType_Hard) {
        result = CreateHardLinkW(wlink, wtarget, NULL);
    } else {
        BOOLEAN sr = CreateSymbolicLinkW(wlink, wtarget, 0);
        if (!sr) {
            sr = CreateHardLinkW(wlink, wtarget, NULL);
        }
        result = sr;
    }

    XFree_System(wtarget);
    XFree_System(wlink);

    return result != 0;
}

bool XFileSystem_readLink(const XString* path, XString* target)
{
    if (!path || !target) return false;
    
    wchar_t* wpath = XStringToWidePath(path);
    if (!wpath) return false;
    
    HANDLE hFile = CreateFileW(wpath, 0, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    XFree_System(wpath);
    
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    wchar_t targetPath[MAX_PATH];
    DWORD len = GetFinalPathNameByHandleW(hFile, targetPath, MAX_PATH, VOLUME_NAME_DOS);
    CloseHandle(hFile);
    
    if (len == 0 || len > MAX_PATH) return false;
    
    char utf8Target[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, targetPath, -1, utf8Target, MAX_PATH, NULL, NULL);
    XString_assign_utf8(target, utf8Target);
    return true;
}

/* ============================================================================
 * 权限操作
 * ============================================================================ */

bool XFileSystem_setPermissions(const XString* path, XFilePermissions permissions)
{
    if (!path) return false;
    
    wchar_t* wpath = XStringToWidePath(path);
    if (!wpath) return false;
    
    DWORD attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        XFree_System(wpath);
        return false;
    }
    
    if (!(permissions & (XFile_WriteOwner | XFile_WriteUser | XFile_WriteGroup | XFile_WriteOther))) {
        attrs |= FILE_ATTRIBUTE_READONLY;
    } else {
        attrs &= ~FILE_ATTRIBUTE_READONLY;
    }
    
    BOOL result = SetFileAttributesW(wpath, attrs);
    XFree_System(wpath);
    
    return result != 0;
}

/* ============================================================================
 * 内存映射
 * ============================================================================ */

void* XFileSystem_map(XFd fd, int64_t offset, int64_t size, int flags)
{
    if (fd < 0 || size <= 0) return NULL;
    (void)flags;  /* Win32 端 MapPrivateOption 等价于 FILE_MAP_COPY */
    
    HANDLE hFile = XW32_getFile(fd);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;
    
    DWORD protect = (flags & 0x2) ? PAGE_READWRITE : PAGE_READONLY;
    DWORD sizeHigh = (DWORD)(size >> 32);
    DWORD sizeLow = (DWORD)(size & 0xFFFFFFFF);
    
    HANDLE hMap = CreateFileMappingW(hFile, NULL, protect, sizeHigh, sizeLow, NULL);
    if (!hMap) return NULL;
    
    DWORD access = (flags & 0x2) ? FILE_MAP_COPY : FILE_MAP_READ;
    DWORD offsetHigh = (DWORD)(offset >> 32);
    DWORD offsetLow = (DWORD)(offset & 0xFFFFFFFF);
    
    void* address = MapViewOfFile(hMap, access, offsetHigh, offsetLow, (SIZE_T)size);
    CloseHandle(hMap);
    
    return address;
}

bool XFileSystem_unmap(void* addr, int64_t size)
{
    (void)size;
    if (!addr) return false;
    return UnmapViewOfFile(addr) != 0;
}

/**
 * @brief 通过文件描述符设置文件时间
 * @param fd 文件描述符（XFileDescriptor 表索引）
 * @param timeType 时间类型（访问时间/修改时间/创建时间）
 * @param newDate 新的 XDateTime 时间值（使用 XinYueC 自己的日期时间类型，不再使用 C time API）
 * @return 成功返回true
 * @note 直接操作已打开的 HANDLE，调用 SetFileTime。
 *       路径版需求由上层通过 open→setFileTime→close 组合实现。
 */
bool XFileSystem_setFileTime(XFd fd, XFileTime timeType, int64_t timeValue)
{
    HANDLE h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    ULARGE_INTEGER ul;
    ul.QuadPart = (uint64_t)timeValue * 10000000ULL + 116444736000000000ULL;

    FILETIME ft;
    ft.dwLowDateTime = ul.LowPart;
    ft.dwHighDateTime = ul.HighPart;

    FILETIME* ftCreate = NULL;
    FILETIME* ftAccess = NULL;
    FILETIME* ftWrite = NULL;

    switch (timeType) {
        case XFile_AccessTime: ftAccess = &ft; break;
        case XFile_BirthTime: ftCreate = &ft; break;
        case XFile_MetadataChangeTime:
        case XFile_ModificationTime: ftWrite = &ft; break;
    }

    return SetFileTime(h, ftCreate, ftAccess, ftWrite) != 0;
}

/* ============================================================================
 * 驱动器列表
 * ============================================================================ */

bool XFileSystem_enumerateDrives(XFileSystemDriveCallback callback, void* userData)
{
    if (!callback) return false;
    
    DWORD driveMask = GetLogicalDrives();
    
    for (int i = 0; i < 26; i++) {
        if (driveMask & (1u << i)) {
            char drivePath[4];
            drivePath[0] = 'A' + i;
            drivePath[1] = ':';
            drivePath[2] = '\\';
            drivePath[3] = '\0';
            XString* path = XString_create_utf8(drivePath);
            if (!path) return false;
            bool cont = callback(path, userData);
            XString_delete_base(path);
            if (!cont) return false;
        }
    }
    
    return true;
}

/* ============================================================================
 * 存储设备信息
 * ============================================================================ */

bool XFileSystem_getStorageInfo(const XString* path, XStorageInfoData* info)
{
    if (!path || !info) return false;
    
    const char* utf8Path = XStringToUtf8(path);
    if (!utf8Path) return false;
    
    wchar_t* wpath = XStringToWidePath(path);
    if (!wpath) return false;
    
    /* 获取磁盘空间信息 */
    ULARGE_INTEGER freeBytesAvailable, totalBytes, freeBytes;
    if (!GetDiskFreeSpaceExW(wpath, &freeBytesAvailable, &totalBytes, &freeBytes)) {
        XFree_System(wpath);
        return false;
    }
    
    info->bytesTotal = (int64_t)totalBytes.QuadPart;
    info->bytesFree = (int64_t)freeBytes.QuadPart;
    info->bytesAvailable = (int64_t)freeBytesAvailable.QuadPart;
    
    /* 获取簇大小 */
    DWORD sectorsPerCluster, bytesPerSector, numberOfFreeClusters, totalNumberOfClusters;
    if (GetDiskFreeSpaceW(wpath, &sectorsPerCluster, &bytesPerSector, &numberOfFreeClusters, &totalNumberOfClusters)) {
        info->blockSize = sectorsPerCluster * bytesPerSector;
    } else {
        info->blockSize = 4096; /* 默认值 */
    }
    
    /* 获取卷信息 */
    wchar_t volumeName[MAX_PATH + 1];
    wchar_t fileSystemName[MAX_PATH + 1];
    DWORD volumeSerialNumber, maximumComponentLength, fileSystemFlags;
    
    if (GetVolumeInformationW(wpath, volumeName, MAX_PATH, &volumeSerialNumber,
                               &maximumComponentLength, &fileSystemFlags, fileSystemName, MAX_PATH)) {
        /* 设置卷标名称 */
        if (info->volumeName) {
            char utf8VolumeName[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, volumeName, -1, utf8VolumeName, MAX_PATH, NULL, NULL);
            XString_assign_utf8(info->volumeName, utf8VolumeName);
        }
        
        /* 设置文件系统类型 */
        if (info->fileSystemType) {
            char utf8FsType[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, fileSystemName, -1, utf8FsType, MAX_PATH, NULL, NULL);
            XString_assign_utf8(info->fileSystemType, utf8FsType);
        }
        
        info->isReadOnly = (fileSystemFlags & FILE_READ_ONLY_VOLUME) != 0;
    } else {
        if (info->volumeName) XString_assign_utf8(info->volumeName, "");
        if (info->fileSystemType) XString_assign_utf8(info->fileSystemType, "");
        info->isReadOnly = false;
    }
    
    /* 获取设备路径（卷GUID） */
    if (info->device) {
        HANDLE hFile = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            wchar_t volumePath[MAX_PATH];
            DWORD len = GetFinalPathNameByHandleW(hFile, volumePath, MAX_PATH, VOLUME_NAME_GUID);
            CloseHandle(hFile);
            
            if (len > 0 && len < MAX_PATH) {
                char utf8Device[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, volumePath, -1, utf8Device, MAX_PATH, NULL, NULL);
                XString_assign_utf8(info->device, utf8Device);
            } else {
                XString_assign_utf8(info->device, "");
            }
        } else {
            XString_assign_utf8(info->device, "");
        }
    }
    
    /* 子卷名称（Windows不支持） */
    if (info->subvolume) {
        XString_assign_utf8(info->subvolume, "");
    }
    
    info->isValid = true;
    info->isReady = true;
    info->isRemovable = false; /* 需要额外检测 */
    
    XFree_System(wpath);
    return true;
}

/* ============================================================================
 * 磁盘格式化
 * ============================================================================ */

/**
 * @brief 格式化磁盘（Windows实现）
 * 
 * Windows下有几种格式化方式：
 * 1. SHFormatDrive - 显示系统格式化对话框
 * 2. FormatEx (fmifs.dll) - 命令行格式化
 * 3. CreateFile + FSCTL_SET_BOOTSECTOR - 低级格式化
 * 
 * 这里使用SHFormatDrive，因为它最安全且由系统处理
 */
bool XFileSystem_format(const XString* drive, 
                        XFileSystemType fsType,
                        const XString* volumeName,
                        int flags,
                        int clusterSize,
                        XFileSystemFormatProgress progress,
                        void* userData)
{
    if (!drive) return false;
    
    const char* utf8Drive = XStringToUtf8(drive);
    if (!utf8Drive) return false;
    
    /* 获取驱动器号 */
    char driveLetter = 0;
    if (strlen(utf8Drive) >= 2 && utf8Drive[1] == ':') {
        driveLetter = toupper(utf8Drive[0]);
    }
    if (driveLetter < 'A' || driveLetter > 'Z') {
        return false;
    }
    
    /* 使用SHFormatDrive显示格式化对话框 */
    /* 注意：这需要COM初始化和用户交互 */
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }
    
    /* SHFormatDrive参数 */
    /* SHFMT_ID == 0xFFFF是当前版本 */
    /* SHFMT_OPT_FULL = 0, SHFMT_OPT_QUICK = 1 */
    int fmtOptions = (flags & XFileSystemFormat_Quick) ? 1 : 0;
    
    /* 调用系统格式化对话框 */
    DWORD result = SHFormatDrive(NULL, driveLetter - 'A', 0xFFFF, fmtOptions);
    
    CoUninitialize();
    
    /* 结果检查 */
    /* SHFMT_OK = 0, SHFMT_ERROR = ... */
    if (result == 0) {
        /* 如果指定了卷标名称，需要设置 */
        if (volumeName) {
            wchar_t* wdrive = XStringToWidePath(drive);
            wchar_t* wlabel = XStringToWidePath(volumeName);
            if (wdrive && wlabel) {
                SetVolumeLabelW(wdrive, wlabel);
            }
            if (wdrive) XFree_System(wdrive);
            if (wlabel) XFree_System(wlabel);
        }
        return true;
    }
    
    return false;
}

#endif /* XFILE_USE_PLATFORM_API */

#endif /* _WIN32 */
