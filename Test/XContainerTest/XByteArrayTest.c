#include"XDataStructTest.h"
#if DEMOTEST
#include"XByteArray.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include<string.h>

/* ==================== 基础测试 ==================== */
static void XByteArrayTest_Basic(void)
{
    XPrintf("\n[XByteArray][基础] create/append/at/data/isEmpty\n");
    XByteArray* ba = XByteArray_create_utf8("Hello");
    XPrintf("  size=%zu (期望 5)\n", XByteArray_size_base(ba));
    XPrintf("  isEmpty=%d\n", XByteArray_isEmpty_base(ba));
    XPrintf("  data=%.*s\n", (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_push_back_1(ba, '!');
    XPrintf("  push_back('!') size=%zu (期望 6)\n", XByteArray_size_base(ba));
    XPrintf("  data=%.*s (期望 Hello!)\n", (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_delete_base(ba);
}

/* ==================== Qt 命名对齐 ==================== */
static void XByteArrayTest_QtAliases(void)
{
    XPrintf("\n[XByteArray][Qt 别名] length/empty/constData/contains/indexOf/startsWith/endsWith\n");
    XByteArray* ba = XByteArray_create_utf8("abcXYZabc");

    size_t s1 = XByteArray_size_base(ba), s2 = XByteArray_length_base(ba);
    XPrintf("  size=%zu length=%zu (%s)\n", s1, s2, (s1==s2?"OK":"FAIL"));

    bool e1 = XByteArray_isEmpty_base(ba), e2 = XByteArray_empty_base(ba);
    XPrintf("  isEmpty=%d empty=%d (%s)\n", e1, e2, (e1==e2?"OK":"FAIL"));

    const uint8_t* cd = XByteArray_constData(ba);
    XPrintf("  constData=%.*s\n", (int)s1, (const char*)cd);

    XPrintf("  contains('X')=%d (期望 1)\n", XByteArray_contains(ba, 'X'));
    XPrintf("  contains('?')=%d (期望 0)\n", XByteArray_contains(ba, '?'));

    XPrintf("  indexOf('a', 0)=%lld (期望 0)\n", (long long)XByteArray_indexOf(ba, 'a', 0));
    XPrintf("  indexOf('a', 1)=%lld (期望 6)\n", (long long)XByteArray_indexOf(ba, 'a', 1));
    XPrintf("  lastIndexOf('c', -1)=%lld (期望 8)\n", (long long)XByteArray_lastIndexOf(ba, 'c', -1));

    XPrintf("  startsWith('a')=%d endsWith('c')=%d (期望 1 1)\n",
        XByteArray_startsWith(ba, 'a'), XByteArray_endsWith(ba, 'c'));

    XByteArray_delete_base(ba);
}

/* ==================== 新 API：fill/truncate/chop/left/right/mid ==================== */
static const bool IsUpper(const void* pv, const void* userData)
{
    (void)userData;
    uint8_t c = *(const uint8_t*)pv;
    return c >= 'A' && c <= 'Z';
}

static void XByteArrayTest_QtNewApis(void)
{
    XPrintf("\n[XByteArray][Qt 新 API] fill/truncate/chop/left/right/mid/removeIf\n");

    // fill
    XByteArray* ba = XByteArray_create();
    XByteArray_fill_base(ba, 'x', 5);
    XPrintf("  fill('x',5) -> size=%zu data=%.*s (期望 5 xxxxx)\n",
        XByteArray_size_base(ba), (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_fill_base(ba, 'y', -1);
    XPrintf("  fill('y',-1) -> data=%.*s (期望 yyyyy)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_delete_base(ba);

    // truncate / chop
    ba = XByteArray_create_utf8("HelloWorld");
    XByteArray_truncate_base(ba, 5);
    XPrintf("  truncate(5) -> %.*s (期望 Hello)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_chop_base(ba, 2);
    XPrintf("  chop(2)     -> %.*s (期望 Hel)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_delete_base(ba);

    // left / right / mid
    ba = XByteArray_create_utf8("0123456789");
    XByteArray* l = XByteArray_left_base(ba, 3);
    XByteArray* r = XByteArray_right_base(ba, 3);
    XByteArray* m = XByteArray_mid_base(ba, 2, 5);
    XPrintf("  left(3) =%.*s (期望 012)\n", (int)XByteArray_size_base(l), (char*)XByteArray_data(l));
    XPrintf("  right(3)=%.*s (期望 789)\n", (int)XByteArray_size_base(r), (char*)XByteArray_data(r));
    XPrintf("  mid(2,5)=%.*s (期望 23456)\n", (int)XByteArray_size_base(m), (char*)XByteArray_data(m));
    XByteArray_delete_base(l); XByteArray_delete_base(r); XByteArray_delete_base(m);
    XByteArray_delete_base(ba);

    // removeIf
    ba = XByteArray_create_utf8("aBcDeFgH");
    size_t n = XByteArray_removeIf(ba, IsUpper, NULL);
    XPrintf("  removeIf(Upper) 删除=%zu 剩余=%.*s (期望 4 aceg)\n",
        n, (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_delete_base(ba);

    // removeAt / removeFirst / removeLast
    ba = XByteArray_create_utf8("HelloX");
    XByteArray_removeAt_base(ba, 1);
    XPrintf("  removeAt(1) -> %.*s (期望 HlloX)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_removeFirst_base(ba);
    XPrintf("  removeFirst -> %.*s (期望 lloX)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_removeLast_base(ba);
    XPrintf("  removeLast  -> %.*s (期望 llo)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_delete_base(ba);
}

/* ==================== 编码 / 压缩 ==================== */
static void XByteArrayTest_Codec(void)
{
    XPrintf("\n[XByteArray][编解码] toHex/toBase64/fromBase64/toCompress/toDecompress\n");
    XByteArray* ba = XByteArray_create_utf8("Hello, XByteArray!");
    XByteArray* hex = XByteArray_toHex(ba);
    XPrintf("  toHex: %.*s\n", (int)XByteArray_size_base(hex), (char*)XByteArray_data(hex));
    XByteArray_delete_base(hex);

    XByteArray* b64 = XByteArray_toBase64(ba);
    XPrintf("  toBase64 size=%zu\n", XByteArray_size_base(b64));
    XByteArray* back = XByteArray_fromBase64(b64);
    XPrintf("  fromBase64: %.*s (期望 Hello, XByteArray!)\n",
        (int)XByteArray_size_base(back), (char*)XByteArray_data(back));
    XByteArray_delete_base(b64);
    XByteArray_delete_base(back);

    XByteArray* cz = XByteArray_toCompress(ba);
    XByteArray* dz = XByteArray_toDecompress(cz);
    XPrintf("  compress size=%zu -> decompress: %.*s\n",
        XByteArray_size_base(cz),
        (int)XByteArray_size_base(dz), (char*)XByteArray_data(dz));
    XByteArray_delete_base(cz);
    XByteArray_delete_base(dz);

    XByteArray_delete_base(ba);
}

/* ==================== 压力测试 ==================== */
static void XByteArrayTest_Stress(void)
{
    XPrintf("\n[XByteArray][压力] 大批量 append/reserve/squeeze/contains\n");
    XByteArray* ba = XByteArray_create();
    XByteArray_reserve_base(ba, 10000);
    XPrintf("  reserve(10000) capacity=%zu\n", XByteArray_capacity_base(ba));
    for (int i = 0; i < 10000; ++i)
        XByteArray_push_back_1(ba, (uint8_t)(i & 0xFF));
    XPrintf("  append 10000: size=%zu capacity=%zu\n",
        XByteArray_size_base(ba), XByteArray_capacity_base(ba));

    int hit = 0;
    for (int i = 0; i < 256; ++i)
        if (XByteArray_contains(ba, (uint8_t)i)) ++hit;
    XPrintf("  contains 覆盖 %d/256 字节值\n", hit);

    XByteArray_chop_base(ba, 5000);
    XPrintf("  chop(5000): size=%zu\n", XByteArray_size_base(ba));
    XByteArray_squeeze_base(ba);
    XPrintf("  squeeze: capacity=%zu\n", XByteArray_capacity_base(ba));

    XByteArray_delete_base(ba);
}

/* ==================== 主入口 ==================== */
static void XByteArrayTest_All(void)
{
#if XByteArray_ON
    XPrintf("XByteArray 全量测试 (Qt QByteArray 对齐)\n");
    XByteArrayTest_Basic();
    XByteArrayTest_QtAliases();
    XByteArrayTest_QtNewApis();
    XByteArrayTest_Codec();
    XByteArrayTest_Stress();
    XPrintf("\n[XByteArray] 全部测试完成\n");
#endif
    XCoreApplication_quit();
}

void XByteArrayTest(void)
{
    XByteArrayTest_All();
}

void XMenu_XByteArrayTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XByteArray(字节数组)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全量测试(基础+Qt别名+编解码+压力)");
        XAction_setAction(action, XByteArrayTest_All);
    }
}
#endif
