#ifndef XRINGCHUNK_H
#define XRINGCHUNK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#if XRingChunk_ON

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "XContainer.h"

#define XRINGCHUNK_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XRingChunk))

// XRingChunk虚函数表枚举
XCLASS_DEFINE_BEGING(XRingChunk)
//XCLASS_DEFINE_ENUM(XRingChunk, Write) = XCLASS_VTABLE_GET_SIZE(XContainer),
//XCLASS_DEFINE_ENUM(XRingChunk, Read),
//XCLASS_DEFINE_ENUM(XRingChunk, Peek),
//XCLASS_DEFINE_ENUM(XRingChunk, Skip),
//XCLASS_DEFINE_ENUM(XRingChunk, Reset),
//XCLASS_DEFINE_ENUM(XRingChunk, Available),
//XCLASS_DEFINE_END(XRingChunk)
XCLASS_DEFINE_EXTEND_END(XRingChunk, XContainer)
typedef struct XRingChunk
{
    XContainer m_class;
    size_t m_readPos;   // 读取位置
    size_t m_writePos;  // 写入位置
    size_t m_markPos;   // <<<< 标记位置，用于事务回滚 >>>>
} XRingChunk;

// 初始化XRingChunk的虚函数表
XVtable* XRingChunk_class_init();

/**
 * @brief 创建一个新的XRingChunk实例
 * @details 在堆上分配内存并初始化一个指定逻辑容量的环形缓冲区。
 *         实际分配的物理内存为 capacity + 1 字节，以解决满/空状态歧义。
 * @param capacity 环形缓冲区的逻辑容量（字节数）
 * @return 成功时返回指向新创建XRingChunk的指针；失败时返回NULL
 */
XRingChunk* XRingChunk_create_ex(XMemoryType memory,  size_t capacity);

/**
 * @brief 创建一个XRingChunk的副本
 * @details 在堆上分配内存，并深度拷贝源XRingChunk的所有数据和状态。
 * @param src 源XRingChunk实例指针
 * @return 成功时返回指向新创建副本的指针；失败或src为NULL时返回NULL
 */
XRingChunk* XRingChunk_create_copy(XRingChunk* src);

/**
 * @brief 初始化一个已分配的XRingChunk结构体
 * @details 对传入的XRingChunk指针进行初始化，设置其容量、分配内存并重置状态。
 *         实际分配的物理内存为 capacity + 1 字节。
 * @param chunk 指向待初始化的XRingChunk结构体的指针
 * @param capacity 环形缓冲区的逻辑容量（字节数）
 */
void XRingChunk_init(XRingChunk* chunk, size_t capacity);

/**
 * @brief 向环形缓冲区写入数据
 * @details 将指定大小的数据从源地址写入缓冲区。如果缓冲区空间不足，
 *         则只写入可容纳的部分。写入操作会更新内部写指针和大小。
 * @param chunk XRingChunk实例指针
 * @param data 指向要写入数据的源地址
 * @param size 要写入的数据大小（字节数）
 * @return 实际成功写入的数据大小（字节数）
 */
size_t XRingChunk_write(XRingChunk* chunk, const void* data, size_t size);

/**
 * @brief 从环形缓冲区读取数据
 * @details 从缓冲区中读取指定大小的数据到目标地址。读取操作会更新内部读指针和大小。
 * @param chunk XRingChunk实例指针
 * @param buffer 指向存储读取数据的目标地址
 * @param size 要读取的数据大小（字节数）
 * @return 实际成功读取的数据大小（字节数）
 */
size_t XRingChunk_read(XRingChunk* chunk, void* buffer, size_t size);

/**
 * @brief 查看（窥探）环形缓冲区中的数据
 * @details 从缓冲区中读取指定大小的数据到目标地址，但**不会**移动内部读指针或改变缓冲区大小。
 * @param chunk XRingChunk实例指针
 * @param buffer 指向存储读取数据的目标地址
 * @param size 要读取的数据大小（字节数）
 * @return 实际成功读取的数据大小（字节数）
 */
size_t XRingChunk_peek(XRingChunk* chunk, void* buffer, size_t size);

/**
 * @brief 跳过环形缓冲区中的指定字节数
 * @details 将内部读指针向前移动指定的字节数，逻辑上丢弃这些数据。
 * @param chunk XRingChunk实例指针
 * @param size 要跳过的字节数
 */
void XRingChunk_skip(XRingChunk* chunk, size_t size);

/**
 * @brief 重置环形缓冲区
 * @details 将读写指针和缓冲区大小都重置为0，使缓冲区回到初始空状态。
 *         缓冲区的容量保持不变。
 * @param chunk XRingChunk实例指针
 */
void XRingChunk_reset(XRingChunk* chunk);

/**
 * @brief 获取环形缓冲区中可读取的数据量
 * @details 返回当前缓冲区中已写入但未读取的数据总字节数。
 * @param chunk XRingChunk实例指针（const修饰，不可修改）
 * @return 可读取的数据量（字节数）
 */
size_t XRingChunk_available(const XRingChunk* chunk);

/**
 * @brief 在当前读取位置设置一个标记。
 * @details 该标记可用于后续的 resetToMark 操作，以回滚读取状态。
 * @param chunk XRingChunk实例指针
 */
void XRingChunk_mark(XRingChunk* chunk);

/**
 * @brief 将读取位置重置到最近一次 mark() 调用时的位置。
 * @details 此操作会丢弃自标记以来读取的所有数据，并将总大小恢复。
 * @param chunk XRingChunk实例指针
 */
void XRingChunk_resetToMark(XRingChunk* chunk);

/**
 * @brief 将数据“退回”到缓冲区的读取位置之前。
 * @details 此操作要求读指针前方有足够的连续空间。主要用于实现 ungetChar。
 * @param chunk XRingChunk实例指针
 * @param data 指向要退回数据的源地址
 * @param size 要退回的数据大小（字节数）
 * @return 实际成功退回的数据大小（字节数）
 */
size_t XRingChunk_unget(XRingChunk* chunk, const void* data, size_t size);

/**
 * @brief 仅重置读取指针，不改变写入指针和数据。
 * @details 这个函数用于在更高层级的缓冲区（如XRingBuffer）需要回滚读取操作，
 *          但不想丢失已写入数据的场景。
 * @param chunk XRingChunk实例指针
 */
void XRingChunk_resetReadPosOnly(XRingChunk* chunk);

// 基础操作宏定义（继承自XContainer）

/**
 * @brief 容器对象拷贝的基础实现
 * @details 通过虚函数调用执行容器对象的深拷贝，支持多态。
 * @param object 目标容器对象指针
 * @param src 源容器对象指针
 */
#define XRingChunk_copy_base                XContainer_copy_base

/**
* @brief 容器对象移动的基础实现
* @details 通过虚函数调用执行容器对象的移动语义，支持多态。
* @param object 目标容器对象指针
* @param src 源容器对象指针
*/
#define XRingChunk_move_base                XContainer_move_base

/**
* @brief 容器对象反初始化的基础实现
* @details 通过虚函数调用释放容器对象占用的资源（不包括对象本身），支持多态。
* @param Object XContainer实例指针
*/
#define XRingChunk_deinit_base              XContainer_deinit_base

/**
* @brief 容器对象删除的基础实现
* @details 通过虚函数调用释放容器对象本身及其占用的所有资源，支持多态。
* @param Object XContainer实例指针
*/
#define XRingChunk_delete_base              XContainer_delete_base

/**
* @brief 清空容器内容的基础实现
* @details 通过虚函数调用清空容器内的所有元素，使其大小变为0，支持多态。
* @param Object XContainer实例指针
*/
#define XRingChunk_clear_base               XContainer_clear_base

/**
* @brief 检查容器是否为空的基础实现
* @details 通过虚函数调用检查容器内是否没有任何元素，支持多态。
* @param Object XContainer实例指针（const修饰，不可修改）
* @return 若容器为空则返回true，否则返回false
*/
#define XRingChunk_isEmpty_base             XContainer_isEmpty_base

/**
* @brief 获取容器当前大小的基础实现
* @details 通过虚函数调用获取容器中当前存储的元素数量，支持多态。
* @param Object XContainer实例指针（const修饰，不可修改）
* @return 容器当前大小（元素个数）
*/
#define XRingChunk_size_base                XContainer_size_base

/**
* @brief 获取容器容量的基础实现
* @details 通过虚函数调用获取容器当前容量，支持多态。
* @param Object XContainer实例指针（const修饰，不可修改）
* @return 容器容量值（size_t类型）
*/
#define XRingChunk_capacity_base            XContainer_capacity_base

/**
* @brief 交换两个容器内容的基础实现
* @details 通过虚函数调用交换两个容器的所有内容，支持多态。
* @param Object1 第一个XContainer实例指针
* @param Object2 第二个XContainer实例指针
*/
#define XRingChunk_swap_base                XContainer_swap_base

/**
* @brief 获取容器元素类型大小的基础实现
* @details 通过虚函数调用获取容器中单个元素的大小（以字节为单位），支持多态。
* @param Object XContainer实例指针（const修饰，不可修改）
* @return 元素类型大小（字节数）
*/
#define XRingChunk_typeSize_base            XContainer_typeSize_base

#endif // XRingChunk_ON

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XRingChunk_create
#define XRingChunk_create(...) XRingChunk_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // !XRINGCHUNK_H