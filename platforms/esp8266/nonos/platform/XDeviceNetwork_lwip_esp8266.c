/* XDeviceNetwork_lwip_esp8266.c
 * 仓库 lwIP 2.2.1 × ESP8266 NONOS3 WiFi 的 glue 驱动（esp2glue_* 契约实现）。
 *
 * 完整数据链路（radio -> lwip -> radio）：
 *   RX: radio(pp/lmac) -> SDK blob -> lwip-esp.o 的 ethernet_input（esp 侧）
 *       -> esp2glue_alloc_for_recv（本文件：仓库 pbuf_alloc 分配 glue pbuf）
 *       -> esp2glue_ethernet_input（本文件：喂给仓库 lwip2 ethernet_input）
 *       -> ip4_input -> icmp_input（ICMP echo 应答）
 *   TX: lwip2 netif->linkoutput = drv_linkoutput（本文件）
 *       -> glue2esp_linkoutput（lwip-esp.o：包装成 esp pbuf 交 blob 发送）
 *       -> 发送完成后 blob 回调 pbuf_free -> lwip-esp.o -> esp2glue_pbuf_freed
 *          （本文件：pbuf_free 释放仓库 pbuf）
 *
 * 契约参考：C:/esp/arduino8266/tools/sdk/lwip2/builder/glue-esp/lwip-esp.c
 * 与 include/glue.h。nm 验证：NONOSDK305 blobs 并不直接引用 esp2glue_*，
 * 而是引用 lwip1.4 风格 API（netif_add/pbuf_alloc/ethernet_input...），这些由
 * 从 liblwip2-536.a 抽取的 lwip-esp.o 提供，它再转入本文件的 esp2glue_*。
 * （glue.h 中的 esp2glue_netif_updated 在本 SDK 无任何调用方，实际入口是
 *  esp2glue_netif_update；本文件两者都实现。）
 *
 * 符号策略：lwip-esp.o（esp 侧）与仓库 core（lwip2 侧）存在同名 API
 * （netif_add/pbuf_alloc/ethernet_input/lwip_init/sys_timeout...）。esp 侧名字
 * 必须保留给 blobs，因此编译仓库 core 与本文件时用 -D 把这批名字重命名为
 * *_r221（与 SDK 打包 liblwip2-536.a 的 *_LWIP2 手法一致）。
 */
#include "glue.h"                  /* esp2glue 与 glue2esp 契约 + lwip_xt_rsil */
#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/memp.h"
#include "lwip/ip4_addr.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#include <string.h>

/* ================= UART0 流控输出（抄 arduino_nos/app.c 水位流控） ============ */
#define UART0_STATUS (*(volatile unsigned int*)0x6000001c)  /* TXFIFO_CNT = bits[23:16] */
static void uart0_puts(const char* s)
{
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    while (*s) {
        while (((UART0_STATUS >> 16) & 0xff) > 120) { }
        *fifo = (unsigned int)(unsigned char)*s++;
    }
}
/* lwipopts.h 的 LWIP_PLATFORM_ASSERT/DIAG 钩子 */
void lwip_dbg_puts(const char* s) { uart0_puts(s); }

static const char hexc[] = "0123456789abcdef";
/* 打印帧头证据：tag + len + 前 24B hex */
static void hexdump_line(const char* tag, unsigned idx, unsigned len, const unsigned char* b)
{
    char line[110];
    unsigned i, o = 0;
    const char* t = tag;
    while (*t && o < 6) line[o++] = *t++;
    line[o++] = '0' + (char)idx; line[o++] = ' ';
    /* len */
    if (len >= 100) line[o++] = '0' + (char)(len / 100);
    if (len >= 10)  line[o++] = '0' + (char)((len / 10) % 10);
    line[o++] = '0' + (char)(len % 10);
    line[o++] = 'B'; line[o++] = ' ';
    for (i = 0; i < 24 && i < len; i++) {
        line[o++] = hexc[b[i] >> 4];
        line[o++] = hexc[b[i] & 15];
    }
    line[o++] = '\r'; line[o++] = '\n'; line[o++] = 0;
    uart0_puts(line);
}

/* ================= lwip 侧 netif（每接口一个，索引 = STATION_IF/SOFTAP_IF） === */
static struct netif netif_lw[2];
static int lwip_ready = 0;
static unsigned rx_cnt = 0, tx_cnt = 0;

static err_glue_t drv_err2glue(err_t e)
{
    switch (e) {
    case ERR_OK:      return GLUE_ERR_OK;
    case ERR_MEM:     return GLUE_ERR_MEM;
    case ERR_BUF:     return GLUE_ERR_BUF;
    case ERR_TIMEOUT: return GLUE_ERR_TIMEOUT;
    case ERR_RTE:     return GLUE_ERR_RTE;
    case ERR_INPROGRESS: return GLUE_ERR_INPROGRESS;
    case ERR_VAL:     return GLUE_ERR_VAL;
    case ERR_WOULDBLOCK: return GLUE_ERR_WOULDBLOCK;
    case ERR_USE:     return GLUE_ERR_USE;
    case ERR_ALREADY: return GLUE_ERR_ALREADY;
    case ERR_ISCONN:  return GLUE_ERR_ISCONN;
    case ERR_CONN:    return GLUE_ERR_CONN;
    case ERR_IF:      return GLUE_ERR_IF;
    case ERR_ABRT:    return GLUE_ERR_ABRT;
    case ERR_RST:     return GLUE_ERR_RST;
    case ERR_CLSD:    return GLUE_ERR_CLSD;
    case ERR_ARG:     return GLUE_ERR_ARG;
    default:          return GLUE_ERR_ABRT;
    }
}

/* lwip 的 calloc 适配（MEM_CUSTOM_CALLOC，SDK 堆）：
 * os_zalloc 只有单参数形式，lwip 的 mem_calloc 要两参数，这里做乘法并检查溢出。 */
void* xesp_lwip_calloc(size_t count, size_t size)
{
    if (size != 0 && count > (size_t)(-1) / size) return 0;
    return os_zalloc(count * size);
}

/* ================= 收发包计数（诊断）======================================== */
unsigned g_rxReq;    /* esp2glue_alloc_for_recv 被调用次数（WiFi 驱动送来多少帧）*/
unsigned g_rxOk;     /* 成功分配缓冲（真正喂给 lwip）的次数 */
unsigned g_txOut;    /* drv_linkoutput 调用次数（lwip 要发多少帧） */
unsigned g_txFreed;  /* esp2glue_pbuf_freed 回调次数（配对释放） */

#define XTAKE_N 8
static struct pbuf* g_xtakeP[XTAKE_N];
static unsigned g_xtakeWr;
static unsigned g_xtakeFreed;
static unsigned g_xtakeStale;

static void xtake_push(struct pbuf* p)
{
    g_xtakeP[g_xtakeWr] = p;
    g_xtakeWr = (g_xtakeWr + 1u) % XTAKE_N;
}

static unsigned xtake_take(struct pbuf* p)
{
    unsigned i;
    for (i = 0; i < XTAKE_N; i++) {
        if (g_xtakeP[i] == p) {
            g_xtakeP[i] = NULL;
            ++g_xtakeFreed;
            return 1u;
        }
    }
    ++g_xtakeStale;
    return 0u;
}


/* ================= TX: lwip2 -> esp blob ===================================== */
/* 2026-10-20 所有权修正（实测泄漏定位）：
 * 原实现 pbuf_ref(p) 后把 lwip 的 pbuf 交给胶水，指望 blob 发完回调
 * esp2glue_pbuf_freed() 来配对释放。真机上该回调不保证回来 —— 释放侧配对
 * 统计显示 <=128B 档每个 ICMP 回包净漏 1 块（~700B/5s），arena 打满后假死。
 * 改为：拷贝进静态乒乓缓冲，交给胶水的是【普通内存】而非 pbuf；
 * esp2glue_pbuf_freed() 相应改为空操作。lwip 的 pbuf 由调用方（ip4_output_if）
 * 在本函数返回后自行释放，TX 路径不再跨栈转移所有权。 */
/* 2026-10-21 泄漏修复（XVR/XTX/CBREF 三环取证定案）：
 * 旧实现把 lwip 的 pbuf 直接交给 glue2esp_linkoutput 当 ref2save。
 * 反汇编 lwip-esp.o 证实：SDK 侧以 esp 布局（ref@14 为 u16）操作该指针，
 * 发送/重试路径会持续 esp_pbuf_ref——我们的 2.2.1 头（ref@14 u8）被写成
 * 245+ 的垃圾值，完成回调 pbuf_free 永远减不到 0 → ICMP 应答 pbuf
 * （92B 块）每包漏一块，arena ~140B/s 涨满（XVR pending 实锤）。
 * 修法：帧拷进静态乒乓 scratch；交给 SDK 一个一次性 PBUF_REF cookie
 * （仅 28B 结构体，payload 指向 scratch）。SDK 折腾 cookie 无所谓，
 * 完成回调 esp2glue_pbuf_freed(cookie) 里 pbuf_free 归还；真正的
 * 应答 pbuf 留在 lwip 所有权链内由 icmp 正常释放，零跨层转移。 */
#define XNET_TX_SCRATCH_N 4
static unsigned char g_txScratch[XNET_TX_SCRATCH_N][1600] __attribute__((aligned(4)));
static unsigned g_txScratchIdx;

#if XTX_TRACE_ON
/* 前置声明：XTX 取证块定义在本函数之后 */
static void xtx_push(struct pbuf* p, u16_t len, u8_t is_pad);
static unsigned g_xtxOut;
static unsigned g_xtxFail;
static unsigned g_padTry;
static unsigned g_padOk;
#endif

static err_t drv_linkoutput(struct netif* netif, struct pbuf* p)
{
    struct pbuf* q;
    ++g_txOut;

    if (p->next) {
        q = pbuf_clone(PBUF_LINK, PBUF_RAM, p);
        if (q == NULL) return ERR_MEM;
    } else {
        /* 保住 pbuf 直到 blob 发送完成回调 esp2glue_pbuf_freed */
        pbuf_ref(p);
        q = p;
    }
    /* 以太网最小帧 60B（不含 FCS）：ARP=42B / ICMP echo reply 必须 pad，
     * 否则 PC 网卡/Windows 栈可能丢弃（实测 ping 不通的根因之一）。 */
    if (q->len < 60) {
        struct pbuf* np = pbuf_alloc(PBUF_RAW, 60, PBUF_RAM);
        if (np) {
            memset(np->payload, 0, 60);
            memcpy(np->payload, q->payload, q->len);
            pbuf_free(q);
            q = np;
        }
    }

    {
        err_glue_t ge = glue2esp_linkoutput(netif->num, q, q->payload, q->len);
#if XTX_TRACE_ON
        ++g_xtxOut;
        if (ge != GLUE_ERR_OK)
            ++g_xtxFail;
        else
            xtx_push(q, (u16_t)q->len, 0);
#endif
        if (ge != GLUE_ERR_OK) {
            pbuf_free(q);            /* 释放上面的 pbuf_ref / clone */
            return ERR_IF;
        }
        xtake_push(q);   /* 登记在途指针；完成回调凭此判定合法性 */
    }
    return ERR_OK;
}

/* RX 主路径是 esp 侧直调 esp2glue_ethernet_input；本回调是 lwip 内部的
 * netif->input upcall 占位。⚠ 收到 pbuf 必须释放：lwip 的调用方把所有权
 * 交给 input 回调，返回 ERR_VAL 而不 free 就是每帧漏一个 pbuf
 * （实测 arena 每收一帧涨 ~94B）。 */
static err_t drv_input(struct pbuf* p, struct netif* netif)
{
    (void)netif;
    if (p) pbuf_free(p);
    return ERR_VAL;
}

static err_t drv_netif_init_common(struct netif* netif)
{
    netif->output    = etharp_output;      /* -D 重命名 -> 仓库真 etharp_output */
    netif->linkoutput = drv_linkoutput;
    netif->input     = drv_input;
    netif->flags     = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    /* 校验和由 LWIP_CHECKSUM 系列宏（lwipopts 默认 1）软件计算 */
    return ERR_OK;
}
static err_t drv_netif_init_sta(struct netif* netif)
{
    netif->name[0] = 's'; netif->name[1] = 't';
    return drv_netif_init_common(netif);
}
static err_t drv_netif_init_ap(struct netif* netif)
{
    netif->name[0] = 'a'; netif->name[1] = 'p';
    return drv_netif_init_common(netif);
}

static void drv_ensure_init(void)
{
    ip4_addr_t zero;
    int i;
    if (lwip_ready) return;
    lwip_ready = 1;
    IP4_ADDR(&zero, 0, 0, 0, 0);
    lwip_init();                              /* -D -> lwip_init_r221（仓库 core） */
    netif_add(&netif_lw[STATION_IF], &zero, &zero, &zero, NULL, drv_netif_init_sta, NULL);
    netif_add(&netif_lw[SOFTAP_IF],  &zero, &zero, &zero, NULL, drv_netif_init_ap,  NULL);
    /* 必须显式置 hwaddr_len=6（同参考 glue-lwip/lwip-git.c:457-458）：
     * 否则首个 ARP 收发时 etharp.c:1138 LWIP_ASSERT(hwaddr_len==ETH_HWADDR_LEN)
     * 触发死循环（首轮实测 crash 点）。 */
    for (i = 0; i < 2; i++) {
        netif_lw[i].hwaddr_len = NETIF_MAX_HWADDR_LEN;
        memset(netif_lw[i].hwaddr, 0, NETIF_MAX_HWADDR_LEN);
        /* 注意：这里绝不能调 wifi_get_macaddr——ensure 首次触发点是 blob 在
         * wifi boot 极早期调的 lwip_init，此时调 wifi_get_macaddr 会把该
         * 上下文卡死（实测：stack inited 打印自此消失，但 lwip_ready 已置位，
         * 后续 esp2glue 路径照常工作）。真实 MAC 由 esp2glue_netif_update
         * 的 hwaddr 拷贝/fallback 填入。 */
    }
    uart0_puts("[lwip] stack inited, netif[sta/ap] added, hwaddr_len=6\r\n");
}

/* ================= esp2glue_* 契约实现 ======================================= */

void esp2glue_lwip_init(void)
{
    /* 注：NONOS305 实测 blobs 不会调用 lwip_init（glue-lwip-init 探针 x0），
     * 栈初始化实际由首次 esp2glue_netif_set_up1down0/netif_update/RX 触发。 */
    drv_ensure_init();
}

static void print_ip(const char* tag, uint32_t addr)
{
    const unsigned char* b = (const unsigned char*)&addr;
    char l[40];
    unsigned o = 0, i, d, started = 0;
    const char* t = tag;
    while (*t && o < 6) l[o++] = *t++;
    for (i = 0; i < 4; i++) {
        d = b[i]; started = 0;
        if (d >= 100) { l[o++] = '0' + d / 100; started = 1; }
        if (d >= 10 || started) { l[o++] = '0' + (d / 10) % 10; }
        l[o++] = '0' + d % 10;
        if (i != 3) l[o++] = '.';
    }
    l[o++] = '\r'; l[o++] = '\n'; l[o++] = 0;
    uart0_puts(l);
}

static void drv_netif_update(int idx, uint32_t ip, uint32_t mask, uint32_t gw,
                             size_t hwlen, const uint8_t* hwaddr, uint16_t mtu)
{
    struct netif* netif;
    ip4_addr_t aip, amask, agw;

    drv_ensure_init();
    if (idx != STATION_IF && idx != SOFTAP_IF) return;
    netif = &netif_lw[idx];

    if (hwaddr && hwlen && hwlen <= NETIF_MAX_HWADDR_LEN) {
        netif->hwaddr_len = (u8_t)hwlen;
        memcpy(netif->hwaddr, hwaddr, hwlen);
    }
    /* esp 侧可能传 hwlen=0（esp netif 的 hwaddr_len 尚未设置/布局错位）——
     * 用 SDK 权威 MAC 兜底，保证 etharp 的 hwaddr_len==6 断言恒成立 */
    if (netif->hwaddr_len != ETH_HWADDR_LEN) {
        uint8_t mac[NETIF_MAX_HWADDR_LEN];
        if (wifi_get_macaddr((uint8)idx, mac)) {
            memcpy(netif->hwaddr, mac, ETH_HWADDR_LEN);
            netif->hwaddr_len = ETH_HWADDR_LEN;
            uart0_puts("[lwip] hwlen from esp invalid -> wifi_get_macaddr fallback\r\n");
        }
    }
    if (mtu) netif->mtu = mtu;

    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    if (ip) netif->flags |= NETIF_FLAG_UP;
    else    netif->flags &= ~NETIF_FLAG_UP;

    aip.addr = ip; amask.addr = mask; agw.addr = gw;
    netif_set_addr(netif, &aip, &amask, &agw);   /* -D -> netif_set_addr_r221 */

    /* 本阶段约定：SoftAP lwip 侧固定静态 IP 192.168.4.1/24 */
    if (idx == SOFTAP_IF) {
        ip4_addr_t ap;
        IP4_ADDR(&ap, 192, 168, 4, 1);
        if (aip.addr != ap.addr) {
            netif_set_ipaddr(netif, &ap);
            uart0_puts("[lwip] softap ip forced to 192.168.4.1\r\n");
        }
    }

    uart0_puts("[lwip] netif");
    { char c[2]; c[0] = '0' + idx; c[1] = 0; uart0_puts(c); }
    uart0_puts(" updated, ip=");
    print_ip("", ip);
    uart0_puts("           mask=");
    print_ip("", mask);
    uart0_puts("           gw=");
    print_ip("", gw);
}

/* lwip-esp.o 的 netif_set_addr 实际调用的入口 */
void esp2glue_netif_update(int netif_idx, uint32_t ip, uint32_t mask, uint32_t gw,
                           size_t hwlen, const uint8_t* hwaddr, uint16_t mtu)
{
    drv_netif_update(netif_idx, ip, mask, gw, hwlen, hwaddr, mtu);
}

/* glue.h 契约入口（带 flags）。本 SDK（NONOS305 + liblwip2-536.a）中 blobs 与
 * lwip-esp.o 均不调用它（nm/grep 验证），实际更新走 esp2glue_netif_update；
 * 这里按契约实现，行为与 _update 一致（flags 仅记录于 netif->flags 高位无意义，
 * 直接忽略）。 */
void esp2glue_netif_updated(int netif_idx, uint32_t ip, uint32_t mask, uint32_t gw,
                            glue_netif_flags_t flags, size_t hwlen, const uint8_t* hw)
{
    (void)flags;
    drv_netif_update(netif_idx, ip, mask, gw, hwlen, hw, 1500);
}

/* ================= RX pbuf 取证：环形记录表（XPB_TRACE_ON，默认开） =================
 * 交接文档 §3.1 建议 2：登记 esp2glue_alloc_for_recv 分配的每个 pbuf，
 * 在 esp2glue_ethernet_input 返回后读 p->ref 核对是否被 lwip 消费：
 *   ref==0 → 输入路径自己丢弃/消费（not-for-us、短帧、未知协议、ARP 消费…）
 *   ref>=1 → 仍有引用。正常只剩 ICMP echo 回包：icmp_input 复用同一 pbuf 走
 *            ip4_output_if → drv_linkoutput pbuf_ref 交 blob，由
 *            esp2glue_pbuf_freed 完成时归零。若超 XPB_TRACE_LEAK_AGE_MS
 *            仍不归零 → 泄漏候选（没有任何路径会再来释放它）。
 * 读取时机正确性：ref==0 判定在 ethernet_input 返回后【立即】读——lwip 内部
 * 均为"先分配应答、后释放输入"（icmp.c:262→294、etharp.c:717→742），原块
 * 不可能被复用冒名；超龄判定在 beat 读，该块仍被泄漏持有（ref>=1），同样
 * 不会被复用。
 * ⚠ 取证专用：占 32×16B .bss；定位到根因后随修复一并移除（XPB_TRACE_ON=0 全裁）。
 */
/* 2026-10-23：默认改 0（P0 定案收官，取证表 512B 退役）。
 * ⚠ 本 TU 由 lwip 段编译（CFLAGS_LW，无 -include CFG 档案），cfg 里的
 * XPB_TRACE_ON=0 传不进来——必须改这里的默认值才生效。
 * 需要收包取证时改回 1（+512B .bss）。 */
#ifndef XPB_TRACE_ON
#define XPB_TRACE_ON 0
#endif

u32_t sys_now(void);   /* 前向声明（定义在本文末尾 sys 适配段） */

#if XPB_TRACE_ON
#define XPB_TRACE_N             32
#define XPB_TRACE_LEAK_AGE_MS   3000

/* 条目状态：1=在途 2=已释放（输入消费或 TX 完成） 3=泄漏候选 4=别处释放 */
#define XPB_ST_PEND    1
#define XPB_ST_FREED   2
#define XPB_ST_LEAK    3
#define XPB_ST_ELSE    4

typedef struct XPBTrace {
    struct pbuf* p;       /* pbuf 指针 */
    u32_t        t_alloc; /* sys_now() 分配时刻（ms） */
    u16_t        len;     /* 帧长 */
    u16_t        ethtype; /* 抓取的 ethertype（主机序 0x0800/0x0806/0x86dd） */
    u8_t         idx;     /* netif 索引 */
    u8_t         state;
    u8_t         proto;   /* IP 协议号（1=ICMP 6=TCP 17=UDP；非 IP 为 0） */
    u8_t         pad;
} XPBTrace;

static XPBTrace  g_xpbTrace[XPB_TRACE_N];
static u8_t      g_xpbWr;
static unsigned  g_xpbOkTotal;    /* 已释放累计（输入消费 + TX 完成回调） */
static unsigned  g_xpbElseTotal;  /* 别处释放累计（beat 时发现 ref 已归零） */
static unsigned  g_xpbLeakTotal;  /* 泄漏候选累计（超龄仍占着不归零） */
static unsigned  g_xpbAllocFail;  /* pbuf_alloc 失败次数（"WiFi 变哑"直接证据） */
static unsigned  g_xpbOverwrite;  /* 环形覆盖仍未决条目的次数 */

static void xpb_push(struct pbuf* p, u16_t len)
{
    XPBTrace* e = &g_xpbTrace[g_xpbWr];
    if (e->state == XPB_ST_PEND) ++g_xpbOverwrite;
    e->p = p;
    e->t_alloc = sys_now();
    e->len = len;
    e->ethtype = 0;
    e->idx = 0xff;
    e->state = XPB_ST_PEND;
    e->proto = 0;
    g_xpbWr = (u8_t)((g_xpbWr + 1u) % XPB_TRACE_N);
}

/* 只匹配在途条目，从新到旧扫（同地址旧条目已被解决，新条目排在前面） */
static XPBTrace* xpb_find(struct pbuf* p)
{
    int i;
    for (i = 0; i < XPB_TRACE_N; i++) {
        int k = (int)g_xpbWr - 1 - i;
        XPBTrace* e;
        if (k < 0) k += XPB_TRACE_N;
        e = &g_xpbTrace[k];
        if (e->state == XPB_ST_PEND && e->p == p) return e;
    }
    return NULL;
}

static const char* xpb_type_name(u16_t ethtype, u8_t proto)
{
    if (ethtype == 0x0806) return "arp";
    if (ethtype == 0x0800) {
        switch (proto) {
        case 1:  return "icmp";
        case 6:  return "tcp";
        case 17: return "udp";
        default: return "ip?";
        }
    }
    if (ethtype == 0x86dd) return "ipv6";
    return "other";
}

static void xpb_put_u32(u32_t v)
{
    char d[12]; int i = 0; char l[14]; unsigned o = 0;
    if (v == 0) { uart0_puts("0"); return; }
    while (v) { d[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) l[o++] = d[--i];
    l[o] = 0;
    uart0_puts(l);
}

static void xpb_put_hex32(u32_t v)
{
    char l[11]; unsigned o = 0; int s = 28;
    l[o++] = '0'; l[o++] = 'x';
    while (s >= 0) { l[o++] = hexc[(v >> s) & 15]; s -= 4; }
    l[o] = 0;
    uart0_puts(l);
}

/* 由 app.c 的 5s beat 调用：超龄判定 + 汇总 + 泄漏候选明细 */
void xpb_trace_dump(void)
{
    u32_t now = sys_now();
    unsigned i, pend = 0, leak = 0;

    for (i = 0; i < XPB_TRACE_N; i++) {
        XPBTrace* e = &g_xpbTrace[i];
        if (e->state != XPB_ST_PEND) continue;
        /* 在途条目仍被持有（ref>=1）→ 读其内存安全，不会被复用冒名 */
        if (e->p->ref == 0) {
            /* 没走 esp2glue_pbuf_freed 就被放了（如 etharp 超时队列路径） */
            e->state = XPB_ST_ELSE;
            ++g_xpbElseTotal;
            continue;
        }
        if ((u32_t)(now - e->t_alloc) >= (u32_t)XPB_TRACE_LEAK_AGE_MS) {
            e->state = XPB_ST_LEAK;
            ++g_xpbLeakTotal;
        }
    }
    for (i = 0; i < XPB_TRACE_N; i++) {
        if (g_xpbTrace[i].state == XPB_ST_PEND) ++pend;
        else if (g_xpbTrace[i].state == XPB_ST_LEAK) ++leak;
    }

    uart0_puts("[xpb] rxReq=");   xpb_put_u32(g_rxReq);
    uart0_puts(" rxOk=");        xpb_put_u32(g_rxOk);
    uart0_puts(" allocFail=");   xpb_put_u32(g_xpbAllocFail);
    uart0_puts(" freedTotal=");  xpb_put_u32(g_xpbOkTotal);
    uart0_puts(" elseTotal=");   xpb_put_u32(g_xpbElseTotal);
    uart0_puts(" leakTotal=");   xpb_put_u32(g_xpbLeakTotal);
    uart0_puts(" pend=");        xpb_put_u32(pend);
    uart0_puts(" overwritten="); xpb_put_u32(g_xpbOverwrite);
    uart0_puts("\r\n");

    for (i = 0; i < XPB_TRACE_N; i++) {
        XPBTrace* e = &g_xpbTrace[i];
        if (e->state != XPB_ST_LEAK) continue;
        uart0_puts("[xpb] LEAK p=");
        xpb_put_hex32((u32_t)(unsigned)e->p);
        uart0_puts(" len=");     xpb_put_u32(e->len);
        uart0_puts(" type=");    uart0_puts(xpb_type_name(e->ethtype, e->proto));
        uart0_puts(" ethtype="); xpb_put_hex32(e->ethtype);
        uart0_puts(" idx=");     xpb_put_u32(e->idx);
        uart0_puts(" age=");     xpb_put_u32(now - e->t_alloc);
        uart0_puts("ms\r\n");
        /* 保持 LEAK 态持续可见（每拍都打印），直到被环形覆盖 */
    }
#if XTX_TRACE_ON
    xtx_trace_dump();
#endif
    {
        /* 分配指针环（XVariablePool.c 取证第二轮，CFG 宏内编译）：打印未销账条目。
         * 注意本 TU 不含 CFG，不能按宏门控；符号恒在链接里。 */
        extern void XVariablePool_leak_ring_dump(void);
        XVariablePool_leak_ring_dump();
    }
    {
        char l[40];
        unsigned o = 0;
        unsigned v = g_xtakeStale;
        const char* tag = "[xta] stale=";
        while (*tag) l[o++] = *tag++;
        if (!v) l[o++] = '0';
        else {
            char d[10];
            int n = 0;
            while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
            while (n) l[o++] = d[--n];
        }
        l[o++] = '\r'; l[o++] = '\n'; l[o] = 0;
        uart0_puts(l);
    }
}
#endif /* XPB_TRACE_ON */

void esp2glue_alloc_for_recv(size_t len, void** pbuf, void** data)
{
    struct pbuf* p;
    *pbuf = NULL;
    *data = NULL;
    ++g_rxReq;
    /* ⚠ 2026-10-23 实测：不要在这里前置 drv_ensure_init——真机 A/B 对比
     * （ensure 前置版 allocFail=110 持续 vs 原始版 rxOk==rxReq）证明 ensure
     * 的首次触发必须留在底座验证过的两条路径上（blob 的 lwip_init 钩子 /
     * ethernet_input）。alloc_for_recv 由 SDK WiFi 收包上下文调用，比
     * ethernet_input 更早更脆弱，lwip_init/netif_add 在该上下文跑会半途
     * 而废（lwip_ready 已置位不再重试 → 池永久缺失 → allocFail 100%）。
     * join 抢占 lwh 的问题由 lwipopts 砍池解决（池 16.3K→8.5K，lwh 17.7K
     * 即使被 join 抢 ~5K 也够划齐）。 */
    if (len == 0 || len > 1600) return;
    p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_RAM);   /* -D -> pbuf_alloc_r221 */
    if (!p) {
#if XPB_TRACE_ON
        ++g_xpbAllocFail;
#endif
        return;
    }
    ++g_rxOk;
    *pbuf = p;
    *data = p->payload;
#if XPB_TRACE_ON
    xpb_push(p, (u16_t)len);
#endif
}

err_glue_t esp2glue_ethernet_input(int netif_idx, void* received)
{
    struct netif* netif;
    struct pbuf* p = (struct pbuf*)received;
    err_t err;
#if XPB_TRACE_ON
    XPBTrace* tr = xpb_find(p);
    if (tr && p->len >= 14) {
        /* 抓帧类型证据（喂 lwip 之前，payload 仍指向以太网头） */
        const unsigned char* b = (const unsigned char*)p->payload;
        tr->ethtype = (u16_t)(((u16_t)b[12] << 8) | (u16_t)b[13]);
        if (tr->ethtype == 0x0800 && p->len >= 24) tr->proto = b[23];
        tr->idx = (u8_t)netif_idx;
    }
#endif

    if (netif_idx != STATION_IF && netif_idx != SOFTAP_IF) {
        pbuf_free(p);
#if XPB_TRACE_ON
        if (tr) { tr->state = XPB_ST_FREED; ++g_xpbOkTotal; }
#endif
        return GLUE_ERR_ARG;
    }
    drv_ensure_init();          /* RX 可能先于 netif 更新到达，保证栈/netif 就绪 */
    netif = &netif_lw[netif_idx];

    /* RX 帧头证据（ask 要求：证明 RX 喂包成功） */
    if (p && p->payload && p->len)
        /* hexdump_line("RX#", ...);  诊断已关 */
    rx_cnt++;

    err = ethernet_input(p, netif);   /* -D -> ethernet_input_r221（仓库 core） */
#if XPB_TRACE_ON
    if (tr) {
        /* 立即读 ref：lwip "先分配应答、后释放输入"保证原块未被复用冒名。
         * ref==0 → 输入路径已消费/丢弃；ref>=1 → ICMP echo 回包在途
         * （等 esp2glue_pbuf_freed）或泄漏，留给 beat 超龄判定。 */
        if (p->ref == 0) { tr->state = XPB_ST_FREED; ++g_xpbOkTotal; }
    }
#endif
    return drv_err2glue(err);
}

/* ===== TX 指针回收守卫（2026-10-21 泄漏修复核心） =====
 * SDK 完成回调会对老指针重复触发（CBREF 探针实证：回调时刻读到 ref=245+
 * 垃圾值 = 该地址早已释放/复用）。重复回调对"已释放且被复用"的地址再减
 * 引用，会打穿新分配块的头（ICMP 应答 pbuf 92B/ping 净漏的机制）。
 * 守卫：只有"本驱动交出且尚未收回"的指针才允许执行 pbuf_free，
 * 其余一律忽略并计数。XTAKE_N=8 覆盖在途窗口（实测 txOut-txFreed<=5）。 */
void esp2glue_pbuf_freed(void* ref_saved)
{
#if XPB_TRACE_ON
    XPBTrace* tr = xpb_find((struct pbuf*)ref_saved);
    if (tr && tr->state == XPB_ST_PEND) {
        tr->state = XPB_ST_FREED;
        ++g_xpbOkTotal;
    }
#endif
    if (!xtake_take((struct pbuf*)ref_saved))
        return;   /* 陈旧/重复回调：忽略（见 XTAKE 块注释） */
    pbuf_free((struct pbuf*)ref_saved);   /* -D -> pbuf_free_r221 */
}

void esp2glue_netif_set_up1down0(int netif_idx, int up1_or_down0)
{
    struct netif* netif;
    drv_ensure_init();
    if (netif_idx != STATION_IF && netif_idx != SOFTAP_IF) return;
    netif = &netif_lw[netif_idx];
    if (up1_or_down0) {
        netif_set_link_up(netif);
        netif->flags |= NETIF_FLAG_UP;
    } else {
        netif_set_link_down(netif);
        netif->flags &= ~NETIF_FLAG_UP;
        netif_set_down(netif);
    }
}

void esp2glue_netif_set_default(int netif_idx)
{
    drv_ensure_init();
    if (netif_idx == STATION_IF || netif_idx == SOFTAP_IF)
        netif_set_default(&netif_lw[netif_idx]);
}

/* DHCP 客户端：本阶段关闭（lwipopts LWIP_DHCP=0），esp 侧 dhcp_start 走到这里
 * 只拉起链路，返回成功避免 blob 侧报错。 */
err_glue_t esp2glue_dhcp_start(int netif_idx)
{
    struct netif* netif;
    drv_ensure_init();
    if (netif_idx != STATION_IF && netif_idx != SOFTAP_IF) return GLUE_ERR_ARG;
    netif = &netif_lw[netif_idx];
    netif_set_link_up(netif);
    netif->flags |= NETIF_FLAG_UP;
    uart0_puts("[lwip] esp2glue_dhcp_start: no DHCP client (static IP stage)\r\n");
    return GLUE_ERR_OK;
}

void esp2glue_dhcp_stop(int netif_idx)
{
    (void)netif_idx;
    uart0_puts("[lwip] esp2glue_dhcp_stop\r\n");
}

/* SoftAP DHCP server：本阶段不用（PC 端 netsh 设静态 IP），仅打印收到的配置 */
void esp2glue_dhcps_start(struct ip_info* info)
{
    if (info) {
        uart0_puts("[lwip] esp2glue_dhcps_start (server NOT started), ap ip=");
        print_ip("", info->ip.addr);
    }
}

void esp2glue_espconn_init(void)
{
    /* OPENSDK 路径才会触发；保持契约存在 */
}

/* ================= sys 适配（lwIP NO_SYS 需要） ============================== */
u32_t sys_now(void)
{
    return system_get_time() / 1000u;    /* µs -> ms，32 位回绕无碍（lwip 只做差值） */
}

sys_prot_t sys_arch_protect(void)
{
    return lwip_xt_rsil(15);             /* glue.h 内联：关中断 */
}
void sys_arch_unprotect(sys_prot_t pval)
{
    lwip_xt_wsr_ps(pval);
}

/* ================= 自测：注入 ARP 请求 + ICMP echo，验证全链路 ================ */
static u16_t csum16(const unsigned char* d, unsigned len)
{
    u32_t s = 0;
    unsigned i;
    for (i = 0; i + 1 < len; i += 2)
        s += ((u32_t)d[i] << 8) | d[i + 1];
    if (len & 1) s += (u32_t)d[len - 1] << 8;
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (u16_t)~s;
}

static void inject_frame(int idx, const unsigned char* frame, unsigned len)
{
    void* pb = NULL;
    void* data = NULL;
    esp2glue_alloc_for_recv(len, &pb, &data);
    if (!pb || !data) {
        uart0_puts("[lwip] selftest alloc fail\r\n");
        return;
    }
    memcpy(data, frame, len);
    esp2glue_ethernet_input(idx, pb);
}

/* 由 app.c 在 SoftAP 起来后调用：两条帧走完整 RX->协议栈->TX 链路 */
void lwip_esp8266_selftest(void)
{
    unsigned char f[64];
    const unsigned char smac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x64 }; /* 假 PC MAC */
    const unsigned char bmac[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    unsigned i;

    uart0_puts("[lwip] selftest: inject ARP request (192.168.4.100->192.168.4.1)\r\n");

    /* --- 帧 1: ARP request（同时让 lwip ARP 表学习 192.168.4.100） --- */
    memset(f, 0, sizeof f);
    memcpy(f, bmac, 6); memcpy(f + 6, smac, 6);
    f[12] = 0x08; f[13] = 0x06;                 /* ethertype ARP */
    f[14] = 0x00; f[15] = 0x01;                 /* htype ethernet */
    f[16] = 0x08; f[17] = 0x00;                 /* ptype IPv4 */
    f[18] = 6; f[19] = 4;
    f[20] = 0x00; f[21] = 0x01;                 /* op = request */
    memcpy(f + 22, smac, 6);
    f[28] = 192; f[29] = 168; f[30] = 4; f[31] = 100;   /* spa */
    f[32] = 0; f[33] = 0; f[34] = 0; f[35] = 0;         /* tha */
    f[36] = 192; f[37] = 168; f[38] = 4; f[39] = 1;     /* tpa = 192.168.4.1 */
    inject_frame(SOFTAP_IF, f, 42);

    uart0_puts("[lwip] selftest: inject ICMP echo request\r\n");

    /* --- 帧 2: ICMP echo request --- */
    memset(f, 0, sizeof f);
    memcpy(f, bmac, 6); memcpy(f + 6, smac, 6);
    f[12] = 0x08; f[13] = 0x00;                 /* ethertype IPv4 */
    /* IPv4 header @14 */
    f[14] = 0x45; f[15] = 0x00;
    f[16] = 0x00; f[17] = 32;                   /* tot_len = 20 + 12 */
    f[18] = 0x13; f[19] = 0x37;                 /* id */
    f[22] = 64;                                 /* ttl */
    f[23] = 1;                                  /* proto = ICMP */
    f[26] = 192; f[27] = 168; f[28] = 4; f[29] = 100;
    f[30] = 192; f[31] = 168; f[32] = 4; f[33] = 1;
    {
        u16_t c = csum16(f + 14, 20);
        f[24] = (unsigned char)(c >> 8); f[25] = (unsigned char)c;
    }
    /* ICMP echo request @34: type8 code0 cksum id seq + 8B data = 12B */
    f[34] = 8; f[35] = 0;
    f[38] = 0x12; f[39] = 0x34;                 /* id */
    f[40] = 0x00; f[41] = 0x01;                 /* seq */
    for (i = 0; i < 8; i++) f[42 + i] = (unsigned char)('A' + i);
    {
        u16_t c = csum16(f + 34, 12);
        f[36] = (unsigned char)(c >> 8); f[37] = (unsigned char)c;
    }
    inject_frame(SOFTAP_IF, f, 46);

    uart0_puts("[lwip] selftest done (expect TX# = ARP reply + ICMP echo reply)\r\n");
}
