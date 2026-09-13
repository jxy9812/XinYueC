#include "XCategoryAxis.h"
#include "XMemory.h"
#include <string.h>

#if XCHARTS_ON

void XCategoryAxis_init(XCategoryAxis* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    self->m_visible = true;
    self->m_gridVisible = true;
}

int XCategoryAxis_append(XCategoryAxis* self, const char* label)
{
    int idx;
    if (!self || !label) return -1;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        char (*cat)[64] = (char(*)[64])XRealloc_System(self->m_categories,
            sizeof(char[64]) * (size_t)cap);
        if (!cat) return -1;
        self->m_categories = cat;
        self->m_capacity = cap;
    }
    idx = self->m_count;
    memset(self->m_categories[idx], 0, 64);
    strncpy(self->m_categories[idx], label, 63);
    self->m_count++;
    return idx;
}

int XCategoryAxis_count(const XCategoryAxis* self)
{ return self ? self->m_count : 0; }

const char* XCategoryAxis_category(const XCategoryAxis* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return "";
    return self->m_categories[index];
}

void XCategoryAxis_setVisible(XCategoryAxis* self, bool visible)
{ if (self) self->m_visible = visible; }

#endif /* XCHARTS_ON */