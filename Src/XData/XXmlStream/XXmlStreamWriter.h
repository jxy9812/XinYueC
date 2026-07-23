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
    bool       m_autoFormatting;   /**< 是否启用自动格式化（缩进） */
    int        m_autoFormattingIndent; /**< 缩进空格数 */
    int        m_elementStack;    /**< 元素嵌套深度 */
    bool       m_hasError;        /**< 是否有错误 */
    bool       m_inStartElement;  /**< 是否在开始标签内部（等待属性） */
    XString**  m_elementNameStack; /**< ???????? writeEndElement ??????? */
    int        m_elementNameStackSize;  /**< ??????? */
    int        m_elementNameStackCapacity; /**< ??????? */
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
 * @brief      初始化 XXmlStreamWriter 实例
 * @param self 待初始化的 XXmlStreamWriter 对象指针
 */
void XXmlStreamWriter_init(XXmlStreamWriter* self);

/**
 * @brief      释放 XXmlStreamWriter 资源
 * @param self 待释放的 XXmlStreamWriter 对象指针
 */
void XXmlStreamWriter_deinit(XXmlStreamWriter* self);

/**
 * @brief      在堆上删除 XXmlStreamWriter 实例
 * @param self 待删除的 XXmlStreamWriter 对象指针
 */
void XXmlStreamWriter_delete(XXmlStreamWriter* self);

/* ========== 虚函数调度 ========== */

void XXmlStreamWriter_deinit_base(XXmlStreamWriter* self);
void XXmlStreamWriter_delete_base(XXmlStreamWriter* self);

/* ========== 设备设置 ========== */

/**
 * @brief      获取输出缓冲区字符串
 * @param self 目标 XXmlStreamWriter 对象指针
 * @return     输出字符串（以 UTF-8 编码）
 */
const char* XXmlStreamWriter_toString(const XXmlStreamWriter* self);

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
void XXmlStreamWriter_writeStartDocument_ex(XXmlStreamWriter* self, const char* version);

/**
 * @brief      写入文档开始声明（带版本和独立标志）
 * @param self       目标 XXmlStreamWriter 对象指针
 * @param version    版本号
 * @param standalone 是否独立文档
 */
void XXmlStreamWriter_writeStartDocument_ex_2(XXmlStreamWriter* self, const char* version, bool standalone);

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
void XXmlStreamWriter_writeStartElement(XXmlStreamWriter* self, const char* qualifiedName);

/**
 * @brief      写入开始标签（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 */
void XXmlStreamWriter_writeStartElement_ex(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

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
void XXmlStreamWriter_writeEmptyElement(XXmlStreamWriter* self, const char* qualifiedName);

/**
 * @brief      写入空元素标签（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 */
void XXmlStreamWriter_writeEmptyElement_ex(XXmlStreamWriter* self, const char* namespaceUri, const char* name);

/**
 * @brief      写入属性
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 * @param value         属性值
 */
void XXmlStreamWriter_writeAttribute(XXmlStreamWriter* self, const char* qualifiedName, const char* value);

/**
 * @brief      写入属性（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 * @param value        属性值
 */
void XXmlStreamWriter_writeAttribute_ex(XXmlStreamWriter* self, const char* namespaceUri, const char* name, const char* value);

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
void XXmlStreamWriter_writeCharacters(XXmlStreamWriter* self, const char* text);

/**
 * @brief      写入 CDATA 段
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text CDATA 文本
 */
void XXmlStreamWriter_writeCDATA(XXmlStreamWriter* self, const char* text);

/**
 * @brief      写入注释
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param text 注释文本
 */
void XXmlStreamWriter_writeComment(XXmlStreamWriter* self, const char* text);

/**
 * @brief      写入处理指令
 * @param self   目标 XXmlStreamWriter 对象指针
 * @param target 指令目标
 * @param data   指令数据（可为 NULL）
 */
void XXmlStreamWriter_writeProcessingInstruction(XXmlStreamWriter* self, const char* target, const char* data);

/**
 * @brief      写入实体引用
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param name 实体名称
 */
void XXmlStreamWriter_writeEntityReference(XXmlStreamWriter* self, const char* name);

/**
 * @brief      写入 DTD 声明
 * @param self 目标 XXmlStreamWriter 对象指针
 * @param dtd  DTD 字符串
 */
void XXmlStreamWriter_writeDTD(XXmlStreamWriter* self, const char* dtd);

/**
 * @brief      写入命名空间声明
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param prefix       命名空间前缀（可为空字符串）
 */
void XXmlStreamWriter_writeNamespace(XXmlStreamWriter* self, const char* namespaceUri, const char* prefix);

/**
 * @brief      写入默认命名空间声明
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 */
void XXmlStreamWriter_writeDefaultNamespace(XXmlStreamWriter* self, const char* namespaceUri);

/**
 * @brief      写入文本元素（包含开始标签、文本、结束标签）
 * @param self          目标 XXmlStreamWriter 对象指针
 * @param qualifiedName 限定名
 * @param text          文本内容
 */
void XXmlStreamWriter_writeTextElement(XXmlStreamWriter* self, const char* qualifiedName, const char* text);

/**
 * @brief      写入文本元素（带命名空间）
 * @param self         目标 XXmlStreamWriter 对象指针
 * @param namespaceUri 命名空间 URI
 * @param name         本地名
 * @param text         文本内容
 */
void XXmlStreamWriter_writeTextElement_ex(XXmlStreamWriter* self, const char* namespaceUri, const char* name, const char* text);

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

#ifdef __cplusplus
}
#endif
#endif /* XXMLSTREAMWRITER_H */
