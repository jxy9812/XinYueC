/* mcgs_touch_inject.c —— 昆仑通态屏触摸注入器（armel 交叉编译）
 *
 * 用途：在 mcgs 屏（Allwinner A33，fbdev + evdev 输入）上注入触摸序列，
 *       用于真机验证框架手势/滚动/点击行为。
 *
 * 背景与坑（2026-10-30 复盘）：
 *   - 屏上原注入器 xinj2/rawxy 随 /tmp tmpfs 重启丢失，源码不在仓库，
 *     故本文件重建最小可用注入器。
 *   - 历史教训：tapraw.sh 全 EINVAL = 假注入（槽位/协议用错）；pointercal
 *     的逆变换必须独立处理 scale 且用 long long 防 32 位溢出。
 *   - 本实现走 **ABS 绝对坐标槽**（MT_SLOT / ABS_MT_POSITION_X/Y），
 *     直接给屏幕绝对坐标，不做 raw→screen 逆变换（板载 ABS 槽本身就是
 *     绝对坐标，pointercal 只在 raw 相对坐标设备上才需要）。
 *
 * 用法：
 *   mcgs_touch_inject tap <x> <y>
 *   mcgs_touch_inject swipe <x1> <y1> <x2> <y2> [steps] [delay_ms]
 *   mcgs_touch_inject multi <id> <x> <y> <id> <x> <y> ...   (多指按下后保持)
 *   mcgs_touch_inject list            (列出 /dev/input 设备与 ABS 槽)
 *
 * 事件序列（与 Linux 多点触摸协议一致）：
 *   ABS_MT_SLOT 选指 → ABS_MT_TRACKING_ID 认领 → ABS_MT_POSITION_X/Y 定位
 *   → SYN_REPORT → （移动时重复）→ ABS_MT_TRACKING_ID -1 释放 → SYN_REPORT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define ABS_MT_SLOT        0x2f
#define ABS_MT_POSITION_X  0x35
#define ABS_MT_POSITION_Y  0x36
#define ABS_MT_TRACKING_ID 0x39
#define ABS_MT_PRESSURE    0x3a
#define BTN_TOUCH          0x14a

static int g_fd = -1;

/* 协议模式：0=未探测 1=MT 多点槽（ABS_MT_SLOT/TRACKING_ID/POSITION） 2=ST 单点（ABS_X/ABS_Y + BTN_TOUCH） */
static int g_proto = 0;

/* ST 协议的坐标范围（ABS_X/ABS_Y 的 absinfo），用于把屏幕像素坐标换算成设备坐标。 */
static int g_st_min_x = 0, g_st_max_x = 0;
static int g_st_min_y = 0, g_st_max_y = 0;
static int g_screen_w = 1024, g_screen_h = 600;

/** @brief 探测输入设备协议模式与 ST 坐标范围。
 *  @details 与框架 XPlatformFbInput_posix.c 的协议分流同口径：
 *           设备报过 ABS_MT_SLOT/ABS_MT_POSITION_X 则走 MT 槽模型，
 *           否则退纯 ST 单槽（ABS_X/ABS_Y + BTN_TOUCH 按压沿）。
 */
static void probe_protocol(int fd)
{
    unsigned long bits[8] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) return;

    int has_mt_slot  = (bits[(ABS_MT_SLOT >> 5)] >> (ABS_MT_SLOT & 31)) & 1;
    int has_mt_x     = (bits[(ABS_MT_POSITION_X >> 5)] >> (ABS_MT_POSITION_X & 31)) & 1;
    int has_st_x     = (bits[(ABS_X >> 5)] >> (ABS_X & 31)) & 1;
    int has_st_y     = (bits[(ABS_Y >> 5)] >> (ABS_Y & 31)) & 1;

    if (has_mt_slot && has_mt_x) {
        g_proto = 1;
        fprintf(stderr, "[inject] protocol=MT (multi-touch slots)\n");
        return;
    }
    if (has_st_x && has_st_y) {
        g_proto = 2;
        struct input_absinfo ai;
        if (ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0) {
            g_st_min_x = ai.minimum; g_st_max_x = ai.maximum;
        }
        if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0) {
            g_st_min_y = ai.minimum; g_st_max_y = ai.maximum;
        }
        fprintf(stderr, "[inject] protocol=ST (ABS_X/ABS_Y + BTN_TOUCH) range x=[%d,%d] y=[%d,%d]\n",
                g_st_min_x, g_st_max_x, g_st_min_y, g_st_max_y);
        return;
    }
    g_proto = 0;
    fprintf(stderr, "[inject] protocol=UNKNOWN (no ABS touch axes)\n");
}

/** @brief 屏幕像素坐标 → ST 设备坐标（线性映射到 absinfo 范围）。
 *  @details 板载 TSC2007 的 ABS 范围常与面板分辨率不同（如 0..4095 对应
 *           1024x600 面板），必须线性换算，否则注入点与视觉位置不符。
 */
static void st_map(int x, int y, int* dx, int* dy)
{
    *dx = g_st_min_x + (x * (g_st_max_x - g_st_min_x)) / (g_screen_w > 1 ? g_screen_w - 1 : 1);
    *dy = g_st_min_y + (y * (g_st_max_y - g_st_min_y)) / (g_screen_h > 1 ? g_screen_h - 1 : 1);
}

static void nap(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void emit(int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = (unsigned short)type;
    ev.code = (unsigned short)code;
    ev.value = value;
    if (write(g_fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
        fprintf(stderr, "write failed: %s\n", strerror(errno));
    }
}

/** @brief 在指定设备上选指并定位（ABS 绝对坐标）。
 *  @param slot 手指槽号（0 起）。
 *  @param x,y 屏幕绝对坐标。
 */
static void touch_down(int slot, int x, int y)
{
    if (g_proto == 2) {
        /* ST 协议：坐标用 ABS_X/ABS_Y（线性映射到 absinfo 范围），
           按压沿走 BTN_TOUCH=1；顺序按「坐标先于按压」投递，与内核
           ST 仿真回显一致。 */
        int dx, dy;
        st_map(x, y, &dx, &dy);
        emit(EV_ABS, ABS_X, dx);
        emit(EV_ABS, ABS_Y, dy);
        emit(EV_KEY, BTN_TOUCH, 1);
        emit(EV_SYN, SYN_REPORT, 0);
        return;
    }
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 1000 + slot); /* 任意非 -1 的跟踪 id */
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    emit(EV_ABS, ABS_MT_PRESSURE, 60);
    emit(EV_SYN, SYN_REPORT, 0);
}

/** @brief 指定手指移动到新坐标。 */
static void touch_move(int slot, int x, int y)
{
    if (g_proto == 2) {
        int dx, dy;
        st_map(x, y, &dx, &dy);
        emit(EV_ABS, ABS_X, dx);
        emit(EV_ABS, ABS_Y, dy);
        emit(EV_SYN, SYN_REPORT, 0);
        return;
    }
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    emit(EV_ABS, ABS_MT_PRESSURE, 60);
    emit(EV_SYN, SYN_REPORT, 0);
}

/** @brief 抬起指定手指（MT：TRACKING_ID=-1；ST：BTN_TOUCH=0）。 */
static void touch_up(int slot)
{
    if (g_proto == 2) {
        emit(EV_KEY, BTN_TOUCH, 0);
        emit(EV_SYN, SYN_REPORT, 0);
        return;
    }
    emit(EV_ABS, ABS_MT_SLOT, slot);
    emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    emit(EV_SYN, SYN_REPORT, 0);
}

/** @brief 列出 /dev/input 下设备的名称与能力，便于确认注入目标。 */
static void list_devices(void)
{
    DIR* d = opendir("/dev/input");
    struct dirent* de;
    char path[256];
    if (!d) { fprintf(stderr, "opendir /dev/input: %s\n", strerror(errno)); return; }
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[256] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
            snprintf(name, sizeof(name), "(unknown)");
        }
        unsigned long bits[4] = {0};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
        int has_abs = (bits[(ABS_MT_POSITION_X >> 5)] >> (ABS_MT_POSITION_X & 31)) & 1;
        printf("%s  %-28s  ABS_X=%d ABS_Y=%d\n", path, name,
               has_abs ? 1 : 0,
               (bits[(ABS_MT_POSITION_Y >> 5)] >> (ABS_MT_POSITION_Y & 31)) & 1);
        close(fd);
    }
    closedir(d);
}

/** @brief 打开输入设备：优先按名称含 touch 的设备，否则取第一个具 ABS_X 的。 */
static int open_touch_device(const char* prefer)
{
    DIR* d = opendir("/dev/input");
    struct dirent* de;
    char path[256];
    char fallback[256] = {0};
    if (!d) { fprintf(stderr, "opendir /dev/input: %s\n", strerror(errno)); return -1; }
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        /* 必须 O_RDWR：只写 fd 上 EVIOCGBIT 能力查询失败 → 位图全 0 →
           设备被误判为「无 ABS 轴」→ 找不到注入目标。 */
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            fd = open(path, O_WRONLY | O_NONBLOCK);
            if (fd < 0) continue;
        }
        char name[256] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        unsigned long bits[8] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) {
            close(fd);
            continue;
        }
        int has_x = (bits[(ABS_X >> 5)] >> (ABS_X & 31)) & 1;
        int has_y = (bits[(ABS_Y >> 5)] >> (ABS_Y & 31)) & 1;
        int has_mt_x = (bits[(ABS_MT_POSITION_X >> 5)] >> (ABS_MT_POSITION_X & 31)) & 1;
        int has_mt_y = (bits[(ABS_MT_POSITION_Y >> 5)] >> (ABS_MT_POSITION_Y & 31)) & 1;
        if (has_x || has_mt_x) {
            if (prefer && strstr(name, prefer)) { closedir(d); return fd; }
            if (!fallback[0]) snprintf(fallback, sizeof(fallback), "%s", path);
            close(fd);
        } else {
            close(fd);
        }
    }
    closedir(d);
    if (fallback[0]) return open(fallback, O_RDWR | O_NONBLOCK);
    fprintf(stderr, "no ABS touch device found under /dev/input\n");
    return -1;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s tap <x> <y>\n"
                "       %s swipe <x1> <y1> <x2> <y2> [steps] [delay_ms]\n"
                "       %s multi <slot> <x> <y> [<slot> <x> <y> ...]\n"
                "       %s list\n",
                argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "list") == 0) { list_devices(); return 0; }

    const char* prefer = getenv("TOUCH_DEV_NAME");
    const char* sw = getenv("TOUCH_SCREEN_W");
    const char* sh = getenv("TOUCH_SCREEN_H");
    if (sw) g_screen_w = atoi(sw);
    if (sh) g_screen_h = atoi(sh);
    g_fd = open_touch_device(prefer);
    if (g_fd < 0) return 3;
    probe_protocol(g_fd);
    if (g_proto == 0) return 4;

    if (strcmp(argv[1], "tap") == 0 && argc >= 4) {
        int x = atoi(argv[2]);
        int y = atoi(argv[3]);
        touch_down(0, x, y);
        nap(80);
        touch_up(0);
        return 0;
    }
    if (strcmp(argv[1], "swipe") == 0 && argc >= 6) {
        int x1 = atoi(argv[2]), y1 = atoi(argv[3]);
        int x2 = atoi(argv[4]), y2 = atoi(argv[5]);
        int steps = (argc >= 7) ? atoi(argv[6]) : 20;
        int delay = (argc >= 8) ? atoi(argv[7]) : 16;
        if (steps < 1) steps = 1;
        touch_down(0, x1, y1);
        for (int i = 1; i <= steps; ++i) {
            int x = x1 + (x2 - x1) * i / steps;
            int y = y1 + (y2 - y1) * i / steps;
            touch_move(0, x, y);
            nap(delay);
        }
        touch_up(0);
        return 0;
    }
    if (strcmp(argv[1], "multi") == 0 && argc >= 5) {
        int count = (argc - 2) / 3;
        for (int i = 0; i < count; ++i) {
            int slot = atoi(argv[2 + i * 3]);
            int x = atoi(argv[3 + i * 3]);
            int y = atoi(argv[4 + i * 3]);
            touch_down(slot, x, y);
            nap(30);
        }
        /* 保持按下，由调用方决定何时释放（可用 release 子命令） */
        return 0;
    }
    if (strcmp(argv[1], "release") == 0 && argc >= 3) {
        int slot = atoi(argv[2]);
        touch_up(slot);
        return 0;
    }
    fprintf(stderr, "bad arguments\n");
    return 2;
}
