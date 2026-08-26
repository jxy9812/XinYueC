/******************************************************************************
 * @file       XImageReader.c
 * @brief      XImageReader 图像读取器实现（对标 Qt 6.8 QImageReader）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageReader.h"
#include "XImageCodec.h"
#include "XImagePluginRegistry.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XVector.h"
#include "XStringList.h"
#include "XFile.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>

static int g_imageReaderAllocationLimitMb = 256;

/* Keep discovery aligned with the independent XImageCodec registry. */
static const char* const g_imageReaderFormats[] = { "bmp", "png", "jpeg", "gif", "svg" };
static const char* const g_imageReaderMimeTypes[] = { "image/bmp", "image/png", "image/jpeg", "image/gif", "image/svg+xml" };

static XStringList* XImageReader_makeStringList(const char* const* values, size_t count)
{
    XStringList* result = XStringList_create();
    if (!result) return NULL;
    for (size_t i = 0; i < count; ++i) {
        XStringList_push_back_utf8(result, values[i]);
    }
    return result;
}

static void XImageReader_makeStringList_prefix(XStringList* result, const char* const* values, size_t count)
{
    size_t i;
    if (!result || !values) return;
    for (i = 0; i < count; ++i) {
        if (values[i] && values[i][0] &&
            !XStringList_contains_utf8(result, values[i], XChar_CaseInsensitive))
            XStringList_push_back_utf8(result, values[i]);
    }
}

static XStringList* XImageReader_supportedFormats(void)
{
    XStringList* result = XStringList_create();
    size_t i;
    if (!result) return NULL;
#if XIMAGECODEC_ON
    for (i = 0; i < sizeof(g_imageReaderFormats) / sizeof(g_imageReaderFormats[0]); ++i)
        if (XImageCodec_canDecode(XImageCodec_formatFromName_2(g_imageReaderFormats[i])))
            XStringList_push_back_utf8(result, g_imageReaderFormats[i]);
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_supportedImageFormats(true);
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

static bool XImageReader_mimeEquals(const char* mimeType, const char* expected)
{
    size_t i;
    if (!mimeType) return false;
    for (i = 0; expected[i] && mimeType[i]; ++i) {
        if (tolower((unsigned char)mimeType[i]) !=
            tolower((unsigned char)expected[i])) return false;
    }
    return expected[i] == '\0' && mimeType[i] == '\0';
}

static bool XImageReader_mimeIsBmp(const char* mimeType)
{
    return XImageReader_mimeEquals(mimeType, "image/bmp");
}

static const char* XImageReader_detectSignature(const unsigned char* data, size_t size)
{
#if XIMAGECODEC_ON
    return XImageCodec_formatName_2(XImageCodec_detect(data, size));
#else
    (void)data;
    (void)size;
    return NULL;
#endif
}

static bool XImageReader_isSupportedFormat(const char* format)
{
    if (!format || !format[0]) return true;
#if XIMAGECODEC_ON
    if (XImageCodec_canDecode(XImageCodec_formatFromName_2(format))) return true;
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XString* value = XString_create_utf8(format);
        bool supported = value && XImagePluginRegistry_supportsReadFormat(value);
        if (value) XString_delete_base((XClass*)value);
        if (supported) return true;
    }
#else
    (void)format;
#endif
    return false;
}

/**
 * @brief      XImageReader 私有数据
 */
typedef struct XImageReaderPrivate
{
    XIODevice*  m_device;            /**< IO 设备 */
    XString*    m_fileName;          /**< 文件名（UTF-8） */
    XString*    m_format;            /**< 格式字符串（UTF-8） */
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
    XString*    m_errorString;       /**< 错误描述 */
    XImageIOHandler* m_handler;      /**< 内部 IO 处理器 */
    XIODevice*      m_fileDevice;   /**< 内部文件设备；由读取器拥有，供插件处理器使用 */
    int         m_sizeW;             /**< 已探测图像宽度 */
    int         m_sizeH;             /**< 已探测图像高度 */
    bool        m_hasSize;           /**< 是否已探测到图像尺寸 */
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    XImageCodecAnimation* m_animation; /**< GIF 动画缓存；由读取器拥有。 */
    int         m_currentImageNumber; /**< 当前帧编号。 */
    XByteArray* m_sourceBytes;        /**< 设备数据缓存，避免多次消费设备。 */
#endif
}XImageReaderPrivate;

static uint32_t XImageReader_readLe32(const unsigned char* data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t XImageReader_readLe16(const unsigned char* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t XImageReader_readBe32(const unsigned char* data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool XImageReader_probeBmpSize(const unsigned char* data, size_t size,
                                      int* width, int* height)
{
    int32_t signedWidth;
    int32_t signedHeight;
    if (!data || size < 26 || data[0] != 'B' || data[1] != 'M' || !width || !height)
        return false;
    signedWidth = (int32_t)XImageReader_readLe32(data + 18);
    signedHeight = (int32_t)XImageReader_readLe32(data + 22);
    if (signedWidth <= 0 || signedHeight == 0 || signedWidth > INT_MAX)
        return false;
    if (signedHeight == INT32_MIN)
        return false;
    signedHeight = signedHeight < 0 ? -signedHeight : signedHeight;
    if (signedHeight > INT_MAX)
        return false;
    *width = (int)signedWidth;
    *height = (int)signedHeight;
    return true;
}

static bool XImageReader_probeSize(XImageReader* self)
{
    enum { XIMAGE_READER_PROBE_MAX = 262144 };
    unsigned char header[XIMAGE_READER_PROBE_MAX];
    size_t size = 0;
    XFile* fileObject;
    XByteArray* bytes;
    if (!self || !self->m_data) return false;
    if (self->m_data->m_hasSize) return true;
    if (self->m_data->m_sourceBytes) {
        size = XByteArray_size_base((const XContainer*)self->m_data->m_sourceBytes);
        if (size > sizeof(header)) size = sizeof(header);
        memcpy(header, XByteArray_data(self->m_data->m_sourceBytes), size);
    } else if (self->m_data->m_fileName) {
        fileObject = XFile_create_2(self->m_data->m_fileName);
        if (!fileObject || !XIODevice_open_base((XIODevice*)fileObject, XIODevice_ReadOnly)) { if (fileObject) XClass_delete_base((XClass*)fileObject); return false; }
        bytes = XIODevice_readAll_3((XIODevice*)fileObject); XIODevice_close_base((XIODevice*)fileObject); XClass_delete_base((XClass*)fileObject);
        if (!bytes) return false; size = XByteArray_size_base((const XContainer*)bytes); if (size > sizeof(header)) size = sizeof(header); if (size) memcpy(header, XByteArray_data(bytes), size); XByteArray_delete_base((XClass*)bytes);
    } else if (self->m_data->m_device) {
        bytes = XIODevice_peek_3(self->m_data->m_device, (int64_t)sizeof(header));
        if (!bytes) return false;
        size = (size_t)XByteArray_size_base((const XContainer*)bytes);
        if (size > sizeof(header)) size = sizeof(header);
        if (size) memcpy(header, XByteArray_data(bytes), size);
        XByteArray_delete_base((XClass*)bytes);
    } else {
        return false;
    }
#if XIMAGECODEC_ON
    if (size && !XImageCodec_probeSize(header, size, XImageCodecFormat_Unknown,
                                       &self->m_data->m_sizeW,
                                       &self->m_data->m_sizeH))
        return false;
#else
    if (!XImageReader_probeBmpSize(header, size, &self->m_data->m_sizeW, &self->m_data->m_sizeH)) return false;
#endif
    self->m_data->m_hasSize = true;
    return true;
}

static void XImageReader_setError(XImageReader* self, XImageReaderError error, const char* message)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_errorString) XString_delete_base((XClass*)self->m_data->m_errorString);
    self->m_data->m_errorString = NULL;
    self->m_data->m_error = error;
    if (message) self->m_data->m_errorString = XString_create_utf8(message);
}

static void XImageReader_releaseHandler(XImageReaderPrivate* data)
{
    if (!data) return;
    if (data->m_handler) XImageIOHandler_delete_base(data->m_handler);
    data->m_handler = NULL;
    if (data->m_fileDevice) XClass_delete_base((XClass*)data->m_fileDevice);
    data->m_fileDevice = NULL;
}

static XImageIOHandler* XImageReader_ensureHandler(XImageReader* self)
{
    XImageReaderPrivate* data;
    if (!self || !(data = self->m_data)) return NULL;
    if (data->m_handler) return data->m_handler;
#if XIMAGEIOPLUGIN_ON
    if (data->m_device) {
        data->m_handler = XImagePluginRegistry_createReadHandler(data->m_device, data->m_format);
        return data->m_handler;
    }
    if (data->m_fileName) {
        XFile* file = XFile_create_2(data->m_fileName);
        if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
            if (file) XClass_delete_base((XClass*)file);
            return NULL;
        }
        data->m_handler = XImagePluginRegistry_createReadHandler((XIODevice*)file, data->m_format);
        if (data->m_handler) {
            data->m_fileDevice = (XIODevice*)file;
            return data->m_handler;
        }
        XIODevice_close_base((XIODevice*)file);
        XClass_delete_base((XClass*)file);
        return NULL;
    }
#else
    (void)self;
#endif
    return NULL;
}

static void VXImageReader_deinit(XImageReader* self)
{
    if (ISNULL(self, "XImageReader")) return;
    if (self->m_data)
    {
        if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
        if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
        if (self->m_data->m_errorString) XString_delete_base((XClass*)self->m_data->m_errorString);
        XImageReader_releaseHandler(self->m_data);
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
        if (self->m_data->m_animation) XImageCodecAnimation_delete(self->m_data->m_animation);
        if (self->m_data->m_sourceBytes) XByteArray_delete_base((XClass*)self->m_data->m_sourceBytes);
#endif
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
}

#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
static void XImageReader_clearAnimation(XImageReaderPrivate* data)
{
    if (!data) return;
    if (data->m_animation) XImageCodecAnimation_delete(data->m_animation);
    data->m_animation = NULL;
    if (data->m_sourceBytes) XByteArray_delete_base((XClass*)data->m_sourceBytes);
    data->m_sourceBytes = NULL;
    data->m_currentImageNumber = 0;
}

static bool XImageReader_prepareAnimation(XImageReader* self)
{
    XImageReaderPrivate* data;
    XByteArray* bytes = NULL;
    XFile* file = NULL;
    const uint8_t* raw;
    size_t size;
    if (!self || !(data = self->m_data) || data->m_animation) return data && data->m_animation;
    if (data->m_format && !XContainer_isEmpty_base((const XContainer*)data->m_format) &&
        XImageCodec_formatFromName(data->m_format) != XImageCodecFormat_Gif)
        return false;
    if (!data->m_fileName && data->m_device &&
        (!data->m_format || XImageCodec_formatFromName(data->m_format) == XImageCodecFormat_Gif)) {
        XByteArray* head = XIODevice_peek_3(data->m_device, 8);
        bool isGif = head && XImageCodec_detect((const uint8_t*)XByteArray_data(head),
                                                 XByteArray_size_base((const XContainer*)head)) == XImageCodecFormat_Gif;
        if (head) XByteArray_delete_base((XClass*)head);
        if (!isGif) return false;
    }
    if (data->m_fileName) {
        file = XFile_create_2(data->m_fileName);
        if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
            if (file) XClass_delete_base((XClass*)file);
            return false;
        }
        bytes = XIODevice_readAll_3((XIODevice*)file);
        XIODevice_close_base((XIODevice*)file);
        XClass_delete_base((XClass*)file);
    } else if (data->m_device) {
        bytes = XIODevice_readAll_3(data->m_device);
    }
    if (!bytes) return false;
    raw = (const uint8_t*)XByteArray_data(bytes);
    size = XByteArray_size_base((const XContainer*)bytes);
    if (XImageCodec_detect(raw, size) != XImageCodecFormat_Gif) {
        XByteArray_delete_base((XClass*)bytes);
        return false;
    }
    data->m_animation = XImageCodec_decodeAnimation(raw, size);
    if (!data->m_animation) {
        XByteArray_delete_base((XClass*)bytes);
        return false;
    }
    data->m_sourceBytes = bytes;
    data->m_currentImageNumber = 0;
    return true;
}
#endif

XVtable* XImageReader_class_init()
{
    XVTABLE_INIT_DEFAULT(XImageReader)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageReader_deinit);
    return XVTABLE_DEFAULT;
}

XImageReader* XImageReader_create_ex(XMemoryType memory)
{
    XImageReader* self = (XImageReader*)XMemory_malloc(sizeof(XImageReader), memory);
    if (!self) return NULL;
    XImageReader_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
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

void XImageReader_init_device(XImageReader* self, XIODevice* device, const XString* format)
{
    XImageReader_init(self);
    if (self->m_data)
    {
        self->m_data->m_device = device;
        if (format && !XContainer_isEmpty_base((const XContainer*)format))
            self->m_data->m_format = XString_create_copy(format);
    }
}

void XImageReader_init_device_2(XImageReader* self, XIODevice* device, const char* format)
{
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XImageReader_init_device(self, device, formatString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XImageReader_init_file(XImageReader* self, const XString* fileName, const XString* format)
{
    XImageReader_init(self);
    if (self->m_data)
    {
        if (fileName) self->m_data->m_fileName = XString_create_copy(fileName);
        if (format && !XContainer_isEmpty_base((const XContainer*)format))
            self->m_data->m_format = XString_create_copy(format);
    }
}

void XImageReader_init_file_2(XImageReader* self, const char* fileName, const char* format)
{
    XString* fileNameString = fileName ? XString_create_utf8(fileName) : NULL;
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XImageReader_init_file(self, fileNameString, formatString);
    if (fileNameString) XString_delete_base((XClass*)fileNameString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XImageReader_setFormat(XImageReader* self, const XString* format)
{
    if (!self || !self->m_data) return;
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    XImageReader_clearAnimation(self->m_data);
#endif
    XImageReader_releaseHandler(self->m_data);
    if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
    self->m_data->m_format = format ? XString_create_copy(format) : NULL;
    self->m_data->m_hasSize = false;
}

void XImageReader_setFormat_2(XImageReader* self, const char* format)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    XImageReader_setFormat(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageReader_format_const(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_format : NULL; }

XString* XImageReader_format(const XImageReader* self)
{
    const XString* value = XImageReader_format_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImageReader_format_2(const XImageReader* self)
{ return XString_toUtf8(XImageReader_format_const(self)); }

void XImageReader_setAutoDetectImageFormat(XImageReader* self, bool enabled)
{ if (self && self->m_data) { XImageReader_releaseHandler(self->m_data); self->m_data->m_autoDetectFormat = enabled; self->m_data->m_hasSize = false; } }

bool XImageReader_autoDetectImageFormat(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_autoDetectFormat : true; }

void XImageReader_setDecideFormatFromContent(XImageReader* self, bool ignored)
{ if (self && self->m_data) { XImageReader_releaseHandler(self->m_data); self->m_data->m_decideFromContent = ignored; self->m_data->m_autoDetectFormat = !ignored; self->m_data->m_hasSize = false; } }

bool XImageReader_decideFormatFromContent(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_decideFromContent : false; }

void XImageReader_setDevice(XImageReader* self, XIODevice* device)
{
    if (!self || !self->m_data) return;
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    XImageReader_clearAnimation(self->m_data);
#endif
    XImageReader_releaseHandler(self->m_data);
    self->m_data->m_device = device;
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = NULL;
    self->m_data->m_hasSize = false;
}

XIODevice* XImageReader_device(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_device : NULL; }

void XImageReader_setFileName(XImageReader* self, const XString* fileName)
{
    if (!self || !self->m_data) return;
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    XImageReader_clearAnimation(self->m_data);
#endif
    XImageReader_releaseHandler(self->m_data);
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = fileName ? XString_create_copy(fileName) : NULL;
    self->m_data->m_device = NULL;
    self->m_data->m_hasSize = false;
}

void XImageReader_setFileName_2(XImageReader* self, const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL;
    XImageReader_setFileName(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageReader_fileName_const(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_fileName : NULL; }

XString* XImageReader_fileName(const XImageReader* self)
{
    const XString* value = XImageReader_fileName_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImageReader_fileName_2(const XImageReader* self)
{ return XString_toUtf8(XImageReader_fileName_const(self)); }

void XImageReader_size(const XImageReader* self, XSize* out)
{
    if (!out) return;
    out->width = 0;
    out->height = 0;
    if (!self || !self->m_data) return;
    XImageReader_ensureHandler((XImageReader*)self);
    if (self->m_data->m_handler &&
        XImageIOHandler_option_base(self->m_data->m_handler,
                                    XImageIOHandlerOption_Size, out))
        return;
    if (XImageReader_probeSize((XImageReader*)self)) {
        out->width = self->m_data->m_sizeW;
        out->height = self->m_data->m_sizeH;
    }
}

XStringList* XImageReader_textKeys(const XImageReader* self)
{
    (void)self;
    return XImageReader_makeStringList(NULL, 0);
}
XString* XImageReader_text(const XImageReader* self, const XString* key)
{
    (void)self;
    (void)key;
    return XString_create();
}

const char* XImageReader_text_2(const XImageReader* self, const char* key)
{
    (void)self;
    (void)key;
    return NULL;
}

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

bool XImageReader_supportsAnimation(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (!self || !self->m_data) return false;
    if (self->m_data->m_format &&
        !XContainer_isEmpty_base((const XContainer*)self->m_data->m_format) &&
        XImageCodec_formatFromName(self->m_data->m_format) != XImageCodecFormat_Gif)
        return false;
    return XImageReader_prepareAnimation((XImageReader*)self);
#else
    (void)self;
    return false;
#endif
}

XImageIOHandlerTransformation XImageReader_transformation(const XImageReader* self)
{ (void)self; return XImageIOHandlerTransformation_None; }

void XImageReader_setAutoTransform(XImageReader* self, bool enabled)
{ if (self && self->m_data) self->m_data->m_autoTransform = enabled; }

bool XImageReader_autoTransform(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_autoTransform : false; }

XString* XImageReader_subType(const XImageReader* self)
{
    (void)self;
    return XString_create();
}

const XString* XImageReader_subType_const(const XImageReader* self)
{
    (void)self;
    return NULL;
}

const char* XImageReader_subType_2(const XImageReader* self)
{
    (void)self;
    return NULL;
}
XStringList* XImageReader_supportedSubTypes(const XImageReader* self)
{
    (void)self;
    return XImageReader_makeStringList(NULL, 0);
}

bool XImageReader_canRead(const XImageReader* self)
{
    if (!self || !self->m_data) return false;
    if (XImageReader_ensureHandler((XImageReader*)self))
        return XImageIOHandler_canRead_base(self->m_data->m_handler);
    if (self->m_data->m_format &&
        !XImageReader_isSupportedFormat(XString_toUtf8(self->m_data->m_format))) return false;
    if (self->m_data->m_device) {
        if (!self->m_data->m_autoDetectFormat && !self->m_data->m_format) return false;
        return XImageReader_isSupportedFormat(XImageReader_imageFormatDevice_2(self->m_data->m_device));
    }
    if (!self->m_data->m_fileName) return false;
    if (!self->m_data->m_autoDetectFormat && !self->m_data->m_format) return false;
    return XImageReader_isSupportedFormat(XImageReader_imageFormat_2(XString_toUtf8(self->m_data->m_fileName)));
}

bool XImageReader_read(XImageReader* self, XImage* out)
{
    bool loadedByHandler = false;
    XSize requestedSize;
    if (!self || !self->m_data || !out) return false;
    XImageReader_size(self, &requestedSize);
    if (g_imageReaderAllocationLimitMb > 0 && requestedSize.width > 0 &&
        requestedSize.height > 0) {
        uint64_t pixels = (uint64_t)(unsigned)requestedSize.width *
                          (uint64_t)(unsigned)requestedSize.height;
        uint64_t limit = (uint64_t)(unsigned)g_imageReaderAllocationLimitMb *
                         1024u * 1024u;
        if (pixels > limit / 4u) {
            XImageReader_setError(self, XImageReaderError_InvalidDataError,
                                  "Image exceeds the configured allocation limit");
            return false;
        }
    }
    if (!XClassIsVtableNull(out)) XImage_deinit_base(out);
    XImage_init(out);
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (XImageReader_prepareAnimation(self)) {
        XImageCodecAnimation* animation = self->m_data->m_animation;
        int frame = self->m_data->m_currentImageNumber;
        if (animation && frame >= 0 && frame < animation->frameCount) {
            XImage_copy_base(out, &animation->frames[frame].image);
            loadedByHandler = !XImage_isNull(out);
        }
    }
#endif
    if (!loadedByHandler)
        XImageReader_ensureHandler(self);
    if (!loadedByHandler && self->m_data->m_handler)
    {
        loadedByHandler = XImageIOHandler_read_base(self->m_data->m_handler, out);
        if (!loadedByHandler) {
            XImageReader_setError(self, XImageReaderError_InvalidDataError, "Image handler failed to read data");
            return false;
        }
    }
    if (!loadedByHandler && self->m_data->m_fileName)
    {
        if (!XImageReader_isSupportedFormat(XString_toUtf8(self->m_data->m_format))) {
            XImageReader_setError(self, XImageReaderError_UnsupportedFormatError,
                                  "The requested image format has no built-in decoder");
            return false;
        }
        if (!XImage_load_2(out, XString_toUtf8(self->m_data->m_fileName),
                         XString_toUtf8(self->m_data->m_format))) {
            if (!XFile_exists_static(self->m_data->m_fileName))
                XImageReader_setError(self, XImageReaderError_FileNotFoundError,
                                      "Image file could not be opened");
            else {
                XImageReader_setError(self, XImageReaderError_InvalidDataError,
                                      "Image data is invalid or unsupported");
            }
            return false;
        }
    }
    else if (!loadedByHandler && self->m_data->m_device) {
        XByteArray* bytes = XIODevice_readAll_3(self->m_data->m_device);
        int64_t size = bytes ? (int64_t)XByteArray_size_base((const XContainer*)bytes) : 0;
        bool ok = bytes && size <= INT_MAX &&
                  XImage_loadFromData_2(out, (const uint8_t*)XByteArray_data(bytes),
                                      (int)size, XString_toUtf8(self->m_data->m_format));
        if (bytes) XByteArray_delete_base((XClass*)bytes);
        if (!ok) {
            XImageReader_setError(self, XImageReaderError_InvalidDataError,
                                  "Image data from the device is invalid or unsupported");
            return false;
        }
    } else if (!loadedByHandler) {
        XImageReader_setError(self, XImageReaderError_DeviceError, "No image source is set");
        return false;
    }

    if (self->m_data->m_hasClipRect) {
        XImage clipped;
        XRect clipRect;
        XImage_init(&clipped);
        clipRect.x = self->m_data->m_clipX;
        clipRect.y = self->m_data->m_clipY;
        clipRect.width = self->m_data->m_clipW;
        clipRect.height = self->m_data->m_clipH;
        XImage_copyRect(out, &clipRect, &clipped);
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
    if (self->m_data->m_hasScaledClipRect) {
        XImage clipped;
        XRect clipRect;
        XImage_init(&clipped);
        clipRect.x = self->m_data->m_scaledClipX;
        clipRect.y = self->m_data->m_scaledClipY;
        clipRect.width = self->m_data->m_scaledClipW;
        clipRect.height = self->m_data->m_scaledClipH;
        XImage_copyRect(out, &clipRect, &clipped);
        XImage_deinit_base(out);
        XImage_move_base(out, &clipped);
        XImage_deinit_base(&clipped);
    }
    XImageReader_setError(self, XImageReaderError_UnknownError, NULL);
    return !XImage_isNull(out);
}

bool XImageReader_jumpToNextImage(XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (XImageReader_prepareAnimation(self) && self->m_data->m_animation &&
        self->m_data->m_currentImageNumber + 1 < self->m_data->m_animation->frameCount) {
        ++self->m_data->m_currentImageNumber;
        return true;
    }
#else
    (void)self;
#endif
    return false;
}
bool XImageReader_jumpToImage(XImageReader* self, int imageNumber)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (imageNumber < 0 || !XImageReader_prepareAnimation(self) ||
        !self->m_data->m_animation || imageNumber >= self->m_data->m_animation->frameCount)
        return false;
    self->m_data->m_currentImageNumber = imageNumber;
    return true;
#else
    return imageNumber == 0 && XImageReader_canRead(self);
#endif
}
int XImageReader_loopCount(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    return (self && self->m_data && XImageReader_prepareAnimation((XImageReader*)self) && self->m_data->m_animation) ? self->m_data->m_animation->loopCount : 0;
#else
    (void)self; return 0;
#endif
}
int XImageReader_imageCount(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (self && self->m_data && XImageReader_prepareAnimation((XImageReader*)self) && self->m_data->m_animation)
        return self->m_data->m_animation->frameCount;
#endif
    return XImageReader_canRead(self) ? 1 : 0;
}
int XImageReader_nextImageDelay(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (self && self->m_data && XImageReader_prepareAnimation((XImageReader*)self) && self->m_data->m_animation) {
        int frame = self->m_data->m_currentImageNumber;
        if (frame >= 0 && frame < self->m_data->m_animation->frameCount)
            return self->m_data->m_animation->frames[frame].delayMs;
    }
#else
    (void)self;
#endif
    return 0;
}
int XImageReader_currentImageNumber(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    return self && self->m_data ? self->m_data->m_currentImageNumber : 0;
#else
    (void)self; return 0;
#endif
}

void XImageReader_currentImageRect(const XImageReader* self, XRect* out)
{
    XSize size;
    if (!out) return;
    memset(out, 0, sizeof(XRect));
    XImageReader_size(self, &size);
    out->width = size.width;
    out->height = size.height;
}

XImageReaderError XImageReader_error(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_error : XImageReaderError_UnknownError; }

XString* XImageReader_errorString(const XImageReader* self)
{
    const XString* value = XImageReader_errorString_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const XString* XImageReader_errorString_const(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_errorString : NULL; }

const char* XImageReader_errorString_2(const XImageReader* self)
{ return XString_toUtf8(XImageReader_errorString_const(self)); }

bool XImageReader_supportsOption(const XImageReader* self, XImageIOHandlerOption option)
{
    if (self && self->m_data && self->m_data->m_handler &&
        XImageIOHandler_supportsOption_base(self->m_data->m_handler, option))
        return true;
    switch (option) {
    case XImageIOHandlerOption_Size:
    case XImageIOHandlerOption_ImageFormat:
        return true;
    default:
        return false;
    }
}

XString* XImageReader_imageFormat(const XString* fileName)
{
    unsigned char signature[16];
    size_t size; XFile* file; XByteArray* bytes;
    if (!fileName) return XString_create();
    file = XFile_create_2(fileName); if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) { if (file) XClass_delete_base((XClass*)file); return XString_create(); }
    bytes = XIODevice_peek_3((XIODevice*)file, sizeof(signature)); XIODevice_close_base((XIODevice*)file); XClass_delete_base((XClass*)file); if (!bytes) return XString_create(); size = XByteArray_size_base((const XContainer*)bytes); if (size > sizeof(signature)) size = sizeof(signature); memcpy(signature, XByteArray_data(bytes), size); XByteArray_delete_base((XClass*)bytes);
    {
        const char* format = XImageReader_detectSignature(signature, size);
        return format ? XString_create_utf8(format) : XString_create();
    }
}

const char* XImageReader_imageFormat_2(const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL; XString* result = XImageReader_imageFormat(value); const char* utf8 = XString_toUtf8(result); static char format[16]; if (utf8) { strncpy(format, utf8, sizeof(format) - 1); format[sizeof(format) - 1] = '\0'; } else format[0] = '\0'; if (result) XString_delete_base((XClass*)result); if (value) XString_delete_base((XClass*)value); return format[0] ? format : NULL;
}

XString* XImageReader_imageFormatDevice(XIODevice* device)
{
    XByteArray* bytes;
    const char* result = NULL;
    if (!device) return XString_create();
    bytes = XIODevice_peek_3(device, 16);
    if (bytes) {
        result = XImageReader_detectSignature(
            (const unsigned char*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base((const XContainer*)bytes));
        XByteArray_delete_base((XClass*)bytes);
    }
    return result ? XString_create_utf8(result) : XString_create();
}

const char* XImageReader_imageFormatDevice_2(XIODevice* device)
{
    XByteArray* bytes;
    const char* result = NULL;
    if (!device) return NULL;
    bytes = XIODevice_peek_3(device, 16);
    if (bytes) {
        result = XImageReader_detectSignature(
            (const unsigned char*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base((const XContainer*)bytes));
        XByteArray_delete_base((XClass*)bytes);
    }
    return result;
}

XStringList* XImageReader_supportedImageFormats()
{
    return XImageReader_supportedFormats();
}

XStringList* XImageReader_supportedMimeTypes()
{
    XStringList* result = XStringList_create();
    size_t i;
    if (!result) return NULL;
#if XIMAGECODEC_ON
    for (i = 0; i < sizeof(g_imageReaderFormats) / sizeof(g_imageReaderFormats[0]); ++i)
        if (XImageCodec_canDecode(XImageCodec_formatFromName_2(g_imageReaderFormats[i])))
            XStringList_push_back_utf8(result, g_imageReaderMimeTypes[i]);
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_supportedMimeTypes(true);
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

XStringList* XImageReader_imageFormatsForMimeType(const XString* mimeType)
{
    const char* mime = XString_toUtf8(mimeType);
    XStringList* result = XImageReader_makeStringList(NULL, 0);
    if (!result) return NULL;
    if (mime) {
        if (XImageReader_mimeEquals(mime, "image/bmp") && XImageReader_isSupportedFormat("bmp")) { const char* value[] = {"bmp"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/png") && XImageReader_isSupportedFormat("png")) { const char* value[] = {"png"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/gif") && XImageReader_isSupportedFormat("gif")) { const char* value[] = {"gif"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/jpeg") && XImageReader_isSupportedFormat("jpeg")) { const char* value[] = {"jpeg"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/svg+xml") && XImageReader_isSupportedFormat("svg")) { const char* value[] = {"svg"}; XImageReader_makeStringList_prefix(result, value, 1); }
    }
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_imageFormatsForMimeType(mimeType, true);
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
    /* Match Qt's value-returning API: an unknown MIME type is an empty list,
       rather than a list containing an unrelated fallback format. */
    return result;
}

XStringList* XImageReader_imageFormatsForMimeType_2(const char* mimeType)
{
    XString* value = mimeType ? XString_create_utf8(mimeType) : NULL;
    XStringList* result = XImageReader_imageFormatsForMimeType(value);
    if (value) XString_delete_base((XClass*)value);
    return result;
}
int XImageReader_allocationLimit() { return g_imageReaderAllocationLimitMb; }
void XImageReader_setAllocationLimit(int mbLimit)
{
    g_imageReaderAllocationLimitMb = mbLimit < 0 ? 0 : mbLimit;
}
