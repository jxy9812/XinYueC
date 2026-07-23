/******************************************************************************
 * @file       XXmlStreamWriter.c
 * @brief      XXmlStreamWriter XML ?????????? Qt 6.8 QXmlStreamWriter?
 * @author     XinYueC ??
 * @note       ???? XML ??????????????
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
 * ??????
 * ============================================================================ */

/** @brief ??????? */
#define DEFAULT_INDENT 4

/* ============================================================================
 * ??????????
 * ============================================================================ */

/** @brief ????????????? > ? />? */
static void close_start_element(XXmlStreamWriter* self, bool empty);

/** @brief ???? */
static void write_indent(XXmlStreamWriter* self);

/** @brief ? UTF-8 ????????? */
static void write_raw(XXmlStreamWriter* self, const char* data, size_t len);

/** @brief ? UTF-8 ????????????????? */
static void write_raw_str(XXmlStreamWriter* self, const char* str);

/** @brief ????? XML ???? */
static void write_escaped(XXmlStreamWriter* self, const char* text, bool isAttribute);

/** @brief ???????????? */
static void write_start_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

/** @brief ????????????? */
static void write_empty_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

/* ============================================================================
 * ???????
 * ============================================================================ */

/**
 * @brief      ????XXmlStreamWriter ? deinit ??
 * @param obj  ??????
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
 * @brief      ????XXmlStreamWriter ? copy ??
 * @param obj  ??????
 * @param src  ?????
 */
static void VXXmlStreamWriter_copy(XXmlStreamWriter* obj, const XXmlStreamWriter* src)
{
    if (ISNULL(obj, "XXmlStreamWriter") || ISNULL(src, "XXmlStreamWriter")) return;
    
    /* ========== 调用父类 copy ========== */
    XClass_copy_base((XClass*)obj, (const XClass*)src);
    
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
}
static void VXXmlStreamWriter_move(XXmlStreamWriter* obj, XXmlStreamWriter* src)
{
    if (ISNULL(obj, "XXmlStreamWriter") || ISNULL(src, "XXmlStreamWriter")) return;
    
    /* ????? move */
    XClass_move_base((XClass*)obj, (const XClass*)src);
    
    /* ????? */
    obj->m_buffer = src->m_buffer;
    obj->m_deviceString = src->m_deviceString;
    obj->m_autoFormatting = src->m_autoFormatting;
    obj->m_autoFormattingIndent = src->m_autoFormattingIndent;
    obj->m_elementStack = src->m_elementStack;
    obj->m_hasError = src->m_hasError;
    obj->m_inStartElement = src->m_inStartElement;
    obj->m_elementNameStack = src->m_elementNameStack;
    obj->m_elementNameStackSize = src->m_elementNameStackSize;
    obj->m_elementNameStackCapacity = src->m_elementNameStackCapacity;
    
    /* ????? */
    src->m_buffer = NULL;
    src->m_deviceString = NULL;
    src->m_autoFormatting = false;
    src->m_autoFormattingIndent = DEFAULT_INDENT;
    src->m_elementStack = 0;
    src->m_hasError = false;
    src->m_inStartElement = false;
    src->m_elementNameStack = NULL;
    src->m_elementNameStackSize = 0;
    src->m_elementNameStackCapacity = 0;
}

/* ============================================================================
 * ????????
 * ============================================================================ */

/**
 * @brief      ? UTF-8 ??????????????
 * @param self ?????????
 * @param data ????
 * @param len  ????
 */
static void write_raw(XXmlStreamWriter* self, const char* data, size_t len)
{
    if (!self || !data || !self->m_buffer) return;
    for (size_t i = 0; i < len; i++) {
        XByteArray_push_back_1(self->m_buffer, (uint8_t)data[i]);
    }
}

/**
 * @brief      ? UTF-8 ?????????????????
 * @param self ?????????
 * @param str  ???
 */
static void write_raw_str(XXmlStreamWriter* self, const char* str)
{
    if (!self || !str || !self->m_buffer) return;
    write_raw(self, str, strlen(str));
}

/**
 * @brief      ????
 * @param self ?????????
 */
static void write_indent(XXmlStreamWriter* self)
{
    if (!self || !self->m_autoFormatting || !self->m_buffer) return;
    
    /* ???? */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)'\n');
    
    /* ?????? */
    int indent = self->m_elementStack * self->m_autoFormattingIndent;
    for (int i = 0; i < indent; i++) {
        XByteArray_push_back_1(self->m_buffer, (uint8_t)' ');
    }
}

/**
 * @brief      ??????????
 * @param self  ?????????
 * @param empty ???????/> ? >?
 */
static void close_start_element(XXmlStreamWriter* self, bool empty)
{
    if (!self || !self->m_inStartElement) return;
    
    if (empty) {
        /* ========== 写入 /> ========== */
        XByteArray_push_back_1(self->m_buffer, (uint8_t)'/');
        XByteArray_push_back_1(self->m_buffer, (uint8_t)'>');
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
        XByteArray_push_back_1(self->m_buffer, (uint8_t)'>');
    }
    self->m_inStartElement = false;
}

/**
 * @brief      ????? XML ????
 * @param self        ?????????
 * @param text        ????
 * @param isAttribute ??????????????????
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
                    XByteArray_push_back_1(self->m_buffer, c);
                }
                break;
            case '\'':
                if (isAttribute) {
                    write_raw_str(self, "&apos;");
                } else {
                    XByteArray_push_back_1(self->m_buffer, c);
                }
                break;
            case '\n':
                if (isAttribute) {
                    write_raw_str(self, "&#10;");
                } else {
                    XByteArray_push_back_1(self->m_buffer, c);
                }
                break;
            case '\r':
                if (isAttribute) {
                    write_raw_str(self, "&#13;");
                } else {
                    XByteArray_push_back_1(self->m_buffer, c);
                }
                break;
            case '\t':
                if (isAttribute) {
                    write_raw_str(self, "&#9;");
                } else {
                    XByteArray_push_back_1(self->m_buffer, c);
                }
                break;
            default:
                if (c < 0x20) {
                    /* ?????????? */
                    char hex[8];
                    snprintf(hex, sizeof(hex), "&#x%02X;", c);
                    write_raw_str(self, hex);
                } else {
                    XByteArray_push_back_1(self->m_buffer, c);
                }
                break;
        }
    }
}

/**
 * @brief      ????????????
 * @param self         ?????????
 * @param namespaceUri ???? URI??? NULL?
 * @param name         ???
 */
static void write_start_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    if (!self || !name || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ???? */
    write_indent(self);
    
    /* ?? < */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)'<');
    
    /* ???????? */
    if (namespaceUri && namespaceUri[0]) {
        write_raw_str(self, namespaceUri);
        XByteArray_push_back_1(self->m_buffer, (uint8_t)':');
    }
    
    /* ????? */
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
    self->m_elementStack++;
}

/**
 * @brief      ?????????????
 * @param self         ?????????
 * @param namespaceUri ???? URI??? NULL?
 * @param name         ???
 */
static void write_empty_element_impl(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    if (!self || !name || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ???? */
    write_indent(self);
    
    /* ?? < */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)'<');
    
    /* ???????? */
    if (namespaceUri && namespaceUri[0]) {
        write_raw_str(self, namespaceUri);
        XByteArray_push_back_1(self->m_buffer, (uint8_t)':');
    }
    
    /* ????? */
    write_raw_str(self, name);
    
    /* ?? /> */
    write_raw_str(self, "/>");
}

/* ============================================================================
 * ???????
 * ============================================================================ */

/**
 * @brief      ??? XXmlStreamWriter ??????
 * @return     ???????? XVtable ???
 */
XVtable* XXmlStreamWriter_class_init(void)
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_SIZE);
    
    /* ???????? */
    XVTABLE_INHERIT_XCLASS(XClass);
    
    /* ????? */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXXmlStreamWriter_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXXmlStreamWriter_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXXmlStreamWriter_move);
    
    return XVTABLE_DEFAULT;
}

/* ============================================================================
 * ??????
 * ============================================================================ */

/**
 * @brief      ????? XXmlStreamWriter ??
 * @return     ?????? XXmlStreamWriter ?????????? NULL
 */
XXmlStreamWriter* XXmlStreamWriter_create(void)
{
    XXmlStreamWriter* self = (XXmlStreamWriter*)XMalloc_System(sizeof(XXmlStreamWriter));
    if (!self) return NULL;
    
    XXmlStreamWriter_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

/**
 * @brief      ??? XXmlStreamWriter ??
 * @param self ????? XXmlStreamWriter ????
 */
void XXmlStreamWriter_init(XXmlStreamWriter* self)
{
    if (ISNULL(self, "XXmlStreamWriter")) return;
    
    /* ?????? */
    memset(self, 0, sizeof(XXmlStreamWriter));
    
    /* ??????? */
    XClass_init((XClass*)self);
    
    /* ?????? */
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
    
    /* ??????? */
    self->m_deviceString = XString_create();
    if (!self->m_deviceString) {
        XByteArray_delete_base(self->m_buffer);
        self->m_buffer = NULL;
        self->m_hasError = true;
        return;
    }
    
    /* ????? */
    self->m_autoFormatting = false;
    self->m_autoFormattingIndent = DEFAULT_INDENT;
    self->m_elementStack = 0;
    self->m_hasError = false;
    self->m_inStartElement = false;
}

/**
 * @brief      ?? XXmlStreamWriter ??
 * @param self ???? XXmlStreamWriter ????
 */
void XXmlStreamWriter_deinit(XXmlStreamWriter* self)
{
    if (ISNULL(self, "XXmlStreamWriter")) return;
    if (XClassIsVtableNull(self)) {
        XClassSetVtable(self, XXmlStreamWriter);
    }
    XXmlStreamWriter_deinit_base(self);
}

/**
 * @brief      ????? XXmlStreamWriter ??
 * @param self ???? XXmlStreamWriter ????
 */
void XXmlStreamWriter_delete(XXmlStreamWriter* self)
{
    if (ISNULL(self, "XXmlStreamWriter")) return;
    if (XClassIsVtableNull(self)) {
        XClassSetVtable(self, XXmlStreamWriter);
    }
    XXmlStreamWriter_delete_base(self);
}

/* ============================================================================
 * ?????
 * ============================================================================ */

/**
 * @brief      XXmlStreamWriter deinit ?????
 * @param self ?? XXmlStreamWriter ????
 */
void XXmlStreamWriter_deinit_base(XXmlStreamWriter* self)
{
    if (ISNULL(self, "XXmlStreamWriter") || ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XXmlStreamWriter*))(self);
}

/**
 * @brief      XXmlStreamWriter delete ?????
 * @param self ?? XXmlStreamWriter ????
 */
void XXmlStreamWriter_delete_base(XXmlStreamWriter* self)
{
    if (ISNULL(self, "XXmlStreamWriter") || ISNULL(XClassGetVtable(self), "Vtable")) return;
    XXmlStreamWriter_deinit_base(self);
    if (Class_MemoryFree(self)) {
        Class_MemoryFree(self)(self);
    }
}

/* ============================================================================
 * ????
 * ============================================================================ */

/**
 * @brief      ??????????
 * @param self ?? XXmlStreamWriter ????
 * @return     ??????? UTF-8 ???
 */
const char* XXmlStreamWriter_toString(const XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) return "";
    
    /* ???????????????? */
    size_t size = XByteArray_size_base((XByteArray*)self->m_buffer);
    if (size == 0) return "";
    
    /* ??????? */
    if (self->m_deviceString) {
        const uint8_t* data = (const uint8_t*)XByteArray_data((XByteArray*)self->m_buffer);
        XString_assign_with_length_utf8(self->m_deviceString, (const char*)data, size);
        return XString_toUtf8(self->m_deviceString);
    }
    
    return (const char*)XByteArray_data((XByteArray*)self->m_buffer);
}

/**
 * @brief      ???????
 * @param self ?? XXmlStreamWriter ????
 * @return     ?? XByteArray ???
 */
XByteArray* XXmlStreamWriter_toByteArray(const XXmlStreamWriter* self)
{
    if (!self) return NULL;
    return self->m_buffer;
}

/* ============================================================================
 * ?????
 * ============================================================================ */

/**
 * @brief      ???????????
 * @param self   ?? XXmlStreamWriter ????
 * @param enable ????
 */
void XXmlStreamWriter_setAutoFormatting(XXmlStreamWriter* self, bool enable)
{
    if (!self) return;
    self->m_autoFormatting = enable;
}

/**
 * @brief      ???????????
 * @param self ?? XXmlStreamWriter ????
 * @return     ???? true
 */
bool XXmlStreamWriter_autoFormatting(const XXmlStreamWriter* self)
{
    if (!self) return false;
    return self->m_autoFormatting;
}

/**
 * @brief      ???????
 * @param self   ?? XXmlStreamWriter ????
 * @param indent ?????
 */
void XXmlStreamWriter_setAutoFormattingIndent(XXmlStreamWriter* self, int indent)
{
    if (!self) return;
    if (indent < 0) indent = 0;
    self->m_autoFormattingIndent = indent;
}

/**
 * @brief      ???????
 * @param self ?? XXmlStreamWriter ????
 * @return     ?????
 */
int XXmlStreamWriter_autoFormattingIndent(const XXmlStreamWriter* self)
{
    if (!self) return DEFAULT_INDENT;
    return self->m_autoFormattingIndent;
}

/* ============================================================================
 * ????
 * ============================================================================ */

/**
 * @brief      ?????????<?xml version="1.0"?>?
 * @param self ?? XXmlStreamWriter ????
 */
void XXmlStreamWriter_writeStartDocument(XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ?? <?xml version="1.0"?> */
    write_raw_str(self, "<?xml version=\"1.0\"?>");
}

/**
 * @brief      ?????????????
 * @param self    ?? XXmlStreamWriter ????
 * @param version ???
 */
void XXmlStreamWriter_writeStartDocument_ex(XXmlStreamWriter* self, const char* version)
{
    if (!self || !version || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ?? <?xml version="version"?> */
    write_raw_str(self, "<?xml version=\"");
    write_raw_str(self, version);
    write_raw_str(self, "\"?>");
}

/**
 * @brief      ??????????????????
 * @param self       ?? XXmlStreamWriter ????
 * @param version    ???
 * @param standalone ??????
 */
void XXmlStreamWriter_writeStartDocument_ex_2(XXmlStreamWriter* self, const char* version, bool standalone)
{
    if (!self || !version || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ?? <?xml version="version" standalone="yes/no"?> */
    write_raw_str(self, "<?xml version=\"");
    write_raw_str(self, version);
    write_raw_str(self, "\" standalone=\"");
    write_raw_str(self, standalone ? "yes" : "no");
    write_raw_str(self, "\"?>");
}

/**
 * @brief      ????????
 * @param self ?? XXmlStreamWriter ????
 */
void XXmlStreamWriter_writeEndDocument(XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ???? */
    if (self->m_autoFormatting) {
        XByteArray_push_back_1(self->m_buffer, (uint8_t)'\n');
    }
}

/**
 * @brief      ??????
 * @param self          ?? XXmlStreamWriter ????
 * @param qualifiedName ???
 */
void XXmlStreamWriter_writeStartElement(XXmlStreamWriter* self, const char* qualifiedName)
{
    write_start_element_impl(self, NULL, qualifiedName);
}

/**
 * @brief      ?????????????
 * @param self         ?? XXmlStreamWriter ????
 * @param namespaceUri ???? URI
 * @param name         ???
 */
void XXmlStreamWriter_writeStartElement_ex(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    write_start_element_impl(self, namespaceUri, name);
}

/**
 * @brief      ??????
 * @param self ?? XXmlStreamWriter ????
 */
void XXmlStreamWriter_writeEndElement(XXmlStreamWriter* self)
{
    if (!self || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    if (self->m_inStartElement) {
        /* ========== 如果当前在开始标签内，直接关闭为自闭合标签 ========== */
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
 * @brief      ???????
 * @param self          ?? XXmlStreamWriter ????
 * @param qualifiedName ???
 */
void XXmlStreamWriter_writeEmptyElement(XXmlStreamWriter* self, const char* qualifiedName)
{
    write_empty_element_impl(self, NULL, qualifiedName);
}

/**
 * @brief      ??????????????
 * @param self         ?? XXmlStreamWriter ????
 * @param namespaceUri ???? URI
 * @param name         ???
 */
void XXmlStreamWriter_writeEmptyElement_ex(XXmlStreamWriter* self, const char* namespaceUri, const char* name)
{
    write_empty_element_impl(self, namespaceUri, name);
}

/**
 * @brief      ????
 * @param self          ?? XXmlStreamWriter ????
 * @param qualifiedName ???
 * @param value         ???
 */
void XXmlStreamWriter_writeAttribute(XXmlStreamWriter* self, const char* qualifiedName, const char* value)
{
    if (!self || !qualifiedName || !value || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    if (!self->m_inStartElement) {
        /* ????????????? */
        self->m_hasError = true;
        return;
    }
    
    /* ???? */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)' ');
    
    /* ????? */
    write_raw_str(self, qualifiedName);
    
    /* ?? =" */
    write_raw_str(self, "=\"");
    
    /* ???????? */
    write_escaped(self, value, true);
    
    /* ?? " */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)'"');
}

/**
 * @brief      ???????????
 * @param self         ?? XXmlStreamWriter ????
 * @param namespaceUri ???? URI
 * @param name         ???
 * @param value        ???
 */
void XXmlStreamWriter_writeAttribute_ex(XXmlStreamWriter* self, const char* namespaceUri, const char* name, const char* value)
{
    if (!self || !name || !value || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    
    /* ???? */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)' ');
    
    /* ????????:??? */
    if (namespaceUri && namespaceUri[0]) {
        write_raw_str(self, namespaceUri);
        XByteArray_push_back_1(self->m_buffer, (uint8_t)':');
    }
    write_raw_str(self, name);
    
    /* ?? ="value" */
    write_raw_str(self, "=\"");
    write_escaped(self, value, true);
    XByteArray_push_back_1(self->m_buffer, (uint8_t)'"');
}

/**
 * @brief      ?????? XXmlStreamAttribute ???
 * @param self      ?? XXmlStreamWriter ????
 * @param attribute ??????
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
    
    /* ????? */
    const char* qualifiedName = XXmlStreamAttribute_qualifiedName(attribute);
    const char* namespaceUri = XXmlStreamAttribute_namespaceUri(attribute);
    const char* name = XXmlStreamAttribute_name(attribute);
    const char* value = XXmlStreamAttribute_value(attribute);
    
    if (!qualifiedName && (!namespaceUri || !name)) {
        self->m_hasError = true;
        return;
    }
    
    if (qualifiedName && qualifiedName[0] && (!namespaceUri || !namespaceUri[0])) {
        /* ????? */
        XXmlStreamWriter_writeAttribute(self, qualifiedName, value);
    } else {
        /* ??????+??? */
        XXmlStreamWriter_writeAttribute_ex(self, namespaceUri, name, value);
    }
}

/**
 * @brief      ??????
 * @param self       ?? XXmlStreamWriter ????
 * @param attributes ??????
 */
void XXmlStreamWriter_writeAttributes(XXmlStreamWriter* self, const XXmlStreamAttributes* attributes)
{
    if (!self || !attributes) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ?????? */
    int count = XXmlStreamAttributes_size(attributes);
    for (int i = 0; i < count; i++) {
        const XXmlStreamAttribute* attr = XXmlStreamAttributes_at(attributes, i);
        if (attr) {
            XXmlStreamWriter_writeAttribute_attr(self, attr);
        }
    }
}

/**
 * @brief      ??????
 * @param self ?? XXmlStreamWriter ????
 * @param text ????
 */
void XXmlStreamWriter_writeCharacters(XXmlStreamWriter* self, const char* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ??????? */
    write_escaped(self, text, false);
}

/**
 * @brief      ?? CDATA ?
 * @param self ?? XXmlStreamWriter ????
 * @param text CDATA ??
 */
void XXmlStreamWriter_writeCDATA(XXmlStreamWriter* self, const char* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ?? <![CDATA[...]]> */
    write_raw_str(self, "<![CDATA[");
    write_raw_str(self, text);
    write_raw_str(self, "]]>");
}

/**
 * @brief      ????
 * @param self ?? XXmlStreamWriter ????
 * @param text ????
 */
void XXmlStreamWriter_writeComment(XXmlStreamWriter* self, const char* text)
{
    if (!self || !text || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ???? */
    write_indent(self);
    
    /* ?? <!-- ... --> */
    write_raw_str(self, "<!--");
    write_raw_str(self, text);
    write_raw_str(self, "-->");
}

/**
 * @brief      ??????
 * @param self   ?? XXmlStreamWriter ????
 * @param target ????
 * @param data   ??????? NULL?
 */
void XXmlStreamWriter_writeProcessingInstruction(XXmlStreamWriter* self, const char* target, const char* data)
{
    if (!self || !target || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ?? <?target ...?> */
    write_raw_str(self, "<?");
    write_raw_str(self, target);
    if (data && data[0]) {
        XByteArray_push_back_1(self->m_buffer, (uint8_t)' ');
        write_raw_str(self, data);
    }
    write_raw_str(self, "?>");
}

/**
 * @brief      ??????
 * @param self ?? XXmlStreamWriter ????
 * @param name ????
 */
void XXmlStreamWriter_writeEntityReference(XXmlStreamWriter* self, const char* name)
{
    if (!self || !name || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ?? &name; */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)'&');
    write_raw_str(self, name);
    XByteArray_push_back_1(self->m_buffer, (uint8_t)';');
}

/**
 * @brief      ?? DTD ??
 * @param self ?? XXmlStreamWriter ????
 * @param dtd  DTD ???
 */
void XXmlStreamWriter_writeDTD(XXmlStreamWriter* self, const char* dtd)
{
    if (!self || !dtd || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ????????????? */
    close_start_element(self, false);
    
    /* ?? DTD ??? */
    write_raw_str(self, dtd);
}

/**
 * @brief      ????????
 * @param self         ?? XXmlStreamWriter ????
 * @param namespaceUri ???? URI
 * @param prefix       ??????????????
 */
void XXmlStreamWriter_writeNamespace(XXmlStreamWriter* self, const char* namespaceUri, const char* prefix)
{
    if (!self || !namespaceUri || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    
    /* ???? */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)' ');
    
    /* ?? xmlns:prefix="namespaceUri" */
    write_raw_str(self, "xmlns");
    if (prefix && prefix[0]) {
        XByteArray_push_back_1(self->m_buffer, (uint8_t)':');
        write_raw_str(self, prefix);
    }
    write_raw_str(self, "=\"");
    write_escaped(self, namespaceUri, true);
    XByteArray_push_back_1(self->m_buffer, (uint8_t)'"');
}

/**
 * @brief      ??????????
 * @param self         ?? XXmlStreamWriter ????
 * @param namespaceUri ???? URI
 */
void XXmlStreamWriter_writeDefaultNamespace(XXmlStreamWriter* self, const char* namespaceUri)
{
    if (!self || !namespaceUri || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    if (!self->m_inStartElement) {
        self->m_hasError = true;
        return;
    }
    
    /* ???? */
    XByteArray_push_back_1(self->m_buffer, (uint8_t)' ');
    
    /* ?? xmlns="namespaceUri" */
    write_raw_str(self, "xmlns=\"");
    write_escaped(self, namespaceUri, true);
    XByteArray_push_back_1(self->m_buffer, (uint8_t)'"');
}

/**
 * @brief      ??????????????????????
 * @param self          ?? XXmlStreamWriter ????
 * @param qualifiedName ???
 * @param text          ????
 */
void XXmlStreamWriter_writeTextElement(XXmlStreamWriter* self, const char* qualifiedName, const char* text)
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
 * @brief      ?????????????
 * @param self         ?? XXmlStreamWriter ????
 * @param namespaceUri ???? URI
 * @param name         ???
 * @param text         ????
 */
void XXmlStreamWriter_writeTextElement_ex(XXmlStreamWriter* self, const char* namespaceUri, const char* name, const char* text)
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
 * @brief      ???? Token????????? Token ?????
 * @param self   ?? XXmlStreamWriter ????
 * @param reader ? XXmlStreamReader ????
 */
void XXmlStreamWriter_writeCurrentToken(XXmlStreamWriter* self, const XXmlStreamReader* reader)
{
    if (!self || !reader || !self->m_buffer) {
        if (self) self->m_hasError = true;
        return;
    }
    
    /* ???? Token ?? */
    int tokenType = XXmlStreamReader_tokenType(reader);
    
    switch (tokenType) {
        case XXmlStream_StartDocument: {
            /* ???????? */
            const char* version = XXmlStreamReader_documentVersion(reader);
            bool standalone = XXmlStreamReader_isStandaloneDocument(reader);
            if (version && version[0]) {
                XXmlStreamWriter_writeStartDocument_ex_2(self, version, standalone);
            } else {
                XXmlStreamWriter_writeStartDocument(self);
            }
            break;
        }
        
        case XXmlStream_EndDocument: {
            /* ?????? */
            XXmlStreamWriter_writeEndDocument(self);
            break;
        }
        
        case XXmlStream_StartElement: {
            /* ?????? */
            const char* namespaceUri = XXmlStreamReader_namespaceUri(reader);
            const char* name = XXmlStreamReader_name(reader);
            if (namespaceUri && namespaceUri[0]) {
                XXmlStreamWriter_writeStartElement_ex(self, namespaceUri, name);
            } else {
                const char* qname = XXmlStreamReader_qualifiedName(reader);
                XXmlStreamWriter_writeStartElement(self, qname ? qname : name);
            }
            
            /* ???????? */
            if (XXmlStreamReader_hasNamespaceDeclarations(reader)) {
                int nsCount = XXmlStreamReader_namespaceDeclarationsCount(reader);
                for (int i = 0; i < nsCount; i++) {
                    const XXmlStreamNamespaceDeclaration* ns = XXmlStreamReader_namespaceDeclaration(reader, i);
                    if (ns) {
                        const char* nsPrefix = XXmlStreamNamespaceDeclaration_prefix(ns);
                        const char* nsUri = XXmlStreamNamespaceDeclaration_namespaceUri(ns);
                        if (nsPrefix && nsPrefix[0]) {
                            XXmlStreamWriter_writeNamespace(self, nsUri, nsPrefix);
                        } else {
                            XXmlStreamWriter_writeDefaultNamespace(self, nsUri);
                        }
                    }
                }
            }
            
            /* ???? */
            const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(reader);
            if (attrs) {
                XXmlStreamWriter_writeAttributes(self, attrs);
            }
            break;
        }
        
        case XXmlStream_EndElement: {
            /* ?????? */
            XXmlStreamWriter_writeEndElement(self);
            break;
        }
        
                case XXmlStream_Characters: {
            /* 写入字符数据 */
            const char* text = XXmlStreamReader_text(reader);
            if (text) {
                /* 直接使用 writeCharacters 写入 */
                XXmlStreamWriter_writeCharacters(self, text);
            }
            break;
        }

        case XXmlStream_Comment: {
            /* ???? */
            const char* text = XXmlStreamReader_text(reader);
            if (text) {
                XXmlStreamWriter_writeComment(self, text);
            }
            break;
        }
        
        case XXmlStream_DTD: {
            /* ?? DTD */
            const char* dtdName = XXmlStreamReader_dtdName(reader);
            const char* dtdPublicId = XXmlStreamReader_dtdPublicId(reader);
            const char* dtdSystemId = XXmlStreamReader_dtdSystemId(reader);
            
            /* ?? DTD ??? */
            XString* dtdStr = XString_create();
            if (dtdStr) {
                XString_append_utf8(dtdStr, "<!DOCTYPE ");
                XString_append_utf8(dtdStr, dtdName ? dtdName : "");
                if (dtdPublicId && dtdPublicId[0]) {
                    XString_append_utf8(dtdStr, " PUBLIC \"");
                    XString_append_utf8(dtdStr, dtdPublicId);
                    XString_append_utf8(dtdStr, "\"");
                }
                if (dtdSystemId && dtdSystemId[0]) {
                    if (!dtdPublicId || !dtdPublicId[0]) {
                        XString_append_utf8(dtdStr, " SYSTEM");
                    }
                    XString_append_utf8(dtdStr, " \"");
                    XString_append_utf8(dtdStr, dtdSystemId);
                    XString_append_utf8(dtdStr, "\"");
                }
                XString_append_utf8(dtdStr, ">");
                
                const char* dtdResult = XString_toUtf8(dtdStr);
                XXmlStreamWriter_writeDTD(self, dtdResult);
                XString_delete_base(dtdStr);
            }
            break;
        }
        
        case XXmlStream_EntityReference: {
            /* ?????? */
            const char* name = XXmlStreamReader_name(reader);
            if (name) {
                XXmlStreamWriter_writeEntityReference(self, name);
            }
            break;
        }
        
        case XXmlStream_ProcessingInstruction: {
            /* ?????? */
            const char* target = XXmlStreamReader_processingInstructionTarget(reader);
            const char* data = XXmlStreamReader_processingInstructionData(reader);
            if (target) {
                XXmlStreamWriter_writeProcessingInstruction(self, target, data);
            }
            break;
        }
        
        case XXmlStream_Invalid:
        default:
            /* ?? Token??????? */
            break;
    }
}

/**
 * @brief      ???????
 * @param self ?? XXmlStreamWriter ????
 * @return     ????? true
 */
bool XXmlStreamWriter_hasError(const XXmlStreamWriter* self)
{
    if (!self) return true;
    return self->m_hasError;
}
