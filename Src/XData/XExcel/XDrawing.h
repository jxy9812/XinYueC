/******************************************************************************
 * @file       XDrawing.h
 * @brief      XDrawing 绘图容器类（对标 QXlsx::Drawing）
 * @author     XinYueC 团队
 * @note       提供绘图容器，管理锚点对象。
 *             对齐 QXlsx::Drawing 全部功能
 ******************************************************************************/
#ifndef XDRAWING_H
#define XDRAWING_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XAbstractOOXmlFile.h"
#include "XVector.h"
typedef struct XDrawingAnchor XDrawingAnchor;
typedef struct XAbstractSheet XAbstractSheet;
typedef struct XWorkbook XWorkbook;
/** @brief XDrawing 绘图容器结构体 */
typedef struct XDrawing {
    XAbstractOOXmlFile m_base;      /**< 基类 */
    XAbstractSheet* m_sheet;         /**< 所属工作表 */
    XWorkbook* m_workbook;          /**< 所属工作簿 */
    XVector* m_anchors;            /**< 锚点列表 */
} XDrawing;
/**
 * @brief  创建绘图容器对象
 * @param  sheet  所属工作表指针
 * @param  flag   创建标志（F_NewFromScratch / F_LoadFromExists）
 * @return 新对象指针，失败返回 NULL
 */
XDrawing* XDrawing_create(XAbstractSheet* sheet, XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief  销毁绘图容器并释放所有锚点资源
 * @param  self  绘图容器指针
 */
void XDrawing_delete(XDrawing* self);

/**
 * @brief  将绘图容器保存为 UTF-8 XML 数据
 * @param  self    绘图容器指针
 * @param  data    输出缓冲区，调用者使用 XFree_System 释放
 * @param  length  输出字节数，不包含结尾 NUL
 * @return 成功返回 true
 */
bool XDrawing_saveToXmlData(const XDrawing* self, uint8_t** data, size_t* length);

/**
 * @brief  从指定长度的 UTF-8 XML 数据加载绘图容器
 * @param  self    绘图容器指针
 * @param  data    输入缓冲区，不要求以 NUL 结尾
 * @param  length  输入字节数
 * @return 成功返回 true
 */
bool XDrawing_loadFromXmlData(XDrawing* self, const uint8_t* data, size_t length);

/**
 * @brief  将绘图容器保存为 XML 文件（xl/drawings/drawingN.xml）
 * @param  self      绘图容器指针
 * @param  filePath  输出文件路径
 * @return 成功返回 true
 */
bool XDrawing_saveToXmlFile(XDrawing* self, const XString* filePath);

/**
 * @brief  从 XML 文件加载绘图容器
 * @param  self      绘图容器指针
 * @param  filePath  输入文件路径
 * @return 成功返回 true
 */
bool XDrawing_loadFromXmlFile(XDrawing* self, const XString* filePath);
#ifdef __cplusplus
}
#endif
#endif
