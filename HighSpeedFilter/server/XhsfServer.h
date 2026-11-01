/**
 * @file       XhsfServer.h
 * @brief      高速过滤器服务端（模板）：TCP 文本协议 + IIR 低通滤波。
 * @details    XObject 派生类，内部持有 XTcpServer 监听与至多
 *             XHSF_SERVER_MAX_CLIENTS 路客户端连接；单线程事件循环、
 *             零锁。协议（'\n' 结尾的文本行）：
 *               d <浮点>  提交采样        → 应答 r <滤波后值>
 *               a <浮点>  设置平滑系数    → 应答 ok
 *               z         复位滤波器      → 应答 ok
 *             滤波算法：一阶 IIR 低通 y += alpha*(x-y)，首样直接播种 y=x。
 */
#ifndef XHSFSERVER_H
#define XHSFSERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XObject.h"
#include "XhsfProtocol.h"

typedef struct XTcpServer XTcpServer;
typedef struct XTcpSocket XTcpSocket;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XhsfServer)
XCLASS_DEFINE_EXTEND_END(XhsfServer, XObject)

/**
 * @brief 高速过滤器服务端结构体（继承 XObject；组合 XTcpServer）。
 * @details 成员禁止直接访问，一律经 API 操作。客户端套接字由服务端
 *          接管（断开后 deleteLater），调用方不得干预。
 */
typedef struct XhsfServer {
    XObject m_class;            /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    XTcpServer* m_listener;     /**< TCP 监听器（create 时创建，析构级联释放）。 */
    XTcpSocket* m_clients[XHSF_SERVER_MAX_CLIENTS]; /**< 客户端套接字槽位（NULL=空闲）。 */
    char m_rxBuf[XHSF_SERVER_MAX_CLIENTS][XHSF_SERVER_RXBUF_SIZE]; /**< 每连接行缓冲。 */
    int m_rxLen[XHSF_SERVER_MAX_CLIENTS]; /**< 行缓冲已积压字节数。 */
    double m_filterY;           /**< 滤波器当前输出 y。 */
    bool m_hasY;                /**< 是否已播种（首样 y=x）。 */
    double m_alpha;             /**< 平滑系数 (0..1]，默认 0.5。 */
    unsigned m_port;            /**< 实际监听端口（listen 成功后可经 port() 读取）。 */
} XhsfServer;

/* ==================== 类初始化 / 构造 ==================== */
XVtable* XhsfServer_class_init(void);
void XhsfServer_init(XhsfServer* srv);
XhsfServer* XhsfServer_create(void);

/* ==================== 启停 ==================== */
/**
 * @brief 监听指定端口（绑定 AllIPv4）。
 * @param srv 服务端实例；NULL 返回 false。
 * @param port 监听端口；0 表示由系统分配（listen 后经 port() 取实际值）。
 * @return true=监听成功（newConnection/断开事件全部内部接管）。
 */
bool XhsfServer_listen(XhsfServer* srv, unsigned port);

/**
 * @brief 读取实际监听端口。
 * @param srv 服务端实例；NULL 或未监听返回 0。
 * @return 监听端口。
 */
unsigned XhsfServer_port(const XhsfServer* srv);

/* ==================== 信号 ==================== */
/**
 * @brief 滤波输出信号：每个采样应答后发射（供进程内扩展挂接）。
 * @param srv 服务端实例；经 XSignal() 宏取地址连接，禁止直调。
 * @param y 滤波后的值。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XhsfServer_filtered_signal(XhsfServer* srv, double y);

#define XhsfServer_deleteLater XObject_deleteLater

#ifdef __cplusplus
}
#endif
#endif /* XHSFSERVER_H */
