/******************************************************************************
 * @file       XXmlStreamReader.h
 * @brief      XXmlStreamReader XML 流式读取器（对标 Qt 6.8 QXmlStreamReader）
 * @author     XinYueC 团队
 * @note       提供 SAX 风格的前向只读 XML 解析功能
 ******************************************************************************/
#ifndef XXMLSTREAMREADER_H
#define XXMLSTREAMREADER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
#include "XFileDevice.h"  /* 对标 QIODevice */

/* ========== 前向声明 ========== */
typedef struct XXmlStreamReader XXmlStreamReader;
typedef struct XXmlStreamAttribute XXmlStreamAttribute;
typedef struct XXmlStreamAttributes XXmlStreamAttributes;
typedef struct XXmlStreamNamespaceDeclaration XXmlStreamNamespaceDeclaration;

/* ========== Token 类型枚举（对标 Qt 6.8 QXmlStreamReader::TokenType） ========== */

/**
 * @brief      XML Token 类型枚举
 */
typedef enum XXmlStreamTokenType
{
    XXmlStream_NoToken = 0,                 /**< 无 Token */
    XXmlStream_StartDocument = 1,           /**< 文档开始 */
    XXmlStream_EndDocument = 2,             /**< 文档结束 */
    XXmlStream_StartElement = 3,            /**< 元素开始标签 */
    XXmlStream_EndElement = 4,              /**< 元素结束标签 */
    XXmlStream_Characters = 5,              /**< 字符数据 */
    XXmlStream_Comment = 6,                 /**< 注释 */
    XXmlStream_DTD = 7,                     /**< DTD 声明 */
    XXmlStream_EntityReference = 8,         /**< 实体引用 */
    XXmlStream_ProcessingInstruction = 9,   /**< 处理指令 */
    XXmlStream_Invalid = 10                 /**< 无效 Token */
} XXmlStreamTokenType;

/* ========== 错误类型枚举（对标 Qt 6.8 QXmlStreamReader::Error） ========== */

/**
 * @brief      错误类型枚举
 */
typedef enum XXmlStreamError
{
    XXmlStream_NoError = 0,                 /**< 无错误 */
    XXmlStream_UnexpectedElementError = 1,  /**< 意外的元素 */
    XXmlStream_CustomError = 2,             /**< 自定义错误 */
    XXmlStream_NotWellFormedError = 3,      /**< 格式不正确 */
    XXmlStream_PrematureEndOfDocumentError = 4 /**< 文档提前结束 */
} XXmlStreamError;

/* ========== 读取选项枚举 ========== */

/**
 * @brief      读取选项枚举
 */
typedef enum XXmlStreamReadOption
{
    XXmlStream_ReadElementTextBehaviour_ErrorOnUnexpectedElement = 0, /**< 在意外元素时报错 */
    XXmlStream_ReadElementTextBehaviour_IncludeChildElements = 1,     /**< 包含子元素文本 */
    XXmlStream_ReadElementTextBehaviour_SkipChildElements = 2         /**< 跳过子元素 */
} XXmlStreamReadOption;

/* ========== XXmlStreamAttribute 结构体（对标 QXmlStreamAttribute） ========== */

/**
 * @brief      XML 属性结构体（对标 Qt 6.8 QXmlStreamAttribute）
 * @note       表示 XML 元素的单个属性
 */
typedef struct XXmlStreamAttribute
{
    XString* m_namespaceUri;   /**< 命名空间 URI */
    XString* m_name;           /**< 属性名称（本地名） */
    XString* m_qualifiedName;  /**< 限定名（前缀:本地名） */
    XString* m_value;          /**< 属性值 */
    bool     m_isDefault;      /**< 是否为默认值 */
} XXmlStreamAttribute;

/**
 * @brief      创建 XML 属性对象
 * @param qualifiedName 限定名
 * @param value        属性值
 * @return     指向新 XXmlStreamAttribute 的指针
 */
XXmlStreamAttribute* XXmlStreamAttribute_create(const char* qualifiedName, const char* value);

/**
 * @brief      创建 XML 属性对象（带命名空间）
 * @param namespaceUri 命名空间 URI
 * @param name        属性名
 * @param value       属性值
 * @return     指向新 XXmlStreamAttribute 的指针
 */
XXmlStreamAttribute* XXmlStreamAttribute_create_ex(const char* namespaceUri, const char* name, const char* value);

/**
 * @brief      释放 XML 属性对象
 * @param self 目标 XXmlStreamAttribute 指针
 */
void XXmlStreamAttribute_delete(XXmlStreamAttribute* self);

/**
 * @brief      获取命名空间 URI
 * @param self 目标 XXmlStreamAttribute 指针
 * @return     命名空间 URI 字符串
 */
const char* XXmlStreamAttribute_namespaceUri(const XXmlStreamAttribute* self);

/**
 * @brief      获取属性名称（本地名）
 * @param self 目标 XXmlStreamAttribute 指针
 * @return     属性名
 */
const char* XXmlStreamAttribute_name(const XXmlStreamAttribute* self);

/**
 * @brief      获取限定名（前缀:本地名）
 * @param self 目标 XXmlStreamAttribute 指针
 * @return     限定名
 */
const char* XXmlStreamAttribute_qualifiedName(const XXmlStreamAttribute* self);

/**
 * @brief      获取前缀
 * @param self 目标 XXmlStreamAttribute 指针
 * @return     前缀字符串
 */
const char* XXmlStreamAttribute_prefix(const XXmlStreamAttribute* self);

/**
 * @brief      获取属性值
 * @param self 目标 XXmlStreamAttribute 指针
 * @return     属性值字符串
 */
const char* XXmlStreamAttribute_value(const XXmlStreamAttribute* self);

/**
 * @brief      判断是否为默认值
 * @param self 目标 XXmlStreamAttribute 指针
 * @return     默认值返回 true
 */
bool XXmlStreamAttribute_isDefault(const XXmlStreamAttribute* self);

/* ========== XXmlStreamAttributes 结构体（对标 QXmlStreamAttributes） ========== */

/**
 * @brief      XML 属性列表结构体（对标 Qt 6.8 QXmlStreamAttributes）
 * @note       属性列表，通过属性名快速查找
 */
typedef struct XXmlStreamAttributes
{
    XXmlStreamAttribute** m_items;  /**< 属性数组 */
    int m_count;                    /**< 属性数量 */
    int m_capacity;                 /**< 数组容量 */
} XXmlStreamAttributes;

/**
 * @brief      创建属性列表
 * @return     指向新 XXmlStreamAttributes 的指针
 */
XXmlStreamAttributes* XXmlStreamAttributes_create(void);

/**
 * @brief      释放属性列表
 * @param self 目标 XXmlStreamAttributes 指针
 */
void XXmlStreamAttributes_delete(XXmlStreamAttributes* self);

/**
 * @brief      获取属性数量
 * @param self 目标 XXmlStreamAttributes 指针
 * @return     属性数量
 */
int XXmlStreamAttributes_size(const XXmlStreamAttributes* self);

/**
 * @brief      按索引获取属性
 * @param self  目标 XXmlStreamAttributes 指针
 * @param index 索引
 * @return     指向 XXmlStreamAttribute 的指针，越界返回 NULL
 */
const XXmlStreamAttribute* XXmlStreamAttributes_at(const XXmlStreamAttributes* self, int index);

/**
 * @brief      按限定名查找属性值
 * @param self          目标 XXmlStreamAttributes 指针
 * @param qualifiedName 限定名
 * @return     属性值字符串，未找到返回空字符串
 */
const char* XXmlStreamAttributes_value(const XXmlStreamAttributes* self, const char* qualifiedName);

/**
 * @brief      按命名空间和名称查找属性值
 * @param self         目标 XXmlStreamAttributes 指针
 * @param namespaceUri 命名空间 URI
 * @param name         属性名
 * @return     属性值字符串，未找到返回空字符串
 */
const char* XXmlStreamAttributes_value_ex(const XXmlStreamAttributes* self, const char* namespaceUri, const char* name);

/**
 * @brief      判断是否包含指定属性
 * @param self          目标 XXmlStreamAttributes 指针
 * @param qualifiedName 限定名
 * @return     包含返回 true
 */
bool XXmlStreamAttributes_hasAttribute(const XXmlStreamAttributes* self, const char* qualifiedName);

/**
 * @brief      追加属性
 * @param self          目标 XXmlStreamAttributes 指针
 * @param namespaceUri  命名空间 URI
 * @param name          属性名
 * @param value         属性值
 */
void XXmlStreamAttributes_append(XXmlStreamAttributes* self, const char* namespaceUri, const char* name, const char* value);

/**
 * @brief      追加属性（带限定名）
 * @param self          目标 XXmlStreamAttributes 指针
 * @param qualifiedName 限定名
 * @param value         属性值
 */
void XXmlStreamAttributes_append_ex(XXmlStreamAttributes* self, const char* qualifiedName, const char* value);

/* ========== XXmlStreamNamespaceDeclaration 结构体（对标 QXmlStreamNamespaceDeclaration） ========== */

/**
 * @brief      XML 命名空间声明结构体（对标 Qt 6.8 QXmlStreamNamespaceDeclaration）
 * @note       表示 xmlns:prefix="uri" 声明
 */
typedef struct XXmlStreamNamespaceDeclaration
{
    XString* m_prefix;        /**< 命名空间前缀 */
    XString* m_namespaceUri;  /**< 命名空间 URI */
} XXmlStreamNamespaceDeclaration;

/**
 * @brief      命名空间声明数组（对标 QXmlStreamNamespaceDeclarations）
 * @note       addExtraNamespaceDeclarations 接收的元素数组类型
 */
typedef struct XXmlStreamNamespaceDeclarations
{
    const XXmlStreamNamespaceDeclaration* m_items;
    int m_count;
} XXmlStreamNamespaceDeclarations;

/**
 * @brief      创建命名空间声明
 * @param prefix       前缀
 * @param namespaceUri 命名空间 URI
 * @return     指向新 XXmlStreamNamespaceDeclaration 的指针
 */
XXmlStreamNamespaceDeclaration* XXmlStreamNamespaceDeclaration_create(const char* prefix, const char* namespaceUri);

/**
 * @brief      释放命名空间声明
 * @param self 目标 XXmlStreamNamespaceDeclaration 指针
 */
void XXmlStreamNamespaceDeclaration_delete(XXmlStreamNamespaceDeclaration* self);

/**
 * @brief      获取前缀
 * @param self 目标 XXmlStreamNamespaceDeclaration 指针
 * @return     前缀字符串
 */
const char* XXmlStreamNamespaceDeclaration_prefix(const XXmlStreamNamespaceDeclaration* self);

/**
 * @brief      获取命名空间 URI
 * @param self 目标 XXmlStreamNamespaceDeclaration 指针
 * @return     命名空间 URI
 */
const char* XXmlStreamNamespaceDeclaration_namespaceUri(const XXmlStreamNamespaceDeclaration* self);

/* ========== XXmlStreamReader 结构体 ========== */

/**
 * @brief      XXmlStreamReader XML 流式读取器（对标 Qt 6.8 QXmlStreamReader）
 * @note       前向只读 XML 解析器，支持流式读取 XML Token
 *             内部使用简单的手写 SAX 解析器
 */
/* 前向声明 */
typedef struct XXmlStreamNotationDeclarations XXmlStreamNotationDeclarations;
typedef struct XXmlStreamEntityDeclarations XXmlStreamEntityDeclarations;
typedef struct XXmlStreamEntityResolver XXmlStreamEntityResolver;

typedef struct XXmlStreamReader
{
    XClass    m_class;           /**< 继承的基类成员 */
    XByteArray* m_data;          /**< 内部 XML 数据缓冲区 */
    int64_t   m_position;        /**< 当前解析位置 */
    int       m_tokenType;       /**< 当前 Token 类型 */
    int       m_error;           /**< 错误类型 */
    XString*  m_errorString;     /**< 错误描述 */
    XString*  m_name;            /**< 当前元素/属性名（本地名） */
    XString*  m_namespaceUri;    /**< 当前命名空间 URI */
    XString*  m_qualifiedName;   /**< 当前限定名 */
    XString*  m_text;            /**< 当前文本内容 */
    XString*  m_prefix;          /**< 当前前缀 */
    XString*  m_dtdName;         /**< DTD 名称 */
    XString*  m_dtdPublicId;     /**< DTD 公共标识符 */
    XString*  m_dtdSystemId;     /**< DTD 系统标识符 */
    XString*  m_entityRefName;   /**< 实体引用名称 */
    XString*  m_piTarget;        /**< 处理指令目标 */
    XString*  m_piData;          /**< 处理指令数据 */
    XString*  m_documentVersion; /**< 文档版本 */
    XString*  m_documentEncoding; /**< 文档编码 */
    bool      m_isStandalone;    /**< 是否为独立文档 */
    bool      m_hasStandalone;   /**< 是否已声明 standalone */
    XXmlStreamAttributes* m_attributes;  /**< 当前元素属性列表 */
    void*     m_namespaceDeclarations;   /**< 命名空间声明列表（内部数组） */
    int       m_nsDeclCount;     /**< 命名空间声明数量 */
    int       m_entityExpansionLimit; /**< 实体扩展限制 */
    bool      m_isEndOfDocument;  /**< 是否到达文档末尾 */
    XXmlStreamNotationDeclarations* m_notationDeclarations; /**< DTD 符号声明列表 */
    XXmlStreamEntityDeclarations* m_entityDeclarations; /**< DTD 实体声明列表 */
    XXmlStreamEntityResolver* m_entityResolver; /**< 实体解析器 */
} XXmlStreamReader;

/* ========== 虚函数表初始化 ========== */

/**
 * @brief      初始化 XXmlStreamReader 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XXmlStreamReader_class_init(void);

/* ========== 创建与初始化 ========== */

/**
 * @brief      在堆上创建 XXmlStreamReader 实例
 * @return     指向新创建的 XXmlStreamReader 对象的指针，失败返回 NULL
 */
XXmlStreamReader* XXmlStreamReader_create(void);

/**
 * @brief      初始化 XXmlStreamReader 实例
 * @param self 待初始化的 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_init(XXmlStreamReader* self);

/**
 * @brief      释放 XXmlStreamReader 资源
 * @param self 待释放的 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_deinit(XXmlStreamReader* self);

/**
 * @brief      在堆上删除 XXmlStreamReader 实例
 * @param self 待删除的 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_delete(XXmlStreamReader* self);

/* ========== 虚函数调度 ========== */

void XXmlStreamReader_deinit_base(XXmlStreamReader* self);
void XXmlStreamReader_delete_base(XXmlStreamReader* self);

/* ========== 数据设置 ========== */

/**
 * @brief      设置输入数据（从字节数组）
 * @param self 目标 XXmlStreamReader 对象指针
 * @param data XML 数据字节数组
 */
void XXmlStreamReader_addData(XXmlStreamReader* self, const XByteArray* data);

/**
 * @brief      设置输入数据（从 C 字符串）
 * @param self 目标 XXmlStreamReader 对象指针
 * @param data XML 数据字符串
 */
void XXmlStreamReader_addData_utf8(XXmlStreamReader* self, const char* data);

/**
 * @brief      清除读取器状态
 * @param self 目标 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_clear(XXmlStreamReader* self);

/**
 * @brief      判断是否到达文档末尾
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     到达末尾返回 true
 */
bool XXmlStreamReader_atEnd(const XXmlStreamReader* self);

/* ========== 读取方法 ========== */

/**
 * @brief      读取下一个 Token
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     Token 类型
 */
int XXmlStreamReader_readNext(XXmlStreamReader* self);

/**
 * @brief      读取下一个开始元素，跳过非开始元素 Token
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     找到开始元素返回 true，否则返回 false
 */
bool XXmlStreamReader_readNextStartElement(XXmlStreamReader* self);

/**
 * @brief      跳过当前元素的所有内容，停在 EndElement 之后
 * @param self 目标 XXmlStreamReader 对象指针
 */
void XXmlStreamReader_skipCurrentElement(XXmlStreamReader* self);

/**
 * @brief      读取当前元素的文本内容
 * @param self 目标 XXmlStreamReader 对象指针
 * @param behaviour 子元素处理行为
 * @return     文本内容字符串
 */
const char* XXmlStreamReader_readElementText(XXmlStreamReader* self, int behaviour);

/* ========== Token 查询 ========== */

/**
 * @brief      获取当前 Token 类型
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     Token 类型枚举值
 */
int XXmlStreamReader_tokenType(const XXmlStreamReader* self);

/**
 * @brief      获取当前 Token 的字符串描述
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     Token 类型名称字符串
 */
const char* XXmlStreamReader_tokenString(const XXmlStreamReader* self);

/**
 * @brief      判断是否为文档开始
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是文档开始返回 true
 */
bool XXmlStreamReader_isStartDocument(const XXmlStreamReader* self);

/**
 * @brief      判断是否为文档结束
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是文档结束返回 true
 */
bool XXmlStreamReader_isEndDocument(const XXmlStreamReader* self);

/**
 * @brief      判断是否为元素开始标签
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是开始标签返回 true
 */
bool XXmlStreamReader_isStartElement(const XXmlStreamReader* self);

/**
 * @brief      判断是否为元素结束标签
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是结束标签返回 true
 */
bool XXmlStreamReader_isEndElement(const XXmlStreamReader* self);

/**
 * @brief      判断是否为字符数据
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是字符数据返回 true
 */
bool XXmlStreamReader_isCharacters(const XXmlStreamReader* self);

/**
 * @brief      判断是否为空白字符
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是空白字符返回 true
 */
bool XXmlStreamReader_isWhitespace(const XXmlStreamReader* self);

/**
 * @brief      判断当前字符数据是否为 CDATA 段
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是 CDATA 返回 true
 */
bool XXmlStreamReader_isCDATA(const XXmlStreamReader* self);

/**
 * @brief      判断是否为注释
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是注释返回 true
 */
bool XXmlStreamReader_isComment(const XXmlStreamReader* self);

/**
 * @brief      判断是否为 DTD
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是 DTD 返回 true
 */
bool XXmlStreamReader_isDTD(const XXmlStreamReader* self);

/**
 * @brief      判断是否为实体引用
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是实体引用返回 true
 */
bool XXmlStreamReader_isEntityReference(const XXmlStreamReader* self);

/**
 * @brief      判断是否为处理指令
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     是处理指令返回 true
 */
bool XXmlStreamReader_isProcessingInstruction(const XXmlStreamReader* self);

/* ========== 元素信息 ========== */

/**
 * @brief      获取当前元素/属性的命名空间 URI
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     命名空间 URI 字符串
 */
const char* XXmlStreamReader_namespaceUri(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素/属性的本地名
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     本地名
 */
const char* XXmlStreamReader_name(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素/属性的限定名
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     限定名
 */
const char* XXmlStreamReader_qualifiedName(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素/属性的前缀
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     前缀字符串
 */
const char* XXmlStreamReader_prefix(const XXmlStreamReader* self);

/**
 * @brief      获取当前字符数据或注释文本
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     文本内容
 */
const char* XXmlStreamReader_text(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素的属性列表
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     指向 XXmlStreamAttributes 的指针
 */
const XXmlStreamAttributes* XXmlStreamReader_attributes(const XXmlStreamReader* self);

/**
 * @brief      获取命名空间声明数量
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     命名空间声明数量
 */
int XXmlStreamReader_namespaceDeclarationsCount(const XXmlStreamReader* self);

/**
 * @brief      按索引获取命名空间声明
 * @param self  目标 XXmlStreamReader 对象指针
 * @param index 索引
 * @return     指向 XXmlStreamNamespaceDeclaration 的指针
 */
const XXmlStreamNamespaceDeclaration* XXmlStreamReader_namespaceDeclaration(const XXmlStreamReader* self, int index);

/**
 * @brief      判断是否具有命名空间声明
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     有命名空间声明返回 true
 */
bool XXmlStreamReader_hasNamespaceDeclarations(const XXmlStreamReader* self);

/* ========== DTD 信息 ========== */

/**
 * @brief      获取 DTD 名称
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     DTD 名称
 */
const char* XXmlStreamReader_dtdName(const XXmlStreamReader* self);

/**
 * @brief      获取 DTD 公共标识符
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     DTD 公共标识符
 */
const char* XXmlStreamReader_dtdPublicId(const XXmlStreamReader* self);

/**
 * @brief      获取 DTD 系统标识符
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     DTD 系统标识符
 */
const char* XXmlStreamReader_dtdSystemId(const XXmlStreamReader* self);

/* ========== 文档信息 ========== */

/**
 * @brief      获取文档版本
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     文档版本字符串
 */
const char* XXmlStreamReader_documentVersion(const XXmlStreamReader* self);

/**
 * @brief      获取文档编码
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     文档编码字符串
 */
const char* XXmlStreamReader_documentEncoding(const XXmlStreamReader* self);

/**
 * @brief      判断是否为独立文档
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     独立文档返回 true
 */
bool XXmlStreamReader_isStandaloneDocument(const XXmlStreamReader* self);

/**
 * @brief      判断 XML 声明中是否包含 standalone 关键字
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     含 standalone 声明返回 true
 * @note       对标 QXmlStreamReader::hasStandaloneDeclaration；
 *             即便 standalone="yes"/"no"，只要出现 standalone 关键字即返回 true。
 */
bool XXmlStreamReader_hasStandaloneDeclaration(const XXmlStreamReader* self);

/**
 * @brief      获取当前 Token 的源行号（从 1 开始）
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     行号（>=1）
 * @note       对标 QXmlStreamReader::lineNumber
 */
int64_t XXmlStreamReader_lineNumber(const XXmlStreamReader* self);

/**
 * @brief      获取当前 Token 的源列号（从 1 开始）
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     列号（>=1）
 * @note       对标 QXmlStreamReader::columnNumber
 */
int64_t XXmlStreamReader_columnNumber(const XXmlStreamReader* self);

/**
 * @brief      获取当前 Token 在整个输入中的字符偏移
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     字符偏移（>=0）
 * @note       对标 QXmlStreamReader::characterOffset
 */
int64_t XXmlStreamReader_characterOffset(const XXmlStreamReader* self);

/**
 * @brief      获取处理指令的目标（target）
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     PI 目标字符串；非 PI Token 时返回空字符串
 * @note       对标 QXmlStreamReader::processingInstructionTarget
 */
const char* XXmlStreamReader_processingInstructionTarget(const XXmlStreamReader* self);

/**
 * @brief      获取处理指令的数据（data）
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     PI 数据字符串；无数据时返回空字符串
 * @note       对标 QXmlStreamReader::processingInstructionData
 */
const char* XXmlStreamReader_processingInstructionData(const XXmlStreamReader* self);

/**
 * @brief      设置是否启用命名空间处理
 * @param self   目标 XXmlStreamReader 对象指针
 * @param enable true 启用（默认），false 关闭
 * @note       对标 QXmlStreamReader::setNamespaceProcessing
 */
void XXmlStreamReader_setNamespaceProcessing(XXmlStreamReader* self, bool enable);

/**
 * @brief      获取命名空间处理开关
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     true 表示启用
 * @note       对标 QXmlStreamReader::namespaceProcessing
 */
bool XXmlStreamReader_namespaceProcessing(const XXmlStreamReader* self);

/**
 * @brief      添加一个额外的命名空间声明（影响后续元素的 prefix -> uri 解析）
 * @param self       目标 XXmlStreamReader 对象指针
 * @param extraDecl  命名空间声明（不可为空；可复用前缀以覆盖默认声明）
 * @note       对标 QXmlStreamReader::addExtraNamespaceDeclaration
 */
void XXmlStreamReader_addExtraNamespaceDeclaration(XXmlStreamReader* self,
    const XXmlStreamNamespaceDeclaration* extraDecl);

/**
 * @brief      批量添加额外的命名空间声明
 * @param self      目标 XXmlStreamReader 对象指针
 * @param extraDecls 声明数组（不可为空；每个元素均为 prefix/uri 对）
 * @param count     数组元素数量
 * @note       对标 QXmlStreamReader::addExtraNamespaceDeclarations
 */
void XXmlStreamReader_addExtraNamespaceDeclarations(XXmlStreamReader* self,
    const XXmlStreamNamespaceDeclaration* extraDecls, int count);

/* ========== 错误处理 ========== */

/**
 * @brief      获取错误类型
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     错误类型枚举值
 */
int XXmlStreamReader_error(const XXmlStreamReader* self);

/**
 * @brief      获取错误描述
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     错误描述字符串
 */
const char* XXmlStreamReader_errorString(const XXmlStreamReader* self);

/**
 * @brief      判断是否有错误
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     有错误返回 true
 */
bool XXmlStreamReader_hasError(const XXmlStreamReader* self);

/**
 * @brief      手动引发错误
 * @param self    目标 XXmlStreamReader 对象指针
 * @param message 错误描述
 */
void XXmlStreamReader_raiseError(XXmlStreamReader* self, const char* message);

/**
 * @brief      获取实体扩展限制
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     实体扩展限制
 */
int XXmlStreamReader_entityExpansionLimit(const XXmlStreamReader* self);

/**
 * @brief      设置实体扩展限制
 * @param self  目标 XXmlStreamReader 对象指针
 * @param limit 实体扩展限制
 */
void XXmlStreamReader_setEntityExpansionLimit(XXmlStreamReader* self, int limit);
/**
 * @brief      设置输入设备（替代 addData）
 * @param self   目标 XXmlStreamReader 对象指针
 * @param device XFileDevice 指针（可为 NULL）
 */
void XXmlStreamReader_setDevice(XXmlStreamReader* self, struct XIODevice* device);

/**
 * @brief      获取当前关联的输入设备
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     XIODevice 指针，未设置返回 NULL
 * @note       对标 QXmlStreamReader::device
 */
struct XIODevice* XXmlStreamReader_device(const XXmlStreamReader* self);


#ifdef __cplusplus
}
#endif

/* ========== DTD 符号声明（对标 QXmlStreamNotationDeclaration） ========== */

/**
 * @brief      DTD 符号声明结构体（对标 Qt 6.8 QXmlStreamNotationDeclaration）
 * @note       表示 DTD 中的 NOTATION 声明
 */
typedef struct XXmlStreamNotationDeclaration
{
    XString* m_name;       /**< 符号名称 */
    XString* m_systemId;   /**< 系统标识符 */
    XString* m_publicId;   /**< 公共标识符 */
} XXmlStreamNotationDeclaration;

/**
 * @brief      创建 DTD 符号声明
 * @return     指向新创建的 XXmlStreamNotationDeclaration 的指针
 */
XXmlStreamNotationDeclaration* XXmlStreamNotationDeclaration_create(void);

/**
 * @brief      初始化 DTD 符号声明
 * @param self 目标 XXmlStreamNotationDeclaration 指针
 */
void XXmlStreamNotationDeclaration_init(XXmlStreamNotationDeclaration* self);

/**
 * @brief      销毁 DTD 符号声明
 * @param self 目标 XXmlStreamNotationDeclaration 指针
 */
void XXmlStreamNotationDeclaration_delete(XXmlStreamNotationDeclaration* self);

/**
 * @brief      获取符号名称
 * @param self 目标 XXmlStreamNotationDeclaration 指针
 * @return     符号名称
 */
const char* XXmlStreamNotationDeclaration_name(const XXmlStreamNotationDeclaration* self);

/**
 * @brief      获取系统标识符
 * @param self 目标 XXmlStreamNotationDeclaration 指针
 * @return     系统标识符
 */
const char* XXmlStreamNotationDeclaration_systemId(const XXmlStreamNotationDeclaration* self);

/**
 * @brief      获取公共标识符
 * @param self 目标 XXmlStreamNotationDeclaration 指针
 * @return     公共标识符
 */
const char* XXmlStreamNotationDeclaration_publicId(const XXmlStreamNotationDeclaration* self);

/* ========== DTD 实体声明（对标 QXmlStreamEntityDeclaration） ========== */

/**
 * @brief      DTD 实体声明结构体（对标 Qt 6.8 QXmlStreamEntityDeclaration）
 * @note       表示 DTD 中的 ENTITY 声明
 */
typedef struct XXmlStreamEntityDeclaration
{
    XString* m_name;         /**< 实体名称 */
    XString* m_notationName;  /**< 符号名称（用于未解析实体）*/
    XString* m_systemId;      /**< 系统标识符 */
    XString* m_publicId;      /**< 公共标识符 */
    XString* m_value;         /**< 实体值（用于解析实体）*/
} XXmlStreamEntityDeclaration;

/**
 * @brief      创建 DTD 实体声明
 * @return     指向新创建的 XXmlStreamEntityDeclaration 的指针
 */
XXmlStreamEntityDeclaration* XXmlStreamEntityDeclaration_create(void);

/**
 * @brief      初始化 DTD 实体声明
 * @param self 目标 XXmlStreamEntityDeclaration 指针
 */
void XXmlStreamEntityDeclaration_init(XXmlStreamEntityDeclaration* self);

/**
 * @brief      销毁 DTD 实体声明
 * @param self 目标 XXmlStreamEntityDeclaration 指针
 */
void XXmlStreamEntityDeclaration_delete(XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取实体名称
 * @param self 目标 XXmlStreamEntityDeclaration 指针
 * @return     实体名称
 */
const char* XXmlStreamEntityDeclaration_name(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取符号名称
 * @param self 目标 XXmlStreamEntityDeclaration 指针
 * @return     符号名称
 */
const char* XXmlStreamEntityDeclaration_notationName(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取系统标识符
 * @param self 目标 XXmlStreamEntityDeclaration 指针
 * @return     系统标识符
 */
const char* XXmlStreamEntityDeclaration_systemId(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取公共标识符
 * @param self 目标 XXmlStreamEntityDeclaration 指针
 * @return     公共标识符
 */
const char* XXmlStreamEntityDeclaration_publicId(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取实体值
 * @param self 目标 XXmlStreamEntityDeclaration 指针
 * @return     实体值
 */
const char* XXmlStreamEntityDeclaration_value(const XXmlStreamEntityDeclaration* self);

/* ========== DTD 声明列表 ========== */

/**
 * @brief      DTD 符号声明列表
 */
typedef struct XXmlStreamNotationDeclarations
{
    XXmlStreamNotationDeclaration* m_declarations;
    size_t m_count;
    size_t m_capacity;
} XXmlStreamNotationDeclarations;

/**
 * @brief      DTD 实体声明列表
 */
typedef struct XXmlStreamEntityDeclarations
{
    XXmlStreamEntityDeclaration* m_declarations;
    size_t m_count;
    size_t m_capacity;
} XXmlStreamEntityDeclarations;

/* ========== DTD 声明列表管理 ========== */

/**
 * @brief      创建 DTD 符号声明列表
 * @return     指向新创建的列表的指针
 */
XXmlStreamNotationDeclarations* XXmlStreamNotationDeclarations_create(void);

/**
 * @brief      销毁 DTD 符号声明列表
 * @param self 目标列表指针
 */
void XXmlStreamNotationDeclarations_delete(XXmlStreamNotationDeclarations* self);

/**
 * @brief      获取声明数量
 * @param self 目标列表指针
 * @return     声明数量
 */
size_t XXmlStreamNotationDeclarations_size(const XXmlStreamNotationDeclarations* self);

/**
 * @brief      按索引获取声明
 * @param self  目标列表指针
 * @param index 索引
 * @return     指向声明的指针，越界返回 NULL
 */
const XXmlStreamNotationDeclaration* XXmlStreamNotationDeclarations_at(const XXmlStreamNotationDeclarations* self, size_t index);

/**
 * @brief      创建 DTD 实体声明列表
 * @return     指向新创建的列表的指针
 */
XXmlStreamEntityDeclarations* XXmlStreamEntityDeclarations_create(void);

/**
 * @brief      销毁 DTD 实体声明列表
 * @param self 目标列表指针
 */
void XXmlStreamEntityDeclarations_delete(XXmlStreamEntityDeclarations* self);

/**
 * @brief      获取声明数量
 * @param self 目标列表指针
 * @return     声明数量
 */
size_t XXmlStreamEntityDeclarations_size(const XXmlStreamEntityDeclarations* self);

/**
 * @brief      按索引获取声明
 * @param self  目标列表指针
 * @param index 索引
 * @return     指向声明的指针，越界返回 NULL
 */
const XXmlStreamEntityDeclaration* XXmlStreamEntityDeclarations_at(const XXmlStreamEntityDeclarations* self, size_t index);

/* ========== 实体解析器（对标 QXmlStreamEntityResolver） ========== */

/**
 * @brief      实体解析器回调函数类型
 */
typedef const char* (*XXmlStreamEntityResolver_ResolveEntityCallback)(
    const char* publicId, const char* systemId, void* userData);

/**
 * @brief      未声明实体解析回调函数类型
 */
typedef const char* (*XXmlStreamEntityResolver_ResolveUndeclaredEntityCallback)(
    const char* name, void* userData);

/**
 * @brief      实体解析器结构体（对标 Qt 6.8 QXmlStreamEntityResolver）
 * @note       用于解析自定义实体
 */
typedef struct XXmlStreamEntityResolver
{
    XClass m_class;
    void* m_userData;
    XXmlStreamEntityResolver_ResolveEntityCallback m_resolveEntityCallback;
    XXmlStreamEntityResolver_ResolveUndeclaredEntityCallback m_resolveUndeclaredEntityCallback;
} XXmlStreamEntityResolver;

/**
 * @brief      创建实体解析器
 * @return     指向新创建的 XXmlStreamEntityResolver 的指针
 */
XXmlStreamEntityResolver* XXmlStreamEntityResolver_create(void);

/**
 * @brief      初始化实体解析器
 * @param self 目标 XXmlStreamEntityResolver 指针
 */
void XXmlStreamEntityResolver_init(XXmlStreamEntityResolver* self);

/**
 * @brief      销毁实体解析器
 * @param self 目标 XXmlStreamEntityResolver 指针
 */
void XXmlStreamEntityResolver_delete(XXmlStreamEntityResolver* self);

/**
 * @brief      解析实体
 * @param self      目标 XXmlStreamEntityResolver 指针
 * @param publicId 公共标识符
 * @param systemId 系统标识符
 * @return          解析后的实体值，未找到返回 NULL
 */
const char* XXmlStreamEntityResolver_resolveEntity(XXmlStreamEntityResolver* self,
    const char* publicId, const char* systemId);

/**
 * @brief      解析未声明实体
 * @param self 目标 XXmlStreamEntityResolver 指针
 * @param name 实体名称
 * @return      解析后的实体值，未找到返回 NULL
 */
const char* XXmlStreamEntityResolver_resolveUndeclaredEntity(XXmlStreamEntityResolver* self,
    const char* name);

/**
 * @brief      设置用户数据
 * @param self     目标 XXmlStreamEntityResolver 指针
 * @param userData 用户数据
 */
void XXmlStreamEntityResolver_setUserData(XXmlStreamEntityResolver* self, void* userData);

/**
 * @brief      获取用户数据
 * @param self 目标 XXmlStreamEntityResolver 指针
 * @return     用户数据
 */
void* XXmlStreamEntityResolver_userData(XXmlStreamEntityResolver* self);

/* ========== XXmlStreamReader DTD 相关 API ========== */

/**
 * @brief      获取 DTD 符号声明列表
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     DTD 符号声明列表（调用者不负责释放）
 */
XXmlStreamNotationDeclarations* XXmlStreamReader_notationDeclarations(const XXmlStreamReader* self);

/**
 * @brief      获取 DTD 实体声明列表
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     DTD 实体声明列表（调用者不负责释放）
 */
XXmlStreamEntityDeclarations* XXmlStreamReader_entityDeclarations(const XXmlStreamReader* self);

/**
 * @brief      设置实体解析器
 * @param self     目标 XXmlStreamReader 对象指针
 * @param resolver 实体解析器（可为空）
 */
void XXmlStreamReader_setEntityResolver(XXmlStreamReader* self, XXmlStreamEntityResolver* resolver);

/**
 * @brief      获取实体解析器
 * @param self 目标 XXmlStreamReader 对象指针
 * @return     实体解析器（无则返回 NULL）
 */
XXmlStreamEntityResolver* XXmlStreamReader_entityResolver(const XXmlStreamReader* self);
#endif /* XXMLSTREAMREADER_H */
