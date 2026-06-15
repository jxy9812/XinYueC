#ifdef _WIN32
#include "XDir.h"
#include "XMemory.h"
#include <windows.h>
#include <direct.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>

/* ============================================================================
 * XFileInfo 列表辅助函数
 * ============================================================================ */

// 用于排序 XFileInfo 的信息结构
typedef struct {
    XFileInfo info;      // 直接存储 XFileInfo 结构体
    bool isDir;
    int64_t size;
    int64_t time;
} XDirInfoEntry;

// 比较函数（用于 XFileInfo 列表排序）
static int compareInfoEntry(const XDirInfoEntry* a, const XDirInfoEntry* b, XDirSortFlags flags)
{
    int result = 0;
    
    // DirsFirst 或 DirsLast 处理
    if ((flags & XDir_DirsFirst) || (flags & XDir_DirsLast)) {
        if (a->isDir && !b->isDir) {
            return (flags & XDir_DirsFirst) ? -1 : 1;
        }
        if (!a->isDir && b->isDir) {
            return (flags & XDir_DirsFirst) ? 1 : -1;
        }
    }
    
        // 获取文件名用于排序
    XString* nameA = XFileInfo_fileName(&a->info);
    XString* nameB = XFileInfo_fileName(&b->info);
    const char* strA = XString_toUtf8(nameA);
    const char* strB = XString_toUtf8(nameB);
    
    bool ignoreCase = (flags & XDir_IgnoreCase) != 0;
    bool localeAware = (flags & XDir_LocaleAware) != 0;
    
    if (flags & XDir_Type) {
        // 按类型（扩展名）排序
        const char* extA = strrchr(strA, '.');
        const char* extB = strrchr(strB, '.');
        if (!extA) extA = "";
        if (!extB) extB = "";
        
        if (localeAware) {
            result = XDir_localeCompare(extA, extB, ignoreCase);
        } else if (ignoreCase) {
            result = _stricmp(extA, extB);
        } else {
            result = strcmp(extA, extB);
        }
        
        if (result == 0) {
            if (localeAware) {
                result = XDir_localeCompare(strA, strB, ignoreCase);
            } else if (ignoreCase) {
                result = _stricmp(strA, strB);
            } else {
                result = strcmp(strA, strB);
            }
        }
    } else {
        switch (flags & XDir_SortByMask) {
            case XDir_Time:
                if (a->time < b->time) result = -1;
                else if (a->time > b->time) result = 1;
                else result = 0;
                break;
            case XDir_Size:
                if (a->size < b->size) result = -1;
                else if (a->size > b->size) result = 1;
                else result = 0;
                break;
            case XDir_Name:
            case XDir_Unsorted:
            default:
                if (localeAware) {
                    result = XDir_localeCompare(strA, strB, ignoreCase);
                } else if (ignoreCase) {
                    result = _stricmp(strA, strB);
                } else {
                    result = strcmp(strA, strB);
                }
                break;
        }
    }
    
    XString_delete_base(nameA);
    XString_delete_base(nameB);
    
    // 反向处理
    if (flags & XDir_Reversed) {
        result = -result;
    }
    
    return result;
}

// 快速排序实现
static void sortInfoEntries(XDirInfoEntry* entries, size_t count, XDirSortFlags flags)
{
    if (count <= 1) return;
    
    // 简单插入排序（对于小数组效率可以）
    for (size_t i = 1; i < count; i++) {
        XDirInfoEntry key = entries[i];
        int64_t j = (int64_t)i - 1;
        
        while (j >= 0 && compareInfoEntry(&entries[j], &key, flags) > 0) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }
}

/* ============================================================================
 * 本地化比较 - Windows 实现
 * ============================================================================ */

int XDir_localeCompare(const char* str1, const char* str2, bool ignoreCase)
{
    if (!str1 || !str2) {
        if (!str1 && !str2) return 0;
        return str1 ? 1 : -1;
    }
    
    if (ignoreCase) {
        // 使用 Windows API CompareStringA 进行本地化比较
        // LOCALE_USER_DEFAULT 使用用户默认区域设置
        // NORM_IGNORECASE 忽略大小写
        int result = CompareStringA(
            LOCALE_USER_DEFAULT,
            NORM_IGNORECASE,
            str1, -1,
            str2, -1
        );
        
        // CompareStringA 返回值：
        // CSTR_LESS_THAN (1) = str1 < str2
        // CSTR_EQUAL (2) = str1 == str2  
        // CSTR_GREATER_THAN (3) = str1 > str2
        // 0 表示错误
        
        switch (result) {
            case CSTR_LESS_THAN: return -1;
            case CSTR_EQUAL: return 0;
            case CSTR_GREATER_THAN: return 1;
            default:
                // 失败时回退到简单比较
                return _stricmp(str1, str2);
        }
    } else {
        // 区分大小写的本地化比较
        int result = CompareStringA(
            LOCALE_USER_DEFAULT,
            0,  // 不设置特殊标志
            str1, -1,
            str2, -1
        );
        
        switch (result) {
            case CSTR_LESS_THAN: return -1;
            case CSTR_EQUAL: return 0;
            case CSTR_GREATER_THAN: return 1;
            default:
                // 失败时回退到简单比较
                return strcmp(str1, str2);
        }
    }
}

/* ============================================================================
 * 路径操作 - Windows 实现
 * ============================================================================ */

XString* XDir_absolutePath(const XDir* dir)
{
    if (!dir || !dir->m_path) return NULL;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return NULL;

    char absPath[MAX_PATH];
    if (_fullpath(absPath, pathUtf8, MAX_PATH)) {
        return XString_create_utf8(absPath);
    }
    return NULL;
}

XString* XDir_canonicalPath(const XDir* dir)
{
    if (!dir || !dir->m_path) return NULL;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return NULL;

    char absPath[MAX_PATH];
    char* result = _fullpath(absPath, pathUtf8, MAX_PATH);
    if (result) {
        // 验证路径是否存在
        DWORD attrs = GetFileAttributesA(result);
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            return XString_create_utf8(result);
        }
    }
    return NULL;
}

/* ============================================================================
 * 状态检查 - Windows 实现
 * ============================================================================ */

bool XDir_exists_1(const XDir* dir)
{
    if (!dir || !dir->m_path) return false;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return false;

    DWORD attrs = GetFileAttributesA(pathUtf8);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

bool XDir_exists_2(const XDir* dir, const XString* name)
{
    // TODO: 依赖 XFile 实现
    // 暂时使用系统 API
    if (!dir || !dir->m_path || !name) return false;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    const char* nameUtf8 = XString_toUtf8(name);

        size_t len = strlen(pathUtf8) + 1 + strlen(nameUtf8) + 1;
    char* fullPath = (char*)XMalloc_System(len);
    if (!fullPath) return false;

    snprintf(fullPath, len, "%s\\%s", pathUtf8, nameUtf8);
    DWORD attrs = GetFileAttributesA(fullPath);
    XFree_System(fullPath);

    return (attrs != INVALID_FILE_ATTRIBUTES);
}

bool XDir_isReadable(const XDir* dir)
{
    if (!dir || !dir->m_path) return false;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return false;

    // 尝试打开目录来检查可读性
    HANDLE hDir = CreateFileA(
        pathUtf8,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        return false;
    }

    CloseHandle(hDir);
    return true;
}

bool XDir_isAbsolute(const XDir* dir)
{
    if (!dir || !dir->m_path) return false;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return false;

    // Windows 绝对路径格式：
    // 1. "C:\path" - 驱动器绝对路径
    // 2. "\path" - 从当前驱动器根目录开始的绝对路径
    // 3. "\\" - UNC 路径

    if (pathUtf8[0] == '\\' && pathUtf8[1] == '\\') {
        return true;  // UNC 路径
    }
    if (pathUtf8[0] == '\\') {
        return true;  // 从当前驱动器根目录开始
    }
    if (isalpha((unsigned char)pathUtf8[0]) && pathUtf8[1] == ':') {
        return true;  // 驱动器路径
    }
    return false;
}

bool XDir_isRoot(const XDir* dir)
{
    if (!dir || !dir->m_path) return false;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return false;

    char absPath[MAX_PATH];
    if (!_fullpath(absPath, pathUtf8, MAX_PATH)) {
        return false;
    }

    size_t len = strlen(absPath);

    // 驱动器根目录 "C:\"
    if (len == 3 && isalpha((unsigned char)absPath[0]) &&
        absPath[1] == ':' && absPath[2] == '\\') {
        return true;
    }

    // UNC 路径检查 "\\server\share\"
    if (absPath[0] == '\\' && absPath[1] == '\\') {
        // 找到服务器名后的反斜杠
        const char* p = absPath + 2;
        while (*p && *p != '\\') p++;
        if (*p == '\\') {
            p++;
            // 找到共享名后的反斜杠
            while (*p && *p != '\\') p++;
            // 如果路径到此结束，说明是 UNC 根目录
            if (*p == '\\' && *(p + 1) == '\0') {
                return true;
            }
            if (*p == '\0') {
                return true;  // "\\server\share" 也是根目录
            }
        }
    }

    return false;
}

/* ============================================================================
 * 目录导航 - Windows 实现
 * ============================================================================ */

bool XDir_cd(XDir* dir, const XString* dirName)
{
    if (!dir || !dir->m_path || !dirName) return false;

    const char* dirNameUtf8 = XString_toUtf8(dirName);
    if (!dirNameUtf8) return false;

    // 处理 ".."
    if (strcmp(dirNameUtf8, "..") == 0) {
        return XDir_cdUp(dir);
    }

    // 构建新路径
    XString* newPath = XDir_filePath(dir, dirName);
    if (!newPath) return false;

    // 检查目录是否存在
    const char* newPathUtf8 = XString_toUtf8(newPath);
    DWORD attrs = GetFileAttributesA(newPathUtf8);

    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        XString_delete_base(newPath);
        return false;
    }

    XString_delete_base(dir->m_path);
    dir->m_path = newPath;
    return true;
}

bool XDir_cdUp(XDir* dir)
{
    if (!dir || !dir->m_path) return false;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return false;

    char absPath[MAX_PATH];
    if (!_fullpath(absPath, pathUtf8, MAX_PATH)) {
        return false;
    }

    // 找到最后一个反斜杠（跳过驱动器部分）
    char* lastSlash = strrchr(absPath, '\\');
    if (!lastSlash) return false;

    // 如果是根目录，无法向上
    if (lastSlash == absPath + 2 && absPath[1] == ':') {
        return false;  // "C:\" 无法向上
    }

    // 截断路径
    *lastSlash = '\0';

    XString_delete_base(dir->m_path);
    dir->m_path = XString_create_utf8(absPath);
    return true;
}

/* ============================================================================
 * 目录操作 - Windows 实现
 * ============================================================================ */

bool XDir_mkdir(XDir* dir, const XString* dirName)
{
    if (!dir || !dir->m_path || !dirName) return false;

    XString* fullPath = XDir_filePath(dir, dirName);
    if (!fullPath) return false;

    const char* pathUtf8 = XString_toUtf8(fullPath);
    BOOL result = CreateDirectoryA(pathUtf8, NULL);

    XString_delete_base(fullPath);
    return result != 0;
}

bool XDir_mkpath(XDir* dir, const XString* dirPath)
{
    if (!dir || !dir->m_path || !dirPath) return false;

    XString* fullPath = XDir_filePath(dir, dirPath);
    if (!fullPath) return false;

    const char* pathUtf8 = XString_toUtf8(fullPath);

    // 递归创建目录
    char* p = (char*)pathUtf8;
    while (*p) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = '\0';

            DWORD attrs = GetFileAttributesA(pathUtf8);
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                CreateDirectoryA(pathUtf8, NULL);
            }

            *p = saved;
        }
        p++;
    }

    // 创建最终目录
    DWORD attrs = GetFileAttributesA(pathUtf8);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryA(pathUtf8, NULL);
    }

    XString_delete_base(fullPath);
    return (GetFileAttributesA(pathUtf8) != INVALID_FILE_ATTRIBUTES);
}

bool XDir_rmdir(XDir* dir, const XString* dirName)
{
    if (!dir || !dir->m_path || !dirName) return false;

    XString* fullPath = XDir_filePath(dir, dirName);
    if (!fullPath) return false;

    const char* pathUtf8 = XString_toUtf8(fullPath);
    BOOL result = RemoveDirectoryA(pathUtf8);

    XString_delete_base(fullPath);
    return result != 0;
}

bool XDir_rmpath(XDir* dir, const XString* dirPath)
{
    if (!dir || !dir->m_path || !dirPath) return false;

    XString* fullPath = XDir_filePath(dir, dirPath);
    if (!fullPath) return false;

    const char* pathUtf8 = XString_toUtf8(fullPath);

    // 从最深目录开始删除
    while (strlen(pathUtf8) > 3) {  // 保留 "C:\"
        DWORD attrs = GetFileAttributesA(pathUtf8);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            if (!RemoveDirectoryA(pathUtf8)) {
                break;  // 目录非空或无法删除
            }
        }

        // 找到上一个目录
        char* lastSlash = strrchr((char*)pathUtf8, '\\');
        if (!lastSlash || lastSlash - pathUtf8 <= 2) break;
        *lastSlash = '\0';
    }

    XString_delete_base(fullPath);
    return true;
}

bool XDir_removeRecursively(XDir* dir)
{
    if (!dir || !dir->m_path) return false;
    
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return false;
    
    // 构建搜索路径
    size_t len = strlen(pathUtf8) + 3;  // "\*"
    char* searchPath = (char*)XMalloc_System(len);
    if (!searchPath) return false;
    
    snprintf(searchPath, len, "%s\\*", pathUtf8);
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    XFree_System(searchPath);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    bool success = true;
    
    do {
        const char* name = findData.cFileName;
        
        // 跳过 "." 和 ".."
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        
        // 构建完整路径
        size_t itemLen = strlen(pathUtf8) + 1 + strlen(name) + 1;
        char* itemPath = (char*)XMalloc_System(itemLen);
        if (!itemPath) {
            success = false;
            continue;
        }
        
        snprintf(itemPath, itemLen, "%s\\%s", pathUtf8, name);
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 递归删除子目录
            XString* subPath = XString_create_utf8(itemPath);
            XDir* subDir = XDir_create_2(subPath);
            XString_delete_base(subPath);
            
            if (subDir) {
                if (!XDir_removeRecursively(subDir)) {
                    success = false;
                }
                XClass_delete_base((XClass*)subDir);
            } else {
                success = false;
            }
        } else {
            // 删除文件
            if (!DeleteFileA(itemPath)) {
                success = false;
            }
        }
        
        XFree_System(itemPath);
        
    } while (FindNextFileA(hFind, &findData));
    
    FindClose(hFind);
    
    // 删除目录本身
    if (success) {
        if (!RemoveDirectoryA(pathUtf8)) {
            success = false;
        }
    }
    
    return success;
}

bool XDir_remove(XDir* dir, const XString* fileName)
{
    // TODO: 依赖 XFile 实现
    if (!dir || !dir->m_path || !fileName) return false;

    XString* fullPath = XDir_filePath(dir, fileName);
    if (!fullPath) return false;

    const char* pathUtf8 = XString_toUtf8(fullPath);
    BOOL result = DeleteFileA(pathUtf8);

    XString_delete_base(fullPath);
    return result != 0;
}

bool XDir_rename(XDir* dir, const XString* oldName, const XString* newName)
{
    // TODO: 依赖 XFile 实现
    if (!dir || !dir->m_path || !oldName || !newName) return false;

    XString* oldPath = XDir_filePath(dir, oldName);
    XString* newPath = XDir_filePath(dir, newName);

    if (!oldPath || !newPath) {
        if (oldPath) XString_delete_base(oldPath);
        if (newPath) XString_delete_base(newPath);
        return false;
    }

    const char* oldUtf8 = XString_toUtf8(oldPath);
    const char* newUtf8 = XString_toUtf8(newPath);

    BOOL result = MoveFileA(oldUtf8, newUtf8);

    XString_delete_base(oldPath);
    XString_delete_base(newPath);
    return result != 0;
}

/* ============================================================================
 * 目录内容 - Windows 实现
 * ============================================================================ */

XStringList* XDir_entryList_2(const XDir* dir, const XStringList* nameFilters,
    XDirFilters filters, XDirSortFlags sort)
{
    if (!dir || !dir->m_path) return NULL;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return NULL;

    XDirFilters actualFilters = (filters == XDir_NoFilter) ? dir->m_filters : filters;
    XDirSortFlags actualSort = (sort == XDir_NoSort) ? dir->m_sorting : sort;

    if (actualFilters == XDir_NoFilter) {
        actualFilters = XDir_AllEntries;
    }
    
    // 检查缓存是否有效
    // 注意：entryList_2 使用传入的 nameFilters，而 entryList_1 使用 dir->m_nameFilters
    // 所以只有当 nameFilters == dir->m_nameFilters 时才能使用缓存
    bool canUseCache = dir->m_cacheValid && dir->m_cachedEntries &&
                       dir->m_cachedFilters == actualFilters &&
                       dir->m_cachedSorting == actualSort;
    
    // 检查 nameFilters 是否匹配
    if (canUseCache && nameFilters && dir->m_nameFilters) {
        // 比较两个 nameFilters 列表
        size_t nfCount = XStringList_size_base(nameFilters);
        size_t cachedNfCount = XStringList_size_base(dir->m_nameFilters);
        if (nfCount != cachedNfCount) {
            canUseCache = false;
        } else {
            for (size_t i = 0; i < nfCount && canUseCache; i++) {
                const XString* nf1 = XStringList_at_base(nameFilters, i);
                const XString* nf2 = XStringList_at_base(dir->m_nameFilters, i);
                const char* s1 = XString_toUtf8((XString*)nf1);
                const char* s2 = XString_toUtf8((XString*)nf2);
                if (!s1 || !s2 || strcmp(s1, s2) != 0) {
                    canUseCache = false;
                }
            }
        }
    } else if (canUseCache) {
        // nameFilters 为空或 dir->m_nameFilters 为空
        bool nfEmpty = (!nameFilters || XStringList_size_base(nameFilters) == 0);
        bool cachedNfEmpty = (!dir->m_nameFilters || XStringList_size_base(dir->m_nameFilters) == 0);
        if (nfEmpty != cachedNfEmpty) {
            canUseCache = false;
        }
    }
    
    if (canUseCache) {
        // 返回缓存的副本
        return XStringList_create_copy(dir->m_cachedEntries);
    }

    // 构建搜索路径
    size_t len = strlen(pathUtf8) + 3;  // "\\*"
    char* searchPath = (char*)XMalloc_System(len);
    if (!searchPath) return NULL;

    snprintf(searchPath, len, "%s\\*", pathUtf8);

    XStringList* result = XStringList_create();

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    XFree_System(searchPath);

    if (hFind == INVALID_HANDLE_VALUE) {
        return result;
    }

    // 用于排序的临时数组
    size_t capacity = 256;
    int64_t* sizes = (int64_t*)XMalloc_System(capacity * sizeof(int64_t));
    int64_t* times = (int64_t*)XMalloc_System(capacity * sizeof(int64_t));
    bool* isDirs = (bool*)XMalloc_System(capacity * sizeof(bool));
    
    if (!sizes || !times || !isDirs) {
        if (sizes) XFree_System(sizes);
        if (times) XFree_System(times);
        if (isDirs) XFree_System(isDirs);
        FindClose(hFind);
        return result;
    }

    do {
        const char* name = findData.cFileName;

        // 过滤 "." 和 ".."
        if (strcmp(name, ".") == 0) {
            if (actualFilters & XDir_NoDot) continue;
            if (!(actualFilters & XDir_AllDirs) && !(actualFilters & XDir_Dirs)) continue;
        }
        if (strcmp(name, "..") == 0) {
            if (actualFilters & XDir_NoDotDot) continue;
            if (!(actualFilters & XDir_AllDirs) && !(actualFilters & XDir_Dirs)) continue;
        }

                // 应用类型过滤器
        bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool isFile = !isDir;
        bool isHidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        bool isSystem = (findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        bool isSymLink = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        
        // 应用符号链接过滤器
        if (isSymLink && (actualFilters & XDir_NoSymLinks)) {
            continue;
        }

        if (isDir && (actualFilters & XDir_AllDirs)) {
            // AllDirs - 包含所有目录
        }
        else if (isDir && (actualFilters & XDir_Dirs)) {
            // Dirs - 包含目录
        }
        else if (isFile && (actualFilters & XDir_Files)) {
            // Files - 包含文件
        }
        else if (!(actualFilters & (XDir_Dirs | XDir_Files | XDir_AllDirs))) {
            continue;
        }

                // 应用隐藏和系统过滤器
        if (isHidden && !(actualFilters & XDir_Hidden)) continue;
        if (isSystem && !(actualFilters & XDir_System)) continue;
        
        // 应用权限过滤器
        if (actualFilters & (XDir_Readable | XDir_Writable | XDir_Executable)) {
            // 构建完整路径检查权限
            size_t checkLen = strlen(pathUtf8) + 1 + strlen(name) + 1;
            char* checkPath = (char*)XMalloc_System(checkLen);
            if (checkPath) {
                snprintf(checkPath, checkLen, "%s\\%s", pathUtf8, name);
                
                // 检查可读性
                if (actualFilters & XDir_Readable) {
                    DWORD attrs = GetFileAttributesA(checkPath);
                    if (attrs == INVALID_FILE_ATTRIBUTES) {
                        XFree_System(checkPath);
                        continue;
                    }
                    // Windows 没有直接的"可读"权限，检查是否可以打开
                    HANDLE hFile = CreateFileA(checkPath, GENERIC_READ, 
                                               FILE_SHARE_READ, NULL, 
                                               OPEN_EXISTING, 0, NULL);
                    if (hFile == INVALID_HANDLE_VALUE) {
                        XFree_System(checkPath);
                        continue;
                    }
                    CloseHandle(hFile);
                }
                
                // 检查可写性
                if (actualFilters & XDir_Writable) {
                    HANDLE hFile = CreateFileA(checkPath, GENERIC_WRITE, 
                                               FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                               NULL, OPEN_EXISTING, 0, NULL);
                    if (hFile == INVALID_HANDLE_VALUE) {
                        XFree_System(checkPath);
                        continue;
                    }
                    CloseHandle(hFile);
                }
                
                // 检查可执行性（Windows 上检查扩展名）
                if ((actualFilters & XDir_Executable) && !isDir) {
                    const char* ext = strrchr(name, '.');
                    if (!ext) {
                        XFree_System(checkPath);
                        continue;  // 无扩展名
                    }
                    // 检查是否为可执行文件扩展名
                    if (_stricmp(ext, ".exe") != 0 && 
                        _stricmp(ext, ".bat") != 0 && 
                        _stricmp(ext, ".cmd") != 0 && 
                        _stricmp(ext, ".com") != 0) {
                        XFree_System(checkPath);
                        continue;
                    }
                }
                
                XFree_System(checkPath);
            }
        }

                // 应用名称过滤器
        if (nameFilters && XStringList_size_base(nameFilters) > 0) {
            XString* nameStr = XString_create_utf8(name);
            // CaseSensitive 过滤器影响名称匹配
            bool matchResult;
            if (actualFilters & XDir_CaseSensitive) {
                // 区分大小写的匹配
                matchResult = false;
                for (size_t fi = 0; fi < XStringList_size_base(nameFilters); fi++) {
                    const XString* filter = XStringList_at_base(nameFilters, fi);
                    const char* filterUtf8 = XString_toUtf8((XString*)filter);
                    // 使用区分大小写的通配符匹配
                    extern bool matchWildcardCaseSensitive(const char* pattern, const char* str);
                    if (matchWildcardCaseSensitive(filterUtf8, name)) {
                        matchResult = true;
                        break;
                    }
                }
            } else {
                matchResult = XDir_match_2(nameFilters, nameStr);
            }
            if (!matchResult) {
                XString_delete_base(nameStr);
                continue;
            }
            XString_delete_base(nameStr);
        }

        // 检查是否需要扩容
        size_t count = XStringList_size_base(result);
        if (count >= capacity) {
            capacity *= 2;
            int64_t* newSizes = (int64_t*)XMalloc_System(capacity * sizeof(int64_t));
            int64_t* newTimes = (int64_t*)XMalloc_System(capacity * sizeof(int64_t));
            bool* newIsDirs = (bool*)XMalloc_System(capacity * sizeof(bool));
            
            if (!newSizes || !newTimes || !newIsDirs) {
                if (newSizes) XFree_System(newSizes);
                if (newTimes) XFree_System(newTimes);
                if (newIsDirs) XFree_System(newIsDirs);
                break;
            }
            
            memcpy(newSizes, sizes, count * sizeof(int64_t));
            memcpy(newTimes, times, count * sizeof(int64_t));
            memcpy(newIsDirs, isDirs, count * sizeof(bool));
            
            XFree_System(sizes);
            XFree_System(times);
            XFree_System(isDirs);
            
            sizes = newSizes;
            times = newTimes;
            isDirs = newIsDirs;
        }
        
        // 保存条目信息用于排序
        sizes[count] = ((int64_t)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;
        // 将 FILETIME 转换为 Unix 时间戳
        ULARGE_INTEGER ul;
        ul.LowPart = findData.ftLastWriteTime.dwLowDateTime;
        ul.HighPart = findData.ftLastWriteTime.dwHighDateTime;
        times[count] = (int64_t)((ul.QuadPart - 116444736000000000LL) / 10000000);
        isDirs[count] = isDir;
        
        XStringList_push_back_utf8(result, name);

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

        // 调用排序函数
    XDir_sortEntryList(result, actualSort, sizes, times, isDirs);
    
    XFree_System(sizes);
    XFree_System(times);
    XFree_System(isDirs);

    // 更新缓存（只有使用默认 nameFilters 时才缓存）
    // 判断是否使用的是 dir->m_nameFilters
    bool shouldCache = (filters == XDir_NoFilter || filters == dir->m_filters) &&
                       (sort == XDir_NoSort || sort == dir->m_sorting);
    
    // 检查 nameFilters 是否与 dir->m_nameFilters 相同
    if (shouldCache && nameFilters && dir->m_nameFilters) {
        size_t nfCount = XStringList_size_base(nameFilters);
        size_t dirNfCount = XStringList_size_base(dir->m_nameFilters);
        if (nfCount != dirNfCount) {
            shouldCache = false;
        } else {
            for (size_t i = 0; i < nfCount && shouldCache; i++) {
                const XString* nf1 = XStringList_at_base(nameFilters, i);
                const XString* nf2 = XStringList_at_base(dir->m_nameFilters, i);
                const char* s1 = XString_toUtf8((XString*)nf1);
                const char* s2 = XString_toUtf8((XString*)nf2);
                if (!s1 || !s2 || strcmp(s1, s2) != 0) {
                    shouldCache = false;
                }
            }
        }
    } else if (shouldCache) {
        bool nfEmpty = (!nameFilters || XStringList_size_base(nameFilters) == 0);
        bool dirNfEmpty = (!dir->m_nameFilters || XStringList_size_base(dir->m_nameFilters) == 0);
        if (nfEmpty != dirNfEmpty) {
            shouldCache = false;
        }
    }
    
    if (shouldCache) {
        XDir* mutableDir = (XDir*)dir;
        
        // 释放旧的缓存
        if (mutableDir->m_cachedEntries) {
            XStringList_delete_base(mutableDir->m_cachedEntries);
        }
        
        // 保存新的缓存
        mutableDir->m_cachedEntries = XStringList_create_copy(result);
        mutableDir->m_cachedFilters = actualFilters;
        mutableDir->m_cachedSorting = actualSort;
        mutableDir->m_cacheValid = true;
    }

    return result;
}

/* ============================================================================
 * 静态函数 - Windows 实现
 * ============================================================================ */

bool XDir_isAbsolutePath(const XString* path)
{
    if (!path) return false;

    const char* pathUtf8 = XString_toUtf8(path);
    if (!pathUtf8) return false;

    if (pathUtf8[0] == '\\' && pathUtf8[1] == '\\') {
        return true;  // UNC 路径
    }
    if (pathUtf8[0] == '\\') {
        return true;  // 从当前驱动器根目录开始
    }
    if (isalpha((unsigned char)pathUtf8[0]) && pathUtf8[1] == ':') {
        return true;  // 驱动器路径
    }
    return false;
}

XDir* XDir_current(void)
{
    char buffer[MAX_PATH];
    if (getcwd(buffer, MAX_PATH)) {
        XString* path = XString_create_utf8(buffer);
        XDir* dir = XDir_create_2(path);
        XString_delete_base(path);
        return dir;
    }
    return NULL;
}

XString* XDir_currentPath(void)
{
    char buffer[MAX_PATH];
    if (getcwd(buffer, MAX_PATH)) {
        return XString_create_utf8(buffer);
    }
    return NULL;
}

bool XDir_setCurrent(const XString* path)
{
    if (!path) return false;

    const char* pathUtf8 = XString_toUtf8(path);
    if (!pathUtf8) return false;

    return _chdir(pathUtf8) == 0;
}

XString* XDir_homePath(void)
{
    char buffer[MAX_PATH];

    // 尝试获取用户配置文件路径
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, buffer) == S_OK) {
        return XString_create_utf8(buffer);
    }

    // 备选方案：使用环境变量
    const char* home = getenv("USERPROFILE");
    if (home) {
        return XString_create_utf8(home);
    }

    // 再尝试 HOMEDRIVE + HOMEPATH
    const char* homeDrive = getenv("HOMEDRIVE");
    const char* homePath = getenv("HOMEPATH");
        if (homeDrive && homePath) {
        size_t len = strlen(homeDrive) + strlen(homePath) + 1;
        char* fullPath = (char*)XMalloc_System(len);
        if (fullPath) {
            snprintf(fullPath, len, "%s%s", homeDrive, homePath);
            XString* result = XString_create_utf8(fullPath);
            XFree_System(fullPath);
            return result;
        }
    }

    return NULL;
}

XString* XDir_rootPath(void)
{
    // Windows 根目录通常是系统驱动器的根目录
    char buffer[MAX_PATH];
    if (GetSystemDirectoryA(buffer, MAX_PATH)) {
        // 获取系统目录所在的驱动器根目录
        buffer[3] = '\0';  // 截取 "C:\"
        return XString_create_utf8(buffer);
    }

    // 默认返回 C:
    return XString_create_utf8("C:\\");
}

XString* XDir_tempPath(void)
{
    char buffer[MAX_PATH];
    if (GetTempPathA(MAX_PATH, buffer)) {
        return XString_create_utf8(buffer);
    }

    // 备选方案：使用环境变量
    const char* temp = getenv("TEMP");
    if (temp) {
        return XString_create_utf8(temp);
    }

    temp = getenv("TMP");
    if (temp) {
        return XString_create_utf8(temp);
    }

    return NULL;
}

XStringList* XDir_drives(void)
{
    XStringList* result = XStringList_create();

    DWORD driveMask = GetLogicalDrives();
    char drivePath[4] = "A:\\";

    for (char letter = 'A'; letter <= 'Z'; letter++) {
        if (driveMask & 1) {
            drivePath[0] = letter;
            XStringList_push_back_utf8(result, drivePath);
        }
        driveMask >>= 1;
    }

    return result;
}

/* ============================================================================
 * 相对路径计算 - Windows 实现
 * ============================================================================ */

// 辅助函数：分割路径为组件
static int splitPath(const char* path, char** parts, int maxParts)
{
    char* dup = XStrdup(path);
    if (!dup) return 0;
    
    int count = 0;
    char* p = dup;
    
    // 跳过驱动器前缀
    if (isalpha((unsigned char)p[0]) && p[1] == ':') {
        p += 2;
    }
    
    // 跳过 UNC 前缀
    if (p[0] == '\\' && p[1] == '\\') {
        p += 2;
        // 跳过服务器名
        while (*p && *p != '\\') p++;
        if (*p == '\\') p++;
        // 跳过共享名
        while (*p && *p != '\\') p++;
    }
    
    // 跳过根目录斜杠
    while (*p == '\\') p++;
    
    // 分割路径
    char* start = p;
    while (*p && count < maxParts) {
        if (*p == '\\') {
            *p = '\0';
            if (strlen(start) > 0) {
                parts[count++] = XStrdup(start);
            }
            p++;
            while (*p == '\\') p++;  // 跳过连续斜杠
            start = p;
        } else {
            p++;
        }
    }
    
    // 最后一部分
    if (*start && count < maxParts) {
        parts[count++] = XStrdup(start);
    }
    
    XFree_System(dup);
    return count;
}

// 辅助函数：释放分割的路径组件
static void freePathParts(char** parts, int count)
{
    for (int i = 0; i < count; i++) {
        if (parts[i]) {
            XFree_System(parts[i]);
            parts[i] = NULL;
        }
    }
}

// 辅助函数：检查两个路径是否在同一驱动器
static bool sameDrive(const char* path1, const char* path2)
{
    char drive1[3] = {0};
    char drive2[3] = {0};
    
    if (isalpha((unsigned char)path1[0]) && path1[1] == ':') {
        drive1[0] = toupper((unsigned char)path1[0]);
        drive1[1] = ':';
    }
    if (isalpha((unsigned char)path2[0]) && path2[1] == ':') {
        drive2[0] = toupper((unsigned char)path2[0]);
        drive2[1] = ':';
    }
    
    // 如果都是 UNC 路径，比较服务器和共享名
    if (path1[0] == '\\' && path1[1] == '\\' &&
        path2[0] == '\\' && path2[1] == '\\') {
        // 简化处理：假设 UNC 路径相同
        return true;
    }
    
    // 如果都没有驱动器（相对路径）
    if (drive1[0] == '\0' && drive2[0] == '\0') {
        return true;
    }
    
    return strcmp(drive1, drive2) == 0;
}

XString* XDir_relativeFilePath(const XDir* dir, const XString* fileName)
{
    if (!dir || !dir->m_path || !fileName) return NULL;

    const char* basePath = XString_toUtf8(dir->m_path);
    const char* targetPath = XString_toUtf8(fileName);

    if (!basePath || !targetPath) return NULL;

    // 获取绝对路径
    char absBase[MAX_PATH];
    char absTarget[MAX_PATH];

    if (!_fullpath(absBase, basePath, MAX_PATH)) return NULL;
    if (!_fullpath(absTarget, targetPath, MAX_PATH)) return NULL;

    // 如果不在同一驱动器，返回绝对路径
    if (!sameDrive(absBase, absTarget)) {
        return XString_create_utf8(absTarget);
    }

    // 分割路径
    #define MAX_PATH_PARTS 64
    char* baseParts[MAX_PATH_PARTS] = {0};
    char* targetParts[MAX_PATH_PARTS] = {0};
    
    int baseCount = splitPath(absBase, baseParts, MAX_PATH_PARTS);
    int targetCount = splitPath(absTarget, targetParts, MAX_PATH_PARTS);
    
    // 找到公共前缀
    int commonLen = 0;
    while (commonLen < baseCount && commonLen < targetCount) {
        if (_stricmp(baseParts[commonLen], targetParts[commonLen]) == 0) {
            commonLen++;
        } else {
            break;
        }
    }
    
    // 构建相对路径
    XString* result = XString_create_utf8("");
    
    // 添加 ".." 返回到公共祖先
    for (int i = commonLen; i < baseCount; i++) {
        if (XString_length_base(result) > 0) {
            XString_append_utf8(result, "/");
        }
        XString_append_utf8(result, "..");
    }
    
    // 添加目标路径的剩余部分
    for (int i = commonLen; i < targetCount; i++) {
        if (XString_length_base(result) > 0) {
            XString_append_utf8(result, "/");
        }
        XString_append_utf8(result, targetParts[i]);
    }
    
    // 如果结果为空，返回 "."
    if (XString_length_base(result) == 0) {
        XString_append_utf8(result, ".");
    }
    
    // 释放分割的路径组件
    freePathParts(baseParts, baseCount);
    freePathParts(targetParts, targetCount);
    
        return result;
}

/* ============================================================================
 * XFileInfo 列表 - Windows 实现
 * ============================================================================ */

XVector* XDir_entryInfoList_1(const XDir* dir, XDirFilters filters, XDirSortFlags sort)
{
    return XDir_entryInfoList_2(dir, dir->m_nameFilters, filters, sort);
}

XVector* XDir_entryInfoList_2(const XDir* dir, const XStringList* nameFilters,
                               XDirFilters filters, XDirSortFlags sort)
{
    if (!dir || !dir->m_path) return NULL;

    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return NULL;

    XDirFilters actualFilters = (filters == XDir_NoFilter) ? dir->m_filters : filters;
    XDirSortFlags actualSort = (sort == XDir_NoSort) ? dir->m_sorting : sort;

    if (actualFilters == XDir_NoFilter) {
        actualFilters = XDir_AllEntries;
    }

    // 构建搜索路径
    size_t len = strlen(pathUtf8) + 3;
    char* searchPath = (char*)XMalloc_System(len);
    if (!searchPath) return NULL;

    snprintf(searchPath, len, "%s\\*", pathUtf8);

    // 创建存储 XFileInfo* 的 Vector
        // 创建存储 XFileInfo 的 Vector（直接存储结构体）
        XVector* result = XVector_create(sizeof(XFileInfo));
        if (!result) {
            XFree_System(searchPath);
            return NULL;
        }
    
        // 设置元素释放方法，自动释放 XFileInfo
        XContainerSetDataDeinitMethod(result, (XCDataDeinitMethod)XFileInfo_deinit_base);
        // 设置元素拷贝方法，实现深拷贝
        XContainerSetDataCopyMethod(result, (XCDataCopyMethod)XFileInfo_copy_base);
        // 设置元素移动方法
        XContainerSetDataMoveMethod(result, (XCDataMoveMethod)XFileInfo_move_base);
       // return NULL;
    //}

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    XFree_System(searchPath);

    if (hFind == INVALID_HANDLE_VALUE) {
        return result;
    }

    // 用于排序的临时数组
    size_t capacity = 256;
    XDirInfoEntry* entries = (XDirInfoEntry*)XMalloc_System(capacity * sizeof(XDirInfoEntry));
    if (!entries) {
        FindClose(hFind);
        return result;
    }

    size_t entryCount = 0;

    do {
        const char* name = findData.cFileName;

        // 过滤 "." 和 ".."
        if (strcmp(name, ".") == 0) {
            if (actualFilters & XDir_NoDot) continue;
            if (!(actualFilters & XDir_AllDirs) && !(actualFilters & XDir_Dirs)) continue;
        }
        if (strcmp(name, "..") == 0) {
            if (actualFilters & XDir_NoDotDot) continue;
            if (!(actualFilters & XDir_AllDirs) && !(actualFilters & XDir_Dirs)) continue;
        }

        bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool isFile = !isDir;
        bool isHidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        bool isSystem = (findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        bool isSymLink = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

        // 应用符号链接过滤器
        if (isSymLink && (actualFilters & XDir_NoSymLinks)) {
            continue;
        }

        if (isDir && (actualFilters & XDir_AllDirs)) {
            // AllDirs - 包含所有目录
        }
        else if (isDir && (actualFilters & XDir_Dirs)) {
            // Dirs - 包含目录
        }
        else if (isFile && (actualFilters & XDir_Files)) {
            // Files - 包含文件
        }
        else if (!(actualFilters & (XDir_Dirs | XDir_Files | XDir_AllDirs))) {
            continue;
        }

        if (isHidden && !(actualFilters & XDir_Hidden)) continue;
        if (isSystem && !(actualFilters & XDir_System)) continue;

        // 应用权限过滤器
        if (actualFilters & (XDir_Readable | XDir_Writable | XDir_Executable)) {
            size_t checkLen = strlen(pathUtf8) + 1 + strlen(name) + 1;
            char* checkPath = (char*)XMalloc_System(checkLen);
            if (checkPath) {
                snprintf(checkPath, checkLen, "%s\\%s", pathUtf8, name);

                if (actualFilters & XDir_Readable) {
                    HANDLE hFile = CreateFileA(checkPath, GENERIC_READ,
                                               FILE_SHARE_READ, NULL,
                                               OPEN_EXISTING, 0, NULL);
                    if (hFile == INVALID_HANDLE_VALUE) {
                        XFree_System(checkPath);
                        continue;
                    }
                    CloseHandle(hFile);
                }

                if (actualFilters & XDir_Writable) {
                    HANDLE hFile = CreateFileA(checkPath, GENERIC_WRITE,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                                               NULL, OPEN_EXISTING, 0, NULL);
                    if (hFile == INVALID_HANDLE_VALUE) {
                        XFree_System(checkPath);
                        continue;
                    }
                    CloseHandle(hFile);
                }

                if ((actualFilters & XDir_Executable) && !isDir) {
                    const char* ext = strrchr(name, '.');
                    if (!ext) {
                        XFree_System(checkPath);
                        continue;
                    }
                    if (_stricmp(ext, ".exe") != 0 &&
                        _stricmp(ext, ".bat") != 0 &&
                        _stricmp(ext, ".cmd") != 0 &&
                        _stricmp(ext, ".com") != 0) {
                        XFree_System(checkPath);
                        continue;
                    }
                }

                XFree_System(checkPath);
            }
        }

        // 应用名称过滤器
        if (nameFilters && XStringList_size_base(nameFilters) > 0) {
            XString* nameStr = XString_create_utf8(name);
            bool matchResult;
            if (actualFilters & XDir_CaseSensitive) {
                matchResult = false;
                for (size_t fi = 0; fi < XStringList_size_base(nameFilters); fi++) {
                    const XString* filter = XStringList_at_base(nameFilters, fi);
                    const char* filterUtf8 = XString_toUtf8((XString*)filter);
                    extern bool matchWildcardCaseSensitive(const char* pattern, const char* str);
                    if (matchWildcardCaseSensitive(filterUtf8, name)) {
                        matchResult = true;
                        break;
                    }
                }
            } else {
                matchResult = XDir_match_2(nameFilters, nameStr);
            }
            if (!matchResult) {
                XString_delete_base(nameStr);
                continue;
            }
            XString_delete_base(nameStr);
        }

        // 检查是否需要扩容
        if (entryCount >= capacity) {
            capacity *= 2;
            XDirInfoEntry* newEntries = (XDirInfoEntry*)XMalloc_System(capacity * sizeof(XDirInfoEntry));
            if (!newEntries) break;
            memcpy(newEntries, entries, entryCount * sizeof(XDirInfoEntry));
            XFree_System(entries);
            entries = newEntries;
        }

        // 构建 XFileInfo
        size_t fullPathLen = strlen(pathUtf8) + 1 + strlen(name) + 1;
        char* fullPath = (char*)XMalloc_System(fullPathLen);
        if (fullPath) {
            snprintf(fullPath, fullPathLen, "%s\\%s", pathUtf8, name);
            XString* filePath = XString_create_utf8(fullPath);
            XFree_System(fullPath);
            
                        // 初始化 XFileInfo 结构体
            XFileInfo_init_2(&entries[entryCount].info, filePath);
            XString_delete_base(filePath);
            
            entries[entryCount].isDir = isDir;
            entries[entryCount].size = ((int64_t)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;
            ULARGE_INTEGER ul;
            ul.LowPart = findData.ftLastWriteTime.dwLowDateTime;
            ul.HighPart = findData.ftLastWriteTime.dwHighDateTime;
            entries[entryCount].time = (int64_t)((ul.QuadPart - 116444736000000000LL) / 10000000);
            entryCount++;
        }

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    // 排序
    if ((actualSort & XDir_SortByMask) != XDir_Unsorted && actualSort != XDir_NoSort) {
        sortInfoEntries(entries, entryCount, actualSort);
    }

        // 将排序后的结果添加到 Vector
    for (size_t i = 0; i < entryCount; i++) {
        XVector_push_back_1_base(result, &entries[i].info);
    }
    
    // 释放临时数组（但不释放 XFileInfo，因为它们已转移到 Vector）
    XFree_System(entries);
    return result;
}

#endif // _WIN32