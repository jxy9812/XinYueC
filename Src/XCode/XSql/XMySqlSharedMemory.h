/**
 * @file XMySqlSharedMemory.h
 * @brief MySQL 共享内存本地传输接口（MYSQL_PROTOCOL_MEMORY，跨平台通用实现）。
 *
 * @details
 * 本接口描述 MySQL 客户端在本机使用共享内存连接 MySQL 服务器时所需的传输能力
 * （连接握手、分块帧协议、跨进程同步）。实现完全位于共享代码
 * （Src/XCode/XSql/XMySqlSharedMemory.c），不包含任何平台头文件：
 * 命名共享内存段的打开/映射/解除映射统一走 XDeviceFile_openSharedMemory /
 * XDeviceFile_map / XDeviceFile_unmap / XDeviceFile_close 三个共享内存
 * 原语。平台 XDeviceFile_openSharedMemory 会为每个段内建一个同名信令
 * 通道（POSIX 为 Unix domain 流式套接字，Windows 为命名管道），返回的
 * XFd 可直接用 XDeviceFile_read / XDeviceFile_write 收发信令字节；
 * 跨进程收发同步完全复刻网络套接字的异步接收语义：数据方写完一块后写
 * 通知字节，对端在信令通道上阻塞等待（非共享内存状态轮询），因此
 * Windows 与 POSIX（Linux/macOS/BSD）行为一致，Linux 同样支持本机
 * 共享内存连接。
 *
 * 线协议由本文件公开（服务器端与客户端必须使用同一组命名段与字段布局）：
 * - 连接协商段 <BASE>_CONNECT_DATA：魔数 + 连接编号 + 请求/应答标志；
 * - 数据段 <BASE>_<连接编号>_DATA：魔数 + 服务器→客户端通道 +
 *   客户端→服务器通道，每条通道为 16000 字节数据区加块长/状态/关闭标志。
 *
 * @note 本头文件属于 XSql 协议模块，不包含任何平台头文件。
 */
#ifndef XMYSQL_SHARED_MEMORY_H
#define XMYSQL_SHARED_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XString.h"
#include "XAtomic.h"

#ifdef __cplusplus
extern "C" {
#endif

/** MySQL 共享内存传输对象，具体布局由通用实现负责。 */
typedef struct XMySqlSharedMemory XMySqlSharedMemory;

/* ============================================================================
 * 线协议布局（服务器端与客户端共用同一组命名段与字段布局）
 * ============================================================================ */

/** 命名共享内存段魔数："XMYS"，用于校验段是否由兼容实现创建。 */
#define XMYSQL_SHARED_MEMORY_MAGIC 0x584D5953u

/** 单块数据最大长度（字节），对齐 MySQL 官方共享内存数据区容量。 */
#define XMYSQL_SHARED_MEMORY_DATA_SIZE 16000u

/**
 * @brief 单向数据通道（服务器→客户端 或 客户端→服务器）。
 * @note 字段按 XAtomic_uint32_t（4 字节对齐）排列，无内部填充，
 *       布局在 Windows/POSIX 编译器上保持一致。
 */
typedef struct {
    XAtomic_uint32_t length; /**< 当前块长度（1..DATA_SIZE，写入方填写的字节数） */
    XAtomic_uint32_t status; /**< 状态：0=空闲，1=本端已写入数据待对端读取 */
    XAtomic_uint32_t closed; /**< 关闭标志：0=正常，1=本端已通知关闭 */
    uint8_t data[XMYSQL_SHARED_MEMORY_DATA_SIZE]; /**< 块数据区 */
} XMySqlSharedMemoryChannel;

/** 数据段线协议布局（段名 <BASE>_<连接编号>_DATA）。 */
typedef struct {
    XAtomic_uint32_t magic;                    /**< 魔数 XMYSQL_SHARED_MEMORY_MAGIC */
    XMySqlSharedMemoryChannel serverToClient;  /**< 服务器→客户端通道 */
    XMySqlSharedMemoryChannel clientToServer;  /**< 客户端→服务器通道 */
} XMySqlSharedMemorySegment;

/** 连接协商段线协议布局（段名 <BASE>_CONNECT_DATA）。 */
typedef struct {
    XAtomic_uint32_t magic;   /**< 魔数 XMYSQL_SHARED_MEMORY_MAGIC */
    XAtomic_uint32_t number;  /**< 服务端分配的连接编号 */
    XAtomic_uint32_t request; /**< 客户端请求标志：0=空闲，1=请求连接 */
    XAtomic_uint32_t answer;  /**< 服务端应答标志：0=空闲，1=已应答 */
} XMySqlSharedMemoryConnect;

/**
 * @brief 打开 MySQL 共享内存传输并完成连接握手。
 * @param baseName MySQL 共享内存基础名称（借用，不能为 NULL），
 *                 例如 "MYSQL"，对应 mysqld --shared-memory-base-name。
 * @param timeoutMs 建立连接的等待时间（毫秒），负数表示不限制。
 * @return 成功返回传输对象；服务器未开启共享内存或打开失败返回 NULL。
 * @note 返回对象由调用方拥有，使用完毕后必须调用 XMySqlSharedMemory_close 释放。
 */
XMySqlSharedMemory* XMySqlSharedMemory_open(const XString* baseName, int timeoutMs);

/**
 * @brief 关闭 MySQL 共享内存传输并释放对象。
 * @param shared 传输对象，可为 NULL。
 * @note 关闭时向服务器发送连接关闭通知，解除映射并释放共享内存描述符。
 */
void XMySqlSharedMemory_close(XMySqlSharedMemory* shared);

/**
 * @brief 从 MySQL 共享内存传输读取指定长度的数据。
 * @param shared 传输对象，不能为 NULL。
 * @param data 输出缓冲区，不能为 NULL。
 * @param size 要读取的字节数。
 * @param timeoutMs 单次等待时间（毫秒），负数表示不限制。
 * @return 成功返回 true；连接关闭、超时或协议错误返回 false。
 */
bool XMySqlSharedMemory_read(XMySqlSharedMemory* shared, void* data,
                             size_t size, int timeoutMs);

/**
 * @brief 向 MySQL 共享内存传输写入指定长度的数据。
 * @param shared 传输对象，不能为 NULL。
 * @param data 输入缓冲区，不能为 NULL。
 * @param size 要写入的字节数。
 * @param timeoutMs 单次等待时间（毫秒），负数表示不限制。
 * @return 成功返回 true；连接关闭、超时或协议错误返回 false。
 */
bool XMySqlSharedMemory_write(XMySqlSharedMemory* shared, const void* data,
                              size_t size, int timeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* XMYSQL_SHARED_MEMORY_H */
