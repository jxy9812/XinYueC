/**
 * @file       XAbstractItemModel.h
 * @brief      XAbstractItemModel 条目模型基类（对标 Qt 6.8 QAbstractItemModel）。
 * @details    提供内存二维条目模型：行/列数、单元格文本读写、行列表头、
 *             数据变化信号。QModelIndex 以 (row,column) 平面索引表达（本库
 *             控件体系沿用 int 索引风格）；树形 parent/children 接口为后续
 *             XTreeView 预留（当前返回 -1 表示根）。XTableWidget 内建本模型，
 *             XTableView/XListView 等视图经 model() 读取数据。
 * @note       模块总开关 XTABLEWIDGET_ON；XAbstractItemModel 为 XObject 派生。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XABSTRACTITEMMODEL_H
#define XABSTRACTITEMMODEL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XVector.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XAbstractItemModel)
XCLASS_DEFINE_EXTEND_END(XAbstractItemModel, XObject)

/**
 * @brief      条目模型对象；m_class 必须是第一个成员（嵌 XObject）。
 * @details    字段含义：
 *             - m_cells：单元格文本向量（行主序 m_rows*m_cols，元素
 *               XString*，对象拥有，可为 NULL）；
 *             - m_hHeader/m_vHeader：列头/行头文本向量（元素 XString*，
 *               对象拥有）；
 *             - m_rows/m_cols：模型维度。
 *             存储容量一律由 XVector 管理，对象不保留裸数组与容量字段。
 *             调用方不得手工修改字段；一律走公开 API。
 */
typedef struct XAbstractItemModel
{
    XObject m_class;    /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XVector m_cells;    /**< 单元格文本向量（行主序 m_rows*m_cols；
                             元素 XString*，对象拥有）。 */
    XVector m_hHeader;  /**< 列头文本向量（元素 XString*，对象拥有）。 */
    XVector m_vHeader;  /**< 行头文本向量（元素 XString*，对象拥有）。 */
    int     m_rows;     /**< 行数。 */
    int     m_cols;     /**< 列数。 */
} XAbstractItemModel;

/* ==================== 生命周期 ==================== */

/** @brief 初始化模型类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XAbstractItemModel_class_init(void);
/** @brief 初始化空模型（0 行 0 列）。 @param self 目标模型；不可为 NULL。 */
void XAbstractItemModel_init(XAbstractItemModel* self);
/** @brief 使用指定内存类型创建模型。
 * @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XAbstractItemModel* XAbstractItemModel_create_ex(XMemoryType memory);
#define XAbstractItemModel_create() \
    XAbstractItemModel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 维度 ==================== */

/** @brief 查询行数（对标 QAbstractItemModel::rowCount）。
 * @param self 目标模型；可为 NULL。
 * @return 当前行数；self 为 NULL 返回 0。
 */
int XAbstractItemModel_rowCount(const XAbstractItemModel* self);
/** @brief 查询列数（对标 columnCount）。
 * @param self 目标模型；可为 NULL。
 * @return 当前列数；self 为 NULL 返回 0。
 */
int XAbstractItemModel_columnCount(const XAbstractItemModel* self);
/** @brief 设置模型维度（扩容时新增单元格为空；缩容裁剪尾部）。
 * @param self 目标模型。
 * @param rows 行数（>=0）。
 * @param cols 列数（>=0）。
 * @return 无返回值（维度变化时发射 rowsInserted/rowsRemoved）。
 */
void XAbstractItemModel_setDimension(XAbstractItemModel* self,
                                     int rows, int cols);
/** @brief 设置行数（对标 Qt 直接扩展模型；不足补空行）。
 * @param self 目标模型。
 * @param rows 行数（>=0）。
 * @return 无返回值。
 */
void XAbstractItemModel_setRowCount(XAbstractItemModel* self, int rows);
/** @brief 设置列数（对标 Qt 直接扩展模型；不足补空列）。
 * @param self 目标模型。
 * @param cols 列数（>=0）。
 * @return 无返回值。
 */
void XAbstractItemModel_setColumnCount(XAbstractItemModel* self, int cols);

/* ==================== 数据 ==================== */

/** @brief 读取单元格文本（内部借用 XString*；不得释放）。
 * @param self 目标模型。
 * @param row 行（越界返回 NULL）。
 * @param col 列（越界返回 NULL）。
 * @return 借用 XString*；空单元格或越界返回 NULL。
 */
const XString* XAbstractItemModel_data(const XAbstractItemModel* self,
                                       int row, int col);
/** @brief 读取单元格文本（UTF-8 借用；越界或空返回空串）。
 * @param self 目标模型；可为 NULL。
 * @param row 行号（越界返回空串）。
 * @param col 列号（越界返回空串）。
 * @return 文本的 UTF-8 借用指针（内部转换缓存，不得释放或修改，随单元
 *         格下次修改失效）；空单元格、越界或 self 为 NULL 返回空串 ""。
 */
const char* XAbstractItemModel_data_2(const XAbstractItemModel* self,
                                      int row, int col);
/** @brief 写入单元格文本（XString 主版本；对标 QAbstractItemModel::setData）。
 * @param self 目标模型。
 * @param row 行（越界返回 false）。
 * @param col 列（越界返回 false）。
 * @param value 借用 XString*；可为 NULL（清空）。
 * @return 成功返回 true（并发射 dataChanged(row,col)）。
 */
bool XAbstractItemModel_setData(XAbstractItemModel* self, int row, int col,
                                const XString* value);
/** @brief 写入单元格文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标模型。
 * @param row 行号（越界返回 false）。
 * @param col 列号（越界返回 false）。
 * @param value UTF-8 文本借用指针（按 UTF-8 解码后深拷贝）；NULL 等价
 *        清空，函数返回后调用方仍可释放或修改源缓冲区。
 * @return 成功返回 true（并发射 dataChanged(row,col)）；解码、内存分配
 *         失败或越界返回 false，此时原单元格内容保持不变。
 */
bool XAbstractItemModel_setData_2(XAbstractItemModel* self, int row, int col,
                                  const char* value);

/* ==================== 表头 ==================== */

/** @brief 读取表头文本（内部借用 XString*；不得释放）。
 * @param self 目标模型。
 * @param section 行列号（行头=行号、列头=列号；越界返回 NULL）。
 * @param orientation 方向（0=水平列头，1=垂直行头，对标 Qt::Orientation 数值）。
 * @return 借用 XString*；未设置或越界返回 NULL。
 */
const XString* XAbstractItemModel_headerData(const XAbstractItemModel* self,
                                             int section, int orientation);
/** @brief 读取表头文本（UTF-8 借用）。
 * @param self 目标模型；可为 NULL。
 * @param section 行列号（行头=行号、列头=列号；越界返回空串）。
 * @param orientation 方向（0=水平列头，1=垂直行头）。
 * @return 表头文本的 UTF-8 借用指针（内部转换缓存，不得释放或修改）；
 *         未设置、越界或 self 为 NULL 返回空串 ""。
 */
const char* XAbstractItemModel_headerData_2(const XAbstractItemModel* self,
                                            int section, int orientation);
/** @brief 设置表头文本（XString 主版本）。
 * @param self 目标模型。
 * @param section 行列号。
 * @param orientation 方向（0=水平列头，1=垂直行头）。
 * @param value 借用 XString*；可为 NULL（清空）。
 * @return 成功返回 true。
 */
bool XAbstractItemModel_setHeaderData(XAbstractItemModel* self, int section,
                                      int orientation, const XString* value);
/** @brief 设置表头文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标模型。
 * @param section 行列号（越界返回 false）。
 * @param orientation 方向（0=水平列头，1=垂直行头）。
 * @param value UTF-8 文本借用指针（按 UTF-8 解码后深拷贝）；NULL 等价
 *        清空，函数返回后调用方仍可释放或修改源缓冲区。
 * @return 成功返回 true；解码、内存分配失败或越界返回 false，此时原表头
 *         内容保持不变。
 */
bool XAbstractItemModel_setHeaderData_2(XAbstractItemModel* self, int section,
                                        int orientation, const char* value);

/* ==================== 树形预留 ==================== */

/** @brief 父索引（对标 QAbstractItemModel::parent；当前扁平模型恒为根 -1）。
 * @param self 目标模型。
 * @param row 子行。
 * @param col 子列。
 * @return 父行号；根或无父返回 -1。
 */
int XAbstractItemModel_parent(const XAbstractItemModel* self,
                              int row, int col);

/* ==================== 信号（对标 QAbstractItemModel） ==================== */

/** @brief dataChanged(row,col) 信号（单元格数据变化时发射）。
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识。
 * @param row 变化单元格的行号。
 * @param col 变化单元格的列号。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_dataChanged_signal(XAbstractItemModel* self,
                                            int row, int col);
/** @brief modelReset() 信号（模型整体重置时发射）。
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_modelReset_signal(XAbstractItemModel* self);
/** @brief rowsInserted(row,count) 信号（行插入后发射）。
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识。
 * @param row 插入起始行号。
 * @param count 插入行数。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_rowsInserted_signal(XAbstractItemModel* self,
                                             int row, int count);
/** @brief rowsRemoved(row,count) 信号（行移除后发射）。
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识。
 * @param row 移除起始行号。
 * @param count 移除行数。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_rowsRemoved_signal(XAbstractItemModel* self,
                                            int row, int count);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTITEMMODEL_H */
