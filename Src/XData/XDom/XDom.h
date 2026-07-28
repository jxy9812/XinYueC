/**
 * @file       XDom.h
 * @brief      XML DOM 文档对象模型公开 API。
 * @details    对齐 Qt 6.8 的 QDomNode、QDomDocument、QDomElement 及其相关类型。
 *             字符串对象使用 XinYueC 的 UTF-16 内部表示，字节输入输出使用 UTF-8。
 *             本模块只依赖 XinYueC 抽象层，不调用 Win32、POSIX、Qt 或其他平台 API，
 *             可用于嵌入式环境。所有返回的对象都必须使用对应的 *_delete_base 释放。
 */
#ifndef XDOM_H
#define XDOM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XClass.h"
#include "XString.h"
#include "XByteArray.h"

/**
 * @brief      DOM 公共句柄类型的虚函数表声明。
 * @details    各句柄的第一个成员都是 XClass；DOM 节点数据通过隐式共享私有节点保存。
 *             句柄复制只增加引用计数，真正的树复制由 XDomNode_cloneNode 完成。
 */
/** DOM 基础节点句柄，对齐 Qt QDomNode。 */
XCLASS_DEFINE_BEGING(XDomNode)
XCLASS_DEFINE_EXTEND_END(XDomNode, XClass)
/** 节点列表句柄，对齐 Qt QDomNodeList。 */
XCLASS_DEFINE_BEGING(XDomNodeList)
XCLASS_DEFINE_EXTEND_END(XDomNodeList, XClass)
/** 命名节点映射句柄，对齐 Qt QDomNamedNodeMap。 */
XCLASS_DEFINE_BEGING(XDomNamedNodeMap)
XCLASS_DEFINE_EXTEND_END(XDomNamedNodeMap, XClass)
/** DOM 文档句柄，对齐 Qt QDomDocument。 */
XCLASS_DEFINE_BEGING(XDomDocument)
XCLASS_DEFINE_EXTEND_END(XDomDocument, XClass)
/** 文档类型声明句柄，对齐 Qt QDomDocumentType。 */
XCLASS_DEFINE_BEGING(XDomDocumentType)
XCLASS_DEFINE_EXTEND_END(XDomDocumentType, XClass)
/** 文档片段句柄，对齐 Qt QDomDocumentFragment。 */
XCLASS_DEFINE_BEGING(XDomDocumentFragment)
XCLASS_DEFINE_EXTEND_END(XDomDocumentFragment, XClass)
/** 字符数据基类句柄，对齐 Qt QDomCharacterData。 */
XCLASS_DEFINE_BEGING(XDomCharacterData)
XCLASS_DEFINE_EXTEND_END(XDomCharacterData, XClass)
/** 属性节点句柄，对齐 Qt QDomAttr。 */
XCLASS_DEFINE_BEGING(XDomAttr)
XCLASS_DEFINE_EXTEND_END(XDomAttr, XClass)
/** 元素节点句柄，对齐 Qt QDomElement。 */
XCLASS_DEFINE_BEGING(XDomElement)
XCLASS_DEFINE_EXTEND_END(XDomElement, XClass)
/** 文本节点句柄，对齐 Qt QDomText。 */
XCLASS_DEFINE_BEGING(XDomText)
XCLASS_DEFINE_EXTEND_END(XDomText, XDomCharacterData)
/** 注释节点句柄，对齐 Qt QDomComment。 */
XCLASS_DEFINE_BEGING(XDomComment)
XCLASS_DEFINE_EXTEND_END(XDomComment, XDomCharacterData)
/** CDATA 节点句柄，对齐 Qt QDomCDATASection。 */
XCLASS_DEFINE_BEGING(XDomCDATASection)
XCLASS_DEFINE_EXTEND_END(XDomCDATASection, XDomText)
/** 实体引用节点句柄，对齐 Qt QDomEntityReference。 */
XCLASS_DEFINE_BEGING(XDomEntityReference)
XCLASS_DEFINE_EXTEND_END(XDomEntityReference, XDomNode)
/** 处理指令节点句柄，对齐 Qt QDomProcessingInstruction。 */
XCLASS_DEFINE_BEGING(XDomProcessingInstruction)
XCLASS_DEFINE_EXTEND_END(XDomProcessingInstruction, XDomNode)
/** 实体声明句柄，对齐 Qt QDomEntity。 */
XCLASS_DEFINE_BEGING(XDomEntity)
XCLASS_DEFINE_EXTEND_END(XDomEntity, XDomNode)
/** 符号声明句柄，对齐 Qt QDomNotation。 */
XCLASS_DEFINE_BEGING(XDomNotation)
XCLASS_DEFINE_EXTEND_END(XDomNotation, XDomNode)
/** DOM 实现能力句柄，对齐 Qt QDomImplementation。 */
XCLASS_DEFINE_BEGING(XDomImplementation)
XCLASS_DEFINE_EXTEND_END(XDomImplementation, XClass)

/**
 * @brief      DOM 私有节点的前置声明。
 * @details    该类型只在实现文件中定义；调用者不能访问、释放或修改它。
 */
typedef struct XDomNodePrivate XDomNodePrivate;

/**
 * @brief      DOM 节点类型。
 * @details    数值与 W3C DOM 节点类型保持兼容；CharacterDataNode 是 XinYueC
 *             对字符数据基类的扩展标识，不对应 XML 文档中的独立节点。
 */
typedef enum XDomNodeType {
    XDom_UnknownNode = 0,                 /**< 空句柄或未知节点。 */
    XDom_ElementNode = 1,                 /**< 元素节点。 */
    XDom_AttributeNode = 2,               /**< 属性节点。 */
    XDom_TextNode = 3,                    /**< 普通文本节点。 */
    XDom_CDATASectionNode = 4,            /**< CDATA 区段节点。 */
    XDom_EntityReferenceNode = 5,         /**< 实体引用节点。 */
    XDom_EntityNode = 6,                  /**< 实体声明节点。 */
    XDom_ProcessingInstructionNode = 7,  /**< 处理指令节点。 */
    XDom_CommentNode = 8,                 /**< XML 注释节点。 */
    XDom_DocumentNode = 9,                /**< 文档根节点。 */
    XDom_DocumentTypeNode = 10,           /**< 文档类型声明节点。 */
    XDom_DocumentFragmentNode = 11,       /**< 临时文档片段节点。 */
    XDom_NotationNode = 12,               /**< 符号声明节点。 */
    XDom_CharacterDataNode = 22          /**< 字符数据基类标识，仅用于类型判断。 */
} XDomNodeType;

/**
 * @brief      DOM 字节序列编码策略。
 * @details    DOM 内部字符串始终为 UTF-16；该枚举只决定序列化为 XByteArray 时使用的声明策略。
 */
typedef enum XDomEncodingPolicy {
    XDom_EncodingFromDocument = 1,      /**< 根据文档声明输出编码。 */
    XDom_EncodingFromTextStream = 2     /**< 按文本流约定输出 UTF-8。 */
} XDomEncodingPolicy;

/**
 * @brief      setContent 的解析选项。
 * @details    选项可以按位组合；未设置选项时使用 Qt 风格的默认解析行为。
 */
typedef enum XDomParseOption {
    XDom_ParseDefault = 0x00,                  /**< 不启用额外选项。 */
    XDom_UseNamespaceProcessing = 0x01,        /**< 解析命名空间、prefix 和 localName。 */
    XDom_PreserveSpacingOnlyNodes = 0x02       /**< 保留只包含空白的文本节点。 */
} XDomParseOption;

/**
 * @brief      无效 XML 字符的处理策略。
 * @details    该策略是进程级配置，不依赖平台 API；默认接受输入中的无效字符以兼容既有流读取器。
 */
typedef enum XDomInvalidDataPolicy {
    XDom_AcceptInvalidChars = 0,  /**< 保留无效字符，不因字符本身中止解析。 */
    XDom_DropInvalidChars = 1,    /**< 丢弃无法表示的字符后继续解析。 */
    XDom_ReturnNullNode = 2       /**< 解析到无效字符时返回失败结果和空文档句柄。 */
} XDomInvalidDataPolicy;

/**
 * @brief      setContent 的解析结果。
 * @details    错误消息由结构体拥有；调用 XDomParseResult_deinit 后不可继续使用。
 */
typedef struct XDomParseResult {
    XString* m_errorMessage; /**< 新分配的错误消息；成功时为 NULL，调用方负责反初始化。 */
    int64_t m_errorLine;     /**< 错误所在行号，从 1 开始；未知时为 -1。 */
    int64_t m_errorColumn;   /**< 错误所在列号，从 1 开始；未知时为 -1。 */
} XDomParseResult;

/**
 * @brief      DOM 基础节点句柄。
 * @details    `m_impl` 是隐式共享的内部节点引用。空句柄仍是已初始化对象，
 *             可以安全调用查询 API，但查询结果为空或返回默认值。
 */
typedef struct XDomNode {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部共享节点，仅供实现使用，调用者不得解引用。 */
} XDomNode;

/** @brief 节点列表句柄；列表元素为借用的节点包装对象，越界返回空句柄。 */
typedef struct XDomNodeList {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部列表对象，仅供实现使用。 */
} XDomNodeList;

/** @brief 命名节点映射句柄；用于属性、实体或符号声明的名称查询。 */
typedef struct XDomNamedNodeMap {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部映射对象，仅供实现使用。 */
} XDomNamedNodeMap;

/** @brief DOM 文档句柄；负责创建节点并拥有文档树根。 */
typedef struct XDomDocument {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部文档节点，仅供实现使用。 */
} XDomDocument;

/** @brief 文档类型声明句柄；保存名称、公共标识符、系统标识符和 DTD 集合。 */
typedef struct XDomDocumentType {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部文档类型节点，仅供实现使用。 */
} XDomDocumentType;

/** @brief 文档片段句柄；可作为临时节点容器批量挂接到文档树。 */
typedef struct XDomDocumentFragment {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部文档片段节点，仅供实现使用。 */
} XDomDocumentFragment;

/** @brief 字符数据基类句柄；适用于文本、CDATA 和注释节点。 */
typedef struct XDomCharacterData {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部字符数据节点，仅供实现使用。 */
} XDomCharacterData;

/** @brief 属性节点句柄；挂接到元素后 parentNode 和 ownerElement 均可查询。 */
typedef struct XDomAttr {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部属性节点，仅供实现使用。 */
} XDomAttr;

/** @brief 元素节点句柄；可持有子节点和属性。 */
typedef struct XDomElement {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部元素节点，仅供实现使用。 */
} XDomElement;

/** @brief 文本节点句柄；索引按 UTF-16 代码单元计算。 */
typedef struct XDomText {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部文本节点，仅供实现使用。 */
} XDomText;

/** @brief 注释节点句柄；数据不包含 XML 注释标记。 */
typedef struct XDomComment {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部注释节点，仅供实现使用。 */
} XDomComment;

/** @brief CDATA 节点句柄；数据不包含 `<![CDATA[` 和 `]]>` 标记。 */
typedef struct XDomCDATASection {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部 CDATA 节点，仅供实现使用。 */
} XDomCDATASection;

/** @brief 实体引用节点句柄；保存实体名称并可持有替换内容。 */
typedef struct XDomEntityReference {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部实体引用节点，仅供实现使用。 */
} XDomEntityReference;

/** @brief 处理指令节点句柄；保存 target 和 data。 */
typedef struct XDomProcessingInstruction {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部处理指令节点，仅供实现使用。 */
} XDomProcessingInstruction;

/** @brief 实体声明句柄；仅可通过文档类型节点的实体映射访问。 */
typedef struct XDomEntity {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部实体节点，仅供实现使用。 */
} XDomEntity;

/** @brief 符号声明句柄；仅可通过文档类型节点的符号映射访问。 */
typedef struct XDomNotation {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部符号节点，仅供实现使用。 */
} XDomNotation;

/**
 * @brief      DOM 实现能力句柄。
 * @details    该对象不拥有文档；它只描述实现能力并创建文档类型或新文档。
 */
typedef struct XDomImplementation {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    bool m_isNull;                  /**< 是否为空实现；仅由生命周期函数维护。 */
} XDomImplementation;

/**
 * @brief      初始化各 DOM 类型的虚函数表。
 * @return     进程内共享的虚函数表指针；分配失败时返回 NULL。
 * @note       调用者不拥有返回值，不得释放或修改虚函数表。
 */
XVtable* XDomNode_class_init(void);
/** @brief 初始化 XDomNodeList 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomNodeList_class_init(void);
/** @brief 初始化 XDomNamedNodeMap 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomNamedNodeMap_class_init(void);
/** @brief 初始化 XDomDocument 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomDocument_class_init(void);
/** @brief 初始化 XDomDocumentType 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomDocumentType_class_init(void);
/** @brief 初始化 XDomDocumentFragment 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomDocumentFragment_class_init(void);
/** @brief 初始化 XDomCharacterData 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomCharacterData_class_init(void);
/** @brief 初始化 XDomAttr 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomAttr_class_init(void);
/** @brief 初始化 XDomElement 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomElement_class_init(void);
/** @brief 初始化 XDomText 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomText_class_init(void);
/** @brief 初始化 XDomComment 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomComment_class_init(void);
/** @brief 初始化 XDomCDATASection 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomCDATASection_class_init(void);
/** @brief 初始化 XDomEntityReference 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomEntityReference_class_init(void);
/** @brief 初始化 XDomProcessingInstruction 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomProcessingInstruction_class_init(void);
/** @brief 初始化 XDomEntity 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomEntity_class_init(void);
/** @brief 初始化 XDomNotation 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomNotation_class_init(void);
/** @brief 初始化 XDomImplementation 虚函数表。 @return 共享虚函数表指针，失败返回 NULL。 */
XVtable* XDomImplementation_class_init(void);

/**
 * @brief      声明 DOM 类型的标准生命周期 API。
 * @details    `copy_base` 和 `move_base` 会先检查目标、源和目标 vtable；目标未初始化时
 *             自动调用完整的 Type_init。移动成功后源对象仍可反初始化，但内部句柄为空。
 * @param      Type DOM 类型名称；该宏只在头文件内部用于生成声明。
 * @note       堆对象必须使用对应 Type_delete_base；栈对象必须成对调用 Type_init 和 Type_deinit_base。
 */
#define XDOM_DECLARE_LIFECYCLE(Type) \
    void Type##_init(Type* self); \
    Type* Type##_create(void); \
    Type* Type##_create_copy(const Type* other); \
    Type* Type##_create_move(Type* other); \
    void Type##_deinit_base(Type* self); \
    void Type##_delete_base(Type* self); \
    void Type##_copy_base(Type* dest, const Type* src); \
    void Type##_move_base(Type* dest, Type* src)

XDOM_DECLARE_LIFECYCLE(XDomNode);
XDOM_DECLARE_LIFECYCLE(XDomNodeList);
XDOM_DECLARE_LIFECYCLE(XDomNamedNodeMap);
XDOM_DECLARE_LIFECYCLE(XDomDocument);
XDOM_DECLARE_LIFECYCLE(XDomDocumentType);
XDOM_DECLARE_LIFECYCLE(XDomDocumentFragment);
XDOM_DECLARE_LIFECYCLE(XDomCharacterData);
XDOM_DECLARE_LIFECYCLE(XDomAttr);
XDOM_DECLARE_LIFECYCLE(XDomElement);
XDOM_DECLARE_LIFECYCLE(XDomText);
XDOM_DECLARE_LIFECYCLE(XDomComment);
XDOM_DECLARE_LIFECYCLE(XDomCDATASection);
XDOM_DECLARE_LIFECYCLE(XDomEntityReference);
XDOM_DECLARE_LIFECYCLE(XDomProcessingInstruction);
XDOM_DECLARE_LIFECYCLE(XDomEntity);
XDOM_DECLARE_LIFECYCLE(XDomNotation);
XDOM_DECLARE_LIFECYCLE(XDomImplementation);

/* QDomNode */
XDomNode* XDomNode_insertBefore(XDomNode* self, const XDomNode* newChild, const XDomNode* refChild);
XDomNode* XDomNode_insertAfter(XDomNode* self, const XDomNode* newChild, const XDomNode* refChild);
XDomNode* XDomNode_replaceChild(XDomNode* self, const XDomNode* newChild, const XDomNode* oldChild);
XDomNode* XDomNode_removeChild(XDomNode* self, const XDomNode* oldChild);
XDomNode* XDomNode_appendChild(XDomNode* self, const XDomNode* newChild);
bool XDomNode_hasChildNodes(const XDomNode* self);
XDomNode* XDomNode_cloneNode(const XDomNode* self, bool deep);
void XDomNode_normalize(XDomNode* self);
bool XDomNode_isSupported(const XDomNode* self, const XString* feature, const XString* version);
bool XDomNode_isSupported_utf8(const XDomNode* self, const char* feature, const char* version);
const XString* XDomNode_nodeName(const XDomNode* self);
XDomNodeType XDomNode_nodeType(const XDomNode* self);
XDomNode* XDomNode_parentNode(const XDomNode* self);
XDomNodeList* XDomNode_childNodes(const XDomNode* self);
XDomNode* XDomNode_firstChild(const XDomNode* self);
XDomNode* XDomNode_lastChild(const XDomNode* self);
XDomNode* XDomNode_previousSibling(const XDomNode* self);
XDomNode* XDomNode_nextSibling(const XDomNode* self);
XDomNamedNodeMap* XDomNode_attributes(const XDomNode* self);
XDomDocument* XDomNode_ownerDocument(const XDomNode* self);
const XString* XDomNode_namespaceURI(const XDomNode* self);
const XString* XDomNode_localName(const XDomNode* self);
bool XDomNode_hasAttributes(const XDomNode* self);
const XString* XDomNode_nodeValue(const XDomNode* self);
void XDomNode_setNodeValue(XDomNode* self, const XString* value);
void XDomNode_setNodeValue_utf8(XDomNode* self, const char* value);
const XString* XDomNode_prefix(const XDomNode* self);
void XDomNode_setPrefix(XDomNode* self, const XString* prefix);
void XDomNode_setPrefix_utf8(XDomNode* self, const char* prefix);
XDomNode* XDomNode_namedItem(const XDomNode* self, const XString* name);
XDomNode* XDomNode_namedItem_utf8(const XDomNode* self, const char* name);
bool XDomNode_isNull(const XDomNode* self);
void XDomNode_clear(XDomNode* self);
bool XDomNode_isAttr(const XDomNode* self);
bool XDomNode_isCDATASection(const XDomNode* self);
bool XDomNode_isDocumentFragment(const XDomNode* self);
bool XDomNode_isDocument(const XDomNode* self);
bool XDomNode_isDocumentType(const XDomNode* self);
bool XDomNode_isElement(const XDomNode* self);
bool XDomNode_isEntityReference(const XDomNode* self);
bool XDomNode_isText(const XDomNode* self);
bool XDomNode_isEntity(const XDomNode* self);
bool XDomNode_isNotation(const XDomNode* self);
bool XDomNode_isProcessingInstruction(const XDomNode* self);
bool XDomNode_isCharacterData(const XDomNode* self);
bool XDomNode_isComment(const XDomNode* self);
XDomElement* XDomNode_toElement(const XDomNode* self);
XDomAttr* XDomNode_toAttr(const XDomNode* self);
XDomText* XDomNode_toText(const XDomNode* self);
XDomCDATASection* XDomNode_toCDATASection(const XDomNode* self);
XDomComment* XDomNode_toComment(const XDomNode* self);
XDomCharacterData* XDomNode_toCharacterData(const XDomNode* self);
XDomDocument* XDomNode_toDocument(const XDomNode* self);
XDomDocumentType* XDomNode_toDocumentType(const XDomNode* self);
XDomDocumentFragment* XDomNode_toDocumentFragment(const XDomNode* self);
XDomEntityReference* XDomNode_toEntityReference(const XDomNode* self);
XDomEntity* XDomNode_toEntity(const XDomNode* self);
XDomNotation* XDomNode_toNotation(const XDomNode* self);
XDomProcessingInstruction* XDomNode_toProcessingInstruction(const XDomNode* self);
XDomElement* XDomNode_firstChildElement(const XDomNode* self, const XString* tagName, const XString* namespaceURI);
XDomElement* XDomNode_firstChildElement_utf8(const XDomNode* self, const char* tagName, const char* namespaceURI);
XDomElement* XDomNode_lastChildElement(const XDomNode* self, const XString* tagName, const XString* namespaceURI);
XDomElement* XDomNode_lastChildElement_utf8(const XDomNode* self, const char* tagName, const char* namespaceURI);
XDomElement* XDomNode_previousSiblingElement(const XDomNode* self, const XString* tagName, const XString* namespaceURI);
XDomElement* XDomNode_nextSiblingElement(const XDomNode* self, const XString* tagName, const XString* namespaceURI);
int64_t XDomNode_lineNumber(const XDomNode* self);
int64_t XDomNode_columnNumber(const XDomNode* self);
XString* XDomNode_toString(const XDomNode* self, int indent);

/* QDomNodeList */
XDomNode* XDomNodeList_item(const XDomNodeList* self, int index);
XDomNode* XDomNodeList_at(const XDomNodeList* self, int index);
int XDomNodeList_length(const XDomNodeList* self);
int XDomNodeList_count(const XDomNodeList* self);
int XDomNodeList_size(const XDomNodeList* self);
bool XDomNodeList_isEmpty(const XDomNodeList* self);

/* QDomNamedNodeMap */
XDomNode* XDomNamedNodeMap_namedItem(const XDomNamedNodeMap* self, const XString* name);
XDomNode* XDomNamedNodeMap_namedItem_utf8(const XDomNamedNodeMap* self, const char* name);
XDomNode* XDomNamedNodeMap_setNamedItem(XDomNamedNodeMap* self, const XDomNode* newNode);
XDomNode* XDomNamedNodeMap_removeNamedItem(XDomNamedNodeMap* self, const XString* name);
XDomNode* XDomNamedNodeMap_removeNamedItem_utf8(XDomNamedNodeMap* self, const char* name);
XDomNode* XDomNamedNodeMap_item(const XDomNamedNodeMap* self, int index);
XDomNode* XDomNamedNodeMap_namedItemNS(const XDomNamedNodeMap* self, const XString* namespaceURI, const XString* localName);
XDomNode* XDomNamedNodeMap_namedItemNS_utf8(const XDomNamedNodeMap* self, const char* namespaceURI, const char* localName);
XDomNode* XDomNamedNodeMap_setNamedItemNS(XDomNamedNodeMap* self, const XDomNode* newNode);
XDomNode* XDomNamedNodeMap_removeNamedItemNS(XDomNamedNodeMap* self, const XString* namespaceURI, const XString* localName);
int XDomNamedNodeMap_length(const XDomNamedNodeMap* self);
int XDomNamedNodeMap_count(const XDomNamedNodeMap* self);
int XDomNamedNodeMap_size(const XDomNamedNodeMap* self);
bool XDomNamedNodeMap_isEmpty(const XDomNamedNodeMap* self);
bool XDomNamedNodeMap_contains(const XDomNamedNodeMap* self, const XString* name);
bool XDomNamedNodeMap_contains_utf8(const XDomNamedNodeMap* self, const char* name);

/* QDomDocument */
XDomDocument* XDomDocument_createName(const XString* name);
XDomDocument* XDomDocument_createName_utf8(const char* name);
XDomDocumentType* XDomDocument_doctype(const XDomDocument* self);
XDomImplementation* XDomDocument_implementation(const XDomDocument* self);
XDomElement* XDomDocument_documentElement(const XDomDocument* self);
XDomElement* XDomDocument_createElement(XDomDocument* self, const XString* tagName);
XDomElement* XDomDocument_createElement_utf8(XDomDocument* self, const char* tagName);
XDomDocumentFragment* XDomDocument_createDocumentFragment(XDomDocument* self);
XDomText* XDomDocument_createTextNode(XDomDocument* self, const XString* data);
XDomText* XDomDocument_createTextNode_utf8(XDomDocument* self, const char* data);
XDomComment* XDomDocument_createComment(XDomDocument* self, const XString* data);
XDomComment* XDomDocument_createComment_utf8(XDomDocument* self, const char* data);
XDomCDATASection* XDomDocument_createCDATASection(XDomDocument* self, const XString* data);
XDomCDATASection* XDomDocument_createCDATASection_utf8(XDomDocument* self, const char* data);
XDomProcessingInstruction* XDomDocument_createProcessingInstruction(XDomDocument* self, const XString* target, const XString* data);
XDomProcessingInstruction* XDomDocument_createProcessingInstruction_utf8(XDomDocument* self, const char* target, const char* data);
XDomAttr* XDomDocument_createAttribute(XDomDocument* self, const XString* name);
XDomAttr* XDomDocument_createAttribute_utf8(XDomDocument* self, const char* name);
XDomEntityReference* XDomDocument_createEntityReference(XDomDocument* self, const XString* name);
XDomEntityReference* XDomDocument_createEntityReference_utf8(XDomDocument* self, const char* name);
XDomNodeList* XDomDocument_elementsByTagName(const XDomDocument* self, const XString* tagName);
XDomNodeList* XDomDocument_elementsByTagName_utf8(const XDomDocument* self, const char* tagName);
XDomNode* XDomDocument_importNode(XDomDocument* self, const XDomNode* importedNode, bool deep);
XDomElement* XDomDocument_createElementNS(XDomDocument* self, const XString* namespaceURI, const XString* qualifiedName);
XDomElement* XDomDocument_createElementNS_utf8(XDomDocument* self, const char* namespaceURI, const char* qualifiedName);
XDomAttr* XDomDocument_createAttributeNS(XDomDocument* self, const XString* namespaceURI, const XString* qualifiedName);
XDomAttr* XDomDocument_createAttributeNS_utf8(XDomDocument* self, const char* namespaceURI, const char* qualifiedName);
XDomNodeList* XDomDocument_elementsByTagNameNS(const XDomDocument* self, const XString* namespaceURI, const XString* localName);
XDomNodeList* XDomDocument_elementsByTagNameNS_utf8(const XDomDocument* self, const char* namespaceURI, const char* localName);
XDomElement* XDomDocument_elementById(const XDomDocument* self, const XString* id);
XDomElement* XDomDocument_elementById_utf8(const XDomDocument* self, const char* id);
XDomParseResult XDomDocument_setContent_result(XDomDocument* self, const XByteArray* data, unsigned int options);
XDomParseResult XDomDocument_setContent_utf8_result(XDomDocument* self, const char* data, unsigned int options);
bool XDomDocument_setContent(XDomDocument* self, const XByteArray* data, unsigned int options,
                                XString** errorMessage, int64_t* errorLine, int64_t* errorColumn);
bool XDomDocument_setContent_utf8(XDomDocument* self, const char* data, unsigned int options,
                                     XString** errorMessage, int64_t* errorLine, int64_t* errorColumn);
XString* XDomDocument_toString(const XDomDocument* self, int indent);
XByteArray* XDomDocument_toByteArray(const XDomDocument* self, int indent);
void XDomParseResult_init(XDomParseResult* result);
void XDomParseResult_deinit(XDomParseResult* result);
bool XDomParseResult_isSuccess(const XDomParseResult* result);

/* QDomDocumentType */
const XString* XDomDocumentType_name(const XDomDocumentType* self);
XDomNamedNodeMap* XDomDocumentType_entities(const XDomDocumentType* self);
XDomNamedNodeMap* XDomDocumentType_notations(const XDomDocumentType* self);
const XString* XDomDocumentType_publicId(const XDomDocumentType* self);
const XString* XDomDocumentType_systemId(const XDomDocumentType* self);
const XString* XDomDocumentType_internalSubset(const XDomDocumentType* self);

/* QDomElement */
const XString* XDomElement_tagName(const XDomElement* self);
void XDomElement_setTagName(XDomElement* self, const XString* name);
void XDomElement_setTagName_utf8(XDomElement* self, const char* name);
const XString* XDomElement_attribute(const XDomElement* self, const XString* name, const XString* defaultValue);
const XString* XDomElement_attribute_utf8(const XDomElement* self, const char* name, const char* defaultValue);
void XDomElement_setAttribute(XDomElement* self, const XString* name, const XString* value);
void XDomElement_setAttribute_utf8(XDomElement* self, const char* name, const char* value);
void XDomElement_setAttribute_int(XDomElement* self, const XString* name, int value);
void XDomElement_setAttribute_uint(XDomElement* self, const XString* name, unsigned int value);
void XDomElement_setAttribute_int64(XDomElement* self, const XString* name, int64_t value);
void XDomElement_setAttribute_uint64(XDomElement* self, const XString* name, uint64_t value);
void XDomElement_setAttribute_double(XDomElement* self, const XString* name, double value);
void XDomElement_removeAttribute(XDomElement* self, const XString* name);
void XDomElement_removeAttribute_utf8(XDomElement* self, const char* name);
XDomAttr* XDomElement_attributeNode(const XDomElement* self, const XString* name);
XDomAttr* XDomElement_attributeNode_utf8(const XDomElement* self, const char* name);
XDomAttr* XDomElement_setAttributeNode(XDomElement* self, const XDomAttr* newAttr);
XDomAttr* XDomElement_removeAttributeNode(XDomElement* self, const XDomAttr* oldAttr);
bool XDomElement_hasAttribute(const XDomElement* self, const XString* name);
bool XDomElement_hasAttribute_utf8(const XDomElement* self, const char* name);
const XString* XDomElement_attributeNS(const XDomElement* self, const XString* namespaceURI, const XString* localName, const XString* defaultValue);
const XString* XDomElement_attributeNS_utf8(const XDomElement* self, const char* namespaceURI, const char* localName, const char* defaultValue);
void XDomElement_setAttributeNS(XDomElement* self, const XString* namespaceURI, const XString* qualifiedName, const XString* value);
void XDomElement_setAttributeNS_utf8(XDomElement* self, const char* namespaceURI, const char* qualifiedName, const char* value);
void XDomElement_removeAttributeNS(XDomElement* self, const XString* namespaceURI, const XString* localName);
void XDomElement_removeAttributeNS_utf8(XDomElement* self, const char* namespaceURI, const char* localName);
XDomAttr* XDomElement_attributeNodeNS(const XDomElement* self, const XString* namespaceURI, const XString* localName);
XDomAttr* XDomElement_setAttributeNodeNS(XDomElement* self, const XDomAttr* newAttr);
bool XDomElement_hasAttributeNS(const XDomElement* self, const XString* namespaceURI, const XString* localName);
XDomNodeList* XDomElement_elementsByTagName(const XDomElement* self, const XString* tagName);
XDomNodeList* XDomElement_elementsByTagName_utf8(const XDomElement* self, const char* tagName);
XDomNodeList* XDomElement_elementsByTagNameNS(const XDomElement* self, const XString* namespaceURI, const XString* localName);
const XString* XDomElement_text(const XDomElement* self);
XDomNamedNodeMap* XDomElement_attributes(const XDomElement* self);

/* QDomAttr */
const XString* XDomAttr_name(const XDomAttr* self);
bool XDomAttr_specified(const XDomAttr* self);
XDomElement* XDomAttr_ownerElement(const XDomAttr* self);
const XString* XDomAttr_value(const XDomAttr* self);
void XDomAttr_setValue(XDomAttr* self, const XString* value);
void XDomAttr_setValue_utf8(XDomAttr* self, const char* value);

/* QDomCharacterData / QDomText / QDomCDATASection / QDomComment */
XString* XDomCharacterData_substringData(const XDomCharacterData* self, uint64_t offset, uint64_t count);
void XDomCharacterData_appendData(XDomCharacterData* self, const XString* value);
void XDomCharacterData_appendData_utf8(XDomCharacterData* self, const char* value);
void XDomCharacterData_insertData(XDomCharacterData* self, uint64_t offset, const XString* value);
void XDomCharacterData_deleteData(XDomCharacterData* self, uint64_t offset, uint64_t count);
void XDomCharacterData_replaceData(XDomCharacterData* self, uint64_t offset, uint64_t count, const XString* value);
int XDomCharacterData_length(const XDomCharacterData* self);
const XString* XDomCharacterData_data(const XDomCharacterData* self);
void XDomCharacterData_setData(XDomCharacterData* self, const XString* value);
void XDomCharacterData_setData_utf8(XDomCharacterData* self, const char* value);
XDomText* XDomText_splitText(XDomText* self, int offset);

/* QDomEntity / QDomNotation */
const XString* XDomEntity_publicId(const XDomEntity* self);
const XString* XDomEntity_systemId(const XDomEntity* self);
const XString* XDomEntity_notationName(const XDomEntity* self);
const XString* XDomNotation_publicId(const XDomNotation* self);
const XString* XDomNotation_systemId(const XDomNotation* self);

/* QDomProcessingInstruction / QDomEntityReference / QDomDocumentFragment */
const XString* XDomProcessingInstruction_target(const XDomProcessingInstruction* self);
const XString* XDomProcessingInstruction_data(const XDomProcessingInstruction* self);
void XDomProcessingInstruction_setData(XDomProcessingInstruction* self, const XString* value);
void XDomProcessingInstruction_setData_utf8(XDomProcessingInstruction* self, const char* value);

/* QDomImplementation */
bool XDomImplementation_hasFeature(const XDomImplementation* self, const XString* feature, const XString* version);
bool XDomImplementation_hasFeature_utf8(const XDomImplementation* self, const char* feature, const char* version);
XDomDocumentType* XDomImplementation_createDocumentType(const XDomImplementation* self, const XString* qualifiedName, const XString* publicId, const XString* systemId);
XDomDocumentType* XDomImplementation_createDocumentType_utf8(const XDomImplementation* self, const char* qualifiedName, const char* publicId, const char* systemId);
XDomDocument* XDomImplementation_createDocument(const XDomImplementation* self, const XString* namespaceURI, const XString* qualifiedName, const XDomDocumentType* doctype);
XDomDocument* XDomImplementation_createDocument_utf8(const XDomImplementation* self, const char* namespaceURI, const char* qualifiedName, const XDomDocumentType* doctype);
bool XDomImplementation_isNull(const XDomImplementation* self);
XDomInvalidDataPolicy XDomImplementation_invalidDataPolicy(void);
void XDomImplementation_setInvalidDataPolicy(XDomInvalidDataPolicy policy);

/* 便于在不显式构造 XDomNode 的场景下使用 Qt 风格的类型转换。 */
XDomNode* XDomElement_toNode(const XDomElement* self);
XDomNode* XDomAttr_toNode(const XDomAttr* self);
XDomNode* XDomText_toNode(const XDomText* self);
XDomNode* XDomCDATASection_toNode(const XDomCDATASection* self);
XDomNode* XDomComment_toNode(const XDomComment* self);
XDomNode* XDomDocument_toNode(const XDomDocument* self);
XDomNode* XDomDocumentType_toNode(const XDomDocumentType* self);
XDomNode* XDomDocumentFragment_toNode(const XDomDocumentFragment* self);
XDomNode* XDomEntity_toNode(const XDomEntity* self);
XDomNode* XDomNotation_toNode(const XDomNotation* self);

#ifdef __cplusplus
}
#endif

#endif
