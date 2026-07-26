/******************************************************************************
 * @file       XXmlStreamWriter.h
 * @brief      XXmlStreamWriter XML 流式写入器（对标 Qt 6.8 QXmlStreamWriter）
 * @author     XinYueC 团队
 * @note       提供流式 XML 写入功能，支持自动格式化
 ******************************************************************************/
#ifndef XXMLSTREAMWRITER_H
#define XXMLSTREAMWRITER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XString.h"
#include "XByteArray.h"
#include "XFileDevice.h"
#include "XXmlStreamReader.h"  /* 用于 writeCurrentToken */

/* ========== XXmlStreamWriter 结构体 ========== */

/**
 * @brief      XXmlStreamWriter XML 流式写入器（对标 Qt 6.8 QXmlStreamWriter）
 * @note       提供流式 XML 写入功能，支持自动缩进格式化
 */
typedef struct XXmlStreamWriter
{
    XClass     m_class;           /**< 继承的基类成员 */
    XByteArray* m_buffer;         /**< 内部输出缓冲区 */
    XString*   m_deviceString;    /**< 输出到字符串的缓冲区 */
    struct XIODevice* m_device;   /**< 可选输出设备（对标 Qt QIODevice） */
    bool       m_autoFormatting;   /**< 是否启用自动格式化（缩进） */
    int        m_autoFormattingIndent; /**< 缩进空格数 */
    int        m_elementStack;    /**< 元素嵌套深度 */
    bool       m_hasError;        /**< 是否有错误 */
    bool       m_inStartElement;  /**< 是否在开始标签内部（等待属性） */
    bool       m_pendingEmptyElement; /**< 当前开始标签应以 /> 结束 */
    unsigned int m_namespacePrefixCounter; /**< 自动生成命名空间属性前缀的计数器 */
    XString**  m_elementNameStack; /**< 记录每个 writeStartElement 的限定名（writeEndElement 时匹配） */
    int        m_elementNameStackSize;  /**< 元素名栈大小 */
    int        m_elementNameStackCapacity; /**< 元素名栈容量 */
} XXmlStreamWriter;

/* ========== 虚函数表初始化 ========== */

/**
 * @brief      初始化 XXmlStreamWriter 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XXmlStreamWriter_class_init(void);

/* ========== 创建与初始化 ========== */

/**
 * @brief      在堆上创建 XXmlStreamWriter 实例
 * @return     指向新创建的 XXmlStreamWriter 对象的指针，失败返回 NULL
 */
XXmlStreamWriter* XXmlStreamWriter_create(void);

/**
 * @brief      在堆上拷贝创建 XXmlStreamWriter 实例（深拷贝 buffer/deviceString/element 栈）
 * @param other 源 XXmlStreamWriter 对象指针
 * @return     指向新创建的 XXmlStreamWriter 对象的指针，失败返回 NULL
 */
XXmlStreamWriter* XXmlStreamWriter_create_copy(const XXmlStreamWriter* other);

/**
 * @brief      在堆上移动创建 XXmlStreamWriter 实例（转移 other 资源所有权）
 * @param other 源 XXmlStreamWriter 对象指针（移动后被置空）
 * @return     指向新创建的 XXmlStreamWriter 对象的指针，失败返回 NULL
 */
XXmlStreamWriter* XXmlStreamWriter_create_move(XXmlStreamWriter* other);

/**
 * @brief      初始化 XXmlStreamWriter 实例
 * @param self 待初始化的 XXmlStreamWriter 对象指针
 */
void XXmlStreamWriter_init(XXmlStreamWriter* self);

/* ========== 虚函数调度 ========== */

#define  XXmlStreamWriter_copy_base             XClass_copy_base
#define  XXmlStreamWriter_move_base             XClass_move_base
#define  XXmlStreamWriter_deinit_base           XClass_deinit_base
#define  XXmlStreamWriter_delete_base           XClass_delete_base

/* ========== 设备设置 ========== */

/**
 * @brief      获取输出缓冲区字符串
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     输出字符串（以 UTF-8 编码）
 */
const char* XXmlStreamWriter_toString(const XXmlStreamWriter* self);

/**
 * @brief      获取输出缓冲区字符串（XString 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     输出字符串（XString，需调用方删除）
 */
XString* XXmlStreamWriter_toString_x(const XXmlStreamWriter* self);

/**
 * @brief      获取输出缓冲区
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     指向 XByteArray 的指针
 */
XByteArray* XXmlStreamWriter_toByteArray(const XXmlStreamWriter* self);

/* ========== 格式化设置 ========== */

/**
 * @brief      设置是否启用自动格式化
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param enable 是否启用
 */
void XXmlStreamWriter_setAutoFormatting(XXmlStreamWriter* self, bool enable);

/**
 * @brief      判断是否启用自动格式化
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     启用返回 true
 */
bool XXmlStreamWriter_autoFormatting(const XXmlStreamWriter* self);

/**
 * @brief      设置缩进空格数
 * @param self  目标 XXmlStreamWriter 对象指针
 * @param indent 缩进空格数
 */
void XXmlStreamWriter_setAutoFormattingIndent(XXmlStreamWriter* self, int indent);

/**
 * @brief      获取缩进空格数
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     缩进空格数
 */
int XXmlStreamWriter_autoFormattingIndent(const XXmlStreamWriter* self);

/* ========== 写入方法 ========== */

/**
 * @brief      写入文档开始声明（<?xml version="1.0"?>）
 * @param self 目标 XXmlStreamWriter 对象指针
 */
void XXmlStreamWriter_writeStartDocument(XXmlStreamWriter* self);

/**
 * @brief      写入文档开始声明（带版本）
 * @param self    目标 XXmlStreamWriter 对象指针
 * @param version 版本号
 */
void XXmlStreamWriter_writeStartDocument_ex(XXmlStreamWriter* self, const XString* version);

/**
 * @brief      写入文档开始声明（带版本）UTF-8 版本
 * @param self    目标 XXmlStreamWriter 对象指针
 * @param version 版本号（UTF-8 编码）
 */
void XXmlStreamWriter_writeStartDocument_ex_utf8(XXmlStreamWriter* self, const char* version);

/**
 * @brief      写入文档开始声明（带版本和独立标志）
 * @param self       目标 XXmlStreamWriter 对象指针
 * @param version    版本号
 * @param standalone 是否独立文档
 */
void XXmlStreamWriter_writeStartDocument_ex_2(XXmlStreamWriter* self, const XString* version, bool standalone);

/**
 * @brief      写入文档开始声明（带版本和独立标志）UTF-8 版本
 * @param self       目标 XXmlStreamWriter 对象指针
 * @param version    版本号（UTF-8 编码）
 * @param standalone 是否独立文档
 */
void XXmlStreamWriter_writeStartDocument_ex_2_utf8(XXmlStreamWriter* self, const char* version, bool standalone);

/**
 * @brief      写入文档结束
 * @param self 目标 XXmlStreamWriter 对象指针
 */
void XXmlStreamWriter_writeEndDocument(XXmlStreamWriter* self);

/**
 * @brief      写入开始标签
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 */
void XXmlStreamWriter_writeStartElement(XXmlStreamWriter* self, const XString* qualifiedName);

/**
 * @brief      写入开始标签（UTF-8 版本）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名（UTF-8 编码）
 */
void XXmlStreamWriter_writeStartElement_utf8(XXmlStreamWriter* self, const char* qualifiedName);

/**
 * @brief      写入开始标签（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 */
void XXmlStreamWriter_writeStartElement_ex(XXmlStreamWriter* self, const XString* namespaceUri, const XString* name);

/**
 * @brief      写入开始标签（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 */
void XXmlStreamWriter_writeStartElement_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

/**
 * @brief      写入结束标签
 * @param self 目标 XXmlStreamWriter 对象指针
 */
void XXmlStreamWriter_writeEndElement(XXmlStreamWriter* self);

/**
 * @brief      写入空元素标签
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 */
void XXmlStreamWriter_writeEmptyElement(XXmlStreamWriter* self, const XString* qualifiedName);

/**
 * @brief      写入空元素标签（UTF-8 版本）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名（UTF-8 编码）
 */
void XXmlStreamWriter_writeEmptyElement_utf8(XXmlStreamWriter* self, const char* qualifiedName);

/**
 * @brief      写入空元素标签（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 */
void XXmlStreamWriter_writeEmptyElement_ex(XXmlStreamWriter* self, const XString* namespaceUri, const XString* name);

/**
 * @brief      写入空元素标签（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 */
void XXmlStreamWriter_writeEmptyElement_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

/**
 * @brief      写入属性
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 * @param value         属性值
 */
void XXmlStreamWriter_writeAttribute(XXmlStreamWriter* self, const XString* qualifiedName, const XString* value);

/**
 * @brief      写入属性（UTF-8 版本）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名（UTF-8 编码）
 * @param value         属性值（UTF-8 编码）
 */
void XXmlStreamWriter_writeAttribute_utf8(XXmlStreamWriter* self, const char* qualifiedName, const char* value);

/**
 * @brief      写入属性（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 * @param value        属性值
 */
void XXmlStreamWriter_writeAttribute_ex(XXmlStreamWriter* self, const XString* namespaceUri, const XString* name, const XString* value);

/**
 * @brief      写入属性（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 * @param value        属性值（UTF-8 编码）
 */
void XXmlStreamWriter_writeAttribute_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name, const char* value);

/**
 * @brief      写入属性（从 XXmlStreamAttribute 对象）
 * @param self      目标 XXmlStreamWriter 对象指针
 * @param attribute 属性对象指针
 */
void XXmlStreamWriter_writeAttribute_attr(XXmlStreamWriter* self, const XXmlStreamAttribute* attribute);

/**
 * @brief      写入属性列表
 * @param self       目标 XXmlStreamWriter 对象指针
 * @param attributes 属性列表指针
 */
void XXmlStreamWriter_writeAttributes(XXmlStreamWriter* self, const XXmlStreamAttributes* attributes);

/**
 * @brief      写入字符数据
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 字符数据
 */
void XXmlStreamWriter_writeCharacters(XXmlStreamWriter* self, const XString* text);

/**
 * @brief      写入字符数据（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 字符数据（UTF-8 编码）
 */
void XXmlStreamWriter_writeCharacters_utf8(XXmlStreamWriter* self, const char* text);

/**
 * @brief      写入 CDATA 段
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text CDATA 文本
 */
void XXmlStreamWriter_writeCDATA(XXmlStreamWriter* self, const XString* text);

/**
 * @brief      写入 CDATA 段（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text CDATA 文本（UTF-8 编码）
 */
void XXmlStreamWriter_writeCDATA_utf8(XXmlStreamWriter* self, const char* text);

/**
 * @brief      写入注释
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 注释文本
 */
void XXmlStreamWriter_writeComment(XXmlStreamWriter* self, const XString* text);

/**
 * @brief      写入注释（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 注释文本（UTF-8 编码）
 */
void XXmlStreamWriter_writeComment_utf8(XXmlStreamWriter* self, const char* text);

/**
 * @brief      写入处理指令
 * @param self   目标 XXmlStreamWriter 对象指针
 * @param target 指令目标
 * @param data   指令数据（可为 NULL）
 */
void XXmlStreamWriter_writeProcessingInstruction(XXmlStreamWriter* self, const XString* target, const XString* data);

/**
 * @brief      写入处理指令（UTF-8 版本）
 * @param self   目标 XXmlStreamWriter 对象指针
 * @param target 指令目标（UTF-8 编码）
 * @param data   指令数据（UTF-8 编码，可为 NULL）
 */
void XXmlStreamWriter_writeProcessingInstruction_utf8(XXmlStreamWriter* self, const char* target, const char* data);

/**
 * @brief      写入实体引用
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param name 实体名称
 */
void XXmlStreamWriter_writeEntityReference(XXmlStreamWriter* self, const XString* name);

/**
 * @brief      写入实体引用（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param name 实体名称（UTF-8 编码）
 */
void XXmlStreamWriter_writeEntityReference_utf8(XXmlStreamWriter* self, const char* name);

/**
 * @brief      写入 DTD 声明
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param dtd  DTD 字符串
 */
void XXmlStreamWriter_writeDTD(XXmlStreamWriter* self, const XString* dtd);

/**
 * @brief      写入 DTD 声明（UTF-8 版本）
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param dtd  DTD 字符串（UTF-8 编码）
 */
void XXmlStreamWriter_writeDTD_utf8(XXmlStreamWriter* self, const char* dtd);

/**
 * @brief      写入命名空间声明
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param prefix       命名空间前缀（可为空字符串）
 */
void XXmlStreamWriter_writeNamespace(XXmlStreamWriter* self, const XString* namespaceUri, const XString* prefix);

/**
 * @brief      写入命名空间声明（UTF-8 版本）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param prefix       命名空间前缀（UTF-8 编码，可为空字符串）
 */
void XXmlStreamWriter_writeNamespace_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* prefix);

/**
 * @brief      写入默认命名空间声明
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 */
void XXmlStreamWriter_writeDefaultNamespace(XXmlStreamWriter* self, const XString* namespaceUri);

/**
 * @brief      写入默认命名空间声明（UTF-8 版本）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 */
void XXmlStreamWriter_writeDefaultNamespace_utf8(XXmlStreamWriter* self, const char* namespaceUri);

/**
 * @brief      写入文本元素（包含开始标签、文本、结束标签）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 * @param text          文本内容
 */
void XXmlStreamWriter_writeTextElement(XXmlStreamWriter* self, const XString* qualifiedName, const XString* text);

/**
 * @brief      写入文本元素（包含开始标签、文本、结束标签）UTF-8 版本
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名（UTF-8 编码）
 * @param text          文本内容（UTF-8 编码）
 */
void XXmlStreamWriter_writeTextElement_utf8(XXmlStreamWriter* self, const char* qualifiedName, const char* text);

/**
 * @brief      写入文本元素（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 * @param text         文本内容
 */
void XXmlStreamWriter_writeTextElement_ex(XXmlStreamWriter* self, const XString* namespaceUri, const XString* name, const XString* text);

/**
 * @brief      写入文本元素（带命名空间）UTF-8 版本
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI（UTF-8 编码）
 * @param name         本地名（UTF-8 编码）
 * @param text         文本内容（UTF-8 编码）
 */
void XXmlStreamWriter_writeTextElement_ex_utf8(XXmlStreamWriter* self, const char* namespaceUri, const char* name, const char* text);

/**
 * @brief      写入当前 Token（从读取器复制当前 Token 到写入器）
 * @param self   目标 XXmlStreamWriter 对象指针
 * @param reader 源 XXmlStreamReader 对象指针
 */
void XXmlStreamWriter_writeCurrentToken(XXmlStreamWriter* self, const XXmlStreamReader* reader);

/**
 * @brief      判断是否有错误
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     有错误返回 true
 */
bool XXmlStreamWriter_hasError(const XXmlStreamWriter* self);

/* ========== 设备管理 ========== */

/**
 * @brief      设置输出设备
 * @param self   目标 XXmlStreamWriter 对象指针
 * @param device XIODevice 指针（可为 NULL）
 * @note       对标 QXmlStreamWriter::setDevice
 */
void XXmlStreamWriter_setDevice(XXmlStreamWriter* self, struct XIODevice* device);

/**
 * @brief      获取输出设备
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     XIODevice 指针，无设备返回 NULL
 * @note       对标 QXmlStreamWriter::device
 */
struct XIODevice* XXmlStreamWriter_device(const XXmlStreamWriter* self);

#ifdef __cplusplus
}
#endif
#endif /* XXMLSTREAMWRITER_H */
