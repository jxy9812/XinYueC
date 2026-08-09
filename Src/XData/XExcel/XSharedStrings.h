/******************************************************************************
 * @file       XSharedStrings.h
 * @brief      XSharedStrings 共享字符串表类
 * @author     XinYueC 团队
 * @note       管理 OOXML 共享字符串表
 ******************************************************************************/
#ifndef XSHAREDSTRINGS_H
#define XSHAREDSTRINGS_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XStringList.h"
#include "XVector.h"
#include "XMap.h"
#include "XRichString.h"
#include "XAbstractOOXmlFile.h"
/** @brief 共享字符串信息结构体 */
typedef struct XlsxSharedStringInfo { 
    int m_index;    /**< 索引 */
    int m_count;    /**< 引用计数 */
} XlsxSharedStringInfo;

/** @brief XSharedStrings 共享字符串表结构体 */
typedef struct XSharedStrings {
    XAbstractOOXmlFile m_base;     /**< 基类 */
    XMap* m_stringTable;           /**< 普通字符串到 XlsxSharedStringInfo 的映射 */
    XVector* m_stringList;         /**< 字符串列表 */
    int m_stringCount;             /**< 含重复引用的字符串总数 */
} XSharedStrings;
/**
 * @brief  创建共享字符串表对象
 * @param  flag  创建标志（F_NewFromScratch / F_LoadFromExists）
 * @return 新对象指针，失败返回 NULL
 */
XSharedStrings* XSharedStrings_create(XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief  销毁共享字符串表并释放资源
 * @param  self  共享字符串表指针
 */
void XSharedStrings_delete(XSharedStrings* self);

/**
 * @brief  获取共享字符串总引用数
 * @param  self  共享字符串表指针
 * @return 包含重复引用的字符串总数；唯一条目数可从 getSharedStrings 的向量大小取得
 */
int XSharedStrings_count(const XSharedStrings* self);

/**
 * @brief  判断共享字符串表是否为空
 * @param  self  共享字符串表指针
 * @return 为空返回 true
 */
bool XSharedStrings_isEmpty(const XSharedStrings* self);

/**
 * @brief  添加普通共享字符串（已存在则增加引用计数）
 * @param  self    共享字符串表指针
 * @param  string  要添加的字符串
 * @return 字符串在表中的索引
 */
int XSharedStrings_addSharedString(XSharedStrings* self, const XString* string);

/**
 * @brief  添加富文本共享字符串（已存在则增加引用计数）
 * @param  self  共享字符串表指针
 * @param  rich  要添加的富文本字符串
 * @return 字符串在表中的索引
 */
int XSharedStrings_addSharedRichString(XSharedStrings* self, const XRichString* rich);

/**
 * @brief  移除共享字符串（减少引用计数，计数归零时删除）
 * @param  self    共享字符串表指针
 * @param  string  要移除的字符串
 */
void XSharedStrings_removeSharedString(XSharedStrings* self, const XString* string);

/**
 * @brief  按索引增加共享字符串的引用计数
 * @param  self  共享字符串表指针
 * @param  idx   字符串索引
 */
void XSharedStrings_incRefByStringIndex(XSharedStrings* self, int idx);

/**
 * @brief  获取字符串在共享字符串表中的索引
 * @param  self    共享字符串表指针
 * @param  string  要查找的字符串
 * @return 索引值，未找到返回 -1
 */
int XSharedStrings_getSharedStringIndex(XSharedStrings* self, const XString* string);

/**
 * @brief  按索引获取共享字符串（富文本形式）
 * @param  self   共享字符串表指针
 * @param  index  字符串索引
 * @return 富文本字符串指针，索引无效返回 NULL
 */
XRichString* XSharedStrings_getSharedString(XSharedStrings* self, int index);

/**
 * @brief  获取所有共享字符串列表
 * @param  self  共享字符串表指针
 * @return 字符串列表向量（XRichString*），调用者不应释放
 */
XVector* XSharedStrings_getSharedStrings(XSharedStrings* self);
#ifdef __cplusplus
}
#endif
/**
 * @brief  将共享字符串表序列化为 XML 数据
 * @param  self    共享字符串表指针
 * @param  outData [out] 接收 XML 数据缓冲区的指针，调用者负责释放
 * @param  outLen  [out] 接收数据长度
 * @return 成功返回 true
 */
bool XSharedStrings_saveToXmlData(const XSharedStrings* self, uint8_t** outData, size_t* outLen);

/**
 * @brief  将共享字符串表保存为 XML 文件（xl/sharedStrings.xml）
 * @param  self      共享字符串表指针
 * @param  filePath  输出文件路径
 * @return 成功返回 true
 */
bool XSharedStrings_saveToXmlFile(XSharedStrings* self, const XString* filePath);

/**
 * @brief  从 XML 数据加载共享字符串表
 * @param  self  共享字符串表指针
 * @param  data  XML 数据缓冲区
 * @param  len   数据长度（字节）
 * @return 成功返回 true
 */
bool XSharedStrings_loadFromXmlData(XSharedStrings* self, const uint8_t* data, size_t len);

/**
 * @brief  从 XML 文件加载共享字符串表
 * @param  self      共享字符串表指针
 * @param  filePath  输入文件路径
 * @return 成功返回 true
 */
bool XSharedStrings_loadFromXmlFile(XSharedStrings* self, const XString* filePath);

#ifdef __cplusplus
}
#endif
#endif /* XSHAREDSTRINGS_H */
