/**
 * @file        XFtp.h
 * @brief       FTP 客户端实现（对标 Qt 6.8 QFtp），基于 XinYueC 信号/槽系统
 * @note        跨平台：Windows / Linux / 嵌入式
 *              特性：主动/被动模式、Explicit FTPS (RFC 4217)、REST 断点续传、
 *                    APPE 追加、MODE Z 压缩、SOCKS5/HTTP 代理、自动重连、URL 解析
 */

#ifndef XFTP_H
#define XFTP_H

#include "CXinYueConfig.h"
#include "XClass.h"
#include "XVector.h"
#include "XMutex.h"
#include "XByteArray.h"
#include "XString.h"
#include "XAbstractSocket.h"
#include "XSslSocket.h"
#include "XIODevice.h"
#include "XFileInfo.h"
#include "XTcpServer.h"
#include "XHostAddress.h"
#include "XFtpCommand.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============== 条件编译 ===============
// FTP 模块：依赖 XNETWORK_ON；通过 XNETWORK_FTP_ON 开关
// 注意：默认启用以保证编译通过；可通过 #define XNETWORK_FTP_ON 显式控制
#if defined(XNETWORK_ON) || 1

// =============== 枚举定义 ===============

/**
 * @brief FTP 客户端状态
 */
typedef enum XFtp_State {
    XFtp_State_Unconnected = 0,    ///< 未连接
    XFtp_State_Connecting,         ///< 正在连接
    XFtp_State_Connected,          ///< 已连接（未登录）
    XFtp_State_LoggedIn,           ///< 已登录
    XFtp_State_Closing             ///< 正在关闭
} XFtp_State;

/**
 * @brief FTP 错误码（对标 Qt 6.8 QFtp::Error 完整 0-21）
 */
typedef enum XFtp_Error {
    XFtp_Error_NoError = 0,            ///< 无错误
    XFtp_Error_Unknown,                ///< 未知错误
    XFtp_Error_HostNotFound,           ///< 找不到主机
    XFtp_Error_ConnectionRefused,      ///< 连接被拒绝
    XFtp_Error_NotConnected,           ///< 未连接
    XFtp_Error_AlreadyConnected,       ///< 已连接
    XFtp_Error_Timeout,                ///< 超时
    XFtp_Error_NetworkError,           ///< 网络错误
    XFtp_Error_ProtocolError,          ///< 协议错误
    XFtp_Error_AuthenticationError,    ///< 鉴权失败（4xx/5xx 响应 5xx）
    XFtp_Error_SslHandshakeFailed,     ///< SSL/TLS 握手失败
    XFtp_Error_ProxyConnectionRefused, ///< 代理连接被拒绝
    XFtp_Error_ProxyAuthenticationRequired, ///< 代理鉴权失败
    XFtp_Error_PassiveModeFailed,      ///< PASV 响应解析失败
    XFtp_Error_ActiveModeFailed,       ///< PORT 绑定失败
    XFtp_Error_TransferAborted,        ///< 传输被用户中断 (ABOR)
    XFtp_Error_TransferFailed,         ///< 数据传输失败
    XFtp_Error_InvalidResponse,        ///< 服务器响应格式无效
    XFtp_Error_CommandFailed,          ///< FTP 命令执行失败
    XFtp_Error_DirectoryListingFailed, ///< 目录列表解析失败
    XFtp_Error_NotLoggedIn,            ///< 操作前未登录
    XFtp_Error_OperationInProgress     ///< 已有命令在执行
} XFtp_Error;

/**
 * @brief 数据传输模式
 */
typedef enum XFtp_TransferType {
    XFtp_TransferType_Passive = 0, ///< 被动模式（PASV，服务器监听）
    XFtp_TransferType_Active       ///< 主动模式（PORT，客户端监听）
} XFtp_TransferMode;

#define XFtp_TransferMode_Passive XFtp_TransferType_Passive
#define XFtp_TransferMode_Active  XFtp_TransferType_Active

/**
 * @brief 数据传输类型（TYPE 命令：二进制 / ASCII）
 */
typedef enum XFtp_DataType {
    XFtp_DataType_Binary = 0,   ///< 二进制（TYPE I）
    XFtp_DataType_Ascii         ///< ASCII（TYPE A）
} XFtp_DataType;

/**
 * @brief 代理类型
 */
typedef enum XFtp_ProxyType {
    XFtp_ProxyType_None = 0,        ///< 无代理（直连）
    XFtp_ProxyType_Http,            ///< HTTP 代理（RFC 2616 CONNECT）
    XFtp_ProxyType_Socks5            ///< SOCKS5 代理（RFC 1928）
} XFtp_ProxyType;

// =============== 私有数据声明 ===============
typedef struct XFtpPrivate XFtpPrivate;
typedef struct XTimer XTimer;

// =============== 主结构体 ===============

/**
 * @brief FTP 特性位（FEAT 协商结果，位域节省空间）
 */
typedef enum XFtp_Feature {
    XFtp_Feature_UTF8     = 0x01,    ///< OPTS UTF8 ON 支持
    XFtp_Feature_MLSD     = 0x02,    ///< MLSD/MLST 机器可读列表
    XFtp_Feature_MODEZ    = 0x04,    ///< MODE Z 传输压缩
    XFtp_Feature_EPSV     = 0x08,    ///< EPSV/EPRT 扩展被动模式
    XFtp_Feature_REST_STREAM = 0x10, ///< REST + STREAM 字节偏移
    XFtp_Feature_TVFS     = 0x20,    ///< TVFS 文本虚拟文件系统
    XFtp_Feature_ABOR     = 0x40,    ///< ABOR 中断传输
    XFtp_Feature_SIZE     = 0x80     ///< SIZE 文件大小
} XFtp_Feature;

/**
 * @brief FTP 客户端对象（嵌入式优化：8字节指针优先 + 位域 + 排序）
 */
typedef struct XFtp {
    XObject m_class;                      // 基类（必须第一位）

    // ===== 8 字节指针区 =====
    XFtpPrivate* m_d;                     // 私有数据
    XAbstractSocket* m_piSocket;          // 控制连接 socket
    XAbstractSocket* m_dtpSocket;         // 数据连接 socket
    XByteArray* m_piBuffer;               // 控制连接读取缓冲
    XByteArray* m_dtpBuffer;              // 数据连接读取缓冲
    XByteArray* m_readBuffer;             // 数据读取缓冲
    XTcpServer* m_dtpServer;              // 数据连接 server（主动模式用）
    XVector* m_pendingCommands;           // 待执行的命令
    XVector* m_listInfo;                  // 目录列表信息
    XMutex* m_commandMutex;               // 命令队列互斥锁
    XTimer* m_reconnectTimer;             // 自动重连定时器
    XFtpCommand* m_currentCommand;        // 当前执行的命令
    void* m_transferDevice;               // 传输设备
    XString* m_errorString;               // 错误信息
    XString* m_host;
    XString* m_user;
    XString* m_password;
    XString* m_proxyHost;                 // 代理主机
    XString* m_proxyUser;                 // 代理用户名
    XString* m_proxyPass;                 // 代理密码

    // ===== 8 字节 int64 区 =====
    int64_t m_transferTotal;              // 传输总字节数
    int64_t m_transferCurrent;            // 当前已传输字节数
    int64_t m_restOffset;                 // REST 断点续传偏移

    // ===== 4 字节 enum/int 区 =====
    XFtp_State m_state;                   // 当前状态
    XFtp_Error m_error;                   // 错误码
    XFtp_TransferMode m_transferMode;     // 传输模式
    XFtp_DataType m_transferType;         // 传输类型（Binary/Ascii）
    XFtp_ProxyType m_proxyType;           // 代理类型
    XSslPeerVerifyMode m_sslPeerVerifyMode; // FTPS 对端证书校验模式
    int m_currentId;                      // 当前命令ID
    int m_reconnectInterval;              // 重试间隔（毫秒）
    int m_maxReconnectAttempts;           // 最大重试次数
    int m_reconnectAttempts;              // 当前已重试次数

    // ===== 2 字节区 =====
    uint16_t m_proxyPort;                 // 代理端口
    uint16_t m_port;                      // FTP 端口

    // ===== 1 字节位域（特性协商、SSL、压缩、ABOR 等开关） =====
    uint8_t m_features;                   // FEAT 协商结果（XFtp_Feature 位掩码）
    uint8_t m_useSsl            : 1;      // 是否启用 SSL/TLS
    uint8_t m_useCompression    : 1;      // 是否启用 MODE Z
    uint8_t m_compressionActive : 1;      // MODE Z 握手成功
    uint8_t m_autoReconnect     : 1;      // 是否自动重连
    uint8_t m_useUtf8           : 1;      // UTF8 模式（OPTS UTF8 ON）
    uint8_t m_abortRequested    : 1;      // 用户发起 ABOR 中断传输
    uint8_t m_useMlsd           : 1;      // 优先用 MLSD 而非 LIST
    uint8_t m_dirListingActive  : 1;      // 当前正在列目录（listInfo 流式输出中）
} XFtp;

// =============== 构造/析构 ===============
XVtable* XFtp_class_init(void);
/**
 * @brief 初始化 XFtp 对象（栈上使用）
 */
void XFtp_init(XFtp* ftp);

/**
 * @brief 反初始化 XFtp 对象（释放资源，不释放自身内存）
 */
void XFtp_deinit_base(XFtp* ftp);

/**
 * @brief 创建 FTP 客户端实例
 */
XFtp* XFtp_create(void);

/**
 * @brief 销毁 FTP 客户端实例
 */
void XFtp_delete(XFtp* ftp);

// =============== 连接管理 ===============

/**
 * @brief 连接到 FTP 服务器
 * @param[in,out] ftp   FTP 实例
 * @param[in]     host  主机名或 IP
 * @param[in]     port  端口（0=默认 21）
 * @return 命令ID，失败返回 -1
 */
int XFtp_connectToHost(XFtp* ftp, const char* host, uint16_t port);

/**
 * @brief 用 URL 一次性连接、登录、切目录
 * @param[in,out] ftp  FTP 实例
 * @param[in]     url  FTP URL，如 ftp://user:pass@host:port/path
 * @return 命令ID，失败返回 -1
 */
int XFtp_connectToUrl(XFtp* ftp, const char* url);

/**
 * @brief 登录
 * @return 命令ID
 */
int XFtp_login(XFtp* ftp, const char* user, const char* password);

/**
 * @brief 关闭连接
 * @return 命令ID
 */
int XFtp_close(XFtp* ftp);

/**
 * @brief 中止所有挂起命令
 */
void XFtp_abort(XFtp* ftp);

// =============== 文件操作 ===============

/**
 * @brief 列出目录
 * @param[in] dir  目录路径，NULL 表示当前目录
 * @return 命令ID
 */
int XFtp_list(XFtp* ftp, const char* dir);

/**
 * @brief 下载文件
 * @param[in] file     远程文件路径
 * @param[in] device   本地保存设备（XFile* 等可写设备）
 * @param[in] type     传输类型（XIODevice_Append / Truncate 等）
 * @return 命令ID
 */
int XFtp_get(XFtp* ftp, const char* file, void* device, int type);

/**
 * @brief 断点续传下载
 * @param[in] file     远程文件路径
 * @param[in] device   本地保存设备
 * @param[in] offset   偏移字节
 * @param[in] type     传输类型
 * @return 命令ID
 */
int XFtp_get_resume(XFtp* ftp, const char* file, void* device, int64_t offset, int type);

/**
 * @brief 上传文件
 * @param[in] file     远程文件路径
 * @param[in] data     数据缓冲
 * @param[in] size     数据字节数
 * @return 命令ID
 */
int XFtp_put(XFtp* ftp, const char* file, const void* data, int64_t size);

/**
 * @brief 追加上传
 */
int XFtp_put_append(XFtp* ftp, const char* file, const void* data, int64_t size);

/**
 * @brief 删除远程文件
 */
int XFtp_remove(XFtp* ftp, const char* file);

/**
 * @brief 重命名/移动远程文件
 */
int XFtp_rename(XFtp* ftp, const char* oldname, const char* newname);

/**
 * @brief 创建远程目录
 */
int XFtp_mkdir(XFtp* ftp, const char* dir);

/**
 * @brief 删除远程目录
 */
int XFtp_rmdir(XFtp* ftp, const char* dir);

/**
 * @brief 切换工作目录
 */
int XFtp_cd(XFtp* ftp, const char* dir);

/**
 * @brief 获取当前工作目录
 */
int XFtp_cdup(XFtp* ftp);

/**
 * @brief 查询文件大小（SIZE 命令，RFC 3659）
 * @param[in] file  远程文件路径
 * @return 命令ID，命令完成后通过 rawCommandReply 信号携带 "213 <size>"
 *         或 commandFinished(id, error=true) 表示失败
 */
int XFtp_size(XFtp* ftp, const char* file);

/**
 * @brief 查询文件修改时间（MDTM 命令，RFC 3659）
 * @param[in] file  远程文件路径
 * @return 命令ID，命令完成后通过 rawCommandReply 信号携带 "213 YYYYMMDDHHMMSS"
 */
int XFtp_mdtm(XFtp* ftp, const char* file);

/**
 * @brief 查询单文件元信息（MLST 命令，RFC 3659）
 * @param[in] file  远程文件路径（NULL = 当前路径）
 * @return 命令ID，命令完成后通过 listInfo 信号发射 XFileInfo
 */
int XFtp_mlst(XFtp* ftp, const char* file);

/**
 * @brief 发送原始命令
 * @param[in] command  命令字符串（自动追加 \r\n）
 * @return 命令ID
 */
int XFtp_rawCommand(XFtp* ftp, const char* command);

// =============== 状态/配置 ===============

XFtp_State XFtp_state(const XFtp* ftp);
XFtp_Error XFtp_error(const XFtp* ftp);
const char* XFtp_errorString(const XFtp* ftp);
int XFtp_currentId(const XFtp* ftp);
XFtpCommand_Type XFtp_currentCommand(const XFtp* ftp);
bool XFtp_hasPendingCommands(const XFtp* ftp);
XFtp_TransferMode XFtp_transferMode(const XFtp* ftp);

void XFtp_setTransferMode(XFtp* ftp, XFtp_TransferMode mode);
XFtp_DataType XFtp_transferType(const XFtp* ftp);
void XFtp_setTransferType(XFtp* ftp, XFtp_DataType type);

/**
 * @brief 启用 UTF8 模式（自动在登录后发送 OPTS UTF8 ON）
 */
void XFtp_setUtf8(XFtp* ftp, bool enabled);
bool XFtp_isUtf8(const XFtp* ftp);

/**
 * @brief 启用 MLSD 优先（默认开启；服务器不支持时回退 LIST）
 */
void XFtp_setMlsdEnabled(XFtp* ftp, bool enabled);

/**
 * @brief 查询 FEAT 协商后某特性是否支持
 */
bool XFtp_supportsFeature(const XFtp* ftp, XFtp_Feature feature);

/**
 * @brief 主动中断当前传输（发送 ABOR）
 * @note 调用后数据传输立即中止，触发 commandFinished(error=true)
 */
void XFtp_abortTransfer(XFtp* ftp);

/**
 * @brief 设置 SSL/TLS (Explicit FTPS, RFC 4217)
 * @note 仅在未连接时可设置
 */
void XFtp_setSsl(XFtp* ftp, bool useSsl);

/**
 * @brief 设置 FTPS 对端证书校验模式。
 * @param[in,out] ftp  FTP 实例；必须处于未连接状态
 * @param[in] mode     对端证书校验模式，默认值为 XSSL_VerifyPeer
 * @note 生产环境应保持 XSSL_VerifyPeer；测试自签名证书时可临时使用
 *       XSSL_VerifyNone，但这不会验证服务器身份。
 */
void XFtp_setSslPeerVerifyMode(XFtp* ftp, XSslPeerVerifyMode mode);

/**
 * @brief 获取 FTPS 对端证书校验模式。
 * @param[in] ftp FTP 实例；NULL 返回 XSSL_VerifyPeer
 * @return 当前校验模式
 */
XSslPeerVerifyMode XFtp_sslPeerVerifyMode(const XFtp* ftp);

/**
 * @brief 设置 MODE Z 压缩
 * @note 仅在未连接时可设置
 */
void XFtp_setCompression(XFtp* ftp, bool useCompression);

/**
 * @brief 设置自动重连
 * @param[in] autoReconnect  是否启用
 * @param[in] intervalMs     重试间隔（毫秒）
 * @param[in] maxAttempts    最大重试次数（0=无限）
 */
void XFtp_setAutoReconnect(XFtp* ftp, bool autoReconnect, int intervalMs, int maxAttempts);

/**
 * @brief 设置 HTTP 代理
 */
void XFtp_setProxy(XFtp* ftp, const char* host, uint16_t port);

/**
 * @brief 设置 SOCKS5 代理
 * @param[in] user     NULL 表示匿名
 * @param[in] password NULL 表示无密码
 */
void XFtp_setSocks5Proxy(XFtp* ftp, const char* host, uint16_t port,
                         const char* user, const char* password);

/**
 * @brief 清除代理设置（恢复直连）
 */
void XFtp_clearProxy(XFtp* ftp);

// =============== 信号 ===============

/**
 * 7 个信号：
 *  - stateChanged(int newState)            状态变化
 *  - commandStarted(int id)                 命令开始
 *  - commandFinished(int id, bool error)    命令完成
 *  - listInfo(XFileInfo* info)              目录列表项
 *  - readyRead()                            数据可读
 *  - dataTransferProgress(int64_t done, int64_t total)  传输进度
 *  - rawCommandReply(int code, const char* reply)  原始响应
 *  - done(bool error)                       所有命令完成
 */
void* XFtp_stateChanged_signal(XFtp* ftp, XFtp_State state);
void* XFtp_commandStarted_signal(XFtp* ftp, int id);
void* XFtp_commandFinished_signal(XFtp* ftp, int id, bool error);
void* XFtp_listInfo_signal(XFtp* ftp, XFileInfo* info);
void* XFtp_readyRead_signal(XFtp* ftp);
void* XFtp_dataTransferProgress_signal(XFtp* ftp, int64_t current, int64_t total);
void* XFtp_rawCommandReply_signal(XFtp* ftp, int code, const char* reply);
void* XFtp_done_signal(XFtp* ftp, bool error);

#endif // XNETWORK_ON && XNETWORK_FTP_ON

#ifdef __cplusplus
}
#endif

#endif // XFTP_H
