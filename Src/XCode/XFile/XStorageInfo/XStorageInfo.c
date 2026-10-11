/**
 * @file       XStorageInfo.c
 * @brief      存储设备信息类实现（移植自 Qt 6.8 QStorageInfo）。
 * @details    XStorageInfo.h 契约的实现侧（模式同 Src/XCode/XSql/XSqlError.c：
 *             XClass 直系，零业务虚槽，仅重载 Copy/Move/Deinit）。容量/类型/
 *             状态等平台相关查询一律经 Drive 层既有原语取数，本文件不含任何
 *             平台 API：
 *             - 单点查询走 XDeviceFile_getStorageInfo（Win32=GetDiskFreeSpaceExW
 *               族，POSIX=statvfs + /proc/self/mounts，见 XDeviceFile.h §12），
 *               结果落入 XStorageInfoData 缓存；
 *             - 根卷/全量枚举走 XDeviceFile_enumerateDrives（POSIX 返回 "/"，
 *               Windows 枚举逻辑驱动器）。
 *             刷新语义对齐 Qt：refresh 失败保留旧缓存、仅标记缓存无效，访问器
 *             按头文件口径返回 -1/false/NULL。字符串成员所有权归对象，访问器
 *             返回借用指针。mountedVolumes 返回列表由调用方释放：先逐元素
 *             XClassDelete 再删列表本身。
 */
#include "XStorageInfo.h"

#include <string.h>

#include "XDeviceFile.h"
#include "XVector.h"
#include "XSystem.h" /* XSystem_environment：根卷系统盘判定（环境唯一入口） */

#if XFILE_ON
#if XSTORAGEINFO_ON

/* ========================================================================== */
/*                        内部工具函数                                         */
/* ========================================================================== */

/**
 * @brief 释放数据缓存中的四个字符串成员并整体清零。
 * @param info 对象指针；可为 NULL。
 */
static void xstorageinfo_clearData(XStorageInfo* info)
{
    if (!info) return;
    if (info->m_data.device) XClassDelete(info->m_data.device);
    if (info->m_data.fileSystemType) XClassDelete(info->m_data.fileSystemType);
    if (info->m_data.volumeName) XClassDelete(info->m_data.volumeName);
    if (info->m_data.subvolume) XClassDelete(info->m_data.subvolume);
    memset(&info->m_data, 0, sizeof(info->m_data));
    info->m_cacheValid = 0;
}

/**
 * @brief 替换挂载点路径并使缓存失效。
 * @param info 对象指针；可为 NULL。
 * @param path 新路径（XString 对象）；NULL 时仅清空旧路径。
 */
static void xstorageinfo_assignPath(XStorageInfo* info, const XString* path)
{
    if (!info) return;
    if (info->m_rootPath) {
        XClassDelete(info->m_rootPath);
        info->m_rootPath = NULL;
    }
    if (path) info->m_rootPath = XString_create_copy(path);
    xstorageinfo_clearData(info);
}

/* ========================================================================== */
/*                        虚函数实现（static，仅供 class_init 注册）            */
/* ========================================================================== */

static void VXStorageInfo_copy(XStorageInfo* dest, const XStorageInfo* src);
static void VXStorageInfo_move(XStorageInfo* dest, XStorageInfo* src);
static void VXStorageInfo_deinit(XStorageInfo* info);

XVtable* XStorageInfo_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStorageInfo)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXStorageInfo_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXStorageInfo_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStorageInfo_deinit);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造与析构                                           */
/* ========================================================================== */

void XStorageInfo_init(XStorageInfo* info)
{
    if (!info) return;
    memset(((unsigned char*)info) + sizeof(XClass), 0, sizeof(*info) - sizeof(XClass));
    XClass_init((XClass*)info);
    XClassSetVtable(info, XStorageInfo);
}

void XStorageInfo_init_2(XStorageInfo* info, const XString* path)
{
    XStorageInfo_init(info);
    xstorageinfo_assignPath(info, path);
}

void XStorageInfo_init_3(XStorageInfo* info, const char* path)
{
    XString* tmp;
    XStorageInfo_init(info);
    tmp = path ? XString_create_utf8(path) : NULL;
    xstorageinfo_assignPath(info, tmp);
    if (tmp) XClassDelete(tmp);
}

static XStorageInfo* xstorageinfo_create(XMemoryType memory)
{
    XStorageInfo* info = (XStorageInfo*)XMemory_malloc(sizeof(XStorageInfo), memory);
    if (!info) return NULL;
    memset(info, 0, sizeof(*info));
    XStorageInfo_init(info);
    Set_Class_Memory(info, memory); Set_Class_IsHeap(info, true);
    return info;
}

XStorageInfo* XStorageInfo_create(void)
{
    return xstorageinfo_create(XCLASS_DEFAULT_MEMORY_TYPE);
}

XStorageInfo* XStorageInfo_create_2(const XString* path)
{
    XStorageInfo* info = xstorageinfo_create(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!info) return NULL;
    xstorageinfo_assignPath(info, path);
    return info;
}

XStorageInfo* XStorageInfo_create_3(const char* path)
{
    XStorageInfo* info = xstorageinfo_create(XCLASS_DEFAULT_MEMORY_TYPE);
    XString* tmp;
    if (!info) return NULL;
    tmp = path ? XString_create_utf8(path) : NULL;
    xstorageinfo_assignPath(info, tmp);
    if (tmp) XClassDelete(tmp);
    return info;
}

XStorageInfo* XStorageInfo_create_copy(const XStorageInfo* other)
{
    XStorageInfo* info = XStorageInfo_create();
    if (!info) return NULL;
    XClassCopy(info, other);
    return info;
}

/* ========================================================================== */
/*                        虚函数（拷贝/移动/析构）                              */
/* ========================================================================== */

static void VXStorageInfo_copy(XStorageInfo* dest, const XStorageInfo* src)
{
    XStorageInfoData data;
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XStorageInfo_init(dest);
    data = src->m_data;
    /* 字符串成员深拷贝，数值与标志位整块复制。 */
    data.device = src->m_data.device ? XString_create_copy(src->m_data.device) : NULL;
    data.fileSystemType = src->m_data.fileSystemType ? XString_create_copy(src->m_data.fileSystemType) : NULL;
    data.volumeName = src->m_data.volumeName ? XString_create_copy(src->m_data.volumeName) : NULL;
    data.subvolume = src->m_data.subvolume ? XString_create_copy(src->m_data.subvolume) : NULL;
    xstorageinfo_assignPath(dest, src->m_rootPath);
    dest->m_data = data;
    dest->m_cacheValid = src->m_cacheValid;
}

static void VXStorageInfo_move(XStorageInfo* dest, XStorageInfo* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XStorageInfo_init(dest);
    xstorageinfo_assignPath(dest, src->m_rootPath);
    xstorageinfo_clearData(dest);
    dest->m_data = src->m_data;
    dest->m_cacheValid = src->m_cacheValid;
    /* 源对象移交所有权后回到"空路径+无效缓存"状态。 */
    memset(&src->m_data, 0, sizeof(src->m_data));
    if (src->m_rootPath) {
        XClassDelete(src->m_rootPath);
        src->m_rootPath = NULL;
    }
    src->m_cacheValid = 0;
}

static void VXStorageInfo_deinit(XStorageInfo* info)
{
    if (!info) return;
    xstorageinfo_clearData(info);
    if (info->m_rootPath) {
        XClassDelete(info->m_rootPath);
        info->m_rootPath = NULL;
    }
    XClass_Deinit_Parent(XClass, info);
}

/* ========================================================================== */
/*                        路径与刷新                                           */
/* ========================================================================== */

void XStorageInfo_setPath(XStorageInfo* info, const XString* path)
{
    xstorageinfo_assignPath(info, path);
}

void XStorageInfo_setPath_2(XStorageInfo* info, const char* path)
{
    XString* tmp = path ? XString_create_utf8(path) : NULL;
    xstorageinfo_assignPath(info, tmp);
    if (tmp) XClassDelete(tmp);
}

void XStorageInfo_refresh(XStorageInfo* info)
{
    XStorageInfoData data;
    XString* device;
    XString* fileSystemType;
    XString* volumeName;
    XString* subvolume;
    bool ok;

    if (!info || !info->m_rootPath) return;

    /* 字符串成员在局部建齐（现成则复用），数值字段清零后交平台后端填充。 */
    memset(&data, 0, sizeof(data));
    device = info->m_data.device;
    fileSystemType = info->m_data.fileSystemType;
    volumeName = info->m_data.volumeName;
    subvolume = info->m_data.subvolume;
    if (!device) device = XString_create();
    if (!fileSystemType) fileSystemType = XString_create();
    if (!volumeName) volumeName = XString_create();
    if (!subvolume) subvolume = XString_create();
    if (!device || !fileSystemType || !volumeName || !subvolume) {
        if (device && device != info->m_data.device) XClassDelete(device);
        if (fileSystemType && fileSystemType != info->m_data.fileSystemType) XClassDelete(fileSystemType);
        if (volumeName && volumeName != info->m_data.volumeName) XClassDelete(volumeName);
        if (subvolume && subvolume != info->m_data.subvolume) XClassDelete(subvolume);
        return;
    }
    data.device = device;
    data.fileSystemType = fileSystemType;
    data.volumeName = volumeName;
    data.subvolume = subvolume;

    ok = XDeviceFile_getStorageInfo(info->m_rootPath, &data);
    /* 平台后端两种约定并存：按指针原地填充（Win32）或整结构清零
     * （POSIX 端 memset，XDeviceFile_posix_api.c），调用后统一核对四个指针，
     * 保证不泄漏、不悬挂。 */
    if (!data.device) data.device = device;
    else if (data.device != device) XClassDelete(device);
    if (!data.fileSystemType) data.fileSystemType = fileSystemType;
    else if (data.fileSystemType != fileSystemType) XClassDelete(fileSystemType);
    if (!data.volumeName) data.volumeName = volumeName;
    else if (data.volumeName != volumeName) XClassDelete(volumeName);
    if (!data.subvolume) data.subvolume = subvolume;
    else if (data.subvolume != subvolume) XClassDelete(subvolume);

    if (!ok) {
        /* 查询失败（POSIX 端已清零 data）：丢弃临时字符串，旧缓存原样保留，
         * 仅按 Qt 语义标记缓存无效——访问器随即回落 -1/false 口径。 */
        if (device != info->m_data.device) XClassDelete(device);
        if (fileSystemType != info->m_data.fileSystemType) XClassDelete(fileSystemType);
        if (volumeName != info->m_data.volumeName) XClassDelete(volumeName);
        if (subvolume != info->m_data.subvolume) XClassDelete(subvolume);
        info->m_cacheValid = 0;
        return;
    }
    info->m_data = data;
    info->m_cacheValid = 1;
}

void XStorageInfo_swap(XStorageInfo* lhs, XStorageInfo* rhs)
{
    XString* path;
    XStorageInfoData data;
    uint16_t cacheValid;
    if (!lhs || !rhs || lhs == rhs) return;
    path = lhs->m_rootPath; lhs->m_rootPath = rhs->m_rootPath; rhs->m_rootPath = path;
    data = lhs->m_data; lhs->m_data = rhs->m_data; rhs->m_data = data;
    cacheValid = (uint16_t)lhs->m_cacheValid;
    lhs->m_cacheValid = rhs->m_cacheValid;
    rhs->m_cacheValid = cacheValid;
}

/* ========================================================================== */
/*                        状态检查                                             */
/* ========================================================================== */

bool XStorageInfo_isValid(const XStorageInfo* info)
{
    return info && info->m_cacheValid && info->m_data.isValid;
}

bool XStorageInfo_isReady(const XStorageInfo* info)
{
    return info && info->m_cacheValid && info->m_data.isReady;
}

bool XStorageInfo_isReadOnly(const XStorageInfo* info)
{
    return info && info->m_cacheValid && info->m_data.isReadOnly;
}

bool XStorageInfo_isRoot(const XStorageInfo* info)
{
    return info && info->m_cacheValid && info->m_data.isRoot;
}

/* ========================================================================== */
/*                        空间信息                                             */
/* ========================================================================== */

int64_t XStorageInfo_bytesTotal(const XStorageInfo* info)
{
    if (!XStorageInfo_isValid(info)) return -1;
    return info->m_data.bytesTotal;
}

int64_t XStorageInfo_bytesFree(const XStorageInfo* info)
{
    if (!XStorageInfo_isValid(info)) return -1;
    return info->m_data.bytesFree;
}

int64_t XStorageInfo_bytesAvailable(const XStorageInfo* info)
{
    if (!XStorageInfo_isValid(info)) return -1;
    return info->m_data.bytesAvailable;
}

int XStorageInfo_blockSize(const XStorageInfo* info)
{
    if (!XStorageInfo_isValid(info)) return -1;
    return info->m_data.blockSize;
}

/* ========================================================================== */
/*                        设备信息（返回对象内部借用指针）                      */
/* ========================================================================== */

XString* XStorageInfo_rootPath(const XStorageInfo* info)
{
    return info ? info->m_rootPath : NULL;
}

XString* XStorageInfo_device(const XStorageInfo* info)
{
    if (!XStorageInfo_isValid(info)) return NULL;
    return info->m_data.device;
}

XString* XStorageInfo_fileSystemType(const XStorageInfo* info)
{
    if (!XStorageInfo_isValid(info)) return NULL;
    return info->m_data.fileSystemType;
}

XString* XStorageInfo_name(const XStorageInfo* info)
{
    if (!XStorageInfo_isValid(info)) return NULL;
    return info->m_data.volumeName;
}

XString* XStorageInfo_displayName(const XStorageInfo* info)
{
    if (!info) return NULL;
    /* Qt 语义：有卷标用卷标，否则回落挂载点路径。 */
    if (info->m_data.volumeName
        && !XContainer_isEmpty_base((const XContainer*)info->m_data.volumeName)) {
        return info->m_data.volumeName;
    }
    return info->m_rootPath;
}

XString* XStorageInfo_subvolume(const XStorageInfo* info)
{
    if (!XStorageInfo_isValid(info)) return NULL;
    return info->m_data.subvolume;
}

/* ========================================================================== */
/*                        静态函数                                             */
/* ========================================================================== */

/** @brief enumerateDrives 回调：只取第一个根路径建对象，取到即停。 */
static bool xstorageinfo_rootCallback(const XString* path, void* userData)
{
    XStorageInfo** out = (XStorageInfo**)userData;
    if (*out) return false;
    *out = path ? XStorageInfo_create_3(XString_toUtf8(path)) : NULL;
    return false;   /* 无论成败都停止枚举（根卷只需第一个）。 */
}

XStorageInfo* XStorageInfo_root(void)
{
    XStorageInfo* root = NULL;
    /* Windows：契约=系统盘，权威来源=SystemDrive 环境变量（经环境唯一
     * 入口 XSystem_environment 取；POSIX 无该变量自然回落枚举首根——
     * 2026-10-08 评审：原实现恒取枚举首盘，存在 A:/B: 保留盘时取错）。 */
    {
        const char* sysDrive = XSystem_environment("SystemDrive");
        if (sysDrive && sysDrive[0] != '\0') {
            char rootPath[16];
            size_t i = 0;
            while (sysDrive[i] != '\0' && i + 2 < sizeof(rootPath)) {
                rootPath[i] = (sysDrive[i] == '\\') ? '/' : sysDrive[i];
                ++i;
            }
            if (i > 0 && rootPath[i - 1] != '/') rootPath[i++] = '/';
            rootPath[i] = '\0';
            root = XStorageInfo_create_3(rootPath);
            if (root) return root;
        }
    }
    (void)XDeviceFile_enumerateDrives(xstorageinfo_rootCallback, &root);
    /* 平台无可枚举根（异常环境）：返回无效对象而非 NULL，调用方按 isValid 分流。 */
    if (!root) root = XStorageInfo_create();
    return root;
}

/** @brief enumerateDrives 回调：为每个根路径建对象并入列；分配失败即停。 */
static bool xstorageinfo_volumesCallback(const XString* path, void* userData)
{
    XStorageInfoList* list = (XStorageInfoList*)userData;
    XStorageInfo* info = path ? XStorageInfo_create_3(XString_toUtf8(path)) : NULL;
    if (!info) return false;
    if (!XVector_push_back_1_base(list, &info)) {
        XClassDelete(info);
        return false;
    }
    return true;
}

XStorageInfoList* XStorageInfo_mountedVolumes(void)
{
    XStorageInfoList* list = XVector_create(sizeof(XStorageInfo*));
    if (!list) return NULL;
    if (!XDeviceFile_enumerateDrives(xstorageinfo_volumesCallback, list)) {
        /* 枚举中断（分配失败）：释放已收集元素后整体放弃。 */
        size_t count = XContainer_size_base((const XContainer*)list);
        size_t i;
        for (i = 0; i < count; ++i) {
            XStorageInfo* info = (XStorageInfo*)XVector_at_base(list, (int64_t)i);
            if (info) XClassDelete(info);
        }
        XClassDelete(list);
        return NULL;
    }
    return list;
}

/* ========================================================================== */
/*                        比较操作                                             */
/* ========================================================================== */

bool XStorageInfo_equals(const XStorageInfo* lhs, const XStorageInfo* rhs)
{
    bool lhsValid = XStorageInfo_isValid(lhs);
    bool rhsValid = XStorageInfo_isValid(rhs);
    if (!lhsValid && !rhsValid) return true;   /* 两个无效对象恒相等（Qt 语义）。 */
    if (lhsValid != rhsValid) return false;
    if (!lhs->m_rootPath || !rhs->m_rootPath) return lhs->m_rootPath == rhs->m_rootPath;
    return XString_equals(lhs->m_rootPath, rhs->m_rootPath, XChar_CaseSensitive);
}

#endif /* XSTORAGEINFO_ON */
#endif /* XFILE_ON */
