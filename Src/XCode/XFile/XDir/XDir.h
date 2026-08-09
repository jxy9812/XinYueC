#ifndef XDIR_H
#define XDIR_H
#include "XFileSystem_config.h"

/**
 * @file XDir.h
 * @brief 目录操作类，提供对目录结构和内容的访问
 * 
 * 移植自 Qt 6.8 QDir 类，提供跨平台的目录操作功能。
 */

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XStringList.h"
#include "XVector.h"
#include "XFileInfo.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XFILE_ON
#if XDIR_ON

/* ============================================================================
 * 枚举定义
 * ============================================================================ */

/**
 * @brief 目录过滤选项
 */
typedef enum XDirFilter {
    XDir_Dirs          = 0x001,  /**< 列出匹配过滤器的目录 */
    XDir_AllDirs       = 0x400,  /**< 列出所有目录，不应用过滤器 */
    XDir_Files         = 0x002,  /**< 列出文件 */
    XDir_Drives        = 0x004,  /**< 列出磁盘驱动器（Unix下忽略） */
    XDir_NoSymLinks    = 0x008,  /**< 不列出符号链接 */
    XDir_AllEntries    = XDir_Dirs | XDir_Files | XDir_Drives,
    XDir_TypeMask      = 0x00f,
    
    XDir_Readable      = 0x010,  /**< 列出可读文件 */
    XDir_Writable      = 0x020,  /**< 列出可写文件 */
    XDir_Executable    = 0x040,  /**< 列出可执行文件 */
    XDir_PermissionMask = 0x070,
    
    XDir_Modified      = 0x080,  /**< 只列出已修改文件（Unix忽略） */
    XDir_Hidden        = 0x100,  /**< 列出隐藏文件 */
    XDir_System        = 0x200,  /**< 列出系统文件 */
    XDir_AccessMask    = 0x3F0,
    
    XDir_CaseSensitive = 0x800,  /**< 区分大小写 */
    XDir_NoDot         = 0x2000, /**< 不列出 "." */
    XDir_NoDotDot      = 0x4000, /**< 不列出 ".." */
    XDir_NoDotAndDotDot = XDir_NoDot | XDir_NoDotDot,
    XDir_NoFilter      = -1
} XDirFilter;

/**
 * @brief 目录排序选项
 */
typedef enum XDirSortFlag {
    XDir_Name        = 0x00,  /**< 按名称排序 */
    XDir_Time        = 0x01,  /**< 按时间排序 */
    XDir_Size        = 0x02,  /**< 按大小排序 */
    XDir_Unsorted    = 0x03,  /**< 不排序 */
    XDir_SortByMask  = 0x03,
    
    XDir_DirsFirst   = 0x04,  /**< 目录优先 */
    XDir_Reversed    = 0x08,  /**< 反向排序 */
    XDir_IgnoreCase  = 0x10,  /**< 忽略大小写 */
    XDir_DirsLast    = 0x20,  /**< 文件优先 */
    XDir_LocaleAware = 0x40,  /**< 使用本地化排序 */
    XDir_Type        = 0x80,  /**< 按类型（扩展名）排序 */
    XDir_NoSort      = -1
} XDirSortFlag;

/* 过滤器和排序标志的位掩码类型 */
typedef int XDirFilters;
typedef int XDirSortFlags;

/* ============================================================================
 * 虚函数表定义
 * ============================================================================ */

XCLASS_DEFINE_BEGING(XDir)
XCLASS_DEFINE_EXTEND_END(XDir, XClass)

/* ============================================================================
 * 结构体定义
 * ============================================================================ */

/**
 * @brief XDir 结构体，表示一个目录
 */
typedef struct XDir {
    XClass m_class;               /**< 基类 */
    XString* m_path;              /**< 目录路径 */
    XStringList* m_nameFilters;   /**< 名称过滤器列表 */
    XDirFilters m_filters;        /**< 过滤器标志 */
    XDirSortFlags m_sorting;      /**< 排序标志 */
    
    /* 缓存字段 */
    XStringList* m_cachedEntries; /**< 缓存的条目列表 */
    XDirFilters m_cachedFilters;  /**< 缓存时的过滤器 */
    XDirSortFlags m_cachedSorting; /**< 缓存时的排序标志 */
    bool m_cacheValid;            /**< 缓存是否有效 */
} XDir;

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

/**
 * @brief 初始化 XDir 类的虚函数表
 * @return 虚函数表指针
 */
XVtable* XDir_class_init(void);

/* ============================================================================
 * 构造与析构（继承自 XClass）
 * ============================================================================ */

#define XDir_delete_base    XClass_delete_base
#define XDir_deinit_base    XClass_deinit_base
#define XDir_copy_base      XClass_copy_base
#define XDir_move_base      XClass_move_base

/**
 * @brief 创建一个指向当前目录的 XDir 对象
 * @return XDir 对象指针，失败返回 NULL
 */
XDir* XDir_create_1(void);

/**
 * @brief 创建一个指向指定路径的 XDir 对象
 * @param path 目录路径（XString对象）
 * @return XDir 对象指针，失败返回 NULL
 */
XDir* XDir_create_2(const XString* path);

/**
 * @brief 创建一个带有名称过滤器的 XDir 对象
 * @param path 目录路径（XString对象）
 * @param nameFilters 名称过滤器列表（XStringList对象）
 * @param sort 排序标志
 * @param filters 过滤器标志
 * @return XDir 对象指针，失败返回 NULL
 */
XDir* XDir_create_3(const XString* path, const XStringList* nameFilters,
                    XDirSortFlags sort, XDirFilters filters);

/**
 * @brief 初始化 XDir 对象（指向当前目录）
 * @param dir XDir 对象指针
 */
void XDir_init_1(XDir* dir);

/**
 * @brief 初始化 XDir 对象（指定路径）
 * @param dir XDir 对象指针
 * @param path 目录路径（XString对象）
 */
void XDir_init_2(XDir* dir, const XString* path);

/**
 * @brief 初始化 XDir 对象（带名称过滤器）
 * @param dir XDir 对象指针
 * @param path 目录路径（XString对象）
 * @param nameFilters 名称过滤器列表（XStringList对象）
 * @param sort 排序标志
 * @param filters 过滤器标志
 */
void XDir_init_3(XDir* dir, const XString* path, const XStringList* nameFilters,
                 XDirSortFlags sort, XDirFilters filters);

/* ============================================================================
 * 路径操作
 * ============================================================================ */

/**
 * @brief 设置目录路径
 * @param dir XDir 对象指针
 * @param path 新路径（XString对象）
 */
void XDir_setPath(XDir* dir, const XString* path);

/**
 * @brief 获取目录路径
 * @param dir XDir 对象指针
 * @return 目录路径（XString指针，内部数据，不要释放）
 */
const XString* XDir_path(const XDir* dir);

/**
 * @brief 获取绝对路径
 * @param dir XDir 对象指针
 * @return 绝对路径（新创建的XString，需要调用者释放）
 */
XString* XDir_absolutePath(const XDir* dir);

/**
 * @brief 获取规范路径（解析符号链接和 "." ".."）
 * @param dir XDir 对象指针
 * @return 规范路径（新创建的XString，需要调用者释放），失败返回 NULL
 */
XString* XDir_canonicalPath(const XDir* dir);

/**
 * @brief 获取目录名称（路径最后一部分）
 * @param dir XDir 对象指针
 * @return 目录名称（新创建的XString，需要调用者释放）
 */
XString* XDir_dirName(const XDir* dir);

/**
 * @brief 获取目录中指定文件的路径
 * @param dir XDir 对象指针
 * @param fileName 文件名（XString对象）
 * @return 文件路径（新创建的XString，需要调用者释放）
 */
XString* XDir_filePath(const XDir* dir, const XString* fileName);

/**
 * @brief 获取目录中指定文件的绝对路径
 * @param dir XDir 对象指针
 * @param fileName 文件名（XString对象）
 * @return 绝对文件路径（新创建的XString，需要调用者释放）
 */
XString* XDir_absoluteFilePath(const XDir* dir, const XString* fileName);

/**
 * @brief 获取指定文件相对于当前目录的相对路径
 * @param dir XDir 对象指针
 * @param fileName 文件名（XString对象）
 * @return 相对路径（新创建的XString，需要调用者释放）
 */
XString* XDir_relativeFilePath(const XDir* dir, const XString* fileName);

/* ============================================================================
 * 目录导航
 * ============================================================================ */

/**
 * @brief 切换到指定子目录
 * @param dir XDir 对象指针
 * @param dirName 子目录名称（XString对象）
 * @return 成功返回 true，失败返回 false
 */
bool XDir_cd(XDir* dir, const XString* dirName);

/**
 * @brief 切换到上级目录
 * @param dir XDir 对象指针
 * @return 成功返回 true，失败返回 false
 */
bool XDir_cdUp(XDir* dir);

/* ============================================================================
 * 目录内容
 * ============================================================================ */

/**
 * @brief 获取目录中的条目数量
 * @param dir XDir 对象指针
 * @return 条目数量
 */
size_t XDir_count(const XDir* dir);

/**
 * @brief 获取目录中指定位置的条目名称
 * @param dir XDir 对象指针
 * @param pos 索引位置
 * @return 条目名称（新创建的XString，需要调用者释放）
 */
XString* XDir_at(const XDir* dir, size_t pos);

/**
 * @brief 获取目录条目名称列表（使用默认过滤器和排序）
 * @param dir XDir 对象指针
 * @param filters 过滤器标志，使用 XDir_NoFilter 表示使用默认过滤器
 * @param sort 排序标志，使用 XDir_NoSort 表示使用默认排序
 * @return XStringList 指针（需要调用者释放）
 */
XStringList* XDir_entryList_1(const XDir* dir, XDirFilters filters, XDirSortFlags sort);

/**
 * @brief 使用指定名称过滤器获取目录条目名称列表
 * @param dir XDir 对象指针
 * @param nameFilters 名称过滤器列表（XStringList对象）
 * @param filters 过滤器标志
 * @param sort 排序标志
 * @return XStringList 指针（需要调用者释放）
 */
XStringList* XDir_entryList_2(const XDir* dir, const XStringList* nameFilters,
                              XDirFilters filters, XDirSortFlags sort);

/**
 * @brief 获取目录条目信息列表（使用默认过滤器和排序）
 * @param dir XDir 对象指针
 * @param filters 过滤器标志，使用 XDir_NoFilter 表示使用默认过滤器
 * @param sort 排序标志，使用 XDir_NoSort 表示使用默认排序
 * @return XFileInfo 数组指针（需要调用者释放，使用 XFileInfo_delete_base 释放每个元素）
 */
XFileInfoList* XDir_entryInfoList_1(const XDir* dir, XDirFilters filters, XDirSortFlags sort);

/**
 * @brief 使用指定名称过滤器获取目录条目信息列表
 * @param dir XDir 对象指针
 * @param nameFilters 名称过滤器列表（XStringList对象）
 * @param filters 过滤器标志
 * @param sort 排序标志
 * @return XFileInfo 数组指针（需要调用者释放）
 */
XFileInfoList* XDir_entryInfoList_2(const XDir* dir, const XStringList* nameFilters,
                               XDirFilters filters, XDirSortFlags sort);
/**
 * @brief 检查目录是否为空
 * @param dir XDir 对象指针
 * @param filters 过滤器标志
 * @return 为空返回 true，否则返回 false
 */
bool XDir_isEmpty(const XDir* dir, XDirFilters filters);

/* ============================================================================
 * 目录操作
 * ============================================================================ */

/**
 * @brief 创建子目录
 * @param dir XDir 对象指针
 * @param dirName 子目录名称（XString对象）
 * @return 成功返回 true，失败返回 false
 */
bool XDir_mkdir(XDir* dir, const XString* dirName);

/**
 * @brief 递归创建目录路径
 * @param dir XDir 对象指针
 * @param dirPath 目录路径（XString对象）
 * @return 成功返回 true，失败返回 false
 */
bool XDir_mkpath(XDir* dir, const XString* dirPath);

/**
 * @brief 删除子目录
 * @param dir XDir 对象指针
 * @param dirName 子目录名称（XString对象）
 * @return 成功返回 true，失败返回 false
 */
bool XDir_rmdir(XDir* dir, const XString* dirName);

/**
 * @brief 递归删除目录路径
 * @param dir XDir 对象指针
 * @param dirPath 目录路径（XString对象）
 * @return 成功返回 true，失败返回 false
 */
bool XDir_rmpath(XDir* dir, const XString* dirPath);

/**
 * @brief 递归删除目录及其所有内容
 * @param dir XDir 对象指针
 * @return 成功返回 true，失败返回 false
 */
bool XDir_removeRecursively(XDir* dir);

/**
 * @brief 删除文件
 * @param dir XDir 对象指针
 * @param fileName 文件名（XString对象）
 * @return 成功返回 true，失败返回 false
 */
bool XDir_remove(XDir* dir, const XString* fileName);

/**
 * @brief 重命名文件或目录
 * @param dir XDir 对象指针
 * @param oldName 原名称（XString对象）
 * @param newName 新名称（XString对象）
 * @return 成功返回 true，失败返回 false
 */
bool XDir_rename(XDir* dir, const XString* oldName, const XString* newName);

/* ============================================================================
 * 状态检查
 * ============================================================================ */

/**
 * @brief 检查目录是否存在
 * @param dir XDir 对象指针
 * @return 存在返回 true，否则返回 false
 */
bool XDir_exists_1(const XDir* dir);

/**
 * @brief 检查指定名称的文件或目录是否存在
 * @param dir XDir 对象指针
 * @param name 名称（XString对象）
 * @return 存在返回 true，否则返回 false
 */
bool XDir_exists_2(const XDir* dir, const XString* name);

/**
 * @brief 检查目录是否可读
 * @param dir XDir 对象指针
 * @return 可读返回 true，否则返回 false
 */
bool XDir_isReadable(const XDir* dir);

/**
 * @brief 检查路径是否为绝对路径
 * @param dir XDir 对象指针
 * @return 是绝对路径返回 true，否则返回 false
 */
bool XDir_isAbsolute(const XDir* dir);

/**
 * @brief 检查路径是否为相对路径
 * @param dir XDir 对象指针
 * @return 是相对路径返回 true，否则返回 false
 */
bool XDir_isRelative(const XDir* dir);

/**
 * @brief 检查是否为根目录
 * @param dir XDir 对象指针
 * @return 是根目录返回 true，否则返回 false
 */
bool XDir_isRoot(const XDir* dir);

/**
 * @brief 将相对路径转换为绝对路径
 * @param dir XDir 对象指针
 * @return 成功返回 true，失败返回 false
 */
bool XDir_makeAbsolute(XDir* dir);

/* ============================================================================
 * 过滤器和排序
 * ============================================================================ */

/**
 * @brief 设置过滤器
 * @param dir XDir 对象指针
 * @param filters 过滤器标志
 */
void XDir_setFilter(XDir* dir, XDirFilters filters);

/**
 * @brief 获取过滤器
 * @param dir XDir 对象指针
 * @return 过滤器标志
 */
XDirFilters XDir_filter(const XDir* dir);

/**
 * @brief 设置排序标志
 * @param dir XDir 对象指针
 * @param sort 排序标志
 */
void XDir_setSorting(XDir* dir, XDirSortFlags sort);

/**
 * @brief 获取排序标志
 * @param dir XDir 对象指针
 * @return 排序标志
 */
XDirSortFlags XDir_sorting(const XDir* dir);

/**
 * @brief 设置名称过滤器
 * @param dir XDir 对象指针
 * @param nameFilters 过滤器列表（XStringList对象）
 */
void XDir_setNameFilters(XDir* dir, const XStringList* nameFilters);

/**
 * @brief 获取名称过滤器
 * @param dir XDir 对象指针
 * @return 名称过滤器列表（内部数据，不要释放）
 */
const XStringList* XDir_nameFilters(const XDir* dir);

/**
 * @brief 刷新目录信息
 * @param dir XDir 对象指针
 */
void XDir_refresh(XDir* dir);

/* ============================================================================
 * 静态函数 - 路径分隔符
 * ============================================================================ */

/**
 * @brief 获取目录分隔符
 * @return 目录分隔符字符（XChar）
 */
XChar XDir_separator(void);

/**
 * @brief 获取路径列表分隔符
 * @return 路径列表分隔符字符（XChar）
 */
XChar XDir_listSeparator(void);

/**
 * @brief 将路径转换为本地分隔符
 * @param pathName 路径名（XString对象）
 * @return 转换后的路径（新创建的XString，需要调用者释放）
 */
XString* XDir_toNativeSeparators(const XString* pathName);

/**
 * @brief 将本地分隔符转换为 "/"
 * @param pathName 路径名（XString对象）
 * @return 转换后的路径（新创建的XString，需要调用者释放）
 */
XString* XDir_fromNativeSeparators(const XString* pathName);

/**
 * @brief 清理路径（移除多余的 "." ".." 和分隔符）
 * @param path 路径（XString对象）
 * @return 清理后的路径（新创建的XString，需要调用者释放）
 */
XString* XDir_cleanPath(const XString* path);

/**
 * @brief 检查路径是否为绝对路径（静态版本）
 * @param path 路径（XString对象）
 * @return 是绝对路径返回 true，否则返回 false
 */
bool XDir_isAbsolutePath(const XString* path);

/**
 * @brief 检查路径是否为相对路径（静态版本）
 * @param path 路径（XString对象）
 * @return 是相对路径返回 true，否则返回 false
 */
bool XDir_isRelativePath(const XString* path);

/**
 * @brief 匹配文件名与过滤器
 * @param filters 过滤器模式（XString对象，支持 * 和 ? 通配符）
 * @param fileName 文件名（XString对象）
 * @return 匹配返回 true，否则返回 false
 */
bool XDir_match_1(const XString* filter, const XString* fileName);

/**
 * @brief 匹配文件名与多个过滤器
 * @param filters 过滤器列表（XStringList对象）
 * @param fileName 文件名（XString对象）
 * @return 匹配任一过滤器返回 true，否则返回 false
 */
bool XDir_match_2(const XStringList* filters, const XString* fileName);

/**
 * @brief 从过滤器字符串解析过滤器列表
 * @param nameFilter 过滤器字符串（如 "*.cpp *.h"）
 * @return XStringList 指针（需要调用者释放）
 */
XStringList* XDir_nameFiltersFromString(const XString* nameFilter);

/* ============================================================================
 * 静态函数 - 特殊目录
 * ============================================================================ */

/**
 * @brief 获取当前目录
 * @return 当前目录的 XDir 对象指针（需要调用者释放）
 */
XDir* XDir_current(void);

/**
 * @brief 获取当前目录路径
 * @return 当前目录路径（新创建的XString，需要调用者释放）
 */
XString* XDir_currentPath(void);

/**
 * @brief 设置当前目录
 * @param path 新的当前目录路径（XString对象）
 * @return 成功返回 true，失败返回 false
 */
bool XDir_setCurrent(const XString* path);

/**
 * @brief 获取用户主目录
 * @return 用户主目录的 XDir 对象指针（需要调用者释放）
 */
XDir* XDir_home(void);

/**
 * @brief 获取用户主目录路径
 * @return 用户主目录路径（新创建的XString，需要调用者释放）
 */
XString* XDir_homePath(void);

/**
 * @brief 获取根目录
 * @return 根目录的 XDir 对象指针（需要调用者释放）
 */
XDir* XDir_root(void);

/**
 * @brief 获取根目录路径
 * @return 根目录路径（新创建的XString，需要调用者释放）
 */
XString* XDir_rootPath(void);

/**
 * @brief 获取临时目录
 * @return 临时目录的 XDir 对象指针（需要调用者释放）
 */
XDir* XDir_temp(void);

/**
 * @brief 获取临时目录路径
 * @return 临时目录路径（新创建的XString，需要调用者释放）
 */
XString* XDir_tempPath(void);

/**
 * @brief 获取驱动器列表
 * @return XStringList 指针（需要调用者释放）
 */
XStringList* XDir_drives(void);

/* ============================================================================
 * 搜索路径
 * ============================================================================ */

/**
 * @brief 设置搜索路径
 * @param prefix 前缀（XString对象）
 * @param searchPaths 搜索路径列表（XStringList对象）
 */
void XDir_setSearchPaths(const XString* prefix, const XStringList* searchPaths);

/**
 * @brief 添加搜索路径
 * @param prefix 前缀（XString对象）
 * @param path 路径（XString对象）
 */
void XDir_addSearchPath(const XString* prefix, const XString* path);

/**
 * @brief 获取搜索路径
 * @param prefix 前缀（XString对象）
 * @return XStringList 指针（需要调用者释放）
 */
XStringList* XDir_searchPaths(const XString* prefix);

/* ============================================================================
 * 内部函数（供平台实现调用）
 * ============================================================================ */

/**
 * @brief 对条目列表进行排序（内部函数）
 * @param list 条目列表
 * @param flags 排序标志
 * @param sizes 文件大小数组（可为 NULL）
 * @param times 修改时间数组（可为 NULL）
 * @param isDirs 是否为目录数组（可为 NULL）
 */
void XDir_sortEntryList(XStringList* list, XDirSortFlags flags, 
                        const int64_t* sizes, const int64_t* times, const bool* isDirs);

/**
 * @brief 本地化字符串比较（内部函数，平台相关）
 * @param str1 字符串1
 * @param str2 字符串2
 * @param ignoreCase 是否忽略大小写
 * @return 比较结果：小于0表示str1<str2，等于0表示相等，大于0表示str1>str2
 */
int XDir_localeCompare(const char* str1, const char* str2, bool ignoreCase);

/**
 * @brief 区分大小写的通配符匹配（内部函数）
 * @param pattern 通配符模式（支持 * 和 ?）
 * @param str 要匹配的字符串
 * @return 匹配返回 true，否则返回 false
 */
bool matchWildcardCaseSensitive(const char* pattern, const char* str);

#endif // XDIR_ON
#endif /* XFILE_ON */
#ifdef __cplusplus
}
#endif

#endif // XDIR_H
