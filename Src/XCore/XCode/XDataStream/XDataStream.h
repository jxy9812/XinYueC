/**
 * @file       XDataStream.h
 * @brief      二进制数据流公开 API（对标 Qt 6.8 QDataStream）。
 * @details    对齐 qtbase/src/corelib/serialization/qdatastream.{h,cpp} 的
 *             公开 API 名字、行为语义与线格式（字节序、长度前缀、可空
 *             标记、浮点精度）；另对齐 QString/QByteArray 的流操作符线
 *             格式（qtbase/src/corelib/text/qstring.cpp、qbytearray.cpp）。
 *
 *             与 Qt 的形态映射（C 无运算符重载/引用返回）：
 *             - `QDataStream &operator<<(T)` → `XDataStream_writeXxx(...)`，
 *               统一返回 `bool`（true=该值已完整写入，false=失败且
 *               `XDataStream_status` 给出原因）。Qt 的「流自身即返回值」
 *               在 C 里由调用方持有流指针自然达成。
 *             - `QDataStream &operator>>(T&)` → `XDataStream_readXxx(...,
 *               T* out)`，同样返回 bool。
 *             - 构造顺序：Qt 的 `QDataStream(QIODevice*)` 与
 *               `QDataStream(QByteArray*, OpenMode)` 两个构造对应
 *               `XDataStream_init_device` / `XDataStream_init_array`。
 *
 *             实现只依赖 XinYueC 抽象层（XIODevice/XByteArray/XString/
 *             XMemory），禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XDATASTREAM_H
#define XDATASTREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XString.h"

/* 本类不新增虚函数：继承 XClass（对齐 Qt QDataStream 不继承 QObject 的
 * 值类型定位），只重载 Deinit。 */
XCLASS_DEFINE_BEGING(XDataStream)
XCLASS_DEFINE_EXTEND_END(XDataStream, XClass)

/**
 * @brief 流版本（对齐 `QDataStream::Version`，数值一致）。
 * @details 决定线格式细节：`XDataStream_Version_1` 的字符串走 Latin-1 字节
 *          数组；`>= 3` 支持 null 字符串标记。仅列到 Qt 6.8 实际会用到
 *          的分档，未列出的历史别名（如 `Qt_4_1 == Qt_4_0`）不再单独枚举。
 */
typedef enum XDataStreamVersion
{
    XDataStream_Version_1 = 1,   /**< Qt_1_0（对齐 QDataStream::Qt_1_0）。 */
    XDataStream_Version_2 = 2,   /**< Qt_2_0。 */
    XDataStream_Version_3 = 3,   /**< Qt_2_1。 */
    XDataStream_Version_4 = 4,   /**< Qt_3_0。 */
    XDataStream_Version_5 = 5,   /**< Qt_3_1。 */
    XDataStream_Version_6 = 6,   /**< Qt_3_3。 */
    XDataStream_Version_7 = 7,   /**< Qt_4_0 / Qt_4_1。 */
    XDataStream_Version_8 = 8,   /**< Qt_4_2。 */
    XDataStream_Version_9 = 9,   /**< Qt_4_3。 */
    XDataStream_Version_10 = 10, /**< Qt_4_4。 */
    XDataStream_Version_11 = 11, /**< Qt_4_5。 */
    XDataStream_Version_12 = 12, /**< Qt_4_6 / Qt_4_7 / Qt_4_8 / Qt_4_9。 */
    XDataStream_Version_13 = 13, /**< Qt_5_0。 */
    XDataStream_Version_14 = 14, /**< Qt_5_1。 */
    XDataStream_Version_15 = 15, /**< Qt_5_2 / Qt_5_3。 */
    XDataStream_Version_16 = 16, /**< Qt_5_4 / Qt_5_5。 */
    XDataStream_Version_17 = 17, /**< Qt_5_6 / Qt_5_7 / Qt_5_8 / Qt_5_9 /
                                      Qt_5_10 / Qt_5_11。 */
    XDataStream_Version_18 = 18, /**< Qt_5_12。 */
    XDataStream_Version_19 = 19, /**< Qt_5_13 / Qt_5_14 / Qt_5_15。 */
    XDataStream_Version_20 = 20, /**< Qt_6_0 ~ Qt_6_5。 */
    XDataStream_Version_21 = 21, /**< Qt_6_6。 */
    XDataStream_Version_22 = 22, /**< Qt_6_7 / Qt_6_8（缺省版本）。 */
    XDataStream_Version_Default = 22 /**< 对齐 Qt_DefaultCompiledVersion。 */
} XDataStreamVersion;

/**
 * @brief 字节序（对齐 `QDataStream::ByteOrder`）。
 * @note  `XDataStream_ByteOrder_BigEndian` 的数值与 `XDataStream_ByteOrder_LittleEndian`
 *        取自 `QSysInfo` 的约定（大端=0、小端=1），与 Qt 一致。
 */
typedef enum XDataStreamByteOrder
{
    XDataStream_ByteOrder_BigEndian = 0,   /**< 大端（对齐 QDataStream::BigEndian）。 */
    XDataStream_ByteOrder_LittleEndian = 1 /**< 小端（对齐 QDataStream::LittleEndian）。 */
} XDataStreamByteOrder;

/**
 * @brief 流状态（对齐 `QDataStream::Status`，数值一致）。
 */
typedef enum XDataStreamStatus
{
    XDataStream_Status_Ok = 0,                /**< 正常。 */
    XDataStream_Status_ReadPastEnd = 1,       /**< 读到流尾（数据不足）。 */
    XDataStream_Status_ReadCorruptData = 2,   /**< 数据损坏（长度非法等）。 */
    XDataStream_Status_WriteFailed = 3,       /**< 写入失败。 */
    XDataStream_Status_SizeLimitExceeded = 4  /**< 尺寸超出承载范围。 */
} XDataStreamStatus;

/**
 * @brief 浮点精度（对齐 `QDataStream::FloatingPointPrecision`，数值一致）。
 * @details `SinglePrecision` 下 `writeDouble` 只写 32 位（对齐 Qt 在
 *          `Qt_4_6` 之前的行为），`DoublePrecision` 写 64 位（缺省）。
 */
typedef enum XDataStreamFloatingPointPrecision
{
    XDataStream_FloatingPointPrecision_Single = 0, /**< 单精度。 */
    XDataStream_FloatingPointPrecision_Double = 1  /**< 双精度（缺省）。 */
} XDataStreamFloatingPointPrecision;

/**
 * @brief 二进制数据流（对齐 Qt 6.8 `QDataStream`）。
 *
 * @details 两种承载方式（与 Qt 的两个构造对应）：
 *          - **设备模式**：绑定 `XIODevice`（借用，不转移所有权），
 *            读/写经设备进行；
 *          - **字节数组模式**：绑定 `XByteArray`（借用），读/写直接在
 *            数组上追加/消费。
 *
 * @note  本类型**不继承** XObject：Qt 的 QDataStream 同样不继承 QObject
 *        （它是值类型），且流常在栈上短生命周期使用。因此本类型用
 *        `_init` / `XClassDeinit` 成对管理。
 * @note  流不拥有被绑定的设备或数组；调用方须保证其生命周期覆盖流。
 */
typedef struct XDataStream
{
    XClass m_class;               /**< 类信息（首成员）；由 XClass 管理，禁止手工修改。 */
    XIODevice* m_device;          /**< 绑定的设备（借用；NULL=非设备模式）。 */
    XByteArray* m_array;          /**< 绑定的数组（借用；NULL=非数组模式）。 */
    int64_t m_arrayPos;           /**< 数组模式的读游标（写模式下发数组长度）。 */
    int m_version;                /**< 流版本（XDataStreamVersion；缺省 22）。 */
    int m_byteOrder;              /**< 字节序（XDataStreamByteOrder；缺省大端，对齐 Qt）。 */
    int m_fpPrecision;            /**< 浮点精度（XDataStreamFloatingPointPrecision；缺省双精度）。 */
    int m_status;                 /**< 流状态（XDataStreamStatus）。 */
    uint16_t m_transactionDepth;  /**< 事务嵌套深度（对齐 transactionDepth）。 */
    uint32_t m_readOnly : 1;      /**< 数组模式只读（对齐 Qt 传入 QByteArray 常量的构造）。 */
    uint32_t m_streamFlagsUnused : 31; /**< 保留位。 */
} XDataStream;

/** @brief 初始化并返回本类虚函数表。 @return 单例虚函数表指针；调用方不得释放。 */
XVtable* XDataStream_class_init(void);

/**
 * @brief 初始化空流（栈对象；无绑定设备/数组）。
 *
 * @param self 目标流；传入 NULL 时函数不执行任何操作。
 * @return 无。
 * @note  缺省值对齐 Qt：版本 `XDataStream_Version_Default`(22)、
 *        大端、双精度、状态 Ok。未绑定承载时读写均置
 *        `XDataStream_Status_WriteFailed` / `ReadPastEnd`。
 */
void XDataStream_init(XDataStream* self);

/**
 * @brief 初始化并绑定设备（栈对象；对齐 `QDataStream(QIODevice*)`）。
 *
 * @param self   目标流；传入 NULL 时函数不执行任何操作。
 * @param device 设备；**借用**，流不取得所有权，可为 NULL。
 * @return 无。
 */
void XDataStream_init_device(XDataStream* self, XIODevice* device);

/**
 * @brief 初始化并绑定可写字节数组（栈对象；对齐
 *        `QDataStream(QByteArray*, OpenMode)` 的写模式）。
 *
 * @param self  目标流；传入 NULL 时函数不执行任何操作。
 * @param array 目标数组；**借用**，流不取得所有权。不能为 NULL。
 * @return 无。
 * @note  写入一律**追加**到数组末尾（对齐 Qt 写模式语义）。
 */
void XDataStream_init_array(XDataStream* self, XByteArray* array);

/**
 * @brief 初始化并绑定只读字节数组（栈对象；对齐
 *        `QDataStream(const QByteArray&)`）。
 *
 * @param self  目标流；传入 NULL 时函数不执行任何操作。
 * @param array 源数组；**借用**，读期间内容不得修改。不能为 NULL。
 * @return 无。
 * @note  只读模式下写操作置 `XDataStream_Status_WriteFailed`。
 */
void XDataStream_init_arrayReadOnly(XDataStream* self, const XByteArray* array);

/**
 * @brief 堆上创建空流。
 * @param memory 内存类型。
 * @return 新建流，由调用方以 `XClassDelete` 释放；分配失败返回 NULL。
 */
XDataStream* XDataStream_create_ex(XMemoryType memory);
#define XDataStream_create() XDataStream_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 承载与状态（对齐 QDataStream 同名查询） ==================== */

/** @brief 读取绑定设备（对齐 `device`）。 @param self 目标流。 @return 借用指针；非设备模式或 NULL 返回 NULL。 */
XIODevice* XDataStream_device(const XDataStream* self);

/**
 * @brief 重新绑定设备（对齐 `setDevice`）。
 * @param self   目标流；传入 NULL 时函数不执行任何操作。
 * @param device 设备；借用，可为 NULL（解除绑定）。绑定设备会清空数组模式。
 * @return 无。
 * @note  对齐 Qt：换绑不清状态（Qt 亦不清 `q_status`）。
 */
void XDataStream_setDevice(XDataStream* self, XIODevice* device);

/** @brief 是否到达末尾（对齐 `atEnd`）。 @param self 目标流。 @return 无更多可读数据返回 true。 */
bool XDataStream_atEnd(const XDataStream* self);

/** @brief 读取流状态（对齐 `status`）。 @param self 目标流。 @return XDataStreamStatus；NULL 返回 Ok。 */
int XDataStream_status(const XDataStream* self);
/** @brief 设置流状态（对齐 `setStatus`）。 @param self 目标流。 @param status XDataStreamStatus。 @return 无。 */
void XDataStream_setStatus(XDataStream* self, int status);
/** @brief 复位流状态为 Ok（对齐 `resetStatus`）。 @param self 目标流。 @return 无。 */
void XDataStream_resetStatus(XDataStream* self);

/** @brief 读取浮点精度（对齐 `floatingPointPrecision`）。 @param self 目标流。 @return XDataStreamFloatingPointPrecision。 */
int XDataStream_floatingPointPrecision(const XDataStream* self);
/** @brief 设置浮点精度（对齐 `setFloatingPointPrecision`）。 @param self 目标流。 @param precision XDataStreamFloatingPointPrecision。 @return 无。 */
void XDataStream_setFloatingPointPrecision(XDataStream* self, int precision);

/** @brief 读取字节序（对齐 `byteOrder`）。 @param self 目标流。 @return XDataStreamByteOrder。 */
int XDataStream_byteOrder(const XDataStream* self);
/** @brief 设置字节序（对齐 `setByteOrder`）。 @param self 目标流。 @param order XDataStreamByteOrder。 @return 无。 */
void XDataStream_setByteOrder(XDataStream* self, int order);

/** @brief 读取流版本（对齐 `version`）。 @param self 目标流。 @return XDataStreamVersion；NULL 返回缺省 22。 */
int XDataStream_version(const XDataStream* self);
/** @brief 设置流版本（对齐 `setVersion`）。 @param self 目标流。 @param version XDataStreamVersion。 @return 无。 */
void XDataStream_setVersion(XDataStream* self, int version);

/* ==================== 原始字节（对齐 readRawData / writeRawData / skipRawData） ==================== */

/**
 * @brief 写入原始字节（对齐 `writeRawData`）。
 * @param self 目标流；传入 NULL 返回 -1。
 * @param data 源缓冲；不能为 NULL（len>0 时）。
 * @param len  字节数；< 0 置 WriteFailed 并返回 -1。
 * @return 实际写入字节数；失败返回 -1（状态已置）。
 */
int64_t XDataStream_writeRawData(XDataStream* self, const char* data,
                                 int64_t len);

/**
 * @brief 读取原始字节（对齐 `readRawData`）。
 * @param self 目标流；传入 NULL 返回 -1。
 * @param data 目标缓冲；不能为 NULL。
 * @param len  期望字节数。
 * @return 实际读到字节数（可能少于 len）；数据不足置 ReadPastEnd。
 */
int64_t XDataStream_readRawData(XDataStream* self, char* data, int64_t len);

/**
 * @brief 跳过若干字节（对齐 `skipRawData`）。
 * @param self 目标流；传入 NULL 返回 -1。
 * @param len  跳过字节数。
 * @return 实际跳过字节数；不足置 ReadPastEnd。
 */
int64_t XDataStream_skipRawData(XDataStream* self, int64_t len);

/* ==================== 长度前缀字节块（对齐 writeBytes / readBytes） ==================== */

/**
 * @brief 写入「长度前缀 + 内容」（对齐 `writeBytes`）。
 *
 * @param self 目标流；传入 NULL 返回 false。
 * @param data 内容；len 为 0 时可为 NULL。
 * @param len  内容字节数；< 0 置 WriteFailed 并返回 false。
 * @return 成功返回 true；失败返回 false（状态已置）。
 * @note  长度按 `writeQSizeType` 编码：`len <= 0xfffffffd` 时写 quint32；
 *        `0xfffffffe` 为扩展标记后接 quint64；`0xffffffff` 为 null 标记
 *        （对齐 Qt 的 NullCode / ExtendedSize 常量）。
 */
bool XDataStream_writeBytes(XDataStream* self, const char* data, int64_t len);

/**
 * @brief 读取「长度前缀 + 内容」（对齐 `readBytes`）。
 *
 * @param self 目标流；传入 NULL 返回 false。
 * @param out  输出内容缓冲；调用方提供，容量至少 *lenInOut。
 * @param lenInOut 输入=缓冲容量，输出=实际读到的字节数。
 * @return 成功返回 true；长度非法置 ReadCorruptData、数据不足置
 *         ReadPastEnd 并返回 false。
 * @warning 流中的长度若大于缓冲容量，按 ReadCorruptData 失败（不截断，
 *          避免静默数据丢失）。
 */
bool XDataStream_readBytes(XDataStream* self, char* out, int64_t* lenInOut);

/* ==================== 定长整型（对齐 operator<< / >> 的各宽度重载） ==================== */

/** @brief 写入有符号 8 位（对齐 `operator<<(qint8)`）。 @return 成功 true。 */
bool XDataStream_writeInt8(XDataStream* self, int8_t value);
/** @brief 读取有符号 8 位（对齐 `operator>>(qint8&)`）。 @return 成功 true。 */
bool XDataStream_readInt8(XDataStream* self, int8_t* out);
/** @brief 写入无符号 8 位。 @return 成功 true。 */
bool XDataStream_writeUInt8(XDataStream* self, uint8_t value);
/** @brief 读取无符号 8 位。 @return 成功 true。 */
bool XDataStream_readUInt8(XDataStream* self, uint8_t* out);
/** @brief 写入有符号 16 位（按当前字节序）。 @return 成功 true。 */
bool XDataStream_writeInt16(XDataStream* self, int16_t value);
/** @brief 读取有符号 16 位。 @return 成功 true。 */
bool XDataStream_readInt16(XDataStream* self, int16_t* out);
/** @brief 写入无符号 16 位。 @return 成功 true。 */
bool XDataStream_writeUInt16(XDataStream* self, uint16_t value);
/** @brief 读取无符号 16 位。 @return 成功 true。 */
bool XDataStream_readUInt16(XDataStream* self, uint16_t* out);
/** @brief 写入有符号 32 位。 @return 成功 true。 */
bool XDataStream_writeInt32(XDataStream* self, int32_t value);
/** @brief 读取有符号 32 位。 @return 成功 true。 */
bool XDataStream_readInt32(XDataStream* self, int32_t* out);
/** @brief 写入无符号 32 位。 @return 成功 true。 */
bool XDataStream_writeUInt32(XDataStream* self, uint32_t value);
/** @brief 读取无符号 32 位。 @return 成功 true。 */
bool XDataStream_readUInt32(XDataStream* self, uint32_t* out);
/** @brief 写入有符号 64 位。 @return 成功 true。 */
bool XDataStream_writeInt64(XDataStream* self, int64_t value);
/** @brief 读取有符号 64 位。 @return 成功 true。 */
bool XDataStream_readInt64(XDataStream* self, int64_t* out);
/** @brief 写入无符号 64 位。 @return 成功 true。 */
bool XDataStream_writeUInt64(XDataStream* self, uint64_t value);
/** @brief 读取无符号 64 位。 @return 成功 true。 */
bool XDataStream_readUInt64(XDataStream* self, uint64_t* out);

/**
 * @brief 写入布尔（对齐 `operator<<(bool)`）。
 *
 * @param self  目标流。
 * @param value 布尔值。
 * @return 成功 true。
 * @note  对齐 Qt：**始终按 1 字节（qint8 0/1）写出**，与流版本无关。
 */
bool XDataStream_writeBool(XDataStream* self, bool value);
/** @brief 读取布尔（对齐 `operator>>(bool&)`；仅 0 为 false，其余非零为 true）。 @return 成功 true。 */
bool XDataStream_readBool(XDataStream* self, bool* out);

/**
 * @brief 写入单精度浮点（对齐 `operator<<(float)`）。
 * @param self  目标流。
 * @param value 浮点值。
 * @return 成功 true。
 * @note  按 IEEE-754 位模式写出 4 字节；**不受**浮点精度设置影响
 *        （对齐 Qt：精度设置只作用于 double 与 QVariant 路径）。
 */
bool XDataStream_writeFloat(XDataStream* self, float value);
/** @brief 读取单精度浮点。 @return 成功 true。 */
bool XDataStream_readFloat(XDataStream* self, float* out);

/**
 * @brief 写入双精度浮点（对齐 `operator<<(double)`）。
 * @param self  目标流。
 * @param value 浮点值。
 * @return 成功 true。
 * @note  精度为 `Double` 时写 8 字节；为 `Single` 时按 float 精度写
 *        4 字节（对齐 Qt `FloatingPointPrecision` 语义）。
 */
bool XDataStream_writeDouble(XDataStream* self, double value);
/** @brief 读取双精度浮点（按当前精度设置）。 @return 成功 true。 */
bool XDataStream_readDouble(XDataStream* self, double* out);

/* ==================== 字符串与字节数组（对齐流操作符线格式） ==================== */

/**
 * @brief 写入 `XString`（对齐 `operator<<(QDataStream&, const QString&)`）。
 *
 * @param self 目标流；传入 NULL 返回 false。
 * @param text 源字符串；NULL 视为 null 字符串（版本 >= 3 时写 null 标记）。
 * @return 成功 true。
 * @note  线格式：先写**字节长度**为「字节数」（UTF-16 每字符 2 字节），
 *        内容按当前字节序逐 16 位写出；版本 1 走 Latin-1 单字节路径；
 *        版本 >= 3 且字符串为 null 时写 null 标记（对齐 Qt
 *        qstring.cpp:9582-9602）。
 */
bool XDataStream_writeString(XDataStream* self, const XString* text);

/**
 * @brief 读取 `XString`（对齐 `operator>>(QDataStream&, QString&)`）。
 *
 * @param self 目标流；传入 NULL 返回 NULL。
 * @return 新建 XString，由调用方以 `XClassDelete` 释放；读到 null 标记时
 *         返回空串对象（非 NULL）；失败返回 NULL 且状态已置。
 */
XString* XDataStream_readString(XDataStream* self);

/**
 * @brief 写入 `XByteArray`（对齐 `operator<<(QDataStream&, const QByteArray&)`）。
 * @param self  目标流；传入 NULL 返回 false。
 * @param array 源数组；NULL 视为 null 数组（版本 >= 3 时写 null 标记）。
 * @return 成功 true。
 * @note  线格式：长度为字节数，内容原样；版本 1 亦同（对齐 Qt
 *        qbytearray.cpp 的 operator<<）。
 */
bool XDataStream_writeByteArray(XDataStream* self, const XByteArray* array);

/**
 * @brief 读取 `XByteArray`（对齐 `operator>>(QDataStream&, QByteArray&)`）。
 * @param self 目标流；传入 NULL 返回 NULL。
 * @return 新建 XByteArray，由调用方以 `XClassDelete` 释放；读到 null 标记
 *         时返回空数组对象（非 NULL）；失败返回 NULL 且状态已置。
 */
XByteArray* XDataStream_readByteArray(XDataStream* self);

/* ==================== 事务（对齐 Qt 6 的 transaction 系列） ==================== */

/**
 * @brief 开始事务（对齐 `startTransaction`）。
 *
 * @param self 目标流；传入 NULL 时函数不执行任何操作。
 * @return 无。
 * @note  只有**设备**支持事务（需设备具备 seek）；数组模式与
 *        不可 seek 的设备上，事务退化为「仅计数」——提交/回滚为
 *        无操作（对齐 Qt 对顺序设备的处理）。
 * @note  事务开始后状态自动复位为 Ok（对齐 Qt `StreamStateSaver`）。
 */
void XDataStream_startTransaction(XDataStream* self);

/**
 * @brief 提交事务（对齐 `commitTransaction`）。
 * @param self 目标流；传入 NULL 或未在事务中返回 false。
 * @return 事务期间无错误返回 true；有错误或不在事务中返回 false。
 */
bool XDataStream_commitTransaction(XDataStream* self);

/**
 * @brief 回滚事务（对齐 `rollbackTransaction`）。
 * @param self 目标流；传入 NULL 或未在事务中时函数不执行任何操作。
 * @return 无。
 * @note  设备可 seek 时把读写位置还原到事务起点；否则仅退栈。
 */
void XDataStream_rollbackTransaction(XDataStream* self);

/**
 * @brief 中止事务（对齐 `abortTransaction`，Qt 6.8 起为 rollback 别名）。
 * @param self 目标流；传入 NULL 时函数不执行任何操作。
 * @return 无。
 */
void XDataStream_abortTransaction(XDataStream* self);

/** @brief 是否处于事务中（对齐 `isDeviceTransactionStarted`）。 @param self 目标流。 @return 在事务中返回 true。 */
bool XDataStream_isDeviceTransactionStarted(const XDataStream* self);

#ifdef __cplusplus
}
#endif

#endif /* XDATASTREAM_H */
