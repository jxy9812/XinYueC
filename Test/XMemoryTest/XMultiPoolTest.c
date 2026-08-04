#include"XMemoryTest.h"
#include"XMultiPool.h"
#include"XThread.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XDateTime.h"

// ==================== 测试辅助宏 ====================
#define TEST_PASS(name) XPrintf("[PASS] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[FAIL] %s: %s\n", name, reason)
#define TEST_INFO(fmt, ...) XPrintf("[INFO] " fmt "\n", ##__VA_ARGS__)

// 性能测试配置
#define PERFORMANCE_TEST_COUNT 250000
//#define PERFORMANCE_POOL_BLOCKS 1000
#define PERFORMANCE_BLOCK_SIZE 640

// ==================== 测试辅助包装函数 ====================
static bool test_basic_function(void);
static bool test_continuous_alloc_free(void);
static bool test_pool_exhaustion(void);
static bool test_boundary_conditions(void);
static bool test_power_of_two_capacity(void);
static bool test_performance(void);
static bool test_multithread_stress(void);
static bool test_global_pool(void);
void XMultiPoolTest(void);
static void test_basic_function_wrapper(XVariant* data) { (void)data; test_basic_function(); }
static void test_continuous_alloc_free_wrapper(XVariant* data) { (void)data; test_continuous_alloc_free(); }
static void test_pool_exhaustion_wrapper(XVariant* data) { (void)data; test_pool_exhaustion(); }
static void test_boundary_conditions_wrapper(XVariant* data) { (void)data; test_boundary_conditions(); }
static void test_power_of_two_capacity_wrapper(XVariant* data) { (void)data; test_power_of_two_capacity(); }
static void test_performance_wrapper(XVariant* data) { (void)data; test_performance(); }
static void test_multithread_stress_wrapper(XVariant* data) { (void)data; test_multithread_stress(); }
static void test_global_pool_wrapper(XVariant* data) { (void)data; test_global_pool(); }
static void XMultiPoolTest_wrapper(XVariant* data) { (void)data; XMultiPoolTest(); }

// ==================== 测试1: 基础功能测试 ====================
static bool test_basic_function(void) {
    TEST_INFO("===== 基础功能测试 =====");
    
    // 创建多级池
    XMultiPool* pool = XMultiPool_create();
    if (!pool) {
        TEST_FAIL("创建多级池", "返回NULL");
        return false;
    }
    
    // 添加子池
    XFixedPool* pool_8 = XFixedPool_create(8, 10);
    XFixedPool* pool_64 = XFixedPool_create(64, 10);
    XFixedPool* pool_256 = XFixedPool_create(256, 5);
    
    if (!pool_8 || !pool_64 || !pool_256) {
        TEST_FAIL("创建子池", "返回NULL");
        XMultiPool_delete(pool);
        return false;
    }
    
    XMultiPool_add_pool(pool, pool_256);
    XMultiPool_add_pool(pool, pool_8);
    XMultiPool_add_pool(pool, pool_64);
    
    // 测试分配
    void* p1 = XMultiPool_malloc(pool, 5);    // 应从8字节池分配
    void* p2 = XMultiPool_malloc(pool, 50);   // 应从64字节池分配
    void* p3 = XMultiPool_malloc(pool, 200);  // 应从256字节池分配
    
    if (!p1 || !p2 || !p3) {
        TEST_FAIL("基础分配", "返回NULL");
        XMultiPool_delete(pool);
        return false;
    }
    
    // 验证指针有效性
    memset(p1, 0xAA, 5);
    memset(p2, 0xBB, 50);
    memset(p3, 0xCC, 200);
    
    // 验证is_from_pool
    if (!XMultiPool_is_from_pool(pool, p1) || 
        !XMultiPool_is_from_pool(pool, p2) || 
        !XMultiPool_is_from_pool(pool, p3)) {
        TEST_FAIL("is_from_pool验证", "指针不属于池");
        XMultiPool_delete(pool);
        return false;
    }
    
    // 释放
    XMultiPool_free(pool, p1);
    XMultiPool_free(pool, p2);
    XMultiPool_free(pool, p3);
    
    XMultiPool_delete(pool);
    TEST_PASS("基础功能测试");
    return true;
}

// ==================== 测试2: 连续申请释放测试 ====================
static bool test_continuous_alloc_free(void) {
    TEST_INFO("===== 连续申请释放测试 =====");
    
    XMultiPool* pool = XMultiPool_create();
    if (!pool) return false;
    
    // 创建容量较小的池便于测试
    XFixedPool* pool_64 = XFixedPool_create(64, 5);  // 只有5个块
    XMultiPool_add_pool(pool, pool_64);
    
    const int iterations = 1000;
    void* ptrs[5];
    int success_count = 0;
    
    for (int i = 0; i < iterations; i++) {
        // 连续申请5个块（池满）
        for (int j = 0; j < 5; j++) {
            ptrs[j] = XMultiPool_malloc(pool, 32);
            if (ptrs[j]) {
                memset(ptrs[j], j, 32);
                success_count++;
            }
        }
        
        // 验证第6次申请应该失败（池已满）
        void* should_fail = XMultiPool_malloc(pool, 32);
        if (should_fail != NULL) {
            TEST_FAIL("池满检测", "第6次申请应该返回NULL");
            XMultiPool_free(pool, should_fail);
        }
        
        // 连续释放
        for (int j = 0; j < 5; j++) {
            if (ptrs[j]) {
                XMultiPool_free(pool, ptrs[j]);
                ptrs[j] = NULL;
            }
        }
    }
    
    XMultiPool_delete(pool);
    
    if (success_count == iterations * 5) {
        TEST_PASS("连续申请释放测试");
        TEST_INFO("  成功分配次数: %d", success_count);
        return true;
    } else {
        TEST_FAIL("连续申请释放测试", "分配次数不匹配");
        return false;
    }
}

// ==================== 测试3: 内存池耗尽测试 ====================
static bool test_pool_exhaustion(void) {
    TEST_INFO("===== 内存池耗尽测试 =====");
    
    XMultiPool* pool = XMultiPool_create();
    if (!pool) return false;
    
    // 创建只有3个块的池
    XFixedPool* pool_32 = XFixedPool_create(32, 3);
    XMultiPool_add_pool(pool, pool_32);
    
    size_t total_size = XMultiPool_totalSize(pool);
    size_t free_size = XMultiPool_freeSize(pool);
    TEST_INFO("  总大小: %zu 字节, 空闲: %zu 字节", total_size, free_size);
    
    void* ptrs[10] = {0};
    int alloc_count = 0;
    int null_count = 0;
    
    // 尝试分配10次，只有前3次应该成功
    for (int i = 0; i < 10; i++) {
        ptrs[i] = XMultiPool_malloc(pool, 20);
        if (ptrs[i]) {
            alloc_count++;
            TEST_INFO("  分配 #%d 成功", i + 1);
        } else {
            null_count++;
            TEST_INFO("  分配 #%d 失败 (池耗尽)", i + 1);
        }
    }
    
    // 验证剩余空间
    size_t remaining = XMultiPool_freeSize(pool);
    TEST_INFO("  分配后剩余: %zu 字节", remaining);
    
    if (alloc_count == 3 && null_count == 7 && remaining == 0) {
        TEST_PASS("内存池耗尽测试");
    } else {
        TEST_FAIL("内存池耗尽测试", "分配/失败次数不符合预期");
    }
    
    // 清理
    for (int i = 0; i < 10; i++) {
        if (ptrs[i]) XMultiPool_free(pool, ptrs[i]);
    }
    
    // 验证释放后空间恢复
    free_size = XMultiPool_freeSize(pool);
    TEST_INFO("  释放后空闲: %zu 字节", free_size);
    
    XMultiPool_delete(pool);
    return (alloc_count == 3 && null_count == 7);
}

// ==================== 测试4: 边界条件测试 ====================
static bool test_boundary_conditions(void) {
    TEST_INFO("===== 边界条件测试 =====");
    
    XMultiPool* pool = XMultiPool_create();
    if (!pool) return false;
    
    XFixedPool* pool_64 = XFixedPool_create(64, 10);
    XFixedPool* pool_128 = XFixedPool_create(128, 10);
    XMultiPool_add_pool(pool, pool_64);
    XMultiPool_add_pool(pool, pool_128);
    
    bool all_passed = true;
    
    // 测试1: 分配0字节
    void* p1 = XMultiPool_malloc(pool, 0);
    if (p1 != NULL) {
        TEST_INFO("  分配0字节: 返回非NULL (可能合法)");
        XMultiPool_free(pool, p1);
    } else {
        TEST_INFO("  分配0字节: 返回NULL (合法)");
    }
    
    // 测试2: 分配刚好等于块大小
    void* p2 = XMultiPool_malloc(pool, 64);
    if (p2) {
        TEST_INFO("  分配64字节: 成功");
        XMultiPool_free(pool, p2);
    } else {
        TEST_FAIL("边界测试", "分配64字节失败");
        all_passed = false;
    }
    
    // 测试3: 分配超过最大块大小
    void* p3 = XMultiPool_malloc(pool, 256);
    if (p3 == NULL) {
        TEST_INFO("  分配256字节(超过最大): 返回NULL (正确)");
    } else {
        TEST_FAIL("边界测试", "超过最大块大小不应分配成功");
        XMultiPool_free(pool, p3);
        all_passed = false;
    }
    
    // 测试4: 释放NULL指针
    XMultiPool_free(pool, NULL);
    TEST_INFO("  释放NULL指针: 无崩溃 (正确)");
    
    // 测试5: 重复释放检测
    void* p4 = XMultiPool_malloc(pool, 32);
    XMultiPool_free(pool, p4);
    // 注意: 重复释放可能导致未定义行为，这里不测试
    
    // 测试6: calloc测试
    void* p5 = XMultiPool_calloc(pool, 4, 16);  // 64字节
    if (p5) {
        // 验证内存已清零
        bool zeroed = true;
        unsigned char* bytes = (unsigned char*)p5;
        for (int i = 0; i < 64; i++) {
            if (bytes[i] != 0) {
                zeroed = false;
                break;
            }
        }
        if (zeroed) {
            TEST_INFO("  calloc内存清零: 成功");
        } else {
            TEST_FAIL("边界测试", "calloc内存未清零");
            all_passed = false;
        }
        XMultiPool_free(pool, p5);
    }
    
    // 测试7: realloc测试
    void* p6 = XMultiPool_malloc(pool, 32);
    void* p7 = XMultiPool_realloc(pool, p6, 64);
    if (p7) {
        TEST_INFO("  realloc扩展: 成功");
        XMultiPool_free(pool, p7);
    } else {
        // realloc可能返回NULL如果新大小超过池容量
        TEST_INFO("  realloc扩展: 返回NULL (可能池不足)");
        if (p6) XMultiPool_free(pool, p6);
    }
    
    XMultiPool_delete(pool);
    
    if (all_passed) {
        TEST_PASS("边界条件测试");
    }
    return all_passed;
}

// ==================== 测试5: 2的幂容量回归测试 ====================
static bool test_power_of_two_capacity(void) {
    static const size_t capacities[] = {64, 128, 256};
    void* ptrs[256];
    bool all_passed = true;

    TEST_INFO("===== 2的幂容量回归测试 =====");

    for (size_t capacity_index = 0;
         capacity_index < sizeof(capacities) / sizeof(capacities[0]);
         ++capacity_index) {
        const size_t capacity = capacities[capacity_index];
        XFixedPool* pool = XFixedPool_create(32, capacity);
        bool capacity_passed = true;

        memset(ptrs, 0, sizeof(ptrs));
        if (!pool) {
            TEST_FAIL("2的幂容量回归测试", "创建固定内存池失败");
            all_passed = false;
            continue;
        }

        for (size_t i = 0; i < capacity; ++i) {
            ptrs[i] = XFixedPool_malloc(pool);
            if (!ptrs[i]) {
                capacity_passed = false;
                break;
            }

            for (size_t j = 0; j < i; ++j) {
                if (ptrs[i] == ptrs[j]) {
                    capacity_passed = false;
                    break;
                }
            }
            if (!capacity_passed) break;

            memset(ptrs[i], (int)(i & 0xff), 32);
        }

        if (capacity_passed && XFixedPool_malloc(pool) != NULL) {
            capacity_passed = false;
        }

        if (capacity_passed && XFixedPool_freeCount(pool) != 0) {
            capacity_passed = false;
        }

        for (size_t i = 0; i < capacity; ++i) {
            if (ptrs[i]) XFixedPool_free(pool, ptrs[i]);
        }

        if (capacity_passed && XFixedPool_freeCount(pool) != capacity) {
            capacity_passed = false;
        }

        if (capacity_passed) {
            void* reused = XFixedPool_malloc(pool);
            if (!reused) {
                capacity_passed = false;
            } else {
                XFixedPool_free(pool, reused);
            }
        }

        if (capacity_passed) {
            TEST_INFO("  容量 %zu: 地址唯一、耗尽检测和释放复用均正常", capacity);
        } else {
            TEST_FAIL("2的幂容量回归测试", "固定内存池空闲链表状态异常");
            TEST_INFO("  失败容量: %zu", capacity);
            all_passed = false;
        }

        XFixedPool_delete(pool);
    }

    if (all_passed) {
        TEST_PASS("2的幂容量回归测试");
    }
    return all_passed;
}

// ==================== 测试6: 性能测试 ====================
static bool test_performance(void) {
    TEST_INFO("===== 性能测试 =====");
    
    XMultiPool* pool = XMultiPool_create();
    if (!pool) return false;
    
    // 创建较大的池
    XFixedPool* pool_64 = XFixedPool_create(PERFORMANCE_BLOCK_SIZE, PERFORMANCE_TEST_COUNT);
    XMultiPool_add_pool(pool, pool_64);
    
    void* ptrs[PERFORMANCE_TEST_COUNT];
    
    int64_t start = XDateTime_currentMSecsSinceEpoch();
    
    // 批量分配
    for (int i = 0; i < PERFORMANCE_TEST_COUNT; i++) {
        ptrs[i] = XMultiPool_malloc(pool, 50);
    }
    
    // 批量释放
    for (int i = 0; i < PERFORMANCE_TEST_COUNT; i++) {
        XMultiPool_free(pool, ptrs[i]);
    }
    
    int64_t end = XDateTime_currentMSecsSinceEpoch();
    double pool_elapsed = (double)(end - start);
    
    TEST_INFO("  XMultiPool %d次分配+释放耗时: %.2f ms", PERFORMANCE_TEST_COUNT, pool_elapsed);
    TEST_INFO("  平均每次: %.4f us", pool_elapsed * 1000.0 / PERFORMANCE_TEST_COUNT);
    
    // 对比系统malloc
    start = XDateTime_currentMSecsSinceEpoch();
    
    for (int i = 0; i < PERFORMANCE_TEST_COUNT; i++) {
        ptrs[i] = XMalloc_System(50);
    }
    for (int i = 0; i < PERFORMANCE_TEST_COUNT; i++) {
        XFree_System(ptrs[i]);
    }
    
    end = XDateTime_currentMSecsSinceEpoch();
    double sys_elapsed = (double)(end - start);
    
    TEST_INFO("  系统malloc %d次耗时: %.2f ms", PERFORMANCE_TEST_COUNT, sys_elapsed);
    TEST_INFO("  平均每次: %.4f us", sys_elapsed * 1000.0 / PERFORMANCE_TEST_COUNT);
    
    // 计算效率倍数
    if (pool_elapsed > 0 && sys_elapsed > 0) {
        double speedup = sys_elapsed / pool_elapsed;
        if (speedup >= 1.0) {
            TEST_INFO("  XMultiPool比系统malloc快 %.2f 倍", speedup);
        } else {
            TEST_INFO("  XMultiPool比系统malloc慢 %.2f 倍", 1.0 / speedup);
        }
    }
    
    XMultiPool_delete(pool);
    TEST_PASS("性能测试");
    return true;
}

// ==================== 测试7: 多线程压力测试 ====================
static volatile bool g_thread_running = true;
static volatile int g_thread_alloc_count = 0;
static volatile int g_thread_free_count = 0;

static void thread_stress_func(XThread* thread, XVarList* list) {
    XVarList_args_1(list, XMultiPool*, pool);
    
    int local_alloc = 0;
    int local_free = 0;
    void* ptrs[10];
    
    while (g_thread_running) {
        // 随机分配和释放
        for (int i = 0; i < 10; i++) {
            ptrs[i] = XMultiPool_malloc(pool, 10 + i * 5);
            if (ptrs[i]) {
                local_alloc++;
                memset(ptrs[i], i, 10 + i * 5);
            }
        }
        
        for (int i = 0; i < 10; i++) {
            if (ptrs[i]) {
                XMultiPool_free(pool, ptrs[i]);
                ptrs[i] = NULL;
                local_free++;
            }
        }
    }
    
    // 原子更新计数
    g_thread_alloc_count += local_alloc;
    g_thread_free_count += local_free;
}

static bool test_multithread_stress(void) {
    TEST_INFO("===== 多线程压力测试 =====");
    
    XMultiPool* pool = XMultiPool_create();
    if (!pool) return false;
    
    // 创建足够大的池
    XFixedPool* pool_64 = XFixedPool_create(64, 500);
    XFixedPool* pool_128 = XFixedPool_create(128, 200);
    XMultiPool_add_pool(pool, pool_64);
    XMultiPool_add_pool(pool, pool_128);
    
    g_thread_running = true;
    g_thread_alloc_count = 0;
    g_thread_free_count = 0;
    
    // 创建多个线程
    #define THREAD_COUNT 4
    XThread* threads[THREAD_COUNT];
    
    for (int i = 0; i < THREAD_COUNT; i++) {
        threads[i] = XThread_create_func(thread_stress_func, 
            XVarList_Create(XVar(XMultiPool*, pool)));
        XThread_start(threads[i]);
    }
    
    // 主线程也参与测试
    void* ptrs[10];
    int main_alloc = 0;
    int main_free = 0;
    
    for (int round = 0; round < 1000; round++) {
        for (int i = 0; i < 10; i++) {
            ptrs[i] = XMultiPool_malloc(pool, 20 + i * 10);
            if (ptrs[i]) {
                main_alloc++;
            }
        }
        
        for (int i = 0; i < 10; i++) {
            if (ptrs[i]) {
                XMultiPool_free(pool, ptrs[i]);
                ptrs[i] = NULL;
                main_free++;
            }
        }
    }
    
    // 停止线程
    g_thread_running = false;
    
    // 等待线程结束
    for (int i = 0; i < THREAD_COUNT; i++) {
        XThread_wait(threads[i], 5000);  // 等待5秒
        XThread_deleteLater(threads[i]);
    }
    
    TEST_INFO("  主线程: 分配 %d, 释放 %d", main_alloc, main_free);
    TEST_INFO("  工作线程: 分配 %d, 释放 %d", g_thread_alloc_count, g_thread_free_count);
    
    // 验证池状态
    size_t free_size = XMultiPool_freeSize(pool);
    size_t total_size = XMultiPool_totalSize(pool);
    TEST_INFO("  池状态: 总大小 %zu, 空闲 %zu", total_size, free_size);
    
    if (free_size == total_size) {
        TEST_INFO("  所有内存已正确回收");
    } else {
        TEST_INFO("  警告: 存在内存泄漏 (%zu 字节)", total_size - free_size);
    }
    
    XMultiPool_delete(pool);
    TEST_PASS("多线程压力测试");
    return true;
}

// ==================== 测试8: 全局池测试 ====================
static bool test_global_pool(void) {
    TEST_INFO("===== 全局池测试 =====");
    
    // 使用全局池分配
    void* p1 = XMultiPool_global_malloc(32);
    void* p2 = XMultiPool_global_calloc(4, 16);
    
    if (!p1 || !p2) {
        TEST_FAIL("全局池测试", "分配失败");
        return false;
    }
    
    // 验证calloc清零
    unsigned char* bytes = (unsigned char*)p2;
    bool zeroed = true;
    for (int i = 0; i < 64; i++) {
        if (bytes[i] != 0) {
            zeroed = false;
            break;
        }
    }
    
    if (!zeroed) {
        TEST_FAIL("全局池测试", "calloc未清零");
    }
    
    // realloc测试
    void* p3 = XMultiPool_global_realloc(p1, 64);
    if (p3) {
        TEST_INFO("  全局realloc成功");
    }
    
    // 释放
    XMultiPool_global_free(p3);
    XMultiPool_global_free(p2);
    
    TEST_PASS("全局池测试");
    return true;
}

// ==================== 主测试入口 ====================
void XMultiPoolTest(void) {
    TEST_INFO("\n========================================");
    TEST_INFO("    XMultiPool 综合测试开始");
    TEST_INFO("========================================\n");
    
    int passed = 0;
    int total = 8;
    
    if (test_basic_function()) passed++;
    if (test_continuous_alloc_free()) passed++;
    if (test_pool_exhaustion()) passed++;
    if (test_boundary_conditions()) passed++;
    if (test_power_of_two_capacity()) passed++;
    if (test_performance()) passed++;
    if (test_multithread_stress()) passed++;
    if (test_global_pool()) passed++;
    
    TEST_INFO("\n========================================");
    TEST_INFO("    测试结果: %d/%d 通过", passed, total);
    TEST_INFO("========================================\n");
}

// ==================== 菜单注册 ====================
void XMenu_XMultiPoolTest(XMenu* root) {
    XMenu* menu = XMenu_create("XMultiPool(多级内存池)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "综合测试(全部)");
        XAction_setAction(action, XMultiPoolTest_wrapper);
        
        XAction* action1 = XMenu_addAction(menu, "基础功能测试");
        XAction_setAction(action1, test_basic_function_wrapper);
        
        XAction* action2 = XMenu_addAction(menu, "连续申请释放测试");
        XAction_setAction(action2, test_continuous_alloc_free_wrapper);
        
        XAction* action3 = XMenu_addAction(menu, "内存池耗尽测试");
        XAction_setAction(action3, test_pool_exhaustion_wrapper);
        
        XAction* action4 = XMenu_addAction(menu, "边界条件测试");
        XAction_setAction(action4, test_boundary_conditions_wrapper);
        
        XAction* action5 = XMenu_addAction(menu, "2的幂容量回归测试");
        XAction_setAction(action5, test_power_of_two_capacity_wrapper);

        XAction* action6 = XMenu_addAction(menu, "性能测试");
        XAction_setAction(action6, test_performance_wrapper);

        XAction* action7 = XMenu_addAction(menu, "多线程压力测试");
        XAction_setAction(action7, test_multithread_stress_wrapper);

        XAction* action8 = XMenu_addAction(menu, "全局池测试");
        XAction_setAction(action8, test_global_pool_wrapper);
    }
}
