/******************************************************************************
 * @file       XXmlStreamReaderTest.c
 * @brief      XXmlStreamReader XML读取器全面测试
 * @author     XinYueC 团队
 * @note       覆盖所有公开API，包括新增的DTD相关功能
 ******************************************************************************/
#include "XXmlStreamReaderTest.h"
#include "XXmlStreamReader.h"
#include "XString.h"
#include "XByteArray.h"
#include "XMenu.h"
#include "XAction.h"
#include "XCoreApplication.h"
#include "XPrintf.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>

/* ==================== 测试辅助宏 ==================== */
#define TEST_PASS(name) XPrintf("[PASS] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[FAIL] %s: %s\n", name, reason)
#define TEST_INFO(fmt, ...) XPrintf("[INFO] " fmt "\n", ##__VA_ARGS__)

/* ==================== 测试函数声明 ==================== */
static bool test_create_delete(void);
static bool test_init_deinit(void);
static bool test_entity_expansion_limit(void);
static bool test_raise_error(void);
static bool test_has_error(void);
static bool test_null_safety(void);
static bool test_notation_declarations_api(void);
static bool test_entity_declarations_api(void);
static bool test_entity_resolver(void);

/* New API coverage tests */
static bool test_line_column_offset(void);
static bool test_standalone_declaration(void);
static bool test_processing_instruction_fields(void);
static bool test_namespace_processing(void);
static bool test_extra_namespace_declaration(void);
static bool test_basic_xml_parse(void);
static bool test_attributes_navigation(void);
static bool test_nested_elements(void);
static bool test_cdata_comment(void);
static bool test_entity_reference(void);
static bool test_dtd_declaration(void);
static bool test_skip_current_element(void);
static bool test_read_next_start_element(void);
static bool test_read_element_text(void);
static bool test_invalid_xml(void);

static bool test_run_all(void);

/* ==================== 包装函数 ==================== */
static void test_create_delete_wrapper(XVariant* d) { (void)d; test_create_delete(); }
static void test_init_deinit_wrapper(XVariant* d) { (void)d; test_init_deinit(); }
static void test_raise_error_wrapper(XVariant* d) { (void)d; test_raise_error(); }
static void test_has_error_wrapper(XVariant* d) { (void)d; test_has_error(); }
static void test_entity_expansion_limit_wrapper(XVariant* d) { (void)d; test_entity_expansion_limit(); }
static void test_null_safety_wrapper(XVariant* d) { (void)d; test_null_safety(); }
static void test_notation_declarations_api_wrapper(XVariant* d) { (void)d; test_notation_declarations_api(); }
static void test_entity_declarations_api_wrapper(XVariant* d) { (void)d; test_entity_declarations_api(); }
static void test_entity_resolver_wrapper(XVariant* d) { (void)d; test_entity_resolver(); }

static void test_line_column_offset_wrapper(XVariant* d) { (void)d; test_line_column_offset(); }
static void test_standalone_declaration_wrapper(XVariant* d) { (void)d; test_standalone_declaration(); }
static void test_processing_instruction_fields_wrapper(XVariant* d) { (void)d; test_processing_instruction_fields(); }
static void test_namespace_processing_wrapper(XVariant* d) { (void)d; test_namespace_processing(); }
static void test_extra_namespace_declaration_wrapper(XVariant* d) { (void)d; test_extra_namespace_declaration(); }
static void test_basic_xml_parse_wrapper(XVariant* d) { (void)d; test_basic_xml_parse(); }
static void test_attributes_navigation_wrapper(XVariant* d) { (void)d; test_attributes_navigation(); }
static void test_nested_elements_wrapper(XVariant* d) { (void)d; test_nested_elements(); }
static void test_cdata_comment_wrapper(XVariant* d) { (void)d; test_cdata_comment(); }
static void test_entity_reference_wrapper(XVariant* d) { (void)d; test_entity_reference(); }
static void test_dtd_declaration_wrapper(XVariant* d) { (void)d; test_dtd_declaration(); }
static void test_skip_current_element_wrapper(XVariant* d) { (void)d; test_skip_current_element(); }
static void test_read_next_start_element_wrapper(XVariant* d) { (void)d; test_read_next_start_element(); }
static void test_read_element_text_wrapper(XVariant* d) { (void)d; test_read_element_text(); }
static void test_invalid_xml_wrapper(XVariant* d) { (void)d; test_invalid_xml(); }

static void test_run_all_wrapper(XVariant* d) { (void)d; test_run_all(); }

/* ==================== 测试1: 创建和删除 ==================== */
static bool test_create_delete(void)
{
    TEST_INFO("===== 创建和删除测试 =====");
    XXmlStreamReader* r = XXmlStreamReader_create();
    if (r) { TEST_PASS("XXmlStreamReader_create"); }
    else { TEST_FAIL("XXmlStreamReader_create", "创建失败"); return false; }
    XXmlStreamReader_delete(r);
    XXmlStreamReader_delete(NULL);
    TEST_PASS("XXmlStreamReader_delete NULL安全");
    return true;
}

/* ==================== 测试2: 初始化和反初始化 ==================== */
static bool test_init_deinit(void)
{
    TEST_INFO("===== 初始化和反初始化测试 =====");
    /* 与 XXmlStreamReader_create 一致：struct + 私有数据 */
    XXmlStreamReader* r = (XXmlStreamReader*)XMalloc_System(sizeof(XXmlStreamReader) + 4096);
    if (!r) { TEST_FAIL("XMalloc_System", "内存分配失败"); return false; }
    memset(r, 0, sizeof(XXmlStreamReader) + 4096);
    XXmlStreamReader_init(r);
    TEST_PASS("XXmlStreamReader_init");
    XXmlStreamReader_deinit(r);
    TEST_PASS("XXmlStreamReader_deinit");
    XFree_System(r);
    return true;
}

/* ==================== 测试3: 引发错误 ==================== */
static bool test_raise_error(void)
{
    TEST_INFO("===== 引发错误测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_raiseError(r, "test error");
    if (XXmlStreamReader_hasError(r)) TEST_PASS("hasError=true after raiseError");
    else { TEST_FAIL("hasError", "应为true"); all_pass = false; }
    if (XXmlStreamReader_error(r) == XXmlStream_CustomError) TEST_PASS("error=CustomError");
    const char* err = XXmlStreamReader_errorString(r);
    if (err && strcmp(err, "test error") == 0) TEST_PASS("errorString match");
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试4: 错误检测 ==================== */
static bool test_has_error(void)
{
    TEST_INFO("===== 错误检测测试 =====");
    XXmlStreamReader* r = XXmlStreamReader_create();
    if (!XXmlStreamReader_hasError(r)) TEST_PASS("new reader has no error");
    else TEST_FAIL("hasError", "新读取器应有错误");
    XXmlStreamReader_delete(r);
    return true;
}

/* ==================== 测试5: 实体扩展限制 ==================== */
static bool test_entity_expansion_limit(void)
{
    TEST_INFO("===== 实体扩展限制测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    int limit = XXmlStreamReader_entityExpansionLimit(r);
    TEST_INFO("default limit: %d", limit);
    XXmlStreamReader_setEntityExpansionLimit(r, 100);
    if (XXmlStreamReader_entityExpansionLimit(r) == 100) TEST_PASS("setEntityExpansionLimit(100)");
    else { TEST_FAIL("set", "设置失败"); all_pass = false; }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试6: 空安全 ==================== */
static bool test_null_safety(void)
{
    TEST_INFO("===== 空安全测试 =====");
    if (XXmlStreamReader_name(NULL) == NULL) TEST_PASS("name(NULL)=NULL");
    if (XXmlStreamReader_namespaceUri(NULL) == NULL) TEST_PASS("namespaceUri(NULL)=NULL");
    if (XXmlStreamReader_text(NULL) == NULL) TEST_PASS("text(NULL)=NULL");
    if (XXmlStreamReader_errorString(NULL) == NULL) TEST_PASS("errorString(NULL)=NULL");
    if (XXmlStreamReader_dtdName(NULL) == NULL) TEST_PASS("dtdName(NULL)=NULL");
    if (XXmlStreamReader_notationDeclarations(NULL) == NULL) TEST_PASS("notationDeclarations(NULL)=NULL");
    if (XXmlStreamReader_entityDeclarations(NULL) == NULL) TEST_PASS("entityDeclarations(NULL)=NULL");
    if (XXmlStreamReader_entityResolver(NULL) == NULL) TEST_PASS("entityResolver(NULL)=NULL");
    return true;
}

/* ==================== 测试7: DTD符号声明API ==================== */
static bool test_notation_declarations_api(void)
{
    TEST_INFO("===== DTD符号声明API测试 =====");
    XXmlStreamNotationDeclarations* decls = XXmlStreamNotationDeclarations_create();
    if (decls) {
        TEST_PASS("XXmlStreamNotationDeclarations_create");
        size_t sz = XXmlStreamNotationDeclarations_size(decls);
        TEST_INFO("size=%zu", sz);
        XXmlStreamNotationDeclarations_delete(decls);
        TEST_PASS("XXmlStreamNotationDeclarations_delete");
    } else {
        TEST_FAIL("create", "创建失败");
    }

    XXmlStreamNotationDeclaration* decl = XXmlStreamNotationDeclaration_create();
    if (decl) {
        TEST_PASS("XXmlStreamNotationDeclaration_create");
        XXmlStreamNotationDeclaration_delete(decl);
        TEST_PASS("XXmlStreamNotationDeclaration_delete");
    }
    return true;
}

/* ==================== 测试8: DTD实体声明API ==================== */
static bool test_entity_declarations_api(void)
{
    TEST_INFO("===== DTD实体声明API测试 =====");
    XXmlStreamEntityDeclarations* decls = XXmlStreamEntityDeclarations_create();
    if (decls) {
        TEST_PASS("XXmlStreamEntityDeclarations_create");
        size_t sz = XXmlStreamEntityDeclarations_size(decls);
        TEST_INFO("size=%zu", sz);
        XXmlStreamEntityDeclarations_delete(decls);
        TEST_PASS("XXmlStreamEntityDeclarations_delete");
    } else {
        TEST_FAIL("create", "创建失败");
    }

    XXmlStreamEntityDeclaration* decl = XXmlStreamEntityDeclaration_create();
    if (decl) {
        TEST_PASS("XXmlStreamEntityDeclaration_create");
        XXmlStreamEntityDeclaration_delete(decl);
        TEST_PASS("XXmlStreamEntityDeclaration_delete");
    }
    return true;
}

/* ==================== 测试9: 实体解析器 ==================== */
static bool test_entity_resolver(void)
{
    TEST_INFO("===== 实体解析器测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();

    XXmlStreamEntityResolver* resolver = XXmlStreamEntityResolver_create();
    if (resolver) TEST_PASS("EntityResolver_create");
    else { TEST_FAIL("create", "创建失败"); all_pass = false; }

    XXmlStreamEntityResolver_init(resolver);
    TEST_PASS("EntityResolver_init");

    XXmlStreamReader_setEntityResolver(r, resolver);
    if (XXmlStreamReader_entityResolver(r) == resolver) TEST_PASS("set/get EntityResolver");
    else { TEST_FAIL("get", "不匹配"); all_pass = false; }

    XXmlStreamEntityResolver_setUserData(resolver, (void*)0x1234);
    if (XXmlStreamEntityResolver_userData(resolver) == (void*)0x1234) TEST_PASS("set/get UserData");
    else { TEST_FAIL("userData", "不匹配"); all_pass = false; }

    XXmlStreamEntityResolver_delete(resolver);
    TEST_PASS("EntityResolver_delete");

    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: 行/列/字符偏移 ==================== */
static bool test_line_column_offset(void)
{
    TEST_INFO("===== 行/列/字符偏移测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    /* 多行 XML，第 2 行的元素用于检查行号 */
    XXmlStreamReader_addData_utf8(r,
        "<?xml version=\"1.0\"?>\n"
        "<root>\n"
        "  <child id=\"1\"/>\n"
        "</root>");
    /* 第一个 token 应该是 StartDocument */
    XXmlStreamReader_readNext(r);
    int64_t ln = XXmlStreamReader_lineNumber(r);
    int64_t col = XXmlStreamReader_columnNumber(r);
    int64_t off = XXmlStreamReader_characterOffset(r);
    if (ln >= 1 && col >= 1 && off >= 0) TEST_PASS("StartDocument 位置");
    else { TEST_FAIL("StartDocument pos", "ln/col/offset 应为正"); all_pass = false; }
    /* 继续读到 child StartElement */
    int safety = 0;
    while (!XXmlStreamReader_atEnd(r) && safety++ < 32) {
        XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_isStartElement(r) && strcmp(XXmlStreamReader_name(r), "child") == 0)
            break;
    }
    /* 找到 child 的开始 */
    if (XXmlStreamReader_isStartElement(r) && strcmp(XXmlStreamReader_name(r), "child") == 0) {
        int64_t lnChild = XXmlStreamReader_lineNumber(r);
        int64_t offChild = XXmlStreamReader_characterOffset(r);
        if (lnChild == 3) TEST_PASS("child 元素行号 = 3");
        else { TEST_FAIL("child line", "应为 3"); all_pass = false; }
        if (offChild > 0) TEST_PASS("child characterOffset > 0");
        else { TEST_FAIL("child offset", "应 > 0"); all_pass = false; }
    } else {
        TEST_FAIL("find child", "未找到 child 开始元素");
        all_pass = false;
    }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: hasStandaloneDeclaration ==================== */
static bool test_standalone_declaration(void)
{
    TEST_INFO("===== standalone 声明测试 =====");
    bool all_pass = true;
    /* 1) 有 standalone 声明 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<?xml version=\"1.0\" standalone=\"yes\"?><a/>");
        XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_hasStandaloneDeclaration(r)) TEST_PASS("standalone=yes -> hasStandaloneDeclaration=true");
        else { TEST_FAIL("standalone=yes", "应为 true"); all_pass = false; }
        if (XXmlStreamReader_isStandaloneDocument(r)) TEST_PASS("standalone=yes -> isStandaloneDocument=true");
        else { TEST_FAIL("isStandalone", "应为 true"); all_pass = false; }
        XXmlStreamReader_delete(r);
    }
    /* 2) 显式 standalone="no" 也算 hasStandalone=true */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<?xml version=\"1.0\" standalone=\"no\"?><a/>");
        XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_hasStandaloneDeclaration(r)) TEST_PASS("standalone=no -> hasStandaloneDeclaration=true");
        else { TEST_FAIL("standalone=no has", "应为 true"); all_pass = false; }
        if (!XXmlStreamReader_isStandaloneDocument(r)) TEST_PASS("standalone=no -> isStandaloneDocument=false");
        else { TEST_FAIL("isStandalone no", "应为 false"); all_pass = false; }
        XXmlStreamReader_delete(r);
    }
    /* 3) 无 standalone 声明 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<?xml version=\"1.0\"?><a/>");
        XXmlStreamReader_readNext(r);
        if (!XXmlStreamReader_hasStandaloneDeclaration(r)) TEST_PASS("无 standalone -> hasStandaloneDeclaration=false");
        else { TEST_FAIL("no standalone", "应为 false"); all_pass = false; }
        XXmlStreamReader_delete(r);
    }
    return all_pass;
}

/* ==================== 测试: 处理指令 target/data ==================== */
static bool test_processing_instruction_fields(void)
{
    TEST_INFO("===== 处理指令字段测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    /* 注意：处理指令在根元素之前 */
    XXmlStreamReader_addData_utf8(r, "<?xml-stylesheet type=\"text/xsl\" href=\"style.xsl\"?><root/>");
    /* 跳到 PI token */
    int type = 0;
    int safety = 0;
    while (!XXmlStreamReader_atEnd(r) && safety++ < 16) {
        type = XXmlStreamReader_readNext(r);
        if (type == XXmlStream_ProcessingInstruction) break;
    }
    if (type == XXmlStream_ProcessingInstruction) {
        const char* target = XXmlStreamReader_processingInstructionTarget(r);
        const char* data = XXmlStreamReader_processingInstructionData(r);
        if (target && strcmp(target, "xml-stylesheet") == 0) TEST_PASS("PI target = xml-stylesheet");
        else { TEST_FAIL("PI target", "应为 xml-stylesheet"); all_pass = false; }
        /* data 解析为 type="text/xsl" href="style.xsl"，至少应非空且包含 text/xsl */
        if (data && strstr(data, "text/xsl") != NULL) TEST_PASS("PI data 包含 text/xsl");
        else { TEST_FAIL("PI data", "应包含 text/xsl"); all_pass = false; }
    } else {
        TEST_FAIL("PI token", "未找到 PI Token");
        all_pass = false;
    }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: 命名空间处理开关 ==================== */
static bool test_namespace_processing(void)
{
    TEST_INFO("===== 命名空间处理开关测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    /* 默认应为 true */
    if (XXmlStreamReader_namespaceProcessing(r)) TEST_PASS("默认 namespaceProcessing=true");
    else { TEST_FAIL("default ns", "应为 true"); all_pass = false; }
    XXmlStreamReader_setNamespaceProcessing(r, false);
    if (!XXmlStreamReader_namespaceProcessing(r)) TEST_PASS("setNamespaceProcessing(false) -> false");
    else { TEST_FAIL("set false", "应为 false"); all_pass = false; }
    XXmlStreamReader_setNamespaceProcessing(r, true);
    if (XXmlStreamReader_namespaceProcessing(r)) TEST_PASS("setNamespaceProcessing(true) -> true");
    else { TEST_FAIL("set true", "应为 true"); all_pass = false; }
    /* NULL 安全 */
    XXmlStreamReader_setNamespaceProcessing(NULL, false);
    if (!XXmlStreamReader_namespaceProcessing(NULL)) TEST_PASS("set/get(NULL) 安全");
    else { TEST_FAIL("NULL safety", "应安全返回 false"); all_pass = false; }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: addExtraNamespaceDeclaration ==================== */
static bool test_extra_namespace_declaration(void)
{
    TEST_INFO("===== addExtraNamespaceDeclaration 测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    /* 添加两个额外声明 */
    XXmlStreamNamespaceDeclaration d1 = {0};
    /* 用临时 XString* 持有 */
    d1.m_prefix = XString_create();
    d1.m_namespaceUri = XString_create();
    XString_assign_utf8(d1.m_prefix, "abc");
    XString_assign_utf8(d1.m_namespaceUri, "http://example.com/abc");
    XXmlStreamNamespaceDeclaration d2 = {0};
    d2.m_prefix = XString_create();
    d2.m_namespaceUri = XString_create();
    XString_assign_utf8(d2.m_prefix, "");
    XString_assign_utf8(d2.m_namespaceUri, "http://default.example.com");
    XXmlStreamNamespaceDeclaration arr[2];
    arr[0] = d1;
    arr[1] = d2;
    XXmlStreamReader_addExtraNamespaceDeclaration(r, &d1);
    XXmlStreamReader_addExtraNamespaceDeclarations(r, arr, 2);
    /* 之后 clear() 应清理（不会泄漏） */
    XXmlStreamReader_clear(r);
    /* 清理临时 XString */
    XString_delete_base(d1.m_prefix); XString_delete_base(d1.m_namespaceUri);
    XString_delete_base(d2.m_prefix); XString_delete_base(d2.m_namespaceUri);
    /* NULL 安全 */
    XXmlStreamReader_addExtraNamespaceDeclaration(NULL, &d1);
    XXmlStreamReader_addExtraNamespaceDeclaration(r, NULL);
    XXmlStreamReader_addExtraNamespaceDeclarations(NULL, arr, 2);
    XXmlStreamReader_addExtraNamespaceDeclarations(r, NULL, 2);
    XXmlStreamReader_addExtraNamespaceDeclarations(r, arr, 0);
    TEST_PASS("addExtraNamespaceDeclaration/DECLs NULL 安全");
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: 基础 XML 解析 ==================== */
static bool test_basic_xml_parse(void)
{
    TEST_INFO("===== 基础 XML 解析测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<root attr=\"v\"><child>hello</child></root>");
    /* 依次读取 token 并验证 */
    int t = XXmlStreamReader_readNext(r); /* StartDocument */
    if (t == XXmlStream_StartDocument) TEST_PASS("StartDocument");
    else { TEST_FAIL("StartDocument", ""); all_pass = false; }
    if (strcmp(XXmlStreamReader_documentVersion(r), "1.0") == 0) TEST_PASS("documentVersion=1.0");
    else { TEST_FAIL("doc ver", ""); all_pass = false; }
    if (strcmp(XXmlStreamReader_documentEncoding(r), "UTF-8") == 0) TEST_PASS("documentEncoding=UTF-8");
    else { TEST_FAIL("doc enc", ""); all_pass = false; }
    t = XXmlStreamReader_readNext(r); /* StartElement root */
    if (t == XXmlStream_StartElement && strcmp(XXmlStreamReader_name(r), "root") == 0) TEST_PASS("StartElement root");
    else { TEST_FAIL("StartElement root", ""); all_pass = false; }
    /* 属性 attr="v" */
    const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(r);
    if (attrs && XXmlStreamAttributes_size((XXmlStreamAttributes*)attrs) == 1) TEST_PASS("属性数量=1");
    else { TEST_FAIL("属性数量", "应为 1"); all_pass = false; }
    if (attrs && strcmp(XXmlStreamAttributes_value((XXmlStreamAttributes*)attrs, "attr"), "v") == 0) TEST_PASS("属性 attr=v");
    else { TEST_FAIL("属性值", "应为 v"); all_pass = false; }
    t = XXmlStreamReader_readNext(r); /* StartElement child */
    if (t == XXmlStream_StartElement && strcmp(XXmlStreamReader_name(r), "child") == 0) TEST_PASS("StartElement child");
    else { TEST_FAIL("StartElement child", ""); all_pass = false; }
    t = XXmlStreamReader_readNext(r); /* Characters hello */
    if (t == XXmlStream_Characters && strcmp(XXmlStreamReader_text(r), "hello") == 0) TEST_PASS("Characters hello");
    else { TEST_FAIL("Characters", ""); all_pass = false; }
    /* 跳到 EndElement child */
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_isEndElement(r))
        XXmlStreamReader_readNext(r);
    if (XXmlStreamReader_isEndElement(r) && strcmp(XXmlStreamReader_name(r), "child") == 0) TEST_PASS("EndElement child");
    else { TEST_FAIL("EndElement child", ""); all_pass = false; }
    /* 越过 EndElement child，EndElement root */
    XXmlStreamReader_readNext(r);
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_isEndElement(r))
        XXmlStreamReader_readNext(r);
    if (XXmlStreamReader_isEndElement(r) && strcmp(XXmlStreamReader_name(r), "root") == 0) TEST_PASS("EndElement root");
    else { TEST_FAIL("EndElement root", ""); all_pass = false; }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: 属性导航 ==================== */
static bool test_attributes_navigation(void)
{
    TEST_INFO("===== 属性导航测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r, "<e a=\"1\" b=\"2\" c=\"3\"/>");
    XXmlStreamReader_readNext(r); /* StartDocument */
    XXmlStreamReader_readNext(r); /* StartElement e */
    const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(r);
    int n = XXmlStreamAttributes_size((XXmlStreamAttributes*)attrs);
    if (n == 3) TEST_PASS("属性数量=3");
    else { TEST_FAIL("属性数量", "应为 3"); all_pass = false; }
    /* 索引访问 */
    const XXmlStreamAttribute* a0 = XXmlStreamAttributes_at((XXmlStreamAttributes*)attrs, 0);
    const XXmlStreamAttribute* a1 = XXmlStreamAttributes_at((XXmlStreamAttributes*)attrs, 1);
    const XXmlStreamAttribute* a2 = XXmlStreamAttributes_at((XXmlStreamAttributes*)attrs, 2);
    if (a0 && a1 && a2 && !XXmlStreamAttributes_at((XXmlStreamAttributes*)attrs, 3)
        && !XXmlStreamAttributes_at((XXmlStreamAttributes*)attrs, -1))
        TEST_PASS("at 索引越界返回 NULL");
    else { TEST_FAIL("at 越界", ""); all_pass = false; }
    /* hasAttribute */
    if (XXmlStreamAttributes_hasAttribute((XXmlStreamAttributes*)attrs, "a")
     && XXmlStreamAttributes_hasAttribute((XXmlStreamAttributes*)attrs, "b")
     && XXmlStreamAttributes_hasAttribute((XXmlStreamAttributes*)attrs, "c")
     && !XXmlStreamAttributes_hasAttribute((XXmlStreamAttributes*)attrs, "z"))
        TEST_PASS("hasAttribute 检查存在/不存在");
    else { TEST_FAIL("hasAttribute", ""); all_pass = false; }
    /* 访问器 */
    if (a0 && strcmp(XXmlStreamAttribute_name(a0), "a") == 0
        && strcmp(XXmlStreamAttribute_value(a0), "1") == 0)
        TEST_PASS("Attribute name/value 访问");
    else { TEST_FAIL("attr 访问", ""); all_pass = false; }
    if (a0 && strcmp(XXmlStreamAttribute_qualifiedName(a0), "a") == 0) TEST_PASS("qualifiedName");
    else { TEST_FAIL("qualifiedName", ""); all_pass = false; }
    /* value 通过 name 查 */
    if (strcmp(XXmlStreamAttributes_value((XXmlStreamAttributes*)attrs, "b"), "2") == 0) TEST_PASS("value by name");
    else { TEST_FAIL("value by name", ""); all_pass = false; }
    /* NULL 安全 */
    XXmlStreamAttributes_delete(NULL);
    XXmlStreamAttributes_size(NULL);
    XXmlStreamAttributes_value(NULL, "x");
    XXmlStreamAttributes_hasAttribute(NULL, "x");
    XXmlStreamAttribute_namespaceUri(NULL);
    XXmlStreamAttribute_name(NULL);
    XXmlStreamAttribute_value(NULL);
    XXmlStreamAttribute_isDefault(NULL);
    TEST_PASS("Attribute(s) NULL 访问安全");
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: 嵌套元素 ==================== */
static bool test_nested_elements(void)
{
    TEST_INFO("===== 嵌套元素测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r, "<a><b><c/></b></a>");
    int depth = 0;
    int maxDepth = 0;
    int safety = 0;
    while (!XXmlStreamReader_atEnd(r) && safety++ < 32) {
        int t = XXmlStreamReader_readNext(r);
        if (t == XXmlStream_StartElement) { depth++; if (depth > maxDepth) maxDepth = depth; }
        else if (t == XXmlStream_EndElement) { depth--; }
        if (XXmlStreamReader_hasError(r)) break;
    }
    if (depth == 0) TEST_PASS("嵌套平衡（最终 depth=0）");
    else { TEST_FAIL("depth", "嵌套不平衡"); all_pass = false; }
    if (maxDepth == 3) TEST_PASS("最大嵌套深度=3 (a->b->c)");
    else { TEST_FAIL("maxDepth", "应为 3"); all_pass = false; }
    if (!XXmlStreamReader_hasError(r)) TEST_PASS("无错误");
    else { TEST_FAIL("hasError", "不应有错误"); all_pass = false; }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: CDATA/注释 ==================== */
static bool test_cdata_comment(void)
{
    TEST_INFO("===== CDATA/注释测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r,
        "<root>before<!--a comment-->middle<![CDATA[<x>raw</x>]]>after</root>");
    int sawCdata = 0, sawComment = 0;
    while (!XXmlStreamReader_atEnd(r)) {
        int t = XXmlStreamReader_readNext(r);
        if (t == XXmlStream_Comment) {
            sawComment = 1;
            const char* tx = XXmlStreamReader_text(r);
            if (tx && strstr(tx, "a comment") != NULL) TEST_PASS("注释内容解析");
            else { TEST_FAIL("comment text", ""); all_pass = false; }
        }
        else if (t == XXmlStream_Characters && XXmlStreamReader_isCDATA(r)) {
            sawCdata = 1;
            const char* tx = XXmlStreamReader_text(r);
            if (tx && strstr(tx, "<x>raw</x>") != NULL) TEST_PASS("CDATA 内容解析（含 < >）");
            else { TEST_FAIL("cdata text", ""); all_pass = false; }
        }
    }
    if (sawComment) TEST_PASS("Comment token 出现");
    else { TEST_FAIL("comment token", ""); all_pass = false; }
    if (sawCdata) TEST_PASS("CDATA token 出现");
    else { TEST_FAIL("cdata token", ""); all_pass = false; }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: 实体引用 ==================== */
static bool test_entity_reference(void)
{
    TEST_INFO("===== 实体引用测试 =====");
    bool all_pass = true;
    /* Qt 的 QXmlStreamReader 对 &amp; 等字符实体展开为 Characters；
       本实现沿用相同语义。验证 reader 不崩溃且能正确展开文本。 */
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r, "<root>&amp; &#65; &lt;tag&gt;</root>");
    int sawText = 0;
    int gotAmp = 0, gotLt = 0, gotGt = 0, got65 = 0;
    int safety = 0;
    while (!XXmlStreamReader_atEnd(r) && safety++ < 16) {
        int t = XXmlStreamReader_readNext(r);
        if (t == XXmlStream_Characters) {
            const char* txt = XXmlStreamReader_text(r);
            sawText = 1;
            if (txt) {
                if (strchr(txt, '&')) gotAmp = 1;
                if (strchr(txt, '<')) gotLt = 1;
                if (strchr(txt, '>')) gotGt = 1;
                if (strchr(txt, 'A')) got65 = 1;
            }
        }
    }
    if (sawText) TEST_PASS("Characters token 出现");
    else { TEST_FAIL("no text", "应出现文本"); all_pass = false; }
    if (gotAmp && gotLt && gotGt && got65) TEST_PASS("实体展开：& < > A");
    else { TEST_FAIL("entity expand", "展开不完整"); all_pass = false; }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: DTD 声明 ==================== */
static bool test_dtd_declaration(void)
{
    TEST_INFO("===== DTD 声明测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r,
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">"
        "<html/>");
    int t = XXmlStreamReader_readNext(r); /* 没有 XML 声明 -> 第一个 token 是 DTD */
    if (t == XXmlStream_DTD) TEST_PASS("DTD token");
    else { TEST_FAIL("DTD token", ""); all_pass = false; }
    const char* dn = XXmlStreamReader_dtdName(r);
    const char* dpi = XXmlStreamReader_dtdPublicId(r);
    const char* dsi = XXmlStreamReader_dtdSystemId(r);
    if (dn && strcmp(dn, "html") == 0) TEST_PASS("dtdName=html");
    else { TEST_FAIL("dtdName", "应为 html"); all_pass = false; }
    if (dpi && strstr(dpi, "W3C") != NULL) TEST_PASS("dtdPublicId 包含 W3C");
    else { TEST_FAIL("dtdPublicId", ""); all_pass = false; }
    if (dsi && strstr(dsi, "w3.org") != NULL) TEST_PASS("dtdSystemId 包含 w3.org");
    else { TEST_FAIL("dtdSystemId", ""); all_pass = false; }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: skipCurrentElement ==================== */
static bool test_skip_current_element(void)
{
    TEST_INFO("===== skipCurrentElement 测试 =====");
    bool all_pass = true;
    /* 简单：跳过一个根元素 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<a><b>x</b><c>y</c></a>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_isStartElement(r))
            XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_isStartElement(r) && strcmp(XXmlStreamReader_name(r), "a") == 0) {
            TEST_PASS("找到 root StartElement");
            XXmlStreamReader_skipCurrentElement(r);
            if (XXmlStreamReader_isEndElement(r) && strcmp(XXmlStreamReader_name(r), "a") == 0)
                TEST_PASS("skip 后停在 EndElement a");
            else { TEST_FAIL("skip result", "应停在 EndElement a"); all_pass = false; }
        } else { TEST_FAIL("find a", ""); all_pass = false; }
        XXmlStreamReader_delete(r);
    }
    return all_pass;
}

/* ==================== 测试: readNextStartElement ==================== */
static bool test_read_next_start_element(void)
{
    TEST_INFO("===== readNextStartElement 测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r,
        "<root>   <!--c-->  <child id=\"1\">v</child>  text  <child2/> </root>");
    /* 第一个 StartElement 应该是 root */
    if (XXmlStreamReader_readNextStartElement(r)
        && strcmp(XXmlStreamReader_name(r), "root") == 0) TEST_PASS("readNextStartElement -> root");
    else { TEST_FAIL("first start", ""); all_pass = false; }
    /* 第二个应是 child */
    if (XXmlStreamReader_readNextStartElement(r)
        && strcmp(XXmlStreamReader_name(r), "child") == 0) TEST_PASS("readNextStartElement -> child");
    else { TEST_FAIL("second start", ""); all_pass = false; }
    /* child2 */
    if (XXmlStreamReader_readNextStartElement(r)
        && strcmp(XXmlStreamReader_name(r), "child2") == 0) TEST_PASS("readNextStartElement -> child2");
    else { TEST_FAIL("third start", ""); all_pass = false; }
    /* 越过 child2 后再调用应失败（已到 root EndElement） */
    if (!XXmlStreamReader_readNextStartElement(r)) TEST_PASS("无更多 StartElement 返回 false");
    else { TEST_FAIL("more start", "应返回 false"); all_pass = false; }
    XXmlStreamReader_delete(r);
    return all_pass;
}

/* ==================== 测试: readElementText ==================== */
static bool test_read_element_text(void)
{
    TEST_INFO("===== readElementText 测试 =====");
    bool all_pass = true;
    /* 简单文本 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<g>hello world</g>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_isStartElement(r))
            XXmlStreamReader_readNext(r);
        const char* txt = XXmlStreamReader_readElementText(r,
            XXmlStream_ReadElementTextBehaviour_ErrorOnUnexpectedElement);
        if (txt && strcmp(txt, "hello world") == 0) TEST_PASS("readElementText = 'hello world'");
        else { TEST_FAIL("readElementText", ""); all_pass = false; }
        XXmlStreamReader_delete(r);
    }
    /* 包含子元素，SkipChildElements 应跳过 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<g>before<x/>after</g>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_isStartElement(r))
            XXmlStreamReader_readNext(r);
        const char* txt = XXmlStreamReader_readElementText(r,
            XXmlStream_ReadElementTextBehaviour_SkipChildElements);
        if (txt && (strstr(txt, "before") != NULL || strstr(txt, "after") != NULL))
            TEST_PASS("SkipChildElements 读取拼接文本");
        else { TEST_FAIL("SkipChildElements", ""); all_pass = false; }
        XXmlStreamReader_delete(r);
    }
    return all_pass;
}

/* ==================== 测试: 非法 XML ==================== */
static bool test_invalid_xml(void)
{
    TEST_INFO("===== 非法 XML 测试 =====");
    bool all_pass = true;
    /* 不平衡 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<a><b></a>");
        while (!XXmlStreamReader_atEnd(r)) {
            XXmlStreamReader_readNext(r);
            if (XXmlStreamReader_hasError(r)) break;
        }
        if (XXmlStreamReader_hasError(r)) TEST_PASS("不平衡 -> hasError=true");
        else { TEST_FAIL("mismatch", "应报错"); all_pass = false; }
        XXmlStreamReader_delete(r);
    }
    /* 未关闭标签 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<a><b></b>");
        while (!XXmlStreamReader_atEnd(r)) {
            XXmlStreamReader_readNext(r);
            if (XXmlStreamReader_hasError(r)) break;
        }
        if (XXmlStreamReader_hasError(r)) TEST_PASS("未关闭 -> hasError=true");
        else { TEST_FAIL("unclosed", "应报错"); all_pass = false; }
        XXmlStreamReader_delete(r);
    }
    return all_pass;
}

/* ==================== 主测试入口 ==================== */

/* ==================== 主测试入口 ==================== */
static bool test_run_all(void)
{
    XPrintf("\n");
    XPrintf("========================================\n");
    XPrintf("  XXmlStreamReader 全面测试\n");
    XPrintf("========================================\n");

    bool result = true;
    result = test_create_delete() && result;
    result = test_init_deinit() && result;
    result = test_has_error() && result;
    result = test_entity_expansion_limit() && result;
    result = test_null_safety() && result;
    result = test_notation_declarations_api() && result;
    result = test_entity_declarations_api() && result;
    /* 新 API 与功能覆盖 */
    result = test_line_column_offset() && result;
    result = test_standalone_declaration() && result;
    result = test_processing_instruction_fields() && result;
    result = test_namespace_processing() && result;
    result = test_extra_namespace_declaration() && result;
    result = test_basic_xml_parse() && result;
    result = test_attributes_navigation() && result;
    result = test_nested_elements() && result;
    result = test_cdata_comment() && result;
    result = test_entity_reference() && result;
    result = test_dtd_declaration() && result;
    result = test_skip_current_element() && result;
    result = test_read_next_start_element() && result;
    result = test_read_element_text() && result;
    result = test_invalid_xml() && result;

    XPrintf("\n========================================\n");
    XPrintf("  XXmlStreamReader 测试完成\n");
    XPrintf("========================================\n");
    return result;
}

/* ==================== 菜单注册 ==================== */
#if DEMOTEST
void XMenu_XXmlStreamReaderTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XXmlStreamReaderTest");
    XMenu_addMenu(root, menu);

    XAction* action = XMenu_addAction(menu, "全部测试");
    XAction_setAction(action, test_run_all_wrapper);
    action = XMenu_addAction(menu, "创建和删除");
    XAction_setAction(action, test_create_delete_wrapper);
    action = XMenu_addAction(menu, "初始化和反初始化");
    XAction_setAction(action, test_init_deinit_wrapper);
    action = XMenu_addAction(menu, "引发错误");
    XAction_setAction(action, test_raise_error_wrapper);
    action = XMenu_addAction(menu, "错误检测");
    XAction_setAction(action, test_has_error_wrapper);
    action = XMenu_addAction(menu, "实体扩展限制");
    XAction_setAction(action, test_entity_expansion_limit_wrapper);
    action = XMenu_addAction(menu, "NULL安全");
    XAction_setAction(action, test_null_safety_wrapper);
    action = XMenu_addAction(menu, "DTD符号声明API");
    XAction_setAction(action, test_notation_declarations_api_wrapper);
    action = XMenu_addAction(menu, "DTD实体声明API");
    XAction_setAction(action, test_entity_declarations_api_wrapper);
    action = XMenu_addAction(menu, "实体解析器");
    XAction_setAction(action, test_entity_resolver_wrapper);
    action = XMenu_addAction(menu, "行/列/字符偏移");
    XAction_setAction(action, test_line_column_offset_wrapper);
    action = XMenu_addAction(menu, "standalone 声明");
    XAction_setAction(action, test_standalone_declaration_wrapper);
    action = XMenu_addAction(menu, "PI target/data");
    XAction_setAction(action, test_processing_instruction_fields_wrapper);
    action = XMenu_addAction(menu, "命名空间开关");
    XAction_setAction(action, test_namespace_processing_wrapper);
    action = XMenu_addAction(menu, "额外命名空间声明");
    XAction_setAction(action, test_extra_namespace_declaration_wrapper);
    action = XMenu_addAction(menu, "基础 XML 解析");
    XAction_setAction(action, test_basic_xml_parse_wrapper);
    action = XMenu_addAction(menu, "属性导航");
    XAction_setAction(action, test_attributes_navigation_wrapper);
    action = XMenu_addAction(menu, "嵌套元素");
    XAction_setAction(action, test_nested_elements_wrapper);
    action = XMenu_addAction(menu, "CDATA/注释");
    XAction_setAction(action, test_cdata_comment_wrapper);
    action = XMenu_addAction(menu, "实体引用");
    XAction_setAction(action, test_entity_reference_wrapper);
    action = XMenu_addAction(menu, "DTD 声明");
    XAction_setAction(action, test_dtd_declaration_wrapper);
    action = XMenu_addAction(menu, "skipCurrentElement");
    XAction_setAction(action, test_skip_current_element_wrapper);
    action = XMenu_addAction(menu, "readNextStartElement");
    XAction_setAction(action, test_read_next_start_element_wrapper);
    action = XMenu_addAction(menu, "readElementText");
    XAction_setAction(action, test_read_element_text_wrapper);
    action = XMenu_addAction(menu, "非法 XML");
    XAction_setAction(action, test_invalid_xml_wrapper);
}
#endif
