#include "XFileInfo.h"
#include "XFileSystem_platform.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * 虚函数实现
 * ============================================================================ */

static void VXFileInfo_copy(XFileInfo* self, const XFileInfo* other)
{
    if (!self || !other) return;
    
    // 拷贝路径
    if (self->m_filePath) {
        XString_delete_base(self->m_filePath);
    }
    self->m_filePath = other->m_filePath ? XString_create_copy(other->m_filePath) : NULL;
    
    // 拷贝缓存设置
    self->m_caching = other->m_caching;
    self->m_cacheValid = false;  // 拷贝后需要重新获取
}

static void VXFileInfo_move(XFileInfo* self, XFileInfo* other)
{
    if (!self || !other) return;
    
    // 移动路径
    if (self->m_filePath) {
        XString_delete_base(self->m_filePath);
    }
    self->m_filePath = other->m_filePath;
    other->m_filePath = NULL;
    
    // 移动缓存数据（直接拷贝整个 m_stat 结构）
    self->m_caching = other->m_caching;
    self->m_cacheValid = other->m_cacheValid;
    self->m_stat = other->m_stat;
    
    other->m_cacheValid = false;
}

static void VXFileInfo_deinit(XFileInfo* self)
{
    if (!self) return;
    
    if (self->m_filePath) {
        XString_delete_base(self->m_filePath);
        self->m_filePath = NULL;
    }
}

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XFileInfo_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XFileInfo))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXFileInfo_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXFileInfo_move);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFileInfo_deinit);
#if SHOWCONTAINERSIZE
        printf("XFileInfo size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
        return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

XFileInfo* XFileInfo_create_1(void)
{
    XFileInfo* info = (XFileInfo*)XMalloc_System(sizeof(XFileInfo));
    if (!info) return NULL;
    
    XFileInfo_init_1(info);
    Set_Class_MemoryFree(info, XFree_System);
    return info;
}

XFileInfo* XFileInfo_create_2(const XString* path)
{
    XFileInfo* info = (XFileInfo*)XMalloc_System(sizeof(XFileInfo));
    if (!info) return NULL;
    
    XFileInfo_init_2(info, path);
    Set_Class_MemoryFree(info, XFree_System);
    return info;
}

XFileInfo* XFileInfo_create_3(const XString* dir, const XString* path)
{
    XFileInfo* info = (XFileInfo*)XMalloc_System(sizeof(XFileInfo));
    if (!info) return NULL;
    
    XFileInfo_init_3(info, dir, path);
    Set_Class_MemoryFree(info, XFree_System);
    return info;
}

void XFileInfo_init_1(XFileInfo* info)
{
    if (!info) return;
    
    XClass_init(&info->m_class);
    XClassSetVtable(info, XFileInfo);
    
    info->m_filePath = XString_create();
    info->m_caching = true;
    info->m_cacheValid = false;
    
    // 初始化 m_stat
    memset(&info->m_stat, 0, sizeof(XFileStat));
    info->m_stat.ownerId = (uint32_t)-2;
    info->m_stat.groupId = (uint32_t)-2;
}

void XFileInfo_init_2(XFileInfo* info, const XString* path)
{
    XFileInfo_init_1(info);
    if (path) {
        XString_delete_base(info->m_filePath);
        info->m_filePath = XString_create_copy(path);
    }
}

void XFileInfo_init_3(XFileInfo* info, const XString* dir, const XString* path)
{
    XFileInfo_init_1(info);
    
    if (!dir || !path) return;
    
    const char* dirUtf8 = XString_toUtf8(dir);
    const char* pathUtf8 = XString_toUtf8(path);
    
    if (!dirUtf8 || !pathUtf8) return;
    
    // 如果 path 是绝对路径，忽略 dir
    if (XFileInfo_isAbsolutePath_static(pathUtf8)) {
        XString_delete_base(info->m_filePath);
        info->m_filePath = XString_create_copy(path);
        return;
    }
    
    // 构建组合路径
    size_t dirLen = strlen(dirUtf8);
    size_t pathLen = strlen(pathUtf8);
    
    // 移除 dir 结尾的分隔符
    while (dirLen > 0 && (dirUtf8[dirLen - 1] == '/' || dirUtf8[dirLen - 1] == '\\')) {
        dirLen--;
    }
    
    // 移除 path 开头的分隔符
    size_t pathStart = 0;
    while (pathStart < pathLen && (pathUtf8[pathStart] == '/' || pathUtf8[pathStart] == '\\')) {
        pathStart++;
    }
    
    XString_delete_base(info->m_filePath);
    info->m_filePath = XString_create_utf8("");
    XString_append_with_length_utf8(info->m_filePath, dirUtf8, dirLen);
    XString_append_utf8(info->m_filePath, "/");
    XString_append_utf8(info->m_filePath, pathUtf8 + pathStart);
}

/* ============================================================================
 * 文件路径（平台无关部分）
 * ============================================================================ */

void XFileInfo_setFile_1(XFileInfo* info, const XString* path)
{
    if (!info) return;
    
    if (info->m_filePath) {
        XString_delete_base(info->m_filePath);
    }
    info->m_filePath = path ? XString_create_copy(path) : XString_create();
    info->m_cacheValid = false;
}

void XFileInfo_setFile_2(XFileInfo* info, const XString* dir, const XString* path)
{
    if (!info) return;
    
    XFileInfo_init_3(info, dir, path);
    info->m_cacheValid = false;
}

const XString* XFileInfo_filePath(const XFileInfo* info)
{
    if (!info) return NULL;
    return info->m_filePath;
}

XString* XFileInfo_fileName(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return XString_create();
    
    size_t len = strlen(pathUtf8);
    if (len == 0) return XString_create();
    
    // 统一分隔符
    char* normalized = (char*)XMalloc_System(len + 1);
    if (!normalized) return XString_create();
    
    for (size_t i = 0; i <= len; i++) {
        normalized[i] = (pathUtf8[i] == '\\') ? '/' : pathUtf8[i];
    }
    
    // 移除结尾的分隔符
    while (len > 0 && normalized[len - 1] == '/') {
        len--;
    }
    normalized[len] = '\0';
    
    // 查找最后一个分隔符
    char* lastSlash = strrchr(normalized, '/');
    XString* result;
    
    if (lastSlash) {
        result = XString_create_utf8(lastSlash + 1);
    } else {
        result = XString_create_utf8(normalized);
    }
    
    XFree_System(normalized);
    return result;
}

XString* XFileInfo_baseName(const XFileInfo* info)
{
    XString* fileName = XFileInfo_fileName(info);
    if (!fileName) return XString_create();
    
    const char* nameUtf8 = XString_toUtf8(fileName);
    if (!nameUtf8) {
        XString_delete_base(fileName);
        return XString_create();
    }
    
    // 查找第一个 '.'
    const char* dot = strchr(nameUtf8, '.');
    XString* result;
    
    if (dot) {
        result = XString_create_utf8("");
        XString_append_with_length_utf8(result, nameUtf8, dot - nameUtf8);
    } else {
        result = XString_create_copy(fileName);
    }
    
    XString_delete_base(fileName);
    return result;
}

XString* XFileInfo_completeBaseName(const XFileInfo* info)
{
    XString* fileName = XFileInfo_fileName(info);
    if (!fileName) return XString_create();
    
    const char* nameUtf8 = XString_toUtf8(fileName);
    if (!nameUtf8) {
        XString_delete_base(fileName);
        return XString_create();
    }
    
    size_t len = strlen(nameUtf8);
    const char* lastDot = NULL;
    
    // 查找最后一个 '.'（从末尾开始）
    for (size_t i = len; i > 0; i--) {
        if (nameUtf8[i - 1] == '.') {
            lastDot = nameUtf8 + i - 1;
            break;
        }
    }
    
    XString* result;
    if (lastDot && lastDot != nameUtf8) {
        result = XString_create_utf8("");
        XString_append_with_length_utf8(result, nameUtf8, lastDot - nameUtf8);
    } else {
        result = XString_create_copy(fileName);
    }
    
    XString_delete_base(fileName);
    return result;
}

XString* XFileInfo_suffix(const XFileInfo* info)
{
    XString* fileName = XFileInfo_fileName(info);
    if (!fileName) return XString_create();
    
    const char* nameUtf8 = XString_toUtf8(fileName);
    if (!nameUtf8) {
        XString_delete_base(fileName);
        return XString_create();
    }
    
    size_t len = strlen(nameUtf8);
    const char* lastDot = NULL;
    
    // 查找最后一个 '.'
    for (size_t i = len; i > 0; i--) {
        if (nameUtf8[i - 1] == '.') {
            lastDot = nameUtf8 + i - 1;
            break;
        }
    }
    
    XString* result;
    if (lastDot && lastDot[1] != '\0') {
        result = XString_create_utf8(lastDot + 1);
    } else {
        result = XString_create();
    }
    
    XString_delete_base(fileName);
    return result;
}

XString* XFileInfo_completeSuffix(const XFileInfo* info)
{
    XString* fileName = XFileInfo_fileName(info);
    if (!fileName) return XString_create();
    
    const char* nameUtf8 = XString_toUtf8(fileName);
    if (!nameUtf8) {
        XString_delete_base(fileName);
        return XString_create();
    }
    
    // 查找第一个 '.'
    const char* firstDot = strchr(nameUtf8, '.');
    XString* result;
    
    if (firstDot && firstDot[1] != '\0') {
        result = XString_create_utf8(firstDot + 1);
    } else {
        result = XString_create();
    }
    
    XString_delete_base(fileName);
    return result;
}

XString* XFileInfo_path(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return XString_create();
    
    size_t len = strlen(pathUtf8);
    if (len == 0) return XString_create();
    
    // 统一分隔符
    char* normalized = (char*)XMalloc_System(len + 1);
    if (!normalized) return XString_create();
    
    for (size_t i = 0; i <= len; i++) {
        normalized[i] = (pathUtf8[i] == '\\') ? '/' : pathUtf8[i];
    }
    
    // 移除结尾的分隔符
    while (len > 0 && normalized[len - 1] == '/') {
        len--;
    }
    normalized[len] = '\0';
    
    // 查找最后一个分隔符
    char* lastSlash = strrchr(normalized, '/');
    XString* result;
    
    if (lastSlash) {
        *lastSlash = '\0';
        result = XString_create_utf8(normalized);
    } else {
        result = XString_create_utf8(".");
    }
    
    XFree_System(normalized);
    return result;
}

/* ============================================================================
 * 缓存控制
 * ============================================================================ */

bool XFileInfo_caching(const XFileInfo* info)
{
    if (!info) return false;
    return info->m_caching;
}

void XFileInfo_setCaching(XFileInfo* info, bool enable)
{
    if (!info) return;
    info->m_caching = enable;
    if (!enable) {
        info->m_cacheValid = false;
    }
}

void XFileInfo_refresh(XFileInfo* info)
{
    if (!info) return;
    info->m_cacheValid = false;
}

/* ============================================================================
 * 路径类型检查（平台无关）
 * ============================================================================ */

bool XFileInfo_isRelative(const XFileInfo* info)
{
    return !XFileInfo_isAbsolute(info);
}

/* ============================================================================
 * 内部辅助函数 - 使用 XFileSystem API
 * ============================================================================ */

static void XFileInfo_updateCache(XFileInfo* info)
{
    if (!info || !info->m_filePath) return;
    if (info->m_cacheValid && info->m_caching) return;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return;
    
    // 直接拷贝整个 XFileStat 结构
    if (!XFileSystem_stat(pathUtf8, &info->m_stat)) {
        info->m_stat.exists = false;
        info->m_stat.isFile = false;
        info->m_stat.isDir = false;
    }
    
    info->m_cacheValid = true;
}

/* ============================================================================
 * 路径操作
 * ============================================================================ */

XString* XFileInfo_absoluteFilePath(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    char absPath[1024];
    
    if (XFileSystem_resolvePath(pathUtf8, absPath, sizeof(absPath), XPathStyle_Absolute)) {
        return XString_create_utf8(absPath);
    }
    return XString_create();
}

XString* XFileInfo_canonicalFilePath(const XFileInfo* info)
{
    return XFileInfo_absoluteFilePath(info);
}

XString* XFileInfo_absolutePath(const XFileInfo* info)
{
    XString* absFilePath = XFileInfo_absoluteFilePath(info);
    if (!absFilePath || XString_length_base(absFilePath) == 0) {
        if (absFilePath) XString_delete_base(absFilePath);
        return XString_create();
    }
    
    const char* pathUtf8 = XString_toUtf8(absFilePath);
    char* lastSlash = strrchr((char*)pathUtf8, '/');
    if (!lastSlash) lastSlash = strrchr((char*)pathUtf8, '\\');
    
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
    return XFileInfo_absolutePath(info);
}

/* ============================================================================
 * 文件类型检查
 * ============================================================================ */

bool XFileInfo_exists(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.exists;
}

bool XFileInfo_exists_static(const XString* path)
{
    if (!path) return false;
    return XFileSystem_exists(XString_toUtf8(path));
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
    return info->m_stat.isFile;
}

bool XFileInfo_isDir(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.isDir;
}

bool XFileInfo_isSymLink(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.isSymLink;
}

bool XFileInfo_isSymbolicLink(const XFileInfo* info)
{
    return XFileInfo_isSymLink(info);
}

bool XFileInfo_isShortcut(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.isShortcut;
}

bool XFileInfo_isJunction(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.isJunction;
}

bool XFileInfo_isRoot(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return false;
    
    char absPath[1024];
    if (!XFileSystem_resolvePath(pathUtf8, absPath, sizeof(absPath), XPathStyle_Absolute)) return false;
    
    size_t len = strlen(absPath);
    
    // Windows: "C:\" or "\\server\share\"
    if (len == 3 && isalpha((unsigned char)absPath[0]) &&
        absPath[1] == ':' && (absPath[2] == '\\' || absPath[2] == '/')) {
        return true;
    }
    
    // Unix: "/"
    if (len == 1 && absPath[0] == '/') {
        return true;
    }
    
    return false;
}

bool XFileInfo_isBundle(const XFileInfo* info)
{
    (void)info;
    return false;  // Bundle 只在 macOS/iOS 上存在
}

bool XFileInfo_isHidden(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.isHidden;
}

/* ============================================================================
 * 路径类型检查
 * ============================================================================ */

bool XFileInfo_isAbsolute(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    return XFileInfo_isAbsolutePath_static(pathUtf8);
}

bool XFileInfo_isAbsolutePath_static(const char* path)
{
    if (!path || !path[0]) return false;
    
    // Windows: "C:\" or "\\server\share"
    if (path[0] == '\\' && path[1] == '\\') return true;
    if (path[0] == '\\') return true;
    if (isalpha((unsigned char)path[0]) && path[1] == ':') return true;
    
    // Unix: "/path"
    if (path[0] == '/') return true;
    
    return false;
}

bool XFileInfo_isNativePath(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    if (!pathUtf8) return false;
    // 以 ":" 开头的是资源路径，不是本地路径
    return pathUtf8[0] != ':';
}

bool XFileInfo_makeAbsolute(XFileInfo* info)
{
    if (!info || !info->m_filePath) return false;
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
 * 权限检查
 * ============================================================================ */

bool XFileInfo_isReadable(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.isReadable;
}

bool XFileInfo_isWritable(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.isWritable;
}

bool XFileInfo_isExecutable(const XFileInfo* info)
{
    if (!info) return false;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.isExecutable;
}

bool XFileInfo_permission(const XFileInfo* info, XFilePermissions permissions)
{
    if (!info) return false;
    XFilePermissions perms = XFileInfo_permissions(info);
    return (perms & permissions) == permissions;
}

XFilePermissions XFileInfo_permissions(const XFileInfo* info)
{
    if (!info) return 0;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.permissions;
}

/* ============================================================================
 * 文件属性
 * ============================================================================ */

int64_t XFileInfo_size(const XFileInfo* info)
{
    if (!info) return 0;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.size;
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
            timestamp = info->m_stat.birthTime;
            break;
        case XFile_MetadataChangeTime:
            timestamp = info->m_stat.metadataChangeTime;
            break;
        case XFile_ModificationTime:
            timestamp = info->m_stat.modificationTime;
            break;
        case XFile_AccessTime:
            timestamp = info->m_stat.accessTime;
            break;
    }
    
    if (timestamp > 0) {
        XDateTime_setSecsSinceEpoch(&result, timestamp);
    }
    
    return result;
}

/* ============================================================================
 * 所有者信息（简化实现，返回缓存的ID）
 * ============================================================================ */

XString* XFileInfo_owner(const XFileInfo* info)
{
    // 所有者名称需要平台特定实现，这里返回空字符串
    (void)info;
    return XString_create();
}

uint32_t XFileInfo_ownerId(const XFileInfo* info)
{
    if (!info) return (uint32_t)-2;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.ownerId;
}

XString* XFileInfo_group(const XFileInfo* info)
{
    // 组名称需要平台特定实现，这里返回空字符串
    (void)info;
    return XString_create();
}

uint32_t XFileInfo_groupId(const XFileInfo* info)
{
    if (!info) return (uint32_t)-2;
    XFileInfo_updateCache((XFileInfo*)info);
    return info->m_stat.groupId;
}

/* ============================================================================
 * 符号链接
 * ============================================================================ */

XString* XFileInfo_symLinkTarget(const XFileInfo* info)
{
    if (!info || !info->m_filePath) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(info->m_filePath);
    char target[1024];
    
    if (!XFileSystem_readLink(pathUtf8, target, sizeof(target))) {
        return XString_create();
    }
    
    return XString_create_utf8(target);
}

XString* XFileInfo_readSymLink(const XFileInfo* info)
{
    return XFileInfo_symLinkTarget(info);
}

XString* XFileInfo_junctionTarget(const XFileInfo* info)
{
    if (!info) return XString_create();
    if (!XFileInfo_isJunction(info)) return XString_create();
    return XFileInfo_symLinkTarget(info);
}

/* ============================================================================
 * 比较操作
 * ============================================================================ */

bool XFileInfo_equals(const XFileInfo* lhs, const XFileInfo* rhs)
{
    if (!lhs || !rhs) return false;
    if (lhs == rhs) return true;
    
    XString* path1 = XFileInfo_canonicalFilePath(lhs);
    XString* path2 = XFileInfo_canonicalFilePath(rhs);
    
    if (!path1 || !path2) {
        if (path1) XString_delete_base(path1);
        if (path2) XString_delete_base(path2);
        return false;
    }
    
    const char* p1 = XString_toUtf8(path1);
    const char* p2 = XString_toUtf8(path2);
    
    bool result = (strcmp(p1, p2) == 0);
    
    XString_delete_base(path1);
    XString_delete_base(path2);
    
    return result;
}