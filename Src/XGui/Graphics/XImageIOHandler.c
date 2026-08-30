/******************************************************************************
 * @file       XImageIOHandler.c
 * @brief      XImageIOHandler 图像 IO 处理器基类实现（对标 Qt 6.8 QImageIOHandler）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageIOHandler.h"
#include "XImageReader.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <limits.h>

/**
 * @brief      XImageIOHandler 私有数据
 */
typedef struct XImageIOHandlerPrivate
{
    XIODevice*  m_device;   /**< IO 设备 */
    XString*    m_format;   /**< 格式字符串（UTF-8） */
    uint32_t m_optionMask;
    XImageIOHandlerOptionValue m_options[XImageIOHandlerOption_ImageTransformation + 1];
}XImageIOHandlerPrivate;

static void VXImageIOHandler_deinit(XImageIOHandler* self)
{
    if (ISNULL(self, "XImageIOHandler")) return;
    if (self->m_data)
    {
        if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
}

static bool XImageIOHandler_optionValid(XImageIOHandlerOption option)
{
    return option >= XImageIOHandlerOption_Size &&
           option <= XImageIOHandlerOption_ImageTransformation;
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
    void* table[] = { (void*)VXImageIOHandler_canRead,
                      (void*)VXImageIOHandler_read,
                      (void*)VXImageIOHandler_write,
                      (void*)VXImageIOHandler_option,
                      (void*)VXImageIOHandler_setOption,
                      (void*)VXImageIOHandler_supportsOption,
                      (void*)VXImageIOHandler_jumpToNextImage,
                      (void*)VXImageIOHandler_jumpToImage,
                      (void*)VXImageIOHandler_loopCount,
                      (void*)VXImageIOHandler_imageCount,
                      (void*)VXImageIOHandler_nextImageDelay,
                      (void*)VXImageIOHandler_currentImageNumber,
                      (void*)VXImageIOHandler_currentImageRect };
    XVTABLE_INIT_DEFAULT(XImageIOHandler)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageIOHandler_deinit);
    return XVTABLE_DEFAULT;
}

XImageIOHandler* XImageIOHandler_create_ex(XMemoryType memory)
{
    XImageIOHandler* self = (XImageIOHandler*)XMemory_malloc(sizeof(XImageIOHandler), memory);
    if (!self) return NULL;
    XImageIOHandler_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
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

void XImageIOHandler_setDevice(XImageIOHandler* self, XIODevice* device)
{
    if (self && self->m_data) self->m_data->m_device = device;
}

XIODevice* XImageIOHandler_device(const XImageIOHandler* self)
{
    return (self && self->m_data) ? self->m_data->m_device : NULL;
}

void XImageIOHandler_setFormat(XImageIOHandler* self, const XString* format)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
    self->m_data->m_format = format ? XString_create_copy(format) : NULL;
}

void XImageIOHandler_setFormat_2(XImageIOHandler* self, const char* format)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    XImageIOHandler_setFormat(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageIOHandler_format_const(const XImageIOHandler* self)
{
    return (self && self->m_data) ? self->m_data->m_format : NULL;
}

XString* XImageIOHandler_format(const XImageIOHandler* self)
{
    const XString* value = XImageIOHandler_format_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImageIOHandler_format_2(const XImageIOHandler* self)
{
    return XString_toUtf8(XImageIOHandler_format_const(self));
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

bool XImageIOHandler_optionValue(const XImageIOHandler* self,
                                 XImageIOHandlerOption option,
                                 XImageIOHandlerOptionValue* out)
{
    if (!self || !self->m_data || !out || !XImageIOHandler_optionValid(option) ||
        !(self->m_data->m_optionMask & ((uint32_t)1u << (unsigned)option)))
        return false;
    *out = self->m_data->m_options[option];
    return true;
}

void XImageIOHandler_storeOptionValue(XImageIOHandler* self,
                                       XImageIOHandlerOption option,
                                       const void* value)
{
    if (!self || !self->m_data || !value || !XImageIOHandler_optionValid(option))
        return;
    self->m_data->m_options[option] = *(const XImageIOHandlerOptionValue*)value;
    self->m_data->m_optionMask |= (uint32_t)1u << (unsigned)option;
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

/*
 * 按 Qt 6.8 QImageData::calculateImageParameters() 的规则计算图像分配量。
 * QImageIOHandler::allocateImage() 使用不低于 32 位的有效深度检查限制，
 * 即使目标格式本身只有 1/8 位，也必须按 GUI 最终常用的 32 位估算。
 * 同时保留 XImage 的 int 行跨度/总字节数边界，避免通过检查后在本地
 * XImageData_create() 中发生截断。依据 qimageiohandler.cpp:532-557、
 * qimage_p.h:88-115。
 */
static bool XImageIOHandler_calculateAllocation(const XSize* size,
                                                XImageFormat format,
                                                uint64_t* totalSize)
{
    uint64_t depth;
    uint64_t bitsPerLine;
    uint64_t bytesPerLine;
    uint64_t actualBytesPerLine;
    uint64_t actualTotalSize;

    if (!size || !totalSize || size->width <= 0 || size->height <= 0 ||
        format <= XImageFormat_Invalid || format >= XImageFormat_NImageFormats)
        return false;

    depth = (uint64_t)XImageFormat_bitDepth(format);
    if (depth < 32u) depth = 32u;
    /* 对齐 Qt 的 width > (INT_MAX - 31) / depth 溢出拒绝分支。 */
    if (depth == 0u || (uint64_t)size->width >
        ((uint64_t)INT_MAX - 31u) / depth)
        return false;
    bitsPerLine = (uint64_t)size->width * depth;
    if (bitsPerLine > UINT64_MAX - 31u)
        return false;
    bytesPerLine = ((bitsPerLine + 31u) >> 5) << 2;
    if (bytesPerLine == 0u || (uint64_t)size->height >
        UINT64_MAX / bytesPerLine)
        return false;
    *totalSize = (uint64_t)size->height * bytesPerLine;

    /* XImageData 使用 int 保存行跨度和 sizeInBytes，先拒绝本实现无法
       表示的实际格式存储量；这不会放宽 Qt 的 allocationLimit 检查。 */
    actualBytesPerLine = (uint64_t)XImageFormat_bytesPerLine(size->width, format);
    if (actualBytesPerLine == 0u || (uint64_t)size->height >
        (uint64_t)INT_MAX / actualBytesPerLine)
        return false;
    actualTotalSize = (uint64_t)size->height * actualBytesPerLine;
    return actualTotalSize > 0u && actualTotalSize <= (uint64_t)INT_MAX;
}

bool XImageIOHandler_allocateImage(const XSize* size, XImageFormat format, XImage* image)
{
    uint64_t totalSize;
    int allocationLimitMb;
    uint64_t limitBytes;

    if (!size || !image || !XImageIOHandler_calculateAllocation(size, format,
                                                                  &totalSize))
        return false;

    /* 与 Qt 相同：同尺寸同格式只 detach，避免无谓地重建像素缓冲区；
       复用路径不重新应用 allocationLimit，因为没有新的分配。 */
    if (XImage_width(image) == size->width &&
        XImage_height(image) == size->height &&
        XImage_format(image) == format) {
        XImage_detach(image);
        return !XImage_isNull(image);
    }

    allocationLimitMb = XImageReader_allocationLimit();
    if (allocationLimitMb > 0) {
        limitBytes = (uint64_t)(unsigned)allocationLimitMb * (uint64_t)1024u *
                     (uint64_t)1024u;
        /* Qt 以 totalSize >> 20 和余数比较；直接比较字节数等价且不会
           在恰好 MB 边界时误放行。 */
        if (totalSize > limitBytes)
            return false;
    }

    /* Qt 只有通过全部参数与限制校验后才替换输出图像，失败时保留
       调用方原有内容。XImage 的 C 输出对象通常已初始化，释放旧数据
       后再建立新对象，避免覆盖 m_data 造成泄漏。 */
    XImage_deinit_base(image);
    XImage_init_ex(image, size->width, size->height, format);
    return !XImage_isNull(image);
}
