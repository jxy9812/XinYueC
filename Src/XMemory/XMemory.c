#include"XMemory.h"
#include<string.h>
#include"XMultiPool.h"
#include"XAtomic.h"
#include"XMemory_config.h"
// ============ 组合内存池（HYBRID）内部函数 ============
// 小于等于阈值走 MULTIPOOL 槽位（池被裁剪时按回落链自动换装），大于走系统堆
#if XMEMORY_HYBRID_ON
static void* hybrid_calloc(size_t count, size_t size);
static void* hybrid_realloc(void* ptr, size_t size);
static void hybrid_free(void* ptr);
static void* hybrid_malloc(size_t size);
#endif

/* ============================================================================
 * 全局内存统计（分发层按类型记账 + 池聚合）
 * ============================================================================ */
#if XMEMORY_STATISTICS_ON

/* 系统分配器可用字节原语：free/realloc 无法回查请求大小，统一按分配器
   可用字节数记账（同一块在分配/释放两侧一致），统计值略大于请求字节数。
   无可用原语的平台不编译系统口径记账，systemBytes 恒为 0（池口径不受
   影响）。 */
#if defined(_WIN32)
#include <malloc.h>
#define XMEMORY_SYSTEM_USABLE(ptr) ((size_t)_msize(ptr))
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#define XMEMORY_SYSTEM_USABLE(ptr) ((size_t)malloc_size(ptr))
#elif defined(__linux__) || defined(__GLIBC__) || defined(__BIONIC__)
#include <malloc.h>
#define XMEMORY_SYSTEM_USABLE(ptr) ((size_t)malloc_usable_size(ptr))
#elif defined(__FreeBSD__)
#include <malloc_np.h>
#define XMEMORY_SYSTEM_USABLE(ptr) ((size_t)malloc_usable_size(ptr))
#endif

/* 系统口径记账条件：统计宏开启 + 有可用字节原语；记账在
   XMemory_malloc/free/realloc/calloc 分发层完成，使用库内跨平台原子
   变量（XAtomic），全部受本宏约束。 */
#if XMEMORY_STATISTICS_ON && defined(XMEMORY_SYSTEM_USABLE) && \
    (defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__BSD__))
#define XMEMORY_STAT_TRACK_SYSTEM 1
static bool xmemory_stat_enabled = true;
/* 静态零初始化等价 XAtomic_init(var, 0)，免去运行期初始化时序问题 */
static XAtomic_SizeT xmemory_stat_systemBytes = { 0 };
static XAtomic_SizeT xmemory_stat_systemPeak = { 0 };

static void xmemory_stat_updatePeak(void)
{
	size_t current = XAtomic_load_size_t(&xmemory_stat_systemBytes, XAtomic_MemoryOrder_Relaxed);
	size_t peak = XAtomic_load_size_t(&xmemory_stat_systemPeak, XAtomic_MemoryOrder_Relaxed);
	while (current > peak &&
	       !XAtomic_compare_exchange_strong_size_t(&xmemory_stat_systemPeak, &peak,
	                                               current, XAtomic_MemoryOrder_Release,
	                                               XAtomic_MemoryOrder_Relaxed)) {
		/* expected 已被刷新为最新峰值，循环直至峰值不小于当前值 */
	}
}
#endif

#ifndef XMEMORY_STAT_TRACK_SYSTEM
#define XMEMORY_STAT_TRACK_SYSTEM 0
#endif

void XMemory_setStatisticsEnabled(bool enabled)
{
#if XMEMORY_STAT_TRACK_SYSTEM
	xmemory_stat_enabled = enabled;
#else
	(void)enabled;
#endif
}

bool XMemory_statisticsEnabled(void)
{
#if XMEMORY_STAT_TRACK_SYSTEM
	return xmemory_stat_enabled;
#else
	return false;
#endif
}

XMemoryStatistics XMemory_statistics_2(XMemoryType type)
{
	XMemoryStatistics stats;
	XMemset(&stats, 0, sizeof(stats));
	if (type < XMEMORY_TYPE_SYSTEM || type >= XMEMORY_TYPE_COUNT)
		return stats;
#if XMEMORY_STAT_TRACK_SYSTEM
	if (type == XMEMORY_TYPE_SYSTEM || type == XMEMORY_TYPE_HYBRID) {
		stats.systemBytes = XAtomic_load_size_t(&xmemory_stat_systemBytes, XAtomic_MemoryOrder_Relaxed);
		stats.systemPeakBytes = XAtomic_load_size_t(&xmemory_stat_systemPeak, XAtomic_MemoryOrder_Relaxed);
	}
#endif
	/* 池未惰性创建时不触发创建，避免统计读取自身改变内存布局 */
#if XMEMORY_MULTIPOOL_ON
	if ((type == XMEMORY_TYPE_MULTIPOOL || type == XMEMORY_TYPE_HYBRID) &&
	    XMultiPool_global_isInited()) {
		XMultiPool* pool = XMultiPool_global();
		stats.poolTotalBytes = XMultiPool_totalSize(pool);
		stats.poolUsedBytes = stats.poolTotalBytes - XMultiPool_freeSize(pool);
	}
#endif
#if XMEMORY_VARIABLEPOOL_ON
	if (type == XMEMORY_TYPE_VARIABLEPOOL && XVariablePool_global_isInited()) {
		XVariablePool* pool = XVariablePool_global();
		stats.poolTotalBytes = XVariablePool_totalSize(pool);
		stats.poolUsedBytes = stats.poolTotalBytes - XVariablePool_freeSize(pool);
	}
#endif
	return stats;
}

/* 与 _2 同口径：使能开关只门控记账，快照读取不受 XMemory_setStatisticsEnabled
   影响；此前统计开启分支缺失本符号（仅 OFF 分支有定义），此处补齐。 */
XMemoryStatistics XMemory_statistics(XMemoryType type)
{
	return XMemory_statistics_2(type);
}

#else /* XMEMORY_STATISTICS_ON == 0 */

void XMemory_setStatisticsEnabled(bool enabled)
{
	(void)enabled;
}

bool XMemory_statisticsEnabled(void)
{
	return false;
}

XMemoryStatistics XMemory_statistics(XMemoryType type)
{
	XMemoryStatistics stats;
	XMemset(&stats, 0, sizeof(stats));
	(void)type;
	return stats;
}

#endif /* XMEMORY_STATISTICS_ON */

#if XMEMORY_STAT_TRACK_SYSTEM

static size_t xmemory_system_usable(void* ptr)
{
	return ptr ? XMEMORY_SYSTEM_USABLE(ptr) : 0;
}

static void xmemory_stat_alloc(size_t block)
{
	XAtomic_fetch_add_size_t(&xmemory_stat_systemBytes, block, XAtomic_MemoryOrder_Release);
	xmemory_stat_updatePeak();
}

static void xmemory_stat_release(size_t block)
{
	if (block)
		XAtomic_fetch_sub_size_t(&xmemory_stat_systemBytes, block, XAtomic_MemoryOrder_Release);
}

static void xmemory_stat_realloc(size_t oldBlock, size_t newBlock)
{
	if (newBlock >= oldBlock)
		XAtomic_fetch_add_size_t(&xmemory_stat_systemBytes, newBlock - oldBlock, XAtomic_MemoryOrder_Release);
	else
		XAtomic_fetch_sub_size_t(&xmemory_stat_systemBytes, oldBlock - newBlock, XAtomic_MemoryOrder_Release);
	xmemory_stat_updatePeak();
}

#endif /* XMEMORY_STAT_TRACK_SYSTEM */

/* 槽位表函数内嵌记账/清账（设计：分配函数内嵌记账、释放函数内嵌清账）——
 * 任何调用路径（XMemory_free 封装、XClass 直调 method->free、其他裸
 * method->free）账目自动对称；自定义表未内嵌记账，两侧直调同样对称。 */
static void* xmemory_system_malloc(size_t size)
{
	void* ptr = malloc(size);
#if XMEMORY_STAT_TRACK_SYSTEM
	if (ptr && xmemory_stat_enabled)
		xmemory_stat_alloc(xmemory_system_usable(ptr));
#endif
	return ptr;
}
static void* xmemory_system_realloc(void* ptr, size_t size)
{
#if XMEMORY_STAT_TRACK_SYSTEM
	size_t oldBlock = ptr ? xmemory_system_usable(ptr) : 0;
	void* newPtr = realloc(ptr, size);
	/* 失败（newPtr 为 NULL 且 size 非零）时原块保持有效，不调整计数；
	   realloc(ptr, 0) 释放原块并返回 NULL，按释放调整。 */
	if ((newPtr || size == 0) && xmemory_stat_enabled)
		xmemory_stat_realloc(oldBlock, xmemory_system_usable(newPtr));
	return newPtr;
#else
	return realloc(ptr, size);
#endif
}
static void* xmemory_system_calloc(size_t count, size_t size)
{
	void* ptr = calloc(count, size);
#if XMEMORY_STAT_TRACK_SYSTEM
	if (ptr && xmemory_stat_enabled)
		xmemory_stat_alloc(xmemory_system_usable(ptr));
#endif
	return ptr;
}
static void xmemory_system_free(void* ptr)
{
#if XMEMORY_STAT_TRACK_SYSTEM
	if (ptr && xmemory_stat_enabled)
		xmemory_stat_release(xmemory_system_usable(ptr));
#endif
	free(ptr);
}

/* ============================================================================
 * 分发槽位表（global_Memory[type]，索引即 XMemoryType 枚举值）
 * 各槽位入口由 XMemory_config.h 裁剪开关决定：被裁掉的池按回落链换装
 * （可变池→系统槽），保证表内无空函数指针。唯一例外：裸机且可变池也被
 * 裁时系统槽无堆可用（malloc/free 为 NULL），需固件经 XMemory_setMethod
 * 接入自定义后端。
 * ============================================================================ */
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include<stdlib.h>
/* 桌面：系统槽 = libc malloc 族（统计包装版） */
#define XMEMORY_SLOT_SYSTEM {xmemory_system_malloc,xmemory_system_free,xmemory_system_realloc,xmemory_system_calloc}
#elif defined(__FreeRTOS__)
#include"FreeRTOS.h"
/* 2 参槽包装：ReallocMethod/CallocMethod 是 2 参函数指针，而 isMalloc 实现为
 * 3 参（末参 type）。经 2 参槽直接调 3 参实现时，xtensa 窗口化尾调用（jx）
 * 会把 type 所在的 a4 复用为跳转目标寄存器 → isMalloc 读到垃圾 type →
 * global_Memory[type] 越界 → 野指针跳转（ESP8266 真机：Fatal exception 0
 * IllegalInstructionCause epc1=0xbaa9e7b8）。以 2 参包装承接槽调用，再以
 * 普通 call 显式 3 参直调 isMalloc，type 正确经 a4 传递。 */
static void* __attribute__((unused)) xmemory_realloc_slot(void* ptr, size_t size)
{
	return XMemory_realloc_isMalloc(ptr, size, XMEMORY_TYPE_SYSTEM);
}
static void* __attribute__((unused)) xmemory_calloc_slot(size_t count, size_t size)
{
	return XMemory_calloc_isMalloc(count, size, XMEMORY_TYPE_SYSTEM);
}
/* FreeRTOS：系统槽 = RTOS 堆（pvPortMalloc/vPortFree，heap_1~5 五选一提供实现） */
#define XMEMORY_SLOT_SYSTEM { pvPortMalloc,vPortFree,xmemory_realloc_slot,xmemory_calloc_slot }
#else//裸机环境
#if XMEMORY_VARIABLEPOOL_ON
/* 裸机：无 RTOS 堆，系统槽默认接入全局可变池（.bss 静态 arena，零堆依赖），
 * XMalloc_System/XNew 等既有调用开箱即用；仍可经 XMemory_setMethod 换装
 * 自定义后端。 */
#define XMEMORY_SLOT_SYSTEM {XVariablePool_global_malloc,XVariablePool_global_free,XVariablePool_global_realloc,XVariablePool_global_calloc}
#else
/* 裸机且可变池被裁：系统级分配不可用，固件需经 XMemory_setMethod 接入
 * 自定义后端（按槽位类型选择）。 */
#define XMEMORY_SLOT_SYSTEM { NULL,NULL,xmemory_realloc_slot,xmemory_calloc_slot }
#endif
#endif

/* MULTIPOOL 槽位：XMultiPool → 裁剪时回落全局可变池 → 再回落系统槽 */
#if XMEMORY_MULTIPOOL_ON
#define XMEMORY_SLOT_MULTIPOOL {XMultiPool_global_malloc,XMultiPool_global_free,XMultiPool_global_realloc,XMultiPool_global_calloc}
#elif XMEMORY_VARIABLEPOOL_ON
#define XMEMORY_SLOT_MULTIPOOL {XVariablePool_global_malloc,XVariablePool_global_free,XVariablePool_global_realloc,XVariablePool_global_calloc}
#else
#define XMEMORY_SLOT_MULTIPOOL XMEMORY_SLOT_SYSTEM
#endif

/* VARIABLEPOOL 槽位：全局 TLSF 池 → 裁剪时回落系统槽 */
#if XMEMORY_VARIABLEPOOL_ON
#define XMEMORY_SLOT_VARIABLEPOOL {XVariablePool_global_malloc,XVariablePool_global_free,XVariablePool_global_realloc,XVariablePool_global_calloc}
#else
#define XMEMORY_SLOT_VARIABLEPOOL XMEMORY_SLOT_SYSTEM
#endif

/* HYBRID 槽位：混合分派 → 裁剪时回落系统槽 */
#if XMEMORY_HYBRID_ON
#define XMEMORY_SLOT_HYBRID {hybrid_malloc,hybrid_free,hybrid_realloc,hybrid_calloc}
#else
#define XMEMORY_SLOT_HYBRID XMEMORY_SLOT_SYSTEM
#endif

/* ============================================================================
 * 分配尺寸直方图（XM_HISTO_ON，默认 0 = 不编译，零开销）
 * ============================================================================
 * 分档按多级池的块尺寸走（32/64/128/256/512/1024/+），溢出并入 "big" 桶。
 * 只统计「申请次数」与「申请字节数」，不做 per-pointer 记账，因此不需要与
 * free 侧对称，也就不干扰 XMemory 自身的账本。 */
#ifndef XM_HISTO_ON
#define XM_HISTO_ON 0
#endif

#if XM_HISTO_ON
/* 末位桶 = >1024B */
#define XMEMORY_HISTO_BINS 6
static unsigned long xmemory_histo_count[XMEMORY_HISTO_BINS];
static unsigned long xmemory_histo_bytes[XMEMORY_HISTO_BINS];
static unsigned long xmemory_histo_fail[XMEMORY_HISTO_BINS];
static size_t xmemory_histo_big = (size_t)-1;
static unsigned long xmemory_histo_big_count;
static unsigned long xmemory_histo_big_fail;

/* 求档（溢出并入 XMEMORY_HISTO_BINS 这个「末位桶」下标）。成功与失败记账
 * 共用同一函数，故失败必然落在与成功相同的档。 */
static unsigned xmemory_histogram_bin(size_t size)
{
	unsigned b = 0;
	size_t cap = 32;
	while (cap < size && b + 1 < XMEMORY_HISTO_BINS) { cap <<= 1; ++b; }
	return (cap < size) ? XMEMORY_HISTO_BINS : b;
}

static void xmemory_histogram_bump(size_t size)
{
	unsigned b = xmemory_histogram_bin(size);
	if (b >= XMEMORY_HISTO_BINS) { xmemory_histo_big_count++; xmemory_histo_big += size; return; }
	xmemory_histo_count[b]++;
	xmemory_histo_bytes[b] += size;
}

/* 分配失败计数。XMultiPool_malloc 找不到档位或档位耗尽时直接 return NULL
 * 且【不回退】可变池（XMultiPool.c:145-159），可变池 arena 用尽时同理。
 * 不打这个计数就分不清「GUI 逐拍重绘失败」是池被打空还是别的原因。 */
static void xmemory_histogram_bump_fail(size_t size)
{
	unsigned b = xmemory_histogram_bin(size);
	if (b >= XMEMORY_HISTO_BINS) { xmemory_histo_big_fail++; return; }
	xmemory_histo_fail[b]++;
}

/* 释放侧直方图（2026-10-19 泄漏定位）：free 路径拿不到原始 size，但
 * XMultiPool/XVariablePool 都能回查块的实际大小，于是
 * 「申请数 - 释放数」按尺寸档相减 = 每个尺寸档的净泄漏。
 * 只有 XM_HISTO_ON 打开时才有额外开销。 */
static unsigned long xmemory_histo_free_count[XMEMORY_HISTO_BINS];
static unsigned long xmemory_histo_free_bytes[XMEMORY_HISTO_BINS];
static unsigned long xmemory_histo_big_free_count;

static void xmemory_histogram_bump_free(size_t size)
{
	unsigned b = xmemory_histogram_bin(size);
	if (b >= XMEMORY_HISTO_BINS) { xmemory_histo_big_free_count++; return; }
	xmemory_histo_free_count[b]++;
	xmemory_histo_free_bytes[b] += size;
}

bool XMemory_histogram(unsigned int bin, unsigned long* outCount, unsigned long* outBytes)
{
	if (outCount) *outCount = 0;
	if (outBytes) *outBytes = 0;
	if (bin >= XMEMORY_HISTO_BINS) return false;
	if (outCount) *outCount = xmemory_histo_count[bin];
	if (outBytes) *outBytes = xmemory_histo_bytes[bin];
	return xmemory_histo_count[bin] != 0;
}

unsigned long XMemory_histogram_fail(unsigned int bin)
{
	if (bin >= XMEMORY_HISTO_BINS) return xmemory_histo_big_fail;
	return xmemory_histo_fail[bin];
}

bool XMemory_histogram_big(unsigned long* outCount, unsigned long* outBytes)
{
	if (outCount) *outCount = 0;
	if (outBytes) *outBytes = 0;
	if (outCount) *outCount = xmemory_histo_big_count;
	if (outBytes) *outBytes = (unsigned long)xmemory_histo_big;
	return xmemory_histo_big_count != 0;
}

bool XMemory_histogram_free(unsigned int bin, unsigned long* outCount, unsigned long* outBytes)
{
	if (outCount) *outCount = 0;
	if (outBytes) *outBytes = 0;
	if (bin >= XMEMORY_HISTO_BINS) return false;
	if (outCount) *outCount = xmemory_histo_free_count[bin];
	if (outBytes) *outBytes = xmemory_histo_free_bytes[bin];
	return xmemory_histo_free_count[bin] != 0;
}
#else /* XM_HISTO_ON == 0 */

bool XMemory_histogram(unsigned int bin, unsigned long* outCount, unsigned long* outBytes)
{
	(void)bin;
	if (outCount) *outCount = 0;
	if (outBytes) *outBytes = 0;
	return false;
}

unsigned long XMemory_histogram_fail(unsigned int bin)
{
	(void)bin;
	return 0;
}

bool XMemory_histogram_big(unsigned long* outCount, unsigned long* outBytes)
{
	if (outCount) *outCount = 0;
	if (outBytes) *outBytes = 0;
	return false;
}

bool XMemory_histogram_free(unsigned int bin, unsigned long* outCount, unsigned long* outBytes)
{
	(void)bin;
	if (outCount) *outCount = 0;
	if (outBytes) *outBytes = 0;
	return false;
}
#endif /* XM_HISTO_ON */

static XMemory global_Memory[] = {
	XMEMORY_SLOT_SYSTEM,         /* [0] XMEMORY_TYPE_SYSTEM      */
	XMEMORY_SLOT_MULTIPOOL,      /* [1] XMEMORY_TYPE_MULTIPOOL   */
	XMEMORY_SLOT_HYBRID,         /* [2] XMEMORY_TYPE_HYBRID      */
	XMEMORY_SLOT_VARIABLEPOOL    /* [3] XMEMORY_TYPE_VARIABLEPOOL */
};

/* 记账/清账已内嵌于槽位表函数本体（上方 xmemory_system_* 系列）——
 * 封装层保持纯直通：若此处再挂钩子，与表函数内嵌账本叠加即双重记账/
 * 双重清账（账本只能挂在一层）。 */
void* XMemory_malloc(size_t size, XMemoryType type)
{
#if XM_HISTO_ON
	/* 一次性尺寸直方图（默认 0 不编译）：分发层是全部 XMalloc_* 槽位的
	 * 唯一收口，挂在这一层即可覆盖框架对象、lwip 与裸 malloc 的所有申请。
	 * 固件侧用 XMemory_histogram_dump() 在启动后打一次，用真实分布回头调
	 * XMP_GLOBAL_C32/C64/C128 的块数。 */
	xmemory_histogram_bump(size);
#endif
	{
		/* 槽位可能为空：裸机且可变池也被裁、未经 XMemory_setMethod 接入自定义
		 * 后端时系统槽无堆可用（槽位表注释所述的唯一例外）。判空返回 NULL，
		 * 让调用方走自己的兜底，而不是空函数指针跳转。 */
		MallocMethod method = global_Memory[type].malloc;
		void* p = method ? method(size) : NULL;
#if XM_HISTO_ON
		if (!p)
			xmemory_histogram_bump_fail(size);
#endif
		return p;
	}
}
void* XMemory_realloc(void* ptr, size_t size, XMemoryType type)
{
	ReallocMethod method = global_Memory[type].realloc;
	return method ? method(ptr, size) : NULL;
}

void* XMemory_calloc(size_t count, size_t size, XMemoryType type)
{
	CallocMethod method = global_Memory[type].calloc;
	return method ? method(count, size) : NULL;
}

void XMemory_free(void* ptr, XMemoryType type)
{
	FreeMethod method = global_Memory[type].free;
	if (method) {
		method(ptr);
	}
}
void* XMalloc_System(size_t size)
{
	return XMemory_malloc(size, XMEMORY_TYPE_SYSTEM);
}
void* XMalloc_MultiPool(size_t size)
{
	return XMemory_malloc(size, XMEMORY_TYPE_MULTIPOOL);
}
void* XMalloc_Hybrid(size_t size)
{
	return XMemory_malloc(size, XMEMORY_TYPE_HYBRID);
}
void* XMalloc_VariablePool(size_t size)
{
	return XMemory_malloc(size, XMEMORY_TYPE_VARIABLEPOOL);
}
void* XAlignedMalloc_System(size_t size, size_t alignment)
{
	if (alignment < sizeof(void*))
		alignment = sizeof(void*);
	if ((alignment & (alignment - 1)) != 0)
		return NULL;

	size_t overhead = alignment - 1 + sizeof(void*);
	if (size > SIZE_MAX - overhead)
		return NULL;

	void* allocation = XMalloc_System(size + overhead);
	if (!allocation)
		return NULL;

	uintptr_t address = (uintptr_t)allocation + sizeof(void*);
	uintptr_t aligned = (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
	((void**)aligned)[-1] = allocation;
	return (void*)aligned;
}
void XAlignedFree_System(void* ptr)
{
	if (ptr)
		XFree_System(((void**)ptr)[-1]);
}
void XFree_System(void* ptr)
{
	XFree(ptr, XMEMORY_TYPE_SYSTEM);
}
void XFree_MultiPool(void* ptr)
{
	XFree(ptr, XMEMORY_TYPE_MULTIPOOL);
}
void XFree_Hybrid(void* ptr)
{
	XFree(ptr, XMEMORY_TYPE_HYBRID);
}
void XFree_VariablePool(void* ptr)
{
	XFree(ptr, XMEMORY_TYPE_VARIABLEPOOL);
}
void* XRealloc_System(void* ptr, size_t size)
{
	return XMemory_realloc(ptr, size, XMEMORY_TYPE_SYSTEM);
}
void* XRealloc_MultiPool(void* ptr, size_t size)
{
	return XMemory_realloc(ptr, size, XMEMORY_TYPE_MULTIPOOL);
}
void* XRealloc_Hybrid(void* ptr, size_t size)
{
	return XMemory_realloc(ptr, size, XMEMORY_TYPE_HYBRID);
}
void* XRealloc_VariablePool(void* ptr, size_t size)
{
	return XMemory_realloc(ptr, size, XMEMORY_TYPE_VARIABLEPOOL);
}
void* XCalloc_System(size_t count, size_t size)
{
	return XMemory_calloc(count, size, XMEMORY_TYPE_SYSTEM);
}
void* XCalloc_MultiPool(size_t count, size_t size)
{
	return XMemory_calloc(count, size, XMEMORY_TYPE_MULTIPOOL);
}
void* XCalloc_Hybrid(size_t count, size_t size)
{
	return  XMemory_calloc(count, size, XMEMORY_TYPE_HYBRID);
}
void* XCalloc_VariablePool(size_t count, size_t size)
{
	return  XMemory_calloc(count, size, XMEMORY_TYPE_VARIABLEPOOL);
}
void XMemory_setMethod(const XMemory* method, XMemoryType type)
{
	if(method)
		global_Memory[type] = *method;
}
XMemory* XMemory_method(XMemoryType type)
{
	return global_Memory+(int)type ;
}
void XMemory_setMallocMethod(MallocMethod method, XMemoryType type)
{
	global_Memory[type].malloc = method;
}
void XMemory_setReallocMethod(ReallocMethod method, XMemoryType type)
{
	global_Memory[type].realloc = method;
}
void XMemory_setCallocMethod(CallocMethod method, XMemoryType type)
{
	global_Memory[type].calloc = method;
}

void XMemory_setFreeMethod(FreeMethod method, XMemoryType type)
{
	global_Memory[type].free = method;
}


bool XMemory_realloc_isNULL(XMemoryType type)
{
	return global_Memory[type].realloc==NULL;
}

bool XMemory_read_data(const uint8_t* src, XByteOrder readOrder, uint8_t* out, size_t size)
{
	// 参数合法性检查：输入/输出指针为空或数据长度为0时返回失败
	if (src == NULL || out == NULL || size == 0)
		return false;

	// 根据当前系统字节序和输入数据字节序判断是否需要转换
#if IS_BIG_ENDIAN  // 当前系统是大端字节序
	// 若输入数据是小端字节序，则需要转换（大端 <-> 小端）
	if (readOrder == XBYTE_ORDER_LITTLE_ENDIAN)
#else  // 当前系统是小端字节序（默认分支）
	// 若输入数据是大端字节序，则需要转换（小端 <-> 大端）
	if (readOrder == XBYTE_ORDER_BIG_ENDIAN)
#endif
	{
		// 字节序转换：反转字节顺序（低地址字节与高地址字节互换）
		// 例：输入 [0x12, 0x34, 0x56]（3字节）-> 输出 [0x56, 0x34, 0x12]
		for (size_t i = 0; i < size; i++)
		{
			out[i] = src[size - 1 - i];  // 第i个位置存储原数据的倒数第i个字节
		}
	}
	else
	{
		// 无需转换：输入数据字节序与当前系统一致，直接拷贝
		memcpy(out, src, size);
	}

	return true;
}

bool XMemory_write_data(uint8_t* write, XByteOrder writeOrder, const uint8_t* in, size_t size)
{
	// 参数合法性检查：输入/输出指针为空或数据长度为0时返回失败
	if (write == NULL || in == NULL || size == 0)
		return false;

	// 根据当前系统字节序和输入数据字节序判断是否需要转换
#if IS_BIG_ENDIAN  // 当前系统是大端字节序
	// 若输入数据是小端字节序，则需要转换（大端 <-> 小端）
	if (readOrder == XBYTE_ORDER_LITTLE_ENDIAN)
#else  // 当前系统是小端字节序（默认分支）
	// 若输入数据是大端字节序，则需要转换（小端 <-> 大端）
	if (writeOrder == XBYTE_ORDER_BIG_ENDIAN)
#endif
	{
		// 字节序转换：反转字节顺序（低地址字节与高地址字节互换）
		// 例：输入 [0x12, 0x34, 0x56]（3字节）-> 输出 [0x56, 0x34, 0x12]
		for (size_t i = 0; i < size; i++)
		{
			write[size - 1 - i]=in[i];  // 第i个位置存储原数据的倒数第i个字节
		}
	}
	else
	{
		// 无需转换：输入数据字节序与当前系统一致，直接拷贝
		memcpy(write,in, size);
	}

	return true;
}
void* XMemcpy(void* dest, const void* src, size_t n)
{
	return memcpy(dest, src, n);
}
void* XMemset(void* dest, int value, size_t n)
{
	return memset(dest, value, n);
}

void* XMemmove(void* dest, const void* src, size_t n)
{
	return memmove(dest, src, n);
}

int XMemcmp(const void* lhs, const void* rhs, size_t n)
{
	return memcmp(lhs, rhs, n);
}

void* XMemory_realloc_isMalloc(void* ptr, size_t size, XMemoryType type)
{
	if (ptr == NULL)
		return XMalloc(size, type);
	if (size == 0)
	{
		XFree(ptr, type);
		return NULL;
	}
	void* newPtr = XMalloc(size, type);
	if (newPtr == NULL)
		return NULL;
	memcpy(newPtr,ptr,size);
	XFree(ptr, type);
	return newPtr;
}

void* XMemory_calloc_isMalloc(size_t count, size_t size, XMemoryType type)
{
	void* ptr = XMalloc(count*size, type);
	if (ptr)
		memset(ptr,0,count*size);
	return ptr;
}

#if XMEMORY_HYBRID_ON
static void* hybrid_malloc(size_t size) {
#if XMEMORY_MULTIPOOL_ON
	if (size <= XMEMORY_HYBRID_THRESHOLD) {
		void* ptr = XMemory_malloc(size, XMEMORY_TYPE_MULTIPOOL);
		// 快路径没命中（档位缺失或该档耗尽）时回退变量池：池化本该是加速，
		// 不能因为池满就让分配硬失败（嵌入式小内存平台上这一条能直接掐死应用链）
		if (ptr != NULL) {
			return ptr;
		}
	}
#endif
	return XMalloc_System(size);
}

static void hybrid_free(void* ptr)
{
	if (ptr == NULL) return;

#if XMEMORY_MULTIPOOL_ON
	// 利用 XMultiPool_is_from_pool 来判断指针来源
	// 注意: 这里传入了全局池实例
	if (XMultiPool_is_from_pool(XMultiPool_global(), ptr)) {
#if XM_HISTO_ON
		/* 释放侧直方图：回查块大小以定位净泄漏的尺寸档 */
		xmemory_histogram_bump_free(XMultiPool_getMaxUserSize(XMultiPool_global(), ptr));
#endif
		XFree_MultiPool(ptr);
		return;
	}
#endif
#if XMEMORY_VARIABLEPOOL_ON
	// MULTIPOOL 槽位被裁时小块可能来自全局可变池；不触发惰性创建——
	// 池未初始化则不可能是其块
	if (XVariablePool_global_isInited() &&
	    XVariablePool_is_from_pool(XVariablePool_global(), ptr)) {
#if XM_HISTO_ON
		xmemory_histogram_bump_free(XVariablePool_getMaxUserSize(XVariablePool_global(), ptr));
#endif
		XFree_VariablePool(ptr);
		return;
	}
#endif
	XFree_System(ptr);
}

static void* hybrid_realloc(void* ptr, size_t size)
{
	if (ptr == NULL) {
		return hybrid_malloc(size);
	}
	if (size == 0) {
		hybrid_free(ptr);
		return NULL;
	}

#if XMEMORY_MULTIPOOL_ON
	{
		XMultiPool* global_pool = XMultiPool_global();
		if (XMultiPool_is_from_pool(global_pool, ptr)) {
			if (size <= XMEMORY_HYBRID_THRESHOLD) {
				// 原块和新块都适合 XMultiPool
				void* new_ptr = XMultiPool_global_realloc(ptr, size);
				if (new_ptr != NULL) {
					return new_ptr;
				}
				// 池内 realloc 失败（耗尽）→ 回退变量池并搬运，原块释放
				size_t old_user_size = XMultiPool_getMaxUserSize(global_pool, ptr);
				new_ptr = XMalloc_System(size);
				if (new_ptr != NULL) {
					memcpy(new_ptr, ptr, (old_user_size < size) ? old_user_size : size);
					XFree_MultiPool(ptr);
				}
				return new_ptr;
			}
			else {
				// 原块来自 XMultiPool，但新块太大，需要迁移到系统堆
				void* new_ptr = XMalloc_System(size);
				if (new_ptr != NULL) {
					// 使用公开 API XMultiPool_getMaxUserSize 回查原块可用大小
					size_t old_user_size = XMultiPool_getMaxUserSize(global_pool, ptr);
					memcpy(new_ptr, ptr, (old_user_size < size) ? old_user_size : size);
					XFree_MultiPool(ptr);
				}
				return new_ptr;
			}
		}
	}
#endif
#if XMEMORY_VARIABLEPOOL_ON
	{
		// 原块可能来自全局可变池（MULTIPOOL 槽位回落或 VARIABLEPOOL 直配）
		XVariablePool* global_pool = XVariablePool_global_isInited() ? XVariablePool_global() : NULL;
		if (global_pool && XVariablePool_is_from_pool(global_pool, ptr)) {
			if (size <= XMEMORY_HYBRID_THRESHOLD) {
				// 原块和新块都留在可变池
				void* new_ptr = XVariablePool_global_realloc(ptr, size);
				if (new_ptr != NULL) {
					return new_ptr;
				}
				// 可变池 realloc 失败 → 回退系统堆并搬运，原块释放
				size_t old_user_size = XVariablePool_getMaxUserSize(global_pool, ptr);
				new_ptr = XMalloc_System(size);
				if (new_ptr != NULL) {
					memcpy(new_ptr, ptr, (old_user_size < size) ? old_user_size : size);
					XFree_VariablePool(ptr);
				}
				return new_ptr;
			}
			else {
				// 原块来自可变池，但新块太大，需要迁移到系统堆
				void* new_ptr = XMalloc_System(size);
				if (new_ptr != NULL) {
					size_t old_user_size = XVariablePool_getMaxUserSize(global_pool, ptr);
					memcpy(new_ptr, ptr, (old_user_size < size) ? old_user_size : size);
					XFree_VariablePool(ptr);
				}
				return new_ptr;
			}
		}
	}
#endif
	{
		// 原块来自系统堆
		if (size <= XMEMORY_HYBRID_THRESHOLD) {
			// 分配一个池块并复制数据
			void* new_ptr = XMemory_malloc(size, XMEMORY_TYPE_MULTIPOOL);
			if (new_ptr != NULL) {
				// 对于系统堆的指针，无法知道确切大小，这是一个已知限制。
				memcpy(new_ptr, ptr, size);
				XFree_System(ptr);
				return new_ptr;
			}
			// 池不可用 → 留在系统堆 realloc，不能丢原块后返回 NULL
			return XRealloc_System(ptr, size);
		}
		else {
			// 原块和新块都适合系统堆
			return XRealloc_System(ptr, size);
		}
	}
}

static void* hybrid_calloc(size_t count, size_t size) {
	if (count == 0 || size == 0) return NULL;
	if (count > SIZE_MAX / size) return NULL; // 防止溢出

	size_t total_size = count * size;
#if XMEMORY_MULTIPOOL_ON
	if (total_size <= XMEMORY_HYBRID_THRESHOLD) {
		void* ptr = XMemory_calloc(count, size, XMEMORY_TYPE_MULTIPOOL);
		if (ptr != NULL) {
			return ptr;
		}
		// 同 hybrid_malloc：池不可用时走慢路径，而不是返回 NULL
	}
#endif
	return XCalloc_System(count, size);
}
#endif /* XMEMORY_HYBRID_ON */
