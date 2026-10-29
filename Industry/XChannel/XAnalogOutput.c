/**
 * @file       XAnalogOutput.c
 * @brief      模拟量输出通道（AO）接口实现。
 * @details    接口层不含传输逻辑：Write 虚槽注册空实现（返回 false），
 *             由适配器子类重载。
 */
#include "XAnalogOutput.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include <string.h>
#include "XMemory.h"

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

static bool VXAnalogOutput_write(XAnalogOutput* self, int32_t value)
{
    (void)self;
    (void)value;
    XERROR_PRINTF("XAnalogOutput_write: 接口未由适配器实现\n");
    return false;
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XAnalogOutput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAnalogOutput)
    XVTABLE_INHERIT_XCLASS(XIoChannel);
    void* table[] = { VXAnalogOutput_write };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XAnalogOutput_init(XAnalogOutput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XAnalogOutput));
    XIoChannel_init((XIoChannel*)obj);
    XClassSetVtable(obj, XAnalogOutput);
}

XAnalogOutput* XAnalogOutput_create(void)
{
    XAnalogOutput* obj = (XAnalogOutput*)XMemory_calloc(1, sizeof(XAnalogOutput), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XAnalogOutput_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 虚函数调度入口 ==================== */

bool XAnalogOutput_write_base(XAnalogOutput* self, int32_t value)
{
    if (ISNULL(self, "XAnalogOutput") || ISNULL(XClassGetVtable(self), "Vtable"))
        return false;
    return XClassGetVirtualFunc(self, EXAnalogOutput_Write,
                                bool (*)(XAnalogOutput*, int32_t))(self, value);
}

/* ==================== 信号 ==================== */

void* XAnalogOutput_writeDone_signal(XAnalogOutput* self, bool ok)
{
    XEmitSignal(self, XAnalogOutput_writeDone_signal,
                XVarList_Create(XVar(bool, ok)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
