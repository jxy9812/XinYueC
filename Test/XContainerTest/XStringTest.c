#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringList.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"

static void XPrintStr(const char* label, XString* s)
{
	XPrintf("%s", label);
	if (s) { XPrintf_2(s); printf("\n"); }
	else XPrintf("NULL\n");
}

// ==================== 1.创建与初始化 ====================
static void XStringCreateTest(void)
{
	XPrintf("===== 创建与初始化测试 =====\n");
	// create
	{
		XString* s = XString_create();
		XPrintStr("create(): ", s);
		XPrintf("  isEmpty=%s, length=%zu\n", XString_isEmpty_base(s) ? "是" : "否", XString_length_base(s));
		XString_delete_base(s);
	}
	// create_utf8
	{
		XString* s = XString_create_utf8("你好世界");
		XPrintStr("create_utf8('你好世界'): ", s);
		XPrintf("  size=%zu\n", XString_size(s));
		XString_delete_base(s);
	}
	// create_utf8(NULL)
	{
		XString* s = XString_create_utf8(NULL);
		XPrintf("create_utf8(NULL)=%s (期望:非空空字符串)\n", s ? (XString_isEmpty_base(s) ? "空" : "非空") : "NULL");
		XString_delete_base(s);
	}
	// create_copy / create_move
	{
		XString* src = XString_create_utf8("源字符串");
		XString* copy = XString_create_copy(src);
		XString* moved = XString_create_move(src);
		XPrintStr("create_copy: ", copy);
		XPrintStr("create_move后src: ", src);
		XPrintf("  move后src.isEmpty=%s\n", XString_isEmpty_base(src) ? "是" : "否");
		XString_delete_base(copy);
		XString_delete_base(moved);
		XString_delete_base(src);
	}
	// create_fmt_utf8
	{
		XString* s = XString_create_fmt_utf8("val=%d, str=%s", 42, "hello");
		XPrintStr("create_fmt_utf8: ", s);
		XString_delete_base(s);
	}
	// create_with_length_utf8
	{
		XString* s = XString_create_with_length_utf8("abcdef", 3);
		XPrintStr("create_with_length_utf8('abcdef',3): ", s);
		XString_delete_base(s);
	}
	// create_utf16
	{
		uint16_t u16[] = { 'H', 'i', 0 };
		XString* s = XString_create_utf16(u16);
		XPrintStr("create_utf16: ", s);
		XString_delete_base(s);
	}
	// create_gbk / latin1 / utf32 / local
	{
		XString* s = XString_create_gbk("gbk测试");
		XPrintStr("create_gbk: ", s);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_latin1("Latin1");
		XPrintStr("create_latin1: ", s);
		XString_delete_base(s);
	}
	{
		uint32_t u32[] = { 'A', 'B', 'C', 0 };
		XString* s = XString_create_utf32(u32);
		XPrintStr("create_utf32: ", s);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_local("local");
		XPrintStr("create_local: ", s);
		XString_delete_base(s);
	}
	// init
	{
		XString str;
		XString_init(&str);
		XPrintf("init: isEmpty=%s\n", XString_isEmpty_base(&str) ? "是" : "否");
		XString_deinit_base(&str);
	}
	XPrintf("\n");
}

// ==================== 2.容量与大小 ====================
static void XStringCapacityTest(void)
{
	XPrintf("===== 容量与大小测试 =====\n");
	XString* s = XString_create_utf8("你好世界");
	XPrintf("length=%zu, size=%zu, isEmpty=%s\n",
		XString_length_base(s), XString_size(s), XString_isEmpty_base(s) ? "是" : "否");
	XString_reserve(s, 100);
	XPrintf("reserve(100): capacity=%zu\n", XString_capacity_base(s));
	XString_squeeze(s);
	XPrintf("squeeze: capacity=%zu\n", XString_capacity_base(s));
	XString_resize(s, 10);
	XPrintStr("resize(10): ", s);
	XString_resizeForOverwrite(s, 12);
	XPrintf("resizeForOverwrite(12): size=%zu\n", XString_length_base(s));
	XString_truncate(s, 8);
	XPrintStr("truncate(8): ", s);
	XPrintf("maxSize=%zu, isNull=%s, isValidUtf16=%s, isRightToLeft=%s\n",
		XString_maxSize(), XString_isNull(s) ? "是" : "否",
		XString_isValidUtf16(s) ? "是" : "否",
		XString_isRightToLeft(s) ? "是" : "否");
	XString_delete_base(s);
	XPrintf("\n");
}

// ==================== 3.元素访问 ====================
static void XStringAccessTest(void)
{
	XPrintf("===== 元素访问测试 =====\n");
	XString* s = XString_create_utf8("ABCDE");
	XPrintf("at(0)=%c, at(2)=%c, at(4)=%c\n",
		(char)XChar_unicode(XString_at(s, 0)), (char)XChar_unicode(XString_at(s, 2)), (char)XChar_unicode(XString_at(s, 4)));
	XPrintf("front=%c, back=%c\n",
		(char)XChar_unicode(XString_front(s)), (char)XChar_unicode(XString_back(s)));
	XPrintf("unicode[0]=%c, unicode[4]=%c\n",
		(char)XChar_unicode(XString_unicode(s)[0]), (char)XChar_unicode(XString_unicode(s)[4]));
	const uint16_t* u16 = XString_utf16(s);
	XPrintf("utf16[0]=%d, utf16[4]=%d\n", u16[0], u16[4]);
	XChar* dp = XString_data(s);
	XPrintf("data[0]=%c, data[2]=%c\n", (char)XChar_unicode(dp[0]), (char)XChar_unicode(dp[2]));
	XPrintf("constData=%s\n", XString_unicode(s) == dp ? "与data同" : "不同");
	// 空串访问
	{
		XString* e = XString_create();
		XChar c = XString_at(e, 0);
		XPrintf("空串at(0): code=%d\n", XChar_unicode(c));
		XString_delete_base(e);
	}
	XString_delete_base(s);
	XPrintf("\n");
}

// ==================== 4.追加/前置/插入 ====================
static void XStringAppendPrependInsertTest(void)
{
	XPrintf("===== 追加/前置/插入测试 =====\n");
	{
		XString* s = XString_create_utf8("Hello");
		XString_append(s, XString_create_utf8(" World"));
		XPrintStr("append: ", s);
		XString_append_utf8(s, " !");
		XPrintStr("append_utf8: ", s);
		XString_append_char(s, XChar_from('!'));
		XPrintStr("append_char: ", s);
		XString_append_with_length_utf8(s, "extra", 3);
		XPrintStr("append_with_length(3): ", s);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_utf8("World");
		XString_prepend(s, XString_create_utf8("Hello "));
		XPrintStr("prepend: ", s);
		XString_prepend_utf8(s, "!! ");
		XPrintStr("prepend_utf8: ", s);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_utf8("你好");
		XString_insert(s, 1, XString_create_utf8("非常"));
		XPrintStr("insert(1,'非常'): ", s);
		XString_insert_utf8(s, 2, "的");
		XPrintStr("insert_utf8(2,'的'): ", s);
		XString_delete_base(s);
	}
	// push_front / push_back / pop_front / pop_back
	{
		XString* s = XString_create_utf8("BC");
		XString_push_front_base(s, XChar_from('A'));
		XString_push_back_base(s, XChar_from('D'));
		XPrintStr("push_front+push_back: ", s);
		XString_pop_front_base(s);
		XString_pop_back_base(s);
		XPrintStr("pop_front+pop_back: ", s);
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 5.删除 ====================
static void XStringRemoveTest(void)
{
	XPrintf("===== 删除测试 =====\n");
	{
		XString* s = XString_create_utf8("0123456789");
		XString_remove_base(s, 2, 4);
		XPrintStr("remove(2,4): ", s);
		XString_erase_base(s, NULL, NULL);
		XPrintStr("erase(头到尾): ", s);
		XString_delete_base(s);
	}
	// remove_char
	{
		XString* s = XString_create_utf8("axbxcxd");
		XString_remove_char(s, XChar_from('x'), XChar_CaseSensitive);
		XPrintStr("remove_char('x'): ", s);
		XString_delete_base(s);
	}
	// removeAt (别名)
	{
		XString* s = XString_create_utf8("ABCDE");
		XString_remove(s, 1, 2);
		XPrintStr("remove(1,2): ", s);
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 6.替换 ====================
static void XStringReplaceTest(void)
{
	XPrintf("===== 替换测试 =====\n");
	{
		XString* s = XString_create_utf8("Hello World");
		XString_replace(s, XString_create_utf8("World"), XString_create_utf8("XinYueC"), XChar_CaseSensitive);
		XPrintStr("replace('World'->'XinYueC'): ", s);
		XString_replace_utf8(s, "Hello", "Hi", XChar_CaseSensitive);
		XPrintStr("replace_utf8('Hello'->'Hi'): ", s);
		XString_delete_base(s);
	}
	// replace_char
	{
		XString* s = XString_create_utf8("a-b-c-d");
		XString_replace_char(s, XChar_from('-'), XChar_from('/'), XChar_CaseSensitive);
		XPrintStr("replace_char('-','/'): ", s);
		XString_delete_base(s);
	}
	// replace(大小写不敏感)
	{
		XString* s = XString_create_utf8("abcABCabc");
		XString_replace(s, XString_create_utf8("abc"), XString_create_utf8("XYZ"), XChar_CaseInsensitive);
		XPrintStr("replace(abc->XYZ,ci): ", s);
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 7.查找与比较 ====================
static void XStringFindCompareTest(void)
{
	XPrintf("===== 查找与比较测试 =====\n");
	{
		XString* s = XString_create_utf8("ababcababc");
		int64_t idx = XString_indexOf(s, XString_create_utf8("abc"), 0, XChar_CaseSensitive);
		XPrintf("indexOf('abc',0)=%lld\n", (long long)idx);
		idx = XString_indexOf(s, XString_create_utf8("abc"), 3, XChar_CaseSensitive);
		XPrintf("indexOf('abc',3)=%lld\n", (long long)idx);
		idx = XString_lastIndexOf(s, XString_create_utf8("abc"), -1, XChar_CaseSensitive);
		XPrintf("lastIndexOf('abc',-1)=%lld\n", (long long)idx);
		idx = XString_lastIndexOf(s, XString_create_utf8("abc"), 5, XChar_CaseSensitive);
		XPrintf("lastIndexOf('abc',5)=%lld\n", (long long)idx);
		idx = XString_indexOf(s, XString_create_utf8("xyz"), 0, XChar_CaseSensitive);
		XPrintf("indexOf('xyz')=%lld (期望-1)\n", (long long)idx);
		// indexOf_utf8 / lastIndexOf_utf8
		idx = XString_indexOf_utf8(s, "abc", 0, XChar_CaseSensitive);
		XPrintf("indexOf_utf8('abc')=%lld\n", (long long)idx);
		idx = XString_lastIndexOf_utf8(s, "abc", -1, XChar_CaseSensitive);
		XPrintf("lastIndexOf_utf8('abc')=%lld\n", (long long)idx);
		XPrintf("contains('abc')=%s, contains('xyz')=%s\n",
			XString_contains(s, XString_create_utf8("abc"), XChar_CaseSensitive) ? "是" : "否",
			XString_contains(s, XString_create_utf8("xyz"), XChar_CaseSensitive) ? "是" : "否");
		XPrintf("contains_utf8('abc')=%s, contains_utf8('xyz')=%s\n",
			XString_contains_utf8(s, "abc", XChar_CaseSensitive) ? "是" : "否",
			XString_contains_utf8(s, "xyz", XChar_CaseSensitive) ? "是" : "否");
		// indexOf_char / lastIndexOf_char / contains_char
		idx = XString_indexOf_char(s, XChar_from('a'), 0, XChar_CaseSensitive);
		XPrintf("indexOf_char('a')=%lld\n", (long long)idx);
		idx = XString_lastIndexOf_char(s, XChar_from('b'), XChar_CaseSensitive);
		XPrintf("lastIndexOf_char('b')=%lld\n", (long long)idx);
		XPrintf("contains_char('a')=%s, contains_char('z')=%s\n",
			XString_contains_char(s, XChar_from('a'), XChar_CaseSensitive) ? "是" : "否",
			XString_contains_char(s, XChar_from('z'), XChar_CaseSensitive) ? "是" : "否");
		XString_delete_base(s);
	}
	// startsWith / endsWith
	{
		XString* s = XString_create_utf8("Hello World");
		XPrintf("startsWith('Hello')=%s, startsWith('World')=%s\n",
			XString_startsWith(s, XString_create_utf8("Hello"), XChar_CaseSensitive) ? "是" : "否",
			XString_startsWith(s, XString_create_utf8("World"), XChar_CaseSensitive) ? "是" : "否");
		XPrintf("startsWith_utf8('Hello')=%s\n",
			XString_startsWith_utf8(s, "Hello", XChar_CaseSensitive) ? "是" : "否");
		XPrintf("endsWith('World')=%s, endsWith('Hello')=%s\n",
			XString_endsWith(s, XString_create_utf8("World"), XChar_CaseSensitive) ? "是" : "否",
			XString_endsWith(s, XString_create_utf8("Hello"), XChar_CaseSensitive) ? "是" : "否");
		XPrintf("endsWith_utf8('World')=%s\n",
			XString_endsWith_utf8(s, "World", XChar_CaseSensitive) ? "是" : "否");
		XString_delete_base(s);
	}
	// isLower / isUpper / compare / equals / localeAwareCompare / XLess
	{
		XString* a = XString_create_utf8("abc");
		XString* b = XString_create_utf8("ABC");
		XPrintf("isLower(a)=%s, isUpper(b)=%s\n",
			XString_isLower(a) ? "是" : "否",
			XString_isUpper(b) ? "是" : "否");
		XPrintf("compare(a,b)=%d, equals(a,b,cs)=%s, equals(a,b,ci)=%s\n",
			XString_compare(a, b),
			XString_equals(a, b, XChar_CaseSensitive) ? "是" : "否",
			XString_equals(a, b, XChar_CaseInsensitive) ? "是" : "否");
		XPrintf("equals_utf8(a,'abc')=%s\n",
			XString_equals_utf8(a, "abc", XChar_CaseSensitive) ? "是" : "否");
		XPrintf("localeAwareCompare(a,b)=%d, XLess=%s\n",
			XString_localeAwareCompare(a, b),
			XLess_XString(a, b) ? "真" : "假");
		XString_delete_base(a);
		XString_delete_base(b);
	}
	{
		XString* s = XString_create_utf8("Hello");
		XString* t = XString_create_utf8("hello");
		XPrintf("startsWith(ci)=%s, endsWith(ci)=%s\n",
			XString_startsWith(s, t, XChar_CaseSensitive) ? "是" : "否",
			XString_endsWith(s, t, XChar_CaseSensitive) ? "是" : "否");
		XString_delete_base(s);
		XString_delete_base(t);
	}
	XPrintf("\n");
}

// ==================== 8.编码转换 ====================
static void XStringConvertTest(void)
{
	XPrintf("===== 编码转换测试 =====\n");
	{
		XString* s = XString_create_utf8("你好ABC");
		const char* u8 = XString_toUtf8(s);
		size_t u8l = XString_toUtf8_length(s);
		XPrintf("toUtf8=%s (len=%zu)\n", u8, u8l);
		const uint16_t* u16 = XString_toUtf16(s);
		size_t u16l = XString_toUtf16_length(s);
		XPrintf("toUtf16: len=%zu, [0]=(0x%04X,%0X)\n", u16l, u16[0], u16[1]);
		const uint32_t* u32 = XString_toUtf32(s);
		size_t u32l = XString_toUtf32_length(s);
		XPrintf("toUtf32: len=%zu, [0]=0x%06X\n", u32l, u32[0]);
		const char* gbk = XString_toGbk(s);
		size_t gbkl = XString_toGbk_length(s);
		XPrintf("toGbk: len=%zu\n", gbkl);
		const char* loc = XString_toLocal(s);
		size_t locl = XString_toLocal_length(s);
		XPrintf("toLocal: len=%zu\n", locl);
		XString_delete_base(s);
	}
	// toLower / toUpper / toCaseFolded
	{
		XString* s = XString_create_utf8("Hello World");
		XString* lo = XString_toLower(s);
		XPrintStr("toLower: ", lo);
		XString* up = XString_toUpper(s);
		XPrintStr("toUpper: ", up);
		XString* cf = XString_toCaseFolded(s);
		XPrintStr("toCaseFolded: ", cf);
		XString_delete_base(lo);
		XString_delete_base(up);
		XString_delete_base(cf);
		XString_delete_base(s);
	}
	// toHtmlEscaped
	{
		XString* s = XString_create_utf8("<tag> & \"quote\"");
		XString* h = XString_toHtmlEscaped(s);
		XPrintStr("toHtmlEscaped: ", h);
		XString_delete_base(h);
		XString_delete_base(s);
	}
	// simplified / trimmed
	{
		XString* s = XString_create_utf8("  Hello   World  !  ");
		XString* sim = XString_simplified(s);
		XPrintStr("simplified: ", sim);
		XString* tri = XString_trimmed(s);
		XPrintStr("trimmed: ", tri);
		XString_delete_base(sim);
		XString_delete_base(tri);
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 9.数值转换 ====================
static void XStringNumTest(void)
{
	XPrintf("===== 数值转换测试 =====\n");
	{
		XString* s = XString_create_utf8("-12345");
		bool ok;
		XPrintf("toShort=%hd, toInt=%d, toLong=%ld, toLongLong=%lld\n",
			XString_toShort(s, &ok, 10), XString_toInt(s, &ok, 10),
			XString_toLong(s, &ok, 10), XString_toLongLong(s, &ok, 10));
		XString_assign_utf8(s, "FF");
		XPrintf("toInt(hex FF)=%d (期望255)\n", XString_toInt(s, &ok, 16));
		XString_assign_utf8(s, "777");
		XPrintf("toUInt=%u, toULong=%lu, toULongLong=%llu\n",
			XString_toUInt(s, &ok, 10), XString_toULong(s, &ok, 10),
			XString_toULongLong(s, &ok, 10));
		XString_assign_utf8(s, "3.14159");
		XPrintf("toFloat=%.6f, toDouble=%.6lf\n",
			(double)XString_toFloat(s, &ok), XString_toDouble(s, &ok));
		XString_delete_base(s);
	}
	// setNum
	{
		XString* s = XString_create();
		XString_setNum_int(s, -42, 10);
		XPrintStr("setNum_int(-42): ", s);
		XString_setNum_uInt(s, 99, 10);
		XPrintStr("setNum_uInt(99): ", s);
		XString_setNum_long(s, -123456L, 10);
		XPrintStr("setNum_long: ", s);
		XString_setNum_uLong(s, 123456UL, 10);
		XPrintStr("setNum_uLong: ", s);
		XString_setNum_llong(s, -123456789LL, 10);
		XPrintStr("setNum_llong: ", s);
		XString_setNum_uLLong(s, 123456789ULL, 10);
		XPrintStr("setNum_uLLong: ", s);
		XString_setNum_float(s, 3.14f, 'f', 2);
		XPrintStr("setNum_float(3.14): ", s);
		XString_setNum_double(s, 3.1415926535, 'f', 6);
		XPrintStr("setNum_double(pi): ", s);
		XString_setNum_short(s, (short)-7, 10);
		XPrintStr("setNum_short(-7): ", s);
		XString_delete_base(s);
	}
	// number (static)
	{
		XString* n = XString_number_llong(255, 16);
		XPrintStr("number_llong(255,16): ", n);
		XString_delete_base(n);
		n = XString_number_ullong(1024ULL, 10);
		XPrintStr("number_ullong(1024): ", n);
		XString_delete_base(n);
		n = XString_number_double(3.1415, 'f', 3);
		XPrintStr("number_double(3.1415,3): ", n);
		XString_delete_base(n);
	}
	// number宏别名
	{
		XString* n = XString_number_int(42, 10);
		XPrintStr("number_int(42): ", n);
		XString_delete_base(n);
		n = XString_number_uint(42u, 10);
		XPrintStr("number_uint(42): ", n);
		XString_delete_base(n);
	}
	XPrintf("\n");
}

// ==================== 10.子串 ====================
static void XStringSubstringTest(void)
{
	XPrintf("===== 子串操作测试 =====\n");
	{
		XString* s = XString_create_utf8("0123456789");
		XString* l = XString_left(s, 4);
		XPrintStr("left(4): ", l);
		XString_delete_base(l);
		XString* r = XString_right(s, 4);
		XPrintStr("right(4): ", r);
		XString_delete_base(r);
		XString* m = XString_mid(s, 3, 4);
		XPrintStr("mid(3,4): ", m);
		XString_delete_base(m);
		XString* f = XString_first(s, 5);
		XPrintStr("first(5): ", f);
		XString_delete_base(f);
		XString* lst = XString_last(s, 5);
		XPrintStr("last(5): ", lst);
		XString_delete_base(lst);
		XString* sl = XString_sliced(s, 3);
		XPrintStr("sliced(3): ", sl);
		XString_delete_base(sl);
		XString* sl2 = XString_sliced_2(s, 3, 3);
		XPrintStr("sliced(3,3): ", sl2);
		XString_delete_base(sl2);
		XString* ch = XString_chopped(s, 4);
		XPrintStr("chopped(4): ", ch);
		XString_delete_base(ch);
		XString_delete_base(s);
	}
	// slice (原地修改)
	{
		XString* s = XString_create_utf8("0123456789");
		XString_slice(s, 3);
		XPrintStr("slice(3): ", s);
		XString_assign_utf8(s, "0123456789");
		XString_slice_2(s, 2, 5);
		XPrintStr("slice(2,5): ", s);
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 11.原地修改 ====================
static void XStringInplaceTest(void)
{
	XPrintf("===== 原地修改测试 =====\n");
	{
		XString* s = XString_create_utf8("Hello World!");
		XString_chop(s, 6);
		XPrintStr("chop(6): ", s);
		XString_fill(s, XChar_from('*'), -1);
		XPrintStr("fill('*'): ", s);
		XString_assign_utf8(s, "test");
		XString_resize_fill(s, 8, XChar_from('_'));
		XPrintStr("resize_fill(8,'_'): ", s);
		XString_swap(s, XString_create_utf8("SWAPPED"));
		XPrintStr("swap后: ", s);
		XString_delete_base(s);
	}
	// truncate
	{
		XString* s = XString_create_utf8("保留前面");
		XString_truncate(s, 2);
		XPrintStr("truncate(2): ", s);
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 12.拆分与拼接 ====================
static void XStringSplitJoinTest(void)
{
	XPrintf("===== 拆分与拼接测试 =====\n");
	{
		XString* s = XString_create_utf8("a,b,c,d,e");
		XStringList* list = XString_split(s, ",", XChar_CaseSensitive);
		if (list)
		{
			XPrintf("split size=%zu\n", XStringList_size_base(list));
			XStringList_delete_base(list);
		}
	}
	// split_limit
	{
		XString* s = XString_create_utf8("a-b-c-d-e");
		XStringList* list = XString_split_limit_utf8(s, "-", 3, XChar_CaseSensitive);
		if (list)
		{
			XPrintf("split_limit_utf8(3): size=%zu\n", XStringList_size_base(list));
			XStringList_delete_base(list);
		}
	}
	// count / repeated
	{
		XString* s = XString_create_utf8("ababab");
		size_t c = XString_count(s, XString_create_utf8("ab"), XChar_CaseSensitive);
		XPrintf("count('ab')=%zu (期望3)\n", c);
		c = XString_count_utf8(s, "ab", XChar_CaseSensitive);
		XPrintf("count_utf8('ab')=%zu (期望3)\n", c);
		c = XString_count_char(s, XChar_from('a'), XChar_CaseSensitive);
		XPrintf("count_char('a')=%zu (期望3)\n", c);
		XString* r = XString_repeated(s, 2);
		XPrintStr("repeated(2): ", r);
		XString_delete_base(r);
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 13.Qt高级对齐 ====================
static void XStringQtAdvancedTest(void)
{
	XPrintf("===== Qt高级对齐测试 =====\n");
	// section
	{
		XString* s = XString_create_utf8("a|b|c|d|e");
		XString* sec = XString_section(s, XString_create_utf8("|"), 1, 3, 0);
		XPrintStr("section(pipe,1,3): ", sec);
		XString_delete_base(sec);
		sec = XString_section_utf8(s, "|", 0, 0, 0);
		XPrintStr("section_utf8(pipe,0,0): ", sec);
		XString_delete_base(sec);
		sec = XString_section_char(s, XChar_from('|'), 2, 4, 0);
		XPrintStr("section_char(pipe,2,4): ", sec);
		XString_delete_base(sec);
		XString_delete_base(s);
	}
	// arg
	{
		XString* s = XString_create_utf8("%1 and %2");
		XString* r = XString_arg(s, XString_create_utf8("Hello"), 0, XChar_from(' '));
		XPrintStr("arg('Hello'): ", r);
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_utf8("%1");
		XString* r = XString_arg_utf8(s, "World", 0, XChar_from(' '));
		XPrintStr("arg_utf8('World'): ", r);
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_utf8("%1");
		XString* r = XString_arg_char(s, XChar_from('A'), 0, XChar_from(' '));
		XPrintStr("arg_char('A'): ", r);
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_utf8("%1");
		XString* r = XString_arg_llong(s, 42, 0, 10, XChar_from(' '));
		XPrintStr("arg_llong(42): ", r);
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_utf8("%1");
		XString* r = XString_arg_ullong(s, 99ULL, 0, 10, XChar_from(' '));
		XPrintStr("arg_ullong(99): ", r);
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_utf8("%1");
		XString* r = XString_arg_double(s, 3.14, 0, 'f', 2, XChar_from(' '));
		XPrintStr("arg_double(3.14): ", r);
		XString_delete_base(r);
		XString_delete_base(s);
	}
	// leftJustified / rightJustified
	{
		XString* s = XString_create_utf8("abc");
		XString* r = XString_leftJustified(s, 10, XChar_from('-'), true);
		XPrintStr("leftJustified(abc,10,-,true): ", r);
		XString_delete_base(r);
		r = XString_rightJustified(s, 10, XChar_from('-'), true);
		XPrintStr("rightJustified(abc,10,-,true): ", r);
		XString_delete_base(r);
		r = XString_leftJustified(s, 3, XChar_from('-'), true);
		XPrintStr("leftJustified(abc,3): ", r);
		XString_delete_base(r);
		r = XString_leftJustified(s, 3, XChar_from('-'), false);
		XPrintStr("leftJustified(abc,3,false): ", r);
		XString_delete_base(r);
		XString_delete_base(s);
	}
	// setUnicode / setUtf16
	{
		XString* s = XString_create();
		XChar ch[3]; ch[0] = XChar_from('H'); ch[1] = XChar_from('i'); ch[2] = XChar_from(0);
		XString_setUnicode(s, ch, 2);
		XPrintStr("setUnicode: ", s);
		uint16_t u16[] = { 'O', 'K', 0 };
		XString_setUtf16(s, u16, 2);
		XPrintStr("setUtf16: ", s);
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 14.赋值与拷贝 ====================
static void XStringAssignTest(void)
{
	XPrintf("===== 赋值与拷贝测试 =====\n");
	{
		XString* s = XString_create_utf8("初始值");
		XString* src = XString_create_utf8("新值");
		XString_assign(s, src);
		XPrintStr("assign(str): ", s);
		XString_assign_utf8(s, "直接UTF8");
		XPrintStr("assign_utf8: ", s);
		XString_assign_with_length_utf8(s, "前面五个字", 5);
		XPrintStr("assign_with_length(5): ", s);
		XString_assign_fmt_utf8(s, "格式%d", 42);
		XPrintStr("assign_fmt: ", s);
		XString_delete_base(s);
		XString_delete_base(src);
	}
	// 复制 / 移动
	{
		XString* a = XString_create_utf8("AAAAAAAAAA");
		XString* b = XString_create_copy(a);
		XPrintf("深拷贝后: equals=%s\n", XString_equals(a, b, XChar_CaseSensitive) ? "是" : "否");
		XString_append_utf8(a, " appended");
		XPrintStr("修改a后: ", a);
		XPrintStr("b不变: ", b);
		XString_delete_base(a);
		XString_delete_base(b);
	}
	{
		XString* a = XString_create_utf8("将被移动");
		XString* b = XString_create_move(a);
		XPrintStr("移动后b: ", b);
		XPrintf("移动后a.isEmpty=%s\n", XString_isEmpty_base(a) ? "是" : "否");
		XString_delete_base(a);
		XString_delete_base(b);
	}
	XPrintf("\n");
}

// ==================== 15.迭代器 ====================
static void XStringIteratorTest(void)
{
	XPrintf("===== 迭代器测试 =====\n");
	{
		XString* s = XString_create_utf8("ABC");
		XPrintf("正向: ");
		{
			XString_iterator it = XString_begin(s);
			XString_iterator endIt = XString_end(s);
			while (!XString_iterator_equality(&it, &endIt))
			{
				XChar ch = *(XChar*)XString_iterator_data(&it);
				printf("%c ", (char)XChar_unicode(ch));
				XString_iterator_add(s, &it);
			}
		}
		printf("\n");
		XPrintf("反向: ");
		{
			XString_reverse_iterator it = XString_rbegin(s);
			XString_reverse_iterator endIt = XString_rend(s);
			while (!XString_reverse_iterator_equality(&it, &endIt))
			{
				XChar ch = *(XChar*)XString_reverse_iterator_data(&it);
				printf("%c ", (char)XChar_unicode(ch));
				XString_reverse_iterator_add(s, &it);
			}
		}
		printf("\n");
		XString_delete_base(s);
	}
	// 空串迭代
	{
		XString* s = XString_create();
		{
			XString_iterator b = XString_begin(s);
			XString_iterator e = XString_end(s);
			XPrintf("空串迭代器: begin=end? %s\n",
				XString_iterator_equality(&b, &e) ? "是" : "否");
		}
		XString_delete_base(s);
	}
	XPrintf("\n");
}

// ==================== 16.NULL安全 ====================
static void XStringSafetyTest(void)
{
	XPrintf("===== NULL安全测试 =====\n");
	XPrintf("create_utf8(NULL)=%s\n", XString_create_utf8(NULL) ? "非空" : "空");
	XString_at(NULL, 0);
	XPrintf("at(NULL)=不崩溃\n");
	XPrintf("unicode(NULL)=%s\n", XString_unicode(NULL) ? "非空" : "空");
	XPrintf("toUtf8(NULL)=%s\n", XString_toUtf8(NULL) ? "非空" : "空");
	XPrintf("length(NULL)=%zu\n", XString_length_base(NULL));
	XPrintf("isEmpty(NULL)=%s\n", XString_isEmpty_base(NULL) ? "是" : "否");
	XPrintf("isNull(NULL)=%s\n", XString_isNull(NULL) ? "是" : "否");
	XPrintf("compare(NULL)=", XString_compare(NULL, NULL));
	XPrintf("XLess(NULL)=%s\n", XLess_XString(NULL, NULL) ? "真" : "假");
	// 修改操作对NULL
	XPrintf("append(NULL)=%s\n", XString_append(NULL, NULL) ? "是" : "否");
	XPrintf("remove(NULL)=%s\n", XString_remove_base(NULL, 0, 1) ? "是" : "否");
	XPrintf("replace(NULL)=%s\n", XString_replace(NULL, NULL, NULL, XChar_CaseSensitive) ? "是" : "否");
	XPrintf("split(NULL)=%s\n", XString_split(NULL, ",", XChar_CaseSensitive) ? "非空" : "空");
	XPrintf("sliced(NULL)=%s\n", XString_sliced(NULL, 0) ? "非空" : "空");
	XPrintf("section(NULL)=%s\n", XString_section(NULL, NULL, 0, 0, 0) ? "非空" : "空");
	XPrintf("arg(NULL)=%s\n", XString_arg(NULL, NULL, 0, XChar_from(' ')) ? "非空" : "空");
	XPrintf("leftJustified(NULL)=%s, rightJustified(NULL)=%s\n",
		XString_leftJustified(NULL, 5, XChar_from('.'), false) ? "非空" : "空",
		XString_rightJustified(NULL, 5, XChar_from('.'), false) ? "非空" : "空");
	XPrintf("toLower(NULL)=%s, toUpper(NULL)=%s\n",
		XString_toLower(NULL) ? "非空" : "空",
		XString_toUpper(NULL) ? "非空" : "空");
	XPrintf("trimmed(NULL)=%s, simplified(NULL)=%s\n",
		XString_trimmed(NULL) ? "非空" : "空",
		XString_simplified(NULL) ? "非空" : "空");
	XString_reserve(NULL, 10);
	XPrintf("reserve(NULL)=无崩溃\n");
	XString_resize(NULL, 5);
	XPrintf("resize(NULL)=无崩溃\n");
	XString_delete_base(NULL);
	XPrintf("delete_base(NULL)=无崩溃\n");
	XPrintf("\n");
}

// ==================== 总入口 ====================
static void XStringAllTest(void)
{
	XPrintf("========== XString 全部测试开始 ==========\n\n");
	XStringCreateTest();
	XStringCapacityTest();
	XStringAccessTest();
	XStringAppendPrependInsertTest();
	XStringRemoveTest();
	XStringReplaceTest();
	XStringFindCompareTest();
	XStringConvertTest();
	XStringNumTest();
	XStringSubstringTest();
	XStringInplaceTest();
	XStringSplitJoinTest();
	XStringQtAdvancedTest();
	XStringAssignTest();
	XStringIteratorTest();
	XStringSafetyTest();
	XPrintf("========== XString 全部测试结束 ==========\n");
	XCoreApplication_quit();
}

void XMenu_XStringTest(XMenu* root)
{
	XMenu* menu = XMenu_create("字符串(XString)");
	XMenu_addMenu(root, menu);
	{
		XAction* a = XMenu_addAction(menu, "【全部测试】");
		XAction_setAction(a, XStringAllTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "创建与初始化");
		XAction_setAction(a, XStringCreateTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "容量与大小");
		XAction_setAction(a, XStringCapacityTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "元素访问");
		XAction_setAction(a, XStringAccessTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "追加/前置/插入");
		XAction_setAction(a, XStringAppendPrependInsertTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "删除");
		XAction_setAction(a, XStringRemoveTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "替换");
		XAction_setAction(a, XStringReplaceTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "查找与比较");
		XAction_setAction(a, XStringFindCompareTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "编码转换");
		XAction_setAction(a, XStringConvertTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "数值转换");
		XAction_setAction(a, XStringNumTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "子串操作");
		XAction_setAction(a, XStringSubstringTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "原地修改");
		XAction_setAction(a, XStringInplaceTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "拆分与拼接");
		XAction_setAction(a, XStringSplitJoinTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "Qt高级对齐");
		XAction_setAction(a, XStringQtAdvancedTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "赋值与拷贝");
		XAction_setAction(a, XStringAssignTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "迭代器");
		XAction_setAction(a, XStringIteratorTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "NULL安全");
		XAction_setAction(a, XStringSafetyTest);
	}
}
#endif
