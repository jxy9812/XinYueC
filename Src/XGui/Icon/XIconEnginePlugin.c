/******************************************************************************
 * @file       XIconEnginePlugin.c
 * @brief      XIconEnginePlugin 图标引擎插件工厂实现。
 ******************************************************************************/
#include "XIconEnginePlugin.h"
#include "XMemory.h"
#include <string.h>

static XIconEngine* VXIconEnginePlugin_create(XIconEnginePlugin* self,
                                               const XString* fileName)
{ (void)self; (void)fileName; return NULL; }

static void VXIconEnginePlugin_deinit(XIconEnginePlugin* self)
{ if (self) XClass_Deinit_Parent(XObject, (XObject*)self); }

XVtable* XIconEnginePlugin_class_init(void)
{
    void* table[] = { (void*)VXIconEnginePlugin_create };
    XVTABLE_INIT_DEFAULT(XIconEnginePlugin)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIconEnginePlugin_deinit);
    return XVTABLE_DEFAULT;
}

XIconEnginePlugin* XIconEnginePlugin_create_ex(XMemoryType memory)
{
    XIconEnginePlugin* self = (XIconEnginePlugin*)XMemory_malloc(
        sizeof(XIconEnginePlugin), memory);
    if (!self) return NULL;
    XIconEnginePlugin_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

void XIconEnginePlugin_init(XIconEnginePlugin* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XIconEnginePlugin);
}

XIconEngine* XIconEnginePlugin_createEngine_base(XIconEnginePlugin* self,
                                                  const XString* fileName)
{
    if (!self || XClassIsVtableNull(self)) return NULL;
    return XClassGetVirtualFunc(self, EXIconEnginePlugin_Create,
        XIconEngine*(*)(XIconEnginePlugin*, const XString*))(self, fileName);
}

XIconEngine* XIconEnginePlugin_createEngine_2_base(XIconEnginePlugin* self,
                                                    const char* fileName)
{
    XIconEngine* result;
    XString* value = fileName ? XString_create_utf8(fileName) : XString_create();
    result = XIconEnginePlugin_createEngine_base(self, value);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

XIconEngine* XIconEnginePlugin_create_base(XIconEnginePlugin* self,
                                           const XString* fileName)
{ return XIconEnginePlugin_createEngine_base(self, fileName); }

XIconEngine* XIconEnginePlugin_create_2_base(XIconEnginePlugin* self,
                                             const char* fileName)
{ return XIconEnginePlugin_createEngine_2_base(self, fileName); }
