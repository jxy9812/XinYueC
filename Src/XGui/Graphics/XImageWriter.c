/******************************************************************************
 * @file       XImageWriter.c
 * @brief      XImageWriter 图像写入器实现（对标 Qt 6.8 QImageWriter）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageWriter.h"
#include "XImageCodec.h"
#include "XImagePluginRegistry.h"
#include "XFile.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XStringList.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

/* Keep writer discovery aligned with XImageCodec capabilities. */
static const char* const g_imageWriterFormats[] = { "bmp", "png", "jpeg", "gif", "svg" };
static const char* const g_imageWriterMimeTypes[] = { "image/bmp", "image/png", "image/jpeg", "image/gif", "image/svg+xml" };

static XStringList* XImageWriter_makeStringList(const char* const* values, size_t count)
{
    XStringList* result = XStringList_create();
    if (!result) return NULL;
    for (size_t i = 0; i < count; ++i) {
        XStringList_push_back_utf8(result, values[i]);
    }
    return result;
}

static void XImageWriter_makeStringList_prefix(XStringList* result, const char* const* values, size_t count)
{
    size_t i;
    if (!result || !values) return;
    for (i = 0; i < count; ++i) {
        if (values[i] && values[i][0] &&
            !XStringList_contains_utf8(result, values[i], XChar_CaseInsensitive))
            XStringList_push_back_utf8(result, values[i]);
    }
}

static XStringList* XImageWriter_supportedFormats(void)
{
    XStringList* result = XStringList_create();
    size_t i;
    if (!result) return NULL;
#if XIMAGECODEC_ON
    for (i = 0; i < sizeof(g_imageWriterFormats) / sizeof(g_imageWriterFormats[0]); ++i)
        if (XImageCodec_canEncode(XImageCodec_formatFromName_2(g_imageWriterFormats[i])))
            XStringList_push_back_utf8(result, g_imageWriterFormats[i]);
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_supportedImageFormats(false);
        int64_t n;
        int64_t j;
        if (plugin) {
            n = XStringList_size_base((XContainer*)plugin);
            for (j = 0; j < n; ++j) {
                XString* item = (XString*)XStringList_at_base((XVector*)plugin, j);
                const char* value = XString_toUtf8(item);
                if (value && value[0] &&
                    !XStringList_contains_utf8(result, value, XChar_CaseInsensitive))
                    XStringList_push_back_utf8(result, value);
            }
            XStringList_delete_base((XClass*)plugin);
        }
    }
#endif
    return result;
}

static bool XImageWriter_mimeEquals(const char* mimeType, const char* expected)
{
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
    XIODevice*  m_fileDevice;        /**< 由写入器内部打开的文件设备 */
    XString*    m_fileName;          /**< 文件名（UTF-8） */
    XString*    m_format;            /**< 格式字符串（UTF-8） */
    XImageIOHandler* m_handler;      /**< 当前插件处理器 */
    int         m_quality;           /**< 质量参数 */
    int         m_compression;       /**< 压缩参数 */
    XString*    m_subType;           /**< 子类型 */
    bool        m_optimizedWrite;    /**< 是否优化写入 */
    bool        m_progressiveScan;   /**< 是否渐进式扫描 */
    XImageIOHandlerTransformation m_transformation; /**< 变换类型 */
    XImageWriterError m_error;       /**< 错误码 */
    XString*    m_errorString;       /**< 错误描述 */
}XImageWriterPrivate;

static void XImageWriter_releaseHandler(XImageWriterPrivate* data)
{
    if (!data) return;
    if (data->m_handler) XImageIOHandler_delete_base(data->m_handler);
    data->m_handler = NULL;
    if (data->m_fileDevice) {
        if (XIODevice_isOpen(data->m_fileDevice)) XIODevice_close_base(data->m_fileDevice);
        XClass_delete_base((XClass*)data->m_fileDevice);
    }
    data->m_fileDevice = NULL;
}

static XString* XImageWriter_resolveFormatForHandler(const XImageWriter* self)
{
    const XString* format;
    if (!self || !self->m_data) return XString_create();
    format = self->m_data->m_format;
    if (format && !XContainer_isEmpty_base((const XContainer*)format))
        return XString_create_copy(format);
    if (self->m_data->m_fileName) {
        const char* fileName = XString_toUtf8(self->m_data->m_fileName);
        const char* dot = fileName ? strrchr(fileName, '.') : NULL;
        if (dot && dot[1]) return XString_create_utf8(dot + 1);
    }
    return XString_create();
}

static XImageIOHandler* XImageWriter_ensureHandler(XImageWriter* self)
{
    XImageWriterPrivate* data;
    XString* format;
    if (!self || !(data = self->m_data)) return NULL;
    if (data->m_handler) return data->m_handler;
#if XIMAGEIOPLUGIN_ON
    format = XImageWriter_resolveFormatForHandler(self);
    if (!format) return NULL;
    if (!XContainer_isEmpty_base((const XContainer*)format)) {
        if (data->m_device) {
            data->m_handler = XImagePluginRegistry_createWriteHandler(data->m_device, format);
            XString_delete_base((XClass*)format);
            return data->m_handler;
        }
        if (data->m_fileName) {
            XFile* file = XFile_create_2(data->m_fileName);
            if (file && XIODevice_open_base((XIODevice*)file,
                        XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create)) {
                data->m_handler = XImagePluginRegistry_createWriteHandler((XIODevice*)file, format);
                if (data->m_handler) {
                    data->m_fileDevice = (XIODevice*)file;
                    XString_delete_base((XClass*)format);
                    return data->m_handler;
                }
                XIODevice_close_base((XIODevice*)file);
            }
            if (file) XClass_delete_base((XClass*)file);
        }
    }
    XString_delete_base((XClass*)format);
#else
    (void)self;
    (void)format;
#endif
    return NULL;
}

static void XImageWriter_applyHandlerSettings(XImageIOHandler* handler,
                                              const XImageWriterPrivate* data)
{
    XImageIOHandlerOptionValue value;
    if (!handler || !data) return;
    memset(&value, 0, sizeof(value));
    if (data->m_quality >= 0) {
        value.integer = data->m_quality;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_Quality, &value);
    }
    if (data->m_compression >= 0) {
        memset(&value, 0, sizeof(value));
        value.integer = data->m_compression;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_CompressionRatio, &value);
    }
    if (data->m_subType && !XContainer_isEmpty_base((const XContainer*)data->m_subType)) {
        memset(&value, 0, sizeof(value));
        value.string = data->m_subType;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_SubType, &value);
    }
    memset(&value, 0, sizeof(value));
    value.boolean = data->m_optimizedWrite;
    XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_OptimizedWrite, &value);
    memset(&value, 0, sizeof(value));
    value.boolean = data->m_progressiveScan;
    XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_ProgressiveScanWrite, &value);
    memset(&value, 0, sizeof(value));
    value.transformation = data->m_transformation;
    XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_ImageTransformation, &value);
}

static void XImageWriter_setError(XImageWriter* self, XImageWriterError error, const char* message)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_errorString) XString_delete_base((XClass*)self->m_data->m_errorString);
    self->m_data->m_errorString = NULL;
    self->m_data->m_error = error;
    if (message)
    {
        self->m_data->m_errorString = XString_create_utf8(message);
    }
}

static bool XImageWriter_isSupportedFormat(const char* format)
{
    if (!format || !format[0]) return false;
#if XIMAGECODEC_ON
    if (XImageCodec_canEncode(XImageCodec_formatFromName_2(format))) return true;
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XString* value = XString_create_utf8(format);
        bool supported = value && XImagePluginRegistry_supportsWriteFormat(value);
        if (value) XString_delete_base((XClass*)value);
        if (supported) return true;
    }
#endif
    return false;
}

static bool XImageWriter_fileLooksSupported(const char* fileName)
{
    const char* dot;
    if (!fileName) return false;
    dot = strrchr(fileName, '.');
    return dot && XImageWriter_isSupportedFormat(dot + 1);
}

static bool XImageWriter_applyTransformation(const XImage* image,
                                             XImageIOHandlerTransformation transformation,
                                             XImage* out)
{
    int sourceWidth;
    int sourceHeight;
    int width;
    int height;
    if (!image || !out || XImage_isNull(image)) return false;
    XImage_init(out);
    if (transformation == XImageIOHandlerTransformation_None) {
        XImage_copy_base(out, image);
        return !XImage_isNull(out);
    }
    sourceWidth = XImage_width(image);
    sourceHeight = XImage_height(image);
    if (sourceWidth <= 0 || sourceHeight <= 0) return false;
    width = sourceWidth;
    height = sourceHeight;
    if (transformation == XImageIOHandlerTransformation_Rotate90 ||
        transformation == XImageIOHandlerTransformation_MirrorAndRotate90 ||
        transformation == XImageIOHandlerTransformation_FlipAndRotate90 ||
        transformation == XImageIOHandlerTransformation_Rotate270) {
        width = sourceHeight;
        height = sourceWidth;
    }
    XImage_init_ex(out, width, height, XImageFormat_ARGB32);
    if (XImage_isNull(out)) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int sourceX = x;
            int sourceY = y;
            switch (transformation) {
            case XImageIOHandlerTransformation_Mirror:
                sourceX = sourceWidth - 1 - x;
                break;
            case XImageIOHandlerTransformation_Flip:
                sourceY = sourceHeight - 1 - y;
                break;
            case XImageIOHandlerTransformation_Rotate180:
                sourceX = sourceWidth - 1 - x;
                sourceY = sourceHeight - 1 - y;
                break;
            case XImageIOHandlerTransformation_Rotate90:
                sourceX = y;
                sourceY = sourceHeight - 1 - x;
                break;
            case XImageIOHandlerTransformation_MirrorAndRotate90:
                sourceX = sourceWidth - 1 - y;
                sourceY = sourceHeight - 1 - x;
                break;
            case XImageIOHandlerTransformation_FlipAndRotate90:
                sourceX = y;
                sourceY = x;
                break;
            case XImageIOHandlerTransformation_Rotate270:
                sourceX = sourceWidth - 1 - y;
                sourceY = x;
                break;
            default:
                XImage_deinit_base(out);
                return false;
            }
            XImage_setPixel(out, x, y, XImage_pixel(image, sourceX, sourceY));
        }
    }
    return true;
}

static bool XImageWriter_writeDevice(XIODevice* device, const XByteArray* bytes)
{
    const char* data;
    int64_t total;
    int64_t offset = 0;
    if (!device || !bytes) return false;
    data = (const char*)XByteArray_constData((XByteArray*)bytes);
    total = (int64_t)XByteArray_size_base((const XContainer*)bytes);
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
        XImageWriter_releaseHandler(self->m_data);
        if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
        if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
        if (self->m_data->m_subType) XString_delete_base((XClass*)self->m_data->m_subType);
        if (self->m_data->m_errorString) XString_delete_base((XClass*)self->m_data->m_errorString);
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

XImageWriter* XImageWriter_create_ex(XMemoryType memory)
{
    XImageWriter* self = (XImageWriter*)XMemory_malloc(sizeof(XImageWriter), memory);
    if (!self) return NULL;
    XImageWriter_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
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

void XImageWriter_init_device(XImageWriter* self, XIODevice* device, const XString* format)
{
    XImageWriter_init(self);
    if (self->m_data)
    {
        self->m_data->m_device = device;
        if (format && !XContainer_isEmpty_base((const XContainer*)format))
            self->m_data->m_format = XString_create_copy(format);
    }
}

void XImageWriter_init_device_2(XImageWriter* self, XIODevice* device, const char* format)
{
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XImageWriter_init_device(self, device, formatString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XImageWriter_init_file(XImageWriter* self, const XString* fileName, const XString* format)
{
    XImageWriter_init(self);
    if (self->m_data)
    {
        if (fileName) self->m_data->m_fileName = XString_create_copy(fileName);
        if (format && !XContainer_isEmpty_base((const XContainer*)format))
            self->m_data->m_format = XString_create_copy(format);
    }
}

void XImageWriter_init_file_2(XImageWriter* self, const char* fileName, const char* format)
{
    XString* fileNameString = fileName ? XString_create_utf8(fileName) : NULL;
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XImageWriter_init_file(self, fileNameString, formatString);
    if (fileNameString) XString_delete_base((XClass*)fileNameString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XImageWriter_setFormat(XImageWriter* self, const XString* format)
{
    if (!self || !self->m_data) return;
    XImageWriter_releaseHandler(self->m_data);
    if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
    self->m_data->m_format = format ? XString_create_copy(format) : NULL;
}

void XImageWriter_setFormat_2(XImageWriter* self, const char* format)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    XImageWriter_setFormat(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageWriter_format_const(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_format : NULL; }

XString* XImageWriter_format(const XImageWriter* self)
{
    const XString* value = XImageWriter_format_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImageWriter_format_2(const XImageWriter* self)
{ return XString_toUtf8(XImageWriter_format_const(self)); }

void XImageWriter_setDevice(XImageWriter* self, XIODevice* device)
{
    if (!self || !self->m_data) return;
    XImageWriter_releaseHandler(self->m_data);
    self->m_data->m_device = device;
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = NULL;
}

XIODevice* XImageWriter_device(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_device : NULL; }

void XImageWriter_setFileName(XImageWriter* self, const XString* fileName)
{
    if (!self || !self->m_data) return;
    XImageWriter_releaseHandler(self->m_data);
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = fileName ? XString_create_copy(fileName) : NULL;
    self->m_data->m_device = NULL;
}

void XImageWriter_setFileName_2(XImageWriter* self, const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL;
    XImageWriter_setFileName(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageWriter_fileName_const(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_fileName : NULL; }

XString* XImageWriter_fileName(const XImageWriter* self)
{
    const XString* value = XImageWriter_fileName_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImageWriter_fileName_2(const XImageWriter* self)
{ return XString_toUtf8(XImageWriter_fileName_const(self)); }

void XImageWriter_setQuality(XImageWriter* self, int quality)
{ if (self && self->m_data) self->m_data->m_quality = quality; }

int XImageWriter_quality(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_quality : -1; }

void XImageWriter_setCompression(XImageWriter* self, int compression)
{ if (self && self->m_data) self->m_data->m_compression = compression; }

int XImageWriter_compression(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_compression : -1; }

void XImageWriter_setSubType(XImageWriter* self, const XString* type)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_subType) XString_delete_base((XClass*)self->m_data->m_subType);
    self->m_data->m_subType = type ? XString_create_copy(type) : NULL;
}

void XImageWriter_setSubType_2(XImageWriter* self, const char* type)
{
    XString* value = type ? XString_create_utf8(type) : NULL;
    XImageWriter_setSubType(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageWriter_subType_const(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_subType : NULL; }

XString* XImageWriter_subType(const XImageWriter* self)
{
    const XString* value = XImageWriter_subType_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImageWriter_subType_2(const XImageWriter* self)
{ return XString_toUtf8(XImageWriter_subType_const(self)); }

XStringList* XImageWriter_supportedSubTypes(const XImageWriter* self)
{
    (void)self;
    return XImageWriter_makeStringList(NULL, 0);
}

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

void XImageWriter_setText(XImageWriter* self, const XString* key, const XString* text)
{
    /* The built-in BMP encoder has no metadata block.  Keep this setter
       intentionally side-effect free and report false from supportsOption;
       callers can therefore distinguish an accepted API surface from data
       that will actually be serialized. */
    (void)self; (void)key; (void)text;
}

void XImageWriter_setText_2(XImageWriter* self, const char* key, const char* text)
{
    XString* keyString = key ? XString_create_utf8(key) : NULL;
    XString* textString = text ? XString_create_utf8(text) : NULL;
    XImageWriter_setText(self, keyString, textString);
    if (keyString) XString_delete_base((XClass*)keyString);
    if (textString) XString_delete_base((XClass*)textString);
}

bool XImageWriter_canWrite(const XImageWriter* self)
{
    if (!self || !self->m_data) return false;
    if (!self->m_data->m_fileName && !self->m_data->m_device) return false;
    if (self->m_data->m_format && !XContainer_isEmpty_base((const XContainer*)self->m_data->m_format) &&
        !XImageWriter_isSupportedFormat(XString_toUtf8(self->m_data->m_format))) return false;
    return self->m_data->m_fileName ?
        (self->m_data->m_format && !XContainer_isEmpty_base((const XContainer*)self->m_data->m_format) ? true :
         XImageWriter_fileLooksSupported(XString_toUtf8(self->m_data->m_fileName))) :
        (self->m_data->m_format && !XContainer_isEmpty_base((const XContainer*)self->m_data->m_format));
}

bool XImageWriter_write(XImageWriter* self, const XImage* image)
{
    XImage transformed;
    const XImage* source = image;
    bool transformedInitialized = false;
    if (!self || !self->m_data || !image || XImage_isNull(image))
    {
        if (self && self->m_data)
            XImageWriter_setError(self, XImageWriterError_InvalidImageError, "Cannot write a null image");
        return false;
    }
    if (self->m_data->m_transformation != XImageIOHandlerTransformation_None) {
        if (!XImageWriter_applyTransformation(image, self->m_data->m_transformation, &transformed)) {
            XImageWriter_setError(self, XImageWriterError_UnsupportedFormatError,
                                  "The requested image transformation is unsupported");
            return false;
        }
        source = &transformed;
        transformedInitialized = true;
    }
    {
        bool wrote = false;
        XImageWriter_ensureHandler(self);
        if (self->m_data->m_handler) {
            XImageWriter_applyHandlerSettings(self->m_data->m_handler, self->m_data);
            wrote = XImageIOHandler_write_base(self->m_data->m_handler, source);
            if (wrote)
                wrote = XIODevice_flush(self->m_data->m_fileDevice ?
                                        self->m_data->m_fileDevice : self->m_data->m_device);
            XImageWriter_releaseHandler(self->m_data);
            if (wrote) {
                XImageWriter_setError(self, XImageWriterError_UnknownError, NULL);
                if (transformedInitialized) XImage_deinit_base(&transformed);
                return true;
            }
        }
        if (!self->m_data->m_fileName && !self->m_data->m_device) {
            XImageWriter_setError(self, XImageWriterError_DeviceError, "No image destination is set");
        } else if (!self->m_data->m_fileName) {
            XByteArray* bytes = XByteArray_create();
#if XIMAGECODEC_ON
            XImageCodecFormat format = XImageCodec_formatFromName(self->m_data->m_format);
            bool ok = XImageWriter_canWrite(self) && XImageCodec_encode(source, format, self->m_data->m_quality, bytes) &&
                      XImageWriter_writeDevice(self->m_data->m_device, bytes);
#else
            bool ok = false;
#endif
            if (bytes) XByteArray_delete_base((XClass*)bytes);
            if (!ok)
                XImageWriter_setError(self, XImageWriterError_DeviceError,
                                      "Image could not be written to the device");
            else
                wrote = true;
        } else if (!XImageWriter_canWrite(self)) {
            XImageWriter_setError(self, XImageWriterError_UnsupportedFormatError,
                                  "The requested image format has no built-in encoder");
        } else if (!XImage_save_2(source, XString_toUtf8(self->m_data->m_fileName),
                              XString_toUtf8(self->m_data->m_format), self->m_data->m_quality)) {
            XImageWriter_setError(self, XImageWriterError_DeviceError, "Image could not be written");
        } else {
            wrote = true;
        }
        if (wrote) XImageWriter_setError(self, XImageWriterError_UnknownError, NULL);
        if (transformedInitialized) XImage_deinit_base(&transformed);
        return wrote;
    }
}

XImageWriterError XImageWriter_error(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_error : XImageWriterError_UnknownError; }

XString* XImageWriter_errorString(const XImageWriter* self)
{
    const XString* value = XImageWriter_errorString_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const XString* XImageWriter_errorString_const(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_errorString : NULL; }

const char* XImageWriter_errorString_2(const XImageWriter* self)
{ return XString_toUtf8(XImageWriter_errorString_const(self)); }

bool XImageWriter_supportsOption(const XImageWriter* self, XImageIOHandlerOption option)
{
    (void)self;
    return option == XImageIOHandlerOption_ImageTransformation;
}

XStringList* XImageWriter_supportedImageFormats()
{
    return XImageWriter_supportedFormats();
}

XStringList* XImageWriter_supportedMimeTypes()
{
    XStringList* result = XStringList_create();
    size_t i;
    if (!result) return NULL;
#if XIMAGECODEC_ON
    for (i = 0; i < sizeof(g_imageWriterFormats) / sizeof(g_imageWriterFormats[0]); ++i)
        if (XImageCodec_canEncode(XImageCodec_formatFromName_2(g_imageWriterFormats[i])))
            XStringList_push_back_utf8(result, g_imageWriterMimeTypes[i]);
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_supportedMimeTypes(false);
        int64_t n;
        int64_t j;
        if (plugin) {
            n = XStringList_size_base((XContainer*)plugin);
            for (j = 0; j < n; ++j) {
                XString* item = (XString*)XStringList_at_base((XVector*)plugin, j);
                const char* value = XString_toUtf8(item);
                if (value && value[0] &&
                    !XStringList_contains_utf8(result, value, XChar_CaseInsensitive))
                    XStringList_push_back_utf8(result, value);
            }
            XStringList_delete_base((XClass*)plugin);
        }
    }
#endif
    return result;
}

XStringList* XImageWriter_imageFormatsForMimeType(const XString* mimeType)
{
    const char* mime = XString_toUtf8(mimeType);
    XStringList* result = XImageWriter_makeStringList(NULL, 0);
    if (!result) return NULL;
    if (mime) {
        if (XImageWriter_mimeEquals(mime, "image/bmp") && XImageWriter_isSupportedFormat("bmp")) { const char* value[] = {"bmp"}; XImageWriter_makeStringList_prefix(result, value, 1); }
        if (XImageWriter_mimeEquals(mime, "image/png") && XImageWriter_isSupportedFormat("png")) { const char* value[] = {"png"}; XImageWriter_makeStringList_prefix(result, value, 1); }
        if (XImageWriter_mimeEquals(mime, "image/gif") && XImageWriter_isSupportedFormat("gif")) { const char* value[] = {"gif"}; XImageWriter_makeStringList_prefix(result, value, 1); }
        if (XImageWriter_mimeEquals(mime, "image/jpeg") && XImageWriter_isSupportedFormat("jpeg")) { const char* value[] = {"jpeg"}; XImageWriter_makeStringList_prefix(result, value, 1); }
        if (XImageWriter_mimeEquals(mime, "image/svg+xml") && XImageWriter_isSupportedFormat("svg")) { const char* value[] = {"svg"}; XImageWriter_makeStringList_prefix(result, value, 1); }
    }
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_imageFormatsForMimeType(mimeType, false);
        int64_t n;
        int64_t j;
        if (plugin) {
            n = XStringList_size_base((XContainer*)plugin);
            for (j = 0; j < n; ++j) {
                XString* item = (XString*)XStringList_at_base((XVector*)plugin, j);
                const char* value = XString_toUtf8(item);
                if (value && value[0] &&
                    !XStringList_contains_utf8(result, value, XChar_CaseInsensitive))
                    XStringList_push_back_utf8(result, value);
            }
            XStringList_delete_base((XClass*)plugin);
        }
    }
#endif
    return result;
}

XStringList* XImageWriter_imageFormatsForMimeType_2(const char* mimeType)
{
    XString* value = mimeType ? XString_create_utf8(mimeType) : NULL;
    XStringList* result = XImageWriter_imageFormatsForMimeType(value);
    if (value) XString_delete_base((XClass*)value);
    return result;
}
