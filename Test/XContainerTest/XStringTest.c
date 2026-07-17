#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringList.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//static void XStringTest();
#if XString_ON

// 前向声明：避免函数在使用前未声明
void XStringIteratorTest(void);
void XStringNumTest(void);
void XStringOperateTest(void);
void XStringCopyTest(void);
void XStringSlicedTest(void);
void XStringInplaceTest(void);
void XStringConvertTest(void);
void XStringStaticTest(void);
void XStringCharOpsTest(void);
void XStringUtf16Test(void);
void XStringQtAlignTest(void);
static void XFor_each_XString(XString* str, void* args)
{
	XPrintf_2(str);
	printf("\n");
}
static void XFor_each_XChar(XChar* ch, void* args)
{
	XPrintf_5(ch);
	XPrintf_3("\n");
}
//迭代器测试
void XStringIteratorTest()
{
	while (true)
	{
		XPrintf_3("XString 正向迭代器测试\n");
		XString* str = XString_create_utf8("正向迭代器");
		XString_iterator_for_each(str, XFor_each_XChar, NULL);
		XPrintf_3("XString 反向迭代器测试\n");
		XString_assign_utf8(str, "反向迭代器");
		XString_reverse_iterator_for_each(str, XFor_each_XChar, NULL);
		XString_delete_base(str);
	}

	XCoreApplication_quit();
}
//字符串 整数 测试
void XStringNumTest()
{
	//while (true)
	{
		XPrintf_3("XString 字符串转整数测试\n");
		XString* str = XString_create_utf8(NULL);
		XString_assign_utf8(str, "66666");
		XPrintf("整数:%d\n", XString_toLongLong(str, NULL, 10));
		XPrintf("整数:%.2lf\n", XString_toDouble(str, NULL));
		XPrintf_3("XString 整数转字符串测试\n");
		XString_setNum_int(str, -6666699, 2);
		XPrintf_2(str);
		XPrintf_3("\n");
		XString_setNum_double(str, 66666.153456, 'f', 2);
		XPrintf_2(str);

		XString_delete_base(str);
	}

	XCoreApplication_quit();
}
void XStringOperateTest()
{
	while (true)
	{
		XPrintf_3("XString 字符串操作测试\n");

		XString* str = XString_create_utf8("这是一个字符串操作例子");
		{
			XString* mid = XString_mid(str, 2, 4);
			if (mid)
			{
				XPrintf_2(mid);
				printf("\n");
			}
			XString_delete_base(mid);
		}
		{
			int64_t index = XString_index_of_utf8(str, "例子", 0, XChar_CaseInsensitive);
			if (index != -1)
				XPrintf("找到了,index:%d\n", index);
		}
		{
			if (XString_replace_utf8(str, "一", "1", XChar_CaseInsensitive))
			{
				XPrintf_2(str);
				printf("\n");
			}
		}
		XString_delete_base(str);
	}
	XCoreApplication_quit();
}
void XStringCopyTest()
{
	while(true)
	{
		XString* str = XString_create_utf8("这是一个字符串拷贝引用测试");
		XPrintf_2(str); printf("\n");
		XString* copy = XString_create_copy(str);
		//XPrintf_2(copy); printf("\n");

		XString_append_utf8(str,"追加测试");
		XString_assign_utf8(str, "写时才复制");
		XPrintf_2(str); printf("\n");
		XPrintf_2(copy); printf("\n");

		XString_delete_base(str);
		XString_delete_base(copy);
	}
	XCoreApplication_quit();
}
void XStringTest()
{
	//char* utf8 = "1234567890";
	//XChar buff [100];
	//XChar_fromUtf8Stream(utf8,0,buff,100);
	//for (size_t i = 0; i < 10; i++)
	//{
	//	printf("utf8:%d utf16:%d %d\n",utf8[i],buff[i].code, buff[i].code&(~0x80));//c & 0x80
	//}
	//
	//return;
	//while(true)
	{
#if XString_ON
	XPrintf_3("XString 测试\n");
	{
		XString* str = XString_create_utf8("你好-在吗");
		if (str)
		{
			XStringList* v = XString_split_utf8(str, "-",XChar_CaseInsensitive);
			if (v)
			{
				XStringList_iterator_for_each(v, XFor_each_XString, NULL);
				XStringList_delete_base(v);
			}
		}
		/*XString_append_utf8(str,"1223dasdas31d32as1d23sa1d32sa123d1sa23d132sad");
		XPrintf_2(str);
		printf("\n");*/
		XString_delete_base(str);
	}
	XCoreApplication_quit();
	return;
	
	XString* str = XString_create_utf8("你好");
	XPrintf_2(str);
	XString_append_utf8(str, "111");
	//XString_push_front_base(str, '#');
	//XString_push_back_base(str, '!');
	XString_insert_utf8(str,1,"12121ni_");
	XPrintf_2(str);
	XString_pop_front_base(str);
	XString_pop_back_base(str);
	XString_assign_utf8(str,"你好吗！");
	XString_clear_base(str);
	//XString_append_utf8(str, "  666\r\n");
	//printf("字符数量%d\n", XString_size(str));
	//XString_assign_utf8(str, "草泥马");
	XPrintf("字符数量%d\n", XString_size_base(str));
	XString_append_utf8(str, "你好呀");

	//XString_erase_base(str, 3, 3);
	XPrintf("字符数量%d\n", XString_size_base(str));
	//XString_erase_base(str, 0, 4);
	XPrintf_2(str);
	XString_delete_base(str);
#endif
	}
	XCoreApplication_quit();
}

// ==================== Qt 6.8 对齐 API 测试 ====================

// 测试 sliced/first/last/chopped 子串操作
void XStringSlicedTest()
{
	XPrintf_3("XString 子串操作测试 (sliced/first/last/chopped)\n");

	XString* str = XString_create_utf8("Hello, World!");
	{
		// 测试 sliced(pos)
		XString* s1 = XString_sliced(str, 7);
		XPrintf("sliced(7): "); XPrintf_2(s1); printf("\n");
		XString_delete_base(s1);
	}
	{
		// 测试 sliced(pos, n)
		XString* s2 = XString_sliced_2(str, 0, 5);
		XPrintf("sliced(0,5): "); XPrintf_2(s2); printf("\n");
		XString_delete_base(s2);
	}
	{
		// 测试 first(n)
		XString* s3 = XString_first(str, 5);
		XPrintf("first(5): "); XPrintf_2(s3); printf("\n");
		XString_delete_base(s3);
	}
	{
		// 测试 last(n)
		XString* s4 = XString_last(str, 6);
		XPrintf("last(6): "); XPrintf_2(s4); printf("\n");
		XString_delete_base(s4);
	}
	{
		// 测试 chopped(len)
		XString* s5 = XString_chopped(str, 1);
		XPrintf("chopped(1): "); XPrintf_2(s5); printf("\n");
		XString_delete_base(s5);
	}
	XString_delete_base(str);

	// 测试中文字符串
	XString* cstr = XString_create_utf8("你好世界");
	{
		XString* s = XString_sliced(cstr, 2);
		XPrintf("中文sliced(2): "); XPrintf_2(s); printf("\n");
		XString_delete_base(s);
	}
	{
		XString* s = XString_first(cstr, 2);
		XPrintf("中文first(2): "); XPrintf_2(s); printf("\n");
		XString_delete_base(s);
	}
	XString_delete_base(cstr);

	XCoreApplication_quit();
}

// 测试 chop/fill/squeeze/slice 原地修改
void XStringInplaceTest()
{
	XPrintf_3("XString 原地修改测试 (chop/fill/squeeze/slice)\n");

	XString* str = XString_create_utf8("Hello, World!");
	{
		// 测试 chop
		XString* s1 = XString_create_copy(str);
		XString_chop(s1, 6);
		XPrintf("chop(6): "); XPrintf_2(s1); printf("\n");
		XString_delete_base(s1);
	}
	{
		// 测试 fill
		XString* s2 = XString_create_utf8("");
		XString_fill(s2, XChar_from('A'), 5);
		XPrintf("fill(A,5): "); XPrintf_2(s2); printf("\n");
		XString_delete_base(s2);
	}
	{
		// 测试 squeeze
		XString* s3 = XString_create_utf8("Hi");
		XString_reserve(s3, 100);
		XPrintf("squeeze前capacity: %zu\n", XString_capacity_base(s3));
		XString_squeeze(s3);
		XPrintf("squeeze后capacity: %zu\n", XString_capacity_base(s3));
		XString_delete_base(s3);
	}
	{
		// 测试 slice(pos)
		XString* s4 = XString_create_copy(str);
		XString_slice(s4, 7);
		XPrintf("slice(7): "); XPrintf_2(s4); printf("\n");
		XString_delete_base(s4);
	}
	{
		// 测试 slice(pos, n)
		XString* s5 = XString_create_copy(str);
		XString_slice_2(s5, 0, 5);
		XPrintf("slice(0,5): "); XPrintf_2(s5); printf("\n");
		XString_delete_base(s5);
	}
	{
		// 测试 resize_fill
		XString* s6 = XString_create_utf8("Hi");
		XString_resize_fill(s6, 10, XChar_from('*'));
		XPrintf("resize_fill(10,*): "); XPrintf_2(s6); printf("\n");
		XString_delete_base(s6);
	}
	{
		// 测试 swap
		XString* a = XString_create_utf8("AAA");
		XString* b = XString_create_utf8("BBB");
		XString_swap(a, b);
		XPrintf("swap: a="); XPrintf_2(a); XPrintf(", b="); XPrintf_2(b); printf("\n");
		XString_delete_base(a);
		XString_delete_base(b);
	}
	XString_delete_base(str);

	XCoreApplication_quit();
}

// 测试 toCaseFolded/toHtmlEscaped/simplified/repeated 转换函数
void XStringConvertTest()
{
	XPrintf_3("XString 转换函数测试 (toCaseFolded/toHtmlEscaped/simplified/repeated)\n");

	{
		// 测试 toCaseFolded
		XString* s1 = XString_create_utf8("Hello WORLD");
		XString* folded = XString_toCaseFolded(s1);
		XPrintf("toCaseFolded: "); XPrintf_2(folded); printf("\n");
		XString_delete_base(folded);
		XString_delete_base(s1);
	}
	{
		// 测试 toHtmlEscaped
		XString* s2 = XString_create_utf8("<div>Hello & \"World\" 'test'</div>");
		XString* escaped = XString_toHtmlEscaped(s2);
		XPrintf("toHtmlEscaped: "); XPrintf_2(escaped); printf("\n");
		XString_delete_base(escaped);
		XString_delete_base(s2);
	}
	{
		// 测试 leftJustified
		XString* s3 = XString_create_utf8("Hi");
		XString* lj = XString_leftJustified(s3, 10, XChar_from('.'), false);
		XPrintf("leftJustified(10,.): "); XPrintf_2(lj); printf("\n");
		XString_delete_base(lj);
		XString_delete_base(s3);
	}
	{
		// 测试 rightJustified
		XString* s4 = XString_create_utf8("Hi");
		XString* rj = XString_rightJustified(s4, 10, XChar_from('.'), false);
		XPrintf("rightJustified(10,.): "); XPrintf_2(rj); printf("\n");
		XString_delete_base(rj);
		XString_delete_base(s4);
	}
	{
		// 测试 simplified
		XString* s5 = XString_create_utf8("  Hello   World  \t\n  ");
		XString* sim = XString_simplified(s5);
		XPrintf("simplified: "); XPrintf_2(sim); printf("\n");
		XString_delete_base(sim);
		XString_delete_base(s5);
	}
	{
		// 测试 repeated
		XString* s6 = XString_create_utf8("Ha");
		XString* rep = XString_repeated(s6, 3);
		XPrintf("repeated(3): "); XPrintf_2(rep); printf("\n");
		XString_delete_base(rep);
		XString_delete_base(s6);
	}

	XCoreApplication_quit();
}

// 测试 count/number/static API
void XStringStaticTest()
{
	XPrintf_3("XString 静态/查询函数测试 (count/number/maxSize)\n");

	{
		// 测试 count
		XString* s = XString_create_utf8("abcabcabc");
		size_t cnt = XString_count_utf8(s, "abc", XChar_CaseSensitive);
		XPrintf("count(\"abc\"): %zu\n", cnt);

		size_t cnt_ch = XString_count_char(s, XChar_from('a'), XChar_CaseSensitive);
		XPrintf("count_char('a'): %zu\n", cnt_ch);
		XString_delete_base(s);
	}
	{
		// 测试 number 系列
		XString* n1 = XString_number_int(12345, 10);
		XPrintf("number_int(12345): "); XPrintf_2(n1); printf("\n");
		XString_delete_base(n1);

		XString* n2 = XString_number_int(255, 16);
		XPrintf("number_int(255,16): "); XPrintf_2(n2); printf("\n");
		XString_delete_base(n2);

		XString* n3 = XString_number_double(3.14159, 'f', 3);
		XPrintf("number_double(3.14159,f,3): "); XPrintf_2(n3); printf("\n");
		XString_delete_base(n3);

		XString* n4 = XString_number_llong(-9876543210LL, 10);
		XPrintf("number_llong(-9876543210): "); XPrintf_2(n4); printf("\n");
		XString_delete_base(n4);
	}
	{
		// 测试 maxSize
		size_t maxSz = XString_maxSize();
		XPrintf("maxSize: %zu\n", maxSz);
	}
	{
		// 测试 setUnicode/setUtf16
		XString* s = XString_create();
		XChar chars[4] = { XChar_from('T'), XChar_from('e'), XChar_from('s'), XChar_from('t') };
		XString_setUnicode(s, chars, 4);
		XPrintf("setUnicode: "); XPrintf_2(s); printf("\n");

		uint16_t utf16[4] = { 'A', 'B', 'C', 'D' };
		XString_setUtf16(s, utf16, 4);
		XPrintf("setUtf16: "); XPrintf_2(s); printf("\n");
		XString_delete_base(s);
	}

	XCoreApplication_quit();
}

// 测试 XChar 重载操作 (indexOf_char/lastIndexOf_char/contains_char/remove_char/replace_char)
void XStringCharOpsTest()
{
	XPrintf_3("XString XChar重载操作测试\n");

	XString* str = XString_create_utf8("Hello, World!");

	{
		// 测试 indexOf_char
		int64_t idx = XString_indexOf_char(str, XChar_from('o'), 0, XChar_CaseSensitive);
		XPrintf("indexOf_char('o'): %lld\n", (long long)idx);
	}
	{
		// 测试 lastIndexOf_char
		int64_t idx = XString_lastIndexOf_char(str, XChar_from('o'), XChar_CaseSensitive);
		XPrintf("lastIndexOf_char('o'): %lld\n", (long long)idx);
	}
	{
		// 测试 contains_char
		bool has = XString_contains_char(str, XChar_from('W'), XChar_CaseSensitive);
		XPrintf("contains_char('W'): %s\n", has ? "true" : "false");
	}
	{
		// 测试 replace_char
		XString* s1 = XString_create_copy(str);
		XString_replace_char(s1, XChar_from('o'), XChar_from('0'), XChar_CaseSensitive);
		XPrintf("replace_char('o'->'0'): "); XPrintf_2(s1); printf("\n");
		XString_delete_base(s1);
	}
	{
		// 测试 remove_char
		XString* s2 = XString_create_copy(str);
		XString_remove_char(s2, XChar_from('l'), XChar_CaseSensitive);
		XPrintf("remove_char('l'): "); XPrintf_2(s2); printf("\n");
		XString_delete_base(s2);
	}
	{
		// 测试 data_ptr 和 constData
		XChar* dp = XString_data_ptr(str);
		XPrintf("data_ptr[0] code: %d\n", dp ? dp[0] : 0);

		const XChar* cdp = XString_constData(str);
		XPrintf("constData[7] code: %d\n", cdp ? cdp[7] : 0);
	}
	{
		// 测试 setNum_short
		XString* s3 = XString_create();
		XString_setNum_short(s3, -12345, 10);
		XPrintf("setNum_short(-12345): "); XPrintf_2(s3); printf("\n");
		XString_delete_base(s3);
	}

	XString_delete_base(str);

	XCoreApplication_quit();
}

// 全部测试：依次运行所有测试
// Qt 6.8 对齐测试：section / arg / localeAwareCompare / 新创建函数
void XStringQtAlignTest()
{
	XPrintf_3("========== Qt对齐测试 (section/arg/localeAwareCompare) ==========\n\n");

	// ---------------- section 分段测试 ----------------
	XPrintf_3("--- section 分段测试 ---\n");
	{
		XString* csv = XString_create_utf8("forename,middlename,surname,phone");
		{
			XString* s = XString_section_utf8(csv, ",", 2, 2, XString_SectionDefault);
			XPrintf_3("csv.section(',',2,2)        = "); XPrintf_2(s); XPrintf_3("  (期望:surname)\n");
			XString_delete_base(s);
		}
		{
			XString* s = XString_section_utf8(csv, ",", -3, -2, XString_SectionDefault);
			XPrintf_3("csv.section(',',-3,-2)      = "); XPrintf_2(s); XPrintf_3("  (期望:middlename,surname)\n");
			XString_delete_base(s);
		}
		XString_delete_base(csv);
	}
	{
		XString* path = XString_create_utf8("/usr/local/bin/myapp");
		{
			XString* s = XString_section_utf8(path, "/", 3, 4, XString_SectionDefault);
			XPrintf_3("path.section('/',3,4)       = "); XPrintf_2(s); XPrintf_3("  (期望:bin/myapp)\n");
			XString_delete_base(s);
		}
		{
			XString* s = XString_section_utf8(path, "/", 3, 3, XString_SectionSkipEmpty);
			XPrintf_3("path.section('/',3,3,Skip)  = "); XPrintf_2(s); XPrintf_3("  (期望:myapp)\n");
			XString_delete_base(s);
		}
		{
			XString* s = XString_section_utf8(path, "/", -1, -1, XString_SectionDefault);
			XPrintf_3("path.section('/',-1)        = "); XPrintf_2(s); XPrintf_3("  (期望:myapp)\n");
			XString_delete_base(s);
		}
		XString_delete_base(path);
	}
	{
		// 多字符分隔符 + 单字符(XChar)分隔符 + 标志位
		XString* data = XString_create_utf8("forename**middlename**surname**phone");
		{
			XString* s = XString_section_utf8(data, "**", -3, -2, XString_SectionDefault);
			XPrintf_3("data.section('**',-3,-2)    = "); XPrintf_2(s); XPrintf_3("  (期望:middlename**surname)\n");
			XString_delete_base(s);
		}
		XString_delete_base(data);

		XString* csv = XString_create_utf8("forename,middlename,surname,phone");
		{
			XString* s = XString_section_char(csv, XChar_from(','), 2, 2,
				XString_SectionIncludeLeadingSep | XString_SectionIncludeTrailingSep);
			XPrintf_3("section_char(',',2,2,Lead|Trail) = "); XPrintf_2(s); XPrintf_3("  (期望:,surname,)\n");
			XString_delete_base(s);
		}
		XString_delete_base(csv);
	}

	// ---------------- arg 占位符替换测试 ----------------
	XPrintf_3("\n--- arg 占位符替换测试 ---\n");
	{
		// 字符串arg链式替换（最低编号优先）
		XString* fmt = XString_create_utf8("%1 你好 %2");
		XString* r1 = XString_arg_utf8(fmt, "世界", 0, XChar_from(' '));
		XString* r2 = XString_arg_utf8(r1, "再见", 0, XChar_from(' '));
		XPrintf_3("arg链式(%1 你好 %2)      = "); XPrintf_2(r2); XPrintf_3("  (期望:世界 你好 再见)\n");
		XString_delete_base(r2);
		XString_delete_base(r1);
		XString_delete_base(fmt);
	}
	{
		// 整数arg（宏委托arg_llong）
		XString* fmt = XString_create_utf8("%1: 共 %2 项");
		XString* r1 = XString_arg_int(fmt, 42, 0, 10, XChar_from(' '));
		XString* r2 = XString_arg_llong(r1, 100, 0, 10, XChar_from(' '));
		XPrintf_3("arg_int/llong(%1: 共 %2 项) = "); XPrintf_2(r2); XPrintf_3("  (期望:42: 共 100 项)\n");
		XString_delete_base(r2);
		XString_delete_base(r1);
		XString_delete_base(fmt);
	}
	{
		// double arg + fieldWidth 填充（右对齐/左对齐）
		XString* fmt = XString_create_utf8("值=%1");
		XString* r1 = XString_arg_double(fmt, 3.14, 8, 'f', 2, XChar_from('_'));
			XPrintf_3("arg_double(fw=8 右对齐)    = "); XPrintf_2(r1); XPrintf_3("  (期望:值=____3.14)\n");
		XString_delete_base(r1);
		XString* r2 = XString_arg_double(fmt, 3.14, -8, 'f', 2, XChar_from('_'));
			XPrintf_3("arg_double(fw=-8 左对齐)   = "); XPrintf_2(r2); XPrintf_3("  (期望:值=3.14____)\n");
		XString_delete_base(r2);
		XString_delete_base(fmt);
	}
	{
		// 单字符arg
		XString* fmt = XString_create_utf8("字母%1");
		XString* r = XString_arg_char(fmt, XChar_from('X'), 0, XChar_from(' '));
		XPrintf_3("arg_char('X')              = "); XPrintf_2(r); XPrintf_3("  (期望:字母X)\n");
		XString_delete_base(r);
		XString_delete_base(fmt);
	}
	{
		// 无占位符：返回源串拷贝
		XString* fmt = XString_create_utf8("无占位符");
		XString* r = XString_arg_utf8(fmt, "x", 0, XChar_from(' '));
		XPrintf_3("arg(无占位符)              = "); XPrintf_2(r); XPrintf_3("  (期望:无占位符)\n");
		XString_delete_base(r);
		XString_delete_base(fmt);
	}
	{
		// 重复占位符 %1 全部替换
		XString* fmt = XString_create_utf8("%1 和 %1");
		XString* r = XString_arg_utf8(fmt, "A", 0, XChar_from(' '));
		XPrintf_3("arg(%1 和 %1)            = "); XPrintf_2(r); XPrintf_3("  (期望:A 和 A)\n");
		XString_delete_base(r);
		XString_delete_base(fmt);
	}

	// ---------------- localeAwareCompare 区域感知比较 ----------------
	XPrintf_3("\n--- localeAwareCompare 区域感知比较 ---\n");
	{
		XString* a = XString_create_utf8("apple");
		XString* b = XString_create_utf8("banana");
		XPrintf("compare(apple,banana) = %d  (期望:<0)\n", XString_localeAwareCompare(a, b));
		XPrintf("compare(apple,apple)   = %d  (期望:0)\n", XString_localeAwareCompare(a, a));
		XPrintf("compare(banana,apple) = %d  (期望:>0)\n", XString_localeAwareCompare(b, a));
		XString_delete_base(a);
		XString_delete_base(b);
	}

	// ---------------- 新创建函数 + 别名宏 ----------------
	XPrintf_3("\n--- 新创建函数 + 别名宏 ---\n");
	{
		XString* s = XString_create_latin1("Hello");
		XPrintf_3("create_latin1(Hello)       = "); XPrintf_2(s); XPrintf_3("  (期望:Hello)\n");
		XString_delete_base(s);
		XString* s2 = XString_fromLatin1("QtAlias");
		XPrintf_3("fromLatin1(QtAlias)        = "); XPrintf_2(s2); XPrintf_3("  (期望:QtAlias)\n");
		XString_delete_base(s2);
	}
	{
		uint32_t ucs4[] = { 0x4F60, 0x597D, 0 };  // 你好
		XString* s = XString_create_utf32(ucs4);
		XPrintf_3("create_utf32(你好)          = "); XPrintf_2(s); XPrintf_3("  (期望:你好)\n");
		XString_delete_base(s);
		XString* s2 = XString_fromUcs4(ucs4);
		XPrintf_3("fromUcs4(你好)              = "); XPrintf_2(s2); XPrintf_3("  (期望:你好)\n");
		XString_delete_base(s2);
	}
	{
		XString* s = XString_fromLocal8Bit("本地8位");
		XPrintf_3("fromLocal8Bit(本地8位)      = "); XPrintf_2(s); XPrintf_3("  (期望:本地8位)\n");
		XString_delete_base(s);
	}
	{
		XString* s = XString_asprintf("%d年%s", 2026, "测试");
		XPrintf_3("asprintf 结果              = "); XPrintf_2(s); XPrintf_3("  (期望:2026年测试)\n");
		XString_delete_base(s);
	}
	{
		// toLocal8Bit / toUcs4 别名
		XString* s = XString_create_utf8("Ab你好");
		const char* local = XString_toLocal8Bit(s);
		XPrintf_3("toLocal8Bit(str)           = "); XPrintf_3(local); XPrintf_3("  (期望:Ab你好)\n");
		const uint32_t* ucs4 = XString_toUcs4(s);
		XPrintf("toUcs4(str)[0]=%u [2]=0x%04X  (期望:65,0x4F60)\n", (unsigned)ucs4[0], (unsigned)ucs4[2]);
		XString_delete_base(s);
	}

	// ---------------- 宏化重构验证（number/constData 别名宏） ----------------
	XPrintf_3("\n--- 宏化重构验证 (number/constData 别名宏) ---\n");
	{
		// number_int 宏委托 number_llong（对齐Qt: number(int)->number(qlonglong)）
		XString* n1 = XString_number_int(255, 16);
		XPrintf_3("number_int(255,16)         = "); XPrintf_2(n1); XPrintf_3("  (期望:FF)\n");
		XString_delete_base(n1);
		XString* n2 = XString_number_int(12345, 10);
		XPrintf_3("number_int(12345,10)       = "); XPrintf_2(n2); XPrintf_3("  (期望:12345)\n");
		XString_delete_base(n2);
		// number_float 宏委托 number_double
		XString* n3 = XString_number_float(3.14, 'f', 2);
		XPrintf_3("number_float(3.14,f,2)     = "); XPrintf_2(n3); XPrintf_3("  (期望:3.14)\n");
		XString_delete_base(n3);
	}
	{
		// constData 宏等价于 unicode（返回同一指针）
		XString* s = XString_create_utf8("abc");
		XPrintf("constData==unicode 指针相同: %s  (期望:是)\n", (XString_unicode(s) == XString_constData(s)) ? "是" : "否");
		XString_delete_base(s);
	}

	// ---------------- leftJustified / rightJustified 对齐填充测试 ----------------
	XPrintf_3("\n--- leftJustified / rightJustified 对齐填充测试 ---\n");
	{
		// 基本左对齐填充
		XString* s = XString_create_utf8("abc");
		XString* r = XString_leftJustified(s, 7, XChar_from('_'), false);
		XPrintf_3("leftJustified(abc,7,_,false)  = "); XPrintf_2(r); XPrintf_3("  (期望:abc____)\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		// 基本右对齐填充
		XString* s = XString_create_utf8("abc");
		XString* r = XString_rightJustified(s, 7, XChar_from('_'), false);
		XPrintf_3("rightJustified(abc,7,_,false) = "); XPrintf_2(r); XPrintf_3("  (期望:____abc)\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		// 左对齐 + 截断 (width < len, truncate=true)
		XString* s = XString_create_utf8("abcde");
		XString* r = XString_leftJustified(s, 3, XChar_from('-'), true);
		XPrintf_3("leftJustified(abcde,3,-,true) = "); XPrintf_2(r); XPrintf_3("  (期望:abc)\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		// 右对齐 + 截断 (width < len, truncate=true)
		XString* s = XString_create_utf8("abcde");
		XString* r = XString_rightJustified(s, 3, XChar_from('-'), true);
		XPrintf_3("rightJustified(abcde,3,-,true)= "); XPrintf_2(r); XPrintf_3("  (期望:abc)\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		// 不截断：width < len, truncate=false → 返回原串拷贝
		XString* s = XString_create_utf8("abcde");
		XString* r = XString_leftJustified(s, 3, XChar_from('-'), false);
		XPrintf_3("leftJustified(abcde,3,-,false)= "); XPrintf_2(r); XPrintf_3("  (期望:abcde)\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		// 不截断：width < len, truncate=false → 返回原串拷贝
		XString* s = XString_create_utf8("abcde");
		XString* r = XString_rightJustified(s, 3, XChar_from('-'), false);
		XPrintf_3("rightJustified(abcde,3,-,false)="); XPrintf_2(r); XPrintf_3("  (期望:abcde)\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		// 等宽：width == len
		XString* s = XString_create_utf8("abc");
		XString* r = XString_leftJustified(s, 3, XChar_from('_'), false);
		XPrintf_3("leftJustified(abc,3,_,false)  = "); XPrintf_2(r); XPrintf_3("  (期望:abc)\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		// 空串填充
		XString* s = XString_create_utf8("");
		XString* r = XString_rightJustified(s, 5, XChar_from('*'), false);
		XPrintf_3("rightJustified('',5,*,false)  = "); XPrintf_2(r); XPrintf_3("  (期望:*****)\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	{
		// NULL 安全性
		XString* r = XString_leftJustified(NULL, 10, XChar_from('.'), false);
		XPrintf("leftJustified(NULL)=%s  (期望:空)\n", r ? "非空" : "空");
		XString_delete_base(r);
	}
	{
		// NULL 安全性
		XString* r = XString_rightJustified(NULL, 10, XChar_from('.'), true);
		XPrintf("rightJustified(NULL)=%s  (期望:空)\n", r ? "非空" : "空");
		XString_delete_base(r);
	}
	{
		// 中文填充
		XString* s = XString_create_utf8("你好");
		XString* r = XString_leftJustified(s, 6, XChar_from(' '), false);
		XPrintf_3("leftJustified(你好,6, ,false)  = "); XPrintf_2(r); XPrintf_3("  (期望:你好    )\n");
		XString_delete_base(r);
		XString_delete_base(s);
	}
	XPrintf_3("\n========== Qt对齐测试结束 ==========\n");
	XCoreApplication_quit();
}

void XStringAllTest()
{
	//while(true)
	{
		XPrintf_3("========== XString 全部测试开始 ==========\n\n");

		XStringQtAlignTest();

		XStringSlicedTest();
		XStringInplaceTest();
		XStringConvertTest();
		XStringStaticTest();
		XStringCharOpsTest();
		XStringUtf16Test();
		XStringNumTest();

		XPrintf_3("\n========== XString 全部测试结束 ==========\n");
	}
	XCoreApplication_quit();
}

// 测试 utf16 直接返回内部数据
void XStringUtf16Test()
{
	XPrintf_3("XString utf16 直接返回测试\n");

	XString* str = XString_create_utf8("Hello 你好");
	const uint16_t* utf16 = XString_utf16(str);
	if (utf16)
	{
		XPrintf("utf16[0] (H): %d\n", utf16[0]);
		XPrintf("utf16[6] (你high): %d\n", utf16[6]);
		XPrintf("utf16[7] (你low): %d\n", utf16[7]);
		// 验证内部数据一致性（XChar就是uint16_t）
		const XChar* uni = XString_unicode(str);
		XPrintf("unicode[0]==utf16[0]: %s\n", (uni[0] == utf16[0]) ? "true" : "false");
		XPrintf("unicode[6]==utf16[6]: %s\n", (uni[6] == utf16[6]) ? "true" : "false");
	}
	// 验证 NULL 输入
	const uint16_t* null_res = XString_utf16(NULL);
	XPrintf("utf16(NULL)==NULL: %s\n", (null_res == NULL) ? "true" : "false");

	XString_delete_base(str);

	XCoreApplication_quit();
}
void XMenu_XStringTest(XMenu* root)
{
	XMenu* menu = XMenu_create("字符串(XString)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "【全部测试】");
		XAction_setAction(action, XStringAllTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XStringTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "迭代器测试");
		XAction_setAction(action, XStringIteratorTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "整数测试");
		XAction_setAction(action, XStringNumTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "操作");
		XAction_setAction(action, XStringOperateTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "拷贝");
		XAction_setAction(action, XStringCopyTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "子串操作(sliced/first/last/chopped)");
		XAction_setAction(action, XStringSlicedTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "原地修改(chop/fill/squeeze/slice)");
		XAction_setAction(action, XStringInplaceTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "转换(toCaseFolded/HTML/simplified)");
		XAction_setAction(action, XStringConvertTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "静态/查询(count/number/maxSize)");
		XAction_setAction(action, XStringStaticTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "XChar操作(indexOf/remove/replace)");
		XAction_setAction(action, XStringCharOpsTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "utf16直接返回测试");
		XAction_setAction(action, XStringUtf16Test);
	}
	{
		XAction* action = XMenu_addAction(menu, "Qt对齐(section/arg/locale)");
		XAction_setAction(action, XStringQtAlignTest);
	}
}
#endif

#endif