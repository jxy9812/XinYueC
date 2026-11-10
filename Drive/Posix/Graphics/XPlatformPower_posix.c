/******************************************************************************
 * @file       XPlatformPower_posix.c
 * @brief      XPlatformPower 契约的 POSIX 实现（fbdev 熄屏 + 系统挂起 +
 *             X11 DPMS 熄屏）。
 * @details    本文件同时承载两个互不排斥的能力面，各自带独立探测：
 *
 *             ① fbdev 面（嵌入式的现实路径）：
 *                - 熄屏三级降级（运行时探测，按优先级选一条并缓存）：
 *                  L1 背光 PWM（/sys/class/backlight 下各设备目录的
 *                     brightness 节点写 0）——
 *                     **真正断电降耗**，是嵌入式唯一有意义的熄屏手段；
 *                  L2 blank sysfs（/sys/class/graphics/fb0/blank 写 1）——
 *                     断扫描输出，驱动不实现 blank 回调时无效；
 *                  L3 FBIOBLANK ioctl（FB_BLANK_NORMAL/UNBLANK）—— 与 L2
 *                     同命脉（内核转同一个 fb_blank 回调），且现代内核的
 *                     DRM fbdev 适配层**已移除 legacy blank 回调**，在
 *                     sun4i-drmdrmfb 这类目标板上大概率返回 EINVAL，
 *                     故只作兜底；用 FB_BLANK_NORMAL 而非
 *                     FB_BLANK_POWERDOWN（后者断面板电源，唤醒不可靠）。
 *                - 系统挂起：写 /sys/power/state（mem=挂起到内存、
 *                  freeze=冻结 CPU）。写者自身会被一并冻结，系统恢复后
 *                  继续执行——语义天然正确。需 root；无该节点或写失败
 *                  返回 false（调用方按「不支持」退化）。
 *
 *             ② X11 面（桌面）：DPMS 扩展强制置屏。函数真名是
 *                DPMSForceLevel（**不是** XDPMSForceLevel），位于
 *                libXext（非 libXss）；本机缺 dpms.h 开发头，故按仓库
 *                RandR/XI2 同款纪律内联最小原型。DPMS 是服务端属性，
 *                对任一客户端连接强制置屏即生效，故本文件自开一条惰性
 *                连接（不复用窗口后端的单连接，避免跨文件耦合）。
 *
 *             ③ 读回面（2026-11-02 新增）：**屏幕实况读回 + 状态跃迁
 *                推送**。X11 面读服务端 DPMS 档位（DPMSInfo，三参
 *                签名见段内注释；服务端档位是共享真值——任一客户端改
 *                它都生效，板端实测与 connector 的 dpms 属性、与面板
 *                背光供电三者一致）；DPMS 扩展不可用时回落 sysfs 的
 *                DRM connector dpms 属性（只认 status=connected 的
 *                connector）。本平台**没有**可用的服务端通知源（板端
 *                实测：DPMS 扩展零事件、DPMS 置屏也不派发 RandR 事件）
 *                ⇒ 平台侧自观测：节流轮询（200ms）到跃迁即调
 *                XPlatformPower_reportScreenStateChanged 上行，公共层
 *                改影子态并转成框架事件 ⇒ 订阅者即时收到。
 *
 * @note       平台 API 只允许出现在 Drive（见 Drive/README.md）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformPower.h"

#if XGUI_ON && defined(__linux__) && !defined(__ANDROID__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include "XDateTime.h" /* XDateTime_currentMSecsSinceEpoch（探测冷却节拍/推送节流，框架自有单调时钟，不引 time.h）。 */
#include "XAbstractEventDispatcher.h" /* addPollCallback（平台侧状态跃迁推送的挂载点）。 */

/* ---- fbdev 面编译开关 ---- */
#if defined(XPLATFORM_FBDEV_ON) && XPLATFORM_FBDEV_ON
#include <sys/ioctl.h>
#include <linux/fb.h>
#ifndef XPLATFORM_FBDEV_DEVICE
#define XPLATFORM_FBDEV_DEVICE "/dev/fb0"
#endif
#define XPOWER_FBDEV_ON 1
#else
#define XPOWER_FBDEV_ON 0
#endif

/* DPMS 置屏档位（X11/extensions/dpmsconst.h 的常量，此处硬编码避免对
 * 开发头的依赖；两个档位在非 X11 构建下同样需要，故置于开关之外）。 */
#define XPOWER_DPMS_MODE_ON  0
#define XPOWER_DPMS_MODE_OFF 3
/* DPMS 读回档位：0=On / 1=Standby / 2=Suspend / 3=Off（dpmsconst.h）。 */
#define XPOWER_DPMS_LEVEL_ON 0

/* ---- X11 面编译开关 ---- */
#if defined(XINYUE_C_HAS_X11)
/* Xlib 与 XinYueC 公共层命名冲突（纪律同 XPlatformNativeWindow_posix.c）：
 * 本文件经 XDateTime.h 包含链拉进公共层，XTypes.h 的 XPoint/XEvent
 * 前向 typedef、XMemory.h 的 XFree 宏别名与 Xlib 同名符号撞车
 *（conflicting types / 吞并 extern 声明）。包含 X11 头期间用预处理宏
 * 把 Xlib 侧符号统一改名（X11_Xxx），包含结束立即 #undef 并恢复公共
 * XFree 宏别名；本文件只用到 Display/XOpenDisplay 与 DPMS 内联原型，
 * 不直接引用被改名符号。 */
#undef XFree
#define XImage X11_XImage
#define XPoint X11_XPoint
#define XEvent X11_XEvent
#define XColor X11_XColor
#define XKeyEvent X11_XKeyEvent
#define XExposeEvent X11_XExposeEvent
#include <X11/Xlib.h>
#undef XImage
#undef XPoint
#undef XEvent
#undef XColor
#undef XKeyEvent
#undef XExposeEvent
#define XFree XMemory_free /* 恢复公共内存释放宏别名。 */
#define XPOWER_X11_ON 1
#else
#define XPOWER_X11_ON 0
#endif

/* ---- DPMS 段编译开关（X11 存在但 libXext 缺失时整段不编译）---- */
#if defined(XINYUE_C_HAS_DPMS)
#define XPOWER_DPMS_ON 1
#else
#define XPOWER_DPMS_ON 0
#endif

/* ==================== 通用小工具 ==================== */

/** @brief 读小文本文件（sysfs 节点），成功返回 true 且 buf 以 NUL 结尾。 */
static bool xpowReadText(const char* path, char* buf, size_t len)
{
    int fd;
    ssize_t n;
    if (!path || !buf || len < 2) return false;
    fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    n = read(fd, buf, len - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    /* sysfs 节点常带尾随换行，去掉便于比较。 */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
    return true;
}

/** @brief 写小文本文件（sysfs 节点）。 */
static bool xpowWriteText(const char* path, const char* text)
{
    int fd;
    ssize_t n;
    size_t len;
    if (!path || !text) return false;
    len = strlen(text);
    fd = open(path, O_WRONLY);
    if (fd < 0) return false;
    n = write(fd, text, len);
    close(fd);
    return n == (ssize_t)len;
}

/* ==================== fbdev 面 ==================== */

#if XPOWER_FBDEV_ON

/** @brief fbdev 熄屏手段（探测后选定并缓存）。 */
typedef enum {
    XPowerFbModeNone = 0,     /**< 无可用手段。 */
    XPowerFbModeBacklight,    /**< 背光 PWM sysfs（首选，真省电）。 */
    XPowerFbModeBlankSysfs,   /**< blank sysfs 节点。 */
    XPowerFbModeIoctl         /**< FBIOBLANK ioctl（兜底）。 */
} XPowerFbMode;

static XPowerFbMode g_xpowFbMode = XPowerFbModeNone;
static bool          g_xpowFbProbed = false;   /**< 探测已成功（手段定型）。 */
static bool          g_xpowFbProbeFailed = false; /**< 探测落空（可冷却重试）。 */
static char          g_xpowBrightPath[256]; /**< 背光节点路径。 */
static int           g_xpowBrightValue = -1;/**< 熄屏前的背光亮度（唤醒写回）。 */
static int           g_xpowBrightMax = 1;   /**< 背光量程（max_brightness，探测时缓存）。 */
static bool          g_xpowFbBlanked = false;/**< 影子状态：是否已熄屏。 */

/** @brief 读背光量程 max_brightness（读不到回 1，写 1 至少能点亮）。 */
static int xpowFbBrightMax(void)
{
    char path[288];
    char buf[32];
    char* slash;
    int value;
    if (!g_xpowBrightPath[0]) return 1;
    slash = strrchr(g_xpowBrightPath, '/');
    if (!slash || slash == g_xpowBrightPath) return 1;
    snprintf(path, sizeof(path), "%.*s/max_brightness",
             (int)(slash - g_xpowBrightPath), g_xpowBrightPath);
    if (!xpowReadText(path, buf, sizeof(buf))) return 1;
    value = atoi(buf);
    return value > 0 ? value : 1;
}

/** @brief 在 /sys/class/backlight 下查找可读 brightness 的背光节点。 */
static bool xpowFbFindBacklight(char* outPath, size_t outLen)
{
    DIR* dir;
    struct dirent* ent;
    bool found = false;
    if (!outPath || outLen < 8) return false;
    dir = opendir("/sys/class/backlight");
    if (!dir) return false;
    while ((ent = readdir(dir)) != NULL) {
        char path[256];
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness",
                 ent->d_name);
        if (access(path, R_OK) == 0) {
            snprintf(outPath, outLen, "%s", path);
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}

/** @brief 探测本机可用的 fbdev 熄屏手段（成功后缓存定型；落空可重试）。
 *  @details [开机竞态 2026-10-30] 探测在设备早期可能全部落空（sysfs/
 *           设备节点尚未挂载就绪），旧版把 None 一并永久缓存 = 开机自启
 *           场景熄屏能力在本次进程内永不生效。现在 None 标记为「落空
 *           待重试」，由 xpowFbProbeRetry（冷却节流）在后续动作时重探；
 *           一旦成功即 g_xpowFbProbed=true 定型。 */
static XPowerFbMode xpowFbProbe(void)
{
    if (g_xpowFbProbed) return g_xpowFbMode;
    g_xpowFbProbeFailed = false;
    /* L1 背光 PWM：真正断电降耗，嵌入式首选。 */
    if (xpowFbFindBacklight(g_xpowBrightPath, sizeof(g_xpowBrightPath))) {
        char buf[32];
        if (xpowReadText(g_xpowBrightPath, buf, sizeof(buf)) &&
            atoi(buf) >= 0) {
            int current = atoi(buf);
            g_xpowFbMode = XPowerFbModeBacklight;
            g_xpowBrightMax = xpowFbBrightMax();
            /* [前任遗留熄屏态自愈 2026-10-29] 进程初上电影子态恒为
             * 「亮屏」（还没熄过屏）；若此刻硬件背光已是 0，说明是前任
             * （旧程序熄屏后退出/被替换、面板自带的电源管理）留下的：
             * 影子说亮、硬件却黑，触摸进 noteActivity 判「未熄屏」不
             * 触发唤醒 → 屏幕永远黑着（mcgs 刷机场景实证，用户报障
             * 「刷入新程序后屏幕原先关着就触摸唤不醒」）。首探时把硬件
             * 拉回亮屏基线，shadow 与硬件对齐。 */
            if (current == 0) {
                char text[16];
                snprintf(text, sizeof(text), "%d", g_xpowBrightMax);
                if (xpowWriteText(g_xpowBrightPath, text))
                    current = g_xpowBrightMax;
            }
            g_xpowBrightValue = current;
            g_xpowFbProbed = true;
            return g_xpowFbMode;
        }
    }
    /* L2 blank sysfs 节点。 */
    if (access("/sys/class/graphics/fb0/blank", W_OK) == 0) {
        /* 同 L1 的自愈语义：前任经 blank 节点熄的屏，首探写 0 拉回。 */
        char buf[8];
        if (xpowReadText("/sys/class/graphics/fb0/blank", buf, sizeof(buf)) &&
            atoi(buf) != 0)
            xpowWriteText("/sys/class/graphics/fb0/blank", "0");
        g_xpowFbMode = XPowerFbModeBlankSysfs;
        g_xpowFbProbed = true;
        return g_xpowFbMode;
    }
    /* L3 FBIOBLANK ioctl 兜底（节点存在即可尝试，能力由实际 ioctl 判定）。 */
    if (access(XPLATFORM_FBDEV_DEVICE, R_OK) == 0) {
        g_xpowFbMode = XPowerFbModeIoctl;
        g_xpowFbProbed = true;
        return g_xpowFbMode;
    }
    g_xpowFbMode = XPowerFbModeNone;
    g_xpowFbProbeFailed = true;
    return g_xpowFbMode;
}

/** @brief 首探入口 + 落空后的冷却重试（节流：两次重探间隔 ≥10s）。
 *  @return 当前可用手段（可能仍为 None）。
 *  @details [首次必探 2026-10-30] 上一版在「从未探测过」的初始态
 *           （probeFailed=false && probed=false）直接返回 None——首探
 *           永远不执行，开机自愈整条链失效（真机实证：部署后触摸唤
 *           不醒）。现改为：已定型走缓存；落空过走冷却；**初始态必探**。 */
static XPowerFbMode xpowFbProbeRetry(void)
{
    static int64_t s_lastFailMs = INT64_MIN;
    if (g_xpowFbProbed) return g_xpowFbMode;
    if (g_xpowFbProbeFailed) {
        int64_t now = XDateTime_currentMSecsSinceEpoch();
        if (s_lastFailMs != INT64_MIN && now - s_lastFailMs < 10000)
            return g_xpowFbMode;
        s_lastFailMs = now;
    }
    return xpowFbProbe();
}

/** @brief fbdev 熄屏。 */
static bool xpowFbBlank(void)
{
    switch (xpowFbProbeRetry()) {
    case XPowerFbModeBacklight: {
        char buf[32];
        int value;
        if (!xpowReadText(g_xpowBrightPath, buf, sizeof(buf))) return false;
        value = atoi(buf);
        if (value < 0) return false;
        /* 熄屏前记下亮度，唤醒时原值写回。 */
        g_xpowBrightValue = value;
        if (!xpowWriteText(g_xpowBrightPath, "0")) return false;
        g_xpowFbBlanked = true;
        return true;
    }
    case XPowerFbModeBlankSysfs:
        if (!xpowWriteText("/sys/class/graphics/fb0/blank", "1")) return false;
        g_xpowFbBlanked = true;
        return true;
    case XPowerFbModeIoctl: {
        int fd = open(XPLATFORM_FBDEV_DEVICE, O_RDWR | O_NONBLOCK);
        int rc;
        if (fd < 0) return false;
        /* FB_BLANK_NORMAL：断扫描输出但保持面板供电（FB_BLANK_POWERDOWN
         * 会断面板电源，唤醒不可靠，不采用）。 */
        rc = ioctl(fd, FBIOBLANK, FB_BLANK_NORMAL);
        close(fd);
        if (rc != 0) return false;
        g_xpowFbBlanked = true;
        return true;
    }
    default:
        return false;
    }
}

/** @brief fbdev 唤醒。 */
static bool xpowFbUnblank(void)
{
    switch (xpowFbProbeRetry()) {
    case XPowerFbModeBacklight: {
        char buf[32];
        int value = g_xpowBrightValue;
        if (value <= 0) {
            /* 熄屏前亮度为 0（原就熄着）或未记录：重读当前值，仍为 0
             * 则写量程上限（写 0 等于没唤醒，屏会继续黑着；量程未知时
             * 至少写 1）。 */
            if (!xpowReadText(g_xpowBrightPath, buf, sizeof(buf))) return false;
            value = atoi(buf);
            if (value <= 0) value = g_xpowBrightMax > 0 ? g_xpowBrightMax : 1;
        }
        {
            char text[16];
            snprintf(text, sizeof(text), "%d", value);
            if (!xpowWriteText(g_xpowBrightPath, text)) return false;
        }
        g_xpowFbBlanked = false;
        return true;
    }
    case XPowerFbModeBlankSysfs:
        if (!xpowWriteText("/sys/class/graphics/fb0/blank", "0")) return false;
        g_xpowFbBlanked = false;
        return true;
    case XPowerFbModeIoctl: {
        int fd = open(XPLATFORM_FBDEV_DEVICE, O_RDWR | O_NONBLOCK);
        int rc;
        if (fd < 0) return false;
        rc = ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
        close(fd);
        if (rc != 0) return false;
        g_xpowFbBlanked = false;
        return true;
    }
    default:
        return false;
    }
}

/** @brief fbdev 是否可用（探测到任一熄屏手段）。 */
static bool xpowFbAvailable(void)
{
    return xpowFbProbeRetry() != XPowerFbModeNone;
}

/** @brief 读屏幕硬件实况（影子/硬件对账数据源；读不回恒 Unknown）。
 *  @details 读回口径按当前生效手段：背光读 brightness（>0=亮）；
 *           blank 节点读 0=亮 1=熄；FBIOBLANK 兜底通道无 sysfs 读回
 *           （内核不回显 blank 态），报 Unknown——对账层对 Unknown
 *           不动作，误报风险为零。 */
static XPlatformPowerScreenActual xpowFbQueryActual(void)
{
    char buf[32];
    switch (xpowFbProbeRetry()) {
    case XPowerFbModeBacklight:
        if (!xpowReadText(g_xpowBrightPath, buf, sizeof(buf)))
            return XPlatformPowerScreenActual_Unknown;
        return atoi(buf) > 0 ? XPlatformPowerScreenActual_On
                             : XPlatformPowerScreenActual_Off;
    case XPowerFbModeBlankSysfs:
        if (!xpowReadText("/sys/class/graphics/fb0/blank", buf, sizeof(buf)))
            return XPlatformPowerScreenActual_Unknown;
        return atoi(buf) == 0 ? XPlatformPowerScreenActual_On
                              : XPlatformPowerScreenActual_Off;
    case XPowerFbModeIoctl:
    default:
        return XPlatformPowerScreenActual_Unknown;
    }
}

/** @brief 系统挂起是否可用：/sys/power/state 存在且含 mem 或 freeze。 */
static bool xpowFbSuspendAvailable(void)
{
    char buf[128];
    if (!xpowReadText("/sys/power/state", buf, sizeof(buf))) return false;
    return strstr(buf, "mem") != NULL || strstr(buf, "freeze") != NULL;
}

/** @brief 系统挂起：写 /sys/power/state（写者自身被冻结，恢复后继续）。 */
static bool xpowFbSuspend(void)
{
    char buf[128];
    const char* mode = NULL;
    if (!xpowReadText("/sys/power/state", buf, sizeof(buf))) return false;
    /* 优先真挂起（内存保持）；无 mem 时退到 freeze（仅冻结 CPU）。 */
    if (strstr(buf, "mem")) mode = "mem";
    else if (strstr(buf, "freeze")) mode = "freeze";
    if (!mode) return false;
    /* 写成功返回时系统已恢复（本进程在写点被冻结）。 */
    return xpowWriteText("/sys/power/state", mode);
}

#else /* !XPOWER_FBDEV_ON */

static bool xpowFbAvailable(void)     { return false; }
static bool xpowFbBlank(void)         { return false; }
static bool xpowFbUnblank(void)       { return false; }
static XPlatformPowerScreenActual xpowFbQueryActual(void)
                                      { return XPlatformPowerScreenActual_Unknown; }
static bool xpowFbSuspendAvailable(void) { return false; }
static bool xpowFbSuspend(void)       { return false; }

#endif /* XPOWER_FBDEV_ON */

/* ==================== 背光通道（与 fbdev 解耦，2026-11-10） ====================
 *
 * [为什么必须独立于 XPOWER_FBDEV_ON] 本板（RK3568 + MIPI DSI 面板）实测：
 *   · `xset dpms force off` 只把 **HDMI** 置 Off，**DSI 面板照旧亮着**
 *     （/sys/class/drm/card0-DSI-1/dpms 恒 On）；
 *   · 真正能熄面板的是背光节点 /sys/class/backlight/fe070000.dsi.0/brightness。
 * 而 X11 构建里 XPOWER_FBDEV_ON=0 ⇒ 整段背光代码被编译掉、Off 只剩 DPMS
 * ⇒ **点"立即熄屏"永远不熄**（用户报障「以前能熄、现在无效」；以前能熄是
 * fbdev 变体）。故把背光读写提成不受该宏影响的公共能力，Off 优先用它。
 */
#define XPOW_BL_MAX_NODES 4

static char g_xpowBlPath[256];      /**< 选定的背光节点（空=未探测）。 */
static int  g_xpowBlMax = 255;      /**< 该节点量程。 */
static int  g_xpowBlSaved = -1;     /**< 熄屏前亮度（唤醒写回；-1=未熄）。 */
static bool g_xpowBlProbed = false; /**< 已探测（无论成败，避免每拍 opendir）。 */

/** @brief 挑一个"能写"的背光节点；优先名字里带 dsi/panel 的（本板真屏）。 */
static bool xpowBlProbe(void)
{
    DIR* dir;
    struct dirent* ent;
    char best[256] = "";

    if (g_xpowBlProbed) return g_xpowBlPath[0] != '\0';
    g_xpowBlProbed = true;
    dir = opendir("/sys/class/backlight");
    if (!dir) return false;
    while ((ent = readdir(dir)) != NULL) {
        char path[320];
        int score = 0;
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness",
                 ent->d_name);
        if (access(path, W_OK) != 0) continue;
        /* [选节点] 本板有三个：backlight / backlight1 / fe070000.dsi.0。
         * dsi/panel 命中的是面板自身背光（实测只有它能熄屏），优先选它；
         * 其余按目录名字典序取第一个可写者。 */
        if (strstr(ent->d_name, "dsi") || strstr(ent->d_name, "panel"))
            score = 100;
        if (score > 0 || best[0] == '\0') {
            if (score > 0) {
                snprintf(best, sizeof(best), "%s", path);
                break;      /* dsi 命中即定案。 */
            }
            snprintf(best, sizeof(best), "%s", path);
        }
    }
    closedir(dir);
    if (!best[0]) return false;
    snprintf(g_xpowBlPath, sizeof(g_xpowBlPath), "%s", best);
    {
        char mb[288];
        char buf[32];
        char* slash = strrchr(g_xpowBlPath, '/');
        if (slash) {
            snprintf(mb, sizeof(mb), "%.*s/max_brightness",
                     (int)(slash - g_xpowBlPath), g_xpowBlPath);
            if (xpowReadText(mb, buf, sizeof(buf)) && atoi(buf) > 0)
                g_xpowBlMax = atoi(buf);
        }
    }
    return true;
}

static bool xpowBlAvailable(void)
{
    return xpowBlProbe() && g_xpowBlPath[0] != '\0';
}

/** @brief 写背光亮度（0=最暗=熄屏）。 */
static bool xpowBlWrite(int value)
{
    char text[16];
    if (!xpowBlAvailable()) return false;
    snprintf(text, sizeof(text), "%d", value);
    return xpowWriteText(g_xpowBlPath, text);
}

/** @brief 熄屏：记下当前亮度再写 0。 */
static bool xpowBlOff(void)
{
    char buf[32];
    if (!xpowBlAvailable()) return false;
    if (g_xpowBlSaved < 0) {
        if (xpowReadText(g_xpowBlPath, buf, sizeof(buf)))
            g_xpowBlSaved = atoi(buf);
        else
            g_xpowBlSaved = g_xpowBlMax;
    }
    return xpowBlWrite(0);
}

/** @brief 点亮：写回熄屏前亮度（没记过就写满量程）。 */
static bool xpowBlOn(void)
{
    int v;
    if (!xpowBlAvailable()) return false;
    v = (g_xpowBlSaved > 0) ? g_xpowBlSaved : g_xpowBlMax;
    if (!xpowBlWrite(v)) return false;
    g_xpowBlSaved = -1;
    return true;
}

/** @brief 背光实况：0 ⇒ 熄。
 *  @return true=读到；false=无背光节点（该通道不参与判定）。 */
static bool xpowBlQueryActual(XPlatformPowerScreenActual* out)
{
    char buf[32];
    if (!out) return false;
    if (!xpowBlAvailable()) return false;
    if (!xpowReadText(g_xpowBlPath, buf, sizeof(buf))) return false;
    *out = (atoi(buf) == 0) ? XPlatformPowerScreenActual_Off
                            : XPlatformPowerScreenActual_On;
    return true;
}


/* ==================== X11 面 ==================== */

#if XPOWER_X11_ON

static Display* g_xpowDisplay = NULL; /**< 惰性自开的 X 连接。 */

#if XPOWER_DPMS_ON
/* DPMS 最小原型（dpms.h 开发头缺失时的内联 ABI 声明，纪律同
 * XPlatformNativeWindow_posix.c 的 RandR/XI2 段）。
 * ⚠【真机教训 2026-11-02】DPMSInfo 的**真实签名是三参**：
 *   Status DPMSInfo(Display *dpy, CARD16 *power_level, BOOL *enabled)
 * （见 dpms.h / dpmsproto.h）。按两参调用会因栈上少传一个出参而
 * 段错误（板端实测崩在 libXext 内），别再"简化"成两参。
 * 同理 DPMSForceLevel 的真实返回是 Status（不是 void）——原声明写成
 * void 与真实 ABI 不符（调用方忽略返回值，行为无碍，但已按真实签名
 * 校正，避免再被当作 void 函数误用）。 */
extern int  DPMSQueryExtension(Display* display, int* eventBase, int* errorBase);
extern int  DPMSInfo(Display* display, unsigned short* powerLevel, int* enabled);
extern int  DPMSForceLevel(Display* display, unsigned short powerLevel);
/* [2026-11-10 熄屏失效根因] 必须能"启用" DPMS：Xorg 在 !DPMSEnabled 时
 * 对 ForceLevel 直接回 BadMatch。xset dpms force off 就是先 Enable 再
 * ForceLevel；本库此前全历史从未调用过 Enable。真实签名同为 Status。 */
extern int  DPMSEnable(Display* display);

static bool     g_xpowDpmsProbed = false;
static bool     g_xpowDpmsUsable = false;

/* 前向声明：xpowDpmsForce 要用读回判定成败，而读回实现定义在其后。 */
static bool xpowDpmsQueryLevel(int* outLevel);

/** @brief 惰性取得 X 连接（失败缓存一次，重试由调用方决定）。 */
static Display* xpowX11Display(void)
{
    if (!g_xpowDisplay) {
        g_xpowDisplay = XOpenDisplay(NULL);
        if (!g_xpowDisplay) return NULL;
    }
    return g_xpowDisplay;
}

/** @brief 探测服务端是否支持 DPMS 扩展。 */
static bool xpowDpmsAvailable(void)
{
    Display* dpy;
    int eventBase = 0;
    int errorBase = 0;
    if (g_xpowDpmsProbed) return g_xpowDpmsUsable;
    dpy = xpowX11Display();
    /* [失败不落缓存 2026-11-10 熄屏失效根因之一] 原实现在**拿到 dpy 之前**
     * 就把 g_xpowDpmsProbed 置真：进程启动早期若 XOpenDisplay 偶发失败
     * （X 未就绪/授权未加载），本次返回 false 并把 probed 永久钉住 ⇒ 之后
     * 能力位恒 0、「立即熄屏」按钮恒灰，公共层 10s 重探也被这层缓存抵消
     * （现场"以前能熄、现在点了没反应"）。改为**只在成功时**落缓存。 */
    if (!dpy) return false;
    if (!DPMSQueryExtension(dpy, &eventBase, &errorBase)) return false;
    /* errorBase 非零表示服务端报告了扩展错误（不可用）。 */
    if (errorBase != 0) return false;
    g_xpowDpmsProbed = true;
    g_xpowDpmsUsable = true;
    return true;
}

/** @brief DPMS 强制置屏（level: 0=On, 3=Off）。 */
static bool xpowDpmsForce(int level)
{
    Display* dpy = xpowX11Display();
    int before = -1;
    int after = -1;
    if (!dpy) return false;
    if (!xpowDpmsAvailable()) return false;
    /* [必须先 DPMSEnable 2026-11-10 熄屏失效根因] Xorg 的
     * ProcDPMSForceLevel 在 `!DPMSEnabled` 时直接回 BadMatch；而
     * DPMSForceLevel 是**异步**请求（libXext 只 GetReq+Unlock，恒返回 1，
     * 服务端错误随后异步到达并被本进程的 X error handler 吞掉）⇒ 旧实现
     * 无论成败都 return true：界面报"已立即熄屏"，面板却亮着。
     * `xset dpms force off` 能熄，正是因为它先 DPMSEnable 再 ForceLevel。 */
    (void)DPMSEnable(dpy);
    if (xpowDpmsQueryLevel(&before) && before == level) return true; /* 幂等 */
    DPMSForceLevel(dpy, (unsigned short)level);
    XSync(dpy, False);   /* 清空输出队列，让服务端真正处理这一步。 */
    /* [以读回为准 2026-11-10] 不能用调用返回值判成败（恒 1）；DPMSInfo 读到
     * 的档位才是真相。读不到时退让为 true（老服务端可能无 DPMSInfo）。 */
    if (xpowDpmsQueryLevel(&after) && after != level) {
        fprintf(stderr,
                "[xpow] DPMS force level=%d 未生效（读回=%d）——"
                "确认 X 服务端允许 DPMS（xset +dpms）\n",
                level, after);
        return false;
    }
    return true;
}

/** @brief 读服务端 DPMS 当前档位（读回屏幕实况的主通道）。
 *  @param outLevel 出参：0=On / 1=Standby / 2=Suspend / 3=Off。
 *  @return true=读到；false=扩展不可用或读失败。
 *  @details 服务端 DPMS 档位是**共享真值**：任一客户端（应用自己、
 *           x11vnc、`xset dpms force on/off`、面板电源管理代理）改它，
 *           服务端就改它 ⇒ 读它即知屏幕此刻亮/灭。板端实测：force off
 *           ⇒ level=3 且 /sys/class/drm/card0-DSI-1/dpms=Off（面板真灭）；
 *           force on ⇒ level=0 且 dpms=On。 */
static bool xpowDpmsQueryLevel(int* outLevel)
{
    Display* dpy = xpowX11Display();
    unsigned short level = 0;
    int enabled = 0;
    if (!dpy) return false;
    if (!xpowDpmsAvailable()) return false;
    /* 三参签名（见上方原型注释）：少传 enabled 出参会段错误。 */
    if (!DPMSInfo(dpy, &level, &enabled)) return false;
    if (outLevel) *outLevel = (int)level;
    return true;
}

/** @brief X11 面屏幕实况读回（DPMS 档位）。
 *  @return On / Off / Unknown（读不到）。
 *  @details 非 On 档位（Standby/Suspend/Off）一律判为「熄」——契约只有
 *           On/Off 两档（刻意不设降亮中间档，见 XPlatformPower.h）。 */
static XPlatformPowerScreenActual xpowDpmsQueryActual(void)
{
    int level = 0;
    if (!xpowDpmsQueryLevel(&level)) return XPlatformPowerScreenActual_Unknown;
    return (level == XPOWER_DPMS_LEVEL_ON) ? XPlatformPowerScreenActual_On
                                           : XPlatformPowerScreenActual_Off;
}
#else /* !XPOWER_DPMS_ON */

static bool xpowDpmsAvailable(void) { return false; }
static bool xpowDpmsForce(int level) { (void)level; return false; }
static bool xpowDpmsQueryLevel(int* outLevel) { (void)outLevel; return false; }
static XPlatformPowerScreenActual xpowDpmsQueryActual(void)
                                      { return XPlatformPowerScreenActual_Unknown; }

#endif /* XPOWER_DPMS_ON */

/* ==================== X11 面：sysfs DRM connector 兜底读回 ==================== */

/** @brief 经 /sys/class/drm 的 connector dpms 属性读屏幕实况（兜底通道）。
 *  @return On / Off / Unknown（读不到）。
 *  @details DPMS 扩展不可用时（老服务端/无 DPMS 的 X server）走这条：
 *           X server 的 DPMS 置屏会落到 connector 的 dpms 属性（板端
 *           实测 force off ⇒ card0-DSI-1/dpms=Off），面板供电与背光
 *           bl_power 同步翻转（4=暗）。
 *  @note  **只认 status=connected 的 connector**：未接的 HDMI 口常年
 *          报 On/Off 与实际亮灭无关，若把它算进来会把「屏幕亮着」
 *          误判成「屏幕黑着」（进而误触发点亮自愈）。板端三个 connector
 *          实测：DSI-1=connected、HDMI-A-1=disconnected、
 *          Writeback-1=unknown，只有 connected 的那个会随置屏翻转。 */
static XPlatformPowerScreenActual xpowDrmQueryActual(void)
{
    DIR* dir;
    struct dirent* ent;
    XPlatformPowerScreenActual result = XPlatformPowerScreenActual_Unknown;
    dir = opendir("/sys/class/drm");
    if (!dir) return XPlatformPowerScreenActual_Unknown;
    while ((ent = readdir(dir)) != NULL) {
        char statusPath[288];
        char dpmsPath[288];
        char buf[32];
        if (ent->d_name[0] == '.') continue;
        snprintf(statusPath, sizeof(statusPath), "/sys/class/drm/%s/status",
                 ent->d_name);
        snprintf(dpmsPath, sizeof(dpmsPath), "/sys/class/drm/%s/dpms",
                 ent->d_name);
        /* 只有 connector 目录才有 status 节点（cardN/renderD* 没有）。 */
        if (!xpowReadText(statusPath, buf, sizeof(buf))) continue;
        if (strcmp(buf, "connected") != 0) continue;
        if (!xpowReadText(dpmsPath, buf, sizeof(buf))) continue;
        if (strcmp(buf, "Off") == 0) {
            result = XPlatformPowerScreenActual_Off;
            break;
        }
        if (strcmp(buf, "On") == 0)
            result = XPlatformPowerScreenActual_On;
    }
    closedir(dir);
    return result;
}

/** @brief X11 面屏幕实况读回总入口（DPMS 档位优先，sysfs DRM 兜底）。 */
static XPlatformPowerScreenActual xpowX11QueryActual(void)
{
    XPlatformPowerScreenActual actual = xpowDpmsQueryActual();
    if (actual != XPlatformPowerScreenActual_Unknown) return actual;
    return xpowDrmQueryActual();
}

/* ==================== 平台侧推送（状态跃迁 → 框架事件） ==================== */

/* 轮询节流（毫秒）：板端实测服务端 DPMS 档位随置屏**即时**翻转，200ms
 * 轮询足以在肉眼不可感知的时延内捕捉到外部唤醒；5Hz 的开销（一次 X
 * 往返）相对板端 CPU 预算可忽略（对比逐帧轮询会徒增事件泵负担）。 */
#define XPOWER_PUSH_INTERVAL_MS 200

static XHandle g_xpowPushHandle = NULL;              /**< 轮询回调句柄。 */
static XPlatformPowerScreenActual g_xpowLastActual =
    XPlatformPowerScreenActual_Unknown;              /**< 上次观测到的实况。 */
static int64_t g_xpowLastPushMs = 0;                 /**< 上次轮询时刻。 */

/** @brief 平台侧轮询回调：观测屏幕实况，跃迁即向上报（平台推送）。
 *  @details 本平台**没有**可用的服务端通知源（板端实测：DPMS 扩展零
 *           事件（DPMSNumberEvents=0），且 DPMS 置屏不派发 RandR 事件）
 *           ⇒ 平台只能自观测。观测到跃迁即调 XPlatformPower_report
 *           ScreenStateChanged（平台→框架的唯一上行口），由公共层改影子
 *           态并转成框架事件 ⇒ 订阅者即时收到。
 *  @note   幂等：实况未变则不上报（平台轮询会以固定节拍重复读到同一
 *           实况，不得反复打扰订阅者）。 */
static bool xpowPushPoll(void* userData)
{
    XPlatformPowerScreenActual actual;
    int64_t now;
    (void)userData;
    now = XDateTime_currentMSecsSinceEpoch();
    if (now - g_xpowLastPushMs < XPOWER_PUSH_INTERVAL_MS) return true;
    g_xpowLastPushMs = now;
    actual = xpowX11QueryActual();
    if (actual == XPlatformPowerScreenActual_Unknown) return true;
    if (actual == g_xpowLastActual) return true;
    g_xpowLastActual = actual;
    XPlatformPower_reportScreenStateChanged(actual);
    return true;
}

/** @brief 惰性注册平台侧轮询推送（事件分发器就绪后一次）。
 *  @note  两处前置条件（缺一则推送不发生，已板端实证）：
 *         ① 轮询回调挂在事件分发器的 processEvents 上 ⇒ 应用须泵事件
 *            （应用侧由主事件循环天然满足）；
 *         ② 分发器的取得**经应用单例**（XAbstractEventDispatcher_instance
 *            → XCoreApplication_eventDispatcher → g_app）⇒ 应用实例须
 *            先于本注册存在，否则本函数静默跳过（下轮再试）。
 *         即便始终注册不上，公共层的 1Hz 对账仍会读到真值并走同一上行
 *         口（功能闭环不依赖本推送，推送只决定时效）。 */
static void xpowEnsurePushRegistered(void)
{
    if (g_xpowPushHandle) return;
    if (XAbstractEventDispatcher_instance(NULL) == NULL) return;
    g_xpowPushHandle =
        XAbstractEventDispatcher_addPollCallback(xpowPushPoll, NULL);
}

#else /* !XPOWER_X11_ON */

static bool xpowDpmsAvailable(void) { return false; }
static bool xpowDpmsForce(int level) { (void)level; return false; }
static bool xpowDpmsQueryLevel(int* outLevel) { (void)outLevel; return false; }
static XPlatformPowerScreenActual xpowDpmsQueryActual(void)
                                      { return XPlatformPowerScreenActual_Unknown; }
static XPlatformPowerScreenActual xpowX11QueryActual(void)
                                      { return XPlatformPowerScreenActual_Unknown; }
static void xpowEnsurePushRegistered(void) { }

#endif /* XPOWER_X11_ON */

/* ==================== 契约实现 ==================== */

unsigned XPlatformPowerDriver_capabilities(void)
{
    unsigned caps = 0u;
    /* 「主动关屏 + 唤醒」在本契约覆盖的全部平台总是同时具备，故合成
     * 一个能力位（XPlatformPowerCap_ScreenControl）。 */
    if (xpowFbAvailable() || xpowDpmsAvailable())
        caps |= (unsigned)XPlatformPowerCap_ScreenControl;
    if (xpowFbSuspendAvailable())
        caps |= (unsigned)XPlatformPowerCap_SystemSuspend;
    /* 平台侧推送（状态跃迁 → 框架事件）：能力探测是应用起来后最早会
     * 走到的驱动入口，借此惰性挂上轮询推送（分发器就绪后一次）。 */
    xpowEnsurePushRegistered();
    return caps;
}

XPlatformPowerScreenActual XPlatformPowerDriver_queryScreenState(void)
{
    XPlatformPowerScreenActual actual;
    /* 平台侧推送的惰性注册也挂在这里：公共层的 1Hz 对账**每拍**都问一次
     * 实况 ⇒ 分发器就绪后最迟 1s 内注册上（只挂能力探测会漏：首次探测
     * 可能早于分发器就绪，而能力位非 0 后不再重探）。 */
    xpowEnsurePushRegistered();
    /* 背光通道（与 fbdev 解耦）：本板 DSI 面板只有它能熄，DPMS 管不到。
     * 放在 DPMS 之前——背光=0 就是真熄，优先于服务端 DPMS 档位。 */
    {
        XPlatformPowerScreenActual bl;
        if (xpowBlQueryActual(&bl)) return bl;
    }
    /* fbdev 背光/blank 节点可读回（真值，嵌入式构建的主通道）。 */
    actual = xpowFbQueryActual();
    if (actual != XPlatformPowerScreenActual_Unknown) return actual;
    /* X11 面：服务端 DPMS 档位（共享真值，任一客户端改它都生效），
     * 扩展不可用时回落 sysfs connector 的 dpms 属性。读不到则 Unknown
     * ——对账层对 Unknown 不动作。 */
    actual = xpowX11QueryActual();
    if (actual != XPlatformPowerScreenActual_Unknown) return actual;
    return XPlatformPowerScreenActual_Unknown;
}

bool XPlatformPowerDriver_setScreenState(XPlatformPowerScreenState state)
{
    if (state == XPlatformPowerScreenState_Off) {
        /* [熄屏优先级 2026-11-10] 背光 → fbdev → DPMS。
         * 本板实测：DPMS 关不掉 DSI 面板（xset dpms force off 只关 HDMI，
         * card0-DSI-1/dpms 恒 On），只有背光写 0 才真熄 ⇒ 背光必须最前，
         * 且不能只在 fbdev 构建里可用。 */
        bool blOk = xpowBlOff();
        bool any = blOk;
        if (xpowFbAvailable()) any = xpowFbBlank() || any;
        if (xpowDpmsAvailable()) any = xpowDpmsForce(XPOWER_DPMS_MODE_OFF) || any;
        return any;
    }
    /* 点亮：两条通道都尝试唤醒（可能只有一条被用过），任一成功即算。 */
    {
        bool ok = false;
        if (xpowBlOn()) ok = true;
        if (xpowFbAvailable() && xpowFbUnblank()) ok = true;
        if (xpowDpmsAvailable() && xpowDpmsForce(XPOWER_DPMS_MODE_ON)) ok = true;
        return ok;
    }
}

bool XPlatformPowerDriver_setSystemState(XPlatformPowerSystemState state)
{
    if (state == XPlatformPowerSystemState_Suspend)
        return xpowFbSuspend();
    /* Active：系统已处于运行态，本契约不主动唤醒系统（唤醒由平台电源
     * 策略或用户操作触发），恒成功。 */
    return true;
}

bool XPlatformPowerDriver_setScreenTimeout(int64_t millis)
{
    (void)millis;
    /* fbdev/X11 侧不支持「委托平台计时」：熄屏是应用自己关屏（背光/
     * DPMS），时长阈值由公共层回落到本地空闲计时。 */
    return false;
}

bool XPlatformPowerDriver_requestScreenTimeoutPermission(void)
{
    /* fbdev/X11 自行关屏计时，无需任何系统授权。 */
    return false;
}

#endif /* XGUI_ON && defined(__linux__) && !defined(__ANDROID__) */
