#include "XDeviceFile.h"
#include "XFileSystem_config.h"
#include "XFileInfo.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XFileDescriptor.h"
#include <stdlib.h>
#include <string.h>

/* 兼容旧 XFd 文件描述符的内部实现，不属于 XDeviceFile 公共 API。 */
void XDeviceFile_legacyClose(XFd fd);
int64_t XDeviceFile_legacyRead(XFd fd, void* buffer, int64_t size);
int64_t XDeviceFile_legacyWrite(XFd fd, const void* data, int64_t size);
int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence);
bool XDeviceFile_legacyFlush(XFd fd);
bool XDeviceFile_legacyResize(XFd fd, int64_t size);
XFd XDeviceFile_legacyOpen(const XString* path, int mode, uint32_t flags, int* error);
bool XDeviceFile_legacyFstat(XFd fd, XFileStat* stat);
bool XDeviceFile_legacySetFileTime(XFd fd, XFileTime timeType, int64_t timeValue);

#if XFILE_ON

/* ============================================================================
 * 打开上下文
 * ============================================================================ */
/**
 * @brief 文件设备打开上下文。
 * @details 每个 XDevice_open/openClass 调用独立分配一份，持有该次打开对应的
 *          原有文件系统内部 XFd 和打开选项，保证共享注册
 *          单例可并发打开。
 *          首成员为 XDeviceContext 基类，文件设备专有数据放在基类之后扩展。
 */
typedef struct XDeviceFileCtx
{
    XDeviceContext m_base;  /* 第一个成员，打开上下文基类，子类按需扩展。 */
    XFd m_fileFd;        /**< 原有文件系统内部描述符。 */
    int m_openMode;      /**< 打开模式（XIODeviceBaseMode 位组合）。 */
    uint32_t m_flags;    /**< XDeviceOpenFlag 位组合。 */
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
static bool  VXDeviceFile_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                                  const XVarList* in, XVarList* out);

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
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceFile_control);
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
    XDeviceFileCtx* ctx = NULL;
    int mode;
    XFd fileFd;
    int fsErr = 0;

    (void)self;
    if (!opts) {
        if (err) *err = (int)XDeviceError_InvalidArgument;
        return NULL;
    }
    if (!opts->m_target) {
        if (err) *err = (int)XDeviceError_InvalidArgument;
        return NULL;
    }

    mode = opts->m_openMode;
    if (mode == 0) mode = (int)XDeviceFile_ReadWrite;

    fileFd = XDeviceFile_legacyOpen(opts->m_target, mode, opts->m_flags, &fsErr);
    if (fileFd == XFD_INVALID) {
        if (err) *err = (int)XDeviceError_IoFail;
        return NULL;
    }

    ctx = (XDeviceFileCtx*)calloc(1, sizeof(XDeviceFileCtx));
    if (ISNULL(ctx, "")) {
        XDeviceFile_legacyClose(fileFd);
        if (err) *err = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    ctx->m_fileFd = fileFd;
    ctx->m_openMode = mode;
    ctx->m_flags = opts->m_flags;
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
            XDeviceFile_legacyClose(ctx->m_fileFd);
        free(ctx);
    }
}

static int64_t VXDeviceFile_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID || !buffer && size > 0)
        return -1;
    return XDeviceFile_legacyRead(ctx->m_fileFd, buffer, size);
}

static int64_t VXDeviceFile_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID || (!data && size > 0))
        return -1;
    return XDeviceFile_legacyWrite(ctx->m_fileFd, data, size);
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
    return XDeviceFile_legacySeek(ctx->m_fileFd, offset, fsWhence);
}

static bool VXDeviceFile_flush(XDevice* self, XDeviceContext* handle)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID)
        return false;
    return XDeviceFile_legacyFlush(ctx->m_fileFd);
}

static bool VXDeviceFile_resize(XDevice* self, XDeviceContext* handle, int64_t size)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    (void)self;
    if (!ctx || ctx->m_fileFd == XFD_INVALID || size < 0)
        return false;
    return XDeviceFile_legacyResize(ctx->m_fileFd, size);
}

static bool VXDeviceFile_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                                 const XVarList* in, XVarList* out)
{
    XDeviceFileCtx* ctx = (XDeviceFileCtx*)handle;
    XVarList* arguments = (XVarList*)in;
    XFileTime timeType;
    int64_t timeValue;
    int64_t offset;
    int64_t size;
    int flags;
    void* address;
    XFileStat stat;
    bool ok;
    (void)self;
    if (!ctx) {
        return false;
    }

    switch ((XDeviceFileCommand)command) {
    case XDeviceFileCommand_GetFileStat:
        if (arguments || !out || out->m_size != sizeof(XFileStat))
            return false;
        ok = ctx->m_fileFd != XFD_INVALID && XDeviceFile_legacyFstat(ctx->m_fileFd, &stat);
        if (ok) {
            memcpy(out->data, &stat, sizeof(stat));
            XVarList_start(out);
        }
        break;
    case XDeviceFileCommand_Map:
        if (!arguments || arguments->m_size != sizeof(int64_t) + sizeof(int64_t) + sizeof(int) ||
            !out || out->m_size != sizeof(void*))
            return false;
        XVarList_start(arguments);
        offset = XVarList_arg(arguments, int64_t);
        size = XVarList_arg(arguments, int64_t);
        flags = XVarList_arg(arguments, int);
        address = ctx->m_fileFd == XFD_INVALID ? NULL :
                  XDeviceFile_map(ctx->m_fileFd, offset, size, flags);
        ok = address != NULL;
        if (ok) {
            memcpy(out->data, &address, sizeof(address));
            XVarList_start(out);
        }
        break;
    case XDeviceFileCommand_Unmap:
        if (!arguments || arguments->m_size != sizeof(void*) + sizeof(int64_t))
            return false;
        XVarList_start(arguments);
        address = XVarList_arg(arguments, void*);
        size = XVarList_arg(arguments, int64_t);
        ok = XDeviceFile_unmap(address, size);
        break;
    case XDeviceFileCommand_SetFileTime:
        if (!arguments || arguments->m_size != sizeof(XFileTime) + sizeof(int64_t))
            return false;
        XVarList_start(arguments);
        timeType = XVarList_arg(arguments, XFileTime);
        timeValue = XVarList_arg(arguments, int64_t);
        ok = ctx->m_fileFd != XFD_INVALID &&
             XDeviceFile_legacySetFileTime(ctx->m_fileFd, timeType, timeValue);
        break;
    default:
        return false;
    }
    ctx->m_base.m_lastError = (int16_t)(ok ? XDeviceError_None : XDeviceError_IoFail);
    return ok;
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
        if (ctx->m_fileFd == XFD_INVALID || !XDeviceFile_legacyFstat(ctx->m_fileFd, &st))
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
        if (ctx->m_fileFd == XFD_INVALID || !XDeviceFile_legacyFstat(ctx->m_fileFd, &st))
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
