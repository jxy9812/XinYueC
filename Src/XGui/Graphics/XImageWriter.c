/******************************************************************************
 * @file       XImageWriter.c
 * @brief      XImageWriter 图像写入器实现（对标 Qt 6.8 QImageWriter）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageWriter.h"
#include "XImageCodec.h"
#include "XImagePluginRegistry.h"
#include "XFile.h"
#include "XSaveFile.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XStringList.h"
#include <string.h>
#include <limits.h>

/* Keep writer discovery aligned with XImageCodec capabilities. */
static const char* const g_imageWriterFormats[] =
    { "bmp", "png", "jpg", "jpeg", "jfif", "gif", "svg" };
static const char* const g_imageWriterMimeTypes[] =
    { "image/bmp", "image/png", "image/jpeg", "image/jpeg", "image/jpeg",
      "image/gif", "image/svg+xml" };

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
    /* Qt QImageWriter::supportedMimeTypes() sorts and removes duplicates. */
    XStringList_sort(result, XChar_CaseSensitive);
    XStringList_removeDuplicates(result);
    return result;
}

static bool XImageWriter_mimeEquals(const char* mimeType, const char* expected)
{
    /* Qt QImageReaderWriterHelpers 按 QByteArray 值精确匹配 MIME；格式名
       本身才按大小写不敏感规则规范化。 */
    return mimeType && expected && strcmp(mimeType, expected) == 0;
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
    float       m_gamma;             /**< Gamma 参数，Qt 默认值为 0.0 */
    XString*    m_description;       /**< 按 Qt 格式拼接的文本描述元数据 */
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
}

static void XImageWriter_releaseOwnedDevice(XImageWriterPrivate* data)
{
    XIODevice* owned;
    if (!data || !(owned = data->m_fileDevice)) return;
    if (XIODevice_isOpen(owned)) XIODevice_close_base(owned);
    if (data->m_device == owned) data->m_device = NULL;
    XClass_delete_base((XClass*)owned);
    data->m_fileDevice = NULL;
}

static XIODevice* XImageWriter_ensureOwnedDevice(XImageWriterPrivate* data)
{
    XFile* file;
    if (!data) return NULL;
    if (data->m_fileDevice) return data->m_fileDevice;
    if (!data->m_fileName ||
        XContainer_isEmpty_base((const XContainer*)data->m_fileName))
        return NULL;
    file = XFile_create_2(data->m_fileName);
    if (!file) return NULL;
    data->m_fileDevice = (XIODevice*)file;
    data->m_device = data->m_fileDevice;
    return data->m_fileDevice;
}

/* Qt keeps the handler for the complete writer lifetime.  Releasing it is
   therefore part of setDevice(), setFileName(), and deinitialization, not
   part of write().  The owned QFile follows the same lifetime rule. */
static void XImageWriter_releaseTransientState(XImageWriterPrivate* data)
{
    if (!data) return;
    XImageWriter_releaseHandler(data);
    if (data->m_fileDevice) {
        /* The owned file device remains observable through device(). */
        if (XIODevice_isOpen(data->m_fileDevice)) XIODevice_close_base(data->m_fileDevice);
    }
}

/* 对标 QImageWriter::canWrite() 对 QFile 的失败清理：检查过程中若新建了
 * 一个原本不存在的目标文件，而后设备/格式校验失败，则删除该空文件。 */
static void XImageWriter_removeNewFileOnFailure(XImageWriter* self,
                                                bool removeOnFailure)
{
    XImageWriterPrivate* data;
    if (!removeOnFailure || !self || !(data = self->m_data) ||
        !data->m_fileDevice || !data->m_fileName)
        return;
    if (XIODevice_isOpen(data->m_fileDevice))
        XIODevice_close_base(data->m_fileDevice);
    (void)XFile_remove_static(data->m_fileName);
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
        const char* base = fileName ? strrchr(fileName, '/') : NULL;
        const char* dot;
        const char* backslash = fileName ? strrchr(fileName, '\\') : NULL;
        if (backslash && (!base || backslash > base)) base = backslash;
        base = base ? base + 1 : fileName;
        dot = base ? strrchr(base, '.') : NULL;
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
            XIODevice* file = XImageWriter_ensureOwnedDevice(data);
            if (file && !XIODevice_isOpen(file) &&
                XIODevice_open_base(file, XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create)) {
                data->m_handler = XImagePluginRegistry_createWriteHandler(file, format);
                if (data->m_handler) {
                    XString_delete_base((XClass*)format);
                    return data->m_handler;
                }
            } else if (file && XIODevice_isOpen(file)) {
                data->m_handler = XImagePluginRegistry_createWriteHandler(file, format);
                if (data->m_handler) {
                    XString_delete_base((XClass*)format);
                    return data->m_handler;
                }
            }
        }
        /* Qt QImageWriter::supportsOption() is valid after setFormat() even
           before a device is assigned.  Create the format handler with a
           null device for that capability query; canWrite()/write() still
           reject the missing device before using it. */
        if (!data->m_handler && !data->m_fileName)
            data->m_handler = XImagePluginRegistry_createWriteHandler(NULL, format);
        if (data->m_handler) {
            XString_delete_base((XClass*)format);
            return data->m_handler;
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
    if (XImageIOHandler_supportsOption_base(handler, XImageIOHandlerOption_Quality)) {
        value.integer = data->m_quality;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_Quality, &value);
    }
    if (XImageIOHandler_supportsOption_base(handler, XImageIOHandlerOption_CompressionRatio)) {
        memset(&value, 0, sizeof(value));
        value.integer = data->m_compression;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_CompressionRatio, &value);
    }
    if (XImageIOHandler_supportsOption_base(handler, XImageIOHandlerOption_Gamma)) {
        /* 对齐 Qt 6.8 qimagewriter.cpp:668-669；即使没有公共 Gamma
           setter，默认的 0.0 仍需传给支持该选项的处理器。 */
        memset(&value, 0, sizeof(value));
        value.real = data->m_gamma;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_Gamma, &value);
    }
    if (data->m_description &&
        !XContainer_isEmpty_base((const XContainer*)data->m_description) &&
        XImageIOHandler_supportsOption_base(handler, XImageIOHandlerOption_Description)) {
        memset(&value, 0, sizeof(value));
        value.string = data->m_description;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_Description, &value);
    }
    if (data->m_subType && !XContainer_isEmpty_base((const XContainer*)data->m_subType) &&
        XImageIOHandler_supportsOption_base(handler, XImageIOHandlerOption_SubType)) {
        memset(&value, 0, sizeof(value));
        value.string = data->m_subType;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_SubType, &value);
    }
    if (XImageIOHandler_supportsOption_base(handler, XImageIOHandlerOption_OptimizedWrite)) {
        memset(&value, 0, sizeof(value));
        value.boolean = data->m_optimizedWrite;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_OptimizedWrite, &value);
    }
    if (XImageIOHandler_supportsOption_base(handler, XImageIOHandlerOption_ProgressiveScanWrite)) {
        memset(&value, 0, sizeof(value));
        value.boolean = data->m_progressiveScan;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_ProgressiveScanWrite, &value);
    }
    if (XImageIOHandler_supportsOption_base(handler, XImageIOHandlerOption_ImageTransformation)) {
        memset(&value, 0, sizeof(value));
        value.transformation = data->m_transformation;
        XImageIOHandler_setOption_base(handler, XImageIOHandlerOption_ImageTransformation, &value);
    }
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
    const char* base;
    const char* dot;
    if (!fileName) return false;
    base = strrchr(fileName, '/');
    {
        const char* backslash = strrchr(fileName, '\\');
        if (backslash && (!base || backslash > base)) base = backslash;
    }
    base = base ? base + 1 : fileName;
    dot = strrchr(base, '.');
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
        XImageWriter_releaseTransientState(self->m_data);
        XImageWriter_releaseOwnedDevice(self->m_data);
        if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
        if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
        if (self->m_data->m_description) XString_delete_base((XClass*)self->m_data->m_description);
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
        /* QImageWriterPrivate 将 gamma 初始化为 0.0，并在 write() 中
           对声明支持 Gamma 的处理器无条件应用该默认值。 */
        self->m_data->m_gamma = 0.0f;
        self->m_data->m_error = XImageWriterError_UnknownError;
        /* Qt 6.8 initializes errorString together with UnknownError.  Keep
         * the string observable even before the first failed operation. */
        self->m_data->m_errorString = XString_create_utf8("Unknown error");
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
        if (self->m_data->m_fileName)
            (void)XImageWriter_ensureOwnedDevice(self->m_data);
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
    XImageWriter_releaseTransientState(self->m_data);
    XImageWriter_releaseOwnedDevice(self->m_data);
    self->m_data->m_device = device;
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = NULL;
}

XIODevice* XImageWriter_device(const XImageWriter* self)
{ return (self && self->m_data) ?
        (self->m_data->m_device ? self->m_data->m_device : self->m_data->m_fileDevice) : NULL; }

void XImageWriter_setFileName(XImageWriter* self, const XString* fileName)
{
    if (!self || !self->m_data) return;
    XImageWriter_releaseTransientState(self->m_data);
    XImageWriter_releaseOwnedDevice(self->m_data);
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = fileName ? XString_create_copy(fileName) : NULL;
    self->m_data->m_device = XImageWriter_ensureOwnedDevice(self->m_data);
}

void XImageWriter_setFileName_2(XImageWriter* self, const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL;
    XImageWriter_setFileName(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageWriter_fileName_const(const XImageWriter* self)
{
    XIODevice* device;
    if (!self || !self->m_data) return NULL;
    if (self->m_data->m_fileName) return self->m_data->m_fileName;

    /* Qt QImageWriter::fileName() 对任意 QFileDevice 返回设备文件名；
       C99 层通过 XFile/XSaveFile 的虚表身份实现同一借用语义。 */
    device = self->m_data->m_device;
    if (device && (XClassGetVtable(device) == XFile_class_init()
#if XSAVEFILE_ON
                   || XClassGetVtable(device) == XSaveFile_class_init()
#endif
                   ))
        return XFileDevice_fileName_base((XFileDevice*)device);
    return NULL;
}

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
    XImageIOHandler* handler;
    XImageIOHandlerOptionValue value;
    XStringList* result;
    if (!self || !self->m_data) return XImageWriter_makeStringList(NULL, 0);
    handler = XImageWriter_ensureHandler((XImageWriter*)self);
    if (!handler ||
        !XImageIOHandler_supportsOption_base(handler,
                                             XImageIOHandlerOption_SupportedSubTypes))
        return XImageWriter_makeStringList(NULL, 0);
    memset(&value, 0, sizeof(value));
    if (!XImageIOHandler_option_base(handler,
                                     XImageIOHandlerOption_SupportedSubTypes,
                                     &value) || !value.stringList)
        return XImageWriter_makeStringList(NULL, 0);
    result = XStringList_create_copy(value.stringList);
    return result ? result : XImageWriter_makeStringList(NULL, 0);
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
    XString* normalizedKey;
    XString* normalizedText;
    XString* description;
    bool ok = true;
    if (!self || !self->m_data) return;

    /* Qt 6.8 stores one Description string: key and value are simplified
       independently and successive records are separated by two newlines.
       The selected handler decides whether this metadata is serialized. */
    normalizedKey = XString_simplified(key);
    normalizedText = XString_simplified(text);
    if (!normalizedKey || !normalizedText) {
        if (normalizedKey) XString_delete_base((XClass*)normalizedKey);
        if (normalizedText) XString_delete_base((XClass*)normalizedText);
        return;
    }
    description = self->m_data->m_description
        ? XString_create_copy(self->m_data->m_description)
        : XString_create();
    if (!description) ok = false;
    if (ok && !XString_isEmpty_base((const XContainer*)description))
        ok = XString_append_utf8(description, "\n\n");
    if (ok) ok = XString_append(description, normalizedKey);
    if (ok) ok = XString_append_utf8(description, ": ");
    if (ok) ok = XString_append(description, normalizedText);
    if (ok) {
        if (self->m_data->m_description)
            XString_delete_base((XClass*)self->m_data->m_description);
        self->m_data->m_description = description;
        description = NULL;
    }
    if (description) XString_delete_base((XClass*)description);
    XString_delete_base((XClass*)normalizedKey);
    XString_delete_base((XClass*)normalizedText);
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
    const char* format;
    bool removeOnFailure = false;
    if (!self || !self->m_data) return false;
    if (!self->m_data->m_device &&
        (!self->m_data->m_fileName ||
         XContainer_isEmpty_base((const XContainer*)self->m_data->m_fileName))) {
        XImageWriter_setError((XImageWriter*)self, XImageWriterError_DeviceError,
                               "Device is not set");
        return false;
    }

    if (self->m_data->m_fileDevice && self->m_data->m_fileName)
        removeOnFailure = !XFile_exists_static(self->m_data->m_fileName);

    /* QImageWriter opens an assigned device on demand and then verifies its
       write mode before asking the image plugin for a handler. */
    if (self->m_data->m_device) {
        if (!XIODevice_isOpen(self->m_data->m_device) &&
            !XIODevice_open_base(self->m_data->m_device, XIODevice_WriteOnly)) {
            XImageWriter_setError((XImageWriter*)self, XImageWriterError_DeviceError,
                                   "Cannot open device for writing");
            XImageWriter_removeNewFileOnFailure((XImageWriter*)self,
                                                removeOnFailure);
            return false;
        }
        if (!XIODevice_isWritable(self->m_data->m_device)) {
            XImageWriter_setError((XImageWriter*)self, XImageWriterError_DeviceError,
                                   "Device not writable");
            XImageWriter_removeNewFileOnFailure((XImageWriter*)self,
                                                removeOnFailure);
            return false;
        }
    }

    format = self->m_data->m_format ?
        XString_toUtf8(self->m_data->m_format) : NULL;
    if (!format || !format[0]) {
        /* A filename-backed writer derives the format from its suffix even
           though Qt exposes the internally-owned QFile through device(). */
        if ((!self->m_data->m_fileName && self->m_data->m_device) ||
            !XImageWriter_fileLooksSupported(
                XString_toUtf8(self->m_data->m_fileName))) {
            XImageWriter_setError((XImageWriter*)self,
                                   XImageWriterError_UnsupportedFormatError,
                                   "Unsupported image format");
            XImageWriter_removeNewFileOnFailure((XImageWriter*)self,
                                                removeOnFailure);
            return false;
        }
    } else if (!XImageWriter_isSupportedFormat(format)) {
        XImageWriter_setError((XImageWriter*)self,
                               XImageWriterError_UnsupportedFormatError,
                               "Unsupported image format");
        XImageWriter_removeNewFileOnFailure((XImageWriter*)self,
                                            removeOnFailure);
        return false;
    }

    /* For plugin-backed formats this also mirrors Qt's handler creation in
       canWriteHelper(); direct codecs continue through the fallback path. */
    if (self->m_data->m_device || self->m_data->m_fileName) {
        XImageIOHandler* handler = XImageWriter_ensureHandler((XImageWriter*)self);
#if XIMAGEIOPLUGIN_ON
        if (!handler) {
            XImageWriter_setError((XImageWriter*)self,
                                   XImageWriterError_UnsupportedFormatError,
                                   "Unsupported image format");
            XImageWriter_removeNewFileOnFailure((XImageWriter*)self,
                                                removeOnFailure);
            return false;
        }
#else
        (void)handler;
#endif
    }
    return true;
}

bool XImageWriter_write(XImageWriter* self, const XImage* image)
{
    XImage transformed;
    const XImage* source = image;
    bool transformedInitialized = false;
    bool handlerAttempted = false;
    bool wrote = false;
    if (!self || !self->m_data || !image || XImage_isNull(image))
    {
        if (self && self->m_data)
            XImageWriter_setError(self, XImageWriterError_InvalidImageError, "Image is empty");
        return false;
    }
    /* Qt checks the image before canWrite(), so an invalid image never opens
       a file as a side effect. */
    if (!XImageWriter_canWrite(self)) return false;

    XImageWriter_ensureHandler(self);
    if (self->m_data->m_handler) {
        handlerAttempted = true;
        if (self->m_data->m_transformation != XImageIOHandlerTransformation_None &&
            !XImageIOHandler_supportsOption_base(
                self->m_data->m_handler,
                XImageIOHandlerOption_ImageTransformation)) {
            if (!XImageWriter_applyTransformation(image, self->m_data->m_transformation,
                                                   &transformed)) {
                XImageWriter_setError(self, XImageWriterError_UnsupportedFormatError,
                                       "The requested image transformation is unsupported");
                return false;
            }
            source = &transformed;
            transformedInitialized = true;
        }
        XImageWriter_applyHandlerSettings(self->m_data->m_handler, self->m_data);
        wrote = XImageIOHandler_write_base(self->m_data->m_handler, source);
        /* QImageWriter flushes only a QFileDevice (qimagewriter.cpp:685-686),
           and deliberately ignores QFileDevice::flush()'s return value before
           returning success (qimagewriter.cpp:683-687).  An arbitrary
           caller-owned QIODevice is not flushed by the wrapper. */
        if (wrote && self->m_data->m_fileDevice)
            (void)XIODevice_flush(self->m_data->m_fileDevice);
        if (wrote) {
            if (transformedInitialized) XImage_deinit_base(&transformed);
            return true;
        }
        /* Qt QImageWriter::write() returns immediately when the selected
           handler rejects the image.  Do not retry through XImage_save_2():
           that fallback would duplicate output and could silently switch
           formats after a plugin has already accepted the request. */
        if (handlerAttempted) {
            if (transformedInitialized) XImage_deinit_base(&transformed);
            /* Qt returns directly from handler->write() here and does not
               replace imageWriterError/errorString (qimagewriter.cpp:683-684).
               Preserve the previously observable status for repeated writes. */
            return false;
        }
    }

    /* Direct codecs do not have an XImageIOHandler.  They therefore receive
       the transformed image here, matching Qt's fallback transform path. */
    if (!transformedInitialized &&
        self->m_data->m_transformation != XImageIOHandlerTransformation_None) {
        if (!XImageWriter_applyTransformation(image, self->m_data->m_transformation,
                                               &transformed)) {
            XImageWriter_setError(self, XImageWriterError_UnsupportedFormatError,
                                   "The requested image transformation is unsupported");
            return false;
        }
        source = &transformed;
        transformedInitialized = true;
    }
    if (!self->m_data->m_fileName && !self->m_data->m_device) {
        XImageWriter_setError(self, XImageWriterError_DeviceError, "Device is not set");
    } else if (!self->m_data->m_fileName) {
        XByteArray* bytes = XByteArray_create();
#if XIMAGECODEC_ON
        XImageCodecFormat format = XImageCodec_formatFromName(self->m_data->m_format);
        bool ok = bytes && XImageCodec_encode(source, format, self->m_data->m_quality, bytes) &&
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
    } else if (!XImage_save_2(source, XString_toUtf8(self->m_data->m_fileName),
                              XString_toUtf8(self->m_data->m_format), self->m_data->m_quality)) {
        XImageWriter_setError(self, XImageWriterError_DeviceError, "Image could not be written");
    } else {
        wrote = true;
    }
    /* QImageWriter::write() does not clear a previous error on success; the
       initial Unknown error string therefore remains observable until a new
       failure replaces it. */
    if (transformedInitialized) XImage_deinit_base(&transformed);
    return wrote;
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
    XImageIOHandler* handler;
    if (!self || !self->m_data) return false;
    handler = XImageWriter_ensureHandler((XImageWriter*)self);
    if (!handler) {
        XImageWriter_setError((XImageWriter*)self,
                               XImageWriterError_UnsupportedFormatError,
                               "Unsupported image format");
        return false;
    }
    return XImageIOHandler_supportsOption_base(handler, option);
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
    XStringList_sort(result, XChar_CaseSensitive);
    XStringList_removeDuplicates(result);
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
        if (XImageWriter_mimeEquals(mime, "image/jpeg") && XImageWriter_isSupportedFormat("jpeg")) {
            const char* value[] = {"jpg", "jpeg", "jfif"};
            XImageWriter_makeStringList_prefix(result, value, 3);
        }
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
