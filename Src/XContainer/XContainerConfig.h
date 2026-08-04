#ifndef XCONTAINERCONFIG_H
#define XCONTAINERCONFIG_H

/**
 * @file XContainerConfig.h
 * @brief XContainer 模块的统一编译配置。
 *
 * @details
 * 该文件只保存容器模块的编译开关，不依赖 CXinYueConfig.h，避免全局配置
 * 与容器配置之间形成循环包含。通常由 CXinYueConfig.h 统一包含，应用也可以
 * 在需要只判断容器开关的模块中直接包含本文件。
 *
 * 宏值为 1 表示启用对应模块，为 0 表示关闭对应模块。关闭基础容器后，
 * 依赖该基础容器的模块会在文件末尾自动关闭，以避免生成不完整的接口。
 */

/* ============================== 基础容器 ============================== */

/** @brief 容器公共基类，提供容量、大小、清除和数据操作等基础能力。 */
#define XContainer_ON                 1

/** @brief 集合基类，提供集合类容器的公共接口。 */
#define XSet_ON                       1

/** @brief 哈希集合，使用哈希函数保存不重复元素。 */
#define XHashSet_ON                   1

/** @brief 有序映射容器，保存键和值的关联关系。 */
#define XMap_ON                       1

/** @brief 哈希映射容器，使用哈希函数进行键查找。 */
#define XHashMap_ON                   1

/* ============================== 序列容器 ============================== */

/** @brief 字符串容器，提供动态文本存储和操作。 */
#define XString_ON                   1

/** @brief 字符串列表容器，保存多个 XString 元素。 */
#define XStringList_ON               1

/** @brief 通用变体列表，保存多个 XVariant 元素。 */
#define XVariantList_ON              1

/** @brief 链表基础容器。 */
#define XList_ON                     1

/** @brief 双向链表。 */
#define XListDLinked_ON              1

/** @brief 单向链表。 */
#define XListSLinked_ON               1

/** @brief 链表无锁实现。 */
#define XLockFreeList_ON              1

/** @brief 栈容器，使用 XVector 保存和访问栈顶元素。 */
#define XStack_ON                     1

/** @brief 动态数组容器。 */
#define XVector_ON                    1

/** @brief 基于动态数组的扩展容器。 */
#define XVectorTwo_ON                 1

/** @brief 位数组容器。 */
#define XBitArray_ON                  1

/** @brief 字节数组容器。 */
#define XByteArray_ON                 1

/* ============================== 队列容器 ============================== */

/** @brief 普通先进先出队列。 */
#define XQueue_ON                     1

/** @brief 优先级队列，按照元素优先级取出数据。 */
#define XPriorityQueue_ON             1

/** @brief 循环队列，使用环形存储空间复用队列容量。 */
#define XCircularQueue_ON             1

/** @brief 无锁队列实现。 */
#define XLockFreeQueue_ON             1

/** @brief 环形块容器。 */
#define XRingChunk_ON                 1

/** @brief 环形缓冲区。 */
#define XRingBuffer_ON                1

/* ============================== 相关模块 ============================== */

/** @brief 变体容器，提供基础类型和扩展数据的统一存储。 */
#define XVariant_ON                   1

/** @brief 正则表达式模块，依赖字符串和容器基础能力。 */
#define XRegularExpression_ON         1

/* ============================== 依赖裁剪 ============================== */

/**
 * @brief 没有链表时关闭依赖链表实现的模块。
 *
 * @details
 * XQueue 使用 XList 的基础能力；关闭 XList 后继续保留 XQueue 开关会导致
 * 头文件接口与实际构建内容不一致。
 */
#if !XList_ON
#define XQueue_ON                     0
#endif

/**
 * @brief 没有动态数组时关闭依赖动态数组的模块。
 *
 * @details
 * XStack、XPriorityQueue、XString 等模块依赖 XVector 的存储或遍历接口，
 * 因此必须随 XVector 一起关闭。
 */
#if !XVector_ON
#define XStack_ON                     0
#define XPriorityQueue_ON             0
#define XString_ON                    0
#define XRegularExpression_ON         0
#define XVectorTwo_ON                 0
#define XStringVector_ON              0
#endif

/**
 * @brief 没有容器基类时关闭全部派生容器。
 *
 * @details
 * 该规则用于最小化嵌入式固件的编译规模和代码体积。XMap、XSet、XList、
 * XStack、XVector 等模块都需要 XContainer 提供的公共结构和操作接口。
 */
#if !XContainer_ON
#define XMap_ON                       0
#define XString_ON                   0
#define XRegularExpression_ON         0
#define XPriorityQueue_ON             0
#define XQueue_ON                     0
#define XList_ON                      0
#define XStack_ON                     0
#define XVector_ON                    0
#define XVectorTwo_ON                 0
#define XStringVector_ON              0
#endif

#endif /* XCONTAINERCONFIG_H */
