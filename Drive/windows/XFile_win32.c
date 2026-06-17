#ifdef _WIN32
#include "XFile.h"
#include "XMemory.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <shellapi.h>

/* ============================================================================
 * 辅助函数：将 XString 转换为 Windows 宽字符路径
 * ============================================================================ */

static wchar_t* XFile_toWidePath(const XString* path)
{
    if (!path) return NULL;
    
    // 获取 UTF-8 编码
    const char* utf8 = XString_toUtf8(path);
    if (!utf8) return NULL;
    
    // 获取所需缓冲区大小
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    
    // 分配缓冲区
    wchar_t* wpath = (wchar_t*)XMalloc_System(len * sizeof(wchar_t));
    if (!wpath) return NULL;
    
    // 转换
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wpath, len);
    return wpath;
}

/* ============================================================================
 * 虚函数实现
 * ============================================================================ */

bool XFile_open_impl(XFile* file, XIODeviceBaseMode mode)
{
    if (!file || !file->m_fileName) return false;
    
    // 如果已打开，先关闭
    if (file->m_parent.m_fileHandle >= 0) {
        XFile_close_base(file);
    }
    
    // 转换路径
    wchar_t* wpath = XFile_toWidePath(file->m_fileName);
    if (!wpath) {
        file->m_parent.m_error = XFileDevice_ResourceError;
        return false;
    }
    
    // 确定访问模式和创建标志
    DWORD desiredAccess = 0;
    DWORD shareMode = FILE_SHARE_READ;
    DWORD creationDisposition = OPEN_EXISTING;
    
    if (mode & XIODevice_ReadOnly) {
        desiredAccess |= GENERIC_READ;
    }
    if (mode & XIODevice_WriteOnly) {
        desiredAccess |= GENERIC_WRITE;
    }
    
    if (mode & XIODevice_Append) {
        desiredAccess |= GENERIC_WRITE;
        creationDisposition = OPEN_ALWAYS;
    } else if (mode & XIODevice_WriteOnly) {
        if (mode & XIODevice_Truncate) {
            creationDisposition = CREATE_ALWAYS;
        } else {
            creationDisposition = OPEN_ALWAYS;
        }
    } else if (mode & XIODevice_NewOnly) {
        creationDisposition = CREATE_NEW;
    } else if (mode & XIODevice_ExistingOnly) {
        creationDisposition = OPEN_EXISTING;
    }
    
    // 打开文件
    HANDLE hFile = CreateFileW(wpath, desiredAccess, shareMode, NULL,
                                creationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    XFree_System(wpath);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        file->m_parent.m_error = XFileDevice_OpenError;
        return false;
    }
    
    // 获取文件描述符
    int fd = _open_osfhandle((intptr_t)hFile, 0);
    if (fd < 0) {
        CloseHandle(hFile);
        file->m_parent.m_error = XFileDevice_OpenError;
        return false;
    }
    
    // 设置文件句柄
    file->m_parent.m_fileHandle = fd;
    file->m_parent.m_handleFlags = XFileDevice_AutoCloseHandle;
    file->m_parent.m_error = XFileDevice_NoError;
    
    // 设置打开模式
    file->m_parent.m_parent.m_openMode = mode;
    
    // 如果是追加模式，移动到文件末尾
    if (mode & XIODevice_Append) {
        _lseeki64(fd, 0, SEEK_END);
    }
    
    return true;
}

void XFile_close_impl(XFile* file)
{
    if (!file || file->m_parent.m_fileHandle < 0) return;
    
    // 发射 aboutToClose 信号
    XIODevice_aboutToClose_signal(&file->m_parent.m_parent);
    
    // 关闭文件句柄
    if (file->m_parent.m_handleFlags & XFileDevice_AutoCloseHandle) {
        _close(file->m_parent.m_fileHandle);
    }
    
    file->m_parent.m_fileHandle = -1;
    file->m_parent.m_parent.m_openMode = XIODevice_NotOpen;
    file->m_parent.m_cachedSize = -1;  // -1 表示无缓存
}

//int64_t XFile_size_impl(const XFile* file)
//{
//    if (!file || file->m_parent.m_fileHandle < 0) return 0;
//    
//    struct _stat64 st;
//    if (_fstat64(file->m_parent.m_fileHandle, &st) != 0) {
//        return 0;
//    }
//    
//    return st.st_size;
//}

bool XFile_resize_impl(XFile* file, int64_t sz)
{
    if (!file || file->m_parent.m_fileHandle < 0) {
        if (file) file->m_parent.m_error = XFileDevice_ResizeError;
        return false;
    }
    
    // 获取当前位置
    int64_t oldPos = _telli64(file->m_parent.m_fileHandle);
    
    // 移动到目标位置
    if (_lseeki64(file->m_parent.m_fileHandle, sz, SEEK_SET) < 0) {
        file->m_parent.m_error = XFileDevice_ResizeError;
        return false;
    }
    
    // 截断文件
    HANDLE hFile = (HANDLE)_get_osfhandle(file->m_parent.m_fileHandle);
    if (!SetEndOfFile(hFile)) {
        file->m_parent.m_error = XFileDevice_ResizeError;
        // 恢复位置
        _lseeki64(file->m_parent.m_fileHandle, oldPos, SEEK_SET);
        return false;
    }
    
    // 恢复位置
    _lseeki64(file->m_parent.m_fileHandle, oldPos, SEEK_SET);
    
    return true;
}

XFilePermissions XFile_permissions_impl(const XFile* file)
{
    if (!file || !file->m_fileName) return 0;
    
    wchar_t* wpath = XFile_toWidePath(file->m_fileName);
    if (!wpath) return 0;
    
    XFilePermissions perms = 0;
    
    // 检查读权限
    if (_waccess_s(wpath, 4) == 0) {
        perms |= XFile_ReadUser;
    }
    
    // 检查写权限
    if (_waccess_s(wpath, 2) == 0) {
        perms |= XFile_WriteUser;
    }
    
    // 检查执行权限（检查文件扩展名）
    const char* utf8 = XString_toUtf8(file->m_fileName);
    if (utf8) {
        const char* ext = strrchr(utf8, '.');
        if (ext && (_stricmp(ext, ".exe") == 0 || _stricmp(ext, ".bat") == 0 ||
                    _stricmp(ext, ".cmd") == 0 || _stricmp(ext, ".com") == 0)) {
            perms |= XFile_ExeUser;
        }
    }
    
    XFree_System(wpath);
    return perms;
}

bool XFile_setPermissions_impl(XFile* file, XFilePermissions permissions)
{
    if (!file || !file->m_fileName) return false;
    
    wchar_t* wpath = XFile_toWidePath(file->m_fileName);
    if (!wpath) return false;
    
    // Windows 只支持只读属性
    DWORD attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        XFree_System(wpath);
        return false;
    }
    
    // 如果没有任何写权限，设置只读属性
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
 * 静态函数实现
 * ============================================================================ */

bool XFile_exists_impl(const XString* fileName)
{
    if (!fileName) return false;
    
    wchar_t* wpath = XFile_toWidePath(fileName);
    if (!wpath) return false;
    
    DWORD attrs = GetFileAttributesW(wpath);
    XFree_System(wpath);
    
    return (attrs != INVALID_FILE_ATTRIBUTES);
}

bool XFile_remove_impl(const XString* fileName)
{
    if (!fileName) return false;
    
    wchar_t* wpath = XFile_toWidePath(fileName);
    if (!wpath) return false;
    
    // 先尝试删除文件
    if (DeleteFileW(wpath)) {
        XFree_System(wpath);
        return true;
    }
    
    // 如果失败，可能是目录，尝试删除目录
    BOOL result = RemoveDirectoryW(wpath);
    XFree_System(wpath);
    
    return result != 0;
}

bool XFile_rename_impl(const XString* oldName, const XString* newName)
{
    if (!oldName || !newName) return false;
    
    wchar_t* wold = XFile_toWidePath(oldName);
    wchar_t* wnew = XFile_toWidePath(newName);
    
    if (!wold || !wnew) {
        if (wold) XFree_System(wold);
        if (wnew) XFree_System(wnew);
        return false;
    }
    
    // 检查目标是否已存在
    if (GetFileAttributesW(wnew) != INVALID_FILE_ATTRIBUTES) {
        // 目标已存在，不覆盖
        XFree_System(wold);
        XFree_System(wnew);
        return false;
    }
    
    BOOL result = MoveFileW(wold, wnew);
    XFree_System(wold);
    XFree_System(wnew);
    
    return result != 0;
}

bool XFile_copy_impl(const XString* fileName, const XString* newName)
{
    if (!fileName || !newName) return false;
    
    wchar_t* wsrc = XFile_toWidePath(fileName);
    wchar_t* wdst = XFile_toWidePath(newName);
    
    if (!wsrc || !wdst) {
        if (wsrc) XFree_System(wsrc);
        if (wdst) XFree_System(wdst);
        return false;
    }
    
    // 检查目标是否已存在
    if (GetFileAttributesW(wdst) != INVALID_FILE_ATTRIBUTES) {
        // 目标已存在，不覆盖
        XFree_System(wsrc);
        XFree_System(wdst);
        return false;
    }
    
    BOOL result = CopyFileW(wsrc, wdst, TRUE);  // TRUE = 不覆盖已存在的文件
    XFree_System(wsrc);
    XFree_System(wdst);
    
    return result != 0;
}

bool XFile_link_impl(const XString* fileName, const XString* linkName)
{
    if (!fileName || !linkName) return false;
    
    wchar_t* wtarget = XFile_toWidePath(fileName);
    wchar_t* wlink = XFile_toWidePath(linkName);
    
    if (!wtarget || !wlink) {
        if (wtarget) XFree_System(wtarget);
        if (wlink) XFree_System(wlink);
        return false;
    }
    
    // 尝试创建符号链接
    BOOLEAN result = CreateSymbolicLinkW(wlink, wtarget, 0);
    
    if (!result) {
        // 尝试创建硬链接
        result = CreateHardLinkW(wlink, wtarget, NULL);
    }
    
    XFree_System(wtarget);
    XFree_System(wlink);
    
    return result != 0;
}

bool XFile_moveToTrash_impl(const XString* fileName, XString* pathInTrash)
{
    if (!fileName) return false;
    
    wchar_t* wpath = XFile_toWidePath(fileName);
    if (!wpath) return false;
    
    // 需要双空终止
    size_t len = wcslen(wpath);
    wchar_t* wpathDouble = (wchar_t*)XMalloc_System((len + 2) * sizeof(wchar_t));
    if (!wpathDouble) {
        XFree_System(wpath);
        return false;
    }
    wcscpy(wpathDouble, wpath);
    wpathDouble[len] = 0;
    wpathDouble[len + 1] = 0;
    XFree_System(wpath);
    
    SHFILEOPSTRUCTW shfos = {0};
    shfos.wFunc = FO_DELETE;
    shfos.pFrom = wpathDouble;
    shfos.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    
    int result = SHFileOperationW(&shfos);
    XFree_System(wpathDouble);
    
    if (result != 0) {
        if (pathInTrash) {
            XString_clear_base(pathInTrash);
        }
        return false;
    }
    
    // Windows 不返回回收站中的路径
    if (pathInTrash) {
        XString_clear_base(pathInTrash);
    }
    
    return true;
}

XString* XFile_symLinkTarget_impl(const XString* fileName)
{
    if (!fileName) return XString_create();
    
    wchar_t* wpath = XFile_toWidePath(fileName);
    if (!wpath) return XString_create();
    
    // 获取符号链接目标
    HANDLE hFile = CreateFileW(wpath, 0, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    XFree_System(wpath);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return XString_create();
    }
    
    // 获取最终路径
    wchar_t target[MAX_PATH + 1];
    DWORD len = GetFinalPathNameByHandleW(hFile, target, MAX_PATH, VOLUME_NAME_DOS);
    CloseHandle(hFile);
    
    if (len == 0 || len > MAX_PATH) {
        return XString_create();
    }
    
    // 转换为 UTF-8
    char utf8Path[MAX_PATH * 4];
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, target, len, utf8Path, sizeof(utf8Path), NULL, NULL);
    
    if (utf8Len <= 0) {
        return XString_create();
    }
    
    utf8Path[utf8Len] = '\0';
    return XString_create_utf8(utf8Path);
}

#endif // _WIN32
