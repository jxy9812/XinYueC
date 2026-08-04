#include "XModbusDevice.h"
#include "XModbusDevice_Protected.h"
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
    XVTABLE_INIT_DEFAULT(XModbusDevice)
	XCLASS_SET_CLASS_NAME_DEFAULT("XModbusDevice");
    // 继承 XObject
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
     NULL,NULL
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusDevice_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XModbusDevice);
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
    return XVariant_create_copy(XModbusDevice_connectionParameter_const(dev, parameter));
}

const XVariant* XModbusDevice_connectionParameter_const(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter)
{
    if (!dev || parameter < 0 || parameter >= XModbusDevice_ParameterCount) {
        return NULL;
    }

    if (dev->m_params[parameter]) {
        return dev->m_params[parameter];
    }
    return NULL;
}

void XModbusDevice_setConnectionParameter(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value)
{
    if (!dev || !value || parameter < 0 || parameter >= XModbusDevice_ParameterCount) {
        return;
    }

    //移动
    if (dev->m_params[parameter]) {
        XVariant_copy_base(dev->m_params[parameter], value);
    }
    else
    {
        // 设置新值（复制）
        dev->m_params[parameter] = XVariant_create_copy(value);
    }
}

void XModbusDevice_setConnectionParameter_move(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value)
{
    if (!dev || !value || parameter < 0 || parameter >= XModbusDevice_ParameterCount) {
        return;
    }

    //移动
    if (dev->m_params[parameter]) {
        XVariant_move_base(dev->m_params[parameter], value);
    }
    else
    {
        // 设置新值（复制）
        dev->m_params[parameter] = XVariant_create_move(value);
    }
}

void XModbusDevice_setConnectionParameter_ref(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value)
{
    if (!dev || !value || parameter < 0 || parameter >= XModbusDevice_ParameterCount) {
        return;
    }
    if (dev->m_params[parameter]) {
        XVariant_delete_base(dev->m_params[parameter]);
    }
    // 设置新值（复制）
    dev->m_params[parameter] = value;
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
    /*if (newState == XModbusDevice_UnconnectedState)
        XPrintf("未连接\n");*/
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
        XVarList_Create(XVar(XModbusDevice_Error, error)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusDevice_stateChanged_signal(XModbusDevice* dev, XModbusDevice_State state)
{
    XEmitSignal(dev, XModbusDevice_stateChanged_signal,
        XVarList_Create(XVar(XModbusDevice_State, state)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}