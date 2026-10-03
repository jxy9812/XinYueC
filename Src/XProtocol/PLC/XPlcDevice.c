#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON
#include "XPlcDevice.h"
#include "XPlcDevice_Protected.h"
#include "XMemory.h"
#include <string.h>

// =============== 虚函数前置声明 ===============
static void VXPlcDevice_deinit(XPlcDevice* dev);

// =============== 辅助函数 ===============
static const char* errorToString(XPlcDevice_Error err) {
    switch (err) {
    case XPlcDevice_NoError: return "No error";
    case XPlcDevice_ReadError: return "Read error";
    case XPlcDevice_WriteError: return "Write error";
    case XPlcDevice_ConnectionError: return "Connection error";
    case XPlcDevice_ConfigurationError: return "Configuration error";
    case XPlcDevice_TimeoutError: return "Timeout error";
    case XPlcDevice_ProtocolError: return "Protocol error";
    case XPlcDevice_ReplyAbortedError: return "Reply aborted";
    case XPlcDevice_UnknownError: return "Unknown error";
    case XPlcDevice_InvalidResponseError: return "Invalid response";
    default: return "Undefined error";
    }
}

// =============== 类初始化 ===============
XVtable* XPlcDevice_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlcDevice)
    // 继承 XObject
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
     NULL,NULL
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlcDevice_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XPlcDevice);
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ===============
XPlcDevice* XPlcDevice_create_ex(XMemoryType memory)
{
    XPlcDevice* dev = (XPlcDevice*)XMemory_malloc(sizeof(XPlcDevice), memory);
    if (dev) {
        XPlcDevice_init(dev);
        Set_Class_Memory(dev, memory); Set_Class_IsHeap(dev, true);
    }
    return dev;
}

void XPlcDevice_init(XPlcDevice* dev)
{
    if (!dev) return;

    // 初始化基类
    XObject_init((XObject*)dev);
    XClassGetVtable(dev) = XPlcDevice_class_init();

    // 初始化成员
    dev->m_state = XPlcDevice_UnconnectedState;
    dev->m_error = XPlcDevice_NoError;
    dev->m_errorString = NULL;
    dev->m_ioDevice = NULL;

    // 初始化参数数组
    for (int i = 0; i < XPlcDevice_ParameterCount; i++) {
        dev->m_params[i] = NULL;
    }
}

// =============== 析构函数 ===============
static void VXPlcDevice_deinit(XPlcDevice* dev)
{
    if (!dev) return;

    // 释放错误字符串
    if (dev->m_errorString) {
        XClassDelete((XClass*)dev->m_errorString);
        dev->m_errorString = NULL;
    }

    // 释放参数数组
    for (int i = 0; i < XPlcDevice_ParameterCount; i++) {
        if (dev->m_params[i]) {
            XClassDelete((XClass*)dev->m_params[i]);
            dev->m_params[i] = NULL;
        }
    }

    // 注意：m_ioDevice 由子类管理，这里不释放
    dev->m_ioDevice = NULL;

    // 调用基类析构
    XClass_Deinit_Parent(XObject, (XObject*)dev);
}

// =============== 连接参数 API ===============
XVariant* XPlcDevice_connectionParameter(const XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter)
{
    return XVariant_create_copy(XPlcDevice_connectionParameter_const(dev, parameter));
}

const XVariant* XPlcDevice_connectionParameter_const(const XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter)
{
    if (!dev || parameter < 0 || parameter >= XPlcDevice_ParameterCount) {
        return NULL;
    }

    if (dev->m_params[parameter]) {
        return dev->m_params[parameter];
    }
    return NULL;
}

void XPlcDevice_setConnectionParameter(XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter, XVariant* value)
{
    if (!dev || !value || parameter < 0 || parameter >= XPlcDevice_ParameterCount) {
        return;
    }

    //移动
    if (dev->m_params[parameter]) {
        XClassCopy(dev->m_params[parameter], value);
    }
    else
    {
        // 设置新值（复制）
        dev->m_params[parameter] = XVariant_create_copy(value);
    }
}

void XPlcDevice_setConnectionParameter_move(XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter, XVariant* value)
{
    if (!dev || !value || parameter < 0 || parameter >= XPlcDevice_ParameterCount) {
        return;
    }

    //移动
    if (dev->m_params[parameter]) {
        XClassMove(dev->m_params[parameter], value);
    }
    else
    {
        // 设置新值（复制）
        dev->m_params[parameter] = XVariant_create_move(value);
    }
}

void XPlcDevice_setConnectionParameter_ref(XPlcDevice* dev, XPlcDevice_ConnectionParameter parameter, XVariant* value)
{
    if (!dev || !value || parameter < 0 || parameter >= XPlcDevice_ParameterCount) {
        return;
    }
    if (dev->m_params[parameter]) {
        XClassDelete((XClass*)dev->m_params[parameter]);
    }
    // 设置新值（引用）
    dev->m_params[parameter] = value;
}

// =============== 连接管理 API ===============
bool XPlcDevice_connectDevice(XPlcDevice* dev)
{
    if (!dev) return false;

    // 通过虚函数表调用 open()
    bool (*open_func)(XPlcDevice*) = XClassGetVirtualFunc(dev, EXPlcDevice_Open, bool(*)(XPlcDevice*));
    if (open_func) {
        return open_func(dev);
    }
    return false;
}

void XPlcDevice_disconnectDevice(XPlcDevice* dev)
{
    if (!dev) return;

    // 通过虚函数表调用 close()
    void (*close_func)(XPlcDevice*) = XClassGetVirtualFunc(dev, EXPlcDevice_Close, void(*)(XPlcDevice*));
    if (close_func) {
        close_func(dev);
    }
}

// =============== 状态/错误查询 API ===============
XPlcDevice_State XPlcDevice_state(const XPlcDevice* dev)
{
    return dev ? (XPlcDevice_State)dev->m_state : XPlcDevice_UnconnectedState;
}

XPlcDevice_Error XPlcDevice_error(const XPlcDevice* dev)
{
    return dev ? (XPlcDevice_Error)dev->m_error : XPlcDevice_UnknownError;
}

XString* XPlcDevice_errorString(const XPlcDevice* dev)
{
    if (!dev) return XString_create_fmt_utf8("Device is NULL");

    if (dev->m_errorString) {
        return XString_create_copy(dev->m_errorString);
    }

    // 使用默认错误消息
    return XString_create_fmt_utf8("%s", errorToString((XPlcDevice_Error)dev->m_error));
}

XIODevice* XPlcDevice_device(const XPlcDevice* dev)
{
    return dev ? dev->m_ioDevice : NULL;
}

// =============== 受保护的 API (供子类使用) ===============
void XPlcDevice_setState(XPlcDevice* dev, XPlcDevice_State newState)
{
    if (!dev || (XPlcDevice_State)dev->m_state == newState) return;

    dev->m_state = (uint16_t)newState;

    // 发射状态改变信号
    XPlcDevice_stateChanged_signal(dev, newState);
}

void XPlcDevice_setError(XPlcDevice* dev, XPlcDevice_Error error, const char* errorText)
{
    if (!dev) return;

    dev->m_error = (uint16_t)error;

    // 释放旧错误字符串
    if (dev->m_errorString) {
        XClassDelete((XClass*)dev->m_errorString);
        dev->m_errorString = NULL;
    }

    // 设置新错误字符串
    if (errorText) {
        dev->m_errorString = XString_create_fmt_utf8("%s", errorText);
    }
    else {
        dev->m_errorString = XString_create_fmt_utf8("%s", errorToString(error));
    }

    // 发射错误信号（NoError 属"清除错误"，非错误事件，不发射——
    // 真机联调第 1 轮：每次 connect 都发 errorOccurred(0) 造成误报噪音）
    if (error != XPlcDevice_NoError) {
        XPlcDevice_errorOccurred_signal(dev, error);
    }
}

// =============== 虚函数调用接口 ===============
bool XPlcDevice_open_base(XPlcDevice* dev)
{
    if (!dev) return false;

    bool (*open_func)(XPlcDevice*) = XClassGetVirtualFunc(dev, EXPlcDevice_Open, bool(*)(XPlcDevice*));
    if (open_func) {
        return open_func(dev);
    }
    return false;
}

void XPlcDevice_close_base(XPlcDevice* dev)
{
    if (!dev) return;

    void (*close_func)(XPlcDevice*) = XClassGetVirtualFunc(dev, EXPlcDevice_Close, void(*)(XPlcDevice*));
    if (close_func) {
        close_func(dev);
    }
}

// =============== 信号发射 ===============
void* XPlcDevice_errorOccurred_signal(XPlcDevice* dev, XPlcDevice_Error error)
{
    XEmitSignal((XObject*)dev, XPlcDevice_errorOccurred_signal,
        XVarList_Create(XVar(XPlcDevice_Error, error)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XPlcDevice_stateChanged_signal(XPlcDevice* dev, XPlcDevice_State state)
{
    XEmitSignal((XObject*)dev, XPlcDevice_stateChanged_signal,
        XVarList_Create(XVar(XPlcDevice_State, state)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
