/******************************************************************************
 * @file       XDrawingAnchor.h
 * @brief      XDrawingAnchor 绘图锚点类（对标 QXlsx::DrawingAnchor）
 * @author     XinYueC 团队
 * @note       提供锚点基类，包括绝对锚点，单单元格锚点，双单元格锚点。
 *             对齐 QXlsx::DrawingAnchor 全部功能
 ******************************************************************************/
#ifndef XDRAWINGANCHOR_H
#define XDRAWINGANCHOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"
#include "XChart.h"
typedef struct XDrawing XDrawing;

/** @brief 锚点标记（单元格位置+偏移）*/
#include "XMediaFile.h"
typedef struct XlsxMarker {
    int m_row;       /**< 行号（0索引）*/
    int m_col;       /**< 列号（0索引）*/
    int m_rowOffset; /**< 行偏移（EMU）*/
    int m_colOffset; /**< 列偏移（EMU）*/
} XlsxMarker;

typedef enum XDrawingAnchor_ObjectType {
    XDAnchor_GraphicFrame = 0,  /**< 图形框架（图表）*/
    XDAnchor_Shape = 1,         /**< 形状*/
    XDAnchor_GroupShape = 2,     /**< 组图形*/
    XDAnchor_ConnectionShape = 3, /**< 连接符*/
    XDAnchor_Picture = 4,        /**< 图片*/
    XDAnchor_Unknown = 5         /**< 未知类型*/
} XDrawingAnchor_ObjectType;

typedef enum XDrawingAnchor_Type {
    XDAnchor_Absolute = 0,   /**< 绝对锚点*/
    XDAnchor_OneCell = 1,    /**< 单单元格锚点*/
    XDAnchor_TwoCell = 2     /**< 双单元格锚点*/
} XDrawingAnchor_Type;

/** @brief XDrawingAnchor 锚点基类*/
typedef struct XDrawingAnchor {
    XDrawing* m_drawing;
    XDrawingAnchor_ObjectType m_objectType; /**< 对象类型*/
    XDrawingAnchor_Type m_anchorType;       /**< 锚点类型*/
    XMediaFile* m_pictureFile;
    XChart* m_chartFile;
    int m_id;

    /* 位置信息 */
    int m_col;        /**< 列号（单/双单元格锚点）*/
    int m_row;        /**< 行号（单/双单元格锚点）*/
    int m_colOffset;  /**< 列偏移（EMU）*/
    int m_rowOffset;  /**< 行偏移（EMU）*/

    /* 扩展尺寸（EMU）*/
    int m_width;   /**< 宽度（EMU）*/
    int m_height;  /**< 高度（EMU）*/

    /* 双单元格锚点目标位置 */
    int m_toCol;       /**< 目标列号*/
    int m_toRow;       /**< 目标行号*/
    int m_toColOffset; /**< 目标列偏移（EMU）*/
    int m_toRowOffset; /**< 目标行偏移（EMU）*/

    /* 绝对锚点坐标 */
    int m_x;  /**< 绝对X坐标（EMU）*/
    int m_y;  /**< 绝对Y坐标（EMU）*/
} XDrawingAnchor;

/* ========== 创建与删除 ========== */
XDrawingAnchor* XDrawingAnchor_create(XDrawing* drawing, XDrawingAnchor_ObjectType objectType);
void XDrawingAnchor_delete(XDrawingAnchor* self);

/* ========== 对象设置 ========== */
void XDrawingAnchor_setPicture(XDrawingAnchor* self, const char* imagePath);
bool XDrawingAnchor_setPictureFromData(XDrawingAnchor* self, const uint8_t* data, size_t len, const char* mimeType);
void XDrawingAnchor_setChart(XDrawingAnchor* self, XChart* chart);
bool XDrawingAnchor_getPicture(XDrawingAnchor* self, XByteArray* outData);

/* ========== 位置查询 ========== */
int XDrawingAnchor_row(const XDrawingAnchor* self);
int XDrawingAnchor_col(const XDrawingAnchor* self);
int XDrawingAnchor_id(const XDrawingAnchor* self);
XDrawingAnchor_Type XDrawingAnchor_anchorType(const XDrawingAnchor* self);

/* ========== XML 序列化 ========== */
/**
 * @brief     将锚点保存为 XML
 * @param self    锚点指针
 * @param writer XXmlStreamWriter 指针（需已设置设备）
 * @return    成功返回true
 */
bool XDrawingAnchor_saveToXml(const XDrawingAnchor* self, void* writer);
/**
 * @brief     从 XML 加载锚点
 * @param self   锚点指针
 * @param reader XXmlStreamReader 指针
 * @return    成功返回true
 */
bool XDrawingAnchor_loadFromXml(XDrawingAnchor* self, void* reader);

#ifdef __cplusplus
}
#endif
#endif
