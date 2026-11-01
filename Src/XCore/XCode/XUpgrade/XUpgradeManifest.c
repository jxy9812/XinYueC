/**
 * @file       XUpgradeManifest.c
 * @brief      升级 manifest 解析实现（schema v1，设计文档 §4）。
 * @details    经 XJson 解析入 XUpgradeManifest 契约结构体：JSON 非法、schema
 *             不为 1、必填字段缺失、类型不符、数组越限或字段超长一律整单拒
 *             绝，out 保持清零态（XUpgradeError_ManifestInvalid 语义）。路径
 *             类字段（组件 id/os/版本串/包文件名）白名单 [0-9A-Za-z._-] 且拒
 *             绝 "."/".."，防路径逃逸（设计文档 §2/§5）；min_version 等可选
 *             版本字段同样按白名单校验。sig 字段仅解析保存；canonical 序列
 *             化（XUpgradeManifest_canonicalBytes）与灰度分流哈希
 *             （XUpgradeManifest_grayHit）的算法唯一实现在本文件（契约位，
 *             XUpgradeGray.c/XUpgradeVerify.c 消费），ECDSA P-256 验签属
 *             XUpgradeVerify；包
 *             sha256 校验开关的读取接口同属验签文件，不在本层实现。实现只
 *             依赖 XinYueC 抽象层（XJson/XString/XByteArray），禁止直接调
 *             用 Win32、POSIX 等平台 API。
 */

#if XUPGRADE_ON

#include "XUpgradeManifest.h"

#include <string.h>

#include "XJsonDocument.h"
#include "XMemory.h"       /* XMemcpy：grayHit 摘要拷贝（统一分配器口径）。 */
#include "XUpgradePackage.h" /* XUPGRADE_SHA256_BIN_SIZE：摘要长度常量（验签同源）。 */
#include "XCryptographicHash.h" /* grayHit 分流哈希：SHA-256 门面（引擎/验签同口径）。 */

/* ========================================================================== */
/*                                  内部类型                                   */
/* ========================================================================== */

/**
 * @brief 字符串字段落盘拷贝模式（内部）。
 */
typedef enum XUpgradeManifestCopyMode {
    XUpgradeManifestCopy_Plain = 0,   /**< 自由文本：仅拒绝 NUL 字节（build/salt/名单/sig/约束串）。 */
    XUpgradeManifestCopy_Version = 1, /**< 可空版本串：白名单 [0-9A-Za-z._-]（min_version 等可选字段）。 */
    XUpgradeManifestCopy_Token = 2    /**< 路径令牌：非空 + 白名单 + 拒绝 "."/".."（id/os/version/file）。 */
} XUpgradeManifestCopyMode;

/* ========================================================================== */
/*                            字符校验与比较辅助                               */
/* ========================================================================== */

/**
 * @brief 判断字符是否在 manifest 字段白名单 [0-9A-Za-z._-] 内。
 * @param c 待判断字符。
 * @return 合法返回 true；否则返回 false。
 * @note 白名单不含路径分隔符，配合 "."/".." 段拒绝防止版本目录/包文件名逃逸。
 */
static bool XUpgradeManifest_charAllowed(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-';
}

/**
 * @brief 校验 SHA-256 十六进制串（恰好 64 个小写十六进制字符）。
 * @param text 待校验的 NUL 结尾字符串；不能为 NULL。
 * @return 合法返回 true；长度不为 64 或含大写/非十六进制字符返回 false。
 * @note manifest 契约约定 sha256 为小写十六进制（XUpgradeManifest.h）。
 */
static bool XUpgradeManifest_isValidSha256Hex(const char* text)
{
    size_t index;
    for (index = 0; index < 64; ++index) {
        char c = text[index];
        bool digit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!digit)
            return false;
    }
    return text[64] == '\0';
}

/**
 * @brief 比较两段等长字节序列的字典序。
 * @param lhs 左序列；借用。
 * @param rhs 右序列；借用。
 * @param length 比较字节数；0 直接返回 0。
 * @return 左小返回 -1；相等返回 0；左大返回 1。
 */
static int XUpgradeManifest_compareBytes(const char* lhs, const char* rhs, size_t length)
{
    int order;
    if (length == 0)
        return 0;
    order = memcmp(lhs, rhs, length);
    if (order < 0)
        return -1;
    if (order > 0)
        return 1;
    return 0;
}

/**
 * @brief 取版本串自 start 起当前段的长度（'.' 或结尾前）。
 * @param version 版本串；借用。
 * @param start 起始下标。
 * @return 段长度；start 位于结尾时为 0（视作补齐的数值 0 段）。
 */
static size_t XUpgradeManifest_segmentLength(const char* version, size_t start)
{
    size_t length = 0;
    while (version[start + length] != '\0' && version[start + length] != '.')
        ++length;
    return length;
}

/**
 * @brief 判断版本段是否为纯数字段。
 * @param segment 段起始指针；借用。
 * @param length 段长度。
 * @return length 大于 0 且全部为 ASCII 数字返回 true；否则返回 false。
 */
static bool XUpgradeManifest_segmentIsNumeric(const char* segment, size_t length)
{
    size_t index;
    if (length == 0)
        return false;
    for (index = 0; index < length; ++index) {
        if (segment[index] < '0' || segment[index] > '9')
            return false;
    }
    return true;
}

/**
 * @brief 计算数字段去除前导零后的有效起始下标。
 * @param segment 段起始指针；借用。
 * @param length 段长度。
 * @return 去前导零后的起始下标；全零段返回 length（有效长度 0，即数值 0）。
 */
static size_t XUpgradeManifest_skipLeadingZeros(const char* segment, size_t length)
{
    size_t start = 0;
    while (start < length && segment[start] == '0')
        ++start;
    return start;
}

/**
 * @brief 比较两个版本段（内部）。
 * @param lhs 左段起始指针；借用。
 * @param lhsLength 左段长度；0 表示段数不足补齐的空段（按数值 0 参与）。
 * @param rhs 右段起始指针；借用。
 * @param rhsLength 右段长度；语义同 lhsLength。
 * @return 左小返回 -1；相等返回 0；左大返回 1。
 * @note 双方均为数字段（含空段）时按无符号整数比较——先比去前导零后的有效
 *       长度，再按字节比较，任意长度不溢出；否则按 UTF-8 字节序比较。
 */
static int XUpgradeManifest_compareSegment(const char* lhs, size_t lhsLength,
                                           const char* rhs, size_t rhsLength)
{
    bool lhsNumeric = lhsLength == 0 || XUpgradeManifest_segmentIsNumeric(lhs, lhsLength);
    bool rhsNumeric = rhsLength == 0 || XUpgradeManifest_segmentIsNumeric(rhs, rhsLength);
    size_t lhsStart;
    size_t rhsStart;
    size_t lhsDigits;
    size_t rhsDigits;
    size_t common;
    int order;
    if (lhsNumeric && rhsNumeric) {
        lhsStart = XUpgradeManifest_skipLeadingZeros(lhs, lhsLength);
        rhsStart = XUpgradeManifest_skipLeadingZeros(rhs, rhsLength);
        lhsDigits = lhsLength - lhsStart;
        rhsDigits = rhsLength - rhsStart;
        if (lhsDigits != rhsDigits)
            return lhsDigits < rhsDigits ? -1 : 1;
        return XUpgradeManifest_compareBytes(lhs + lhsStart, rhs + rhsStart, lhsDigits);
    }
    common = lhsLength < rhsLength ? lhsLength : rhsLength;
    order = XUpgradeManifest_compareBytes(lhs, rhs, common);
    if (order != 0)
        return order;
    if (lhsLength == rhsLength)
        return 0;
    return lhsLength < rhsLength ? -1 : 1;
}

/**
 * @brief 比较两个 NUL 结尾字符串是否相等。
 * @param lhs 左串；借用。
 * @param rhs 右串；借用。
 * @return 相等返回 true；否则返回 false。
 * @note 替代 string.h 的 strcmp（风格指南白名单仅放行 mem* 原语）。
 */
static bool XUpgradeManifest_cstrEquals(const char* lhs, const char* rhs)
{
    size_t index = 0;
    while (lhs[index] != '\0' && rhs[index] != '\0') {
        if (lhs[index] != rhs[index])
            return false;
        ++index;
    }
    return lhs[index] == rhs[index];
}

/**
 * @brief 校验路径类令牌（组件 id/os/版本串/包文件名）。
 * @param utf8 待校验字节串；借用。
 * @param length 字节数。
 * @return 非空、全部字符在白名单内且不为 "."/".." 返回 true；否则返回 false。
 */
static bool XUpgradeManifest_isValidPathToken(const char* utf8, size_t length)
{
    size_t index;
    if (length == 0)
        return false;
    for (index = 0; index < length; ++index) {
        if (!XUpgradeManifest_charAllowed(utf8[index]))
            return false;
    }
    if (length == 1 && utf8[0] == '.')
        return false;
    if (length == 2 && utf8[0] == '.' && utf8[1] == '.')
        return false;
    return true;
}

/* ========================================================================== */
/*                            XJson 取值辅助                                   */
/* ========================================================================== */

/**
 * @brief 按键取对象成员（内部）。
 * @param object JSON 对象；借用，不能为 NULL。
 * @param key UTF-8 键名；借用，不能为 NULL。
 * @param out 输出命中值；仅返回 1 时有效。
 * @return 键存在返回 1（*out 为堆上深拷贝值，调用者必须 XJsonValue_delete）；
 *         键不存在返回 0；内部分配失败返回 -1。
 */
static int XUpgradeManifest_findValue(const XJsonObject* object, const char* key, XJsonValue** out)
{
    XJsonValue* value;
    value = XJsonObject_value_keyUtf8(object, key);
    if (!value)
        return -1;
    if (XJsonValue_isUndefined(value)) {
        /* 键缺失时接口返回堆上 Undefined 值，同样需要释放 */
        XJsonValue_delete(value);
        return 0;
    }
    *out = value;
    return 1;
}

/**
 * @brief 将借用的 UTF-8 串按模式校验后拷入定长缓冲区。
 * @param dst 目标缓冲区；调用方提供存储空间。
 * @param cap 缓冲区容量（含结尾 NUL）。
 * @param utf8 源 UTF-8 字节串；借用。
 * @param length 源字节数。
 * @param mode 拷贝模式（NUL 检查/白名单/路径令牌）。
 * @return 拷贝成功返回 true；超容量、含 NUL 或不满足模式要求返回 false。
 * @note 超容量按"字段越限"整单拒绝，不做静默截断。
 */
static bool XUpgradeManifest_storeString(char* dst, uint32_t cap, const char* utf8,
                                         size_t length, XUpgradeManifestCopyMode mode)
{
    size_t index;
    if (cap == 0 || length >= (size_t)cap)
        return false;
    for (index = 0; index < length; ++index) {
        if (utf8[index] == '\0')
            return false;
        if (mode != XUpgradeManifestCopy_Plain && !XUpgradeManifest_charAllowed(utf8[index]))
            return false;
    }
    if (mode == XUpgradeManifestCopy_Token && !XUpgradeManifest_isValidPathToken(utf8, length))
        return false;
    memcpy(dst, utf8, length);
    dst[length] = '\0';
    return true;
}

/**
 * @brief 读取对象中的字符串字段并拷入定长缓冲区。
 * @param object JSON 对象；借用，不能为 NULL。
 * @param key UTF-8 键名；借用，不能为 NULL。
 * @param dst 目标缓冲区；调用方提供存储空间。
 * @param cap 缓冲区容量（含结尾 NUL）；0 返回 false。
 * @param required 字段是否必填；缺失必填字段返回 false。
 * @param mode 拷贝模式。
 * @return 成功返回 true（缺失且非必填时 dst 置空串）；类型不符、越限或校验
 *         不通过返回 false。
 */
static bool XUpgradeManifest_getString(const XJsonObject* object, const char* key,
                                       char* dst, uint32_t cap,
                                       bool required, XUpgradeManifestCopyMode mode)
{
    XJsonValue* value;
    const XString* string;
    const char* utf8;
    size_t length;
    int found;
    bool ok = true;
    if (cap == 0)
        return false;
    dst[0] = '\0';
    found = XUpgradeManifest_findValue(object, key, &value);
    if (found == 0)
        return !required;
    if (found < 0)
        return false;
    string = XJsonValue_toString(value);
    if (!string) {
        ok = false;
    }
    else {
        utf8 = XString_toUtf8(string);
        length = utf8 ? XString_toUtf8_length(string) : 0;
        /* 空 XString 的 toUtf8 返回 NULL（无数据缓冲），按契约"空串=未设置"
         * 接受（Copy_Version 注释"可空版本串"），不得整单拒绝。 */
        if (utf8 == NULL) {
            utf8 = "";
            length = 0;
        }
        ok = XUpgradeManifest_storeString(dst, cap, utf8, length, mode);
    }
    XJsonValue_delete(value);
    return ok;
}

/**
 * @brief 读取对象中的整数字段（仅接受 JSON 整数类型）。
 * @param object JSON 对象；借用，不能为 NULL。
 * @param key UTF-8 键名；借用，不能为 NULL。
 * @param required 字段是否必填；缺失必填字段返回 false。
 * @param out 输出整数值；缺失且非必填时写 0。
 * @return 成功返回 true；类型不为整数（浮点/字符串等按类型错误）返回 false。
 */
static bool XUpgradeManifest_getInt(const XJsonObject* object, const char* key,
                                    bool required, int64_t* out)
{
    XJsonValue* value;
    int found;
    found = XUpgradeManifest_findValue(object, key, &value);
    if (found == 0) {
        if (required)
            return false;
        *out = 0;
        return true;
    }
    if (found < 0)
        return false;
    if (!XJsonValue_isInt(value)) {
        XJsonValue_delete(value);
        return false;
    }
    *out = XJsonValue_toInt(value, 0);
    XJsonValue_delete(value);
    return true;
}

/* ========================================================================== */
/*                          子结构解析（requires/gray/组件）                    */
/* ========================================================================== */

/**
 * @brief 解析 policy 字符串为策略枚举。
 * @param text 策略文本（"forced"/"optional"/"silent"）；借用。
 * @param policy 输出策略；不能为 NULL。
 * @return 识别成功返回 true；未知策略文本返回 false（按字段非法整单拒绝）。
 */
static bool XUpgradeManifest_policyFromString(const char* text, XUpgradePolicy* policy)
{
    if (XUpgradeManifest_cstrEquals(text, "forced"))
        *policy = XUpgradePolicy_Forced;
    else if (XUpgradeManifest_cstrEquals(text, "optional"))
        *policy = XUpgradePolicy_Optional;
    else if (XUpgradeManifest_cstrEquals(text, "silent"))
        *policy = XUpgradePolicy_Silent;
    else
        return false;
    return true;
}

/**
 * @brief 解析 requires 依赖约束表。
 * @param requires 输出约束表；调用方提供存储空间（已清零）。
 * @param value requires 字段的 JSON 值；借用，必须为对象。
 * @return 解析成功返回 true；类型不符、条数超过 XUPGRADE_REQUIRES_MAX、组件
 *         id 非法或约束串越限返回 false。
 * @note 键为被依赖组件 id（路径令牌校验），值为约束串（如 ">=2.0"，含 '>'/
 *       '='，按自由文本校验）；约束语义的强制执行属 staging 校验实现。
 */
static bool XUpgradeManifest_parseRequires(XUpgradeManifestRequires* requires, const XJsonValue* value)
{
    XJsonObject* object;
    XVector* keys;
    size_t total;
    size_t index;
    if (!XJsonValue_isObject(value))
        return false;
    object = XJsonValue_toObject(value);
    if (!object)
        return false;
    total = (size_t)XJsonObject_size_base((const XContainer*)object);
    if (total > (size_t)XUPGRADE_REQUIRES_MAX)
        return false;
    if (total == 0)
        return true;
    keys = XJsonObject_keys_base((const XMapBase*)object);
    if (!keys)
        return false;
    for (index = 0; index < total; ++index) {
        XString* key = (XString*)XVector_at_base(keys, (int64_t)index);
        XJsonValue* constraint;
        const XString* text;
        const char* keyUtf8;
        const char* constraintUtf8;
        size_t keyLength;
        size_t constraintLength;
        int found;
        if (!key) {
            XClassDelete(keys);
            return false;
        }
        keyUtf8 = XString_toUtf8(key);
        keyLength = keyUtf8 ? XString_toUtf8_length(key) : 0;
        if (!keyUtf8 || keyLength >= (size_t)XUPGRADE_COMPONENT_ID_MAX ||
            !XUpgradeManifest_isValidPathToken(keyUtf8, keyLength)) {
            XClassDelete(keys);
            return false;
        }
        found = XUpgradeManifest_findValue(object, keyUtf8, &constraint);
        if (found != 1) {
            XClassDelete(keys);
            return false;
        }
        text = XJsonValue_toString(constraint);
        constraintUtf8 = text ? XString_toUtf8(text) : NULL;
        constraintLength = constraintUtf8 ? XString_toUtf8_length(text) : 0;
        if (!constraintUtf8 || constraintLength >= (size_t)XUPGRADE_CONSTRAINT_MAX ||
            !XUpgradeManifest_storeString(requires->m_component[index],
                                          XUPGRADE_COMPONENT_ID_MAX, keyUtf8, keyLength,
                                          XUpgradeManifestCopy_Plain) ||
            !XUpgradeManifest_storeString(requires->m_constraint[index],
                                          XUPGRADE_CONSTRAINT_MAX, constraintUtf8,
                                          constraintLength, XUpgradeManifestCopy_Plain)) {
            XJsonValue_delete(constraint);
            XClassDelete(keys);
            return false;
        }
        XJsonValue_delete(constraint);
    }
    requires->m_count = (uint32_t)total;
    XClassDelete(keys);
    return true;
}

/**
 * @brief 解析灰度 allow/deny 名单数组。
 * @param rows 输出行缓冲区；调用方提供存储空间。
 * @param capacity 行数上限（XUPGRADE_GRAY_LIST_MAX）。
 * @param count 输出有效行数。
 * @param value 名单字段的 JSON 值；借用，必须为数组。
 * @return 解析成功返回 true；类型不符、条数越限或元素类型/长度非法返回 false。
 */
static bool XUpgradeManifest_parseSerialList(char rows[][XUPGRADE_SERIAL_MAX],
                                             uint32_t capacity, uint32_t* count,
                                             const XJsonValue* value)
{
    XJsonArray* array;
    int64_t total;
    int64_t index;
    if (!XJsonValue_isArray(value))
        return false;
    array = XJsonValue_toArray(value);
    if (!array)
        return false;
    total = (int64_t)XJsonArray_size_base((const XContainer*)array);
    if (total > (int64_t)capacity)
        return false;
    for (index = 0; index < total; ++index) {
        const XJsonValue* element = XJsonArray_at_const(array, index);
        const XString* string;
        const char* utf8;
        size_t length;
        if (!element || !XJsonValue_isString(element))
            return false;
        string = XJsonValue_toString(element);
        if (!string)
            return false;
        utf8 = XString_toUtf8(string);
        length = utf8 ? XString_toUtf8_length(string) : 0;
        if (!utf8 || !XUpgradeManifest_storeString(rows[index], XUPGRADE_SERIAL_MAX,
                                                   utf8, length, XUpgradeManifestCopy_Plain))
            return false;
    }
    *count = (uint32_t)total;
    return true;
}

/**
 * @brief 解析 gray 灰度规则（M1 仅解析保存，不参与判定）。
 * @param gray 输出灰度规则；调用方提供存储空间（已清零）。
 * @param value gray 字段的 JSON 值；借用，必须为对象。
 * @return 解析成功返回 true；类型不符、percent 越界、名单越限或元素非法返回
 *         false。
 */
static bool XUpgradeManifest_parseGray(XUpgradeManifestGray* gray, const XJsonValue* value)
{
    XJsonObject* object;
    XJsonValue* list;
    int64_t number;
    int found;
    bool ok;
    if (!XJsonValue_isObject(value))
        return false;
    object = XJsonValue_toObject(value);
    if (!object)
        return false;
    ok = XUpgradeManifest_getInt(object, "percent", true, &number);
    if (ok && (number < 0 || number > 100))
        ok = false;
    if (ok)
        gray->m_percent = (uint32_t)number;
    if (ok)
        ok = XUpgradeManifest_getString(object, "salt", gray->m_salt,
                                        XUPGRADE_SERIAL_MAX, true, XUpgradeManifestCopy_Plain);
    if (ok) {
        found = XUpgradeManifest_findValue(object, "allow", &list);
        if (found < 0)
            ok = false;
        else if (found == 1) {
            ok = XUpgradeManifest_parseSerialList(gray->m_allow, XUPGRADE_GRAY_LIST_MAX,
                                                  &gray->m_allowCount, list);
            XJsonValue_delete(list);
        }
    }
    if (ok) {
        found = XUpgradeManifest_findValue(object, "deny", &list);
        if (found < 0)
            ok = false;
        else if (found == 1) {
            ok = XUpgradeManifest_parseSerialList(gray->m_deny, XUPGRADE_GRAY_LIST_MAX,
                                                  &gray->m_denyCount, list);
            XJsonValue_delete(list);
        }
    }
    return ok;
}

/**
 * @brief 解析单个组件条目（components[] 元素）。
 * @param component 输出组件条目；调用方提供存储空间（已清零）。
 * @param value 组件条目的 JSON 值；借用，必须为对象。
 * @return 解析成功返回 true；类型不符、必填字段缺失、白名单/长度校验不通过
 *         或 requires 非法返回 false。
 */
static bool XUpgradeManifest_parseComponent(XUpgradeManifestComponent* component, const XJsonValue* value)
{
    XJsonObject* object;
    XJsonValue* requires;
    char sha256Text[XUPGRADE_SHA256_HEX_MAX];
    int64_t number;
    int found;
    bool ok;
    if (!XJsonValue_isObject(value))
        return false;
    object = XJsonValue_toObject(value);
    if (!object)
        return false;
    ok = XUpgradeManifest_getString(object, "id", component->m_id,
                                    XUPGRADE_COMPONENT_ID_MAX, true, XUpgradeManifestCopy_Token);
    if (ok)
        ok = XUpgradeManifest_getString(object, "os", component->m_os,
                                        XUPGRADE_OS_MAX, true, XUpgradeManifestCopy_Token);
    if (ok)
        ok = XUpgradeManifest_getString(object, "version", component->m_version,
                                        XUPGRADE_VERSION_MAX, true, XUpgradeManifestCopy_Token);
    if (ok)
        ok = XUpgradeManifest_getString(object, "file", component->m_file,
                                        XUPGRADE_FILENAME_MAX, true, XUpgradeManifestCopy_Token);
    if (ok)
        ok = XUpgradeManifest_getInt(object, "size", true, &number);
    if (ok && number < 0)
        ok = false;
    if (ok)
        component->m_size = (uint64_t)number;
    if (ok)
        ok = XUpgradeManifest_getString(object, "sha256", sha256Text,
                                        XUPGRADE_SHA256_HEX_MAX, true, XUpgradeManifestCopy_Plain);
    if (ok)
        ok = XUpgradeManifest_isValidSha256Hex(sha256Text);
    if (ok)
        memcpy(component->m_sha256, sha256Text, XUPGRADE_SHA256_HEX_MAX);
    if (ok) {
        found = XUpgradeManifest_findValue(object, "requires", &requires);
        if (found < 0)
            ok = false;
        else if (found == 1) {
            ok = XUpgradeManifest_parseRequires(&component->m_requires, requires);
            XJsonValue_delete(requires);
        }
    }
    return ok;
}

/**
 * @brief 解析 components 数组并写入组件表。
 * @param out manifest 输出结构体；调用方提供存储空间（已清零）。
 * @param root manifest 根对象；借用，不能为 NULL。
 * @return 解析成功返回 true；字段缺失、类型不符或条数不在 [1,
 *         XUPGRADE_COMPONENT_MAX] 返回 false。
 */
static bool XUpgradeManifest_fillComponents(XUpgradeManifest* out, const XJsonObject* root)
{
    XJsonValue* value;
    XJsonArray* array;
    int64_t total;
    int64_t index;
    int found = XUpgradeManifest_findValue(root, "components", &value);
    if (found != 1)
        return false;
    if (!XJsonValue_isArray(value)) {
        XJsonValue_delete(value);
        return false;
    }
    array = XJsonValue_toArray(value);
    if (!array) {
        XJsonValue_delete(value);
        return false;
    }
    total = (int64_t)XJsonArray_size_base((const XContainer*)array);
    if (total < 1 || total > (int64_t)XUPGRADE_COMPONENT_MAX) {
        XJsonValue_delete(value);
        return false;
    }
    for (index = 0; index < total; ++index) {
        const XJsonValue* element = XJsonArray_at_const(array, index);
        if (!element || !XJsonValue_isObject(element) ||
            !XUpgradeManifest_parseComponent(&out->m_components[index], element)) {
            XJsonValue_delete(value);
            return false;
        }
    }
    out->m_componentCount = (uint32_t)total;
    XJsonValue_delete(value);
    return true;
}

/**
 * @brief 解析 manifest 根对象的全部字段（除 components）。
 * @param out manifest 输出结构体；调用方提供存储空间（已清零）。
 * @param root manifest 根对象；借用，不能为 NULL。
 * @return 解析成功返回 true；字段缺失、类型不符或校验不通过返回 false。
 * @note 必填字段：schema（恒为 1）/channel/version；components 由
 *       XUpgradeManifest_fillComponents 单独解析。可选字段缺省值：policy 为
 *       Optional（未知策略文本拒绝），build/min_version/rollback_to/
 *       policy_forced_below/sig 缺省空串，forced_grace_hours 缺省 0，gray 全零。
 */
static bool XUpgradeManifest_fillRoot(XUpgradeManifest* out, const XJsonObject* root)
{
    char policyText[16];
    XJsonValue* value;
    int64_t number;
    int found;
    bool ok;
    ok = XUpgradeManifest_getInt(root, "schema", true, &number);
    if (ok && number != 1)
        ok = false;
    if (ok)
        out->m_schema = 1u;
    if (ok)
        ok = XUpgradeManifest_getString(root, "channel", out->m_channel,
                                        XUPGRADE_CHANNEL_MAX, true, XUpgradeManifestCopy_Token);
    if (ok)
        ok = XUpgradeManifest_getString(root, "version", out->m_version,
                                        XUPGRADE_VERSION_MAX, true, XUpgradeManifestCopy_Token);
    if (ok)
        ok = XUpgradeManifest_getString(root, "build", out->m_build,
                                        XUPGRADE_BUILD_MAX, false, XUpgradeManifestCopy_Plain);
    if (ok)
        ok = XUpgradeManifest_getString(root, "min_version", out->m_minVersion,
                                        XUPGRADE_VERSION_MAX, false, XUpgradeManifestCopy_Version);
    if (ok)
        ok = XUpgradeManifest_getString(root, "rollback_to", out->m_rollbackTo,
                                        XUPGRADE_VERSION_MAX, false, XUpgradeManifestCopy_Version);
    out->m_policy = XUpgradePolicy_Optional;
    if (ok)
        ok = XUpgradeManifest_getString(root, "policy", policyText,
                                        (uint32_t)sizeof(policyText), false,
                                        XUpgradeManifestCopy_Plain);
    if (ok && policyText[0] != '\0')
        ok = XUpgradeManifest_policyFromString(policyText, &out->m_policy);
    if (ok)
        ok = XUpgradeManifest_getString(root, "policy_forced_below", out->m_policyForcedBelow,
                                        XUPGRADE_VERSION_MAX, false, XUpgradeManifestCopy_Version);
    if (ok)
        ok = XUpgradeManifest_getInt(root, "forced_grace_hours", false, &number);
    if (ok && number < 0)
        ok = false;                         /* 宽限期为小时数（uint32），负值按字段非法拒绝 */
    if (ok)
        out->m_forcedGraceHours = (uint32_t)number; /* 缺省 0（staged 完成即自动 apply） */
    if (ok) {
        found = XUpgradeManifest_findValue(root, "gray", &value);
        if (found < 0)
            ok = false;
        else if (found == 1) {
            ok = XUpgradeManifest_parseGray(&out->m_gray, value);
            XJsonValue_delete(value);
        }
    }
    if (ok)
        ok = XUpgradeManifest_fillComponents(out, root);
    if (ok)
        ok = XUpgradeManifest_getString(root, "sig", out->m_sig,
                                        XUPGRADE_SIGNATURE_MAX, false, XUpgradeManifestCopy_Plain);
    return ok;
}

/* ========================================================================== */
/*                                  公共 API                                   */
/* ========================================================================== */

bool XUpgradeManifest_parse(XUpgradeManifest* out, const char* jsonUtf8, size_t lengthBytes)
{
    XByteArray* bytes;
    XJsonDocument* document;
    XJsonObject* root;
    XJsonParseError parseError;
    bool ok;
    if (!out)
        return false;
    memset(out, 0, sizeof(XUpgradeManifest));
    if (!jsonUtf8 || lengthBytes == 0)
        return false;
    /* 契约输入不必以 NUL 结尾：按精确长度拷贝后交给解析器，
     * XJsonDocument_fromJson_ex 自行剥离末尾单个 NUL（如有）。 */
    bytes = XByteArray_create_with_data(jsonUtf8, lengthBytes);
    if (!bytes)
        return false;
    XJsonParseError_init(&parseError);
    document = XJsonDocument_fromJson_ex(bytes, &parseError);
    XClassDelete(bytes);
    if (!document) {
        XERROR_PRINTF("XUpgradeManifest: JSON invalid, offset=%lld, error=%s",
                      (long long)parseError.offset, XJsonParseError_errorString(&parseError));
        return false;
    }
    root = XJsonDocument_object(document);
    ok = root != NULL;
    if (ok)
        ok = XUpgradeManifest_fillRoot(out, root);
    XClassDelete(document);
    if (!ok) {
        XERROR_PRINTF("XUpgradeManifest: manifest invalid (schema/field/rule), rejected");
        memset(out, 0, sizeof(XUpgradeManifest));
    }
    return ok;
}

const XUpgradeManifestComponent* XUpgradeManifest_findComponent(const XUpgradeManifest* manifest,
                                                                const char* componentId,
                                                                const char* targetOs)
{
    const XUpgradeManifestComponent* fallback = NULL;
    uint32_t index;
    if (!manifest || !componentId || !targetOs)
        return NULL;
    /* 第一轮：精确 os 匹配优先 */
    for (index = 0; index < manifest->m_componentCount && index < (uint32_t)XUPGRADE_COMPONENT_MAX; ++index) {
        const XUpgradeManifestComponent* component = &manifest->m_components[index];
        if (XUpgradeManifest_cstrEquals(component->m_id, componentId) &&
            XUpgradeManifest_cstrEquals(component->m_os, targetOs))
            return component;
    }
    /* 第二轮：回退 os=="any" 条目，同 id 多条时取第一条 */
    for (index = 0; index < manifest->m_componentCount && index < (uint32_t)XUPGRADE_COMPONENT_MAX; ++index) {
        const XUpgradeManifestComponent* component = &manifest->m_components[index];
        if (XUpgradeManifest_cstrEquals(component->m_id, componentId) &&
            XUpgradeManifest_cstrEquals(component->m_os, "any")) {
            fallback = component;
            break;
        }
    }
    return fallback;
}

int XUpgradeManifest_compareVersions(const char* lhs, const char* rhs)
{
    size_t lhsIndex = 0;
    size_t rhsIndex = 0;
    if (!lhs)
        lhs = "";
    if (!rhs)
        rhs = "";
    for (;;) {
        size_t lhsLength = XUpgradeManifest_segmentLength(lhs, lhsIndex);
        size_t rhsLength = XUpgradeManifest_segmentLength(rhs, rhsIndex);
        int order = XUpgradeManifest_compareSegment(lhs + lhsIndex, lhsLength,
                                                    rhs + rhsIndex, rhsLength);
        if (order != 0)
            return order;
        lhsIndex += lhsLength;
        rhsIndex += rhsLength;
        if (lhs[lhsIndex] == '\0' && rhs[rhsIndex] == '\0')
            return 0;
        /* 跳过段分隔符；段数不足的一方以 0 段补齐（空段按数值 0 参与） */
        if (lhs[lhsIndex] == '.')
            ++lhsIndex;
        if (rhs[rhsIndex] == '.')
            ++rhsIndex;
    }
}

/* ========================================================================== */
/*                        灰度分流哈希与 canonical 序列化                       */
/*         （契约位唯一实现：算法入契约 §4.2/§11，跨版本不得变化）              */
/* ========================================================================== */

/**
 * @brief 计算 NUL 结尾字符串的字节长度（白名单内仅 mem* 原语，等价 strlen）。
 * @param text 目标串；可为 NULL。
 * @return 字节数（不含结尾 NUL）；text 为 NULL 返回 0。
 * @note 库内禁用 string.h 字符串函数（白名单仅放行 mem* 原语），同批先例
 *       XUpgradePackage.c xupgrade_package_cstrLen。
 */
static size_t XUpgradeManifest_cstrLength(const char* text)
{
    size_t length = 0;
    if (!text)
        return 0;
    while (text[length] != '\0')
        ++length;
    return length;
}

bool XUpgradeManifest_grayHit(const XUpgradeManifestGray* gray, const char* deviceKey)
{
    static const char emptyKey = 0;
    size_t saltLength;
    size_t keyLength;
    XCryptographicHash hash;
    XByteArrayView view;
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    uint32_t bucket;
    uint32_t index;

    if (!gray)
        return false;
    if (!deviceKey)
        deviceKey = &emptyKey;              /* 契约：NULL 按空串处理 */
    /* 判定顺序（§11，不得随版本变化）：allow 精确命中 → true；deny 精确
     * 命中 → false；空名单视为不命中。分类判定（XUpgradeGray_evaluate）
     * 与本入口重复检查同一名单，结论恒一致。 */
    for (index = 0; index < gray->m_allowCount && index < (uint32_t)XUPGRADE_GRAY_LIST_MAX; ++index) {
        if (gray->m_allow[index][0] != '\0' &&
            XUpgradeManifest_cstrEquals(gray->m_allow[index], deviceKey))
            return true;
    }
    for (index = 0; index < gray->m_denyCount && index < (uint32_t)XUPGRADE_GRAY_LIST_MAX; ++index) {
        if (gray->m_deny[index][0] != '\0' &&
            XUpgradeManifest_cstrEquals(gray->m_deny[index], deviceKey))
            return false;
    }
    /* 分流哈希：SHA256(salt 的 UTF-8 字节 || deviceKey 的 UTF-8 字节，无分隔符)，
     * 摘要前 4 字节按大端无符号整数 mod 100 < percent。 */
    saltLength = XUpgradeManifest_cstrLength(gray->m_salt);
    keyLength = XUpgradeManifest_cstrLength(deviceKey);
    XCryptographicHash_init(&hash, XCryptographicHash_Sha256);
    if (saltLength > 0)
        XCryptographicHash_addData(&hash, gray->m_salt, saltLength);
    if (keyLength > 0)
        XCryptographicHash_addData(&hash, deviceKey, keyLength);
    view = XCryptographicHash_resultView(&hash);
    if (XByteArrayView_size(&view) != XUPGRADE_SHA256_BIN_SIZE) {
        XCryptographicHash_deinit(&hash);
        return false;                       /* 摘要不可得按未命中（灰度从保守） */
    }
    XMemcpy(digest, XByteArrayView_data(&view), XUPGRADE_SHA256_BIN_SIZE);
    XCryptographicHash_deinit(&hash);
    bucket = ((uint32_t)digest[0] << 24) | ((uint32_t)digest[1] << 16) |
             ((uint32_t)digest[2] << 8) | (uint32_t)digest[3];
    return (bucket % 100u) < gray->m_percent;
}

/**
 * @brief canonical 序列化的定界缓冲写入器（内部）。
 * @details 容量不足置 overflow 并丢弃后续写入（契约：失败返回 false 且
 *          outLen 不写入）。
 */
typedef struct XUpgradeManifestCanonWriter {
    uint8_t* buffer;                        /**< 输出缓冲区（借用）。 */
    uint32_t cap;                           /**< 缓冲区容量。 */
    uint32_t length;                        /**< 已写字节数。 */
    bool overflow;                          /**< 容量越界标志。 */
} XUpgradeManifestCanonWriter;

/** @brief 追加原始字节（无转义）。 */
static void XUpgradeManifest_canonWriteRaw(XUpgradeManifestCanonWriter* writer,
                                           const char* bytes, size_t length)
{
    if (!writer || writer->overflow || length == 0)
        return;
    if ((uint64_t)writer->length + (uint64_t)length > (uint64_t)writer->cap) {
        writer->overflow = true;
        return;
    }
    memcpy(writer->buffer + writer->length, bytes, length);
    writer->length += (uint32_t)length;
}

/** @brief 写 JSON 键（含结尾冒号）：\"key\":。 */
static void XUpgradeManifest_canonWriteKey(XUpgradeManifestCanonWriter* writer,
                                           const char* key)
{
    XUpgradeManifest_canonWriteRaw(writer, "\"", 1);
    XUpgradeManifest_canonWriteRaw(writer, key, XUpgradeManifest_cstrLength(key));
    XUpgradeManifest_canonWriteRaw(writer, "\":", 2);
}

/**
 * @brief 写 JSON 字符串字面量（含引号）。
 * @details 转义 " 与 \，控制字符按 \u00xx；其余字节（含 UTF-8 多字节序列）
 *          原样直写。manifest 字段多为白名单 ASCII，防御性覆盖 Plain 模式
 *          字段（salt/名单/build）理论上的任意字节。
 */
static void XUpgradeManifest_canonWriteString(XUpgradeManifestCanonWriter* writer,
                                              const char* utf8)
{
    static const char hexDigits[] = "0123456789abcdef";
    char escape[6];
    size_t index;
    size_t length = XUpgradeManifest_cstrLength(utf8);
    XUpgradeManifest_canonWriteRaw(writer, "\"", 1);
    for (index = 0; index < length; ++index) {
        unsigned char c = (unsigned char)utf8[index];
        if (c == '"' || c == '\\') {
            escape[0] = '\\';
            escape[1] = (char)c;
            XUpgradeManifest_canonWriteRaw(writer, escape, 2);
        } else if (c < 0x20u) {
            escape[0] = '\\';
            escape[1] = 'u';
            escape[2] = '0';
            escape[3] = '0';
            escape[4] = hexDigits[c >> 4];
            escape[5] = hexDigits[c & 0x0Fu];
            XUpgradeManifest_canonWriteRaw(writer, escape, 6);
        } else {
            XUpgradeManifest_canonWriteRaw(writer, (const char*)&utf8[index], 1);
        }
    }
    XUpgradeManifest_canonWriteRaw(writer, "\"", 1);
}

/** @brief 写无符号整数十进制（无前导零）。 */
static void XUpgradeManifest_canonWriteUnsigned(XUpgradeManifestCanonWriter* writer,
                                                uint64_t value)
{
    char reversed[24];
    char digits[24];
    size_t count = 0;
    size_t index = 0;
    if (value == 0) {
        XUpgradeManifest_canonWriteRaw(writer, "0", 1);
        return;
    }
    while (value != 0) {
        reversed[count++] = (char)('0' + (char)(value % 10u));
        value /= 10u;
    }
    while (count > 0)
        digits[index++] = reversed[--count];
    XUpgradeManifest_canonWriteRaw(writer, digits, index);
}

/** @brief 策略枚举转 canonical 文本（与 policyFromString 解析文本互逆）。 */
static const char* XUpgradeManifest_policyToText(XUpgradePolicy policy)
{
    switch (policy) {
    case XUpgradePolicy_Forced:
        return "forced";
    case XUpgradePolicy_Silent:
        return "silent";
    case XUpgradePolicy_Optional:
    default:
        return "optional";
    }
}

/**
 * @brief 按字典序比较两个 NUL 结尾字符串（canonical 键排序用）。
 * @param lhs 左串；借用。 @param rhs 右串；借用。
 * @return 左小返回 -1；相等返回 0；左大返回 1。
 * @note 先比公共前缀字节，再比长度（"app" < "appx"）；键为白名单 ASCII，
 *       字节序即字典序。
 */
static int XUpgradeManifest_cstrOrder(const char* lhs, const char* rhs)
{
    size_t lhsLength = XUpgradeManifest_cstrLength(lhs);
    size_t rhsLength = XUpgradeManifest_cstrLength(rhs);
    size_t common = lhsLength < rhsLength ? lhsLength : rhsLength;
    int order = XUpgradeManifest_compareBytes(lhs, rhs, common);
    if (order != 0)
        return order;
    if (lhsLength == rhsLength)
        return 0;
    return lhsLength < rhsLength ? -1 : 1;
}

bool XUpgradeManifest_canonicalBytes(const XUpgradeManifest* manifest,
                                     uint8_t* outBuffer, uint32_t cap, uint32_t* outLen)
{
    static const char comma = ',';
    XUpgradeManifestCanonWriter writer;
    uint32_t index;
    if (!manifest || !outBuffer || !outLen || cap == 0)
        return false;
    writer.buffer = outBuffer;
    writer.cap = cap;
    writer.length = 0;
    writer.overflow = false;
    /* 根对象键字典序：build < channel < components < forced_grace_hours <
     * gray < min_version < policy < policy_forced_below < rollback_to <
     * schema < version。无空白；m_sig 不参与（§4.2 签名对象=除 sig 外全部
     * 字段；解析保存的其余字段全量出现——canonical 形态与输入省略无关）。 */
    XUpgradeManifest_canonWriteRaw(&writer, "{", 1);
    XUpgradeManifest_canonWriteKey(&writer, "build");
    XUpgradeManifest_canonWriteString(&writer, manifest->m_build);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "channel");
    XUpgradeManifest_canonWriteString(&writer, manifest->m_channel);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "components");
    XUpgradeManifest_canonWriteRaw(&writer, "[", 1);
    for (index = 0; index < manifest->m_componentCount && index < (uint32_t)XUPGRADE_COMPONENT_MAX; ++index) {
        const XUpgradeManifestComponent* component = &manifest->m_components[index];
        /* 组件对象键字典序：file < id < os < requires < sha256 < size < version。
         * requires 组件键字典序（插入排序，条数 ≤ XUPGRADE_REQUIRES_MAX）。 */
        char order[(size_t)XUPGRADE_REQUIRES_MAX];
        uint32_t slot;
        uint32_t scan;
        if (index > 0)
            XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteRaw(&writer, "{", 1);
        XUpgradeManifest_canonWriteKey(&writer, "file");
        XUpgradeManifest_canonWriteString(&writer, component->m_file);
        XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteKey(&writer, "id");
        XUpgradeManifest_canonWriteString(&writer, component->m_id);
        XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteKey(&writer, "os");
        XUpgradeManifest_canonWriteString(&writer, component->m_os);
        XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteKey(&writer, "requires");
        XUpgradeManifest_canonWriteRaw(&writer, "{", 1);
        for (slot = 0; slot < component->m_requires.m_count && slot < (uint32_t)XUPGRADE_REQUIRES_MAX; ++slot)
            order[slot] = (char)slot;
        for (slot = 1; slot < component->m_requires.m_count && slot < (uint32_t)XUPGRADE_REQUIRES_MAX; ++slot) {
            char current = order[slot];
            scan = slot;
            while (scan > 0 &&
                   XUpgradeManifest_cstrOrder(
                       component->m_requires.m_component[(uint8_t)order[scan - 1]],
                       component->m_requires.m_component[(uint8_t)current]) > 0) {
                order[scan] = order[scan - 1];
                --scan;
            }
            order[scan] = current;
        }
        for (slot = 0; slot < component->m_requires.m_count && slot < (uint32_t)XUPGRADE_REQUIRES_MAX; ++slot) {
            if (slot > 0)
                XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
            XUpgradeManifest_canonWriteKey(&writer,
                                           component->m_requires.m_component[(uint8_t)order[slot]]);
            XUpgradeManifest_canonWriteString(&writer,
                                              component->m_requires.m_constraint[(uint8_t)order[slot]]);
        }
        XUpgradeManifest_canonWriteRaw(&writer, "}", 1);
        XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteKey(&writer, "sha256");
        XUpgradeManifest_canonWriteString(&writer, component->m_sha256);
        XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteKey(&writer, "size");
        XUpgradeManifest_canonWriteUnsigned(&writer, component->m_size);
        XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteKey(&writer, "version");
        XUpgradeManifest_canonWriteString(&writer, component->m_version);
        XUpgradeManifest_canonWriteRaw(&writer, "}", 1);
    }
    XUpgradeManifest_canonWriteRaw(&writer, "]", 1);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "forced_grace_hours");
    XUpgradeManifest_canonWriteUnsigned(&writer, manifest->m_forcedGraceHours);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "gray");
    XUpgradeManifest_canonWriteRaw(&writer, "{", 1);
    /* gray 对象键字典序：allow < deny < percent < salt。 */
    XUpgradeManifest_canonWriteKey(&writer, "allow");
    XUpgradeManifest_canonWriteRaw(&writer, "[", 1);
    for (index = 0; index < manifest->m_gray.m_allowCount && index < (uint32_t)XUPGRADE_GRAY_LIST_MAX; ++index) {
        if (index > 0)
            XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteString(&writer, manifest->m_gray.m_allow[index]);
    }
    XUpgradeManifest_canonWriteRaw(&writer, "]", 1);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "deny");
    XUpgradeManifest_canonWriteRaw(&writer, "[", 1);
    for (index = 0; index < manifest->m_gray.m_denyCount && index < (uint32_t)XUPGRADE_GRAY_LIST_MAX; ++index) {
        if (index > 0)
            XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
        XUpgradeManifest_canonWriteString(&writer, manifest->m_gray.m_deny[index]);
    }
    XUpgradeManifest_canonWriteRaw(&writer, "]", 1);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "percent");
    XUpgradeManifest_canonWriteUnsigned(&writer, manifest->m_gray.m_percent);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "salt");
    XUpgradeManifest_canonWriteString(&writer, manifest->m_gray.m_salt);
    XUpgradeManifest_canonWriteRaw(&writer, "}", 1);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "min_version");
    XUpgradeManifest_canonWriteString(&writer, manifest->m_minVersion);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "policy");
    XUpgradeManifest_canonWriteString(&writer, XUpgradeManifest_policyToText(manifest->m_policy));
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "policy_forced_below");
    XUpgradeManifest_canonWriteString(&writer, manifest->m_policyForcedBelow);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "rollback_to");
    XUpgradeManifest_canonWriteString(&writer, manifest->m_rollbackTo);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "schema");
    XUpgradeManifest_canonWriteUnsigned(&writer, manifest->m_schema);
    XUpgradeManifest_canonWriteRaw(&writer, &comma, 1);
    XUpgradeManifest_canonWriteKey(&writer, "version");
    XUpgradeManifest_canonWriteString(&writer, manifest->m_version);
    XUpgradeManifest_canonWriteRaw(&writer, "}", 1);
    if (writer.overflow)
        return false;                       /* 超 XUPGRADE_MANIFEST_CANONICAL_MAX 口径 */
    *outLen = writer.length;
    return true;
}

/* ========================================================================== */
/*                     多版本目录（schema 2）解析（§4 扩展）                    */
/* ========================================================================== */

/**
 * @brief 解析目录 releases 数组并填充 release 指针表（内部）。
 * @param out 目录结构体；调用方提供存储空间（已清零）。
 * @param root 目录根对象；借用，不能为 NULL。
 * @return 解析成功返回 true；releases 缺失/非数组/越限/元素非法返回 false。
 * @note 条目字段 version/manifest 均按 Token 白名单校验（防路径逃逸，同
 *       v1 组件文件口径）。
 */
static bool XUpgradeManifest_fillCatalogReleases(XUpgradeCatalog* out, const XJsonObject* root)
{
    XJsonValue* value;
    XJsonArray* array;
    int64_t total;
    int64_t index;
    int found = XUpgradeManifest_findValue(root, "releases", &value);
    if (found != 1)
        return false;
    if (!XJsonValue_isArray(value)) {
        XJsonValue_delete(value);
        return false;
    }
    array = XJsonValue_toArray(value);
    if (!array) {
        XJsonValue_delete(value);
        return false;
    }
    total = (int64_t)XJsonArray_size_base((const XContainer*)array);
    if (total < 0 || total > (int64_t)XUPGRADE_CATALOG_MAX) {
        XJsonValue_delete(value);
        return false;
    }
    for (index = 0; index < total; ++index) {
        const XJsonValue* element = XJsonArray_at_const(array, index);
        XJsonObject* object;
        XUpgradeCatalogEntry* entry = &out->m_releases[index];
        if (!element || !XJsonValue_isObject(element)) {
            XJsonValue_delete(value);
            return false;
        }
        object = XJsonValue_toObject(element);
        if (!object ||
            !XUpgradeManifest_getString(object, "version", entry->m_version,
                                        XUPGRADE_VERSION_MAX, true,
                                        XUpgradeManifestCopy_Token) ||
            !XUpgradeManifest_getString(object, "manifest", entry->m_manifest,
                                        XUPGRADE_FILENAME_MAX, true,
                                        XUpgradeManifestCopy_Token)) {
            XJsonValue_delete(value);
            return false;
        }
    }
    out->m_releaseCount = (uint32_t)total;
    XJsonValue_delete(value);
    return true;
}

bool XUpgradeManifest_parseCatalog(XUpgradeCatalog* out, const char* jsonUtf8,
                                   size_t lengthBytes)
{
    XByteArray* bytes;
    XJsonDocument* document;
    XJsonObject* root;
    XJsonParseError parseError;
    int64_t number;
    bool ok;
    if (!out)
        return false;
    XMemset(out, 0, sizeof(*out));
    if (!jsonUtf8 || lengthBytes == 0)
        return false;
    bytes = XByteArray_create_with_data(jsonUtf8, lengthBytes);
    if (!bytes)
        return false;
    XJsonParseError_init(&parseError);
    document = XJsonDocument_fromJson_ex(bytes, &parseError);
    XClassDelete(bytes);
    if (!document)
        return false;
    root = XJsonDocument_object(document);
    ok = root != NULL;
    if (ok)
        ok = XUpgradeManifest_getInt(root, "schema", true, &number);
    if (ok && number != 2)
        ok = false;
    if (ok)
        out->m_schema = 2u;
    if (ok)
        ok = XUpgradeManifest_getString(root, "channel", out->m_channel,
                                        XUPGRADE_CHANNEL_MAX, true,
                                        XUpgradeManifestCopy_Token);
    if (ok)
        ok = XUpgradeManifest_fillCatalogReleases(out, root);
    XClassDelete(document);
    if (!ok)
        XMemset(out, 0, sizeof(*out));
    return ok;
}

#endif /* XUPGRADE_ON */
