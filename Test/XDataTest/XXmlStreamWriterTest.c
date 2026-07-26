/******************************************************************************
 * @file       XXmlStreamWriterTest.c
 * @brief      XXmlStreamWriter XML写入器全面测试
 * @author     XinYueC 团队
 * @note       覆盖所有公开API，完善测试用例
 ******************************************************************************/
#include"XXmlStreamWriterTest.h"
#include"XXmlStreamWriter.h"
#include"XXmlStreamReader.h"
#include"XClass.h"
#include"XString.h"
#include"XByteArray.h"
#include"XFile.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XMemory.h"
#include<string.h>
#include<stdlib.h>

/* ==================== 测试辅助宏 ==================== */
#define TEST_PASS(name) XPrintf("[PASS] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[FAIL] %s: %s\n", name, reason)
#define TEST_INFO(fmt, ...) XPrintf("[INFO] " fmt "\n", ##__VA_ARGS__)

/* ==================== 测试函数声明 ==================== */
static bool test_create_delete(void);
static bool test_init_deinit(void);
static bool test_write_start_document(void);
static bool test_write_end_document(void);
static bool test_write_start_end_element(void);
static bool test_write_empty_element(void);
static bool test_write_attribute(void);
static bool test_write_attributes(void);
static bool test_write_characters(void);
static bool test_write_cdata(void);
static bool test_write_comment(void);
static bool test_write_processing_instruction(void);
static bool test_write_entity_reference(void);
static bool test_write_dtd(void);
static bool test_write_namespace(void);
static bool test_write_default_namespace(void);
static bool test_write_text_element(void);
static bool test_auto_formatting(void);
static bool test_to_string_bytearray(void);
static bool test_has_error(void);
static bool test_copy_move(void);
static bool test_complex_document(void);
static bool test_write_current_token(void);
static bool test_null_safety(void);
static bool test_device_output(void);
static bool test_qt_edge_semantics(void);

/* ==================== 包装函数 ==================== */
static void test_create_delete_wrapper(XVariant* d) { (void)d; test_create_delete(); }
static void test_init_deinit_wrapper(XVariant* d) { (void)d; test_init_deinit(); }
static void test_write_start_document_wrapper(XVariant* d) { (void)d; test_write_start_document(); }
static void test_write_end_document_wrapper(XVariant* d) { (void)d; test_write_end_document(); }
static void test_write_start_end_element_wrapper(XVariant* d) { (void)d; test_write_start_end_element(); }
static void test_write_empty_element_wrapper(XVariant* d) { (void)d; test_write_empty_element(); }
static void test_write_attribute_wrapper(XVariant* d) { (void)d; test_write_attribute(); }
static void test_write_attributes_wrapper(XVariant* d) { (void)d; test_write_attributes(); }
static void test_write_characters_wrapper(XVariant* d) { (void)d; test_write_characters(); }
static void test_write_cdata_wrapper(XVariant* d) { (void)d; test_write_cdata(); }
static void test_write_comment_wrapper(XVariant* d) { (void)d; test_write_comment(); }
static void test_write_processing_instruction_wrapper(XVariant* d) { (void)d; test_write_processing_instruction(); }
static void test_write_entity_reference_wrapper(XVariant* d) { (void)d; test_write_entity_reference(); }
static void test_write_dtd_wrapper(XVariant* d) { (void)d; test_write_dtd(); }
static void test_write_namespace_wrapper(XVariant* d) { (void)d; test_write_namespace(); }
static void test_write_default_namespace_wrapper(XVariant* d) { (void)d; test_write_default_namespace(); }
static void test_write_text_element_wrapper(XVariant* d) { (void)d; test_write_text_element(); }
static void test_auto_formatting_wrapper(XVariant* d) { (void)d; test_auto_formatting(); }
static void test_to_string_bytearray_wrapper(XVariant* d) { (void)d; test_to_string_bytearray(); }
static void test_has_error_wrapper(XVariant* d) { (void)d; test_has_error(); }
static void test_copy_move_wrapper(XVariant* d) { (void)d; test_copy_move(); }
static void test_complex_document_wrapper(XVariant* d) { (void)d; test_complex_document(); }
static void test_write_current_token_wrapper(XVariant* d) { (void)d; test_write_current_token(); }
static void test_null_safety_wrapper(XVariant* d) { (void)d; test_null_safety(); }
static void test_device_output_wrapper(XVariant* d) { (void)d; test_device_output(); }
static void test_qt_edge_semantics_wrapper(XVariant* d) { (void)d; test_qt_edge_semantics(); }
/* ==================== 测试1: 创建和删除测试 ==================== */
static bool test_create_delete(void)
{
    TEST_INFO("===== 创建和删除测试 =====");
    bool all_pass = true;

    /* 测试 create */
    XXmlStreamWriter* w = XXmlStreamWriter_create();
    if (w) {
        TEST_PASS("XXmlStreamWriter_create");
    } else {
        TEST_FAIL("XXmlStreamWriter_create", "创建失败，返回NULL");
        all_pass = false;
    }

    /* 测试 delete */
    XXmlStreamWriter_delete_base(w);
    TEST_PASS("XXmlStreamWriter_delete");

    /* 测试 delete 传入 NULL 不会崩溃 */
    XXmlStreamWriter_delete_base(NULL);
    TEST_PASS("XXmlStreamWriter_delete(NULL) 安全");

    return all_pass;
}

/* ==================== 测试2: 初始化和反初始化测试 ==================== */
static bool test_init_deinit(void)
{
    TEST_INFO("===== 初始化和反初始化测试 =====");
    bool all_pass = true;

    /* 测试 init */
    XXmlStreamWriter w;
    memset(&w, 0, sizeof(w));
    XXmlStreamWriter_init(&w);
    if (w.m_buffer) {
        TEST_PASS("XXmlStreamWriter_init 初始化缓冲区");
    } else {
        TEST_FAIL("XXmlStreamWriter_init", "缓冲区为NULL");
        all_pass = false;
    }
    /* 验证初始状态 */
    if (w.m_autoFormatting == false) {
        TEST_PASS("XXmlStreamWriter_init autoFormatting默认为false");
    } else {
        TEST_FAIL("XXmlStreamWriter_init autoFormatting", "默认值不为false");
        all_pass = false;
    }
    if (w.m_hasError == false) {
        TEST_PASS("XXmlStreamWriter_init hasError默认为false");
    } else {
        TEST_FAIL("XXmlStreamWriter_init hasError", "默认值不为false");
        all_pass = false;
    }
    if (w.m_elementStack == 0) {
        TEST_PASS("XXmlStreamWriter_init elementStack默认为0");
    } else {
        TEST_FAIL("XXmlStreamWriter_init elementStack", "默认值不为0");
        all_pass = false;
    }

    /* 测试 deinit */
    XXmlStreamWriter_deinit_base(&w);
    TEST_PASS("XXmlStreamWriter_deinit");

    /* 测试 deinit 传入 NULL 不会崩溃 */
    XXmlStreamWriter_deinit_base(NULL);
    TEST_PASS("XXmlStreamWriter_deinit(NULL) 安全");

    /* 测试 deinit_base */
    XXmlStreamWriter w2;
    memset(&w2, 0, sizeof(w2));
    XXmlStreamWriter_init(&w2);
    XXmlStreamWriter_deinit_base(&w2);
    TEST_PASS("XXmlStreamWriter_deinit_base");

    /* 测试 delete_base */
    XXmlStreamWriter* w3 = XXmlStreamWriter_create();
    XXmlStreamWriter_delete_base(w3);
    TEST_PASS("XXmlStreamWriter_delete_base");

    return all_pass;
}
/* ==================== 测试3: 写文档开始测试 ==================== */
static bool test_write_start_document(void)
{
    TEST_INFO("===== 写文档开始测试 =====");
    bool all_pass = true;

    /* 测试 writeStartDocument - 默认版本 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeStartDocument", "创建失败"); return false; }
        XXmlStreamWriter_writeStartDocument_ex_utf8(w, "1.0");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<?xml")) {
            TEST_PASS("writeStartDocument(1.0)");
        } else {
            TEST_FAIL("writeStartDocument(1.0)", "缺少<?xml>标记");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 内存输出与 Qt 的 QString 输出一致，不自动添加 encoding。 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeStartDocument_ex", "创建失败"); return false; }
        XXmlStreamWriter_writeStartDocument_ex_utf8(w, "1.0");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "version=\"1.0\"") && !strstr(result, "encoding=")) {
            TEST_PASS("writeStartDocument_ex(1.0) 内存输出");
        } else {
            TEST_FAIL("writeStartDocument_ex(1.0)", "内存输出不符合 Qt 语义");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 直接调用默认重载时，内存输出同样不包含 encoding。 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeStartDocument 默认重载", "创建失败"); return false; }
        XXmlStreamWriter_writeStartDocument(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strcmp(result, "<?xml version=\"1.0\"?>") == 0)
            TEST_PASS("writeStartDocument 默认内存输出");
        else { TEST_FAIL("writeStartDocument 默认重载", "输出不符合 Qt 语义"); all_pass = false; }
        XXmlStreamWriter_delete_base(w);
    }

    /* XString 重载必须与 UTF-8 重载保持相同的内存输出语义。 */
    {
        XString version;
        XString_init(&version);
        XString_assign_utf8(&version, "1.1");
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { XString_deinit_base(&version); TEST_FAIL("writeStartDocument XString", "创建失败"); return false; }
        XXmlStreamWriter_writeStartDocument_ex(w, &version);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strcmp(result, "<?xml version=\"1.1\"?>") == 0)
            TEST_PASS("writeStartDocument_ex XString 内存输出");
        else { TEST_FAIL("writeStartDocument_ex XString", "输出不符合 Qt 语义"); all_pass = false; }
        XXmlStreamWriter_delete_base(w);
        XString_deinit_base(&version);
    }

    /* 测试 writeStartDocument_ex_2 - 带编码和独立标志 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeStartDocument_ex_2", "创建失败"); return false; }
        XXmlStreamWriter_writeStartDocument_ex_2_utf8(w, "1.0", true);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "standalone")) {
            TEST_PASS("writeStartDocument_ex_2(1.0, standalone)");
        } else {
            TEST_FAIL("writeStartDocument_ex_2(1.0, standalone)", "缺少standalone属性");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    {
        XString version;
        XString_init(&version);
        XString_assign_utf8(&version, "1.1");
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { XString_deinit_base(&version); TEST_FAIL("writeStartDocument_ex_2 XString", "创建失败"); return false; }
        XXmlStreamWriter_writeStartDocument_ex_2(w, &version, false);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strcmp(result, "<?xml version=\"1.1\" standalone=\"no\"?>") == 0)
            TEST_PASS("writeStartDocument_ex_2 XString 内存输出");
        else { TEST_FAIL("writeStartDocument_ex_2 XString", "输出不符合 Qt 语义"); all_pass = false; }
        XXmlStreamWriter_delete_base(w);
        XString_deinit_base(&version);
    }

    /* 测试 writeStartDocument - 带版本号验证 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeStartDocument version", "创建失败"); return false; }
        XXmlStreamWriter_writeStartDocument_ex_utf8(w, "1.0");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "version=\"1.0\"")) {
            TEST_PASS("writeStartDocument 版本号1.0");
        } else {
            TEST_FAIL("writeStartDocument 版本号", "缺少version属性");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试4: 写文档结束测试 ==================== */
static bool test_write_end_document(void)
{
    TEST_INFO("===== 写文档结束测试 =====");
    bool all_pass = true;

    XXmlStreamWriter* w = XXmlStreamWriter_create();
    if (!w) { TEST_FAIL("writeEndDocument", "创建失败"); return false; }

    XXmlStreamWriter_writeStartDocument_ex_utf8(w, "1.0");
    XXmlStreamWriter_writeStartElement_utf8(w, "root");
    XXmlStreamWriter_writeEndElement(w);
    XXmlStreamWriter_writeEndDocument(w);

    const char* result = XXmlStreamWriter_toString(w);
    if (result) {
        TEST_PASS("writeEndDocument 执行成功");
    } else {
        TEST_FAIL("writeEndDocument", "输出为空");
        all_pass = false;
    }

    XXmlStreamWriter_delete_base(w);

    /* 测试连续调用 writeEndDocument 不会崩溃 */
    {
        XXmlStreamWriter* w2 = XXmlStreamWriter_create();
        if (!w2) { TEST_FAIL("writeEndDocument 连续调用", "创建失败"); return false; }
        XXmlStreamWriter_writeEndDocument(w2);
        XXmlStreamWriter_writeEndDocument(w2);
        TEST_PASS("writeEndDocument 连续调用安全");
        XXmlStreamWriter_delete_base(w2);
    }

    return all_pass;
}
/* ==================== 测试5: 写元素测试 ==================== */
static bool test_write_start_end_element(void)
{
    TEST_INFO("===== 写元素测试 =====");
    bool all_pass = true;

    /* 基本元素 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeStartElement", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "root");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<root/>")) {
            TEST_PASS("writeStartElement/writeEndElement");
        } else {
            TEST_FAIL("writeStartElement/writeEndElement", "未按 Qt 语义写成空元素");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 带命名空间的元素 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeStartElement_ex", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_ex_utf8(w, "http://example.com", "root");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "root")) {
            TEST_PASS("writeStartElement_ex 带命名空间");
        } else {
            TEST_FAIL("writeStartElement_ex", "缺少root元素");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 多层嵌套 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("嵌套元素", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "root");
        XXmlStreamWriter_writeStartElement_utf8(w, "child");
        XXmlStreamWriter_writeEndElement(w);
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<root>") && strstr(result, "<child/>") && strstr(result, "</root>")) {
            TEST_PASS("多层嵌套元素");
        } else {
            TEST_FAIL("多层嵌套元素", "缺少嵌套结构");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 深层嵌套 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("深层嵌套", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "a");
        XXmlStreamWriter_writeStartElement_utf8(w, "b");
        XXmlStreamWriter_writeStartElement_utf8(w, "c");
        XXmlStreamWriter_writeEndElement(w);
        XXmlStreamWriter_writeEndElement(w);
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<a>") && strstr(result, "<b>") && strstr(result, "<c/>") && strstr(result, "</a>")) {
            TEST_PASS("深层嵌套3层");
        } else {
            TEST_FAIL("深层嵌套3层", "缺少嵌套结构");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试6: 空元素测试 ==================== */
static bool test_write_empty_element(void)
{
    TEST_INFO("===== 空元素测试 =====");
    bool all_pass = true;

    /* 普通空元素 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeEmptyElement", "创建失败"); return false; }
        XXmlStreamWriter_writeEmptyElement_utf8(w, "br");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<br/>")) {
            TEST_PASS("writeEmptyElement(br)");
        } else {
            TEST_FAIL("writeEmptyElement(br)", "缺少<br/>标记");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 带命名空间的空元素 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeEmptyElement_ex", "创建失败"); return false; }
        XXmlStreamWriter_writeEmptyElement_ex_utf8(w, "http://ns.com", "item");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "item")) {
            TEST_PASS("writeEmptyElement_ex 带命名空间");
        } else {
            TEST_FAIL("writeEmptyElement_ex", "缺少item元素");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 多个空元素 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("多个空元素", "创建失败"); return false; }
        XXmlStreamWriter_writeEmptyElement_utf8(w, "br");
        XXmlStreamWriter_writeEmptyElement_utf8(w, "hr");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<br/>") && strstr(result, "<hr/>")) {
            TEST_PASS("多个空元素");
        } else {
            TEST_FAIL("多个空元素", "缺少空元素标记");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}
/* ==================== 测试7: 写属性测试 ==================== */
static bool test_write_attribute(void)
{
    TEST_INFO("===== 写属性测试 =====");
    bool all_pass = true;

    /* 普通属性 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeAttribute", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "elem");
        XXmlStreamWriter_writeAttribute_utf8(w, "id", "123");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "id=\"123\"")) {
            TEST_PASS("writeAttribute(id, 123)");
        } else {
            TEST_FAIL("writeAttribute(id, 123)", "缺少id属性");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 带命名空间的属性 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeAttribute_ex", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "elem");
        XXmlStreamWriter_writeAttribute_ex_utf8(w, "http://ns.com", "attr", "val");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "val")) {
            TEST_PASS("writeAttribute_ex 带命名空间");
        } else {
            TEST_FAIL("writeAttribute_ex", "缺少属性值");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 通过XXmlStreamAttribute对象写属性 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeAttribute_attr", "创建失败"); return false; }
        XString* qname = XString_create_utf8("name");
        XString* qval = XString_create_utf8("value");
        XXmlStreamAttribute* attr = XXmlStreamAttribute_create(qname, qval);
        XString_delete_base(qname);
        XString_delete_base(qval);
        if (attr) {
            XXmlStreamWriter_writeStartElement_utf8(w, "elem");
            XXmlStreamWriter_writeAttribute_attr(w, attr);
            XXmlStreamWriter_writeEndElement(w);
            const char* result = XXmlStreamWriter_toString(w);
            if (result && strstr(result, "name=\"value\"")) {
                TEST_PASS("writeAttribute_attr(name=value)");
            } else {
                TEST_FAIL("writeAttribute_attr", "缺少name属性");
                all_pass = false;
            }
            XXmlStreamAttribute_delete(attr);
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 多个属性 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("多个属性", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "elem");
        XXmlStreamWriter_writeAttribute_utf8(w, "id", "1");
        XXmlStreamWriter_writeAttribute_utf8(w, "class", "main");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "id=\"1\"") && strstr(result, "class=\"main\"")) {
            TEST_PASS("多个属性");
        } else {
            TEST_FAIL("多个属性", "缺少属性");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 属性值包含特殊字符(转义测试) */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("属性特殊字符", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "elem");
        XXmlStreamWriter_writeAttribute_utf8(w, "desc", "a & b < c > d");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "&amp;") && strstr(result, "&lt;") && strstr(result, "&gt;")) {
            TEST_PASS("属性值特殊字符转义");
        } else {
            TEST_FAIL("属性值特殊字符转义", "缺少转义序列");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}
/* ==================== 测试8: 写属性列表测试 ==================== */
static bool test_write_attributes(void)
{
    TEST_INFO("===== 写属性列表测试 =====");
    bool all_pass = true;

    XXmlStreamWriter* w = XXmlStreamWriter_create();
    if (!w) { TEST_FAIL("writeAttributes", "创建失败"); return false; }

    XXmlStreamAttributes* attrs = XXmlStreamAttributes_create();
    if (!attrs) {
        TEST_FAIL("writeAttributes", "属性列表创建失败");
        XXmlStreamWriter_delete_base(w);
        return false;
    }

    /* 创建属性并添加到列表 */
    XString* a1_name = XString_create_utf8("id");
    XString* a1_val = XString_create_utf8("42");
    XString* a2_name = XString_create_utf8("type");
    XString* a2_val = XString_create_utf8("test");
    XXmlStreamAttribute* a1 = XXmlStreamAttribute_create(a1_name, a1_val);
    XXmlStreamAttribute* a2 = XXmlStreamAttribute_create(a2_name, a2_val);
    XString_delete_base(a1_name);
    XString_delete_base(a1_val);
    XString_delete_base(a2_name);
    XString_delete_base(a2_val);
    if (a1 && a2) {
        XXmlStreamWriter_writeStartElement_utf8(w, "elem");
        /* 由于XXmlStreamAttributes没有add方法，直接调用writeAttributes */
        XXmlStreamWriter_writeAttributes(w, attrs);
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result) {
            TEST_PASS("writeAttributes 执行成功");
        } else {
            TEST_FAIL("writeAttributes", "输出为空");
            all_pass = false;
        }
    }
    XXmlStreamAttribute_delete(a1);
    XXmlStreamAttribute_delete(a2);
    XXmlStreamAttributes_delete(attrs);
    XXmlStreamWriter_delete_base(w);
    return all_pass;
}

/* ==================== 测试9: 写字符数据测试 ==================== */
static bool test_write_characters(void)
{
    TEST_INFO("===== 写字符数据测试 =====");
    bool all_pass = true;

    /* 普通文本 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeCharacters", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "p");
        XXmlStreamWriter_writeCharacters_utf8(w, "Hello World");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "Hello World")) {
            TEST_PASS("writeCharacters(Hello World)");
        } else {
            TEST_FAIL("writeCharacters(Hello World)", "缺少文本内容");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 空文本 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeCharacters 空", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "p");
        XXmlStreamWriter_writeCharacters_utf8(w, "");
        XXmlStreamWriter_writeEndElement(w);
        TEST_PASS("writeCharacters 空字符串安全");
        XXmlStreamWriter_delete_base(w);
    }

    /* 特殊字符转义 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeCharacters 转义", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "p");
        XXmlStreamWriter_writeCharacters_utf8(w, "a & b < c > d \" e ' f");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "&amp;") && strstr(result, "&lt;") && strstr(result, "&gt;")) {
            TEST_PASS("writeCharacters 特殊字符转义");
        } else {
            TEST_FAIL("writeCharacters 特殊字符转义", "缺少转义序列");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 中文文本 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeCharacters 中文", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "p");
        XXmlStreamWriter_writeCharacters_utf8(w, "你好，世界！");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "你好")) {
            TEST_PASS("writeCharacters 中文文本");
        } else {
            TEST_FAIL("writeCharacters 中文文本", "缺少中文内容");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试10: CDATA测试 ==================== */
static bool test_write_cdata(void)
{
    TEST_INFO("===== CDATA测试 =====");
    bool all_pass = true;

    /* 普通CDATA */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeCDATA", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "script");
        XXmlStreamWriter_writeCDATA_utf8(w, "if (a < b && b > c)");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<![CDATA[") && strstr(result, "if (a < b && b > c)")) {
            TEST_PASS("writeCDATA");
        } else {
            TEST_FAIL("writeCDATA", "缺少CDATA节");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 空CDATA */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeCDATA 空", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "script");
        XXmlStreamWriter_writeCDATA_utf8(w, "");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<![CDATA[]]>")) {
            TEST_PASS("writeCDATA 空字符串");
        } else {
            TEST_FAIL("writeCDATA 空字符串", "缺少空CDATA节");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}
/* ==================== 测试11: 注释测试 ==================== */
static bool test_write_comment(void)
{
    TEST_INFO("===== 注释测试 =====");
    bool all_pass = true;

    /* 普通注释 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeComment", "创建失败"); return false; }
        XXmlStreamWriter_writeComment_utf8(w, "This is a comment");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<!--") && strstr(result, "This is a comment") && strstr(result, "-->")) {
            TEST_PASS("writeComment");
        } else {
            TEST_FAIL("writeComment", "缺少注释标记");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 空注释 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeComment 空", "创建失败"); return false; }
        XXmlStreamWriter_writeComment_utf8(w, "");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<!---->")) {
            TEST_PASS("writeComment 空字符串");
        } else {
            TEST_FAIL("writeComment 空字符串", "缺少空注释");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 中文注释 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeComment 中文", "创建失败"); return false; }
        XXmlStreamWriter_writeComment_utf8(w, "这是一个注释");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "这是一个注释")) {
            TEST_PASS("writeComment 中文注释");
        } else {
            TEST_FAIL("writeComment 中文注释", "缺少中文注释内容");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试12: 处理指令测试 ==================== */
static bool test_write_processing_instruction(void)
{
    TEST_INFO("===== 处理指令测试 =====");
    bool all_pass = true;

    /* 带数据的处理指令 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeProcessingInstruction", "创建失败"); return false; }
        XXmlStreamWriter_writeProcessingInstruction_utf8(w, "xml-stylesheet", "type=\"text/xsl\" href=\"style.xsl\"");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<?xml-stylesheet") && strstr(result, "?>")) {
            TEST_PASS("writeProcessingInstruction");
        } else {
            TEST_FAIL("writeProcessingInstruction", "缺少处理指令");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 无数据的处理指令 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeProcessingInstruction 无数据", "创建失败"); return false; }
        XXmlStreamWriter_writeProcessingInstruction_utf8(w, "target", NULL);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<?target?>")) {
            TEST_PASS("writeProcessingInstruction 无数据");
        } else {
            TEST_FAIL("writeProcessingInstruction 无数据", "缺少处理指令");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试13: 实体引用测试 ==================== */
static bool test_write_entity_reference(void)
{
    TEST_INFO("===== 实体引用测试 =====");
    bool all_pass = true;

    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeEntityReference", "创建失败"); return false; }
        XXmlStreamWriter_writeEntityReference_utf8(w, "amp");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "&amp;")) {
            TEST_PASS("writeEntityReference(amp)");
        } else {
            TEST_FAIL("writeEntityReference(amp)", "缺少&amp;");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 自定义实体 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeEntityReference 自定义", "创建失败"); return false; }
        XXmlStreamWriter_writeEntityReference_utf8(w, "myentity");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "&myentity;")) {
            TEST_PASS("writeEntityReference(myentity)");
        } else {
            TEST_FAIL("writeEntityReference(myentity)", "缺少&myentity;");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试14: DTD测试 ==================== */
static bool test_write_dtd(void)
{
    TEST_INFO("===== DTD测试 =====");
    bool all_pass = true;

    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeDTD", "创建失败"); return false; }
        XXmlStreamWriter_writeDTD_utf8(w, "<!DOCTYPE root PUBLIC \"-//Test//DTD//EN\">");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<!DOCTYPE")) {
            TEST_PASS("writeDTD");
        } else {
            TEST_FAIL("writeDTD", "缺少DTD");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 简单DTD */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeDTD 简单", "创建失败"); return false; }
        XXmlStreamWriter_writeDTD_utf8(w, "<!DOCTYPE root>");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<!DOCTYPE root>")) {
            TEST_PASS("writeDTD 简单");
        } else {
            TEST_FAIL("writeDTD 简单", "缺少简单DTD");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}
/* ==================== 测试15: 命名空间测试 ==================== */
static bool test_write_namespace(void)
{
    TEST_INFO("===== 命名空间测试 =====");
    bool all_pass = true;

    /* 带前缀命名空间 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeNamespace", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "root");
        XXmlStreamWriter_writeNamespace_utf8(w, "http://ns.com", "ns");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result) {
            TEST_PASS("writeNamespace 执行成功");
        } else {
            TEST_FAIL("writeNamespace", "输出为空");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 默认命名空间 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeDefaultNamespace", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "root");
        XXmlStreamWriter_writeDefaultNamespace_utf8(w, "http://default.com");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result) {
            TEST_PASS("writeDefaultNamespace 执行成功");
        } else {
            TEST_FAIL("writeDefaultNamespace", "输出为空");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 元素中使用命名空间 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeStartElement_ex 命名空间", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_ex_utf8(w, "http://ns.com", "item");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "item")) {
            TEST_PASS("writeStartElement_ex 命名空间元素");
        } else {
            TEST_FAIL("writeStartElement_ex 命名空间元素", "缺少item元素");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试16: 默认命名空间测试 ==================== */
static bool test_write_default_namespace(void)
{
    TEST_INFO("===== 默认命名空间测试 =====");
    bool all_pass = true;

    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeDefaultNamespace", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w, "root");
        XXmlStreamWriter_writeDefaultNamespace_utf8(w, "http://default.com");
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result) {
            TEST_PASS("writeDefaultNamespace 执行成功");
        } else {
            TEST_FAIL("writeDefaultNamespace", "输出为空");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试17: 文本元素测试 ==================== */
static bool test_write_text_element(void)
{
    TEST_INFO("===== 文本元素测试 =====");
    bool all_pass = true;

    /* 普通文本元素 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeTextElement", "创建失败"); return false; }
        XXmlStreamWriter_writeTextElement_utf8(w, "title", "Hello");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<title>") && strstr(result, "Hello") && strstr(result, "</title>")) {
            TEST_PASS("writeTextElement(title, Hello)");
        } else {
            TEST_FAIL("writeTextElement(title, Hello)", "缺少<title>Hello</title>");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 带命名空间的文本元素 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeTextElement_ex", "创建失败"); return false; }
        XXmlStreamWriter_writeTextElement_ex_utf8(w, "http://ns.com", "item", "value");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "item") && strstr(result, "value")) {
            TEST_PASS("writeTextElement_ex 带命名空间");
        } else {
            TEST_FAIL("writeTextElement_ex 带命名空间", "缺少item或value");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 空文本元素 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeTextElement 空文本", "创建失败"); return false; }
        XXmlStreamWriter_writeTextElement_utf8(w, "empty", "");
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "<empty>") && strstr(result, "</empty>")) {
            TEST_PASS("writeTextElement 空文本");
        } else {
            TEST_FAIL("writeTextElement 空文本", "缺少<empty></empty>");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}
/* ==================== 测试18: 自动格式化测试 ==================== */
static bool test_auto_formatting(void)
{
    TEST_INFO("===== 自动格式化测试 =====");
    bool all_pass = true;

    /* 设置自动格式化 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("setAutoFormatting", "创建失败"); return false; }
        XXmlStreamWriter_setAutoFormatting(w, true);
        if (XXmlStreamWriter_autoFormatting(w)) {
            TEST_PASS("setAutoFormatting(true) / autoFormatting");
        } else {
            TEST_FAIL("setAutoFormatting(true)", "autoFormatting返回false");
            all_pass = false;
        }

        XXmlStreamWriter_setAutoFormatting(w, false);
        if (!XXmlStreamWriter_autoFormatting(w)) {
            TEST_PASS("setAutoFormatting(false)");
        } else {
            TEST_FAIL("setAutoFormatting(false)", "autoFormatting返回true");
            all_pass = false;
        }

        XXmlStreamWriter_setAutoFormatting(w, true);
        if (XXmlStreamWriter_autoFormatting(w)) {
            TEST_PASS("setAutoFormatting 再次启用");
        } else {
            TEST_FAIL("setAutoFormatting 再次启用", "autoFormatting返回false");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 设置缩进 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("setAutoFormattingIndent", "创建失败"); return false; }
        XXmlStreamWriter_setAutoFormattingIndent(w, 2);
        if (XXmlStreamWriter_autoFormattingIndent(w) == 2) {
            TEST_PASS("setAutoFormattingIndent(2)");
        } else {
            TEST_FAIL("setAutoFormattingIndent(2)", "autoFormattingIndent返回值不为2");
            all_pass = false;
        }

        XXmlStreamWriter_setAutoFormattingIndent(w, 4);
        if (XXmlStreamWriter_autoFormattingIndent(w) == 4) {
            TEST_PASS("setAutoFormattingIndent(4)");
        } else {
            TEST_FAIL("setAutoFormattingIndent(4)", "autoFormattingIndent返回值不为4");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* 格式化输出验证 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("格式化输出", "创建失败"); return false; }
        XXmlStreamWriter_setAutoFormatting(w, true);
        XXmlStreamWriter_setAutoFormattingIndent(w, 4);
        XXmlStreamWriter_writeStartDocument_ex_utf8(w, "1.0");
        XXmlStreamWriter_writeStartElement_utf8(w, "root");
        XXmlStreamWriter_writeStartElement_utf8(w, "child");
        XXmlStreamWriter_writeEndElement(w);
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "\n")) {
            TEST_PASS("格式化输出包含换行");
        } else {
            TEST_FAIL("格式化输出包含换行", "输出中无换行符");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* Qt 以负缩进表示 Tab；绝对值表示每层的 Tab 数。 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("负缩进", "创建失败"); return false; }
        XXmlStreamWriter_setAutoFormatting(w, true);
        XXmlStreamWriter_setAutoFormattingIndent(w, -1);
        XXmlStreamWriter_writeStartElement_utf8(w, "root");
        XXmlStreamWriter_writeStartElement_utf8(w, "child");
        XXmlStreamWriter_writeEndElement(w);
        XXmlStreamWriter_writeEndElement(w);
        const char* result = XXmlStreamWriter_toString(w);
        if (result && strstr(result, "\n\t<child"))
            TEST_PASS("负缩进使用 Tab");
        else { TEST_FAIL("负缩进", "未按 Tab 输出"); all_pass = false; }
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试19: toString和toByteArray测试 ==================== */
static bool test_to_string_bytearray(void)
{
    TEST_INFO("===== toString和toByteArray测试 =====");
    bool all_pass = true;

    XXmlStreamWriter* w = XXmlStreamWriter_create();
    if (!w) { TEST_FAIL("toString/ByteArray", "创建失败"); return false; }

    XXmlStreamWriter_writeStartElement_utf8(w, "root");
    XXmlStreamWriter_writeEndElement(w);

    /* 测试toString */
    const char* str = XXmlStreamWriter_toString(w);
    if (str) {
        TEST_PASS("XXmlStreamWriter_toString");
    } else {
        TEST_FAIL("XXmlStreamWriter_toString", "返回NULL");
        all_pass = false;
    }

    /* 测试toByteArray */
    XByteArray* ba = XXmlStreamWriter_toByteArray(w);
    if (ba) {
        TEST_PASS("XXmlStreamWriter_toByteArray");
    } else {
        TEST_FAIL("XXmlStreamWriter_toByteArray", "返回NULL");
        all_pass = false;
    }

    XXmlStreamWriter_delete_base(w);
    return all_pass;
}

/* ==================== 测试20: 错误处理测试 ==================== */
static bool test_has_error(void)
{
    TEST_INFO("===== 错误处理测试 =====");
    bool all_pass = true;

    /* 新创建的writer应该没有错误 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("hasError", "创建失败"); return false; }
        if (!XXmlStreamWriter_hasError(w)) {
            TEST_PASS("hasError 初始无错误");
        } else {
            TEST_FAIL("hasError 初始无错误", "初始状态有错误");
            all_pass = false;
        }
        XXmlStreamWriter_delete_base(w);
    }

    /* NULL传入hasError */
    {
        if (XXmlStreamWriter_hasError(NULL)) {
            TEST_PASS("hasError(NULL) 返回true");
        } else {
            TEST_FAIL("hasError(NULL)", "传入NULL未返回true");
            all_pass = false;
        }
    }

    return all_pass;
}
/* ==================== 测试21: 拷贝和移动测试 ==================== */
static bool test_copy_move(void)
{
    TEST_INFO("===== 拷贝和移动测试 =====");
    bool all_pass = true;

    /* 拷贝测试 */
    {
        XXmlStreamWriter* w1 = XXmlStreamWriter_create();
        if (!w1) { TEST_FAIL("拷贝测试", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w1, "root");
        XXmlStreamWriter_writeEndElement(w1);

        /* 栈上初始化并拷贝 */
        XXmlStreamWriter w2;
        memset(&w2, 0, sizeof(w2));
        XXmlStreamWriter_init(&w2);

        /* 使用XClass框架的拷贝 */
        XClass* cls1 = (XClass*)w1;
        XClass* cls2 = (XClass*)&w2;
        XClass_copy_base(cls2, cls1);
        TEST_PASS("XClass_copy 拷贝成功");

        XXmlStreamWriter_deinit_base(&w2);
        XXmlStreamWriter_delete_base(w1);
    }

    /* 移动测试 */
    {
        XXmlStreamWriter* w1 = XXmlStreamWriter_create();
        if (!w1) { TEST_FAIL("移动测试", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w1, "root");
        XXmlStreamWriter_writeEndElement(w1);

        XXmlStreamWriter w2;
        memset(&w2, 0, sizeof(w2));
        XXmlStreamWriter_init(&w2);

        XClass* cls1 = (XClass*)w1;
        XClass* cls2 = (XClass*)&w2;
        XClass_move_base(cls2, cls1);
        TEST_PASS("XClass_move 移动成功");

        XXmlStreamWriter_deinit_base(&w2);
        XXmlStreamWriter_delete_base(w1);
    }

    /* 深拷贝验证 */
    {
        XXmlStreamWriter* w1 = XXmlStreamWriter_create();
        if (!w1) { TEST_FAIL("深拷贝验证", "创建失败"); return false; }
        XXmlStreamWriter_writeStartElement_utf8(w1, "root");
        XXmlStreamWriter_writeStartElement_utf8(w1, "child");
        XXmlStreamWriter_writeEndElement(w1);
        XXmlStreamWriter_writeEndElement(w1);

        XXmlStreamWriter w2;
        memset(&w2, 0, sizeof(w2));
        XXmlStreamWriter_init(&w2);

        XClass_copy_base((XClass*)&w2, (XClass*)w1);

        const char* original = XXmlStreamWriter_toString(w1);
        const char* copied = XXmlStreamWriter_toString(&w2);
        if (original && copied && strcmp(original, copied) == 0) {
            TEST_PASS("深拷贝内容一致");
        } else {
            TEST_FAIL("深拷贝内容一致", "拷贝后内容不同");
            all_pass = false;
        }

        /* 修改原对象不应影响拷贝 */
        XXmlStreamWriter_delete_base(w1);
        const char* afterDelete = XXmlStreamWriter_toString(&w2);
        if (afterDelete) {
            TEST_PASS("深拷贝独立于原对象");
        } else {
            TEST_FAIL("深拷贝独立于原对象", "原对象删除后拷贝内容失效");
            all_pass = false;
        }

        XXmlStreamWriter_deinit_base(&w2);
    }

    return all_pass;
}
/* ==================== 测试22: 复杂XML文档测试 ==================== */
static bool test_complex_document(void)
{
    TEST_INFO("===== 复杂XML文档测试 =====");
    bool all_pass = true;

    XXmlStreamWriter* w = XXmlStreamWriter_create();
    if (!w) { TEST_FAIL("复杂文档", "创建失败"); return false; }

    /* 生成一个完整的XHTML文档 */
    XXmlStreamWriter_writeStartDocument_ex_utf8(w, "1.0");
    XXmlStreamWriter_writeComment_utf8(w, "这是一个复杂文档测试");
    XXmlStreamWriter_writeProcessingInstruction_utf8(w, "xml-stylesheet", "type=\"text/xsl\"");
    XXmlStreamWriter_writeDTD_utf8(w, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\">");

    XXmlStreamWriter_writeStartElement_utf8(w, "html");
    XXmlStreamWriter_writeStartElement_utf8(w, "head");
    XXmlStreamWriter_writeStartElement_utf8(w, "title");
    XXmlStreamWriter_writeCharacters_utf8(w, "Test Document");
    XXmlStreamWriter_writeEndElement(w);
    XXmlStreamWriter_writeEndElement(w);

    XXmlStreamWriter_writeStartElement_utf8(w, "body");
    XXmlStreamWriter_writeAttribute_utf8(w, "class", "main");

    XXmlStreamWriter_writeStartElement_utf8(w, "h1");
    XXmlStreamWriter_writeCharacters_utf8(w, "Hello World");
    XXmlStreamWriter_writeEndElement(w);

    XXmlStreamWriter_writeStartElement_utf8(w, "p");
    XXmlStreamWriter_writeAttribute_utf8(w, "id", "p1");
    XXmlStreamWriter_writeCharacters_utf8(w, "This is a paragraph with <special> & characters.");
    XXmlStreamWriter_writeEndElement(w);

    XXmlStreamWriter_writeEmptyElement_utf8(w, "br");

    XXmlStreamWriter_writeStartElement_utf8(w, "script");
    XXmlStreamWriter_writeCDATA_utf8(w, "if (a < b) { alert('test'); }");
    XXmlStreamWriter_writeEndElement(w);

    XXmlStreamWriter_writeEndElement(w); /* body */
    XXmlStreamWriter_writeEndElement(w); /* html */
    XXmlStreamWriter_writeEndDocument(w);

    const char* result = XXmlStreamWriter_toString(w);
    if (result) {
        /* 验证所有关键元素 */
        bool hasXmlDecl = strstr(result, "<?xml") != NULL;
        bool hasHtml = strstr(result, "<html>") != NULL;
        bool hasHead = strstr(result, "<head>") != NULL;
        bool hasTitle = strstr(result, "<title>") != NULL;
        bool hasBody = strstr(result, "<body") != NULL;
        bool hasH1 = strstr(result, "<h1>") != NULL;
        bool hasP = strstr(result, "<p ") != NULL || strstr(result, "<p>") != NULL;
        bool hasBr = strstr(result, "<br/>") != NULL;
        bool hasCDATA = strstr(result, "<![CDATA[") != NULL;
        bool hasComment = strstr(result, "<!--") != NULL;
        bool hasEndHtml = strstr(result, "</html>") != NULL;

        if (hasXmlDecl && hasHtml && hasHead && hasTitle && hasBody && hasH1 && hasP && hasEndHtml) {
            TEST_PASS("复杂文档结构完整");
        } else {
            TEST_FAIL("复杂文档结构完整", "缺少关键元素");
            all_pass = false;
        }

        if (hasBr) {
            TEST_PASS("复杂文档空元素正常");
        } else {
            TEST_FAIL("复杂文档空元素正常", "缺少<br/>");
            all_pass = false;
        }

        if (hasCDATA) {
            TEST_PASS("复杂文档CDATA正常");
        } else {
            TEST_FAIL("复杂文档CDATA正常", "缺少CDATA节");
            all_pass = false;
        }

        if (hasComment) {
            TEST_PASS("复杂文档注释正常");
        } else {
            TEST_FAIL("复杂文档注释正常", "缺少注释");
            all_pass = false;
        }

        /* 验证特殊字符转义 */
        if (strstr(result, "&amp;") && strstr(result, "&lt;") && strstr(result, "&gt;")) {
            TEST_PASS("复杂文档特殊字符转义");
        } else {
            TEST_FAIL("复杂文档特殊字符转义", "缺少转义序列");
            all_pass = false;
        }
    } else {
        TEST_FAIL("复杂文档", "输出为空");
        all_pass = false;
    }

    XXmlStreamWriter_delete_base(w);
    return all_pass;
}
/* ==================== 测试23: writeCurrentToken测试 ==================== */
static bool test_write_current_token(void)
{
    TEST_INFO("===== writeCurrentToken测试 =====");
    bool all_pass = true;

    /* 创建reader读取XML并写入writer */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeCurrentToken", "创建失败"); return false; }

        /* 创建一个简单的XML字符串 */
        const char* xml = "<?xml version=\"1.0\"?><root><child>text</child></root>";

        /* 创建reader */
        XXmlStreamReader* r = XXmlStreamReader_create();
        if (!r) {
            TEST_FAIL("writeCurrentToken", "reader创建失败");
            XXmlStreamWriter_delete_base(w);
            return false;
        }

        /* 添加数据到reader */
        XByteArray* ba = XByteArray_create();
        XByteArray_append_utf8(ba, xml);

        /* 开始读取并写入 */
        XXmlStreamReader_addData(r, ba);
        XXmlStreamReader_readNext(r);

        while (!XXmlStreamReader_atEnd(r)) {
            XXmlStreamWriter_writeCurrentToken(w, r);
            XXmlStreamReader_readNext(r);
        }

        const char* result = XXmlStreamWriter_toString(w);
        if (result) {
            TEST_PASS("writeCurrentToken 执行成功");
        } else {
            TEST_FAIL("writeCurrentToken", "输出为空");
            all_pass = false;
        }

        XByteArray_delete_base(ba);
        XXmlStreamReader_delete_base(r);
        XXmlStreamWriter_delete_base(w);
    }

    /* 测试reader为NULL时安全 */
    {
        XXmlStreamWriter* w = XXmlStreamWriter_create();
        if (!w) { TEST_FAIL("writeCurrentToken NULL", "创建失败"); return false; }
        XXmlStreamWriter_writeCurrentToken(w, NULL);
        TEST_PASS("writeCurrentToken(w, NULL) 安全");
        XXmlStreamWriter_delete_base(w);
    }

    return all_pass;
}

/* ==================== 测试24: NULL安全测试 ==================== */
static bool test_null_safety(void)
{
    TEST_INFO("===== NULL安全测试 =====");
    bool all_pass = true;

    /* 向NULL writer传参的各种函数 */
    XXmlStreamWriter_writeStartDocument_ex_utf8(NULL, "1.0");
    TEST_PASS("writeStartDocument(NULL, ...) 安全");

    XXmlStreamWriter_writeStartDocument_ex_utf8(NULL, "1.0");
    TEST_PASS("writeStartDocument_ex(NULL, ...) 安全");

    XXmlStreamWriter_writeStartDocument_ex_2_utf8(NULL, "1.0", true);
    TEST_PASS("writeStartDocument_ex_2(NULL, ...) 安全");

    XXmlStreamWriter_writeEndDocument(NULL);
    TEST_PASS("writeEndDocument(NULL) 安全");

    XXmlStreamWriter_writeStartElement_utf8(NULL, "root");
    TEST_PASS("writeStartElement(NULL, ...) 安全");

    XXmlStreamWriter_writeStartElement_ex_utf8(NULL, "ns", "root");
    TEST_PASS("writeStartElement_ex(NULL, ...) 安全");

    XXmlStreamWriter_writeEndElement(NULL);
    TEST_PASS("writeEndElement(NULL) 安全");

    XXmlStreamWriter_writeEmptyElement_utf8(NULL, "br");
    TEST_PASS("writeEmptyElement(NULL, ...) 安全");

    XXmlStreamWriter_writeEmptyElement_ex_utf8(NULL, "ns", "br");
    TEST_PASS("writeEmptyElement_ex(NULL, ...) 安全");

    XXmlStreamWriter_writeAttribute_utf8(NULL, "id", "1");
    TEST_PASS("writeAttribute(NULL, ...) 安全");

    XXmlStreamWriter_writeAttribute_ex_utf8(NULL, "ns", "id", "1");
    TEST_PASS("writeAttribute_ex(NULL, ...) 安全");

    XXmlStreamWriter_writeAttribute_attr(NULL, NULL);
    TEST_PASS("writeAttribute_attr(NULL, NULL) 安全");

    XXmlStreamWriter_writeCharacters_utf8(NULL, "text");
    TEST_PASS("writeCharacters(NULL, ...) 安全");

    XXmlStreamWriter_writeCDATA_utf8(NULL, "cdata");
    TEST_PASS("writeCDATA(NULL, ...) 安全");

    XXmlStreamWriter_writeComment_utf8(NULL, "comment");
    TEST_PASS("writeComment(NULL, ...) 安全");

    XXmlStreamWriter_writeProcessingInstruction_utf8(NULL, "target", "data");
    TEST_PASS("writeProcessingInstruction(NULL, ...) 安全");

    XXmlStreamWriter_writeEntityReference_utf8(NULL, "amp");
    TEST_PASS("writeEntityReference(NULL, ...) 安全");

    XXmlStreamWriter_writeDTD_utf8(NULL, "<!DOCTYPE root>");
    TEST_PASS("writeDTD(NULL, ...) 安全");

    XXmlStreamWriter_writeNamespace_utf8(NULL, "uri", "prefix");
    TEST_PASS("writeNamespace(NULL, ...) 安全");

    XXmlStreamWriter_writeDefaultNamespace_utf8(NULL, "uri");
    TEST_PASS("writeDefaultNamespace(NULL, ...) 安全");

    XXmlStreamWriter_writeTextElement_utf8(NULL, "elem", "text");
    TEST_PASS("writeTextElement(NULL, ...) 安全");

    XXmlStreamWriter_writeTextElement_ex_utf8(NULL, "ns", "elem", "text");
    TEST_PASS("writeTextElement_ex(NULL, ...) 安全");

    /* setAutoFormatting */
    XXmlStreamWriter_setAutoFormatting(NULL, true);
    TEST_PASS("setAutoFormatting(NULL, ...) 安全");

    /* setAutoFormattingIndent */
    XXmlStreamWriter_setAutoFormattingIndent(NULL, 4);
    TEST_PASS("setAutoFormattingIndent(NULL, ...) 安全");

    return all_pass;
}

/* ==================== 测试: QIODevice 输出 ==================== */
static bool test_device_output(void)
{
    TEST_INFO("===== QIODevice 输出测试 =====");
    bool all_pass = true;
    XString* path = XString_create_utf8("xmlstream_writer_device_test.xml");
    XFile_remove_static(path);
    XFile* file = XFile_create_2(path);
    if (!path || !file || !XFile_open_2(file,
            XIODevice_WriteOnly | XIODevice_Create | XIODevice_Truncate,
            XFile_ReadOwner | XFile_WriteOwner)) {
        TEST_FAIL("Writer setDevice", "无法打开临时输出文件");
        if (file) XFile_deleteLater(file);
        if (path) { XFile_remove_static(path); XString_delete_base(path); }
        return false;
    }
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    XXmlStreamWriter_setDevice(writer, (XIODevice*)file);
    XXmlStreamWriter_writeStartDocument(writer);
    XXmlStreamWriter_writeStartElement_utf8(writer, "root");
    XXmlStreamWriter_writeCharacters_utf8(writer, "device");
    XXmlStreamWriter_writeEndDocument(writer);
    if (!XXmlStreamWriter_hasError(writer)) TEST_PASS("写入设备无错误");
    else { TEST_FAIL("写入设备", "Writer 报告错误"); all_pass = false; }
    XXmlStreamWriter_delete_base(writer);
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);

    XFile* readFile = XFile_create_2(path);
    XByteArray* bytes = NULL;
    if (readFile && XFile_open_2(readFile, XIODevice_ReadOnly | XIODevice_Existing,
            XFile_ReadOwner | XFile_WriteOwner))
        bytes = XIODevice_readAll_3((XIODevice*)readFile);
    const char* output = bytes ? (const char*)XByteArray_data(bytes) : NULL;
    if (output && strstr(output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>") &&
        strstr(output, "<root>device</root>")) TEST_PASS("设备内容与 Qt 编码语义一致");
    else { TEST_FAIL("设备内容", "未写入预期 XML"); all_pass = false; }
    if (bytes) XByteArray_delete_base(bytes);
    if (readFile) { XIODevice_close_base((XIODevice*)readFile); XFile_deleteLater(readFile); }
    XFile_remove_static(path);
    XString_delete_base(path);
    return all_pass;
}

/* ==================== 测试: Qt 文档边界与设备错误语义 ==================== */
static bool test_qt_edge_semantics(void)
{
    TEST_INFO("===== Qt 文档边界/设备错误测试 =====");
    bool all_pass = true;

    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    XXmlStreamWriter_writeStartElement_utf8(writer, "root");
    XXmlStreamWriter_writeStartElement_utf8(writer, "child");
    XXmlStreamWriter_writeEndDocument(writer);
    const char* output = XXmlStreamWriter_toString(writer);
    size_t outputLength = output ? strlen(output) : 0;
    if (output && strcmp(output, "<root><child/></root>\n") == 0 &&
        outputLength > 0 && output[outputLength - 1] == '\n')
        TEST_PASS("writeEndDocument 自动关闭元素并写入换行");
    else { TEST_FAIL("writeEndDocument 边界语义", "输出不符合 Qt 语义"); all_pass = false; }
    XXmlStreamWriter_delete_base(writer);

    XString* path = XString_create_utf8("xmlstream_writer_readonly_test.xml");
    XFile_remove_static(path);
    XFile* seed = XFile_create_2(path);
    bool seeded = seed && XFile_open_2(seed,
        XIODevice_WriteOnly | XIODevice_Create | XIODevice_Truncate,
        XFile_ReadOwner | XFile_WriteOwner);
    if (seeded) {
        XIODevice_write_3((XIODevice*)seed, "seed");
        XIODevice_close_base((XIODevice*)seed);
    }
    if (seed) XFile_deleteLater(seed);

    XFile* readOnly = XFile_create_2(path);
    bool opened = readOnly && XFile_open_2(readOnly, XIODevice_ReadOnly | XIODevice_Existing,
        XFile_ReadOwner | XFile_WriteOwner);
    writer = XXmlStreamWriter_create();
    XXmlStreamWriter_setDevice(writer, (XIODevice*)readOnly);
    XXmlStreamWriter_writeStartElement_utf8(writer, "root");
    if (seeded && opened && XXmlStreamWriter_hasError(writer))
        TEST_PASS("设备写入失败传播 hasError");
    else { TEST_FAIL("设备写入失败", "未报告 hasError"); all_pass = false; }
    XXmlStreamWriter_delete_base(writer);
    if (readOnly) { XIODevice_close_base((XIODevice*)readOnly); XFile_deleteLater(readOnly); }
    XFile_remove_static(path);
    XString_delete_base(path);
    return all_pass;
}
/* ==================== 全量测试 ==================== */
static void XXmlStreamWriterTest_all(XVariant* data)
{
    (void)data;
    TEST_INFO("========== XXmlStreamWriter 全面测试开始 ==========");
    int pass = 0, fail = 0;

    if (test_create_delete()) pass++; else fail++;
    if (test_init_deinit()) pass++; else fail++;
    if (test_write_start_document()) pass++; else fail++;
    if (test_write_end_document()) pass++; else fail++;
    if (test_write_start_end_element()) pass++; else fail++;
    if (test_write_empty_element()) pass++; else fail++;
    if (test_write_attribute()) pass++; else fail++;
    if (test_write_attributes()) pass++; else fail++;
    if (test_write_characters()) pass++; else fail++;
    if (test_write_cdata()) pass++; else fail++;
    if (test_write_comment()) pass++; else fail++;
    if (test_write_processing_instruction()) pass++; else fail++;
    if (test_write_entity_reference()) pass++; else fail++;
    if (test_write_dtd()) pass++; else fail++;
    if (test_write_namespace()) pass++; else fail++;
    if (test_write_default_namespace()) pass++; else fail++;
    if (test_write_text_element()) pass++; else fail++;
    if (test_auto_formatting()) pass++; else fail++;
    if (test_to_string_bytearray()) pass++; else fail++;
    if (test_has_error()) pass++; else fail++;
    if (test_copy_move()) pass++; else fail++;
    if (test_complex_document()) pass++; else fail++;
    if (test_write_current_token()) pass++; else fail++;
    if (test_null_safety()) pass++; else fail++;
    if (test_device_output()) pass++; else fail++;
    if (test_qt_edge_semantics()) pass++; else fail++;

    TEST_INFO("========== 测试结果: %d 通过, %d 失败 ==========", pass, fail);
}

/* ==================== 菜单注册 ==================== */
void XMenu_XXmlStreamWriterTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XXmlStreamWriterTest");
    XMenu_addMenu(root, menu);
    XAction* action = XMenu_addAction(menu, "全面测试");
    XAction_setAction(action, XXmlStreamWriterTest_all);
    action = XMenu_addAction(menu, "创建和删除");
    XAction_setAction(action, test_create_delete_wrapper);
    action = XMenu_addAction(menu, "初始化和反初始化");
    XAction_setAction(action, test_init_deinit_wrapper);
    action = XMenu_addAction(menu, "写文档开始");
    XAction_setAction(action, test_write_start_document_wrapper);
    action = XMenu_addAction(menu, "写文档结束");
    XAction_setAction(action, test_write_end_document_wrapper);
    action = XMenu_addAction(menu, "写元素");
    XAction_setAction(action, test_write_start_end_element_wrapper);
    action = XMenu_addAction(menu, "空元素");
    XAction_setAction(action, test_write_empty_element_wrapper);
    action = XMenu_addAction(menu, "写属性");
    XAction_setAction(action, test_write_attribute_wrapper);
    action = XMenu_addAction(menu, "写属性列表");
    XAction_setAction(action, test_write_attributes_wrapper);
    action = XMenu_addAction(menu, "写字符数据");
    XAction_setAction(action, test_write_characters_wrapper);
    action = XMenu_addAction(menu, "CDATA");
    XAction_setAction(action, test_write_cdata_wrapper);
    action = XMenu_addAction(menu, "注释");
    XAction_setAction(action, test_write_comment_wrapper);
    action = XMenu_addAction(menu, "处理指令");
    XAction_setAction(action, test_write_processing_instruction_wrapper);
    action = XMenu_addAction(menu, "实体引用");
    XAction_setAction(action, test_write_entity_reference_wrapper);
    action = XMenu_addAction(menu, "DTD");
    XAction_setAction(action, test_write_dtd_wrapper);
    action = XMenu_addAction(menu, "命名空间");
    XAction_setAction(action, test_write_namespace_wrapper);
    action = XMenu_addAction(menu, "默认命名空间");
    XAction_setAction(action, test_write_default_namespace_wrapper);
    action = XMenu_addAction(menu, "文本元素");
    XAction_setAction(action, test_write_text_element_wrapper);
    action = XMenu_addAction(menu, "自动格式化");
    XAction_setAction(action, test_auto_formatting_wrapper);
    action = XMenu_addAction(menu, "toString/ByteArray");
    XAction_setAction(action, test_to_string_bytearray_wrapper);
    action = XMenu_addAction(menu, "错误处理");
    XAction_setAction(action, test_has_error_wrapper);
    action = XMenu_addAction(menu, "拷贝和移动");
    XAction_setAction(action, test_copy_move_wrapper);
    action = XMenu_addAction(menu, "复杂文档");
    XAction_setAction(action, test_complex_document_wrapper);
    action = XMenu_addAction(menu, "writeCurrentToken");
    XAction_setAction(action, test_write_current_token_wrapper);
    action = XMenu_addAction(menu, "NULL安全");
    XAction_setAction(action, test_null_safety_wrapper);
    action = XMenu_addAction(menu, "QIODevice 输出");
    XAction_setAction(action, test_device_output_wrapper);
    action = XMenu_addAction(menu, "Qt 文档边界/设备错误");
    XAction_setAction(action, test_qt_edge_semantics_wrapper);
}
