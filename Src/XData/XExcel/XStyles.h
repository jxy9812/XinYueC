/******************************************************************************
 * @file       XStyles.h
 * @brief      XStyles OOXML 样式管理器类
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSTYLES_H
#define XSTYLES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"
#include "XColor.h"
#include "XVector.h"
#include "XMap.h"
#include "XFormat.h"
#include "XAbstractOOXmlFile.h"
/** @brief 格式数字数据结构 */
typedef struct XlsxFormatNumberData { 
    int m_formatIndex;       /**< 格式索引 */
    XString* m_formatString;  /**< 格式字符串 */
} XlsxFormatNumberData;

/** @brief XStyles 样式管理器结构体 */
typedef struct XStyles {
    XAbstractOOXmlFile m_base;             /**< 基类 */
    XVector* m_fontsList;                 /**< 字体列表 */
    XVector* m_fillsList;                 /**< 填充列表 */
    XVector* m_bordersList;                /**< 边框列表 */
    XVector* m_xfFormatsList;              /**< XF格式列表 */
    XVector* m_dxfFormatsList;             /**< DXF格式列表（差分格式） */
    XMap* m_customNumFmtIdMap;             /**< 自定义数字格式ID映射 */
    int m_nextCustomNumFmtId;              /**< 下一个自定义格式ID */
    XColor m_indexedColors[64];            /**< 索引颜色数组 */
    bool m_emptyFormatAdded;                /**< 是否已添加空格式 */
} XStyles;
/**
 * @brief  创建样式管理器对象
 * @param  flag  创建标志（F_NewFromScratch / F_LoadFromExists）
 * @return 新对象指针，失败返回 NULL
 */
XStyles* XStyles_create(XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief  销毁样式管理器并释放所有格式资源
 * @param  self  样式管理器指针
 */
void XStyles_delete(XStyles* self);

/**
 * @brief  添加单元格格式（xf 类型）到样式表
 * @param  self    样式管理器指针
 * @param  format  要添加的格式对象
 * @param  force   为 true 时强制添加（即使已存在相同格式）
 */
void XStyles_addXfFormat(XStyles* self, const XFormat* format, bool force);

/**
 * @brief  按索引获取单元格格式（xf 类型）
 * @param  self  样式管理器指针
 * @param  idx   格式索引
 * @return 格式对象指针，索引无效返回 NULL
 */
XFormat* XStyles_xfFormat(XStyles* self, int idx);

/**
 * @brief  添加差异格式（dxf 类型）到样式表
 * @param  self    样式管理器指针
 * @param  format  要添加的格式对象
 * @param  force   为 true 时强制添加（即使已存在相同格式）
 */
void XStyles_addDxfFormat(XStyles* self, const XFormat* format, bool force);

/**
 * @brief  按索引获取差异格式（dxf 类型）
 * @param  self  样式管理器指针
 * @param  idx   格式索引
 * @return 格式对象指针，索引无效返回 NULL
 */
XFormat* XStyles_dxfFormat(XStyles* self, int idx);

/**
 * @brief  按索引获取调色板颜色
 * @param  self  样式管理器指针
 * @param  idx   颜色索引（0~63）
 * @return 对应的颜色值
 */
XColor XStyles_getColorByIndex(XStyles* self, int idx);
/**
 * @brief  将样式表序列化为 XML 数据
 * @param  self    样式管理器指针
 * @param  outData [out] 接收 XML 数据缓冲区的指针，调用者负责释放
 * @param  outLen  [out] 接收数据长度
 * @return 成功返回 true
 */
bool XStyles_saveToXmlData(const XStyles* self, uint8_t** outData, size_t* outLen);

/**
 * @brief  将样式表保存为 XML 文件（xl/styles.xml）
 * @param  self      样式管理器指针
 * @param  filePath  输出文件路径
 * @return 成功返回 true
 */
bool XStyles_saveToXmlFile(XStyles* self, const XString* filePath);

/**
 * @brief  从 styles.xml 数据加载样式表
 * @param  self 样式管理器指针
 * @param  data XML 数据
 * @param  len  数据长度
 * @return 成功返回 true
 */
bool XStyles_loadFromXmlData(XStyles* self, const uint8_t* data, size_t len);

/**
 * @brief  从 styles.xml 文件加载样式表
 * @param  self     样式管理器指针
 * @param  filePath 文件路径
 * @return 成功返回 true
 */
bool XStyles_loadFromXmlFile(XStyles* self, const XString* filePath);

#ifdef __cplusplus
}
#endif
#endif /* XSTYLES_H */
