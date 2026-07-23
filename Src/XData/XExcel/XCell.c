/******************************************************************************
 * @file       XCell.c
 * @brief      XCell 单元格类实现（对标 QXlsx::Cell）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCell.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

/* ========== 创建与初始化 ========== */

XCell* XCell_create(void)
{
    XCell* self = (XCell*)XMalloc_System(sizeof(XCell));
    if (!self) return NULL;
    memset(self, 0, sizeof(XCell));
    self->m_cellType = XCell_CustomType;
    self->m_styleNumber = -1;
    return self;
}

XCell* XCell_create_ex(const char* value, XCell_CellType type, XFormat* format)
{
    XCell* self = XCell_create();
    if (!self) return NULL;
    self->m_cellType = type;
    if (value)
    {
        self->m_value = XString_create();
        if (self->m_value) XString_append_utf8(self->m_value, value);
    }
    if (format) self->m_format = format;
    return self;
}

XCell* XCell_copy(const XCell* other)
{
    if (!other) return NULL;
    XCell* self = XCell_create();
    if (!self) return NULL;
    self->m_cellType = other->m_cellType;
    self->m_styleNumber = other->m_styleNumber;
    self->m_row = other->m_row;
    self->m_column = other->m_column;
    if (other->m_value)
    {
        self->m_value = XString_create();
        if (self->m_value) XString_copy_base(self->m_value, other->m_value);
    }
    if (other->m_format) self->m_format = other->m_format;
    if (other->m_formula) self->m_formula = other->m_formula;
    if (other->m_richString) self->m_richString = other->m_richString;
    return self;
}

void XCell_delete(XCell* self)
{
    if (self)
    {
        if (self->m_value) { XString_deinit_base(self->m_value); XFree_System(self->m_value); }
        XFree_System(self);
    }
}

/* ========== 访问方法 ========== */

XCell_CellType XCell_cellType(const XCell* self) { return self ? self->m_cellType : XCell_CustomType; }
void XCell_setCellType(XCell* self, XCell_CellType type) { if (self) self->m_cellType = type; }

const char* XCell_value(const XCell* self)
{ return (self && self->m_value) ? XString_toUtf8_const(self->m_value) : ""; }

void XCell_setValue(XCell* self, const char* value)
{
    if (!self) return;
    if (!self->m_value) self->m_value = XString_create();
    if (self->m_value) { XString_clear_base(self->m_value); XString_append_utf8(self->m_value, value); }
}

XFormat* XCell_format(const XCell* self) { return self ? self->m_format : NULL; }
void XCell_setFormat(XCell* self, XFormat* format) { if (self) self->m_format = format; }

bool XCell_hasFormula(const XCell* self) { return self && self->m_formula != NULL; }
XCellFormula* XCell_formula(const XCell* self) { return self ? self->m_formula : NULL; }
void XCell_setFormula(XCell* self, XCellFormula* formula) { if (self) self->m_formula = formula; }

bool XCell_isDateTime(const XCell* self)
{
    if (!self) return false;
    if (self->m_cellType == XCell_DateType) return true;
    if (self->m_format) return XFormat_isDateTimeFormat(self->m_format);
    return false;
}

bool XCell_isRichString(const XCell* self) { return self && self->m_richString != NULL; }
XRichString* XCell_richString(const XCell* self) { return self ? self->m_richString : NULL; }
void XCell_setRichString(XCell* self, XRichString* rich) { if (self) self->m_richString = rich; }

int XCell_styleNumber(const XCell* self) { return self ? self->m_styleNumber : -1; }
void XCell_setStyleNumber(XCell* self, int style) { if (self) self->m_styleNumber = style; }
int XCell_row(const XCell* self) { return self ? self->m_row : -1; }
void XCell_setRow(XCell* self, int row) { if (self) self->m_row = row; }
int XCell_column(const XCell* self) { return self ? self->m_column : -1; }
void XCell_setColumn(XCell* self, int column) { if (self) self->m_column = column; }

/* ========== 静态方法 ========== */

bool XCell_isDateType(XCell_CellType cellType, const XFormat* format)
{
    if (cellType == XCell_DateType) return true;
    if (cellType == XCell_NumberType && format) return XFormat_isDateTimeFormat(format);
    return false;
}
