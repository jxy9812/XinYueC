/**
 * @file XUtf8StringViewTest.c
 * @brief XUtf8StringView 全面测试（对标 Qt 6.8 QUtf8StringView）
 * @details 覆盖构造、访问、子视图、查找、比较、数值转换、迭代器等全部 API，
 *          包括 null/empty 边界、UTF-8 有效性检测、中文多字节字符测试。
 */
#include "XDataStructTest.h"
#if DEMOTEST
#include "XUtf8StringView.h"
#include "XString.h"
#include "XStringView.h"
#include "XByteArrayView.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>

/* ==================== 1. 构造与创建测试 ==================== */
static void XUtf8StringViewTest_Create(void)
{
    XPrintf("===== 构造与创建测试 =====\n");

    /* create() */
    {
        XUtf8StringView v = XUtf8StringView_create();
        XPrintf("  create(): data=%s, size=%lld, isNull=%d, empty=%d (期望 NULL,0,1,1)\n",
            v.m_data ? "非NULL" : "NULL", (long long)v.m_size,
            XUtf8StringView_isNull(&v), XUtf8StringView_empty(&v));
    }

    /* create_cstr() */
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("Hello");
        XPrintf("  create_cstr('Hello'): size=%lld, data=%s (期望 5,Hello)\n",
            (long long)XUtf8StringView_size(&v), XUtf8StringView_data(&v));
    }

    /* create_cstr(NULL) */
    {
        XUtf8StringView v = XUtf8StringView_create_cstr(NULL);
        XPrintf("  create_cstr(NULL): isNull=%d (期望 1)\n", XUtf8StringView_isNull(&v));
    }

    /* create_cstr(中文) */
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("你好世界");
        XPrintf("  create_cstr('你好世界'): size=%lld (期望 12, 每个中文字3字节)\n",
            (long long)XUtf8StringView_size(&v));
    }

    /* create_data() */
    {
        XUtf8StringView v = XUtf8StringView_create_data("Hello", 5);
        XPrintf("  create_data('Hello',5): size=%lld, at(0)=%c, at(4)=%c (期望 5,H,o)\n",
            (long long)XUtf8StringView_size(&v),
            (char)XUtf8StringView_at(&v, 0), (char)XUtf8StringView_at(&v, 4));
    }

    /* create_data(NULL, 0) */
    {
        XUtf8StringView v = XUtf8StringView_create_data(NULL, 0);
        XPrintf("  create_data(NULL,0): isNull=%d (期望 1)\n", XUtf8StringView_isNull(&v));
    }

    /* create_range() */
    {
        const char* data = "World";
        XUtf8StringView v = XUtf8StringView_create_range(data, data + 5);
        XPrintf("  create_range('World'): size=%lld, front=%c, back=%c (期望 5,W,d)\n",
            (long long)XUtf8StringView_size(&v),
            (char)XUtf8StringView_front(&v), (char)XUtf8StringView_back(&v));
    }

    /* create_bytearrayview() */
    {
        const uint8_t bdata[] = "ByteView";
        XByteArrayView bav = XByteArrayView_create_data(bdata, 8);
        XUtf8StringView v = XUtf8StringView_create_bytearrayview(&bav);
        XPrintf("  create_bytearrayview('ByteView'): size=%lld (期望 8)\n",
            (long long)XUtf8StringView_size(&v));
    }

    /* create_bytearrayview(NULL) */
    {
        XUtf8StringView v = XUtf8StringView_create_bytearrayview(NULL);
        XPrintf("  create_bytearrayview(NULL): isNull=%d (期望 1)\n", XUtf8StringView_isNull(&v));
    }

    XPrintf("\n");
}

/* ==================== 2. 基本访问测试 ==================== */
static void XUtf8StringViewTest_Access(void)
{
    XPrintf("===== 基本访问测试 =====\n");
    XUtf8StringView v = XUtf8StringView_create_cstr("ABCDEFGHIJ");

    XPrintf("  utf8()=%s\n", XUtf8StringView_utf8(&v));
    XPrintf("  data()=%s\n", XUtf8StringView_data(&v));
    XPrintf("  constData()=%s\n", XUtf8StringView_constData(&v));
    XPrintf("  size()=%lld\n", (long long)XUtf8StringView_size(&v));
    XPrintf("  length()=%lld\n", (long long)XUtf8StringView_length(&v));
    XPrintf("  empty()=%d (期望 0)\n", XUtf8StringView_empty(&v));
    XPrintf("  isNull()=%d (期望 0)\n", XUtf8StringView_isNull(&v));
    XPrintf("  at(0)=%c, at(5)=%c, at(9)=%c (期望 A,F,J)\n",
        (char)XUtf8StringView_at(&v, 0), (char)XUtf8StringView_at(&v, 5), (char)XUtf8StringView_at(&v, 9));
    XPrintf("  front()=%c (期望 A)\n", (char)XUtf8StringView_front(&v));
    XPrintf("  back()=%c (期望 J)\n", (char)XUtf8StringView_back(&v));

    /* 越界 */
    XPrintf("  at(-1)=%d (期望 0)\n", XUtf8StringView_at(&v, -1));
    XPrintf("  at(100)=%d (期望 0)\n", XUtf8StringView_at(&v, 100));

    XPrintf("\n");
}

/* ==================== 3. 子视图测试 ==================== */
static void XUtf8StringViewTest_SubView(void)
{
    XPrintf("===== 子视图测试 =====\n");
    XUtf8StringView v = XUtf8StringView_create_cstr("0123456789");

    {
        XUtf8StringView sub = XUtf8StringView_first_n(&v, 3);
        XPrintf("  first_n(3): size=%lld (期望 3)\n", (long long)XUtf8StringView_size(&sub));
    }
    {
        XUtf8StringView sub = XUtf8StringView_first_n(&v, 0);
        XPrintf("  first_n(0): size=%lld (期望 0)\n", (long long)XUtf8StringView_size(&sub));
    }
    {
        XUtf8StringView sub = XUtf8StringView_first_n(&v, 100);
        XPrintf("  first_n(100): size=%lld (期望 10)\n", (long long)XUtf8StringView_size(&sub));
    }
    {
        XUtf8StringView sub = XUtf8StringView_last_n(&v, 3);
        XPrintf("  last_n(3): size=%lld (期望 3)\n", (long long)XUtf8StringView_size(&sub));
    }
    {
        XUtf8StringView sub = XUtf8StringView_sliced(&v, 3);
        XPrintf("  sliced(3): size=%lld (期望 7)\n", (long long)XUtf8StringView_size(&sub));
    }
    {
        XUtf8StringView sub = XUtf8StringView_sliced_2(&v, 2, 4);
        XPrintf("  sliced_2(2,4): size=%lld (期望 4)\n", (long long)XUtf8StringView_size(&sub));
    }
    {
        XUtf8StringView sub = XUtf8StringView_chopped(&v, 3);
        XPrintf("  chopped(3): size=%lld (期望 7)\n", (long long)XUtf8StringView_size(&sub));
    }

    /* left/right/mid 宏 */
    {
        XUtf8StringView l = XUtf8StringView_left(v, 3);
        XUtf8StringView r = XUtf8StringView_right(v, 3);
        XUtf8StringView m = XUtf8StringView_mid(v, 2, 5);
        XPrintf("  left(3) size=%lld (期望 3)\n", (long long)XUtf8StringView_size(&l));
        XPrintf("  right(3) size=%lld (期望 3)\n", (long long)XUtf8StringView_size(&r));
        XPrintf("  mid(2,5) size=%lld (期望 5)\n", (long long)XUtf8StringView_size(&m));
    }

    XPrintf("\n");
}

/* ==================== 4. 原地修改测试 ==================== */
static void XUtf8StringViewTest_Modify(void)
{
    XPrintf("===== 原地修改测试 =====\n");
    XUtf8StringView v = XUtf8StringView_create_cstr("HelloWorld");

    XUtf8StringView_truncate(&v, 5);
    XPrintf("  truncate(5): size=%lld (期望 5)\n", (long long)XUtf8StringView_size(&v));

    XUtf8StringView_chop(&v, 2);
    XPrintf("  chop(2): size=%lld (期望 3)\n", (long long)XUtf8StringView_size(&v));

    XUtf8StringView_truncate(&v, 0);
    XPrintf("  truncate(0): size=%lld, empty=%d (期望 0,1)\n",
        (long long)XUtf8StringView_size(&v), XUtf8StringView_empty(&v));

    v = XUtf8StringView_create_cstr("HelloWorld");
    XUtf8StringView_chop(&v, 0);
    XPrintf("  chop(0): size=%lld (期望 10)\n", (long long)XUtf8StringView_size(&v));

    XPrintf("\n");
}

/* ==================== 5. 查找测试 ==================== */
static void XUtf8StringViewTest_Find(void)
{
    XPrintf("===== 查找测试 =====\n");
    XUtf8StringView v = XUtf8StringView_create_cstr("abcXYZabc123");

    XPrintf("  indexOf('a',0)=%lld (期望 0)\n", (long long)XUtf8StringView_indexOf_char(&v, 'a', 0, 1));
    XPrintf("  indexOf('a',1)=%lld (期望 6)\n", (long long)XUtf8StringView_indexOf_char(&v, 'a', 1, 1));
    XPrintf("  indexOf('?',0)=%lld (期望 -1)\n", (long long)XUtf8StringView_indexOf_char(&v, '?', 0, 1));

    XPrintf("  lastIndexOf('a',-1)=%lld (期望 6)\n", (long long)XUtf8StringView_lastIndexOf_char(&v, 'a', -1, 1));
    XPrintf("  lastIndexOf('c',-1)=%lld (期望 8)\n", (long long)XUtf8StringView_lastIndexOf_char(&v, 'c', -1, 1));

    XPrintf("  contains('X')=%d (期望 1)\n", XUtf8StringView_contains_char(&v, 'X', 1));
    XPrintf("  contains('?')=%d (期望 0)\n", XUtf8StringView_contains_char(&v, '?', 1));

    XPrintf("  count('a')=%lld (期望 2)\n", (long long)XUtf8StringView_count_char(&v, 'a', 1));
    XPrintf("  count('?')=%lld (期望 0)\n", (long long)XUtf8StringView_count_char(&v, '?', 1));

    XPrintf("  startsWith('a')=%d (期望 1)\n", XUtf8StringView_startsWith_char(&v, 'a', 1));
    XPrintf("  endsWith('3')=%d (期望 1)\n", XUtf8StringView_endsWith_char(&v, '3', 1));

    {
        XUtf8StringView pref = XUtf8StringView_create_cstr("abc");
        XPrintf("  startsWith('abc')=%d (期望 1)\n", XUtf8StringView_startsWith(&v, &pref, 1));
    }
    {
        XUtf8StringView suff = XUtf8StringView_create_cstr("123");
        XPrintf("  endsWith('123')=%d (期望 1)\n", XUtf8StringView_endsWith(&v, &suff, 1));
    }

    XPrintf("\n");
}

/* ==================== 6. 比较测试 ==================== */
static void XUtf8StringViewTest_Compare(void)
{
    XPrintf("===== 比较测试 =====\n");
    XUtf8StringView v1 = XUtf8StringView_create_cstr("Hello");
    XUtf8StringView v2 = XUtf8StringView_create_cstr("Hello");
    XUtf8StringView v3 = XUtf8StringView_create_cstr("HELLO");
    XUtf8StringView v4 = XUtf8StringView_create_cstr("World");

    XPrintf("  equal('Hello','Hello',1)=%d (期望 1)\n", XUtf8StringView_equal(&v1, &v2, 1));
    XPrintf("  equal('Hello','World',1)=%d (期望 0)\n", XUtf8StringView_equal(&v1, &v4, 1));
    XPrintf("  equal('Hello','HELLO',0)=%d (期望 1)\n", XUtf8StringView_equal(&v1, &v3, 0));
    XPrintf("  equal('Hello','HELLO',1)=%d (期望 0)\n", XUtf8StringView_equal(&v1, &v3, 1));

    XPrintf("  compare('Hello','Hello')=%d (期望 0)\n", XUtf8StringView_compare(&v1, &v2, 1));
    XPrintf("  compare('Hello','HELLO',0)=%d (期望 0)\n", XUtf8StringView_compare(&v1, &v3, 0));
    XPrintf("  compare('Hello','World')=%d (期望 <0)\n", XUtf8StringView_compare(&v1, &v4, 1));

    XPrintf("\n");
}

/* ==================== 7. 修剪测试 ==================== */
static void XUtf8StringViewTest_Trim(void)
{
    XPrintf("===== 修剪测试 =====\n");
    XUtf8StringView v1 = XUtf8StringView_create_cstr("  Hello World  ");
    XUtf8StringView v2 = XUtf8StringView_create_cstr("\t\n  \r");
    XUtf8StringView v3 = XUtf8StringView_create_cstr("NoTrim");

    XUtf8StringView t1 = XUtf8StringView_trimmed(&v1);
    XPrintf("  trimmed('  Hello World  '): size=%lld (期望 11)\n", (long long)XUtf8StringView_size(&t1));

    XUtf8StringView t2 = XUtf8StringView_trimmed(&v2);
    XPrintf("  trimmed(whitespace only): size=%lld (期望 0)\n", (long long)XUtf8StringView_size(&t2));

    XUtf8StringView t3 = XUtf8StringView_trimmed(&v3);
    XPrintf("  trimmed('NoTrim'): size=%lld (期望 6)\n", (long long)XUtf8StringView_size(&t3));

    XPrintf("\n");
}

/* ==================== 8. 数值转换测试 ==================== */
static void XUtf8StringViewTest_NumConvert(void)
{
    XPrintf("===== 数值转换测试 =====\n");
    bool ok;

    {
        XUtf8StringView v = XUtf8StringView_create_cstr("12345");
        int val = XUtf8StringView_toInt(&v, &ok, 10);
        XPrintf("  toInt('12345')=%d, ok=%d (期望 12345,1)\n", val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("-6789");
        int val = XUtf8StringView_toInt(&v, &ok, 10);
        XPrintf("  toInt('-6789')=%d, ok=%d (期望 -6789,1)\n", val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("0xFF");
        int val = XUtf8StringView_toInt(&v, &ok, 16);
        XPrintf("  toInt('0xFF',16)=%d, ok=%d (期望 255,1)\n", val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("abc");
        int val = XUtf8StringView_toInt(&v, &ok, 10);
        XPrintf("  toInt('abc')=%d, ok=%d (期望 0,0)\n", val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("3.14159");
        double val = XUtf8StringView_toDouble(&v, &ok);
        XPrintf("  toDouble('3.14159')=%f, ok=%d (期望 ~3.14159,1)\n", val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("2.718");
        float val = XUtf8StringView_toFloat(&v, &ok);
        XPrintf("  toFloat('2.718')=%f, ok=%d (期望 ~2.718,1)\n", val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("32767");
        short val = XUtf8StringView_toShort(&v, &ok, 10);
        XPrintf("  toShort('32767')=%d, ok=%d (期望 32767,1)\n", val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("65535");
        unsigned short val = XUtf8StringView_toUShort(&v, &ok, 10);
        XPrintf("  toUShort('65535')=%u, ok=%d (期望 65535,1)\n", val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("9223372036854775807");
        int64_t val = XUtf8StringView_toLongLong(&v, &ok, 10);
        XPrintf("  toLongLong(max)=%lld, ok=%d (期望 9223372036854775807,1)\n", (long long)val, ok);
    }
    {
        XUtf8StringView v = XUtf8StringView_create();
        int val = XUtf8StringView_toInt(&v, &ok, 10);
        XPrintf("  toInt(null view)=%d, ok=%d (期望 0,0)\n", val, ok);
    }

    XPrintf("\n");
}

/* ==================== 9. UTF-8 有效性检测测试 ==================== */
static void XUtf8StringViewTest_ValidUtf8(void)
{
    XPrintf("===== UTF-8 有效性检测测试 =====\n");

    {
        XUtf8StringView v = XUtf8StringView_create_cstr("Hello");
        XPrintf("  isValidUtf8('Hello')=%d (期望 1)\n", XUtf8StringView_isValidUtf8(&v));
    }
    {
        XUtf8StringView v = XUtf8StringView_create_cstr("你好世界");
        XPrintf("  isValidUtf8('你好世界')=%d (期望 1)\n", XUtf8StringView_isValidUtf8(&v));
    }
    {
        XUtf8StringView v = XUtf8StringView_create();
        XPrintf("  isValidUtf8(null)=%d (期望 1)\n", XUtf8StringView_isValidUtf8(&v));
    }

    XPrintf("\n");
}

/* ==================== 10. toString 测试 ==================== */
static void XUtf8StringViewTest_ToString(void)
{
    XPrintf("===== toString 测试 =====\n");
    XUtf8StringView v = XUtf8StringView_create_cstr("Hello");
    XString* s = XUtf8StringView_toString(&v);
    XPrintf("  toString(): size=%zu (期望 5)\n", XString_length_base(s));
    XString_delete_base(s);

    {
        XUtf8StringView nv = XUtf8StringView_create();
        XString* ns = XUtf8StringView_toString(&nv);
        XPrintf("  toString(null view): isNull=%d (期望 1)\n", ns ? XString_isNull(ns) : 1);
        XString_delete_base(ns);
    }

    XPrintf("\n");
}

/* ==================== 11. 迭代器测试 ==================== */
static void XUtf8StringViewTest_Iterator(void)
{
    XPrintf("===== 迭代器测试 =====\n");
    XUtf8StringView v = XUtf8StringView_create_cstr("ABCDE");

    {
        const char* it = XUtf8StringView_begin(v);
        const char* end = XUtf8StringView_end(v);
        int count = 0;
        while (it != end) { count++; it++; }
        XPrintf("  begin->end count=%d (期望 5)\n", count);
    }
    {
        const char* it = XUtf8StringView_cbegin(v);
        const char* end = XUtf8StringView_cend(v);
        XPrintf("  cbegin->cend distance=%lld (期望 5)\n", (long long)(end - it));
    }
    {
        const char* it = XUtf8StringView_rbegin(v);
        const char* end = XUtf8StringView_rend(v);
        int count = 0;
        while (it != end) { count++; it--; }
        XPrintf("  rbegin->rend count=%d (期望 5)\n", count);
    }

    XPrintf("\n");
}

/* ==================== 12. Null/空视图边界测试 ==================== */
static void XUtf8StringViewTest_NullEmpty(void)
{
    XPrintf("===== Null/空视图边界测试 =====\n");

    XUtf8StringView nullView = XUtf8StringView_create();
    XUtf8StringView emptyView = XUtf8StringView_create_data("", 0);

    XPrintf("  nullView: isNull=%d, empty=%d, size=%lld (期望 1,1,0)\n",
        XUtf8StringView_isNull(&nullView), XUtf8StringView_empty(&nullView),
        (long long)XUtf8StringView_size(&nullView));
    XPrintf("  emptyView: isNull=%d, empty=%d, size=%lld (期望 0,1,0)\n",
        XUtf8StringView_isNull(&emptyView), XUtf8StringView_empty(&emptyView),
        (long long)XUtf8StringView_size(&emptyView));

    XPrintf("  nullView front()=%d (期望 0)\n", XUtf8StringView_front(&nullView));
    XPrintf("  nullView back()=%d (期望 0)\n", XUtf8StringView_back(&nullView));

    {
        XUtf8StringView sub = XUtf8StringView_first_n(&nullView, 3);
        XPrintf("  nullView first_n(3): size=%lld (期望 0)\n", (long long)XUtf8StringView_size(&sub));
    }

    XPrintf("\n");
}

/* ==================== 全部测试 ==================== */
static void XUtf8StringViewTest_All(void)
{
    XUtf8StringViewTest_Create();
    XUtf8StringViewTest_Access();
    XUtf8StringViewTest_SubView();
    XUtf8StringViewTest_Modify();
    XUtf8StringViewTest_Find();
    XUtf8StringViewTest_Compare();
    XUtf8StringViewTest_Trim();
    XUtf8StringViewTest_NumConvert();
    XUtf8StringViewTest_ValidUtf8();
    XUtf8StringViewTest_ToString();
    XUtf8StringViewTest_Iterator();
    XUtf8StringViewTest_NullEmpty();
}

/* ==================== 菜单入口 ==================== */
void XUtf8StringViewTest(void)
{
    XUtf8StringViewTest_All();
}

void XMenu_XUtf8StringViewTest(XMenu* root)
{
    XMenu* menu = XMenu_create("Utf8StringView(UTF-8字符串视图)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全部测试");
        XAction_setAction(action, XUtf8StringViewTest_All);
    }
    {
        XAction* action = XMenu_addAction(menu, "构造与创建");
        XAction_setAction(action, XUtf8StringViewTest_Create);
    }
    {
        XAction* action = XMenu_addAction(menu, "基本访问");
        XAction_setAction(action, XUtf8StringViewTest_Access);
    }
    {
        XAction* action = XMenu_addAction(menu, "子视图");
        XAction_setAction(action, XUtf8StringViewTest_SubView);
    }
    {
        XAction* action = XMenu_addAction(menu, "原地修改");
        XAction_setAction(action, XUtf8StringViewTest_Modify);
    }
    {
        XAction* action = XMenu_addAction(menu, "查找");
        XAction_setAction(action, XUtf8StringViewTest_Find);
    }
    {
        XAction* action = XMenu_addAction(menu, "比较");
        XAction_setAction(action, XUtf8StringViewTest_Compare);
    }
    {
        XAction* action = XMenu_addAction(menu, "修剪");
        XAction_setAction(action, XUtf8StringViewTest_Trim);
    }
    {
        XAction* action = XMenu_addAction(menu, "数值转换");
        XAction_setAction(action, XUtf8StringViewTest_NumConvert);
    }
    {
        XAction* action = XMenu_addAction(menu, "UTF-8检测");
        XAction_setAction(action, XUtf8StringViewTest_ValidUtf8);
    }
    {
        XAction* action = XMenu_addAction(menu, "toString");
        XAction_setAction(action, XUtf8StringViewTest_ToString);
    }
    {
        XAction* action = XMenu_addAction(menu, "迭代器");
        XAction_setAction(action, XUtf8StringViewTest_Iterator);
    }
    {
        XAction* action = XMenu_addAction(menu, "Null/空边界");
        XAction_setAction(action, XUtf8StringViewTest_NullEmpty);
    }
}
#endif
