#include "XSharedStrings.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

XSharedStrings* XSharedStrings_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XSharedStrings* self = (XSharedStrings*)XMalloc_System(sizeof(XSharedStrings));
    if (!self) return NULL;
    memset(self, 0, sizeof(XSharedStrings));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_stringTable = XMap_create();
    self->m_stringList = XVector_create(sizeof(XRichString*));
    self->m_stringCount = 0;
    return self;
}

void XSharedStrings_delete(XSharedStrings* self)
{
    if (!self) return;
    if (self->m_stringTable) { XMap_deinit_base(self->m_stringTable); XFree_System(self->m_stringTable); }
    if (self->m_stringList) { XVector_deinit_base(self->m_stringList); XFree_System(self->m_stringList); }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

int XSharedStrings_count(const XSharedStrings* self) { return self ? (int)XVector_size_base(self->m_stringList) : 0; }
bool XSharedStrings_isEmpty(const XSharedStrings* self) { return self ? XVector_empty_base(self->m_stringList) : true; }

int XSharedStrings_addSharedString(XSharedStrings* self, const char* string)
{
    if (!self || !string) return -1;
    int idx = XSharedStrings_getSharedStringIndex(self, string);
    if (idx >= 0) { self->m_stringCount++; return idx; }
    XRichString* rich = XRichString_create();
    if (!rich) return -1;
    XRichString_setText(rich, string);
    idx = (int)XVector_size_base(self->m_stringList);
    XRichString** p = (XRichString**)XVector_emplace_back_base(self->m_stringList);
    if (p) *p = rich;
    self->m_stringCount++;
    return idx;
}

int XSharedStrings_addSharedRichString(XSharedStrings* self, const XRichString* rich)
{
    if (!self || !rich) return -1;
    int idx = (int)XVector_size_base(self->m_stringList);
    XRichString** p = (XRichString**)XVector_emplace_back_base(self->m_stringList);
    if (p) *p = XRichString_copy(rich);
    self->m_stringCount++;
    return idx;
}

void XSharedStrings_removeSharedString(XSharedStrings* self, const char* string)
{
    if (!self || !string) return;
    int idx = XSharedStrings_getSharedStringIndex(self, string);
    if (idx >= 0) {
        XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, idx);
        if (rich) XRichString_delete(rich);
        XVector_erase_base(self->m_stringList, idx);
    }
}

void XSharedStrings_incRefByStringIndex(XSharedStrings* self, int idx)
{ if (self && idx >= 0) self->m_stringCount++; }

int XSharedStrings_getSharedStringIndex(XSharedStrings* self, const char* string)
{
    if (!self || !string || !self->m_stringList) return -1;
    size_t count = XVector_size_base(self->m_stringList);
    for (size_t i = 0; i < count; i++) {
        XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, i);
        if (rich) { const char* text = XRichString_text(rich); if (text && strcmp(text, string) == 0) return (int)i; }
    }
    return -1;
}

XRichString* XSharedStrings_getSharedString(XSharedStrings* self, int index)
{
    if (!self || !self->m_stringList || index < 0) return NULL;
    size_t count = XVector_size_base(self->m_stringList);
    if ((size_t)index >= count) return NULL;
    return *(XRichString**)XVector_at_base(self->m_stringList, index);
}

XVector* XSharedStrings_getSharedStrings(XSharedStrings* self) { return self ? self->m_stringList : NULL; }
