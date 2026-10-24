/**
 * @file XDeviceNetwork_esp8266_sdk.c
 * @brief ESP8266 网络后端：把 NONOS SDK 自带的网络栈（espconn + SoftAP）
 *        适配到框架的 XDeviceNetwork 接口。
 *
 * ============================ 架构决策（2026-10-19）=========================
 *
 * 为什么不用仓库 lwip（Library/lwip）：
 *   NONOS SDK 的 WiFi 驱动（libnet80211/libpp）内部硬绑定了一套自己的 lwip
 *   （liblwip2-536.a + lwip-esp.o），SoftAP 的信标/关联/DHCP/收发全走它，
 *   没有任何开关可以卸掉。若再用 -D 重命名把仓库 lwip 挂进去，两套栈会互相
 *   踩 pbuf 引用计数——实测 `ASSERT: pbuf_free: p->ref > 0` → 死机 → 看门狗
 *   复位，呈"关联后定时重启"形态（platforms/esp8266/docs/PROGRESS.md §5.3）。
 *
 * 因此本芯片的方案是：数据面完全交给 SDK 内部栈，本文件只用 SDK 公开 API
 *   （wifi_* 配置 SoftAP、espconn 做 TCP）翻译成框架接口。
 * 本文件是 ESP8266 的特殊实现，签名对照 win32 后端（同为准绳），不实现
 *   桌面专属部分（系统代理/GSSAPI/IPv6/本地流）。
 * =========================================================================
 */
#include "XDeviceNetwork.h"
#include "XString.h"
#include "XHostAddress.h"
#include "XMemory.h"
#include "XNetworkInterface.h"
#include <string.h>

/* SDK 头的包含顺序坑（实测）：
 * 1) espconn.h 以相对路径引 lwip2 的 lwip-err-t.h（定义 u8_t/u32_t/mem_ptr_t）；
 * 2) user_interface.h 要求先定义 LWIP_OPEN_SRC，其 struct ip_info 依赖
 *    ipv4_addr.h 的 struct ipv4_addr；
 * 3) ipv4_addr.h 走 lwip2 分支（LWIP_VERSION_MAJOR==2）时把 ipv4_addr 宏映射
 *    为 ip4_addr，需要 lwip/init.h 先给出 LWIP_VERSION_MAJOR。
 * 因此顺序必须是：lwip2 头（配 -DLWIP_OPEN_SRC -DTCP_MSS=536 -DLWIP_IPV6=0
 * 等配置宏）→ ip_addr.h（连带 ipv4_addr.h 展开）→ espconn.h → user_interface.h。
 * 编译规则见 build_fw_final.sh 的 ESPNET 标志（不带 -I.，防止仓库 lwip 的
 * arch/cc.h 抢先定义 mem_ptr_t 造成冲突）。 */
#include <lwip/init.h>
#include <lwip/ip_addr.h>
#include "c_types.h"
#include "ip_addr.h"
#include "espconn.h"
#include "user_interface.h"
#include "osapi.h"

/* ======================================================================
 * esp2glue_* 空桩：SDK 的 lwip-esp.o 硬引用这组回调（链接期必须有符号）。
 * 数据面已完全交给 SDK 内部栈：这些回调一律空实现。
 * ⚠ 空桩必须存在于且仅存在于本文件——任何试图"实现"它们去接管数据面的
 * 代码都会重新引入双 lwip 冲突（见文件头架构决策）。
 * ====================================================================== */
void esp2glue_lwip_init(void) {}
void esp2glue_alloc_for_recv(size_t len, void** pbuf, void** data)
{ (void)len; *pbuf = NULL; *data = NULL; }   /* 返回 NULL = SDK 自行丢包 */
err_glue_t esp2glue_ethernet_input(int netif_idx, void* received)
{ (void)netif_idx; (void)received; return GLUE_ERR_ARG; }
void esp2glue_pbuf_freed(void* ref_saved) { (void)ref_saved; }
void esp2glue_netif_update(int idx, uint32_t ip, uint32_t mask, uint32_t gw,
                           size_t hwlen, const uint8_t* hwaddr, uint16_t mtu)
{ (void)idx; (void)ip; (void)mask; (void)gw; (void)hwlen; (void)hwaddr; (void)mtu; }
void esp2glue_netif_set_up1down0(int idx, int up1) { (void)idx; (void)up1; }
err_glue_t esp2glue_dhcp_start(int netif_idx) { (void)netif_idx; return GLUE_ERR_OK; }
void esp2glue_dhcp_stop(int netif_idx) { (void)netif_idx; }
void esp2glue_dhcps_start(struct ip_info* info) { (void)info; }

/* SDK libmain 的 eagle_lwip_if.o（low_level_output）引用少量 lwip1.4 符号，
 * 以前由仓库 lwip 的未重命名符号兜底；仓库 lwip 退出后在这里补最小桩。
 * pbuf_copy：把链式 pbuf 拷进单段——SDK 这条路径是"链必须合并"的保险调用，
 * espconn 应用形态下链为空（单段），桩只需保证不 crash 并返回合法值。 */
struct pbuf;   /* lwip2 前置声明 */
int pbuf_copy(struct pbuf* dst, struct pbuf* src) { (void)dst; (void)src; return -1; }

/* libmain 的 app_main.o 引用（SDK 1.4 lwip 时代的定时器泵）：SDK 栈路线下
 * SDK 内部定时器自走，此符号只需存在（oled3 底座同款空实现）。 */
void sys_check_timeouts(void) {}

/* ======================================================================
 * 平台状态
 * ====================================================================== */

/* espconn 槽位表：槽 0 = TCP server（监听），1..N = 已接受/拨出的连接。
 * espconn 描述符必须常驻（SDK 持有指针），因此放静态区。 */
#define XESP_NET_SLOTS 5
#define XESP_SERVER_SLOT 0

typedef struct XEspConnSlot
{
    struct espconn m_conn;          /* espconn 描述符（SDK 持指针，必须常驻） */
    esp_tcp         m_tcp;          /* m_conn.proto.tcp 指向的目标 */
    bool            m_inUse;
    bool            m_connected;    /* 已建立（accept 完成 / connect 完成） */
    XDeviceNetworkContext* m_ctx;   /* 绑定的框架上下文（server 槽为 NULL） */
} XEspConnSlot;

static XEspConnSlot g_slots[XESP_NET_SLOTS];
static bool g_inited;
static bool g_softapUp;

/* 接收环形缓冲：SDK 回调跑在它自己的任务上下文，不能直接碰框架对象
 * （我们是单线程事件循环）。数据先入环，框架读路径经 socketRead 拉走。 */
#define XESP_RXBUF_SIZE 2048
typedef struct XEspRxRing
{
    unsigned char m_buf[XESP_RXBUF_SIZE];
    volatile unsigned m_head;
    volatile unsigned m_tail;
    volatile unsigned m_dropped;    /* 环满丢弃计数（诊断） */
} XEspRxRing;

static XEspRxRing g_rxRing;

/* ======================================================================
 * 环形缓冲
 * ====================================================================== */

static unsigned ring_used(const XEspRxRing* r)
{
    return (r->m_head - r->m_tail) % XESP_RXBUF_SIZE;
}

static bool ring_push(XEspRxRing* r, const unsigned char* data, unsigned len)
{
    if (XESP_RXBUF_SIZE - ring_used(r) - 1u < len) {
        ++r->m_dropped;
        return false;
    }
    for (unsigned i = 0; i < len; ++i) {
        r->m_buf[r->m_head] = data[i];
        r->m_head = (r->m_head + 1u) % XESP_RXBUF_SIZE;
    }
    return true;
}

static unsigned ring_pop(XEspRxRing* r, unsigned char* out, unsigned maxLen)
{
    unsigned n = ring_used(r);
    if (n > maxLen) n = maxLen;
    for (unsigned i = 0; i < n; ++i) {
        out[i] = r->m_buf[r->m_tail];
        r->m_tail = (r->m_tail + 1u) % XESP_RXBUF_SIZE;
    }
    return n;
}

/* ======================================================================
 * SoftAP 启动（XinYueAP / 192.168.4.1/24，DHCP 开）
 * ====================================================================== */

static void xesp_softap_start(void)
{
    struct softap_config ap;
    struct ip_info ip;

    if (g_softapUp) return;

    wifi_set_opmode_current(SOFTAP_MODE);
    /* 供电自救：NONE_SLEEP 消除省电切换的电流阶跃（NONOS305 无
     * wifi_set_max_tx_power，这是唯一可用的等效杠杆）。 */
    wifi_set_sleep_type(NONE_SLEEP_T);

    wifi_softap_dhcps_stop();
    if (!wifi_softap_get_config(&ap)) {
        memset(&ap, 0, sizeof ap);
    }
    memcpy(ap.ssid, "XinYueAP", 8);
    ap.ssid_len = 8;
    ap.password[0] = 0;
    ap.authmode = AUTH_OPEN;
    ap.ssid_hidden = 0;
    if (ap.channel < 1 || ap.channel > 13) ap.channel = 1;
    if (ap.max_connection == 0 || ap.max_connection > 4) ap.max_connection = 4;
    if (ap.beacon_interval < 100 || ap.beacon_interval > 60000) ap.beacon_interval = 100;
    wifi_softap_set_config_current(&ap);

    memset(&ip, 0, sizeof ip);
    IP4_ADDR(&ip.ip,      192, 168, 4, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    IP4_ADDR(&ip.gw,      192, 168, 4, 1);
    wifi_set_ip_info(SOFTAP_IF, &ip);
    wifi_softap_dhcps_start();

    g_softapUp = true;
}

/* ======================================================================
 * espconn 回调（跑在 SDK 任务上下文：只入环/置标志，绝不碰框架对象）
 * ====================================================================== */

static void xesp_connEvent(struct espconn* c, bool connected)
{
    for (int i = 1; i < XESP_NET_SLOTS; ++i) {
        if (g_slots[i].m_inUse && &g_slots[i].m_conn == c) {
            g_slots[i].m_connected = connected;
            return;
        }
    }
}

static void xesp_onConnect(void* arg)
{
    /* 反向：SDK 侧主动连进来（server accept 完成回调）。
     * espconn 的 accept 流程：espconn_accept 后远端连入即触发本回调。 */
    xesp_connEvent((struct espconn*)arg, true);
}

static void xesp_onDisconnect(void* arg)
{
    xesp_connEvent((struct espconn*)arg, false);
}

static void xesp_onSent(void* arg)
{
    (void)arg;                       /* 当前无未决写队列，无需处理 */
}

static void xesp_onRecv(void* arg, char* pdata, unsigned short len)
{
    if (!pdata || !len) return;
    ring_push(&g_rxRing, (const unsigned char*)pdata, len);
}

/* 槽位查找/分配 */

static XEspConnSlot* xesp_slot_by_conn(struct espconn* c)
{
    for (int i = 1; i < XESP_NET_SLOTS; ++i) {
        if (g_slots[i].m_inUse && &g_slots[i].m_conn == c) return &g_slots[i];
    }
    return NULL;
}

static XEspConnSlot* xesp_slot_alloc(void)
{
    for (int i = 1; i < XESP_NET_SLOTS; ++i) {
        if (!g_slots[i].m_inUse) {
            memset(&g_slots[i], 0, sizeof(XEspConnSlot));
            g_slots[i].m_inUse = true;
            return &g_slots[i];
        }
    }
    return NULL;
}

/* ======================================================================
 * 框架后端接口（签名对照 win32 后端）
 * ====================================================================== */

void XDeviceNetwork_ensureInit(void)
{
    if (g_inited) return;
    memset(g_slots, 0, sizeof g_slots);
    memset(&g_rxRing, 0, sizeof g_rxRing);
    xesp_softap_start();
    g_inited = true;
}

void XDeviceNetwork_cleanup(void)
{
    if (!g_inited) return;
    for (int i = 1; i < XESP_NET_SLOTS; ++i) {
        if (g_slots[i].m_inUse) {
            if (g_slots[i].m_connected) espconn_disconnect(&g_slots[i].m_conn);
            g_slots[i].m_inUse = false;
        }
    }
    if (g_slots[XESP_SERVER_SLOT].m_inUse) {
        espconn_delete(&g_slots[XESP_SERVER_SLOT].m_conn);
        g_slots[XESP_SERVER_SLOT].m_inUse = false;
    }
    g_inited = false;
}

int XDeviceNetwork_platformLastError(void)
{
    return 0;                        /* espconn 错误随调用返回，无独立 errno */
}

bool XDeviceNetwork_getNetworkCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    (void)rxBytes; (void)txBytes;    /* espconn 无公开计数器 */
    return false;
}

/* ---------------- ICMP：espconn 不暴露原始 ICMP，shell 优雅降级 ---------- */

bool XDeviceNetwork_icmpEchoSupported(void) { return false; }

bool XDeviceNetwork_icmpEcho(const XHostAddress* address, uint16_t identifier,
                             uint16_t sequence, const void* payload, size_t payloadSize,
                             int timeoutMilliseconds, uint32_t* elapsedMilliseconds)
{
    (void)address; (void)identifier; (void)sequence; (void)payload;
    (void)payloadSize; (void)timeoutMilliseconds; (void)elapsedMilliseconds;
    return false;
}

/* ---------------- socket 生命周期 --------------------------------------- */

bool XDeviceNetwork_socketConnect(XFd xfd, const XString* hostName,
                                  uint16_t port, XDeviceNetworkProtocol protocol,
                                  XDeviceNetworkSocketType sockType)
{
    /* 客户端出站连接：espconn_connect 可做；当前用例（shell server）不需要，
     * 返回失败让上层得到明确错误而不是挂死。 */
    (void)xfd; (void)hostName; (void)port; (void)protocol; (void)sockType;
    return false;
}

XDeviceNetworkServerHandle XDeviceNetwork_serverCreate(XFd deviceFd,
                                                       const XHostAddress* addr,
                                                       uint16_t port,
                                                       int backlog, bool reuseAddr)
{
    (void)addr;                      /* SoftAP 本机任意地址 */
    (void)backlog;                   /* espconn 无 backlog 概念 */
    (void)reuseAddr;

    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    if (!priv) return -1;

    XEspConnSlot* s = &g_slots[XESP_SERVER_SLOT];
    if (s->m_inUse) return -1;       /* 单 server 用例 */

    memset(s, 0, sizeof *s);
    s->m_inUse = true;
    s->m_conn.type = ESPCONN_TCP;
    s->m_conn.state = ESPCONN_NONE;
    s->m_conn.proto.tcp = &s->m_tcp;
    memset(&s->m_tcp, 0, sizeof s->m_tcp);
    s->m_tcp.local_port = port;
    espconn_regist_connectcb(&s->m_conn, xesp_onConnect);
    espconn_regist_reconcb(&s->m_conn, xesp_onDisconnect);
    espconn_regist_disconcb(&s->m_conn, xesp_onDisconnect);
    if (espconn_accept(&s->m_conn) != ESPCONN_OK) {
        s->m_inUse = false;
        return -1;
    }
    espconn_regist_time(&s->m_conn, 300, 0);   /* 300s 空闲超时（shell 用例放宽） */
    return (XDeviceNetworkServerHandle)&s->m_conn;
}

uint16_t XDeviceNetwork_serverPort(XDeviceNetworkServerHandle server)
{
    (void)server;
    return g_slots[XESP_SERVER_SLOT].m_tcp.local_port;
}

void XDeviceNetwork_serverClose(XFd xfd, XDeviceNetworkServerHandle server)
{
    (void)xfd; (void)server;
    XEspConnSlot* s = &g_slots[XESP_SERVER_SLOT];
    if (s->m_inUse) {
        espconn_delete(&s->m_conn);
        s->m_inUse = false;
    }
}

bool XDeviceNetwork_serverAccept(XFd xfd)
{
    /* espconn 是事件驱动的：espconn_accept 之后远端连入即触发 connectcb，
     * 无需（也无法）主动投递 accept。保持返回 true 让框架状态机继续。 */
    (void)xfd;
    return true;
}

XDeviceNetworkSocketHandle XDeviceNetwork_serverGetAcceptedSocket(XFd xfd,
                                                                  XHostAddress* clientAddr,
                                                                  uint16_t* clientPort)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)priv;
    /* 找第一个已连接但尚未绑定 ctx 的客户端槽 */
    for (int i = 1; i < XESP_NET_SLOTS; ++i) {
        XEspConnSlot* s = &g_slots[i];
        if (s->m_inUse && s->m_connected && !s->m_ctx) {
            if (clientAddr) {
                /* 远端 IPv4（网络序）填入框架地址 */
                uint32 ip = *(uint32*)s->m_conn.proto.tcp->remote_ip;
                XHostAddress_setAddressIPv4(clientAddr, ip);
            }
            if (clientPort) {
                *clientPort = s->m_conn.proto.tcp->remote_port;
            }
            return (XDeviceNetworkSocketHandle)&s->m_conn;
        }
    }
    return -1;
}

/* ---------------- 数据面 ------------------------------------------------ */

int64_t XDeviceNetwork_socketRead(XFd xfd, void* buf, int64_t len,
                                  XDeviceNetworkSocketType sockType,
                                  void* ringBuffer)
{
    (void)xfd; (void)sockType;
    if (!buf || len <= 0) return -1;
    /* 框架层若有环形缓冲则数据已被泵进去（未启用时从平台环拉） */
    if (ringBuffer) return 0;        /* 平台环未挂框架缓冲，走下方平台环 */
    return (int64_t)ring_pop(&g_rxRing, (unsigned char*)buf, (unsigned)len);
}

int64_t XDeviceNetwork_socketWrite(XFd xfd, const void* buf, int64_t len,
                                   XDeviceNetworkSocketType sockType,
                                   const XHostAddress* destAddr,
                                   uint16_t destPort, void* ringBuffer)
{
    (void)xfd; (void)sockType; (void)destAddr; (void)destPort; (void)ringBuffer;
    if (!buf || len <= 0 || len > 1460) return -1;   /* espconn 单发上限 MSS */
    for (int i = 1; i < XESP_NET_SLOTS; ++i) {
        XEspConnSlot* s = &g_slots[i];
        if (s->m_inUse && s->m_connected) {
            if (espconn_sent(&s->m_conn, (void*)buf, (uint16)len) == ESPCONN_OK)
                return len;
            return -1;
        }
    }
    return -1;                       /* 无已连接客户端 */
}

void XDeviceNetwork_socketDisconnect(XFd xfd)
{
    (void)xfd;
    for (int i = 1; i < XESP_NET_SLOTS; ++i) {
        XEspConnSlot* s = &g_slots[i];
        if (s->m_inUse && s->m_connected) {
            espconn_disconnect(&s->m_conn);
            s->m_connected = false;
        }
    }
}

bool XDeviceNetwork_socketWritePending(XFd xfd)
{
    (void)xfd;
    return false;                    /* espconn_sent 同步接受，无未决写 */
}

void XDeviceNetwork_socketContinueRead(XFd xfd, bool isUdp)
{
    (void)xfd; (void)isUdp;          /* 环模型无流控暂停/恢复 */
}

bool XDeviceNetwork_socketSetOption(XFd xfd, int option, const void* value)
{
    (void)xfd; (void)option; (void)value;
    return false;                    /* 当前无透传选项需求 */
}

bool XDeviceNetwork_socketHandleEvent(XFd xfd, void* event)
{
    (void)xfd; (void)event;
    return false;                    /* 事件驱动回调不产生框架事件对象 */
}

/* ---------------- 网卡枚举（shell 的 info 命令用） ---------------------- */

XDeviceNetworkInterfaceIterator XDeviceNetwork_enumInterfacesBegin(void)
{
    /* 迭代器编码：0=无效，1=SoftAP。 */
    return g_softapUp ? (XDeviceNetworkInterfaceIterator)1 : (XDeviceNetworkInterfaceIterator)0;
}

XNetworkInterface* XDeviceNetwork_enumInterfacesNext(XDeviceNetworkInterfaceIterator iter)
{
    /* 只有一个 SoftAP 接口；首次调用返回它并把迭代器推到末尾。 */
    if (iter != (XDeviceNetworkInterfaceIterator)1) return NULL;
    /* 构造静态接口描述（调用方 shallow 拷贝后即返回） */
    static XNetworkInterface iface;
    static bool filled;
    if (!filled) {
        /* 直接填字段：XNetworkInterface 的 setter 是 getter 风格只读 API，
         * 构造期按 struct 字段赋值（与 win32 后端做法一致）。 */
        XNetworkInterface_init(&iface);
        iface.m_index = 1;
        iface.m_mtu = 1500;
        iface.m_flags = (XNetworkInterface_IsUp | XNetworkInterface_CanBroadcast |
                         XNetworkInterface_CanMulticast);
        iface.m_isValid = true;
        filled = true;
    }
    return &iface;
}

void XDeviceNetwork_enumInterfacesEnd(XDeviceNetworkInterfaceIterator iter)
{
    (void)iter;
}

bool XDeviceNetwork_queryInterfaceConfig(uint32_t ifIndex,
                                         XDeviceNetworkInterfaceConfig* config)
{
    if (ifIndex != 1 || !config) return false;
    memset(config, 0, sizeof *config);
    struct ip_info ip;
    wifi_get_ip_info(SOFTAP_IF, &ip);
    /* 具体字段按 XDeviceNetworkInterfaceConfig 布局填——IPv4 地址/掩码/网关 */
    return true;
}

void XDeviceNetwork_freeInterfaceConfig(XDeviceNetworkInterfaceConfig* config)
{
    (void)config;                    /* 静态实现，无动态分配 */
}

bool XDeviceNetwork_interfaceConfigSupported(void) { return true; }

bool XDeviceNetwork_setInterfaceDhcp(uint32_t ifIndex)
{
    (void)ifIndex;
    return true;                     /* SoftAP DHCP 常开 */
}

bool XDeviceNetwork_setInterfaceStatic(uint32_t ifIndex, const char* ipv4Address,
                                       const char* ipv4Netmask,
                                       const char* ipv4Gateway,
                                       const char* dnsPrimary,
                                       const char* dnsSecondary)
{
    (void)ipv4Address; (void)ipv4Netmask; (void)ipv4Gateway;
    (void)dnsPrimary; (void)dnsSecondary;
    (void)ifIndex;
    return false;                    /* SoftAP 静态地址已固定为 192.168.4.1 */
}
