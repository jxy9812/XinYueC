#ifndef XRINGBUFFER_H
#define XRINGBUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#if XRingBuffer_ON

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "XContainer.h"

// XRingBuffer虚函数表大小（继承自XContainer）
#define XRINGBUFFER_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XRingBuffer))

// XRingBuffer虚函数表枚举（仅用于扩展，不新增自定义虚函数）
XCLASS_DEFINE_BEGING(XRingBuffer)
XCLASS_DEFINE_EXTEND_END(XRingBuffer, XContainer)

typedef struct XRingBuffer
{
    XContainer m_class;           ///< 继承自XContainer的基础成员
    struct XVector* m_chunks;           ///< 存储XRingChunk指针的向量
    //size_t m_totalSize;                 ///< 缓冲区中当前存储的总数据量（字节）
    size_t m_currentReadChunk;          ///< 当前用于读取操作的chunk在向量中的索引
    size_t m_currentWriteChunk;         ///< 当前用于写入操作的chunk在向量中的索引
    size_t m_markedReadChunk;           // <<<< chunk索引 >>>>
    // --- 修正: 添加完整的标记状态 ---
    size_t m_markedReadChunkIndex;      ///< 被标记时的读取chunk索引
    size_t m_markedReadPosInChunk;      ///< 被标记时在该chunk内的读取位置
    size_t m_markedTotalSize; // 新增：记录标记时的总大小
    bool m_hasMark;                     ///< 是否存在有效标记
} XRingBuffer;

/**
 * @brief 初始化XRingBuffer的虚函数表
 * @details 为XRingBuffer类构建并返回其虚函数表指针，用于多态调用。
 *          仅重写了父类的Copy、Move、Clear等虚函数，未添加新的自定义虚函数。
 * @return 指向XRingBuffer虚函数表的指针
 */
XVtable* XRingBuffer_class_init();

/**
 * @brief 创建一个新的XRingBuffer实例
 * @details 在堆上分配内存并初始化一个环形缓冲区。缓冲区由多个固定大小的chunk组成，
 *         初始时包含一个chunk。当一个chunk写满后，会自动分配新的chunk，因此
 *         XRingBuffer是一个**动态扩容**的缓冲区，没有固定的总容量上限。
 * @param chunkSize 每个内部chunk的大小（字节数）
 * @return 成功时返回指向新创建XRingBuffer的指针；失败时返回NULL
 */
XRingBuffer* XRingBuffer_create_ex(XMemoryType memory,  size_t chunkSize);

/**
 * @brief 初始化一个已分配的XRingBuffer结构体
 * @details 对传入的XRingBuffer指针进行初始化，创建其内部管理结构。
 *         XContainerCapacity被设为0，以表明其动态扩容特性。
 * @param buffer 指向待初始化的XRingBuffer结构体的指针
 * @param chunkSize 每个内部chunk的大小（字节数）
 */
void XRingBuffer_init(XRingBuffer* buffer, size_t chunkSize);

/**
 * @brief 向环形缓冲区写入数据
 * @details 将指定大小的数据从源地址写入缓冲区。如果当前chunk空间不足，
 *         会自动创建新的chunk来容纳剩余数据。
 * @param buffer XRingBuffer实例指针
 * @param data 指向要写入数据的源地址
 * @param size 要写入的数据大小（字节数）
 * @return 实际成功写入的数据大小（字节数）
 */
size_t XRingBuffer_write(XRingBuffer* buffer, const void* data, size_t size);

/**
 * @brief 从环形缓冲区读取数据
 * @details 从缓冲区中读取指定大小的数据到目标地址。读取操作会更新内部读指针和总大小。
 * @param buffer XRingBuffer实例指针
 * @param buffer_out 指向存储读取数据的目标地址
 * @param size 要读取的数据大小（字节数）
 * @return 实际成功读取的数据大小（字节数）
 */
size_t XRingBuffer_read(XRingBuffer* buffer, void* buffer_out, size_t size);

/**
 * @brief 查看（窥探）环形缓冲区中的数据
 * @details 从缓冲区中读取指定大小的数据到目标地址，但**不会**移动内部读指针或改变缓冲区状态。
 * @param buffer XRingBuffer实例指针
 * @param buffer_out 指向存储读取数据的目标地址
 * @param size 要读取的数据大小（字节数）
 * @return 实际成功读取的数据大小（字节数）
 */
size_t XRingBuffer_peek(XRingBuffer* buffer, void* buffer_out, size_t size);

/**
 * @brief 跳过环形缓冲区中的指定字节数
 * @details 将内部读指针向前移动指定的字节数，逻辑上丢弃这些数据。
 * @param buffer XRingBuffer实例指针
 * @param size 要跳过的字节数
 */
void XRingBuffer_skip(XRingBuffer* buffer, size_t size);

/**
 * @brief 重置环形缓冲区
 * @details 清空所有chunk中的数据，并将读写指针重置，使缓冲区回到初始空状态。
 * @param buffer XRingBuffer实例指针
 */
void XRingBuffer_reset(XRingBuffer* buffer);

/**
 * @brief 获取环形缓冲区中可读取的数据量
 * @details 返回当前缓冲区中已写入但未读取的数据总字节数。
 * @param buffer XRingBuffer实例指针（const修饰，不可修改）
 * @return 可读取的数据量（字节数）
 */
size_t XRingBuffer_available(const XRingBuffer* buffer);

/**
 * @brief 添加一个新的chunk到缓冲区
 * @details 手动向缓冲区添加一个指定大小的新chunk。通常由内部写入逻辑自动调用。
 * @param buffer XRingBuffer实例指针
 * @param chunkSize 新chunk的大小（字节数）
 * @return 成功添加返回true，否则返回false
 */
bool XRingBuffer_addChunk(XRingBuffer* buffer, size_t chunkSize);

/**
 * @brief 在当前读取位置设置一个全局标记。
 * @details 该操作会在当前活动的读取chunk上调用 mark()，并保存当前chunk索引。
 * @param buffer XRingBuffer实例指针
 */
void XRingBuffer_mark(XRingBuffer* buffer);

/**
 * @brief 将整个缓冲区的读取状态重置到最近一次 mark() 调用时的状态。
 * @details 此操作会将读取chunk索引和内部chunk的读取位置都恢复。
 * @param buffer XRingBuffer实例指针
 */
void XRingBuffer_resetToMark(XRingBuffer* buffer);

/**
 * @brief 获取当前写入chunk的剩余可写空间（估算值）。
 * @details 由于XRingBuffer是动态扩容的，此函数仅返回当前活动写入chunk的剩余空间，
 *          并非总可用空间。主要用于判断是否需要flush。
 * @param buffer XRingBuffer实例指针
 * @return 当前写入chunk的剩余空间（字节数）
 */
size_t XRingBuffer_writeable(const XRingBuffer* buffer);

/**
 * @brief 获取指向当前可读数据的连续内存指针。
 * @details 此函数仅返回当前活动读取chunk中的数据指针。如果数据跨越多个chunk，
 *          调用者需要多次调用此函数和 read() 来处理所有数据。
 * @param buffer XRingBuffer实例指针
 * @param size 请求的大小，函数会返回实际连续的数据量
 * @return 指向连续数据的指针，失败时返回 NULL
 */
const void* XRingBuffer_peekReadPtr(XRingBuffer* buffer, size_t* size);

// 基础操作宏定义（继承自XContainer）

#define XRingBuffer_readable XRingBuffer_available
/**
 * @brief 容器对象拷贝的基础实现
 * @details 通过虚函数调用执行容器对象的深拷贝，支持多态。
 * @param object 目标容器对象指针
 * @param src 源容器对象指针
 */
#define XRingBuffer_copy_base               XContainer_copy_base

/**
 * @brief 容器对象移动的基础实现
 * @details 通过虚函数调用执行容器对象的移动语义，支持多态。
 * @param object 目标容器对象指针
 * @param src 源容器对象指针
 */
#define XRingBuffer_move_base               XContainer_move_base

/**
 * @brief 容器对象反初始化的基础实现
 * @details 通过虚函数调用释放容器对象占用的资源（不包括对象本身），支持多态。
 * @param Object XContainer实例指针
 */
#define XRingBuffer_deinit_base             XContainer_deinit_base

/**
 * @brief 容器对象删除的基础实现
 * @details 通过虚函数调用释放容器对象本身及其占用的所有资源，支持多态。
 * @param Object XContainer实例指针
 */
#define XRingBuffer_delete_base             XContainer_delete_base

/**
 * @brief 清空容器内容的基础实现
 * @details 通过虚函数调用清空容器内的所有元素，使其大小变为0，支持多态。
 * @param Object XContainer实例指针
 */
#define XRingBuffer_clear_base              XContainer_clear_base

/**
 * @brief 检查容器是否为空的基础实现
 * @details 通过虚函数调用检查容器内是否没有任何元素，支持多态。
 * @param Object XContainer实例指针（const修饰，不可修改）
 * @return 若容器为空则返回true，否则返回false
 */
#define XRingBuffer_isEmpty_base            XContainer_isEmpty_base

/**
 * @brief 获取容器当前大小的基础实现
 * @details 获取XRingBuffer中当前存储的数据总量（字节数）。
 * @param Object XContainer实例指针（const修饰，不可修改）
 * @return 容器当前大小（字节数）
 */
#define XRingBuffer_size_base               XContainer_size_base

/**
 * @brief 获取容器容量的基础实现
 * @details **注意**: 对于XRingBuffer，此函数返回0，因为它是动态扩容的，没有固定容量上限。
 * @param Object XContainer实例指针（const修饰，不可修改）
 * @return 0
 */
#define XRingBuffer_capacity_base           XContainer_capacity_base

/**
 * @brief 交换两个容器内容的基础实现
 * @details 通过虚函数调用交换两个容器的所有内容，支持多态。
 * @param Object1 第一个XContainer实例指针
 * @param Object2 第二个XContainer实例指针
 */
#define XRingBuffer_swap_base               XContainer_swap_base

/**
 * @brief 获取容器元素类型大小的基础实现
 * @details 获取XRingBuffer中单个元素的大小（以字节为单位），此处为1（字节流）。
 * @param Object XContainer实例指针（const修饰，不可修改）
 * @return 元素类型大小（字节数）
 */
#define XRingBuffer_typeSize_base           XContainer_typeSize_base

#endif // XRingBuffer_ON

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XRingBuffer_create
#define XRingBuffer_create(...) XRingBuffer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // !XRINGBUFFER_H