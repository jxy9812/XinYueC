/* ESP8266 NONOS3 阶段最小化 lwipopts（替代仓库 Library/lwip/include/lwipopts.h）
 *
 * 与仓库版差异（本阶段有意为之，见汇报“遗留问题”）：
 *   - 不依赖 XMemory.h / XNetwork_config.h / XPrintf.h（桌面工程头无法在 xtensa 裸编）
 *   - LWIP_DHCP=0（DHCP 客户端关闭，与 esp 侧 lwip-esp.o 的 dhcp_* 桩避免符号冲突）
 *   - LWIP_DNS=0 / LWIP_IGMP=0 / LWIP_SOCKET=0 / LWIP_NETCONN=0（本阶段仅 ICMP）
 *   - LWIP_TIMERS_CUSTOM=0：仓库版 =1 时 core/timeouts.c 整体不编译（无 sys_timeout /
 *     sys_check_timeouts 实现），而 esp 侧桩已占用 sys_timeout 名字；本阶段改用
 *     lwIP 自带轮询定时器，由 app.c 的 os_timer 每 5ms 调 sys_check_timeouts()。
 *     （仓库要求的 sys_timeouts_call() 在本仓库 lwip 2.2.1 中不存在，见 timeouts.c。）
 *   - SYS_LIGHTWEIGHT_PROT=1（驱动以 rsil 实现 sys_arch_protect，防 pp/tmr 任务交错）
 *   - 内存走 MEM_CUSTOM_* = XMalloc_Hybrid/XFree_Hybrid/XCalloc_Hybrid（XMemory 两池）
 * 保持不变：NO_SYS=1、LWIP_ICMP=1、LWIP_ARP/LWIP_ETHERNET=1、LWIP_NETIF_LOOPBACK=1。
 */
#ifndef ESP8266_STAGE_LWIPOPTS_H
#define ESP8266_STAGE_LWIPOPTS_H

/* ---------- 调试 ---------- */
#define LWIP_DBG_MIN_LEVEL      LWIP_DBG_LEVEL_ALL
/* LWIP_DEBUG 不定义（关闭，终版）；诊断阶段曾开 WARNING 级（丢包原因输出）。
 * 需要时可重新打开：#define LWIP_DEBUG 1 + LWIP_DBG_TYPES_ON，
 * 输出走 LWIP_PLATFORM_DIAG -> snprintf -> newlib -> _write_r(stubs 流控)。 */
/* LWIP_STATS=0 省 RAM */

/* ---------- OS ---------- */
#define NO_SYS                  1
#define SYS_LIGHTWEIGHT_PROT    1
#define LWIP_TCPIP_CORE_LOCKING 0

/* ---------- 协议 ---------- */
#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_RAW                0
#define LWIP_ICMP               1
#define LWIP_ARP                1
#define LWIP_ETHERNET           1
#define LWIP_DHCP               0
#define LWIP_AUTOIP             0
#define LWIP_ACD                0
#define LWIP_DNS                0
#define LWIP_IGMP               0

/* ---------- 定时器 ---------- */
#define LWIP_TIMERS             1
#define LWIP_TIMERS_CUSTOM      0      /* 关键：启用 core/timeouts.c 轮询实现 */

/* ---------- 内存 ---------- */
#define MEM_CUSTOM_ALLOCATOR    1
/* 本轮总装：lwip 的内存全部走框架自带的 XMemory 混合槽位——
 * <=XMEMORY_HYBRID_THRESHOLD(512B) 走全局多级固定池，>512B 走全局 TLSF
 * 可变池（裸机下 arena 为 .bss 静态缓冲）。没有裸 malloc 直连，故不存在
 * 绕过池的分配路径。
 * 这里不 include XMemory.h（xtensa 裸编链不进仓库公共头），按原型前向声明
 * ——签名与 Src/XMemory/XMemory.h:204/244/322 完全一致。
 * lwip 的 opt.h 在本文件【之前】被包含（Library/lwip/include/lwip/opt.h:51），
 * 此时 stddef 未必到位，size_t 未定义 → 本轮构建实证报 unknown type name。
 * 故此处自带 stddef（本文件只用到 size_t、void 指针与 NULL 宏）。 */
#include <stddef.h>
/* 2026-10-20 最终修法：lwip 改用 SDK 堆（os_malloc）。
 * 教训：把 lwip 放进 .bss 的 XMemory arena 后，收包 pbuf 与框架对象争抢
 * 同一块 28KB 内存，arena 打满时 pbuf_alloc 失败 → WiFi 表现为"变哑"
 * （rxReq 涨、rxOk 不涨）。SDK 堆与 arena 互不挤占，且底座固件正是靠
 * 充裕的堆（h_alloc 覆盖整段剩余 DRAM）跑出 0% 丢包。 */
extern void* os_malloc(size_t size);
extern void* os_zalloc(size_t size);
extern void  os_free(void* ptr);
extern void* xesp_lwip_calloc(size_t count, size_t size);  /* 实现见 XDeviceNetwork_lwip_esp8266.c */
/* 2026-10-21 泄漏定案：lwip 全部内存改走 SDK 堆（os_malloc/os_free）。
 * esp 胶水层（lwip-esp.o）以 esp 布局/引用协议操作这些指针，arena 的
 * TLSF 块头会被改写（ref 写成 245+ 垃圾值）导致 pbuf 永不释放——每 ping
 * 漏 92B、arena 两分钟打满。SDK 堆与其配套，从结构上消除该冲突。 */
#define MEM_CUSTOM_MALLOC       os_malloc
#define MEM_CUSTOM_FREE         os_free
#define MEM_CUSTOM_CALLOC       xesp_lwip_calloc
#define MEMP_MEM_MALLOC         1
#define MEM_ALIGNMENT           4
#define MEM_SIZE                (32 * 1024)   /* MEM_CUSTOM 下不建静态堆，仅占位 */

/* ⚠️ 总装轮瘦身（2026-10-18）：MEM_SIZE 在 MEM_CUSTOM_ALLOCATOR=1 下只是占位
 * （见上），真正吃堆的是 MEMP 池与 PBUF_POOL —— 它们由 memp_init_pool 在
 * lwip_init 时一次性 malloc，占用实测是「开机 boot heap 从满堆掉到 16.3KB」
 * 那一档的主体：PBUF_POOL_BUFSIZE 默认 = LWIP_MEM_ALIGN_SIZE(536+20+20+14)
 * = 592B，PBUF_POOL_SIZE=16 ⇒ 单这一池 ~9.7KB。下面的取值按「够 ping 通」
 * 收敛，TCP 侧只保证本轮验收（ICMP）不受影响。 */
/* ⚠ 总装第 2 轮（2026-10-19）：这一组池是【arena 的头号占用者】，全部从
 * XMemory 可变池 arena 里出（MEM_CUSTOM_ALLOCATOR=1，MEMP_MEM_MALLOC=1）。
 * R7 真机实测（cap_R7_shell.log）：after-init 时 varPoolUsed=12992/14304
 * （91%），只剩 1312B；随后 SDK 胶水层 lwip-esp.c:221 的
 * pbuf_wrapper_get() → pvPortMalloc(8 × sizeof(pbuf_wrapper) ≈ 352B)
 * 拿不到 → glue2esp_linkoutput 返回 GLUE_ERR_MEM(-1)，串口打
 * "[lwip] TX FAILED by esp side, code=-1"；再往下 ieee80211_hostap_attach
 * 里同一次分配失败的 NULL 被解引用 → Fatal exception 28
 * (LoadProhibited, excvaddr=0x10, epc1=0x4023aed5) → 0.7s 重启循环，
 * cap_R7_shell.log 里 86 次 oled init FAIL。
 * arena 不能涨（_bss_end 必须 < 0x3FFFC000，见 xgui_shell_esp8266.h:190-196），
 * 只能从这里砍。PBUF_POOL 8×592B≈4.7KB、MEMP_TCP_SEG 8×~600B≈4.8KB 是两块
 * 最大死重；本轮验收只走 ICMP + ARP（LWIP_ICMP=1 / SOCKET=0 / NETCONN=0），
 * TCP/UDP 段池降到 2 已足够，池空时 lwip 会退回 mem_malloc，不硬失败。 */
/* 2026-10-23 lwh 耗尽修复：shell RAM 裁剪后 lwh 缩到 16,840B，低于 P0
 * 定案验证配比（21.4K）。lwip 池常驻 ~16.3K 几乎吃满，运行期收包 pbuf
 * （MEMP_MEM_MALLOC=1 下全走 os_malloc）无空间 → 跑 60+ 包后 allocFail
 * 永久（xesp_lwh_free=0 实证耗尽）。当前固件只用 ICMP+ARP，TCP 池是
 * 最大死重，砍到最小（池空时 lwip 退回 mem_malloc，非硬失败）：
 *   池常驻 16.3K→~8K，给运行期留 ~8.8K。
 * ⚠ 注意：与 2026-10-23 早间的"砍池无效"实验不同——那次同时做了
 * ensure 前置（已证实有害并回退），本次是单独变量。 */
#define MEMP_NUM_PBUF           8      /* 16→8（lwh 耗尽修复） */
#define MEMP_NUM_UDP_PCB        1      /* 2 → 1 */
#define MEMP_NUM_TCP_PCB        1      /* 4→1（ICMP+ARP 场景 TCP 死重） */
#define MEMP_NUM_TCP_PCB_LISTEN 1      /* 不变 */
#define MEMP_NUM_TCP_SEG        2      /* 8→2（ICMP+ARP 场景 TCP 死重） */
/* 2026-10-19：4 → 8。SoftAP 上 ARP 定时器 + DHCP(2) + TCP PCB 已把 4 档占满，
 * 再申请就返回 NULL → 包被静默丢弃、ping 无响应且日志无任何报错。 */
#define MEMP_NUM_SYS_TIMEOUT    8
#define MEMP_NUM_NETBUF         1      /* 2 → 1 */
#define MEMP_NUM_NETCONN        1
/* 收包 pbuf 池：2 → 4。arena 稳态余量实测 ~10KB（cap_R10_shell.log:184
 * varPoolUsed=14496/24544），这一轮把 128/256 两档固定池关掉又还回来 ~1KB，
 * 抬到 4 块（4 × 592B ≈ 2.4KB）仍有余量。池空时 pbuf_alloc 会退回 PBUF_RAM
 * （memp_malloc(MEMP_PBUF)），故只是把大包路径换成堆路径，不是硬失败。 */
/* 2026-10-19：4→8→6。TX 改 scratch 后 .bss 涨 3.2KB，池回调到 6 平衡。
 * 2026-10-22 回开 SoftAP 抬到 12（基线 28/30 版）。
 * 2026-10-23 lwh 耗尽修复：12→6（6×592≈3.5KB，与 TCP 池砍小合计把池
 * 常驻从 16.3K 压到 ~8K，给运行期收包留出 lwh 余量）。 */
#define PBUF_POOL_SIZE          6

/* ---------- TCP 参数（对齐 SDK lwip2-536 的省 RAM 取值） ---------- */
/* ⚠ 上一轮汇报把根因指向 Src/XCode/XNetwork/XNetwork_config.h:108-113 的
 * XNETWORK_LWIP_RECV/SEND_BUFFER_SIZE（默认 8192/8192），要求在 ESP8266 档案里
 * 覆盖成 1460/2920。本轮核实：那条路径对本固件【无效】——
 *   - 编译 lwip core 时 -I. (build/arduino_final) 排在 -I$REPO/include 之前
 *     且 lwip 源文件用 "lwipopts.h" 引号包含 → 生效的是【本文件】，
 *     不是 Library/lwip/include/lwipopts.h（本文件第 1 行即写明「替代仓库版」）；
 *   - 本文件不 include XNetwork_config.h，TCP_WND/TCP_SND_BUF 直接写死
 *     2*TCP_MSS = 1072，本来就不是 8192；
 *   - Library/lwip/platform/XDeviceNetwork_lwip.c（唯一消费这两个宏的文件）
 *     不在 filelist_final.txt（275 个文件，无 lwip/platform）。
 * 真正吃 arena 的是上面的 MEMP 池与 PBUF_POOL，不是 TCP 缓冲。 */
/* TCP_WND / TCP_SND_BUF 保持底座原值：init.c:300-322 的 lwip_sanity_check 硬性
 * 要求 TCP_SND_BUF ≥ 2*TCP_MSS，且这两个值并不在 init 期从池里分配（TCP_WND 是
 * PCB 结构体成员、TCP_SND_BUF 只决定 TCP_SND_QUEUELEN 的段数），调小它们省不到
 * 启动堆，只会把 TCP 行为改坏。真机实测（build_R2.log）：2*MSS→1*MSS 直接编译失败。*/
#define TCP_MSS                 536
#define TCP_WND                 (2 * TCP_MSS)
#define TCP_SND_BUF             (2 * TCP_MSS)
#define TCP_SND_QUEUELEN        ((4 * TCP_SND_BUF + (TCP_MSS - 1)) / TCP_MSS)
#define TCP_MAXRTX              6
#define TCP_SYNMAXRTX           4

/* ---------- IP/ARP ---------- */
#define IP_REASSEMBLY           0   /* 本轮无分片重组：去掉 ~1KB 的 reassembly 静态表 */
#define IP_FRAG                 0
#define ARP_TABLE_SIZE          8
#define ARP_MAXAGE              300

/* ---------- netif ---------- */
#define LWIP_NETIF_HOSTNAME            0
#define LWIP_NETIF_STATUS_CALLBACK     0
#define LWIP_NETIF_LINK_CALLBACK       0
#define LWIP_NETIF_LOOPBACK            0   /* 关 loopback netif 省一个 netif + 缓冲 */
#define LWIP_HAVE_LOOPIF               0
#define LWIP_NETIF_TX_SINGLE_PBUF      1
#define NETIF_MAX_HWADDR_LEN           6

/* ---------- API（关闭） ---------- */
#define LWIP_SOCKET            0
#define LWIP_NETCONN           0
#define LWIP_NETIF_API         0
#define LWIP_COMPAT_SOCKETS    0

/* ---------- 应用层（关闭） ---------- */
#define LWIP_HTTPD             0
#define LWIP_SNMP              0
#define PPP_SUPPORT            0
#define LWIP_MDNS_RESPONDER    0
#define LWIP_DHCP_AUTOIP_FALLBACK 0

/* ---------- 其他 ---------- */
#define LWIP_PROVIDE_ERRNO           1
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 0
#define LWIP_TIMEVAL_PRIVATE         0
#define LWIP_STATS                   0

/* 平台诊断/断言：断言走字符串直拼（不拉 newlib stdio），诊断走 snprintf */
#define LWIP_PLATFORM_DIAG(x)   do { char _dbuf[192]; snprintf(_dbuf, sizeof _dbuf, x); lwip_dbg_puts(_dbuf); } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { lwip_dbg_puts("ASSERT: " x "\n"); for (;;) { } } while (0)

void lwip_dbg_puts(const char* s);   /* 驱动提供：UART0 流控输出 */

#endif /* ESP8266_STAGE_LWIPOPTS_H */
