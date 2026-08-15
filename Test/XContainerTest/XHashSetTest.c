#include"XDataStructTest.h"
#if DEMOTEST
#include"XHashSet.h"
#include"XVector.h"
#include"XCompare.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"

static void PrintKey(void* pvKey, void* args)
{
    (void)args;
    XPrintf("  key=%d\n", *(int*)pvKey);
}

static void CountEach(void* pvKey, void* args)
{
    (void)pvKey;
    (*(size_t*)args)++;
}

/* ==================== 基础测试 ==================== */
static void XHashSetTest_Basic(void)
{
    XPrintf("\n[XHashSet][基础] insert/remove/find/contains/迭代\n");
    XHashSet* set = XHashSet_Create(int, int_compare);

    int arr[] = { 10, 3, 55, 3, 7, 55, 42, 1 };
    for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); ++i)
        XHashSet_insert_base(set, &arr[i]);

    XPrintf("  size=%zu (期望 6)\n", XHashSet_size_base(set));

    int k = 42;
    XPrintf("  contains(42)=%d\n", XHashSet_contains(set, &k));
    k = 999;
    XPrintf("  contains(999)=%d\n", XHashSet_contains(set, &k));

    k = 3;
    XHashSet_remove_base(set, &k);
    XPrintf("  remove(3) 后 size=%zu (期望 5)\n", XHashSet_size_base(set));

    XPrintf("  遍历:\n");
    XHashSet_iterator_for_each(set, PrintKey, NULL);

    XHashSet_clear_base(set);
    XPrintf("  clear 后 size=%zu\n", XHashSet_size_base(set));
    XHashSet_delete_base(set);
}

/* ==================== Qt 命名对齐 ==================== */
static void XHashSetTest_QtAliases(void)
{
    XPrintf("\n[XHashSet][Qt 别名] count/empty/values ⇔ size/isEmpty/keys\n");
    XHashSet* set = XHashSet_Create(int, int_compare);
    for (int i = 0; i < 30; ++i) XHashSet_insert_base(set, &i);

    size_t s1 = XHashSet_size_base(set), s2 = XHashSet_count_base(set);
    bool e1 = XHashSet_isEmpty_base(set), e2 = XHashSet_empty_base(set);
    XPrintf("  size=%zu count=%zu (%s)\n", s1, s2, (s1==s2?"OK":"FAIL"));
    XPrintf("  isEmpty=%d empty=%d (%s)\n", e1, e2, (e1==e2?"OK":"FAIL"));

    XVector* keys = XHashSet_keys_base(set);
    XVector* vals = XHashSet_values_base(set);
    XPrintf("  keys.size=%zu values.size=%zu (%s)\n",
        XVector_size_base(keys), XVector_size_base(vals),
        (XVector_size_base(keys)==XVector_size_base(vals)?"OK":"FAIL"));
    XVector_delete_base(keys);
    XVector_delete_base(vals);
    XHashSet_delete_base(set);
}

/* ==================== 压力测试 ==================== */
/* ==================== 新增 Qt API：constFind / removeIf / erase_if / reserve / squeeze ==================== */
static bool IsGreaterThan100(const void* pvKey, void* args)
{
    (void)args;
    return *(const int*)pvKey > 100;
}
static bool IsOdd(const void* pvKey, void* args)
{
    (void)args;
    return (*(const int*)pvKey % 2) != 0;
}

static void XHashSetTest_QtNewApis(void)
{
    XPrintf("\n[XHashSet][Qt 新 API] constFind/removeIf/erase_if/reserve/squeeze\n");
    XHashSet* set = XHashSet_Create(int, int_compare);

    // reserve：预分配到 1000 应扩桶
    bool r = XHashSet_reserve_base(set, 1000);
    size_t cap_after_reserve = XHashSet_capacity_base(set);
    XPrintf("  reserve(1000) = %d, capacity=%zu (期望 >= 1334/0.75 上取 2^n)\n",
        (int)r, cap_after_reserve);

    for (int i = 0; i < 200; ++i) XHashSet_insert_base(set, &i);
    XPrintf("  插入 200 后 size=%zu capacity=%zu\n",
        XHashSet_size_base(set), XHashSet_capacity_base(set));

    // constFind
    int k = 42;
    XHashSet_iterator it = {0};
    bool ok = XHashSet_constFind_base(set, &k, &it);
    XPrintf("  constFind(42) = %d\n", (int)ok);

    // removeIf: 删除 >100
    size_t removed = XHashSet_removeIf_base(set, IsGreaterThan100, NULL);
    XPrintf("  removeIf(>100) 删除=%zu (期望 99), 剩余=%zu (期望 101)\n",
        removed, XHashSet_size_base(set));

    // erase_if 别名: 删除奇数
    size_t rem2 = XHashSet_erase_if_base(set, IsOdd, NULL);
    XPrintf("  erase_if(奇数) 删除=%zu, 剩余=%zu\n", rem2, XHashSet_size_base(set));

    // squeeze：收缩至最紧凑桶数
    size_t cap_before_sq = XHashSet_capacity_base(set);
    XHashSet_squeeze_base(set);
    size_t cap_after_sq = XHashSet_capacity_base(set);
    XPrintf("  squeeze 前 cap=%zu, 后 cap=%zu (%s)\n",
        cap_before_sq, cap_after_sq,
        (cap_after_sq <= cap_before_sq ? "OK" : "FAIL"));

    XHashSet_delete_base(set);
}

static void XHashSetTest_Stress(void)
{
    XPrintf("\n[XHashSet][压力] 大批量 insert/remove/contains/迭代\n");
    XHashSet* set = XHashSet_Create(int, int_compare);
    const int N = 5000;
    for (int i = 0; i < N; ++i) XHashSet_insert_base(set, &i);
    for (int i = 0; i < N; ++i) XHashSet_insert_base(set, &i);
    XPrintf("  插入 %d 唯一后 size=%zu\n", N, XHashSet_size_base(set));

    int hit = 0;
    for (int i = 0; i < N; ++i)
        if (XHashSet_contains(set, &i)) ++hit;
    XPrintf("  contains 命中=%d (期望 %d)\n", hit, N);

    for (int i = 1; i < N; i += 2) XHashSet_remove_base(set, &i);
    XPrintf("  移除奇数后 size=%zu (期望 %d)\n", XHashSet_size_base(set), N/2);

    size_t cnt = 0;
    XHashSet_iterator_for_each(set, CountEach, &cnt);
    XPrintf("  遍历统计=%zu\n", cnt);

    XHashSet_delete_base(set);
}

/* ==================== 主入口 ==================== */
static void XHashSetTest_All(void)
{
#if XHashMap_ON
    XPrintf("XHashSet 全量测试 (Qt QSet 对齐)\n");
    XHashSetTest_Basic();
    XHashSetTest_QtAliases();
    XHashSetTest_QtNewApis();
    XHashSetTest_Stress();
    XPrintf("\n[XHashSet] 全部测试完成\n");
#endif
    //XCoreApplication_quit();
}

void XHashSetTest(void)
{
    XHashSetTest_All();
}

void XMenu_XHashSetTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XHashSet(无序集合)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全量测试(基础+Qt别名+压力)");
        XAction_setAction(action, XHashSetTest_All);
    }
}
#endif
