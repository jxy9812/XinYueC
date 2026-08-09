/******************************************************************************
 * @file       XImageWriter.c
 * @brief      XImageWriter 图像写入器实现（对标 Qt 6.8 QImageWriter）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageWriter.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XVector.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

/* Keep writer discovery aligned with the only portable codec implemented in
   this module.  Entries are static strings stored in a newly allocated
   XVector returned to each caller. */
static const char* const g_imageWriterFormats[] = { "bmp" };
static const char* const g_imageWriterMimeTypes[] = { "image/bmp" };

static XVector* XImageWriter_makeStringList(const char* const* values, size_t count)
{
    XVector* result = XVector_create(sizeof(const char*));
    if (!result) return NULL;
    for (size_t i = 0; i < count; ++i) {
        const char* value = values[i];
        if (!XVector_push_back_1_base(result, &value)) {
            XVector_delete_base(result);
            return NULL;
        }
    }
    return result;
}

static bool XImageWriter_mimeIsBmp(const char* mimeType)
{
    const char* expected = "image/bmp";
    size_t i;
    if (!mimeType) return false;
    for (i = 0; expected[i] && mimeType[i]; ++i) {
        if (tolower((unsigned char)mimeType[i]) !=
            tolower((unsigned char)expected[i])) return false;
    }
    return expected[i] == '\0' && mimeType[i] == '\0';
}

/**
 * @brief      XImageWriter 私有数据
 */
typedef struct XImageWriterPrivate
{
    XIODevice*  m_device;            /**< IO 设备 */
    char*       m_fileName;          /**< 文件名 */
    char*       m_format;            /**< 格式字符串 */
    int         m_quality;           /**< 质量参数 */
    int         m_compression;       /**< 压缩参数 */
    char*       m_subType;           /**< 子类型 */
    bool        m_optimizedWrite;    /**< 是否优化写入 */
    bool        m_progressiveScan;   /**< 是否渐进式扫描 */
    XImageIOHandlerTransformation m_transformation; /**< 变换类型 */
    XImageWriterError m_error;       /**< 错误码 */
    char*       m_errorString;       /**< 错误描述 */
}XImageWriterPrivate;

static void XImageWriter_setError(XImageWriter* self, XImageWriterError error, const char* message)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_errorString) XFree_System(self->m_data->m_errorString);
    self->m_data->m_errorString = NULL;
    self->m_data->m_error = error;
    if (message)
    {
        self->m_data->m_errorString = (char*)XMalloc_System(strlen(message) + 1);
        if (self->m_data->m_errorString) strcpy(self->m_data->m_errorString, message);
    }
}

static bool XImageWriter_isBmpFormat(const char* format)
{
    if (!format || !format[0]) return true;
    return strlen(format) == 3 &&
           tolower((unsigned char)format[0]) == 'b' &&
           tolower((unsigned char)format[1]) == 'm' &&
           tolower((unsigned char)format[2]) == 'p';
}

static bool XImageWriter_fileLooksBmp(const char* fileName)
{
    const char* dot;
    if (!fileName) return false;
    dot = strrchr(fileName, '.');
    return dot && XImageWriter_isBmpFormat(dot + 1);
}

static void XImageWriter_writeLe16(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)(value >> 8);
}

static void XImageWriter_writeLe32(uint8_t* out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)(value >> 24);
}

static bool XImageWriter_encodeBmp(const XImage* image, XByteArray* bytes)
{
    XImage converted;
    bool withAlpha;
    uint16_t bpp;
    uint64_t rowBytes;
    uint64_t imageBytes;
    uint64_t totalBytes;
    uint8_t* data;

    if (!image || !bytes || XImage_isNull(image)) return false;
    withAlpha = XImage_hasAlphaChannel(image);
    XImage_init(&converted);
    XImage_convertToFormat(image, withAlpha ? XImageFormat_ARGB32 : XImageFormat_RGB888,
                           0, &converted);
    if (XImage_isNull(&converted)) return false;

    bpp = withAlpha ? 32u : 24u;
    rowBytes = (((uint64_t)(uint32_t)XImage_width(&converted) * bpp) + 31u) / 32u * 4u;
    imageBytes = rowBytes * (uint32_t)XImage_height(&converted);
    totalBytes = 54u + imageBytes;
    if (imageBytes > UINT32_MAX - 54u || totalBytes > SIZE_MAX) {
        XImage_deinit_base(&converted);
        return false;
    }
    if (!XByteArray_resize_base(bytes, (size_t)totalBytes)) {
        XImage_deinit_base(&converted);
        return false;
    }

    data = (uint8_t*)XByteArray_data(bytes);
    memset(data, 0, (size_t)totalBytes);
    data[0] = 'B'; data[1] = 'M';
    XImageWriter_writeLe32(data + 2, (uint32_t)totalBytes);
    XImageWriter_writeLe32(data + 10, 54u);
    XImageWriter_writeLe32(data + 14, 40u);
    XImageWriter_writeLe32(data + 18, (uint32_t)XImage_width(&converted));
    XImageWriter_writeLe32(data + 22, (uint32_t)XImage_height(&converted));
    XImageWriter_writeLe16(data + 26, 1u);
    XImageWriter_writeLe16(data + 28, bpp);
    XImageWriter_writeLe32(data + 34, (uint32_t)imageBytes);

    for (int y = 0; y < XImage_height(&converted); ++y) {
        int sourceY = XImage_height(&converted) - 1 - y;
        uint8_t* row = data + 54u + (size_t)y * (size_t)rowBytes;
        for (int x = 0; x < XImage_width(&converted); ++x) {
            uint32_t pixel = XImage_pixel(&converted, x, sourceY);
            if (withAlpha) {
                row[x * 4] = (uint8_t)(pixel & 0xffu);
                row[x * 4 + 1] = (uint8_t)((pixel >> 8) & 0xffu);
                row[x * 4 + 2] = (uint8_t)((pixel >> 16) & 0xffu);
                row[x * 4 + 3] = (uint8_t)(pixel >> 24);
            } else {
                row[x * 3] = (uint8_t)(pixel & 0xffu);
                row[x * 3 + 1] = (uint8_t)((pixel >> 8) & 0xffu);
                row[x * 3 + 2] = (uint8_t)((pixel >> 16) & 0xffu);
            }
        }
    }
    XImage_deinit_base(&converted);
    return true;
}

static bool XImageWriter_writeDevice(XIODevice* device, const XByteArray* bytes)
{
    const char* data;
    int64_t total;
    int64_t offset = 0;
    if (!device || !bytes) return false;
    data = (const char*)XByteArray_constData(bytes);
    total = (int64_t)XByteArray_size_base(bytes);
    while (offset < total) {
        int64_t written = XIODevice_write_1(device, data + offset, total - offset);
        if (written <= 0) return false;
        offset += written;
    }
    return XIODevice_flush(device);
}

static void VXImageWriter_deinit(XImageWriter* self)
{
    if (ISNULL(self, "XImageWriter")) return;
    if (self->m_data)
    {
        if (self->m_data->m_fileName) XFree_System(self->m_data->m_fileName);
        if (self->m_data->m_format) XFree_System(self->m_data->m_format);
        if (self->m_data->m_subType) XFree_System(self->m_data->m_subType);
        if (self->m_data->m_errorString) XFree_System(self->m_data->m_errorString);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
}

XVtable* XImageWriter_class_init()
{
    XVTABLE_INIT_DEFAULT(XImageWriter)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageWriter_deinit);
    return XVTABLE_DEFAULT;
}

XImageWriter* XImageWriter_create()
{
    XImageWriter* self = (XImageWriter*)XMalloc_System(sizeof(XImageWriter));
    if (!self) return NULL;
    XImageWriter_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XImageWriter_init(XImageWriter* self)
{
    if (ISNULL(self, "XImageWriter")) return;
    memset(self, 0, sizeof(XImageWriter));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XImageWriter);
    self->m_data = (XImageWriterPrivate*)XMalloc_System(sizeof(XImageWriterPrivate));
    if (self->m_data)
    {
        memset(self->m_data, 0, sizeof(XImageWriterPrivate));
        self->m_data->m_quality = -1;
        self->m_data->m_compression = -1;
        self->m_data->m_error = XImageWriterError_UnknownError;
    }
}

void XImageWriter_init_device(XImageWriter* self, XIODevice* device, const char* format)
{
    XImageWriter_init(self);
    if (self->m_data)
    {
        self->m_data->m_device = device;
        if (format && format[0])
        {
            self->m_data->m_format = (char*)XMalloc_System(strlen(format) + 1);
            if (self->m_data->m_format) strcpy(self->m_data->m_format, format);
        }
    }
}

void XImageWriter_init_file(XImageWriter* self, const char* fileName, const char* format)
{
    XImageWriter_init(self);
    if (self->m_data && fileName)
    {
        self->m_data->m_fileName = (char*)XMalloc_System(strlen(fileName) + 1);
        if (self->m_data->m_fileName) strcpy(self->m_data->m_fileName, fileName);
        if (format && format[0])
        {
            self->m_data->m_format = (char*)XMalloc_System(strlen(format) + 1);
            if (self->m_data->m_format) strcpy(self->m_data->m_format, format);
        }
    }
}

void XImageWriter_deinit(XImageWriter* self) { XImageWriter_deinit_base(self); }
void XImageWriter_deinit_base(XImageWriter* self)
{
    if (ISNULL(self, "XImageWriter") || ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XImageWriter*))(self);
}

void XImageWriter_setFormat(XImageWriter* self, const char* format)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_format) XFree_System(self->m_data->m_format);
    self->m_data->m_format = format ? (char*)XMalloc_System(strlen(format) + 1) : NULL;
    if (format && self->m_data->m_format) strcpy(self->m_data->m_format, format);
}

const char* XImageWriter_format(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_format : NULL; }

void XImageWriter_setDevice(XImageWriter* self, XIODevice* device)
{
    if (!self || !self->m_data) return;
    self->m_data->m_device = device;
    if (self->m_data->m_fileName) XFree_System(self->m_data->m_fileName);
    self->m_data->m_fileName = NULL;
}

XIODevice* XImageWriter_device(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_device : NULL; }

void XImageWriter_setFileName(XImageWriter* self, const char* fileName)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_fileName) XFree_System(self->m_data->m_fileName);
    self->m_data->m_fileName = fileName ? (char*)XMalloc_System(strlen(fileName) + 1) : NULL;
    if (fileName && self->m_data->m_fileName) strcpy(self->m_data->m_fileName, fileName);
    self->m_data->m_device = NULL;
}

const char* XImageWriter_fileName(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_fileName : NULL; }

void XImageWriter_setQuality(XImageWriter* self, int quality)
{ if (self && self->m_data) self->m_data->m_quality = quality; }

int XImageWriter_quality(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_quality : -1; }

void XImageWriter_setCompression(XImageWriter* self, int compression)
{ if (self && self->m_data) self->m_data->m_compression = compression; }

int XImageWriter_compression(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_compression : -1; }

void XImageWriter_setSubType(XImageWriter* self, const char* type)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_subType) XFree_System(self->m_data->m_subType);
    self->m_data->m_subType = type ? (char*)XMalloc_System(strlen(type) + 1) : NULL;
    if (type && self->m_data->m_subType) strcpy(self->m_data->m_subType, type);
}

const char* XImageWriter_subType(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_subType : NULL; }

void* XImageWriter_supportedSubTypes(const XImageWriter* self) { (void)self; return NULL; }

void XImageWriter_setOptimizedWrite(XImageWriter* self, bool optimize)
{ if (self && self->m_data) self->m_data->m_optimizedWrite = optimize; }

bool XImageWriter_optimizedWrite(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_optimizedWrite : false; }

void XImageWriter_setProgressiveScanWrite(XImageWriter* self, bool progressive)
{ if (self && self->m_data) self->m_data->m_progressiveScan = progressive; }

bool XImageWriter_progressiveScanWrite(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_progressiveScan : false; }

XImageIOHandlerTransformation XImageWriter_transformation(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_transformation : XImageIOHandlerTransformation_None; }

void XImageWriter_setTransformation(XImageWriter* self, XImageIOHandlerTransformation orientation)
{ if (self && self->m_data) self->m_data->m_transformation = orientation; }

void XImageWriter_setText(XImageWriter* self, const char* key, const char* text)
{ (void)self; (void)key; (void)text; }

bool XImageWriter_canWrite(const XImageWriter* self)
{
    if (!self || !self->m_data) return false;
    if (!self->m_data->m_fileName && !self->m_data->m_device) return false;
    if (!XImageWriter_isBmpFormat(self->m_data->m_format)) return false;
    return self->m_data->m_fileName ?
        (self->m_data->m_format && self->m_data->m_format[0] ? true :
         XImageWriter_fileLooksBmp(self->m_data->m_fileName)) :
        (self->m_data->m_format && self->m_data->m_format[0]);
}

bool XImageWriter_write(XImageWriter* self, const XImage* image)
{
    if (!self || !self->m_data || !image || XImage_isNull(image))
    {
        if (self && self->m_data)
            XImageWriter_setError(self, XImageWriterError_InvalidImageError, "Cannot write a null image");
        return false;
    }
    if (!self->m_data->m_fileName && !self->m_data->m_device)
        XImageWriter_setError(self, XImageWriterError_DeviceError, "No image destination is set");
    else if (!self->m_data->m_fileName) {
        XByteArray* bytes = XByteArray_create();
        bool ok = XImageWriter_canWrite(self) && XImageWriter_encodeBmp(image, bytes) &&
                  XImageWriter_writeDevice(self->m_data->m_device, bytes);
        if (bytes) XByteArray_delete_base(bytes);
        if (!ok)
            XImageWriter_setError(self, XImageWriterError_DeviceError,
                                  "BMP image could not be written to the device");
        else {
            XImageWriter_setError(self, XImageWriterError_UnknownError, NULL);
            return true;
        }
    }
    else if (!XImageWriter_canWrite(self))
        XImageWriter_setError(self, XImageWriterError_UnsupportedFormatError,
                              "Only BMP is supported by the built-in portable encoder");
    else if (!XImage_save(image, self->m_data->m_fileName, self->m_data->m_format, self->m_data->m_quality))
        XImageWriter_setError(self, XImageWriterError_DeviceError, "BMP image could not be written");
    else {
        XImageWriter_setError(self, XImageWriterError_UnknownError, NULL);
        return true;
    }
    return false;
}

XImageWriterError XImageWriter_error(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_error : XImageWriterError_UnknownError; }

const char* XImageWriter_errorString(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_errorString : NULL; }

bool XImageWriter_supportsOption(const XImageWriter* self, XImageIOHandlerOption option)
{
    (void)self;
    (void)option;
    return false;
}

void* XImageWriter_supportedImageFormats()
{
    return XImageWriter_makeStringList(g_imageWriterFormats,
                                       sizeof(g_imageWriterFormats) /
                                       sizeof(g_imageWriterFormats[0]));
}

void* XImageWriter_supportedMimeTypes()
{
    return XImageWriter_makeStringList(g_imageWriterMimeTypes,
                                       sizeof(g_imageWriterMimeTypes) /
                                       sizeof(g_imageWriterMimeTypes[0]));
}

void* XImageWriter_imageFormatsForMimeType(const char* mimeType)
{
    if (XImageWriter_mimeIsBmp(mimeType))
        return XImageWriter_makeStringList(g_imageWriterFormats,
                                           sizeof(g_imageWriterFormats) /
                                           sizeof(g_imageWriterFormats[0]));
    return XImageWriter_makeStringList(NULL, 0);
}
