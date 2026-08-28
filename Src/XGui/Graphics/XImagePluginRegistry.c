/*
 * @file       XImagePluginRegistry.c
 * @brief      XImageIOPlugin 源码级注册表实现。
 */
#include "XImagePluginRegistry.h"
#include "XImageBuiltinPlugin.h"
#include "XContainer.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>

#if XIMAGEIOPLUGIN_ON

static XImageIOPlugin* g_plugins[XIMAGEPLUGINREGISTRY_CAPACITY];
static int g_pluginCount = 0;
static bool g_builtinRegistered = false;

static void XImagePluginRegistry_ensureBuiltin(void)
{
    XImageIOPlugin* builtin;
    if (g_builtinRegistered) return;
    builtin = XImageBuiltinPlugin_instance();
    if (builtin) XImagePluginRegistry_addPlugin(builtin);
    g_builtinRegistered = true;
}

/* 为避免修改借用字符串，case-insensitive 比较改在本地副本上完成。 */
static bool XImagePluginRegistry_mimeEquals(const char* mimeType, const char* expected)
{
    /* QImageReaderWriterHelpers::imageFormatsForMimeType() compares the
     * QByteArray MIME value exactly.  MIME lookup is therefore deliberately
     * case-sensitive even though image format names are normalized to lower
     * case by QImageReader/QImageWriter. */
    return mimeType && expected && strcmp(mimeType, expected) == 0;
}

static bool XImagePluginRegistry_formatEmpty(const XString* format)
{
    return !format || XContainer_isEmpty_base((const XContainer*)format);
}

static const char* XImagePluginRegistry_formatUtf8(const XString* format)
{
    return XString_toUtf8(format);
}

/*
 * QImageReader 在调用插件 capabilities()/create() 前后会保护非顺序设备
 * 的当前位置（对应 Qt qimagereader.cpp:203-219、224-254）。插件可以为了
 * 判断魔数而读取设备，但该读取不能改变后续处理器看到的起始偏移。
 */
static int64_t XImagePluginRegistry_savePos(XIODevice* device)
{
    if (!device || XIODevice_isSequential(device)) return -1;
    return XIODevice_pos_base(device);
}

static void XImagePluginRegistry_restorePos(XIODevice* device, int64_t pos)
{
    if (device && pos >= 0) (void)XIODevice_seek_base(device, pos);
}

/* 内置处理器通过 XIODevice_peek() 建立内部预读缓存，不能只恢复底层
 * 文件描述符位置，否则后续 readAll() 会再次读到已经缓存的前缀。 */
static bool XImagePluginRegistry_needsPositionRestore(XImageIOPlugin* plugin)
{
    XImageIOPlugin* builtin = XImageBuiltinPlugin_instance();
    return plugin && plugin != builtin;
}

/* QImageReader/QImageWriter 将传给插件的格式名规范化为小写。 */
static XString* XImagePluginRegistry_normalizedFormat(const XString* format)
{
    if (XImagePluginRegistry_formatEmpty(format)) return NULL;
    return XString_toLower(format);
}

static XImageIOPluginCapability XImagePluginRegistry_capabilityFor(bool readOnly)
{
    return readOnly ? XImageIOPlugin_CanRead : XImageIOPlugin_CanWrite;
}

static uint32_t XImagePluginRegistry_capabilitiesUtf8(XImageIOPlugin* plugin,
                                                      XIODevice* device,
                                                      const char* formatUtf8)
{
    XString* value = formatUtf8 ? XString_create_utf8(formatUtf8) : NULL;
    int64_t pos = XImagePluginRegistry_needsPositionRestore(plugin)
        ? XImagePluginRegistry_savePos(device) : -1;
    uint32_t cap = XImageIOPlugin_capabilities_base(plugin, device, value);
    XImagePluginRegistry_restorePos(device, pos);
    if (value) XString_delete_base((XClass*)value);
    return cap;
}

static bool XImagePluginRegistry_pluginHasKey(XImageIOPlugin* plugin, const char* key)
{
    XStringList* keys;
    if (!plugin || !key) return false;
    keys = XImageIOPlugin_keys_base(plugin);
    return keys && XStringList_contains_utf8(keys, key, XChar_CaseInsensitive);
}

static bool XImagePluginRegistry_appendUniqueUtf8(XStringList* list, const char* value)
{
    if (!list || !value || !value[0]) return false;
    if (XStringList_contains_utf8(list, value, XChar_CaseSensitive)) return false;
    XStringList_push_back_utf8(list, value);
    return true;
}

static const char* XImagePluginRegistry_fallbackMime(const char* format)
{
    if (!format) return NULL;
    if (XImagePluginRegistry_mimeEquals(format, "bmp")) return "image/bmp";
    if (XImagePluginRegistry_mimeEquals(format, "png")) return "image/png";
    if (XImagePluginRegistry_mimeEquals(format, "jpeg") ||
        XImagePluginRegistry_mimeEquals(format, "jpg")) return "image/jpeg";
    if (XImagePluginRegistry_mimeEquals(format, "gif")) return "image/gif";
    if (XImagePluginRegistry_mimeEquals(format, "svg") ||
        XImagePluginRegistry_mimeEquals(format, "svgz")) return "image/svg+xml";
    return NULL;
}

static bool XImagePluginRegistry_pluginSupports(XImageIOPlugin* plugin, bool readOnly,
                                                XIODevice* device, const char* formatUtf8)
{
    uint32_t wanted = (uint32_t)XImagePluginRegistry_capabilityFor(readOnly);
    uint32_t actual = XImagePluginRegistry_capabilitiesUtf8(plugin, device, formatUtf8);
    if ((actual & wanted) == 0) return false;
    if (!formatUtf8 || !formatUtf8[0]) return true;
    return XImagePluginRegistry_pluginHasKey(plugin, formatUtf8);
}

static XStringList* XImagePluginRegistry_emptyList(void)
{
    return XStringList_create();
}

void XImagePluginRegistry_clear(void)
{
    g_pluginCount = 0;
    g_builtinRegistered = false;
}

bool XImagePluginRegistry_addPlugin(XImageIOPlugin* plugin)
{
    int i;
    int insertIndex;
    XImageIOPlugin* builtin;
    if (!plugin) return false;
    for (i = 0; i < g_pluginCount; ++i)
        if (g_plugins[i] == plugin) return false;
    if (g_pluginCount >= XIMAGEPLUGINREGISTRY_CAPACITY) return false;
    /* Qt 的 QFactoryLoader 先尝试外部 imageformats 插件，内置处理器仅在
       插件未匹配时使用；将显式注册插件插入内置插件之前即可保持该覆盖
       语义，同时保留同一插件的注册顺序。 */
    builtin = XImageBuiltinPlugin_instance();
    insertIndex = g_pluginCount;
    if (plugin != builtin && builtin) {
        for (i = 0; i < g_pluginCount; ++i) {
            if (g_plugins[i] == builtin) {
                insertIndex = i;
                break;
            }
        }
    }
    for (i = g_pluginCount; i > insertIndex; --i)
        g_plugins[i] = g_plugins[i - 1];
    g_plugins[insertIndex] = plugin;
    ++g_pluginCount;
    return true;
}

bool XImagePluginRegistry_removePlugin(XImageIOPlugin* plugin)
{
    int i;
    if (!plugin) return false;
    for (i = 0; i < g_pluginCount; ++i) {
        if (g_plugins[i] == plugin) {
            for (; i + 1 < g_pluginCount; ++i)
                g_plugins[i] = g_plugins[i + 1];
            --g_pluginCount;
            return true;
        }
    }
    return false;
}

int XImagePluginRegistry_pluginCount(void)
{
    XImagePluginRegistry_ensureBuiltin();
    return g_pluginCount;
}

XImageIOPlugin* XImagePluginRegistry_pluginAt(int index)
{
    XImagePluginRegistry_ensureBuiltin();
    return index >= 0 && index < g_pluginCount ? g_plugins[index] : NULL;
}

static void XImagePluginRegistry_setupHandler(XImageIOHandler* handler,
                                              XIODevice* device,
                                              const XString* format)
{
    if (!handler) return;
    XImageIOHandler_setDevice(handler, device);
    if (format && !XContainer_isEmpty_base((const XContainer*)format))
        XImageIOHandler_setFormat(handler, format);
}

XImageIOHandler* XImagePluginRegistry_createReadHandlerEx(
    XIODevice* device, const XString* format,
    bool autoDetectImageFormat, bool decideFormatFromContent)
{
    int i;
    XImageIOHandler* handler;
    XString* normalizedFormat;
    const XString* effectiveFormat;
    if (!device) return NULL;
    XImagePluginRegistry_ensureBuiltin();
    normalizedFormat = XImagePluginRegistry_normalizedFormat(format);
    effectiveFormat = normalizedFormat ? normalizedFormat : format;
    /* Qt 的 ignoresFormatAndExtension 分支会完全忽略显式格式；内容决策
       开启时必须直接进入空格式探测，不能先调用格式插件。 */
    if (!decideFormatFromContent && effectiveFormat &&
        !XContainer_isEmpty_base((const XContainer*)effectiveFormat)) {
        for (i = 0; i < g_pluginCount; ++i) {
            XImageIOPlugin* plugin = g_plugins[i];
            uint32_t wanted = (uint32_t)XImageIOPlugin_CanRead;
            uint32_t actual = XImagePluginRegistry_capabilitiesUtf8(
                plugin, device, XString_toUtf8(effectiveFormat));
            if ((actual & wanted) != 0 &&
                XImagePluginRegistry_pluginHasKey(plugin,
                                                  XString_toUtf8(effectiveFormat))) {
                int64_t pos = XImagePluginRegistry_needsPositionRestore(plugin)
                    ? XImagePluginRegistry_savePos(device) : -1;
                handler = XImageIOPlugin_create_base(plugin, device, effectiveFormat);
                XImagePluginRegistry_restorePos(device, pos);
                if (handler) {
                    XImagePluginRegistry_setupHandler(handler, device,
                                                      effectiveFormat);
                    goto done;
                }
            }
        }
    }
    if (autoDetectImageFormat || decideFormatFromContent) {
        for (i = 0; i < g_pluginCount; ++i) {
            XImageIOPlugin* plugin = g_plugins[i];
            uint32_t wanted = (uint32_t)XImageIOPlugin_CanRead;
            uint32_t actual = XImagePluginRegistry_capabilitiesUtf8(plugin, device, NULL);
            if ((actual & wanted) != 0) {
                /* 内置插件把空格式作为“按内容识别”标志；若沿用文件后缀，
                   错误后缀会把正确的内容锁死在错误解码器上。外部插件仍
                   按 Qt 的 testFormat 约定收到推导出的后缀。 */
                const XString* createFormat = decideFormatFromContent ? NULL :
                    (plugin == XImageBuiltinPlugin_instance() ? NULL : effectiveFormat);
                int64_t pos = XImagePluginRegistry_needsPositionRestore(plugin)
                    ? XImagePluginRegistry_savePos(device) : -1;
                handler = XImageIOPlugin_create_base(plugin, device, createFormat);
                XImagePluginRegistry_restorePos(device, pos);
                if (handler) {
                    XImagePluginRegistry_setupHandler(handler, device, createFormat);
                    goto done;
                }
            }
        }
    }
    handler = NULL;
done:
    if (normalizedFormat) XString_delete_base((XClass*)normalizedFormat);
    return handler;
}

XImageIOHandler* XImagePluginRegistry_createReadHandler(XIODevice* device,
                                                        const XString* format)
{
    return XImagePluginRegistry_createReadHandlerEx(device, format, true, false);
}

XImageIOHandler* XImagePluginRegistry_createReadHandlerContentFallback(
    XIODevice* device, const XString* rejectedFormat)
{
    int i;
    bool skippedSuffixPlugin = false;
    XString* normalizedFormat;
    const char* rejectedUtf8;
    XImageIOPlugin* builtin;
    if (!device) return NULL;
    XImagePluginRegistry_ensureBuiltin();
    normalizedFormat = XImagePluginRegistry_normalizedFormat(rejectedFormat);
    rejectedUtf8 = normalizedFormat
        ? XString_toUtf8(normalizedFormat) : NULL;
    builtin = XImageBuiltinPlugin_instance();
    for (i = 0; i < g_pluginCount; ++i) {
        XImageIOPlugin* plugin = g_plugins[i];
        uint32_t actual;
        const XString* createFormat;
        int64_t pos;
        XImageIOHandler* handler;
        /* Qt skips only the plugin selected by the suffix map.  The built-in
           plugin must remain eligible for the same suffix. */
        if (!skippedSuffixPlugin && plugin != builtin && rejectedUtf8 &&
            XImagePluginRegistry_pluginHasKey(plugin, rejectedUtf8)) {
            skippedSuffixPlugin = true;
            continue;
        }
        actual = XImagePluginRegistry_capabilitiesUtf8(plugin, device, NULL);
        if ((actual & (uint32_t)XImageIOPlugin_CanRead) == 0) continue;
        createFormat = plugin == builtin ? NULL : normalizedFormat;
        pos = XImagePluginRegistry_needsPositionRestore(plugin)
            ? XImagePluginRegistry_savePos(device) : -1;
        handler = XImageIOPlugin_create_base(plugin, device, createFormat);
        XImagePluginRegistry_restorePos(device, pos);
        if (handler) {
            XImagePluginRegistry_setupHandler(handler, device, createFormat);
            if (normalizedFormat) XString_delete_base((XClass*)normalizedFormat);
            return handler;
        }
    }
    if (normalizedFormat) XString_delete_base((XClass*)normalizedFormat);
    return NULL;
}

XImageIOHandler* XImagePluginRegistry_createWriteHandler(XIODevice* device,
                                                         const XString* format)
{
    int i;
    const char* formatUtf8;
    XString* normalizedFormat;
    const XString* effectiveFormat;
    if (!device || XImagePluginRegistry_formatEmpty(format)) return NULL;
    XImagePluginRegistry_ensureBuiltin();
    normalizedFormat = XImagePluginRegistry_normalizedFormat(format);
    effectiveFormat = normalizedFormat ? normalizedFormat : format;
    formatUtf8 = XImagePluginRegistry_formatUtf8(effectiveFormat);
    for (i = 0; i < g_pluginCount; ++i) {
        XImageIOPlugin* plugin = g_plugins[i];
        uint32_t wanted = (uint32_t)XImageIOPlugin_CanWrite;
        uint32_t actual = XImagePluginRegistry_capabilitiesUtf8(
            plugin, device, XString_toUtf8(effectiveFormat));
        if ((actual & wanted) != 0 &&
            XImagePluginRegistry_pluginHasKey(plugin, formatUtf8)) {
            int64_t pos = XImagePluginRegistry_needsPositionRestore(plugin)
                ? XImagePluginRegistry_savePos(device) : -1;
            XImageIOHandler* handler = XImageIOPlugin_create_base(plugin, device,
                                                                   effectiveFormat);
            XImagePluginRegistry_restorePos(device, pos);
            if (handler) {
                XImagePluginRegistry_setupHandler(handler, device, effectiveFormat);
                if (normalizedFormat)
                    XString_delete_base((XClass*)normalizedFormat);
                return handler;
            }
        }
    }
    if (normalizedFormat) XString_delete_base((XClass*)normalizedFormat);
    return NULL;
}

bool XImagePluginRegistry_supportsReadFormat(const XString* format)
{
    int i;
    const char* formatUtf8;
    if (XImagePluginRegistry_formatEmpty(format)) return false;
    XImagePluginRegistry_ensureBuiltin();
    formatUtf8 = XImagePluginRegistry_formatUtf8(format);
    for (i = 0; i < g_pluginCount; ++i) {
        if (XImagePluginRegistry_pluginSupports(g_plugins[i], true, NULL, formatUtf8))
            return true;
    }
    return false;
}

bool XImagePluginRegistry_supportsWriteFormat(const XString* format)
{
    int i;
    const char* formatUtf8;
    if (XImagePluginRegistry_formatEmpty(format)) return false;
    XImagePluginRegistry_ensureBuiltin();
    formatUtf8 = XImagePluginRegistry_formatUtf8(format);
    for (i = 0; i < g_pluginCount; ++i) {
        if (XImagePluginRegistry_pluginSupports(g_plugins[i], false, NULL, formatUtf8))
            return true;
    }
    return false;
}

XString* XImagePluginRegistry_detectReadFormat(XIODevice* device)
{
    int i;
    if (!device) return XString_create();
    XImagePluginRegistry_ensureBuiltin();
    for (i = 0; i < g_pluginCount; ++i) {
        XImageIOPlugin* plugin = g_plugins[i];
        XStringList* keys = XImageIOPlugin_keys_base(plugin);
        int64_t count = keys ? XStringList_size_base((XContainer*)keys) : 0;
        int64_t j;
        for (j = 0; j < count; ++j) {
            XString* key = (XString*)XStringList_at_base((XVector*)keys, j);
            const char* keyUtf8 = XString_toUtf8(key);
            if (keyUtf8 && XImagePluginRegistry_pluginSupports(plugin, true,
                                                                device, keyUtf8))
                return XString_create_copy(key);
        }
        /* Some plugins implement content probing only for an empty format.
         * Their first metadata key is the only portable format name exposed
         * by this C facade in that case. */
        if (count > 0 && XImagePluginRegistry_pluginSupports(plugin, true,
                                                              device, NULL)) {
            XString* key = (XString*)XStringList_at_base((XVector*)keys, 0);
            return XString_create_copy(key);
        }
    }
    return XString_create();
}

XStringList* XImagePluginRegistry_supportedImageFormats(bool readOnly)
{
    XStringList* result = XImagePluginRegistry_emptyList();
    int i;
    if (!result) return NULL;
    XImagePluginRegistry_ensureBuiltin();
    for (i = 0; i < g_pluginCount; ++i) {
        XImageIOPlugin* plugin = g_plugins[i];
        XStringList* keys = XImageIOPlugin_keys_base(plugin);
        int64_t count;
        int64_t j;
        if (!keys) continue;
        count = XStringList_size_base((XContainer*)keys);
        for (j = 0; j < count; ++j) {
            XString* keyObject = (XString*)XStringList_at_base((XVector*)keys, j);
            const char* key = XString_toUtf8(keyObject);
            if (key && XImagePluginRegistry_pluginSupports(plugin, readOnly, NULL, key))
                XImagePluginRegistry_appendUniqueUtf8(result, key);
        }
    }
    XStringList_sort(result, XChar_CaseSensitive);
    XStringList_removeDuplicates(result);
    return result;
}

XStringList* XImagePluginRegistry_supportedMimeTypes(bool readOnly)
{
    XStringList* result = XImagePluginRegistry_emptyList();
    int i;
    if (!result) return NULL;
    XImagePluginRegistry_ensureBuiltin();
    for (i = 0; i < g_pluginCount; ++i) {
        XImageIOPlugin* plugin = g_plugins[i];
        XStringList* keys = XImageIOPlugin_keys_base(plugin);
        XStringList* mimes = XImageIOPlugin_mimeTypes_base(plugin);
        int64_t keyCount = keys ? XStringList_size_base((XContainer*)keys) : 0;
        int64_t mimeCount = mimes ? XStringList_size_base((XContainer*)mimes) : 0;
        int64_t j;
        int64_t limit = keyCount < mimeCount ? keyCount : mimeCount;
        for (j = 0; j < keyCount; ++j) {
            XString* keyObject = (XString*)XStringList_at_base((XVector*)keys, j);
            const char* key = XString_toUtf8(keyObject);
            const char* mimeUtf8 = NULL;
            if (!key || !XImagePluginRegistry_pluginSupports(plugin, readOnly, NULL, key))
                continue;
            if (j < limit)
                mimeUtf8 = XString_toUtf8((XString*)XStringList_at_base((XVector*)mimes, j));
            if (!mimeUtf8 || !mimeUtf8[0])
                mimeUtf8 = XImagePluginRegistry_fallbackMime(key);
            if (mimeUtf8)
                XImagePluginRegistry_appendUniqueUtf8(result, mimeUtf8);
        }
    }
    XStringList_sort(result, XChar_CaseSensitive);
    XStringList_removeDuplicates(result);
    return result;
}

XStringList* XImagePluginRegistry_imageFormatsForMimeType(const XString* mimeType,
                                                          bool readOnly)
{
    XStringList* result = XImagePluginRegistry_emptyList();
    const char* wanted;
    int i;
    if (!result) return NULL;
    XImagePluginRegistry_ensureBuiltin();
    wanted = XImagePluginRegistry_formatUtf8(mimeType);
    if (!wanted) return result;
    for (i = 0; i < g_pluginCount; ++i) {
        XImageIOPlugin* plugin = g_plugins[i];
        XStringList* keys = XImageIOPlugin_keys_base(plugin);
        XStringList* mimes = XImageIOPlugin_mimeTypes_base(plugin);
        int64_t keyCount = keys ? XStringList_size_base((XContainer*)keys) : 0;
        int64_t mimeCount = mimes ? XStringList_size_base((XContainer*)mimes) : 0;
        int64_t limit = keyCount < mimeCount ? keyCount : mimeCount;
        int64_t j;
        for (j = 0; j < keyCount; ++j) {
            XString* keyObject = (XString*)XStringList_at_base((XVector*)keys, j);
            const char* key = XString_toUtf8(keyObject);
            const char* mimeUtf8 = NULL;
            if (j < limit)
                mimeUtf8 = XString_toUtf8((XString*)XStringList_at_base((XVector*)mimes, j));
            if (!mimeUtf8 || !mimeUtf8[0])
                mimeUtf8 = XImagePluginRegistry_fallbackMime(key);
            if (mimeUtf8 && XImagePluginRegistry_mimeEquals(wanted, mimeUtf8) &&
                XImagePluginRegistry_pluginSupports(plugin, readOnly, NULL, key))
                XImagePluginRegistry_appendUniqueUtf8(result, key);
        }
    }
    XStringList_sort(result, XChar_CaseSensitive);
    XStringList_removeDuplicates(result);
    return result;
}

XStringList* XImagePluginRegistry_imageFormatsForMimeType_2(const char* mimeType,
                                                            bool readOnly)
{
    XString* value = mimeType ? XString_create_utf8(mimeType) : NULL;
    XStringList* result = XImagePluginRegistry_imageFormatsForMimeType(value, readOnly);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

#else

void XImagePluginRegistry_clear(void) {}
bool XImagePluginRegistry_addPlugin(XImageIOPlugin* plugin) { (void)plugin; return false; }
bool XImagePluginRegistry_removePlugin(XImageIOPlugin* plugin) { (void)plugin; return false; }
int XImagePluginRegistry_pluginCount(void) { return 0; }
XImageIOPlugin* XImagePluginRegistry_pluginAt(int index) { (void)index; return NULL; }
XImageIOHandler* XImagePluginRegistry_createReadHandler(XIODevice* device, const XString* format)
{ (void)device; (void)format; return NULL; }
XImageIOHandler* XImagePluginRegistry_createReadHandlerContentFallback(
    XIODevice* device, const XString* rejectedFormat)
{ (void)device; (void)rejectedFormat; return NULL; }
XImageIOHandler* XImagePluginRegistry_createReadHandlerEx(
    XIODevice* device, const XString* format,
    bool autoDetectImageFormat, bool decideFormatFromContent)
{ (void)device; (void)format; (void)autoDetectImageFormat; (void)decideFormatFromContent; return NULL; }
XImageIOHandler* XImagePluginRegistry_createWriteHandler(XIODevice* device, const XString* format)
{ (void)device; (void)format; return NULL; }
bool XImagePluginRegistry_supportsReadFormat(const XString* format) { (void)format; return false; }
bool XImagePluginRegistry_supportsWriteFormat(const XString* format) { (void)format; return false; }
XString* XImagePluginRegistry_detectReadFormat(XIODevice* device)
{ (void)device; return XString_create(); }
XStringList* XImagePluginRegistry_supportedImageFormats(bool readOnly) { (void)readOnly; return XStringList_create(); }
XStringList* XImagePluginRegistry_supportedMimeTypes(bool readOnly) { (void)readOnly; return XStringList_create(); }
XStringList* XImagePluginRegistry_imageFormatsForMimeType(const XString* mimeType, bool readOnly)
{ (void)mimeType; (void)readOnly; return XStringList_create(); }
XStringList* XImagePluginRegistry_imageFormatsForMimeType_2(const char* mimeType, bool readOnly)
{ (void)mimeType; (void)readOnly; return XStringList_create(); }

#endif /* XIMAGEIOPLUGIN_ON */
