#include "XStyles.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

XStyles* XStyles_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XStyles* self = (XStyles*)XMalloc_System(sizeof(XStyles));
    if (!self) return NULL;
    memset(self, 0, sizeof(XStyles));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_fontsList = XVector_create(sizeof(XFormat*));
    self->m_fillsList = XVector_create(sizeof(XFormat*));
    self->m_bordersList = XVector_create(sizeof(XFormat*));
    self->m_xfFormatsList = XVector_create(sizeof(XFormat*));
    self->m_dxfFormatsList = XVector_create(sizeof(XFormat*));
    self->m_customNumFmtIdMap = XMap_create();
    self->m_nextCustomNumFmtId = 164;
    for (int i = 0; i < 64; i++) XColor_create_init(&self->m_indexedColors[i], 0, 0, 0, 0);
    return self;
}

void XStyles_delete(XStyles* self)
{
    if (!self) return;
    if (self->m_fontsList) { XVector_deinit_base(self->m_fontsList); XFree_System(self->m_fontsList); }
    if (self->m_fillsList) { XVector_deinit_base(self->m_fillsList); XFree_System(self->m_fillsList); }
    if (self->m_bordersList) { XVector_deinit_base(self->m_bordersList); XFree_System(self->m_bordersList); }
    if (self->m_xfFormatsList) { XVector_deinit_base(self->m_xfFormatsList); XFree_System(self->m_xfFormatsList); }
    if (self->m_dxfFormatsList) { XVector_deinit_base(self->m_dxfFormatsList); XFree_System(self->m_dxfFormatsList); }
    if (self->m_customNumFmtIdMap) { XMap_deinit_base(self->m_customNumFmtIdMap); XFree_System(self->m_customNumFmtIdMap); }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

void XStyles_addXfFormat(XStyles* self, const XFormat* format, bool force)
{
    if (!self) return;
    XFormat** p = (XFormat**)XVector_emplace_back_base(self->m_xfFormatsList);
    if (p) { *p = XFormat_create(); if (*p && format) XFormat_copy(*p, format); }
}

XFormat* XStyles_xfFormat(XStyles* self, int idx)
{
    if (!self || !self->m_xfFormatsList || idx < 0) return NULL;
    size_t count = XVector_size_base(self->m_xfFormatsList);
    if ((size_t)idx >= count) return NULL;
    return *(XFormat**)XVector_at_base(self->m_xfFormatsList, idx);
}

void XStyles_addDxfFormat(XStyles* self, const XFormat* format, bool force)
{
    if (!self) return;
    XFormat** p = (XFormat**)XVector_emplace_back_base(self->m_dxfFormatsList);
    if (p) { *p = XFormat_create(); if (*p && format) XFormat_copy(*p, format); }
}

XFormat* XStyles_dxfFormat(XStyles* self, int idx)
{
    if (!self || !self->m_dxfFormatsList || idx < 0) return NULL;
    size_t count = XVector_size_base(self->m_dxfFormatsList);
    if ((size_t)idx >= count) return NULL;
    return *(XFormat**)XVector_at_base(self->m_dxfFormatsList, idx);
}

XColor XStyles_getColorByIndex(XStyles* self, int idx)
{
    if (self && idx >= 0 && idx < 64) return self->m_indexedColors[idx];
    XColor c; XColor_create_init(&c, 0, 0, 0, 0); return c;
}
