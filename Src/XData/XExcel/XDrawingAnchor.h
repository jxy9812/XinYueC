/******************************************************************************
 * @file       XDrawingAnchor.h
 * @brief      XDrawingAnchor 绘图锚点基类（对标 QXlsx::DrawingAnchor）
 * @author     XinYueC 团队
 * @note       提供锚点基类，包括绝对锚点、单单元格锚点、双单元格锚点。
 *             对齐 QXlsx::DrawingAnchor 全部功能
 ******************************************************************************/
#ifndef XDRAWINGANCHOR_H
#define XDRAWINGANCHOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>#include <stdbool.h>#include <stddef.h>
#include "XString.h"
#include "XByteArray.h"
#include "XChart.h"
#include "XMediaFile.h"
typedef struct XDrawing XDrawing;

typedef struct XlsxMarker {
    int m_row; int m_col; int m_rowOffset; int m_colOffset;
} XlsxMarker;

typedef enum XDrawingAnchor_ObjectType {
    XDAnchor_GraphicFrame, XDAnchor_Shape, XDAnchor_GroupShape,
    XDAnchor_ConnectionShape, XDAnchor_Picture, XDAnchor_Unknown
} XDrawingAnchor_ObjectType;

typedef struct XDrawingAnchor {
    XDrawing* m_drawing;
    XDrawingAnchor_ObjectType m_objectType;
    XMediaFile* m_pictureFile;
    XChart* m_chartFile;
    int m_id;
} XDrawingAnchor;

XDrawingAnchor* XDrawingAnchor_create(XDrawing* drawing, XDrawingAnchor_ObjectType objectType);
void XDrawingAnchor_delete(XDrawingAnchor* self);
void XDrawingAnchor_setPicture(XDrawingAnchor* self, const char* imagePath);
void XDrawingAnchor_setChart(XDrawingAnchor* self, XChart* chart);
int XDrawingAnchor_row(const XDrawingAnchor* self);
int XDrawingAnchor_col(const XDrawingAnchor* self);
int XDrawingAnchor_id(XDrawingAnchor* self);
#ifdef __cplusplus
}
#endif
#endif
