#include"XDataStructTest.h"
#if DEMOTEST
#include"XHashMap.h"
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
static void XHashMapTest_Basic(void)
{
    XPrintf("\n[XHashMap][基础] insert/remove/find/value/contains/迭代\n");
    XHashMap* map = XHashMap_Create(int, int, int_compare);

    int keys[] = { 10, 3, 55, 3, 7, 55, 42, 1 };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i) {
        int v = keys[i] * 10;
        XHashMap_insert_base(map, &keys[i], &v);
    }
    XPrintf("  size=%zu (期望 6)\n", XHashMap_size_base(map));

    int k = 55;
    XPrintf("  contains(55)=%d\n", XMapBase_contains(map, &k));
    k = 999;
    XPrintf("  contains(999)=%d\n", XMapBase_contains(map, &k));

    k = 42;
    int* pv = (int*)XHashMap_value_base(map, &k);
    XPrintf("  value(42)=%d (期望 420)\n", pv ? *pv : -1);

    XHashMap_iterator it = { 0 };
    if (XHashMap_find_base(map, &k, &it))
        XPrintf("  find(42) 成功\n");

    k = 3;
    XHashMap_remove_base(map, &k);
    XPrintf("  remove(3) 后 size=%zu (期望 5)\n", XHashMap_size_base(map));

    XPrintf("  遍历:\n");
    XHashMap_iterator_for_each(map, PrintPair, NULL);

    XHashMap_clear_base(map);
    XPrintf("  clear 后 size=%zu (期望 0)\n", XHashMap_size_base(map));
    XHashMap_delete_base(map);
}

/* ==================== Qt 命名对齐 ==================== */
static void XHashMapTest_QtAliases(void)
{
    XPrintf("\n[XHashMap][Qt 别名] count/empty/constFind 与 size/isEmpty/find 等价\n");
    XHashMap* map = XHashMap_Create(int, int, int_compare);
    for (int i = 0; i < 20; ++i) { int v = i*i; XHashMap_insert_base(map, &i, &v); }

    size_t s1 = XHashMap_size_base(map), s2 = XHashMap_count_base(map);
    bool e1 = XHashMap_isEmpty_base(map), e2 = XHashMap_empty_base(map);
    XPrintf("  size=%zu count=%zu (%s)\n", s1, s2, (s1==s2?"OK":"FAIL"));
    XPrintf("  isEmpty=%d empty=%d (%s)\n", e1, e2, (e1==e2?"OK":"FAIL"));

    int k = 7;
    XHashMap_iterator it = {0};
    bool f1 = XHashMap_find_base(map, &k, &it);
    bool f2 = XHashMap_constFind_base(map, &k, &it);
    XPrintf("  find=%d constFind=%d (%s)\n", (int)f1, (int)f2, (f1==f2?"OK":"FAIL"));

    XVector* keys = XMapBase_keys_base(map);
    XVector* vals = XMapBase_values_base(map);
    XPrintf("  keys.size=%zu values.size=%zu (%s)\n",
        XVector_size_base(keys), XVector_size_base(vals),
        (XVector_size_base(keys)==XVector_size_base(vals)?"OK":"FAIL"));
    XVector_delete_base(keys);
    XVector_delete_base(vals);
    XHashMap_delete_base(map);
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

static void XHashMapTest_QtNewApis(void)
{
    XPrintf("\n[XHashMap][Qt 新 API] removeIf/erase_if/reserve/squeeze\n");
    XHashMap* map = XHashMap_Create(int, int, int_compare);

    bool r = XHashMap_reserve_base(map, 1000);
    XPrintf("  reserve(1000)=%d, capacity=%zu\n", (int)r, XHashMap_capacity_base(map));

    for (int i = 0; i < 200; ++i) { int v = i; XHashMap_insert_base(map, &i, &v); }
    XPrintf("  插入 200 后 size=%zu capacity=%zu\n",
        XHashMap_size_base(map), XHashMap_capacity_base(map));

    size_t removed = XHashMap_removeIf_base(map, KeyGT100, NULL);
    XPrintf("  removeIf(key>100) 删除=%zu (期望 99), 剩余=%zu (期望 101)\n",
        removed, XHashMap_size_base(map));

    size_t rem2 = XHashMap_erase_if_base(map, ValIsOdd, NULL);
    XPrintf("  erase_if(val 为奇数) 删除=%zu, 剩余=%zu\n", rem2, XHashMap_size_base(map));

    size_t cap_before = XHashMap_capacity_base(map);
    XHashMap_squeeze_base(map);
    size_t cap_after = XHashMap_capacity_base(map);
    XPrintf("  squeeze: capacity %zu -> %zu\n", cap_before, cap_after);

    XHashMap_delete_base(map);
}

/* ==================== 压力测试 ==================== */
static void XHashMapTest_Stress(void)
{
    XPrintf("\n[XHashMap][压力] 大批量 insert/value/remove\n");
    XHashMap* map = XHashMap_Create(int, int, int_compare);
    const int N = 5000;
    for (int i = 0; i < N; ++i) { int v = i * 3; XHashMap_insert_base(map, &i, &v); }
    XPrintf("  插入 %d 后 size=%zu capacity=%zu\n", N,
        XHashMap_size_base(map), XHashMap_capacity_base(map));

    int miss = 0, hit = 0;
    for (int i = 0; i < N; ++i) {
        int* v = (int*)XHashMap_value_base(map, &i);
        if (v && *v == i * 3) ++hit; else ++miss;
    }
    XPrintf("  value 命中=%d 缺失=%d\n", hit, miss);

    for (int i = 0; i < N; i += 2) XHashMap_remove_base(map, &i);
    XPrintf("  移除偶数键后 size=%zu\n", XHashMap_size_base(map));

    long long sum = 0;
    XHashMap_iterator_for_each(map, SumValues, &sum);
    XPrintf("  遍历求 val 和 sum=%lld\n", sum);

    XHashMap_delete_base(map);
}

/* ==================== 主入口 ==================== */
static void XHashMapTest_All(void)
{
#if XHashMap_ON
    XPrintf("XHashMap 全量测试 (Qt QHash 对齐)\n");
    XHashMapTest_Basic();
    XHashMapTest_QtAliases();
    XHashMapTest_QtNewApis();
    XHashMapTest_Stress();
    XPrintf("\n[XHashMap] 全部测试完成\n");
#endif
    //XCoreApplication_quit();
}

void XHashMapTest(void)
{
    XHashMapTest_All();
}

void XMenu_XHashMapTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XHashMap(无序映射)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全量测试(基础+Qt别名+压力)");
        XAction_setAction(action, XHashMapTest_All);
    }
}
#endif
