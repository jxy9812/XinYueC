#ifndef XSOCKET_H
#define XSOCKET_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XIODevice.h"
#include<stdio.h>
#include<stdint.h>
#define XSOCKETBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSocket))       //XSocketBase虚函数表大小
//XSwitchDevice虚函数表枚举
XCLASS_DEFINE_BEGING(XSocket)
XCLASS_DEFINE_ENUM(XSocket, ConnectToHost) = XCLASS_VTABLE_GET_SIZE(XIODevice),
XCLASS_DEFINE_ENUM(XSocket, DisconnectFromHost),
XCLASS_DEFINE_ENUM(XSocket, WaitForConnected),
XCLASS_DEFINE_ENUM(XSocket, WaitForDisconnected),
XCLASS_DEFINE_ENUM(XSocket, LocalAddress),
XCLASS_DEFINE_ENUM(XSocket, LocalPort),
XCLASS_DEFINE_END(XSocket)

/**
 * @brief 套接字错误类型枚举
 * @details 与 QAbstractSocket::SocketError 对齐
 */
    typedef enum {
    XSOCKET_CONNECTION_REFUSED_ERROR = 0,      // 连接被对端拒绝（或超时）
    XSOCKET_REMOTE_HOST_CLOSED_ERROR = 1,      // 远程主机关闭了连接
    XSOCKET_HOST_NOT_FOUND_ERROR = 2,          // 未找到主机地址
    XSOCKET_SOCKET_ACCESS_ERROR = 3,           // 应用程序缺乏所需权限
    XSOCKET_SOCKET_RESOURCE_ERROR = 4,         // 本地系统资源耗尽
    XSOCKET_SOCKET_TIMEOUT_ERROR = 5,          // 套接字操作超时
    XSOCKET_DATAGRAM_TOO_LARGE_ERROR = 6,      // 数据报大于操作系统限制
    XSOCKET_NETWORK_ERROR = 7,                 // 网络发生错误
    XSOCKET_ADDRESS_IN_USE_ERROR = 8,          // 地址已在使用中
    XSOCKET_SOCKET_ADDRESS_NOT_AVAILABLE_ERROR = 9, // 地址不属于主机
    XSOCKET_UNSUPPORTED_SOCKET_OPERATION_ERROR = 10, // 操作系统不支持请求的操作
    XSOCKET_UNFINISHED_SOCKET_OPERATION_ERROR = 11, // 上次操作尚未完成
    XSOCKET_PROXY_AUTHENTICATION_REQUIRED_ERROR = 12, // 代理需要认证
    XSOCKET_SSL_HANDSHAKE_FAILED_ERROR = 13,   // SSL/TLS 握手失败
    XSOCKET_PROXY_CONNECTION_REFUSED_ERROR = 14, // 无法联系代理服务器
    XSOCKET_PROXY_CONNECTION_CLOSED_ERROR = 15, // 与代理服务器的连接意外关闭
    XSOCKET_PROXY_CONNECTION_TIMEOUT_ERROR = 16, // 与代理服务器的连接超时
    XSOCKET_PROXY_NOT_FOUND_ERROR = 17,        // 未找到代理地址
    XSOCKET_PROXY_PROTOCOL_ERROR = 18,         // 代理服务器响应无法理解
    XSOCKET_OPERATION_ERROR = 19,              // 在不允许的状态下尝试操作
    XSOCKET_SSL_INTERNAL_ERROR = 20,           // SSL 库报告内部错误
    XSOCKET_SSL_INVALID_USER_DATA_ERROR = 21,  // 提供了无效的 SSL 数据
    XSOCKET_TEMPORARY_ERROR = 22,              // 发生临时错误
    XSOCKET_UNKNOWN_SOCKET_ERROR = -1          // 未识别的错误
} XSocketError;

/**
 * @brief 绑定标志枚举
 * @details 与 QAbstractSocket::BindFlag 对齐
 */
typedef enum {
    XSOCKET_SHARE_ADDRESS = 0x1,               // 允许其他服务绑定到相同地址和端口
    XSOCKET_DONT_SHARE_ADDRESS = 0x2,          // 独占绑定地址和端口
    XSOCKET_REUSE_ADDRESS_HINT = 0x4,          // 提示应尝试重新绑定已绑定的地址
    XSOCKET_DEFAULT_FOR_PLATFORM = 0x0         // 使用当前平台的默认选项
} XSocketBindFlag;

/**
 * @brief 套接字状态枚举
 * @details 定义套接字在不同阶段的状态
 */
typedef enum 
{
    XSOCKET_UNCONNECTED_STATE = 0,  // 套接字未连接
    XSOCKET_HOST_LOOKUP_STATE = 1,  // 套接字正在执行主机名查找
    XSOCKET_CONNECTING_STATE = 2,   // 套接字已开始建立连接
    XSOCKET_CONNECTED_STATE = 3,    // 连接已建立
    XSOCKET_BOUND_STATE = 4,        // 套接字已绑定到地址和端口
    XSOCKET_LISTENING_STATE = 5,    // 仅用于内部使用
    XSOCKET_CLOSING_STATE = 6       // 套接字即将关闭(数据可能仍在等待写入)
}XSocketState;
/**
 * @brief 套接字类型枚举
 * @details 定义支持的网络协议类型
 */
typedef enum {
    XSOCKET_TYPE_TCP = 0,          // TCP协议
    XSOCKET_TYPE_UDP = 1,          // UDP协议
    XSOCKET_TYPE_SCTP = 2,         // SCTP协议
    XSOCKET_TYPE_UNKNOWN = -1      // 非TCP、UDP和SCTP的其他协议
} XSocketType;

/**
 * @brief 网络层协议枚举
 * @details 与 QAbstractSocket::NetworkLayerProtocol 对齐
 */
typedef enum {
    XSOCKET_IPV4_PROTOCOL = 0,
    XSOCKET_IPV6_PROTOCOL = 1,
    XSOCKET_ANY_IP_PROTOCOL = 2,
    XSOCKET_UNKNOWN_NETWORK_LAYER_PROTOCOL = -1
} XSocketNetworkLayerProtocol;

/**
 * @brief 套接字基础类结构体
 * @details 继承自XIODeviceBase，包含套接字的基本属性和状态
 */
typedef struct XSocketBase
{
	XIODevice m_class;//父对象
    XSocketState m_state;//
    XSocketType m_socketType;
    XSocketError m_socketError; // 新增：存储最后发生的错误
    XString* m_peerName;//远程主机名
    XString* m_peerAddress;//远程主机地址
    uint16_t m_peerPort;//远程主机端口
    bool m_ipv6Enabled;           // IPv6启用标志
}XSocketBase;
/**
 * @brief 初始化XSocketBase对象
 * @param socket XSocketBase对象指针
 * @retval 无
 */
void XSocketBase_init(XSocketBase* socket);

/**
 * @brief 连接到指定主机和端口（基础实现，调用虚函数）
 * @param socket XSocketBase对象指针
 * @param hostName 主机名或IP地址字符串
 * @param port 目标端口号
 * @param mode IO设备操作模式
 * @retval 无
 */
void XSocket_connectToHost_base(XSocketBase* socket, const char* hostName, uint16_t port, XIODeviceBaseMode mode);

/**
 * @brief 断开与主机的连接（基础实现，调用虚函数）
 * @param socket XSocketBase对象指针
 * @retval 无
 */
void XSocket_disconnectFromHost_base(XSocketBase* socket);

/**
 * @brief 等待连接完成（基础实现，调用虚函数）
 * @param socket XSocketBase对象指针
 * @param msecs 超时时间（毫秒）
 * @retval 无
 */
void XSocket_waitForConnected_base(XSocketBase* socket, int msecs);

/**
 * @brief 等待断开连接（基础实现，调用虚函数）
 * @param socket XSocketBase对象指针
 * @param msecs 超时时间（毫秒）
 * @retval 无
 */
void XSocket_waitForDisconnected_base(XSocketBase* socket, int msecs);

/**
 * @brief 获取本地地址（基础实现，调用虚函数）
 * @param socket XSocketBase对象指针
 * @retval 本地IP地址字符串，失败返回NULL
 */
const char* XSocket_localAddress_base(XSocketBase* socket);

/**
 * @brief 获取本地端口（基础实现，调用虚函数）
 * @param socket XSocketBase对象指针
 * @retval 本地端口号，失败返回0
 */
uint16_t XSocket_localPort_base(XSocketBase* socket);

/**
 * @brief 获取远程主机地址
 * @param socket XSocketBase对象指针
 * @retval 远程IP地址字符串，失败返回NULL
 */
const char* XSocket_peerAddress(XSocketBase* socket);

/**
 * @brief 获取远程主机名
 * @param socket XSocketBase对象指针
 * @retval 远程主机名字符串，失败返回NULL
 */
const char* XSocket_peerName(XSocketBase* socket);

/**
 * @brief 获取远程主机端口
 * @param socket XSocketBase对象指针
 * @retval 远程端口号，失败返回0
 */
uint16_t XSocket_peerPort(XSocketBase* socket);

/**
 * @brief 设置套接字类型（仅在未连接状态有效）
 * @param socket XSocketBase对象指针
 * @param type 要设置的套接字类型（TCP/UDP/SCTP等）
 * @retval 无
 */
void XSocket_setSocketType(XSocketBase* socket, XSocketType type);

/**
 * @brief 获取当前套接字类型
 * @param socket XSocketBase对象指针
 * @retval 当前套接字类型，失败返回XSOCKET_TYPE_UNKNOWN
 */
XSocketType XSocket_socketType(const XSocketBase* socket);

/**
 * @brief 获取当前套接字状态
 * @param socket XSocketBase对象指针
 * @retval 当前套接字状态，失败返回XSOCKET_UNCONNECTED_STATE
 */
XSocketState XSocket_state(const XSocketBase* socket);

/**
 * @brief 宏定义：复用XIODeviceBase的isOpen方法
 * @note 判断套接字是否处于打开状态
 */
#define XSocket_isOpen_base					XIODevice_isOpen

/**
* @brief 宏定义：复用XIODeviceBase的open_base方法
* @note 基础打开方法
*/
#define XSocket_open_base				XIODevice_open_base

/**
* @brief 宏定义：复用XIODeviceBase的close_base方法
* @note 基础关闭方法
*/
#define XSocket_close_base				XIODevice_close_base

/**
* @brief 宏定义：复用XIODeviceBase的setDevice_base方法
* @note 设置设备基础属性
*/
//#define XSocket_setDevice_base			XIODevice_setDevice_base

/**
 * @brief 宏定义：复用XIODeviceBase的delete_base方法
 * @note 基础删除方法
 */
#define XSocket_delete_base				XIODevice_deleteLater

/**
* @brief 宏定义：复用XIODeviceBase的poll_base方法
* @note 基础轮询方法
*/
#define XSocket_poll_base				XIODevice_poll_base

/**
 * @brief 宏定义：复用XIODeviceBase的setWriteBuffer_base方法
 * @note 设置写缓冲区
 */
//#define XSocket_setWriteBuffer_base     XIODevice_setWriteBuffer_base

/**
* @brief 宏定义：复用XIODeviceBase的setReadBuffer_base方法
* @note 设置读缓冲区
*/
//#define XSocket_setReadBuffer_base      XIODevice_setReadBuffer_base

/**
* @brief 宏定义：复用XIODeviceBase的write_base方法
* @note 基础写数据方法
*/
#define XSocket_write_base              XIODevice_write

/**
 * @brief 宏定义：复用XIODeviceBase的read_base方法
 * @note 基础读数据方法
 */
#define XSocket_read_base               XIODevice_read

/**
* @brief 宏定义：复用XIODeviceBase的getBytesAvailable_base方法
* @note 获取可用字节数
*/
#define XSocket_bytesAvailable_base  XIODevice_bytesAvailable_base

/**
 * @brief 宏定义：复用XIODeviceBase的getBytesToWrite_base方法
 * @note 获取待写入字节数
 */
#define XSocket_bytesToWrite_base    XIODevice_bytesToWrite_base

/**
 * @brief 宏定义：复用XIODeviceBase的atEnd_base方法
 * @note 判断是否到达数据末尾
 */
#define XSocket_atEnd_base              XIODevice_atEnd_base

/**
 * @brief 宏定义：复用XIODeviceBase的writeFull_base方法
 * @note 完整写入数据方法
 */
//#define XSocket_writeFull_base          XIODevice_writeFull_base

/*                                              信号  ·                   */
void* XSocket_connected_signal(XSocket* socket);//连接上
void* XSocket_disconnected_signal(XSocket* socket);//连接断开
void* XSocket_stateChanged_signal(XSocket* socket, XSocketState state);//状态改变信号  XSocketState state
// 平台相关实现包含
#ifdef WIN32
#include"XSocketWin32.h"  // Windows平台套接字实现
#elif defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XSocketPosix.h"  // Linux/BSD平台套接字实现
#elif defined(USE_STDPERIPH_DRIVER)
// 嵌入式平台实现
#endif

#ifdef __cplusplus
}
#endif
#endif