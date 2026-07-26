#include "XAbstractSheet.h"
#include "XMemory.h"
#include <stdlib.h>

#include <string.h>


void XAbstractSheet_init(XAbstractSheet* self, const XString* sheetName, int sheetId, XWorkbook* book, XAbstractOOXmlFile_CreateFlag flag)
{
    if (!self) return;
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_sheetType = XAbstractSheet_ST_WorkSheet;
    self->m_sheetState = XAbstractSheet_SS_Visible;
    self->m_sheetId = sheetId;
    /* 默认 -1 表示 rId 未分配，由 XDocument_saveAs 在生成 rels 时回填 */
    self->m_rid = -1;
    self->m_workbook = book;
    if (sheetName)
    {
        self->m_sheetName = XString_create();
        if (self->m_sheetName && sheetName) XString_append(self->m_sheetName, sheetName);
    }
}

void XAbstractSheet_setRid(XAbstractSheet* self, int rid)
{
    if (!self) return;
    self->m_rid = rid;
}

void XAbstractSheet_deinit(XAbstractSheet* self)
{
    if (!self) return;
    if (self->m_sheetName) XString_delete_base(self->m_sheetName);
    XAbstractOOXmlFile_deinit(&self->m_base);
}

const XString* XAbstractSheet_sheetName(const XAbstractSheet* self)
{ return (self && self->m_sheetName) ? self->m_sheetName : NULL; }
XAbstractSheet_SheetType XAbstractSheet_sheetType(const XAbstractSheet* self) { return self ? self->m_sheetType : XAbstractSheet_ST_WorkSheet; }
XAbstractSheet_SheetState XAbstractSheet_sheetState(const XAbstractSheet* self) { return self ? self->m_sheetState : XAbstractSheet_SS_Visible; }
void XAbstractSheet_setSheetState(XAbstractSheet* self, XAbstractSheet_SheetState ss) { if (self) self->m_sheetState = ss; }
bool XAbstractSheet_isHidden(const XAbstractSheet* self) { return self && self->m_sheetState != XAbstractSheet_SS_Visible; }
bool XAbstractSheet_isVisible(const XAbstractSheet* self) { return self && self->m_sheetState == XAbstractSheet_SS_Visible; }
void XAbstractSheet_setHidden(XAbstractSheet* self, bool hidden) { if (self) self->m_sheetState = hidden ? XAbstractSheet_SS_Hidden : XAbstractSheet_SS_Visible; }
void XAbstractSheet_setVisible(XAbstractSheet* self, bool visible) { if (self) self->m_sheetState = visible ? XAbstractSheet_SS_Visible : XAbstractSheet_SS_Hidden; }
void XAbstractSheet_setSheetName(XAbstractSheet* self, const XString* sheetName)
{
    if (!self) return;
    if (!self->m_sheetName) self->m_sheetName = XString_create();
    if (self->m_sheetName) { XString_clear_base(self->m_sheetName); if (sheetName) XString_append(self->m_sheetName, sheetName); }
}
void XAbstractSheet_setSheetType(XAbstractSheet* self, XAbstractSheet_SheetType type) { if (self) self->m_sheetType = type; }
int XAbstractSheet_sheetId(const XAbstractSheet* self) { return self ? self->m_sheetId : -1; }
XWorkbook* XAbstractSheet_workbook(const XAbstractSheet* self) { return self ? self->m_workbook : NULL; }
XDrawing* XAbstractSheet_drawing(const XAbstractSheet* self) { return self ? self->m_drawing : NULL; }
