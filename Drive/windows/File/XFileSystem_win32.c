#ifdef _WIN32

#include "XFileSystem_config.h"  /* 文件系统配置 */

/* 仅在启用平台API模式时编译此文件 */
#if defined(XFILE_USE_PLATFORM_API)

#include "XFileSystem.h"
#include "XFileDevice.h"
#include "XMemory.h"
#include "XString.h"
#include "XFileDescriptor.h"  /* XFd_alloc, XFd_free, XFd_handle, XFd_type */
#include "XAbstractNetIoRing.h"
#include "XObject.h"          /* 嵌入式 XObject 基类（事件通知接收者） */
#include "XClass.h"           /* XClass_deinit_base（共享内存段释放） */
#include "XCoreApplication.h" /* 移除已投递事件（关闭共享内存段） */
#include "XDateTime.h"        /* 有界等待截止时间 */
#include <winsock2.h>          /* FD_READ/FD_WRITE 事件掩码 */
#include "XNetIoRingWin32.h"  /* IOCP 事件上下文与异步读辅助 */
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>
#include <shellapi.h>
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

/* 共享内存段的平台私有句柄（挂在 XFileDescriptor.ctx 上）。
   段数据由 mapHandle（CreateFileMapping/OpenFileMapping）承载，信令
   通道由 signalPipe（\\.\pipe\<name>.sig 命名管道）承载，
   XFileSystem_map/unmap 使用 mapHandle。
   信令通道的异步接收完全接入库内部事件通知系统（XNetIoRingWin32
   全局 IOCP 完成端口，与网络套接字/串口的异步读一致）：
   openSharedMemory 建立信令管道后即关联 IOCP 并提交常驻异步
   OVERLAPPED ReadFile，完成事件由 waitForEvents（GetQueuedCompletionStatus）
   处理并写入 m_readResult；XFileSystem_read 消费异步接收缓冲，无数据时
   通过事件环等待完成通知，不做任何轮询。
   首成员为嵌入式 XObject（事件环分发 CQ 条目时以 desc->ctx 作为
   事件接收者），关闭时通过 XClass_deinit_base 完整释放。 */
typedef struct XFileSharedMemoryWin32 {
    XObject m_object;            /**< 嵌入式 XObject 基类（事件通知接收者，必须为首成员） */
    XEventContext_IOCP m_read;   /**< 信令通道异步读事件上下文（IOCP OVERLAPPED ReadFile） */
    uint8_t m_readBuffer[16];    /**< 异步读缓冲区（存放到达的信令字节） */
    size_t m_rxCount;            /**< 异步接收缓冲内未消费字节数 */
    size_t m_rxOffset;           /**< 异步接收缓冲已消费偏移 */
    int64_t m_readResult;        /**< 异步读完成结果（完成回调写入，>=0 字节数，<0 通道错误） */
    bool m_readPending;          /**< 是否有在途异步读（OVERLAPPED ReadFile 已提交未完成） */
    bool m_iocpAssociated;       /**< 信令管道是否已关联到当前全局 IOCP */
    HANDLE mapHandle;            /**< 命名内存映射句柄（CreateFileMapping/OpenFileMapping） */
    HANDLE signalPipe;           /**< 信令管道句柄（\\.\pipe\<name>.sig） */
    bool created;                /**< 是否为创建方（创建方负责断开管道实例） */
} XFileSharedMemoryWin32;

/* 共享内存信令通道异步接收辅助函数（实现在"内存映射"小节）。
   XFileSystem_read / XFileSystem_close 在文件前部使用，需要前置声明。 */
static int64_t xfs_win32_shmRead(XFd fdx, XFileSharedMemoryWin32* m,
                                 void* buf, int64_t len);
static void xfs_win32_shm_cancelRead(XFileSharedMemoryWin32* m);

typedef struct XWin32ConsoleInputState {
    uint64_t magic;
    XFd fd;
    HANDLE handle;
    HANDLE thread;
    HANDLE readPermit;
    SRWLOCK lock;
    char* data;
    size_t size;
    size_t capacity;
    DWORD error;
    bool eof;
    bool stopping;
    bool readRequested;
    bool readInProgress;
} XWin32ConsoleInputState;

#define XWIN32_CONSOLE_INPUT_MAGIC UINT64_C(0x5843494E50555431)

static XWin32ConsoleInputState* XFileSystem_consoleInputState(XFd fd)
{
    XFileDescriptor* descriptor = XFd_get(fd);
    XWin32ConsoleInputState* state;
    if (!descriptor || descriptor->type != XFD_TYPE_CONSOLE)
        return NULL;
    state = (XWin32ConsoleInputState*)descriptor->handle;
    if (!state || state->magic != XWIN32_CONSOLE_INPUT_MAGIC)
        return NULL;
    return state;
}

static void XFileSystem_notifyConsoleInput(XWin32ConsoleInputState* state)
{
#if XAbstractNetIoRing_ON
    XAbstractNetIoRing* ring;
    XAbstractNetIoRing_CQEntry entry;
    if (!state || state->fd == XFD_INVALID) return;
    ring = XAbstractNetIoRing_global();
    if (!ring || !XAbstractNetIoRing_isEnabled(ring)) return;
    memset(&entry, 0, sizeof(entry));
    entry.m_fd = state->fd;
    entry.m_events = XSocketAct_Read;
    entry.m_sourceType = XAbstractNetIoRing_Source_Custom;
    entry.m_fdType = XFD_TYPE_CONSOLE;
    if (XAbstractNetIoRing_pushCompletion(ring, &entry))
        XAbstractNetIoRing_wakeUp_base(ring);
#else
    (void)state;
#endif
}

static DWORD WINAPI XFileSystem_consoleInputThread(LPVOID parameter)
{
    XWin32ConsoleInputState* state = (XWin32ConsoleInputState*)parameter;
    char input[256];

    if (!state) return ERROR_INVALID_PARAMETER;
    for (;;) {
        DWORD bytesRead = 0;
        DWORD waitResult = WaitForSingleObject(state->readPermit, INFINITE);
        if (waitResult != WAIT_OBJECT_0) return GetLastError();

        AcquireSRWLockExclusive(&state->lock);
        bool stopping = state->stopping;
        state->readRequested = false;
        if (!stopping) state->readInProgress = true;
        ReleaseSRWLockExclusive(&state->lock);
        if (stopping) break;

        if (!ReadConsoleA(state->handle, input, (DWORD)sizeof(input),
                          &bytesRead, NULL)) {
            DWORD error = GetLastError();
            AcquireSRWLockExclusive(&state->lock);
            state->readInProgress = false;
            if (!state->stopping)
                state->error = error ? error : ERROR_READ_FAULT;
            ReleaseSRWLockExclusive(&state->lock);
            XFileSystem_notifyConsoleInput(state);
            break;
        }

        AcquireSRWLockExclusive(&state->lock);
        state->readInProgress = false;
        if (state->stopping) {
            ReleaseSRWLockExclusive(&state->lock);
            break;
        }
        if (bytesRead == 0) {
            state->eof = true;
            ReleaseSRWLockExclusive(&state->lock);
            XFileSystem_notifyConsoleInput(state);
            break;
        }
        if ((size_t)bytesRead > SIZE_MAX - state->size) {
            state->error = ERROR_NOT_ENOUGH_MEMORY;
            ReleaseSRWLockExclusive(&state->lock);
            XFileSystem_notifyConsoleInput(state);
            break;
        }
        {
            size_t required = state->size + (size_t)bytesRead;
            if (required > state->capacity) {
                size_t capacity = state->capacity ? state->capacity : 512u;
                char* data;
                while (capacity < required) {
                    if (capacity > SIZE_MAX / 2u) {
                        capacity = required;
                        break;
                    }
                    capacity *= 2u;
                }
                data = (char*)XRealloc_System(state->data, capacity);
                if (!data) {
                    state->error = ERROR_NOT_ENOUGH_MEMORY;
                    ReleaseSRWLockExclusive(&state->lock);
                    XFileSystem_notifyConsoleInput(state);
                    break;
                }
                state->data = data;
                state->capacity = capacity;
            }
            memcpy(state->data + state->size, input, (size_t)bytesRead);
            state->size = required;
        }
        ReleaseSRWLockExclusive(&state->lock);
        XFileSystem_notifyConsoleInput(state);

        /* Wait until the event thread has consumed and processed this line.
         * This lets password/login handlers change console echo before the
         * next ReadConsole call begins. */
    }
    return ERROR_SUCCESS;
}

static void XFileSystem_destroyConsoleInputState(XWin32ConsoleInputState* state)
{
    if (!state) return;
    AcquireSRWLockExclusive(&state->lock);
    state->stopping = true;
    ReleaseSRWLockExclusive(&state->lock);
    if (state->readPermit) SetEvent(state->readPermit);
    if (state->thread) {
        (void)CancelSynchronousIo(state->thread);
        if (state->handle && state->handle != INVALID_HANDLE_VALUE)
            (void)CancelIoEx(state->handle, NULL);
        (void)WaitForSingleObject(state->thread, INFINITE);
        CloseHandle(state->thread);
    }
    if (state->readPermit) CloseHandle(state->readPermit);
    if (state->handle && state->handle != INVALID_HANDLE_VALUE)
        CloseHandle(state->handle);
    if (state->data) XFree_System(state->data);
    state->magic = 0;
    XFree_System(state);
}

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
 * @brief 通过 XFd 获取底层 Windows HANDLE
 */
static HANDLE XW32_getFile(XFd fd)
{
    XFileDescriptor* descriptor = XFd_get(fd);
    if (!descriptor) return INVALID_HANDLE_VALUE;
    if (descriptor->type == XFD_TYPE_CONSOLE) {
        XWin32ConsoleInputState* state = XFileSystem_consoleInputState(fd);
        return state ? state->handle : INVALID_HANDLE_VALUE;
    }
    if (descriptor->type != XFD_TYPE_FILE)
        return INVALID_HANDLE_VALUE;
    if (!descriptor->handle || descriptor->handle == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    return (HANDLE)descriptor->handle;
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
    XFd fd;
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
    if (type == FILE_TYPE_CHAR) {
        DWORD mode;
        if (GetConsoleMode(duplicate, &mode)) {
            XWin32ConsoleInputState* state =
                (XWin32ConsoleInputState*)XCalloc_System(1, sizeof(*state));
            if (!state) {
                CloseHandle(duplicate);
                if (error) *error = XFileDevice_ResourceError;
                return XFD_INVALID;
            }
            state->magic = XWIN32_CONSOLE_INPUT_MAGIC;
            state->fd = XFD_INVALID;
            state->handle = duplicate;
            InitializeSRWLock(&state->lock);
            state->readPermit = CreateEventW(NULL, FALSE, FALSE, NULL);
            if (!state->readPermit) {
                XFileSystem_destroyConsoleInputState(state);
                if (error) *error = XFileDevice_ResourceError;
                return XFD_INVALID;
            }
            fd = XFd_alloc(XFD_TYPE_CONSOLE, state, NULL);
            if (fd == XFD_INVALID) {
                XFileSystem_destroyConsoleInputState(state);
                if (error) *error = XFileDevice_ResourceError;
                return XFD_INVALID;
            }
            state->fd = fd;
            state->thread = CreateThread(NULL, 0, XFileSystem_consoleInputThread,
                                         state, 0, NULL);
            if (!state->thread) {
                XFd_setCtx(fd, NULL);
                XFileSystem_destroyConsoleInputState(state);
                XFd_free(fd);
                if (error) *error = XFileDevice_ResourceError;
                return XFD_INVALID;
            }
            if (error) *error = XFileDevice_NoError;
            return fd;
        }
    }
    fd = XFd_alloc(XFD_TYPE_FILE, duplicate, NULL);
    if (fd == XFD_INVALID) {
        CloseHandle(duplicate);
        if (error) *error = XFileDevice_ResourceError;
        return XFD_INVALID;
    }
    if (error) *error = XFileDevice_NoError;
    return fd;
}

void XFileSystem_close(XFd fd)
{
    XFileDescriptor* descriptor = XFd_get(fd);
    XWin32ConsoleInputState* state = XFileSystem_consoleInputState(fd);
    if (state) {
        XFd_setCtx(fd, NULL);
        XFileSystem_destroyConsoleInputState(state);
    } else if (descriptor && descriptor->type == XFD_TYPE_MAPPING) {
        /* 共享内存段：先取消并回收在途异步读（避免悬垂 OVERLAPPED 完成包
           指向已释放的读上下文），再释放信令管道与映射句柄，创建方断开
           管道实例，最后移除事件环可能已投递到本对象的未处理事件并释放
           嵌入式 XObject 基类。 */
        XFileSharedMemoryWin32* mapping = (XFileSharedMemoryWin32*)descriptor->ctx;
        if (mapping) {
            xfs_win32_shm_cancelRead(mapping);
            if (mapping->signalPipe && mapping->signalPipe != INVALID_HANDLE_VALUE) {
                if (mapping->created) DisconnectNamedPipe(mapping->signalPipe);
                CloseHandle(mapping->signalPipe);
            }
            if (mapping->mapHandle && mapping->mapHandle != INVALID_HANDLE_VALUE)
                CloseHandle(mapping->mapHandle);
            XCoreApplication_removePostedEvents((XObject*)&mapping->m_object, 0);
            XClass_deinit_base((XClass*)&mapping->m_object);
            XFree_System(mapping);
        }
    } else {
        HANDLE h = XW32_getFile(fd);
        if (h != INVALID_HANDLE_VALUE)
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
    XFileDescriptor* descriptor = XFd_get(fd);
    HANDLE h;
    if (!descriptor || !buf || len <= 0) return -1;

    /* 共享内存段：句柄是信令管道，走 IOCP 异步接收 + 库内部事件通知
       （xfs_win32_shmRead：消费异步缓冲，无数据时事件环等待完成，不轮询）。 */
    if (descriptor->type == XFD_TYPE_MAPPING)
        return xfs_win32_shmRead(fd, (XFileSharedMemoryWin32*)descriptor->ctx,
                                 buf, len);

    h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;

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
        XWin32ConsoleInputState* state = XFileSystem_consoleInputState(fd);
        size_t count;
        if (!state) return -2;
        AcquireSRWLockExclusive(&state->lock);
        if (state->size == 0) {
            DWORD stateError = state->error;
            bool eof = state->eof;
            bool requestRead = !stateError && !eof && !state->stopping &&
                               !state->readRequested && !state->readInProgress;
            if (requestRead) state->readRequested = true;
            ReleaseSRWLockExclusive(&state->lock);
            if (requestRead && !SetEvent(state->readPermit)) return -2;
            if (stateError) return -2;
            return eof ? -1 : 0;
        }
        count = (size_t)len < state->size ? (size_t)len : state->size;
        memcpy(buf, state->data, count);
        state->size -= count;
        if (state->size)
            memmove(state->data, state->data + count, state->size);
        ReleaseSRWLockExclusive(&state->lock);
        return (int64_t)count;
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
    XFileDescriptor* descriptor = XFd_get(fd);
    HANDLE h;
    if (!descriptor || !buf || len <= 0) return -1;

    /* 共享内存段：句柄是信令管道，WriteFile 发送信令字节。
       写方向保持同步等待完成（与 POSIX 一致：信令字节极小）。 */
    if (descriptor->type == XFD_TYPE_MAPPING) {
        XFileSharedMemoryWin32* mapping = (XFileSharedMemoryWin32*)descriptor->ctx;
        if (!mapping || !mapping->signalPipe
            || mapping->signalPipe == INVALID_HANDLE_VALUE)
            return -1;
        /* 共享内存信令管道使用 FILE_FLAG_OVERLAPPED 以支持异步读，因此
           不能再给 WriteFile 传 NULL OVERLAPPED。写操作仍在本调用内等待
           完成，并通过事件句柄低位禁止把内部写完成投递到 IOCP。 */
        {
            int64_t totalWritten = 0;
            while (totalWritten < len) {
                OVERLAPPED overlapped;
                HANDLE eventHandle;
                DWORD bytesWritten = 0;
                DWORD toWrite = (DWORD)((uint64_t)(len - totalWritten) > UINT32_MAX
                                        ? UINT32_MAX
                                        : (uint64_t)(len - totalWritten));
                BOOL ok;

                eventHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
                if (!eventHandle) return -1;
                memset(&overlapped, 0, sizeof(overlapped));
                /* 防止该内部写请求在已关联的 IOCP 上产生无法识别的完成包。 */
                overlapped.hEvent = (HANDLE)((ULONG_PTR)eventHandle | 1u);
                ok = WriteFile(mapping->signalPipe,
                               (const char*)buf + totalWritten, toWrite,
                               &bytesWritten, &overlapped);
                if (!ok) {
                    DWORD lastError = GetLastError();
                    if (lastError != ERROR_IO_PENDING
                        || WaitForSingleObject(eventHandle, INFINITE) != WAIT_OBJECT_0
                        || !GetOverlappedResult(mapping->signalPipe, &overlapped,
                                                &bytesWritten, FALSE)) {
                        CloseHandle(eventHandle);
                        return -1;
                    }
                }
                CloseHandle(eventHandle);
                if (bytesWritten == 0) break;
                totalWritten += bytesWritten;
            }
            return totalWritten;
        }
    }

    h = XW32_getFile(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    
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
 * 内存映射（3个）- 可选
 * ============================================================================
 *
 * 共享内存段在 Windows 上由两块组成：
 *   1. 命名内存映射（CreateFileMapping/OpenFileMapping）：存放跨进程数据，
 *      通过 XFileSystem_map 建立视图；
 *   2. 命名管道信令通道（\\.\pipe\<共享内存名>.sig）：数据方写完一块数据后
 *      向管道写入 1 个信令字节，对端通过 IOCP + 库内部事件通知异步接收
 *      （OVERLAPPED ReadFile 关联全局完成端口，与网络套接字/串口的异步
 *      读一致），无需轮询共享内存状态字段。
 *      该通道内置于平台实现，不新增任何公共 API。
 *
 * XFd 句柄为信令管道句柄，XFileSystem_read / XFileSystem_write 直接在
 * 信令通道上收发；ctx 保存映射句柄，XFileSystem_map / XFileSystem_close
 * 据此完成视图与释放。
 */

/* 将共享内存段名称整理为合法的信令管道名：
   仅保留字母数字与 ._-，其余字符替换为 '_'，避免段名中的目录分隔符破坏
   管道路径。失败（名称超长等）返回 false。 */
static bool xfs_win32_makeSignalPipeName(const char* name, wchar_t* out, size_t outLen)
{
    static const wchar_t prefix[] = L"\\\\.\\pipe\\";
    size_t i, n, used;
    if (!name || !out || outLen == 0) return false;
    n = strlen(name);
    if (n > 200) return false; /* 管道名长度限制，预留前缀与后缀 */
    used = 0;
    while (prefix[used] != L'\0' && used + 1 < outLen) {
        out[used] = prefix[used];
        ++used;
    }
    if (prefix[used] != L'\0') return false;
    for (i = 0; i < n && used + 1 < outLen - 4; ++i) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
            out[used++] = (wchar_t)c;
        else
            out[used++] = L'_';
    }
    {
        static const wchar_t suffix[] = L".sig";
        size_t k = 0;
        while (suffix[k] != L'\0' && used + 1 < outLen) {
            out[used++] = suffix[k];
            ++k;
        }
        if (suffix[k] != L'\0') return false;
    }
    out[used] = L'\0';
    return true;
}

/* 信令通道异步读的有界等待片（毫秒）：XFileSystem_read 在无信令字节时
   通过事件环的 waitForEvents 最多阻塞该时长后返回 0，传输层据此检查
   整体超时；信令到达时事件环立即唤醒，不存在任何轮询。 */
#define XFILE_SHM_SIGNAL_RCVTIMEO_MS 500

/* 关闭时取消异步读后、等待完成事件回收的最多轮询片数
   （每片 XFILE_SHM_SIGNAL_RCVTIMEO_MS，用于防御平台异常）。 */
#define XFS_SHM_CANCEL_RECLAIM_TRIES 50

/* ============================================================================
 * 信令通道异步接收（IOCP + 库内部事件通知）
 *
 * 读取路径与网络套接字/串口完全一致：openSharedMemory 建立信令管道后
 * 即把管道句柄关联到 XAbstractNetIoRing 全局 IOCP 完成端口，并提交常驻
 * 异步 OVERLAPPED ReadFile；完成事件由事件环的 waitForEvents
 * （GetQueuedCompletionStatus）出队，先经完成回调把结果写入 m_readResult
 * 并清除在途标记；XFileSystem_read 消费 m_readBuffer 中的字节，无数据时
 * 通过事件环有界等待完成通知（超时片返回 0 供传输层检查整体截止时间），
 * 整个过程不轮询共享内存状态字段。
 * ============================================================================ */

/* IOCP 完成回调：waitForEvents 从完成端口出队本读完成包后最先调用，
   context->finishedBytes 已由事件环写入实际传输字节数。本函数把结果
   记录到 m_readResult 并清除在途标记。返回 false 表示这是内部接收，
   不再把 CQ 条目分发给应用层对象。 */
static bool xfs_win32_shmReadCompletion(XEventContext_IOCP* context, void* userData)
{
    XFileSharedMemoryWin32* m = (XFileSharedMemoryWin32*)userData;
    if (!m) return false;
    m->m_readResult = (int64_t)context->finishedBytes;
    m->m_readPending = false;
    /* 这是 XFileSystem_read 的内部接收，不应再生成一个应用层 socket
       事件；调用方会在 waitForEvents 返回后直接消费 m_readBuffer。 */
    return false;
}

/* 确保信令管道已经绑定到当前全局 IOCP。共享内存可能在事件环创建前
   打开，因此不能只在 openSharedMemory 中尝试一次绑定。 */
static bool xfs_win32_shm_associate(XFileSharedMemoryWin32* m)
{
    XAbstractNetIoRing* ring;

    if (!m || !m->signalPipe || m->signalPipe == INVALID_HANDLE_VALUE)
        return false;
    if (m->m_iocpAssociated) return true;
    ring = XAbstractNetIoRing_global();
    if (!ring || !XAbstractNetIoRing_isEnabled(ring)) return false;
    if (!XNetIoRingWin32_assocHandle((XNetIoRingWin32*)ring, m->signalPipe,
                                     (ULONG_PTR)&m->m_object))
        return false;
    m->m_iocpAssociated = true;
    return true;
}

/* 提交信令通道异步读（OVERLAPPED ReadFile，提交到全局 IOCP 完成端口）。
   读入 mapping->m_readBuffer，完成事件由事件环处理并经完成回调写入
   m_readResult。成功返回 true；无事件环、句柄无效或提交失败返回 false。
   调用方保证当前无在途读。 */
static bool xfs_win32_shm_armRead(XFd fdx, XFileSharedMemoryWin32* m)
{
    XAbstractNetIoRing* ring;
    BOOL ok;

    if (!m || m->m_readPending || !m->signalPipe
        || m->signalPipe == INVALID_HANDLE_VALUE)
        return false;
    ring = XAbstractNetIoRing_global();
    if (!ring || !xfs_win32_shm_associate(m)) return false;

    memset(&m->m_read, 0, sizeof(m->m_read));
    m->m_read.base.type = XEventContextType_Type_File;
    m->m_read.base.fd = fdx;
    m->m_read.socket = XSocketDescriptor_fromIntptr((intptr_t)m->signalPipe);
    m->m_read.eventMask = FD_READ;
    m->m_read.buffer = m->m_readBuffer;
    m->m_read.bufferSize = sizeof(m->m_readBuffer);
    m->m_read.completionCallback = xfs_win32_shmReadCompletion;
    m->m_read.completionUserData = m;

    m->m_readPending = true;
    ok = ReadFile(m->signalPipe, m->m_readBuffer,
                  (DWORD)sizeof(m->m_readBuffer), NULL,
                  &m->m_read.base.overlapped);
    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        m->m_readPending = false;
        return false;
    }
    return true;
}

/* 通过库内部事件环等待信令通道异步读完成（有界等待）：
   先检查完成回调是否已处理本读（事件环可能已批处理完成事件）；若仍在
   途，则以 XFILE_SHM_SIGNAL_RCVTIMEO_MS 为总截止时间循环调用事件环
   waitForEvents（GetQueuedCompletionStatus，信令到达立即唤醒）。
   注意 waitForEvents 可能因其他事件源（唤醒包、其他上下文完成）提前
   返回，本函数只认本读上下文 m_readPending 标记的变化，不把无关唤醒
   误判为超时；总等待片到点仍未完成才返回 false（由调用方按整体截止
   时间处理）。返回 true 表示本读已完成。 */
static bool xfs_win32_shm_waitReadable(XFileSharedMemoryWin32* m)
{
    XAbstractNetIoRing* ring;
    int64_t deadline;
    int64_t now;

    if (!m) return false;
    if (!m->m_readPending)
        return true; /* 完成回调已处理本读（事件环已批处理） */
    ring = XAbstractNetIoRing_global();
    if (!ring) return false;

    /* 以本等待片为总截止时间，循环等待本读的完成事件；
       waitForEvents 每次按剩余时间有界等待，避免无关唤醒导致忙转。 */
    deadline = XDateTime_currentMSecsSinceEpoch() + XFILE_SHM_SIGNAL_RCVTIMEO_MS;
    for (;;) {
        int waitMs;
        now = XDateTime_currentMSecsSinceEpoch();
        if (now >= deadline) break;
        waitMs = (int)(deadline - now);
        if (waitMs <= 0) break;
        XAbstractNetIoRing_waitForEvents_base(ring, waitMs);
        if (!m->m_readPending)
            return true;
    }
    return false;
}

/* 无事件环退化路径的同步阻塞读：对端信令字节到达前在内核等待。
   使用本函数私有的 OVERLAPPED + 事件句柄完成同步等待；无事件环时
   句柄未关联完成端口，不会产生重复完成包。 */
static int64_t xfs_win32_shm_blockingRead(XFileSharedMemoryWin32* m,
                                          void* buf, int64_t len)
{
    OVERLAPPED ov;
    HANDLE ev;
    DWORD bytesRead = 0;
    BOOL ok;

    if (!m || !m->signalPipe || m->signalPipe == INVALID_HANDLE_VALUE) return -1;
    ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ev) return -1;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = ev;
    ok = ReadFile(m->signalPipe, buf, (DWORD)len, NULL, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ev, INFINITE) != WAIT_OBJECT_0) {
            CloseHandle(ev);
            return -1;
        }
    }
    if (!GetOverlappedResult(m->signalPipe, &ov, &bytesRead, FALSE)) {
        CloseHandle(ev);
        return -1; /* 通道错误/对端关闭均视为不可用 */
    }
    CloseHandle(ev);
    return (int64_t)bytesRead;
}

/* 取消在途异步读并回收完成事件（XFileSystem_close 关闭前调用）：
   提交 CancelIoEx 后通过事件环有界等待该读的完成包出队（完成回调会
   清除 m_readPending 并写入结果），确保释放 mapping 前不存在悬垂
   OVERLAPPED 完成包指向已释放的读上下文（与 POSIX 的 ASYNC_CANCEL
   回收语义一致）。 */
static void xfs_win32_shm_cancelRead(XFileSharedMemoryWin32* m)
{
    XAbstractNetIoRing* ring;
    int tries = 0;

    if (!m || !m->m_readPending) return;
    ring = XAbstractNetIoRing_global();
    if (!ring) { m->m_readPending = false; return; }

    if (m->signalPipe && m->signalPipe != INVALID_HANDLE_VALUE)
        CancelIoEx(m->signalPipe, &m->m_read.base.overlapped);

    while (m->m_readPending && tries < XFS_SHM_CANCEL_RECLAIM_TRIES) {
        XAbstractNetIoRing_waitForEvents_base(ring, XFILE_SHM_SIGNAL_RCVTIMEO_MS);
        ++tries;
    }
    m->m_readPending = false;
    m->m_readResult = 0;
    m->m_rxCount = 0;
    m->m_rxOffset = 0;
}

/* XFileSystem_read 的共享内存信令通道实现：
   优先消费异步接收缓冲；无数据时通过库内部事件环等待完成通知，
   超时片返回 0（调用方检查整体截止时间），对端关闭/通道错误返回 -1。 */
static int64_t xfs_win32_shmRead(XFd fdx, XFileSharedMemoryWin32* m,
                                 void* buf, int64_t len)
{
    size_t n;

    if (!m || !m->signalPipe || m->signalPipe == INVALID_HANDLE_VALUE) return -1;
    if (len <= 0) return 0;

    /* 1. 优先消费异步接收缓冲中的字节；缓冲耗尽后立即重新武装下一次
          异步读，保证信令通道始终有常驻接收请求（与网络套接字一致）。 */
    if (m->m_rxCount > 0) {
        n = ((size_t)len < m->m_rxCount) ? (size_t)len : m->m_rxCount;
        memcpy(buf, m->m_readBuffer + m->m_rxOffset, n);
        m->m_rxOffset += n;
        m->m_rxCount -= n;
        if (m->m_rxCount == 0)
            (void)xfs_win32_shm_armRead(fdx, m);
        return (int64_t)n;
    }

    /* 2. 无事件环（未创建事件调度器）的退化路径：直接在内核阻塞读，
          不轮询、不设接收超时；正常使用（XCoreApplication 事件循环）
          下不会走到这里。 */
    {
        XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
        if (!ring || !XAbstractNetIoRing_isEnabled(ring))
            return xfs_win32_shm_blockingRead(m, buf, len);
    }

    /* 3. 确保在途异步读后，通过事件环等待完成通知（不忙轮询）。 */
    if (!m->m_readPending) {
        if (!xfs_win32_shm_armRead(fdx, m)) return -1;
    }
    if (!xfs_win32_shm_waitReadable(m))
        return 0; /* 超时片：由调用方检查整体截止时间 */

    {
        int64_t res = m->m_readResult;
        m->m_readResult = 0;
        if (res <= 0) {
            /* 0=对端关闭（EOF），负=通道错误：均视为通道不可用。 */
            m->m_rxCount = 0;
            m->m_rxOffset = 0;
            return -1;
        }
        m->m_rxCount = (size_t)res;
        m->m_rxOffset = 0;
        n = ((size_t)len < m->m_rxCount) ? (size_t)len : m->m_rxCount;
        memcpy(buf, m->m_readBuffer, n);
        m->m_rxOffset = n;
        m->m_rxCount -= n;
        if (m->m_rxCount == 0)
            (void)xfs_win32_shm_armRead(fdx, m);
        return (int64_t)n;
    }
}

XFd XFileSystem_openSharedMemory(const XString* name, bool create, int64_t maxSize, int* error)
{
    wchar_t* wname;
    wchar_t signalPipeName[MAX_PATH];
    const char* utf8Name;
    HANDLE hMap = NULL;
    HANDLE hPipe = NULL;
    XFileSharedMemoryWin32* mapping = NULL;
    XFd result = XFD_INVALID;
    DWORD lastError = 0;

    if (error) *error = 0;
    if (!name || (create && maxSize <= 0)) {
        if (error) *error = ERROR_INVALID_PARAMETER;
        return XFD_INVALID;
    }
    wname = XStringToWidePath(name);
    if (!wname) {
        if (error) *error = ERROR_INVALID_PARAMETER;
        return XFD_INVALID;
    }

    if (create) {
        /* 页文件背书的命名内存段，供 MySQL 共享内存等服务端创建、客户端按名打开 */
        DWORD sizeHigh = (DWORD)((uint64_t)maxSize >> 32);
        DWORD sizeLow = (DWORD)((uint64_t)maxSize & 0xFFFFFFFF);
        hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                  sizeHigh, sizeLow, wname);
        if (!hMap) {
            lastError = GetLastError();
            goto fail;
        }
    } else {
        hMap = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, wname);
        if (!hMap) {
            lastError = GetLastError();
            goto fail;
        }
    }

    /* 信令通道：命名管道。创建方建立管道实例并阻塞等待客户端连接，
       客户端等待管道可用后连接（与网络套接字 accept/connect 语义一致）。 */
    utf8Name = XString_toUtf8(name);
    if (!utf8Name
        || !xfs_win32_makeSignalPipeName(utf8Name, signalPipeName,
                                         sizeof(signalPipeName) / sizeof(wchar_t))) {
        lastError = ERROR_INVALID_PARAMETER;
        goto fail;
    }
    if (create) {
        hPipe = CreateNamedPipeW(signalPipeName,
                                 PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE
                                 | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                 1, 64, 64, 0, NULL);
        if (hPipe == INVALID_HANDLE_VALUE) {
            lastError = GetLastError();
            goto fail;
        }
        /* OVERLAPPED 句柄的 ConnectNamedPipe 必须以 OVERLAPPED 方式执行：
           提交后等待完成事件；客户端已先行连接（ERROR_PIPE_CONNECTED）
           同样视为成功（与网络套接字 accept 语义一致）。 */
        {
            OVERLAPPED ov;
            memset(&ov, 0, sizeof(ov));
            ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            if (!ov.hEvent) {
                lastError = GetLastError();
                goto fail;
            }
            if (!ConnectNamedPipe(hPipe, &ov)) {
                lastError = GetLastError();
                if (lastError == ERROR_PIPE_CONNECTED) {
                    lastError = 0; /* 客户端已连接 */
                } else if (lastError != ERROR_IO_PENDING) {
                    CloseHandle(ov.hEvent);
                    goto fail;
                } else {
                    DWORD bytes = 0;
                    if (WaitForSingleObject(ov.hEvent, INFINITE) != WAIT_OBJECT_0
                        || !GetOverlappedResult(hPipe, &ov, &bytes, FALSE)) {
                        lastError = GetLastError();
                        CloseHandle(ov.hEvent);
                        goto fail;
                    }
                    lastError = 0;
                }
            }
            CloseHandle(ov.hEvent);
        }
    } else {
        if (!WaitNamedPipeW(signalPipeName, 10000)) {
            lastError = GetLastError();
            goto fail;
        }
        hPipe = CreateFileW(signalPipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (hPipe == INVALID_HANDLE_VALUE) {
            lastError = GetLastError();
            goto fail;
        }
    }

    mapping = (XFileSharedMemoryWin32*)XCalloc_System(1, sizeof(XFileSharedMemoryWin32));
    if (!mapping) {
        lastError = ERROR_NOT_ENOUGH_MEMORY;
        goto fail;
    }
    XObject_init(&mapping->m_object); /* 嵌入式基类：事件通知接收者 */
    mapping->mapHandle = hMap;
    mapping->signalPipe = hPipe;
    mapping->created = create;

    result = XFd_alloc(XFD_TYPE_MAPPING, (void*)hPipe, mapping);
    if (result < 0) {
        lastError = ERROR_TOO_MANY_OPEN_FILES;
        goto fail;
    }

    /* 建立信令管道后立即关联 IOCP 完成端口并开启异步接收（库内部
       事件通知，与网络套接字/串口 open 后异步读语义一致）。 */
    {
        XNetIoRingWin32* ring = (XNetIoRingWin32*)XAbstractNetIoRing_global();
        if (ring && hPipe && hPipe != INVALID_HANDLE_VALUE) {
            if (XNetIoRingWin32_assocHandle(ring, hPipe,
                                            (ULONG_PTR)&mapping->m_object)) {
                mapping->m_iocpAssociated = true;
                (void)xfs_win32_shm_armRead(result, mapping);
            }
        }
    }
    XFree_System(wname);
    return result;

fail:
    if (hPipe && hPipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
    if (hMap) CloseHandle(hMap);
    if (mapping) {
        XClass_deinit_base((XClass*)&mapping->m_object);
        XFree_System(mapping);
    }
    XFree_System(wname);
    if (error) *error = (int)lastError;
    return XFD_INVALID;
}

void* XFileSystem_map(XFd fd, int64_t offset, int64_t size, int flags)
{
    XFileDescriptor* descriptor;
    HANDLE hMap;
    DWORD access;
    DWORD offsetHigh;
    DWORD offsetLow;
    if (fd < 0 || size <= 0) return NULL;
    descriptor = XFd_get(fd);
    if (!descriptor) return NULL;

    /* 共享内存段描述符：句柄是信令管道，映射句柄保存在 ctx。 */
    if (descriptor->type == XFD_TYPE_MAPPING) {
        XFileSharedMemoryWin32* mapping = (XFileSharedMemoryWin32*)descriptor->ctx;
        if (!mapping || !mapping->mapHandle || mapping->mapHandle == INVALID_HANDLE_VALUE)
            return NULL;
        hMap = mapping->mapHandle;
        access = (flags & 0x2) ? (FILE_MAP_READ | FILE_MAP_WRITE) : FILE_MAP_READ;
        offsetHigh = (DWORD)((uint64_t)offset >> 32);
        offsetLow = (DWORD)((uint64_t)offset & 0xFFFFFFFF);
        return MapViewOfFile(hMap, access, offsetHigh, offsetLow, (SIZE_T)size);
    }
    
    {
        HANDLE hFile = XW32_getFile(fd);
        if (hFile == INVALID_HANDLE_VALUE) return NULL;

        DWORD protect = (flags & 0x2) ? PAGE_READWRITE : PAGE_READONLY;
        DWORD sizeHigh = (DWORD)((uint64_t)size >> 32);
        DWORD sizeLow = (DWORD)((uint64_t)size & 0xFFFFFFFF);

        HANDLE hMap2 = CreateFileMappingW(hFile, NULL, protect, sizeHigh, sizeLow, NULL);
        if (!hMap2) return NULL;

        /* bit0=私有映射 → FILE_MAP_COPY（写时复制，不写回文件）；
           bit0=共享映射且可写 → FILE_MAP_WRITE（共享可写，对齐 POSIX MAP_SHARED） */
        if (flags & 0x1)
            access = (flags & 0x2) ? FILE_MAP_COPY : FILE_MAP_READ;
        else
            access = (flags & 0x2) ? FILE_MAP_WRITE : FILE_MAP_READ;
        offsetHigh = (DWORD)((uint64_t)offset >> 32);
        offsetLow = (DWORD)((uint64_t)offset & 0xFFFFFFFF);

        void* address = MapViewOfFile(hMap2, access, offsetHigh, offsetLow, (SIZE_T)size);
        CloseHandle(hMap2);

        return address;
    }
}

bool XFileSystem_unmap(void* addr, int64_t size)
{
    (void)size;
    if (!addr) return false;
    return UnmapViewOfFile(addr) != 0;
}

/**
 * @brief 通过文件描述符设置文件时间
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
