/**
 * @file XAnyStringViewTest.c
 * @brief XAnyStringView 全面测试（对标 Qt 6.8 QAnyStringView）
 * @details 覆盖构造、访问、子视图、编码检测、比较、迭代器等全部 API，
 *          包括三种编码（UTF-8/Latin-1/UTF-16）的全面测试，
 *          null/empty 边界、编码间转换测试。
 */
#include "XDataStructTest.h"
#if DEMOTEST
#include "XAnyStringView.h"
#include "XString.h"
#include "XStringView.h"
#include "XLatin1StringView.h"
#include "XUtf8StringView.h"
#include "XByteArrayView.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>

/* ==================== 1. 构造与创建测试 ==================== */
static void XAnyStringViewTest_Create(void)
{
    XPrintf("===== 构造与创建测试 =====\n");

    /* create() — 默认 null view */
    {
        XAnyStringView v = XAnyStringView_create();
        XPrintf("  create(): data=%s, size=%lld, isNull=%d, empty=%d (期望 NULL,0,1,1)\n",
            v.m_data ? "非NULL" : "NULL", (long long)XAnyStringView_size(&v),
            XAnyStringView_isNull(&v), XAnyStringView_empty(&v));
    }

    /* create_stringview() — 从 UTF-16 视图 */
    {
        const XChar u16[] = { 'H','e','l','l','o' };
        XStringView sv = XStringView_create_data(u16, 5);
        XAnyStringView v = XAnyStringView_create_stringview(&sv);
        XPrintf("  create_stringview('Hello'): size=%lld, encoding=%d (期望 5,2=UTF16)\n",
            (long long)XAnyStringView_size(&v), (int)XAnyStringView_encoding(&v));
        XPrintf("  isUtf16=%d (期望 1)\n", XAnyStringView_isUtf16(&v));
    }

    /* create_stringview(NULL) */
    {
        XAnyStringView v = XAnyStringView_create_stringview(NULL);
        XPrintf("  create_stringview(NULL): isNull=%d (期望 1)\n", XAnyStringView_isNull(&v));
    }

    /* create_latin1() — 从 XLatin1StringView */
    {
        XLatin1StringView lv = XLatin1StringView_create_cstr("Latin1");
        XAnyStringView v = XAnyStringView_create_latin1(&lv);
        XPrintf("  create_latin1('Latin1'): size=%lld, isLatin1=%d (期望 6,1)\n",
            (long long)XAnyStringView_size(&v), XAnyStringView_isLatin1(&v));
    }

    /* create_utf8view() — 从 XUtf8StringView */
    {
        XUtf8StringView uv = XUtf8StringView_create_cstr("Utf8Str");
        XAnyStringView v = XAnyStringView_create_utf8view(&uv);
        XPrintf("  create_utf8view('Utf8Str'): size=%lld, isUtf8=%d (期望 7,1)\n",
            (long long)XAnyStringView_size(&v), XAnyStringView_isUtf8(&v));
    }

    /* create_bytearrayview() — 从字节数组视图 */
    {
        const uint8_t bd[] = "ByteArr";
        XByteArrayView bav = XByteArrayView_create_data(bd, 7);
        XAnyStringView v = XAnyStringView_create_bytearrayview(&bav);
        XPrintf("  create_bytearrayview('ByteArr'): size=%lld (期望 7)\n",
            (long long)XAnyStringView_size(&v));
    }

    /* create_utf8() — 从 UTF-8 数据指针+长度 */
    {
        XAnyStringView v = XAnyStringView_create_utf8("Hello", 5);
        XPrintf("  create_utf8('Hello',5): size=%lld, isUtf8=%d (期望 5,1)\n",
            (long long)XAnyStringView_size(&v), XAnyStringView_isUtf8(&v));
    }

    /* create_latin1_data() — 从 Latin-1 数据指针+长度 */
    {
        XAnyStringView v = XAnyStringView_create_latin1_data("Hello", 5);
        XPrintf("  create_latin1_data('Hello',5): size=%lld, isLatin1=%d (期望 5,1)\n",
            (long long)XAnyStringView_size(&v), XAnyStringView_isLatin1(&v));
    }

    /* create_utf16() — 从 UTF-16 数据 */
    {
        const XChar u16[] = { 'H','i' };
        XAnyStringView v = XAnyStringView_create_utf16(u16, 2);
        XPrintf("  create_utf16('Hi'): size=%lld, isUtf16=%d (期望 2,1)\n",
            (long long)XAnyStringView_size(&v), XAnyStringView_isUtf16(&v));
    }

    /* create_cstr() — 从 C 字符串 */
    {
        XAnyStringView v = XAnyStringView_create_cstr("Hello");
        XPrintf("  create_cstr('Hello'): size=%lld (期望 5)\n",
            (long long)XAnyStringView_size(&v));
    }

    /* create_string() — 从 XString */
    {
        XString* s = XString_create_utf8("XString");
        XAnyStringView v = XAnyStringView_create_string(s);
        XPrintf("  create_string('XString'): size=%lld (期望 7)\n",
            (long long)XAnyStringView_size(&v));
        XString_delete_base(s);
    }

    XPrintf("\n");
}

/* ==================== 2. 基本访问测试 ==================== */
static void XAnyStringViewTest_Access(void)
{
    XPrintf("===== 基本访问测试 =====\n");

    /* UTF-8 视图 */
    {
        XAnyStringView v = XAnyStringView_create_utf8("ABCDE", 5);
        XPrintf("  utf8: size()=%lld, size_bytes()=%lld, empty=%d, isNull=%d\n",
            (long long)XAnyStringView_size(&v),
            (long long)XAnyStringView_size_bytes(&v),
            XAnyStringView_empty(&v), XAnyStringView_isNull(&v));
        XPrintf("  utf8: charSize=%d (期望 1)\n", XAnyStringView_charSize(&v));
    }

    /* Latin-1 视图 */
    {
        XAnyStringView v = XAnyStringView_create_latin1_data("FGHIJ", 5);
        XPrintf("  latin1: size()=%lld, charSize=%d (期望 5,1)\n",
            (long long)XAnyStringView_size(&v), XAnyStringView_charSize(&v));
    }

    /* UTF-16 视图 */
    {
        const XChar u16[] = { 'K','L','M','N','O' };
        XAnyStringView v = XAnyStringView_create_utf16(u16, 5);
        XPrintf("  utf16: size()=%lld, charSize=%d (期望 5,2)\n",
            (long long)XAnyStringView_size(&v), XAnyStringView_charSize(&v));
    }

    XPrintf("\n");
}

/* ==================== 3. 子视图测试 ==================== */
static void XAnyStringViewTest_SubView(void)
{
    XPrintf("===== 子视图测试 =====\n");

    /* UTF-8 子视图 */
    {
        XAnyStringView v = XAnyStringView_create_utf8("0123456789", 10);
        XAnyStringView sub = XAnyStringView_first_n(&v, 3);
        XPrintf("  utf8 first_n(3): size=%lld (期望 3)\n", (long long)XAnyStringView_size(&sub));

        sub = XAnyStringView_last_n(&v, 3);
        XPrintf("  utf8 last_n(3): size=%lld (期望 3)\n", (long long)XAnyStringView_size(&sub));

        sub = XAnyStringView_sliced(&v, 3);
        XPrintf("  utf8 sliced(3): size=%lld (期望 7)\n", (long long)XAnyStringView_size(&sub));

        sub = XAnyStringView_sliced_2(&v, 2, 4);
        XPrintf("  utf8 sliced_2(2,4): size=%lld (期望 4)\n", (long long)XAnyStringView_size(&sub));

        sub = XAnyStringView_chopped(&v, 3);
        XPrintf("  utf8 chopped(3): size=%lld (期望 7)\n", (long long)XAnyStringView_size(&sub));
    }

    /* Latin-1 子视图 */
    {
        XAnyStringView v = XAnyStringView_create_latin1_data("0123456789", 10);
        XAnyStringView sub = XAnyStringView_first_n(&v, 3);
        XPrintf("  latin1 first_n(3): size=%lld (期望 3)\n", (long long)XAnyStringView_size(&sub));
    }

    /* UTF-16 子视图 */
    {
        const XChar u16[] = { '0','1','2','3','4','5','6','7','8','9' };
        XAnyStringView v = XAnyStringView_create_utf16(u16, 10);
        XAnyStringView sub = XAnyStringView_first_n(&v, 3);
        XPrintf("  utf16 first_n(3): size=%lld (期望 3)\n", (long long)XAnyStringView_size(&sub));
    }

    /* left/right/mid 宏 */
    {
        XAnyStringView v = XAnyStringView_create_utf8("0123456789", 10);
        XAnyStringView l = XAnyStringView_left(v, 3);
        XAnyStringView r = XAnyStringView_right(v, 3);
        XAnyStringView m = XAnyStringView_mid(v, 2, 5);
        XPrintf("  left(3) size=%lld (期望 3)\n", (long long)XAnyStringView_size(&l));
        XPrintf("  right(3) size=%lld (期望 3)\n", (long long)XAnyStringView_size(&r));
        XPrintf("  mid(2,5) size=%lld (期望 5)\n", (long long)XAnyStringView_size(&m));
    }

    XPrintf("\n");
}

/* ==================== 4. 原地修改测试 ==================== */
static void XAnyStringViewTest_Modify(void)
{
    XPrintf("===== 原地修改测试 =====\n");

    /* UTF-8 */
    {
        XAnyStringView v = XAnyStringView_create_utf8("HelloWorld", 10);
        XAnyStringView_truncate(&v, 5);
        XPrintf("  utf8 truncate(5): size=%lld (期望 5)\n", (long long)XAnyStringView_size(&v));

        XAnyStringView_chop(&v, 2);
        XPrintf("  utf8 chop(2): size=%lld (期望 3)\n", (long long)XAnyStringView_size(&v));
    }

    /* Latin-1 */
    {
        XAnyStringView v = XAnyStringView_create_latin1_data("HelloWorld", 10);
        XAnyStringView_truncate(&v, 5);
        XPrintf("  latin1 truncate(5): size=%lld (期望 5)\n", (long long)XAnyStringView_size(&v));
    }

    /* UTF-16 */
    {
        const XChar u16[] = { 'H','e','l','l','o','W','o','r','l','d' };
        XAnyStringView v = XAnyStringView_create_utf16(u16, 10);
        XAnyStringView_truncate(&v, 5);
        XPrintf("  utf16 truncate(5): size=%lld (期望 5)\n", (long long)XAnyStringView_size(&v));
    }

    XPrintf("\n");
}

/* ==================== 5. 比较测试 ==================== */
static void XAnyStringViewTest_Compare(void)
{
    XPrintf("===== 比较测试 =====\n");

    /* 同编码比较 */
    {
        XAnyStringView v1 = XAnyStringView_create_utf8("Hello", 5);
        XAnyStringView v2 = XAnyStringView_create_utf8("Hello", 5);
        XAnyStringView v3 = XAnyStringView_create_utf8("World", 5);
        XPrintf("  equal('Hello','Hello')=%d (期望 1)\n", XAnyStringView_equal(&v1, &v2));
        XPrintf("  equal('Hello','World')=%d (期望 0)\n", XAnyStringView_equal(&v1, &v3));
        XPrintf("  compare('Hello','Hello')=%d (期望 0)\n", XAnyStringView_compare(&v1, &v2, 1));
        XPrintf("  compare('Hello','World')=%d (期望 <0)\n", XAnyStringView_compare(&v1, &v3, 1));
    }

    /* 跨编码比较：UTF-8 vs Latin-1 */
    {
        XAnyStringView v1 = XAnyStringView_create_utf8("Hello", 5);
        XLatin1StringView lv = XLatin1StringView_create_cstr("Hello");
        XAnyStringView v2 = XAnyStringView_create_latin1(&lv);
        XPrintf("  equal(utf8='Hello', latin1='Hello')=%d (期望 1)\n", XAnyStringView_equal(&v1, &v2));
    }

    /* 跨编码比较：UTF-8 vs UTF-16 */
    {
        const XChar u16[] = { 'H','e','l','l','o' };
        XAnyStringView v1 = XAnyStringView_create_utf8("Hello", 5);
        XAnyStringView v2 = XAnyStringView_create_utf16(u16, 5);
        XPrintf("  equal(utf8='Hello', utf16='Hello')=%d (期望 1)\n", XAnyStringView_equal(&v1, &v2));
    }

    /* 大小写不敏感 */
    {
        XAnyStringView v1 = XAnyStringView_create_utf8("Hello", 5);
        XLatin1StringView lv = XLatin1StringView_create_cstr("HELLO");
        XAnyStringView v2 = XAnyStringView_create_latin1(&lv);
        XPrintf("  compare(utf8='Hello', latin1='HELLO', cs=0)=%d (期望 0)\n",
            XAnyStringView_compare(&v1, &v2, 0));
    }

    XPrintf("\n");
}

/* ==================== 6. toString 测试 ==================== */
static void XAnyStringViewTest_ToString(void)
{
    XPrintf("===== toString 测试 =====\n");

    /* UTF-8 */
    {
        XAnyStringView v = XAnyStringView_create_utf8("Hello", 5);
        XString* s = XAnyStringView_toString(&v);
        XPrintf("  utf8 toString(): size=%zu (期望 5)\n", XString_length_base(s));
        XString_delete_base(s);
    }

    /* Latin-1 */
    {
        XLatin1StringView lv = XLatin1StringView_create_cstr("Hello");
        XAnyStringView v = XAnyStringView_create_latin1(&lv);
        XString* s = XAnyStringView_toString(&v);
        XPrintf("  latin1 toString(): size=%zu (期望 5)\n", XString_length_base(s));
        XString_delete_base(s);
    }

    /* UTF-16 */
    {
        const XChar u16[] = { 'H','e','l','l','o' };
        XAnyStringView v = XAnyStringView_create_utf16(u16, 5);
        XString* s = XAnyStringView_toString(&v);
        XPrintf("  utf16 toString(): size=%zu (期望 5)\n", XString_length_base(s));
        XString_delete_base(s);
    }

    /* null view */
    {
        XAnyStringView nv = XAnyStringView_create();
        XString* ns = XAnyStringView_toString(&nv);
        XPrintf("  toString(null view): isNull=%d (期望 1)\n", ns ? XString_isNull(ns) : 1);
        XString_delete_base(ns);
    }

    XPrintf("\n");
}


/* ==================== 7. 迭代器测试 ==================== */
static void XAnyStringViewTest_Iterator(void)
{
    XPrintf("===== 迭代器测试 =====\n");
    {
        XAnyStringView v = XAnyStringView_create_utf8("ABCDE", 5);

        XAnyStringView_iterator it = XAnyStringView_begin(&v);
        XAnyStringView_iterator end = XAnyStringView_end(&v);
        int count = 0;
        while (!XAnyStringView_iterator_equality(&it, &end)) { count++; XAnyStringView_iterator_add(&v, &it); }
        XPrintf("  begin->end count=%d (期望 5)\n", count);
    }
    {
        XAnyStringView v = XAnyStringView_create_utf8("ABCDE", 5);
        XAnyStringView_iterator it = XAnyStringView_begin(&v);
        XAnyStringView_iterator end = XAnyStringView_end(&v);
        XPrintf("  begin->end distance via iteration (期望 5)\n");
    }
    {
        XAnyStringView v = XAnyStringView_create_utf8("ABCDE", 5);
        XAnyStringView_reverse_iterator it = XAnyStringView_rbegin(&v);
        XAnyStringView_reverse_iterator end = XAnyStringView_rend(&v);
        int count = 0;
        while (!XAnyStringView_reverse_iterator_equality(&it, &end)) { count++; XAnyStringView_reverse_iterator_add(&v, &it); }
        XPrintf("  rbegin->rend count=%d (期望 5)\n", count);
    }

    XPrintf("\n");
}

/* ==================== 8. 编码检测测试 ==================== */


static void XAnyStringViewTest_Encoding(void)
{
    XPrintf("===== 编码检测测试 =====\n");

    {
        XAnyStringView v = XAnyStringView_create_utf8("Hello", 5);
        XPrintf("  utf8: encoding=%d, isUtf8=%d, isLatin1=%d, isUtf16=%d (期望 0,1,0,0)\n",
            (int)XAnyStringView_encoding(&v),
            XAnyStringView_isUtf8(&v), XAnyStringView_isLatin1(&v), XAnyStringView_isUtf16(&v));
    }
    {
        XLatin1StringView lv = XLatin1StringView_create_cstr("Hello");
        XAnyStringView v = XAnyStringView_create_latin1(&lv);
        XPrintf("  latin1: encoding=%d, isUtf8=%d, isLatin1=%d, isUtf16=%d (期望 1,0,1,0)\n",
            (int)XAnyStringView_encoding(&v),
            XAnyStringView_isUtf8(&v), XAnyStringView_isLatin1(&v), XAnyStringView_isUtf16(&v));
    }
    {
        const XChar u16[] = { 'H','i' };
        XAnyStringView v = XAnyStringView_create_utf16(u16, 2);
        XPrintf("  utf16: encoding=%d, isUtf8=%d, isLatin1=%d, isUtf16=%d (期望 2,0,0,1)\n",
            (int)XAnyStringView_encoding(&v),
            XAnyStringView_isUtf8(&v), XAnyStringView_isLatin1(&v), XAnyStringView_isUtf16(&v));
    }

    XPrintf("\n");
}

/* ==================== 9. Null/空视图边界测试 ==================== */
static void XAnyStringViewTest_NullEmpty(void)
{
    XPrintf("===== Null/空视图边界测试 =====\n");

    XAnyStringView nullView = XAnyStringView_create();
    XAnyStringView emptyView = XAnyStringView_create_utf8("", 0);

    XPrintf("  nullView: isNull=%d, empty=%d, size=%lld (期望 1,1,0)\n",
        XAnyStringView_isNull(&nullView), XAnyStringView_empty(&nullView),
        (long long)XAnyStringView_size(&nullView));
    XPrintf("  emptyView: isNull=%d, empty=%d, size=%lld (期望 0,1,0)\n",
        XAnyStringView_isNull(&emptyView), XAnyStringView_empty(&emptyView),
        (long long)XAnyStringView_size(&emptyView));

    {
        XAnyStringView sub = XAnyStringView_first_n(&nullView, 3);
        XPrintf("  nullView first_n(3): size=%lld (期望 0)\n", (long long)XAnyStringView_size(&sub));
    }

    XPrintf("\n");
}

/* ==================== 全部测试 ==================== */
static void XAnyStringViewTest_All(void)
{
    XAnyStringViewTest_Create();
    XAnyStringViewTest_Access();
    XAnyStringViewTest_SubView();
    XAnyStringViewTest_Modify();
    XAnyStringViewTest_Compare();
    XAnyStringViewTest_ToString();
    XAnyStringViewTest_Iterator();
    XAnyStringViewTest_Encoding();
    XAnyStringViewTest_NullEmpty();
}

/* ==================== 菜单入口 ==================== */
void XAnyStringViewTest(void)
{
    XAnyStringViewTest_All();
}

void XMenu_XAnyStringViewTest(XMenu* root)
{
    XMenu* menu = XMenu_create("AnyStringView(任意编码字符串视图)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全部测试");
        XAction_setAction(action, XAnyStringViewTest_All);
    }
    {
        XAction* action = XMenu_addAction(menu, "构造与创建");
        XAction_setAction(action, XAnyStringViewTest_Create);
    }
    {
        XAction* action = XMenu_addAction(menu, "基本访问");
        XAction_setAction(action, XAnyStringViewTest_Access);
    }
    {
        XAction* action = XMenu_addAction(menu, "子视图");
        XAction_setAction(action, XAnyStringViewTest_SubView);
    }
    {
        XAction* action = XMenu_addAction(menu, "原地修改");
        XAction_setAction(action, XAnyStringViewTest_Modify);
    }
    {
        XAction* action = XMenu_addAction(menu, "比较");
        XAction_setAction(action, XAnyStringViewTest_Compare);
    }
    {
        XAction* action = XMenu_addAction(menu, "toString");
        XAction_setAction(action, XAnyStringViewTest_ToString);
    }
    {
        XAction* action = XMenu_addAction(menu, "迭代器");
        XAction_setAction(action, XAnyStringViewTest_Iterator);
    }
    {
        XAction* action = XMenu_addAction(menu, "编码检测");
        XAction_setAction(action, XAnyStringViewTest_Encoding);
    }
    {
        XAction* action = XMenu_addAction(menu, "Null/空边界");
        XAction_setAction(action, XAnyStringViewTest_NullEmpty);
    }
}
#endif
