/**
 * @file XNetwork_lwip_posix.c
 * @brief lwIP Linux/macOS/BSD 平台 TAP/TUN 虚拟网卡实现
 *
 * 对标 Windows XNetwork_lwip_win32.c（Npcap 虚拟网卡实现）。
 *
 * 核心功能：
 *   1. 创建 TAP 虚拟网卡（Linux: /dev/net/tun，macOS: utun）
 *   2. 创建回环网卡 lo0 (127.0.0.1)
 *   3. 枚举物理网卡，为每个创建 lwIP netif
 *   4. 对可发送数据包的网卡启用 DHCP 客户端
 *   5. 使用 XRandomGenerator 生成随机 MAC 地址，避免与真实网卡冲突
 *
 * Linux 实现依赖内核 TUN/TAP 驱动（CONFIG_TUN），
 * macOS 使用 utun 接口。
 */

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)

#include "XNetwork_config.h"
#if defined(XNETWORK_USE_LWIP)

/* ================================================================
 * 系统头文件
 * ================================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#ifdef __linux__
#include <linux/if_tun.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#endif

#ifdef __APPLE__
#include <net/if_utun.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#endif

#include "XNetwork_lwip_platform.h"
#include "XNetwork_platform.h"
#include "XMemory.h"
#include "XThread.h"
#include "XPrintf.h"
#include "XRandomGenerator.h"
#include "lwip/netif.h"

#ifndef NETIF_FLAG_LOOPBACK
#define NETIF_FLAG_LOOPBACK 0x200
#endif
#include "lwip/ip_addr.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/err.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
/* #include <pcap.h> moved to physical NIC section */
#include "netif/ethernet.h"

/* Netif input function */
#if NO_SYS
#define XNETIF_INPUT_FN  ethernet_input
#else
#define XNETIF_INPUT_FN  tcpip_input
#endif

/* ================================================================
 * 网络适配方式
 *
 * 选择一种模式（取消注释即可）：
 *   - LWIP_USE_DHCP:        DHCP 模式（默认），使用主机预先创建的 TAP 设备 + dnsmasq
 *   - LWIP_USE_STATIC_IP:   静态 IP 模式，自动创建 TAP 设备
 *   - LWIP_USE_PHYSICAL:    物理网卡模式，直接操作物理网卡（如 ens33），
 *                           与 Windows Npcap 行为一致，性能最佳
 *
 * 注意：三种模式只能启用一种，同时启用多个或都不启用会触发编译错误。
 * ================================================================ */
#define LWIP_USE_DHCP
//#define LWIP_USE_STATIC_IP
//#define LWIP_USE_PHYSICAL

/* 互斥检查：确保且仅启用一种模式 */
#if defined(LWIP_USE_DHCP) && defined(LWIP_USE_STATIC_IP)
#error "XNetwork_lwip_posix: 不能同时启用 LWIP_USE_DHCP 和 LWIP_USE_STATIC_IP"
#endif
#if defined(LWIP_USE_DHCP) && defined(LWIP_USE_PHYSICAL)
#error "XNetwork_lwip_posix: 不能同时启用 LWIP_USE_DHCP 和 LWIP_USE_PHYSICAL"
#endif
#if defined(LWIP_USE_STATIC_IP) && defined(LWIP_USE_PHYSICAL)
#error "XNetwork_lwip_posix: 不能同时启用 LWIP_USE_STATIC_IP 和 LWIP_USE_PHYSICAL"
#endif
#if !defined(LWIP_USE_DHCP) && !defined(LWIP_USE_STATIC_IP) && !defined(LWIP_USE_PHYSICAL)
#error "XNetwork_lwip_posix: 必须启用 LWIP_USE_DHCP、LWIP_USE_STATIC_IP 或 LWIP_USE_PHYSICAL 中的一种"
#endif

/* ================================================================
 * 静态 IP 配置（仅 LWIP_USE_STATIC_IP 模式使用）
 * ================================================================ */
#ifdef LWIP_USE_STATIC_IP
#define LWIP_STATIC_IP_ADDR    PP_HTONL(LWIP_MAKEU32(192,168,200,2))
#define LWIP_STATIC_MASK_ADDR  PP_HTONL(LWIP_MAKEU32(255,255,255,0))
#define LWIP_STATIC_GW_ADDR    PP_HTONL(LWIP_MAKEU32(192,168,200,1))
#endif

/* ================================================================
 * DHCP 配置（仅 LWIP_USE_DHCP 模式使用）
 * ================================================================ */
#ifdef LWIP_USE_DHCP
/* 主机预先创建的 TAP 设备名称（需在运行程序前创建并配置好 IP） */
#define LWIP_TAP_DEVICE_NAME   "lwip0"
#endif

/* ================================================================
 * 物理网卡配置（仅 LWIP_USE_PHYSICAL 模式使用）
 * ================================================================ */
#ifdef LWIP_USE_PHYSICAL
/* 物理网卡名称（如 ens33、eth0、wlan0 等） */
#define LWIP_PHYSICAL_IF_NAME  "ens33"
/* 每组数据包批量处理的最大数量 */
#define LWIP_PHYSICAL_BATCH_MAX  64
/* 物理网卡上下文最大数量 */
#define LWIP_PHYSICAL_MAX_ADAPTERS  4
#endif


#define TAP_MAX_ADAPTERS    8
#define TAP_READ_TIMEOUT_MS 10
#define TAP_MTU             1500

/* ================================================================
 * TAP 上下文
 * ================================================================ */

typedef struct tap_ctx_t {
    struct netif* netif;         /* lwIP 网络接口 */
    int           tap_fd;        /* TAP 设备文件描述符 */
    int           sock_fd;       /* 控制套接字（用于 ioctl） */
    char          if_name[IFNAMSIZ]; /* 接口名称 (tap0, tap1, ...) */
    char          winDesc[64];   /* 描述字符串 */
    bool          canSend;       /* 是否可以发送数据包 */
    bool          dhcpEnabled;   /* 是否已启用 DHCP */
    uint32_t      dhcpStartTime; /* DHCP 启动时间 */
    int           index;         /* 上下文索引 */
} tap_ctx_t;

/* ================================================================
 * 全局状态
 * ================================================================ */

static tap_ctx_t  g_tapCtxs[TAP_MAX_ADAPTERS];
static int        g_tapCtxCount = 0;
static bool       g_dhcpStarted = false;
static struct     netif* g_loopbackNetif = NULL;
static struct netif* g_defaultLwipNetif = NULL;

/* ================================================================
 * 前方声明
 * ================================================================ */

static err_t tap_if_init(struct netif* netif);
static err_t tap_link_output(struct netif* netif, struct pbuf* p);
static void   gen_lwip_mac(uint8_t* mac);

/* ================================================================
 * 辅助函数
 * ================================================================ */

/**
 * @brief 生成随机 MAC 地址
 * 使用本地管理的单播地址（bit1=1, bit0=0），避免与真实网卡 MAC 冲突。
 */
static void gen_lwip_mac(uint8_t* mac)
{
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)XRandomGenerator_system();
    }
    mac[0] = (mac[0] & 0xFE) | 0x02;
}

#ifdef __linux__
/**
 * @brief 创建 Linux TAP 虚拟网卡
 */
static int tap_create(char* ifName, size_t ifNameSize)
{
    /* 先尝试打开已有的 TAP 设备（DHCP 模式下由主机创建） */
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        LWIP_DBG("[TAP] 无法打开 /dev/net/tun: %s\n", strerror(errno));
        LWIP_DBG("[TAP] 请确保内核已加载 tun 模块 (modprobe tun)\n");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

#if defined(LWIP_USE_DHCP)
    /* DHCP 模式：使用主机预先创建的 TAP 设备 */
    strncpy(ifr.ifr_name, LWIP_TAP_DEVICE_NAME, IFNAMSIZ - 1);
#elif defined(LWIP_USE_STATIC_IP)
    /* 静态 IP 模式：让内核自动分配设备名称（tap0, tap1, ...） */
    /* ifr.ifr_name 保持为空，内核自动分配 */
#endif

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        LWIP_DBG("[TAP] ioctl(TUNSETIFF) 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    strncpy(ifName, ifr.ifr_name, ifNameSize - 1);
    ifName[ifNameSize - 1] = '\0';

    /* 设置 MTU */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
        ifr.ifr_mtu = TAP_MTU;
        if (ioctl(sock, SIOCSIFMTU, &ifr) < 0) {
            LWIP_DBG("[TAP] 设置 MTU 失败: %s\n", strerror(errno));
        }
        close(sock);
    }

    LWIP_DBG("[TAP] 创建虚拟网卡: %s (fd=%d)\n", ifName, fd);
    return fd;
}

static void tap_set_iface_addr(const char* ifName, const char* ipStr, const char* maskStr)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;

    struct ifreq ifr;
    struct sockaddr_in* addr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
    addr = (struct sockaddr_in*)&ifr.ifr_addr;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, ipStr, &addr->sin_addr);
    ioctl(sock, SIOCSIFADDR, &ifr);

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
    addr = (struct sockaddr_in*)&ifr.ifr_netmask;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, maskStr, &addr->sin_addr);
    ioctl(sock, SIOCSIFNETMASK, &ifr);

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFFLAGS, &ifr);
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    ioctl(sock, SIOCSIFFLAGS, &ifr);

    close(sock);
}

static void tap_delete(const char* ifName)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFFLAGS, &ifr);
    ifr.ifr_flags &= ~(IFF_UP | IFF_RUNNING);
    ioctl(sock, SIOCSIFFLAGS, &ifr);

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
    struct sockaddr_in* addr = (struct sockaddr_in*)&ifr.ifr_addr;
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    ioctl(sock, SIOCSIFADDR, &ifr);

    close(sock);
}

#elif defined(__APPLE__)

static int tap_create(char* ifName, size_t ifNameSize)
{
    for (int i = 0; i < 4; i++) {
        struct sockaddr_ctl sc;
        struct ctl_info ctlInfo;
        int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
        if (fd < 0) continue;

        memset(&ctlInfo, 0, sizeof(ctlInfo));
        strncpy(ctlInfo.ctl_name, UTUN_CONTROL_NAME, sizeof(ctlInfo.ctl_name));
        if (ioctl(fd, CTLIOCGINFO, &ctlInfo) < 0) {
            close(fd);
            continue;
        }

        memset(&sc, 0, sizeof(sc));
        sc.sc_id = ctlInfo.ctl_id;
        sc.sc_unit = i + 1;
        sc.sc_family = AF_SYSTEM;
        sc.ss_len = sizeof(sc);
        if (connect(fd, (struct sockaddr*)&sc, sizeof(sc)) < 0) {
            close(fd);
            continue;
        }

        snprintf(ifName, ifNameSize, "utun%d", i + 1);
        LWIP_DBG("[TAP] 创建虚拟网卡: %s (fd=%d)\n", ifName, fd);
        return fd;
    }
    LWIP_DBG("[TAP] 无法创建 utun 接口\n");
    return -1;
}

static void tap_set_iface_addr(const char* ifName, const char* ipStr, const char* maskStr)
{
    (void)ifName; (void)ipStr; (void)maskStr;
}

static void tap_delete(const char* ifName)
{
    (void)ifName;
}

#else
/* BSD 通用 */

static int tap_create(char* ifName, size_t ifNameSize)
{
    int fd = open("/dev/tap0", O_RDWR);
    if (fd < 0) {
        LWIP_DBG("[TAP] 无法打开 /dev/tap0: %s\n", strerror(errno));
        return -1;
    }
    strncpy(ifName, "tap0", ifNameSize - 1);
    ifName[ifNameSize - 1] = '\0';
    return fd;
}

static void tap_set_iface_addr(const char* ifName, const char* ipStr, const char* maskStr)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ifconfig %s inet %s netmask %s up 2>/dev/null",
             ifName, ipStr, maskStr);
    system(cmd);
}

static void tap_delete(const char* ifName)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ifconfig %s down 2>/dev/null", ifName);
    system(cmd);
}
#endif /* platform selection */

/* ================================================================
 * 测试发送能力
 * ================================================================ */

static bool test_tap_send(int tapFd)
{
    if (tapFd < 0) return false;

    uint8_t testFrame[60];
    memset(testFrame, 0, sizeof(testFrame));
    memset(testFrame, 0xFF, 6);           /* 目标 MAC：广播 */
    gen_lwip_mac(testFrame + 6);          /* 源 MAC：随机 */
    testFrame[12] = 0x08;                 /* 以太网类型：IPv4 */
    testFrame[13] = 0x00;

    ssize_t n = write(tapFd, testFrame, sizeof(testFrame));
    if (n < 0) {
        LWIP_DBG("[TAP] 发送测试失败: %s\n", strerror(errno));
        return false;
    }
    LWIP_DBG("[TAP] 发送测试成功: 写入 %zd 字节\n", n);
    return true;
}

/* ================================================================
 * lwIP 网卡回调
 * ================================================================ */

static err_t tap_if_init(struct netif* netif)
{
    netif->name[0] = 't';
    netif->name[1] = 'a';

    gen_lwip_mac(netif->hwaddr);
    netif->hwaddr_len = 6;

    netif->mtu = TAP_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->output = etharp_output;
    netif->linkoutput = tap_link_output;
    netif->state = NULL;

    LWIP_DBG("[TAP] 网卡初始化: %c%c%d MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
             netif->name[0], netif->name[1], netif->num,
             netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2],
             netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]);
    return ERR_OK;
}

static err_t tap_link_output(struct netif* netif, struct pbuf* p)
{
    tap_ctx_t* ctx = NULL;
    for (int i = 0; i < g_tapCtxCount; i++) {
        if (g_tapCtxs[i].netif == netif) {
            ctx = &g_tapCtxs[i];
            break;
        }
    }
    if (!ctx || ctx->tap_fd < 0) return ERR_IF;

    /* A TAP write is one complete Ethernet frame.  pbuf chains commonly
     * split link/IP/TCP headers and payload; writing every segment separately
     * turns them into malformed packets on the host side. */
    uint8_t frame[TAP_MTU + 18];
    if (p->tot_len == 0 || p->tot_len > sizeof(frame)) return ERR_BUF;
    if (pbuf_copy_partial(p, frame, p->tot_len, 0) != p->tot_len) return ERR_BUF;

    ssize_t n = write(ctx->tap_fd, frame, p->tot_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return ERR_BUF;
        return ERR_IF;
    }
    return n == p->tot_len ? ERR_OK : ERR_IF;
}

#if !LWIP_HAVE_LOOPIF
static err_t loop_if_init(struct netif* netif)
{
    netif->name[0] = 'l';
    netif->name[1] = 'o';
    netif->mtu = 65536;
    netif->flags = NETIF_FLAG_UP | NETIF_FLAG_LOOPBACK | NETIF_FLAG_LINK_UP;
    netif->output = etharp_output;
    netif->linkoutput = NULL;
    return ERR_OK;
}
#endif

static void tap_status_callback(struct netif* netif)
{
    LWIP_DBG("[网卡] 状态变更: %c%c%d up=%d link=%d\n",
             netif->name[0], netif->name[1], netif->num,
             netif_is_up(netif), netif_is_link_up(netif));
}

/* ================================================================
 * 物理网卡上下文（仅 LWIP_USE_PHYSICAL 模式）
 * ================================================================ */
#ifdef LWIP_USE_PHYSICAL
#include <pcap.h>

typedef struct {
    pcap_t*       pcap;         /* libpcap 句柄 */
    struct netif* netif;        /* lwIP 网络接口 */
    uint8_t       mac[6];       /* 物理网卡 MAC 地址 */
    char          ifName[IFNAMSIZ]; /* 接口名称 */
    char          desc[64];     /* 描述 */
    bool          canSend;      /* 是否可以发送 */
    bool          dhcpEnabled;  /* 是否已启用 DHCP */
    uint32_t      dhcpStartTime;/* DHCP 启动时间 */
    int           index;        /* 上下文索引 */
} physical_ctx_t;

static physical_ctx_t g_physCtxs[LWIP_PHYSICAL_MAX_ADAPTERS];
static int g_physCtxCount = 0;

/* pcap 数据包回调：将收到的以太网帧喂入 lwIP */
static void pcapif_input_callback(u_char* user, const struct pcap_pkthdr* hdr, const u_char* data)
{
    physical_ctx_t* ctx = (physical_ctx_t*)user;
    if (!ctx || !ctx->netif || !hdr || !data || hdr->len < 14) return;

    /* MAC 过滤：只接收发往本机或广播/多播的包 */
    const uint8_t* dstMac = data;
    bool forUs = false;
    if (dstMac[0] & 0x01) {
        forUs = true; /* 广播或多播 */
    } else if (memcmp(dstMac, ctx->mac, 6) == 0) {
        forUs = true; /* 发往本机 */
    }
    if (!forUs) return;

    struct pbuf* p = pbuf_alloc(PBUF_RAW, hdr->len, PBUF_POOL);
    if (!p) return;

    if (pbuf_take(p, data, hdr->len) == ERR_OK) {
        if (ctx->netif->input(p, ctx->netif) != ERR_OK) {
            pbuf_free(p);
        }
    } else {
        pbuf_free(p);
    }
}

/* 物理网卡输出回调：将 lwIP 的 pbuf 通过 pcap 发送 */
static err_t phys_link_output(struct netif* netif, struct pbuf* p)
{
    physical_ctx_t* ctx = NULL;
    for (int i = 0; i < g_physCtxCount; i++) {
        if (g_physCtxs[i].netif == netif) {
            ctx = &g_physCtxs[i];
            break;
        }
    }
    if (!ctx || !ctx->pcap) return ERR_IF;

    /* 将 pbuf 数据拼接到连续缓冲区 */
    uint8_t buf[2048];
    u16_t offset = 0;
    for (struct pbuf* q = p; q && offset < sizeof(buf); q = q->next) {
        u16_t copyLen = (q->len <= sizeof(buf) - offset) ? q->len : (u16_t)(sizeof(buf) - offset);
        memcpy(buf + offset, q->payload, copyLen);
        offset += copyLen;
    }
    if (offset == 0) return ERR_BUF;

    if (pcap_inject(ctx->pcap, buf, offset) == -1) {
        return ERR_IF;
    }
    return ERR_OK;
}

/* 物理网卡初始化回调 */
static err_t phys_if_init(struct netif* netif)
{
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->hwaddr_len = 6;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->output = etharp_output;
    netif->linkoutput = phys_link_output;
    LWIP_DBG("[物理网卡] 初始化: %c%c%d MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
             netif->name[0], netif->name[1], netif->num,
             netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2],
             netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]);
    return ERR_OK;
}

/* 获取物理网卡 MAC 地址 */
static bool get_interface_mac(const char* ifName, uint8_t* mac)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifName, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        close(fd);
        return false;
    }
    close(fd);
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return true;
}

#endif /* LWIP_USE_PHYSICAL */

/* ================================================================
 * 平台初始化
 * ================================================================ */

struct netif* XNetworkLwip_platform_init(void)
{
    LWIP_DBG("[平台初始化] 开始...\n");

    g_tapCtxCount = 0;
    memset(g_tapCtxs, 0, sizeof(g_tapCtxs));

    /* lwIP already creates its own loopback interface when LWIP_HAVE_LOOPIF
     * is enabled.  Do not register a second one or promote it to the default
     * route: an unavailable TAP must fail routing cleanly, not emit Ethernet
     * frames with a zero-length MAC address through loopback. */
#if !LWIP_HAVE_LOOPIF
    /* 第一步：创建回环网卡 */
    {
        struct netif* lnif = (struct netif*)XMalloc_System(sizeof(struct netif));
        if (lnif) {
            memset(lnif, 0, sizeof(struct netif));
            ip4_addr_t ipaddr, netmask, gw;
            IP4_ADDR(&gw, 127, 0, 0, 1);
            IP4_ADDR(&ipaddr, 127, 0, 0, 1);
            IP4_ADDR(&netmask, 255, 0, 0, 0);
            struct netif* result = netif_add(lnif, &ipaddr, &netmask, &gw, NULL,
                                             loop_if_init, XNETIF_INPUT_FN);
            if (result) {
                netif_set_up(result);
                netif_set_link_up(result);
                netif_set_default(result);
                g_loopbackNetif = result;
                g_defaultLwipNetif = result;
                XNetworkLwip_setDefaultNetif(result);
                LWIP_DBG("[回环] 创建回环网卡: %c%c%d\n",
                         result->name[0], result->name[1], result->num);
            } else {
                XFree_System(lnif);
            }
        }
    }
#endif

#ifdef LWIP_USE_PHYSICAL
    /* 第二步：打开物理网卡 */
    {
        LWIP_DBG("[物理网卡] 开始打开物理网卡 %s...\n", LWIP_PHYSICAL_IF_NAME);

        if (!get_interface_mac(LWIP_PHYSICAL_IF_NAME, g_physCtxs[0].mac)) {
            LWIP_DBG("[物理网卡] 获取 MAC 地址失败\n");
            return g_loopbackNetif;
        }
        memcpy(g_physCtxs[0].ifName, LWIP_PHYSICAL_IF_NAME, sizeof(LWIP_PHYSICAL_IF_NAME));

        char errbuf[PCAP_ERRBUF_SIZE];
        pcap_t* pcap = pcap_open_live(LWIP_PHYSICAL_IF_NAME, 65536, 1, 10, errbuf);
        if (!pcap) {
            LWIP_DBG("[物理网卡] pcap_open_live 失败: %s\n", errbuf);
            return g_loopbackNetif;
        }
        /* 设置 BPF 过滤：只捕获发往本机 MAC、广播和多播的包 */
        {
            struct bpf_program bpf;
            char filter[256];
            snprintf(filter, sizeof(filter),
                "ether dst %02x:%02x:%02x:%02x:%02x:%02x or ether broadcast or ether multicast",
                g_physCtxs[0].mac[0], g_physCtxs[0].mac[1],
                g_physCtxs[0].mac[2], g_physCtxs[0].mac[3],
                g_physCtxs[0].mac[4], g_physCtxs[0].mac[5]);
            if (pcap_compile(pcap, &bpf, filter, 1, PCAP_NETMASK_UNKNOWN) == 0) {
                pcap_setfilter(pcap, &bpf);
                pcap_freecode(&bpf);
                LWIP_DBG("[物理网卡] BPF 过滤已设置: %s\n", filter);
            } else {
                LWIP_DBG("[物理网卡] BPF 编译失败: %s\n", pcap_geterr(pcap));
            }
        }


        struct netif* n = (struct netif*)XMalloc_System(sizeof(struct netif));
        if (!n) {
            pcap_close(pcap);
            return g_loopbackNetif;
        }
        memset(n, 0, sizeof(struct netif));
        memcpy(n->hwaddr, g_physCtxs[0].mac, 6);

        ip4_addr_t ipaddr, netmask, gw;
        IP4_ADDR(&ipaddr, 0, 0, 0, 0);
        IP4_ADDR(&netmask, 0, 0, 0, 0);
        IP4_ADDR(&gw, 0, 0, 0, 0);

        struct netif* result = netif_add(n, &ipaddr, &netmask, &gw, NULL,
                                         phys_if_init, XNETIF_INPUT_FN);
        if (result) {
            g_physCtxs[0].netif = result;
            g_physCtxs[0].pcap = pcap;
            g_physCtxs[0].index = 0;
            snprintf(g_physCtxs[0].desc, sizeof(g_physCtxs[0].desc),
                     "PHYS-%s", LWIP_PHYSICAL_IF_NAME);
            g_physCtxCount = 1;

            netif_set_up(result);
            netif_set_link_up(result);
            netif_set_status_callback(result, NULL);

            if (g_loopbackNetif) {
                netif_set_default(result);
                g_defaultLwipNetif = result;
                XNetworkLwip_setDefaultNetif(result);
            }
            LWIP_DBG("[物理网卡] 创建成功: %c%c%d (if_name=%s)\n",
                     result->name[0], result->name[1], result->num,
                     LWIP_PHYSICAL_IF_NAME);
        } else {
            LWIP_DBG("[物理网卡] netif_add 失败\n");
            XFree_System(n);
            pcap_close(pcap);
        }
    }

    /* 物理网卡模式：返回物理网卡 */
    if (g_physCtxCount > 0 && g_physCtxs[0].netif) {
        return g_physCtxs[0].netif;
    }
    if (g_loopbackNetif) return g_loopbackNetif;
    return NULL;
}
#else
    /* 第二步：创建 TAP 虚拟网卡 */
    int tapCount = 0;
#if defined(LWIP_USE_DHCP)
    /* DHCP 模式：只使用一个 TAP 设备（由主机预先创建） */
    #define TAP_MAX_CREATE 1
#elif defined(LWIP_USE_STATIC_IP)
    /* 静态 IP 模式：创建多个 TAP 设备 */
    #define TAP_MAX_CREATE 4
#endif
    for (int i = 0; i < TAP_MAX_ADAPTERS && tapCount < TAP_MAX_CREATE; i++) {
        tap_ctx_t* ctx = &g_tapCtxs[tapCount];
        memset(ctx, 0, sizeof(tap_ctx_t));

        char ifName[IFNAMSIZ];
        int fd = tap_create(ifName, sizeof(ifName));
        if (fd < 0) break;

        ctx->tap_fd = fd;
        strncpy(ctx->if_name, ifName, sizeof(ctx->if_name) - 1);
        ctx->if_name[sizeof(ctx->if_name) - 1] = '\0';
        snprintf(ctx->winDesc, sizeof(ctx->winDesc), "TAP-%s", ifName);
        ctx->index = tapCount;
        ctx->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);

        /* 设置主机侧 TAP 接口 IP 地址 */
        tap_set_iface_addr(ifName, "192.168.200.1", "255.255.255.0");

        struct netif* n = (struct netif*)XMalloc_System(sizeof(struct netif));
        if (n) {
            memset(n, 0, sizeof(struct netif));
            ip4_addr_t ipaddr, netmask, gw;
            IP4_ADDR(&ipaddr, 0, 0, 0, 0);
            IP4_ADDR(&netmask, 0, 0, 0, 0);
            IP4_ADDR(&gw, 0, 0, 0, 0);
            struct netif* result = netif_add(n, &ipaddr, &netmask, &gw, ctx,
                                             tap_if_init, XNETIF_INPUT_FN);
            if (result) {
                ctx->netif = result;
                netif_set_up(result);
                netif_set_link_up(result);
                netif_set_status_callback(result, tap_status_callback);
                g_tapCtxCount++;
                tapCount++;
                LWIP_DBG("[TAP] 创建虚拟网卡: %c%c%d (if_name=%s)\n",
                         result->name[0], result->name[1], result->num, ifName);

                if (g_loopbackNetif) {
                    netif_set_default(result);
                    g_defaultLwipNetif = result;
                    XNetworkLwip_setDefaultNetif(result);
                }
            } else {
                LWIP_DBG("[TAP] netif_add 失败\n");
                XFree_System(n);
                close(fd);
                ctx->tap_fd = -1;
            }
        } else {
            LWIP_DBG("[TAP] XMalloc_System 分配 netif 失败\n");
            close(fd);
            ctx->tap_fd = -1;
        }
    }

    LWIP_DBG("[平台初始化] 共创建 %d 个 TAP 虚拟网卡\n", g_tapCtxCount);

    /* 第三步：应用静态 IP 地址（如果配置了静态 IP） */
#ifdef LWIP_USE_STATIC_IP
    LWIP_DBG("[静态IP] 应用静态 IP 地址...\n");
    for (int i = 0; i < g_tapCtxCount; i++) {
        tap_ctx_t* ctx = &g_tapCtxs[i];
        struct netif* n = ctx->netif;
        if (!n) continue;
        ip4_addr_t ipaddr, netmask, gw;
        ipaddr.addr = LWIP_STATIC_IP_ADDR;
        netmask.addr = LWIP_STATIC_MASK_ADDR;
        gw.addr = LWIP_STATIC_GW_ADDR;
        netif_set_addr(n, &ipaddr, &netmask, &gw);
        /* 配置 DNS 服务器 */
        {
            ip_addr_t dns_server;
            IP_ADDR4(&dns_server, 8, 8, 8, 8);  /* 备用 DNS：Google */
            dns_setserver(0, &dns_server);
            IP_ADDR4(&dns_server, 114, 114, 114, 114);  /* 备用 DNS：114DNS */
            dns_setserver(1, &dns_server);
        }
        {
            char ipbuf[4][16];
            ip4addr_ntoa_r(&ipaddr, ipbuf[0], sizeof(ipbuf[0]));
            ip4addr_ntoa_r(&netmask, ipbuf[1], sizeof(ipbuf[1]));
            ip4addr_ntoa_r(&gw, ipbuf[2], sizeof(ipbuf[2]));
            LWIP_DBG("[静态IP] %c%c%d -> %s/%s gw=%s\n",
                     n->name[0], n->name[1], n->num,
                     ipbuf[0], ipbuf[1], ipbuf[2]);
        }
    }
#endif

    /* 第四步：启动 DHCP */

#ifdef LWIP_USE_DHCP
    if (!g_dhcpStarted && g_tapCtxCount > 0) {
        g_dhcpStarted = true;
        LWIP_DBG("[DHCP] 检测到 %d 个虚拟网卡，开始检测发送能力...\n", g_tapCtxCount);

        int dhcpCount = 0;
        for (int i = 0; i < g_tapCtxCount && dhcpCount < 4; i++) {
            tap_ctx_t* ctx = &g_tapCtxs[i];
            struct netif* n = ctx->netif;
            if (!n) continue;

            ctx->canSend = test_tap_send(ctx->tap_fd);
            if (!ctx->canSend) {
                LWIP_DBG("[DHCP] %c%c%d (%s) 发送测试失败，跳过 DHCP\n",
                         n->name[0], n->name[1], n->num, ctx->winDesc);
                continue;
            }

            ctx->dhcpEnabled = true;
            ctx->dhcpStartTime = 0;
            LWIP_DBG("[DHCP] 网卡 %c%c%d (%s) 启动 DHCP\n",
                     n->name[0], n->name[1], n->num, ctx->winDesc);
            err_t dhcp_err = dhcp_start(n);
            LWIP_DBG("[DHCP] %c%c%d dhcp_start 返回=%d (0=OK)\n",
                     n->name[0], n->name[1], n->num, (int)dhcp_err);
            dhcpCount++;
        }
        LWIP_DBG("[DHCP] 共启动 %d 个 DHCP 客户端\n", dhcpCount);
    }
#endif

    LWIP_DBG("[平台初始化] 完成\n");

#ifdef LWIP_USE_PHYSICAL
    if (g_physCtxCount > 0 && g_physCtxs[0].netif) return g_physCtxs[0].netif;
#endif
    /* The TAP interface is the external lwIP route.  Returning loopback here
     * makes XNetwork_ensureInitialized() overwrite the TAP default netif,
     * sending DHCP/ARP frames through a non-Ethernet loopback interface. */
    if (g_tapCtxCount > 0 && g_tapCtxs[0].netif) return g_tapCtxs[0].netif;
    if (g_loopbackNetif) return g_loopbackNetif;
    return NULL;
}

/* 关闭 #else (TAP 模式) 块 */
#endif /* !LWIP_USE_PHYSICAL */

/* ================================================================
 * 平台清理
 * ================================================================ */

void XNetworkLwip_platform_deinit(void)
{
#ifdef LWIP_USE_PHYSICAL
    LWIP_DBG("[平台清理] 开始关闭 %d 个物理网卡...\n", g_physCtxCount);
    for (int i = 0; i < g_physCtxCount; i++) {
        physical_ctx_t* ctx = &g_physCtxs[i];
        if (ctx->pcap) {
            pcap_close(ctx->pcap);
            ctx->pcap = NULL;
        }
    }
    g_physCtxCount = 0;
}
#else
    LWIP_DBG("[平台清理] 开始关闭 %d 个 TAP 虚拟网卡...\n", g_tapCtxCount);

    for (int i = 0; i < g_tapCtxCount; i++) {
        tap_ctx_t* ctx = &g_tapCtxs[i];
        if (ctx->tap_fd >= 0) {
            tap_delete(ctx->if_name);
            close(ctx->tap_fd);
            ctx->tap_fd = -1;
        }
        if (ctx->sock_fd >= 0) {
            close(ctx->sock_fd);
            ctx->sock_fd = -1;
        }
    }

    g_tapCtxCount = 0;
    g_dhcpStarted = false;

    if (g_loopbackNetif) {
        XFree_System(g_loopbackNetif);
        g_loopbackNetif = NULL;
    }

    g_defaultLwipNetif = NULL;
    LWIP_DBG("[平台清理] 完成\n");
}
#endif /* LWIP_USE_PHYSICAL */

/* ================================================================
 * 数据包轮询
 * ================================================================ */

void XNetworkLwip_pollPcap(void)
{
#ifdef LWIP_USE_PHYSICAL
    /* 物理网卡模式：使用 pcap_dispatch 批量处理 */
    for (int i = 0; i < g_physCtxCount; i++) {
        physical_ctx_t* ctx = &g_physCtxs[i];
        if (!ctx->pcap || !ctx->netif) continue;
        pcap_dispatch(ctx->pcap, LWIP_PHYSICAL_BATCH_MAX, pcapif_input_callback, (u_char*)ctx);
    }
#else
    /* TAP 模式：逐个轮询 TAP 设备 */
    for (int i = 0; i < g_tapCtxCount; i++) {
        tap_ctx_t* ctx = &g_tapCtxs[i];
        if (ctx->tap_fd < 0 || !ctx->netif) continue;

        struct pollfd pfd;
        pfd.fd = ctx->tap_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 0);
        if (ret <= 0) continue;

        if (pfd.revents & POLLIN) {
            uint8_t buf[2048];
            ssize_t n = read(ctx->tap_fd, buf, sizeof(buf));
            if (n <= 0) continue;

            struct pbuf* p = pbuf_alloc(PBUF_RAW, (u16_t)n, PBUF_POOL);
            if (!p) continue;

            if (pbuf_take(p, buf, (u16_t)n) == ERR_OK) {
                if (ctx->netif->input(p, ctx->netif) != ERR_OK) {
                    pbuf_free(p);
                }
            } else {
                pbuf_free(p);
            }
        }
    }
#endif
}

/* ================================================================
 * 默认 netif 管理
 * ================================================================ */

bool XNetwork_socketConnectLocal(XNetworkSocketPrivate* priv, const XString* endpoint,
                                 XNetworkLocalStreamType streamType,
                                 int timeoutMs,
                                 XNetworkSocketType sockType)
{
    (void)priv;
    (void)endpoint;
    (void)streamType;
    (void)timeoutMs;
    (void)sockType;
    return false;
}

struct netif* XNetworkLwip_defaultNetif(void)
{
    return g_defaultLwipNetif;
}

void XNetworkLwip_setDefaultNetif(struct netif* netif)
{
    g_defaultLwipNetif = netif;
}

#endif /* XNETWORK_USE_LWIP */

#endif /* __linux__ || __APPLE__ || __BSD__ */
