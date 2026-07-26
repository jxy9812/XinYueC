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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * 默认配置
 * ============================================================================ */

/** @brief 自动缩进使用的默认缩进宽度。 */
#define DEFAULT_INDENT 4

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
static void write_escaped(XXmlStreamWriter* self, const char* text, bool isAttribute);

/** @brief 写入开始元素标签并更新元素栈状态。 */
static void write_start_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

/** @brief 写入空元素标签并标记待关闭状态。 */
static void write_empty_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

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
    if (ISNULL(obj, "XXmlStreamWriter") || ISNULL(src, "XXmlStreamWriter")) return;

    /* 目标未 init 则自动 init（vtable 为空时），让 copy_base 可在未初始化目标上安全调用 */
    if (XClassIsVtableNull(obj)) {
        XXmlStreamWriter_init(obj);
    }

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
}
static void VXXmlStreamWriter_move(XXmlStreamWriter* obj, XXmlStreamWriter* src)
{
    if (ISNULL(obj, "XXmlStreamWriter") || ISNULL(src, "XXmlStreamWriter")) return;

    /* 目标未 init 则自动 init */
    if (XClassIsVtableNull(obj)) {
        XXmlStreamWriter_init(obj);
    }

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

    /* ========== 转移所有权 ========== */
    obj->m_buffer = src->m_buffer;
    obj->m_deviceString = src->m_deviceString;
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
    
    /* 将源对象恢复为可安全释放的空状态。 */
    src->m_buffer = NULL;
    src->m_deviceString = NULL;
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
    if (self->m_device && XIODevice_write_1(self->m_device, data, (int64_t)len) != (int64_t)len)
        self->m_hasError = true;
}

static void write_byte(XXmlStreamWriter* self, uint8_t value)
{
    if (!self || !self->m_buffer) return;
    XByteArray_push_back_1(self->m_buffer, value);
    if (self->m_device && XIODevice_write_1(self->m_device, (const char*)&value, 1) != 1)
        self->m_hasError = true;
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
        self->m_elementStack--;
        /* ========== 从栈中弹出元素名 ========== */
        if (self->m_elementNameStackSize > 0) {
            self->m_elementNameStackSize--;
            if (self->m_elementNameStack[self->m_elementNameStackSize]) {
                XString_delete_base(self->m_elementNameStack[self->m_elementNameStackSize]);
                self->m_elementNameStack[self->m_elementNameStackSize] = NULL;
            }
        }
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
static void write_escaped(XXmlStreamWriter* self, const char* text, bool isAttribute)
{
    if (!self || !text || !self->m_buffer) return;
    
    for (const char* p = text; *p; p++) {
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
                if (c < 0x20) {
                    /* XML 1.0 不允许的控制字符使用十六进制字符引用。 */
                    char hex[8];
                    snprintf(hex, sizeof(hex), "&#x%02X;", c);
                    write_raw_str(self, hex);
                } else {
                    write_byte(self, c);
                }
                break;
        }
    }
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
    
    /* 先关闭上一个尚未完成的开始标签。 */
    close_start_element(self, false);
    
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
    
    /* ========== 将元素名压入栈 ========== */
    if (self->m_elementNameStackSize >= self->m_elementNameStackCapacity) {
        int newCap = self->m_elementNameStackCapacity ? self->m_elementNameStackCapacity * 2 : 16;
        XString** newStack = (XString**)XRealloc_System(self->m_elementNameStack, newCap * sizeof(XString*));
        if (!newStack) {
            self->m_hasError = true;
            return;
        }
        self->m_elementNameStack = newStack;
        self->m_elementNameStackCapacity = newCap;
    }
    self->m_elementNameStack[self->m_elementNameStackSize] = XString_create_utf8(name);
    self->m_elementNameStackSize++;
    
    /* ========== 设置开始标签状态 ========== */
    self->m_inStartElement = true;
    self->m_pendingEmptyElement = false;
    self->m_elementStack++;
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

/* ============================================================================
 * 类初始化
 * ============================================================================ */

/**
 * @brief      初始化 XXmlStreamWriter 的虚函数表。
 * @return     已初始化的类虚函数表指针。
 */
XVtable* XXmlStreamWriter_class_init(void)
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_SIZE);
    
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
    write_start_element_impl(self, NULL, XString_toUtf8(name));
    if (namespaceUri && XString_size(namespaceUri) > 0 && !self->m_hasError) {
        XXmlStreamWriter_writeDefaultNamespace(self, namespaceUri);
    }
}

/**
 * @brief      写入开始标签（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 */
void XXmlStreamWriter_writeStartElement_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    write_start_element_impl(self, NULL, name);
    if (namespaceUri && namespaceUri[0] && self && !self->m_hasError) {
        XXmlStreamWriter_writeDefaultNamespace_utf8(self, namespaceUri);
    }
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
    
    /* Qt 在没有内容时使用空元素形式。 */
    /* writeEmptyElement() 已经代表完整子元素；调用方随后结束父元素时，
       先关闭该空子元素，再继续关闭仍在栈中的父元素。 */
    if (self->m_inStartElement && self->m_pendingEmptyElement) {
        close_start_element(self, true);
        if (self->m_elementStack > 0)
            XXmlStreamWriter_writeEndElement(self);
        return;
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
    
    /* ========== 从栈中弹出元素名 ========== */
    const char* elementName = NULL;
    if (self->m_elementNameStackSize > 0) {
        self->m_elementNameStackSize--;
        elementName = XString_toUtf8(self->m_elementNameStack[self->m_elementNameStackSize]);
    }
    
    /* ========== 写入缩进 ========== */
    write_indent(self);
    
    /* ========== 写入结束标签 </element> ========== */
    write_raw_str(self, "</");
    if (elementName) {
        write_raw_str(self, elementName);
    }
    write_raw_str(self, ">");
    
    /* ========== 释放元素名字符串资源 ========== */
    if (self->m_elementNameStackSize >= 0 && self->m_elementNameStack[self->m_elementNameStackSize]) {
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
    write_empty_element_impl(self, NULL, XString_toUtf8(name));
    if (namespaceUri && XString_size(namespaceUri) > 0 && !self->m_hasError) {
        XXmlStreamWriter_writeDefaultNamespace(self, namespaceUri);
    }
}

/**
 * @brief      写入空元素标签（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 */
void XXmlStreamWriter_writeEmptyElement_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    write_empty_element_impl(self, NULL, name);
    if (namespaceUri && namespaceUri[0] && self && !self->m_hasError) {
        XXmlStreamWriter_writeDefaultNamespace_utf8(self, namespaceUri);
    }
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
    if (!namespaceUri || !namespaceUri[0]) {
        XXmlStreamWriter_writeAttribute_utf8(self, name, value);
        return;
    }

    char prefix[32];
    self->m_namespacePrefixCounter++;
    snprintf(prefix, sizeof(prefix), "xns%u", self->m_namespacePrefixCounter);
    XXmlStreamWriter_writeNamespace_utf8(self, namespaceUri, prefix);
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

    if (!qualifiedName && (!namespaceUri || !name)) {
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
    close_start_element(self, false);
    write_raw_str(self, "<![CDATA[");
    write_raw_str(self, XString_toUtf8(text));
    write_raw_str(self, "]]>");
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
    close_start_element(self, false);
    write_raw_str(self, "<![CDATA[");
    write_raw_str(self, text);
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
    close_start_element(self, false);
    write_indent(self);
    write_raw_str(self, "<!--");
    write_raw_str(self, XString_toUtf8(text));
    write_raw_str(self, "-->");
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
    close_start_element(self, false);
    write_raw_str(self, "<?");
    write_raw_str(self, XString_toUtf8(target));
    if (data && XString_size(data) > 0) {
        write_byte(self, (uint8_t)' ');
        write_raw_str(self, XString_toUtf8(data));
    }
    write_raw_str(self, "?>");
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
    close_start_element(self, false);
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
    close_start_element(self, false);
    write_byte(self, (uint8_t)'&');
    write_raw_str(self, XString_toUtf8(name));
    write_byte(self, (uint8_t)';');
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
    close_start_element(self, false);
    write_raw_str(self, XString_toUtf8(dtd));
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
    close_start_element(self, false);
    write_raw_str(self, dtd);
}

/**
 * @brief      写入命名空间声明
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param prefix       命名空间前缀（可为空字符串）
 */
void XXmlStreamWriter_writeNamespace(XXmlStreamWriter* self, const XString* namespaceUri, const XString* prefix)
{
    if (!self || !namespaceUri || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    write_byte(self, (uint8_t)' ');
    write_raw_str(self, "xmlns");
    if (prefix && XString_size(prefix) > 0) {
        write_byte(self, (uint8_t)':');
        write_raw_str(self, XString_toUtf8(prefix));
    }
    write_raw_str(self, "=\"");
    write_escaped(self, XString_toUtf8(namespaceUri), true);
    write_byte(self, (uint8_t)'"');
}

/**
 * @brief      写入命名空间声明（UTF-8 版本）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param prefix       命名空间前缀（UTF-8 编码，可为空字符串）
 */
void XXmlStreamWriter_writeNamespace_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* prefix)
{
    if (!self || !namespaceUri || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    write_byte(self, (uint8_t)' ');
    write_raw_str(self, "xmlns");
    if (prefix && prefix[0]) {
        write_byte(self, (uint8_t)':');
        write_raw_str(self, prefix);
    }
    write_raw_str(self, "=\"");
    write_escaped(self, namespaceUri, true);
    write_byte(self, (uint8_t)'"');
}

/**
 * @brief      写入默认命名空间声明
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 */
void XXmlStreamWriter_writeDefaultNamespace(XXmlStreamWriter* self, const XString* namespaceUri)
{
    if (!self || !namespaceUri || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    write_byte(self, (uint8_t)' ');
    write_raw_str(self, "xmlns=\"");
    write_escaped(self, XString_toUtf8(namespaceUri), true);
    write_byte(self, (uint8_t)'"');
}

/**
 * @brief      写入默认命名空间声明（UTF-8 版本）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 */
void XXmlStreamWriter_writeDefaultNamespace_utf8(XXmlStreamWriter* self, const char* namespaceUri)
{
    if (!self || !namespaceUri || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    write_byte(self, (uint8_t)' ');
    write_raw_str(self, "xmlns=\"");
    write_escaped(self, namespaceUri, true);
    write_byte(self, (uint8_t)'"');
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
            const XString* version = XXmlStreamReader_documentVersion_const(reader);
            bool standalone = XXmlStreamReader_isStandaloneDocument(reader);
            if (version && XString_size(version) > 0) {
                XXmlStreamWriter_writeStartDocument_ex_2(self, version, standalone);
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
            const XString* namespaceUri = XXmlStreamReader_namespaceUri_const(reader);
            const XString* name = XXmlStreamReader_name_const(reader);
            if (namespaceUri && XString_size(namespaceUri) > 0) {
                XXmlStreamWriter_writeStartElement_ex(self, namespaceUri, name);
            } else {
                const XString* qname = XXmlStreamReader_qualifiedName_const(reader);
                XXmlStreamWriter_writeStartElement(self, qname ? qname : name);
            }

            if (XXmlStreamReader_hasNamespaceDeclarations(reader)) {
                int nsCount = XXmlStreamReader_namespaceDeclarationsCount(reader);
                for (int i = 0; i < nsCount; i++) {
                    const XXmlStreamNamespaceDeclaration* ns = XXmlStreamReader_namespaceDeclaration(reader, i);
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
            const XString* text = XXmlStreamReader_text_const(reader);
            if (text) {
                XXmlStreamWriter_writeCharacters(self, text);
            }
            break;
        }

        case XXmlStream_Comment: {
            const XString* text = XXmlStreamReader_text_const(reader);
            if (text) {
                XXmlStreamWriter_writeComment(self, text);
            }
            break;
        }

        case XXmlStream_DTD: {
            const XString* dtdName = XXmlStreamReader_dtdName_const(reader);
            const XString* dtdPublicId = XXmlStreamReader_dtdPublicId_const(reader);
            const XString* dtdSystemId = XXmlStreamReader_dtdSystemId_const(reader);

            XString* dtdStr = XString_create();
            if (dtdStr) {
                XString_append_utf8(dtdStr, "<!DOCTYPE ");
                XString_append_utf8(dtdStr, dtdName ? XString_toUtf8(dtdName) : "");
                if (dtdPublicId && XString_size(dtdPublicId) > 0) {
                    XString_append_utf8(dtdStr, " PUBLIC \"");
                    XString_append_utf8(dtdStr, XString_toUtf8(dtdPublicId));
                    XString_append_utf8(dtdStr, "\"");
                }
                if (dtdSystemId && XString_size(dtdSystemId) > 0) {
                    if (!dtdPublicId || XString_size(dtdPublicId) == 0) {
                        XString_append_utf8(dtdStr, " SYSTEM");
                    }
                    XString_append_utf8(dtdStr, " \"");
                    XString_append_utf8(dtdStr, XString_toUtf8(dtdSystemId));
                    XString_append_utf8(dtdStr, "\"");
                }
                XString_append_utf8(dtdStr, ">");
                XXmlStreamWriter_writeDTD(self, dtdStr);
                XString_delete_base(dtdStr);
            }
            break;
        }

        case XXmlStream_EntityReference: {
            const XString* name = XXmlStreamReader_name_const(reader);
            if (name) {
                XXmlStreamWriter_writeEntityReference(self, name);
            }
            break;
        }

        case XXmlStream_ProcessingInstruction: {
            const XString* target = XXmlStreamReader_processingInstructionTarget_const(reader);
            const XString* data = XXmlStreamReader_processingInstructionData_const(reader);
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
    self->m_device = device;
}

XIODevice* XXmlStreamWriter_device(const XXmlStreamWriter* self)
{
    return self? self->m_device:NULL;
}
