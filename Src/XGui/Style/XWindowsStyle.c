#include "XWindowsStyle.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XSTYLE_ON

static void VXWindowsStyle_deinit(XWindowsStyle* self);

XVtable* XWindowsStyle_class_init(void)
{
    /* 新增 StyleHint 槽：以 NULL 占位追加（保持 size 与枚举一致）。 */
    void* table[XCLASS_VTABLE_GET_SIZE(XWindowsStyle) -
                XCLASS_VTABLE_GET_SIZE(XCommonStyle)] = { NULL };
    XVTABLE_INIT_DEFAULT(XWindowsStyle)
    XVTABLE_INHERIT_XCLASS(XCommonStyle);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXWindowsStyle_deinit);
    return XVTABLE_DEFAULT;
}

void XWindowsStyle_init(XWindowsStyle* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XCommonStyle_init(&self->m_base);
    XClassSetVtable(self, XWindowsStyle);
}

XWindowsStyle* XWindowsStyle_create_ex(XMemoryType memory)
{
    XWindowsStyle* self = (XWindowsStyle*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XWindowsStyle_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXWindowsStyle_deinit(XWindowsStyle* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XCommonStyle, (XCommonStyle*)self);
}

int XWindowsStyle_styleHint(XWindowsStyle* self, int hint,
                            const XStyleOption* option)
{
    /* Qt 6 QWindowsStyle 的 styleHint 均回落基类；此处保持分派点。 */
    (void)hint;
    (void)option;
    return XStyle_pixelMetric((XStyle*)self, XStylePM_DefaultFrameWidth,
                              option);
}

#endif /* XSTYLE_ON */
