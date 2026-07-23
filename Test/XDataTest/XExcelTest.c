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
static bool test_document_create(void);
static bool test_document_write_read(void);
static bool test_document_write_read_ref(void);
static bool test_document_cell_at(void);
static bool test_document_sheet_management(void);
static bool test_document_column_row(void);
static bool test_document_merge_cells(void);
static bool test_document_properties(void);
static bool test_document_dimension(void);
static bool test_document_image(void);
static bool test_document_save_load(void);
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
static void test_document_create_wrapper(XVariant* d) { (void)d; test_document_create(); }
static void test_document_write_read_wrapper(XVariant* d) { (void)d; test_document_write_read(); }
static void test_document_write_read_ref_wrapper(XVariant* d) { (void)d; test_document_write_read_ref(); }
static void test_document_cell_at_wrapper(XVariant* d) { (void)d; test_document_cell_at(); }
static void test_document_sheet_management_wrapper(XVariant* d) { (void)d; test_document_sheet_management(); }
static void test_document_column_row_wrapper(XVariant* d) { (void)d; test_document_column_row(); }
static void test_document_merge_cells_wrapper(XVariant* d) { (void)d; test_document_merge_cells(); }
static void test_document_properties_wrapper(XVariant* d) { (void)d; test_document_properties(); }
static void test_document_dimension_wrapper(XVariant* d) { (void)d; test_document_dimension(); }
static void test_document_image_wrapper(XVariant* d) { (void)d; test_document_image(); }
static void test_document_save_load_wrapper(XVariant* d) { (void)d; test_document_save_load(); }

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
    XCellReference ref3 = XCellReference_create_str("A1");
    if (XCellReference_isValid(&ref3) && XCellReference_row(&ref3) == 1 && XCellReference_column(&ref3) == 1) {
        TEST_PASS("XCellReference_create_str(A1)");
    } else {
        TEST_FAIL("XCellReference_create_str(A1)", "A1应解析为row=1,col=1");
        all_pass = false;
    }

    /* 使用字符串创建 Z100 */
    XCellReference ref4 = XCellReference_create_str("Z100");
    if (XCellReference_isValid(&ref4) && XCellReference_row(&ref4) == 100 && XCellReference_column(&ref4) == 26) {
        TEST_PASS("XCellReference_create_str(Z100)");
    } else {
        TEST_FAIL("XCellReference_create_str(Z100)", "Z100应解析为row=100,col=26");
        all_pass = false;
    }

    /* 使用字符串创建 AA1 */
    XCellReference ref5 = XCellReference_create_str("AA1");
    if (XCellReference_isValid(&ref5) && XCellReference_column(&ref5) == 27) {
        TEST_PASS("XCellReference_create_str(AA1)");
    } else {
        TEST_FAIL("XCellReference_create_str(AA1)", "AA1应解析为col=27");
        all_pass = false;
    }

    /* 列名转数字 */
    int col = XCellReference_nameToColumn("A");
    if (col == 1) {
        TEST_PASS("XCellReference_nameToColumn(A) = 1");
    } else {
        TEST_FAIL("XCellReference_nameToColumn(A)", "A应转为1");
        all_pass = false;
    }

    col = XCellReference_nameToColumn("Z");
    if (col == 26) {
        TEST_PASS("XCellReference_nameToColumn(Z) = 26");
    } else {
        TEST_FAIL("XCellReference_nameToColumn(Z)", "Z应转为26");
        all_pass = false;
    }

    col = XCellReference_nameToColumn("AA");
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
    XString_deinit_base(&colName);

    colName = XCellReference_columnToName(26);
    cn = XString_toUtf8(&colName);
    if (cn && strcmp(cn, "Z") == 0) {
        TEST_PASS("XCellReference_columnToName(26) = Z");
    } else {
        TEST_FAIL("XCellReference_columnToName(26)", "26应转为Z");
        all_pass = false;
    }
    XString_deinit_base(&colName);

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
    XString_deinit_base(&str);

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
    XCellRange range3 = XCellRange_create_str("A1:C3");
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
    XString_deinit_base(&str);

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
    XCellFormula* f2 = XCellFormula_create_ex("SUM(A1:A10)");
    if (f2) {
        const char* text = XCellFormula_text(f2);
        if (text && strcmp(text, "SUM(A1:A10)") == 0) {
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
        if (XCellFormula_type(f2) == XCellFormula_Normal) {
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
    XCellFormula* f3 = XCellFormula_create_ex("A1+B1");
    if (f3) {
        XCellFormula_setType(f3, XCellFormula_Array);
        if (XCellFormula_type(f3) == XCellFormula_Array) {
            TEST_PASS("XCellFormula_setType(Array)");
        } else {
            TEST_FAIL("XCellFormula_setType(Array)", "类型未改变");
            all_pass = false;
        }
        XCellFormula_setText(f3, "A1*B1");
        const char* text = XCellFormula_text(f3);
        if (text && strcmp(text, "A1*B1") == 0) {
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
    XCell* cell2 = XCell_create_ex("42", XCell_NumberType, NULL);
    if (cell2) {
        const char* val = XCell_value(cell2);
        if (val && strcmp(val, "42") == 0) {
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
    XCell* cell3 = XCell_create_ex("Hello", XCell_StringType, NULL);
    XCell* cell4 = XCell_copy(cell3);
    if (cell4) {
        const char* val = XCell_value(cell4);
        if (val && strcmp(val, "Hello") == 0) {
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
    XCell_setValue(cell, "Test Value");
    const char* val = XCell_value(cell);
    if (val && strcmp(val, "Test Value") == 0) {
        TEST_PASS("XCell_setValue / XCell_value");
    } else {
        TEST_FAIL("XCell_setValue", "值不正确");
        all_pass = false;
    }

    /* 更新值 */
    XCell_setValue(cell, "Updated");
    val = XCell_value(cell);
    if (val && strcmp(val, "Updated") == 0) {
        TEST_PASS("XCell_setValue 更新");
    } else {
        TEST_FAIL("XCell_setValue 更新", "值未更新");
        all_pass = false;
    }

    /* 设置空值 */
    XCell_setValue(cell, "");
    val = XCell_value(cell);
    if (val && val[0] == '\0') {
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
    XCellFormula* formula = XCellFormula_create_ex("SUM(A1:A10)");
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
            const char* text = XCellFormula_text(f2);
            if (text && strcmp(text, "SUM(A1:A10)") == 0) {
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
    XCellFormula* f = XCellFormula_create_ex("A1+B1");
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
    XFormat* fmt3 = XFormat_copy(fmt2);
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
    XFormat_setFontName(fmt, "Arial");
    const char* name = XFormat_fontName(fmt);
    if (name && strcmp(name, "Arial") == 0) {
        TEST_PASS("XFormat_setFontName(Arial)");
    } else {
        TEST_FAIL("XFormat_setFontName", "字体名称不正确");
        all_pass = false;
    }

    /* 字体大小 */
    XFormat_setFontSize(fmt, 12.5);
    double size = XFormat_fontSize(fmt);
    if (size == 12.5) {
        TEST_PASS("XFormat_setFontSize(12.5)");
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
    XColor color = XColor_create(255, 0, 0, 255);
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

    XColor blue = XColor_create(0, 0, 255, 255);
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
    XColor yellow = XColor_create(255, 255, 0, 255);
    XFormat_setPatternForegroundColor(fmt, &yellow);
    XColor fg = XFormat_patternForegroundColor(fmt);
    if (XColor_red(&fg) == 255 && XColor_green(&fg) == 255 && XColor_blue(&fg) == 0) {
        TEST_PASS("XFormat_setPatternForegroundColor(黄色)");
    } else {
        TEST_FAIL("XFormat_setPatternForegroundColor", "颜色不正确");
        all_pass = false;
    }

    /* 背景色 */
    XColor white = XColor_create(255, 255, 255, 255);
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
