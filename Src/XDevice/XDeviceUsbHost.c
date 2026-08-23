/* ============================================================================
 * XDeviceUsbHost.c
 * @brief      USB Host 外接设备统一设备类实现（XDeviceUsbHost）。
 * @details    本文件实现 XDeviceUsbHost 类（类别名  usbhost）。打开、关闭和控制统一
 *             通过 XDevice 门面（XFd）进行；平台后端通过 XDeviceUsbHost_platform*
 *             函数接入，由 Drive 目录下的平台文件实现。
 *             公共层不依赖 libusb、WinUSB、SetupAPI、STM32 HAL 或 ESP-IDF。
 * ========================================================================== */
#include "XDeviceUsbHost.h"
#include "XVarList.h"
#include "XMemory.h"
#include "XFileDescriptor.h"
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * 平台后端接口前置声明
 * ============================================================================ */
/* 控制器层。 */
void* XDeviceUsbHost_platformControllerCreate(const XDeviceUsbControllerConfig* config, int* error);
bool  XDeviceUsbHost_platformControllerOpen(void* controller, const XDeviceUsbControllerConfig* config, int* error);
void  XDeviceUsbHost_platformControllerClose(void* controller);
void  XDeviceUsbHost_platformControllerDelete(void* controller);
bool  XDeviceUsbHost_platformControllerEnumerate(void* controller, XDeviceUsbEnumerateCallback callback, void* userData);
bool  XDeviceUsbHost_platformControllerSetHotplug(void* controller, XDeviceUsbHotplugCallback callback, void* userData);
XDeviceUsbProcessResult XDeviceUsbHost_platformControllerProcessEvents(void* controller, int32_t timeoutMs);
XDeviceUsbFeatures XDeviceUsbHost_platformControllerFeatures(void* controller);
XDeviceUsbNativeHandle XDeviceUsbHost_platformControllerHandle(void* controller);
XDeviceUsbError XDeviceUsbHost_platformControllerLastError(void* controller);
int32_t XDeviceUsbHost_platformControllerNativeError(void* controller);
void XDeviceUsbHost_platformControllerClearError(void* controller);

/* 外接设备层。 */
void* XDeviceUsbHost_platformDeviceOpen(void* controller, const XDeviceUsbDeviceSelector* selector, int* error);
void  XDeviceUsbHost_platformDeviceClose(void* device);
bool  XDeviceUsbHost_platformDeviceGetInfo(void* device, XDeviceUsbDeviceInfo* info);
bool  XDeviceUsbHost_platformDeviceSetConfiguration(void* device, uint8_t value);
bool  XDeviceUsbHost_platformDeviceGetConfiguration(void* device, uint8_t* value);
bool  XDeviceUsbHost_platformDeviceClaimInterface(void* device, uint8_t interfaceNumber);
bool  XDeviceUsbHost_platformDeviceReleaseInterface(void* device, uint8_t interfaceNumber);
bool  XDeviceUsbHost_platformDeviceSetAlternateSetting(void* device, uint8_t interfaceNumber, uint8_t alternate);
size_t XDeviceUsbHost_platformDeviceEndpointCount(void* device, uint8_t interfaceNumber, uint8_t alternate);
bool  XDeviceUsbHost_platformDeviceGetEndpointInfo(void* device, uint8_t interfaceNumber, uint8_t alternate,
                                               size_t index, XDeviceUsbEndpointInfo* endpoint);
bool  XDeviceUsbHost_platformDeviceClearHalt(void* device, XDeviceUsbEndpointAddress endpoint);
bool  XDeviceUsbHost_platformDeviceReset(void* device);
XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceControlTransfer(
    void* device, const XDeviceUsbControlRequest* request, void* data,
    size_t capacity, size_t* transferred, int32_t timeoutMs);
XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceTransfer(
    void* device, XDeviceUsbEndpointAddress endpoint, void* data,
    size_t length, size_t* transferred, int32_t timeoutMs);
XDeviceUsbTransferId XDeviceUsbHost_platformDeviceSubmitTransfer(
    void* device, const XDeviceUsbTransferRequest* request,
    XDeviceUsbTransferCallback callback, void* userData);
XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceCancelTransfer(
    void* device, XDeviceUsbTransferId transferId);
XDeviceUsbFeatures XDeviceUsbHost_platformDeviceFeatures(void* device);
XDeviceUsbNativeHandle XDeviceUsbHost_platformDeviceHandle(void* device);
XDeviceUsbError XDeviceUsbHost_platformDeviceLastError(void* device);
int32_t XDeviceUsbHost_platformDeviceNativeError(void* device);
void XDeviceUsbHost_platformDeviceClearError(void* device);

/* ============================================================================
 * 类定义
 * ============================================================================ */
static XDeviceUsbHost g_deviceUsb;

static XDeviceContext* VXDeviceUsbHost_open(XDevice* self, const XDeviceOpenOptions* options, int* error);
static void VXDeviceUsbHost_close(XDevice* self, XDeviceContext* handle);
static int64_t VXDeviceUsbHost_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size);
static int64_t VXDeviceUsbHost_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size);
static bool VXDeviceUsbHost_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                                const XVarList* in, XVarList* out);

static XDeviceUsbContext* usbContext(XDeviceContext* handle)
{
    return (XDeviceUsbContext*)handle;
}

static void usbSetGenericError(XDeviceContext* base, XDeviceError error)
{
    if (base) base->m_lastError = (int16_t)error;
}

/* 把平台 USB 通用错误映射为 XDevice 通用错误。 */
static XDeviceError usbMapError(XDeviceUsbError error)
{
    switch (error) {
    case XDeviceUsbError_None:              return XDeviceError_None;
    case XDeviceUsbError_InvalidArgument:   return XDeviceError_InvalidArgument;
    case XDeviceUsbError_NotOpen:           return XDeviceError_NotOpen;
    case XDeviceUsbError_AlreadyOpen:       return XDeviceError_AlreadyOpen;
    case XDeviceUsbError_NoDevice:          return XDeviceError_NotFound;
    case XDeviceUsbError_Busy:              return XDeviceError_Busy;
    case XDeviceUsbError_PermissionDenied:  return XDeviceError_IoFail;
    case XDeviceUsbError_Timeout:           return XDeviceError_Timeout;
    case XDeviceUsbError_Stall:             return XDeviceError_IoFail;
    case XDeviceUsbError_Disconnected:      return XDeviceError_Closed;
    case XDeviceUsbError_Unsupported:       return XDeviceError_NotSupported;
    case XDeviceUsbError_Resource:          return XDeviceError_OutOfMemory;
    case XDeviceUsbError_Controller:        return XDeviceError_IoFail;
    case XDeviceUsbError_Io:                return XDeviceError_IoFail;
    case XDeviceUsbError_Interrupted:       return XDeviceError_IoFail;
    case XDeviceUsbError_Unknown:           return XDeviceError_IoFail;
    default:                                return XDeviceError_IoFail;
    }
}

/* 根据设备后端最近错误设置通用错误码。 */
static void usbSetDeviceError(XDeviceUsbContext* context)
{
    XDeviceError error;
    if (!context) return;
    if (context->m_backendDevice) {
        error = usbMapError(XDeviceUsbHost_platformDeviceLastError(context->m_backendDevice));
    } else if (context->m_backendController) {
        error = usbMapError(XDeviceUsbHost_platformControllerLastError(context->m_backendController));
    } else {
        error = XDeviceError_IoFail;
    }
    context->m_base.m_lastError = (int16_t)error;
}

static void usbSetOpenError(int* error, int platformError)
{
    if (!error) return;
    if (platformError > 0)
        *error = (int)usbMapError((XDeviceUsbError)platformError);
    else
        *error = (int)XDeviceError_IoFail;
}

XVtable* XDeviceUsbHost_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceUsbHost)
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceUsbHost_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceUsbHost_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Read, VXDeviceUsbHost_read);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Write, VXDeviceUsbHost_write);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceUsbHost_control);
    XCLASS_SET_CLASS_NAME_DEFAULT("usbhost");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceUsbHost);
    return XVTABLE_DEFAULT;
}

void XDeviceUsbHost_init(XDeviceUsbHost* self)
{
    if (!self) return;
    memset(((XDevice*)self) + 1, 0, sizeof(*self) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceUsbHost);
    self->m_base.m_type = XDeviceType_Class;
    self->m_base.m_capabilities = XDeviceCap_Read | XDeviceCap_Write;
}

XDeviceUsbHost* XDeviceUsbHost_create(void)
{
    XDeviceUsbHost* self = (XDeviceUsbHost*)XClass_Malloc(XDeviceUsbHost);
    if (!self) return NULL;
    XDeviceUsbHost_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

bool XDeviceUsbHost_register(void)
{
    static bool registered = false;
    if (registered) return true;
    XDeviceUsbHost_init(&g_deviceUsb);
    if (!XDevice_register(&g_deviceUsb.m_base)) return false;
    registered = true;
    return true;
}
/* ============================================================================
 * 虚函数实现
 * ============================================================================ */
static XDeviceContext* VXDeviceUsbHost_open(XDevice* self, const XDeviceOpenOptions* options, int* error)
{
    const XDeviceUsbOpenOptions* usbOptions;
    XDeviceUsbContext* context;
    XDeviceUsbControllerConfig controllerConfig;
    XDeviceUsbDeviceSelector selector;
    void* controller = NULL;
    void* device = NULL;
    int platformError = 0;

    (void)self;
    if (!options) { usbSetOpenError(error, (int)XDeviceUsbError_InvalidArgument); return NULL; }
    usbOptions = (const XDeviceUsbOpenOptions*)options;

    memset(&controllerConfig, 0, sizeof(controllerConfig));
    memset(&selector, 0, sizeof(selector));
    controllerConfig.m_controller = usbOptions->m_controller.m_controller;
    controllerConfig.m_flags = usbOptions->m_controller.m_flags;
    selector = usbOptions->m_selector;

    controller = XDeviceUsbHost_platformControllerCreate(&controllerConfig, &platformError);
    if (!controller) { usbSetOpenError(error, platformError); return NULL; }
    if (!XDeviceUsbHost_platformControllerOpen(controller, &controllerConfig, &platformError)) {
        XDeviceUsbHost_platformControllerDelete(controller);
        usbSetOpenError(error, platformError);
        return NULL;
    }
    device = XDeviceUsbHost_platformDeviceOpen(controller, &selector, &platformError);
    if (!device) {
        XDeviceUsbHost_platformControllerClose(controller);
        XDeviceUsbHost_platformControllerDelete(controller);
        usbSetOpenError(error, platformError);
        return NULL;
    }

    context = (XDeviceUsbContext*)XCalloc_System(1, sizeof(*context));
    if (!context) {
        XDeviceUsbHost_platformDeviceClose(device);
        XDeviceUsbHost_platformControllerClose(controller);
        XDeviceUsbHost_platformControllerDelete(controller);
        usbSetOpenError(error, (int)XDeviceUsbError_Resource);
        return NULL;
    }
    context->m_base.m_fd = XFd_alloc(XFD_TYPE_CLASS, &context->m_base, NULL);
    if (context->m_base.m_fd == XFD_INVALID) {
        XDeviceUsbHost_platformDeviceClose(device);
        XDeviceUsbHost_platformControllerClose(controller);
        XDeviceUsbHost_platformControllerDelete(controller);
        XFree_System(context);
        usbSetOpenError(error, (int)XDeviceUsbError_Resource);
        return NULL;
    }
    context->m_backendController = controller;
    context->m_backendDevice = device;
    context->m_ioEndpointIn = usbOptions->m_ioEndpointIn;
    context->m_ioEndpointOut = usbOptions->m_ioEndpointOut;
    context->m_transferTimeoutMs = usbOptions->m_transferTimeoutMs;
    context->m_base.m_device = self;
    context->m_base.m_state = (uint16_t)XDeviceState_Opening;
    context->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Sync;
    context->m_base.m_lastError = (int16_t)XDeviceError_None;
    if (!XDeviceUsbHost_platformDeviceGetInfo(device, &context->m_info))
        memset(&context->m_info, 0, sizeof(context->m_info));
    if (error) *error = (int)XDeviceError_None;
    return &context->m_base;
}

static void VXDeviceUsbHost_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceUsbContext* context = usbContext(handle);
    (void)self;
    if (!context) return;
    if (context->m_backendDevice)
        XDeviceUsbHost_platformDeviceClose(context->m_backendDevice);
    context->m_backendDevice = NULL;
    if (context->m_backendController) {
        XDeviceUsbHost_platformControllerClose(context->m_backendController);
        XDeviceUsbHost_platformControllerDelete(context->m_backendController);
    }
    context->m_backendController = NULL;
    XFree_System(context);
}

static int64_t VXDeviceUsbHost_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size)
{
    XDeviceUsbContext* context = usbContext(handle);
    size_t transferred = 0;
    XDeviceUsbTransferResult result;
    (void)self;
    if (!context || size < 0 || (size > 0 && !buffer)) {
        usbSetGenericError(handle, XDeviceError_InvalidArgument);
        return -1;
    }
    if (context->m_ioEndpointIn == 0 || !XDEVICE_USB_ENDPOINT_IS_IN(context->m_ioEndpointIn)) {
        usbSetGenericError(handle, XDeviceError_InvalidArgument);
        return -1;
    }
    result = XDeviceUsbHost_platformDeviceTransfer(context->m_backendDevice,
        context->m_ioEndpointIn, buffer, (size_t)size, &transferred,
        context->m_transferTimeoutMs);
    context->m_base.m_lastError = (int16_t)(result == XDeviceUsbTransferResult_Ok
        ? XDeviceError_None : XDeviceError_IoFail);
    return result == XDeviceUsbTransferResult_Ok ? (int64_t)transferred : -1;
}

static int64_t VXDeviceUsbHost_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size)
{
    XDeviceUsbContext* context = usbContext(handle);
    size_t transferred = 0;
    XDeviceUsbTransferResult result;
    (void)self;
    if (!context || size < 0 || (size > 0 && !data)) {
        usbSetGenericError(handle, XDeviceError_InvalidArgument);
        return -1;
    }
    if (context->m_ioEndpointOut == 0 || XDEVICE_USB_ENDPOINT_IS_IN(context->m_ioEndpointOut)) {
        usbSetGenericError(handle, XDeviceError_InvalidArgument);
        return -1;
    }
    result = XDeviceUsbHost_platformDeviceTransfer(context->m_backendDevice,
        context->m_ioEndpointOut, (void*)data, (size_t)size, &transferred,
        context->m_transferTimeoutMs);
    context->m_base.m_lastError = (int16_t)(result == XDeviceUsbTransferResult_Ok
        ? XDeviceError_None : XDeviceError_IoFail);
    return result == XDeviceUsbTransferResult_Ok ? (int64_t)transferred : -1;
}
/* ============================================================================
 * UTF-16LE 字符串描述符转 UTF-8 辅助函数
 * ============================================================================ */
static size_t usbUtf16Length(const uint16_t* units, size_t count)
{
    size_t i;
    size_t total = 0;
    for (i = 0; i < count; ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < count &&
            units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
            cp = 0x10000u + (((cp - 0xD800u) << 10) | (units[i + 1] - 0xDC00u));
            ++i;
        }
        if (cp < 0x80u) total += 1;
        else if (cp < 0x800u) total += 2;
        else if (cp < 0x10000u) total += 3;
        else total += 4;
    }
    return total;
}

static void usbUtf16Write(const uint16_t* units, size_t count, char* utf8)
{
    size_t i;
    size_t pos = 0;
    for (i = 0; i < count; ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < count &&
            units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
            cp = 0x10000u + (((cp - 0xD800u) << 10) | (units[i + 1] - 0xDC00u));
            ++i;
        }
        if (cp < 0x80u) {
            utf8[pos++] = (char)cp;
        } else if (cp < 0x800u) {
            utf8[pos++] = (char)(0xC0u | (cp >> 6));
            utf8[pos++] = (char)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            utf8[pos++] = (char)(0xE0u | (cp >> 12));
            utf8[pos++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            utf8[pos++] = (char)(0x80u | (cp & 0x3Fu));
        } else {
            utf8[pos++] = (char)(0xF0u | (cp >> 18));
            utf8[pos++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
            utf8[pos++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            utf8[pos++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }
    utf8[pos] = '\0';
}

static size_t usbStringDescriptorToUtf8(const uint8_t* raw, size_t rawLen,
                                        char* utf8, size_t capacity)
{
    uint16_t units[128];
    size_t count = 0;
    size_t i;
    size_t total;
    if (!raw || rawLen < 2) return 0;
    for (i = 2; i + 1 < rawLen && count < 127; i += 2)
        units[count++] = (uint16_t)(raw[i] | ((uint16_t)raw[i + 1] << 8));
    total = usbUtf16Length(units, count);
    if (utf8 && capacity > total)
        usbUtf16Write(units, count, utf8);
    return total;
}

/* ============================================================================
 * 控制命令分发
 * ============================================================================ */
static bool VXDeviceUsbHost_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                                const XVarList* in, XVarList* out)
{
    XDeviceUsbContext* context = usbContext(handle);
    XVarList* input = (XVarList*)in;
    XVarList* output = (XVarList*)out;
    bool ok;
    (void)self;
    if (!context || !context->m_backendDevice) return false;

    ok = false;
    switch ((XDeviceUsbCommand)command) {
    case XDeviceUsbCommand_GetInfo: {
        XDeviceUsbDeviceInfo* info;
        if (input || !output || output->m_size != sizeof(info)) break;
        XVarList_start(output);
        info = XVarList_arg(output, XDeviceUsbDeviceInfo*);
        if (!info) break;
        ok = XDeviceUsbHost_platformDeviceGetInfo(context->m_backendDevice, info);
        break;
    }
    case XDeviceUsbCommand_SetIoEndpoint: {
        XDeviceUsbEndpointAddress endpointIn;
        XDeviceUsbEndpointAddress endpointOut;
        if (!input || output ||
            input->m_size != sizeof(endpointIn) + sizeof(endpointOut)) break;
        XVarList_start(input);
        endpointIn = XVarList_arg(input, XDeviceUsbEndpointAddress);
        endpointOut = XVarList_arg(input, XDeviceUsbEndpointAddress);
        if ((endpointIn != 0 && !XDEVICE_USB_ENDPOINT_IS_IN(endpointIn)) ||
            (endpointOut != 0 && XDEVICE_USB_ENDPOINT_IS_IN(endpointOut))) break;
        if (endpointIn != 0) context->m_ioEndpointIn = endpointIn;
        if (endpointOut != 0) context->m_ioEndpointOut = endpointOut;
        ok = true;
        break;
    }
    case XDeviceUsbCommand_GetDescriptor: {
        XDeviceUsbDescriptorType descriptorType;
        uint8_t descriptorIndex;
        uint16_t languageId;
        void* data;
        size_t capacity;
        size_t* transferred;
        int32_t timeoutMs;
        XDeviceUsbTransferResult result;
        XDeviceUsbTransferResult* slot;
        XDeviceUsbControlRequest request;
        if (!input || !output ||
            input->m_size != sizeof(descriptorType) + sizeof(descriptorIndex) +
                             sizeof(languageId) + sizeof(data) + sizeof(capacity) +
                             sizeof(transferred) + sizeof(timeoutMs) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        descriptorType = XVarList_arg(input, XDeviceUsbDescriptorType);
        descriptorIndex = XVarList_arg(input, uint8_t);
        languageId = XVarList_arg(input, uint16_t);
        data = XVarList_arg(input, void*);
        capacity = XVarList_arg(input, size_t);
        transferred = XVarList_arg(input, size_t*);
        timeoutMs = XVarList_arg(input, int32_t);
        if ((data == NULL && capacity != 0) || capacity > 0xffffu) break;
        memset(&request, 0, sizeof(request));
        request.m_requestType = 0x80u;
        request.m_request = 6; /* GET_DESCRIPTOR */
        request.m_value = (uint16_t)(((uint16_t)descriptorType << 8) | descriptorIndex);
        request.m_index = languageId;
        request.m_length = (uint16_t)capacity;
        result = XDeviceUsbHost_platformDeviceControlTransfer(context->m_backendDevice,
            &request, data, capacity, transferred, timeoutMs);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferResult*);
        if (!slot) break;
        *slot = result;
        ok = result == XDeviceUsbTransferResult_Ok;
        break;
    }
    case XDeviceUsbCommand_GetStringDescriptor: {
        uint8_t descriptorIndex;
        uint16_t languageId;
        char* utf8;
        size_t capacity;
        size_t* length;
        int32_t timeoutMs;
        unsigned char raw[256];
        size_t rawLen = 0;
        size_t required;
        XDeviceUsbTransferResult result;
        XDeviceUsbTransferResult* slot;
        XDeviceUsbControlRequest request;
        if (!input || !output ||
            input->m_size != sizeof(descriptorIndex) + sizeof(languageId) +
                             sizeof(utf8) + sizeof(capacity) + sizeof(length) +
                             sizeof(timeoutMs) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        descriptorIndex = XVarList_arg(input, uint8_t);
        languageId = XVarList_arg(input, uint16_t);
        utf8 = XVarList_arg(input, char*);
        capacity = XVarList_arg(input, size_t);
        length = XVarList_arg(input, size_t*);
        timeoutMs = XVarList_arg(input, int32_t);
        memset(&request, 0, sizeof(request));
        request.m_requestType = 0x80u;
        request.m_request = 6; /* GET_DESCRIPTOR */
        request.m_value = (uint16_t)(0x0300u | descriptorIndex);
        request.m_index = languageId;
        request.m_length = 255u;
        result = XDeviceUsbHost_platformDeviceControlTransfer(context->m_backendDevice,
            &request, raw, sizeof(raw), &rawLen, timeoutMs);
        if (result == XDeviceUsbTransferResult_Ok && rawLen >= 2) {
            required = usbStringDescriptorToUtf8(raw, rawLen, utf8, capacity);
            if (length) *length = required;
            if (utf8 != NULL && capacity <= required)
                result = XDeviceUsbTransferResult_Overflow;
        } else if (result == XDeviceUsbTransferResult_Ok) {
            result = XDeviceUsbTransferResult_IoError;
        }
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferResult*);
        if (!slot) break;
        *slot = result;
        ok = result == XDeviceUsbTransferResult_Ok;
        break;
    }
    case XDeviceUsbCommand_SetConfiguration: {
        uint8_t value;
        if (!input || output || input->m_size != sizeof(value)) break;
        XVarList_start(input);
        value = XVarList_arg(input, uint8_t);
        ok = XDeviceUsbHost_platformDeviceSetConfiguration(context->m_backendDevice, value);
        break;
    }
    case XDeviceUsbCommand_GetConfiguration: {
        uint8_t* value;
        if (input || !output || output->m_size != sizeof(value)) break;
        XVarList_start(output);
        value = XVarList_arg(output, uint8_t*);
        if (!value) break;
        ok = XDeviceUsbHost_platformDeviceGetConfiguration(context->m_backendDevice, value);
        break;
    }
    case XDeviceUsbCommand_ClaimInterface: {
        uint8_t interfaceNumber;
        if (!input || output || input->m_size != sizeof(interfaceNumber)) break;
        XVarList_start(input);
        interfaceNumber = XVarList_arg(input, uint8_t);
        ok = XDeviceUsbHost_platformDeviceClaimInterface(context->m_backendDevice, interfaceNumber);
        break;
    }
    case XDeviceUsbCommand_ReleaseInterface: {
        uint8_t interfaceNumber;
        if (!input || output || input->m_size != sizeof(interfaceNumber)) break;
        XVarList_start(input);
        interfaceNumber = XVarList_arg(input, uint8_t);
        ok = XDeviceUsbHost_platformDeviceReleaseInterface(context->m_backendDevice, interfaceNumber);
        break;
    }
    case XDeviceUsbCommand_SetAlternateSetting: {
        uint8_t interfaceNumber;
        uint8_t alternate;
        if (!input || output || input->m_size != sizeof(interfaceNumber) + sizeof(alternate)) break;
        XVarList_start(input);
        interfaceNumber = XVarList_arg(input, uint8_t);
        alternate = XVarList_arg(input, uint8_t);
        ok = XDeviceUsbHost_platformDeviceSetAlternateSetting(context->m_backendDevice,
            interfaceNumber, alternate);
        break;
    }
    case XDeviceUsbCommand_EndpointCount: {
        uint8_t interfaceNumber;
        uint8_t alternate;
        size_t* count;
        if (!input || !output || input->m_size != sizeof(interfaceNumber) + sizeof(alternate) ||
            output->m_size != sizeof(count)) break;
        XVarList_start(input);
        interfaceNumber = XVarList_arg(input, uint8_t);
        alternate = XVarList_arg(input, uint8_t);
        XVarList_start(output);
        count = XVarList_arg(output, size_t*);
        if (!count) break;
        *count = XDeviceUsbHost_platformDeviceEndpointCount(context->m_backendDevice,
            interfaceNumber, alternate);
        ok = true;
        break;
    }
    case XDeviceUsbCommand_GetEndpointInfo: {
        uint8_t interfaceNumber;
        uint8_t alternate;
        size_t index;
        XDeviceUsbEndpointInfo* endpoint;
        if (!input || !output || input->m_size != sizeof(interfaceNumber) + sizeof(alternate) + sizeof(index) ||
            output->m_size != sizeof(endpoint)) break;
        XVarList_start(input);
        interfaceNumber = XVarList_arg(input, uint8_t);
        alternate = XVarList_arg(input, uint8_t);
        index = XVarList_arg(input, size_t);
        XVarList_start(output);
        endpoint = XVarList_arg(output, XDeviceUsbEndpointInfo*);
        if (!endpoint) break;
        ok = XDeviceUsbHost_platformDeviceGetEndpointInfo(context->m_backendDevice,
            interfaceNumber, alternate, index, endpoint);
        break;
    }
    case XDeviceUsbCommand_ClearHalt: {
        XDeviceUsbEndpointAddress endpoint;
        if (!input || output || input->m_size != sizeof(endpoint)) break;
        XVarList_start(input);
        endpoint = XVarList_arg(input, XDeviceUsbEndpointAddress);
        ok = XDeviceUsbHost_platformDeviceClearHalt(context->m_backendDevice, endpoint);
        break;
    }
    case XDeviceUsbCommand_Reset: {
        if (input || output) break;
        ok = XDeviceUsbHost_platformDeviceReset(context->m_backendDevice);
        break;
    }
    case XDeviceUsbCommand_ControlTransfer: {
        const XDeviceUsbControlRequest* request;
        void* data;
        size_t capacity;
        size_t* transferred;
        int32_t timeoutMs;
        XDeviceUsbTransferResult result;
        XDeviceUsbTransferResult* slot;
        if (!input || !output ||
            input->m_size != sizeof(request) + sizeof(data) + sizeof(capacity) +
                             sizeof(transferred) + sizeof(timeoutMs) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        request = XVarList_arg(input, const XDeviceUsbControlRequest*);
        data = XVarList_arg(input, void*);
        capacity = XVarList_arg(input, size_t);
        transferred = XVarList_arg(input, size_t*);
        timeoutMs = XVarList_arg(input, int32_t);
        if (!request || (data == NULL && capacity != 0)) break;
        result = XDeviceUsbHost_platformDeviceControlTransfer(context->m_backendDevice,
            request, data, capacity, transferred, timeoutMs);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferResult*);
        if (!slot) break;
        *slot = result;
        ok = result == XDeviceUsbTransferResult_Ok;
        break;
    }
    case XDeviceUsbCommand_Transfer: {
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
        result = XDeviceUsbHost_platformDeviceTransfer(context->m_backendDevice,
            endpoint, data, length, transferred, timeoutMs);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferResult*);
        if (!slot) break;
        *slot = result;
        ok = result == XDeviceUsbTransferResult_Ok;
        break;
    }
    case XDeviceUsbCommand_SubmitTransfer: {
        const XDeviceUsbTransferRequest* request;
        XDeviceUsbTransferCallback callback;
        void* userData;
        XDeviceUsbTransferId id;
        XDeviceUsbTransferId* slot;
        if (!input || !output ||
            input->m_size != sizeof(request) + sizeof(callback) + sizeof(userData) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        request = XVarList_arg(input, const XDeviceUsbTransferRequest*);
        callback = XVarList_arg(input, XDeviceUsbTransferCallback);
        userData = XVarList_arg(input, void*);
        if (!request || !callback) break;
        id = XDeviceUsbHost_platformDeviceSubmitTransfer(context->m_backendDevice,
            request, callback, userData);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferId*);
        if (!slot) break;
        *slot = id;
        ok = id != XDEVICE_USB_INVALID_TRANSFER_ID;
        break;
    }
    case XDeviceUsbCommand_CancelTransfer: {
        XDeviceUsbTransferId transferId;
        XDeviceUsbTransferResult result;
        XDeviceUsbTransferResult* slot;
        if (!input || !output || input->m_size != sizeof(transferId) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        transferId = XVarList_arg(input, XDeviceUsbTransferId);
        result = XDeviceUsbHost_platformDeviceCancelTransfer(context->m_backendDevice, transferId);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbTransferResult*);
        if (!slot) break;
        *slot = result;
        ok = result == XDeviceUsbTransferResult_Ok ||
             result == XDeviceUsbTransferResult_Cancelled;
        break;
    }
    case XDeviceUsbCommand_Enumerate: {
        XDeviceUsbEnumerateCallback callback;
        void* userData;
        if (!input || output || input->m_size != sizeof(callback) + sizeof(userData)) break;
        XVarList_start(input);
        callback = XVarList_arg(input, XDeviceUsbEnumerateCallback);
        userData = XVarList_arg(input, void*);
        if (!callback) break;
        ok = XDeviceUsbHost_platformControllerEnumerate(context->m_backendController,
            callback, userData);
        break;
    }
    case XDeviceUsbCommand_SetHotplugCallback: {
        XDeviceUsbHotplugCallback callback;
        void* userData;
        if (!input || output || input->m_size != sizeof(callback) + sizeof(userData)) break;
        XVarList_start(input);
        callback = XVarList_arg(input, XDeviceUsbHotplugCallback);
        userData = XVarList_arg(input, void*);
        ok = XDeviceUsbHost_platformControllerSetHotplug(context->m_backendController,
            callback, userData);
        break;
    }
    case XDeviceUsbCommand_ProcessEvents: {
        int32_t timeoutMs;
        XDeviceUsbProcessResult result;
        XDeviceUsbProcessResult* slot;
        if (!input || !output || input->m_size != sizeof(timeoutMs) ||
            output->m_size != sizeof(slot)) break;
        XVarList_start(input);
        timeoutMs = XVarList_arg(input, int32_t);
        result = XDeviceUsbHost_platformControllerProcessEvents(context->m_backendController,
            timeoutMs);
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbProcessResult*);
        if (!slot) break;
        *slot = result;
        ok = result != XDeviceUsbProcessResult_Error;
        break;
    }
    case XDeviceUsbCommand_Features: {
        XDeviceUsbFeatures* features;
        if (input || !output || output->m_size != sizeof(features)) break;
        XVarList_start(output);
        features = XVarList_arg(output, XDeviceUsbFeatures*);
        if (!features) break;
        *features = XDeviceUsbHost_platformDeviceFeatures(context->m_backendDevice);
        ok = true;
        break;
    }
    case XDeviceUsbCommand_Handle: {
        XDeviceUsbNativeHandle* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbNativeHandle*);
        if (!slot) break;
        *slot = XDeviceUsbHost_platformDeviceHandle(context->m_backendDevice);
        ok = true;
        break;
    }
    case XDeviceUsbCommand_LastError: {
        XDeviceUsbError* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, XDeviceUsbError*);
        if (!slot) break;
        *slot = XDeviceUsbHost_platformDeviceLastError(context->m_backendDevice);
        ok = true;
        break;
    }
    case XDeviceUsbCommand_NativeError: {
        int32_t* slot;
        if (input || !output || output->m_size != sizeof(slot)) break;
        XVarList_start(output);
        slot = XVarList_arg(output, int32_t*);
        if (!slot) break;
        *slot = XDeviceUsbHost_platformDeviceNativeError(context->m_backendDevice);
        ok = true;
        break;
    }
    case XDeviceUsbCommand_ClearError: {
        if (input || output) break;
        XDeviceUsbHost_platformDeviceClearError(context->m_backendDevice);
        XDeviceUsbHost_platformControllerClearError(context->m_backendController);
        ok = true;
        break;
    }

    default:
        break;
    }
    if (ok) context->m_base.m_lastError = (int16_t)XDeviceError_None;
    else usbSetDeviceError(context);
    return ok;
}
/* ============================================================================
 * 便捷 API（统一通过 XFd 门面）
 * ============================================================================ */
bool XDeviceUsbHost_getInfo(XFd fd, XDeviceUsbDeviceInfo* info)
{
    XVarList* output;
    bool result;
    if (!info) return false;
    output = XVarList_Create(XVar(XDeviceUsbDeviceInfo*, info));
    if (!output) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_GetInfo, NULL, output);
    XVarList_delete(output);
    return result;
}

bool XDeviceUsbHost_setIoEndpoint(XFd fd, XDeviceUsbEndpointAddress endpointIn,
                              XDeviceUsbEndpointAddress endpointOut)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(XDeviceUsbEndpointAddress, endpointIn),
                            XVar(XDeviceUsbEndpointAddress, endpointOut));
    if (!input) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_SetIoEndpoint, input, NULL);
    XVarList_delete(input);
    return result;
}

XDeviceUsbTransferResult XDeviceUsbHost_getDescriptor(
    XFd fd, XDeviceUsbDescriptorType descriptorType, uint8_t descriptorIndex,
    uint16_t languageId, void* data, size_t capacity, size_t* transferred,
    int32_t timeoutMs)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_InvalidArgument;
    XDeviceUsbTransferResult* slot = &result;
    input = XVarList_Create(XVar(XDeviceUsbDescriptorType, descriptorType),
                            XVar(uint8_t, descriptorIndex),
                            XVar(uint16_t, languageId),
                            XVar(void*, data),
                            XVar(size_t, capacity),
                            XVar(size_t*, transferred),
                            XVar(int32_t, timeoutMs));
    if (!input) return XDeviceUsbTransferResult_ResourceError;
    output = XVarList_Create(XVar(XDeviceUsbTransferResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbTransferResult_ResourceError; }
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_GetDescriptor, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

XDeviceUsbTransferResult XDeviceUsbHost_getStringDescriptor_utf8(
    XFd fd, uint8_t descriptorIndex, uint16_t languageId, char* utf8,
    size_t capacity, size_t* length, int32_t timeoutMs)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_InvalidArgument;
    XDeviceUsbTransferResult* slot = &result;
    input = XVarList_Create(XVar(uint8_t, descriptorIndex),
                            XVar(uint16_t, languageId),
                            XVar(char*, utf8),
                            XVar(size_t, capacity),
                            XVar(size_t*, length),
                            XVar(int32_t, timeoutMs));
    if (!input) return XDeviceUsbTransferResult_ResourceError;
    output = XVarList_Create(XVar(XDeviceUsbTransferResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbTransferResult_ResourceError; }
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_GetStringDescriptor, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbHost_setConfiguration(XFd fd, uint8_t configurationValue)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(uint8_t, configurationValue));
    if (!input) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_SetConfiguration, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbHost_getConfiguration(XFd fd, uint8_t* configurationValue)
{
    XVarList* output;
    bool result;
    if (!configurationValue) return false;
    output = XVarList_Create(XVar(uint8_t*, configurationValue));
    if (!output) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_GetConfiguration, NULL, output);
    XVarList_delete(output);
    return result;
}

bool XDeviceUsbHost_claimInterface(XFd fd, uint8_t interfaceNumber)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(uint8_t, interfaceNumber));
    if (!input) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_ClaimInterface, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbHost_releaseInterface(XFd fd, uint8_t interfaceNumber)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(uint8_t, interfaceNumber));
    if (!input) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_ReleaseInterface, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbHost_setAlternateSetting(XFd fd, uint8_t interfaceNumber,
                                    uint8_t alternateSetting)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(uint8_t, interfaceNumber),
                            XVar(uint8_t, alternateSetting));
    if (!input) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_SetAlternateSetting, input, NULL);
    XVarList_delete(input);
    return result;
}

size_t XDeviceUsbHost_endpointCount(XFd fd, uint8_t interfaceNumber,
                                uint8_t alternateSetting)
{
    XVarList* input;
    XVarList* output;
    size_t count = 0;
    size_t* slot = &count;
    bool result;
    input = XVarList_Create(XVar(uint8_t, interfaceNumber),
                            XVar(uint8_t, alternateSetting));
    if (!input) return 0;
    output = XVarList_Create(XVar(size_t*, slot));
    if (!output) { XVarList_delete(input); return 0; }
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_EndpointCount, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result ? count : 0;
}

bool XDeviceUsbHost_getEndpointInfo(XFd fd, uint8_t interfaceNumber,
                                uint8_t alternateSetting, size_t index,
                                XDeviceUsbEndpointInfo* endpoint)
{
    XVarList* input;
    XVarList* output;
    bool result;
    if (!endpoint) return false;
    input = XVarList_Create(XVar(uint8_t, interfaceNumber),
                            XVar(uint8_t, alternateSetting),
                            XVar(size_t, index));
    if (!input) return false;
    output = XVarList_Create(XVar(XDeviceUsbEndpointInfo*, endpoint));
    if (!output) { XVarList_delete(input); return false; }
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_GetEndpointInfo, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbHost_clearHalt(XFd fd, XDeviceUsbEndpointAddress endpoint)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(XDeviceUsbEndpointAddress, endpoint));
    if (!input) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_ClearHalt, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbHost_reset(XFd fd)
{
    return XDeviceUsbHost_control(fd, XDeviceUsbCommand_Reset, NULL, NULL);
}

XDeviceUsbTransferResult XDeviceUsbHost_controlTransfer(
    XFd fd, const XDeviceUsbControlRequest* request, void* data,
    size_t capacity, size_t* transferred, int32_t timeoutMs)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_InvalidArgument;
    XDeviceUsbTransferResult* slot = &result;
    if (!request) return XDeviceUsbTransferResult_InvalidArgument;
    input = XVarList_Create(XVar(const XDeviceUsbControlRequest*, request),
                            XVar(void*, data),
                            XVar(size_t, capacity),
                            XVar(size_t*, transferred),
                            XVar(int32_t, timeoutMs));
    if (!input) return XDeviceUsbTransferResult_ResourceError;
    output = XVarList_Create(XVar(XDeviceUsbTransferResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbTransferResult_ResourceError; }
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_ControlTransfer, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

XDeviceUsbTransferResult XDeviceUsbHost_transfer(
    XFd fd, XDeviceUsbEndpointAddress endpoint, void* data, size_t length,
    size_t* transferred, int32_t timeoutMs)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_InvalidArgument;
    XDeviceUsbTransferResult* slot = &result;
    if (endpoint == 0) return XDeviceUsbTransferResult_InvalidArgument;
    input = XVarList_Create(XVar(XDeviceUsbEndpointAddress, endpoint),
                            XVar(void*, data),
                            XVar(size_t, length),
                            XVar(size_t*, transferred),
                            XVar(int32_t, timeoutMs));
    if (!input) return XDeviceUsbTransferResult_ResourceError;
    output = XVarList_Create(XVar(XDeviceUsbTransferResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbTransferResult_ResourceError; }
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_Transfer, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

XDeviceUsbTransferId XDeviceUsbHost_submitTransfer(
    XFd fd, const XDeviceUsbTransferRequest* request,
    XDeviceUsbTransferCallback callback, void* userData)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferId id = XDEVICE_USB_INVALID_TRANSFER_ID;
    XDeviceUsbTransferId* slot = &id;
    if (!request || !callback) return XDEVICE_USB_INVALID_TRANSFER_ID;
    input = XVarList_Create(XVar(const XDeviceUsbTransferRequest*, request),
                            XVar(XDeviceUsbTransferCallback, callback),
                            XVar(void*, userData));
    if (!input) return XDEVICE_USB_INVALID_TRANSFER_ID;
    output = XVarList_Create(XVar(XDeviceUsbTransferId*, slot));
    if (!output) { XVarList_delete(input); return XDEVICE_USB_INVALID_TRANSFER_ID; }
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_SubmitTransfer, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return id;
}

XDeviceUsbTransferResult XDeviceUsbHost_cancelTransfer(XFd fd,
                                                   XDeviceUsbTransferId transferId)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_InvalidArgument;
    XDeviceUsbTransferResult* slot = &result;
    input = XVarList_Create(XVar(XDeviceUsbTransferId, transferId));
    if (!input) return XDeviceUsbTransferResult_ResourceError;
    output = XVarList_Create(XVar(XDeviceUsbTransferResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbTransferResult_ResourceError; }
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_CancelTransfer, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbHost_enumerate(XFd fd, XDeviceUsbEnumerateCallback callback,
                          void* userData)
{
    XVarList* input;
    bool result;
    if (!callback) return false;
    input = XVarList_Create(XVar(XDeviceUsbEnumerateCallback, callback),
                            XVar(void*, userData));
    if (!input) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_Enumerate, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceUsbHost_setHotplugCallback(XFd fd, XDeviceUsbHotplugCallback callback,
                                   void* userData)
{
    XVarList* input;
    bool result;
    input = XVarList_Create(XVar(XDeviceUsbHotplugCallback, callback),
                            XVar(void*, userData));
    if (!input) return false;
    result = XDeviceUsbHost_control(fd, XDeviceUsbCommand_SetHotplugCallback, input, NULL);
    XVarList_delete(input);
    return result;
}

XDeviceUsbProcessResult XDeviceUsbHost_processEvents(XFd fd, int32_t timeoutMs)
{
    XVarList* input;
    XVarList* output;
    XDeviceUsbProcessResult result = XDeviceUsbProcessResult_Error;
    XDeviceUsbProcessResult* slot = &result;
    input = XVarList_Create(XVar(int32_t, timeoutMs));
    if (!input) return XDeviceUsbProcessResult_Error;
    output = XVarList_Create(XVar(XDeviceUsbProcessResult*, slot));
    if (!output) { XVarList_delete(input); return XDeviceUsbProcessResult_Error; }
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_ProcessEvents, input, output);
    XVarList_delete(output);
    XVarList_delete(input);
    return result;
}

XDeviceUsbFeatures XDeviceUsbHost_features(XFd fd)
{
    XVarList* output;
    XDeviceUsbFeatures features = XDeviceUsbFeature_None;
    XDeviceUsbFeatures* slot = &features;
    output = XVarList_Create(XVar(XDeviceUsbFeatures*, slot));
    if (!output) return XDeviceUsbFeature_None;
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_Features, NULL, output);
    XVarList_delete(output);
    return features;
}

XDeviceUsbNativeHandle XDeviceUsbHost_handle(XFd fd)
{
    XVarList* output;
    XDeviceUsbNativeHandle handle = XDEVICE_USB_INVALID_NATIVE_HANDLE;
    XDeviceUsbNativeHandle* slot = &handle;
    output = XVarList_Create(XVar(XDeviceUsbNativeHandle*, slot));
    if (!output) return XDEVICE_USB_INVALID_NATIVE_HANDLE;
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_Handle, NULL, output);
    XVarList_delete(output);
    return handle;
}

XDeviceUsbError XDeviceUsbHost_lastError(XFd fd)
{
    XVarList* output;
    XDeviceUsbError error = XDeviceUsbError_None;
    XDeviceUsbError* slot = &error;
    output = XVarList_Create(XVar(XDeviceUsbError*, slot));
    if (!output) return XDeviceUsbError_Unknown;
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_LastError, NULL, output);
    XVarList_delete(output);
    return error;
}

int32_t XDeviceUsbHost_nativeError(XFd fd)
{
    XVarList* output;
    int32_t code = 0;
    int32_t* slot = &code;
    output = XVarList_Create(XVar(int32_t*, slot));
    if (!output) return 0;
    XDeviceUsbHost_control(fd, XDeviceUsbCommand_NativeError, NULL, output);
    XVarList_delete(output);
    return code;
}

void XDeviceUsbHost_clearError(XFd fd)
{
    (void)XDeviceUsbHost_control(fd, XDeviceUsbCommand_ClearError, NULL, NULL);
}

const char* XDeviceUsbHost_errorString(XDeviceUsbError error)
{
    switch (error) {
    case XDeviceUsbError_None:             return "No error";
    case XDeviceUsbError_InvalidArgument:  return "Invalid argument";
    case XDeviceUsbError_NotOpen:          return "Not open";
    case XDeviceUsbError_AlreadyOpen:      return "Already open";
    case XDeviceUsbError_NoDevice:         return "No device";
    case XDeviceUsbError_Busy:             return "Busy";
    case XDeviceUsbError_PermissionDenied: return "Permission denied";
    case XDeviceUsbError_Timeout:          return "Timeout";
    case XDeviceUsbError_Stall:            return "USB STALL";
    case XDeviceUsbError_Disconnected:     return "Disconnected";
    case XDeviceUsbError_Unsupported:      return "Unsupported";
    case XDeviceUsbError_Resource:         return "Resource error";
    case XDeviceUsbError_Controller:       return "Controller error";
    case XDeviceUsbError_Io:               return "I/O error";
    case XDeviceUsbError_Interrupted:      return "Interrupted";
    case XDeviceUsbError_Unknown:          return "Unknown";
    default:                               return "Unknown";
    }
}
