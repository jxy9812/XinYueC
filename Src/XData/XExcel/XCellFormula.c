/******************************************************************************
 * @file       XCellFormula.c
 * @brief      XCellFormula 单元格公式类实现
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCellFormula.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

XCellFormula* XCellFormula_create(void)
{
    XCellFormula* self = (XCellFormula*)XMalloc_System(sizeof(XCellFormula));
    if (!self) return NULL;
    memset(self, 0, sizeof(XCellFormula));
    self->m_sharedIndex = -1;
    self->m_type = XCellFormula_Normal;
    XCellReference_init(&self->m_reference);
    return self;
}

XCellFormula* XCellFormula_create_ex(const char* text)
{
    XCellFormula* self = XCellFormula_create();
    if (self && text)
    {
        XString* str = XString_create();
        if (str)
        {
            XString_append_utf8(str, text);
            self->m_text = str;
        }
    }
    return self;
}

void XCellFormula_delete(XCellFormula* self)
{
    if (self)
    {
        if (self->m_text) { XString_deinit_base(self->m_text); XFree_System(self->m_text); }
        if (self->m_ca) { XString_deinit_base(self->m_ca); XFree_System(self->m_ca); }
        XFree_System(self);
    }
}

bool XCellFormula_isValid(const XCellFormula* self)
{
    return self && self->m_text && XString_size_base(self->m_text) > 0;
}

const char* XCellFormula_text(const XCellFormula* self)
{
    return (self && self->m_text) ? XString_toUtf8_const(self->m_text) : "";
}

void XCellFormula_setText(XCellFormula* self, const char* text)
{
    if (!self) return;
    if (!self->m_text)
    {
        self->m_text = XString_create();
        if (!self->m_text) return;
    }
    XString_clear_base(self->m_text);
    if (text) XString_append_utf8(self->m_text, text);
}

XCellFormula_Type XCellFormula_type(const XCellFormula* self)
{
    return self ? self->m_type : XCellFormula_Normal;
}

void XCellFormula_setType(XCellFormula* self, XCellFormula_Type type)
{
    if (self) self->m_type = type;
}
