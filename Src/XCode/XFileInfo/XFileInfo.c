#include "XFileInfo.h"
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
    
    // 移动缓存数据
    self->m_caching = other->m_caching;
    self->m_cacheValid = other->m_cacheValid;
    self->m_size = other->m_size;
    self->m_birthTime = other->m_birthTime;
    self->m_metadataChangeTime = other->m_metadataChangeTime;
    self->m_modificationTime = other->m_modificationTime;
    self->m_accessTime = other->m_accessTime;
    self->m_permissions = other->m_permissions;
    self->m_ownerId = other->m_ownerId;
    self->m_groupId = other->m_groupId;
    self->m_exists = other->m_exists;
    self->m_isFile = other->m_isFile;
    self->m_isDir = other->m_isDir;
    self->m_isSymLink = other->m_isSymLink;
    self->m_isShortcut = other->m_isShortcut;
    self->m_isJunction = other->m_isJunction;
    self->m_isHidden = other->m_isHidden;
    self->m_isReadable = other->m_isReadable;
    self->m_isWritable = other->m_isWritable;
    self->m_isExecutable = other->m_isExecutable;
    
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
    info->m_size = 0;
    info->m_birthTime = 0;
    info->m_metadataChangeTime = 0;
    info->m_modificationTime = 0;
    info->m_accessTime = 0;
    info->m_permissions = 0;
    info->m_ownerId = (uint32_t)-2;
    info->m_groupId = (uint32_t)-2;
    info->m_exists = false;
    info->m_isFile = false;
    info->m_isDir = false;
    info->m_isSymLink = false;
    info->m_isShortcut = false;
    info->m_isJunction = false;
    info->m_isHidden = false;
    info->m_isReadable = false;
    info->m_isWritable = false;
    info->m_isExecutable = false;
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
 * 以下函数依赖平台实现，在 Drive/windows/XFileInfo_win32.c 或 
 * Drive/linux/XFileInfo_linux.c 中实现
 * ============================================================================ */

// XString* XFileInfo_absoluteFilePath(const XFileInfo* info);
// XString* XFileInfo_canonicalFilePath(const XFileInfo* info);
// XString* XFileInfo_absolutePath(const XFileInfo* info);
// XString* XFileInfo_canonicalPath(const XFileInfo* info);
// bool XFileInfo_exists(const XFileInfo* info);
// bool XFileInfo_exists_static(const XString* path);
// void XFileInfo_stat(XFileInfo* info);
// bool XFileInfo_isFile(const XFileInfo* info);
// bool XFileInfo_isDir(const XFileInfo* info);
// bool XFileInfo_isSymLink(const XFileInfo* info);
// bool XFileInfo_isSymbolicLink(const XFileInfo* info);
// bool XFileInfo_isShortcut(const XFileInfo* info);
// bool XFileInfo_isJunction(const XFileInfo* info);
// bool XFileInfo_isRoot(const XFileInfo* info);
// bool XFileInfo_isBundle(const XFileInfo* info);
// bool XFileInfo_isHidden(const XFileInfo* info);
// bool XFileInfo_isAbsolute(const XFileInfo* info);
// bool XFileInfo_isAbsolutePath_static(const char* path);
// bool XFileInfo_isNativePath(const XFileInfo* info);
// bool XFileInfo_makeAbsolute(XFileInfo* info);
// bool XFileInfo_isReadable(const XFileInfo* info);
// bool XFileInfo_isWritable(const XFileInfo* info);
// bool XFileInfo_isExecutable(const XFileInfo* info);
// bool XFileInfo_permission(const XFileInfo* info, XFilePermissions permissions);
// XFilePermissions XFileInfo_permissions(const XFileInfo* info);
// int64_t XFileInfo_size(const XFileInfo* info);
// XDateTime XFileInfo_birthTime(const XFileInfo* info);
// XDateTime XFileInfo_metadataChangeTime(const XFileInfo* info);
// XDateTime XFileInfo_lastModified(const XFileInfo* info);
// XDateTime XFileInfo_lastRead(const XFileInfo* info);
// XDateTime XFileInfo_fileTime(const XFileInfo* info, XFileTime time);
// XString* XFileInfo_owner(const XFileInfo* info);
// uint32_t XFileInfo_ownerId(const XFileInfo* info);
// XString* XFileInfo_group(const XFileInfo* info);
// uint32_t XFileInfo_groupId(const XFileInfo* info);
// XString* XFileInfo_symLinkTarget(const XFileInfo* info);
// XString* XFileInfo_readSymLink(const XFileInfo* info);
// XString* XFileInfo_junctionTarget(const XFileInfo* info);