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
#include "XClass.h"
#include "XUtility.h"
#include "XXmlStreamReader.h"
#include <string.h>
#include <stdlib.h>


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
    if (!self->m_sheets || !self->m_sharedStrings || !self->m_styles || !self->m_theme ||
        !self->m_mediaFiles || !self->m_chartFiles || !self->m_defineNames ||
        !self->m_defaultDateFormat) {
        XWorkbook_delete(self);
        return NULL;
    }
    return self;
}

void XWorkbook_delete(XWorkbook* self)
{
    if (!self) return;
    /* 释放工作表（根据类型调用正确的析构函数） */
    for (size_t i = 0; self->m_sheets && i < XVector_size_base(self->m_sheets); ++i) {
        XAbstractSheet* sheet = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (sheet) {
            if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) {
                XChartsheet_delete((XChartsheet*)sheet);
            } else {
                XWorksheet_delete((XWorksheet*)sheet);
            }
        }
    }
    if (self->m_sheets) XVector_delete_base(self->m_sheets);
    if (self->m_sharedStrings) XSharedStrings_delete(self->m_sharedStrings);
    if (self->m_styles) XStyles_delete(self->m_styles);
    if (self->m_theme) { XTheme_delete(self->m_theme); }
    if (self->m_mediaFiles) XVector_delete_base(self->m_mediaFiles);
    if (self->m_chartFiles) XVector_delete_base(self->m_chartFiles);
    /* 释放定义名称 */
    if (self->m_defineNames) {
        for (size_t i = 0; i < XVector_size_base(self->m_defineNames); ++i) {
            XWorkbook_DefineName* dn = (XWorkbook_DefineName*)XVector_at_base(self->m_defineNames, i);
            if (dn->m_name) XString_delete_base(dn->m_name);
            if (dn->m_formula) XString_delete_base(dn->m_formula);
            if (dn->m_comment) XString_delete_base(dn->m_comment);
            if (dn->m_scope) XString_delete_base(dn->m_scope);
        }
        XVector_delete_base(self->m_defineNames);
    }
    if (self->m_defaultDateFormat) XString_delete_base(self->m_defaultDateFormat);
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

/* ========== 工作表管理 ========== */

int XWorkbook_sheetCount(const XWorkbook* self) {
    return self && self->m_sheets ? (int)XVector_size_base(self->m_sheets) : 0;
}

XAbstractSheet* XWorkbook_sheet(const XWorkbook* self, int index)
{
    if (!self || !self->m_sheets || index < 0 || (size_t)index >= XVector_size_base(self->m_sheets)) return NULL;
    return *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)index);
}

static bool workbook_sheet_name_exists(const XWorkbook* self, const XString* name,
                                       const XAbstractSheet* ignored)
{
    if (!self || !self->m_sheets || !name) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_sheets); ++i) {
        XAbstractSheet* sheet = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (sheet && sheet != ignored && sheet->m_sheetName &&
            XString_equals(sheet->m_sheetName, name, XChar_CaseInsensitive)) return true;
    }
    return false;
}

static XString* workbook_default_sheet_name(const XWorkbook* self)
{
    for (int suffix = 1; suffix < INT32_MAX; ++suffix) {
        XString* candidate = XString_create_fmt_utf8("Sheet%d", suffix);
        if (!candidate) return NULL;
        if (!workbook_sheet_name_exists(self, candidate, NULL)) return candidate;
        XString_delete_base(candidate);
    }
    return NULL;
}

static XString* workbook_copy_sheet_name(const XWorkbook* self, const XString* sourceName)
{
    for (int suffix = 1; suffix < INT32_MAX; ++suffix) {
        char suffixText[32];
        snprintf(suffixText, sizeof(suffixText), suffix == 1 ? "_copy" : "_copy%d", suffix);
        size_t suffixLength = strlen(suffixText);
        size_t baseLength = suffixLength < 31 ? 31 - suffixLength : 0;
        XString* base = sourceName ? XString_left(sourceName, baseLength)
                                   : XString_create_utf8("Sheet");
        XString* candidate = base ? XString_create_copy(base) : NULL;
        if (base) XString_delete_base(base);
        if (!candidate) return NULL;
        XString_append_utf8(candidate, suffixText);
        if (XUtility_isValidSheetName(candidate) &&
            !workbook_sheet_name_exists(self, candidate, NULL)) return candidate;
        XString_delete_base(candidate);
    }
    return NULL;
}

XAbstractSheet* XWorkbook_addSheet(XWorkbook* self, const XString* name, XAbstractSheet_SheetType type)
{
    if (!self || !self->m_sheets ||
        (type != XAbstractSheet_ST_WorkSheet && type != XAbstractSheet_ST_ChartSheet)) return NULL;
    XAbstractSheet* sheet = NULL;
    XString* tempName = NULL;
    const XString* useName = name;
    if (!name || XString_size_base((XString*)name) == 0) {
        tempName = workbook_default_sheet_name(self);
        useName = tempName;
    }
    if (!useName || !XUtility_isValidSheetName(useName) ||
        workbook_sheet_name_exists(self, useName, NULL)) {
        if (tempName) XString_delete_base(tempName);
        return NULL;
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
    if (!XVector_push_back_2(self->m_sheets, &sheet, 1)) {
        if (type == XAbstractSheet_ST_ChartSheet) XChartsheet_delete((XChartsheet*)sheet);
        else XWorksheet_delete((XWorksheet*)sheet);
        return NULL;
    }
    self->m_nextSheetId++;
    return sheet;
}

XAbstractSheet* XWorkbook_insertSheet(XWorkbook* self, int index, const XString* name, XAbstractSheet_SheetType type)
{
    if (!self || index < 0 || index > XWorkbook_sheetCount(self)) return NULL;
    int oldActiveIndex = self->m_activeSheetIndex;
    int oldCount = XWorkbook_sheetCount(self);
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
    if (oldCount > 0 && index <= oldActiveIndex) self->m_activeSheetIndex = oldActiveIndex + 1;
    return sheet;
}

bool XWorkbook_renameSheet(XWorkbook* self, int index, const XString* name)
{
    XAbstractSheet* sheet = XWorkbook_sheet(self, index);
    if (!sheet || !name || !XUtility_isValidSheetName(name) ||
        workbook_sheet_name_exists(self, name, sheet)) return false;
    XAbstractSheet_setSheetName(sheet, name);
    return true;
}

bool XWorkbook_deleteSheet(XWorkbook* self, int index)
{
    if (!self || !self->m_sheets || index < 0 || (size_t)index >= XVector_size_base(self->m_sheets)) return false;
    XAbstractSheet* sheet = *(XAbstractSheet**)XVector_at_base(self->m_sheets, (size_t)index);
    if (sheet) { if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) { XChartsheet_delete((XChartsheet*)sheet); } else { XWorksheet_delete((XWorksheet*)sheet); } }
    XVector_removeAt_base(self->m_sheets, (size_t)index);
    int remaining = XWorkbook_sheetCount(self);
    if (remaining == 0) self->m_activeSheetIndex = 0;
    else if (self->m_activeSheetIndex > index) self->m_activeSheetIndex--;
    else if (self->m_activeSheetIndex >= remaining) self->m_activeSheetIndex = remaining - 1;
    return true;
}

bool XWorkbook_copySheet(XWorkbook* self, int index, const XString* newName)
{
    XAbstractSheet* src = XWorkbook_sheet(self, index);
    if (!src) return false;
    XString* tempName = NULL;
    const XString* useName = newName;
    if (!newName || XString_size_base((XString*)newName) == 0) {
        tempName = workbook_copy_sheet_name(self, src->m_sheetName);
        useName = tempName;
    }
    if (!useName || !XUtility_isValidSheetName(useName) ||
        workbook_sheet_name_exists(self, useName, NULL)) {
        if (tempName) XString_delete_base(tempName);
        return false;
    }
    int newId = self->m_nextSheetId;
    XAbstractSheet* newSheet = NULL;
    if (src->m_sheetType == XAbstractSheet_ST_ChartSheet) {
        XChartsheet* chartsheet = XChartsheet_create(useName, newId, self,
            XAbstractOOXmlFile_F_NewFromScratch);
        if (chartsheet) {
            XChart* sourceChart = ((XChartsheet*)src)->m_chart;
            chartsheet->m_chart = sourceChart ? XChart_copy(sourceChart, &chartsheet->m_base) : NULL;
            chartsheet->m_ownsChart = chartsheet->m_chart != NULL;
            newSheet = &chartsheet->m_base;
        }
    } else {
        newSheet = (XAbstractSheet*)XWorksheet_copy((XWorksheet*)src, useName, newId);
    }
    if (tempName) XString_delete_base(tempName);
    if (!newSheet || !XVector_push_back_2(self->m_sheets, &newSheet, 1)) {
        if (newSheet) {
            if (newSheet->m_sheetType == XAbstractSheet_ST_ChartSheet)
                XChartsheet_delete((XChartsheet*)newSheet);
            else XWorksheet_delete((XWorksheet*)newSheet);
        }
        return false;
    }
    self->m_nextSheetId++;
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
    if (self->m_activeSheetIndex == srcIndex) self->m_activeSheetIndex = distIndex;
    else if (srcIndex < self->m_activeSheetIndex && self->m_activeSheetIndex <= distIndex)
        self->m_activeSheetIndex--;
    else if (distIndex <= self->m_activeSheetIndex && self->m_activeSheetIndex < srcIndex)
        self->m_activeSheetIndex++;
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
    if (!self || !self->m_defineNames || !name || !formula ||
        XString_isEmpty_base((const XContainer*)name) ||
        XString_isEmpty_base((const XContainer*)formula)) return false;
    XWorkbook_DefineName dn;
    memset(&dn, 0, sizeof(dn));
    dn.m_name = XString_create_copy(name);
    dn.m_formula = XString_create_copy(formula);
    if (comment) { dn.m_comment = XString_create_copy(comment); }
    if (scope) { dn.m_scope = XString_create_copy(scope); }
    if (!dn.m_name || !dn.m_formula || (comment && !dn.m_comment) ||
        (scope && !dn.m_scope) || !XVector_push_back_2(self->m_defineNames, &dn, 1)) {
        if (dn.m_name) XString_delete_base(dn.m_name);
        if (dn.m_formula) XString_delete_base(dn.m_formula);
        if (dn.m_comment) XString_delete_base(dn.m_comment);
        if (dn.m_scope) XString_delete_base(dn.m_scope);
        return false;
    }
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
    if (!self || !self->m_mediaFiles || !media) return;
    if (!force) {
        size_t count = XVector_size_base(self->m_mediaFiles);
        const uint8_t* contents = XMediaFile_contents(media);
        size_t contentsSize = XMediaFile_contentsSize(media);
        for (size_t i = 0; i < count; ++i) {
            XMediaFile* existing = *(XMediaFile**)XVector_at_base(self->m_mediaFiles, i);
            if (existing == media) {
                XMediaFile_setIndex(media, (int)i);
                return;
            }
            if (!existing || XMediaFile_contentsSize(existing) != contentsSize) continue;
            const uint8_t* existingContents = XMediaFile_contents(existing);
            bool contentEqual = contentsSize == 0 ||
                (contents && existingContents &&
                 memcmp(contents, existingContents, contentsSize) == 0);
            if (contentEqual) {
                XMediaFile_setIndex(media, (int)i);
                return;
            }
        }
    }
    int index = (int)XVector_size_base(self->m_mediaFiles);
    if (XVector_push_back_2(self->m_mediaFiles, &media, 1))
        XMediaFile_setIndex(media, index);
}

XMediaFile** XWorkbook_mediaFiles(const XWorkbook* self, int* count) {
    if (count) *count = (self && self->m_mediaFiles) ? (int)XVector_size_base(self->m_mediaFiles) : 0;
    return (self && self->m_mediaFiles) ? (XMediaFile**)XVector_data(self->m_mediaFiles) : NULL;
}

void XWorkbook_addChartFile(XWorkbook* self, XChart* chartFile) {
    if (!self || !self->m_chartFiles || !chartFile) return;
    for (size_t i = 0; i < XVector_size_base(self->m_chartFiles); ++i) {
        XChart* existing = *(XChart**)XVector_at_base(self->m_chartFiles, i);
        if (existing == chartFile) return;
    }
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
    if (!self || sheetId <= 0) return NULL;
    for (int i = 0; i < XWorkbook_sheetCount(self); ++i) {
        XAbstractSheet* existing = XWorkbook_sheet(self, i);
        if (existing && existing->m_sheetId == sheetId) return NULL;
    }
    XAbstractSheet* sheet = XWorkbook_addSheet(self, name, type);
    if (!sheet) return NULL;
    sheet->m_sheetId = sheetId;
    if (sheetId >= self->m_nextSheetId) self->m_nextSheetId = sheetId + 1;
    return sheet;
}


/* ========== XML 序列化 ========== */

static void workbookAppendEscaped(XByteArray* output, const XString* value)
{
    const char* text = value ? XString_toUtf8(value) : "";
    if (!text) return;
    for (const char* p = text; *p; ++p) {
        switch (*p) {
        case '&': XByteArray_append_utf8(output, "&amp;"); break;
        case '<': XByteArray_append_utf8(output, "&lt;"); break;
        case '>': XByteArray_append_utf8(output, "&gt;"); break;
        case '\"': XByteArray_append_utf8(output, "&quot;"); break;
        case '\'': XByteArray_append_utf8(output, "&apos;"); break;
        default: XByteArray_push_back_1(output, (uint8_t)*p); break;
        }
    }
}

static const XString* workbookAttribute(const XXmlStreamAttributes* attributes, const char* name)
{
    if (!attributes || !name) return NULL;
    XString_Init_Utf8(attributeName, name);
    const XString* value = XXmlStreamAttributes_value_ex(attributes, NULL, attributeName);
    XString_deinit_base(attributeName);
    if (!value) {
        /* OOXML workbook 的关系属性使用限定名 r:id。 */
        XString_Init_Utf8(qualifiedName, "r:id");
        value = XXmlStreamAttributes_value(attributes, qualifiedName);
        XString_deinit_base(qualifiedName);
    }
    return value;
}

static int workbookRidFromString(const XString* value)
{
    const char* text = value ? XString_toUtf8(value) : NULL;
    if (!text) return -1;
    if (XString_startsWith_utf8(value, "rId", XChar_CaseSensitive)) text += 3;
    char* end = NULL;
    long rid = strtol(text, &end, 10);
    return end && *end == '\0' && rid >= 0 && rid <= INT32_MAX ? (int)rid : -1;
}

static void workbookClearSheets(XWorkbook* self)
{
    while (self && XWorkbook_sheetCount(self) > 0) XWorkbook_deleteSheet(self, 0);
    if (self) {
        self->m_activeSheetIndex = 0;
        self->m_nextSheetId = 1;
    }
}

static void workbookClearDefinedNames(XWorkbook* self)
{
    if (!self || !self->m_defineNames) return;
    while (XVector_size_base(self->m_defineNames) > 0) {
        size_t index = XVector_size_base(self->m_defineNames) - 1;
        XWorkbook_DefineName* name = (XWorkbook_DefineName*)XVector_at_base(self->m_defineNames, index);
        if (name) {
            if (name->m_name) XString_delete_base(name->m_name);
            if (name->m_formula) XString_delete_base(name->m_formula);
            if (name->m_comment) XString_delete_base(name->m_comment);
            if (name->m_scope) XString_delete_base(name->m_scope);
        }
        XVector_pop_back_base(self->m_defineNames);
    }
}

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
    {
        char view[96];
        snprintf(view, sizeof(view), "  <bookViews><workbookView activeTab=\"%d\"/></bookViews>\n",
                 self->m_activeSheetIndex);
        XByteArray_append_utf8(buf, view);
    }
    
    /* 写入 sheets */
    XByteArray_append_utf8(buf, "  <sheets>\n");
    int sheetCount = self->m_sheets ? (int)XVector_size_base(self->m_sheets) : 0;
    for (int i = 0; i < sheetCount; i++) {
        XAbstractSheet* sheet = *(XAbstractSheet**)XVector_at_base(self->m_sheets, i);
        if (!sheet) continue;
        
        char sheetEntry[256];
        const char* sheetType = "worksheet";
        if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) sheetType = "chartsheet";
        const char* sheetState = "visible";
        if (sheet->m_sheetState == XAbstractSheet_SS_Hidden) sheetState = "hidden";
        else if (sheet->m_sheetState == XAbstractSheet_SS_VeryHidden) sheetState = "veryHidden";
        
        /*
         * 优先使用 XAbstractSheet_setRid 由 XDocument_saveAs 分配的 rId；
         * 未分配（m_rid < 0）时退回到 sheetId 兼容旧调用。
         */
        int sheetRid = sheet->m_rid >= 0 ? sheet->m_rid : (i + 1);
        XByteArray_append_utf8(buf, "    <sheet name=\"");
        workbookAppendEscaped(buf, sheet->m_sheetName);
        snprintf(sheetEntry, sizeof(sheetEntry),
            "\" sheetId=\"%d\" r:id=\"rId%d\" state=\"%s\" type=\"%s\"/>\n",
            sheet->m_sheetId, sheetRid, sheetState, sheetType);
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
            XByteArray_append_utf8(buf, "    <definedName name=\"");
            workbookAppendEscaped(buf, dn->m_name);
            XByteArray_append_utf8(buf, "\"");
            if (dn->m_comment && XString_size_base(dn->m_comment) > 0) {
                XByteArray_append_utf8(buf, " comment=\"");
                workbookAppendEscaped(buf, dn->m_comment);
                XByteArray_append_utf8(buf, "\"");
            }
            if (dn->m_scope && XString_size_base(dn->m_scope) > 0) {
                for (int sheetIndex = 0; sheetIndex < sheetCount; ++sheetIndex) {
                    XAbstractSheet* scopeSheet = XWorkbook_sheet(self, sheetIndex);
                    if (scopeSheet && scopeSheet->m_sheetName &&
                        XString_equals(scopeSheet->m_sheetName, dn->m_scope, XChar_CaseSensitive)) {
                        char scope[64];
                        snprintf(scope, sizeof(scope), " localSheetId=\"%d\"", sheetIndex);
                        XByteArray_append_utf8(buf, scope);
                        break;
                    }
                }
            }
            XByteArray_append_utf8(buf, ">");
            workbookAppendEscaped(buf, dn->m_formula);
            XByteArray_append_utf8(buf, "</definedName>\n");
        }
        XByteArray_append_utf8(buf, "  </definedNames>\n");
    }
    
    XByteArray_append_utf8(buf, "</workbook>\n");
    
    *outData = (uint8_t*)XMalloc_System(XByteArray_size_base(buf) + 1);
    if (*outData) {
        memcpy(*outData, XByteArray_data(buf), XByteArray_size_base(buf));
        *outLen = XByteArray_size_base(buf);
        (*outData)[*outLen] = '\0';
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
        if (file) XClass_delete_base((XClass*)file);
        XFree_System(data);
        return false;
    }
    bool result = XIODevice_write_1((XIODevice*)file, (const char*)data,
        (int64_t)len) == (int64_t)len;
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    XFree_System(data);
    return result;
}

bool XWorkbook_loadFromXmlData(XWorkbook* self, const uint8_t* data, size_t len)
{
    if (!self || !data || len == 0) return false;
    XByteArray* xml = XByteArray_create_with_data((const char*)data, len);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!xml || !reader) {
        if (xml) XByteArray_delete_base(xml);
        if (reader) XXmlStreamReader_delete_base(reader);
        return false;
    }

    workbookClearSheets(self);
    workbookClearDefinedNames(self);
    XXmlStreamReader_addData(reader, xml);
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        if (token != XXmlStream_StartElement) continue;
        const XString* element = XXmlStreamReader_name_const(reader);
        const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
        if (!element) continue;

        if (XString_equals_utf8(element, "workbookPr", XChar_CaseSensitive)) {
            const XString* value = workbookAttribute(attributes, "date1904");
            self->m_date1904 = value &&
                (XString_equals_utf8(value, "1", XChar_CaseSensitive) ||
                 XString_equals_utf8(value, "true", XChar_CaseInsensitive));
        } else if (XString_equals_utf8(element, "workbookView", XChar_CaseSensitive)) {
            const XString* value = workbookAttribute(attributes, "activeTab");
            if (value) self->m_activeSheetIndex = atoi(XString_toUtf8(value));
        } else if (XString_equals_utf8(element, "sheet", XChar_CaseSensitive)) {
            const XString* name = workbookAttribute(attributes, "name");
            const XString* id = workbookAttribute(attributes, "sheetId");
            const XString* rid = workbookAttribute(attributes, "id");
            const XString* type = workbookAttribute(attributes, "type");
            const XString* state = workbookAttribute(attributes, "state");
            if (!name) continue;
            XAbstractSheet_SheetType sheetType = type &&
                XString_equals_utf8(type, "chartsheet", XChar_CaseInsensitive)
                ? XAbstractSheet_ST_ChartSheet : XAbstractSheet_ST_WorkSheet;
            XAbstractSheet* sheet = XWorkbook_addSheet(self, name, sheetType);
            if (!sheet) continue;
            if (id) sheet->m_sheetId = atoi(XString_toUtf8(id));
            sheet->m_rid = workbookRidFromString(rid);
            if (state && XString_equals_utf8(state, "veryHidden", XChar_CaseInsensitive))
                sheet->m_sheetState = XAbstractSheet_SS_VeryHidden;
            else if (state && XString_equals_utf8(state, "hidden", XChar_CaseInsensitive))
                sheet->m_sheetState = XAbstractSheet_SS_Hidden;
            else
                sheet->m_sheetState = XAbstractSheet_SS_Visible;
            if (sheet->m_sheetId >= self->m_nextSheetId) self->m_nextSheetId = sheet->m_sheetId + 1;
        } else if (XString_equals_utf8(element, "definedName", XChar_CaseSensitive)) {
            const XString* nameAttr = workbookAttribute(attributes, "name");
            const XString* commentAttr = workbookAttribute(attributes, "comment");
            const XString* localSheetId = workbookAttribute(attributes, "localSheetId");
            XString* name = nameAttr ? XString_create_copy(nameAttr) : NULL;
            XString* comment = commentAttr ? XString_create_copy(commentAttr) : NULL;
            XString* scope = NULL;
            if (localSheetId) {
                XAbstractSheet* scopeSheet = XWorkbook_sheet(self, atoi(XString_toUtf8(localSheetId)));
                if (scopeSheet && scopeSheet->m_sheetName) scope = XString_create_copy(scopeSheet->m_sheetName);
            }
            const XString* formula = XXmlStreamReader_readElementText_const(reader,
                XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
            if (name && formula) XWorkbook_defineName(self, name, formula, comment, scope);
            if (name) XString_delete_base(name);
            if (comment) XString_delete_base(comment);
            if (scope) XString_delete_base(scope);
        }
    }

    bool ok = !XXmlStreamReader_hasError(reader) && XWorkbook_sheetCount(self) > 0;
    if (self->m_activeSheetIndex < 0 || self->m_activeSheetIndex >= XWorkbook_sheetCount(self))
        self->m_activeSheetIndex = 0;
    XXmlStreamReader_delete_base(reader);
    XByteArray_delete_base(xml);
    return ok;
}

bool XWorkbook_loadFromXmlFile(XWorkbook* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2(filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* data = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    bool result = data && XWorkbook_loadFromXmlData(self, XByteArray_data(data), XByteArray_size_base(data));
    if (data) XByteArray_delete_base(data);
    return result;
}

/* ========== UTF-8 便捷变体 ========== */

XAbstractSheet* XWorkbook_addSheet_utf8(XWorkbook* self, const char* name, XAbstractSheet_SheetType type)
{
    XString* s = name ? XString_create_utf8(name) : NULL;
    XAbstractSheet* result = XWorkbook_addSheet(self, s, type);
    if (s) XString_delete_base(s);
    return result;
}
