#ifdef _WIN32
#include "XFileInfo.h"
#include "XMemory.h"
#include <windows.h>
#include <direct.h>
#include <shlobj.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <initguid.h>
#include <shobjidl.h>
#include <objbase.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

// 如果 SE_FILE_OBJECT 未定义，使用替代定义
#ifndef SE_FILE_OBJECT
#define SE_FILE_OBJECT 1
#endif

// 符号链接和 Junction 的重解析点结构体
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

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

// 将 FILETIME 转换为 Unix 时间戳（秒）
static int64_t fileTimeToUnixTime(const FILETIME* ft)
{
    ULARGE_INTEGER ul;
    ul.LowPart = ft->dwLowDateTime;
    ul.HighPart = ft->dwHighDateTime;
    // FILETIME 是 100 纳秒单位，从 1601-01-01 开始
    // Unix 时间戳是从 1970-01-01 开始
    // 两者相差 11644473600 秒
    return (int64_t)((ul.QuadPart - 116444736000000000LL) / 10000000);
}

// 解析快捷方式 (.lnk) 文件的目标路径
static XString* XFileInfo_resolveShortcut(const char* lnkPath)
{
    if (!lnkPath) return XString_create();
    
    HRESULT hr;
    IShellLinkA* pShellLink = NULL;
    IPersistFile* pPersistFile = NULL;
    XString* result = XString_create();
    
    // 初始化 COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        // COM 可能已经初始化
    }
    
    // 创建 ShellLink 对象
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IShellLinkA, (void**)&pShellLink);
    if (FAILED(hr) || !pShellLink) {
        CoUninitialize();
        return result;
    }
    
    // 获取 IPersistFile 接口
    hr = pShellLink->lpVtbl->QueryInterface(pShellLink, &IID_IPersistFile, (void**)&pPersistFile);
    if (FAILED(hr) || !pPersistFile) {
        pShellLink->lpVtbl->Release(pShellLink);
        CoUninitialize();
        return result;
    }
    
    // 将路径转换为宽字符
    wchar_t wPath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, lnkPath, -1, wPath, MAX_PATH);
    
    // 加载快捷方式文件
    hr = pPersistFile->lpVtbl->Load(pPersistFile, wPath, STGM_READ);
    if (SUCCEEDED(hr)) {
        // 解析快捷方式（可能需要查找目标）
        hr = pShellLink->lpVtbl->Resolve(pShellLink, NULL, SLR_NO_UI);
        
        if (SUCCEEDED(hr)) {
            // 获取目标路径
            char targetPath[MAX_PATH];
            hr = pShellLink->lpVtbl->GetPath(pShellLink, targetPath, MAX_PATH, NULL, 0);
            
            if (SUCCEEDED(hr) && targetPath[0] != '\0') {
                XString_delete_base(result);
                result = XString_create_utf8(targetPath);
            }
        }
    }
    
    pPersistFile->lpVtbl->Release(pPersistFile);
    pShellLink->lpVtbl->Release(pShellLink);
    CoUninitialize();
    
    return result;
}

// 检查路径是否为绝对路径（静态版本）
bool XFileInfo_isAbsolutePath_static(const char* path)
{
    if (!path || !path[0]) return false;
    
        // UNC 路径 "\\server\share"
    if (path[0] == '\\' && path[1] == '\\') return true;
    
    // 从当前驱动器根目录开始 "\path"
    if (path[0] == '\\') return true;
    
    // 驱动器路径 "C:\"
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return true;
    
    return false;
}

// 获取文件属性并缓存
static void XFileInfo_updateCache(XFileInfo* info)
{
    if (!info || !info->m_filePath) return;
    if (info->m_cacheValid && info->m_caching) return;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return;
    
    // 获取文件属性
    WIN32_FILE_ATTRIBUTE_DATA attrData;
    if (!GetFileAttributesExA(pathUtf8, GetFileExInfoStandard, &attrData)) {
        // 文件不存在
        info->m_exists = false;
        info->m_isFile = false;
        info->m_isDir = false;
        info->m_cacheValid = true;
        return;
    }
    
    info->m_exists = true;
    info->m_isDir = (attrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info->m_isFile = !info->m_isDir;
    info->m_isHidden = (attrData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    info->m_isSymLink = (attrData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    info->m_isJunction = false;  // 需要进一步检查
    info->m_isShortcut = false;  // 需要检查 .lnk 文件
    
        // 检查是否为 Junction
    if (info->m_isSymLink) {
        HANDLE hFile = CreateFileA(pathUtf8, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                   NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            BYTE buffer[MAX_PATH * 2];
            DWORD bytesReturned;
            if (DeviceIoControl(hFile, FSCTL_GET_REPARSE_POINT, NULL, 0,
                               buffer, sizeof(buffer), &bytesReturned, NULL)) {
                PREPARSE_DATA_BUFFER reparse = (PREPARSE_DATA_BUFFER)buffer;
                info->m_isJunction = (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT);
                info->m_isSymLink = (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK);
            }
            CloseHandle(hFile);
        }
    }
    
    // 检查是否为快捷方式
    size_t len = strlen(pathUtf8);
    if (len > 4 && _stricmp(pathUtf8 + len - 4, ".lnk") == 0) {
        info->m_isShortcut = true;
        info->m_isSymLink = true;  // Qt 将快捷方式视为符号链接
    }
    
    // 文件大小
    ULARGE_INTEGER fileSize;
    fileSize.LowPart = attrData.nFileSizeLow;
    fileSize.HighPart = attrData.nFileSizeHigh;
    info->m_size = (int64_t)fileSize.QuadPart;
    
    // 时间信息
    info->m_birthTime = fileTimeToUnixTime(&attrData.ftCreationTime);
    info->m_modificationTime = fileTimeToUnixTime(&attrData.ftLastWriteTime);
    info->m_accessTime = fileTimeToUnixTime(&attrData.ftLastAccessTime);
    info->m_metadataChangeTime = info->m_modificationTime;  // Windows 不单独存储
    
    // 权限检查
    info->m_isReadable = true;   // 默认值，实际需要检查
    info->m_isWritable = (attrData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;
    info->m_isExecutable = false;
    
    // 检查可执行性（检查扩展名）
    if (info->m_isFile) {
        const char* ext = strrchr(pathUtf8, '.');
        if (ext) {
                        info->m_isExecutable = (_stricmp(ext, ".exe") == 0 ||
                                   _stricmp(ext, ".bat") == 0 ||
                                   _stricmp(ext, ".cmd") == 0 ||
                                   _stricmp(ext, ".com") == 0);
        }
    }
    
    info->m_cacheValid = true;
}

/* ============================================================================
 * 路径操作 - Windows 实现
 * ============================================================================ */

XString* XFileInfo_absoluteFilePath(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return XString_create();
    
    char absPath[MAX_PATH];
    if (_fullpath(absPath, pathUtf8, MAX_PATH)) {
        return XString_create_utf8(absPath);
    }
    return XString_create();
}

XString* XFileInfo_canonicalFilePath(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return XString_create();
    
    char absPath[MAX_PATH];
    char* result = _fullpath(absPath, pathUtf8, MAX_PATH);
    if (result) {
        // 验证路径是否存在
        DWORD attrs = GetFileAttributesA(result);
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            return XString_create_utf8(result);
        }
    }
    return XString_create();
}

XString* XFileInfo_absolutePath(const XFileInfo* info)
{
    XString* absFilePath = XFileInfo_absoluteFilePath(info);
    if (!absFilePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(absFilePath);
    if (!pathUtf8) {
        XString_delete_base(absFilePath);
        return XString_create();
    }
    
    size_t len = strlen(pathUtf8);
    if (len == 0) {
        XString_delete_base(absFilePath);
        return XString_create();
    }
    
    // 查找最后一个分隔符
    char* lastSlash = strrchr((char*)pathUtf8, '\\');
    if (!lastSlash) lastSlash = strrchr((char*)pathUtf8, '/');
    
    XString* result;
    if (lastSlash) {
        *lastSlash = '\0';
        result = XString_create_utf8(pathUtf8);
    } else {
        result = XString_create_utf8(".");
    }
    
    XString_delete_base(absFilePath);
    return result;
}

XString* XFileInfo_canonicalPath(const XFileInfo* info)
{
    XString* canFilePath = XFileInfo_canonicalFilePath(info);
    if (!canFilePath || XString_length_base(canFilePath) == 0) {
        if (canFilePath) XString_delete_base(canFilePath);
        return XString_create();
    }
    
    const char* pathUtf8 = XString_toUtf8(canFilePath);
    if (!pathUtf8) {
        XString_delete_base(canFilePath);
        return XString_create();
    }
    
    // 查找最后一个分隔符
    char* lastSlash = strrchr((char*)pathUtf8, '\\');
    if (!lastSlash) lastSlash = strrchr((char*)pathUtf8, '/');
    
    XString* result;
    if (lastSlash) {
        *lastSlash = '\0';
        result = XString_create_utf8(pathUtf8);
    } else {
        result = XString_create_utf8(".");
    }
    
        XString_delete_base(canFilePath);
    return result;
}

/* ============================================================================
 * 文件类型检查 - Windows 实现
 * ============================================================================ */

bool XFileInfo_exists(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_exists;
}

bool XFileInfo_exists_static(const XString* path)
{
    if (!path) return false;
    
    const char* pathUtf8 = XString_toUtf8(path);
    if (!pathUtf8) return false;
    
    DWORD attrs = GetFileAttributesA(pathUtf8);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

void XFileInfo_stat(XFileInfo* info)
{
    if (!info) return;
    info->m_cacheValid = false;
    XFileInfo_updateCache(info);
}

bool XFileInfo_isFile(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_isFile;
}

bool XFileInfo_isDir(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_isDir;
}

bool XFileInfo_isSymLink(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_isSymLink;
}

bool XFileInfo_isSymbolicLink(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    // 真正的符号链接，不包括快捷方式
    return info->m_isSymLink && !info->m_isShortcut;
}

bool XFileInfo_isShortcut(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_isShortcut;
}

bool XFileInfo_isJunction(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_isJunction;
}

bool XFileInfo_isRoot(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return false;
    
    char absPath[MAX_PATH];
    if (!_fullpath(absPath, pathUtf8, MAX_PATH)) return false;
    
    size_t len = strlen(absPath);
    
        // 驱动器根目录 "C:\"
    if (len == 3 && isalpha((unsigned char)absPath[0]) &&
        absPath[1] == ':' && absPath[2] == '\\') {
        return true;
    }
    
        // UNC 路径 "\\server\share\"
        if (absPath[0] == '\\' && absPath[1] == '\\') {
            const char* p = absPath + 2;
            while (*p && *p != '\\') p++;
            if (*p == '\\') {
                p++;
                while (*p && *p != '\\') p++;
                if (*p == '\\' && *(p + 1) == '\0') return true;
                if (*p == '\0') return true;
            }
        }
    
    return false;
}

bool XFileInfo_isBundle(const XFileInfo* info)
{
    // Bundle 只在 macOS/iOS 上存在
    (void)info;
    return false;
}

bool XFileInfo_isHidden(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_isHidden;
}

/* ============================================================================
 * 路径类型检查 - Windows 实现
 * ============================================================================ */

bool XFileInfo_isAbsolute(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    return XFileInfo_isAbsolutePath_static(pathUtf8);
}

bool XFileInfo_isNativePath(const XFileInfo* info)
{
    // 检查是否为 Qt 资源路径（以 ":" 开头）
    if (!info || !info->m_filePath) return false;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return false;
    
    // 以 ":" 开头的是资源路径，不是本地路径
    if (pathUtf8[0] == ':') return false;
    
    return true;
}

bool XFileInfo_makeAbsolute(XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
    
    // 如果已经是绝对路径，返回 true（无需转换）
    if (XFileInfo_isAbsolute(info)) return true;
    
    XString* absPath = XFileInfo_absoluteFilePath(info);
    if (!absPath || XString_length_base(absPath) == 0) {
        if (absPath) XString_delete_base(absPath);
        return false;
    }
    
    XString_delete_base(info->m_filePath);
    info->m_filePath = absPath;
    info->m_cacheValid = false;
    return true;
}

/* ============================================================================
 * 权限检查 - Windows 实现
 * ============================================================================ */

bool XFileInfo_isReadable(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return false;
    
    // 尝试打开文件/目录来检查可读性
    HANDLE hFile = CreateFileA(pathUtf8, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    CloseHandle(hFile);
    return true;
}

bool XFileInfo_isWritable(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return false;
    
    // 检查只读属性
    DWORD attrs = GetFileAttributesA(pathUtf8);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (attrs & FILE_ATTRIBUTE_READONLY) return false;
    
    // 尝试打开文件来检查可写性
    HANDLE hFile = CreateFileA(pathUtf8, GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    CloseHandle(hFile);
    return true;
}

bool XFileInfo_isExecutable(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_isExecutable;
}

bool XFileInfo_permission(const XFileInfo* info, XFilePermissions permissions)
{
    if (!info) return false;
    
    XFilePermissions perms = XFileInfo_permissions(info);
    return (perms & permissions) == permissions;
}

XFilePermissions XFileInfo_permissions(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return 0;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return 0;
    
    XFilePermissions perms = 0;
    
    // 检查可读性
    if (XFileInfo_isReadable(info)) {
        perms |= XFile_ReadUser | XFile_ReadOwner;
    }
    
    // 检查可写性
    if (XFileInfo_isWritable(info)) {
        perms |= XFile_WriteUser | XFile_WriteOwner;
    }
    
    // 检查可执行性
    if (XFileInfo_isExecutable(info)) {
        perms |= XFile_ExeUser | XFile_ExeOwner;
    }
    
    return perms;
}

/* ============================================================================
 * 文件属性 - Windows 实现
 * ============================================================================ */

int64_t XFileInfo_size(const XFileInfo* info)
{
    if (!info) return 0;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_size;
}

XDateTime XFileInfo_birthTime(const XFileInfo* info)
{
    return XFileInfo_fileTime(info, XFile_BirthTime);
}

XDateTime XFileInfo_metadataChangeTime(const XFileInfo* info)
{
    return XFileInfo_fileTime(info, XFile_MetadataChangeTime);
}

XDateTime XFileInfo_lastModified(const XFileInfo* info)
{
    return XFileInfo_fileTime(info, XFile_ModificationTime);
}

XDateTime XFileInfo_lastRead(const XFileInfo* info)
{
    return XFileInfo_fileTime(info, XFile_AccessTime);
}

XDateTime XFileInfo_fileTime(const XFileInfo* info, XFileTime time)
{
    XDateTime result = XDateTime_create();
    
    if (!info) return result;
    
    XFileInfo_updateCache((XFileInfo*)info);
    
    int64_t timestamp = 0;
    switch (time) {
        case XFile_BirthTime:
            timestamp = info->m_birthTime;
            break;
        case XFile_MetadataChangeTime:
            timestamp = info->m_metadataChangeTime;
            break;
        case XFile_ModificationTime:
            timestamp = info->m_modificationTime;
            break;
        case XFile_AccessTime:
            timestamp = info->m_accessTime;
            break;
    }
    
        if (timestamp > 0) {
        XDateTime_setSecsSinceEpoch(&result, timestamp);
    }
    
    return result;
}

/* ============================================================================
 * 所有者信息 - Windows 实现
 * ============================================================================ */

XString* XFileInfo_owner(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return XString_create();
    
    // 获取文件安全描述符
    PSID pOwnerSid = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    
    if (GetNamedSecurityInfoA(pathUtf8, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                              &pOwnerSid, NULL, NULL, NULL, &pSD) != ERROR_SUCCESS) {
        return XString_create();
    }
    
    // 将 SID 转换为账户名
    char name[256];
    char domain[256];
    DWORD nameLen = sizeof(name);
    DWORD domainLen = sizeof(domain);
    SID_NAME_USE use;
    
    XString* result = XString_create();
    if (LookupAccountSidA(NULL, pOwnerSid, name, &nameLen, domain, &domainLen, &use)) {
        result = XString_create_utf8(name);
    }
    
    LocalFree(pSD);
    return result;
}

uint32_t XFileInfo_ownerId(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return (uint32_t)-2;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return (uint32_t)-2;
    
    PSID pOwnerSid = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    
    if (GetNamedSecurityInfoA(pathUtf8, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                              &pOwnerSid, NULL, NULL, NULL, &pSD) != ERROR_SUCCESS) {
        return (uint32_t)-2;
    }
    
    // 获取 SID 的最后部分作为 ID
    uint32_t id = (uint32_t)-2;
    PSID_IDENTIFIER_AUTHORITY auth = GetSidIdentifierAuthority(pOwnerSid);
    PUCHAR subAuthCount = GetSidSubAuthorityCount(pOwnerSid);
    if (*subAuthCount > 0) {
        id = *GetSidSubAuthority(pOwnerSid, *subAuthCount - 1);
    }
    
    LocalFree(pSD);
    return id;
}

XString* XFileInfo_group(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return XString_create();
    
    PSID pGroupSid = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    
    if (GetNamedSecurityInfoA(pathUtf8, SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
                              NULL, &pGroupSid, NULL, NULL, &pSD) != ERROR_SUCCESS) {
        return XString_create();
    }
    
    char name[256];
    char domain[256];
    DWORD nameLen = sizeof(name);
    DWORD domainLen = sizeof(domain);
    SID_NAME_USE use;
    
    XString* result = XString_create();
    if (LookupAccountSidA(NULL, pGroupSid, name, &nameLen, domain, &domainLen, &use)) {
        result = XString_create_utf8(name);
    }
    
    LocalFree(pSD);
    return result;
}

uint32_t XFileInfo_groupId(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return (uint32_t)-2;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return (uint32_t)-2;
    
    PSID pGroupSid = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    
    if (GetNamedSecurityInfoA(pathUtf8, SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
                              NULL, &pGroupSid, NULL, NULL, &pSD) != ERROR_SUCCESS) {
        return (uint32_t)-2;
    }
    
        uint32_t id = (uint32_t)-2;
    PUCHAR subAuthCount = GetSidSubAuthorityCount(pGroupSid);
    if (*subAuthCount > 0) {
        id = *GetSidSubAuthority(pGroupSid, *subAuthCount - 1);
    }
    
    LocalFree(pSD);
    return id;
}

/* ============================================================================
 * 符号链接 - Windows 实现
 * ============================================================================ */

XString* XFileInfo_symLinkTarget(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return XString_create();
    
    // 检查是否为快捷方式
    size_t len = strlen(pathUtf8);
    if (len > 4 && _stricmp(pathUtf8 + len - 4, ".lnk") == 0) {
        return XFileInfo_resolveShortcut(pathUtf8);
    }
    
    // 检查是否为符号链接或 Junction
    HANDLE hFile = CreateFileA(pathUtf8, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                               NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return XString_create();
    }
    
    BYTE buffer[MAX_PATH * 2];
    DWORD bytesReturned;
    
    if (!DeviceIoControl(hFile, FSCTL_GET_REPARSE_POINT, NULL, 0,
                         buffer, sizeof(buffer), &bytesReturned, NULL)) {
        CloseHandle(hFile);
        return XString_create();
    }
    
    CloseHandle(hFile);
    
    PREPARSE_DATA_BUFFER reparse = (PREPARSE_DATA_BUFFER)buffer;
    
    if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        // 符号链接
        USHORT nameOffset = reparse->SymbolicLinkReparseBuffer.SubstituteNameOffset / sizeof(WCHAR);
        USHORT nameLength = reparse->SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
        
        // 获取目标路径
        wchar_t targetPath[MAX_PATH];
        wcsncpy_s(targetPath, MAX_PATH, reparse->SymbolicLinkReparseBuffer.PathBuffer + nameOffset, nameLength);
        targetPath[nameLength] = L'\0';
        
        // 转换为 UTF-8
        char targetUtf8[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, targetPath, -1, targetUtf8, MAX_PATH, NULL, NULL);
        
        // 处理 \??\ 前缀
        char* finalPath = targetUtf8;
        if (strncmp(targetUtf8, "\\??\\", 4) == 0) {
            finalPath = targetUtf8 + 4;
        }
        
        return XString_create_utf8(finalPath);
    }
    else if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        // Junction
        USHORT nameOffset = reparse->MountPointReparseBuffer.SubstituteNameOffset / sizeof(WCHAR);
        USHORT nameLength = reparse->MountPointReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
        
        // 获取目标路径
        wchar_t targetPath[MAX_PATH];
        wcsncpy_s(targetPath, MAX_PATH, reparse->MountPointReparseBuffer.PathBuffer + nameOffset, nameLength);
        targetPath[nameLength] = L'\0';
        
        // 转换为 UTF-8
        char targetUtf8[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, targetPath, -1, targetUtf8, MAX_PATH, NULL, NULL);
        
        char* finalPath = targetUtf8;
        if (strncmp(targetUtf8, "\\??\\", 4) == 0) {
            finalPath = targetUtf8 + 4;
        }
        
        return XString_create_utf8(finalPath);
    }
    
    return XString_create();
}

XString* XFileInfo_readSymLink(const XFileInfo* info)
{
    // 与 symLinkTarget 类似，但返回原始路径（不一定是绝对路径）
    return XFileInfo_symLinkTarget(info);
}

XString* XFileInfo_junctionTarget(const XFileInfo* info)
{
    if (!info) return XString_create();
    
    if (!XFileInfo_isJunction(info)) {
        return XString_create();
    }
    
    return XFileInfo_symLinkTarget(info);
}

/* ============================================================================
 * 比较操作
 * ============================================================================ */

bool XFileInfo_equals(const XFileInfo* lhs, const XFileInfo* rhs)
{
    if (!lhs || !rhs) return false;
    if (lhs == rhs) return true;
    
    // 获取规范路径比较
    XString* path1 = XFileInfo_canonicalFilePath(lhs);
    XString* path2 = XFileInfo_canonicalFilePath(rhs);
    
    if (!path1 || !path2) {
        if (path1) XString_delete_base(path1);
        if (path2) XString_delete_base(path2);
        return false;
    }
    
    const char* p1 = XString_toUtf8(path1);
    const char* p2 = XString_toUtf8(path2);
    
    // Windows 路径不区分大小写
    bool result = (_stricmp(p1, p2) == 0);
    
    XString_delete_base(path1);
    XString_delete_base(path2);
    
    return result;
}

#endif // _WIN32
