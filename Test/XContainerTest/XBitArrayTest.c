#include"XDataStructTest.h"
#if DEMOTEST
#include"XBitArray.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"

/* ==================== 基础测试 ==================== */
static void XBitArrayTest_Basic(void)
{
    XPrintf("\n[XBitArray][基础] create/setBit/getBit/toggleBit/size\n");
    XBitArray* a = XBitArray_create(16);
    XBitArray_fill(a, false);
    XPrintf("  size=%zu isEmpty=%d (期望 16 0)\n",
        XBitArray_size_base(a), XBitArray_isEmpty_base(a));

    XBitArray_setBit(a, 0, true);
    XBitArray_setBit(a, 3, true);
    XBitArray_setBit(a, 15, true);
    XPrintf("  bits[0,3,15]=%d %d %d (期望 1 1 1)\n",
        XBitArray_getBit(a, 0), XBitArray_getBit(a, 3), XBitArray_getBit(a, 15));

    XBitArray_toggleBit(a, 0);
    XPrintf("  toggleBit(0) -> bit[0]=%d (期望 0)\n", XBitArray_getBit(a, 0));

    XBitArray_delete_base(a);
}

/* ==================== Qt 命名对齐 ==================== */
static void XBitArrayTest_QtAliases(void)
{
    XPrintf("\n[XBitArray][Qt 别名] testBit/at/clearBit/count_base/isNull/bits\n");
    XBitArray* a = XBitArray_create(8);
    XBitArray_fill(a, true);
    XPrintf("  count_base=%zu isNull=%d (期望 8 0)\n",
        XBitArray_count_base(a), XBitArray_isNull_base(a));
    XPrintf("  testBit(0)=%d at(3)=%d (期望 1 1)\n",
        XBitArray_testBit(a, 0), XBitArray_at(a, 3));

    XBitArray_clearBit(a, 0);
    XPrintf("  clearBit(0) -> testBit(0)=%d (期望 0)\n", XBitArray_testBit(a, 0));

    const char* raw = XBitArray_bits(a);
    XPrintf("  bits ptr=%s\n", raw ? "OK" : "NULL");

    XBitArray_delete_base(a);
}

/* ==================== Qt 新 API ==================== */
static void XBitArrayTest_QtNewApis(void)
{
    XPrintf("\n[XBitArray][Qt 新 API] setBit_one/countBits/fill_range/equals/and/or/xor/invert/inverted\n");

    XBitArray* a = XBitArray_create(10);
    XBitArray_fill(a, false);
    XBitArray_setBit_one(a, 2);
    XBitArray_setBit_one(a, 5);
    XBitArray_setBit_one(a, 9);
    XPrintf("  setBit_one(2,5,9) -> count(true)=%zu count(false)=%zu (期望 3 7)\n",
        XBitArray_countBits(a, true), XBitArray_countBits(a, false));

    XBitArray_fill_range(a, true, 0, 5);
    XPrintf("  fill_range(true,0,5) -> count(true)=%zu (期望 7)\n",
        XBitArray_countBits(a, true));
    // 打印
    XPrintf("  bits=");
    for (size_t i = 0; i < XBitArray_size_base(a); ++i)
        XPrintf("%d", XBitArray_getBit(a, i));
    XPrintf(" (期望 1111110001)\n");

    // equals
    XBitArray* b = XBitArray_create_copy(a);
    XPrintf("  equals(a,b) = %d (期望 1)\n", XBitArray_equals(a, b));
    XBitArray_toggleBit(b, 0);
    XPrintf("  after toggle b[0], equals = %d (期望 0)\n", XBitArray_equals(a, b));
    XBitArray_delete_base(b);

    // and/or/xor
    XBitArray* x = XBitArray_create(8);
    XBitArray* y = XBitArray_create(8);
    XBitArray_fill(x, false); XBitArray_fill(y, false);
    for (int i = 0; i < 4; ++i) XBitArray_setBit(x, i, true); // 11110000
    for (int i = 2; i < 6; ++i) XBitArray_setBit(y, i, true); // 00111100
    XBitArray* xy_and = XBitArray_create_copy(x);
    XBitArray_and_inplace(xy_and, y);
    XPrintf("  x & y count(true)=%zu (期望 2)\n", XBitArray_countBits(xy_and, true));
    XBitArray* xy_or = XBitArray_create_copy(x);
    XBitArray_or_inplace(xy_or, y);
    XPrintf("  x | y count(true)=%zu (期望 6)\n", XBitArray_countBits(xy_or, true));
    XBitArray* xy_xor = XBitArray_create_copy(x);
    XBitArray_xor_inplace(xy_xor, y);
    XPrintf("  x ^ y count(true)=%zu (期望 4)\n", XBitArray_countBits(xy_xor, true));

    XBitArray* nx = XBitArray_inverted(x);
    XPrintf("  ~x count(true)=%zu (期望 4)\n", XBitArray_countBits(nx, true));
    XBitArray_invert_inplace(nx);
    XPrintf("  ~~x equals x = %d (期望 1)\n", XBitArray_equals(nx, x));

    XBitArray_delete_base(x);
    XBitArray_delete_base(y);
    XBitArray_delete_base(xy_and);
    XBitArray_delete_base(xy_or);
    XBitArray_delete_base(xy_xor);
    XBitArray_delete_base(nx);
    XBitArray_delete_base(a);
}

/* ==================== 压力测试 ==================== */
static void XBitArrayTest_Stress(void)
{
    XPrintf("\n[XBitArray][压力] 大批量 setBit/toggleBit/countBits/inverted\n");
    const size_t N = 20000;
    XBitArray* a = XBitArray_create(N);
    XBitArray_fill(a, false);
    for (size_t i = 0; i < N; i += 3) XBitArray_setBit(a, i, true);
    size_t c = XBitArray_countBits(a, true);
    XPrintf("  每3位置1 count(true)=%zu (期望 %zu)\n", c, (N + 2) / 3);

    XBitArray_invert_inplace(a);
    XPrintf("  invert -> count(true)=%zu\n", XBitArray_countBits(a, true));

    XBitArray_truncate(a, 100);
    XPrintf("  truncate(100) -> size=%zu (期望 100)\n", XBitArray_size_base(a));

    XBitArray_delete_base(a);
}

/* ==================== 主入口 ==================== */
static void XBitArrayTest_All(void)
{
#if XBitArray_ON
    XPrintf("XBitArray 全量测试 (Qt QBitArray 对齐)\n");
    XBitArrayTest_Basic();
    XBitArrayTest_QtAliases();
    XBitArrayTest_QtNewApis();
    XBitArrayTest_Stress();
    XPrintf("\n[XBitArray] 全部测试完成\n");
#endif
    //XCoreApplication_quit();
}

void XMenu_XBitArrayTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XBitArray(位数组)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全量测试(基础+Qt别名+Qt新API+压力)");
        XAction_setAction(action, XBitArrayTest_All);
    }
}
#endif
