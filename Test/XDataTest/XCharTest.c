#include"XCharTest.h"
#include"XChar.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"

// ==================== 测试辅助宏 ====================
#define TEST_PASS(name) XPrintf("[PASS] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[FAIL] %s: %s\n", name, reason)
#define TEST_INFO(fmt, ...) XPrintf("[INFO] " fmt "\n", ##__VA_ARGS__)

// ==================== 测试函数声明 ====================
static bool test_create_functions(void);
static bool test_unicode_info(void);
static bool test_char_classification(void);
static bool test_case_conversion(void);
static bool test_surrogate_pair(void);
static bool test_mirror_direction(void);
static bool test_ucs4_overload(void);
static bool test_numeric_conversion(void);
static bool test_utf8_stream(void);
static bool test_utf16_stream(void);
static bool test_gbk_stream(void);
static bool test_special_character(void);

// ==================== 测试包装函数 ====================
static void test_create_functions_wrapper(XVariant* data) { (void)data; test_create_functions(); }
static void test_unicode_info_wrapper(XVariant* data) { (void)data; test_unicode_info(); }
static void test_char_classification_wrapper(XVariant* data) { (void)data; test_char_classification(); }
static void test_case_conversion_wrapper(XVariant* data) { (void)data; test_case_conversion(); }
static void test_surrogate_pair_wrapper(XVariant* data) { (void)data; test_surrogate_pair(); }
static void test_mirror_direction_wrapper(XVariant* data) { (void)data; test_mirror_direction(); }
static void test_ucs4_overload_wrapper(XVariant* data) { (void)data; test_ucs4_overload(); }
static void test_numeric_conversion_wrapper(XVariant* data) { (void)data; test_numeric_conversion(); }
static void test_utf8_stream_wrapper(XVariant* data) { (void)data; test_utf8_stream(); }
static void test_utf16_stream_wrapper(XVariant* data) { (void)data; test_utf16_stream(); }
static void test_gbk_stream_wrapper(XVariant* data) { (void)data; test_gbk_stream(); }
static void test_special_character_wrapper(XVariant* data) { (void)data; test_special_character(); }

// ==================== 测试1: 创建函数 ====================
static bool test_create_functions(void)
{
    TEST_INFO("===== XChar 创建函数测试 =====");
    bool all_pass = true;

    // XChar_null
    XChar ch0 = XChar_null();
    if (ch0 == 0) {
        TEST_PASS("XChar_null");
    } else {
        TEST_FAIL("XChar_null", "返回值不为0");
        all_pass = false;
    }

    // XChar_from
    XChar ch1 = XChar_from(0x0041); // 'A'
    if (ch1 == 0x0041) {
        TEST_PASS("XChar_from(0x0041)");
    } else {
        TEST_FAIL("XChar_from", "返回值不正确");
        all_pass = false;
    }

    // XChar_fromLatin1
    XChar ch2 = XChar_fromLatin1('A');
    if (ch2 == 'A') {
        TEST_PASS("XChar_fromLatin1('A')");
    } else {
        TEST_FAIL("XChar_fromLatin1", "返回值不正确");
        all_pass = false;
    }

    // XChar_fromLatin1 高位字符
    XChar ch3 = XChar_fromLatin1((char)0xFF);
    if (ch3 == 0x00FF) {
        TEST_PASS("XChar_fromLatin1(0xFF)");
    } else {
        TEST_FAIL("XChar_fromLatin1(0xFF)", "返回值不正确");
        all_pass = false;
    }

    // XChar_fromUcs2
    XChar ch4 = XChar_fromUcs2(0x4E2D); // '中'
    if (ch4 == 0x4E2D) {
        TEST_PASS("XChar_fromUcs2(0x4E2D)");
    } else {
        TEST_FAIL("XChar_fromUcs2", "返回值不正确");
        all_pass = false;
    }

    // XChar_fromUnicode
    XChar ch5 = XChar_fromUnicode(0x0042); // 'B'
    if (ch5 == 0x0042) {
        TEST_PASS("XChar_fromUnicode(0x0042)");
    } else {
        TEST_FAIL("XChar_fromUnicode", "返回值不正确");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试2: Unicode信息获取 ====================
static bool test_unicode_info(void)
{
    TEST_INFO("===== XChar Unicode信息获取测试 =====");
    bool all_pass = true;

    XChar ch_A = XChar_from(0x0041); // 'A'
    XChar ch_chinese = XChar_from(0x4E2D); // '中'

    // XChar_unicode
    if (XChar_unicode(ch_A) == 0x0041) {
        TEST_PASS("XChar_unicode('A')");
    } else {
        TEST_FAIL("XChar_unicode('A')", "返回值不正确");
        all_pass = false;
    }

    // XChar_toLatin1
    if (XChar_toLatin1(ch_A) == 'A') {
        TEST_PASS("XChar_toLatin1('A')");
    } else {
        TEST_FAIL("XChar_toLatin1('A')", "返回值不正确");
        all_pass = false;
    }

    // XChar_toLatin1 超出范围
    if (XChar_toLatin1(ch_chinese) == 0) {
        TEST_PASS("XChar_toLatin1(中文) 返回0");
    } else {
        TEST_FAIL("XChar_toLatin1(中文)", "应返回0");
        all_pass = false;
    }

    // XChar_row / XChar_cell
    if (XChar_row(ch_chinese) == 0x4E) {
        TEST_PASS("XChar_row(中)");
    } else {
        TEST_FAIL("XChar_row(中)", "返回值不正确");
        all_pass = false;
    }
    if (XChar_cell(ch_chinese) == 0x2D) {
        TEST_PASS("XChar_cell(中)");
    } else {
        TEST_FAIL("XChar_cell(中)", "返回值不正确");
        all_pass = false;
    }

    // XChar_toUtf8Size
    if (XChar_toUtf8Size(ch_A) == 1) {
        TEST_PASS("XChar_toUtf8Size('A') == 1");
    } else {
        TEST_FAIL("XChar_toUtf8Size('A')", "应返回1");
        all_pass = false;
    }
    if (XChar_toUtf8Size(ch_chinese) == 3) {
        TEST_PASS("XChar_toUtf8Size(中) == 3");
    } else {
        TEST_FAIL("XChar_toUtf8Size(中)", "应返回3");
        all_pass = false;
    }

    // XChar_currentUnicodeVersion
    XChar_UnicodeVersion ver = XChar_currentUnicodeVersion();
    if (ver >= XChar_Unicode_16_0) {
        TEST_PASS("XChar_currentUnicodeVersion >= Unicode_16_0");
    } else {
        TEST_FAIL("XChar_currentUnicodeVersion", "版本过低");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试3: 字符分类 ====================
static bool test_char_classification(void)
{
    TEST_INFO("===== XChar 字符分类测试 =====");
    bool all_pass = true;

    // isNull
    if (XChar_isNull(XChar_null()) && !XChar_isNull(XChar_from('A'))) {
        TEST_PASS("XChar_isNull");
    } else {
        TEST_FAIL("XChar_isNull", "判断失败");
        all_pass = false;
    }

    // isLetter
    if (XChar_isLetter(XChar_from('A')) && XChar_isLetter(XChar_from('z')) &&
        !XChar_isLetter(XChar_from('1'))) {
        TEST_PASS("XChar_isLetter");
    } else {
        TEST_FAIL("XChar_isLetter", "判断失败");
        all_pass = false;
    }

    // isDigit
    if (XChar_isDigit(XChar_from('5')) && !XChar_isDigit(XChar_from('a'))) {
        TEST_PASS("XChar_isDigit");
    } else {
        TEST_FAIL("XChar_isDigit", "判断失败");
        all_pass = false;
    }

    // isNumber
    if (XChar_isNumber(XChar_from('9')) && XChar_isNumber(XChar_from(0x0660))) { // 阿拉伯数字0
        TEST_PASS("XChar_isNumber");
    } else {
        TEST_FAIL("XChar_isNumber", "判断失败");
        all_pass = false;
    }

    // isLetterOrNumber
    if (XChar_isLetterOrNumber(XChar_from('A')) && XChar_isLetterOrNumber(XChar_from('3')) &&
        !XChar_isLetterOrNumber(XChar_from('!'))) {
        TEST_PASS("XChar_isLetterOrNumber");
    } else {
        TEST_FAIL("XChar_isLetterOrNumber", "判断失败");
        all_pass = false;
    }

    // isUpper / isLower
    if (XChar_isUpper(XChar_from('A')) && !XChar_isUpper(XChar_from('a')) &&
        XChar_isLower(XChar_from('a')) && !XChar_isLower(XChar_from('A'))) {
        TEST_PASS("XChar_isUpper / XChar_isLower");
    } else {
        TEST_FAIL("XChar_isUpper / XChar_isLower", "判断失败");
        all_pass = false;
    }

    // isSpace
    if (XChar_isSpace(XChar_from(' ')) && XChar_isSpace(XChar_from('\t')) &&
        !XChar_isSpace(XChar_from('A'))) {
        TEST_PASS("XChar_isSpace");
    } else {
        TEST_FAIL("XChar_isSpace", "判断失败");
        all_pass = false;
    }

    // isPunct
    if (XChar_isPunct(XChar_from('!')) && XChar_isPunct(XChar_from('.')) &&
        !XChar_isPunct(XChar_from('A'))) {
        TEST_PASS("XChar_isPunct");
    } else {
        TEST_FAIL("XChar_isPunct", "判断失败");
        all_pass = false;
    }

    // isPrint
    if (XChar_isPrint(XChar_from('A')) && XChar_isPrint(XChar_from(' ')) &&
        !XChar_isPrint(XChar_null())) {
        TEST_PASS("XChar_isPrint");
    } else {
        TEST_FAIL("XChar_isPrint", "判断失败");
        all_pass = false;
    }

    // isControl
    if (XChar_isControl(XChar_from('\n')) && XChar_isControl(XChar_from(0x007F)) &&
        !XChar_isControl(XChar_from('A'))) {
        TEST_PASS("XChar_isControl");
    } else {
        TEST_FAIL("XChar_isControl", "判断失败");
        all_pass = false;
    }

    // isSymbol
    if (XChar_isSymbol(XChar_from(0x2200)) && !XChar_isSymbol(XChar_from('A'))) {
        TEST_PASS("XChar_isSymbol");
    } else {
        TEST_FAIL("XChar_isSymbol", "判断失败");
        all_pass = false;
    }

    // isNonCharacter
    if (XChar_isNonCharacter(XChar_from(0xFFFE)) && XChar_isNonCharacter(XChar_from(0xFFFF)) &&
        !XChar_isNonCharacter(XChar_from('A'))) {
        TEST_PASS("XChar_isNonCharacter");
    } else {
        TEST_FAIL("XChar_isNonCharacter", "判断失败");
        all_pass = false;
    }

    // category
    XChar_Category cat1 = XChar_category(XChar_from('A'));
    if (cat1 == XChar_Letter_Uppercase) {
        TEST_PASS("XChar_category('A') == Letter_Uppercase");
    } else {
        TEST_FAIL("XChar_category('A')", "分类不正确");
        all_pass = false;
    }

    XChar_Category cat2 = XChar_category(XChar_from('5'));
    if (cat2 == XChar_Number_DecimalDigit) {
        TEST_PASS("XChar_category('5') == Number_DecimalDigit");
    } else {
        TEST_FAIL("XChar_category('5')", "分类不正确");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试4: 大小写转换 ====================
static bool test_case_conversion(void)
{
    TEST_INFO("===== XChar 大小写转换测试 =====");
    bool all_pass = true;

    // toUpper
    XChar upper = XChar_toUpper(XChar_from('a'));
    if (upper == 'A') {
        TEST_PASS("XChar_toLower('a') -> 'A'");
    } else {
        TEST_FAIL("XChar_toUpper('a')", "转换失败");
        all_pass = false;
    }

    // toLower
    XChar lower = XChar_toLower(XChar_from('A'));
    if (lower == 'a') {
        TEST_PASS("XChar_toLower('A') -> 'a'");
    } else {
        TEST_FAIL("XChar_toLower('A')", "转换失败");
        all_pass = false;
    }

    // toCaseFolded
    XChar folded = XChar_toCaseFolded(XChar_from('A'));
    if (folded == 'a') {
        TEST_PASS("XChar_toCaseFolded('A') -> 'a'");
    } else {
        TEST_FAIL("XChar_toCaseFolded('A')", "转换失败");
        all_pass = false;
    }

    // toTitleCase
    XChar title = XChar_toTitleCase(XChar_from('a'));
    if (title == 'A') {
        TEST_PASS("XChar_toTitleCase('a') -> 'A'");
    } else {
        TEST_FAIL("XChar_toTitleCase('a')", "转换失败");
        all_pass = false;
    }

    // 希腊字母大小写
    XChar greek_upper = XChar_toUpper(XChar_from(0x03B1)); // α -> Α
    if (greek_upper == 0x0391) {
        TEST_PASS("XChar_toUpper(α) -> Α");
    } else {
        TEST_FAIL("XChar_toUpper(α)", "希腊字母转换失败");
        all_pass = false;
    }

    // 西里尔字母大小写
    XChar cyr_upper = XChar_toUpper(XChar_from(0x0430)); // а -> А
    if (cyr_upper == 0x0410) {
        TEST_PASS("XChar_toUpper(а) -> А");
    } else {
        TEST_FAIL("XChar_toUpper(а)", "西里尔字母转换失败");
        all_pass = false;
    }

    // digitValue
    int d1 = XChar_digitValue(XChar_from('7'));
    if (d1 == 7) {
        TEST_PASS("XChar_digitValue('7') == 7");
    } else {
        TEST_FAIL("XChar_digitValue('7')", "值不正确");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试5: 代理对 ====================
static bool test_surrogate_pair(void)
{
    TEST_INFO("===== XChar 代理对测试 =====");
    bool all_pass = true;

    // isHighSurrogate / isLowSurrogate
    XChar high = XChar_from(0xD83D);
    XChar low = XChar_from(0xDE00);
    if (XChar_isHighSurrogate(high) && !XChar_isHighSurrogate(low)) {
        TEST_PASS("XChar_isHighSurrogate");
    } else {
        TEST_FAIL("XChar_isHighSurrogate", "判断失败");
        all_pass = false;
    }
    if (XChar_isLowSurrogate(low) && !XChar_isLowSurrogate(high)) {
        TEST_PASS("XChar_isLowSurrogate");
    } else {
        TEST_FAIL("XChar_isLowSurrogate", "判断失败");
        all_pass = false;
    }

    // isSurrogate
    if (XChar_isSurrogate(high) && XChar_isSurrogate(low) && !XChar_isSurrogate(XChar_from('A'))) {
        TEST_PASS("XChar_isSurrogate");
    } else {
        TEST_FAIL("XChar_isSurrogate", "判断失败");
        all_pass = false;
    }

    // surrogateToUcs4
    uint32_t ucs4 = XChar_surrogateToUcs4(high, low);
    if (ucs4 == 0x1F600) {
        TEST_PASS("XChar_surrogateToUcs4(0xD83D, 0xDE00) == 0x1F600");
    } else {
        TEST_FAIL("XChar_surrogateToUcs4", "转换结果不正确");
        all_pass = false;
    }

    // requiresSurrogates
    if (XChar_requiresSurrogates(0x1F600) && !XChar_requiresSurrogates(0x0041)) {
        TEST_PASS("XChar_requiresSurrogates");
    } else {
        TEST_FAIL("XChar_requiresSurrogates", "判断失败");
        all_pass = false;
    }

    // highSurrogate / lowSurrogate
    XChar hs = XChar_highSurrogate(0x1F600);
    XChar ls = XChar_lowSurrogate(0x1F600);
    if (hs == 0xD83D && ls == 0xDE00) {
        TEST_PASS("XChar_highSurrogate / XChar_lowSurrogate");
    } else {
        TEST_FAIL("XChar_highSurrogate / XChar_lowSurrogate", "代理值不正确");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试6: 镜像与方向 ====================
static bool test_mirror_direction(void)
{
    TEST_INFO("===== XChar 镜像与方向测试 =====");
    bool all_pass = true;

    // hasMirrored / mirroredChar
    if (XChar_hasMirrored(XChar_from('('))) {
        TEST_PASS("XChar_hasMirrored('(')");
    } else {
        TEST_FAIL("XChar_hasMirrored('(')", "判断失败");
        all_pass = false;
    }

    XChar mirror = XChar_mirroredChar(XChar_from('('));
    if (mirror == ')') {
        TEST_PASS("XChar_mirroredChar('(') == ')'");
    } else {
        TEST_FAIL("XChar_mirroredChar('(')", "镜像不正确");
        all_pass = false;
    }

    // direction
    XChar_Direction dir_l = XChar_direction(XChar_from('A'));
    if (dir_l == XChar_DirL) {
        TEST_PASS("XChar_direction('A') == DirL");
    } else {
        TEST_FAIL("XChar_direction('A')", "方向不正确");
        all_pass = false;
    }

    // combiningClass
    uint8_t cc = XChar_combiningClass(XChar_from('A'));
    if (cc == 0) {
        TEST_PASS("XChar_combiningClass('A') == 0");
    } else {
        TEST_FAIL("XChar_combiningClass('A')", "组合类不正确");
        all_pass = false;
    }

    // decompositionTag
    XChar_Decomposition dt = XChar_decompositionTag(XChar_from('A'));
    if (dt == XChar_NoDecomposition) {
        TEST_PASS("XChar_decompositionTag('A') == NoDecomposition");
    } else {
        TEST_FAIL("XChar_decompositionTag('A')", "分解标签不正确");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试7: UCS-4 重载函数 ====================
static bool test_ucs4_overload(void)
{
    TEST_INFO("===== XChar UCS-4 重载函数(_2)测试 =====");
    bool all_pass = true;

    // 基本功能：与实例版本结果一致
    XChar_Category cat1 = XChar_category(XChar_from('A'));
    XChar_Category cat2 = XChar_category_2(0x0041);
    if (cat1 == cat2) {
        TEST_PASS("XChar_category_2 与 XChar_category 一致");
    } else {
        TEST_FAIL("XChar_category_2", "与实例版本不一致");
        all_pass = false;
    }

    // 补充平面字符不崩溃
    XChar_Category cat3 = XChar_category_2(0x1F600);
    (void)cat3;
    TEST_PASS("XChar_category_2(0x1F600) 补充平面不崩溃");

    // isLetter_2
    if (XChar_isLetter_2('A') && !XChar_isLetter_2('1')) {
        TEST_PASS("XChar_isLetter_2");
    } else {
        TEST_FAIL("XChar_isLetter_2", "判断失败");
        all_pass = false;
    }

    // isDigit_2
    if (XChar_isDigit_2('5') && !XChar_isDigit_2('a')) {
        TEST_PASS("XChar_isDigit_2");
    } else {
        TEST_FAIL("XChar_isDigit_2", "判断失败");
        all_pass = false;
    }

    // toUpper_2 / toLower_2
    if (XChar_toUpper_2('a') == 'A' && XChar_toLower_2('A') == 'a') {
        TEST_PASS("XChar_toUpper_2 / XChar_toLower_2");
    } else {
        TEST_FAIL("XChar_toUpper_2 / XChar_toLower_2", "转换失败");
        all_pass = false;
    }

    // isHighSurrogate_2 / isLowSurrogate_2
    if (XChar_isHighSurrogate_2(0xD83D) && XChar_isLowSurrogate_2(0xDE00)) {
        TEST_PASS("XChar_isHighSurrogate_2 / XChar_isLowSurrogate_2");
    } else {
        TEST_FAIL("XChar_isHighSurrogate_2 / XChar_isLowSurrogate_2", "判断失败");
        all_pass = false;
    }

    // 补充平面返回安全默认值
    if (!XChar_isLetter_2(0x1F600) && !XChar_isDigit_2(0x1F600) &&
        !XChar_isSpace_2(0x1F600)) {
        TEST_PASS("补充平面安全默认值");
    } else {
        TEST_FAIL("补充平面安全默认值", "返回值不安全");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试8: 数值转换 ====================
static bool test_numeric_conversion(void)
{
    TEST_INFO("===== XChar 数值转换测试 =====");
    bool all_pass = true;

    // 测试 UTF-16 数组转数值（通过 XChar_toShort 等）
    XChar digits[] = { '1', '2', '3', 0 };
    bool success = false;

    int int_val = XChar_toInt(digits, 3, 10, &success);
    if (success && int_val == 123) {
        TEST_PASS("XChar_toInt('123') == 123");
    } else {
        TEST_FAIL("XChar_toInt('123')", "转换失败");
        all_pass = false;
    }

    short short_val = XChar_toShort(digits, 3, 10, &success);
    if (success && short_val == 123) {
        TEST_PASS("XChar_toShort('123') == 123");
    } else {
        TEST_FAIL("XChar_toShort('123')", "转换失败");
        all_pass = false;
    }

    // 十六进制
    XChar hex[] = { 'F', 'F', 0 };
    unsigned int uint_val = XChar_toUInt(hex, 2, 16, &success);
    if (success && uint_val == 0xFF) {
        TEST_PASS("XChar_toUInt('FF',16) == 255");
    } else {
        TEST_FAIL("XChar_toUInt('FF',16)", "转换失败");
        all_pass = false;
    }

    // 浮点数
    XChar flt[] = { '3', '.', '1', '4', 0 };
    double dbl_val = XChar_toDouble(flt, 4, &success);
    if (success && dbl_val > 3.13 && dbl_val < 3.15) {
        TEST_PASS("XChar_toDouble('3.14')");
    } else {
        TEST_FAIL("XChar_toDouble('3.14')", "转换失败");
        all_pass = false;
    }

    // 数值转 XChar 数组
    XChar buf[32] = { 0 };
    int64_t len = XChar_fromInt(123, 10, buf, 32, true);
    if (len == 3 && buf[0] == '1' && buf[1] == '2' && buf[2] == '3') {
        TEST_PASS("XChar_fromInt(123) == '123'");
    } else {
        TEST_FAIL("XChar_fromInt(123)", "转换失败");
        all_pass = false;
    }

    // 浮点数转 XChar
    XChar fbuf[32] = { 0 };
    int64_t flen = XChar_fromDouble(3.14, 'f', fbuf, 32, 2);
    if (flen > 0) {
        TEST_PASS("XChar_fromDouble(3.14,'f',2)");
    } else {
        TEST_FAIL("XChar_fromDouble(3.14,'f',2)", "转换失败");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试9: UTF-8 流转换 ====================
static bool test_utf8_stream(void)
{
    TEST_INFO("===== XChar UTF-8 流转换测试 =====");
    bool all_pass = true;

    // UTF-8 -> XChar: "Hello"
    const char* utf8_hello = "Hello";
    int64_t count = XChar_fromUtf8Stream((const uint8_t*)utf8_hello, 0, NULL, 0);
    if (count == 5) {
        TEST_PASS("XChar_fromUtf8Stream('Hello') 计数 == 5");
    } else {
        TEST_FAIL("XChar_fromUtf8Stream('Hello') 计数", "数量不正确");
        all_pass = false;
    }

    XChar out[16] = { 0 };
    int64_t converted = XChar_fromUtf8Stream((const uint8_t*)utf8_hello, 0, out, 16);
    if (converted == 5 && out[0] == 'H' && out[4] == 'o') {
        TEST_PASS("XChar_fromUtf8Stream('Hello') 转换正确");
    } else {
        TEST_FAIL("XChar_fromUtf8Stream('Hello') 转换", "结果不正确");
        all_pass = false;
    }

    // UTF-8 中文 "中" = 0xE4 0xB8 0xAD -> 0x4E2D
    const char* utf8_cn = "\xE4\xB8\xAD";
    count = XChar_fromUtf8Stream((const uint8_t*)utf8_cn, 0, NULL, 0);
    if (count == 1) {
        TEST_PASS("XChar_fromUtf8Stream('中') 计数 == 1");
    } else {
        TEST_FAIL("XChar_fromUtf8Stream('中') 计数", "数量不正确");
        all_pass = false;
    }

    XChar cn_out[4] = { 0 };
    XChar_fromUtf8Stream((const uint8_t*)utf8_cn, 0, cn_out, 4);
    if (cn_out[0] == 0x4E2D) {
        TEST_PASS("XChar_fromUtf8Stream('中') 码点正确");
    } else {
        TEST_FAIL("XChar_fromUtf8Stream('中') 码点", "码点不正确");
        all_pass = false;
    }

    // XChar -> UTF-8: "中"
    XChar chinese[] = { 0x4E2D, 0 };
    uint8_t utf8_buf[8] = { 0 };
    int64_t utf8_len = XChar_toUtf8Stream(chinese, 1, utf8_buf, 8);
    if (utf8_len == 3 && utf8_buf[0] == 0xE4 && utf8_buf[1] == 0xB8 && utf8_buf[2] == 0xAD) {
        TEST_PASS("XChar_toUtf8Stream(中) 编码正确");
    } else {
        TEST_FAIL("XChar_toUtf8Stream(中)", "编码不正确");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试10: UTF-16 流转换 ====================
static bool test_utf16_stream(void)
{
    TEST_INFO("===== XChar UTF-16 流转换测试 =====");
    bool all_pass = true;

    // UTF-16 -> XChar
    const uint16_t utf16_hello[] = { 'H', 'e', 'l', 'l', 'o', 0 };
    int64_t count = XChar_fromUtf16Stream(utf16_hello, 5, NULL, 0);
    if (count == 5) {
        TEST_PASS("XChar_fromUtf16Stream 计数 == 5");
    } else {
        TEST_FAIL("XChar_fromUtf16Stream 计数", "数量不正确");
        all_pass = false;
    }

    // XChar -> UTF-16
    XChar xchars[] = { 'H', 'i', 0 };
    uint16_t utf16_buf[8] = { 0 };
    int64_t len = XChar_toUtf16Stream(xchars, 2, utf16_buf, 8);
    if (len == 2 && utf16_buf[0] == 'H' && utf16_buf[1] == 'i') {
        TEST_PASS("XChar_toUtf16Stream 编码正确");
    } else {
        TEST_FAIL("XChar_toUtf16Stream", "编码不正确");
        all_pass = false;
    }

    return all_pass;
}

// ==================== 测试11: GBK 流转换 ====================
static bool test_gbk_stream(void)
{
    TEST_INFO("===== XChar GBK 流转换测试 =====");
    bool all_pass = true;

#ifdef _WIN32
    // GBK "中" = 0xD6 0xD0
    const char gbk_cn[] = { (char)0xD6, (char)0xD0, '\0' };
    int64_t count = XChar_fromGbkStream(gbk_cn, 2, NULL, 0);
    if (count == 1) {
        TEST_PASS("XChar_fromGbkStream('中') 计数 == 1");
    } else {
        TEST_FAIL("XChar_fromGbkStream('中') 计数", "数量不正确");
        all_pass = false;
    }

    XChar out[4] = { 0 };
    XChar_fromGbkStream(gbk_cn, 2, out, 4);
    if (out[0] == 0x4E2D) {
        TEST_PASS("XChar_fromGbkStream('中') 码点 == 0x4E2D");
    } else {
        TEST_FAIL("XChar_fromGbkStream('中') 码点", "码点不正确");
        all_pass = false;
    }

    // XChar -> GBK
    XChar cn_char[] = { 0x4E2D, 0 };
    char gbk_buf[8] = { 0 };
    int64_t gbk_len = XChar_toGbkStream(cn_char, 1, gbk_buf, 8);
    if (gbk_len == 2 && (uint8_t)gbk_buf[0] == 0xD6 && (uint8_t)gbk_buf[1] == 0xD0) {
        TEST_PASS("XChar_toGbkStream(中) 编码正确");
    } else {
        TEST_FAIL("XChar_toGbkStream(中)", "编码不正确");
        all_pass = false;
    }
#else
    TEST_INFO("GBK 测试仅在 Windows 平台执行");
#endif

    return all_pass;
}

// ==================== 测试12: SpecialCharacter 与枚举 ====================
static bool test_special_character(void)
{
    TEST_INFO("===== XChar SpecialCharacter 与枚举测试 =====");
    bool all_pass = true;

    // XChar_fromSpecial 基本构造
    XChar tab = XChar_fromSpecial(XChar_Tabulation);
    if (tab == 0x0009) {
        TEST_PASS("XChar_fromSpecial(Tabulation) == 0x0009");
    } else {
        TEST_FAIL("XChar_fromSpecial(Tabulation)", "值不正确");
        all_pass = false;
    }

    XChar lf = XChar_fromSpecial(XChar_LineFeed);
    if (lf == 0x000A) {
        TEST_PASS("XChar_fromSpecial(LineFeed) == 0x000A");
    } else {
        TEST_FAIL("XChar_fromSpecial(LineFeed)", "值不正确");
        all_pass = false;
    }

    XChar space = XChar_fromSpecial(XChar_Space);
    if (space == 0x0020) {
        TEST_PASS("XChar_fromSpecial(Space) == 0x0020");
    } else {
        TEST_FAIL("XChar_fromSpecial(Space)", "值不正确");
        all_pass = false;
    }

    XChar nbsp = XChar_fromSpecial(XChar_Nbsp);
    if (nbsp == 0x00A0) {
        TEST_PASS("XChar_fromSpecial(Nbsp) == 0x00A0");
    } else {
        TEST_FAIL("XChar_fromSpecial(Nbsp)", "值不正确");
        all_pass = false;
    }

    XChar bom = XChar_fromSpecial(XChar_ByteOrderMark);
    if (bom == 0xFEFF) {
        TEST_PASS("XChar_fromSpecial(ByteOrderMark) == 0xFEFF");
    } else {
        TEST_FAIL("XChar_fromSpecial(ByteOrderMark)", "值不正确");
        all_pass = false;
    }

    XChar repl = XChar_fromSpecial(XChar_ReplacementCharacter);
    if (repl == 0xFFFD) {
        TEST_PASS("XChar_fromSpecial(ReplacementCharacter) == 0xFFFD");
    } else {
        TEST_FAIL("XChar_fromSpecial(ReplacementCharacter)", "值不正确");
        all_pass = false;
    }

    // SpecialCharacter 值可直接用于 isXxx 判断
    if (XChar_isSpace(space) && XChar_isSpace(nbsp)) {
        TEST_PASS("SpecialCharacter Space/Nbsp isSpace");
    } else {
        TEST_FAIL("SpecialCharacter Space/Nbsp isSpace", "判断失败");
        all_pass = false;
    }

    if (XChar_isControl(XChar_fromSpecial(XChar_Null))) {
        TEST_PASS("SpecialCharacter Null isControl");
    } else {
        TEST_FAIL("SpecialCharacter Null isControl", "判断失败");
        all_pass = false;
    }

    // LastValidCodePoint
    XChar last = XChar_fromSpecial(XChar_LastValidCodePoint);
    if (last == 0xFFFF) { // XChar 是 uint16_t，截断到 0xFFFF
        TEST_PASS("XChar_fromSpecial(LastValidCodePoint) 截断为 0xFFFF");
    } else {
        TEST_FAIL("XChar_fromSpecial(LastValidCodePoint)", "截断值不正确");
        all_pass = false;
    }

    // 枚举值覆盖验证
    if (XChar_ParagraphSeparator == 0x2029 && XChar_LineSeparator == 0x2028) {
        TEST_PASS("ParagraphSeparator/LineSeparator 枚举值正确");
    } else {
        TEST_FAIL("ParagraphSeparator/LineSeparator", "枚举值不正确");
        all_pass = false;
    }

    // Category 枚举覆盖
    if (XChar_Letter_Uppercase == 14 && XChar_Number_DecimalDigit == 3 &&
        XChar_Other_Control == 9 && XChar_Symbol_Other == 29) {
        TEST_PASS("Category 枚举关键值正确");
    } else {
        TEST_FAIL("Category 枚举关键值", "值不正确");
        all_pass = false;
    }

    // Direction 枚举覆盖
    if (XChar_DirL == 0 && XChar_DirR == 1 && XChar_DirAL == 13) {
        TEST_PASS("Direction 枚举关键值正确");
    } else {
        TEST_FAIL("Direction 枚举关键值", "值不正确");
        all_pass = false;
    }

    // UnicodeVersion 枚举覆盖
    if (XChar_Unicode_Unassigned == 0 && XChar_Unicode_1_1 == 1 &&
        XChar_Unicode_16_0 == 27) {
        TEST_PASS("UnicodeVersion 枚举关键值正确");
    } else {
        TEST_FAIL("UnicodeVersion 枚举关键值", "值不正确");
        all_pass = false;
    }

    return all_pass;
}

// ==================== XCharTest 入口 ====================
static void XCharTest_all(XVariant* data)
{
    (void)data;
    TEST_INFO("========== XChar 全部测试开始 ==========");
    int pass = 0, fail = 0;

    if (test_create_functions()) pass++; else fail++;
    if (test_unicode_info()) pass++; else fail++;
    if (test_char_classification()) pass++; else fail++;
    if (test_case_conversion()) pass++; else fail++;
    if (test_surrogate_pair()) pass++; else fail++;
    if (test_mirror_direction()) pass++; else fail++;
    if (test_ucs4_overload()) pass++; else fail++;
    if (test_numeric_conversion()) pass++; else fail++;
    if (test_utf8_stream()) pass++; else fail++;
    if (test_utf16_stream()) pass++; else fail++;
    if (test_gbk_stream()) pass++; else fail++;
    if (test_special_character()) pass++; else fail++;

    TEST_INFO("========== 测试结果: %d 通过, %d 失败 ==========", pass, fail);
}

void XMenu_XCharTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XCharTest");
    XMenu_addMenu(root, menu);
    XAction* action = XMenu_addAction(menu, "全部测试");
    XAction_setAction(action, XCharTest_all);
    action = XMenu_addAction(menu, "创建函数");
    XAction_setAction(action, test_create_functions_wrapper);
    action = XMenu_addAction(menu, "Unicode信息");
    XAction_setAction(action, test_unicode_info_wrapper);
    action = XMenu_addAction(menu, "字符分类");
    XAction_setAction(action, test_char_classification_wrapper);
    action = XMenu_addAction(menu, "大小写转换");
    XAction_setAction(action, test_case_conversion_wrapper);
    action = XMenu_addAction(menu, "代理对");
    XAction_setAction(action, test_surrogate_pair_wrapper);
    action = XMenu_addAction(menu, "镜像与方向");
    XAction_setAction(action, test_mirror_direction_wrapper);
    action = XMenu_addAction(menu, "UCS-4重载");
    XAction_setAction(action, test_ucs4_overload_wrapper);
    action = XMenu_addAction(menu, "数值转换");
    XAction_setAction(action, test_numeric_conversion_wrapper);
    action = XMenu_addAction(menu, "UTF-8转换");
    XAction_setAction(action, test_utf8_stream_wrapper);
    action = XMenu_addAction(menu, "UTF-16转换");
    XAction_setAction(action, test_utf16_stream_wrapper);
    action = XMenu_addAction(menu, "GBK转换");
    XAction_setAction(action, test_gbk_stream_wrapper);
    action = XMenu_addAction(menu, "SpecialCharacter");
    XAction_setAction(action, test_special_character_wrapper);
}
