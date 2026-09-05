/******************************************************************************
 * @file       XCell.c
 * @brief      XCell 单元格类实现（对标 QXlsx::Cell）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCell.h"
#include "XMemory.h"
#include "XUtility.h"
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

XCell* XCell_create_ex(const XString* value, XCell_CellType type, XFormat* format)
{
    XCell* self = XCell_create();
    if (!self) return NULL;
    self->m_cellType = type;
    if (value)
    {
        self->m_value = XString_create();
        if (self->m_value && value) XString_append(self->m_value, value);
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
        if (self->m_value) XCopy(self->m_value, other->m_value);
    }
    if (other->m_format) self->m_format = other->m_format;
    if (other->m_formula) self->m_formula = XCellFormula_copy(other->m_formula);
    if (other->m_richString) {
        self->m_richString = XRichString_create();
        XRichString_copy(self->m_richString, other->m_richString);
    }
    return self;
}

void XCell_delete(XCell* self)
{
    if (self)
    {
        if (self->m_value) XString_delete_base(self->m_value);
        if (self->m_formula) XCellFormula_delete(self->m_formula);
        if (self->m_richString) XRichString_delete(self->m_richString);
        XFree_System(self);
    }
}

/* ========== 访问方法 ========== */

XCell_CellType XCell_cellType(const XCell* self) { return self ? self->m_cellType : XCell_CustomType; }
void XCell_setCellType(XCell* self, XCell_CellType type) { if (self) self->m_cellType = type; }

const XString* XCell_value(const XCell* self)
{
    if (!self || !self->m_value) return NULL;
    return self->m_value;
}

void XCell_setValue(XCell* self, const XString* value)
{
    if (!self) return;
    /* 写入普通值会替换此前的公式或富文本负载。 */
    XCell_setFormula(self, NULL);
    XCell_setRichString(self, NULL);
    if (!self->m_value) self->m_value = XString_create();
    if (self->m_value) { XString_clear_base(self->m_value); if (value) XString_append(self->m_value, value); }
}

XFormat* XCell_format(const XCell* self) { return self ? self->m_format : NULL; }
void XCell_setFormat(XCell* self, XFormat* format) { if (self) self->m_format = format; }

bool XCell_hasFormula(const XCell* self) { return self && self->m_formula != NULL; }
XCellFormula* XCell_formula(const XCell* self) { return self ? self->m_formula : NULL; }
void XCell_setFormula(XCell* self, XCellFormula* formula)
{
    if (!self || self->m_formula == formula) return;
    if (self->m_formula) XCellFormula_delete(self->m_formula);
    self->m_formula = formula;
}

bool XCell_isDateTime(const XCell* self)
{
    if (!self) return false;
    if (self->m_cellType == XCell_DateType) return true;
    if (self->m_format) return XFormat_isDateTimeFormat(self->m_format);
    return false;
}

int64_t XCell_dateTime(const XCell* self, bool date1904)
{
    if (!self) return 0;
    if (!XCell_isDateTime(self)) return 0;
    /* 值为 Excel 序列号（浮点），转换为毫秒时间戳 */
    const XString* val = XCell_value(self);
    if (!val || XString_isEmpty_base(val)) return 0;
    double serial = strtod(XString_toUtf8(val), NULL);
    return XUtility_excelSerialToDateTime(serial, date1904);
}

const XString* XCell_readValue(const XCell* self)
{
    /* 在 C 移植中，值已经以字符串形式存储，readValue 与 value 行为一致 */
    return XCell_value(self);
}

bool XCell_isRichString(const XCell* self) { return self && self->m_richString != NULL; }
XRichString* XCell_richString(const XCell* self) { return self ? self->m_richString : NULL; }
void XCell_setRichString(XCell* self, XRichString* rich)
{
    if (!self || self->m_richString == rich) return;
    if (self->m_richString) XRichString_delete(self->m_richString);
    self->m_richString = rich;
}

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

/* ========== UTF-8 便捷变体 ========== */

XCell* XCell_create_ex_utf8(const char* value, XCell_CellType type, XFormat* format)
{
    XString* s = value ? XString_create_utf8(value) : NULL;
    XCell* result = XCell_create_ex(s, type, format);
    if (s) XString_delete_base(s);
    return result;
}

void XCell_setValue_utf8(XCell* self, const char* value)
{
    XString* s = value ? XString_create_utf8(value) : NULL;
    XCell_setValue(self, s);
    if (s) XString_delete_base(s);
}
