#include "XDeviceTimer.h"
#include "XDeviceTimerPrivate.h"
#include "XAbstractEventDispatcher.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XFileDescriptor.h"
#include "XHrTimerGroup.h"
#include "XMemory.h"
#include "XTimerData.h"
#include "XTimeWheelGroup.h"
#include "XVarList.h"
#include "XVariant.h"
#include <string.h>

static XDeviceTimer g_deviceTimer;
static XDeviceTimerOpenOptions g_defaultTimerOptions = {
    { 0 }, NULL, NULL, 0, XTimerType_CoarseTimer, 0, 0
};
static bool g_deviceTimerInitialized = false;
static bool g_deviceTimerRegistered = false;

static XDeviceContext* VXDeviceTimer_open(XDevice* self,
    const XDeviceOpenOptions* options, int* error);
static void VXDeviceTimer_close(XDevice* self, XDeviceContext* handle);
static bool VXDeviceTimer_setProperty(XDevice* self, XDeviceContext* handle,
    uint32_t property, const XVariant* value);
static bool VXDeviceTimer_control(XDevice* self, XDeviceContext* handle,
    uint32_t command, const XVarList* in, XVarList* out);

static XDeviceTimerContext* timerContext(XDeviceContext* handle)
{
    return (XDeviceTimerContext*)handle;
}

static bool timerSetError(XDeviceTimerContext* context, XDeviceError error)
{
    if (context) context->m_base.m_lastError = (int16_t)error;
    return false;
}

static bool timerTypeValid(XTimerType type)
{
    return type == XTimerType_PreciseTimer || type == XTimerType_CoarseTimer ||
        type == XTimerType_VeryCoarseTimer;
}

static void timerExpired(void* userData, XTimerData* timer)
{
    XDeviceTimerContext* context = (XDeviceTimerContext*)userData;
    bool singleShot;
    XDeviceTimerCallback callback;
    void* callbackData;

    if (!context || context->m_base.m_state == (uint16_t)XDeviceState_Closing)
        return;

    singleShot = context->m_singleShot != 0;
    callback = context->m_callback;
    callbackData = context->m_userData;
    if (singleShot) {
        context->m_active = false;
        /* 时间轮会在单次回调后回收节点；红黑树需要先标记删除，
         * 由当前 tick 的收尾阶段释放节点。 */
        if ((XTimerType)context->m_backendType == XTimerType_PreciseTimer)
            (void)XDeviceTimer_cancel(context->m_dispatcher,
                (XTimerType)context->m_backendType, context->m_backendHandle);
        context->m_backendHandle = NULL;
    }
    if (callback) {
        (void)timer;
        callback(callbackData);
    }
}

static bool timerStop(XDeviceTimerContext* context)
{
    bool cancelled = true;
    if (!context) return false;
    if (context->m_backendHandle) {
        cancelled = XDeviceTimer_cancel(context->m_dispatcher,
            (XTimerType)context->m_backendType, context->m_backendHandle);
        context->m_backendHandle = NULL;
    }
    context->m_active = false;
    return cancelled;
}

static bool timerStart(XDeviceTimerContext* context)
{
    XTimerData data;
    XTimerType actualType;
    XHandle handle = NULL;

    if (!context || !context->m_dispatcher || !context->m_callback ||
        context->m_intervalNs <= 0 ||
        !timerTypeValid((XTimerType)context->m_timerType))
        return false;
    if (context->m_active) return true;

    /* Precise 单次节点会在触发后保持一个可取消的脱离状态；重启前先把它
     * 从上一轮生命周期中释放。 */
    if (context->m_backendHandle && !timerStop(context))
        return false;

    memset(&data, 0, sizeof(data));
    XTimerData_setTimerId(&data, (XTimerId)context->m_base.m_fd);
    XTimerData_setTimerCallback(&data, timerExpired);
    XTimerData_setUserData(&data, context);
    XTimerData_setSingleShot(&data, context->m_singleShot != 0);
    XTimerData_setAutoDelete(&data, false);

    if (!XDeviceTimer_schedule(context->m_dispatcher, &data, context->m_intervalNs,
            (XTimerType)context->m_timerType, &actualType, &handle) || !handle)
        return false;

    context->m_backendType = (uint32_t)actualType;
    context->m_backendHandle = handle;
    context->m_active = true;
    context->m_base.m_lastError = (int16_t)XDeviceError_None;
    XAbstractEventDispatcher_wakeUp_base(context->m_dispatcher);
    return true;
}

XVtable* XDeviceTimer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceTimer)
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceTimer_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceTimer_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_SetProperty, VXDeviceTimer_setProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceTimer_control);
    XCLASS_SET_CLASS_NAME_DEFAULT("timer");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceTimer);
    return XVTABLE_DEFAULT;
}

void XDeviceTimer_init(XDeviceTimer* self)
{
    if (!self) return;
    memset(((XDevice*)self) + 1, 0, sizeof(*self) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceTimer);
    self->m_base.m_type = XDeviceType_Timer;
    self->m_base.m_capabilities = XDeviceCap_Async;
    self->m_base.m_defaultOpenOptions = &g_defaultTimerOptions.m_base;
    self->m_timeWheel = XTimeWheelGroup_global();
}

XDeviceTimer* XDeviceTimer_create(void)
{
    XDeviceTimer* self = (XDeviceTimer*)XClass_Malloc(XDeviceTimer);
    if (!self) return NULL;
    XDeviceTimer_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

bool XDeviceTimer_register(void)
{
    if (!g_deviceTimerInitialized) {
        XDeviceTimer_init(&g_deviceTimer);
        g_deviceTimerInitialized = true;
    }
    if (g_deviceTimerRegistered) return true;
    if (!g_deviceTimer.m_timeWheel || !XDevice_register(&g_deviceTimer.m_base))
        return false;
    g_deviceTimerRegistered = true;
    return true;
}

XTimeWheelGroup* XDeviceTimer_timeWheel(void)
{
    if (!g_deviceTimerInitialized && !XDeviceTimer_register())
        return NULL;
    return g_deviceTimer.m_timeWheel;
}

bool XDeviceTimer_schedule(XAbstractEventDispatcher* dispatcher, XTimerData* data,
    XDuration intervalNs, XTimerType requestedType, XTimerType* actualType,
    XHandle* backendHandle)
{
    XTimeWheelGroup* wheel;
    uint64_t intervalMs;
    XHandle handle;

    if (!dispatcher || !dispatcher->d_ptr || !data || intervalNs <= 0 ||
        !actualType || !backendHandle || !timerTypeValid(requestedType))
        return false;
    *backendHandle = NULL;

    if (requestedType != XTimerType_PreciseTimer) {
        intervalMs = ((uint64_t)intervalNs + 999999ULL) / 1000000ULL;
        if (requestedType == XTimerType_VeryCoarseTimer)
            intervalMs = ((intervalMs + 999ULL) / 1000ULL) * 1000ULL;
        wheel = XDeviceTimer_timeWheel();
        if (wheel && intervalMs > 0 && intervalMs < XTimeWheelGroup_max_time(
                (XTimerGroupBase*)wheel)) {
            XTimerData_setTimeout(data, 0);
            XTimerData_setInterval(data, (size_t)intervalMs);
            handle = XTimeWheelGroup_addTimerMs_base((XTimerGroupBase*)wheel, *data);
            if (handle) {
                *actualType = requestedType;
                *backendHandle = handle;
                return true;
            }
        }
    }

    if (!dispatcher->d_ptr->m_hrtimerGroup) {
        dispatcher->d_ptr->m_hrtimerGroup = XHrTimerGroup_create(1);
        if (!dispatcher->d_ptr->m_hrtimerGroup) return false;
        XHrTimerGroup_setHighResTimeFunc((XTimerGroupBase*)dispatcher->d_ptr->m_hrtimerGroup,
            XDateTime_currentNSecsSinceEpoch);
    }
    XTimerData_setTimeout(data, 0);
    XTimerData_setInterval(data, (size_t)intervalNs);
    handle = XHrTimerGroup_addTimerNs_base(
        (XTimerGroupBase*)dispatcher->d_ptr->m_hrtimerGroup, *data);
    if (!handle) return false;
    *actualType = XTimerType_PreciseTimer;
    *backendHandle = handle;
    return true;
}

bool XDeviceTimer_cancel(XAbstractEventDispatcher* dispatcher, XTimerType actualType,
    XHandle backendHandle)
{
    XTimeWheelGroup* wheel;
    if (!dispatcher || !backendHandle) return false;
    if (actualType == XTimerType_PreciseTimer) {
        return dispatcher->d_ptr && dispatcher->d_ptr->m_hrtimerGroup &&
            XHrTimerGroup_removeTimer_base((XTimerGroupBase*)dispatcher->d_ptr->m_hrtimerGroup,
                backendHandle);
    }
    wheel = XDeviceTimer_timeWheel();
    return wheel && XTimeWheelGroup_removeTimer_base((XTimerGroupBase*)wheel,
        backendHandle);
}

void XDeviceTimer_process(XAbstractEventDispatcher* dispatcher, bool processGlobalWheel)
{
    XTimeWheelGroup* wheel;
    if (!dispatcher || !dispatcher->d_ptr) return;
    if (processGlobalWheel) {
        wheel = XDeviceTimer_timeWheel();
        if (wheel && XTimeWheelGroup_count(wheel))
            XTimeWheelGroup_handler_base((XTimerGroupBase*)wheel);
    }
    if (dispatcher->d_ptr->m_hrtimerGroup)
        XHrTimerGroup_handler_base((XTimerGroupBase*)dispatcher->d_ptr->m_hrtimerGroup);
}

uint64_t XDeviceTimer_nextPreciseDeadline(const XAbstractEventDispatcher* dispatcher)
{
    if (!dispatcher || !dispatcher->d_ptr || !dispatcher->d_ptr->m_hrtimerGroup)
        return UINT64_MAX;
    return XHrTimerGroup_getNextExpireTime(dispatcher->d_ptr->m_hrtimerGroup);
}

void XDeviceTimer_releaseDispatcher(XAbstractEventDispatcher* dispatcher)
{
    if (!dispatcher || !dispatcher->d_ptr || !dispatcher->d_ptr->m_hrtimerGroup)
        return;
    XClass_delete_base((XClass*)dispatcher->d_ptr->m_hrtimerGroup);
    dispatcher->d_ptr->m_hrtimerGroup = NULL;
}

static XDeviceContext* VXDeviceTimer_open(XDevice* self,
    const XDeviceOpenOptions* options, int* error)
{
    const XDeviceTimerOpenOptions* timerOptions =
        (const XDeviceTimerOpenOptions*)options;
    XDeviceTimerContext* context;
    XAbstractEventDispatcher* dispatcher;
    (void)self;

    if (!timerOptions || !timerTypeValid((XTimerType)timerOptions->m_timerType)) {
        if (error) *error = (int)XDeviceError_InvalidArgument;
        return NULL;
    }

    dispatcher = XCoreApplication_eventDispatcher();
    if (!dispatcher) {
        if (error) *error = (int)XDeviceError_NotOpen;
        return NULL;
    }
    context = (XDeviceTimerContext*)XCalloc_System(1, sizeof(*context));
    if (!context) {
        if (error) *error = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    context->m_base.m_fd = XFd_alloc(XFD_TYPE_CLASS, &context->m_base, NULL);
    if (context->m_base.m_fd == XFD_INVALID) {
        XFree_System(context);
        if (error) *error = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    context->m_dispatcher = dispatcher;
    context->m_callback = timerOptions->m_callback;
    context->m_userData = timerOptions->m_userData;
    context->m_intervalNs = timerOptions->m_intervalNs;
    context->m_timerType = (uint32_t)timerOptions->m_timerType;
    context->m_backendType = (uint32_t)timerOptions->m_timerType;
    context->m_singleShot = timerOptions->m_singleShot ? 1u : 0u;
    context->m_base.m_state = (uint16_t)XDeviceState_Opening;
    context->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Async;
    context->m_base.m_lastError = (int16_t)XDeviceError_None;
    if (error) *error = (int)XDeviceError_None;
    return &context->m_base;
}

static void VXDeviceTimer_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceTimerContext* context = timerContext(handle);
    (void)self;
    if (!context) return;
    (void)timerStop(context);
    XFree_System(context);
}

static bool timerVariantInteger(const XVariant* value)
{
    int type = XVariant_type((XVariant*)value);
    return type == XVariantType_Uint8 || type == XVariantType_Uint16 ||
        type == XVariantType_Uint32 || type == XVariantType_Uint64 ||
        type == XVariantType_Int8 || type == XVariantType_Int16 ||
        type == XVariantType_Int32 || type == XVariantType_Int64 ||
        type == XVariantType_Int || type == XVariantType_Size_t;
}

static bool VXDeviceTimer_setProperty(XDevice* self, XDeviceContext* handle,
    uint32_t property, const XVariant* value)
{
    XDeviceTimerContext* context = timerContext(handle);
    XDeviceTimerCallback callback;
    union {
        void* data;
        XDeviceTimerCallback callback;
    } callbackValue;
    XDuration intervalNs;
    XTimerType timerType;
    bool wasActive;
    (void)self;

    if (!context || !value)
        return timerSetError(context, XDeviceError_InvalidArgument);

    switch (property) {
    case XDeviceTimerProperty_TimerType:
        if (!timerVariantInteger(value))
            return timerSetError(context, XDeviceError_InvalidArgument);
        timerType = (XTimerType)XVariant_toInt(value);
        if (!timerTypeValid(timerType))
            return timerSetError(context, XDeviceError_InvalidArgument);
        wasActive = context->m_active != 0;
        if (wasActive && !timerStop(context))
            return timerSetError(context, XDeviceError_IoFail);
        context->m_timerType = (uint32_t)timerType;
        if (wasActive && !timerStart(context))
            return timerSetError(context, XDeviceError_IoFail);
        break;
    case XDeviceTimerProperty_IntervalNs:
        if (!timerVariantInteger(value))
            return timerSetError(context, XDeviceError_InvalidArgument);
        intervalNs = (XDuration)XVariant_toInt64(value);
        if (intervalNs <= 0)
            return timerSetError(context, XDeviceError_InvalidArgument);
        wasActive = context->m_active != 0;
        if (wasActive && !timerStop(context))
            return timerSetError(context, XDeviceError_IoFail);
        context->m_intervalNs = intervalNs;
        if (wasActive && !timerStart(context))
            return timerSetError(context, XDeviceError_IoFail);
        break;
    case XDeviceTimerProperty_Callback:
        if (XVariant_type((XVariant*)value) != XVariantType_Ptr)
            return timerSetError(context, XDeviceError_InvalidArgument);
        callbackValue.data = XVariant_toPtr(value);
        callback = callbackValue.callback;
        if (!callback)
            return timerSetError(context, XDeviceError_InvalidArgument);
        wasActive = context->m_active != 0;
        if (wasActive && !timerStop(context))
            return timerSetError(context, XDeviceError_IoFail);
        context->m_callback = callback;
        if (wasActive && !timerStart(context))
            return timerSetError(context, XDeviceError_IoFail);
        break;
    case XDeviceTimerProperty_UserData:
        if (XVariant_type((XVariant*)value) != XVariantType_Ptr)
            return timerSetError(context, XDeviceError_InvalidArgument);
        wasActive = context->m_active != 0;
        if (wasActive && !timerStop(context))
            return timerSetError(context, XDeviceError_IoFail);
        context->m_userData = XVariant_toPtr(value);
        if (wasActive && !timerStart(context))
            return timerSetError(context, XDeviceError_IoFail);
        break;
    default:
        return timerSetError(context, XDeviceError_NotSupported);
    }
    context->m_base.m_lastError = (int16_t)XDeviceError_None;
    return true;
}

static bool VXDeviceTimer_control(XDevice* self, XDeviceContext* handle,
    uint32_t command, const XVarList* in, XVarList* out)
{
    XDeviceTimerContext* context = timerContext(handle);
    bool ready;
    (void)self;
    if (!context) return false;

    switch (command) {
    case XDeviceCommand_Poll:
        if (!out || out->m_size != sizeof(ready))
            return timerSetError(context, XDeviceError_InvalidArgument);
        ready = context->m_active != 0;
        memcpy(out->data, &ready, sizeof(ready));
        XVarList_start(out);
        return true;
    case XDeviceTimerCommand_Start:
        if (in || out) return timerSetError(context, XDeviceError_InvalidArgument);
        return timerStart(context) || timerSetError(context, XDeviceError_IoFail);
    case XDeviceTimerCommand_Stop:
        if (in || out) return timerSetError(context, XDeviceError_InvalidArgument);
        return timerStop(context) || timerSetError(context, XDeviceError_IoFail);
    case XDeviceTimerCommand_Restart:
        if (in || out) return timerSetError(context, XDeviceError_InvalidArgument);
        if (!timerStop(context)) return timerSetError(context, XDeviceError_IoFail);
        return timerStart(context) || timerSetError(context, XDeviceError_IoFail);
    default:
        return timerSetError(context, XDeviceError_NotSupported);
    }
}

bool XDeviceTimer_start(XFd fd)
{
    return XDevice_control(fd, XDeviceTimerCommand_Start, NULL, NULL);
}

bool XDeviceTimer_stop(XFd fd)
{
    return XDevice_control(fd, XDeviceTimerCommand_Stop, NULL, NULL);
}

bool XDeviceTimer_restart(XFd fd)
{
    return XDevice_control(fd, XDeviceTimerCommand_Restart, NULL, NULL);
}

static bool timerSetVariant(XFd fd, XDeviceTimerProperty property,
    const void* data, size_t size, int type)
{
    XVariant value;
    bool result;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, (void*)data, size, type);
    result = XDevice_setProperty(fd, (XDeviceProperty)property, &value);
    XVariant_deinit_base((XClass*)&value);
    return result;
}

bool XDeviceTimer_setTimerType(XFd fd, XTimerType type)
{
    int value = (int)type;
    return timerSetVariant(fd, XDeviceTimerProperty_TimerType, &value,
        sizeof(value), XVariantType_Int);
}

bool XDeviceTimer_setInterval(XFd fd, XDuration intervalNs)
{
    return timerSetVariant(fd, XDeviceTimerProperty_IntervalNs, &intervalNs,
        sizeof(intervalNs), XVariantType_Int64);
}

bool XDeviceTimer_setCallback(XFd fd, XDeviceTimerCallback callback)
{
    void* value = NULL;
    union {
        void* data;
        XDeviceTimerCallback callback;
    } callbackValue;
    callbackValue.callback = callback;
    value = callbackValue.data;
    return timerSetVariant(fd, XDeviceTimerProperty_Callback, &value,
        sizeof(value), XVariantType_Ptr);
}

bool XDeviceTimer_setUserData(XFd fd, void* userData)
{
    return timerSetVariant(fd, XDeviceTimerProperty_UserData, &userData,
        sizeof(userData), XVariantType_Ptr);
}
