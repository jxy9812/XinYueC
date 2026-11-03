/**
 * @file XSettingsNative_win32.c
 * @brief XSettings 原生设置存储的 Windows 注册表后端。
 * @details
 * 本文件实现 Src/XCore/XPlatform/XSettingsNative.h 声明的全部注册表原语，
 * 逐条对标 Qt 6.8.3 qtbase/src/corelib/io/qsettings_win.cpp 的 Win32 封装层：
 * openKey/createOrOpenKey、readKey 的五类 REG_* 分派、deleteChildGroups 的
 * 递归删键、childKeysOrGroups 的枚举、RegFlushKey 刷盘。平台头（windows.h）
 * 只在本文件出现，公共层不感知 Win32。
 *
 * 与 Qt 的差异（均为本项目的类型/内存口径，不改变可观察语义）：
 * - 键名/值名统一用 uint16_t 承载 UTF-16，与 Qt 的 QString UTF-16 视图同构；
 *   内部按 wchar_t 指针传给 Win32（Windows 上 wchar_t 为 16 位）。
 * - 读取结果按 XSettingsNativeValue 返回，由调用方 valueFree 释放。
 * - 错误不打印（Qt 用 qErrnoWarning），由公共层统一置 AccessError。
 *
 * REG_BINARY 的承载口径（与 Qt 一致）：Qt 在 variantToString 结果含 NUL 时
 * 写 REG_BINARY，缓冲内容是该 QString 的 UTF-16 字节流；读取时 Qt 用
 * fromWCharArray(data, data.size()/2) 还原成同一个 QString。故本后端对
 * REG_BINARY/REG_NONE 同样交出 UTF-16 文本视图（text + textUnits），而非
 * 交给调用方自行解释的裸字节。
 * @note 仅 Windows 构建编译；其它平台由 XSettingsNative_unsupported.c 提供存根。
 */

#include "XSettingsNative.h"

#if XSETTINGS_ON && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "XMemory.h"



/* Qt 在缺少现代 SDK 头时的兼容兜底（qsettings_win.cpp 同款）。 */
#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif
#ifndef KEY_WOW64_32KEY
#define KEY_WOW64_32KEY 0x0200
#endif

/** @brief 读写权限（对标 qsettings_win.cpp registryPermissions，刻意不含 KEY_ALL_ACCESS）。 */
#define XSETTINGS_REG_PERMS (KEY_READ | KEY_WRITE)

/** @brief 名字缓冲上界（防无 NUL 的坏输入导致越界扫描）。 */
#define XSETTINGS_NAME_LIMIT 0x100000u

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

/** @brief 根键枚举 → HKEY（非法枚举返回 NULL）。 */
static HKEY xsnRootHandle(XSettingsNativeRoot root)
{
    switch (root) {
    case XSettingsNativeRoot_CurrentUser:  return HKEY_CURRENT_USER;
    case XSettingsNativeRoot_LocalMachine: return HKEY_LOCAL_MACHINE;
    case XSettingsNativeRoot_ClassesRoot:  return HKEY_CLASSES_ROOT;
    case XSettingsNativeRoot_Users:        return HKEY_USERS;
    default:                               return NULL;
    }
}

/** @brief WOW64 视图 → REGSAM 附加标志（对标 Qt 的 access 参数）。 */
static REGSAM xsnViewAccess(XSettingsNativeView view)
{
    switch (view) {
    case XSettingsNativeView_32: return KEY_WOW64_32KEY;
    case XSettingsNativeView_64: return KEY_WOW64_64KEY;
    default:                     return 0;
    }
}

/** @brief 借用转换：uint16_t* → wchar_t*（Windows 上同宽；空指针原样传递）。 */
static const wchar_t* xsnWide(const uint16_t* text)
{
    return (const wchar_t*)text;
}

/** @brief 有界求 UTF-16 序列的有效码元数（不含结尾 NUL）。 */
static size_t xsnLength(const uint16_t* text)
{
    size_t n = 0;
    if (!text)
        return 0;
    while (text[n] != 0 && n < XSETTINGS_NAME_LIMIT)
        ++n;
    return n;
}

/** @brief 复制 UTF-16 序列到新分配缓冲（补 NUL 结尾）；units 为 0 返回 NULL。 */
static uint16_t* xsnDuplicate(const uint16_t* text, size_t units)
{
    uint16_t* copy;
    if (!text || units == 0)
        return NULL;
    copy = (uint16_t*)XMalloc_System((units + 1) * sizeof(uint16_t));
    if (!copy)
        return NULL;
    XMemcpy(copy, text, units * sizeof(uint16_t));
    copy[units] = 0;
    return copy;
}

/* ==========================================================================
 * 键生命周期
 * ========================================================================== */

/** @brief 打开键（openKey 语义，不创建）；键不存在或权限不足返回 NULL。 */
static HKEY xsnOpen(HKEY parent, REGSAM perms, const uint16_t* subKey, REGSAM access)
{
    HKEY handle = NULL;
    if (!parent)
        return NULL;
    if (RegOpenKeyExW(parent, xsnWide(subKey), 0, perms | access, &handle) != ERROR_SUCCESS)
        return NULL;
    return handle;
}

/** @brief 打开或创建键（createOrOpenKey(perms) 语义）。 */
static HKEY xsnOpenOrCreate(HKEY parent, REGSAM perms, const uint16_t* subKey, REGSAM access)
{
    HKEY handle = xsnOpen(parent, perms, subKey, access);
    if (handle)
        return handle;
    if (!parent)
        return NULL;
    if (RegCreateKeyExW(parent, xsnWide(subKey), 0, NULL, REG_OPTION_NON_VOLATILE,
                        perms | access, NULL, &handle, NULL) != ERROR_SUCCESS)
        return NULL;
    return handle;
}

bool XSettingsNative_openKey(XSettingsNativeRoot root, XSettingsNativeView view,
                             const uint16_t* subKey, bool write, bool createIfMissing,
                             XSettingsNativeKey* outKey, bool* outReadOnly)
{
    HKEY parent = xsnRootHandle(root);
    REGSAM access = xsnViewAccess(view);
    HKEY handle = NULL;
    bool readOnly = false;

    if (!outKey)
        return false;
    *outKey = NULL;
    if (outReadOnly)
        *outReadOnly = false;
    if (!parent)
        return false;

    if (write) {
        /* 先要读写权限（必要时创建），拿不到则回落只读
         * （对标 createOrOpenKey(…, &readOnly)） */
        handle = xsnOpenOrCreate(parent, XSETTINGS_REG_PERMS, subKey, access);
        if (!handle) {
            handle = xsnOpenOrCreate(parent, KEY_READ, subKey, access);
            readOnly = true;
        }
    }
    else if (createIfMissing) {
        handle = xsnOpenOrCreate(parent, KEY_READ, subKey, access);
    }
    else {
        handle = xsnOpen(parent, KEY_READ, subKey, access);
    }

    if (!handle)
        return false;

    *outKey = (XSettingsNativeKey)handle;
    if (outReadOnly)
        *outReadOnly = readOnly;
    return true;
}

void XSettingsNative_closeKey(XSettingsNativeKey key)
{
    if (key)
        RegCloseKey((HKEY)key);
}

bool XSettingsNative_isWritable(XSettingsNativeKey key)
{
    return key != NULL;
}

const char* XSettingsNative_rootName(XSettingsNativeRoot root)
{
    switch (root) {
    case XSettingsNativeRoot_CurrentUser:  return "HKEY_CURRENT_USER";
    case XSettingsNativeRoot_LocalMachine: return "HKEY_LOCAL_MACHINE";
    case XSettingsNativeRoot_ClassesRoot:  return "HKEY_CLASSES_ROOT";
    case XSettingsNativeRoot_Users:        return "HKEY_USERS";
    default:                               return NULL;
    }
}

/* ==========================================================================
 * 读取
 * ========================================================================== */

/**
 * @brief 读单个值（对标 qsettings_win.cpp readKey 的类型分派）。
 * @details REG_SZ/REG_EXPAND_SZ/REG_MULTI_SZ/REG_BINARY/REG_NONE 均交出 UTF-16
 *          文本视图；REG_DWORD/REG_QWORD 交出标量。
 */
static bool xsnReadKeyValue(HKEY handle, const uint16_t* name, XSettingsNativeValue* out)
{
    DWORD dataType = 0;
    DWORD dataSize = 0;
    const wchar_t* valueName = xsnWide(name);
    uint8_t* buffer;
    size_t capacity;
    DWORD actual;

    if (RegQueryValueExW(handle, valueName, NULL, &dataType, NULL, &dataSize) != ERROR_SUCCESS)
        return false;

    /* 罕见情况下数据缺少结尾 NUL：按 Qt 口径预留（readKey 同款） */
    if (dataType == REG_SZ || dataType == REG_EXPAND_SZ)
        dataSize += 2;
    else if (dataType == REG_MULTI_SZ)
        dataSize += 4;

    capacity = (size_t)dataSize + 2;
    buffer = (uint8_t*)XMalloc_System(capacity);
    if (!buffer)
        return false;
    XMemset(buffer, 0, capacity);

    actual = dataSize;
    if (RegQueryValueExW(handle, valueName, NULL, NULL, (LPBYTE)buffer, &actual) != ERROR_SUCCESS) {
        XFree_System(buffer);
        return false;
    }

    switch (dataType) {
    case REG_EXPAND_SZ:
    case REG_SZ: {
        size_t units = (size_t)actual / sizeof(wchar_t);
        while (units > 0 && ((const uint16_t*)buffer)[units - 1] == 0)
            --units;
        out->kind = XSettingsNativeValueKind_String;
        out->text = xsnDuplicate((const uint16_t*)buffer, units);
        out->textUnits = out->text ? units : 0;
        XFree_System(buffer);
        return true;
    }

    case REG_MULTI_SZ: {
        size_t units = (size_t)actual / sizeof(wchar_t);
        /* 保留 REG_MULTI_SZ 的双 NUL 结束布局，只裁掉多余尾部零 */
        while (units > 2 && ((const uint16_t*)buffer)[units - 1] == 0 &&
               ((const uint16_t*)buffer)[units - 2] == 0)
            --units;
        out->kind = XSettingsNativeValueKind_StringList;
        out->text = xsnDuplicate((const uint16_t*)buffer, units);
        out->textUnits = out->text ? units : 0;
        XFree_System(buffer);
        return true;
    }

    case REG_NONE:
    case REG_BINARY: {
        /* Qt 口径：REG_BINARY 承载的是 variantToString 的 UTF-16 字节流，
         * 读取时按 data.size()/2 个码元还原成文本 */
        size_t units = (size_t)actual / sizeof(wchar_t);
        while (units > 0 && ((const uint16_t*)buffer)[units - 1] == 0)
            --units;
        out->kind = XSettingsNativeValueKind_Binary;
        out->text = xsnDuplicate((const uint16_t*)buffer, units);
        out->textUnits = out->text ? units : 0;
        XFree_System(buffer);
        return true;
    }

    case REG_DWORD: {
        int32_t value = 0;
        if (actual >= sizeof(int32_t))
            XMemcpy(&value, buffer, sizeof(int32_t));
        out->kind = XSettingsNativeValueKind_Int32;
        out->int32Value = value;
        XFree_System(buffer);
        return true;
    }

    case REG_DWORD_BIG_ENDIAN: {
        /* 大端存储：按字节序重组（Qt 未单独处理该分支，此处按语义补齐） */
        int32_t value = 0;
        if (actual >= sizeof(int32_t)) {
            const uint8_t* raw = buffer;
            value = (int32_t)(((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
                              ((uint32_t)raw[2] << 8) | (uint32_t)raw[3]);
        }
        out->kind = XSettingsNativeValueKind_Int32;
        out->int32Value = value;
        XFree_System(buffer);
        return true;
    }

    case REG_QWORD: {
        int64_t value = 0;
        if (actual >= sizeof(int64_t))
            XMemcpy(&value, buffer, sizeof(int64_t));
        out->kind = XSettingsNativeValueKind_Int64;
        out->int64Value = value;
        XFree_System(buffer);
        return true;
    }

    default:
        /* 未知类型：对标 Qt 的 qWarning + nullopt */
        out->kind = XSettingsNativeValueKind_Unsupported;
        XFree_System(buffer);
        return false;
    }
}

bool XSettingsNative_readValue(XSettingsNativeKey key, const uint16_t* name,
                               XSettingsNativeValue* out)
{
    if (!out)
        return false;
    XMemset(out, 0, sizeof(*out));
    out->kind = XSettingsNativeValueKind_None;
    if (!key)
        return false;
    return xsnReadKeyValue((HKEY)key, name, out);
}

void XSettingsNative_valueFree(XSettingsNativeValue* value)
{
    if (!value)
        return;
    if (value->text) {
        XFree_System(value->text);
        value->text = NULL;
    }
    if (value->bytes) {
        XFree_System(value->bytes);
        value->bytes = NULL;
    }
    value->textUnits = 0;
    value->byteSize = 0;
    value->kind = XSettingsNativeValueKind_None;
}

/* ==========================================================================
 * 写入
 * ========================================================================== */

bool XSettingsNative_writeString(XSettingsNativeKey key, const uint16_t* name,
                                 const uint16_t* text)
{
    static const wchar_t empty[] = L"";
    size_t units;
    DWORD size;
    if (!key)
        return false;
    units = xsnLength(text);
    /* REG_SZ 必须含结尾 NUL（Qt set 分支：type == REG_SZ 时 length 加一） */
    size = (DWORD)((units + 1) * sizeof(wchar_t));
    return RegSetValueExW((HKEY)key, xsnWide(name), 0, REG_SZ,
                          (const BYTE*)(text ? xsnWide(text) : empty),
                          size) == ERROR_SUCCESS;
}

bool XSettingsNative_writeStringList(XSettingsNativeKey key, const uint16_t* name,
                                     const uint16_t* multiSz, size_t units)
{
    static const uint16_t emptyList[] = { 0, 0 };
    const uint16_t* payload = multiSz ? multiSz : emptyList;
    size_t payloadUnits = multiSz ? units : 2;
    if (!key)
        return false;
    /* units 是完整 REG_MULTI_SZ 载荷码元数（含结尾双 NUL），调用方负责算好 */
    if (payloadUnits < 2)
        payloadUnits = 2;
    return RegSetValueExW((HKEY)key, xsnWide(name), 0, REG_MULTI_SZ,
                          (const BYTE*)payload,
                          (DWORD)(payloadUnits * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool XSettingsNative_writeBinary(XSettingsNativeKey key, const uint16_t* name,
                                 const uint8_t* data, size_t size)
{
    if (!key)
        return false;
    return RegSetValueExW((HKEY)key, xsnWide(name), 0, REG_BINARY, data,
                          (DWORD)size) == ERROR_SUCCESS;
}

bool XSettingsNative_writeInt32(XSettingsNativeKey key, const uint16_t* name, int32_t value)
{
    if (!key)
        return false;
    return RegSetValueExW((HKEY)key, xsnWide(name), 0, REG_DWORD,
                          (const BYTE*)&value, sizeof(value)) == ERROR_SUCCESS;
}

bool XSettingsNative_writeInt64(XSettingsNativeKey key, const uint16_t* name, int64_t value)
{
    if (!key)
        return false;
    return RegSetValueExW((HKEY)key, xsnWide(name), 0, REG_QWORD,
                          (const BYTE*)&value, sizeof(value)) == ERROR_SUCCESS;
}

/* ==========================================================================
 * 删除与枚举
 * ========================================================================== */

bool XSettingsNative_deleteValue(XSettingsNativeKey key, const uint16_t* name)
{
    if (!key)
        return false;
    return RegDeleteValueW((HKEY)key, xsnWide(name)) == ERROR_SUCCESS;
}

/** @brief 递归删除 parent 下的全部子键（对标 deleteChildGroups）。 */
static void xsnDeleteChildGroups(HKEY parent, REGSAM access)
{
    DWORD index = 0;
    for (;;) {
        wchar_t nameBuffer[512];
        DWORD nameLength = (DWORD)(sizeof(nameBuffer) / sizeof(wchar_t));
        HKEY child;
        LONG result = RegEnumKeyExW(parent, index, nameBuffer, &nameLength,
                                    NULL, NULL, NULL, NULL);
        if (result != ERROR_SUCCESS)
            return;   /* ERROR_NO_MORE_ITEMS 或其它错误：结束 */
        child = xsnOpen(parent, XSETTINGS_REG_PERMS, (const uint16_t*)nameBuffer, access);
        if (!child) {
            ++index;
            continue;
        }
        xsnDeleteChildGroups(child, access);
        RegCloseKey(child);
        if (RegDeleteKeyW(parent, nameBuffer) != ERROR_SUCCESS)
            return;
        /* 删掉后索引不前移（后续项顶替当前位） */
    }
}

bool XSettingsNative_deleteSubKeyTree(XSettingsNativeKey parent, const uint16_t* subKey)
{
    HKEY parentKey = (HKEY)parent;

    if (!parentKey)
        return false;

    if (xsnLength(subKey) == 0) {
        /* 空路径 = 清空 parent 自身的直接子键与直接值（Qt remove("") 分支） */
        DWORD index = 0;
        xsnDeleteChildGroups(parentKey, 0);
        for (;;) {
            wchar_t nameBuffer[512];
            DWORD nameLength = (DWORD)(sizeof(nameBuffer) / sizeof(wchar_t));
            LONG result;
            /* 预清零：默认值（空名字）时 RegEnumValueW 不回写缓冲，
             * 未清零会拿垃圾指针去删值 */
            XMemset(nameBuffer, 0, sizeof(nameBuffer));
            result = RegEnumValueW(parentKey, index, nameBuffer, &nameLength,
                                   NULL, NULL, NULL, NULL);
            if (result != ERROR_SUCCESS)
                break;
            if (RegDeleteValueW(parentKey, nameBuffer) != ERROR_SUCCESS)
                ++index;   /* 删失败则跳过，避免死循环 */
        }
        return true;
    }

    {
        HKEY target = xsnOpen(parentKey, XSETTINGS_REG_PERMS, subKey, 0);
        if (target) {
            xsnDeleteChildGroups(target, 0);
            RegCloseKey(target);
        }
        /* 删掉键本身（此时应已无子键） */
        return RegDeleteKeyW(parentKey, xsnWide(subKey)) == ERROR_SUCCESS;
    }
}

bool XSettingsNative_enumChildren(XSettingsNativeKey key, bool groups,
                                  XSettingsNativeEnumFunc func, void* userData)
{
    HKEY handle = (HKEY)key;
    DWORD numSubKeys = 0;
    DWORD maxSubKeyLen = 0;
    DWORD numValues = 0;
    DWORD maxValueNameLen = 0;
    DWORD index;

    if (!handle || !func)
        return false;

    if (RegQueryInfoKeyW(handle, NULL, NULL, NULL, &numSubKeys, &maxSubKeyLen, NULL,
                         &numValues, &maxValueNameLen, NULL, NULL, NULL) != ERROR_SUCCESS)
        return false;

    if (groups) {
        DWORD capacity = maxSubKeyLen + 2;
        wchar_t* buffer = (wchar_t*)XMalloc_System((size_t)capacity * sizeof(wchar_t));
        if (!buffer)
            return false;
        for (index = 0; index < numSubKeys; ++index) {
            DWORD length = maxSubKeyLen + 1;
            if (RegEnumKeyExW(handle, index, buffer, &length, NULL, NULL, NULL, NULL)
                != ERROR_SUCCESS)
                continue;
            if (!func(userData, (const uint16_t*)buffer)) {
                XFree_System(buffer);
                return true;   /* 回调提前终止：不算失败 */
            }
        }
        XFree_System(buffer);
    }
    else {
        DWORD capacity = maxValueNameLen + 2;
        wchar_t* buffer = (wchar_t*)XMalloc_System((size_t)capacity * sizeof(wchar_t));
        if (!buffer)
            return false;
        for (index = 0; index < numValues; ++index) {
            DWORD length = maxValueNameLen + 1;
            if (RegEnumValueW(handle, index, buffer, &length, NULL, NULL, NULL, NULL)
                != ERROR_SUCCESS)
                continue;
            if (length == 0) {
                /* 空值名（默认值）按 Qt 口径回报 "."（childKeysOrGroups 同款） */
                static const uint16_t dot[] = { (uint16_t)'.', 0 };
                if (!func(userData, dot)) {
                    XFree_System(buffer);
                    return true;
                }
                continue;
            }
            if (!func(userData, (const uint16_t*)buffer)) {
                XFree_System(buffer);
                return true;
            }
        }
        XFree_System(buffer);
    }
    return true;
}

/* ==========================================================================
 * 刷盘
 * ========================================================================== */

void XSettingsNative_flushKey(XSettingsNativeKey key)
{
    if (key)
        RegFlushKey((HKEY)key);
}

/* ==========================================================================
 * 全键递归收集（对标 qsettings_win.cpp allKeys）
 * ========================================================================== */

/** @brief 拼 "prefix + '\\' + name"；prefix 为空时直接返回 name 的拷贝。 */
static uint16_t* xsnJoinPath(const uint16_t* prefix, const uint16_t* name)
{
    size_t prefixLen = xsnLength(prefix);
    size_t nameLen = xsnLength(name);
    size_t total;
    uint16_t* out;
    if (prefixLen == 0)
        return xsnDuplicate(name, nameLen);
    total = prefixLen + 1 + nameLen;
    out = (uint16_t*)XMalloc_System((total + 1) * sizeof(uint16_t));
    if (!out)
        return NULL;
    XMemcpy(out, prefix, prefixLen * sizeof(uint16_t));
    out[prefixLen] = (uint16_t)'\\';
    if (nameLen)
        XMemcpy(out + prefixLen + 1, name, nameLen * sizeof(uint16_t));
    out[total] = 0;
    return out;
}

/** @brief 去除末尾 '\'（Qt allKeys 里 rSubKey 不带尾分隔符）。 */
static size_t xsnTrimTrailingSep(const uint16_t* path)
{
    size_t len = xsnLength(path);
    while (len > 0 && path[len - 1] == (uint16_t)'\\')
        --len;
    return len;
}

/**
 * @brief 递归主体（对标 allKeys(HKEY parentHandle, QString rSubKey, NameSet*)）。
 * @param handle 当前已打开的键。
 * @param rSubKey 当前键的路径前缀（不含末尾 '\'）。
 * @param basePath 回调前缀（相对语义用；可为空）。
 * @param func 回调。
 * @param stop 提前终止标记。
 */
static void xsnCollectRecursive(HKEY handle, const uint16_t* rSubKey,
                                const uint16_t* basePath,
                                XSettingsNativePathEnumFunc func, void* userData,
                                bool* stop)
{
    DWORD numSubKeys = 0;
    DWORD maxSubKeyLen = 0;
    DWORD numValues = 0;
    DWORD maxValueNameLen = 0;
    DWORD index;

    if (*stop)
        return;
    if (RegQueryInfoKeyW(handle, NULL, NULL, NULL, &numSubKeys, &maxSubKeyLen, NULL,
                         &numValues, &maxValueNameLen, NULL, NULL, NULL) != ERROR_SUCCESS)
        return;

    /* 1) 本层值 → 叶子键路径 */
    if (numValues > 0) {
        wchar_t* buffer = (wchar_t*)XMalloc_System(((size_t)maxValueNameLen + 2) * sizeof(wchar_t));
        if (buffer) {
            for (index = 0; index < numValues && !*stop; ++index) {
                DWORD length = maxValueNameLen + 1;
                uint16_t* joined;
                uint16_t* relative;
                if (RegEnumValueW(handle, index, buffer, &length, NULL, NULL, NULL, NULL)
                    != ERROR_SUCCESS)
                    continue;
                /* 空值名（默认值）按 Qt 口径当 "." */
                if (length == 0) {
                    static const uint16_t dot[] = { (uint16_t)'.', 0 };
                    joined = xsnJoinPath(rSubKey, dot);
                }
                else {
                    joined = xsnJoinPath(rSubKey, (const uint16_t*)buffer);
                }
                if (!joined)
                    continue;
                {
                    /* relPath：去掉 basePath 前缀（含分隔符） */
                    size_t baseLen = xsnTrimTrailingSep(basePath);
                    const uint16_t* rel = joined;
                    if (baseLen > 0 && xsnLength(joined) > baseLen &&
                        joined[baseLen] == (uint16_t)'\\')
                        rel = joined + baseLen + 1;
                    relative = xsnDuplicate(rel, xsnLength(rel));
                }
                if (relative) {
                    if (!func(userData, relative))
                        *stop = true;
                    XFree_System(relative);
                }
                XFree_System(joined);
            }
            XFree_System(buffer);
        }
    }

    /* 2) 子键 → 递归 */
    if (numSubKeys > 0 && !*stop) {
        wchar_t* buffer = (wchar_t*)XMalloc_System(((size_t)maxSubKeyLen + 2) * sizeof(wchar_t));
        if (buffer) {
            DWORD i;
            for (i = 0; i < numSubKeys && !*stop; ++i) {
                DWORD length = maxSubKeyLen + 1;
                uint16_t* subPath;
                HKEY child;
                if (RegEnumKeyExW(handle, i, buffer, &length, NULL, NULL, NULL, NULL)
                    != ERROR_SUCCESS)
                    continue;
                subPath = xsnJoinPath(rSubKey, (const uint16_t*)buffer);
                if (!subPath)
                    continue;
                child = xsnOpen(handle, KEY_READ, (const uint16_t*)buffer, 0);
                if (child) {
                    xsnCollectRecursive(child, subPath, basePath, func, userData, stop);
                    RegCloseKey(child);
                }
                XFree_System(subPath);
            }
            XFree_System(buffer);
        }
    }
}

bool XSettingsNative_collectAllKeys(XSettingsNativeRoot root, XSettingsNativeView view,
                                    const uint16_t* subKey, const uint16_t* basePath,
                                    XSettingsNativePathEnumFunc func, void* userData)
{
    HKEY parent = xsnRootHandle(root);
    REGSAM access = xsnViewAccess(view);
    HKEY handle;
    bool stop = false;

    if (!parent || !func)
        return false;

    handle = xsnOpen(parent, KEY_READ, subKey, access);
    if (!handle) {
        return true;   /* 键不存在：空结果不算错误（Qt allKeys 同款） */
    }

    xsnCollectRecursive(handle, subKey ? subKey : (const uint16_t*)L"",
                        basePath, func, userData, &stop);
    RegCloseKey(handle);
    return true;
}

#endif /* XSETTINGS_ON && defined(_WIN32) */
