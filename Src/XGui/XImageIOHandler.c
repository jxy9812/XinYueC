/******************************************************************************
 * @file       XImageIOHandler.c
 * @brief      XImageIOHandler 图像 IO 处理器基类实现（对标 Qt 6.8 QImageIOHandler）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageIOHandler.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief      XImageIOHandler 私有数据
 */
typedef struct XImageIOHandlerPrivate
{
    XIODevice*  m_device;   /**< IO 设备 */
    char*       m_format;   /**< 格式字符串 */
}XImageIOHandlerPrivate;

static void VXImageIOHandler_deinit(XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler")) return;
    if (self->m_data)
    {
        if (self->m_data->m_format) XFree_System(self->m_data->m_format);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
}

static bool VXImageIOHandler_canRead(const XImageIOHandler* self)
{
    (void)self;
    return false; // 纯虚函数，默认返回 false
}

static bool VXImageIOHandler_read(XImageIOHandler* self, XImage* image)
{
    (void)self;
    (void)image;
    return false; // 纯虚函数，默认返回 false
}

static bool VXImageIOHandler_write(XImageIOHandler* self, const XImage* image)
{
    (void)self;
    (void)image;
    return false;
}

static bool VXImageIOHandler_option(const XImageIOHandler* self, XImageIOHandlerOption option, void* out)
{
    (void)self;
    (void)option;
    (void)out;
    return false;
}

static void VXImageIOHandler_setOption(XImageIOHandler* self, XImageIOHandlerOption option, const void* value)
{
    (void)self;
    (void)option;
    (void)value;
}

static bool VXImageIOHandler_supportsOption(const XImageIOHandler* self, XImageIOHandlerOption option)
{
    (void)self;
    (void)option;
    return false;
}

static bool VXImageIOHandler_jumpToNextImage(XImageIOHandler* self)
{
    (void)self;
    return false;
}

static bool VXImageIOHandler_jumpToImage(XImageIOHandler* self, int imageNumber)
{
    (void)self;
    (void)imageNumber;
    return false;
}

static int VXImageIOHandler_loopCount(const XImageIOHandler* self)
{
    (void)self;
    return 0;
}

static int VXImageIOHandler_imageCount(const XImageIOHandler* self)
{
    return XImageIOHandler_canRead_base(self) ? 1 : 0;
}

static int VXImageIOHandler_nextImageDelay(const XImageIOHandler* self)
{
    (void)self;
    return 0;
}

static int VXImageIOHandler_currentImageNumber(const XImageIOHandler* self)
{
    (void)self;
    return 0;
}

static void VXImageIOHandler_currentImageRect(const XImageIOHandler* self, XRect* out)
{
    (void)self;
    if (out) memset(out, 0, sizeof(XRect));
}

/* ========== 虚函数表初始化 ========== */

XVtable* XImageIOHandler_class_init()
{
    XVTABLE_INIT_DEFAULT(XImageIOHandler)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageIOHandler_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_CanRead, VXImageIOHandler_canRead);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Read, VXImageIOHandler_read);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Write, VXImageIOHandler_write);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Option, VXImageIOHandler_option);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_SetOption, VXImageIOHandler_setOption);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_SupportsOption, VXImageIOHandler_supportsOption);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_JumpToNextImage, VXImageIOHandler_jumpToNextImage);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_JumpToImage, VXImageIOHandler_jumpToImage);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_LoopCount, VXImageIOHandler_loopCount);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_ImageCount, VXImageIOHandler_imageCount);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_NextImageDelay, VXImageIOHandler_nextImageDelay);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_CurrentImageNumber, VXImageIOHandler_currentImageNumber);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_CurrentImageRect, VXImageIOHandler_currentImageRect);
    return XVTABLE_DEFAULT;
}

XImageIOHandler* XImageIOHandler_create()
{
    XImageIOHandler* self = (XImageIOHandler*)XMalloc_System(sizeof(XImageIOHandler));
    if (!self) return NULL;
    XImageIOHandler_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XImageIOHandler_init(XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler")) return;
    memset(self, 0, sizeof(XImageIOHandler));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XImageIOHandler);
    self->m_data = (XImageIOHandlerPrivate*)XMalloc_System(sizeof(XImageIOHandlerPrivate));
    if (self->m_data) memset(self->m_data, 0, sizeof(XImageIOHandlerPrivate));
}

void XImageIOHandler_deinit(XImageIOHandler* self) { XImageIOHandler_deinit_base(self); }

void XImageIOHandler_deinit_base(XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XImageIOHandler*))(self);
}

void XImageIOHandler_setDevice(XImageIOHandler* self, XIODevice* device)
{
    if (self && self->m_data) self->m_data->m_device = device;
}

XIODevice* XImageIOHandler_device(const XImageIOHandler* self)
{
    return (self && self->m_data) ? self->m_data->m_device : NULL;
}

void XImageIOHandler_setFormat(XImageIOHandler* self, const char* format)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_format) XFree_System(self->m_data->m_format);
    self->m_data->m_format = format ? (char*)XMalloc_System(strlen(format) + 1) : NULL;
    if (format && self->m_data->m_format) strcpy(self->m_data->m_format, format);
}

const char* XImageIOHandler_format(const XImageIOHandler* self)
{
    return (self && self->m_data) ? self->m_data->m_format : NULL;
}

bool XImageIOHandler_canRead_base(const XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return false;
    return XClassGetVirtualFunc(self, EXImageIOHandler_CanRead, bool(*)(const XImageIOHandler*))(self);
}

bool XImageIOHandler_read_base(XImageIOHandler* self, XImage* image)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable") || ISNULL(image, "XImage")) return false;
    return XClassGetVirtualFunc(self, EXImageIOHandler_Read, bool(*)(XImageIOHandler*, XImage*))(self, image);
}

bool XImageIOHandler_write_base(XImageIOHandler* self, const XImage* image)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable") ||
        ISNULL(image, "XImage")) return false;
    return XClassGetVirtualFunc(self, EXImageIOHandler_Write, bool(*)(XImageIOHandler*, const XImage*))(self, image);
}

bool XImageIOHandler_option_base(const XImageIOHandler* self, XImageIOHandlerOption option, void* out)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return false;
    return XClassGetVirtualFunc(self, EXImageIOHandler_Option, bool(*)(const XImageIOHandler*, XImageIOHandlerOption, void*))(self, option, out);
}

void XImageIOHandler_setOption_base(XImageIOHandler* self, XImageIOHandlerOption option, const void* value)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXImageIOHandler_SetOption, void(*)(XImageIOHandler*, XImageIOHandlerOption, const void*))(self, option, value);
}

bool XImageIOHandler_supportsOption_base(const XImageIOHandler* self, XImageIOHandlerOption option)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return false;
    return XClassGetVirtualFunc(self, EXImageIOHandler_SupportsOption, bool(*)(const XImageIOHandler*, XImageIOHandlerOption))(self, option);
}

bool XImageIOHandler_jumpToNextImage_base(XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return false;
    return XClassGetVirtualFunc(self, EXImageIOHandler_JumpToNextImage, bool(*)(XImageIOHandler*))(self);
}

bool XImageIOHandler_jumpToImage_base(XImageIOHandler* self, int imageNumber)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return false;
    return XClassGetVirtualFunc(self, EXImageIOHandler_JumpToImage, bool(*)(XImageIOHandler*, int))(self, imageNumber);
}

int XImageIOHandler_loopCount_base(const XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return 0;
    return XClassGetVirtualFunc(self, EXImageIOHandler_LoopCount, int(*)(const XImageIOHandler*))(self);
}

int XImageIOHandler_imageCount_base(const XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return 0;
    return XClassGetVirtualFunc(self, EXImageIOHandler_ImageCount, int(*)(const XImageIOHandler*))(self);
}

int XImageIOHandler_nextImageDelay_base(const XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return 0;
    return XClassGetVirtualFunc(self, EXImageIOHandler_NextImageDelay, int(*)(const XImageIOHandler*))(self);
}

int XImageIOHandler_currentImageNumber_base(const XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return 0;
    return XClassGetVirtualFunc(self, EXImageIOHandler_CurrentImageNumber, int(*)(const XImageIOHandler*))(self);
}

void XImageIOHandler_currentImageRect_base(const XImageIOHandler* self, XRect* out)
{
    if (ISNULL(self, "XImageIOHandler") || ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXImageIOHandler_CurrentImageRect, void(*)(const XImageIOHandler*, XRect*))(self, out);
}

bool XImageIOHandler_allocateImage(const XSize* size, XImageFormat format, XImage* image)
{
    if (!size || !image) return false;
    XImage_init_ex(image, size->width, size->height, format);
    return !XImage_isNull(image);
}
