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
#include <stdlib.h> /* XClass 宏使用 ISO C exit 声明；XML 本身不调用平台 API。 */
#include <string.h>

/* ============================================================================
 * 内部常量定义
 * ============================================================================ */

/** @brief 实体扩展默认限制 */
#define DEFAULT_ENTITY_EXPANSION_LIMIT 4096

/** @brief 内部缓冲区初始大小 */
#define INITIAL_BUFFER_SIZE 4096

/** @brief 流结束标记 */
#define STREAM_EOF ((uint32_t)~0U)

static bool is_xml_ascii_digit(uint8_t value)
{
    return value >= (uint8_t)'0' && value <= (uint8_t)'9';
}

static bool is_xml_ascii_hex_digit(uint8_t value)
{
    return (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
           (value >= (uint8_t)'a' && value <= (uint8_t)'f') ||
           (value >= (uint8_t)'A' && value <= (uint8_t)'F');
}

enum
{
    XML_INPUT_UTF8 = 0,
    XML_INPUT_UTF16LE,
    XML_INPUT_UTF16BE,
    XML_INPUT_UTF32LE,
    XML_INPUT_UTF32BE,
    XML_INPUT_LATIN1,
    XML_INPUT_ASCII
};

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
    int m_namespaceBindingCountBefore; /**< 进入标签前的命名空间绑定数 */
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

/** @brief DTD ATTLIST 中可自动补入元素的默认属性。 */
typedef struct XmlDefaultAttribute
{
    XString* m_elementName;
    XString* m_attributeName;
    XString* m_value;
    bool m_required;
} XmlDefaultAttribute;

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
    XByteArray* m_ownedData;     /**< addData/设备输入的自有副本 */
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
    XmlNamespaceDeclaration* m_namespaceBindings;     /**< 当前作用域内全部命名空间绑定 */
    int m_namespaceBindingCount;                      /**< 有效绑定数量 */
    int m_namespaceBindingCapacity;                   /**< 绑定数组容量 */
    XXmlStreamNamespaceDeclarations m_namespaceDeclarationsView; /**< Qt 同名只读列表视图 */

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
    bool m_seenRootElement;      /**< 是否已经出现根元素 */
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

    /* ---------- DTD 默认属性 ---------- */
    XmlDefaultAttribute* m_defaultAttributes;
    int m_defaultAttributeCount;
    int m_defaultAttributeCapacity;

    /* ---------- 输入编码 ---------- */
    int m_inputEncoding;
    bool m_inputInitialized;
    uint8_t m_pendingInput[4];
    size_t m_pendingInputLength;
    uint16_t m_pendingUtf16High;
    bool m_hasPendingUtf16High;

    /* ---------- 文档状态 ---------- */
    bool m_startedDocument;
    bool m_seenXmlDeclaration;
    bool m_seenDtd;
    bool m_finishedRootElement;

    /* ---------- 内部缓冲区 ---------- */
    XString* m_buffer;           /**< 内部临时缓冲区 */
} XXmlStreamReaderPrivate;

/* ============================================================================
 * 内部辅助函数前向声明
 * ============================================================================ */

/** @brief 跳过空白字符 */
static const char* skip_whitespace(const char* ptr, const char* end);

/** @brief 检查字符是否为 XML 名称起始字符 */
static bool is_name_start_char(uint32_t c);

/** @brief 检查字符是否为 XML 名称字符 */
static bool is_name_char(uint32_t c);

/** @brief 解析 XML 名称 */
static bool parse_name(const char** ptr, const char* end, XString* out);

/** @brief 解析引号字符串 */
static bool parse_quoted_string(const char** ptr, const char* end, XString* out);

static bool parse_attribute_value(XXmlStreamReaderPrivate* d,
                                  const char** ptr, const char* end, XString* out);
static bool expand_entity_value(XXmlStreamReaderPrivate* d, const char* value,
                                XString* output, int depth);
static bool decode_utf8_codepoint(const char* ptr, const char* end,
                                  uint32_t* codepoint, size_t* length);
static bool is_xml_char(uint32_t codepoint);
static bool is_valid_utf8_xml(const char* data, size_t length);
static bool append_codepoint_utf8(XString* output, uint32_t codepoint);
static bool normalize_input(XXmlStreamReaderPrivate* d, const char* data, size_t length);
static void clear_default_attributes(XXmlStreamReaderPrivate* d);
static bool append_default_attribute(XXmlStreamReaderPrivate* d,
                                     const XString* elementName,
                                     const XString* attributeName,
                                     const XString* value,
                                     bool required);
static const XmlDefaultAttribute* find_default_attribute(
    const XXmlStreamReaderPrivate* d, const char* elementName, const char* attributeName);

/** @brief 解析 XML 声明（<?xml ... ?>） */
static bool parse_xml_declaration(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);
static int encoding_name_to_input(const char* encoding);
static bool reencode_existing_single_byte(XXmlStreamReaderPrivate* d, bool asciiOnly);

/** @brief 解析注释 */
static bool parse_comment(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析处理指令 */
static bool parse_processing_instruction(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析 CDATA 段 */
static bool parse_cdata(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

/** @brief 解析 DTD */
static bool parse_dtd(XXmlStreamReaderPrivate* d, const char** ptr, const char* end);

static void clear_dtd_declarations(XXmlStreamReaderPrivate* d);
static bool parse_dtd_subset(XXmlStreamReaderPrivate* d, const char* start, const char* end);

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
static bool parse_namespace_declaration(XXmlStreamReaderPrivate* d, const XString* attrName, const XString* attrValue);

/** @brief 清理当前 Token 数据 */
static void clear_current_token(XXmlStreamReaderPrivate* d);

/** @brief 设置错误 */
static void set_error(XXmlStreamReaderPrivate* d, XXmlStreamError error, const char* message);

/** @brief 初始化私有数据 */
static void private_init(XXmlStreamReaderPrivate* d);

/** @brief 释放私有数据 */
static void private_deinit(XXmlStreamReaderPrivate* d);

static void truncate_namespace_bindings(XXmlStreamReaderPrivate* d, int count);

static const char* namespace_for_prefix(const XXmlStreamReaderPrivate* d, const char* prefix);

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

static bool decode_utf8_codepoint(const char* ptr, const char* end,
                                  uint32_t* codepoint, size_t* length)
{
    if (!ptr || ptr >= end || !codepoint || !length) return false;
    const uint8_t* bytes = (const uint8_t*)ptr;
    uint32_t value = 0;
    size_t count = 0;
    if (bytes[0] <= 0x7f) {
        value = bytes[0];
        count = 1;
    } else if (bytes[0] >= 0xc2 && bytes[0] <= 0xdf) {
        value = bytes[0] & 0x1fU;
        count = 2;
    } else if (bytes[0] >= 0xe0 && bytes[0] <= 0xef) {
        value = bytes[0] & 0x0fU;
        count = 3;
    } else if (bytes[0] >= 0xf0 && bytes[0] <= 0xf4) {
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
        value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU))
        return false;
    *codepoint = value;
    *length = count;
    return true;
}

static bool is_xml_char(uint32_t codepoint)
{
    return codepoint == 0x9U || codepoint == 0xaU || codepoint == 0xdU ||
           (codepoint >= 0x20U && codepoint <= 0xd7ffU) ||
           (codepoint >= 0xe000U && codepoint <= 0xfffdU) ||
           (codepoint >= 0x10000U && codepoint <= 0x10ffffU);
}

static bool is_valid_utf8_xml(const char* data, size_t length)
{
    if (!data && length > 0) return false;
    const char* ptr = data;
    const char* end = data ? data + length : data;
    while (ptr < end) {
        uint32_t codepoint;
        size_t byteLength;
        if (!decode_utf8_codepoint(ptr, end, &codepoint, &byteLength) ||
            !is_xml_char(codepoint)) return false;
        ptr += byteLength;
    }
    return true;
}

static int encoding_name_to_input(const char* encoding)
{
    if (!encoding || !*encoding) return -1;
    char normalized[32];
    size_t length = strlen(encoding);
    if (length >= sizeof(normalized)) return -1;
    size_t out = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)encoding[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') return -1;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        normalized[out++] = (char)c;
    }
    normalized[out] = '\0';

    if (strcmp(normalized, "UTF-8") == 0 || strcmp(normalized, "UTF8") == 0)
        return XML_INPUT_UTF8;
    if (strcmp(normalized, "UTF-16") == 0 || strcmp(normalized, "UTF16") == 0 ||
        strcmp(normalized, "UTF-16LE") == 0 || strcmp(normalized, "UTF16LE") == 0)
        return XML_INPUT_UTF16LE;
    if (strcmp(normalized, "UTF-16BE") == 0 || strcmp(normalized, "UTF16BE") == 0)
        return XML_INPUT_UTF16BE;
    if (strcmp(normalized, "UTF-32") == 0 || strcmp(normalized, "UTF32") == 0 ||
        strcmp(normalized, "UTF-32LE") == 0 || strcmp(normalized, "UTF32LE") == 0)
        return XML_INPUT_UTF32LE;
    if (strcmp(normalized, "UTF-32BE") == 0 || strcmp(normalized, "UTF32BE") == 0)
        return XML_INPUT_UTF32BE;
    if (strcmp(normalized, "ISO-8859-1") == 0 ||
        strcmp(normalized, "ISO8859-1") == 0 ||
        strcmp(normalized, "LATIN1") == 0)
        return XML_INPUT_LATIN1;
    if (strcmp(normalized, "US-ASCII") == 0 || strcmp(normalized, "ASCII") == 0)
        return XML_INPUT_ASCII;
    return -1;
}

static bool encoding_name_matches_input(const XXmlStreamReaderPrivate* d, const char* encoding)
{
    if (!d || !encoding || !*encoding) return false;
    /* QString/addData 输入已经转换为 UTF-8，Qt 会锁定解码器编码。 */
    if (d->m_lockEncoding) return true;
    int declared = encoding_name_to_input(encoding);
    if (declared < 0) return false;
    if (declared == d->m_inputEncoding) return true;
    if ((d->m_inputEncoding == XML_INPUT_UTF16LE ||
         d->m_inputEncoding == XML_INPUT_UTF16BE) && declared == XML_INPUT_UTF16LE)
        return true;
    if ((d->m_inputEncoding == XML_INPUT_UTF32LE ||
         d->m_inputEncoding == XML_INPUT_UTF32BE) && declared == XML_INPUT_UTF32LE)
        return true;
    /* 无 BOM 的单字节编码在声明解析前按字节暂存，允许稍后切换解码器。 */
    return d->m_inputEncoding == XML_INPUT_UTF8 &&
           (declared == XML_INPUT_LATIN1 || declared == XML_INPUT_ASCII);
}

static bool append_codepoint_utf8(XString* output, uint32_t codepoint)
{
    if (!output || !is_xml_char(codepoint)) return false;
    char utf8[4];
    size_t length = 0;
    if (codepoint <= 0x7fU) {
        utf8[0] = (char)codepoint;
        length = 1;
    } else if (codepoint <= 0x7ffU) {
        utf8[0] = (char)(0xc0U | (codepoint >> 6));
        utf8[1] = (char)(0x80U | (codepoint & 0x3fU));
        length = 2;
    } else if (codepoint <= 0xffffU) {
        utf8[0] = (char)(0xe0U | (codepoint >> 12));
        utf8[1] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
        utf8[2] = (char)(0x80U | (codepoint & 0x3fU));
        length = 3;
    } else {
        utf8[0] = (char)(0xf0U | (codepoint >> 18));
        utf8[1] = (char)(0x80U | ((codepoint >> 12) & 0x3fU));
        utf8[2] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
        utf8[3] = (char)(0x80U | (codepoint & 0x3fU));
        length = 4;
    }
    return XString_append_with_length_utf8(output, utf8, length);
}

/**
 * @brief      检查字符是否为 XML 名称起始字符
 * @param c   字符
 * @return     是名称起始字符返回 true
 */
static bool is_name_start_char(uint32_t c)
{
    return c == ':' || c == '_' || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || (c >= 0xc0U && c <= 0xd6U) ||
           (c >= 0xd8U && c <= 0xf6U) || (c >= 0xf8U && c <= 0x2ffU) ||
           (c >= 0x370U && c <= 0x37dU) || (c >= 0x37fU && c <= 0x1fffU) ||
           (c >= 0x200cU && c <= 0x200dU) || (c >= 0x2070U && c <= 0x218fU) ||
           (c >= 0x2c00U && c <= 0x2fefU) || (c >= 0x3001U && c <= 0xd7ffU) ||
           (c >= 0xf900U && c <= 0xfdcfU) || (c >= 0xfdf0U && c <= 0xfffdU) ||
           (c >= 0x10000U && c <= 0xeffffU);
}

/**
 * @brief      检查字符是否为 XML 名称字符
 * @param c   字符
 * @return     是名称字符返回 true
 */
static bool is_name_char(uint32_t c)
{
    return is_name_start_char(c) || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == 0xb7U || (c >= 0x300U && c <= 0x36fU) ||
           (c >= 0x203fU && c <= 0x2040U);
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
    uint32_t codepoint;
    size_t byteLength;
    if (*ptr >= end || !decode_utf8_codepoint(*ptr, end, &codepoint, &byteLength) ||
        !is_name_start_char(codepoint))
        return false;
    *ptr += byteLength;
    while (*ptr < end) {
        if (!decode_utf8_codepoint(*ptr, end, &codepoint, &byteLength) ||
            !is_name_char(codepoint)) break;
        *ptr += byteLength;
    }
    size_t len = (size_t)(*ptr - start);
    if (out)
        return XString_append_with_length_utf8(out, start, len);
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
    while (*ptr < end && **ptr != quote) {
        if (**ptr == '<') return false;
        uint32_t codepoint;
        size_t byteLength;
        if (!decode_utf8_codepoint(*ptr, end, &codepoint, &byteLength) ||
            !is_xml_char(codepoint)) return false;
        *ptr += byteLength;
    }
    if (*ptr >= end)
        return false;
    /* 追加剩余文本 */
    if (out && *ptr > start)
        if (!XString_append_with_length_utf8(out, start, (size_t)(*ptr - start))) return false;
    ++(*ptr); /* 跳过结束引号 */
    return true;
}

static bool parse_attribute_value(XXmlStreamReaderPrivate* d,
                                  const char** ptr, const char* end, XString* out)
{
    XString raw;
    XString_init(&raw);
    bool ok = parse_quoted_string(ptr, end, &raw);
    if (ok) {
        const char* value = XString_toUtf8(&raw);
        ok = value ? expand_entity_value(d, value, out, 0) : true;
    }
    XString_deinit_base(&raw);
    return ok;
}

static void clear_default_attributes(XXmlStreamReaderPrivate* d)
{
    if (!d || !d->m_defaultAttributes) return;
    for (int i = 0; i < d->m_defaultAttributeCount; ++i) {
        XString_delete_base(d->m_defaultAttributes[i].m_elementName);
        XString_delete_base(d->m_defaultAttributes[i].m_attributeName);
        XString_delete_base(d->m_defaultAttributes[i].m_value);
        memset(&d->m_defaultAttributes[i], 0, sizeof(XmlDefaultAttribute));
    }
    d->m_defaultAttributeCount = 0;
}

static bool append_default_attribute(XXmlStreamReaderPrivate* d,
                                     const XString* elementName,
                                     const XString* attributeName,
                                     const XString* value,
                                     bool required)
{
    if (!d || !elementName || !attributeName || !value) return false;
    for (int i = 0; i < d->m_defaultAttributeCount; ++i) {
        XmlDefaultAttribute* item = &d->m_defaultAttributes[i];
        if (XString_equals(item->m_elementName, elementName, XChar_CaseSensitive) &&
            XString_equals(item->m_attributeName, attributeName, XChar_CaseSensitive)) {
            XString_assign(item->m_value, value);
            item->m_required = required;
            return true;
        }
    }
    if (d->m_defaultAttributeCount >= d->m_defaultAttributeCapacity) {
        int capacity = d->m_defaultAttributeCapacity ? d->m_defaultAttributeCapacity * 2 : 8;
        XmlDefaultAttribute* items = (XmlDefaultAttribute*)XRealloc_System(
            d->m_defaultAttributes, (size_t)capacity * sizeof(XmlDefaultAttribute));
        if (!items) return false;
        memset(items + d->m_defaultAttributeCapacity, 0,
               (size_t)(capacity - d->m_defaultAttributeCapacity) * sizeof(XmlDefaultAttribute));
        d->m_defaultAttributes = items;
        d->m_defaultAttributeCapacity = capacity;
    }
    XmlDefaultAttribute* item = &d->m_defaultAttributes[d->m_defaultAttributeCount];
    item->m_elementName = XString_create_copy(elementName);
    item->m_attributeName = XString_create_copy(attributeName);
    item->m_value = XString_create_copy(value);
    item->m_required = required;
    if (!item->m_elementName || !item->m_attributeName || !item->m_value) {
        XString_delete_base(item->m_elementName);
        XString_delete_base(item->m_attributeName);
        XString_delete_base(item->m_value);
        memset(item, 0, sizeof(*item));
        return false;
    }
    ++d->m_defaultAttributeCount;
    return true;
}

static const XmlDefaultAttribute* find_default_attribute(
    const XXmlStreamReaderPrivate* d, const char* elementName, const char* attributeName)
{
    if (!d || !elementName || !attributeName) return NULL;
    for (int i = 0; i < d->m_defaultAttributeCount; ++i) {
        const XmlDefaultAttribute* item = &d->m_defaultAttributes[i];
        const char* element = XString_toUtf8(item->m_elementName);
        const char* attribute = XString_toUtf8(item->m_attributeName);
        if (element && attribute && strcmp(element, elementName) == 0 &&
            strcmp(attribute, attributeName) == 0) return item;
    }
    return NULL;
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
    bool hasVersion = false;
    bool hasEncoding = false;
    bool hasStandalone = false;
    *ptr = skip_whitespace(*ptr, end);
    while (*ptr < end) {
        if (*ptr + 1 < end && **ptr == '?' && *(*ptr + 1) == '>') {
            *ptr += 2;
            return hasVersion;
        }
        XString attrName;
        XString attrValue;
        XString_init(&attrName);
        XString_init(&attrValue);
        if (!parse_name(ptr, end, &attrName)) {
            XString_deinit_base(&attrName);
            XString_deinit_base(&attrValue);
            return false;
        }
        const char* name = XString_toUtf8(&attrName);
        *ptr = skip_whitespace(*ptr, end);
        if (*ptr >= end || **ptr != '=') {
            XString_deinit_base(&attrName);
            XString_deinit_base(&attrValue);
            return false;
        }
        ++(*ptr);
        *ptr = skip_whitespace(*ptr, end);
        if (!parse_quoted_string(ptr, end, &attrValue)) {
            XString_deinit_base(&attrName);
            XString_deinit_base(&attrValue);
            return false;
        }
        const char* value = XString_toUtf8(&attrValue);
        bool valid = name && value && strchr(value, '&') == NULL;
        if (valid && strcmp(name, "version") == 0) {
            if (hasVersion || hasEncoding || hasStandalone ||
                (strcmp(value, "1.0") != 0 && strcmp(value, "1.1") != 0)) valid = false;
            if (valid) {
                XString_assign_utf8(d->m_documentVersion, value);
                hasVersion = true;
            }
        } else if (valid && strcmp(name, "encoding") == 0) {
            if (!hasVersion || hasEncoding || hasStandalone || !*value) valid = false;
            for (const char* p = value; valid && *p; ++p) {
                if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                      (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-'))
                    valid = false;
            }
            if (valid && !encoding_name_matches_input(d, value)) valid = false;
            if (valid && !d->m_lockEncoding) {
                int declaredEncoding = encoding_name_to_input(value);
                if (declaredEncoding == XML_INPUT_LATIN1 ||
                    declaredEncoding == XML_INPUT_ASCII) {
                    size_t offset = 0;
                    size_t endOffset = 0;
                    if (d->m_data && *ptr >= d->m_data)
                        offset = (size_t)(*ptr - d->m_data);
                    if (d->m_data && end >= d->m_data)
                        endOffset = (size_t)(end - d->m_data);
                    if (!reencode_existing_single_byte(
                            d, declaredEncoding == XML_INPUT_ASCII)) {
                        valid = false;
                    } else {
                        d->m_inputEncoding = declaredEncoding;
                        *ptr = d->m_data + offset;
                        end = d->m_data + endOffset;
                    }
                }
            }
            if (valid) {
                XString_assign_utf8(d->m_documentEncoding, value);
                hasEncoding = true;
            }
        } else if (valid && strcmp(name, "standalone") == 0) {
            /* XML 声明允许 standalone 位于可选 encoding 之后。 */
            if (!hasVersion || hasStandalone ||
                (strcmp(value, "yes") != 0 && strcmp(value, "no") != 0)) valid = false;
            if (valid) {
                d->m_isStandaloneDocument = strcmp(value, "yes") == 0;
                d->m_hasStandalone = true;
                hasStandalone = true;
            }
        } else {
            valid = false;
        }
        XString_deinit_base(&attrName);
        XString_deinit_base(&attrValue);
        if (!valid) return false;
        const char* afterValue = *ptr;
        if (*ptr < end && **ptr != '?' &&
            **ptr != ' ' && **ptr != '\t' && **ptr != '\r' && **ptr != '\n') return false;
        *ptr = skip_whitespace(afterValue, end);
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
    /* 已跳过 "<!--" */
    const char* start = *ptr;
    while (*ptr < end)
    {
        if (**ptr == '-' && *ptr + 2 < end && *(*ptr + 1) == '-' && *(*ptr + 2) == '>')
        {
            /* 提取注释文本 */
            if (!is_valid_utf8_xml(start, (size_t)(*ptr - start))) return false;
            if (d->m_text && *ptr > start)
                XString_assign_with_length_utf8(d->m_text, start, (size_t)(*ptr - start));
            *ptr += 3;
            d->m_type = XXmlStream_Comment;
            return true;
        }
        if (**ptr == '-' && *ptr + 1 < end && *(*ptr + 1) == '-') return false;
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
    const char* target = XString_toUtf8(d->m_processingInstructionTarget);
    if (!target) return false;
    size_t targetLength = strlen(target);
    if (targetLength == 3 &&
        (target[0] == 'x' || target[0] == 'X') &&
        (target[1] == 'm' || target[1] == 'M') &&
        (target[2] == 'l' || target[2] == 'L')) return false;
    *ptr = skip_whitespace(*ptr, end);
    /* 解析数据（直到 ?>） */
    const char* start = *ptr;
    while (*ptr < end)
    {
        if (**ptr == '?' && *ptr + 1 < end && *(*ptr + 1) == '>')
        {
            if (!is_valid_utf8_xml(start, (size_t)(*ptr - start))) return false;
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
            if (!is_valid_utf8_xml(start, (size_t)(*ptr - start))) return false;
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
static void free_notation_entry(XXmlStreamNotationDeclaration* entry)
{
    if (!entry) return;
    if (entry->m_name) XString_delete_base(entry->m_name);
    if (entry->m_systemId) XString_delete_base(entry->m_systemId);
    if (entry->m_publicId) XString_delete_base(entry->m_publicId);
    memset(entry, 0, sizeof(*entry));
}

static void free_entity_entry(XXmlStreamEntityDeclaration* entry)
{
    if (!entry) return;
    if (entry->m_name) XString_delete_base(entry->m_name);
    if (entry->m_notationName) XString_delete_base(entry->m_notationName);
    if (entry->m_systemId) XString_delete_base(entry->m_systemId);
    if (entry->m_publicId) XString_delete_base(entry->m_publicId);
    if (entry->m_value) XString_delete_base(entry->m_value);
    memset(entry, 0, sizeof(*entry));
}

static void clear_dtd_declarations(XXmlStreamReaderPrivate* d)
{
    if (!d) return;
    clear_default_attributes(d);
    if (!d->m_notationDeclarations)
        d->m_notationDeclarations = XXmlStreamNotationDeclarations_create();
    if (!d->m_entityDeclarations)
        d->m_entityDeclarations = XXmlStreamEntityDeclarations_create();
    if (d->m_notationDeclarations) {
        for (size_t i = 0; i < d->m_notationDeclarations->m_count; ++i)
            free_notation_entry(&d->m_notationDeclarations->m_declarations[i]);
        d->m_notationDeclarations->m_count = 0;
    }
    if (d->m_entityDeclarations) {
        for (size_t i = 0; i < d->m_entityDeclarations->m_count; ++i)
            free_entity_entry(&d->m_entityDeclarations->m_declarations[i]);
        d->m_entityDeclarations->m_count = 0;
    }
}

static bool append_notation_declaration(XXmlStreamReaderPrivate* d,
    const XString* name, const XString* systemId, const XString* publicId)
{
    if (!d || !d->m_notationDeclarations || !name) return false;
    XXmlStreamNotationDeclarations* list = d->m_notationDeclarations;
    if (list->m_count >= list->m_capacity) {
        size_t capacity = list->m_capacity ? list->m_capacity * 2 : 4;
        XXmlStreamNotationDeclaration* items = (XXmlStreamNotationDeclaration*)XRealloc_System(
            list->m_declarations, capacity * sizeof(*items));
        if (!items) return false;
        memset(items + list->m_capacity, 0,
               (capacity - list->m_capacity) * sizeof(*items));
        list->m_declarations = items;
        list->m_capacity = capacity;
    }
    XXmlStreamNotationDeclaration* item = &list->m_declarations[list->m_count];
    memset(item, 0, sizeof(*item));
    item->m_name = XString_create_copy(name);
    item->m_systemId = systemId ? XString_create_copy(systemId) : XString_create();
    item->m_publicId = publicId ? XString_create_copy(publicId) : XString_create();
    if (!item->m_name || !item->m_systemId || !item->m_publicId) {
        free_notation_entry(item);
        return false;
    }
    list->m_count++;
    return true;
}

static bool append_entity_declaration(XXmlStreamReaderPrivate* d,
    const XString* name, const XString* notationName, const XString* systemId,
    const XString* publicId, const XString* value)
{
    if (!d || !d->m_entityDeclarations || !name) return false;
    XXmlStreamEntityDeclarations* list = d->m_entityDeclarations;
    if (list->m_count >= list->m_capacity) {
        size_t capacity = list->m_capacity ? list->m_capacity * 2 : 4;
        XXmlStreamEntityDeclaration* items = (XXmlStreamEntityDeclaration*)XRealloc_System(
            list->m_declarations, capacity * sizeof(*items));
        if (!items) return false;
        memset(items + list->m_capacity, 0,
               (capacity - list->m_capacity) * sizeof(*items));
        list->m_declarations = items;
        list->m_capacity = capacity;
    }
    XXmlStreamEntityDeclaration* item = &list->m_declarations[list->m_count];
    memset(item, 0, sizeof(*item));
    item->m_name = XString_create_copy(name);
    item->m_notationName = notationName ? XString_create_copy(notationName) : XString_create();
    item->m_systemId = systemId ? XString_create_copy(systemId) : XString_create();
    item->m_publicId = publicId ? XString_create_copy(publicId) : XString_create();
    item->m_value = value ? XString_create_copy(value) : XString_create();
    if (!item->m_name || !item->m_notationName || !item->m_systemId ||
        !item->m_publicId || !item->m_value) {
        free_entity_entry(item);
        return false;
    }
    list->m_count++;
    return true;
}

static const char* dtd_declaration_end(const char* start, const char* end)
{
    char quote = '\0';
    for (const char* p = start; p < end; ++p) {
        if (quote) {
            if (*p == quote) quote = '\0';
        } else if (*p == '\'' || *p == '"') {
            quote = *p;
        } else if (*p == '>') {
            return p;
        }
    }
    return NULL;
}

static bool parse_dtd_subset(XXmlStreamReaderPrivate* d, const char* start, const char* end)
{
    const char* p = start;
    while (p < end) {
        if (*p != '<' || p + 2 >= end || p[1] != '!') {
            ++p;
            continue;
        }
        bool notation = (size_t)(end - p) >= 11 && strncmp(p + 2, "NOTATION", 8) == 0;
        bool entity = (size_t)(end - p) >= 9 && strncmp(p + 2, "ENTITY", 6) == 0;
        bool attlist = (size_t)(end - p) >= 10 && strncmp(p + 2, "ATTLIST", 7) == 0;
        bool element = (size_t)(end - p) >= 9 && strncmp(p + 2, "ELEMENT", 7) == 0;
        if (!notation && !entity && !attlist && !element) {
            ++p;
            continue;
        }
        const char* declEnd = dtd_declaration_end(p + 2, end);
        if (!declEnd) return false;
        const char* q = p + 2 + (notation ? 8 : (entity ? 6 : 7));
        q = skip_whitespace(q, declEnd);
        if (element) {
            /* ELEMENT 内容模型会影响验证，但不影响流式 token；完整扫描其声明，
               确保括号和引号没有破坏内部子集边界。 */
            int parenthesisDepth = 0;
            char quote = '\0';
            for (; q < declEnd; ++q) {
                if (quote) {
                    if (*q == quote) quote = '\0';
                } else if (*q == '\'' || *q == '"') {
                    quote = *q;
                } else if (*q == '(') {
                    ++parenthesisDepth;
                } else if (*q == ')' && parenthesisDepth > 0) {
                    --parenthesisDepth;
                }
            }
            if (quote || parenthesisDepth != 0) return false;
            p = declEnd + 1;
            continue;
        }
        if (attlist) {
            XString elementName;
            XString_init(&elementName);
            bool ok = parse_name(&q, declEnd, &elementName);
            while (ok) {
                q = skip_whitespace(q, declEnd);
                if (q >= declEnd) break;
                XString attributeName;
                XString_init(&attributeName);
                ok = parse_name(&q, declEnd, &attributeName);
                q = skip_whitespace(q, declEnd);
                if (!ok || q >= declEnd) {
                    XString_deinit_base(&attributeName);
                    break;
                }
                if (*q == '(') {
                    int depth = 0;
                    do {
                        if (*q == '(') ++depth;
                        else if (*q == ')') --depth;
                        ++q;
                    } while (q < declEnd && depth > 0);
                    if (depth != 0) ok = false;
                } else {
                    XString typeName;
                    XString_init(&typeName);
                    ok = parse_name(&q, declEnd, &typeName);
                    XString_deinit_base(&typeName);
                }
                q = skip_whitespace(q, declEnd);
                bool required = false;
                bool hasValue = false;
                XString defaultValue;
                XString_init(&defaultValue);
                if (ok && q < declEnd && *q == '#') {
                    ++q;
                    const char* keywordStart = q;
                    while (q < declEnd && ((*q >= 'A' && *q <= 'Z') ||
                           (*q >= 'a' && *q <= 'z'))) ++q;
                    size_t keywordLength = (size_t)(q - keywordStart);
                    if (keywordLength == 8 && strncmp(keywordStart, "REQUIRED", 8) == 0) {
                        required = true;
                    } else if (keywordLength == 7 && strncmp(keywordStart, "IMPLIED", 7) == 0) {
                        /* 没有默认值。 */
                    } else if (keywordLength == 5 && strncmp(keywordStart, "FIXED", 5) == 0) {
                        q = skip_whitespace(q, declEnd);
                        hasValue = parse_quoted_string(&q, declEnd, &defaultValue);
                    } else {
                        ok = false;
                    }
                } else if (ok && q < declEnd && (*q == '\'' || *q == '"')) {
                    hasValue = parse_quoted_string(&q, declEnd, &defaultValue);
                } else {
                    ok = false;
                }
                if (ok && hasValue)
                    ok = append_default_attribute(d, &elementName, &attributeName,
                                                  &defaultValue, required);
                XString_deinit_base(&defaultValue);
                XString_deinit_base(&attributeName);
                q = skip_whitespace(q, declEnd);
            }
            XString_deinit_base(&elementName);
            if (!ok) return false;
            p = declEnd + 1;
            continue;
        }
        if (entity && q < declEnd && *q == '%') {
            ++q;
            q = skip_whitespace(q, declEnd);
        }
        XString name, keyword, first, second, notationName;
        XString_init(&name); XString_init(&keyword); XString_init(&first);
        XString_init(&second); XString_init(&notationName);
        bool ok = parse_name(&q, declEnd, &name);
        q = skip_whitespace(q, declEnd);
        bool directEntityValue = entity && q < declEnd && (*q == '\'' || *q == '"');
        if (ok && !directEntityValue && !parse_name(&q, declEnd, &keyword)) ok = false;
        q = skip_whitespace(q, declEnd);
        if (ok && notation) {
            const char* kind = XString_toUtf8(&keyword);
            if (kind && strcmp(kind, "PUBLIC") == 0) {
                ok = parse_quoted_string(&q, declEnd, &first);
                q = skip_whitespace(q, declEnd);
                if (ok && q < declEnd && (*q == '\'' || *q == '"'))
                    ok = parse_quoted_string(&q, declEnd, &second);
            } else if (kind && strcmp(kind, "SYSTEM") == 0) {
                ok = parse_quoted_string(&q, declEnd, &first);
            } else {
                ok = false;
            }
            if (ok) {
                if (kind && strcmp(kind, "PUBLIC") == 0)
                    ok = append_notation_declaration(d, &name, &second, &first);
                else
                    ok = append_notation_declaration(d, &name, &first, NULL);
            }
        } else if (ok) {
            const char* kind = XString_toUtf8(&keyword);
            if (directEntityValue) {
                ok = parse_quoted_string(&q, declEnd, &first);
                if (ok) ok = append_entity_declaration(d, &name, NULL, NULL, NULL, &first);
            } else if (kind && strcmp(kind, "PUBLIC") == 0) {
                ok = parse_quoted_string(&q, declEnd, &first);
                q = skip_whitespace(q, declEnd);
                if (ok && q < declEnd && (*q == '\'' || *q == '"'))
                    ok = parse_quoted_string(&q, declEnd, &second);
                if (ok) ok = append_entity_declaration(d, &name, NULL, &second, &first, NULL);
            } else if (kind && strcmp(kind, "SYSTEM") == 0) {
                ok = parse_quoted_string(&q, declEnd, &first);
                q = skip_whitespace(q, declEnd);
                if (ok && q + 5 <= declEnd && strncmp(q, "NDATA", 5) == 0) {
                    q += 5;
                    q = skip_whitespace(q, declEnd);
                    ok = parse_name(&q, declEnd, &notationName);
                }
                if (ok) ok = append_entity_declaration(d, &name, &notationName, &first, NULL, NULL);
            } else {
                ok = false;
            }
        }
        XString_deinit_base(&name); XString_deinit_base(&keyword);
        XString_deinit_base(&first); XString_deinit_base(&second);
        XString_deinit_base(&notationName);
        if (!ok) return false;
        p = declEnd + 1;
    }
    return true;
}

static bool parse_dtd(XXmlStreamReaderPrivate* d, const char** ptr, const char* end)
{
    const char* dtdStart = (d->m_data && *ptr >= d->m_data + 9) ? *ptr - 9 : *ptr;
    clear_dtd_declarations(d);
    XString_clear_base(d->m_dtdName);
    XString_clear_base(d->m_dtdPublicId);
    XString_clear_base(d->m_dtdSystemId);
    *ptr = skip_whitespace(*ptr, end);
    if (!parse_name(ptr, end, d->m_dtdName)) return false;
    *ptr = skip_whitespace(*ptr, end);
    if (*ptr + 6 <= end && strncmp(*ptr, "PUBLIC", 6) == 0) {
        *ptr += 6;
        *ptr = skip_whitespace(*ptr, end);
        if (!parse_quoted_string(ptr, end, d->m_dtdPublicId)) return false;
        *ptr = skip_whitespace(*ptr, end);
        if (!parse_quoted_string(ptr, end, d->m_dtdSystemId)) return false;
    } else if (*ptr + 6 <= end && strncmp(*ptr, "SYSTEM", 6) == 0) {
        *ptr += 6;
        *ptr = skip_whitespace(*ptr, end);
        if (!parse_quoted_string(ptr, end, d->m_dtdSystemId)) return false;
    }
    *ptr = skip_whitespace(*ptr, end);
    if (*ptr < end && **ptr == '[') {
        const char* subsetStart = ++(*ptr);
        int depth = 1;
        char quote = '\0';
        while (*ptr < end && depth > 0) {
            if (quote) {
                if (**ptr == quote) quote = '\0';
            } else if (**ptr == '\'' || **ptr == '"') {
                quote = **ptr;
            } else if (**ptr == '[') {
                ++depth;
            } else if (**ptr == ']') {
                --depth;
                if (depth == 0) break;
            }
            ++(*ptr);
        }
        if (*ptr >= end || **ptr != ']') return false;
        if (!parse_dtd_subset(d, subsetStart, *ptr)) return false;
        ++(*ptr);
        *ptr = skip_whitespace(*ptr, end);
    }
    if (*ptr >= end || **ptr != '>') return false;
    ++(*ptr);
    if (d->m_text && dtdStart < *ptr)
        XString_assign_with_length_utf8(d->m_text, dtdStart, (size_t)(*ptr - dtdStart));
    d->m_type = XXmlStream_DTD;
    return true;
}
/**
 * @brief      释放当前 Token 持有的命名空间声明
 * @param d    解析器私有数据
 */
static void clear_namespace_declarations(XXmlStreamReaderPrivate* d)
{
    if (!d) return;
    for (int i = 0; i < d->m_namespaceDeclarationCount; i++)
    {
        XmlNamespaceDeclaration* decl = &d->m_namespaceDeclarations[i];
        if (decl->m_prefix) XString_delete_base(decl->m_prefix);
        if (decl->m_namespaceUri) XString_delete_base(decl->m_namespaceUri);
        decl->m_prefix = NULL;
        decl->m_namespaceUri = NULL;
    }
    d->m_namespaceDeclarationCount = 0;
}

static bool ensure_namespace_capacity(XmlNamespaceDeclaration** declarations, int* capacity, int needed)
{
    if (*capacity >= needed) return true;
    int newCapacity = *capacity == 0 ? 8 : *capacity;
    while (newCapacity < needed) newCapacity *= 2;
    XmlNamespaceDeclaration* resized = (XmlNamespaceDeclaration*)XRealloc_System(
        *declarations, (size_t)newCapacity * sizeof(XmlNamespaceDeclaration));
    if (!resized) return false;
    *declarations = resized;
    *capacity = newCapacity;
    return true;
}

static bool append_namespace(XmlNamespaceDeclaration** declarations, int* count, int* capacity,
                             const char* prefix, const char* namespaceUri)
{
    if (!ensure_namespace_capacity(declarations, capacity, *count + 1)) return false;
    XmlNamespaceDeclaration* declaration = &(*declarations)[*count];
    memset(declaration, 0, sizeof(*declaration));
    declaration->m_prefix = XString_create_utf8(prefix ? prefix : "");
    declaration->m_namespaceUri = XString_create_utf8(namespaceUri ? namespaceUri : "");
    if (!declaration->m_prefix || !declaration->m_namespaceUri) {
        if (declaration->m_prefix) XString_delete_base(declaration->m_prefix);
        if (declaration->m_namespaceUri) XString_delete_base(declaration->m_namespaceUri);
        memset(declaration, 0, sizeof(*declaration));
        return false;
    }
    (*count)++;
    return true;
}

static void truncate_namespace_bindings(XXmlStreamReaderPrivate* d, int count)
{
    if (!d) return;
    if (count < 0) count = 0;
    while (d->m_namespaceBindingCount > count) {
        XmlNamespaceDeclaration* declaration =
            &d->m_namespaceBindings[d->m_namespaceBindingCount - 1];
        if (declaration->m_prefix) XString_delete_base(declaration->m_prefix);
        if (declaration->m_namespaceUri) XString_delete_base(declaration->m_namespaceUri);
        memset(declaration, 0, sizeof(*declaration));
        d->m_namespaceBindingCount--;
    }
}

static const char* namespace_for_prefix(const XXmlStreamReaderPrivate* d, const char* prefix)
{
    const char* wanted = prefix ? prefix : "";
    if (strcmp(wanted, "xml") == 0) return "http://www.w3.org/XML/1998/namespace";
    for (int i = d->m_namespaceBindingCount - 1; i >= 0; --i) {
        const XString* prefixString = d->m_namespaceBindings[i].m_prefix;
        const char* candidate = XString_toUtf8(prefixString);
        if (((wanted[0] == '\0') && XString_size(prefixString) == 0) ||
            (candidate && strcmp(candidate, wanted) == 0))
            return XString_toUtf8(d->m_namespaceBindings[i].m_namespaceUri);
    }
    for (int i = d->m_extraNamespaceDeclarationCount - 1; i >= 0; --i) {
        const XString* prefixString = d->m_extraNamespaceDeclarations[i].m_prefix;
        const char* candidate = XString_toUtf8(prefixString);
        if (((wanted[0] == '\0') && XString_size(prefixString) == 0) ||
            (candidate && strcmp(candidate, wanted) == 0))
            return XString_toUtf8(d->m_extraNamespaceDeclarations[i].m_namespaceUri);
    }
    return NULL;
}

/**
 * @brief      解析命名空间声明
 * @param d        解析器私有数据
 * @param attrName 属性名
 * @param attrValue 属性值
 */
static bool parse_namespace_declaration(XXmlStreamReaderPrivate* d, const XString* attrName, const XString* attrValue)
{
    const char* name = XString_toUtf8(attrName);
    const char* value = XString_toUtf8(attrValue);
    if (!name || !value) return false;
    const char* prefix = NULL;
    if (strcmp(name, "xmlns") == 0) prefix = "";
    else if (strncmp(name, "xmlns:", 6) == 0) prefix = name + 6;
    else return true;

    if ((strcmp(prefix, "xml") == 0) !=
        (strcmp(value, "http://www.w3.org/XML/1998/namespace") == 0)) return false;
    if (strcmp(prefix, "xmlns") == 0 ||
        strcmp(value, "http://www.w3.org/2000/xmlns/") == 0) return false;
    if (*prefix != '\0' && *value == '\0') return false;

    for (int i = 0; i < d->m_namespaceDeclarationCount; ++i) {
        const char* existing = XString_toUtf8(d->m_namespaceDeclarations[i].m_prefix);
        if (existing && strcmp(existing, prefix) == 0) return false;
    }

    if (!append_namespace(&d->m_namespaceDeclarations, &d->m_namespaceDeclarationCount,
                          &d->m_namespaceDeclarationCapacity, prefix, value)) return false;
    if (!append_namespace(&d->m_namespaceBindings, &d->m_namespaceBindingCount,
                          &d->m_namespaceBindingCapacity, prefix, value)) {
        XmlNamespaceDeclaration* declaration =
            &d->m_namespaceDeclarations[--d->m_namespaceDeclarationCount];
        XString_delete_base(declaration->m_prefix);
        XString_delete_base(declaration->m_namespaceUri);
        memset(declaration, 0, sizeof(*declaration));
        return false;
    }
    return true;
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
    typedef struct RawAttribute {
        XString m_name;
        XString m_value;
    } RawAttribute;
    RawAttribute* rawAttributes = NULL;
    int rawCount = 0;
    int rawCapacity = 0;
    bool ok = false;

    while (*ptr < end)
    {
        *ptr = skip_whitespace(*ptr, end);
        if (*ptr >= end) goto cleanup;
        /* 遇到 /> 或 > 结束属性解析 */
        if (**ptr == '/' || **ptr == '>')
            break;
        if (rawCount >= rawCapacity) {
            int newCapacity = rawCapacity == 0 ? 8 : rawCapacity * 2;
            RawAttribute* resized = (RawAttribute*)XRealloc_System(
                rawAttributes, (size_t)newCapacity * sizeof(RawAttribute));
            if (!resized) goto cleanup;
            rawAttributes = resized;
            rawCapacity = newCapacity;
        }
        /* 解析属性名 */
        RawAttribute* raw = &rawAttributes[rawCount++];
        XString_init(&raw->m_name);
        XString_init(&raw->m_value);
        if (!parse_name(ptr, end, &raw->m_name)) goto cleanup;
        *ptr = skip_whitespace(*ptr, end);
        if (*ptr >= end || **ptr != '=') goto cleanup;
        ++(*ptr);
        *ptr = skip_whitespace(*ptr, end);
        const char* valueEnd = *ptr;
        if (!parse_attribute_value(d, ptr, end, &raw->m_value)) goto cleanup;
        if (*ptr < end && **ptr != '/' && **ptr != '>' &&
            (*ptr == valueEnd || (**ptr != ' ' && **ptr != '\t' &&
             **ptr != '\r' && **ptr != '\n'))) goto cleanup;
    }

    /* 声明在解析普通属性前统一加入作用域，所以属性顺序不影响命名空间解析。 */
    if (d->m_namespaceProcessing) {
        for (int i = 0; i < rawCount; ++i) {
            const char* name = XString_toUtf8(&rawAttributes[i].m_name);
            if (name && (strcmp(name, "xmlns") == 0 || strncmp(name, "xmlns:", 6) == 0)) {
                if (!parse_namespace_declaration(d, &rawAttributes[i].m_name,
                                                  &rawAttributes[i].m_value)) goto cleanup;
            }
        }
    }

    for (int i = 0; i < rawCount; ++i) {
        const char* qualifiedName = XString_toUtf8(&rawAttributes[i].m_name);
        if (!qualifiedName) goto cleanup;
        if (d->m_namespaceProcessing &&
            (strcmp(qualifiedName, "xmlns") == 0 || strncmp(qualifiedName, "xmlns:", 6) == 0)) continue;
        const char* colon = strchr(qualifiedName, ':');
        if (colon && strchr(colon + 1, ':') != NULL) goto cleanup;
        const char* localName = colon ? colon + 1 : qualifiedName;
        if (!*localName || (colon && colon == qualifiedName)) goto cleanup;
        const char* namespaceUri = NULL;
        char prefix[128] = {0};
        if (colon) {
            size_t prefixLength = (size_t)(colon - qualifiedName);
            if (prefixLength == 0 || prefixLength >= sizeof(prefix)) goto cleanup;
            memcpy(prefix, qualifiedName, prefixLength);
            if (d->m_namespaceProcessing) {
                namespaceUri = namespace_for_prefix(d, prefix);
                if (!namespaceUri) goto cleanup;
            }
        }

        XString* namespaceString = namespaceUri ? XString_create_utf8(namespaceUri) : NULL;
        XString* localString = XString_create_utf8(localName);
        XXmlStreamAttribute* attribute = XXmlStreamAttribute_create_ex(
            namespaceString, localString, &rawAttributes[i].m_value);
        if (namespaceString) XString_delete_base(namespaceString);
        if (localString) XString_delete_base(localString);
        if (!attribute) goto cleanup;
        if (attribute->m_qualifiedName) XString_delete_base(attribute->m_qualifiedName);
        attribute->m_qualifiedName = XString_create_copy(&rawAttributes[i].m_name);
        if (attribute->m_prefix) XString_delete_base(attribute->m_prefix);
        attribute->m_prefix = XString_create_utf8(colon ? prefix : "");
        if (!attribute->m_qualifiedName || !attribute->m_prefix) {
            XXmlStreamAttribute_delete(attribute);
            goto cleanup;
        }
        for (int j = 0; j < XXmlStreamAttributes_size(d->m_attributes); ++j) {
            const XXmlStreamAttribute* existing = XXmlStreamAttributes_at(d->m_attributes, j);
            bool duplicate = d->m_namespaceProcessing
                ? XString_equals(existing->m_name, attribute->m_name, XChar_CaseSensitive) &&
                  ((!existing->m_namespaceUri && !attribute->m_namespaceUri) ||
                   (existing->m_namespaceUri && attribute->m_namespaceUri &&
                    XString_equals(existing->m_namespaceUri, attribute->m_namespaceUri, XChar_CaseSensitive)))
                : XString_equals(existing->m_qualifiedName, attribute->m_qualifiedName, XChar_CaseSensitive);
            if (duplicate) {
                XXmlStreamAttribute_delete(attribute);
                goto cleanup;
            }
        }
        if (d->m_attributes->m_count >= d->m_attributes->m_capacity) {
            int newCapacity = d->m_attributes->m_capacity ? d->m_attributes->m_capacity * 2 : 4;
            XXmlStreamAttribute** resized = (XXmlStreamAttribute**)XRealloc_System(
                d->m_attributes->m_items, (size_t)newCapacity * sizeof(XXmlStreamAttribute*));
            if (!resized) {
                XXmlStreamAttribute_delete(attribute);
                goto cleanup;
            }
            d->m_attributes->m_items = resized;
            d->m_attributes->m_capacity = newCapacity;
        }
        d->m_attributes->m_items[d->m_attributes->m_count++] = attribute;
    }
    ok = true;

cleanup:
    for (int i = 0; i < rawCount; ++i) {
        XString_deinit_base(&rawAttributes[i].m_name);
        XString_deinit_base(&rawAttributes[i].m_value);
    }
    XFree_System(rawAttributes);
    return ok;
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
    if (d->m_tagStackSize == 0 && d->m_seenRootElement)
        return false;
    clear_namespace_declarations(d);
    int namespaceBindingCountBefore = d->m_namespaceBindingCount;
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
    /* 清除上一元素的属性 */
    if (d->m_attributes) {
        XXmlStreamAttributes_delete(d->m_attributes);
    }
    d->m_attributes = XXmlStreamAttributes_create();
    /* 解析属性 */
    if (!parse_attributes(d, ptr, end)) {
        truncate_namespace_bindings(d, namespaceBindingCountBefore);
        return false;
    }
    if (d->m_entityDeclarations && d->m_defaultAttributeCount > 0) {
        const char* elementName = XString_toUtf8(d->m_qualifiedName);
        for (int i = 0; elementName && i < d->m_defaultAttributeCount; ++i) {
            const XmlDefaultAttribute* item = &d->m_defaultAttributes[i];
            const char* declaredElement = XString_toUtf8(item->m_elementName);
            const char* declaredAttribute = XString_toUtf8(item->m_attributeName);
            if (!declaredElement || !declaredAttribute ||
                strcmp(declaredElement, elementName) != 0) continue;
            bool exists = false;
            for (int j = 0; j < XXmlStreamAttributes_size(d->m_attributes); ++j) {
                const XXmlStreamAttribute* existing =
                    XXmlStreamAttributes_at(d->m_attributes, j);
                const char* existingName = existing && existing->m_qualifiedName
                    ? XString_toUtf8(existing->m_qualifiedName) : NULL;
                if (existingName && strcmp(existingName, declaredAttribute) == 0) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;
            XString expanded;
            XString_init(&expanded);
            const char* rawValue = XString_toUtf8(item->m_value);
            if (!rawValue || !expand_entity_value(d, rawValue, &expanded, 0)) {
                XString_deinit_base(&expanded);
                truncate_namespace_bindings(d, namespaceBindingCountBefore);
                return false;
            }
            XXmlStreamAttributes_append_ex(d->m_attributes, item->m_attributeName, &expanded);
            if (d->m_attributes->m_count > 0) {
                XXmlStreamAttribute* added = d->m_attributes->m_items[d->m_attributes->m_count - 1];
                if (added) added->m_isDefault = true;
            }
            XString_deinit_base(&expanded);
        }
    }
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
        const char* namespaceUri = d->m_namespaceProcessing
            ? namespace_for_prefix(d, prefix_utf8 ? prefix_utf8 : "") : NULL;
        if (d->m_namespaceProcessing && prefix_utf8 && *prefix_utf8 && !namespaceUri) {
            truncate_namespace_bindings(d, namespaceBindingCountBefore);
            return false;
        }
        if (namespaceUri) XString_assign_utf8(d->m_namespaceUri, namespaceUri);
        else XString_clear_base(d->m_namespaceUri);
        /* 推入标签栈 */
        if (d->m_tagStackSize >= d->m_tagStackCapacity)
        {
            int newCap = d->m_tagStackCapacity == 0 ? 16 : d->m_tagStackCapacity * 2;
            XmlTag* newStack = (XmlTag*)XRealloc_System(d->m_tagStack, (size_t)newCap * sizeof(XmlTag));
            if (!newStack) {
                truncate_namespace_bindings(d, namespaceBindingCountBefore);
                return false;
            }
            d->m_tagStack = newStack;
            d->m_tagStackCapacity = newCap;
        }
        XmlTag* tag = &d->m_tagStack[d->m_tagStackSize];
        memset(tag, 0, sizeof(XmlTag));
        tag->m_name = XString_create_copy(d->m_name);
        tag->m_qualifiedName = XString_create_copy(d->m_qualifiedName);
        tag->m_prefix = XString_create_copy(d->m_prefix);
        tag->m_namespaceUri = XString_create_copy(d->m_namespaceUri);
        tag->m_namespaceBindingCountBefore = namespaceBindingCountBefore;
        if (!tag->m_name || !tag->m_qualifiedName || !tag->m_prefix || !tag->m_namespaceUri) {
            if (tag->m_name) XString_delete_base(tag->m_name);
            if (tag->m_qualifiedName) XString_delete_base(tag->m_qualifiedName);
            if (tag->m_prefix) XString_delete_base(tag->m_prefix);
            if (tag->m_namespaceUri) XString_delete_base(tag->m_namespaceUri);
            memset(tag, 0, sizeof(*tag));
            truncate_namespace_bindings(d, namespaceBindingCountBefore);
            return false;
        }
        d->m_tagStackSize++;
        if (d->m_tagStackSize == 1)
            d->m_seenRootElement = true;
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
                int namespaceBindingCountBefore = tag->m_namespaceBindingCountBefore;
                /* 释放标签 */
                XString_delete_base(tag->m_name);
                XString_delete_base(tag->m_qualifiedName);
                XString_delete_base(tag->m_prefix);
                XString_delete_base(tag->m_namespaceUri);
                memset(tag, 0, sizeof(XmlTag));
                d->m_tagStackSize--;
                truncate_namespace_bindings(d, namespaceBindingCountBefore);
                if (d->m_tagStackSize == 0) d->m_finishedRootElement = true;
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

static const XXmlStreamEntityDeclaration* find_entity_declaration(
    const XXmlStreamReaderPrivate* d, const char* name, size_t nameLength)
{
    if (!d || !d->m_entityDeclarations || !name) return NULL;
    for (size_t i = 0; i < d->m_entityDeclarations->m_count; ++i) {
        const XXmlStreamEntityDeclaration* declaration =
            &d->m_entityDeclarations->m_declarations[i];
        const char* declaredName = XString_toUtf8(declaration->m_name);
        if (declaredName && strlen(declaredName) == nameLength &&
            memcmp(declaredName, name, nameLength) == 0)
            return declaration;
    }
    return NULL;
}

static bool entity_declaration_is_external(const XXmlStreamEntityDeclaration* declaration)
{
    if (!declaration) return false;
    const char* publicId = XString_toUtf8(declaration->m_publicId);
    const char* systemId = XString_toUtf8(declaration->m_systemId);
    return (publicId && *publicId) || (systemId && *systemId);
}

static bool append_entity_codepoint(XXmlStreamReaderPrivate* d, XString* output,
                                    const char* value, size_t length)
{
    if (!output || !value || length < 2 || value[0] != '#') return false;
    size_t index = 1;
    int base = 10;
    if (index < length && (value[index] == 'x' || value[index] == 'X')) {
        base = 16;
        ++index;
    }
    if (index == length) return false;

    uint32_t codepoint = 0;
    for (; index < length; ++index) {
        unsigned char c = (unsigned char)value[index];
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit < 0 || digit >= base || codepoint > 0x10ffffU / (uint32_t)base) {
            set_error(d, XXmlStream_NotWellFormedError, "无效的数字实体引用。");
            return false;
        }
        codepoint = codepoint * (uint32_t)base + (uint32_t)digit;
    }
    if (!is_xml_char(codepoint)) {
        set_error(d, XXmlStream_NotWellFormedError, "无效的数字实体引用。");
        return false;
    }

    char utf8[4];
    size_t utf8Length = 0;
    if (codepoint < 0x80U) {
        utf8[0] = (char)codepoint;
        utf8Length = 1;
    } else if (codepoint < 0x800U) {
        utf8[0] = (char)(0xc0U | (codepoint >> 6));
        utf8[1] = (char)(0x80U | (codepoint & 0x3fU));
        utf8Length = 2;
    } else if (codepoint < 0x10000U) {
        utf8[0] = (char)(0xe0U | (codepoint >> 12));
        utf8[1] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
        utf8[2] = (char)(0x80U | (codepoint & 0x3fU));
        utf8Length = 3;
    } else {
        utf8[0] = (char)(0xf0U | (codepoint >> 18));
        utf8[1] = (char)(0x80U | ((codepoint >> 12) & 0x3fU));
        utf8[2] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
        utf8[3] = (char)(0x80U | (codepoint & 0x3fU));
        utf8Length = 4;
    }
    return XString_append_with_length_utf8(output, utf8, utf8Length);
}

/* 递归展开 DTD 内部实体；限制在调用端按最终展开文本计算。 */
static bool expand_entity_value(XXmlStreamReaderPrivate* d, const char* value,
                                XString* output, int depth)
{
    if (!d || !value || !output) return false;
    if (depth >= 32) {
        set_error(d, XXmlStream_NotWellFormedError, "实体递归层级超过限制。");
        return false;
    }

    const char* p = value;
    while (*p) {
        if (*p != '&') {
            uint32_t codepoint;
            size_t byteLength;
            const char* end = p + strlen(p);
            if (!decode_utf8_codepoint(p, end, &codepoint, &byteLength) ||
                !is_xml_char(codepoint) ||
                !XString_append_with_length_utf8(output, p, byteLength)) return false;
            p += byteLength;
            continue;
        }

        const char* semicolon = strchr(p + 1, ';');
        if (!semicolon) {
            set_error(d, XXmlStream_NotWellFormedError, "实体引用缺少分号。");
            return false;
        }
        const char* entity = p + 1;
        size_t entityLength = (size_t)(semicolon - entity);
        bool predefined = false;
        if (entityLength == 3 && memcmp(entity, "amp", 3) == 0) {
            predefined = XString_append_utf8(output, "&");
        } else if (entityLength == 2 && memcmp(entity, "lt", 2) == 0) {
            predefined = XString_append_utf8(output, "<");
        } else if (entityLength == 2 && memcmp(entity, "gt", 2) == 0) {
            predefined = XString_append_utf8(output, ">");
        } else if (entityLength == 4 && memcmp(entity, "quot", 4) == 0) {
            predefined = XString_append_utf8(output, "\"");
        } else if (entityLength == 4 && memcmp(entity, "apos", 4) == 0) {
            predefined = XString_append_utf8(output, "'");
        }

        if (predefined) {
            p = semicolon + 1;
            continue;
        }
        if (entityLength > 0 && entity[0] == '#') {
            if (!append_entity_codepoint(d, output, entity, entityLength)) return false;
            p = semicolon + 1;
            continue;
        }

        const char* entityNameEnd = entity;
        if (!parse_name(&entityNameEnd, semicolon, NULL) || entityNameEnd != semicolon) {
            set_error(d, XXmlStream_NotWellFormedError, "实体名称无效。");
            return false;
        }

        const XXmlStreamEntityDeclaration* declaration =
            find_entity_declaration(d, entity, entityLength);
        const XString* replacement = declaration && !entity_declaration_is_external(declaration)
            ? declaration->m_value : NULL;
        if (!replacement && declaration && d->m_entityResolver) {
            replacement = XXmlStreamEntityResolver_resolveEntity(
                d->m_entityResolver, declaration->m_publicId, declaration->m_systemId);
        }
        if (!replacement) {
            set_error(d, XXmlStream_NotWellFormedError, "未声明的实体引用。");
            return false;
        }
        const char* replacementUtf8 = XString_toUtf8(replacement);
        if (!replacementUtf8 || !expand_entity_value(d, replacementUtf8, output, depth + 1))
            return false;
        p = semicolon + 1;
    }
    return true;
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
        if (*ptr < end && (**ptr == 'x' || **ptr == 'X'))
        {
            ++(*ptr);
            while (*ptr < end && **ptr != ';' && is_xml_ascii_hex_digit((uint8_t)**ptr))
                ++(*ptr);
        }
        else
        {
            while (*ptr < end && **ptr != ';' && is_xml_ascii_digit((uint8_t)**ptr))
                ++(*ptr);
        }
        if (*ptr < end && **ptr == ';' && *ptr > start + 1)
        {
            size_t valueLength = (size_t)(*ptr - start);
            ++(*ptr);
            /* 解析数字字符引用为 UTF-8 并追加到 m_text */
            if (d->m_text && !append_entity_codepoint(d, d->m_text, start, valueLength))
                return false;
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
                const XXmlStreamEntityDeclaration* declaration =
                    find_entity_declaration(d, entity_utf8, strlen(entity_utf8));
                const XString* replacement = declaration && !entity_declaration_is_external(declaration)
                    ? declaration->m_value : NULL;
                if (!replacement && declaration && d->m_entityResolver)
                    replacement = XXmlStreamEntityResolver_resolveEntity(
                        d->m_entityResolver, declaration->m_publicId, declaration->m_systemId);
                if (!replacement && d->m_entityResolver)
                    replacement = XXmlStreamEntityResolver_resolveUndeclaredEntity(
                        d->m_entityResolver, &entityName);
                if (replacement) {
                    XString expanded;
                    XString_init(&expanded);
                    const char* replacementUtf8 = XString_toUtf8(replacement);
                    if (!replacementUtf8 || !expand_entity_value(d, replacementUtf8, &expanded, 0)) {
                        XString_deinit_base(&expanded);
                        XString_deinit_base(&entityName);
                        return false;
                    }
                    size_t expandedLength = strlen(XString_toUtf8(&expanded));
                    size_t referenceLength = strlen(entity_utf8) + 2;
                    size_t addedLength = expandedLength > referenceLength
                        ? expandedLength - referenceLength : 0;
                    if (d->m_entityExpansionLimit >= 0 &&
                        addedLength > (size_t)d->m_entityExpansionLimit) {
                        set_error(d, XXmlStream_NotWellFormedError,
                                  "实体扩展超过限制。");
                        XString_deinit_base(&expanded);
                        XString_deinit_base(&entityName);
                        return false;
                    }
                    if (d->m_text) XString_append(d->m_text, &expanded);
                    XString_deinit_base(&expanded);
                    d->m_type = XXmlStream_Characters;
                } else {
                    XString_assign_utf8(d->m_name, entity_utf8);
                    d->m_type = XXmlStream_EntityReference;
                }
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
                if (!is_valid_utf8_xml(start, (size_t)(*ptr - start))) {
                    set_error(d, XXmlStream_NotWellFormedError, "字符数据包含无效 XML 字符。");
                    return false;
                }
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
                if (d->m_tagStackSize == 0 && !d->m_isWhitespace) {
                    set_error(d, XXmlStream_NotWellFormedError,
                              "根元素外存在字符数据。");
                    return false;
                }
                return true;
            }
            /* 如果 m_type 已被前面的实体引用设置为 Characters，说明 m_text 中已积累展开文本 */
            if (d->m_type == XXmlStream_Characters)
            {
                if (d->m_tagStackSize == 0 && !d->m_isWhitespace) {
                    set_error(d, XXmlStream_NotWellFormedError,
                              "根元素外存在字符数据。");
                    return false;
                }
                return true;
            }
            return false;
        }
        else if (**ptr == '&')
        {
            if (*ptr > start && !is_valid_utf8_xml(start, (size_t)(*ptr - start))) {
                set_error(d, XXmlStream_NotWellFormedError, "字符数据包含无效 XML 字符。");
                return false;
            }
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
        if (!is_valid_utf8_xml(start, (size_t)(*ptr - start))) {
            set_error(d, XXmlStream_NotWellFormedError, "字符数据包含无效 XML 字符。");
            return false;
        }
        XString_append_with_length_utf8(d->m_text, start, (size_t)(*ptr - start));
        d->m_type = XXmlStream_Characters;
        if (d->m_tagStackSize == 0 && !d->m_isWhitespace) {
            set_error(d, XXmlStream_NotWellFormedError,
                      "根元素外存在字符数据。");
            return false;
        }
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
    clear_namespace_declarations(d);
    d->m_isCDATA = false;
    d->m_isWhitespace = true;
    d->m_isEmptyElement = false;
}

/**
 * @brief      更新 Reader 的当前解析位置。
 * @param      d Reader 私有数据。
 * @param      position 当前输入位置，按 UTF-8 归一化缓冲区计数。
 * @return     无。
 * @note       对齐 Qt：lineNumber 从 1 开始，columnNumber 和 characterOffset
 *             从 0 开始，并指向当前解析游标而不是 Token 起点。
 */
static void update_current_position(XXmlStreamReaderPrivate* d, const char* position)
{
    if (!d || !d->m_data || !position || position < d->m_data) return;
    const char* end = d->m_data + d->m_dataLength;
    if (position > end) position = end;
    const char* lineStart = d->m_data;
    for (const char* p = d->m_data; p < position; ++p) {
        if (*p == '\r') {
            lineStart = p + 1;
            if (p + 1 < position && p[1] == '\n') {
                lineStart = p + 2;
                ++p;
            }
        } else if (*p == '\n') {
            lineStart = p + 1;
        }
    }
    /* Qt 的读取缓冲区是 UTF-16 QString，位置按 UTF-16 code unit 计数，
       不能直接使用归一化 UTF-8 缓冲区的字节偏移。 */
    int64_t totalUnits = 0;
    int64_t lineUnits = 0;
    const char* p = d->m_data;
    while (p < position) {
        uint32_t codepoint = 0;
        size_t length = 0;
        if (!decode_utf8_codepoint(p, position, &codepoint, &length) || length == 0) {
            ++p;
            ++totalUnits;
            continue;
        }
        totalUnits += codepoint > 0xffffU ? 2 : 1;
        p += length;
    }
    p = lineStart;
    while (p < position) {
        uint32_t codepoint = 0;
        size_t length = 0;
        if (!decode_utf8_codepoint(p, position, &codepoint, &length) || length == 0) {
            ++p;
            ++lineUnits;
            continue;
        }
        lineUnits += codepoint > 0xffffU ? 2 : 1;
        p += length;
    }
    d->m_lineNumber = count_newlines(d->m_data, position);
    d->m_lastLineStart = totalUnits - lineUnits;
    d->m_characterOffset = totalUnits;
    d->m_tokenColumn = lineUnits;
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
    d->m_ownedData = XByteArray_create();
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
    d->m_namespaceBindings = NULL;
    d->m_namespaceBindingCount = 0;
    d->m_namespaceBindingCapacity = 0;
    d->m_tagStack = NULL;
    d->m_tagStackCapacity = 0;
    d->m_tagStackSize = 0;
    d->m_notationDeclarations = XXmlStreamNotationDeclarations_create();
    d->m_entityDeclarations = XXmlStreamEntityDeclarations_create();
    d->m_inputEncoding = XML_INPUT_UTF8;
    d->m_inputInitialized = false;
    d->m_pendingInputLength = 0;
    d->m_pendingUtf16High = 0;
    d->m_hasPendingUtf16High = false;
    d->m_startedDocument = false;
    d->m_seenXmlDeclaration = false;
    d->m_seenDtd = false;
    d->m_finishedRootElement = false;
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
    if (d->m_ownedData) XByteArray_delete_base(d->m_ownedData);
    if (d->m_attributes) {
        XXmlStreamAttributes_delete(d->m_attributes);
        d->m_attributes = NULL;
    }
    if (d->m_notationDeclarations) {
        XXmlStreamNotationDeclarations_delete(d->m_notationDeclarations);
        d->m_notationDeclarations = NULL;
    }
    if (d->m_entityDeclarations) {
        XXmlStreamEntityDeclarations_delete(d->m_entityDeclarations);
        d->m_entityDeclarations = NULL;
    }
    clear_default_attributes(d);
    if (d->m_defaultAttributes) {
        XFree_System(d->m_defaultAttributes);
        d->m_defaultAttributes = NULL;
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
        clear_namespace_declarations(d);
        XFree_System(d->m_namespaceDeclarations);
        d->m_namespaceDeclarations = NULL;
        d->m_namespaceDeclarationCapacity = 0;
    }
    if (d->m_namespaceBindings)
    {
        truncate_namespace_bindings(d, 0);
        XFree_System(d->m_namespaceBindings);
        d->m_namespaceBindings = NULL;
        d->m_namespaceBindingCapacity = 0;
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

/**
 * @brief      重置解析状态并保留 Qt 要求跨输入重置保留的配置。
 * @param      d Reader 私有数据；调用方必须保证对象已初始化。
 * @return     无。
 * @note       QXmlStreamReader::clear/setDevice 会重置解析器状态，但不会
 *             清除实体解析器、实体扩展限制和额外命名空间声明。
 */
static void private_reset_for_input(XXmlStreamReaderPrivate* d)
{
    if (!d) return;
    XXmlStreamEntityResolver* resolver = d->m_entityResolver;
    int entityExpansionLimit = d->m_entityExpansionLimit;
    XmlNamespaceDeclaration* extraDeclarations = d->m_extraNamespaceDeclarations;
    int extraCount = d->m_extraNamespaceDeclarationCount;
    int extraCapacity = d->m_extraNamespaceDeclarationCapacity;

    d->m_entityResolver = NULL;
    d->m_extraNamespaceDeclarations = NULL;
    d->m_extraNamespaceDeclarationCount = 0;
    d->m_extraNamespaceDeclarationCapacity = 0;
    private_deinit(d);
    private_init(d);

    d->m_entityResolver = resolver;
    d->m_entityExpansionLimit = entityExpansionLimit;
    d->m_extraNamespaceDeclarations = extraDeclarations;
    d->m_extraNamespaceDeclarationCount = extraCount;
    d->m_extraNamespaceDeclarationCapacity = extraCapacity;
}

static bool contains_sequence(const char* start, const char* end, const char* sequence, size_t length)
{
    if (!start || !end || !sequence || length == 0) return false;
    for (const char* p = start; p + length <= end; ++p) {
        if (memcmp(p, sequence, length) == 0) return true;
    }
    return false;
}

static bool markup_is_complete(const char* start, const char* end)
{
    if (!start || start >= end || *start != '<') return true;
    const char* p = start + 1;
    if (p >= end) return false;
    if (p + 2 < end && p[0] == '!' && p[1] == '-' && p[2] == '-')
        return contains_sequence(p + 3, end, "-->", 3);
    if (p + 7 < end && strncmp(p, "![CDATA[", 8) == 0)
        return contains_sequence(p + 8, end, "]]>", 3);

    bool quote = false;
    char quoteCharacter = '\0';
    int subsetDepth = 0;
    bool processingInstruction = *p == '?';
    bool doctype = p + 8 <= end && strncmp(p, "!DOCTYPE", 8) == 0;
    for (; p < end; ++p) {
        if (quote) {
            if (*p == quoteCharacter) quote = false;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            quote = true;
            quoteCharacter = *p;
            continue;
        }
        if (doctype) {
            if (*p == '[') subsetDepth++;
            else if (*p == ']' && subsetDepth > 0) subsetDepth--;
        }
        if (*p == '>' && subsetDepth == 0) {
            if (!processingInstruction || (p > start && *(p - 1) == '?')) return true;
        }
    }
    return false;
}

static bool append_normalized_codepoint(XByteArray* output, uint32_t codepoint)
{
    if (!output || !is_xml_char(codepoint)) return false;
    uint8_t utf8[4];
    size_t length = 0;
    if (codepoint <= 0x7fU) {
        utf8[0] = (uint8_t)codepoint;
        length = 1;
    } else if (codepoint <= 0x7ffU) {
        utf8[0] = (uint8_t)(0xc0U | (codepoint >> 6));
        utf8[1] = (uint8_t)(0x80U | (codepoint & 0x3fU));
        length = 2;
    } else if (codepoint <= 0xffffU) {
        utf8[0] = (uint8_t)(0xe0U | (codepoint >> 12));
        utf8[1] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3fU));
        utf8[2] = (uint8_t)(0x80U | (codepoint & 0x3fU));
        length = 3;
    } else {
        utf8[0] = (uint8_t)(0xf0U | (codepoint >> 18));
        utf8[1] = (uint8_t)(0x80U | ((codepoint >> 12) & 0x3fU));
        utf8[2] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3fU));
        utf8[3] = (uint8_t)(0x80U | (codepoint & 0x3fU));
        length = 4;
    }
    return XByteArray_push_back_2(output, utf8, length);
}

static bool reencode_existing_single_byte(XXmlStreamReaderPrivate* d, bool asciiOnly)
{
    if (!d || !d->m_ownedData) return false;
    const uint8_t* bytes = (const uint8_t*)XByteArray_data(d->m_ownedData);
    size_t length = XByteArray_size_base(d->m_ownedData);
    size_t readOffset = 0;
    if (d->m_data && d->m_readPtr && d->m_readPtr >= d->m_data)
        readOffset = (size_t)(d->m_readPtr - d->m_data);
    XByteArray* normalized = XByteArray_create();
    if (!normalized) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < length; ++i) {
        if (asciiOnly && bytes[i] > 0x7fU) {
            ok = false;
            break;
        }
        if (!append_normalized_codepoint(normalized, bytes[i])) {
            ok = false;
            break;
        }
    }
    if (ok) {
        XByteArray_clear_base(d->m_ownedData);
        ok = XByteArray_push_back_2(d->m_ownedData, XByteArray_data(normalized),
                                    XByteArray_size_base(normalized));
    }
    XByteArray_delete_base(normalized);
    if (!ok) return false;
    d->m_data = (const char*)XByteArray_data(d->m_ownedData);
    d->m_dataLength = XByteArray_size_base(d->m_ownedData);
    if (readOffset > d->m_dataLength) readOffset = d->m_dataLength;
    d->m_readPtr = d->m_data + readOffset;
    d->m_endPtr = d->m_data + d->m_dataLength;
    return true;
}

static bool normalize_input(XXmlStreamReaderPrivate* d, const char* data, size_t length)
{
    if (!d || (!data && length > 0) || !d->m_ownedData) return false;
    if (length == 0) return true;

    const char* inputData = data;
    size_t inputLength = length;
    XByteArray* combined = NULL;
    size_t offset = 0;
    if (!d->m_inputInitialized && d->m_pendingInputLength > 0) {
        combined = XByteArray_create();
        if (!combined) return false;
        if (!XByteArray_push_back_2(combined, d->m_pendingInput,
                                    d->m_pendingInputLength) ||
            !XByteArray_push_back_2(combined, data, length)) {
            XByteArray_delete_base(combined);
            return false;
        }
        d->m_pendingInputLength = 0;
        inputData = (const char*)XByteArray_data(combined);
        inputLength = XByteArray_size_base(combined);
    }
    if (!d->m_inputInitialized) {
        if (inputLength < 4) {
            memcpy(d->m_pendingInput, inputData, inputLength);
            d->m_pendingInputLength = inputLength;
            XByteArray_delete_base(combined);
            return true;
        }
        d->m_inputEncoding = XML_INPUT_UTF8;
        if (inputLength >= 4 && (uint8_t)inputData[0] == 0x00 && (uint8_t)inputData[1] == 0x00 &&
            (uint8_t)inputData[2] == 0xfe && (uint8_t)inputData[3] == 0xff) {
            d->m_inputEncoding = XML_INPUT_UTF32BE;
            offset = 4;
        } else if (inputLength >= 4 && (uint8_t)inputData[0] == 0xff && (uint8_t)inputData[1] == 0xfe &&
                   (uint8_t)inputData[2] == 0x00 && (uint8_t)inputData[3] == 0x00) {
            d->m_inputEncoding = XML_INPUT_UTF32LE;
            offset = 4;
        } else if (inputLength >= 3 && (uint8_t)inputData[0] == 0xef &&
                   (uint8_t)inputData[1] == 0xbb && (uint8_t)inputData[2] == 0xbf) {
            d->m_inputEncoding = XML_INPUT_UTF8;
            offset = 3;
        } else if (inputLength >= 2 && (uint8_t)inputData[0] == 0xfe && (uint8_t)inputData[1] == 0xff) {
            d->m_inputEncoding = XML_INPUT_UTF16BE;
            offset = 2;
        } else if (inputLength >= 2 && (uint8_t)inputData[0] == 0xff && (uint8_t)inputData[1] == 0xfe) {
            d->m_inputEncoding = XML_INPUT_UTF16LE;
            offset = 2;
        } else if (inputLength >= 4 && (uint8_t)inputData[0] == 0x00 && (uint8_t)inputData[1] == 0x00 &&
                   (uint8_t)inputData[2] == 0x00 && (uint8_t)inputData[3] == 0x3c) {
            d->m_inputEncoding = XML_INPUT_UTF32BE;
        } else if (inputLength >= 4 && (uint8_t)inputData[0] == 0x3c && (uint8_t)inputData[1] == 0x00 &&
                   (uint8_t)inputData[2] == 0x00 && (uint8_t)inputData[3] == 0x00) {
            d->m_inputEncoding = XML_INPUT_UTF32LE;
        } else if (inputLength >= 4 && (uint8_t)inputData[0] == 0x00 && (uint8_t)inputData[1] == 0x3c &&
                   (uint8_t)inputData[2] == 0x00 && (uint8_t)inputData[3] == 0x3f) {
            d->m_inputEncoding = XML_INPUT_UTF16BE;
        } else if (inputLength >= 4 && (uint8_t)inputData[0] == 0x3c && (uint8_t)inputData[1] == 0x00 &&
                   (uint8_t)inputData[2] == 0x3f && (uint8_t)inputData[3] == 0x00) {
            d->m_inputEncoding = XML_INPUT_UTF16LE;
        }
        d->m_inputInitialized = true;
    }

    XByteArray* normalized = XByteArray_create();
    if (!normalized) return false;
    bool ok = true;
    if (d->m_inputEncoding == XML_INPUT_UTF8) {
        ok = XByteArray_push_back_2(normalized, inputData + offset, inputLength - offset);
    } else if (d->m_inputEncoding == XML_INPUT_LATIN1 ||
               d->m_inputEncoding == XML_INPUT_ASCII) {
        for (size_t i = offset; ok && i < inputLength; ++i) {
            if (d->m_inputEncoding == XML_INPUT_ASCII &&
                (uint8_t)inputData[i] > 0x7fU) {
                ok = false;
                break;
            }
            ok = append_normalized_codepoint(normalized, (uint8_t)inputData[i]);
        }
    } else if (d->m_inputEncoding == XML_INPUT_UTF16LE ||
               d->m_inputEncoding == XML_INPUT_UTF16BE) {
        uint8_t bytes[2];
        size_t byteCount = d->m_pendingInputLength;
        uint16_t pendingHigh = d->m_pendingUtf16High;
        bool hasPendingHigh = d->m_hasPendingUtf16High;
        if (byteCount > 0) memcpy(bytes, d->m_pendingInput, byteCount);
        for (size_t i = offset; ok && i < inputLength; ++i) {
            bytes[byteCount++] = (uint8_t)inputData[i];
            if (byteCount < 2) continue;
            uint16_t unit = d->m_inputEncoding == XML_INPUT_UTF16LE
                ? (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8))
                : (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
            byteCount = 0;
            if (unit >= 0xdc00U && unit <= 0xdfffU) {
                if (!hasPendingHigh) { ok = false; break; }
                uint32_t codepoint = 0x10000U +
                    (((uint32_t)pendingHigh - 0xd800U) << 10) + (unit - 0xdc00U);
                pendingHigh = 0;
                hasPendingHigh = false;
                ok = append_normalized_codepoint(normalized, codepoint);
            } else if (unit >= 0xd800U && unit <= 0xdbffU) {
                if (hasPendingHigh) { ok = false; break; }
                pendingHigh = unit;
                hasPendingHigh = true;
            } else if (unit >= 0xd800U && unit <= 0xdfffU) {
                ok = false;
            } else if (hasPendingHigh) {
                ok = false;
            } else {
                ok = append_normalized_codepoint(normalized, unit);
            }
        }

        d->m_pendingUtf16High = pendingHigh;
        d->m_hasPendingUtf16High = hasPendingHigh;
        d->m_pendingInputLength = byteCount;
        if (ok && byteCount > 0) {
            d->m_pendingInput[0] = bytes[0];
        }
    } else if (d->m_inputEncoding == XML_INPUT_UTF32LE ||
               d->m_inputEncoding == XML_INPUT_UTF32BE) {
        uint8_t bytes[4];
        size_t byteCount = d->m_pendingInputLength;
        if (byteCount > 0) memcpy(bytes, d->m_pendingInput, byteCount);
        for (size_t i = offset; ok && i < inputLength; ++i) {
            bytes[byteCount++] = (uint8_t)inputData[i];
            if (byteCount < 4) continue;
            uint32_t codepoint = d->m_inputEncoding == XML_INPUT_UTF32LE
                ? ((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                   ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24))
                : (((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
                   ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3]);
            byteCount = 0;
            ok = append_normalized_codepoint(normalized, codepoint);
        }
        d->m_pendingInputLength = byteCount;
        if (byteCount > 0) memcpy(d->m_pendingInput, bytes, byteCount);
    }

    if (ok && XByteArray_size_base(normalized) > 0)
        ok = XByteArray_push_back_2(d->m_ownedData, XByteArray_data(normalized),
                                    XByteArray_size_base(normalized));
    XByteArray_delete_base(normalized);
    XByteArray_delete_base(combined);
    if (!ok) {
        set_error(d, XXmlStream_NotWellFormedError, "输入编码或 XML 字符无效。");
        return false;
    }
    d->m_data = (const char*)XByteArray_data(d->m_ownedData);
    d->m_dataLength = XByteArray_size_base(d->m_ownedData);
    return true;
}

static bool has_incomplete_input(const XXmlStreamReaderPrivate* d)
{
    if (!d) return false;
    return d->m_pendingInputLength > 0 || d->m_hasPendingUtf16High;
}

static bool append_input_data(XXmlStreamReaderPrivate* d, const char* data, size_t length)
{
    if (!d || (!data && length > 0) || !d->m_ownedData) return false;
    size_t readOffset = 0;
    if (d->m_data && d->m_readPtr && d->m_readPtr >= d->m_data)
        readOffset = (size_t)(d->m_readPtr - d->m_data);
    if (length > 0 && !normalize_input(d, data, length)) return false;
    d->m_data = (const char*)XByteArray_data(d->m_ownedData);
    d->m_dataLength = XByteArray_size_base(d->m_ownedData);
    if (readOffset > d->m_dataLength) readOffset = d->m_dataLength;
    d->m_readPtr = d->m_data + readOffset;
    d->m_endPtr = d->m_data + d->m_dataLength;
    d->m_isDataFromDevice = false;
    d->m_atEnd = false;
    if (d->m_error == XXmlStream_PrematureEndOfDocumentError) {
        d->m_error = XXmlStream_NoError;
        d->m_type = XXmlStream_NoToken;
        if (d->m_errorString) XString_clear_base(d->m_errorString);
    }
    return true;
}

/* 设备输入只在解析器需要更多字节时读取，避免 setDevice() 提前消耗顺序设备的数据。 */
static bool read_more_from_device(XXmlStreamReaderPrivate* d)
{
    if (!d || !d->m_device) return false;

    XByteArray* input = XIODevice_readAll_3(d->m_device);
    if (!input) return false;

    size_t size = XByteArray_size_base(input);
    bool appended = size > 0 && append_input_data(
        d, (const char*)XByteArray_data(input), size);
    XByteArray_delete_base(input);
    d->m_isDataFromDevice = true;
    return appended;
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
        update_current_position(d, d->m_readPtr);
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
            int namespaceBindingCountBefore = tag->m_namespaceBindingCountBefore;
            XString_delete_base(tag->m_name);
            XString_delete_base(tag->m_qualifiedName);
            XString_delete_base(tag->m_prefix);
            XString_delete_base(tag->m_namespaceUri);
            memset(tag, 0, sizeof(XmlTag));
                d->m_tagStackSize--;
                truncate_namespace_bindings(d, namespaceBindingCountBefore);
                if (d->m_tagStackSize == 0) d->m_finishedRootElement = true;
        }
        update_current_position(d, d->m_readPtr);
        return XXmlStream_EndElement;
    }
    /* 清理当前 Token */
    clear_current_token(d);
    /* 设备输入按需填充；顺序设备可在后续 readNext() 调用中继续提供数据。 */
    if (d->m_device && d->m_isDataFromDevice &&
        (!d->m_readPtr || !d->m_endPtr || d->m_readPtr >= d->m_endPtr)) {
        read_more_from_device(d);
    }

    /* 设置读取指针 */
    const char* ptr = d->m_readPtr;
    const char* end = d->m_endPtr;
    if (!d->m_startedDocument) {
        d->m_startedDocument = true;
        if (ptr && end && ptr < end && *ptr == '<' &&
            (size_t)(end - ptr) >= 5 && memcmp(ptr, "<?xml", 5) == 0 &&
            (ptr + 5 == end || ptr[5] == ' ' || ptr[5] == '\t' ||
             ptr[5] == '\r' || ptr[5] == '\n' || ptr[5] == '?')) {
            ptr += 5;
            if (!parse_xml_declaration(d, &ptr, end)) {
                set_error(d, XXmlStream_NotWellFormedError, "XML 声明无效。");
                return XXmlStream_Invalid;
            }
            end = d->m_endPtr;
            d->m_seenXmlDeclaration = true;
            d->m_type = XXmlStream_StartDocument;
            d->m_readPtr = ptr;
            update_current_position(d, ptr);
            return d->m_type;
        }
        if (d->m_documentVersion && XString_isEmpty_base(d->m_documentVersion))
            XString_assign_utf8(d->m_documentVersion, "1.0");
        d->m_type = XXmlStream_StartDocument;
        d->m_readPtr = ptr;
        update_current_position(d, ptr);
        return d->m_type;
    }
    if (!ptr || !end || ptr >= end)
    {
        d->m_atEnd = true;
        if (has_incomplete_input(d)) {
            set_error(d, XXmlStream_PrematureEndOfDocumentError, "输入编码在文档末尾不完整。");
            update_current_position(d, ptr);
            return XXmlStream_Invalid;
        }
        /* 检测未关闭标签 */
        if (d->m_tagStackSize > 0)
        {
            d->m_type = XXmlStream_EndDocument;
            set_error(d, XXmlStream_PrematureEndOfDocumentError, "文档提前结束：存在未关闭的标签。");
            update_current_position(d, ptr);
            return XXmlStream_Invalid;
        }
        if (!d->m_seenRootElement)
        {
            set_error(d, XXmlStream_NotWellFormedError, "XML 文档缺少根元素。");
            update_current_position(d, ptr);
            return XXmlStream_Invalid;
        }
        if (d->m_type != XXmlStream_EndDocument)
        {
            d->m_type = XXmlStream_EndDocument;
            update_current_position(d, ptr);
            return XXmlStream_EndDocument;
        }
        update_current_position(d, ptr);
        return XXmlStream_NoToken;
    }
    /* 元素内容中的空白是有效字符数据，不能像标签内部空白那样跳过。 */
    if (ptr >= end)
    {
        d->m_readPtr = ptr;
        d->m_atEnd = true;
        /* 检测未关闭标签 */
        if (d->m_tagStackSize > 0)
        {
            d->m_type = XXmlStream_EndDocument;
            set_error(d, XXmlStream_PrematureEndOfDocumentError, "文档提前结束：存在未关闭的标签。");
            update_current_position(d, ptr);
            return XXmlStream_Invalid;
        }
        if (!d->m_seenRootElement)
        {
            set_error(d, XXmlStream_NotWellFormedError, "XML 文档缺少根元素。");
            update_current_position(d, ptr);
            return XXmlStream_Invalid;
        }
        d->m_type = XXmlStream_EndDocument;
        update_current_position(d, ptr);
        return XXmlStream_EndDocument;
    }
    if (*ptr == '<' && !markup_is_complete(ptr, end)) {
        d->m_readPtr = ptr;
        if (d->m_device && d->m_isDataFromDevice && read_more_from_device(d))
            return XXmlStreamReader_readNext(self);
        d->m_atEnd = true;
        set_error(d, XXmlStream_PrematureEndOfDocumentError, "文档提前结束。");
        return XXmlStream_Invalid;
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
            if (ptr + 3 < end && strncmp(ptr, "xml", 3) == 0 &&
                (*(ptr + 3) == ' ' || *(ptr + 3) == '\t' ||
                 *(ptr + 3) == '\r' || *(ptr + 3) == '\n' || *(ptr + 3) == '?'))
            {
                set_error(d, XXmlStream_NotWellFormedError, "XML 声明必须位于文档开头。");
                return XXmlStream_Invalid;
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
                if (d->m_seenDtd || d->m_seenRootElement) {
                    set_error(d, XXmlStream_NotWellFormedError, "DTD 必须位于根元素之前且只能出现一次。");
                    return XXmlStream_Invalid;
                }
                ptr += 7;
                /* DTD */
                if (!parse_dtd(d, &ptr, end))
                {
                    set_error(d, XXmlStream_NotWellFormedError, "解析 DTD 失败。");
                    return XXmlStream_Invalid;
                }
                d->m_seenDtd = true;
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
            if (d->m_error != XXmlStream_NoError)
                return XXmlStream_Invalid;
            /* 如果没有字符内容，尝试结束文档 */
            d->m_atEnd = true;
            d->m_type = XXmlStream_EndDocument;
            d->m_readPtr = ptr;
            return XXmlStream_EndDocument;
        }
    }
    d->m_readPtr = ptr;
    update_current_position(d, ptr);
    return d->m_type;
}
/* ============================================================================
 * XXmlStreamReader 公共 API 实现
 * ============================================================================ */

/* ---- 虚函数表 ---- */

/**
 * @brief      XXmlStreamReader 虚 deinit 函数（清理私有数据）
 * @param obj   目标对象（XXmlStreamReader 指针）
 */
static void VXXmlStreamReader_deinit(XXmlStreamReader* obj)
{
    if (ISNULL(obj, "XXmlStreamReader")) return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(obj + 1);
    private_deinit(d);
}

/* ========== 复制私有数据（深拷贝） ========== */
static void private_copy(XXmlStreamReader* destObject,
                         const XXmlStreamReader* srcObject)
{
    if (!destObject || !srcObject || destObject == srcObject) return;

    /*
     * copy_base 允许目标是仅分配、尚未 init 的栈对象。先完成完整的
     * XXmlStreamReader 初始化，再访问私有区，避免 private_deinit 读取
     * 未初始化的成员。
     */
    if (XClassIsVtableNull(destObject))
        XXmlStreamReader_init(destObject);
    if (XClassIsVtableNull(srcObject)) return;

    XXmlStreamReaderPrivate* dest =
        (XXmlStreamReaderPrivate*)(destObject + 1);
    const XXmlStreamReaderPrivate* src =
        (const XXmlStreamReaderPrivate*)(srcObject + 1);

    /* 释放目标已有资源（如果之前有） */
    private_deinit(dest);
    memset(dest, 0, sizeof(*dest));

    /* 深拷贝所有 XString 字段 */
    if (src->m_name)                         dest->m_name = XString_create_copy(src->m_name);
    if (src->m_qualifiedName)                dest->m_qualifiedName = XString_create_copy(src->m_qualifiedName);
    if (src->m_prefix)                       dest->m_prefix = XString_create_copy(src->m_prefix);
    if (src->m_namespaceUri)                 dest->m_namespaceUri = XString_create_copy(src->m_namespaceUri);
    if (src->m_text)                         dest->m_text = XString_create_copy(src->m_text);
    if (src->m_documentVersion)              dest->m_documentVersion = XString_create_copy(src->m_documentVersion);
    if (src->m_documentEncoding)             dest->m_documentEncoding = XString_create_copy(src->m_documentEncoding);
    if (src->m_dtdName)                      dest->m_dtdName = XString_create_copy(src->m_dtdName);
    if (src->m_dtdPublicId)                  dest->m_dtdPublicId = XString_create_copy(src->m_dtdPublicId);
    if (src->m_dtdSystemId)                  dest->m_dtdSystemId = XString_create_copy(src->m_dtdSystemId);
    if (src->m_processingInstructionTarget)  dest->m_processingInstructionTarget = XString_create_copy(src->m_processingInstructionTarget);
    if (src->m_processingInstructionData)    dest->m_processingInstructionData = XString_create_copy(src->m_processingInstructionData);
    if (src->m_errorString)                  dest->m_errorString = XString_create_copy(src->m_errorString);
    if (src->m_buffer)                       dest->m_buffer = XString_create_copy(src->m_buffer);
    if (src->m_ownedData)                    dest->m_ownedData = XByteArray_create_copy(src->m_ownedData);

    /* 深拷贝 attributes */
    if (src->m_attributes) {
        dest->m_attributes = XXmlStreamAttributes_create();
        if (dest->m_attributes) {
            int count = src->m_attributes->m_count;
            for (int i = 0; i < count; i++) {
                XXmlStreamAttribute* srcAttr = src->m_attributes->m_items[i];
                if (srcAttr) {
                    XXmlStreamAttribute* newAttr = XXmlStreamAttribute_create_ex(
                        srcAttr->m_namespaceUri, srcAttr->m_name, srcAttr->m_value);
                    if (newAttr && srcAttr->m_qualifiedName) {
                        if (newAttr->m_qualifiedName) XString_delete_base(newAttr->m_qualifiedName);
                        newAttr->m_qualifiedName = XString_create_copy(srcAttr->m_qualifiedName);
                        if (newAttr->m_prefix) XString_delete_base(newAttr->m_prefix);
                        newAttr->m_prefix = srcAttr->m_prefix
                            ? XString_create_copy(srcAttr->m_prefix) : XString_create();
                        newAttr->m_isDefault = srcAttr->m_isDefault;
                    }
                    if (newAttr) {
                        if (dest->m_attributes->m_count >= dest->m_attributes->m_capacity) {
                            int newCapacity = dest->m_attributes->m_capacity
                                ? dest->m_attributes->m_capacity * 2 : 4;
                            XXmlStreamAttribute** resized = (XXmlStreamAttribute**)XRealloc_System(
                                dest->m_attributes->m_items,
                                (size_t)newCapacity * sizeof(XXmlStreamAttribute*));
                            if (!resized) {
                                XXmlStreamAttribute_delete(newAttr);
                                continue;
                            }
                            dest->m_attributes->m_items = resized;
                            dest->m_attributes->m_capacity = newCapacity;
                        }
                        dest->m_attributes->m_items[dest->m_attributes->m_count++] = newAttr;
                    }
                }
            }
        }
    }

    if (src->m_namespaceBindings && src->m_namespaceBindingCount > 0) {
        dest->m_namespaceBindingCapacity = src->m_namespaceBindingCapacity;
        dest->m_namespaceBindings = (XmlNamespaceDeclaration*)XMalloc_System(
            sizeof(XmlNamespaceDeclaration) * (size_t)dest->m_namespaceBindingCapacity);
        if (dest->m_namespaceBindings) {
            memset(dest->m_namespaceBindings, 0,
                   sizeof(XmlNamespaceDeclaration) * (size_t)dest->m_namespaceBindingCapacity);
            for (int i = 0; i < src->m_namespaceBindingCount; ++i) {
                dest->m_namespaceBindings[i].m_prefix =
                    XString_create_copy(src->m_namespaceBindings[i].m_prefix);
                dest->m_namespaceBindings[i].m_namespaceUri =
                    XString_create_copy(src->m_namespaceBindings[i].m_namespaceUri);
            }
            dest->m_namespaceBindingCount = src->m_namespaceBindingCount;
        }
    }

    /* 深拷贝 namespaceDeclarations 数组 */
    if (src->m_namespaceDeclarations && src->m_namespaceDeclarationCount > 0) {
        dest->m_namespaceDeclarationCapacity = src->m_namespaceDeclarationCapacity;
        dest->m_namespaceDeclarations = (XmlNamespaceDeclaration*)XMalloc_System(
            sizeof(XmlNamespaceDeclaration) * dest->m_namespaceDeclarationCapacity);
        if (dest->m_namespaceDeclarations) {
            for (int i = 0; i < src->m_namespaceDeclarationCount; i++) {
                if (src->m_namespaceDeclarations[i].m_prefix)
                    dest->m_namespaceDeclarations[i].m_prefix = XString_create_copy(src->m_namespaceDeclarations[i].m_prefix);
                if (src->m_namespaceDeclarations[i].m_namespaceUri)
                    dest->m_namespaceDeclarations[i].m_namespaceUri = XString_create_copy(src->m_namespaceDeclarations[i].m_namespaceUri);
            }
            dest->m_namespaceDeclarationCount = src->m_namespaceDeclarationCount;
        }
    }

    /* 深拷贝 extraNamespaceDeclarations 数组 */
    if (src->m_extraNamespaceDeclarations && src->m_extraNamespaceDeclarationCount > 0) {
        dest->m_extraNamespaceDeclarationCapacity = src->m_extraNamespaceDeclarationCapacity;
        dest->m_extraNamespaceDeclarations = (XmlNamespaceDeclaration*)XMalloc_System(
            sizeof(XmlNamespaceDeclaration) * dest->m_extraNamespaceDeclarationCapacity);
        if (dest->m_extraNamespaceDeclarations) {
            for (int i = 0; i < src->m_extraNamespaceDeclarationCount; i++) {
                if (src->m_extraNamespaceDeclarations[i].m_prefix)
                    dest->m_extraNamespaceDeclarations[i].m_prefix = XString_create_copy(src->m_extraNamespaceDeclarations[i].m_prefix);
                if (src->m_extraNamespaceDeclarations[i].m_namespaceUri)
                    dest->m_extraNamespaceDeclarations[i].m_namespaceUri = XString_create_copy(src->m_extraNamespaceDeclarations[i].m_namespaceUri);
            }
            dest->m_extraNamespaceDeclarationCount = src->m_extraNamespaceDeclarationCount;
        }
    }

    /* 深拷贝 tagStack 数组 */
    if (src->m_tagStack && src->m_tagStackSize > 0) {
        dest->m_tagStackCapacity = src->m_tagStackCapacity;
        dest->m_tagStack = (XmlTag*)XMalloc_System(sizeof(XmlTag) * dest->m_tagStackCapacity);
        if (dest->m_tagStack) {
            for (int i = 0; i < src->m_tagStackSize; i++) {
                if (src->m_tagStack[i].m_name)          dest->m_tagStack[i].m_name = XString_create_copy(src->m_tagStack[i].m_name);
                if (src->m_tagStack[i].m_qualifiedName) dest->m_tagStack[i].m_qualifiedName = XString_create_copy(src->m_tagStack[i].m_qualifiedName);
                if (src->m_tagStack[i].m_prefix)        dest->m_tagStack[i].m_prefix = XString_create_copy(src->m_tagStack[i].m_prefix);
                if (src->m_tagStack[i].m_namespaceUri)  dest->m_tagStack[i].m_namespaceUri = XString_create_copy(src->m_tagStack[i].m_namespaceUri);
                dest->m_tagStack[i].m_namespaceBindingCountBefore =
                    src->m_tagStack[i].m_namespaceBindingCountBefore;
            }
            dest->m_tagStackSize = src->m_tagStackSize;
        }
    }

    /* 简单类型字段 */
    dest->m_type = src->m_type;
    dest->m_error = src->m_error;
    dest->m_lineNumber = src->m_lineNumber;
    dest->m_lastLineStart = src->m_lastLineStart;
    dest->m_characterOffset = src->m_characterOffset;
    dest->m_tokenColumn = src->m_tokenColumn;
    dest->m_atEnd = src->m_atEnd;
    dest->m_seenRootElement = src->m_seenRootElement;
    dest->m_isCDATA = src->m_isCDATA;
    dest->m_isWhitespace = src->m_isWhitespace;
    dest->m_isEmptyElement = src->m_isEmptyElement;
    dest->m_lockEncoding = src->m_lockEncoding;
    dest->m_entityExpansionLimit = src->m_entityExpansionLimit;
    dest->m_namespaceProcessing = src->m_namespaceProcessing;
    dest->m_isStandaloneDocument = src->m_isStandaloneDocument;
    dest->m_hasStandalone = src->m_hasStandalone;

    /* 设备/解析器等外部资源只复制指针（不深拷贝） */
    dest->m_device = src->m_device;
    dest->m_deleteDevice = false;  /* 复制时新对象不负责删除 */
    dest->m_dataLength = src->m_dataLength;
    if (dest->m_ownedData) {
        size_t readOffset = (src->m_data && src->m_readPtr && src->m_readPtr >= src->m_data)
            ? (size_t)(src->m_readPtr - src->m_data) : 0;
        dest->m_data = (const char*)XByteArray_data(dest->m_ownedData);
        dest->m_readPtr = dest->m_data + readOffset;
        dest->m_endPtr = dest->m_data + dest->m_dataLength;
    } else {
        dest->m_data = src->m_data;
        dest->m_readPtr = src->m_readPtr;
        dest->m_endPtr = src->m_endPtr;
    }
    dest->m_isDataFromDevice = src->m_isDataFromDevice;
    dest->m_notationDeclarations = XXmlStreamNotationDeclarations_create();
    dest->m_entityDeclarations = XXmlStreamEntityDeclarations_create();
    if (src->m_notationDeclarations) {
        for (size_t i = 0; dest->m_notationDeclarations &&
                i < src->m_notationDeclarations->m_count; ++i) {
            const XXmlStreamNotationDeclaration* item =
                &src->m_notationDeclarations->m_declarations[i];
            append_notation_declaration(dest, item->m_name, item->m_systemId, item->m_publicId);
        }
    }
    if (src->m_entityDeclarations) {
        for (size_t i = 0; dest->m_entityDeclarations &&
                i < src->m_entityDeclarations->m_count; ++i) {
            const XXmlStreamEntityDeclaration* item =
                &src->m_entityDeclarations->m_declarations[i];
            append_entity_declaration(dest, item->m_name, item->m_notationName,
                                      item->m_systemId, item->m_publicId, item->m_value);
        }
    }
    for (int i = 0; i < src->m_defaultAttributeCount; ++i) {
        const XmlDefaultAttribute* item = &src->m_defaultAttributes[i];
        append_default_attribute(dest, item->m_elementName, item->m_attributeName,
                                 item->m_value, item->m_required);
    }
    dest->m_entityResolver = src->m_entityResolver;
    dest->m_inputEncoding = src->m_inputEncoding;
    dest->m_inputInitialized = src->m_inputInitialized;
    memcpy(dest->m_pendingInput, src->m_pendingInput, sizeof(dest->m_pendingInput));
    dest->m_pendingInputLength = src->m_pendingInputLength;
    dest->m_pendingUtf16High = src->m_pendingUtf16High;
    dest->m_hasPendingUtf16High = src->m_hasPendingUtf16High;
    dest->m_startedDocument = src->m_startedDocument;
    dest->m_seenXmlDeclaration = src->m_seenXmlDeclaration;
    dest->m_seenDtd = src->m_seenDtd;
    dest->m_finishedRootElement = src->m_finishedRootElement;
}

/* ========== 移动私有数据（转移所有权） ========== */
static void private_move(XXmlStreamReader* destObject,
                         XXmlStreamReader* srcObject)
{
    if (!destObject || !srcObject || destObject == srcObject) return;

    /* move_base 同样允许目标尚未 init，必须先建立类和私有数据状态。 */
    if (XClassIsVtableNull(destObject))
        XXmlStreamReader_init(destObject);
    if (XClassIsVtableNull(srcObject)) return;

    XXmlStreamReaderPrivate* dest =
        (XXmlStreamReaderPrivate*)(destObject + 1);
    XXmlStreamReaderPrivate* src =
        (XXmlStreamReaderPrivate*)(srcObject + 1);

    /* 释放目标已有资源 */
    private_deinit(dest);
    memset(dest, 0, sizeof(*dest));

    /* 转移所有 XString 字段 */
    dest->m_name = src->m_name;                         src->m_name = NULL;
    dest->m_qualifiedName = src->m_qualifiedName;       src->m_qualifiedName = NULL;
    dest->m_prefix = src->m_prefix;                     src->m_prefix = NULL;
    dest->m_namespaceUri = src->m_namespaceUri;         src->m_namespaceUri = NULL;
    dest->m_text = src->m_text;                         src->m_text = NULL;
    dest->m_documentVersion = src->m_documentVersion;   src->m_documentVersion = NULL;
    dest->m_documentEncoding = src->m_documentEncoding; src->m_documentEncoding = NULL;
    dest->m_dtdName = src->m_dtdName;                   src->m_dtdName = NULL;
    dest->m_dtdPublicId = src->m_dtdPublicId;           src->m_dtdPublicId = NULL;
    dest->m_dtdSystemId = src->m_dtdSystemId;           src->m_dtdSystemId = NULL;
    dest->m_processingInstructionTarget = src->m_processingInstructionTarget; src->m_processingInstructionTarget = NULL;
    dest->m_processingInstructionData = src->m_processingInstructionData;     src->m_processingInstructionData = NULL;
    dest->m_errorString = src->m_errorString;           src->m_errorString = NULL;
    dest->m_buffer = src->m_buffer;                     src->m_buffer = NULL;
    dest->m_ownedData = src->m_ownedData;               src->m_ownedData = NULL;

    /* 转移 attributes */
    dest->m_attributes = src->m_attributes;             src->m_attributes = NULL;

    /* 转移 namespaceDeclarations 数组 */
    dest->m_namespaceDeclarations = src->m_namespaceDeclarations;
    dest->m_namespaceDeclarationCount = src->m_namespaceDeclarationCount;
    dest->m_namespaceDeclarationCapacity = src->m_namespaceDeclarationCapacity;
    src->m_namespaceDeclarations = NULL;
    src->m_namespaceDeclarationCount = 0;
    src->m_namespaceDeclarationCapacity = 0;

    dest->m_namespaceBindings = src->m_namespaceBindings;
    dest->m_namespaceBindingCount = src->m_namespaceBindingCount;
    dest->m_namespaceBindingCapacity = src->m_namespaceBindingCapacity;
    src->m_namespaceBindings = NULL;
    src->m_namespaceBindingCount = 0;
    src->m_namespaceBindingCapacity = 0;

    /* 转移 extraNamespaceDeclarations 数组 */
    dest->m_extraNamespaceDeclarations = src->m_extraNamespaceDeclarations;
    dest->m_extraNamespaceDeclarationCount = src->m_extraNamespaceDeclarationCount;
    dest->m_extraNamespaceDeclarationCapacity = src->m_extraNamespaceDeclarationCapacity;
    src->m_extraNamespaceDeclarations = NULL;
    src->m_extraNamespaceDeclarationCount = 0;
    src->m_extraNamespaceDeclarationCapacity = 0;

    /* 转移 tagStack 数组 */
    dest->m_tagStack = src->m_tagStack;
    dest->m_tagStackSize = src->m_tagStackSize;
    dest->m_tagStackCapacity = src->m_tagStackCapacity;
    src->m_tagStack = NULL;
    src->m_tagStackSize = 0;
    src->m_tagStackCapacity = 0;

    /* 简单类型字段 */
    dest->m_type = src->m_type;
    dest->m_error = src->m_error;
    dest->m_lineNumber = src->m_lineNumber;
    dest->m_lastLineStart = src->m_lastLineStart;
    dest->m_characterOffset = src->m_characterOffset;
    dest->m_tokenColumn = src->m_tokenColumn;
    dest->m_atEnd = src->m_atEnd;
    dest->m_seenRootElement = src->m_seenRootElement;
    dest->m_isCDATA = src->m_isCDATA;
    dest->m_isWhitespace = src->m_isWhitespace;
    dest->m_isEmptyElement = src->m_isEmptyElement;
    dest->m_lockEncoding = src->m_lockEncoding;
    dest->m_entityExpansionLimit = src->m_entityExpansionLimit;
    dest->m_namespaceProcessing = src->m_namespaceProcessing;
    dest->m_isStandaloneDocument = src->m_isStandaloneDocument;
    dest->m_hasStandalone = src->m_hasStandalone;

    /* 转移设备/解析器所有权 */
    dest->m_device = src->m_device;
    dest->m_deleteDevice = src->m_deleteDevice;
    dest->m_data = src->m_data;
    dest->m_dataLength = src->m_dataLength;
    dest->m_readPtr = src->m_readPtr;
    dest->m_endPtr = src->m_endPtr;
    dest->m_isDataFromDevice = src->m_isDataFromDevice;
    dest->m_notationDeclarations = src->m_notationDeclarations;
    dest->m_entityDeclarations = src->m_entityDeclarations;
    dest->m_entityResolver = src->m_entityResolver;
    dest->m_defaultAttributes = src->m_defaultAttributes;
    dest->m_defaultAttributeCount = src->m_defaultAttributeCount;
    dest->m_defaultAttributeCapacity = src->m_defaultAttributeCapacity;
    dest->m_inputEncoding = src->m_inputEncoding;
    dest->m_inputInitialized = src->m_inputInitialized;
    memcpy(dest->m_pendingInput, src->m_pendingInput, sizeof(dest->m_pendingInput));
    dest->m_pendingInputLength = src->m_pendingInputLength;
    dest->m_pendingUtf16High = src->m_pendingUtf16High;
    dest->m_hasPendingUtf16High = src->m_hasPendingUtf16High;
    dest->m_startedDocument = src->m_startedDocument;
    dest->m_seenXmlDeclaration = src->m_seenXmlDeclaration;
    dest->m_seenDtd = src->m_seenDtd;
    dest->m_finishedRootElement = src->m_finishedRootElement;
    src->m_device = NULL;
    src->m_data = NULL;
    src->m_readPtr = NULL;
    src->m_endPtr = NULL;
    src->m_dataLength = 0;
    src->m_deleteDevice = false;
    src->m_isDataFromDevice = false;
    src->m_seenRootElement = false;
    src->m_notationDeclarations = NULL;
    src->m_entityDeclarations = NULL;
    src->m_entityResolver = NULL;
    src->m_defaultAttributes = NULL;
    src->m_defaultAttributeCount = 0;
    src->m_defaultAttributeCapacity = 0;
    src->m_pendingInputLength = 0;
    src->m_pendingUtf16High = 0;
    src->m_hasPendingUtf16High = false;
    src->m_inputInitialized = false;
    src->m_startedDocument = false;
    src->m_seenXmlDeclaration = false;
    src->m_seenDtd = false;
    src->m_finishedRootElement = false;
}

/**
 * @brief      XXmlStreamReader 虚拷贝函数（深拷贝私有数据）
 * @param dest  目标对象
 * @param src   源对象
 */
static void VXXmlStreamReader_copy(XXmlStreamReader* dest, const XXmlStreamReader* src)
{
    if (!dest || !src || dest == src) return;
    private_copy(dest, src);
}

/**
 * @brief      XXmlStreamReader 虚移动函数（转移所有权）
 * @param dest  目标对象
 * @param src   源对象（移动后被清空）
 */
static void VXXmlStreamReader_move(XXmlStreamReader* dest, XXmlStreamReader* src)
{
    if (!dest || !src || dest == src) return;
    private_move(dest, src);
}

/**
 * @brief      初始化 XXmlStreamReader 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XXmlStreamReader_class_init(void)
{
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_SIZE)
	XCLASS_SET_CLASS_NAME_DEFAULT("XXmlStreamReader");
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXXmlStreamReader_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy,   VXXmlStreamReader_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move,   VXXmlStreamReader_move);
    return XVTABLE_DEFAULT;
}

/* ---- 创建与初始化 ---- */

/**
 * @brief      在堆上创建 XXmlStreamReader 实例
 * @return     指向新创建的 XXmlStreamReader 对象的指针，失败返回 NULL
 */
XXmlStreamReader* XXmlStreamReader_create_ex(XMemoryType memory)
{
    size_t totalSize = sizeof(XXmlStreamReader) + sizeof(XXmlStreamReaderPrivate);
    XXmlStreamReader* self = (XXmlStreamReader*)XMemory_malloc(totalSize, memory);
    if (!self) return NULL;
    XXmlStreamReader_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

XXmlStreamReader* XXmlStreamReader_create_copy(const XXmlStreamReader* other)
{
    if (!other) return NULL;
    XXmlStreamReader* self = XXmlStreamReader_create();
    if (!self) return NULL;
    XCopy(self, other);
    return self;
}

XXmlStreamReader* XXmlStreamReader_create_move(XXmlStreamReader* other)
{
    if (!other) return NULL;
    XXmlStreamReader* self = XXmlStreamReader_create();
    if (!self) return NULL;
    XMove(self, other);
    return self;
}

XXmlStreamReader* XXmlStreamReader_create_byteArray(const XByteArray* data)
{
    XXmlStreamReader* self = XXmlStreamReader_create();
    if (self) XXmlStreamReader_addData(self, data);
    return self;
}

XXmlStreamReader* XXmlStreamReader_create_string(const XString* data)
{
    XXmlStreamReader* self = XXmlStreamReader_create();
    if (self) XXmlStreamReader_addData_string(self, data);
    return self;
}

XXmlStreamReader* XXmlStreamReader_create_utf8(const char* data)
{
    XXmlStreamReader* self = XXmlStreamReader_create();
    if (self) XXmlStreamReader_addData_utf8(self, data);
    return self;
}

XXmlStreamReader* XXmlStreamReader_create_device(XIODevice* device)
{
    XXmlStreamReader* self = XXmlStreamReader_create();
    if (self) XXmlStreamReader_setDevice(self, device);
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
    private_reset_for_input(d);
    d->m_device = device;
    d->m_deleteDevice = false;
    d->m_isDataFromDevice = device != NULL;
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
    append_input_data(d, data, length);
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
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (d->m_device)
    {
        /* 有设备时忽略 addData */
        return;
    }
    append_input_data(d, (const char*)XByteArray_data(data), XByteArray_size_base(data));
}

/**
 * @brief      添加 UTF-16 XString 输入数据。
 * @param      self 目标 XXmlStreamReader 对象指针。
 * @param      data UTF-16 XML 文本，只在调用期间借用。
 * @return     无；NULL 输入或设备输入状态不执行操作。
 * @note       对标 Qt QXmlStreamReader::addData(QString)，内部通过 XString 的
 *             UTF-8 表示追加到增量输入缓冲区，不调用平台 API。
 */
void XXmlStreamReader_addData_string(XXmlStreamReader* self, const XString* data)
{
    if (ISNULL(self, "XXmlStreamReader") || !data) return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (d->m_device) return;
    /* QXmlStreamReader::addData(QString) 固定使用 UTF-8 解码器。 */
    d->m_lockEncoding = true;
    d->m_inputEncoding = XML_INPUT_UTF8;
    append_input_data(d, XString_toUtf8(data), XString_toUtf8_length(data));
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
    private_reset_for_input(d);
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
    return d->m_atEnd || d->m_type == XXmlStream_Invalid;
}
/* ---- Token 类型 ---- */

int XXmlStreamReader_tokenType(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return XXmlStream_Invalid;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type;
}

/* token 名称字符串用 const char* 静态存储，节省内存 */
static const char* s_tokenStrings[12] = {
    "NoToken",
    "Invalid",
    "StartDocument",
    "EndDocument",
    "StartElement",
    "EndElement",
    "Characters",
    "Comment",
    "DTD",
    "EntityReference",
    "ProcessingInstruction",
    "Unknown"
};

const char* XXmlStreamReader_tokenString(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) {
        return s_tokenStrings[XXmlStream_Invalid];
    }
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (d->m_type < 0 || d->m_type > 10) return s_tokenStrings[11]; /* Unknown */
    return s_tokenStrings[d->m_type];
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

const XString* XXmlStreamReader_processingInstructionTarget(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_processingInstructionTarget;
}

const XString* XXmlStreamReader_processingInstructionData(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader"))
        return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_processingInstructionData;
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

static bool xxml_stream_string_equals(const XString* left, const XString* right)
{
    if (!left || !right) return left == right;
    return XString_equals(left, right, XChar_CaseSensitive);
}

XXmlStreamAttribute* XXmlStreamAttribute_create(const XString* qualifiedName, const XString* value)
{
    XXmlStreamAttribute* self = (XXmlStreamAttribute*)XClass_Malloc(XXmlStreamAttribute);
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamAttribute));
    self->m_qualifiedName = qualifiedName ? XString_create_copy(qualifiedName) : NULL;
    self->m_prefix = XString_create();
    if (qualifiedName) {
        const char* qualified = XString_toUtf8(qualifiedName);
        const char* colon = qualified ? strchr(qualified, ':') : NULL;
        self->m_name = XString_create_utf8(colon ? colon + 1 : (qualified ? qualified : ""));
        if (colon) XString_assign_with_length_utf8(self->m_prefix, qualified,
                                                    (size_t)(colon - qualified));
    }
    self->m_value = value ? XString_create_copy(value) : NULL;
    if (!self->m_prefix || (qualifiedName && (!self->m_qualifiedName || !self->m_name))) {
        XXmlStreamAttribute_delete(self);
        return NULL;
    }
    return self;
}

XXmlStreamAttribute* XXmlStreamAttribute_create_ex(const XString* namespaceUri, const XString* name, const XString* value)
{
    XXmlStreamAttribute* self = (XXmlStreamAttribute*)XClass_Malloc(XXmlStreamAttribute);
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamAttribute));
    self->m_namespaceUri = namespaceUri ? XString_create_copy(namespaceUri) : NULL;
    self->m_name = name ? XString_create_copy(name) : NULL;
    /* 无前缀时，qualifiedName 等于 name（带前缀时上层负责补齐） */
    self->m_qualifiedName = name ? XString_create_copy(name) : NULL;
    self->m_prefix = XString_create();
    self->m_value = value ? XString_create_copy(value) : NULL;
    if (!self->m_prefix || (name && (!self->m_name || !self->m_qualifiedName))) {
        XXmlStreamAttribute_delete(self);
        return NULL;
    }
    return self;
}

void XXmlStreamAttribute_delete(XXmlStreamAttribute* self)
{
    if (!self) return;
    if (self->m_namespaceUri) XString_delete_base(self->m_namespaceUri);
    if (self->m_name) XString_delete_base(self->m_name);
    if (self->m_qualifiedName) XString_delete_base(self->m_qualifiedName);
    if (self->m_prefix) XString_delete_base(self->m_prefix);
    if (self->m_value) XString_delete_base(self->m_value);
    XFree_System(self);
}

static XXmlStreamAttribute* xxml_stream_attribute_copy_private(
    const XXmlStreamAttribute* source)
{
    if (!source) return NULL;
    XXmlStreamAttribute* copy = (XXmlStreamAttribute*)XMalloc_System(
        sizeof(XXmlStreamAttribute));
    if (!copy) return NULL;
    memset(copy, 0, sizeof(XXmlStreamAttribute));
    copy->m_namespaceUri = source->m_namespaceUri ?
        XString_create_copy(source->m_namespaceUri) : NULL;
    copy->m_name = source->m_name ? XString_create_copy(source->m_name) : NULL;
    copy->m_qualifiedName = source->m_qualifiedName ?
        XString_create_copy(source->m_qualifiedName) : NULL;
    copy->m_prefix = source->m_prefix ? XString_create_copy(source->m_prefix) : NULL;
    copy->m_value = source->m_value ? XString_create_copy(source->m_value) : NULL;
    copy->m_isDefault = source->m_isDefault;
    if ((source->m_namespaceUri && !copy->m_namespaceUri) ||
        (source->m_name && !copy->m_name) ||
        (source->m_qualifiedName && !copy->m_qualifiedName) ||
        (source->m_prefix && !copy->m_prefix) ||
        (source->m_value && !copy->m_value)) {
        XXmlStreamAttribute_delete(copy);
        return NULL;
    }
    return copy;
}

XXmlStreamAttributes* XXmlStreamAttributes_create(void)
{
    XXmlStreamAttributes* self = (XXmlStreamAttributes*)XClass_Malloc(XXmlStreamAttributes);
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

static bool xxml_stream_attributes_reserve(XXmlStreamAttributes* self, int needed)
{
    if (!self || needed < 0) return false;
    if (needed <= self->m_capacity) return true;
    int capacity = self->m_capacity ? self->m_capacity : 4;
    while (capacity < needed) {
        if (capacity > INT32_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    XXmlStreamAttribute** items = (XXmlStreamAttribute**)XRealloc_System(
        self->m_items, sizeof(XXmlStreamAttribute*) * (size_t)capacity);
    if (!items) return false;
    self->m_items = items;
    self->m_capacity = capacity;
    return true;
}


/* ============================================================================
 * XXmlStreamAttribute getter 实现
 * ============================================================================ */

const XString* XXmlStreamAttribute_namespaceUri(const XXmlStreamAttribute* self)
{
    if (!self) return NULL;
    return self->m_namespaceUri;
}

const XString* XXmlStreamAttribute_name(const XXmlStreamAttribute* self)
{
    if (!self) return NULL;
    return self->m_name;
}

const XString* XXmlStreamAttribute_qualifiedName(const XXmlStreamAttribute* self)
{
    if (!self) return NULL;
    return self->m_qualifiedName;
}

const XString* XXmlStreamAttribute_prefix(const XXmlStreamAttribute* self)
{
    return self ? self->m_prefix : NULL;
}

const XString* XXmlStreamAttribute_value(const XXmlStreamAttribute* self)
{
    if (!self) return NULL;
    return self->m_value;
}

bool XXmlStreamAttribute_isDefault(const XXmlStreamAttribute* self)
{
    return self ? self->m_isDefault : false;
}

bool XXmlStreamAttribute_equals(const XXmlStreamAttribute* left,
    const XXmlStreamAttribute* right)
{
    if (!left || !right) return left == right;
    if (!xxml_stream_string_equals(left->m_value, right->m_value)) return false;
    if (!left->m_namespaceUri || !right->m_namespaceUri)
        return xxml_stream_string_equals(left->m_qualifiedName, right->m_qualifiedName);
    return xxml_stream_string_equals(left->m_namespaceUri, right->m_namespaceUri) &&
        xxml_stream_string_equals(left->m_name, right->m_name);
}

/* ============================================================================
 * XXmlStreamAttributes getter 实现
 * ============================================================================ */

int XXmlStreamAttributes_size(const XXmlStreamAttributes* self)
{
    return self ? self->m_count : 0;
}

int XXmlStreamAttributes_count(const XXmlStreamAttributes* self)
{
    return XXmlStreamAttributes_size(self);
}

bool XXmlStreamAttributes_isEmpty(const XXmlStreamAttributes* self)
{
    return XXmlStreamAttributes_size(self) == 0;
}

bool XXmlStreamAttributes_equals(const XXmlStreamAttributes* left,
    const XXmlStreamAttributes* right)
{
    if (!left || !right) return left == right;
    if (left->m_count != right->m_count) return false;
    for (int i = 0; i < left->m_count; ++i) {
        if (!XXmlStreamAttribute_equals(left->m_items[i], right->m_items[i])) return false;
    }
    return true;
}

const XXmlStreamAttribute* XXmlStreamAttributes_at(const XXmlStreamAttributes* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return self->m_items[index];
}

const XString* XXmlStreamAttributes_value(const XXmlStreamAttributes* self, const XString* qualifiedName)
{
    if (!self || !qualifiedName) return NULL;
    for (int i = 0; i < self->m_count; i++) {
        if (self->m_items[i] && self->m_items[i]->m_qualifiedName) {
            if (XString_equals(self->m_items[i]->m_qualifiedName, qualifiedName, XChar_CaseSensitive))
                return self->m_items[i]->m_value;
        }
    }
    return NULL;
}

const XString* XXmlStreamAttributes_value_ex(const XXmlStreamAttributes* self, const XString* namespaceUri, const XString* name)
{
    if (!self || !name) return NULL;
    for (int i = 0; i < self->m_count; i++) {
        if (!self->m_items[i]) continue;
        if (self->m_items[i]->m_name && XString_equals(self->m_items[i]->m_name, name, XChar_CaseSensitive)) {
            if (!namespaceUri || XString_isEmpty_base(namespaceUri)) {
                if (!self->m_items[i]->m_namespaceUri ||
                    XString_isEmpty_base(self->m_items[i]->m_namespaceUri))
                    return self->m_items[i]->m_value;
            } else if (self->m_items[i]->m_namespaceUri &&
                       XString_equals(self->m_items[i]->m_namespaceUri, namespaceUri, XChar_CaseSensitive)) {
                return self->m_items[i]->m_value;
            }
        }
    }
    return NULL;
}

bool XXmlStreamAttributes_hasAttribute(const XXmlStreamAttributes* self, const XString* qualifiedName)
{
    if (!self || !qualifiedName) return false;
    for (int i = 0; i < self->m_count; i++) {
        if (self->m_items[i] && self->m_items[i]->m_qualifiedName) {
            if (XString_equals(self->m_items[i]->m_qualifiedName, qualifiedName, XChar_CaseSensitive))
                return true;
        }
    }
    return false;
}

bool XXmlStreamAttributes_hasAttribute_ex(const XXmlStreamAttributes* self,
    const XString* namespaceUri, const XString* name)
{
    return XXmlStreamAttributes_value_ex(self, namespaceUri, name) != NULL;
}

void XXmlStreamAttributes_append(XXmlStreamAttributes* self, const XString* namespaceUri, const XString* name, const XString* value)
{
    if (!self) return;
    XXmlStreamAttribute* attr = XXmlStreamAttribute_create_ex(namespaceUri, name, value);
    if (!attr) return;
    if (!xxml_stream_attributes_reserve(self, self->m_count + 1)) {
        XXmlStreamAttribute_delete(attr); return;
    }
    self->m_items[self->m_count++] = attr;
}

void XXmlStreamAttributes_append_ex(XXmlStreamAttributes* self, const XString* qualifiedName, const XString* value)
{
    if (!self) return;
    XXmlStreamAttribute* attr = XXmlStreamAttribute_create(qualifiedName, value);
    if (!attr) return;
    if (!xxml_stream_attributes_reserve(self, self->m_count + 1)) {
        XXmlStreamAttribute_delete(attr); return;
    }
    self->m_items[self->m_count++] = attr;
}

void XXmlStreamAttributes_append_utf8(XXmlStreamAttributes* self, const char* namespaceUri, const char* name, const char* value)
{
    if (!self || !name) return;
    XString* ns = namespaceUri ? XString_create_utf8(namespaceUri) : NULL;
    XString* nm = XString_create_utf8(name);
    XString* val = value ? XString_create_utf8(value) : NULL;
    XXmlStreamAttributes_append(self, ns, nm, val);
    if (ns) XString_delete_base(ns);
    if (nm) XString_delete_base(nm);
    if (val) XString_delete_base(val);
}

void XXmlStreamAttributes_append_ex_utf8(XXmlStreamAttributes* self, const char* qualifiedName, const char* value)
{
    if (!self || !qualifiedName) return;
    XString* qn = XString_create_utf8(qualifiedName);
    XString* val = value ? XString_create_utf8(value) : NULL;
    XXmlStreamAttributes_append_ex(self, qn, val);
    if (qn) XString_delete_base(qn);
    if (val) XString_delete_base(val);
}

bool XXmlStreamAttributes_appendAttribute(XXmlStreamAttributes* self,
    const XXmlStreamAttribute* attribute)
{
    return XXmlStreamAttributes_insert(self, self ? self->m_count : -1, attribute);
}

bool XXmlStreamAttributes_insert(XXmlStreamAttributes* self, int index,
    const XXmlStreamAttribute* attribute)
{
    if (!self || !attribute || index < 0 || index > self->m_count) return false;
    XXmlStreamAttribute* copy = xxml_stream_attribute_copy_private(attribute);
    if (!copy || !xxml_stream_attributes_reserve(self, self->m_count + 1)) {
        XXmlStreamAttribute_delete(copy);
        return false;
    }
    if (index < self->m_count) {
        memmove(&self->m_items[index + 1], &self->m_items[index],
                (size_t)(self->m_count - index) * sizeof(*self->m_items));
    }
    self->m_items[index] = copy;
    ++self->m_count;
    return true;
}

bool XXmlStreamAttributes_removeAt(XXmlStreamAttributes* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return false;
    XXmlStreamAttribute_delete(self->m_items[index]);
    if (index + 1 < self->m_count) {
        memmove(&self->m_items[index], &self->m_items[index + 1],
                (size_t)(self->m_count - index - 1) * sizeof(*self->m_items));
    }
    --self->m_count;
    self->m_items[self->m_count] = NULL;
    return true;
}

void XXmlStreamAttributes_clear(XXmlStreamAttributes* self)
{
    if (!self) return;
    while (self->m_count > 0) XXmlStreamAttributes_removeAt(self, self->m_count - 1);
}

XXmlStreamAttributes* XXmlStreamAttributes_create_copy(const XXmlStreamAttributes* self)
{
    if (!self) return NULL;
    XXmlStreamAttributes* copy = XXmlStreamAttributes_create();
    if (!copy) return NULL;
    for (int i = 0; i < self->m_count; ++i) {
        if (!XXmlStreamAttributes_appendAttribute(copy, self->m_items[i])) {
            XXmlStreamAttributes_delete(copy);
            return NULL;
        }
    }
    return copy;
}

/* ============================================================================
 * XXmlStreamNamespaceDeclaration 实现
 * ============================================================================ */

XXmlStreamNamespaceDeclaration* XXmlStreamNamespaceDeclaration_create(const XString* prefix, const XString* namespaceUri)
{
    XXmlStreamNamespaceDeclaration* self = (XXmlStreamNamespaceDeclaration*)XClass_Malloc(XXmlStreamNamespaceDeclaration);
    if (!self) return NULL;
    memset(self, 0, sizeof(XXmlStreamNamespaceDeclaration));
    self->m_prefix = prefix ? XString_create_copy(prefix) : NULL;
    self->m_namespaceUri = namespaceUri ? XString_create_copy(namespaceUri) : NULL;
    return self;
}

void XXmlStreamNamespaceDeclaration_delete(XXmlStreamNamespaceDeclaration* self)
{
    if (!self) return;
    if (self->m_prefix) XString_delete_base(self->m_prefix);
    if (self->m_namespaceUri) XString_delete_base(self->m_namespaceUri);
    XFree_System(self);
}

const XString* XXmlStreamNamespaceDeclaration_prefix(const XXmlStreamNamespaceDeclaration* self)
{
    if (!self) return NULL;
    return self->m_prefix;
}

const XString* XXmlStreamNamespaceDeclaration_namespaceUri(const XXmlStreamNamespaceDeclaration* self)
{
    if (!self) return NULL;
    return self->m_namespaceUri;
}

bool XXmlStreamNamespaceDeclaration_equals(const XXmlStreamNamespaceDeclaration* left,
    const XXmlStreamNamespaceDeclaration* right)
{
    if (!left || !right) return left == right;
    return xxml_stream_string_equals(left->m_prefix, right->m_prefix) &&
        xxml_stream_string_equals(left->m_namespaceUri, right->m_namespaceUri);
}

/* ============================================================================
 * XXmlStreamReader getter 实现
 * ============================================================================ */

const XString* XXmlStreamReader_namespaceUri(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_namespaceUri;
}

const XString* XXmlStreamReader_name(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_name;
}

const XString* XXmlStreamReader_qualifiedName(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_qualifiedName;
}

const XString* XXmlStreamReader_prefix(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_prefix;
}

const XString* XXmlStreamReader_text(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_text;
}

const XXmlStreamAttributes* XXmlStreamReader_attributes(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_StartElement ? d->m_attributes : NULL;
}

const XXmlStreamNamespaceDeclarations* XXmlStreamReader_namespaceDeclarations(
    const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (d->m_type == XXmlStream_StartElement && d->m_namespaceDeclarationCount > 0) {
        d->m_namespaceDeclarationsView.m_items =
            (XXmlStreamNamespaceDeclaration*)d->m_namespaceDeclarations;
        d->m_namespaceDeclarationsView.m_count = d->m_namespaceDeclarationCount;
        d->m_namespaceDeclarationsView.m_capacity = 0;
        d->m_namespaceDeclarationsView.m_ownsItems = false;
    } else {
        d->m_namespaceDeclarationsView.m_items = NULL;
        d->m_namespaceDeclarationsView.m_count = 0;
        d->m_namespaceDeclarationsView.m_capacity = 0;
        d->m_namespaceDeclarationsView.m_ownsItems = false;
    }
    return &d->m_namespaceDeclarationsView;
}

int XXmlStreamNamespaceDeclarations_size(const XXmlStreamNamespaceDeclarations* self)
{
    return self ? self->m_count : 0;
}

int XXmlStreamNamespaceDeclarations_count(const XXmlStreamNamespaceDeclarations* self)
{
    return XXmlStreamNamespaceDeclarations_size(self);
}

bool XXmlStreamNamespaceDeclarations_isEmpty(const XXmlStreamNamespaceDeclarations* self)
{
    return XXmlStreamNamespaceDeclarations_size(self) == 0;
}

bool XXmlStreamNamespaceDeclarations_equals(const XXmlStreamNamespaceDeclarations* left,
    const XXmlStreamNamespaceDeclarations* right)
{
    if (!left || !right) return left == right;
    if (left->m_count != right->m_count) return false;
    for (int i = 0; i < left->m_count; ++i) {
        if (!XXmlStreamNamespaceDeclaration_equals(&left->m_items[i], &right->m_items[i]))
            return false;
    }
    return true;
}

const XXmlStreamNamespaceDeclaration* XXmlStreamNamespaceDeclarations_at(
    const XXmlStreamNamespaceDeclarations* self, int index)
{
    if (!self || index < 0 || index >= self->m_count || !self->m_items) return NULL;
    return &self->m_items[index];
}

static void xxml_stream_namespace_declaration_clear_private(
    XXmlStreamNamespaceDeclaration* declaration)
{
    if (!declaration) return;
    XString_delete_base(declaration->m_prefix);
    XString_delete_base(declaration->m_namespaceUri);
    declaration->m_prefix = NULL;
    declaration->m_namespaceUri = NULL;
}

static bool xxml_stream_namespace_declarations_reserve(
    XXmlStreamNamespaceDeclarations* self, int needed)
{
    if (!self || !self->m_ownsItems || needed < 0) return false;
    if (needed <= self->m_capacity) return true;
    int capacity = self->m_capacity ? self->m_capacity : 4;
    while (capacity < needed) {
        if (capacity > INT32_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    XXmlStreamNamespaceDeclaration* items =
        (XXmlStreamNamespaceDeclaration*)XRealloc_System(
            self->m_items, sizeof(XXmlStreamNamespaceDeclaration) * (size_t)capacity);
    if (!items) return false;
    memset(items + self->m_capacity, 0,
           sizeof(XXmlStreamNamespaceDeclaration) * (size_t)(capacity - self->m_capacity));
    self->m_items = items;
    self->m_capacity = capacity;
    return true;
}

XXmlStreamNamespaceDeclarations* XXmlStreamNamespaceDeclarations_create(void)
{
    XXmlStreamNamespaceDeclarations* self =
        (XXmlStreamNamespaceDeclarations*)XMalloc_System(
            sizeof(XXmlStreamNamespaceDeclarations));
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    self->m_ownsItems = true;
    return self;
}

XXmlStreamNamespaceDeclarations* XXmlStreamNamespaceDeclarations_create_copy(
    const XXmlStreamNamespaceDeclarations* source)
{
    if (!source) return NULL;
    XXmlStreamNamespaceDeclarations* copy = XXmlStreamNamespaceDeclarations_create();
    if (!copy) return NULL;
    for (int i = 0; i < source->m_count; ++i) {
        if (!XXmlStreamNamespaceDeclarations_append(copy, &source->m_items[i])) {
            XXmlStreamNamespaceDeclarations_delete(copy);
            return NULL;
        }
    }
    return copy;
}

void XXmlStreamNamespaceDeclarations_delete(XXmlStreamNamespaceDeclarations* self)
{
    if (!self || !self->m_ownsItems) return;
    for (int i = 0; i < self->m_count; ++i)
        xxml_stream_namespace_declaration_clear_private(&self->m_items[i]);
    XFree_System(self->m_items);
    self->m_items = NULL;
    self->m_count = 0;
    self->m_capacity = 0;
    self->m_ownsItems = false;
    XFree_System(self);
}

bool XXmlStreamNamespaceDeclarations_append(XXmlStreamNamespaceDeclarations* self,
    const XXmlStreamNamespaceDeclaration* declaration)
{
    return XXmlStreamNamespaceDeclarations_insert(self, self ? self->m_count : -1,
                                                  declaration);
}

bool XXmlStreamNamespaceDeclarations_insert(XXmlStreamNamespaceDeclarations* self,
    int index, const XXmlStreamNamespaceDeclaration* declaration)
{
    if (!self || !self->m_ownsItems || !declaration || index < 0 ||
        index > self->m_count) return false;
    XString* prefix = declaration->m_prefix ?
        XString_create_copy(declaration->m_prefix) : NULL;
    XString* namespaceUri = declaration->m_namespaceUri ?
        XString_create_copy(declaration->m_namespaceUri) : NULL;
    if ((declaration->m_prefix && !prefix) ||
        (declaration->m_namespaceUri && !namespaceUri)) {
        XString_delete_base(prefix);
        XString_delete_base(namespaceUri);
        return false;
    }
    if (!xxml_stream_namespace_declarations_reserve(self, self->m_count + 1)) {
        XString_delete_base(prefix);
        XString_delete_base(namespaceUri);
        return false;
    }
    if (index < self->m_count) {
        memmove(&self->m_items[index + 1], &self->m_items[index],
                (size_t)(self->m_count - index) * sizeof(*self->m_items));
    }
    self->m_items[index].m_prefix = prefix;
    self->m_items[index].m_namespaceUri = namespaceUri;
    ++self->m_count;
    return true;
}

bool XXmlStreamNamespaceDeclarations_removeAt(XXmlStreamNamespaceDeclarations* self,
    int index)
{
    if (!self || !self->m_ownsItems || index < 0 || index >= self->m_count) return false;
    xxml_stream_namespace_declaration_clear_private(&self->m_items[index]);
    if (index + 1 < self->m_count) {
        memmove(&self->m_items[index], &self->m_items[index + 1],
                (size_t)(self->m_count - index - 1) * sizeof(*self->m_items));
    }
    --self->m_count;
    memset(&self->m_items[self->m_count], 0, sizeof(*self->m_items));
    return true;
}

void XXmlStreamNamespaceDeclarations_clear(XXmlStreamNamespaceDeclarations* self)
{
    if (!self || !self->m_ownsItems) return;
    while (self->m_count > 0)
        XXmlStreamNamespaceDeclarations_removeAt(self, self->m_count - 1);
}

XXmlStreamAttributes* XXmlStreamReader_attributes_copy(const XXmlStreamReader* self)
{
    const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(self);
    return attributes ? XXmlStreamAttributes_create_copy(attributes) : NULL;
}

XXmlStreamNamespaceDeclarations* XXmlStreamReader_namespaceDeclarations_copy(
    const XXmlStreamReader* self)
{
    const XXmlStreamNamespaceDeclarations* declarations =
        XXmlStreamReader_namespaceDeclarations(self);
    return declarations ? XXmlStreamNamespaceDeclarations_create_copy(declarations) : NULL;
}

bool XXmlStreamReader_hasNamespaceDeclarations(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_StartElement && d->m_namespaceDeclarationCount > 0;
}

const XString* XXmlStreamReader_dtdName(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_DTD ? d->m_dtdName : NULL;
}

const XString* XXmlStreamReader_dtdPublicId(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_DTD ? d->m_dtdPublicId : NULL;
}

const XString* XXmlStreamReader_dtdSystemId(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_DTD ? d->m_dtdSystemId : NULL;
}

const XString* XXmlStreamReader_documentVersion(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_StartDocument ? d->m_documentVersion : NULL;
}

const XString* XXmlStreamReader_documentEncoding(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_StartDocument ? d->m_documentEncoding : NULL;
}

bool XXmlStreamReader_hasXmlDeclaration(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_seenXmlDeclaration;
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
    return d->m_type == XXmlStream_Invalid ? (int)d->m_error : XXmlStream_NoError;
}

const XString* XXmlStreamReader_errorString(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_Invalid ? d->m_errorString : NULL;
}

bool XXmlStreamReader_hasError(const XXmlStreamReader* self)
{
    if (ISNULL(self, "XXmlStreamReader")) return false;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    return d->m_type == XXmlStream_Invalid && d->m_error != XXmlStream_NoError;
}

void XXmlStreamReader_raiseError(XXmlStreamReader* self, const XString* message)
{
    if (ISNULL(self, "XXmlStreamReader")) return;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    set_error(d, XXmlStream_CustomError, message ? XString_toUtf8(message) : "");
}

void XXmlStreamReader_raiseError_utf8(XXmlStreamReader* self, const char* message)
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
        if (XXmlStreamReader_isEndElement(self)) return false;
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

const XString* XXmlStreamReader_readElementText(XXmlStreamReader* self, int behaviour)
{
    if (ISNULL(self, "XXmlStreamReader")) return NULL;
    XXmlStreamReaderPrivate* d = (XXmlStreamReaderPrivate*)(self + 1);
    if (!XXmlStreamReader_isStartElement(self) || !d->m_buffer) {
        if (d->m_buffer) XString_clear_base(d->m_buffer);
        return d->m_buffer;
    }

    XString_clear_base(d->m_buffer);
    int depth = 1;
    while (depth > 0) {
        int token = XXmlStreamReader_readNext(self);
        if (token == XXmlStream_Characters || token == XXmlStream_EntityReference) {
            if (d->m_text) XString_append(d->m_buffer, d->m_text);
        } else if (token == XXmlStream_EndElement) {
            depth--;
        } else if (token == XXmlStream_StartElement) {
            if (behaviour == XXmlStream_ReadElementTextBehaviour_SkipChildElements) {
                XXmlStreamReader_skipCurrentElement(self);
            } else if (behaviour == XXmlStream_ReadElementTextBehaviour_IncludeChildElements) {
                depth++;
            } else {
                set_error(d, XXmlStream_UnexpectedElementError,
                          "Expected character data while reading element text");
                return d->m_buffer;
            }
        } else if (token == XXmlStream_Comment || token == XXmlStream_ProcessingInstruction) {
            continue;
        } else if (token == XXmlStream_Invalid || XXmlStreamReader_hasError(self)) {
            return d->m_buffer;
        } else if (behaviour == XXmlStream_ReadElementTextBehaviour_ErrorOnUnexpectedElement) {
            set_error(d, XXmlStream_UnexpectedElementError,
                      "Expected character data while reading element text");
            return d->m_buffer;
        }
    }
    return d->m_buffer;
}

/* ============================================================================
 * DTD 符号声明实现（对标 QXmlStreamNotationDeclaration）
 * ============================================================================ */

XXmlStreamNotationDeclaration* XXmlStreamNotationDeclaration_create(void)
{
    XXmlStreamNotationDeclaration* self = (XXmlStreamNotationDeclaration*)XClass_Malloc(XXmlStreamNotationDeclaration);
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

const XString* XXmlStreamNotationDeclaration_name(const XXmlStreamNotationDeclaration* self)
{
    if (!self) return NULL;
    return self->m_name;
}

const XString* XXmlStreamNotationDeclaration_systemId(const XXmlStreamNotationDeclaration* self)
{
    if (!self) return NULL;
    return self->m_systemId;
}

const XString* XXmlStreamNotationDeclaration_publicId(const XXmlStreamNotationDeclaration* self)
{
    if (!self) return NULL;
    return self->m_publicId;
}

bool XXmlStreamNotationDeclaration_equals(const XXmlStreamNotationDeclaration* left,
    const XXmlStreamNotationDeclaration* right)
{
    if (!left || !right) return left == right;
    return xxml_stream_string_equals(left->m_name, right->m_name) &&
        xxml_stream_string_equals(left->m_systemId, right->m_systemId) &&
        xxml_stream_string_equals(left->m_publicId, right->m_publicId);
}

/* ============================================================================
 * DTD 实体声明实现（对标 QXmlStreamEntityDeclaration）
 * ============================================================================ */

XXmlStreamEntityDeclaration* XXmlStreamEntityDeclaration_create(void)
{
    XXmlStreamEntityDeclaration* self = (XXmlStreamEntityDeclaration*)XClass_Malloc(XXmlStreamEntityDeclaration);
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

const XString* XXmlStreamEntityDeclaration_name(const XXmlStreamEntityDeclaration* self)
{
    if (!self) return NULL;
    return self->m_name;
}

const XString* XXmlStreamEntityDeclaration_notationName(const XXmlStreamEntityDeclaration* self)
{
    if (!self) return NULL;
    return self->m_notationName;
}

const XString* XXmlStreamEntityDeclaration_systemId(const XXmlStreamEntityDeclaration* self)
{
    if (!self) return NULL;
    return self->m_systemId;
}

const XString* XXmlStreamEntityDeclaration_publicId(const XXmlStreamEntityDeclaration* self)
{
    if (!self) return NULL;
    return self->m_publicId;
}

const XString* XXmlStreamEntityDeclaration_value(const XXmlStreamEntityDeclaration* self)
{
    if (!self) return NULL;
    return self->m_value;
}

bool XXmlStreamEntityDeclaration_equals(const XXmlStreamEntityDeclaration* left,
    const XXmlStreamEntityDeclaration* right)
{
    if (!left || !right) return left == right;
    return xxml_stream_string_equals(left->m_name, right->m_name) &&
        xxml_stream_string_equals(left->m_notationName, right->m_notationName) &&
        xxml_stream_string_equals(left->m_systemId, right->m_systemId) &&
        xxml_stream_string_equals(left->m_publicId, right->m_publicId) &&
        xxml_stream_string_equals(left->m_value, right->m_value);
}

/* ============================================================================
 * DTD 声明列表实现
 * ============================================================================ */

XXmlStreamNotationDeclarations* XXmlStreamNotationDeclarations_create(void)
{
    XXmlStreamNotationDeclarations* self = (XXmlStreamNotationDeclarations*)XClass_Malloc(XXmlStreamNotationDeclarations);
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

size_t XXmlStreamNotationDeclarations_count(const XXmlStreamNotationDeclarations* self)
{
    return XXmlStreamNotationDeclarations_size(self);
}

bool XXmlStreamNotationDeclarations_isEmpty(const XXmlStreamNotationDeclarations* self)
{
    return XXmlStreamNotationDeclarations_size(self) == 0;
}

bool XXmlStreamNotationDeclarations_equals(const XXmlStreamNotationDeclarations* left,
    const XXmlStreamNotationDeclarations* right)
{
    if (!left || !right) return left == right;
    if (left->m_count != right->m_count) return false;
    for (size_t i = 0; i < left->m_count; ++i) {
        if (!XXmlStreamNotationDeclaration_equals(&left->m_declarations[i],
                &right->m_declarations[i])) return false;
    }
    return true;
}

const XXmlStreamNotationDeclaration* XXmlStreamNotationDeclarations_at(const XXmlStreamNotationDeclarations* self, size_t index)
{
    if (!self || index >= self->m_count) return NULL;
    return &self->m_declarations[index];
}

static void xxml_stream_notation_clear_private(XXmlStreamNotationDeclaration* declaration)
{
    if (!declaration) return;
    XString_delete_base(declaration->m_name);
    XString_delete_base(declaration->m_systemId);
    XString_delete_base(declaration->m_publicId);
    memset(declaration, 0, sizeof(*declaration));
}

static bool xxml_stream_notation_reserve(XXmlStreamNotationDeclarations* self,
    size_t needed)
{
    if (!self || needed < self->m_count) return false;
    if (needed <= self->m_capacity) return true;
    size_t capacity = self->m_capacity ? self->m_capacity : 4;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    XXmlStreamNotationDeclaration* declarations =
        (XXmlStreamNotationDeclaration*)XRealloc_System(
            self->m_declarations, sizeof(*declarations) * capacity);
    if (!declarations) return false;
    memset(declarations + self->m_capacity, 0,
           sizeof(*declarations) * (capacity - self->m_capacity));
    self->m_declarations = declarations;
    self->m_capacity = capacity;
    return true;
}

bool XXmlStreamNotationDeclarations_append(XXmlStreamNotationDeclarations* self,
    const XXmlStreamNotationDeclaration* declaration)
{
    return XXmlStreamNotationDeclarations_insert(self, self ? self->m_count : 0,
                                                  declaration);
}

bool XXmlStreamNotationDeclarations_insert(XXmlStreamNotationDeclarations* self,
    size_t index, const XXmlStreamNotationDeclaration* declaration)
{
    if (!self || !declaration || index > self->m_count) return false;
    XString* name = declaration->m_name ? XString_create_copy(declaration->m_name) : NULL;
    XString* systemId = declaration->m_systemId ? XString_create_copy(declaration->m_systemId) : NULL;
    XString* publicId = declaration->m_publicId ? XString_create_copy(declaration->m_publicId) : NULL;
    if ((declaration->m_name && !name) || (declaration->m_systemId && !systemId) ||
        (declaration->m_publicId && !publicId)) {
        XString_delete_base(name); XString_delete_base(systemId); XString_delete_base(publicId);
        return false;
    }
    if (!xxml_stream_notation_reserve(self, self->m_count + 1)) {
        XString_delete_base(name); XString_delete_base(systemId); XString_delete_base(publicId);
        return false;
    }
    if (index < self->m_count) {
        memmove(&self->m_declarations[index + 1], &self->m_declarations[index],
                (self->m_count - index) * sizeof(*self->m_declarations));
    }
    self->m_declarations[index].m_name = name;
    self->m_declarations[index].m_systemId = systemId;
    self->m_declarations[index].m_publicId = publicId;
    ++self->m_count;
    return true;
}

bool XXmlStreamNotationDeclarations_removeAt(XXmlStreamNotationDeclarations* self,
    size_t index)
{
    if (!self || index >= self->m_count) return false;
    xxml_stream_notation_clear_private(&self->m_declarations[index]);
    if (index + 1 < self->m_count) {
        memmove(&self->m_declarations[index], &self->m_declarations[index + 1],
                (self->m_count - index - 1) * sizeof(*self->m_declarations));
    }
    --self->m_count;
    memset(&self->m_declarations[self->m_count], 0, sizeof(*self->m_declarations));
    return true;
}

void XXmlStreamNotationDeclarations_clear(XXmlStreamNotationDeclarations* self)
{
    if (!self) return;
    while (self->m_count > 0)
        XXmlStreamNotationDeclarations_removeAt(self, self->m_count - 1);
}

XXmlStreamEntityDeclarations* XXmlStreamEntityDeclarations_create(void)
{
    XXmlStreamEntityDeclarations* self = (XXmlStreamEntityDeclarations*)XClass_Malloc(XXmlStreamEntityDeclarations);
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

size_t XXmlStreamEntityDeclarations_count(const XXmlStreamEntityDeclarations* self)
{
    return XXmlStreamEntityDeclarations_size(self);
}

bool XXmlStreamEntityDeclarations_isEmpty(const XXmlStreamEntityDeclarations* self)
{
    return XXmlStreamEntityDeclarations_size(self) == 0;
}

bool XXmlStreamEntityDeclarations_equals(const XXmlStreamEntityDeclarations* left,
    const XXmlStreamEntityDeclarations* right)
{
    if (!left || !right) return left == right;
    if (left->m_count != right->m_count) return false;
    for (size_t i = 0; i < left->m_count; ++i) {
        if (!XXmlStreamEntityDeclaration_equals(&left->m_declarations[i],
                &right->m_declarations[i])) return false;
    }
    return true;
}

const XXmlStreamEntityDeclaration* XXmlStreamEntityDeclarations_at(const XXmlStreamEntityDeclarations* self, size_t index)
{
    if (!self || index >= self->m_count) return NULL;
    return &self->m_declarations[index];
}

static void xxml_stream_entity_clear_private(XXmlStreamEntityDeclaration* declaration)
{
    if (!declaration) return;
    XString_delete_base(declaration->m_name);
    XString_delete_base(declaration->m_notationName);
    XString_delete_base(declaration->m_systemId);
    XString_delete_base(declaration->m_publicId);
    XString_delete_base(declaration->m_value);
    memset(declaration, 0, sizeof(*declaration));
}

static bool xxml_stream_entity_reserve(XXmlStreamEntityDeclarations* self,
    size_t needed)
{
    if (!self || needed < self->m_count) return false;
    if (needed <= self->m_capacity) return true;
    size_t capacity = self->m_capacity ? self->m_capacity : 4;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    XXmlStreamEntityDeclaration* declarations =
        (XXmlStreamEntityDeclaration*)XRealloc_System(
            self->m_declarations, sizeof(*declarations) * capacity);
    if (!declarations) return false;
    memset(declarations + self->m_capacity, 0,
           sizeof(*declarations) * (capacity - self->m_capacity));
    self->m_declarations = declarations;
    self->m_capacity = capacity;
    return true;
}

bool XXmlStreamEntityDeclarations_append(XXmlStreamEntityDeclarations* self,
    const XXmlStreamEntityDeclaration* declaration)
{
    return XXmlStreamEntityDeclarations_insert(self, self ? self->m_count : 0,
                                                declaration);
}

bool XXmlStreamEntityDeclarations_insert(XXmlStreamEntityDeclarations* self,
    size_t index, const XXmlStreamEntityDeclaration* declaration)
{
    if (!self || !declaration || index > self->m_count) return false;
    XString* name = declaration->m_name ? XString_create_copy(declaration->m_name) : NULL;
    XString* notationName = declaration->m_notationName ? XString_create_copy(declaration->m_notationName) : NULL;
    XString* systemId = declaration->m_systemId ? XString_create_copy(declaration->m_systemId) : NULL;
    XString* publicId = declaration->m_publicId ? XString_create_copy(declaration->m_publicId) : NULL;
    XString* value = declaration->m_value ? XString_create_copy(declaration->m_value) : NULL;
    if ((declaration->m_name && !name) || (declaration->m_notationName && !notationName) ||
        (declaration->m_systemId && !systemId) || (declaration->m_publicId && !publicId) ||
        (declaration->m_value && !value)) {
        XString_delete_base(name); XString_delete_base(notationName);
        XString_delete_base(systemId); XString_delete_base(publicId); XString_delete_base(value);
        return false;
    }
    if (!xxml_stream_entity_reserve(self, self->m_count + 1)) {
        XString_delete_base(name); XString_delete_base(notationName);
        XString_delete_base(systemId); XString_delete_base(publicId); XString_delete_base(value);
        return false;
    }
    if (index < self->m_count) {
        memmove(&self->m_declarations[index + 1], &self->m_declarations[index],
                (self->m_count - index) * sizeof(*self->m_declarations));
    }
    self->m_declarations[index].m_name = name;
    self->m_declarations[index].m_notationName = notationName;
    self->m_declarations[index].m_systemId = systemId;
    self->m_declarations[index].m_publicId = publicId;
    self->m_declarations[index].m_value = value;
    ++self->m_count;
    return true;
}

bool XXmlStreamEntityDeclarations_removeAt(XXmlStreamEntityDeclarations* self,
    size_t index)
{
    if (!self || index >= self->m_count) return false;
    xxml_stream_entity_clear_private(&self->m_declarations[index]);
    if (index + 1 < self->m_count) {
        memmove(&self->m_declarations[index], &self->m_declarations[index + 1],
                (self->m_count - index - 1) * sizeof(*self->m_declarations));
    }
    --self->m_count;
    memset(&self->m_declarations[self->m_count], 0, sizeof(*self->m_declarations));
    return true;
}

void XXmlStreamEntityDeclarations_clear(XXmlStreamEntityDeclarations* self)
{
    if (!self) return;
    while (self->m_count > 0)
        XXmlStreamEntityDeclarations_removeAt(self, self->m_count - 1);
}

XXmlStreamNotationDeclarations* XXmlStreamReader_notationDeclarations_copy(
    const XXmlStreamReader* self)
{
    const XXmlStreamNotationDeclarations* source =
        XXmlStreamReader_notationDeclarations(self);
    if (!source) return NULL;
    XXmlStreamNotationDeclarations* copy = XXmlStreamNotationDeclarations_create();
    if (!copy) return NULL;
    for (size_t i = 0; i < source->m_count; ++i) {
        if (!XXmlStreamNotationDeclarations_append(copy, &source->m_declarations[i])) {
            XXmlStreamNotationDeclarations_delete(copy);
            return NULL;
        }
    }
    return copy;
}

XXmlStreamEntityDeclarations* XXmlStreamReader_entityDeclarations_copy(
    const XXmlStreamReader* self)
{
    const XXmlStreamEntityDeclarations* source =
        XXmlStreamReader_entityDeclarations(self);
    if (!source) return NULL;
    XXmlStreamEntityDeclarations* copy = XXmlStreamEntityDeclarations_create();
    if (!copy) return NULL;
    for (size_t i = 0; i < source->m_count; ++i) {
        if (!XXmlStreamEntityDeclarations_append(copy, &source->m_declarations[i])) {
            XXmlStreamEntityDeclarations_delete(copy);
            return NULL;
        }
    }
    return copy;
}

/* ============================================================================
 * 实体解析器实现（对标 QXmlStreamEntityResolver）
 * ============================================================================ */

XXmlStreamEntityResolver* XXmlStreamEntityResolver_create(void)
{
    XXmlStreamEntityResolver* self = (XXmlStreamEntityResolver*)XClass_Malloc(XXmlStreamEntityResolver);
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

const XString* XXmlStreamEntityResolver_resolveEntity(XXmlStreamEntityResolver* self,
    const XString* publicId, const XString* systemId)
{
    if (!self || !self->m_resolveEntityCallback) return NULL;
    return self->m_resolveEntityCallback(publicId, systemId, self->m_userData);
}

const XString* XXmlStreamEntityResolver_resolveUndeclaredEntity(XXmlStreamEntityResolver* self,
    const XString* name)
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
 * @note       对标 QXmlStreamReader::entityDeclarations
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
