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
#include <stdint.h>#include <stdbool.h>#include <stddef.h>
#include "XAbstractOOXmlFile.h"
#include "XVector.h"
typedef struct XDrawingAnchor XDrawingAnchor;
typedef struct XAbstractSheet XAbstractSheet;
typedef struct XWorkbook XWorkbook;
typedef struct XDrawing {
    XAbstractOOXmlFile m_base;
    XAbstractSheet* m_sheet;
    XWorkbook* m_workbook;
    XVector* m_anchors;
} XDrawing;
XDrawing* XDrawing_create(XAbstractSheet* sheet, XAbstractOOXmlFile_CreateFlag flag);
void XDrawing_delete(XDrawing* self);
bool XDrawing_saveToXmlFile(XDrawing* self, const char* filePath);
bool XDrawing_loadFromXmlFile(XDrawing* self, const char* filePath);
#ifdef __cplusplus
}
#endif
#endif
