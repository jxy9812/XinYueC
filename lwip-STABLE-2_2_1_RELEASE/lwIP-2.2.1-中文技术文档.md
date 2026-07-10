# lwIP 2.2.1 源码深度解析文档

> **版本**: lwIP STABLE-2_2_1_RELEASE  
> **协议许可**: BSD 许可证  
> **原作者**: Adam Dunkels (瑞典计算机科学研究所)  
> **当前维护**: 全球开发者社区

---

## 目录

- [1. 概述](#1-概述)
- [2. 架构设计](#2-架构设计)
- [3. 代码结构](#3-代码结构)
- [4. 核心基础设施](#4-核心基础设施)
  - [4.1 内存管理](#41-内存管理)
  - [4.2 数据包缓冲区 (pbuf)](#42-数据包缓冲区-pbuf)
  - [4.3 操作系统抽象层 (sys_arch)](#43-操作系统抽象层-sys_arch)
  - [4.4 网络接口管理 (netif)](#44-网络接口管理-netif)
  - [4.5 定时器系统](#45-定时器系统)
  - [4.6 校验和算法](#46-校验和算法)
  - [4.7 统计系统](#47-统计系统)
  - [4.8 初始化流程](#48-初始化流程)
- [5. IP 层实现](#5-ip-层实现)
  - [5.1 IPv4](#51-ipv4)
  - [5.2 IPv6](#52-ipv6)
  - [5.3 IP 分片与重组](#53-ip-分片与重组)
  - [5.4 ICMP / ICMPv6](#54-icmp--icmpv6)
  - [5.5 ARP / 邻居发现 (ND6)](#55-arp--邻居发现-nd6)
  - [5.6 DHCP / AutoIP / ACD](#56-dhcp--autoip--acd)
  - [5.7 IGMP / MLD](#57-igmp--mld)
  - [5.8 DNS 解析器](#58-dns-解析器)
- [6. 传输层实现](#6-传输层实现)
  - [6.1 TCP](#61-tcp)
  - [6.2 UDP](#62-udp)
  - [6.3 Raw API](#63-raw-api)
  - [6.4 altcp 抽象层](#64-altcp-抽象层)
- [7. API 层设计](#7-api-层设计)
  - [7.1 三层 API 架构](#71-三层-api-架构)
  - [7.2 TCP/IP 线程模型](#72-tcpip-线程模型)
  - [7.3 Netconn API](#73-netconn-api)
  - [7.4 Socket API](#74-socket-api)
  - [7.5 select / poll 实现](#75-select--poll-实现)
- [8. 网络接口驱动](#8-网络接口驱动)
  - [8.1 以太网](#81-以太网)
  - [8.2 SLIP](#82-slip)
  - [8.3 6LoWPAN](#83-6lowpan)
  - [8.4 PPP](#84-ppp)
  - [8.5 网桥](#85-网桥)
  - [8.6 ZEP](#86-zep)
- [9. 应用层协议](#9-应用层协议)
- [10. 配置系统](#10-配置系统)
- [11. 移植指南](#11-移植指南)
- [12. 网络原理](#12-网络原理)

---

## 1. 概述

lwIP (Lightweight IP) 是一个小型的独立 TCP/IP 协议栈实现，专为嵌入式系统设计。其核心设计目标是在保持完整 TCP 功能的前提下，最大限度减少 RAM 和 ROM 的使用量——仅需约 **10KB 空闲 RAM** 和 **40KB 代码 ROM** 即可运行。

### 1.1 主要特性

| 类别 | 支持的协议/功能 |
|------|-----------------|
| 网络层 | IP (IPv4/IPv6)、ICMP、IGMP、MLD、ARP、ND |
| 传输层 | TCP (含拥塞控制、RTT 估计、快速恢复/重传、SACK)、UDP (含 UDP-Lite)、Raw API |
| 自动配置 | DHCP、AutoIP/APIPA、ACD (地址冲突检测)、无状态 DHCPv6 |
| 地址 | IPv6 SLAAC、邻居发现 (RFC 4861)、地址作用域 (RFC 4007) |
| 安全 | altcp 透明 TLS 层 (mbedTLS 集成) |
| 链路层 | PPP (PPPoS/PPPoE)、6LoWPAN (IEEE 802.15.4/BLE/ZEP)、网桥 |
| API | Raw 回调 API、Netconn 顺序 API、Berkeley Socket API |
| 应用 | HTTP 服务器 (SSI/CGI/HTTPS)、SNMP (v1/v2c/v3)、SNTP、MQTT、mDNS、SMTP、TFTP、iPerf、NetBIOS |

### 1.2 设计哲学

lwIP 的核心设计理念包括：

1. **减少 RAM 使用**：通过精心的数据结构设计和可配置的内存池，使协议栈能在极小内存下运行
2. **回调驱动架构**：使用回调函数而非阻塞式调用，实现高效的事件处理
3. **零拷贝支持**：pbuf 链和自定义 pbuf 机制支持零拷贝收发
4. **高度可配置**：通过 `lwipopts.h` 文件几乎可以开关每一个功能
5. **OS 无关**：通过 `sys_arch` 抽象层，既可在裸机环境 (NO_SYS=1) 运行，也可在 RTOS 上运行

---

## 2. 架构设计

### 2.1 分层架构

lwIP 采用分层架构，但各层之间的界限较为灵活，以优化性能：

```
┌──────────────────────────────────────────────────────┐
│                  应用程序                              │
├──────────────┬──────────────┬────────────────────────┤
│  Socket API  │  Netconn API │   Raw API (回调)       │
│ (sockets.c)  │ (api_lib.c)  │ (tcp/udp/raw_*)        │
├──────────────┴──────────────┴────────────────────────┤
│              TCP/IP 线程 (tcpip.c)                     │
│         (消息传递 + 核心锁机制)                          │
├────────────────────────────────────────────────────────┤
│  传输层: TCP (tcp.c/in/out) │ UDP │ Raw │ altcp       │
├────────────────────────────────────────────────────────┤
│  网络层: IPv4 (ip4.c) │ IPv6 (ip6.c) │ ICMP │ IGMP    │
│         ARP (etharp.c) │ ND6 (nd6.c) │ DNS (dns.c)   │
├────────────────────────────────────────────────────────┤
│  链路层: Ethernet │ SLIP │ PPP │ 6LoWPAN │ Bridge     │
├────────────────────────────────────────────────────────┤
│  硬件抽象: netif │ sys_arch │ cc.h │ 内存管理          │
├────────────────────────────────────────────────────────┤
│  硬件驱动 (以太网MAC / 串口 / 无线)                      │
└────────────────────────────────────────────────────────┘
```

### 2.2 线程模型

lwIP 支持两种运行模式：

**NO_SYS=1 (裸机模式)**：
- 无 RTOS 依赖，所有 OS 抽象函数为空操作
- 仅支持 Raw API（回调式）
- 主循环中调用 `sys_check_timeouts()` 处理定时器
- 数据包在中断或轮询中接收

**NO_SYS=0 (OS 模式)**：
- 使用专用 TCP/IP 线程处理所有核心操作
- 应用线程通过邮箱/核心锁与 TCP/IP 线程通信
- 支持全部三种 API
- 定时器由 TCP/IP 线程自动处理

### 2.3 数据流动

**接收路径 (RX)**：
```
硬件中断 → 驱动程序 → pbuf_alloc(PBUF_POOL) → 复制数据到pbuf
    → netif->input(p, netif)
    → [NO_SYS=0: tcpip_input() → 邮箱投递到tcpip线程]
    → ethernet_input() → 协议分发
    → ip4_input()/ip6_input()/etharp_input()
    → tcp_input()/udp_input()/icmp_input()
    → 应用回调 / recvmbox / socket
```

**发送路径 (TX)**：
```
应用 → socket send() / netconn_write() / tcp_write()
    → [NO_SYS=0: API消息投递到tcpip线程]
    → tcp_output() / udp_send() / raw_send()
    → ip4_output_if() / ip6_output_if()
    → etharp_output() / ethip6_output() (地址解析)
    → ethernet_output() → netif->linkoutput()
    → 硬件驱动发送
```

---

## 3. 代码结构

### 3.1 目录结构

```
lwip-STABLE-2_2_1_RELEASE/
├── src/                        # 源代码
│   ├── core/                   # 核心协议实现
│   │   ├── ipv4/               # IPv4 相关 (ip4, etharp, dhcp, icmp, igmp, autoip, acd)
│   │   └── ipv6/               # IPv6 相关 (ip6, nd6, icmp6, mld6, dhcp6)
│   ├── api/                    # API 层 (socket, netconn, tcpip 线程)
│   ├── netif/                  # 网络接口驱动
│   │   └── ppp/                # PPP 协议栈
│   ├── apps/                   # 应用层协议
│   │   ├── http/               # HTTP 服务器/客户端
│   │   ├── snmp/               # SNMP 代理
│   │   ├── sntp/               # SNTP 客户端
│   │   ├── mqtt/               # MQTT 客户端
│   │   ├── mdns/               # mDNS 响应器
│   │   ├── smtp/               # SMTP 客户端
│   │   ├── tftp/               # TFTP 服务器/客户端
│   │   ├── lwiperf/            # iPerf 服务器
│   │   ├── netbiosns/          # NetBIOS 名称服务
│   │   └── altcp_tls/          # altcp TLS (mbedTLS)
│   └── include/                # 头文件
│       ├── lwip/               # 公共头文件
│       │   ├── prot/           # 协议头定义 (IP/TCP/UDP/ICMP 等结构体)
│       │   └── priv/           # 私有头文件 (内部实现细节)
│       └── netif/              # 网络接口头文件
├── contrib/                    # 贡献代码
│   ├── ports/                  # 平台移植
│   │   ├── win32/              # Windows 移植
│   │   ├── unix/               # Unix/Linux 移植
│   │   └── freertos/           # FreeRTOS 移植
│   ├── examples/               # 示例代码
│   └── apps/                   # 额外应用
├── doc/                        # 文档
└── test/                       # 测试代码
```

### 3.2 核心源文件功能

| 文件 | 行数 | 功能 |
|------|------|------|
| `core/init.c` | 390 | 初始化序列，编译时配置校验 |
| `core/mem.c` | 1004 | 堆内存管理 (首次适应 + 合并) |
| `core/memp.c` | 447 | 固定大小内存池管理 |
| `core/pbuf.c` | 1554 | 数据包缓冲区管理 |
| `core/netif.c` | 1855 | 网络接口管理 |
| `core/sys.c` | 148 | OS 抽象 (仅 sys_msleep) |
| `core/timeouts.c` | 451 | 定时器和超时管理 |
| `core/inet_chksum.c` | 608 | Internet 校验和算法 |
| `core/ip.c` | 167 | 双栈 IP 输入分发 |
| `core/tcp.c` | 2696 | TCP PCB 管理和状态机 |
| `core/tcp_in.c` | 2194 | TCP 输入处理 |
| `core/tcp_out.c` | 2260 | TCP 输出处理 |
| `core/udp.c` | 1321 | UDP 实现 |
| `core/dns.c` | 1657 | DNS 解析器 |
| `core/ipv4/ip4.c` | ~900 | IPv4 主处理 |
| `core/ipv4/etharp.c` | ~1500 | ARP 协议 |
| `core/ipv4/dhcp.c` | ~2000 | DHCP 客户端 |
| `core/ipv6/ip6.c` | 1495 | IPv6 主处理 |
| `core/ipv6/nd6.c` | 2474 | 邻居发现 |
| `api/sockets.c` | 4244 | Socket API (最大文件) |
| `api/api_msg.c` | 2177 | Netconn API 消息处理 |
| `api/tcpip.c` | 715 | TCP/IP 线程 |

---

## 4. 核心基础设施

### 4.1 内存管理

lwIP 提供三种互斥的内存分配策略，通过配置宏选择：

#### 4.1.1 自定义分配器模式 (`MEM_CUSTOM_ALLOCATOR`)

将 `mem_malloc`/`mem_free` 委托给用户自定义的宏（通常是 C 库的 `malloc`/`free`）。`mem_init()` 和 `mem_trim()` 为空操作。

#### 4.1.2 内存池模式 (`MEM_USE_POOLS`)

使用多个固定大小的池替代堆。`mem_malloc()` 从小到大扫描池，找到第一个足够大的池分配。每个分配块前附加 `memp_malloc_helper` 头记录所属池号，`mem_free()` 据此归还。

#### 4.1.3 lwIP 内置堆分配器 (默认)

这是最常用的模式。核心数据结构：

```c
struct mem {
    mem_size_t next;     // 下一个块的偏移量 (相对于 ram 基址)
    mem_size_t prev;     // 前一个块的偏移量
    u8_t used;           // 1=已分配, 0=空闲
    mem_size_t user_size; // 用户请求大小 (仅溢出检查时)
};
```

**算法要点**：

- 使用静态数组 `ram_heap[MEM_SIZE + 2*SIZEOF_STRUCT_MEM]` 作为堆空间
- 以**偏移量**（而非指针）连接空闲块链表，在 16 位系统上更紧凑
- **首次适应 (First-Fit) 分配**：从 `lfree` 指针开始向前扫描，找到第一个足够大的空闲块
- **合并 (Coalescing)**：`plug_holes()` 在每次 `mem_free()` 后合并相邻空闲块
- **分割 (Splitting)**：分配时如果剩余空间 >= `MIN_SIZE_ALIGNED`，则分割出新空闲块
- **最小块大小** `MIN_SIZE`（默认 12 字节）防止碎片浪费 `struct mem` 开销
- **溢出检测** (`MEM_OVERFLOW_CHECK`)：在分配块前后填充 `0xCD` 保护字节
- **中断安全释放** (`LWIP_ALLOW_MEM_FREE_FROM_OTHER_CONTEXT`)：使用 `mem_free_count` volatile 标志让 `mem_malloc()` 检测中断中的释放并重试

#### 4.1.4 固定大小内存池 (memp)

memp 使用 **X-Macro 模式**（X 宏）声明和管理所有池：

```c
// memp_std.h 中定义:
#define LWIP_MEMPOOL(name, num, size, desc)

// memp.c 中多次包含，每次重新定义宏:
#define LWIP_MEMPOOL(name,num,size,desc) LWIP_MEMPOOL_DECLARE(name,num,size,desc)
#include "lwip/priv/memp_std.h"
```

这自动生成：池内存数组、池描述符数组 `memp_pools[]`、`memp_t` 枚举（MEMP_PBUF、MEMP_TCP_PCB 等）。

**分配/释放**：O(1) 操作——从单链表空闲栈弹出/压入。使用 `SYS_ARCH_PROTECT`（中断屏蔽）而非互斥锁以保证速度。

**溢出检测** (`MEMP_OVERFLOW_CHECK`)：每个池元素前后添加 `0xCD` 保护字节。级别 2 在每次操作时检查所有池元素。

**循环链表检测** (`MEMP_SANITY_CHECK`)：使用 Floyd 龟兔赛跑算法检测空闲链表是否成环。

### 4.2 数据包缓冲区 (pbuf)

pbuf 是 lwIP 中所有网络数据包的统一表示形式。

#### 4.2.1 pbuf 结构

```c
struct pbuf {
    struct pbuf *next;       // 链中下一个 pbuf
    void *payload;           // 数据指针
    u16_t tot_len;           // 本 pbuf 及后续所有 pbuf 的总长度
    u16_t len;               // 仅本 pbuf 的数据长度
    u8_t type_internal;      // 编码了分配来源和数据特性
    u8_t flags;              // 标志位 (PUSH, IS_CUSTOM, MCASTLOOP 等)
    LWIP_PBUF_REF_T ref;     // 引用计数
    u8_t if_idx;             // 接收此包的 netif 索引
};
```

#### 4.2.2 pbuf 类型

| 类型 | 分配来源 | 用途 | 特点 |
|------|---------|------|------|
| `PBUF_RAM` | 堆 (mem_malloc) | 发送 (TX) | struct + payload 连续分配 |
| `PBUF_POOL` | 内存池 (MEMP_PBUF_POOL) | 接收 (RX) | struct + payload 连续，可链式 |
| `PBUF_ROM` | MEMP_PBUF | 只读数据 | payload 指向 ROM，入队时无需拷贝 |
| `PBUF_REF` | MEMP_PBUF | 外部 RAM 数据 | payload 指向外部 RAM，易变，入队时需拷贝 |

#### 4.2.3 关键操作

- **`pbuf_alloc(layer, length, type)`**：根据 layer 预留头部空间（PBUF_TRANSPORT 预留 TCP+IP+链路层头），按 type 分配
- **`pbuf_free(p)`**：遍历链减引用计数，归零则释放，遇到 ref>0 则停止。返回释放的 pbuf 数
- **`pbuf_ref(p)`**：原子递增引用计数（使用 `SYS_ARCH_SET`）
- **`pbuf_cat(h, t)`**：拼接两个链，t 的引用转移给 h（无引用计数变化）
- **`pbuf_chain(h, t)`**：拼接并 `pbuf_ref(t)`，调用者仍持有 t 的引用
- **`pbuf_header(p, size)`**：调整 payload 指针以暴露/隐藏协议头。正值向后移（暴露头），负值向前移（隐藏头）
- **`pbuf_copy_partial(p, dst, len, offset)`**：从 pbuf 链指定偏移复制数据到平面缓冲区
- **`pbuf_take(p, src, len)`**：将应用数据复制到 pbuf 链

#### 4.2.4 零拷贝支持

通过 `pbuf_custom` 结构支持零拷贝接收：

```c
struct pbuf_custom {
    struct pbuf pbuf;
    pbuf_free_custom_fn custom_free_function; // 自定义释放函数
};
```

驱动可以预分配 DMA 缓冲区，用 `pbuf_alloced_custom()` 创建指向 DMA 缓冲区的 PBUF_REF pbuf。当 lwIP 用完该 pbuf 时，自定义释放函数将 DMA 描述符归还硬件——全程零拷贝。

#### 4.2.5 链式 pbuf 与分散-聚集 I/O

pbuf 链天然支持分散-聚集 I/O：
- `tot_len` vs `len` 的区别可以判断链是否结束（`tot_len == len` 表示最后一个）
- 接收大包时，`PBUF_POOL` 会自动链式分配
- 协议头处理通过 `pbuf_header()` 在同一 pbuf 上操作，无需重新分配

### 4.3 操作系统抽象层 (sys_arch)

lwIP 的 OS 抽象层定义在 `sys.h` 中，由移植者实现 `sys_arch.c`。

#### 4.3.1 抽象原语

| 原语 | 函数 | 说明 |
|------|------|------|
| 信号量 | `sys_sem_new/signal/wait/free` | 二进制/计数信号量 |
| 互斥锁 | `sys_mutex_new/lock/unlock/free` | 递归互斥锁（LWIP_COMPAT_MUTEX=0 时） |
| 邮箱 | `sys_mbox_new/post/trypost/fetch/tryfetch` | 消息队列，消息为 `void*` |
| 线程 | `sys_thread_new(name, fn, arg, stack, prio)` | 创建线程，**不允许失败** |
| 时间 | `sys_now()` | 返回当前毫秒时间（回绕安全） |
| 临界区 | `sys_arch_protect/unprotect` | 轻量级保护（中断屏蔽或调度器锁） |

#### 4.3.2 NO_SYS 模式

当 `NO_SYS=1` 时，所有 OS 函数定义为空操作宏。`sys_sem_t`、`sys_mutex_t`、`sys_mbox_t` 类型退化为 `u8_t`。仅需实现 `sys_now()` 和（可选）`sys_arch_protect/unprotect`。

#### 4.3.3 临界区保护

`SYS_ARCH_PROTECT`/`SYS_ARCH_UNPROTECT` 提供轻量级临界区保护（通常通过中断屏蔽实现），用于内存分配等短临界区，避免互斥锁开销。支持原子操作宏：`SYS_ARCH_INC`、`SYS_ARCH_DEC`、`SYS_ARCH_GET`、`SYS_ARCH_SET`。

### 4.4 网络接口管理 (netif)

#### 4.4.1 netif 结构

`struct netif` 是所有网络接口的核心描述符，通过单链表管理：

```c
struct netif {
    struct netif *next;           // 链表指针
    ip_addr_t ip_addr, netmask, gw; // IPv4 配置
    ip_addr_t ip6_addr[N];       // IPv6 地址数组
    u8_t ip6_addr_state[N];      // IPv6 地址状态
    netif_input_fn input;        // 接收回调 (ethernet_input 或 tcpip_input)
    netif_output_fn output;      // IPv4 输出 (通常 etharp_output)
    netif_output_ip6_fn output_ip6; // IPv6 输出 (通常 ethip6_output)
    netif_linkoutput_fn linkoutput; // 原始链路层输出
    void *state;                 // 驱动私有状态
    void* client_data[];         // 客户端数据槽 (DHCP, AUTOIP 等)
    u16_t mtu;
    u8_t hwaddr[NETIF_MAX_HWADDR_LEN];
    u8_t hwaddr_len;
    u8_t flags;                  // NETIF_FLAG_UP, LINK_UP, ETHARP, ETHERNET...
    char name[2]; u8_t num;     // 接口标识 (如 "en0")
};
```

#### 4.4.2 回调层级

netif 支持多级回调：

1. **基本状态回调** (`status_callback`)：netif up/down 时调用
2. **链路状态回调** (`link_callback`)：物理链路 up/down 时调用
3. **移除回调** (`remove_callback`)：netif 被移除时调用
4. **扩展状态回调** (`netif_ext_callback`)：多订阅者观察者模式，支持 NSC_NETIF_ADDED、LINK_CHANGED、IPV4_ADDRESS_CHANGED、IPV6_SET 等事件

#### 4.4.3 关键操作

- **`netif_add()`**：注册新接口，调用驱动 `init()` 函数，分配唯一接口号 (0-254)
- **`netif_set_up/down()`**：切换管理状态标志，up 时发送免费 ARP 和 IGMP/MLD 报告
- **`netif_set_link_up/down()`**：切换物理链路状态，通知 DHCP/AUTOIP/ACD
- **`netif_set_default()`**：设置默认路由接口
- **`netif_create_ip6_linklocal_address()`**：从 MAC 地址通过 EUI-64 转换生成 IPv6 链路本地地址

#### 4.4.4 软件环回

当 `LWIP_NETIF_LOOPBACK=1` 时，发送到本机 IP 的包不经过硬件，而是复制到 PBUF_RAM 并排队在 `netif->loop_first/loop_last`，由 `netif_poll()` 处理。多线程模式下通过 `tcpip_try_callback()` 调度。

### 4.5 定时器系统

#### 4.5.1 数据结构

```c
struct sys_timeo {
    struct sys_timeo *next;
    u32_t time;              // 绝对到期时间 (ms)
    sys_timeout_handler h;   // 回调函数
    void *arg;               // 回调参数
};
```

#### 4.5.2 循环定时器表

lwIP 维护一个静态的循环定时器数组 `lwip_cyclic_timers[]`，条件编译包含：

| 定时器 | 间隔 | 功能 |
|--------|------|------|
| TCP 定时器 | 250ms | 调用 tcp_tmr (按需启动) |
| IP 重组定时器 | 1000ms | 清理过期分片 |
| ARP 定时器 | 1000ms | 清理过期 ARP 表项 |
| DHCP 粗定时器 | 60s | 租约管理 |
| DHCP 细定时器 | 500ms | 请求重传 |
| ACD 定时器 | 100ms | 地址冲突探测 |
| IGMP 定时器 | 100ms | 组成员报告 |
| DNS 定时器 | 1000ms | 缓存 TTL 和重传 |
| ND6 定时器 | 1000ms | 邻居缓存管理 |
| MLD6 定时器 | 100ms | 组监听报告 |
| DHCPv6 定时器 | 500ms | 请求重传 |

#### 4.5.3 核心机制

- **排序链表**：超时项按绝对到期时间排序，`sys_check_timeouts()` 只需检查链表头
- **回绕安全比较**：`TIME_LESS_THAN(t, compare_to)` 使用无符号 32 位回绕算术
- **按需 TCP 定时器**：TCP 定时器最频繁(250ms)，但仅在有活动连接时才运行。`tcp_timer_needed()` 在新 PCB 注册时延迟启动
- **漂移校正**：循环定时器回调从 `current_timeout_due_time + interval` 计算下次到期，而非 `sys_now() + interval`，避免处理延迟导致的漂移积累
- **重启机制**：`sys_restart_timeouts()` 将所有超时时间重新基准化为 `sys_now()`，用于低功耗唤醒后避免定时器风暴

#### 4.5.4 NO_SYS 模式下的使用

```c
// 主循环中:
while (1) {
    sys_check_timeouts();     // 处理所有到期定时器
    // 接收数据包...
    u32_t sleep_time = sys_timeouts_sleeptime(); // 下次定时器的等待时间
    if (sleep_time != INFINITE) {
        // 可以休眠 sleep_time 毫秒以节省功耗
    }
}
```

### 4.6 校验和算法

`inet_chksum.c` 提供三种 Internet 校验和算法实现，通过 `LWIP_CHKSUM_ALGORITHM` 选择：

- **算法 1**：简单可移植版本，16 位累加到 32 位累加器
- **算法 2** (默认)：Curt McDowell 版本，处理非对齐缓冲区，使用 `u16_t*` 指针批量处理
- **算法 3**：优化版本，循环展开，每次处理 8 字节，使用 `u32_t*` 指针

所有算法使用 32 位累加 + `FOLD_U32T()` 折叠处理进位。

**伪头校验和**：为 TCP/UDP 计算，包含源/目的 IP 地址、协议号和长度。支持 IPv4 和 IPv6 两种伪头。提供部分校验和变体 (`inet_chksum_pseudo_partial`) 用于 UDP-Lite。

可通过 `LWIP_CHKSUM` 宏覆盖为平台特定的汇编优化实现。

### 4.7 统计系统

统计通过宏递增，禁用时零运行时开销：

```c
// 禁用时:
#define TCP_STATS_INC(x)

// 启用时:
#define TCP_STATS_INC(x) LWIP_STATS_INC(x)
```

统计类别包括：链路、ARP、IP、ICMP、UDP、TCP、IGMP、内存、内存池、系统（信号量/邮箱计数）以及 SNMP MIB-II 计数器。

### 4.8 初始化流程

`lwip_init()` 按以下顺序初始化所有模块：

```
stats_init()          → 统计
sys_init()            → OS 抽象 (仅 NO_SYS=0)
mem_init()            → 堆内存
memp_init()           → 内存池
pbuf_init()           → 数据包缓冲区
netif_init()          → 网络接口 (含环回接口)
ip_init()             → IP 层 (双栈分发)
etharp_init()         → ARP
raw_init()            → Raw IP
udp_init()            → UDP
tcp_init()            → TCP
igmp_init()           → IGMP
dns_init()            → DNS
ppp_init()            → PPP
sys_timeouts_init()   → 定时器 (注册所有循环定时器)
```

`init.c` 中还包含大量编译时 `#error` 检查，确保配置一致性（如：Netconn/Socket API 需要 NO_SYS=0；TCP 窗口必须 >= 2*MSS 等）。

---

## 5. IP 层实现

### 5.1 IPv4

#### 5.1.1 IP 头部

```c
struct ip_hdr {
    u8_t  _v_hl;      // 版本(4位) + 头部长度(4位, 以32位字为单位)
    u8_t  _tos;        // 服务类型
    u16_t _len;        // 总长度
    u16_t _id;         // 标识
    u16_t _offset;     // 标志 + 分片偏移
    u8_t  _ttl;        // 生存时间
    u8_t  _proto;      // 协议
    u16_t _chksum;     // 头部校验和
    ip4_addr_p_t src;  // 源地址
    ip4_addr_p_t dest; // 目的地址
};
```

#### 5.1.2 输入路径 (`ip4_input()`)

1. **头部校验**：验证 IP 版本=4，提取头部长度和总长度，校验头部校验和
2. **地址复制**：复制源/目的地址到对齐的 `ip_data` 全局变量
3. **接口匹配**：
   - 组播：检查 IGMP 组成员
   - 单播/广播：检查目的地址是否匹配本机 netif 的 IP/广播地址/链路本地地址
   - 遍历所有 netif 查找匹配
4. **源地址验证**：拒绝广播/组播源地址（RFC 1122）
5. **转发**：如果不是给本机的且 `IP_FORWARD` 开启，调用 `ip4_forward()`
6. **分片重组**：如果 `IPH_OFFSET` 有分片标志，调用 `ip4_reass()`
7. **上层分发**：根据 `IPH_PROTO` 分发到 UDP/TCP/ICMP/IGMP

#### 5.1.3 输出路径 (`ip4_output_if_src()`)

1. 确定源地址（如未指定，使用 netif 的 IP）
2. 构造 IP 头部（版本、TTL、协议、长度、ID 等）
3. 计算头部校验和
4. 环回处理（如果目的地址是本机）
5. 分片（如果超过 MTU）
6. 调用 `netif->output()`（通常是 `etharp_output`）

#### 5.1.4 路由 (`ip4_route()`)

1. 组播且有默认组播接口 → 返回该接口
2. 遍历 netif，匹配目的地址 & 子网掩码
3. 点对点接口匹配网关
4. 环回处理
5. 回退到默认 netif

### 5.2 IPv6

#### 5.2.1 IPv6 地址

```c
struct ip6_addr {
    u32_t addr[4];    // 128 位，4×32 位，网络字节序
#if LWIP_IPV6_SCOPES
    u8_t zone;         // 区域索引 (RFC 4007)
#endif
};
```

**区域处理**：链路本地单播和接口本地/链路本地组播地址有作用域约束。`ip6_addr_assign_zone()` 根据 netif 索引分配区域。区域在输入/输出/转发时强制执行。

**地址分类宏**：`ip6_addr_islinklocal()` (fe80::/10)、`ip6_addr_isglobal()` (2000::/3)、`ip6_addr_ismulticast()` (ff00::/8)、`ip6_addr_issolicitednode()` (ff02::1:ff00:0/104) 等。

**地址状态**：INVALID → TENTATIVE (DAD 探测中) → PREFERRED → DEPRECATED → INVALID

#### 5.2.2 IPv6 输入 (`ip6_input()`)

1. 验证版本=6
2. 拷贝地址并分配区域
3. 拒绝 IPv4 映射地址和组播源
4. 接口匹配（组播检查 MLD 成员/ solicited-node；单播检查各 netif 地址）
5. 处理扩展头：
   - **Hop-by-Hop**：解析选项（PAD1、PADN、Router Alert、Jumbo）
   - **Destination Options**：同上，还处理 Home Address 选项
   - **Routing**：验证类型（Type 2、RPL），跳过
   - **Fragment**：委托给重组
6. 分发到 Raw/UDP/TCP/ICMPv6

#### 5.2.3 源地址选择 (RFC 6724)

`ip6_select_source_address()` 实现简化版的 RFC 6724 源地址选择规则：
- 规则 1：相同地址优先
- 规则 2：适当作用域优先
- 规则 3：preferred 优于 deprecated
- 规则 8：最长前缀匹配

### 5.3 IP 分片与重组

#### 5.3.1 IPv4 重组 (`ip4_reass()`)

- 维护 `reassdatagrams` 链表，每个条目对应一个正在重组的数据报
- 每个分片的 IP 头被覆写为 `ip_reass_helper` 结构（next_pbuf, start, end）
- 分片按偏移量插入排序链表
- 如果 `IP_REASS_CHECK_OVERLAP`，检查重叠/重复分片
- 所有分片到齐（最后分片已收到 + 无间隙 + 起始偏移=0）时，恢复原始 IP 头并链式拼接 pbuf
- 重组超时 `IP_REASS_MAXAGE`（默认 15 秒），超时发送 ICMP Time Exceeded
- 资源限制 `IP_REASS_MAX_PBUFS`，满了时释放最旧的数据报

#### 5.3.2 IPv4 分片 (`ip4_frag()`)

- 计算每片最大数据块 `nfb = (MTU - IP_HLEN) / 8`
- 两种模式：
  - `LWIP_NETIF_TX_SINGLE_PBUF=1`：分配单个 PBUF_RAM，拷贝数据
  - `LWIP_NETIF_TX_SINGLE_PBUF=0`：使用 `pbuf_custom_ref` 创建 PBUF_REF 零拷贝引用
- 设置分片偏移和 MF 标志，重算校验和，逐片发送

#### 5.3.3 IPv6 分片/重组

IPv6 使用独立的 Fragment 头部（Next Header=44）。重组逻辑类似 IPv4 但使用 32 位标识符和 IPv6 特定的区域处理。分片时需考虑 Path MTU（通过 `nd6_get_destination_mtu()` 获取）。

### 5.4 ICMP / ICMPv6

#### 5.4.1 ICMP (IPv4)

处理的 ICMP 类型：
- **Echo Request (8)**：验证校验和，交换源/目的地址，类型改为 Echo Reply (0)，增量更新校验和，发送回复。组播/广播 ping 可选支持。
- **Echo Reply (0)**：递增 MIB 计数器
- **Destination Unreachable (3)**、**Time Exceeded (11)**：仅递增计数器
- 发送函数：`icmp_dest_unreach()` 和 `icmp_time_exceeded()`，包含原始 IP 头 + 8 字节载荷

#### 5.4.2 ICMPv6

处理的类型：
| 类型 | 名称 | 处理 |
|------|------|------|
| 1 | Destination Unreachable | `icmp6_dest_unreach()` |
| 2 | Packet Too Big | `nd6_input()` 更新 PMTU |
| 3 | Time Exceeded | `icmp6_time_exceeded()` |
| 4 | Parameter Problem | `icmp6_param_problem()` |
| 128 | Echo Request | 生成 Echo Reply |
| 129 | Echo Reply | Raw PCB 处理 |
| 130-132 | MLD | `mld6_input()` |
| 133-137 | RS/RA/NS/NA/RD | `nd6_input()` |

所有 ICMPv6 错误消息的跳数限制为 255。

### 5.5 ARP / 邻居发现 (ND6)

#### 5.5.1 ARP 缓存

```c
struct etharp_entry {
    struct pbuf *q;           // 等待解析的数据包队列
    ip4_addr_t ipaddr;        // IP 地址
    struct netif *netif;      // 网络接口
    struct eth_addr ethaddr;  // 以太网地址
    u16_t ctime;              // 年龄 (秒)
    u8_t state;               // 状态
};
```

**ARP 表项状态机**：

```
EMPTY → PENDING (发送ARP请求) → STABLE (收到回复)
                                   ↓ (接近超时)
                         STABLE_REREQUESTING_1
                                   ↓
                         STABLE_REREQUESTING_2
                                   ↓
                               STABLE (重新请求)
```

- `ARP_MAXAGE`（默认 300 秒）：稳定表项超时
- `ARP_MAXPENDING`（默认 5 秒）：待解析表项超时
- 接近超时时自动发送单播/广播 ARP 刷新请求

**`etharp_find_entry()`**：单次扫描同时查找精确匹配、空闲条目、最旧稳定条目（用于回收）和最旧待解析条目。

**`etharp_output()`**：处理广播 IP（映射到以太网广播）、组播 IP（映射到 01:00:5E:xx:xx:xx）、单播 IP（查 ARP 表，必要时发 ARP 请求并排队数据包）。

#### 5.5.2 IPv6 邻居发现 (ND6)

ND6 维护四个全局静态数组：

| 缓存 | 默认大小 | 配置选项 |
|------|---------|---------|
| 邻居缓存 | 10 | `LWIP_ND6_NUM_NEIGHBORS` |
| 目的缓存 | 10 | `LWIP_ND6_NUM_DESTINATIONS` |
| 前缀列表 | 5 | `LWIP_ND6_NUM_PREFIXES` |
| 默认路由器列表 | 3 | `LWIP_ND6_NUM_ROUTERS` |

**邻居缓存状态机** (RFC 4861)：

```
INCOMPLETE → (收到NA) → REACHABLE → (超时) → STALE
                ↑                                    ↓
                +← (NS回复/上层提示) ← DELAY ← (发包) ←+
                                     ↓ (超时)
                                   PROBE
                                     ↓ (NS重试)
                                   REACHABLE / 释放
```

- **REACHABLE**：可达时间（默认 30 秒）内确认可达
- **STALE**：可达时间过期，但不主动探测，发包时转 DELAY
- **DELAY**：等待上层协议确认（5 秒），否则转 PROBE
- **PROBE**：发送单播 NS，最多 `LWIP_ND6_MAX_UNICAST_SOLICIT`（3）次

**TCP 可达性提示** (`LWIP_ND6_TCP_REACHABILITY_HINTS`)：TCP 收发 ACK 时调用 `nd6_reachability_hint()`，无需发送 NS 即可确认邻居可达。

**路由器发现**：发送 RS（Router Solicitation），最多 `LWIP_ND6_MAX_MULTICAST_SOLICIT`（3）次。收到 RA 时更新默认路由器列表、前缀列表、可达时间和 MTU。

### 5.6 DHCP / AutoIP / ACD

#### 5.6.1 DHCP 状态机

```
OFF → INIT → SELECTING (发送DISCOVER)
              ↓ (收到OFFER)
         REQUESTING (发送REQUEST)
              ↓ (收到ACK + ACD通过)
           BOUND ──── (T1超时) ──→ RENEWING (单播续租)
              ↑                        ↓ (T2超时)
              +← (ACK) ←──── REBINDING (广播续租)
                                    ↓ (T0超时)
                              OFF → SELECTING (重新获取)
```

**定时器**：
- 粗定时器（60 秒）：管理租约时间，T1(续租, 默认 50%)、T2(重绑, 默认 87.5%)
- 细定时器（500 毫秒）：请求重传，指数退避

**选项处理**：支持选项重载（file/sname 字段包含选项）、子网掩码、路由器、DNS、租约时间、T1/T2、服务器 ID、NTP 服务器。可通过 `LWIP_HOOK_DHCP_APPEND_OPTIONS`/`LWIP_HOOK_DHCP_PARSE_OPTION` 扩展自定义选项。

**安全**：随机事务 ID (`DHCP_CREATE_RAND_XID`)、ACD 地址冲突检测 (`LWIP_DHCP_DOES_ACD_CHECK`)。

#### 5.6.2 AutoIP

地址范围 169.254.1.0 ~ 169.254.254.255 (RFC 3927)。从 MAC 地址最后 2 字节生成种子地址，使用 ACD 探测。可与 DHCP 协同工作 (`LWIP_DHCP_AUTOIP_COOP`)——DHCP 尝试 `LWIP_DHCP_AUTOIP_COOP_TRIES` 次失败后启动 AutoIP。

#### 5.6.3 ACD (地址冲突检测, RFC 5227)

状态机：`PROBE_WAIT → PROBING (3次探测) → ANNOUNCE_WAIT → ANNOUNCING (2次公告) → ONGOING (冲突时防御)`

- **探测**：发送源 IP=0.0.0.0 的 ARP 请求，间隔 1-2 秒随机
- **公告**：发送源 IP=目标IP 的 ARP 请求（免费 ARP），间隔 2 秒
- **冲突防御**：在 ONGOING 状态检测到冲突时，10 秒内只防御一次（`DEFEND_INTERVAL`），超过则退让
- **速率限制**：冲突超过 10 次后进入 60 秒冷却

### 5.7 IGMP / MLD

#### 5.7.1 IGMP (IPv4 组管理)

每个 netif 维护一个组链表。组状态：NON_MEMBER / DELAYING_MEMBER / IDLE_MEMBER。

- **加入组**：立即发送 V2 Membership Report，启动 500ms 延迟报告定时器
- **离开组**：如果是最后一个报告者，发送 Leave Group 到 224.0.0.2
- **收到查询**：为所有组启动延迟报告定时器（随机化）
- **收到报告**：同组的 DELAYING_MEMBER 取消定时器（别人已经报告了）
- 所有 IGMP 消息 TTL=1，包含 Router Alert IP 选项

#### 5.7.2 MLD (IPv6 组监听)

MLDv1 实现，类似 IGMP v2。组地址为 IPv6 组播地址。MLD 消息包含 Hop-by-Hop Router Alert 选项，跳数限制=1。

### 5.8 DNS 解析器

#### 5.8.1 架构

- `dns_table[DNS_TABLE_SIZE]`：解析缓存
- `dns_requests[DNS_MAX_REQUESTS]`：待处理请求回调
- `dns_servers[DNS_MAX_SERVERS]`：DNS 服务器地址
- 使用 UDP 端口 53（mDNS 使用 5353）

#### 5.8.2 解析流程

1. 检查 "localhost"
2. 尝试 `ipaddr_aton()` 解析数字 IP
3. 查本地缓存 `dns_lookup()`
4. 双栈回退（先 IPv4 再 IPv6 或反之）
5. mDNS 检查 ".local" 后缀
6. 入队 `dns_enqueue()` → 返回 ERR_INPROGRESS

#### 5.8.3 安全特性

- 随机事务 ID (`LWIP_DNS_SECURE_RAND_XID`)
- 随机源端口 (`LWIP_DNS_SECURE_RAND_SRC_PORT`)
- 禁止重复待处理请求 (`LWIP_DNS_SECURE_NO_MULTIPLE_OUTSTANDING`)
- 响应源地址验证 (RFC 5452)

---

## 6. 传输层实现

### 6.1 TCP

TCP 是 lwIP 中最复杂的协议，由三个文件实现。

#### 6.1.1 TCP PCB 结构

```c
struct tcp_pcb {
    // 公共 IP PCB 字段
    ip_addr_t local_ip, remote_ip;
    u16_t local_port, remote_port;
    u8_t ttl, tos;
    
    enum tcp_state state;      // 连接状态
    u8_t flags;                // TF_ACK_DELAY, TF_ACK_NOW, TF_INFR, TF_NODELAY...
    
    // 接收变量
    u32_t rcv_nxt;             // 期望的下一个序列号
    tcpwnd_size_t rcv_wnd;     // 可用接收窗口
    tcpwnd_size_t rcv_ann_wnd; // 要通告的窗口
    u32_t rcv_ann_right_edge;  // 通告的窗口右边缘
    
    // 重传定时器
    s16_t rtime;               // 重传定时器 (-1=未运行)
    u16_t mss;                 // 最大段大小
    
    // RTT 估计 (Van Jacobson/Karels 算法)
    u32_t rttest;              // RTT 测量开始时间 (0=未测量)
    u32_t rtseq;               // 被计时的序列号
    s16_t sa, sv;              // 平滑 RTT 平均值和方差
    s16_t rto;                 // 重传超时 (以 TCP_SLOW_INTERVAL 为单位)
    u8_t nrtx;                 // 重传次数
    
    // 拥塞控制
    tcpwnd_size_t cwnd;        // 拥塞窗口
    tcpwnd_size_t ssthresh;    // 慢启动阈值
    tcpwnd_size_t bytes_acked; // 已确认字节数
    
    // 发送变量
    u32_t snd_nxt;             // 下一个要发送的序列号
    u32_t snd_wl1, snd_wl2;   // 最后窗口更新的序列号/确认号
    tcpwnd_size_t snd_wnd;     // 发送窗口
    tcpwnd_size_t snd_buf;     // 可用发送缓冲区
    u16_t snd_queuelen;        // 发送队列中的 pbuf 数
    
    // 段队列
    struct tcp_seg *unsent;    // 未发送的段
    struct tcp_seg *unacked;   // 已发送未确认的段
    struct tcp_seg *ooseq;     // 乱序接收的段 (TCP_QUEUE_OOSEQ)
    
    // 回调函数
    tcp_sent_fn sent;
    tcp_recv_fn recv;
    tcp_connected_fn connected;
    tcp_poll_fn poll;
    tcp_err_fn errf;
    
    // 保活和持久定时器
    u32_t keep_idle;
    u8_t persist_backoff, persist_probe;
    
    // 窗口缩放 (LWIP_WND_SCALE)
    u8_t snd_scale, rcv_scale;
};
```

#### 6.1.2 TCP 状态机

```
                          应用 close
                    +--------------------------+
                    |                          |
                    v                          |
  CLOSED --connect--> SYN_SENT --SYN+ACK--> ESTABLISHED --close--> FIN_WAIT_1
     |                     |                    |                     |
     |                  RST/timeout          FIN received          ACK of FIN
     |                     |                    |                     |
     v                     v                    v                    v
  CLOSED              CLOSED              CLOSE_WAIT             FIN_WAIT_2
                                              |                     |
                                           close                FIN received
                                              |                     |
                                              v                     v
                                          LAST_ACK             TIME_WAIT
                                              |                     |
                                          ACK received          2*MSL
                                              |                     |
                                              v                     v
                                           CLOSED              CLOSED

  LISTEN --SYN received--> SYN_RCVD --ACK--> ESTABLISHED
                               |
                            close/RST
                               |
                               v
                           FIN_WAIT_1
```

PCB 分属四个链表：
- `tcp_bound_pcbs`：已绑定但未连接/监听
- `tcp_listen_pcbs`：LISTEN 状态
- `tcp_active_pcbs`：SYN_SENT ~ LAST_ACK（不含 TIME_WAIT）
- `tcp_tw_pcbs`：TIME_WAIT 状态

#### 6.1.3 拥塞控制

**初始拥塞窗口** (RFC 2581)：
```c
cwnd = min(4*MSS, max(2*MSS, 4380))
```

**慢启动** (cwnd < ssthresh)：
- 每收到一个 ACK，cwnd 增加 `min(acked, 2*MSS)` (RFC 3465 ABC)
- RTO 后限制为每次 1*MSS

**拥塞避免** (cwnd >= ssthresh)：
- 累计已确认字节，每累积一个 cwnd 增加 1*MSS（约每 RTT 增加 MSS）

**快速重传** (3 次重复 ACK)：
- `ssthresh = min(cwnd, snd_wnd) / 2`（最小 2*MSS）
- `cwnd = ssthresh + 3*MSS`（膨胀 3 个段）
- 设置 `TF_INFR` 标志
- 立即重传最早的未确认段
- 重置重传定时器

**快速恢复** (TF_INFR 期间)：
- 每个额外重复 ACK：`cwnd += MSS`（继续膨胀）
- 收到新数据 ACK：`cwnd = ssthresh`（收缩），清除 `TF_INFR`

**RTO 拥塞控制**：
- `ssthresh = min(cwnd, snd_wnd) / 2`（最小 2*MSS）
- `cwnd = 1*MSS`（重置为 1 个段）
- 所有未确认段移回 unsent 队列重传

#### 6.1.4 RTT 估计 (Van Jacobson/Karels 算法)

```
测量开始: rttest = tcp_ticks; rtseq = seqno;

测量完成 (ACK 覆盖 rtseq):
m = tcp_ticks - rttest              // 测量的 RTT (500ms tick)
m = m - (sa >> 3)                   // 误差 = 测量值 - 平滑平均
sa += m                             // SA = 7/8 * SA + 1/8 * m
if (m < 0) m = -m                   // 绝对值
m = m - (sv >> 2)                   // 偏差
sv += m                             // SV = 3/4 * SV + 1/4 * |m|
rto = (sa >> 3) + sv               // RTO = SA + 4*SV

重传时 (Karn 算法): rttest = 0     // 取消测量（避免重传歧义）
重传退避: rto = rto << backoff[nrtx]  // 指数退避: {1,2,3,4,5,6,7,7,7,...}
```

#### 6.1.5 发送队列管理

**`tcp_write()`**：将数据入队，三个阶段：
1. **Oversize 填充**：将数据填入最后一个 unsent pbuf 的未用尾部（TCP_OVERSIZE）
2. **Pbuf 链扩展**：链接新 pbuf 到最后一个 unsent 段
3. **新段创建**：为剩余数据创建新的 `tcp_seg`，每个不超过 MSS

**`tcp_output()`**：从 unsent 队列发送段到 unacked 队列：
- 计算有效窗口 `wnd = min(snd_wnd, cwnd)`
- 遍历 unsent 队列，窗口允许则发送
- 检查 Nagle 算法
- 移动已发送段到 unacked 队列（保持序列号排序）
- 窗口为 0 时启动持久定时器

#### 6.1.6 接收处理 (`tcp_receive()`)

**按序数据** (seqno == rcv_nxt)：
- 修剪超出接收窗口的数据
- 处理 ooseq 队列：移除已被覆盖的段，链接到 recv_data
- 更新 rcv_nxt，减少 rcv_wnd
- 设置 recv_data pbuf 交付应用
- 发送 ACK

**乱序数据** (seqno != rcv_nxt 但在窗口内)：
- 插入 ooseq 队列正确位置
- 修剪/丢弃重叠段
- 更新 SACK 范围
- 发送空 ACK（带 SACK 选项）

**ACK 处理**：
- 更新发送窗口（使用 snd_wl1/snd_wl2 防止旧窗口更新）
- 检测重复 ACK（5 个条件全满足）
- 新数据 ACK：重置 dupacks，更新 cwnd，释放已确认段，重置重传定时器

#### 6.1.7 SACK 支持

当 `LWIP_TCP_SACK_OUT` 启用时：
- `rcv_sacks[LWIP_TCP_MAX_SACK_NUM]` 数组存储 SACK 范围
- 收到乱序段时调用 `tcp_add_sack()` 添加 SACK 条目（RFC 2018）
- 发送空 ACK 时在选项中包含 SACK
- 收到按序数据时调用 `tcp_remove_sacks_lt()` 清理被覆盖的 SACK

#### 6.1.8 定时器

**慢定时器** (`tcp_slowtmr()`, 每 500ms)：
- 递增 `tcp_ticks`
- 最大重传检查：SYN_SENT 超过 `TCP_SYNMAXRTX`(6) 或其他超过 `TCP_MAXRTX`(12) → 移除 PCB
- 持久定时器：窗口为 0 时发送零窗口探测
- RTO 定时器：重传所有未确认数据
- FIN_WAIT_2 超时、保活探测、OOSEQ 超时、SYN_RCVD 超时、LAST_ACK 超时
- 轮询回调
- TIME_WAIT PCB 在 2*MSL 后移除

**快定时器** (`tcp_fasttmr()`, 每 250ms)：
- 发送延迟 ACK (TF_ACK_DELAY)
- 发送待处理 FIN (TF_CLOSEPEND)
- 重新交付被拒绝的数据

#### 6.1.9 PCB 内存回收

`tcp_alloc()` 在内存不足时按优先级回收 PCB：
1. 尝试 `memp_malloc(MEMP_TCP_PCB)`
2. 失败 → 发送待处理 FIN
3. 杀死最旧的 TIME_WAIT PCB
4. 杀死最旧的 LAST_ACK PCB
5. 杀死最旧的 CLOSING PCB
6. 杀死优先级较低的活跃 PCB

### 6.2 UDP

#### 6.2.1 UDP PCB

```c
struct udp_pcb {
    IP_PCB;
    struct udp_pcb *next;
    u8_t flags;           // NOCHKSUM, UDPLITE, CONNECTED, MULTICAST_LOOP
    u16_t local_port, remote_port;
    udp_recv_fn recv;     // 接收回调
    void *recv_arg;
};
```

#### 6.2.2 输入多路分用

- 遍历 `udp_pcbs` 链表
- 优先匹配"完美匹配"（已连接 PCB，同时匹配本地和远程地址+端口）
- 否则匹配第一个未连接但匹配本地地址+端口的 PCB
- 匹配的 PCB 移到链表头部（缓存局部性）
- `SO_REUSE_RXTOALL`：广播/组播包交付给所有匹配的 PCB

#### 6.2.3 输出

`udp_sendto_if_src()` 核心发送：
- 自动绑定本地端口（如果未绑定，从 49152-65535 分配）
- 预置 UDP 头部
- 计算伪头校验和（IPv6 强制校验和，IPv4 校验和 0 表示不校验）
- 处理组播环回和 TTL
- 调用 `ip_output_if_src()`

### 6.3 Raw API

Raw API 允许应用直接访问 IP 层，处理任何协议：

```c
struct raw_pcb {
    IP_PCB;
    struct raw_pcb *next;
    u8_t protocol;        // IP 协议号
    raw_recv_fn recv;     // 接收回调
    void *recv_arg;
};
```

`raw_input()` 遍历 `raw_pcbs` 链表，匹配协议号和地址。如果回调返回 1（吃掉包），则停止传递。Raw PCB 总是移到链表头部以优化缓存。

### 6.4 altcp 抽象层

altcp 实现**虚函数表模式**，允许在 TCP 上叠加应用层协议（如 TLS）而不修改应用代码：

```c
struct altcp_pcb {
    const struct altcp_functions *fns;  // 虚函数表
    struct altcp_pcb *inner_conn;       // 下一层连接
    void *state;                        // 层特定状态
    void *arg;                          // 应用回调参数
    // 应用回调...
};
```

层叠结构：TLS altcp_pcb → TCP altcp_pcb → tcp_pcb

`altcp_default_*` 函数简单转发到 `inner_conn`，允许中间层只覆盖需要的函数。TCP 后端 (`altcp_tcp.c`) 将 altcp 调用翻译为 `tcp_*` 调用。

---

## 7. API 层设计

### 7.1 三层 API 架构

```
应用代码
    │
    ├─→ Socket API (sockets.c) ──→ Netconn API (api_lib.c) ──→ api_msg.c ──→ Raw API
    │                                                                   │
    └─→ Raw API (直接使用 tcp_*/udp_*/raw_* 回调)                       │
                                                                        ↓
                                                               tcpip_thread 执行
```

- **Raw API**：最高性能，回调驱动，必须在 tcpip_thread 上下文使用
- **Netconn API**：线程安全，通过消息传递与 tcpip_thread 通信
- **Socket API**：在 Netconn 之上实现 POSIX 兼容接口

### 7.2 TCP/IP 线程模型

#### 7.2.1 线程创建

```c
void tcpip_init(tcpip_init_done_fn initfunc, void *arg) {
    lwip_init();                    // 初始化所有模块
    sys_mbox_new(&tcpip_mbox, TCPIP_MBOX_SIZE);  // 创建主邮箱
    // 如果 LWIP_TCPIP_CORE_LOCKING: 创建 lock_tcpip_core 互斥锁
    sys_thread_new(TCPIP_THREAD_NAME, tcpip_thread, ...);  // 创建线程
}
```

#### 7.2.2 主循环

```c
static void tcpip_thread(void *arg) {
    while (1) {
        tcpip_mbox_fetch(&tcpip_mbox, &msg);  // 阻塞等待消息 (含定时器处理)
        if (msg == NULL) continue;
        tcpip_thread_handle_msg(msg);          // 分发消息
    }
}
```

`tcpip_mbox_fetch()` 不是简单的阻塞等待——它计算到下一个定时器的睡眠时间，用该超时等待邮箱。超时则调用 `sys_check_timeouts()` 处理定时器。这优雅地将定时器处理集成到主循环中，无需单独的定时器线程。

#### 7.2.3 消息类型

| 类型 | 用途 |
|------|------|
| `TCPIP_MSG_API` | Netconn API 函数调用 |
| `TCPIP_MSG_API_CALL` | 同步 API 调用 (netifapi) |
| `TCPIP_MSG_INPKT` | 收到的数据包 |
| `TCPIP_MSG_TIMEOUT` | 注册超时 |
| `TCPIP_MSG_UNTIMEOUT` | 取消超时 |
| `TCPIP_MSG_CALLBACK` | 异步回调 |
| `TCPIP_MSG_CALLBACK_STATIC` | 预分配回调消息 (ISR 安全) |

#### 7.2.4 两种同步策略

**核心锁模式** (`LWIP_TCPIP_CORE_LOCKING=1`, 推荐)：
- 应用线程直接锁核心互斥锁，执行操作，解锁
- 无邮箱开销，无信号量创建/销毁

**消息传递模式** (`LWIP_TCPIP_CORE_LOCKING=0`)：
- 应用线程分配 `tcpip_msg`，投递到 `tcpip_mbox`，等待信号量
- tcpip_thread 执行函数后信号通知

### 7.3 Netconn API

#### 7.3.1 netconn 结构

```c
struct netconn {
    enum netconn_type type;    // TCP/UDP/RAW, IPv4/IPv6
    enum netconn_state state;  // NONE, WRITE, LISTEN, CONNECT, CLOSE
    union { struct tcp_pcb *tcp; struct udp_pcb *udp; struct raw_pcb *raw; } pcb;
    sys_sem_t op_completed;    // API 操作完成信号
    sys_mbox_t recvmbox;       // 接收数据队列
    sys_mbox_t acceptmbox;     // 待接受连接队列 (TCP)
    u32_t recv_timeout;        // SO_RCVTIMEO
    int recv_bufsize;          // SO_RCVBUF
    u8_t flags;                // NON_BLOCKING, MBOXCLOSED...
    struct api_msg *current_msg; // 进行中的写/关操作
    netconn_callback callback; // 事件回调
};
```

#### 7.3.2 线程边界跨越

每个 Netconn API 函数遵循统一模式：

```c
err_t netconn_X(struct netconn *conn, ...) {
    API_MSG_VAR_DECLARE(msg);        // 声明 api_msg
    API_MSG_VAR_ALLOC(msg);          // 分配 (MPU 模式从池分配)
    // 填充 msg 字段...
    err = netconn_apimsg(lwip_netconn_do_X, &msg);  // 跨线程
    API_MSG_VAR_FREE(msg);
    return err;
}
```

`netconn_apimsg()` 调用 `tcpip_send_msg_wait_sem()`：
- 核心锁模式：`LOCK_TCPIP_CORE(); fn(apimsg); UNLOCK_TCPIP_CORE();`
- 消息模式：分配 tcpip_msg，投递邮箱，等待 `op_completed` 信号量

#### 7.3.3 TCP 回调桥接

`setup_tcp()` 在 PCB 上注册回调，将 TCP 事件翻译为 Netconn 事件：

| TCP 回调 | 作用 |
|---------|------|
| `recv_tcp()` | 将 pbuf 投递到 recvmbox；NULL pbuf 表示 FIN |
| `sent_tcp()` | 继续写/关操作，检查写空间通知 select |
| `poll_tcp()` | 每 2 秒检查写/关操作重试 |
| `err_tcp()` | 设置 pending_err，唤醒所有阻塞线程 |
| `accept_function()` | 分配新 netconn，投递到 acceptmbox |

### 7.4 Socket API

#### 7.4.1 套接字表

```c
static struct lwip_sock sockets[NUM_SOCKETS];  // NUM_SOCKETS = MEMP_NUM_NETCONN

struct lwip_sock {
    struct netconn *conn;      // 底层 netconn
    union lwip_sock_lastdata lastdata;  // 上次接收的剩余数据
    s16_t rcvevent;            // 接收事件计数器
    u16_t sendevent;           // 发送就绪标志
    u16_t errevent;            // 错误标志
    SELWAIT_T select_waiting;  // select 等待者计数
};
```

#### 7.4.2 事件回调桥接

`event_callback()` 是 netconn 事件到 socket 事件计数的桥梁：

- `NETCONN_EVT_RCVPLUS`：`rcvevent++`
- `NETCONN_EVT_RCVMINUS`：`rcvevent--`
- `NETCONN_EVT_SENDPLUS`：`sendevent = 1`
- `NETCONN_EVT_SENDMINUS`：`sendevent = 0`
- `NETCONN_EVT_ERROR`：`errevent = 1`

如果 `select_waiting > 0` 且事件匹配，则唤醒 select 等待者。

#### 7.4.3 支持的套接字选项

| Level | 选项 |
|-------|------|
| SOL_SOCKET | SO_REUSEADDR, SO_KEEPALIVE, SO_BROADCAST, SO_TYPE, SO_ERROR, SO_SNDTIMEO, SO_RCVTIMEO, SO_RCVBUF, SO_LINGER, SO_NO_CHECK, SO_BINDTODEVICE, SO_ACCEPTCONN |
| IPPROTO_IP | IP_TTL, IP_TOS, IP_PKTINFO, IP_MULTICAST_TTL/IF/LOOP, IP_ADD/DROP_MEMBERSHIP |
| IPPROTO_TCP | TCP_NODELAY, TCP_KEEPALIVE, TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT |
| IPPROTO_IPV6 | IPV6_V6ONLY, IPV6_CHECKSUM, IPV6_JOIN/LEAVE_GROUP |
| IPPROTO_UDPLITE | UDPLITE_SEND_CSCOV, UDPLITE_RECV_CSCOV |

### 7.5 select / poll 实现

#### 7.5.1 全局状态

```c
static struct lwip_sock sockets[NUM_SOCKETS];
static struct lwip_select_cb *select_cb_list;  // 等待中的 select/poll 链表
static volatile int select_cb_ctr;              // 代计数器 (无核心锁模式)
```

#### 7.5.2 select 流程

1. 标记所有 fd_set 中的套接字为"使用中"（防止 FULLDUP 模式释放）
2. **第一次扫描** `lwip_selscan()`：立即检查所有套接字
3. 有就绪 → 立即返回
4. 无就绪且超时=0 → 返回 0（轮询）
5. 需等待：
   a. 分配 `lwip_select_cb`，创建信号量
   b. 加入 `select_cb_list` 链表
   c. 递增所有相关套接字的 `select_waiting`
   d. **第二次扫描**（竞态条件处理：注册和扫描之间可能有事件到达）
   e. 仍无就绪 → `sys_arch_sem_wait(sem, timeout)`
   f. 递减 `select_waiting`，从链表移除
   g. **最终扫描**获取实际就绪集

#### 7.5.3 唤醒机制

当 `event_callback()` 更新事件计数器后，如果 `select_waiting > 0` 且需要检查等待者：
1. 遍历 `select_cb_list`
2. 对每个等待者，检查其 fd_set/poll_fds 是否包含当前套接字
3. 匹配则设置 `sem_signalled = 1` 并信号通知信号量

---

## 8. 网络接口驱动

### 8.1 以太网

#### 8.1.1 输入处理 (`ethernet_input()`)

1. 验证包长度 > 14 字节
2. 解析以太网头：目的 MAC、源 MAC、EtherType
3. VLAN 处理 (`ETHARP_SUPPORT_VLAN`)：解析 VLAN 头，应用检查
4. 设置 `p->if_idx`
5. 组播/广播检测：
   - IPv4 组播 `01:00:5e:xx:xx:xx` → `PBUF_FLAG_LLMCAST`
   - IPv6 组播 `33:33:xx:xx:xx:xx` → `PBUF_FLAG_LLMCAST`
   - 广播 `ff:ff:ff:ff:ff:ff` → `PBUF_FLAG_LLBCAST`
6. 协议分发：
   - 0x0800 → `ip4_input()`
   - 0x0806 → `etharp_input()`
   - 0x86DD → `ip6_input()`
   - 0x8863/0x8864 → PPPoE
   - 其他 → `LWIP_HOOK_UNKNOWN_ETH_PROTOCOL` 或丢弃

#### 8.1.2 输出处理 (`ethernet_output()`)

1. 可选 VLAN 插入
2. `pbuf_add_header()` 预留以太网头空间
3. 填充以太网头（类型、目的 MAC、源 MAC）
4. 调用 `netif->linkoutput()` 发送原始帧

#### 8.1.3 驱动模板

移植时需实现以下函数（参考 `contrib/examples/ethernetif/`）：

```c
// 硬件初始化
static void low_level_init(struct netif *netif) {
    netif->hwaddr_len = ETH_HWADDR_LEN;
    // 设置 MAC 地址
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    // 初始化硬件 (PHY, MAC, DMA)
}

// 发送
static err_t low_level_output(struct netif *netif, struct pbuf *p) {
    for (q = p; q != NULL; q = q->next) {
        // 发送 q->payload 中 q->len 字节
    }
    LINK_STATS_INC(link.xmit);
    return ERR_OK;
}

// 接收
static struct pbuf *low_level_input(struct netif *netif) {
    u16_t len = get_received_length();
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p) {
        // 将数据拷贝到 pbuf 链
    }
    return p;
}

// 输入分发
void ethernetif_input(struct netif *netif) {
    struct pbuf *p = low_level_input(netif);
    if (p) {
        netif->input(p, netif);  // ethernet_input 或 tcpip_input
    }
}

// 初始化回调
err_t ethernetif_init(struct netif *netif) {
    netif->name[0] = 'e'; netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->output_ip6 = ethip6_output;
    netif->linkoutput = low_level_output;
    low_level_init(netif);
    return ERR_OK;
}
```

### 8.2 SLIP

SLIP (RFC 1055) 是最简单的 IP over 串行线封装。

**控制字符**：
- `0xC0` (END)：包开始和结束
- `0xDB` (ESC)：转义字符
- `0xDC`：转义后的 END
- `0xDD`：转义后的 ESC

**封装**：发送 `END`，逐字节发送数据（遇到特殊字符转义），发送 `END`

**解封装**：两状态机（NORMAL / ESCAPE），逐字节处理

**三种接收模式**：
1. 专用 RX 线程 (`SLIP_USE_RX_THREAD`)：阻塞在 `sio_read()`
2. 轮询 (`slipif_poll()`)：从主循环调用，非阻塞
3. ISR 驱动 (`SLIP_RX_FROM_ISR`)：中断中调用 `slipif_received_byte()`，主循环处理完成队列

### 8.3 6LoWPAN

#### 8.3.1 IPHC 压缩 (RFC 6282)

`lowpan6_compress_headers()` 实现 IPHC 头部压缩：

**2 字节 IPHC 调度字**：
```
字节0: 011 TF NH HL
       011  = IPHC 调度标识
       TF   = 流量类别/流标签压缩
       NH   = 下一头压缩 (1=UDP NHC)
       HL   = 跳数限制压缩 (00=内联, 01=1, 10=64, 11=255)
字节1: C ID SA DA
       C    = 上下文标识扩展
       ID   = 源地址压缩 (SAC+SAM)
       M    = 目的组播
       DA   = 目的地址压缩 (DAC+DAM)
```

地址压缩模式：
- 模式 3：完全省略（MAC 地址完全匹配 IPv6 地址）
- 模式 2：16 位内联（链路本地 + 00ff:fe00:xxxx）
- 模式 1：64 位内联（链路本地 + 接口 ID）
- 模式 0：128 位内联（完整地址）

UDP NHC 压缩：端口可压缩到 1 字节（f0bx 模式），长度可省略（从数据报大小推断），校验和始终内联。

#### 8.3.2 IEEE 802.15.4 分片

IEEE 802.15.4 帧限制 127 字节。`lowpan6_frag()` 处理分片：
- FRAG1 头（4 字节）：dispatch + datagram_size + datagram_tag
- FRAGN 头（5 字节）：同上 + datagram_offset（8 字节为单位）
- 重组在 `lowpan6_input()` 中处理，2 秒超时

#### 8.3.3 6LoWPAN over BLE (RFC 7668)

无 802.15.4 头、无分片（BLE L2CAP 处理 MTU）。IPHC 压缩后直接发送。地址转换：6 字节 BLE MAC → 8 字节 EUI-64（插入 FF:FE）。

### 8.4 PPP

#### 8.4.1 PPP 协议栈

lwIP 的 PPP 实现源自 Paul Mackerras 的 pppd，包含：

| 子协议 | 文件 | 功能 |
|--------|------|------|
| LCP | lcp.c | 链路控制协议 (建立和配置链路) |
| IPCP | ipcp.c | IP 控制协议 (协商 IPv4 参数) |
| IPv6CP | ipv6cp.c | IPv6 控制协议 |
| PAP | upap.c | 密码认证协议 |
| CHAP | chap-new.c, chap-md5.c, chap_ms.c | 挑战握手认证 (MD5/MS-CHAP v1/v2) |
| EAP | eap.c | 扩展认证协议 |
| CCP/MPPE | ccp.c, mppe.c | 压缩控制 / 微软点对点加密 |
| FSM | fsm.c | 通用有限状态机 (RFC 1661) |
| VJ | vj.c | Van Jacobson TCP/IP 头压缩 |

#### 8.4.2 PPP 阶段

```
DEAD → HOLDOFF → INITIALIZE → ESTABLISH(LCP) → AUTHENTICATE(PAP/CHAP)
    → NETWORK(IPCP/IPv6CP) → RUNNING → TERMINATE → DISCONNECT → DEAD
```

#### 8.4.3 FSM 状态机 (RFC 1661)

10 个状态：INITIAL, STARTING, CLOSED, STOPPED, CLOSING, STOPPING, REQSENT, ACKRCVD, ACKSENT, OPENED

通过回调函数实现协议特定操作：`resetci`, `cilen`, `addci`, `ackci`, `nakci`, `rejci`, `reqci`, `up`, `down` 等。

#### 8.4.4 PPPoS (PPP over Serial)

使用 HDLC-like 帧 (RFC 1662)：
- 标志 `0x7E`，地址 `0xFF`，控制 `0x03`
- FCS：16 位 CRC (CCITT 多项式 0x8408)，好值 0xF0B8
- ACCM：32 位位图控制需要转义的控制字符 (0-31)
- 字节级状态机输入解码：PDIDLE → PDADDRESS → PDCONTROL → PDPROTOCOL1 → PDPROTOCOL2 → PDDATA

#### 8.4.5 PPPoE (PPP over Ethernet)

RFC 2516 实现：
- 发现阶段：PADI (广播) → PADO → PADR → PADS (获取会话 ID)
- 会话阶段：EtherType 0x8864，PPPoE 头 + PPP 载荷
- PADT 终止会话

### 8.5 网桥

IEEE 802.1D MAC 网桥实现：

- `bridgeif_init()`：创建网桥 netif，设置 etharp_output / ethip6_output / bridgeif_output
- `bridgeif_add_port()`：添加端口 netif，替换端口 input 为 `bridgeif_input`
- **输入** (`bridgeif_input()`)：学习源 MAC → FDB 更新；查目的 MAC → FDB 查找 → 转发到匹配端口（包括 CPU 端口）
- **输出** (`bridgeif_output()`)：从 IP 栈来的帧，查 FDB → 转发到端口
- **动态 FDB** (`bridgeif_fdb.c`)：线性扫描，自动学习，5 分钟老化
- **静态 FDB**：手动添加的 MAC→端口映射
- 未知单播 → 泛洪到所有端口（除接收端口）
- 组播/广播 → 泛洪

### 8.6 ZEP

ZEP (ZigBee Encapsulation Protocol) 通过 UDP 隧道传输 6LoWPAN/802.15.4 帧，用于测试和仿真。ZEP 头包含协议标识 "EX"、版本 2、类型（Data）、通道、设备 ID、CRC 模式、时间戳、序列号和长度。接收时剥离 ZEP 头和 CRC 尾部，传递给 `lowpan6_input()`。

---

## 9. 应用层协议

### 9.1 HTTP 服务器

| 特性 | 说明 |
|------|------|
| 文件系统 | `makefsdata` 工具将目录转为 C 源码 (`fsdata.c`)，编译进固件 |
| SSI | 解析 `<!--#tag-->` (和 `/*#tag*/`)，回调返回插入文本 |
| CGI | URL 模式匹配，处理器返回响应文件 |
| POST | 应用回调 `httpd_post_begin/receive_data/finished` |
| HTTPS | 通过 altcp_tls 支持 TLS |
| Keep-Alive | HTTP/1.1 持久连接 (`LWIP_HTTPD_SUPPORT_11_KEEPALIVE`) |
| 动态头 | 运行时根据文件扩展名生成 HTTP 头 |
| 虚拟文件 | `fs_open_custom()` 支持动态/虚拟文件 |

初始化：`httpd_init()` 在端口 80 创建监听 PCB（HTTPS 用 `httpd_inits()` 在 443）。

### 9.2 SNMP 代理

- 支持 SNMPv1/v2c/v3 (v3 实验性，使用 mbedTLS 加密)
- ASN.1 BER 编码/解码
- MIB-II (RFC 1213) 标准模块：system, interfaces, IP, ICMP, TCP, UDP, SNMP
- MIB 树结构：树节点（内部）+ 叶节点（标量/表）
- Trap 和 INFORM 通知
- 两种传输前端：Raw API (默认) 或 Netconn API (工作线程)
- GET / GETNEXT / SET / GETBULK 操作

### 9.3 SNTP 客户端

- 实现 SNTPv4 (RFC 4330)
- 48 字节请求/响应
- 单播轮询模式或广播监听模式
- Kiss-of-Death 处理
- 往返延迟补偿 (`SNTP_COMP_ROUNDTRIP`)
- 服务器可达性移位寄存器 (RFC 5905)
- 指数退避重试
- 通过 `SNTP_SET_SYSTEM_TIME(sec)` 钩子设置系统时钟
- 可从 DHCP/DHCPv6 获取 NTP 服务器

### 9.4 MQTT 客户端

- MQTT 3.1.1 (协议级别 4)
- QoS 0/1/2 支持
- Last Will and Testament
- Keep-Alive 心跳
- 通过 altcp_tls 支持 TLS (端口 8883)
- 输出环形缓冲区 (256 字节)
- 最多 4 个在飞请求，30 秒超时
- 连接状态：TCP_DISCONNECTED → TCP_CONNECTING → MQTT_CONNECTING → MQTT_CONNECTED

### 9.5 mDNS 响应器

- RFC 6762 (mDNS) + RFC 6763 (DNS-SD)
- 探测-公告机制：3 次探测 (250ms 间隔) → 2 次公告
- 冲突检测和速率限制
- 响应 A/AAAA/PTR/SRV/TXT/ANY 查询
- 已知答案抑制
- 名称压缩
- 服务搜索 (`LWIP_MDNS_SEARCH`)
- 组播地址：IPv4 `224.0.0.251`，IPv6 `FF02::FB`，端口 5353

### 9.6 其他应用

| 应用 | 协议 | 说明 |
|------|------|------|
| SMTP 客户端 | RFC 5321 | AUTH PLAIN/LOGIN，通过 altcp_tls 支持 SMTPS |
| TFTP | RFC 1350 | 服务器和客户端，512 字节块，5 次重传 |
| iPerf | iPerf2 TCP | 带宽测试，服务器/客户端模式 |
| NetBIOS | NBNS | 名称查询响应器 (仅 IPv4) |
| HTTP 客户端 | HTTP/1.1 | GET 请求，代理支持，磁盘下载 |
| altcp_proxyconnect | HTTP CONNECT | 通过 HTTP 代理建立 TCP 隧道 |

---

## 10. 配置系统

### 10.1 配置层次

```
opt.h (默认值)
  ↓ 包含
lwipopts.h (用户覆盖) ←── 移植者创建
  ↓ 包含
arch/cc.h (编译器/平台抽象)
```

`opt.h` 中每个选项使用 `#if !defined(X) || defined(__DOXYGEN__)` 保护，用户在 `lwipopts.h` 中 `#define` 即可覆盖。

### 10.2 关键配置选项分类

#### 系统选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `NO_SYS` | 0 | 1=无 OS (裸机) |
| `LWIP_TIMERS` | 1 | 定时器支持 |
| `LWIP_TCPIP_CORE_LOCKING` | 1 | 核心锁模式 |
| `SYS_LIGHTWEIGHT_PROT` | 1 | 临界区保护 |
| `LWIP_ASSERT_CORE_LOCKED` | 空 | 核心锁检查宏 |

#### 内存选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `MEM_SIZE` | 1600 | 堆大小 (字节) |
| `MEM_ALIGNMENT` | 1 | 对齐 (32 位系统用 4) |
| `MEMP_MEM_MALLOC` | 0 | 用堆替代池 |
| `MEM_USE_POOLS` | 0 | 用多大小池替代堆 |
| `MEM_LIBC_MALLOC` | 0 | 用 C 库 malloc |
| `MEM_OVERFLOW_CHECK` | 0 | 溢出检测 (0/1/2) |
| `MEMP_NUM_TCP_PCB` | 5 | 活跃 TCP 连接数 |
| `MEMP_NUM_TCP_SEG` | 16 | TCP 段数 |
| `PBUF_POOL_SIZE` | 16 | pbuf 池大小 |

#### TCP 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `TCP_MSS` | 536 | 最大段大小 |
| `TCP_WND` | 4*MSS | 接收窗口 |
| `TCP_SND_BUF` | 2*MSS | 发送缓冲区 |
| `TCP_MAXRTX` | 12 | 最大数据重传 |
| `TCP_SYNMAXRTX` | 6 | 最大 SYN 重传 |
| `LWIP_TCP_SACK_OUT` | 0 | SACK 支持 |
| `LWIP_WND_SCALE` | 0 | 窗口缩放 |
| `LWIP_TCP_TIMESTAMPS` | 0 | 时间戳 |
| `TCP_OVERSIZE` | TCP_MSS | 预分配超大小 |
| `TCP_QUEUE_OOSEQ` | 1 | 乱序排队 |

#### API 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `LWIP_NETCONN` | 1 | Netconn API |
| `LWIP_SOCKET` | 1 | Socket API |
| `LWIP_COMPAT_SOCKETS` | 1 | BSD 名称映射 |
| `LWIP_SO_RCVTIMEO` | 0 | 接收超时 |
| `LWIP_SO_RCVBUF` | 0 | 接收缓冲区大小 |
| `LWIP_SOCKET_SELECT` | 1 | select() |
| `LWIP_SOCKET_POLL` | 1 | poll() |

#### IPv6 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `LWIP_IPV6` | 0 | 启用 IPv6 |
| `LWIP_IPV6_NUM_ADDRESSES` | 3 | 每 netif IPv6 地址数 |
| `LWIP_IPV6_AUTOCONFIG` | =LWIP_IPV6 | SLAAC |
| `LWIP_IPV6_DUP_DETECT_ATTEMPTS` | 1 | DAD 探测次数 |
| `LWIP_ND6_NUM_NEIGHBORS` | 10 | 邻居缓存大小 |

#### 校验和选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `CHECKSUM_GEN_IP` | 1 | 生成 IP 校验和 |
| `CHECKSUM_CHECK_IP` | 1 | 校验 IP 校验和 |
| `LWIP_CHECKSUM_CTRL_PER_NETIF` | 0 | 每 netif 校验和控制 |

### 10.3 钩子 (Hooks)

lwIP 提供大量钩子扩展点（全部默认未定义，用户自定义）：

| 钩子 | 用途 |
|------|------|
| `LWIP_HOOK_IP4_ROUTE` | 自定义 IPv4 路由 |
| `LWIP_HOOK_IP6_ROUTE` | 自定义 IPv6 路由 |
| `LWIP_HOOK_ETHARP_GET_GW` | 自定义网关选择 |
| `LWIP_HOOK_VLAN_CHECK` | VLAN 包过滤 |
| `LWIP_HOOK_VLAN_SET` | 出帧 VLAN 标记 |
| `LWIP_HOOK_DHCP_APPEND_OPTIONS` | 添加自定义 DHCP 选项 |
| `LWIP_HOOK_DHCP_PARSE_OPTION` | 解析自定义 DHCP 选项 |
| `LWIP_HOOK_TCP_ISN` | 自定义 TCP 初始序列号 |
| `LWIP_HOOK_SOCKETS_SETSOCKOPT` | 自定义 setsockopt |
| `LWIP_HOOK_UNKNOWN_ETH_PROTOCOL` | 处理未知以太网协议 |
| `LWIP_HOOK_MEMP_AVAILABLE` | 内存池有空闲项时通知 |

---

## 11. 移植指南

### 11.1 移植概述

将 lwIP 移植到新平台需要创建以下文件：

```
项目目录/
├── arch/
│   ├── cc.h           # 编译器/平台抽象 (必须)
│   ├── sys_arch.h     # OS 类型定义 (NO_SYS=0 必须)
│   ├── sys_arch.c     # OS 抽象实现 (NO_SYS=0 必须)
│   ├── perf.h         # 性能 profiling (可选，通常空)
│   ├── bpstruct.h     # 结构体打包开始 (PACK_STRUCT_USE_INCLUDES 时)
│   └── epstruct.h     # 结构体打包结束
├── lwipopts.h         # lwIP 配置 (必须)
└── ethernetif.c       # 以太网驱动 (必须)
```

### 11.2 创建 cc.h

`cc.h` 是编译器和平台抽象文件，必须提供：

```c
#ifndef __CC_H__
#define __CC_H__

/* 1. 字节序 (必须) */
#define BYTE_ORDER LITTLE_ENDIAN  /* 或 BIG_ENDIAN */

/* 2. 随机数 (必须) */
#define LWIP_RAND() ((u32_t)rand()) /* 或硬件随机数 */

/* 3. 诊断输出 (必须) */
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while (0)

/* 4. 断言 (必须) */
#define LWIP_PLATFORM_ASSERT(x) do { \
    printf("Assert: %s, file %s, line %d\n", x, __FILE__, __LINE__); \
    while (1); \
} while (0)

/* 5. 临界区保护类型 */
typedef unsigned int sys_prot_t;

/* 6. 结构体打包 (根据编译器选择一种) */
/* GCC/Clang: 使用默认的 __attribute__((packed)) */
/* MSVC: 使用 PACK_STRUCT_USE_INCLUDES + bpstruct.h/epstruct.h */
#define PACK_STRUCT_USE_INCLUDES

/* 7. 格式字符串 (如果没有 inttypes.h) */
#define LWIP_NO_INTTYPES_H 1
#define U16_F "hu"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"
#define SZT_F "lu"

/* 8. errno 处理 */
/* 选项 A: 使用系统 errno */
#define LWIP_ERRNO_STDINCLUDE 1
/* 选项 B: lwIP 提供 errno */
/* #define LWIP_PROVIDE_ERRNO */

#endif /* __CC_H__ */
```

### 11.3 创建 sys_arch (NO_SYS=0)

#### 11.3.1 sys_arch.h

```c
/* 信号量类型 */
typedef struct { void *sem; } sys_sem_t;
#define sys_sem_valid(sema) ((sema)->sem != NULL)
#define sys_sem_set_invalid(sema) ((sema)->sem = NULL)

/* 互斥锁类型 */
typedef struct { void *mutex; } sys_mutex_t;
/* 同上 valid/invalid */

/* 邮箱类型 */
typedef struct { void *queue; } sys_mbox_t;
#define SYS_MBOX_NULL { NULL }

/* 线程类型 */
typedef unsigned long sys_thread_t;
```

#### 11.3.2 sys_arch.c 必须实现的函数

```c
/* 时间 */
u32_t sys_now(void) { /* 返回当前毫秒时间 */ }
u32_t sys_jiffies(void) { /* 返回滴答数 */ }

/* 初始化 */
void sys_init(void) { /* 初始化全局状态 */ }

/* 信号量 */
err_t sys_sem_new(sys_sem_t *sem, u8_t count) { /* 创建信号量 */ }
void sys_sem_signal(sys_sem_t *sem) { /* 释放信号量 */ }
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) {
    /* 等待信号量, timeout=0 表示无限等待
     * 返回等待的毫秒数, 超时返回 SYS_ARCH_TIMEOUT */
}
void sys_sem_free(sys_sem_t *sem) { /* 释放 */ }

/* 互斥锁 */
err_t sys_mutex_new(sys_mutex_t *mutex) { /* 创建递归互斥锁 */ }
void sys_mutex_lock(sys_mutex_t *mutex) { /* 加锁 */ }
void sys_mutex_unlock(sys_mutex_t *mutex) { /* 解锁 */ }
void sys_mutex_free(sys_mutex_t *mutex) { /* 释放 */ }

/* 邮箱 */
err_t sys_mbox_new(sys_mbox_t *mbox, int size) { /* 创建大小为 size 的邮箱 */ }
void sys_mbox_post(sys_mbox_t *mbox, void *msg) { /* 阻塞投递 */ }
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) { /* 非阻塞投递, 满返回 ERR_MEM */ }
err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg) { /* ISR 中投递 */ }
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
    /* 等待获取消息, 超时返回 SYS_ARCH_TIMEOUT */
}
err_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg) { /* 非阻塞获取 */ }
void sys_mbox_free(sys_mbox_t *mbox) { /* 释放 */ }

/* 线程 */
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread,
    void *arg, int stacksize, int prio) {
    /* 创建线程, 不允许失败 */
}

/* 临界区保护 (SYS_LIGHTWEIGHT_PROT=1) */
sys_prot_t sys_arch_protect(void) { /* 禁中断或锁调度器 */ }
void sys_arch_unprotect(sys_prot_t pval) { /* 恢复 */ }
```

### 11.4 创建 lwipopts.h

最小裸机配置示例：

```c
/* lwipopts.h - 裸机最小配置 */
#define NO_SYS 1
#define LWIP_TIMERS 1

#define MEM_ALIGNMENT 4
#define MEM_SIZE (10 * 1024)

#define LWIP_TCP 1
#define LWIP_UDP 1
#define TCP_MSS 536
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (2 * TCP_MSS)

#define PBUF_POOL_SIZE 16
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_UDP_PCB 4

#define LWIP_DHCP 1
#define LWIP_DNS 1

#define LWIP_STATS 1
```

带 OS 配置示例：

```c
/* lwipopts.h - 带 RTOS 配置 */
#define NO_SYS 0
#define SYS_LIGHTWEIGHT_PROT 1
#define LWIP_TCPIP_CORE_LOCKING 1

#define MEM_ALIGNMENT 4
#define MEM_SIZE (20 * 1024)

#define LWIP_NETCONN 1
#define LWIP_SOCKET 1
#define LWIP_COMPAT_SOCKETS 1
#define LWIP_SO_RCVTIMEO 1

#define LWIP_TCP 1
#define LWIP_UDP 1
#define TCP_MSS 1460
#define TCP_WND (20 * 1024)
#define TCP_SND_BUF (8 * 1024)

#define PBUF_POOL_SIZE 32
#define MEMP_NUM_TCP_PCB 10
#define MEMP_NUM_NETCONN 8

#define TCPIP_THREAD_STACKSIZE 1024
#define TCPIP_THREAD_PRIO 5
#define TCPIP_MBOX_SIZE 64

/* 核心锁检查 */
void sys_check_core_locking(void);
#define LWIP_ASSERT_CORE_LOCKED() sys_check_core_locking()
void sys_mark_tcpip_thread(void);
#define LWIP_MARK_TCPIP_THREAD() sys_mark_tcpip_thread()
```

### 11.5 实现以太网驱动

参见 8.1.3 节的驱动模板。关键要点：

1. **`low_level_init()`**：设置 MAC 地址、MTU、flags，初始化硬件
2. **`low_level_output()`**：遍历 pbuf 链发送数据
3. **`low_level_input()`**：分配 PBUF_POOL，拷贝接收数据
4. **`ethernetif_init()`**：设置 `netif->output = etharp_output`，`netif->linkoutput = low_level_output`

### 11.6 主程序

#### 裸机模式 (NO_SYS=1)

```c
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

int main(void) {
    struct netif netif;
    
    /* 硬件初始化 */
    hw_init();
    
    /* lwIP 初始化 (不创建线程) */
    lwip_init();
    
    /* 添加网络接口 */
    netif_add(&netif, &ipaddr, &netmask, &gw, NULL,
              ethernetif_init, ethernet_input);  // 注意: ethernet_input 而非 tcpip_input
    netif_set_default(&netif);
    netif_set_up(&netif);
    
    /* 启动应用 */
    httpd_init();
    
    /* 主循环 */
    while (1) {
        /* 检查链路状态 */
        if (link_state_changed()) {
            if (link_is_up()) netif_set_link_up(&netif);
            else netif_set_link_down(&netif);
        }
        
        /* 接收数据包 (轮询或从 ISR 队列取出) */
        ethernetif_input(&netif);
        
        /* 处理定时器 (关键!) */
        sys_check_timeouts();
    }
}
```

#### OS 模式 (NO_SYS=0)

```c
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

static void tcpip_init_done(void *arg) {
    sys_sem_t *init_sem = (sys_sem_t *)arg;
    struct netif *netif = (struct netif *)malloc(sizeof(struct netif));
    
    /* 在 tcpip_thread 上下文中添加接口 */
    netif_add(netif, &ipaddr, &netmask, &gw, NULL,
              ethernetif_init, tcpip_input);  // tcpip_input 将包投递到 tcpip_thread
    netif_set_default(netif);
    netif_set_up(netif);
    
    /* 启动应用 */
    httpd_init();
    
    /* 通知主线程初始化完成 */
    sys_sem_signal(init_sem);
}

int main(void) {
    sys_sem_t init_sem;
    
    /* 硬件初始化 */
    hw_init();
    
    /* 创建信号量 */
    sys_sem_new(&init_sem, 0);
    
    /* 初始化 lwIP (创建 tcpip_thread) */
    tcpip_init(tcpip_init_done, &init_sem);
    
    /* 等待初始化完成 */
    sys_arch_sem_wait(&init_sem, 0);
    sys_sem_free(&init_sem);
    
    /* 主循环 (不需要 sys_check_timeouts, tcpip_thread 处理) */
    while (1) {
        /* 可做其他工作或休眠 */
        sys_msleep(100);
    }
}
```

### 11.7 零拷贝接收实现

```c
/* 自定义 pbuf 结构 */
typedef struct {
    struct pbuf_custom p;    // 必须是第一个成员
    void *dma_descriptor;    // DMA 描述符
} my_custom_pbuf_t;

/* 自定义释放函数 */
void my_pbuf_free_custom(void *p) {
    my_custom_pbuf_t *my_pbuf = (my_custom_pbuf_t *)p;
    SYS_ARCH_DECL_PROTECT(old_level);
    
    invalidate_cpu_cache();  /* DMA 安全 */
    
    SYS_ARCH_PROTECT(old_level);
    free_rx_dma_descriptor(my_pbuf->dma_descriptor);  /* 归还 DMA 缓冲区 */
    LWIP_MEMPOOL_FREE(RX_POOL, my_pbuf);               /* 归还池 */
    SYS_ARCH_UNPROTECT(old_level);
}

/* RX 中断处理 */
void eth_rx_irq(void) {
    dma_descriptor *dma = get_rx_dma_descriptor();
    my_custom_pbuf_t *my_pbuf = LWIP_MEMPOOL_ALLOC(RX_POOL);
    
    my_pbuf->p.custom_free_function = my_pbuf_free_custom;
    my_pbuf->dma_descriptor = dma;
    
    /* 创建指向 DMA 缓冲区的自定义 pbuf -- 零拷贝! */
    struct pbuf *p = pbuf_alloced_custom(PBUF_RAW,
        dma->length, PBUF_REF,
        &my_pbuf->p,
        dma->data,
        dma->max_buffer_size);
    
    if (netif->input(p, netif) != ERR_OK) {
        pbuf_free(p);
    }
}
```

### 11.8 移植检查清单

| 步骤 | 文件 | 说明 |
|------|------|------|
| 1 | `arch/cc.h` | 编译器类型、字节序、打包、诊断、随机 |
| 2 | `lwipopts.h` | 所有功能开关和参数 |
| 3 | `arch/sys_arch.h` | OS 类型定义 (NO_SYS=0) |
| 4 | `arch/sys_arch.c` | OS 抽象实现 (NO_SYS=0) |
| 5 | `ethernetif.c` | 网络驱动 |
| 6 | `arch/bpstruct.h` + `epstruct.h` | 打包支持 (MSVC) |
| 7 | 主程序 | 初始化和主循环 |
| 8 | 构建系统 | 添加 lwIP 源文件和头文件路径 |

---

## 12. 网络原理

### 12.1 TCP/IP 协议栈原理

lwIP 实现了完整的 TCP/IP 四层模型：

| 层 | lwIP 实现 | 功能 |
|----|----------|------|
| 应用层 | apps/ (HTTP, MQTT, SNMP...) | 应用协议 |
| 传输层 | tcp.c, udp.c, raw.c | 端到端通信 |
| 网络层 | ip4.c, ip6.c, icmp.c | 路由和寻址 |
| 链路层 | ethernet.c, ppp/, slipif.c | 帧封装 |

### 12.2 TCP 可靠传输原理

lwIP 的 TCP 实现包含完整的可靠性机制：

1. **序列号和确认号**：每个字节有序列号，接收方通过 ACK 确认
2. **重传定时器 (RTO)**：发送后启动定时器，超时未确认则重传
3. **RTT 估计**：Van Jacobson/Karels 算法动态调整 RTO
4. **Karn 算法**：重传的段不参与 RTT 测量（避免重传歧义）
5. **指数退避**：每次重传 RTO 翻倍（上限 7 倍）
6. **滑动窗口**：接收窗口控制发送方速率，防止接收方溢出
7. **拥塞控制**：慢启动 + 拥塞避免 + 快速重传 + 快速恢复
8. **持久定时器**：窗口为 0 时定期发送探测
9. **保活定时器**：检测连接是否存活
10. **Nagle 算法**：合并小段，减少网络中小包

### 12.3 地址解析原理

**ARP (IPv4)**：
1. 发送 IP 包时查 ARP 缓存
2. 找到 → 直接发送
3. 未找到 → 发送 ARP 请求（广播），缓存待发数据包
4. 收到 ARP 回复 → 更新缓存，发送排队数据包
5. 缓存表项有超时和自动刷新机制

**邻居发现 ND6 (IPv6)**：
1. 类似 ARP 但使用 ICMPv6（NS/NA 消息）
2. 支持可达性确认（REACHABLE/STALE/DELAY/PROBE 状态机）
3. 支持路由器发现（RA/RS）和前缀发现
4. 集成 DAD（重复地址检测）
5. 支持 PMTU 发现

### 12.4 DHCP 工作原理

1. **DISCOVER**：客户端广播，寻找 DHCP 服务器
2. **OFFER**：服务器单播/广播响应，提供 IP 配置
3. **REQUEST**：客户端广播，请求特定配置
4. **ACK**：服务器确认，分配 IP 地址
5. 租约管理：T1(50%) 续租，T2(87.5%) 重绑，T0 到期释放

### 12.5 IPv6 自动配置原理

**SLAAC (RFC 4862)**：
1. 生成链路本地地址（EUI-64 从 MAC 派生）
2. DAD 探测（发送 NS，等待响应）
3. 发送 RS 请求路由器信息
4. 收到 RA 中的前缀信息 → 生成全局地址
5. DAD 探测全局地址
6. 管理地址生命周期（preferred → deprecated → invalid）

### 12.6 内存管理策略对比

| 策略 | 优点 | 缺点 | 适用场景 |
|------|------|------|---------|
| 内置堆 | 灵活，任意大小 | 碎片风险，O(n) 分配 | 通用，TX 缓冲 |
| 固定池 | O(1) 分配，无碎片 | 固定大小，需预估 | PCB, pbuf struct |
| C 库 malloc | 简单 | 不确定性行为 | 有足够 RAM 的系统 |
| 混合 (MEMP_MEM_MALLOC) | 池 API + 堆灵活性 | 仍有碎片风险 | 池不够用的场景 |

### 12.7 线程安全机制

lwIP 在多线程环境下的线程安全通过以下机制保证：

1. **单一 TCP/IP 线程**：所有核心操作在 tcpip_thread 中执行
2. **核心锁** (`LWIP_TCPIP_CORE_LOCKING`)：应用线程锁核心互斥锁后直接操作
3. **消息传递**：应用线程通过邮箱投递 API 消息到 tcpip_thread
4. **临界区保护** (`SYS_ARCH_PROTECT`)：短临界区使用中断屏蔽
5. **`LWIP_ASSERT_CORE_LOCKED()`**：运行时验证调用者在正确上下文中
6. **邮箱中断安全投递** (`sys_mbox_trypost_fromisr`)：ISR 中安全投递

### 12.8 pbuf 零拷贝原理

```
传统方式:  DMA缓冲区 → 拷贝 → pbuf → 拷贝 → 协议栈处理

零拷贝:    DMA缓冲区 ←── pbuf (PBUF_REF) → 协议栈处理 (直接读DMA缓冲区)
                    ↑                     ↓
                    └─── 自定义释放函数 ───┘ (归还DMA描述符)
```

通过 `pbuf_custom` + `pbuf_alloced_custom()` 实现，pbuf 直接指向 DMA 缓冲区，无需数据拷贝。释放时通过自定义函数归还 DMA 资源。

---

## 附录

### A. 关键文件快速索引

| 功能 | 文件路径 |
|------|---------|
| 初始化 | `src/core/init.c` |
| 堆内存 | `src/core/mem.c` |
| 内存池 | `src/core/memp.c` |
| 数据包缓冲区 | `src/core/pbuf.c` |
| 网络接口 | `src/core/netif.c` |
| 定时器 | `src/core/timeouts.c` |
| TCP 核心 | `src/core/tcp.c` |
| TCP 输入 | `src/core/tcp_in.c` |
| TCP 输出 | `src/core/tcp_out.c` |
| UDP | `src/core/udp.c` |
| DNS | `src/core/dns.c` |
| IPv4 | `src/core/ipv4/ip4.c` |
| ARP | `src/core/ipv4/etharp.c` |
| DHCP | `src/core/ipv4/dhcp.c` |
| ICMP | `src/core/ipv4/icmp.c` |
| IPv6 | `src/core/ipv6/ip6.c` |
| 邻居发现 | `src/core/ipv6/nd6.c` |
| Socket API | `src/api/sockets.c` |
| Netconn API | `src/api/api_lib.c` |
| TCP/IP 线程 | `src/api/tcpip.c` |
| 以太网 | `src/netif/ethernet.c` |
| PPP | `src/netif/ppp/` |
| 配置选项 | `src/include/lwip/opt.h` |
| 编译器抽象 | `src/include/lwip/arch.h` |
| OS 抽象 | `src/include/lwip/sys.h` |

### B. 配置选项快速参考

```c
/* === 系统模式 === */
#define NO_SYS 0              /* 0=带OS, 1=裸机 */
#define SYS_LIGHTWEIGHT_PROT 1 /* 临界区保护 */
#define LWIP_TCPIP_CORE_LOCKING 1 /* 核心锁 */

/* === 内存 === */
#define MEM_ALIGNMENT 4       /* 对齐字节 */
#define MEM_SIZE (16*1024)   /* 堆大小 */
#define MEMP_NUM_TCP_PCB 10   /* TCP PCB数 */
#define PBUF_POOL_SIZE 32     /* pbuf池大小 */

/* === TCP === */
#define TCP_MSS 1460          /* 最大段大小 */
#define TCP_WND (20*1024)     /* 接收窗口 */
#define TCP_SND_BUF (8*1024)  /* 发送缓冲区 */

/* === IPv4/v6 === */
#define LWIP_IPV4 1
#define LWIP_IPV6 0           /* 按需启用 */
#define LWIP_DHCP 1           /* DHCP客户端 */
#define LWIP_DNS 1            /* DNS解析器 */

/* === API === */
#define LWIP_NETCONN 1        /* Netconn API */
#define LWIP_SOCKET 1         /* Socket API */
#define LWIP_COMPAT_SOCKETS 1 /* BSD名称映射 */

/* === 统计 === */
#define LWIP_STATS 1          /* 统计收集 */
#define LWIP_STATS_DISPLAY 1  /* 统计显示函数 */
```

### C. 常见问题

**Q: 如何选择 NO_SYS=0 还是 NO_SYS=1?**

A: 如果项目使用 RTOS 且需要 Socket/Netconn API，选 NO_SYS=0。如果是裸机或只使用 Raw API，选 NO_SYS=1 可减少代码量和依赖。

**Q: TCP 发送缓冲区不够怎么办?**

A: 增大 `TCP_SND_BUF` 和 `TCP_SND_QUEUELEN`，同时增大 `MEMP_NUM_TCP_SEG`。考虑启用 `LWIP_WND_SCALE` 以支持更大的窗口。

**Q: 如何优化性能?**

A: (1) 增大 `TCP_MSS` (通常 1460); (2) 启用窗口缩放 `LWIP_WND_SCALE=1`; (3) 增大 `TCP_WND` 和 `TCP_SND_BUF`; (4) 增大 `PBUF_POOL_SIZE`; (5) 禁用不需要的统计; (6) 使用硬件校验和卸载 (`CHECKSUM_GEN_*=0`, `CHECKSUM_CHECK_*=0`); (7) 使用零拷贝接收。

**Q: 如何减少内存使用?**

A: (1) 减小 `MEM_SIZE` 和各 `MEMP_NUM_*`; (2) 禁用不需要的协议; (3) 减小 `PBUF_POOL_SIZE` 和 `PBUF_POOL_BUFSIZE`; (4) 使用 `MEMP_MEM_MALLOC=1` 让池从堆分配; (5) 减小 `TCP_WND` 和 `TCP_SND_BUF`; (6) 禁用统计 `LWIP_STATS=0`。

**Q: ARP 表项不够怎么办?**

A: 增大 `ARP_TABLE_SIZE`。如果仍有问题，检查是否有大量不同 IP 的通信（如扫描器），考虑使用 `ETHARP_SUPPORT_STATIC_ENTRIES` 添加静态条目。

---

*本文档基于 lwIP STABLE-2_2_1_RELEASE 源码分析生成，涵盖全部核心模块、协议实现、API 层、驱动层、应用层及移植相关内容。*
