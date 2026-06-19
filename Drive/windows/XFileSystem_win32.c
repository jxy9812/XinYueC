#ifdef _WIN32
#include "XFileSystem_platform.h"
#include "XMemory.h"
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

// 重解析点结构体
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
 * @brief 将 UTF-8 路径转换为 Windows 宽字符路径
 */
static wchar_t* toWidePath(const char* path)
{
    if (!path) return NULL;
    
    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (len <= 0) return NULL;
    
    wchar_t* wpath = (wchar_t*)XMalloc_System(len * sizeof(wchar_t));
    if (!wpath) return NULL;
    
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, len);
    return wpath;
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
 * @brief 获取文件属性并填充 XFileStat
 */
static bool fillFileStat(const char* path, WIN32_FILE_ATTRIBUTE_DATA* attrData, XFileStat* stat)
{
    if (!path || !stat) return false;
    
    memset(stat, 0, sizeof(XFileStat));
    
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, attrData)) {
        stat->exists = false;
        return false;
    }
    
    stat->exists = true;
    stat->isDir = (attrData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    stat->isFile = !stat->isDir;
    stat->isHidden = (attrData->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    stat->isSymLink = (attrData->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    stat->isJunction = false;  // 默认值，需要进一步检查
    stat->isShortcut = false;  // 默认值，需要检查 .lnk 文件
    
    // 文件大小
    ULARGE_INTEGER fileSize;
    fileSize.LowPart = attrData->nFileSizeLow;
    fileSize.HighPart = attrData->nFileSizeHigh;
    stat->size = (int64_t)fileSize.QuadPart;
    
    // 时间信息
    stat->birthTime = fileTimeToUnixTime(&attrData->ftCreationTime);
    stat->modificationTime = fileTimeToUnixTime(&attrData->ftLastWriteTime);
    stat->accessTime = fileTimeToUnixTime(&attrData->ftLastAccessTime);
    stat->metadataChangeTime = stat->modificationTime;
    
    // 权限
    stat->isReadable = true;
    stat->isWritable = (attrData->dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;
    
    // 可执行性检查
    if (stat->isFile) {
        const char* ext = strrchr(path, '.');
        if (ext) {
            stat->isExecutable = (_stricmp(ext, ".exe") == 0 ||
                                   _stricmp(ext, ".bat") == 0 ||
                                   _stricmp(ext, ".cmd") == 0 ||
                                   _stricmp(ext, ".com") == 0);
        }
    }
    
    // 权限标志
    stat->permissions = 0;
    if (stat->isReadable) stat->permissions |= 0x0400; // XFile_ReadUser
    if (stat->isWritable) stat->permissions |= 0x0200; // XFile_WriteUser
    if (stat->isExecutable) stat->permissions |= 0x0100; // XFile_ExeUser
    
    return true;
}

/* ============================================================================
 * 核心文件操作
 * ============================================================================ */

int XFileSystem_open(const char* path, int mode, int* error)
{
    if (!path) return -1;
    
    wchar_t* wpath = toWidePath(path);
    if (!wpath) {
        if (error) *error = XFileDevice_ResourceError;
        return -1;
    }
    
    DWORD desiredAccess = 0;
    DWORD shareMode = FILE_SHARE_READ;
    DWORD creationDisposition = OPEN_EXISTING;
    
    if (mode & XFileSystem_ReadOnly) desiredAccess |= GENERIC_READ;
    if (mode & XFileSystem_WriteOnly) desiredAccess |= GENERIC_WRITE;
    if (mode & XFileSystem_ReadWrite) desiredAccess |= GENERIC_READ | GENERIC_WRITE;
    
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
            // 纯写入模式：如果文件不存在则创建，存在则打开
            creationDisposition = OPEN_ALWAYS;
        }
    
    HANDLE hFile = CreateFileW(wpath, desiredAccess, shareMode, NULL,
                                creationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    XFree_System(wpath);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        if (error) *error = XFileDevice_OpenError;
        return -1;
    }
    
    int fd = _open_osfhandle((intptr_t)hFile, 0);
    if (fd < 0) {
        CloseHandle(hFile);
        if (error) *error = XFileDevice_OpenError;
        return -1;
    }
    
    if (mode & XFileSystem_Append) {
        _lseeki64(fd, 0, SEEK_END);
    }
    
    if (error) *error = XFileDevice_NoError;
    return fd;
}

void XFileSystem_close(int fd)
{
    if (fd >= 0) {
        _close(fd);
    }
}

int64_t XFileSystem_pos(int fd)
{
    if (fd < 0) return -1;
    return _lseeki64(fd, 0, SEEK_CUR);
}

bool XFileSystem_seek(int fd, int64_t pos)
{
    if (fd < 0 || pos < 0) return false;
    return _lseeki64(fd, pos, SEEK_SET) >= 0;
}

int64_t XFileSystem_read(int fd, void* buf, int64_t len)
{
    if (fd < 0 || !buf || len <= 0) return -1;
    
    int64_t totalRead = 0;
    while (totalRead < len) {
        int64_t toRead = len - totalRead;
        if (toRead > UINT_MAX) toRead = UINT_MAX;
        
        size_t bytesRead = _read(fd, (char*)buf + totalRead, (unsigned int)toRead);
        if (bytesRead < 0) return -1;
        if (bytesRead == 0) break;
        totalRead += bytesRead;
    }
    return totalRead;
}

int64_t XFileSystem_write(int fd, const void* buf, int64_t len)
{
    if (fd < 0 || !buf || len <= 0) return -1;
    
    int64_t totalWritten = 0;
    while (totalWritten < len) {
        int64_t toWrite = len - totalWritten;
        if (toWrite > UINT_MAX) toWrite = UINT_MAX;
        
        size_t bytesWritten = _write(fd, (const char*)buf + totalWritten, (unsigned int)toWrite);
        if (bytesWritten < 0) return -1;
        if (bytesWritten == 0) break;
        totalWritten += bytesWritten;
    }
    return totalWritten;
}

bool XFileSystem_flush(int fd)
{
    if (fd < 0) return false;
    
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    return FlushFileBuffers(hFile) != 0;
}

bool XFileSystem_resize(int fd, int64_t size)
{
    if (fd < 0 || size < 0) return false;
    
    int64_t oldPos = _telli64(fd);
    
    if (_lseeki64(fd, size, SEEK_SET) < 0) return false;
    
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (!SetEndOfFile(hFile)) {
        _lseeki64(fd, oldPos, SEEK_SET);
        return false;
    }
    
    _lseeki64(fd, oldPos, SEEK_SET);
    return true;
}

/* ============================================================================
 * 文件属性操作
 * ============================================================================ */

bool XFileSystem_stat(const char* path, XFileStat* stat)
{
    if (!path || !stat) return false;
    
    WIN32_FILE_ATTRIBUTE_DATA attrData;
    return fillFileStat(path, &attrData, stat);
}

bool XFileSystem_fstat(int fd, XFileStat* stat)
{
    if (fd < 0 || !stat) return false;
    
    struct _stati64 st;
    if (_fstati64(fd, &st) != 0) return false;
    
    memset(stat, 0, sizeof(XFileStat));
    stat->exists = true;
    stat->isFile = (st.st_mode & _S_IFREG) != 0;
    stat->isDir = (st.st_mode & _S_IFDIR) != 0;
    stat->size = st.st_size;
    stat->modificationTime = st.st_mtime;
    stat->accessTime = st.st_atime;
    stat->birthTime = st.st_ctime;
    
    return true;
}

int64_t XFileSystem_size(int fd)
{
    if (fd < 0) return -1;
    
    struct _stati64 st;
    if (_fstati64(fd, &st) != 0) return -1;
    
    return st.st_size;
}

/* ============================================================================
 * 文件系统操作
 * ============================================================================ */

bool XFileSystem_exists(const char* path)
{
    if (!path) return false;
    
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    
    DWORD attrs = GetFileAttributesW(wpath);
    XFree_System(wpath);
    
    return attrs != INVALID_FILE_ATTRIBUTES;
}

bool XFileSystem_remove(const char* path)
{
    if (!path) return false;
    
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    
    BOOL result = DeleteFileW(wpath);
    if (!result) {
        result = RemoveDirectoryW(wpath);
    }
    
    XFree_System(wpath);
    return result != 0;
}

bool XFileSystem_rename(const char* oldPath, const char* newPath)
{
    if (!oldPath || !newPath) return false;
    
    wchar_t* wold = toWidePath(oldPath);
    wchar_t* wnew = toWidePath(newPath);
    
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

bool XFileSystem_copy(const char* srcPath, const char* dstPath)
{
    if (!srcPath || !dstPath) return false;
    
    wchar_t* wsrc = toWidePath(srcPath);
    wchar_t* wdst = toWidePath(dstPath);
    
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

bool XFileSystem_link(const char* targetPath, const char* linkPath)
{
    if (!targetPath || !linkPath) return false;
    
    wchar_t* wtarget = toWidePath(targetPath);
    wchar_t* wlink = toWidePath(linkPath);
    
    if (!wtarget || !wlink) {
        if (wtarget) XFree_System(wtarget);
        if (wlink) XFree_System(wlink);
        return false;
    }
    
    BOOLEAN result = CreateSymbolicLinkW(wlink, wtarget, 0);
    if (!result) {
        result = CreateHardLinkW(wlink, wtarget, NULL);
    }
    
    XFree_System(wtarget);
    XFree_System(wlink);
    
    return result != 0;
}

bool XFileSystem_moveToTrash(const char* path)
{
    if (!path) return false;
    
    wchar_t* wpath = toWidePath(path);
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
    
    return result == 0;
}

bool XFileSystem_readLink(const char* path, char* target, int targetSize)
{
    if (!path || !target || targetSize <= 0) return false;
    
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    
    HANDLE hFile = CreateFileW(wpath, 0, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    XFree_System(wpath);
    
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    wchar_t targetPath[MAX_PATH];
    DWORD len = GetFinalPathNameByHandleW(hFile, targetPath, MAX_PATH, VOLUME_NAME_DOS);
    CloseHandle(hFile);
    
    if (len == 0 || len > MAX_PATH) return false;
    
    WideCharToMultiByte(CP_UTF8, 0, targetPath, len, target, targetSize, NULL, NULL);
    return true;
}

bool XFileSystem_setPermissions(const char* path, uint32_t permissions)
{
    if (!path) return false;
    
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    
    DWORD attrs = GetFileAttributesW(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        XFree_System(wpath);
        return false;
    }
    
    // Windows 只支持只读属性
    if (!(permissions & 0x0200)) { // 无写权限
        attrs |= FILE_ATTRIBUTE_READONLY;
    } else {
        attrs &= ~FILE_ATTRIBUTE_READONLY;
    }
    
    BOOL result = SetFileAttributesW(wpath, attrs);
    XFree_System(wpath);
    
    return result != 0;
}

bool XFileSystem_setFileTime(int fd, int timeType, int64_t timeValue)
{
    if (fd < 0) return false;
    
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    // 转换为 FILETIME
    ULARGE_INTEGER ul;
    ul.QuadPart = (timeValue * 10000000LL) + 116444736000000000LL;
    
    FILETIME ft;
    ft.dwLowDateTime = ul.LowPart;
    ft.dwHighDateTime = ul.HighPart;
    
    FILETIME* ftCreate = NULL;
    FILETIME* ftAccess = NULL;
    FILETIME* ftWrite = NULL;
    
    switch (timeType) {
        case 0: ftAccess = &ft; break;      // AccessTime
        case 1: ftCreate = &ft; break;      // BirthTime
        case 2: 
        case 3: ftWrite = &ft; break;       // MetadataChangeTime, ModificationTime
    }
    
    return SetFileTime(hFile, ftCreate, ftAccess, ftWrite) != 0;
}

/* ============================================================================
 * 内存映射
 * ============================================================================ */

void* XFileSystem_map(int fd, int64_t offset, int64_t size, bool writable)
{
    if (fd < 0 || size <= 0) return NULL;
    
    HANDLE hFile = (HANDLE)_get_osfhandle(fd);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;
    
    DWORD protect = writable ? PAGE_READWRITE : PAGE_READONLY;
    DWORD sizeHigh = (DWORD)(size >> 32);
    DWORD sizeLow = (DWORD)(size & 0xFFFFFFFF);
    
    HANDLE hMap = CreateFileMappingA(hFile, NULL, protect, sizeHigh, sizeLow, NULL);
    if (!hMap) return NULL;
    
    DWORD access = writable ? FILE_MAP_WRITE : FILE_MAP_READ;
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

/* ============================================================================
 * 路径操作
 * ============================================================================ */

bool XFileSystem_absolutePath(const char* path, char* absPath, int absPathSize)
{
    if (!path || !absPath || absPathSize <= 0) return false;
    return _fullpath(absPath, path, absPathSize) != NULL;
}

bool XFileSystem_canonicalPath(const char* path, char* canPath, int canPathSize)
{
    if (!path || !canPath || canPathSize <= 0) return false;
    
    char absPath[MAX_PATH];
    if (!_fullpath(absPath, path, MAX_PATH)) return false;
    
    wchar_t* wpath = toWidePath(absPath);
    if (!wpath) return false;
    
    DWORD attrs = GetFileAttributesW(wpath);
    XFree_System(wpath);
    
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    
    strncpy(canPath, absPath, canPathSize);
    return true;
}

/* ============================================================================
 * 目录操作
 * ============================================================================ */

bool XFileSystem_mkdir(const char* path)
{
    if (!path) return false;
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    BOOL result = CreateDirectoryW(wpath, NULL);
    XFree_System(wpath);
    return result != 0;
}

bool XFileSystem_rmdir(const char* path)
{
    if (!path) return false;
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    BOOL result = RemoveDirectoryW(wpath);
    XFree_System(wpath);
    return result != 0;
}

bool XFileSystem_mkdir_p(const char* path)
{
    if (!path) return false;
    
    char* pathCopy = _strdup(path);
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
            wchar_t* wpath = toWidePath(pathCopy);
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
        wchar_t* wpath = toWidePath(pathCopy);
        if (wpath) {
            if (GetFileAttributesW(wpath) == INVALID_FILE_ATTRIBUTES) {
                result = CreateDirectoryW(wpath, NULL) != 0;
            }
            XFree_System(wpath);
        }
    }
    
    free(pathCopy);
    return result;
}

struct DirIteratorData {
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    bool firstEntry;
};

XDirIterator XFileSystem_opendir(const char* path)
{
    if (!path) return NULL;
    
    char searchPath[MAX_PATH];
    _snprintf(searchPath, MAX_PATH, "%s\\*", path);
    
    wchar_t* wpath = toWidePath(searchPath);
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
    
    WideCharToMultiByte(CP_UTF8, 0, data->findData.cFileName, -1, entry->name, sizeof(entry->name), NULL, NULL);
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
 * 所有者操作
 * ============================================================================ */

bool XFileSystem_getOwner(const char* path, char* owner, int ownerSize, uint32_t* ownerId)
{
    if (!path) return false;
    
    PSID pOwnerSid = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    
    if (GetNamedSecurityInfoA(path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                              &pOwnerSid, NULL, NULL, NULL, &pSD) != ERROR_SUCCESS) {
        return false;
    }
    
    bool result = false;
    if (owner && ownerSize > 0) {
        char name[256], domain[256];
        DWORD nameLen = sizeof(name), domainLen = sizeof(domain);
        SID_NAME_USE use;
        if (LookupAccountSidA(NULL, pOwnerSid, name, &nameLen, domain, &domainLen, &use)) {
            strncpy(owner, name, ownerSize);
            result = true;
        }
    }
    if (ownerId) {
        PUCHAR subAuthCount = GetSidSubAuthorityCount(pOwnerSid);
        if (subAuthCount && *subAuthCount > 0) {
            *ownerId = *GetSidSubAuthority(pOwnerSid, *subAuthCount - 1);
            result = true;
        }
    }
    LocalFree(pSD);
    return result;
}

bool XFileSystem_getGroup(const char* path, char* group, int groupSize, uint32_t* groupId)
{
    if (!path) return false;
    
    PSID pGroupSid = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    
    if (GetNamedSecurityInfoA(path, SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
                              NULL, &pGroupSid, NULL, NULL, &pSD) != ERROR_SUCCESS) {
        return false;
    }
    
    bool result = false;
    if (group && groupSize > 0) {
        char name[256], domain[256];
        DWORD nameLen = sizeof(name), domainLen = sizeof(domain);
        SID_NAME_USE use;
        if (LookupAccountSidA(NULL, pGroupSid, name, &nameLen, domain, &domainLen, &use)) {
            strncpy(group, name, groupSize);
            result = true;
        }
    }
    if (groupId) {
        PUCHAR subAuthCount = GetSidSubAuthorityCount(pGroupSid);
        if (subAuthCount && *subAuthCount > 0) {
            *groupId = *GetSidSubAuthority(pGroupSid, *subAuthCount - 1);
            result = true;
        }
    }
    LocalFree(pSD);
    return result;
}

bool XFileSystem_setOwner(const char* path, uint32_t ownerId)
{
    (void)path; (void)ownerId;
    return false; // 需要管理员权限
}

bool XFileSystem_setGroup(const char* path, uint32_t groupId)
{
    (void)path; (void)groupId;
    return false; // 需要管理员权限
}

/* ============================================================================
 * 符号链接高级操作
 * ============================================================================ */

bool XFileSystem_isJunction(const char* path)
{
    if (!path) return false;
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    
    HANDLE hFile = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    XFree_System(wpath);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    BYTE buffer[MAX_PATH * 2];
    DWORD bytesReturned;
    bool isJunction = false;
    
    if (DeviceIoControl(hFile, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer, sizeof(buffer), &bytesReturned, NULL)) {
        PREPARSE_DATA_BUFFER reparse = (PREPARSE_DATA_BUFFER)buffer;
        isJunction = (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT);
    }
    CloseHandle(hFile);
    return isJunction;
}

bool XFileSystem_isShortcut(const char* path, char* target, int targetSize)
{
    if (!path) return false;
    size_t len = strlen(path);
    if (len < 5 || _stricmp(path + len - 4, ".lnk") != 0) return false;
    
    if (target && targetSize > 0) {
        // 简化实现：返回空字符串
        target[0] = '\0';
    }
    return true;
}

bool XFileSystem_junctionTarget(const char* path, char* target, int targetSize)
{
    if (!path || !target || targetSize <= 0) return false;
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    
    HANDLE hFile = CreateFileW(wpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    XFree_System(wpath);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    BYTE buffer[MAX_PATH * 2];
    DWORD bytesReturned;
    if (!DeviceIoControl(hFile, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer, sizeof(buffer), &bytesReturned, NULL)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    
    PREPARSE_DATA_BUFFER reparse = (PREPARSE_DATA_BUFFER)buffer;
    if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        USHORT nameOffset = reparse->MountPointReparseBuffer.SubstituteNameOffset / sizeof(WCHAR);
        USHORT nameLength = reparse->MountPointReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
        
        wchar_t targetPath[MAX_PATH];
        wcsncpy_s(targetPath, MAX_PATH, reparse->MountPointReparseBuffer.PathBuffer + nameOffset, nameLength);
        targetPath[nameLength] = L'\0';
        
        char targetUtf8[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, targetPath, -1, targetUtf8, MAX_PATH, NULL, NULL);
        
        char* finalPath = targetUtf8;
        if (strncmp(targetUtf8, "\\??\\", 4) == 0) finalPath = targetUtf8 + 4;
        strncpy(target, finalPath, targetSize);
        return true;
    }
    return false;
}

/* ============================================================================
 * 文件系统信息
 * ============================================================================ */

int64_t XFileSystem_totalSpace(const char* path)
{
    if (!path) return 0;
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return 0;
    
    ULARGE_INTEGER totalBytes;
    if (!GetDiskFreeSpaceExW(wpath, NULL, &totalBytes, NULL)) {
        XFree_System(wpath);
        return 0;
    }
    XFree_System(wpath);
    return (int64_t)totalBytes.QuadPart;
}

int64_t XFileSystem_availableSpace(const char* path)
{
    if (!path) return 0;
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return 0;
    
    ULARGE_INTEGER freeBytes;
    if (!GetDiskFreeSpaceExW(wpath, &freeBytes, NULL, NULL)) {
        XFree_System(wpath);
        return 0;
    }
    XFree_System(wpath);
    return (int64_t)freeBytes.QuadPart;
}

/* ============================================================================
 * 特殊路径
 * ============================================================================ */

bool XFileSystem_currentPath(char* path, int pathSize)
{
    if (!path || pathSize <= 0) return false;
    return _getcwd(path, pathSize) != NULL;
}

bool XFileSystem_setCurrentPath(const char* path)
{
    if (!path) return false;
    return _chdir(path) == 0;
}

bool XFileSystem_homePath(char* path, int pathSize)
{
    if (!path || pathSize <= 0) return false;
    
    char* home = getenv("USERPROFILE");
    if (home) {
        strncpy(path, home, pathSize);
        return true;
    }
    
    char* homeDrive = getenv("HOMEDRIVE");
    char* homePath = getenv("HOMEPATH");
    if (homeDrive && homePath) {
        _snprintf(path, pathSize, "%s%s", homeDrive, homePath);
        return true;
    }
    
    return false;
}

bool XFileSystem_rootPath(char* path, int pathSize)
{
    if (!path || pathSize < 4) return false;
    
    char cwd[MAX_PATH];
    if (_getcwd(cwd, MAX_PATH) && cwd[0] && cwd[1] == ':') {
        path[0] = cwd[0];
        path[1] = ':';
        path[2] = '\\';
        path[3] = '\0';
        return true;
    }
    
    strcpy(path, "C:\\");
    return true;
}

bool XFileSystem_tempPath(char* path, int pathSize)
{
    if (!path || pathSize <= 0) return false;
    
    wchar_t wTempPath[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, wTempPath);
    if (len == 0 || len >= MAX_PATH) return false;
    
    WideCharToMultiByte(CP_UTF8, 0, wTempPath, -1, path, pathSize, NULL, NULL);
    return true;
}

/* ============================================================================
 * 驱动器列表
 * ============================================================================ */

struct DriveIteratorData {
    DWORD drives;
    int current;
};

XDriveIterator XFileSystem_beginDrives(void)
{
    struct DriveIteratorData* iter = (struct DriveIteratorData*)XMalloc_System(sizeof(struct DriveIteratorData));
    if (!iter) return NULL;
    
    iter->drives = GetLogicalDrives();
    iter->current = 0;
    return (XDriveIterator)iter;
}

bool XFileSystem_nextDrive(XDriveIterator iter, char* drive, int driveSize)
{
    if (!iter || !drive || driveSize < 4) return false;
    struct DriveIteratorData* data = (struct DriveIteratorData*)iter;
    
    while (data->current < 26) {
        if (data->drives & (1 << data->current)) {
            drive[0] = 'A' + data->current;
            drive[1] = ':';
            drive[2] = '\\';
            drive[3] = '\0';
            data->current++;
            return true;
        }
        data->current++;
    }
    return false;
}

void XFileSystem_endDrives(XDriveIterator iter)
{
    if (iter) XFree_System(iter);
}

/* ============================================================================
 * 递归删除目录
 * ============================================================================ */

bool XFileSystem_rmdir_recursive(const char* path)
{
    if (!path) return false;
    
    wchar_t* wpath = toWidePath(path);
    if (!wpath) return false;
    
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
    shfos.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
    
    int result = SHFileOperationW(&shfos);
    XFree_System(wpathDouble);
    
    return result == 0;
}

#endif // _WIN32