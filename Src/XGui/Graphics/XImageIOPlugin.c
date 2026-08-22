/******************************************************************************
 * @file       XImageIOPlugin.c
 * @brief      XImageIOPlugin 图像 I/O 插件抽象类实现。
 ******************************************************************************/
#include "XImageIOPlugin.h"
#include "XMemory.h"
#include <string.h>

static uint32_t VXImageIOPlugin_capabilities(const XImageIOPlugin* self,
                                             XIODevice* device, const XString* format)
{ (void)self; (void)device; (void)format; return 0; }
static XImageIOHandler* VXImageIOPlugin_create(XImageIOPlugin* self, XIODevice* device,
                                               const XString* format)
{ (void)self; (void)device; (void)format; return NULL; }
static XStringList* VXImageIOPlugin_keys(const XImageIOPlugin* self)
{ (void)self; return XStringList_create(); }
static XStringList* VXImageIOPlugin_nameFilters(const XImageIOPlugin* self)
{ (void)self; return XStringList_create(); }
static void VXImageIOPlugin_deinit(XImageIOPlugin* self)
{ if (self) XClass_Deinit_Parent(XObject, (XObject*)self); }

XVtable* XImageIOPlugin_class_init(void)
{
    void* table[] = { (void*)VXImageIOPlugin_capabilities,
                      (void*)VXImageIOPlugin_create,
                      (void*)VXImageIOPlugin_keys,
                      (void*)VXImageIOPlugin_nameFilters };
    XVTABLE_INIT_DEFAULT(XImageIOPlugin)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageIOPlugin_deinit);
    return XVTABLE_DEFAULT;
}

XImageIOPlugin* XImageIOPlugin_create_ex(XMemoryType memory)
{
    XImageIOPlugin* self = (XImageIOPlugin*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XImageIOPlugin_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

void XImageIOPlugin_init(XImageIOPlugin* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XImageIOPlugin);
}

uint32_t XImageIOPlugin_capabilities_base(const XImageIOPlugin* self,
                                          XIODevice* device, const XString* format)
{ return self && XClassGetVtable(self) ? XClassGetVirtualFunc(self, EXImageIOPlugin_Capabilities,
    uint32_t(*)(const XImageIOPlugin*,XIODevice*,const XString*))(self,device,format) : 0; }
XImageIOHandler* XImageIOPlugin_create_base(XImageIOPlugin* self, XIODevice* device,
                                            const XString* format)
{ return self && XClassGetVtable(self) ? XClassGetVirtualFunc(self, EXImageIOPlugin_Create,
    XImageIOHandler*(*)(XImageIOPlugin*,XIODevice*,const XString*))(self,device,format) : NULL; }
XStringList* XImageIOPlugin_keys_base(const XImageIOPlugin* self)
{ return self && XClassGetVtable(self) ? XClassGetVirtualFunc(self, EXImageIOPlugin_Keys,
    XStringList*(*)(const XImageIOPlugin*))(self) : XStringList_create(); }
XStringList* XImageIOPlugin_nameFilters_base(const XImageIOPlugin* self)
{ return self && XClassGetVtable(self) ? XClassGetVirtualFunc(self, EXImageIOPlugin_NameFilters,
    XStringList*(*)(const XImageIOPlugin*))(self) : XStringList_create(); }
