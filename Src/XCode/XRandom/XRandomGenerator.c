/**
 * @file XRandomGenerator.c
 * @brief 随机数生成器通用实现（对齐Qt 6.8 QRandomGenerator）
 * 
 * 平台相关函数由 XRandomGenerator_platform.c 提供：
 * - XRandomGenerator_platformFillSecure() - 安全随机数填充
 */

#include "XRandomGenerator.h"
#include "XMemory.h"
#include <string.h>

/* =============== 常量定义 =============== */

#define XRANDOM_STATE_SIZE 8  /* 内部状态大小（32位字），Qt使用类似大小 */

/* =============== 内部结构定义 =============== */

/**
 * @brief 随机数生成器内部结构
 * 使用xorshift128+算法，与Qt类似的高质量PRNG
 */
struct XRandomGenerator {
    uint32_t state[XRANDOM_STATE_SIZE];  /**< 内部状态 */
    uint32_t index;                       /**< 当前索引 */
    bool isSystemGenerator;               /**< 是否为系统生成器 */
};

/* =============== 全局生成器 =============== */

static XRandomGenerator g_globalGenerator = {0};
static XRandomGenerator g_systemGenerator = {0};
static bool g_globalInitialized = false;
static bool g_systemInitialized = false;

/* =============== 平台函数声明（由平台实现提供） =============== */

/**
 * @brief 平台安全随机数填充（由XRandomGenerator_platform.c实现）
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 成功返回true
 */
extern bool XRandomGenerator_platformFillSecure(void* buffer, size_t size);

/* =============== 内部辅助函数 =============== */

/**
 * @brief xorshift128+ 风格的随机数生成
 * @param gen 生成器指针
 * @return 32位随机数
 */
static uint32_t xorshiftGenerate(XRandomGenerator* gen) {
    uint32_t* s = gen->state;
    uint32_t t = s[gen->index];
    
    /* xorshift32 变体 */
    t ^= t << 13;
    t ^= t >> 17;
    t ^= t << 5;
    
    s[gen->index] = t;
    gen->index = (gen->index + 1) % XRANDOM_STATE_SIZE;
    
    return t;
}

/**
 * @brief 初始化状态
 * @param gen 生成器指针
 * @param seed 种子值
 */
static void initializeState(XRandomGenerator* gen, uint32_t seed) {
    /* 使用简单的线性同余初始化状态数组 */
    uint32_t s = seed;
    for (int i = 0; i < XRANDOM_STATE_SIZE; i++) {
        s = s * 1103515245 + 12345;
        gen->state[i] = s;
    }
    gen->index = 0;
    gen->isSystemGenerator = false;
}

/**
 * @brief 使用种子数组初始化状态
 * @param gen 生成器指针
 * @param seeds 种子数组
 * @param count 种子数量
 */
static void initializeStateWithArray(XRandomGenerator* gen, const uint32_t* seeds, size_t count) {
    /* 混合种子数组 */
    for (int i = 0; i < XRANDOM_STATE_SIZE; i++) {
        gen->state[i] = 0;
    }
    
    for (size_t i = 0; i < count; i++) {
        gen->state[i % XRANDOM_STATE_SIZE] ^= seeds[i];
        gen->state[i % XRANDOM_STATE_SIZE] = gen->state[i % XRANDOM_STATE_SIZE] * 1103515245 + 12345;
    }
    
    /* 确保状态不全为0 */
    bool allZero = true;
    for (int i = 0; i < XRANDOM_STATE_SIZE; i++) {
        if (gen->state[i] != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        gen->state[0] = 0x12345678;
    }
    
    gen->index = 0;
    gen->isSystemGenerator = false;
}

/* =============== 构造/析构函数 =============== */

XRandomGenerator* XRandomGenerator_create(void) {
    return XRandomGenerator_createWithSeed(1);
}

XRandomGenerator* XRandomGenerator_createWithSeed(uint32_t seedValue) {
    XRandomGenerator* gen = (XRandomGenerator*)XCalloc_System(1, sizeof(XRandomGenerator));
    if (gen) {
        initializeState(gen, seedValue);
    }
    return gen;
}

XRandomGenerator* XRandomGenerator_createWithSeeds(const uint32_t* seedBuffer, size_t len) {
    return XRandomGenerator_createWithRange(seedBuffer, seedBuffer + len);
}

XRandomGenerator* XRandomGenerator_createWithRange(const uint32_t* begin, const uint32_t* end) {
    XRandomGenerator* gen = (XRandomGenerator*)XCalloc_System(1, sizeof(XRandomGenerator));
    if (gen) {
        size_t count = end - begin;
        if (count > 0) {
            initializeStateWithArray(gen, begin, count);
        } else {
            initializeState(gen, 1);
        }
    }
    return gen;
}

XRandomGenerator* XRandomGenerator_copy(const XRandomGenerator* other) {
    if (!other) {
        return NULL;
    }
    
    XRandomGenerator* gen = (XRandomGenerator*)XCalloc_System(1, sizeof(XRandomGenerator));
    if (gen) {
        memcpy(gen->state, other->state, sizeof(other->state));
        gen->index = other->index;
        gen->isSystemGenerator = other->isSystemGenerator;
    }
    return gen;
}

void XRandomGenerator_delete(XRandomGenerator* generator) {
    if (generator) {
        XFree_System(generator);
    }
}

/* =============== 种子设置 =============== */

void XRandomGenerator_seed(XRandomGenerator* generator, uint32_t seed) {
    if (generator) {
        initializeState(generator, seed);
    }
}

void XRandomGenerator_seedWithArray(XRandomGenerator* generator, const uint32_t* seedBuffer, size_t len) {
    if (generator && seedBuffer && len > 0) {
        initializeStateWithArray(generator, seedBuffer, len);
    }
}

/* =============== 随机数生成 =============== */

uint32_t XRandomGenerator_generate(XRandomGenerator* generator) {
    if (!generator) {
        return 0;
    }
    
    if (generator->isSystemGenerator) {
        uint32_t value;
        if (XRandomGenerator_platformFillSecure(&value, sizeof(value))) {
            return value;
        }
        /* 平台失败时回退到伪随机 */
        return xorshiftGenerate(generator);
    }
    
    return xorshiftGenerate(generator);
}

uint64_t XRandomGenerator_generate64(XRandomGenerator* generator) {
    uint64_t high = (uint64_t)XRandomGenerator_generate(generator) << 32;
    uint64_t low = (uint64_t)XRandomGenerator_generate(generator);
    return high | low;
}

double XRandomGenerator_generateDouble(XRandomGenerator* generator) {
    /* 生成 [0, 1) 范围的浮点数 */
    uint64_t v = XRandomGenerator_generate64(generator);
    /* 使用53位精度 */
    return (double)(v >> 11) / (double)(1ULL << 53);
}

void XRandomGenerator_fillRange32(XRandomGenerator* generator, uint32_t* buffer, size_t count) {
    if (!generator || !buffer || count == 0) {
        return;
    }
    
    if (generator->isSystemGenerator) {
        /* 批量获取系统随机数 */
        if (XRandomGenerator_platformFillSecure(buffer, count * sizeof(uint32_t))) {
            return;
        }
    }
    
    for (size_t i = 0; i < count; i++) {
        buffer[i] = XRandomGenerator_generate(generator);
    }
}

void XRandomGenerator_fillRange64(XRandomGenerator* generator, uint64_t* buffer, size_t count) {
    if (!generator || !buffer || count == 0) {
        return;
    }
    
    for (size_t i = 0; i < count; i++) {
        buffer[i] = XRandomGenerator_generate64(generator);
    }
}

void XRandomGenerator_discard(XRandomGenerator* generator, unsigned long long z) {
    if (!generator) {
        return;
    }
    
    while (z-- > 0) {
        XRandomGenerator_generate(generator);
    }
}

XRandomGenerator_result_type XRandomGenerator_call(XRandomGenerator* generator) {
    return XRandomGenerator_generate(generator);
}

/* =============== bounded() 函数族 =============== */

uint32_t XRandomGenerator_boundedU32(XRandomGenerator* generator, uint32_t highest) {
    if (highest == 0) {
        return 0;
    }
    
    /* 使用拒绝采样避免模偏差 */
    uint32_t threshold = (UINT32_MAX - highest + 1) % highest;
    uint32_t v;
    
    do {
        v = XRandomGenerator_generate(generator);
    } while (v < threshold);
    
    return v % highest;
}

uint32_t XRandomGenerator_boundedU32Range(XRandomGenerator* generator, uint32_t lowest, uint32_t highest) {
    if (highest <= lowest) {
        return lowest;
    }
    return lowest + XRandomGenerator_boundedU32(generator, highest - lowest);
}

uint64_t XRandomGenerator_boundedU64(XRandomGenerator* generator, uint64_t highest) {
    if (highest == 0) {
        return 0;
    }
    
    /* 使用拒绝采样 */
    uint64_t threshold = (UINT64_MAX - highest + 1) % highest;
    uint64_t v;
    
    do {
        v = XRandomGenerator_generate64(generator);
    } while (v < threshold);
    
    return v % highest;
}

uint64_t XRandomGenerator_boundedU64Range(XRandomGenerator* generator, uint64_t lowest, uint64_t highest) {
    if (highest <= lowest) {
        return lowest;
    }
    return lowest + XRandomGenerator_boundedU64(generator, highest - lowest);
}

int32_t XRandomGenerator_boundedI32(XRandomGenerator* generator, int32_t highest) {
    if (highest <= 0) {
        return 0;
    }
    return (int32_t)XRandomGenerator_boundedU32(generator, (uint32_t)highest);
}

int32_t XRandomGenerator_boundedI32Range(XRandomGenerator* generator, int32_t lowest, int32_t highest) {
    if (highest <= lowest) {
        return lowest;
    }
    return lowest + (int32_t)XRandomGenerator_boundedU32(generator, (uint32_t)(highest - lowest));
}

int64_t XRandomGenerator_boundedI64(XRandomGenerator* generator, int64_t highest) {
    if (highest <= 0) {
        return 0;
    }
    return (int64_t)XRandomGenerator_boundedU64(generator, (uint64_t)highest);
}

int64_t XRandomGenerator_boundedI64Range(XRandomGenerator* generator, int64_t lowest, int64_t highest) {
    if (highest <= lowest) {
        return lowest;
    }
    return lowest + (int64_t)XRandomGenerator_boundedU64(generator, (uint64_t)(highest - lowest));
}

double XRandomGenerator_boundedDouble(XRandomGenerator* generator, double highest) {
    return XRandomGenerator_generateDouble(generator) * highest;
}

/* =============== 静态函数 =============== */

XRandomGenerator_result_type XRandomGenerator_min(void) {
    return 0;
}

XRandomGenerator_result_type XRandomGenerator_max(void) {
    return UINT32_MAX;
}

XRandomGenerator* XRandomGenerator_global(void) {
    if (!g_globalInitialized) {
        /* 使用系统随机数初始化全局生成器 */
        uint32_t seed[XRANDOM_STATE_SIZE];
        if (XRandomGenerator_platformFillSecure(seed, sizeof(seed))) {
            initializeStateWithArray(&g_globalGenerator, seed, XRANDOM_STATE_SIZE);
        } else {
            initializeState(&g_globalGenerator, 1);
        }
        g_globalInitialized = true;
    }
    return &g_globalGenerator;
}

XRandomGenerator* XRandomGenerator_system(void) {
    if (!g_systemInitialized) {
        /* 系统生成器标记为使用平台安全随机数 */
        g_systemGenerator.isSystemGenerator = true;
        g_systemInitialized = true;
    }
    return &g_systemGenerator;
}

XRandomGenerator* XRandomGenerator_securelySeeded(void) {
    XRandomGenerator* gen = (XRandomGenerator*)XCalloc_System(1, sizeof(XRandomGenerator));
    if (gen) {
        uint32_t seed[XRANDOM_STATE_SIZE];
        if (XRandomGenerator_platformFillSecure(seed, sizeof(seed))) {
            initializeStateWithArray(gen, seed, XRANDOM_STATE_SIZE);
        } else {
            initializeState(gen, 1);
        }
    }
    return gen;
}

/* =============== 便捷静态函数 =============== */

uint32_t XRandomGenerator_random(void) {
    return XRandomGenerator_generate(XRandomGenerator_global());
}

uint32_t XRandomGenerator_randomBounded(uint32_t highest) {
    return XRandomGenerator_boundedU32(XRandomGenerator_global(), highest);
}

uint64_t XRandomGenerator_random64(void) {
    return XRandomGenerator_generate64(XRandomGenerator_global());
}

double XRandomGenerator_randomDouble(void) {
    return XRandomGenerator_generateDouble(XRandomGenerator_global());
}

bool XRandomGenerator_fillSecure(void* buffer, size_t size) {
    return XRandomGenerator_platformFillSecure(buffer, size);
}