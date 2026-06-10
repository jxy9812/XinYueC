#ifndef XIODevice_H
#define XIODevice_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XObject.h"
//缓冲区大小
#define XBuffSize						256
#define XIODevice_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XIODevice))       //XIODeviceBase虚函数表大小
//XContainer虚函数表枚举
XCLASS_DEFINE_BEGING(XIODevice)
XCLASS_DEFINE_ENUM(XIODevice, Open) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XIODevice, Close),
XCLASS_DEFINE_ENUM(XIODevice, IsSequential),
XCLASS_DEFINE_ENUM(XIODevice, Pos),
XCLASS_DEFINE_ENUM(XIODevice, Size),
XCLASS_DEFINE_ENUM(XIODevice, Seek),
XCLASS_DEFINE_ENUM(XIODevice, AtEnd),
XCLASS_DEFINE_ENUM(XIODevice, Reset),
XCLASS_DEFINE_ENUM(XIODevice, BytesAvailable),
XCLASS_DEFINE_ENUM(XIODevice, BytesToWrite),
XCLASS_DEFINE_ENUM(XIODevice, CanReadLine),
XCLASS_DEFINE_ENUM(XIODevice, WaitForReadyRead),
XCLASS_DEFINE_ENUM(XIODevice, WaitForBytesWritten),
XCLASS_DEFINE_ENUM(XIODevice, ReadData),        // ← 纯虚
XCLASS_DEFINE_ENUM(XIODevice, ReadLineData),
XCLASS_DEFINE_ENUM(XIODevice, SkipData),
XCLASS_DEFINE_ENUM(XIODevice, WriteData),       // ← 纯虚
XCLASS_DEFINE_END(XIODevice)
typedef struct XCircularQueue XCircularQueue;
typedef struct XIODevicePrivate  XIODevicePrivate;
typedef enum /*XIODevice*/
{
	XIODevice_NotOpen      = 0x0000, ///< 设备未打开
    XIODevice_ReadOnly     = 0x0001, ///< 只读
    XIODevice_WriteOnly    = 0x0002, ///< 只写
    XIODevice_ReadWrite    = XIODevice_ReadOnly | XIODevice_WriteOnly, ///< 读写
    XIODevice_Append       = 0x0004, ///< 追加模式
    XIODevice_Truncate     = 0x0008, ///< 打开时截断
    XIODevice_Text         = 0x0010, ///< 文本模式（自动处理换行符）
    XIODevice_Unbuffered   = 0x0020, ///< 绕过缓冲（保留 Qt 内部标志）
    XIODevice_NewOnly      = 0x0040, ///< 仅当不存在时创建
    XIODevice_ExistingOnly = 0x0080  ///< 仅当存在时打开
}XIODeviceBaseMode;
//IO设备
typedef struct XIODevice
{
	XObject m_class;//继承类
	//void* device;//设备
	uint16_t m_openMode;//打开模式
    bool m_textModeEnabled;
    int m_currentReadChannel;       // ← 多通道支持
    int m_currentWriteChannel;
    XIODevicePrivate* m_d;
}XIODevice;
/**
 * @brief 获取 XIODevice 类的虚函数表
 * @return 指向 XIODevice 虚函数表的指针
 */
XVtable* XIODevice_class_init();

/**
 * @brief 在堆上创建一个新的 XIODevice 对象实例
 * @return 指向新创建的 XIODevice 对象的指针，若创建失败则返回 NULL
 * @note 该对象需要通过 XIODevice_deleteLater() 或 XObject_deleteLater() 来释放
 */
XIODevice* XIODevice_create();

/**
 * @brief 初始化一个已分配的 XIODevice 对象
 * @param io 指向待初始化的 XIODevice 对象的指针
 * @note 此函数通常由 XIODevice_create() 内部调用
 */
void XIODevice_init(XIODevice* io);
#define XIODevice_deleteLater		XObject_deleteLater
#define XIODevice_deinitLater		XObject_deinitLater
// —————— Public API ——————

/**
 * @brief 获取设备的打开模式
 * @param self 指向 XIODevice 对象的指针
 * @return 设备的当前打开模式（XIODeviceBaseMode 枚举值）
 */
XIODeviceBaseMode XIODevice_openMode(const XIODevice* self);

/**
 * @brief 设置是否启用文本模式
 * @param self 指向 XIODevice 对象的指针
 * @param enabled 若为 true 则启用文本模式，否则禁用
 * @note 在文本模式下，某些操作（如换行符）可能会被自动转换
 */
void XIODevice_setTextModeEnabled(XIODevice* self, bool enabled);

/**
 * @brief 查询设备是否处于文本模式
 * @param self 指向 XIODevice 对象的指针
 * @return 若设备处于文本模式则返回 true，否则返回 false
 */
bool XIODevice_isTextModeEnabled(const XIODevice* self);

/**
 * @brief 查询设备是否已打开
 * @param self 指向 XIODevice 对象的指针
 * @return 若设备已成功打开则返回 true，否则返回 false
 */
bool XIODevice_isOpen(const XIODevice* self);

/**
 * @brief 查询设备是否可读
 * @param self 指向 XIODevice 对象的指针
 * @return 若设备以只读或读写模式打开则返回 true，否则返回 false
 */
bool XIODevice_isReadable(const XIODevice* self);

/**
 * @brief 查询设备是否可写
 * @param self 指向 XIODevice 对象的指针
 * @return 若设备以只写或读写模式打开则返回 true，否则返回 false
 */
bool XIODevice_isWritable(const XIODevice* self);

/**
 * @brief 查询设备是否为顺序访问设备
 * @param self 指向 XIODevice 对象的指针
 * @return 若设备是顺序访问（如串口、管道）则返回 true，否则（如文件）返回 false
 */
bool XIODevice_isSequential(const XIODevice* self);

/**
 * @brief 获取设备支持的读通道数量
 * @param self 指向 XIODevice 对象的指针
 * @return 读通道的数量
 */
int XIODevice_readChannelCount(const XIODevice* self);

/**
 * @brief 获取设备支持的写通道数量
 * @param self 指向 XIODevice 对象的指针
 * @return 写通道的数量（当前实现通常为 1）
 */
int XIODevice_writeChannelCount(const XIODevice* self);

/**
 * @brief 获取当前活动的读通道索引
 * @param self 指向 XIODevice 对象的指针
 * @return 当前读通道的索引
 */
int XIODevice_currentReadChannel(const XIODevice* self);

/**
 * @brief 设置当前活动的读通道
 * @param self 指向 XIODevice 对象的指针
 * @param channel 要设置为当前读通道的索引
 */
void XIODevice_setCurrentReadChannel(XIODevice* self, int channel);

/**
 * @brief 获取当前活动的写通道索引
 * @param self 指向 XIODevice 对象的指针
 * @return 当前写通道的索引
 */
int XIODevice_currentWriteChannel(const XIODevice* self);

/**
 * @brief 设置当前活动的写通道
 * @param self 指向 XIODevice 对象的指针
 * @param channel 要设置为当前写通道的索引
 */
void XIODevice_setCurrentWriteChannel(XIODevice* self, int channel);

/**
 * @brief 从设备中读取最多 maxlen 个字节的数据到缓冲区 data 中
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放读取数据的缓冲区
 * @param maxlen 要读取的最大字节数
 * @return 实际读取的字节数，若发生错误则返回 -1
 * @note 此函数会调用底层的 readData 虚函数
 */
int64_t XIODevice_read_1(XIODevice* self, char* data, int64_t maxlen);
int64_t XIODevice_read_2(XIODevice* self, XByteArray* buff, int64_t maxlen, bool isAppend);
/**
 * @brief 从设备中读取最多 maxlen 个字节的数据，并返回一个新的 XByteArray 对象
 * @param self 指向 XIODevice 对象的指针
 * @param maxlen 要读取的最大字节数
 * @return 包含读取数据的 XByteArray 对象指针
 * @note 返回的对象需要手动释放
 */
XByteArray* XIODevice_read_3(XIODevice* self, int64_t maxlen);

/**
 * @brief 从设备中读取所有可用数据
 * @param self 指向 XIODevice 对象的指针
 * @return 包含所有读取数据的 XByteArray 对象指针
 * @note 返回的对象需要手动释放。此函数会循环调用 read 直到无数据可读
 */
int64_t XIODevice_readAll_1(XIODevice* self, char* buff, int64_t buffSize);
int64_t XIODevice_readAll_2(XIODevice* self, XByteArray* buff,bool isAppend);
XByteArray* XIODevice_readAll_3(XIODevice* self);

/**
 * @brief 从设备中读取一行数据（直到遇到换行符 '\\n' 或达到 maxlen 限制）
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放读取数据的缓冲区
 * @param maxlen 缓冲区的最大长度
 * @return 实际读取的字节数（包含换行符），若发生错误则返回 -1
 */
int64_t XIODevice_readLine_1(XIODevice* self, char* data, int64_t maxlen);
int64_t XIODevice_readLine_2(XIODevice* self, XByteArray* buff);
/**
 * @brief 从设备中读取一行数据，并返回一个新的 XByteArray 对象
 * @param self 指向 XIODevice 对象的指针
 * @param maxlen 要读取的最大字节数（若 <=0 则使用默认值 1024）
 * @return 包含读取行数据的 XByteArray 对象指针
 * @note 返回的对象需要手动释放
 */
XByteArray* XIODevice_readLine_3(XIODevice* self);
/**
 * @brief 开始一个 I/O 事务
 * @param self 指向 XIODevice 对象的指针
 * @note 事务期间的操作可以被回滚
 */
void XIODevice_startTransaction(XIODevice* self);

/**
 * @brief 提交当前的 I/O 事务
 * @param self 指向 XIODevice 对象的指针
 * @note 提交后，事务期间的操作将变为永久性
 */
void XIODevice_commitTransaction(XIODevice* self);

/**
 * @brief 回滚当前的 I/O 事务
 * @param self 指向 XIODevice 对象的指针
 * @note 回滚后，事务期间的操作将被撤销
 */
void XIODevice_rollbackTransaction(XIODevice* self);

/**
 * @brief 查询当前是否有一个 I/O 事务正在进行
 * @param self 指向 XIODevice 对象的指针
 * @return 若有事务正在进行则返回 true，否则返回 false
 */
bool XIODevice_isTransactionStarted(const XIODevice* self);

/**
 * @brief 向设备写入 len 个字节的数据
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向要写入的数据缓冲区
 * @param len 要写入的字节数
 * @return 实际写入的字节数，若发生错误则返回 -1
 * @note 成功写入后会触发 bytesWritten 信号
 */
int64_t XIODevice_write_1(XIODevice* self, const char* data, int64_t len);

/**
 * @brief 向设备写入一个 XByteArray 对象的内容
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向要写入的 XByteArray 对象
 * @return 实际写入的字节数
 */
int64_t XIODevice_write_2(XIODevice* self, const XByteArray* data);

/**
 * @brief 向设备写入一个以空字符结尾的 C 字符串
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向要写入的 C 字符串
 * @return 实际写入的字节数
 */
int64_t XIODevice_write_3(XIODevice* self, const char* data);

/**
 * @brief 刷新写缓冲区，将所有待发送数据写入底层设备。
 * @param self XIODevice实例指针
 * @return 成功返回true，失败返回false。
 */
bool XIODevice_flush(XIODevice* self);
/**
 * @brief 从设备中窥探（不移除）最多 maxlen 个字节的数据到缓冲区 data 中
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放窥探数据的缓冲区
 * @param maxlen 要窥探的最大字节数
 * @return 实际窥探到的字节数
 */
int64_t XIODevice_peek_1(XIODevice* self, char* data, int64_t maxlen);
int64_t XIODevice_peek_2(XIODevice* self, XByteArray* buff, int64_t maxlen);
/**
 * @brief 从设备中窥探（不移除）最多 maxlen 个字节的数据，并返回一个新的 XByteArray 对象
 * @param self 指向 XIODevice 对象的指针
 * @param maxlen 要窥探的最大字节数
 * @return 包含窥探数据的 XByteArray 对象指针
 * @note 返回的对象需要手动释放
 */
XByteArray* XIODevice_peek_3(XIODevice* self, int64_t maxlen);

/**
 * @brief 从输入流中跳过最多 maxSize 个字节
 * @param self 指向 XIODevice 对象的指针
 * @param maxSize 要跳过的最大字节数
 * @return 实际跳过的字节数
 */
int64_t XIODevice_skip(XIODevice* self, int64_t maxSize);

/**
 * @brief 将一个字符放回输入流的前端，使其成为下一个被读取的字符
 * @param self 指向 XIODevice 对象的指针
 * @param c 要放回的字符
 */
void XIODevice_ungetChar(XIODevice* self, char c);

/**
 * @brief 向设备写入单个字符
 * @param self 指向 XIODevice 对象的指针
 * @param c 要写入的字符
 * @return 若写入成功则返回 true，否则返回 false
 */
bool XIODevice_putChar(XIODevice* self, char c);

/**
 * @brief 从设备读取单个字符
 * @param self 指向 XIODevice 对象的指针
 * @param c 指向用于存放读取字符的变量
 * @return 若读取成功则返回 true，否则返回 false
 */
bool XIODevice_getChar(XIODevice* self, char* c);

/**
 * @brief 获取设备最近一次操作的错误描述字符串
 * @param self 指向 XIODevice 对象的指针
 * @return 包含错误信息的 XString 对象指针
 * @note 返回的对象是副本，调用者需要负责释放
 */
XString* XIODevice_errorString(const XIODevice* self);


// —————— 虚函数（_base） ——————

/**
 * @brief 虚函数：打开设备
 * @param self 指向 XIODevice 对象的指针
 * @param mode 打开模式（XIODeviceBaseMode 枚举值）
 * @return 若打开成功则返回 true，否则返回 false
 */
bool XIODevice_open_base(XIODevice* self, XIODeviceBaseMode mode);

/**
 * @brief 虚函数：关闭设备
 * @param self 指向 XIODevice 对象的指针
 */
void XIODevice_close_base(XIODevice* self);

/**
 * @brief 虚函数：查询设备是否为顺序访问设备
 * @param self 指向 XIODevice 对象的指针
 * @return 若设备是顺序访问则返回 true，否则返回 false
 */
bool XIODevice_isSequential_base(const XIODevice* self);

/**
 * @brief 虚函数：获取设备的当前位置
 * @param self 指向 XIODevice 对象的指针
 * @return 设备的当前位置（字节偏移量）
 */
int64_t XIODevice_pos_base(const XIODevice* self);

/**
 * @brief 虚函数：获取设备的总大小
 * @param self 指向 XIODevice 对象的指针
 * @return 设备的总大小（字节数）
 */
int64_t XIODevice_size_base(const XIODevice* self);

/**
 * @brief 虚函数：将设备位置设置到指定偏移量
 * @param self 指向 XIODevice 对象的指针
 * @param pos 要设置的位置（字节偏移量）
 * @return 若寻址成功则返回 true，否则返回 false
 */
bool XIODevice_seek_base(XIODevice* self, int64_t pos);

/**
 * @brief 虚函数：查询设备是否已到达末尾
 * @param self 指向 XIODevice 对象的指针
 * @return 若已到达末尾则返回 true，否则返回 false
 */
bool XIODevice_atEnd_base(const XIODevice* self);

/**
 * @brief 虚函数：将设备位置重置到开头
 * @param self 指向 XIODevice 对象的指针
 * @return 若重置成功则返回 true，否则返回 false
 */
bool XIODevice_reset_base(XIODevice* self);

/**
 * @brief 虚函数：查询当前可读取的字节数
 * @param self 指向 XIODevice 对象的指针
 * @return 当前缓冲区中可立即读取的字节数
 */
int64_t XIODevice_bytesAvailable_base(const XIODevice* self);

/**
 * @brief 虚函数：查询当前待写入的字节数
 * @param self 指向 XIODevice 对象的指针
 * @return 当前缓冲区中待写入的字节数
 */
int64_t XIODevice_bytesToWrite_base(const XIODevice* self);

/**
 * @brief 虚函数：查询缓冲区中是否有完整的一行数据
 * @param self 指向 XIODevice 对象的指针
 * @return 若缓冲区中包含换行符则返回 true，否则返回 false
 */
bool XIODevice_canReadLine_base(const XIODevice* self);

/**
 * @brief 虚函数：等待设备准备好可读数据
 * @param self 指向 XIODevice 对象的指针
 * @param msecs 等待的超时时间（毫秒）
 * @return 若在超时前准备好则返回 true，否则返回 false
 */
bool XIODevice_waitForReadyRead_base(XIODevice* self, int msecs);

/**
 * @brief 虚函数：等待设备完成所有待写入数据
 * @param self 指向 XIODevice 对象的指针
 * @param msecs 等待的超时时间（毫秒）
 * @return 若在超时前完成则返回 true，否则返回 false
 */
bool XIODevice_waitForBytesWritten_base(XIODevice* self, int msecs);

// —————— 信号 ——————

/**
 * @brief 发射 readyRead 信号
 * @param self 指向 XIODevice 对象的指针
 * @return 信号连接器指针（内部使用）
 * @note 当设备有新的数据可供读取时应调用此函数
 */
void* XIODevice_readyRead_signal(XIODevice* self);

/**
 * @brief 发射 bytesWritten 信号
 * @param self 指向 XIODevice 对象的指针
 * @param bytes 已成功写入的字节数
 * @return 信号连接器指针（内部使用）
 * @note 当数据成功写入设备后应调用此函数
 */
void* XIODevice_bytesWritten_signal(XIODevice* self, int64_t bytes);

/**
 * @brief 发射 aboutToClose 信号
 * @param self 指向 XIODevice 对象的指针
 * @return 信号连接器指针（内部使用）
 * @note 在设备即将关闭之前应调用此函数
 */
void* XIODevice_aboutToClose_signal(XIODevice* self);

/**
 * @brief 发射 channelBytesWritten 信号
 * @param self 指向 XIODevice 对象的指针
 * @param channel 写入数据的通道索引
 * @param bytes 已成功写入的字节数
 * @return 信号连接器指针（内部使用）
 */
void* XIODevice_channelBytesWritten_signal(XIODevice* self, int channel, int64_t bytes);

/**
 * @brief 发射 channelReadyRead 信号
 * @param self 指向 XIODevice 对象的指针
 * @param channel 有新数据可读的通道索引
 * @return 信号连接器指针（内部使用）
 */
void* XIODevice_channelReadyRead_signal(XIODevice* self, int channel);

/**
 * @brief 发射 readChannelFinished 信号
 * @param self 指向 XIODevice 对象的指针
 * @return 信号连接器指针（内部使用）
 * @note 当某个读通道结束时应调用此函数
 */
void* XIODevice_readChannelFinished_signal(XIODevice* self);
#ifdef __cplusplus
}
#endif
#endif