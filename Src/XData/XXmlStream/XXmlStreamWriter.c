/******************************************************************************
 * @file       XXmlStreamWriter.c
 * @brief      XXmlStreamWriter 的 XML 写入实现，行为参考 Qt 6.8 QXmlStreamWriter。
 * @author     XinYueC
 * @note       输出 UTF-8 编码的 XML，并支持写入内存缓冲区或关联设备。
 ******************************************************************************/
#include "XXmlStreamWriter.h"
#include "XString.h"
#include "XByteArray.h"
#include "XFileDevice.h"
#include "XMemory.h"
#include <stdlib.h> /* XClass 宏使用 ISO C exit 声明；XML 本身不调用平台 API。 */
#include <string.h>

/* ============================================================================
 * 默认配置
 * ============================================================================ */

/** @brief 自动缩进使用的默认缩进宽度。 */
#define DEFAULT_INDENT 4

typedef struct XmlWriterNamespaceBinding
{
    XString* m_prefix;
    XString* m_namespaceUri;
} XmlWriterNamespaceBinding;

/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/** @brief 关闭当前开始标签，按需写入 `>` 或 `/>`。 */
static void close_start_element(XXmlStreamWriter* self, bool empty);

/** @brief 按当前嵌套层级写入自动格式化缩进。 */
static void write_indent(XXmlStreamWriter* self);

/** @brief 将指定长度的 UTF-8 原始字节写入输出。 */
static void write_raw(XXmlStreamWriter* self, const char* data, size_t len);

/** @brief 写入单个字节到缓冲区和关联设备 */
static void write_byte(XXmlStreamWriter* self, uint8_t value);

/** @brief 将以空字符结尾的 UTF-8 字符串写入输出。 */
static void write_raw_str(XXmlStreamWriter* self, const char* str);

/** @brief 写入 XML 文本并转义保留字符。 */
static bool write_escaped(XXmlStreamWriter* self, const char* text, bool isAttribute);

/** @brief 写入开始元素标签并更新元素栈状态。 */
static void write_start_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

/** @brief 写入空元素标签并标记待关闭状态。 */
static void write_empty_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

static bool is_valid_writer_xml(const char* text);
static bool is_valid_writer_name(const char* name, bool qualified);
static void clear_namespace_bindings(XXmlStreamWriter* self);
static bool append_namespace_binding(XXmlStreamWriter* self, const char* prefix,
                                     const char* namespaceUri);
static const char* namespace_for_prefix(const XXmlStreamWriter* self, const char* prefix);
static const char* prefix_for_namespace(const XXmlStreamWriter* self, const char* namespaceUri);
static bool write_namespace_utf8_impl(XXmlStreamWriter* self, const char* namespaceUri,
                                      const char* prefix);
static void restore_namespace_scope(XXmlStreamWriter* self, int scopeStart);
static bool push_element_state(XXmlStreamWriter* self, const char* qualifiedName);
static void pop_element_state(XXmlStreamWriter* self);

static bool decode_writer_utf8(const char* ptr, const char* end,
                               uint32_t* codepoint, size_t* length)
{
    if (!ptr || ptr >= end || !codepoint || !length) return false;
    const uint8_t* bytes = (const uint8_t*)ptr;
    uint32_t value;
    size_t count;
    if (bytes[0] <= 0x7fU) {
        value = bytes[0];
        count = 1;
    } else if (bytes[0] >= 0xc2U && bytes[0] <= 0xdfU) {
        value = bytes[0] & 0x1fU;
        count = 2;
    } else if (bytes[0] >= 0xe0U && bytes[0] <= 0xefU) {
        value = bytes[0] & 0x0fU;
        count = 3;
    } else if (bytes[0] >= 0xf0U && bytes[0] <= 0xf4U) {
        value = bytes[0] & 0x07U;
        count = 4;
    } else {
        return false;
    }
    if ((size_t)(end - ptr) < count) return false;
    for (size_t i = 1; i < count; ++i) {
        if ((bytes[i] & 0xc0U) != 0x80U) return false;
        value = (value << 6) | (bytes[i] & 0x3fU);
    }
    if ((count == 2 && value < 0x80U) ||
        (count == 3 && value < 0x800U) ||
        (count == 4 && value < 0x10000U) ||
        value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) return false;
    *codepoint = value;
    *length = count;
    return true;
}

static bool is_writer_xml_char(uint32_t codepoint)
{
    return codepoint == 0x9U || codepoint == 0xaU || codepoint == 0xdU ||
           (codepoint >= 0x20U && codepoint <= 0xd7ffU) ||
           (codepoint >= 0xe000U && codepoint <= 0xfffdU) ||
           (codepoint >= 0x10000U && codepoint <= 0x10ffffU);
}

static bool is_writer_name_start(uint32_t codepoint)
{
    return codepoint == '_' || (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 0xc0U && codepoint <= 0xd6U) ||
           (codepoint >= 0xd8U && codepoint <= 0xf6U) || (codepoint >= 0xf8U && codepoint <= 0x2ffU) ||
           (codepoint >= 0x370U && codepoint <= 0x37dU) || (codepoint >= 0x37fU && codepoint <= 0x1fffU) ||
           (codepoint >= 0x200cU && codepoint <= 0x200dU) || (codepoint >= 0x2070U && codepoint <= 0x218fU) ||
           (codepoint >= 0x2c00U && codepoint <= 0x2fefU) || (codepoint >= 0x3001U && codepoint <= 0xd7ffU) ||
           (codepoint >= 0xf900U && codepoint <= 0xfdcfU) || (codepoint >= 0xfdf0U && codepoint <= 0xfffdU) ||
           (codepoint >= 0x10000U && codepoint <= 0xeffffU);
}

static bool is_writer_name_char(uint32_t codepoint)
{
    return is_writer_name_start(codepoint) || (codepoint >= '0' && codepoint <= '9') ||
           codepoint == '-' || codepoint == '.' || codepoint == 0xb7U ||
           (codepoint >= 0x300U && codepoint <= 0x36fU) ||
           (codepoint >= 0x203fU && codepoint <= 0x2040U);
}

static bool is_valid_writer_xml(const char* text)
{
    if (!text) return false;
    const char* ptr = text;
    const char* end = text + strlen(text);
    while (ptr < end) {
        uint32_t codepoint;
        size_t length;
        if (!decode_writer_utf8(ptr, end, &codepoint, &length) ||
            !is_writer_xml_char(codepoint)) return false;
        ptr += length;
    }
    return true;
}

static bool writer_ascii_equals_ignore_case(const char* left, const char* right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        char leftChar = *left++;
        char rightChar = *right++;
        if (leftChar >= 'A' && leftChar <= 'Z') leftChar = (char)(leftChar + ('a' - 'A'));
        if (rightChar >= 'A' && rightChar <= 'Z') rightChar = (char)(rightChar + ('a' - 'A'));
        if (leftChar != rightChar) return false;
    }
    return *left == '\0' && *right == '\0';
}

static void make_writer_prefix(char* output, size_t capacity, unsigned int number)
{
    if (!output || capacity < 3) return;
    char digits[sizeof(unsigned int) * 3 + 2];
    size_t digitCount = 0;
    if (number == 0) {
        digits[digitCount++] = '0';
    } else {
        while (number > 0 && digitCount < sizeof(digits)) {
            digits[digitCount++] = (char)('0' + (number % 10U));
            number /= 10U;
        }
    }
    if (digitCount + 2 > capacity) {
        output[0] = '\0';
        return;
    }
    output[0] = 'n';
    for (size_t i = 0; i < digitCount; ++i)
        output[i + 1] = digits[digitCount - i - 1];
    output[digitCount + 1] = '\0';
}

static bool is_valid_writer_name_part(const char* name, size_t length)
{
    if (!name || length == 0) return false;
    const char* ptr = name;
    const char* end = name + length;
    uint32_t codepoint;
    size_t byteLength;
    if (!decode_writer_utf8(ptr, end, &codepoint, &byteLength) ||
        !is_writer_name_start(codepoint)) return false;
    ptr += byteLength;
    while (ptr < end) {
        if (!decode_writer_utf8(ptr, end, &codepoint, &byteLength) ||
            !is_writer_name_char(codepoint)) return false;
        ptr += byteLength;
    }
    return true;
}

static bool is_valid_writer_name(const char* name, bool qualified)
{
    if (!name || !*name) return false;
    const char* firstColon = strchr(name, ':');
    if (!qualified && firstColon) return false;
    if (!firstColon) return is_valid_writer_name_part(name, strlen(name));
    if (strchr(firstColon + 1, ':') || firstColon == name || !firstColon[1]) return false;
    return is_valid_writer_name_part(name, (size_t)(firstColon - name)) &&
           is_valid_writer_name_part(firstColon + 1, strlen(firstColon + 1));
}

static XmlWriterNamespaceBinding* writer_namespace_bindings(const XXmlStreamWriter* self)
{
    return self ? (XmlWriterNamespaceBinding*)self->m_namespaceBindings : NULL;
}

static void clear_namespace_bindings(XXmlStreamWriter* self)
{
    if (!self) return;
    XmlWriterNamespaceBinding* bindings = writer_namespace_bindings(self);
    for (int i = 0; bindings && i < self->m_namespaceBindingCount; ++i) {
        XString_delete_base(bindings[i].m_prefix);
        XString_delete_base(bindings[i].m_namespaceUri);
        bindings[i].m_prefix = NULL;
        bindings[i].m_namespaceUri = NULL;
    }
    if (bindings) XFree_System(bindings);
    if (self->m_namespaceScopeStack) XFree_System(self->m_namespaceScopeStack);
    self->m_namespaceBindings = NULL;
    self->m_namespaceBindingCount = 0;
    self->m_namespaceBindingCapacity = 0;
    self->m_pendingNamespaceBindingCount = 0;
    self->m_namespaceScopeStack = NULL;
    self->m_namespaceScopeStackCapacity = 0;
}

static bool append_namespace_binding(XXmlStreamWriter* self, const char* prefix,
                                     const char* namespaceUri)
{
    if (!self || !prefix || !namespaceUri) return false;
    if (self->m_namespaceBindingCount >= self->m_namespaceBindingCapacity) {
        int capacity = self->m_namespaceBindingCapacity ? self->m_namespaceBindingCapacity * 2 : 8;
        XmlWriterNamespaceBinding* bindings = (XmlWriterNamespaceBinding*)XRealloc_System(
            self->m_namespaceBindings, (size_t)capacity * sizeof(XmlWriterNamespaceBinding));
        if (!bindings) return false;
        memset(bindings + self->m_namespaceBindingCapacity, 0,
               (size_t)(capacity - self->m_namespaceBindingCapacity) * sizeof(XmlWriterNamespaceBinding));
        self->m_namespaceBindings = bindings;
        self->m_namespaceBindingCapacity = capacity;
    }
    XmlWriterNamespaceBinding* bindings = writer_namespace_bindings(self);
    XmlWriterNamespaceBinding* binding = &bindings[self->m_namespaceBindingCount];
    binding->m_prefix = XString_create_utf8(prefix);
    binding->m_namespaceUri = XString_create_utf8(namespaceUri);
    if (!binding->m_prefix || !binding->m_namespaceUri) {
        XString_delete_base(binding->m_prefix);
        XString_delete_base(binding->m_namespaceUri);
        binding->m_prefix = NULL;
        binding->m_namespaceUri = NULL;
        return false;
    }
    ++self->m_namespaceBindingCount;
    return true;
}

static const char* namespace_for_prefix(const XXmlStreamWriter* self, const char* prefix)
{
    if (!self || !prefix) return NULL;
    XmlWriterNamespaceBinding* bindings = writer_namespace_bindings(self);
    for (int i = self->m_namespaceBindingCount - 1; bindings && i >= 0; --i) {
        const char* candidate = XString_toUtf8(bindings[i].m_prefix);
        if (candidate && strcmp(candidate, prefix) == 0)
            return XString_toUtf8(bindings[i].m_namespaceUri);
    }
    return NULL;
}

static const char* prefix_for_namespace(const XXmlStreamWriter* self, const char* namespaceUri)
{
    if (!self || !namespaceUri) return NULL;
    XmlWriterNamespaceBinding* bindings = writer_namespace_bindings(self);
    for (int i = self->m_namespaceBindingCount - 1; bindings && i >= 0; --i) {
        const char* candidate = XString_toUtf8(bindings[i].m_namespaceUri);
        if (candidate && strcmp(candidate, namespaceUri) == 0)
            return XString_toUtf8(bindings[i].m_prefix);
    }
    return NULL;
}

static void restore_namespace_scope(XXmlStreamWriter* self, int scopeStart)
{
    if (!self) return;
    XmlWriterNamespaceBinding* bindings = writer_namespace_bindings(self);
    while (self->m_namespaceBindingCount > scopeStart) {
        XmlWriterNamespaceBinding* binding = &bindings[self->m_namespaceBindingCount - 1];
        XString_delete_base(binding->m_prefix);
        XString_delete_base(binding->m_namespaceUri);
        binding->m_prefix = NULL;
        binding->m_namespaceUri = NULL;
        --self->m_namespaceBindingCount;
    }
}

static bool push_element_state(XXmlStreamWriter* self, const char* qualifiedName)
{
    if (!self || !qualifiedName) return false;
    if (self->m_elementNameStackSize >= self->m_elementNameStackCapacity) {
        int capacity = self->m_elementNameStackCapacity ? self->m_elementNameStackCapacity * 2 : 16;
        XString** stack = (XString**)XRealloc_System(
            self->m_elementNameStack, (size_t)capacity * sizeof(XString*));
        if (!stack) return false;
        memset(stack + self->m_elementNameStackCapacity, 0,
               (size_t)(capacity - self->m_elementNameStackCapacity) * sizeof(XString*));
        self->m_elementNameStack = stack;
        self->m_elementNameStackCapacity = capacity;
    }
    if (self->m_elementStack >= self->m_namespaceScopeStackCapacity) {
        int capacity = self->m_namespaceScopeStackCapacity ? self->m_namespaceScopeStackCapacity * 2 : 16;
        int* stack = (int*)XRealloc_System(self->m_namespaceScopeStack, (size_t)capacity * sizeof(int));
        if (!stack) return false;
        self->m_namespaceScopeStack = stack;
        self->m_namespaceScopeStackCapacity = capacity;
    }
    XString* elementName = XString_create_utf8(qualifiedName);
    if (!elementName) return false;
    self->m_elementNameStack[self->m_elementNameStackSize++] = elementName;
    int scopeStart = self->m_namespaceBindingCount - self->m_pendingNamespaceBindingCount;
    if (scopeStart < 0) scopeStart = 0;
    self->m_namespaceScopeStack[self->m_elementStack] = scopeStart;
    return true;
}

static void pop_element_state(XXmlStreamWriter* self)
{
    if (!self || self->m_elementStack <= 0) return;
    int depth = self->m_elementStack - 1;
    int scopeStart = self->m_namespaceScopeStack ? self->m_namespaceScopeStack[depth] : 0;
    restore_namespace_scope(self, scopeStart);
    if (self->m_elementNameStackSize > 0) {
        --self->m_elementNameStackSize;
        XString_delete_base(self->m_elementNameStack[self->m_elementNameStackSize]);
        self->m_elementNameStack[self->m_elementNameStackSize] = NULL;
    }
    --self->m_elementStack;
}

/* ============================================================================
 * 生命周期和复制移动
 * ============================================================================ */

/**
 * @brief      释放 XXmlStreamWriter 及其拥有的资源。
 * @param obj  待释放的 writer 对象。
 */
static void VXXmlStreamWriter_deinit(XXmlStreamWriter* obj)
{
    if (ISNULL(obj, "XXmlStreamWriter")) return;

    /* ========== 释放元素名栈 ========== */
    if (obj->m_elementNameStack) {
        for (int i = 0; i < obj->m_elementNameStackSize; i++) {
            if (obj->m_elementNameStack[i]) {
                XString_delete_base(obj->m_elementNameStack[i]);
                obj->m_elementNameStack[i] = NULL;
            }
        }
        XFree_System(obj->m_elementNameStack);
        obj->m_elementNameStack = NULL;
        obj->m_elementNameStackSize = 0;
        obj->m_elementNameStackCapacity = 0;
    }

    /* ========== 释放输出缓冲区 ========== */
    if (obj->m_buffer) {
        XByteArray_delete_base(obj->m_buffer);
        obj->m_buffer = NULL;
    }
    
    /* ========== 释放设备字符串 ========== */
    if (obj->m_deviceString) {
        XString_delete_base(obj->m_deviceString);
        obj->m_deviceString = NULL;
    }
    clear_namespace_bindings(obj);
    
    /* ========== 调用父类 deinit ========== */
    XClass_Deinit_Parent(XClass, obj);
}

/**
 * @brief      深拷贝 XXmlStreamWriter 的状态和缓冲区。
 * @param obj  拷贝目标对象。
 * @param src  拷贝源对象。
 */
static void VXXmlStreamWriter_copy(XXmlStreamWriter* obj, const XXmlStreamWriter* src)
{
    if (ISNULL(obj, "XXmlStreamWriter") || ISNULL(src, "XXmlStreamWriter") || obj == src)
        return;

    /* 目标未 init 则自动 init，让 copy_base 可安全用于栈对象。 */
    if (XClassIsVtableNull(obj)) {
        XXmlStreamWriter_init(obj);
    }
    if (XClassIsVtableNull(src)) return;

    /* ========== 释放目标对象已有资源 ========== */
    if (obj->m_elementNameStack) {
        for (int i = 0; i < obj->m_elementNameStackSize; i++) {
            if (obj->m_elementNameStack[i]) {
                XString_delete_base(obj->m_elementNameStack[i]);
                obj->m_elementNameStack[i] = NULL;
            }
        }
        XFree_System(obj->m_elementNameStack);
    }
    if (obj->m_buffer) {
        XByteArray_delete_base(obj->m_buffer);
    }
    if (obj->m_deviceString) {
        XString_delete_base(obj->m_deviceString);
    }
    clear_namespace_bindings(obj);

    /* ========== 复制元素名栈 ========== */
    obj->m_elementNameStack = NULL;
    obj->m_elementNameStackSize = 0;
    obj->m_elementNameStackCapacity = 0;
    if (src->m_elementNameStack && src->m_elementNameStackSize > 0) {
        obj->m_elementNameStack = (XString**)XMalloc_System(src->m_elementNameStackCapacity * sizeof(XString*));
        if (obj->m_elementNameStack) {
            obj->m_elementNameStackCapacity = src->m_elementNameStackCapacity;
            for (int i = 0; i < src->m_elementNameStackSize; i++) {
                if (src->m_elementNameStack[i]) {
                    obj->m_elementNameStack[i] = XString_create_copy(src->m_elementNameStack[i]);
                    obj->m_elementNameStackSize++;
                }
            }
        }
    }
    
    /* ========== 复制缓冲区 ========== */
    if (src->m_buffer) {
        obj->m_buffer = XByteArray_create_copy(src->m_buffer);
    } else {
        obj->m_buffer = NULL;
    }
    
    /* ========== 复制设备字符串 ========== */
    if (src->m_deviceString) {
        obj->m_deviceString = XString_create_copy(src->m_deviceString);
    } else {
        obj->m_deviceString = NULL;
    }
    
    /* ========== 复制其他成员 ========== */
    obj->m_autoFormatting = src->m_autoFormatting;
    obj->m_autoFormattingIndent = src->m_autoFormattingIndent;
    obj->m_elementStack = src->m_elementStack;
    obj->m_hasError = src->m_hasError;
    obj->m_inStartElement = src->m_inStartElement;
    obj->m_pendingEmptyElement = src->m_pendingEmptyElement;
    obj->m_namespacePrefixCounter = src->m_namespacePrefixCounter;
    obj->m_device = src->m_device;
    obj->m_externalBuffer = src->m_externalBuffer;
    obj->m_externalString = src->m_externalString;

    if (src->m_namespaceBindingCapacity > 0) {
        XmlWriterNamespaceBinding* srcBindings = writer_namespace_bindings(src);
        XmlWriterNamespaceBinding* bindings = (XmlWriterNamespaceBinding*)XMalloc_System(
            (size_t)src->m_namespaceBindingCapacity * sizeof(XmlWriterNamespaceBinding));
        if (bindings) {
            memset(bindings, 0, (size_t)src->m_namespaceBindingCapacity * sizeof(XmlWriterNamespaceBinding));
            obj->m_namespaceBindings = bindings;
            obj->m_namespaceBindingCapacity = src->m_namespaceBindingCapacity;
            for (int i = 0; i < src->m_namespaceBindingCount; ++i) {
                bindings[i].m_prefix = XString_create_copy(srcBindings[i].m_prefix);
                bindings[i].m_namespaceUri = XString_create_copy(srcBindings[i].m_namespaceUri);
            }
            obj->m_namespaceBindingCount = src->m_namespaceBindingCount;
            obj->m_pendingNamespaceBindingCount = src->m_pendingNamespaceBindingCount;
        }
    }
    if (src->m_namespaceScopeStackCapacity > 0) {
        obj->m_namespaceScopeStack = (int*)XMalloc_System(
            (size_t)src->m_namespaceScopeStackCapacity * sizeof(int));
        if (obj->m_namespaceScopeStack) {
            memcpy(obj->m_namespaceScopeStack, src->m_namespaceScopeStack,
                   (size_t)src->m_namespaceScopeStackCapacity * sizeof(int));
            obj->m_namespaceScopeStackCapacity = src->m_namespaceScopeStackCapacity;
        }
    }
}
static void VXXmlStreamWriter_move(XXmlStreamWriter* obj, XXmlStreamWriter* src)
{
    if (ISNULL(obj, "XXmlStreamWriter") || ISNULL(src, "XXmlStreamWriter") || obj == src)
        return;

    /* 目标未 init 则自动 init，让 move_base 可安全用于栈对象。 */
    if (XClassIsVtableNull(obj)) {
        XXmlStreamWriter_init(obj);
    }
    if (XClassIsVtableNull(src)) return;

    /* ========== 释放目标对象已有资源 ========== */
    if (obj->m_elementNameStack) {
        for (int i = 0; i < obj->m_elementNameStackSize; i++) {
            if (obj->m_elementNameStack[i]) {
                XString_delete_base(obj->m_elementNameStack[i]);
                obj->m_elementNameStack[i] = NULL;
            }
        }
        XFree_System(obj->m_elementNameStack);
    }
    if (obj->m_buffer) {
        XByteArray_delete_base(obj->m_buffer);
    }
    if (obj->m_deviceString) {
        XString_delete_base(obj->m_deviceString);
    }
    clear_namespace_bindings(obj);

    /* ========== 转移所有权 ========== */
    obj->m_buffer = src->m_buffer;
    obj->m_deviceString = src->m_deviceString;
    obj->m_externalBuffer = src->m_externalBuffer;
    obj->m_externalString = src->m_externalString;
    obj->m_device = src->m_device;
    obj->m_autoFormatting = src->m_autoFormatting;
    obj->m_autoFormattingIndent = src->m_autoFormattingIndent;
    obj->m_elementStack = src->m_elementStack;
    obj->m_hasError = src->m_hasError;
    obj->m_inStartElement = src->m_inStartElement;
    obj->m_pendingEmptyElement = src->m_pendingEmptyElement;
    obj->m_namespacePrefixCounter = src->m_namespacePrefixCounter;
    obj->m_elementNameStack = src->m_elementNameStack;
    obj->m_elementNameStackSize = src->m_elementNameStackSize;
    obj->m_elementNameStackCapacity = src->m_elementNameStackCapacity;
    obj->m_namespaceBindings = src->m_namespaceBindings;
    obj->m_namespaceBindingCount = src->m_namespaceBindingCount;
    obj->m_namespaceBindingCapacity = src->m_namespaceBindingCapacity;
    obj->m_pendingNamespaceBindingCount = src->m_pendingNamespaceBindingCount;
    obj->m_namespaceScopeStack = src->m_namespaceScopeStack;
    obj->m_namespaceScopeStackCapacity = src->m_namespaceScopeStackCapacity;
    
    /* 将源对象恢复为可安全释放的空状态。 */
    src->m_buffer = NULL;
    src->m_deviceString = NULL;
    src->m_externalBuffer = NULL;
    src->m_externalString = NULL;
    src->m_device = NULL;
    src->m_autoFormatting = false;
    src->m_autoFormattingIndent = DEFAULT_INDENT;
    src->m_elementStack = 0;
    src->m_hasError = false;
    src->m_inStartElement = false;
    src->m_pendingEmptyElement = false;
    src->m_namespacePrefixCounter = 0;
    src->m_elementNameStack = NULL;
    src->m_elementNameStackSize = 0;
    src->m_elementNameStackCapacity = 0;
    src->m_namespaceBindings = NULL;
    src->m_namespaceBindingCount = 0;
    src->m_namespaceBindingCapacity = 0;
    src->m_pendingNamespaceBindingCount = 0;
    src->m_namespaceScopeStack = NULL;
    src->m_namespaceScopeStackCapacity = 0;
}

/* ============================================================================
 * 输出辅助函数
 * ============================================================================ */

/**
 * @brief      将指定长度的 UTF-8 字节写入缓冲区和关联设备。
 * @param self writer 对象。
 * @param data 待写入的字节数据。
 * @param len  数据长度（字节数）。
 */
static void write_raw(XXmlStreamWriter* self, const char* data, size_t len)
{
    if (!self || !data || !self->m_buffer) return;
    for (size_t i = 0; i < len; i++) {
        XByteArray_push_back_1(self->m_buffer, (uint8_t)data[i]);
    }
    if (self->m_externalBuffer && self->m_externalBuffer != self->m_buffer) {
        for (size_t i = 0; i < len; i++) {
            if (!XByteArray_push_back_1(self->m_externalBuffer, (uint8_t)data[i])) {
                self->m_hasError = true;
                break;
            }
        }
    }
    if (self->m_externalString &&
        !XString_append_with_length_utf8(self->m_externalString, data, len))
        self->m_hasError = true;
    if (self->m_device && XIODevice_write_1(self->m_device, data, (int64_t)len) != (int64_t)len)
        self->m_hasError = true;
}

static void write_byte(XXmlStreamWriter* self, uint8_t value)
{
    write_raw(self, (const char*)&value, 1);
}

/**
 * @brief      将以空字符结尾的 UTF-8 字符串写入输出。
 * @param self writer 对象。
 * @param str  待写入的字符串。
 */
static void write_raw_str(XXmlStreamWriter* self, const char* str)
{
    if (!self || !str || !self->m_buffer) return;
    write_raw(self, str, strlen(str));
}

/**
 * @brief      按嵌套层级写入自动格式化缩进。
 * @param self writer 对象。
 */
static void write_indent(XXmlStreamWriter* self)
{
    if (!self || !self->m_autoFormatting || !self->m_buffer) return;
    
    /* 自动格式化时先换行。 */
    write_byte(self, (uint8_t)'\n');
    
    /* 负缩进表示使用制表符，否则使用空格。 */
    int indent = self->m_elementStack * self->m_autoFormattingIndent;
    uint8_t indentCharacter = indent < 0 ? (uint8_t)'\t' : (uint8_t)' ';
    if (indent < 0) indent = -indent;
    for (int i = 0; i < indent; i++) {
        write_byte(self, indentCharacter);
    }
}

/**
 * @brief      关闭当前开始标签并维护元素栈。
 * @param self  writer 对象。
 * @param empty 是否强制使用空元素形式 `/>`。
 */
static void close_start_element(XXmlStreamWriter* self, bool empty)
{
    if (!self || !self->m_inStartElement) return;

    bool closeAsEmpty = empty || self->m_pendingEmptyElement;
    if (closeAsEmpty) {
        /* ========== 写入 /> ========== */
        write_byte(self, (uint8_t)'/');
        write_byte(self, (uint8_t)'>');
        pop_element_state(self);
    } else {
        /* ========== 写入 > ========== */
        write_byte(self, (uint8_t)'>');
    }
    self->m_inStartElement = false;
    self->m_pendingEmptyElement = false;
}

/**
 * @brief      写入 XML 文本并转义特殊字符。
 * @param self        writer 对象。
 * @param text        待写入的文本。
 * @param isAttribute 是否按 XML 属性值规则转义换行、回车和制表符。
 */
static bool write_escaped(XXmlStreamWriter* self, const char* text, bool isAttribute)
{
    if (!self || !text || !self->m_buffer) return false;
    const char* p = text;
    const char* end = text + strlen(text);
    while (p < end) {
        uint32_t codepoint;
        size_t length;
        if (!decode_writer_utf8(p, end, &codepoint, &length) ||
            !is_writer_xml_char(codepoint)) {
            self->m_hasError = true;
            return false;
        }
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '&':
                write_raw_str(self, "&amp;");
                break;
            case '<':
                write_raw_str(self, "&lt;");
                break;
            case '>':
                write_raw_str(self, "&gt;");
                break;
            case '"':
                if (isAttribute) {
                    write_raw_str(self, "&quot;");
                } else {
                    write_byte(self, c);
                }
                break;
            case '\'':
                if (isAttribute) {
                    write_raw_str(self, "&apos;");
                } else {
                    write_byte(self, c);
                }
                break;
            case '\n':
                if (isAttribute) {
                    write_raw_str(self, "&#10;");
                } else {
                    write_byte(self, c);
                }
                break;
            case '\r':
                if (isAttribute) {
                    write_raw_str(self, "&#13;");
                } else {
                    write_byte(self, c);
                }
                break;
            case '\t':
                if (isAttribute) {
                    write_raw_str(self, "&#9;");
                } else {
                    write_byte(self, c);
                }
                break;
            default:
                /* 多字节 UTF-8 必须整体写入，不能只写首字节。 */
                write_raw(self, p, length);
                break;
        }
        p += length;
    }
    return true;
}

/**
 * @brief      写入开始元素标签并更新元素栈。
 * @param self         writer 对象。
 * @param namespaceUri 命名空间前缀，可为 NULL。
 * @param name         元素名称。
 */
static void write_start_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    if (!self || !name || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if ((namespaceUri && namespaceUri[0] && !is_valid_writer_name(namespaceUri, false)) ||
        !is_valid_writer_name(name, namespaceUri && namespaceUri[0] ? false : true)) {
        self->m_hasError = true;
        return;
    }

    XString qualifiedName;
    XString_init(&qualifiedName);
    if (namespaceUri && namespaceUri[0]) {
        if (!XString_append_utf8(&qualifiedName, namespaceUri) ||
            !XString_append_utf8(&qualifiedName, ":") ||
            !XString_append_utf8(&qualifiedName, name)) {
            XString_deinit_base(&qualifiedName);
            self->m_hasError = true;
            return;
        }
    } else if (!XString_append_utf8(&qualifiedName, name)) {
        XString_deinit_base(&qualifiedName);
        self->m_hasError = true;
        return;
    }
    /* 先关闭上一个尚未完成的开始标签。 */
    close_start_element(self, false);
    const char* qualifiedUtf8 = XString_toUtf8(&qualifiedName);
    if (!qualifiedUtf8 || !push_element_state(self, qualifiedUtf8)) {
        XString_deinit_base(&qualifiedName);
        self->m_hasError = true;
        return;
    }
    
    /* 自动格式化缩进。 */
    write_indent(self);
    
    /* 写入开始标签左尖括号。 */
    write_byte(self, (uint8_t)'<');
    
    /* 有命名空间前缀时写入前缀和分隔符。 */
    if (namespaceUri && namespaceUri[0]) {
        write_raw_str(self, namespaceUri);
        write_byte(self, (uint8_t)':');
    }
    
    /* 写入元素名称。 */
    write_raw_str(self, name);

    /* 将此前为“下一个元素”登记的声明写入当前开始标签。 */
    if (self->m_pendingNamespaceBindingCount > 0) {
        int firstPending = self->m_namespaceBindingCount - self->m_pendingNamespaceBindingCount;
        XmlWriterNamespaceBinding* bindings = writer_namespace_bindings(self);
        for (int i = firstPending; bindings && i < self->m_namespaceBindingCount; ++i) {
            const char* pendingPrefix = XString_toUtf8(bindings[i].m_prefix);
            const char* pendingUri = XString_toUtf8(bindings[i].m_namespaceUri);
            write_byte(self, (uint8_t)' ');
            write_raw_str(self, "xmlns");
            if (pendingPrefix && pendingPrefix[0]) {
                write_byte(self, (uint8_t)':');
                write_raw_str(self, pendingPrefix);
            }
            write_raw_str(self, "=\"");
            write_escaped(self, pendingUri, true);
            write_byte(self, (uint8_t)'\"');
        }
        self->m_pendingNamespaceBindingCount = 0;
    }

    /* ========== 设置开始标签状态 ========== */
    self->m_inStartElement = true;
    self->m_pendingEmptyElement = false;
    self->m_elementStack++;
    XString_deinit_base(&qualifiedName);
}

/**
 * @brief      写入空元素标签并标记待关闭状态。
 * @param self         writer 对象。
 * @param namespaceUri 命名空间前缀，可为 NULL。
 * @param name         元素名称。
 */
static void write_empty_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    write_start_element_impl(self, namespaceUri, name);
    if (self && !self->m_hasError) self->m_pendingEmptyElement = true;
}

static bool write_namespace_utf8_impl(XXmlStreamWriter* self, const char* namespaceUri,
                                      const char* prefix)
{
    if (!self || !namespaceUri || !prefix || !self->m_buffer ||
        !is_valid_writer_xml(namespaceUri) ||
        (prefix[0] && !is_valid_writer_name(prefix, false)) ||
        strcmp(prefix, "xmlns") == 0 ||
        (strcmp(prefix, "xml") == 0 && strcmp(namespaceUri, "http://www.w3.org/XML/1998/namespace") != 0) ||
        (strcmp(namespaceUri, "http://www.w3.org/XML/1998/namespace") == 0 &&
         strcmp(prefix, "xml") != 0)) {
        if (self) self->m_hasError = true;
        return false;
    }
    if (strcmp(namespaceUri, "http://www.w3.org/2000/xmlns/") == 0) {
        self->m_hasError = true;
        return false;
    }

    int pendingCount = self->m_pendingNamespaceBindingCount;
    int scopeStart = self->m_inStartElement && self->m_namespaceScopeStack && self->m_elementStack > 0
        ? self->m_namespaceScopeStack[self->m_elementStack - 1]
        : self->m_namespaceBindingCount - pendingCount;
    if (scopeStart < 0) scopeStart = 0;
    XmlWriterNamespaceBinding* bindings = writer_namespace_bindings(self);
    for (int i = self->m_namespaceBindingCount - 1; bindings && i >= scopeStart; --i) {
        const char* boundPrefix = XString_toUtf8(bindings[i].m_prefix);
        const char* boundUri = XString_toUtf8(bindings[i].m_namespaceUri);
        if (boundPrefix && strcmp(boundPrefix, prefix) == 0) {
            if (boundUri && strcmp(boundUri, namespaceUri) == 0) return true;
            self->m_hasError = true;
            return false;
        }
    }
    const char* inheritedUri = namespace_for_prefix(self, prefix);
    if (inheritedUri && strcmp(inheritedUri, namespaceUri) == 0) return true;
    if (!append_namespace_binding(self, prefix, namespaceUri)) {
        self->m_hasError = true;
        return false;
    }

    if (!self->m_inStartElement) {
        ++self->m_pendingNamespaceBindingCount;
        return true;
    }

    write_byte(self, (uint8_t)' ');
    write_raw_str(self, "xmlns");
    if (prefix[0]) {
        write_byte(self, (uint8_t)':');
        write_raw_str(self, prefix);
    }
    write_raw_str(self, "=\"");
    write_escaped(self, namespaceUri, true);
    write_byte(self, (uint8_t)'\"');
    return !self->m_hasError;
}

/* ============================================================================
 * 类初始化
 * ============================================================================ */

/**
 * @brief      初始化 XXmlStreamWriter 的虚函数表。
 * @return     已初始化的类虚函数表指针。
 */
XVtable* XXmlStreamWriter_class_init(void)
{
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_SIZE)
	XCLASS_SET_CLASS_NAME_DEFAULT("XXmlStreamWriter");
    
    /* 继承 XClass 的基础行为。 */
    XVTABLE_INHERIT_XCLASS(XClass);
    
    /* 注册生命周期和复制移动重载。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXXmlStreamWriter_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXXmlStreamWriter_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXXmlStreamWriter_move);
    
    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * 创建和初始化
 * ============================================================================ */

/**
 * @brief      创建一个动态分配的 XXmlStreamWriter。
 * @return     成功返回对象指针，内存不足时返回 NULL。
 */
XXmlStreamWriter* XXmlStreamWriter_create(void)
{
    XXmlStreamWriter* self = (XXmlStreamWriter*)XMalloc_System(sizeof(XXmlStreamWriter));
    if (!self) return NULL;

    XXmlStreamWriter_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XXmlStreamWriter* XXmlStreamWriter_create_copy(const XXmlStreamWriter* other)
{
    if (!other) return NULL;
    XXmlStreamWriter* self = XXmlStreamWriter_create();
    if (!self) return NULL;
    XXmlStreamWriter_copy_base(self, other);
    return self;
}

XXmlStreamWriter* XXmlStreamWriter_create_move(XXmlStreamWriter* other)
{
    if (!other) return NULL;
    XXmlStreamWriter* self = XXmlStreamWriter_create();
    if (!self) return NULL;
    XXmlStreamWriter_move_base(self, other);
    return self;
}

XXmlStreamWriter* XXmlStreamWriter_create_byteArray(XByteArray* array)
{
    XXmlStreamWriter* self = XXmlStreamWriter_create();
    if (self) self->m_externalBuffer = array;
    return self;
}

XXmlStreamWriter* XXmlStreamWriter_create_string(XString* string)
{
    XXmlStreamWriter* self = XXmlStreamWriter_create();
    if (self) self->m_externalString = string;
    return self;
}

XXmlStreamWriter* XXmlStreamWriter_create_device(XIODevice* device)
{
    XXmlStreamWriter* self = XXmlStreamWriter_create();
    if (self) self->m_device = device;
    return self;
}

/**
 * @brief      初始化 XXmlStreamWriter 对象。
 * @param self 待初始化的 XXmlStreamWriter 对象。
 */
void XXmlStreamWriter_init(XXmlStreamWriter* self)
{
    if (ISNULL(self, "XXmlStreamWriter")) return;
    
    /* 清零对象字段，建立确定的初始状态。 */
    memset(self, 0, sizeof(XXmlStreamWriter));
    
    /* 初始化继承的 XClass 基类。 */
    XClass_init((XClass*)self);
    
    /* 设置 XXmlStreamWriter 对应的虚函数表。 */
    XClassSetVtable(self, XXmlStreamWriter);
    
    /* ========== 初始化元素名栈 ========== */
    self->m_elementNameStack = NULL;
    self->m_elementNameStackSize = 0;
    self->m_elementNameStackCapacity = 0;
    
    /* ========== 创建输出缓冲区 ========== */
    self->m_buffer = XByteArray_create();
    if (!self->m_buffer) {
        self->m_hasError = true;
        return;
    }
    
    /* 创建设备字符串缓存。 */
    self->m_deviceString = XString_create();
    if (!self->m_deviceString) {
        XByteArray_delete_base(self->m_buffer);
        self->m_buffer = NULL;
        self->m_hasError = true;
        return;
    }
    
    /* 初始化格式化、栈和错误状态。 */
    self->m_autoFormatting = false;
    self->m_autoFormattingIndent = DEFAULT_INDENT;
    self->m_elementStack = 0;
    self->m_hasError = false;
    self->m_inStartElement = false;
    self->m_pendingEmptyElement = false;
    self->m_namespacePrefixCounter = 0;
    self->m_device = NULL;
    self->m_externalBuffer = NULL;
    self->m_externalString = NULL;
    self->m_namespaceBindings = NULL;
    self->m_namespaceBindingCount = 0;
    self->m_namespaceBindingCapacity = 0;
    self->m_namespaceScopeStack = NULL;
    self->m_namespaceScopeStackCapacity = 0;
}


/**
 * @brief      获取当前 XML 输出内容。
 * @param self XXmlStreamWriter 对象。
 * @return     以空字符结尾的 UTF-8 字符串；对象无效时返回空字符串。
 */
const char* XXmlStreamWriter_toString(const XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) return "";
    close_start_element((XXmlStreamWriter*)self, false);

    size_t size = XByteArray_size_base((XByteArray*)self->m_buffer);
    if (size == 0) return "";

    if (self->m_deviceString) {
        const uint8_t* data = (const uint8_t*)XByteArray_data((XByteArray*)self->m_buffer);
        XString_assign_with_length_utf8(self->m_deviceString, (const char*)data, size);
        return XString_toUtf8(self->m_deviceString);
    }

    return (const char*)XByteArray_data((XByteArray*)self->m_buffer);
}

XString* XXmlStreamWriter_toString_x(const XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) return NULL;
    close_start_element((XXmlStreamWriter*)self, false);

    size_t size = XByteArray_size_base((XByteArray*)self->m_buffer);
    if (size == 0) {
        return XString_create();
    }

    const uint8_t* data = (const uint8_t*)XByteArray_data((XByteArray*)self->m_buffer);
    return XString_create_with_length_utf8((const char*)data, size);
}

/**
 * @brief      获取当前 XML 输出缓冲区。
 * @param self XXmlStreamWriter 对象。
 * @return     内部 XByteArray 指针；对象无效时返回 NULL。
 */
XByteArray* XXmlStreamWriter_toByteArray(const XXmlStreamWriter* self)
{
    if (!self) return NULL;
    close_start_element((XXmlStreamWriter*)self, false);
    return self->m_buffer;
}

/* ============================================================================
 * 格式化设置
 * ============================================================================ */

/**
 * @brief      设置是否启用自动格式化。
 * @param self   XXmlStreamWriter 对象。
 * @param enable true 启用自动换行和缩进，false 保持紧凑输出。
 */
void XXmlStreamWriter_setAutoFormatting(XXmlStreamWriter* self, bool enable)
{
    if (!self) return;
    self->m_autoFormatting = enable;
}

/**
 * @brief      查询是否启用了自动格式化。
 * @param self XXmlStreamWriter 对象。
 * @return     已启用返回 true，否则返回 false。
 */
bool XXmlStreamWriter_autoFormatting(const XXmlStreamWriter* self)
{
    if (!self) return false;
    return self->m_autoFormatting;
}

/**
 * @brief      设置自动格式化的缩进宽度。
 * @param self   XXmlStreamWriter 对象。
 * @param indent 缩进宽度；负值表示使用制表符，非负值表示空格数。
 */
void XXmlStreamWriter_setAutoFormattingIndent(XXmlStreamWriter* self, int indent)
{
    if (!self) return;
    self->m_autoFormattingIndent = indent;
}

/**
 * @brief      获取自动格式化的缩进宽度。
 * @param self XXmlStreamWriter 对象。
 * @return     当前缩进宽度；对象无效时返回默认值 4。
 */
int XXmlStreamWriter_autoFormattingIndent(const XXmlStreamWriter* self)
{
    if (!self) return DEFAULT_INDENT;
    return self->m_autoFormattingIndent;
}

/* ============================================================================
 * 文档节点
 * ============================================================================ */

/**
 * @brief      写入默认版本为 1.0 的 XML 声明。
 * @param self XXmlStreamWriter 对象。
 */
void XXmlStreamWriter_writeStartDocument(XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* 先关闭上一个尚未完成的开始标签。 */
    close_start_element(self, false);
    
    XXmlStreamWriter_writeStartDocument_ex_utf8(self, "1.0");
}

/**
 * @brief      写入文档开始声明（带版本）XString 版本
 * @param self    目标 XXmlStreamWriter 对象指针
 * @param version 版本号
 */
void XXmlStreamWriter_writeStartDocument_ex(XXmlStreamWriter* self, const XString* version)
{
    if (!self || !version || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_raw_str(self, "<?xml version=\"");
    write_raw_str(self, XString_toUtf8(version));
    if (self->m_device) write_raw_str(self, "\" encoding=\"UTF-8");
    write_raw_str(self, "\"?>");
}

/**
 * @brief      写入文档开始声明（带版本）UTF-8 版本
 * @param self    目标 XXmlStreamWriter 对象指针
 * @param version 版本号（UTF-8 编码）
 */
void XXmlStreamWriter_writeStartDocument_ex_utf8(XXmlStreamWriter* self, const char* version)
{
    if (!self || !version || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_raw_str(self, "<?xml version=\"");
    write_raw_str(self, version);
    if (self->m_device) write_raw_str(self, "\" encoding=\"UTF-8");
    write_raw_str(self, "\"?>");
}

/**
 * @brief      写入带版本和独立标志的 XML 声明。
 * @param self       XXmlStreamWriter 对象。
 * @param version    XML 版本号。
 * @param standalone 是否标记为独立文档。
 */

/**
 * @brief      写入文档开始声明（带版本和独立标志）XString 版本
 * @param self       目标 XXmlStreamWriter 对象指针
 * @param version    版本号
 * @param standalone 是否独立文档
 */
void XXmlStreamWriter_writeStartDocument_ex_2(XXmlStreamWriter* self, const XString* version, bool standalone)
{
    if (!self || !version || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_raw_str(self, "<?xml version=\"");
    write_raw_str(self, XString_toUtf8(version));
    if (self->m_device) write_raw_str(self, "\" encoding=\"UTF-8");
    write_raw_str(self, "\" standalone=\"");
    write_raw_str(self, standalone ? "yes" : "no");
    write_raw_str(self, "\"?>");
}

/**
 * @brief      写入文档开始声明（带版本和独立标志）UTF-8 版本
 * @param self       目标 XXmlStreamWriter 对象指针
 * @param version    版本号（UTF-8 编码）
 * @param standalone 是否独立文档
 */
void XXmlStreamWriter_writeStartDocument_ex_2_utf8(XXmlStreamWriter* self, const char* version, bool standalone)
{
    if (!self || !version || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_raw_str(self, "<?xml version=\"");
    write_raw_str(self, version);
    if (self->m_device) write_raw_str(self, "\" encoding=\"UTF-8");
    write_raw_str(self, "\" standalone=\"");
    write_raw_str(self, standalone ? "yes" : "no");
    write_raw_str(self, "\"?>");
}
void XXmlStreamWriter_writeEndDocument(XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* Qt 会在文档结束时关闭所有仍打开的元素。 */
    while (self->m_elementStack > 0) {
        XXmlStreamWriter_writeEndElement(self);
    }

    write_byte(self, (uint8_t)'\n');
}

/**
 * @brief      写入开始元素标签。
 * @param self          XXmlStreamWriter 对象。
 * @param qualifiedName 限定元素名称。
 */

/**
 * @brief      写入开始标签
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 */
void XXmlStreamWriter_writeStartElement(XXmlStreamWriter* self, const XString* qualifiedName)
{
    if (!self || !qualifiedName) {
        if (self) self->m_hasError = true;
        return;
    }
    write_start_element_impl(self, NULL, XString_toUtf8(qualifiedName));
}

/**
 * @brief      写入开始标签（UTF-8 版本）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名（UTF-8 编码）
 */
void XXmlStreamWriter_writeStartElement_utf8(XXmlStreamWriter* self, const char* qualifiedName)
{
    write_start_element_impl(self, NULL, qualifiedName);
}
void XXmlStreamWriter_writeStartElement_ex(XXmlStreamWriter* self, const XString* namespaceUri, const XString* name)
{
    if (!self || !name) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeStartElement_ex_utf8(
        self, namespaceUri ? XString_toUtf8(namespaceUri) : NULL, XString_toUtf8(name));
}

/**
 * @brief      写入开始标签（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 */
void XXmlStreamWriter_writeStartElement_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    if (!self || !name) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_name(name, false)) {
        self->m_hasError = true;
        return;
    }
    if (!namespaceUri || !namespaceUri[0]) {
        write_start_element_impl(self, NULL, name);
        return;
    }
    const char* prefix = prefix_for_namespace(self, namespaceUri);
    char generatedPrefix[32];
    if (!prefix) {
        do {
            ++self->m_namespacePrefixCounter;
            make_writer_prefix(generatedPrefix, sizeof(generatedPrefix),
                               self->m_namespacePrefixCounter);
        } while (namespace_for_prefix(self, generatedPrefix));
        prefix = generatedPrefix;
    }
    write_start_element_impl(self, prefix, name);
    if (!self->m_hasError) write_namespace_utf8_impl(self, namespaceUri, prefix);
}

/**
 * @brief      写入结束元素标签。
 * @param self XXmlStreamWriter 对象。
 */
void XXmlStreamWriter_writeEndElement(XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* Qt 的 writeEmptyElement 允许随后继续写属性，但不要求调用方再为
       空元素单独调用 writeEndElement；结束父元素时先收口空子元素。 */
    if (self->m_inStartElement && self->m_pendingEmptyElement) {
        close_start_element(self, true);
        if (self->m_elementStack <= 0) return;
    }
    if (self->m_inStartElement) {
        close_start_element(self, true);
        return;
    }
    
    if (self->m_elementStack <= 0) {
        self->m_hasError = true;
        return;
    }
    
    self->m_elementStack--;

    /* ========== 读取栈顶限定名并恢复命名空间作用域 ========== */
    const char* elementName = self->m_elementNameStackSize > 0
        ? XString_toUtf8(self->m_elementNameStack[self->m_elementNameStackSize - 1]) : NULL;
    
    /* ========== 写入缩进 ========== */
    write_indent(self);
    
    /* ========== 写入结束标签 </element> ========== */
    write_raw_str(self, "</");
    if (elementName) {
        write_raw_str(self, elementName);
    }
    write_raw_str(self, ">");
    
    restore_namespace_scope(self, self->m_namespaceScopeStack
        ? self->m_namespaceScopeStack[self->m_elementStack] : 0);
    if (self->m_elementNameStackSize > 0) {
        --self->m_elementNameStackSize;
        XString_delete_base(self->m_elementNameStack[self->m_elementNameStackSize]);
        self->m_elementNameStack[self->m_elementNameStackSize] = NULL;
    }
}

/**
 * @brief      写入空元素标签
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 */
void XXmlStreamWriter_writeEmptyElement(XXmlStreamWriter* self, const XString* qualifiedName)
{
    if (!self || !qualifiedName) {
        if (self) self->m_hasError = true;
        return;
    }
    write_empty_element_impl(self, NULL, XString_toUtf8(qualifiedName));
}

/**
 * @brief      写入空元素标签（UTF-8 版本）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名（UTF-8 编码）
 */
void XXmlStreamWriter_writeEmptyElement_utf8(XXmlStreamWriter* self, const char* qualifiedName)
{
    write_empty_element_impl(self, NULL, qualifiedName);
}

/**
 * @brief      写入带命名空间的空元素标签。
 * @param self         XXmlStreamWriter 对象。
 * @param namespaceUri 命名空间 URI。
 * @param name         本地元素名称。
 */

/**
 * @brief      写入空元素标签（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 */
void XXmlStreamWriter_writeEmptyElement_ex(XXmlStreamWriter* self, const XString* namespaceUri, const XString* name)
{
    if (!self || !name) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeEmptyElement_ex_utf8(
        self, namespaceUri ? XString_toUtf8(namespaceUri) : NULL, XString_toUtf8(name));
}

/**
 * @brief      写入空元素标签（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 */
void XXmlStreamWriter_writeEmptyElement_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    if (!self || !name) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_name(name, false)) {
        self->m_hasError = true;
        return;
    }
    if (!namespaceUri || !namespaceUri[0]) {
        write_empty_element_impl(self, NULL, name);
        return;
    }
    const char* prefix = prefix_for_namespace(self, namespaceUri);
    char generatedPrefix[32];
    if (!prefix) {
        do {
            ++self->m_namespacePrefixCounter;
            make_writer_prefix(generatedPrefix, sizeof(generatedPrefix),
                               self->m_namespacePrefixCounter);
        } while (namespace_for_prefix(self, generatedPrefix));
        prefix = generatedPrefix;
    }
    write_empty_element_impl(self, prefix, name);
    if (!self->m_hasError) write_namespace_utf8_impl(self, namespaceUri, prefix);
}

/**
 * @brief      写入属性
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 * @param value         属性值
 */
void XXmlStreamWriter_writeAttribute(XXmlStreamWriter* self, const XString* qualifiedName, const XString* value)
{
    if (!self || !qualifiedName || !value || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_name(XString_toUtf8(qualifiedName), true) ||
        !is_valid_writer_xml(XString_toUtf8(value))) {
        self->m_hasError = true;
        return;
    }
    write_byte(self, (uint8_t)' ');
    write_raw_str(self, XString_toUtf8(qualifiedName));
    write_raw_str(self, "=\"");
    write_escaped(self, XString_toUtf8(value), true);
    write_byte(self, (uint8_t)'"');
}

/**
 * @brief      写入属性（UTF-8 版本）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名（UTF-8 编码）
 * @param value         属性值（UTF-8 编码）
 */
void XXmlStreamWriter_writeAttribute_utf8(XXmlStreamWriter* self, const char* qualifiedName, const char* value)
{
    if (!self || !qualifiedName || !value || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_name(qualifiedName, true) || !is_valid_writer_xml(value)) {
        self->m_hasError = true;
        return;
    }
    write_byte(self, (uint8_t)' ');
    write_raw_str(self, qualifiedName);
    write_raw_str(self, "=\"");
    write_escaped(self, value, true);
    write_byte(self, (uint8_t)'"');
}

/**
 * @brief      写入属性（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 * @param value        属性值
 */
void XXmlStreamWriter_writeAttribute_ex(XXmlStreamWriter* self, const XString* namespaceUri, const XString* name, const XString* value)
{
    if (!self || !name || !value || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeAttribute_ex_utf8(self,
        namespaceUri ? XString_toUtf8(namespaceUri) : NULL,
        XString_toUtf8(name), XString_toUtf8(value));
}

/**
 * @brief      写入属性（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 * @param value        属性值（UTF-8 编码）
 */
void XXmlStreamWriter_writeAttribute_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name, const char* value)
{
    if (!self || !name || !value || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_name(name, false) ||
        (namespaceUri && namespaceUri[0] && !is_valid_writer_xml(namespaceUri))) {
        self->m_hasError = true;
        return;
    }
    if (!namespaceUri || !namespaceUri[0]) {
        XXmlStreamWriter_writeAttribute_utf8(self, name, value);
        return;
    }

    const char* prefix = prefix_for_namespace(self, namespaceUri);
    char generatedPrefix[32];
    if (!prefix || !prefix[0]) {
        do {
            ++self->m_namespacePrefixCounter;
            make_writer_prefix(generatedPrefix, sizeof(generatedPrefix),
                               self->m_namespacePrefixCounter);
        } while (namespace_for_prefix(self, generatedPrefix));
        prefix = generatedPrefix;
    }
    if (!write_namespace_utf8_impl(self, namespaceUri, prefix)) return;
    write_byte(self, (uint8_t)' ');
    write_raw_str(self, prefix);
    write_byte(self, (uint8_t)':');
    write_raw_str(self, name);
    write_raw_str(self, "=\"");
    write_escaped(self, value, true);
    write_byte(self, (uint8_t)'"');
}

/**
 * @brief      写入属性（从 XXmlStreamAttribute 对象）
 * @param self      目标 XXmlStreamWriter 对象指针
 * @param attribute 属性对象指针
 */
void XXmlStreamWriter_writeAttribute_attr(XXmlStreamWriter* self, const XXmlStreamAttribute* attribute)
{
    if (!self || !attribute) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    const char* qualifiedName = XXmlStreamAttribute_qualifiedName(attribute)
        ? XString_toUtf8(XXmlStreamAttribute_qualifiedName(attribute)) : NULL;
    const char* namespaceUri = XXmlStreamAttribute_namespaceUri(attribute)
        ? XString_toUtf8(XXmlStreamAttribute_namespaceUri(attribute)) : NULL;
    const char* name = XXmlStreamAttribute_name(attribute)
        ? XString_toUtf8(XXmlStreamAttribute_name(attribute)) : NULL;
    const char* value = XXmlStreamAttribute_value(attribute)
        ? XString_toUtf8(XXmlStreamAttribute_value(attribute)) : NULL;

    if (!value || (!qualifiedName && (!namespaceUri || !name))) {
        self->m_hasError = true;
        return;
    }
    if (qualifiedName && qualifiedName[0]) {
        XXmlStreamWriter_writeAttribute_utf8(self, qualifiedName, value);
    } else {
        XXmlStreamWriter_writeAttribute_ex_utf8(self, namespaceUri, name, value);
    }
}
void XXmlStreamWriter_writeAttributes(XXmlStreamWriter* self, const XXmlStreamAttributes* attributes)
{
    if (!self || !attributes) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* 按顺序写入属性集合中的每个属性。 */
    int count = XXmlStreamAttributes_size(attributes);
    for (int i = 0; i < count; i++) {
        const XXmlStreamAttribute* attr = XXmlStreamAttributes_at(attributes, i);
        if (attr) {
            XXmlStreamWriter_writeAttribute_attr(self, attr);
        }
    }
}

/**
 * @brief      写入字符数据。
 * @param self XXmlStreamWriter 对象。
 * @param text 要写入的字符数据。
 */

/**
 * @brief      写入字符数据
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 字符数据
 */
void XXmlStreamWriter_writeCharacters(XXmlStreamWriter* self, const XString* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_escaped(self, XString_toUtf8(text), false);
}

/**
 * @brief      写入字符数据（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 字符数据（UTF-8 编码）
 */
void XXmlStreamWriter_writeCharacters_utf8(XXmlStreamWriter* self, const char* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_escaped(self, text, false);
}

/**
 * @brief      写入 CDATA 段
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text CDATA 文本
 */
void XXmlStreamWriter_writeCDATA(XXmlStreamWriter* self, const XString* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeCDATA_utf8(self, XString_toUtf8(text));
}

/**
 * @brief      写入 CDATA 段（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text CDATA 文本（UTF-8 编码）
 */
void XXmlStreamWriter_writeCDATA_utf8(XXmlStreamWriter* self, const char* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_xml(text)) {
        self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_raw_str(self, "<![CDATA[");
    const char* part = text;
    const char* forbidden;
    while ((forbidden = strstr(part, "]]>") ) != NULL) {
        write_raw(self, part, (size_t)(forbidden - part));
        write_raw_str(self, "]]]]><![CDATA[>");
        part = forbidden + 3;
    }
    write_raw_str(self, part);
    write_raw_str(self, "]]>");
}

/**
 * @brief      写入注释
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 注释文本
 */
void XXmlStreamWriter_writeComment(XXmlStreamWriter* self, const XString* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeComment_utf8(self, XString_toUtf8(text));
}

/**
 * @brief      写入注释（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 注释文本（UTF-8 编码）
 */
void XXmlStreamWriter_writeComment_utf8(XXmlStreamWriter* self, const char* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    size_t length = strlen(text);
    if (!is_valid_writer_xml(text) || strstr(text, "--") ||
        (length > 0 && text[length - 1] == '-')) {
        self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_indent(self);
    write_raw_str(self, "<!--");
    write_raw_str(self, text);
    write_raw_str(self, "-->");
}

/**
 * @brief      写入处理指令
 * @param self   目标 XXmlStreamWriter 对象指针
 * @param target 指令目标
 * @param data   指令数据（可为 NULL）
 */
void XXmlStreamWriter_writeProcessingInstruction(XXmlStreamWriter* self, const XString* target, const XString* data)
{
    if (!self || !target || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeProcessingInstruction_utf8(
        self, XString_toUtf8(target), data ? XString_toUtf8(data) : NULL);
}

/**
 * @brief      写入处理指令（UTF-8 版本）
 * @param self   目标 XXmlStreamWriter 对象指针
 * @param target 指令目标（UTF-8 编码）
 * @param data   指令数据（UTF-8 编码，可为 NULL）
 */
void XXmlStreamWriter_writeProcessingInstruction_utf8(XXmlStreamWriter* self, const char* target, const char* data)
{
    if (!self || !target || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_name(target, true) || writer_ascii_equals_ignore_case(target, "xml") ||
        (data && (!is_valid_writer_xml(data) || strstr(data, "?>")))) {
        self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_indent(self);
    write_raw_str(self, "<?");
    write_raw_str(self, target);
    if (data && data[0]) {
        write_byte(self, (uint8_t)' ');
        write_raw_str(self, data);
    }
    write_raw_str(self, "?>");
}

/**
 * @brief      写入实体引用
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param name 实体名称
 */
void XXmlStreamWriter_writeEntityReference(XXmlStreamWriter* self, const XString* name)
{
    if (!self || !name || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeEntityReference_utf8(self, XString_toUtf8(name));
}

/**
 * @brief      写入实体引用（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param name 实体名称（UTF-8 编码）
 */
void XXmlStreamWriter_writeEntityReference_utf8(XXmlStreamWriter* self, const char* name)
{
    if (!self || !name || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_name(name, true)) {
        self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    write_byte(self, (uint8_t)'&');
    write_raw_str(self, name);
    write_byte(self, (uint8_t)';');
}

/**
 * @brief      写入 DTD 声明
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param dtd  DTD 字符串
 */
void XXmlStreamWriter_writeDTD(XXmlStreamWriter* self, const XString* dtd)
{
    if (!self || !dtd || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeDTD_utf8(self, XString_toUtf8(dtd));
}

/**
 * @brief      写入 DTD 声明（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param dtd  DTD 字符串（UTF-8 编码）
 */
void XXmlStreamWriter_writeDTD_utf8(XXmlStreamWriter* self, const char* dtd)
{
    if (!self || !dtd || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!is_valid_writer_xml(dtd)) {
        self->m_hasError = true;
        return;
    }
    close_start_element(self, false);
    if (self->m_autoFormatting) write_byte(self, (uint8_t)'\n');
    write_raw_str(self, dtd);
    if (self->m_autoFormatting) write_byte(self, (uint8_t)'\n');
}

/**
 * @brief      写入命名空间声明
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param prefix       命名空间前缀（可为空字符串）
 */
void XXmlStreamWriter_writeNamespace(XXmlStreamWriter* self, const XString* namespaceUri, const XString* prefix)
{
    if (!self || !namespaceUri) {
        if (self) self->m_hasError = true;
        return;
    }
    const char* prefixUtf8 = prefix ? XString_toUtf8(prefix) : "";
    char generatedPrefix[32];
    if (!prefixUtf8 || !prefixUtf8[0]) {
        do {
            ++self->m_namespacePrefixCounter;
            make_writer_prefix(generatedPrefix, sizeof(generatedPrefix),
                               self->m_namespacePrefixCounter);
        } while (namespace_for_prefix(self, generatedPrefix));
        prefixUtf8 = generatedPrefix;
    }
    write_namespace_utf8_impl(self, XString_toUtf8(namespaceUri), prefixUtf8);
}

/**
 * @brief      写入命名空间声明（UTF-8 版本）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param prefix       命名空间前缀（UTF-8 编码，可为空字符串）
 */
void XXmlStreamWriter_writeNamespace_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* prefix)
{
    if (!self || !namespaceUri) {
        if (self) self->m_hasError = true;
        return;
    }
    const char* prefixUtf8 = prefix ? prefix : "";
    char generatedPrefix[32];
    if (!prefixUtf8[0]) {
        do {
            ++self->m_namespacePrefixCounter;
            make_writer_prefix(generatedPrefix, sizeof(generatedPrefix),
                               self->m_namespacePrefixCounter);
        } while (namespace_for_prefix(self, generatedPrefix));
        prefixUtf8 = generatedPrefix;
    }
    write_namespace_utf8_impl(self, namespaceUri, prefixUtf8);
}

/**
 * @brief      写入默认命名空间声明
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 */
void XXmlStreamWriter_writeDefaultNamespace(XXmlStreamWriter* self, const XString* namespaceUri)
{
    if (!self || !namespaceUri) {
        if (self) self->m_hasError = true;
        return;
    }
    write_namespace_utf8_impl(self, XString_toUtf8(namespaceUri), "");
}

/**
 * @brief      写入默认命名空间声明（UTF-8 版本）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 */
void XXmlStreamWriter_writeDefaultNamespace_utf8(XXmlStreamWriter* self, const char* namespaceUri)
{
    write_namespace_utf8_impl(self, namespaceUri, "");
}

/**
 * @brief      写入文本元素（包含开始标签、文本、结束标签）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 * @param text          文本内容
 */
void XXmlStreamWriter_writeTextElement(XXmlStreamWriter* self, const XString* qualifiedName, const XString* text)
{
    if (!self || !qualifiedName || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeStartElement(self, qualifiedName);
    XXmlStreamWriter_writeCharacters(self, text);
    XXmlStreamWriter_writeEndElement(self);
}

/**
 * @brief      写入文本元素（包含开始标签、文本、结束标签）UTF-8 版本
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名（UTF-8 编码）
 * @param text          文本内容（UTF-8 编码）
 */
void XXmlStreamWriter_writeTextElement_utf8(XXmlStreamWriter* self, const char* qualifiedName, const char* text)
{
    if (!self || !qualifiedName || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeStartElement_utf8(self, qualifiedName);
    XXmlStreamWriter_writeCharacters_utf8(self, text);
    XXmlStreamWriter_writeEndElement(self);
}

/**
 * @brief      写入文本元素（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 * @param text         文本内容
 */
void XXmlStreamWriter_writeTextElement_ex(XXmlStreamWriter* self, const XString* namespaceUri, const XString* name, const XString* text)
{
    if (!self || !name || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeStartElement_ex(self, namespaceUri, name);
    XXmlStreamWriter_writeCharacters(self, text);
    XXmlStreamWriter_writeEndElement(self);
}

/**
 * @brief      写入文本元素（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 * @param text         文本内容（UTF-8 编码）
 */
void XXmlStreamWriter_writeTextElement_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name, const char* text)
{
    if (!self || !name || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    XXmlStreamWriter_writeStartElement_ex_utf8(self, namespaceUri, name);
    XXmlStreamWriter_writeCharacters_utf8(self, text);
    XXmlStreamWriter_writeEndElement(self);
}

void XXmlStreamWriter_writeCurrentToken(XXmlStreamWriter* self, const XXmlStreamReader* reader)
{
    if (!self || !reader || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }

    int tokenType = XXmlStreamReader_tokenType(reader);

    switch (tokenType) {
        case XXmlStream_StartDocument: {
            const XString* version = XXmlStreamReader_documentVersion(reader);
            if (version && XString_size(version) > 0) {
                if (XXmlStreamReader_hasStandaloneDeclaration(reader)) {
                    XXmlStreamWriter_writeStartDocument_ex_2(
                        self, version, XXmlStreamReader_isStandaloneDocument(reader));
                } else {
                    XXmlStreamWriter_writeStartDocument_ex(self, version);
                }
            } else {
                XXmlStreamWriter_writeStartDocument(self);
            }
            break;
        }

        case XXmlStream_EndDocument: {
            XXmlStreamWriter_writeEndDocument(self);
            break;
        }

        case XXmlStream_StartElement: {
            const XString* namespaceUri = XXmlStreamReader_namespaceUri(reader);
            const XString* name = XXmlStreamReader_name(reader);
            const XString* qname = XXmlStreamReader_qualifiedName(reader);
            if (qname && XString_size(qname) > 0) {
                XXmlStreamWriter_writeStartElement(self, qname);
            } else if (namespaceUri && XString_size(namespaceUri) > 0) {
                XXmlStreamWriter_writeStartElement_ex(self, namespaceUri, name);
            } else {
                XXmlStreamWriter_writeStartElement(self, name);
            }

            if (XXmlStreamReader_hasNamespaceDeclarations(reader)) {
                const XXmlStreamNamespaceDeclarations* declarations =
                    XXmlStreamReader_namespaceDeclarations(reader);
                int nsCount = XXmlStreamNamespaceDeclarations_size(declarations);
                for (int i = 0; i < nsCount; i++) {
                    const XXmlStreamNamespaceDeclaration* ns =
                        XXmlStreamNamespaceDeclarations_at(declarations, i);
                    if (ns) {
                        const XString* nsPrefix = XXmlStreamNamespaceDeclaration_prefix(ns);
                        const XString* nsUri = XXmlStreamNamespaceDeclaration_namespaceUri(ns);
                        if (nsPrefix && XString_size(nsPrefix) > 0) {
                            XXmlStreamWriter_writeNamespace(self, nsUri, nsPrefix);
                        } else {
                            XXmlStreamWriter_writeDefaultNamespace(self, nsUri);
                        }
                    }
                }
            }

            const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(reader);
            if (attrs) {
                XXmlStreamWriter_writeAttributes(self, attrs);
            }
            break;
        }

        case XXmlStream_EndElement: {
            XXmlStreamWriter_writeEndElement(self);
            break;
        }

        case XXmlStream_Characters: {
            const XString* text = XXmlStreamReader_text(reader);
            if (text) {
                if (XXmlStreamReader_isCDATA(reader)) {
                    XXmlStreamWriter_writeCDATA(self, text);
                } else {
                    XXmlStreamWriter_writeCharacters(self, text);
                }
            }
            break;
        }

        case XXmlStream_Comment: {
            const XString* text = XXmlStreamReader_text(reader);
            if (text) {
                XXmlStreamWriter_writeComment(self, text);
            }
            break;
        }

        case XXmlStream_DTD: {
            const XString* dtdText = XXmlStreamReader_text(reader);
            if (dtdText && XString_size(dtdText) > 0) {
                XXmlStreamWriter_writeDTD(self, dtdText);
            }
            break;
        }

        case XXmlStream_EntityReference: {
            const XString* name = XXmlStreamReader_name(reader);
            if (name) {
                XXmlStreamWriter_writeEntityReference(self, name);
            }
            break;
        }

        case XXmlStream_ProcessingInstruction: {
            const XString* target = XXmlStreamReader_processingInstructionTarget(reader);
            const XString* data = XXmlStreamReader_processingInstructionData(reader);
            if (target) {
                XXmlStreamWriter_writeProcessingInstruction(self, target, data);
            }
            break;
        }

        case XXmlStream_Invalid:
        default:
            break;
    }
}

/**
 * @brief      判断是否有错误
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     有错误返回 true
 */
bool XXmlStreamWriter_hasError(const XXmlStreamWriter* self)
{
    if (!self) return true;
    return self->m_hasError;
}

void XXmlStreamWriter_setDevice(XXmlStreamWriter* self, XIODevice* device)
{
    if (ISNULL(self, "XXmlStreamWriter")) return;
    /* Qt setDevice 会解除此前的外部字符串或字节数组输出目标。 */
    self->m_externalBuffer = NULL;
    self->m_externalString = NULL;
    self->m_device = device;
}

XIODevice* XXmlStreamWriter_device(const XXmlStreamWriter* self)
{
    return self? self->m_device:NULL;
}
