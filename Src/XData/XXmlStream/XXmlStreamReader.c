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
    int64_t m_tokenColumn;       /**< 当前 token 的列号（1-based） */

    /* ---------- 当前 Token ---------- */
    XXmlStreamTokenType m_type;  /**< 当前 Token 类型 */
    XString* m_name;             /**< 当前元素/属性名 */
    XString* m_qualifiedName;    /**< 当前限定名 */
    XString* m_prefix;           /**< 当前前缀 */
    XString* m_namespaceUri;     /**< 当前命名空间 URI */
    XString* m_text;             /**< 当前文本内容 */

    /* ---------- 属性 ---------- */
    XXmlStreamAttributes* m_attributes;   /**< 当前元素的属性列表（使用 XXmlStreamAttributes_append 追加） */

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
    bool m_namespaceProcessing;       /**< 是否启用命名空间处理（默认 true） */
    XmlNamespaceDeclaration* m_extraNamespaceDeclarations; /**< 额外声明的命名空间 */
    int m_extraNamespaceDeclarationCount;                   /**< 额外声明数量 */
    int m_extraNamespaceDeclarationCapacity;                /**< 额外声明容量 */
    /* ---------- DTD 声明 ---------- */
    XXmlStreamNotationDeclarations* m_notationDeclarations; /**< DTD 符号声明列表 */
    XXmlStreamEntityDeclarations* m_entityDeclarations; /**< DTD 实体声明列表 */
    XXmlStreamEntityResolver* m_entityResolver; /**< 实体解析器 */

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
 * @brief 统计从 [from, ptr) 范围内的换行符数量（含 \n 与 \r\n）
 */
static int64_t count_newlines(const char* from, const char* to)
{
    int64_t n = 0;
    while (from < to) {
        if (*from == '\n') {
            n++;
        } else if (*from == '\r') {
            n++;
            /* \r\n 算一行 */
            if (from + 1 < to && *(from + 1) == '\n') from++;
        }
        from++;
    }
    return n;
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
        XString_append_with_length_utf8(out, start, len);
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
                XString_append_with_length_utf8(out, start, (size_t)(*ptr - start));
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
                        XString_append_with_length_utf8(out, (const char*)utf8_buf, (size_t)utf8_len);
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
                        XString_append_with_length_utf8(out, (const char*)utf8_buf, (size_t)utf8_len);
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
                    else if (entity_len == 4 && strncmp(entity_start, "quot", 4) == 0) replacement = "\"";
                    else if (entity_len == 3 && strncmp(entity_start, "amp", 3) == 0) replacement = "&";
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
        XString_append_with_length_utf8(out, start, (size_t)(*ptr - start));
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
                XString_assign_with_length_utf8(d->m_text, start, (size_t)(*ptr - start));
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
                XString_assign_with_length_utf8(d->m_processingInstructionData, start, (size_t)(*ptr - start));
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
                XString_assign_with_length_utf8(d->m_text, start, (size_t)(*ptr - start));
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
        /* 添加属性到 m_attributes 列表 */
        if (d->m_attributes)
        {
            /* 查找/创建命名空间 URI（若有前缀） */
            const char* colon = strchr(name_utf8, ':');
            const char* ns_uri = NULL;
            const char* local_name = name_utf8;
            if (colon)
            {
                size_t prefix_len = (size_t)(colon - name_utf8);
                local_name = colon + 1;
                for (int i = 0; i < d->m_namespaceDeclarationCount; i++)
                {
                    const char* declPrefix = XString_toUtf8(d->m_namespaceDeclarations[i].m_prefix);
                    if (declPrefix && (size_t)strlen(declPrefix) == prefix_len
                        && strncmp(name_utf8, declPrefix, prefix_len) == 0)
                    {
                        ns_uri = XString_toUtf8(d->m_namespaceDeclarations[i].m_namespaceUri);
                        break;
                    }
                }
            }
            XXmlStreamAttributes_append(d->m_attributes, ns_uri, local_name, XString_toUtf8(&attrValue));
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
    d->m_namespaceDeclarationCount = 0;
    if (d->m_extraNamespaceDeclarations) {
        for (int i = 0; i < d->m_extraNamespaceDeclarationCount; i++) {
            if (d->m_extraNamespaceDeclarations[i].m_prefix) XString_clear_base(d->m_extraNamespaceDeclarations[i].m_prefix);
            if (d->m_extraNamespaceDeclarations[i].m_namespaceUri) XString_clear_base(d->m_extraNamespaceDeclarations[i].m_namespaceUri);
        }
    }
    d->m_extraNamespaceDeclarationCount = 0;
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
        XString_assign_with_length_utf8(d->m_prefix, name_utf8, prefix_len);
        XString_assign_utf8(d->m_name, colon + 1);
        XString_assign_utf8(d->m_qualifiedName, name_utf8);
    }
    else
    {
        XString_assign_utf8(d->m_name, name_utf8);
        XString_assign_utf8(d->m_qualifiedName, name_utf8);
        XString_clear_base(d->m_prefix);
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
        /* 从标签栈恢复命名空间信息并校验标签名匹配 */
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
                d->m_type = XXmlStream_EndElement;
                XString_deinit_base(&tagName);
                return true;
            }
            else
            {
                /* 标签名不匹配 —— 报告格式错误 */
                d->m_type = XXmlStream_EndElement;
                /* 即使不匹配也返回 true（已读到 >），让 readNext 设置错误 */
                XString_assign_utf8(d->m_name, name_utf8 ? name_utf8 : "");
                XString_deinit_base(&tagName);
                return false;  /* 触发 readNext 设置错误 */
            }
        }
        /* 没有标签栈 —— 报告 PrematureEndOfDocumentError */
        d->m_type = XXmlStream_EndElement;
        XString_assign_utf8(d->m_name, XString_toUtf8(&tagName));
        XString_deinit_base(&tagName);
        return false;
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
            /* 解析数字字符引用为 UTF-8 并追加到 m_text */
            if (d->m_text)
            {
                int code = 0;
                const char* p = start;
                if (p < end && *p == '#') ++p;
                if (p < end && *p == 'x') { ++p; while (p < end && isxdigit((uint8_t)*p)) { code = code * 16 + ((*p >= '0' && *p <= '9') ? (*p - '0') : ((*p | 0x20) - 'a' + 10)); ++p; } }
                else { while (p < end && isdigit((uint8_t)*p)) { code = code * 10 + (*p - '0'); ++p; } }
                if (code > 0 && code < 0x110000)
                {
                    uint8_t utf8_buf[4];
                    int utf8_len = 0;
                    if (code < 0x80) { utf8_buf[0] = (uint8_t)code; utf8_len = 1; }
                    else if (code < 0x800) { utf8_buf[0] = (uint8_t)(0xC0 | (code >> 6)); utf8_buf[1] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 2; }
                    else if (code < 0x10000) { utf8_buf[0] = (uint8_t)(0xE0 | (code >> 12)); utf8_buf[1] = (uint8_t)(0x80 | ((code >> 6) & 0x3F)); utf8_buf[2] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 3; }
                    else { utf8_buf[0] = (uint8_t)(0xF0 | (code >> 18)); utf8_buf[1] = (uint8_t)(0x80 | ((code >> 12) & 0x3F)); utf8_buf[2] = (uint8_t)(0x80 | ((code >> 6) & 0x3F)); utf8_buf[3] = (uint8_t)(0x80 | (code & 0x3F)); utf8_len = 4; }
                    XString_append_with_length_utf8(d->m_text, (const char*)utf8_buf, (size_t)utf8_len);
                }
            }
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
        XString_append_with_length_utf8(&entityName, *ptr, 1);
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
                if (d->m_text) XString_append_utf8(d->m_text, "\"");
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
                    XString_append_with_length_utf8(d->m_text, start, (size_t)(*ptr - start));
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
            /* 如果 m_type 已被前面的实体引用设置为 Characters，说明 m_text 中已积累展开文本 */
            if (d->m_type == XXmlStream_Characters)
            {
                return true;
            }
            return false;
        }
        else if (**ptr == '&')
        {
            if (*ptr > start && d->m_text)
                XString_append_with_length_utf8(d->m_text, start, (size_t)(*ptr - start));
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
        XString_assign_with_length_utf8(d->m_text, start, (size_t)(*ptr - start));
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
    if (d->m_name) XString_clear_base(d->m_name);
    if (d->m_qualifiedName) XString_clear_base(d->m_qualifiedName);
    if (d->m_prefix) XString_clear_base(d->m_prefix);
    if (d->m_namespaceUri) XString_clear_base(d->m_namespaceUri);
    if (d->m_text) XString_clear_base(d->m_text);
    if (d->m_processingInstructionTarget) XString_clear_base(d->m_processingInstructionTarget);
    if (d->m_processingInstructionData) XString_clear_base(d->m_processingInstructionData);
    d->m_namespaceDeclarationCount = 0;
    if (d->m_extraNamespaceDeclarations) {
        for (int i = 0; i < d->m_extraNamespaceDeclarationCount; i++) {
            if (d->m_extraNamespaceDeclarations[i].m_prefix) XString_clear_base(d->m_extraNamespaceDeclarations[i].m_prefix);
            if (d->m_extraNamespaceDeclarations[i].m_namespaceUri) XString_clear_base(d->m_extraNamespaceDeclarations[i].m_namespaceUri);
        }
    }
    d->m_extraNamespaceDeclarationCount = 0;
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
    d->m_namespaceProcessing = true;
    d->m_extraNamespaceDeclarations = NULL;
    d->m_extraNamespaceDeclarationCount = 0;
    d->m_extraNamespaceDeclarationCapacity = 0;
    d->m_type = XXmlStream_NoToken;
    d->m_error = XXmlStream_NoError;
    d->m_attributes = XXmlStreamAttributes_create();
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
    if (d->m_attributes) {
        XXmlStreamAttributes_delete(d->m_attributes);
        d->m_attributes = NULL;
    }
    if (d->m_extraNamespaceDeclarations)
    {
        for (int i = 0; i < d->m_extraNamespaceDeclarationCount; i++)
        {
            if (d->m_extraNamespaceDeclarations[i].m_prefix) XString_delete_base(d->m_extraNamespaceDeclarations[i].m_prefix);
            if (d->m_extraNamespaceDeclarations[i].m_namespaceUri) XString_delete_base(d->m_extraNamespaceDeclarations[i].m_namespaceUri);
        }
        XFree_System(d->m_extraNamespaceDeclarations);
        d->m_extraNamespaceDeclarations = NULL;
        d->m_extraNamespaceDeclarationCount = 0;
        d->m_extraNamespaceDeclarationCapacity = 0;
    }
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
int XXmlStreamReader_readNext(XXmlStreamReader* self)
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
    /* 处理空元素后的 EndElement —— 必须先于 clear_current_token，否则 m_isEmptyElement 被清掉 */
    if (d->m_type == XXmlStream_StartElement && d->m_isEmptyElement)
    {
        d->m_type = XXmlStream_EndElement;
        d->m_isEmptyElement = false;
        /* 同时弹出标签栈以保持与 parse_end_element 一致的语义 */
        if (d->m_tagStackSize > 0)
        {
            XmlTag* tag = &d->m_tagStack[d->m_tagStackSize - 1];
            XString_delete_base(tag->m_name);
            XString_delete_base(tag->m_qualifiedName);
            XString_delete_base(tag->m_prefix);
            XString_delete_base(tag->m_namespaceUri);
            memset(tag, 0, sizeof(XmlTag));
            d->m_tagStackSize--;
        }
        return XXmlStream_EndElement;
    }
    /* 清理当前 Token */
    clear_current_token(d);
    /* 设置读取指针 */
    const char* ptr = d->m_readPtr;
    const char* end = d->m_endPtr;
    /* token_start 将在跳过空白后设置为真正的 token 起始位置 */
    const char* token_start = NULL;
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
        /* 检测未关闭标签 */
        if (d->m_tagStackSize > 0)
        {
            d->m_type = XXmlStream_EndDocument;
            set_error(d, XXmlStream_PrematureEndOfDocumentError, "文档提前结束：存在未关闭的标签。");
            return XXmlStream_Invalid;
        }
        if (d->m_type != XXmlStream_EndDocument)
        {
            d->m_type = XXmlStream_EndDocument;
            return XXmlStream_EndDocument;
        }
        return XXmlStream_NoToken;
    }
    /* 跳过空白 */
    ptr = skip_whitespace(ptr, end);
    if (ptr >= end)
    {
        d->m_readPtr = ptr;
        d->m_atEnd = true;
        /* 检测未关闭标签 */
        if (d->m_tagStackSize > 0)
        {
            d->m_type = XXmlStream_EndDocument;
            set_error(d, XXmlStream_PrematureEndOfDocumentError, "文档提前结束：存在未关闭的标签。");
            return XXmlStream_Invalid;
        }
        d->m_type = XXmlStream_EndDocument;
        return XXmlStream_EndDocument;
    }
    /* token_start 为跳过空白后的第一个非空白字符位置 */
    token_start = ptr;
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
    /* 更新行号、字符偏移：基于 token 起始位置 */
    if (d->m_data && token_start && token_start >= d->m_data) {
        d->m_lineNumber = count_newlines(d->m_data, token_start);
        d->m_characterOffset = (int64_t)(token_start - d->m_data);
        /* 最近一个换行符之后的位置（用于列号） */
        const char* nl = token_start;
        const char* base = d->m_data;
        while (nl > base) {
            const char* prev = nl - 1;
            if (*prev == '\n') { break; }
            if (*prev == '\r') { break; }
            nl--;
            if (nl == base) break;
        }
        d->m_lastLineStart = (int64_t)(nl - base);
        /* 列号：1-based */
        d->m_tokenColumn = (int64_t)(token_start - nl) + 1;
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
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_SIZE);
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
    if (d->m_errorString) XString_clear_base(d->m_errorString);
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
void XXmlStreamReader_addData_utf8(XXmlStreamReader* self, const char* data)
{
    if (ISNULL(self, "XXmlStreamReader") || !data)
        return;
    size_t length = strlen(data);
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
void XXmlStreamReader_addData(XXmlStreamReader* self, const XByteArray* data)
{
    if (ISNULL(self, "XXmlStreamReader") || !data)
        return;
    const char* ptr = (const char*)XByteArray_data(data);
    size_t len = XByteArray_size_base(data);
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (d->m_device)
    {
        /* 有设备时忽略 addData */
        return;
    }
    d->m_data = ptr;
    d->m_dataLength = len;
    d->m_readPtr = ptr;
    d->m_endPtr = ptr + len;
    d->m_isDataFromDevice = false;
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

int XXmlStreamReader_tokenType(const XXmlStreamReader* self)
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
    /* 优先返回 token 起始列 */
    return d->m_tokenColumn;
}

int64_t XXmlStreamReader_characterOffset(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return 0;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_characterOffset;
}

bool XXmlStreamReader_hasStandaloneDeclaration(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_hasStandalone;
}

const char* XXmlStreamReader_processingInstructionTarget(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_processingInstructionTarget ? XString_toUtf8(d->m_processingInstructionTarget) : "";
}

const char* XXmlStreamReader_processingInstructionData(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_processingInstructionData ? XString_toUtf8(d->m_processingInstructionData) : "";
}

void XXmlStreamReader_setNamespaceProcessing(XXmlStreamReader* self, bool enable)
{
    if (ISNULL(self, "XXmlStreamReader")) return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    d->m_namespaceProcessing = enable;
}

bool XXmlStreamReader_namespaceProcessing(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_namespaceProcessing;
}

static void ensure_extra_decl_capacity(XXmlStreamReaderPrivate* d, int needed)
{
    if (d->m_extraNamespaceDeclarationCapacity >= needed) return;
    int newCap = d->m_extraNamespaceDeclarationCapacity == 0 ? 4 : d->m_extraNamespaceDeclarationCapacity;
    while (newCap < needed) newCap *= 2;
    XmlNamespaceDeclaration* arr = (XmlNamespaceDeclaration*)XRealloc_System(
        d->m_extraNamespaceDeclarations, (size_t)newCap * sizeof(XmlNamespaceDeclaration));
    if (arr) {
        d->m_extraNamespaceDeclarations = arr;
        d->m_extraNamespaceDeclarationCapacity = newCap;
    }
}

void XXmlStreamReader_addExtraNamespaceDeclaration(XXmlStreamReader* self,
    const XXmlStreamNamespaceDeclaration* extraDecl)
{
    if (ISNULL(self, "XXmlStreamReader") || !extraDecl) return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    ensure_extra_decl_capacity(d, d->m_extraNamespaceDeclarationCount + 1);
    if (!d->m_extraNamespaceDeclarations) return;
    XmlNamespaceDeclaration* slot = &d->m_extraNamespaceDeclarations[d->m_extraNamespaceDeclarationCount];
    slot->m_prefix = XString_create();
    slot->m_namespaceUri = XString_create();
    if (extraDecl->m_prefix)
        XString_assign_utf8(slot->m_prefix, XString_toUtf8(extraDecl->m_prefix));
    if (extraDecl->m_namespaceUri)
        XString_assign_utf8(slot->m_namespaceUri, XString_toUtf8(extraDecl->m_namespaceUri));
    d->m_extraNamespaceDeclarationCount++;
}

void XXmlStreamReader_addExtraNamespaceDeclarations(XXmlStreamReader* self,
    const XXmlStreamNamespaceDeclaration* extraDecls, int count)
{
    if (ISNULL(self, "XXmlStreamReader") || !extraDecls || count <= 0) return;
    for (int i = 0; i < count; i++) {
        XXmlStreamReader_addExtraNamespaceDeclaration(self, &extraDecls[i]);
    }
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

/* ============================================================================
 * XXmlStreamAttribute 实现
 * ============================================================================ */

XXmlStreamAttribute* XXmlStreamAttribute_create(const char* qualifiedName, const char* value)
{
    XXmlStreamAttribute* self = (XXmlStreamAttribute*)XMalloc_System(sizeof(XXmlStreamAttribute));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamAttribute));
    self->m_qualifiedName = XString_create_utf8(qualifiedName);
    self->m_value = XString_create_utf8(value);
    return self;
}

XXmlStreamAttribute* XXmlStreamAttribute_create_ex(const char* namespaceUri, const char* name, const char* value)
{
    XXmlStreamAttribute* self = (XXmlStreamAttribute*)XMalloc_System(sizeof(XXmlStreamAttribute));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamAttribute));
    self->m_namespaceUri = XString_create_utf8(namespaceUri);
    self->m_name = XString_create_utf8(name);
    /* 无前缀时，qualifiedName 等于 name（带前缀时上层负责补齐） */
    self->m_qualifiedName = XString_create_utf8(name);
    self->m_value = XString_create_utf8(value);
    return self;
}

void XXmlStreamAttribute_delete(XXmlStreamAttribute* self)
{
    if (!self) return;
    if (self->m_namespaceUri) XString_delete_base(self->m_namespaceUri);
    if (self->m_name) XString_delete_base(self->m_name);
    if (self->m_qualifiedName) XString_delete_base(self->m_qualifiedName);
    if (self->m_value) XString_delete_base(self->m_value);
    XFree_System(self);
}

XXmlStreamAttributes* XXmlStreamAttributes_create(void)
{
    XXmlStreamAttributes* self = (XXmlStreamAttributes*)XMalloc_System(sizeof(XXmlStreamAttributes));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamAttributes));
    self->m_items = NULL;
    self->m_count = 0;
    self->m_capacity = 0;
    return self;
}

void XXmlStreamAttributes_delete(XXmlStreamAttributes* self)
{
    if (!self) return;
    for (int i = 0; i < self->m_count; i++)
        if (self->m_items[i]) XXmlStreamAttribute_delete(self->m_items[i]);
    if (self->m_items) XFree_System(self->m_items);
    XFree_System(self);
}


/* ============================================================================
 * XXmlStreamAttribute getter 实现
 * ============================================================================ */

const char* XXmlStreamAttribute_namespaceUri(const XXmlStreamAttribute* self)
{
    if (!self) return "";
    return self->m_namespaceUri ? XString_toUtf8(self->m_namespaceUri) : "";
}

const char* XXmlStreamAttribute_name(const XXmlStreamAttribute* self)
{
    if (!self) return "";
    return self->m_name ? XString_toUtf8(self->m_name) : "";
}

const char* XXmlStreamAttribute_qualifiedName(const XXmlStreamAttribute* self)
{
    if (!self) return "";
    return self->m_qualifiedName ? XString_toUtf8(self->m_qualifiedName) : "";
}

const char* XXmlStreamAttribute_prefix(const XXmlStreamAttribute* self)
{
    if (!self || !self->m_qualifiedName) return "";
    const char* qn = XString_toUtf8(self->m_qualifiedName);
    const char* colon = strchr(qn, ':');
    if (!colon) return "";
    // Use thread-local static buffer
    static __thread char s_prefixBuf[256];
    size_t len = (size_t)(colon - qn);
    if (len >= sizeof(s_prefixBuf)) len = sizeof(s_prefixBuf) - 1;
    memcpy(s_prefixBuf, qn, len);
    s_prefixBuf[len] = '\0';
    return s_prefixBuf;
}

const char* XXmlStreamAttribute_value(const XXmlStreamAttribute* self)
{
    if (!self) return "";
    return self->m_value ? XString_toUtf8(self->m_value) : "";
}

bool XXmlStreamAttribute_isDefault(const XXmlStreamAttribute* self)
{
    return self ? self->m_isDefault : false;
}

/* ============================================================================
 * XXmlStreamAttributes getter 实现
 * ============================================================================ */

int XXmlStreamAttributes_size(const XXmlStreamAttributes* self)
{
    return self ? self->m_count : 0;
}

const XXmlStreamAttribute* XXmlStreamAttributes_at(const XXmlStreamAttributes* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return self->m_items[index];
}

const char* XXmlStreamAttributes_value(const XXmlStreamAttributes* self, const char* qualifiedName)
{
    if (!self || !qualifiedName) return "";
    for (int i = 0; i < self->m_count; i++) {
        if (self->m_items[i] && self->m_items[i]->m_qualifiedName) {
            if (strcmp(XString_toUtf8(self->m_items[i]->m_qualifiedName), qualifiedName) == 0)
                return self->m_items[i]->m_value ? XString_toUtf8(self->m_items[i]->m_value) : "";
        }
    }
    return "";
}

const char* XXmlStreamAttributes_value_ex(const XXmlStreamAttributes* self, const char* namespaceUri, const char* name)
{
    if (!self || !name) return "";
    for (int i = 0; i < self->m_count; i++) {
        if (!self->m_items[i]) continue;
        if (self->m_items[i]->m_name && strcmp(XString_toUtf8(self->m_items[i]->m_name), name) == 0) {
            if (!namespaceUri || !namespaceUri[0]) 
                return self->m_items[i]->m_value ? XString_toUtf8(self->m_items[i]->m_value) : "";
            if (self->m_items[i]->m_namespaceUri && strcmp(XString_toUtf8(self->m_items[i]->m_namespaceUri), namespaceUri) == 0)
                return self->m_items[i]->m_value ? XString_toUtf8(self->m_items[i]->m_value) : "";
        }
    }
    return "";
}

bool XXmlStreamAttributes_hasAttribute(const XXmlStreamAttributes* self, const char* qualifiedName)
{
    if (!self || !qualifiedName) return false;
    for (int i = 0; i < self->m_count; i++) {
        if (self->m_items[i] && self->m_items[i]->m_qualifiedName) {
            if (strcmp(XString_toUtf8(self->m_items[i]->m_qualifiedName), qualifiedName) == 0)
                return true;
        }
    }
    return false;
}

void XXmlStreamAttributes_append(XXmlStreamAttributes* self, const char* namespaceUri, const char* name, const char* value)
{
    if (!self) return;
    XXmlStreamAttribute* attr = XXmlStreamAttribute_create_ex(namespaceUri, name, value);
    if (!attr) return;
    if (self->m_count >= self->m_capacity) {
        int newCap = self->m_capacity ? self->m_capacity * 2 : 4;
        XXmlStreamAttribute** newItems = (XXmlStreamAttribute**)XRealloc_System(self->m_items, sizeof(XXmlStreamAttribute*) * newCap);
        if (!newItems) { XXmlStreamAttribute_delete(attr); return; }
        self->m_items = newItems;
        self->m_capacity = newCap;
    }
    self->m_items[self->m_count++] = attr;
}

void XXmlStreamAttributes_append_ex(XXmlStreamAttributes* self, const char* qualifiedName, const char* value)
{
    if (!self) return;
    XXmlStreamAttribute* attr = XXmlStreamAttribute_create(qualifiedName, value);
    if (!attr) return;
    if (self->m_count >= self->m_capacity) {
        int newCap = self->m_capacity ? self->m_capacity * 2 : 4;
        XXmlStreamAttribute** newItems = (XXmlStreamAttribute**)XRealloc_System(self->m_items, sizeof(XXmlStreamAttribute*) * newCap);
        if (!newItems) { XXmlStreamAttribute_delete(attr); return; }
        self->m_items = newItems;
        self->m_capacity = newCap;
    }
    self->m_items[self->m_count++] = attr;
}

/* ============================================================================
 * XXmlStreamNamespaceDeclaration 实现
 * ============================================================================ */

XXmlStreamNamespaceDeclaration* XXmlStreamNamespaceDeclaration_create(const char* prefix, const char* namespaceUri)
{
    XXmlStreamNamespaceDeclaration* self = (XXmlStreamNamespaceDeclaration*)XMalloc_System(sizeof(XXmlStreamNamespaceDeclaration));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamNamespaceDeclaration));
    self->m_prefix = XString_create_utf8(prefix);
    self->m_namespaceUri = XString_create_utf8(namespaceUri);
    return self;
}

void XXmlStreamNamespaceDeclaration_delete(XXmlStreamNamespaceDeclaration* self)
{
    if (!self) return;
    if (self->m_prefix) XString_delete_base(self->m_prefix);
    if (self->m_namespaceUri) XString_delete_base(self->m_namespaceUri);
    XFree_System(self);
}

const char* XXmlStreamNamespaceDeclaration_prefix(const XXmlStreamNamespaceDeclaration* self)
{
    if (!self) return "";
    return self->m_prefix ? XString_toUtf8(self->m_prefix) : "";
}

const char* XXmlStreamNamespaceDeclaration_namespaceUri(const XXmlStreamNamespaceDeclaration* self)
{
    if (!self) return "";
    return self->m_namespaceUri ? XString_toUtf8(self->m_namespaceUri) : "";
}

/* ============================================================================
 * XXmlStreamReader getter 实现
 * ============================================================================ */

const char* XXmlStreamReader_namespaceUri(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_namespaceUri ? XString_toUtf8(d->m_namespaceUri) : "";
}

const char* XXmlStreamReader_name(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_name ? XString_toUtf8(d->m_name) : "";
}

const char* XXmlStreamReader_qualifiedName(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_qualifiedName ? XString_toUtf8(d->m_qualifiedName) : "";
}

const char* XXmlStreamReader_prefix(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_prefix ? XString_toUtf8(d->m_prefix) : "";
}

const char* XXmlStreamReader_text(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_text ? XString_toUtf8(d->m_text) : "";
}

const XXmlStreamAttributes* XXmlStreamReader_attributes(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_attributes;
}

int XXmlStreamReader_namespaceDeclarationsCount(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return 0;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_namespaceDeclarationCount;
}

const XXmlStreamNamespaceDeclaration* XXmlStreamReader_namespaceDeclaration(const XXmlStreamReader* self, int index)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (index < 0 || index >= d->m_namespaceDeclarationCount) return NULL;
    return &d->m_namespaceDeclarations[index];
}

bool XXmlStreamReader_hasNamespaceDeclarations(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_namespaceDeclarationCount > 0;
}

const char* XXmlStreamReader_dtdName(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_dtdName ? XString_toUtf8(d->m_dtdName) : "";
}

const char* XXmlStreamReader_dtdPublicId(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_dtdPublicId ? XString_toUtf8(d->m_dtdPublicId) : "";
}

const char* XXmlStreamReader_dtdSystemId(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_dtdSystemId ? XString_toUtf8(d->m_dtdSystemId) : "";
}

const char* XXmlStreamReader_documentVersion(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_documentVersion ? XString_toUtf8(d->m_documentVersion) : "";
}

const char* XXmlStreamReader_documentEncoding(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_documentEncoding ? XString_toUtf8(d->m_documentEncoding) : "";
}

bool XXmlStreamReader_isStandaloneDocument(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_isStandaloneDocument;
}


int XXmlStreamReader_error(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return 0;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return (int)d->m_error;
}

const char* XXmlStreamReader_errorString(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_errorString ? XString_toUtf8(d->m_errorString) : "";
}

bool XXmlStreamReader_hasError(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_error != XXmlStream_NoError;
}

void XXmlStreamReader_raiseError(XXmlStreamReader* self, const char* message)
{
    if (ISNULL(self, "XXmlStreamReader")) return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    set_error(d, XXmlStream_CustomError, message);
}

int XXmlStreamReader_entityExpansionLimit(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return 0;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_entityExpansionLimit;
}

void XXmlStreamReader_setEntityExpansionLimit(XXmlStreamReader* self, int limit)
{
    if (ISNULL(self, "XXmlStreamReader")) return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    d->m_entityExpansionLimit = limit;
}

bool XXmlStreamReader_readNextStartElement(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return false;
    while (XXmlStreamReader_readNext(self) != XXmlStream_Invalid) {
        if (XXmlStreamReader_isStartElement(self)) return true;
        if (XXmlStreamReader_isEndDocument(self)) return false;
    }
    return false;
}

void XXmlStreamReader_skipCurrentElement(XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return;
    int depth = 1;
    while (depth > 0 && XXmlStreamReader_readNext(self) != XXmlStream_Invalid) {
        if (XXmlStreamReader_isStartElement(self)) depth++;
        else if (XXmlStreamReader_isEndElement(self)) depth--;
    }
}

const char* XXmlStreamReader_readElementText(XXmlStreamReader* self, int behaviour)
{
    (void)behaviour;
    if (ISNULL(self, "XXmlStreamReader")) return "";
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    // Read until we get characters or end element
    while (XXmlStreamReader_readNext(self) != XXmlStream_Invalid) {
        if (XXmlStreamReader_isCharacters(self))
            return d->m_text ? XString_toUtf8(d->m_text) : "";
        if (XXmlStreamReader_isEndElement(self))
            return "";
        if (XXmlStreamReader_isStartElement(self)) {
            if (behaviour == XXmlStream_ReadElementTextBehaviour_ErrorOnUnexpectedElement) {
                set_error(d, XXmlStream_UnexpectedElementError, "Unexpected element while reading element text");
                return "";
            }
            if (behaviour == XXmlStream_ReadElementTextBehaviour_SkipChildElements) {
                XXmlStreamReader_skipCurrentElement(self);
                continue;
            }
            // IncludeChildElements - fall through
            break;
        }
    }
    return "";
}

/* ============================================================================
 * DTD 符号声明实现（对标 QXmlStreamNotationDeclaration）
 * ============================================================================ */

XXmlStreamNotationDeclaration* XXmlStreamNotationDeclaration_create(void)
{
    XXmlStreamNotationDeclaration* self = (XXmlStreamNotationDeclaration*)XMalloc_System(sizeof(XXmlStreamNotationDeclaration));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamNotationDeclaration));
    XXmlStreamNotationDeclaration_init(self);
    return self;
}

void XXmlStreamNotationDeclaration_init(XXmlStreamNotationDeclaration* self)
{
    if (!self) return;
    self->m_name = XString_create();
    self->m_systemId = XString_create();
    self->m_publicId = XString_create();
}

void XXmlStreamNotationDeclaration_delete(XXmlStreamNotationDeclaration* self)
{
    if (!self) return;
    if (self->m_name) { XString_delete_base(self->m_name); }
    if (self->m_systemId) { XString_delete_base(self->m_systemId); }
    if (self->m_publicId) { XString_delete_base(self->m_publicId); }
    XFree_System(self);
}

const char* XXmlStreamNotationDeclaration_name(const XXmlStreamNotationDeclaration* self)
{
    if (!self || !self->m_name) return "";
    return XString_toUtf8(self->m_name);
}

const char* XXmlStreamNotationDeclaration_systemId(const XXmlStreamNotationDeclaration* self)
{
    if (!self || !self->m_systemId) return "";
    return XString_toUtf8(self->m_systemId);
}

const char* XXmlStreamNotationDeclaration_publicId(const XXmlStreamNotationDeclaration* self)
{
    if (!self || !self->m_publicId) return "";
    return XString_toUtf8(self->m_publicId);
}

/* ============================================================================
 * DTD 实体声明实现（对标 QXmlStreamEntityDeclaration）
 * ============================================================================ */

XXmlStreamEntityDeclaration* XXmlStreamEntityDeclaration_create(void)
{
    XXmlStreamEntityDeclaration* self = (XXmlStreamEntityDeclaration*)XMalloc_System(sizeof(XXmlStreamEntityDeclaration));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamEntityDeclaration));
    XXmlStreamEntityDeclaration_init(self);
    return self;
}

void XXmlStreamEntityDeclaration_init(XXmlStreamEntityDeclaration* self)
{
    if (!self) return;
    self->m_name = XString_create();
    self->m_notationName = XString_create();
    self->m_systemId = XString_create();
    self->m_publicId = XString_create();
    self->m_value = XString_create();
}

void XXmlStreamEntityDeclaration_delete(XXmlStreamEntityDeclaration* self)
{
    if (!self) return;
    if (self->m_name) { XString_delete_base(self->m_name); }
    if (self->m_notationName) { XString_delete_base(self->m_notationName); }
    if (self->m_systemId) { XString_delete_base(self->m_systemId); }
    if (self->m_publicId) { XString_delete_base(self->m_publicId); }
    if (self->m_value) { XString_delete_base(self->m_value); }
    XFree_System(self);
}

const char* XXmlStreamEntityDeclaration_name(const XXmlStreamEntityDeclaration* self)
{
    if (!self || !self->m_name) return "";
    return XString_toUtf8(self->m_name);
}

const char* XXmlStreamEntityDeclaration_notationName(const XXmlStreamEntityDeclaration* self)
{
    if (!self || !self->m_notationName) return "";
    return XString_toUtf8(self->m_notationName);
}

const char* XXmlStreamEntityDeclaration_systemId(const XXmlStreamEntityDeclaration* self)
{
    if (!self || !self->m_systemId) return "";
    return XString_toUtf8(self->m_systemId);
}

const char* XXmlStreamEntityDeclaration_publicId(const XXmlStreamEntityDeclaration* self)
{
    if (!self || !self->m_publicId) return "";
    return XString_toUtf8(self->m_publicId);
}

const char* XXmlStreamEntityDeclaration_value(const XXmlStreamEntityDeclaration* self)
{
    if (!self || !self->m_value) return "";
    return XString_toUtf8(self->m_value);
}

/* ============================================================================
 * DTD 声明列表实现
 * ============================================================================ */

XXmlStreamNotationDeclarations* XXmlStreamNotationDeclarations_create(void)
{
    XXmlStreamNotationDeclarations* self = (XXmlStreamNotationDeclarations*)XMalloc_System(sizeof(XXmlStreamNotationDeclarations));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamNotationDeclarations));
    return self;
}

void XXmlStreamNotationDeclarations_delete(XXmlStreamNotationDeclarations* self)
{
    if (!self) return;
    if (self->m_declarations) {
        for (size_t i = 0; i < self->m_count; i++) {
            if (self->m_declarations[i].m_name) { XString_delete_base(self->m_declarations[i].m_name); }
            if (self->m_declarations[i].m_systemId) { XString_delete_base(self->m_declarations[i].m_systemId); }
            if (self->m_declarations[i].m_publicId) { XString_delete_base(self->m_declarations[i].m_publicId); }
        }
        XFree_System(self->m_declarations);
    }
    XFree_System(self);
}

size_t XXmlStreamNotationDeclarations_size(const XXmlStreamNotationDeclarations* self)
{
    return self ? self->m_count : 0;
}

const XXmlStreamNotationDeclaration* XXmlStreamNotationDeclarations_at(const XXmlStreamNotationDeclarations* self, size_t index)
{
    if (!self || index >= self->m_count) return NULL;
    return &self->m_declarations[index];
}

XXmlStreamEntityDeclarations* XXmlStreamEntityDeclarations_create(void)
{
    XXmlStreamEntityDeclarations* self = (XXmlStreamEntityDeclarations*)XMalloc_System(sizeof(XXmlStreamEntityDeclarations));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamEntityDeclarations));
    return self;
}

void XXmlStreamEntityDeclarations_delete(XXmlStreamEntityDeclarations* self)
{
    if (!self) return;
    if (self->m_declarations) {
        for (size_t i = 0; i < self->m_count; i++) {
            if (self->m_declarations[i].m_name) { XString_delete_base(self->m_declarations[i].m_name); }
            if (self->m_declarations[i].m_notationName) { XString_delete_base(self->m_declarations[i].m_notationName); }
            if (self->m_declarations[i].m_systemId) { XString_delete_base(self->m_declarations[i].m_systemId); }
            if (self->m_declarations[i].m_publicId) { XString_delete_base(self->m_declarations[i].m_publicId); }
            if (self->m_declarations[i].m_value) { XString_delete_base(self->m_declarations[i].m_value); }
        }
        XFree_System(self->m_declarations);
    }
    XFree_System(self);
}

size_t XXmlStreamEntityDeclarations_size(const XXmlStreamEntityDeclarations* self)
{
    return self ? self->m_count : 0;
}

const XXmlStreamEntityDeclaration* XXmlStreamEntityDeclarations_at(const XXmlStreamEntityDeclarations* self, size_t index)
{
    if (!self || index >= self->m_count) return NULL;
    return &self->m_declarations[index];
}

/* ============================================================================
 * 实体解析器实现（对标 QXmlStreamEntityResolver）
 * ============================================================================ */

XXmlStreamEntityResolver* XXmlStreamEntityResolver_create(void)
{
    XXmlStreamEntityResolver* self = (XXmlStreamEntityResolver*)XMalloc_System(sizeof(XXmlStreamEntityResolver));
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamEntityResolver));
    XXmlStreamEntityResolver_init(self);
    return self;
}

void XXmlStreamEntityResolver_init(XXmlStreamEntityResolver* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XXmlStreamEntityResolver));
}

void XXmlStreamEntityResolver_delete(XXmlStreamEntityResolver* self)
{
    XFree_System(self);
}

const char* XXmlStreamEntityResolver_resolveEntity(XXmlStreamEntityResolver* self,
    const char* publicId, const char* systemId)
{
    if (!self || !self->m_resolveEntityCallback) return NULL;
    return self->m_resolveEntityCallback(publicId, systemId, self->m_userData);
}

const char* XXmlStreamEntityResolver_resolveUndeclaredEntity(XXmlStreamEntityResolver* self,
    const char* name)
{
    if (!self || !self->m_resolveUndeclaredEntityCallback) return NULL;
    return self->m_resolveUndeclaredEntityCallback(name, self->m_userData);
}

void XXmlStreamEntityResolver_setUserData(XXmlStreamEntityResolver* self, void* userData)
{
    if (self) self->m_userData = userData;
}

void* XXmlStreamEntityResolver_userData(XXmlStreamEntityResolver* self)
{
    return self ? self->m_userData : NULL;
}

/* ============================================================================
 * XXmlStreamReader DTD 相关 API 实现
 * ============================================================================ */

/**
 * @brief      获取 DTD 符号声明列表
 * @note       对标 QXlsx::Document::copyStyle
 */
XXmlStreamNotationDeclarations* XXmlStreamReader_notationDeclarations(const XXmlStreamReader* self)
{
    if (!self) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)((const char*)self + sizeof(XXmlStreamReader));
    return d->m_notationDeclarations;
}

/**
 * @brief      获取 DTD 实体声明列表
 */
XXmlStreamEntityDeclarations* XXmlStreamReader_entityDeclarations(const XXmlStreamReader* self)
{
    if (!self) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)((const char*)self + sizeof(XXmlStreamReader));
    return d->m_entityDeclarations;
}

/**
 * @brief      设置实体解析器
 */
void XXmlStreamReader_setEntityResolver(XXmlStreamReader* self, XXmlStreamEntityResolver* resolver)
{
    if (!self) return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)((char*)self + sizeof(XXmlStreamReader));
    d->m_entityResolver = resolver;
}

/**
 * @brief      获取实体解析器
 */
XXmlStreamEntityResolver* XXmlStreamReader_entityResolver(const XXmlStreamReader* self)
{
    if (!self) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)((const char*)self + sizeof(XXmlStreamReader));
    return d->m_entityResolver;
}
