/**
 * @file       XXmlStreamReader.h
 * @brief      XXmlStreamReader XML 流式读取器（对标 Qt 6.8 QXmlStreamReader）。
 * @details    提供 SAX 风格的前向只读 XML 解析功能；输入字符串按 UTF-8 或 UTF-16
 *             约定处理，设备访问只通过 XinYueC 的 XIODevice 抽象完成，不调用平台 API。
 */
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
    XXmlStream_Invalid = 1,                 /**< 无效 Token */
    XXmlStream_StartDocument = 2,           /**< 文档开始 */
    XXmlStream_EndDocument = 3,             /**< 文档结束 */
    XXmlStream_StartElement = 4,            /**< 元素开始标签 */
    XXmlStream_EndElement = 5,              /**< 元素结束标签 */
    XXmlStream_Characters = 6,              /**< 字符数据 */
    XXmlStream_Comment = 7,                 /**< 注释 */
    XXmlStream_DTD = 8,                     /**< DTD 声明 */
    XXmlStream_EntityReference = 9,         /**< 实体引用 */
    XXmlStream_ProcessingInstruction = 10  /**< 处理指令 */
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
 * @note      表示 XML 元素的单个属性
 */
typedef struct XXmlStreamAttribute
{
    XString* m_namespaceUri;   /**< 命名空间 URI */
    XString* m_name;           /**< 属性名称（本地名） */
    XString* m_qualifiedName;  /**< 限定名（前缀:本地名） */
    XString* m_prefix;         /**< 属性前缀 */
    XString* m_value;          /**< 属性值 */
    bool     m_isDefault;      /**< 是否为默认值 */
} XXmlStreamAttribute;

/**
 * @brief      创建 XML 属性对象
 * @param      qualifiedName 限定名
 * @param      value        属性值
 * @return      指向新 XXmlStreamAttribute 的指针
 */
XXmlStreamAttribute* XXmlStreamAttribute_create(const XString* qualifiedName, const XString* value);

/**
 * @brief      创建 XML 属性对象（带命名空间）
 * @param      namespaceUri 命名空间 URI
 * @param      name        属性名
 * @param      value       属性值
 * @return      指向新 XXmlStreamAttribute 的指针
 */
XXmlStreamAttribute* XXmlStreamAttribute_create_ex(const XString* namespaceUri, const XString* name, const XString* value);

/**
 * @brief      释放 XML 属性对象
 * @param      self 目标 XXmlStreamAttribute 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamAttribute_delete(XXmlStreamAttribute* self);

/**
 * @brief      获取命名空间 URI
 * @param      self 目标 XXmlStreamAttribute 指针
 * @return      命名空间 URI 字符串
 */
const XString* XXmlStreamAttribute_namespaceUri(const XXmlStreamAttribute* self);

/**
 * @brief      获取属性名称（本地名）
 * @param      self 目标 XXmlStreamAttribute 指针
 * @return      属性名
 */
const XString* XXmlStreamAttribute_name(const XXmlStreamAttribute* self);

/**
 * @brief      获取限定名（前缀:本地名）
 * @param      self 目标 XXmlStreamAttribute 指针
 * @return      限定名
 */
const XString* XXmlStreamAttribute_qualifiedName(const XXmlStreamAttribute* self);

/**
 * @brief      获取前缀
 * @param      self 目标 XXmlStreamAttribute 指针
 * @return      前缀字符串
 */
const XString* XXmlStreamAttribute_prefix(const XXmlStreamAttribute* self);

/**
 * @brief      获取属性值
 * @param      self 目标 XXmlStreamAttribute 指针
 * @return      属性值字符串
 */
const XString* XXmlStreamAttribute_value(const XXmlStreamAttribute* self);

/**
 * @brief      判断是否为默认值
 * @param      self 目标 XXmlStreamAttribute 指针
 * @return      默认值返回 true
 */
bool XXmlStreamAttribute_isDefault(const XXmlStreamAttribute* self);

/**
 * @brief      判断两个 XML 属性是否等价。
 * @param      left 左属性，可为 NULL。
 * @param      right 右属性，可为 NULL。
 * @return      字段符合 Qt 等价规则返回 true。
 */
bool XXmlStreamAttribute_equals(const XXmlStreamAttribute* left,
    const XXmlStreamAttribute* right);

/* ========== XXmlStreamAttributes 结构体（对标 QXmlStreamAttributes） ========== */

/**
 * @brief      XML 属性列表结构体（对标 Qt 6.8 QXmlStreamAttributes）
 * @note      属性列表，通过属性名快速查找
 */
typedef struct XXmlStreamAttributes
{
    XXmlStreamAttribute** m_items;  /**< 属性数组 */
    int m_count;                    /**< 属性数量 */
    int m_capacity;                 /**< 数组容量 */
} XXmlStreamAttributes;

/**
 * @brief      创建属性列表
 * @return      指向新 XXmlStreamAttributes 的指针
 */
XXmlStreamAttributes* XXmlStreamAttributes_create(void);

/**
 * @brief      释放属性列表
 * @param      self 目标 XXmlStreamAttributes 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamAttributes_delete(XXmlStreamAttributes* self);

/**
 * @brief      获取属性数量
 * @param      self 目标 XXmlStreamAttributes 指针
 * @return      属性数量
 */
int XXmlStreamAttributes_size(const XXmlStreamAttributes* self);

/**
 * @brief      获取属性数量（Qt count 别名）。
 * @param      self 目标属性列表，可为 NULL。
 * @return      属性数量。
 */
int XXmlStreamAttributes_count(const XXmlStreamAttributes* self);

/**
 * @brief      判断属性列表是否为空。
 * @param      self 目标属性列表，可为 NULL。
 * @return      列表为空或 NULL 返回 true。
 */
bool XXmlStreamAttributes_isEmpty(const XXmlStreamAttributes* self);

/**
 * @brief      判断两个属性列表是否逐项等价。
 * @param      left 左列表，可为 NULL。
 * @param      right 右列表，可为 NULL。
 * @return      数量和属性顺序均相同返回 true。
 */
bool XXmlStreamAttributes_equals(const XXmlStreamAttributes* left,
    const XXmlStreamAttributes* right);

/**
 * @brief      按索引获取属性
 * @param      self  目标 XXmlStreamAttributes 指针
 * @param      index 索引
 * @return      指向 XXmlStreamAttribute 的指针，越界返回 NULL
 */
const XXmlStreamAttribute* XXmlStreamAttributes_at(const XXmlStreamAttributes* self, int index);

/**
 * @brief      按限定名查找属性值
 * @param      self          目标 XXmlStreamAttributes 指针
 * @param      qualifiedName 限定名
 * @return      属性值字符串，未找到返回空字符串
 */
const XString* XXmlStreamAttributes_value(const XXmlStreamAttributes* self, const XString* qualifiedName);

/**
 * @brief      按命名空间和名称查找属性值
 * @param      self         目标 XXmlStreamAttributes 指针
 * @param      namespaceUri 命名空间 URI
 * @param      name         属性名
 * @return      属性值字符串，未找到返回空字符串
 */
const XString* XXmlStreamAttributes_value_ex(const XXmlStreamAttributes* self, const XString* namespaceUri, const XString* name);

/**
 * @brief      判断是否包含指定属性
 * @param      self          目标 XXmlStreamAttributes 指针
 * @param      qualifiedName 限定名
 * @return      包含返回 true
 */
bool XXmlStreamAttributes_hasAttribute(const XXmlStreamAttributes* self, const XString* qualifiedName);

/**
 * @brief      判断是否包含指定命名空间和本地名的属性
 * @param      self         目标 XXmlStreamAttributes 指针
 * @param      namespaceUri 命名空间 URI
 * @param      name         属性本地名
 * @return      包含返回 true
 */
bool XXmlStreamAttributes_hasAttribute_ex(const XXmlStreamAttributes* self,
    const XString* namespaceUri, const XString* name);

/**
 * @brief      追加属性
 * @param      self          目标 XXmlStreamAttributes 指针
 * @param      namespaceUri  命名空间 URI
 * @param      name          属性名
 * @param      value         属性值
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamAttributes_append(XXmlStreamAttributes* self, const XString* namespaceUri, const XString* name, const XString* value);

/**
 * @brief      追加属性（带限定名）
 * @param      self          目标 XXmlStreamAttributes 指针
 * @param      qualifiedName 限定名
 * @param      value         属性值
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamAttributes_append_ex(XXmlStreamAttributes* self, const XString* qualifiedName, const XString* value);

/**
 * @brief      追加属性（UTF-8 版本）
 * @param      self          目标 XXmlStreamAttributes 指针
 * @param      namespaceUri  命名空间 URI（UTF-8 编码，可为 NULL）
 * @param      name          属性名（UTF-8 编码）
 * @param      value         属性值（UTF-8 编码）
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamAttributes_append_utf8(XXmlStreamAttributes* self, const char* namespaceUri, const char* name, const char* value);

/**
 * @brief      追加属性（带限定名，UTF-8 版本）
 * @param      self          目标 XXmlStreamAttributes 指针
 * @param      qualifiedName 限定名（UTF-8 编码）
 * @param      value         属性值（UTF-8 编码）
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamAttributes_append_ex_utf8(XXmlStreamAttributes* self, const char* qualifiedName, const char* value);

/**
 * @brief      追加已有属性的深拷贝。
 * @param      self 目标属性列表，取得复制后属性的所有权。
 * @param      attribute 待复制属性，只借用。
 * @return     成功返回 true；输入为空或内存不足返回 false。
 */
bool XXmlStreamAttributes_appendAttribute(XXmlStreamAttributes* self,
    const XXmlStreamAttribute* attribute);

/**
 * @brief      在指定位置插入已有属性的深拷贝。
 * @param      self 目标属性列表。
 * @param      index 插入位置；小于 0 或大于 size 时失败。
 * @param      attribute 待复制属性，只借用。
 * @return     成功返回 true；参数非法或内存不足返回 false。
 */
bool XXmlStreamAttributes_insert(XXmlStreamAttributes* self, int index,
    const XXmlStreamAttribute* attribute);

/**
 * @brief      移除指定位置的属性并释放其列表所有权。
 * @param      self 目标属性列表。
 * @param      index 待移除索引。
 * @return     索引有效并完成移除返回 true，否则返回 false。
 */
bool XXmlStreamAttributes_removeAt(XXmlStreamAttributes* self, int index);

/**
 * @brief      清空属性列表中的全部属性。
 * @param      self 目标属性列表，可为 NULL。
 * @return     无；NULL 输入不执行操作。
 */
void XXmlStreamAttributes_clear(XXmlStreamAttributes* self);

/**
 * @brief      深拷贝属性列表。
 * @param      self 源属性列表，只借用。
 * @return     新列表及其属性均由调用方使用 XXmlStreamAttributes_delete 释放。
 */
XXmlStreamAttributes* XXmlStreamAttributes_create_copy(
    const XXmlStreamAttributes* self);

/* ========== XXmlStreamNamespaceDeclaration 结构体（对标 QXmlStreamNamespaceDeclaration） ========== */

/**
 * @brief      XML 命名空间声明结构体（对标 Qt 6.8 QXmlStreamNamespaceDeclaration）
 * @note      表示 xmlns:prefix="uri" 声明
 */
typedef struct XXmlStreamNamespaceDeclaration
{
    XString* m_prefix;        /**< 命名空间前缀 */
    XString* m_namespaceUri;  /**< 命名空间 URI */
} XXmlStreamNamespaceDeclaration;

/**
 * @brief      命名空间声明数组（对标 QXmlStreamNamespaceDeclarations）
 * @note      addExtraNamespaceDeclarations 接收的元素数组类型
 */
typedef struct XXmlStreamNamespaceDeclarations
{
    XXmlStreamNamespaceDeclaration* m_items; /**< 声明数组；视图时借用，独立列表时自有。 */
    int m_count;                             /**< 当前声明数量。 */
    int m_capacity;                         /**< 独立列表容量。 */
    bool m_ownsItems;                        /**< 是否拥有数组及其中字符串。 */
} XXmlStreamNamespaceDeclarations;

/**
 * @brief      创建命名空间声明
 * @param      prefix       前缀
 * @param      namespaceUri 命名空间 URI
 * @return      指向新 XXmlStreamNamespaceDeclaration 的指针
 */
XXmlStreamNamespaceDeclaration* XXmlStreamNamespaceDeclaration_create(const XString* prefix, const XString* namespaceUri);

/**
 * @brief      释放命名空间声明
 * @param      self 目标 XXmlStreamNamespaceDeclaration 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamNamespaceDeclaration_delete(XXmlStreamNamespaceDeclaration* self);

/**
 * @brief      获取前缀
 * @param      self 目标 XXmlStreamNamespaceDeclaration 指针
 * @return      前缀字符串
 */
const XString* XXmlStreamNamespaceDeclaration_prefix(const XXmlStreamNamespaceDeclaration* self);

/**
 * @brief      获取命名空间 URI
 * @param      self 目标 XXmlStreamNamespaceDeclaration 指针
 * @return      命名空间 URI
 */
const XString* XXmlStreamNamespaceDeclaration_namespaceUri(const XXmlStreamNamespaceDeclaration* self);

/**
 * @brief      判断两个 XML 命名空间声明是否等价。
 * @param      left 左声明，可为 NULL。
 * @param      right 右声明，可为 NULL。
 * @return      前缀和 URI 均相同返回 true。
 */
bool XXmlStreamNamespaceDeclaration_equals(const XXmlStreamNamespaceDeclaration* left,
    const XXmlStreamNamespaceDeclaration* right);

/**
 * @brief      创建空的命名空间声明列表。
 * @return     新列表；调用方使用 XXmlStreamNamespaceDeclarations_delete 释放。
 */
XXmlStreamNamespaceDeclarations* XXmlStreamNamespaceDeclarations_create(void);

/**
 * @brief      深拷贝命名空间声明列表。
 * @param      self 源列表，只借用。
 * @return     新列表；调用方负责释放。
 */
XXmlStreamNamespaceDeclarations* XXmlStreamNamespaceDeclarations_create_copy(
    const XXmlStreamNamespaceDeclarations* self);

/**
 * @brief      释放独立命名空间声明列表。
 * @param      self 目标列表；Reader 返回的借用视图不可调用此函数。
 * @return     无；NULL 输入不执行操作。
 */
void XXmlStreamNamespaceDeclarations_delete(XXmlStreamNamespaceDeclarations* self);

/**
 * @brief      向独立命名空间声明列表追加声明副本。
 * @param      self 目标独立列表。
 * @param      declaration 待复制声明，只借用。
 * @return     成功返回 true；视图、输入为空或内存不足返回 false。
 */
bool XXmlStreamNamespaceDeclarations_append(XXmlStreamNamespaceDeclarations* self,
    const XXmlStreamNamespaceDeclaration* declaration);

/**
 * @brief      在独立命名空间声明列表指定位置插入声明副本。
 * @param      self 目标独立列表。
 * @param      index 插入位置。
 * @param      declaration 待复制声明，只借用。
 * @return     成功返回 true，否则返回 false。
 */
bool XXmlStreamNamespaceDeclarations_insert(XXmlStreamNamespaceDeclarations* self,
    int index, const XXmlStreamNamespaceDeclaration* declaration);

/**
 * @brief      移除独立命名空间声明列表项。
 * @param      self 目标独立列表。
 * @param      index 待移除索引。
 * @return     成功返回 true，否则返回 false。
 */
bool XXmlStreamNamespaceDeclarations_removeAt(XXmlStreamNamespaceDeclarations* self,
    int index);

/**
 * @brief      清空独立命名空间声明列表。
 * @param      self 目标独立列表。
 * @return     无；NULL 或借用视图不执行操作。
 */
void XXmlStreamNamespaceDeclarations_clear(XXmlStreamNamespaceDeclarations* self);

/* ========== XXmlStreamReader 结构体 ========== */

/**
 * @brief      XXmlStreamReader XML 流式读取器（对标 Qt 6.8 QXmlStreamReader）
 * @note      前向只读 XML 解析器，支持流式读取 XML Token
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
 * @return      指向初始化完成的 XVtable 的指针
 */
XVtable* XXmlStreamReader_class_init(void);

/* ========== 创建与初始化 ========== */

/**
 * @brief      在堆上创建 XXmlStreamReader 实例
 * @return      指向新创建的 XXmlStreamReader 对象的指针，失败返回 NULL
 */
XXmlStreamReader* XXmlStreamReader_create_ex(XMemoryType memory);

/**
 * @brief      创建并追加字节数组输入的 Reader。
 * @param      data XML 字节数组，只借用。
 * @return     新 Reader；调用方使用 XXmlStreamReader_delete_base 释放。
 */
XXmlStreamReader* XXmlStreamReader_create_byteArray(const XByteArray* data);

/**
 * @brief      创建并追加 UTF-16 XString 输入的 Reader。
 * @param      data UTF-16 XML 文本，只借用。
 * @return     新 Reader；调用方使用 XXmlStreamReader_delete_base 释放。
 */
XXmlStreamReader* XXmlStreamReader_create_string(const XString* data);

/**
 * @brief      创建并追加 UTF-8 C 字符串输入的 Reader。
 * @param      data UTF-8 XML 字符串，只借用。
 * @return     新 Reader；调用方使用 XXmlStreamReader_delete_base 释放。
 */
XXmlStreamReader* XXmlStreamReader_create_utf8(const char* data);

/**
 * @brief      创建并关联 XIODevice 的 Reader。
 * @param      device 输入设备，只借用，Reader 不负责打开、关闭或释放。
 * @return     新 Reader；调用方使用 XXmlStreamReader_delete_base 释放。
 */
XXmlStreamReader* XXmlStreamReader_create_device(struct XIODevice* device);

/**
 * @brief      在堆上拷贝创建 XXmlStreamReader 实例（深拷贝私有数据）
 * @param      other 源 XXmlStreamReader 对象指针
 * @return      指向新创建的 XXmlStreamReader 对象的指针，失败返回 NULL
 */
XXmlStreamReader* XXmlStreamReader_create_copy(const XXmlStreamReader* other);

/**
 * @brief      在堆上移动创建 XXmlStreamReader 实例（转移 other 资源所有权）
 * @param      other 源 XXmlStreamReader 对象指针（移动后被置空）
 * @return      指向新创建的 XXmlStreamReader 对象的指针，失败返回 NULL
 */
XXmlStreamReader* XXmlStreamReader_create_move(XXmlStreamReader* other);

/**
 * @brief      初始化 XXmlStreamReader 实例
 * @param      self 待初始化的 XXmlStreamReader 对象指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_init(XXmlStreamReader* self);

#define  XXmlStreamReader_deinit_base           XClass_deinit_base
#define  XXmlStreamReader_delete_base           XClass_delete_base

/* ========== 数据设置 ========== */

/**
 * @brief      设置输入数据（从字节数组）
 * @param      self 目标 XXmlStreamReader 对象指针
 * @param      data XML 数据字节数组
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_addData(XXmlStreamReader* self, const XByteArray* data);

/**
 * @brief      添加 UTF-16 XString 输入数据。
 * @param      self 目标 XXmlStreamReader 对象指针
 * @param      data UTF-16 XML 文本，只在调用期间借用
 * @return     无；NULL 输入或设备输入状态不执行操作。
 * @note       对标 Qt QXmlStreamReader::addData(QString)。
 */
void XXmlStreamReader_addData_string(XXmlStreamReader* self, const XString* data);

/**
 * @brief      设置输入数据（从 C 字符串）
 * @param      self 目标 XXmlStreamReader 对象指针
 * @param      data XML 数据字符串
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_addData_utf8(XXmlStreamReader* self, const char* data);

/**
 * @brief      清除读取器状态
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_clear(XXmlStreamReader* self);

/**
 * @brief      判断是否到达文档末尾
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      到达末尾返回 true
 */
bool XXmlStreamReader_atEnd(const XXmlStreamReader* self);

/* ========== 读取方法 ========== */

/**
 * @brief      读取下一个 Token
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      Token 类型
 */
int XXmlStreamReader_readNext(XXmlStreamReader* self);

/**
 * @brief      读取下一个开始元素，跳过非开始元素 Token
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      找到开始元素返回 true，否则返回 false
 */
bool XXmlStreamReader_readNextStartElement(XXmlStreamReader* self);

/**
 * @brief      跳过当前元素的所有内容，停在 EndElement 之后
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_skipCurrentElement(XXmlStreamReader* self);

/**
 * @brief      读取当前元素的文本内容
 * @param      self 目标 XXmlStreamReader 对象指针
 * @param      behaviour 子元素处理行为
 * @return      文本内容字符串
 */
const XString* XXmlStreamReader_readElementText(XXmlStreamReader* self, int behaviour);

/* ========== Token 查询 ========== */

/**
 * @brief      获取当前 Token 类型
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      Token 类型枚举值
 */
int XXmlStreamReader_tokenType(const XXmlStreamReader* self);

/**
 * @brief      获取当前 Token 的字符串描述
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      Token 类型名称字符串（UTF-8 编码，内部静态存储，无需释放）
 */
const char* XXmlStreamReader_tokenString(const XXmlStreamReader* self);

/**
 * @brief      判断是否为文档开始
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是文档开始返回 true
 */
bool XXmlStreamReader_isStartDocument(const XXmlStreamReader* self);

/**
 * @brief      判断是否为文档结束
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是文档结束返回 true
 */
bool XXmlStreamReader_isEndDocument(const XXmlStreamReader* self);

/**
 * @brief      判断是否为元素开始标签
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是开始标签返回 true
 */
bool XXmlStreamReader_isStartElement(const XXmlStreamReader* self);

/**
 * @brief      判断是否为元素结束标签
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是结束标签返回 true
 */
bool XXmlStreamReader_isEndElement(const XXmlStreamReader* self);

/**
 * @brief      判断是否为字符数据
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是字符数据返回 true
 */
bool XXmlStreamReader_isCharacters(const XXmlStreamReader* self);

/**
 * @brief      判断是否为空白字符
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是空白字符返回 true
 */
bool XXmlStreamReader_isWhitespace(const XXmlStreamReader* self);

/**
 * @brief      判断当前字符数据是否为 CDATA 段
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是 CDATA 返回 true
 */
bool XXmlStreamReader_isCDATA(const XXmlStreamReader* self);

/**
 * @brief      判断是否为注释
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是注释返回 true
 */
bool XXmlStreamReader_isComment(const XXmlStreamReader* self);

/**
 * @brief      判断是否为 DTD
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是 DTD 返回 true
 */
bool XXmlStreamReader_isDTD(const XXmlStreamReader* self);

/**
 * @brief      判断是否为实体引用
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是实体引用返回 true
 */
bool XXmlStreamReader_isEntityReference(const XXmlStreamReader* self);

/**
 * @brief      判断是否为处理指令
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      是处理指令返回 true
 */
bool XXmlStreamReader_isProcessingInstruction(const XXmlStreamReader* self);

/* ========== 元素信息 ========== */

/**
 * @brief      获取当前元素/属性的命名空间 URI
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      命名空间 URI 字符串
 */
const XString* XXmlStreamReader_namespaceUri(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素/属性的本地名
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      本地名
 */
const XString* XXmlStreamReader_name(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素/属性的限定名
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      限定名
 */
const XString* XXmlStreamReader_qualifiedName(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素/属性的前缀
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      前缀字符串
 */
const XString* XXmlStreamReader_prefix(const XXmlStreamReader* self);

/**
 * @brief      获取当前字符数据或注释文本
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      文本内容
 */
const XString* XXmlStreamReader_text(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素的属性列表
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      指向 XXmlStreamAttributes 的指针
 */
const XXmlStreamAttributes* XXmlStreamReader_attributes(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素属性的独立深拷贝。
 * @param      self 目标 XXmlStreamReader 对象指针。
 * @return     新属性列表；调用方使用 XXmlStreamAttributes_delete 释放。
 * @note       非开始元素返回 NULL；与 Qt attributes() 的值返回语义对应。
 */
XXmlStreamAttributes* XXmlStreamReader_attributes_copy(const XXmlStreamReader* self);

/**
 * @brief      获取当前元素的命名空间声明列表（对标 Qt namespaceDeclarations）
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      只读命名空间声明列表视图；非开始元素返回空列表视图
 */
const XXmlStreamNamespaceDeclarations* XXmlStreamReader_namespaceDeclarations(
    const XXmlStreamReader* self);

/**
 * @brief      获取当前元素命名空间声明的独立深拷贝。
 * @param      self 目标 XXmlStreamReader 对象指针。
 * @return     新列表；调用方使用 XXmlStreamNamespaceDeclarations_delete 释放。
 * @note       非开始元素返回空列表；与 Qt namespaceDeclarations() 的值返回语义对应。
 */
XXmlStreamNamespaceDeclarations* XXmlStreamReader_namespaceDeclarations_copy(
    const XXmlStreamReader* self);

/**
 * @brief      获取命名空间声明列表数量
 * @param      self 目标命名空间声明列表
 * @return      声明数量
 */
int XXmlStreamNamespaceDeclarations_size(const XXmlStreamNamespaceDeclarations* self);

/**
 * @brief      获取命名空间声明数量（Qt count 别名）。
 * @param      self 目标列表，可为 NULL。
 * @return      声明数量。
 */
int XXmlStreamNamespaceDeclarations_count(const XXmlStreamNamespaceDeclarations* self);

/**
 * @brief      判断命名空间声明列表是否为空。
 * @param      self 目标列表，可为 NULL。
 * @return      列表为空或 NULL 返回 true。
 */
bool XXmlStreamNamespaceDeclarations_isEmpty(const XXmlStreamNamespaceDeclarations* self);

/**
 * @brief      判断两个命名空间声明列表是否逐项等价。
 * @param      left 左列表，可为 NULL。
 * @param      right 右列表，可为 NULL。
 * @return      数量和声明顺序均相同返回 true。
 */
bool XXmlStreamNamespaceDeclarations_equals(const XXmlStreamNamespaceDeclarations* left,
    const XXmlStreamNamespaceDeclarations* right);

/**
 * @brief      按索引获取命名空间声明列表项
 * @param      self 目标命名空间声明列表
 * @param      index 索引
 * @return      声明项；越界返回 NULL
 */
const XXmlStreamNamespaceDeclaration* XXmlStreamNamespaceDeclarations_at(
    const XXmlStreamNamespaceDeclarations* self, int index);

/**
 * @brief      判断是否具有命名空间声明
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      有命名空间声明返回 true
 */
bool XXmlStreamReader_hasNamespaceDeclarations(const XXmlStreamReader* self);

/* ========== DTD 信息 ========== */

/**
 * @brief      获取 DTD 名称
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      DTD 名称
 */
const XString* XXmlStreamReader_dtdName(const XXmlStreamReader* self);

/**
 * @brief      获取 DTD 公共标识符
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      DTD 公共标识符
 */
const XString* XXmlStreamReader_dtdPublicId(const XXmlStreamReader* self);

/**
 * @brief      获取 DTD 系统标识符
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      DTD 系统标识符
 */
const XString* XXmlStreamReader_dtdSystemId(const XXmlStreamReader* self);

/* ========== 文档信息 ========== */

/**
 * @brief      获取文档版本
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      文档版本字符串
 */
const XString* XXmlStreamReader_documentVersion(const XXmlStreamReader* self);

/**
 * @brief      获取文档编码
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      文档编码字符串
 */
const XString* XXmlStreamReader_documentEncoding(const XXmlStreamReader* self);

/**
 * @brief      判断输入中是否实际包含 XML 声明
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      已解析到 XML 声明返回 true；没有声明时返回 false
 * @note      与 documentVersion() 区分：无声明时 version 仍可能默认为 1.0。
 */
bool XXmlStreamReader_hasXmlDeclaration(const XXmlStreamReader* self);

/**
 * @brief      判断是否为独立文档
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      独立文档返回 true
 */
bool XXmlStreamReader_isStandaloneDocument(const XXmlStreamReader* self);

/**
 * @brief      判断 XML 声明中是否包含 standalone 关键字
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      含 standalone 声明返回 true
 * @note      对标 QXmlStreamReader::hasStandaloneDeclaration；
 *             即便 standalone="yes"/"no"，只要出现 standalone 关键字即返回 true。
 */
bool XXmlStreamReader_hasStandaloneDeclaration(const XXmlStreamReader* self);

/**
 * @brief      获取当前 Token 的源行号（从 1 开始）
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      行号（>=1）
 * @note      对标 QXmlStreamReader::lineNumber
 */
int64_t XXmlStreamReader_lineNumber(const XXmlStreamReader* self);

/**
 * @brief      获取当前解析游标的源列号（从 0 开始）
 * @param      self 目标 XXmlStreamReader 对象指针；允许为 NULL。
 * @return      当前行内 UTF-8 字节列号（>=0）；无输入时返回 0。
 * @note      对标 QXmlStreamReader::columnNumber
 */
int64_t XXmlStreamReader_columnNumber(const XXmlStreamReader* self);

/**
 * @brief      获取当前 Token 在整个输入中的字符偏移
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      字符偏移（>=0）
 * @note      对标 QXmlStreamReader::characterOffset
 */
int64_t XXmlStreamReader_characterOffset(const XXmlStreamReader* self);

/**
 * @brief      获取处理指令的目标（target）
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      PI 目标字符串；非 PI Token 时返回空字符串
 * @note      对标 QXmlStreamReader::processingInstructionTarget
 */
const XString* XXmlStreamReader_processingInstructionTarget(const XXmlStreamReader* self);

/**
 * @brief      获取处理指令的数据（data）
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      PI 数据字符串；无数据时返回空字符串
 * @note      对标 QXmlStreamReader::processingInstructionData
 */
const XString* XXmlStreamReader_processingInstructionData(const XXmlStreamReader* self);

/**
 * @brief      设置是否启用命名空间处理
 * @param      self   目标 XXmlStreamReader 对象指针
 * @param      enable true 启用（默认），false 关闭
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 * @note      对标 QXmlStreamReader::setNamespaceProcessing
 */
void XXmlStreamReader_setNamespaceProcessing(XXmlStreamReader* self, bool enable);

/**
 * @brief      获取命名空间处理开关
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      true 表示启用
 * @note      对标 QXmlStreamReader::namespaceProcessing
 */
bool XXmlStreamReader_namespaceProcessing(const XXmlStreamReader* self);

/**
 * @brief      添加一个额外的命名空间声明（影响后续元素的 prefix -> uri 解析）
 * @param      self       目标 XXmlStreamReader 对象指针
 * @param      extraDecl  命名空间声明（不可为空；可复用前缀以覆盖默认声明）
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 * @note      对标 QXmlStreamReader::addExtraNamespaceDeclaration
 */
void XXmlStreamReader_addExtraNamespaceDeclaration(XXmlStreamReader* self,
    const XXmlStreamNamespaceDeclaration* extraDecl);

/**
 * @brief      批量添加额外的命名空间声明
 * @param      self      目标 XXmlStreamReader 对象指针
 * @param      extraDecls 声明数组（不可为空；每个元素均为 prefix/uri 对）
 * @param      count     数组元素数量
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 * @note      对标 QXmlStreamReader::addExtraNamespaceDeclarations
 */
void XXmlStreamReader_addExtraNamespaceDeclarations(XXmlStreamReader* self,
    const XXmlStreamNamespaceDeclaration* extraDecls, int count);

/* ========== 错误处理 ========== */

/**
 * @brief      获取错误类型
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      错误类型枚举值
 */
int XXmlStreamReader_error(const XXmlStreamReader* self);

/**
 * @brief      获取错误描述
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      错误描述字符串
 */
const XString* XXmlStreamReader_errorString(const XXmlStreamReader* self);

/**
 * @brief      判断是否有错误
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      有错误返回 true
 */
bool XXmlStreamReader_hasError(const XXmlStreamReader* self);

/**
 * @brief      手动引发错误
 * @param      self    目标 XXmlStreamReader 对象指针
 * @param      message 错误描述
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_raiseError(XXmlStreamReader* self, const XString* message);

/**
 * @brief      手动引发错误（UTF-8 版本）
 * @param      self    目标 XXmlStreamReader 对象指针
 * @param      message 错误描述（UTF-8 字符串）
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_raiseError_utf8(XXmlStreamReader* self, const char* message);

/**
 * @brief      获取实体扩展限制
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      实体扩展限制
 */
int XXmlStreamReader_entityExpansionLimit(const XXmlStreamReader* self);

/**
 * @brief      设置实体扩展限制
 * @param      self  目标 XXmlStreamReader 对象指针
 * @param      limit 实体扩展限制
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_setEntityExpansionLimit(XXmlStreamReader* self, int limit);
/**
 * @brief      设置输入设备（替代 addData）
 * @param      self   目标 XXmlStreamReader 对象指针
 * @param      device XIODevice 指针（可为 NULL）；Reader 只借用，不负责释放
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_setDevice(XXmlStreamReader* self, struct XIODevice* device);

/**
 * @brief      获取当前关联的输入设备
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      XIODevice 指针，未设置返回 NULL
 * @note      对标 QXmlStreamReader::device
 */
struct XIODevice* XXmlStreamReader_device(const XXmlStreamReader* self);


/* ========== DTD 符号声明（对标 QXmlStreamNotationDeclaration） ========== */

/**
 * @brief      DTD 符号声明结构体（对标 Qt 6.8 QXmlStreamNotationDeclaration）
 * @note      表示 DTD 中的 NOTATION 声明
 */
typedef struct XXmlStreamNotationDeclaration
{
    XString* m_name;       /**< 符号名称 */
    XString* m_systemId;   /**< 系统标识符 */
    XString* m_publicId;   /**< 公共标识符 */
} XXmlStreamNotationDeclaration;

/**
 * @brief      创建 DTD 符号声明
 * @return      指向新创建的 XXmlStreamNotationDeclaration 的指针
 */
XXmlStreamNotationDeclaration* XXmlStreamNotationDeclaration_create(void);

/**
 * @brief      初始化 DTD 符号声明
 * @param      self 目标 XXmlStreamNotationDeclaration 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamNotationDeclaration_init(XXmlStreamNotationDeclaration* self);

/**
 * @brief      销毁 DTD 符号声明
 * @param      self 目标 XXmlStreamNotationDeclaration 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamNotationDeclaration_delete(XXmlStreamNotationDeclaration* self);

/**
 * @brief      获取符号名称
 * @param      self 目标 XXmlStreamNotationDeclaration 指针
 * @return      符号名称
 */
const XString* XXmlStreamNotationDeclaration_name(const XXmlStreamNotationDeclaration* self);

/**
 * @brief      获取系统标识符
 * @param      self 目标 XXmlStreamNotationDeclaration 指针
 * @return      系统标识符
 */
const XString* XXmlStreamNotationDeclaration_systemId(const XXmlStreamNotationDeclaration* self);

/**
 * @brief      获取公共标识符
 * @param      self 目标 XXmlStreamNotationDeclaration 指针
 * @return      公共标识符
 */
const XString* XXmlStreamNotationDeclaration_publicId(const XXmlStreamNotationDeclaration* self);

/**
 * @brief      判断两个 DTD 符号声明是否等价。
 * @param      left 左声明，可为 NULL。
 * @param      right 右声明，可为 NULL。
 * @return      字段均相同返回 true。
 */
bool XXmlStreamNotationDeclaration_equals(const XXmlStreamNotationDeclaration* left,
    const XXmlStreamNotationDeclaration* right);

/* ========== DTD 实体声明（对标 QXmlStreamEntityDeclaration） ========== */

/**
 * @brief      DTD 实体声明结构体（对标 Qt 6.8 QXmlStreamEntityDeclaration）
 * @note      表示 DTD 中的 ENTITY 声明
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
 * @return      指向新创建的 XXmlStreamEntityDeclaration 的指针
 */
XXmlStreamEntityDeclaration* XXmlStreamEntityDeclaration_create(void);

/**
 * @brief      初始化 DTD 实体声明
 * @param      self 目标 XXmlStreamEntityDeclaration 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamEntityDeclaration_init(XXmlStreamEntityDeclaration* self);

/**
 * @brief      销毁 DTD 实体声明
 * @param      self 目标 XXmlStreamEntityDeclaration 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamEntityDeclaration_delete(XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取实体名称
 * @param      self 目标 XXmlStreamEntityDeclaration 指针
 * @return      实体名称
 */
const XString* XXmlStreamEntityDeclaration_name(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取符号名称
 * @param      self 目标 XXmlStreamEntityDeclaration 指针
 * @return      符号名称
 */
const XString* XXmlStreamEntityDeclaration_notationName(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取系统标识符
 * @param      self 目标 XXmlStreamEntityDeclaration 指针
 * @return      系统标识符
 */
const XString* XXmlStreamEntityDeclaration_systemId(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取公共标识符
 * @param      self 目标 XXmlStreamEntityDeclaration 指针
 * @return      公共标识符
 */
const XString* XXmlStreamEntityDeclaration_publicId(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      获取实体值
 * @param      self 目标 XXmlStreamEntityDeclaration 指针
 * @return      实体值
 */
const XString* XXmlStreamEntityDeclaration_value(const XXmlStreamEntityDeclaration* self);

/**
 * @brief      判断两个 DTD 实体声明是否等价。
 * @param      left 左声明，可为 NULL。
 * @param      right 右声明，可为 NULL。
 * @return      字段均相同返回 true。
 */
bool XXmlStreamEntityDeclaration_equals(const XXmlStreamEntityDeclaration* left,
    const XXmlStreamEntityDeclaration* right);

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
 * @return      指向新创建的列表的指针
 */
XXmlStreamNotationDeclarations* XXmlStreamNotationDeclarations_create(void);

/**
 * @brief      销毁 DTD 符号声明列表
 * @param      self 目标列表指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamNotationDeclarations_delete(XXmlStreamNotationDeclarations* self);

/**
 * @brief      获取声明数量
 * @param      self 目标列表指针
 * @return      声明数量
 */
size_t XXmlStreamNotationDeclarations_size(const XXmlStreamNotationDeclarations* self);

/**
 * @brief      获取符号声明数量（Qt count 别名）。
 * @param      self 目标列表，可为 NULL。
 * @return      声明数量。
 */
size_t XXmlStreamNotationDeclarations_count(const XXmlStreamNotationDeclarations* self);

/**
 * @brief      判断符号声明列表是否为空。
 * @param      self 目标列表，可为 NULL。
 * @return      列表为空或 NULL 返回 true。
 */
bool XXmlStreamNotationDeclarations_isEmpty(const XXmlStreamNotationDeclarations* self);

/**
 * @brief      判断两个符号声明列表是否逐项等价。
 * @param      left 左列表，可为 NULL。
 * @param      right 右列表，可为 NULL。
 * @return      数量和声明顺序均相同返回 true。
 */
bool XXmlStreamNotationDeclarations_equals(const XXmlStreamNotationDeclarations* left,
    const XXmlStreamNotationDeclarations* right);

/**
 * @brief      按索引获取声明
 * @param      self  目标列表指针
 * @param      index 索引
 * @return      指向声明的指针，越界返回 NULL
 */
const XXmlStreamNotationDeclaration* XXmlStreamNotationDeclarations_at(const XXmlStreamNotationDeclarations* self, size_t index);

/**
 * @brief      追加符号声明副本。
 * @param      self 目标列表。
 * @param      declaration 待复制声明，只借用。
 * @return     成功返回 true，否则返回 false。
 */
bool XXmlStreamNotationDeclarations_append(XXmlStreamNotationDeclarations* self,
    const XXmlStreamNotationDeclaration* declaration);

/**
 * @brief      在指定位置插入符号声明副本。
 * @param      self 目标列表。
 * @param      index 插入位置。
 * @param      declaration 待复制声明，只借用。
 * @return     成功返回 true，否则返回 false。
 */
bool XXmlStreamNotationDeclarations_insert(XXmlStreamNotationDeclarations* self,
    size_t index, const XXmlStreamNotationDeclaration* declaration);

/**
 * @brief      移除指定符号声明。
 * @param      self 目标列表。
 * @param      index 待移除索引。
 * @return     成功返回 true，否则返回 false。
 */
bool XXmlStreamNotationDeclarations_removeAt(XXmlStreamNotationDeclarations* self,
    size_t index);

/**
 * @brief      清空符号声明列表。
 * @param      self 目标列表。
 * @return     无；NULL 输入不执行操作。
 */
void XXmlStreamNotationDeclarations_clear(XXmlStreamNotationDeclarations* self);

/**
 * @brief      创建 DTD 实体声明列表
 * @return      指向新创建的列表的指针
 */
XXmlStreamEntityDeclarations* XXmlStreamEntityDeclarations_create(void);

/**
 * @brief      销毁 DTD 实体声明列表
 * @param      self 目标列表指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamEntityDeclarations_delete(XXmlStreamEntityDeclarations* self);

/**
 * @brief      获取声明数量
 * @param      self 目标列表指针
 * @return      声明数量
 */
size_t XXmlStreamEntityDeclarations_size(const XXmlStreamEntityDeclarations* self);

/**
 * @brief      获取实体声明数量（Qt count 别名）。
 * @param      self 目标列表，可为 NULL。
 * @return      声明数量。
 */
size_t XXmlStreamEntityDeclarations_count(const XXmlStreamEntityDeclarations* self);

/**
 * @brief      判断实体声明列表是否为空。
 * @param      self 目标列表，可为 NULL。
 * @return      列表为空或 NULL 返回 true。
 */
bool XXmlStreamEntityDeclarations_isEmpty(const XXmlStreamEntityDeclarations* self);

/**
 * @brief      判断两个实体声明列表是否逐项等价。
 * @param      left 左列表，可为 NULL。
 * @param      right 右列表，可为 NULL。
 * @return      数量和声明顺序均相同返回 true。
 */
bool XXmlStreamEntityDeclarations_equals(const XXmlStreamEntityDeclarations* left,
    const XXmlStreamEntityDeclarations* right);

/**
 * @brief      按索引获取声明
 * @param      self  目标列表指针
 * @param      index 索引
 * @return      指向声明的指针，越界返回 NULL
 */
const XXmlStreamEntityDeclaration* XXmlStreamEntityDeclarations_at(const XXmlStreamEntityDeclarations* self, size_t index);

/**
 * @brief      追加实体声明副本。
 * @param      self 目标列表。
 * @param      declaration 待复制声明，只借用。
 * @return     成功返回 true，否则返回 false。
 */
bool XXmlStreamEntityDeclarations_append(XXmlStreamEntityDeclarations* self,
    const XXmlStreamEntityDeclaration* declaration);

/**
 * @brief      在指定位置插入实体声明副本。
 * @param      self 目标列表。
 * @param      index 插入位置。
 * @param      declaration 待复制声明，只借用。
 * @return     成功返回 true，否则返回 false。
 */
bool XXmlStreamEntityDeclarations_insert(XXmlStreamEntityDeclarations* self,
    size_t index, const XXmlStreamEntityDeclaration* declaration);

/**
 * @brief      移除指定实体声明。
 * @param      self 目标列表。
 * @param      index 待移除索引。
 * @return     成功返回 true，否则返回 false。
 */
bool XXmlStreamEntityDeclarations_removeAt(XXmlStreamEntityDeclarations* self,
    size_t index);

/**
 * @brief      清空实体声明列表。
 * @param      self 目标列表。
 * @return     无；NULL 输入不执行操作。
 */
void XXmlStreamEntityDeclarations_clear(XXmlStreamEntityDeclarations* self);

/* ========== 实体解析器（对标 QXmlStreamEntityResolver） ========== */

/**
 * @brief      实体解析器回调函数类型
 */
typedef const XString* (*XXmlStreamEntityResolver_ResolveEntityCallback)(
    const XString* publicId, const XString* systemId, void* userData);

/**
 * @brief      未声明实体解析回调函数类型
 */
typedef const XString* (*XXmlStreamEntityResolver_ResolveUndeclaredEntityCallback)(
    const XString* name, void* userData);

/**
 * @brief      实体解析器结构体（对标 Qt 6.8 QXmlStreamEntityResolver）
 * @note      用于解析自定义实体
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
 * @return      指向新创建的 XXmlStreamEntityResolver 的指针
 */
XXmlStreamEntityResolver* XXmlStreamEntityResolver_create(void);

/**
 * @brief      初始化实体解析器
 * @param      self 目标 XXmlStreamEntityResolver 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamEntityResolver_init(XXmlStreamEntityResolver* self);

/**
 * @brief      销毁实体解析器
 * @param      self 目标 XXmlStreamEntityResolver 指针
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamEntityResolver_delete(XXmlStreamEntityResolver* self);

/**
 * @brief      解析实体
 * @param      self      目标 XXmlStreamEntityResolver 指针
 * @param      publicId 公共标识符
 * @param      systemId 系统标识符
 * @return      解析后的实体值，未找到返回 NULL
 */
const XString* XXmlStreamEntityResolver_resolveEntity(XXmlStreamEntityResolver* self,
    const XString* publicId, const XString* systemId);

/**
 * @brief      解析未声明实体
 * @param      self 目标 XXmlStreamEntityResolver 指针
 * @param      name 实体名称
 * @return      解析后的实体值，未找到返回 NULL
 */
const XString* XXmlStreamEntityResolver_resolveUndeclaredEntity(XXmlStreamEntityResolver* self,
    const XString* name);

/**
 * @brief      设置用户数据
 * @param      self     目标 XXmlStreamEntityResolver 指针
 * @param      userData 用户数据
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamEntityResolver_setUserData(XXmlStreamEntityResolver* self, void* userData);

/**
 * @brief      获取用户数据
 * @param      self 目标 XXmlStreamEntityResolver 指针
 * @return      用户数据
 */
void* XXmlStreamEntityResolver_userData(XXmlStreamEntityResolver* self);

/* ========== XXmlStreamReader DTD 相关 API ========== */

/**
 * @brief      获取 DTD 符号声明列表
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      DTD 符号声明列表（调用者不负责释放）
 */
XXmlStreamNotationDeclarations* XXmlStreamReader_notationDeclarations(const XXmlStreamReader* self);

/**
 * @brief      获取 DTD 符号声明的独立深拷贝。
 * @param      self 目标 XXmlStreamReader 对象指针。
 * @return     新列表；调用方负责释放。
 */
XXmlStreamNotationDeclarations* XXmlStreamReader_notationDeclarations_copy(
    const XXmlStreamReader* self);

/**
 * @brief      获取 DTD 实体声明列表
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      DTD 实体声明列表（调用者不负责释放）
 */
XXmlStreamEntityDeclarations* XXmlStreamReader_entityDeclarations(const XXmlStreamReader* self);

/**
 * @brief      获取 DTD 实体声明的独立深拷贝。
 * @param      self 目标 XXmlStreamReader 对象指针。
 * @return     新列表；调用方负责释放。
 */
XXmlStreamEntityDeclarations* XXmlStreamReader_entityDeclarations_copy(
    const XXmlStreamReader* self);

/**
 * @brief      设置实体解析器
 * @param      self     目标 XXmlStreamReader 对象指针
 * @param      resolver 实体解析器（可为空）
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XXmlStreamReader_setEntityResolver(XXmlStreamReader* self, XXmlStreamEntityResolver* resolver);

/**
 * @brief      获取实体解析器
 * @param      self 目标 XXmlStreamReader 对象指针
 * @return      实体解析器（无则返回 NULL）
 */
XXmlStreamEntityResolver* XXmlStreamReader_entityResolver(const XXmlStreamReader* self);
#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XXmlStreamReader_create
#define XXmlStreamReader_create() XXmlStreamReader_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XXMLSTREAMREADER_H */
