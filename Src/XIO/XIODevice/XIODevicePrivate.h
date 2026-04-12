#ifndef XIODEVICEPRIVATE_H
#define XIODEVICEPRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XByteArray.h"
#include "XString.h"

struct XIODevice;

/**
 * @brief XIODevice 的私有数据结构。
 *
 * 此结构体封装了 XIODevice 的所有内部状态和缓冲区，实现了 PIMPL (Pointer to IMPLementation) 模式，
 * 以隐藏实现细节并保持公共 API 的稳定性。
 */
typedef struct XIODevicePrivate {
    // 多读通道缓冲（动态扩展）
    struct XByteArray** readBuffers;        /**< @brief 指向各读通道缓冲区的指针数组。索引 [channel] 对应特定通道。 */
    struct XByteArray** ungetBuffers;       /**< @brief 指向各通道“退回”字符缓冲区的指针数组。用于 ungetChar 功能。 */
    int maxReadChannels;                    /**< @brief 当前为读通道分配的缓冲区数量（至少为1）。 */

    // 写缓冲（全局，因 Qt 通常单写流）
    struct XByteArray* writeBuffer;         /**< @brief 全局写缓冲区。假定设备通常只有一个写流。 */

    // 事务（简化：仅作用于当前读通道）
    struct XByteArray* transactionBuffer;   /**< @brief 用于存储事务期间读取数据的缓冲区。 */
    bool transactionStarted;                /**< @brief 标志位，指示当前是否处于一个事务中。 */

    // 错误 & 状态
    struct XString* errorString;            /**< @brief 存储最近一次 I/O 操作产生的错误信息。 */
    bool aboutToCloseEmitted;               /**< @brief 标志位，指示 aboutToClose 信号是否已被触发。 */

    struct XIODevice* q_ptr;                /**< @brief 指向拥有此私有数据的公有 XIODevice 对象的反向指针。 */
} XIODevicePrivate;

/**
 * @brief 创建一个新的 XIODevicePrivate 实例。
 *
 * 该函数负责分配内存并初始化 XIODevicePrivate 结构体的所有成员。
 * @param q 指向关联的公有 XIODevice 对象的指针，将被赋值给 q_ptr 成员。
 * @return 返回指向新创建的 XIODevicePrivate 实例的指针；若内存分配失败，则返回 NULL。
 */
XIODevicePrivate* XIODevicePrivate_create(struct XIODevice* q);

/**
 * @brief 销毁一个 XIODevicePrivate 实例。
 *
 * 该函数负责释放 XIODevicePrivate 实例及其所有成员（如缓冲区、错误字符串等）占用的内存。
 * 调用此函数后，传入的指针 d 将变为无效。
 * @param d 指向要销毁的 XIODevicePrivate 实例的指针。
 * @retval 无
 */
void XIODevicePrivate_delete(XIODevicePrivate* d);

// 带通道参数的操作

/**
 * @brief 向指定读通道的缓冲区末尾添加一个字符。
 *
 * 该函数主要用于内部实现，例如在从设备读取数据后将其放入缓冲区。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param c 要添加的字符。
 * @param channel 目标读通道的索引。
 * @retval 无
 */
void XIODevicePrivate_putChar(XIODevicePrivate* d, char c, int channel);

/**
 * @brief 从指定读通道的缓冲区（或 unget 缓冲区）中取出一个字符。
 *
 * 该函数会优先从 unget 缓冲区读取，若其为空，则从主读缓冲区读取。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param c 指向用于接收读取字符的变量的指针。
 * @param channel 源读通道的索引。
 * @retval bool 操作成功（即缓冲区非空）时返回 true；否则返回 false。
 */
bool XIODevicePrivate_getChar(XIODevicePrivate* d, char* c, int channel);

/**
 * @brief 从指定读通道的缓冲区中窥探（不移除）最多 maxlen 个字节的数据。
 *
 * 此函数不会改变缓冲区的状态。如果缓冲区中的数据不足 maxlen 字节，
 * 它会尝试通过调用 device 的 readData 虚函数来填充缓冲区。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param data 指向用于存放窥探数据的缓冲区的指针。
 * @param maxlen 请求窥探的最大字节数。
 * @param device 指向关联的 XIODevice 对象的指针，用于在需要时进行底层读取。
 * @param channel 源读通道的索引。
 * @return 返回实际窥探到的字节数。
 */
int64_t XIODevicePrivate_peek(XIODevicePrivate* d, char* data, int64_t maxlen, struct XIODevice* device, int channel);

/**
 * @brief 从指定读通道的缓冲区中读取一行数据（以换行符 '\n' 结尾）。
 *
 * 该函数会读取数据直到遇到换行符或达到 maxlen 限制，并将换行符包含在输出中。
 * 如果缓冲区中没有完整的行，它会返回 0。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param data 指向用于存放读取行的缓冲区的指针。
 * @param maxlen 读取缓冲区的最大容量（包括结尾的空字符）。
 * @param channel 源读通道的索引。
 * @return 返回实际读取的字节数（包括 '\n'）；若缓冲区中无完整行，则返回 0。
 */
int64_t XIODevicePrivate_readLineFromBuffer(XIODevicePrivate* d, char* data, int64_t maxlen, int channel);

/**
 * @brief 检查指定读通道的缓冲区中是否存在完整的行（以换行符 '\n' 结尾）。
 *
 * 此函数用于在执行 readLine 操作前快速判断是否有数据可读。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param channel 要检查的读通道索引。
 * @retval bool 如果缓冲区中存在至少一个完整的行，则返回 true；否则返回 false。
 */
bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d, int channel);

/**
 * @brief 在当前读通道上启动一个事务。
 *
 * 事务开始后，所有后续的读取操作都会被记录，以便在需要时可以回滚到事务开始前的状态。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @retval 无
 */
void XIODevicePrivate_startTransaction(XIODevicePrivate* d);

/**
 * @brief 提交当前正在进行的事务。
 *
 * 提交操作会清空事务缓冲区，使所有在事务期间的读取操作成为永久性的。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @retval 无
 */
void XIODevicePrivate_commitTransaction(XIODevicePrivate* d);

/**
 * @brief 回滚当前正在进行的事务。
 *
 * 回滚操作会将事务期间读取的所有数据重新放回读缓冲区的前端，使设备状态恢复到事务开始之前。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @retval 无
 */
void XIODevicePrivate_rollbackTransaction(XIODevicePrivate* d);

#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H