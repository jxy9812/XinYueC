/******************************************************************************
 * @file       XImageReader.c
 * @brief      XImageReader 图像读取器实现（对标 Qt 6.8 QImageReader）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageReader.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XVector.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>

static int g_imageReaderAllocationLimitMb = 256;

/* The portable reader currently has one built-in codec.  Keep this small
   registry in one place so the static discovery APIs cannot advertise a
   format that the reader does not actually instantiate. */
static const char* const g_imageReaderFormats[] = { "bmp" };
static const char* const g_imageReaderMimeTypes[] = { "image/bmp" };

static XVector* XImageReader_makeStringList(const char* const* values, size_t count)
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

static bool XImageReader_mimeIsBmp(const char* mimeType)
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

static const char* XImageReader_detectSignature(const unsigned char* data, size_t size)
{
    static const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (size >= sizeof(png) && memcmp(data, png, sizeof(png)) == 0) return "png";
    if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) return "jpeg";
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') return "bmp";
    if (size >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) return "gif";
    if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) return "webp";
    return NULL;
}

static bool XImageReader_isBmpFormat(const char* format)
{
    if (!format || !format[0]) return true;
    return strlen(format) == 3 &&
           tolower((unsigned char)format[0]) == 'b' &&
           tolower((unsigned char)format[1]) == 'm' &&
           tolower((unsigned char)format[2]) == 'p';
}

/**
 * @brief      XImageReader 私有数据
 */
typedef struct XImageReaderPrivate
{
    XIODevice*  m_device;            /**< IO 设备 */
    char*       m_fileName;          /**< 文件名 */
    char*       m_format;            /**< 格式字符串 */
    bool        m_autoDetectFormat;  /**< 是否自动检测格式 */
    bool        m_decideFromContent; /**< 是否从内容决定格式 */
    int         m_clipX;             /**< 裁剪矩形 X */
    int         m_clipY;             /**< 裁剪矩形 Y */
    int         m_clipW;             /**< 裁剪矩形宽度 */
    int         m_clipH;             /**< 裁剪矩形高度 */
    bool        m_hasClipRect;       /**< 是否有裁剪矩形 */
    int         m_scaledW;           /**< 缩放宽度 */
    int         m_scaledH;           /**< 缩放高度 */
    bool        m_hasScaledSize;     /**< 是否有缩放尺寸 */
    int         m_quality;           /**< 质量参数 */
    int         m_scaledClipX;       /**< 缩放裁剪矩形 X */
    int         m_scaledClipY;       /**< 缩放裁剪矩形 Y */
    int         m_scaledClipW;       /**< 缩放裁剪矩形宽度 */
    int         m_scaledClipH;       /**< 缩放裁剪矩形高度 */
    bool        m_hasScaledClipRect; /**< 是否有缩放裁剪矩形 */
    uint32_t    m_bgColor;           /**< 背景色 */
    bool        m_hasBgColor;        /**< 是否有背景色 */
    bool        m_autoTransform;     /**< 是否自动变换 */
    XImageReaderError m_error;       /**< 错误码 */
    char*       m_errorString;       /**< 错误描述 */
    XImageIOHandler* m_handler;      /**< 内部 IO 处理器 */
}XImageReaderPrivate;

static void XImageReader_setError(XImageReader* self, XImageReaderError error, const char* message)
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

static void VXImageReader_deinit(XImageReader* self)
{
    if (ISNULL(self, "XImageReader")) return;
    if (self->m_data)
    {
        if (self->m_data->m_fileName) XFree_System(self->m_data->m_fileName);
        if (self->m_data->m_format) XFree_System(self->m_data->m_format);
        if (self->m_data->m_errorString) XFree_System(self->m_data->m_errorString);
        if (self->m_data->m_handler) XImageIOHandler_deinit_base(self->m_data->m_handler);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
}

XVtable* XImageReader_class_init()
{
    XVTABLE_INIT_DEFAULT(XImageReader)
	XCLASS_SET_CLASS_NAME_DEFAULT("XImageReader");
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageReader_deinit);
    return XVTABLE_DEFAULT;
}

XImageReader* XImageReader_create()
{
    XImageReader* self = (XImageReader*)XMalloc_System(sizeof(XImageReader));
    if (!self) return NULL;
    XImageReader_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XImageReader_init(XImageReader* self)
{
    if (ISNULL(self, "XImageReader")) return;
    memset(self, 0, sizeof(XImageReader));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XImageReader);
    self->m_data = (XImageReaderPrivate*)XMalloc_System(sizeof(XImageReaderPrivate));
    if (self->m_data)
    {
        memset(self->m_data, 0, sizeof(XImageReaderPrivate));
        self->m_data->m_autoDetectFormat = true;
        self->m_data->m_quality = -1;
        self->m_data->m_error = XImageReaderError_UnknownError;
    }
}

void XImageReader_init_device(XImageReader* self, XIODevice* device, const char* format)
{
    XImageReader_init(self);
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

void XImageReader_init_file(XImageReader* self, const char* fileName, const char* format)
{
    XImageReader_init(self);
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

void XImageReader_deinit(XImageReader* self) { XImageReader_deinit_base(self); }
void XImageReader_deinit_base(XImageReader* self)
{
    if (ISNULL(self, "XImageReader") || ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XImageReader*))(self);
}

void XImageReader_setFormat(XImageReader* self, const char* format)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_format) XFree_System(self->m_data->m_format);
    self->m_data->m_format = format ? (char*)XMalloc_System(strlen(format) + 1) : NULL;
    if (format && self->m_data->m_format) strcpy(self->m_data->m_format, format);
}

const char* XImageReader_format(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_format : NULL; }

void XImageReader_setAutoDetectImageFormat(XImageReader* self, bool enabled)
{ if (self && self->m_data) self->m_data->m_autoDetectFormat = enabled; }

bool XImageReader_autoDetectImageFormat(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_autoDetectFormat : true; }

void XImageReader_setDecideFormatFromContent(XImageReader* self, bool ignored)
{ if (self && self->m_data) self->m_data->m_decideFromContent = ignored; }

bool XImageReader_decideFormatFromContent(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_decideFromContent : false; }

void XImageReader_setDevice(XImageReader* self, XIODevice* device)
{
    if (!self || !self->m_data) return;
    self->m_data->m_device = device;
    if (self->m_data->m_fileName) XFree_System(self->m_data->m_fileName);
    self->m_data->m_fileName = NULL;
}

XIODevice* XImageReader_device(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_device : NULL; }

void XImageReader_setFileName(XImageReader* self, const char* fileName)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_fileName) XFree_System(self->m_data->m_fileName);
    self->m_data->m_fileName = fileName ? (char*)XMalloc_System(strlen(fileName) + 1) : NULL;
    if (fileName && self->m_data->m_fileName) strcpy(self->m_data->m_fileName, fileName);
    self->m_data->m_device = NULL;
}

const char* XImageReader_fileName(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_fileName : NULL; }

void XImageReader_size(const XImageReader* self, XSize* out)
{
    if (out) { out->width = 0; out->height = 0; }
}


void* XImageReader_textKeys(const XImageReader* self) { (void)self; return NULL; }
const char* XImageReader_text(const XImageReader* self, const char* key) { (void)self; (void)key; return NULL; }

void XImageReader_setClipRect(XImageReader* self, const XRect* rect)
{
    if (!self || !self->m_data || !rect) return;
    self->m_data->m_clipX = rect->x;
    self->m_data->m_clipY = rect->y;
    self->m_data->m_clipW = rect->width;
    self->m_data->m_clipH = rect->height;
    self->m_data->m_hasClipRect = true;
}

void XImageReader_clipRect(const XImageReader* self, XRect* out)
{
    if (out)
    {
        out->x = (self && self->m_data) ? self->m_data->m_clipX : 0;
        out->y = (self && self->m_data) ? self->m_data->m_clipY : 0;
        out->width = (self && self->m_data) ? self->m_data->m_clipW : 0;
        out->height = (self && self->m_data) ? self->m_data->m_clipH : 0;
    }
}

void XImageReader_setScaledSize(XImageReader* self, const XSize* size)
{
    if (!self || !self->m_data || !size) return;
    self->m_data->m_scaledW = size->width;
    self->m_data->m_scaledH = size->height;
    self->m_data->m_hasScaledSize = true;
}

void XImageReader_scaledSize(const XImageReader* self, XSize* out)
{
    if (out)
    {
        out->width = (self && self->m_data) ? self->m_data->m_scaledW : 0;
        out->height = (self && self->m_data) ? self->m_data->m_scaledH : 0;
    }
}

void XImageReader_setQuality(XImageReader* self, int quality)
{ if (self && self->m_data) self->m_data->m_quality = quality; }

int XImageReader_quality(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_quality : -1; }

void XImageReader_setScaledClipRect(XImageReader* self, const XRect* rect)
{
    if (!self || !self->m_data || !rect) return;
    self->m_data->m_scaledClipX = rect->x;
    self->m_data->m_scaledClipY = rect->y;
    self->m_data->m_scaledClipW = rect->width;
    self->m_data->m_scaledClipH = rect->height;
    self->m_data->m_hasScaledClipRect = true;
}

void XImageReader_scaledClipRect(const XImageReader* self, XRect* out)
{
    if (out)
    {
        out->x = (self && self->m_data) ? self->m_data->m_scaledClipX : 0;
        out->y = (self && self->m_data) ? self->m_data->m_scaledClipY : 0;
        out->width = (self && self->m_data) ? self->m_data->m_scaledClipW : 0;
        out->height = (self && self->m_data) ? self->m_data->m_scaledClipH : 0;
    }
}

void XImageReader_setBackgroundColor(XImageReader* self, uint32_t color)
{ if (self && self->m_data) { self->m_data->m_bgColor = color; self->m_data->m_hasBgColor = true; } }

uint32_t XImageReader_backgroundColor(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_bgColor : 0; }

bool XImageReader_supportsAnimation(const XImageReader* self) { (void)self; return false; }

XImageIOHandlerTransformation XImageReader_transformation(const XImageReader* self)
{ (void)self; return XImageIOHandlerTransformation_None; }

void XImageReader_setAutoTransform(XImageReader* self, bool enabled)
{ if (self && self->m_data) self->m_data->m_autoTransform = enabled; }

bool XImageReader_autoTransform(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_autoTransform : false; }

const char* XImageReader_subType(const XImageReader* self) { (void)self; return NULL; }
void* XImageReader_supportedSubTypes(const XImageReader* self) { (void)self; return NULL; }

bool XImageReader_canRead(const XImageReader* self)
{
    if (!self || !self->m_data) return false;
    if (self->m_data->m_handler) return XImageIOHandler_canRead_base(self->m_data->m_handler);
    if (!XImageReader_isBmpFormat(self->m_data->m_format)) return false;
    if (self->m_data->m_device) {
        const char* detected = XImageReader_imageFormatDevice(self->m_data->m_device);
        return detected && strcmp(detected, "bmp") == 0;
    }
    if (!self->m_data->m_fileName) return false;
    const char* detected = XImageReader_imageFormat(self->m_data->m_fileName);
    return detected && strcmp(detected, "bmp") == 0;
}

bool XImageReader_read(XImageReader* self, XImage* out)
{
    if (!self || !self->m_data || !out) return false;
    if (!XClassIsVtableNull(out)) XImage_deinit_base(out);
    XImage_init(out);
    if (self->m_data->m_handler)
    {
        if (XImageIOHandler_read_base(self->m_data->m_handler, out)) return true;
        XImageReader_setError(self, XImageReaderError_InvalidDataError, "Image handler failed to read data");
        return false;
    }
    if (self->m_data->m_fileName)
    {
        if (!XImageReader_isBmpFormat(self->m_data->m_format)) {
            XImageReader_setError(self, XImageReaderError_UnsupportedFormatError,
                                  "Only BMP is supported by the built-in portable decoder");
            return false;
        }
        if (!XImage_load(out, self->m_data->m_fileName, self->m_data->m_format)) {
            FILE* file = fopen(self->m_data->m_fileName, "rb");
            if (!file)
                XImageReader_setError(self, XImageReaderError_FileNotFoundError,
                                      "Image file could not be opened");
            else {
                fclose(file);
                XImageReader_setError(self, XImageReaderError_InvalidDataError,
                                      "BMP image data is invalid or unsupported");
            }
            return false;
        }
    }
    else if (self->m_data->m_device) {
        XByteArray* bytes = XIODevice_readAll_3(self->m_data->m_device);
        int64_t size = bytes ? (int64_t)XByteArray_size_base(bytes) : 0;
        bool ok = bytes && size <= INT_MAX &&
                  XImage_loadFromData(out, (const uint8_t*)XByteArray_data(bytes),
                                      (int)size, self->m_data->m_format);
        if (bytes) XByteArray_delete_base(bytes);
        if (!ok) {
            XImageReader_setError(self, XImageReaderError_InvalidDataError,
                                  "BMP image data from the device is invalid or unsupported");
            return false;
        }
    } else {
        XImageReader_setError(self, XImageReaderError_DeviceError, "No image source is set");
        return false;
    }

    if (self->m_data->m_hasClipRect) {
        XImage clipped;
        XImage_init(&clipped);
        XImage_copyRect(out, &(XRect){self->m_data->m_clipX, self->m_data->m_clipY,
                                     self->m_data->m_clipW, self->m_data->m_clipH}, &clipped);
        XImage_deinit_base(out);
        XImage_move_base(out, &clipped);
        XImage_deinit_base(&clipped);
    }
    if (self->m_data->m_hasScaledSize && self->m_data->m_scaledW > 0 && self->m_data->m_scaledH > 0) {
        XImage scaled;
        XImage_init(&scaled);
        XImage_scaled(out, self->m_data->m_scaledW, self->m_data->m_scaledH, 0, 0, &scaled);
        XImage_deinit_base(out);
        XImage_move_base(out, &scaled);
        XImage_deinit_base(&scaled);
    }
    XImageReader_setError(self, XImageReaderError_UnknownError, NULL);
    return !XImage_isNull(out);
}

bool XImageReader_jumpToNextImage(XImageReader* self) { (void)self; return false; }
bool XImageReader_jumpToImage(XImageReader* self, int imageNumber) { (void)self; (void)imageNumber; return false; }
int XImageReader_loopCount(const XImageReader* self) { (void)self; return 0; }
int XImageReader_imageCount(const XImageReader* self) { (void)self; return 0; }
int XImageReader_nextImageDelay(const XImageReader* self) { (void)self; return 0; }
int XImageReader_currentImageNumber(const XImageReader* self) { (void)self; return 0; }

void XImageReader_currentImageRect(const XImageReader* self, XRect* out)
{
    if (out) memset(out, 0, sizeof(XRect));
}

XImageReaderError XImageReader_error(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_error : XImageReaderError_UnknownError; }

const char* XImageReader_errorString(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_errorString : NULL; }

bool XImageReader_supportsOption(const XImageReader* self, XImageIOHandlerOption option)
{
    (void)self;
    (void)option;
    return false;
}

const char* XImageReader_imageFormat(const char* fileName)
{
    unsigned char signature[16];
    size_t size;
    FILE* file;
    if (!fileName) return NULL;
    file = fopen(fileName, "rb");
    if (!file) return NULL;
    size = fread(signature, 1, sizeof(signature), file);
    fclose(file);
    return XImageReader_detectSignature(signature, size);
}

const char* XImageReader_imageFormatDevice(XIODevice* device)
{
    XByteArray* bytes;
    const char* result = NULL;
    if (!device) return NULL;
    bytes = XIODevice_peek_3(device, 16);
    if (bytes) {
        result = XImageReader_detectSignature(
            (const unsigned char*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base(bytes));
        XByteArray_delete_base(bytes);
    }
    return result;
}

void* XImageReader_supportedImageFormats()
{
    return XImageReader_makeStringList(g_imageReaderFormats,
                                       sizeof(g_imageReaderFormats) /
                                       sizeof(g_imageReaderFormats[0]));
}

void* XImageReader_supportedMimeTypes()
{
    return XImageReader_makeStringList(g_imageReaderMimeTypes,
                                       sizeof(g_imageReaderMimeTypes) /
                                       sizeof(g_imageReaderMimeTypes[0]));
}

void* XImageReader_imageFormatsForMimeType(const char* mimeType)
{
    if (XImageReader_mimeIsBmp(mimeType))
        return XImageReader_makeStringList(g_imageReaderFormats,
                                           sizeof(g_imageReaderFormats) /
                                           sizeof(g_imageReaderFormats[0]));
    /* Match Qt's value-returning API: an unknown MIME type is an empty list,
       rather than a list containing an unrelated fallback format. */
    return XImageReader_makeStringList(NULL, 0);
}
int XImageReader_allocationLimit() { return g_imageReaderAllocationLimitMb; }
void XImageReader_setAllocationLimit(int mbLimit)
{
    g_imageReaderAllocationLimitMb = mbLimit < 0 ? 0 : mbLimit;
}
