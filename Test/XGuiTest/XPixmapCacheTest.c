#include "XGuiTest.h"
#if DEMOTEST
#include "XPixmapCache.h"
#include "XPixmap.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <assert.h>

/* ==================== XPixmapCache 测试 ==================== */

/**
 * @brief      缓存键测试
 */
static void XPixmapCacheKeyTest(void)
{
    XPrintf("===== 缓存键测试 =====\n");
    /* init / isValid */
    {
        XPixmapCacheKey key;
        XPixmapCacheKey_init(&key);
        XPrintf("init: isValid=%s (期望:否)\n", XPixmapCacheKey_isValid(&key) ? "是" : "否");
        XPixmapCacheKey_deinit(&key);
    }
    /* copy */
    {
        XPixmapCacheKey a, b;
        XPixmapCacheKey_init(&a);
        XPixmapCacheKey_init(&b);
        XPixmapCacheKey_copy(&b, &a);
        XPrintf("copy: a.isValid=%s, b.isValid=%s\n",
            XPixmapCacheKey_isValid(&a) ? "是" : "否",
            XPixmapCacheKey_isValid(&b) ? "是" : "否");
        XPixmapCacheKey_deinit(&a);
        XPixmapCacheKey_deinit(&b);
    }
    /* equals */
    {
        XPixmapCacheKey a, b;
        XPixmapCacheKey_init(&a);
        XPixmapCacheKey_init(&b);
        XPrintf("equals: %s (期望:是,两个空键)\n", XPixmapCacheKey_equals(&a, &b) ? "是" : "否");
        XPixmapCacheKey_deinit(&a);
        XPixmapCacheKey_deinit(&b);
    }
    /* swap */
    {
        XPixmapCacheKey a, b;
        XPixmapCacheKey_init(&a);
        XPixmapCacheKey_init(&b);
        XPixmapCacheKey_swap(&a, &b);
        XPrintf("swap: a.isValid=%s, b.isValid=%s\n",
            XPixmapCacheKey_isValid(&a) ? "是" : "否",
            XPixmapCacheKey_isValid(&b) ? "是" : "否");
        XPixmapCacheKey_deinit(&a);
        XPixmapCacheKey_deinit(&b);
    }
    XPrintf("\n");
}

static void XPixmapCacheRegressionTest(void)
{
    int oldLimit = XPixmapCache_cacheLimit();
    XPixmap p10, p20, p32, out;
    XPixmapCacheKey key, copy;
    XPixmap_init_ex(&p10, 10, 10);
    XPixmap_init_ex(&p20, 20, 20);
    XPixmap_init_ex(&p32, 32, 32);
    XPixmap_init(&out);
    XPixmapCacheKey_init(&key);
    XPixmapCacheKey_init(&copy);
    assert(!XPixmapCacheKey_isValid(&key));
    XPixmapCache_setCacheLimit(64);
    assert(XPixmapCache_insertKey(&p10, &key));
    XPixmapCacheKey_copy(&copy, &key);
    XPixmapCache_removeKey(&key);
    assert(!XPixmapCacheKey_isValid(&key) && !XPixmapCacheKey_isValid(&copy));
    assert(XPixmapCache_insert("duplicate", &p10));
    assert(XPixmapCache_insert("duplicate", &p20));
    assert(XPixmapCache_find("duplicate", &out) && XPixmap_width(&out) == 20);
    XPixmapCache_clear();
    XPixmapCache_setCacheLimit(8);
    assert(XPixmapCache_insert("a", &p32));
    assert(XPixmapCache_insert("b", &p32));
    assert(XPixmapCache_find("a", NULL));
    assert(XPixmapCache_insert("c", &p32));
    assert(XPixmapCache_find("a", NULL));
    assert(!XPixmapCache_find("b", NULL));
    assert(XPixmapCache_find("c", NULL));
    XPixmapCache_setCacheLimit(-1);
    assert(!XPixmapCache_find("a", NULL));
    assert(!XPixmapCache_insert("rejected", &p10));
    XPixmapCache_clear();
    XPixmapCache_setCacheLimit(oldLimit);
    XPixmapCacheKey_deinit(&copy);
    XPixmapCacheKey_deinit(&key);
    XPixmap_deinit_base(&out);
    XPixmap_deinit_base(&p32);
    XPixmap_deinit_base(&p20);
    XPixmap_deinit_base(&p10);
    XPrintf("XPixmapCache lifecycle/LRU regression: PASS\n");
}

/**
 * @brief      缓存操作测试
 */
static void XPixmapCacheOperationTest(void)
{
    XPrintf("===== 缓存操作测试 =====\n");
    int oldLimit = XPixmapCache_cacheLimit();
    XPrintf("default cacheLimit=%d KB\n", oldLimit);
    /* 设置缓存限制 */
    XPixmapCache_setCacheLimit(2048);
    XPrintf("setCacheLimit(2048): limit=%d (期望:2048)\n", XPixmapCache_cacheLimit());
    /* 插入字符串键 */
    {
        XPixmap p;
        XPixmap_init_ex(&p, 100, 100);
        bool ok = XPixmapCache_insert("test_key", &p);
        XPrintf("insert('test_key'): %s (期望:是)\n", ok ? "是" : "否");
        XPixmap_deinit_base(&p);
    }
    /* 查找字符串键 */
    {
        XPixmap out;
        XPixmap_init(&out);
        bool found = XPixmapCache_find("test_key", &out);
        XPrintf("find('test_key'): found=%s (期望:是), w=%d (期望:100)\n",
            found ? "是" : "否", XPixmap_width(&out));
        XPixmap_deinit_base(&out);
    }
    /* 查找不存在的键 */
    {
        XPixmap out;
        XPixmap_init(&out);
        bool found = XPixmapCache_find("nonexistent", &out);
        XPrintf("find('nonexistent'): found=%s (期望:否)\n", found ? "是" : "否");
        XPixmap_deinit_base(&out);
    }
    /* 插入 Key 对象 */
    {
        XPixmap p;
        XPixmap_init_ex(&p, 50, 50);
        XPixmapCacheKey key;
        XPixmapCacheKey_init(&key);
        bool ok = XPixmapCache_insertKey(&p, &key);
        XPrintf("insertKey(50x50): ok=%s (期望:是), key.isValid=%s\n",
            ok ? "是" : "否", XPixmapCacheKey_isValid(&key) ? "是" : "否");
        /* 查找 */
        {
            XPixmap out;
            XPixmap_init(&out);
            bool found = XPixmapCache_findKey(&key, &out);
            XPrintf("findKey: found=%s (期望:是), w=%d\n", found ? "是" : "否", XPixmap_width(&out));
            XPixmap_deinit_base(&out);
        }
        /* 替换 */
        {
            XPixmap p2;
            XPixmap_init_ex(&p2, 80, 80);
            bool replaced = XPixmapCache_replace(&key, &p2);
            XPrintf("replace(80x80): ok=%s (期望:是)\n", replaced ? "是" : "否");
            XPixmap_deinit_base(&p2);
        }
        /* 通过 Key 移除 */
        XPixmapCache_removeKey(&key);
        {
            XPixmap out;
            XPixmap_init(&out);
            bool found = XPixmapCache_findKey(&key, &out);
            XPrintf("removeKey后 findKey: found=%s (期望:否)\n", found ? "是" : "否");
            XPixmap_deinit_base(&out);
        }
        XPixmapCacheKey_deinit(&key);
        XPixmap_deinit_base(&p);
    }
    /* 移除字符串键 */
    XPixmapCache_remove("test_key");
    {
        XPixmap out;
        XPixmap_init(&out);
        bool found = XPixmapCache_find("test_key", &out);
        XPrintf("remove后 find('test_key'): found=%s (期望:否)\n", found ? "是" : "否");
        XPixmap_deinit_base(&out);
    }
    /* 清空 */
    {
        XPixmap p;
        XPixmap_init_ex(&p, 10, 10);
        XPixmapCache_insert("key1", &p);
        XPixmapCache_insert("key2", &p);
        XPixmap_deinit_base(&p);
        XPixmapCache_clear();
        {
            XPixmap out;
            XPixmap_init(&out);
            XPrintf("clear后 find('key1'): found=%s (期望:否)\n", XPixmapCache_find("key1", &out) ? "是" : "否");
            XPixmap_deinit_base(&out);
        }
    }
    /* 恢复缓存限制 */
    XPixmapCache_setCacheLimit(oldLimit);
    XPrintf("\n");
}

/**
 * @brief      XPixmapCache 综合测试入口
 */
void XMenu_XPixmapCacheTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XPixmapCache 缓存类");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "缓存键测试");
    XAction_setAction(action, XPixmapCacheKeyTest);

    action = XMenu_addAction(menu, "缓存操作测试");
    XAction_setAction(action, XPixmapCacheOperationTest);

    action = XMenu_addAction(menu, "生命周期与LRU回归测试");
    XAction_setAction(action, XPixmapCacheRegressionTest);
}
#endif // DEMOTEST


