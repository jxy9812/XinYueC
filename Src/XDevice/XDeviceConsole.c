#include "XDeviceConsole.h"
#include "XFileDescriptor.h"
#include "XMemory.h"
#include "XVarList.h"
#include <string.h>

/* 现有平台文件后端负责创建和操作终端私有状态。 */
XFd XDeviceFile_openStandardInput(int* error);
void XDeviceFile_legacyClose(XFd fd);
int64_t XDeviceFile_legacyRead(XFd fd, void* buffer, int64_t size);
int64_t XDeviceFile_legacyWrite(XFd fd, const void* data, int64_t size);
bool XDeviceFile_legacyFlush(XFd fd);
bool XDeviceFile_legacySetStandardInputEcho(XFd fd, bool enabled);

static XDeviceConsole g_deviceConsole;

static XDeviceContext* VXDeviceConsole_open(XDevice* self,
    const XDeviceOpenOptions* options, int* error);
static void VXDeviceConsole_close(XDevice* self, XDeviceContext* handle);
static int64_t VXDeviceConsole_read(XDevice* self, XDeviceContext* handle,
    void* buffer, int64_t size);
static int64_t VXDeviceConsole_write(XDevice* self, XDeviceContext* handle,
    const void* data, int64_t size);
static bool VXDeviceConsole_flush(XDevice* self, XDeviceContext* handle);
static bool VXDeviceConsole_control(XDevice* self, XDeviceContext* handle,
    uint32_t command, const XVarList* in, XVarList* out);

static XDeviceConsoleContext* consoleContext(XDeviceContext* handle)
{
    return (XDeviceConsoleContext*)handle;
}

static bool consoleSetError(XDeviceConsoleContext* context, XDeviceError error)
{
    if (context) context->m_base.m_lastError = (int16_t)error;
    return false;
}

XVtable* XDeviceConsole_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceConsole)
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceConsole_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceConsole_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Read, VXDeviceConsole_read);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Write, VXDeviceConsole_write);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Flush, VXDeviceConsole_flush);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceConsole_control);
    XCLASS_SET_CLASS_NAME_DEFAULT("console");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceConsole);
    return XVTABLE_DEFAULT;
}

void XDeviceConsole_init(XDeviceConsole* self)
{
    if (!self) return;
    memset(((XDevice*)self) + 1, 0, sizeof(*self) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceConsole);
    self->m_base.m_type = XDeviceType_Console;
    /* 当前设备语义是标准输入控制台；输出仍由 Shell/上层输出通道负责。 */
    self->m_base.m_capabilities = XDeviceCap_Read;
}

XDeviceConsole* XDeviceConsole_create(void)
{
    XDeviceConsole* self = (XDeviceConsole*)XClass_Malloc(XDeviceConsole);
    if (!self) return NULL;
    XDeviceConsole_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

bool XDeviceConsole_register(void)
{
    static bool registered = false;
    if (registered) return true;
    XDeviceConsole_init(&g_deviceConsole);
    if (!XDevice_register(&g_deviceConsole.m_base)) return false;
    registered = true;
    return true;
}

static XDeviceContext* VXDeviceConsole_open(XDevice* self,
    const XDeviceOpenOptions* options, int* error)
{
    XDeviceConsoleContext* context;
    XFd backendFd;
    (void)self;

    /* 控制台设备只表示当前标准输入，不接受路径目标。 */
    if (options && options->m_target) {
        if (error) *error = (int)XDeviceError_InvalidArgument;
        return NULL;
    }
    backendFd = XDeviceFile_openStandardInput(error);
    if (backendFd == XFD_INVALID) return NULL;

    context = (XDeviceConsoleContext*)XCalloc_System(1, sizeof(*context));
    if (!context) {
        XDeviceFile_legacyClose(backendFd);
        if (error) *error = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    context->m_backendFd = backendFd;
    context->m_base.m_fd = XFd_alloc(XFD_TYPE_CLASS, &context->m_base, NULL);
    if (context->m_base.m_fd == XFD_INVALID) {
        XDeviceFile_legacyClose(backendFd);
        XFree_System(context);
        if (error) *error = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    context->m_base.m_device = self;
    context->m_base.m_state = (uint16_t)XDeviceState_Opening;
    context->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Sync;
    context->m_base.m_lastError = (int16_t)XDeviceError_None;
    if (error) *error = (int)XDeviceError_None;
    return &context->m_base;
}

static void VXDeviceConsole_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceConsoleContext* context = consoleContext(handle);
    (void)self;
    if (!context) return;
    if (context->m_backendFd != XFD_INVALID) {
        XDeviceFile_legacyClose(context->m_backendFd);
        context->m_backendFd = XFD_INVALID;
    }
    XFree_System(context);
}

static int64_t VXDeviceConsole_read(XDevice* self, XDeviceContext* handle,
    void* buffer, int64_t size)
{
    XDeviceConsoleContext* context = consoleContext(handle);
    (void)self;
    if (!context || context->m_backendFd == XFD_INVALID ||
        (!buffer && size > 0) || size < 0)
        return -1;
    return XDeviceFile_legacyRead(context->m_backendFd, buffer, size);
}

static int64_t VXDeviceConsole_write(XDevice* self, XDeviceContext* handle,
    const void* data, int64_t size)
{
    XDeviceConsoleContext* context = consoleContext(handle);
    (void)self;
    if (!context || context->m_backendFd == XFD_INVALID ||
        (!data && size > 0) || size < 0)
        return -1;
    return XDeviceFile_legacyWrite(context->m_backendFd, data, size);
}

static bool VXDeviceConsole_flush(XDevice* self, XDeviceContext* handle)
{
    XDeviceConsoleContext* context = consoleContext(handle);
    (void)self;
    if (!context || context->m_backendFd == XFD_INVALID) return false;
    return XDeviceFile_legacyFlush(context->m_backendFd);
}

static bool VXDeviceConsole_control(XDevice* self, XDeviceContext* handle,
    uint32_t command, const XVarList* in, XVarList* out)
{
    XDeviceConsoleContext* context = consoleContext(handle);
    XVarList* input = (XVarList*)in;
    bool enabled;
    bool result;
    (void)self;
    if (!context || context->m_backendFd == XFD_INVALID ||
        command != XDeviceConsoleCommand_SetEcho || !input || out ||
        input->m_size != sizeof(enabled))
        return consoleSetError(context, XDeviceError_InvalidArgument);
    XVarList_start(input);
    enabled = XVarList_arg(input, bool);
    result = XDeviceFile_legacySetStandardInputEcho(context->m_backendFd, enabled);
    context->m_base.m_lastError = (int16_t)(result ? XDeviceError_None :
        XDeviceError_IoFail);
    return result;
}

XFd XDeviceConsole_openStandardInput(int* error)
{
    XDeviceConsoleOpenOptions options;
    memset(&options, 0, sizeof(options));
    return XDeviceConsole_open(&options, error);
}

bool XDeviceConsole_setEcho(XFd fd, bool enabled)
{
    XVarList* input = XVarList_Create(XVar(bool, enabled));
    bool result;
    if (!input) return false;
    result = XDeviceConsole_control(fd, XDeviceConsoleCommand_SetEcho,
        input, NULL);
    XVarList_delete(input);
    return result;
}

XFd XDeviceConsole_backendFd(XFd fd)
{
    XDeviceContext* handle;
    if (fd == XFD_INVALID || XFd_type(fd) != XFD_TYPE_CLASS) return XFD_INVALID;
    handle = XDevice_handle(fd);
    if (!handle || !handle->m_device ||
        handle->m_device->m_type != XDeviceType_Console)
        return XFD_INVALID;
    return consoleContext(handle)->m_backendFd;
}
