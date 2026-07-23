/******************************************************************************
 * @file       XRichString.h
 * @brief      XRichString 富文本字符串类（对标 QXlsx::RichString）
 * @author     XinYueC 团队
 * @note       提供富文本字符串的表示，支持多个带格式的文本片段。
 *             对齐 QXlsx::RichString 全部功能
 ******************************************************************************/
#ifndef XRICHSTRING_H
#define XRICHSTRING_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"
#include "XStringList.h"
#include "XVector.h"
#include "XFormat.h"

/* 前向声明 */
typedef struct XRichStringFragment XRichStringFragment;

/**
 * @brief      富文本片段结构体
 * @note       包含文本内容和对应的格式
 */
typedef struct XRichStringFragment
{
    XString* m_text;    /**< 片段文本 */
    XFormat* m_format;  /**< 片段格式 */
} XRichStringFragment;

/**
 * @brief      XRichString 富文本字符串结构体
 * @note       包含多个带格式的文本片段，对齐 QXlsx::RichString 全部功能。
 */
typedef struct XRichString
{
    XString* m_plainText;        /**< 纯文本字符串（缓存） */
    XVector* m_fragments;        /**< 片段列表（XRichStringFragment 数组） */
} XRichString;

/* ========== 创建与初始化 ========== */

/**
 * @brief      创建一个空的 XRichString 对象
 * @return     指向新创建的 XRichString 的指针，失败返回 NULL
 */
XRichString* XRichString_create(void);

/**
 * @brief      使用纯文本创建 XRichString 对象
 * @param text 纯文本
 * @return     指向新创建的 XRichString 的指针
 */
XRichString* XRichString_create_utf8(const char* text);

/**
 * @brief      复制 XRichString 对象
 * @param self  目标指针
 * @param other 源指针
 */
void XRichString_copy(XRichString* self, const XRichString* other);

/**
 * @brief      在堆上删除 XRichString 实例
 * @param self 待删除的指针
 */
void XRichString_delete(XRichString* self);

/* ========== 查询方法 ========== */

/**
 * @brief      判断是否为富文本（有多个片段或格式）
 * @param self 指针
 * @return     是富文本返回 true
 */
bool XRichString_isRichString(const XRichString* self);

/**
 * @brief      判断是否为 NULL
 * @param self 指针
 * @return     NULL 返回 true
 */
bool XRichString_isNull(const XRichString* self);

/**
 * @brief      判断是否为空
 * @param self 指针
 * @return     为空返回 true
 */
bool XRichString_isEmpty(const XRichString* self);

/**
 * @brief      获取纯文本字符串
 * @param self 指针
 * @return     纯文本字符串
 */
const char* XRichString_toPlainString(const XRichString* self);

/**
 * @brief      将富文本转换为 HTML 字符串
 * @param self 指针
 * @return     HTML 字符串（需调用 XString_deinit_base 释放）
 */
XString XRichString_toHtml(const XRichString* self);

/**
 * @brief      从 HTML 字符串设置富文本
 * @param self 指针
 * @param text HTML 字符串
 */
void XRichString_setHtml(XRichString* self, const char* text);

/* ========== 片段管理 ========== */

/**
 * @brief      获取片段数量
 * @param self 指针
 * @return     片段数量
 */
int XRichString_fragmentCount(const XRichString* self);

/**
 * @brief      添加文本片段（带格式）
 * @param self   指针
 * @param text   片段文本
 * @param format 片段格式
 */
void XRichString_addFragment(XRichString* self, const char* text, const XFormat* format);

/**
 * @brief      获取指定索引的片段文本
 * @param self  指针
 * @param index 索引
 * @return     片段文本字符串
 */
const char* XRichString_fragmentText(const XRichString* self, int index);

/**
 * @brief      获取指定索引的片段格式
 * @param self  指针
 * @param index 索引
 * @return     指向片段格式的指针
 */
const XFormat* XRichString_fragmentFormat(const XRichString* self, int index);

#ifdef __cplusplus
}
#endif
#endif /* XRICHSTRING_H */
