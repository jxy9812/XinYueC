/**
 * @file       XDeviceWiFi_posix.c
 * @brief      POSIX（Linux/BSD）平台无线网络实现。
 * @details    契约见 Src/XDevice/XDeviceWiFi.h。本文件只实现**平台叶子函数**
 *             （设备虚槽 VXDeviceWiFi_* 的落地实现）：功能入口一律经父类的
 *             XDevice_control / XDevice_getProperty 走统一调度，业务层见不到
 *             本文件。
 *
 *             ## 后端选择（platformOpen 时探测，优先级：NM > wpa > WEXT）
 *               - **NetworkManager**（nmcli 子进程）：桌面发行版主流通路。
 *                 扫描列表与安全方式最完整；连接用后台子进程，不在轮询路径
 *                 里同步等待；
 *               - **wpa_supplicant 控制口**（AF_UNIX 数据报，不需要可执行
 *                 程序在场）：嵌入式与手工组网环境。WPA/WPA2/WPA3、断开、
 *                 删除已保存网络、准确的 wpa_state；
 *               - **WEXT**（ioctl SIOCGIW*，Linux 专用）：零外部依赖的兜底，
 *                 负责适配器识别、扫描、关联状态查询，以及开放/WEP 连接。
 *                 即便上层选了 NM/wpa，关联状态查询仍走 WEXT（只读、廉价）。
 *
 *             ## 异步口径（与 win32 / android 后端一致，全部"发起即返回"）
 *               - 扫描：发起后立即返回；scanState 轮询收口，超时 4s；
 *                 结果在 takeScanResults 一次性交出；
 *               - 连接：所有动作只负责"受理"，connectState 轮询关联状态 /
 *                 wpa_state / 子进程退出码判定，超时 20s；
 *               - **轮询路径不做重活**：nmcli 只在 takeScanResults 与动作
 *                 类接口里执行一次（有 1.5s 上限），scanState 绝不拉子进程，
 *                 避免 250ms 定时器把 UI 拖住。
 *
 *             ## 线程纪律
 *             不创建线程、不投递事件、不触碰界面对象；状态只在本线程可见的
 *             槽位里更新，上层用事件循环定时器轮询即可（XTHREAD_ON=0 的
 *             裁剪构建同样可用）。
 *
 *             ## 适配器标识
 *             adapterId = 网络接口名（如 "wlan0"），与 /sys/class/net 同名；
 *             兼容 "/dev/wlan0" 与 "/sys/class/net/wlan0" 两种旧写法。
 *
 *             ## 打开语义
 *             platformOpen 只在**检测到至少一个无线接口**时成功；没有无线
 *             硬件时如实失败，上层据此禁用无线入口（与网络设置页的自检
 *             断言一致）。USB 网卡热插拔后重新打开设备即可见到新接口。
 *
 *             ## 权限
 *             扫描/连接需要 CAP_NET_ADMIN 与 wpa_supplicant 控制口访问权限
 *             （通常为 root）。无权限时如实返回失败，不伪造成功。无线接口的
 *             IP 配置（DHCP/静态）不属本模块，见 XDeviceNetwork 的 POSIX
 *             后端。
 *
 *             ## 前置条件（平台侧，非本模块职责）
 *             本模块是**用户态**通路，依赖内核已提供无线协议栈：
 *               - 桌面发行版：cfg80211/mac80211（一般已编入）+ NetworkManager
 *                 或 wpa_supplicant —— 直接可用；
 *               - 嵌入式板：若内核**未编入** cfg80211/mac80211（无
 *                 /sys/class/ieee80211、无 /proc/net/wireless），则插 USB
 *                 无线网卡也不会出现无线接口，本模块将如实报告"无线不可用"
 *                 —— 这需要**内核侧**补协议栈与驱动，不是本模块能补的。
 *             故排查无线不通时，先确认内核侧：
 *                 ls /sys/class/ieee80211     # 有无 wiphy
 *                 ls /sys/class/net/          # 逐个看有无 wireless 子目录
 */
#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#if defined(XNETWORK_USE_PLATFORM_API) && \
    (defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || \
     defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)) && \
    !defined(__ANDROID__) /* 安卓由 Drive/Android/Device/Network/XDeviceWiFi_android.c
                             承担（JNI WifiManager），此处排除防重复定义。 */

#include "XDeviceWiFi.h"
#include "XDeviceWiFi_posix_Parsers.h"
#include "XString.h"
#include "XVector.h"
#include "XMemory.h"
#include "XDateTime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <net/if.h>

#if defined(__linux__)
#include <linux/wireless.h> /* WEXT：SIOCGIW* / struct iw_event */
#endif

/* =========================================================================
 * 一、常量与类型
 * ========================================================================= */

/** @brief 扫描超时兜底（与 win32 XWIFI_SCAN_TIMEOUT_MS 同值）。 */
#define XDEVWIFI_SCAN_TIMEOUT_MS 4000

/** @brief 连接超时兜底（与 win32 XWIFI_CONNECT_TIMEOUT_MS 同值）。 */
#define XDEVWIFI_CONNECT_TIMEOUT_MS 20000

/** @brief 起扫后至少经过此时长才认为结果可读（给驱动/系统刷新缓存）。 */
#define XDEVWIFI_RESCAN_MIN_MS 800

/** @brief 扫描结果缓冲区初值/上限（WEXT SIOCGIWSCAN 的 E2BIG 重试）。 */
#define XDEVWIFI_SCAN_BUF_INIT (16u * 1024u)
#define XDEVWIFI_SCAN_BUF_MAX  (256u * 1024u)

/** @brief wpa_supplicant 控制口单次命令等待上限（毫秒）。 */
#define XDEVWIFI_WPA_TIMEOUT_MS 500

/** @brief wpa_supplicant 命令缓冲与应答缓冲上限。 */
#define XDEVWIFI_WPA_CMD_MAX   512
#define XDEVWIFI_WPA_REPLY_MAX (8u * 1024u)

/** @brief nmcli 单次执行等待上限（毫秒）；超时即 SIGKILL。 */
#define XDEVWIFI_NM_TIMEOUT_MS 1500

/** @brief nmcli 扫描列表输出缓冲上限。 */
#define XDEVWIFI_NM_LIST_MAX (64u * 1024u)

/** @brief 安全描述 / 适配器文本缓冲。 */
#define XDEVWIFI_SSID_MAX  64
#define XDEVWIFI_BSSID_MAX 18

/**
 * @brief 单适配器槽位。
 * @details 扫描结果缓存由槽位持有，takeScanResults 移交所有权。
 */
typedef struct XDevWifiSlot
{
    bool used;                          /**< 槽位是否有效。 */
    char ifname[IFNAMSIZ];              /**< 接口名（即 adapterId）。 */
    char protocol[IFNAMSIZ];            /**< SIOCGIWNAME 协议描述。 */
    uint32_t ifIndex;                   /**< 接口索引。 */

    XDeviceWiFiScanState scanState;     /**< 扫描状态机。 */
    int64_t scanStartMs;                /**< 起扫时刻（单调毫秒）。 */
    XVector* scanCache;                 /**< 结果缓存（NetworkInfo*）。 */

    XDeviceWiFiConnectState connectState; /**< 连接状态机。 */
    int64_t connectStartMs;             /**< 连接发起时刻。 */
    char connectSsid[XDEVWIFI_SSID_MAX]; /**< 本次连接目标 SSID。 */
    pid_t nmChild;                      /**< nmcli 连接子进程；-1=无。 */

    bool wpaTried;                      /**< 是否已探测过控制口。 */
    bool wpaOk;                         /**< 控制口可用。 */
    char wpaPath[96];                   /**< 控制口路径。 */
} XDevWifiSlot;

/** @brief 打开的实例引用计数（先关闭的会话不释放他人仍在用的资源）。 */
static int s_refs = 0;

/** @brief 适配器槽位表。 */
static XDevWifiSlot s_slots[XDEVICE_WIFI_MAX_ADAPTERS];

/** @brief NetworkManager 是否在运行（三态：-1=未探测）。 */
static int s_nmRunning = -1;

/* =========================================================================
 * 二、通用工具
 * ========================================================================= */

/** @brief 当前单调毫秒（与全库计时源一致，不受对时跳变影响）。 */
static int64_t xdevwifi_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 有界文本复制；dst 始终以 NUL 结尾。 */
static void xdevwifi_copyText(char* dst, size_t cap, const char* src)
{
    size_t n;
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* =========================================================================
 * 三、WEXT：适配器识别与关联查询（Linux）
 * ========================================================================= */

#if defined(__linux__)

/** @brief 打开一个用于 WEXT ioctl 的 UDP 套接字；失败返回 -1。 */
static int xdevwifi_wextSocket(void)
{
    return socket(AF_INET, SOCK_DGRAM, 0);
}

/**
 * @brief 判断接口是否为无线接口，并取协议描述。
 * @details 双判据：sysfs 的 wireless/phy80211 节点（cfg80211 驱动即使裁掉
 *          WEXT 也能识别）优先，其次 SIOCGIWNAME（WEXT 驱动）。
 */
static bool xdevwifi_isWireless(const char* ifname, char* protocol, size_t cap)
{
    char path[128];
    struct iwreq wr;
    int fd;

    if (protocol && cap) protocol[0] = '\0';
    if (!ifname || !ifname[0]) return false;

    snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", ifname);
    if (access(path, F_OK) == 0) {
        if (protocol && cap) xdevwifi_copyText(protocol, cap, "IEEE 802.11");
        return true;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", ifname);
    if (access(path, F_OK) == 0) {
        if (protocol && cap) xdevwifi_copyText(protocol, cap, "IEEE 802.11 (cfg80211)");
        return true;
    }

    fd = xdevwifi_wextSocket();
    if (fd < 0) return false;
    memset(&wr, 0, sizeof(wr));
    xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
    if (ioctl(fd, SIOCGIWNAME, &wr) == 0) {
        if (protocol && cap) {
            /* u.name 是定长数组，不保证 NUL 结尾。 */
            size_t n = 0;
            while (n + 1 < cap && n < IFNAMSIZ && wr.u.name[n]) {
                protocol[n] = wr.u.name[n];
                ++n;
            }
            protocol[n] = '\0';
        }
        close(fd);
        return true;
    }
    close(fd);
    return false;
}

/** @brief WEXT 关联查询结果。 */
typedef struct XDevWifiAssoc
{
    bool associated;                 /**< 已关联到某个 AP。 */
    char ssid[XDEVWIFI_SSID_MAX];    /**< 当前 ESSID。 */
    char bssid[XDEVWIFI_BSSID_MAX];  /**< 当前 BSSID。 */
    int signal;                      /**< 0..100；-1=未知。 */
    int channel;                     /**< 信道；0=未知。 */
} XDevWifiAssoc;

/** @brief 读取当前关联（ESSID/AP/QUAL/FREQ）；无关联时 associated=false。 */
static void xdevwifi_wextQueryAssoc(const char* ifname, XDevWifiAssoc* out)
{
    struct iwreq wr;
    int fd;
    char ssid[XDEVWIFI_SSID_MAX];

    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->signal = -1;
    if (!ifname || !ifname[0]) return;

    fd = xdevwifi_wextSocket();
    if (fd < 0) return;

    memset(ssid, 0, sizeof(ssid));
    memset(&wr, 0, sizeof(wr));
    xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
    wr.u.essid.pointer = ssid;
    wr.u.essid.length = sizeof(ssid) - 1;
    wr.u.essid.flags = 0;
    if (ioctl(fd, SIOCGIWESSID, &wr) == 0 && wr.u.essid.length > 0) {
        size_t n = wr.u.essid.length;
        if (n >= sizeof(ssid)) n = sizeof(ssid) - 1;
        ssid[n] = '\0';
        xdevwifi_copyText(out->ssid, sizeof(out->ssid), ssid);
        out->associated = (ssid[0] != '\0');
    }

    if (out->associated) {
        memset(&wr, 0, sizeof(wr));
        xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
        if (ioctl(fd, SIOCGIWAP, &wr) == 0) {
            const unsigned char* mac =
                (const unsigned char*)wr.u.ap_addr.sa_data;
            if (mac[0] || mac[1] || mac[2] || mac[3] || mac[4] || mac[5]) {
                snprintf(out->bssid, sizeof(out->bssid),
                         "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
                         mac[2], mac[3], mac[4], mac[5]);
            }
        }
    }

    /* 当前链路质量取 SIOCGIWSTATS（struct iw_statistics.qual）：
     * SIOCGIWQUAL 不是合法的 ioctl 命令号（扫描事件里对应的是 IWEVQUAL）。 */
    {
        struct iw_statistics stats;
        memset(&wr, 0, sizeof(wr));
        memset(&stats, 0, sizeof(stats));
        xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
        wr.u.data.pointer = &stats;
        wr.u.data.length = sizeof(stats);
        wr.u.data.flags = 0;
        if (ioctl(fd, SIOCGIWSTATS, &wr) == 0) {
            const struct iw_quality* q = &stats.qual;
            if ((q->updated & IW_QUAL_LEVEL_UPDATED) &&
                (q->updated & IW_QUAL_DBM)) {
                int level = (int)q->level;
                if (level > 128) level -= 256; /* 老驱动用无符号 dBm。 */
                out->signal = XDevWifiPosix_DbmToPercent(level);
            } else if (q->updated & IW_QUAL_QUAL_UPDATED) {
                out->signal = XDevWifiPosix_QualityToPercent((int)q->qual);
            } else if (q->updated & IW_QUAL_LEVEL_UPDATED) {
                out->signal = XDevWifiPosix_QualityToPercent((int)q->level);
            }
        }
    }

    memset(&wr, 0, sizeof(wr));
    xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
    if (ioctl(fd, SIOCGIWFREQ, &wr) == 0) {
        const struct iw_freq* f = &wr.u.freq;
        if (f->e == 0 && f->m > 0 && f->m < 1000) {
            out->channel = (int)f->m;
        } else if (f->m > 0) {
            long long mhz = (long long)f->m;
            int exponent = (int)f->e;
            while (exponent > 6) { mhz /= 10; --exponent; }
            while (exponent < 6) { mhz *= 10; ++exponent; }
            if (mhz > 0 && mhz < 100000)
                out->channel = XDevWifiPosix_ChannelFromMhz((int)mhz);
        }
    }
    close(fd);
}

/** @brief 发起 WEXT 扫描（立即返回；驱动忙按"已受理"处理）。 */
static bool xdevwifi_wextStartScan(const char* ifname)
{
    struct iwreq wr;
    int fd;
    int rc;

    fd = xdevwifi_wextSocket();
    if (fd < 0) return false;
    memset(&wr, 0, sizeof(wr));
    xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
    rc = ioctl(fd, SIOCSIWSCAN, &wr);
    close(fd);
    if (rc == 0) return true;
    /* EBUSY/EAGAIN：已有扫描在飞，结果照样可取。 */
    return (errno == EBUSY || errno == EAGAIN || errno == EINPROGRESS);
}

/**
 * @brief 取 WEXT 扫描结果。
 * @return 1=已完成（out 已填充）；0=进行中（-EAGAIN）；-1=失败。
 * @note 驱动缓冲区不足时按 E2BIG 倍增重试，上限 XDEVWIFI_SCAN_BUF_MAX。
 */
static int xdevwifi_wextTakeScan(const char* ifname, XVector** out)
{
    size_t capacity = XDEVWIFI_SCAN_BUF_INIT;
    unsigned char* buffer;
    int fd;

    if (!out) return -1;
    *out = NULL;
    fd = xdevwifi_wextSocket();
    if (fd < 0) return -1;
    buffer = (unsigned char*)XMalloc_System(capacity);
    if (!buffer) {
        close(fd);
        return -1;
    }

    for (;;) {
        struct iwreq wr;
        int rc;
        memset(&wr, 0, sizeof(wr));
        xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
        wr.u.data.pointer = buffer;
        wr.u.data.length = (__u16)(capacity > 0xFFFF ? 0xFFFF : capacity);
        wr.u.data.flags = 0;
        rc = ioctl(fd, SIOCGIWSCAN, &wr);
        if (rc != 0) {
            if (errno == E2BIG && capacity < XDEVWIFI_SCAN_BUF_MAX) {
                unsigned char* bigger;
                capacity *= 2;
                bigger = (unsigned char*)XRealloc_System(buffer, capacity);
                if (!bigger) {
                    XFree_System(buffer);
                    close(fd);
                    return -1;
                }
                buffer = bigger;
                continue;
            }
            XFree_System(buffer);
            close(fd);
            if (errno == EAGAIN || errno == EINPROGRESS) return 0;
            return -1;
        }
        {
            bool ok = XDevWifiPosix_ParseWextScan(buffer, wr.u.data.length, out);
            XFree_System(buffer);
            close(fd);
            return ok ? 1 : -1;
        }
    }
}

/**
 * @brief WEXT 连接（仅开放 / WEP；WPA 系必须走 wpa_supplicant）。
 * @details 顺序对齐 iwconfig：先设密钥再设 ESSID，驱动才会在关联时带上
 *          正确的加密参数。
 */
static bool xdevwifi_wextConnect(const char* ifname, const char* ssid,
                                 const char* passphrase,
                                 XDeviceWiFiSecurity security)
{
    struct iwreq wr;
    int fd;
    bool ok = true;

    if (!ifname || !ssid || !ssid[0]) return false;
    if (security != XDeviceWiFi_SecurityOpen &&
        security != XDeviceWiFi_SecurityWep)
        return false;
    if (security == XDeviceWiFi_SecurityWep &&
        (!passphrase || !passphrase[0]))
        return false;

    fd = xdevwifi_wextSocket();
    if (fd < 0) return false;

    if (security == XDeviceWiFi_SecurityWep) {
        char key[128];
        const char* keyValue = passphrase;
        memset(&wr, 0, sizeof(wr));
        xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
        /* WEP 密钥：十六进制直接给，否则按 ASCII 加 "s:" 前缀。 */
        if (!(strlen(keyValue) == 10 || strlen(keyValue) == 26)) {
            snprintf(key, sizeof(key), "s:%s", keyValue);
            keyValue = key;
        }
        wr.u.encoding.pointer = (void*)keyValue;
        wr.u.encoding.length = (__u16)strlen(keyValue);
        wr.u.encoding.flags = IW_ENCODE_OPEN; /* 密钥索引 0 + 开放系统认证。 */
        if (ioctl(fd, SIOCSIWENCODE, &wr) != 0) ok = false;
    }

    if (ok) {
        memset(&wr, 0, sizeof(wr));
        xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
        wr.u.essid.pointer = (void*)ssid;
        wr.u.essid.length = (__u16)strlen(ssid);
        wr.u.essid.flags = 1; /* IW_ESSID_ON */
        if (ioctl(fd, SIOCSIWESSID, &wr) != 0) ok = false;
    }
    close(fd);
    return ok;
}

/** @brief WEXT 断开：清空 ESSID（等价 iwconfig essid off）。 */
static bool xdevwifi_wextDisconnect(const char* ifname)
{
    struct iwreq wr;
    char empty[1] = { '\0' };
    int fd;

    fd = xdevwifi_wextSocket();
    if (fd < 0) return false;
    memset(&wr, 0, sizeof(wr));
    xdevwifi_copyText(wr.ifr_name, IFNAMSIZ, ifname);
    wr.u.essid.pointer = empty;
    wr.u.essid.length = 0;
    wr.u.essid.flags = 0;
    if (ioctl(fd, SIOCSIWESSID, &wr) != 0) {
        close(fd);
        return false;
    }
    close(fd);
    return true;
}

#else /* 非 Linux：BSD 系无 WEXT，后端从 wpa_supplicant / NetworkManager 走。 */

typedef struct XDevWifiAssoc
{
    bool associated;
    char ssid[XDEVWIFI_SSID_MAX];
    char bssid[XDEVWIFI_BSSID_MAX];
    int signal;
    int channel;
} XDevWifiAssoc;

static bool xdevwifi_isWireless(const char* ifname, char* protocol, size_t cap)
{
    (void)ifname;
    if (protocol && cap) protocol[0] = '\0';
    return false;
}

static void xdevwifi_wextQueryAssoc(const char* ifname, XDevWifiAssoc* out)
{
    (void)ifname;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->signal = -1;
}

static bool xdevwifi_wextStartScan(const char* ifname)
{
    (void)ifname;
    return false;
}

static int xdevwifi_wextTakeScan(const char* ifname, XVector** out)
{
    (void)ifname;
    if (out) *out = NULL;
    return -1;
}

static bool xdevwifi_wextConnect(const char* ifname, const char* ssid,
                                 const char* passphrase,
                                 XDeviceWiFiSecurity security)
{
    (void)ifname; (void)ssid; (void)passphrase; (void)security;
    return false;
}

static bool xdevwifi_wextDisconnect(const char* ifname)
{
    (void)ifname;
    return false;
}

#endif /* __linux__ */

/* =========================================================================
 * 四、外部命令（nmcli）
 * ========================================================================= */

/**
 * @brief 执行外部命令并捕获标准输出（有界、带超时）。
 * @details fork + pipe + execvp；父进程非阻塞读并与 waitpid(WNOHANG) 交替
 *          轮询，超过 timeoutMs 直接 SIGKILL，保证不长时间阻塞调用线程。
 *          命令与参数一律经 execvp 数组传递，不经过 shell。
 * @return true=进程正常退出（退出码 0）；false=启动失败/非零退出/超时。
 */
static bool xdevwifi_runCapture(const char* const argv[], char* out,
                                size_t cap, int timeoutMs)
{
    int fds[2];
    pid_t pid;
    int status = 0;
    bool exited = false;
    size_t used = 0;
    int64_t deadline;

    if (!argv || !argv[0] || !out || cap == 0) return false;
    out[0] = '\0';
    if (pipe(fds) != 0) return false;
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        execvp(argv[0], (char* const*)argv);
        _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    deadline = xdevwifi_nowMs() + timeoutMs;

    for (;;) {
        char chunk[512];
        ssize_t n = 0;
        bool atEof = false;
        while ((n = read(fds[0], chunk, sizeof(chunk))) > 0) {
            if (used + (size_t)n < cap) {
                memcpy(out + used, chunk, (size_t)n);
                used += (size_t)n;
                out[used] = '\0';
            }
        }
        if (n == 0) atEof = true;
        if (!exited && waitpid(pid, &status, WNOHANG) == pid) exited = true;
        if (exited && atEof) break; /* 进程已退且管道读空。 */
        if (xdevwifi_nowMs() >= deadline) {
            if (!exited) {
                kill(pid, SIGKILL);
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
            }
            break;
        }
        {
            struct pollfd pfd;
            pfd.fd = fds[0];
            pfd.events = POLLIN;
            pfd.revents = 0;
            (void)poll(&pfd, 1, 20);
        }
    }
    close(fds[0]);
    return exited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/** @brief NetworkManager 是否在运行（结果缓存；探测失败按未运行处理）。 */
static bool xdevwifi_nmAvailable(void)
{
    char output[64];
    const char* argv[] = { "nmcli", "-t", "-f", "RUNNING", "general", NULL };
    if (s_nmRunning >= 0) return s_nmRunning == 1;
    s_nmRunning = 0;
    if (xdevwifi_runCapture(argv, output, sizeof(output),
                            XDEVWIFI_NM_TIMEOUT_MS)) {
        char* newline = strchr(output, '\n');
        if (newline) *newline = '\0';
        if (strcmp(output, "running") == 0) s_nmRunning = 1;
    }
    return s_nmRunning == 1;
}

/** @brief 经 NetworkManager 发起重扫（"扫描太频繁"按已受理处理）。 */
static bool xdevwifi_nmStartScan(const char* ifname)
{
    char output[512];
    const char* argv[] = { "nmcli", "device", "wifi", "rescan",
                           "ifname", ifname, NULL };
    if (!xdevwifi_nmAvailable()) return false;
    if (!xdevwifi_runCapture(argv, output, sizeof(output),
                             XDEVWIFI_NM_TIMEOUT_MS)) {
        /* 系统有 30 秒节流，被拒时缓存仍可取，按受理返回。 */
        if (strstr(output, "Scanning not allowed") != NULL) return true;
        return false;
    }
    return true;
}

/** @brief 经 NetworkManager 取扫描列表（调用方保证不在高频轮询路径）。 */
static bool xdevwifi_nmScanResults(const char* ifname, XVector** out)
{
    char* output;
    bool ok;
    const char* argv[] = { "nmcli", "-t", "-f",
                           "SSID,BSSID,SIGNAL,CHAN,SECURITY",
                           "device", "wifi", "list", "ifname", ifname, NULL };

    if (!out) return false;
    *out = NULL;
    if (!xdevwifi_nmAvailable()) return false;
    output = (char*)XMalloc_System(XDEVWIFI_NM_LIST_MAX);
    if (!output) return false;
    ok = xdevwifi_runCapture(argv, output, XDEVWIFI_NM_LIST_MAX,
                             XDEVWIFI_NM_TIMEOUT_MS);
    if (ok) ok = XDevWifiPosix_ParseNmWifiList(output, out);
    XFree_System(output);
    return ok;
}

/**
 * @brief 经 NetworkManager 查询当前连接详情。
 * @details `nmcli device wifi list` 的 IN-USE 列用 "*" 标记当前正在使用的
 *          AP，据此定位已连接网络。桌面 cfg80211 驱动常不带 WEXT 兼容层，
 *          关联状态无法从 SIOCGIWESSID 读到，故 NM 路径必须自己取连接详情。
 * @param ifname 接口名；@param outInfo 输出（由调用方释放字符串）。
 * @return true=已连接且填好了详情；false=未连接或查询失败。
 */
static bool xdevwifi_nmQueryConnection(const char* ifname,
                                       XDeviceWiFiConnectionInfo* outInfo)
{
    char* output;
    bool found = false;
    const char* argv[] = { "nmcli", "-t", "-f",
                           "IN-USE,SSID,BSSID,SIGNAL,CHAN,SECURITY",
                           "device", "wifi", "list", "ifname", ifname, NULL };

    if (!ifname || !outInfo) return false;
    if (!xdevwifi_nmAvailable()) return false;
    output = (char*)XMalloc_System(XDEVWIFI_NM_LIST_MAX);
    if (!output) return false;
    if (xdevwifi_runCapture(argv, output, XDEVWIFI_NM_LIST_MAX,
                            XDEVWIFI_NM_TIMEOUT_MS)) {
        const char* line = output;
        while (*line && !found) {
            const char* end = strchr(line, '\n');
            char row[512];
            size_t lineLen = end ? (size_t)(end - line) : strlen(line);
            if (lineLen > 0) {
                char* fields[6];
                size_t copy = lineLen < sizeof(row) - 1 ? lineLen : sizeof(row) - 1;
                memcpy(row, line, copy);
                row[copy] = '\0';
                /* IN-USE 列为 "*" 即当前连接。 */
                if (XDevWifiPosix_SplitTerse(row, fields, 6) == 6 &&
                    strchr(fields[0], '*') != NULL) {
                    int signal = atoi(fields[3]);
                    if (signal < 0 || signal > 100) signal = -1;
                    if (fields[1][0]) outInfo->ssid = XString_create_utf8(fields[1]);
                    if (fields[2][0]) outInfo->bssid = XString_create_utf8(fields[2]);
                    outInfo->signalStrength = signal;
                    outInfo->security =
                        XDevWifiPosix_SecurityFromText(fields[5]);
                    outInfo->state = XDeviceWiFi_ConnectConnected;
                    found = true;
                }
            }
            if (!end) break;
            line = end + 1;
        }
    }
    XFree_System(output);
    return found;
}

/**
 * @brief 经 NetworkManager 发起连接（后台子进程，立即返回）。
 * @details nmcli 要等激活完成才退出（默认最长 90s），因此**不能同步等待**：
 *          fork 后立即返回，父进程在 connectState 轮询里 waitpid(WNOHANG)
 *          收口。子进程带 --wait 20，最长约 20s 后自行退出，不会残留。
 */
static bool xdevwifi_nmConnect(XDevWifiSlot* slot, const char* ssid,
                               const char* passphrase)
{
    pid_t pid;
    char ssidArg[XDEVWIFI_SSID_MAX];
    char ifArg[IFNAMSIZ];
    char passArg[XDEVWIFI_SSID_MAX];

    if (!slot || !ssid || !ssid[0]) return false;
    if (!xdevwifi_nmAvailable()) return false;
    /* 已有子进程在跑：先回收，避免僵尸与动作串台。 */
    if (slot->nmChild > 0) {
        int status = 0;
        if (waitpid(slot->nmChild, &status, WNOHANG) == slot->nmChild)
            slot->nmChild = -1;
        else
            return false;
    }
    xdevwifi_copyText(ssidArg, sizeof(ssidArg), ssid);
    xdevwifi_copyText(ifArg, sizeof(ifArg), slot->ifname);
    xdevwifi_copyText(passArg, sizeof(passArg), passphrase ? passphrase : "");

    pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        /* nmcli device wifi connect SSID [password PWD] ifname IF */
        if (passArg[0]) {
            char* argv[] = { "nmcli", "--wait", "20", "device", "wifi",
                             "connect", ssidArg, "password", passArg,
                             "ifname", ifArg, NULL };
            execvp(argv[0], argv);
        } else {
            char* argv[] = { "nmcli", "--wait", "20", "device", "wifi",
                             "connect", ssidArg, "ifname", ifArg, NULL };
            execvp(argv[0], argv);
        }
        _exit(127);
    }
    slot->nmChild = pid;
    return true;
}

/** @brief 经 NetworkManager 断开当前连接。 */
static bool xdevwifi_nmDisconnect(const char* ifname)
{
    char output[256];
    const char* argv[] = { "nmcli", "device", "disconnect", ifname, NULL };
    if (!xdevwifi_nmAvailable()) return false;
    return xdevwifi_runCapture(argv, output, sizeof(output),
                               XDEVWIFI_NM_TIMEOUT_MS);
}

/** @brief 经 NetworkManager 删除已保存的连接（按 SSID 精确匹配）。 */
static bool xdevwifi_nmForget(const char* ssid)
{
    char* output;
    char* line;
    bool removed = false;
    const char* listArgv[] = { "nmcli", "-t", "-f", "NAME,TYPE",
                               "connection", "show", NULL };

    if (!ssid || !ssid[0] || !xdevwifi_nmAvailable()) return false;
    output = (char*)XMalloc_System(32u * 1024u);
    if (!output) return false;
    if (!xdevwifi_runCapture(listArgv, output, 32u * 1024u,
                             XDEVWIFI_NM_TIMEOUT_MS)) {
        XFree_System(output);
        return false;
    }
    line = output;
    while (*line && !removed) {
        char* end = strchr(line, '\n');
        char row[512];
        char* fields[2];
        if (end) *end = '\0';
        xdevwifi_copyText(row, sizeof(row), line);
        if (XDevWifiPosix_SplitTerse(row, fields, 2) == 2 &&
            strstr(fields[1], "wireless") != NULL &&
            strcmp(fields[0], ssid) == 0) {
            char out[256];
            const char* delArgv[] = { "nmcli", "connection", "delete",
                                      fields[0], NULL };
            removed = xdevwifi_runCapture(delArgv, out, sizeof(out),
                                          XDEVWIFI_NM_TIMEOUT_MS);
        }
        if (!end) break;
        line = end + 1;
    }
    XFree_System(output);
    return removed;
}

/* =========================================================================
 * 五、wpa_supplicant 控制接口
 * ========================================================================= */

/**
 * @brief 定位接口的控制口路径。
 * @details 优先接口专属口（/run 与 /var/run 两条惯例路径），其次全局控制口
 *          （此时命令需带 "IFNAME=<if> " 前缀）。
 * @return true=找到。
 */
static bool xdevwifi_wpaResolveCtrl(XDevWifiSlot* slot)
{
    static const char* const kDirs[] = {
        "/run/wpa_supplicant", "/var/run/wpa_supplicant"
    };
    size_t i;

    if (!slot) return false;
    if (slot->wpaTried) return slot->wpaOk;
    slot->wpaTried = true;
    slot->wpaOk = false;
    slot->wpaPath[0] = '\0';

    for (i = 0; i < sizeof(kDirs) / sizeof(kDirs[0]); ++i) {
        struct stat st;
        char path[96];
        snprintf(path, sizeof(path), "%s/%s", kDirs[i], slot->ifname);
        if (stat(path, &st) == 0 && S_ISSOCK(st.st_mode)) {
            xdevwifi_copyText(slot->wpaPath, sizeof(slot->wpaPath), path);
            slot->wpaOk = true;
            return true;
        }
    }
    for (i = 0; i < sizeof(kDirs) / sizeof(kDirs[0]); ++i) {
        struct stat st;
        if (stat(kDirs[i], &st) == 0 && S_ISSOCK(st.st_mode)) {
            xdevwifi_copyText(slot->wpaPath, sizeof(slot->wpaPath), kDirs[i]);
            slot->wpaOk = true;
            return true;
        }
    }
    return false;
}

/** @brief 控制口是否为"全局口"（命令需加 IFNAME 前缀）。 */
static bool xdevwifi_wpaIsGlobal(const XDevWifiSlot* slot)
{
    const char* base;
    if (!slot || !slot->wpaPath[0]) return false;
    base = strrchr(slot->wpaPath, '/');
    if (!base) return false;
    return strcmp(base, "/wpa_supplicant") == 0;
}

/**
 * @brief 向 wpa_supplicant 发一条命令并取应答。
 * @return true=取得应答（内容可能是 "FAIL"）；false=通信失败/超时。
 * @note 数据报套接字 connect 不产生网络往返；send/recv 全程受
 *       XDEVWIFI_WPA_TIMEOUT_MS 限制，不会拖住调用线程。
 */
static bool xdevwifi_wpaCommand(XDevWifiSlot* slot, const char* cmd,
                                char* reply, size_t cap)
{
    struct sockaddr_un address;
    struct pollfd pfd;
    char full[XDEVWIFI_WPA_CMD_MAX];
    int fd;
    ssize_t n;
    size_t cmdLen;

    if (!slot || !cmd || !reply || cap == 0) return false;
    if (!xdevwifi_wpaResolveCtrl(slot)) return false;
    reply[0] = '\0';
    if (xdevwifi_wpaIsGlobal(slot))
        snprintf(full, sizeof(full), "IFNAME=%s %s", slot->ifname, cmd);
    else
        xdevwifi_copyText(full, sizeof(full), cmd);

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    xdevwifi_copyText(address.sun_path, sizeof(address.sun_path), slot->wpaPath);
    if (connect(fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
        close(fd);
        return false;
    }
    cmdLen = strlen(full);
    n = send(fd, full, cmdLen, 0);
    if (n < 0 || (size_t)n != cmdLen) {
        close(fd);
        return false;
    }
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, XDEVWIFI_WPA_TIMEOUT_MS) <= 0) {
        close(fd);
        return false;
    }
    n = recv(fd, reply, cap - 1, 0);
    close(fd);
    if (n < 0) return false;
    reply[n] = '\0';
    while (n > 0 && (reply[n - 1] == '\n' || reply[n - 1] == '\r'))
        reply[--n] = '\0';
    return true;
}

/** @brief 应答是否为成功（"OK" 或 ADD_NETWORK 返回的纯数字 id）。 */
static bool xdevwifi_wpaReplyOk(const char* reply)
{
    if (!reply || !reply[0]) return false;
    if (strcmp(reply, "OK") == 0) return true;
    if (strncmp(reply, "FAIL", 4) == 0) return false;
    if (strncmp(reply, "ERR", 3) == 0) return false;
    return true;
}

/** @brief 经 wpa_supplicant 发起连接（WPA/WPA2/WPA3 及开放/WEP）。 */
static bool xdevwifi_wpaConnect(XDevWifiSlot* slot, const char* ssid,
                                const char* passphrase,
                                XDeviceWiFiSecurity security)
{
    char reply[XDEVWIFI_WPA_REPLY_MAX];
    char command[XDEVWIFI_WPA_CMD_MAX];
    char ssidValue[XDEVWIFI_WPA_CMD_MAX / 2];
    char pskValue[XDEVWIFI_WPA_CMD_MAX / 2];
    int networkId;
    bool ok = true;

    if (!xdevwifi_wpaResolveCtrl(slot)) return false;
    /* 企业认证需要证书/身份材料，本接口签名不承载，如实拒绝。 */
    if (security == XDeviceWiFi_SecurityWpa2Enterprise) return false;
    if (!XDevWifiPosix_BuildWpaValue(ssid, ssidValue, sizeof(ssidValue)))
        return false;

    if (!xdevwifi_wpaCommand(slot, "ADD_NETWORK", reply, sizeof(reply)) ||
        !xdevwifi_wpaReplyOk(reply))
        return false;
    networkId = atoi(reply);
    if (networkId < 0) return false;

#define XDEVWIFI_WPA_SET(...)                                              \
    do {                                                                   \
        snprintf(command, sizeof(command), __VA_ARGS__);                   \
        if (!xdevwifi_wpaCommand(slot, command, reply, sizeof(reply)) ||   \
            !xdevwifi_wpaReplyOk(reply)) {                                 \
            ok = false;                                                    \
            goto cleanup;                                                  \
        }                                                                  \
    } while (0)

    XDEVWIFI_WPA_SET("SET_NETWORK %d ssid %s", networkId, ssidValue);

    if (security == XDeviceWiFi_SecurityOpen) {
        XDEVWIFI_WPA_SET("SET_NETWORK %d key_mgmt NONE", networkId);
    } else if (security == XDeviceWiFi_SecurityWep) {
        if (!passphrase || !passphrase[0] ||
            !XDevWifiPosix_BuildWpaValue(passphrase, pskValue, sizeof(pskValue))) {
            ok = false;
            goto cleanup;
        }
        XDEVWIFI_WPA_SET("SET_NETWORK %d key_mgmt NONE", networkId);
        XDEVWIFI_WPA_SET("SET_NETWORK %d wep_key0 %s", networkId, pskValue);
        XDEVWIFI_WPA_SET("SET_NETWORK %d wep_tx_keyidx 0", networkId);
    } else {
        if (!passphrase || !passphrase[0] ||
            !XDevWifiPosix_BuildWpaValue(passphrase, pskValue, sizeof(pskValue))) {
            ok = false;
            goto cleanup;
        }
        XDEVWIFI_WPA_SET("SET_NETWORK %d psk %s", networkId, pskValue);
        if (security == XDeviceWiFi_SecurityWpa3Sae)
            XDEVWIFI_WPA_SET("SET_NETWORK %d key_mgmt SAE", networkId);
        else
            XDEVWIFI_WPA_SET("SET_NETWORK %d key_mgmt WPA-PSK", networkId);
    }
    XDEVWIFI_WPA_SET("ENABLE_NETWORK %d", networkId);
    XDEVWIFI_WPA_SET("SELECT_NETWORK %d", networkId);
    XDEVWIFI_WPA_SET("%s", "SAVE_CONFIG");

cleanup:
#undef XDEVWIFI_WPA_SET
    if (!ok) {
        snprintf(command, sizeof(command), "REMOVE_NETWORK %d", networkId);
        (void)xdevwifi_wpaCommand(slot, command, reply, sizeof(reply));
    }
    return ok;
}

/** @brief 经 wpa_supplicant 删除与 ssid 匹配的已保存网络。 */
static bool xdevwifi_wpaForget(XDevWifiSlot* slot, const char* ssid)
{
    char reply[XDEVWIFI_WPA_REPLY_MAX];
    const char* line;

    if (!xdevwifi_wpaResolveCtrl(slot)) return false;
    if (!xdevwifi_wpaCommand(slot, "LIST_NETWORKS", reply, sizeof(reply)))
        return false;
    line = strchr(reply, '\n'); /* 跳过表头。 */
    if (!line) return false;
    line += 1;
    while (*line) {
        const char* end = strchr(line, '\n');
        size_t lineLen = end ? (size_t)(end - line) : strlen(line);
        if (lineLen > 0) {
            char row[512];
            size_t copy = lineLen < sizeof(row) - 1 ? lineLen : sizeof(row) - 1;
            char* firstTab;
            memcpy(row, line, copy);
            row[copy] = '\0';
            firstTab = strchr(row, '\t');
            if (firstTab) {
                char* secondTab;
                *firstTab = '\0';
                secondTab = strchr(firstTab + 1, '\t');
                if (secondTab) *secondTab = '\0';
                if (strcmp(firstTab + 1, ssid) == 0) {
                    char command[64];
                    snprintf(command, sizeof(command), "REMOVE_NETWORK %s", row);
                    if (xdevwifi_wpaCommand(slot, command, reply,
                                            sizeof(reply)) &&
                        xdevwifi_wpaReplyOk(reply)) {
                        (void)xdevwifi_wpaCommand(slot, "SAVE_CONFIG", reply,
                                                  sizeof(reply));
                        return true;
                    }
                    return false;
                }
            }
        }
        if (!end) break;
        line = end + 1;
    }
    return false;
}

/** @brief 从 `wpa_cli STATUS` 推算连接状态，并可回带 ssid/bssid/key_mgmt。 */
static XDeviceWiFiConnectState xdevwifi_wpaState(XDevWifiSlot* slot,
                                                 char* ssidOut, size_t ssidCap,
                                                 char* bssidOut, size_t bssidCap,
                                                 char* keyMgmtOut,
                                                 size_t keyMgmtCap)
{
    char reply[XDEVWIFI_WPA_REPLY_MAX];
    char state[32];

    if (ssidOut && ssidCap) ssidOut[0] = '\0';
    if (bssidOut && bssidCap) bssidOut[0] = '\0';
    if (keyMgmtOut && keyMgmtCap) keyMgmtOut[0] = '\0';
    if (!xdevwifi_wpaCommand(slot, "STATUS", reply, sizeof(reply)))
        return XDeviceWiFi_ConnectIdle;
    if (ssidOut) XDevWifiPosix_StatusValue(reply, "ssid", ssidOut, ssidCap);
    if (bssidOut) XDevWifiPosix_StatusValue(reply, "bssid", bssidOut, bssidCap);
    if (keyMgmtOut)
        XDevWifiPosix_StatusValue(reply, "key_mgmt", keyMgmtOut, keyMgmtCap);
    if (!XDevWifiPosix_StatusValue(reply, "wpa_state", state, sizeof(state)))
        return XDeviceWiFi_ConnectIdle;
    if (strcmp(state, "COMPLETED") == 0) return XDeviceWiFi_ConnectConnected;
    if (strcmp(state, "ASSOCIATING") == 0 ||
        strcmp(state, "ASSOCIATED") == 0 ||
        strcmp(state, "4WAY_HANDSHAKE") == 0 ||
        strcmp(state, "GROUP_HANDSHAKE") == 0 ||
        strcmp(state, "SCANNING") == 0)
        return XDeviceWiFi_ConnectConnecting;
    return XDeviceWiFi_ConnectIdle;
}

/** @brief 经 wpa_supplicant 取扫描结果；true=结果已就绪。 */
static bool xdevwifi_wpaScanResults(XDevWifiSlot* slot, XVector** out)
{
    char reply[XDEVWIFI_WPA_REPLY_MAX];
    if (!xdevwifi_wpaCommand(slot, "SCAN_RESULTS", reply, sizeof(reply)))
        return false;
    if (strncmp(reply, "FAIL", 4) == 0) return false;
    return XDevWifiPosix_ParseWpaScanResults(reply, out);
}

/* =========================================================================
 * 六、槽位管理与后端判定
 * ========================================================================= */

/** @brief 释放槽位持有的运行时资源并复位字段。 */
static void xdevwifi_slotRelease(XDevWifiSlot* slot)
{
    if (!slot) return;
    if (slot->scanCache) {
        XDeviceWiFi_freeNetworks(slot->scanCache);
        slot->scanCache = NULL;
    }
    if (slot->nmChild > 0) {
        int status = 0;
        if (waitpid(slot->nmChild, &status, WNOHANG) == 0)
            kill(slot->nmChild, SIGKILL);
        while (waitpid(slot->nmChild, &status, 0) < 0 && errno == EINTR) { }
        slot->nmChild = -1;
    }
    memset(slot, 0, sizeof(*slot));
    slot->nmChild = -1;
}

/**
 * @brief 扫描 /sys/class/net 刷新无线适配器槽位表。
 * @details 保留同名接口的既有状态（不打断在飞扫描/连接），移除已消失的
 *          接口并释放其缓存——USB 网卡热插拔可用。
 * @return 当前无线适配器数量。
 */
static int xdevwifi_refreshSlots(void)
{
    DIR* dir;
    struct dirent* entry;
    bool seen[XDEVICE_WIFI_MAX_ADAPTERS];
    int count = 0;
    int i;

    memset(seen, 0, sizeof(seen));
    dir = opendir("/sys/class/net");
    if (!dir) return 0;
    while ((entry = readdir(dir)) != NULL) {
        char protocol[IFNAMSIZ];
        if (entry->d_name[0] == '.') continue;
        if (!xdevwifi_isWireless(entry->d_name, protocol, sizeof(protocol)))
            continue;
        for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i) {
            if (s_slots[i].used &&
                strcmp(s_slots[i].ifname, entry->d_name) == 0)
                break;
        }
        if (i == XDEVICE_WIFI_MAX_ADAPTERS) {
            for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i) {
                if (!s_slots[i].used) break;
            }
            if (i == XDEVICE_WIFI_MAX_ADAPTERS) continue; /* 槽位耗尽。 */
            memset(&s_slots[i], 0, sizeof(s_slots[i]));
            s_slots[i].nmChild = -1;
            xdevwifi_copyText(s_slots[i].ifname, sizeof(s_slots[i].ifname),
                              entry->d_name);
            s_slots[i].used = true;
            s_slots[i].scanState = XDeviceWiFi_ScanIdle;
            s_slots[i].connectState = XDeviceWiFi_ConnectIdle;
        }
        xdevwifi_copyText(s_slots[i].protocol, sizeof(s_slots[i].protocol),
                          protocol);
        s_slots[i].ifIndex = if_nametoindex(entry->d_name);
        seen[i] = true;
        ++count;
    }
    closedir(dir);
    for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i) {
        if (s_slots[i].used && !seen[i]) xdevwifi_slotRelease(&s_slots[i]);
    }
    return count;
}

/** @brief 由 adapterId 找槽位（接受 "wlan0" / "/dev/wlan0" / sysfs 路径）。 */
static XDevWifiSlot* xdevwifi_findSlot(const char* adapterId)
{
    const char* name = adapterId;
    int i;

    if (!adapterId || !adapterId[0]) return NULL;
    if (strncmp(name, "/dev/", 5) == 0) {
        name += 5;
    } else {
        const char* marker = strstr(name, "/sys/class/net/");
        if (marker) name = marker + strlen("/sys/class/net/");
    }
    for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i) {
        if (s_slots[i].used && strcmp(s_slots[i].ifname, name) == 0)
            return &s_slots[i];
    }
    /* 未命中：可能是刚热插拔进来的接口，刷新后再找一次。 */
    if (xdevwifi_refreshSlots() > 0) {
        for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i) {
            if (s_slots[i].used && strcmp(s_slots[i].ifname, name) == 0)
                return &s_slots[i];
        }
    }
    return NULL;
}

/**
 * @brief 判定槽位实际使用的后端（优先级：NM > wpa > WEXT）。
 * @details 三者并存时以上层管理者为准：NM 在跑时接口由它管理，直接向
 *          wpa_supplicant 发命令会与 NM 的调度打架。
 */
typedef enum XDevWifiBackend {
    XDevWifi_BackendNone = 0,
    XDevWifi_BackendWext,
    XDevWifi_BackendWpa,
    XDevWifi_BackendNm
} XDevWifiBackend;

static XDevWifiBackend xdevwifi_backendOf(XDevWifiSlot* slot)
{
    if (!slot) return XDevWifi_BackendNone;
    if (xdevwifi_nmAvailable()) return XDevWifi_BackendNm;
    if (xdevwifi_wpaResolveCtrl(slot)) return XDevWifi_BackendWpa;
#if defined(__linux__)
    return XDevWifi_BackendWext;
#else
    return XDevWifi_BackendNone;
#endif
}

/** @brief 槽位能力位（按后端如实给出）。 */
static uint32_t xdevwifi_slotCapabilities(XDevWifiSlot* slot)
{
    switch (xdevwifi_backendOf(slot)) {
    case XDevWifi_BackendNm:
    case XDevWifi_BackendWpa:
        return XDeviceWiFi_CapScan | XDeviceWiFi_CapConnect |
               XDeviceWiFi_CapDisconnect | XDeviceWiFi_CapProfile;
    case XDevWifi_BackendWext:
        /* WEXT 原生只能连开放/WEP，没有 profile 概念。 */
        return XDeviceWiFi_CapScan | XDeviceWiFi_CapConnect |
               XDeviceWiFi_CapDisconnect;
    default:
        return 0;
    }
}

/* =========================================================================
 * 七、平台叶子实现
 * ========================================================================= */

bool XDeviceWiFi_platformOpen(void)
{
    if (s_refs == 0) {
        bool backendReady = false;
        int i;

        memset(s_slots, 0, sizeof(s_slots));
        for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i)
            s_slots[i].nmChild = -1;
        s_nmRunning = -1;

        /* 无无线接口即打开失败：上层据此禁用无线入口，不出现"会话已开、
         * 列表空、按钮可点但没反应"的半可用状态。 */
        if (xdevwifi_refreshSlots() == 0) return false;
        /* 有接口但后端全不可用（无 WEXT、无 wpa 控制口、无 NM）：同样如实
         * 失败。逐个槽位判定，任一可用即成功（多网卡不必同源）。 */
        for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i) {
            if (!s_slots[i].used) continue;
            if (xdevwifi_backendOf(&s_slots[i]) != XDevWifi_BackendNone) {
                backendReady = true;
                break;
            }
        }
        if (!backendReady) return false;
    }
    s_refs++;
    return true;
}

void XDeviceWiFi_platformClose(void)
{
    int i;
    if (s_refs > 0) s_refs--;
    if (s_refs > 0) return;
    for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i)
        xdevwifi_slotRelease(&s_slots[i]);
    s_nmRunning = -1;
}

bool XDeviceWiFi_platformCapabilities(uint32_t* outCapabilities)
{
    uint32_t capabilities = 0;
    int i;

    if (!outCapabilities) return false;
    *outCapabilities = 0;
    if (s_refs <= 0) return false;
    for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i) {
        if (!s_slots[i].used) continue;
        capabilities |= xdevwifi_slotCapabilities(&s_slots[i]);
    }
    *outCapabilities = capabilities;
    /* 无适配器时能力位为 0 也算查询成功：由上层按空列表处理。 */
    return true;
}

bool XDeviceWiFi_platformEnumAdapters(XVector** outAdapters)
{
    XVector* adapters;
    int i;

    if (!outAdapters) return false;
    *outAdapters = NULL;
    adapters = XVector_Create(XDeviceWiFiAdapterInfo*);
    if (!adapters) return false;
    *outAdapters = adapters;
    if (s_refs <= 0) return true;
    xdevwifi_refreshSlots();
    for (i = 0; i < XDEVICE_WIFI_MAX_ADAPTERS; ++i) {
        XDevWifiSlot* slot = &s_slots[i];
        XDeviceWiFiAdapterInfo* info;
        if (!slot->used) continue;
        info = (XDeviceWiFiAdapterInfo*)XCalloc_System(1, sizeof(*info));
        if (!info) continue;
        info->id = XString_create_utf8(slot->ifname);
        info->name = XString_create_utf8(slot->ifname);
        info->description = slot->protocol[0]
                                ? XString_create_utf8(slot->protocol)
                                : NULL;
        info->ifIndex = slot->ifIndex;
        info->capabilities = xdevwifi_slotCapabilities(slot);
        info->mode = XDeviceWiFi_ModeStation;
        if (!XVector_push_back_1_base(adapters, &info))
            XDeviceWiFi_freeAdapterInfo(info);
    }
    return true;
}

bool XDeviceWiFi_platformStartScan(const char* adapterId)
{
    XDevWifiSlot* slot = xdevwifi_findSlot(adapterId);
    bool started = false;

    if (!slot) return false;
    if (slot->scanState == XDeviceWiFi_ScanRunning) return false;
    if (slot->scanCache) {
        XDeviceWiFi_freeNetworks(slot->scanCache);
        slot->scanCache = NULL;
    }
    switch (xdevwifi_backendOf(slot)) {
    case XDevWifi_BackendNm:
        started = xdevwifi_nmStartScan(slot->ifname);
        break;
    case XDevWifi_BackendWpa: {
        char reply[XDEVWIFI_WPA_REPLY_MAX];
        if (xdevwifi_wpaCommand(slot, "SCAN", reply, sizeof(reply))) {
            /* FAIL-BUSY：已有扫描在飞，结果照样可取。 */
            started = xdevwifi_wpaReplyOk(reply) ||
                      strstr(reply, "FAIL-BUSY") != NULL;
        }
        break;
    }
    case XDevWifi_BackendWext:
        started = xdevwifi_wextStartScan(slot->ifname);
        break;
    default:
        break;
    }
    if (!started) {
        slot->scanState = XDeviceWiFi_ScanFailed;
        return false;
    }
    slot->scanState = XDeviceWiFi_ScanRunning;
    slot->scanStartMs = xdevwifi_nowMs();
    return true;
}

XDeviceWiFiScanState XDeviceWiFi_platformScanState(const char* adapterId)
{
    XDevWifiSlot* slot = xdevwifi_findSlot(adapterId);
    int64_t elapsed;
    XDevWifiBackend backend;

    if (!slot) return XDeviceWiFi_ScanIdle;
    if (slot->scanState != XDeviceWiFi_ScanRunning) return slot->scanState;
    elapsed = xdevwifi_nowMs() - slot->scanStartMs;
    if (elapsed < XDEVWIFI_RESCAN_MIN_MS) return XDeviceWiFi_ScanRunning;
    backend = xdevwifi_backendOf(slot);

    /* 轮询路径不做重活：NM 的列表查询要拉子进程，留到 takeScanResults；
     * 这里按"缓存即结果"收口（与 android 后端同口径）。 */
    if (backend == XDevWifi_BackendNm) {
        slot->scanState = XDeviceWiFi_ScanCompleted;
        return slot->scanState;
    }

    if (backend == XDevWifi_BackendWpa) {
        XVector* networks = NULL;
        if (xdevwifi_wpaScanResults(slot, &networks) && networks &&
            XVector_size_base((XContainer*)networks) > 0) {
            slot->scanCache = networks;
            slot->scanState = XDeviceWiFi_ScanCompleted;
            return slot->scanState;
        }
        XDeviceWiFi_freeNetworks(networks);
        if (elapsed >= XDEVWIFI_SCAN_TIMEOUT_MS) {
            slot->scanState = XDeviceWiFi_ScanFailed;
            return slot->scanState;
        }
        return XDeviceWiFi_ScanRunning;
    }

    if (backend == XDevWifi_BackendWext) {
        XVector* networks = NULL;
        int rc = xdevwifi_wextTakeScan(slot->ifname, &networks);
        if (rc == 1) {
            slot->scanCache = networks;
            slot->scanState = XDeviceWiFi_ScanCompleted;
            return slot->scanState;
        }
        if (rc < 0 || elapsed >= XDEVWIFI_SCAN_TIMEOUT_MS) {
            slot->scanState = XDeviceWiFi_ScanFailed;
            return slot->scanState;
        }
        return XDeviceWiFi_ScanRunning;
    }
    slot->scanState = XDeviceWiFi_ScanFailed;
    return slot->scanState;
}

bool XDeviceWiFi_platformTakeScanResults(const char* adapterId,
                                         XVector** outNetworks)
{
    XDevWifiSlot* slot = xdevwifi_findSlot(adapterId);

    if (!outNetworks) return false;
    *outNetworks = NULL;
    if (!slot) return false;
    if (slot->scanState != XDeviceWiFi_ScanCompleted) return false;
    slot->scanState = XDeviceWiFi_ScanIdle;
    if (!slot->scanCache && xdevwifi_backendOf(slot) == XDevWifi_BackendNm)
        (void)xdevwifi_nmScanResults(slot->ifname, &slot->scanCache);
    if (!slot->scanCache) {
        /* 扫描完成但一条都没有：按契约交出空容器（非 NULL）。 */
        slot->scanCache = XVector_Create(XDeviceWiFiNetworkInfo*);
        if (!slot->scanCache) return false;
    }
    *outNetworks = slot->scanCache;
    slot->scanCache = NULL;
    return true;
}

bool XDeviceWiFi_platformStartConnect(const char* adapterId, const char* ssid,
                                      const char* passphrase,
                                      XDeviceWiFiSecurity security)
{
    XDevWifiSlot* slot = xdevwifi_findSlot(adapterId);
    bool accepted = false;

    if (!slot || !ssid || !ssid[0]) return false;
    switch (xdevwifi_backendOf(slot)) {
    case XDevWifi_BackendNm:
        accepted = xdevwifi_nmConnect(slot, ssid, passphrase);
        break;
    case XDevWifi_BackendWpa:
        accepted = xdevwifi_wpaConnect(slot, ssid, passphrase, security);
        break;
    case XDevWifi_BackendWext:
        accepted = xdevwifi_wextConnect(slot->ifname, ssid, passphrase,
                                        security);
        break;
    default:
        break;
    }
    if (!accepted) {
        slot->connectState = XDeviceWiFi_ConnectFailed;
        return false;
    }
    xdevwifi_copyText(slot->connectSsid, sizeof(slot->connectSsid), ssid);
    slot->connectState = XDeviceWiFi_ConnectConnecting;
    slot->connectStartMs = xdevwifi_nowMs();
    return true;
}

XDeviceWiFiConnectState XDeviceWiFi_platformConnectState(const char* adapterId)
{
    XDevWifiSlot* slot = xdevwifi_findSlot(adapterId);
    XDevWifiBackend backend;

    if (!slot) return XDeviceWiFi_ConnectIdle;
    backend = xdevwifi_backendOf(slot);

    if (slot->connectState == XDeviceWiFi_ConnectConnected) {
        /* 已连接态做一次廉价复核：链路掉开时如实回到 Idle。
         * NM 路径用"活动 AP 标记"复核（桌面 cfg80211 驱动未必有 WEXT
         * 兼容层，SIOCGIWESSID 读不到关联态）。 */
        if (backend == XDevWifi_BackendNm) {
            XDeviceWiFiConnectionInfo info;
            memset(&info, 0, sizeof(info));
            if (xdevwifi_nmQueryConnection(slot->ifname, &info)) {
                XDeviceWiFi_freeConnectionInfo(&info);
            } else {
                slot->connectState = XDeviceWiFi_ConnectIdle;
            }
            return slot->connectState;
        }
        {
            XDevWifiAssoc assoc;
            xdevwifi_wextQueryAssoc(slot->ifname, &assoc);
            if (!assoc.associated) slot->connectState = XDeviceWiFi_ConnectIdle;
        }
        return slot->connectState;
    }
    if (slot->connectState != XDeviceWiFi_ConnectConnecting)
        return slot->connectState;

    /* wpa 路径以 wpa_state 为准：只关联成功但四次握手失败不能算连上。 */
    if (backend == XDevWifi_BackendWpa) {
        XDeviceWiFiConnectState state =
            xdevwifi_wpaState(slot, NULL, 0, NULL, 0, NULL, 0);
        if (state == XDeviceWiFi_ConnectConnected) {
            slot->connectState = XDeviceWiFi_ConnectConnected;
            return slot->connectState;
        }
        if (state == XDeviceWiFi_ConnectConnecting)
            return XDeviceWiFi_ConnectConnecting;
    }

    if (slot->nmChild > 0) {
        int status = 0;
        if (waitpid(slot->nmChild, &status, WNOHANG) == slot->nmChild) {
            slot->nmChild = -1;
            slot->connectState = (WIFEXITED(status) &&
                                  WEXITSTATUS(status) == 0)
                                     ? XDeviceWiFi_ConnectConnected
                                     : XDeviceWiFi_ConnectFailed;
            return slot->connectState;
        }
    }

    /* NM 路径的快速判定：nmcli 还在跑，但 NM 已把某个 AP 标记为活动即算
     * 成功（桌面 cfg80211 驱动未必有 WEXT 兼容层，关联态读不到）。 */
    if (backend == XDevWifi_BackendNm) {
        XDeviceWiFiConnectionInfo info;
        memset(&info, 0, sizeof(info));
        if (xdevwifi_nmQueryConnection(slot->ifname, &info)) {
            bool matched = (!slot->connectSsid[0] || !info.ssid ||
                             strcmp(XString_toUtf8(info.ssid),
                                    slot->connectSsid) == 0);
            XDeviceWiFi_freeConnectionInfo(&info);
            if (matched) {
                slot->connectState = XDeviceWiFi_ConnectConnected;
                return slot->connectState;
            }
        }
    }

    if (xdevwifi_nowMs() - slot->connectStartMs >= XDEVWIFI_CONNECT_TIMEOUT_MS) {
        if (backend == XDevWifi_BackendNm) {
            XDeviceWiFiConnectionInfo info;
            memset(&info, 0, sizeof(info));
            if (xdevwifi_nmQueryConnection(slot->ifname, &info)) {
                bool matched = (!slot->connectSsid[0] || !info.ssid ||
                                 strcmp(XString_toUtf8(info.ssid),
                                        slot->connectSsid) == 0);
                XDeviceWiFi_freeConnectionInfo(&info);
                slot->connectState = matched ? XDeviceWiFi_ConnectConnected
                                             : XDeviceWiFi_ConnectFailed;
            } else {
                slot->connectState = XDeviceWiFi_ConnectFailed;
            }
            return slot->connectState;
        }
        {
            XDevWifiAssoc assoc;
            xdevwifi_wextQueryAssoc(slot->ifname, &assoc);
            if (assoc.associated && slot->connectSsid[0] &&
                strcmp(assoc.ssid, slot->connectSsid) == 0)
                slot->connectState = XDeviceWiFi_ConnectConnected;
            else
                slot->connectState = XDeviceWiFi_ConnectFailed;
        }
        return slot->connectState;
    }
    return XDeviceWiFi_ConnectConnecting;
}

bool XDeviceWiFi_platformQueryConnection(const char* adapterId,
                                         XDeviceWiFiConnectionInfo* outInfo)
{
    XDevWifiSlot* slot = xdevwifi_findSlot(adapterId);
    XDevWifiAssoc assoc;
    XDevWifiBackend backend;

    if (!slot || !outInfo) return false;
    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->signalStrength = -1;
    outInfo->security = XDeviceWiFi_SecurityUnknown;
    outInfo->state = XDeviceWiFi_ConnectIdle;

    backend = xdevwifi_backendOf(slot);
    if (backend == XDevWifi_BackendNm) {
        /* NM 路径：桌面 cfg80211 驱动未必带 WEXT 兼容层，关联态读不到，
         * 连接详情由 NM 的"活动 AP 标记"给出。 */
        if (xdevwifi_nmQueryConnection(slot->ifname, outInfo)) return true;
        return false;
    }
    if (backend == XDevWifi_BackendWpa) {
        /* wpa 是权威来源：SSID 编码（含非 UTF-8）与 wpa_state 都更准。 */
        char ssid[XDEVWIFI_SSID_MAX];
        char bssid[XDEVWIFI_BSSID_MAX];
        char keyMgmt[64];
        XDeviceWiFiConnectState state =
            xdevwifi_wpaState(slot, ssid, sizeof(ssid), bssid, sizeof(bssid),
                              keyMgmt, sizeof(keyMgmt));
        if (state == XDeviceWiFi_ConnectConnected) {
            xdevwifi_wextQueryAssoc(slot->ifname, &assoc);
            if (ssid[0]) outInfo->ssid = XString_create_utf8(ssid);
            else if (assoc.ssid[0])
                outInfo->ssid = XString_create_utf8(assoc.ssid);
            if (bssid[0]) outInfo->bssid = XString_create_utf8(bssid);
            else if (assoc.bssid[0])
                outInfo->bssid = XString_create_utf8(assoc.bssid);
            outInfo->signalStrength = assoc.signal;
            outInfo->state = XDeviceWiFi_ConnectConnected;
            if (strstr(keyMgmt, "SAE"))
                outInfo->security = XDeviceWiFi_SecurityWpa3Sae;
            else if (strstr(keyMgmt, "802.1X"))
                outInfo->security = XDeviceWiFi_SecurityWpa2Enterprise;
            else if (strstr(keyMgmt, "PSK"))
                outInfo->security = XDeviceWiFi_SecurityWpa2Psk;
            else if (strcmp(keyMgmt, "NONE") == 0)
                outInfo->security = XDeviceWiFi_SecurityOpen;
            return true;
        }
    }

    xdevwifi_wextQueryAssoc(slot->ifname, &assoc);
    if (!assoc.associated) return false;
    if (assoc.ssid[0]) outInfo->ssid = XString_create_utf8(assoc.ssid);
    if (assoc.bssid[0]) outInfo->bssid = XString_create_utf8(assoc.bssid);
    outInfo->signalStrength = assoc.signal;
    outInfo->state = XDeviceWiFi_ConnectConnected;
    return true;
}

bool XDeviceWiFi_platformDisconnect(const char* adapterId)
{
    XDevWifiSlot* slot = xdevwifi_findSlot(adapterId);
    bool done = false;

    if (!slot) return false;
    switch (xdevwifi_backendOf(slot)) {
    case XDevWifi_BackendNm:
        done = xdevwifi_nmDisconnect(slot->ifname);
        break;
    case XDevWifi_BackendWpa: {
        char reply[XDEVWIFI_WPA_REPLY_MAX];
        if (xdevwifi_wpaCommand(slot, "DISCONNECT", reply, sizeof(reply)))
            done = xdevwifi_wpaReplyOk(reply);
        break;
    }
    case XDevWifi_BackendWext:
        done = xdevwifi_wextDisconnect(slot->ifname);
        break;
    default:
        break;
    }
    if (!done) return false;
    slot->connectState = XDeviceWiFi_ConnectIdle;
    slot->connectSsid[0] = '\0';
    return true;
}

bool XDeviceWiFi_platformForgetProfile(const char* adapterId, const char* ssid)
{
    XDevWifiSlot* slot = xdevwifi_findSlot(adapterId);
    if (!slot || !ssid || !ssid[0]) return false;
    switch (xdevwifi_backendOf(slot)) {
    case XDevWifi_BackendNm:
        return xdevwifi_nmForget(ssid);
    case XDevWifi_BackendWpa:
        return xdevwifi_wpaForget(slot, ssid);
    default:
        return false; /* WEXT 无已保存配置概念。 */
    }
}

#endif /* POSIX 平台 */

#endif /* XDEVICE_ON && XDEVICE_WIFI_ON */
