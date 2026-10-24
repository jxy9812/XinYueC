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

#include "XPlatformScreen.h"
#include "XMemory.h"
#include "XAndroidJniChannels.h" /* 本文件实现的 JNI 通道声明（Drive 内部） */
#include "XAndroidJavaPackage.h" /* Java 包名唯一来源（JNI 查类拼全名用） */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <jni.h>
#include <android/log.h>
/* 剪贴板 JNI 失败点追踪（tag=XClip；定位 getSystemService/setPrimaryClip
 * 哪一环失败用，成功路径零输出）。 */
#define XCLIP_TRACE(...) \
    ((void)__android_log_print(ANDROID_LOG_INFO, "XClip", __VA_ARGS__))

/* ==================== JavaVM 注入（android_main.c 调用） ==================== */

static JavaVM* g_xsysAndroidVm = NULL;
static jobject g_xsysMainActivity = NULL;

/* JNI 查询 last-known-good 缓存：密度是设备常量；原点/窗口尺寸仅在
   窗口化↔最大化切换时缓变。查询抖动（Activity 过渡期读数失效）沿用
   旧值远好于回落 0/160——0 会把屏幕坐标当窗口本地坐标用（DOWN/UP
   原点不一致即出现负坐标），160/1.0 会让整套 DPI 缩放静默失效。 */
static int g_xsysLastOriginX = 0;
static int g_xsysLastOriginY = 0;
static bool g_xsysHaveOrigin = false;
static int g_xsysLastFrameW = 0;
static int g_xsysLastFrameH = 0;
static bool g_xsysHaveFrame = false;
static float g_xsysLastDensity = 0.0f;
static int g_xsysLastDensityDpi = 0;
static int g_xsysLastXdpi = 0;
static int g_xsysLastYdpi = 0;
static bool g_xsysHaveDensity = false;

void XAndroid_setJavaVm(JavaVM* vm)
{
    g_xsysAndroidVm = vm;
}

void XAndroid_setMainActivity(jobject activity)
{
    /* activity->clazz 是 onCreate 调用帧里的【局部引用】：仅 UI 线程
       当前消息帧内有效。demo/输入线程随后的密度/原点/窗口尺寸查询
       跨线程复用它属未定义行为——实测同一份代码首启 JNI 查询成功、
       关闭后冷启二启整链失败（回落 160/1.0、几何 0x0），运行期读数
       抖动致点击坐标漂移（用户实测"点击左侧不准"根因）。必须升全局
       引用；Activity 重建换实例时释放旧全局引用防泄漏。 */
    if (g_xsysMainActivity && g_xsysAndroidVm) {
        JNIEnv* env = NULL;
        if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                       JNI_VERSION_1_6) == JNI_OK) {
            (*env)->DeleteGlobalRef(env, g_xsysMainActivity);
            g_xsysMainActivity = NULL;
        }
    }
    g_xsysMainActivity = activity;
    if (g_xsysMainActivity && g_xsysAndroidVm) {
        JNIEnv* env = NULL;
        if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                       JNI_VERSION_1_6) == JNI_OK)
            g_xsysMainActivity =
                (*env)->NewGlobalRef(env, g_xsysMainActivity);
    }
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
    if (!g_xsysAndroidVm || !g_xsysMainActivity) goto fail2;
    if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                   JNI_VERSION_1_6) != JNI_OK &&
        (*g_xsysAndroidVm)->AttachCurrentThread(g_xsysAndroidVm,
                                                &env, NULL) != JNI_OK)
        goto fail2;

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
    g_xsysLastOriginX = *outX;
    g_xsysLastOriginY = *outY;
    g_xsysHaveOrigin = true;
    return true;

fail2:
    if (env && (*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    /* 查询抖动兜底：沿用上次成功原点而非 0——窗口化/最大化切换瞬间
       原点读数失效会把点击算到窗外（DOWN/UP 原点不一致即负坐标）。 */
    if (g_xsysHaveOrigin) {
        *outX = g_xsysLastOriginX;
        *outY = g_xsysLastOriginY;
        return true;
    }
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
    if (!g_xsysAndroidVm || !g_xsysMainActivity) goto fail3;
    if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                   JNI_VERSION_1_6) != JNI_OK &&
        (*g_xsysAndroidVm)->AttachCurrentThread(g_xsysAndroidVm,
                                                &env, NULL) != JNI_OK)
        goto fail3;
    natActivityCls = (*env)->GetObjectClass(env, g_xsysMainActivity);
    if (!natActivityCls) goto fail3;

    midContent = (*env)->GetMethodID(env, natActivityCls, "findViewById",
                                           "(I)Landroid/view/View;");
    if (!midContent) goto fail3;
    contentView = (*env)->CallObjectMethod(env, g_xsysMainActivity, midContent,
                                                 0x01020002);
    if (!contentView) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        goto fail3;
    }
    viewCls = (*env)->GetObjectClass(env, contentView);
    midGetWidth = (*env)->GetMethodID(env, viewCls, "getWidth", "()I");
    midGetHeight = (*env)->GetMethodID(env, viewCls, "getHeight", "()I");
    if (!midGetWidth || !midGetHeight) goto fail3;
    w = (*env)->CallIntMethod(env, contentView, midGetWidth);
    h = (*env)->CallIntMethod(env, contentView, midGetHeight);
    (*env)->DeleteLocalRef(env, contentView);
    (*env)->DeleteLocalRef(env, viewCls);
    if (w <= 0 || h <= 0) goto fail3;
    *outWidth = (int)w;
    *outHeight = (int)h;
    g_xsysLastFrameW = *outWidth;
    g_xsysLastFrameH = *outHeight;
    g_xsysHaveFrame = true;
    return true;

fail3:
    /* 查询抖动兜底：沿用上次成功尺寸而非 0（0x0 会让屏幕登记/最大化
       目标几何全错）。 */
    if (g_xsysHaveFrame) {
        *outWidth = g_xsysLastFrameW;
        *outHeight = g_xsysLastFrameH;
        return true;
    }
    return false;
}

/**
 * @brief  经 JNI 查询 DisplayMetrics 密度（dpr 唯一数据源 + 物理 DPI）。
 * @details getResources().getDisplayMetrics() 四字段：density（即 dpr，
 *          wm density 覆盖即时生效）、densityDpi（整档，仅诊断日志用）、
 *          xdpi/ydpi（设备真实物理 DPI，XScreen physicalSize 毫米反推用，
 *          见设计 §1.5）。回填定版：只回填 setDevicePixelRatio(density)，
 *          logicalDpi 永不回填（H1：densityDpi 灌入 logicalDpi 会触发
 *          dpr²x(densityDpi/96) 复合爆炸）。纯 JNI 读数（GetEnv/Attach
 *          双路径自适应任意线程），UI 线程首帧同步采样依此（不属信号面
 *          收口，§1.4 澄清）。
 * @return true 查询成功；false 无 VM/反射失败/数值非法（调用方兜底
 *         160 dpi / 1.0，安全退化为现网 dpr=1 行为）。
 */
bool XAndroid_getDisplayDensity(float* outDensity, int* outDensityDpi,
                                int* outXdpi, int* outYdpi)
{
    JNIEnv* env = NULL;
    jclass natActivityCls = NULL;
    jmethodID midGetResources;
    jobject resources = NULL;
    jclass resCls = NULL;
    jmethodID midMetrics;
    jobject metrics = NULL;
    jclass metricsCls = NULL;
    jfieldID fidDensity;
    jfieldID fidDensityDpi;
    jfieldID fidXdpi;
    jfieldID fidYdpi;
    float density;
    int densityDpi;
    float xdpi;
    float ydpi;

    /* 兜底先写：任一步失败调用方拿到的都是 160/1.0 安全值。 */
    if (outDensity) *outDensity = 1.0f;
    if (outDensityDpi) *outDensityDpi = 160;
    if (outXdpi) *outXdpi = 160;
    if (outYdpi) *outYdpi = 160;
    if (!outDensity || !outDensityDpi || !outXdpi || !outYdpi) return false;
    if (!g_xsysAndroidVm || !g_xsysMainActivity) goto fail;
    if ((*g_xsysAndroidVm)->GetEnv(g_xsysAndroidVm, (void**)&env,
                                   JNI_VERSION_1_6) != JNI_OK &&
        (*g_xsysAndroidVm)->AttachCurrentThread(g_xsysAndroidVm,
                                                &env, NULL) != JNI_OK)
        goto fail;

    natActivityCls = (*env)->GetObjectClass(env, g_xsysMainActivity);
    if (!natActivityCls) goto fail;
    midGetResources = (*env)->GetMethodID(env, natActivityCls,
                                          "getResources",
                                          "()Landroid/content/res/Resources;");
    if (!midGetResources) goto fail;
    resources = (*env)->CallObjectMethod(env, g_xsysMainActivity,
                                         midGetResources);
    if (!resources) goto fail;
    resCls = (*env)->GetObjectClass(env, resources);
    midMetrics = (*env)->GetMethodID(env, resCls, "getDisplayMetrics",
                                     "()Landroid/util/DisplayMetrics;");
    if (!midMetrics) goto fail;
    metrics = (*env)->CallObjectMethod(env, resources, midMetrics);
    if (!metrics) goto fail;
    metricsCls = (*env)->GetObjectClass(env, metrics);
    fidDensity = (*env)->GetFieldID(env, metricsCls, "density", "F");
    fidDensityDpi = (*env)->GetFieldID(env, metricsCls, "densityDpi", "I");
    fidXdpi = (*env)->GetFieldID(env, metricsCls, "xdpi", "F");
    fidYdpi = (*env)->GetFieldID(env, metricsCls, "ydpi", "F");
    if (!fidDensity || !fidDensityDpi || !fidXdpi || !fidYdpi) goto fail;
    density = (*env)->GetFloatField(env, metrics, fidDensity);
    densityDpi = (*env)->GetIntField(env, metrics, fidDensityDpi);
    xdpi = (*env)->GetFloatField(env, metrics, fidXdpi);
    ydpi = (*env)->GetFloatField(env, metrics, fidYdpi);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto fail;
    }
    /* 非法读数（部分 ROM 返回 0）按失败处理，走 160/1.0 兜底。 */
    if (density <= 0.0f || xdpi <= 0.0f || ydpi <= 0.0f ||
        densityDpi <= 0)
        goto fail;
    *outDensity = density;
    *outDensityDpi = densityDpi;
    *outXdpi = (int)(xdpi + 0.5f);
    *outYdpi = (int)(ydpi + 0.5f);
    /* last-known-good：密度是设备常量，成功一次永久缓存；后续查询失败
       （Activity 过渡/线程态抖动）沿用缓存而非回落 160/1.0——后者会让
       整套 DPI 缩放静默失效（dpr 1.5→1.0 翻转即点击全偏）。 */
    g_xsysLastDensity = density;
    g_xsysLastDensityDpi = densityDpi;
    g_xsysLastXdpi = *outXdpi;
    g_xsysLastYdpi = *outYdpi;
    g_xsysHaveDensity = true;
    (*env)->DeleteLocalRef(env, metricsCls);
    (*env)->DeleteLocalRef(env, metrics);
    (*env)->DeleteLocalRef(env, resCls);
    (*env)->DeleteLocalRef(env, resources);
    (*env)->DeleteLocalRef(env, natActivityCls);
    return true;

fail:
    /* 失败路径与成功路径同一纪律：清异常后删除已创建的局部引用
       （未创建者为 NULL，DeleteLocalRef(NULL) 按 JNI 规范为安全空操作；
       env 为 NULL 时异常检查整体跳过）。 */
    if (env && (*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (metricsCls) (*env)->DeleteLocalRef(env, metricsCls);
    if (metrics) (*env)->DeleteLocalRef(env, metrics);
    if (resCls) (*env)->DeleteLocalRef(env, resCls);
    if (resources) (*env)->DeleteLocalRef(env, resources);
    if (natActivityCls) (*env)->DeleteLocalRef(env, natActivityCls);
    /* 有历史成功读数时沿用缓存（密度是设备常量）；从未成功才回落
       160/1.0，由调用方 m_densityValid 守卫位按"未知"处理。 */
    if (g_xsysHaveDensity) {
        if (outDensity) *outDensity = g_xsysLastDensity;
        if (outDensityDpi) *outDensityDpi = g_xsysLastDensityDpi;
        if (outXdpi) *outXdpi = g_xsysLastXdpi;
        if (outYdpi) *outYdpi = g_xsysLastYdpi;
        return true;
    }
    return false;
}

/* ==================== XPlatformScreen 契约实现（安卓） ==================== */
/* 统一平台 API 的安卓侧：委托上方三个 JNI 读数函数（last-known-good
   缓存纪律在那些函数内），公共层经 Src/XGui/Platform/XPlatformScreen.h
   的统一签名拉取，不感知 JNI 细节。 */

bool XPlatformScreen_isAvailable(void)
{
    return g_xsysAndroidVm != NULL && g_xsysMainActivity != NULL;
}

bool XPlatformScreen_queryDpi(XPlatformScreenDpiInfo* out)
{
    if (!out) return false;
    out->m_valid = XAndroid_getDisplayDensity(&out->m_density,
                                              &out->m_densityDpi,
                                              &out->m_xdpi, &out->m_ydpi);
    return out->m_valid;
}

bool XPlatformScreen_queryOrigin(int* outX, int* outY)
{
    return XAndroid_getWindowScreenOrigin(outX, outY);
}

bool XPlatformScreen_queryFrameSize(int* outWidth, int* outHeight)
{
    return XAndroid_getWindowFrameSize(outWidth, outHeight);
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

/* ==================== 剪贴板（JNI -> ClipboardManager） ====================
 * [2026-10-24 安卓剪贴板接入] 系统级剪贴板读写：文本走
 * ClipboardManager.getPrimaryClip()/setPrimaryClip(ClipData.newPlainText)。
 * 注意点：
 * - getSystemService 挂在 Activity（Context）上；g_xsysMainActivity 已是
 *   全局引用（见 XAndroid_setMainActivity 的局部引用升级注释）。
 * - 读方向用 Item.coerceToText(context)：Intent/URI/文本统一降为文本，
 *   对标 QClipboard::text 的宽收口径。
 * - Android 10+ 对后台应用读剪贴板有焦点限制（SecurityException/空）：
 *   异常一律 ExceptionClear 并报 false，上层回落进程内存储。
 * - GetStringUTFChars 返回 modified UTF-8；BMP 内与标准 UTF-8 兼容，
 *   演示/工控文本域足够，增补平面字符有 CESU-8 伪影（已知边界）。 */

/** @brief 解析 ClipboardManager 实例（调用方不持 local ref，仅用即弃）。 */
static jobject xsys_clipManager(JNIEnv* env)
{
    jclass ctxCls;
    jmethodID midGetSvc;
    jstring svcName;
    jobject mgr;
    if (!g_xsysMainActivity) {
        XCLIP_TRACE("mgr: no activity ref");
        return NULL;
    }
    ctxCls = (*env)->FindClass(env, "android/content/Context");
    if (!ctxCls) goto fail;
    midGetSvc = (*env)->GetMethodID(env, ctxCls, "getSystemService",
                                    "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!midGetSvc) goto fail;
    svcName = (*env)->NewStringUTF(env, "clipboard");
    if (!svcName) goto fail;
    mgr = (*env)->CallObjectMethod(env, g_xsysMainActivity, midGetSvc,
                                   svcName);
    if ((*env)->ExceptionCheck(env)) goto fail;
    XCLIP_TRACE("mgr: ok mgr=%p", mgr != NULL ? (void*)mgr : NULL);
    return mgr; /* 调用方在使用帧内消费；本帧退出即由 JNI 表回收 */

fail:
    XCLIP_TRACE("mgr: FAIL (find/method/call), cleared");
    (*env)->ExceptionClear(env);
    return NULL;
}

bool XAndroid_clipboardGetText(char** outText)
{
    JNIEnv* env;
    jobject mgr;
    jclass mgrCls;
    jmethodID midGetClip;
    jobject clip;
    jclass clipCls;
    jmethodID midCount;
    jmethodID midGetItem;
    jobject item;
    jclass itemCls;
    jmethodID midCoerce;
    jobject cs;
    jclass csCls;
    jmethodID midToString;
    jstring jstr;
    const char* utf;
    jsize len;
    char* copy;

    if (!outText) return false;
    *outText = NULL;
    env = xsys_androidEnv();
    if (!env) {
        XCLIP_TRACE("getText: no env");
        return false;
    }
    mgr = xsys_clipManager(env);
    if (!mgr) return false;
    mgrCls = (*env)->GetObjectClass(env, mgr);
    midGetClip = (*env)->GetMethodID(env, mgrCls, "getPrimaryClip",
                                     "()Landroid/content/ClipData;");
    if (!midGetClip) goto fail;
    clip = (*env)->CallObjectMethod(env, mgr, midGetClip);
    if ((*env)->ExceptionCheck(env) || !clip) {
        XCLIP_TRACE("getText: primary clip null/exception");
        goto fail;
    }
    clipCls = (*env)->GetObjectClass(env, clip);
    midCount = (*env)->GetMethodID(env, clipCls, "getItemCount", "()I");
    midGetItem = (*env)->GetMethodID(env, clipCls, "getItemAt",
                                     "(I)Landroid/content/ClipData$Item;");
    if (!midCount || !midGetItem) goto fail;
    if ((*env)->CallIntMethod(env, clip, midCount) <= 0) goto fail;
    item = (*env)->CallObjectMethod(env, clip, midGetItem, 0);
    if ((*env)->ExceptionCheck(env) || !item) goto fail;
    itemCls = (*env)->GetObjectClass(env, item);
    midCoerce = (*env)->GetMethodID(env, itemCls, "coerceToText",
                                    "(Landroid/content/Context;)"
                                    "Ljava/lang/CharSequence;");
    if (!midCoerce) goto fail;
    cs = (*env)->CallObjectMethod(env, item, midCoerce, g_xsysMainActivity);
    if ((*env)->ExceptionCheck(env) || !cs) goto fail;
    csCls = (*env)->GetObjectClass(env, cs);
    midToString = (*env)->GetMethodID(env, csCls, "toString",
                                      "()Ljava/lang/String;");
    if (!midToString) goto fail;
    jstr = (*env)->CallObjectMethod(env, cs, midToString);
    if ((*env)->ExceptionCheck(env) || !jstr) goto fail;
    utf = (*env)->GetStringUTFChars(env, jstr, NULL);
    if (!utf) goto fail;
    len = (*env)->GetStringUTFLength(env, jstr);
    copy = (char*)XMalloc_System((size_t)len + 1u);
    if (copy) {
        memcpy(copy, utf, (size_t)len);
        copy[len] = '\0';
        *outText = copy;
    }
    (*env)->ReleaseStringUTFChars(env, jstr, utf);
    XCLIP_TRACE("getText: ok len=%d", (int)len);
    return copy != NULL;

fail:
    (*env)->ExceptionClear(env);
    return false;
}

bool XAndroid_clipboardSetText(const char* utf8)
{
    JNIEnv* env;
    jobject mgr;
    jclass mgrCls;
    jclass clipCls;
    jmethodID midNewPlain;
    jmethodID midSet;
    jstring label;
    jstring jtext;
    jobject clip;

    if (!utf8) utf8 = "";
    env = xsys_androidEnv();
    if (!env) return false;
    mgr = xsys_clipManager(env);
    if (!mgr) return false;
    clipCls = (*env)->FindClass(env, "android/content/ClipData");
    if (!clipCls) goto fail;
    midNewPlain = (*env)->GetStaticMethodID(env, clipCls, "newPlainText",
                                            "(Ljava/lang/CharSequence;"
                                            "Ljava/lang/CharSequence;)"
                                            "Landroid/content/ClipData;");
    if (!midNewPlain) goto fail;
    mgrCls = (*env)->GetObjectClass(env, mgr);
    midSet = (*env)->GetMethodID(env, mgrCls, "setPrimaryClip",
                                 "(Landroid/content/ClipData;)V");
    if (!midSet) {
        XCLIP_TRACE("setText: no setPrimaryClip mid");
        goto fail;
    }
    label = (*env)->NewStringUTF(env, "XGuiDemo");
    jtext = (*env)->NewStringUTF(env, utf8);
    if (!jtext) goto fail;
    clip = (*env)->CallStaticObjectMethod(env, clipCls, midNewPlain,
                                          label, jtext);
    if ((*env)->ExceptionCheck(env) || !clip) {
        XCLIP_TRACE("setText: newPlainText failed");
        goto fail;
    }
    (*env)->CallVoidMethod(env, mgr, midSet, clip);
    if ((*env)->ExceptionCheck(env)) {
        XCLIP_TRACE("setText: setPrimaryClip threw (focus/permission?)");
        goto fail;
    }
    XCLIP_TRACE("setText: ok len=%d", (int)strlen(utf8));
    return true;

fail:
    (*env)->ExceptionClear(env);
    return false;
}

bool XAndroid_clipboardClear(void)
{
    /* Android 无独立清空：写一条空文本等价（对标 QClipboard::clear 的
       系统可见效果）。 */
    return XAndroid_clipboardSetText("");
}

/* ==================== 平台补扫 2026-10-24（Services/Theme/WiFi） ====================
 * 三个薄 JNI 通道，与剪贴板同纪律：VM/Activity 引用由壳层注入、异常一律
 * ExceptionClear 后报 false、失败路径零副作用。 */

/** @brief Activity 引用是否就绪（Services 驱动 isAvailable 口径）。 */
bool XAndroid_hasActivity(void)
{
    return g_xsysMainActivity != NULL;
}

/** @brief 打开系统浏览器/对应处理器（ACTION_VIEW；对标 xdg-open）。 */
bool XAndroid_openUrl(const char* url)
{
    JNIEnv* env;
    jclass uriCls;
    jmethodID midParse;
    jobject uri;
    jclass intentCls;
    jmethodID midCtor;
    jmethodID midSetData;
    jmethodID midStart;
    jstring jurl;
    jstring jaction;
    jobject intent;

    if (!url || !url[0]) return false;
    env = xsys_androidEnv();
    if (!env || !g_xsysMainActivity) return false;
    uriCls = (*env)->FindClass(env, "android/net/Uri");
    if (!uriCls) goto fail;
    midParse = (*env)->GetStaticMethodID(env, uriCls, "parse",
                                         "(Ljava/lang/String;)"
                                         "Landroid/net/Uri;");
    if (!midParse) goto fail;
    intentCls = (*env)->FindClass(env, "android/content/Intent");
    if (!intentCls) goto fail;
    midCtor = (*env)->GetMethodID(env, intentCls, "<init>",
                                  "(Ljava/lang/String;)V");
    midSetData = (*env)->GetMethodID(env, intentCls, "setData",
                                     "(Landroid/net/Uri;)"
                                     "Landroid/content/Intent;");
    midStart = (*env)->GetMethodID(env, (*env)->GetObjectClass(
                                       env, g_xsysMainActivity),
                                   "startActivity",
                                   "(Landroid/content/Intent;)V");
    if (!midCtor || !midSetData || !midStart) goto fail;
    jurl = (*env)->NewStringUTF(env, url);
    jaction = (*env)->NewStringUTF(env, "android.intent.action.VIEW");
    if (!jurl || !jaction) goto fail;
    uri = (*env)->CallStaticObjectMethod(env, uriCls, midParse, jurl);
    if ((*env)->ExceptionCheck(env) || !uri) goto fail;
    intent = (*env)->NewObject(env, intentCls, midCtor, jaction);
    if ((*env)->ExceptionCheck(env) || !intent) goto fail;
    intent = (*env)->CallObjectMethod(env, intent, midSetData, uri);
    if ((*env)->ExceptionCheck(env) || !intent) goto fail;
    (*env)->CallVoidMethod(env, g_xsysMainActivity, midStart, intent);
    if ((*env)->ExceptionCheck(env)) goto fail;
    return true;

fail:
    (*env)->ExceptionClear(env);
    return false;
}

/** @brief 查询系统暗色主题（Configuration.uiMode 的 NIGHT 位）。 */
bool XAndroid_queryDarkTheme(bool* outDark)
{
    JNIEnv* env;
    jobject resources;
    jobject configuration;
    jclass cfgCls;
    jfieldID fidUiMode;
    int uiMode;

    if (!outDark) return false;
    *outDark = false;
    env = xsys_androidEnv();
    if (!env || !g_xsysMainActivity) return false;
    resources = (*env)->CallObjectMethod(
        env, g_xsysMainActivity,
        (*env)->GetMethodID(env, (*env)->GetObjectClass(env,
                            g_xsysMainActivity),
                            "getResources", "()Landroid/content/res/Resources;"));
    if ((*env)->ExceptionCheck(env) || !resources) goto fail;
    configuration = (*env)->CallObjectMethod(
        env, resources,
        (*env)->GetMethodID(env, (*env)->GetObjectClass(env, resources),
                            "getConfiguration",
                            "()Landroid/content/res/Configuration;"));
    if ((*env)->ExceptionCheck(env) || !configuration) goto fail;
    cfgCls = (*env)->GetObjectClass(env, configuration);
    fidUiMode = (*env)->GetFieldID(env, cfgCls, "uiMode", "I");
    if (!fidUiMode) goto fail;
    uiMode = (*env)->GetIntField(env, configuration, fidUiMode);
    /* UI_MODE_NIGHT_MASK=0x30；UI_MODE_NIGHT_YES=0x20（android.content
       res.Configuration 常量，手写避免引头文件）。 */
    *outDark = (uiMode & 0x30) == 0x20;
    return true;

fail:
    (*env)->ExceptionClear(env);
    return false;
}

/* ==================== WiFi（JNI -> WifiManager） ==================== */
/* XAndroidWifiScanItem 布局见共享头 XAndroidJniChannels.h。 */

/** @brief 按(system service 名)取系统服务实例（剪贴板/WiFi 共用）。 */
static jobject xsys_systemService(JNIEnv* env, const char* name)
{
    jclass ctxCls;
    jmethodID midGetSvc;
    jstring svcName;
    jobject svc;
    if (!g_xsysMainActivity) return NULL;
    ctxCls = (*env)->FindClass(env, "android/content/Context");
    if (!ctxCls) goto fail;
    midGetSvc = (*env)->GetMethodID(env, ctxCls, "getSystemService",
                                    "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!midGetSvc) goto fail;
    svcName = (*env)->NewStringUTF(env, name);
    if (!svcName) goto fail;
    svc = (*env)->CallObjectMethod(env, g_xsysMainActivity, midGetSvc,
                                   svcName);
    if ((*env)->ExceptionCheck(env)) goto fail;
    return svc;
fail:
    (*env)->ExceptionClear(env);
    return NULL;
}

/** @brief 发起一次系统扫描（立即返回；true=已受理。系统节流时 false，
 *  结果仍可从系统缓存取——调用方不必因此判失败）。 */
bool XAndroid_wifiStartScan(void)
{
    JNIEnv* env;
    jobject mgr;
    jclass mgrCls;
    jmethodID midStart;

    env = xsys_androidEnv();
    if (!env) return false;
    mgr = xsys_systemService(env, "wifi");
    if (!mgr) return false;
    mgrCls = (*env)->GetObjectClass(env, mgr);
    midStart = (*env)->GetMethodID(env, mgrCls, "startScan", "()Z");
    if (!midStart) goto fail;
    {
        jboolean started = (*env)->CallBooleanMethod(env, mgr, midStart);
        if ((*env)->ExceptionCheck(env)) goto fail;
        return started != JNI_FALSE;
    }
fail:
    (*env)->ExceptionClear(env);
    return false;
}

/** @brief 取当前 Wi-Fi 的 DHCP 租约快照（WifiManager.getDhcpInfo）。
 *  @details 安卓应用域读不了 /proc/net（网关）也没有 /etc/resolv.conf
 *           （DNS 由 netd 按网络管理）——posix 网络后端的常规取数在
 *           安卓全空，网关/DNS/DHCP 模式恒缺。系统级权威源是
 *           WifiManager.getDhcpInfo()（DhcpInfo 公开 int 字段）。
 *           字段字节序：DhcpInfo 的 int 是**小端序**（LSB=第一八位组，
 *           与 Formatter.formatIpAddress 的解读一致），此处翻成
 *           MSB=第一八位组的主机序（XHostAddress_setAddressIPv4 口径，
 *           与 posix 后端 ntohl 后的值同构）。 */
bool XAndroid_netDhcpInfo(XAndroidDhcpInfo* outInfo)
{
    JNIEnv* env;
    jobject mgr;
    jclass mgrCls;
    jmethodID midGet;
    jobject info;
    jclass infoCls;
    jfieldID fid;

    if (!outInfo) return false;
    memset(outInfo, 0, sizeof(*outInfo));
    env = xsys_androidEnv();
    if (!env) return false;
    mgr = xsys_systemService(env, "wifi");
    if (!mgr) return false;
    mgrCls = (*env)->GetObjectClass(env, mgr);
    midGet = (*env)->GetMethodID(env, mgrCls, "getDhcpInfo",
                                 "()Landroid/net/DhcpInfo;");
    if (!midGet) goto fail;
    info = (*env)->CallObjectMethod(env, mgr, midGet);
    if ((*env)->ExceptionCheck(env) || !info) goto fail;
    infoCls = (*env)->GetObjectClass(env, info);
    if (!infoCls) goto fail;

    /* 小端 int（LSB=首八位组）→ MSB=首八位组的主机序。 */
#define XSYS_DHCP_FIELD(name, field)                                    \
    do {                                                                \
        jint v = 0;                                                     \
        fid = (*env)->GetFieldID(env, infoCls, name, "I");              \
        if (fid) {                                                      \
            v = (*env)->GetIntField(env, info, fid);                    \
            outInfo->field = (unsigned int)(((v & 0xFFu) << 24) |       \
                                            ((v & 0xFF00u) << 8) |      \
                                            ((v >> 8) & 0xFF00u) |      \
                                            ((v >> 24) & 0xFFu));       \
        }                                                               \
    } while (0)

    XSYS_DHCP_FIELD("ipAddress", ipAddress);
    XSYS_DHCP_FIELD("gateway", gateway);
    XSYS_DHCP_FIELD("netmask", netmask);
    XSYS_DHCP_FIELD("dns1", dns1);
    XSYS_DHCP_FIELD("dns2", dns2);
    XSYS_DHCP_FIELD("serverAddress", serverAddress);
#undef XSYS_DHCP_FIELD
    fid = (*env)->GetFieldID(env, infoCls, "leaseDuration", "I");
    if (fid)
        outInfo->leaseDuration = (*env)->GetIntField(env, info, fid);

    /* 无 DHCP 服务器地址 = 当前连接非 DHCP 取得（或未连接）：如实报无租约。 */
    return outInfo->serverAddress != 0;

fail:
    (*env)->ExceptionClear(env);
    return false;
}

/** @brief 找应用自身的 Java 类（经 Activity 的 ClassLoader；定义在本文件
 *         后段 WiFi 连接段）。 */
static jclass xsys_findAppClass(JNIEnv* env, const char* className);

/** @brief 经应用类加载器取 XNetAdapter 的静态方法 ID（失败 NULL）。 */
static jmethodID xsys_netAdapterMethod(JNIEnv* env, const char* name,
                                       const char* sig)
{
    jclass cls;
    jmethodID mid;
    if (!env) return NULL;
    cls = xsys_findAppClass(env, XANDROID_JAVA_CLASS("XNetAdapter"));
    if (!cls) return NULL;
    mid = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (!mid) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return mid;
}

/** @brief C 缓冲 ← jstring（截断安全；清异常）。 */
static void xsys_copyJString(JNIEnv* env, jstring s, char* out, int cap)
{
    const char* utf;
    if (out == NULL || cap <= 0) return;
    out[0] = '\0';
    if (!s) return;
    utf = (*env)->GetStringUTFChars(env, s, NULL);
    if (!utf) {
        (*env)->ExceptionClear(env);
        return;
    }
    strncpy(out, utf, (size_t)cap - 1u);
    out[cap - 1] = '\0';
    (*env)->ReleaseStringUTFChars(env, s, utf);
}

bool XAndroid_netLinkInfo(char* outV6Gateway, int v6GatewayCap,
                          char* outDns1, int dns1Cap,
                          char* outDns2, int dns2Cap,
                          char* outIfaceName, int ifaceNameCap)
{
    JNIEnv* env;
    jmethodID mid;
    jclass cls;
    jobjectArray arr;
    int filled = 0;

    if (outV6Gateway) outV6Gateway[0] = '\0';
    if (outDns1) outDns1[0] = '\0';
    if (outDns2) outDns2[0] = '\0';
    if (outIfaceName) outIfaceName[0] = '\0';
    env = xsys_androidEnv();
    if (!env) return false;
    cls = xsys_findAppClass(env, XANDROID_JAVA_CLASS("XNetAdapter"));
    if (!cls) return false;
    mid = (*env)->GetStaticMethodID(env, cls, "linkInfo",
                                    "(Landroid/content/Context;)"
                                    "[Ljava/lang/String;");
    if (!mid) {
        (*env)->ExceptionClear(env);
        return false;
    }
    arr = (jobjectArray)(*env)->CallStaticObjectMethod(env, cls, mid,
                                                       g_xsysMainActivity);
    if ((*env)->ExceptionCheck(env) || !arr) {
        (*env)->ExceptionClear(env);
        return false;
    }
    {
        jsize n = (*env)->GetArrayLength(env, arr);
        /* 槽位：[0]=v6gw [1]=dns1 [2]=dns2 [3]=iface */
        if (n > 0 && outV6Gateway) {
            jstring s = (jstring)(*env)->GetObjectArrayElement(env, arr, 0);
            xsys_copyJString(env, s, outV6Gateway, v6GatewayCap);
            if (s) { (*env)->DeleteLocalRef(env, s); ++filled; }
        }
        if (n > 1 && outDns1) {
            jstring s = (jstring)(*env)->GetObjectArrayElement(env, arr, 1);
            xsys_copyJString(env, s, outDns1, dns1Cap);
            if (s) { (*env)->DeleteLocalRef(env, s); ++filled; }
        }
        if (n > 2 && outDns2) {
            jstring s = (jstring)(*env)->GetObjectArrayElement(env, arr, 2);
            xsys_copyJString(env, s, outDns2, dns2Cap);
            if (s) { (*env)->DeleteLocalRef(env, s); ++filled; }
        }
        if (n > 3 && outIfaceName) {
            jstring s = (jstring)(*env)->GetObjectArrayElement(env, arr, 3);
            xsys_copyJString(env, s, outIfaceName, ifaceNameCap);
            if (s) (*env)->DeleteLocalRef(env, s);
        }
    }
    return true;
}

bool XAndroid_netSetDhcp(void)
{
    JNIEnv* env;
    jmethodID mid;
    jclass cls;
    jboolean ok;
    env = xsys_androidEnv();
    if (!env) return false;
    cls = xsys_findAppClass(env, XANDROID_JAVA_CLASS("XNetAdapter"));
    if (!cls) return false;
    mid = xsys_netAdapterMethod(env, "setDhcpMode",
                                "(Landroid/content/Context;)Z");
    if (!mid) return false;
    ok = (*env)->CallStaticBooleanMethod(env, cls, mid, g_xsysMainActivity);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return false;
    }
    return ok == JNI_TRUE;
}

bool XAndroid_netSetStaticV4(const char* ip, int prefixLength,
                             const char* gateway,
                             const char* dnsPrimary, const char* dnsSecondary)
{
    JNIEnv* env;
    jmethodID mid;
    jclass cls;
    jstring jIp, jGw, jD1, jD2;
    jboolean ok;
    if (!ip || !ip[0] || prefixLength <= 0) return false;
    env = xsys_androidEnv();
    if (!env) return false;
    cls = xsys_findAppClass(env, XANDROID_JAVA_CLASS("XNetAdapter"));
    if (!cls) return false;
    mid = xsys_netAdapterMethod(env, "setStaticV4",
        "(Landroid/content/Context;Ljava/lang/String;ILjava/lang/String;"
        "Ljava/lang/String;Ljava/lang/String;)Z");
    if (!mid) return false;
    jIp = (*env)->NewStringUTF(env, ip);
    jGw = (gateway && gateway[0]) ? (*env)->NewStringUTF(env, gateway) : NULL;
    jD1 = (dnsPrimary && dnsPrimary[0]) ? (*env)->NewStringUTF(env, dnsPrimary)
                                        : NULL;
    jD2 = (dnsSecondary && dnsSecondary[0])
              ? (*env)->NewStringUTF(env, dnsSecondary) : NULL;
    ok = (*env)->CallStaticBooleanMethod(env, cls, mid, g_xsysMainActivity,
                                         jIp, (jint)prefixLength, jGw, jD1, jD2);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ok = JNI_FALSE;
    }
    if (jIp) (*env)->DeleteLocalRef(env, jIp);
    if (jGw) (*env)->DeleteLocalRef(env, jGw);
    if (jD1) (*env)->DeleteLocalRef(env, jD1);
    if (jD2) (*env)->DeleteLocalRef(env, jD2);
    return ok == JNI_TRUE;
}

/** @brief 取系统当前扫描结果缓存（次数为返回值；items 容量 max）。 */
int XAndroid_wifiScanResults(XAndroidWifiScanItem* items, int max)
{
    JNIEnv* env;
    jobject mgr;
    jclass mgrCls;
    jmethodID midGet;
    jobject list;
    jclass listCls;
    jmethodID midSize;
    jmethodID midGetI;
    int count;
    int i;

    if (!items || max <= 0) return 0;
    env = xsys_androidEnv();
    if (!env) return 0;
    mgr = xsys_systemService(env, "wifi");
    if (!mgr) return 0;
    mgrCls = (*env)->GetObjectClass(env, mgr);
    midGet = (*env)->GetMethodID(env, mgrCls, "getScanResults",
                                 "()Ljava/util/List;");
    if (!midGet) goto fail;
    list = (*env)->CallObjectMethod(env, mgr, midGet);
    if ((*env)->ExceptionCheck(env) || !list) goto fail;
    listCls = (*env)->GetObjectClass(env, list);
    midSize = (*env)->GetMethodID(env, listCls, "size", "()I");
    midGetI = (*env)->GetMethodID(env, listCls, "get",
                                  "(I)Ljava/lang/Object;");
    if (!midSize || !midGetI) goto fail;
    count = (*env)->CallIntMethod(env, list, midSize);
    if (count > max) count = max;
    for (i = 0; i < count; ++i) {
        jobject result = (*env)->CallObjectMethod(env, list, midGetI, i);
        jclass resCls;
        jfieldID fid;
        jstring str;
        const char* utf;
        if ((*env)->ExceptionCheck(env) || !result) {
            (*env)->ExceptionClear(env);
            continue;
        }
        resCls = (*env)->GetObjectClass(env, result);
        memset(&items[i], 0, sizeof(items[i]));
        fid = (*env)->GetFieldID(env, resCls, "SSID",
                                 "Ljava/lang/String;");
        if (fid) {
            str = (*env)->GetObjectField(env, result, fid);
            if (str) {
                utf = (*env)->GetStringUTFChars(env, str, NULL);
                if (utf) {
                    strncpy(items[i].ssid, utf,
                            sizeof(items[i].ssid) - 1u);
                    (*env)->ReleaseStringUTFChars(env, str, utf);
                }
            }
        }
        fid = (*env)->GetFieldID(env, resCls, "BSSID",
                                 "Ljava/lang/String;");
        if (fid) {
            str = (*env)->GetObjectField(env, result, fid);
            if (str) {
                utf = (*env)->GetStringUTFChars(env, str, NULL);
                if (utf) {
                    strncpy(items[i].bssid, utf,
                            sizeof(items[i].bssid) - 1u);
                    (*env)->ReleaseStringUTFChars(env, str, utf);
                }
            }
        }
        fid = (*env)->GetFieldID(env, resCls, "capabilities",
                                 "Ljava/lang/String;");
        if (fid) {
            str = (*env)->GetObjectField(env, result, fid);
            if (str) {
                utf = (*env)->GetStringUTFChars(env, str, NULL);
                if (utf) {
                    strncpy(items[i].capabilities, utf,
                            sizeof(items[i].capabilities) - 1u);
                    (*env)->ReleaseStringUTFChars(env, str, utf);
                }
            }
        }
        fid = (*env)->GetFieldID(env, resCls, "level", "I");
        if (fid) items[i].level = (*env)->GetIntField(env, result, fid);
        fid = (*env)->GetFieldID(env, resCls, "frequency", "I");
        if (fid) items[i].frequency = (*env)->GetIntField(env, result, fid);
    }
    return count;

fail:
    (*env)->ExceptionClear(env);
    return 0;
}

/** @brief 查询当前连接（返回 true=已连接；字段拷入调用方缓冲）。 */
bool XAndroid_wifiConnection(char* outSsid, int ssidCap,
                             char* outBssid, int bssidCap, int* outRssi)
{
    JNIEnv* env;
    jobject mgr;
    jclass mgrCls;
    jmethodID midInfo;
    jobject info;
    jclass infoCls;
    jmethodID midSsid;
    jmethodID midBssid;
    jmethodID midRssi;
    jstring str;
    const char* utf;

    if (!outSsid || ssidCap <= 0) return false;
    outSsid[0] = '\0';
    if (outBssid && bssidCap > 0) outBssid[0] = '\0';
    if (outRssi) *outRssi = 0;
    env = xsys_androidEnv();
    if (!env) return false;
    mgr = xsys_systemService(env, "wifi");
    if (!mgr) return false;
    mgrCls = (*env)->GetObjectClass(env, mgr);
    midInfo = (*env)->GetMethodID(env, mgrCls, "getConnectionInfo",
                                  "()Landroid/net/wifi/WifiInfo;");
    if (!midInfo) goto fail;
    info = (*env)->CallObjectMethod(env, mgr, midInfo);
    if ((*env)->ExceptionCheck(env) || !info) goto fail;
    infoCls = (*env)->GetObjectClass(env, info);
    midSsid = (*env)->GetMethodID(env, infoCls, "getSSID",
                                  "()Ljava/lang/String;");
    if (!midSsid) goto fail;
    str = (*env)->CallObjectMethod(env, info, midSsid);
    if ((*env)->ExceptionCheck(env) || !str) goto fail;
    utf = (*env)->GetStringUTFChars(env, str, NULL);
    if (!utf) goto fail;
    /* getSSID 返回带引号形式（"MyNet"）或 "<unknown ssid>"：剥引号并判未知。 */
    if (strncmp(utf, "<unknown ssid>", 14) != 0) {
        size_t len = strlen(utf);
        size_t begin = (len >= 2 && utf[0] == '"') ? 1u : 0u;
        size_t end = (len >= begin + 2 && utf[len - 1] == '"')
                         ? len - 1u : len;
        if (end > begin) {
            size_t n = end - begin;
            if (n >= (size_t)ssidCap) n = (size_t)ssidCap - 1u;
            memcpy(outSsid, utf + begin, n);
            outSsid[n] = '\0';
        }
    }
    (*env)->ReleaseStringUTFChars(env, str, utf);
    if (outSsid[0] == '\0') return false; /* 未连接 */
    if (outBssid && bssidCap > 0) {
        midBssid = (*env)->GetMethodID(env, infoCls, "getBSSID",
                                       "()Ljava/lang/String;");
        if (midBssid) {
            str = (*env)->CallObjectMethod(env, info, midBssid);
            if (!(*env)->ExceptionCheck(env) && str) {
                utf = (*env)->GetStringUTFChars(env, str, NULL);
                if (utf) {
                    strncpy(outBssid, utf, (size_t)bssidCap - 1u);
                    outBssid[bssidCap - 1] = '\0';
                    (*env)->ReleaseStringUTFChars(env, str, utf);
                }
            } else {
                (*env)->ExceptionClear(env);
            }
        }
    }
    if (outRssi) {
        midRssi = (*env)->GetMethodID(env, infoCls, "getRssi", "()I");
        if (midRssi)
            *outRssi = (*env)->CallIntMethod(env, info, midRssi);
    }
    return true;

fail:
    (*env)->ExceptionClear(env);
    return false;
}

/* ==================== WiFi 连接/断开（XWifiConnector 桥） ================= */

/** @brief 找应用自身的 Java 类（经 Activity 的 ClassLoader）。
 *  @details **不能用 FindClass 直接找应用类**：本文件的所有调用都发生在
 *           **原生线程**（渲染线程/事件线程）上，JNI 的 FindClass 在这种
 *           上下文里用的是**系统 ClassLoader**，看不见应用自己的 dex
 *           → 必然 ClassNotFoundException（真机实证：startConnect 受理=0）。
 *           正解：取 Activity 的 Class → getClassLoader() → loadClass()。
 *           （JNI_OnLoad 里注册的类不受此限——那里有 Java 调用栈上下文。）
 */
static jclass xsys_findAppClass(JNIEnv* env, const char* className)
{
    jclass actCls;
    jclass loaderCls;
    jmethodID midGetLoader;
    jmethodID midLoadClass;
    jobject loader;
    jstring jName;
    jclass cls;
    if (!env || !g_xsysMainActivity || !className) return NULL;
    actCls = (*env)->GetObjectClass(env, g_xsysMainActivity);
    if (!actCls) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    loaderCls = (*env)->FindClass(env, "java/lang/ClassLoader");
    if (!loaderCls) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    midGetLoader = (*env)->GetMethodID(env, actCls, "getClassLoader",
                                       "()Ljava/lang/ClassLoader;");
    if (!midGetLoader) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    /* 注意：**在 Activity 实例上**调 getClassLoader()，不是在它的 Class
     * 对象上（后者是 java.lang.Class，没有该方法 → ART 直接 SIGABRT：
     * "can't call getClassLoader() on instance of java.lang.Class<...>"，
     * 真机实证）。 */
    loader = (*env)->CallObjectMethod(env, g_xsysMainActivity, midGetLoader);
    if ((*env)->ExceptionCheck(env) || !loader) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    midLoadClass = (*env)->GetMethodID(env, loaderCls, "loadClass",
                                       "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!midLoadClass) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jName = (*env)->NewStringUTF(env, className);
    if (!jName) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    cls = (jclass)(*env)->CallObjectMethod(env, loader, midLoadClass, jName);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        cls = NULL;
    }
    (*env)->DeleteLocalRef(env, jName);
    return cls;
}

/** @brief 取 XWifiConnector 类的静态方法 ID（失败返回 NULL）。 */
static jmethodID xsys_wifiConnectorMethod(JNIEnv* env, const char* name,
                                          const char* sig)
{
    jclass cls;
    jmethodID mid;
    if (!env) return NULL;
    cls = xsys_findAppClass(env, XANDROID_JAVA_CLASS("XWifiConnector"));
    if (!cls) return NULL;
    mid = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (!mid) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return mid;
}

bool XAndroid_wifiConnect(const char* ssid, const char* passphrase,
                          int security)
{
    JNIEnv* env;
    jmethodID mid;
    jclass cls;
    jstring jSsid;
    jstring jPass;
    jboolean ok;
    if (!ssid || !ssid[0]) return false;
    env = xsys_androidEnv();
    if (!env) return false;
    cls = xsys_findAppClass(env, XANDROID_JAVA_CLASS("XWifiConnector"));
    if (!cls) return false;
    mid = xsys_wifiConnectorMethod(env, "connect",
                                   "(Landroid/content/Context;Ljava/lang/String;"
                                   "Ljava/lang/String;I)Z");
    if (!mid) return false;
    jSsid = (*env)->NewStringUTF(env, ssid);
    jPass = (passphrase && passphrase[0]) ? (*env)->NewStringUTF(env, passphrase)
                                          : NULL;
    ok = (*env)->CallStaticBooleanMethod(env, cls, mid, g_xsysMainActivity,
                                         jSsid, jPass, (jint)security);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ok = JNI_FALSE;
    }
    if (jSsid) (*env)->DeleteLocalRef(env, jSsid);
    if (jPass) (*env)->DeleteLocalRef(env, jPass);
    return ok == JNI_TRUE;
}

bool XAndroid_wifiDisconnect(void)
{
    JNIEnv* env;
    jmethodID mid;
    jclass cls;
    jboolean ok;
    env = xsys_androidEnv();
    if (!env) return false;
    cls = xsys_findAppClass(env, XANDROID_JAVA_CLASS("XWifiConnector"));
    if (!cls) return false;
    mid = xsys_wifiConnectorMethod(env, "disconnect",
                                   "(Landroid/content/Context;)Z");
    if (!mid) return false;
    ok = (*env)->CallStaticBooleanMethod(env, cls, mid, g_xsysMainActivity);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ok = JNI_FALSE;
    }
    return ok == JNI_TRUE;
}

int XAndroid_wifiConnectState(void)
{
    JNIEnv* env;
    jmethodID mid;
    jclass cls;
    jint state = 0;
    env = xsys_androidEnv();
    if (!env) return 0;
    cls = xsys_findAppClass(env, XANDROID_JAVA_CLASS("XWifiConnector"));
    if (!cls) return 0;
    mid = xsys_wifiConnectorMethod(env, "state",
                                   "(Landroid/content/Context;)I");
    if (!mid) return 0;
    state = (*env)->CallStaticIntMethod(env, cls, mid, g_xsysMainActivity);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    return (int)state;
}

#endif /* __ANDROID__ */
