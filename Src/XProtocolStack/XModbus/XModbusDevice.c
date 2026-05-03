#include "XModbusDevice.h"
#include "XMemory.h"
#include <string.h>
static void VXModbusDevice_deinit(XModbusDevice* dev);
// 私有数据结构：用于存储连接参数
struct XModbusDevicePrivate 
{
    XVariant* params[XModbusDevice_NetworkAddressParameter + 1]; // Size based on enum
    XIODevice* io_device; ///< 底层IO设备，对应 QModbusDevice::device()
};

// ----------------- Private Helper Functions -----------------
static void XModbusDevicePrivate_init(XModbusDevicePrivate* d) {
    if (!d) return;
    memset(d->params, 0, sizeof(d->params));
    d->io_device = NULL; // <<<--- 初始化为 NULL
}

static void XModbusDevicePrivate_destroy(XModbusDevicePrivate* d) {
    if (!d) return;
    for (size_t i = 0; i < sizeof(d->params) / sizeof(d->params[0]); ++i) {
        if (d->params[i]) {
            XVariant_delete_base(d->params[i]);
            d->params[i] = NULL;
        }
    }
    // 注意: XModbusDevice 不负责 delete io_device!
    // 生命周期由子类管理。
    d->io_device = NULL; // <<<--- 仅置空
    XMemory_free(d);
}

// 错误字符串映射
static const char* errorToString(XModbusDevice_Error err) {
    switch (err) {
    case XModbusDevice_NoError: return "No error";
    case XModbusDevice_ReadError: return "Read error";
    case XModbusDevice_WriteError: return "Write error";
    case XModbusDevice_ConnectionError: return "Connection error";
    case XModbusDevice_ConfigurationError: return "Configuration error";
    case XModbusDevice_TimeoutError: return "Timeout error";
    case XModbusDevice_ProtocolError: return "Protocol error";
    case XModbusDevice_ReplyAbortedError: return "Reply aborted";
    case XModbusDevice_UnknownError: return "Unknown error";
    case XModbusDevice_InvalidResponseError: return "Invalid response";
    default: return "Undefined error";
    }
}

// ----------------- Virtual Function Table -----------------
// The actual open/close implementations are provided by subclasses.
// We just declare the slots in the vtable.

XVtable* XModbusDevice_class_init() 
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusDevice))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承 XModbusDevice
    XVTABLE_INHERIT_XCLASS(XModbusDevice);
    void* table[] = { NULL,NULL };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载析构
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusDevice_deinit);
#if SHOWCONTAINERSIZE
    printf("XModbusDevice size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// ----------------- Constructor/Destructor -----------------
XModbusDevice* XModbusDevice_create() {
    XModbusDevice* dev = (XModbusDevice*)XMemory_malloc(sizeof(XModbusDevice));
    if (dev) {
        XModbusDevice_init(dev);
        Set_Class_MemoryFree(dev, XFree);
    }
    return dev;
}

void XModbusDevice_init(XModbusDevice* dev) {
    if (!dev) return;

    // Initialize base class
    XObject_init((XObject*)dev);
    XClassGetVtable(dev) = XModbusDevice_class_init();

    // Initialize members
    dev->m_state = XModbusDevice_UnconnectedState;
    dev->m_error = XModbusDevice_NoError;
    dev->m_errorString = NULL;
    dev->m_d = (XModbusDevicePrivate*)XMemory_calloc(1, sizeof(XModbusDevicePrivate));
    if (dev->m_d) {
        XModbusDevicePrivate_init(dev->m_d);
    }
}

// Destructor (called via virtual table)
void VXModbusDevice_deinit(XModbusDevice* dev) {
    if (!dev) return;

    // Clean up private data
    if (dev->m_d) {
        XModbusDevicePrivate_destroy(dev->m_d);
        dev->m_d = NULL;
    }

    // Clean up error string
    if (dev->m_errorString) {
        XString_delete_base(dev->m_errorString);
        dev->m_errorString = NULL;
    }

    // Call base class destructor
   XClass_Deinit_Parent(XObject,(XObject*)dev);
}

// ----------------- Public API Implementation -----------------
XVariant* XModbusDevice_connectionParameter(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter) {
    if (!dev || !dev->m_d || parameter < 0 || parameter >= (int)(sizeof(dev->m_d->params) / sizeof(dev->m_d->params[0]))) {
        return NULL;
    }
    if (dev->m_d->params[parameter]) {
        return XVariant_create_copy(dev->m_d->params[parameter]);
    }
    return NULL;
}

void XModbusDevice_setConnectionParameter(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value) {
    if (!dev || !dev->m_d || !value || parameter < 0 || parameter >= (int)(sizeof(dev->m_d->params) / sizeof(dev->m_d->params[0]))) {
        return;
    }
    if (dev->m_d->params[parameter]) {
        XVariant_delete_base(dev->m_d->params[parameter]);
    }
    dev->m_d->params[parameter] = XVariant_create_copy(value);
}

bool XModbusDevice_connectDevice(XModbusDevice* dev) {
    if (!dev) return false;
    // Call the pure virtual open() function through the vtable
    bool (*open_func)(XModbusDevice*) = XClassGetVirtualFunc(dev, EXModbusDevice_Open, bool(*)(XModbusDevice*));
    if (open_func) {
        return open_func(dev);
    }
    return false; // Should not happen if subclass is implemented correctly
}

void XModbusDevice_disconnectDevice(XModbusDevice* dev) {
    if (!dev) return;
    // Call the pure virtual close() function through the vtable
    void (*close_func)(XModbusDevice*) = XClassGetVirtualFunc(dev, EXModbusDevice_Close, void(*)(XModbusDevice*));
    if (close_func) {
        close_func(dev);
    }
}

XModbusDevice_State XModbusDevice_state(const XModbusDevice* dev) {
    return dev ? dev->m_state : XModbusDevice_UnconnectedState;
}

XModbusDevice_Error XModbusDevice_error(const XModbusDevice* dev) {
    return dev ? dev->m_error : XModbusDevice_UnknownError;
}

XString* XModbusDevice_errorString(const XModbusDevice* dev) {
    if (!dev) return XString_create_fmt_utf8("Device is NULL");
    if (dev->m_errorString) {
        return XString_create_copy(dev->m_errorString);
    }
    // Fallback to default error message
    return XString_create_fmt_utf8("%s", errorToString(dev->m_error));
}

XIODevice* XModbusDevice_device(const XModbusDevice* dev)
{
    if (!dev || !dev->m_d) return NULL;
    return dev->m_d->io_device; // <<<--- 直接返回指针
}

// ----------------- Protected API Implementation -----------------
void XModbusDevice_setState(XModbusDevice* dev, XModbusDevice_State newState) {
    if (!dev || dev->m_state == newState) return;
    dev->m_state = newState;
    // Emit signal
    XModbusDevice_stateChanged_signal(dev, newState);
}

void XModbusDevice_setError(XModbusDevice* dev, const char* errorText, XModbusDevice_Error error) {
    if (!dev) return;
    dev->m_error = error;
    if (dev->m_errorString) {
        XString_delete_base(dev->m_errorString);
        dev->m_errorString = NULL;
    }
    if (errorText) {
        dev->m_errorString = XString_create_fmt_utf8("%s", errorText);
    }
    else {
        // Use default error string
        dev->m_errorString = XString_create_fmt_utf8("%s", errorToString(error));
    }
    // Emit signal
    XModbusDevice_errorOccurred_signal(dev, error);
}

bool XModbusDevice_open_base(XModbusDevice* dev)
{
    if (ISNULL(dev, "") || ISNULL(XClassGetVtable(dev), ""))
        return false;
    return XClassGetVirtualFunc(dev, EXModbusDevice_Open, bool(*)(XModbusDevice*))(dev);
}

void XModbusDevice_close_base(XModbusDevice* dev)
{
    if (ISNULL(dev, "") || ISNULL(XClassGetVtable(dev), ""))
        return;
    XClassGetVirtualFunc(dev, EXModbusDevice_Close, void(*)(XModbusDevice*))(dev);
}

// ----------------- Signal Emitters -----------------
void* XModbusDevice_errorOccurred_signal(XModbusDevice* dev, XModbusDevice_Error error) {
    XEmitSignal(dev, XModbusDevice_errorOccurred_signal, XVariant_create_int((int)error), XVariant_delete_base, NULL, XEVENT_PRIORITY_LOWEST);
}

void* XModbusDevice_stateChanged_signal(XModbusDevice* dev, XModbusDevice_State state) {
    XEmitSignal(dev, XModbusDevice_stateChanged_signal, XVariant_create_int((int)state), XVariant_delete_base, NULL, XEVENT_PRIORITY_LOWEST);
}