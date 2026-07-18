#include"XDataStructTest.h"
#if DEMOTEST
#include"XSet.h"
#include"XVector.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"

/* ==================== 内部工具 ==================== */
static void PrintKey(void* pvKey, void* args)
{
    (void)args;
    XPrintf("  key=%d\n", *(int*)pvKey);
}

static void SumKeys(void* pvKey, void* args)
{
    *(long long*)args += *(int*)pvKey;
}

/* ==================== 基础测试：insert/remove/find/contains/迭代 ==================== */
static void XSetTest_Basic(void)
{
    XPrintf("\n[XSet][基础] insert/remove/find/contains/迭代\n");
    XSet* set = XSet_Create(int, int_compare);

    int arr[] = { 10, 3, 55, 3, 7, 55, 42, 1 };
    for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); ++i)
        XSet_insert_base(set, &arr[i]);

    XPrintf("  size=%zu (期望 6)\n", XSet_size_base(set));
    XPrintf("  isEmpty=%d\n", XSet_isEmpty_base(set));

    int k = 55;
    XPrintf("  contains(55)=%d\n", XSet_contains(set, &k));
    k = 999;
    XPrintf("  contains(999)=%d\n", XSet_contains(set, &k));

    XSet_iterator it = { 0 };
    k = 42;
    if (XSet_find_base(set, &k, &it))
        XPrintf("  find(42) 成功\n");

    k = 3;
    XSet_remove_base(set, &k);
    XPrintf("  remove(3) 后 size=%zu (期望 5)\n", XSet_size_base(set));

    XPrintf("  正向遍历:\n");
    XSet_iterator_for_each(set, PrintKey, NULL);

    XSet_clear_base(set);
    XPrintf("  clear 后 size=%zu (期望 0)\n", XSet_size_base(set));
    XSet_delete_base(set);
}

/* ==================== Qt 命名对齐测试 ==================== */
static void XSetTest_QtAliases(void)
{
    XPrintf("\n[XSet][Qt 别名] count/empty/values 与 size/isEmpty/keys 等价\n");
    XSet* set = XSet_Create(int, int_compare);
    for (int i = 0; i < 20; ++i) XSet_insert_base(set, &i);

    size_t s1 = XSet_size_base(set), s2 = XSet_count_base(set);
    bool e1 = XSet_isEmpty_base(set), e2 = XSet_empty_base(set);
    XPrintf("  size=%zu count=%zu (%s)\n", s1, s2, (s1==s2?"OK":"FAIL"));
    XPrintf("  isEmpty=%d empty=%d (%s)\n", e1, e2, (e1==e2?"OK":"FAIL"));

    XVector* keys = XSet_keys_base(set);
    XVector* vals = XSet_values_base(set);
    XPrintf("  keys.size=%zu values.size=%zu (%s)\n",
        XVector_size_base(keys), XVector_size_base(vals),
        (XVector_size_base(keys)==XVector_size_base(vals)?"OK":"FAIL"));
    XVector_delete_base(keys);
    XVector_delete_base(vals);
    XSet_delete_base(set);
}

/* ==================== 压力/穿透测试 ==================== */
/* ==================== 新增 Qt API：constFind / removeIf / erase_if / reserve / squeeze ==================== */
static bool IsGreaterThan100(const void* pvKey, void* args)
{
    (void)args;
    return *(const int*)pvKey > 100;
}
static bool IsEven(const void* pvKey, void* args)
{
    (void)args;
    return (*(const int*)pvKey % 2) == 0;
}

static void XSetTest_QtNewApis(void)
{
    XPrintf("\n[XSet][Qt 新 API] constFind/removeIf/erase_if/reserve/squeeze\n");
    XSet* set = XSet_Create(int, int_compare);

    // reserve / squeeze：XSet 为红黑树，无操作但应返回成功
    bool r = XSet_reserve_base(set, 1000);
    XPrintf("  reserve(1000) = %d (期望 1 no-op)\n", (int)r);
    XSet_squeeze_base(set);
    XPrintf("  squeeze() 完成 (RB 树无操作)\n");

    for (int i = 0; i < 200; ++i) XSet_insert_base(set, &i);
    XPrintf("  插入 200 后 size=%zu\n", XSet_size_base(set));

    // constFind
    int k = 42;
    XSet_iterator it = {0};
    bool ok = XSet_constFind_base(set, &k, &it);
    XPrintf("  constFind(42) = %d\n", (int)ok);

    // removeIf: 删除 >100
    size_t removed = XSet_removeIf_base(set, IsGreaterThan100, NULL);
    XPrintf("  removeIf(>100) 删除=%zu (期望 99), 剩余=%zu (期望 101)\n",
        removed, XSet_size_base(set));

    // erase_if 别名: 删除偶数
    size_t rem2 = XSet_erase_if_base(set, IsEven, NULL);
    XPrintf("  erase_if(偶数) 删除=%zu, 剩余=%zu\n", rem2, XSet_size_base(set));

    XSet_delete_base(set);
}

static void XSetTest_Stress(void)
{
    XPrintf("\n[XSet][压力] 大批量 insert / remove / contains\n");
    XSet* set = XSet_Create(int, int_compare);
    const int N = 5000;
    for (int i = 0; i < N; ++i) XSet_insert_base(set, &i);
    for (int i = 0; i < N; ++i) XSet_insert_base(set, &i); // 重复插入
    XPrintf("  插入 %d 唯一元素后 size=%zu\n", N, XSet_size_base(set));

    int miss = 0, hit = 0;
    for (int i = 0; i < N; ++i)
        if (XSet_contains(set, &i)) ++hit; else ++miss;
    XPrintf("  contains 命中=%d 缺失=%d\n", hit, miss);

    for (int i = 0; i < N; i += 2) XSet_remove_base(set, &i);
    XPrintf("  移除偶数后 size=%zu (期望 %d)\n", XSet_size_base(set), N/2);

    long long sum = 0;
    XSet_iterator_for_each(set, SumKeys, &sum);
    XPrintf("  遍历求和 sum=%lld\n", sum);

    XSet_delete_base(set);
}

/* ==================== 主入口 ==================== */
static void XSetTest_All(void)
{
#if XHashMap_ON
    XPrintf("XSet 全量测试 (Qt QSet 对齐)\n");
    XSetTest_Basic();
    XSetTest_QtAliases();
    XSetTest_QtNewApis();
    XSetTest_Stress();
    XPrintf("\n[XSet] 全部测试完成\n");
#endif
    XCoreApplication_quit();
}

void XSetTest(void)
{
    XSetTest_All();
}

void XMenu_XSetTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XSet(有序集合)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "全量测试(基础+Qt别名+压力)");
        XAction_setAction(action, XSetTest_All);
    }
}
#endif
