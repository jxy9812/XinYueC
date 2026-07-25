#include "XChartsheet.h"
#include "XWorkbook.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

XChartsheet* XChartsheet_create(const XString* sheetName, int sheetId, void* book, XAbstractOOXmlFile_CreateFlag flag) {
    XChartsheet* self = (XChartsheet*)XMalloc_System(sizeof(XChartsheet));
    if (!self) return NULL; memset(self, 0, sizeof(XChartsheet));
    XAbstractSheet_init(&self->m_base, sheetName, sheetId, (XWorkbook*)book, flag);
    self->m_base.m_sheetType = XAbstractSheet_ST_ChartSheet;
    return self;
}
void XChartsheet_delete(XChartsheet* self) { if (!self) return; XAbstractSheet_deinit(&self->m_base); XFree_System(self); }
void XChartsheet_setChart(XChartsheet* self, XChart* chart) { if (self) self->m_chart = chart; }
XChart* XChartsheet_chart(const XChartsheet* self) { return self ? self->m_chart : NULL; }

/* ========== UTF-8 便捷变体 ========== */

XChartsheet* XChartsheet_create_utf8(const char* sheetName, int sheetId, void* book, XAbstractOOXmlFile_CreateFlag flag)
{
    XString* s = sheetName ? XString_create_utf8(sheetName) : NULL;
    XChartsheet* result = XChartsheet_create(s, sheetId, book, flag);
    if (s) XString_delete_base(s);
    return result;
}
