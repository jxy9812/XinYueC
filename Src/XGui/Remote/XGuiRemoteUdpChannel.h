/**
 * @file       XGuiRemoteUdpChannel.h
 * @brief      XGuiRemote UDP 低延迟旁路通道(内部模块头, 非冻结契约)。
 * @details    会话内旁路加速通道(设计稿 out/mcgs-campaign/udp-design.md):
 *               - 载体划分: TCP=握手/认证/元数据/保活锚(不变); 本通道只承载
 *                 FB_UPDATE 帧 tile(S→C, 最新帧优先丢旧不追)与 INPUT_*
 *                 输入(C→S, 序号+64 槽重排+NACK 重传=可靠有序)与 UDP 心跳;
 *               - 数据报头 16B(魔数 'XGU1'/版本/通道/负载长/token/序号),
 *                 负载=与 TCP 帧完全相同的消息负载字节(零新解码器);
 *               - 收侧事件驱动: XUdpSocket bind 即在 XNetIoRing(io_uring/
 *                 epoll 双模式)挂 RECV, 就绪经 readyRead 信号即时处理,
 *                 无任何忙轮询(资源档 ~183ms RECV 唤醒节拍前科的规避纪律);
 *               - 发送为非阻塞 sendto 直用自有 fd(平台 UDP 发送缺陷绕开,
 *                 设计稿 §1; 不构成"第二对象接管描述符");
 *               - 单通道单绑定会话(token 身份凭据, 服务端多会话共享套接字,
 *                 第二会话 BIND 被拒 UDP_RESULT(0) 回落 TCP);
 *               - 全部 API 仅限 GUI 线程(与 XGuiServer/XGuiClient 泵同线程)。
 * @note       平台: sendto 直用段仅 __linux__ 编译; 其余平台通道恒不可用
 *             (serverBind/clientPrepare 返回 false, 上层静默纯 TCP)。
 * @author     XinYueC 团队
 */
#ifndef XGUIREMOTEUDPCHANNEL_H
#define XGUIREMOTEUDPCHANNEL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiRemoteProto.h"

#if XGUI_REMOTE_ON

/** @brief 通道对象(不透明; create_ex 分配, delete 释放)。 */
typedef struct XGuiRemoteUdpChannel XGuiRemoteUdpChannel;

/** @brief XObject 前向声明(套接字信号挂接视图; 完整定义见 XObject.h)。 */
typedef struct XObject XObject;

/** @brief 通道角色。 */
typedef enum XGuiRemoteUdpRole {
    XGUI_REMOTE_UDP_ROLE_SERVER = 0, /**< 服务端: 共享套接字, 逐 token 分帧 */
    XGUI_REMOTE_UDP_ROLE_CLIENT = 1  /**< 客户端: 点对点 */
} XGuiRemoteUdpRole;

/* 通道状态枚举 XGuiRemoteUdpState 见 XGuiRemoteProto.h(单一来源)。 */

/* ==================== 线上常量(设计稿 §4.0, 互操作依据) ==================== */

/** @brief 数据报头字节数: 魔数4+版本1+通道1+负载长2+token4+序号4。 */
#define XGUI_REMOTE_UDP_HEADER_BYTES   16
/** @brief 单数据报负载上限(16+8000 ≤ 8192=XNETWORK_READ_BUFFER_SIZE)。 */
#define XGUI_REMOTE_UDP_MAX_PAYLOAD    8000
/** @brief 数据报魔数 'X','G','U','1' 的 LE u32 助记(比较用 memcmp 语义常量)。 */
#define XGUI_REMOTE_UDP_DATAGRAM_MAGIC 0x31554758u
/** @brief 数据报通道 id。 */
#define XGUI_REMOTE_UDP_CHANNEL_FRAME  1 /**< FB_UPDATE 负载。 */
#define XGUI_REMOTE_UDP_CHANNEL_INPUT  2 /**< [u8 msgType][负载]。 */
#define XGUI_REMOTE_UDP_CHANNEL_CTRL   3 /**< 心跳/NACK。 */

/**
 * @brief 收到一帧业务负载(GUI 线程回调)。
 * @param channel XGUI_REMOTE_UDP_CHANNEL_*; INPUT 负载首字节为 msgType
 *                (已由通道剥出, msgType 参数携带, payload 不含该字节)。
 * @param seq     通道内序号(FRAME 通道丢旧去重依据)。
 */
typedef void (*XGuiRemoteUdpRecvFn)(void* user, uint8_t channel,
                                    uint8_t msgType, uint32_t seq,
                                    const uint8_t* payload, size_t len);
/** @brief 状态迁移回调(GUI 线程; state 为 XGuiRemoteUdpState)。 */
typedef void (*XGuiRemoteUdpStateFn)(void* user, int state);

/** @brief 创建配置(回调均借用, 生命周期须覆盖通道)。 */
typedef struct XGuiRemoteUdpChannelConfig {
    XGuiRemoteUdpRole    role;           /**< 角色。 */
    void*                user;           /**< 回调上下文(借用)。 */
    XGuiRemoteUdpRecvFn  onRecv;         /**< 业务负载回调(可 NULL)。 */
    XGuiRemoteUdpStateFn onStateChanged; /**< 状态迁移回调(可 NULL)。 */
} XGuiRemoteUdpChannelConfig;

/* ==================== 生命周期 ==================== */

XGuiRemoteUdpChannel* XGuiRemoteUdpChannel_create_ex(
        const XGuiRemoteUdpChannelConfig* config);
void XGuiRemoteUdpChannel_delete(XGuiRemoteUdpChannel* channel);

/* ==================== 套接字(建链前置) ==================== */

/**
 * @brief  服务端绑定 UDP 套接字(preferredPort 起 +1..+8 顺延试绑)。
 * @return true 绑定成功(*boundPortOut=实际端口); false 全占用/平台不支持
 *         (上层静默纯 TCP, 能力位不宣告)。
 */
bool XGuiRemoteUdpChannel_serverBind(XGuiRemoteUdpChannel* channel,
                                     uint16_t preferredPort,
                                     uint16_t* boundPortOut);

/**
 * @brief  客户端就绪发送套接字(绑临时口 + 记录服务器 host:port 为回送目标;
 *         RECV 由 bind 即刻在事件环挂起)。
 */
bool XGuiRemoteUdpChannel_clientPrepare(XGuiRemoteUdpChannel* channel,
                                        const char* hostUtf8,
                                        uint16_t port);

/** @brief 关闭套接字(运行期停用; 会话保持 TCP)。 */
void XGuiRemoteUdpChannel_closeSocket(XGuiRemoteUdpChannel* channel);

/** @brief 套接字是否已就绪(服务端=已绑定, 客户端=已预备)。 */
bool XGuiRemoteUdpChannel_socketReady(const XGuiRemoteUdpChannel* channel);

/** @brief 通道套接字的 XObject 视图(readyRead 信号挂接用; 借用,
 *         无套接字返回 NULL; 挂接纪律见 XGuiServer.c/XGuiClient.c)。 */
XObject* XGuiRemoteUdpChannel_socketObject(const XGuiRemoteUdpChannel* channel);

/* ==================== 会话绑定 ==================== */

/**
 * @brief  绑定会话(token 身份凭据; 服务端另需回送地址=TCP 对端 IP+自报口;
 *         客户端 peerAddrUtf8 传 NULL——目标已在 clientPrepare 记录)。
 * @note   重绑(断流重建)安全: 序号空间/重排缓存/重传缓存全部复位。
 */
void XGuiRemoteUdpChannel_bindSession(XGuiRemoteUdpChannel* channel,
                                      uint32_t token,
                                      const char* peerAddrUtf8,
                                      uint16_t peerUdpPort);

/** @brief 解除会话绑定(会话关闭时必须调用, 收回调自此不再触发)。 */
void XGuiRemoteUdpChannel_unbindSession(XGuiRemoteUdpChannel* channel);

/** @brief 登记会话上下文(服务端绑定会话时调用; 收回调 user 参数即此值,
 *         解绑/重绑须同步更新——悬垂防护纪律见 XGuiServer.c)。 */
void XGuiRemoteUdpChannel_setSessionUser(XGuiRemoteUdpChannel* channel,
                                         void* user);

/** @brief 是否已绑定会话。 */
bool XGuiRemoteUdpChannel_sessionBound(const XGuiRemoteUdpChannel* channel);

/** @brief 当前绑定会话的 token(未绑定为 0; 重绑请求比对用)。 */
uint32_t XGuiRemoteUdpChannel_sessionToken(const XGuiRemoteUdpChannel* channel);

/* ==================== 数据面 ==================== */

/** @brief 激活/停用数据面(停用=帧/输入回落 TCP; 心跳仍发, NAT 保活)。 */
void XGuiRemoteUdpChannel_setActive(XGuiRemoteUdpChannel* channel, bool active);
/** @brief 当前状态。 */
int XGuiRemoteUdpChannel_state(const XGuiRemoteUdpChannel* channel);

/**
 * @brief  发一帧 FB_UPDATE 负载(FRAME 通道; seq 自增; 最新帧优先语义,
 *         EAGAIN/ENOBUFS 丢帧不重试)。负载须 ≤ XGUI_REMOTE_UDP_MAX_PAYLOAD。
 * @param  nowMs 单调毫秒(帧活性统计; XGuiServer 侧 xgs_nowMs 口径)。
 * @return true 已递交内核(含主动丢弃); false 未绑定/未激活/超长。
 */
bool XGuiRemoteUdpChannel_sendFrame(XGuiRemoteUdpChannel* channel,
                                    const uint8_t* payload, size_t len,
                                    int64_t nowMs);

/**
 * @brief  发一条输入负载(INPUT 通道; 负载首字节=msgType; seq 自增并
 *         入 64 帧重传缓存供 NACK 补发)。
 * @return true 已递交内核(发送失败帧不入序号空间); false 未绑定/未激活。
 */
bool XGuiRemoteUdpChannel_sendInput(XGuiRemoteUdpChannel* channel,
                                    const uint8_t* payload, size_t len);

/** @brief NACK 应答: 重传缓存中 [firstSeq, firstSeq+count) 的输入帧。 */
void XGuiRemoteUdpChannel_nackRetransmit(XGuiRemoteUdpChannel* channel,
                                         uint32_t firstSeq, uint16_t count);

/* ==================== 周期处理 ==================== */

/**
 * @brief  泵节拍调用(收兜底排空 + 心跳 + NACK 重发 + 帧活性统计)。
 * @param  nowMs 单调毫秒(调用方时钟, 全库统一 XDateTime 口径)。
 */
void XGuiRemoteUdpChannel_tick(XGuiRemoteUdpChannel* channel, int64_t nowMs);

/** @brief 立即排空收侧(readyRead 直连槽调用; tick 内部亦调用)。 */
void XGuiRemoteUdpChannel_drain(XGuiRemoteUdpChannel* channel, int64_t nowMs);

/* ==================== [wake] 事件循环响应延迟探针(内部共享, 诊断) ========
 * env XGUI_REMOTE_WAKE_PROF=1 门控(关闭恒 0/空操作, 热路径零成本)。
 * 三段口径(设计稿 out/mcgs-campaign/udp-design.md「事件循环唤醒延迟」):
 * 环层两段见 [wake][ring] 行(XNetIoRingPosix.c); 消息层第三段
 * 「读完成→解析派发」由两端在派发点采样, 每 5s 每标签汇总一行
 * [wake][tag] n=.. p50=..us p95=..us max=..us over5ms=..。 */

/** @brief 探针是否开启(两端打点判据; 关闭时采样点直接短路)。 */
bool XGuiRemoteUdp_wakeProfOn(void);

/**
 * @brief  探针 5s 窗口到点检查(两端泵回调每圈调用)。
 * @note   与采样解耦: 流量停止后末窗汇总仍能落盘(纯 TCP 无通道、tick
 *         不可达时亦有效)。探针关闭时空操作。
 */
void XGuiRemoteUdp_wakeProfPoll(void);

/**
 * @brief  逐消息采样(派发完成后调用)。
 * @param  tag       汇总标签("srv-tcp"/"srv-udp"/"cli-tcp"/"cli-udp")。
 * @param  arriveUs  该消息载体最近一次读完成时刻
 *                    (XAbstractNetIoRing_profLastRecvUs; 0=无记录跳过)。
 */
void XGuiRemoteUdp_wakeProfSample(const char* tag, uint64_t arriveUs);

/**
 * @brief  通道收侧最近一次读完成时刻(探针用; 未开探针/无记录=0)。
 * @note   封装通道自有 fd 的 XAbstractNetIoRing_profLastRecvUs 查询,
 *         fd 不外泄(发送直用 fd 的所有权纪律不变)。
 */
uint64_t XGuiRemoteUdpChannel_probeArrivalUs(
        const XGuiRemoteUdpChannel* channel);

/* ==================== [stage] fb→镜像腿分段计时探针(诊断) ==============
 * env XGUI_REMOTE_STAGE_PROF=1 门控(关闭恒 false/0/空操作, 热路径零成本)。
 * 模式与落盘风格同 [wake]: 打点判据 stageProfOn, 每 5s 每标签汇总一行
 * [stage][tag] n=.. p50=..us p95=..us max=..us(stderr, pump 回调驱动)。
 * 分段口径见 XGuiRemoteUdpChannel.c 探针节注; 跨线程段经单调钟戳传递
 * (同进程 CLOCK_MONOTONIC 同轴), 跨机器不做减法。 */

/** @brief 探针是否开启(两端打点判据; 关闭时打点恒 0/跳过)。 */
bool XGuiRemoteUdp_stageProfOn(void);

/** @brief 打点时刻(µs, 单调钟; 关闭恒 0——调用方存 0 戳即零成本)。 */
uint64_t XGuiRemoteUdp_stageProfNowUs(void);

/**
 * @brief  跨点段采样: 段长=t1Us−t0Us(同进程单调钟; 0 戳/逆序跳过)。
 * @param  tag  汇总标签("srv-cap"/"srv-lat"/"srv-enc"/"srv-q"/
 *              "srv-send"/"srv-in2fb"/"cli-dec"/"cli-merg"/"cli-paint"/
 *              "cli-blit"; 静态字符串常量)。
 */
void XGuiRemoteUdp_stageProfSpan(const char* tag, uint64_t t0Us,
                                 uint64_t t1Us);

/** @brief 单段时长便捷采样(t0=stageProfNowUs 打点; 关闭恒空操作)。 */
void XGuiRemoteUdp_stageProfSample(const char* tag, uint64_t t0Us);

/**
 * @brief  字节数累计(诊断, 与同名标签的时长样本同窗汇总)。
 * @param  tag  标签(静态字符串常量, 与 stageProfSpan 同一槽位表)。
 * @param  n    本次事件字节数(采集/编码/收发各挂点按事件粒度调用)。
 * @note   每 5s 汇总行以 bytes=<合计> avg=<均值/事件> 追加输出, 供账本
 *         报线缆负载与压缩比; 探针关闭恒空操作。同名标签须先/同时有
 *         时长样本才落行(槽位由 span/sample 创建, bytes 仅累加)。
 */
void XGuiRemoteUdp_stageProfBytes(const char* tag, uint32_t n);

/**
 * @brief  探针 5s 窗口到点检查(两端 pump 回调每圈调用)。
 * @note   与采样解耦: 流量停止后末窗汇总仍能落盘。探针关闭时空操作。
 */
void XGuiRemoteUdp_stageProfPoll(void);

/* ==================== 查询(显示/测试断言口径) ==================== */

/** @brief 本地 UDP 端口(未就绪 0)。 */
uint16_t XGuiRemoteUdpChannel_localPort(const XGuiRemoteUdpChannel* channel);
/** @brief 距最近一次合法数据报的毫秒数(从未收到=INT64_MAX)。 */
int64_t XGuiRemoteUdpChannel_msSinceRecv(const XGuiRemoteUdpChannel* channel,
                                         int64_t nowMs);
/** @brief 服务端侧: 距最近一帧 FRAME 发出的毫秒数(CTRL-PONG 携带)。 */
uint32_t XGuiRemoteUdpChannel_msSinceFrameSend(
        const XGuiRemoteUdpChannel* channel, int64_t nowMs);
/** @brief 客户端侧: 最近 PONG 携带的服务端帧时差(0xFFFFFFFF=未测得;
 *         小值+本端 FRAME 长期静默 ⇒ 单向丢帧, 触发全量刷新恢复)。 */
uint32_t XGuiRemoteUdpChannel_pongFrameMsAgo(
        const XGuiRemoteUdpChannel* channel);
/** @brief 累计发出数据报数。 */
uint64_t XGuiRemoteUdpChannel_txDatagrams(const XGuiRemoteUdpChannel* channel);
/** @brief 累计收下(合法 token)数据报数。 */
uint64_t XGuiRemoteUdpChannel_rxDatagrams(const XGuiRemoteUdpChannel* channel);
/** @brief 累计丢弃输入数(序号跳进/缓存逐出; LAN 常态为 0)。 */
uint64_t XGuiRemoteUdpChannel_droppedInputs(
        const XGuiRemoteUdpChannel* channel);

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUIREMOTEUDPCHANNEL_H */
