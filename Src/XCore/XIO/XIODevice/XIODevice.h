/**
 * @file       XIODevice.h
 * @brief      IO 设备抽象基类公开 API。
 * @details    对齐 Qt QIODevice；提供打开模式、缓冲读写、行读取、事务与
 *             多通道支持，底层 readData/writeData 由子类经虚函数表实现。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或
 *             其他平台 API，平台差异由具体子类适配。
 */
#ifndef XIODevice_H
#define XIODevice_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XObject.h"
//#include"XFileDescriptor.h"
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
/**
 * @brief 设备打开模式位组合；**低位字节与 Qt 6.8 `QIODeviceBase::OpenModeFlag`
 *        逐值一致**（/usr/include/x86_64-linux-gnu/qt6/QtCore/qiodevicebase.h:16-27）。
 *
 * @details 低 8 位（0x0001~0x0080）必须与 Qt 完全相同——否则按 Qt 字面量落盘或
 *          跨端传模式位会静默变成另一种语义（历史教训：本框架旧编码里
 *          `Text=0x0080` 恰好等于 Qt 的 `ExistingOnly`、`NewOnly=0x0020`
 *          等于 Qt 的 `Unbuffered`）。
 *
 *          `Create` 是**本框架独有的兼容位**（Qt 6.8 已无此标志，Qt 的
 *          `QFile::open(WriteOnly)` 在文件不存在时直接创建），放在 Qt 不使用的
 *          bit8，避免与 Qt 的 `Text`(0x10) 冲突；基类不解释该位，文件后端
 *          （`XDeviceFile_legacyOpen` / win32）按符号判定即可。
 *          `Existing` 是 `ExistingOnly` 的同义别名（Qt 无 `Existing` 名字，
 *          但本仓库既有调用点较多，保留为别名而非删除）。
 *          **对齐台账 gaps #30/#31**：`XIODevice_flush` / `XIODevice_Create` /
 *          `XIODevice_Existing` 都是 Qt 6.8 没有的名字，但各有十余/三十余处
 *          跨模块调用（XImageWriter/XSsh/XTelnet/XHttpServer/mbedtls/sqlite/
 *          XFtp/XLink/XImage 等），按「有调用保留并登记」处置，不删。
 */
typedef enum /*XIODevice*/
{
	XIODevice_NotOpen      = 0x0000, ///< 设备未打开
    XIODevice_ReadOnly     = 0x0001, ///< 只读
    XIODevice_WriteOnly    = 0x0002, ///< 只写
    XIODevice_ReadWrite    = 0x0003, ///< 读写 (ReadOnly|WriteOnly)
    XIODevice_Append       = 0x0004, ///< 追加模式
    XIODevice_Truncate     = 0x0008, ///< 打开时截断
    XIODevice_Text         = 0x0010, ///< 文本模式（读时剥 '\r'，对齐 Qt 6.8）
    XIODevice_Unbuffered   = 0x0020, ///< 绕过内部读缓冲（对齐 Qt 6.8）
    XIODevice_NewOnly      = 0x0040, ///< 仅当不存在时创建（已存在则失败）
    XIODevice_ExistingOnly = 0x0080, ///< 仅当存在时打开（不存在则失败）
    XIODevice_Create       = 0x0100, ///< 【本框架扩展，非 Qt 标志】允许创建；基类不解释
    XIODevice_Existing     = XIODevice_ExistingOnly ///< 【别名】与 ExistingOnly 同义
}XIODeviceBaseMode;
//IO设备
typedef struct XIODevice
{
	XObject m_class;//继承类：第一个成员，由 XClass 管理，禁止手工修改
    XFd      m_fd;                       /**< @brief XFileDescriptor 统一标识符（继承层级使用） */
    bool m_textModeEnabled;             /**< 【有意镜像位·已核差异（台账 P0-E13）】文本模式镜像位；唯一写入点
                                             是 XIODevice_setTextModeEnabled（XIODevice.c:202），全仓**零读点**——
                                             Text 判定一律以 `m_openMode` 的 `XIODevice_Text` 位为准
                                             （XIODevice_isTextModeEnabled 直接读 openMode 位）。本字段仅作为
                                             Qt `QIODevicePrivate::textModeEnabled` 的同形镜像保留，属公开结构体
                                             （ABI 变更须随下一次结构体版本同批删除），调用方不得依赖它的值。 */
    uint16_t m_openMode;                /**< 打开模式位组合（XIODeviceBaseMode）；16 位以容纳扩展位 Create=0x0100 */
    uint8_t m_currentReadChannel;       /**< 当前读通道索引；有效范围 [0, readChannelCount-1]，单通道设备固定为 0 */
    uint8_t m_currentWriteChannel;      /**< 当前写通道索引；有效范围 [0, writeChannelCount-1]，单通道设备固定为 0 */
    XIODevicePrivate* m_d;              /**< 私有实现数据（通道缓冲、事务、错误串），由对象拥有并在析构时释放；仅供实现使用，禁止直接访问；init 分配失败时可为 NULL */
}XIODevice;

/** XFileDescriptor 统一标识符（继承层级使用） */
//#define XIODevice_fd(io) ((io) && ((XIODevice*)io)->m_d ? ((XIODevice*)io)->m_d->xfd : XFD_INVALID)

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
XIODevice* XIODevice_create_ex(XMemoryType memory);

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
 * @brief 获取 XFileDescriptor 统一标识符。
 * @param sock 套接字实例（非 NULL）
 * @return XFileDescriptor fd，未分配返回 XFD_INVALID
 */
XFd XIODevice_fd(const XIODevice* self);
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
 * @return 读通道的数量；**关闭态返回 0**，可读设备打开后为 1
 * @note 对齐 Qt 6.8（qiodevice.cpp:756-766）：关闭时 readChannelCount=0。
 *       原实现关闭态也返回 1，与 Qt 相反。
 */
int XIODevice_readChannelCount(const XIODevice* self);

/**
 * @brief 获取设备支持的写通道数量
 * @param self 指向 XIODevice 对象的指针
 * @return 写通道数量；关闭态返回 0，可写设备打开后为 1
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
 * @return 实际读取的字节数；设备未打开/不可读、maxlen<0 或底层出错且一字未得时返回 -1
 * @note 对齐 Qt 6.8 `QIODevice::read`：**先判可读（CHECK_READABLE）再判 maxlen**，
 *       所以「关闭态 read(buf,0)」返回 -1 而不是 0；打开态 read(buf,0) 返回 0。
 *       读取顺序是「先消费内部缓冲、不足部分再向 readData 取」，短读合法。
 */
int64_t XIODevice_read_1(XIODevice* self, char* data, int64_t maxlen);
/* `XIODevice_read_2`（读进调用方 XByteArray，带 isAppend）已按 API 收敛删除：
 * Qt 6.8 无「读进调用方数组」语义，设备层能力由 `XIODevice_read_1`（char*）
 * 与 `XIODevice_read_3`（返回新 XByteArray）完全覆盖，数组扩容/尺寸回填属容器
 * 职责。需要写进既有数组时用 `XByteArray_resize_base` + `XIODevice_read_1`。 */
/**
 * @brief 从设备中读取最多 maxlen 个字节的数据，并返回一个新的 XByteArray 对象
 * @param self 指向 XIODevice 对象的指针
 * @param maxlen 要读取的最大字节数；**<= 0 返回空数组对象**（对齐 Qt 6.8
 *               `read(qint64)`：CHECK_READABLE / CHECK_MAXLEN 失败都返回空
 *               QByteArray，不返回 NULL；`maxSize == 0` 仍会给子类一次
 *               `readData(data, 0)` 钩子，P0-56）
 * @return 包含读取数据的 XByteArray 对象指针（即使为空也返回对象；仅 self 为空
 *         或对象创建失败时返回 NULL）
 * @note 返回的对象需要手动释放
 */
XByteArray* XIODevice_read_3(XIODevice* self, int64_t maxlen);

/**
 * @brief 从设备中读取所有可用数据到调用方缓冲区中
 * @param self 指向 XIODevice 对象的指针
 * @param buff 调用方提供用于存放读取数据的缓冲区，借用、不会取得所有权
 * @param buffSize 缓冲区容量（字节数）
 * @return 实际读取的字节数；参数非法时返回 0，设备不可读时返回 -1
 * @note 对齐 Qt `QIODevice::readAll`：顺序设备**循环 readData 直到读空**（而不是只看
 *       内部 ring buffer 计数），因此「bytesAvailable() 恒 0 的套接字」也能读全；
 *       可用字节数超过 buffSize 时按 buffSize 截断。
 */
int64_t XIODevice_readAll_1(XIODevice* self, char* buff, int64_t buffSize);
/**
 * @brief 从设备中读取所有可用数据到 XByteArray 缓冲区中
 * @param self 指向 XIODevice 对象的指针
 * @param buff 调用方提供的 XByteArray 存储空间；函数按 isAppend 调整其大小并写入数据
 * @param isAppend 为 true 时在 buff 现有内容后追加，为 false 时覆盖原有内容
 * @return 实际读取的字节数；参数非法时返回 0
 */
int64_t XIODevice_readAll_2(XIODevice* self, XByteArray* buff,bool isAppend);
/**
 * @brief 从设备中读取所有可用数据，并返回一个新的 XByteArray 对象
 * @param self 指向 XIODevice 对象的指针
 * @return 包含所有读取数据的 XByteArray 对象指针，创建失败时返回 NULL
 * @note 返回的对象需要手动释放
 */
XByteArray* XIODevice_readAll_3(XIODevice* self);

/**
 * @brief 从设备中读取一行数据（直到遇到换行符 '\\n' 或达到 maxlen-1 限制）
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放读取数据的缓冲区
 * @param maxlen 缓冲区的最大长度（**必须 >= 2**，要留出结尾 '\\0'）
 * @return 实际读取的字节数（包含换行符）；无数据可读返回 0，出错返回 -1
 * @note 对齐 Qt 6.8 `QIODevice::readLine(char*, qint64)`：**总会写结尾 '\\0'**
 *       （qiodevice.cpp:1383/1404/1414），最多写 maxlen-1 个数据字节；
 *       maxlen < 2 直接返回 -1（CHECK_LINEMAXLEN）。
 */
int64_t XIODevice_readLine_1(XIODevice* self, char* data, int64_t maxlen);
/* `XIODevice_readLine_2`（读一行到调用方 XByteArray）已按 API 收敛删除：它只是
 * Qt `readLine(0)` 的「写进调用方数组」包装，Qt 6.8 无该签名；同一语义由
 * `XIODevice_readLine_3`（= `readLine(0)`，返回新 XByteArray）与
 * `XIODevice_readLine_4`（= `readLine(maxSize)`）覆盖。 */
/**
 * @brief 从设备中读取一行数据，并返回一个新的 XByteArray 对象
 * @param self 指向 XIODevice 对象的指针
 * @return 包含读取行数据的 XByteArray 对象指针；无数据时返回空数组对象
 * @note 对齐 Qt `readLine(0)`（无长度上限）。需要限长时用 XIODevice_readLine_4。
 *       返回的对象需要手动释放。
 */
XByteArray* XIODevice_readLine_3(XIODevice* self);
/**
 * @brief 从设备中读取一行数据（限长），并返回一个新的 XByteArray 对象
 * @param self 指向 XIODevice 对象的指针
 * @param maxlen 最大字节数，对齐 Qt `QIODevice::readLine(qint64 maxSize)`：
 *               `0` 表示不限长；`1` 非法（返回空数组）；其余最多读 maxlen-1 字节
 * @return 包含读取行数据的 XByteArray 对象指针（即使为空也返回对象，失败返回 NULL）
 * @note 返回的对象需要手动释放。
 */
XByteArray* XIODevice_readLine_4(XIODevice* self, int64_t maxlen);
/**
 * @brief 开始一个 I/O 事务
 * @param self 指向 XIODevice 对象的指针
 * @note 对齐 Qt 6.8 `QIODevice::startTransaction`（qiodevice.cpp:1561-1572）：
 *       记录事务原点；**重复调用只告警、不改原点**（嵌套事务不支持）。
 *       顺序设备的事务读会把数据保留在内部缓冲里以支持回滚；
 *       随机访问设备的回滚通过 seek 回事务原点实现。
 */
void XIODevice_startTransaction(XIODevice* self);

/**
 * @brief 提交当前的 I/O 事务
 * @param self 指向 XIODevice 对象的指针
 * @note 对齐 Qt：无事务时只告警、不做任何事。顺序设备提交时丢弃事务区间
 *       已消费的数据（其余仍留在缓冲里作为后续可读数据）。
 */
void XIODevice_commitTransaction(XIODevice* self);

/**
 * @brief 回滚当前的 I/O 事务
 * @param self 指向 XIODevice 对象的指针
 * @note 对齐 Qt：无事务时只告警。回滚后位置回到 startTransaction 处，
 *       同一段数据可以被重新读出。
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
 * @return 实际写入的字节数；设备未打开/不可写、len<0 时返回 -1；len==0 返回 0
 * @note 对齐 Qt 6.8 `QIODevice::write`：**先 CHECK_WRITABLE 再 CHECK_MAXLEN**，
 *       所以 write(x,0) 是合法的空写（返回 0）。基类**不发** bytesWritten——
 *       Qt 里该信号由真正完成一次设备写入的子类（QTcpSocket/QBuffer 等）发射。
 */
int64_t XIODevice_write_1(XIODevice* self, const char* data, int64_t len);

/**
 * @brief 向设备写入一个 XByteArray 对象的内容
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向要写入的 XByteArray 对象
 * @return 实际写入的字节数；**空数组返回 0**（对齐 Qt `write(QByteArray())`：
 *         走 CHECK_WRITABLE 后按 maxSize==0 处理，未打开仍 -1；修复前因空
 *         XByteArray 的 data 指针为 NULL 被当作非法入参返回 -1，P0-55）。
 *         其余契约同 `XIODevice_write_1`（未打开/不可写/长度 <0 返回 -1）。
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
 * @note **本框架扩展**：Qt 6.8 的 `QIODevice` 没有 flush()，只有
 *       `QFileDevice::flush()`（fsync 语义，对应本仓库的 XFileDevice_flush）。
 *       本函数刷的是 XIODevice 内部的环形写缓冲，与 XFileDevice_flush 同名不同义；
 *       因全仓尚有十余处调用，暂保留（见对齐台账 gaps）。
 */
bool XIODevice_flush(XIODevice* self);
/**
 * @brief 从设备中窥探（不移除）最多 maxlen 个字节的数据到缓冲区 data 中
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放窥探数据的缓冲区
 * @param maxlen 要窥探的最大字节数
 * @return 实际窥探到的字节数；设备未打开/不可读、maxlen<0 时返回 -1
 * @note 对齐 Qt 6.8 `QIODevice::peek`：先 CHECK_MAXLEN 再 CHECK_READABLE；
 *       **不移动 pos**，peek 过的数据后续 read 仍会读到。
 */
int64_t XIODevice_peek_1(XIODevice* self, char* data, int64_t maxlen);
/* `XIODevice_peek_2`（窥探进调用方 XByteArray）已按 API 收敛删除：Qt 6.8 无该
 * 语义（只有 `peek(char*, qint64)` 与 `peek(qint64)`），设备层能力由
 * `XIODevice_peek_1` 与 `XIODevice_peek_3` 完全覆盖。 */
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
 * @return 实际跳过的字节数；设备未打开/不可读、maxSize<0 时返回 -1
 * @note 对齐 Qt 6.8 `QIODevice::skip`（qiodevice.cpp:1939-2003）：
 *       ① 先吃掉内部读缓冲（peek/readLine 预读过的前缀不会重复读出）；
 *       ② 随机访问设备再走 seek 快路径（seek 到 pos+n）；
 *       ③ 最后才用 skipData 逐块读丢弃。
 *       文本模式或顺序设备事务中改走「按读跳过」，保证 '\r' 折算一致。
 */
int64_t XIODevice_skip(XIODevice* self, int64_t maxSize);

/**
 * @brief 将一个字符放回输入流的前端，使其成为下一个被读取的字符
 * @param self 指向 XIODevice 对象的指针
 * @param c 要放回的字符
 * @note 对齐 Qt 6.8 `QIODevice::ungetChar`：不可读设备忽略；**事务进行中忽略**；
 *       随机访问设备把 pos 减 1。放回的字符对 bytesAvailable/read/peek/readLine
 *       全部可见（Qt 文档契约；本机 Qt 6.8.0 的多字节 peek/read 在该场景有
 *       跳过字节的实现缺陷，X 侧按文档语义实现）。
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
 * @return 包含错误信息的 XString 对象；未设置过错误时返回 "Unknown error"
 * @note 返回的对象是副本，调用者需要负责释放。文案口径对齐 Qt
 *       （`d->errorString.isEmpty() ? "Unknown error" : d->errorString`）。
 *       子类用 XIODevice_setErrorString 填充真实文案（如 "No such file or directory"），
 *       open() 成功时会清空。
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

/* XClass create API default-memory wrappers. */
#undef XIODevice_create
#define XIODevice_create() XIODevice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif