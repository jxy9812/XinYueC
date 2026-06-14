#ifdef _WIN32
#include "XDir.h"
#include "XMemory.h"
#include <windows.h>
#include <direct.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>

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

XString* XDir_absoluteFilePath(const XDir* dir, const XString* fileName)
{
    if (!dir || !dir->m_path || !fileName) return NULL;

    XString* absPath = XDir_absolutePath(dir);
    if (!absPath) return NULL;

    const char* absUtf8 = XString_toUtf8(absPath);
    const char* fileUtf8 = XString_toUtf8(fileName);

        size_t len = strlen(absUtf8) + 1 + strlen(fileUtf8) + 1;
    char* fullPath = (char*)XMalloc_System(len);
    if (!fullPath) {
        XString_delete_base(absPath);
        return NULL;
    }

    snprintf(fullPath, len, "%s\\%s", absUtf8, fileUtf8);
    XString* result = XString_create_utf8(fullPath);

    XFree_System(fullPath);
    XString_delete_base(absPath);
    return result;
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

    // 检查是否是根目录 "C:\" 或 "\\server\share\"
    size_t len = strlen(absPath);
    if (len == 3 && isalpha((unsigned char)absPath[0]) &&
        absPath[1] == ':' && absPath[2] == '\\') {
        return true;  // 驱动器根目录
    }

    // TODO: UNC 路径检查

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
    // TODO: 需要递归删除所有文件和子目录
    // 依赖 XFile 和完整的 entryList 实现
    (void)dir;
    return false;
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

    bool ignoreCase = (actualSort & XDir_IgnoreCase) != 0;

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

        // 应用名称过滤器
        if (nameFilters && XStringList_size_base(nameFilters) > 0) {
            XString* nameStr = XString_create_utf8(name);
            if (!XDir_match_2(nameFilters, nameStr)) {
                XString_delete_base(nameStr);
                continue;
            }
            XString_delete_base(nameStr);
        }

        XStringList_push_back_utf8(result, name);

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    // TODO: 排序实现
    // 需要根据 actualSort 进行排序

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

XString* XDir_cleanPath(const XString* path)
{
    if (!path) return NULL;

    const char* pathUtf8 = XString_toUtf8(path);
    if (!pathUtf8) return NULL;

        size_t len = strlen(pathUtf8);
    char* result = (char*)XMalloc_System(len + 1);
    if (!result) return NULL;

    // 统一分隔符为 '/'
    for (size_t i = 0; i <= len; i++) {
        result[i] = (pathUtf8[i] == '\\') ? '/' : pathUtf8[i];
    }

    // 简化路径（移除多余的 "." 和 ".."）
    // TODO: 实现完整的路径简化逻辑

    XString* cleanStr = XString_create_utf8(result);
    XFree_System(result);

    return cleanStr;
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

    // 简单实现：如果目标在基准目录下，返回相对路径
    // TODO: 实现完整的相对路径计算逻辑

    size_t baseLen = strlen(absBase);
    size_t targetLen = strlen(absTarget);

    // 检查是否以基准路径开头
    if (_strnicmp(absBase, absTarget, baseLen) == 0) {
        // 目标在基准目录下
        if (absTarget[baseLen] == '\\' || absTarget[baseLen] == '/') {
            return XString_create_utf8(absTarget + baseLen + 1);
        }
    }

    // 无法计算相对路径，返回绝对路径
    return XString_create_utf8(absTarget);
}

#endif // _WIN32