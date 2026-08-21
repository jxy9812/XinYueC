/******************************************************************************
 * @file       XIconEngine.c
 * @brief      XIconEngine 图标引擎抽象类实现。
 ******************************************************************************/
#include "XIconEngine.h"
#include "XIcon.h"
#include "XMemory.h"
#include "XObject.h"
#include "XPicture.h"
#include <string.h>

static void VXIconEngine_paint(const XIconEngine* self, void* painter, const XRect* rect,
                               XIconMode mode, XIconState state)
{ (void)self; (void)painter; (void)rect; (void)mode; (void)state; }

static void VXIconEngine_actualSize(const XIconEngine* self, const XSize* size,
                                    XIconMode mode, XIconState state, XSize* out)
{
    (void)self; (void)mode; (void)state;
    if (out) *out = size ? *size : (XSize){0, 0};
}

static void VXIconEngine_pixmap(const XIconEngine* self, const XSize* size,
                                XIconMode mode, XIconState state, XPixmap* out)
{ (void)self; (void)size; (void)mode; (void)state; if (out) XPixmap_init(out); }

static void VXIconEngine_addPixmap(XIconEngine* self, const XPixmap* pixmap,
                                   XIconMode mode, XIconState state)
{ (void)self; (void)pixmap; (void)mode; (void)state; }

static void VXIconEngine_addFile(XIconEngine* self, const XString* fileName,
                                 const XSize* size, XIconMode mode, XIconState state)
{ (void)self; (void)fileName; (void)size; (void)mode; (void)state; }

static XString* VXIconEngine_key(const XIconEngine* self)
{ (void)self; return XString_create(); }

static XIconEngine* VXIconEngine_clone(const XIconEngine* self)
{ (void)self; return NULL; }

static bool VXIconEngine_read(XIconEngine* self, XIODevice* device)
{ (void)self; (void)device; return false; }

static bool VXIconEngine_write(const XIconEngine* self, XIODevice* device)
{ (void)self; (void)device; return false; }

static void VXIconEngine_availableSizes(const XIconEngine* self, XIconMode mode,
                                        XIconState state, XVector* out)
{ (void)self; (void)mode; (void)state; if (out) XVector_clear_base((XContainer*)out); }

static XString* VXIconEngine_iconName(const XIconEngine* self)
{ (void)self; return XString_create(); }

static bool VXIconEngine_isNull(const XIconEngine* self)
{ (void)self; return false; }

static void VXIconEngine_scaledPixmap(const XIconEngine* self, const XSize* size,
                                      XIconMode mode, XIconState state, float scale,
                                      XPixmap* out)
{
    XSize physical;
    if (!out) return;
    XPixmap_init(out);
    if (!size) return;
    if (scale <= 0.0f) scale = 1.0f;
    physical.width = (int)((float)size->width * scale + 0.5f);
    physical.height = (int)((float)size->height * scale + 0.5f);
    XIconEngine_pixmap_base(self, &physical, mode, state, out);
    if (!XPixmap_isNull(out)) XPixmap_setDevicePixelRatio(out, scale);
}

static void VXIconEngine_virtualHook(const XIconEngine* self, int id, void* data)
{ (void)self; (void)id; (void)data; }

static void VXIconEngine_deinit(XIconEngine* self)
{ if (self) XClass_Deinit_Parent(XClass, (XClass*)self); }

XVtable* XIconEngine_class_init(void)
{
    void* table[] = {
        (void*)VXIconEngine_paint, (void*)VXIconEngine_actualSize,
        (void*)VXIconEngine_pixmap, (void*)VXIconEngine_addPixmap,
        (void*)VXIconEngine_addFile, (void*)VXIconEngine_key,
        (void*)VXIconEngine_clone, (void*)VXIconEngine_read,
        (void*)VXIconEngine_write, (void*)VXIconEngine_availableSizes,
        (void*)VXIconEngine_iconName, (void*)VXIconEngine_isNull,
        (void*)VXIconEngine_scaledPixmap, (void*)VXIconEngine_virtualHook
    };
    XVTABLE_INIT_DEFAULT(XIconEngine)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIconEngine_deinit);
    return XVTABLE_DEFAULT;
}

XIconEngine* XIconEngine_create_ex(XMemoryType memory)
{
    XIconEngine* self = (XIconEngine*)XMemory_malloc(sizeof(XIconEngine), memory);
    if (!self) return NULL;
    XIconEngine_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

void XIconEngine_init(XIconEngine* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XIconEngine);
}


#define XICON_ENGINE_CALL(self, slot, type, args, fallback) \
    ((self) && XClassGetVtable(self) ? XClassGetVirtualFunc((self), slot, type) args : fallback)

void XIconEngine_paint_base(const XIconEngine* self, void* painter, const XRect* rect,
                           XIconMode mode, XIconState state)
{ if (self && XClassGetVtable(self)) XClassGetVirtualFunc(self, EXIconEngine_Paint, void(*)(const XIconEngine*,void*,const XRect*,XIconMode,XIconState))(self,painter,rect,mode,state); }
void XIconEngine_actualSize_base(const XIconEngine* self, const XSize* size,
                                 XIconMode mode, XIconState state, XSize* out)
{ if (out) { *out = (XSize){0,0}; if (self && XClassGetVtable(self)) XClassGetVirtualFunc(self, EXIconEngine_ActualSize, void(*)(const XIconEngine*,const XSize*,XIconMode,XIconState,XSize*))(self,size,mode,state,out); } }
void XIconEngine_pixmap_base(const XIconEngine* self, const XSize* size,
                             XIconMode mode, XIconState state, XPixmap* out)
{ if (out) { XPixmap_init(out); if (self && XClassGetVtable(self)) XClassGetVirtualFunc(self, EXIconEngine_Pixmap, void(*)(const XIconEngine*,const XSize*,XIconMode,XIconState,XPixmap*))(self,size,mode,state,out); } }
void XIconEngine_addPixmap_base(XIconEngine* self, const XPixmap* pixmap,
                                XIconMode mode, XIconState state)
{ if (self && XClassGetVtable(self)) XClassGetVirtualFunc(self, EXIconEngine_AddPixmap, void(*)(XIconEngine*,const XPixmap*,XIconMode,XIconState))(self,pixmap,mode,state); }
void XIconEngine_addFile_base(XIconEngine* self, const XString* fileName,
                              const XSize* size, XIconMode mode, XIconState state)
{ if (self && XClassGetVtable(self)) XClassGetVirtualFunc(self, EXIconEngine_AddFile, void(*)(XIconEngine*,const XString*,const XSize*,XIconMode,XIconState))(self,fileName,size,mode,state); }
XString* XIconEngine_key_base(const XIconEngine* self)
{ return XICON_ENGINE_CALL(self, EXIconEngine_Key, XString*(*)(const XIconEngine*),(self), XString_create()); }
XIconEngine* XIconEngine_clone_base(const XIconEngine* self)
{ return XICON_ENGINE_CALL(self, EXIconEngine_Clone, XIconEngine*(*)(const XIconEngine*),(self), NULL); }
bool XIconEngine_read_base(XIconEngine* self, XIODevice* device)
{ return XICON_ENGINE_CALL(self, EXIconEngine_Read, bool(*)(XIconEngine*,XIODevice*),(self,device), false); }
bool XIconEngine_write_base(const XIconEngine* self, XIODevice* device)
{ return XICON_ENGINE_CALL(self, EXIconEngine_Write, bool(*)(const XIconEngine*,XIODevice*),(self,device), false); }
void XIconEngine_availableSizes_base(const XIconEngine* self, XIconMode mode, XIconState state, XVector* out)
{ if (self && XClassGetVtable(self)) XClassGetVirtualFunc(self, EXIconEngine_AvailableSizes, void(*)(const XIconEngine*,XIconMode,XIconState,XVector*))(self,mode,state,out); }
XString* XIconEngine_iconName_base(const XIconEngine* self)
{ return XICON_ENGINE_CALL(self, EXIconEngine_IconName, XString*(*)(const XIconEngine*),(self), XString_create()); }
bool XIconEngine_isNull_base(const XIconEngine* self)
{ return XICON_ENGINE_CALL(self, EXIconEngine_IsNull, bool(*)(const XIconEngine*),(self), true); }
void XIconEngine_scaledPixmap_base(const XIconEngine* self, const XSize* size, XIconMode mode, XIconState state, float scale, XPixmap* out)
{ if (out) { XPixmap_init(out); if (self && XClassGetVtable(self)) XClassGetVirtualFunc(self, EXIconEngine_ScaledPixmap, void(*)(const XIconEngine*,const XSize*,XIconMode,XIconState,float,XPixmap*))(self,size,mode,state,scale,out); } }
void XIconEngine_virtualHook_base(const XIconEngine* self, int id, void* data)
{ if (self && XClassGetVtable(self)) XClassGetVirtualFunc(self, EXIconEngine_VirtualHook, void(*)(const XIconEngine*,int,void*))(self,id,data); }
