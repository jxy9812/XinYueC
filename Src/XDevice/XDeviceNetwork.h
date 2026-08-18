/**
 * @file       XDeviceNetwork.h
 * @brief      网络设备类及其平台实现接口。
 * @details    XDeviceNetwork 是可扩展的网络设备类，同时承载 POSIX、Windows
 *          和 lwIP 平台所需的统一网络接口。平台源文件直接实现本头文件声明，
 *          不再额外引入独立的网络后端抽象层。所有 XString 参数仅在调用期间
 *          借用，其内部按 UTF-16 代码单元存储；返回对象的所有权由各 API 注释明确说明。
 *
 * 架构层级：
 *   XAbstractSocket / XTcpSocket / XUdpSocket  -> 业务中间层（通用代码）
 *        |
 *   XDeviceNetwork.h (本文件)                        -> 网络设备公共类接口
 *   |-- windows/XDeviceNetwork_win32.c               -> Windows IOCP 实现
 *   |-- Posix/XDeviceNetwork_posix.c                 -> Linux io_uring 实现
 *   |-- lwip/XDeviceNetwork_lwip.c                   -> lwIP 回调驱动实现
 *
 * 代理协议（SOCKS5/HTTP）在 XNetworkProxy 模块中用通用代码实现
 */

#ifndef XDEVICENETWORK_H
#define XDEVICENETWORK_H
#include "XNetwork_config.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XDevice.h"
#include "XHostAddress.h"
#include "XFileDescriptor.h"
#include "XNetworkProxy.h"
#include "XByteArray.h"
#include "XString.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNETWORK_ON

/**
 * @brief XNetworkInterface 的不完整类型声明。
 * @details 完整定义位于 XNetworkInterface.h；本接口仅在调用期间借用其对象，
 *          具体所有权由对应 API 说明。
 */
typedef struct XNetworkInterface XNetworkInterface;
typedef struct XAbstractSocket XAbstractSocket;

typedef struct XVariant XVariant;

/* =========================================================================
 * 平台移植指南
 * =========================================================================
 *
 * 对外只暴露 XDeviceNetwork 公共接口。平台钩子由 XDeviceNetwork.c 在实现内部
 * 声明，移植新平台时根据异步 I/O 模型实现对应钩子：
 *
 * 【模型 A：投递 + 完成通知】— Windows IOCP / Linux io_uring
 *   - static startAsyncRead(priv, isUdp)
 *       向异步引擎投递一次读操作（WSARecv / IORING_OP_RECV）。
 *       由 socketBind / socketConnect / socketRead / socketContinueRead 内部调用。
 *   - static startAsyncWrite(priv, data, len, destAddr, destPort, isUdp)
 *       向异步引擎投递一次写操作（WSASend / IORING_OP_SEND）。
 *       由 socketWrite 内部调用。
 *   以上两个函数为平台内部 static 实现，不在本头文件声明，
 *   但移植 IOCP / io_uring 类平台时必须实现。
 *
 * 【模型 B：回调驱动】— lwIP
 *   - 读：协议栈通过回调（tcp_recv / udp_recv）自动填充内部缓冲区，
 *         应用层 pull 取数据，无需 startAsyncRead。
 *   - 写：直接调用 tcp_write + tcp_output / udp_sendto，同步入栈，
 *         无需 startAsyncWrite。
 *   回调驱动平台不需要实现 startAsyncRead / startAsyncWrite。
 *
 * 两种模型都通过 XDevice 的虚函数重载向业务层提供统一能力。
 * ========================================================================= */

/* =========================================================================
 * 一、基础类型
 * ========================================================================= */

/**
 * @brief 套接字平台描述符值类型。
 * @details 值 -1 表示无效句柄；有效值只应传回 XNetwork API，不得假定它是
 *          POSIX 文件描述符或直接交给平台函数。
 */
typedef intptr_t XDeviceNetworkSocketHandle;

/**
 * @brief TCP 服务器平台句柄值类型。
 * @details 值 -1 表示无效句柄；仅由 XDeviceNetwork 实现内部和控制命令传递，
 *          业务层不得直接关闭或交给平台函数。
 */
typedef intptr_t XDeviceNetworkServerHandle;

/** @brief 套接字传输类型；枚举值不可按位组合。 */
typedef enum {
    XDeviceNetwork_Tcp = 0,   /**< 面向连接的 TCP 套接字。 */
    XDeviceNetwork_Udp = 1    /**< 无连接的 UDP 数据报套接字。 */
} XDeviceNetworkSocketType;

/** @brief 平台本地字节流传输类型；仅供数据库等后端使用，不可按位组合。 */
typedef enum {
    XDeviceNetwork_LocalStream_Unknown = 0, /**< 未知或未指定的本地传输。 */
    XDeviceNetwork_LocalStream_UnixSocket,  /**< POSIX 域套接字。 */
    XDeviceNetwork_LocalStream_NamedPipe    /**< Windows 命名管道。 */
} XDeviceNetworkLocalStreamType;

/**
 * @brief 地址族类型。
 * @details 复用 XHostAddress_NetworkLayerProtocol；枚举值互斥、不可按位组合。
 */
typedef XHostAddress_NetworkLayerProtocol XDeviceNetworkProtocol;
#define XDeviceNetwork_IPv4  XHostAddress_IPv4Protocol /**< IPv4 地址族。 */
#define XDeviceNetwork_IPv6  XHostAddress_IPv6Protocol /**< IPv6 地址族。 */
#define XDeviceNetwork_Any   XHostAddress_AnyIPProtocol /**< 由平台选择可用地址族。 */

/**
 * @brief 发送一次 ICMP Echo 请求并等待回复。
 *
 * 该接口是同步的一次性探测，不创建长期持有的套接字对象。平台后端负责
 * 选择权限允许的 ICMP 通道（POSIX 的数据报/原始 ICMP、Windows 的系统
 * ICMP 服务、lwIP Raw API），上层不得直接访问平台句柄。同时支持 IPv4 与 IPv6：
 * 各后端根据目标地址的协议族自动选择 ICMPv4 / ICMPv6 通道。
 *
 * @param address 目标 IPv4 或 IPv6 地址；调用期间借用，不能为 NULL。
 * @param identifier 请求标识；用于区分并发或不同调用方的请求。
 * @param sequence 请求序号；用于匹配本次回复。
 * @param payload 请求负载；可为 NULL，但 payloadSize 非零时不能为 NULL。
 * @param payloadSize 请求负载字节数；建议不超过 1400，过大时后端可能拒绝。
 * @param timeoutMilliseconds 等待超时；必须大于 0，单位为毫秒。
 * @param elapsedMilliseconds 可选的调用方输出存储；可为 NULL，成功时写入往返
 *                            耗时，单位为毫秒，失败时保持不变。
 * @return 收到匹配的 Echo Reply 返回 true；参数错误、超时、权限不足或
 *         后端不支持返回 false。
 */
bool XDeviceNetwork_icmpEcho(const XHostAddress* address, uint16_t identifier,
                       uint16_t sequence, const void* payload, size_t payloadSize,
                       int timeoutMilliseconds, uint32_t* elapsedMilliseconds);

/**
 * @brief 查询当前网络后端是否编译了 ICMP Echo 能力。
 * @return 后端声明支持 ICMP Echo 返回 true，否则返回 false。
 * @note 返回 true 只表示能力存在；运行时仍可能因系统权限、路由或防火墙失败。
 */
bool XDeviceNetwork_icmpEchoSupported(void);

/* =========================================================================
 * 二、平台初始化与错误
 * ========================================================================= */

/**
 * @brief 确保网络子系统已经初始化。
 * @return 无。
 * @note 每次调用都增加平台引用计数，必须由同一组件使用 XDeviceNetwork_cleanup
 *       成对释放。跨线程调用是否安全由所选平台后端保证。
 */
void XDeviceNetwork_ensureInit(void);

/**
 * @brief 释放一次网络子系统初始化引用。
 * @return 无。
 * @note 必须与本组件成功执行的 XDeviceNetwork_ensureInit 成对；引用计数归零后平台
 *       可以释放全局网络资源，之后仍在使用的网络对象行为未定义。
 */
void XDeviceNetwork_cleanup(void);

/**
 * @brief 驱动当前网络后端完成一次非阻塞轮询。
 * @details lwIP 后端在此读取网卡输入并处理协议栈回调；POSIX 与 Windows 的
 *          完成事件由各自 I/O 引擎投递，本函数为空操作。业务层不接触 lwIP 类型。
 */
void XDeviceNetwork_poll(void);

/**
 * @brief 获取当前线程最近一次平台网络错误码。
 * @return 当前线程最近一次平台网络调用的错误码；没有错误时返回 0。
 */
int XDeviceNetwork_lastError(void);

/**
 * @brief 将平台错误码转换为可读字符串。
 * @param errorCode XDeviceNetwork_lastError 或平台后端返回的错误码。
 * @return 成功返回新分配的 NUL 结尾错误描述，调用方必须使用 XFree_System
 *         释放；分配失败返回 NULL。
 */
char* XDeviceNetwork_errorString(int errorCode);

/* =========================================================================
 * DNS
 * ========================================================================= */

/**
 * @brief 获取本机主机名。
 * @return 成功返回新建的 XString 主机名，内部按 UTF-16 代码单元存储，调用方
 *         必须使用 XString_delete_base 释放；平台不支持、查询或分配失败返回 NULL。
 */
XString* XDeviceNetwork_localHostName(void);

/**
 * @brief 同步解析主机名。
 * @param name 待解析主机名；不能为 NULL，调用期间只读借用，内部按 UTF-16
 *             代码单元存储，函数不会保存或取得所有权。
 * @return 成功返回新建的 XVector<XHostAddress> 地址列表，元素由容器拥有，
 *         调用方必须使用 XVector_delete_base 释放；参数无效、解析或分配失败
 *         返回 NULL。成功但没有地址时允许返回空容器。
 */
XVector* XDeviceNetwork_lookupName(const XString* name);

/* =========================================================================
 * 三、网络设备打开上下文（由 XFd 持有）
 * ========================================================================= */

/**
 * @brief 网络设备的唯一打开上下文。
 * @details 首成员为 XDeviceContext。平台实现可在本结构体后追加自己的异步 I/O
 *          状态；整个上下文只由 XDevice_open 创建的 XFd 持有和定位。业务层不得
 *          保存或访问该结构体，统一通过 XDevice 的父类门面 API 操作网络设备。
 */
typedef struct XDeviceNetworkContext {
    XDeviceContext m_base;                  /**< XDevice 上下文基类，必须位于首成员。 */
    union {
        XAbstractSocket* m_socket;          /**< 客户端 XAbstractSocket 借用/自有对象。 */
        XDeviceNetworkServerHandle m_serverHandle; /**< TCP 监听句柄。 */
    } m_endpoint;                           /**< 客户端和服务端生命周期互斥的端点状态。 */
    void* m_owner;                          /**< XAbstractSocket 或 XTcpServer 借用指针。 */
    XVector* m_notifiers;                   /**< 平台事件通知器容器，由上下文拥有。 */
    XHostAddress m_peerAddress;             /**< UDP 默认目标地址。 */
    int64_t m_readBufferSize;               /**< 请求的接收缓冲容量。 */
    uint16_t m_peerPort;                    /**< UDP 默认目标端口。 */
    uint32_t m_socketType     : 1;          /**< TCP 或 UDP。 */
    uint32_t m_protocol       : 2;          /**< 地址族选择。 */
    uint32_t m_flags          : 3;          /**< XDeviceOpenFlag 位组合。 */
    uint32_t m_openMode       : 13;         /**< XIODeviceBaseMode。 */
    uint32_t m_hasPeerAddress : 1;          /**< m_peerAddress 是否有效。 */
    uint32_t m_connectedMode  : 1;          /**< 是否已通过 connect 建立默认目标。 */
    uint32_t m_connected      : 1;          /**< 平台已绑定、连接或正在监听。 */
    uint32_t m_ownsSocket     : 1;          /**< m_socket 是否由上下文创建并销毁。 */
    uint32_t m_isServer       : 1;          /**< 是否为 TCP 监听上下文。 */
    uint32_t m_reserved       : 8;          /**< 保留位，必须为 0。 */
} XDeviceNetworkContext;


/* =========================================================================
 * 四、系统代理与GSSAPI（平台相关）
 * ========================================================================= */

/**
 * @brief 获取系统代理配置
 * @param queryUrl 查询 URL；借用，可为 NULL，NULL 表示查询默认系统代理。
 * @param outProxy 调用方提供的已初始化代理配置；成功时函数写入结果，失败时不保证其内容。
 * @return 成功获得系统代理设置返回 true；平台不支持或查询失败返回 false。
 * @note Windows: WinHttpGetProxyForUrl/注册表; Linux: 环境变量; macOS: CFProxySupport
 */
bool XDeviceNetwork_getSystemProxy(const XString* queryUrl, XNetworkProxy* outProxy);

/**
 * @brief GSSAPI认证处理
 * @param serviceName 服务名称，如 "HTTP@proxy.example.com"；借用，不能为 NULL。
 * @param inputToken 输入令牌；借用，可为 NULL，NULL 表示初始化协商。
 * @param outputToken 调用方提供的 XByteArray；成功时写入要发送的令牌，不能为 NULL。
 * @param context 输入输出上下文；首次调用时 *context 必须为 NULL，函数在协商期间写入内部状态。
 * @return 协商完成返回 0，需要继续交换令牌返回 1，失败返回 -1。
 * @note 协商进行中调用方必须原样保存 *context，不得释放或修改其指向的内部状态；
 *       调用返回 0 或 -1 后应以平台返回的 *context 值为准。
 */
int XDeviceNetwork_gssapiAuth(const XString* serviceName,
                         const XByteArray* inputToken,
                         XByteArray* outputToken,
                         void** context);

/* ============================================================================
 * XDeviceNetwork 类接口
 * ============================================================================ */

/** @brief 打开后是否立即对套接字执行一个端点操作。 */
typedef enum XDeviceNetworkOpenOperation
{
    XDeviceNetworkOpen_None    = 0, /**< 只创建套接字，不绑定或连接。 */
    XDeviceNetworkOpen_Connect = 1, /**< 提交异步 TCP/UDP 连接。 */
    XDeviceNetworkOpen_Bind    = 2, /**< 绑定本地地址和端口。 */
    XDeviceNetworkOpen_Adopt   = 3, /**< 接管已创建的平台套接字描述符。 */
    XDeviceNetworkOpen_Local   = 4, /**< 连接 Unix 域套接字或 Windows 命名管道。 */
    XDeviceNetworkOpen_Listen  = 5, /**< 创建并监听 TCP 服务器套接字。 */
    XDeviceNetworkOpen_ListenAdopt = 6 /**< 接管已在监听的平台 TCP 套接字描述符。 */
} XDeviceNetworkOpenOperation;

/**
 * @brief 网络设备打开选项。
 * @details 第一个成员必须是 XDeviceOpenOptions；地址和目标主机名只在 Open 调用
 *          期间借用，设备不会保存外部对象的所有权。
 */
typedef struct XDeviceNetworkOpenOptions
{
    XDeviceOpenOptions m_base;             /**< 通用设备打开选项。 */
    const XHostAddress* m_address;          /**< Bind 的本地地址。 */
    const XHostAddress* m_peerAddress;      /**< UDP 写入的默认目标地址，可为 NULL。 */
    void* m_owner;                          /**< 可选的高层套接字或 TCP 服务器所有者；仅在 Open 调用期间借用。 */
    intptr_t m_socketDescriptor;            /**< Adopt 时接管的平台描述符，成功后所有权转移。 */
    int m_initialState;                     /**< Adopt 时的 XAbstractSocket_SocketState 值。 */
    int m_timeoutMs;                        /**< Local 连接超时毫秒数；负数表示平台默认。 */
    int m_listenBacklog;                    /**< Listen 时的等待队列长度；必须为正数。 */
    uint16_t m_port;                        /**< Connect 目标或 Bind 本地端口。 */
    uint16_t m_peerPort;                    /**< UDP 写入的默认目标端口。 */
    uint32_t m_socketType      : 1;         /**< TCP 或 UDP。 */
    uint32_t m_protocol        : 2;         /**< IPv4、IPv6 或 Any。 */
    uint32_t m_operation       : 3;         /**< None、Connect、Bind、Adopt、Local 或 Listen。 */
    uint32_t m_reuseAddress    : 1;         /**< Bind 时请求地址重用。 */
    uint32_t m_shareAddress    : 1;         /**< Bind 时允许地址共享。 */
    uint32_t m_localStreamType : 2;         /**< Local 时的本地字节流类型。 */
    uint32_t m_reserved        : 22;        /**< 保留位，必须为 0。 */
} XDeviceNetworkOpenOptions;

/** @brief 网络设备专有属性编号，从 XDeviceProperty_Count 开始分配。 */
typedef enum XDeviceNetworkProperty
{
    XDeviceNetworkProperty_SocketType = XDeviceProperty_Count,
    XDeviceNetworkProperty_Protocol,
    XDeviceNetworkProperty_Connected,
    XDeviceNetworkProperty_LocalPort,
    XDeviceNetworkProperty_PeerPort,
    XDeviceNetworkProperty_ReadBufferSize,
    XDeviceNetworkProperty_ReadFinishedBytes,
    XDeviceNetworkProperty_WriteFinishedBytes,
    XDeviceNetworkProperty_WritePending
} XDeviceNetworkProperty;

/** @brief 网络设备专有控制命令。 */
typedef enum XDeviceNetworkCommand
{
    XDeviceNetworkCommand_HandleEvent = XDeviceCommand_Count
        /**< 处理平台事件；in 为 XVarList(XEvent* event)，out 为 NULL。 */,
    XDeviceNetworkCommand_ContinueRead
        /**< 继续异步读取；in/out 均为 NULL。 */,
    XDeviceNetworkCommand_ContinueWrite
        /**< 继续异步写入；in/out 均为 NULL。 */,
    XDeviceNetworkCommand_SetSocketOption
        /**< 设置套接字选项；in 为 XVarList(int option, const XVariant* value)，out 为 NULL。 */,
    XDeviceNetworkCommand_GetSocketOption
        /**< 获取套接字选项；in 为 XVarList(int option)，out 为 XVarList(int value)。 */,
    XDeviceNetworkCommand_GetLastDatagramSender
        /**< 获取最近 UDP 数据报发送方；in 为 NULL，out 为 XVarList(XHostAddress* address, uint16_t* port)。 */,
    XDeviceNetworkCommand_GetReadBuffer
        /**< 获取最近异步读取缓冲区；in 为 NULL，out 为 XVarList(const char* buffer)，返回指针仅在继续读取或关闭前有效。 */,
    XDeviceNetworkCommand_SendDatagram
        /**< 向指定 UDP 端点发送；in 为 XVarList(const void* data, int64_t size, const XHostAddress* address, uint16_t port)，out 为 XVarList(int64_t written)。 */,
    XDeviceNetworkCommand_SetMulticastGroup
        /**< 加入或离开多播组；in 为 XVarList(bool join, const XHostAddress* group, uint32_t interfaceIndex)，out 为 NULL。 */,
    XDeviceNetworkCommand_SetMulticastInterface
        /**< 设置 UDP 多播接口；in 为 XVarList(uint32_t interfaceIndex)，out 为 NULL。 */,
    XDeviceNetworkCommand_GetMulticastInterface
        /**< 获取 UDP 多播接口；in 为 NULL，out 为 XVarList(uint32_t* interfaceIndex)。 */,
    XDeviceNetworkCommand_ContinueAccept
        /**< 继续异步接受 TCP 连接；in/out 均为 NULL，仅监听设备可用。 */,
    XDeviceNetworkCommand_GetAcceptedSocket
        /**< 取出最近已接受连接；in 为 NULL，out 为 XVarList(XDeviceNetworkSocketHandle* handle, XHostAddress* address, uint16_t* port)，成功后句柄所有权转给调用方。 */,
    XDeviceNetworkCommand_CloseAcceptedSocket
        /**< 关闭未被接管的已接受连接；in 为 XVarList(XDeviceNetworkSocketHandle handle)，out 为 NULL，仅监听设备可用。 */,
    XDeviceNetworkCommand_Count
        /**< 网络命令数量；网络子类从此值继续编号，不可传给 XDevice_control。 */
} XDeviceNetworkCommand;

/** @brief XDeviceNetwork 类虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceNetwork)
XCLASS_DEFINE_EXTEND_END(XDeviceNetwork, XDevice)

/** @brief 网络设备类对象；基类必须是第一个成员。 */
typedef struct XDeviceNetwork
{
    XDevice m_base;
} XDeviceNetwork;

XVtable* XDeviceNetwork_class_init(void);
void XDeviceNetwork_init(XDeviceNetwork* self);
XDeviceNetwork* XDeviceNetwork_create(void);

/** @brief 注册类别名为 "socket" 的内置网络设备，函数可重复调用。 */
bool XDeviceNetwork_register(void);

#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif

#endif /* XDEVICENETWORK_H */
