/******************************************************************************
 * @file       XCellLocation.h
 * @brief      XCellLocation 单元格位置类（对标 QXlsx::CellLocation）
 * @author     XinYueC 团队
 * @note       提供单元格位置信息，包含行列号和单元格指针。
 *             对齐 QXlsx::CellLocation 全部功能
 ******************************************************************************/
#ifndef XCELLLOCATION_H
#define XCELLLOCATION_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 前向声明 */
typedef struct XCell XCell;

/**
 * @brief      XCellLocation 单元格位置结构体
 * @note       包含单元格的行列坐标和对应的单元格指针。
 *             对齐 QXlsx::CellLocation 全部功能。
 */
typedef struct XCellLocation
{
    int m_col;           /**< 列号（1 索引） */
    int m_row;           /**< 行号（1 索引） */
    XCell* m_cell;       /**< 指向单元格对象的指针 */
} XCellLocation;

/**
 * @brief      创建一个空的 XCellLocation 对象
 * @return     空的 XCellLocation 对象
 */
XCellLocation XCellLocation_create(void);

/**
 * @brief      使用行列号和单元格指针创建 XCellLocation
 * @param row  行号（1 索引）
 * @param col  列号（1 索引）
 * @param cell 单元格指针
 * @return     XCellLocation 对象
 */
XCellLocation XCellLocation_create_ex(int row, int col, XCell* cell);

/**
 * @brief      初始化 XCellLocation 对象
 * @param self 待初始化的指针
 */
void XCellLocation_init(XCellLocation* self);

/**
 * @brief      使用行列号和单元格指针初始化
 * @param self 待初始化的指针
 * @param row  行号
 * @param col  列号
 * @param cell 单元格指针
 */
void XCellLocation_init_ex(XCellLocation* self, int row, int col, XCell* cell);

#ifdef __cplusplus
}
#endif
#endif /* XCELLLOCATION_H */
