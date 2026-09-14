#include "XCategoryAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

void XCategoryAxis_init(XCategoryAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    self->m_visible = true;
    self->m_gridVisible = true;
}

int XCategoryAxis_append(XCategoryAxis* self, const char* label)
{
    int idx;
    if (!self || !label) return -1;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        int oldCap = self->m_capacity;
        int ci;
        XString** cat = (XString**)XRealloc_System(self->m_categories,
            sizeof(XString*) * (size_t)cap);
        if (!cat) return -1;
        self->m_categories = cat;
        for (ci = oldCap; ci < cap; ++ci)
            self->m_categories[ci] = NULL;
        self->m_capacity = cap;
    }
    idx = self->m_count;
    self->m_categories[idx] = XString_create_utf8(label);
    self->m_count++;
    return idx;
}

int XCategoryAxis_count(const XCategoryAxis* self)
{ return self ? self->m_count : 0; }

const char* XCategoryAxis_category(const XCategoryAxis* self, int index)
{
    const char* text;
    if (!self || index < 0 || index >= self->m_count ||
        !self->m_categories || !self->m_categories[index])
        return "";
    text = XString_toUtf8(self->m_categories[index]);
    return text ? text : "";
}

void XCategoryAxis_setVisible(XCategoryAxis* self, bool visible)
{ if (self) self->m_visible = visible; }

#endif /* XCHARTS_ON */