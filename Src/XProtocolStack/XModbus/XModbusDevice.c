#include "XModbusDevice.h"
#include "XMemory.h"
#include <string.h>

// =============== 虚函数前置声明 ===============
static void VXModbusDevice_deinit(XModbusDevice* dev);

// =============== 辅助函数 ===============
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

// =============== 类初始化 ===============
XVtable* XModbusDevice_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusDevice))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承 XObject
        XVTABLE_INHERIT_XCLASS(XObject);

    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusDevice_deinit);

#if SHOWCONTAINERSIZE
    printf("XModbusDevice vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ===============
XModbusDevice* XModbusDevice_create()
{
    XModbusDevice* dev = (XModbusDevice*)XMalloc_System(sizeof(XModbusDevice));
    if (dev) {
        XModbusDevice_init(dev);
        Set_Class_MemoryFree(dev, XFree_System);
    }
    return dev;
}

void XModbusDevice_init(XModbusDevice* dev)
{
    if (!dev) return;

    // 初始化基类
    XObject_init((XObject*)dev);
    XClassGetVtable(dev) = XModbusDevice_class_init();

    // 初始化成员
    dev->m_state = XModbusDevice_UnconnectedState;
    dev->m_error = XModbusDevice_NoError;
    dev->m_errorString = NULL;
    dev->m_ioDevice = NULL;

    // 初始化参数数组
    for (int i = 0; i < XModbusDevice_ParameterCount; i++) {
        dev->m_params[i] = NULL;
    }
}

// =============== 析构函数 ===============
static void VXModbusDevice_deinit(XModbusDevice* dev)
{
    if (!dev) return;

    // 释放错误字符串
    if (dev->m_errorString) {
        XString_delete_base(dev->m_errorString);
        dev->m_errorString = NULL;
    }

    // 释放参数数组
    for (int i = 0; i < XModbusDevice_ParameterCount; i++) {
        if (dev->m_params[i]) {
            XVariant_delete_base(dev->m_params[i]);
            dev->m_params[i] = NULL;
        }
    }

    // 注意：m_ioDevice 由子类管理，这里不释放
    dev->m_ioDevice = NULL;

    // 调用基类析构
    XClass_Deinit_Parent(XObject, dev);
}

// =============== 连接参数 API ===============
XVariant* XModbusDevice_connectionParameter(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter)
{
    if (!dev || parameter < 0 || parameter >= XModbusDevice_ParameterCount) {
        return NULL;
    }

    if (dev->m_params[parameter]) {
        return XVariant_create_copy(dev->m_params[parameter]);
    }
    return NULL;
}

void XModbusDevice_setConnectionParameter(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value)
{
    if (!dev || !value || parameter < 0 || parameter >= XModbusDevice_ParameterCount) {
        return;
    }

    // 释放旧值
    if (dev->m_params[parameter]) {
        XVariant_delete_base(dev->m_params[parameter]);
    }

    // 设置新值（复制）
    dev->m_params[parameter] = XVariant_create_copy(value);
}

// =============== 连接管理 API ===============
bool XModbusDevice_connectDevice(XModbusDevice* dev)
{
    if (!dev) return false;

    // 通过虚函数表调用 open()
    bool (*open_func)(XModbusDevice*) = XClassGetVirtualFunc(dev, EXModbusDevice_Open, bool(*)(XModbusDevice*));
    if (open_func) {
        return open_func(dev);
    }
    return false;
}

void XModbusDevice_disconnectDevice(XModbusDevice* dev)
{
    if (!dev) return;

    // 通过虚函数表调用 close()
    void (*close_func)(XModbusDevice*) = XClassGetVirtualFunc(dev, EXModbusDevice_Close, void(*)(XModbusDevice*));
    if (close_func) {
        close_func(dev);
    }
}

// =============== 状态/错误查询 API ===============
XModbusDevice_State XModbusDevice_state(const XModbusDevice* dev)
{
    return dev ? dev->m_state : XModbusDevice_UnconnectedState;
}

XModbusDevice_Error XModbusDevice_error(const XModbusDevice* dev)
{
    return dev ? dev->m_error : XModbusDevice_UnknownError;
}

XString* XModbusDevice_errorString(const XModbusDevice* dev)
{
    if (!dev) return XString_create_fmt_utf8("Device is NULL");

    if (dev->m_errorString) {
        return XString_create_copy(dev->m_errorString);
    }

    // 使用默认错误消息
    return XString_create_fmt_utf8("%s", errorToString(dev->m_error));
}

XIODevice* XModbusDevice_device(const XModbusDevice* dev)
{
    return dev ? dev->m_ioDevice : NULL;
}

// =============== 受保护的 API (供子类使用) ===============
void XModbusDevice_setState(XModbusDevice* dev, XModbusDevice_State newState)
{
    if (!dev || dev->m_state == newState) return;

    dev->m_state = newState;

    // 发射状态改变信号
    XModbusDevice_stateChanged_signal(dev, newState);
}

void XModbusDevice_setError(XModbusDevice* dev, XModbusDevice_Error error, const char* errorText)
{
    if (!dev) return;

    dev->m_error = error;

    // 释放旧错误字符串
    if (dev->m_errorString) {
        XString_delete_base(dev->m_errorString);
        dev->m_errorString = NULL;
    }

    // 设置新错误字符串
    if (errorText) {
        dev->m_errorString = XString_create_fmt_utf8("%s", errorText);
    }
    else {
        dev->m_errorString = XString_create_fmt_utf8("%s", errorToString(error));
    }

    // 发射错误信号
    XModbusDevice_errorOccurred_signal(dev, error);
}

// =============== 虚函数调用接口 ===============
bool XModbusDevice_open_base(XModbusDevice* dev)
{
    if (!dev) return false;

    bool (*open_func)(XModbusDevice*) = XClassGetVirtualFunc(dev, EXModbusDevice_Open, bool(*)(XModbusDevice*));
    if (open_func) {
        return open_func(dev);
    }
    return false;
}

void XModbusDevice_close_base(XModbusDevice* dev)
{
    if (!dev) return;

    void (*close_func)(XModbusDevice*) = XClassGetVirtualFunc(dev, EXModbusDevice_Close, void(*)(XModbusDevice*));
    if (close_func) {
        close_func(dev);
    }
}

// =============== 信号发射 ===============
void* XModbusDevice_errorOccurred_signal(XModbusDevice* dev, XModbusDevice_Error error)
{
    XEmitSignal(dev, XModbusDevice_errorOccurred_signal,
        XVariant_create_int((int)error), XVariant_delete_base,
        NULL, XEVENT_PRIORITY_LOWEST);
    return NULL;
}

void* XModbusDevice_stateChanged_signal(XModbusDevice* dev, XModbusDevice_State state)
{
    XEmitSignal(dev, XModbusDevice_stateChanged_signal,
        XVariant_create_int((int)state), XVariant_delete_base,
        NULL, XEVENT_PRIORITY_LOWEST);
    return NULL;
}