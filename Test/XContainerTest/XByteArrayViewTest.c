/**
 * @file XByteArrayViewTest.c
 * @brief XByteArrayView 全面测试（对标 Qt 6.8 QByteArrayView）
 * @details 覆盖构造、访问、子视图、查找、比较、数值转换、迭代器等全部 API，
 *          包括 null/empty 边界、内存安全测试。
 */
#include "XDataStructTest.h"
#if DEMOTEST
#include "XByteArrayView.h"
#include "XByteArray.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>

/* ==================== 1. 构造与创建测试 ==================== */
static void XByteArrayViewTest_Create(void)
{
    XPrintf("===== 构造与创建测试 =====\n");

    /* create() */
    {
        XByteArrayView v = XByteArrayView_create();
        XPrintf("  create(): data=%s, size=%lld, isNull=%d, empty=%d (期望 NULL,0,1,1)\n",
            v.m_data ? "非NULL" : "NULL", (long long)v.m_size,
            XByteArrayView_isNull(&v), XByteArrayView_empty(&v));
    }

    /* create_data() */
    {
        const uint8_t data[] = "Hello";
        XByteArrayView v = XByteArrayView_create_data(data, 5);
        XPrintf("  create_data('Hello',5): size=%lld, at(0)=%c, at(4)=%c (期望 5,H,o)\n",
            (long long)XByteArrayView_size(&v),
            (char)XByteArrayView_at(&v, 0), (char)XByteArrayView_at(&v, 4));
    }

    /* create_data(NULL, 0) */
    {
        XByteArrayView v = XByteArrayView_create_data(NULL, 0);
        XPrintf("  create_data(NULL,0): isNull=%d (期望 1)\n", XByteArrayView_isNull(&v));
    }

    /* create_range() */
    {
        const uint8_t data[] = "World";
        XByteArrayView v = XByteArrayView_create_range(data, data + 5);
        XPrintf("  create_range('World'): size=%lld, front=%c, back=%c (期望 5,W,d)\n",
            (long long)XByteArrayView_size(&v),
            (char)XByteArrayView_front(&v), (char)XByteArrayView_back(&v));
    }

    /* create_range(NULL, NULL) */
    {
        XByteArrayView v = XByteArrayView_create_range(NULL, NULL);
        XPrintf("  create_range(NULL,NULL): isNull=%d (期望 1)\n", XByteArrayView_isNull(&v));
    }

    /* create_cstr() */
    {
        XByteArrayView v = XByteArrayView_create_cstr("TestString");
        XPrintf("  create_cstr('TestString'): size=%lld (期望 10)\n",
            (long long)XByteArrayView_size(&v));
    }

    /* create_cstr(NULL) */
    {
        XByteArrayView v = XByteArrayView_create_cstr(NULL);
        XPrintf("  create_cstr(NULL): isNull=%d (期望 1)\n", XByteArrayView_isNull(&v));
    }

    /* create_bytearray() */
    {
        XByteArray* ba = XByteArray_create_utf8("ByteArray");
        XByteArrayView v = XByteArrayView_create_bytearray(ba);
        XPrintf("  create_bytearray('ByteArray'): size=%lld (期望 9)\n",
            (long long)XByteArrayView_size(&v));
        XByteArray_delete_base(ba);
    }

    /* create_bytearray(NULL) */
    {
        XByteArrayView v = XByteArrayView_create_bytearray(NULL);
        XPrintf("  create_bytearray(NULL): isNull=%d (期望 1)\n", XByteArrayView_isNull(&v));
    }

    XPrintf("\n");
}

/* ==================== 2. 基本访问测试 ==================== */
static void XByteArrayViewTest_Access(void)
{
    XPrintf("===== 基本访问测试 =====\n");
    const uint8_t data[] = "ABCDEFGHIJ";
    XByteArrayView v = XByteArrayView_create_data(data, 10);

    XPrintf("  data()=%s\n", (const char*)XByteArrayView_data(&v));
    XPrintf("  constData()=%s\n", (const char*)XByteArrayView_constData(&v));
    XPrintf("  size()=%lld\n", (long long)XByteArrayView_size(&v));
    XPrintf("  length()=%lld\n", (long long)XByteArrayView_length(&v));
    XPrintf("  empty()=%d (期望 0)\n", XByteArrayView_empty(&v));
    XPrintf("  isNull()=%d (期望 0)\n", XByteArrayView_isNull(&v));
    XPrintf("  at(0)=%c, at(5)=%c, at(9)=%c (期望 A,F,J)\n",
        (char)XByteArrayView_at(&v, 0), (char)XByteArrayView_at(&v, 5), (char)XByteArrayView_at(&v, 9));
    XPrintf("  front()=%c (期望 A)\n", (char)XByteArrayView_front(&v));
    XPrintf("  back()=%c (期望 J)\n", (char)XByteArrayView_back(&v));
    XPrintf("  at(-1)=%d (期望 0)\n", XByteArrayView_at(&v, -1));
    XPrintf("  at(100)=%d (期望 0)\n", XByteArrayView_at(&v, 100));
    XPrintf("  maxSize()=%lld (期望 9223372036854775806)\n", (long long)XByteArrayView_maxSize());

    XPrintf("\n");
}

/* ==================== 3. 子视图测试 ==================== */
static void XByteArrayViewTest_SubView(void)
{
    XPrintf("===== 子视图测试 =====\n");
    const uint8_t data[] = "0123456789";
    XByteArrayView v = XByteArrayView_create_data(data, 10);

    {
        XByteArrayView sub = XByteArrayView_first_n(&v, 3);
        XPrintf("  first_n(3): size=%lld, data=%.*s (期望 3,012)\n",
            (long long)XByteArrayView_size(&sub), (int)XByteArrayView_size(&sub),
            (const char*)XByteArrayView_data(&sub));
    }
    {
        XByteArrayView sub = XByteArrayView_first_n(&v, 0);
        XPrintf("  first_n(0): size=%lld (期望 0)\n", (long long)XByteArrayView_size(&sub));
    }
    {
        XByteArrayView sub = XByteArrayView_first_n(&v, 100);
        XPrintf("  first_n(100): size=%lld (期望 10)\n", (long long)XByteArrayView_size(&sub));
    }
    {
        XByteArrayView sub = XByteArrayView_last_n(&v, 3);
        XPrintf("  last_n(3): size=%lld, data=%.*s (期望 3,789)\n",
            (long long)XByteArrayView_size(&sub), (int)XByteArrayView_size(&sub),
            (const char*)XByteArrayView_data(&sub));
    }
    {
        XByteArrayView sub = XByteArrayView_sliced(&v, 3);
        XPrintf("  sliced(3): size=%lld, data=%.*s (期望 7,3456789)\n",
            (long long)XByteArrayView_size(&sub), (int)XByteArrayView_size(&sub),
            (const char*)XByteArrayView_data(&sub));
    }
    {
        XByteArrayView sub = XByteArrayView_sliced_n(&v, 2, 4);
        XPrintf("  sliced_n(2,4): size=%lld, data=%.*s (期望 4,2345)\n",
            (long long)XByteArrayView_size(&sub), (int)XByteArrayView_size(&sub),
            (const char*)XByteArrayView_data(&sub));
    }
    {
        XByteArrayView sub = XByteArrayView_chopped(&v, 3);
        XPrintf("  chopped(3): size=%lld, data=%.*s (期望 7,0123456)\n",
            (long long)XByteArrayView_size(&sub), (int)XByteArrayView_size(&sub),
            (const char*)XByteArrayView_data(&sub));
    }

    /* left/right/mid 宏（传值，非指针） */
    {
        XByteArrayView l = XByteArrayView_left(&v, 3);
        XByteArrayView r = XByteArrayView_right(&v, 3);
        XByteArrayView m = XByteArrayView_mid(&v, 2, 5);
        XPrintf("  left(3)=%.*s (期望 012)\n", (int)XByteArrayView_size(&l), (const char*)XByteArrayView_data(&l));
        XPrintf("  right(3)=%.*s (期望 789)\n", (int)XByteArrayView_size(&r), (const char*)XByteArrayView_data(&r));
        XPrintf("  mid(2,5)=%.*s (期望 23456)\n", (int)XByteArrayView_size(&m), (const char*)XByteArrayView_data(&m));
    }

    XPrintf("\n");
}

/* ==================== 4. 原地修改测试 ==================== */
static void XByteArrayViewTest_Modify(void)
{
    XPrintf("===== 原地修改测试 =====\n");
    const uint8_t data[] = "HelloWorld";
    XByteArrayView v = XByteArrayView_create_data(data, 10);

    XByteArrayView_truncate(&v, 5);
    XPrintf("  truncate(5): size=%lld, data=%.*s (期望 5,Hello)\n",
        (long long)XByteArrayView_size(&v), (int)XByteArrayView_size(&v),
        (const char*)XByteArrayView_data(&v));

    XByteArrayView_chop(&v, 2);
    XPrintf("  chop(2): size=%lld, data=%.*s (期望 3,Hel)\n",
        (long long)XByteArrayView_size(&v), (int)XByteArrayView_size(&v),
        (const char*)XByteArrayView_data(&v));

    XByteArrayView_truncate(&v, 0);
    XPrintf("  truncate(0): size=%lld, empty=%d (期望 0,1)\n",
        (long long)XByteArrayView_size(&v), XByteArrayView_empty(&v));

    v = XByteArrayView_create_data(data, 10);
    XByteArrayView_chop(&v, 0);
    XPrintf("  chop(0): size=%lld (期望 10)\n", (long long)XByteArrayView_size(&v));

    XPrintf("\n");
}

/* ==================== 5. 查找测试 ==================== */
static void XByteArrayViewTest_Find(void)
{
    XPrintf("===== 查找测试 =====\n");
    const uint8_t data[] = "abcXYZabc123";
    XByteArrayView v = XByteArrayView_create_data(data, 12);

    /* indexOf_char */
    XPrintf("  indexOf_char('a',0)=%lld (期望 0)\n", (long long)XByteArrayView_indexOf_char(&v, 'a', 0));
    XPrintf("  indexOf_char('a',1)=%lld (期望 6)\n", (long long)XByteArrayView_indexOf_char(&v, 'a', 1));
    XPrintf("  indexOf_char('?',0)=%lld (期望 -1)\n", (long long)XByteArrayView_indexOf_char(&v, '?', 0));

    /* indexOf (视图) */
    {
        const uint8_t abc[] = "abc";
        XByteArrayView abcView = XByteArrayView_create_data(abc, 3);
        XPrintf("  indexOf('abc',0)=%lld (期望 0)\n", (long long)XByteArrayView_indexOf(&v, &abcView, 0));
        XPrintf("  indexOf('abc',1)=%lld (期望 6)\n", (long long)XByteArrayView_indexOf(&v, &abcView, 1));
    }

    /* lastIndexOf_char */
    XPrintf("  lastIndexOf_char('a',0)=%lld (期望 0, from=0 只搜索位置0)\n", (long long)XByteArrayView_lastIndexOf_char(&v, 'a', 0));
    XPrintf("  lastIndexOf_char('c',0)=%lld (期望 -1, from=0 只搜索位置0)\n", (long long)XByteArrayView_lastIndexOf_char(&v, 'c', 0));
    XPrintf("  lastIndexOf_char('?',0)=%lld (期望 -1)\n", (long long)XByteArrayView_lastIndexOf_char(&v, '?', 0));

    /* lastIndexOf (视图) */
    {
        const uint8_t abc[] = "abc";
        XByteArrayView abcView = XByteArrayView_create_data(abc, 3);
        XPrintf("  lastIndexOf('abc')=%lld (期望 6)\n", (long long)XByteArrayView_lastIndexOf(&v, &abcView));
        XPrintf("  lastIndexOf_from('abc',0)=%lld (期望 0, from=0 只搜索位置0)\n", (long long)XByteArrayView_lastIndexOf_from(&v, &abcView, 0));
    }

    /* contains_char / contains */
    XPrintf("  contains_char('X')=%d (期望 1)\n", XByteArrayView_contains_char(&v, 'X'));
    XPrintf("  contains_char('?')=%d (期望 0)\n", XByteArrayView_contains_char(&v, '?'));
    {
        const uint8_t abc[] = "abc";
        XByteArrayView abcView = XByteArrayView_create_data(abc, 3);
        XPrintf("  contains('abc')=%d (期望 1)\n", XByteArrayView_contains(&v, &abcView));
    }

    /* count_char / count */
    XPrintf("  count_char('a')=%lld (期望 2)\n", (long long)XByteArrayView_count_char(&v, 'a'));
    XPrintf("  count_char('?')=%lld (期望 0)\n", (long long)XByteArrayView_count_char(&v, '?'));
    {
        const uint8_t abc[] = "abc";
        XByteArrayView abcView = XByteArrayView_create_data(abc, 3);
        XPrintf("  count('abc')=%lld (期望 2)\n", (long long)XByteArrayView_count(&v, &abcView));
    }

    /* startsWith_char / endsWith_char */
    XPrintf("  startsWith_char('a')=%d (期望 1)\n", XByteArrayView_startsWith_char(&v, 'a'));
    XPrintf("  startsWith_char('b')=%d (期望 0)\n", XByteArrayView_startsWith_char(&v, 'b'));
    XPrintf("  endsWith_char('3')=%d (期望 1)\n", XByteArrayView_endsWith_char(&v, '3'));
    XPrintf("  endsWith_char('2')=%d (期望 0)\n", XByteArrayView_endsWith_char(&v, '2'));

    /* startsWith / endsWith (视图) */
    {
        const uint8_t pref[] = "abc";
        XByteArrayView prefView = XByteArrayView_create_data(pref, 3);
        XPrintf("  startsWith('abc')=%d (期望 1)\n", XByteArrayView_startsWith(&v, &prefView));
    }
    {
        const uint8_t suff[] = "123";
        XByteArrayView suffView = XByteArrayView_create_data(suff, 3);
        XPrintf("  endsWith('123')=%d (期望 1)\n", XByteArrayView_endsWith(&v, &suffView));
    }

    XPrintf("\n");
}

/* ==================== 6. 比较测试 ==================== */
static void XByteArrayViewTest_Compare(void)
{
    XPrintf("===== 比较测试 =====\n");
    const uint8_t d1[] = "Hello";
    const uint8_t d2[] = "Hello";
    const uint8_t d3[] = "HELLO";
    const uint8_t d4[] = "World";
    XByteArrayView v1 = XByteArrayView_create_data(d1, 5);
    XByteArrayView v2 = XByteArrayView_create_data(d2, 5);
    XByteArrayView v3 = XByteArrayView_create_data(d3, 5);
    XByteArrayView v4 = XByteArrayView_create_data(d4, 5);

    /* equal (无 cs 参数) */
    XPrintf("  equal('Hello','Hello')=%d (期望 1)\n", XByteArrayView_equal(&v1, &v2));
    XPrintf("  equal('Hello','World')=%d (期望 0)\n", XByteArrayView_equal(&v1, &v4));

    /* compare */
    XPrintf("  compare('Hello','Hello',1)=%d (期望 0)\n", XByteArrayView_compare(&v1, &v2, 1));
    XPrintf("  compare('Hello','HELLO',1)=%d (期望 >0)\n", XByteArrayView_compare(&v1, &v3, 1));
    XPrintf("  compare('Hello','HELLO',0)=%d (期望 0)\n", XByteArrayView_compare(&v1, &v3, 0));
    XPrintf("  compare('Hello','World')=%d (期望 <0)\n", XByteArrayView_compare(&v1, &v4, 1));

    XPrintf("\n");
}

/* ==================== 7. 修剪测试 ==================== */
static void XByteArrayViewTest_Trim(void)
{
    XPrintf("===== 修剪测试 =====\n");
    const uint8_t d1[] = "  Hello World  ";
    const uint8_t d2[] = "\t\n  \r";
    const uint8_t d3[] = "NoTrim";
    XByteArrayView v1 = XByteArrayView_create_data(d1, 15);
    XByteArrayView v2 = XByteArrayView_create_data(d2, 5);
    XByteArrayView v3 = XByteArrayView_create_data(d3, 6);

    XByteArrayView t1 = XByteArrayView_trimmed(&v1);
    XPrintf("  trimmed('  Hello World  '): size=%lld, data=%.*s (期望 11,Hello World)\n",
        (long long)XByteArrayView_size(&t1), (int)XByteArrayView_size(&t1),
        (const char*)XByteArrayView_data(&t1));

    XByteArrayView t2 = XByteArrayView_trimmed(&v2);
    XPrintf("  trimmed(whitespace only): size=%lld (期望 0)\n", (long long)XByteArrayView_size(&t2));

    XByteArrayView t3 = XByteArrayView_trimmed(&v3);
    XPrintf("  trimmed('NoTrim'): size=%lld (期望 6)\n", (long long)XByteArrayView_size(&t3));

    XPrintf("\n");
}

/* ==================== 8. 数值转换测试 ==================== */
static void XByteArrayViewTest_NumConvert(void)
{
    XPrintf("===== 数值转换测试 =====\n");
    bool ok;

    {
        const uint8_t d[] = "12345";
        XByteArrayView v = XByteArrayView_create_data(d, 5);
        int val = XByteArrayView_toInt(&v, &ok, 10);
        XPrintf("  toInt('12345')=%d, ok=%d (期望 12345,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "-6789";
        XByteArrayView v = XByteArrayView_create_data(d, 5);
        int val = XByteArrayView_toInt(&v, &ok, 10);
        XPrintf("  toInt('-6789')=%d, ok=%d (期望 -6789,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "0xFF";
        XByteArrayView v = XByteArrayView_create_data(d, 4);
        int val = XByteArrayView_toInt(&v, &ok, 16);
        XPrintf("  toInt('0xFF',16)=%d, ok=%d (期望 255,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "abc";
        XByteArrayView v = XByteArrayView_create_data(d, 3);
        int val = XByteArrayView_toInt(&v, &ok, 10);
        XPrintf("  toInt('abc')=%d, ok=%d (期望 0,0)\n", val, ok);
    }
    {
        const uint8_t d[] = "3.14159";
        XByteArrayView v = XByteArrayView_create_data(d, 7);
        double val = XByteArrayView_toDouble(&v, &ok);
        XPrintf("  toDouble('3.14159')=%f, ok=%d (期望 ~3.14159,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "9223372036854775807";
        XByteArrayView v = XByteArrayView_create_data(d, 19);
        int64_t val = XByteArrayView_toLongLong(&v, &ok, 10);
        XPrintf("  toLongLong(max)=%lld, ok=%d (期望 9223372036854775807,1)\n", (long long)val, ok);
    }
    {
        const uint8_t d[] = "2.71828";
        XByteArrayView v = XByteArrayView_create_data(d, 7);
        float val = XByteArrayView_toFloat(&v, &ok);
        XPrintf("  toFloat('2.71828')=%f, ok=%d (期望 ~2.71828,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "32767";
        XByteArrayView v = XByteArrayView_create_data(d, 5);
        short val = XByteArrayView_toShort(&v, &ok, 10);
        XPrintf("  toShort('32767')=%d, ok=%d (期望 32767,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "65535";
        XByteArrayView v = XByteArrayView_create_data(d, 5);
        unsigned short val = XByteArrayView_toUShort(&v, &ok, 10);
        XPrintf("  toUShort('65535')=%u, ok=%d (期望 65535,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "4294967295";
        XByteArrayView v = XByteArrayView_create_data(d, 10);
        unsigned int val = XByteArrayView_toUInt(&v, &ok, 10);
        XPrintf("  toUInt('4294967295')=%u, ok=%d (期望 4294967295,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "2147483647";
        XByteArrayView v = XByteArrayView_create_data(d, 10);
        long val = XByteArrayView_toLong(&v, &ok, 10);
        XPrintf("  toLong('2147483647')=%ld, ok=%d (期望 2147483647,1)\n", val, ok);
    }
    {
        const uint8_t d[] = "18446744073709551615";
        XByteArrayView v = XByteArrayView_create_data(d, 20);
        uint64_t val = XByteArrayView_toULongLong(&v, &ok, 10);
        XPrintf("  toULongLong(max)=%llu, ok=%d (期望 18446744073709551615,1)\n",
            (unsigned long long)val, ok);
    }
    {
        XByteArrayView v = XByteArrayView_create();
        int val = XByteArrayView_toInt(&v, &ok, 10);
        XPrintf("  toInt(null view)=%d, ok=%d (期望 0,0)\n", val, ok);
    }

    XPrintf("\n");
}

/* ==================== 9. UTF-8 编码检测测试 ==================== */
static void XByteArrayViewTest_Utf8Check(void)
{
    XPrintf("===== UTF-8 编码检测测试 =====\n");

    {
        const uint8_t d[] = "Hello";
        XByteArrayView v = XByteArrayView_create_data(d, 5);
        XPrintf("  isValidUtf8('Hello')=%d (期望 1)\n", XByteArrayView_isValidUtf8(&v));
    }
    {
        const uint8_t d[] = "你好世界";
        XByteArrayView v = XByteArrayView_create_data(d, 12);
        XPrintf("  isValidUtf8('你好世界')=%d (期望 1)\n", XByteArrayView_isValidUtf8(&v));
    }
    {
        const uint8_t d[] = { 0xFF, 0xFE, 0x00 };
        XByteArrayView v = XByteArrayView_create_data(d, 3);
        XPrintf("  isValidUtf8(0xFF,0xFE)=%d (期望 0)\n", XByteArrayView_isValidUtf8(&v));
    }
    {
        XByteArrayView v = XByteArrayView_create();
        XPrintf("  isValidUtf8(null)=%d (期望 1)\n", XByteArrayView_isValidUtf8(&v));
    }

    XPrintf("\n");
}

/* ==================== 10. 迭代器测试 ==================== */
static void XByteArrayViewTest_Iterator(void)
{
    XPrintf("===== 迭代器测试 =====\n");
    const uint8_t data[] = "ABCDE";
    XByteArrayView v = XByteArrayView_create_data(data, 5);

    {
        XByteArrayView_iterator it = XByteArrayView_begin(&v);
        XByteArrayView_iterator end = XByteArrayView_end(&v);
        int count = 0;
        while (!XByteArrayView_iterator_equality(&it, &end)) { count++; XByteArrayView_iterator_add(&v, &it); }
        XPrintf("  begin->end count=%d (期望 5)\n", count);
    }
    {
        XByteArrayView_iterator it = XByteArrayView_begin(&v);
        XByteArrayView_iterator end = XByteArrayView_end(&v);
        XPrintf("  begin->end distance via iteration (期望 5)\n");
    }
    {
        XByteArrayView_reverse_iterator it = XByteArrayView_rbegin(&v);
        XByteArrayView_reverse_iterator end = XByteArrayView_rend(&v);
        int count = 0;
        while (!XByteArrayView_reverse_iterator_equality(&it, &end)) { count++; XByteArrayView_reverse_iterator_add(&v, &it); }
        XPrintf("  rbegin->rend count=%d (期望 5)\n", count);
    }

    XPrintf("\n");
}

/* ==================== 11. Null/空视图边界测试 ==================== */
static void XByteArrayViewTest_NullEmpty(void)
{
    XPrintf("===== Null/空视图边界测试 =====\n");

    XByteArrayView nullView = XByteArrayView_create();
    const uint8_t emptyData[] = "";
    XByteArrayView emptyView = XByteArrayView_create_data(emptyData, 0);

    XPrintf("  nullView: isNull=%d, empty=%d, size=%lld (期望 1,1,0)\n",
        XByteArrayView_isNull(&nullView), XByteArrayView_empty(&nullView),
        (long long)XByteArrayView_size(&nullView));
    XPrintf("  emptyView: isNull=%d, empty=%d, size=%lld (期望 0,1,0)\n",
        XByteArrayView_isNull(&emptyView), XByteArrayView_empty(&emptyView),
        (long long)XByteArrayView_size(&emptyView));

    XPrintf("  nullView front()=%d (期望 0)\n", XByteArrayView_front(&nullView));
    XPrintf("  nullView back()=%d (期望 0)\n", XByteArrayView_back(&nullView));
    XPrintf("  nullView at(0)=%d (期望 0)\n", XByteArrayView_at(&nullView, 0));

    {
        XByteArrayView sub = XByteArrayView_first_n(&nullView, 3);
        XPrintf("  nullView first_n(3): size=%lld (期望 0)\n", (long long)XByteArrayView_size(&sub));
    }
    {
        XByteArrayView t = XByteArrayView_trimmed(&nullView);
        XPrintf("  nullView trimmed: isNull=%d (期望 1)\n", XByteArrayView_isNull(&t));
    }

    XPrintf("\n");
}

/* ==================== 全部测试 ==================== */
static void XByteArrayViewTest_All(void)
{
    XByteArrayViewTest_Create();
    XByteArrayViewTest_Access();
    XByteArrayViewTest_SubView();
    XByteArrayViewTest_Modify();
    XByteArrayViewTest_Find();
    XByteArrayViewTest_Compare();
    XByteArrayViewTest_Trim();
    XByteArrayViewTest_NumConvert();
    XByteArrayViewTest_Utf8Check();
    XByteArrayViewTest_Iterator();
    XByteArrayViewTest_NullEmpty();
}

/* ==================== 菜单入口 ==================== */
void XByteArrayViewTest(void)
{
    XByteArrayViewTest_All();
}

void XMenu_XByteArrayViewTest(XMenu* root)
{
    XMenu* menu = XMenu_create("ByteArrayView(字节数组视图)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全部测试");
        XAction_setAction(action, XByteArrayViewTest_All);
    }
    {
        XAction* action = XMenu_addAction(menu, "构造与创建");
        XAction_setAction(action, XByteArrayViewTest_Create);
    }
    {
        XAction* action = XMenu_addAction(menu, "基本访问");
        XAction_setAction(action, XByteArrayViewTest_Access);
    }
    {
        XAction* action = XMenu_addAction(menu, "子视图");
        XAction_setAction(action, XByteArrayViewTest_SubView);
    }
    {
        XAction* action = XMenu_addAction(menu, "原地修改");
        XAction_setAction(action, XByteArrayViewTest_Modify);
    }
    {
        XAction* action = XMenu_addAction(menu, "查找");
        XAction_setAction(action, XByteArrayViewTest_Find);
    }
    {
        XAction* action = XMenu_addAction(menu, "比较");
        XAction_setAction(action, XByteArrayViewTest_Compare);
    }
    {
        XAction* action = XMenu_addAction(menu, "修剪");
        XAction_setAction(action, XByteArrayViewTest_Trim);
    }
    {
        XAction* action = XMenu_addAction(menu, "数值转换");
        XAction_setAction(action, XByteArrayViewTest_NumConvert);
    }
    {
        XAction* action = XMenu_addAction(menu, "UTF-8检测");
        XAction_setAction(action, XByteArrayViewTest_Utf8Check);
    }
    {
        XAction* action = XMenu_addAction(menu, "迭代器");
        XAction_setAction(action, XByteArrayViewTest_Iterator);
    }
    {
        XAction* action = XMenu_addAction(menu, "Null/空边界");
        XAction_setAction(action, XByteArrayViewTest_NullEmpty);
    }
}
#endif
