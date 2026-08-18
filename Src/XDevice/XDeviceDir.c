#include "XDeviceDir.h"
#include "XVarList.h"
#include "XMemory.h"
#include "XString.h"
#include "XFileDescriptor.h"
#include <string.h>

/* 平台目录后端只返回不透明句柄，不接触 XFd 生命周期。 */
void* XDeviceDir_platformOpen(const XString* path);
bool XDeviceDir_platformRead(void* backendHandle, XDirEntry* entry);
void XDeviceDir_platformClose(void* backendHandle);

static XDeviceDir g_deviceDir;

static XDeviceContext* VXDeviceDir_open(XDevice* self,
    const XDeviceOpenOptions* options, int* error);
static void VXDeviceDir_close(XDevice* self, XDeviceContext* handle);
static bool VXDeviceDir_control(XDevice* self, XDeviceContext* handle,
    uint32_t command, const XVarList* in, XVarList* out);

static XDeviceDirContext* dirContext(XDeviceContext* handle)
{
    return (XDeviceDirContext*)handle;
}

static bool dirSetError(XDeviceDirContext* context, XDeviceError error)
{
    if (context) context->m_base.m_lastError = (int16_t)error;
    return false;
}

XVtable* XDeviceDir_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceDir)
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceDir_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceDir_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceDir_control);
    XCLASS_SET_CLASS_NAME_DEFAULT("dir");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceDir);
    return XVTABLE_DEFAULT;
}

void XDeviceDir_init(XDeviceDir* self)
{
    if (!self) return;
    memset(((XDevice*)self) + 1, 0, sizeof(*self) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceDir);
    self->m_base.m_type = XDeviceType_Dir;
    self->m_base.m_capabilities = XDeviceCap_None;
}

XDeviceDir* XDeviceDir_create(void)
{
    XDeviceDir* self = (XDeviceDir*)XClass_Malloc(XDeviceDir);
    if (!self) return NULL;
    XDeviceDir_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

bool XDeviceDir_register(void)
{
    static bool registered = false;
    if (registered) return true;
    XDeviceDir_init(&g_deviceDir);
    if (!XDevice_register(&g_deviceDir.m_base)) return false;
    registered = true;
    return true;
}

static XDeviceContext* VXDeviceDir_open(XDevice* self,
    const XDeviceOpenOptions* options, int* error)
{
    XDeviceDirContext* context;
    void* backendHandle;
    (void)self;

    if (!options || !options->m_target) {
        if (error) *error = (int)XDeviceError_InvalidArgument;
        return NULL;
    }
    backendHandle = XDeviceDir_platformOpen(options->m_target);
    if (!backendHandle) {
        if (error) *error = (int)XDeviceError_IoFail;
        return NULL;
    }
    context = (XDeviceDirContext*)XCalloc_System(1, sizeof(*context));
    if (!context) {
        XDeviceDir_platformClose(backendHandle);
        if (error) *error = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    context->m_base.m_fd = XFd_alloc(XFD_TYPE_CLASS, &context->m_base, NULL);
    if (context->m_base.m_fd == XFD_INVALID) {
        XDeviceDir_platformClose(backendHandle);
        XFree_System(context);
        if (error) *error = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    context->m_backendHandle = backendHandle;
    context->m_base.m_device = self;
    context->m_base.m_state = (uint16_t)XDeviceState_Opening;
    context->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Sync;
    context->m_base.m_lastError = (int16_t)XDeviceError_None;
    if (error) *error = (int)XDeviceError_None;
    return &context->m_base;
}

static void VXDeviceDir_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceDirContext* context = dirContext(handle);
    (void)self;
    if (!context) return;
    if (context->m_backendHandle)
        XDeviceDir_platformClose(context->m_backendHandle);
    XFree_System(context);
}

static bool VXDeviceDir_control(XDevice* self, XDeviceContext* handle,
    uint32_t command, const XVarList* in, XVarList* out)
{
    XDeviceDirContext* context = dirContext(handle);
    XDirEntry* entry;
    XVarList* output = (XVarList*)out;
    bool result;
    (void)self;
    if (!context || !context->m_backendHandle)
        return false;
    if (command != XDeviceDirCommand_ReadNext || in || !output ||
        output->m_size != sizeof(entry))
        return dirSetError(context, XDeviceError_InvalidArgument);
    XVarList_start(output);
    entry = XVarList_arg(output, XDirEntry*);
    if (!entry || !entry->name)
        return dirSetError(context, XDeviceError_InvalidArgument);
    result = XDeviceDir_platformRead(context->m_backendHandle, entry);
    context->m_base.m_lastError = (int16_t)(result ? XDeviceError_None : XDeviceError_IoFail);
    return result;
}

XFd XDeviceDir_openPath(const XString* path, int* error)
{
    XDeviceDirOpenOptions options;
    memset(&options, 0, sizeof(options));
    options.m_base.m_target = path;
    return XDeviceDir_open(&options, error);
}

bool XDeviceDir_readNext(XFd fd, XDirEntry* entry)
{
    XVarList* output;
    bool result;
    if (!entry || !entry->name) return false;
    output = XVarList_Create(XVar(XDirEntry*, entry));
    if (!output) return false;
    result = XDeviceDir_control(fd, XDeviceDirCommand_ReadNext, NULL, output);
    XVarList_delete(output);
    return result;
}
