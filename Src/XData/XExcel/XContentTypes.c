/******************************************************************************
 * @file       XContentTypes.c
 * @brief      XContentTypes OOXML 内容类型管理实现（对标 QXlsx::ContentTypes）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XContentTypes.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

void XContentTypes_addDefault(XContentTypes* self, const char* key, const char* value)
{
    if (!self) return;
    XMap_insert_base(self->m_defaults, key, strlen(key) + 1, (void*)value, strlen(value) + 1);
}

void XContentTypes_addOverride(XContentTypes* self, const char* key, const char* value)
{
    if (!self) return;
    XMap_insert_base(self->m_overrides, key, strlen(key) + 1, (void*)value, strlen(value) + 1);
}

void XContentTypes_addDocPropCore(XContentTypes* self)
{ XContentTypes_addOverride(self, "/docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml"); }

void XContentTypes_addDocPropApp(XContentTypes* self)
{ XContentTypes_addOverride(self, "/docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml"); }

void XContentTypes_addStyles(XContentTypes* self)
{ XContentTypes_addOverride(self, "/xl/styles.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"); }

void XContentTypes_addTheme(XContentTypes* self)
{ XContentTypes_addOverride(self, "/xl/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml"); }

void XContentTypes_addWorkbook(XContentTypes* self)
{ XContentTypes_addOverride(self, "/xl/workbook.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"); }

void XContentTypes_addWorksheetName(XContentTypes* self, const char* name)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/worksheets/sheet%d.xml", self->m_worksheetCount + 1);
    XContentTypes_addOverride(self, key, "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    self->m_worksheetCount++;
}

void XContentTypes_addChartsheetName(XContentTypes* self, const char* name)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/chartsheets/sheet%d.xml", self->m_chartsheetCount + 1);
    XContentTypes_addOverride(self, key, "application/vnd.openxmlformats-officedocument.chartshhet+xml");
    self->m_chartsheetCount++;
}

void XContentTypes_addChartName(XContentTypes* self, const char* name)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/charts/chart%d.xml", self->m_chartCount + 1);
    XContentTypes_addOverride(self, key, "application/vnd.openxmlformats-officedocument.drawingml.chart+xml");
    self->m_chartCount++;
}

void XContentTypes_addDrawingName(XContentTypes* self, const char* name)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/drawings/drawing%d.xml", self->m_drawingCount + 1);
    XContentTypes_addOverride(self, key, "application/vnd.openxmlformats-officedocument.drawing+xml");
    self->m_drawingCount++;
}

void XContentTypes_addCommentName(XContentTypes* self, const char* name)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/comments%d.xml", self->m_commentCount + 1);
    XContentTypes_addOverride(self, key, "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml");
    self->m_commentCount++;
}

void XContentTypes_addTableName(XContentTypes* self, const char* name)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/tables/table%d.xml", self->m_tableCount + 1);
    XContentTypes_addOverride(self, key, "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml");
    self->m_tableCount++;
}

void XContentTypes_addExternalLinkName(XContentTypes* self, const char* name)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/externalLinks/externalLink%d.xml", self->m_externalLinkCount + 1);
    XContentTypes_addOverride(self, key, "application/vnd.openxmlformats-officedocument.spreadsheetml.externalLink+xml");
    self->m_externalLinkCount++;
}

void XContentTypes_addSharedString(XContentTypes* self)
{ XContentTypes_addOverride(self, "/xl/sharedStrings.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"); }

void XContentTypes_addVmlName(XContentTypes* self)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/drawings/vmlDrawing%d.vml", self->m_vmlCount + 1);
    XContentTypes_addOverride(self, key, "application/vnd.openxmlformats-officedocument.vmlDrawing");
    self->m_vmlCount++;
}

void XContentTypes_addCalcChain(XContentTypes* self)
{ XContentTypes_addOverride(self, "/xl/calcChain.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"); }

void XContentTypes_addVbaProject(XContentTypes* self)
{ XContentTypes_addOverride(self, "/xl/vbaProject.bin", "application/vnd.ms-office.vbaProject"); }

void XContentTypes_clearOverrides(XContentTypes* self)
{
    if (self) { XMap_clear_base(self->m_overrides); }
}
