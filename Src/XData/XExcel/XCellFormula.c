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
    XCellRange_init(&self->m_reference);
    return self;
}

XCellFormula* XCellFormula_create_ex(const XString* text)
{
    XCellFormula* self = XCellFormula_create();
    if (self && text)
    {
        self->m_text = XString_create_copy(text);
    }
    return self;
}

XCellFormula* XCellFormula_create_typed(const XString* text, XCellFormula_Type type)
{
    XCellFormula* self = XCellFormula_create_ex(text);
    if (self) self->m_type = type;
    return self;
}

XCellFormula* XCellFormula_create_withRef(const XString* text, const XCellRange* ref, XCellFormula_Type type)
{
    XCellFormula* self = XCellFormula_create_ex(text);
    if (self)
    {
        self->m_type = type;
        if (ref) self->m_reference = *ref;
    }
    return self;
}

void XCellFormula_delete(XCellFormula* self)
{
    if (self)
    {
        if (self->m_text) XString_delete_base(self->m_text);
        if (self->m_ca) XString_delete_base(self->m_ca);
        XFree_System(self);
    }
}

bool XCellFormula_isValid(const XCellFormula* self)
{
    return self && self->m_text && XString_size_base(self->m_text) > 0;
}

XCellFormula_Type XCellFormula_formulaType(const XCellFormula* self)
{
    return self ? self->m_type : XCellFormula_Normal;
}

const XString* XCellFormula_formulaText(const XCellFormula* self)
{
    return (self && self->m_text) ? self->m_text : NULL;
}

XCellRange XCellFormula_reference(const XCellFormula* self)
{
    if (!self) { XCellRange r; XCellRange_init(&r); return r; }
    return self->m_reference;
}

int XCellFormula_sharedIndex(const XCellFormula* self)
{
    return self ? self->m_sharedIndex : -1;
}

void XCellFormula_setText(XCellFormula* self, const XString* text)
{
    if (!self) return;
    if (!self->m_text)
    {
        self->m_text = XString_create();
        if (!self->m_text) return;
    }
    XString_clear_base(self->m_text);
    if (text) XString_append(self->m_text, text);
}

void XCellFormula_setType(XCellFormula* self, XCellFormula_Type type)
{
    if (self) self->m_type = type;
}

void XCellFormula_setReference(XCellFormula* self, const XCellRange* ref)
{
    if (self && ref) self->m_reference = *ref;
}

void XCellFormula_setSharedIndex(XCellFormula* self, int index)
{
    if (self) self->m_sharedIndex = index;
}

/* ========== 拷贝 ========== */

XCellFormula* XCellFormula_copy(const XCellFormula* src)
{
    if (!src) return NULL;
    XCellFormula* self = XCellFormula_create();
    if (!self) return NULL;
    if (src->m_text) self->m_text = XString_create_copy(src->m_text);
    self->m_type = src->m_type;
    self->m_sharedIndex = src->m_sharedIndex;
    self->m_reference = src->m_reference;
    if (src->m_ca) self->m_ca = XString_create_copy(src->m_ca);
    return self;
}

/* ========== 比较 ========== */

bool XCellFormula_equals(const XCellFormula* a, const XCellFormula* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->m_type != b->m_type) return false;
    if (a->m_sharedIndex != b->m_sharedIndex) return false;
    if (!XCellRange_equals(&a->m_reference, &b->m_reference)) return false;
    return XString_compare(a->m_text, b->m_text) == XCompare_Equality;
}

bool XCellFormula_notEquals(const XCellFormula* a, const XCellFormula* b)
{
    return !XCellFormula_equals(a, b);
}

/* ========== UTF-8 便捷变体 ========== */

XCellFormula* XCellFormula_create_ex_utf8(const char* text)
{
    XString* s = text ? XString_create_utf8(text) : NULL;
    XCellFormula* result = XCellFormula_create_ex(s);
    if (s) XString_delete_base(s);
    return result;
}

void XCellFormula_setText_utf8(XCellFormula* self, const char* text)
{
    XString* s = text ? XString_create_utf8(text) : NULL;
    XCellFormula_setText(self, s);
    if (s) XString_delete_base(s);
}
