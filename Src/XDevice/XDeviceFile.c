#include "XDeviceFile.h"
#include "XFileSystem.h"
#include "XFileSystem_config.h"
#include "XFileInfo.h"
#include "XVariant.h"
#include "XFileDescriptor.h"
#include <stdlib.h>
#include <string.h>

#if XFILE_ON

/* ============================================================================
 * 打开上下文
 * ============================================================================ */
/**
 * @brief 文件设备打开上下文。
 * @details 每个 XDevice_open/openClass 调用独立分配一份，持有该次打开对应的
 *          底层 XFileSystem 文件描述符和打开选项，保证共享注册单例可并发打开。
 *          首成员为 XDeviceContext 基类，文件设备专有数据放在基类之后扩展。
 */
typedef struct XDeviceFileCtx
{
    XDeviceContext m_base;  /* 第一个成员，打开上下文基类，子类按需扩展。 */
    XFd m_fileFd;        /**< XFileSystem 返回的文件描述符；XFD_INVALID 表示无效。 */
    int m_openMode;      /**< 打开模式（XIODeviceBaseMode 位组合）。 */
    uint32_t m_flags;    /**< XDeviceOpenFlag 位组合。 */
    uint64_t m_bufferSize; /**< 请求的读写缓冲字节数；0 表示设备默认。 */
} XDeviceFileCtx;

/* ============================================================================
 * 虚函数原型（内部实现前缀 V + 类名，按代码风格指南执行）
 * ============================================================================ */
static XDeviceContext* VXDeviceFile_open(XDevice* self, const XDeviceOpenOptions* opts, int* err);
static void  VXDeviceFile_close(XDevice* self, XDeviceContext* handle);
static int64_t VXDeviceFile_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size);
static int64_t VXDeviceFile_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size);
static int64_t VXDeviceFile_seek(XDevice* self, XDeviceContext* handle, int64_t offset, int whence);
static bool  VXDeviceFile_flush(XDevice* self, XDeviceContext* handle);
static bool  VXDeviceFile_resize(XDevice* self, XDeviceContext* handle, int64_t size);
static bool  VXDeviceFile_getProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value);
static bool  VXDeviceFile_queryProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value);

/* ============================================================================
 * 类虚函数表
 * ============================================================================ */
XVtable* XDeviceFile_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceFile)
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceFile_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceFile_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Read, VXDeviceFile_read);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Write, VXDeviceFile_write);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Seek, VXDeviceFile_seek);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Flush, VXDeviceFile_flush);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Resize, VXDeviceFile_resize);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_GetProperty, VXDeviceFile_getProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_QueryProperty, VXDeviceFile_queryProperty);
    XCLASS_SET_CLASS_NAME_DEFAULT("file");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceFile);
    return XVTABLE_DEFAULT;
}

void XDeviceFile_init(XDeviceFile* self)
{
    if (ISNULL(self, "")) return;
    memset(((XDevice*)self) + 1, 0, sizeof(XDeviceFile) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceFile);
    self->m_base.m_type = XDeviceType_File;
    self->m_base.m_capabilities =
        XDeviceCap_Read | XDeviceCap_Write | XDeviceCap_Seek |
        XDeviceCap_Flush | XDeviceCap_Resize;
}

XDeviceFile* XDeviceFile_create(void)
{
    XDeviceFile* self = (XDeviceFile*)XClass_Malloc(XDeviceFile);
    if (ISNULL(self, "")) return NULL;
    XDeviceFile_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ============================================================================
 * 虚函数实现
 * ============================================================================ */
static XDeviceContext* VXDeviceFile_open(XDevice* self, const XDeviceOpenOptions* opts, int* err)
{
    const XDeviceFileOpenOptions* fopts;
    XDeviceFileCtx* ctx = NULL;
    XFd fileFd;
    int mode;
    int fsErr = 0;

    (void)self;
    if (!opts) {
        if (err) *err = (int)XDeviceError_InvalidArgument;
        return NULL;
    }
    fopts = (const XDeviceFileOpenOptions*)opts;
    if (!fopts->m_path) {
        if (err) *err = (int)XDeviceError_InvalidArgument;
        return NULL;
    }

    mode = fopts->m_base.m_openMode;
    if (mode == 0) mode = (int)XFileSystem_ReadWrite;

    fileFd = XFileSystem_open(fopts->m_path, mode, &fsErr);
    if (fileFd == XFD_INVALID) {
        if (err) *err = (int)XDeviceError_IoFail;
        return NULL;
    }

    ctx = (XDeviceFileCtx*)calloc(1, sizeof(XDeviceFileCtx));
    if (ISNULL(ctx, "")) {
        XFileSystem_close(fileFd);
        if (err) *err = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    ctx->m_fileFd = fileFd;
    ctx->m_openMode = mode;
    ctx->m_flags = fopts->m_base.m_flags;
    ctx->m_bufferSize = fopts->m_bufferSize;
    ctx->m_base.m_device = self;
    ctx->m_base.m_state = (uint16_t)XDeviceState_Active;
    ctx->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Sync;
    ctx->m_base.m_pendingOps = 0;
    ctx->m_base.m_lastError = (int16_t)XDeviceError_None;
    if (err) *err = (int)XDeviceError_None;
    return &ctx->m_base;
}

static void VXDeviceFile_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (ctx) {
        if (ctx->m_fileFd != XFD_INVALID)
            XFileSystem_close(ctx->m_fileFd);
        free(ctx);
    }
}

static int64_t VXDeviceFile_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID || !buffer && size > 0)
        return -1;
    return XFileSystem_read(ctx->m_fileFd, buffer, size);
}

static int64_t VXDeviceFile_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID || (!data && size > 0))
        return -1;
    return XFileSystem_write(ctx->m_fileFd, data, size);
}

static int64_t VXDeviceFile_seek(XDevice* self, XDeviceContext* handle, int64_t offset, int whence)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    XSeekWhence fsWhence;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID)
        return -1;
    switch (whence) {
    case XDeviceSeekWhence_Begin:   fsWhence = XSeekSet; break;
    case XDeviceSeekWhence_Current: fsWhence = XSeekCur; break;
    case XDeviceSeekWhence_End:     fsWhence = XSeekEnd; break;
    default: return -1;
    }
    return XFileSystem_seek(ctx->m_fileFd, offset, fsWhence);
}

static bool VXDeviceFile_flush(XDevice* self, XDeviceContext* handle)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID)
        return false;
    return XFileSystem_flush(ctx->m_fileFd);
}

static bool VXDeviceFile_resize(XDevice* self, XDeviceContext* handle, int64_t size)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID || size < 0)
        return false;
    return XFileSystem_resize(ctx->m_fileFd, size);
}

static bool VXDeviceFile_getProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || !value) return false;

    switch ((int)property) {
    case XDeviceProperty_OpenMode:
        XVariant_setValue_int(value, ctx->m_openMode);
        return true;
    case XDeviceProperty_Size:
    {
        XFileStat st;
        if (ctx->m_fileFd == XFD_INVALID || !XFileSystem_fstat(ctx->m_fileFd, &st))
            return false;
        XVariant_setValue_int64(value, st.size);
        return true;
    }
    case XDeviceProperty_NativeHandle:
        XVariant_setValue_ptr(value, (void*)XFd_handle(ctx->m_fileFd));
        return true;
    case XDeviceProperty_NonBlocking:
        XVariant_setValue_bool(value, (ctx->m_flags & XDeviceOpenFlag_NonBlocking) != 0);
        return true;
    case XDeviceProperty_IoMode:
        XVariant_setValue_int(value, (int)ctx->m_base.m_ioMode);
        return true;
    default:
        return false;
    }
}

static bool VXDeviceFile_queryProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || !value) return false;

    switch ((int)property) {
    case XDeviceProperty_OpenMode:
        XVariant_setValue_int(value, ctx->m_openMode);
        return true;
    case XDeviceProperty_IoMode:
        XVariant_setValue_int(value, (int)ctx->m_base.m_ioMode);
        return true;
    case XDeviceProperty_State:
        XVariant_setValue_int(value, (int)ctx->m_base.m_state);
        return true;
    case XDeviceProperty_NonBlocking:
        XVariant_setValue_bool(value, (ctx->m_flags & XDeviceOpenFlag_NonBlocking) != 0);
        return true;
    case XDeviceProperty_NativeHandle:
        XVariant_setValue_ptr(value, (void*)XFd_handle(ctx->m_fileFd));
        return true;
    case XDeviceProperty_Size:
    {
        XFileStat st;
        if (ctx->m_fileFd == XFD_INVALID || !XFileSystem_fstat(ctx->m_fileFd, &st))
            return false;
        XVariant_setValue_int64(value, st.size);
        return true;
    }
    default:
        return false;
    }
}

/* ============================================================================
 * 注册
 * ============================================================================ */
static XDeviceFile g_deviceFile; /* 内置文件设备静态单例 */

bool XDeviceFile_register(void)
{
    static bool registered = false;
    if (registered) return true;
    XDeviceFile_init(&g_deviceFile);
    if (!XDevice_register(&g_deviceFile.m_base))
        return false;
    registered = true;
    return true;
}

#else /* !XFILE_ON */

bool XDeviceFile_register(void) { return false; }

#endif /* XFILE_ON */
