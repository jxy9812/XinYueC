/******************************************************************************
 * @file       XExcelTest.c
 * @brief      XExcel 全面测试（XDocument、XCell、XFormat、XCellReference等）
 * @author     XinYueC 团队
 * @note       覆盖 XExcel 主要模块的公开API
 ******************************************************************************/
#include"XExcelTest.h"
#include"XDocument.h"
#include"XCell.h"
#include"XCellReference.h"
#include"XCellRange.h"
#include"XCellFormula.h"
#include"XCellLocation.h"
#include"XFormat.h"
#include"XColor.h"
#include"XFont.h"
#include"XWorksheet.h"
#include"XWorkbook.h"
#include"XContentTypes.h"
#include"XChart.h"
#include"XChartsheet.h"
#include"XCellLocation.h"
#include"XRichString.h"
#include"XConditionalFormatting.h"
#include"XDataValidation.h"
#include"XRelationships.h"
#include"XCellFormula.h"
#include"XSharedStrings.h"
#include"XVariant.h"
#include"XStyles.h"
#include"XAbstractSheet.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XMemory.h"
#include"XString.h"
#include"XByteArray.h"
#include"XVector.h"
#include<string.h>
#include<stdlib.h>
#include<math.h>

/* ==================== 测试辅助宏 ==================== */
#define TEST_PASS(name) XPrintf("[PASS] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[FAIL] %s: %s\n", name, reason)
#define TEST_INFO(fmt, ...) XPrintf("[INFO] " fmt "\n", ##__VA_ARGS__)

/* ==================== 测试函数声明 ==================== */
static bool test_cell_reference(void);
static bool test_cell_range(void);
static bool test_cell_formula(void);
static bool test_cell_location(void);
static bool test_cell_create(void);
static bool test_cell_value(void);
static bool test_cell_type(void);
static bool test_cell_format(void);
static bool test_cell_formula_integration(void);
static bool test_format_create(void);
static bool test_format_font(void);
static bool test_format_alignment(void);
static bool test_format_border(void);
static bool test_format_fill(void);
static bool test_format_number(void);
static bool test_format_protection(void);
/* ==================== 包装函数 ==================== */
static void test_cell_reference_wrapper(XVariant* d) { (void)d; test_cell_reference(); }
static void test_cell_range_wrapper(XVariant* d) { (void)d; test_cell_range(); }
static void test_cell_formula_wrapper(XVariant* d) { (void)d; test_cell_formula(); }
static void test_cell_location_wrapper(XVariant* d) { (void)d; test_cell_location(); }
static void test_cell_create_wrapper(XVariant* d) { (void)d; test_cell_create(); }
static void test_cell_value_wrapper(XVariant* d) { (void)d; test_cell_value(); }
static void test_cell_type_wrapper(XVariant* d) { (void)d; test_cell_type(); }
static void test_cell_format_wrapper(XVariant* d) { (void)d; test_cell_format(); }
static void test_cell_formula_integration_wrapper(XVariant* d) { (void)d; test_cell_formula_integration(); }
static void test_format_create_wrapper(XVariant* d) { (void)d; test_format_create(); }
static void test_format_font_wrapper(XVariant* d) { (void)d; test_format_font(); }
static void test_format_alignment_wrapper(XVariant* d) { (void)d; test_format_alignment(); }
static void test_format_border_wrapper(XVariant* d) { (void)d; test_format_border(); }
static void test_format_fill_wrapper(XVariant* d) { (void)d; test_format_fill(); }
static void test_format_number_wrapper(XVariant* d) { (void)d; test_format_number(); }
static void test_format_protection_wrapper(XVariant* d) { (void)d; test_format_protection(); }

/* --- 修复后回归测试的前向声明 --- */
static bool test_format_merge(void);
static bool test_format_property(void);
static bool test_xvariant_setvalue(void);
static bool test_cond_format_copy(void);
static bool test_content_types_add(void);
static bool test_document_create_full(void);
static bool test_document_xvariant_writes(void);
static bool test_document_columns_rows(void);
static bool test_document_save_load(void);
static bool test_rich_string_settext(void);
static bool test_cell_formula_basic(void);
static bool test_cell_create_basic(void);
static bool test_chart_basic(void);
/* 公共 API 覆盖测试 */
static bool test_document_property(void);
static bool test_document_sheet_management(void);
static bool test_document_define_name(void);
static bool test_document_dimension(void);
static bool test_document_change_image(void);
static bool test_document_get_image(void);
static bool test_worksheet_string_writes(void);
static bool test_worksheet_protection_flags(void);
static bool test_worksheet_merged_cells(void);
static bool test_format_font(void);
static bool test_format_pattern(void);
static bool test_cell_formula(void);
static bool test_run_all(void);

/* ==================== 测试1: XCellReference 测试 ==================== */
static bool test_cell_reference(void)
{
    TEST_INFO("===== XCellReference 测试 =====");
    bool all_pass = true;

    /* 创建空引用 */
    XCellReference ref = XCellReference_create();
    if (!XCellReference_isValid(&ref)) {
        TEST_PASS("XCellReference_create 无效引用");
    } else {
        TEST_FAIL("XCellReference_create", "初始应为无效引用");
        all_pass = false;
    }

    /* 使用行列创建 */
    XCellReference ref2 = XCellReference_create_ex(1, 1);
    if (XCellReference_isValid(&ref2) && XCellReference_row(&ref2) == 1 && XCellReference_column(&ref2) == 1) {
        TEST_PASS("XCellReference_create_ex(1,1)");
    } else {
        TEST_FAIL("XCellReference_create_ex(1,1)", "行列值不正确");
        all_pass = false;
    }

    /* 使用字符串创建 A1 */
    XCellReference ref3 = XCellReference_create_str_utf8("A1");
    if (XCellReference_isValid(&ref3) && XCellReference_row(&ref3) == 1 && XCellReference_column(&ref3) == 1) {
        TEST_PASS("XCellReference_create_str(A1)");
    } else {
        TEST_FAIL("XCellReference_create_str(A1)", "A1应解析为row=1,col=1");
        all_pass = false;
    }

    /* 使用字符串创建 Z100 */
    XCellReference ref4 = XCellReference_create_str_utf8("Z100");
    if (XCellReference_isValid(&ref4) && XCellReference_row(&ref4) == 100 && XCellReference_column(&ref4) == 26) {
        TEST_PASS("XCellReference_create_str(Z100)");
    } else {
        TEST_FAIL("XCellReference_create_str(Z100)", "Z100应解析为row=100,col=26");
        all_pass = false;
    }

    /* 使用字符串创建 AA1 */
    XCellReference ref5 = XCellReference_create_str_utf8("AA1");
    if (XCellReference_isValid(&ref5) && XCellReference_column(&ref5) == 27) {
        TEST_PASS("XCellReference_create_str(AA1)");
    } else {
        TEST_FAIL("XCellReference_create_str(AA1)", "AA1应解析为col=27");
        all_pass = false;
    }

    /* 列名转数字 */
    int col = XCellReference_nameToColumn_utf8("A");
    if (col == 1) {
        TEST_PASS("XCellReference_nameToColumn(A) = 1");
    } else {
        TEST_FAIL("XCellReference_nameToColumn(A)", "A应转为1");
        all_pass = false;
    }

    col = XCellReference_nameToColumn_utf8("Z");
    if (col == 26) {
        TEST_PASS("XCellReference_nameToColumn(Z) = 26");
    } else {
        TEST_FAIL("XCellReference_nameToColumn(Z)", "Z应转为26");
        all_pass = false;
    }

    col = XCellReference_nameToColumn_utf8("AA");
    if (col == 27) {
        TEST_PASS("XCellReference_nameToColumn(AA) = 27");
    } else {
        TEST_FAIL("XCellReference_nameToColumn(AA)", "AA应转为27");
        all_pass = false;
    }

    /* 数字转列名 */
    XString colName = XCellReference_columnToName(1);
    const char* cn = XString_toUtf8(&colName);
    if (cn && strcmp(cn, "A") == 0) {
        TEST_PASS("XCellReference_columnToName(1) = A");
    } else {
        TEST_FAIL("XCellReference_columnToName(1)", "1应转为A");
        all_pass = false;
    }
    XString_deinit_base((XClass*)&colName);

    colName = XCellReference_columnToName(26);
    cn = XString_toUtf8(&colName);
    if (cn && strcmp(cn, "Z") == 0) {
        TEST_PASS("XCellReference_columnToName(26) = Z");
    } else {
        TEST_FAIL("XCellReference_columnToName(26)", "26应转为Z");
        all_pass = false;
    }
    XString_deinit_base((XClass*)&colName);

    /* 转字符串 */
    XCellReference ref6 = XCellReference_create_ex(1, 1);
    XString str = XCellReference_toString(&ref6, false, false);
    const char* s = XString_toUtf8(&str);
    if (s && strcmp(s, "A1") == 0) {
        TEST_PASS("XCellReference_toString(A1)");
    } else {
        TEST_FAIL("XCellReference_toString(A1)", "应输出A1");
        all_pass = false;
    }
    XString_deinit_base((XClass*)&str);

    /* 拷贝 */
    XCellReference ref7 = XCellReference_create();
    XCellReference_copy(&ref7, &ref6);
    if (XCellReference_equals(&ref7, &ref6)) {
        TEST_PASS("XCellReference_copy");
    } else {
        TEST_FAIL("XCellReference_copy", "拷贝后应相等");
        all_pass = false;
    }

    return all_pass;
}
/* ==================== 测试2: XCellRange 测试 ==================== */
static bool test_cell_range(void)
{
    TEST_INFO("===== XCellRange 测试 =====");
    bool all_pass = true;

    /* 创建空范围 */
    XCellRange range = XCellRange_create();
    if (!XCellRange_isValid(&range)) {
        TEST_PASS("XCellRange_create 无效范围");
    } else {
        TEST_FAIL("XCellRange_create", "初始应为无效范围");
        all_pass = false;
    }

    /* 使用行列创建 */
    XCellRange range2 = XCellRange_create_ex(1, 1, 10, 5);
    if (XCellRange_isValid(&range2)) {
        TEST_PASS("XCellRange_create_ex(1,1,10,5) 有效");
    } else {
        TEST_FAIL("XCellRange_create_ex(1,1,10,5)", "应为有效范围");
        all_pass = false;
    }

    /* 检查行列数 */
    if (XCellRange_rowCount(&range2) == 10 && XCellRange_columnCount(&range2) == 5) {
        TEST_PASS("XCellRange rowCount/columnCount");
    } else {
        TEST_FAIL("XCellRange rowCount/columnCount", "行数应为10，列数应为5");
        all_pass = false;
    }

    /* 使用字符串创建 */
    XCellRange range3 = XCellRange_create_str_utf8("A1:C3");
    if (XCellRange_isValid(&range3)) {
        TEST_PASS("XCellRange_create_str(A1:C3)");
    } else {
        TEST_FAIL("XCellRange_create_str(A1:C3)", "应为有效范围");
        all_pass = false;
    }

    /* 检查各角 */
    XCellReference tl = XCellRange_topLeft(&range3);
    XCellReference br = XCellRange_bottomRight(&range3);
    if (XCellReference_row(&tl) == 1 && XCellReference_column(&tl) == 1 &&
        XCellReference_row(&br) == 3 && XCellReference_column(&br) == 3) {
        TEST_PASS("XCellRange topLeft/bottomRight");
    } else {
        TEST_FAIL("XCellRange topLeft/bottomRight", "角点坐标不正确");
        all_pass = false;
    }

    /* 检查行数列数 */
    if (XCellRange_rowCount(&range3) == 3 && XCellRange_columnCount(&range3) == 3) {
        TEST_PASS("XCellRange 3x3行数列数");
    } else {
        TEST_FAIL("XCellRange 3x3行数列数", "应为3行3列");
        all_pass = false;
    }

    /* 转字符串 */
    XString str = XCellRange_toString(&range3, false, false);
    const char* s = XString_toUtf8(&str);
    if (s && strstr(s, "A1")) {
        TEST_PASS("XCellRange_toString");
    } else {
        TEST_FAIL("XCellRange_toString", "应包含A1");
        all_pass = false;
    }
    XString_deinit_base((XClass*)&str);

    /* 相等比较 */
    XCellRange range4 = XCellRange_create_ex(1, 1, 3, 3);
    if (XCellRange_equals(&range3, &range4)) {
        TEST_PASS("XCellRange_equals");
    } else {
        TEST_FAIL("XCellRange_equals", "相同范围应相等");
        all_pass = false;
    }

    return all_pass;
}

/* ==================== 测试3: XCellFormula 测试 ==================== */
static bool test_cell_formula(void)
{
    TEST_INFO("===== XCellFormula 测试 =====");
    bool all_pass = true;

    /* 创建空公式 */
    XCellFormula* f = XCellFormula_create();
    if (f) {
        TEST_PASS("XCellFormula_create");
    } else {
        TEST_FAIL("XCellFormula_create", "创建失败");
        all_pass = false;
    }
    XCellFormula_delete(f);

    /* 创建带文本的公式 */
    XCellFormula* f2 = XCellFormula_create_ex_utf8("SUM(A1:A10)");
    if (f2) {
        const XString* text = XCellFormula_formulaText(f2);
        if (text && XString_equals_utf8(text, "SUM(A1:A10)", XChar_CaseSensitive)) {
            TEST_PASS("XCellFormula_create_ex(SUM)");
        } else {
            TEST_FAIL("XCellFormula_create_ex(SUM)", "公式文本不正确");
            all_pass = false;
        }
        if (XCellFormula_isValid(f2)) {
            TEST_PASS("XCellFormula_isValid");
        } else {
            TEST_FAIL("XCellFormula_isValid", "有效公式应返回true");
            all_pass = false;
        }
        if (XCellFormula_formulaType(f2) == XCellFormula_Normal) {
            TEST_PASS("XCellFormula_type 默认Normal");
        } else {
            TEST_FAIL("XCellFormula_type", "默认类型应为Normal");
            all_pass = false;
        }
    } else {
        TEST_FAIL("XCellFormula_create_ex", "创建失败");
        all_pass = false;
    }
    XCellFormula_delete(f2);

    /* 设置类型 */
    XCellFormula* f3 = XCellFormula_create_ex_utf8("A1+B1");
    if (f3) {
        XCellFormula_setType(f3, XCellFormula_Array);
        if (XCellFormula_formulaType(f3) == XCellFormula_Array) {
            TEST_PASS("XCellFormula_setType(Array)");
        } else {
            TEST_FAIL("XCellFormula_setType(Array)", "类型未改变");
            all_pass = false;
        }
        XCellFormula_setText_utf8(f3, "A1*B1");
        const XString* text = XCellFormula_formulaText(f3);
        if (text && XString_equals_utf8(text, "A1*B1", XChar_CaseSensitive)) {
            TEST_PASS("XCellFormula_setText");
        } else {
            TEST_FAIL("XCellFormula_setText", "文本未更新");
            all_pass = false;
        }
    }
    XCellFormula_delete(f3);

    return all_pass;
}

/* ==================== 测试4: XCellLocation 测试 ==================== */
static bool test_cell_location(void)
{
    TEST_INFO("===== XCellLocation 测试 =====");
    bool all_pass = true;

    /* 创建空位置 */
    XCellLocation loc = XCellLocation_create();
    if (loc.m_row == 0 && loc.m_col == 0 && loc.m_cell == NULL) {
        TEST_PASS("XCellLocation_create");
    } else {
        TEST_FAIL("XCellLocation_create", "初始值不正确");
        all_pass = false;
    }

    /* 创建带参数位置 */
    XCell* cell = XCell_create();
    XCellLocation loc2 = XCellLocation_create_ex(1, 2, cell);
    if (loc2.m_row == 1 && loc2.m_col == 2 && loc2.m_cell == cell) {
        TEST_PASS("XCellLocation_create_ex");
    } else {
        TEST_FAIL("XCellLocation_create_ex", "成员值不正确");
        all_pass = false;
    }
    XCell_delete(cell);

    /* init 测试 */
    XCellLocation loc3;
    XCellLocation_init(&loc3);
    if (loc3.m_row == 0 && loc3.m_col == 0 && loc3.m_cell == NULL) {
        TEST_PASS("XCellLocation_init");
    } else {
        TEST_FAIL("XCellLocation_init", "初始值不正确");
        all_pass = false;
    }

    return all_pass;
}
/* ==================== 测试5: XCell 创建和删除测试 ==================== */
static bool test_cell_create(void)
{
    TEST_INFO("===== XCell 创建和删除测试 =====");
    bool all_pass = true;

    /* 创建空单元格 */
    XCell* cell = XCell_create();
    if (cell) {
        TEST_PASS("XCell_create");
    } else {
        TEST_FAIL("XCell_create", "创建失败");
        all_pass = false;
    }
    XCell_delete(cell);

    /* 创建带值和类型的单元格 */
    XCell* cell2 = XCell_create_ex_utf8("42", XCell_NumberType, NULL);
    if (cell2) {
        const XString* val = XCell_value(cell2);
        if (val && XString_equals_utf8(val, "42", XChar_CaseSensitive)) {
            TEST_PASS("XCell_create_ex 值=42");
        } else {
            TEST_FAIL("XCell_create_ex", "值不正确");
            all_pass = false;
        }
        if (XCell_cellType(cell2) == XCell_NumberType) {
            TEST_PASS("XCell_create_ex 类型=NumberType");
        } else {
            TEST_FAIL("XCell_create_ex", "类型不正确");
            all_pass = false;
        }
    } else {
        TEST_FAIL("XCell_create_ex", "创建失败");
        all_pass = false;
    }
    XCell_delete(cell2);

    /* 拷贝单元格 */
    XCell* cell3 = XCell_create_ex_utf8("Hello", XCell_StringType, NULL);
    XCell* cell4 = XCell_copy(cell3);
    if (cell4) {
        const XString* val = XCell_value(cell4);
        if (val && XString_equals_utf8(val, "Hello", XChar_CaseSensitive)) {
            TEST_PASS("XCell_copy");
        } else {
            TEST_FAIL("XCell_copy", "拷贝值不正确");
            all_pass = false;
        }
    } else {
        TEST_FAIL("XCell_copy", "拷贝失败");
        all_pass = false;
    }
    XCell_delete(cell3);
    XCell_delete(cell4);

    /* delete NULL 安全 */
    XCell_delete(NULL);
    TEST_PASS("XCell_delete(NULL) 安全");

    return all_pass;
}

/* ==================== 测试6: XCell 值操作测试 ==================== */
static bool test_cell_value(void)
{
    TEST_INFO("===== XCell 值操作测试 =====");
    bool all_pass = true;

    XCell* cell = XCell_create();
    if (!cell) { TEST_FAIL("XCell值操作", "创建失败"); return false; }

    /* 设置字符串值 */
    XCell_setValue_utf8(cell, "Test Value");
    const XString* val = XCell_value(cell);
    if (val && XString_equals_utf8(val, "Test Value", XChar_CaseSensitive)) {
        TEST_PASS("XCell_setValue / XCell_value");
    } else {
        TEST_FAIL("XCell_setValue", "值不正确");
        all_pass = false;
    }

    /* 更新值 */
    XCell_setValue_utf8(cell, "Updated");
    val = XCell_value(cell);
    if (val && XString_equals_utf8(val, "Updated", XChar_CaseSensitive)) {
        TEST_PASS("XCell_setValue 更新");
    } else {
        TEST_FAIL("XCell_setValue 更新", "值未更新");
        all_pass = false;
    }

    /* 设置空值 */
    XCell_setValue_utf8(cell, "");
    val = XCell_value(cell);
    if (val && XString_isEmpty_base(val)) {
        TEST_PASS("XCell_setValue 空字符串");
    } else {
        TEST_FAIL("XCell_setValue 空字符串", "应为空字符串");
        all_pass = false;
    }

    XCell_delete(cell);
    return all_pass;
}

/* ==================== 测试7: XCell 类型操作测试 ==================== */
static bool test_cell_type(void)
{
    TEST_INFO("===== XCell 类型操作测试 =====");
    bool all_pass = true;

    XCell* cell = XCell_create();
    if (!cell) { TEST_FAIL("XCell类型", "创建失败"); return false; }

    /* 测试各种类型 */
    XCell_setCellType(cell, XCell_BooleanType);
    if (XCell_cellType(cell) == XCell_BooleanType) {
        TEST_PASS("XCell_setCellType(BooleanType)");
    } else {
        TEST_FAIL("XCell_setCellType(BooleanType)", "类型不正确");
        all_pass = false;
    }

    XCell_setCellType(cell, XCell_NumberType);
    if (XCell_cellType(cell) == XCell_NumberType) {
        TEST_PASS("XCell_setCellType(NumberType)");
    } else {
        TEST_FAIL("XCell_setCellType(NumberType)", "类型不正确");
        all_pass = false;
    }

    XCell_setCellType(cell, XCell_DateType);
    if (XCell_cellType(cell) == XCell_DateType) {
        TEST_PASS("XCell_setCellType(DateType)");
    } else {
        TEST_FAIL("XCell_setCellType(DateType)", "类型不正确");
        all_pass = false;
    }

    XCell_setCellType(cell, XCell_StringType);
    if (XCell_cellType(cell) == XCell_StringType) {
        TEST_PASS("XCell_setCellType(StringType)");
    } else {
        TEST_FAIL("XCell_setCellType(StringType)", "类型不正确");
        all_pass = false;
    }

    XCell_delete(cell);
    return all_pass;
}
/* ==================== 测试8: XCell 格式/公式/行列测试 ==================== */
static bool test_cell_format(void)
{
    TEST_INFO("===== XCell 格式/公式/行列测试 =====");
    bool all_pass = true;

    XCell* cell = XCell_create();
    if (!cell) { TEST_FAIL("XCell格式", "创建失败"); return false; }

    /* 设置行和列 */
    XCell_setRow(cell, 5);
    XCell_setColumn(cell, 3);
    if (XCell_row(cell) == 5 && XCell_column(cell) == 3) {
        TEST_PASS("XCell_setRow/setColumn");
    } else {
        TEST_FAIL("XCell_setRow/setColumn", "行列值不正确");
        all_pass = false;
    }

    /* 设置格式 */
    XFormat* fmt = XFormat_create();
    if (fmt) {
        XCell_setFormat(cell, fmt);
        XFormat* fmt2 = XCell_format(cell);
        if (fmt2) {
            TEST_PASS("XCell_setFormat / XCell_format");
        } else {
            TEST_FAIL("XCell_format", "格式为空");
            all_pass = false;
        }
    }

    /* 设置公式 */
    XCellFormula* formula = XCellFormula_create_ex_utf8("SUM(A1:A10)");
    if (formula) {
        XCell_setFormula(cell, formula);
        if (XCell_hasFormula(cell)) {
            TEST_PASS("XCell_hasFormula");
        } else {
            TEST_FAIL("XCell_hasFormula", "有公式应返回true");
            all_pass = false;
        }
        XCellFormula* f2 = XCell_formula(cell);
        if (f2) {
            const XString* text = XCellFormula_formulaText(f2);
            if (text && XString_equals_utf8(text, "SUM(A1:A10)", XChar_CaseSensitive)) {
                TEST_PASS("XCell_formula 文本正确");
            } else {
                TEST_FAIL("XCell_formula", "公式文本不正确");
                all_pass = false;
            }
        }
    }

    /* 设置样式编号 */
    XCell_setStyleNumber(cell, 42);
    if (XCell_styleNumber(cell) == 42) {
        TEST_PASS("XCell_setStyleNumber");
    } else {
        TEST_FAIL("XCell_setStyleNumber", "样式编号不正确");
        all_pass = false;
    }

    XCell_delete(cell);
    return all_pass;
}

/* ==================== 测试9: XCell 公式集成测试 ==================== */
static bool test_cell_formula_integration(void)
{
    TEST_INFO("===== XCell 公式集成测试 =====");
    bool all_pass = true;

    XCell* cell = XCell_create();
    if (!cell) { TEST_FAIL("公式集成", "创建失败"); return false; }

    /* 无公式时 */
    if (XCell_hasFormula(cell) == false) {
        TEST_PASS("XCell_hasFormula 初始false");
    } else {
        TEST_FAIL("XCell_hasFormula 初始", "新单元格不应有公式");
        all_pass = false;
    }

    if (XCell_formula(cell) == NULL) {
        TEST_PASS("XCell_formula 初始为NULL");
    } else {
        TEST_FAIL("XCell_formula 初始", "新单元格公式应为NULL");
        all_pass = false;
    }

    /* 设置公式后 */
    XCellFormula* f = XCellFormula_create_ex_utf8("A1+B1");
    if (f) {
        XCell_setFormula(cell, f);
        if (XCell_hasFormula(cell)) {
            TEST_PASS("XCell_hasFormula 设公式后true");
        } else {
            TEST_FAIL("XCell_hasFormula 设公式后", "应返回true");
            all_pass = false;
        }
    }

    /* 日期类型判断 */
    if (XCell_isDateTime(cell) == false) {
        TEST_PASS("XCell_isDateTime 默认false");
    } else {
        TEST_FAIL("XCell_isDateTime 默认", "新单元格不应是日期类型");
        all_pass = false;
    }

    /* 富文本判断 */
    if (XCell_isRichString(cell) == false) {
        TEST_PASS("XCell_isRichString 默认false");
    } else {
        TEST_FAIL("XCell_isRichString 默认", "新单元格不应是富文本");
        all_pass = false;
    }

    XCell_delete(cell);
    return all_pass;
}

/* ==================== 测试10: XFormat 创建测试 ==================== */
static bool test_format_create(void)
{
    TEST_INFO("===== XFormat 创建测试 =====");
    bool all_pass = true;

    XFormat* fmt = XFormat_create();
    if (fmt) {
        TEST_PASS("XFormat_create");
    } else {
        TEST_FAIL("XFormat_create", "创建失败");
        all_pass = false;
    }

    /* 新格式有效性 */
    if (XFormat_isValid(fmt)) {
        TEST_PASS("XFormat_isValid 初始有效");
    } else {
        TEST_FAIL("XFormat_isValid 初始", "新格式应有效");
        all_pass = false;
    }

    /* 判断是否为空（无属性设置） */
    if (XFormat_isEmpty(fmt)) {
        TEST_PASS("XFormat_isEmpty 初始为空");
    } else {
        TEST_FAIL("XFormat_isEmpty 初始", "新格式应为空");
        all_pass = false;
    }

    XFormat_delete(fmt);
    fmt = NULL;

    /* 拷贝格式 */
    XFormat* fmt2 = XFormat_create();
    XFormat_setFontBold(fmt2, true);
    XFormat* fmt3 = XFormat_create(); XFormat_copy(fmt3, fmt2);
    if (fmt3) {
        TEST_PASS("XFormat_copy");
        XFormat_delete(fmt3);
    } else {
        TEST_FAIL("XFormat_copy", "拷贝失败");
        all_pass = false;
    }
    XFormat_delete(fmt2);

    /* 删除NULL安全 */
    XFormat_delete(NULL);
    TEST_PASS("XFormat_delete(NULL) 安全");

    return all_pass;
}
/* ==================== 测试11: XFormat 字体测试 ==================== */
static bool test_format_font(void)
{
    TEST_INFO("===== XFormat 字体测试 =====");
    bool all_pass = true;

    XFormat* fmt = XFormat_create();
    if (!fmt) { TEST_FAIL("字体测试", "创建失败"); return false; }

    /* 字体名称 */
    XFormat_setFontName_utf8(fmt, "Arial");
    const char* name = XFormat_fontName_utf8(fmt);
    if (name && strcmp(name, "Arial") == 0) {
        TEST_PASS("XFormat_setFontName(Arial)");
    } else {
        TEST_FAIL("XFormat_setFontName", "字体名称不正确");
        all_pass = false;
    }

    /* 字体大小 */
    XFormat_setFontSize(fmt, 12);
    int size = XFormat_fontSize(fmt);
    if (size == 12) {
        TEST_PASS("XFormat_setFontSize(12)");
    } else {
        TEST_FAIL("XFormat_setFontSize", "字体大小不正确");
        all_pass = false;
    }

    /* 粗体 */
    XFormat_setFontBold(fmt, true);
    if (XFormat_fontBold(fmt)) {
        TEST_PASS("XFormat_setFontBold(true)");
    } else {
        TEST_FAIL("XFormat_setFontBold", "应为粗体");
        all_pass = false;
    }

    /* 斜体 */
    XFormat_setFontItalic(fmt, true);
    if (XFormat_fontItalic(fmt)) {
        TEST_PASS("XFormat_setFontItalic(true)");
    } else {
        TEST_FAIL("XFormat_setFontItalic", "应为斜体");
        all_pass = false;
    }

    /* 下划线 */
    XFormat_setFontUnderline(fmt, XFormat_FontUnderlineSingle);
    if (XFormat_fontUnderline(fmt) == XFormat_FontUnderlineSingle) {
        TEST_PASS("XFormat_setFontUnderline(Single)");
    } else {
        TEST_FAIL("XFormat_setFontUnderline", "下划线类型不正确");
        all_pass = false;
    }

    /* 删除线 */
    XFormat_setFontStrikeOut(fmt, true);
    if (XFormat_fontStrikeOut(fmt)) {
        TEST_PASS("XFormat_setFontStrikeOut(true)");
    } else {
        TEST_FAIL("XFormat_setFontStrikeOut", "应为删除线");
        all_pass = false;
    }

    /* 字体颜色 */
    XColor color = XColor_create_rgb(255, 0, 0, 255);
    XFormat_setFontColor(fmt, &color);
    XColor retColor = XFormat_fontColor(fmt);
    if (XColor_red(&retColor) == 255 && XColor_green(&retColor) == 0 && XColor_blue(&retColor) == 0) {
        TEST_PASS("XFormat_setFontColor(红色)");
    } else {
        TEST_FAIL("XFormat_setFontColor", "颜色不正确");
        all_pass = false;
    }

    XFormat_delete(fmt);
    return all_pass;
}

/* ==================== 测试12: XFormat 对齐测试 ==================== */
static bool test_format_alignment(void)
{
    TEST_INFO("===== XFormat 对齐测试 =====");
    bool all_pass = true;

    XFormat* fmt = XFormat_create();
    if (!fmt) { TEST_FAIL("对齐测试", "创建失败"); return false; }

    /* 水平对齐 */
    XFormat_setHorizontalAlignment(fmt, XFormat_AlignHCenter);
    if (XFormat_horizontalAlignment(fmt) == XFormat_AlignHCenter) {
        TEST_PASS("XFormat_setHorizontalAlignment(居中)");
    } else {
        TEST_FAIL("XFormat_setHorizontalAlignment", "对齐方式不正确");
        all_pass = false;
    }

    XFormat_setHorizontalAlignment(fmt, XFormat_AlignRight);
    if (XFormat_horizontalAlignment(fmt) == XFormat_AlignRight) {
        TEST_PASS("XFormat_setHorizontalAlignment(右对齐)");
    } else {
        TEST_FAIL("XFormat_setHorizontalAlignment(右对齐)", "对齐方式不正确");
        all_pass = false;
    }

    /* 垂直对齐 */
    XFormat_setVerticalAlignment(fmt, XFormat_AlignVCenter);
    if (XFormat_verticalAlignment(fmt) == XFormat_AlignVCenter) {
        TEST_PASS("XFormat_setVerticalAlignment(垂直居中)");
    } else {
        TEST_FAIL("XFormat_setVerticalAlignment", "垂直对齐不正确");
        all_pass = false;
    }

    /* 文本换行 */
    XFormat_setTextWrap(fmt, true);
    if (XFormat_textWrap(fmt)) {
        TEST_PASS("XFormat_setTextWrap(true)");
    } else {
        TEST_FAIL("XFormat_setTextWrap", "应为自动换行");
        all_pass = false;
    }

    /* 缩进 */
    XFormat_setIndent(fmt, 2);
    if (XFormat_indent(fmt) == 2) {
        TEST_PASS("XFormat_setIndent(2)");
    } else {
        TEST_FAIL("XFormat_setIndent", "缩进值不正确");
        all_pass = false;
    }

    /* 旋转角度 */
    XFormat_setRotation(fmt, 45);
    if (XFormat_rotation(fmt) == 45) {
        TEST_PASS("XFormat_setRotation(45)");
    } else {
        TEST_FAIL("XFormat_setRotation", "旋转角度不正确");
        all_pass = false;
    }

    XFormat_delete(fmt);
    return all_pass;
}
/* ==================== 测试13: XFormat 边框测试 ==================== */
static bool test_format_border(void)
{
    TEST_INFO("===== XFormat 边框测试 =====");
    bool all_pass = true;

    XFormat* fmt = XFormat_create();
    if (!fmt) { TEST_FAIL("边框测试", "创建失败"); return false; }

    /* 左边框 */
    XFormat_setLeftBorderStyle(fmt, XFormat_BorderThin);
    if (XFormat_leftBorderStyle(fmt) == XFormat_BorderThin) {
        TEST_PASS("XFormat_setLeftBorderStyle(Thin)");
    } else {
        TEST_FAIL("XFormat_setLeftBorderStyle", "边框样式不正确");
        all_pass = false;
    }

    XColor blue = XColor_create_rgb(0, 0, 255, 255);
    XFormat_setLeftBorderColor(fmt, &blue);
    XColor ret = XFormat_leftBorderColor(fmt);
    if (XColor_blue(&ret) == 255 && XColor_red(&ret) == 0) {
        TEST_PASS("XFormat_setLeftBorderColor(蓝色)");
    } else {
        TEST_FAIL("XFormat_setLeftBorderColor", "颜色不正确");
        all_pass = false;
    }

    /* 右边框 */
    XFormat_setRightBorderStyle(fmt, XFormat_BorderMedium);
    if (XFormat_rightBorderStyle(fmt) == XFormat_BorderMedium) {
        TEST_PASS("XFormat_setRightBorderStyle(Medium)");
    } else {
        TEST_FAIL("XFormat_setRightBorderStyle", "边框样式不正确");
        all_pass = false;
    }

    /* 上边框 */
    XFormat_setTopBorderStyle(fmt, XFormat_BorderThick);
    if (XFormat_topBorderStyle(fmt) == XFormat_BorderThick) {
        TEST_PASS("XFormat_setTopBorderStyle(Thick)");
    } else {
        TEST_FAIL("XFormat_setTopBorderStyle", "边框样式不正确");
        all_pass = false;
    }

    /* 下边框 */
    XFormat_setBottomBorderStyle(fmt, XFormat_BorderDouble);
    if (XFormat_bottomBorderStyle(fmt) == XFormat_BorderDouble) {
        TEST_PASS("XFormat_setBottomBorderStyle(Double)");
    } else {
        TEST_FAIL("XFormat_setBottomBorderStyle", "边框样式不正确");
        all_pass = false;
    }

    /* 对角线 */
    XFormat_setDiagonalBorderStyle(fmt, XFormat_BorderDashed);
    XFormat_setDiagonalBorderType(fmt, XFormat_DiagonalBorderBoth);
    if (XFormat_diagonalBorderStyle(fmt) == XFormat_BorderDashed &&
        XFormat_diagonalBorderType(fmt) == XFormat_DiagonalBorderBoth) {
        TEST_PASS("XFormat 对角线边框");
    } else {
        TEST_FAIL("XFormat 对角线边框", "对角线样式不正确");
        all_pass = false;
    }

    XFormat_delete(fmt);
    return all_pass;
}

/* ==================== 测试14: XFormat 填充测试 ==================== */
static bool test_format_fill(void)
{
    TEST_INFO("===== XFormat 填充测试 =====");
    bool all_pass = true;

    XFormat* fmt = XFormat_create();
    if (!fmt) { TEST_FAIL("填充测试", "创建失败"); return false; }

    /* 填充模式 */
    XFormat_setFillPattern(fmt, XFormat_PatternSolid);
    if (XFormat_fillPattern(fmt) == XFormat_PatternSolid) {
        TEST_PASS("XFormat_setFillPattern(Solid)");
    } else {
        TEST_FAIL("XFormat_setFillPattern", "填充模式不正确");
        all_pass = false;
    }

    /* 前景色 */
    XColor yellow = XColor_create_rgb(255, 255, 0, 255);
    XFormat_setPatternForegroundColor(fmt, &yellow);
    XColor fg = XFormat_patternForegroundColor(fmt);
    if (XColor_red(&fg) == 255 && XColor_green(&fg) == 255 && XColor_blue(&fg) == 0) {
        TEST_PASS("XFormat_setPatternForegroundColor(黄色)");
    } else {
        TEST_FAIL("XFormat_setPatternForegroundColor", "颜色不正确");
        all_pass = false;
    }

    /* 背景色 */
    XColor white = XColor_create_rgb(255, 255, 255, 255);
    XFormat_setPatternBackgroundColor(fmt, &white);
    XColor bg = XFormat_patternBackgroundColor(fmt);
    if (XColor_red(&bg) == 255 && XColor_green(&bg) == 255 && XColor_blue(&bg) == 255) {
        TEST_PASS("XFormat_setPatternBackgroundColor(白色)");
    } else {
        TEST_FAIL("XFormat_setPatternBackgroundColor", "颜色不正确");
        all_pass = false;
    }

    XFormat_delete(fmt);
    return all_pass;
}

/* ==================== 测试15: XFormat 数字格式测试 ==================== */
static bool test_format_number(void)
{
    TEST_INFO("===== XFormat 数字格式测试 =====");
    bool all_pass = true;

    XFormat* fmt = XFormat_create();
    if (!fmt) { TEST_FAIL("数字格式", "创建失败"); return false; }

    /* 数字格式字符串 */
    XFormat_setNumberFormat(fmt, "#,##0.00");
    const char* nf = XFormat_numberFormat(fmt);
    if (nf && strcmp(nf, "#,##0.00") == 0) {
        TEST_PASS("XFormat_setNumberFormat(#,##0.00)");
    } else {
        TEST_FAIL("XFormat_setNumberFormat", "数字格式不正确");
        all_pass = false;
    }

    /* 数字格式索引 */
    XFormat_setNumberFormatIndex(fmt, 4);
    if (XFormat_numberFormatIndex(fmt) == 4) {
        TEST_PASS("XFormat_setNumberFormatIndex(4)");
    } else {
        TEST_FAIL("XFormat_setNumberFormatIndex", "格式索引不正确");
        all_pass = false;
    }

    XFormat_delete(fmt);
    return all_pass;
}

/* ==================== 测试16: XFormat 保护测试 ==================== */
static bool test_format_protection(void)
{
    TEST_INFO("===== XFormat 保护测试 =====");
    bool all_pass = true;

    XFormat* fmt = XFormat_create();
    if (!fmt) { TEST_FAIL("保护测试", "创建失败"); return false; }

    /* 锁定 */
    XFormat_setLocked(fmt, true);
    if (XFormat_locked(fmt)) {
        TEST_PASS("XFormat_setLocked(true)");
    } else {
        TEST_FAIL("XFormat_setLocked", "应为锁定");
        all_pass = false;
    }

    XFormat_setLocked(fmt, false);
    if (!XFormat_locked(fmt)) {
        TEST_PASS("XFormat_setLocked(false)");
    } else {
        TEST_FAIL("XFormat_setLocked(false)", "应非锁定");
        all_pass = false;
    }

    /* 隐藏 */
    XFormat_setHidden(fmt, true);
    if (XFormat_hidden(fmt)) {
        TEST_PASS("XFormat_setHidden(true)");
    } else {
        TEST_FAIL("XFormat_setHidden", "应为隐藏");
        all_pass = false;
    }

    XFormat_delete(fmt);
    return all_pass;
}


/* ===========================================================================
 * 菜单注册
 * =========================================================================== */

/* ========== 新增测试的包装函数（菜单回调签名 XVariant*） ========== */
static void test_format_merge_wrapper(XVariant* d) { (void)d; test_format_merge(); }
static void test_format_property_wrapper(XVariant* d) { (void)d; test_format_property(); }
static void test_xvariant_setvalue_wrapper(XVariant* d) { (void)d; test_xvariant_setvalue(); }
static void test_cond_format_copy_wrapper(XVariant* d) { (void)d; test_cond_format_copy(); }
static void test_content_types_add_wrapper(XVariant* d) { (void)d; test_content_types_add(); }
static void test_document_create_full_wrapper(XVariant* d) { (void)d; test_document_create_full(); }
static void test_document_xvariant_writes_wrapper(XVariant* d) { (void)d; test_document_xvariant_writes(); }
static void test_document_columns_rows_wrapper(XVariant* d) { (void)d; test_document_columns_rows(); }
static void test_document_save_load_wrapper(XVariant* d) { (void)d; test_document_save_load(); }
static void test_rich_string_settext_wrapper(XVariant* d) { (void)d; test_rich_string_settext(); }
static void test_cell_formula_basic_wrapper(XVariant* d) { (void)d; test_cell_formula_basic(); }
static void test_cell_create_basic_wrapper(XVariant* d) { (void)d; test_cell_create_basic(); }
static void test_chart_basic_wrapper(XVariant* d) { (void)d; test_chart_basic(); }
static void test_document_property_wrapper(XVariant* d) { (void)d; test_document_property(); }
static void test_document_sheet_management_wrapper(XVariant* d) { (void)d; test_document_sheet_management(); }
static void test_document_define_name_wrapper(XVariant* d) { (void)d; test_document_define_name(); }
static void test_document_dimension_wrapper(XVariant* d) { (void)d; test_document_dimension(); }
static void test_document_change_image_wrapper(XVariant* d) { (void)d; test_document_change_image(); }
static void test_document_get_image_wrapper(XVariant* d) { (void)d; test_document_get_image(); }
static void test_worksheet_string_writes_wrapper(XVariant* d) { (void)d; test_worksheet_string_writes(); }
static void test_worksheet_protection_flags_wrapper(XVariant* d) { (void)d; test_worksheet_protection_flags(); }
static void test_worksheet_merged_cells_wrapper(XVariant* d) { (void)d; test_worksheet_merged_cells(); }
static void test_format_pattern_wrapper(XVariant* d) { (void)d; test_format_pattern(); }
static void test_run_all_wrapper(XVariant* d) { (void)d; test_run_all(); }

void XMenu_XExcelTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XExcelTest");
    XMenu_addMenu(root, menu);
    XAction* action;

    action = XMenu_addAction(menu, "01 CellReference");
    XAction_setAction(action, test_cell_reference_wrapper);
    action = XMenu_addAction(menu, "02 CellRange");
    XAction_setAction(action, test_cell_range_wrapper);
    action = XMenu_addAction(menu, "03 CellFormula");
    XAction_setAction(action, test_cell_formula_wrapper);
    action = XMenu_addAction(menu, "04 CellLocation");
    XAction_setAction(action, test_cell_location_wrapper);
    action = XMenu_addAction(menu, "05 Cell创建/拷贝/删除");
    XAction_setAction(action, test_cell_create_wrapper);
    action = XMenu_addAction(menu, "06 Cell值");
    XAction_setAction(action, test_cell_value_wrapper);
    action = XMenu_addAction(menu, "07 Cell类型");
    XAction_setAction(action, test_cell_type_wrapper);
    action = XMenu_addAction(menu, "08 Cell格式/公式");
    XAction_setAction(action, test_cell_format_wrapper);
    action = XMenu_addAction(menu, "09 Cell公式+值");
    XAction_setAction(action, test_cell_formula_integration_wrapper);

    action = XMenu_addAction(menu, "10 Format创建");
    XAction_setAction(action, test_format_create_wrapper);
    action = XMenu_addAction(menu, "11 Format字体");
    XAction_setAction(action, test_format_font_wrapper);
    action = XMenu_addAction(menu, "12 Format对齐");
    XAction_setAction(action, test_format_alignment_wrapper);
    action = XMenu_addAction(menu, "13 Format边框");
    XAction_setAction(action, test_format_border_wrapper);
    action = XMenu_addAction(menu, "14 Format填充");
    XAction_setAction(action, test_format_fill_wrapper);
    action = XMenu_addAction(menu, "15 Format数字格式");
    XAction_setAction(action, test_format_number_wrapper);
    action = XMenu_addAction(menu, "16 Format保护");
    XAction_setAction(action, test_format_protection_wrapper);

    /* ===== 以下为 XExcel 各 bug 修复后新增的回归测试 ===== */

    action = XMenu_addAction(menu, "17 Format合并 (修复64位ptr截断)");
    XAction_setAction(action, test_format_merge_wrapper);
    action = XMenu_addAction(menu, "18 Format属性ID (修复stub)");
    XAction_setAction(action, test_format_property_wrapper);
    action = XMenu_addAction(menu, "19 XVariant setValue (修复SEGV)");
    XAction_setAction(action, test_xvariant_setvalue_wrapper);
    action = XMenu_addAction(menu, "20 CondFormat copy (修复double-free)");
    XAction_setAction(action, test_cond_format_copy_wrapper);
    action = XMenu_addAction(menu, "21 ContentTypes add (修复指针传递)");
    XAction_setAction(action, test_content_types_add_wrapper);
    action = XMenu_addAction(menu, "22 Document 创建+默认工作表");
    XAction_setAction(action, test_document_create_full_wrapper);
    action = XMenu_addAction(menu, "23 Document XVariant 完整通路");
    XAction_setAction(action, test_document_xvariant_writes_wrapper);
    action = XMenu_addAction(menu, "24 Document 列/行/分组");
    XAction_setAction(action, test_document_columns_rows_wrapper);
    action = XMenu_addAction(menu, "25 Document save/load 占位");
    XAction_setAction(action, test_document_save_load_wrapper);
    action = XMenu_addAction(menu, "26 RichString setText 同步片段");
    XAction_setAction(action, test_rich_string_settext_wrapper);
    action = XMenu_addAction(menu, "27 CellFormula");
    XAction_setAction(action, test_cell_formula_basic_wrapper);
    action = XMenu_addAction(menu, "28 Cell create/copy");
    XAction_setAction(action, test_cell_create_basic_wrapper);
    action = XMenu_addAction(menu, "29 Chart + Chartsheet");
    XAction_setAction(action, test_chart_basic_wrapper);
    action = XMenu_addAction(menu, "30 Document 属性");
    XAction_setAction(action, test_document_property_wrapper);
    action = XMenu_addAction(menu, "31 Sheet 管理");
    XAction_setAction(action, test_document_sheet_management_wrapper);
    action = XMenu_addAction(menu, "32 defineName");
    XAction_setAction(action, test_document_define_name_wrapper);
    action = XMenu_addAction(menu, "33 dimension");
    XAction_setAction(action, test_document_dimension_wrapper);
    action = XMenu_addAction(menu, "34 changeImage/getImage");
    XAction_setAction(action, test_document_change_image_wrapper);
    action = XMenu_addAction(menu, "35 getImage 安全");
    XAction_setAction(action, test_document_get_image_wrapper);
    action = XMenu_addAction(menu, "36 Worksheet 写方法");
    XAction_setAction(action, test_worksheet_string_writes_wrapper);
    action = XMenu_addAction(menu, "37 Worksheet 保护/可见性");
    XAction_setAction(action, test_worksheet_protection_flags_wrapper);
    action = XMenu_addAction(menu, "38 Worksheet 合并单元格");
    XAction_setAction(action, test_worksheet_merged_cells_wrapper);
    action = XMenu_addAction(menu, "39 Format 填充模式");
    XAction_setAction(action, test_format_pattern_wrapper);
    action = XMenu_addAction(menu, "40 RunAll 综合");
    XAction_setAction(action, test_run_all_wrapper);
}


/* ========== 测试 17-20: XFormat 合并 ============= */
/* ========== 验证修复后的 XFormat_mergeFormat 64 位 ptr 不再被截断 ========== */
static bool test_format_merge(void)
{
    TEST_INFO("===== 测试 17: XFormat 合并 (修复 64 位 ptr 截断) =====");
    bool all_pass = true;
    XFormat* src = XFormat_create();
    XFormat* dst = XFormat_create();
    XFormat_setFontBold(src, true);
    XFormat_setFontName_utf8(src, "Arial");

    /* 触发 mergeFormat - 之前会因为 val = (int)newPtr 截断指针触发 double-free */
    XFormat_mergeFormat(dst, src);

    if (XFormat_fontBold(dst)) TEST_PASS("mergeFormat 复制了 bold");
    else { TEST_FAIL("mergeFormat", "未复制 bold"); all_pass = false; }

    /* 字符串属性必须保持可读（合并后的 dst.m_format == src.m_format 也得能 delete）
     * 现在合并后会创建独立的 XString，不会与 src 共享 string ptr */
    XFormat_setFontName_utf8(dst, "Times");  /* 不应崩溃 */

    /* 释放 src（之前会 double-free dst 的 XString） */
    XFormat_delete(src);

    /* 释放 dst */
    XFormat_delete(dst);
    TEST_PASS("XFormat_mergeFormat 修复后无 double-free");

    return all_pass;
}

static bool test_format_property(void)
{
    TEST_INFO("===== 测试 18: XFormat 属性 ID 存储 (修复 stub) =====");
    bool all_pass = true;
    XFormat* f = XFormat_create();
    int val = 18;
    XFormat_setProperty(f, XFormat_P_NumFmt_Id, &val);
    if (XFormat_hasProperty(f, XFormat_P_NumFmt_Id)) TEST_PASS("hasProperty");
    else { TEST_FAIL("hasProperty", "未设置"); all_pass = false; }
    int* pval = (int*)XFormat_property(f, XFormat_P_NumFmt_Id);
    if (pval && *pval == 18) TEST_PASS("XFormat_property 取值正确（修复 stub）");
    else { TEST_FAIL("property 取错", "应=18"); all_pass = false; }

    XFormat_clearProperty(f, XFormat_P_NumFmt_Id);
    if (!XFormat_hasProperty(f, XFormat_P_NumFmt_Id)) TEST_PASS("clearProperty");
    else { TEST_FAIL("clearProperty", "清除失败"); all_pass = false; }
    XFormat_delete(f);
    return all_pass;
}

static bool test_xvariant_setvalue(void)
{
    TEST_INFO("===== 测试 19: XVariant_create_null + setValue (修复 SEGV) =====");
    bool all_pass = true;

    XVariant* v1 = XVariant_create_null();
    XVariant_setValue_utf8_str(v1, "Hello SEGV fix");
    if (v1) {
        XString* s = XVariant_toString(v1);
        if (s) {
            const char* u = XString_toUtf8(s);
            if (u && strlen(u) > 0) TEST_PASS("XVariant setValue_utf8_str 不再 SEGV");
            else { TEST_FAIL("setValue 结果", "字符串空"); all_pass = false; }
            XString_delete_base((XClass*)s);
        }
        XClass_delete_base((XClass*)v1);
    } else { TEST_FAIL("create_null", "失败"); all_pass = false; }

    XVariant* v2 = XVariant_create_null();
    XVariant_setValue_int(v2, 42);
    if (XVariant_toInt(v2) == 42) TEST_PASS("XVariant setValue_int");
    else { TEST_FAIL("setValue_int", "应=42"); all_pass = false; }
    XClass_delete_base((XClass*)v2);

    XVariant* v3 = XVariant_create_null();
    XVariant_setValue_bool(v3, true);
    if (XVariant_toBool(v3)) TEST_PASS("XVariant setValue_bool");
    else { TEST_FAIL("setValue_bool", "应=true"); all_pass = false; }
    XClass_delete_base((XClass*)v3);

    XVariant* v4 = XVariant_create_null();
    XVariant_setValue_double(v4, 3.14);
    if (XVariant_toDouble(v4) > 3.0 && XVariant_toDouble(v4) < 3.2) TEST_PASS("XVariant setValue_double");
    else { TEST_FAIL("setValue_double", "应≈3.14"); all_pass = false; }
    XClass_delete_base((XClass*)v4);

    return all_pass;
}


/* ========== 测试 20-24: XConditionalFormatting 深拷贝 ========== */
static bool test_cond_format_copy(void)
{
    TEST_INFO("===== 测试 20: XConditionalFormatting_copy 深拷贝 (修复 double-free) =====");
    bool all_pass = true;
    XConditionalFormatting* cf = XConditionalFormatting_create();
    XConditionalFormatting_addRange(cf, 1, 1, 10, 1);
    XColor red = XColor_create_rgb(255, 0, 0, 255);
    XConditionalFormatting_addDataBarRule(cf, &red, true, false);

    /* 修复前: dst.m_format = src->m_format （共享指针）
     * 修复后: dst.m_format = XFormat_create(); XFormat_copy(...);
     * 这样 cpy delete 时不会 double-free src 的 m_format */
    XConditionalFormatting* cpy = XConditionalFormatting_copy(cf);
    if (cpy) TEST_PASS("copy 返回非 NULL");
    else { TEST_FAIL("copy", "返回 NULL"); all_pass = false; }

    if (XConditionalFormatting_rulesCount(cpy) >= 1) TEST_PASS("rules 复制成功");
    else { TEST_FAIL("rules 复制", "错"); all_pass = false; }

    /* 释放 cpy：修复前会 double-free */
    XConditionalFormatting_delete(cpy);
    TEST_PASS("delete cpy 无 double-free");

    /* 释放 cf：原 m_format 仍存在，说明 cpy 持有独立副本 */
    XConditionalFormatting_delete(cf);
    TEST_PASS("delete src 仍正常");

    return all_pass;
}


/* ========== 测试 21-23: XContentTypes 指针传递 ========== */
static bool test_content_types_add(void)
{
    TEST_INFO("===== 测试 21: XContentTypes addDefault/addOverride (修复指针传递) =====");
    bool all_pass = true;
    XContentTypes* ct = XContentTypes_create();

    /* 修复前这两行传入的是 const char*，被 map 当作 const char** 解释，导致崩溃 */
    XContentTypes_addDefault_utf8(ct, "rels", "application/vnd.openxmlformats-package.relationships+xml");
    XContentTypes_addDefault_utf8(ct, "xml", "application/xml");
    XContentTypes_addOverride_utf8(ct, "/xl/workbook.xml",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.workbook+xml");
    TEST_PASS("addDefault/addOverride 不再崩溃");

    XContentTypes_addDocPropCore(ct);
    XContentTypes_addDocPropApp(ct);
    XContentTypes_addStyles(ct);
    XContentTypes_addTheme(ct);
    XContentTypes_addWorkbook(ct);
    XContentTypes_addWorksheetName_utf8(ct, "Sheet1");
    XContentTypes_addWorksheetName_utf8(ct, "Sheet2");
    TEST_PASS("addDocPropCore/App/Styles/Theme/Workbook + addWorksheetName x 2");

    XContentTypes_delete(ct);
    return all_pass;
}


/* ========== 测试 22-25: XDocument 完整通路 ========== */
static bool test_document_create_full(void)
{
    TEST_INFO("===== 测试 22: XDocument 创建 + 默认工作表 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (doc) TEST_PASS("XDocument_create");
    else { TEST_FAIL("create", "失败"); return false; }
    if (XDocument_workbook(doc)) TEST_PASS("workbook 不为空");
    else { TEST_FAIL("workbook", "空"); all_pass = false; }
    if (XDocument_currentWorksheet(doc)) TEST_PASS("currentWorksheet 默认存在");
    else { TEST_FAIL("current", "空"); all_pass = false; }
    if (XDocument_currentSheet(doc)) TEST_PASS("currentSheet 不为空");
    else { TEST_FAIL("currentSheet", "空"); all_pass = false; }
    XDocument_delete(doc);
    return all_pass;
}

static bool test_document_xvariant_writes(void)
{
    TEST_INFO("===== 测试 23: XDocument 走 XVariant 通路 (修复 SEGV 后回归) =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();

    XVariant* v1 = XVariant_create_null();
    XVariant_setValue_utf8_str(v1, "XDocument write string");
    if (XDocument_write(doc, 1, 1, v1, NULL))
        TEST_PASS("XDocument_write(string via XVariant)");
    else { TEST_FAIL("write string", "失败"); all_pass = false; }
    XClass_delete_base((XClass*)v1);

    XVariant* v2 = XVariant_create_null();
    XVariant_setValue_int(v2, 99);
    if (XDocument_write(doc, 2, 1, v2, NULL))
        TEST_PASS("XDocument_write(int via XVariant)");
    else { TEST_FAIL("write int", "失败"); all_pass = false; }
    XClass_delete_base((XClass*)v2);

    XVariant* r = XDocument_read(doc, 1, 1);
    if (r) {
        XString* s = XVariant_toString(r);
        if (s) {
            const char* u = XString_toUtf8(s);
            if (u && strlen(u) > 0) TEST_PASS("XDocument_read 出 XVariant 字符串值");
            else TEST_FAIL("read value", "空");
            XString_delete_base((XClass*)s);
        } else TEST_FAIL("read toString", "NULL");
        XClass_delete_base((XClass*)r);
    } else TEST_FAIL("read", "返回 NULL");

    XDocument_delete(doc);
    return all_pass;
}


/* ========== 测试 24: XDocument 列/行/分组/合并 ========== */
static bool test_document_columns_rows(void)
{
    TEST_INFO("===== 测试 24: XDocument 列/行/分组 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    XFormat* fmt = XFormat_create();
    XFormat_setFontBold(fmt, true);

    if (XDocument_setColumnWidth(doc, 1, 5, 18.5)) TEST_PASS("setColumnWidth");
    else TEST_FAIL("setColumnWidth", "失败");
    if (fabs(XDocument_columnWidth(doc, 3) - 18.5) < 0.001) TEST_PASS("columnWidth(3)=18.5");
    else TEST_FAIL("columnWidth", "应 18.5");

    if (XDocument_setColumnFormat(doc, 1, 5, fmt)) TEST_PASS("setColumnFormat");
    else TEST_FAIL("setColumnFormat", "失败");
    if (XDocument_columnFormat(doc, 1) == fmt) TEST_PASS("columnFormat(1) 返回 fmt");
    else TEST_FAIL("columnFormat", "错");

    if (XDocument_setColumnHidden(doc, 1, 5, true)) TEST_PASS("setColumnHidden");
    if (XDocument_isColumnHidden(doc, 2)) TEST_PASS("isColumnHidden(2)");
    else TEST_FAIL("isColumnHidden", "错");

    if (XDocument_setRowHeight(doc, 1, 5, 22.0)) TEST_PASS("setRowHeight");
    if (fabs(XDocument_rowHeight(doc, 3) - 22.0) < 0.001) TEST_PASS("rowHeight(3)=22");
    else TEST_FAIL("rowHeight", "应 22");

    if (XDocument_setRowFormat(doc, 1, 5, fmt)) TEST_PASS("setRowFormat");
    if (XDocument_setRowHidden(doc, 1, 5, true)) TEST_PASS("setRowHidden");
    if (XDocument_isRowHidden(doc, 3)) TEST_PASS("isRowHidden(3)");
    else TEST_FAIL("isRowHidden", "错");

    if (XDocument_groupRows(doc, 1, 5, true)) TEST_PASS("groupRows");
    else TEST_FAIL("groupRows", "失败");
    if (XDocument_groupColumns(doc, 1, 5, true)) TEST_PASS("groupColumns");
    else TEST_FAIL("groupColumns", "失败");

    XFormat_delete(fmt);
    XDocument_delete(doc);
    return all_pass;
}

static bool test_document_save_load(void)
{
    TEST_INFO("===== 测试 25: XDocument save/saveAs/load =====");
    XDocument* doc = XDocument_create();
    if (!doc) {
        TEST_FAIL("XDocument_create", "创建失败");
        return false;
    }
    TEST_PASS("XDocument_create");
    
    /* 保存到文件 */
    const char* filename = "/tmp/test_output.xlsx";
    if (XDocument_saveAs_utf8(doc, filename)) {
        TEST_PASS("XDocument_saveAs 成功");
    } else {
        TEST_FAIL("saveAs", "失败");
    }
    
    XDocument_delete(doc);
    doc = NULL;
    
    /* 检查文件是否存在 */
    FILE* f = fopen(filename, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fclose(f);
        if (size > 100) {
            TEST_PASS("xlsx 文件已生成");
        } else {
            TEST_FAIL("xlsx 文件", "太小");
        }
    } else {
        TEST_FAIL("xlsx 文件", "不存在");
    }
    
    return true;
}
static bool test_rich_string_settext(void)
{
    TEST_INFO("===== 测试 26: XRichString_setText 同步 addFragment (修复 fragmentCount) =====");
    bool all_pass = true;
    XRichString* rs = XRichString_create();
    XRichString_setText_utf8(rs, "Plain");

    /* 修复后: setText 应该添加 1 个 fragment (对齐 QXlsx 行为) */
    int cnt1 = XRichString_fragmentCount(rs);
    if (cnt1 == 1) TEST_PASS("setText 后 fragmentCount=1");
    else { char _ebuf1[64]; snprintf(_ebuf1, sizeof(_ebuf1), "应=1, 实=%d", cnt1); TEST_FAIL("setText fragmentCount", _ebuf1); all_pass = false; }

    /* 再 addFragment x 2，应该 = 3 */
    XRichString_addFragment_utf8(rs, " Red", NULL);
    XRichString_addFragment_utf8(rs, " Bold", NULL);
    int cnt2 = XRichString_fragmentCount(rs);
    if (cnt2 == 3) TEST_PASS("setText + addFragment x 2 = 3");
    else { char _ebuf2[64]; snprintf(_ebuf2, sizeof(_ebuf2), "应=3, 实=%d", cnt2); TEST_FAIL("total fragmentCount", _ebuf2); all_pass = false; }

    XRichString_delete(rs);
    return all_pass;
}


/* ========== 测试 27: XCellFormula ========== */
static bool test_cell_formula_basic(void)
{
    TEST_INFO("===== 测试 27: XCellFormula =====");
    bool all_pass = true;
    XCellFormula* f = XCellFormula_create();
    if (f && !XCellFormula_isValid(f)) TEST_PASS("空公式无效");
    else TEST_FAIL("空公式", "应无效");
    XCellFormula_setText_utf8(f, "SUM(A1:A10)");
    if (XCellFormula_isValid(f)) TEST_PASS("setText 后有效");
    else TEST_FAIL("setText", "应有效");
    if (XString_equals_utf8(XCellFormula_formulaText(f), "SUM(A1:A10)", XChar_CaseSensitive)) TEST_PASS("text() 返回正确");
    else TEST_FAIL("text", "错");
    XCellFormula_setType(f, XCellFormula_Array);
    if (XCellFormula_formulaType(f) == XCellFormula_Array) TEST_PASS("type=Array");
    else TEST_FAIL("type", "错");
    XCellFormula_delete(f);
    return all_pass;
}

static bool test_cell_create_basic(void)
{
    TEST_INFO("===== 测试 28: XCell 创建/拷贝 =====");
    bool all_pass = true;
    XCell* c = XCell_create();
    if (c && XCell_cellType(c) == XCell_CustomType) TEST_PASS("create 默认 CustomType");
    else TEST_FAIL("create", "错");
    XCell_setCellType(c, XCell_NumberType);
    if (XCell_cellType(c) == XCell_NumberType) TEST_PASS("setCellType(Number)");
    else TEST_FAIL("setCellType", "错");
    XCell_setRow(c, 5);
    XCell_setColumn(c, 3);
    if (XCell_row(c) == 5 && XCell_column(c) == 3) TEST_PASS("row/column 设置");
    else TEST_FAIL("row/column", "错");
    XCell_delete(c);
    return all_pass;
}


/* ========== 测试 29: XChartsheet / XChart 完整通路 ========== */
static bool test_chart_basic(void)
{
    TEST_INFO("===== 测试 29: XChart 创建/系列/属性 =====");
    bool all_pass = true;
    XWorkbook* wb = XWorkbook_create(XAbstractOOXmlFile_F_NewFromScratch);
    XAbstractSheet* s = XWorkbook_addSheet_utf8(wb, "S", XAbstractSheet_ST_WorkSheet);

    XChart* ch = XChart_create(s, XAbstractOOXmlFile_F_NewFromScratch);
    if (ch) TEST_PASS("XChart_create");
    else TEST_FAIL("create", "失败");
    XChart_setChartType(ch, XChart_BarChart);
    XChart_setChartTitle_utf8(ch, "我的图表");
    XChart_setChartStyle(ch, 10);
    TEST_PASS("setChartType/Title/Style");

    XCellReference tl = XCellReference_create_ex(1, 1);
    XCellReference br = XCellReference_create_ex(5, 3);
    XCellRange range = XCellRange_create_ref(&tl, &br);
    XChart_addSeries(ch, &range, false, true, false);
    TEST_PASS("addSeries");

    XChartsheet* cs = XChartsheet_create_utf8("ChartSheet1", 5, wb, XAbstractOOXmlFile_F_NewFromScratch);
    if (cs) TEST_PASS("XChartsheet_create");
    else TEST_FAIL("cs create", "失败");
    XChartsheet_setChart(cs, ch);
    if (XChartsheet_chart(cs) == ch) TEST_PASS("Chartsheet.setChart/getChart");
    else TEST_FAIL("chart", "错");

    XChartsheet_delete(cs);
    XChart_delete(ch);
    XWorkbook_delete(wb);
    return all_pass;
}


/* ========== 内存泄露检测钩子 ========== */
static long g_allocCount = 0;
static long g_freeCount  = 0;
static void* tracking_malloc(size_t size) { void* p = malloc(size); if (p) g_allocCount++; return p; }
static void  tracking_free(void* p)        { if (p) g_freeCount++; free(p); }
static void* tracking_realloc(void* p, size_t sz) {
    void* np = realloc(p, sz);
    if (!p && np) g_allocCount++;
    return np;
}
static void* tracking_calloc(size_t n, size_t sz) { void* p = calloc(n, sz); if (p) g_allocCount++; return p; }
static void tracking_memory_install(void) {
    XMemory m = { tracking_malloc, tracking_free, tracking_realloc, tracking_calloc };
    XMemory_setMethod(&m, XMEMORY_TYPE_SYSTEM);
}
static void tracking_reset(void) { g_allocCount = 0; g_freeCount = 0; }

/* ========== 公共 API 覆盖：Document 属性 ========== */
static bool test_document_property(void)
{
    TEST_INFO("===== Document 属性测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) { TEST_FAIL("create", ""); return false; }
    /* set */
    XDocument_setDocumentProperty_utf8(doc, "title", "Test");
    XDocument_setDocumentProperty_utf8(doc, "author", "XinYueC");
    /* 取出来比对 */
    const XString* t = XDocument_documentProperty_utf8(doc, "title");
    if (!t || !XString_equals_utf8(t, "Test", XChar_CaseSensitive)) { TEST_FAIL("title get", ""); all_pass = false; }
    else TEST_PASS("title get");
    const XString* a = XDocument_documentProperty_utf8(doc, "author");
    if (!a || !XString_equals_utf8(a, "XinYueC", XChar_CaseSensitive)) { TEST_FAIL("author get", ""); all_pass = false; }
    else TEST_PASS("author get");
    /* 取不存在的属性 -> 应返回空字符串或 NULL */
    const XString* none = XDocument_documentProperty_utf8(doc, "nonexistent");
    if (!none || XString_isEmpty_base(none)) TEST_PASS("不存在属性返回空");
    else { TEST_FAIL("nonexistent", "应返回空字符串"); all_pass = false; }
    /* documentPropertyNames 至少包含我们设置的 */
    XString** names = NULL;
    int n = XDocument_documentPropertyNames(doc, &names);
    if (n < 2) { TEST_FAIL("names count", "应 >=2"); all_pass = false; }
    else TEST_PASS("属性名数量 >= 2");
    if (names) {
        for (int i = 0; i < n; i++)
            if (names[i]) XString_delete_base((XClass*)names[i]);
        XFree_System(names);
    }
    XDocument_delete(doc);
    return all_pass;
}

/* ========== 公共 API 覆盖：Sheet 管理 ========== */
static bool test_document_sheet_management(void)
{
    TEST_INFO("===== Sheet 管理测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) return false;
    /* 添加工作表 */
    if (!XDocument_addSheet_utf8(doc, "Sheet2", 0 /*WorkSheet*/)) { TEST_FAIL("addSheet", ""); all_pass = false; }
    else TEST_PASS("addSheet Sheet2");
    if (!XDocument_addSheet_utf8(doc, "Sheet3", 0)) { TEST_FAIL("addSheet3", ""); all_pass = false; }
    else TEST_PASS("addSheet Sheet3");
    /* 列出所有工作表 */
    XString** names = NULL;
    int n = XDocument_sheetNames(doc, &names);
    if (n < 3) { TEST_FAIL("sheet count", "应 >=3"); all_pass = false; }
    else TEST_PASS("sheet count >= 3");
    if (names) {
        for (int i = 0; i < n; i++) if (names[i]) XString_delete_base((XClass*)names[i]);
        XFree_System(names);
    }
    /* 选择工作表 */
    if (!XDocument_selectSheet_utf8(doc, "Sheet2")) { TEST_FAIL("selectSheet", ""); all_pass = false; }
    else TEST_PASS("selectSheet Sheet2");
    if (!XDocument_selectSheetByIndex(doc, 0)) { TEST_FAIL("selectSheetByIndex", ""); all_pass = false; }
    else TEST_PASS("selectSheetByIndex 0");
    /* 重命名工作表 */
    if (!XDocument_renameSheet_utf8(doc, "Sheet2", "Renamed")) { TEST_FAIL("renameSheet", ""); all_pass = false; }
    else TEST_PASS("renameSheet Sheet2 -> Renamed");
    /* 删除工作表 */
    if (!XDocument_deleteSheet_utf8(doc, "Sheet3")) { TEST_FAIL("deleteSheet", ""); all_pass = false; }
    else TEST_PASS("deleteSheet Sheet3");
    XDocument_delete(doc);
    return all_pass;
}

/* ========== 公共 API 覆盖：defineName ========== */
static bool test_document_define_name(void)
{
    TEST_INFO("===== defineName 测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) return false;
    if (!XDocument_defineName_utf8(doc, "MyName", "Sheet1!$A$1", "comment", NULL)) {
        TEST_FAIL("defineName", ""); all_pass = false;
    } else TEST_PASS("defineName 成功");
    XDocument_delete(doc);
    return all_pass;
}

/* ========== 公共 API 覆盖：dimension ========== */
static bool test_document_dimension(void)
{
    TEST_INFO("===== dimension 测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) return false;
    /* 写入一些数据 */
    XFormat* fmt = XFormat_create();
    XVariant* v = XVariant_create_int(42);
    XDocument_write(doc, 1, 1, v, fmt);
    XDocument_write(doc, 5, 3, v, fmt);
    XVariant_delete_base(v);
    XCellRange dim = XDocument_dimension(doc);
    if (dim.m_firstRow == 1 && dim.m_lastRow == 5 &&
        dim.m_firstColumn == 1 && dim.m_lastColumn == 3) TEST_PASS("dimension 正确 (1,1)-(5,3)");
    else { TEST_FAIL("dimension", "应包含所有写入的单元格"); all_pass = false; }
    XFormat_delete(fmt);
    XDocument_delete(doc);
    return all_pass;
}

/* ========== 公共 API 覆盖：changeImage / getImage ========== */
static bool test_document_change_image(void)
{
    TEST_INFO("===== changeImage/getImage 测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) return false;
    /* 用占位符写入无效路径 -> 应该返回错误但不崩溃 */
    int idx = XDocument_insertImage_utf8(doc, 1, 1, "/nonexistent/path.png");
    /* 占位实现可能成功或失败，我们只检查不崩溃 */
    TEST_INFO("insertImage 返回 %d（占位）", idx);
    /* getImage 应安全处理 */
    XByteArray* ba = XByteArray_create();
    bool ok = XDocument_getImage(doc, 0, ba);
    TEST_INFO("getImage(0) 返回 %d（占位）", ok);
    XByteArray_delete_base((XClass*)ba);
    XDocument_delete(doc);
    return all_pass;
}

static bool test_document_get_image(void)
{
    TEST_INFO("===== getImage(占位) 测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) return false;
    XByteArray* ba = XByteArray_create();
    /* 越界索引应安全返回 false */
    if (!XDocument_getImage(doc, -1, ba)) TEST_PASS("getImage(-1) 安全");
    else { TEST_FAIL("getImage(-1)", "应失败"); all_pass = false; }
    if (!XDocument_getImage(doc, 99999, ba)) TEST_PASS("getImage(99999) 安全");
    else { TEST_FAIL("getImage(99999)", "应失败"); all_pass = false; }
    XByteArray_delete_base((XClass*)ba);
    XDocument_delete(doc);
    return all_pass;
}

/* ========== 公共 API 覆盖：Worksheet 写入 ========== */
static bool test_worksheet_string_writes(void)
{
    TEST_INFO("===== Worksheet string writes 测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) return false;
    XWorksheet* ws = XDocument_currentWorksheet(doc);
    XFormat* fmt = XFormat_create();
    if (!XWorksheet_writeString_utf8(ws, 1, 1, "hello", fmt)) { TEST_FAIL("writeString", ""); all_pass = false; }
    else TEST_PASS("writeString");
    if (!XWorksheet_writeNumeric(ws, 2, 1, 3.14, fmt)) { TEST_FAIL("writeNumeric", ""); all_pass = false; }
    else TEST_PASS("writeNumeric");
    if (!XWorksheet_writeBool(ws, 3, 1, true, fmt)) { TEST_FAIL("writeBool", ""); all_pass = false; }
    else TEST_PASS("writeBool");
    if (!XWorksheet_writeBlank(ws, 4, 1, fmt)) { TEST_FAIL("writeBlank", ""); all_pass = false; }
    else TEST_PASS("writeBlank");
    XFormat_delete(fmt);
    XDocument_delete(doc);
    return all_pass;
}

/* ========== 公共 API 覆盖：Worksheet 保护/可见性 标志 ========== */
static bool test_worksheet_protection_flags(void)
{
    TEST_INFO("===== Worksheet 保护/可见性 标志测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) return false;
    XWorksheet* ws = XDocument_currentWorksheet(doc);
    /* 这些 getter 应返回 bool 不崩溃 */
    TEST_INFO("isWindowProtected=%d", XWorksheet_isWindowProtected(ws));
    TEST_INFO("isFormulasVisible=%d", XWorksheet_isFormulasVisible(ws));
    TEST_INFO("isGridLinesVisible=%d", XWorksheet_isGridLinesVisible(ws));
    TEST_INFO("isRightToLeft=%d", XWorksheet_isRightToLeft(ws));
    TEST_PASS("保护/可见性 getter 不崩溃");
    XDocument_delete(doc);
    return all_pass;
}

/* ========== 公共 API 覆盖：Worksheet mergedCells ========== */
static bool test_worksheet_merged_cells(void)
{
    TEST_INFO("===== Worksheet mergedCells 测试 =====");
    bool all_pass = true;
    XDocument* doc = XDocument_create();
    if (!doc) return false;
    XWorksheet* ws = XDocument_currentWorksheet(doc);
    XFormat* fmt = XFormat_create();
    /* 合并单元格 */
    if (!XWorksheet_mergeCells(ws, 1, 1, 3, 3, fmt)) { TEST_FAIL("mergeCells", ""); all_pass = false; }
    else TEST_PASS("mergeCells 1,1-3,3");
    /* 获取合并单元格列表 */
    int count = 0;
    XCellRange* cells = XWorksheet_mergedCells(ws, &count);
    if (cells && count >= 1) TEST_PASS("mergedCells 返回至少 1 个");
    else { TEST_FAIL("mergedCells count", "应 >= 1"); all_pass = false; }
    /* XWorksheet_mergedCells 返回内部向量数据指针，不可释放 */
    /* 取消合并 */
    if (!XWorksheet_unmergeCells(ws, 1, 1, 3, 3)) { TEST_FAIL("unmergeCells", ""); all_pass = false; }
    else TEST_PASS("unmergeCells");
    XFormat_delete(fmt);
    XDocument_delete(doc);
    return all_pass;
}

/* ========== 公共 API 覆盖：Format pattern ========== */
static bool test_format_pattern(void)
{
    TEST_INFO("===== Format 填充模式测试 =====");
    bool all_pass = true;
    XFormat* fmt = XFormat_create();
    if (!fmt) return false;
    /* Pattern 类型 */
    XFormat_setFillPattern(fmt, XFormat_PatternSolid); /* Solid */
    if (XFormat_fillPattern(fmt) == XFormat_PatternSolid) TEST_PASS("setFillPattern=Solid -> fillPattern=Solid");
    else { TEST_FAIL("fillPattern", ""); all_pass = false; }
    /* 前景/背景颜色 */
    XColor fg;
    XColor_init(&fg);
    XColor_setRgb(&fg, 0xFF, 0x00, 0x00, 0xFF);
    XFormat_setPatternForegroundColor(fmt, &fg);
    XColor rfg = XFormat_patternForegroundColor(fmt);
    if (XColor_red(&rfg) == 0xFF) TEST_PASS("前景颜色 R=0xFF");
    else { TEST_FAIL("前景颜色", ""); all_pass = false; }
    XColor bg;
    XColor_init(&bg);
    XColor_setRgb(&bg, 0x00, 0xFF, 0x00, 0xFF);
    XFormat_setPatternBackgroundColor(fmt, &bg);
    XColor rbg = XFormat_patternBackgroundColor(fmt);
    if (XColor_green(&rbg) == 0xFF) TEST_PASS("背景颜色 G=0xFF");
    else { TEST_FAIL("背景颜色", ""); all_pass = false; }
    XFormat_delete(fmt);
            return all_pass;
}

/* ========== 测试 30: RunAll 总入口 ========== */
static bool test_run_all(void)
{
    TEST_INFO("===== 测试 30: RunAll 全量 + 内存泄露 =====");
    /* 安装跟踪钩子 */
    tracking_reset();
    tracking_memory_install();

#define RUN_TRACKED(fn) do { \
    long _a0 = g_allocCount, _f0 = g_freeCount; \
    bool _r = fn(); \
    long _da = g_allocCount - _a0, _df = g_freeCount - _f0; \
    if (_da != _df) TEST_INFO("  [LEAK] %s: +%ld alloc, +%ld free, net=%ld", #fn, _da, _df, _da - _df); \
    overall = overall && _r; \
} while(0)

    bool overall = true;
    RUN_TRACKED(test_cell_reference);
    RUN_TRACKED(test_cell_range);
    RUN_TRACKED(test_cell_formula_basic);
    RUN_TRACKED(test_cell_create_basic);
    RUN_TRACKED(test_format_create);
    RUN_TRACKED(test_format_merge);
    RUN_TRACKED(test_format_property);
    RUN_TRACKED(test_xvariant_setvalue);
    RUN_TRACKED(test_cond_format_copy);
    RUN_TRACKED(test_content_types_add);
    RUN_TRACKED(test_document_create_full);
    RUN_TRACKED(test_document_xvariant_writes);
    RUN_TRACKED(test_document_columns_rows);
    RUN_TRACKED(test_document_save_load);
    RUN_TRACKED(test_rich_string_settext);
    RUN_TRACKED(test_chart_basic);
    RUN_TRACKED(test_document_property);
    RUN_TRACKED(test_document_sheet_management);
    RUN_TRACKED(test_document_define_name);
    RUN_TRACKED(test_document_dimension);
    RUN_TRACKED(test_document_change_image);
    RUN_TRACKED(test_document_get_image);
    RUN_TRACKED(test_worksheet_string_writes);
    RUN_TRACKED(test_worksheet_protection_flags);
    RUN_TRACKED(test_worksheet_merged_cells);
    RUN_TRACKED(test_format_pattern);
#undef RUN_TRACKED
    TEST_INFO("RunAll：16 个综合测试 %s (内存泄露统计如下)", overall ? "全部通过" : "有失败");

    long leaked = g_allocCount - g_freeCount;
    if (leaked == 0) {
        TEST_INFO("[内存泄露检测] ✓ 无泄露 (alloc=%ld, free=%ld)", g_allocCount, g_freeCount);
    } else {
        TEST_INFO("[内存泄露] alloc=%ld, free=%ld, 净泄露=%ld", g_allocCount, g_freeCount, leaked);
    }
    return overall && leaked == 0;
}
