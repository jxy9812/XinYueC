/**
 * @file XStringViewTest.c
 * @brief XStringView 全面测试（对标 Qt 6.8 QStringView）
 * @details 覆盖构造、访问、子视图、查找、比较、数值转换、迭代器等全部 API，
 *          包括 null/empty 边界、内存安全测试。
 */
#include "XDataStructTest.h"
#if DEMOTEST
#include "XStringView.h"
#include "XString.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>

/* ==================== 1. 构造与创建测试 ==================== */
static void XStringViewTest_Create(void)
{
    XPrintf("===== 构造与创建测试 =====\n");

    /* create() — 默认 null view */
    {
        XStringView v = XStringView_create();
        XPrintf("  create(): data=%s, size=%lld, isNull=%d, empty=%d (期望 NULL,0,1,1)\n",
            v.m_data ? "非NULL" : "NULL", (long long)v.m_size,
            XStringView_isNull(&v), XStringView_empty(&v));
    }

    /* create_data() */
    {
        const XChar data[] = { 'H', 'e', 'l', 'l', 'o', 0 };
        XStringView v = XStringView_create_data(data, 5);
        XPrintf("  create_data('Hello',5): size=%lld, at(0)=%c, at(4)=%c (期望 5,H,o)\n",
            (long long)XStringView_size(&v),
            (char)XStringView_at(&v, 0), (char)XStringView_at(&v, 4));
    }

    /* create_data(NULL, 0) */
    {
        XStringView v = XStringView_create_data(NULL, 0);
        XPrintf("  create_data(NULL,0): isNull=%d (期望 1)\n", XStringView_isNull(&v));
    }

    /* create_range() */
    {
        const XChar data[] = { 'W', 'o', 'r', 'l', 'd' };
        XStringView v = XStringView_create_range(data, data + 5);
        XPrintf("  create_range('World'): size=%lld, front=%c, back=%c (期望 5,W,d)\n",
            (long long)XStringView_size(&v),
            (char)XStringView_front(&v), (char)XStringView_back(&v));
    }

    /* create_cstr() */
    {
        const XChar data[] = { 'T', 'e', 's', 't', 0 };
        XStringView v = XStringView_create_cstr(data);
        XPrintf("  create_cstr('Test'): size=%lld (期望 4)\n", (long long)XStringView_size(&v));
    }

    /* create_cstr(NULL) */
    {
        XStringView v = XStringView_create_cstr(NULL);
        XPrintf("  create_cstr(NULL): isNull=%d (期望 1)\n", XStringView_isNull(&v));
    }

    /* create_utf16() */
    {
        const uint16_t data[] = { 'U', 'T', 'F', '1', '6' };
        XStringView v = XStringView_create_utf16(data, 5);
        XPrintf("  create_utf16(): size=%lld (期望 5)\n", (long long)XStringView_size(&v));
    }

    /* create_string() — 从 XString */
    {
        XString* s = XString_create_utf8("XString");
        XStringView v = XStringView_create_string(s);
        XPrintf("  create_string('XString'): size=%lld (期望 7)\n", (long long)XStringView_size(&v));
        XString_delete_base(s);
    }

    /* create_string(NULL) */
    {
        XStringView v = XStringView_create_string(NULL);
        XPrintf("  create_string(NULL): isNull=%d (期望 1)\n", XStringView_isNull(&v));
    }

    XPrintf("\n");
}

/* ==================== 2. 基本访问测试 ==================== */
static void XStringViewTest_Access(void)
{
    XPrintf("===== 基本访问测试 =====\n");
    const XChar data[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J' };
    XStringView v = XStringView_create_data(data, 10);

    XPrintf("  data()=%s\n", XStringView_data(&v) ? "非NULL" : "NULL");
    XPrintf("  constData()=%s\n", XStringView_constData(&v) ? "非NULL" : "NULL");
    XPrintf("  size()=%lld\n", (long long)XStringView_size(&v));
    XPrintf("  length()=%lld\n", (long long)XStringView_length(&v));
    XPrintf("  empty()=%d (期望 0)\n", XStringView_empty(&v));
    XPrintf("  isNull()=%d (期望 0)\n", XStringView_isNull(&v));
    XPrintf("  at(0)=%c, at(5)=%c, at(9)=%c (期望 A,F,J)\n",
        (char)XStringView_at(&v, 0), (char)XStringView_at(&v, 5), (char)XStringView_at(&v, 9));
    XPrintf("  front()=%c (期望 A)\n", (char)XStringView_front(&v));
    XPrintf("  back()=%c (期望 J)\n", (char)XStringView_back(&v));

    /* 越界访问 */
    XPrintf("  at(-1)=%d (期望 0)\n", XStringView_at(&v, -1));
    XPrintf("  at(100)=%d (期望 0)\n", XStringView_at(&v, 100));

    XPrintf("\n");
}

/* ==================== 3. 子视图测试 ==================== */
static void XStringViewTest_SubView(void)
{
    XPrintf("===== 子视图测试 =====\n");
    const XChar data[] = { '0','1','2','3','4','5','6','7','8','9' };
    XStringView v = XStringView_create_data(data, 10);

    /* first_n */
    {
        XStringView sub = XStringView_first_n(&v, 3);
        XPrintf("  first_n(3): size=%lld (期望 3)\n", (long long)XStringView_size(&sub));
    }
    /* first_n(0) */
    {
        XStringView sub = XStringView_first_n(&v, 0);
        XPrintf("  first_n(0): size=%lld (期望 0)\n", (long long)XStringView_size(&sub));
    }
    /* first_n(超过大小) */
    {
        XStringView sub = XStringView_first_n(&v, 100);
        XPrintf("  first_n(100): size=%lld (期望 10)\n", (long long)XStringView_size(&sub));
    }

    /* last_n */
    {
        XStringView sub = XStringView_last_n(&v, 3);
        XPrintf("  last_n(3): size=%lld (期望 3)\n", (long long)XStringView_size(&sub));
    }

    /* sliced */
    {
        XStringView sub = XStringView_sliced(&v, 3);
        XPrintf("  sliced(3): size=%lld (期望 7)\n", (long long)XStringView_size(&sub));
    }

    /* sliced_2 */
    {
        XStringView sub = XStringView_sliced_2(&v, 2, 4);
        XPrintf("  sliced_2(2,4): size=%lld (期望 4)\n", (long long)XStringView_size(&sub));
    }

    /* chopped */
    {
        XStringView sub = XStringView_chopped(&v, 3);
        XPrintf("  chopped(3): size=%lld (期望 7)\n", (long long)XStringView_size(&sub));
    }

    /* left/right/mid 宏 */
    {
        XStringView l = XStringView_left(&v, 3);
        XStringView r = XStringView_right(&v, 3);
        XStringView m = XStringView_mid(&v, 2, 5);
        XPrintf("  left(3) size=%lld (期望 3)\n", (long long)XStringView_size(&l));
        XPrintf("  right(3) size=%lld (期望 3)\n", (long long)XStringView_size(&r));
        XPrintf("  mid(2,5) size=%lld (期望 5)\n", (long long)XStringView_size(&m));
    }

    XPrintf("\n");
}

/* ==================== 4. 原地修改测试 ==================== */
static void XStringViewTest_Modify(void)
{
    XPrintf("===== 原地修改测试 =====\n");
    const XChar data[] = { 'H','e','l','l','o','W','o','r','l','d' };
    XStringView v = XStringView_create_data(data, 10);

    XStringView_truncate(&v, 5);
    XPrintf("  truncate(5): size=%lld (期望 5)\n", (long long)XStringView_size(&v));

    XStringView_chop(&v, 2);
    XPrintf("  chop(2): size=%lld (期望 3)\n", (long long)XStringView_size(&v));

    XStringView_truncate(&v, 0);
    XPrintf("  truncate(0): size=%lld, empty=%d (期望 0,1)\n",
        (long long)XStringView_size(&v), XStringView_empty(&v));

    /* chop(0) */
    v = XStringView_create_data(data, 10);
    XStringView_chop(&v, 0);
    XPrintf("  chop(0): size=%lld (期望 10)\n", (long long)XStringView_size(&v));

    XPrintf("\n");
}

/* ==================== 5. 查找测试 ==================== */
static void XStringViewTest_Find(void)
{
    XPrintf("===== 查找测试 =====\n");
    const XChar data[] = { 'a','b','c','X','Y','Z','a','b','c','1','2','3' };
    XStringView v = XStringView_create_data(data, 12);

    /* indexOf */
    XPrintf("  indexOf('a',0)=%lld (期望 0)\n", (long long)XStringView_indexOf_char(&v, 'a', 0, 1));
    XPrintf("  indexOf('a',1)=%lld (期望 6)\n", (long long)XStringView_indexOf_char(&v, 'a', 1, 1));
    XPrintf("  indexOf('?',0)=%lld (期望 -1)\n", (long long)XStringView_indexOf_char(&v, '?', 0, 1));

    /* lastIndexOf */
    XPrintf("  lastIndexOf('a',-1)=%lld (期望 6)\n", (long long)XStringView_lastIndexOf_char(&v, 'a', -1, 1));
    XPrintf("  lastIndexOf('c',-1)=%lld (期望 8)\n", (long long)XStringView_lastIndexOf_char(&v, 'c', -1, 1));

    /* contains */
    XPrintf("  contains('X')=%d (期望 1)\n", XStringView_contains_char(&v, 'X', 1));
    XPrintf("  contains('?')=%d (期望 0)\n", XStringView_contains_char(&v, '?', 1));

    /* count */
    XPrintf("  count('a')=%lld (期望 2)\n", (long long)XStringView_count_char(&v, 'a', 1));
    XPrintf("  count('?')=%lld (期望 0)\n", (long long)XStringView_count_char(&v, '?', 1));

    /* startsWith / endsWith */
    XPrintf("  startsWith('a')=%d (期望 1)\n", XStringView_startsWith_char(&v, 'a', 1));
    XPrintf("  endsWith('3')=%d (期望 1)\n", XStringView_endsWith_char(&v, '3', 1));

    /* startsWith / endsWith (视图) */
    {
        const XChar pref[] = { 'a','b','c' };
        XStringView prefView = XStringView_create_data(pref, 3);
        XPrintf("  startsWith('abc')=%d (期望 1)\n", XStringView_startsWith(&v, &prefView, 1));
    }
    {
        const XChar suff[] = { '1','2','3' };
        XStringView suffView = XStringView_create_data(suff, 3);
        XPrintf("  endsWith('123')=%d (期望 1)\n", XStringView_endsWith(&v, &suffView, 1));
    }

    XPrintf("\n");
}

/* ==================== 6. 比较测试 ==================== */
static void XStringViewTest_Compare(void)
{
    XPrintf("===== 比较测试 =====\n");
    const XChar d1[] = { 'H','e','l','l','o' };
    const XChar d2[] = { 'H','e','l','l','o' };
    const XChar d3[] = { 'H','E','L','L','O' };
    const XChar d4[] = { 'W','o','r','l','d' };
    XStringView v1 = XStringView_create_data(d1, 5);
    XStringView v2 = XStringView_create_data(d2, 5);
    XStringView v3 = XStringView_create_data(d3, 5);
    XStringView v4 = XStringView_create_data(d4, 5);

    XPrintf("  equal('Hello','Hello')=%d (期望 1)\n", XStringView_equal(&v1, &v2, 1));
    XPrintf("  equal('Hello','World')=%d (期望 0)\n", XStringView_equal(&v1, &v4, 1));
    XPrintf("  equal('Hello','HELLO',0)=%d (期望 1)\n", XStringView_equal(&v1, &v3, 0));
    XPrintf("  equal('Hello','HELLO',1)=%d (期望 0)\n", XStringView_equal(&v1, &v3, 1));

    XPrintf("  compare('Hello','Hello')=%d (期望 0)\n", XStringView_compare(&v1, &v2, 1));
    XPrintf("  compare('Hello','HELLO',0)=%d (期望 0)\n", XStringView_compare(&v1, &v3, 0));
    XPrintf("  compare('Hello','World')=%d (期望 <0)\n", XStringView_compare(&v1, &v4, 1));

    XPrintf("\n");
}

/* ==================== 7. 修剪测试 ==================== */
static void XStringViewTest_Trim(void)
{
    XPrintf("===== 修剪测试 =====\n");
    const XChar d1[] = { ' ',' ','H','e','l','l','o',' ',' ' };
    const XChar d2[] = { '\t','\n',' ','\r' };
    const XChar d3[] = { 'N','o','T','r','i','m' };
    XStringView v1 = XStringView_create_data(d1, 9);
    XStringView v2 = XStringView_create_data(d2, 4);
    XStringView v3 = XStringView_create_data(d3, 6);

    XStringView t1 = XStringView_trimmed(&v1);
    XPrintf("  trimmed('  Hello  '): size=%lld (期望 5)\n", (long long)XStringView_size(&t1));

    XStringView t2 = XStringView_trimmed(&v2);
    XPrintf("  trimmed(whitespace only): size=%lld (期望 0)\n", (long long)XStringView_size(&t2));

    XStringView t3 = XStringView_trimmed(&v3);
    XPrintf("  trimmed('NoTrim'): size=%lld (期望 6)\n", (long long)XStringView_size(&t3));

    XPrintf("\n");
}

/* ==================== 8. 数值转换测试 ==================== */
static void XStringViewTest_NumConvert(void)
{
    XPrintf("===== 数值转换测试 =====\n");
    bool ok;

    {
        const XChar d[] = { '1','2','3','4','5' };
        XStringView v = XStringView_create_data(d, 5);
        int val = XStringView_toInt(&v, &ok, 10);
        XPrintf("  toInt('12345')=%d, ok=%d (期望 12345,1)\n", val, ok);
    }
    {
        const XChar d[] = { '-','6','7','8','9' };
        XStringView v = XStringView_create_data(d, 5);
        int val = XStringView_toInt(&v, &ok, 10);
        XPrintf("  toInt('-6789')=%d, ok=%d (期望 -6789,1)\n", val, ok);
    }
    {
        const XChar d[] = { '0','x','F','F' };
        XStringView v = XStringView_create_data(d, 4);
        int val = XStringView_toInt(&v, &ok, 16);
        XPrintf("  toInt('0xFF',16)=%d, ok=%d (期望 255,1)\n", val, ok);
    }
    {
        const XChar d[] = { 'a','b','c' };
        XStringView v = XStringView_create_data(d, 3);
        int val = XStringView_toInt(&v, &ok, 10);
        XPrintf("  toInt('abc')=%d, ok=%d (期望 0,0)\n", val, ok);
    }
    {
        const XChar d[] = { '3','.','1','4','1','5','9' };
        XStringView v = XStringView_create_data(d, 7);
        double val = XStringView_toDouble(&v, &ok);
        XPrintf("  toDouble('3.14159')=%f, ok=%d (期望 ~3.14159,1)\n", val, ok);
    }
    {
        const XChar d[] = { '2','.','7','1','8' };
        XStringView v = XStringView_create_data(d, 5);
        float val = XStringView_toFloat(&v, &ok);
        XPrintf("  toFloat('2.718')=%f, ok=%d (期望 ~2.718,1)\n", val, ok);
    }
    {
        XStringView v = XStringView_create();
        int val = XStringView_toInt(&v, &ok, 10);
        XPrintf("  toInt(null view)=%d, ok=%d (期望 0,0)\n", val, ok);
    }

    XPrintf("\n");
}

/* ==================== 9. 迭代器测试 ==================== */
static void XStringViewTest_Iterator(void)
{
    XPrintf("===== 迭代器测试 =====\n");
    const XChar data[] = { 'A','B','C','D','E' };
    XStringView v = XStringView_create_data(data, 5);

    {
        const XChar* it = XStringView_begin(v);
        const XChar* end = XStringView_end(v);
        int count = 0;
        while (it != end) { count++; it++; }
        XPrintf("  begin->end count=%d (期望 5)\n", count);
    }
    {
        const XChar* it = XStringView_cbegin(v);
        const XChar* end = XStringView_cend(v);
        XPrintf("  cbegin->cend distance=%lld (期望 5)\n", (long long)(end - it));
    }
    {
        const XChar* it = XStringView_rbegin(v);
        const XChar* end = XStringView_rend(v);
        int count = 0;
        while (it != end) { count++; it--; }
        XPrintf("  rbegin->rend count=%d (期望 5)\n", count);
    }

    XPrintf("\n");
}

/* ==================== 10. Null/空视图边界测试 ==================== */
static void XStringViewTest_NullEmpty(void)
{
    XPrintf("===== Null/空视图边界测试 =====\n");

    XStringView nullView = XStringView_create();
    const XChar emptyData[] = { 0 };
    XStringView emptyView = XStringView_create_data(emptyData, 0);

    XPrintf("  nullView: isNull=%d, empty=%d, size=%lld (期望 1,1,0)\n",
        XStringView_isNull(&nullView), XStringView_empty(&nullView),
        (long long)XStringView_size(&nullView));
    XPrintf("  emptyView: isNull=%d, empty=%d, size=%lld (期望 0,1,0)\n",
        XStringView_isNull(&emptyView), XStringView_empty(&emptyView),
        (long long)XStringView_size(&emptyView));

    XPrintf("  nullView front()=%d (期望 0)\n", XStringView_front(&nullView));
    XPrintf("  nullView back()=%d (期望 0)\n", XStringView_back(&nullView));
    XPrintf("  nullView at(0)=%d (期望 0)\n", XStringView_at(&nullView, 0));

    {
        XStringView sub = XStringView_first_n(&nullView, 3);
        XPrintf("  nullView first_n(3): size=%lld (期望 0)\n", (long long)XStringView_size(&sub));
    }

    XPrintf("\n");
}

/* ==================== 11. toString 测试 ==================== */
static void XStringViewTest_ToString(void)
{
    XPrintf("===== toString 测试 =====\n");
    const XChar data[] = { 'H','e','l','l','o' };
    XStringView v = XStringView_create_data(data, 5);
    XString* s = XStringView_toString(&v);
    XPrintf("  toString(): size=%zu (期望 5)\n", XString_length_base(s));
    XString_delete_base(s);

    /* null view toString */
    {
        XStringView nv = XStringView_create();
        XString* ns = XStringView_toString(&nv);
        XPrintf("  toString(null view): isNull=%d (期望 1)\n", ns ? XString_isNull(ns) : 1);
        XString_delete_base(ns);
    }

    XPrintf("\n");
}

/* ==================== 全部测试 ==================== */
static void XStringViewTest_All(void)
{
    XStringViewTest_Create();
    XStringViewTest_Access();
    XStringViewTest_SubView();
    XStringViewTest_Modify();
    XStringViewTest_Find();
    XStringViewTest_Compare();
    XStringViewTest_Trim();
    XStringViewTest_NumConvert();
    XStringViewTest_Iterator();
    XStringViewTest_NullEmpty();
    XStringViewTest_ToString();
}

/* ==================== 菜单入口 ==================== */
void XStringViewTest(void)
{
    XStringViewTest_All();
}

void XMenu_XStringViewTest(XMenu* root)
{
    XMenu* menu = XMenu_create("StringView(字符串视图)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全部测试");
        XAction_setAction(action, XStringViewTest_All);
    }
    {
        XAction* action = XMenu_addAction(menu, "构造与创建");
        XAction_setAction(action, XStringViewTest_Create);
    }
    {
        XAction* action = XMenu_addAction(menu, "基本访问");
        XAction_setAction(action, XStringViewTest_Access);
    }
    {
        XAction* action = XMenu_addAction(menu, "子视图");
        XAction_setAction(action, XStringViewTest_SubView);
    }
    {
        XAction* action = XMenu_addAction(menu, "原地修改");
        XAction_setAction(action, XStringViewTest_Modify);
    }
    {
        XAction* action = XMenu_addAction(menu, "查找");
        XAction_setAction(action, XStringViewTest_Find);
    }
    {
        XAction* action = XMenu_addAction(menu, "比较");
        XAction_setAction(action, XStringViewTest_Compare);
    }
    {
        XAction* action = XMenu_addAction(menu, "修剪");
        XAction_setAction(action, XStringViewTest_Trim);
    }
    {
        XAction* action = XMenu_addAction(menu, "数值转换");
        XAction_setAction(action, XStringViewTest_NumConvert);
    }
    {
        XAction* action = XMenu_addAction(menu, "迭代器");
        XAction_setAction(action, XStringViewTest_Iterator);
    }
    {
        XAction* action = XMenu_addAction(menu, "Null/空边界");
        XAction_setAction(action, XStringViewTest_NullEmpty);
    }
    {
        XAction* action = XMenu_addAction(menu, "toString");
        XAction_setAction(action, XStringViewTest_ToString);
    }
}
#endif
