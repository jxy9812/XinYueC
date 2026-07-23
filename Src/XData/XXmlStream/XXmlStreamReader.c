/******************************************************************************
 * @file       XXmlStreamReader.c
 * @brief      XXmlStreamReader XML 流式读取器实现（对标 Qt 6.8 QXmlStreamReader）
 * @author     XinYueC 团队
 * @note       提供 SAX 风格的前向只读 XML 解析功能，完整实现所有 Token 类型
 ******************************************************************************/
#include "XXmlStreamReader.h"
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
#include "XFileDevice.h"
#include "XStack.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ============================================================================
 * 内部常量定义
 * ============================================================================ */

/** @brief 实体扩展默认限制 */
#define DEFAULT_ENTITY_EXPANSION_LIMIT 5000

/** @brief 内部缓冲区初始大小 */
#define INITIAL_BUFFER_SIZE 4096

/** @brief 流结束标记 */
#define STREAM_EOF ((uint32_t)~0U)

/* ============================================================================
 * 内部结构体定义
 * ============================================================================ */

/**
 * @brief      标签信息（用于元素栈）
 * @note       对应 Qt QXmlStreamReaderPrivate::Tag 结构
 */
typedef struct XmlTag
{
    XString* m_name;            /**< 标签本地名 */
    XString* m_qualifiedName;   /**< 标签限定名（前缀:本地名） */
    XString* m_prefix;          /**< 标签前缀 */
    XString* m_namespaceUri;    /**< 标签命名空间 URI */
} XmlTag;

/**
 * @brief      命名空间声明
 * @note       对应 Qt QXmlStreamNamespaceDeclaration
 */
typedef struct XmlNamespaceDeclaration
{
    XString* m_prefix;          /**< 命名空间前缀 */
    XString* m_namespaceUri;    /**< 命名空间 URI */
} XmlNamespaceDeclaration;

/**
 * @brief      XML 解析上下文
 * @note       对应 Qt QXmlStreamReaderPrivate 的主要数据
 */
typedef struct XXmlStreamReaderPrivate
{
    /* ---------- 输入源 ---------- */
    XIODevice* m_device;         /**< 关联的 IO 设备（可为 NULL） */
    const char* m_data;          /**< 内存数据指针 */
    size_t m_dataLength;         /**< 内存数据长度 */
    bool m_deleteDevice;         /**< 是否在清理时删除设备 */
    bool m_isDataFromDevice;     /**< 是否从设备读取数据 */

    /* ---------- 读取位置 ---------- */
    const char* m_readPtr;       /**< 当前读取位置 */
    const char* m_endPtr;        /**< 数据结束位置 */
    int64_t m_lineNumber;        /**< 当前行号（从 0 开始） */
    int64_t m_lastLineStart;     /**< 上一行起始位置偏移 */
    int64_t m_characterOffset;   /**< 字符偏移量 */

    /* ---------- 当前 Token ---------- */
    XXmlStreamTokenType m_type;  /**< 当前 Token 类型 */
    XString* m_name;             /**< 当前元素/属性名 */
    XString* m_qualifiedName;    /**< 当前限定名 */
    XString* m_prefix;           /**< 当前前缀 */
    XString* m_namespaceUri;     /**< 当前命名空间 URI */
    XString* m_text;             /**< 当前文本内容 */

    /* ---------- 属性 ---------- */
    XXmlStreamAttributes* m_attributes;   /**< 当前元素的属性列表 */
    int m_attributeCount;                 /**< 属性数量 */

    /* ---------- 命名空间 ---------- */
    XmlNamespaceDeclaration* m_namespaceDeclarations; /**< 命名空间声明数组 */
    int m_namespaceDeclarationCount;                  /**< 命名空间声明数量 */
    int m_namespaceDeclarationCapacity;               /**< 命名空间声明容量 */

    /* ---------- 元素栈 ---------- */
    XmlTag* m_tagStack;        /**< 标签栈 */
    int m_tagStackSize;        /**< 标签栈大小 */
    int m_tagStackCapacity;    /**< 标签栈容量 */

    /* ---------- 文档信息 ---------- */
    XString* m_documentVersion;    /**< 文档版本 */
    XString* m_documentEncoding;   /**< 文档编码 */
    bool m_isStandaloneDocument;   /**< 是否为独立文档 */
    bool m_hasStandalone;          /**< 是否已有 standalone 声明 */

    /* ---------- DTD 信息 ---------- */
    XString* m_dtdName;        /**< DTD 名称 */
    XString* m_dtdPublicId;    /**< DTD 公共标识符 */
    XString* m_dtdSystemId;    /**< DTD 系统标识符 */

    /* ---------- 处理指令 ---------- */
    XString* m_processingInstructionTarget; /**< 处理指令目标 */
    XString* m_processingInstructionData;   /**< 处理指令数据 */

    /* ---------- 错误 ---------- */
    XXmlStreamError m_error;     /**< 错误类型 */
    XString* m_errorString;      /**< 错误描述 */

    /* ---------- 标志位 ---------- */
    bool m_atEnd;                /**< 是否到达末尾 */
    bool m_isCDATA;              /**< 当前字符是否为 CDATA */
    bool m_isWhitespace;         /**< 当前字符是否为空白 */
    bool m_isEmptyElement;       /**< 当前元素是否为空元素 */
    bool m_lockEncoding;         /**< 是否锁定编码 */
    int m_entityExpansionLimit;  /**< 实体扩展限制 */

    /* ---------- 内部缓冲区 ---------- */
    XString* m_buffer;           /**< 内部临时缓冲区 */
} XXmlStreamReaderPrivate;

/* ============================================================================
 * 内部辅助函数前向声明
 * ============================================================================ */

/** @brief 跳过空白字符 */
static const char* skip_whitespace(const char* ptr, const char* end);

/** @brief 检查字符是否为 XML 名称起始字符 */
static bool is_name_start_char(uint8_t c);

/** @brief 检查字符是否为 XML 名称字符 */
static bool is_name_char(uint8_t c);

/** @brief 解析 XML 名称 */
static bool parse_name(const char** ptr, const char* end, XString* out);

/** @brief 解析引号字符串 */
static bool parse_quoted_string(const char** ptr, const char* end, XString* out);

/** @brief 解析 XML 声明（<?xml ... ?>） */
static bool parse_xml_declaration(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析注释 */
static bool parse_comment(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析处理指令 */
static bool parse_processing_instruction(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析 CDATA 段 */
static bool parse_cdata(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析 DTD */
static bool parse_dtd(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析开始标签 */
static bool parse_start_element(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析结束标签 */
static bool parse_end_element(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析字符内容 */
static bool parse_characters(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析属性 */
static bool parse_attributes(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析实体引用 */
static bool parse_entity_reference(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析命名空间声明 */
static void parse_namespace_declaration(XXmlStreamReaderPrivate* d, const XString* attrName, const XString* attrValue);

/** @brief 清理当前 Token 数据 */
static void clear_current_token(XXmlStreamReaderPrivate* d);

/** @brief 设置错误 */
static void set_error(XXmlStreamReaderPrivate* d, XXmlStreamError error, const char* message);

/** @brief 初始化私有数据 */
static void private_init(XXmlStreamReaderPrivate* d);

/** @brief 释放私有数据 */
static void private_deinit(XXmlStreamReaderPrivate* d);

/* ============================================================================
 * 内部辅助函数实现
 * ============================================================================ */

/**
 * @brief      跳过空白字符
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     跳过空白后的新位置
 */
static const char* skip_whitespace(const char* ptr, const char* end)
{
    while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r'))
        ++ptr;
    return ptr;
}

/**
 * @brief      检查字符是否为 XML 名称起始字符
 * @param c   字符
 * @return     是名称起始字符返回 true
 */
static bool is_name_start_char(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' ||
           (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0xFF);
}

/**
 * @brief      检查字符是否为 XML 名称字符
 * @param c   字符
 * @return     是名称字符返回 true
 */
static bool is_name_char(uint8_t c)
{
    return is_name_start_char(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

/**
 * @brief      解析 XML 名称
 * @param ptr  当前解析位置指针（输入输出，解析后推进）
 * @param end  数据结束位置
 * @param out  输出缓冲区
 * @return     解析成功返回 true
 */
static bool parse_name(const char** ptr, const char* end, XString* out)
{
    const char* start = *ptr;
    if (*ptr >= end || !is_name_start_char((uint8_t)**ptr))
        return false;
    ++(*ptr);
    while (*ptr < end && is_name_char((uint8_t)**ptr))
        ++(*ptr);
    size_t len = (size_t)(*ptr - start);
    if (out)
    {
        XString_append_utf8_len(out, start, len);
    }
    return true;
}

/**
 * @brief      解析引号字符串（单引号或双引号）
 * @param ptr  当前解析位置指针（输入输出）
 * @param end  数据结束位置
 * @param out  输出缓冲区
 * @return     解析成功返回 true
 */
static bool parse_quoted_string(const char** ptr, const char* end, XString* out)
{
    if (*ptr >= end)
        return false;
    char quote = **ptr;
    if (quote != '"' && quote != '\'')
        return false;
    ++(*ptr); /* 跳过引号 */
    const char* start = *ptr;
    while (*ptr < end && **ptr != quote)
    {
        if (**ptr == '&')
        {
            /* 先追加已扫描的文本 */
            if (out && *ptr > start)
                XString_append_utf8_len(out, start, (size_t)(*ptr - start));
            /* 解析实体引用 */
            ++(*ptr);
            if (**ptr == '#')
            {
                ++(*ptr);
                if (**ptr == 'x')
                {
                    ++(*ptr);
                    /* 十六进制字符引用 */
                    uint32_t code = 0;
                    while (*ptr < end && **ptr != ';')
                    {
                        char c = **ptr;
                        if (c >= '0' && c <= '9') code = code * 16 + (uint32_t)(c - '0');
                        else if (c >= 'a' && c <= 'f') code = code * 16 + (uint32_t)(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') code = code * 16 + (uint32_t)(c - 'A' + 10);
                        else break;
                        ++(*ptr);
                    }
                    if (*ptr < end && **ptr == ';') ++(*ptr);
                    /* 将 Unicode 码点转为 UTF-8 */
                    if (out && code > 0)
                    {
                        uint8_t utf8_buf[4];
                        int utf8_len = 0;
                        if (code < 0x80) { utf8_buf[0] = (uint8_t)code; utf8_len = 1; }
                        else if (code < 0x800) { utf8_buf[0] = (uint8_t)(0xC0 | (code >> 6)); utf8_buf[1] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 2; }
                        else if (code < 0x10000) { utf8_buf[0] = (uint8_t)(0xE0 | (code >> 12)); utf8_buf[1] = (uint8_t)(0x80 | ((code >> 6) & 0x3F)); utf8_buf[2] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 3; }
                        else { utf8_buf[0] = (uint8_t)(0xF0 | (code >> 18)); utf8_buf[1] = (uint8_t)(0x80 | ((code >> 12) & 0x3F)); utf8_buf[2] = (uint8_t)(0x80 | ((code >> 6) & 0x3F)); utf8_buf[3] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 4; }
                        XString_append_utf8_len(out, (const char*)utf8_buf, (size_t)utf8_len);
                    }
                    start = *ptr;
                }
                else
                {
                    /* 十进制字符引用 */
                    uint32_t code = 0;
                    while (*ptr < end && **ptr != ';')
                    {
                        char c = **ptr;
                        if (c >= '0' && c <= '9') code = code * 10 + (uint32_t)(c - '0');
                        else break;
                        ++(*ptr);
                    }
                    if (*ptr < end && **ptr == ';') ++(*ptr);
                    if (out && code > 0)
                    {
                        uint8_t utf8_buf[4];
                        int utf8_len = 0;
                        if (code < 0x80) { utf8_buf[0] = (uint8_t)code; utf8_len = 1; }
                        else if (code < 0x800) { utf8_buf[0] = (uint8_t)(0xC0 | (code >> 6)); utf8_buf[1] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 2; }
                        else if (code < 0x10000) { utf8_buf[0] = (uint8_t)(0xE0 | (code >> 12)); utf8_buf[1] = (uint8_t)(0x80 | ((code >> 6) & 0x3F)); utf8_buf[2] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 3; }
                        else { utf8_buf[0] = (uint8_t)(0xF0 | (code >> 18)); utf8_buf[1] = (uint8_t)(0x80 | ((code >> 12) & 0x3F)); utf8_buf[2] = (uint8_t)(0x80 | ((code >> 6) & 0x3F)); utf8_buf[3] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 4; }
                        XString_append_utf8_len(out, (const char*)utf8_buf, (size_t)utf8_len);
                    }
                    start = *ptr;
                }
            }
            else
            {
                /* 预定义实体引用 */
                const char* entity_start = *ptr;
                while (*ptr < end && **ptr != ';' && is_name_char((uint8_t)**ptr))
                    ++(*ptr);
                if (*ptr < end && **ptr == ';')
                {
                    size_t entity_len = (size_t)(*ptr - entity_start);
                    const char* replacement = NULL;
                    if (entity_len == 2 && strncmp(entity_start, "lt", 2) == 0) replacement = "<";
                    else if (entity_len == 2 && strncmp(entity_start, "gt", 2) == 0) replacement = ">";
                    else if (entity_len == 2 && strncmp(entity_start, "ap", 2) == 0 && *(*ptr - 1) == 'o') { /* &apos; */
                        replacement = "'";
                    }
                    else if (entity_len == 4 && strncmp(entity_start, "quot", 4) == 0) replacement = "\\"";
                    else if (entity_len == 2 && strncmp(entity_start, "amp", 3) == 0) replacement = "&";
                    if (replacement && out)
                        XString_append_utf8(out, replacement);
                    ++(*ptr); /* 跳过 ; */
                }
                start = *ptr;
            }
        }
        else
        {
            ++(*ptr);
        }
    }
    if (*ptr >= end)
        return false;
    /* 追加剩余文本 */
    if (out && *ptr > start)
        XString_append_utf8_len(out, start, (size_t)(*ptr - start));
    ++(*ptr); /* 跳过结束引号 */
    return true;
}
/**
 * @brief      解析 XML 声明（<?xml ... ?>）
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_xml_declaration(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    /* 已跳过 <?xml */
    *ptr = skip_whitespace(*ptr, end);
    while (*ptr < end)
    {
        if (**ptr == '?')
        {
            const char* save = *ptr;
            ++(*ptr);
            if (*ptr < end && **ptr == '>')
            {
                ++(*ptr);
                return true;
            }
            *ptr = save;
        }
        /* 解析属性 */
        XString attrName, attrValue;
        XString_init(&attrName);
        XString_init(&attrValue);
        if (!parse_name(ptr, end, &attrName))
        {
            XString_deinit_base(&attrName);
            XString_deinit_base(&attrValue);
            break;
        }
        *ptr = skip_whitespace(*ptr, end);
        if (*ptr < end && **ptr == '=')
        {
            ++(*ptr);
            *ptr = skip_whitespace(*ptr, end);
            if (parse_quoted_string(ptr, end, &attrValue))
            {
                const char* name_utf8 = XString_toUtf8(&attrName);
                const char* value_utf8 = XString_toUtf8(&attrValue);
                if (name_utf8 && value_utf8)
                {
                    if (strcmp(name_utf8, "version") == 0)
                    {
                        if (d->m_documentVersion)
                            XString_assign_utf8(d->m_documentVersion, value_utf8);
                    }
                    else if (strcmp(name_utf8, "encoding") == 0)
                    {
                        if (d->m_documentEncoding)
                            XString_assign_utf8(d->m_documentEncoding, value_utf8);
                    }
                    else if (strcmp(name_utf8, "standalone") == 0)
                    {
                        if (strcmp(value_utf8, "yes") == 0)
                            d->m_isStandaloneDocument = true;
                        else
                            d->m_isStandaloneDocument = false;
                        d->m_hasStandalone = true;
                    }
                }
            }
        }
        XString_deinit_base(&attrName);
        XString_deinit_base(&attrValue);
        *ptr = skip_whitespace(*ptr, end);
    }
    return false;
}

/**
 * @brief      解析注释（<!-- ... -->）
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_comment(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    if (*ptr + 3 >= end)
        return false;
    /* 已跳过 "<!--" */
    const char* start = *ptr;
    while (*ptr < end)
    {
        if (**ptr == '-' && *ptr + 2 < end && *(*ptr + 1) == '-' && *(*ptr + 2) == '>')
        {
            /* 提取注释文本 */
            if (d->m_text && *ptr > start)
                XString_assign_utf8_len(d->m_text, start, (size_t)(*ptr - start));
            *ptr += 3;
            d->m_type = XXmlStream_Comment;
            return true;
        }
        ++(*ptr);
    }
    return false;
}

/**
 * @brief      解析处理指令（<?...?>）
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_processing_instruction(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    /* 已跳过 <? */
    *ptr = skip_whitespace(*ptr, end);
    /* 解析目标 */
    if (!parse_name(ptr, end, d->m_processingInstructionTarget))
        return false;
    *ptr = skip_whitespace(*ptr, end);
    /* 解析数据（直到 ?>） */
    const char* start = *ptr;
    while (*ptr < end)
    {
        if (**ptr == '?' && *ptr + 1 < end && *(*ptr + 1) == '>')
        {
            if (d->m_processingInstructionData && *ptr > start)
                XString_assign_utf8_len(d->m_processingInstructionData, start, (size_t)(*ptr - start));
            *ptr += 2;
            d->m_type = XXmlStream_ProcessingInstruction;
            return true;
        }
        ++(*ptr);
    }
    return false;
}

/**
 * @brief      解析 CDATA 段（<![CDATA[...]]>）
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_cdata(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    /* 已跳过 <![CDATA[ */
    const char* start = *ptr;
    while (*ptr < end)
    {
        if (**ptr == ']' && *ptr + 2 < end && *(*ptr + 1) == ']' && *(*ptr + 2) == '>')
        {
            if (d->m_text && *ptr > start)
                XString_assign_utf8_len(d->m_text, start, (size_t)(*ptr - start));
            *ptr += 3;
            d->m_type = XXmlStream_Characters;
            d->m_isCDATA = true;
            d->m_isWhitespace = false;
            return true;
        }
        ++(*ptr);
    }
    return false;
}

/**
 * @brief      解析 DTD 声明
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_dtd(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    /* 已跳过 <!DOCTYPE */
    *ptr = skip_whitespace(*ptr, end);
    /* 解析 DTD 名称 */
    if (!parse_name(ptr, end, d->m_dtdName))
        return false;
    *ptr = skip_whitespace(*ptr, end);
    /* 解析 PUBLIC / SYSTEM */
    if (*ptr < end && **ptr == 'P')
    {
        if (strncmp(*ptr, "PUBLIC", 6) == 0)
        {
            *ptr += 6;
            *ptr = skip_whitespace(*ptr, end);
            parse_quoted_string(ptr, end, d->m_dtdPublicId);
            *ptr = skip_whitespace(*ptr, end);
            parse_quoted_string(ptr, end, d->m_dtdSystemId);
        }
    }
    else if (*ptr < end && **ptr == 'S')
    {
        if (strncmp(*ptr, "SYSTEM", 6) == 0)
        {
            *ptr += 6;
            *ptr = skip_whitespace(*ptr, end);
            parse_quoted_string(ptr, end, d->m_dtdSystemId);
        }
    }
    /* 跳过内部子集或外部标识符后的内容，直到 ]> 或 > */
    while (*ptr < end)
    {
        if (**ptr == '[')
        {
            /* 内部子集 */
            int depth = 1;
            ++(*ptr);
            while (*ptr < end && depth > 0)
            {
                if (**ptr == '[') ++depth;
                else if (**ptr == ']') --depth;
                ++(*ptr);
            }
        }
        else if (**ptr == '>')
        {
            ++(*ptr);
            d->m_type = XXmlStream_DTD;
            return true;
        }
        else
        {
            ++(*ptr);
        }
    }
    return false;
}
/**
 * @brief      解析命名空间声明
 * @param d        解析器私有数据
 * @param attrName 属性名
 * @param attrValue 属性值
 */
static void parse_namespace_declaration(XXmlStreamReaderPrivate* d, const XString* attrName, const XString* attrValue)
{
    const char* name = XString_toUtf8(attrName);
    const char* value = XString_toUtf8(attrValue);
    if (!name || !value)
        return;
    /* 检查是否为 xmlns 或 xmlns:prefix 形式 */
    if (strcmp(name, "xmlns") == 0)
    {
        /* 默认命名空间声明 */
        if (d->m_namespaceDeclarationCount >= d->m_namespaceDeclarationCapacity)
        {
            int newCap = d->m_namespaceDeclarationCapacity == 0 ? 8 : d->m_namespaceDeclarationCapacity * 2;
            XmlNamespaceDeclaration* newDecl = (XmlNamespaceDeclaration*)XRealloc_System(d->m_namespaceDeclarations, (size_t)newCap * sizeof(XmlNamespaceDeclaration));
            if (!newDecl) return;
            d->m_namespaceDeclarations = newDecl;
            d->m_namespaceDeclarationCapacity = newCap;
        }
        XmlNamespaceDeclaration* decl = &d->m_namespaceDeclarations[d->m_namespaceDeclarationCount];
        memset(decl, 0, sizeof(XmlNamespaceDeclaration));
        decl->m_prefix = XString_create();
        decl->m_namespaceUri = XString_create_utf8(value);
        d->m_namespaceDeclarationCount++;
    }
    else if (strncmp(name, "xmlns:", 6) == 0)
    {
        /* 带前缀的命名空间声明 */
        if (d->m_namespaceDeclarationCount >= d->m_namespaceDeclarationCapacity)
        {
            int newCap = d->m_namespaceDeclarationCapacity == 0 ? 8 : d->m_namespaceDeclarationCapacity * 2;
            XmlNamespaceDeclaration* newDecl = (XmlNamespaceDeclaration*)XRealloc_System(d->m_namespaceDeclarations, (size_t)newCap * sizeof(XmlNamespaceDeclaration));
            if (!newDecl) return;
            d->m_namespaceDeclarations = newDecl;
            d->m_namespaceDeclarationCapacity = newCap;
        }
        XmlNamespaceDeclaration* decl = &d->m_namespaceDeclarations[d->m_namespaceDeclarationCount];
        memset(decl, 0, sizeof(XmlNamespaceDeclaration));
        decl->m_prefix = XString_create_utf8(name + 6);
        decl->m_namespaceUri = XString_create_utf8(value);
        d->m_namespaceDeclarationCount++;
    }
}

/**
 * @brief      解析属性列表
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_attributes(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    while (*ptr < end)
    {
        *ptr = skip_whitespace(*ptr, end);
        if (*ptr >= end)
            return false;
        /* 遇到 /> 或 > 结束属性解析 */
        if (**ptr == '/' || **ptr == '>')
            return true;
        /* 解析属性名 */
        XString attrName;
        XString attrValue;
        XString_init(&attrName);
        XString_init(&attrValue);
        if (!parse_name(ptr, end, &attrName))
        {
            XString_deinit_base(&attrName);
            XString_deinit_base(&attrValue);
            return false;
        }
        *ptr = skip_whitespace(*ptr, end);
        if (*ptr < end && **ptr == '=')
        {
            ++(*ptr);
            *ptr = skip_whitespace(*ptr, end);
            if (!parse_quoted_string(ptr, end, &attrValue))
            {
                XString_deinit_base(&attrName);
                XString_deinit_base(&attrValue);
                return false;
            }
        }
        else
        {
            XString_deinit_base(&attrName);
            XString_deinit_base(&attrValue);
            return false;
        }
        /* 检查是否为命名空间声明 */
        const char* name_utf8 = XString_toUtf8(&attrName);
        /* 添加属性 */
        if (d->m_attributes)
        {
            XXmlStreamAttribute* attr = &d->m_attributes[d->m_attributeCount];
            memset(attr, 0, sizeof(XXmlStreamAttribute));
            /* 检查是否有前缀 */
            const char* colon = strchr(name_utf8, ':');
            if (colon)
            {
                size_t prefix_len = (size_t)(colon - name_utf8);
                attr->m_namespaceUri = XString_create();
                attr->m_qualifiedName = XString_create_copy(&attrName);
                attr->m_name = XString_create_utf8(colon + 1);
                /* 尝试查找命名空间 */
                for (int i = 0; i < d->m_namespaceDeclarationCount; i++)
                {
                    const char* declPrefix = XString_toUtf8(d->m_namespaceDeclarations[i].m_prefix);
                    if (declPrefix && (size_t)strlen(declPrefix) == prefix_len && strncmp(name_utf8, declPrefix, prefix_len) == 0)
                    {
                        XString_assign_utf8(attr->m_namespaceUri, XString_toUtf8(d->m_namespaceDeclarations[i].m_namespaceUri));
                        break;
                    }
                }
            }
            else
            {
                attr->m_qualifiedName = XString_create_copy(&attrName);
                attr->m_name = XString_create_copy(&attrName);
                attr->m_namespaceUri = XString_create();
            }
            attr->m_value = XString_create_copy(&attrValue);
            attr->m_isDefault = false;
            d->m_attributeCount++;
        }
        /* 解析命名空间声明 */
        if (name_utf8 && (strcmp(name_utf8, "xmlns") == 0 || strncmp(name_utf8, "xmlns:", 6) == 0))
        {
            parse_namespace_declaration(d, &attrName, &attrValue);
        }
        XString_deinit_base(&attrName);
        XString_deinit_base(&attrValue);
    }
    return true;
}

/**
 * @brief      解析开始标签
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_start_element(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    /* 已跳过 < */
    d->m_attributeCount = 0;
    d->m_namespaceDeclarationCount = 0;
    /* 解析标签名 */
    XString tagName;
    XString_init(&tagName);
    if (!parse_name(ptr, end, &tagName))
    {
        XString_deinit_base(&tagName);
        return false;
    }
    const char* name_utf8 = XString_toUtf8(&tagName);
    /* 设置标签信息 */
    const char* colon = name_utf8 ? strchr(name_utf8, ':') : NULL;
    if (colon)
    {
        size_t prefix_len = (size_t)(colon - name_utf8);
        XString_assign_utf8_len(d->m_prefix, name_utf8, prefix_len);
        XString_assign_utf8(d->m_name, colon + 1);
        XString_assign_utf8(d->m_qualifiedName, name_utf8);
    }
    else
    {
        XString_assign_utf8(d->m_name, name_utf8);
        XString_assign_utf8(d->m_qualifiedName, name_utf8);
        XString_clear(d->m_prefix);
    }
    XString_deinit_base(&tagName);
    /* 解析属性 */
    if (!parse_attributes(d, ptr, end))
        return false;
    /* 检查空元素 */
    d->m_isEmptyElement = false;
    if (*ptr < end && **ptr == '/')
    {
        d->m_isEmptyElement = true;
        ++(*ptr);
    }
    if (*ptr < end && **ptr == '>')
    {
        ++(*ptr);
        /* 解析命名空间 URI */
        const char* prefix_utf8 = XString_toUtf8(d->m_prefix);
        if (prefix_utf8 && strlen(prefix_utf8) > 0)
        {
            for (int i = 0; i < d->m_namespaceDeclarationCount; i++)
            {
                const char* declPrefix = XString_toUtf8(d->m_namespaceDeclarations[i].m_prefix);
                if (declPrefix && strcmp(prefix_utf8, declPrefix) == 0)
                {
                    XString_assign_utf8(d->m_namespaceUri, XString_toUtf8(d->m_namespaceDeclarations[i].m_namespaceUri));
                    break;
                }
            }
        }
        /* 推入标签栈 */
        if (d->m_tagStackSize >= d->m_tagStackCapacity)
        {
            int newCap = d->m_tagStackCapacity == 0 ? 16 : d->m_tagStackCapacity * 2;
            XmlTag* newStack = (XmlTag*)XRealloc_System(d->m_tagStack, (size_t)newCap * sizeof(XmlTag));
            if (!newStack) return false;
            d->m_tagStack = newStack;
            d->m_tagStackCapacity = newCap;
        }
        XmlTag* tag = &d->m_tagStack[d->m_tagStackSize];
        memset(tag, 0, sizeof(XmlTag));
        tag->m_name = XString_create_copy(d->m_name);
        tag->m_qualifiedName = XString_create_copy(d->m_qualifiedName);
        tag->m_prefix = XString_create_copy(d->m_prefix);
        tag->m_namespaceUri = XString_create_copy(d->m_namespaceUri);
        d->m_tagStackSize++;
        d->m_type = XXmlStream_StartElement;
        return true;
    }
    return false;
}

/**
 * @brief      解析结束标签
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_end_element(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    /* 已跳过 </ */
    XString tagName;
    XString_init(&tagName);
    if (!parse_name(ptr, end, &tagName))
    {
        XString_deinit_base(&tagName);
        return false;
    }
    *ptr = skip_whitespace(*ptr, end);
    if (*ptr < end && **ptr == '>')
    {
        ++(*ptr);
        /* 从标签栈恢复命名空间信息 */
        if (d->m_tagStackSize > 0)
        {
            XmlTag* tag = &d->m_tagStack[d->m_tagStackSize - 1];
            const char* name_utf8 = XString_toUtf8(&tagName);
            const char* tag_qname = XString_toUtf8(tag->m_qualifiedName);
            if (name_utf8 && tag_qname && strcmp(name_utf8, tag_qname) == 0)
            {
                XString_assign_utf8(d->m_name, XString_toUtf8(tag->m_name));
                XString_assign_utf8(d->m_qualifiedName, XString_toUtf8(tag->m_qualifiedName));
                XString_assign_utf8(d->m_prefix, XString_toUtf8(tag->m_prefix));
                XString_assign_utf8(d->m_namespaceUri, XString_toUtf8(tag->m_namespaceUri));
                /* 释放标签 */
                XString_delete_base(tag->m_name);
                XString_delete_base(tag->m_qualifiedName);
                XString_delete_base(tag->m_prefix);
                XString_delete_base(tag->m_namespaceUri);
                memset(tag, 0, sizeof(XmlTag));
                d->m_tagStackSize--;
            }
        }
        d->m_type = XXmlStream_EndElement;
        return true;
    }
    XString_deinit_base(&tagName);
    return false;
}
/**
 * @brief      解析实体引用
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_entity_reference(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    const char* start = *ptr;
    if (**ptr == '#')
    {
        ++(*ptr);
        if (**ptr == 'x')
        {
            ++(*ptr);
            while (*ptr < end && **ptr != ';' && isxdigit((uint8_t)**ptr))
                ++(*ptr);
        }
        else
        {
            while (*ptr < end && **ptr != ';' && isdigit((uint8_t)**ptr))
                ++(*ptr);
        }
        if (*ptr < end && **ptr == ';')
        {
            ++(*ptr);
            if (d->m_text)
                XString_assign_utf8_len(d->m_text, start - 1, (size_t)(*ptr - start + 1));
            d->m_type = XXmlStream_Characters;
            d->m_isWhitespace = false;
            d->m_isCDATA = false;
            return true;
        }
        return false;
    }
    XString entityName;
    XString_init(&entityName);
    while (*ptr < end && **ptr != ';' && is_name_char((uint8_t)**ptr))
    {
        XString_append_utf8_len(&entityName, *ptr, 1);
        ++(*ptr);
    }
    if (*ptr < end && **ptr == ';')
    {
        ++(*ptr);
        const char* entity_utf8 = XString_toUtf8(&entityName);
        if (entity_utf8)
        {
            if (strcmp(entity_utf8, "amp") == 0)
            {
                if (d->m_text) XString_append_utf8(d->m_text, "&");
                d->m_type = XXmlStream_Characters;
            }
            else if (strcmp(entity_utf8, "lt") == 0)
            {
                if (d->m_text) XString_append_utf8(d->m_text, "<");
                d->m_type = XXmlStream_Characters;
            }
            else if (strcmp(entity_utf8, "gt") == 0)
            {
                if (d->m_text) XString_append_utf8(d->m_text, ">");
                d->m_type = XXmlStream_Characters;
            }
            else if (strcmp(entity_utf8, "quot") == 0)
            {
                if (d->m_text) XString_append_utf8(d->m_text, """);
                d->m_type = XXmlStream_Characters;
            }
            else if (strcmp(entity_utf8, "apos") == 0)
            {
                if (d->m_text) XString_append_utf8(d->m_text, "'");
                d->m_type = XXmlStream_Characters;
            }
            else
            {
                XString_assign_utf8(d->m_name, entity_utf8);
                d->m_type = XXmlStream_EntityReference;
            }
        }
        XString_deinit_base(&entityName);
        return true;
    }
    XString_deinit_base(&entityName);
    return false;
}
/**
 * @brief      解析字符内容
 * @param d    解析器私有数据
 * @param ptr  当前解析位置指针
 * @param end  数据结束位置
 * @return     解析成功返回 true
 */
static bool parse_characters(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    const char* start = *ptr;
    d->m_isWhitespace = true;
    d->m_isCDATA = false;
    while (*ptr < end)
    {
        if (**ptr == '<')
        {
            if (*ptr > start)
            {
                if (d->m_text)
                {
                    XString_assign_utf8_len(d->m_text, start, (size_t)(*ptr - start));
                    for (const char* p = start; p < *ptr; p++)
                    {
                        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
                        {
                            d->m_isWhitespace = false;
                            break;
                        }
                    }
                }
                d->m_type = XXmlStream_Characters;
                return true;
            }
            return false;
        }
        else if (**ptr == '&')
        {
            if (*ptr > start && d->m_text)
                XString_append_utf8_len(d->m_text, start, (size_t)(*ptr - start));
            ++(*ptr);
            if (!parse_entity_reference(d, ptr, end))
                return false;
            start = *ptr;
            d->m_isWhitespace = false;
        }
        else
        {
            if (**ptr != ' ' && **ptr != '\t' && **ptr != '\n' && **ptr != '\r')
                d->m_isWhitespace = false;
            ++(*ptr);
        }
    }
    if (*ptr > start && d->m_text)
    {
        XString_assign_utf8_len(d->m_text, start, (size_t)(*ptr - start));
        d->m_type = XXmlStream_Characters;
        return true;
    }
    return false;
}
/**
 * @brief      清理当前 Token 数据
 * @param d    解析器私有数据
 */
static void clear_current_token(XXmlStreamReaderPrivate* d)
{
    if (d->m_name) XString_clear(d->m_name);
    if (d->m_qualifiedName) XString_clear(d->m_qualifiedName);
    if (d->m_prefix) XString_clear(d->m_prefix);
    if (d->m_namespaceUri) XString_clear(d->m_namespaceUri);
    if (d->m_text) XString_clear(d->m_text);
    if (d->m_processingInstructionTarget) XString_clear(d->m_processingInstructionTarget);
    if (d->m_processingInstructionData) XString_clear(d->m_processingInstructionData);
    d->m_attributeCount = 0;
    d->m_namespaceDeclarationCount = 0;
    d->m_isCDATA = false;
    d->m_isWhitespace = true;
    d->m_isEmptyElement = false;
}

/**
 * @brief      设置错误
 * @param d      解析器私有数据
 * @param error  错误类型
 * @param message 错误描述
 */
static void set_error(XXmlStreamReaderPrivate* d, XXmlStreamError error, const char* message)
{
    d->m_error = error;
    if (d->m_errorString)
    {
        if (message)
            XString_assign_utf8(d->m_errorString, message);
        else
        {
            switch (error)
            {
                case XXmlStream_PrematureEndOfDocumentError:
                    XString_assign_utf8(d->m_errorString, "文档提前结束。");
                    break;
                case XXmlStream_CustomError:
                    XString_assign_utf8(d->m_errorString, "无效文档。");
                    break;
                case XXmlStream_NotWellFormedError:
                    XString_assign_utf8(d->m_errorString, "格式不正确。");
                    break;
                default:
                    XString_assign_utf8(d->m_errorString, "未知错误。");
                    break;
            }
        }
    }
    d->m_type = XXmlStream_Invalid;
}

/**
 * @brief      初始化私有数据
 * @param d    解析器私有数据
 */
static void private_init(XXmlStreamReaderPrivate* d)
{
    memset(d, 0, sizeof(XXmlStreamReaderPrivate));
    d->m_name = XString_create();
    d->m_qualifiedName = XString_create();
    d->m_prefix = XString_create();
    d->m_namespaceUri = XString_create();
    d->m_text = XString_create();
    d->m_documentVersion = XString_create();
    d->m_documentEncoding = XString_create();
    d->m_dtdName = XString_create();
    d->m_dtdPublicId = XString_create();
    d->m_dtdSystemId = XString_create();
    d->m_processingInstructionTarget = XString_create();
    d->m_processingInstructionData = XString_create();
    d->m_errorString = XString_create();
    d->m_buffer = XString_create();
    d->m_entityExpansionLimit = DEFAULT_ENTITY_EXPANSION_LIMIT;
    d->m_type = XXmlStream_NoToken;
    d->m_error = XXmlStream_NoError;
    d->m_attributes = NULL;
    d->m_namespaceDeclarations = NULL;
    d->m_namespaceDeclarationCapacity = 0;
    d->m_tagStack = NULL;
    d->m_tagStackCapacity = 0;
    d->m_tagStackSize = 0;
}
/**
 * @brief      释放私有数据
 * @param d    解析器私有数据
 */
static void private_deinit(XXmlStreamReaderPrivate* d)
{
    if (!d) return;
    if (d->m_name) XString_delete_base(d->m_name);
    if (d->m_qualifiedName) XString_delete_base(d->m_qualifiedName);
    if (d->m_prefix) XString_delete_base(d->m_prefix);
    if (d->m_namespaceUri) XString_delete_base(d->m_namespaceUri);
    if (d->m_text) XString_delete_base(d->m_text);
    if (d->m_documentVersion) XString_delete_base(d->m_documentVersion);
    if (d->m_documentEncoding) XString_delete_base(d->m_documentEncoding);
    if (d->m_dtdName) XString_delete_base(d->m_dtdName);
    if (d->m_dtdPublicId) XString_delete_base(d->m_dtdPublicId);
    if (d->m_dtdSystemId) XString_delete_base(d->m_dtdSystemId);
    if (d->m_processingInstructionTarget) XString_delete_base(d->m_processingInstructionTarget);
    if (d->m_processingInstructionData) XString_delete_base(d->m_processingInstructionData);
    if (d->m_errorString) XString_delete_base(d->m_errorString);
    if (d->m_buffer) XString_delete_base(d->m_buffer);
    if (d->m_attributes) XFree_System(d->m_attributes);
    d->m_attributes = NULL;
    if (d->m_namespaceDeclarations)
    {
        for (int i = 0; i < d->m_namespaceDeclarationCount; i++)
        {
            if (d->m_namespaceDeclarations[i].m_prefix) XString_delete_base(d->m_namespaceDeclarations[i].m_prefix);
            if (d->m_namespaceDeclarations[i].m_namespaceUri) XString_delete_base(d->m_namespaceDeclarations[i].m_namespaceUri);
        }
        XFree_System(d->m_namespaceDeclarations);
        d->m_namespaceDeclarations = NULL;
    }
    if (d->m_tagStack)
    {
        for (int i = 0; i < d->m_tagStackSize; i++)
        {
            if (d->m_tagStack[i].m_name) XString_delete_base(d->m_tagStack[i].m_name);
            if (d->m_tagStack[i].m_qualifiedName) XString_delete_base(d->m_tagStack[i].m_qualifiedName);
            if (d->m_tagStack[i].m_prefix) XString_delete_base(d->m_tagStack[i].m_prefix);
            if (d->m_tagStack[i].m_namespaceUri) XString_delete_base(d->m_tagStack[i].m_namespaceUri);
        }
        XFree_System(d->m_tagStack);
        d->m_tagStack = NULL;
    }
    if (d->m_deleteDevice && d->m_device)
    {
        XIODevice_close_base(d->m_device);
        XIODevice_deleteLater(d->m_device);
    }
    d->m_device = NULL;
    d->m_data = NULL;
    d->m_readPtr = NULL;
    d->m_endPtr = NULL;
}
/* ============================================================================
 * 主解析函数：读取下一个 Token
 * ============================================================================ */

/**
 * @brief      读取下一个 XML Token
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     Token 类型
 */
XXmlStreamTokenType XXmlStreamReader_readNext(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return XXmlStream_Invalid;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    /* 检查错误状态 */
    if (d->m_error != XXmlStream_NoError && d->m_error != XXmlStream_PrematureEndOfDocumentError)
    {
        d->m_atEnd = true;
        d->m_type = XXmlStream_Invalid;
        return XXmlStream_Invalid;
    }
    /* 清理当前 Token */
    clear_current_token(d);
    /* 设置读取指针 */
    const char* ptr = d->m_readPtr;
    const char* end = d->m_endPtr;
    if (!ptr || !end || ptr >= end)
    {
        /* 尝试从设备读取更多数据 */
        if (d->m_device && d->m_isDataFromDevice)
        {
            /* 简化：从设备读取数据到缓冲区 */
            if (d->m_buffer && XIODevice_atEnd_base(d->m_device))
            {
                d->m_atEnd = true;
                d->m_type = XXmlStream_EndDocument;
                return XXmlStream_EndDocument;
            }
            d->m_atEnd = true;
            d->m_type = XXmlStream_EndDocument;
            return XXmlStream_EndDocument;
        }
        d->m_atEnd = true;
        if (d->m_type != XXmlStream_EndDocument)
        {
            d->m_type = XXmlStream_EndDocument;
            return XXmlStream_EndDocument;
        }
        return XXmlStream_NoToken;
    }
    /* 处理空元素后的 EndElement */
    if (d->m_type == XXmlStream_StartElement && d->m_isEmptyElement)
    {
        d->m_type = XXmlStream_EndElement;
        return XXmlStream_EndElement;
    }
    /* 跳过空白 */
    ptr = skip_whitespace(ptr, end);
    if (ptr >= end)
    {
        d->m_readPtr = ptr;
        d->m_atEnd = true;
        d->m_type = XXmlStream_EndDocument;
        return XXmlStream_EndDocument;
    }
    /* 根据当前字符决定解析路径 */
    if (*ptr == '<')
    {
        ++ptr;
        if (ptr >= end) { set_error(d, XXmlStream_PrematureEndOfDocumentError, NULL); return XXmlStream_Invalid; }
        if (*ptr == '/')
        {
            ++ptr;
            /* 结束标签 */
            if (!parse_end_element(d, &ptr, end))
            {
                set_error(d, XXmlStream_NotWellFormedError, "解析结束标签失败。");
                return XXmlStream_Invalid;
            }
        }
        else if (*ptr == '?')
        {
            ++ptr;
            if (ptr + 3 < end && strncmp(ptr, "xml", 3) == 0 && (*(ptr + 3) == ' ' || *(ptr + 3) == '?'))
            {
                /* XML 声明 */
                ptr += 3;
                if (!parse_xml_declaration(d, &ptr, end))
                {
                    set_error(d, XXmlStream_NotWellFormedError, "解析 XML 声明失败。");
                    return XXmlStream_Invalid;
                }
                d->m_type = XXmlStream_StartDocument;
            }
            else
            {
                /* 处理指令 */
                if (!parse_processing_instruction(d, &ptr, end))
                {
                    set_error(d, XXmlStream_NotWellFormedError, "解析处理指令失败。");
                    return XXmlStream_Invalid;
                }
            }
        }
        else if (*ptr == '!')
        {
            ++ptr;
            if (ptr >= end) { set_error(d, XXmlStream_PrematureEndOfDocumentError, NULL); return XXmlStream_Invalid; }
            if (*ptr == '-')
            {
                ++ptr;
                if (ptr >= end || *ptr != '-') { set_error(d, XXmlStream_NotWellFormedError, "解析注释失败。"); return XXmlStream_Invalid; }
                ++ptr;
                /* 注释 */
                if (!parse_comment(d, &ptr, end))
                {
                    set_error(d, XXmlStream_NotWellFormedError, "解析注释失败。");
                    return XXmlStream_Invalid;
                }
            }
            else if (*ptr == '[')
            {
                ++ptr;
                if (ptr + 6 >= end || strncmp(ptr, "CDATA[", 6) != 0)
                {
                    set_error(d, XXmlStream_NotWellFormedError, "解析 CDATA 失败。");
                    return XXmlStream_Invalid;
                }
                ptr += 6;
                /* CDATA */
                if (!parse_cdata(d, &ptr, end))
                {
                    set_error(d, XXmlStream_NotWellFormedError, "解析 CDATA 失败。");
                    return XXmlStream_Invalid;
                }
            }
            else if (strncmp(ptr, "DOCTYPE", 7) == 0)
            {
                ptr += 7;
                /* DTD */
                if (!parse_dtd(d, &ptr, end))
                {
                    set_error(d, XXmlStream_NotWellFormedError, "解析 DTD 失败。");
                    return XXmlStream_Invalid;
                }
            }
            else
            {
                set_error(d, XXmlStream_NotWellFormedError, "意外的 '<!' 结构。");
                return XXmlStream_Invalid;
            }
        }
        else
        {
            /* 开始标签 */
            if (!parse_start_element(d, &ptr, end))
            {
                set_error(d, XXmlStream_NotWellFormedError, "解析开始标签失败。");
                return XXmlStream_Invalid;
            }
        }
    }
    else
    {
        /* 字符内容 */
        if (!parse_characters(d, &ptr, end))
        {
            /* 如果没有字符内容，尝试结束文档 */
            d->m_atEnd = true;
            d->m_type = XXmlStream_EndDocument;
            d->m_readPtr = ptr;
            return XXmlStream_EndDocument;
        }
    }
    d->m_readPtr = ptr;
    return d->m_type;
}
/* ============================================================================
 * XXmlStreamReader 公共 API 实现
 * ============================================================================ */

/* ---- 虚函数表 ---- */

/**
 * @brief      初始化 XXmlStreamReader 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XXmlStreamReader_class_init(void)
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XXMLSTREAMREADER_VTABLE_SIZE);
    XVTABLE_INHERIT_XCLASS(XClass);
    return XVTABLE_DEFAULT;
}

/* ---- 创建与初始化 ---- */

/**
 * @brief      在堆上创建 XXmlStreamReader 实例
 * @return     指向新创建的 XXmlStreamReader 对象的指针，失败返回 NULL
 */
XXmlStreamReader* XXmlStreamReader_create(void)
{
    size_t totalSize = sizeof(XXmlStreamReader) + sizeof(XXmlStreamReaderPrivate);
    XXmlStreamReader* self = (XXmlStreamReader*)XMalloc_System(totalSize);
    if (!self) return NULL;
    XXmlStreamReader_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

/**
 * @brief      初始化 XXmlStreamReader 实例
 * @param self 待初始化的 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_init(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return;
    memset(self, 0, sizeof(XXmlStreamReader) + sizeof(XXmlStreamReaderPrivate));
    XClass_init(&self->m_class);
    XClassSetVtable(self, XXmlStreamReader);
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    private_init(d);
}

/**
 * @brief      释放 XXmlStreamReader 内部资源
 * @param self 待释放的 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_deinit(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    private_deinit(d);
    XClass_deinit_base(&self->m_class);
}

/**
 * @brief      在堆上删除 XXmlStreamReader 实例
 * @param self 待删除的 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_delete(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return;
    XXmlStreamReader_deinit(self);
    /* 使用 m_free 释放 */
    if (self->m_class.m_free)
        self->m_class.m_free(self);
}

/* ---- 虚函数调度 ---- */

void XXmlStreamReader_deinit_base(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader") || ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XXmlStreamReader*))(self);
}

void XXmlStreamReader_delete_base(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader") || ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    XXmlStreamReader_deinit(self);
    if (self->m_class.m_free)
        self->m_class.m_free(self);
}

/* ---- 设备设置 ---- */

/**
 * @brief      设置 IO 设备
 * @param self   目标 XXmlStreamReader 对象指针
 * @param device IO 设备指针
 */
void XXmlStreamReader_setDevice(XXmlStreamReader* self, XIODevice* device)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (d->m_deleteDevice && d->m_device)
    {
        XIODevice_close_base(d->m_device);
        XIODevice_deleteLater(d->m_device);
    }
    d->m_device = device;
    d->m_deleteDevice = false;
    d->m_isDataFromDevice = (device != NULL);
    d->m_data = NULL;
    d->m_dataLength = 0;
    d->m_readPtr = NULL;
    d->m_endPtr = NULL;
    d->m_lineNumber = 0;
    d->m_lastLineStart = 0;
    d->m_characterOffset = 0;
    d->m_atEnd = false;
    d->m_type = XXmlStream_NoToken;
    d->m_error = XXmlStream_NoError;
    if (d->m_errorString) XString_clear(d->m_errorString);
}

/**
 * @brief      获取当前 IO 设备
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     IO 设备指针，未设置返回 NULL
 */
XIODevice* XXmlStreamReader_device(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_device;
}

/**
 * @brief      添加数据（从内存缓冲区读取）
 * @param self 目标 XXmlStreamReader 对象指针
 * @param data 数据指针
 * @param length 数据长度
 */
void XXmlStreamReader_addData(XXmlStreamReader* self, const char* data, size_t length)
{
    if (ISNULL(self, "XXmlStreamReader") || !data || length == 0)
        return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (d->m_device)
    {
        /* 有设备时忽略 addData */
        return;
    }
    d->m_data = data;
    d->m_dataLength = length;
    d->m_readPtr = data;
    d->m_endPtr = data + length;
    d->m_isDataFromDevice = false;
}

/**
 * @brief      添加字节数组数据
 * @param self 目标 XXmlStreamReader 对象指针
 * @param data 字节数组指针
 */
void XXmlStreamReader_addData_byteArray(XXmlStreamReader* self, const XByteArray* data)
{
    if (ISNULL(self, "XXmlStreamReader") || !data)
        return;
    const char* ptr = XByteArray_toString(data);
    size_t len = XByteArray_size(data);
    XXmlStreamReader_addData(self, ptr, len);
}

/**
 * @brief      重置读取器到初始状态
 * @param self 目标 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_clear(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    private_deinit(d);
    private_init(d);
    d->m_data = NULL;
    d->m_readPtr = NULL;
    d->m_endPtr = NULL;
    d->m_device = NULL;
}

/**
 * @brief      判断是否已到达末尾
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     已到达末尾返回 true
 */
bool XXmlStreamReader_atEnd(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return true;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_atEnd;
}
/* ---- Token 类型 ---- */

XXmlStreamTokenType XXmlStreamReader_tokenType(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return XXmlStream_Invalid;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type;
}

const char* XXmlStreamReader_tokenString(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return "Invalid";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    switch (d->m_type)
    {
        case XXmlStream_NoToken: return "NoToken";
        case XXmlStream_StartDocument: return "StartDocument";
        case XXmlStream_EndDocument: return "EndDocument";
        case XXmlStream_StartElement: return "StartElement";
        case XXmlStream_EndElement: return "EndElement";
        case XXmlStream_Characters: return "Characters";
        case XXmlStream_Comment: return "Comment";
        case XXmlStream_DTD: return "DTD";
        case XXmlStream_EntityReference: return "EntityReference";
        case XXmlStream_ProcessingInstruction: return "ProcessingInstruction";
        case XXmlStream_Invalid: return "Invalid";
        default: return "Unknown";
    }
}

/* ---- 位置信息 ---- */

int64_t XXmlStreamReader_lineNumber(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return 0;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_lineNumber + 1;
}

int64_t XXmlStreamReader_columnNumber(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return 0;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (d->m_readPtr && d->m_data)
        return (int64_t)(d->m_readPtr - d->m_data) - d->m_lastLineStart;
    return 0;
}

int64_t XXmlStreamReader_characterOffset(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return 0;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_characterOffset;
}

/* ---- 类型判断 ---- */

bool XXmlStreamReader_isStartDocument(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_StartDocument;
}

bool XXmlStreamReader_isEndDocument(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_EndDocument;
}

bool XXmlStreamReader_isStartElement(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_StartElement;
}

bool XXmlStreamReader_isEndElement(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_EndElement;
}

bool XXmlStreamReader_isCharacters(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_Characters;
}

bool XXmlStreamReader_isWhitespace(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_Characters && d->m_isWhitespace;
}

bool XXmlStreamReader_isCDATA(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_Characters && d->m_isCDATA;
}

bool XXmlStreamReader_isComment(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_Comment;
}

bool XXmlStreamReader_isDTD(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_DTD;
}

bool XXmlStreamReader_isEntityReference(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_EntityReference;
}

bool XXmlStreamReader_isProcessingInstruction(const XXmlStreamReader* self)
{
    return XXmlStreamReader_tokenType(self) == XXmlStream_ProcessingInstruction;
}
