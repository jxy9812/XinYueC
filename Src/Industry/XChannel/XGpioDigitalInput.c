/**
 * @file       XGpioDigitalInput.c
 * @brief      GPIO 数字量输入（DI）通道适配器实现。
 * @details    周期轮询 XGpio_readActive：值翻转时发射 inputChanged 信号，
 *             首个成功轮询拍推送初值；读失败经保护接口报错并清除 valid。
 *             GPIO 句柄借用不拥有，deinit 只停定时器。
 */
#include "XGpioDigitalInput.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_GPIO_ON
#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || defined(USE_STDPERIPH_DRIVER) || defined(ESP_PLATFORM)

#include <string.h>
#include "XMemory.h"
#include "XEvent.h"
#include "XIoChannel_Protected.h"

/* ==================== 内部实现（static，仅供本文件使用） ==================== */

/**
 * @brief 单次轮询：读引脚有效态并按翻转/首拍规则推送 inputChanged。
 * @param self 目标适配器；不能为 NULL。
 * @return 无。读失败时报硬件错误（同一错误去重，不反复发射 errorOccurred）。
 */
static void gpioDiPoll(XGpioDigitalInput* self)
{
    XIoChannel* io = &self->m_class.m_class;
    bool value = false;
    if (!self->m_gpio) {
        /* 未挂接：静默跳过本拍（挂接由 attach 负责）。 */
        return;
    }
    if (!XGpio_readActive(self->m_gpio, &value)) {
        if (XIoChannel_lastError(io) != (int)XIoChannelError_Hardware) {
            XIoChannel_setError(io, XIoChannelError_Hardware);
        }
        return;
    }
    XIoChannel_clearError(io);
    XIoChannel_setValid(io, true);
    if (!self->m_hasLastValue || value != self->m_lastValue) {
        self->m_lastValue = value;
        self->m_hasLastValue = true;
        XDigitalInput_inputChanged_signal(&self->m_class, value);
    }
}

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

static bool VXGpioDigitalInput_read(XGpioDigitalInput* self, bool* active)
{
    XIoChannel* io;
    bool value = false;
    if (!self || !active) return false;
    io = &self->m_class.m_class;
    if (!self->m_gpio) {
        XIoChannel_setError(io, XIoChannelError_NotAttached);
        return false;
    }
    if (!XGpio_readActive(self->m_gpio, &value)) {
        XIoChannel_setError(io, XIoChannelError_Hardware);
        return false;
    }
    XIoChannel_clearError(io);
    XIoChannel_setValid(io, true);
    *active = value;
    return true;
}

static void VXGpioDigitalInput_timerEvent(XObject* obj, XTimerEvent* event)
{
    XGpioDigitalInput* self = (XGpioDigitalInput*)obj;
    if (!self || !event) goto parent_call;
    XEvent_accept(event);
    if (XTimerEvent_timerId(event) != self->m_pollTimer) goto parent_call;
    gpioDiPoll(self);
    return;
parent_call:
    /* 未知定时器 id 交父类处理，不吞事件。 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(obj, event);
}

static void VXGpioDigitalInput_deinit(XGpioDigitalInput* self)
{
    if (!self) return;
    if (self->m_pollTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_pollTimer);
        self->m_pollTimer = XTIMER_INVALID_ID;
    }
    self->m_gpio = NULL; /* 借用资源，不关闭不释放，仅清除引用。 */
    XClass_Deinit_Parent(XDigitalInput, (XDigitalInput*)self);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XGpioDigitalInput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGpioDigitalInput)
    XVTABLE_INHERIT_XCLASS(XDigitalInput);
    XVTABLE_OVERLOAD_DEFAULT(EXDigitalInput_Read, VXGpioDigitalInput_read);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXGpioDigitalInput_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGpioDigitalInput_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XGpioDigitalInput_init(XGpioDigitalInput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XGpioDigitalInput));
    XDigitalInput_init(&obj->m_class);
    XClassSetVtable(obj, XGpioDigitalInput);
    obj->m_gpio = NULL;
    obj->m_pollTimer = XTIMER_INVALID_ID;
    obj->m_lastValue = false;
    obj->m_hasLastValue = false;
}

bool XGpioDigitalInput_attach(XGpioDigitalInput* obj, XGpio* gpio, uint32_t pollIntervalMs)
{
    XTimerId timer;
    if (!obj || !gpio || pollIntervalMs == 0u) {
        XERROR_PRINTF("XGpioDigitalInput_attach: 参数非法\n");
        return false;
    }
    timer = XObject_startTimer_ms((XObject*)obj, (uint64_t)pollIntervalMs,
                                  XTimerType_CoarseTimer);
    if (timer == XTIMER_INVALID_ID) {
        XERROR_PRINTF("XGpioDigitalInput_attach: 轮询定时器启动失败\n");
        return false;
    }
    /* 参数校验通过后再拆旧挂接，失败路径不影响原有状态。 */
    if (obj->m_pollTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)obj, obj->m_pollTimer);
    }
    obj->m_pollTimer = timer;
    obj->m_gpio = gpio;
    obj->m_lastValue = false;
    obj->m_hasLastValue = false; /* 重新挂接后首拍重新推送初值。 */
    return true;
}

XGpioDigitalInput* XGpioDigitalInput_create_ex(XGpio* gpio, uint32_t pollIntervalMs)
{
    XGpioDigitalInput* obj = (XGpioDigitalInput*)XMalloc_System(sizeof(XGpioDigitalInput));
    if (!obj) return NULL;
    XGpioDigitalInput_init(obj);
    Set_Class_IsHeap(obj, true); /* 堆分配对象：标记所有权（旧写法把释放函数当 XMemoryType 传入，编译即错）。 */
    if (!XGpioDigitalInput_attach(obj, gpio, pollIntervalMs)) {
        XClassDelete(obj);
        return NULL;
    }
    return obj;
}

#endif /* 平台守卫 */

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_GPIO_ON */
