#include "XCodeTest.h"
#include "XMemory.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XRandomGenerator.h"
#include <stdio.h>
#include <string.h>

// 测试基本随机数生成
static void test_basic_generation(void)
{
    XPrintf("\n========== 基本生成测试 ==========\n");
    
    // 获取全局随机数生成器
    XRandomGenerator* rng = XRandomGenerator_global();
    if (!rng) {
        XPrintf("错误：无法获取全局随机数生成器\n");
        return;
    }
    
    // 生成随机数
    XPrintf("生成10个随机uint32值：\n");
    for (int i = 0; i < 10; i++) {
        uint32_t val = XRandomGenerator_generate(rng);
        XPrintf("  [%d] %u\n", i + 1, val);
    }
    
    XPrintf("\n生成10个随机uint64值：\n");
    for (int i = 0; i < 10; i++) {
        uint64_t val = XRandomGenerator_generate64(rng);
        XPrintf("  [%d] %llu\n", i + 1, (unsigned long long)val);
    }
}

// 测试有界随机数
static void test_bounded_generation(void)
{
    XPrintf("\n========== 有界生成测试 ==========\n");
    
    XRandomGenerator* rng = XRandomGenerator_global();
    
    // 测试有界随机数（0-99）
    XPrintf("生成20个范围[0, 100)内的随机数：\n");
    for (int i = 0; i < 20; i++) {
        uint32_t val = XRandomGenerator_boundedU32(rng, 100);
        XPrintf("%3u ", val);
        if ((i + 1) % 10 == 0) XPrintf("\n");
    }
    
    // 测试有界随机数（0-9）
    XPrintf("\n生成20个范围[0, 10)内的随机数：\n");
    for (int i = 0; i < 20; i++) {
        uint32_t val = XRandomGenerator_boundedU32(rng, 10);
        XPrintf("%u ", val);
        if ((i + 1) % 10 == 0) XPrintf("\n");
    }
    
    // 测试边界情况
    XPrintf("\n边界情况测试：\n");
    XPrintf("  bounded(1) = %u （应该总是0）\n", XRandomGenerator_boundedU32(rng, 1));
    XPrintf("  bounded(2) = %u （应该是0或1）\n", XRandomGenerator_boundedU32(rng, 2));
}

// 测试浮点数随机数
static void test_float_generation(void)
{
    XPrintf("\n========== 浮点数生成测试 ==========\n");
    
    XRandomGenerator* rng = XRandomGenerator_global();
    
    XPrintf("生成10个范围[0.0, 1.0)内的随机浮点数：\n");
    for (int i = 0; i < 10; i++) {
        double val = XRandomGenerator_generateDouble(rng);
        XPrintf("  [%d] %.10f\n", i + 1, val);
    }
    
    // 测试范围
    XPrintf("\n生成10个范围[0.0, 100.0)内的随机浮点数：\n");
    for (int i = 0; i < 10; i++) {
        double val = XRandomGenerator_generateDouble(rng) * 100.0;
        XPrintf("  [%d] %.6f\n", i + 1, val);
    }
}

// 测试种子设置
static void test_seed(void)
{
    XPrintf("\n========== 种子测试 ==========\n");
    
    // 创建两个相同种子的生成器
    XRandomGenerator* rng1 = XRandomGenerator_create();
    XRandomGenerator* rng2 = XRandomGenerator_create();
    
    if (rng1 && rng2) {
        XRandomGenerator_seed(rng1, 12345);
        XRandomGenerator_seed(rng2, 12345);
        
        XPrintf("两个使用相同种子(12345)的生成器：\n");
        XPrintf("  生成器1: ");
        for (int i = 0; i < 5; i++) {
            XPrintf("%u ", XRandomGenerator_generate(rng1));
        }
        XPrintf("\n  生成器2: ");
        for (int i = 0; i < 5; i++) {
            XPrintf("%u ", XRandomGenerator_generate(rng2));
        }
        XPrintf("\n（序列应该完全相同）\n");
        
        // 测试不同种子
        XRandomGenerator_seed(rng1, 54321);
        XPrintf("\n将生成器1重新设置种子为54321后：\n");
        XPrintf("  生成器1: ");
        for (int i = 0; i < 5; i++) {
            XPrintf("%u ", XRandomGenerator_generate(rng1));
        }
        XPrintf("\n  生成器2: ");
        for (int i = 0; i < 5; i++) {
            XPrintf("%u ", XRandomGenerator_generate(rng2));
        }
        XPrintf("\n（序列应该不同）\n");
    }
    
    XRandomGenerator_delete(rng1);
    XRandomGenerator_delete(rng2);
}

// 测试范围随机数
static void test_range_generation(void)
{
    XPrintf("\n========== 范围生成测试 ==========\n");
    
    XRandomGenerator* rng = XRandomGenerator_global();
    
    // 测试指定范围
    int32_t min = -50, max = 50;
    XPrintf("生成20个范围[%d, %d]内的随机整数：\n", min, max);
    for (int i = 0; i < 20; i++) {
        int32_t val = XRandomGenerator_boundedI32Range(rng, min, max);
        XPrintf("%4d ", val);
        if ((i + 1) % 10 == 0) XPrintf("\n");
    }
    
    // 统计测试
    XPrintf("\n分布测试（10000个样本，范围[0, 10)）：\n");
    int counts[10] = {0};
    for (int i = 0; i < 10000; i++) {
        uint32_t val = XRandomGenerator_boundedU32(rng, 10);
        counts[val]++;
    }
    for (int i = 0; i < 10; i++) {
        XPrintf("  [%d]: %d (%.1f%%)\n", i, counts[i], counts[i] / 100.0);
    }
}

// 测试批量生成
static void test_bulk_generation(void)
{
    XPrintf("\n========== 批量生成测试 ==========\n");
    
    XRandomGenerator* rng = XRandomGenerator_global();
    
    // 生成大量随机数并检查分布
    const int sampleCount = 100000;
    uint64_t sum = 0;
    uint32_t minVal = UINT32_MAX;
    uint32_t maxVal = 0;
    
    for (int i = 0; i < sampleCount; i++) {
        uint32_t val = XRandomGenerator_generate(rng);
        sum += val;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
    }
    
    double average = (double)sum / sampleCount;
    double expectedAvg = (double)UINT32_MAX / 2.0;
    
    XPrintf("生成了%d个随机uint32值：\n", sampleCount);
    XPrintf("  最小值: %u\n", minVal);
    XPrintf("  最大值: %u\n", maxVal);
    XPrintf("  平均值: %.2f （期望值: %.2f）\n", average, expectedAvg);
    XPrintf("  偏差: %.2f%%\n", (average - expectedAvg) / expectedAvg * 100.0);
}

// 测试复制
static void test_copy(void)
{
    XPrintf("\n========== 复制测试 ==========\n");
    
    XRandomGenerator* original = XRandomGenerator_createWithSeed(99999);
    if (original) {
        // 测试复制
        XRandomGenerator* copied = XRandomGenerator_copy(original);
        
        if (copied) {
            XPrintf("原始: %u\n", XRandomGenerator_generate(original));
            XPrintf("复制:   %u （应该与原始的下一个值匹配）\n", XRandomGenerator_generate(copied));
            XPrintf("原始: %u\n", XRandomGenerator_generate(original));
            XPrintf("复制:   %u\n", XRandomGenerator_generate(copied));
            
            XRandomGenerator_delete(copied);
        }
        
        XRandomGenerator_delete(original);
    }
}

// 测试系统随机数（安全随机）
static void test_system_random(void)
{
    XPrintf("\n========== 系统随机数测试 ==========\n");
    
    uint8_t buffer[32];
    
    if (XRandomGenerator_fillSecure(buffer, 32)) {
        XPrintf("从系统CSPRNG获取的32字节：\n  ");
        for (int i = 0; i < 32; i++) {
            XPrintf("%02x", buffer[i]);
        }
        XPrintf("\n");
    } else {
        XPrintf("系统CSPRNG不可用\n");
    }
}

// 性能测试
static void test_performance(void)
{
    XPrintf("\n========== 性能测试 ==========\n");
    
    XRandomGenerator* rng = XRandomGenerator_global();
    const int iterations = 10000000;
    
    // 记录开始时间
    XPrintf("正在生成%d个随机数...\n", iterations);
    
    // 使用 XDateTime 计时
    extern int64_t XDateTime_currentMSecsSinceEpoch(void);
    int64_t start = XDateTime_currentMSecsSinceEpoch();
    
    volatile uint32_t sum = 0;
    for (int i = 0; i < iterations; i++) {
        sum += XRandomGenerator_generate(rng);
    }
    
    int64_t end = XDateTime_currentMSecsSinceEpoch();
    double elapsed = (end - start) / 1000.0;
    
    XPrintf("  耗时: %.3f 秒\n", elapsed);
    XPrintf("  速率: %.0f 个/秒\n", iterations / elapsed);
    (void)sum; // 避免优化掉
}

void XRandomGeneratorTest(void)
{
    XPrintf("=== XRandomGenerator 综合测试 ===\n");
    
    test_basic_generation();
    test_bounded_generation();
    test_float_generation();
    test_seed();
    test_range_generation();
    test_bulk_generation();
    test_copy();
    test_system_random();
    test_performance();
    
    XPrintf("\n=== 所有XRandomGenerator测试完成 ===\n");
}

void XTestMenu_XRandomGeneratorTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XRandomGenerator(随机数生成器)");
    XTestMenu_addMenu(root, menu);
    {
        XAction* action = XTestMenu_addAction(menu, "主测试");
        XTestMenu_setActionFunction(action, XRandomGeneratorTest);
    }
}