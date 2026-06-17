#include "XFile.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 平台相关函数声明（在 XFile_win32.c 或 XFile_linux.c 中实现）
 * ============================================================================ */

// 虚函数相关
extern bool XFile_open_impl(XFile* file, XIODeviceBaseMode mode);
extern void XFile_close_impl(XFile* file);
extern bool XFile_resize_impl(XFile* file, int64_t sz);
extern XFilePermissions XFile_permissions_impl(const XFile* file);
extern bool XFile_setPermissions_impl(XFile* file, XFilePermissions permissions);

// 静态函数相关
extern bool XFile_exists_impl(const XString* fileName);
extern bool XFile_remove_impl(const XString* fileName);
extern bool XFile_rename_impl(const XString* oldName, const XString* newName);
extern bool XFile_copy_impl(const XString* fileName, const XString* newName);
extern bool XFile_link_impl(const XString* fileName, const XString* linkName);
extern bool XFile_moveToTrash_impl(const XString* fileName, XString* pathInTrash);
extern XString* XFile_symLinkTarget_impl(const XString* fileName);

/* ============================================================================
 * 虚函数实现（重写父类虚函数）
 * ============================================================================ */

/**
 * @brief 虚函数：获取文件名
 */
static const XString* VXFile_fileName(const XFileDevice* device)
{
    const XFile* file = (const XFile*)device;
    if (!file || !file->m_fileName) {
        static XString emptyString;
        static bool initialized = false;
        if (!initialized) {
            XString_init(&emptyString);
            initialized = true;
        }
        return &emptyString;
    }
    return file->m_fileName;
}

/**
 * @brief 虚函数：打开文件
 */
static bool VXFile_open(XIODevice* device, XIODeviceBaseMode mode)
{
    return XFile_open_impl((XFile*)device, mode);
}

/**
 * @brief 虚函数：关闭文件
 */
static void VXFile_close(XIODevice* device)
{
    XFile_close_impl((XFile*)device);
}

/**
 * @brief 虚函数：调整文件大小
 */
static bool VXFile_resize(XFileDevice* device, int64_t sz)
{
    return XFile_resize_impl((XFile*)device, sz);
}

/**
 * @brief 虚函数：获取文件权限
 */
static XFilePermissions VXFile_permissions(const XFileDevice* device)
{
    return XFile_permissions_impl((const XFile*)device);
}

/**
 * @brief 虚函数：设置文件权限
 */
static bool VXFile_setPermissions(XFileDevice* device, XFilePermissions permissions)
{
    return XFile_setPermissions_impl((XFile*)device, permissions);
}

/**
 * @brief 虚函数：析构
 */
static void VXFile_deinit(XFile* file)
{
    if (!file) return;
    
    // 释放文件名
    if (file->m_fileName) {
        XString_delete_base(file->m_fileName);
        file->m_fileName = NULL;
    }
    
    // 释放父对象
    XClass_Deinit_Parent(XFileDevice, file);
}

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XFile_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XFile))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XFileDevice);

    // 重写虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFile_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_FileName, VXFile_fileName);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXFile_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXFile_close);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_Resize, VXFile_resize);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_Permissions, VXFile_permissions);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_SetPermissions, VXFile_setPermissions);

#if SHOWCONTAINERSIZE
    printf("XFile vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif

    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

XFile* XFile_create_1(void)
{
    XFile* file = (XFile*)XMalloc_System(sizeof(XFile));
    if (!file) return NULL;
    XFile_init_1(file);
    Set_Class_MemoryFree(file, XFree_System);
    return file;
}

XFile* XFile_create_2(const XString* name)
{
    XFile* file = XFile_create_1();
    if (file && name) {
        XFile_setFileName(file, name);
    }
    return file;
}

void XFile_init_1(XFile* file)
{
    if (!file) return;
    XFileDevice_init(&file->m_parent);
    XClassSetVtable(file, XFile);
    file->m_fileName = NULL;
}

void XFile_init_2(XFile* file, const XString* name)
{
    XFile_init_1(file);
    if (file && name) {
        XFile_setFileName(file, name);
    }
}

void XFile_deinit_base(XFile* file)
{
    if (!file) return;
    
    // 释放文件名
    if (file->m_fileName) {
        XString_delete_base(file->m_fileName);
        file->m_fileName = NULL;
    }
    
    // 调用父类析构
    XClass_Deinit_Parent(XFileDevice, file);
}

/* ============================================================================
 * 文件名操作
 * ============================================================================ */

void XFile_setFileName(XFile* file, const XString* name)
{
    if (!file || !name) return;
    
    // 如果文件已打开，不允许更改文件名
    if (file->m_parent.m_fileHandle >= 0) {
        return;
    }
    
    if (file->m_fileName) {
        XString_delete_base(file->m_fileName);
    }
    file->m_fileName = XString_create_copy(name);
}

/* ============================================================================
 * 打开文件
 * ============================================================================ */

bool XFile_open_2(XFile* file, XIODeviceBaseMode mode, XFilePermissions permissions)
{
    // 调用虚函数打开文件
    if (!XFile_open_base(file, mode)) return false;
    // 设置权限
    if (permissions != 0) {
        return XFile_setPermissions_base(file, permissions);
    }
    return true;
}

bool XFile_open_3(XFile* file, int fd, XIODeviceBaseMode mode, XFileDeviceFileHandleFlags handleFlags)
{
    if (!file || fd < 0) return false;
    
    // 如果已有打开的文件，先关闭
    if (file->m_parent.m_fileHandle >= 0) {
        XIODevice_close_base(&file->m_parent.m_parent);
    }
    
    // 设置文件句柄
    file->m_parent.m_fileHandle = fd;
    file->m_parent.m_handleFlags = handleFlags;
    file->m_parent.m_error = XFileDevice_NoError;
    file->m_parent.m_parent.m_openMode = mode;
    
    return true;
}

/* ============================================================================
 * 文件存在检查
 * ============================================================================ */

bool XFile_exists(const XFile* file)
{
    if (!file || !file->m_fileName) return false;
    return XFile_exists_static(file->m_fileName);
}

bool XFile_exists_static(const XString* fileName)
{
    return XFile_exists_impl(fileName);
}

/* ============================================================================
 * 文件删除
 * ============================================================================ */

bool XFile_remove(XFile* file)
{
    if (!file || !file->m_fileName) return false;
    return XFile_remove_static(file->m_fileName);
}

bool XFile_remove_static(const XString* fileName)
{
    return XFile_remove_impl(fileName);
}

/* ============================================================================
 * 文件重命名
 * ============================================================================ */

bool XFile_rename(XFile* file, const XString* newName)
{
    if (!file || !file->m_fileName || !newName) return false;
    return XFile_rename_static(file->m_fileName, newName);
}

bool XFile_rename_static(const XString* oldName, const XString* newName)
{
    return XFile_rename_impl(oldName, newName);
}

/* ============================================================================
 * 文件复制
 * ============================================================================ */

bool XFile_copy(XFile* file, const XString* newName)
{
    if (!file || !file->m_fileName || !newName) return false;
    return XFile_copy_static(file->m_fileName, newName);
}

bool XFile_copy_static(const XString* fileName, const XString* newName)
{
    return XFile_copy_impl(fileName, newName);
}

/* ============================================================================
 * 创建链接
 * ============================================================================ */

bool XFile_link(XFile* file, const XString* linkName)
{
    if (!file || !file->m_fileName || !linkName) return false;
    return XFile_link_static(file->m_fileName, linkName);
}

bool XFile_link_static(const XString* fileName, const XString* linkName)
{
    return XFile_link_impl(fileName, linkName);
}

/* ============================================================================
 * 移到回收站
 * ============================================================================ */

bool XFile_moveToTrash(XFile* file)
{
    if (!file || !file->m_fileName) return false;
    return XFile_moveToTrash_static(file->m_fileName, NULL);
}

bool XFile_moveToTrash_static(const XString* fileName, XString* pathInTrash)
{
    return XFile_moveToTrash_impl(fileName, pathInTrash);
}

/* ============================================================================
 * 符号链接目标
 * ============================================================================ */

XString* XFile_symLinkTarget(const XFile* file)
{
    if (!file || !file->m_fileName) return XString_create();
    return XFile_symLinkTarget_static(file->m_fileName);
}

XString* XFile_symLinkTarget_static(const XString* fileName)
{
    return XFile_symLinkTarget_impl(fileName);
}

/* ============================================================================
 * 静态便捷函数（通过临时对象调用虚函数）
 * ============================================================================ */

bool XFile_resize_static(const XString* fileName, int64_t sz)
{
    XFile file;
    XFile_init_2(&file, fileName);
    bool result = XFile_resize_base(&file, sz);
    XFile_deinit_base(&file);
    return result;
}

XFilePermissions XFile_permissions_static(const XString* fileName)
{
    XFile file;
    XFile_init_2(&file, fileName);
    XFilePermissions perms = XFile_permissions_base(&file);
    XFile_deinit_base(&file);
    return perms;
}

bool XFile_setPermissions_static(const XString* fileName, XFilePermissions permissions)
{
    XFile file;
    XFile_init_2(&file, fileName);
    bool result = XFile_setPermissions_base(&file, permissions);
    XFile_deinit_base(&file);
    return result;
}

/* ============================================================================
 * 文件名编码转换
 * ============================================================================ */

XByteArray* XFile_encodeName(const XString* fileName)
{
    if (!fileName) return XByteArray_create();
    const char* local = XString_toLocal(fileName);
    if (!local) return XByteArray_create();
    return XByteArray_create_utf8(local);
}

XString* XFile_decodeName(const XByteArray* localFileName)
{
    if (!localFileName) return XString_create();
    const char* data = (const char*)XByteArray_data((XByteArray*)localFileName);
    if (!data) return XString_create();
    return XString_create_utf8(data);
}

XString* XFile_decodeName_2(const char* localFileName)
{
    if (!localFileName) return XString_create();
    return XString_create_utf8(localFileName);
}