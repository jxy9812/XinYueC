/**
 * @file       XIODevice_Protected.h
 * @brief      XIODevice 保护接口（子类重载入口与内部辅助）。
 * @details    仅供子类（XSerialPort 等）和内部实现使用，不对外公开；
 *             使用者必须显式包含本头文件。
 */
#ifndef XIODevice_Protected_H
#define XIODevice_Protected_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XIODevice.h"
/**
 * @brief 基类 open 的具体实现（**非虚**，对齐 Qt 的 `QIODevice::open(OpenMode)`）。
 * @param self 指向 XIODevice 对象的指针
 * @param mode 新的打开模式（XIODeviceBaseMode 位组合，`NotOpen` 被拒绝）
 * @return 成功返回 true；`self` 为空、未分配私有数据或 `mode == NotOpen` 返回 false
 * @note **子类重载 Open 后必须显式调用本函数**来收口状态（openMode / pos /
 *       读缓冲 / 通道数 / errorString），调 `XIODevice_open_base` 会经虚表回到
 *       子类自己造成无限递归（Qt 里对应的写法是 `QFile::open()` 里
 *       `QIODevice::open(mode)`，qfile.cpp:922）。
 *       对齐 Qt 6.8 qiodevice.cpp:756-773：
 *       ① `openMode = mode`；② `pos = (mode & Append) ? size() : 0`；
 *       ③ 清读缓冲（重开不得读到上一会话的预读数据）；④ 通道数按可读/可写
 *       置 1 或 0；⑤ 清 errorString；⑥ 允许对同一对象重复 open
 *       （QFile 那一层的「已打开」闸由子类自己加，见 VXFile_open）。
 */
bool XIODevice_open(XIODevice* self, XIODeviceBaseMode mode);

/**
 * @brief 基类 close 的具体实现（**非虚**，对齐 Qt 的 `QIODevice::close()`）。
 * @param self 指向 XIODevice 对象的指针
 * @note **子类重载 Close 后必须显式调用本函数**收口基类状态（发
 *       aboutToClose、置 NotOpen、pos/事务/读通道数复位）；调
 *       `XIODevice_close_base` 会经虚表回到子类自己造成无限递归。Qt 里
 *       对应的写法是 `QFileDevice::close()` 内部调 `QIODevice::close()`
 *       （qfiledevice.cpp:332）。`openMode` 已是 NotOpen 时本函数直接早退
 *       （与 Qt 同），句柄关闭由子类自己负责（基类不认识句柄）。
 */
void XIODevice_close(XIODevice* self);

/**
 * @brief 虚函数：从设备读取原始数据（纯虚函数，子类必须实现）
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放读取数据的缓冲区
 * @param maxlen 要读取的最大字节数
 * @return 实际读取的字节数
 */
int64_t XIODevice_readData_base(XIODevice* self, char* data, int64_t maxlen);

/**
 * @brief 虚函数：向设备写入原始数据（纯虚函数，子类必须实现）
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向要写入的数据缓冲区
 * @param len 要写入的字节数
 * @return 实际写入的字节数
 */
int64_t XIODevice_writeData_base(XIODevice* self, const char* data, int64_t len);

/**
 * @brief 设置设备的错误描述字符串
 * @param self 指向 XIODevice 对象的指针
 * @param str 错误描述的 C 字符串，若为 NULL 则清空错误信息
 */
void XIODevice_setErrorString(XIODevice* self, const char* str);

/**
 * @brief 设置打开模式并同步通道数（**子类在 open 成功后必须调用**）。
 * @param self 指向 XIODevice 对象的指针
 * @param mode 新的打开模式（XIODeviceBaseMode 位组合）
 * @note 对齐 Qt 6.8 的 protected `QIODevice::setOpenMode`
 *       （qiodevice.cpp:508-518）：`openMode = mode`，随后
 *       「可读 ⇒ 读通道数至少 1；不可读 ⇒ 0」，写侧同理。
 *       Qt 的子类用它在中途改模式（如 QTcpSocket 的 SSL 升级），
 *       通道数会跟着变——本框架此前没有该入口，子类只能直接写公有字段，
 *       通道数因此永远不会随模式变化。
 */
void XIODevice_setOpenMode(XIODevice* self, XIODeviceBaseMode mode);
/**
 * @brief 虚函数：从输入流中跳过原始数据
 * @param self 指向 XIODevice 对象的指针
 * @param maxSize 要跳过的最大字节数
 * @return 实际跳过的字节数
 */
int64_t XIODevice_skipData_base(XIODevice* self, int64_t maxSize);

/**
 * @brief 虚函数：从设备读取一行原始数据
 * @param self 指向 XIODevice 对象的指针
 * @param data 指向用于存放读取数据的缓冲区
 * @param maxlen 要读取的最大字节数
 * @return 实际读取的字节数
 */
int64_t XIODevice_readLineData_base(XIODevice* self, char* data, int64_t maxlen);

/**
 * @brief 基类 seek 的具体实现（**非虚**，对齐 Qt 的 `QIODevice::seek(qint64)`）。
 * @param self 指向 XIODevice 对象的指针
 * @param pos 目标位置
 * @return 成功返回 true（顺序设备/未打开/负值返回 false）
 * @note **子类重载 Seek 后必须显式调用本函数**来登记位置并同步内部缓冲；
 *       调 `XIODevice_seek_base` 会经虚表回到子类自己，导致无限递归
 *       （Qt 里对应的写法是 `QBuffer::seek()` 内部调 `QIODevice::seek(pos)`）。
 */
bool XIODevice_seek(XIODevice* self, int64_t pos);

/**
 * @brief 基类 bytesAvailable 的具体实现（**非虚**，对齐 Qt 的
 *        `QIODevice::bytesAvailable()` 基类体，qiodevice.cpp:953-959）。
 * @param self 指向 XIODevice 对象的指针
 * @return 随机访问设备 = max(size()-pos, 0)；顺序设备 = 缓冲字节 - 事务偏移
 * @note **重载 BytesAvailable 的子类必须调用本函数**来叠加自己的引擎/通道
 *       字节（Qt 里 `QAbstractSocket::bytesAvailable()` 就是
 *       `QIODevice::bytesAvailable() + socketEngine->bytesAvailable()`，
 *       qabstractsocket.cpp:1755-1767）。调 `XIODevice_bytesAvailable_base`
 *       会经虚表回到子类自身形成无限递归（历史实测栈溢出，见
 *       XAbstractSocket.c 的 vtable 注释）。
 */
int64_t XIODevice_bytesAvailable(const XIODevice* self);

/**
 * @brief 设置设备的 XFd 文件描述符（仅供子类使用）
 * @param self 指向 XIODevice 对象的指针
 * @param fd 要设置的文件描述符
 */
void XIODevice_setFd(XIODevice* self, XFd fd);

#ifdef __cplusplus
}
#endif
#endif