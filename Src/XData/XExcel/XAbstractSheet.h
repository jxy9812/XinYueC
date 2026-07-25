/******************************************************************************
 * @file       XAbstractSheet.h
 * @brief      XAbstractSheet 工作表抽象基类（对标 QXlsx::AbstractSheet）
 * @author     XinYueC 团队
 * @note       提供工作表、图表工作表的抽象基类，包含名称、类型、状态等属性。
 *             对齐 QXlsx::AbstractSheet 全部功能
 ******************************************************************************/
#ifndef XABSTRACTSHEET_H
#define XABSTRACTSHEET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XAbstractOOXmlFile.h"

/* 前向声明 */
typedef struct XDrawing XDrawing;
typedef struct XWorkbook XWorkbook;

/**
 * @brief      工作表类型枚举
 */
typedef enum XAbstractSheet_SheetType
{
    XAbstractSheet_ST_WorkSheet = 0,   /**< 普通工作表 */
    XAbstractSheet_ST_ChartSheet = 1,  /**< 图表工作表 */
    XAbstractSheet_ST_DialogSheet = 2, /**< 对话框工作表 */
    XAbstractSheet_ST_MacroSheet = 3   /**< 宏工作表 */
} XAbstractSheet_SheetType;

/**
 * @brief      工作表状态枚举
 */
typedef enum XAbstractSheet_SheetState
{
    XAbstractSheet_SS_Visible = 0,     /**< 可见 */
    XAbstractSheet_SS_Hidden = 1,      /**< 隐藏 */
    XAbstractSheet_SS_VeryHidden = 2   /**< 深度隐藏 */
} XAbstractSheet_SheetState;

/**
 * @brief      XAbstractSheet 工作表抽象基类
 * @note       继承自 XAbstractOOXmlFile，包含名称、类型、状态和工作簿引用。
 *             对齐 QXlsx::AbstractSheet 全部功能。
 */
typedef struct XAbstractSheet
{
    XAbstractOOXmlFile m_base;              /**< 基类 */
    XString* m_sheetName;                   /**< 工作表名称 */
    XAbstractSheet_SheetType m_sheetType;   /**< 工作表类型 */
    XAbstractSheet_SheetState m_sheetState; /**< 工作表状态 */
    int m_sheetId;                          /**< 工作表 ID */
    XWorkbook* m_workbook;                  /**< 所属工作簿 */
    XDrawing* m_drawing;                    /**< 绘图对象 */
} XAbstractSheet;

/**
 * @brief      初始化 XAbstractSheet 基类
 * @param self      指针
 * @param sheetName 工作表名称
 * @param sheetId   工作表 ID
 * @param book      所属工作簿
 * @param flag      创建标志
 */
void XAbstractSheet_init(XAbstractSheet* self, const XString* sheetName, int sheetId, XWorkbook* book, XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief      释放资源
 * @param self 指针
 */
void XAbstractSheet_deinit(XAbstractSheet* self);

const XString* XAbstractSheet_sheetName(const XAbstractSheet* self);
XAbstractSheet_SheetType XAbstractSheet_sheetType(const XAbstractSheet* self);
XAbstractSheet_SheetState XAbstractSheet_sheetState(const XAbstractSheet* self);
void XAbstractSheet_setSheetState(XAbstractSheet* self, XAbstractSheet_SheetState ss);
bool XAbstractSheet_isHidden(const XAbstractSheet* self);
bool XAbstractSheet_isVisible(const XAbstractSheet* self);
void XAbstractSheet_setHidden(XAbstractSheet* self, bool hidden);
void XAbstractSheet_setVisible(XAbstractSheet* self, bool visible);
void XAbstractSheet_setSheetName(XAbstractSheet* self, const XString* sheetName);
void XAbstractSheet_setSheetType(XAbstractSheet* self, XAbstractSheet_SheetType type);
int XAbstractSheet_sheetId(const XAbstractSheet* self);
XWorkbook* XAbstractSheet_workbook(const XAbstractSheet* self);
XDrawing* XAbstractSheet_drawing(const XAbstractSheet* self);

#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTSHEET_H */
