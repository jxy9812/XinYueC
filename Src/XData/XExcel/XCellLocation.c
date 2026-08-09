/******************************************************************************
 * @file       XCellLocation.c
 * @brief      XCellLocation 单元格位置类实现
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCellLocation.h"
#include <stdlib.h>

#include <string.h>


XCellLocation XCellLocation_create(void)
{
    XCellLocation loc = { 0, 0, NULL };
    return loc;
}

XCellLocation XCellLocation_create_ex(int row, int col, XCell* cell)
{
    XCellLocation loc = { col, row, cell };
    return loc;
}

void XCellLocation_init(XCellLocation* self)
{
    if (self)
    {
        self->m_col = 0;
        self->m_row = 0;
        self->m_cell = NULL;
    }
}

void XCellLocation_init_ex(XCellLocation* self, int row, int col, XCell* cell)
{
    if (self)
    {
        self->m_col = col;
        self->m_row = row;
        self->m_cell = cell;
    }
}
