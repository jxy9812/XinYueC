/******************************************************************************
 * @file       android_main.c
 * @brief      XGuiWindowDemo 的 NativeActivity 入口（安卓 APK 壳）。
 * @details    NativeActivity 线程模型：
 *             - UI 线程：ANativeActivity 回调（生命周期/surface/输入），
 *               全部转投 Drive/Android 平台层（XPad_* 接线函数）。
 *             - 渲染线程：android_main 启动，跑 demo_main()（即
 *               XGuiWindowDemo 的 main 逻辑，XGuiApplication_exec 事件
 *               循环常驻于此）；事件泵经 poll 回调消费平台层 pending。
 *             退出协议：demo main 返回（窗口全关/quit）→ 渲染线程置
 *             g_padAppQuit 并 ANativeActivity_finish() 结束 Activity。
 * @note       本文件仅服务于安卓 APK 壳目标 XGuiWindowDemo_App；
 *             桌面/嵌入式构建不参与（CMake 安卓分支显式列源）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if defined(__ANDROID__)

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 平台层接线（Drive/Android/Graphics/XPlatformWindowAndroid.c）。 */
void XPad_onNativeWindowCreated(ANativeWindow* window);
void XPad_onNativeWindowResized(ANativeWindow* window);
void XPad_onNativeWindowRedrawNeeded(ANativeWindow* window);
void XPad_onNativeWindowDestroyed(void);
void XPad_onInputEvent(AInputEvent* event);
void XPad_setRunning(bool running);
void XPad_setDestroyed(void);
void XPad_resetDestroyed(void);
void XPad_setActivity(ANativeActivity* activity);

/* 平台采样后端（Drive/Android/Core/XSystemAndroid.c）：注入 JavaVM 供
   TrafficStats JNI 采样；注入 Activity 引用供窗口尺寸 JNI 查询。 */
void XAndroid_setJavaVm(JavaVM* vm);
void XAndroid_setMainActivity(jobject activity);

/* XGuiWindowDemo 主逻辑（Test/XGuiDemo，桌面 main 的同一实现）。 */
extern int xgui_window_demo_main(int argc, char* argv[]);

#define XPAD_LOG(...) ((void)__android_log_print(ANDROID_LOG_INFO, "XGuiDemo", __VA_ARGS__))

/* demo 侧诊断通道（xgui_window_demo.c 验证用）：stdout 不可见，转 logcat。 */
void XGuiDemo_debugLog(const char* text)
{
    XPAD_LOG("%s", text);
}

static pthread_t g_padThread;
static volatile bool g_padThreadStarted = false;
static volatile bool g_padQuitRequested = false;
ANativeActivity* g_padActivityRef = NULL;   /* 渲染线程 finish 用 */
static ALooper* g_padLooper = NULL;
static AInputQueue* g_padQueue = NULL;

/** @brief 渲染线程：跑 demo 主逻辑；返回即请求结束 Activity。
 *  支持 XGUI_DEMO_ARGS 环境变量拆分传参（如 "--screenshot /data/local/tmp/x.png"）。 */
static void* xpad_demoThread(void* arg)
{
    char argv0[] = "XGuiWindowDemo";
    char* argv[8] = { argv0 };
    int argc = 1;
    char argsBuf[256] = {0};
    char* tok;
    const char* envArgs;
    int rc;
    FILE* probe;
    (void)arg;
    /* 采样源探测：SELinux 对 untrusted_app 可能屏蔽 /proc/stat 与
       /proc/net/dev（shell 域可读不代表 app 域可读）。一次性打印结果。 */
    probe = fopen("/proc/stat", "r");
    XPAD_LOG("probe /proc/stat = %s", probe ? "OK" : "DENIED/FAIL");
    if (probe) {
        char line[128];
        if (fgets(line, sizeof(line), probe))
            XPAD_LOG("proc/stat: %.40s", line);
        fclose(probe);
    }
    probe = fopen("/proc/self/stat", "r");
    XPAD_LOG("probe /proc/self/stat = %s", probe ? "OK" : "DENIED/FAIL");
    if (probe) fclose(probe);
    probe = fopen("/proc/self/net/dev", "r");
    XPAD_LOG("probe /proc/self/net/dev = %s", probe ? "OK" : "DENIED/FAIL");
    if (probe) fclose(probe);
    probe = fopen("/proc/pressure/cpu", "r");
    XPAD_LOG("probe /proc/pressure/cpu = %s", probe ? "OK" : "DENIED/FAIL");
    if (probe) {
        char line2[160];
        while (fgets(line2, sizeof(line2), probe))
            XPAD_LOG("psi: %.60s", line2);
        fclose(probe);
    }
    probe = fopen("/proc/net/dev", "r");
    XPAD_LOG("probe /proc/net/dev = %s", probe ? "OK" : "DENIED/FAIL");
    if (probe) fclose(probe);
    probe = fopen("/proc/meminfo", "r");
    XPAD_LOG("probe /proc/meminfo = %s", probe ? "OK" : "DENIED/FAIL");
    if (probe) fclose(probe);
    envArgs = getenv("XGUI_DEMO_ARGS");
    if (!envArgs || strlen(envArgs) == 0) {
        /* APK 壳兜底：读 /data/local/tmp/xgui_args.txt（adb 可写），
           内容为空格分隔的 demo 参数，便于无人值守验证。 */
        FILE* f = fopen("/data/local/tmp/xgui_args.txt", "rb");
        if (f) {
            size_t n = fread(argsBuf, 1, sizeof(argsBuf) - 1, f);
            fclose(f);
            if (n > 0) {
                argsBuf[n] = 0;
                /* 去掉结尾换行 */
                while (n > 0 && (argsBuf[n - 1] == '\n' || argsBuf[n - 1] == '\r'))
                    argsBuf[--n] = 0;
                envArgs = argsBuf;
            }
        }
    }
    if (envArgs && strlen(envArgs) > 0 && strlen(envArgs) < sizeof(argsBuf)) {
        strcpy(argsBuf, envArgs);
        tok = strtok(argsBuf, " ");
        while (tok && argc < 8) { argv[argc++] = tok; tok = strtok(NULL, " "); }
    }
    XPAD_LOG("demo thread: enter xgui_window_demo_main argc=%d", argc);
    rc = xgui_window_demo_main(argc, argv);
    XPAD_LOG("demo thread: main returned rc=%d, exiting process", rc);
    g_padQuitRequested = true;
    g_padThreadStarted = false; /* 允许后续新 Activity 实例重建线程 */
    /* demo 退出 -> 完全退出程序：先 ANativeActivity_finish 让系统正常
       收尾 Activity（可任意线程调），随后 exit(0) 终止进程。仅 finish
       不够——hasCode=false 的 NativeActivity 无 Java 组件持有进程，
       但 ART 运行时线程（Binder/JIT/Finalizer）仍驻留，实测关闭按钮
       后进程存活（"关窗不退程序"）；桌面语义 close=quit=进程结束，
       安卓对齐之（用户裁定 2026-09-30：关闭按钮完全退出）。 */
    if (g_padActivityRef)
        ANativeActivity_finish(g_padActivityRef);
    /* 给系统一个调度窗口处理 finish（ActivityTaskManager 记录任务移除），
       随后无条件终止。1ms 足够 AMS 写任务栈记录，不追求优雅存活。 */
    struct timespec ts = { 0, 1000 * 1000 }; /* 1ms */
    nanosleep(&ts, NULL);
    exit(rc == 0 ? 0 : 1);
    return NULL; /* 不可达 */
}

/* ==================== ANativeActivityCallbacks（UI 线程） ==================== */

static void xpad_onStart(ANativeActivity* activity)
{ (void)activity; XPad_setRunning(true); }

static void xpad_onResume(ANativeActivity* activity)
{ (void)activity; XPad_setRunning(true); }

static void* xpad_onSaveInstanceState(ANativeActivity* activity, size_t* outLen)
{ (void)activity; if (outLen) *outLen = 0; return NULL; }

static void xpad_onPause(ANativeActivity* activity)
{ (void)activity; XPad_setRunning(false); }

static void xpad_onStop(ANativeActivity* activity)
{ (void)activity; XPad_setRunning(false); }

static void xpad_onDestroy(ANativeActivity* activity)
{
    (void)activity;
    XPAD_LOG("onDestroy: activity destroyed");
    XPad_setDestroyed();
    XPad_setRunning(false);
}

static void xpad_onNativeWindowCreated(ANativeActivity* activity,
                                       ANativeWindow* window)
{
    (void)activity;
    XPad_onNativeWindowCreated(window);
}

static void xpad_onNativeWindowResized(ANativeActivity* activity,
                                       ANativeWindow* window)
{
    (void)activity;
    XPad_onNativeWindowResized(window);
}

static void xpad_onNativeWindowRedrawNeeded(ANativeActivity* activity,
                                            ANativeWindow* window)
{
    (void)activity;
    XPad_onNativeWindowRedrawNeeded(window);
}

static void xpad_onNativeWindowDestroyed(ANativeActivity* activity,
                                         ANativeWindow* window)
{
    (void)activity; (void)window;
    XPad_onNativeWindowDestroyed();
}

static void xpad_onWindowFocusChanged(ANativeActivity* activity, int focused)
{ (void)activity; (void)focused; }

static void xpad_onConfigurationChanged(ANativeActivity* activity)
{ (void)activity; }

static void xpad_onLowMemory(ANativeActivity* activity)
{ (void)activity; }

/** @brief UI 线程输入泵：AInputQueue 逐事件取出并转投平台层。 */
static int xpad_inputCallback(int fd, int events, void* data)
{
    AInputEvent* event = NULL;
    (void)fd; (void)events; (void)data;
    if (!g_padQueue) return 1;
    while (AInputQueue_hasEvents(g_padQueue) > 0) {
        while (AInputQueue_getEvent(g_padQueue, &event) >= 0 &&
               event != NULL) {
            int32_t handled = 1; /* 全部消费（单点触摸映射左键） */
            XPAD_LOG("input: type=%d", AInputEvent_getType(event));
            XPad_onInputEvent(event);
            AInputQueue_finishEvent(g_padQueue, event, handled);
            event = NULL;
            if (!g_padQueue) break;
        }
        if (!g_padQueue) break;
    }
    return 1; /* 继续接收回调 */
}

/* 输入队列生命周期（onCreate 时通常尚未创建，经回调接管）。 */
static void xpad_onInputQueueCreated(ANativeActivity* activity,
                                     AInputQueue* queue)
{
    (void)activity;
    XPAD_LOG("inputQueue created: queue=%p", (void*)queue);
    g_padQueue = queue;
    g_padLooper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    AInputQueue_attachLooper(queue, g_padLooper, 1,
                             xpad_inputCallback, NULL);
    XPAD_LOG("inputQueue attached to looper %p", (void*)g_padLooper);
}

static void xpad_onInputQueueDestroyed(ANativeActivity* activity,
                                       AInputQueue* queue)
{
    (void)activity;
    AInputQueue_detachLooper(queue);
    if (g_padQueue == queue) g_padQueue = NULL;
}

/* ==================== Activity 入口 ==================== */

/** @brief 回调装配（onCreate 与 Activity 重建共用）。 */
static void InstallCallbacks(ANativeActivity* activity)
{
    activity->callbacks->onStart               = xpad_onStart;
    activity->callbacks->onResume              = xpad_onResume;
    activity->callbacks->onSaveInstanceState   = xpad_onSaveInstanceState;
    activity->callbacks->onPause               = xpad_onPause;
    activity->callbacks->onStop                = xpad_onStop;
    activity->callbacks->onDestroy             = xpad_onDestroy;
    activity->callbacks->onNativeWindowCreated = xpad_onNativeWindowCreated;
    activity->callbacks->onNativeWindowResized = xpad_onNativeWindowResized;
    activity->callbacks->onNativeWindowRedrawNeeded = xpad_onNativeWindowRedrawNeeded;
    activity->callbacks->onNativeWindowDestroyed = xpad_onNativeWindowDestroyed;
    activity->callbacks->onWindowFocusChanged  = xpad_onWindowFocusChanged;
    activity->callbacks->onConfigurationChanged = xpad_onConfigurationChanged;
    activity->callbacks->onLowMemory           = xpad_onLowMemory;
    activity->callbacks->onInputQueueCreated   = xpad_onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = xpad_onInputQueueDestroyed;
}

void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState,
                              size_t savedStateSize)
{
    (void)savedState; (void)savedStateSize;
    XPAD_LOG("ANativeActivity_onCreate");

    /* Activity 重建（配置变化如密度/尺寸切换，系统销毁旧实例并在同一
       进程创建新实例）时渲染线程仍在运行：demo 单例状态（XGuiApplication
       /dispatcher）不可重建，直接复用现存线程，仅更新实例引用与回调。
       若二次起线程会在 XThreadData 上叠加第二个"主线程"exec（rc=-1）
       并与旧线程竞态析构 dispatcher（SIGSEGV 实测）。 */
    if (g_padThreadStarted && !g_padQuitRequested) {
        XPAD_LOG("onCreate: reuse existing demo thread");
        g_padActivityRef = activity;
        if (activity->vm)
            XAndroid_setJavaVm(activity->vm);
        XAndroid_setMainActivity(activity->clazz);
        XPad_resetDestroyed();
        XPad_setActivity(activity);
        InstallCallbacks(activity);
        return;
    }

    g_padActivityRef = activity;
    if (activity->vm)
        XAndroid_setJavaVm(activity->vm);
    XAndroid_setMainActivity(activity->clazz);
    XPad_setActivity(activity);

    InstallCallbacks(activity);

    /* 渲染线程启动（demo 事件循环常驻于此）。 */
    if (pthread_create(&g_padThread, NULL, xpad_demoThread, NULL) == 0) {
        g_padThreadStarted = true;
    }
    else {
        XPAD_LOG("FATAL: demo thread create failed");
        ANativeActivity_finish(activity);
    }
}

/* 渲染线程退出后收口：demo main 返回即 finish Activity 并 exit 终止
   进程（见 xpad_demoThread 尾部——本壳无 glue 库，无独立 native 主
   线程；ANativeActivity_onCreate 返回后生命周期全由 Java 侧驱动）。 */

#endif /* __ANDROID__ */
