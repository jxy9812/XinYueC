#include "XMemoryTest.h"
#include "XVariablePool.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XDateTime.h"
#include "XTestMenu.h"
#include "XAction.h"
#include <string.h>
#include <stdint.h>

#define XVARIABLEPOOL_TEST_INFO(...)                                           \
    do {                                                                       \
        XPrintf("[信息] ");                                                   \
        XPrintf(__VA_ARGS__);                                                  \
        XPrintf("\n");                                                        \
    } while (0)
#define XVARIABLEPOOL_TEST_PASS(name)                                          \
    XPrintf("[通过] %s\n", name)
#define XVARIABLEPOOL_TEST_FAIL(name, reason)                                  \
    XPrintf("[失败] %s：%s\n", name, reason)
#define XVARIABLEPOOL_TEST_SECTION(name)                                       \
    XVARIABLEPOOL_TEST_INFO("=================================================="); \
    XVARIABLEPOOL_TEST_INFO("开始测试：%s", name)

typedef struct XVariablePoolTestLockState
{
    size_t lock_count;
    size_t unlock_count;
} XVariablePoolTestLockState;

static bool XVariablePoolTest_initBoundary(void);
static bool XVariablePoolTest_alignment(void);
static bool XVariablePoolTest_basic(void);
static bool XVariablePoolTest_split(void);
static bool XVariablePoolTest_coalesce(void);
static bool XVariablePoolTest_fragmentation(void);
static bool XVariablePoolTest_realloc(void);
static bool XVariablePoolTest_invalidFree(void);
static bool XVariablePoolTest_lockCallback(void);
static bool XVariablePoolTest_dynamic(void);
static bool XVariablePoolTest_stress(void);
static bool XVariablePoolTest_performance(void);

static void XVariablePoolTest_initBoundary_wrapper(XVariant* data);
static void XVariablePoolTest_alignment_wrapper(XVariant* data);
static void XVariablePoolTest_basic_wrapper(XVariant* data);
static void XVariablePoolTest_split_wrapper(XVariant* data);
static void XVariablePoolTest_coalesce_wrapper(XVariant* data);
static void XVariablePoolTest_fragmentation_wrapper(XVariant* data);
static void XVariablePoolTest_realloc_wrapper(XVariant* data);
static void XVariablePoolTest_invalidFree_wrapper(XVariant* data);
static void XVariablePoolTest_lockCallback_wrapper(XVariant* data);
static void XVariablePoolTest_dynamic_wrapper(XVariant* data);
static void XVariablePoolTest_stress_wrapper(XVariant* data);
static void XVariablePoolTest_performance_wrapper(XVariant* data);
static void XVariablePoolTest_all_wrapper(XVariant* data);

static void XVariablePoolTest_printStats(const char* title, const XVariablePool* pool)
{
    XVARIABLEPOOL_TEST_INFO(
        "%s：总容量=%zu，空闲容量=%zu，已分配容量=%zu，最大连续空闲=%zu，空闲块数量=%zu",
        title,
        XVariablePool_totalSize(pool),
        XVariablePool_freeSize(pool),
        XVariablePool_allocatedSize(pool),
        XVariablePool_largestFreeSize(pool),
        XVariablePool_freeBlockCount(pool));
}

static bool XVariablePoolTest_finish(const char* name, bool result, const char* reason)
{
    if (result)
        XVARIABLEPOOL_TEST_PASS(name);
    else
        XVARIABLEPOOL_TEST_FAIL(name, reason);
    return result;
}

static bool XVariablePoolTest_initBoundary(void)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[1024];
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char tiny[32];
    XVariablePool pool;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("初始化参数和容量边界");
    result = !XVariablePool_init(NULL, buffer, sizeof(buffer), 0);
    result = result && !XVariablePool_init(&pool, NULL, sizeof(buffer), 0);
    result = result && !XVariablePool_init(&pool, buffer, 0, 0);
    result = result && !XVariablePool_init(&pool, buffer, sizeof(buffer), 3);
    result = result && !XVariablePool_init(&pool, tiny, sizeof(tiny), 0);
    result = result && XVariablePool_init(&pool, buffer, sizeof(buffer), 0);
    if (result) {
        XVARIABLEPOOL_TEST_INFO("默认对齐=%zu，最小块=%zu，管理区=%zu 字节",
                                pool.alignment, pool.minimum_block_size,
                                pool.total_managed_size);
        result = XVariablePool_check(&pool);
        XVariablePool_deinit(&pool);
    }
    return XVariablePoolTest_finish("初始化参数和容量边界", result,
                                    "非法参数未被拒绝或合法 arena 初始化失败");
}

static bool XVariablePoolTest_alignment(void)
{
    XALIGNAS(16) unsigned char buffer[8192];
    XVariablePool pool;
    const size_t requests[] = {1u, 7u, 31u, 64u, 127u, 513u, 2049u};
    void* blocks[sizeof(requests) / sizeof(requests[0])] = {0};
    size_t i;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("16 字节对齐和不同申请尺寸");
    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init(&pool, buffer, sizeof(buffer), 16);
    if (result) {
        for (i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i) {
            blocks[i] = XVariablePool_malloc(&pool, requests[i]);
            XVARIABLEPOOL_TEST_INFO("申请 #%zu：请求=%zu，地址=%p，可用容量=%zu",
                                    i + 1u, requests[i], blocks[i],
                                    XVariablePool_getMaxUserSize(&pool, blocks[i]));
            if (!blocks[i] || ((uintptr_t)blocks[i] & 15u) != 0u ||
                XVariablePool_getMaxUserSize(&pool, blocks[i]) < requests[i]) {
                result = false;
                break;
            }
            memset(blocks[i], (int)(i + 1u), requests[i]);
        }
    }
    if (pool.initialized) {
        for (i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i)
            XVariablePool_free(&pool, blocks[i]);
    }
    if (pool.initialized)
        result = result && XVariablePool_check(&pool);
    XVariablePool_deinit(&pool);
    return XVariablePoolTest_finish("16 字节对齐和不同申请尺寸", result,
                                    "返回地址未对齐、容量不足或释放后校验失败");
}

static bool XVariablePoolTest_basic(void)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[8192];
    XVariablePool pool;
    unsigned char* zeroed;
    void* first;
    void* second;
    void* third;
    size_t i;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("malloc、calloc、NULL 和基础释放");
    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init(&pool, buffer, sizeof(buffer), 0);
    first = result ? XVariablePool_malloc(&pool, 37) : NULL;
    second = result ? XVariablePool_calloc(&pool, 8, 23) : NULL;
    third = result ? XVariablePool_realloc(&pool, NULL, 511) : NULL;
    result = result && first && second && third;
    XVARIABLEPOOL_TEST_INFO("malloc(37)=%p，calloc(8,23)=%p，realloc(NULL,511)=%p",
                            first, second, third);
    zeroed = (unsigned char*)second;
    if (zeroed) {
        for (i = 0; i < 8u * 23u; ++i) {
            if (zeroed[i] != 0u) {
                result = false;
                break;
            }
        }
    }
    result = result && XVariablePool_calloc(&pool, SIZE_MAX, 2u) == NULL;
    result = result && XVariablePool_calloc(&pool, 0, 1u) == NULL;
    XVariablePoolTest_printStats("申请后三块内存", &pool);
    XVariablePool_free(&pool, first);
    XVariablePool_free(&pool, second);
    XVariablePool_free(&pool, third);
    XVariablePool_free(&pool, NULL);
    result = result && XVariablePool_check(&pool) &&
             XVariablePool_freeBlockCount(&pool) == 1u;
    XVariablePoolTest_printStats("全部释放后", &pool);
    XVariablePool_deinit(&pool);
    return XVariablePoolTest_finish("malloc、calloc、NULL 和基础释放", result,
                                    "基础申请、calloc 清零、溢出检查或释放校验失败");
}

static bool XVariablePoolTest_split(void)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[8192];
    XVariablePool pool;
    void* large;
    void* small;
    size_t before;
    size_t after_large;
    size_t after_small;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("空闲块切分和容量统计");
    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init(&pool, buffer, sizeof(buffer), 0);
    before = XVariablePool_freeSize(&pool);
    large = result ? XVariablePool_malloc(&pool, 1024) : NULL;
    after_large = XVariablePool_freeSize(&pool);
    small = large ? XVariablePool_malloc(&pool, 128) : NULL;
    after_small = XVariablePool_freeSize(&pool);
    XVARIABLEPOOL_TEST_INFO("初始空闲=%zu，申请 1024 后=%zu，申请 128 后=%zu",
                            before, after_large, after_small);
    result = result && large && small && after_large < before &&
             after_small < after_large && XVariablePool_check(&pool);
    XVariablePool_free(&pool, small);
    XVariablePool_free(&pool, large);
    result = result && XVariablePool_check(&pool) &&
             XVariablePool_freeBlockCount(&pool) == 1u;
    XVariablePool_deinit(&pool);
    return XVariablePoolTest_finish("空闲块切分和容量统计", result,
                                    "块切分后统计不一致或物理链校验失败");
}

static bool XVariablePoolTest_coalesceCase(const char* title, int mode)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[8192];
    XVariablePool pool;
    void* blocks[4] = {0};
    size_t before;
    size_t after;
    size_t i;
    bool result;

    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init(&pool, buffer, sizeof(buffer), 0);
    if (result) {
        for (i = 0; i < 4u; ++i)
            blocks[i] = XVariablePool_malloc(&pool, 1800);
        result = blocks[0] && blocks[1] && blocks[2] && blocks[3];
    }
    if (result) {
        if (mode == 0) {
            XVariablePool_free(&pool, blocks[1]);
            blocks[1] = NULL;
            before = XVariablePool_largestFreeSize(&pool);
            XVariablePool_free(&pool, blocks[0]);
            blocks[0] = NULL;
        }
        else if (mode == 1) {
            XVariablePool_free(&pool, blocks[1]);
            blocks[1] = NULL;
            before = XVariablePool_largestFreeSize(&pool);
            XVariablePool_free(&pool, blocks[2]);
            blocks[2] = NULL;
        }
        else {
            XVariablePool_free(&pool, blocks[0]);
            blocks[0] = NULL;
            XVariablePool_free(&pool, blocks[2]);
            blocks[2] = NULL;
            before = XVariablePool_largestFreeSize(&pool);
            XVariablePool_free(&pool, blocks[1]);
            blocks[1] = NULL;
        }
        after = XVariablePool_largestFreeSize(&pool);
        XVARIABLEPOOL_TEST_INFO("%s：合并前最大连续=%zu，合并后=%zu，空闲块=%zu",
                                title, before, after,
                                XVariablePool_freeBlockCount(&pool));
        result = XVariablePool_check(&pool) && after > before;
    }
    for (i = 0; i < 4u; ++i) {
        XVariablePool_free(&pool, blocks[i]);
        blocks[i] = NULL;
    }
    result = result && XVariablePool_check(&pool) &&
             XVariablePool_freeBlockCount(&pool) == 1u;
    XVariablePool_deinit(&pool);
    return result;
}

static bool XVariablePoolTest_coalesce(void)
{
    bool next_result;
    bool previous_result;
    bool both_result;

    XVARIABLEPOOL_TEST_SECTION("前邻、后邻和双侧空闲块合并");
    next_result = XVariablePoolTest_coalesceCase("释放块与后邻块合并", 0);
    previous_result = XVariablePoolTest_coalesceCase("释放块与前邻块合并", 1);
    both_result = XVariablePoolTest_coalesceCase("释放块同时与前后块合并", 2);
    XVARIABLEPOOL_TEST_INFO("后邻合并=%s，前邻合并=%s，双侧合并=%s",
                            next_result ? "通过" : "失败",
                            previous_result ? "通过" : "失败",
                            both_result ? "通过" : "失败");
    return XVariablePoolTest_finish("前邻、后邻和双侧空闲块合并",
                                    next_result && previous_result && both_result,
                                    "相邻空闲块未立即合并或合并后统计错误");
}

static bool XVariablePoolTest_fragmentation(void)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[8192];
    XVariablePool pool;
    void* first;
    void* middle;
    void* third;
    void* last;
    void* large;
    size_t free_size;
    size_t largest_size;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("外部碎片和最大连续空间");
    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init(&pool, buffer, sizeof(buffer), 0);
    first = result ? XVariablePool_malloc(&pool, 1800) : NULL;
    middle = result ? XVariablePool_malloc(&pool, 1800) : NULL;
    third = result ? XVariablePool_malloc(&pool, 1800) : NULL;
    last = result ? XVariablePool_malloc(&pool, 1800) : NULL;
    result = result && first && middle && third && last;
    XVariablePool_free(&pool, first);
    XVariablePool_free(&pool, third);
    free_size = XVariablePool_freeSize(&pool);
    largest_size = XVariablePool_largestFreeSize(&pool);
    XVARIABLEPOOL_TEST_INFO("释放两个非相邻块后：空闲总量=%zu，最大连续块=%zu",
                            free_size, largest_size);
    large = XVariablePool_malloc(&pool, 3500);
    result = result && free_size > largest_size && large == NULL &&
             XVariablePool_check(&pool);
    XVARIABLEPOOL_TEST_INFO("申请 3500 字节：%s（预期因外部碎片失败）",
                            large ? "意外成功" : "按预期失败");
    XVariablePool_free(&pool, middle);
    large = XVariablePool_malloc(&pool, 3500);
    result = result && large != NULL && XVariablePool_check(&pool);
    XVARIABLEPOOL_TEST_INFO("合并中间块后再次申请 3500 字节：%s",
                            large ? "成功" : "失败");
    XVariablePool_free(&pool, large);
    XVariablePool_free(&pool, last);
    XVariablePool_deinit(&pool);
    return XVariablePoolTest_finish("外部碎片和最大连续空间", result,
                                    "碎片统计错误，或相邻块合并后仍无法申请连续空间");
}

static bool XVariablePoolTest_checkPattern(const unsigned char* memory,
                                           size_t size,
                                           unsigned char pattern)
{
    size_t i;
    if (!memory)
        return false;
    for (i = 0; i < size; ++i) {
        if (memory[i] != pattern)
            return false;
    }
    return true;
}

static bool XVariablePoolTest_realloc(void)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char shrink_buffer[8192];
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char grow_buffer[8192];
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char move_buffer[8192];
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char fail_buffer[4096];
    XVariablePool shrink_pool;
    XVariablePool grow_pool;
    XVariablePool move_pool;
    XVariablePool fail_pool;
    unsigned char* memory;
    void* original;
    void* blocker;
    void* resized = NULL;
    bool shrink_result = false;
    bool grow_result = false;
    bool move_result = false;
    bool fail_result = false;

    XVARIABLEPOOL_TEST_SECTION("realloc 原地收缩、原地扩展、迁移和失败保持");

    memset(&shrink_pool, 0, sizeof(shrink_pool));
    memset(&grow_pool, 0, sizeof(grow_pool));
    memset(&move_pool, 0, sizeof(move_pool));
    memset(&fail_pool, 0, sizeof(fail_pool));
    XVariablePool_init(&shrink_pool, shrink_buffer, sizeof(shrink_buffer), 0);
    original = XVariablePool_malloc(&shrink_pool, 512);
    memory = (unsigned char*)original;
    if (memory) {
        memset(memory, 0xA5, 512);
        resized = XVariablePool_realloc(&shrink_pool, original, 64);
        shrink_result = resized == original &&
                        XVariablePoolTest_checkPattern((unsigned char*)resized, 64, 0xA5) &&
                        XVariablePool_check(&shrink_pool);
        XVariablePool_free(&shrink_pool, resized);
    }
    XVARIABLEPOOL_TEST_INFO("收缩：%s，原地址=%p，新地址=%p",
                            shrink_result ? "原地完成" : "失败",
                            original, resized);
    XVariablePool_deinit(&shrink_pool);

    resized = NULL;
    XVariablePool_init(&grow_pool, grow_buffer, sizeof(grow_buffer), 0);
    original = XVariablePool_malloc(&grow_pool, 1000);
    blocker = XVariablePool_malloc(&grow_pool, 1000);
    memory = (unsigned char*)original;
    if (memory && blocker) {
        memset(memory, 0xB6, 1000);
        XVariablePool_free(&grow_pool, blocker);
        resized = XVariablePool_realloc(&grow_pool, original, 2500);
        grow_result = resized == original &&
                      XVariablePool_getMaxUserSize(&grow_pool, resized) >= 2500u &&
                      XVariablePoolTest_checkPattern((unsigned char*)resized, 1000, 0xB6) &&
                      XVariablePool_check(&grow_pool);
        XVariablePool_free(&grow_pool, resized);
    }
    XVARIABLEPOOL_TEST_INFO("相邻空闲块扩展：%s，原地址=%p，新地址=%p",
                            grow_result ? "原地完成" : "失败",
                            original, resized);
    XVariablePool_deinit(&grow_pool);

    resized = NULL;
    XVariablePool_init(&move_pool, move_buffer, sizeof(move_buffer), 0);
    original = XVariablePool_malloc(&move_pool, 1000);
    blocker = XVariablePool_malloc(&move_pool, 1000);
    memory = (unsigned char*)original;
    if (memory && blocker) {
        memset(memory, 0xC7, 1000);
        resized = XVariablePool_realloc(&move_pool, original, 2500);
        move_result = resized && resized != original &&
                      XVariablePoolTest_checkPattern((unsigned char*)resized, 1000, 0xC7) &&
                      XVariablePool_check(&move_pool);
        XVariablePool_free(&move_pool, resized);
        XVariablePool_free(&move_pool, blocker);
    }
    XVARIABLEPOOL_TEST_INFO("相邻块被占用时迁移：%s，原地址=%p，新地址=%p",
                            move_result ? "复制成功" : "失败",
                            original, resized);
    XVariablePool_deinit(&move_pool);

    resized = NULL;
    XVariablePool_init(&fail_pool, fail_buffer, sizeof(fail_buffer), 0);
    original = XVariablePool_malloc(&fail_pool, 256);
    memory = (unsigned char*)original;
    if (memory) {
        memset(memory, 0xD8, 256);
        resized = XVariablePool_realloc(&fail_pool, original, SIZE_MAX);
        fail_result = resized == NULL &&
                      XVariablePoolTest_checkPattern(memory, 256, 0xD8) &&
                      XVariablePool_check(&fail_pool);
        XVariablePool_free(&fail_pool, original);
    }
    XVARIABLEPOOL_TEST_INFO("超大申请失败后原块保持：%s",
                            fail_result ? "保持不变" : "校验失败");
    XVariablePool_deinit(&fail_pool);

    return XVariablePoolTest_finish("realloc 原地收缩、原地扩展、迁移和失败保持",
                                    shrink_result && grow_result && move_result && fail_result,
                                    "realloc 未按预期选择原地、迁移或失败保持路径");
}

static bool XVariablePoolTest_invalidFree(void)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[4096];
    XVariablePool pool;
    unsigned char outside[32] = {0};
    void* memory;
    size_t free_size;
    size_t free_count;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("重复释放、非法指针和已释放块识别");
    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init(&pool, buffer, sizeof(buffer), 0);
    memory = result ? XVariablePool_malloc(&pool, 256) : NULL;
    result = result && memory && XVariablePool_is_from_pool(&pool, memory);
    XVariablePool_free(&pool, memory);
    free_size = XVariablePool_freeSize(&pool);
    free_count = XVariablePool_freeBlockCount(&pool);
    XVariablePool_free(&pool, memory);
    XVariablePool_free(&pool, (unsigned char*)memory + 1u);
    XVariablePool_free(&pool, outside);
    result = result && !XVariablePool_is_from_pool(&pool, memory) &&
             !XVariablePool_is_from_pool(&pool, outside) &&
             XVariablePool_freeSize(&pool) == free_size &&
             XVariablePool_freeBlockCount(&pool) == free_count &&
             XVariablePool_realloc(&pool, memory, 128) == NULL &&
             XVariablePool_check(&pool);
    XVARIABLEPOOL_TEST_INFO("重复释放后：空闲容量=%zu，空闲块数量=%zu，链表校验=%s",
                            XVariablePool_freeSize(&pool),
                            XVariablePool_freeBlockCount(&pool),
                            XVariablePool_check(&pool) ? "正常" : "损坏");
    XVariablePool_deinit(&pool);
    return XVariablePoolTest_finish("重复释放、非法指针和已释放块识别", result,
                                    "非法指针改变了统计或破坏了物理块链");
}

static void XVariablePoolTest_lock(void* context)
{
    XVariablePoolTestLockState* state = (XVariablePoolTestLockState*)context;
    state->lock_count++;
}

static void XVariablePoolTest_unlock(void* context)
{
    XVariablePoolTestLockState* state = (XVariablePoolTestLockState*)context;
    state->unlock_count++;
}

static bool XVariablePoolTest_lockCallback(void)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[4096];
    XVariablePool pool;
    XVariablePoolTestLockState state = {0};
    void* memory;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("锁回调成对调用");
    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init_ex(&pool, buffer, sizeof(buffer), 0,
                                   XVariablePoolTest_lock,
                                   XVariablePoolTest_unlock, &state);
    memory = result ? XVariablePool_malloc(&pool, 256) : NULL;
    XVariablePool_free(&pool, memory);
    result = result && memory && XVariablePool_check(&pool) &&
             state.lock_count > 0u && state.lock_count == state.unlock_count;
    XVARIABLEPOOL_TEST_INFO("加锁次数=%zu，解锁次数=%zu",
                            state.lock_count, state.unlock_count);
    XVariablePool_deinit(&pool);
    return XVariablePoolTest_finish("锁回调成对调用", result,
                                    "锁回调未调用、未成对或池校验失败");
}

static bool XVariablePoolTest_dynamic(void)
{
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char backing[4096];
    XVariablePool stack_pool;
    XVariablePool* heap_pool;
    XVariablePool* external_pool;
    void* memory;
    bool heap_result;
    bool external_result;
    bool stack_result;

    XVARIABLEPOOL_TEST_SECTION("堆模式、已有内存模式和栈模式");
    heap_pool = XVariablePool_create(8192, 16);
    memory = heap_pool ? XVariablePool_malloc(heap_pool, 4097) : NULL;
    heap_result = heap_pool && memory &&
                  ((uintptr_t)memory & 15u) == 0u &&
                  XVariablePool_check(heap_pool);
    XVariablePool_free(heap_pool, memory);
    heap_result = heap_result && XVariablePool_check(heap_pool);
    XVariablePool_delete(heap_pool);

    external_pool = XVariablePool_create_from_memory(backing, sizeof(backing), 0);
    memory = external_pool ? XVariablePool_malloc(external_pool, 512) : NULL;
    external_result = external_pool && memory && XVariablePool_check(external_pool);
    XVariablePool_free(external_pool, memory);
    external_result = external_result && XVariablePool_check(external_pool);
    XVariablePool_delete(external_pool);

    memset(&stack_pool, 0, sizeof(stack_pool));
    stack_result = XVariablePool_init(&stack_pool, backing, sizeof(backing), 0);
    memory = stack_result ? XVariablePool_malloc(&stack_pool, 512) : NULL;
    stack_result = stack_result && memory && XVariablePool_check(&stack_pool);
    XVariablePool_free(&stack_pool, memory);
    stack_result = stack_result && XVariablePool_check(&stack_pool);
    XVariablePool_deinit(&stack_pool);

    XVARIABLEPOOL_TEST_INFO("堆模式=%s，已有内存模式=%s，栈对象模式=%s",
                            heap_result ? "通过" : "失败",
                            external_result ? "通过" : "失败",
                            stack_result ? "通过" : "失败");
    return XVariablePoolTest_finish("堆模式、已有内存模式和栈模式",
                                    heap_result && external_result && stack_result,
                                    "三种生命周期模式中至少一种初始化或释放失败");
}

static bool XVariablePoolTest_verifySlot(void* memory,
                                         size_t size,
                                         unsigned char pattern)
{
    return XVariablePoolTest_checkPattern((const unsigned char*)memory, size, pattern);
}

static bool XVariablePoolTest_stress(void)
{
    enum { SLOT_COUNT = 128, ROUND_COUNT = 5000 };
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[32768];
    XVariablePool pool;
    void* slots[SLOT_COUNT] = {0};
    size_t requested[SLOT_COUNT] = {0};
    uint32_t state = UINT32_C(0x12345678);
    size_t allocated_count = 0;
    size_t freed_count = 0;
    size_t allocation_fail_count = 0;
    size_t round;
    size_t i;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("随机申请释放和长时间碎片压力");
    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init(&pool, buffer, sizeof(buffer), 0);
    for (round = 0; result && round < ROUND_COUNT; ++round) {
        size_t slot;
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        slot = (size_t)(state % SLOT_COUNT);
        if (slots[slot]) {
            result = XVariablePoolTest_verifySlot(
                slots[slot], requested[slot], (unsigned char)(requested[slot] & 0xFFu));
            XVariablePool_free(&pool, slots[slot]);
            slots[slot] = NULL;
            requested[slot] = 0;
            freed_count++;
        }
        else {
            requested[slot] = (size_t)(state % 900u) + 1u;
            slots[slot] = XVariablePool_malloc(&pool, requested[slot]);
            if (slots[slot]) {
                unsigned char pattern = (unsigned char)(requested[slot] & 0xFFu);
                memset(slots[slot], pattern, requested[slot]);
                result = XVariablePool_getMaxUserSize(&pool, slots[slot]) >= requested[slot];
                allocated_count++;
            }
            else {
                requested[slot] = 0;
                allocation_fail_count++;
            }
        }
        result = result && XVariablePool_check(&pool);
        if (result && (round + 1u) % 1000u == 0u)
            XVARIABLEPOOL_TEST_INFO("已完成 %zu/%d 轮，当前空闲=%zu，最大连续=%zu",
                                    round + 1u, ROUND_COUNT,
                                    XVariablePool_freeSize(&pool),
                                    XVariablePool_largestFreeSize(&pool));
    }
    for (i = 0; i < SLOT_COUNT; ++i) {
        if (slots[i]) {
            result = result && XVariablePoolTest_verifySlot(
                slots[i], requested[i], (unsigned char)(requested[i] & 0xFFu));
            XVariablePool_free(&pool, slots[i]);
            slots[i] = NULL;
        }
    }
    result = result && XVariablePool_check(&pool) &&
             XVariablePool_freeBlockCount(&pool) == 1u;
    XVARIABLEPOOL_TEST_INFO("申请成功=%zu，释放成功=%zu，空间不足=%zu",
                            allocated_count, freed_count, allocation_fail_count);
    XVariablePool_deinit(&pool);
    return XVariablePoolTest_finish("随机申请释放和长时间碎片压力", result,
                                    "随机压力期间数据被覆盖、块链损坏或释放后未恢复");
}

static bool XVariablePoolTest_performance(void)
{
    enum { OPERATION_COUNT = 20000 };
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[16384];
    XVariablePool pool;
    int64_t start;
    int64_t end;
    int64_t tlsf_elapsed;
    int64_t system_elapsed;
    size_t i;
    size_t tlsf_success = 0;
    size_t system_success = 0;
    void* memory;
    bool result;

    XVARIABLEPOOL_TEST_SECTION("TLSF分配释放性能统计");
    memset(&pool, 0, sizeof(pool));
    result = XVariablePool_init(&pool, buffer, sizeof(buffer), 0);
    start = XDateTime_currentMSecsSinceEpoch();
    for (i = 0; result && i < OPERATION_COUNT; ++i) {
        memory = XVariablePool_malloc(&pool, (i % 512u) + 1u);
        if (!memory)
            result = false;
        else {
            tlsf_success++;
            XVariablePool_free(&pool, memory);
        }
    }
    end = XDateTime_currentMSecsSinceEpoch();
    tlsf_elapsed = end - start;

    start = XDateTime_currentMSecsSinceEpoch();
    for (i = 0; i < OPERATION_COUNT; ++i) {
        memory = XMalloc_System((i % 512u) + 1u);
        if (memory) {
            system_success++;
            XFree_System(memory);
        }
    }
    end = XDateTime_currentMSecsSinceEpoch();
    system_elapsed = end - start;
    XVARIABLEPOOL_TEST_INFO("TLSF：成功=%zu/%d，耗时=%lld ms，平均=%.3f us/次",
                            tlsf_success, OPERATION_COUNT,
                            (long long)tlsf_elapsed,
                            tlsf_elapsed > 0 ? (double)tlsf_elapsed * 1000.0 / OPERATION_COUNT : 0.0);
    XVARIABLEPOOL_TEST_INFO("系统malloc：成功=%zu/%d，耗时=%lld ms，平均=%.3f us/次",
                            system_success, OPERATION_COUNT,
                            (long long)system_elapsed,
                            system_elapsed > 0 ? (double)system_elapsed * 1000.0 / OPERATION_COUNT : 0.0);
    XVARIABLEPOOL_TEST_INFO("说明：性能测试只记录结果，不以主机系统malloc速度作为通过条件");
    result = result && tlsf_success == OPERATION_COUNT &&
             XVariablePool_check(&pool);
    XVariablePool_deinit(&pool);
    return XVariablePoolTest_finish("TLSF分配释放性能统计", result,
                                    "固定 arena 在性能循环中出现意外分配失败或链表损坏");
}

void XVariablePoolTest(void)
{
    int passed = 0;
    const int total = 12;

    XVARIABLEPOOL_TEST_INFO("TLSF可变内存池详细测试开始");
    XVARIABLEPOOL_TEST_INFO("测试范围：初始化、对齐、基础API、切分、合并、碎片、realloc、");
    XVARIABLEPOOL_TEST_INFO("非法释放、锁回调、生命周期、随机压力和性能统计");
    if (XVariablePoolTest_initBoundary()) passed++;
    if (XVariablePoolTest_alignment()) passed++;
    if (XVariablePoolTest_basic()) passed++;
    if (XVariablePoolTest_split()) passed++;
    if (XVariablePoolTest_coalesce()) passed++;
    if (XVariablePoolTest_fragmentation()) passed++;
    if (XVariablePoolTest_realloc()) passed++;
    if (XVariablePoolTest_invalidFree()) passed++;
    if (XVariablePoolTest_lockCallback()) passed++;
    if (XVariablePoolTest_dynamic()) passed++;
    if (XVariablePoolTest_stress()) passed++;
    if (XVariablePoolTest_performance()) passed++;
    XVARIABLEPOOL_TEST_INFO("==================================================");
    XVARIABLEPOOL_TEST_INFO("详细测试结果：%d/%d 项通过", passed, total);
}

static void XVariablePoolTest_initBoundary_wrapper(XVariant* data) { (void)data; XVariablePoolTest_initBoundary(); }
static void XVariablePoolTest_alignment_wrapper(XVariant* data) { (void)data; XVariablePoolTest_alignment(); }
static void XVariablePoolTest_basic_wrapper(XVariant* data) { (void)data; XVariablePoolTest_basic(); }
static void XVariablePoolTest_split_wrapper(XVariant* data) { (void)data; XVariablePoolTest_split(); }
static void XVariablePoolTest_coalesce_wrapper(XVariant* data) { (void)data; XVariablePoolTest_coalesce(); }
static void XVariablePoolTest_fragmentation_wrapper(XVariant* data) { (void)data; XVariablePoolTest_fragmentation(); }
static void XVariablePoolTest_realloc_wrapper(XVariant* data) { (void)data; XVariablePoolTest_realloc(); }
static void XVariablePoolTest_invalidFree_wrapper(XVariant* data) { (void)data; XVariablePoolTest_invalidFree(); }
static void XVariablePoolTest_lockCallback_wrapper(XVariant* data) { (void)data; XVariablePoolTest_lockCallback(); }
static void XVariablePoolTest_dynamic_wrapper(XVariant* data) { (void)data; XVariablePoolTest_dynamic(); }
static void XVariablePoolTest_stress_wrapper(XVariant* data) { (void)data; XVariablePoolTest_stress(); }
static void XVariablePoolTest_performance_wrapper(XVariant* data) { (void)data; XVariablePoolTest_performance(); }
static void XVariablePoolTest_all_wrapper(XVariant* data) { (void)data; XVariablePoolTest(); }

void XTestMenu_XVariablePoolTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XVariablePool(TLSF详细测试)");
    XTestMenu_addMenu(root, menu);
    {
        XAction* action = XTestMenu_addAction(menu, "综合测试(全部12项)");
        XTestMenu_setActionFunction(action, XVariablePoolTest_all_wrapper);
        action = XTestMenu_addAction(menu, "1. 初始化参数和容量边界");
        XTestMenu_setActionFunction(action, XVariablePoolTest_initBoundary_wrapper);
        action = XTestMenu_addAction(menu, "2. 16字节对齐和不同申请尺寸");
        XTestMenu_setActionFunction(action, XVariablePoolTest_alignment_wrapper);
        action = XTestMenu_addAction(menu, "3. malloc calloc NULL和基础释放");
        XTestMenu_setActionFunction(action, XVariablePoolTest_basic_wrapper);
        action = XTestMenu_addAction(menu, "4. 空闲块切分和容量统计");
        XTestMenu_setActionFunction(action, XVariablePoolTest_split_wrapper);
        action = XTestMenu_addAction(menu, "5. 前邻后邻和双侧合并");
        XTestMenu_setActionFunction(action, XVariablePoolTest_coalesce_wrapper);
        action = XTestMenu_addAction(menu, "6. 外部碎片和最大连续空间");
        XTestMenu_setActionFunction(action, XVariablePoolTest_fragmentation_wrapper);
        action = XTestMenu_addAction(menu, "7. realloc四种路径");
        XTestMenu_setActionFunction(action, XVariablePoolTest_realloc_wrapper);
        action = XTestMenu_addAction(menu, "8. 重复释放和非法指针");
        XTestMenu_setActionFunction(action, XVariablePoolTest_invalidFree_wrapper);
        action = XTestMenu_addAction(menu, "9. 锁回调成对调用");
        XTestMenu_setActionFunction(action, XVariablePoolTest_lockCallback_wrapper);
        action = XTestMenu_addAction(menu, "10. 堆模式已有内存和栈模式");
        XTestMenu_setActionFunction(action, XVariablePoolTest_dynamic_wrapper);
        action = XTestMenu_addAction(menu, "11. 随机申请释放压力");
        XTestMenu_setActionFunction(action, XVariablePoolTest_stress_wrapper);
        action = XTestMenu_addAction(menu, "12. TLSF性能统计");
        XTestMenu_setActionFunction(action, XVariablePoolTest_performance_wrapper);
    }
}
