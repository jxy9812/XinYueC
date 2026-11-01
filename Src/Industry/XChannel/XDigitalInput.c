/**
 * @file       XDigitalInput.c
 * @brief      数字量输入通道（DI）接口实现。
 * @details    接口层不含传输逻辑：Read 虚槽注册空实现（返回 false），
 *             由适配器子类重载。
 */
#include "XDigitalInput.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include <string.h>
#include "XMemory.h"

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

static bool VXDigitalInput_read(XDigitalInput* self, bool* active)
{
    (void)self;
    (void)active;
    XERROR_PRINTF("XDigitalInput_read: 接口未由适配器实现\n");
    return false;
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XDigitalInput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDigitalInput)
    XVTABLE_INHERIT_XCLASS(XIoChannel);
    void* table[] = { VXDigitalInput_read };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XDigitalInput_init(XDigitalInput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XDigitalInput));
    XIoChannel_init((XIoChannel*)obj);
    XClassSetVtable(obj, XDigitalInput);
}

XDigitalInput* XDigitalInput_create(void)
{
    XDigitalInput* obj = (XDigitalInput*)XMemory_calloc(1, sizeof(XDigitalInput), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XDigitalInput_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 虚函数调度入口 ==================== */

bool XDigitalInput_read_base(XDigitalInput* self, bool* active)
{
    if (ISNULL(self, "XDigitalInput") || ISNULL(XClassGetVtable(self), "Vtable"))
        return false;
    return XClassGetVirtualFunc(self, EXDigitalInput_Read,
                                bool (*)(XDigitalInput*, bool*))(self, active);
}

/* ==================== 信号 ==================== */

void* XDigitalInput_inputChanged_signal(XDigitalInput* self, bool active)
{
    XEmitSignal(self, XDigitalInput_inputChanged_signal,
                XVarList_Create(XVar(bool, active)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
