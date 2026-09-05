/**
 * @file       XDom.h
 * @brief      XML DOM 文档对象模型公开 API。
 * @details      对齐 Qt 6.8 的 QDomNode、QDomDocument、QDomElement 及其相关类型。
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
 * @details      各句柄的第一个成员都是 XClass；DOM 节点数据通过隐式共享私有节点保存。
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
 * @details      该类型只在实现文件中定义；调用者不能访问、释放或修改它。
 */
typedef struct XDomNodePrivate XDomNodePrivate;
typedef struct XXmlStreamReader XXmlStreamReader;
typedef struct XIODevice XIODevice;

/**
 * @brief      DOM 节点类型。
 * @details      数值与 W3C DOM 节点类型保持兼容；CharacterDataNode 是 XinYueC
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
    XDom_BaseNode = 21,                   /**< 基础节点句柄本身，不对应 XML 文档节点。 */
    XDom_CharacterDataNode = 22          /**< 字符数据基类标识，仅用于类型判断。 */
} XDomNodeType;

/**
 * @brief      DOM 字节序列编码策略。
 * @details      DOM 内部字符串始终为 UTF-16；该枚举只决定序列化为 XByteArray 时使用的声明策略。
 */
typedef enum XDomEncodingPolicy {
    XDom_EncodingFromDocument = 1,      /**< 根据文档声明输出编码。 */
    XDom_EncodingFromTextStream = 2     /**< 按文本流约定输出 UTF-8。 */
} XDomEncodingPolicy;

/**
 * @brief      setContent 的解析选项。
 * @details      选项可以按位组合；未设置选项时使用 Qt 风格的默认解析行为。
 */
typedef enum XDomParseOption {
    XDom_ParseDefault = 0x00,                  /**< 不启用额外选项。 */
    XDom_UseNamespaceProcessing = 0x01,        /**< 解析命名空间、prefix 和 localName。 */
    XDom_PreserveSpacingOnlyNodes = 0x02       /**< 保留只包含空白的文本节点。 */
} XDomParseOption;

/**
 * @brief      无效 XML 字符的处理策略。
 * @details      该策略是进程级配置，不依赖平台 API；默认接受输入中的无效字符以兼容既有流读取器。
 */
typedef enum XDomInvalidDataPolicy {
    XDom_AcceptInvalidChars = 0,  /**< 保留无效字符，不因字符本身中止解析。 */
    XDom_DropInvalidChars = 1,    /**< 丢弃无法表示的字符后继续解析。 */
    XDom_ReturnNullNode = 2       /**< 解析到无效字符时返回失败结果和空文档句柄。 */
} XDomInvalidDataPolicy;

/**
 * @brief      setContent 的解析结果。
 * @details      错误消息由结构体拥有；调用 XDomParseResult_deinit 后不可继续使用。
 */
typedef struct XDomParseResult {
    XString* m_errorMessage; /**< 新分配的错误消息；成功时为 NULL，调用方负责反初始化。 */
    int64_t m_errorLine;     /**< 错误所在行号，从 1 开始；未进入读取器时为 0。 */
    int64_t m_errorColumn;   /**< 错误所在列号，从 0 开始；未进入读取器时为 0。 */
} XDomParseResult;

/**
 * @brief      DOM 基础节点句柄。
 * @details      `m_impl` 是隐式共享的内部节点引用。空句柄仍是已初始化对象，
 *             可以安全调用查询 API，但查询结果为空或返回默认值。
 */
typedef struct XDomNode {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部共享节点，仅供实现使用，调用者不得解引用。 */
} XDomNode;

/**
 * @brief      节点列表句柄；列表元素为借用的节点包装对象，越界返回空句柄。
 */
typedef struct XDomNodeList {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部列表对象，仅供实现使用。 */
} XDomNodeList;

/**
 * @brief      命名节点映射句柄；用于属性、实体或符号声明的名称查询。
 */
typedef struct XDomNamedNodeMap {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部映射对象，仅供实现使用。 */
} XDomNamedNodeMap;

/**
 * @brief      DOM 文档句柄；负责创建节点并拥有文档树根。
 */
typedef struct XDomDocument {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部文档节点，仅供实现使用。 */
} XDomDocument;

/**
 * @brief      文档类型声明句柄；保存名称、公共标识符、系统标识符和 DTD 集合。
 */
typedef struct XDomDocumentType {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部文档类型节点，仅供实现使用。 */
} XDomDocumentType;

/**
 * @brief      文档片段句柄；可作为临时节点容器批量挂接到文档树。
 */
typedef struct XDomDocumentFragment {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部文档片段节点，仅供实现使用。 */
} XDomDocumentFragment;

/**
 * @brief      字符数据基类句柄；适用于文本、CDATA 和注释节点。
 */
typedef struct XDomCharacterData {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部字符数据节点，仅供实现使用。 */
} XDomCharacterData;

/**
 * @brief      属性节点句柄；挂接到元素后 parentNode 和 ownerElement 均可查询。
 */
typedef struct XDomAttr {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部属性节点，仅供实现使用。 */
} XDomAttr;

/**
 * @brief      元素节点句柄；可持有子节点和属性。
 */
typedef struct XDomElement {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部元素节点，仅供实现使用。 */
} XDomElement;

/**
 * @brief      文本节点句柄；索引按 UTF-16 代码单元计算。
 */
typedef struct XDomText {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部文本节点，仅供实现使用。 */
} XDomText;

/**
 * @brief      注释节点句柄；数据不包含 XML 注释标记。
 */
typedef struct XDomComment {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部注释节点，仅供实现使用。 */
} XDomComment;

/**
 * @brief      CDATA 节点句柄；数据不包含 `<![CDATA[` 和 `]]>` 标记。
 */
typedef struct XDomCDATASection {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部 CDATA 节点，仅供实现使用。 */
} XDomCDATASection;

/**
 * @brief      实体引用节点句柄；保存实体名称并可持有替换内容。
 */
typedef struct XDomEntityReference {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部实体引用节点，仅供实现使用。 */
} XDomEntityReference;

/**
 * @brief      处理指令节点句柄；保存 target 和 data。
 */
typedef struct XDomProcessingInstruction {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部处理指令节点，仅供实现使用。 */
} XDomProcessingInstruction;

/**
 * @brief      实体声明句柄；仅可通过文档类型节点的实体映射访问。
 */
typedef struct XDomEntity {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部实体节点，仅供实现使用。 */
} XDomEntity;

/**
 * @brief      符号声明句柄；仅可通过文档类型节点的符号映射访问。
 */
typedef struct XDomNotation {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDomNodePrivate* m_impl;     /**< 内部符号节点，仅供实现使用。 */
} XDomNotation;

/**
 * @brief      DOM 实现能力句柄。
 * @details      该对象不拥有文档；它只描述实现能力并创建文档类型或新文档。
 */
typedef struct XDomImplementation {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    bool m_isNull;                  /**< 是否为空实现；文档实现句柄由 XDomDocument_implementation 创建。 */
    XDomNodePrivate* m_document;    /**< 所属文档的共享引用；NULL 表示独立实现对象。 */
} XDomImplementation;

/**
 * @brief      初始化各 DOM 类型的虚函数表。
 * @return      进程内共享的虚函数表指针；分配失败时返回 NULL。
 * @note      调用者不拥有返回值，不得释放或修改虚函数表。
 */
XVtable* XDomNode_class_init(void);
/**
 * @brief      初始化 XDomNodeList 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomNodeList_class_init(void);
/**
 * @brief      初始化 XDomNamedNodeMap 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomNamedNodeMap_class_init(void);
/**
 * @brief      初始化 XDomDocument 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomDocument_class_init(void);
/**
 * @brief      初始化 XDomDocumentType 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomDocumentType_class_init(void);
/**
 * @brief      初始化 XDomDocumentFragment 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomDocumentFragment_class_init(void);
/**
 * @brief      初始化 XDomCharacterData 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomCharacterData_class_init(void);
/**
 * @brief      初始化 XDomAttr 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomAttr_class_init(void);
/**
 * @brief      初始化 XDomElement 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomElement_class_init(void);
/**
 * @brief      初始化 XDomText 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomText_class_init(void);
/**
 * @brief      初始化 XDomComment 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomComment_class_init(void);
/**
 * @brief      初始化 XDomCDATASection 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomCDATASection_class_init(void);
/**
 * @brief      初始化 XDomEntityReference 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomEntityReference_class_init(void);
/**
 * @brief      初始化 XDomProcessingInstruction 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomProcessingInstruction_class_init(void);
/**
 * @brief      初始化 XDomEntity 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomEntity_class_init(void);
/**
 * @brief      初始化 XDomNotation 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomNotation_class_init(void);
/**
 * @brief      初始化 XDomImplementation 虚函数表。
 * @return      共享虚函数表指针，失败返回 NULL。
 */
XVtable* XDomImplementation_class_init(void);

/**
 * @brief      声明 DOM 类型的标准生命周期 API。
 * @details      `XCopy` 和 `XMove` 会先检查目标、源和目标 vtable；目标未初始化时
 *             自动调用完整的 Type_init。移动成功后源对象仍可反初始化，但内部句柄为空。
 * @param      Type DOM 类型名称；该宏只在头文件内部用于生成声明。
 * @param      self init、deinit_base 和 delete_base 操作的目标句柄；可为 NULL 时按对应 API 规则处理。
 * @param      other create_copy 或 create_move 的源句柄；调用期间只借用，移动后源句柄为空。
 * @return      create、create_copy 和 create_move 返回新堆句柄，失败返回 NULL；其余生命周期
 *             API 无返回值，class_init 返回共享且不由调用者释放的虚函数表。
 * @note      堆对象必须使用对应 Type_delete_base；栈对象必须成对调用 Type_init 和 Type_deinit_base。
 */
#define XDOM_DECLARE_LIFECYCLE(Type) \
    void Type##_init(Type* self); \
    Type* Type##_create(void); \
    Type* Type##_create_copy(const Type* other); \
    Type* Type##_create_move(Type* other); \
    void Type##_deinit_base(Type* self); \
    void Type##_delete_base(Type* self)

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

XDomDocument* XDomDocument_create_ex(XMemoryType memory);
XDomImplementation* XDomImplementation_create_ex(XMemoryType memory);

/**
 * @brief      QDomNode 节点树和节点查询 API。
 * @param      self 目标节点；允许为空或空句柄，查询函数会返回默认值。
 * @param      newChild、oldChild、refChild 参与树操作的节点；输入句柄只借用，
 *             返回的新句柄由调用者使用对应的 *_delete_base 释放。
 * @return      查询函数返回的新句柄或只读字符串均由本模块管理；新句柄由调用者释放，
 *             字符串在所属节点有效期间保持有效。树操作失败时返回空句柄。
 * @note      节点包装是隐式共享的，复制句柄不会复制树；需要独立树时使用 cloneNode。
 */
/**
 * @brief      在参考子节点前插入子节点。
 * @param      self 父节点；必须允许拥有子节点。
 * @param      newChild 要插入的节点；只借用，不取得所有权。
 * @param      refChild 参考子节点；NULL 表示插入到第一个子节点之前。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；失败返回空句柄。
 * @note      对齐 `QDomNode::insertBefore`，输入节点与返回句柄共享底层节点。
 */
XDomNode* XDomNode_insertBefore(XDomNode* self, const XDomNode* newChild, const XDomNode* refChild);
/**
 * @brief      在参考子节点后插入子节点。
 * @param      self 父节点；必须允许拥有子节点。
 * @param      newChild 要插入的节点；只借用，不取得所有权。
 * @param      refChild 参考子节点；NULL 表示追加到末尾。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；失败返回空句柄。
 * @note      对齐 XinYueC 的扩展 API，等价于在参考节点后执行插入。
 */
XDomNode* XDomNode_insertAfter(XDomNode* self, const XDomNode* newChild, const XDomNode* refChild);
/**
 * @brief      用新节点替换旧子节点。
 * @param      self 旧节点的父节点；必须是有效节点。
 * @param      newChild 替换节点；只借用，不取得所有权。
 * @param      oldChild self 的直接子节点；只借用。
 * @return      被替换节点句柄，调用者使用 XDomNode_delete_base 释放；失败返回空句柄。
 * @note      对齐 `QDomNode::replaceChild`，参数非法时不修改原树。
 */
XDomNode* XDomNode_replaceChild(XDomNode* self, const XDomNode* newChild, const XDomNode* oldChild);
/**
 * @brief      移除直接子节点。
 * @param      self 父节点；必须是有效节点。
 * @param      oldChild self 的直接子节点；只借用。
 * @return      被移除节点句柄，调用者使用 XDomNode_delete_base 释放；失败返回空句柄。
 * @note      对齐 `QDomNode::removeChild`，参数非法时不修改原树。
 */
XDomNode* XDomNode_removeChild(XDomNode* self, const XDomNode* oldChild);
/**
 * @brief      将节点追加到子节点末尾。
 * @param      self 父节点；必须允许拥有子节点。
 * @param      newChild 要追加的节点；只借用，不取得所有权。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；失败返回空句柄。
 * @note      对齐 `QDomNode::appendChild`，参数非法时不修改原树。
 */
XDomNode* XDomNode_appendChild(XDomNode* self, const XDomNode* newChild);
/**
 * @brief      判断节点是否有直接子节点。
 * @param      self 目标节点；可为 NULL。
 * @return      至少有一个直接子节点返回 true，否则返回 false。
 */
bool XDomNode_hasChildNodes(const XDomNode* self);
/**
 * @brief      克隆节点。
 * @param      self 要克隆的节点；只借用，不取得所有权。
 * @param      deep true 时递归复制子树，false 时只复制当前节点及属性。
 * @return      独立的新节点句柄，调用者使用 XDomNode_delete_base 释放；失败返回空句柄。
 * @note      对齐 `QDomNode::cloneNode`，克隆结果不与源节点共享树结构。
 */
XDomNode* XDomNode_cloneNode(const XDomNode* self, bool deep);
/**
 * @brief      合并当前层相邻的文本节点。
 * @param      self 要规范化的节点；可为 NULL。
 * @return      无；参数非法时保持原节点不变。
 * @note      对齐 `QDomNode::normalize`，只处理当前层，不递归进入子元素。
 */
void XDomNode_normalize(XDomNode* self);
/**
 * @brief      查询 DOM 能力支持情况。
 * @param      self 目标节点；可为 NULL。
 * @param      feature 能力名称；只借用，不取得所有权。
 * @param      version 能力版本；空字符串表示不限版本，只借用。
 * @return      支持时返回 true，否则返回 false。
 */
bool XDomNode_isSupported(const XDomNode* self, const XString* feature, const XString* version);
/**
 * @brief      查询 DOM 能力支持情况的 UTF-8 版本。
 * @param      self 目标节点；可为 NULL。
 * @param      feature UTF-8 能力名称；只借用，不取得所有权。
 * @param      version UTF-8 能力版本；可为 NULL，表示不限版本。
 * @return      支持时返回 true，否则返回 false。
 * @note      输入按 UTF-8 解码后执行与 XDomNode_isSupported 相同的判断。
 */
bool XDomNode_isSupported_utf8(const XDomNode* self, const char* feature, const char* version);
/**
 * @brief      获取节点名称。
 * @param      self 目标节点；可为 NULL。
 * @return      节点内部只读 XString 指针；不需要释放，所属节点失效后不可继续使用。
 */
const XString* XDomNode_nodeName(const XDomNode* self);
/**
 * @brief      获取节点类型。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型；NULL 或空句柄返回 XDom_UnknownNode。
 */
XDomNodeType XDomNode_nodeType(const XDomNode* self);
/**
 * @brief      获取父节点。
 * @param      self 目标节点；可为 NULL。
 * @return      新父节点句柄，调用者使用 XDomNode_delete_base 释放；无父节点时为空。
 */
XDomNode* XDomNode_parentNode(const XDomNode* self);
/**
 * @brief      获取实时子节点列表。
 * @param      self 目标节点；可为 NULL。
 * @return      新列表句柄，调用者使用 XDomNodeList_delete_base 释放；失败返回空句柄。
 * @note      列表会反映所属树后续的节点变化。
 */
XDomNodeList* XDomNode_childNodes(const XDomNode* self);
/**
 * @brief      获取第一个直接子节点。
 * @param      self 目标节点；可为 NULL。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；没有子节点时为空。
 */
XDomNode* XDomNode_firstChild(const XDomNode* self);
/**
 * @brief      获取最后一个直接子节点。
 * @param      self 目标节点；可为 NULL。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；没有子节点时为空。
 */
XDomNode* XDomNode_lastChild(const XDomNode* self);
/**
 * @brief      获取前一个兄弟节点。
 * @param      self 目标节点；可为 NULL。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；没有兄弟节点时为空。
 */
XDomNode* XDomNode_previousSibling(const XDomNode* self);
/**
 * @brief      获取后一个兄弟节点。
 * @param      self 目标节点；可为 NULL。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；没有兄弟节点时为空。
 */
XDomNode* XDomNode_nextSibling(const XDomNode* self);
/**
 * @brief      获取节点属性映射。
 * @param      self 目标节点；元素节点的映射可修改，其他节点返回空映射。
 * @return      新映射句柄，调用者使用 XDomNamedNodeMap_delete_base 释放。
 */
XDomNamedNodeMap* XDomNode_attributes(const XDomNode* self);
/**
 * @brief      获取所属文档。
 * @param      self 目标节点；可为 NULL。
 * @return      新文档句柄，调用者使用 XDomDocument_delete_base 释放；没有所属文档时为空。
 */
XDomDocument* XDomNode_ownerDocument(const XDomNode* self);
/**
 * @brief      获取命名空间 URI。
 * @param      self 目标节点；可为 NULL。
 * @return      节点内部只读 XString 指针；不需要释放，所属节点失效后不可继续使用。
 */
const XString* XDomNode_namespaceURI(const XDomNode* self);
/**
 * @brief      获取命名空间本地名。
 * @param      self 目标节点；可为 NULL。
 * @return      节点内部只读 XString 指针；不需要释放，无命名空间时返回空字符串。
 */
const XString* XDomNode_localName(const XDomNode* self);
/**
 * @brief      判断节点是否有属性。
 * @param      self 目标节点；可为 NULL。
 * @return      节点含有至少一个属性时返回 true，否则返回 false。
 */
bool XDomNode_hasAttributes(const XDomNode* self);
/**
 * @brief      获取节点值。
 * @param      self 目标节点；可为 NULL。
 * @return      节点内部只读 XString 指针；不需要释放，所属节点失效后不可继续使用。
 */
const XString* XDomNode_nodeValue(const XDomNode* self);
/**
 * @brief      设置节点值。
 * @param      self 目标节点；不支持节点类型的调用不会修改原节点。
 * @param      value 新值；只借用，函数内部复制，可为 NULL。
 * @return      无；参数非法或节点类型不支持时保持原节点不变。
 */
void XDomNode_setNodeValue(XDomNode* self, const XString* value);
/**
 * @brief      设置节点值的 UTF-8 版本。
 * @param      self 目标节点；不支持节点类型的调用不会修改原节点。
 * @param      value UTF-8 新值；只借用，可为 NULL。
 * @return      无；参数非法时保持原节点不变。
 */
void XDomNode_setNodeValue_utf8(XDomNode* self, const char* value);
/**
 * @brief      获取命名空间前缀。
 * @param      self 目标节点；可为 NULL。
 * @return      节点内部只读前缀；不需要释放，所属节点失效后不可继续使用。
 */
const XString* XDomNode_prefix(const XDomNode* self);
/**
 * @brief      设置命名空间前缀。
 * @param      self 命名空间节点；必须由命名空间 API 创建。
 * @param      prefix 新前缀；只借用并复制，可为 NULL。
 * @return      无；参数非法时保持原节点不变。
 */
void XDomNode_setPrefix(XDomNode* self, const XString* prefix);
/**
 * @brief      设置命名空间前缀的 UTF-8 版本。
 * @param      self 命名空间节点；必须由命名空间 API 创建。
 * @param      prefix UTF-8 新前缀；只借用，可为 NULL。
 * @return      无；输入按 UTF-8 解码，参数非法时保持原节点不变。
 */
void XDomNode_setPrefix_utf8(XDomNode* self, const char* prefix);
/**
 * @brief      按名称查找节点。
 * @param      self 映射或元素节点；可为 NULL。
 * @param      name 节点名称；只借用，不取得所有权。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；找不到时为空。
 */
XDomNode* XDomNode_namedItem(const XDomNode* self, const XString* name);
/**
 * @brief      按名称查找节点的 UTF-8 版本。
 * @param      self 映射或元素节点；可为 NULL。
 * @param      name UTF-8 节点名称；只借用。
 * @return      新节点句柄，调用者使用 XDomNode_delete_base 释放；找不到时为空。
 */
XDomNode* XDomNode_namedItem_utf8(const XDomNode* self, const char* name);
/**
 * @brief      判断节点句柄是否为空。
 * @param      self 目标节点；NULL 表示调用者没有提供对象。
 * @return      NULL 或内部节点为空返回 true，否则返回 false。
 */
bool XDomNode_isNull(const XDomNode* self);
/**
 * @brief      清空当前节点句柄。
 * @param      self 要清空的句柄；只改变当前句柄，不释放共享树。
 * @return      无；其他共享句柄和底层节点保持不变。
 */
void XDomNode_clear(XDomNode* self);
/**
 * @brief      判断是否为属性节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_AttributeNode 时返回 true，否则返回 false。
 */
bool XDomNode_isAttr(const XDomNode* self);
/**
 * @brief      判断是否为 CDATA 节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_CDATASectionNode 时返回 true，否则返回 false。
 */
bool XDomNode_isCDATASection(const XDomNode* self);
/**
 * @brief      判断是否为文档片段。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_DocumentFragmentNode 时返回 true，否则返回 false。
 */
bool XDomNode_isDocumentFragment(const XDomNode* self);
/**
 * @brief      判断是否为文档节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_DocumentNode 时返回 true，否则返回 false。
 */
bool XDomNode_isDocument(const XDomNode* self);
/**
 * @brief      判断是否为文档类型节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_DocumentTypeNode 时返回 true，否则返回 false。
 */
bool XDomNode_isDocumentType(const XDomNode* self);
/**
 * @brief      判断是否为元素节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_ElementNode 时返回 true，否则返回 false。
 */
bool XDomNode_isElement(const XDomNode* self);
/**
 * @brief      判断是否为实体引用节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_EntityReferenceNode 时返回 true，否则返回 false。
 */
bool XDomNode_isEntityReference(const XDomNode* self);
/**
 * @brief      判断是否为文本节点。
 * @param      self 目标节点；可为 NULL。
 * @return      Text 或 CDATA 节点返回 true，否则返回 false。
 */
bool XDomNode_isText(const XDomNode* self);
/**
 * @brief      判断是否为实体节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_EntityNode 时返回 true，否则返回 false。
 */
bool XDomNode_isEntity(const XDomNode* self);
/**
 * @brief      判断是否为符号声明节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_NotationNode 时返回 true，否则返回 false。
 */
bool XDomNode_isNotation(const XDomNode* self);
/**
 * @brief      判断是否为处理指令节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_ProcessingInstructionNode 时返回 true，否则返回 false。
 */
bool XDomNode_isProcessingInstruction(const XDomNode* self);
/**
 * @brief      判断是否为字符数据节点。
 * @param      self 目标节点；可为 NULL。
 * @return      Text、CDATA 或 Comment 节点返回 true，否则返回 false。
 */
bool XDomNode_isCharacterData(const XDomNode* self);
/**
 * @brief      判断是否为注释节点。
 * @param      self 目标节点；可为 NULL。
 * @return      节点类型为 XDom_CommentNode 时返回 true，否则返回 false。
 */
bool XDomNode_isComment(const XDomNode* self);
/**
 * @brief      将节点转换为元素句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；类型不符时为空。
 */
XDomElement* XDomNode_toElement(const XDomNode* self);
/**
 * @brief      将节点转换为属性句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新属性句柄，调用者使用 XDomAttr_delete_base 释放；类型不符时为空。
 */
XDomAttr* XDomNode_toAttr(const XDomNode* self);
/**
 * @brief      将节点转换为文本句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新文本句柄，调用者使用 XDomText_delete_base 释放；类型不符时为空。
 */
XDomText* XDomNode_toText(const XDomNode* self);
/**
 * @brief      将节点转换为 CDATA 句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新 CDATA 句柄，调用者使用 XDomCDATASection_delete_base 释放；类型不符时为空。
 */
XDomCDATASection* XDomNode_toCDATASection(const XDomNode* self);
/**
 * @brief      将节点转换为注释句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新注释句柄，调用者使用 XDomComment_delete_base 释放；类型不符时为空。
 */
XDomComment* XDomNode_toComment(const XDomNode* self);
/**
 * @brief      将节点转换为字符数据句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新字符数据句柄，调用者使用 XDomCharacterData_delete_base 释放；类型不符时为空。
 */
XDomCharacterData* XDomNode_toCharacterData(const XDomNode* self);
/**
 * @brief      将节点转换为文档句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新文档句柄，调用者使用 XDomDocument_delete_base 释放；类型不符时为空。
 */
XDomDocument* XDomNode_toDocument(const XDomNode* self);
/**
 * @brief      将节点转换为文档类型句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新文档类型句柄，调用者使用 XDomDocumentType_delete_base 释放；类型不符时为空。
 */
XDomDocumentType* XDomNode_toDocumentType(const XDomNode* self);
/**
 * @brief      将节点转换为文档片段句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新片段句柄，调用者使用 XDomDocumentFragment_delete_base 释放；类型不符时为空。
 */
XDomDocumentFragment* XDomNode_toDocumentFragment(const XDomNode* self);
/**
 * @brief      将节点转换为实体引用句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新实体引用句柄，调用者使用 XDomEntityReference_delete_base 释放；类型不符时为空。
 */
XDomEntityReference* XDomNode_toEntityReference(const XDomNode* self);
/**
 * @brief      将节点转换为实体声明句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新实体句柄，调用者使用 XDomEntity_delete_base 释放；类型不符时为空。
 */
XDomEntity* XDomNode_toEntity(const XDomNode* self);
/**
 * @brief      将节点转换为符号声明句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新符号句柄，调用者使用 XDomNotation_delete_base 释放；类型不符时为空。
 */
XDomNotation* XDomNode_toNotation(const XDomNode* self);
/**
 * @brief      将节点转换为处理指令句柄。
 * @param      self 目标节点；只借用，不取得所有权。
 * @return      新处理指令句柄，调用者使用 XDomProcessingInstruction_delete_base 释放；类型不符时为空。
 */
XDomProcessingInstruction* XDomNode_toProcessingInstruction(const XDomNode* self);
/**
 * @brief      获取第一个匹配的直接子元素。
 * @param      self 父节点；只借用。
 * @param      tagName 标签名；可为 NULL，表示不按标签名筛选。
 * @param      namespaceURI 命名空间 URI；可为 NULL，表示不按命名空间筛选。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；没有匹配项时为空。
 */
XDomElement* XDomNode_firstChildElement(const XDomNode* self, const XString* tagName, const XString* namespaceURI);
/**
 * @brief      获取第一个匹配的直接子元素的 UTF-8 版本。
 * @param      self 父节点；只借用。
 * @param      tagName UTF-8 标签名；可为 NULL，表示不筛选。
 * @param      namespaceURI UTF-8 命名空间 URI；可为 NULL，表示不筛选。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；没有匹配项时为空。
 */
XDomElement* XDomNode_firstChildElement_utf8(const XDomNode* self, const char* tagName, const char* namespaceURI);
/**
 * @brief      获取最后一个匹配的直接子元素。
 * @param      self 父节点；只借用。
 * @param      tagName 标签名；可为 NULL，表示不筛选。
 * @param      namespaceURI 命名空间 URI；可为 NULL，表示不筛选。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；没有匹配项时为空。
 */
XDomElement* XDomNode_lastChildElement(const XDomNode* self, const XString* tagName, const XString* namespaceURI);
/**
 * @brief      获取最后一个匹配的直接子元素的 UTF-8 版本。
 * @param      self 父节点；只借用。
 * @param      tagName UTF-8 标签名；可为 NULL，表示不筛选。
 * @param      namespaceURI UTF-8 命名空间 URI；可为 NULL，表示不筛选。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；没有匹配项时为空。
 */
XDomElement* XDomNode_lastChildElement_utf8(const XDomNode* self, const char* tagName, const char* namespaceURI);
/**
 * @brief      获取前一个匹配的兄弟元素。
 * @param      self 当前节点；只借用。
 * @param      tagName 标签名；可为 NULL，表示不筛选。
 * @param      namespaceURI 命名空间 URI；可为 NULL，表示不筛选。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；没有匹配项时为空。
 */
XDomElement* XDomNode_previousSiblingElement(const XDomNode* self, const XString* tagName, const XString* namespaceURI);
/**
 * @brief      获取前一个匹配的兄弟元素的 UTF-8 版本。
 * @param      self 当前节点；只借用。
 * @param      tagName UTF-8 标签名；可为 NULL，表示不筛选。
 * @param      namespaceURI UTF-8 命名空间 URI；可为 NULL，表示不筛选。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；没有匹配项时为空。
 */
XDomElement* XDomNode_previousSiblingElement_utf8(const XDomNode* self, const char* tagName, const char* namespaceURI);
/**
 * @brief      获取后一个匹配的兄弟元素。
 * @param      self 当前节点；只借用。
 * @param      tagName 标签名；可为 NULL，表示不筛选。
 * @param      namespaceURI 命名空间 URI；可为 NULL，表示不筛选。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；没有匹配项时为空。
 */
XDomElement* XDomNode_nextSiblingElement(const XDomNode* self, const XString* tagName, const XString* namespaceURI);
/**
 * @brief      获取后一个匹配的兄弟元素的 UTF-8 版本。
 * @param      self 当前节点；只借用。
 * @param      tagName UTF-8 标签名；可为 NULL，表示不筛选。
 * @param      namespaceURI UTF-8 命名空间 URI；可为 NULL，表示不筛选。
 * @return      新元素句柄，调用者使用 XDomElement_delete_base 释放；没有匹配项时为空。
 */
XDomElement* XDomNode_nextSiblingElement_utf8(const XDomNode* self, const char* tagName, const char* namespaceURI);
/**
 * @brief      获取节点解析行号。
 * @param      self 解析得到的节点；只借用。
 * @return      从 1 开始的源行号；未知或没有位置时返回 -1。
 */
int64_t XDomNode_lineNumber(const XDomNode* self);
/**
 * @brief      获取节点解析列号。
 * @param      self 解析得到的节点；只借用。
 * @return      从 0 开始的源列号；未知或没有位置时返回 -1。
 */
int64_t XDomNode_columnNumber(const XDomNode* self);
/**
 * @brief      将节点序列化为字符串。
 * @param      self 目标节点；只借用。
 * @param      indent 缩进宽度；小于 0 表示紧凑输出。
 * @return      新分配的 UTF-16 XString，调用者使用 XString_delete_base 释放；失败返回 NULL。
 */
XString* XDomNode_toString(const XDomNode* self, int indent);
/**
 * @brief      将节点序列化并写入设备。
 * @param      self 目标节点；只借用。
 * @param      device 可写 XIODevice；只借用，不打开、关闭或释放。
 * @param      indent 缩进宽度；小于 0 表示紧凑输出。
 * @param      encodingPolicy 编码策略；当前字节输出使用 UTF-8。
 * @return      全部数据写入成功返回 true，否则返回 false；失败时设备状态由设备决定。
 */
bool XDomNode_save(const XDomNode* self, XIODevice* device, int indent, XDomEncodingPolicy encodingPolicy);
/**
 * @brief      判断两个节点句柄是否引用同一底层节点。
 * @param      left 左节点句柄；只借用，可为 NULL。
 * @param      right 右节点句柄；只借用，可为 NULL。
 * @return      两者引用同一底层节点或均为空时返回 true，否则返回 false。
 */
bool XDomNode_equals(const XDomNode* left, const XDomNode* right);

/**
 * @brief      按索引获取实时节点列表中的节点。
 * @param      self 列表句柄；由 childNodes 或 elementsByTagName 系列函数创建。
 * @param      index 从零开始的列表索引。
 * @return      新节点句柄，调用者使用 XDomNode_delete 释放；越界或输入为空时返回 NULL。
 * @note      列表反映底层节点树的后续变化，列表本身仍需使用 XDomNodeList_delete 释放。
 */
XDomNode* XDomNodeList_item(const XDomNodeList* self, int index);
/**
 * @brief      按索引获取实时节点列表中的节点，Qt QDomNodeList::at 的兼容别名。
 * @param      self 列表句柄；只借用。
 * @param      index 从零开始的列表索引。
 * @return      新节点句柄，调用者使用 XDomNode_delete 释放；越界或输入为空时返回 NULL。
 */
XDomNode* XDomNodeList_at(const XDomNodeList* self, int index);
/**
 * @brief      获取实时节点列表的长度，对齐 Qt QDomNodeList::length。
 * @param      self 列表句柄；只借用。
 * @return      当前匹配节点数量；输入为空时返回 0。
 */
int XDomNodeList_length(const XDomNodeList* self);
/**
 * @brief      获取实时节点列表的数量，对齐 Qt QDomNodeList::count。
 * @param      self 列表句柄；只借用。
 * @return      当前匹配节点数量；输入为空时返回 0。
 */
int XDomNodeList_count(const XDomNodeList* self);
/**
 * @brief      获取实时节点列表的数量，提供容器风格的 size 别名。
 * @param      self 列表句柄；只借用。
 * @return      当前匹配节点数量；输入为空时返回 0。
 */
int XDomNodeList_size(const XDomNodeList* self);
/**
 * @brief      判断实时节点列表是否为空。
 * @param      self 列表句柄；只借用。
 * @return      列表为空或输入为空时返回 true，否则返回 false。
 */
bool XDomNodeList_isEmpty(const XDomNodeList* self);
/**
 * @brief      判断两个节点列表句柄是否引用同一实时列表。
 * @param      left 左列表句柄；只借用，可为 NULL。
 * @param      right 右列表句柄；只借用，可为 NULL。
 * @return      底层列表相同或两个句柄均为空时返回 true，否则返回 false。
 */
bool XDomNodeList_equals(const XDomNodeList* left, const XDomNodeList* right);

/**
 * @brief      按名称查找节点。
 * @param      self 映射句柄。
 * @param      name 名称，只借用。
 * @return      新节点句柄，调用者释放；找不到时为空。
 */
XDomNode* XDomNamedNodeMap_namedItem(const XDomNamedNodeMap* self, const XString* name);
/**
 * @brief      UTF-8 版本的 namedItem。
 * @param      self 映射句柄。
 * @param      name UTF-8 名称。
 * @return      新节点句柄，调用者释放；找不到时为空。
 */
XDomNode* XDomNamedNodeMap_namedItem_utf8(const XDomNamedNodeMap* self, const char* name);
/**
 * @brief      按名称插入或替换节点。
 * @param      self 映射句柄。
 * @param      newNode 新节点，只借用。
 * @return      被替换节点句柄，调用者释放；无旧节点时为空。
 */
XDomNode* XDomNamedNodeMap_setNamedItem(XDomNamedNodeMap* self, const XDomNode* newNode);
/**
 * @brief      按名称移除节点。
 * @param      self 映射句柄。
 * @param      name 名称，只借用。
 * @return      被移除节点句柄，调用者释放；失败时为空。
 */
XDomNode* XDomNamedNodeMap_removeNamedItem(XDomNamedNodeMap* self, const XString* name);
/**
 * @brief      UTF-8 版本的 removeNamedItem。
 * @param      self 映射句柄。
 * @param      name UTF-8 名称。
 * @return      被移除节点句柄，调用者释放；失败时为空。
 */
XDomNode* XDomNamedNodeMap_removeNamedItem_utf8(XDomNamedNodeMap* self, const char* name);
/**
 * @brief      按索引获取节点。
 * @param      self 映射句柄。
 * @param      index 从 0 开始的索引。
 * @return      新节点句柄，调用者释放；越界时为空。
 */
XDomNode* XDomNamedNodeMap_item(const XDomNamedNodeMap* self, int index);
/**
 * @brief      按命名空间 URI 和本地名查找节点。
 * @param      self 映射句柄。
 * @param      namespaceURI 命名空间 URI。
 * @param      localName 本地名。
 * @return      新节点句柄，调用者释放；找不到时为空。
 */
XDomNode* XDomNamedNodeMap_namedItemNS(const XDomNamedNodeMap* self, const XString* namespaceURI, const XString* localName);
/**
 * @brief      UTF-8 版本的 namedItemNS。
 * @param      self 映射句柄。
 * @param      namespaceURI UTF-8 URI。
 * @param      localName UTF-8 本地名。
 * @return      新节点句柄，调用者释放；找不到时为空。
 */
XDomNode* XDomNamedNodeMap_namedItemNS_utf8(const XDomNamedNodeMap* self, const char* namespaceURI, const char* localName);
/**
 * @brief      按命名空间插入或替换节点。
 * @param      self 映射句柄。
 * @param      newNode 新节点，只借用。
 * @return      被替换节点句柄，调用者释放；无旧节点时为空。
 */
XDomNode* XDomNamedNodeMap_setNamedItemNS(XDomNamedNodeMap* self, const XDomNode* newNode);
/**
 * @brief      按命名空间移除节点。
 * @param      self 映射句柄。
 * @param      namespaceURI 命名空间 URI。
 * @param      localName 本地名。
 * @return      被移除节点句柄，调用者释放；失败时为空。
 */
XDomNode* XDomNamedNodeMap_removeNamedItemNS(XDomNamedNodeMap* self, const XString* namespaceURI, const XString* localName);
/**
 * @brief      获取映射长度。
 * @param      self 映射句柄。
 * @return      当前节点数量。
 */
int XDomNamedNodeMap_length(const XDomNamedNodeMap* self);
/**
 * @brief      length 的 Qt 兼容别名。
 * @param      self 映射句柄。
 * @return      当前节点数量。
 */
int XDomNamedNodeMap_count(const XDomNamedNodeMap* self);
/**
 * @brief      length 的容器风格别名。
 * @param      self 映射句柄。
 * @return      当前节点数量。
 */
int XDomNamedNodeMap_size(const XDomNamedNodeMap* self);
/**
 * @brief      判断映射是否为空。
 * @param      self 映射句柄。
 * @return      没有节点或句柄为空返回 true。
 */
bool XDomNamedNodeMap_isEmpty(const XDomNamedNodeMap* self);
/**
 * @brief      判断名称是否存在。
 * @param      self 映射句柄。
 * @param      name 名称，只借用。
 * @return      存在返回 true。
 */
bool XDomNamedNodeMap_contains(const XDomNamedNodeMap* self, const XString* name);
/**
 * @brief      UTF-8 版本的 contains。
 * @param      self 映射句柄。
 * @param      name UTF-8 名称。
 * @return      存在返回 true。
 */
bool XDomNamedNodeMap_contains_utf8(const XDomNamedNodeMap* self, const char* name);
/**
 * @brief      判断两个映射句柄是否引用同一映射。
 * @param      left 左句柄。
 * @param      right 右句柄。
 * @return      底层映射相同或均为空返回 true。
 */
bool XDomNamedNodeMap_equals(const XDomNamedNodeMap* left, const XDomNamedNodeMap* right);

/**
 * @brief      QDomDocument 文档和节点工厂 API。
 * @param      self 目标文档；创建函数返回的节点属于该文档，输入节点仅借用。
 * @param      data/data UTF-8 输入 XML；setContent 会替换原文档树，错误结果中的消息由
 *             XDomParseResult_deinit 释放。
 * @return      工厂和导入函数返回的新句柄由调用者释放；字符串序列化结果也由调用者释放。
 * @note      一个文档最多包含一个文档类型节点和一个根元素；setContent 不调用平台 API。
 */
/**
 * @brief      创建带名称的文档。
 * @param      name 文档名称，可为 NULL，只借用。
 * @return      新文档句柄，调用者释放。
 */
XDomDocument* XDomDocument_createName(const XString* name);
/**
 * @brief      UTF-8 版本的 createName。
 * @param      name UTF-8 文档名称，可为 NULL。
 * @return      新文档句柄，调用者释放。
 */
XDomDocument* XDomDocument_createName_utf8(const char* name);
/**
 * @brief      按文档类型构造文档。
 * @param      doctype 文档类型，只借用；NULL 使用空文档类型。
 * @return      新文档句柄，由调用者释放。
 */
XDomDocument* XDomDocument_createDoctype(const XDomDocumentType* doctype);
/**
 * @brief      获取文档类型。
 * @param      self 目标文档。
 * @return      新文档类型句柄，调用者释放；没有时为空。
 */
XDomDocumentType* XDomDocument_doctype(const XDomDocument* self);
/**
 * @brief      获取 DOM 实现对象。
 * @param      self 目标文档。
 * @return      新实现句柄，调用者释放。
 */
XDomImplementation* XDomDocument_implementation(const XDomDocument* self);
/**
 * @brief      获取文档根元素。
 * @param      self 目标文档。
 * @return      新元素句柄，调用者释放；没有根元素时为空。
 */
XDomElement* XDomDocument_documentElement(const XDomDocument* self);
/**
 * @brief      创建普通元素。
 * @param      self 所属文档。
 * @param      tagName XML 标签名，只借用并复制。
 * @return      新元素句柄，调用者释放。
 */
XDomElement* XDomDocument_createElement(XDomDocument* self, const XString* tagName);
/**
 * @brief      UTF-8 版本的 createElement。
 * @param      self 所属文档。
 * @param      tagName UTF-8 标签名。
 * @return      新元素句柄，调用者释放。
 */
XDomElement* XDomDocument_createElement_utf8(XDomDocument* self, const char* tagName);
/**
 * @brief      创建文档片段。
 * @param      self 所属文档。
 * @return      新片段句柄，调用者释放。
 */
XDomDocumentFragment* XDomDocument_createDocumentFragment(XDomDocument* self);
/**
 * @brief      创建文本节点。
 * @param      self 所属文档。
 * @param      data 文本数据，只借用并复制。
 * @return      新文本句柄，调用者释放。
 */
XDomText* XDomDocument_createTextNode(XDomDocument* self, const XString* data);
/**
 * @brief      UTF-8 版本的 createTextNode。
 * @param      self 所属文档。
 * @param      data UTF-8 文本数据。
 * @return      新文本句柄，调用者释放。
 */
XDomText* XDomDocument_createTextNode_utf8(XDomDocument* self, const char* data);
/**
 * @brief      创建注释节点。
 * @param      self 所属文档。
 * @param      data 注释数据，不含标记，只借用并复制。
 * @return      新注释句柄，调用者释放。
 */
XDomComment* XDomDocument_createComment(XDomDocument* self, const XString* data);
/**
 * @brief      UTF-8 版本的 createComment。
 * @param      self 所属文档。
 * @param      data UTF-8 注释数据。
 * @return      新注释句柄，调用者释放。
 */
XDomComment* XDomDocument_createComment_utf8(XDomDocument* self, const char* data);
/**
 * @brief      创建 CDATA 节点。
 * @param      self 所属文档。
 * @param      data CDATA 数据，不含标记，只借用并复制。
 * @return      新 CDATA 句柄，调用者释放。
 */
XDomCDATASection* XDomDocument_createCDATASection(XDomDocument* self, const XString* data);
/**
 * @brief      UTF-8 版本的 createCDATASection。
 * @param      self 所属文档。
 * @param      data UTF-8 CDATA 数据。
 * @return      新句柄，调用者释放。
 */
XDomCDATASection* XDomDocument_createCDATASection_utf8(XDomDocument* self, const char* data);
/**
 * @brief      创建处理指令。
 * @param      self 所属文档。
 * @param      target 指令目标，只借用并复制。
 * @param      data 指令数据，只借用并复制。
 * @return      新句柄，调用者释放。
 */
XDomProcessingInstruction* XDomDocument_createProcessingInstruction(XDomDocument* self, const XString* target, const XString* data);
/**
 * @brief      UTF-8 版本的 createProcessingInstruction。
 * @param      self 所属文档。
 * @param      target UTF-8 目标。
 * @param      data UTF-8 数据。
 * @return      新句柄，调用者释放。
 */
XDomProcessingInstruction* XDomDocument_createProcessingInstruction_utf8(XDomDocument* self, const char* target, const char* data);
/**
 * @brief      创建属性节点。
 * @param      self 所属文档。
 * @param      name 属性名称，只借用并复制。
 * @return      新属性句柄，调用者释放。
 */
XDomAttr* XDomDocument_createAttribute(XDomDocument* self, const XString* name);
/**
 * @brief      UTF-8 版本的 createAttribute。
 * @param      self 所属文档。
 * @param      name UTF-8 属性名称。
 * @return      新属性句柄，调用者释放。
 */
XDomAttr* XDomDocument_createAttribute_utf8(XDomDocument* self, const char* name);
/**
 * @brief      创建实体引用节点。
 * @param      self 所属文档。
 * @param      name 实体名称，只借用并复制。
 * @return      新句柄，调用者释放。
 */
XDomEntityReference* XDomDocument_createEntityReference(XDomDocument* self, const XString* name);
/**
 * @brief      UTF-8 版本的 createEntityReference。
 * @param      self 所属文档。
 * @param      name UTF-8 实体名称。
 * @return      新句柄，调用者释放。
 */
XDomEntityReference* XDomDocument_createEntityReference_utf8(XDomDocument* self, const char* name);
/**
 * @brief      创建按标签名查询的实时列表。
 * @param      self 文档。
 * @param      tagName 标签名，NULL 或空字符串匹配全部元素。
 * @return      新列表句柄，调用者释放。
 */
XDomNodeList* XDomDocument_elementsByTagName(const XDomDocument* self, const XString* tagName);
/**
 * @brief      UTF-8 版本的 elementsByTagName。
 * @param      self 文档。
 * @param      tagName UTF-8 标签名。
 * @return      新列表句柄，调用者释放。
 */
XDomNodeList* XDomDocument_elementsByTagName_utf8(const XDomDocument* self, const char* tagName);
/**
 * @brief      将节点导入本文档。
 * @param      self 目标文档。
 * @param      importedNode 源节点，只借用。
 * @param      deep 是否递归复制子树。
 * @return      新节点句柄，调用者释放。
 */
XDomNode* XDomDocument_importNode(XDomDocument* self, const XDomNode* importedNode, bool deep);
/**
 * @brief      创建命名空间元素。
 * @param      self 文档。
 * @param      namespaceURI 命名空间 URI，可为 NULL。
 * @param      qualifiedName 限定名。
 * @return      新元素句柄，调用者释放。
 */
XDomElement* XDomDocument_createElementNS(XDomDocument* self, const XString* namespaceURI, const XString* qualifiedName);
/**
 * @brief      UTF-8 版本的 createElementNS。
 * @param      self 文档。
 * @param      namespaceURI UTF-8 URI，可为 NULL。
 * @param      qualifiedName UTF-8 限定名。
 * @return      新句柄，调用者释放。
 */
XDomElement* XDomDocument_createElementNS_utf8(XDomDocument* self, const char* namespaceURI, const char* qualifiedName);
/**
 * @brief      创建命名空间属性。
 * @param      self 文档。
 * @param      namespaceURI 命名空间 URI，可为 NULL。
 * @param      qualifiedName 限定名。
 * @return      新属性句柄，调用者释放。
 */
XDomAttr* XDomDocument_createAttributeNS(XDomDocument* self, const XString* namespaceURI, const XString* qualifiedName);
/**
 * @brief      UTF-8 版本的 createAttributeNS。
 * @param      self 文档。
 * @param      namespaceURI UTF-8 URI，可为 NULL。
 * @param      qualifiedName UTF-8 限定名。
 * @return      新句柄，调用者释放。
 */
XDomAttr* XDomDocument_createAttributeNS_utf8(XDomDocument* self, const char* namespaceURI, const char* qualifiedName);
/**
 * @brief      创建按命名空间和本地名查询的实时列表。
 * @param      self 文档。
 * @param      namespaceURI URI。
 * @param      localName 本地名。
 * @return      新列表句柄，调用者释放。
 */
XDomNodeList* XDomDocument_elementsByTagNameNS(const XDomDocument* self, const XString* namespaceURI, const XString* localName);
/**
 * @brief      UTF-8 版本的 elementsByTagNameNS。
 * @param      self 文档。
 * @param      namespaceURI UTF-8 URI。
 * @param      localName UTF-8 本地名。
 * @return      新列表句柄，调用者释放。
 */
XDomNodeList* XDomDocument_elementsByTagNameNS_utf8(const XDomDocument* self, const char* namespaceURI, const char* localName);
/**
 * @brief      按 id 属性查找元素。
 * @param      self 文档。
 * @param      id id 值，只借用。
 * @return      新元素句柄，调用者释放；找不到时为空。
 */
XDomElement* XDomDocument_elementById(const XDomDocument* self, const XString* id);
/**
 * @brief      UTF-8 版本的 elementById。
 * @param      self 文档。
 * @param      id UTF-8 id 值。
 * @return      新元素句柄，调用者释放；找不到时为空。
 */
XDomElement* XDomDocument_elementById_utf8(const XDomDocument* self, const char* id);
/**
 * @brief      使用字节数组解析文档。
 * @param      self 目标文档。
 * @param      data XML 字节输入，只借用。
 * @param      options 解析选项位组合。
 * @return      结构化解析结果，须调用 deinit。
 */
XDomParseResult XDomDocument_setContent_result(XDomDocument* self, const XByteArray* data, unsigned int options);
/**
 * @brief      使用 UTF-8 字符串解析文档。
 * @param      self 目标文档。
 * @param      data UTF-8 输入，只借用。
 * @param      options 解析选项位组合。
 * @return      结构化解析结果，须调用 deinit。
 */
XDomParseResult XDomDocument_setContent_utf8_result(XDomDocument* self, const char* data, unsigned int options);
/**
 * @brief      从 UTF-16 XString 设置文档内容。
 * @param      self 目标文档，原文档树会在解析前清空。
 * @param      data UTF-16 XML 文本，只在调用期间借用；NULL 表示输入为空。
 * @param      options XDomParseOption 位组合。
 * @return      解析结果中的错误消息由 XDomParseResult_deinit 释放。
 * @note      XString 文本直接通过 Reader 的 UTF-16 输入接口追加，不调用平台 API。
 */
XDomParseResult XDomDocument_setContent_string_result(XDomDocument* self, const XString* data,
                                                       unsigned int options);
/**
 * @brief      从已有 XXmlStreamReader 设置文档内容。
 * @param      self 目标文档，原文档树会在解析前清空。
 * @param      reader 已初始化的 Reader；输入句柄只借用，解析过程会推进其状态。
 * @param      options XDomParseOption 位组合。
 * @return      解析结果；错误消息由 XDomParseResult_deinit 释放。
 * @note      Reader 的 XML 声明、编码、DTD 和错误位置由 Reader 提供，调用者仍负责释放 Reader。
 */
XDomParseResult XDomDocument_setContent_reader_result(XDomDocument* self, XXmlStreamReader* reader,
                                                       unsigned int options);
/**
 * @brief      从已有 XIODevice 设置文档内容。
 * @param      self 目标文档，原文档树会在解析前清空。
 * @param      device 可读设备；输入句柄只借用，不由 DOM 打开、关闭或释放。
 * @param      options XDomParseOption 位组合。
 * @return      解析结果；错误消息由 XDomParseResult_deinit 释放。
 * @note      设备读取完全通过 XIODevice 抽象完成，不使用文件描述符或平台 API。
 */
XDomParseResult XDomDocument_setContent_device_result(XDomDocument* self, XIODevice* device,
                                                       unsigned int options);
/**
 * @brief      旧式字节数组解析接口。
 * @param      self 目标文档。
 * @param      data XML 字节输入，只借用。
 * @param      options 解析选项。
 * @param      errorMessage 错误消息输出，由调用者释放。
 * @param      errorLine 错误行号输出，可为 NULL。
 * @param      errorColumn 错误列号输出，可为 NULL。
 * @return      成功返回 true。
 */
bool XDomDocument_setContent(XDomDocument* self, const XByteArray* data, unsigned int options,
                                XString** errorMessage, int64_t* errorLine, int64_t* errorColumn);
/**
 * @brief      旧式 UTF-8 字符串解析接口。
 * @param      self 目标文档。
 * @param      data UTF-8 输入，只借用。
 * @param      options 解析选项。
 * @param      errorMessage 错误消息输出，由调用者释放。
 * @param      errorLine 行号输出，可为 NULL。
 * @param      errorColumn 列号输出，可为 NULL。
 * @return      成功返回 true。
 */
bool XDomDocument_setContent_utf8(XDomDocument* self, const char* data, unsigned int options,
                                     XString** errorMessage, int64_t* errorLine, int64_t* errorColumn);
/**
 * @brief      从 XString 设置文档并展开旧式错误输出参数。
 * @param      self 目标文档；解析开始前先清空原文档树。
 * @param      data UTF-16 XML 文本；只借用，函数内部按 UTF-16 内容读取。
 * @param      options XDomParseOption 位组合。
 * @param      errorMessage 输出错误字符串；成功时写入 NULL，非 NULL 时由调用者释放。
 * @param      errorLine 输出错误行号；可为 NULL，调用方提供存储空间。
 * @param      errorColumn 输出错误列号；可为 NULL，调用方提供存储空间。
 * @return      解析成功返回 true；失败返回 false，错误消息和位置通过输出参数返回。
 */
bool XDomDocument_setContent_string(XDomDocument* self, const XString* data, unsigned int options,
                                    XString** errorMessage, int64_t* errorLine, int64_t* errorColumn);
/**
 * @brief      从已有 Reader 设置文档并展开旧式错误输出参数。
 * @param      self 目标文档；解析开始前先清空原文档树。
 * @param      reader 已初始化的 Reader；只借用，解析过程会推进其状态。
 * @param      options XDomParseOption 位组合。
 * @param      errorMessage 输出错误字符串；成功时写入 NULL，非 NULL 时由调用者释放。
 * @param      errorLine 输出错误行号；可为 NULL，调用方提供存储空间。
 * @param      errorColumn 输出错误列号；可为 NULL，调用方提供存储空间。
 * @return      解析成功返回 true；失败返回 false，Reader 仍由调用者负责释放。
 * @note      Reader 的 XML 声明、DTD 和错误位置参与构建文档结果。
 */
bool XDomDocument_setContent_reader(XDomDocument* self, XXmlStreamReader* reader,
                                    unsigned int options, XString** errorMessage,
                                    int64_t* errorLine, int64_t* errorColumn);
/**
 * @brief      从已有设备设置文档并展开旧式错误输出参数。
 * @param      self 目标文档；解析开始前先清空原文档树。
 * @param      device 可读 XIODevice；只借用，不打开、关闭或释放。
 * @param      options XDomParseOption 位组合。
 * @param      errorMessage 输出错误字符串；成功时写入 NULL，非 NULL 时由调用者释放。
 * @param      errorLine 输出错误行号；可为 NULL，调用方提供存储空间。
 * @param      errorColumn 输出错误列号；可为 NULL，调用方提供存储空间。
 * @return      解析成功返回 true；失败返回 false，设备状态由设备抽象维护。
 * @note      设备读取只通过 XIODevice 完成，不使用平台文件描述符。
 */
bool XDomDocument_setContent_device(XDomDocument* self, XIODevice* device,
                                    unsigned int options, XString** errorMessage,
                                    int64_t* errorLine, int64_t* errorColumn);
/**
 * @brief      序列化为 XString。
 * @param      self 文档。
 * @param      indent 缩进宽度，小于 0 表示紧凑输出。
 * @return      新字符串，调用者释放。
 */
XString* XDomDocument_toString(const XDomDocument* self, int indent);
/**
 * @brief      序列化为 UTF-8 字节数组。
 * @param      self 文档。
 * @param      indent 缩进宽度，小于 0 表示紧凑输出。
 * @return      新字节数组，调用者释放。
 */
XByteArray* XDomDocument_toByteArray(const XDomDocument* self, int indent);
/**
 * @brief      初始化解析结果。
 * @param      result 可写结果对象。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomParseResult_init(XDomParseResult* result);
/**
 * @brief      释放解析结果。
 * @param      result 已初始化结果；可为 NULL。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomParseResult_deinit(XDomParseResult* result);
/**
 * @brief      判断解析是否成功。
 * @param      result 解析结果。
 * @return      没有错误消息且位置为 0 时返回 true。
 */
bool XDomParseResult_isSuccess(const XDomParseResult* result);

/**
 * @brief      QDomDocumentType 文档类型声明查询 API。
 * @param      self 文档类型节点；为空时返回空字符串或空映射。
 * @return      name/publicId/systemId/internalSubset 为所属节点持有的只读字符串；entities 和
 *             notations 返回的映射由调用者释放。
 */
/**
 * @brief      获取文档类型名称。
 * @param      self 文档类型节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomDocumentType_name(const XDomDocumentType* self);
/**
 * @brief      获取实体声明映射。
 * @param      self 文档类型节点。
 * @return      新映射句柄，调用者释放。
 */
XDomNamedNodeMap* XDomDocumentType_entities(const XDomDocumentType* self);
/**
 * @brief      获取符号声明映射。
 * @param      self 文档类型节点。
 * @return      新映射句柄，调用者释放。
 */
XDomNamedNodeMap* XDomDocumentType_notations(const XDomDocumentType* self);
/**
 * @brief      获取公共标识符。
 * @param      self 文档类型节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomDocumentType_publicId(const XDomDocumentType* self);
/**
 * @brief      获取系统标识符。
 * @param      self 文档类型节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomDocumentType_systemId(const XDomDocumentType* self);
/**
 * @brief      获取内部 DTD 子集。
 * @param      self 文档类型节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomDocumentType_internalSubset(const XDomDocumentType* self);

/**
 * @brief      QDomElement 元素、属性和命名空间 API。
 * @param      self 目标元素；输入字符串只借用，设置函数会复制所需数据。
 * @param      defaultValue 属性不存在时返回的默认字符串，只在本次调用期间借用。
 * @return      查询字符串由元素持有；属性节点和实时列表返回新句柄，由调用者释放。
 * @note      命名空间查询同时比较 namespaceURI 与 localName；xmlns 声明按普通属性序列化。
 */
/**
 * @brief      获取标签名。
 * @param      self 元素。
 * @return      元素持有的只读字符串，无需释放。
 */
const XString* XDomElement_tagName(const XDomElement* self);
/**
 * @brief      设置标签名。
 * @param      self 元素。
 * @param      name 合法标签名，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setTagName(XDomElement* self, const XString* name);
/**
 * @brief      UTF-8 版本的 setTagName。
 * @param      self 元素。
 * @param      name UTF-8 标签名，只借用。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setTagName_utf8(XDomElement* self, const char* name);
/**
 * @brief      按名称获取属性值。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @param      defaultValue 未找到时的默认值，只借用。
 * @return      元素持有值或默认值。
 */
const XString* XDomElement_attribute(const XDomElement* self, const XString* name, const XString* defaultValue);
/**
 * @brief      UTF-8 版本的 attribute。
 * @param      self 元素。
 * @param      name UTF-8 属性名。
 * @param      defaultValue UTF-8 默认值，可为 NULL。
 * @return      只读属性值或默认值。
 */
const XString* XDomElement_attribute_utf8(const XDomElement* self, const char* name, const char* defaultValue);
/**
 * @brief      设置字符串属性。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @param      value 属性值，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttribute(XDomElement* self, const XString* name, const XString* value);
/**
 * @brief      UTF-8 版本的 setAttribute。
 * @param      self 元素。
 * @param      name UTF-8 属性名。
 * @param      value UTF-8 属性值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttribute_utf8(XDomElement* self, const char* name, const char* value);
/**
 * @brief      设置 int 属性。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @param      value 整数值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttribute_int(XDomElement* self, const XString* name, int value);
/**
 * @brief      设置 unsigned int 属性。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @param      value 无符号整数值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttribute_uint(XDomElement* self, const XString* name, unsigned int value);
/**
 * @brief      设置 int64 属性。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @param      value 64 位整数值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttribute_int64(XDomElement* self, const XString* name, int64_t value);
/**
 * @brief      设置 uint64 属性。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @param      value 64 位无符号整数值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttribute_uint64(XDomElement* self, const XString* name, uint64_t value);
/**
 * @brief      设置 double 属性。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @param      value 双精度浮点值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttribute_double(XDomElement* self, const XString* name, double value);
/**
 * @brief      设置 float 属性。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @param      value 单精度浮点值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttribute_float(XDomElement* self, const XString* name, float value);
/**
 * @brief      删除属性。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_removeAttribute(XDomElement* self, const XString* name);
/**
 * @brief      UTF-8 版本的 removeAttribute。
 * @param      self 元素。
 * @param      name UTF-8 属性名。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_removeAttribute_utf8(XDomElement* self, const char* name);
/**
 * @brief      获取属性节点。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @return      新属性句柄，调用者释放；找不到时为空。
 */
XDomAttr* XDomElement_attributeNode(const XDomElement* self, const XString* name);
/**
 * @brief      UTF-8 版本的 attributeNode。
 * @param      self 元素。
 * @param      name UTF-8 属性名。
 * @return      新属性句柄，调用者释放；找不到时为空。
 */
XDomAttr* XDomElement_attributeNode_utf8(const XDomElement* self, const char* name);
/**
 * @brief      挂接或替换属性节点。
 * @param      self 元素。
 * @param      newAttr 属性节点，只借用。
 * @return      被替换属性句柄，调用者释放；无旧属性时为空。
 */
XDomAttr* XDomElement_setAttributeNode(XDomElement* self, const XDomAttr* newAttr);
/**
 * @brief      移除属性节点。
 * @param      self 元素。
 * @param      oldAttr 要移除的属性，只借用。
 * @return      被移除属性句柄，调用者释放；失败时为空。
 */
XDomAttr* XDomElement_removeAttributeNode(XDomElement* self, const XDomAttr* oldAttr);
/**
 * @brief      判断属性是否存在。
 * @param      self 元素。
 * @param      name 属性名，只借用。
 * @return      存在返回 true。
 */
bool XDomElement_hasAttribute(const XDomElement* self, const XString* name);
/**
 * @brief      UTF-8 版本的 hasAttribute。
 * @param      self 元素。
 * @param      name UTF-8 属性名。
 * @return      存在返回 true。
 */
bool XDomElement_hasAttribute_utf8(const XDomElement* self, const char* name);
/**
 * @brief      按命名空间读取属性。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      localName 本地名。
 * @param      defaultValue 未找到时默认值，只借用。
 * @return      属性值或默认值。
 */
const XString* XDomElement_attributeNS(const XDomElement* self, const XString* namespaceURI, const XString* localName, const XString* defaultValue);
/**
 * @brief      UTF-8 版本的 attributeNS。
 * @param      self 元素。
 * @param      namespaceURI UTF-8 URI。
 * @param      localName UTF-8 本地名。
 * @param      defaultValue UTF-8 默认值。
 * @return      属性值或默认值。
 */
const XString* XDomElement_attributeNS_utf8(const XDomElement* self, const char* namespaceURI, const char* localName, const char* defaultValue);
/**
 * @brief      设置命名空间属性。
 * @param      self 元素。
 * @param      namespaceURI URI，可为 NULL。
 * @param      qualifiedName 限定名。
 * @param      value 属性值，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttributeNS(XDomElement* self, const XString* namespaceURI, const XString* qualifiedName, const XString* value);
/**
 * @brief      UTF-8 版本的 setAttributeNS。
 * @param      self 元素。
 * @param      namespaceURI UTF-8 URI。
 * @param      qualifiedName UTF-8 限定名。
 * @param      value UTF-8 值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttributeNS_utf8(XDomElement* self, const char* namespaceURI, const char* qualifiedName, const char* value);
/**
 * @brief      设置命名空间 int 属性。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      qualifiedName 限定名。
 * @param      value 整数值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttributeNS_int(XDomElement* self, const XString* namespaceURI, const XString* qualifiedName, int value);
/**
 * @brief      设置命名空间 unsigned int 属性。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      qualifiedName 限定名。
 * @param      value 无符号整数值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttributeNS_uint(XDomElement* self, const XString* namespaceURI, const XString* qualifiedName, unsigned int value);
/**
 * @brief      设置命名空间 int64 属性。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      qualifiedName 限定名。
 * @param      value 64 位整数值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttributeNS_int64(XDomElement* self, const XString* namespaceURI, const XString* qualifiedName, int64_t value);
/**
 * @brief      设置命名空间 uint64 属性。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      qualifiedName 限定名。
 * @param      value 64 位无符号整数值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttributeNS_uint64(XDomElement* self, const XString* namespaceURI, const XString* qualifiedName, uint64_t value);
/**
 * @brief      设置命名空间 double 属性。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      qualifiedName 限定名。
 * @param      value 双精度浮点值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_setAttributeNS_double(XDomElement* self, const XString* namespaceURI, const XString* qualifiedName, double value);
/**
 * @brief      删除命名空间属性。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      localName 本地名。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_removeAttributeNS(XDomElement* self, const XString* namespaceURI, const XString* localName);
/**
 * @brief      UTF-8 版本的 removeAttributeNS。
 * @param      self 元素。
 * @param      namespaceURI UTF-8 URI。
 * @param      localName UTF-8 本地名。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomElement_removeAttributeNS_utf8(XDomElement* self, const char* namespaceURI, const char* localName);
/**
 * @brief      获取命名空间属性节点。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      localName 本地名。
 * @return      新属性句柄，调用者释放；找不到时为空。
 */
XDomAttr* XDomElement_attributeNodeNS(const XDomElement* self, const XString* namespaceURI, const XString* localName);
/**
 * @brief      挂接或替换命名空间属性。
 * @param      self 元素。
 * @param      newAttr 属性，只借用。
 * @return      被替换属性句柄，调用者释放；无旧属性时为空。
 */
XDomAttr* XDomElement_setAttributeNodeNS(XDomElement* self, const XDomAttr* newAttr);
/**
 * @brief      判断命名空间属性是否存在。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      localName 本地名。
 * @return      存在返回 true。
 */
bool XDomElement_hasAttributeNS(const XDomElement* self, const XString* namespaceURI, const XString* localName);
/**
 * @brief      创建按标签名查询的实时列表。
 * @param      self 元素。
 * @param      tagName 标签名，NULL 或空字符串匹配全部。
 * @return      新列表句柄，调用者释放。
 */
XDomNodeList* XDomElement_elementsByTagName(const XDomElement* self, const XString* tagName);
/**
 * @brief      UTF-8 版本的 elementsByTagName。
 * @param      self 元素。
 * @param      tagName UTF-8 标签名。
 * @return      新列表句柄，调用者释放。
 */
XDomNodeList* XDomElement_elementsByTagName_utf8(const XDomElement* self, const char* tagName);
/**
 * @brief      按命名空间查询实时子孙列表。
 * @param      self 元素。
 * @param      namespaceURI URI。
 * @param      localName 本地名。
 * @return      新列表句柄，调用者释放。
 */
XDomNodeList* XDomElement_elementsByTagNameNS(const XDomElement* self, const XString* namespaceURI, const XString* localName);
/**
 * @brief      获取元素递归文本。
 * @param      self 元素。
 * @return      元素持有的只读字符串，无需释放。
 */
const XString* XDomElement_text(const XDomElement* self);
/**
 * @brief      获取属性映射。
 * @param      self 元素。
 * @return      新映射句柄，调用者释放。
 */
XDomNamedNodeMap* XDomElement_attributes(const XDomElement* self);

/**
 * @brief      QDomAttr 属性节点查询和修改 API。
 * @param      self 目标属性；value 设置会复制输入字符串并标记 specified。
 * @return      查询字符串由属性节点持有；ownerElement 返回的新句柄由调用者释放。
 */
/**
 * @brief      获取属性名称。
 * @param      self 属性节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomAttr_name(const XDomAttr* self);
/**
 * @brief      判断属性是否明确指定。
 * @param      self 属性节点。
 * @return      specified 状态。
 */
bool XDomAttr_specified(const XDomAttr* self);
/**
 * @brief      获取所属元素。
 * @param      self 属性节点。
 * @return      新元素句柄，调用者释放；未挂接时为空。
 */
XDomElement* XDomAttr_ownerElement(const XDomAttr* self);
/**
 * @brief      获取属性值。
 * @param      self 属性节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomAttr_value(const XDomAttr* self);
/**
 * @brief      设置属性值。
 * @param      self 属性节点。
 * @param      value 新值，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomAttr_setValue(XDomAttr* self, const XString* value);
/**
 * @brief      UTF-8 版本的 setValue。
 * @param      self 属性节点。
 * @param      value UTF-8 新值，可为 NULL。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomAttr_setValue_utf8(XDomAttr* self, const char* value);

/**
 * @brief      QDomCharacterData、QDomText、QDomCDATASection 和 QDomComment API。
 * @param      self 目标字符数据节点；offset/count 按 UTF-16 代码单元计数。
 * @param      value 新数据，只借用并在函数内复制；splitText 返回的新文本节点由调用者释放。
 * @return      substringData 返回的新字符串由调用者释放，其余查询字符串由节点持有。
 * @note      insertData 的 offset 超过当前长度时保持原值；CDATA 序列化会拆分 `]]>`。
 */
/**
 * @brief      截取字符数据。
 * @param      self 字符数据节点。
 * @param      offset 起始 UTF-16 索引。
 * @param      count 最大代码单元数。
 * @return      新 XString，调用者释放。
 */
XString* XDomCharacterData_substringData(const XDomCharacterData* self, uint64_t offset, uint64_t count);
/**
 * @brief      追加字符数据。
 * @param      self 字符数据节点。
 * @param      value 新数据，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomCharacterData_appendData(XDomCharacterData* self, const XString* value);
/**
 * @brief      UTF-8 版本的 appendData。
 * @param      self 字符数据节点。
 * @param      value UTF-8 数据，只借用。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomCharacterData_appendData_utf8(XDomCharacterData* self, const char* value);
/**
 * @brief      在指定位置插入数据。
 * @param      self 字符数据节点。
 * @param      offset UTF-16 索引。
 * @param      value 数据，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomCharacterData_insertData(XDomCharacterData* self, uint64_t offset, const XString* value);
/**
 * @brief      删除字符区间。
 * @param      self 字符数据节点。
 * @param      offset 起始 UTF-16 索引。
 * @param      count 删除代码单元数。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomCharacterData_deleteData(XDomCharacterData* self, uint64_t offset, uint64_t count);
/**
 * @brief      替换字符区间。
 * @param      self 字符数据节点。
 * @param      offset 起始 UTF-16 索引。
 * @param      count 删除代码单元数。
 * @param      value 替换数据，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomCharacterData_replaceData(XDomCharacterData* self, uint64_t offset, uint64_t count, const XString* value);
/**
 * @brief      获取字符数据长度。
 * @param      self 字符数据节点。
 * @return      UTF-16 代码单元数量。
 */
int XDomCharacterData_length(const XDomCharacterData* self);
/**
 * @brief      获取字符数据。
 * @param      self 字符数据节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomCharacterData_data(const XDomCharacterData* self);
/**
 * @brief      设置字符数据。
 * @param      self 字符数据节点。
 * @param      value 新数据，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomCharacterData_setData(XDomCharacterData* self, const XString* value);
/**
 * @brief      UTF-8 版本的 setData。
 * @param      self 字符数据节点。
 * @param      value UTF-8 数据，只借用。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomCharacterData_setData_utf8(XDomCharacterData* self, const char* value);
/**
 * @brief      拆分文本节点。
 * @param      self Text 节点。
 * @param      offset UTF-16 拆分位置。
 * @return      插入原节点后的新 Text 句柄，调用者释放。
 */
XDomText* XDomText_splitText(XDomText* self, int offset);

/**
 * @brief      QDomEntity 和 QDomNotation 声明查询 API。
 * @param      self 实体或符号声明节点；空句柄返回空字符串。
 * @return      所有返回字符串均为节点持有的只读值，不需要调用者释放。
 */
/**
 * @brief      获取实体公共标识符。
 * @param      self 实体节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomEntity_publicId(const XDomEntity* self);
/**
 * @brief      获取实体系统标识符。
 * @param      self 实体节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomEntity_systemId(const XDomEntity* self);
/**
 * @brief      获取实体 notationName。
 * @param      self 实体节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomEntity_notationName(const XDomEntity* self);
/**
 * @brief      获取符号公共标识符。
 * @param      self 符号节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomNotation_publicId(const XDomNotation* self);
/**
 * @brief      获取符号系统标识符。
 * @param      self 符号节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomNotation_systemId(const XDomNotation* self);

/**
 * @brief      QDomProcessingInstruction、QDomEntityReference 和 QDomDocumentFragment API。
 * @param      self 目标节点；处理指令 setData 会复制输入数据。
 * @return      查询字符串由节点持有，不需要调用者释放；节点句柄按对应生命周期 API 释放。
 */
/**
 * @brief      获取处理指令 target。
 * @param      self 处理指令节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomProcessingInstruction_target(const XDomProcessingInstruction* self);
/**
 * @brief      获取处理指令 data。
 * @param      self 处理指令节点。
 * @return      节点持有的只读字符串，无需释放。
 */
const XString* XDomProcessingInstruction_data(const XDomProcessingInstruction* self);
/**
 * @brief      设置处理指令 data。
 * @param      self 处理指令节点。
 * @param      value 新数据，只借用并复制。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomProcessingInstruction_setData(XDomProcessingInstruction* self, const XString* value);
/**
 * @brief      UTF-8 版本的 setData。
 * @param      self 处理指令节点。
 * @param      value UTF-8 数据，只借用。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomProcessingInstruction_setData_utf8(XDomProcessingInstruction* self, const char* value);

/**
 * @brief      QDomImplementation 能力查询和文档工厂 API。
 * @param      self 实现句柄；由 XDomDocument_implementation 创建并由调用者释放。
 * @param      feature/version 能力名称和版本；当前支持 XML、空版本和 XML 1.0。
 * @param      qualifiedName 文档类型或根元素名称；必须是有效 XML 名称且不能为空。
 * @param      namespaceURI 命名空间 URI；为 NULL 时创建无命名空间节点。
 * @param      doctype 可选文档类型；输入句柄只借用，创建文档时会复制它。
 * @return      创建函数返回的新文档或文档类型由调用者释放；失败返回空句柄。
 * @note      invalidDataPolicy 是进程级配置，只接受本枚举中的三个值；不使用平台 API。
 */
/**
 * @brief      查询 DOM 能力。
 * @param      self 实现句柄。
 * @param      feature 能力名称。
 * @param      version 版本，空字符串表示不限版本。
 * @return      支持返回 true。
 */
bool XDomImplementation_hasFeature(const XDomImplementation* self, const XString* feature, const XString* version);
/**
 * @brief      UTF-8 版本的 hasFeature。
 * @param      self 实现句柄。
 * @param      feature UTF-8 能力名称。
 * @param      version UTF-8 版本。
 * @return      支持返回 true。
 */
bool XDomImplementation_hasFeature_utf8(const XDomImplementation* self, const char* feature, const char* version);
/**
 * @brief      创建文档类型。
 * @param      self 实现句柄。
 * @param      qualifiedName 限定名。
 * @param      publicId 公共标识符。
 * @param      systemId 系统标识符。
 * @return      新文档类型句柄，调用者释放。
 */
XDomDocumentType* XDomImplementation_createDocumentType(const XDomImplementation* self, const XString* qualifiedName, const XString* publicId, const XString* systemId);
/**
 * @brief      UTF-8 版本的 createDocumentType。
 * @param      self 实现句柄。
 * @param      qualifiedName UTF-8 限定名。
 * @param      publicId UTF-8 公共标识符。
 * @param      systemId UTF-8 系统标识符。
 * @return      新句柄，调用者释放。
 */
XDomDocumentType* XDomImplementation_createDocumentType_utf8(const XDomImplementation* self, const char* qualifiedName, const char* publicId, const char* systemId);
/**
 * @brief      创建文档。
 * @param      self 实现句柄。
 * @param      namespaceURI 根元素 URI，可为 NULL。
 * @param      qualifiedName 根元素限定名。
 * @param      doctype 文档类型，只借用并复制。
 * @return      新文档句柄，调用者释放。
 */
XDomDocument* XDomImplementation_createDocument(const XDomImplementation* self, const XString* namespaceURI, const XString* qualifiedName, const XDomDocumentType* doctype);
/**
 * @brief      UTF-8 版本的 createDocument。
 * @param      self 实现句柄。
 * @param      namespaceURI UTF-8 URI，可为 NULL。
 * @param      qualifiedName UTF-8 根元素名。
 * @param      doctype 文档类型，只借用。
 * @return      新文档句柄，调用者释放。
 */
XDomDocument* XDomImplementation_createDocument_utf8(const XDomImplementation* self, const char* namespaceURI, const char* qualifiedName, const XDomDocumentType* doctype);
/**
 * @brief      判断实现句柄是否为空。
 * @param      self 实现句柄。
 * @return      NULL 或空实现返回 true。
 */
bool XDomImplementation_isNull(const XDomImplementation* self);
/**
 * @brief      判断两个实现句柄是否等价。
 * @param      left 左句柄。
 * @param      right 右句柄。
 * @return      两者同为空或均为有效实现返回 true。
 */
bool XDomImplementation_equals(const XDomImplementation* left, const XDomImplementation* right);
/**
 * @brief      获取进程级无效字符策略。
 * @return      当前 XDomInvalidDataPolicy。
 */
XDomInvalidDataPolicy XDomImplementation_invalidDataPolicy(void);
/**
 * @brief      设置进程级无效字符策略。
 * @param      policy 有效的 XDomInvalidDataPolicy 枚举值。
 * @return      无；NULL 输入不执行操作，成功后对象状态按函数说明更新。
 */
void XDomImplementation_setInvalidDataPolicy(XDomInvalidDataPolicy policy);

/**
 * @brief      把各具体 DOM 句柄转换为共享的 XDomNode 句柄。
 * @param      self 具体节点句柄；输入只借用，必须与目标函数要求的节点类型一致。
 * @return      返回共享节点的新包装句柄，由调用者使用 XDomNode_delete_base 释放；类型不符
 *             或输入为空时返回空节点句柄，不复制底层树。
 */
/**
 * @brief      将元素句柄转换为 XDomNode。
 * @param      self 元素句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomElement_toNode(const XDomElement* self);
/**
 * @brief      将属性句柄转换为 XDomNode。
 * @param      self 属性句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomAttr_toNode(const XDomAttr* self);
/**
 * @brief      将文本句柄转换为 XDomNode。
 * @param      self 文本句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomText_toNode(const XDomText* self);
/**
 * @brief      将 CDATA 句柄转换为 XDomNode。
 * @param      self CDATA 句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomCDATASection_toNode(const XDomCDATASection* self);
/**
 * @brief      将注释句柄转换为 XDomNode。
 * @param      self 注释句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomComment_toNode(const XDomComment* self);
/**
 * @brief      将文档句柄转换为 XDomNode。
 * @param      self 文档句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomDocument_toNode(const XDomDocument* self);
/**
 * @brief      将文档类型句柄转换为 XDomNode。
 * @param      self 文档类型句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomDocumentType_toNode(const XDomDocumentType* self);
/**
 * @brief      将文档片段句柄转换为 XDomNode。
 * @param      self 文档片段句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomDocumentFragment_toNode(const XDomDocumentFragment* self);
/**
 * @brief      将字符数据句柄转换为 XDomNode。
 * @param      self 字符数据句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomCharacterData_toNode(const XDomCharacterData* self);
/**
 * @brief      将实体句柄转换为 XDomNode。
 * @param      self 实体句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomEntity_toNode(const XDomEntity* self);
/**
 * @brief      将符号句柄转换为 XDomNode。
 * @param      self 符号句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomNotation_toNode(const XDomNotation* self);
/**
 * @brief      将实体引用句柄转换为 XDomNode。
 * @param      self 实体引用句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomEntityReference_toNode(const XDomEntityReference* self);
/**
 * @brief      将处理指令句柄转换为 XDomNode。
 * @param      self 处理指令句柄，只借用。
 * @return      新节点句柄，调用者释放；不复制底层树。
 */
XDomNode* XDomProcessingInstruction_toNode(const XDomProcessingInstruction* self);

#ifdef __cplusplus
}
#endif

#undef XDomDocument_create
#define XDomDocument_create() XDomDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XDomImplementation_create
#define XDomImplementation_create() XDomImplementation_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif
