#include "XDeviceSerialPort.h"
#include "XIODevice.h"
#include "XRingBuffer.h"
#include "XString.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XMemory.h"
#include "XFileDescriptor.h"
#include <stdlib.h>
#include <string.h>

#if XSERIALPORT_ON

/* 所选平台在自己的 .c 中实现这些钩子；它们不是公共 API。 */
XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void);
void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* context);
bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName);
void XDeviceSerialPort_platformClose(XFd fd);
int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size);
int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size);
bool XDeviceSerialPort_platformFlush(XFd fd);
bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property, const XVariant* value);
bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property, XVariant* value);
bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command,
                                       const XVarList* in, XVarList* out);

static XDeviceContext* VXDeviceSerialPort_open(XDevice* self,
    const XDeviceOpenOptions* options, int* error);
static void VXDeviceSerialPort_close(XDevice* self, XDeviceContext* handle);
static int64_t VXDeviceSerialPort_read(XDevice* self, XDeviceContext* handle,
    void* buffer, int64_t size);
static int64_t VXDeviceSerialPort_write(XDevice* self, XDeviceContext* handle,
    const void* data, int64_t size);
static int64_t VXDeviceSerialPort_seek(XDevice* self, XDeviceContext* handle,
    int64_t offset, int whence);
static bool VXDeviceSerialPort_flush(XDevice* self, XDeviceContext* handle);
static bool VXDeviceSerialPort_resize(XDevice* self, XDeviceContext* handle, int64_t size);
static bool VXDeviceSerialPort_setProperty(XDevice* self, XDeviceContext* handle,
    uint32_t property, const XVariant* value);
static bool VXDeviceSerialPort_getProperty(XDevice* self, XDeviceContext* handle,
    uint32_t property, XVariant* value);
static bool VXDeviceSerialPort_queryProperty(XDevice* self, XDeviceContext* handle,
    uint32_t property, XVariant* value);
static bool VXDeviceSerialPort_control(XDevice* self, XDeviceContext* handle,
    uint32_t command, const XVarList* in, XVarList* out);

static XDeviceSerialPort g_deviceSerialPort;
static XDeviceSerialPortOpenOptions g_defaultSerialOptions;
static bool g_defaultSerialOptionsInitialized = false;

static XDeviceSerialPortContext* serialContext(XDeviceContext* handle)
{
    return (XDeviceSerialPortContext*)handle;
}

static bool serialSetError(XDeviceSerialPortContext* context,
                           XSerialPort_Error serialError, XDeviceError deviceError)
{
    if (context) {
        context->m_error = serialError;
        context->m_base.m_lastError = (int16_t)deviceError;
    }
    return false;
}

static bool validDataBits(int value)
{
    return value == XSerialPort_Data5 || value == XSerialPort_Data6 ||
           value == XSerialPort_Data7 || value == XSerialPort_Data8 ||
           value == XSerialPort_Data9;
}

static bool validParity(int value)
{
    return value == XSerialPort_NoParity || value == XSerialPort_EvenParity ||
           value == XSerialPort_OddParity || value == XSerialPort_SpaceParity ||
           value == XSerialPort_MarkParity;
}

static bool validStopBits(int value)
{
    return value == XSerialPort__ZeroPointFive || value == XSerialPort_OneStop ||
           value == XSerialPort_OneAndHalfStop || value == XSerialPort_TwoStop;
}

static bool validFlowControl(int value)
{
    return value >= XSerialPort_NoFlowControl && value <= XSerialPort_BothControl;
}

void XDeviceSerialPortOpenOptions_init(XDeviceSerialPortOpenOptions* options)
{
    if (!options) return;
    memset(options, 0, sizeof(*options));
    options->m_base.m_openMode = XIODevice_ReadWrite;
    options->m_base.m_flags = XDeviceOpenFlag_NonBlocking;
    options->m_baudRate = XSerialPort_Baud9600;
    options->m_dataBits = XSerialPort_Data8;
    options->m_parity = XSerialPort_NoParity;
    options->m_stopBits = XSerialPort_OneStop;
    options->m_flowControl = XSerialPort_NoFlowControl;
    options->m_readBufferSize = 512 * 1024;
}

XVtable* XDeviceSerialPort_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceSerialPort)
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceSerialPort_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceSerialPort_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Read, VXDeviceSerialPort_read);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Write, VXDeviceSerialPort_write);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Seek, VXDeviceSerialPort_seek);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Flush, VXDeviceSerialPort_flush);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Resize, VXDeviceSerialPort_resize);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_SetProperty, VXDeviceSerialPort_setProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_GetProperty, VXDeviceSerialPort_getProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_QueryProperty, VXDeviceSerialPort_queryProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceSerialPort_control);
    XCLASS_SET_CLASS_NAME_DEFAULT("serial");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceSerialPort);
    return XVTABLE_DEFAULT;
}

void XDeviceSerialPort_init(XDeviceSerialPort* self)
{
    if (!self) return;
    memset(((XDevice*)self) + 1, 0, sizeof(*self) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceSerialPort);
    self->m_base.m_type = XDeviceType_Serial;
    self->m_base.m_capabilities = XDeviceCap_Read | XDeviceCap_Write |
                                  XDeviceCap_Flush;
    if (!g_defaultSerialOptionsInitialized) {
        XDeviceSerialPortOpenOptions_init(&g_defaultSerialOptions);
        g_defaultSerialOptionsInitialized = true;
    }
    self->m_base.m_defaultOpenOptions = &g_defaultSerialOptions.m_base;
}

XDeviceSerialPort* XDeviceSerialPort_create(void)
{
    XDeviceSerialPort* self = (XDeviceSerialPort*)XClass_Malloc(XDeviceSerialPort);
    if (!self) return NULL;
    XDeviceSerialPort_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

static bool validOpenOptions(const XDeviceSerialPortOpenOptions* options)
{
    int mode;
    if (!options || !options->m_base.m_target || options->m_baudRate <= 0 ||
        !validDataBits(options->m_dataBits) || !validParity(options->m_parity) ||
        !validStopBits(options->m_stopBits) || !validFlowControl(options->m_flowControl) ||
        options->m_readBufferSize <= 0)
        return false;
    mode = options->m_base.m_openMode;
    return (mode & (XIODevice_ReadOnly | XIODevice_WriteOnly)) != 0;
}

static void deleteBuffers(XDeviceSerialPortContext* context)
{
    if (!context) return;
    if (context->m_readBuffer) {
        XRingBuffer_delete_base((XClass*)context->m_readBuffer);
        context->m_readBuffer = NULL;
    }
    if (context->m_writeBuffer) {
        XRingBuffer_delete_base((XClass*)context->m_writeBuffer);
        context->m_writeBuffer = NULL;
    }
}

static XDeviceContext* VXDeviceSerialPort_open(XDevice* self,
    const XDeviceOpenOptions* baseOptions, int* error)
{
    const XDeviceSerialPortOpenOptions* options =
        (const XDeviceSerialPortOpenOptions*)baseOptions;
    XDeviceSerialPortContext* context;

    if (!validOpenOptions(options)) {
        if (error) *error = XDeviceError_InvalidArgument;
        return NULL;
    }

    context = XDeviceSerialPort_platformCreateContext();
    if (!context) {
        if (error) *error = XDeviceError_OutOfMemory;
        return NULL;
    }
    context->m_base.m_fd = XFD_INVALID;
    context->m_base.m_device = self;
    context->m_base.m_state = XDeviceState_Opening;
    context->m_base.m_ioMode = XDeviceIoMode_Sync;
    context->m_owner = options->m_owner;
    context->m_platformData = options->m_platformData;
    context->m_openMode = options->m_base.m_openMode;
    context->m_flags = options->m_base.m_flags;
    context->m_baudRate = options->m_baudRate;
    context->m_dataBits = options->m_dataBits;
    context->m_parity = options->m_parity;
    context->m_stopBits = options->m_stopBits;
    context->m_flowControl = options->m_flowControl;
    context->m_readBufferSize = options->m_readBufferSize;
    context->m_error = XSerialPort_NoError;
    context->m_readBuffer = XRingBuffer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 2048);
    context->m_writeBuffer = XRingBuffer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 2048);
    if (!context->m_readBuffer || !context->m_writeBuffer) {
        deleteBuffers(context);
        XDeviceSerialPort_platformDeleteContext(context);
        if (error) *error = XDeviceError_OutOfMemory;
        return NULL;
    }

    context->m_base.m_fd = XFd_alloc(XFD_TYPE_CLASS, &context->m_base, context->m_owner);
    if (context->m_base.m_fd == XFD_INVALID) {
        deleteBuffers(context);
        XDeviceSerialPort_platformDeleteContext(context);
        if (error) *error = XDeviceError_OutOfMemory;
        return NULL;
    }

    if (!XDeviceSerialPort_platformOpen(context->m_base.m_fd, options->m_base.m_target)) {
        XFd_free(context->m_base.m_fd);
        context->m_base.m_fd = XFD_INVALID;
        deleteBuffers(context);
        XDeviceSerialPort_platformDeleteContext(context);
        if (error) *error = XDeviceError_IoFail;
        return NULL;
    }

    context->m_base.m_state = XDeviceState_Active;
    context->m_base.m_lastError = XDeviceError_None;
    if (error) *error = XDeviceError_None;
    return &context->m_base;
}

static void VXDeviceSerialPort_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceSerialPortContext* context = serialContext(handle);
    (void)self;
    if (!context) return;
    XDeviceSerialPort_platformClose(context->m_base.m_fd);
    deleteBuffers(context);
    XDeviceSerialPort_platformDeleteContext(context);
}

static int64_t VXDeviceSerialPort_read(XDevice* self, XDeviceContext* handle,
    void* buffer, int64_t size)
{
    XDeviceSerialPortContext* context = serialContext(handle);
    int64_t result;
    (void)self;
    if (!context || size < 0 || (!buffer && size > 0)) return -1;
    if (size == 0) return 0;
    result = XDeviceSerialPort_platformRead(context->m_base.m_fd, buffer, size);
    if (result < 0)
        serialSetError(context, XSerialPort_ReadError, XDeviceError_IoFail);
    return result;
}

static int64_t VXDeviceSerialPort_write(XDevice* self, XDeviceContext* handle,
    const void* data, int64_t size)
{
    XDeviceSerialPortContext* context = serialContext(handle);
    int64_t result;
    (void)self;
    if (!context || size < 0 || (!data && size > 0)) return -1;
    if (size == 0) return 0;
    result = XDeviceSerialPort_platformWrite(context->m_base.m_fd, data, size);
    if (result < 0)
        serialSetError(context, XSerialPort_WriteError, XDeviceError_IoFail);
    return result;
}

static int64_t VXDeviceSerialPort_seek(XDevice* self, XDeviceContext* handle,
    int64_t offset, int whence)
{
    (void)self; (void)handle; (void)offset; (void)whence;
    return -1;
}

static bool VXDeviceSerialPort_flush(XDevice* self, XDeviceContext* handle)
{
    XDeviceSerialPortContext* context = serialContext(handle);
    (void)self;
    if (!context || !XDeviceSerialPort_platformFlush(context->m_base.m_fd))
        return serialSetError(context, XSerialPort_WriteError, XDeviceError_IoFail);
    return true;
}

static bool VXDeviceSerialPort_resize(XDevice* self, XDeviceContext* handle, int64_t size)
{
    (void)self; (void)handle; (void)size;
    return false;
}

static bool applyChangedProperty(XDeviceSerialPortContext* context, uint32_t property,
                                 const XVariant* value)
{
    if (XDeviceSerialPort_platformSetProperty(context->m_base.m_fd, property, value))
        return true;
    return serialSetError(context, XSerialPort_UnsupportedOperationError,
                          XDeviceError_NotSupported);
}

static bool VXDeviceSerialPort_setProperty(XDevice* self, XDeviceContext* handle,
    uint32_t property, const XVariant* value)
{
    XDeviceSerialPortContext* context = serialContext(handle);
    int oldInt;
    int64_t oldInt64;
    bool oldBool;
    int newValue;
    (void)self;
    if (!context || !value) return false;

    switch (property) {
    case XDeviceSerialPortProperty_BaudRate:
        newValue = XVariant_toInt(value);
        if (newValue <= 0) return serialSetError(context, XSerialPort_UnsupportedOperationError,
                                                 XDeviceError_InvalidArgument);
        oldInt = context->m_baudRate;
        context->m_baudRate = newValue;
        if (applyChangedProperty(context, property, value)) return true;
        context->m_baudRate = oldInt;
        (void)XDeviceSerialPort_platformSetProperty(context->m_base.m_fd, property, value);
        return false;
    case XDeviceSerialPortProperty_DataBits:
        newValue = XVariant_toInt(value);
        if (!validDataBits(newValue)) return serialSetError(context, XSerialPort_UnsupportedOperationError,
                                                            XDeviceError_InvalidArgument);
        oldInt = context->m_dataBits;
        context->m_dataBits = (XSerialPort_DataBits)newValue;
        if (applyChangedProperty(context, property, value)) return true;
        context->m_dataBits = (XSerialPort_DataBits)oldInt;
        return false;
    case XDeviceSerialPortProperty_Parity:
        newValue = XVariant_toInt(value);
        if (!validParity(newValue)) return serialSetError(context, XSerialPort_UnsupportedOperationError,
                                                          XDeviceError_InvalidArgument);
        oldInt = context->m_parity;
        context->m_parity = (XSerialPort_Parity)newValue;
        if (applyChangedProperty(context, property, value)) return true;
        context->m_parity = (XSerialPort_Parity)oldInt;
        return false;
    case XDeviceSerialPortProperty_StopBits:
        newValue = XVariant_toInt(value);
        if (!validStopBits(newValue)) return serialSetError(context, XSerialPort_UnsupportedOperationError,
                                                            XDeviceError_InvalidArgument);
        oldInt = context->m_stopBits;
        context->m_stopBits = (XSerialPort_StopBits)newValue;
        if (applyChangedProperty(context, property, value)) return true;
        context->m_stopBits = (XSerialPort_StopBits)oldInt;
        return false;
    case XDeviceSerialPortProperty_FlowControl:
        newValue = XVariant_toInt(value);
        if (!validFlowControl(newValue)) return serialSetError(context, XSerialPort_UnsupportedOperationError,
                                                               XDeviceError_InvalidArgument);
        oldInt = context->m_flowControl;
        context->m_flowControl = (XSerialPort_FlowControl)newValue;
        if (applyChangedProperty(context, property, value)) return true;
        context->m_flowControl = (XSerialPort_FlowControl)oldInt;
        return false;
    case XDeviceSerialPortProperty_ReadBufferSize:
        oldInt64 = context->m_readBufferSize;
        context->m_readBufferSize = XVariant_toInt64(value);
        if (context->m_readBufferSize <= 0) {
            context->m_readBufferSize = oldInt64;
            return serialSetError(context, XSerialPort_UnsupportedOperationError,
                                  XDeviceError_InvalidArgument);
        }
        if (applyChangedProperty(context, property, value)) return true;
        context->m_readBufferSize = oldInt64;
        return false;
    case XDeviceSerialPortProperty_DataTerminalReady:
        oldBool = context->m_dataTerminalReady;
        if (!applyChangedProperty(context, property, value)) return false;
        context->m_dataTerminalReady = XVariant_toBool(value);
        (void)oldBool;
        return true;
    case XDeviceSerialPortProperty_RequestToSend:
        oldBool = context->m_requestToSend;
        if (!applyChangedProperty(context, property, value)) return false;
        context->m_requestToSend = XVariant_toBool(value);
        (void)oldBool;
        return true;
    case XDeviceSerialPortProperty_BreakEnabled:
        oldBool = context->m_breakEnabled;
        if (!applyChangedProperty(context, property, value)) return false;
        context->m_breakEnabled = XVariant_toBool(value);
        (void)oldBool;
        return true;
    default:
        return false;
    }
}

static bool getPlatformInt64(XDeviceSerialPortContext* context, uint32_t property,
                             int64_t* result)
{
    XVariant value;
    bool ok;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    ok = XDeviceSerialPort_platformGetProperty(context->m_base.m_fd, property, &value);
    if (ok) *result = XVariant_toInt64(&value);
    XVariant_deinit_base((XClass*)&value);
    return ok;
}

static bool VXDeviceSerialPort_getProperty(XDevice* self, XDeviceContext* handle,
    uint32_t property, XVariant* value)
{
    XDeviceSerialPortContext* context = serialContext(handle);
    int64_t count;
    (void)self;
    if (!context || !value) return false;
    switch (property) {
    case XDeviceProperty_OpenMode:
        XVariant_setValue_int(value, context->m_openMode); return true;
    case XDeviceProperty_IoMode:
        XVariant_setValue_int(value, context->m_base.m_ioMode); return true;
    case XDeviceProperty_State:
        XVariant_setValue_int(value, context->m_base.m_state); return true;
    case XDeviceProperty_NonBlocking:
        XVariant_setValue_bool(value, true); return true;
    case XDeviceProperty_LastError:
        XVariant_setValue_int(value, context->m_base.m_lastError); return true;
    case XDeviceProperty_NativeHandle:
        return XDeviceSerialPort_platformGetProperty(context->m_base.m_fd, property, value);
    case XDeviceSerialPortProperty_BaudRate:
        XVariant_setValue_int(value, context->m_baudRate); return true;
    case XDeviceSerialPortProperty_DataBits:
        XVariant_setValue_int(value, context->m_dataBits); return true;
    case XDeviceSerialPortProperty_Parity:
        XVariant_setValue_int(value, context->m_parity); return true;
    case XDeviceSerialPortProperty_StopBits:
        XVariant_setValue_int(value, context->m_stopBits); return true;
    case XDeviceSerialPortProperty_FlowControl:
        XVariant_setValue_int(value, context->m_flowControl); return true;
    case XDeviceSerialPortProperty_ReadBufferSize:
        XVariant_setValue_int64(value, context->m_readBufferSize); return true;
    case XDeviceSerialPortProperty_DataTerminalReady:
        XVariant_setValue_bool(value, context->m_dataTerminalReady); return true;
    case XDeviceSerialPortProperty_RequestToSend:
        XVariant_setValue_bool(value, context->m_requestToSend); return true;
    case XDeviceSerialPortProperty_BreakEnabled:
        XVariant_setValue_bool(value, context->m_breakEnabled); return true;
    case XDeviceSerialPortProperty_BytesAvailable:
        count = context->m_readBuffer ? (int64_t)XRingBuffer_available(context->m_readBuffer) : 0;
        {
            int64_t nativeCount = 0;
            if (getPlatformInt64(context, property, &nativeCount) && nativeCount > 0)
                count += nativeCount;
        }
        XVariant_setValue_int64(value, count); return true;
    case XDeviceSerialPortProperty_BytesToWrite:
        count = context->m_writeBuffer ? (int64_t)XRingBuffer_available(context->m_writeBuffer) : 0;
        count += context->m_pendingWriteBytes;
        XVariant_setValue_int64(value, count); return true;
    case XDeviceSerialPortProperty_PinoutSignals:
        return XDeviceSerialPort_platformGetProperty(context->m_base.m_fd, property, value);
    case XDeviceSerialPortProperty_Error:
        XVariant_setValue_int(value, context->m_error); return true;
    default:
        return false;
    }
}

static bool VXDeviceSerialPort_queryProperty(XDevice* self, XDeviceContext* handle,
    uint32_t property, XVariant* value)
{
    return VXDeviceSerialPort_getProperty(self, handle, property, value);
}

static bool VXDeviceSerialPort_control(XDevice* self, XDeviceContext* handle,
    uint32_t command, const XVarList* in, XVarList* out)
{
    XDeviceSerialPortContext* context = serialContext(handle);
    XVarList* arguments = (XVarList*)in;
    (void)self;
    if (!context) return false;
    switch (command) {
    case XDeviceCommand_Cancel:
        return XDeviceSerialPort_platformControl(context->m_base.m_fd, command, in, out);
    case XDeviceCommand_Poll:
        if (out) {
            XVariant value;
            bool ready = false;
            if (out->m_size != sizeof(bool))
                return serialSetError(context, XSerialPort_UnknownError,
                                      XDeviceError_InvalidArgument);
            memset(&value, 0, sizeof(value));
            XVariant_init(&value, NULL, 0, XVariantType_NULL);
            if (VXDeviceSerialPort_getProperty(self, handle,
                    XDeviceSerialPortProperty_BytesAvailable, &value))
                ready = XVariant_toInt64(&value) > 0;
            XVariant_deinit_base((XClass*)&value);
            memcpy(out->data, &ready, sizeof(ready));
            XVarList_start(out);
        }
        return true;
    case XDeviceSerialPortCommand_HandleEvent:
        return XDeviceSerialPort_platformControl(context->m_base.m_fd, command, in, out);
    case XDeviceSerialPortCommand_Clear:
    {
        XSerialPort_Direction directions;
        if (!arguments || arguments->m_size != sizeof(directions))
            return serialSetError(context, XSerialPort_UnknownError,
                                  XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        directions = XVarList_arg(arguments, XSerialPort_Direction);
        if ((directions & XSerialPort_AllDirections) == 0)
            return serialSetError(context, XSerialPort_UnknownError,
                                  XDeviceError_InvalidArgument);
        if (!XDeviceSerialPort_platformControl(context->m_base.m_fd, command, in, out))
            return serialSetError(context, XSerialPort_ResourceError, XDeviceError_IoFail);
        if ((directions & XSerialPort_Input) && context->m_readBuffer)
            XRingBuffer_reset(context->m_readBuffer);
        if ((directions & XSerialPort_Output) && context->m_writeBuffer)
            XRingBuffer_reset(context->m_writeBuffer);
        return true;
    }
    case XDeviceSerialPortCommand_ClearError:
        context->m_error = XSerialPort_NoError;
        context->m_base.m_lastError = XDeviceError_None;
        return true;
    default:
        return false;
    }
}

bool XDeviceSerialPort_register(void)
{
    static bool registered = false;
    if (registered) return true;
    XDeviceSerialPort_init(&g_deviceSerialPort);
    if (!XDevice_register(&g_deviceSerialPort.m_base)) return false;
    registered = true;
    return true;
}

#else

void XDeviceSerialPortOpenOptions_init(XDeviceSerialPortOpenOptions* options)
{
    if (options) memset(options, 0, sizeof(*options));
}
bool XDeviceSerialPort_register(void) { return false; }

#endif /* XSERIALPORT_ON */
