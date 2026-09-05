#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringView.h"
#include"XStringList.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"

static void XPrintStr(const char* label, XString* s)
{
	XPrintf("%s", label);
	if (s) { XPrintf_2(s); XPrintf("\n"); }
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
		XString* tmp = XString_create_utf8(" World");
		XString_append(s, tmp);
		XString_delete_base(tmp);
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
		XString* tmp = XString_create_utf8("Hello ");
		XString_prepend(s, tmp);
		XString_delete_base(tmp);
		XPrintStr("prepend: ", s);
		XString_prepend_utf8(s, "!! ");
		XPrintStr("prepend_utf8: ", s);
		XString_delete_base(s);
	}
	{
		XString* s = XString_create_utf8("你好");
		XString* tmp = XString_create_utf8("非常");
		XString_insert(s, 1, tmp);
		XString_delete_base(tmp);
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
		XString* from1 = XString_create_utf8("World");
		XString* to1 = XString_create_utf8("XinYueC");
		XString_replace(s, from1, to1, XChar_CaseSensitive);
		XString_delete_base(from1);
		XString_delete_base(to1);
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
		XString* from2 = XString_create_utf8("abc");
		XString* to2 = XString_create_utf8("XYZ");
		XString_replace(s, from2, to2, XChar_CaseInsensitive);
		XString_delete_base(from2);
		XString_delete_base(to2);
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
		XString* pat = XString_create_utf8("abc");
		int64_t idx = XString_indexOf(s, pat, 0, XChar_CaseSensitive);
		XPrintf("indexOf('abc',0)=%lld\n", (long long)idx);
		idx = XString_indexOf(s, pat, 3, XChar_CaseSensitive);
		XPrintf("indexOf('abc',3)=%lld\n", (long long)idx);
		idx = XString_lastIndexOf(s, pat, -1, XChar_CaseSensitive);
		XPrintf("lastIndexOf('abc',-1)=%lld\n", (long long)idx);
		idx = XString_lastIndexOf(s, pat, 5, XChar_CaseSensitive);
		XPrintf("lastIndexOf('abc',5)=%lld\n", (long long)idx);
		XString* pat2 = XString_create_utf8("xyz");
		idx = XString_indexOf(s, pat2, 0, XChar_CaseSensitive);
		XPrintf("indexOf('xyz')=%lld (期望-1)\n", (long long)idx);
		XString_delete_base(pat2);
		// indexOf_utf8 / lastIndexOf_utf8
		idx = XString_indexOf_utf8(s, "abc", 0, XChar_CaseSensitive);
		XPrintf("indexOf_utf8('abc')=%lld\n", (long long)idx);
		idx = XString_lastIndexOf_utf8(s, "abc", -1, XChar_CaseSensitive);
		XPrintf("lastIndexOf_utf8('abc')=%lld\n", (long long)idx);
		XString* pat3 = XString_create_utf8("abc");
		XString* pat4 = XString_create_utf8("xyz");
		XPrintf("contains('abc')=%s, contains('xyz')=%s\n",
			XString_contains(s, pat3, XChar_CaseSensitive) ? "是" : "否",
			XString_contains(s, pat4, XChar_CaseSensitive) ? "是" : "否");
		XString_delete_base(pat3);
		XString_delete_base(pat4);
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
		XString_delete_base(pat);
		XString_delete_base(s);
	}
	// startsWith / endsWith
	{
		XString* s = XString_create_utf8("Hello World");
		XString* pre = XString_create_utf8("Hello");
		XString* pre2 = XString_create_utf8("World");
		XString* suf = XString_create_utf8("World");
		XString* suf2 = XString_create_utf8("Hello");
		XPrintf("startsWith('Hello')=%s, startsWith('World')=%s\n",
			XString_startsWith(s, pre, XChar_CaseSensitive) ? "是" : "否",
			XString_startsWith(s, pre2, XChar_CaseSensitive) ? "是" : "否");
		XPrintf("startsWith_utf8('Hello')=%s\n",
			XString_startsWith_utf8(s, "Hello", XChar_CaseSensitive) ? "是" : "否");
		XPrintf("endsWith('World')=%s, endsWith('Hello')=%s\n",
			XString_endsWith(s, suf, XChar_CaseSensitive) ? "是" : "否",
			XString_endsWith(s, suf2, XChar_CaseSensitive) ? "是" : "否");
		XPrintf("endsWith_utf8('World')=%s\n",
			XString_endsWith_utf8(s, "World", XChar_CaseSensitive) ? "是" : "否");
		XString_delete_base(pre);
		XString_delete_base(pre2);
		XString_delete_base(suf);
		XString_delete_base(suf2);
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
	/* 有效空字符串允许尚未分配字符缓冲区，比较不得解引用空数据指针。 */
	{
		XString* firstEmpty = XString_create();
		XString* secondEmpty = XString_create();
		bool equals = firstEmpty && secondEmpty &&
			XString_equals(firstEmpty, secondEmpty, XChar_CaseSensitive);
		XPrintf("两个未分配缓冲区的空字符串相等=%s\n", equals ? "通过" : "失败");
		XString_delete_base(firstEmpty);
		XString_delete_base(secondEmpty);
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
		XString* swp = XString_create_utf8("SWAPPED");
		XString_swap(s, swp);
		XString_delete_base(swp);
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
		XString_delete_base(s);
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
		XString_delete_base(s);
	}
	// count / repeated
	{
		XString* s = XString_create_utf8("ababab");
		XString* pat = XString_create_utf8("ab");
		size_t c = XString_count(s, pat, XChar_CaseSensitive);
		XString_delete_base(pat);
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
		XString* sep = XString_create_utf8("|");
		XString* sec = XString_section(s, sep, 1, 3, 0);
		XString_delete_base(sep);
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
		XString* argVal = XString_create_utf8("Hello");
		XString* r = XString_arg(s, argVal, 0, XChar_from(' '));
		XString_delete_base(argVal);
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
				XPrintf("%c ", (char)XChar_unicode(ch));
				XString_iterator_add(s, &it);
			}
		}
		XPrintf("\n");
		XPrintf("反向: ");
		{
			XString_reverse_iterator it = XString_rbegin(s);
			XString_reverse_iterator endIt = XString_rend(s);
			while (!XString_reverse_iterator_equality(&it, &endIt))
			{
				XChar ch = *(XChar*)XString_reverse_iterator_data(&it);
				XPrintf("%c ", (char)XChar_unicode(ch));
				XString_reverse_iterator_add(s, &it);
			}
		}
		XPrintf("\n");
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
	{
		XString* t = XString_create_utf8(NULL);
		XPrintf("create_utf8(NULL)=%s\n", t ? "非空" : "空");
		XString_delete_base(t);
	}
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
	{
		XString* t = XString_split(NULL, ",", XChar_CaseSensitive);
		XPrintf("split(NULL)=%s\n", t ? "非空" : "空");
		XStringList_delete_base(t);
	}
	{
		XString* t = XString_sliced(NULL, 0);
		XPrintf("sliced(NULL)=%s\n", t ? "非空" : "空");
		XString_delete_base(t);
	}
	{
		XString* t = XString_section(NULL, NULL, 0, 0, 0);
		XPrintf("section(NULL)=%s\n", t ? "非空" : "空");
		XString_delete_base(t);
	}
	{
		XString* t = XString_arg(NULL, NULL, 0, XChar_from(' '));
		XPrintf("arg(NULL)=%s\n", t ? "非空" : "空");
		XString_delete_base(t);
	}
	{
		XString* t1 = XString_leftJustified(NULL, 5, XChar_from('.'), false);
		XString* t2 = XString_rightJustified(NULL, 5, XChar_from('.'), false);
		XPrintf("leftJustified(NULL)=%s, rightJustified(NULL)=%s\n",
			t1 ? "非空" : "空",
			t2 ? "非空" : "空");
		XString_delete_base(t1);
		XString_delete_base(t2);
	}
	{
		XString* t1 = XString_toLower(NULL);
		XString* t2 = XString_toUpper(NULL);
		XPrintf("toLower(NULL)=%s, toUpper(NULL)=%s\n",
			t1 ? "非空" : "空",
			t2 ? "非空" : "空");
		XString_delete_base(t1);
		XString_delete_base(t2);
	}
	{
		XString* t1 = XString_trimmed(NULL);
		XString* t2 = XString_simplified(NULL);
		XPrintf("trimmed(NULL)=%s, simplified(NULL)=%s\n",
			t1 ? "非空" : "空",
			t2 ? "非空" : "空");
		XString_delete_base(t1);
		XString_delete_base(t2);
	}
	XString_reserve(NULL, 10);
	XPrintf("reserve(NULL)=无崩溃\n");
	XString_resize(NULL, 5);
	XPrintf("resize(NULL)=无崩溃\n");
	XString_delete_base(NULL);
	XPrintf("delete_base(NULL)=无崩溃\n");
	XPrintf("\n");
}

// ==================== 总入口 ====================
/* ==================== 委托给 XStringView 验证 ==================== */
static void XStringViewDelegationTest(void)
{
    XPrintf("\n===== XString -> XStringView 委托验证 =====\n");

    /* 准备测试数据 */
    XString* str = XString_create_utf8("  Hello World!  ");
    XString* empty = XString_create();
    XString* num = XString_create_utf8("  -1234  ");
    XString* hex = XString_create_utf8("FF");
    XString* fp = XString_create_utf8("3.14159");
    XString* a = XString_create_utf8("Hello");
    XString* b = XString_create_utf8("hello");
    XString* search = XString_create_utf8("abXcdXef");
    XString* sub = XString_create_utf8("X");

    /* 直接创建 View 作为参考 */
    XStringView refView = XStringView_create_string(str);
    XStringView emptyView = XStringView_create_string(empty);
    XStringView searchView = XStringView_create_string(search);
    XStringView subView = XStringView_create_string(sub);

    /* ---- left 委托验证 ---- */
    {
        XString* l = XString_left(str, 5);
        XStringView lv = XStringView_first_n(&refView, 5);
        bool ok = (l && XString_length_base(l) == (size_t)lv.m_size);
        XPrintf("  left(5) 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", l ? XString_length_base(l) : 0, (long long)lv.m_size);
        XString_delete_base(l);
    }
    /* ---- right 委托验证 ---- */
    {
        XString* r = XString_right(str, 6);
        XStringView rv = XStringView_last_n(&refView, 6);
        bool ok = (r && XString_length_base(r) == (size_t)rv.m_size);
        XPrintf("  right(6) 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", r ? XString_length_base(r) : 0, (long long)rv.m_size);
        XString_delete_base(r);
    }
    /* ---- mid 委托验证 ---- */
    {
        XString* m = XString_mid(str, 2, 5);
        XStringView mv = XStringView_sliced_2(&refView, 2, 5);
        bool ok = (m && XString_length_base(m) == (size_t)mv.m_size);
        XPrintf("  mid(2,5) 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", m ? XString_length_base(m) : 0, (long long)mv.m_size);
        XString_delete_base(m);
    }
    /* ---- sliced 委托验证 ---- */
    {
        XString* s = XString_sliced(str, 2);
        XStringView sv = XStringView_sliced(&refView, 2);
        bool ok = (s && XString_length_base(s) == (size_t)sv.m_size);
        XPrintf("  sliced(2) 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", s ? XString_length_base(s) : 0, (long long)sv.m_size);
        XString_delete_base(s);
    }
    /* ---- first 委托验证 ---- */
    {
        XString* f = XString_first(str, 5);
        XStringView fv = XStringView_first_n(&refView, 5);
        bool ok = (f && XString_length_base(f) == (size_t)fv.m_size);
        XPrintf("  first(5) 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", f ? XString_length_base(f) : 0, (long long)fv.m_size);
        XString_delete_base(f);
    }
    /* ---- last 委托验证 ---- */
    {
        XString* l = XString_last(str, 6);
        XStringView lv = XStringView_last_n(&refView, 6);
        bool ok = (l && XString_length_base(l) == (size_t)lv.m_size);
        XPrintf("  last(6) 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", l ? XString_length_base(l) : 0, (long long)lv.m_size);
        XString_delete_base(l);
    }
    /* ---- chopped 委托验证 ---- */
    {
        XString* c = XString_chopped(str, 3);
        XStringView cv = XStringView_chopped(&refView, 3);
        bool ok = (c && XString_length_base(c) == (size_t)cv.m_size);
        XPrintf("  chopped(3) 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", c ? XString_length_base(c) : 0, (long long)cv.m_size);
        XString_delete_base(c);
    }
    /* ---- trimmed 委托验证 ---- */
    {
        XString* t = XString_trimmed(str);
        XStringView tv = XStringView_trimmed(&refView);
        bool ok = (t && XString_length_base(t) == (size_t)tv.m_size);
        XPrintf("  trimmed() 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", t ? XString_length_base(t) : 0, (long long)tv.m_size);
        XString_delete_base(t);
    }
    /* ---- trimmed(empty) 委托验证 ---- */
    {
        XString* t = XString_trimmed(empty);
        XStringView tv = XStringView_trimmed(&emptyView);
        bool ok = (t && XString_length_base(t) == (size_t)tv.m_size);
        XPrintf("  trimmed(empty) 委托验证: %s (size=%zu, 期望 %lld)\n",
            ok ? "通过" : "失败", t ? XString_length_base(t) : 0, (long long)tv.m_size);
        XString_delete_base(t);
    }
    /* ---- compare 委托验证 ---- */
    {
        int32_t cmp = XString_compare(str, str);
        int32_t cmp_ref = XStringView_compare(&refView, &refView, 1);
        XPrintf("  compare(self,self) 委托验证: %s (got %d, 期望 %d)\n",
            cmp == cmp_ref ? "通过" : "失败", cmp, cmp_ref);
    }
    /* ---- indexOf 委托验证 ---- */
    {
        int64_t pos = XString_indexOf(search, sub, 0, XChar_CaseSensitive);
        int64_t pos_ref = XStringView_indexOf(&searchView, &subView, 0, 1);
        XPrintf("  indexOf('X') 委托验证: %s (got %lld, 期望 %lld)\n",
            pos == pos_ref ? "通过" : "失败", (long long)pos, (long long)pos_ref);
    }
    /* ---- lastIndexOf 委托验证 ---- */
    {
        int64_t pos = XString_lastIndexOf(search, sub, -1, XChar_CaseSensitive);
        int64_t pos_ref = XStringView_lastIndexOf(&searchView, &subView, -1, 1);
        XPrintf("  lastIndexOf('X') 委托验证: %s (got %lld, 期望 %lld)\n",
            pos == pos_ref ? "通过" : "失败", (long long)pos, (long long)pos_ref);
    }
    /* ---- contains 委托验证 ---- */
    {
        bool c = XString_contains(search, sub, XChar_CaseSensitive);
        bool c_ref = XStringView_contains(&searchView, &subView, 1);
        XPrintf("  contains('X') 委托验证: %s (got %d, 期望 %d)\n",
            c == c_ref ? "通过" : "失败", c, c_ref);
    }
    /* ---- startsWith 委托验证 ---- */
    {
        XString* prefix = XString_create_utf8("  He");
        bool sw = XString_startsWith(str, prefix, XChar_CaseSensitive);
        XStringView pv = XStringView_create_string(prefix);
        bool sw_ref = XStringView_startsWith(&refView, &pv, 1);
        XPrintf("  startsWith('  He') 委托验证: %s (got %d, 期望 %d)\n",
            sw == sw_ref ? "通过" : "失败", sw, sw_ref);
        XString_delete_base(prefix);
    }
    /* ---- endsWith 委托验证 ---- */
    {
        XString* suffix = XString_create_utf8("!  ");
        bool ew = XString_endsWith(str, suffix, XChar_CaseSensitive);
        XStringView suv = XStringView_create_string(suffix);
        bool ew_ref = XStringView_endsWith(&refView, &suv, 1);
        XPrintf("  endsWith('!  ') 委托验证: %s (got %d, 期望 %d)\n",
            ew == ew_ref ? "通过" : "失败", ew, ew_ref);
        XString_delete_base(suffix);
    }
    /* ---- count 委托验证 ---- */
    {
        size_t cnt = XString_count(search, sub, XChar_CaseSensitive);
        int64_t cnt_ref = XStringView_count(&searchView, &subView, 1);
        XPrintf("  count('X') 委托验证: %s (got %zu, 期望 %lld)\n",
            (int64_t)cnt == cnt_ref ? "通过" : "失败", cnt, (long long)cnt_ref);
    }
    /* ---- indexOf_char 委托验证 ---- */
    {
        int64_t pos = XString_indexOf_char(search, 'X', 0, XChar_CaseSensitive);
        int64_t pos_ref = XStringView_indexOf_char(&searchView, 'X', 0, 1);
        XPrintf("  indexOf_char('X') 委托验证: %s (got %lld, 期望 %lld)\n",
            pos == pos_ref ? "通过" : "失败", (long long)pos, (long long)pos_ref);
    }
    /* ---- lastIndexOf_char 委托验证 ---- */
    {
        int64_t pos = XString_lastIndexOf_char(search, 'X', XChar_CaseSensitive);
        int64_t pos_ref = XStringView_lastIndexOf_char(&searchView, 'X', -1, 1);
        XPrintf("  lastIndexOf_char('X') 委托验证: %s (got %lld, 期望 %lld)\n",
            pos == pos_ref ? "通过" : "失败", (long long)pos, (long long)pos_ref);
    }
    /* ---- contains_char 委托验证 ---- */
    {
        bool c = XString_contains_char(search, 'X', XChar_CaseSensitive);
        bool c_ref = XStringView_contains_char(&searchView, 'X', 1);
        XPrintf("  contains_char('X') 委托验证: %s (got %d, 期望 %d)\n",
            c == c_ref ? "通过" : "失败", c, c_ref);
    }
    /* ---- count_char 委托验证 ---- */
    {
        size_t cnt = XString_count_char(search, 'X', XChar_CaseSensitive);
        int64_t cnt_ref = XStringView_count_char(&searchView, 'X', 1);
        XPrintf("  count_char('X') 委托验证: %s (got %zu, 期望 %lld)\n",
            (int64_t)cnt == cnt_ref ? "通过" : "失败", cnt, (long long)cnt_ref);
    }
    /* ---- toInt 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        int v1 = XString_toInt(num, &ok1, 10);
        XStringView nv = XStringView_create_string(num);
        int v2 = XStringView_toInt(&nv, &ok2, 10);
        XPrintf("  toInt 委托验证: %s (got %d ok=%d, 期望 %d ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toLongLong 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        long long v1 = XString_toLongLong(num, &ok1, 10);
        XStringView nv = XStringView_create_string(num);
        int64_t v2 = XStringView_toLongLong(&nv, &ok2, 10);
        XPrintf("  toLongLong 委托验证: %s (got %lld ok=%d, 期望 %lld ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败",
            (long long)v1, ok1, (long long)v2, ok2);
    }
    /* ---- toDouble 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        double v1 = XString_toDouble(fp, &ok1);
        XStringView fv = XStringView_create_string(fp);
        double v2 = XStringView_toDouble(&fv, &ok2);
        XPrintf("  toDouble 委托验证: %s (got %f ok=%d, 期望 %f ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toFloat 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        float v1 = XString_toFloat(fp, &ok1);
        XStringView fv = XStringView_create_string(fp);
        float v2 = XStringView_toFloat(&fv, &ok2);
        XPrintf("  toFloat 委托验证: %s (got %f ok=%d, 期望 %f ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toLong 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        long v1 = XString_toLong(num, &ok1, 10);
        XStringView nv = XStringView_create_string(num);
        long v2 = XStringView_toLong(&nv, &ok2, 10);
        XPrintf("  toLong 委托验证: %s (got %ld ok=%d, 期望 %ld ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toULong 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        unsigned long v1 = XString_toULong(hex, &ok1, 16);
        XStringView hv = XStringView_create_string(hex);
        unsigned long v2 = XStringView_toULong(&hv, &ok2, 16);
        XPrintf("  toULong(hex,16) 委托验证: %s (got %lu ok=%d, 期望 %lu ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toUInt 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        unsigned int v1 = XString_toUInt(hex, &ok1, 16);
        XStringView hv = XStringView_create_string(hex);
        unsigned int v2 = XStringView_toUInt(&hv, &ok2, 16);
        XPrintf("  toUInt(hex,16) 委托验证: %s (got %u ok=%d, 期望 %u ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toShort 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        short v1 = XString_toShort(num, &ok1, 10);
        XStringView nv = XStringView_create_string(num);
        short v2 = XStringView_toShort(&nv, &ok2, 10);
        XPrintf("  toShort 委托验证: %s (got %d ok=%d, 期望 %d ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", (int)v1, ok1, (int)v2, ok2);
    }
    /* ---- toUShort 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        unsigned short v1 = XString_toUShort(hex, &ok1, 16);
        XStringView hv = XStringView_create_string(hex);
        unsigned short v2 = XStringView_toUShort(&hv, &ok2, 16);
        XPrintf("  toUShort(hex,16) 委托验证: %s (got %u ok=%d, 期望 %u ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", (unsigned)v1, ok1, (unsigned)v2, ok2);
    }
    /* ---- toULongLong 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        unsigned long long v1 = XString_toULongLong(hex, &ok1, 16);
        XStringView hv = XStringView_create_string(hex);
        uint64_t v2 = XStringView_toULongLong(&hv, &ok2, 16);
        XPrintf("  toULongLong(hex,16) 委托验证: %s (got %llu ok=%d, 期望 %llu ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败",
            (unsigned long long)v1, ok1, (unsigned long long)v2, ok2);
    }
    /* ---- NULL 安全验证 ---- */
    {
        bool ok = false;
        int v = XString_toInt(NULL, &ok, 10);
        XPrintf("  toInt(NULL) 委托验证: %s (got %d ok=%d, 期望 0 ok=0)\n",
            (v == 0 && ok == false) ? "通过" : "失败", v, ok);
    }
    {
        int64_t pos = XString_indexOf(NULL, sub, 0, XChar_CaseSensitive);
        XPrintf("  indexOf(NULL) 委托验证: %s (got %lld, 期望 -1)\n",
            pos == -1 ? "通过" : "失败", (long long)pos);
    }
    {
        bool c = XString_contains(NULL, sub, XChar_CaseSensitive);
        XPrintf("  contains(NULL) 委托验证: %s (got %d, 期望 0)\n",
            c == false ? "通过" : "失败", c);
    }

    /* 清理 */
    XString_delete_base(str);
    XString_delete_base(empty);
    XString_delete_base(num);
    XString_delete_base(hex);
    XString_delete_base(fp);
    XString_delete_base(a);
    XString_delete_base(b);
    XString_delete_base(search);
    XString_delete_base(sub);
}



static void XStringAllTest(void)
{
    XPrintf("========== XString全部测试开始 ==========\n\n");
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
    XStringViewDelegationTest();
    XPrintf("========== XString全部测试结束 ==========\n");
    //XCoreApplication_quit();
}

void XTestMenu_XStringTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("字符串(XString)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* a = XTestMenu_addAction(menu, "【全部测试】");
		XTestMenu_setActionFunction(a, XStringAllTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "创建与初始化");
		XTestMenu_setActionFunction(a, XStringCreateTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "容量与大小");
		XTestMenu_setActionFunction(a, XStringCapacityTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "元素访问");
		XTestMenu_setActionFunction(a, XStringAccessTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "追加/前置/插入");
		XTestMenu_setActionFunction(a, XStringAppendPrependInsertTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "删除");
		XTestMenu_setActionFunction(a, XStringRemoveTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "替换");
		XTestMenu_setActionFunction(a, XStringReplaceTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "查找与比较");
		XTestMenu_setActionFunction(a, XStringFindCompareTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "编码转换");
		XTestMenu_setActionFunction(a, XStringConvertTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "数值转换");
		XTestMenu_setActionFunction(a, XStringNumTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "子串操作");
		XTestMenu_setActionFunction(a, XStringSubstringTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "原地修改");
		XTestMenu_setActionFunction(a, XStringInplaceTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "拆分与拼接");
		XTestMenu_setActionFunction(a, XStringSplitJoinTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "Qt高级对齐");
		XTestMenu_setActionFunction(a, XStringQtAdvancedTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "赋值与拷贝");
		XTestMenu_setActionFunction(a, XStringAssignTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "迭代器");
		XTestMenu_setActionFunction(a, XStringIteratorTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "NULL安全");
		XTestMenu_setActionFunction(a, XStringSafetyTest);
	}
}
#endif
