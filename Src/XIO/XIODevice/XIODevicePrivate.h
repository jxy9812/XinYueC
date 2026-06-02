#ifndef XIODEVICEPRIVATE_H
#define XIODEVICEPRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XRingBuffer.h"
#include "XString.h"
#include "XVector.h" 

struct XIODevice;

/**
 * @brief XIODevice 的私有数据结构（多通道模型）。
 *
 * 此结构体现在支持多输入/多输出通道。
 * 它通过 XVector 动态管理多个读缓冲区和写缓冲区，每个缓冲区对应一个唯一的通道ID。
 * 对于单通道设备，它会自动使用 ID 为 XIO_DEFAULT_CHANNEL_ID (0) 的通道，行为与旧版完全一致。
 */
typedef struct XIODevicePrivate {
    // --- 读缓冲区池 ---
    struct XVector* readBuffers;        /**< @brief XVector<XRingBuffer*>，存储所有读缓冲区。索引即为通道ID。 */

    // --- 写缓冲区池 ---
    struct XVector* writeBuffers;       /**< @brief XVector<XRingBuffer*>，存储所有写缓冲区。索引即为通道ID。 */

    // --- 事务相关 ---
    bool transactionStarted;            /**< @brief 标志位，指示是否已调用 startTransaction()。 */
    bool aboutToCloseEmitted;           /**< @brief 标志位，防止 aboutToClose 信号重复发射。 */
    int64_t transactionReadPos;         /**< @brief 事务开始时的读位置（针对默认通道）。 */
    // --- 错误 & 状态 ---
    struct XString* errorString;        /**< @brief 存储最近一次 I/O 操作的错误信息。 */
    struct XIODevice* q_ptr;            /**< @brief 反向指针，指向公有接口 XIODevice 实例。 */
} XIODevicePrivate;

/**
 * @brief 创建一个 XIODevicePrivate 实例。
 * @param q 指向所属的 XIODevice 公有对象的指针。
 * @return 成功时返回新实例指针；失败时返回 NULL。
 */
XIODevicePrivate* XIODevicePrivate_create(struct XIODevice* q);

/**
 * @brief 销毁一个 XIODevicePrivate 实例。
 * @param d 指向要销毁的实例的指针。
 */
void XIODevicePrivate_delete(XIODevicePrivate* d);

// === 多通道操作 API ===

/**
 * @brief 获取指定读通道的缓冲区。如果通道不存在，则创建它。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param channelId 通道ID。
 * @return 指向 XRingBuffer 的指针。
 */
struct XRingBuffer* XIODevicePrivate_getOrCreateReadBuffer(XIODevicePrivate* d, int channelId);

/**
 * @brief 获取指定写通道的缓冲区。如果通道不存在，则创建它。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param channelId 通道ID。
 * @return 指向 XRingBuffer 的指针。
 */
struct XRingBuffer* XIODevicePrivate_getOrCreateWriteBuffer(XIODevicePrivate* d, int channelId);

// === 单通道兼容 API (内部使用当前通道ID) ===

/**
 * @brief 将一个字符推回当前读通道缓冲区前端。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param c 要推回的字符。
 */
void XIODevicePrivate_putChar(XIODevicePrivate* d, char c);

/**
 * @brief 从当前读通道缓冲区获取一个字符。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param c 输出参数，用于存储读取到的字符。
 * @return 若成功读取字符则返回 true，否则返回 false。
 */
bool XIODevicePrivate_getChar(XIODevicePrivate* d, char* c);

/**
 * @brief 从当前通道的设备和缓冲区中预读（peek）最多 maxlen 字节的数据。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param data 输出缓冲区。
 * @param maxlen 最大预读字节数。
 * @param device 指向所属 XIODevice 的指针，用于触发底层 read。
 * @return 实际 peek 到的字节数。
 */
int64_t XIODevicePrivate_peek(XIODevicePrivate* d, char* data, int64_t maxlen, struct XIODevice* device);

/**
 * @brief 从当前读通道缓冲区中读取一行（以 '\n' 结尾）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param data 输出缓冲区。
 * @param maxlen 最大读取字节数。
 * @return 实际读取的字节数；若未找到换行符则返回 0。
 */
int64_t XIODevicePrivate_readLineFromBuffer(XIODevicePrivate* d, char* data, int64_t maxlen);

/**
 * @brief 检查当前读通道缓冲区中是否存在完整的行（含 '\n'）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @return 若存在完整行则返回 true，否则返回 false。
 */
bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d);

/**
 * @brief 开始一个针对默认读通道的读事务（标记当前读位置）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 */
void XIODevicePrivate_startTransaction(XIODevicePrivate* d);

/**
 * @brief 提交当前针对默认读通道的读事务（清除标记）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 */
void XIODevicePrivate_commitTransaction(XIODevicePrivate* d);

/**
 * @brief 回滚当前针对默认读通道的读事务（恢复到标记位置）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 */
void XIODevicePrivate_rollbackTransaction(XIODevicePrivate* d);

#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H