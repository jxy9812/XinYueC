/******************************************************************************
 * @file       XWorkbook.c
 * @brief      XWorkbook 工作簿类实现（对标 QXlsx::Workbook）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWorkbook.h"
#include "XWorksheet.h"
#include "XChartsheet.h"
#include "XMemory.h"
#include "XFile.h"
#include <string.h>


/* ========== 创建与初始化 ========== */

XWorkbook* XWorkbook_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XWorkbook* self = (XWorkbook*)XMalloc_System(sizeof(XWorkbook));
    if (!self) return NULL;
    memset(self, 0, sizeof(XWorkbook));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_sheets = XVector_Create(XAbstractSheet*);
    self->m_sharedStrings = XSharedStrings_create(flag);
    self->m_styles = XStyles_create(flag);
    self->m_theme = XTheme_create(flag);
    self->m_mediaFiles = XVector_Create(XMediaFile*);
    self->m_chartFiles = XVector_Create(XChart*);
    self->m_defineNames = XVector_Create(XWorkbook_DefineName);
    self->m_activeSheetIndex = 0;
    self->m_nextSheetId = 1;
    self->m_defaultDateFormat = XString_create();
    if (self->m_defaultDateFormat) XString_append_utf8(self->m_defaultDateFormat, "yyyy-MM-dd");
    return self;
}

void XWorkbook_delete(XWorkbook* self)
{
    if (!self) return;
    /* 释放工作表（根据类型调用正确的析构函数） */
    for (size_t i = 0; i < XVector_size_base(self->m_sheets); ++i) {
        XAbstractSheet* sheet = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (sheet) {
            if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) {
                XChartsheet_delete((XChartsheet*)sheet);
            } else {
                XWorksheet_delete((XWorksheet*)sheet);
            }
        }
    }
    if (self->m_sheets) { XVector_deinit_base(self->m_sheets); XFree_System(self->m_sheets); }
    if (self->m_sharedStrings) XSharedStrings_delete(self->m_sharedStrings);
    if (self->m_styles) XStyles_delete(self->m_styles);
    if (self->m_theme) { XTheme_delete(self->m_theme); }
    if (self->m_mediaFiles) { XVector_deinit_base(self->m_mediaFiles); XFree_System(self->m_mediaFiles); }
    if (self->m_chartFiles) { XVector_deinit_base(self->m_chartFiles); XFree_System(self->m_chartFiles); }
    /* 释放定义名称 */
    if (self->m_defineNames) {
        for (size_t i = 0; i < XVector_size_base(self->m_defineNames); ++i) {
            XWorkbook_DefineName* dn = (XWorkbook_DefineName*)XVector_at_base(self->m_defineNames, i);
            if (dn->m_name) { XString_deinit_base(dn->m_name); XFree_System(dn->m_name); }
            if (dn->m_formula) { XString_deinit_base(dn->m_formula); XFree_System(dn->m_formula); }
            if (dn->m_comment) { XString_deinit_base(dn->m_comment); XFree_System(dn->m_comment); }
            if (dn->m_scope) { XString_deinit_base(dn->m_scope); XFree_System(dn->m_scope); }
        }
        XVector_deinit_base(self->m_defineNames); XFree_System(self->m_defineNames);
    }
    if (self->m_defaultDateFormat) { XString_deinit_base(self->m_defaultDateFormat); XFree_System(self->m_defaultDateFormat); }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

/* ========== 工作表管理 ========== */

int XWorkbook_sheetCount(const XWorkbook* self) { return self ? (int)XVector_size_base(self->m_sheets) : 0; }

XAbstractSheet* XWorkbook_sheet(const XWorkbook* self, int index)
{
    if (!self || !self->m_sheets || index < 0 || (size_t)index >= XVector_size_base(self->m_sheets)) return NULL;
    return *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)index);
}

XAbstractSheet* XWorkbook_addSheet(XWorkbook* self, const XString* name, XAbstractSheet_SheetType type)
{
    if (!self) return NULL;
    XAbstractSheet* sheet = NULL;
    XString* tempName = NULL;
    const XString* useName = name;
    if (!name || XString_size_base((XString*)name) == 0) {
        tempName = XString_create_fmt_utf8("Sheet%d", self->m_nextSheetId);
        useName = tempName;
    }
    if (type == XAbstractSheet_ST_ChartSheet) {
        XChartsheet* cs = XChartsheet_create(useName, self->m_nextSheetId, self, XAbstractOOXmlFile_F_NewFromScratch);
        sheet = (XAbstractSheet*)cs;
    } else {
        XWorksheet* ws = XWorksheet_create(useName, self->m_nextSheetId, self, XAbstractOOXmlFile_F_NewFromScratch);
        sheet = (XAbstractSheet*)ws;
    }
    if (tempName) XString_delete_base(tempName);
    if (!sheet) return NULL;
    sheet->m_sheetType = type;
    XVector_push_back_2(self->m_sheets, &sheet, 1);
    self->m_nextSheetId++;
    return sheet;
}

XAbstractSheet* XWorkbook_insertSheet(XWorkbook* self, int index, const XString* name, XAbstractSheet_SheetType type)
{
    if (!self || index < 0) return NULL;
    XAbstractSheet* sheet = XWorkbook_addSheet(self, name, type);
    if (!sheet) return NULL;
    /* 移动到指定位置 */
    size_t lastIdx = XVector_size_base(self->m_sheets) - 1;
    if ((size_t)index < lastIdx) {
        for (size_t i = lastIdx; i > (size_t)index; --i) {
            XAbstractSheet* tmp = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
            *(XAbstractSheet**)XVector_at_base(self->m_sheets, i) = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i - 1);
            *(XAbstractSheet**)XVector_at_base(self->m_sheets, i - 1) = tmp;
        }
    }
    return sheet;
}

bool XWorkbook_renameSheet(XWorkbook* self, int index, const XString* name)
{
    XAbstractSheet* sheet = XWorkbook_sheet(self, index);
    if (!sheet) return false;
    XAbstractSheet_setSheetName(sheet, name);
    return true;
}

bool XWorkbook_deleteSheet(XWorkbook* self, int index)
{
    if (!self || !self->m_sheets || index < 0 || (size_t)index >= XVector_size_base(self->m_sheets)) return false;
    XAbstractSheet* sheet = *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)index);
    if (sheet) { if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) { XChartsheet_delete((XChartsheet*)sheet); } else { XWorksheet_delete((XWorksheet*)sheet); } }
    {
    XVector_removeAt_base(self->m_sheets, (size_t)index);
}
    return true;
}

bool XWorkbook_copySheet(XWorkbook* self, int index, const XString* newName)
{
    XAbstractSheet* src = XWorkbook_sheet(self, index);
    if (!src) return false;
    XString* tempName = NULL;
    const XString* useName = newName;
    if (!newName || XString_size_base((XString*)newName) == 0) {
        tempName = XString_create();
        if (src->m_sheetName) XString_append(tempName, src->m_sheetName);
        XString_append_utf8(tempName, "_copy");
        useName = tempName;
    }
    XAbstractSheet* newSheet = XWorkbook_addSheet(self, useName, src->m_sheetType);
    if (tempName) XString_delete_base(tempName);
    if (!newSheet) return false;
    newSheet->m_sheetState = src->m_sheetState;
    return true;
}

bool XWorkbook_moveSheet(XWorkbook* self, int srcIndex, int distIndex)
{
    if (!self || !self->m_sheets) return false;
    size_t n = XVector_size_base(self->m_sheets);
    if (srcIndex < 0 || (size_t)srcIndex >= n || distIndex < 0 || (size_t)distIndex >= n) return false;
    if (srcIndex == distIndex) return true;
    XAbstractSheet* tmp = *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)srcIndex);
    if (srcIndex < distIndex) {
        for (int i = srcIndex; i < distIndex; ++i)
            *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)i) = *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)(i + 1));
    } else {
        for (int i = srcIndex; i > distIndex; --i)
            *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)i) = *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)(i - 1));
    }
    *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)distIndex) = tmp;
    return true;
}

XAbstractSheet* XWorkbook_activeSheet(const XWorkbook* self)
{
    return XWorkbook_sheet(self, self ? self->m_activeSheetIndex : 0);
}

bool XWorkbook_setActiveSheet(XWorkbook* self, int index)
{
    if (!self) return false;
    XAbstractSheet* sheet = XWorkbook_sheet(self, index);
    if (!sheet) return false;
    self->m_activeSheetIndex = index;
    return true;
}

/* ========== 定义名称 ========== */

bool XWorkbook_defineName(XWorkbook* self, const XString* name, const XString* formula, const XString* comment, const XString* scope)
{
    if (!self || !name || !formula) return false;
    XWorkbook_DefineName dn;
    memset(&dn, 0, sizeof(dn));
    dn.m_name = XString_create_copy(name);
    dn.m_formula = XString_create_copy(formula);
    if (comment) { dn.m_comment = XString_create_copy(comment); }
    if (scope) { dn.m_scope = XString_create_copy(scope); }
    XVector_push_back_2(self->m_defineNames, &dn, 1);
    return true;
}

/* ========== 属性 ========== */
bool XWorkbook_isDate1904(const XWorkbook* self) { return self ? self->m_date1904 : false; }
void XWorkbook_setDate1904(XWorkbook* self, bool date1904) { if (self) self->m_date1904 = date1904; }
bool XWorkbook_isStringsToNumbersEnabled(const XWorkbook* self) { return self ? self->m_stringsToNumbers : false; }
void XWorkbook_setStringsToNumbersEnabled(XWorkbook* self, bool enable) { if (self) self->m_stringsToNumbers = enable; }
bool XWorkbook_isStringsToHyperlinksEnabled(const XWorkbook* self) { return self ? self->m_stringsToHyperlinks : false; }
void XWorkbook_setStringsToHyperlinksEnabled(XWorkbook* self, bool enable) { if (self) self->m_stringsToHyperlinks = enable; }
bool XWorkbook_isHtmlToRichStringEnabled(const XWorkbook* self) { return self ? self->m_htmlToRichString : false; }
void XWorkbook_setHtmlToRichStringEnabled(XWorkbook* self, bool enable) { if (self) self->m_htmlToRichString = enable; }
const XString* XWorkbook_defaultDateFormat(const XWorkbook* self) { return (self && self->m_defaultDateFormat) ? self->m_defaultDateFormat : NULL; }
void XWorkbook_setDefaultDateFormat(XWorkbook* self, const XString* format) {
    if (!self) return;
    if (!self->m_defaultDateFormat) self->m_defaultDateFormat = XString_create();
    if (self->m_defaultDateFormat) { XString_clear_base(self->m_defaultDateFormat); if (format) XString_append(self->m_defaultDateFormat, format); }
}
void XWorkbook_setWriteDatesAsText(XWorkbook* self, bool enable) { if (self) self->m_writeDatesAsText = enable; }
bool XWorkbook_writeDatesAsText(const XWorkbook* self) { return self ? self->m_writeDatesAsText : false; }

/* ========== 内部接口 ========== */
XSharedStrings* XWorkbook_sharedStrings(const XWorkbook* self) { return self ? self->m_sharedStrings : NULL; }
XStyles* XWorkbook_styles(XWorkbook* self) { return self ? self->m_styles : NULL; }
XTheme* XWorkbook_theme(const XWorkbook* self) { return self ? self->m_theme : NULL; }

void XWorkbook_addMediaFile(XWorkbook* self, XMediaFile* media, bool force) {
    if (!self || !media) return;
    (void)force;
    XVector_push_back_2(self->m_mediaFiles, &media, 1);
}

XMediaFile** XWorkbook_mediaFiles(const XWorkbook* self, int* count) {
    if (count) *count = (self && self->m_mediaFiles) ? (int)XVector_size_base(self->m_mediaFiles) : 0;
    return (self && self->m_mediaFiles) ? (XMediaFile**)XVector_data(self->m_mediaFiles) : NULL;
}

void XWorkbook_addChartFile(XWorkbook* self, XChart* chartFile) {
    if (!self || !chartFile) return;
    XVector_push_back_2(self->m_chartFiles, &chartFile, 1);
}

XChart** XWorkbook_chartFiles(const XWorkbook* self, int* count) {
    if (count) *count = (self && self->m_chartFiles) ? (int)XVector_size_base(self->m_chartFiles) : 0;
    return (self && self->m_chartFiles) ? (XChart**)XVector_data(self->m_chartFiles) : NULL;
}

XAbstractSheet** XWorkbook_getSheetsByTypes(const XWorkbook* self, XAbstractSheet_SheetType type, int* count)
{
    if (!self || !count) return NULL;
    int cnt = 0;
    for (size_t i = 0; i < XVector_size_base(self->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (s->m_sheetType == type) cnt++;
    }
    *count = cnt;
    if (cnt == 0) return NULL;
    XAbstractSheet** result = (XAbstractSheet**)XMalloc_System(sizeof(XAbstractSheet*) * (size_t)cnt);
    if (!result) return NULL;
    int idx = 0;
    for (size_t i = 0; i < XVector_size_base(self->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (s->m_sheetType == type) result[idx++] = s;
    }
    return result;
}

XString** XWorkbook_worksheetNames(const XWorkbook* self, int* count)
{
    if (!self || !count) return NULL;
    int cnt = 0;
    for (size_t i = 0; i < XVector_size_base(self->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (s->m_sheetType == XAbstractSheet_ST_WorkSheet) cnt++;
    }
    *count = cnt;
    if (cnt == 0) return NULL;
    XString** result = (XString**)XMalloc_System(sizeof(XString*) * (size_t)cnt);
    if (!result) return NULL;
    int idx = 0;
    for (size_t i = 0; i < XVector_size_base(self->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (s->m_sheetType == XAbstractSheet_ST_WorkSheet) {
            result[idx] = s->m_sheetName;
            idx++;
        }
    }
    return result;
}

XAbstractSheet* XWorkbook_addSheetEx(XWorkbook* self, const XString* name, int sheetId, XAbstractSheet_SheetType type)
{
    (void)sheetId;
    return XWorkbook_addSheet(self, name, type);
}


/* ========== XML 序列化 ========== */

bool XWorkbook_saveToXmlData(const XWorkbook* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    XByteArray* buf = XByteArray_create();
    if (!buf) return false;
    
    XByteArray_append_utf8(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(buf, "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"");
    XByteArray_append_utf8(buf, " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n");
    
    /* 写入 workbookPr */
    XByteArray_append_utf8(buf, "  <workbookPr date1904=\"");
    XByteArray_append_utf8(buf, self->m_date1904 ? "true" : "false");
    XByteArray_append_utf8(buf, "\"/>\n");
    
    /* 写入 sheets */
    XByteArray_append_utf8(buf, "  <sheets>\n");
    int sheetCount = self->m_sheets ? (int)XVector_size_base(self->m_sheets) : 0;
    for (int i = 0; i < sheetCount; i++) {
        XAbstractSheet* sheet = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (!sheet) continue;
        
        char sheetEntry[512];
        const char* name = sheet->m_sheetName ? XString_toUtf8(sheet->m_sheetName) : "";
        const char* sheetType = "worksheet";
        if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) sheetType = "chartsheet";
        
        snprintf(sheetEntry, sizeof(sheetEntry), 
            "    <sheet name=\"%s\" sheetId=\"%d\" r:id=\"rId%d\" type=\"%s\"/>\n",
            name, i + 1, i + 1, sheetType);
        XByteArray_append_utf8(buf, sheetEntry);
    }
    XByteArray_append_utf8(buf, "  </sheets>\n");
    
    /* 写入 definedNames (如果有) */
    if (self->m_defineNames && XVector_size_base(self->m_defineNames) > 0) {
        XByteArray_append_utf8(buf, "  <definedNames>\n");
        size_t dnCount = XVector_size_base(self->m_defineNames);
        for (size_t i = 0; i < dnCount; i++) {
            XWorkbook_DefineName* dn = (XWorkbook_DefineName*)XVector_at_base(self->m_defineNames, i);
            if (!dn || !dn->m_name) continue;
            char dnEntry[512];
            snprintf(dnEntry, sizeof(dnEntry), 
                "    <definedName name=\"%s\" localSheetId=\"0\">%s</definedName>\n",
                XString_toUtf8(dn->m_name),
                dn->m_formula ? XString_toUtf8(dn->m_formula) : "");
            XByteArray_append_utf8(buf, dnEntry);
        }
        XByteArray_append_utf8(buf, "  </definedNames>\n");
    }
    
    XByteArray_append_utf8(buf, "</workbook>\n");
    
    *outData = (uint8_t*)XMalloc_System(XByteArray_size_base(buf) + 1);
    if (*outData) {
        memcpy(*outData, XByteArray_data(buf), XByteArray_size_base(buf));
        *outLen = XByteArray_size_base(buf);
    }
    XByteArray_delete_base(buf);
    return *outData != NULL;
}

bool XWorkbook_saveToXmlFile(XWorkbook* self, const XString* filePath)
{
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XWorkbook_saveToXmlData(self, &data, &len)) return false;
    
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        if (file) XFile_deleteLater(file);
        XFree_System(data);
        return false;
    }
    XIODevice_write_1((XIODevice*)file, data, (int64_t)len);
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    XFree_System(data);
    return true;
}

bool XWorkbook_loadFromXmlData(XWorkbook* self, const uint8_t* data, size_t len)
{
    (void)self; (void)data; (void)len;
    /* TODO: 实现 XML 解析 */
    return false;
}

bool XWorkbook_loadFromXmlFile(XWorkbook* self, const XString* filePath)
{
    (void)self; (void)filePath;
    /* TODO: 实现 XML 文件解析 */
    return false;
}

/* ========== UTF-8 便捷变体 ========== */

XAbstractSheet* XWorkbook_addSheet_utf8(XWorkbook* self, const char* name, XAbstractSheet_SheetType type)
{
    XString* s = name ? XString_create_utf8(name) : NULL;
    XAbstractSheet* result = XWorkbook_addSheet(self, s, type);
    if (s) XString_delete_base(s);
    return result;
}
