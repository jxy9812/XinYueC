#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* 基本协议功能配置 */
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_ICMP               1
#define LWIP_DHCP               1
#define LWIP_IPV4               1
#define LWIP_IPV6               1  //
#define IPV6_FRAG_COPYHEADER    1  // MSVC 结构体对齐兼容
#define LWIP_ACD                0  // 禁用地址冲突检测
#define LWIP_DHCP_DOES_ACD_CHECK 0 // MSVC 下 struct acd 未定义，DHCP 不调用 ACD
#define LWIP_IGMP               1  // 启用IGMP多播（XNetwork需要）
#define LWIP_AUTOIP             0  // 禁用AutoIP
#define LWIP_DNS                1  // 启用DNS客户端

/* 内存管理配置 */
#define MEM_CUSTOM_ALLOCATOR    1
#define MEM_CUSTOM_MALLOC       XMalloc_System     // XMemory改为XMalloc_System
#define MEM_CUSTOM_FREE         XFree_System
#define MEM_CUSTOM_CALLOC       XCalloc_System
#define MEM_ALIGNMENT           4
#define MEM_SIZE                32768   // 32KB，根据RAM大小调整

/* 内存池配置 - 修复TCP_SEG与SND_QUEUELEN冲突 */
#define MEMP_NUM_PBUF           16
#define MEMP_NUM_UDP_PCB        4
#define MEMP_NUM_TCP_PCB        4
#define MEMP_NUM_TCP_PCB_LISTEN 2    // 新增：监听状态PCB数量
#define MEMP_NUM_TCP_SEG        16   // 增加到16以匹配SND_QUEUELEN
#define MEMP_NUM_SYS_TIMEOUT    16   // 增加到16，TCP重传+ARP+定时器需要更多
#define MEMP_NUM_NETBUF         0    // LWIP_NETCONN=0 不需要NetBuf
#define MEMP_NUM_NETCONN        0    // LWIP_NETCONN=0 不需要NetConn

/* 任务与线程配置 */
#define TCPIP_THREAD_STACKSIZE  2048
#define TCPIP_MBOX_SIZE         8
#define DEFAULT_THREAD_STACKSIZE 1024  // 新增：默认线程栈大小
#define DEFAULT_THREAD_PRIO     1      // 新增：默认线程优先级
#define TCPIP_THREAD_PRIO       2      // 新增：TCP/IP线程优先级
#define LWIP_TCPIP_CORE_LOCKING 1
#define SYS_LIGHTWEIGHT_PROT    1      // 轻量级协议保护

/* 协议参数配置 */
#define TCP_MSS                 1460   // 以太网最佳MSS
#define TCP_WND                 8192   // 8KB接收窗口
#define TCP_SND_BUF             8192   // TCP发送缓冲区
#define TCP_SND_QUEUELEN        16     // 匹配MEMP_NUM_TCP_SEG
#define TCP_MAXRTX              5      // 最大重传次数
#define TCP_SYNMAXRTX           3      // SYN最大重传次数
#define IP_REASSEMBLY           1      // 启用IP分片重组
#define IP_FRAG                 1      // 启用IP分片
#define ARP_TABLE_SIZE          8
#define ARP_MAXAGE              120    // ARP表项超时(秒)

/* API支持配置 */
#define LWIP_SOCKET             0      // 关闭BSD Socket，使用XFd统一标识符
#define LWIP_NETCONN            0      // 关闭Netconn，使用Raw API回调
#define LWIP_HAVE_API           1      // 启用lwIP核心API
#define LWIP_COMPAT_SOCKETS     0      // 禁用兼容模式以减小代码

/* 调试与断言配置 */
#define LWIP_PROVIDE_ERRNO      1
#define LWIP_DEBUG              0      // 禁用调试以减小代码
#define LWIP_DBG_MIN_LEVEL      LWIP_DBG_LEVEL_OFF  // 调试级别
#define LWIP_PLATFORM_ASSERT(x) do { printf("Assert failed: %s at line %d\n", #x, __LINE__); while(1); } while(0)

/* 字节序配置 - lwIP 提供 lwip_htonl/lwip_ntohl 函数, XNetwork_lwip.c 中直接调用 */
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS  0  /* 允许 lwIP 提供 lwip_htonl 等 */

/* 硬件相关配置 */
#define CHECKSUM_BY_HARDWARE    1      // 启用硬件校验和
#define ETH_PHY_ADDR            0      // PHY地址
#define PHY_READY_TIMEOUT       5000   // PHY初始化超时(ms)
#define LINK_SPEED_CHECK        1      // 启用链路速度检测

/* 网络接口配置 */
#define LWIP_NETIF_STATUS_CALLBACK   1  // 启用接口状态回调
#define LWIP_NETIF_LINK_CALLBACK     1  // 启用链路状态回调
#define LWIP_HAVE_LOOPIF             0  // 禁用回环接口
#define NETIF_MAX_HWADDR_LEN         6  // MAC地址长度

/* 高级功能配置 */
#define LWIP_HTTPD              0      // 禁用HTTP服务器
#define LWIP_SNMP               0      // 禁用SNMP
#define PPP_SUPPORT             0      // 禁用PPP
#define LWIP_SLIP               0      // 禁用SLIP（不需要串口协议）
#define LWIP_MDNS_RESPONDER     0      // 禁用mDNS
#define LWIP_DHCP_AUTOIP_FALLBACK 0    // 禁用DHCP自动降级到AutoIP

/* 其他配置 */
#define TCPIP_THREAD_NAME       "tcpip_thread"  // 线程名称
#define DEFAULT_RAW_RECVMBOX_SIZE 4  // 原始套接字接收邮箱大小
#define DEFAULT_UDP_RECVMBOX_SIZE  4  // UDP接收邮箱大小
#define DEFAULT_TCP_RECVMBOX_SIZE  4  // TCP接收邮箱大小

#endif /* LWIP_LWIPOPTS_H */