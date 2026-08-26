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
    size_t i = 0;
    if (!mimeType || !expected) return false;
    for (;;) {
        unsigned char a;
        unsigned char b;
        if (!expected[i] || !mimeType[i])
            break;
        a = (unsigned char)mimeType[i];
        b = (unsigned char)expected[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return false;
        ++i;
    }
    return expected[i] == '\0' && mimeType[i] == '\0';
}

static bool XImagePluginRegistry_formatEmpty(const XString* format)
{
    return !format || XContainer_isEmpty_base((const XContainer*)format);
}

static const char* XImagePluginRegistry_formatUtf8(const XString* format)
{
    return XString_toUtf8(format);
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
    uint32_t cap = XImageIOPlugin_capabilities_base(plugin, device, value);
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
    if (XStringList_contains_utf8(list, value, XChar_CaseInsensitive)) return false;
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
    if (!plugin) return false;
    for (i = 0; i < g_pluginCount; ++i)
        if (g_plugins[i] == plugin) return false;
    if (g_pluginCount >= XIMAGEPLUGINREGISTRY_CAPACITY) return false;
    g_plugins[g_pluginCount++] = plugin;
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

XImageIOHandler* XImagePluginRegistry_createReadHandler(XIODevice* device,
                                                        const XString* format)
{
    int i;
    XImageIOHandler* handler;
    if (!device) return NULL;
    XImagePluginRegistry_ensureBuiltin();
    if (format && !XContainer_isEmpty_base((const XContainer*)format)) {
        for (i = 0; i < g_pluginCount; ++i) {
            XImageIOPlugin* plugin = g_plugins[i];
            uint32_t wanted = (uint32_t)XImageIOPlugin_CanRead;
            uint32_t actual = XImageIOPlugin_capabilities_base(plugin, device, format);
            if ((actual & wanted) != 0 &&
                XImagePluginRegistry_pluginHasKey(plugin, XString_toUtf8(format))) {
                handler = XImageIOPlugin_create_base(plugin, device, format);
                if (handler) {
                    XImagePluginRegistry_setupHandler(handler, device, format);
                    return handler;
                }
            }
        }
    }
    for (i = 0; i < g_pluginCount; ++i) {
        XImageIOPlugin* plugin = g_plugins[i];
        uint32_t wanted = (uint32_t)XImageIOPlugin_CanRead;
        uint32_t actual = XImageIOPlugin_capabilities_base(plugin, device, NULL);
        if ((actual & wanted) != 0) {
            handler = XImageIOPlugin_create_base(plugin, device, format);
            if (handler) {
                XImagePluginRegistry_setupHandler(handler, device, format);
                return handler;
            }
        }
    }
    return NULL;
}

XImageIOHandler* XImagePluginRegistry_createWriteHandler(XIODevice* device,
                                                         const XString* format)
{
    int i;
    const char* formatUtf8;
    if (!device || XImagePluginRegistry_formatEmpty(format)) return NULL;
    XImagePluginRegistry_ensureBuiltin();
    formatUtf8 = XImagePluginRegistry_formatUtf8(format);
    for (i = 0; i < g_pluginCount; ++i) {
        XImageIOPlugin* plugin = g_plugins[i];
        uint32_t wanted = (uint32_t)XImageIOPlugin_CanWrite;
        uint32_t actual = XImageIOPlugin_capabilities_base(plugin, device, format);
        if ((actual & wanted) != 0 &&
            XImagePluginRegistry_pluginHasKey(plugin, formatUtf8)) {
            XImageIOHandler* handler = XImageIOPlugin_create_base(plugin, device, format);
            if (handler) {
                XImagePluginRegistry_setupHandler(handler, device, format);
                return handler;
            }
        }
    }
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
XImageIOHandler* XImagePluginRegistry_createWriteHandler(XIODevice* device, const XString* format)
{ (void)device; (void)format; return NULL; }
bool XImagePluginRegistry_supportsReadFormat(const XString* format) { (void)format; return false; }
bool XImagePluginRegistry_supportsWriteFormat(const XString* format) { (void)format; return false; }
XStringList* XImagePluginRegistry_supportedImageFormats(bool readOnly) { (void)readOnly; return XStringList_create(); }
XStringList* XImagePluginRegistry_supportedMimeTypes(bool readOnly) { (void)readOnly; return XStringList_create(); }
XStringList* XImagePluginRegistry_imageFormatsForMimeType(const XString* mimeType, bool readOnly)
{ (void)mimeType; (void)readOnly; return XStringList_create(); }
XStringList* XImagePluginRegistry_imageFormatsForMimeType_2(const char* mimeType, bool readOnly)
{ (void)mimeType; (void)readOnly; return XStringList_create(); }

#endif /* XIMAGEIOPLUGIN_ON */
