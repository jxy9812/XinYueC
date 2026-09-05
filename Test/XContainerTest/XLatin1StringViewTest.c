/**
 * @file XLatin1StringViewTest.c
 * @brief XLatin1StringView 全面测试（对标 Qt 6.8 QLatin1StringView）
 * @details 覆盖构造、访问、子视图、查找、比较、修剪、迭代器等全部 API，
 *          包括 null/empty 边界测试。
 */
#include "XDataStructTest.h"
#if DEMOTEST
#include "XLatin1StringView.h"
#include "XString.h"
#include "XByteArrayView.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>

/* ==================== 1. 构造与创建测试 ==================== */
static void XLatin1StringViewTest_Create(void)
{
    XPrintf("===== 构造与创建测试 =====\n");

    /* create() */
    {
        XLatin1StringView v = XLatin1StringView_create();
        XPrintf("  create(): data=%s, size=%lld, isNull=%d, empty=%d (期望 NULL,0,1,1)\n",
            v.m_data ? "非NULL" : "NULL", (long long)v.m_size,
            XLatin1StringView_isNull(&v), XLatin1StringView_empty(&v));
    }

    /* create_cstr() */
    {
        XLatin1StringView v = XLatin1StringView_create_cstr("Latin1");
        XPrintf("  create_cstr('Latin1'): size=%lld, data=%s (期望 6,Latin1)\n",
            (long long)XLatin1StringView_size(&v), XLatin1StringView_data(&v));
    }

    /* create_cstr(NULL) */
    {
        XLatin1StringView v = XLatin1StringView_create_cstr(NULL);
        XPrintf("  create_cstr(NULL): isNull=%d (期望 1)\n", XLatin1StringView_isNull(&v));
    }

    /* create_data() */
    {
        XLatin1StringView v = XLatin1StringView_create_data("Hello", 5);
        XPrintf("  create_data('Hello',5): size=%lld, at(0)=%c, at(4)=%c (期望 5,H,o)\n",
            (long long)XLatin1StringView_size(&v),
            XLatin1StringView_at(&v, 0), XLatin1StringView_at(&v, 4));
    }

    /* create_data(NULL, 0) */
    {
        XLatin1StringView v = XLatin1StringView_create_data(NULL, 0);
        XPrintf("  create_data(NULL,0): isNull=%d (期望 1)\n", XLatin1StringView_isNull(&v));
    }

    /* create_range() */
    {
        const char* data = "World";
        XLatin1StringView v = XLatin1StringView_create_range(data, data + 5);
        XPrintf("  create_range('World'): size=%lld, front=%c, back=%c (期望 5,W,d)\n",
            (long long)XLatin1StringView_size(&v),
            XLatin1StringView_front(&v), XLatin1StringView_back(&v));
    }

    /* create_bytearrayview() */
    {
        const uint8_t bdata[] = "ByteView";
        XByteArrayView bav = XByteArrayView_create_data(bdata, 8);
        XLatin1StringView v = XLatin1StringView_create_bytearrayview(&bav);
        XPrintf("  create_bytearrayview('ByteView'): size=%lld (期望 8)\n",
            (long long)XLatin1StringView_size(&v));
    }

    /* create_bytearrayview(NULL) */
    {
        XLatin1StringView v = XLatin1StringView_create_bytearrayview(NULL);
        XPrintf("  create_bytearrayview(NULL): isNull=%d (期望 1)\n", XLatin1StringView_isNull(&v));
    }

    XPrintf("\n");
}

/* ==================== 2. 基本访问测试 ==================== */
static void XLatin1StringViewTest_Access(void)
{
    XPrintf("===== 基本访问测试 =====\n");
    XLatin1StringView v = XLatin1StringView_create_cstr("ABCDEFGHIJ");

    XPrintf("  latin1()=%s\n", XLatin1StringView_latin1(&v));
    XPrintf("  data()=%s\n", XLatin1StringView_data(&v));
    XPrintf("  constData()=%s\n", XLatin1StringView_constData(&v));
    XPrintf("  size()=%lld\n", (long long)XLatin1StringView_size(&v));
    XPrintf("  empty()=%d (期望 0)\n", XLatin1StringView_empty(&v));
    XPrintf("  isNull()=%d (期望 0)\n", XLatin1StringView_isNull(&v));
    XPrintf("  at(0)=%c, at(5)=%c, at(9)=%c (期望 A,F,J)\n",
        XLatin1StringView_at(&v, 0), XLatin1StringView_at(&v, 5), XLatin1StringView_at(&v, 9));
    XPrintf("  front()=%c (期望 A)\n", XLatin1StringView_front(&v));
    XPrintf("  back()=%c (期望 J)\n", XLatin1StringView_back(&v));

    /* 越界 */
    XPrintf("  at(-1)=%d (期望 0)\n", XLatin1StringView_at(&v, -1));
    XPrintf("  at(100)=%d (期望 0)\n", XLatin1StringView_at(&v, 100));

    XPrintf("\n");
}

/* ==================== 3. 子视图测试 ==================== */
static void XLatin1StringViewTest_SubView(void)
{
    XPrintf("===== 子视图测试 =====\n");
    XLatin1StringView v = XLatin1StringView_create_cstr("0123456789");

    {
        XLatin1StringView sub = XLatin1StringView_first_n(&v, 3);
        XPrintf("  first_n(3): size=%lld (期望 3)\n", (long long)XLatin1StringView_size(&sub));
    }
    {
        XLatin1StringView sub = XLatin1StringView_first_n(&v, 0);
        XPrintf("  first_n(0): size=%lld (期望 0)\n", (long long)XLatin1StringView_size(&sub));
    }
    {
        XLatin1StringView sub = XLatin1StringView_first_n(&v, 100);
        XPrintf("  first_n(100): size=%lld (期望 10)\n", (long long)XLatin1StringView_size(&sub));
    }
    {
        XLatin1StringView sub = XLatin1StringView_last_n(&v, 3);
        XPrintf("  last_n(3): size=%lld (期望 3)\n", (long long)XLatin1StringView_size(&sub));
    }
    {
        XLatin1StringView sub = XLatin1StringView_sliced(&v, 3);
        XPrintf("  sliced(3): size=%lld (期望 7)\n", (long long)XLatin1StringView_size(&sub));
    }
    {
        XLatin1StringView sub = XLatin1StringView_sliced_2(&v, 2, 4);
        XPrintf("  sliced_2(2,4): size=%lld (期望 4)\n", (long long)XLatin1StringView_size(&sub));
    }
    {
        XLatin1StringView sub = XLatin1StringView_chopped(&v, 3);
        XPrintf("  chopped(3): size=%lld (期望 7)\n", (long long)XLatin1StringView_size(&sub));
    }

    /* left/right/mid 宏 */
    {
        XLatin1StringView l = XLatin1StringView_left(&v, 3);
        XLatin1StringView r = XLatin1StringView_right(&v, 3);
        XLatin1StringView m = XLatin1StringView_mid(&v, 2, 5);
        XPrintf("  left(3) size=%lld (期望 3)\n", (long long)XLatin1StringView_size(&l));
        XPrintf("  right(3) size=%lld (期望 3)\n", (long long)XLatin1StringView_size(&r));
        XPrintf("  mid(2,5) size=%lld (期望 5)\n", (long long)XLatin1StringView_size(&m));
    }

    XPrintf("\n");
}

/* ==================== 4. 原地修改测试 ==================== */
static void XLatin1StringViewTest_Modify(void)
{
    XPrintf("===== 原地修改测试 =====\n");
    XLatin1StringView v = XLatin1StringView_create_cstr("HelloWorld");

    XLatin1StringView_truncate(&v, 5);
    XPrintf("  truncate(5): size=%lld (期望 5)\n", (long long)XLatin1StringView_size(&v));

    XLatin1StringView_chop(&v, 2);
    XPrintf("  chop(2): size=%lld (期望 3)\n", (long long)XLatin1StringView_size(&v));

    XLatin1StringView_truncate(&v, 0);
    XPrintf("  truncate(0): size=%lld, empty=%d (期望 0,1)\n",
        (long long)XLatin1StringView_size(&v), XLatin1StringView_empty(&v));

    v = XLatin1StringView_create_cstr("HelloWorld");
    XLatin1StringView_chop(&v, 0);
    XPrintf("  chop(0): size=%lld (期望 10)\n", (long long)XLatin1StringView_size(&v));

    XPrintf("\n");
}

/* ==================== 5. 查找测试 ==================== */
static void XLatin1StringViewTest_Find(void)
{
    XPrintf("===== 查找测试 =====\n");
    XLatin1StringView v = XLatin1StringView_create_cstr("abcXYZabc123");

    XPrintf("  indexOf('a',0)=%lld (期望 0)\n", (long long)XLatin1StringView_indexOf_char(&v, 'a', 0, 1));
    XPrintf("  indexOf('a',1)=%lld (期望 6)\n", (long long)XLatin1StringView_indexOf_char(&v, 'a', 1, 1));
    XPrintf("  indexOf('?',0)=%lld (期望 -1)\n", (long long)XLatin1StringView_indexOf_char(&v, '?', 0, 1));

    XPrintf("  lastIndexOf('a',-1)=%lld (期望 6)\n", (long long)XLatin1StringView_lastIndexOf_char(&v, 'a', -1, 1));
    XPrintf("  lastIndexOf('c',-1)=%lld (期望 8)\n", (long long)XLatin1StringView_lastIndexOf_char(&v, 'c', -1, 1));

    XPrintf("  contains('X')=%d (期望 1)\n", XLatin1StringView_contains_char(&v, 'X', 1));
    XPrintf("  contains('?')=%d (期望 0)\n", XLatin1StringView_contains_char(&v, '?', 1));

    XPrintf("  count('a')=%lld (期望 2)\n", (long long)XLatin1StringView_count_char(&v, 'a', 1));
    XPrintf("  count('?')=%lld (期望 0)\n", (long long)XLatin1StringView_count_char(&v, '?', 1));

    XPrintf("  startsWith('a')=%d (期望 1)\n", XLatin1StringView_startsWith_char(&v, 'a', 1));
    XPrintf("  endsWith('3')=%d (期望 1)\n", XLatin1StringView_endsWith_char(&v, '3', 1));

    {
        XLatin1StringView pref = XLatin1StringView_create_cstr("abc");
        XPrintf("  startsWith('abc')=%d (期望 1)\n", XLatin1StringView_startsWith(&v, &pref, 1));
    }
    {
        XLatin1StringView suff = XLatin1StringView_create_cstr("123");
        XPrintf("  endsWith('123')=%d (期望 1)\n", XLatin1StringView_endsWith(&v, &suff, 1));
    }

    XPrintf("\n");
}

/* ==================== 6. 比较测试 ==================== */
static void XLatin1StringViewTest_Compare(void)
{
    XPrintf("===== 比较测试 =====\n");
    XLatin1StringView v1 = XLatin1StringView_create_cstr("Hello");
    XLatin1StringView v2 = XLatin1StringView_create_cstr("Hello");
    XLatin1StringView v3 = XLatin1StringView_create_cstr("HELLO");
    XLatin1StringView v4 = XLatin1StringView_create_cstr("World");

    XPrintf("  equal('Hello','Hello',1)=%d (期望 1)\n", XLatin1StringView_equal(&v1, &v2, 1));
    XPrintf("  equal('Hello','World',1)=%d (期望 0)\n", XLatin1StringView_equal(&v1, &v4, 1));
    XPrintf("  equal('Hello','HELLO',0)=%d (期望 1)\n", XLatin1StringView_equal(&v1, &v3, 0));
    XPrintf("  equal('Hello','HELLO',1)=%d (期望 0)\n", XLatin1StringView_equal(&v1, &v3, 1));

    XPrintf("  compare('Hello','Hello')=%d (期望 0)\n", XLatin1StringView_compare(&v1, &v2, 1));
    XPrintf("  compare('Hello','HELLO',0)=%d (期望 0)\n", XLatin1StringView_compare(&v1, &v3, 0));
    XPrintf("  compare('Hello','World')=%d (期望 <0)\n", XLatin1StringView_compare(&v1, &v4, 1));

    XPrintf("\n");
}

/* ==================== 7. 修剪测试 ==================== */
static void XLatin1StringViewTest_Trim(void)
{
    XPrintf("===== 修剪测试 =====\n");
    XLatin1StringView v1 = XLatin1StringView_create_cstr("  Hello World  ");
    XLatin1StringView v2 = XLatin1StringView_create_cstr("\t\n  \r");
    XLatin1StringView v3 = XLatin1StringView_create_cstr("NoTrim");

    XLatin1StringView t1 = XLatin1StringView_trimmed(&v1);
    XPrintf("  trimmed('  Hello World  '): size=%lld (期望 11)\n", (long long)XLatin1StringView_size(&t1));

    XLatin1StringView t2 = XLatin1StringView_trimmed(&v2);
    XPrintf("  trimmed(whitespace only): size=%lld (期望 0)\n", (long long)XLatin1StringView_size(&t2));

    XLatin1StringView t3 = XLatin1StringView_trimmed(&v3);
    XPrintf("  trimmed('NoTrim'): size=%lld (期望 6)\n", (long long)XLatin1StringView_size(&t3));

    XPrintf("\n");
}

/* ==================== 8. toString 测试 ==================== */
static void XLatin1StringViewTest_ToString(void)
{
    XPrintf("===== toString 测试 =====\n");
    XLatin1StringView v = XLatin1StringView_create_cstr("Hello");
    XString* s = XLatin1StringView_toString(&v);
    XPrintf("  toString(): size=%zu (期望 5)\n", XString_length_base(s));
    XString_delete_base(s);

    /* null view */
    {
        XLatin1StringView nv = XLatin1StringView_create();
        XString* ns = XLatin1StringView_toString(&nv);
        XPrintf("  toString(null view): isNull=%d (期望 1)\n", ns ? XString_isNull(ns) : 1);
        XString_delete_base(ns);
    }

    XPrintf("\n");
}

/* ==================== 9. 迭代器测试 ==================== */
static void XLatin1StringViewTest_Iterator(void)
{
    XPrintf("===== 迭代器测试 =====\n");
    XLatin1StringView v = XLatin1StringView_create_cstr("ABCDE");

    {
        XLatin1StringView_iterator it = XLatin1StringView_begin(&v);
        XLatin1StringView_iterator end = XLatin1StringView_end(&v);
        int count = 0;
        while (!XLatin1StringView_iterator_equality(&it, &end)) { count++; XLatin1StringView_iterator_add(&v, &it); }
        XPrintf("  begin->end count=%d (期望 5)\n", count);
    }
    {
        XLatin1StringView_iterator it = XLatin1StringView_begin(&v);
        XLatin1StringView_iterator end = XLatin1StringView_end(&v);
        XPrintf("  begin->end distance via iteration (期望 5)\n");
    }
    {
        XLatin1StringView_reverse_iterator it = XLatin1StringView_rbegin(&v);
        XLatin1StringView_reverse_iterator end = XLatin1StringView_rend(&v);
        int count = 0;
        while (!XLatin1StringView_reverse_iterator_equality(&it, &end)) { count++; XLatin1StringView_reverse_iterator_add(&v, &it); }
        XPrintf("  rbegin->rend count=%d (期望 5)\n", count);
    }

    XPrintf("\n");
}

/* ==================== 10. Null/空视图边界测试 ==================== */
static void XLatin1StringViewTest_NullEmpty(void)
{
    XPrintf("===== Null/空视图边界测试 =====\n");

    XLatin1StringView nullView = XLatin1StringView_create();
    XLatin1StringView emptyView = XLatin1StringView_create_data("", 0);

    XPrintf("  nullView: isNull=%d, empty=%d, size=%lld (期望 1,1,0)\n",
        XLatin1StringView_isNull(&nullView), XLatin1StringView_empty(&nullView),
        (long long)XLatin1StringView_size(&nullView));
    XPrintf("  emptyView: isNull=%d, empty=%d, size=%lld (期望 0,1,0)\n",
        XLatin1StringView_isNull(&emptyView), XLatin1StringView_empty(&emptyView),
        (long long)XLatin1StringView_size(&emptyView));

    XPrintf("  nullView front()=%d (期望 0)\n", XLatin1StringView_front(&nullView));
    XPrintf("  nullView back()=%d (期望 0)\n", XLatin1StringView_back(&nullView));

    {
        XLatin1StringView sub = XLatin1StringView_first_n(&nullView, 3);
        XPrintf("  nullView first_n(3): size=%lld (期望 0)\n", (long long)XLatin1StringView_size(&sub));
    }

    XPrintf("\n");
}

/* ==================== 全部测试 ==================== */
static void XLatin1StringViewTest_All(void)
{
    XLatin1StringViewTest_Create();
    XLatin1StringViewTest_Access();
    XLatin1StringViewTest_SubView();
    XLatin1StringViewTest_Modify();
    XLatin1StringViewTest_Find();
    XLatin1StringViewTest_Compare();
    XLatin1StringViewTest_Trim();
    XLatin1StringViewTest_ToString();
    XLatin1StringViewTest_Iterator();
    XLatin1StringViewTest_NullEmpty();
}

/* ==================== 菜单入口 ==================== */
void XLatin1StringViewTest(void)
{
    XLatin1StringViewTest_All();
}

void XTestMenu_XLatin1StringViewTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("Latin1StringView(Latin-1字符串视图)");
    XTestMenu_addMenu(root, menu);
    {
        XAction* action = XTestMenu_addAction(menu, "全部测试");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_All);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "构造与创建");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_Create);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "基本访问");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_Access);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "子视图");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_SubView);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "原地修改");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_Modify);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "查找");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_Find);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "比较");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_Compare);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "修剪");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_Trim);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "toString");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_ToString);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "迭代器");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_Iterator);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "Null/空边界");
        XTestMenu_setActionFunction(action, XLatin1StringViewTest_NullEmpty);
    }
}
#endif
