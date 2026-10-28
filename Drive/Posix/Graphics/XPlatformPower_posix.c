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

/* ---- X11 面编译开关 ---- */
#if defined(XINYUE_C_HAS_X11)
#include <X11/Xlib.h>
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
static bool          g_xpowFbProbed = false;
static char          g_xpowBrightPath[256]; /**< 背光节点路径。 */
static int           g_xpowBrightValue = -1;/**< 熄屏前的背光亮度（唤醒写回）。 */
static bool          g_xpowFbBlanked = false;/**< 影子状态：是否已熄屏。 */

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

/** @brief 探测本机可用的 fbdev 熄屏手段（结果缓存）。 */
static XPowerFbMode xpowFbProbe(void)
{
    if (g_xpowFbProbed) return g_xpowFbMode;
    g_xpowFbProbed = true;
    /* L1 背光 PWM：真正断电降耗，嵌入式首选。 */
    if (xpowFbFindBacklight(g_xpowBrightPath, sizeof(g_xpowBrightPath))) {
        char buf[32];
        if (xpowReadText(g_xpowBrightPath, buf, sizeof(buf)) &&
            atoi(buf) >= 0) {
            g_xpowFbMode = XPowerFbModeBacklight;
            return g_xpowFbMode;
        }
    }
    /* L2 blank sysfs 节点。 */
    if (access("/sys/class/graphics/fb0/blank", W_OK) == 0) {
        g_xpowFbMode = XPowerFbModeBlankSysfs;
        return g_xpowFbMode;
    }
    /* L3 FBIOBLANK ioctl 兜底（节点存在即可尝试，能力由实际 ioctl 判定）。 */
    if (access(XPLATFORM_FBDEV_DEVICE, R_OK) == 0) {
        g_xpowFbMode = XPowerFbModeIoctl;
        return g_xpowFbMode;
    }
    g_xpowFbMode = XPowerFbModeNone;
    return g_xpowFbMode;
}

/** @brief fbdev 熄屏。 */
static bool xpowFbBlank(void)
{
    switch (xpowFbProbe()) {
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
    switch (xpowFbProbe()) {
    case XPowerFbModeBacklight: {
        char buf[32];
        int value = g_xpowBrightValue;
        if (value <= 0) {
            /* 熄屏前亮度为 0（原就熄着）或未记录：重读当前值，仍为 0
             * 则写 1（写 0 等于没唤醒，屏会继续黑着）。 */
            if (!xpowReadText(g_xpowBrightPath, buf, sizeof(buf))) return false;
            value = atoi(buf);
            if (value <= 0) value = 1;
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
    return xpowFbProbe() != XPowerFbModeNone;
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
static bool xpowFbSuspendAvailable(void) { return false; }
static bool xpowFbSuspend(void)       { return false; }

#endif /* XPOWER_FBDEV_ON */

/* ==================== X11 面 ==================== */

#if XPOWER_X11_ON

static Display* g_xpowDisplay = NULL; /**< 惰性自开的 X 连接。 */

#if XPOWER_DPMS_ON
/* DPMS 最小原型（dpms.h 开发头缺失时的内联 ABI 声明，纪律同
 * XPlatformNativeWindow_posix.c 的 RandR/XI2 段）。 */
extern int  DPMSQueryExtension(Display* display, int* eventBase, int* errorBase);
extern void DPMSForceLevel(Display* display, int powerLevel);

static bool     g_xpowDpmsProbed = false;
static bool     g_xpowDpmsUsable = false;

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
    g_xpowDpmsProbed = true;
    dpy = xpowX11Display();
    if (!dpy) return false;
    if (!DPMSQueryExtension(dpy, &eventBase, &errorBase)) return false;
    /* errorBase 非零表示服务端报告了扩展错误（不可用）。 */
    if (errorBase != 0) return false;
    g_xpowDpmsUsable = true;
    return true;
}

/** @brief DPMS 强制置屏（level: 0=On, 3=Off）。 */
static bool xpowDpmsForce(int level)
{
    Display* dpy = xpowX11Display();
    if (!dpy) return false;
    if (!xpowDpmsAvailable()) return false;
    DPMSForceLevel(dpy, level);
    XFlush(dpy);
    return true;
}
#else /* !XPOWER_DPMS_ON */

static bool xpowDpmsAvailable(void) { return false; }
static bool xpowDpmsForce(int level) { (void)level; return false; }

#endif /* XPOWER_DPMS_ON */

#else /* !XPOWER_X11_ON */

static bool xpowDpmsAvailable(void) { return false; }
static bool xpowDpmsForce(int level) { (void)level; return false; }

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
    return caps;
}

bool XPlatformPowerDriver_setScreenState(XPlatformPowerScreenState state)
{
    if (state == XPlatformPowerScreenState_Off) {
        /* 熄屏：fbdev 优先（嵌入式场景）；桌面走 DPMS。 */
        if (xpowFbAvailable()) return xpowFbBlank();
        if (xpowDpmsAvailable()) return xpowDpmsForce(XPOWER_DPMS_MODE_OFF);
        return false;
    }
    /* 点亮：两条通道都尝试唤醒（可能只有一条被用过），任一成功即算。 */
    {
        bool ok = false;
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
