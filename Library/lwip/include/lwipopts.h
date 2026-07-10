#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H
#include"XMemory.h"
/**
 * @file lwipopts.h
 * @brief lwIP 配置选项
 *
 * XinYueC 组件替换：
 *   - 内存: XMalloc_System / XFree_System / XCalloc_System
 *   - 定时器: XTimeWheelGroup
 *   - 随机数: XRandomGenerator_system()
 *   - 线程: XThread (sys_arch.c)
 *   - lwIP内部调试: printf+fflush
 *   - 适配层调试: LWIP_DBG (XPrintf), LWIP_NET_DEBUG=0 一键关闭
 */

/* ==================== 调试开关 ==================== */
#define LWIP_NET_DEBUG            1
#if LWIP_NET_DEBUG
  #define LWIP_DBG(fmt, ...)  XPrintf(fmt, ##__VA_ARGS__)
#else
  #define LWIP_DBG(fmt, ...)
#endif

/* ==================== 操作系统模式 ==================== */
#define NO_SYS                  0

/* ==================== 协议栈功能 ==================== */
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_ICMP               1
#define LWIP_DHCP               1
#define LWIP_DHCP_DOES_ACD_CHECK   0
#define LWIP_ACD                   0
#define LWIP_IPV4               1
#define LWIP_IPV6               0       /* 暂关闭IPv6，减少复杂度 */
#define LWIP_IGMP               0
#define LWIP_AUTOIP             0
#define LWIP_DNS                1

/* ==================== 内存管理 ==================== */
#define MEM_CUSTOM_ALLOCATOR    1
#define MEM_CUSTOM_MALLOC       XMalloc_System
#define MEM_CUSTOM_FREE         XFree_System
#define MEM_CUSTOM_CALLOC       XCalloc_System
#define MEMP_MEM_MALLOC         0
#define MEM_ALIGNMENT           4
#define MEM_SIZE                32768

/* ==================== 内存池数量 ==================== */
#define MEMP_NUM_PBUF           16
#define MEMP_NUM_UDP_PCB        8
#define MEMP_NUM_TCP_PCB        8
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG        32
#define MEMP_NUM_SYS_TIMEOUT    12
#define MEMP_NUM_NETBUF         8
#define MEMP_NUM_NETCONN        4

/* ==================== 线程与同步 ==================== */
#define LWIP_TCPIP_CORE_LOCKING 1
#define SYS_LIGHTWEIGHT_PROT    1

/* ==================== 协议栈参数 ==================== */
#define TCP_MSS                 1460
#define TCP_WND                 (8 * TCP_MSS)
#define TCP_SND_BUF             (8 * TCP_MSS)
#define TCP_SND_QUEUELEN        32
#define TCP_MAXRTX              6
#define TCP_SYNMAXRTX           4
#define IP_REASSEMBLY           1
#define IP_FRAG                 0
#define ARP_TABLE_SIZE          16
#define ARP_MAXAGE              300
#define LWIP_ARP                1

/* ==================== API 模式 ==================== */
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0       /* 关闭Netconn API，使用Raw API */
#define LWIP_HAVE_API           1
#define LWIP_COMPAT_SOCKETS     0

/* ==================== 网络接口 ==================== */
#define LWIP_NETIF_STATUS_CALLBACK   1
#define LWIP_NETIF_LINK_CALLBACK     1
#define LWIP_HAVE_LOOPIF             0
#define LWIP_NETIF_LOOPBACK          1
#define NETIF_MAX_HWADDR_LEN         6

/* ==================== 应用层协议 ==================== */
#define LWIP_HTTPD              0
#define LWIP_SNMP               0
#define PPP_SUPPORT             0
#define LWIP_MDNS_RESPONDER     0
#define LWIP_DHCP_AUTOIP_FALLBACK 0

/* ==================== 调试：lwIP内部用printf+fflush ==================== */
#define LWIP_PROVIDE_ERRNO      1
#define LWIP_PLATFORM_DIAG(x)   do { printf x; fflush(stdout); } while(0)
#define LWIP_DEBUG              1
#define LWIP_DBG_MIN_LEVEL      LWIP_DBG_LEVEL_ALL
#define DHCP_DEBUG              LWIP_DBG_ON

/* ==================== 其他 ==================== */
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS  0
#define LWIP_TIMERS             1
#define LWIP_ETHERNET           1
#define TCPIP_THREAD_NAME       "tcpip_thread"

#endif /* LWIP_LWIPOPTS_H */