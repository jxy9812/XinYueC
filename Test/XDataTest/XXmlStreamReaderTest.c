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
#include "XFile.h"
#include "XMenu.h"
#include "XAction.h"
#include "XCoreApplication.h"
#include "XPrintf.h"
#include "XMemory.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ==================== 测试辅助宏 ==================== */
#define TEST_PASS(name) XPrintf("[通过] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[失败] %s: %s\n", name, reason)
#define TEST_INFO(fmt, ...) XPrintf("[信息] " fmt "\n", ##__VA_ARGS__)

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
static bool test_dtd_declaration_values(void);
static bool test_device_input(void);
static bool test_dtd_copy_move(void);
static bool test_incremental_device_input(void);
static bool test_encoded_input(void);
static bool test_namespace_declarations_api(void);
static bool test_uninitialized_copy_move(void);

/* New API coverage tests */
static bool test_line_column_offset(void);
static bool test_standalone_declaration(void);
static bool test_processing_instruction_fields(void);
static bool test_namespace_processing(void);
static bool test_extra_namespace_declaration(void);
static bool test_basic_xml_parse(void);
static bool test_attributes_navigation(void);
static bool test_stream_value_types(void);
static bool test_nested_elements(void);
static bool test_cdata_comment(void);
static bool test_entity_reference(void);
static bool test_dtd_declaration(void);
static bool test_skip_current_element(void);
static bool test_read_next_start_element(void);
static bool test_read_element_text(void);
static bool test_qt_collection_copy_and_mutation(void);
static bool test_invalid_xml(void);
static bool test_reset_preserves_configuration(void);
static bool test_split_bom_and_single_byte_encoding(void);

bool XXmlStreamReaderTest_runAll(void);

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
static void test_dtd_declaration_values_wrapper(XVariant* d) { (void)d; test_dtd_declaration_values(); }
static void test_device_input_wrapper(XVariant* d) { (void)d; test_device_input(); }
static void test_dtd_copy_move_wrapper(XVariant* d) { (void)d; test_dtd_copy_move(); }
static void test_incremental_device_input_wrapper(XVariant* d) { (void)d; test_incremental_device_input(); }
static void test_encoded_input_wrapper(XVariant* d) { (void)d; test_encoded_input(); }
static void test_namespace_declarations_api_wrapper(XVariant* d) { (void)d; test_namespace_declarations_api(); }
static void test_uninitialized_copy_move_wrapper(XVariant* d) { (void)d; test_uninitialized_copy_move(); }

static void test_line_column_offset_wrapper(XVariant* d) { (void)d; test_line_column_offset(); }
static void test_standalone_declaration_wrapper(XVariant* d) { (void)d; test_standalone_declaration(); }
static void test_processing_instruction_fields_wrapper(XVariant* d) { (void)d; test_processing_instruction_fields(); }
static void test_namespace_processing_wrapper(XVariant* d) { (void)d; test_namespace_processing(); }
static void test_extra_namespace_declaration_wrapper(XVariant* d) { (void)d; test_extra_namespace_declaration(); }
static void test_basic_xml_parse_wrapper(XVariant* d) { (void)d; test_basic_xml_parse(); }
static void test_attributes_navigation_wrapper(XVariant* d) { (void)d; test_attributes_navigation(); }
static void test_stream_value_types_wrapper(XVariant* d) { (void)d; test_stream_value_types(); }
static void test_nested_elements_wrapper(XVariant* d) { (void)d; test_nested_elements(); }
static void test_cdata_comment_wrapper(XVariant* d) { (void)d; test_cdata_comment(); }
static void test_entity_reference_wrapper(XVariant* d) { (void)d; test_entity_reference(); }
static void test_dtd_declaration_wrapper(XVariant* d) { (void)d; test_dtd_declaration(); }
static void test_skip_current_element_wrapper(XVariant* d) { (void)d; test_skip_current_element(); }
static void test_read_next_start_element_wrapper(XVariant* d) { (void)d; test_read_next_start_element(); }
static void test_read_element_text_wrapper(XVariant* d) { (void)d; test_read_element_text(); }
static void test_qt_collection_copy_and_mutation_wrapper(XVariant* d) { (void)d; test_qt_collection_copy_and_mutation(); }
static void test_invalid_xml_wrapper(XVariant* d) { (void)d; test_invalid_xml(); }
static void test_reset_preserves_configuration_wrapper(XVariant* d)
{
    (void)d;
    test_reset_preserves_configuration();
}
static void test_split_bom_and_single_byte_encoding_wrapper(XVariant* d)
{
    (void)d;
    test_split_bom_and_single_byte_encoding();
}

static void test_run_all_wrapper(XVariant* d) { (void)d; XXmlStreamReaderTest_runAll(); }

/* ==================== 测试1: 创建和删除 ==================== */
static bool test_create_delete(void)
{
    TEST_INFO("===== 创建和删除测试 =====");
    XXmlStreamReader* r = XXmlStreamReader_create();
    if (r) { TEST_PASS("XXmlStreamReader_create"); }
    else { TEST_FAIL("XXmlStreamReader_create", "创建失败"); return false; }
    if (XXmlStream_NoToken == 0 && XXmlStream_Invalid == 1 &&
        XXmlStream_StartDocument == 2 && XXmlStream_ProcessingInstruction == 10)
        TEST_PASS("TokenType 数值与 Qt 对齐");
    else { TEST_FAIL("TokenType 数值", "与 Qt 枚举值不一致"); }
    if (strcmp(XXmlStreamReader_tokenString(r), "NoToken") == 0)
        TEST_PASS("tokenString(NoToken)");
    else TEST_FAIL("tokenString(NoToken)", "Token 文本映射错误");
    XByteArray* constructorData = XByteArray_create_utf8("<root/>");
    XString* constructorString = XString_create_utf8("<root/>");
    XXmlStreamReader* byteReader = XXmlStreamReader_create_byteArray(constructorData);
    XXmlStreamReader* stringReader = XXmlStreamReader_create_string(constructorString);
    XXmlStreamReader* utf8Reader = XXmlStreamReader_create_utf8("<root/>");
    XXmlStreamReader* deviceReader = XXmlStreamReader_create_device(NULL);
    if (byteReader && stringReader && utf8Reader && deviceReader)
        TEST_PASS("Reader 数据/字符串/设备构造映射");
    else { TEST_FAIL("Reader 构造映射", "构造函数重载映射失败"); }
    XXmlStreamReader_delete_base(byteReader);
    XXmlStreamReader_delete_base(stringReader);
    XXmlStreamReader_delete_base(utf8Reader);
    XXmlStreamReader_delete_base(deviceReader);
    XByteArray_delete_base(constructorData);
    XString_delete_base(constructorString);
    XXmlStreamReader_delete_base(r);
    XXmlStreamReader_delete_base(NULL);
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
    XXmlStreamReader_deinit_base(r);
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
    XXmlStreamReader_raiseError_utf8(r, "test error");
    if (XXmlStreamReader_hasError(r)) TEST_PASS("hasError=true after raiseError");
    else { TEST_FAIL("hasError", "应为true"); all_pass = false; }
    if (XXmlStreamReader_error(r) == XXmlStream_CustomError) TEST_PASS("error=CustomError");
    const XString* err = XXmlStreamReader_errorString(r);
    if (err && XString_equals_utf8(err, "test error", XChar_CaseSensitive)) TEST_PASS("errorString match");
    XXmlStreamReader_delete_base(r);
    return all_pass;
}

/* ==================== 测试4: 错误检测 ==================== */
static bool test_has_error(void)
{
    TEST_INFO("===== 错误检测测试 =====");
    XXmlStreamReader* r = XXmlStreamReader_create();
    if (!XXmlStreamReader_hasError(r)) TEST_PASS("new reader has no error");
    else TEST_FAIL("hasError", "新读取器应有错误");
    XXmlStreamReader_delete_base(r);
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
    if (limit == 4096) TEST_PASS("默认实体扩展限制为 Qt 的 4096");
    else { TEST_FAIL("默认实体扩展限制", "应为 4096"); all_pass = false; }
    XXmlStreamReader_setEntityExpansionLimit(r, 100);
    if (XXmlStreamReader_entityExpansionLimit(r) == 100) TEST_PASS("setEntityExpansionLimit(100)");
    else { TEST_FAIL("set", "设置失败"); all_pass = false; }
    XXmlStreamReader_delete_base(r);

    r = XXmlStreamReader_create();
    XXmlStreamReader_setEntityExpansionLimit(r, 6);
    XXmlStreamReader_addData_utf8(r,
        "<!DOCTYPE doc [<!ENTITY a \"0123456789\">]><doc>&a;</doc>");
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
        XXmlStreamReader_readNext(r);
    if (XXmlStreamReader_hasError(r) &&
        XXmlStreamReader_error(r) == XXmlStream_NotWellFormedError)
        TEST_PASS("实体扩展超限报告 NotWellFormedError");
    else { TEST_FAIL("实体扩展限制", "未按限制报告错误"); all_pass = false; }
    XXmlStreamReader_delete_base(r);

    /* 与 Qt tst_qxmlstream::entityExpansionLimit 相同的递归实体边界。 */
    const char* nestedEntities =
        "<!DOCTYPE foo ["
        "<!ENTITY a \"0123456789\">"
        "<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\">"
        "<!ENTITY c \"&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;\">"
        "<!ENTITY d \"&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;\">"
        "]><foo>&d;&d;&d;</foo>";
    r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r, nestedEntities);
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
        XXmlStreamReader_readNext(r);
    if (XXmlStreamReader_error(r) == XXmlStream_NotWellFormedError)
        TEST_PASS("默认限制拒绝递归实体扩展");
    else { TEST_FAIL("默认递归实体限制", "应报告 NotWellFormedError"); all_pass = false; }
    XXmlStreamReader_delete_base(r);

    r = XXmlStreamReader_create();
    XXmlStreamReader_setEntityExpansionLimit(r, 9996);
    XXmlStreamReader_addData_utf8(r, nestedEntities);
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
        XXmlStreamReader_readNext(r);
    if (XXmlStreamReader_error(r) == XXmlStream_NotWellFormedError)
        TEST_PASS("递归实体限制 9996 拒绝");
    else { TEST_FAIL("实体限制 9996", "应报告 NotWellFormedError"); all_pass = false; }
    XXmlStreamReader_delete_base(r);

    r = XXmlStreamReader_create();
    XXmlStreamReader_setEntityExpansionLimit(r, 9997);
    XXmlStreamReader_addData_utf8(r, nestedEntities);
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
        XXmlStreamReader_readNext(r);
    if (XXmlStreamReader_error(r) == XXmlStream_NoError)
        TEST_PASS("递归实体限制 9997 通过");
    else { TEST_FAIL("实体限制 9997", "不应报告错误"); all_pass = false; }
    XXmlStreamReader_delete_base(r);
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

    XXmlStreamReader_delete_base(r);
    return all_pass;
}

static const XString* test_resolve_undeclared_entity(const XString* name, void* userData)
{
    if (!name || !userData) return NULL;
    return XString_equals_utf8(name, "external", XChar_CaseSensitive)
        ? (const XString*)userData : NULL;
}

static const XString* test_resolve_declared_entity(
    const XString* publicId, const XString* systemId, void* userData)
{
    if (!userData || !systemId || !XString_equals_utf8(
            systemId, "external-system", XChar_CaseSensitive))
        return NULL;
    const char* publicIdUtf8 = publicId ? XString_toUtf8(publicId) : NULL;
    if (publicIdUtf8 && *publicIdUtf8) return NULL;
    return (const XString*)userData;
}

/* ==================== 测试: DTD 声明内容与实体解析器 ==================== */
static bool test_dtd_declaration_values(void)
{
    TEST_INFO("===== DTD 声明内容/实体解析器测试 =====");
    bool all_pass = true;
    XXmlStreamReader* r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r,
        "<!DOCTYPE doc ["
        "<!NOTATION image PUBLIC \"image-public\">"
        "<!NOTATION binary SYSTEM \"binary-system\">"
        "<!ENTITY internal \"hello\">"
        "<!ENTITY external SYSTEM \"external-system\">"
        "<!ENTITY publicEntity PUBLIC \"entity-public\" \"entity-system\">"
        "<!ENTITY unparsed SYSTEM \"binary-system\" NDATA image>"
        "]><doc>&internal;&external;</doc>");
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
        XXmlStreamReader_readNext(r);
    XXmlStreamNotationDeclarations* notations = XXmlStreamReader_notationDeclarations(r);
    XXmlStreamEntityDeclarations* entities = XXmlStreamReader_entityDeclarations(r);
    const XXmlStreamNotationDeclaration* notation =
        XXmlStreamNotationDeclarations_at(notations, 0);
    const XXmlStreamNotationDeclaration* systemNotation =
        XXmlStreamNotationDeclarations_at(notations, 1);
    const XXmlStreamEntityDeclaration* internal =
        XXmlStreamEntityDeclarations_at(entities, 0);
    const XXmlStreamEntityDeclaration* external = XXmlStreamEntityDeclarations_at(entities, 1);
    const XXmlStreamEntityDeclaration* publicEntity = XXmlStreamEntityDeclarations_at(entities, 2);
    const XXmlStreamEntityDeclaration* unparsed = XXmlStreamEntityDeclarations_at(entities, 3);
    if (!XXmlStreamReader_hasError(r) && notations &&
        XXmlStreamNotationDeclarations_size(notations) == 2 && notation &&
        XString_equals_utf8(notation->m_name, "image", XChar_CaseSensitive) &&
        XString_equals_utf8(notation->m_publicId, "image-public", XChar_CaseSensitive))
        TEST_PASS("NOTATION 声明内容");
    else { TEST_FAIL("NOTATION 声明内容", "解析结果不完整"); all_pass = false; }
    if (!XXmlStreamReader_hasError(r) && systemNotation &&
        XString_equals_utf8(systemNotation->m_name, "binary", XChar_CaseSensitive) &&
        XString_equals_utf8(systemNotation->m_systemId, "binary-system", XChar_CaseSensitive))
        TEST_PASS("NOTATION SYSTEM 声明内容");
    else { TEST_FAIL("NOTATION SYSTEM 声明内容", "解析结果不完整"); all_pass = false; }
    if (!XXmlStreamReader_hasError(r) && entities &&
        XXmlStreamEntityDeclarations_size(entities) == 4 && internal && external &&
        publicEntity && unparsed &&
        XString_equals_utf8(internal->m_name, "internal", XChar_CaseSensitive) &&
        XString_equals_utf8(internal->m_value, "hello", XChar_CaseSensitive) &&
        XString_equals_utf8(external->m_systemId, "external-system", XChar_CaseSensitive) &&
        XString_equals_utf8(publicEntity->m_publicId, "entity-public", XChar_CaseSensitive) &&
        XString_equals_utf8(publicEntity->m_systemId, "entity-system", XChar_CaseSensitive) &&
        XString_equals_utf8(unparsed->m_notationName, "image", XChar_CaseSensitive))
        TEST_PASS("ENTITY SYSTEM/PUBLIC/NDATA 声明内容");
    else { TEST_FAIL("ENTITY 高级声明内容", "解析结果不完整"); all_pass = false; }
    XXmlStreamReader_delete_base(r);

    XString* replacement = XString_create_utf8("resolved");
    XXmlStreamEntityResolver* resolver = XXmlStreamEntityResolver_create();
    if (!replacement || !resolver) {
        if (replacement) XString_delete_base(replacement);
        if (resolver) XXmlStreamEntityResolver_delete(resolver);
        return false;
    }
    resolver->m_userData = replacement;
    resolver->m_resolveUndeclaredEntityCallback = test_resolve_undeclared_entity;
    r = XXmlStreamReader_create();
    XXmlStreamReader_setEntityResolver(r, resolver);
    XXmlStreamReader_addData_utf8(r, "<doc>&external;</doc>");
    bool resolved = false;
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r)) {
        if (XXmlStreamReader_readNext(r) == XXmlStream_Characters &&
            XString_equals_utf8(XXmlStreamReader_text(r), "resolved", XChar_CaseSensitive)) {
            resolved = true;
            break;
        }
    }
    if (resolved) TEST_PASS("未知实体通过 resolver 解析");
    else { TEST_FAIL("未知实体 resolver", "未得到替换文本"); all_pass = false; }
    XXmlStreamReader_delete_base(r);
    XXmlStreamEntityResolver_delete(resolver);
    XString_delete_base(replacement);

    replacement = XString_create_utf8("declared-resolved");
    resolver = XXmlStreamEntityResolver_create();
    if (!replacement || !resolver) {
        if (replacement) XString_delete_base(replacement);
        if (resolver) XXmlStreamEntityResolver_delete(resolver);
        return false;
    }
    XXmlStreamEntityResolver_setUserData(resolver, replacement);
    resolver->m_resolveEntityCallback = test_resolve_declared_entity;
    r = XXmlStreamReader_create();
    XXmlStreamReader_setEntityResolver(r, resolver);
    XXmlStreamReader_addData_utf8(r,
        "<!DOCTYPE doc [<!ENTITY external SYSTEM \"external-system\">]><doc>&external;</doc>");
    resolved = false;
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r)) {
        if (XXmlStreamReader_readNext(r) == XXmlStream_Characters &&
            XString_equals_utf8(XXmlStreamReader_text(r), "declared-resolved", XChar_CaseSensitive)) {
            resolved = true;
            break;
        }
    }
    if (resolved) TEST_PASS("已声明外部实体通过 resolveEntity 解析");
    else { TEST_FAIL("已声明实体 resolver", "未得到替换文本"); all_pass = false; }
    XXmlStreamReader_delete_base(r);
    XXmlStreamEntityResolver_delete(resolver);
    XString_delete_base(replacement);
    return all_pass;
}

/* ==================== 测试: QIODevice 输入 ==================== */
static bool test_device_input(void)
{
    TEST_INFO("===== QIODevice 输入测试 =====");
    bool all_pass = true;
    XString* path = XString_create_utf8("xmlstream_reader_device_test_v2.xml");
    XFile_remove_static(path);
    XFile* writerFile = XFile_create_2(path);
    if (!path || !writerFile || !XFile_open_2(writerFile,
            XIODevice_WriteOnly | XIODevice_Create | XIODevice_Truncate,
            0)) {
        TEST_FAIL("Reader setDevice", "无法创建临时输入文件");
        if (writerFile) XFile_deleteLater(writerFile);
        if (path) { XFile_remove_static(path); XString_delete_base(path); }
        return false;
    }
    XIODevice_write_3((XIODevice*)writerFile, "<device><value>ok</value></device>");
    XIODevice_close_base((XIODevice*)writerFile);
    XFile_deleteLater(writerFile);

    XFile* input = XFile_create_2(path);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    bool opened = input && XFile_open_2(input, XIODevice_ReadOnly | XIODevice_Existing,
        0);
    XXmlStreamReader_addData_utf8(reader, "<old/>");
    XXmlStreamReader_readNext(reader);
    XXmlStreamReader_readNext(reader);
    XXmlStreamReader_setDevice(reader, (XIODevice*)input);
    bool found = false;
    if (XXmlStreamReader_device(reader) == (XIODevice*)input) TEST_PASS("device() 返回关联设备");
    else { TEST_FAIL("device()", "关联设备不一致"); all_pass = false; }
    if (opened && XIODevice_pos_base((XIODevice*)input) == 0)
        TEST_PASS("setDevice 不提前读取设备数据");
    else { TEST_FAIL("setDevice 延迟读取", "设置设备时不应消耗输入数据"); all_pass = false; }
    if (XXmlStreamReader_tokenType(reader) == XXmlStream_NoToken &&
        !XXmlStreamReader_hasError(reader))
        TEST_PASS("setDevice 清除旧解析状态");
    else { TEST_FAIL("setDevice 状态重置", "仍保留旧输入状态"); all_pass = false; }
    if (opened) {
        while (!XXmlStreamReader_atEnd(reader) && !XXmlStreamReader_hasError(reader)) {
            if (XXmlStreamReader_readNext(reader) == XXmlStream_StartElement &&
                XString_equals_utf8(XXmlStreamReader_name(reader), "value", XChar_CaseSensitive)) {
                found = true;
                break;
            }
        }
    }
    if (found) TEST_PASS("setDevice 输入解析");
    else { TEST_FAIL("setDevice 输入解析", "未读到 value 元素"); all_pass = false; }
    XXmlStreamReader_delete_base(reader);
    if (input) { XIODevice_close_base((XIODevice*)input); XFile_deleteLater(input); }
    XFile_remove_static(path);
    XString_delete_base(path);
    return all_pass;
}

/* ==================== 测试: DTD 拷贝与移动所有权 ==================== */
static bool test_dtd_copy_move(void)
{
    TEST_INFO("===== DTD 拷贝/移动测试 =====");
    bool all_pass = true;
    const char* xml = "<!DOCTYPE doc [<!ENTITY item \"value\">]><doc>&item;</doc>";
    XXmlStreamReader* source = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(source, xml);
    while (!XXmlStreamReader_atEnd(source) && !XXmlStreamReader_hasError(source))
        XXmlStreamReader_readNext(source);

    XXmlStreamReader* copy = XXmlStreamReader_create_copy(source);
    XXmlStreamReader_delete_base(source);
    XXmlStreamEntityDeclarations* declarations =
        copy ? XXmlStreamReader_entityDeclarations(copy) : NULL;
    const XXmlStreamEntityDeclaration* declaration =
        XXmlStreamEntityDeclarations_at(declarations, 0);
    if (declaration && XString_equals_utf8(declaration->m_value, "value", XChar_CaseSensitive))
        TEST_PASS("DTD 列表深拷贝独立于源对象");
    else { TEST_FAIL("DTD 深拷贝", "源对象销毁后声明内容失效"); all_pass = false; }
    if (copy) XXmlStreamReader_delete_base(copy);

    source = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(source, xml);
    while (!XXmlStreamReader_atEnd(source) && !XXmlStreamReader_hasError(source))
        XXmlStreamReader_readNext(source);
    XXmlStreamReader* moved = XXmlStreamReader_create_move(source);
    declarations = moved ? XXmlStreamReader_entityDeclarations(moved) : NULL;
    declaration = XXmlStreamEntityDeclarations_at(declarations, 0);
    if (declaration && XString_equals_utf8(declaration->m_name, "item", XChar_CaseSensitive))
        TEST_PASS("DTD 列表移动后仍可访问");
    else { TEST_FAIL("DTD 移动", "移动后声明内容失效"); all_pass = false; }
    if (source) XXmlStreamReader_delete_base(source);
    if (moved) XXmlStreamReader_delete_base(moved);
    return all_pass;
}

/* ==================== 测试: 设备分块输入 ==================== */
static bool test_incremental_device_input(void)
{
    TEST_INFO("===== QIODevice 分块输入测试 =====");
    bool all_pass = true;
    XString* path = XString_create_utf8("xmlstream_reader_incremental_test_v2.xml");
    XFile_remove_static(path);
    XFile* initial = XFile_create_2(path);
    if (!path || !initial || !XFile_open_2(initial,
            XIODevice_ReadWrite | XIODevice_Create | XIODevice_Truncate,
            0)) {
        TEST_FAIL("分块设备准备", "无法创建临时文件");
        if (initial) XFile_deleteLater(initial);
        if (path) { XFile_remove_static(path); XString_delete_base(path); }
        return false;
    }
    XIODevice_write_3((XIODevice*)initial, "<device");
    XIODevice_close_base((XIODevice*)initial);
    XFile_deleteLater(initial);

    XFile* input = XFile_create_2(path);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    bool opened = input && XFile_open_2(input, XIODevice_ReadWrite | XIODevice_Existing,
        0);
    XXmlStreamReader_setDevice(reader, (XIODevice*)input);
    int firstToken = opened ? XXmlStreamReader_readNext(reader) : XXmlStream_Invalid;
    int incompleteToken = opened ? XXmlStreamReader_readNext(reader) : XXmlStream_Invalid;
    if (firstToken == XXmlStream_StartDocument &&
        incompleteToken == XXmlStream_Invalid &&
        XXmlStreamReader_error(reader) == XXmlStream_PrematureEndOfDocumentError)
        TEST_PASS("不完整设备标记等待后续数据");
    else { TEST_FAIL("分块设备初始读取", "未报告文档提前结束"); all_pass = false; }

    const char* suffix = "><value>ok</value></device>";
    bool appendedOk = opened &&
        XIODevice_write_3((XIODevice*)input, suffix) == (int64_t)strlen(suffix);
    if (appendedOk)
        appendedOk = XIODevice_seek_base((XIODevice*)input, 7);

    /* 续读调用会清除 PrematureEndOfDocumentError 并拉取文件新增字节。 */
    int tokenAfterAppend = appendedOk ? XXmlStreamReader_readNext(reader) : XXmlStream_Invalid;
    bool found = tokenAfterAppend == XXmlStream_StartElement &&
        XString_equals_utf8(XXmlStreamReader_name(reader), "value", XChar_CaseSensitive);
    while (appendedOk && !XXmlStreamReader_atEnd(reader) && !XXmlStreamReader_hasError(reader)) {
        if (found) break;
        if (XXmlStreamReader_readNext(reader) == XXmlStream_StartElement &&
            XString_equals_utf8(XXmlStreamReader_name(reader), "value", XChar_CaseSensitive)) {
            found = true;
            break;
        }
    }
    if (found) TEST_PASS("不完整标记追加数据后继续解析");
    else { TEST_FAIL("分块设备续读", "未读取追加后的 value 元素"); all_pass = false; }
    if (reader) XXmlStreamReader_delete_base(reader);
    if (input) { XIODevice_close_base((XIODevice*)input); XFile_deleteLater(input); }
    XFile_remove_static(path);
    XString_delete_base(path);
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
    if (ln >= 1 && col >= 0 && off >= 0) TEST_PASS("StartDocument 位置");
    else { TEST_FAIL("StartDocument pos", "行号应从 1 开始，列号和偏移应从 0 开始"); all_pass = false; }
    /* 继续读到 child StartElement */
    int safety = 0;
    while (!XXmlStreamReader_atEnd(r) && safety++ < 32) {
        XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_isStartElement(r) && XString_equals_utf8(XXmlStreamReader_name(r), "child", XChar_CaseSensitive))
            break;
    }
    /* 找到 child 的开始 */
    if (XXmlStreamReader_isStartElement(r) && XString_equals_utf8(XXmlStreamReader_name(r), "child", XChar_CaseSensitive)) {
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
    XXmlStreamReader_delete_base(r);

    /* Qt 位置按 UTF-16 code unit 计数；同时验证 CRLF 不把 LF 算入下一列。 */
    r = XXmlStreamReader_create_utf8("<root>\r\n  中<child/></root>");
    bool foundUnicodeChild = false;
    safety = 0;
    while (r && !XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r) && safety++ < 32) {
        if (XXmlStreamReader_readNext(r) == XXmlStream_StartElement &&
            XString_equals_utf8(XXmlStreamReader_name(r), "child", XChar_CaseSensitive)) {
            foundUnicodeChild = XXmlStreamReader_lineNumber(r) == 2 &&
                                XXmlStreamReader_columnNumber(r) == 11 &&
                                XXmlStreamReader_characterOffset(r) == 19;
            break;
        }
    }
    if (foundUnicodeChild) TEST_PASS("CRLF 和 Unicode 的 UTF-16 位置");
    else {
        TEST_FAIL("CRLF/Unicode 位置", "列号或字符偏移未按 Qt UTF-16 语义计算");
        all_pass = false;
    }
    XXmlStreamReader_delete_base(r);
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
        XXmlStreamReader_delete_base(r);
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
        XXmlStreamReader_delete_base(r);
    }
    /* 3) 无 standalone 声明 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<?xml version=\"1.0\"?><a/>");
        XXmlStreamReader_readNext(r);
        if (!XXmlStreamReader_hasStandaloneDeclaration(r)) TEST_PASS("无 standalone -> hasStandaloneDeclaration=false");
        else { TEST_FAIL("no standalone", "应为 false"); all_pass = false; }
        XXmlStreamReader_delete_base(r);
    }
    /* 4) XML 规范允许 encoding 后接 standalone，OOXML workbook.xml 会使用该形式。 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r,
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><a/>");
        XXmlStreamReader_readNext(r);
        if (!XXmlStreamReader_hasError(r) &&
            XXmlStreamReader_hasStandaloneDeclaration(r) &&
            XXmlStreamReader_isStandaloneDocument(r))
            TEST_PASS("encoding 后 standalone=yes 可解析");
        else {
            TEST_FAIL("encoding 后 standalone=yes", "合法 XML 声明被拒绝");
            all_pass = false;
        }
        XXmlStreamReader_delete_base(r);
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
        const XString* target = XXmlStreamReader_processingInstructionTarget(r);
        const XString* data = XXmlStreamReader_processingInstructionData(r);
        if (target && XString_equals_utf8(target, "xml-stylesheet", XChar_CaseSensitive)) TEST_PASS("PI target = xml-stylesheet");
        else { TEST_FAIL("PI target", "应为 xml-stylesheet"); all_pass = false; }
        /* data 解析为 type="text/xsl" href="style.xsl"，至少应非空且包含 text/xsl */
        if (data && XString_toUtf8(data) && strstr(XString_toUtf8(data), "text/xsl") != NULL) TEST_PASS("PI data 包含 text/xsl");
        else { TEST_FAIL("PI data", "应包含 text/xsl"); all_pass = false; }
    } else {
        TEST_FAIL("PI token", "未找到 PI Token");
        all_pass = false;
    }
    XXmlStreamReader_delete_base(r);
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
    XXmlStreamReader_delete_base(r);
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
    XXmlStreamReader_delete_base(r);
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
    if (XString_equals_utf8(XXmlStreamReader_documentVersion(r), "1.0", XChar_CaseSensitive)) TEST_PASS("documentVersion=1.0");
    else { TEST_FAIL("doc ver", ""); all_pass = false; }
    if (XString_equals_utf8(XXmlStreamReader_documentEncoding(r), "UTF-8", XChar_CaseSensitive)) TEST_PASS("documentEncoding=UTF-8");
    else { TEST_FAIL("doc enc", ""); all_pass = false; }
    t = XXmlStreamReader_readNext(r); /* StartElement root */
    if (t == XXmlStream_StartElement && XString_equals_utf8(XXmlStreamReader_name(r), "root", XChar_CaseSensitive)) TEST_PASS("StartElement root");
    else { TEST_FAIL("StartElement root", ""); all_pass = false; }
    /* 属性 attr="v" */
    const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(r);
    if (attrs && XXmlStreamAttributes_size((XXmlStreamAttributes*)attrs) == 1) TEST_PASS("属性数量=1");
    else { TEST_FAIL("属性数量", "应为 1"); all_pass = false; }
    if (attrs) {
        XString_Init_Utf8(attrName, "attr");
        const XString* attrVal = XXmlStreamAttributes_value((XXmlStreamAttributes*)attrs, attrName);
        if (attrVal && XString_equals_utf8(attrVal, "v", XChar_CaseSensitive)) TEST_PASS("属性 attr=v");
        else { TEST_FAIL("属性值", "应为 v"); all_pass = false; }
        XString_deinit_base(attrName);
    }
    t = XXmlStreamReader_readNext(r); /* StartElement child */
    if (t == XXmlStream_StartElement && XString_equals_utf8(XXmlStreamReader_name(r), "child", XChar_CaseSensitive)) TEST_PASS("StartElement child");
    else { TEST_FAIL("StartElement child", ""); all_pass = false; }
    t = XXmlStreamReader_readNext(r); /* Characters hello */
    if (t == XXmlStream_Characters && XString_equals_utf8(XXmlStreamReader_text(r), "hello", XChar_CaseSensitive)) TEST_PASS("Characters hello");
    else { TEST_FAIL("Characters", ""); all_pass = false; }
    /* 跳到 EndElement child */
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_isEndElement(r))
        XXmlStreamReader_readNext(r);
    if (XXmlStreamReader_isEndElement(r) && XString_equals_utf8(XXmlStreamReader_name(r), "child", XChar_CaseSensitive)) TEST_PASS("EndElement child");
    else { TEST_FAIL("EndElement child", ""); all_pass = false; }
    /* 越过 EndElement child，EndElement root */
    XXmlStreamReader_readNext(r);
    while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_isEndElement(r))
        XXmlStreamReader_readNext(r);
    if (XXmlStreamReader_isEndElement(r) && XString_equals_utf8(XXmlStreamReader_name(r), "root", XChar_CaseSensitive)) TEST_PASS("EndElement root");
    else { TEST_FAIL("EndElement root", ""); all_pass = false; }
    XXmlStreamReader_delete_base(r);
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
    {
        XString_Init_Utf8(aName, "a");
        XString_Init_Utf8(bName, "b");
        XString_Init_Utf8(cName, "c");
        XString_Init_Utf8(zName, "z");
        if (XXmlStreamAttributes_hasAttribute((XXmlStreamAttributes*)attrs, aName)
         && XXmlStreamAttributes_hasAttribute((XXmlStreamAttributes*)attrs, bName)
         && XXmlStreamAttributes_hasAttribute((XXmlStreamAttributes*)attrs, cName)
         && !XXmlStreamAttributes_hasAttribute((XXmlStreamAttributes*)attrs, zName))
            TEST_PASS("hasAttribute 检查存在/不存在");
        else { TEST_FAIL("hasAttribute", ""); all_pass = false; }
        XString_deinit_base(aName);
        XString_deinit_base(bName);
        XString_deinit_base(cName);
        XString_deinit_base(zName);
    }
    /* 访问器 */
    if (a0 && XString_equals_utf8(XXmlStreamAttribute_name(a0), "a", XChar_CaseSensitive)
        && XString_equals_utf8(XXmlStreamAttribute_value(a0), "1", XChar_CaseSensitive))
        TEST_PASS("Attribute name/value 访问");
    else { TEST_FAIL("attr 访问", ""); all_pass = false; }
    if (a0 && XString_equals_utf8(XXmlStreamAttribute_qualifiedName(a0), "a", XChar_CaseSensitive)) TEST_PASS("qualifiedName");
    else { TEST_FAIL("qualifiedName", ""); all_pass = false; }
    /* value 通过 name 查 */
    {
        XString_Init_Utf8(bName, "b");
        const XString* bVal = XXmlStreamAttributes_value((XXmlStreamAttributes*)attrs, bName);
        if (bVal && XString_equals_utf8(bVal, "2", XChar_CaseSensitive)) TEST_PASS("value by name");
        else { TEST_FAIL("value by name", ""); all_pass = false; }
        XString_deinit_base(bName);
    }
    {
        XString_Init_Utf8(aNameEx, "a");
        if (XXmlStreamAttributes_hasAttribute_ex((XXmlStreamAttributes*)attrs,
                NULL, aNameEx)) TEST_PASS("hasAttribute(namespace,name)");
        else { TEST_FAIL("hasAttribute(namespace,name)", "未找到无命名空间属性"); all_pass = false; }
        XString_deinit_base(aNameEx);
    }
    XXmlStreamReader_delete_base(r);
    r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r, "<e a=\"it&apos;s\"/>");
    XXmlStreamReader_readNext(r);
    XXmlStreamReader_readNext(r);
    XString* aposName = XString_create_utf8("a");
    const XString* aposValue = XXmlStreamAttributes_value(
        (const XXmlStreamAttributes*)XXmlStreamReader_attributes(r), aposName);
    if (aposValue && XString_equals_utf8(aposValue, "it's", XChar_CaseSensitive))
        TEST_PASS("属性 &apos; 展开");
    else { TEST_FAIL("属性 &apos; 展开", "属性实体值解析错误"); all_pass = false; }
    if (aposName) XString_delete_base(aposName);
    XXmlStreamReader_delete_base(r);

    r = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(r, "<e xmlns:p=\"urn:attrs\" p:a=\"1\" a=\"2\"/>");
    XXmlStreamReader_readNext(r);
    XXmlStreamReader_readNext(r);
    XString* namespaceUri = XString_create_utf8("urn:attrs");
    XString* localName = XString_create_utf8("a");
    XString* wrongNamespace = XString_create_utf8("urn:other");
    attrs = XXmlStreamReader_attributes(r);
    if (namespaceUri && localName && wrongNamespace &&
        XXmlStreamAttributes_hasAttribute_ex(attrs, namespaceUri, localName) &&
        !XXmlStreamAttributes_hasAttribute_ex(attrs, wrongNamespace, localName))
        TEST_PASS("hasAttribute(namespace,name) 命名空间匹配/不匹配");
    else { TEST_FAIL("hasAttribute 命名空间", "命名空间筛选结果错误"); all_pass = false; }
    if (namespaceUri) XString_delete_base(namespaceUri);
    if (localName) XString_delete_base(localName);
    if (wrongNamespace) XString_delete_base(wrongNamespace);
    XXmlStreamReader_delete_base(r);
    r = NULL;
    /* NULL 安全 */
    XXmlStreamAttributes_delete(NULL);
    XXmlStreamAttributes_size(NULL);
    {
        XString_Init_Utf8(xName, "x");
        XXmlStreamAttributes_value(NULL, xName);
        XXmlStreamAttributes_hasAttribute(NULL, xName);
        XString_deinit_base(xName);
    }
    XXmlStreamAttribute_namespaceUri(NULL);
    XXmlStreamAttribute_name(NULL);
    XXmlStreamAttribute_value(NULL);
    XXmlStreamAttribute_isDefault(NULL);
    TEST_PASS("Attribute(s) NULL 访问安全");
    XXmlStreamReader_delete_base(r);
    return all_pass;
}

/* ==================== 测试: XML 流值类型与列表便捷 API ==================== */
static bool test_stream_value_types(void)
{
    TEST_INFO("===== XML 流值类型等价与列表别名测试 =====");
    bool all_pass = true;
    XString* namespaceUri = XString_create_utf8("urn:test");
    XString* qualifiedName = XString_create_utf8("p:id");
    XString* localName = XString_create_utf8("id");
    XString* value = XString_create_utf8("42");
    XXmlStreamAttribute* qualified = XXmlStreamAttribute_create(qualifiedName, value);
    XXmlStreamAttribute* qualifiedCopy = XXmlStreamAttribute_create(qualifiedName, value);
    XXmlStreamAttribute* namespaced = XXmlStreamAttribute_create_ex(namespaceUri, localName, value);
    if (qualified && qualifiedCopy &&
        XXmlStreamAttribute_equals(qualified, qualifiedCopy) &&
        !XXmlStreamAttribute_equals(qualified, namespaced) &&
        XXmlStreamAttribute_equals(NULL, NULL) &&
        !XXmlStreamAttribute_equals(qualified, NULL))
        TEST_PASS("QXmlStreamAttribute 等价规则");
    else { TEST_FAIL("QXmlStreamAttribute 等价规则", "属性等价或空指针规则错误"); all_pass = false; }

    XXmlStreamNamespaceDeclaration* declaration =
        XXmlStreamNamespaceDeclaration_create(localName, namespaceUri);
    XXmlStreamNamespaceDeclaration* declarationCopy =
        XXmlStreamNamespaceDeclaration_create(localName, namespaceUri);
    if (declaration && declarationCopy &&
        XXmlStreamNamespaceDeclaration_equals(declaration, declarationCopy) &&
        XXmlStreamNamespaceDeclaration_equals(NULL, NULL))
        TEST_PASS("QXmlStreamNamespaceDeclaration 等价规则");
    else { TEST_FAIL("QXmlStreamNamespaceDeclaration 等价规则", "命名空间声明比较错误"); all_pass = false; }

    XXmlStreamNotationDeclaration* notation = XXmlStreamNotationDeclaration_create();
    XXmlStreamNotationDeclaration* notationCopy = XXmlStreamNotationDeclaration_create();
    XXmlStreamEntityDeclaration* entity = XXmlStreamEntityDeclaration_create();
    XXmlStreamEntityDeclaration* entityCopy = XXmlStreamEntityDeclaration_create();
    if (notation && notationCopy && entity && entityCopy) {
        XString_assign_utf8(notation->m_name, "image");
        XString_assign_utf8(notationCopy->m_name, "image");
        XString_assign_utf8(entity->m_name, "item");
        XString_assign_utf8(entityCopy->m_name, "item");
        XString_assign_utf8(entity->m_notationName, "image");
        XString_assign_utf8(entityCopy->m_notationName, "image");
        XString_assign_utf8(entity->m_value, "value");
        XString_assign_utf8(entityCopy->m_value, "value");
    }
    if (notation && notationCopy && entity && entityCopy &&
        XXmlStreamNotationDeclaration_equals(notation, notationCopy) &&
        XXmlStreamEntityDeclaration_equals(entity, entityCopy) &&
        XString_equals_utf8(XXmlStreamEntityDeclaration_notationName(entity), "image",
            XChar_CaseSensitive))
        TEST_PASS("QXmlStreamNotation/EntityDeclaration 等价规则");
    else { TEST_FAIL("QXmlStreamNotation/EntityDeclaration 等价规则", "DTD 声明比较错误"); all_pass = false; }

    XXmlStreamAttributes* attributes = XXmlStreamAttributes_create();
    XXmlStreamAttributes* attributesCopy = XXmlStreamAttributes_create();
    XXmlStreamNotationDeclarations* notations = XXmlStreamNotationDeclarations_create();
    XXmlStreamNotationDeclarations* notationsCopy = XXmlStreamNotationDeclarations_create();
    XXmlStreamEntityDeclarations* entities = XXmlStreamEntityDeclarations_create();
    XXmlStreamEntityDeclarations* entitiesCopy = XXmlStreamEntityDeclarations_create();
    XXmlStreamNamespaceDeclarations namespaceDeclarations = { NULL, 0 };
    XXmlStreamNamespaceDeclarations namespaceDeclarationsCopy = { NULL, 0 };
    XXmlStreamAttributes_append_ex_utf8(attributes, "id", "42");
    XXmlStreamAttributes_append_ex_utf8(attributesCopy, "id", "42");
    if (attributes && XXmlStreamAttributes_size(attributes) == 1 &&
        XXmlStreamAttributes_count(attributes) == 1 &&
        !XXmlStreamAttributes_isEmpty(attributes) &&
        XXmlStreamAttributes_equals(attributes, attributesCopy) &&
        XXmlStreamAttributes_isEmpty(NULL) &&
        XXmlStreamNotationDeclarations_count(notations) == 0 &&
        XXmlStreamNotationDeclarations_isEmpty(notations) &&
        XXmlStreamNotationDeclarations_equals(notations, notationsCopy) &&
        XXmlStreamEntityDeclarations_count(entities) == 0 &&
        XXmlStreamEntityDeclarations_isEmpty(entities) &&
        XXmlStreamEntityDeclarations_equals(entities, entitiesCopy) &&
        XXmlStreamNamespaceDeclarations_count(&namespaceDeclarations) == 0 &&
        XXmlStreamNamespaceDeclarations_isEmpty(&namespaceDeclarations) &&
        XXmlStreamNamespaceDeclarations_equals(&namespaceDeclarations,
            &namespaceDeclarationsCopy))
        TEST_PASS("XML 流列表 size/count/isEmpty 别名");
    else { TEST_FAIL("XML 流列表 size/count/isEmpty 别名", "列表便捷 API 结果错误"); all_pass = false; }

    XXmlStreamAttributes_delete(attributes);
    XXmlStreamAttributes_delete(attributesCopy);
    XXmlStreamNotationDeclarations_delete(notations);
    XXmlStreamNotationDeclarations_delete(notationsCopy);
    XXmlStreamEntityDeclarations_delete(entities);
    XXmlStreamEntityDeclarations_delete(entitiesCopy);
    XXmlStreamAttribute_delete(qualified);
    XXmlStreamAttribute_delete(qualifiedCopy);
    XXmlStreamAttribute_delete(namespaced);
    XXmlStreamNamespaceDeclaration_delete(declaration);
    XXmlStreamNamespaceDeclaration_delete(declarationCopy);
    XXmlStreamNotationDeclaration_delete(notation);
    XXmlStreamNotationDeclaration_delete(notationCopy);
    XXmlStreamEntityDeclaration_delete(entity);
    XXmlStreamEntityDeclaration_delete(entityCopy);
    XString_delete_base(namespaceUri);
    XString_delete_base(qualifiedName);
    XString_delete_base(localName);
    XString_delete_base(value);
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
    XXmlStreamReader_delete_base(r);
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
            const XString* tx = XXmlStreamReader_text(r);
            if (tx && XString_toUtf8(tx) && strstr(XString_toUtf8(tx), "a comment") != NULL) TEST_PASS("注释内容解析");
            else { TEST_FAIL("comment text", ""); all_pass = false; }
        }
        else if (t == XXmlStream_Characters && XXmlStreamReader_isCDATA(r)) {
            sawCdata = 1;
            const XString* tx = XXmlStreamReader_text(r);
            if (tx && XString_toUtf8(tx) && strstr(XString_toUtf8(tx), "<x>raw</x>") != NULL) TEST_PASS("CDATA 内容解析（含 < >）");
            else { TEST_FAIL("cdata text", ""); all_pass = false; }
        }
    }
    if (sawComment) TEST_PASS("Comment token 出现");
    else { TEST_FAIL("comment token", ""); all_pass = false; }
    if (sawCdata) TEST_PASS("CDATA token 出现");
    else { TEST_FAIL("cdata token", ""); all_pass = false; }
    XXmlStreamReader_delete_base(r);
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
            const XString* txtX = XXmlStreamReader_text(r);
            const char* txt = txtX ? XString_toUtf8(txtX) : NULL;
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
    XXmlStreamReader_delete_base(r);
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
    int t = XXmlStreamReader_readNext(r); /* Qt 先报告 StartDocument，再报告 DTD。 */
    if (t == XXmlStream_StartDocument &&
        XXmlStreamReader_readNext(r) == XXmlStream_DTD) TEST_PASS("DTD token");
    else { TEST_FAIL("DTD token", ""); all_pass = false; }
    const XString* dn = XXmlStreamReader_dtdName(r);
    const XString* dpi = XXmlStreamReader_dtdPublicId(r);
    const XString* dsi = XXmlStreamReader_dtdSystemId(r);
    if (dn && XString_equals_utf8(dn, "html", XChar_CaseSensitive)) TEST_PASS("dtdName=html");
    else { TEST_FAIL("dtdName", "应为 html"); all_pass = false; }
    if (dpi && XString_toUtf8(dpi) && strstr(XString_toUtf8(dpi), "W3C") != NULL) TEST_PASS("dtdPublicId 包含 W3C");
    else { TEST_FAIL("dtdPublicId", ""); all_pass = false; }
    if (dsi && XString_toUtf8(dsi) && strstr(XString_toUtf8(dsi), "w3.org") != NULL) TEST_PASS("dtdSystemId 包含 w3.org");
    else { TEST_FAIL("dtdSystemId", ""); all_pass = false; }
    XXmlStreamReader_delete_base(r);
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
        if (XXmlStreamReader_isStartElement(r) && XString_equals_utf8(XXmlStreamReader_name(r), "a", XChar_CaseSensitive)) {
            TEST_PASS("找到 root StartElement");
            XXmlStreamReader_skipCurrentElement(r);
            if (XXmlStreamReader_isEndElement(r) && XString_equals_utf8(XXmlStreamReader_name(r), "a", XChar_CaseSensitive))
                TEST_PASS("skip 后停在 EndElement a");
            else { TEST_FAIL("skip result", "应停在 EndElement a"); all_pass = false; }
        } else { TEST_FAIL("find a", ""); all_pass = false; }
        XXmlStreamReader_delete_base(r);
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
        && XString_equals_utf8(XXmlStreamReader_name(r), "root", XChar_CaseSensitive)) TEST_PASS("readNextStartElement -> root");
    else { TEST_FAIL("first start", ""); all_pass = false; }
    /* 第二个应是 child */
    if (XXmlStreamReader_readNextStartElement(r)
        && XString_equals_utf8(XXmlStreamReader_name(r), "child", XChar_CaseSensitive)) TEST_PASS("readNextStartElement -> child");
    else { TEST_FAIL("second start", ""); all_pass = false; }
    /* 当前停在 child 的 EndElement，Qt 语义要求本次返回 false。 */
    if (XXmlStreamReader_readNextStartElement(r)
        && XString_equals_utf8(XXmlStreamReader_name(r), "child2", XChar_CaseSensitive)) {
        TEST_FAIL("子元素结束边界", "未按 Qt 语义停在 child 的结束标签");
        all_pass = false;
    } else if (XXmlStreamReader_isEndElement(r) &&
               XString_equals_utf8(XXmlStreamReader_name(r), "child", XChar_CaseSensitive)) {
        TEST_PASS("子元素结束时返回 false");
    } else {
        TEST_FAIL("子元素结束边界", "未停在 child 的结束标签");
        all_pass = false;
    }
    /* 从 child 的结束标签继续读取，应找到后续 child2。 */
    if (XXmlStreamReader_readNextStartElement(r) &&
        XString_equals_utf8(XXmlStreamReader_name(r), "child2", XChar_CaseSensitive)) {
        TEST_PASS("readNextStartElement 找到后续 child2");
    } else {
        TEST_FAIL("readNextStartElement 后续元素", "未找到 child2");
        all_pass = false;
    }
    /* 空元素 child2 的下一个 token 是其结束标签。 */
    if (!XXmlStreamReader_readNextStartElement(r) && XXmlStreamReader_isEndElement(r))
        TEST_PASS("空元素结束时返回 false");
    else { TEST_FAIL("空元素结束边界", "应返回 false 并停在结束标签"); all_pass = false; }
    XXmlStreamReader_delete_base(r);
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
        const XString* txt = XXmlStreamReader_readElementText(r,
            XXmlStream_ReadElementTextBehaviour_ErrorOnUnexpectedElement);
        if (txt && XString_equals_utf8(txt, "hello world", XChar_CaseSensitive)) TEST_PASS("readElementText = 'hello world'");
        else { TEST_FAIL("readElementText", ""); all_pass = false; }
        const XString* empty = XXmlStreamReader_readElementText(r,
            XXmlStream_ReadElementTextBehaviour_ErrorOnUnexpectedElement);
        if (empty && XString_isEmpty_base(empty)) TEST_PASS("非开始元素 readElementText 返回空");
        else { TEST_FAIL("非开始元素 readElementText", "不应返回上一次缓存"); all_pass = false; }
        XXmlStreamReader_delete_base(r);
    }
    /* 包含子元素，SkipChildElements 应跳过 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<g>before<x/>after</g>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_isStartElement(r))
            XXmlStreamReader_readNext(r);
        const XString* txt = XXmlStreamReader_readElementText(r,
            XXmlStream_ReadElementTextBehaviour_SkipChildElements);
        if (txt && XString_toUtf8(txt) && (strstr(XString_toUtf8(txt), "before") != NULL || strstr(XString_toUtf8(txt), "after") != NULL))
            TEST_PASS("SkipChildElements 读取拼接文本");
        else { TEST_FAIL("SkipChildElements", ""); all_pass = false; }
        XXmlStreamReader_delete_base(r);
    }
    return all_pass;
}

/* ==================== 测试: Qt 集合值语义与 UTF-16 输入 ==================== */
static bool test_qt_collection_copy_and_mutation(void)
{
    TEST_INFO("===== Qt 集合深拷贝/修改和 XString 输入测试 =====");
    bool all_pass = true;

    XString* name = XString_create_utf8("item");
    XString* value = XString_create_utf8("值");
    XXmlStreamAttribute* attribute = XXmlStreamAttribute_create(name, value);
    XXmlStreamAttributes* attributes = XXmlStreamAttributes_create();
    XXmlStreamAttributes* copiedAttributes = NULL;
    if (attribute && attributes &&
        XXmlStreamAttributes_appendAttribute(attributes, attribute) &&
        XXmlStreamAttributes_insert(attributes, 0, attribute) &&
        XXmlStreamAttributes_size(attributes) == 2 &&
        (copiedAttributes = XXmlStreamAttributes_create_copy(attributes)) &&
        XXmlStreamAttributes_removeAt(copiedAttributes, 0) &&
        XXmlStreamAttributes_size(copiedAttributes) == 1 &&
        XXmlStreamAttributes_size(attributes) == 2)
        TEST_PASS("属性列表深拷贝和插入/删除");
    else { TEST_FAIL("属性列表修改", "QList 等价操作失败"); all_pass = false; }
    XXmlStreamAttributes_clear(copiedAttributes);
    XXmlStreamAttributes_delete(copiedAttributes);
    XXmlStreamAttributes_delete(attributes);
    XXmlStreamAttribute_delete(attribute);
    XString_delete_base(value);
    XString_delete_base(name);

    XString* prefix = XString_create_utf8("p");
    XString* namespaceUri = XString_create_utf8("urn:copy");
    XXmlStreamNamespaceDeclaration* namespaceDeclaration =
        XXmlStreamNamespaceDeclaration_create(prefix, namespaceUri);
    XXmlStreamNamespaceDeclarations* namespaceDeclarations =
        XXmlStreamNamespaceDeclarations_create();
    XXmlStreamNamespaceDeclarations* namespaceCopy = NULL;
    if (namespaceDeclaration && namespaceDeclarations &&
        XXmlStreamNamespaceDeclarations_append(namespaceDeclarations, namespaceDeclaration) &&
        (namespaceCopy = XXmlStreamNamespaceDeclarations_create_copy(namespaceDeclarations)) &&
        XXmlStreamNamespaceDeclarations_insert(namespaceCopy, 0, namespaceDeclaration) &&
        XXmlStreamNamespaceDeclarations_size(namespaceCopy) == 2 &&
        XXmlStreamNamespaceDeclarations_removeAt(namespaceCopy, 0) &&
        XXmlStreamNamespaceDeclarations_size(namespaceCopy) == 1)
        TEST_PASS("命名空间列表深拷贝和修改");
    else { TEST_FAIL("命名空间列表修改", "独立 QList 映射失败"); all_pass = false; }
    XXmlStreamNamespaceDeclarations_clear(namespaceCopy);
    XXmlStreamNamespaceDeclarations_delete(namespaceCopy);
    XXmlStreamNamespaceDeclarations_delete(namespaceDeclarations);
    XXmlStreamNamespaceDeclaration_delete(namespaceDeclaration);
    XString_delete_base(namespaceUri);
    XString_delete_base(prefix);

    XXmlStreamReader* reader = XXmlStreamReader_create();
    XString* xml = XString_create_utf8("<root xmlns:p=\"urn:p\" p:id=\"值\"/>");
    XXmlStreamReader_addData_string(reader, xml);
    XXmlStreamReader_readNext(reader);
    XXmlStreamReader_readNext(reader);
    XXmlStreamAttributes* readerAttributes = XXmlStreamReader_attributes_copy(reader);
    XXmlStreamNamespaceDeclarations* readerNamespaces =
        XXmlStreamReader_namespaceDeclarations_copy(reader);
    if (readerAttributes && readerNamespaces &&
        XXmlStreamAttributes_size(readerAttributes) == 1 &&
        XXmlStreamNamespaceDeclarations_size(readerNamespaces) == 1)
        TEST_PASS("XString UTF-16 输入和 Reader 集合深拷贝");
    else { TEST_FAIL("Reader 集合深拷贝", "UTF-16 输入或独立集合为空"); all_pass = false; }
    XXmlStreamNamespaceDeclarations_delete(readerNamespaces);
    XXmlStreamAttributes_delete(readerAttributes);
    XXmlStreamReader_delete_base(reader);
    XString_delete_base(xml);

    XXmlStreamNotationDeclaration* notation = XXmlStreamNotationDeclaration_create();
    XXmlStreamNotationDeclarations* notations = XXmlStreamNotationDeclarations_create();
    XXmlStreamEntityDeclaration* entity = XXmlStreamEntityDeclaration_create();
    XXmlStreamEntityDeclarations* entities = XXmlStreamEntityDeclarations_create();
    bool dtdListsOk = notation && notations && entity && entities &&
        XXmlStreamNotationDeclarations_append(notations, notation) &&
        XXmlStreamNotationDeclarations_removeAt(notations, 0) &&
        XXmlStreamEntityDeclarations_append(entities, entity);
    XXmlStreamEntityDeclarations_clear(entities);
    if (dtdListsOk)
        TEST_PASS("Notation/Entity 列表修改 API");
    else { TEST_FAIL("DTD 列表修改", "列表追加或清空失败"); all_pass = false; }
    XXmlStreamNotationDeclarations_delete(notations);
    XXmlStreamEntityDeclarations_delete(entities);
    XXmlStreamNotationDeclaration_delete(notation);
    XXmlStreamEntityDeclaration_delete(entity);
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
        XXmlStreamReader_delete_base(r);
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
        XXmlStreamReader_delete_base(r);
    }
    /* 多根元素：Qt 的 QXmlStreamReader 必须报告格式错误。 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<a/><b/>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
            XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_hasError(r)) TEST_PASS("多根元素 -> hasError=true");
        else { TEST_FAIL("multiple roots", "应报错"); all_pass = false; }
        XXmlStreamReader_delete_base(r);
    }
    /* XML 声明之后没有根元素同样不是完整 XML 文档。 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<?xml version=\"1.0\"?>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
            XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_hasError(r)) TEST_PASS("缺少根元素 -> hasError=true");
        else { TEST_FAIL("missing root", "应报错"); all_pass = false; }
        XXmlStreamReader_delete_base(r);
    }
    /* 根元素外的非空白文本不合法。 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "text<root/>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
            XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_hasError(r)) TEST_PASS("根元素外文本 -> hasError=true");
        else { TEST_FAIL("text outside root", "应报错"); all_pass = false; }
        XXmlStreamReader_delete_base(r);
    }
    /* DTD 内部子集未闭合。 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r, "<!DOCTYPE doc [<!ENTITY item \"value\">]<doc/>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
            XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_hasError(r)) TEST_PASS("DTD 语法错误 -> hasError=true");
        else { TEST_FAIL("DTD malformed", "应报错"); all_pass = false; }
        XXmlStreamReader_delete_base(r);
    }
    /* DTD 内部声明类型非法，覆盖 parse_dtd_subset 的错误分支。 */
    {
        XXmlStreamReader* r = XXmlStreamReader_create();
        XXmlStreamReader_addData_utf8(r,
            "<!DOCTYPE doc [<!ENTITY item BOGUS \"value\">]><doc/>");
        while (!XXmlStreamReader_atEnd(r) && !XXmlStreamReader_hasError(r))
            XXmlStreamReader_readNext(r);
        if (XXmlStreamReader_hasError(r)) TEST_PASS("DTD 内部声明错误 -> hasError=true");
        else { TEST_FAIL("DTD subset malformed", "应报错"); all_pass = false; }
        XXmlStreamReader_delete_base(r);
    }
    return all_pass;
}

/* ==================== 主测试入口 ==================== */

/* ==================== 主测试入口 ==================== */
static bool test_encoded_document(const uint8_t* bytes, size_t length, const char* label)
{
    XXmlStreamReader* reader = XXmlStreamReader_create();
    XByteArray* data = XByteArray_create_with_data((const char*)bytes, length);
    bool ok = reader && data;
    bool sawRoot = false;
    int guard = 0;

    if (ok) {
        XXmlStreamReader_addData(reader, data);
        while (!XXmlStreamReader_atEnd(reader) && guard++ < 16) {
            if (XXmlStreamReader_readNext(reader) == XXmlStream_StartElement &&
                XString_equals_utf8(XXmlStreamReader_name(reader), "root",
                                     XChar_CaseSensitive)) {
                sawRoot = true;
            }
            if (XXmlStreamReader_hasError(reader)) break;
        }
        ok = sawRoot && !XXmlStreamReader_hasError(reader);
    }

    if (ok) TEST_PASS(label);
    else TEST_FAIL(label, "编码 XML 解析失败");
    if (data) XByteArray_delete_base(data);
    if (reader) XXmlStreamReader_delete_base(reader);
    return ok;
}

static bool test_encoded_input(void)
{
    TEST_INFO("===== UTF-8/UTF-16/UTF-32 编码输入测试 =====");
    static const uint8_t utf8[] = {
        0xef, 0xbb, 0xbf, '<', 'r', 'o', 'o', 't', '/', '>'
    };
    static const uint8_t utf16le[] = {
        0xff, 0xfe,
        0x3c, 0x00, 0x72, 0x00, 0x6f, 0x00, 0x6f, 0x00,
        0x74, 0x00, 0x2f, 0x00, 0x3e, 0x00
    };
    static const uint8_t utf16be[] = {
        0xfe, 0xff,
        0x00, 0x3c, 0x00, 0x72, 0x00, 0x6f, 0x00, 0x6f,
        0x00, 0x74, 0x00, 0x2f, 0x00, 0x3e
    };
    static const uint8_t utf32le[] = {
        0xff, 0xfe, 0x00, 0x00,
        0x3c, 0x00, 0x00, 0x00, 0x72, 0x00, 0x00, 0x00,
        0x6f, 0x00, 0x00, 0x00, 0x6f, 0x00, 0x00, 0x00,
        0x74, 0x00, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x00,
        0x3e, 0x00, 0x00, 0x00
    };
    static const uint8_t utf32be[] = {
        0x00, 0x00, 0xfe, 0xff,
        0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x72,
        0x00, 0x00, 0x00, 0x6f, 0x00, 0x00, 0x00, 0x6f,
        0x00, 0x00, 0x00, 0x74, 0x00, 0x00, 0x00, 0x2f,
        0x00, 0x00, 0x00, 0x3e
    };
    bool allPass = true;
    allPass = test_encoded_document(utf8, sizeof(utf8), "UTF-8 BOM 编码输入") && allPass;
    allPass = test_encoded_document(utf16le, sizeof(utf16le), "UTF-16LE 编码输入") && allPass;
    allPass = test_encoded_document(utf16be, sizeof(utf16be), "UTF-16BE 编码输入") && allPass;
    allPass = test_encoded_document(utf32le, sizeof(utf32le), "UTF-32LE 编码输入") && allPass;
    allPass = test_encoded_document(utf32be, sizeof(utf32be), "UTF-32BE 编码输入") && allPass;
    return allPass;
}

/* ==================== 测试: 输入重置和扩展编码 ==================== */
static bool test_reset_preserves_configuration(void)
{
    TEST_INFO("===== clear/setDevice 配置保留测试 =====");
    bool allPass = true;
    XXmlStreamReader* reader = XXmlStreamReader_create();
    XXmlStreamEntityResolver* resolver = XXmlStreamEntityResolver_create();
    XString* prefix = XString_create_utf8("p");
    XString* uri = XString_create_utf8("urn:extra");
    XXmlStreamNamespaceDeclaration* extra =
        XXmlStreamNamespaceDeclaration_create(prefix, uri);
    if (!reader || !resolver || !extra) {
        TEST_FAIL("重置配置准备", "测试对象创建失败");
        allPass = false;
    } else {
        XXmlStreamReader_setEntityResolver(reader, resolver);
        XXmlStreamReader_setEntityExpansionLimit(reader, 123);
        XXmlStreamReader_addExtraNamespaceDeclaration(reader, extra);
        XXmlStreamReader_clear(reader);
        bool clearPreserved =
            XXmlStreamReader_entityResolver(reader) == resolver &&
            XXmlStreamReader_entityExpansionLimit(reader) == 123;
        if (clearPreserved) TEST_PASS("clear 保留实体解析器和扩展限制");
        else { TEST_FAIL("clear 配置保留", "clear 清除了 Qt 要求保留的配置"); allPass = false; }

        XXmlStreamReader_setDevice(reader, NULL);
        if (XXmlStreamReader_entityResolver(reader) == resolver &&
            XXmlStreamReader_entityExpansionLimit(reader) == 123)
            TEST_PASS("setDevice 保留实体解析器和扩展限制");
        else { TEST_FAIL("setDevice 配置保留", "setDevice 清除了 Qt 要求保留的配置"); allPass = false; }
    }
    XXmlStreamNamespaceDeclaration_delete(extra);
    XString_delete_base(prefix);
    XString_delete_base(uri);
    XXmlStreamEntityResolver_delete(resolver);
    XXmlStreamReader_delete_base(reader);
    return allPass;
}

static bool test_split_bom_and_single_byte_encoding(void)
{
    TEST_INFO("===== 拆分 BOM、Latin1 和 ASCII 编码测试 =====");
    bool allPass = true;
    {
        static const char firstBytes[] = { (char)0xff };
        static const char secondBytes[] = {
            (char)0xfe,
            '<', 0, 'r', 0, 'o', 0, 'o', 0, 't', 0, '/', 0, '>', 0
        };
        XByteArray* first = XByteArray_create_with_data(firstBytes, sizeof(firstBytes));
        XByteArray* second = XByteArray_create_with_data(secondBytes, sizeof(secondBytes));
        XXmlStreamReader* reader = XXmlStreamReader_create();
        bool sawRoot = false;
        int guard = 0;
        if (reader && first && second) {
            XXmlStreamReader_addData(reader, first);
            XXmlStreamReader_addData(reader, second);
            while (!XXmlStreamReader_atEnd(reader) && guard++ < 16) {
                if (XXmlStreamReader_readNext(reader) == XXmlStream_StartElement &&
                    XString_equals_utf8(XXmlStreamReader_name(reader), "root", XChar_CaseSensitive))
                    sawRoot = true;
                if (XXmlStreamReader_hasError(reader)) break;
            }
        }
        if (sawRoot && !XXmlStreamReader_hasError(reader)) TEST_PASS("拆分 UTF-16LE BOM 后继续解析");
        else { TEST_FAIL("拆分 BOM", "BOM 分片未正确锁定 UTF-16LE"); allPass = false; }
        XXmlStreamReader_delete_base(reader);
        XByteArray_delete_base(first);
        XByteArray_delete_base(second);
    }
    {
        static const char latin1[] =
            "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><root>\xe9</root>";
        XByteArray* data = XByteArray_create_with_data(latin1, sizeof(latin1) - 1);
        XXmlStreamReader* reader = XXmlStreamReader_create();
        bool sawText = false;
        int guard = 0;
        if (reader && data) {
            XXmlStreamReader_addData(reader, data);
            while (!XXmlStreamReader_atEnd(reader) && guard++ < 32) {
                if (XXmlStreamReader_readNext(reader) == XXmlStream_Characters &&
                    XString_length_base(XXmlStreamReader_text(reader)) == 1)
                    sawText = true;
                if (XXmlStreamReader_hasError(reader)) break;
            }
        }
        if (sawText && !XXmlStreamReader_hasError(reader)) TEST_PASS("ISO-8859-1 声明和单字节字符");
        else { TEST_FAIL("Latin1 编码", "ISO-8859-1 输入未完成归一化"); allPass = false; }
        XXmlStreamReader_delete_base(reader);
        XByteArray_delete_base(data);
    }
    {
        XXmlStreamReader* reader = XXmlStreamReader_create_utf8(
            "<?xml version=\"1.0\" encoding=\"US-ASCII\"?><root>ok</root>");
        int guard = 0;
        while (reader && !XXmlStreamReader_atEnd(reader) && !XXmlStreamReader_hasError(reader) && guard++ < 32)
            XXmlStreamReader_readNext(reader);
        if (reader && !XXmlStreamReader_hasError(reader)) TEST_PASS("US-ASCII 声明");
        else { TEST_FAIL("ASCII 编码", "US-ASCII 声明未通过"); allPass = false; }
        XXmlStreamReader_delete_base(reader);
    }
    return allPass;
}

static bool test_namespace_declarations_api(void)
{
    TEST_INFO("===== 命名空间声明列表数量/索引测试 =====");
    bool allPass = true;
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) return false;

    XXmlStreamReader_addData_utf8(reader,
        "<root xmlns:p=\"urn:p\" xmlns=\"urn:default\"/>");
    XXmlStreamReader_readNext(reader);
    XXmlStreamReader_readNext(reader);

    const XXmlStreamNamespaceDeclarations* declarations =
        XXmlStreamReader_namespaceDeclarations(reader);
    int count = XXmlStreamNamespaceDeclarations_size(declarations);
    bool foundPrefix = false;
    bool foundDefault = false;
    for (int i = 0; i < count; ++i) {
        const XXmlStreamNamespaceDeclaration* declaration =
            XXmlStreamNamespaceDeclarations_at(declarations, i);
        if (!declaration) continue;
        if (XString_equals_utf8(XXmlStreamNamespaceDeclaration_prefix(declaration), "p",
                                 XChar_CaseSensitive) &&
            XString_equals_utf8(XXmlStreamNamespaceDeclaration_namespaceUri(declaration),
                                "urn:p", XChar_CaseSensitive)) {
            foundPrefix = true;
        }
        if (XString_equals_utf8(XXmlStreamNamespaceDeclaration_prefix(declaration), "",
                                 XChar_CaseSensitive) &&
            XString_equals_utf8(XXmlStreamNamespaceDeclaration_namespaceUri(declaration),
                                "urn:default", XChar_CaseSensitive)) {
            foundDefault = true;
        }
    }
    if (count == 2 && foundPrefix && foundDefault &&
        !XXmlStreamNamespaceDeclarations_at(declarations, -1) &&
        !XXmlStreamNamespaceDeclarations_at(declarations, count)) {
        TEST_PASS("命名空间声明列表数量和索引");
    } else {
        TEST_FAIL("命名空间声明列表数量和索引", "列表内容或边界不正确");
        allPass = false;
    }

    XXmlStreamReader_readNext(reader);
    if (XXmlStreamNamespaceDeclarations_size(
            XXmlStreamReader_namespaceDeclarations(reader)) == 0) {
        TEST_PASS("非开始元素的命名空间声明列表为空");
    } else {
        TEST_FAIL("非开始元素的命名空间声明列表", "列表应为空");
        allPass = false;
    }
    if (XXmlStreamNamespaceDeclarations_size(NULL) == 0 &&
        XXmlStreamNamespaceDeclarations_at(NULL, 0) == NULL) {
        TEST_PASS("命名空间声明列表空指针安全");
    } else {
        TEST_FAIL("命名空间声明列表空指针安全", "空指针访问不安全");
        allPass = false;
    }
    XXmlStreamReader_delete_base(reader);
    return allPass;
}

static bool test_uninitialized_copy_move(void)
{
    TEST_INFO("===== 未初始化目标拷贝/移动测试 =====");
    bool allPass = true;
    XXmlStreamReader* source = XXmlStreamReader_create();
    if (!source) return false;
    XXmlStreamReader_addData_utf8(source, "<root/>");
    XXmlStreamReader_readNext(source);
    XXmlStreamReader_readNext(source);

    XXmlStreamReader* copied = (XXmlStreamReader*)XMalloc_System(
        sizeof(XXmlStreamReader) + 4096);
    XXmlStreamReader* moved = (XXmlStreamReader*)XMalloc_System(
        sizeof(XXmlStreamReader) + 4096);
    if (!copied || !moved) {
        if (copied) XFree_System(copied);
        if (moved) XFree_System(moved);
        XXmlStreamReader_delete_base(source);
        return false;
    }
    memset(copied, 0, sizeof(XXmlStreamReader) + 4096);
    memset(moved, 0, sizeof(XXmlStreamReader) + 4096);

    XClass_copy_base((XClass*)copied, (const XClass*)source);
    if (XXmlStreamReader_tokenType(copied) == XXmlStream_StartElement &&
        XString_equals_utf8(XXmlStreamReader_name(copied), "root", XChar_CaseSensitive)) {
        TEST_PASS("读取器拷贝自动初始化空目标");
    } else {
        TEST_FAIL("读取器拷贝自动初始化空目标", "拷贝目标无效");
        allPass = false;
    }
    XXmlStreamReader_deinit_base(copied);

    XClass_move_base((XClass*)moved, source);
    if (XXmlStreamReader_tokenType(moved) == XXmlStream_StartElement &&
        XString_equals_utf8(XXmlStreamReader_name(moved), "root", XChar_CaseSensitive)) {
        TEST_PASS("读取器移动自动初始化空目标");
    } else {
        TEST_FAIL("读取器移动自动初始化空目标", "移动目标无效");
        allPass = false;
    }
    XXmlStreamReader_deinit_base(moved);
    XFree_System(copied);
    XFree_System(moved);
    XXmlStreamReader_delete_base(source);
    return allPass;
}

bool XXmlStreamReaderTest_runAll(void)
{
    XPrintf("\n");
    XPrintf("========================================\n");
    XPrintf("  XXmlStreamReader 全面测试\n");
    XPrintf("========================================\n");

    bool result = true;
    result = test_create_delete() && result;
    result = test_init_deinit() && result;
    result = test_raise_error() && result;
    result = test_has_error() && result;
    result = test_entity_expansion_limit() && result;
    result = test_null_safety() && result;
    result = test_notation_declarations_api() && result;
    result = test_entity_declarations_api() && result;
    result = test_entity_resolver() && result;
    result = test_dtd_declaration_values() && result;
    result = test_device_input() && result;
    result = test_dtd_copy_move() && result;
    result = test_incremental_device_input() && result;
    result = test_encoded_input() && result;
    result = test_reset_preserves_configuration() && result;
    result = test_split_bom_and_single_byte_encoding() && result;
    result = test_namespace_declarations_api() && result;
    result = test_uninitialized_copy_move() && result;
    /* 新 API 与功能覆盖 */
    result = test_line_column_offset() && result;
    result = test_standalone_declaration() && result;
    result = test_processing_instruction_fields() && result;
    result = test_namespace_processing() && result;
    result = test_extra_namespace_declaration() && result;
    result = test_basic_xml_parse() && result;
    result = test_attributes_navigation() && result;
    result = test_stream_value_types() && result;
    result = test_nested_elements() && result;
    result = test_cdata_comment() && result;
    result = test_entity_reference() && result;
    result = test_dtd_declaration() && result;
    result = test_skip_current_element() && result;
    result = test_read_next_start_element() && result;
    result = test_read_element_text() && result;
    result = test_qt_collection_copy_and_mutation() && result;
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
    action = XMenu_addAction(menu, "DTD 声明内容/实体解析器");
    XAction_setAction(action, test_dtd_declaration_values_wrapper);
    action = XMenu_addAction(menu, "QIODevice 输入");
    XAction_setAction(action, test_device_input_wrapper);
    action = XMenu_addAction(menu, "DTD 拷贝/移动");
    XAction_setAction(action, test_dtd_copy_move_wrapper);
    action = XMenu_addAction(menu, "QIODevice 分块输入");
    XAction_setAction(action, test_incremental_device_input_wrapper);
    action = XMenu_addAction(menu, "UTF-8/16/32 编码输入");
    XAction_setAction(action, test_encoded_input_wrapper);
    action = XMenu_addAction(menu, "命名空间声明列表");
    XAction_setAction(action, test_namespace_declarations_api_wrapper);
    action = XMenu_addAction(menu, "未初始化目标拷贝/移动");
    XAction_setAction(action, test_uninitialized_copy_move_wrapper);
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
    action = XMenu_addAction(menu, "XML 流值类型与列表别名");
    XAction_setAction(action, test_stream_value_types_wrapper);
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
    action = XMenu_addAction(menu, "Qt 集合深拷贝和修改");
    XAction_setAction(action, test_qt_collection_copy_and_mutation_wrapper);
    action = XMenu_addAction(menu, "非法 XML");
    XAction_setAction(action, test_invalid_xml_wrapper);
    action = XMenu_addAction(menu, "clear/setDevice 配置保留");
    XAction_setAction(action, test_reset_preserves_configuration_wrapper);
    action = XMenu_addAction(menu, "拆分 BOM 和单字节编码");
    XAction_setAction(action, test_split_bom_and_single_byte_encoding_wrapper);
}
#endif
