#include"XDataStructTest.h"
#if DEMOTEST
#include"XMap.h"
#include"XVector.h"
#include"XPair.h"
#include"XCompare.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include<string.h>

/* ==================== 内部工具 ==================== */
static void PrintPair(void* pvPair, void* args)
{
    (void)args;
    XPair* pair = (XPair*)pvPair;
    XPrintf("  key=%d val=%d\n", XPair_First(pair, int), XPair_Second(pair, int));
}
static void SumValues(void* pvPair, void* args)
{
    XPair* pair = (XPair*)pvPair;
    *(long long*)args += XPair_Second(pair, int);
}

/* ==================== 基础测试 ==================== */
static void XMapTest_Basic(void)
{
    XPrintf("\n[XMap][基础] insert/remove/find/value/contains/迭代\n");
    XMap* map = XMap_Create(int, int, int_compare);

    int keys[] = { 10, 3, 55, 3, 7, 55, 42, 1 };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i) {
        int v = keys[i] * 10;
        XMap_insert_base(map, &keys[i], &v);
    }
    XPrintf("  size=%zu (期望 6)\n", XMap_size_base(map));
    XPrintf("  isEmpty=%d\n", XMap_isEmpty_base(map));

    int k = 55;
    XPrintf("  contains(55)=%d\n", XMapBase_contains(map, &k));
    k = 999;
    XPrintf("  contains(999)=%d\n", XMapBase_contains(map, &k));

    k = 42;
    int* pv = (int*)XMap_value_base(map, &k);
    XPrintf("  value(42)=%d (期望 420)\n", pv ? *pv : -1);

    XMap_iterator it = { 0 };
    if (XMap_find_base(map, &k, &it))
        XPrintf("  find(42) 成功\n");

    k = 3;
    XMap_remove_base(map, &k);
    XPrintf("  remove(3) 后 size=%zu (期望 5)\n", XMap_size_base(map));

    XPrintf("  正向遍历:\n");
    XMap_iterator_for_each(map, PrintPair, NULL);

    XMap_clear_base(map);
    XPrintf("  clear 后 size=%zu (期望 0)\n", XMap_size_base(map));
    XMap_delete_base(map);
}

/* ==================== Qt 命名对齐 ==================== */
static void XMapTest_QtAliases(void)
{
    XPrintf("\n[XMap][Qt 别名] count/empty/constFind 与 size/isEmpty/find 等价\n");
    XMap* map = XMap_Create(int, int, int_compare);
    for (int i = 0; i < 20; ++i) { int v = i*i; XMap_insert_base(map, &i, &v); }

    size_t s1 = XMap_size_base(map), s2 = XMap_count_base(map);
    bool e1 = XMap_isEmpty_base(map), e2 = XMap_empty_base(map);
    XPrintf("  size=%zu count=%zu (%s)\n", s1, s2, (s1==s2?"OK":"FAIL"));
    XPrintf("  isEmpty=%d empty=%d (%s)\n", e1, e2, (e1==e2?"OK":"FAIL"));

    int k = 7;
    XMap_iterator it = {0};
    bool f1 = XMap_find_base(map, &k, &it);
    bool f2 = XMap_constFind_base(map, &k, &it);
    XPrintf("  find=%d constFind=%d (%s)\n", (int)f1, (int)f2, (f1==f2?"OK":"FAIL"));

    XVector* keys = XMapBase_keys_base(map);
    XVector* vals = XMapBase_values_base(map);
    XPrintf("  keys.size=%zu values.size=%zu (%s)\n",
        XVector_size_base(keys), XVector_size_base(vals),
        (XVector_size_base(keys)==XVector_size_base(vals)?"OK":"FAIL"));
    XVector_delete_base(keys);
    XVector_delete_base(vals);
    XMap_delete_base(map);
}

/* ==================== 新 API：removeIf / erase_if / reserve / squeeze ==================== */
static bool ValIsOdd(const void* pvKey, void* pvValue, void* args)
{
    (void)pvKey; (void)args;
    return ((*(int*)pvValue) & 1) == 1;
}
static bool KeyGT100(const void* pvKey, void* pvValue, void* args)
{
    (void)pvValue; (void)args;
    return *(const int*)pvKey > 100;
}

static void XMapTest_QtNewApis(void)
{
    XPrintf("\n[XMap][Qt 新 API] removeIf/erase_if/reserve/squeeze(no-op)\n");
    XMap* map = XMap_Create(int, int, int_compare);

    bool r = XMap_reserve_base(map, 1000);
    XPrintf("  reserve(1000)=%d (期望 1 no-op)\n", (int)r);
    XMap_squeeze_base(map);
    XPrintf("  squeeze() 完成 (红黑树 no-op)\n");

    for (int i = 0; i < 200; ++i) { int v = i; XMap_insert_base(map, &i, &v); }
    XPrintf("  插入 200 后 size=%zu\n", XMap_size_base(map));

    size_t removed = XMap_removeIf_base(map, KeyGT100, NULL);
    XPrintf("  removeIf(key>100) 删除=%zu (期望 99), 剩余=%zu (期望 101)\n",
        removed, XMap_size_base(map));

    size_t rem2 = XMap_erase_if_base(map, ValIsOdd, NULL);
    XPrintf("  erase_if(val 为奇数) 删除=%zu, 剩余=%zu\n", rem2, XMap_size_base(map));

    XMap_delete_base(map);
}

/* ==================== 压力测试 ==================== */
static void XMapTest_Stress(void)
{
    XPrintf("\n[XMap][压力] 大批量 insert/value/remove\n");
    XMap* map = XMap_Create(int, int, int_compare);
    const int N = 5000;
    for (int i = 0; i < N; ++i) { int v = i * 3; XMap_insert_base(map, &i, &v); }
    XPrintf("  插入 %d 后 size=%zu\n", N, XMap_size_base(map));

    int miss = 0, hit = 0;
    for (int i = 0; i < N; ++i) {
        int* v = (int*)XMap_value_base(map, &i);
        if (v && *v == i * 3) ++hit; else ++miss;
    }
    XPrintf("  value 命中=%d 缺失=%d\n", hit, miss);

    for (int i = 0; i < N; i += 2) XMap_remove_base(map, &i);
    XPrintf("  移除偶数键后 size=%zu (期望 %d)\n", XMap_size_base(map), N/2);

    long long sum = 0;
    XMap_iterator_for_each(map, SumValues, &sum);
    XPrintf("  遍历求 val 和 sum=%lld\n", sum);

    XMap_delete_base(map);
}

/* ==================== 主入口 ==================== */
static void XMapTest_All(void)
{
#if XMap_ON
    XPrintf("XMap 全量测试 (Qt QMap 对齐)\n");
    XMapTest_Basic();
    XMapTest_QtAliases();
    XMapTest_QtNewApis();
    XMapTest_Stress();
    XPrintf("\n[XMap] 全部测试完成\n");
#endif
    XCoreApplication_quit();
}

void XMapTest(void)
{
    XMapTest_All();
}

void XMenu_XMapTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XMap(有序映射)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全量测试(基础+Qt别名+压力)");
        XAction_setAction(action, XMapTest_All);
    }
}
#endif
