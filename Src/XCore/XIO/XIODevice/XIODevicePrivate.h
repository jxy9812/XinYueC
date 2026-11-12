#ifndef XIODEVICEPRIVATE_H
#define XIODEVICEPRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XTypes.h"
struct XIODevice;

/**
 * @brief ungetChar 回退栈容量。
 * @details Qt 把放回的字符直接插回读缓冲（QRingBuffer::reserveFront），因此
 *          没有显式上限；本框架的环形缓冲没有「前端插入」原语，故用定容栈
 *          承载。64 字节覆盖实际解析器（回溯 1~2 个字符）的全部需要，超出时
 *          再退化为往环形缓冲前端回退（见 XIODevicePrivate_ungetChar）。
 */
#define XIODevice_UngetCapacity 64

/**
 * @brief XIODevice 的私有数据结构（多通道模型）。
 *
 * 此结构体现在支持多输入/多输出通道。
 * 它通过 XVector 动态管理多个读缓冲区和写缓冲区，每个缓冲区对应一个唯一的通道ID。
 * 对于单通道设备，它会自动使用 ID 为 XIO_DEFAULT_CHANNEL_ID (0) 的通道，行为与旧版完全一致。
 *
 * @note 位置与通道计数字段逐项对齐 Qt 6.8 `QIODevicePrivate`
 *       （src/corelib/io/qiodevice_p.h）：pos / devicePos / transactionPos /
 *       readChannelCount / writeChannelCount。
 */
typedef struct XIODevicePrivate {
    // --- 读缓冲区池 ---
    struct XVector* readBuffers;        /**< @brief XVector<XRingBuffer*>，存储所有读缓冲区。索引即为通道ID。 */

    // --- 写缓冲区池 ---
    struct XVector* writeBuffers;       /**< @brief XVector<XRingBuffer*>，存储所有写缓冲区。索引即为通道ID。 */

    // --- 位置与事务（对齐 Qt d->pos / d->devicePos / d->transactionPos） ---
    int64_t pos;                        /**< @brief 逻辑读写位置；对齐 Qt `QIODevicePrivate::pos`。 */
    int64_t devicePos;                  /**< @brief 底层设备指针的已知位置；对齐 Qt `devicePos`。预读会让它领先 pos。 */
    int64_t transactionReadPos;         /**< @brief 事务原点：随机访问设备为设备位置，顺序设备为读缓冲内的偏移；对齐 Qt `transactionPos`。 */

    // --- 通道计数（对齐 Qt readChannelCount / writeChannelCount） ---
    int32_t readChannelCount;           /**< @brief 读通道数；关闭态 0，打开且可读 1。 */
    int32_t writeChannelCount;          /**< @brief 写通道数；关闭态 0，打开且可写 1。 */

    // --- 错误 & 状态 ---
    struct XString* errorString;        /**< @brief 存储最近一次 I/O 操作的错误信息（open 时清空，对齐 Qt）。 */
    struct XIODevice* q_ptr;            /**< @brief 反向指针，指向公有接口 XIODevice 实例。 */

    /* unget 字符栈：逻辑上位于输入流最前端（LIFO，后放回的先读出） */
    char ungetStack[XIODevice_UngetCapacity];
    int ungetCount;

    // --- 标志位（置于尾部，避免在 64 位指针前产生对齐空洞） ---
    bool transactionStarted;            /**< @brief 标志位，指示是否已调用 startTransaction()。 */
    bool aboutToCloseEmitted;           /**< @brief 标志位，防止 aboutToClose 信号重复发射。 */
    bool baseReadLineDataCalled;        /**< @brief 基类 readLineData 是否已自行推进过 pos（对齐 Qt 同名字段）。 */

} XIODevicePrivate;

/**
 * @brief 创建并初始化一个 XIODevicePrivate 实例（堆内存）。
 * @param q 指向所属的 XIODevice 公有对象的指针。
 * @return 成功时返回新实例指针；失败时返回 NULL。
 */
XIODevicePrivate* XIODevicePrivate_create(struct XIODevice* q);

/**
 * @brief 销毁一个 XIODevicePrivate 实例并释放内存。
 * @param d 指向要销毁的实例的指针。
 */
void XIODevicePrivate_delete(XIODevicePrivate* d);

/**
 * @brief 初始化一个已分配的 XIODevicePrivate 实例（栈或子类继承使用）。
 * @param d 指向已分配内存的 XIODevicePrivate 指针。
 * @param q 指向所属的 XIODevice 公有对象的指针。
 */
void XIODevicePrivate_init(XIODevicePrivate* d, struct XIODevice* q);

/**
 * @brief 释放 XIODevicePrivate 的资源但不释放内存（栈或子类继承使用）。
 * @param d 指向已分配内存的 XIODevicePrivate 指针。
 */
void XIODevicePrivate_deinit(XIODevicePrivate* d);

// === 多通道操作 API ===

/**
 * @brief 获取指定读通道的缓冲区。如果通道不存在，则创建它。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param channelId 通道ID。
 * @return 指向 XRingBuffer 的指针。
 */
struct XRingBuffer* XIODevicePrivate_getOrCreateReadBuffer(XIODevicePrivate* d, int channelId);

/**
 * @brief 只查不建地获取指定读通道的缓冲区（**const 查询路径专用**）。
 * @details 对齐 Qt `QIODevicePrivate::buffer` 的只读语义：查询类 API
 *          （`bytesAvailable()` / `atEnd()` / `canReadLine()` / `pos()` 校准）
 *          **不得**因为一次读取就创建 4KB 预读 chunk。修复前
 *          `XIODevicePrivate_bufferedBytes` 走的是 `getOrCreate` 路径，空设备
 *          每查一次就分配一次（嵌入式 2.6KB 堆下直接 malloc 失败）。
 * @param d 私有数据；NULL 或通道越界返回 NULL。
 * @param channelId 通道ID。
 * @return 借用指针（可能是 NULL）；**不得**释放。
 */
struct XRingBuffer* XIODevicePrivate_readBufferConst(const XIODevicePrivate* d, int channelId);

/**
 * @brief 只查不建地获取指定写通道的缓冲区（同上，`bytesToWrite()` 用）。
 * @param d 私有数据；NULL 或通道越界返回 NULL。
 * @param channelId 通道ID。
 * @return 借用指针（可能是 NULL）；**不得**释放。
 */
struct XRingBuffer* XIODevicePrivate_writeBufferConst(const XIODevicePrivate* d, int channelId);

/**
 * @brief 获取指定写通道的缓冲区。如果通道不存在，则创建它。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param channelId 通道ID。
 * @return 指向 XRingBuffer 的指针。
 */
struct XRingBuffer* XIODevicePrivate_getOrCreateWriteBuffer(XIODevicePrivate* d, int channelId);

// === 通道计数（对齐 Qt setReadChannelCount / setWriteChannelCount） ===

/**
 * @brief 设置读通道数；缩容时销毁多余通道的读缓冲区（对齐 Qt readBuffers.resize）。
 * @param d 私有数据。
 * @param count 新通道数；<=0 表示全部销毁。
 */
void XIODevicePrivate_setReadChannelCount(XIODevicePrivate* d, int count);

/**
 * @brief 设置写通道数；缩容时销毁多余通道的写缓冲区。
 * @param d 私有数据。
 * @param count 新通道数；<=0 表示全部销毁。
 */
void XIODevicePrivate_setWriteChannelCount(XIODevicePrivate* d, int count);

/**
 * @brief 清空全部读缓冲通道的数据（保留通道对象）。
 * @param d 私有数据。
 */
void XIODevicePrivate_clearReadBuffers(XIODevicePrivate* d);

// === 读缓冲统一视图 ===
// 逻辑输入流 = unget 栈（LIFO，先出）+ 当前通道环形缓冲。
// ungetChar() 放回的字符因此对 bytesAvailable/read/peek/readLine 全部可见，
// 与 Qt 文档契约一致（Qt 把字符插回缓冲前端）。

/**
 * @brief 当前通道逻辑可读字节数（unget 栈 + 环形缓冲）。
 * @param d 私有数据；NULL 返回 0。
 * @param channel 通道号。
 * @return 可读字节数。
 */
int64_t XIODevicePrivate_bufferedBytes(const XIODevicePrivate* d, int channel);

/**
 * @brief 从逻辑输入流的指定偏移处预读，不消费。
 * @param d 私有数据。
 * @param channel 通道号。
 * @param data 输出缓冲（调用方保证容量 >= maxlen）。
 * @param maxlen 最多读取字节数。
 * @param offset 相对逻辑流首的偏移（事务读用）。
 * @return 实际读取字节数。
 */
int64_t XIODevicePrivate_bufferPeek(const XIODevicePrivate* d, int channel, char* data,
                                   int64_t maxlen, int64_t offset);

/**
 * @brief 从逻辑输入流消费最多 maxlen 字节。
 * @param d 私有数据。
 * @param channel 通道号。
 * @param data 输出缓冲。
 * @param maxlen 最多读取字节数。
 * @return 实际消费字节数。
 */
int64_t XIODevicePrivate_bufferRead(XIODevicePrivate* d, int channel, char* data, int64_t maxlen);

/**
 * @brief 在逻辑输入流的 [offset, offset+maxlen) 范围内查找字符 c。
 * @param d 私有数据。
 * @param channel 通道号。
 * @param c 待查找字符。
 * @param maxlen 最多扫描字节数。
 * @param offset 起始偏移。
 * @return 命中位置（相对逻辑流首）；未命中返回 -1。
 */
int64_t XIODevicePrivate_bufferIndexOf(const XIODevicePrivate* d, int channel, char c,
                                       int64_t maxlen, int64_t offset);

/**
 * @brief 丢弃逻辑输入流开头最多 maxlen 字节（skip 用）。
 * @param d 私有数据。
 * @param channel 通道号。
 * @param maxlen 最多丢弃字节数。
 * @return 实际丢弃字节数。
 */
int64_t XIODevicePrivate_bufferSkip(XIODevicePrivate* d, int channel, int64_t maxlen);

/**
 * @brief 逻辑输入流是否为空（考虑顺序设备的事务偏移）。
 * @param d 私有数据。
 * @param channel 通道号。
 * @return 无剩余可读字节返回 true。
 */
bool XIODevicePrivate_isBufferEmpty(const XIODevicePrivate* d, int channel);

/**
 * @brief 把设备位置移动到 newPos 对应的缓冲位置（对齐 Qt seekBuffer）。
 * @details 前移时若目标仍在预读范围内只丢弃前缀；回退或越界则整体清空。
 * @param d 私有数据。
 * @param channel 通道号。
 * @param newPos 目标逻辑位置（随机访问设备为设备位置）。
 */
void XIODevicePrivate_seekBuffer(XIODevicePrivate* d, int channel, int64_t newPos);

// === 单通道兼容 API (内部使用当前通道ID) ===

/**
 * @brief 将一个字符放回输入流前端（对齐 Qt QIODevice::ungetChar 的缓冲行为）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param c 要推回的字符。
 */
void XIODevicePrivate_ungetChar(XIODevicePrivate* d, char c);

/**
 * @brief 从当前读通道缓冲区中读取一行（以 '\n' 结尾）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @param data 输出缓冲区。
 * @param maxlen 最大读取字节数（不写结尾 '\0'）。
 * @return 实际读取的字节数（含换行符）；缓冲内没有完整行时返回 0。
 */
int64_t XIODevicePrivate_readLineFromBuffer(XIODevicePrivate* d, char* data, int64_t maxlen);

/**
 * @brief 检查当前读通道缓冲区中是否存在完整的行（含 '\n'）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 * @return 若存在完整行则返回 true，否则返回 false。
 */
bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d);

/**
 * @brief 判断子类是否重载了 Pos 虚函数槽（基类读路径的位置校准依据）。
 * @param self 设备对象。
 * @return 重载返回 true；用基类实现返回 false。
 */
bool XIODevice_isPosOverridden(const struct XIODevice* self);

/**
 * @brief 判断子类是否重载了 Seek 虚函数槽。
 * @param self 设备对象。
 * @return 重载返回 true；用基类实现返回 false。
 */
bool XIODevice_isSeekOverridden(const struct XIODevice* self);

/**
 * @brief 开始一个读事务（记录事务原点）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 */
void XIODevicePrivate_startTransaction(XIODevicePrivate* d);

/**
 * @brief 提交当前读事务（丢弃事务区间内已消费的数据）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 */
void XIODevicePrivate_commitTransaction(XIODevicePrivate* d);

/**
 * @brief 回滚当前读事务（恢复到事务原点）。
 * @param d 指向 XIODevicePrivate 实例的指针。
 */
void XIODevicePrivate_rollbackTransaction(XIODevicePrivate* d);

#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H
