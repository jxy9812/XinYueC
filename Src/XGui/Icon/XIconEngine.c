/******************************************************************************
 * @file       XIconEngine.c
 * @brief      XIconEngine 图标引擎抽象类实现。
 ******************************************************************************/
#include "XIconEngine.h"
#include "XIcon.h"
#include "XMemory.h"
#include "XObject.h"
#include "XPicture.h"
#include "XPainter.h"
#include <limits.h>
#include <math.h>
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
{
    XImage image;
    XPixmap generated;
    XPainter painter;
    XRect rect;
    bool active;
    if (!out) return;
    if (!size || size->width <= 0 || size->height <= 0) return;

    /* 对标 Qt 6.8 QIconEngine::pixmap：先创建请求尺寸的像素图，
       再通过 QPainter 调用 paint() 填充，而不是直接返回空对象。 */
    XImage_init_ex(&image, size->width, size->height,
                   XImageFormat_ARGB32_Premultiplied);
    if (XImage_isNull(&image)) {
        XImage_deinit_base(&image);
        return;
    }
    XPainter_init(&painter, NULL);
    active = XPainter_begin_image(&painter, &image);
    if (active) {
        rect.x = 0;
        rect.y = 0;
        rect.width = size->width;
        rect.height = size->height;
        XIconEngine_paint_base(self, &painter, &rect, mode, state);
        XPainter_end(&painter);
        /* pixmap_base() 已经清空调用方输出；先在临时像素图中接管图像，
           再移动到输出，避免 XPixmap_init_image() 对输出进行第二次 reset。 */
        XPixmap_init_image(&generated, &image, 0);
        XPixmap_move_base(out, &generated);
        XPixmap_deinit_base(&generated);
    }
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}

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
{
    bool isNull = false;
    /* Qt 的非钩子实现先将结果设为 false，再让 virtual_hook(IsNullHook)
       由派生引擎决定是否改写；默认钩子不修改该值。 */
    XIconEngine_virtualHook_base(self, XIconEngine_IsNullHook, &isNull);
    return isNull;
}

static void VXIconEngine_scaledPixmap(const XIconEngine* self, const XSize* size,
                                      XIconMode mode, XIconState state, float scale,
                                      XPixmap* out)
{
    XIconEngineScaledPixmapArgument argument;
    if (!out) return;
    if (!size) return;
    argument.size = *size;
    argument.mode = mode;
    argument.state = state;
    argument.scale = scale;
    argument.pixmap = out;
    /* Qt 的默认实现通过 ScaledPixmapHook 间接调用 pixmap()；派生引擎
       可以只重载 hook 来提供主题或高 DPI 专用资源。 */
    XIconEngine_virtualHook_base(self, XIconEngine_ScaledPixmapHook, &argument);
}

static void VXIconEngine_virtualHook(const XIconEngine* self, int id, void* data)
{
    XIconEngineScaledPixmapArgument* argument;
    XSize physical;
    float scale;
    if (!data) return;
    if (id != XIconEngine_ScaledPixmapHook) return;
    argument = (XIconEngineScaledPixmapArgument*)data;
    if (!argument->pixmap) return;
    /* QIconEngine::virtual_hook() 通过值语义替换 QPixmap；先释放调用方
       原有输出，使无效尺寸也得到空像素图而不会残留旧内容。保留虚表，
       由 pixmap_base 在需要生成有效结果时继续复用该对象。 */
    XPixmap_init(argument->pixmap);
    if (argument->size.width <= 0 || argument->size.height <= 0)
        return;
    /* Qt 的 QSize * qreal 在非正比例下产生非正尺寸，QPixmap 最终为空；
       不把错误的比例悄悄改写成 1.0。NaN/无穷值也必须在转换前拒绝，
       避免 C 浮点到整数转换的未定义行为。 */
    scale = argument->scale;
    if (!(scale > 0.0f) || !isfinite(scale)) return;
    {
        double width = (double)argument->size.width * (double)scale;
        double height = (double)argument->size.height * (double)scale;
        if (width > (double)INT_MAX - 0.5 ||
            height > (double)INT_MAX - 0.5)
            return;
        physical.width = (int)(width + 0.5);
        physical.height = (int)(height + 0.5);
    }
    if (physical.width <= 0 || physical.height <= 0) return;
    /* Qt 6.8 qiconengine.cpp:236-247 只把物理尺寸的 pixmap() 结果写入
       参数，不在 virtual_hook() 内改写 DPR；QIcon::pixmap() 会在钩子返回
       后依据实际像素尺寸统一设置设备像素比。这里保持同一层次的职责，
       直接调用引擎的 pixmap() 虚函数并保留其返回对象的 DPR。 */
    /* scaledPixmap_base() 已经清空输出；这里直接调用像素图虚函数，
       避免再次经过 pixmap_base() 重复清空同一个输出对象。 */
    if (self && XClassGetVtable(self))
        XClassGetVirtualFunc(self, EXIconEngine_Pixmap,
            void(*)(const XIconEngine*, const XSize*, XIconMode, XIconState,
                    XPixmap*))(self, &physical, argument->mode, argument->state,
                               argument->pixmap);
}

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
