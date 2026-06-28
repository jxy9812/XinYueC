#include "XFile.h"
#include "XFileSystem_platform.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 虚函数实现（重写父类虚函数）
 * ============================================================================ */

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

static bool VXFile_open(XIODevice* device, XIODeviceBaseMode mode)
{
    XFile* file = (XFile*)device;
    if (!file || !file->m_fileName) return false;
    
    int fsMode = 0;
    if (mode & XIODevice_ReadOnly) fsMode |= XFileSystem_ReadOnly;
    if (mode & XIODevice_WriteOnly) fsMode |= XFileSystem_WriteOnly;
    if (mode & XIODevice_ReadWrite) fsMode |= XFileSystem_ReadWrite;
    if (mode & XIODevice_Append) fsMode |= XFileSystem_Append;
    if (mode & XIODevice_Truncate) fsMode |= XFileSystem_Truncate;
    if (mode & XIODevice_NewOnly) fsMode |= XFileSystem_NewOnly;
    
    const char* pathUtf8 = XString_toUtf8(file->m_fileName);
    int error = 0;
    int fd = XFileSystem_open(pathUtf8, fsMode, &error);
    
    if (fd < 0) {
        file->m_parent.m_error = error;
        return false;
    }
    
    file->m_parent.m_fileHandle = fd;
    file->m_parent.m_handleFlags = XFileDevice_AutoCloseHandle;
    file->m_parent.m_error = XFileDevice_NoError;
    device->m_openMode = mode;
    return true;
}

static void VXFile_close(XIODevice* device)
{
    XFile* file = (XFile*)device;
    if (!file || file->m_parent.m_fileHandle < 0) return;
    
    XIODevice_aboutToClose_signal(device);
    
    if (file->m_parent.m_handleFlags & XFileDevice_AutoCloseHandle) {
        XFileSystem_close(file->m_parent.m_fileHandle);
    }
    
    file->m_parent.m_fileHandle = -1;
    device->m_openMode = XIODevice_NotOpen;
    file->m_parent.m_cachedSize = -1;
}

static bool VXFile_resize(XFileDevice* device, int64_t sz)
{
    if (!device || device->m_fileHandle < 0) return false;
    return XFileSystem_resize(device->m_fileHandle, sz);
}

static XFilePermissions VXFile_permissions(const XFileDevice* device)
{
    const XFile* file = (const XFile*)device;
    if (!file || !file->m_fileName) return 0;
    
    const char* pathUtf8 = XString_toUtf8(file->m_fileName);
    XFileStat stat;
    if (!XFileSystem_stat(pathUtf8, &stat)) return 0;
    return stat.permissions;
}

static bool VXFile_setPermissions(XFileDevice* device, XFilePermissions permissions)
{
    const XFile* file = (const XFile*)device;
    if (!file || !file->m_fileName) return false;
    
    const char* pathUtf8 = XString_toUtf8(file->m_fileName);
    return XFileSystem_setPermissions(pathUtf8, permissions);
}

static void VXFile_deinit(XFile* file)
{
    if (!file) return;
    if (file->m_fileName) {
        XString_delete_base(file->m_fileName);
        file->m_fileName = NULL;
    }
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
    if (file && name) XFile_setFileName(file, name);
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
    if (file && name) XFile_setFileName(file, name);
}

//void XFile_deinit_base(XFile* file)
//{
//    if (!file) return;
//    if (file->m_fileName) {
//        XString_delete_base(file->m_fileName);
//        file->m_fileName = NULL;
//    }
//    XClass_Deinit_Parent(XFileDevice, file);
//}

/* ============================================================================
 * 文件名操作
 * ============================================================================ */

void XFile_setFileName(XFile* file, const XString* name)
{
    if (!file || !name) return;
    if (file->m_parent.m_fileHandle >= 0) return;
    
    if (file->m_fileName) XString_delete_base(file->m_fileName);
    file->m_fileName = XString_create_copy(name);
}

/* ============================================================================
 * 打开文件
 * ============================================================================ */

bool XFile_open_2(XFile* file, XIODeviceBaseMode mode, XFilePermissions permissions)
{
    if (!XFile_open_base(file, mode)) return false;
    if (permissions != 0) return XFile_setPermissions_base(file, permissions);
    return true;
}

bool XFile_open_3(XFile* file, int fd, XIODeviceBaseMode mode, XFileDeviceFileHandleFlags handleFlags)
{
    if (!file || fd < 0) return false;
    if (file->m_parent.m_fileHandle >= 0) {
        XIODevice_close_base(&file->m_parent.m_parent);
    }
    file->m_parent.m_fileHandle = fd;
    file->m_parent.m_handleFlags = handleFlags;
    file->m_parent.m_error = XFileDevice_NoError;
    file->m_parent.m_parent.m_openMode = mode;
    return true;
}

/* ============================================================================
 * 文件操作（静态函数使用 XFileSystem API）
 * ============================================================================ */

bool XFile_exists(const XFile* file)
{
    if (!file || !file->m_fileName) return false;
    return XFile_exists_static(file->m_fileName);
}

bool XFile_exists_static(const XString* fileName)
{
    if (!fileName) return false;
    return XFileSystem_exists(XString_toUtf8(fileName));
}

bool XFile_remove(XFile* file)
{
    if (!file || !file->m_fileName) return false;
    return XFile_remove_static(file->m_fileName);
}

bool XFile_remove_static(const XString* fileName)
{
    if (!fileName) return false;
    return XFileSystem_remove(XString_toUtf8(fileName));
}

bool XFile_rename(XFile* file, const XString* newName)
{
    if (!file || !file->m_fileName || !newName) return false;
    return XFile_rename_static(file->m_fileName, newName);
}

bool XFile_rename_static(const XString* oldName, const XString* newName)
{
    if (!oldName || !newName) return false;
    return XFileSystem_rename(XString_toUtf8(oldName), XString_toUtf8(newName));
}

bool XFile_copy(XFile* file, const XString* newName)
{
    if (!file || !file->m_fileName || !newName) return false;
    return XFile_copy_static(file->m_fileName, newName);
}

bool XFile_copy_static(const XString* fileName, const XString* newName)
{
    if (!fileName || !newName) return false;
    return XFileSystem_copy(XString_toUtf8(fileName), XString_toUtf8(newName));
}

bool XFile_link(XFile* file, const XString* linkName)
{
    if (!file || !file->m_fileName || !linkName) return false;
    return XFile_link_static(file->m_fileName, linkName);
}

bool XFile_link_static(const XString* fileName, const XString* linkName)
{
    if (!fileName || !linkName) return false;
    return XFileSystem_link(XString_toUtf8(fileName), XString_toUtf8(linkName));
}

bool XFile_moveToTrash(XFile* file)
{
    if (!file || !file->m_fileName) return false;
    return XFile_moveToTrash_static(file->m_fileName, NULL);
}

bool XFile_moveToTrash_static(const XString* fileName, XString* pathInTrash)
{
    (void)pathInTrash;
    if (!fileName) return false;
    return XFileSystem_moveToTrash(XString_toUtf8(fileName));
}

XString* XFile_symLinkTarget(const XFile* file)
{
    if (!file || !file->m_fileName) return XString_create();
    return XFile_symLinkTarget_static(file->m_fileName);
}

XString* XFile_symLinkTarget_static(const XString* fileName)
{
    if (!fileName) return XString_create();
    char target[1024];
    if (!XFileSystem_readLink(XString_toUtf8(fileName), target, sizeof(target))) {
        return XString_create();
    }
    return XString_create_utf8(target);
}

/* ============================================================================
 * 静态便捷函数
 * ============================================================================ */

bool XFile_resize_static(const XString* fileName, int64_t sz)
{
    XFile file;
    XFile_init_2(&file, fileName);
    bool result = XFile_resize_base(&file, sz);
    //XFile_deinit_base(&file);
    XClass_deinit_base(&file);
    return result;
}

XFilePermissions XFile_permissions_static(const XString* fileName)
{
    XFile file;
    XFile_init_2(&file, fileName);
    XFilePermissions perms = XFile_permissions_base(&file);
    XClass_deinit_base(&file);
    return perms;
}

bool XFile_setPermissions_static(const XString* fileName, XFilePermissions permissions)
{
    XFile file;
    XFile_init_2(&file, fileName);
    bool result = XFile_setPermissions_base(&file, permissions);
    XClass_deinit_base(&file);
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