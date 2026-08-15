#include "XFile.h"
#include "XFileSystem.h"
#include "XIODevice_Protected.h"  /* XIODevice_setFd */
#include <stdlib.h>
#include <string.h>
#if XFILE_ON
#if XFILE_OBJECT_ON

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
    
    int error = 0;
    XFd fd = XFileSystem_open(file->m_fileName, (int)mode, &error);
    
    if (fd < 0) {
        file->m_parent.m_error = error;
        return false;
    }
    
    XIODevice_setFd(device, fd);
    file->m_parent.m_handleFlags = XFileDevice_AutoCloseHandle;
    file->m_parent.m_error = XFileDevice_NoError;
    device->m_openMode = mode;
    /* Qt 行为: 打开时根据 Text 标志设置文本模式 */
    XIODevice_setTextModeEnabled(device, (mode & XIODevice_Text) != 0);
    XFileDevice_registerAsync(&file->m_parent);
    return true;
}

static void VXFile_close(XIODevice* device)
{
    XFile* file = (XFile*)device;
    if (!file || XIODevice_fd(device) < 0) return;
    
    XIODevice_aboutToClose_signal(device);
    
    XFileDevice_unregisterAsync(&file->m_parent);
    
    if (file->m_parent.m_handleFlags & XFileDevice_AutoCloseHandle) {
        XFileSystem_close(XIODevice_fd(device));
    }
    
    XIODevice_setFd(device, XFD_INVALID);
    device->m_openMode = XIODevice_NotOpen;
    file->m_parent.m_cachedSize = -1;
}

static bool VXFile_resize(XFileDevice* device, int64_t sz)
{
    if (!device || XIODevice_fd(&device->m_parent) < 0) return false;
    return XFileSystem_resize(XIODevice_fd(&device->m_parent), sz);
}

static XFilePermissions VXFile_permissions(const XFileDevice* device)
{
    const XFile* file = (const XFile*)device;
    if (!file || !file->m_fileName) return 0;
    
    XFileStat stat;
    if (!XFileSystem_stat(file->m_fileName, &stat)) return 0;
    return stat.permissions;
}

static bool VXFile_setPermissions(XFileDevice* device, XFilePermissions permissions)
{
    const XFile* file = (const XFile*)device;
    if (!file || !file->m_fileName) return false;
    
    return XFileSystem_setPermissions(file->m_fileName, permissions);
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
    XVTABLE_INIT_DEFAULT(XFile)
    XVTABLE_INHERIT_XCLASS(XFileDevice);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFile_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_FileName, VXFile_fileName);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXFile_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXFile_close);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_Resize, VXFile_resize);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_Permissions, VXFile_permissions);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_SetPermissions, VXFile_setPermissions);

    XCLASS_SHOW_SIZE_DEFAULT(XFile);
    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

XFile* XFile_create_ex(XMemoryType memory)
{
    XFile* file = (XFile*)XMemory_malloc(sizeof(XFile), memory);
    if (!file) return NULL;
    XFile_init(file);
    Set_Class_Memory(file, memory); Set_Class_IsHeap(file, true);
    return file;
}

XFile* XFile_create_2(const XString* name)
{
    XFile* file = XFile_create();
    if (file && name) XFile_setFileName(file, name);
    return file;
}

void XFile_init(XFile* file)
{
    if (!file) return;
    XFileDevice_init(&file->m_parent);
    XClassSetVtable(file, XFile);
    file->m_fileName = NULL;
}

void XFile_init_2(XFile* file, const XString* name)
{
    XFile_init(file);
    if (file && name) XFile_setFileName(file, name);
}

/* ============================================================================
 * 文件名操作
 * ============================================================================ */

void XFile_setFileName(XFile* file, const XString* name)
{
    if (!file || !name) return;
    /* Qt 行为: 文件已打开时, setFileName 会先关闭再更新名 */
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XIODevice_close_base((XIODevice*)&file->m_parent.m_parent);
    }

    if (file->m_fileName) XString_delete_base(file->m_fileName);
    file->m_fileName = XString_create_copy(name);
}

/* ============================================================================
 * 打开文件
 * ============================================================================ */

bool XFile_open_2(XFile* file, XIODeviceBaseMode mode, XFilePermissions permissions)
{
    /* Qt 行为: Append/NewOnly 隐含 WriteOnly */
    if (mode & (XIODevice_Append | XIODevice_NewOnly)) {
        mode |= XIODevice_WriteOnly;
    }
    if (!XFile_open_base(file, mode)) return false;
    if (permissions != 0) XFile_setPermissions_base(file, permissions);
    return true;
}

bool XFile_open_3(XFile* file, int fd, XIODeviceBaseMode mode, XFileDeviceFileHandleFlags handleFlags)
{
    if (!file || fd < 0) return false;
    /* Qt 行为: Append/NewOnly 隐含 WriteOnly */
    if (mode & (XIODevice_Append | XIODevice_NewOnly)) {
        mode |= XIODevice_WriteOnly;
    }
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XIODevice_close_base((XIODevice*)&file->m_parent.m_parent);
    }
    /* 关闭传入 fd 的 AutoClose: open_3 不会自动关闭外部传入的 fd, 由 handleFlags 控制 */
    XIODevice_setFd(&file->m_parent.m_parent, (XFd)fd);
    file->m_parent.m_handleFlags = handleFlags;
    file->m_parent.m_error = XFileDevice_NoError;
    file->m_parent.m_parent.m_openMode = mode;
    /* Qt 行为: 打开时根据 Text 标志设置文本模式 */
    XIODevice_setTextModeEnabled((XIODevice*)&file->m_parent.m_parent, (mode & XIODevice_Text) != 0);
    XFileDevice_registerAsync(&file->m_parent);
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
    return XFileSystem_exists(fileName);
}

bool XFile_remove(XFile* file)
{
    if (!file || !file->m_fileName) return false;
    /* Qt 行为: 成员 remove 先关闭文件再删除 */
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XIODevice_close_base((XIODevice*)&file->m_parent.m_parent);
    }
    return XFile_remove_static(file->m_fileName);
}

bool XFile_remove_static(const XString* fileName)
{
    if (!fileName) return false;
    return XFileSystem_removePermanent(fileName);
}

bool XFile_rename(XFile* file, const XString* newName)
{
    if (!file || !file->m_fileName || !newName) return false;
    /* Qt 行为: 成员 rename 先关闭再改名 */
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XIODevice_close_base((XIODevice*)&file->m_parent.m_parent);
    }
    return XFile_rename_static(file->m_fileName, newName);
}

bool XFile_rename_static(const XString* oldName, const XString* newName)
{
    if (!oldName || !newName) return false;
    return XFileSystem_rename(oldName, newName);
}

bool XFile_copy(XFile* file, const XString* newName)
{
    if (!file || !file->m_fileName || !newName) return false;
    /* Qt 行为: 成员 copy 先关闭 */
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XIODevice_close_base((XIODevice*)&file->m_parent.m_parent);
    }
    return XFile_copy_static(file->m_fileName, newName);
}

bool XFile_copy_static(const XString* fileName, const XString* newName)
{
    if (!fileName || !newName) return false;
    return XFileSystem_copy(fileName, newName);
}

bool XFile_link(XFile* file, const XString* linkName)
{
    if (!file || !file->m_fileName || !linkName) return false;
    return XFile_link_static(file->m_fileName, linkName);
}

bool XFile_link_static(const XString* fileName, const XString* linkName)
{
    if (!fileName || !linkName) return false;
    return XFileSystem_link(fileName, linkName, XLinkType_Symbolic);
}

bool XFile_moveToTrash(XFile* file)
{
    if (!file || !file->m_fileName) return false;
    /* Qt 行为: 成员 moveToTrash 先关闭文件再放入回收站 */
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XIODevice_close_base((XIODevice*)&file->m_parent.m_parent);
    }
    return XFile_moveToTrash_static(file->m_fileName, NULL);
}

bool XFile_moveToTrash_static(const XString* fileName, XString* pathInTrash)
{
    /*
     * Qt 行为: QFile::moveToTrash() 在 Windows 上用 SHFileOperation(FO_DELETE|FOF_ALLOWUNDO),
     * 在 Unix 上 (Freedesktop.org Trash v1.0) 把文件 move 到 ~/.local/share/Trash/files.
     * 平台层已经实现 XFileSystem_remove(Trash), 这里仅做一次抽象调用;
     * 不可用时平台层自行退化到永久删除。
     */
    return XFileSystem_remove(fileName, XRemoveMode_Trash, pathInTrash);
}

XString* XFile_symLinkTarget(const XFile* file)
{
    if (!file || !file->m_fileName) return XString_create();
    return XFile_symLinkTarget_static(file->m_fileName);
}

XString* XFile_symLinkTarget_static(const XString* fileName)
{
    if (!fileName) return XString_create();
    XString* target = XString_create();
    if (!target) return NULL;
    
    if (!XFileSystem_readLink(fileName, target)) {
        XString_delete_base(target);
        return NULL;
    }
    return target;
}

/* ============================================================================
 * 静态便捷函数
 * ============================================================================ */

bool XFile_resize_static(const XString* fileName, int64_t sz)
{
    XFile file;
    XFile_init_2(&file, fileName);
    /* Qt 行为: 静态 resize 内部打开文件再调整大小 */
    bool result = false;
    if (XIODevice_open_base((XIODevice*)&file, XIODevice_ReadWrite)) {
        result = XFile_resize_base((XFileDevice*)&file, sz);
        XIODevice_close_base((XIODevice*)&file);
    }
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
    int64_t sz = XByteArray_size_base((XByteArray*)localFileName);
    if (sz <= 0) return XString_create();
    const char* data = (const char*)XByteArray_data((XByteArray*)localFileName);
    if (!data) return XString_create();
    /* XByteArray 不保证 NUL 终止, 拷贝到临时 NUL 终止缓冲再 assign */
    char* tmp = (char*)XMalloc_System((size_t)sz + 1);
    if (!tmp) return XString_create();
    memcpy(tmp, data, (size_t)sz);
    tmp[sz] = '\0';
    XString* s = XString_create_utf8(tmp);
    XFree_System(tmp);
    return s;
}

XString* XFile_decodeName_2(const char* localFileName)
{
    if (!localFileName) return XString_create();
    return XString_create_utf8(localFileName);
}
#endif // XFILE_OBJECT_ON
#endif /* XFILE_ON */
