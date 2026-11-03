/**
 * @file XSettingsNative_unsupported.c
 * @brief 未提供注册表后端的平台存根。
 * @details
 * 注册表后端在 Qt 中也只存在于 Windows（qsettings_win.cpp 由 Q_OS_WIN 守卫）；
 * 定位是「Windows 上 NativeFormat/Registry32Format/Registry64Format 走注册表」。
 * 非 Windows 平台的 NativeFormat 在 Qt 里本就是 INI 文件，因此这些平台不会
 * 走到本契约——存根只为保证 XSettings 的公共分发代码在所有平台可链接。
 *
 * 所有函数统一返回失败/空结果；公共层在非 Windows 平台不会调用它们（格式
 * 分派在 XSettings.c 里按平台宏决定后端种类），即便被误调也只会表现为
 * AccessError，不会崩溃。
 */

#include "XSettingsNative.h"

#if XSETTINGS_ON && !defined(_WIN32)

#include "XMemory.h"

bool XSettingsNative_openKey(XSettingsNativeRoot root, XSettingsNativeView view,
                             const uint16_t* subKey, bool write, bool createIfMissing,
                             XSettingsNativeKey* outKey, bool* outReadOnly)
{
    (void)root;
    (void)view;
    (void)subKey;
    (void)write;
    (void)createIfMissing;
    if (outKey)
        *outKey = NULL;
    if (outReadOnly)
        *outReadOnly = false;
    return false;
}

void XSettingsNative_closeKey(XSettingsNativeKey key)
{
    (void)key;
}

bool XSettingsNative_isWritable(XSettingsNativeKey key)
{
    (void)key;
    return false;
}

const char* XSettingsNative_rootName(XSettingsNativeRoot root)
{
    (void)root;
    return NULL;
}

bool XSettingsNative_readValue(XSettingsNativeKey key, const uint16_t* name,
                               XSettingsNativeValue* out)
{
    (void)key;
    (void)name;
    if (out) {
        XMemset(out, 0, sizeof(*out));
        out->kind = XSettingsNativeValueKind_Unsupported;
    }
    return false;
}

void XSettingsNative_valueFree(XSettingsNativeValue* value)
{
    if (!value)
        return;
    XMemset(value, 0, sizeof(*value));
}

bool XSettingsNative_writeString(XSettingsNativeKey key, const uint16_t* name,
                                 const uint16_t* text)
{
    (void)key;
    (void)name;
    (void)text;
    return false;
}

bool XSettingsNative_writeStringList(XSettingsNativeKey key, const uint16_t* name,
                                     const uint16_t* multiSz, size_t units)
{
    (void)key;
    (void)name;
    (void)multiSz;
    (void)units;
    return false;
}

bool XSettingsNative_writeBinary(XSettingsNativeKey key, const uint16_t* name,
                                 const uint8_t* data, size_t size)
{
    (void)key;
    (void)name;
    (void)data;
    (void)size;
    return false;
}

bool XSettingsNative_writeInt32(XSettingsNativeKey key, const uint16_t* name, int32_t value)
{
    (void)key;
    (void)name;
    (void)value;
    return false;
}

bool XSettingsNative_writeInt64(XSettingsNativeKey key, const uint16_t* name, int64_t value)
{
    (void)key;
    (void)name;
    (void)value;
    return false;
}

bool XSettingsNative_deleteValue(XSettingsNativeKey key, const uint16_t* name)
{
    (void)key;
    (void)name;
    return false;
}

bool XSettingsNative_deleteSubKeyTree(XSettingsNativeKey parent, const uint16_t* subKey)
{
    (void)parent;
    (void)subKey;
    return false;
}

bool XSettingsNative_enumChildren(XSettingsNativeKey key, bool groups,
                                  XSettingsNativeEnumFunc func, void* userData)
{
    (void)key;
    (void)groups;
    (void)func;
    (void)userData;
    return false;
}

bool XSettingsNative_collectAllKeys(XSettingsNativeRoot root, XSettingsNativeView view,
                                    const uint16_t* subKey, const uint16_t* basePath,
                                    XSettingsNativePathEnumFunc func, void* userData)
{
    (void)root;
    (void)view;
    (void)subKey;
    (void)basePath;
    (void)func;
    (void)userData;
    return false;
}

void XSettingsNative_flushKey(XSettingsNativeKey key)
{
    (void)key;
}

#endif /* XSETTINGS_ON && !defined(_WIN32) */
