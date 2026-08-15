#include "XCan_config.h"
#if XPROTOCOL_ON
#if XCAN_ON
#if XCAN_DEVICE_ON
#include "XCanBusDevice.h"
#include "XCanBusDevice_Protected.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XMutex.h"
#include "XThread.h"
#include <string.h>

// =============== 虚函数前置声明 ===============
static void VXCanBusDevice_deinit(XCanBusDevice* dev);
static bool VXCanBusDevice_writeFrame(XCanBusDevice* dev, const XCanBusFrame* frame);
static XString* VXCanBusDevice_interpretErrorFrame(XCanBusDevice* dev, const XCanBusFrame* errorFrame);
static void VXCanBusDevice_resetController(XCanBusDevice* dev);
static bool VXCanBusDevice_hasBusStatus(const XCanBusDevice* dev);
static XCanBusDevice_CanBusStatus VXCanBusDevice_busStatus(XCanBusDevice* dev);
static bool VXCanBusDevice_deviceInfo(XCanBusDevice* dev, XCanBusDeviceInfo* info);

// =============== 辅助函数 ===============
static const char* errorToString(XCanBusDevice_Error err)
{
    switch (err) {
    case XCanBusDevice_NoError:             return "No error";
    case XCanBusDevice_ReadError:           return "Read error";
    case XCanBusDevice_WriteError:          return "Write error";
    case XCanBusDevice_ConnectionError:     return "Connection error";
    case XCanBusDevice_ConfigurationError:  return "Configuration error";
    case XCanBusDevice_UnknownError:        return "Unknown error";
    case XCanBusDevice_OperationError:      return "Operation error";
    case XCanBusDevice_TimeoutError:        return "Timeout error";
    default:                                return "Undefined error";
    }
}

// =============== 配置条目辅助函数 ===============
static void configEntryDeinit(void* entry)
{
    XCanBusDevice_ConfigEntry* e = (XCanBusDevice_ConfigEntry*)entry;
    if (e->m_value) {
        XVariant_delete_base((XVariant*)e->m_value);
        e->m_value = NULL;
    }
}

static void configEntryCopy(void* dest, const void* src)
{
    const XCanBusDevice_ConfigEntry* s = (const XCanBusDevice_ConfigEntry*)src;
    XCanBusDevice_ConfigEntry* d = (XCanBusDevice_ConfigEntry*)dest;
    d->m_key = s->m_key;
    if (s->m_value)
        d->m_value = XVariant_create_copy((const XVariant*)s->m_value);
    else
        d->m_value = NULL;
}

// =============== 类初始化 ===============
XVtable* XCanBusDevice_class_init()
{
    XVTABLE_INIT_DEFAULT(XCanBusDevice)
    // 继承 XObject
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCanBusDevice_deinit);
    // 重载虚函数（提供默认实现）
    XVTABLE_OVERLOAD_DEFAULT(EXCanBusDevice_WriteFrame, VXCanBusDevice_writeFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXCanBusDevice_InterpretErrorFrame, VXCanBusDevice_interpretErrorFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXCanBusDevice_ResetController, VXCanBusDevice_resetController);
    XVTABLE_OVERLOAD_DEFAULT(EXCanBusDevice_HasBusStatus, VXCanBusDevice_hasBusStatus);
    XVTABLE_OVERLOAD_DEFAULT(EXCanBusDevice_BusStatus, VXCanBusDevice_busStatus);
    XVTABLE_OVERLOAD_DEFAULT(EXCanBusDevice_DeviceInfo, VXCanBusDevice_deviceInfo);

    XCLASS_SHOW_SIZE_DEFAULT(XCanBusDevice);
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ===============
XCanBusDevice* XCanBusDevice_create_ex(XMemoryType memory)
{
    XCanBusDevice* dev = (XCanBusDevice*)XMemory_malloc(sizeof(XCanBusDevice), memory);
    if (dev) {
        XCanBusDevice_init(dev);
        Set_Class_Memory(dev, memory); Set_Class_IsHeap(dev, true);
    }
    return dev;
}

void XCanBusDevice_init(XCanBusDevice* dev)
{
    if (!dev) return;

    // 初始化基类
    XObject_init((XObject*)dev);
    XClassGetVtable(dev) = XCanBusDevice_class_init();

    // 初始化成员
    dev->m_state = XCanBusDevice_UnconnectedState;
    dev->m_error = XCanBusDevice_NoError;
    dev->m_errorString = NULL;

    // 初始化帧队列
    dev->m_incomingFrames = XVector_create(sizeof(XCanBusFrame*));
    dev->m_outgoingFrames = XVector_create(sizeof(XCanBusFrame*));

    // 初始化配置参数列表
    dev->m_configOptions = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XCanBusDevice_ConfigEntry), false);

    dev->m_waitForReceivedEntered = false;
    dev->m_waitForWrittenEntered = false;

    // 初始化接收帧队列互斥锁
    dev->m_incomingFramesGuard = XMutex_create(XLock_NonRecursive);
}

// =============== 析构函数 ===============
static void VXCanBusDevice_deinit(XCanBusDevice* dev)
{
    if (!dev) return;

    // 释放错误字符串
    if (dev->m_errorString) {
        XString_delete_base(dev->m_errorString);
        dev->m_errorString = NULL;
    }

    // 释放接收帧队列
    if (dev->m_incomingFrames) {
        size_t inCount = XVector_size_base(dev->m_incomingFrames);
        for (size_t i = 0; i < inCount; i++) {
            XCanBusFrame** frame = (XCanBusFrame**)XVector_at_base(dev->m_incomingFrames, i);
            if (frame && *frame) {
                XCanBusFrame_delete(*frame);
            }
        }
        XVector_delete_base(dev->m_incomingFrames);
        dev->m_incomingFrames = NULL;
    }

    // 释放发送帧队列
    if (dev->m_outgoingFrames) {
        size_t outCount = XVector_size_base(dev->m_outgoingFrames);
        for (size_t i = 0; i < outCount; i++) {
            XCanBusFrame** frame = (XCanBusFrame**)XVector_at_base(dev->m_outgoingFrames, i);
            if (frame && *frame) {
                XCanBusFrame_delete(*frame);
            }
        }
        XVector_delete_base(dev->m_outgoingFrames);
        dev->m_outgoingFrames = NULL;
    }

    // 释放配置参数
    if (dev->m_configOptions) {
        XVector_delete_base(dev->m_configOptions);
        dev->m_configOptions = NULL;
    }

    // 释放互斥锁
    if (dev->m_incomingFramesGuard) {
        XMutex_delete(dev->m_incomingFramesGuard);
        dev->m_incomingFramesGuard = NULL;
    }

    // 调用基类析构
    XClass_Deinit_Parent(XObject, dev);
}

// =============== 配置参数 API ===============
void XCanBusDevice_setConfigurationParameter(XCanBusDevice* dev,
    XCanBusDevice_ConfigurationKey key, void* value)
{
    if (!dev || !value) return;

    // 查找是否已存在相同键
    size_t count = XVector_size_base(dev->m_configOptions);
    for (size_t i = 0; i < count; i++) {
        XCanBusDevice_ConfigEntry* entry = (XCanBusDevice_ConfigEntry*)
            XVector_at_base(dev->m_configOptions, i);
        if (entry && entry->m_key == key) {
            // 更新现有值
            if (entry->m_value) {
                XVariant_copy_base((XVariant*)entry->m_value, (const XVariant*)value);
            } else {
                entry->m_value = XVariant_create_copy((const XVariant*)value);
            }
            return;
        }
    }

    // 添加新条目
    XCanBusDevice_ConfigEntry newEntry;
    newEntry.m_key = key;
    newEntry.m_value = XVariant_create_copy((const XVariant*)value);
    XVector_push_back_1_base(dev->m_configOptions, &newEntry);
}

void* XCanBusDevice_configurationParameter(const XCanBusDevice* dev,
    XCanBusDevice_ConfigurationKey key)
{
    if (!dev) return NULL;

    size_t count = XVector_size_base(dev->m_configOptions);
    for (size_t i = 0; i < count; i++) {
        XCanBusDevice_ConfigEntry* entry = (XCanBusDevice_ConfigEntry*)
            XVector_at_base(dev->m_configOptions, i);
        if (entry && entry->m_key == key && entry->m_value) {
            return XVariant_create_copy((const XVariant*)entry->m_value);
        }
    }
    return NULL;
}

XVector* XCanBusDevice_configurationKeys(const XCanBusDevice* dev)
{
    if (!dev) return XVector_create(sizeof(int));

    XVector* keys = XVector_create(sizeof(int));
    size_t count = XVector_size_base(dev->m_configOptions);
    for (size_t i = 0; i < count; i++) {
        XCanBusDevice_ConfigEntry* entry = (XCanBusDevice_ConfigEntry*)
            XVector_at_base(dev->m_configOptions, i);
        if (entry) {
            int key = (int)entry->m_key;
            XVector_push_back_1_base(keys, &key);
        }
    }
    return keys;
}

// =============== 帧收发 API ===============
// 默认实现：纯虚函数，返回 false
static bool VXCanBusDevice_writeFrame(XCanBusDevice* dev, const XCanBusFrame* frame)
{
    (void)dev;
    (void)frame;
    return false;
}

bool XCanBusDevice_writeFrame(XCanBusDevice* dev, const XCanBusFrame* frame)
{
    if (!dev || !frame) return false;
    bool (*write_func)(XCanBusDevice*, const XCanBusFrame*) =
        XClassGetVirtualFunc(dev, EXCanBusDevice_WriteFrame,
            bool(*)(XCanBusDevice*, const XCanBusFrame*));
    if (write_func) {
        return write_func(dev, frame);
    }
    return false;
}

// =============== Fix 1+2: readFrame/readAllFrames 添加 ConnectedState 检查 ===============
XCanBusFrame* XCanBusDevice_readFrame(XCanBusDevice* dev)
{
    if (!dev || !dev->m_incomingFrames) return NULL;

    // Qt 行为：未连接时设置 OperationError 并返回无效帧
    if (dev->m_state != XCanBusDevice_ConnectedState) {
        XCanBusDevice_setError(dev, XCanBusDevice_OperationError,
            "Cannot read frame as device is not connected.");
        return NULL;
    }

    XCanBusDevice_clearError(dev);

    // 线程安全：加锁保护接收队列
    if (dev->m_incomingFramesGuard)
        XMutex_lock(dev->m_incomingFramesGuard);

    size_t count = XVector_size_base(dev->m_incomingFrames);
    if (count == 0) {
        if (dev->m_incomingFramesGuard)
            XMutex_unlock(dev->m_incomingFramesGuard);
        return NULL;
    }

    // 取出第一个帧
    XCanBusFrame** frame = (XCanBusFrame**)XVector_at_base(dev->m_incomingFrames, 0);
    XCanBusFrame* result = *frame;
    *frame = NULL; // 防止被释放

    // 从队列中移除
    XVector_removeAt_base(dev->m_incomingFrames, 0);

    if (dev->m_incomingFramesGuard)
        XMutex_unlock(dev->m_incomingFramesGuard);

    return result;
}

XVector* XCanBusDevice_readAllFrames(XCanBusDevice* dev)
{
    if (!dev || !dev->m_incomingFrames) return XVector_create(sizeof(XCanBusFrame*));

    // Qt 行为：未连接时设置 OperationError 并返回空列表
    if (dev->m_state != XCanBusDevice_ConnectedState) {
        XCanBusDevice_setError(dev, XCanBusDevice_OperationError,
            "Cannot read frame as device is not connected.");
        return XVector_create(sizeof(XCanBusFrame*));
    }

    XCanBusDevice_clearError(dev);

    // 线程安全：加锁保护接收队列
    if (dev->m_incomingFramesGuard)
        XMutex_lock(dev->m_incomingFramesGuard);

    // 移动所有帧到新列表
    XVector* frames = XVector_create_move(dev->m_incomingFrames);
    dev->m_incomingFrames = XVector_create(sizeof(XCanBusFrame*));

    if (dev->m_incomingFramesGuard)
        XMutex_unlock(dev->m_incomingFramesGuard);

    return frames;
}

int64_t XCanBusDevice_framesAvailable(const XCanBusDevice* dev)
{
    if (!dev || !dev->m_incomingFrames) return 0;

    // 线程安全：加锁保护
    int64_t count = 0;
    if (dev->m_incomingFramesGuard)
        XMutex_lock(dev->m_incomingFramesGuard);

    count = (int64_t)XVector_size_base(dev->m_incomingFrames);

    if (dev->m_incomingFramesGuard)
        XMutex_unlock(dev->m_incomingFramesGuard);

    return count;
}

int64_t XCanBusDevice_framesToWrite(const XCanBusDevice* dev)
{
    if (!dev || !dev->m_outgoingFrames) return 0;
    return (int64_t)XVector_size_base(dev->m_outgoingFrames);
}

// =============== 控制器管理 API ===============
static void VXCanBusDevice_resetController(XCanBusDevice* dev)
{
    (void)dev;
    // 默认空实现
}

void XCanBusDevice_resetController(XCanBusDevice* dev)
{
    if (!dev) return;
    void (*reset_func)(XCanBusDevice*) =
        XClassGetVirtualFunc(dev, EXCanBusDevice_ResetController,
            void(*)(XCanBusDevice*));
    if (reset_func) {
        reset_func(dev);
    }
}

static bool VXCanBusDevice_hasBusStatus(const XCanBusDevice* dev)
{
    (void)dev;
    return false;
}

bool XCanBusDevice_hasBusStatus(const XCanBusDevice* dev)
{
    if (!dev) return false;
    bool (*has_func)(const XCanBusDevice*) =
        XClassGetVirtualFunc(dev, EXCanBusDevice_HasBusStatus,
            bool(*)(const XCanBusDevice*));
    if (has_func) {
        return has_func(dev);
    }
    return false;
}

static XCanBusDevice_CanBusStatus VXCanBusDevice_busStatus(XCanBusDevice* dev)
{
    (void)dev;
    return XCanBusDevice_CanBusStatus_Unknown;
}

XCanBusDevice_CanBusStatus XCanBusDevice_busStatus(XCanBusDevice* dev)
{
    if (!dev) return XCanBusDevice_CanBusStatus_Unknown;
    XCanBusDevice_CanBusStatus (*status_func)(XCanBusDevice*) =
        XClassGetVirtualFunc(dev, EXCanBusDevice_BusStatus,
            XCanBusDevice_CanBusStatus(*)(XCanBusDevice*));
    if (status_func) {
        return status_func(dev);
    }
    return XCanBusDevice_CanBusStatus_Unknown;
}

// =============== Fix 3: clear() 添加 ConnectedState 检查 ===============
void XCanBusDevice_clear(XCanBusDevice* dev, XCanBusDevice_Direction direction)
{
    if (!dev) return;

    // Qt 行为：未连接时设置 OperationError 并警告
    if (dev->m_state != XCanBusDevice_ConnectedState) {
        XCanBusDevice_setError(dev, XCanBusDevice_OperationError,
            "Cannot clear buffers as device is not connected.");
        return;
    }

    XCanBusDevice_clearError(dev);

    if (direction & XCanBusDevice_Input) {
        // 线程安全：加锁保护接收队列
        if (dev->m_incomingFramesGuard)
            XMutex_lock(dev->m_incomingFramesGuard);

        size_t count = XVector_size_base(dev->m_incomingFrames);
        for (size_t i = 0; i < count; i++) {
            XCanBusFrame** frame = (XCanBusFrame**)XVector_at_base(dev->m_incomingFrames, i);
            if (frame && *frame) {
                XCanBusFrame_delete(*frame);
            }
        }
        XVector_clear_base(dev->m_incomingFrames);

        if (dev->m_incomingFramesGuard)
            XMutex_unlock(dev->m_incomingFramesGuard);
    }

    if (direction & XCanBusDevice_Output) {
        size_t count = XVector_size_base(dev->m_outgoingFrames);
        for (size_t i = 0; i < count; i++) {
            XCanBusFrame** frame = (XCanBusFrame**)XVector_at_base(dev->m_outgoingFrames, i);
            if (frame && *frame) {
                XCanBusFrame_delete(*frame);
            }
        }
        XVector_clear_base(dev->m_outgoingFrames);
    }
}

// =============== Fix 4: waitForFramesWritten/Received 添加递归保护和状态检查 ===============
bool XCanBusDevice_waitForFramesWritten(XCanBusDevice* dev, int msecs)
{
    if (!dev) return false;

    // Qt 行为：防止递归调用
    if (dev->m_waitForWrittenEntered) {
        XCanBusDevice_setError(dev, XCanBusDevice_OperationError,
            "QCanBusDevice::waitForFramesWritten() must not be called recursively.");
        return false;
    }

    // Qt 行为：检查 ConnectedState
    if (dev->m_state != XCanBusDevice_ConnectedState) {
        XCanBusDevice_setError(dev, XCanBusDevice_OperationError,
            "Cannot wait for frames written as device is not connected.");
        return false;
    }

    if (XCanBusDevice_framesToWrite(dev) == 0)
        return false;

    dev->m_waitForWrittenEntered = true;

    // 简单轮询实现
    int elapsed = 0;
    bool result = false;
    while (elapsed < msecs || msecs < 0) {
        if (XCanBusDevice_framesToWrite(dev) == 0) {
            result = true;
            break;
        }
        XThread_msleep(10);
        elapsed += 10;
    }

    dev->m_waitForWrittenEntered = false;

    if (!result) {
        XCanBusDevice_setError(dev, XCanBusDevice_TimeoutError, NULL);
    } else {
        XCanBusDevice_clearError(dev);
    }

    return result;
}

bool XCanBusDevice_waitForFramesReceived(XCanBusDevice* dev, int msecs)
{
    if (!dev) return false;

    // Qt 行为：防止递归调用
    if (dev->m_waitForReceivedEntered) {
        XCanBusDevice_setError(dev, XCanBusDevice_OperationError,
            "QCanBusDevice::waitForFramesReceived() must not be called recursively.");
        return false;
    }

    // Qt 行为：检查 ConnectedState
    if (dev->m_state != XCanBusDevice_ConnectedState) {
        XCanBusDevice_setError(dev, XCanBusDevice_OperationError,
            "Cannot wait for frames received as device is not connected.");
        return false;
    }

    dev->m_waitForReceivedEntered = true;

    // 简单轮询实现
    int elapsed = 0;
    bool result = false;
    while (elapsed < msecs || msecs < 0) {
        if (XCanBusDevice_framesAvailable(dev) > 0) {
            result = true;
            break;
        }
        XThread_msleep(10);
        elapsed += 10;
    }

    dev->m_waitForReceivedEntered = false;

    if (!result) {
        XCanBusDevice_setError(dev, XCanBusDevice_TimeoutError, NULL);
    } else {
        XCanBusDevice_clearError(dev);
    }

    return result;
}

// =============== 连接管理 API ===============
bool XCanBusDevice_connectDevice(XCanBusDevice* dev)
{
    if (!dev) return false;

    if (dev->m_state != XCanBusDevice_UnconnectedState) {
        XCanBusDevice_setError(dev, XCanBusDevice_ConnectionError,
            "Can not connect an already connected device.");
        return false;
    }

    XCanBusDevice_setState(dev, XCanBusDevice_ConnectingState);

    // 调用 open() 虚函数
    bool (*open_func)(XCanBusDevice*) = XClassGetVirtualFunc(dev,
        EXCanBusDevice_Open, bool(*)(XCanBusDevice*));
    if (!open_func || !open_func(dev)) {
        XCanBusDevice_setState(dev, XCanBusDevice_UnconnectedState);
        return false;
    }

    XCanBusDevice_clearError(dev);
    // Connected 由后端设置 -> 可能通过事件循环延迟
    return true;
}

void XCanBusDevice_disconnectDevice(XCanBusDevice* dev)
{
    if (!dev) return;

    if (dev->m_state == XCanBusDevice_UnconnectedState ||
        dev->m_state == XCanBusDevice_ClosingState) {
        return;
    }

    XCanBusDevice_setState(dev, XCanBusDevice_ClosingState);

    // 调用 close() 虚函数
    void (*close_func)(XCanBusDevice*) = XClassGetVirtualFunc(dev,
        EXCanBusDevice_Close, void(*)(XCanBusDevice*));
    if (close_func) {
        close_func(dev);
    }
}

// =============== 状态/错误查询 API ===============
XCanBusDevice_State XCanBusDevice_state(const XCanBusDevice* dev)
{
    return dev ? dev->m_state : XCanBusDevice_UnconnectedState;
}

XCanBusDevice_Error XCanBusDevice_error(const XCanBusDevice* dev)
{
    return dev ? dev->m_error : XCanBusDevice_UnknownError;
}

// =============== Fix 6: errorString() NoError 返回空字符串 ===============
XString* XCanBusDevice_errorString(const XCanBusDevice* dev)
{
    if (!dev) return XString_create_utf8("Device is NULL");

    // Qt 行为：NoError 时返回空字符串
    if (dev->m_error == XCanBusDevice_NoError)
        return XString_create();

    if (dev->m_errorString) {
        return XString_create_copy(dev->m_errorString);
    }

    return XString_create_fmt_utf8("%s", errorToString(dev->m_error));
}

// 默认实现：纯虚函数
static XString* VXCanBusDevice_interpretErrorFrame(XCanBusDevice* dev, const XCanBusFrame* errorFrame)
{
    (void)dev;
    (void)errorFrame;
    return XString_create();
}

XString* XCanBusDevice_interpretErrorFrame(XCanBusDevice* dev, const XCanBusFrame* errorFrame)
{
    if (!dev || !errorFrame) return XString_create();
    XString* (*interpret_func)(XCanBusDevice*, const XCanBusFrame*) =
        XClassGetVirtualFunc(dev, EXCanBusDevice_InterpretErrorFrame,
            XString*(*)(XCanBusDevice*, const XCanBusFrame*));
    if (interpret_func) {
        return interpret_func(dev, errorFrame);
    }
    return XString_create();
}

static bool VXCanBusDevice_deviceInfo(XCanBusDevice* dev, XCanBusDeviceInfo* info)
{
    (void)dev;
    if (info) {
        XCanBusDeviceInfo_init(info);
    }
    return true;
}

bool XCanBusDevice_deviceInfo(XCanBusDevice* dev, XCanBusDeviceInfo* info)
{
    if (!dev || !info) return false;
    bool (*info_func)(XCanBusDevice*, XCanBusDeviceInfo*) =
        XClassGetVirtualFunc(dev, EXCanBusDevice_DeviceInfo,
            bool(*)(XCanBusDevice*, XCanBusDeviceInfo*));
    if (info_func) {
        return info_func(dev, info);
    }
    return false;
}

// =============== 受保护的 API（供子类使用）===============
void XCanBusDevice_setState(XCanBusDevice* dev, XCanBusDevice_State newState)
{
    if (!dev || dev->m_state == newState) return;

    dev->m_state = newState;

    // 发射状态改变信号
    XCanBusDevice_stateChanged_signal(dev, newState);
}

void XCanBusDevice_setError(XCanBusDevice* dev, XCanBusDevice_Error error, const char* errorText)
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
    } else {
        dev->m_errorString = XString_create_fmt_utf8("%s", errorToString(error));
    }

    // 发射错误信号
    XCanBusDevice_errorOccurred_signal(dev, error);
}

void XCanBusDevice_clearError(XCanBusDevice* dev)
{
    if (!dev) return;
    dev->m_error = XCanBusDevice_NoError;
    if (dev->m_errorString) {
        XString_delete_base(dev->m_errorString);
        dev->m_errorString = NULL;
    }
}

// =============== Fix 5: enqueueReceivedFrames 添加线程安全保护 ===============
void XCanBusDevice_enqueueReceivedFrames(XCanBusDevice* dev, const XVector* newFrames)
{
    if (!dev || !newFrames || !dev->m_incomingFrames) return;

    // 线程安全：加锁保护接收队列
    if (dev->m_incomingFramesGuard)
        XMutex_lock(dev->m_incomingFramesGuard);

    size_t count = XVector_size_base(newFrames);
    for (size_t i = 0; i < count; i++) {
        XCanBusFrame** frame = (XCanBusFrame**)XVector_at_base(newFrames, i);
        if (frame && *frame) {
            XCanBusFrame* copy = XCanBusFrame_create_copy(*frame);
            if (copy) {
                XVector_push_back_1_base(dev->m_incomingFrames, &copy);
            }
        }
    }

    if (dev->m_incomingFramesGuard)
        XMutex_unlock(dev->m_incomingFramesGuard);

    // 发射帧接收信号
    XCanBusDevice_framesReceived_signal(dev);
}

void XCanBusDevice_enqueueOutgoingFrame(XCanBusDevice* dev, const XCanBusFrame* newFrame)
{
    if (!dev || !newFrame || !dev->m_outgoingFrames) return;

    XCanBusFrame* copy = XCanBusFrame_create_copy(newFrame);
    if (copy) {
        XVector_push_back_1_base(dev->m_outgoingFrames, &copy);
    }
}

XCanBusFrame* XCanBusDevice_dequeueOutgoingFrame(XCanBusDevice* dev)
{
    if (!dev || !dev->m_outgoingFrames) return NULL;

    size_t count = XVector_size_base(dev->m_outgoingFrames);
    if (count == 0) return NULL;

    XCanBusFrame** frame = (XCanBusFrame**)XVector_at_base(dev->m_outgoingFrames, 0);
    XCanBusFrame* result = *frame;
    *frame = NULL;
    XVector_removeAt_base(dev->m_outgoingFrames, 0);
    return result;
}

bool XCanBusDevice_hasOutgoingFrames(const XCanBusDevice* dev)
{
    return dev && dev->m_outgoingFrames &&
        XVector_size_base(dev->m_outgoingFrames) > 0;
}

// =============== 静态工厂方法：创建设备信息 ===============
void XCanBusDevice_createDeviceInfo(XCanBusDeviceInfo* info,
    const char* plugin, const char* name,
    bool isVirtual, bool isFlexibleDataRateCapable)
{
    if (!info) return;
    XCanBusDeviceInfo_init(info);
    if (plugin)  info->m_plugin = XString_create_utf8(plugin);
    if (name)    info->m_name = XString_create_utf8(name);
    info->m_isVirtual = isVirtual;
    info->m_hasFlexibleDataRate = isFlexibleDataRateCapable;
}

void XCanBusDevice_createDeviceInfo_full(XCanBusDeviceInfo* info,
    const char* plugin, const char* name,
    const char* serialNumber, const char* description,
    const char* alias, int channel,
    bool isVirtual, bool isFlexibleDataRateCapable)
{
    if (!info) return;
    XCanBusDeviceInfo_init(info);
    if (plugin)        info->m_plugin = XString_create_utf8(plugin);
    if (name)          info->m_name = XString_create_utf8(name);
    if (serialNumber)  info->m_serialNumber = XString_create_utf8(serialNumber);
    if (description)   info->m_description = XString_create_utf8(description);
    if (alias)         info->m_alias = XString_create_utf8(alias);
    info->m_channel = channel;
    info->m_isVirtual = isVirtual;
    info->m_hasFlexibleDataRate = isFlexibleDataRateCapable;
}

// =============== 虚函数调用接口 ===============
bool XCanBusDevice_open_base(XCanBusDevice* dev)
{
    if (!dev) return false;
    bool (*open_func)(XCanBusDevice*) = XClassGetVirtualFunc(dev,
        EXCanBusDevice_Open, bool(*)(XCanBusDevice*));
    if (open_func) return open_func(dev);
    return false;
}

void XCanBusDevice_close_base(XCanBusDevice* dev)
{
    if (!dev) return;
    void (*close_func)(XCanBusDevice*) = XClassGetVirtualFunc(dev,
        EXCanBusDevice_Close, void(*)(XCanBusDevice*));
    if (close_func) close_func(dev);
}

// =============== 信号发射 ===============
void* XCanBusDevice_errorOccurred_signal(XCanBusDevice* dev, XCanBusDevice_Error error)
{
    XEmitSignal(dev, XCanBusDevice_errorOccurred_signal,
        XVarList_Create(XVar(XCanBusDevice_Error, error)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCanBusDevice_framesReceived_signal(XCanBusDevice* dev)
{
    XEmitSignal(dev, XCanBusDevice_framesReceived_signal,
        NULL, NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCanBusDevice_framesWritten_signal(XCanBusDevice* dev, int64_t framesCount)
{
    XEmitSignal(dev, XCanBusDevice_framesWritten_signal,
        XVarList_Create(XVar(int64_t, framesCount)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XCanBusDevice_stateChanged_signal(XCanBusDevice* dev, XCanBusDevice_State state)
{
    XEmitSignal(dev, XCanBusDevice_stateChanged_signal,
        XVarList_Create(XVar(XCanBusDevice_State, state)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XCAN_DEVICE_ON */
#endif /* XCAN_ON */
#endif /* XPROTOCOL_ON */
