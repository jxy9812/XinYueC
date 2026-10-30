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

#if XDEVICE_ON && XDEVICE_SERIALPORT_ON

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
bool XDeviceSerialPort_platformEnumeratePorts(XDeviceSerialPortPortCollector collector,
                                              void* userData);

/* ================================================================
 * 可读事件源（跨平台统一的等待集接入，2026-10-30）
 *
 * 语义：串口 fd 注册进**框架自己的异步后端等待集**（数据到达 ⇒ 内核
 * 唤醒阻塞中的事件循环 ⇒ 回调属主发射 readyRead）。**不再等下一轮
 * 「毫秒级轮询拍」**——板端主循环一圈 100+ms 时，轮询拍的感知延迟同
 * 量级，是 485 轮询「卡一下」的来源之一。
 *
 * 与网络套接字同一套机制（socket notifier / fbdev 触摸通道）：
 *   - POSIX：框架异步后端 XAbstractNetIoRing 的 **epoll / io_uring 双
 *     引擎**（同一 fd 语义 ⇒ 跨设备行为统一）；
 *   - Windows：框架 IOCP。
 *
 * 不支持事件驱动的平台（裸机 RTOS 等）⇒ 返回 false，调用方保留毫秒级
 * 轮询兜底，行为与改造前逐位一致（不产生功能回退）。
 * ================================================================ */
#if !XPLATFORM_POSIX && !defined(_WIN32)
/* 非 POSIX / 非 Windows（裸机 RTOS 等）：无框架异步后端 ⇒ 不支持。 */
bool XDeviceSerialPort_platformArmReadable(XFd fd,
                                           void (*notify)(void* ctx),
                                           void* ctx)
{
    (void)fd; (void)notify; (void)ctx;
    return false;
}

void XDeviceSerialPort_platformDisarmReadable(XFd fd)
{
    (void)fd;
}
#endif

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

/* ============================================================================
 * 串口列表：条目字符串的深拷贝、释放与复制
 * ============================================================================ */

/**
 * @brief 复制一个 UTF-8 字符串到系统堆。
 * @param text 源字符串；可为 NULL。
 * @return 新分配的副本（调用方用 XFree_System 释放）；text 为 NULL 时返回 NULL。
 */
static char* serialPortDup(const char* text)
{
    size_t length;
    char* copy;
    if (!text) return NULL;
    length = strlen(text);
    copy = (char*)XMalloc_System(length + 1);
    if (!copy) return NULL;
    memcpy(copy, text, length + 1);
    return copy;
}

/**
 * @brief 释放串口列表的条目数组与条目字符串。
 * @param list 列表；可为 NULL。
 */
static void serialPortFreeList(XDeviceSerialPortPortList* list)
{
    size_t i;
    if (!list) return;
    for (i = 0; i < list->m_count; ++i) {
        XDeviceSerialPortPortInfo* port = &list->m_ports[i];
        if (port->m_portName) XFree_System((void*)port->m_portName);
        if (port->m_description) XFree_System((void*)port->m_description);
        if (port->m_manufacturer) XFree_System((void*)port->m_manufacturer);
        if (port->m_serialNumber) XFree_System((void*)port->m_serialNumber);
    }
    if (list->m_ports) XFree_System(list->m_ports);
    list->m_ports = NULL;
    list->m_count = 0;
}

/**
 * @brief 深拷贝一个串口列表。
 * @param source 源列表；可为 NULL。
 * @return 新分配的列表（调用方用 XDeviceSerialPortPortList_delete 释放）；
 *         source 为 NULL 或分配失败时返回 NULL。
 */
static XDeviceSerialPortPortList* serialPortCopyList(const XDeviceSerialPortPortList* source)
{
    XDeviceSerialPortPortList* copy;
    size_t i;
    if (!source) return NULL;
    copy = (XDeviceSerialPortPortList*)XCalloc_System(1, sizeof(*copy));
    if (!copy) return NULL;
    if (source->m_count == 0) return copy;
    copy->m_ports = (XDeviceSerialPortPortInfo*)XCalloc_System(source->m_count,
                                                               sizeof(XDeviceSerialPortPortInfo));
    if (!copy->m_ports) { XFree_System(copy); return NULL; }
    for (i = 0; i < source->m_count; ++i) {
        const XDeviceSerialPortPortInfo* from = &source->m_ports[i];
        XDeviceSerialPortPortInfo* to = &copy->m_ports[i];
        to->m_portName = serialPortDup(from->m_portName);
        to->m_description = serialPortDup(from->m_description);
        to->m_manufacturer = serialPortDup(from->m_manufacturer);
        to->m_serialNumber = serialPortDup(from->m_serialNumber);
        to->m_vendorId = from->m_vendorId;
        to->m_productId = from->m_productId;
        to->m_busy = from->m_busy;
    }
    copy->m_count = source->m_count;
    return copy;
}

/**
 * @brief 平台枚举过程中的临时收集状态。
 */
typedef struct XSerialPortCollectState
{
    XDeviceSerialPortPortInfo* m_ports;
    size_t m_count;
    size_t m_capacity;
} XSerialPortCollectState;

/**
 * @brief 平台枚举回调：把一个条目深拷贝进收集状态。
 * @param userData XSerialPortCollectState*。
 * @param port 当前条目；借用。
 * @return false 表示内存不足、提前终止枚举。
 */
static bool serialPortCollect(void* userData, const XDeviceSerialPortPortInfo* port)
{
    XSerialPortCollectState* state = (XSerialPortCollectState*)userData;
    XDeviceSerialPortPortInfo* target;
    if (!state || !port || !port->m_portName) return true;
    if (state->m_count >= state->m_capacity) {
        size_t capacity = state->m_capacity ? state->m_capacity * 2 : 8;
        XDeviceSerialPortPortInfo* grown = (XDeviceSerialPortPortInfo*)XRealloc_System(
            state->m_ports, capacity * sizeof(XDeviceSerialPortPortInfo));
        if (!grown) return false;
        state->m_ports = grown;
        state->m_capacity = capacity;
    }
    target = &state->m_ports[state->m_count];
    memset(target, 0, sizeof(*target));
    target->m_portName = serialPortDup(port->m_portName);
    if (!target->m_portName) return false;
    target->m_description = serialPortDup(port->m_description);
    target->m_manufacturer = serialPortDup(port->m_manufacturer);
    target->m_serialNumber = serialPortDup(port->m_serialNumber);
    target->m_vendorId = port->m_vendorId;
    target->m_productId = port->m_productId;
    target->m_busy = port->m_busy;
    ++state->m_count;
    return true;
}

/**
 * @brief 刷新设备上下文里的可用串口缓存。
 * @param context 设备上下文。
 * @return 枚举成功返回 true；失败时缓存为空。
 */
static bool serialPortRefreshPorts(XDeviceSerialPortContext* context)
{
    XSerialPortCollectState state;
    if (!context) return false;
    serialPortFreeList(&context->m_availablePorts);
    memset(&state, 0, sizeof(state));
    if (!XDeviceSerialPort_platformEnumeratePorts(serialPortCollect, &state)) {
        /* 枚举中断：释放已收集条目，缓存保持为空。 */
        if (state.m_ports) {
            XDeviceSerialPortPortList partial;
            partial.m_count = state.m_count;
            partial.m_ports = state.m_ports;
            serialPortFreeList(&partial);
        }
        return false;
    }
    context->m_availablePorts.m_count = state.m_count;
    context->m_availablePorts.m_ports = state.m_ports;
    return true;
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
    bool enumerate;
    if (!options || options->m_baudRate <= 0 ||
        !validDataBits(options->m_dataBits) || !validParity(options->m_parity) ||
        !validStopBits(options->m_stopBits) || !validFlowControl(options->m_flowControl) ||
        options->m_readBufferSize <= 0)
        return false;
    enumerate = options->m_enumerate != 0;
    if (!enumerate && !options->m_base.m_target) return false;
    mode = options->m_base.m_openMode;
    return (mode & (XIODevice_ReadOnly | XIODevice_WriteOnly)) != 0;
}

static void deleteBuffers(XDeviceSerialPortContext* context)
{
    if (!context) return;
    if (context->m_readBuffer) {
        XClassDelete((XClass*)context->m_readBuffer);
        context->m_readBuffer = NULL;
    }
    if (context->m_writeBuffer) {
        XClassDelete((XClass*)context->m_writeBuffer);
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
    context->m_enumerate = options->m_enumerate ? 1 : 0;
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

    /* 枚举模式不打开任何具体端口，只保留上下文供查询可用串口属性。 */
    if (context->m_enumerate) {
        context->m_base.m_state = XDeviceState_Active;
        context->m_base.m_lastError = XDeviceError_None;
        if (error) *error = XDeviceError_None;
        return &context->m_base;
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
    serialPortFreeList(&context->m_availablePorts);
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
    XClassDeinit((XClass*)&value);
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
    case XDeviceSerialPortProperty_AvailablePorts:
        if (!serialPortRefreshPorts(context)) return false;
        XVariant_setValue_ptr(value, (void*)&context->m_availablePorts);
        return true;
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
            XClassDeinit((XClass*)&value);
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

void XDeviceSerialPortPortList_delete(XDeviceSerialPortPortList* list)
{
    if (!list) return;
    serialPortFreeList(list);
    XFree_System(list);
}

XDeviceSerialPortPortList* XDeviceSerialPort_availablePorts(XFd fd)
{
    XVariant value;
    void** reference;
    const XDeviceSerialPortPortList* borrowed;
    XDeviceSerialPortPortList* copy;

    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    if (!XDevice_getProperty(fd, XDeviceSerialPortProperty_AvailablePorts, &value)) {
        XClassDeinit((XClass*)&value);
        return NULL;
    }
    reference = (void**)XVariant_toRef(&value, XVariantType_Ptr);
    borrowed = reference ? (const XDeviceSerialPortPortList*)(*reference) : NULL;
    copy = borrowed ? serialPortCopyList(borrowed) : NULL;
    XClassDeinit((XClass*)&value);
    return copy;
}

#else

void XDeviceSerialPortOpenOptions_init(XDeviceSerialPortOpenOptions* options)
{
    if (options) memset(options, 0, sizeof(*options));
}
bool XDeviceSerialPort_register(void) { return false; }
void XDeviceSerialPortPortList_delete(XDeviceSerialPortPortList* list) { (void)list; }
XDeviceSerialPortPortList* XDeviceSerialPort_availablePorts(XFd fd)
{
    (void)fd;
    return NULL;
}

#endif /* XDEVICE_SERIALPORT_ON */
