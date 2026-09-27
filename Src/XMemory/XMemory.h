#ifndef XMEMORY_H
#define XMEMORY_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"CXinYueConfig.h"
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
 */
typedef enum {
	XMEMORY_TYPE_SYSTEM,        ///< 使用系统 malloc/free
	XMEMORY_TYPE_MULTIPOOL,    ///< 使用 XMultiPool
	XMEMORY_TYPE_HYBRID         ///< 组合模式：小内存用 XMultiPool，大内存用系统
} XMemoryType;
/**
* @brief 设置全局内存管理方法
* @param method 内存管理方法结构体指针，为NULL时不执行操作
*/
void XMemory_setMethod(const XMemory* method, XMemoryType type);
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
void* XMalloc_System(size_t size);
void* XMalloc_MultiPool(size_t size);
void* XMalloc_Hybrid(size_t size);

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
void XFree_System(void* ptr);
void XFree_MultiPool(void* ptr);
void XFree_Hybrid(void* ptr);
/**
* @brief 内存重分配函数（调用全局配置的reallocate方法）
* @param ptr 原内存块指针
* @param size 新的内存大小（字节数）
* @return 成功返回新内存块指针，失败返回NULL
*/
#define XRealloc                           XMemory_realloc
void* XRealloc_System(void* ptr, size_t size);
void* XRealloc_MultiPool(void* ptr, size_t size);
void* XRealloc_Hybrid(void* ptr, size_t size);
/**
* @brief 零初始化内存分配函数（调用全局配置的callocZero方法）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0），失败返回NULL
*/
#define XCalloc								XMemory_calloc
void* XCalloc_System(size_t count, size_t size);
void* XCalloc_MultiPool(size_t count, size_t size);
void* XCalloc_Hybrid(size_t count, size_t size);

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
* @brief 读取全局内存统计快照。
* @return 当前统计快照（系统分配器与内存池两路口径齐全）；宏裁剪时各字段
*         恒为 0。
* @note 等价于 XMemory_statistics_2(XMEMORY_TYPE_HYBRID)。systemBytes 与
*       poolUsedBytes 口径不同（堆可用字节 vs 池用户容量），全局多级内存
*       池的后备缓冲计入 systemBytes，读取本接口不会触发内存池的惰性创建。
*/
XMemoryStatistics XMemory_statistics(void);

/**
* @brief 读取指定内存类型的统计快照。
* @param type 内存类型；XMEMORY_TYPE_SYSTEM 只含系统分配器（堆）口径，
*             XMEMORY_TYPE_MULTIPOOL 只含内存池口径，
*             XMEMORY_TYPE_HYBRID 为两路合计（混合模式的系统侧分配与
*             SYSTEM 类型共享同一计数，无法按来源拆分）。
* @return 对应类型的统计快照；未涉及的口径字段为 0，type 越界或宏裁剪时
*         各字段恒为 0。
* @note 池的后备缓冲经系统堆分配，计入 SYSTEM 类型的 systemBytes，不随
*       MULTIPOOL 类型返回；百分比基准同样只取 poolTotalBytes>0 的类型。
*/
XMemoryStatistics XMemory_statistics_2(XMemoryType type);

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
