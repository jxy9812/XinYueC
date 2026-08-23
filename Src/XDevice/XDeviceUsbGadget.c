/* ============================================================================
 * XDeviceUsbGadget.c
 * @brief      USB Device/Gadget 从机统一设备类实现（XDeviceUsbGadget）。
 * @details    本文件实现 XDeviceUsbGadget 类（类别名  usbgadget）。控制器
 *             生命周期、描述符配置、Setup/事件回调和端点传输统一通过 XFd 门面
 *             暴露。平台后端由 XDeviceUsbGadget_platform* 函数接入，公共层只
 *             依赖 USB 标准描述符与端点传输。
 * ========================================================================== */
#include "XDeviceUsbGadget.h"
#include "XVarList.h"
#include "XMemory.h"
#include "XFileDescriptor.h"
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * 平台后端接口前置声明
 * ============================================================================ */
void* XDeviceUsbGadget_platformCreate(const XDeviceUsbGadgetConfig* config, int* error);
bool  XDeviceUsbGadget_platformOpen(void* controller, const XDeviceUsbGadgetConfig* config, int* error);
void  XDeviceUsbGadget_platformClose(void* controller);
void  XDeviceUsbGadget_platformDelete(void* controller);
bool  XDeviceUsbGadget_platformIsStarted(void* controller);
bool  XDeviceUsbGadget_platformIsConfigured(void* controller);
bool  XDeviceUsbGadget_platformGetConfig(void* controller, XDeviceUsbGadgetConfig* config);
bool  XDeviceUsbGadget_platformConfigure(void* controller, const XDeviceUsbGadgetConfig* config);
XDeviceUsbGadgetState XDeviceUsbGadget_platformStatus(void* controller);
bool  XDeviceUsbGadget_platformStart(void* controller);
bool  XDeviceUsbGadget_platformStop(void* controller);
bool  XDeviceUsbGadget_platformSetSetupCallback(void* controller,
                                                 XDeviceUsbGadgetSetupCallback callback,
                                                 void* userData);
bool  XDeviceUsbGadget_platformSetEventCallback(void* controller,
                                                XDeviceUsbGadgetEventCallback callback,
                                                void* userData);
XDeviceUsbProcessResult XDeviceUsbGadget_platformProcessEvents(void* controller, int32_t timeoutMs);
bool  XDeviceUsbGadget_platformGetEndpointInfo(void* controller, XDeviceUsbGadgetEndpointInfo* endpoint);
XDeviceUsbTransferResult XDeviceUsbGadget_platformTransfer(
    void* controller, XDeviceUsbEndpointAddress endpoint, void* data,
    size_t length, size_t* transferred, int32_t timeoutMs);
XDeviceUsbTransferId XDeviceUsbGadget_platformSubmitTransfer(
    void* controller, XDeviceUsbEndpointAddress endpoint, void* data,
    size_t length, int32_t timeoutMs,
    XDeviceUsbGadgetTransferCallback callback, void* userData);
XDeviceUsbTransferResult XDeviceUsbGadget_platformCancelTransfer(
    void* controller, XDeviceUsbTransferId transferId);
bool  XDeviceUsbGadget_platformSetEndpointStalled(void* controller,
                                                  XDeviceUsbEndpointAddress endpoint,
                                                  bool stalled);
bool  XDeviceUsbGadget_platformClearEndpointQueue(void* controller,
                                                  XDeviceUsbEndpointAddress endpoint);
bool  XDeviceUsbGadget_platformRemoteWakeup(void* controller);
XDeviceUsbGadgetFeatures XDeviceUsbGadget_platformFeatures(void* controller);
XDeviceUsbNativeHandle XDeviceUsbGadget_platformHandle(void* controller);
XDeviceUsbError XDeviceUsbGadget_platformLastError(void* controller);
int32_t XDeviceUsbGadget_platformNativeError(void* controller);
void XDeviceUsbGadget_platformClearError(void* controller);

/* ============================================================================
 * 类定义
 * ============================================================================ */
static XDeviceUsbGadget g_deviceUsbDevice;

static XDeviceContext* VXDeviceUsbGadget_open(XDevice* self, const XDeviceOpenOptions* options, int* error);
static void VXDeviceUsbGadget_close(XDevice* self, XDeviceContext* handle);
static bool VXDeviceUsbGadget_control(XDevice* self, XDeviceContext* handle,
                                      uint32_t command, const XVarList* in, XVarList* out);

static XDeviceUsbGadgetContext* usbDeviceContext(XDeviceContext* handle)
{
    return (XDeviceUsbGadgetContext*)handle;
}

static XDeviceError usbDeviceMapError(XDeviceUsbError error)
{
    switch (error) {
    case XDeviceUsbError_None:              return XDeviceError_None;
    case XDeviceUsbError_InvalidArgument:   return XDeviceError_InvalidArgument;
    case XDeviceUsbError_NotOpen:           return XDeviceError_NotOpen;
    case XDeviceUsbError_AlreadyOpen:       return XDeviceError_AlreadyOpen;
    case XDeviceUsbError_Busy:              return XDeviceError_Busy;
    case XDeviceUsbError_Timeout:           return XDeviceError_Timeout;
    case XDeviceUsbError_Disconnected:      return XDeviceError_Closed;
    case XDeviceUsbError_Unsupported:       return XDeviceError_NotSupported;
    case XDeviceUsbError_Resource:          return XDeviceError_OutOfMemory;
    case XDeviceUsbError_Stall:             return XDeviceError_IoFail;
    case XDeviceUsbError_PermissionDenied:  return XDeviceError_IoFail;
    case XDeviceUsbError_NoDevice:          return XDeviceError_NotFound;
    case XDeviceUsbError_Controller:        return XDeviceError_IoFail;
    case XDeviceUsbError_Io:                return XDeviceError_IoFail;
    case XDeviceUsbError_Interrupted:       return XDeviceError_IoFail;
    case XDeviceUsbError_Unknown:           return XDeviceError_IoFail;
    default:                                return XDeviceError_IoFail;
    }
}

static void usbDeviceSetError(XDeviceUsbGadgetContext* context)
{
    XDeviceError error;
    if (!context) return;
    if (context->m_backendController)
        error = usbDeviceMapError(XDeviceUsbGadget_platformLastError(context->m_backendController));
    else
        error = XDeviceError_IoFail;
    context->m_base.m_lastError = (int16_t)error;
}

static void usbDeviceSetOpenError(int* error, int platformError)
{
    if (!error) return;
    *error = platformError > 0 ? (int)usbDeviceMapError((XDeviceUsbError)platformError)
                               : (int)XDeviceError_IoFail;
}

XVtable* XDeviceUsbGadget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceUsbGadget)
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceUsbGadget_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceUsbGadget_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceUsbGadget_control);
    XCLASS_SET_CLASS_NAME_DEFAULT("usbgadget");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceUsbGadget);
    return XVTABLE_DEFAULT;
}

void XDeviceUsbGadget_init(XDeviceUsbGadget* self)
{
    if (!self) return;
    memset(((XDevice*)self) + 1, 0, sizeof(*self) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceUsbGadget);
    self->m_base.m_type = XDeviceType_Class;
    self->m_base.m_capabilities =
        XDeviceCap_Read | XDeviceCap_Write | XDeviceCap_Async;
}

XDeviceUsbGadget* XDeviceUsbGadget_create(void)
{
    XDeviceUsbGadget* self = (XDeviceUsbGadget*)XClass_Malloc(XDeviceUsbGadget);
    if (!self) return NULL;
    XDeviceUsbGadget_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

bool XDeviceUsbGadget_register(void)
{
    static bool registered = false;
    if (registered) return true;
    XDeviceUsbGadget_init(&g_deviceUsbDevice);
    if (!XDevice_register(&g_deviceUsbDevice.m_base)) return false;
    registered = true;
    return true;
}

/* ============================================================================
 * 虚函数实现
 * ============================================================================ */
static XDeviceContext* VXDeviceUsbGadget_open(XDevice* self, const XDeviceOpenOptions* options, int* error)
{
    const XDeviceUsbGadgetOpenOptions* deviceOptions;
    XDeviceUsbGadgetContext* context;
    XDeviceUsbGadgetConfig config;
    void* backend = NULL;
    int platformError = 0;

    (void)self;
    if (!options) { usbDeviceSetOpenError(error, (int)XDeviceUsbError_InvalidArgument); return NULL; }
    deviceOptions = (const XDeviceUsbGadgetOpenOptions*)options;
    config = deviceOptions->m_config;

    backend = XDeviceUsbGadget_platformCreate(&config, &platformError);
    if (!backend) { usbDeviceSetOpenError(error, platformError); return NULL; }
    if (!XDeviceUsbGadget_platformOpen(backend, &config, &platformError)) {
        XDeviceUsbGadget_platformDelete(backend);
        usbDeviceSetOpenError(error, platformError);
        return NULL;
    }

    context = (XDeviceUsbGadgetContext*)XCalloc_System(1, sizeof(*context));
    if (!context) {
        XDeviceUsbGadget_platformClose(backend);
        XDeviceUsbGadget_platformDelete(backend);
        usbDeviceSetOpenError(error, (int)XDeviceUsbError_Resource);
        return NULL;
    }
    context->m_base.m_fd = XFd_alloc(XFD_TYPE_CLASS, &context->m_base, NULL);
    if (context->m_base.m_fd == XFD_INVALID) {
        XDeviceUsbGadget_platformClose(backend);
        XDeviceUsbGadget_platformDelete(backend);
        XFree_System(context);
        usbDeviceSetOpenError(error, (int)XDeviceUsbError_Resource);
        return NULL;
    }
    context->m_backendController = backend;
    context->m_state = XDeviceUsbGadget_platformStatus(backend);
    context->m_base.m_device = self;
    context->m_base.m_state = (uint16_t)XDeviceState_Opening;
    context->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Sync;
    context->m_base.m_lastError = (int16_t)XDeviceError_None;
    if (error) *error = (int)XDeviceError_None;
    return &context->m_base;
}

static void VXDeviceUsbGadget_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceUsbGadgetContext* context = usbDeviceContext(handle);
    (void)self;
    if (!context) return;
    if (context->m_backendController) {
        XDeviceUsbGadget_platformClose(context->m_backendController);
        XDeviceUsbGadget_platformDelete(context->m_backendController);
    }
    context->m_backendController = NULL;
    context->m_state = XDeviceUsbGadgetState_Closed;
    XFree_System(context);
}

/* ============================================================================
 * 控制命令分发
 * ============================================================================ */
static bool VXDeviceUsbGadget_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                                      const XVarList* in, XVarList* out)
{
    XDeviceUsbGadgetContext* context = usbDeviceContext(handle);
    XVarList* input = (XVarList*)in;
    XVarList* output = (XVarList*)out;
    bool ok;
    (void)self;
    if (!context || !context->m_backendController) return false;

    ok = false;
    switch ((XDeviceUsbGadgetCommand)command) {
    case XDeviceUsbGadgetCommand_IsStarted: {
        bool* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, bool*);
        if (!slot) break;
        *slot = XDeviceUsbGadget_platformIsStarted(context->m_backendController);
        ok = true;
        break;
    }
    case XDeviceUsbGadgetCommand_IsConfigured: {
        bool* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, bool*);
        if (!slot) break;
        *slot = XDeviceUsbGadget_platformIsConfigured(context->m_backendController);
        ok = true;
        break;
    }
    case XDeviceUsbGadgetCommand_GetConfig: {
        XDeviceUsbGadgetConfig* config;
        if (input || !output || output->m_size != sizeof(config)) break;
        XVarList_start(output);
        config = XVarList_arg(output, XDeviceUsbGadgetConfig*);
        if (!config) break;
        ok = XDeviceUsbGadget_platformGetConfig(context->m_backendController, config);
        break;
    }
    case XDeviceUsbGadgetCommand_Configure: {
        const XDeviceUsbGadgetConfig* config;
        if (!input || output || input->m_size != sizeof(config)) break;
        XVarList_start(input);
        config = XVarList_arg(input, const XDeviceUsbGadgetConfig*);
        if (!config) break;
        ok = XDeviceUsbGadget_platformConfigure(context->m_backendController, config);
        if (ok)
            context->m_state = XDeviceUsbGadget_platformStatus(context->m_backendController);
        break;
    }
    case XDeviceUsbGadgetCommand_Status: {
        XDeviceUsbGadgetState* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbGadgetState*);
        if (!slot) break;
        *slot = XDeviceUsbGadget_platformStatus(context->m_backendController);
        context->m_state = *slot;
        ok = true;
        break;
    }
    case XDeviceUsbGadgetCommand_Start: {
        if (input || output) break;
        ok = XDeviceUsbGadget_platformStart(context->m_backendController);
        if (ok)
            context->m_state = XDeviceUsbGadget_platformStatus(context->m_backendController);
        break;
    }
    case XDeviceUsbGadgetCommand_Stop: {
        if (input || output) break;
        ok = XDeviceUsbGadget_platformStop(context->m_backendController);
        if (ok)
            context->m_state = XDeviceUsbGadget_platformStatus(context->m_backendController);
        break;
    }
    case XDeviceUsbGadgetCommand_SetSetupCallback: {
        XDeviceUsbGadgetSetupCallback callback;
        void* userData;
        if (!input || output ||
            input->m_size != sizeof(callback) + sizeof(userData)) break;
        XVarList_start(input);
        callback = XVarList_arg(input, XDeviceUsbGadgetSetupCallback);
        userData = XVarList_arg(input, void*);
        ok = XDeviceUsbGadget_platformSetSetupCallback(context->m_backendController,
            callback, userData);
        break;
    }
    case XDeviceUsbGadgetCommand_SetEventCallback: {
        XDeviceUsbGadgetEventCallback callback;
        void* userData;
        if (!input || output ||
            input->m_size != sizeof(callback) + sizeof(userData)) break;
        XVarList_start(input);
        callback = XVarList_arg(input, XDeviceUsbGadgetEventCallback);
        userData = XVarList_arg(input, void*);
        ok = XDeviceUsbGadget_platformSetEventCallback(context->m_backendController,
            callback, userData);
        break;
    }
    case XDeviceUsbGadgetCommand_ProcessEvents: {
        int32_t timeoutMs;
        XDeviceUsbProcessResult result;
        XDeviceUsbProcessResult* slot;
        if (!input || !output || input->m_size != sizeof(timeoutMs) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        timeoutMs = XVarList_arg(input, int32_t);
        result = XDeviceUsbGadget_platformProcessEvents(context->m_backendController,
            timeoutMs);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbProcessResult*);
        if (!slot) break;
        *slot = result;
        ok = result != XDeviceUsbProcessResult_Error;
        break;
    }
    case XDeviceUsbGadgetCommand_GetEndpointInfo: {
        XDeviceUsbGadgetEndpointInfo* endpoint;
        if (!input || output || input->m_size != sizeof(endpoint)) break;
        XVarList_start(input);
        endpoint = XVarList_arg(input, XDeviceUsbGadgetEndpointInfo*);
        if (!endpoint) break;
        ok = XDeviceUsbGadget_platformGetEndpointInfo(context->m_backendController, endpoint);
        break;
    }
    case XDeviceUsbGadgetCommand_Transfer: {
        XDeviceUsbEndpointAddress endpoint;
        void* data;
        size_t length;
        size_t* transferred;
        int32_t timeoutMs;
        XDeviceUsbTransferResult result;
        XDeviceUsbTransferResult* slot;
        if (!input || !output ||
            input->m_size != sizeof(endpoint) + sizeof(data) + sizeof(length) +
                             sizeof(transferred) + sizeof(timeoutMs) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        endpoint = XVarList_arg(input, XDeviceUsbEndpointAddress);
        data = XVarList_arg(input, void*);
        length = XVarList_arg(input, size_t);
        transferred = XVarList_arg(input, size_t*);
        timeoutMs = XVarList_arg(input, int32_t);
        if (endpoint == 0 || (data == NULL && length != 0)) break;
        result = XDeviceUsbGadget_platformTransfer(context->m_backendController,
            endpoint, data, length, transferred, timeoutMs);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferResult*);
        if (!slot) break;
        *slot = result;
        ok = result == XDeviceUsbTransferResult_Ok;
        break;
    }
    case XDeviceUsbGadgetCommand_SubmitTransfer: {
        XDeviceUsbEndpointAddress endpoint;
        void* data;
        size_t length;
        int32_t timeoutMs;
        XDeviceUsbGadgetTransferCallback callback;
        void* userData;
        XDeviceUsbTransferId id;
        XDeviceUsbTransferId* slot;
        if (!input || !output ||
            input->m_size != sizeof(endpoint) + sizeof(data) + sizeof(length) +
                             sizeof(timeoutMs) + sizeof(callback) + sizeof(userData) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        endpoint = XVarList_arg(input, XDeviceUsbEndpointAddress);
        data = XVarList_arg(input, void*);
        length = XVarList_arg(input, size_t);
        timeoutMs = XVarList_arg(input, int32_t);
        callback = XVarList_arg(input, XDeviceUsbGadgetTransferCallback);
        userData = XVarList_arg(input, void*);
        if (endpoint == 0 || !callback || (data == NULL && length != 0)) break;
        id = XDeviceUsbGadget_platformSubmitTransfer(context->m_backendController,
            endpoint, data, length, timeoutMs, callback, userData);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferId*);
        if (!slot) break;
        *slot = id;
        ok = id != XDEVICE_USB_INVALID_TRANSFER_ID;
        break;
    }
    case XDeviceUsbGadgetCommand_CancelTransfer: {
        XDeviceUsbTransferId transferId;
        XDeviceUsbTransferResult result;
        XDeviceUsbTransferResult* slot;
        if (!input || !output || input->m_size != sizeof(transferId) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        transferId = XVarList_arg(input, XDeviceUsbTransferId);
        result = XDeviceUsbGadget_platformCancelTransfer(context->m_backendController, transferId);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferResult*);
        if (!slot) break;
        *slot = result;
        ok = result == XDeviceUsbTransferResult_Ok ||
             result == XDeviceUsbTransferResult_Cancelled;
        break;
    }
    case XDeviceUsbGadgetCommand_SetEndpointStalled: {
        XDeviceUsbEndpointAddress endpoint;
        bool stalled;
        if (!input || output ||
            input->m_size != sizeof(endpoint) + sizeof(stalled)) break;
        XVarList_start(input);
        endpoint = XVarList_arg(input, XDeviceUsbEndpointAddress);
        stalled = XVarList_arg(input, bool);
        if (endpoint == 0) break;
        ok = XDeviceUsbGadget_platformSetEndpointStalled(context->m_backendController,
            endpoint, stalled);
        break;
    }
    case XDeviceUsbGadgetCommand_ClearEndpointQueue: {
        XDeviceUsbEndpointAddress endpoint;
        if (!input || output || input->m_size != sizeof(endpoint)) break;
        XVarList_start(input);
        endpoint = XVarList_arg(input, XDeviceUsbEndpointAddress);
        if (endpoint == 0) break;
        ok = XDeviceUsbGadget_platformClearEndpointQueue(context->m_backendController, endpoint);
        break;
    }
    case XDeviceUsbGadgetCommand_RemoteWakeup: {
        if (input || output) break;
        ok = XDeviceUsbGadget_platformRemoteWakeup(context->m_backendController);
        break;
    }
    case XDeviceUsbGadgetCommand_Features: {
        XDeviceUsbGadgetFeatures* features;
        if (input || !output || output->m_size != sizeof(features)) break;
        XVarList_start(output);
        features = XVarList_arg(output, XDeviceUsbGadgetFeatures*);
        if (!features) break;
        *features = XDeviceUsbGadget_platformFeatures(context->m_backendController);
        ok = true;
        break;
    }
    case XDeviceUsbGadgetCommand_Handle: {
        XDeviceUsbNativeHandle* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbNativeHandle*);
        if (!slot) break;
        *slot = XDeviceUsbGadget_platformHandle(context->m_backendController);
        ok = true;
        break;
    }
    case XDeviceUsbGadgetCommand_LastError: {
        XDeviceUsbError* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbError*);
        if (!slot) break;
        *slot = XDeviceUsbGadget_platformLastError(context->m_backendController);
        ok = true;
        break;
    }
    case XDeviceUsbGadgetCommand_NativeError: {
        int32_t* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, int32_t*);
        if (!slot) break;
        *slot = XDeviceUsbGadget_platformNativeError(context->m_backendController);
        ok = true;
        break;
    }
    case XDeviceUsbGadgetCommand_ClearError: {
        if (input || output) break;
        XDeviceUsbGadget_platformClearError(context->m_backendController);
        ok = true;
        break;
    }

    default:
        break;
    }
    if (ok) context->m_base.m_lastError = (int16_t)XDeviceError_None;
    else usbDeviceSetError(context);
    return ok;
}

/* ============================================================================
 * 便捷 API（统一通过 XFd 门面）
 * ============================================================================ */
bool XDeviceUsbGadget_isStarted(XFd fd)
{
    XVarList* output;
    bool started = false;
    bool* slot = &started;
    output = XVarList_Create(XVar(bool*, slot));
    if (!output) return false;
    {
        bool ok = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_IsStarted, NULL, output);
        XVarList_delete(output);
        return ok;
    }
}

bool XDeviceUsbGadget_isConfigured(XFd fd)
{
    XVarList* output;
    bool configured = false;
    bool* slot = &configured;
    output = XVarList_Create(XVar(bool*, slot));
    if (!output) return false;
    {
        bool ok = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_IsConfigured, NULL, output);
        XVarList_delete(output);
        return ok;
    }
}

bool XDeviceUsbGadget_getConfig(XFd fd, XDeviceUsbGadgetConfig* config)
{
    XVarList* output;
    bool result;
    if (!config) return false;
    output = XVarList_Create(XVar(XDeviceUsbGadgetConfig*, config));
    if (!output) return false;
    result = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_GetConfig, NULL, output);
    XVarList_delete(output);
    return result;
}

bool XDeviceUsbGadget_configure(XFd fd, const XDeviceUsbGadgetConfig* config)
{
    XVarList* input;
    bool result;
    if (!config) return false;
    input = XVarList_Create(XVar(const XDeviceUsbGadgetConfig*, config));
    if (!input) return false;
    result = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_Configure, input, NULL);
    XVarList_delete(input);
    return result;
}

XDeviceUsbGadgetState XDeviceUsbGadget_status(XFd fd)
{
    XVarList* output;
    XDeviceUsbGadgetState state = XDeviceUsbGadgetState_Closed;
    XDeviceUsbGadgetState* slot = &state;
    output = XVarList_Create(XVar(XDeviceUsbGadgetState*, slot));
    if (!output) return XDeviceUsbGadgetState_Closed;
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_Status, NULL, output);
    XVarList_delete(output);
    return state;
}

bool XDeviceUsbGadget_start(XFd fd)
{
    return XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_Start, NULL, NULL);
}

bool XDeviceUsbGadget_stop(XFd fd)
{
    return XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_Stop, NULL, NULL);
}

bool XDeviceUsbGadget_setSetupCallback(XFd fd, XDeviceUsbGadgetSetupCallback callback, void* userData)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(XDeviceUsbGadgetSetupCallback, callback),
                            XVar(void*, userData));
    if (!input) return false;
    result = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_SetSetupCallback, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbGadget_setEventCallback(XFd fd, XDeviceUsbGadgetEventCallback callback, void* userData)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(XDeviceUsbGadgetEventCallback, callback),
                            XVar(void*, userData));
    if (!input) return false;
    result = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_SetEventCallback, input, NULL);
    XVarList_delete(input);
    return result;
}

XDeviceUsbProcessResult XDeviceUsbGadget_processEvents(XFd fd, int32_t timeoutMs)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbProcessResult result = XDeviceUsbProcessResult_Error;
    XDeviceUsbProcessResult* slot = &result;
    input = XVarList_Create(XVar(int32_t, timeoutMs));
    if (!input) return XDeviceUsbProcessResult_Error;
    output = XVarList_Create(XVar(XDeviceUsbProcessResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbProcessResult_Error; }
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_ProcessEvents, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbGadget_getEndpointInfo(XFd fd, XDeviceUsbGadgetEndpointInfo* endpoint)
{
    XVarList* input;
    bool result;
    if (!endpoint) return false;
    input = XVarList_Create(XVar(XDeviceUsbGadgetEndpointInfo*, endpoint));
    if (!input) return false;
    result = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_GetEndpointInfo, input, NULL);
    XVarList_delete(input);
    return result;
}

XDeviceUsbTransferResult XDeviceUsbGadget_transfer(XFd fd, XDeviceUsbEndpointAddress endpoint,
    void* data, size_t length, size_t* transferred, int32_t timeoutMs)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_InvalidArgument;
    XDeviceUsbTransferResult* slot = &result;
    input = XVarList_Create(XVar(XDeviceUsbEndpointAddress, endpoint),
                            XVar(void*, data),
                            XVar(size_t, length),
                            XVar(size_t*, transferred),
                            XVar(int32_t, timeoutMs));
    if (!input) return XDeviceUsbTransferResult_ResourceError;
    output = XVarList_Create(XVar(XDeviceUsbTransferResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbTransferResult_ResourceError; }
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_Transfer, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

XDeviceUsbTransferId XDeviceUsbGadget_submitTransfer(XFd fd, XDeviceUsbEndpointAddress endpoint,
    void* data, size_t length, int32_t timeoutMs,
    XDeviceUsbGadgetTransferCallback callback, void* userData)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferId result = XDEVICE_USB_INVALID_TRANSFER_ID;
    XDeviceUsbTransferId* slot = &result;
    input = XVarList_Create(XVar(XDeviceUsbEndpointAddress, endpoint),
                            XVar(void*, data),
                            XVar(size_t, length),
                            XVar(int32_t, timeoutMs),
                            XVar(XDeviceUsbGadgetTransferCallback, callback),
                            XVar(void*, userData));
    if (!input) return XDEVICE_USB_INVALID_TRANSFER_ID;
    output = XVarList_Create(XVar(XDeviceUsbTransferId*, slot));
    if (!output) { XVarList_delete(input); return XDEVICE_USB_INVALID_TRANSFER_ID; }
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_SubmitTransfer, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

XDeviceUsbTransferResult XDeviceUsbGadget_cancelTransfer(XFd fd, XDeviceUsbTransferId transferId)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_InvalidArgument;
    XDeviceUsbTransferResult* slot = &result;
    input = XVarList_Create(XVar(XDeviceUsbTransferId, transferId));
    if (!input) return XDeviceUsbTransferResult_ResourceError;
    output = XVarList_Create(XVar(XDeviceUsbTransferResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbTransferResult_ResourceError; }
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_CancelTransfer, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbGadget_setEndpointStalled(XFd fd, XDeviceUsbEndpointAddress endpoint, bool stalled)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(XDeviceUsbEndpointAddress, endpoint),
                            XVar(bool, stalled));
    if (!input) return false;
    result = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_SetEndpointStalled, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbGadget_clearEndpointQueue(XFd fd, XDeviceUsbEndpointAddress endpoint)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(XDeviceUsbEndpointAddress, endpoint));
    if (!input) return false;
    result = XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_ClearEndpointQueue, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbGadget_remoteWakeup(XFd fd)
{
    return XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_RemoteWakeup, NULL, NULL);
}

XDeviceUsbGadgetFeatures XDeviceUsbGadget_features(XFd fd)
{
    XVarList* output;
    XDeviceUsbGadgetFeatures features = XDeviceUsbGadgetFeature_None;
    XDeviceUsbGadgetFeatures* slot = &features;
    output = XVarList_Create(XVar(XDeviceUsbGadgetFeatures*, slot));
    if (!output) return XDeviceUsbGadgetFeature_None;
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_Features, NULL, output);
    XVarList_delete(output);
    return features;
}

bool XDeviceUsbGadget_hasFeature(XFd fd, XDeviceUsbGadgetFeature feature)
{
    XDeviceUsbGadgetFeatures features = XDeviceUsbGadget_features(fd);
    return feature != XDeviceUsbGadgetFeature_None &&
           ((features & (uint32_t)feature) != 0u);
}

XDeviceUsbNativeHandle XDeviceUsbGadget_handle(XFd fd)
{
    XVarList* output;
    XDeviceUsbNativeHandle handle = XDEVICE_USB_INVALID_NATIVE_HANDLE;
    XDeviceUsbNativeHandle* slot = &handle;
    output = XVarList_Create(XVar(XDeviceUsbNativeHandle*, slot));
    if (!output) return XDEVICE_USB_INVALID_NATIVE_HANDLE;
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_Handle, NULL, output);
    XVarList_delete(output);
    return handle;
}

XDeviceUsbError XDeviceUsbGadget_lastError(XFd fd)
{
    XVarList* output;
    XDeviceUsbError error = XDeviceUsbError_None;
    XDeviceUsbError* slot = &error;
    output = XVarList_Create(XVar(XDeviceUsbError*, slot));
    if (!output) return XDeviceUsbError_Unknown;
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_LastError, NULL, output);
    XVarList_delete(output);
    return error;
}

int32_t XDeviceUsbGadget_nativeError(XFd fd)
{
    XVarList* output;
    int32_t code = 0;
    int32_t* slot = &code;
    output = XVarList_Create(XVar(int32_t*, slot));
    if (!output) return 0;
    XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_NativeError, NULL, output);
    XVarList_delete(output);
    return code;
}

void XDeviceUsbGadget_clearError(XFd fd)
{
    (void)XDeviceUsbGadget_control(fd, XDeviceUsbGadgetCommand_ClearError, NULL, NULL);
}

const char* XDeviceUsbGadget_errorString(XDeviceUsbError error)
{
    return XDeviceUsbHost_errorString(error);
}
