#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H
#include"XMemory.h"
#include "XNetwork_config.h"   /* 获取 XNETWORK_LWIP_NO_SYS 等宏开关 */

/**
 * @file lwipopts.h
 * @brief lwIP 协议栈配置选项（XinYueC 移植版）
 *
 * XinYueC 组件替换：
 *   - 内存: XMalloc_Hybrid（静态池+系统堆混合分配）/ XFree_Hybrid
 *   - 定时器: XTimeWheelGroup（事件驱动时间轮，替代 sys_check_timeouts 轮询）
 *   - 随机数: XRandomGenerator_system()
 *   - 线程/同步: XThread / XMutex / XSemaphore（sys_arch.c）
 *   - lwIP 内部调试: printf + fflush
 *   - 适配层调试: LWIP_DBG 宏（XPrintf），LWIP_NET_DEBUG=0 一键关闭
 */

/* ================================================================
 * 调试开关
 * ================================================================ */

/* 适配层调试总开关：1=开启 XPrintf 输出，0=静默（不影响 lwIP 内部 LWIP_DEBUG） */
#define LWIP_NET_DEBUG            0
#if LWIP_NET_DEBUG
  /* 适配层自定义调试输出，用于 XNetwork_lwip.c / sys_arch.c 等 */
  #define LWIP_DBG(fmt, ...)  XPrintf(fmt, ##__VA_ARGS__)
#else
  #define LWIP_DBG(fmt, ...)
#endif

/* ================================================================
 * 操作系统模式
 * ================================================================ */

/* NO_SYS 受 XNETWORK_LWIP_NO_SYS 控制（见 XNetwork_config.h）
 *   0 = OS 模式：完整 sys_arch（线程/信号量/邮箱/互斥锁），lwIP 内部创建 tcpip_thread
 *   1 = 裸机模式：最小 sys_arch（仅 sys_arch_protect / sys_now 等），无 tcpip_thread
 * 裸机模式下需手动调用 lwip_init() 和 XNetworkLwip_ensureNetworkReady() */
#define NO_SYS                  1 //当前架构始终裸机模式效率最高

/* ================================================================
 * 协议栈功能开关
 * ================================================================ */

/* TCP 协议支持：1=启用（HTTP/HTTPS/Telnet 等需要） */
#define LWIP_TCP                1

/* UDP 协议支持：1=启用（DNS/DHCP/SNMP 等需要） */
#define LWIP_UDP                1

/* ICMP 协议支持：1=启用（Ping/目的不可达等） */
#define LWIP_ICMP               1

/* DHCP 客户端：1=启用（自动获取 IP/网关/DNS） */
#define LWIP_DHCP               1

/* DHCP 获取 IP 前是否做 ACD（地址冲突检测）：0=关闭（Windows/Npcap 环境无需） */
#define LWIP_DHCP_DOES_ACD_CHECK   0

/* ACD（Address Conflict Detection，地址冲突检测）：0=关闭 */
#define LWIP_ACD                   0

/* IPv4 协议栈支持：1=启用 */
#define LWIP_IPV4               1

/* IPv6 协议栈支持：由 XNETWORK_LWIP_IPV6 控制（默认 0=关闭） */
#define LWIP_IPV6               XNETWORK_LWIP_IPV6

/* IGMP（Internet 组管理协议）：由 XNETWORK_LWIP_IGMP 控制（组播需要） */
#define LWIP_IGMP               XNETWORK_LWIP_IGMP

/* AUTOIP（自动私有 IP 分配，169.254.x.x）：0=关闭（与 DHCP 冲突时优先 DHCP） */
#define LWIP_AUTOIP             0

/* DNS 客户端：1=启用（域名解析，XHostInfo 依赖） */
#define LWIP_DNS                1

/* IPv6 分片重组时复制头部：64 位平台必须为 1
 * 原因：ip6_reass_helper 指针大小 > IP6_FRAG_HLEN，需拷贝而非原地操作 */
#define IPV6_FRAG_COPYHEADER    1

/* ================================================================
 * 内存管理
 * ================================================================ */

/* 启用自定义内存分配器：1=使用 MEM_CUSTOM_MALLOC/FREE/CALLOC 替代 lwIP 默认堆 */
#define MEM_CUSTOM_ALLOCATOR    1

/* 自定义 malloc：使用 XMalloc_Hybrid（小对象走静态池，大对象走系统堆，减少碎片） */
#define MEM_CUSTOM_MALLOC       XMalloc_Hybrid

/* 自定义 free：与 XMalloc_Hybrid 配对 */
#define MEM_CUSTOM_FREE         XFree_Hybrid

/* 自定义 calloc：零初始化分配 */
#define MEM_CUSTOM_CALLOC       XCalloc_Hybrid

/* 内存池通过 malloc 分配：1=所有 MEMP_xxx 池由 MEM_CUSTOM_MALLOC 动态分配
 * 优点：无需预计算池大小，灵活性高；缺点：略有分配开销 */
#define MEMP_MEM_MALLOC         1

/* 内存对齐字节数：4 字节对齐（ARM/x86 通用） */
#define MEM_ALIGNMENT           4

/* lwIP 内部堆大小（字节）：128KB，供 pbuf/TCP 段等动态分配
 * 当 MEMP_MEM_MALLOC=1 时，内存池也从该堆分配 */
#define MEM_SIZE                131072

/* ================================================================
 * 内存池数量（MEMP_NUM_xxx）
 * 每个池预分配固定数量的对象，耗尽后对应功能无法创建新实例
 * ================================================================ */

/* 空白 pbuf 数量（仅含 pbuf 头部，无数据区，用于链表拼接） */
#define MEMP_NUM_PBUF           64

/* UDP PCB（协议控制块）数量：每个 UDP 套接字占用 1 个 */
#define MEMP_NUM_UDP_PCB        8

/* TCP PCB 数量：每个活跃 TCP 连接占用 1 个 */
#define MEMP_NUM_TCP_PCB        8

/* TCP 监听 PCB 数量：每个 TCP Server 占用 1 个 */
#define MEMP_NUM_TCP_PCB_LISTEN 4

/* TCP 段数量：用于发送/接收缓冲中的数据分片
 * 经验值：≥ 2 × TCP_SND_QUEUELEN，当前 32 */
#define MEMP_NUM_TCP_SEG        32

/* 系统定时器数量：lwIP 内部 sys_timeout 槽位数
 * 注意：LWIP_TIMERS_CUSTOM=1 后定时器走 XTimeWheelGroup，
 *       此值仅影响 lwIP 内部 memp 池预分配，设 12 足够 */
#define MEMP_NUM_SYS_TIMEOUT    12

/* netbuf 数量：Netconn API 的网络缓冲（当前关闭 Netconn，保留备用） */
#define MEMP_NUM_NETBUF         8

/* netconn 数量：Netconn API 连接对象（当前关闭 Netconn，保留备用） */
#define MEMP_NUM_NETCONN        4

/* ================================================================
 * 线程与同步
 * ================================================================ */

/* TCPIP 核心锁：允许直接持有锁操作 Raw API，无需通过邮箱发消息
 *   NO_SYS=1 时必须为 0（无 tcpip_thread，无法锁定）
 *   NO_SYS=0 时为 1（tcpip_thread 存在，支持核心锁定） */
#if NO_SYS
#define LWIP_TCPIP_CORE_LOCKING 0
#else
#define LWIP_TCPIP_CORE_LOCKING 1
#endif

/* 轻量级临界区保护：1=提供 sys_arch_protect/sys_arch_unprotect（递归锁）
 *   0=不提供，适用于 NO_SYS=1 单线程架构
 * 当前架构：NO_SYS=1 + LWIP_TIMERS_CUSTOM=1，所有 lwIP 操作在主线程
 * （定时器回调、pcap 轮询、Socket 操作均在主线程事件循环），无并发访问，可安全关闭
 * 关闭后 lwIP 内部 SYS_ARCH_PROTECT/UNPROTECT 宏为空操作，零开销 */
#define SYS_LIGHTWEIGHT_PROT    0

/* ================================================================
 * 协议栈参数
 * ================================================================ */

/* TCP 最大报文段长度（字节）：1460 = MTU(1500) - IP头(20) - TCP头(20)
 * 以太网标准值，不要随意修改 */
#define TCP_MSS                 1460

/* TCP 接收窗口大小：8 × MSS = 11680 字节
 * 值越大吞吐越高，但占用内存越多 */
#define TCP_WND                 (8 * TCP_MSS)

/* TCP 发送缓冲区大小：8 × MSS = 11680 字节
 * 应用层单次最多可写入此大小的数据 */
#define TCP_SND_BUF             (8 * TCP_MSS)

/* TCP 发送队列长度：32 个段
 * 控制 lwIP 内部待发送/未确认的段数量 */
#define TCP_SND_QUEUELEN        32

/* TCP 最大重传次数：6 次（超过后连接超时断开） */
#define TCP_MAXRTX              6

/* TCP SYN 最大重传次数：4 次（连接建立阶段超时） */
#define TCP_SYNMAXRTX           4

/* IP 分片重组：1=启用（接收被分片的 IP 包并重组） */
#define IP_REASSEMBLY           1

/* IP 分片发送：0=关闭（发送大于 MTU 的包时直接丢弃，不主动分片） */
#define IP_FRAG                 0

/* ARP 缓存表大小：16 个表项（每项对应一个已知 MAC 地址） */
#define ARP_TABLE_SIZE          16

/* ARP 表项最大存活时间（秒）：300 秒后老化，需重新 ARP 请求 */
#define ARP_MAXAGE              300

/* ARP 协议支持：1=启用（以太网必需） */
#define LWIP_ARP                1

/* ================================================================
 * API 模式
 * ================================================================ */

/* Socket API：0=关闭（不编译 sockets.c，节省代码体积） */
#define LWIP_SOCKET             0

/* Netconn API：0=关闭（不编译 api_lib.c/api_msg.c，使用 Raw API）
 * XinYueC 自行封装 XSocket/XTcpSocket 等，不依赖 lwIP Netconn */
#define LWIP_NETCONN            0

/* 是否提供 API：1=保留 api.h 头文件（部分内部代码可能引用） */
#define LWIP_HAVE_API           1

/* Socket 兼容层：0=关闭（不提供 POSIX socket 函数封装） */
#define LWIP_COMPAT_SOCKETS     0

/* ================================================================
 * 网络接口
 * ================================================================ */

/* 网卡状态变化回调：1=启用（IP 分配/释放时通知应用层） */
#define LWIP_NETIF_STATUS_CALLBACK   1

/* 网卡链路状态回调：1=启用（网线插拔时通知应用层） */
#define LWIP_NETIF_LINK_CALLBACK     1

/* 回环接口（lo0）：0=关闭（由平台层自行创建回环网卡） */
#define LWIP_HAVE_LOOPIF             1

/* 网卡环回：1=启用（目标 IP 为本机时，数据不经过物理网卡直接环回） */
#define LWIP_NETIF_LOOPBACK          1

/* 硬件地址（MAC）最大长度：6 字节（以太网标准） */
#define NETIF_MAX_HWADDR_LEN         6

/* ================================================================
 * 应用层协议
 * ================================================================ */

/* HTTP 服务器：0=关闭 */
#define LWIP_HTTPD              0

/* SNMP（简单网络管理协议）：0=关闭 */
#define LWIP_SNMP               0

/* PPP（点对点协议）：0=关闭 */
#define PPP_SUPPORT             0

/* mDNS（组播 DNS）响应器：0=关闭 */
#define LWIP_MDNS_RESPONDER     0

/* DHCP 失败后自动切换到 AUTOIP：0=关闭 */
#define LWIP_DHCP_AUTOIP_FALLBACK 0

/* ================================================================
 * 调试：lwIP 内部调试输出
 * ================================================================ */

/* 提供 errno 定义：1=启用（部分 lwIP 代码引用 errno） */
#define LWIP_PROVIDE_ERRNO      1

/* 平台诊断输出宏：lwIP 内部 LWIP_DEBUGF 使用 printf+fflush
 * 与适配层 LWIP_DBG(XPrintf) 分离，互不影响 */
#define LWIP_PLATFORM_DIAG(x)   do { XPrintf x; fflush(stdout); } while(0)

/* lwIP 调试总开关
 * 重要：不能写 #define LWIP_DEBUG 0！
 * C 预处理器 #ifdef 检查“是否定义”而非“值是否非零”。
 * #define LWIP_DEBUG 0 仍然算已定义，LWIP_DEBUGF 仍然输出。
 * 正确做法：关闭时不定义此宏，开启时定义为 1。
 * 由 XNETWORK_LWIP_DEBUG 控制：0=不定义（关闭），1=定义为 1（开启） */
#if XNETWORK_LWIP_DEBUG
#define LWIP_DEBUG 1
#endif

/* 调试最低级别：LWIP_DBG_LEVEL_ALL=输出所有级别（含 trace/warning/serious/fatal） */
#define LWIP_DBG_MIN_LEVEL      LWIP_DBG_LEVEL_ALL

/* DHCP 模块调试：LWIP_DBG_ON=开启 DHCP 状态机详细日志 */
#define DHCP_DEBUG              LWIP_DBG_ON

/* ================================================================
 * 其他
 * ================================================================ */

/* 字节序函数：0=提供 lwIP 自带的 htonl/ntohs 等
 * 1=不提供（假设系统已有，避免冲突） */
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS  0

/* 定时器总开关：1=启用 lwIP 定时器框架 */
#define LWIP_TIMERS             1

/* 自定义定时器后端：1=由 sys_arch.c 提供 sys_timeout/sys_untimeout 等
 * 直接对接 XTimeWheelGroup 事件驱动，无需 sys_check_timeouts 轮询
 * 此宏同时使 timeouts.c 整体排除编译（#if !LWIP_TIMERS_CUSTOM 包裹） */
#define LWIP_TIMERS_CUSTOM      1

/* 以太网支持：1=启用（etharp + ethernetInput） */
#define LWIP_ETHERNET           1

/* tcpip_thread 线程名（NO_SYS=0 时使用） */
#define TCPIP_THREAD_NAME       "tcpip_thread"

/* tcpip_thread 配置（仅 NO_SYS=0 时生效） */
#if !NO_SYS
/* tcpip_thread 邮箱大小：128 条消息（0=使用默认值） */
#define TCPIP_MBOX_SIZE         128

/* tcpip_thread 栈大小：4096 字节 */
#define TCPIP_THREAD_STACKSIZE  4096

/* tcpip_thread 优先级：1（低优先级，避免抢占主线程） */
#define TCPIP_THREAD_PRIO       1
#endif

#endif /* LWIP_LWIPOPTS_H */
