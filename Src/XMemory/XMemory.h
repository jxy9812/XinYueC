/**
 * @file       XMemory.h
 * @brief      统一动态内存分配器公开 API。
 * @details    模块用途：封装系统堆、多级池、可变池与混合等多后端的内存
 *             申请、释放、重分配和零初始化接口，并提供全局内存统计与
 *             字节序读写工具；对齐 Qt 的全局内存管理职责（Qt 中无单一
 *             对应类，角色近似 qMalloc/qFree 及其池化分配后端）；
 *             平台约束：实现只依赖 XinYueC 抽象层与白名单 C 标准头，
 *             禁止直接调用 Win32、POSIX、Qt 或其他平台 API，平台差异
 *             由 Drive/ 适配层承担。
 */
#ifndef XMEMORY_H
#define XMEMORY_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include"CXinYueConfig.h"
#include"XVariablePool.h"
//全局默认的内存方法

/**
* @brief 内存申请函数指针类型
* @param size 申请的内存大小（字节数）
* @return 成功返回内存块指针，失败返回NULL
*/
typedef void* (*MallocMethod)(size_t size);
/**
* @brief 内存释放函数指针类型
* @param ptr 待释放的内存块指针
*/
typedef void (*FreeMethod)(void* ptr);
/**
* @brief 内存重分配函数指针类型
* @param ptr 原内存块指针
* @param size 新的内存大小（字节数）
* @return 成功返回新内存块指针，失败返回NULL
*/
typedef void* (*ReallocMethod)(void* ptr, size_t size);
/**
* @brief 零初始化内存分配函数指针类型
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0），失败返回NULL
*/
typedef void* (*CallocMethod)(size_t count, size_t size);
/**
* @brief 内存管理方法结构体，封装各类内存操作函数
*/
typedef struct
{
	MallocMethod malloc;      ///< 内存申请函数
	FreeMethod free;    	///< 内存释放函数
	ReallocMethod realloc;   ///< 内存重分配函数
	CallocMethod calloc;    ///< 零初始化内存分配函数
} XMemory;
/**
* @brief 字节序枚举定义
*/
typedef enum
{
	XBYTE_ORDER_LITTLE_ENDIAN = 0,  ///< 小端序：低字节在前，高字节在后 (LE)
	XBYTE_ORDER_BIG_ENDIAN,        ///< 大端序：高字节在前，低字节在后 (BE)
	XBYTE_ORDER_NATIVE            ///< 本机字节序：使用当前系统的默认字节序
} XByteOrder;
/**
* @brief 比特存储顺序枚举（控制字节内比特的高低位存储模式）
*/
typedef enum {
	XBIT_ORDER_MSB_FIRST = 0,  ///< 高位在前（Most Significant Bit First）：字节的第7位为第一个比特
	XBIT_ORDER_LSB_FIRST,       ///< 低位在前（Least Significant Bit First）：字节的第0位为第一个比特
	XBIT_ORDER_DEFAULT          ///< 默认使用XBIT_ORDER_LSB_FIRST
} XBitOrder;
/**
* @brief 内存池类型枚举
* @note 枚举值同时是 XMemory.c 内 global_Memory[] 槽位表的索引；新增类型
*       时须同步扩表，并把 XMEMORY_TYPE_COUNT 保持为最后一个成员。
*       各池可用 XMemory_config.h 的裁剪开关整体裁剪，被裁掉的槽位按
*       回落链自动换装（可变池→系统槽），调用方代码无需改动。
*/
typedef enum {
	XMEMORY_TYPE_SYSTEM,        ///< 使用系统 malloc/free（裸机默认回落全局可变池）
	XMEMORY_TYPE_MULTIPOOL,    ///< 使用 XMultiPool
	XMEMORY_TYPE_HYBRID,        ///< 组合模式：小内存用 XMultiPool，大内存用系统
	XMEMORY_TYPE_VARIABLEPOOL,  ///< 使用 XVariablePool（TLSF 可变池，嵌入式默认后端）
	XMEMORY_TYPE_COUNT          ///< 槽位总数（仅作上界哨兵，不是有效类型）
} XMemoryType;
/**
* @brief 设置全局内存管理方法
* @param method 内存管理方法结构体指针，为NULL时不执行操作
*/
void XMemory_setMethod(const XMemory* method, XMemoryType type);
/**
* @brief 获取指定内存类型的全局内存方法表
* @param type 内存类型，取值见 XMemoryType
* @return 返回库内部槽位表中对应槽位的借用指针（生命周期与库相同，
*         调用方不得释放或长期保存；可读取各成员或经 XMemory_setMethod
*         系列换装）；type 越界时返回的指针无效，禁止解引用
*/
XMemory* XMemory_method(XMemoryType type);
/**
* @brief 设置内存申请函数
* @param method 自定义的内存申请函数指针
*/
void XMemory_setMallocMethod(MallocMethod method, XMemoryType type);
/**
* @brief 设置内存释放函数
* @param method 自定义的内存释放函数指针
*/
void XMemory_setFreeMethod(FreeMethod method, XMemoryType type);
/**
* @brief 设置内存重分配函数
* @param method 自定义的内存重分配函数指针
*/
void XMemory_setReallocMethod(ReallocMethod method, XMemoryType type);
/**
* @brief 设置零初始化内存分配函数
* @param method 自定义的零初始化内存分配函数指针
*/
void XMemory_setCallocMethod(CallocMethod method, XMemoryType type);
/**
* @brief 使用malloc实现的内存重分配函数（XMemory_realloc的备选实现）
* @details 扩大内存时通过malloc+拷贝+free实现，存在数据拷贝隐患
* @param ptr 原内存块指针
* @param size 新的内存大小（字节数）
* @return 成功返回新内存块指针，失败返回NULL
*/
void* XMemory_realloc_isMalloc(void* ptr, size_t size, XMemoryType type);
/**
* @brief 使用malloc+memset实现的零初始化内存分配函数（XMemory_calloc的备选实现）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0），失败返回NULL
*/
void* XMemory_calloc_isMalloc(size_t count, size_t size, XMemoryType type);
/**
* @brief 内存申请函数（调用全局配置的allocate方法）
* @param size 申请的内存大小（字节数）
* @return 成功返回内存块指针，失败返回NULL
*/
void* XMemory_malloc(size_t size, XMemoryType type);
/**
* @brief 内存释放函数（调用全局配置的deallocate方法）
* @param ptr 待释放的内存块指针
*/
void XMemory_free(void* ptr, XMemoryType type);
/**
* @brief 内存重分配函数（调用全局配置的reallocate方法）
* @param ptr 原内存块指针
* @param size 新的内存大小（字节数）
* @return 成功返回新内存块指针，失败返回NULL
*/
void* XMemory_realloc(void* ptr, size_t size, XMemoryType type);
/**
* @brief 零初始化内存分配函数（调用全局配置的callocZero方法）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0），失败返回NULL
*/
void* XMemory_calloc(size_t count, size_t size, XMemoryType type);
/**
* @brief 检查当前内存重分配函数是否为NULL
* @return true表示realloc方法未设置（为NULL），false表示已设置
*/
bool XMemory_realloc_isNULL(XMemoryType type);

/**
* @brief 申请指定类型对象的内存（封装XMalloc_System）
* @param obj 目标对象类型（如int、struct xxx等）
* @return 成功返回对应类型的内存块指针，失败返回NULL
*/
#define XNew(type)                      (type*)XMalloc_System(sizeof(type))
/**
* @brief 释放内存（封装XFree_System）
* @param ptr 待释放的内存块指针
*/
#define XDelete(ptr)                   XFree_System(ptr);
/**
* @brief XMalloc_System的宏别名
*/
#define XMalloc                        XMemory_malloc
/**
* @brief 从系统堆申请内存（XMEMORY_TYPE_SYSTEM 槽位）
* @param size 申请的内存大小（字节数）
* @return 成功返回新申请的内存块指针（内容未初始化，为新增对象，调用方
*         必须用 XFree_System 释放），失败返回NULL
*/
void* XMalloc_System(size_t size);
/**
* @brief 从全局多级内存池申请内存（XMEMORY_TYPE_MULTIPOOL 槽位）
* @param size 申请的内存大小（字节数）
* @return 成功返回新申请的内存块指针（内容未初始化，为新增对象，调用方
*         必须用 XFree_MultiPool 释放），失败返回NULL
*/
void* XMalloc_MultiPool(size_t size);
/**
* @brief 混合模式申请内存（XMEMORY_TYPE_HYBRID 槽位）：不大于
*        XMEMORY_HYBRID_THRESHOLD 的申请走全局多级池，否则走系统堆
* @param size 申请的内存大小（字节数）
* @return 成功返回新申请的内存块指针（内容未初始化，为新增对象，调用方
*         必须用 XFree_Hybrid 释放），失败返回NULL
*/
void* XMalloc_Hybrid(size_t size);
/**
* @brief 从全局 TLSF 可变内存池申请内存（XMEMORY_TYPE_VARIABLEPOOL 槽位）
* @param size 申请的内存大小（字节数）
* @return 成功返回新申请的内存块指针（内容未初始化，为新增对象，调用方
*         必须用 XFree_VariablePool 释放），失败返回NULL
*/
void* XMalloc_VariablePool(size_t size);

/**
* @brief 使用系统内存方法申请满足指定对齐要求的内存
* @param size 申请大小（字节）
* @param alignment 对齐值，必须为2的幂
* @return 成功返回对齐后的地址，失败返回NULL
* @note 必须使用 XAlignedFree_System 释放
*/
void* XAlignedMalloc_System(size_t size, size_t alignment);
void XAlignedFree_System(void* ptr);
/**
* @brief XFree_System的宏别名
*/
#define XFree						   XMemory_free
/**
* @brief 向系统堆释放内存（XMEMORY_TYPE_SYSTEM 槽位）
* @param ptr 待释放的内存块指针，NULL 按空操作处理；只能用于释放同一
*        槽位分配函数返回的指针，释放后不得再访问
*/
void XFree_System(void* ptr);
/**
* @brief 向全局多级内存池释放内存（XMEMORY_TYPE_MULTIPOOL 槽位）
* @param ptr 待释放的内存块指针，NULL 按空操作处理；只能用于释放
*        XMalloc_MultiPool 等同槽位分配函数返回的指针，释放后不得再访问
*/
void XFree_MultiPool(void* ptr);
/**
* @brief 向混合模式释放内存（XMEMORY_TYPE_HYBRID 槽位，内部按指针来源
*        自动分派到多级池或系统堆）
* @param ptr 待释放的内存块指针，NULL 按空操作处理；只能用于释放
*        XMalloc_Hybrid 返回的指针，释放后不得再访问
*/
void XFree_Hybrid(void* ptr);
/**
* @brief 向全局 TLSF 可变内存池释放内存（XMEMORY_TYPE_VARIABLEPOOL 槽位）
* @param ptr 待释放的内存块指针，NULL 按空操作处理；只能用于释放
*        XMalloc_VariablePool 等同槽位分配函数返回的指针，释放后不得再访问
*/
void XFree_VariablePool(void* ptr);
/**
* @brief 内存重分配函数（调用全局配置的reallocate方法）
* @param ptr 原内存块指针
* @param size 新的内存大小（字节数）
* @return 成功返回新内存块指针，失败返回NULL
*/
#define XRealloc                           XMemory_realloc
/**
* @brief 使用系统堆重分配内存（XMEMORY_TYPE_SYSTEM 槽位）
* @param ptr 原内存块指针，NULL 时等同于 XMalloc_System
* @param size 新的内存大小（字节数），为0时释放原块并返回NULL
* @return 成功返回新内存块指针（须用 XFree_System 释放），失败返回NULL
*         且原块保持有效
*/
void* XRealloc_System(void* ptr, size_t size);
/**
* @brief 使用全局多级内存池重分配内存（XMEMORY_TYPE_MULTIPOOL 槽位）
* @param ptr 原内存块指针，NULL 时等同于 XMalloc_MultiPool
* @param size 新的内存大小（字节数），为0时释放原块并返回NULL
* @return 成功返回新内存块指针（须用 XFree_MultiPool 释放），失败返回
*         NULL 且原块保持有效
*/
void* XRealloc_MultiPool(void* ptr, size_t size);
/**
* @brief 使用混合模式重分配内存（XMEMORY_TYPE_HYBRID 槽位，内部按指针
*        来源在多级池与系统堆之间迁移数据）
* @param ptr 原内存块指针，NULL 时等同于 XMalloc_Hybrid
* @param size 新的内存大小（字节数），为0时释放原块并返回NULL
* @return 成功返回新内存块指针（须用 XFree_Hybrid 释放），失败返回NULL
*         且原块保持有效
*/
void* XRealloc_Hybrid(void* ptr, size_t size);
/**
* @brief 使用全局 TLSF 可变内存池重分配内存（XMEMORY_TYPE_VARIABLEPOOL 槽位）
* @param ptr 原内存块指针，NULL 时按新申请处理（size 为0返回NULL）
* @param size 新的内存大小（字节数），为0时释放原块并返回NULL
* @return 成功返回新内存块指针（须用 XFree_VariablePool 释放），失败
*         返回NULL 且原块保持有效
*/
void* XRealloc_VariablePool(void* ptr, size_t size);
/**
* @brief 零初始化内存分配函数（调用全局配置的callocZero方法）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0），失败返回NULL
*/
#define XCalloc								XMemory_calloc
/**
* @brief 使用系统堆分配零初始化内存（XMEMORY_TYPE_SYSTEM 槽位）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0，为新增对象，调用方必须
*         用 XFree_System 释放），失败返回NULL
*/
void* XCalloc_System(size_t count, size_t size);
/**
* @brief 使用全局多级内存池分配零初始化内存（XMEMORY_TYPE_MULTIPOOL 槽位）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0，为新增对象，调用方必须
*         用 XFree_MultiPool 释放），失败返回NULL
*/
void* XCalloc_MultiPool(size_t count, size_t size);
/**
* @brief 使用混合模式分配零初始化内存（XMEMORY_TYPE_HYBRID 槽位，与
*        XMalloc_Hybrid 相同的阈值分派）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0，为新增对象，调用方必须
*         用 XFree_Hybrid 释放），失败返回NULL
*/
void* XCalloc_Hybrid(size_t count, size_t size);
/**
* @brief 使用全局 TLSF 可变内存池分配零初始化内存（XMEMORY_TYPE_VARIABLEPOOL 槽位）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0，为新增对象，调用方必须
*         用 XFree_VariablePool 释放），失败返回NULL
*/
void* XCalloc_VariablePool(size_t count, size_t size);

/* ========================================================================
 * 全局内存统计
 * ======================================================================== */

/**
* @brief 是否编译全局内存统计（系统分配器统计包装与统计聚合同受此宏约束）。
* @details 置 0 时统计 API 各字段恒为 0、开关为 no-op，系统分配器直通底层
*          分配原语，嵌入式可完全免包装开销。默认开。
* @note 可在包含本头文件前或 CXinYueConfig.h 中预定义覆盖。
*/
#ifndef XMEMORY_STATISTICS_ON
#define XMEMORY_STATISTICS_ON 1
#endif

/**
* @brief XMemory 全局内存统计快照。
* @details systemBytes 为系统分配器（堆）口径，poolUsedBytes/poolTotalBytes
*          为内存池口径；两者相加即库内在用总量。百分比基准只取有固定容量
*          的内存池（poolUsedBytes/poolTotalBytes），系统堆无固定上限，不参
*          与百分比基准。
*/
typedef struct XMemoryStatistics {
	size_t systemBytes;     /**< 系统分配器当前在用字节数；按分配器可用字节
                             数计（含对齐开销，略大于请求值），平台无可用字节原语
                             或统计关闭时为 0。 */
	size_t systemPeakBytes; /**< systemBytes 的历史峰值；统计启用期间有效。 */
	size_t poolUsedBytes;   /**< 内存池已分配给用户的字节数（全局多级内存池口径）。 */
	size_t poolTotalBytes;  /**< 内存池总容量字节数；0 表示池未启用或容量未知。 */
} XMemoryStatistics;

/**
* @brief 设置全局内存统计开关。
* @param enabled true 开启统计，false 暂停统计。
* @return 无。
* @note 统计计数从启用时刻起累计，无法追溯启用前的历史分配；建议在首次
*       分配前配置。暂停期间的分配/释放在恢复后不补记。SYSTEM 类型的堆
*       记账对经 XMemory_setMethod 系列换装的自定义分配器同样生效，块大
*       小按平台可用字节原语回查，要求自定义分配器的块与平台默认堆兼容
*       （如 malloc 的包装器）；内存池口径不受影响。运行期并发切换开关
*       与分配操作竞争时按尽力而为处理。
*/
void XMemory_setStatisticsEnabled(bool enabled);

/**
* @brief 查询全局内存统计是否开启。
* @return 统计开启返回 true；运行时关闭返回 false。
* @note 宏裁剪（XMEMORY_STATISTICS_ON=0）时恒返回 false。
*/
bool XMemory_statisticsEnabled(void);

/**
* @brief 读取指定内存类型的统计快照。
* @param type 内存类型；XMEMORY_TYPE_SYSTEM 只含系统分配器（堆）口径，
*             XMEMORY_TYPE_MULTIPOOL 只含多级池口径，
*             XMEMORY_TYPE_VARIABLEPOOL 只含全局 TLSF 池口径，
*             XMEMORY_TYPE_HYBRID 为系统+多级池两路合计（混合模式的系统侧
*             分配与 SYSTEM 类型共享同一计数，无法按来源拆分）。
* @return 对应类型的统计快照；未涉及的口径字段为 0，type 越界或宏裁剪时
*         各字段恒为 0。
* @note 池的后备缓冲经系统堆分配，计入 SYSTEM 类型的 systemBytes，不随
*       池类型返回；百分比基准同样只取 poolTotalBytes>0 的类型。
*/
XMemoryStatistics XMemory_statistics(XMemoryType type);

/**
* @brief 读取指定内存类型的统计快照（即时口径，不走统计使能开关）。
* @param type 内存类型；口径与 XMemory_statistics 一致（SYSTEM/MULTIPOOL/
*             HYBRID 三选一）。
* @return 对应类型的统计快照；type 越界或宏裁剪时各字段恒为 0。
* @note 与 XMemory_statistics 的差别：本函数不受 XMemory_statisticsEnabled
*       门控，直接读取当前计数（XPerformanceOverlay 等面板直读场景使用）。
*/
XMemoryStatistics XMemory_statistics_2(XMemoryType type);

/**
* @brief 读取分配尺寸直方图的一档计数（嵌入式调池用）。
* @details XM_HISTO_ON（默认 0）打开后，XMemory_malloc 会在分发层收口处
*          按 32/64/128/256/512/1024 六个档累计「申请次数」与「申请字节
*          数」，超过 1024B 的并入末位桶。用于按真机真实分布回头调
*          XMP_GLOBAL_C32/C64/C128 的块数。
* @param bin 档位序号 0..5（对应 <=32/64/128/256/512/1024 字节）。
* @param outCount 输出：该档申请次数；可为 NULL。
* @param outBytes 输出：该档申请字节数之和；可为 NULL。
* @return 该档是否已有计数（true 表示 bin 合法且曾有分配）。
* @note 宏裁剪（XM_HISTO_ON=0）时恒返回 false，不产生任何统计开销。
*/
bool XMemory_histogram(unsigned int bin, unsigned long* outCount, unsigned long* outBytes);

/**
* @brief 读取溢出桶（>1024 字节）的累计值。
* @param outCount 输出：溢出申请次数；可为 NULL。
* @param outBytes 输出：溢出申请字节数之和；可为 NULL。
* @return 宏裁剪（XM_HISTO_ON=0）时恒返回 false。
*/
bool XMemory_histogram_big(unsigned long* outCount, unsigned long* outBytes);

/**
* @brief 读取分配失败的累计次数（与 XMemory_histogram 同档口径）。
* @details 多级池找不到匹配档位或档位耗尽时直接返回 NULL 且不回退可变池
*          （XMultiPool.c 的 XMultiPool_malloc）；可变池 arena 用尽时同理。
*          这两个「静默打空」正是嵌入式固件偶发重绘/收发失败的头号原因，
*          因此单独累计失败次数，与申请次数一起打出来即可直接定位是哪一档
*          被打爆、该加块数还是该开回退。
* @param bin 档位序号 0..5（对应 <=32/64/128/256/512/1024 字节）；传
*             >=6 表示溢出桶（>1024 字节）。
* @return 该档累计失败次数；宏裁剪（XM_HISTO_ON=0）时恒返回 0。
*/
unsigned long XMemory_histogram_fail(unsigned int bin);

/**
 * @brief 读取释放次数累计（与 XMemory_histogram 同档口径）。
 * @details 「申请数 - 释放数」按档相减 = 该尺寸档的净泄漏量，用于定位
 *          哪一类分配没有配对释放。
 * @param bin 档位序号（0..XMEMORY_HISTO_BINS-1）。
 * @param outCount 释放次数，可为 NULL。
 * @param outBytes 释放字节数，可为 NULL。
 * @return 该档是否有释放记录。
 */
bool XMemory_histogram_free(unsigned int bin, unsigned long* outCount, unsigned long* outBytes);

/**
* @brief 将指定字节序的数据流读取到内存缓冲区，并根据字节序转换
* @details 用于跨平台/协议数据交互，自动处理字节序转换，确保内存数据符合当前系统字节序
* @param[in]  src        输入数据源指针（待读取的字节流）
* @param[in]  readOrder  输入数据的字节序（XBYTE_ORDER_LITTLE_ENDIAN / BIG_ENDIAN / NATIVE）
* @param[out] out        输出内存缓冲区（存储转换后的字节数据）
* @param[in]  size       数据长度（字节数，必须 >= 1，单字节数据无需转换）
* @return bool 转换成功返回true，参数无效（空指针或长度为0）返回false
*/
bool XMemory_read_data(const uint8_t* src, XByteOrder readOrder, uint8_t* out, size_t size);
/**
* @brief 将内存缓冲区数据按指定字节序写入数据流，并根据字节序转换
* @details 用于跨平台/协议数据交互，自动处理字节序转换，确保输出数据符合目标字节序
* @param[out] write      输出数据流指针（待写入的字节流）
* @param[in]  writeOrder 输出数据的字节序（XBYTE_ORDER_LITTLE_ENDIAN / BIG_ENDIAN / NATIVE）
* @param[in]  in         输入内存缓冲区（读取转换前的字节数据）
* @param[in]  size       数据长度（字节数，必须 >= 1，单字节数据无需转换）
* @return bool 转换成功返回true，参数无效（空指针或长度为0）返回false
*/
bool XMemory_write_data(uint8_t* write, XByteOrder writeOrder, const uint8_t* in, size_t size);
/**
* @brief 从字节流读取数据并转换为指定类型变量（封装XMemory_read_data）
* @param src        输入数据源指针
* @param readOrder  输入数据的字节序
* @param varName    输出变量名
* @param varType    输出变量类型
*/
#define XMemory_Read_Var(src,readOrder,varName,varType) varType varName; XMemory_read_data(src,readOrder,&varName,sizeof(varType));
/**
* @brief 将内存数据转换为指定字节序并写入变量（封装XMemory_write_data）
* @param in         输入内存缓冲区
* @param writeOrder 输出数据的字节序
* @param varName    输出变量名
* @param varType    输出变量类型
*/
#define XMemory_Write_Var(in,writeOrder,varName,varType) varType varName; XMemory_write_data(&varName,writeOrder,in,sizeof(varType));

/**
 * @brief 内存复制（语义同 memcpy；编译器内建实现，与 libc 相同优化）。
 * @param dest 目标起始地址，须至少有 n 字节可写空间。
 * @param src 源起始地址，须至少有 n 字节可读；与 dest 不得重叠。
 * @param n 复制字节数。
 * @return dest。
 */
void* XMemcpy(void* dest, const void* src, size_t n);
/**
 * @brief 内存填充（语义同 memset；value 取低 8 位）。
 * @param dest 目标起始地址。
 * @param value 填充字节值。
 * @param n 填充字节数。
 * @return dest。
 */
void* XMemset(void* dest, int value, size_t n);

/**
 * @brief 内存移动（语义同 memmove，允许源/目标重叠）。
 * @param dest 目标起始地址。
 * @param src 源起始地址。
 * @param n 移动字节数。
 * @return dest。
 */
void* XMemmove(void* dest, const void* src, size_t n);
/**
 * @brief 内存比较（语义同 memcmp，按无符号字节）。
 * @param lhs 第一块内存起始地址。
 * @param rhs 第二块内存起始地址。
 * @param n 比较字节数。
 * @return lhs 小于/等于/大于 rhs 时分别返回负值/0/正值。
 */
int XMemcmp(const void* lhs, const void* rhs, size_t n);
#ifdef __cplusplus
}
#endif
#endif // !XMEMORY_H
