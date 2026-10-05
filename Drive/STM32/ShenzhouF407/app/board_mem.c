/* ==================== 神舟号 内存布局与 XMemory 接线实现 ====================
 * 片内池规格（.intpool 段 48KB，替代桌面环境的大池默认值）：
 *   32B×512 / 64B×256 / 128B×128 / 256B×16 —— 控件树、XString、事件对象。
 * 池耗尽回落 pvPortMalloc（FreeRTOS 堆 = 外扩 SRAM）。
 */
#include "XMemory.h"
#include "XPrintf.h"
#include "board_sys.h"
#include "XMultiPool.h"
#include "XFixedPool.h"
#include "FreeRTOS.h"
#include "task.h"
#include "board_mem.h"

extern unsigned char __intpool_start;
extern unsigned char __intpool_end;

extern size_t xPortHeapBlockSize(const void* ptr);   /* heap_4：堆内块用户容量 */
extern void xPortHeapGetBounds(void** outBase, void** outEnd);
extern void vPortFree(void* pv);

static XMultiPool* s_multi;
static bool s_region_active;
static void board_sys_free(void* ptr);

static const struct
{
    size_t block;
    size_t count;
} kSubPools[] = {
    { 32, 32 },
    { 64, 16 },
    { 128, 8 },
    { 256, 2 },
};

static bool ptr_in_intpool(const void* p)
{
    const unsigned char* q = (const unsigned char*)p;
    return q >= &__intpool_start && q < &__intpool_end;
}

/* 片内池连续失败计数：池满后本轮运行停用（错误日志会经 UART 刷屏拖慢
 * 启动，且外扩 960KB 堆空间充足）。 */
static bool s_poolEnabled = true;
static unsigned s_poolFailTotal;

int board_extbuf_alloc(void** out, size_t size);
int board_extbuf_owns(const void* ptr);
int board_extbuf_grow(void* ptr, size_t oldSize, size_t newSize);
size_t board_extbuf_size_of(const void* ptr);

#define BOARD_EXTBUF_MIN 8192u

static void* board_mem_malloc(size_t size)
{
    void* p;
    if (size == 0)
        return NULL;
    /* 大块（后备存储/大图像缓冲）→ 外扩 SRAM bump 区（常驻不回收）。 */
    if (size >= BOARD_EXTBUF_MIN && board_extbuf_alloc(&p, size))
        return p;
    /* intpool 小对象池已裁剪（池满后 XMultiPool 错误日志刷屏 + 与
     * heap_4 断言相关），全部走 FreeRTOS 堆。 */
    (void)s_multi;
    return pvPortMalloc(size);                   /* 回落：FreeRTOS 堆 */
}

static void board_mem_free(void* ptr)
{
    if (!ptr)
        return;
    if (ptr_in_intpool(ptr))
        XMultiPool_free(s_multi, ptr);
    else
        /* 与 SYSTEM 方法同一守卫：非 intpool 指针一律过堆边界检查，
         * 野 free 就地停机取证，而不是静默污染 heap_4 空闲链表。 */
        board_sys_free(ptr);
}

static void* board_mem_realloc(void* ptr, size_t size)
{
    void* np;
    size_t old_cap;
    if (!ptr)
        return board_mem_malloc(size);
    if (size == 0) {
        board_mem_free(ptr);
        return NULL;
    }
    /* malloc+拷贝+free。按“源块真实容量”截断拷贝：
     * - intpool 块用池查询（块定长，可能小于新 size——原实现按新 size
     *   拷贝会越界读后面的池块）；
     * - 堆块用 heap_4 块头查询（原实现完全不拷贝，堆块 realloc 丢数据）。 */
    np = board_mem_malloc(size);
    if (!np)
        return NULL;
    if (ptr_in_intpool(ptr))
        old_cap = XMultiPool_getMaxUserSize(s_multi, ptr);
    else
        old_cap = xPortHeapBlockSize(ptr);
    {
        const unsigned char* src = (const unsigned char*)ptr;
        unsigned char* dst = (unsigned char*)np;
        size_t n = (old_cap < size) ? old_cap : size;
        size_t i;
        for (i = 0; i < n; ++i)
            dst[i] = src[i];
    }
    board_mem_free(ptr);
    return np;
}

static void* board_mem_calloc(size_t count, size_t size)
{
    void* p;
    if (count != 0 && size > (size_t)-1 / count)
        return NULL;                                 /* 乘法溢出拒绝 */
    p = board_mem_malloc(count * size);
    if (p) {
        unsigned char* q = (unsigned char*)p;
        size_t n = count * size;
        size_t i;
        for (i = 0; i < n; ++i)
            q[i] = 0;
    }
    return p;
}

static const XMemory kBoardMemory = {
    board_mem_malloc,
    board_mem_free,
    board_mem_realloc,
    board_mem_calloc,
};

/* ---------------- SYSTEM 方法守卫包装（野指针 free 捕捉器） ------------- */
int board_extbuf_alloc(void** out, size_t size);
int board_extbuf_owns(const void* ptr);
int board_extbuf_grow(void* ptr, size_t oldSize, size_t newSize);
size_t board_extbuf_size_of(const void* ptr);

#define BOARD_EXTBUF_MIN 8192u

static void* board_sys_malloc(size_t size)
{
    void* p = NULL;
    /* 大块（后备存储/大图像缓冲）→ 外扩 SRAM bump 区（常驻不回收）。 */
    if (size >= BOARD_EXTBUF_MIN && board_extbuf_alloc(&p, size))
        return p;
    return pvPortMalloc(size);
}

volatile void* g_strayFreePtr;
volatile void* g_strayFreeCaller;

static void board_sys_free(void* ptr)
{
    void* base;
    void* end;
    if (!ptr)
        return;
    if (board_extbuf_owns(ptr))
        return;                              /* bump 区：不回收 */
    xPortHeapGetBounds(&base, &end);
    if ((char*)ptr < (char*)base - 8 || (char*)ptr >= (char*)end)
    {
        g_strayFreePtr = ptr;
        g_strayFreeCaller = __builtin_return_address(0);
        XPrintf("[fatal] stray vPortFree ptr=%p caller=%p (heap corrupt)",
                ptr, g_strayFreeCaller);
        taskDISABLE_INTERRUPTS();
        for (;;)
            ;
    }
    vPortFree(ptr);
}

static unsigned long board_sys_old_usable(void* ptr)
{
    void* base;
    void* end;
    if (board_extbuf_owns(ptr)) {
        /* extbuf bump 块：读到段尾为止都安全。 */
        extern unsigned char __extbuf_end;
        return (unsigned long)(&__extbuf_end - (unsigned char*)ptr);
    }
    if (ptr_in_intpool(ptr))
        return 256;                              /* intpool 最大块。 */
    /* FreeRTOS heap_4 块：头部 xBlockSize（bit31=占用），可用 = size-8。 */
    {
        unsigned long hdr = ((unsigned long*)ptr)[-1];
        if (hdr & 0x80000000UL)
            return (hdr & 0x7FFFFFFFUL) - 8;
    }
    return 0;
}

static void* board_sys_realloc(void* ptr, size_t size)
{
    void* np;
    size_t copyLen;
    if (!ptr)
        return board_sys_malloc(size);
    if (size == 0) {
        board_sys_free(ptr);
        return NULL;
    }
    /* extbuf 块优先就地扩容（bump 尾块直接延伸，不搬不移不漏）。 */
    if (board_extbuf_owns(ptr)
        && board_extbuf_grow(ptr, board_extbuf_size_of(ptr), size))
        return ptr;
    np = board_sys_malloc(size);
    if (!np)
        return NULL;
    copyLen = board_sys_old_usable(ptr);
    if (copyLen > size)
        copyLen = size;
    if (copyLen) {
        unsigned char* d = (unsigned char*)np;
        unsigned char* s2 = (unsigned char*)ptr;
        size_t i;
        for (i = 0; i < copyLen; ++i)
            d[i] = s2[i];
    }
    board_sys_free(ptr);
    return np;
}

static void* board_sys_calloc(size_t count, size_t size)
{
    void* p = board_sys_malloc(count * size);
    if (p) {
        unsigned char* q = (unsigned char*)p;
        size_t n = count * size;
        size_t i;
        for (i = 0; i < n; ++i)
            q[i] = 0;
    }
    return p;
}

static const XMemory kSystemMemory = {
    board_sys_malloc,
    board_sys_free,
    board_sys_realloc,
    board_sys_calloc,
};

void board_mem_init(void)
{
    unsigned char* cursor = &__intpool_start;
    unsigned char* limit = &__intpool_end;
    size_t i;

    s_multi = XMultiPool_create();
    if (!s_multi)
        return;
    for (i = 0; i < sizeof(kSubPools) / sizeof(kSubPools[0]); ++i)
    {
        size_t bytes = kSubPools[i].block * kSubPools[i].count;
        XFixedPool* fp;
        if ((size_t)(limit - cursor) < bytes)
            break;
        fp = XFixedPool_create_from_memory(cursor, bytes,
                                           kSubPools[i].block);
        if (!fp)
            break;
        if (!XMultiPool_add_pool(s_multi, fp))
            break;
        cursor += bytes;
    }
    s_region_active = (cursor != &__intpool_start);
    /* MULTIPOOL 与 HYBRID 都接到片内池实现：HYBRID 的库默认实现按
     * "指针是否属于 XMultiPool_global" 分类块——我们的 intpool 指针不在
     * 它的名册上会被误判为系统块转 vPortFree（XLabel 同步路径实测崩溃）。
     * 统一实现按地址区间路由：intpool → XMultiPool_free，否则 vPortFree。 */
    XMemory_setMethod(&kBoardMemory, XMEMORY_TYPE_MULTIPOOL);
    XMemory_setMethod(&kBoardMemory, XMEMORY_TYPE_HYBRID);
    /* SYSTEM 方法也走守卫包装：野指针 free 现场捕捉（调试期）。 */
    XMemory_setMethod(&kSystemMemory, XMEMORY_TYPE_SYSTEM);
}

size_t board_mem_intpool_bytes(void)
{
    size_t total = 0;
    size_t i;
    for (i = 0; i < sizeof(kSubPools) / sizeof(kSubPools[0]); ++i)
        total += kSubPools[i].block * kSubPools[i].count;
    return s_region_active ? total : 0;
}
