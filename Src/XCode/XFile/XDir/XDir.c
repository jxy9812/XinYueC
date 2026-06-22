#include "XDir.h"
#include "XFileSystem_platform.h"
#include "XSort.h"
#include "XCompare.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

/* ============================================================================
 * 虚函数实�?
 * ============================================================================ */

static void VXDir_copy(XDir* self, const XDir* other)
{
    if (!self || !other) return;
    
    // 拷贝路径
    if (self->m_path) {
        XString_delete_base(self->m_path);
    }
    self->m_path = other->m_path ? XString_create_copy(other->m_path) : NULL;
    
    // 拷贝名称过滤�?
    if (self->m_nameFilters) {
        XStringList_delete_base(self->m_nameFilters);
    }
    self->m_nameFilters = other->m_nameFilters ? XStringList_create_copy(other->m_nameFilters) : NULL;
    
    // 拷贝过滤器和排序标志
    self->m_filters = other->m_filters;
    self->m_sorting = other->m_sorting;
}

static void VXDir_move(XDir* self, XDir* other)
{
    if (!self || !other) return;
    
    // 移动路径
    if (self->m_path) {
        XString_delete_base(self->m_path);
    }
    self->m_path = other->m_path;
    other->m_path = NULL;
    
    // 移动名称过滤�?
    if (self->m_nameFilters) {
        XStringList_delete_base(self->m_nameFilters);
    }
    self->m_nameFilters = other->m_nameFilters;
    other->m_nameFilters = NULL;
    
    // 移动过滤器和排序标志
    self->m_filters = other->m_filters;
    self->m_sorting = other->m_sorting;
    other->m_filters = XDir_AllEntries;
    other->m_sorting = XDir_Name | XDir_IgnoreCase;
}

static void VXDir_deinit(XDir* self)
{
    if (!self) return;
    
    // 释放路径
    if (self->m_path) {
        XString_delete_base(self->m_path);
        self->m_path = NULL;
    }
    
    // 释放名称过滤�?
    if (self->m_nameFilters) {
        XStringList_delete_base(self->m_nameFilters);
        self->m_nameFilters = NULL;
    }
    
    // 释放缓存
    if (self->m_cachedEntries) {
        XStringList_delete_base(self->m_cachedEntries);
        self->m_cachedEntries = NULL;
    }
}

/* ============================================================================
 * 虚函数表初始�?
 * ============================================================================ */

XVtable* XDir_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始�?
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XDir))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承�?
        XVTABLE_INHERIT_XCLASS(XClass);
        //重载虚函�?
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXDir_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXDir_move);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDir_deinit);
#if SHOWCONTAINERSIZE
        printf("XDir size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
        return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

XDir* XDir_create_1(void)
{
    XDir* dir = (XDir*)XMalloc_System(sizeof(XDir));
    if (!dir) return NULL;
    
    XDir_init_1(dir);
    Set_Class_MemoryFree(dir, XFree_System);
    return dir;
}

XDir* XDir_create_2(const XString* path)
{
    XDir* dir = (XDir*)XMalloc_System(sizeof(XDir));
    if (!dir) return NULL;
    
    XDir_init_2(dir, path);
    Set_Class_MemoryFree(dir, XFree_System);
    return dir;
}

XDir* XDir_create_3(const XString* path, const XStringList* nameFilters,
                    XDirSortFlags sort, XDirFilters filters)
{
    XDir* dir = (XDir*)XMalloc_System(sizeof(XDir));
    if (!dir) return NULL;
    
    XDir_init_3(dir, path, nameFilters, sort, filters);
    Set_Class_MemoryFree(dir, XFree_System);
    return dir;
}

void XDir_init_1(XDir* dir)
{
    if (!dir) return;
    
    XClass_init(&dir->m_class);
    XClassSetVtable(dir, XDir);
    
    dir->m_path = XString_create_utf8(".");
    dir->m_nameFilters = XStringList_create();
    dir->m_filters = XDir_AllEntries;
    dir->m_sorting = XDir_Name | XDir_IgnoreCase;
    
    // 初始化缓�?
    dir->m_cachedEntries = NULL;
    dir->m_cachedFilters = XDir_NoFilter;
    dir->m_cachedSorting = XDir_NoSort;
    dir->m_cacheValid = false;
}

void XDir_init_2(XDir* dir, const XString* path)
{
    if (!dir) return;
    
    XClass_init(&dir->m_class);
    XClassSetVtable(dir, XDir);
    
    dir->m_path = XString_create_copy(path);
    dir->m_nameFilters = XStringList_create();
    dir->m_filters = XDir_AllEntries;
    dir->m_sorting = XDir_Name | XDir_IgnoreCase;
    
    // 初始化缓�?
    dir->m_cachedEntries = NULL;
    dir->m_cachedFilters = XDir_NoFilter;
    dir->m_cachedSorting = XDir_NoSort;
    dir->m_cacheValid = false;
}

void XDir_init_3(XDir* dir, const XString* path, const XStringList* nameFilters,
                 XDirSortFlags sort, XDirFilters filters)
{
    if (!dir) return;
    
    XClass_init(&dir->m_class);
    XClassSetVtable(dir, XDir);
    
    dir->m_path = XString_create_copy(path);
    dir->m_nameFilters = XStringList_create_copy(nameFilters);
    dir->m_filters = (filters == XDir_NoFilter) ? XDir_AllEntries : filters;
    dir->m_sorting = (sort == XDir_NoSort) ? (XDir_Name | XDir_IgnoreCase) : sort;
    
    // 初始化缓�?
    dir->m_cachedEntries = NULL;
    dir->m_cachedFilters = XDir_NoFilter;
    dir->m_cachedSorting = XDir_NoSort;
    dir->m_cacheValid = false;
}

/* ============================================================================
 * 路径操作
 * ============================================================================ */

void XDir_setPath(XDir* dir, const XString* path)
{
    if (!dir || !path) return;
    
    if (dir->m_path) {
        XString_delete_base(dir->m_path);
    }
    dir->m_path = XString_create_copy(path);
    
    // 使缓存失�?
    dir->m_cacheValid = false;
}

const XString* XDir_path(const XDir* dir)
{
    if (!dir) return NULL;
    return dir->m_path;
}

XString* XDir_dirName(const XDir* dir)
{
    if (!dir || !dir->m_path) return XString_create();
    
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return XString_create();
    
    size_t len = strlen(pathUtf8);
    if (len == 0) return XString_create();
    
    // 统一分隔符为 '/'
    char* normalized = (char*)XMalloc_System(len + 1);
    if (!normalized) return XString_create();
    
    for (size_t i = 0; i <= len; i++) {
        normalized[i] = (pathUtf8[i] == '\\') ? '/' : pathUtf8[i];
    }
    
    // 移除结尾的分隔符
    while (len > 1 && normalized[len - 1] == '/') {
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

XString* XDir_filePath(const XDir* dir, const XString* fileName)
{
    if (!dir || !dir->m_path || !fileName) return NULL;
    
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    const char* fileUtf8 = XString_toUtf8(fileName);
    if (!pathUtf8 || !fileUtf8) return NULL;
    
    size_t pathLen = strlen(pathUtf8);
    size_t fileLen = strlen(fileUtf8);
    
    // 移除路径结尾的分隔符
    while (pathLen > 0 && (pathUtf8[pathLen - 1] == '/' || pathUtf8[pathLen - 1] == '\\')) {
        pathLen--;
    }
    
    // 移除文件名开头的分隔�?
    size_t fileStart = 0;
    while (fileStart < fileLen && (fileUtf8[fileStart] == '/' || fileUtf8[fileStart] == '\\')) {
        fileStart++;
    }
    
    // 构建结果
    XString* result = XString_create_utf8("");
    XString_append_with_length_utf8(result, pathUtf8, pathLen);
    XString_append_utf8(result, "/");
    XString_append_utf8(result, fileUtf8 + fileStart);
    
    return result;
}

/* ============================================================================
 * 过滤器和排序
 * ============================================================================ */

void XDir_setFilter(XDir* dir, XDirFilters filters)
{
    if (!dir) return;
    
    // 如果过滤器改变了，使缓存失效
    if (dir->m_filters != filters) {
        dir->m_filters = filters;
        dir->m_cacheValid = false;
    }
}

XDirFilters XDir_filter(const XDir* dir)
{
    if (!dir) return XDir_NoFilter;
    return dir->m_filters;
}

void XDir_setSorting(XDir* dir, XDirSortFlags sort)
{
    if (!dir) return;
    
    // 如果排序改变了，使缓存失�?
    if (dir->m_sorting != sort) {
        dir->m_sorting = sort;
        dir->m_cacheValid = false;
    }
}

XDirSortFlags XDir_sorting(const XDir* dir)
{
    if (!dir) return XDir_NoSort;
    return dir->m_sorting;
}

void XDir_setNameFilters(XDir* dir, const XStringList* nameFilters)
{
    if (!dir) return;
    
    if (dir->m_nameFilters) {
        XStringList_delete_base(dir->m_nameFilters);
    }
    dir->m_nameFilters = XStringList_create_copy(nameFilters);
    
    // 使缓存失�?
    dir->m_cacheValid = false;
}

const XStringList* XDir_nameFilters(const XDir* dir)
{
    if (!dir) return NULL;
    return dir->m_nameFilters;
}

/* ============================================================================
 * 通配符匹配（平台无关�?
 * ============================================================================ */

static bool matchWildcard(const char* pattern, const char* str, bool ignoreCase)
{
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return true;
            
            while (*str) {
                if (matchWildcard(pattern, str, ignoreCase)) {
                    return true;
                }
                str++;
            }
            return false;
        } else if (*pattern == '?') {
            pattern++;
            str++;
        } else {
            char pc = ignoreCase ? tolower((unsigned char)*pattern) : *pattern;
            char sc = ignoreCase ? tolower((unsigned char)*str) : *str;
            if (pc != sc) return false;
            pattern++;
            str++;
        }
    }
    
    while (*pattern == '*') pattern++;
    return !*pattern && !*str;
}

// 区分大小写的通配符匹配（供外部调用）
bool matchWildcardCaseSensitive(const char* pattern, const char* str)
{
    return matchWildcard(pattern, str, false);
}

bool XDir_match_1(const XString* filter, const XString* fileName)
{
    if (!filter || !fileName) return false;
    
    const char* filterUtf8 = XString_toUtf8(filter);
    const char* fileUtf8 = XString_toUtf8(fileName);
    
    // Qt �?match() 默认忽略大小�?
    return matchWildcard(filterUtf8, fileUtf8, true);
}

bool XDir_match_2(const XStringList* filters, const XString* fileName)
{
    if (!filters || !fileName) return false;
    
    size_t count = XStringList_size_base(filters);
    for (size_t i = 0; i < count; i++) {
        const XString* filter = XStringList_at_base(filters, i);
        if (XDir_match_1(filter, fileName)) {
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * 分隔符转换（平台无关�?
 * ============================================================================ */

XChar XDir_separator(void)
{
#ifdef _WIN32
    return XChar_from('\\');
#else
    return XChar_from('/');
#endif
}

XChar XDir_listSeparator(void)
{
#ifdef _WIN32
    return XChar_from(';');
#else
    return XChar_from(':');
#endif
}

XString* XDir_toNativeSeparators(const XString* pathName)
{
    if (!pathName) return NULL;
    
    XString* result = XString_create_copy(pathName);
    const char* utf8 = XString_toUtf8(result);
    
#ifdef _WIN32
    // �?'/' 转换�?'\'
    char* data = (char*)utf8;  // 假设可以修改
    while (*data) {
        if (*data == '/') *data = '\\';
        data++;
    }
#else
    (void)utf8;  // Unix 下不需要转�?
#endif
    
    return result;
}

XString* XDir_fromNativeSeparators(const XString* pathName)
{
    if (!pathName) return NULL;
    
    XString* result = XString_create_copy(pathName);
    // �?'\' 转换�?'/'
    const char* utf8 = XString_toUtf8(result);
    char* data = (char*)utf8;
    
    while (*data) {
        if (*data == '\\') *data = '/';
        data++;
    }
    
    return result;
}
/* ============================================================================
 * 名称过滤器解析（平台无关�?
 * ============================================================================ */
XStringList* XDir_nameFiltersFromString(const XString* nameFilter)
{
    if (!nameFilter) return XStringList_create();
    
    const char* filterUtf8 = XString_toUtf8(nameFilter);
    if (!filterUtf8) return XStringList_create();
    
    XStringList* result = XStringList_create();
    
    // 使用 xstrdup 复制字符串（XString.h 中提供）
    char* dup = XStrdup(filterUtf8);
    if (!dup) return result;
    
    // 按空格和分号分割
    char* token = strtok(dup, " ;");
    while (token) {
        XStringList_push_back_utf8(result, token);
        token = strtok(NULL, " ;");
    }
    
    XFree_System(dup);
    return result;
}

/* ============================================================================
 * 排序功能（平台无关）- 使用 XVector 和自定义比较函数
 * ============================================================================ */

// 用于排序的条目信�?
typedef struct {
    const char* name;
    bool isDir;
    int64_t size;
    int64_t time;
} XDirEntryInfo;

// 排序上下文（避免全局变量�?
typedef struct {
    XDirSortFlags flags;
} XDirSortContext;

static XDirSortContext g_sortContext;

// 按名称比�?
static int compareByName(const XDirEntryInfo* a, const XDirEntryInfo* b, XDirSortFlags flags)
{
    bool ignoreCase = (flags & XDir_IgnoreCase) != 0;
    
    const char* nameA = a->name;
    const char* nameB = b->name;
    
    if (ignoreCase) {
        while (*nameA && *nameB) {
            char ca = tolower((unsigned char)*nameA);
            char cb = tolower((unsigned char)*nameB);
            if (ca != cb) return ca - cb;
            nameA++;
            nameB++;
        }
        return tolower((unsigned char)*nameA) - tolower((unsigned char)*nameB);
    }
    
    return strcmp(nameA, nameB);
}

// 按时间比�?
static int compareByTime(const XDirEntryInfo* a, const XDirEntryInfo* b)
{
    if (a->time < b->time) return XCompare_Less;
    if (a->time > b->time) return XCompare_Greater;
    return XCompare_Equality;
}

// 按大小比�?
static int compareBySize(const XDirEntryInfo* a, const XDirEntryInfo* b)
{
    if (a->size < b->size) return XCompare_Less;
    if (a->size > b->size) return XCompare_Greater;
    return XCompare_Equality;
}

// 按类型（扩展名）比较
static int compareByType(const XDirEntryInfo* a, const XDirEntryInfo* b, XDirSortFlags flags)
{
    const char* extA = strrchr(a->name, '.');
    const char* extB = strrchr(b->name, '.');
    
    if (!extA) extA = "";
    if (!extB) extB = "";
    
    bool ignoreCase = (flags & XDir_IgnoreCase) != 0;
    int result;
    
#ifdef _WIN32
    result = ignoreCase ? _stricmp(extA, extB) : strcmp(extA, extB);
#else
    result = ignoreCase ? strcasecmp(extA, extB) : strcmp(extA, extB);
#endif
    
    if (result == 0) {
        result = compareByName(a, b, flags);
    }
    
    return result;
}

// XVector 比较回调函数（符�?XCompare 类型�?
static int32_t XDirEntryInfo_compare(const void* lhs, const void* rhs)
{
    const XDirEntryInfo* a = (const XDirEntryInfo*)lhs;
    const XDirEntryInfo* b = (const XDirEntryInfo*)rhs;
    XDirSortFlags flags = g_sortContext.flags;
    
    int result = 0;
    
    // DirsFirst �?DirsLast 处理
    if ((flags & XDir_DirsFirst) || (flags & XDir_DirsLast)) {
        if (a->isDir && !b->isDir) {
            return (flags & XDir_DirsFirst) ? XCompare_Less : XCompare_Greater;
        }
        if (!a->isDir && b->isDir) {
            return (flags & XDir_DirsFirst) ? XCompare_Greater : XCompare_Less;
        }
    }
    
    // 根据排序类型选择比较方式
    if (flags & XDir_Type) {
        result = compareByType(a, b, flags);
    } else {
        switch (flags & XDir_SortByMask) {
            case XDir_Time:
                result = compareByTime(a, b);
                break;
            case XDir_Size:
                result = compareBySize(a, b);
                break;
            case XDir_Name:
            case XDir_Unsorted:
            default:
                result = compareByName(a, b, flags);
                break;
        }
    }
    
    // 反向处理
    if (flags & XDir_Reversed) {
        result = -result;
    }
    
    return result;
}

/* ============================================================================
 * 通用函数（平台无关）
 * ============================================================================ */

bool XDir_isRelative(const XDir* dir)
{
    return !XDir_isAbsolute(dir);
}

bool XDir_makeAbsolute(XDir* dir)
{
    if (!dir || !dir->m_path) return false;
    
    if (XDir_isAbsolute(dir)) return true;
    
    XString* absPath = XDir_absolutePath(dir);
    if (!absPath) return false;
    
    XString_delete_base(dir->m_path);
    dir->m_path = absPath;
    return true;
}

size_t XDir_count(const XDir* dir)
{
    if (!dir) return 0;
    
    // 检查缓存是否有效，如果无效则更新缓�?
    if (!dir->m_cacheValid || !dir->m_cachedEntries) {
        // 使用默认过滤器和排序更新缓存
        XStringList* list = XDir_entryList_1(dir, XDir_NoFilter, XDir_NoSort);
        if (!list) return 0;
        
        // 更新缓存
        XDir* mutableDir = (XDir*)dir;
        if (mutableDir->m_cachedEntries) {
            XStringList_delete_base(mutableDir->m_cachedEntries);
        }
        mutableDir->m_cachedEntries = list;
        mutableDir->m_cachedFilters = dir->m_filters;
        mutableDir->m_cachedSorting = dir->m_sorting;
        mutableDir->m_cacheValid = true;
    }
    
    return XStringList_size_base(dir->m_cachedEntries);
}

XString* XDir_at(const XDir* dir, size_t pos)
{
    if (!dir) return NULL;
    
    // 确保 count() 已经更新了缓�?
    size_t count = XDir_count(dir);
    
    if (pos >= count || !dir->m_cachedEntries) {
        return NULL;
    }
    
    const XString* entry = XStringList_at_base(dir->m_cachedEntries, pos);
    return XString_create_copy(entry);
}

XStringList* XDir_entryList_1(const XDir* dir, XDirFilters filters, XDirSortFlags sort)
{
    if (!dir) return NULL;
    
    // 确定实际使用的过滤器和排�?
    XDirFilters actualFilters = (filters == XDir_NoFilter) ? dir->m_filters : filters;
    XDirSortFlags actualSort = (sort == XDir_NoSort) ? dir->m_sorting : sort;
    
    // 检查缓存是否有效且过滤器和排序匹配
    if (dir->m_cacheValid && dir->m_cachedEntries &&
        dir->m_cachedFilters == actualFilters &&
        dir->m_cachedSorting == actualSort) {
        // 返回缓存的副�?
        return XStringList_create_copy(dir->m_cachedEntries);
    }
    
    // 缓存无效或参数不匹配，需要重新获�?
    return XDir_entryList_2(dir, dir->m_nameFilters, filters, sort);
}

bool XDir_isEmpty(const XDir* dir, XDirFilters filters)
{
    if (!dir) return true;
    
    XDirFilters actualFilters = (filters == XDir_NoFilter) ?
        (XDir_AllEntries | XDir_NoDotAndDotDot) : filters;
    
    XStringList* list = XDir_entryList_1(dir, actualFilters, XDir_NoSort);
    if (!list) return true;
    
    bool empty = (XStringList_size_base(list) == 0);
    XStringList_delete_base(list);
    
    return empty;
}

void XDir_refresh(XDir* dir)
{
    if (!dir) return;
    
    // 使缓存失�?
    dir->m_cacheValid = false;
    
    // 释放缓存的条目列�?
    if (dir->m_cachedEntries) {
        XStringList_delete_base(dir->m_cachedEntries);
        dir->m_cachedEntries = NULL;
    }
}

bool XDir_isRelativePath(const XString* path)
{
    return !XDir_isAbsolutePath(path);
}

XDir* XDir_home(void)
{
    XString* path = XDir_homePath();
    if (!path) return NULL;
    
    XDir* dir = XDir_create_2(path);
    XString_delete_base(path);
    return dir;
}

XDir* XDir_root(void)
{
    XString* path = XDir_rootPath();
    if (!path) return NULL;
    
    XDir* dir = XDir_create_2(path);
    XString_delete_base(path);
    return dir;
}

XDir* XDir_temp(void)
{
    XString* path = XDir_tempPath();
    if (!path) return NULL;
    
    XDir* dir = XDir_create_2(path);
    XString_delete_base(path);
    return dir;
}

XString* XDir_absoluteFilePath(const XDir* dir, const XString* fileName)
{
    if (!dir || !dir->m_path || !fileName) return NULL;
    
    XString* absPath = XDir_absolutePath(dir);
    if (!absPath) return NULL;
    
    // 构建临时 XDir 对象来使�?XDir_filePath（统一使用 '/' 分隔符）
    XDir tempDir;
    tempDir.m_path = absPath;
    
    XString* result = XDir_filePath(&tempDir, fileName);
    
    XString_delete_base(absPath);
    return result;
}

XString* XDir_cleanPath(const XString* path)
{
    if (!path) return NULL;
    
    const char* pathUtf8 = XString_toUtf8(path);
    if (!pathUtf8) return NULL;
    
    size_t len = strlen(pathUtf8);
    if (len == 0) return XString_create();
    
    // 使用栈数组处理路径（避免动态分配）
    char* result = (char*)XMalloc_System(len + 1);
    if (!result) return NULL;
    
    // 第一步：统一分隔符为 '/'
    for (size_t i = 0; i <= len; i++) {
        result[i] = (pathUtf8[i] == '\\') ? '/' : pathUtf8[i];
    }
    
    // 第二步：处理 "." �?".." 以及冗余�?"//"
    // 使用指针数组存储路径的各个部�?
    char** parts = (char**)XMalloc_System(sizeof(char*) * (len + 1));
    if (!parts) {
        XFree_System(result);
        return NULL;
    }
    
    int partCount = 0;
    char* p = result;
    
    // 检查是否有驱动器前缀 (Windows: "C:/")
    bool hasDrivePrefix = false;
    if (len >= 2 && result[1] == ':' && 
        ((result[0] >= 'A' && result[0] <= 'Z') || 
         (result[0] >= 'a' && result[0] <= 'z'))) {
        hasDrivePrefix = true;
    }
    
    // 检查是否以 '/' 开头（绝对路径�?UNC�?
    bool isAbsolute = (result[0] == '/');
    
    // 分割路径并处�?"." �?".."
    while (*p) {
        // 跳过多余�?'/'
        while (*p == '/') p++;
        if (!*p) break;
        
        // 记录这部分路径的起始位置
        char* start = p;
        
        // 找到这部分路径的结束位置
        while (*p && *p != '/') p++;
        
        size_t partLen = p - start;
        
        // 处理 "."
        if (partLen == 1 && start[0] == '.') {
            continue;  // 忽略 "."
        }
        
        // 处理 ".."
        if (partLen == 2 && start[0] == '.' && start[1] == '.') {
            if (partCount > 0) {
                // 回退一级（但不能越过驱动器前缀或根目录�?
                if (isAbsolute && partCount == 1) {
                    // 已经在根目录，不能回退
                } else if (hasDrivePrefix && partCount == 1) {
                    // 已经在驱动器根目录，不能回退
                } else {
                    partCount--;  // 移除上一�?
                }
            }
            continue;
        }
        
        // 保存这部分路�?
        start[partLen] = '\0';  // 临时截断
        parts[partCount++] = start;
    }
    
    // 第三步：重建路径
    char* finalPath = (char*)XMalloc_System(len + 1);
    if (!finalPath) {
        XFree_System(result);
        XFree_System(parts);
        return NULL;
    }
    
    finalPath[0] = '\0';
    size_t pos = 0;
    
    // 添加驱动器前缀
    if (hasDrivePrefix) {
        finalPath[0] = result[0];
        finalPath[1] = ':';
        finalPath[2] = '\0';
        pos = 2;
    }
    
    // 添加根目录标�?
    if (isAbsolute) {
        finalPath[pos++] = '/';
        finalPath[pos] = '\0';
    }
    
    // 添加各个路径部分
    for (int i = 0; i < partCount; i++) {
        if (pos > 0 && finalPath[pos - 1] != '/') {
            finalPath[pos++] = '/';
        }
        size_t partLen = strlen(parts[i]);
        memcpy(finalPath + pos, parts[i], partLen);
        pos += partLen;
    }
    
    finalPath[pos] = '\0';
    
    // 处理空路径情�?
    if (pos == 0) {
        finalPath[0] = '.';
        finalPath[1] = '\0';
    }
    
    XString* cleanStr = XString_create_utf8(finalPath);
    
    XFree_System(result);
    XFree_System(parts);
    XFree_System(finalPath);
    
    return cleanStr;
}

/* ============================================================================
 * 搜索路径（平台无关）
 * ============================================================================ */

#define MAX_SEARCH_PREFIXES 32

typedef struct {
    XString* prefix;
    XStringList* paths;
} XDirSearchPathEntry;

static XDirSearchPathEntry g_searchPaths[MAX_SEARCH_PREFIXES];
static int g_searchPathCount = 0;

void XDir_setSearchPaths(const XString* prefix, const XStringList* searchPaths)
{
    if (!prefix) return;
    
    const char* prefixUtf8 = XString_toUtf8(prefix);
    
    // 查找现有条目
    for (int i = 0; i < g_searchPathCount; i++) {
        const char* existingPrefix = XString_toUtf8(g_searchPaths[i].prefix);
        
        if (existingPrefix && prefixUtf8 && strcmp(existingPrefix, prefixUtf8) == 0) {
            // 替换现有条目
            if (g_searchPaths[i].paths) {
                XStringList_delete_base(g_searchPaths[i].paths);
            }
            g_searchPaths[i].paths = XStringList_create_copy(searchPaths);
            return;
        }
    }
    
    // 添加新条�?
    if (g_searchPathCount < MAX_SEARCH_PREFIXES) {
        g_searchPaths[g_searchPathCount].prefix = XString_create_copy(prefix);
        g_searchPaths[g_searchPathCount].paths = XStringList_create_copy(searchPaths);
        g_searchPathCount++;
    }
}

void XDir_addSearchPath(const XString* prefix, const XString* path)
{
    if (!prefix || !path) return;
    
    const char* prefixUtf8 = XString_toUtf8(prefix);
    
    // 查找现有条目
    for (int i = 0; i < g_searchPathCount; i++) {
        const char* existingPrefix = XString_toUtf8(g_searchPaths[i].prefix);
        
        if (existingPrefix && strcmp(existingPrefix, prefixUtf8) == 0) {
            // 添加到现有条�?
            if (g_searchPaths[i].paths) {
                XStringList_push_back_base(g_searchPaths[i].paths, path);
            }
            return;
        }
    }
    
    // 创建新条�?
    if (g_searchPathCount < MAX_SEARCH_PREFIXES) {
        g_searchPaths[g_searchPathCount].prefix = XString_create_copy(prefix);
        g_searchPaths[g_searchPathCount].paths = XStringList_create();
        XStringList_push_back_base(g_searchPaths[g_searchPathCount].paths, path);
        g_searchPathCount++;
    }
}

XStringList* XDir_searchPaths(const XString* prefix)
{
    if (!prefix) return NULL;
    
    const char* prefixUtf8 = XString_toUtf8(prefix);
    
    for (int i = 0; i < g_searchPathCount; i++) {
        const char* existingPrefix = XString_toUtf8(g_searchPaths[i].prefix);
        
        if (existingPrefix && strcmp(existingPrefix, prefixUtf8) == 0) {
            return XStringList_create_copy(g_searchPaths[i].paths);
        }
    }
    
    return XStringList_create();
}

/* ============================================================================
 * 目录内容（使�?XFileSystem API�?
 * ============================================================================ */

XStringList* XDir_entryList_2(const XDir* dir, const XStringList* nameFilters,
                              XDirFilters filters, XDirSortFlags sort)
{
    if (!dir || !dir->m_path) return NULL;
    
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return NULL;
    
    XDirFilters actualFilters = (filters == XDir_NoFilter) ? dir->m_filters : filters;
    XDirSortFlags actualSort = (sort == XDir_NoSort) ? dir->m_sorting : sort;
    
     XVector* entryInfos = XVector_create(sizeof(XDirEntryInfo));
    if (!entryInfos) return NULL;
    
    XDirIterator iter = XFileSystem_opendir(pathUtf8);
    if (!iter) {
        XVector_delete_base(entryInfos);
        return XStringList_create();
    }
    
    XDirEntry entry;
    while (XFileSystem_readdir(iter, &entry)) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) {
            if ((actualFilters & XDir_NoDotAndDotDot) == XDir_NoDotAndDotDot) continue;
        }
        
        if ((actualFilters & XDir_Dirs) && !entry.isDir) continue;
        if ((actualFilters & XDir_Files) && entry.isFile) continue;
        if ((actualFilters & XDir_Hidden) && !entry.isHidden) continue;
        if ((actualFilters & XDir_NoSymLinks) && entry.isSymLink) continue;
        
        if (nameFilters && XStringList_size_base(nameFilters) > 0) {
            XString* fileName = XString_create_utf8(entry.name);
            bool matched = XDir_match_2(nameFilters, fileName);
            XString_delete_base(fileName);
            if (!matched) continue;
        }
        
        XDirEntryInfo info;
        info.name = XStrdup(entry.name);
        info.isDir = entry.isDir;
        info.size = 0;
        info.time = 0;
        XVector_push_back_1_base(entryInfos, &info);
    }
    
    XFileSystem_closedir(iter);
    
    if ((actualSort & XDir_SortByMask) != XDir_Unsorted && XVector_size_base(entryInfos) > 1) {
        g_sortContext.flags = actualSort;
        XContainerSetCompare(entryInfos, XDirEntryInfo_compare);
        XVector_sort_base(entryInfos, XSORT_ASC);
    }
    
    XStringList* result = XStringList_create();
    size_t count = XVector_size_base(entryInfos);
    for (size_t i = 0; i < count; i++) {
        XDirEntryInfo* info = (XDirEntryInfo*)XVector_at_base(entryInfos, i);
        if (info->name) {
            XStringList_push_back_utf8(result, info->name);
            XFree_System((void*)info->name);
        }
    }
    
    XVector_delete_base(entryInfos);
    return result;
}

bool XDir_mkdir(XDir* dir, const XString* dirName)
{
    if (!dir || !dirName) return false;
    XString* fullPath = XDir_filePath(dir, dirName);
    if (!fullPath) return false;
    const char* pathUtf8 = XString_toUtf8(fullPath);
    bool result = XFileSystem_mkdir(pathUtf8, false);
    XString_delete_base(fullPath);
    return result;
}

bool XDir_mkpath(XDir* dir, const XString* dirPath)
{
    if (!dir || !dirPath) return false;
    XString* fullPath = XDir_filePath(dir, dirPath);
    if (!fullPath) return false;
    const char* pathUtf8 = XString_toUtf8(fullPath);
    bool result = XFileSystem_mkdir(pathUtf8, true);
    XString_delete_base(fullPath);
    return result;
}

bool XDir_rmdir(XDir* dir, const XString* dirName)
{
    if (!dir || !dirName) return false;
    XString* fullPath = XDir_filePath(dir, dirName);
    if (!fullPath) return false;
    const char* pathUtf8 = XString_toUtf8(fullPath);
    bool result = XFileSystem_rmdir(pathUtf8);
    XString_delete_base(fullPath);
    return result;
}

bool XDir_rmpath(XDir* dir, const XString* dirPath)
{
    if (!dir || !dirPath) return false;
    XString* fullPath = XDir_filePath(dir, dirPath);
    if (!fullPath) return false;
    const char* pathUtf8 = XString_toUtf8(fullPath);
    bool result = XFileSystem_rmdir(pathUtf8);
    XString_delete_base(fullPath);
    return result;
}

bool XDir_removeRecursively(XDir* dir)
{
    if (!dir || !dir->m_path) return false;
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    return XFileSystem_rmdir_recursive(pathUtf8);
}

bool XDir_remove(XDir* dir, const XString* fileName)
{
    if (!dir || !fileName) return false;
    XString* fullPath = XDir_filePath(dir, fileName);
    if (!fullPath) return false;
    const char* pathUtf8 = XString_toUtf8(fullPath);
    bool result = XFileSystem_remove(pathUtf8);
    XString_delete_base(fullPath);
    return result;
}

bool XDir_rename(XDir* dir, const XString* oldName, const XString* newName)
{
    if (!dir || !oldName || !newName) return false;
    XString* oldPath = XDir_filePath(dir, oldName);
    XString* newPath = XDir_filePath(dir, newName);
    if (!oldPath || !newPath) {
        if (oldPath) XString_delete_base(oldPath);
        if (newPath) XString_delete_base(newPath);
        return false;
    }
    const char* oldUtf8 = XString_toUtf8(oldPath);
    const char* newUtf8 = XString_toUtf8(newPath);
    bool result = XFileSystem_rename(oldUtf8, newUtf8);
    XString_delete_base(oldPath);
    XString_delete_base(newPath);
    return result;
}

bool XDir_exists_1(const XDir* dir)
{
    if (!dir || !dir->m_path) return false;
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    return XFileSystem_exists(pathUtf8);
}

bool XDir_exists_2(const XDir* dir, const XString* name)
{
    if (!dir || !name) return false;
    XString* fullPath = XDir_filePath(dir, name);
    if (!fullPath) return false;
    const char* pathUtf8 = XString_toUtf8(fullPath);
    bool result = XFileSystem_exists(pathUtf8);
    XString_delete_base(fullPath);
    return result;
}

bool XDir_isReadable(const XDir* dir)
{
    if (!dir || !dir->m_path) return false;
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    XFileStat stat;
    if (!XFileSystem_stat(pathUtf8, &stat)) return false;
    return stat.isReadable;
}

bool XDir_isAbsolute(const XDir* dir)
{
    if (!dir || !dir->m_path) return false;
    return XDir_isAbsolutePath(dir->m_path);
}

bool XDir_isRoot(const XDir* dir)
{
    if (!dir || !dir->m_path) return false;
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    if (!pathUtf8) return false;
    XString* clean = XDir_cleanPath(dir->m_path);
    if (!clean) return false;
    const char* cleanUtf8 = XString_toUtf8(clean);
    size_t len = strlen(cleanUtf8);
    bool isRoot = false;
#ifdef _WIN32
    if (len == 3 && cleanUtf8[1] == ':' && 
        (cleanUtf8[2] == '\\' || cleanUtf8[2] == '/')) {
        isRoot = true;
    }
#else
    isRoot = (len == 1 && cleanUtf8[0] == '/');
#endif
    XString_delete_base(clean);
    return isRoot;
}

bool XDir_isAbsolutePath(const XString* path)
{
    if (!path) return false;
    const char* pathUtf8 = XString_toUtf8(path);
    if (!pathUtf8) return false;
#ifdef _WIN32
    if (isalpha((unsigned char)pathUtf8[0]) && pathUtf8[1] == ':') return true;
    if (pathUtf8[0] == '\\' && pathUtf8[1] == '\\') return true;
    return false;
#else
    return pathUtf8[0] == '/';
#endif
}

XString* XDir_absolutePath(const XDir* dir)
{
    if (!dir || !dir->m_path) return NULL;
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    char absPath[MAX_PATH];
    if (!XFileSystem_resolvePath(pathUtf8, absPath, MAX_PATH, XPathStyle_Absolute)) return NULL;
    return XString_create_utf8(absPath);
}

XString* XDir_canonicalPath(const XDir* dir)
{
    if (!dir || !dir->m_path) return NULL;
    const char* pathUtf8 = XString_toUtf8(dir->m_path);
    char canPath[MAX_PATH];
    if (!XFileSystem_resolvePath(pathUtf8, canPath, MAX_PATH, XPathStyle_Canonical)) return NULL;
    return XString_create_utf8(canPath);
}

XString* XDir_relativeFilePath(const XDir* dir, const XString* fileName)
{
    if (!dir || !dir->m_path || !fileName) return NULL;
    XString* absDir = XDir_absolutePath(dir);
    XString* absFile = XDir_absoluteFilePath(dir, fileName);
    if (!absDir || !absFile) {
        if (absDir) XString_delete_base(absDir);
        if (absFile) XString_delete_base(absFile);
        return NULL;
    }
    XString_delete_base(absDir);
    return absFile;
}

bool XDir_cd(XDir* dir, const XString* dirName)
{
    if (!dir || !dirName) return false;
    XString* newPath = XDir_filePath(dir, dirName);
    if (!newPath) return false;
    const char* pathUtf8 = XString_toUtf8(newPath);
    if (!XFileSystem_exists(pathUtf8)) {
        XString_delete_base(newPath);
        return false;
    }
    XFileStat stat;
    if (!XFileSystem_stat(pathUtf8, &stat) || !stat.isDir) {
        XString_delete_base(newPath);
        return false;
    }
    XString_delete_base(dir->m_path);
    dir->m_path = newPath;
    dir->m_cacheValid = false;
    return true;
}

bool XDir_cdUp(XDir* dir)
{
    if (!dir || !dir->m_path) return false;
    XString* parent = XString_create_utf8("..");
    bool result = XDir_cd(dir, parent);
    XString_delete_base(parent);
    return result;
}

XDir* XDir_current(void)
{
    XString* path = XDir_currentPath();
    if (!path) return NULL;
    XDir* dir = XDir_create_2(path);
    XString_delete_base(path);
    return dir;
}

XString* XDir_currentPath(void)
{
    char path[MAX_PATH];
    if (!XFileSystem_currentPath(path, MAX_PATH)) return NULL;
    return XString_create_utf8(path);
}

bool XDir_setCurrent(const XString* path)
{
    if (!path) return false;
    const char* pathUtf8 = XString_toUtf8(path);
    return XFileSystem_setCurrentPath(pathUtf8);
}

XString* XDir_homePath(void)
{
    char path[MAX_PATH];
    if (!XFileSystem_homePath(path, MAX_PATH)) return NULL;
    return XString_create_utf8(path);
}

XString* XDir_rootPath(void)
{
    char path[MAX_PATH];
    if (!XFileSystem_rootPath(path, MAX_PATH)) return NULL;
    return XString_create_utf8(path);
}

XString* XDir_tempPath(void)
{
    char path[MAX_PATH];
    if (!XFileSystem_tempPath(path, MAX_PATH)) return NULL;
    return XString_create_utf8(path);
}

XStringList* XDir_drives(void)
{
    XStringList* result = XStringList_create();
    char drives[26][16];
    int count = XFileSystem_drives(drives, 26);
    for (int i = 0; i < count; i++) {
        XStringList_push_back_utf8(result, drives[i]);
    }
    return result;
}

/* ============================================================================
 * 目录条目信息列表
 * ============================================================================ */

XFileInfoList* XDir_entryInfoList_1(const XDir* dir, XDirFilters filters, XDirSortFlags sort)
{
    return XDir_entryInfoList_2(dir, NULL, filters, sort);
}

XFileInfoList* XDir_entryInfoList_2(const XDir* dir, const XStringList* nameFilters,
                                    XDirFilters filters, XDirSortFlags sort)
{
    if (!dir || !dir->m_path) return NULL;
    
    XStringList* names = XDir_entryList_2(dir, nameFilters, filters, sort);
    if (!names) return NULL;
    
    XFileInfoList* result = XVector_create(sizeof(XFileInfo));
        if (!result) {
            XStringList_delete_base(names);
            return NULL;
        }
    
        // 设置元素操作方法（XFileInfo内部有动态分配成员，需要正确的拷贝/移动/释放
        XContainerSetDataCopyMethod(result, (XCDataCopyMethod)XFileInfo_copy_base);
        XContainerSetDataMoveMethod(result, (XCDataMoveMethod)XFileInfo_move_base);
        XContainerSetDataDeinitMethod(result, (XCDataDeinitMethod)XFileInfo_deinit_base);
    
        size_t count = XStringList_size_base(names);
        for (size_t i = 0; i < count; i++) {
            const XString* name = XStringList_at_base(names, i);
            XString* fullPath = XDir_filePath(dir, name);
            if (fullPath) {
                XFileInfo info;
                XFileInfo_init_1(&info);
                XFileInfo_setFile_1(&info, fullPath);
                XVector_push_back_1_base(result, &info);
                XFileInfo_deinit_base(&info);
                XString_delete_base(fullPath);
            }
        }
    
    XStringList_delete_base(names);
    return result;
}