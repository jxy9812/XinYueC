/******************************************************************************
 * @file       XExcelTest.c
 * @brief      XExcel end-to-end tests.
 *
 * The tests intentionally follow real workbook workflows.  Small object and
 * XML round-trip tests live in XExcelExtendedTest.c; this file focuses on
 * creating useful workbooks, saving them, loading them again, and checking the
 * resulting object graph.
 ******************************************************************************/
#include "XExcelTest.h"
#include "XExcelExtendedTest.h"
#include "XDocument.h"
#include "XCell.h"
#include "XCellReference.h"
#include "XCellRange.h"
#include "XCellFormula.h"
#include "XFormat.h"
#include "XColor.h"
#include "XFile.h"
#include "XWorksheet.h"
#include "XWorkbook.h"
#include "XRichString.h"
#include "XConditionalFormatting.h"
#include "XDataValidation.h"
#include "XChart.h"
#include "XChartsheet.h"
#include "XAbstractSheet.h"
#include "XVariant.h"
#include "XString.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PASS(name) XPrintf("[通过] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[失败] %s：%s\n", name, reason)
#define TEST_INFO(fmt, ...) XPrintf("[信息] " fmt "\n", ##__VA_ARGS__)

static void record_result(bool result, const char* name, bool* all_pass)
{
    (void)name;
    if (result) TEST_PASS("流程步骤完成");
    else {
        TEST_FAIL("流程步骤失败", "请检查上一步操作");
        if (all_pass) *all_pass = false;
    }
}

#define CHECK_OK(expr, name) record_result((expr), (name), &all_pass)

static const char* xexcel_asset_path(const char* name)
{
    static char path[512];
    XString candidate;

    if (!name) return "";

    snprintf(path, sizeof(path), "assets/%s", name);
    XString_init(&candidate);
    XString_assign_utf8(&candidate, path);
    if (XFile_exists_static(&candidate)) {
        XString_deinit_base(&candidate);
        return path;
    }

    snprintf(path, sizeof(path), "../assets/%s", name);
    XString_clear_base(&candidate);
    XString_assign_utf8(&candidate, path);
    if (XFile_exists_static(&candidate)) {
        XString_deinit_base(&candidate);
        return path;
    }

    snprintf(path, sizeof(path), "../../assets/%s", name);
    XString_clear_base(&candidate);
    XString_assign_utf8(&candidate, path);
    if (XFile_exists_static(&candidate)) {
        XString_deinit_base(&candidate);
        return path;
    }

    XString_deinit_base(&candidate);
    return "";
}

static bool xexcel_file_exists(const char* name)
{
    XString* path = name ? XString_create_utf8(name) : NULL;
    bool exists = path && XFile_exists_static(path);
    if (path) XString_delete_base(path);
    return exists;
}

static void xexcel_remove_file(const char* name)
{
    XString* path = name ? XString_create_utf8(name) : NULL;
    if (path) {
        XFile_remove_static(path);
        XString_delete_base(path);
    }
}

static void check_cell_text(XWorksheet* worksheet, int row, int column,
    const char* expected, const char* name, bool* all_pass)
{
    XCell* cell = worksheet ? XWorksheet_cellAt(worksheet, row, column) : NULL;
    const XString* value = cell ? XCell_value(cell) : NULL;
    record_result(value && XString_equals_utf8(value, expected, XChar_CaseSensitive),
        name, all_pass);
}

static XFormat* make_title_format(void)
{
    XFormat* format = XFormat_create();
    XColor fill = XColor_create_rgb(0x1f, 0x4e, 0x78, 0xff);
    XColor font = XColor_create_rgb(0xff, 0xff, 0xff, 0xff);

    if (!format) return NULL;
    XFormat_setFontName_utf8(format, "Arial");
    XFormat_setFontSize(format, 14);
    XFormat_setFontBold(format, true);
    XFormat_setFontColor(format, &font);
    XFormat_setFillPattern(format, XFormat_PatternSolid);
    XFormat_setPatternForegroundColor(format, &fill);
    XFormat_setHorizontalAlignment(format, XFormat_AlignHCenter);
    XFormat_setVerticalAlignment(format, XFormat_AlignVCenter);
    XFormat_setTextWrap(format, true);
    XFormat_setBorderStyle(format, XFormat_BorderThin);
    return format;
}

static XFormat* make_header_format(void)
{
    XFormat* format = XFormat_create();
    XColor fill = XColor_create_rgb(0x5b, 0x9b, 0xd5, 0xff);
    XColor font = XColor_create_rgb(0xff, 0xff, 0xff, 0xff);

    if (!format) return NULL;
    XFormat_setFontBold(format, true);
    XFormat_setFontColor(format, &font);
    XFormat_setFillPattern(format, XFormat_PatternSolid);
    XFormat_setPatternForegroundColor(format, &fill);
    XFormat_setHorizontalAlignment(format, XFormat_AlignHCenter);
    XFormat_setVerticalAlignment(format, XFormat_AlignVCenter);
    XFormat_setTextWrap(format, true);
    XFormat_setBorderStyle(format, XFormat_BorderThin);
    return format;
}

static XFormat* make_body_format(void)
{
    XFormat* format = XFormat_create();
    XColor border = XColor_create_rgb(0xa6, 0xa6, 0xa6, 0xff);

    if (!format) return NULL;
    XFormat_setBorderStyle(format, XFormat_BorderThin);
    XFormat_setBorderColor(format, &border);
    XFormat_setVerticalAlignment(format, XFormat_AlignVCenter);
    return format;
}

static bool test_data_roundtrip_flow(void)
{
    const char* output = "xexcel_data_roundtrip.xlsx";
    bool all_pass = true;
    XDocument* document = NULL;
    XDocument* loaded = NULL;
    XFormat* title = NULL;
    XFormat* header = NULL;
    XFormat* body = NULL;
    XFormat* number = NULL;
    XFormat* date = NULL;
    XRichString* rich = NULL;
    XFormat* richBold = NULL;
    XCellFormula* formula = NULL;
    XVariant* variant = NULL;
    XVariant* readVariant = NULL;

    TEST_INFO("===== 流程1：数据类型与保存加载往返 =====");
    document = XDocument_create();
    if (!document) {
        TEST_FAIL("创建文档", "返回空指针");
        return false;
    }

    CHECK_OK(XDocument_renameSheet_utf8(document, "Sheet1", "Summary"),
        "rename default sheet");
    CHECK_OK(XDocument_addSheet_utf8(document, "Types", XAbstractSheet_ST_WorkSheet),
        "add Types sheet");
    CHECK_OK(XDocument_addSheet_utf8(document, "Notes", XAbstractSheet_ST_WorkSheet),
        "add Notes sheet");
    XDocument_setDocumentProperty_utf8(document, "title", "XinYueC Excel flow");
    CHECK_OK(XDocument_defineName_utf8(document, "SalesTotal", "Summary!$C$3", NULL, NULL),
        "define workbook name");

    title = make_title_format();
    header = make_header_format();
    body = make_body_format();
    number = make_body_format();
    date = make_body_format();
    CHECK_OK(title && header && body && number && date, "create workflow formats");

    XString_Init_Utf8(numberCode, "#,##0.00");
    XString_Init_Utf8(dateCode, "yyyy-mm-dd hh:mm:ss");
    if (number) XFormat_setNumberFormat(number, numberCode);
    if (date) XFormat_setNumberFormat(date, dateCode);

    CHECK_OK(XDocument_selectSheet_utf8(document, "Summary"), "select Summary sheet");
    XWorksheet* summary = XDocument_currentWorksheet(document);
    CHECK_OK(summary != NULL, "get Summary worksheet");
    if (summary) {
        CHECK_OK(XWorksheet_writeString_utf8(summary, 1, 1,
            "XinYueC Excel data type roundtrip", title), "write merged title");
        CHECK_OK(XWorksheet_mergeCells(summary, 1, 1, 1, 12, title),
            "merge title cells");

        const char* labels[] = {
            "String", "Integer", "Decimal", "Formula", "Boolean", "Date",
            "Time", "DateTime", "Blank", "Hyperlink", "RichText", "Variant"
        };
        for (int i = 0; i < 12; ++i)
            CHECK_OK(XWorksheet_writeString_utf8(summary, 2, i + 1, labels[i], header),
                "write type header");

        CHECK_OK(XWorksheet_writeString_utf8(summary, 3, 1, "hello", body),
            "write string");
        CHECK_OK(XWorksheet_writeNumeric(summary, 3, 2, 42.0, number),
            "write integer number");
        CHECK_OK(XWorksheet_writeNumeric(summary, 3, 3, 3.14159, number),
            "write decimal number");
        formula = XCellFormula_create_ex_utf8("SUM(B3:C3)");
        CHECK_OK(formula && XWorksheet_writeFormula(summary, 3, 4, formula, number, 45.14159),
            "write formula");
        CHECK_OK(XWorksheet_writeBool(summary, 3, 5, true, body), "write boolean");
        CHECK_OK(XWorksheet_writeDate(summary, 3, 6, 2026, 7, 27, date),
            "write date");
        CHECK_OK(XWorksheet_writeTime(summary, 3, 7, 13, 14, 15.5, date),
            "write time");
        CHECK_OK(XWorksheet_writeDateTime(summary, 3, 8, 1785100800000LL, date),
            "write datetime");
        CHECK_OK(XWorksheet_writeBlank(summary, 3, 9, body), "write blank");

        XString_Init_Utf8(url, "https://example.com/xinyuec");
        XString_Init_Utf8(display, "XinYueC home");
        XString_Init_Utf8(tip, "open project page");
        CHECK_OK(XWorksheet_writeHyperlink(summary, 3, 10, url, body, display, tip),
            "write hyperlink");
        XString_deinit_base(url);
        XString_deinit_base(display);
        XString_deinit_base(tip);

        rich = XRichString_create();
        richBold = XFormat_create();
        if (richBold) {
            XFormat_setFontBold(richBold, true);
            XFormat_setFontColor(richBold, &(XColor){ XColor_Rgb, 0xffff, 0xffff, 0, 0 });
        }
        if (rich) {
            XRichString_setText_utf8(rich, "plain ");
            XRichString_addFragment_utf8(rich, "bold", richBold);
        }
        CHECK_OK(rich && XWorksheet_writeRichString(summary, 3, 11, rich, body),
            "write rich string");
        XRichString_delete(rich);
        rich = NULL;
        XFormat_delete(richBold);
        richBold = NULL;

        variant = XVariant_create_utf8_str("written by XVariant");
        CHECK_OK(variant && XWorksheet_write(summary, 3, 12, variant, body),
            "write XVariant value");
        XVariant_delete_base(variant);
        variant = NULL;

        XCellReference ref = XCellReference_create_ex(4, 1);
        CHECK_OK(XWorksheet_writeStringRef(summary, &ref, numberCode, body),
            "write by cell reference");
        XCellRange range = XCellRange_create_ex(3, 1, 4, 4);
        CHECK_OK(XCellRange_isValid(&range), "create data range");

        for (int column = 1; column <= 12; ++column)
            CHECK_OK(XWorksheet_setColumnWidth(summary, column, column, 16.0),
                "set column width");
        CHECK_OK(XWorksheet_setRowHeight(summary, 1, 1, 28.0), "set title row height");
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "Types"), "select Types sheet");
    XWorksheet* types = XDocument_currentWorksheet(document);
    CHECK_OK(types != NULL, "get Types worksheet");
    if (types) {
        CHECK_OK(XWorksheet_writeString_utf8(types, 1, 1, "Type", header),
            "write Types header");
        CHECK_OK(XWorksheet_writeString_utf8(types, 2, 1, "int64", body),
            "write int64 label");
        CHECK_OK(XWorksheet_writeNumeric(types, 2, 2, -922337203685477.0, number),
            "write large numeric value");
        CHECK_OK(XWorksheet_writeString_utf8(types, 3, 1, "float", body),
            "write float label");
        CHECK_OK(XWorksheet_writeNumeric(types, 3, 2, 0.125, number),
            "write float value");
        CHECK_OK(XWorksheet_writeString_utf8(types, 4, 1, "XDocument_write", body),
            "write document API label");
        variant = XVariant_create_int(2026);
        CHECK_OK(variant && XDocument_write(document, 4, 2, variant, number),
            "write with XDocument API");
        XVariant_delete_base(variant);
        variant = NULL;
        CHECK_OK(XWorksheet_setColumnWidth(types, 1, 1, 24.0), "set Types label width");
        CHECK_OK(XWorksheet_setColumnWidth(types, 2, 2, 20.0), "set Types value width");
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "Notes"), "select Notes sheet");
    XWorksheet* notes = XDocument_currentWorksheet(document);
    CHECK_OK(notes && XWorksheet_writeString_utf8(notes, 1, 1,
        "This sheet verifies multiple worksheets in one package.", body),
        "write Notes sheet");

    CHECK_OK(XDocument_saveAs_utf8(document, output), "save data workflow");
    CHECK_OK(xexcel_file_exists(output), "data workflow file exists");

    XFormat_delete(title);
    XFormat_delete(header);
    XFormat_delete(body);
    XFormat_delete(number);
    XFormat_delete(date);
    title = header = body = number = date = NULL;
    XCellFormula_delete(formula);
    formula = NULL;
    XString_deinit_base(numberCode);
    XString_deinit_base(dateCode);

    XString* loadPath = XString_create_utf8(output);
    loaded = XDocument_createFromFile(loadPath);
    XString_delete_base(loadPath);
    CHECK_OK(loaded != NULL, "load data workflow");
    if (loaded) {
        CHECK_OK(XWorkbook_sheetCount(XDocument_workbook(loaded)) == 3,
            "loaded worksheet count");
        CHECK_OK(XDocument_selectSheet_utf8(loaded, "Summary"),
            "select loaded Summary");
        XWorksheet* loadedSummary = XDocument_currentWorksheet(loaded);
        check_cell_text(loadedSummary, 3, 1, "hello", "loaded string value", &all_pass);
        check_cell_text(loadedSummary, 3, 12, "written by XVariant",
            "loaded XVariant value", &all_pass);
        XCell* loadedFormulaCell = loadedSummary ? XWorksheet_cellAt(loadedSummary, 3, 4) : NULL;
        CHECK_OK(loadedFormulaCell && XCell_hasFormula(loadedFormulaCell),
            "loaded formula object");
        CHECK_OK(loadedFormulaCell && XCell_cellType(loadedFormulaCell) == XCell_NumberType,
            "loaded formula number type");
        readVariant = XDocument_read(loaded, 3, 2);
        CHECK_OK(readVariant && XVariant_toDouble(readVariant) == 42.0,
            "read numeric XVariant");
        XVariant_delete_base(readVariant);
        readVariant = NULL;
        CHECK_OK(XDocument_selectSheet_utf8(loaded, "Types"), "select loaded Types");
        XCell* loadedDate = XDocument_cellAt(loaded, 4, 2);
        CHECK_OK(loadedDate && XCell_cellType(loadedDate) == XCell_NumberType,
            "loaded document-written number");
    }

    XDocument_delete(loaded);
    XDocument_delete(document);
    xexcel_remove_file(output);
    return all_pass;
}

static bool test_feature_roundtrip_flow(void)
{
    const char* output = "xexcel_feature_roundtrip.xlsx";
    bool all_pass = true;
    XDocument* document = NULL;
    XDocument* loaded = NULL;
    XFormat* title = NULL;
    XFormat* header = NULL;
    XFormat* body = NULL;
    XFormat* highlightFormat = NULL;
    XDataValidation* validation = NULL;
    XConditionalFormatting* conditional = NULL;
    XChart* chartSheetChart = NULL;

    TEST_INFO("===== 流程2：布局、验证、条件格式、图片与图表往返 =====");
    document = XDocument_create();
    if (!document) {
        TEST_FAIL("创建文档", "返回空指针");
        return false;
    }
    CHECK_OK(XDocument_renameSheet_utf8(document, "Sheet1", "Feature"),
        "rename feature sheet");
    CHECK_OK(XDocument_addSheet_utf8(document, "Gallery", XAbstractSheet_ST_WorkSheet),
        "add Gallery sheet");
    CHECK_OK(XDocument_addSheet_utf8(document, "ChartSheet", XAbstractSheet_ST_ChartSheet),
        "add ChartSheet");

    title = make_title_format();
    header = make_header_format();
    body = make_body_format();
    highlightFormat = make_body_format();
    CHECK_OK(title && header && body && highlightFormat, "create feature formats");
    if (highlightFormat) {
        XColor red = XColor_create_rgb(0xff, 0xe6, 0xe6, 0xff);
        XFormat_setFontBold(highlightFormat, true);
        XFormat_setPatternForegroundColor(highlightFormat, &red);
        XFormat_setFillPattern(highlightFormat, XFormat_PatternSolid);
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "Feature"), "select Feature sheet");
    XWorksheet* feature = XDocument_currentWorksheet(document);
    CHECK_OK(feature != NULL, "get Feature worksheet");
    if (feature) {
        CHECK_OK(XWorksheet_writeString_utf8(feature, 1, 1,
            "Feature workflow", title), "write feature title");
        CHECK_OK(XWorksheet_mergeCells(feature, 1, 1, 1, 4, title),
            "merge feature title");
        CHECK_OK(XWorksheet_writeString_utf8(feature, 2, 1, "Item", header),
            "write feature item header");
        CHECK_OK(XWorksheet_writeString_utf8(feature, 2, 2, "Amount", header),
            "write feature amount header");
        CHECK_OK(XWorksheet_writeString_utf8(feature, 2, 3, "Score", header),
            "write feature score header");
        CHECK_OK(XWorksheet_writeString_utf8(feature, 2, 4, "Status", header),
            "write feature status header");
        for (int row = 3; row <= 7; ++row) {
            char item[32];
            snprintf(item, sizeof(item), "Item %d", row - 2);
            CHECK_OK(XWorksheet_writeString_utf8(feature, row, 1, item, body),
                "write feature item");
            CHECK_OK(XWorksheet_writeNumeric(feature, row, 2, row * 25.5, body),
                "write feature amount");
            CHECK_OK(XWorksheet_writeNumeric(feature, row, 3, row * 12.0, body),
                "write feature score");
            CHECK_OK(XWorksheet_writeString_utf8(feature, row, 4,
                row == 5 ? "Review" : "Ready", body), "write feature status");
        }
        CHECK_OK(XWorksheet_setColumnWidth(feature, 1, 1, 18.0), "set feature item width");
        CHECK_OK(XWorksheet_setColumnWidth(feature, 2, 3, 14.0), "set feature number widths");
        CHECK_OK(XWorksheet_setColumnWidth(feature, 4, 4, 16.0), "set feature status width");
        CHECK_OK(XWorksheet_setRowHeight(feature, 1, 1, 26.0), "set feature title height");
        CHECK_OK(XWorksheet_setColumnFormat(feature, 2, 3, body), "set feature column format");
        CHECK_OK(XWorksheet_setRowFormat(feature, 3, 7, body), "set feature row format");
        CHECK_OK(XWorksheet_setColumnHidden(feature, 4, 4, false), "set column visibility");
        CHECK_OK(XWorksheet_setRowHidden(feature, 8, 8, true), "hide feature row");
        CHECK_OK(XWorksheet_groupRows(feature, 3, 7, false), "group feature rows");
        XWorksheet_setWindowProtected(feature, true);
        CHECK_OK(XWorksheet_isWindowProtected(feature), "enable sheet protection");
        CHECK_OK(XWorksheet_setStartPage(feature, 2), "set worksheet start page");

        XString_Init_Utf8(minValue, "1");
        XString_Init_Utf8(maxValue, "100");
        validation = XDataValidation_create_ex(XDataValidation_Whole,
            XDataValidation_Between, minValue, maxValue, true);
        if (validation) XDataValidation_addRange(validation, 3, 3, 7, 3);
        CHECK_OK(validation && XWorksheet_addDataValidation(feature, validation),
            "add data validation");
        if (validation && feature->m_dataValidations &&
            XVector_size_base((XContainer*)feature->m_dataValidations) > 0)
            validation = NULL;
        XString_deinit_base(minValue);
        XString_deinit_base(maxValue);

        conditional = XConditionalFormatting_create();
        XString_Init_Utf8(conditionFormula, "B3>75");
        bool conditionalRule = conditional && XConditionalFormatting_addHighlightCellsRule2(
            conditional, XCF_Highlight_GreaterThan, conditionFormula, highlightFormat, true);
        if (conditional) XConditionalFormatting_addRange(conditional, 3, 2, 7, 2);
        CHECK_OK(conditionalRule && XWorksheet_addConditionalFormatting(feature, conditional),
            "add conditional formatting");
        if (conditional && feature->m_conditionalFormatting &&
            XVector_size_base((XContainer*)feature->m_conditionalFormatting) > 0)
            conditional = NULL;
        XString_deinit_base(conditionFormula);

        XChart* inlineChart = XWorksheet_insertChart(feature, 10, 1, 640, 360);
        CHECK_OK(inlineChart != NULL, "insert worksheet chart");
        if (inlineChart) {
            XCellRange chartRange = XCellRange_create_ex(2, 1, 7, 3);
            XChart_setChartType(inlineChart, XChart_BarChart);
            XChart_setChartTitle_utf8(inlineChart, "Feature amounts");
            XChart_setChartStyle(inlineChart, 10);
            XChart_setChartLegend(inlineChart, XChart_AxisPosBottom, true);
            XChart_setGridlinesEnable(inlineChart, true, false);
            XChart_addSeries(inlineChart, &chartRange, true, true, false);
            CHECK_OK(XVector_size_base((XContainer*)feature->m_chartFiles) > 0,
                "register worksheet chart");
        }
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "Gallery"), "select Gallery sheet");
    XWorksheet* gallery = XDocument_currentWorksheet(document);
    CHECK_OK(gallery != NULL, "get Gallery worksheet");
    if (gallery) {
        CHECK_OK(XWorksheet_writeString_utf8(gallery, 1, 1,
            "Image gallery", title), "write Gallery title");
        CHECK_OK(XWorksheet_mergeCells(gallery, 1, 1, 1, 12, title),
            "merge Gallery title");
        const char* images[] = {
            "配置cmake.png", "https.png", "运行.png", "分支.png"
        };
        const int imageRows[] = { 3, 3, 16, 16 };
        const int imageColumns[] = { 1, 8, 1, 8 };
        for (int i = 0; i < 4; ++i) {
            const char* path = xexcel_asset_path(images[i]);
            int index = XDocument_insertImage_utf8(document, imageRows[i],
                imageColumns[i], path);
            CHECK_OK(path[0] != '\0' && index == i, "insert Gallery image");
        }
        CHECK_OK(XDocument_getImageCount(document) == 4, "count Gallery images");
        XByteArray* imageData = XByteArray_create();
        CHECK_OK(imageData && XDocument_getImageAt(document, 3, 1, imageData) &&
            XByteArray_size_base((XContainer*)imageData) > 0, "read Gallery image");
        XByteArray_delete_base(imageData);
        XString_Init_Utf8(replacement, xexcel_asset_path("克隆信息.png"));
        CHECK_OK(XDocument_changeImage(document, 0, replacement), "replace Gallery image");
        XString_deinit_base(replacement);
        for (int column = 1; column <= 12; ++column)
            CHECK_OK(XWorksheet_setColumnWidth(gallery, column, column, 13.0),
                "set Gallery column width");
    }

    XAbstractSheet* chartSheetBase = XWorkbook_sheet(XDocument_workbook(document), 2);
    XChartsheet* chartSheet = chartSheetBase &&
        chartSheetBase->m_sheetType == XAbstractSheet_ST_ChartSheet
        ? (XChartsheet*)chartSheetBase : NULL;
    if (chartSheet) {
        chartSheetChart = XChart_create(&chartSheet->m_base,
            XAbstractOOXmlFile_F_NewFromScratch);
        CHECK_OK(chartSheetChart != NULL, "create chart sheet chart");
        if (chartSheetChart) {
            XCellRange chartRange = XCellRange_create_ex(2, 1, 7, 3);
            XString_Init_Utf8(axisTitle, "Amount");
            XChart_setChartType(chartSheetChart, XChart_PieChart);
            XChart_setChartTitle_utf8(chartSheetChart, "Feature distribution");
            XChart_setAxisTitle(chartSheetChart, XChart_AxisPosLeft, axisTitle);
            XChart_setSize(chartSheetChart, 640, 360);
            XChart_setDataSheetName_utf8(chartSheetChart, "Feature");
            XChart_addSeries(chartSheetChart, &chartRange, true, true, false);
            XChartsheet_setChart(chartSheet, chartSheetChart);
            XString_deinit_base(axisTitle);
        }
    } else {
        TEST_FAIL("获取图表工作表", "工作表类型不正确");
        all_pass = false;
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "Feature"),
        "select worksheet before feature save");
    CHECK_OK(XDocument_saveAs_utf8(document, output), "save feature workflow");
    CHECK_OK(xexcel_file_exists(output), "feature workflow file exists");

    XFormat_delete(title);
    XFormat_delete(header);
    XFormat_delete(body);
    XFormat_delete(highlightFormat);
    title = header = body = highlightFormat = NULL;
    if (validation) XDataValidation_delete(validation);
    if (conditional) XConditionalFormatting_delete(conditional);
    validation = NULL;
    conditional = NULL;

    XString* loadPath = XString_create_utf8(output);
    loaded = XDocument_createFromFile(loadPath);
    XString_delete_base(loadPath);
    CHECK_OK(loaded != NULL, "load feature workflow");
    if (loaded) {
        CHECK_OK(XWorkbook_sheetCount(XDocument_workbook(loaded)) == 3,
            "loaded feature sheet count");
        CHECK_OK(XDocument_selectSheet_utf8(loaded, "Feature"),
            "select loaded Feature");
        XWorksheet* loadedFeature = XDocument_currentWorksheet(loaded);
        CHECK_OK(loadedFeature && loadedFeature->m_dataValidations &&
            XVector_size_base((XContainer*)loadedFeature->m_dataValidations) == 1,
            "loaded data validation");
        CHECK_OK(loadedFeature && loadedFeature->m_conditionalFormatting &&
            XVector_size_base((XContainer*)loadedFeature->m_conditionalFormatting) == 1,
            "loaded conditional formatting");
        CHECK_OK(loadedFeature && loadedFeature->m_chartFiles &&
            XVector_size_base((XContainer*)loadedFeature->m_chartFiles) == 1,
            "loaded worksheet chart");
        CHECK_OK(XDocument_selectSheet_utf8(loaded, "Gallery"),
            "select loaded Gallery");
        CHECK_OK(XDocument_getImageCount(loaded) == 4, "loaded Gallery images");
        XAbstractSheet* loadedChartSheetBase = XWorkbook_sheet(XDocument_workbook(loaded), 2);
        CHECK_OK(loadedChartSheetBase &&
            loadedChartSheetBase->m_sheetType == XAbstractSheet_ST_ChartSheet,
            "loaded ChartSheet type");
        XChartsheet* loadedChartSheet = loadedChartSheetBase &&
            loadedChartSheetBase->m_sheetType == XAbstractSheet_ST_ChartSheet
            ? (XChartsheet*)loadedChartSheetBase : NULL;
        CHECK_OK(loadedChartSheet && XChartsheet_chart(loadedChartSheet) &&
            XChartsheet_chart(loadedChartSheet)->m_chartType == XChart_PieChart,
            "loaded ChartSheet chart");
    }

    XDocument_delete(loaded);
    XDocument_delete(document);
    if (chartSheetChart) XChart_delete(chartSheetChart);
    xexcel_remove_file(output);
    return all_pass;
}

static bool test_office_inspection_flow(void)
{
    const char* output = "xexcel_office_inspection.xlsx";
    bool all_pass = true;
    XDocument* document = NULL;
    XDocument* loaded = NULL;
    XFormat* title = NULL;
    XFormat* header = NULL;
    XFormat* body = NULL;
    XFormat* number = NULL;
    XFormat* date = NULL;
    XFormat* time = NULL;
    XFormat* dateTime = NULL;
    XChart* chartSheetChart = NULL;

    TEST_INFO("===== 流程3：生成 Office 人工检查工作簿 =====");
    document = XDocument_create();
    if (!document) {
        TEST_FAIL("创建文档", "返回空指针");
        return false;
    }
    CHECK_OK(XDocument_renameSheet_utf8(document, "Sheet1", "Summary"),
        "rename inspection Summary");
    CHECK_OK(XDocument_addSheet_utf8(document, "Types", XAbstractSheet_ST_WorkSheet),
        "add inspection Types");
    CHECK_OK(XDocument_addSheet_utf8(document, "Images", XAbstractSheet_ST_WorkSheet),
        "add inspection Images");
    CHECK_OK(XDocument_addSheet_utf8(document, "ChartData", XAbstractSheet_ST_WorkSheet),
        "add inspection ChartData");
    CHECK_OK(XDocument_addSheet_utf8(document, "Charts", XAbstractSheet_ST_ChartSheet),
        "add inspection Charts");
    XDocument_setDocumentProperty_utf8(document, "title", "XinYueC Office inspection workbook");
    XDocument_setDocumentProperty_utf8(document, "author", "XinYueC");

    title = make_title_format();
    header = make_header_format();
    body = make_body_format();
    number = make_body_format();
    date = make_body_format();
    time = make_body_format();
    dateTime = make_body_format();
    CHECK_OK(title && header && body && number && date && time && dateTime,
        "create inspection formats");
    if (number) XFormat_setNumberFormatIndex(number, 4);
    if (date) XFormat_setNumberFormatIndex(date, 14);
    if (time) XFormat_setNumberFormatIndex(time, 21);
    if (dateTime) XFormat_setNumberFormatIndex(dateTime, 22);

    CHECK_OK(XDocument_selectSheet_utf8(document, "Summary"), "select inspection Summary");
    XWorksheet* summary = XDocument_currentWorksheet(document);
    if (summary) {
        CHECK_OK(XWorksheet_writeString_utf8(summary, 1, 1,
            "XinYueC multi-sheet Office inspection", title), "write inspection title");
        CHECK_OK(XWorksheet_mergeCells(summary, 1, 1, 1, 6, title),
            "merge inspection title");
        const char* headings[] = { "Product", "Quantity", "Price", "Total", "Enabled", "Remark" };
        for (int i = 0; i < 6; ++i)
            CHECK_OK(XWorksheet_writeString_utf8(summary, 2, i + 1, headings[i], header),
                "write inspection header");
        const char* products[] = { "Red", "Green", "Blue", "Orange", "Purple" };
        for (int row = 3; row <= 7; ++row) {
            int index = row - 3;
            CHECK_OK(XWorksheet_writeString_utf8(summary, row, 1, products[index], body),
                "write inspection product");
            CHECK_OK(XWorksheet_writeNumeric(summary, row, 2, (index + 1) * 10, body),
                "write inspection quantity");
            CHECK_OK(XWorksheet_writeNumeric(summary, row, 3, 1.25 + index * 0.75, number),
                "write inspection price");
            XCellFormula* total = XCellFormula_create_ex_utf8(
                "B3*C3");
            if (total) {
                char formulaText[32];
                snprintf(formulaText, sizeof(formulaText), "B%d*C%d", row, row);
                XCellFormula_setText_utf8(total, formulaText);
            }
            CHECK_OK(total && XWorksheet_writeFormula(summary, row, 4, total, number,
                (index + 1) * 10 * (1.25 + index * 0.75)), "write inspection formula");
            XCellFormula_delete(total);
            CHECK_OK(XWorksheet_writeBool(summary, row, 5, (index % 2) == 0, body),
                "write inspection boolean");
            CHECK_OK(XWorksheet_writeString_utf8(summary, row, 6,
                index % 2 ? "Needs review" : "OK", body), "write inspection remark");
            CHECK_OK(XWorksheet_writeString_utf8(summary, row + 35, 1,
                products[index], body), "write chart category");
            CHECK_OK(XWorksheet_writeNumeric(summary, row + 35, 2,
                (index + 1) * 10 * (1.25 + index * 0.75), number),
                "write chart total");
        }
        CHECK_OK(XWorksheet_writeString_utf8(summary, 37, 1, "Product", header),
            "write chart category header");
        CHECK_OK(XWorksheet_writeString_utf8(summary, 37, 2, "Total", header),
            "write chart total header");
        for (int column = 1; column <= 6; ++column)
            CHECK_OK(XWorksheet_setColumnWidth(summary, column, column, 16.0),
                "set inspection Summary width");
        CHECK_OK(XDocument_defineName_utf8(document, "InspectionTotal", "Summary!$D$3:$D$7",
            NULL, NULL), "define inspection range");
        XChart* chart = XWorksheet_insertChart(summary, 10, 1, 640, 360);
        CHECK_OK(chart != NULL, "insert inspection worksheet chart");
        if (chart) {
            XCellRange chartRange = XCellRange_create_ex(37, 1, 42, 2);
            XChart_setChartType(chart, XChart_LineChart);
            XChart_setChartTitle_utf8(chart, "Inspection totals");
            XChart_setChartStyle(chart, 12);
            XChart_addSeries(chart, &chartRange, true, true, false);
        }
    } else {
        TEST_FAIL("获取检查用汇总表", "工作表不可用");
        all_pass = false;
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "Types"), "select inspection Types");
    XWorksheet* types = XDocument_currentWorksheet(document);
    if (types) {
        const char* headings[] = { "Type", "Example" };
        CHECK_OK(XWorksheet_writeString_utf8(types, 1, 1, headings[0], header),
            "write Types type header");
        CHECK_OK(XWorksheet_writeString_utf8(types, 1, 2, headings[1], header),
            "write Types example header");
        const char* names[] = { "String", "Integer", "Decimal", "Boolean", "Date", "Time", "DateTime", "Blank", "RichString", "Hyperlink" };
        for (int row = 2; row <= 11; ++row)
            CHECK_OK(XWorksheet_writeString_utf8(types, row, 1, names[row - 2], body),
                "write Types name");
        CHECK_OK(XWorksheet_writeString_utf8(types, 2, 2, "text", body), "write Types string");
        CHECK_OK(XWorksheet_writeNumeric(types, 3, 2, 12345, body), "write Types integer");
        CHECK_OK(XWorksheet_writeNumeric(types, 4, 2, 3.1415926, number), "write Types decimal");
        CHECK_OK(XWorksheet_writeBool(types, 5, 2, true, body), "write Types boolean");
        CHECK_OK(XWorksheet_writeDate(types, 6, 2, 2026, 7, 27, date), "write Types date");
        CHECK_OK(XWorksheet_writeTime(types, 7, 2, 9, 30, 15.25, time), "write Types time");
        CHECK_OK(XWorksheet_writeDateTime(types, 8, 2, 1785158055000LL, dateTime),
            "write Types datetime");
        CHECK_OK(XWorksheet_writeBlank(types, 9, 2, body), "write Types blank");
        XRichString* rich = XRichString_create();
        XFormat* richFormat = XFormat_create();
        if (richFormat) XFormat_setFontBold(richFormat, true);
        if (rich) {
            XRichString_setText_utf8(rich, "plain ");
            XRichString_addFragment_utf8(rich, "bold", richFormat);
        }
        CHECK_OK(rich && XWorksheet_writeRichString(types, 10, 2, rich, body),
            "write Types rich string");
        XRichString_delete(rich);
        XFormat_delete(richFormat);
        XString_Init_Utf8(typesUrl, "https://example.com/types");
        XString_Init_Utf8(typesDisplay, "Types link");
        CHECK_OK(XWorksheet_writeHyperlink(types, 11, 2, typesUrl, body, typesDisplay, NULL),
            "write Types hyperlink");
        XString_deinit_base(typesUrl);
        XString_deinit_base(typesDisplay);
        CHECK_OK(XWorksheet_setColumnWidth(types, 1, 1, 16.0), "set Types name width");
        CHECK_OK(XWorksheet_setColumnWidth(types, 2, 2, 24.0), "set Types example width");
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "Images"), "select inspection Images");
    XWorksheet* images = XDocument_currentWorksheet(document);
    if (images) {
        CHECK_OK(XWorksheet_writeString_utf8(images, 1, 1,
            "Colored PNG image gallery", title), "write Images title");
        CHECK_OK(XWorksheet_mergeCells(images, 1, 1, 1, 12, title), "merge Images title");
        const char* imageNames[] = {
            "配置cmake.png", "https.png", "VS克隆储存库.png", "克隆信息.png",
            "分支.png", "运行.png"
        };
        const int rows[] = { 3, 3, 3, 18, 18, 18 };
        const int columns[] = { 1, 8, 15, 1, 8, 15 };
        for (int i = 0; i < 6; ++i) {
            const char* path = xexcel_asset_path(imageNames[i]);
            int index = XDocument_insertImage_utf8(document, rows[i], columns[i], path);
            CHECK_OK(path[0] != '\0' && index == i, "insert inspection image");
        }
        CHECK_OK(XDocument_getImageCount(document) == 6, "count inspection images");
        for (int column = 1; column <= 20; ++column)
            CHECK_OK(XWorksheet_setColumnWidth(images, column, column, 12.0),
                "set Images column width");
    } else {
        TEST_FAIL("获取检查用图片表", "工作表不可用");
        all_pass = false;
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "ChartData"), "select inspection ChartData");
    XWorksheet* chartData = XDocument_currentWorksheet(document);
    if (chartData) {
        CHECK_OK(XWorksheet_writeString_utf8(chartData, 1, 1, "Month", header),
            "write ChartData month header");
        CHECK_OK(XWorksheet_writeString_utf8(chartData, 1, 2, "Sales", header),
            "write ChartData sales header");
        const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun" };
        for (int row = 2; row <= 7; ++row) {
            CHECK_OK(XWorksheet_writeString_utf8(chartData, row, 1, months[row - 2], body),
                "write ChartData month");
            CHECK_OK(XWorksheet_writeNumeric(chartData, row, 2, 100 + row * 17.5, number),
                "write ChartData value");
        }
        CHECK_OK(XWorksheet_setColumnWidth(chartData, 1, 2, 16.0),
            "set ChartData widths");
    }

    XAbstractSheet* chartSheetBase = XWorkbook_sheet(XDocument_workbook(document), 4);
    XChartsheet* chartSheet = chartSheetBase &&
        chartSheetBase->m_sheetType == XAbstractSheet_ST_ChartSheet
        ? (XChartsheet*)chartSheetBase : NULL;
    if (chartSheet && chartData) {
        chartSheetChart = XChart_create(&chartSheet->m_base,
            XAbstractOOXmlFile_F_NewFromScratch);
        CHECK_OK(chartSheetChart != NULL, "create inspection chartsheet chart");
        if (chartSheetChart) {
            XCellRange range = XCellRange_create_ex(1, 1, 7, 2);
            XChart_setChartType(chartSheetChart, XChart_BarChart);
            XChart_setChartTitle_utf8(chartSheetChart, "Monthly sales");
            XChart_setChartLegend(chartSheetChart, XChart_AxisPosBottom, false);
            XChart_setGridlinesEnable(chartSheetChart, true, true);
            XChart_setDataSheetName_utf8(chartSheetChart, "ChartData");
            XChart_addSeries(chartSheetChart, &range, true, true, false);
            XChartsheet_setChart(chartSheet, chartSheetChart);
        }
    } else {
        TEST_FAIL("获取检查用图表", "图表表或数据表不可用");
        all_pass = false;
    }

    CHECK_OK(XDocument_selectSheet_utf8(document, "Summary"),
        "select inspection Summary before save");
    CHECK_OK(XDocument_saveAs_utf8(document, output), "save Office inspection workbook");
    CHECK_OK(xexcel_file_exists(output), "Office inspection file exists");
    TEST_INFO("Office 检查工作簿已保留在程序运行目录：%s", output);

    XFormat_delete(title);
    XFormat_delete(header);
    XFormat_delete(body);
    XFormat_delete(number);
    XFormat_delete(date);
    XFormat_delete(time);
    XFormat_delete(dateTime);
    title = header = body = number = date = time = dateTime = NULL;

    XString* loadPath = XString_create_utf8(output);
    loaded = XDocument_createFromFile(loadPath);
    XString_delete_base(loadPath);
    CHECK_OK(loaded != NULL, "load Office inspection workbook");
    if (loaded) {
        CHECK_OK(XWorkbook_sheetCount(XDocument_workbook(loaded)) == 5,
            "loaded inspection sheet count");
        CHECK_OK(XDocument_selectSheet_utf8(loaded, "Images"),
            "select loaded Images");
        CHECK_OK(XDocument_getImageCount(loaded) == 6, "loaded inspection images");
        CHECK_OK(XDocument_selectSheet_utf8(loaded, "Summary"),
            "select loaded inspection Summary");
        check_cell_text(XDocument_currentWorksheet(loaded), 3, 1, "Red",
            "loaded inspection first product", &all_pass);
    }

    XDocument_delete(loaded);
    XDocument_delete(document);
    if (chartSheetChart) XChart_delete(chartSheetChart);
    return all_pass;
}

static bool test_run_all(void)
{
    bool all_pass = true;
    TEST_INFO("===== 全部测试：Excel 完整流程 =====");
    all_pass = test_data_roundtrip_flow() && all_pass;
    all_pass = test_feature_roundtrip_flow() && all_pass;
    all_pass = test_office_inspection_flow() && all_pass;
    all_pass = XExcelExtendedTest_runAll() && all_pass;
    TEST_INFO("全部流程结果：%s", all_pass ? "通过" : "失败");
    return all_pass;
}

static void data_flow_wrapper(XVariant* data)
{
    (void)data;
    test_data_roundtrip_flow();
}

static void feature_flow_wrapper(XVariant* data)
{
    (void)data;
    test_feature_roundtrip_flow();
}

static void office_flow_wrapper(XVariant* data)
{
    (void)data;
    test_office_inspection_flow();
}

static void run_all_wrapper(XVariant* data)
{
    (void)data;
    test_run_all();
}

static void extended_wrapper(XVariant* data)
{
    (void)data;
    XExcelExtendedTest_runAll();
}

void XMenu_XExcelTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XExcelTest");
    XAction* action;

    XMenu_addMenu(root, menu);

    action = XMenu_addAction(menu, "01 数据类型与保存加载往返");
    XAction_setAction(action, data_flow_wrapper);
    action = XMenu_addAction(menu, "02 布局图片图表往返");
    XAction_setAction(action, feature_flow_wrapper);
    action = XMenu_addAction(menu, "03 Office 人工检查工作簿");
    XAction_setAction(action, office_flow_wrapper);
    action = XMenu_addAction(menu, "04 执行全部完整流程");
    XAction_setAction(action, run_all_wrapper);
    action = XMenu_addAction(menu, "05 扩展 XML ZIP 支持流程");
    XAction_setAction(action, extended_wrapper);
}
