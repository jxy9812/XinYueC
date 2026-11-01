/**
 * @file       XGpioDigitalOutput.c
 * @brief      GPIO 数字量输出（DO）通道适配器实现。
 * @details    write 直接调 XGpio_writeActive：成功则同步发射 writeDone(true)
 *             后返回 true；失败经保护接口报错后返回 false。
 *             GPIO 句柄借用不拥有，deinit 只清除引用。
 */
#include "XGpioDigitalOutput.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_GPIO_ON
#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || defined(USE_STDPERIPH_DRIVER) || defined(ESP_PLATFORM)

#include <string.h>
#include "XMemory.h"
#include "XIoChannel_Protected.h"

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

static bool VXGpioDigitalOutput_write(XGpioDigitalOutput* self, bool active)
{
    XIoChannel* io;
    if (!self) return false;
    io = &self->m_class.m_class;
    if (!self->m_gpio) {
        XIoChannel_setError(io, XIoChannelError_NotAttached);
        return false;
    }
    if (!XGpio_writeActive(self->m_gpio, active)) {
        XIoChannel_setError(io, XIoChannelError_Hardware);
        return false;
    }
    XIoChannel_clearError(io);
    XIoChannel_setValid(io, true);
    /* 同步通道：在 write 返回前同步发射完成确认。 */
    XDigitalOutput_writeDone_signal(&self->m_class, true);
    return true;
}

static void VXGpioDigitalOutput_deinit(XGpioDigitalOutput* self)
{
    if (!self) return;
    self->m_gpio = NULL; /* 借用资源，不关闭不释放，仅清除引用。 */
    XClass_Deinit_Parent(XDigitalOutput, (XDigitalOutput*)self);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XGpioDigitalOutput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGpioDigitalOutput)
    XVTABLE_INHERIT_XCLASS(XDigitalOutput);
    XVTABLE_OVERLOAD_DEFAULT(EXDigitalOutput_Write, VXGpioDigitalOutput_write);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGpioDigitalOutput_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XGpioDigitalOutput_init(XGpioDigitalOutput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XGpioDigitalOutput));
    XDigitalOutput_init(&obj->m_class);
    XClassSetVtable(obj, XGpioDigitalOutput);
    obj->m_gpio = NULL;
}

bool XGpioDigitalOutput_attach(XGpioDigitalOutput* obj, XGpio* gpio, uint32_t pollIntervalMs)
{
    (void)pollIntervalMs; /* 保留参数：DO 同步直驱，不使用定时器。 */
    if (!obj || !gpio) {
        XERROR_PRINTF("XGpioDigitalOutput_attach: 参数非法\n");
        return false;
    }
    obj->m_gpio = gpio;
    return true;
}

XGpioDigitalOutput* XGpioDigitalOutput_create_ex(XGpio* gpio, uint32_t pollIntervalMs)
{
    XGpioDigitalOutput* obj = (XGpioDigitalOutput*)XMalloc_System(sizeof(XGpioDigitalOutput));
    if (!obj) return NULL;
    XGpioDigitalOutput_init(obj);
    Set_Class_IsHeap(obj, true); /* 堆分配对象：标记所有权（旧写法把释放函数当 XMemoryType 传入，编译即错）。 */
    if (!XGpioDigitalOutput_attach(obj, gpio, pollIntervalMs)) {
        XClassDelete(obj);
        return NULL;
    }
    return obj;
}

#endif /* 平台守卫 */

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_GPIO_ON */
