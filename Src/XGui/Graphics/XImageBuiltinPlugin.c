/******************************************************************************
 * @file       XImageBuiltinPlugin.c
 * @brief      XImageCodec 内置图像插件实现。
 * @note       插件把 XImageCodec 的格式发现、解码与编码能力包装成 Qt 风格
 *             图像插件，供 XImagePluginRegistry 统一发现和创建处理器。
 ******************************************************************************/
#include "XImageBuiltinPlugin.h"
#include "XImageCodec.h"
#include "XImageIOHandler.h"
#include "XContainer.h"
#include "XByteArray.h"
#include "XStringList.h"
#include "XMemory.h"
#include <string.h>

#if XIMAGEIOPLUGIN_ON

static const char* const g_builtinFormats[] =
    { "bmp", "png", "jpeg", "gif", "svg" };
static const char* const g_builtinMimeTypes[] =
    { "image/bmp", "image/png", "image/jpeg", "image/gif", "image/svg+xml" };
static const char* const g_builtinNameFilters[] =
    { "*.bmp", "*.png", "*.jpeg", "*.gif", "*.svg" };

XCLASS_DEFINE_BEGING(XImageBuiltinHandler)
XCLASS_DEFINE_EXTEND_END(XImageBuiltinHandler, XImageIOHandler)

typedef struct XImageBuiltinHandler
{
    XImageIOHandler m_base; /**< 基类成员；必须是第一个。 */
} XImageBuiltinHandler;

XCLASS_DEFINE_BEGING(XImageBuiltinPlugin)
XCLASS_DEFINE_EXTEND_END(XImageBuiltinPlugin, XImageIOPlugin)

typedef struct XImageBuiltinPlugin
{
    XImageIOPlugin m_base;    /**< 基类成员；必须是第一个。 */
    XStringList*   m_keys;    /**< 插件声明的格式键列表。 */
    XStringList*   m_mimes;   /**< 插件声明的 MIME 类型列表。 */
    XStringList*   m_filters; /**< 插件声明的文件名过滤器列表。 */
} XImageBuiltinPlugin;

#if XIMAGECODEC_ON

static XImageCodecFormat builtin_formatFromString(const XString* format)
{
    return XImageCodec_formatFromName_2(XString_toUtf8(format));
}

static uint32_t builtin_capabilityForFormat(const XString* format)
{
    XImageCodecFormat codecFormat;
    uint32_t capability = 0;
    if (!format || XContainer_isEmpty_base((const XContainer*)format))
        return 0;
    codecFormat = builtin_formatFromString(format);
    if (codecFormat == XImageCodecFormat_Unknown)
        return 0;
    if (XImageCodec_canDecode(codecFormat))
        capability |= (uint32_t)XImageIOPlugin_CanRead;
    if (XImageCodec_canEncode(codecFormat))
        capability |= (uint32_t)XImageIOPlugin_CanWrite;
    return capability;
}

static XImageCodecFormat builtin_detectWithDevice(XIODevice* device)
{
    XByteArray* header;
    XImageCodecFormat format = XImageCodecFormat_Unknown;
    if (!device) return format;
    header = XIODevice_peek_3(device, 16);
    if (header) {
        format = XImageCodec_detect(
            (const uint8_t*)XByteArray_data(header),
            (size_t)XByteArray_size_base((const XContainer*)header));
        XByteArray_delete_base((XClass*)header);
    }
    return format;
}

/* 按 Qt QBmpHandler::option(ImageFormat) 规则从 BMP 信息头推导像素格式。
 * 仅窥视固定头部，不改变设备当前位置，也不触发整幅图像分配。 */
static XImageFormat builtin_bmpImageFormat(XIODevice* device)
{
    XByteArray* bytes;
    const uint8_t* data;
    size_t size;
    uint16_t bpp;
    if (!device) return XImageFormat_Invalid;
    bytes = XIODevice_peek_3(device, 34);
    if (!bytes) return XImageFormat_Invalid;
    data = (const uint8_t*)XByteArray_data(bytes);
    size = (size_t)XByteArray_size_base((const XContainer*)bytes);
    if (!data || size < 34 || data[0] != 'B' || data[1] != 'M') {
        XByteArray_delete_base((XClass*)bytes);
        return XImageFormat_Invalid;
    }
    bpp = (uint16_t)data[28] | ((uint16_t)data[29] << 8);
    XByteArray_delete_base((XClass*)bytes);
    switch (bpp) {
        case 32:
        case 24:
        case 16:
            return XImageFormat_RGB32;
        case 8:
        case 4:
            return XImageFormat_Indexed8;
        default:
            return XImageFormat_Mono;
    }
}

static XImageFormat builtin_imageFormat(XImageIOHandler* self)
{
    XImageCodecFormat format;
    if (!self) return XImageFormat_Invalid;
    format = builtin_formatFromString(XImageIOHandler_format_const(self));
    if (format == XImageCodecFormat_Unknown)
        format = builtin_detectWithDevice(XImageIOHandler_device(self));
    if (format == XImageCodecFormat_Bmp)
        return builtin_bmpImageFormat(XImageIOHandler_device(self));
    /* The portable codec facade currently normalizes non-BMP decoded output
       to ARGB32; keep that as the handler's conservative image format. */
    if (format != XImageCodecFormat_Unknown)
        return XImageFormat_ARGB32;
    return XImageFormat_Invalid;
}

static bool builtin_writeBytes(XIODevice* device, const XByteArray* bytes)
{
    const char* data;
    int64_t total;
    int64_t offset = 0;
    if (!device || !bytes) return false;
    data = (const char*)XByteArray_data((XByteArray*)bytes);
    total = (int64_t)XByteArray_size_base((const XContainer*)bytes);
    while (offset < total) {
        int64_t written = XIODevice_write_1(device, data + offset, total - offset);
        if (written <= 0) return false;
        offset += written;
    }
    return XIODevice_flush(device);
}

static bool VXImageBuiltinHandler_canRead(const XImageIOHandler* self)
{
    XIODevice* device;
    XImageCodecFormat format;
    XImageCodecFormat detected;
    if (!self) return false;
    device = XImageIOHandler_device(self);
    if (!device) return false;
    format = builtin_formatFromString(XImageIOHandler_format_const(self));
    if (format == XImageCodecFormat_Unknown)
        format = builtin_detectWithDevice(device);
    if (format == XImageCodecFormat_Unknown || !XImageCodec_canDecode(format))
        return false;
    /* Qt treats the explicit format as a hint.  A readable handler still has
     * to validate the device contents before accepting unrelated bytes. */
    detected = builtin_detectWithDevice(device);
    return detected == format;
}

static bool VXImageBuiltinHandler_read(XImageIOHandler* self, XImage* image)
{
    XIODevice* device;
    XImageCodecFormat format;
    XByteArray* bytes;
    bool ok = false;
    if (!self || !image || !(device = XImageIOHandler_device(self)))
        return false;
    format = builtin_formatFromString(XImageIOHandler_format_const(self));
    bytes = XIODevice_readAll_3(device);
    if (!bytes) return false;
    if (format == XImageCodecFormat_Unknown)
        format = XImageCodec_detect(
            (const uint8_t*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base((const XContainer*)bytes));
    if (format != XImageCodecFormat_Unknown && XImageCodec_canDecode(format))
        ok = XImageCodec_decode((const uint8_t*)XByteArray_data(bytes),
                                (size_t)XByteArray_size_base((const XContainer*)bytes),
                                format, image);
    XByteArray_delete_base((XClass*)bytes);
    return ok;
}

static bool VXImageBuiltinHandler_write(XImageIOHandler* self, const XImage* image)
{
    XIODevice* device;
    XImageCodecFormat format;
    XByteArray* bytes;
    int quality = -1;
    XImageIOHandlerOptionValue value;
    bool ok = false;
    if (!self || !image || !(device = XImageIOHandler_device(self)))
        return false;
    format = XImageCodec_formatFromName_2(XImageIOHandler_format_2(self));
    if (format == XImageCodecFormat_Unknown || !XImageCodec_canEncode(format))
        return false;
    memset(&value, 0, sizeof(value));
    if (XImageIOHandler_optionValue(self, XImageIOHandlerOption_Quality, &value))
        quality = value.integer;
    bytes = XByteArray_create();
    if (!bytes) return false;
    if (XImageCodec_encode(image, format, quality, bytes))
        ok = builtin_writeBytes(device, bytes);
    XByteArray_delete_base((XClass*)bytes);
    return ok;
}

static void VXImageBuiltinHandler_setOption(XImageIOHandler* self,
                                             XImageIOHandlerOption option,
                                             const void* value)
{
    if (option == XImageIOHandlerOption_Quality)
        XImageIOHandler_storeOptionValue(self, option, value);
}

static bool VXImageBuiltinHandler_option(const XImageIOHandler* self,
                                         XImageIOHandlerOption option,
                                         void* out)
{
    XImageIOHandlerOptionValue* value = (XImageIOHandlerOptionValue*)out;
    if (option != XImageIOHandlerOption_ImageFormat || !value)
        return false;
    value->format = builtin_imageFormat((XImageIOHandler*)self);
    return value->format != XImageFormat_Invalid;
}

static bool VXImageBuiltinHandler_supportsOption(const XImageIOHandler* self,
                                                 XImageIOHandlerOption option)
{
    (void)self;
    return option == XImageIOHandlerOption_Quality ||
           option == XImageIOHandlerOption_ImageFormat;
}

static XVtable* XImageBuiltinHandler_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XImageBuiltinHandler)
    XVTABLE_INHERIT_XCLASS(XImageIOHandler);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_CanRead, VXImageBuiltinHandler_canRead);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Read, VXImageBuiltinHandler_read);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Write, VXImageBuiltinHandler_write);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Option, VXImageBuiltinHandler_option);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_SetOption, VXImageBuiltinHandler_setOption);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_SupportsOption, VXImageBuiltinHandler_supportsOption);
    return XVTABLE_DEFAULT;
}

static XImageBuiltinHandler* XImageBuiltinHandler_create(void)
{
    XImageBuiltinHandler* self = (XImageBuiltinHandler*)XMemory_malloc(
        sizeof(XImageBuiltinHandler), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XImageIOHandler_init(&self->m_base);
    XClassSetVtable(self, XImageBuiltinHandler);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

static uint32_t VXImageBuiltinPlugin_capabilities(const XImageIOPlugin* self,
                                                  XIODevice* device,
                                                  const XString* format)
{
    uint32_t capability = builtin_capabilityForFormat(format);
    XImageCodecFormat requested;
    XImageCodecFormat detected;
    (void)self;
    if (format && !XContainer_isEmpty_base((const XContainer*)format)) {
        if (!device) return capability;
        requested = builtin_formatFromString(format);
        if (requested == XImageCodecFormat_Unknown)
            return 0;
        detected = builtin_detectWithDevice(device);
        /* Writing does not inspect the output device.  Reading does, as
         * required by QImageIOPlugin::capabilities(). */
        if (detected != requested)
            capability &= ~(uint32_t)XImageIOPlugin_CanRead;
        return capability;
    }
    if (!device) return capability;
    detected = builtin_detectWithDevice(device);
    return (detected != XImageCodecFormat_Unknown &&
            XImageCodec_canDecode(detected))
        ? (uint32_t)XImageIOPlugin_CanRead : 0;
}

static XImageIOHandler* VXImageBuiltinPlugin_create(XImageIOPlugin* self,
                                                    XIODevice* device,
                                                    const XString* format)
{
    XImageBuiltinHandler* handler;
    (void)self;
    (void)format;
    handler = XImageBuiltinHandler_create();
    if (handler)
        XImageIOHandler_setDevice(&handler->m_base, device);
    return (XImageIOHandler*)handler;
}

static XStringList* VXImageBuiltinPlugin_keys(const XImageIOPlugin* self)
{
    return self ? ((XImageBuiltinPlugin*)self)->m_keys : NULL;
}

static XStringList* VXImageBuiltinPlugin_mimeTypes(const XImageIOPlugin* self)
{
    return self ? ((XImageBuiltinPlugin*)self)->m_mimes : NULL;
}

static XStringList* VXImageBuiltinPlugin_nameFilters(const XImageIOPlugin* self)
{
    return self ? ((XImageBuiltinPlugin*)self)->m_filters : NULL;
}

static void VXImageBuiltinPlugin_deinit(XImageIOPlugin* self)
{
    XImageBuiltinPlugin* plugin;
    if (!self) return;
    plugin = (XImageBuiltinPlugin*)self;
    if (plugin->m_keys) XStringList_delete_base((XClass*)plugin->m_keys);
    if (plugin->m_mimes) XStringList_delete_base((XClass*)plugin->m_mimes);
    if (plugin->m_filters) XStringList_delete_base((XClass*)plugin->m_filters);
    plugin->m_keys = NULL;
    plugin->m_mimes = NULL;
    plugin->m_filters = NULL;
    XClass_Deinit_Parent(XImageIOPlugin, self);
}

static XVtable* XImageBuiltinPlugin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XImageBuiltinPlugin)
    XVTABLE_INHERIT_XCLASS(XImageIOPlugin);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageBuiltinPlugin_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Capabilities, VXImageBuiltinPlugin_capabilities);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Create, VXImageBuiltinPlugin_create);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Keys, VXImageBuiltinPlugin_keys);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_MimeTypes, VXImageBuiltinPlugin_mimeTypes);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_NameFilters, VXImageBuiltinPlugin_nameFilters);
    return XVTABLE_DEFAULT;
}

static XImageBuiltinPlugin g_builtinPlugin;

static XImageBuiltinPlugin* XImageBuiltinPlugin_ensure(void)
{
    int64_t i;
    if (g_builtinPlugin.m_keys) return &g_builtinPlugin;
    memset(&g_builtinPlugin, 0, sizeof(g_builtinPlugin));
    XImageIOPlugin_init(&g_builtinPlugin.m_base);
    g_builtinPlugin.m_keys = XStringList_create();
    g_builtinPlugin.m_mimes = XStringList_create();
    g_builtinPlugin.m_filters = XStringList_create();
    if (!g_builtinPlugin.m_keys || !g_builtinPlugin.m_mimes ||
        !g_builtinPlugin.m_filters) {
        if (g_builtinPlugin.m_keys)
            XStringList_delete_base((XClass*)g_builtinPlugin.m_keys);
        if (g_builtinPlugin.m_mimes)
            XStringList_delete_base((XClass*)g_builtinPlugin.m_mimes);
        if (g_builtinPlugin.m_filters)
            XStringList_delete_base((XClass*)g_builtinPlugin.m_filters);
        memset(&g_builtinPlugin, 0, sizeof(g_builtinPlugin));
        return NULL;
    }
    for (i = 0; i < 5; ++i) {
        XStringList_push_back_utf8(g_builtinPlugin.m_keys, g_builtinFormats[i]);
        XStringList_push_back_utf8(g_builtinPlugin.m_mimes, g_builtinMimeTypes[i]);
        XStringList_push_back_utf8(g_builtinPlugin.m_filters, g_builtinNameFilters[i]);
    }
    XClassSetVtable(&g_builtinPlugin, XImageBuiltinPlugin);
    return &g_builtinPlugin;
}

#endif /* XIMAGECODEC_ON */

XImageIOPlugin* XImageBuiltinPlugin_instance(void)
{
#if XIMAGECODEC_ON
    return (XImageIOPlugin*)XImageBuiltinPlugin_ensure();
#else
    return NULL;
#endif
}

#else /* XIMAGEIOPLUGIN_ON */

XImageIOPlugin* XImageBuiltinPlugin_instance(void)
{
    return NULL;
}

#endif /* XIMAGEIOPLUGIN_ON */
