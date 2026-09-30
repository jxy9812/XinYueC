/******************************************************************************
 * @file       XSystemAndroid.c
 * @brief      XSystem 安卓平台采样后端（CPU/网络；内存沿用 POSIX 实现）。
 * @details    Android 的 SELinux 策略（untrusted_app 域）将 /proc/stat 与
 *             /proc/net/dev 对应用隐藏——shell 域可读不代表 app 可读
 *             （WSA/真机实测 fopen 即失败）。本文件提供沙箱内的替代源：
 *             - CPU：/proc/self/stat 的 utime+stime 增量除以墙钟增量，
 *               得到本进程占用的总 CPU 百分比（多核可 >100% 上限不裁剪，
 *               与 /proc/stat 全机口径差异已在显示层标注）。悬浮窗语义
 *               从"系统负载"变为"本进程负载"，但数据真实可用。
 *             - 网络：经 ANativeActivity->vm 的 JNI 调
 *               android.net.TrafficStats.getTotalRxBytes()/getTotalTxBytes()
 *               （系统级收发计数，应用无需任何权限）。JavaVM 由壳
 *               （android_main.c）经 XAndroid_setJavaVm 注入。
 *             - GPU：安卓无用户态 GPU 使用率通用接口，沿用 -1（无计数器）。
 * @note       平台 API 只出现在 Drive；本文件仅在 __ANDROID__ 下编译出
 *             实体，其他平台整文件为空。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XSystem_Protected.h"

#if defined(__ANDROID__)

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <jni.h>

/* ==================== JavaVM 注入（android_main.c 调用） ==================== */

static JavaVM* g_xsysAndroidVm = NULL;
static jobject g_xsysMainActivity = NULL;

void XAndroid_setJavaVm(JavaVM* vm)
{
    g_xsysAndroidVm = vm;
}

void XAndroid_setMainActivity(jobject activity)
{
    g_xsysMainActivity = activity;
}

/**
 * @brief  经 JNI 查询 Activity 窗口的真实逻辑尺寸（decorView 宽高）。
 * @details WSA 拖动缩放窗口时系统只改窗口逻辑尺寸而保持 surface 缓冲旧
 *          尺寸拉伸显示——输入事件坐标在逻辑空间，渲染缓冲在旧像素空
 *          间，两套坐标错位导致点击全部落偏。调用方拿到逻辑尺寸后以
 *          ANativeWindow_setBuffersGeometry 对齐缓冲，输入与渲染即 1:1。
 * @return true 查询成功；false 无 VM/反射失败（调用方保持现状）。
 */
/**
 * @brief  经 JNI 查询 Activity 窗口在屏幕上的原点。
 * @details 不同模拟器/设备的 MotionEvent X/Y 坐标空间不一致（WSA 报
 *          窗口本地坐标，BlueStacks 实测报屏幕坐标）。统一口径：输入
 *          注入用 getRawX/Y（屏幕坐标）减去本函数返回的窗口原点，得到
 *          与控件几何一致的窗口本地坐标。
 * @return true 查询成功；false 无 VM/反射失败（调用方按 0 处理）。
 */
bool XAndroid_getWindowScreenOrigin(int* outX, int* outY)
{
    JNIEnv* env = NULL;
    jclass natActivityCls;
    jmethodID midGetWindow;
    jobject window;
    jclass windowCls;
    jmethodID midGetDecorView;
    jobject decorView;
    jclass viewCls;
    jmethodID midGetLoc;
    jmethodID midContent;
    jobject contentView;
    jintArray arr;
    jint elems[2];

    if (!outX || !outY) return false;
    *outX = 0;
    *outY = 0;
    if (!g_xsysAndroidVm || !g_xsysMainActivity) return false;
    if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                   JNI_VERSION_1_6) != JNI_OK &&
        (*g_xsysAndroidVm)->AttachCurrentThread(g_xsysAndroidVm,
                                                &env, NULL) != JNI_OK)
        return false;

    natActivityCls = (*env)->GetObjectClass(env, g_xsysMainActivity);
    if (!natActivityCls) goto fail2;
    midGetWindow = (*env)->GetMethodID(env, natActivityCls, "getWindow",
                                       "()Landroid/view/Window;");
    if (!midGetWindow) goto fail2;
    window = (*env)->CallObjectMethod(env, g_xsysMainActivity, midGetWindow);
    if (!window) goto fail2;
    /* 用 android.R.id.content（内容视图）而非 decorView：自由窗口的
       系统标题栏(caption)属于装饰层，内容视图原点在其下方——用户实测
       BlueStacks 上点击触发位置恒偏下约一个标题栏高度，即少减了这段。 */
    midContent = (*env)->GetMethodID(env, natActivityCls, "findViewById",
                                           "(I)Landroid/view/View;");
    if (!midContent) goto fail2;
    contentView = (*env)->CallObjectMethod(env, g_xsysMainActivity, midContent,
                                                 0x01020002);
    if (!contentView) goto fail2;
    viewCls = (*env)->GetObjectClass(env, contentView);
    midGetLoc = (*env)->GetMethodID(env, viewCls, "getLocationOnScreen",
                                    "([I)V");
    if (!midGetLoc) goto fail2;
    arr = (*env)->NewIntArray(env, 2);
    if (!arr) goto fail2;
    (*env)->CallVoidMethod(env, contentView, midGetLoc, arr);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto fail2;
    }
    (*env)->GetIntArrayRegion(env, arr, 0, 2, elems);
    (*env)->DeleteLocalRef(env, arr);
    (*env)->DeleteLocalRef(env, natActivityCls);
    *outX = (int)elems[0];
    *outY = (int)elems[1];
    return true;

fail2:
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    return false;
}

/**
 * @brief  经 JNI 把 Activity 退到后台（moveTaskToBack）——最小化语义。
 * @return true 已请求；false 无 VM/实例/反射失败。
 */
bool XAndroid_moveTaskToBack(void)
{
    JNIEnv* env = NULL;
    jclass natActivityCls;
    jmethodID midMove;
    jobject window;
    jclass windowCls;
    jmethodID midMove2;
    jboolean ok;

    if (!g_xsysAndroidVm || !g_xsysMainActivity) return false;
    if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                   JNI_VERSION_1_6) != JNI_OK &&
        (*g_xsysAndroidVm)->AttachCurrentThread(g_xsysAndroidVm,
                                                &env, NULL) != JNI_OK)
        return false;
    natActivityCls = (*env)->GetObjectClass(env, g_xsysMainActivity);
    if (!natActivityCls) goto fail3;
    /* ANativeActivity 自带 moveTaskToBack(boolean) 实例方法（Activity）。 */
    midMove = (*env)->GetMethodID(env, natActivityCls, "moveTaskToBack",
                                  "(Z)Z");
    if (!midMove) goto fail3;
    ok = (*env)->CallBooleanMethod(env, g_xsysMainActivity, midMove,
                                   JNI_TRUE);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return false;
    }
    (*env)->DeleteLocalRef(env, natActivityCls);
    return (bool)ok;

fail3:
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    return false;
}

bool XAndroid_getWindowFrameSize(int* outWidth, int* outHeight)
{
    JNIEnv* env = NULL;
    jclass natActivityCls;
    jmethodID midContent;
    jobject contentView;
    jclass viewCls;
    jmethodID midGetWidth;
    jmethodID midGetHeight;
    jint w;
    jint h;

    if (!outWidth || !outHeight) return false;
    *outWidth = 0;
    *outHeight = 0;
    if (!g_xsysAndroidVm || !g_xsysMainActivity) return false;
    if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                   JNI_VERSION_1_6) != JNI_OK &&
        (*g_xsysAndroidVm)->AttachCurrentThread(g_xsysAndroidVm,
                                                &env, NULL) != JNI_OK)
        return false;
    natActivityCls = (*env)->GetObjectClass(env, g_xsysMainActivity);
    if (!natActivityCls) return false;

    midContent = (*env)->GetMethodID(env, natActivityCls, "findViewById",
                                           "(I)Landroid/view/View;");
    if (!midContent) return false;
    contentView = (*env)->CallObjectMethod(env, g_xsysMainActivity, midContent,
                                                 0x01020002);
    if (!contentView) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        return false;
    }
    viewCls = (*env)->GetObjectClass(env, contentView);
    midGetWidth = (*env)->GetMethodID(env, viewCls, "getWidth", "()I");
    midGetHeight = (*env)->GetMethodID(env, viewCls, "getHeight", "()I");
    if (!midGetWidth || !midGetHeight) return false;
    w = (*env)->CallIntMethod(env, contentView, midGetWidth);
    h = (*env)->CallIntMethod(env, contentView, midGetHeight);
    (*env)->DeleteLocalRef(env, contentView);
    (*env)->DeleteLocalRef(env, viewCls);
    if (w <= 0 || h <= 0) return false;
    *outWidth = (int)w;
    *outHeight = (int)h;
    return true;
}

/** @brief 取当前线程 JNIEnv；主线程外首次调用时附加（ detach 由本文件
 *  在采样线程为常驻线程的前提下免做——渲染线程与 demo 生命周期一致）。 */
static JNIEnv* xsys_androidEnv(void)
{
    JNIEnv* env = NULL;
    if (!g_xsysAndroidVm) return NULL;
    if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                   JNI_VERSION_1_6) == JNI_OK)
        return env;
    if ((*g_xsysAndroidVm)->AttachCurrentThread(g_xsysAndroidVm,
                                                &env, NULL) == JNI_OK)
        return env;
    return NULL;
}

/* ==================== CPU（/proc/self/stat 进程级口径） ==================== */

#if XSYSTEM_CPU_USAGE_ON

double XSystem_platformCpuUsagePercent(void)
{
    static bool s_init = false;
    static long long s_prevCpuTicks = 0;
    static long long s_prevWallMs = 0;
    FILE* f;
    char line[512];
    long long utime = 0;
    long long stime = 0;
    long long cpuTicks;
    long long wallMs;
    long long dCpu;
    long long dWall;
    struct timespec ts;
    double percent;
    const long long kHertz = 100; /* bionic CLK_TCK 恒 100 */
    int scanned;

    f = fopen("/proc/self/stat", "r");
    if (!f) return -1.0;
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1.0;
    }
    fclose(f);
    /* 进程名可能含空格/括号，定位最后一个 ')' 后再解析。 */
    {
        char* closeParen = strrchr(line, ')');
        char* cursor;
        int field;
        if (!closeParen) return -1.0;
        cursor = closeParen + 1; /* 越过 ") " 后从 state 字段(3)起 */
        /* state(3) 是单字母（S/R/...），非数字：先跳过它再走数值循环，
           对齐字段号——此后 ppid=4 ... utime=14, stime=15。 */
        while (*cursor == ' ') ++cursor;
        if (!*cursor) return -1.0;
        ++cursor; /* 吃掉 state 字母 */
        field = 3;
        scanned = 0;
        while (*cursor && field < 15) {
            char* end;
            long long value;
            while (*cursor == ' ') ++cursor;
            if (!*cursor) break;
            value = strtoll(cursor, &end, 10);
            if (end == cursor) break;
            ++field;
            if (field == 14) utime = value;   /* utime: 第 14 字段 */
            if (field == 15) stime = value;   /* stime: 第 15 字段 */
            if (field >= 15) { ++scanned; break; }
            cursor = end;
        }
        if (scanned == 0) return -1.0;
    }
    cpuTicks = utime + stime;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1.0;
    wallMs = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;

    if (!s_init) {
        s_init = true;
        s_prevCpuTicks = cpuTicks;
        s_prevWallMs = wallMs;
        return -1.0; /* 首次只建基线 */
    }
    dCpu = cpuTicks - s_prevCpuTicks;
    dWall = wallMs - s_prevWallMs;
    s_prevCpuTicks = cpuTicks;
    s_prevWallMs = wallMs;
    if (dWall <= 0) return -1.0;
    /* ticks/ms * 1000/hertz * 100 = 百分比；多核进程可 >100%。 */
    percent = 100.0 * (double)dCpu / ((double)dWall * 0.001 * (double)kHertz);
    if (percent < 0.0) percent = 0.0;
    return percent;
}

#endif /* XSYSTEM_CPU_USAGE_ON */

/* ==================== GPU 使用率（sysfs 探测，可选权限路径） ==================== */

/*
 * Adreno: /sys/class/kgsl/kgsl-3d0/gpubusy —— 内容 " 可用 busy total "，
 * busy/total 即占用率；需要读前写 0 复位（部分内核只读也能算）。
 * Mali:   /sys/kernel/gpu/gpu_busy —— 直接百分比。
 * SELinux 默认把 untrusted_app 挡在这两个节点外（实测 EACCES）；
 * 若设备用户已 root 或厂商放行则可读——探测式读取，读不到按缺失处理。
 * 返回 [0,100]；不可用返回 -1（调用方显示 "-"）。
 */
#include <fcntl.h>
#include <unistd.h>

static int xsys_android_readIntFile(const char* path)
{
    char buf[64];
    int fd = open(path, O_RDONLY);
    int n;
    if (fd < 0) return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    return atoi(buf);
}

double XAndroid_gpuUsagePercent(void)
{
    /* Adreno kgsl：gpubusy 格式 "<busy> <total>"（jiffies 累计计数，
       差分除以墙钟才是占用率；写 "0" 复位是官方协议但多数内核只读。
       无 root 时节点本身 EACCES，探测式读取，读不到回退 Mali 节点。 */
    {
        char buf[64];
        int busy = -1;
        int total = -1;
        int fd = open("/sys/class/kgsl/kgsl-3d0/gpubusy", O_RDWR);
        if (fd >= 0) {
            (void)!write(fd, "0", 1);
            int n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = 0;
                if (sscanf(buf, "%d %d", &busy, &total) == 2 &&
                    total > 0 && busy >= 0 && busy <= total) {
                    /* kgsl 是自上次复位以来的累计值，单次读即为区间占用率 */
                    return 100.0 * (double)busy / (double)total;
                }
            }
        }
        (void)busy; (void)total;
    }
    /* Mali: gpu_busy 直接带 % 号。 */
    {
        char buf[32];
        int fd = open("/sys/kernel/gpu/gpu_busy", O_RDONLY);
        int n;
        if (fd >= 0) {
            n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = 0;
                int v = atoi(buf);
                if (v >= 0 && v <= 100) return (double)v;
            }
        }
    }
    return -1.0;
}

/* 平台 GPU 使用率入口：Adreno kgsl / Mali sysfs 探测式读取，
 * 全部不可用时返回 -1（悬浮窗按计数器缺失显示 "-"）。 */
double XSystem_platformGpuUsagePercent(void)
{
    return XAndroid_gpuUsagePercent();
}

/* ==================== 网络（JNI -> TrafficStats 系统级计数） ==================== */

bool XAndroid_getNetworkCounters(uint64_t* rxBytes, uint64_t* txBytes);

bool XAndroid_getNetworkCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    JNIEnv* env;
    jclass cls;
    jmethodID midRx;
    jmethodID midTx;
    jlong rx;
    jlong tx;

    if (!rxBytes || !txBytes) return false;
    *rxBytes = 0;
    *txBytes = 0;
    env = xsys_androidEnv();
    if (!env) return false;
    cls = (*env)->FindClass(env, "android/net/TrafficStats");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return false;
    }
    midRx = (*env)->GetStaticMethodID(env, cls, "getTotalRxBytes",
                                      "()J");
    midTx = (*env)->GetStaticMethodID(env, cls, "getTotalTxBytes",
                                      "()J");
    if (!midRx || !midTx) {
        (*env)->ExceptionClear(env);
        return false;
    }
    rx = (*env)->CallStaticLongMethod(env, cls, midRx);
    tx = (*env)->CallStaticLongMethod(env, cls, midTx);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return false;
    }
    (*env)->DeleteLocalRef(env, cls);
    *rxBytes = (uint64_t)rx;
    *txBytes = (uint64_t)tx;
    return true;
}

#endif /* __ANDROID__ */
