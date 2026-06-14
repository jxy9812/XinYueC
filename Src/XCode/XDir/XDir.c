#include "XDir.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 虚函数实现
 * ============================================================================ */

static void VXDir_copy(XDir* self, const XDir* other)
{
    if (!self || !other) return;
    
    // 拷贝路径
    if (self->m_path) {
        XString_delete_base(self->m_path);
    }
    self->m_path = other->m_path ? XString_create_copy(other->m_path) : NULL;
    
    // 拷贝名称过滤器
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
    
    // 移动名称过滤器
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
    
    // 释放名称过滤器
    if (self->m_nameFilters) {
        XStringList_delete_base(self->m_nameFilters);
        self->m_nameFilters = NULL;
    }
}

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XDir_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XDir))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_XCLASS(XClass);
        //重载虚函数
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
    
    // 移除文件名开头的分隔符
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
    dir->m_filters = filters;
}

XDirFilters XDir_filter(const XDir* dir)
{
    if (!dir) return XDir_NoFilter;
    return dir->m_filters;
}

void XDir_setSorting(XDir* dir, XDirSortFlags sort)
{
    if (!dir) return;
    dir->m_sorting = sort;
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
}

const XStringList* XDir_nameFilters(const XDir* dir)
{
    if (!dir) return NULL;
    return dir->m_nameFilters;
}

/* ============================================================================
 * 通配符匹配（平台无关）
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

bool XDir_match_1(const XString* filter, const XString* fileName)
{
    if (!filter || !fileName) return false;
    
    const char* filterUtf8 = XString_toUtf8(filter);
    const char* fileUtf8 = XString_toUtf8(fileName);
    
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
 * 分隔符转换（平台无关）
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
    // 将 '/' 转换为 '\'
    char* data = (char*)utf8;  // 假设可以修改
    while (*data) {
        if (*data == '/') *data = '\\';
        data++;
    }
#else
    (void)utf8;  // Unix 下不需要转换
#endif
    
    return result;
}

XString* XDir_fromNativeSeparators(const XString* pathName)
{
    if (!pathName) return NULL;
    
    XString* result = XString_create_copy(pathName);
    // 将 '\' 转换为 '/'
    const char* utf8 = XString_toUtf8(result);
    char* data = (char*)utf8;
    
    while (*data) {
        if (*data == '\\') *data = '/';
        data++;
    }
    
    return result;
}
/* ============================================================================
 * 名称过滤器解析（平台无关）
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
    XStringList* list = XDir_entryList_1(dir, XDir_NoFilter, XDir_NoSort);
    if (!list) return 0;
    
    size_t count = XStringList_size_base(list);
    XStringList_delete_base(list);
    return count;
}

XString* XDir_at(const XDir* dir, size_t pos)
{
    XStringList* list = XDir_entryList_1(dir, XDir_NoFilter, XDir_NoSort);
    if (!list) return NULL;
    
    if (pos >= XStringList_size_base(list)) {
        XStringList_delete_base(list);
        return NULL;
    }
    
    const XString* entry = XStringList_at_base(list, pos);
    XString* result = XString_create_copy(entry);
    
    XStringList_delete_base(list);
    return result;
}

XStringList* XDir_entryList_1(const XDir* dir, XDirFilters filters, XDirSortFlags sort)
{
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
    // 目录信息通常是实时获取的，此函数暂时为空
    (void)dir;
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
    
    // 添加新条目
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
            // 添加到现有条目
            if (g_searchPaths[i].paths) {
                XStringList_push_back_base(g_searchPaths[i].paths, path);
            }
            return;
        }
    }
    
    // 创建新条目
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
 * 以下函数依赖平台实现，在 Drive/windows/XDir_win32.c 或 
 * Drive/linux/XDir_linux.c 中实现
 * ============================================================================ */

// XString* XDir_absolutePath(const XDir* dir);
// XString* XDir_canonicalPath(const XDir* dir);
// XString* XDir_absoluteFilePath(const XDir* dir, const XString* fileName);
// XString* XDir_relativeFilePath(const XDir* dir, const XString* fileName);
// bool XDir_cd(XDir* dir, const XString* dirName);
// bool XDir_cdUp(XDir* dir);
// size_t XDir_count(const XDir* dir);
// XString* XDir_at(const XDir* dir, size_t pos);
// XStringList* XDir_entryList_1(const XDir* dir, XDirFilters filters, XDirSortFlags sort);
// XStringList* XDir_entryList_2(const XDir* dir, const XStringList* nameFilters, XDirFilters filters, XDirSortFlags sort);
// bool XDir_isEmpty(const XDir* dir, XDirFilters filters);
// bool XDir_mkdir(XDir* dir, const XString* dirName);
// bool XDir_mkpath(XDir* dir, const XString* dirPath);
// bool XDir_rmdir(XDir* dir, const XString* dirName);
// bool XDir_rmpath(XDir* dir, const XString* dirPath);
// bool XDir_removeRecursively(XDir* dir);
// bool XDir_remove(XDir* dir, const XString* fileName);
// bool XDir_rename(XDir* dir, const XString* oldName, const XString* newName);
// bool XDir_exists_1(const XDir* dir);
// bool XDir_exists_2(const XDir* dir, const XString* name);
// bool XDir_isReadable(const XDir* dir);
// bool XDir_isAbsolute(const XDir* dir);
// bool XDir_isRoot(const XDir* dir);
// bool XDir_isAbsolutePath(const XString* path);
// XString* XDir_cleanPath(const XString* path);
// XDir* XDir_current(void);
// XString* XDir_currentPath(void);
// bool XDir_setCurrent(const XString* path);
// XString* XDir_homePath(void);
// XString* XDir_rootPath(void);
// XString* XDir_tempPath(void);
// XStringList* XDir_drives(void);