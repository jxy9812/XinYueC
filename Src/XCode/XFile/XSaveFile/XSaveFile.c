#include "XSaveFile.h"
#include "XIODevice_Protected.h"
#include "XFileSystem.h"
#include <stdlib.h>
#include <string.h>
#if XFILE_ON
#if XSAVEFILE_ON

/* ============================================================================
 * 内部函数：生成临时文件名
 * ============================================================================ */

bool XSaveFile_generateTempFileName(const XString* targetPath, XString* tempPath)
{
    if (!targetPath || !tempPath) return false;
    
    XString_assign(tempPath, targetPath);
    XString_append_utf8(tempPath, ".XXXXXX");
    
    return true;
}

/* ============================================================================
 * 虚函数实现（重写父类虚函数）
 * ============================================================================ */

static const XString* VXSaveFile_fileName(const XFileDevice* device)
{
    const XSaveFile* file = (const XSaveFile*)device;
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

static bool VXSaveFile_open(XIODevice* device, XIODeviceBaseMode mode)
{
    XSaveFile* file = (XSaveFile*)device;
    if (!file || !file->m_fileName) return false;
    
    // QSaveFile 只支持 WriteOnly
    if (!(mode & XIODevice_WriteOnly)) {
        return false;
    }
    
    // 不支持 ReadOnly, Append, NewOnly, ExistingOnly
    if (mode & XIODevice_ReadOnly) {
        return false;
    }
    if (mode & XIODevice_Append) {
        // XSaveFile 不支持追加模式，因为它使用临时文件机制
        return false;
    }
    if (mode & XIODevice_NewOnly) {
        return false;
    }
    if (mode & XIODevice_ExistingOnly) {
        return false;
    }
    
    // 初始化状态
    file->m_canceled = false;
    file->m_committed = false;
    file->m_writeError = false;
    file->m_useTempFile = true;
    
    // 尝试创建临时文件
    if (!file->m_directWriteFallback) {
        // 必须使用临时文件
        file->m_tempFileName = XString_create();
        if (!file->m_tempFileName) return false;
        
        if (!XSaveFile_generateTempFileName(file->m_fileName, file->m_tempFileName)) {
            XString_delete_base(file->m_tempFileName);
            file->m_tempFileName = NULL;
            return false;
        }
        
        // 保存临时文件名
        file->m_useTempFile = true;
        
        // 打开临时文件
        int fsMode = XFileSystem_WriteOnly | XFileSystem_Create | XFileSystem_Truncate;
        int error = 0;
        XFd fd = XFileSystem_open(file->m_tempFileName, fsMode, &error);
        
        if (fd < 0) {
            XString_delete_base(file->m_tempFileName);
            file->m_tempFileName = NULL;
            return false;
        }
        
        XIODevice_setFd(device, fd);
        file->m_parent.m_handleFlags = XFileDevice_AutoCloseHandle;
        file->m_parent.m_error = XFileDevice_NoError;
        device->m_openMode = mode;
        XFileDevice_registerAsync(&file->m_parent);
        return true;
    } else {
        // 允许回退到直接写入
        int fsMode = XFileSystem_WriteOnly | XFileSystem_Create | XFileSystem_Truncate;
        int error = 0;
        XFd fd = XFileSystem_open(file->m_fileName, fsMode, &error);
        
        if (fd >= 0) {
            // 直接写入成功
            file->m_tempFileName = NULL;
            file->m_useTempFile = false;
            XIODevice_setFd(device, fd);
            file->m_parent.m_handleFlags = XFileDevice_AutoCloseHandle;
            file->m_parent.m_error = XFileDevice_NoError;
            device->m_openMode = mode;
            XFileDevice_registerAsync(&file->m_parent);
            return true;
        }
        
        // 尝试创建临时文件
        file->m_tempFileName = XString_create();
        if (!file->m_tempFileName) return false;
        
        if (!XSaveFile_generateTempFileName(file->m_fileName, file->m_tempFileName)) {
            XString_delete_base(file->m_tempFileName);
            file->m_tempFileName = NULL;
            return false;
        }
        file->m_useTempFile = true;
        
        fd = XFileSystem_open(file->m_tempFileName, fsMode, &error);
        
        if (fd < 0) {
            XString_delete_base(file->m_tempFileName);
            file->m_tempFileName = NULL;
            return false;
        }
        
        XIODevice_setFd(device, fd);
        file->m_parent.m_handleFlags = XFileDevice_AutoCloseHandle;
        file->m_parent.m_error = XFileDevice_NoError;
        device->m_openMode = mode;
        XFileDevice_registerAsync(&file->m_parent);
        return true;
    }
}

static void VXSaveFile_close(XIODevice* device)
{
    // QSaveFile 不允许直接调用 close()
    // 必须使用 commit() 或析构时自动处理
    // 这里什么都不做，保持文件打开状态
    (void)device;
}

static int64_t VXSaveFile_writeData(XFileDevice* device, const char* data, int64_t len)
{
    XSaveFile* file = (XSaveFile*)device;
    if (!file || file->m_canceled || file->m_writeError) {
        return -1;
    }
    
    // 调用父类的写入实现
    int64_t written = XClass_Parent(XFileDevice,EXIODevice_WriteData, int64_t (*)(XFileDevice*, const char* , int64_t ))(device, data, len);
    
    // 检测写入错误（如磁盘满）
    if (written < 0 || written < len) {
        file->m_writeError = true;
        file->m_parent.m_error = XFileDevice_WriteError;
    }
    
    return written;
}

static void VXSaveFile_deinit(XSaveFile* file)
{
    if (!file) return;
    
    // 如果没有 commit，删除临时文件
    if (file->m_useTempFile && file->m_tempFileName && !file->m_committed) {
        XFileSystem_removePermanent(file->m_tempFileName);
    }
    
    // 关闭文件句柄
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XFileDevice_unregisterAsync(&file->m_parent);
        if (file->m_parent.m_handleFlags & XFileDevice_AutoCloseHandle) {
            XFileSystem_close(XIODevice_fd(&file->m_parent.m_parent));
        }
        XIODevice_setFd(&file->m_parent.m_parent, XFD_INVALID);
    }
    
    // 释放字符串
    if (file->m_fileName) {
        XString_delete_base(file->m_fileName);
        file->m_fileName = NULL;
    }
    if (file->m_tempFileName) {
        XString_delete_base(file->m_tempFileName);
        file->m_tempFileName = NULL;
    }
    
    XClass_Deinit_Parent(XFileDevice, file);
}

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

XVtable* XSaveFile_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSaveFile)
    XVTABLE_INHERIT_XCLASS(XFileDevice);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSaveFile_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXFileDevice_FileName, VXSaveFile_fileName);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXSaveFile_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXSaveFile_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VXSaveFile_writeData);

    XCLASS_SHOW_SIZE_DEFAULT(XSaveFile);
    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

XSaveFile* XSaveFile_create_1(void)
{
    XSaveFile* file = (XSaveFile*)XClass_Malloc(XSaveFile);
    if (!file) return NULL;
    XSaveFile_init_1(file);
    Set_Class_IsHeap(file, true);
    return file;
}

XSaveFile* XSaveFile_create_2(const XString* name)
{
    XSaveFile* file = XSaveFile_create_1();
    if (file && name) XSaveFile_setFileName(file, name);
    return file;
}

void XSaveFile_init_1(XSaveFile* file)
{
    if (!file) return;
    XFileDevice_init(&file->m_parent);
    XClassSetVtable(file, XSaveFile);
    
    file->m_fileName = NULL;
    file->m_tempFileName = NULL;
    file->m_useTempFile = false;
    file->m_canceled = false;
    file->m_committed = false;
    file->m_directWriteFallback = false;
    file->m_writeError = false;
}

void XSaveFile_init_2(XSaveFile* file, const XString* name)
{
    XSaveFile_init_1(file);
    if (file && name) XSaveFile_setFileName(file, name);
}

void XSaveFile_deinit_base(XSaveFile* file)
{
    VXSaveFile_deinit(file);
}

/* ============================================================================
 * 文件名操作
 * ============================================================================ */

void XSaveFile_setFileName(XSaveFile* file, const XString* name)
{
    if (!file || !name) return;
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) return;  // 文件已打开，不能修改
    
    if (file->m_fileName) XString_delete_base(file->m_fileName);
    file->m_fileName = XString_create_copy(name);
}

/* ============================================================================
 * 提交与取消
 * ============================================================================ */

bool XSaveFile_commit(XSaveFile* file)
{
    if (!file) return false;
    
    // 已经提交或取消
    if (file->m_committed || file->m_canceled) {
        if (file->m_useTempFile && file->m_tempFileName) {
            XFileSystem_removePermanent(file->m_tempFileName);
            XString_delete_base(file->m_tempFileName);
            file->m_tempFileName = NULL;
        }
        return false;
    }
    
    // 写入过程中有错误
    if (file->m_writeError) {
        if (file->m_useTempFile && file->m_tempFileName) {
            XFileSystem_removePermanent(file->m_tempFileName);
            XString_delete_base(file->m_tempFileName);
            file->m_tempFileName = NULL;
        }
        file->m_parent.m_error = XFileDevice_WriteError;
        return false;
    }
    
    // 刷新缓冲区
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XFileSystem_flush(XIODevice_fd(&file->m_parent.m_parent));
    }
    
    // 关闭文件
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XFileDevice_unregisterAsync(&file->m_parent);
        if (file->m_parent.m_handleFlags & XFileDevice_AutoCloseHandle) {
            XFileSystem_close(XIODevice_fd(&file->m_parent.m_parent));
        }
        XIODevice_setFd(&file->m_parent.m_parent, XFD_INVALID);
    }
    
    file->m_parent.m_parent.m_openMode = XIODevice_NotOpen;
    file->m_committed = true;
    
    // 直接写入模式（无临时文件）
    if (!file->m_useTempFile) {
        return true;
    }
    
    // 重命名临时文件为目标文件
    bool result = XFileSystem_rename(file->m_tempFileName, file->m_fileName);
    
    if (!result) {
        // 重命名失败，尝试复制后删除
        result = XFileSystem_copy(file->m_tempFileName, file->m_fileName);
        if (result) {
            XFileSystem_removePermanent(file->m_tempFileName);
        }
        file->m_parent.m_error = XFileDevice_RenameError;
    }
    
    // 清理临时文件名
    if (file->m_tempFileName) {
        XString_delete_base(file->m_tempFileName);
        file->m_tempFileName = NULL;
    }
    
    return result;
}

void XSaveFile_cancelWriting(XSaveFile* file)
{
    if (!file) return;
    file->m_canceled = true;
}

/* ============================================================================
 * 直接写入回退模式
 * ============================================================================ */

void XSaveFile_setDirectWriteFallback(XSaveFile* file, bool enabled)
{
    if (!file) return;
    file->m_directWriteFallback = enabled;
}

bool XSaveFile_directWriteFallback(const XSaveFile* file)
{
    if (!file) return false;
    return file->m_directWriteFallback;
}
#endif // XSAVEFILE_ON
#endif /* XFILE_ON */
