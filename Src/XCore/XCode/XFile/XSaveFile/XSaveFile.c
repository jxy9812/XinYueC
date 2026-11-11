#include "XSaveFile.h"
#include "XIODevice_Protected.h"
#include "XDeviceFile.h"
#include "XSystem.h"           /* XSystem_environment：TMPDIR 回退 */
#include "XDateTime.h"         /* 唯一名时间戳（单调毫秒） */
#include <stdlib.h>
#include <string.h>
#if XFILE_ON
#if XSAVEFILE_ON

static XFd xsavefile_open_file(const XString* path, int mode, int* error)
{
    XDeviceOpenOptions options;
    memset(&options, 0, sizeof(options));
    options.m_openMode = mode;
    options.m_target = path;
    return XDevice_open(XDeviceType_File, &options, error);
}

/**
 * @brief 句柄就绪后的统一收口：登记 fd/句柄标志，并交给基类 open 复位状态。
 * @details 对齐 QSaveFile::open：文件引擎打开成功后调 `QFileDevice::open(mode)`
 *          （qsavefile.cpp:204-207 的直接写分支同款），由基类置 openMode /
 *          pos / 读通道数 / 清 errorString。原实现直接写 `m_openMode`，
 *          读通道数恒 0、pos 与事务不复位（对齐台账 gaps #25）。
 * @return 基类拒绝（只剩私有数据缺失）时回滚句柄并返回 false。
 */
static bool xsavefile_finishOpen(XSaveFile* file, XFd fd, XIODeviceBaseMode mode)
{
    XIODevice* io = (XIODevice*)&file->m_parent.m_parent;
    XIODevice_setFd(io, fd);
    file->m_parent.m_handleFlags = XFileDevice_AutoCloseHandle;
    file->m_parent.m_error = XFileDevice_NoError;
    if (!XIODevice_open(io, mode)) {
        XDeviceFile_close(fd);
        XIODevice_setFd(io, XFD_INVALID);
        return false;
    }
    XFileDevice_registerAsync(&file->m_parent);
    return true;
}

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
 * 匿名临时文件（C 标准 tmpfile/tmpnam 的库内等价物，嵌入式适配）
 * ============================================================================ */

static XString* xsavefile_tempDir = NULL;   /* 模块默认临时目录；NULL=未设置 */
static uint32_t xsavefile_tempCounter = 0;  /* 唯一名辅助序号 */
static bool xsavefile_tempDirAtExitRegistered = false;

/* 进程退出兜底释放（对齐 XGpuRenderBackend/XPainter 的 atexit 惯例）：
 * 模块级静态 XString 无其他释放点，否则堆串驻留至进程结束。 */
static void xsavefile_tempDir_atexit(void)
{
    if (xsavefile_tempDir) {
        XClassDelete((XClass*)xsavefile_tempDir);
        xsavefile_tempDir = NULL;
    }
}

void XSaveFile_setTempDir_static(const XString* dir)
{
    if (!xsavefile_tempDirAtExitRegistered) {
        if (atexit(xsavefile_tempDir_atexit) == 0)
            xsavefile_tempDirAtExitRegistered = true;
    }
    if (xsavefile_tempDir) {
        XClassDelete((XClass*)xsavefile_tempDir);
        xsavefile_tempDir = NULL;
    }
    if (dir) xsavefile_tempDir = XString_create_utf8(XString_toUtf8(dir));
}

XString* XSaveFile_tempDir_static(void)
{
    if (xsavefile_tempDir)
        return XString_create_utf8(XString_toUtf8(xsavefile_tempDir));
    /* 未设置时回退 TMPDIR 环境变量，再退当前目录（嵌入式建议显式
       setTempDir 指向可写 scratch 挂载点）。 */
    const char* env = XSystem_environment("TMPDIR");
    return XString_create_utf8((env && env[0]) ? env : ".");
}

XString* XSaveFile_uniqueTempPath_static(const XString* prefix)
{
    enum { XSAVEFILE_UNIQUE_ATTEMPTS = 8 };
    XString* dir = XSaveFile_tempDir_static();
    XString* path = NULL;
    bool found = false;
    int attempt;

    if (!dir) return NULL;
    for (attempt = 0; attempt < XSAVEFILE_UNIQUE_ATTEMPTS && !found; ++attempt) {
        unsigned long stamp =
            (unsigned long)(XDateTime_currentMSecsSinceEpoch() & 0xFFFFFFFFul) ^
            (unsigned long)(++xsavefile_tempCounter << 20);
        path = XString_create_utf8(XString_toUtf8(dir));
        if (!path) break;
        XString_append_utf8(path, "/");
        if (prefix && !XString_isEmpty_base((const XContainer*)prefix))
            XString_append_utf8(path, XString_toUtf8(prefix));
        XString_append_utf8(path, ".tmp.");
        {
            XString tail;
            XString_init(&tail);
            XString_setNum_uLong(&tail, stamp, 16);
            XString_append(path, &tail);
            XClassDeinit((XClass*)&tail);
        }
        if (!XDeviceFile_exists(path)) {
            found = true;
        } else {
            XClassDelete((XClass*)path);
            path = NULL;
        }
    }
    XClassDelete((XClass*)dir);
    return found ? path : NULL;
}

bool XSaveFile_openUniqueTemp(XSaveFile* file, const XString* prefix)
{
    XString* path = XSaveFile_uniqueTempPath_static(prefix);
    bool result = false;
    if (!file || !path) {
        if (path) XClassDelete((XClass*)path);
        return false;
    }
    /* 唯一名设为目标名 → 既有 open 流程在其旁生成 ".XXXXXX" 工作临时
       文件；从不 commit → deinit 自动删除（tmpfile 的用后即焚语义）。 */
    XSaveFile_setFileName(file, path);
    result = XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly);
    XClassDelete((XClass*)path);
    return result;
}

/* ============================================================================
 * 虚函数实现（重写父类虚函数）
 * ============================================================================ */

/* 关闭当前 fd（deinit/commit 共用的收尾序列）。 */
static void xsavefile_close_handle(XSaveFile* file)
{
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XFileDevice_unregisterAsync(&file->m_parent);
        if (file->m_parent.m_handleFlags & XFileDevice_AutoCloseHandle) {
            XDeviceFile_close(XIODevice_fd(&file->m_parent.m_parent));
        }
        XIODevice_setFd(&file->m_parent.m_parent, XFD_INVALID);
    }
}

/* 丢弃未提交的临时文件。必须先关句柄再删：Win32 上 legacyOpen 不带
 * FILE_SHARE_DELETE（shareMode 仅 FILE_SHARE_READ），DeleteFileW 对仍
 * 打开的句柄必然共享冲突失败——先删后关会让临时文件滞留磁盘且随后
 * m_tempFileName 被释放、永远失去重试入口；POSIX unlink 打开中文件不受
 * 影响。删除失败时保留 m_tempFileName，由 deinit 兜底重试（届时句柄
 * 必已关闭，Win32 上删除可成功）。 */
static void xsavefile_discard_temp_file(XSaveFile* file)
{
    xsavefile_close_handle(file);
    if (file->m_useTempFile && file->m_tempFileName && !file->m_committed) {
        if (XDeviceFile_removePermanent(file->m_tempFileName)) {
            XClassDelete(file->m_tempFileName);
            file->m_tempFileName = NULL;
        }
    }
}

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

    /* 已处于打开态（含 close 为空操作后的重开）时拒绝重复 open：否则
     * m_tempFileName/m_fd 被直接覆盖，旧堆串与旧 OS 句柄（含全局 IoRing
     * 注册）泄漏且不可回收（对齐 QIODevice::open 对已打开设备返回 false）。 */
    if (file->m_tempFileName || XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        return false;
    }

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
            XClassDelete(file->m_tempFileName);
            file->m_tempFileName = NULL;
            return false;
        }
        
        // 保存临时文件名
        file->m_useTempFile = true;
        
        // 打开临时文件
        int fsMode = XDeviceFile_WriteOnly | XDeviceFile_Create | XDeviceFile_Truncate;
        int error = 0;
        XFd fd = xsavefile_open_file(file->m_tempFileName, fsMode, &error);
        
        if (fd < 0) {
            XClassDelete(file->m_tempFileName);
            file->m_tempFileName = NULL;
            return false;
        }
        
        return xsavefile_finishOpen(file, fd, mode);
    } else {
        // 允许回退到直接写入
        int fsMode = XDeviceFile_WriteOnly | XDeviceFile_Create | XDeviceFile_Truncate;
        int error = 0;
        XFd fd = xsavefile_open_file(file->m_fileName, fsMode, &error);
        
        if (fd >= 0) {
            // 直接写入成功
            file->m_tempFileName = NULL;
            file->m_useTempFile = false;
            return xsavefile_finishOpen(file, fd, mode);
        }
        
        // 尝试创建临时文件
        file->m_tempFileName = XString_create();
        if (!file->m_tempFileName) return false;
        
        if (!XSaveFile_generateTempFileName(file->m_fileName, file->m_tempFileName)) {
            XClassDelete(file->m_tempFileName);
            file->m_tempFileName = NULL;
            return false;
        }
        file->m_useTempFile = true;
        
        fd = xsavefile_open_file(file->m_tempFileName, fsMode, &error);
        
        if (fd < 0) {
            XClassDelete(file->m_tempFileName);
            file->m_tempFileName = NULL;
            return false;
        }
        
        return xsavefile_finishOpen(file, fd, mode);
    }
}

/**
 * @brief close：**故意为空操作**（对齐 Qt 6.8 的 QSaveFile::close）。
 * @details Qt 6.8 `QSaveFile::close()` 的原文就是
 *          `qFatal("QSaveFile::close called")`（qsavefile.cpp:272-275）——
 *          在 Qt 里调 close 会**直接终止进程**（头文件里还把它声明为
 *          private，qsavefile.h:54）。本框架是嵌入式/现场框架，不能因为一次
 *          误调 close 就 abort 整个进程，故取「不改变任何状态」的等价实现：
 *          临时文件保持打开，唯一合法出口仍是 `XSaveFile_commit`
 *          （或析构时的丢弃路径）。这是**项目有意偏差**，依据即上述 Qt 原文。
 */
static void VXSaveFile_close(XIODevice* device)
{
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

    // 如果没有 commit，删除临时文件（先关句柄再删：Win32 上打开中的
    // 句柄会使 DeleteFileW 共享冲突失败，见 xsavefile_discard_temp_file）
    xsavefile_discard_temp_file(file);

    // 释放字符串
    if (file->m_fileName) {
        XClassDelete(file->m_fileName);
        file->m_fileName = NULL;
    }
    if (file->m_tempFileName) {
        XClassDelete(file->m_tempFileName);
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

/* ============================================================================
 * 文件名操作
 * ============================================================================ */

void XSaveFile_setFileName(XSaveFile* file, const XString* name)
{
    if (!file || !name) return;
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) return;  // 文件已打开，不能修改
    
    if (file->m_fileName) XClassDelete(file->m_fileName);
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
        xsavefile_discard_temp_file(file);
        return false;
    }

    // 写入过程中有错误
    if (file->m_writeError) {
        xsavefile_discard_temp_file(file);
        file->m_parent.m_error = XFileDevice_WriteError;
        return false;
    }

    // 刷新缓冲区
    if (XIODevice_fd(&file->m_parent.m_parent) >= 0) {
        XDeviceFile_flush(XIODevice_fd(&file->m_parent.m_parent));
    }

    // 关闭文件（先于 rename/remove：Win32 上打开中的句柄会令目标替换
    // 与临时文件删除共享冲突失败）
    xsavefile_close_handle(file);

    /* 对齐 Qt `QSaveFile::commit()` 里的 `QFileDevice::close()`
     * （qsavefile.cpp:299）：走基类 close 收口 openMode / pos / 事务 /
     * 读通道数并发出 aboutToClose（原实现只把 m_openMode 置 NotOpen，
     * 通道数与位置不复位）。 */
    XIODevice_close((XIODevice*)&file->m_parent.m_parent);
    file->m_committed = true;

    // 直接写入模式（无临时文件）
    if (!file->m_useTempFile) {
        return true;
    }

    // 重命名临时文件为目标文件（替换语义：目标已存在时原子覆盖，
    // XDeviceFile.h §XDeviceFile_renameReplace；commit 由此获得
    // "任意时刻断电=旧文件或新文件" 的不变式 2 原子性）
    bool result = XDeviceFile_renameReplace(file->m_tempFileName, file->m_fileName);
    bool tempGone = result; /* rename 成功=临时文件名已不存在于磁盘 */

    if (!result) {
        // 重命名失败，尝试复制后删除
        result = XDeviceFile_copy(file->m_tempFileName, file->m_fileName);
        if (result) {
            tempGone = XDeviceFile_removePermanent(file->m_tempFileName);
        }
        file->m_parent.m_error = XFileDevice_RenameError;
    }

    // 清理临时文件名
    if (file->m_tempFileName) {
        if (tempGone) {
            XClassDelete(file->m_tempFileName);
            file->m_tempFileName = NULL;
        } else {
            /* rename+copy 双败（或 copy 成功但删除失败）时磁盘临时文件
             * 仍在：保留 m_tempFileName 并回退 m_committed，交由 deinit/
             * 后续 commit 的丢弃路径兜底删除（句柄已关，Win32 上删除可
             * 成功）；此处若照旧释放文件名，磁盘文件将成永无清理入口的
             * 孤儿。置错误态：后续 commit 走丢弃早退，不会把旧临时文件
             * 重命名到 setFileName 换上的新目标。 */
            file->m_committed = false;
            file->m_writeError = true;
        }
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
