/* touch_inject2.c —— mcgs 屏触摸注入器二号（2026-11-04）
 *
 * 与 mcgs_touch_inject 的差别：ST 协议坐标换算不再线性缩放 absinfo
 * 区间，而是运行时读取 /etc/pointercal 求逆矩阵（本板 TSC2007 raw
 * 0..4095 经校准矩阵旋转映射到 1024x600，线性缩放注入点整屏偏移）。
 * 全程单进程无 fork（shell 版每步 fork 3 进程，45 步拖动在小板上
 * 爬行数分钟），适合真机跟手拖动复现。
 *
 * 用法：
 *   touch_inject2 tap <sx> <sy>
 *   touch_inject2 down <sx> <sy> | move <sx> <sy> | up
 *   touch_inject2 drag <sx1> <sy1> <sx2> <sy2> [steps] [delay_ms] [hold_ms]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <linux/input.h>

static int g_fd = -1;
static int g_proto = 0;
static int g_st_min_x, g_st_max_x, g_st_min_y, g_st_max_y;
/* pointercal: xs=(a*xr+b*yr+c)/s, ys=(d*xr+e*yr+f)/s；逆变换用它解 raw。 */
static long long g_a, g_b, g_c, g_d, g_e, g_f, g_s;

static void nap_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* 一帧一次 write（真实触摸硬件的报文形态）：逐事件 write 会让读侧
 * 首读拿到半帧（坐标与按压沿被拆到两次读里），首触即被丢。 */
static struct input_event g_frame[8];
static int g_frameN;

static void frame_reset(void) { g_frameN = 0; }

static void frame_put(int type, int code, int value)
{
    struct input_event* ev;
    if (g_frameN >= (int)(sizeof(g_frame) / sizeof(g_frame[0]))) return;
    ev = &g_frame[g_frameN++];
    memset(ev, 0, sizeof(*ev));
    ev->type = (unsigned short)type;
    ev->code = (unsigned short)code;
    ev->value = value;
}

static void frame_flush(void)
{
    if (g_frameN <= 0) return;
    if (write(g_fd, g_frame, (size_t)g_frameN * sizeof(g_frame[0])) !=
        (ssize_t)((size_t)g_frameN * sizeof(g_frame[0])))
        fprintf(stderr, "write: %s\n", strerror(errno));
    g_frameN = 0;
}

static void emit(int type, int code, int value)
{
    frame_put(type, code, value);
}

static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); frame_flush(); }

static void probe(int fd)
{
    unsigned long bits[8] = {0};
    int has_mt_x, has_st_x, has_st_y;
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
    has_mt_x = (bits[(ABS_MT_POSITION_X / 32)] >> (ABS_MT_POSITION_X % 32)) & 1;
    has_st_x = (bits[(ABS_X / 32)] >> (ABS_X % 32)) & 1;
    has_st_y = (bits[(ABS_Y / 32)] >> (ABS_Y % 32)) & 1;
    if (has_mt_x) { g_proto = 1; return; }
    if (has_st_x && has_st_y) {
        struct input_absinfo ai;
        g_proto = 2;
        if (ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0) {
            g_st_min_x = ai.minimum;
            g_st_max_x = ai.maximum;
        }
        if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0) {
            g_st_min_y = ai.minimum;
            g_st_max_y = ai.maximum;
        }
    }
}

/* 读 /etc/pointercal；缺省按单位阵注入（不过校准）。 */
static void load_pointercal(void)
{
    FILE* f = fopen("/etc/pointercal", "r");
    g_a = 1;
    g_b = 0;
    g_c = 0;
    g_d = 0;
    g_e = 1;
    g_f = 0;
    g_s = 1;
    if (!f) return;
    if (fscanf(f, "%lld %lld %lld %lld %lld %lld %lld",
               &g_a, &g_b, &g_c, &g_d, &g_e, &g_f, &g_s) != 7)
        fprintf(stderr, "pointercal 解析失败，按单位阵注入\n");
    fclose(f);
}

/* 屏幕坐标 → raw 设备坐标（解 2x2 逆矩阵，long long 防溢出）。 */
static void st_map(int x, int y, int* dx, int* dy)
{
    long long det = g_a * g_e - g_b * g_d;
    long long u, v, xr, yr;
    if (det == 0) {
        *dx = x;
        *dy = y;
        return;
    }
    u = (long long)x * g_s - g_c;
    v = (long long)y * g_s - g_f;
    xr = (g_e * u - g_b * v) / det;
    yr = (-g_d * u + g_a * v) / det;
    if (xr < g_st_min_x) xr = g_st_min_x;
    if (xr > g_st_max_x) xr = g_st_max_x;
    if (yr < g_st_min_y) yr = g_st_min_y;
    if (yr > g_st_max_y) yr = g_st_max_y;
    *dx = (int)xr;
    *dy = (int)yr;
}

static void down(int x, int y)
{
    int dx, dy;
    frame_reset();
    if (g_proto == 2) {
        st_map(x, y, &dx, &dy);
        emit(EV_ABS, ABS_X, dx);
        emit(EV_ABS, ABS_Y, dy);
        emit(EV_KEY, BTN_TOUCH, 1);
        syn();
        return;
    }
    emit(EV_ABS, ABS_MT_SLOT, 0);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 1000);
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    syn();
}

static void move(int x, int y)
{
    int dx, dy;
    frame_reset();
    if (g_proto == 2) {
        st_map(x, y, &dx, &dy);
        emit(EV_ABS, ABS_X, dx);
        emit(EV_ABS, ABS_Y, dy);
        syn();
        return;
    }
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    syn();
}

static void up(void)
{
    frame_reset();
    if (g_proto == 2) {
        emit(EV_KEY, BTN_TOUCH, 0);
        syn();
        return;
    }
    emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    syn();
}

static int num(const char* s)
{
    return s ? atoi(s) : 0;
}

static void usage(void)
{
    fprintf(stderr, "用法: touch_inject2 tap <sx> <sy>\n");
    fprintf(stderr, "      touch_inject2 down|move <sx> <sy> | up\n");
    fprintf(stderr, "      touch_inject2 drag <x1> <y1> <x2> <y2> [steps] [delay_ms] [hold_ms]\n");
}

int main(int argc, char** argv)
{
    int i;
    int steps = 40;
    int delay_ms = 20;
    int hold_ms = 120;
    if (argc < 2) {
        usage();
        return 2;
    }
    g_fd = open("/dev/input/event0", O_WRONLY);
    if (g_fd < 0) {
        perror("open /dev/input/event0");
        return 1;
    }
    probe(g_fd);
    load_pointercal();
    fprintf(stderr,
            "proto=%d pointercal=%lld %lld %lld %lld %lld %lld %lld\n",
            g_proto, g_a, g_b, g_c, g_d, g_e, g_f, g_s);
    if (!strcmp(argv[1], "tap") && argc >= 4) {
        down(num(argv[2]), num(argv[3]));
        nap_ms(90);
        up();
    } else if (!strcmp(argv[1], "down") && argc >= 4) {
        down(num(argv[2]), num(argv[3]));
    } else if (!strcmp(argv[1], "move") && argc >= 4) {
        move(num(argv[2]), num(argv[3]));
    } else if (!strcmp(argv[1], "up")) {
        up();
    } else if (!strcmp(argv[1], "drag") && argc >= 6) {
        int x1 = num(argv[2]), y1 = num(argv[3]);
        int x2 = num(argv[4]), y2 = num(argv[5]);
        if (argc >= 7) steps = num(argv[6]);
        if (argc >= 8) delay_ms = num(argv[7]);
        if (argc >= 9) hold_ms = num(argv[8]);
        down(x1, y1);
        nap_ms(hold_ms);
        for (i = 1; i <= steps; ++i) {
            move(x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps);
            nap_ms(delay_ms);
        }
        nap_ms(hold_ms);
        up();
    } else {
        usage();
        close(g_fd);
        return 2;
    }
    close(g_fd);
    return 0;
}
