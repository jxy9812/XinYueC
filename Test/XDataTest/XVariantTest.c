#include"XVariantTest.h"
#include"XVariant.h"
#include"XString.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XPrintf.h"
#include<string.h>
#include <stdint.h>

// ==================== 测试辅助宏 ====================
#define TEST_PASS(name) XPrintf("[PASS] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[FAIL] %s: %s\n", name, reason)

// ==================== 测试辅助函数 ====================
/**
 * @brief 用非零垃圾模式毒化栈结构，模拟"未清零栈结构"最恶劣情形。
 * @details 0xA5 模式保证 m_data 为非 NULL 垃圾指针、m_dataSize 为非 0 垃圾值——
 *          库层防呆缺失时 XVariant_init 的防御性释放路径必然误 free 该垃圾指针。
 *          写入经独立函数中转，编译器无法将该写入优化掉。
 */
static void xvariant_test_poison(void* p, size_t n)
{
	unsigned char* b = (unsigned char*)p;
	size_t i;
	for (i = 0; i < n; i++)
		b[i] = (unsigned char)(0xA5 ^ (unsigned char)(i & 0xFF));
}

// ==================== 测试1: 未清零栈结构 init 标量类型 ====================
static bool test_unzeroed_init_scalar(void)
{
	XVariant v;
	bool ok = true;
	xvariant_test_poison(&v, sizeof(v));
	XVariant_init(&v, NULL, 0, XVariantType_NULL);
	/* init(NULL,0,NULL) 后 m_data 必须为 NULL（垃圾被清零而非被误 free） */
	if (XVariant_data(&v) != NULL || XVariant_dataSize(&v) != 0)
	{
		TEST_FAIL("unzeroed_init_null", "init 后 m_data/dataSize 非空");
		ok = false;
	}
	XClassDeinit((XClass*)&v);

	/* 未清零 + 带数据 init：垃圾 m_data 不得被误 free，值必须正确 */
	xvariant_test_poison(&v, sizeof(v));
	{
		uint32_t src = 0x11223344u;
		XVariant_init(&v, &src, sizeof(src), XVariantType_Uint32);
	}
	if (XVariant_type(&v) != XVariantType_Uint32 ||
		XVariant_dataSize(&v) != sizeof(uint32_t) ||
		XVariant_toUint32(&v) != 0x11223344u)
	{
		TEST_FAIL("unzeroed_init_uint32", "值或类型不正确");
		ok = false;
	}
	XClassDeinit((XClass*)&v);

	/* 未清零 + double */
	xvariant_test_poison(&v, sizeof(v));
	{
		double d = 3.14159;
		XVariant_init(&v, &d, sizeof(d), XVariantType_Double);
	}
	if (XVariant_toDouble(&v) != 3.14159)
	{
		TEST_FAIL("unzeroed_init_double", "值不正确");
		ok = false;
	}
	XClassDeinit((XClass*)&v);

	if (ok) TEST_PASS("unzeroed_init_scalar");
	return ok;
}

// ==================== 测试2: 未清零栈结构 init→set→get→clear 全程 ====================
static bool test_unzeroed_init_set_get_clear(void)
{
	XVariant v;
	bool ok = true;
	xvariant_test_poison(&v, sizeof(v));
	XVariant_init(&v, NULL, 0, XVariantType_NULL);
	/* set */
	XVariant_setValue_int(&v, -12345);
	if (XVariant_toInt(&v) != -12345)
	{
		TEST_FAIL("unzeroed_set_get", "setValue_int 后 toInt 不正确");
		ok = false;
	}
	/* 换型 set（触发内部 deinit 释放路径） */
	XVariant_setValue_double(&v, 2.5);
	if (XVariant_toDouble(&v) != 2.5)
	{
		TEST_FAIL("unzeroed_set_get", "换型 setValue_double 后不正确");
		ok = false;
	}
	/* clear（清零内容不释放） */
	XVariant_clear(&v);
	/* 再 set 回标量，验证 clear 后对象仍可用 */
	XVariant_setValue_uint16(&v, 0xBEEF);
	if (XVariant_toUint16(&v) != 0xBEEF)
	{
		TEST_FAIL("unzeroed_set_get", "clear 后再 set 不正确");
		ok = false;
	}
	XClassDeinit((XClass*)&v);
	if (ok) TEST_PASS("unzeroed_init_set_get_clear");
	return ok;
}

// ==================== 测试3: 未清零栈结构 扩展类型（String）init ====================
static bool test_unzeroed_init_string(void)
{
	XVariant v;
	bool ok = true;
	/* String 型变体的合法载荷是完整 XString 对象（见 XString_toVariant，
	 * XString.c:16-28）：分配 sizeof(XString) 后 XString_init 初始化。
	 * 注意：禁止对 String 型传裸 char 缓冲——deinit 会经 XClass_deinit_base
	 * 按多态析构 XString，裸缓冲必崩（既有契约，非本防呆范围）。 */
	xvariant_test_poison(&v, sizeof(v));
	XVariant_init(&v, NULL, sizeof(XString), XVariantType_String);
	{
		XString* s = (XString*)XVariant_data(&v);
		XString_init(s);
		XString_append_utf8(s, "variant-hello");
		if (XVariant_type(&v) != XVariantType_String ||
			XVariant_dataSize(&v) != sizeof(XString) ||
			strcmp(XString_c_str(s), "variant-hello") != 0)
		{
			TEST_FAIL("unzeroed_init_string", "字符串内容不正确");
			ok = false;
		}
	}
	XClassDeinit((XClass*)&v);
	if (ok) TEST_PASS("unzeroed_init_string");
	return ok;
}

// ==================== 测试4: XVariant_Init 宏（裸栈声明） ====================
static bool test_init_macro_uninitialized(void)
{
	bool ok = true;
	/* XVariant_Init 宏声明未初始化栈变量后直接 init——库层防呆的主防线场景 */
	XVariant_Init(mv, NULL, 0, XVariantType_NULL);
	XVariant_setValue_int(mv, 777);
	if (XVariant_toInt(mv) != 777)
	{
		TEST_FAIL("init_macro_uninitialized", "宏声明裸栈变量后 set/get 不正确");
		ok = false;
	}
	XClassDeinit((XClass*)mv);

	/* 再来一次带数据的宏场景 */
	{
		uint64_t big = 0xDEADBEEFCAFEBABEull;
		XVariant_Init(bv, &big, sizeof(big), XVariantType_Uint64);
		if (XVariant_toUint64(bv) != big)
		{
			TEST_FAIL("init_macro_uninitialized", "宏 + uint64 不正确");
			ok = false;
		}
		XClassDeinit((XClass*)bv);
	}
	if (ok) TEST_PASS("init_macro_uninitialized");
	return ok;
}

// ==================== 测试5: 已初始化对象重复 init 仍先释放旧数据（防泄漏契约保持） ====================
static bool test_reinit_releases_old(void)
{
	XVariant v;
	bool ok = true;
	uint32_t a = 0xAAAA1111u, b = 0xBBBB2222u;
	memset(&v, 0, sizeof(v));
	XVariant_init(&v, &a, sizeof(a), XVariantType_Uint32);
	if (XVariant_toUint32(&v) != a)
	{
		TEST_FAIL("reinit_releases_old", "首次 init 值不正确");
		ok = false;
	}
	/* 重复 init：旧 m_data 必须被释放（valgrind/ASan 下验证不漏）且新值正确 */
	XVariant_init(&v, &b, sizeof(b), XVariantType_Uint32);
	if (XVariant_toUint32(&v) != b)
	{
		TEST_FAIL("reinit_releases_old", "重复 init 后值不正确");
		ok = false;
	}
	/* 换型重复 init：类型变化路径 */
	{
		double d = 1.25;
		XVariant_init(&v, &d, sizeof(d), XVariantType_Double);
		if (XVariant_toDouble(&v) != 1.25)
		{
			TEST_FAIL("reinit_releases_old", "换型重复 init 后值不正确");
			ok = false;
		}
	}
	XClassDeinit((XClass*)&v);
	if (ok) TEST_PASS("reinit_releases_old");
	return ok;
}

// ==================== 测试6: 堆创建/拷贝/移动/setDataRef 契约不回归 ====================
static bool test_heap_ownership_contract(void)
{
	XVariant* v = XVariant_create_uint32(0xCAFED00Du);
	XVariant* c = NULL;
	XVariant* m = NULL;
	bool ok = true;
	if (!v || XVariant_toUint32(v) != 0xCAFED00Du)
	{
		TEST_FAIL("heap_ownership_contract", "create_uint32 不正确");
		ok = false;
	}
	c = XVariant_create_copy(v);
	if (!c || XVariant_toUint32(c) != 0xCAFED00Du || c == v)
	{
		TEST_FAIL("heap_ownership_contract", "create_copy 不正确");
		ok = false;
	}
	m = XVariant_create_move(c);
	if (!m || XVariant_toUint32(m) != 0xCAFED00Du)
	{
		TEST_FAIL("heap_ownership_contract", "create_move 不正确");
		ok = false;
	}
	if (m)
	{
		/* setDataRef 移交所有权后重复 init 应释放所adopted指针（不漏不崩）。
		 * 用标量 Ptr 型（非扩展类型，forType 返回 NULL → deinit 直接
		 * XFree_System）——扩展类型（String/ByteArray 等）的合法载荷是完整
		 * 类型对象而非裸缓冲，不可用裸指针探所有权（见 XString_toVariant）。 */
		size_t* blob = (size_t*)XMalloc_System(sizeof(size_t) * 4);
		if (blob)
		{
			memset(blob, 0x5A, sizeof(size_t) * 4);
			XVariant_setDataRef(m, blob, sizeof(size_t) * 4, XVariantType_Ptr);
			if (XVariant_dataSize(m) != sizeof(size_t) * 4 ||
				XVariant_data(m) != (void*)blob)
			{
				TEST_FAIL("heap_ownership_contract", "setDataRef 未接管指针");
				ok = false;
			}
			/* 清空载荷应释放 blob。注意必须用 setValue_null 而非 XVariant_init：
			 * XClass_init（XClass_virtual.c:23-24）无条件置 m_is_heap=false，
			 * 对堆对象调 init 会把对象降级为栈语义，随后 XClassDelete 跳过结构体
			 * 释放（ASan 实证漏 48B 结构体）；setValue_null 只清载荷不动头部。 */
			XVariant_setValue_null(m);
		}
		XClassDelete(m);
	}
	/* create 系列产物是堆对象：必须 XClassDelete（多态 deinit 释放 m_data +
	 * 按 IsHeap 释放结构体）；XDelete 只裸 free 结构体会泄漏 m_data（ASan 实证） */
	if (c) XClassDelete(c);
	if (v) XClassDelete(v);
	if (ok) TEST_PASS("heap_ownership_contract");
	return ok;
}

// ==================== 测试7: 零初始化结构（既有安全用法）不受影响 ====================
static bool test_zeroed_path_unchanged(void)
{
	XVariant v;
	bool ok = true;
	int src = 4321;
	memset(&v, 0, sizeof(v));
	XVariant_init(&v, &src, sizeof(src), XVariantType_Int);
	if (XVariant_toInt(&v) != 4321)
	{
		TEST_FAIL("zeroed_path_unchanged", "清零结构 init 不正确");
		ok = false;
	}
	XClassDeinit((XClass*)&v);
	/* XProperty 等模块的既有 memset+init 用法 */
	{
		XVariant z;
		memset(&z, 0, sizeof(z));
		XVariant_init(&z, NULL, 0, XVariantType_NULL);
		if (XVariant_data(&z) != NULL)
		{
			TEST_FAIL("zeroed_path_unchanged", "清零结构 init(NULL) 后 m_data 非空");
			ok = false;
		}
		XClassDeinit((XClass*)&z);
	}
	if (ok) TEST_PASS("zeroed_path_unchanged");
	return ok;
}

// ==================== 入口 ====================
int XVariantTest_run(void)
{
	bool t1 = test_unzeroed_init_scalar();
	bool t2 = test_unzeroed_init_set_get_clear();
	bool t3 = test_unzeroed_init_string();
	bool t4 = test_init_macro_uninitialized();
	bool t5 = test_reinit_releases_old();
	bool t6 = test_heap_ownership_contract();
	bool t7 = test_zeroed_path_unchanged();
	bool all = t1 && t2 && t3 && t4 && t5 && t6 && t7;
	XPrintf("XVariant 专项测试: scalar=%s set_get_clear=%s string=%s macro=%s reinit=%s heap=%s zeroed=%s => %s\n",
		t1 ? "通过" : "失败", t2 ? "通过" : "失败", t3 ? "通过" : "失败",
		t4 ? "通过" : "失败", t5 ? "通过" : "失败", t6 ? "通过" : "失败",
		t7 ? "通过" : "失败", all ? "通过" : "失败");
	return all ? 0 : 1;
}

#if DEMOTEST
static void xvariant_test_wrapper(XVariant* data)
{
	(void)data;
	XVariantTest_run();
}

void XTestMenu_XVariantTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XVariantTest");
	XTestMenu_addMenu(root, menu);
	XAction* action = XTestMenu_addAction(menu, "Variant 生命周期/防呆专项测试");
	XTestMenu_setActionFunction(action, xvariant_test_wrapper);
}
#endif
