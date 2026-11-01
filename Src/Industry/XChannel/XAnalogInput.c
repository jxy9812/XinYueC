/**
 * @file       XAnalogInput.c
 * @brief      模拟量输入通道（AI）接口实现。
 * @details    接口层不含传输逻辑：Read 虚槽注册空实现（返回 false），
 *             由适配器子类重载。
 */
#include "XAnalogInput.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include <string.h>
#include "XMemory.h"

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

static bool VXAnalogInput_read(XAnalogInput* self, int32_t* value)
{
    (void)self;
    (void)value;
    XERROR_PRINTF("XAnalogInput_read: 接口未由适配器实现\n");
    return false;
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XAnalogInput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAnalogInput)
    XVTABLE_INHERIT_XCLASS(XIoChannel);
    void* table[] = { VXAnalogInput_read };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XAnalogInput_init(XAnalogInput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XAnalogInput));
    XIoChannel_init((XIoChannel*)obj);
    XClassSetVtable(obj, XAnalogInput);
}

XAnalogInput* XAnalogInput_create(void)
{
    XAnalogInput* obj = (XAnalogInput*)XMemory_calloc(1, sizeof(XAnalogInput), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XAnalogInput_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 虚函数调度入口 ==================== */

bool XAnalogInput_read_base(XAnalogInput* self, int32_t* value)
{
    if (ISNULL(self, "XAnalogInput") || ISNULL(XClassGetVtable(self), "Vtable"))
        return false;
    return XClassGetVirtualFunc(self, EXAnalogInput_Read,
                                bool (*)(XAnalogInput*, int32_t*))(self, value);
}

/* ==================== 信号 ==================== */

void* XAnalogInput_inputChanged_signal(XAnalogInput* self, int32_t value)
{
    XEmitSignal(self, XAnalogInput_inputChanged_signal,
                XVarList_Create(XVar(int32_t, value)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
