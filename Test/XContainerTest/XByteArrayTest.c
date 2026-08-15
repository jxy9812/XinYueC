#include"XDataStructTest.h"
#if DEMOTEST
#include"XByteArray.h"
#include"XByteArrayView.h"
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
    XByteArray_fill(ba, 'x', 5);
    XPrintf("  fill('x',5) -> size=%zu data=%.*s (期望 5 xxxxx)\n",
        XByteArray_size_base(ba), (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_fill(ba, 'y', -1);
    XPrintf("  fill('y',-1) -> data=%.*s (期望 yyyyy)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_delete_base(ba);

    // truncate / chop
    ba = XByteArray_create_utf8("HelloWorld");
    XByteArray_truncate(ba, 5);
    XPrintf("  truncate(5) -> %.*s (期望 Hello)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_chop(ba, 2);
    XPrintf("  chop(2)     -> %.*s (期望 Hel)\n",
        (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
    XByteArray_delete_base(ba);

    // left / right / mid
    ba = XByteArray_create_utf8("0123456789");
    XByteArray* l = XByteArray_left(ba, 3);
    XByteArray* r = XByteArray_right(ba, 3);
    XByteArray* m = XByteArray_mid(ba, 2, 5);
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

    XByteArray_chop(ba, 5000);
    XPrintf("  chop(5000): size=%zu\n", XByteArray_size_base(ba));
    XByteArray_squeeze_base(ba);
    XPrintf("  squeeze: capacity=%zu\n", XByteArray_capacity_base(ba));

    XByteArray_delete_base(ba);
}

/* ==================== 主入口 ==================== */


/* ==================== Qt 重量项：replace/split/simplified/trimmed/toUpper/toLower/toInt/toDouble/setNum/percent/compare ==================== */
static void XByteArrayTest_QtHeavy(void)
{
    XPrintf("\n[XByteArray][Qt 重量项] replace/split/trimmed/simplified/toUpper/toLower/toInt/toDouble/setNum/percent/compareCS\n");

    /* replace */
    {
        XByteArray* ba = XByteArray_create_utf8("foo-bar-foo-baz");
        size_t n = XByteArray_replace(ba, (const uint8_t*)"foo", 3, (const uint8_t*)"XX", 2);
        XPrintf("  replace('foo'->'XX')=%zu -> %.*s (期望 2, XX-bar-XX-baz)\n",
            n, (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
        /* replace 空插入 = 删除 */
        n = XByteArray_replace(ba, (const uint8_t*)"XX", 2, (const uint8_t*)"", 0);
        XPrintf("  replace('XX'->'')=%zu -> %.*s (期望 2, -bar--baz)\n",
            n, (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
        XByteArray_delete_base(ba);
    }

    /* split */
    {
        XByteArray* ba = XByteArray_create_utf8("a,,bb,ccc,");
        XVector* parts = XByteArray_split(ba, ',');
        XPrintf("  split(',') 段数=%zu (期望 5)\n", XVector_size_base(parts));
        for (size_t i = 0; i < XVector_size_base(parts); ++i) {
            XByteArray* p = *(XByteArray**)XVector_at_base(parts, (int64_t)i);
            XPrintf("    [%zu]=\"%.*s\"\n", i,
                (int)XByteArray_size_base(p), (char*)XByteArray_data(p));
        }
        XByteArray_split_free(parts);
        XByteArray_delete_base(ba);
    }

    /* trimmed / simplified */
    {
        XByteArray* ba = XByteArray_create_utf8("  \t Hello   \n world  ");
        XByteArray* t = XByteArray_trimmed(ba);
        XByteArray* s2 = XByteArray_simplified(ba);
        XPrintf("  trimmed = \"%.*s\" (期望 Hello   \\n world)\n",
            (int)XByteArray_size_base(t), (char*)XByteArray_data(t));
        XPrintf("  simplified = \"%.*s\" (期望 Hello world)\n",
            (int)XByteArray_size_base(s2), (char*)XByteArray_data(s2));
        XByteArray_delete_base(t);
        XByteArray_delete_base(s2);
        XByteArray_delete_base(ba);
    }

    /* toUpper / toLower */
    {
        XByteArray* ba = XByteArray_create_utf8("Hello-World-123");
        XByteArray_toUpper(ba);
        XPrintf("  toUpper = %.*s (期望 HELLO-WORLD-123)\n",
            (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
        XByteArray_toLower(ba);
        XPrintf("  toLower = %.*s (期望 hello-world-123)\n",
            (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
        XByteArray_delete_base(ba);
    }

    /* toInt / toLongLong / toDouble */
    {
        bool ok = false;
        XByteArray* ba = XByteArray_create_utf8("  -1234 ");
        int v = XByteArray_toInt(ba, &ok, 10);
        XPrintf("  toInt(\"-1234\")=%d ok=%d (期望 -1234 1)\n", v, ok);
        XByteArray_delete_base(ba);

        ba = XByteArray_create_utf8("0xff");
        v = XByteArray_toInt(ba, &ok, 0);
        XPrintf("  toInt(\"0xff\",base=0)=%d ok=%d (期望 255 1)\n", v, ok);
        XByteArray_delete_base(ba);

        ba = XByteArray_create_utf8("abc");
        v = XByteArray_toInt(ba, &ok, 10);
        XPrintf("  toInt(\"abc\")=%d ok=%d (期望 0 0)\n", v, ok);
        XByteArray_delete_base(ba);

        ba = XByteArray_create_utf8("3.14159");
        double d = XByteArray_toDouble(ba, &ok);
        XPrintf("  toDouble(\"3.14159\")=%.5f ok=%d (期望 3.14159 1)\n", d, ok);
        XByteArray_delete_base(ba);
    }

    /* setNum */
    {
        XByteArray* ba = XByteArray_create();
        XByteArray_setNum_i64(ba, -9876543210LL, 10);
        XPrintf("  setNum_i64(-9876543210)=%.*s\n",
            (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
        XByteArray_setNum_i32(ba, 255, 16);
        XPrintf("  setNum_i32(255,base=16)=%.*s (期望 ff)\n",
            (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
        XByteArray_setNum_double(ba, 3.14159, 'f', 3);
        XPrintf("  setNum_double(3.14159,'f',3)=%.*s (期望 3.142)\n",
            (int)XByteArray_size_base(ba), (char*)XByteArray_data(ba));
        XByteArray_delete_base(ba);
    }

    /* percent encoding */
    {
        XByteArray* ba = XByteArray_create_utf8("Hello World / A+B=C");
        XByteArray* enc = XByteArray_toPercentEncoding(ba);
        XPrintf("  percentEncode: %.*s\n",
            (int)XByteArray_size_base(enc), (char*)XByteArray_data(enc));
        XByteArray* dec = XByteArray_fromPercentEncoding(enc);
        XPrintf("  percentDecode: %.*s (期望 Hello World / A+B=C  注:+->空格)\n",
            (int)XByteArray_size_base(dec), (char*)XByteArray_data(dec));
        XByteArray_delete_base(enc);
        XByteArray_delete_base(dec);
        XByteArray_delete_base(ba);
    }

    /* compareCS */
    {
        XByteArray* a = XByteArray_create_utf8("Hello");
        XByteArray* b = XByteArray_create_utf8("hello");
        XByteArray* c = XByteArray_create_utf8("HELLO");
        XPrintf("  compareCS(Hello,hello,cs=1) = %d (期望 <0)\n", XByteArray_compareCS(a, b, 1));
        XPrintf("  compareCS(Hello,hello,cs=0) = %d (期望 0)\n",  XByteArray_compareCS(a, b, 0));
        XPrintf("  compareCI(Hello,HELLO)      = %d (期望 0)\n",  XByteArray_compareCI(a, c));
        XByteArray_delete_base(a);
        XByteArray_delete_base(b);
        XByteArray_delete_base(c);
    }
}

/* ==================== 委托给 XByteArrayView 验证 ==================== */
static void XByteArrayTest_ViewDelegation(void)
{
    XPrintf("\n[XByteArray][View委托验证] 验证委托给 XByteArrayView 的函数行为一致性\n");

    /* 准备测试数据 */
    XByteArray* ba = XByteArray_create_utf8("  Hello World!  ");
    XByteArray* empty = XByteArray_create();
    XByteArray* num = XByteArray_create_utf8("  -1234  ");
    XByteArray* hex = XByteArray_create_utf8("FF");
    XByteArray* fp = XByteArray_create_utf8("3.14159");
    XByteArray* a = XByteArray_create_utf8("Hello");
    XByteArray* b = XByteArray_create_utf8("hello");

    /* 直接创建 View 作为参考 */
    XByteArrayView refView = XByteArrayView_create_bytearray(ba);
    XByteArrayView emptyView = XByteArrayView_create_bytearray(empty);

    /* ---- left/right/mid 委托验证 ---- */
    {
        XByteArray* l = XByteArray_left(ba, 5);
        XByteArrayView lv = XByteArrayView_first_n(&refView, 5);
        bool l_ok = (l && XByteArray_size_base(l) == (size_t)lv.m_size
            && memcmp(XByteArray_data(l), lv.m_data, (size_t)lv.m_size) == 0);
        XPrintf("  left(5) 委托验证: %s (size=%zu, 期望 %lld)\n",
            l_ok ? "通过" : "失败", l ? XByteArray_size_base(l) : 0, (long long)lv.m_size);
        XByteArray_delete_base(l);
    }
    {
        XByteArray* r = XByteArray_right(ba, 6);
        XByteArrayView rv = XByteArrayView_last_n(&refView, 6);
        bool r_ok = (r && XByteArray_size_base(r) == (size_t)rv.m_size
            && memcmp(XByteArray_data(r), rv.m_data, (size_t)rv.m_size) == 0);
        XPrintf("  right(6) 委托验证: %s (size=%zu, 期望 %lld)\n",
            r_ok ? "通过" : "失败", r ? XByteArray_size_base(r) : 0, (long long)rv.m_size);
        XByteArray_delete_base(r);
    }
    {
        XByteArray* m = XByteArray_mid(ba, 2, 5);
        XByteArrayView mv = XByteArrayView_mid(&refView, 2, 5);
        bool m_ok = (m && XByteArray_size_base(m) == (size_t)mv.m_size
            && memcmp(XByteArray_data(m), mv.m_data, (size_t)mv.m_size) == 0);
        XPrintf("  mid(2,5) 委托验证: %s (data=%.*s, 期望 %.*s)\n",
            m_ok ? "通过" : "失败",
            (int)(m ? XByteArray_size_base(m) : 0), (const char*)(m ? XByteArray_data(m) : (uint8_t*)"NULL"),
            (int)mv.m_size, (const char*)mv.m_data);
        XByteArray_delete_base(m);
    }
    /* ---- trimmed 委托验证 ---- */
    {
        XByteArray* t = XByteArray_trimmed(ba);
        XByteArrayView tv = XByteArrayView_trimmed(&refView);
        bool t_ok = (t && XByteArray_size_base(t) == (size_t)tv.m_size
            && memcmp(XByteArray_data(t), tv.m_data, (size_t)tv.m_size) == 0);
        XPrintf("  trimmed() 委托验证: %s (data=%.*s, 期望 %.*s)\n",
            t_ok ? "通过" : "失败",
            (int)(t ? XByteArray_size_base(t) : 0), (const char*)(t ? XByteArray_data(t) : (uint8_t*)"NULL"),
            (int)tv.m_size, (const char*)tv.m_data);
        XByteArray_delete_base(t);
    }
    /* ---- trimmed(empty) 委托验证 ---- */
    {
        XByteArray* t = XByteArray_trimmed(empty);
        XByteArrayView tv = XByteArrayView_trimmed(&emptyView);
        bool t_ok = (t && XByteArray_size_base(t) == (size_t)tv.m_size);
        XPrintf("  trimmed(empty) 委托验证: %s (size=%zu, 期望 %lld)\n",
            t_ok ? "通过" : "失败", t ? XByteArray_size_base(t) : 0, (long long)tv.m_size);
        XByteArray_delete_base(t);
    }
    /* ---- compare 委托验证 ---- */
    {
        int32_t cmp = XByteArray_compare(ba, ba);
        XByteArrayView bv = XByteArrayView_create_bytearray(ba);
        int32_t cmp_ref = XByteArrayView_compare(&bv, &bv, 1);
        XPrintf("  compare(self,self) 委托验证: %s (got %d, 期望 %d)\n",
            cmp == cmp_ref ? "通过" : "失败", cmp, cmp_ref);
    }
    /* ---- compareCS 委托验证 ---- */
    {
        int32_t cs1 = XByteArray_compareCS(a, b, 1);
        int32_t cs0 = XByteArray_compareCS(a, b, 0);
        XByteArrayView av = XByteArrayView_create_bytearray(a);
        XByteArrayView bv = XByteArrayView_create_bytearray(b);
        int32_t cs1_ref = XByteArrayView_compare(&av, &bv, 1);
        int32_t cs0_ref = XByteArrayView_compare(&av, &bv, 0);
        XPrintf("  compareCS(cs=1) 委托验证: %s (got %d, 期望 %d)\n",
            cs1 == cs1_ref ? "通过" : "失败", cs1, cs1_ref);
        XPrintf("  compareCS(cs=0) 委托验证: %s (got %d, 期望 %d)\n",
            cs0 == cs0_ref ? "通过" : "失败", cs0, cs0_ref);
    }
    /* ---- toLongLong 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        int64_t v1 = XByteArray_toLongLong(num, &ok1, 10);
        XByteArrayView nv = XByteArrayView_create_bytearray(num);
        int64_t v2 = XByteArrayView_toLongLong(&nv, &ok2, 10);
        XPrintf("  toLongLong 委托验证: %s (got %lld ok=%d, 期望 %lld ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败",
            (long long)v1, ok1, (long long)v2, ok2);
    }
    /* ---- toInt 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        int32_t v1 = XByteArray_toInt(num, &ok1, 10);
        XByteArrayView nv = XByteArrayView_create_bytearray(num);
        int32_t v2 = XByteArrayView_toInt(&nv, &ok2, 10);
        XPrintf("  toInt 委托验证: %s (got %d ok=%d, 期望 %d ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toInt(hex) 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        int32_t v1 = XByteArray_toInt(hex, &ok1, 16);
        XByteArrayView hv = XByteArrayView_create_bytearray(hex);
        int32_t v2 = XByteArrayView_toInt(&hv, &ok2, 16);
        XPrintf("  toInt(hex,16) 委托验证: %s (got %d ok=%d, 期望 %d ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toDouble 委托验证 ---- */
    {
        bool ok1 = false, ok2 = false;
        double v1 = XByteArray_toDouble(fp, &ok1);
        XByteArrayView fv = XByteArrayView_create_bytearray(fp);
        double v2 = XByteArrayView_toDouble(&fv, &ok2);
        XPrintf("  toDouble 委托验证: %s (got %f ok=%d, 期望 %f ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }
    /* ---- toInt(NULL) 委托验证 ---- */
    {
        bool ok1 = false;
        int32_t v1 = XByteArray_toInt(NULL, &ok1, 10);
        XByteArrayView nv = XByteArrayView_create_bytearray(NULL);
        bool ok2 = false;
        int32_t v2 = XByteArrayView_toInt(&nv, &ok2, 10);
        XPrintf("  toInt(NULL) 委托验证: %s (got %d ok=%d, 期望 %d ok=%d)\n",
            (v1 == v2 && ok1 == ok2) ? "通过" : "失败", v1, ok1, v2, ok2);
    }

    /* 清理 */
    XByteArray_delete_base(ba);
    XByteArray_delete_base(empty);
    XByteArray_delete_base(num);
    XByteArray_delete_base(hex);
    XByteArray_delete_base(fp);
    XByteArray_delete_base(a);
    XByteArray_delete_base(b);
}

static void XByteArrayTest_All(void)

{
#if XByteArray_ON
    XPrintf("XByteArray 全量测试 (Qt QByteArray 对齐)\n");
    XByteArrayTest_Basic();
    XByteArrayTest_QtAliases();
    XByteArrayTest_QtNewApis();
    XByteArrayTest_Codec();
    XByteArrayTest_QtHeavy();
    XByteArrayTest_Stress();
    XPrintf("\n[XByteArray] 全部测试完成\n");
#endif
    //XCoreApplication_quit();
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
