/******************************************************************************
 * @file       XAbstractSheet.h
 * @brief      XAbstractSheet 工作表抽象基类（对标 QXlsx::AbstractSheet）
 * @details    对齐 QXlsx::AbstractSheet；纯 C99 实现，仅依赖 XinYueC 库内
 *             组件（XString/XAbstractOOXmlFile 等）与白名单标准头，
 *             不调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 * @note       提供工作表、图表工作表的抽象基类，包含名称、类型、状态等属性。
 *             对齐 QXlsx::AbstractSheet 全部功能
 ******************************************************************************/
#ifndef XABSTRACTSHEET_H
#define XABSTRACTSHEET_H

#if XEXCEL_ON
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
    XString* m_sheetName;                   /**< 工作表名称；可为 NULL（init 未提供名称时），由本对象拥有并在 deinit 中释放；禁止调用者直接修改或释放 */
    XAbstractSheet_SheetType m_sheetType;   /**< 工作表类型 */
    XAbstractSheet_SheetState m_sheetState; /**< 工作表状态 */
    int m_sheetId;                          /**< 工作表 ID */
    int m_rid;                              /**< workbook.xml.rels 中的 rId；-1 表示未分配 */
    XWorkbook* m_workbook;                  /**< 所属工作簿；可为 NULL，仅借用不归本对象拥有（由 XWorkbook 管理），禁止调用者释放 */
    XDrawing* m_drawing;                    /**< 绘图对象；可为 NULL（保留字段，当前实现未赋值），不归本对象拥有，禁止调用者释放 */
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

/**
 * @brief      设置工作表在 workbook.xml.rels 中的 rId
 * @param self  XAbstractSheet 实例指针
 * @param rid   rId 值；负值表示清空
 * @return      无
 * @note       由 XDocument_saveAs 在生成 rels 时分配并写入，避免 sheet 的
 *             r:id 属性硬编码为 rId1 与 rels 中实际 rId 不一致。
 */
void XAbstractSheet_setRid(XAbstractSheet* self, int rid);

/**
 * @brief      获取工作表名称
 * @param self 指针
 * @return     工作表名称（借用内部对象，禁止释放或修改；对象销毁或
 *             setSheetName 后失效）；未设置名称或 self 为 NULL 时返回 NULL
 * @warning    返回的 XString* 属于对象内部存储，不能调用 XClassDelete 释放。
 */
const XString* XAbstractSheet_sheetName(const XAbstractSheet* self);

/**
 * @brief      获取工作表类型
 * @param self 指针
 * @return     工作表类型
 */
XAbstractSheet_SheetType XAbstractSheet_sheetType(const XAbstractSheet* self);

/**
 * @brief      获取工作表状态
 * @param self 指针
 * @return     工作表状态
 */
XAbstractSheet_SheetState XAbstractSheet_sheetState(const XAbstractSheet* self);

/**
 * @brief      设置工作表状态
 * @param self 指针
 * @param ss   工作表状态
 * @return     无。参数非法时保持原对象不变。
 */
void XAbstractSheet_setSheetState(XAbstractSheet* self, XAbstractSheet_SheetState ss);

/**
 * @brief      判断工作表是否隐藏
 * @param self 指针
 * @return     隐藏返回 true
 */
bool XAbstractSheet_isHidden(const XAbstractSheet* self);

/**
 * @brief      判断工作表是否可见
 * @param self 指针
 * @return     可见返回 true
 */
bool XAbstractSheet_isVisible(const XAbstractSheet* self);

/**
 * @brief      设置工作表隐藏状态
 * @param self   指针
 * @param hidden 是否隐藏
 * @return     无。参数非法时保持原对象不变。
 */
void XAbstractSheet_setHidden(XAbstractSheet* self, bool hidden);

/**
 * @brief      设置工作表可见状态
 * @param self    指针
 * @param visible 是否可见
 * @return     无。参数非法时保持原对象不变。
 */
void XAbstractSheet_setVisible(XAbstractSheet* self, bool visible);

/**
 * @brief      设置工作表名称
 * @param self       指针
 * @param sheetName  新名称
 * @return     无。参数非法时保持原对象不变。
 */
void XAbstractSheet_setSheetName(XAbstractSheet* self, const XString* sheetName);

/**
 * @brief      设置工作表类型
 * @param self 指针
 * @param type 工作表类型
 * @return     无。参数非法时保持原对象不变。
 */
void XAbstractSheet_setSheetType(XAbstractSheet* self, XAbstractSheet_SheetType type);

/**
 * @brief      获取工作表 ID
 * @param self 指针
 * @return     工作表 ID
 */
int XAbstractSheet_sheetId(const XAbstractSheet* self);

/**
 * @brief      获取所属工作簿
 * @param self 指针
 * @return     所属工作簿指针（借用，不归本对象拥有，禁止释放）；
 *             self 为 NULL 时返回 NULL
 */
XWorkbook* XAbstractSheet_workbook(const XAbstractSheet* self);

/**
 * @brief      获取绘图对象
 * @param self 指针
 * @return     绘图对象指针（借用，禁止释放）；当前实现保留字段恒为
 *             NULL，self 为 NULL 时同样返回 NULL
 */
XDrawing* XAbstractSheet_drawing(const XAbstractSheet* self);

#ifdef __cplusplus
}
#endif

#endif /* XEXCEL_ON */

#endif /* XABSTRACTSHEET_H */
