/******************************************************************************
 * @file       XPlatformWindowAndroid.c
 * @brief      XGui 安卓平台层：NativeActivity 单窗直写 + 触摸输入注入。
 * @details    模型对齐嵌入式 fbdev 直写路径（XPlatformFramebuffer_posix
 *             的单屏注册表模型），差异仅在"帧缓冲"换成了
 *             ANativeWindow（NativeActivity 的应用窗口 surface）：
 *             - 显示：经 XPlatformDisplayDriverOps 注册表向框架报告
 *               surface 尺寸/格式/行距；present 由 ANativeWindow_lock
 *               直写 + unlock 提交（软件渲染路径，对标 linuxfb memcpy）。
 *               GPU（EGL/Vulkan）路径待后续批次接入。
 *             - 输入：NativeActivity 主线程回调（AInputQueue）抽整帧
 *               真触摸（多点 ≤8：逐点 id/raw 坐标 + 事件时间戳）入队，
 *               渲染线程经 XWindowSystemInterface_handleTouchPoints_ex
 *               注入 —— 映射为 TOUCH BEGIN/UPDATE/END/CANCEL 四态多点
 *               （对标 QTouchEvent 的 QEventPoint 列表；笔悬停
 *               HOVER_MOVE 仍走单点鼠标 MOVE，双击由框架层手势判定）。
 *             - 生命周期：ANativeActivityCallbacks onResume/onPause/
 *               onDestroy 与 surface Created/Changed/Destroyed 全部落到
 *               本文件的静态状态机；XGui 侧零感知。
 *             线程模型（NativeActivity 官方约定）：生命周期回调全部在
 *             ANativeActivity 的主 UI 线程；渲染/事件泵在独立的
 *             android_app->thread。XGui exec 跑在渲染线程，通过
 *             xpad_waitForEvents 阻塞在条件变量上；UI 线程投递事件后
 *             signal 唤醒。与 X11 的 "poll fd + processPendingEvents"
 *             同构：这里的事件源是 android_app 消息队列。
 * @note       仅在 __ANDROID__ && XGUI_ON && XPLATFORMINTEGRATION_ON 下
 *             编译出内容；其他平台整文件裁剪为空（与 fbdev 同款守卫）。
 *             平台 API（android/native_activity.h 等）只允许出现在
 *             Drive 目录——本文件是安卓平台 API 的唯一入口。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformNativeWindow.h"

#if defined(__ANDROID__) && XGUI_ON

#include "XPlatformDisplayDriver.h"
#include "XPlatformScreen.h"
#include "XWindowSystemInterface.h"
#include "XWindow.h"
#include "XScreen.h"
#include "XGuiApplication.h"
#include "XImage.h"
#include "XImageFormat.h"
#include "XGeometry.h"
#include "XString.h"
#include "XPrintf.h"
#include "XRingBuffer.h"
#include "XWindowRotation.h" /* 安卓内容旋转换算（与 posix/win32 同源口径）。 */
#include "XCursor.h" /* 软件光标精灵：触摸喂位 + present 合成后落笔。 */

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/looper.h>
#include <android/log.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

#define XPAD_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "XPad", __VA_ARGS__))

/* 由 android_main.c / Drive/Android/Core/XSystemAndroid.c 提供（最小化
   moveTaskToBack；密度/原点/窗口尺寸查询已改走
   Src/XGui/Platform/XPlatformScreen.h 统一契约）；前置声明——setWindowState
   的实现位置在使用点之前，C99 隐式声明会把 bool 返回值截断成 int 引发冲突。 */
void XAndroid_setMainActivity(jobject activity);
bool XAndroid_moveTaskToBack(void);

/** @brief 单调毫秒（CLOCK_MONOTONIC）。帧界钳等节奏判定专用：不受
 *         wall-clock 跳变影响（pthread 等待用的 CLOCK_REALTIME 各管
 *         各的，互不替代）。 */
static int64_t xpad_monoMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ==================== 编译期配置 ==================== */

#ifndef XPLATFORM_ANDROID_ON
#define XPLATFORM_ANDROID_ON 1
#endif

/* [跨圈帧界钳 2026-10-27] 纯 MOVE 帧注入的最小间隔（毫秒；0=关闭）。
 * 触摸报点 120~240Hz 远超重绘需求：同圈折叠只保证「一圈一次」，而事件
 * 循环圈频率=报点率，每条放行 MOVE 都会驱动一次控件布局/失效链。本宏
 * 把注入频率再钳到时间界（与 fbdev XPF_MOVE_FRAME_MS 同款纪律；拖拽
 * 语义按绝对坐标无状态重算，被钳帧的位移由放行帧一次落位，零丢失；
 * 手指停住无新报点时位置最多滞后一个帧界）。按下/抬起不受此门。 */
#ifndef XGUI_MOVE_FRAME_MS
#define XGUI_MOVE_FRAME_MS 25
#endif

#if XPLATFORMINTEGRATION_ON && XPLATFORM_ANDROID_ON

/* ==================== 模块状态（单屏单窗模型） ==================== */

/** @brief 单触点快照（帧内一员：触点 id + 窗口本地逻辑坐标）。 */
typedef struct XPadTouchPoint
{
    int32_t id;   /**< AMotionEvent_getPointerId（跨线程稳定标识）。 */
    int32_t x;    /**< 逻辑化后窗口本地坐标（§6.1，见 onInputEvent）。 */
    int32_t y;
} XPadTouchPoint;

/** 单帧触点上限（Android 多指常见 ≤5，8 留裕量；超出截断）。 */
#define XPAD_MAX_TOUCH_POINTS 8

/** @brief 待注入触摸帧（完整多点快照：动作 + 全部触点 + 时间戳）。
 *  @note  Android 语义：每个 motion 事件自带当前全部触点（POINTER_UP
 *         的离屏点仍在列表内），帧即事件原样快照，平台侧无需跨帧重建
 *         触点表。action 存未掩码原值：低 8 位动作码 + 高 8 位
 *         ACTION_POINTER_INDEX（POINTER_DOWN/UP 取变化点下标要用）。 */
typedef struct XPadTouchEvent
{
    int32_t action;                      /**< 未掩码动作值。 */
    int32_t count;                       /**< 触点数（<=XPAD_MAX_TOUCH_POINTS）。 */
    XPadTouchPoint pts[XPAD_MAX_TOUCH_POINTS]; /**< 逐点快照。 */
    uint32_t timestampMs;                /**< AMotionEvent_getEventTime（ns→ms）。 */
} XPadTouchEvent;

/** @brief 待注入键盘帧（翻译已完成，渲染线程直投 WSI）。
 *  @details [2026-10-24 安卓键盘接入] AKEY 事件此前被 onInputEvent 的
 *           MOTION 早退整类丢弃（物理键盘/adb input text 打字全无反应的
 *           根因）。翻译在 UI 线程就地完成后入队（值拷贝，跨线程安全）；
 *           key 用 X11 同款「字母大写归一」口径（0x41-0x5A），大小写由
 *           Shift 修饰位表达——XLineControl 非合成路径（扫描码/时间戳
 *           非零）据此派生文本。 */
typedef struct XPadKeyEvent
{
    int32_t type;          /**< XEVENT_TYPE_KEY_PRESS / KEY_RELEASE。 */
    int32_t key;           /**< XKey 码位（ASCII 恒等 + XKey_* 专码）。 */
    int32_t modifiers;     /**< XKeyboardModifiers 位掩码。 */
    int32_t scanCode;      /**< AKeyEvent_getScanCode（未知为 0）。 */
    uint32_t timestampMs;  /**< AKeyEvent_getEventTime（ns→ms，恒非 0）。 */
    bool autoRepeat;       /**< DOWN 重复（ACTION_MULTIPLE 归并入此）。 */
} XPadKeyEvent;

/** @brief 坐标空间标定分支（设计 §4.3 探针：不预设 origin/raw 的测量
 *  口径——Android 文档称物理 px，但 WSA/模拟器实测历史分叉，运行期用
 *  getWindowFrameSize 与 ANativeWindow 尺寸比对二选一，结果打日志）。 */
typedef enum XPadViewSpace
{
    XPAD_VIEW_SPACE_PHYSICAL = 0,  /**< origin/raw 均物理 px：evt=(raw−origin)/dpr */
    XPAD_VIEW_SPACE_LOGICAL  = 1   /**< origin 为逻辑 px：evt=raw/dpr−origin */
} XPadViewSpace;

/** @brief 生命周期/surface 状态机（UI 线程写、渲染线程读，mutex 保护）。 */
typedef struct XPadState
{
    ANativeActivity* m_activity;      /**< NativeActivity 句柄。 */
    ANativeWindow* m_window;          /**< 当前 surface；NULL=无表面。 */
    pthread_mutex_t m_mutex;          /**< 保护以下全部状态。 */
    pthread_cond_t m_cond;            /**< 渲染线程等待事件泵的睡点。 */
    bool m_running;                   /**< APP_CMD_START..STOP 之间为真。 */
    bool m_destroyed;                 /**< onDestroy 已到，请求退出。 */
    bool m_focus;                     /**< 有窗口焦点。 */
    bool m_surfaceDirty;              /**< surface 新建/变尺寸，待重绘。 */

    /* 触摸事件队列：库内 XRingBuffer（字节环形缓冲，动态扩容），以
       XPadTouchEvent 整帧为单位读写。input tap 的 DOWN/UP 同毫秒到达，
       单槽互相覆盖丢事件（实测 RELEASE 被 DOWN 覆盖，工具按钮的
       释放弹菜单永不触发），故必须排队；队列懒初始化。
       按压态不再跨事件增量维护（旧 m_pointerDown 已撤销）：每帧自带
       完整触点列表，按压与否按帧 count>0 直读；双击亦改由框架层手势
       状态机统一判定（平台合成 DBL_CLICK 已撤销）。 */
    XRingBuffer m_touchQueue;         /**< 事件队列。 */
    XRingBuffer m_keyQueue;           /**< 键盘事件队列（XPadKeyEvent 整帧）。 */
    bool m_queueReady;                /**< 队列已初始化。 */
    int m_keyModifiers;               /**< 当前修饰键位（键盘帧维护；queryKeyboardModifiers 口径）。 */

    /* MOVE 帧逐点状态缓存（对标 Win32 WM_POINTER 修复）：Android 的
       MOVE 动作不指明变化触点（Win32 每条消息自带变化 id），全帧
       UPDATED 会让静止伴指逐帧重投到抓取靶、架空控件层的 STATIONARY
       过滤。按触点 id 记上一注入帧坐标：MOVE 帧内坐标与缓存相同者标
       STATIONARY、变化者标 UPDATED；DOWN/POINTER_DOWN/POINTER_UP/UP/
       CANCEL 的状态映射不变，缓存随 CANCEL/全部抬起（UP）清空、其余
       动作整体替换为当前帧快照（新 DOWN 全量重建，残 id 不跨序列
       残留）。仅注入侧（事件线程）在 m_mutex 锁内读写。 */
    XPadTouchPoint m_lastPts[XPAD_MAX_TOUCH_POINTS]; /**< 按 id 的上次坐标。 */
    int m_lastCount;                  /**< 上帧快照点数（0=无可比缓存）。 */

    /* ---- DPI/坐标空间标定（设计 §1/§4.3/§5/§6）：UI 线程（生命周期
       回调）写、框架事件线程读，一律持 m_mutex。首帧密度在
       onNativeWindowCreated 内同步采样（纯 JNI 读数不属 §1.4 信号面
       收口，可直接在 UI 线程调），探针 JNI 采样在锁外完成、结果锁内
       发布（H3/H4）。 ---- */
    float m_dpr;                      /**< 密度快照（DisplayMetrics.density；失败兜底 1.0）。 */
    int m_densityDpi;                 /**< DisplayMetrics.densityDpi（诊断日志用）。 */
    float m_xdpi;                     /**< 设备物理 DPI x（physicalSize 毫米反推用，§1.5）。 */
    float m_ydpi;                     /**< 设备物理 DPI y。 */
    bool m_densityValid;              /**< 密度 JNI 查询成功（xdpi/ydpi 才是设备真值；
                                          失败时 160 兜底是假值，禁入 mm 上报链——
                                          契约「0=未知，严禁编造」的守卫位）。 */
    XPadViewSpace m_viewSpace;        /**< 标定分支（探针结果，§4.3；Resized/Redraw 复用不重标）。 */
    float m_presentScale;             /**< 实测放大系数=surfW_actual/logicalW（§5，不盲取 dpr）。 */
    int m_logicalW;                   /**< surface 的框架逻辑口径宽（标定分支推导）。 */
    int m_logicalH;                   /**< surface 的框架逻辑口径高。 */
    bool m_screenPending;             /**< 事件线程待消费：屏幕登记/密度回填（§1.3/§1.4）。 */
    /* ---- 全屏逻辑几何回推（2026-10-25 手机竖横屏适配）----
     * 安卓没有窗口管理器：surface 就是全部画布，框架窗口的逻辑尺寸
     * 必须**跟随 surface**，否则框架仍按启动时请求的尺寸（如 demo 的
     * 1000x700）排布，present 再按 presentScale 放大进 surface——放大
     * 后超出画布的部分被裁掉（真机实证：手机竖屏下 demo 右侧面板整片
     * 裁切、上下留黑边）。旋转只换 surface 尺寸，若不回推几何，框架
     * 连 RESIZE 事件都收不到（安卓侧不产几何事件），布局无从重排。
     * 口径：几何=surface 的逻辑尺寸（logicalW/H），坐标原点 (0,0)，
     * 与 presentScale 自洽（logicalW*presentScale==surfW，1:1 铺满）。
     * 状态栏让位由 demo 的根布局上边距承担（demo_sysbarH）。 */
    bool m_geomPending;               /**< 事件线程待消费：全屏几何回推。 */
    bool m_startupFullscreenPushed;   /**< 启动全屏态已对齐（一次性）。 */
    int m_geomW;                      /**< 待回推的框架逻辑宽。 */
    int m_geomH;                      /**< 待回推的框架逻辑高。 */
    /* [跨圈帧界钳 2026-10-27] 纯 MOVE 帧的处理节流基准（单调毫秒，
     * 0=从未注入）。同圈 MOVE 折叠只保证「一圈一次」，但事件循环圈
     * 频率远高于人眼需求：真机实测触摸移动时 FPS 100~200、CPU 满核。
     * 故再加一层时间门把注入频率钳到 XGUI_MOVE_FRAME_MS（与 fbdev
     * XPF_MOVE_FRAME_MS 同款纪律）。被钳帧的位移由放行帧按绝对坐标
     * 一次落位（拖拽语义无状态重算，零丢失）；按下/抬起不受此门限制。 */
    int64_t m_lastMoveCommitMs;       /**< 上次纯 MOVE 帧注入的单调毫秒。 */
    int m_moveFrameMs;                /**< 帧界窗口（毫秒；0=关闭钳制）。 */
} XPadState;

static XPadState g_xpad = {
    NULL, NULL,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    false, false, false, false,
    { 0 }, { 0 }, false, 0,
    { { 0 } }, 0,
    /* DPI/标定默认：dpr=1、PHYSICAL、scale=1 —— 全部安全退化为现网
       dpr=1 行为（JNI 读数失败时的兜底链，设计 §10-13）。 */
    1.0f, 160, 160.0f, 160.0f, false,
    XPAD_VIEW_SPACE_PHYSICAL, 1.0f, 0, 0, false,
    /* 全屏几何回推默认：无待推几何；启动全屏态尚未对齐。
       帧界钳默认：从未注入、窗口取缺省 25ms（见 XGUI_MOVE_FRAME_MS）。 */
    false, false, 0, 0, 0, 25
};

/* 显示驱动注册句柄与几何缓存。 */
static bool g_xpadRegistered = false;
static XWindow* g_xpadPrimaryWindow = NULL;   /**< 状态控制作用的主窗。 */
static int g_xpadWidth = 0;
static int g_xpadHeight = 0;
/* 多顶层登记表：demo 会建不止一个顶层窗（主窗+菜单/弹层等 Popup）。
   弹层在单 surface 模型下按"图层"合成：记录每层的窗口位置（框架
   setGeometry 下发）与最后提交的图像引用，present 时按登记顺序
   画家算法叠加（主窗先、弹层后）。 */
#define XPAD_MAX_WINDOWS 8
typedef struct XPadLayer
{
    XWindow* win;              /**< 登记的图层窗口。 */
    XPoint pos;                /**< 窗口位置（setGeometry 下发，主窗 (0,0)）。 */
    XPoint devPos;             /**< 设备轴原点（表面逻辑坐标）：旋转态弹层按
                                   opener 正变换换算，0° 时恒等于 pos。 */
    bool visible;              /**< setVisible 状态。 */
    const XImage* lastImage;   /**< 最近一次 present 的图像（借用，弹层叠加用）。 */
} XPadLayer;
static XPadLayer g_xpadWindows[XPAD_MAX_WINDOWS];
static XPadLayer* xpad_findLayer(XWindow* window);
static int g_xpadWindowCount = 0;

/* ---- 目的地缓冲欠账表（脏区直递 2026-10-27，对齐 Qt 上屏语义）----
 * ANativeWindow 的 BufferQueue 持 2~3 块物理缓冲轮转，lock 取到哪块
 * 内容无保证（可能是新分配，也可能是两帧前的旧块）——这正是旧实现
 * 「每帧整屏清黑+全图层重画」的原因：每帧 O(全屏) 清屏+逐像素缩放
 * blit，拖拽时 CPU 打满而帧率上不去（Qt 软件栈语义=脏区直递，绝不全
 * 屏重画；其安卓端则连软件提交都没有——widget 走 QRhiBackingStore 上
 * 传纹理由 GPU 合成）。
 * 改法：给每块目的缓冲记「欠账」（其后所有帧的物理脏区都累进所有块
 * 的账）；lock 到哪块就从各图层 lastImage 把它的欠账裁剪补画再清账。
 * 陌生指针（新缓冲/surface 重建）=欠全屏，走一次全量重画兜底。源
 * （各图层 lastImage）恒为最新完整内容，欠账无论跨几帧都能一次补对；
 * 身份判错的最坏结果只是回落全量（多画，不会画错）。 */
#define XPAD_MAX_BUFFERS 4
typedef struct XPadBufferLedger
{
    uint8_t* bits;    /**< 缓冲身份（lock 返回 bits 指针）；NULL=空槽。 */
    XRegion pending;  /**< 待补区（物理 surface 坐标；capacity 0 起步由
                           XRegion_addRect 懒扩容）。 */
    bool full;        /**< 欠全屏（首次见到/源变化范围未知时置位）。 */
} XPadBufferLedger;
static XPadBufferLedger g_xpadBufLedger[XPAD_MAX_BUFFERS];
static int g_xpadBufLedgerVictim = 0;   /**< 表满时的逐出游标（轮转）。 */
/* 光标精灵带（物理坐标）：精灵画在 surface 上、不在任何图层 lastImage
 * 里，隐没/移位时旧带必须补画还原——每帧把「上帧带∪本帧带」记入全部
 * 欠账即可（同 fbdev 差带账本的 purpose，账本载体不同）。surface 重建
 * 时随欠账表一并失效。 */
static XRect g_xpadLastCursorBand;
static bool g_xpadHasCursorBand = false;

/** @brief 欠账表整体失效（surface 生命周期事件，持 g_xpad.m_mutex 调）。 */
static void xpad_bufLedgerResetLocked(void)
{
    int i;
    for (i = 0; i < XPAD_MAX_BUFFERS; ++i) {
        g_xpadBufLedger[i].bits = NULL;
        g_xpadBufLedger[i].full = false;
        XRegion_clear(&g_xpadBufLedger[i].pending);
    }
    g_xpadBufLedgerVictim = 0;
    g_xpadHasCursorBand = false;
    XRect_init(&g_xpadLastCursorBand, 0, 0, 0, 0);
}

/** @brief 按身份查欠账槽；无则占空槽/逐出最旧槽（持 g_xpad.m_mutex 调）。
 *  @note  返回 NULL 表示表异常（不应发生）；调用方按欠全屏处理。 */
static XPadBufferLedger* xpad_bufLedgerAcquireLocked(uint8_t* bits)
{
    int i;
    if (!bits) return NULL;
    for (i = 0; i < XPAD_MAX_BUFFERS; ++i)
        if (g_xpadBufLedger[i].bits == bits)
            return &g_xpadBufLedger[i];
    for (i = 0; i < XPAD_MAX_BUFFERS; ++i) {
        if (!g_xpadBufLedger[i].bits) {
            g_xpadBufLedger[i].bits = bits;
            g_xpadBufLedger[i].full = true;   /* 新缓冲：欠全屏 */
            XRegion_clear(&g_xpadBufLedger[i].pending);
            return &g_xpadBufLedger[i];
        }
    }
    /* 表满（BufferQueue 缓冲数超过表容）：轮转逐出，逐出槽欠全屏。 */
    i = g_xpadBufLedgerVictim % XPAD_MAX_BUFFERS;
    ++g_xpadBufLedgerVictim;
    g_xpadBufLedger[i].bits = bits;
    g_xpadBufLedger[i].full = true;
    XRegion_clear(&g_xpadBufLedger[i].pending);
    return &g_xpadBufLedger[i];
}

/** @brief 把一笔物理矩形账记入全部已登记欠账（持 g_xpad.m_mutex 调）。 */
static void xpad_bufLedgerAccrueRectLocked(const XRect* rectPhys)
{
    int i;
    if (!rectPhys || XRect_isEmpty(rectPhys)) return;
    for (i = 0; i < XPAD_MAX_BUFFERS; ++i) {
        if (!g_xpadBufLedger[i].bits || g_xpadBufLedger[i].full) continue;
        XRegion_addRect(&g_xpadBufLedger[i].pending, rectPhys);
    }
}

/** @brief 全部已登记欠账升格为欠全屏（持 g_xpad.m_mutex 调）。
 *  @note  用于源变化范围未知的帧（区域空/旋转层 flush）：本次全量重画
 *         只救当前缓冲，其他缓冲的既有欠账不含未知的源变化，若不清全
 *         会在其下次补画时漏掉这部分变化（陈旧像素残留）。 */
static void xpad_bufLedgerMarkAllFullLocked(void)
{
    int i;
    for (i = 0; i < XPAD_MAX_BUFFERS; ++i) {
        if (!g_xpadBufLedger[i].bits) continue;
        g_xpadBufLedger[i].full = true;
        XRegion_clear(&g_xpadBufLedger[i].pending);
    }
}

/* ==================== 显示驱动（XPlatformDisplayDriverOps 契约） ==================== */

/** @brief 把 ANativeWindow 的硬件格式映射为 XImageFormat。 */
static XImageFormat xpad_formatFromWindow(int32_t androidFormat)
{
    switch (androidFormat) {
    /* Windows 系 ABI 格式常量在 NDK 头中兼容非 Windows 平台定义。 */
    case WINDOW_FORMAT_RGBA_8888:
        return XImageFormat_ARGB32;   /* 预乘语义由框架统一处理 */
    case WINDOW_FORMAT_RGBX_8888:
        return XImageFormat_ARGB32;   /* X 通道忽略 */
    case WINDOW_FORMAT_RGB_565:
        return XImageFormat_RGB16;
    default:
        return XImageFormat_Invalid;
    }
}

static bool xpad_probe(XPlatformDisplayInfo* outInfo)
{
    bool ok;
    pthread_mutex_lock(&g_xpad.m_mutex);
    ok = g_xpad.m_window != NULL;
    if (ok) {
        g_xpadWidth = ANativeWindow_getWidth(g_xpad.m_window);
        g_xpadHeight = ANativeWindow_getHeight(g_xpad.m_window);
        if (outInfo) {
            outInfo->m_width = g_xpadWidth;
            outInfo->m_height = g_xpadHeight;
            outInfo->m_format = xpad_formatFromWindow(
                ANativeWindow_getFormat(g_xpad.m_window));
            outInfo->m_bitsPerPixel = 32;
            outInfo->m_stride = (size_t)ANativeWindow_getStride(g_xpad.m_window) * 4u;
            outInfo->m_frameBuffer = NULL;      /* 直写地址由 present 时锁出 */
            outInfo->m_frameBufferSize = 0;
            outInfo->m_doubleBuffered = false;  /* lock/unlock 单缓冲语义 */
#if XPLATFORM_DISPLAY_DRIVER_ABI_VERSION >= 2
            /* ABI 2 物理毫米（xdpi/ydpi 反推）：物理 px 口径，与 m_width/
               m_height 同源——消费方以 (m_width, mm) 派生的物理 DPI 恒等于
               设备 xdpi/ydpi 真值（对标 fbdev var.width/height 原值语义）。
               JNI 查询失败时 m_xdpi/ydpi 是 160 兜底假值，按契约上报
               0=未知，严禁编造（守卫位 m_densityValid，XSystemAndroid
               XAndroid_getDisplayDensity 的失败返回）。 */
            if (g_xpad.m_densityValid &&
                g_xpad.m_xdpi > 0.0f && g_xpad.m_ydpi > 0.0f) {
                outInfo->m_physicalWidthMm =
                    (float)g_xpadWidth * 25.4f / g_xpad.m_xdpi;
                outInfo->m_physicalHeightMm =
                    (float)g_xpadHeight * 25.4f / g_xpad.m_ydpi;
            }
            else {
                outInfo->m_physicalWidthMm = 0.0f;
                outInfo->m_physicalHeightMm = 0.0f;
            }
#endif
        }
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return ok;
}

static bool xpad_formatNegotiate(XImageFormat preferred, XImageFormat* outFormat)
{
    XImageFormat panel;
    if (!xpad_probe(NULL)) return false;
    pthread_mutex_lock(&g_xpad.m_mutex);
    panel = xpad_formatFromWindow(
        g_xpad.m_window ? ANativeWindow_getFormat(g_xpad.m_window) : 0);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (panel == XImageFormat_Invalid) return false;
    if (outFormat) *outFormat = panel;
    /* RGBA8888 面板可直写 ARGB32 预乘缓冲（Android 预乘约定一致）；
       RGB565 面板与 RGB16 缓冲直写。 */
    return preferred == XImageFormat_Invalid || preferred == panel;
}

static bool xpad_pan(int bufferIndex)
{
    /* ANativeWindow 无显式翻页：unlockAndPost 即提交扫描。 */
    (void)bufferIndex;
    return true;
}

static bool xpad_cacheSync(XPlatformDisplayCacheMode mode, void* address,
                           size_t length)
{
    /* ANativeWindow lock 的 CPU 写入经 GPU 合成器读取，一致性由图形栈
       保证，无需板级 cache 管理。 */
    (void)mode; (void)address; (void)length;
    return true;
}

static bool xpad_waitVsync(int timeoutMilliseconds)
{
    /* Choreographer vsync 回调接入待后续批次；软件路径 present 频率受
       XGUI_PRESENT_MAX_FPS 闸门约束，撕裂风险由 Android 合成器兜底。 */
    (void)timeoutMilliseconds;
    return false;
}

static size_t xpad_stride(int width, XImageFormat format)
{
    int32_t s;
    (void)format;
    pthread_mutex_lock(&g_xpad.m_mutex);
    s = g_xpad.m_window ? ANativeWindow_getStride(g_xpad.m_window) : 0;
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (s <= 0) s = width;   /* 无 surface 时按紧排兜底 */
    return (size_t)s * 4u;
}

static const XPlatformDisplayDriverOps g_xpadDisplayOps = {
    "android-anativewindow",
    XPLATFORM_DISPLAY_DRIVER_ABI_VERSION,
    xpad_probe,
    xpad_formatNegotiate,
    xpad_pan,
    xpad_cacheSync,
    xpad_waitVsync,
    xpad_stride
};

/* ==================== present（直写 surface + 图层合成） ==================== */

/** @brief 把 XImage 的一行（ARGB32 字节序 B,G,R,A）重排进 RGBA8888 行。 */
static void xpad_blitRowSwap(uint8_t* dst, const uint8_t* src,
                             int width)
{
    int32_t col;
    for (col = 0; col < width; ++col) {
        dst[col * 4 + 0] = src[col * 4 + 2];
        dst[col * 4 + 1] = src[col * 4 + 1];
        dst[col * 4 + 2] = src[col * 4 + 0];
        dst[col * 4 + 3] = src[col * 4 + 3];
    }
}

/* ---- 缩放 blit 快路化辅助（xpad_drawImageScaled 专用，静态） ---- */

/** 栈上分块大小（目标像素计）：块缓冲最大 256x4=1KB，免逐帧堆分配。 */
#define XPAD_SCALED_CHUNK 256

/** @brief 缩放计划：裁剪收口后三条快路共用的只读参数包。 */
typedef struct XPadScaledPlan {
    const uint8_t* m_src;       /* 源图像基址 */
    uint8_t* m_dst;             /* 目标 surface 基址 */
    size_t m_srcBpl;            /* 源行距（字节） */
    size_t m_dstBpl;            /* 目标行距（字节） */
    int m_srcBpp;               /* 源字节宽（RGB16 源 2 / ARGB32 源 4） */
    int m_sx0, m_sy0;           /* 源裁剪起点 */
    int m_visW, m_visH;         /* 可见源尺寸 */
    int m_dw, m_dh;             /* 可见源对应的目标尺寸 */
    int m_x0, m_y0;             /* 目标写起点（surface 物理域） */
    int m_cols, m_rows;         /* 目标写尺寸 */
    int m_colStart, m_rowStart; /* 写域在缩放域内的起点 */
} XPadScaledPlan;

/** @brief 放大 run 表项：列映射与行无关，逐调用构建一次、全行复用。
 *         ANativeWindow 锁出的 bits 页对齐，行基址偏移均为像素宽整数倍，
 *         故按 2/4 字节定宽写入对齐有保证（与逐字节写语义逐位一致）。 */
typedef struct XPadScaledRun {
    int32_t m_cnt; /* 目标连续像素数（>0，已按写域裁剪） */
    int32_t m_src; /* 源列（未加 sx0 的相对列） */
} XPadScaledRun;

/* 像素级小辅助每 run/每像素调用，当前安卓构建（CMAKE_BUILD_TYPE 空，
   实为 -O0）下调用开销显著，强制内联（gcc/clang 均支持）。 */
#if defined(__GNUC__)
#define XPAD_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#define XPAD_ALWAYS_INLINE static
#endif

/** @brief 取一源像素折成 565 位型（RGB16 源直拷，ARGB32 源位截断，
 *         转换式与逐像素参照一致）。 */
XPAD_ALWAYS_INLINE uint16_t xpad_scaledLoad565(const uint8_t* s8, int srcBpp, int col)
{
    if (srcBpp == 2) {
        uint16_t px;
        memcpy(&px, s8 + (size_t)col * 2u, 2u);
        return px;
    }
    else {
        uint8_t b8 = s8[col * 4 + 0];
        uint8_t g8 = s8[col * 4 + 1];
        uint8_t r8 = s8[col * 4 + 2];
        return (uint16_t)(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
    }
}

/** @brief 取一 ARGB32 源像素重排成 RGBA8888 单字（小端写入，内存序
 *         R,G,B,A），供常量填充宽写。 */
XPAD_ALWAYS_INLINE uint32_t xpad_scaledLoad8888(const uint8_t* s8, int col)
{
    return (uint32_t)s8[col * 4 + 2] |
           ((uint32_t)s8[col * 4 + 1] << 8) |
           ((uint32_t)s8[col * 4 + 0] << 16) |
           ((uint32_t)s8[col * 4 + 3] << 24);
}

/** @brief 把单像素常量填充进目标连续 cnt 段（无数据相关分支的
 *         memset 形态，可被编译器自动向量化成宽写）。 */
XPAD_ALWAYS_INLINE void xpad_scaledFill565(uint16_t* d16, int cnt, uint16_t px)
{
    int k;
    for (k = 0; k < cnt; ++k)
        d16[k] = px;
}

XPAD_ALWAYS_INLINE void xpad_scaledFill8888(uint32_t* d32, int cnt, uint32_t px)
{
    int k;
    for (k = 0; k < cnt; ++k)
        d32[k] = px;
}

/** @brief 放大快路（565 窗口，dw>=visW，含非整数系数）：列映射与行无关
 *         ——先逐块构建 run 表（源列 s 覆盖全局目标 [f(s), f(s+1))，
 *         f(s)=ceil(s*dw/visW)，run 长 = f(s+1)-f(s) 以 base+余数进位
 *         递推免除法），再逐行查表纯填充。行映射沿用参照式
 *         srcRow = sy0 + row*visH/dh，输出与逐像素参照逐位一致。 */
static void xpad_scaledUp565(const XPadScaledPlan* p)
{
    XPadScaledRun runs[XPAD_SCALED_CHUNK + 2];
    const int dw = p->m_dw;
    const int visW = p->m_visW;
    const int base = dw / visW; /* 每 run 基长（>=1） */
    const int rem = dw - base * visW;
    int rr, c0;
    for (c0 = 0; c0 < p->m_cols; c0 += XPAD_SCALED_CHUNK) {
        int cn = p->m_cols - c0;
        int s, num, tG, frac, tNext, nr, i;
        if (cn > XPAD_SCALED_CHUNK) cn = XPAD_SCALED_CHUNK;
        s = ((p->m_colStart + c0) * visW) / dw; /* 块首目标列的源列 */
        num = s * dw + visW - 1;                /* ceil 分子 N_s */
        tG = num / visW;                        /* f(s) */
        frac = num - tG * visW;
        tNext = tG - (p->m_colStart + c0);      /* 首run块内起点（可<0） */
        nr = 0;
        while (tNext < cn) {
            int run = base;
            int a, b;
            frac += rem;
            if (frac >= visW) { frac -= visW; ++run; }
            a = (tNext > 0) ? tNext : 0;
            b = tNext + run;
            if (b > cn) b = cn;
            if (b > a) {
                runs[nr].m_cnt = b - a;
                runs[nr].m_src = s;
                ++nr;
            }
            tNext += run;
            ++s;
        }
        for (rr = 0; rr < p->m_rows; ++rr) {
            int row = p->m_rowStart + rr;
            int srcRow = p->m_sy0 + (row * p->m_visH) / p->m_dh;
            const uint8_t* s8 = p->m_src + (size_t)srcRow * p->m_srcBpl;
            uint16_t* d16 = (uint16_t*)
                (p->m_dst + (size_t)(p->m_y0 + rr) * p->m_dstBpl +
                 (size_t)(p->m_x0 + c0) * 2u);
            for (i = 0; i < nr; ++i) {
                xpad_scaledFill565(d16, runs[i].m_cnt,
                                   xpad_scaledLoad565(s8, p->m_srcBpp,
                                                      p->m_sx0 +
                                                          runs[i].m_src));
                d16 += runs[i].m_cnt;
            }
        }
    }
}

/** @brief 放大快路（8888 窗口）：结构同 xpad_scaledUp565，像素为
 *         RGBA8888 单字宽写。 */
static void xpad_scaledUp8888(const XPadScaledPlan* p)
{
    XPadScaledRun runs[XPAD_SCALED_CHUNK + 2];
    const int dw = p->m_dw;
    const int visW = p->m_visW;
    const int base = dw / visW;
    const int rem = dw - base * visW;
    int rr, c0;
    for (c0 = 0; c0 < p->m_cols; c0 += XPAD_SCALED_CHUNK) {
        int cn = p->m_cols - c0;
        int s, num, tG, frac, tNext, nr, i;
        if (cn > XPAD_SCALED_CHUNK) cn = XPAD_SCALED_CHUNK;
        s = ((p->m_colStart + c0) * visW) / dw;
        num = s * dw + visW - 1;
        tG = num / visW;
        frac = num - tG * visW;
        tNext = tG - (p->m_colStart + c0);
        nr = 0;
        while (tNext < cn) {
            int run = base;
            int a, b;
            frac += rem;
            if (frac >= visW) { frac -= visW; ++run; }
            a = (tNext > 0) ? tNext : 0;
            b = tNext + run;
            if (b > cn) b = cn;
            if (b > a) {
                runs[nr].m_cnt = b - a;
                runs[nr].m_src = s;
                ++nr;
            }
            tNext += run;
            ++s;
        }
        for (rr = 0; rr < p->m_rows; ++rr) {
            int row = p->m_rowStart + rr;
            int srcRow = p->m_sy0 + (row * p->m_visH) / p->m_dh;
            const uint8_t* s8 = p->m_src + (size_t)srcRow * p->m_srcBpl;
            uint32_t* d32 = (uint32_t*)
                (p->m_dst + (size_t)(p->m_y0 + rr) * p->m_dstBpl +
                 (size_t)(p->m_x0 + c0) * 4u);
            for (i = 0; i < nr; ++i) {
                xpad_scaledFill8888(d32, runs[i].m_cnt,
                                    xpad_scaledLoad8888(
                                        s8, p->m_sx0 + runs[i].m_src));
                d32 += runs[i].m_cnt;
            }
        }
    }
}

/** @brief 整数倍放大快路（565 窗口，dw=n*visW 且 dh=n*visH）：块缓冲里
 *         整行展开一次、复制到共享同一源行的连续 n 个目标行（行复制
 *         路线）。组边界仍逐行重算参照式 srcRow 收口，不依赖整除假设；
 *         列映射 g/n 与参照 (colStart+col)*visW/dw 恒等（visW 约束后
 *         整除消去）。 */
static void xpad_scaledIntRow565(const XPadScaledPlan* p, int n)
{
    uint16_t tmp[XPAD_SCALED_CHUNK];
    int rr = 0;
    while (rr < p->m_rows) {
        int row = p->m_rowStart + rr;
        int srcRow = p->m_sy0 + (row * p->m_visH) / p->m_dh;
        const uint8_t* s8 = p->m_src + (size_t)srcRow * p->m_srcBpl;
        int rr2 = rr + 1;
        int c0;
        while (rr2 < p->m_rows &&
               p->m_sy0 + ((p->m_rowStart + rr2) * p->m_visH) / p->m_dh ==
                   srcRow)
            ++rr2; /* 行组 [rr,rr2) 共享源行（整数倍下恰 n 行，首尾组可短） */
        for (c0 = 0; c0 < p->m_cols; c0 += XPAD_SCALED_CHUNK) {
            int cn = p->m_cols - c0;
            int gA, s, skip, t, r;
            if (cn > XPAD_SCALED_CHUNK) cn = XPAD_SCALED_CHUNK;
            gA = p->m_colStart + c0; /* 块首全局目标列 */
            s = gA / n;
            skip = gA - s * n;       /* 首源像素被块界裁掉的前导 */
            t = 0;
            while (t < cn) {
                uint16_t px = xpad_scaledLoad565(s8, p->m_srcBpp,
                                                 p->m_sx0 + s);
                int k = skip;
                skip = 0;
                for (; k < n && t < cn; ++k, ++t)
                    tmp[t] = px;
                ++s;
            }
            for (r = rr; r < rr2; ++r)
                memcpy(p->m_dst + (size_t)(p->m_y0 + r) * p->m_dstBpl +
                           (size_t)(p->m_x0 + c0) * 2u,
                       tmp, (size_t)cn * 2u);
        }
        rr = rr2;
    }
}

/** @brief 整数倍放大快路（8888 窗口）：结构同 xpad_scaledIntRow565，
 *         像素为 RGBA8888 单字。 */
static void xpad_scaledIntRow8888(const XPadScaledPlan* p, int n)
{
    uint32_t tmp[XPAD_SCALED_CHUNK];
    int rr = 0;
    while (rr < p->m_rows) {
        int row = p->m_rowStart + rr;
        int srcRow = p->m_sy0 + (row * p->m_visH) / p->m_dh;
        const uint8_t* s8 = p->m_src + (size_t)srcRow * p->m_srcBpl;
        int rr2 = rr + 1;
        int c0;
        while (rr2 < p->m_rows &&
               p->m_sy0 + ((p->m_rowStart + rr2) * p->m_visH) / p->m_dh ==
                   srcRow)
            ++rr2;
        for (c0 = 0; c0 < p->m_cols; c0 += XPAD_SCALED_CHUNK) {
            int cn = p->m_cols - c0;
            int gA, s, skip, t, r;
            if (cn > XPAD_SCALED_CHUNK) cn = XPAD_SCALED_CHUNK;
            gA = p->m_colStart + c0;
            s = gA / n;
            skip = gA - s * n;
            t = 0;
            while (t < cn) {
                uint32_t px = xpad_scaledLoad8888(s8, p->m_sx0 + s);
                int k = skip;
                skip = 0;
                for (; k < n && t < cn; ++k, ++t)
                    tmp[t] = px;
                ++s;
            }
            for (r = rr; r < rr2; ++r)
                memcpy(p->m_dst + (size_t)(p->m_y0 + r) * p->m_dstBpl +
                           (size_t)(p->m_x0 + c0) * 4u,
                       tmp, (size_t)cn * 4u);
        }
        rr = rr2;
    }
}

/** @brief 缩小快路（565 窗口，dw<visW）：每目标列源列查表（每调用一次、
 *         栈上分块，源列式与参照恒等 off[t] = sx0+(colStart+t)*visW/dw），
 *         逐行纯查表拷贝（无逐像素进位分支）。 */
static void xpad_scaledDown565(const XPadScaledPlan* p)
{
    int32_t off[XPAD_SCALED_CHUNK];
    int c0;
    for (c0 = 0; c0 < p->m_cols; c0 += XPAD_SCALED_CHUNK) {
        int cn = p->m_cols - c0;
        int t, rr;
        if (cn > XPAD_SCALED_CHUNK) cn = XPAD_SCALED_CHUNK;
        for (t = 0; t < cn; ++t)
            off[t] = p->m_sx0 +
                     ((p->m_colStart + c0 + t) * p->m_visW) / p->m_dw;
        for (rr = 0; rr < p->m_rows; ++rr) {
            int row = p->m_rowStart + rr;
            int srcRow = p->m_sy0 + (row * p->m_visH) / p->m_dh;
            const uint8_t* s8 = p->m_src + (size_t)srcRow * p->m_srcBpl;
            uint16_t* d16 = (uint16_t*)
                (p->m_dst + (size_t)(p->m_y0 + rr) * p->m_dstBpl +
                 (size_t)(p->m_x0 + c0) * 2u);
            for (t = 0; t < cn; ++t)
                d16[t] = xpad_scaledLoad565(s8, p->m_srcBpp, off[t]);
        }
    }
}

/** @brief 缩小快路（8888 窗口）：结构同 xpad_scaledDown565，像素为
 *         RGBA8888 单字。 */
static void xpad_scaledDown8888(const XPadScaledPlan* p)
{
    int32_t off[XPAD_SCALED_CHUNK];
    int c0;
    for (c0 = 0; c0 < p->m_cols; c0 += XPAD_SCALED_CHUNK) {
        int cn = p->m_cols - c0;
        int t, rr;
        if (cn > XPAD_SCALED_CHUNK) cn = XPAD_SCALED_CHUNK;
        for (t = 0; t < cn; ++t)
            off[t] = p->m_sx0 +
                     ((p->m_colStart + c0 + t) * p->m_visW) / p->m_dw;
        for (rr = 0; rr < p->m_rows; ++rr) {
            int row = p->m_rowStart + rr;
            int srcRow = p->m_sy0 + (row * p->m_visH) / p->m_dh;
            const uint8_t* s8 = p->m_src + (size_t)srcRow * p->m_srcBpl;
            uint32_t* d32 = (uint32_t*)
                (p->m_dst + (size_t)(p->m_y0 + rr) * p->m_dstBpl +
                 (size_t)(p->m_x0 + c0) * 4u);
            for (t = 0; t < cn; ++t)
                d32[t] = xpad_scaledLoad8888(s8, off[t]);
        }
    }
}

/** @brief 缩放路径：最近邻整数式重采样（scale!=1.0 时启用）。
 *  @details 全链唯一物理放大点（设计 §5，方案 B）：图层图像为逻辑口径
 *           后备存储，present 时按实测系数 g_xpad.m_presentScale 放大进
 *           物理 surface。内核映射定义式保持 srcCol(col)=sx0+
 *           (colStart+col)*visW/dw、srcRow(row)=sy0+row*visH/dh（最近邻
 *           索引只依赖相对偏移，visW>dw 的缩小同样成立），按缩放类型
 *           分派三条等价快路（输出与逐像素 Bresenham 参照逐位一致）：
 *           - 整数倍（dw=n*visW 且 dh=n*visH，n>=2）：整行展开一次 +
 *             连续 n 行 memcpy 行复制（565/8888 双路）；
 *           - 非整数放大（dw>=visW）：源像素 → 连续目标 run，run 长
 *             = f(s+1)-f(s)，f(s)=ceil(s*dw/visW)，base+余数进位递推
 *             免除法；run 表逐调用构建一次（与行无关）、逐行查表纯
 *             常量填充（内环无分支，可被编译器向量化）；
 *           - 缩小（dw<visW）：每目标列源列查表（每调用一次）+ 逐行
 *             纯拷贝。
 *           裁剪次序（§5-4 源侧裁剪缩放前）：左/上越界先折算成源裁剪量
 *           并平移目标原点；右/下与折算舍入残余由目标域最终钳制收口
 *           ——最近邻索引只依赖相对偏移，目标域截断与源侧截断逐一
 *           对应，源读索引可证不越界（srcCol<=sx0+visW-1<=w-1）。 */
static void xpad_drawImageScaled(ANativeWindow_Buffer* buf,
                                 const XImage* image,
                                 int dx, int dy, int32_t winFmt, float scale,
                                 int cx0, int cy0, int cx1, int cy1)
{
    const uint8_t* src;
    size_t srcBpl;
    int w, h;
    int dw, dh;
    int sx0, sy0, visW, visH;
    int x0, y0, x1, y1;
    int colStart, rowStart;

    src = (const uint8_t*)XImage_constBits(image);
    if (!src) return;
    w = XImage_width(image);
    h = XImage_height(image);
    srcBpl = (size_t)XImage_bytesPerLine(image);
    if (w <= 0 || h <= 0 || scale <= 0.0f) return;
    dw = (int)((float)w * scale + 0.5f);
    dh = (int)((float)h * scale + 0.5f);
    if (dw <= 0 || dh <= 0) return;

    /* 源侧裁剪（缩放前，逻辑坐标）：左/上越界折算成源裁剪量并平移
       目标原点。 */
    sx0 = 0; sy0 = 0; visW = w; visH = h;
    if (dx < 0) {
        sx0 = (int)((float)(-dx) / scale + 0.5f);
        if (sx0 > w) sx0 = w;
        visW = w - sx0;
        dx += (int)((float)sx0 * scale + 0.5f);
    }
    if (dy < 0) {
        sy0 = (int)((float)(-dy) / scale + 0.5f);
        if (sy0 > h) sy0 = h;
        visH = h - sy0;
        dy += (int)((float)sy0 * scale + 0.5f);
    }
    if (visW <= 0 || visH <= 0) return;
    dw = (int)((float)visW * scale + 0.5f);
    dh = (int)((float)visH * scale + 0.5f);
    if (dw <= 0 || dh <= 0) return;

    /* 目标域最终钳制：覆盖右/下溢出与源侧折算的舍入残余（理论可达：
       round(sx0*scale) 可比补偿量少 1 像素使 dx 残留 -1）；再交目的裁剪
       （脏区直递：本帧只许写欠账矩形内，[cx0,cx1)x[cy0,cy1) 含头舍尾）。 */
    x0 = (dx < 0) ? 0 : dx;
    y0 = (dy < 0) ? 0 : dy;
    x1 = dx + dw; if (x1 > buf->width)  x1 = buf->width;
    y1 = dy + dh; if (y1 > buf->height) y1 = buf->height;
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    colStart = x0 - dx;
    rowStart = y0 - dy;
    if (x1 <= x0 || y1 <= y0 || colStart >= dw || rowStart >= dh) return;

    /* 依窗口格式定源字节宽与目标行距，再按缩放类型分派三条快路。 */
    {
        XPadScaledPlan plan;
        int nCand;
        if (winFmt == WINDOW_FORMAT_RGB_565) {
            plan.m_srcBpp =
                (XImage_format(image) == XImageFormat_RGB16) ? 2 : 4;
            if (srcBpl < (size_t)w * (size_t)plan.m_srcBpp) return;
            plan.m_dstBpl = (size_t)buf->stride * 2u;
        }
        else {
            plan.m_srcBpp = 4;
            if (srcBpl < (size_t)w * 4u) return;
            plan.m_dstBpl = (size_t)buf->stride * 4u;
        }
        plan.m_src = src;
        plan.m_dst = (uint8_t*)buf->bits;
        plan.m_srcBpl = srcBpl;
        plan.m_sx0 = sx0;
        plan.m_sy0 = sy0;
        plan.m_visW = visW;
        plan.m_visH = visH;
        plan.m_dw = dw;
        plan.m_dh = dh;
        plan.m_x0 = x0;
        plan.m_y0 = y0;
        plan.m_cols = x1 - x0;
        plan.m_rows = y1 - y0;
        plan.m_colStart = colStart;
        plan.m_rowStart = rowStart;

        nCand = dw / visW;
        if (nCand >= 2 && nCand * visW == dw && dh == nCand * visH) {
            /* 整数倍：整行展开一次 + 连续 n 行复制。 */
            if (winFmt == WINDOW_FORMAT_RGB_565)
                xpad_scaledIntRow565(&plan, nCand);
            else
                xpad_scaledIntRow8888(&plan, nCand);
        }
        else if (dw >= visW) {
            /* 放大（含 1 倍邻域）：源像素 → 连续目标 run。 */
            if (winFmt == WINDOW_FORMAT_RGB_565)
                xpad_scaledUp565(&plan);
            else
                xpad_scaledUp8888(&plan);
        }
        else {
            /* 缩小：每目标列源列查表 + 逐行纯拷贝。 */
            if (winFmt == WINDOW_FORMAT_RGB_565)
                xpad_scaledDown565(&plan);
            else
                xpad_scaledDown8888(&plan);
        }
    }
}

/** @brief 把图像按图层位置画进已锁定的 surface 缓冲（越界+目的域裁剪）。
 *  @note  width/height 为图像（逻辑）口径；pos 为图层在 surface 内的
 *         目标位置、scale 为物理放大系数（均由调用方按实测系数折算）。
 *         scale==1.0 走历史 memcpy 快路径（dpr=1/WSA 逐位不变，红线；
 *         presentScale 在 dpr<=1 时被钳为恒 1.0，见各更新点守卫）。
 *         clip 为目的裁剪矩形（surface 物理坐标；NULL=整幅）——脏区
 *         直递时传欠账矩形，本帧只写其内像素。 */
static void xpad_drawImage(ANativeWindow_Buffer* buf, const XImage* image,
                           const XPoint* pos, int32_t winFmt, float scale,
                           const XRect* clip)
{
    const uint8_t* src;
    size_t srcBpl;
    int w, h, x, y, row;
    int cx0, cy0, cx1, cy1;

    if (!buf || !buf->bits || !image) return;
    /* 目的裁剪域（脏区直递：clip=NULL=整个 surface）。 */
    cx0 = 0; cy0 = 0;
    cx1 = buf->width; cy1 = buf->height;
    if (clip && clip->width > 0 && clip->height > 0) {
        if (clip->x > cx0) cx0 = clip->x;
        if (clip->y > cy0) cy0 = clip->y;
        if (clip->x + clip->width < cx1) cx1 = clip->x + clip->width;
        if (clip->y + clip->height < cy1) cy1 = clip->y + clip->height;
    }
    if (cx1 <= cx0 || cy1 <= cy0) return;
    if (scale != 1.0f) {
        /* 缩放路径：pos 已由调用方折算成目标系坐标。 */
        xpad_drawImageScaled(buf, image, pos->x, pos->y, winFmt, scale,
                             cx0, cy0, cx1, cy1);
        return;
    }
    src = (const uint8_t*)XImage_constBits(image);
    if (!src) return;
    w = XImage_width(image);
    h = XImage_height(image);
    srcBpl = (size_t)XImage_bytesPerLine(image);
    if (w <= 0 || h <= 0) return;

    x = pos->x; y = pos->y;
    /* 左侧/上方越界裁剪（弹层可被拖出屏外）+ 目的裁剪域收口。 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > buf->width) w = buf->width - x;
    if (y + h > buf->height) h = buf->height - y;
    if (x < cx0) { w -= cx0 - x; x = cx0; }
    if (y < cy0) { h -= cy0 - y; y = cy0; }
    if (x + w > cx1) w = cx1 - x;
    if (y + h > cy1) h = cy1 - y;
    if (w <= 0 || h <= 0) return;

    if (winFmt == WINDOW_FORMAT_RGB_565) {
        /* 565 窗口：图像 ARGB32(预乘,B,G,R,A 内存序)→RGB565 位截断；
           RGB16 图像按 2 字节行直拷。行距 2 字节/像素。 */
        size_t dstBpl = (size_t)buf->stride * 2u;
        int srcBpp = (XImage_format(image) == XImageFormat_RGB16) ? 2 : 4;
        if (srcBpl < (size_t)XImage_width(image) * (size_t)srcBpp) return;
        for (row = 0; row < h; ++row) {
            const uint8_t* s8 = src + (size_t)row * srcBpl;
            uint8_t* d8 = (uint8_t*)buf->bits +
                          (size_t)(y + row) * dstBpl + (size_t)x * 2u;
            int col;
            if (srcBpp == 2) {
                memcpy(d8, s8, (size_t)w * 2u);
            }
            else {
                for (col = 0; col < w; ++col) {
                    uint8_t b8 = s8[col * 4 + 0];
                    uint8_t g8 = s8[col * 4 + 1];
                    uint8_t r8 = s8[col * 4 + 2];
                    uint16_t px = (uint16_t)(((r8 >> 3) << 11) |
                                             ((g8 >> 2) << 5) |
                                             (b8 >> 3));
                    d8[col * 2 + 0] = (uint8_t)(px & 0xFF);
                    d8[col * 2 + 1] = (uint8_t)(px >> 8);
                }
            }
        }
    }
    else {
        /* 8888 窗口（RGBA8888/RGBX8888）：ARGB32 内存序 B,G,R,A →
           R,G,B,A 通道重排，行距 4 字节/像素。 */
        if (srcBpl < (size_t)XImage_width(image) * 4u) return;
        for (row = 0; row < h; ++row) {
            xpad_blitRowSwap((uint8_t*)buf->bits +
                                 (size_t)(y + row) * (size_t)buf->stride * 4u +
                                 (size_t)x * 4u,
                             src + (size_t)row * srcBpl, w);
        }
    }
}

/**
 * @brief  计算各图层在**设备轴**（表面逻辑坐标）下的原点。
 * @details [弹层落位域·安卓 2026-10-24] 契约与 posix(X11) / win32 同源：
 *          控件层给弹层（Popup）定位用的是**逻辑全局**坐标
 *          （XComboBox xcombo_popupReposition / 系统菜单：父链逻辑偏移
 *          累加 + 顶层逻辑原点），而旋转窗在屏上的占位是**对调后的设备
 *          footprint**——弹层要贴着 opener 出现，必须以
 *          「opener 设备原点 + 旋转正变换（弹层逻辑局部矩形，
 *          XWindowRotation.h MapRect 像素下标口径，与 present/输入同源）」
 *          为落位锚点，否则弹层落在旧角度对应的位置、甚至跑出窗口外
 *          （用户实测：切到 90° 后方向下拉浮在页面中间，点不到）。
 *
 *          0° 时恒等（devPos = pos），非弹层图层恒等（旋转只对调宽高，
 *          **原点不随旋转变**——XWindowRotation.h 的基石）。
 * @note   调用方须持 g_xpad.m_mutex（本函数不自行加锁）。
 */
static void xpad_updateDeviceOrigins(void)
{
    int i;
    /* 第一遍：默认恒等（主窗/普通图层/0° 弹层）。 */
    for (i = 0; i < g_xpadWindowCount; ++i) {
        g_xpadWindows[i].devPos = g_xpadWindows[i].pos;
    }
    /* 第二遍：弹层按 opener 正变换换算（opener 先于弹层登记，故其
       devPos 已是最终值；嵌套弹层按登记序逐级成立）。 */
    for (i = 0; i < g_xpadWindowCount; ++i) {
        XPadLayer* layer = &g_xpadWindows[i];
        XWindow* win = layer->win;
        XWindow* opener;
        XPadLayer* ol;
        XPoint openerDev;
        int rot, localX, localY, lw, lh, devX, devY, devW, devH;
        if (!win) continue;
        rot = XWindow_contentRotationDegrees(win);
        if (rot == 0) continue;
        if (XWindow_type(win) != XWindowType_Popup) continue;
        opener = XWindow_transientParent(win);
        if (!opener) continue;
        ol = xpad_findLayer(opener);
        openerDev = ol ? ol->devPos
                        : (XPoint){ XWindow_x(opener), XWindow_y(opener) };
        /* 弹层逻辑局部矩形（相对 opener 原点）。 */
        localX = layer->pos.x - XWindow_x(opener);
        localY = layer->pos.y - XWindow_y(opener);
        /* 弹层逻辑尺寸：优先用已提交图像的逻辑宽高（与 present 侧绘制
           同源），无图像时回落窗口逻辑几何。 */
        if (layer->lastImage) {
            lw = XImage_width(layer->lastImage);
            lh = XImage_height(layer->lastImage);
        }
        else {
            lw = XWindow_width(win);
            lh = XWindow_height(win);
        }
        if (lw <= 0 || lh <= 0) continue;
        devX = devY = devW = devH = 0;
        XWindow_rotationMapRect(rot, XWindow_width(opener),
                                XWindow_height(opener),
                                &localX, &localY, &lw, &lh,
                                &devX, &devY, &devW, &devH);
        layer->devPos.x = openerDev.x + devX;
        layer->devPos.y = openerDev.y + devY;
    }
}

/**
 * @brief  把图像按旋转角转置后画进 surface（安卓内容旋转接入 2026-10-24）。
 * @details 口径与桌面（posix fbdev 直写 / win32 present）严格同源：
 *          - 源图是**逻辑轴**图像（w=逻辑宽，h=逻辑高）；
 *          - 目标矩形是**对调后的设备 footprint**（90/270 时宽高互换）；
 *          - 逐设备像素用 XWindow_rotationUnmapPoint 反查逻辑源坐标
 *            （R⁻¹），与 present 侧 R⁻¹ 转置互为逆运算。
 *          两者共用 XWindowRotation.h —— 呈现与命中两端口径逐位一致。
 *          原点不随旋转变（窗口 x/y 保持），故目标矩形左上角 = pos。
 * @param   rot   内容旋转角（0/90/180/270）。
 * @param   scale 物理放大系数（presentScale；1.0 = 逐位直通）。
 */
static void xpad_drawImageRotated(ANativeWindow_Buffer* buf,
                                  const XImage* image,
                                  const XPoint* pos, int32_t winFmt,
                                  float scale, int rot)
{
    const uint8_t* src;
    size_t srcBpl;
    int w, h, devW, devH;
    int dx, dy;
    int srcBpp;

    if (!buf || !buf->bits || !image || rot == 0) return;
    src = (const uint8_t*)XImage_constBits(image);
    if (!src) return;
    w = XImage_width(image);
    h = XImage_height(image);
    srcBpl = (size_t)XImage_bytesPerLine(image);
    srcBpp = (XImage_format(image) == XImageFormat_RGB16) ? 2 : 4;
    if (w <= 0 || h <= 0) return;
    if (srcBpl < (size_t)w * (size_t)srcBpp) return;

    /* 设备 footprint：90/270 时宽高对调（旋转只对调宽高，原点不变）。 */
    XWindow_rotationDeviceSize(rot, w, h, &devW, &devH);
    if (devW <= 0 || devH <= 0) return;

    {
        int step = (scale > 0.0f && scale != 1.0f) ? 1 : 1;
        int dstW = (int)((float)devW * scale + 0.5f);
        int dstH = (int)((float)devH * scale + 0.5f);
        int x0 = pos->x;
        int y0 = pos->y;
        int x1 = x0 + dstW;
        int y1 = y0 + dstH;
        /* 目标域钳制（弹层可被拖出屏外）。 */
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > buf->width) x1 = buf->width;
        if (y1 > buf->height) y1 = buf->height;
        if (x1 <= x0 || y1 <= y0) return;
        (void)step;

        for (dy = y0; dy < y1; ++dy) {
            /* 设备轴坐标（未放大）：R⁻¹ 反查逻辑源坐标。 */
            int devY = (int)((float)(dy - pos->y) / scale);
            uint8_t* drow = (uint8_t*)buf->bits +
                            (size_t)dy * (size_t)buf->stride *
                            (winFmt == WINDOW_FORMAT_RGB_565 ? 2u : 4u);
            if (devY < 0 || devY >= devH) continue;
            for (dx = x0; dx < x1; ++dx) {
                int devX = (int)((float)(dx - pos->x) / scale);
                int lx = 0, ly = 0;
                const uint8_t* p;
                if (devX < 0 || devX >= devW) continue;
                XWindow_rotationUnmapPoint(rot, w, h, devX, devY, &lx, &ly);
                if (lx < 0 || lx >= w || ly < 0 || ly >= h) continue;
                p = src + (size_t)ly * srcBpl + (size_t)lx * (size_t)srcBpp;
                if (winFmt == WINDOW_FORMAT_RGB_565) {
                    uint8_t* d8 = drow + (size_t)dx * 2u;
                    if (srcBpp == 2) {
                        d8[0] = p[0];
                        d8[1] = p[1];
                    }
                    else {
                        uint8_t b8 = p[0];
                        uint8_t g8 = p[1];
                        uint8_t r8 = p[2];
                        uint16_t px = (uint16_t)(((r8 >> 3) << 11) |
                                                 ((g8 >> 2) << 5) |
                                                 (b8 >> 3));
                        d8[0] = (uint8_t)(px & 0xFF);
                        d8[1] = (uint8_t)(px >> 8);
                    }
                }
                else {
                    /* 8888 窗口：ARGB32(B,G,R,A) → R,G,B,A 通道重排。 */
                    uint8_t* d8 = drow + (size_t)dx * 4u;
                    d8[0] = p[2];
                    d8[1] = p[1];
                    d8[2] = p[0];
                    d8[3] = (srcBpp == 2) ? 0xFFu : p[3];
                }
            }
        }
    }
}

/** @brief 图层补画快照（部分补画路径持锁一次取齐，锁外逐矩形绘制）。 */
typedef struct XPadLayerSnap
{
    const XImage* img;   /**< 图层最近提交图像（借用）。 */
    XPoint posPx;        /**< surface 物理落位（devPos×presentScale）。 */
} XPadLayerSnap;

/** @brief 逻辑（窗口本地）脏矩形 → 物理 surface 矩形：图层设备原点平移
 *         （含缓冲偏移修正）+ presentScale 放大，右/下缘多收 1 物理像素
 *         防逐帧舍入缝（欠 1 像素=残影，多 1 像素=无害重画），再裁
 *         surface 界。 */
static XRect xpad_dirtyRectToPhys(const XRect* logical, const XPoint* devOrigin,
                                  float scale, int surfW, int surfH)
{
    int lx0 = devOrigin->x + logical->x;
    int ly0 = devOrigin->y + logical->y;
    int px0 = (int)((float)lx0 * scale);
    int py0 = (int)((float)ly0 * scale);
    int px1 = (int)((float)(lx0 + logical->width) * scale + 0.5f) + 1;
    int py1 = (int)((float)(ly0 + logical->height) * scale + 0.5f) + 1;
    XRect r;

    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 > surfW) px1 = surfW;
    if (py1 > surfH) py1 = surfH;
    XRect_init(&r, px0, py0, px1 - px0, py1 - py0);
    return r;
}

/**
 * @brief  XPlatformNativeWindow_present 的安卓实现（脏区直递，2026-10-27）。
 * @details [对齐 Qt 上屏语义：脏区直递、绝不无故整屏重画] region 不再
 *          丢弃：窗口本地逻辑脏区折算物理坐标后记入**全部**目的缓冲的
 *          欠账；本次 lock 到的缓冲按其欠账从各图层 lastImage 裁剪补画
 *          （画家序）再清账。lock 缓冲以 bits 指针为身份（BufferQueue
 *          2~3 块轮转，各记各的账，见 g_xpadBufLedger 注）；陌生缓冲
 *          （首次 lock 到/surface 重建）欠全屏，走一次全量兜底。
 *          整幅回退（=旧模型：整屏清黑+全图层重画）的场合：
 *          - region 为 NULL/空（契约：按整张缓冲处理）；
 *          - 源变化范围未知：本层未登记、本层或任一可见层带内容旋转
 *            （旋转 blit 无矩形口径）——此时其余缓冲一并升格欠全屏
 *            （其既有欠账不含未知变化，不清全会在下次补画漏新）；
 *          - presentScale 未标定（≤0）。
 *          光标精灵带每帧随账入各缓冲（上帧带=还原、本帧带=落笔），
 *          精灵不在任何图层图像里，须专账（g_xpadLastCursorBand）。
 * @note   ANativeWindow 的 stride 是像素数且可能大于宽度，必须逐行拷。
 */
#if XCURSOR_ON
/**
 * @brief  算软件光标精灵的物理像素带与设备轴锚点（纯算不画，记账/落笔
 *         两用；2026-10-27 从 xpad_drawSwCursor 提取）。
 * @details 与 fbdev 直写精灵同源口径（XCursor_swRect/swBits/swPosFrame）：
 *          激活判定 XCursor_swActive（软件使能 && 无硬件后端 && 已喂位
 *          && 有效形状非 Blank）；逻辑外接矩形按喂给参考系
 *          （rot/logW/logH/origin）rotationMapRect 正变换到设备坐标，
 *          再按 presentScale 放大到物理像素并裁 surface 界。
 * @param   scale    presentScale（物理/逻辑放大系数）。
 * @param   surfW/H  surface 物理尺寸（裁剪界）。
 * @param   outPhys  出参：物理带矩形（已裁界）。
 * @param   outDev   出参：设备轴矩形原点（逐像素反查用）；可 NULL。
 * @return  false=精灵不可见（未激活/形状空/带裁空），outPhys 不动。
 */
static bool xpad_swCursorGeom(float scale, int surfW, int surfH,
                              XRect* outPhys, XPoint* outDev)
{
    XRect lrect;
    int bw, bh;
    int rot = 0, logW = 0, logH = 0;
    XPoint frameOrigin;
    int px, py, pw, ph;
    int x0, y0, x1, y1;

    if (!XCursor_swActive()) return false;
    lrect = XCursor_swRect();
    if (lrect.width <= 0 || lrect.height <= 0) return false;
    bw = XCursor_swWidth();
    bh = XCursor_swHeight();
    if (!XCursor_swBits() || bw <= 0 || bh <= 0) return false;
    XCursor_swPosFrame(&rot, &logW, &logH, &frameOrigin);

    /* 逻辑矩形 → 设备矩形（rotationMapRect 正变换 + 原点平移）。 */
    {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        if (!XWindow_rotationNormalize(rot, &rx)) return false;
        XWindow_rotationMapRect(rot, logW, logH, &lrect.x, &lrect.y,
                                &lrect.width, &lrect.height,
                                &rx, &ry, &rw, &rh);
        px = rx + frameOrigin.x;
        py = ry + frameOrigin.y;
        pw = rw;
        ph = rh;
    }
    if (pw <= 0 || ph <= 0) return false;

    /* 设备矩形 → 物理像素矩形（presentScale 放大）+ surface 界裁剪。 */
    x0 = (int)((float)px * scale + 0.5f);
    y0 = (int)((float)py * scale + 0.5f);
    x1 = (int)((float)(px + pw) * scale + 0.5f);
    y1 = (int)((float)(py + ph) * scale + 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > surfW) x1 = surfW;
    if (y1 > surfH) y1 = surfH;
    if (x1 <= x0 || y1 <= y0) return false;

    XRect_init(outPhys, x0, y0, x1 - x0, y1 - y0);
    if (outDev) { outDev->x = px; outDev->y = py; }
    return true;
}

/**
 * @brief  把软件光标精灵画进 surface（present 合成后落笔，2026-10-27）。
 * @details 逐物理像素 /scale 反查设备坐标 → R⁻¹ 回内容局部 → 位图取色
 *          （0=透明跳过，1=黑，2=白），565/8888 两格式同 xpad_drawImage
 *          的像素写法。精灵带由 xpad_swCursorGeom 给出；脏区直递模型下
 *          本帧带与上帧带都已记入各缓冲欠账（欠账补画先还原旧精灵再
 *          落新精灵），不会踩到未补画的底图。
 * @param   buf    已 lock 的 surface 缓冲。
 * @param   winFmt 窗口像素格式（565/8888）。
 * @param   scale  presentScale（物理/逻辑放大系数）。
 */
static void xpad_drawSwCursor(ANativeWindow_Buffer* buf, int32_t winFmt,
                              float scale)
{
    XRect band;
    XPoint dev;
    XRect lrect;
    const uint8_t* bits;
    int bw, bh;
    int rot = 0, logW = 0, logH = 0;
    XPoint frameOrigin;
    int px, py;
    int dx, dy;
    size_t bpp;

    if (!buf || !buf->bits) return;
    if (!xpad_swCursorGeom(scale, buf->width, buf->height, &band, &dev)) return;
    bits = XCursor_swBits();
    bw = XCursor_swWidth();
    bh = XCursor_swHeight();
    if (!bits || bw <= 0 || bh <= 0) return;
    lrect = XCursor_swRect();
    XCursor_swPosFrame(&rot, &logW, &logH, &frameOrigin);
    px = dev.x;
    py = dev.y;

    bpp = (winFmt == WINDOW_FORMAT_RGB_565) ? 2u : 4u;
    for (dy = band.y; dy < band.y + band.height; ++dy)
    {
        uint8_t* row = (uint8_t*)buf->bits +
            (size_t)dy * (size_t)buf->stride * bpp;
        for (dx = band.x; dx < band.x + band.width; ++dx)
        {
            int devX = (int)((float)(dx - band.x) / scale) + px;
            int devY = (int)((float)(dy - band.y) / scale) + py;
            int lx = 0, ly = 0;
            int sx, sy;
            uint8_t v;
            uint8_t* d8;
            /* 物理像素 → 设备坐标（/scale）→ 内容局部（R⁻¹）。 */
            if (rot != 0)
            {
                XWindow_rotationUnmapPoint(rot, logW, logH,
                                           devX - frameOrigin.x,
                                           devY - frameOrigin.y, &lx, &ly);
            }
            else
            {
                lx = devX - frameOrigin.x;
                ly = devY - frameOrigin.y;
            }
            sx = lx - lrect.x;
            sy = ly - lrect.y;
            if (sx < 0 || sx >= bw || sy < 0 || sy >= bh) continue;
            v = bits[(size_t)sy * (size_t)bw + (size_t)sx];
            if (v == 0) continue; /* 透明：保留底图。 */
            d8 = row + (size_t)dx * bpp;
            if (winFmt == WINDOW_FORMAT_RGB_565)
            {
                uint16_t pxv = (v == 2) ? 0xFFFFu : 0x0000u;
                d8[0] = (uint8_t)(pxv & 0xFF);
                d8[1] = (uint8_t)(pxv >> 8);
            }
            else
            {
                /* 8888（ARGB）：精灵全实心（无半透明），黑/白不透明。 */
                d8[0] = 0;                          /* B */
                d8[1] = (v == 2) ? 0xFF : 0x00;     /* G */
                d8[2] = (v == 2) ? 0xFF : 0x00;     /* R */
                d8[3] = 0xFF;                       /* A */
            }
        }
    }
}
#endif /* XCURSOR_ON */

static bool xpad_present(XWindow* window, const XImage* image,
                         const XRegion* region, const XPoint* offset)
{
    ANativeWindow_Buffer buf;
    ANativeWindow* win;
    int32_t winFmt;
    XPadLayer* selfLayer;
    float presentScale;
    XPoint selfDev;
    int selfRot = 0;
    bool anyRot = false;
    bool sourceUnknown = false;
    XRegion frameDirty;
    XRegion work;
    int i;
    bool ok = false;

    if (!window || !image) return false;
    XRegion_init(&frameDirty);
    XRegion_init(&work);
    selfDev.x = 0; selfDev.y = 0;

    /* 短锁①：登记本层图像 + 取 surface 引用 + 读实测放大系数与账目
       快照（锁外使用，先持引用）。 */
    pthread_mutex_lock(&g_xpad.m_mutex);
    win = g_xpad.m_window;
    selfLayer = xpad_findLayer(window);
    if (selfLayer) selfLayer->lastImage = image;
    presentScale = g_xpad.m_presentScale;
    if (win) ANativeWindow_acquire(win);
    /* [脏区直递] 账目快照：设备原点换算 + 各可见层实时取角（与合成段
       同源口径；旋转层 blit 无矩形口径 → 本帧退整幅）。 */
    xpad_updateDeviceOrigins();
    if (selfLayer) {
        selfDev = selfLayer->devPos;
        selfRot = selfLayer->win
                      ? XWindow_contentRotationDegrees(selfLayer->win) : 0;
    }
    for (i = 0; i < g_xpadWindowCount; ++i) {
        XPadLayer* layer = &g_xpadWindows[i];
        if (layer->visible && layer->lastImage && layer->win &&
            XWindow_contentRotationDegrees(layer->win) != 0) {
            anyRot = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (!(presentScale > 0.0f)) presentScale = 1.0f; /* 未标定兜底 */
    /* surface 未就绪：干净退出，等 onNativeWindowCreated 之后的
       surfaceDirty 全窗 expose 再画。严禁持有互斥量早退——上一版在
       无 surface 分支把锁留在自己手里，渲染线程下一次加锁与 UI 线程
       生命周期回调全体挂死（黑屏+卡死根因）。 */
    if (!win) return false;

    if (ANativeWindow_lock(win, &buf, NULL) != 0) {
        ANativeWindow_release(win);
        return false;
    }
    if (!buf.bits || buf.width <= 0 || buf.height <= 0 || buf.stride <= 0) {
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }
    g_xpadWidth = buf.width;
    g_xpadHeight = buf.height;
    winFmt = ANativeWindow_getFormat(win);

    /* 仅支持 565（直拷/转换）与 8888（R/B 重排）两类窗口格式。 */
    if (winFmt != WINDOW_FORMAT_RGB_565 &&
        winFmt != WINDOW_FORMAT_RGBA_8888 &&
        winFmt != WINDOW_FORMAT_RGBX_8888) {
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    /* 本帧账目构建（锁外纯算）：逻辑脏区（窗口本地，扣缓冲偏移）→
       物理 surface 坐标。 */
    sourceUnknown = (selfLayer == NULL || selfRot != 0 || anyRot ||
                     !region || XRegion_isEmpty(region));
    if (!sourceUnknown) {
        XPoint origin;
        origin.x = selfDev.x - (offset ? offset->x : 0);
        origin.y = selfDev.y - (offset ? offset->y : 0);
        for (i = 0; i < region->count; ++i) {
            XRect pr = xpad_dirtyRectToPhys(&region->rects[i], &origin,
                                            presentScale,
                                            buf.width, buf.height);
            XRegion_addRect(&frameDirty, &pr);
        }
    }

    /* 欠账结算（短锁②）：本帧账先记入全部已登记缓冲（源变化未知则
       全部升格欠全屏），再取走本次缓冲的欠账作补画清单并清其账。
       取走-清账后本次绘制在锁外进行（欠账表仅本线程与生命周期清账
       竞争，均持本互斥量）。 */
    {
        XPadBufferLedger* led;
        bool workFull = sourceUnknown;

        pthread_mutex_lock(&g_xpad.m_mutex);
        if (sourceUnknown)
            xpad_bufLedgerMarkAllFullLocked();
        else {
            for (i = 0; i < frameDirty.count; ++i)
                xpad_bufLedgerAccrueRectLocked(&frameDirty.rects[i]);
        }
#if XCURSOR_ON
        /* 光标精灵带记账：上帧带（还原旧精灵）∪本帧带（落新精灵）。 */
        {
            XRect nowBand;
            if (xpad_swCursorGeom(presentScale, buf.width, buf.height,
                                  &nowBand, NULL)) {
                if (g_xpadHasCursorBand)
                    xpad_bufLedgerAccrueRectLocked(&g_xpadLastCursorBand);
                xpad_bufLedgerAccrueRectLocked(&nowBand);
                g_xpadLastCursorBand = nowBand;
                g_xpadHasCursorBand = true;
            }
            else if (g_xpadHasCursorBand) {
                /* 精灵隐没：旧带仍须补画还原。 */
                xpad_bufLedgerAccrueRectLocked(&g_xpadLastCursorBand);
                g_xpadHasCursorBand = false;
            }
        }
#endif /* XCURSOR_ON */
        led = xpad_bufLedgerAcquireLocked((uint8_t*)buf.bits);
        if (led && !led->full) {
            XRegion_copy(&led->pending, &work);
            led->full = false;
            XRegion_clear(&led->pending);
        }
        else {
            workFull = true;   /* 陌生缓冲/欠全屏：整幅补画。 */
            if (led) {
                led->full = false;
                XRegion_clear(&led->pending);
            }
        }
        pthread_mutex_unlock(&g_xpad.m_mutex);

        if (workFull) {
            /* 整幅补画（回退路径）：整屏清黑 + 全图层重画。
               整屏清黑：图层合成模型下窗口外的 surface 区域不属于任何
               图层，不清则保留上一帧残影（最大化→还原后旧全宽标题栏/
               浮窗滞留）。lock 缓冲每帧都可能是新分配的（需初始化）或
               复用的（含旧帧），统一清零最稳妥。 */
            size_t bpp = (winFmt == WINDOW_FORMAT_RGB_565) ? 2u : 4u;
            uint8_t* row8 = (uint8_t*)buf.bits;
            size_t rowBytes = (size_t)buf.stride * bpp;
            int r;
            for (r = 0; r < buf.height; ++r)
                memset(row8 + (size_t)r * rowBytes, 0, rowBytes);

            /*
             * 图层合成（画家算法）：按登记顺序把每个可见图层的最后提交
             * 图像画到 surface 的窗口位置上（主窗先画，弹层按
             * setGeometry 位置后画覆盖其上）。本次提交的图像已在上一步
             * 记入图层表，统一走 lastImage 管线；逐层绘制在锁外进行
             * （图像生命周期由后备存储持有，与 present 调用方同线程，
             * 无竞态）。
             * 全链唯一物理放大点（设计 §0/§5）：图层 pos 为框架逻辑
             * 口径，此处 x 实测系数折算物理像素后绘制（layerPosPx=
             * round(logical x scale)）；scale==1.0 时折算恒等、
             * drawImage 走 memcpy 快路径，dpr=1/WSA 逐位不变。后备存储
             * 保持逻辑尺寸零改动。
             */
            pthread_mutex_lock(&g_xpad.m_mutex);
            /* 设备原点先行换算（弹层落位域）：旋转态弹层要按 opener
               正变换落到设备轴，0°/普通图层恒等。 */
            xpad_updateDeviceOrigins();
            for (i = 0; i < g_xpadWindowCount; ++i) {
                XPadLayer* layer = &g_xpadWindows[i];
                if (layer->visible && layer->lastImage) {
                    XPoint posPx;
                    const XImage* img = layer->lastImage;
                    int rot;
                    /* 落位用**设备轴原点** devPos（旋转态弹层≠逻辑全局
                       pos）。 */
                    posPx.x =
                        (int)((float)layer->devPos.x * presentScale + 0.5f);
                    posPx.y =
                        (int)((float)layer->devPos.y * presentScale + 0.5f);
                    /* [实时取角 2026-10-24] 旋转角**每帧实时**取自窗口，
                       不缓存：安卓主窗几何由系统决定，setGeometry 未必被
                       调用（真机实测主窗全程无 setGeometry 日志），若只
                       在 setGeometry 里记角，主窗的角会永远停在 0 → 切了
                       方向但画面不转。弹层（Popup）已按 transientParent
                       继承主窗的角（XWindow_setTransientParent →
                       xwin_inheritTransientRotation），故这里对所有图层
                       统一实时取角，弹层与主窗同角同源。 */
                    rot = layer->win
                              ? XWindow_contentRotationDegrees(layer->win)
                              : 0;
                    pthread_mutex_unlock(&g_xpad.m_mutex);
                    if (rot != 0)
                        /* 内容旋转：逻辑帧按 R⁻¹ 转置后写进设备
                           footprint（与桌面 posix/win32 同源口径，
                           XWindowRotation.h 唯一样本）。 */
                        xpad_drawImageRotated(&buf, img, &posPx, winFmt,
                                              presentScale, rot);
                    else
                        xpad_drawImage(&buf, img, &posPx, winFmt, presentScale,
                                       NULL); /* clip=NULL=整幅 surface */
                    pthread_mutex_lock(&g_xpad.m_mutex);
                }
            }
            pthread_mutex_unlock(&g_xpad.m_mutex);
        }
        else {
            /* 部分补画（主路径）：不清屏（欠账外像素=本缓冲上次自己画
               的内容，未变），对欠账逐矩形按画家序裁剪补各层。图层快照
               持锁一次取齐（含实时取角；旋转层理论已被 anyRot 闸拦到
               整幅路径，这里仍防御性跳过），逐矩形绘制在锁外。 */
            XPadLayerSnap snaps[XPAD_MAX_WINDOWS];
            int snapCount = 0;
            int ri;

            pthread_mutex_lock(&g_xpad.m_mutex);
            xpad_updateDeviceOrigins();
            for (i = 0; i < g_xpadWindowCount; ++i) {
                XPadLayer* layer = &g_xpadWindows[i];
                if (layer->visible && layer->lastImage && layer->win &&
                    XWindow_contentRotationDegrees(layer->win) == 0) {
                    snaps[snapCount].img = layer->lastImage;
                    snaps[snapCount].posPx.x =
                        (int)((float)layer->devPos.x * presentScale + 0.5f);
                    snaps[snapCount].posPx.y =
                        (int)((float)layer->devPos.y * presentScale + 0.5f);
                    ++snapCount;
                }
            }
            pthread_mutex_unlock(&g_xpad.m_mutex);

            for (ri = 0; ri < work.count; ++ri) {
                for (i = 0; i < snapCount; ++i)
                    xpad_drawImage(&buf, snaps[i].img, &snaps[i].posPx,
                                   winFmt, presentScale, &work.rects[ri]);
            }
        }
    }

    /* [软件光标 2026-10-27] 图层合成后落笔精灵（全屏最顶）。脏区直递
     * 模型下精灵带已随账补画（上帧带还原），此处只管落笔。 */
#if XCURSOR_ON
    xpad_drawSwCursor(&buf, winFmt, presentScale);
#endif /* XCURSOR_ON */

    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);
    XRegion_deinit(&frameDirty);
    XRegion_deinit(&work);
    ok = true;
    return ok;
}

/* ==================== 原生窗口接口（XPlatformNativeWindow.h 契约） ==================== */

/* 哨兵实现约定：本文件与 Unsupported 哨兵同符号，链接期以本文件优先——
   构建系统须保证安卓构建时本目录先于 Unsupported 参与归档。 */
bool XPlatformNativeWindow_isAvailable(void)
{
    return true;
}

static XPadLayer* xpad_findLayer(XWindow* window)
{
    int i;
    for (i = 0; i < g_xpadWindowCount; ++i)
        if (g_xpadWindows[i].win == window) return &g_xpadWindows[i];
    return NULL;
}

static void xpad_registerWindow(XWindow* window)
{
    int i;
    for (i = 0; i < g_xpadWindowCount; ++i)
        if (g_xpadWindows[i].win == window) return;   /* 幂等 */
    if (g_xpadWindowCount < XPAD_MAX_WINDOWS) {
        XRect geom = XWindow_geometry(window);
        if (g_xpadPrimaryWindow == NULL)
            g_xpadPrimaryWindow = window;   /* 首个登记为主窗 */
        g_xpadWindows[g_xpadWindowCount].win = window;
        g_xpadWindows[g_xpadWindowCount].pos.x = geom.x;
        g_xpadWindows[g_xpadWindowCount].pos.y = geom.y;
        g_xpadWindows[g_xpadWindowCount].devPos.x = geom.x;
        g_xpadWindows[g_xpadWindowCount].devPos.y = geom.y;
        g_xpadWindows[g_xpadWindowCount].visible = true;
        g_xpadWindows[g_xpadWindowCount].lastImage = NULL;
        ++g_xpadWindowCount;
        /* [全屏几何回推 2026-10-25] 新窗口登记后必须立刻拿到 surface
         * 逻辑尺寸：surface 可能先于框架窗口创建（onNativeWindowCreated
         * 早于 demo 建窗），那时的几何回推因无窗口可推而丢失。此处补
         * 置 pending，由事件线程在有窗后回推一次。 */
        g_xpad.m_geomPending = true;
        g_xpad.m_geomW = g_xpad.m_logicalW;
        g_xpad.m_geomH = g_xpad.m_logicalH;
        XPAD_LOGI("register: win=%p pos=(%d,%d) size=%dx%d",
                  (void*)window, geom.x, geom.y, geom.width, geom.height);
    }
}

static void xpad_unregisterWindow(XWindow* window)
{
    int i;
    for (i = 0; i < g_xpadWindowCount; ++i) {
        if (g_xpadWindows[i].win == window) {
            g_xpadWindows[i] = g_xpadWindows[--g_xpadWindowCount];
            return;
        }
    }
}

/* ==================== 屏幕登记与密度回填（设计 §1.3/§1.4） ==================== */

/* XScreen 所有权归平台层：登记失败（false）不 delete、保留待重试——
   重试登记模式例外（所有权持续归平台层，成功前不视为泄漏；契约注释
   修订见公共层 PR 之 G4 义务）。 */
static XScreen* g_xpadScreen = NULL;   /**< 安卓唯一逻辑屏（平台层所有）。 */
static bool g_xpadScreenDone = false;  /**< 仅 handleScreenAdded 成功后置位（F6）。 */
static bool g_xpadScreenLogged = false;/**< 登记成功诊断日志一次性哨兵（差分
                                            回填不复打，避免 logcat 刷屏）。 */

/**
 * @brief  登记安卓屏幕并回填 dpr/几何/物理尺寸（仅框架事件线程调用）。
 * @details 线程口径（§1.4 唯一规则）：XObject 信号面（本函数全部
 *          XScreen_* setter 与 handleScreenAdded）只在 demo 事件线程
 *          （=渲染线程）执行；UI 线程回调（surface/config）只写 g_xpad
 *          快照 + m_screenPending，由 processPendingEvents 消费。
 *          回填定版（§0/H1/F8）：
 *          - dpr = DisplayMetrics.density（唯一密度载体）；
 *          - geometry = surface 物理 / dpr（round，框架恒逻辑像素）；
 *          - physicalSize mm = 逻辑px x 25.4 / 设备xdpi —— 由逻辑 px
 *            反推使派生 physicalDpi = 设备 xdpi 真值（XScreen 单一派生
 *            式下与 physicalSize 真值不可兼得，取舍登记 §10-9）；
 *          - logicalDpi 永不回填（保持默认 96；handleScreenLogical
 *            DotsPerInchChange 通道安卓封死——会把 densityDpi 灌进
 *            logicalDpi 触发 dpr²x(densityDpi/96) 复合爆炸）。
 *          重试守卫（F6）：XGuiApplication 未就绪或 handleScreenAdded
 *          失败时不置位、不删对象，下次 pending 消费重试（覆盖“UI 线程
 *          首试 app 未建→事件线程二试成功”竞态）；已登记后的密度变化
 *          走同一函数的差分回填分支（setter 内部值不变即静默，dpr 与
 *          geometry 成对下发）。
 */
static void xpad_ensureScreenRegistered(void)
{
    XRect logicalGeom;
    XSizeF mm;
    float dpr;
    float xdpi;
    float ydpi;
    int logicalW;
    int logicalH;
    int densityDpi;

    pthread_mutex_lock(&g_xpad.m_mutex);
    dpr = g_xpad.m_dpr;
    xdpi = g_xpad.m_xdpi;
    ydpi = g_xpad.m_ydpi;
    logicalW = g_xpad.m_logicalW;
    logicalH = g_xpad.m_logicalH;
    densityDpi = g_xpad.m_densityDpi;
    pthread_mutex_unlock(&g_xpad.m_mutex);

    if (!g_xpadScreenDone && !XGuiApplication_instance())
        return;                        /* 未就绪：保留待重试 */
    if (!g_xpadScreen) {
        g_xpadScreen = XScreen_create();
        if (!g_xpadScreen) return;
    }
    if (logicalW > 0 && logicalH > 0 && xdpi > 0.0f && ydpi > 0.0f) {
        XRect_init(&logicalGeom, 0, 0, logicalW, logicalH);
        mm.width = (float)logicalW * 25.4f / xdpi;   /* F8：逻辑 px 反推 */
        mm.height = (float)logicalH * 25.4f / ydpi;
    }
    else {
        /* surface 未标定（异常时序）：上报空几何保登记通道，等下次
           pending 带新标定值差分更新。 */
        XRect_init(&logicalGeom, 0, 0, 0, 0);
        mm.width = 0.0f;
        mm.height = 0.0f;
    }
    if (!g_xpadScreenDone) {
        if (!XWindowSystemInterface_handleScreenAdded(g_xpadScreen))
            return;                    /* 失败：不删（所有权在平台层）、
                                          不置守卫，下次 pending 重试 */
        XGuiApplication_setPrimaryScreen(g_xpadScreen);
        g_xpadScreenDone = true;
    }
    /* 回填（首次登记与运行期差分共用，dpr 与 geometry 成对下发）。
       时序定版=先登记后回填：XGuiApplication_screenAdded 内部挂接的
       dpr/logicalDpi 变化槽（公共层 §1.6 闭环）靠 setter 的值变化信号
       把 dpr 推进已建窗口并失效字体度量表——先回填后登记会把首帧
       dpr 推送发在挂槽之前而丢失。setter 差分静默，重复回填无抖动。
       上报统一走 WSI 推送面（对标 Qt QWindowSystemInterface::
       handleScreenDevicePixelRatioChange + handleScreenGeometryChange），
       平台层不再直调 XScreen_set*。 */
    XWindowSystemInterface_handleScreenDevicePixelRatioChange(g_xpadScreen,
                                                              dpr, &mm);
    XWindowSystemInterface_handleScreenGeometryChange(g_xpadScreen,
                                                      &logicalGeom, NULL);
    if (g_xpadScreenDone && !g_xpadScreenLogged) {
        g_xpadScreenLogged = true;
        XPAD_LOGI("screen registered: dpr=%.2f densityDpi=%d logical=%dx%d "
                  "mm=%.1fx%.1f xdpi=%.0f ydpi=%.0f",
                  dpr, densityDpi, logicalW, logicalH, mm.width, mm.height,
                  xdpi, ydpi);
    }
}

bool XPlatformNativeWindow_create(XWindow* window)
{
    /* 安卓无独立创建步骤：NativeActivity 的 surface 就是窗口。
       surface 事件（onNativeWindowCreated）通常早于 demo 线程的窗口
       登记——dirty 若在那次已被消费（当时尚无窗口登记），这里必须
       补挂，否则初始 expose 丢失、首帧永不绘制（Win32 靠系统重发
       WM_PAINT 无此问题，安卓 surface 事件一次性）。 */
    if (!window) return false;
    pthread_mutex_lock(&g_xpad.m_mutex);
    xpad_registerWindow(window);
    if (g_xpad.m_window) g_xpad.m_surfaceDirty = true;
    /* 登记点 2（demo 事件线程）：屏幕登记同样经 pending 槽由本线程
       processPendingEvents 消费（§1.3 两登记点统一收口 §1.4）。 */
    g_xpad.m_screenPending = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("create: window registered (%d total), surface=%s",
              g_xpadWindowCount, g_xpad.m_window ? "yes" : "no");
    return true;
}

bool XPlatformNativeWindow_attachForeign(XWindow* window, XWindowId nativeId)
{
    if (!window) return false;
    (void)nativeId;
    pthread_mutex_lock(&g_xpad.m_mutex);
    xpad_registerWindow(window);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return true;
}

void XPlatformNativeWindow_destroy(XWindow* window)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    xpad_unregisterWindow(window);
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

bool XPlatformNativeWindow_setVisible(XWindow* window, bool visible)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    {
        XPadLayer* layer = xpad_findLayer(window);
        if (layer) layer->visible = visible;
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return true;
}

bool XPlatformNativeWindow_setGeometry(XWindow* window, const XRect* geometry)
{
    /* 安卓主窗几何由系统决定；弹层（菜单/Popup）的位置经此处下发——
       记录到图层表供 present 按位叠加。
       [内容旋转 2026-10-24 安卓旋转接入] 旋转角**不在此处缓存**：
       安卓主窗几何由系统决定，setGeometry 未必被调用（真机实测主窗全程
       无 setGeometry），缓存会停在 0°。present 与输入两侧都改为**每帧
       实时**取 XWindow_contentRotationDegrees（弹层已按 transientParent
       继承主窗的角），故此处只管位置。 */
    if (!window || !geometry) return false;
    pthread_mutex_lock(&g_xpad.m_mutex);
    {
        XPadLayer* layer = xpad_findLayer(window);
        if (layer) {
            layer->pos.x = geometry->x;
            layer->pos.y = geometry->y;
        }
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("setGeometry: win=%p pos=(%d,%d) size=%dx%d rot=%d",
              (void*)window, geometry->x, geometry->y,
              geometry->width, geometry->height,
              XWindow_contentRotationDegrees(window));
    return true;
}

bool XPlatformNativeWindow_setWindowState(XWindow* window,
                                          uint32_t state)
{
    /* 最大化：向框架回推整屏几何使控件树重排（surface 本身始终是
       全屏画布，窗口尺寸是图层属性，不动缓冲几何）。最小化：经 JNI
       moveTaskToBack 退到安卓后台（保持进程存活），回前台由 Activity
       生命周期回调自然恢复。还原（NoState）：回落常规 800x600 几何。
       本入口由框架事件处理调用，调用方不持本模块互斥量——严禁在
       入口处 unlock 不属于自己的锁（UB），也严禁持有互斥量跨框架
       调用（handleGeometryChange 会同步重排并回 present 抢锁）。 */
    XRect target;

    if (!window) return false;

    if (state & 0x00000001u /* XWindowState_Minimized */) {
        XAndroid_moveTaskToBack();
        return true;
    }

    if (state & 0x00000006u /* Maximized | FullScreen */) {
        int fw = 0;
        int fh = 0;
        float dpr;
        bool logicalFrame;
        if (!XPlatformScreen_queryFrameSize(&fw, &fh) || fw <= 0 || fh <= 0)
            return false;
        /* §4.3：frameSize 按标定口径换算成框架逻辑 px——LOGICAL 标定下
           frameSize 已是逻辑值直用；PHYSICAL 标定下为物理 px，÷dpr
           （round）后下发（handleGeometryChange 进框架逻辑坐标系）。 */
        pthread_mutex_lock(&g_xpad.m_mutex);
        dpr = g_xpad.m_dpr;
        logicalFrame = (g_xpad.m_viewSpace == XPAD_VIEW_SPACE_LOGICAL);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        if (!logicalFrame && dpr > 0.0f) {
            fw = (int)((float)fw / dpr + 0.5f);
            fh = (int)((float)fh / dpr + 0.5f);
        }
        /* [旋转感知 2026-10-24] 90/270 时窗口的**逻辑**尺寸必须取对调后的
           (fh, fw)：内容旋转的设备 footprint = 逻辑宽高对调（XWindowRotation.h
           口径），只有逻辑尺寸 = (fh, fw) 时 footprint 才恰等于面板矩形
           (fw, fh) → 内容铺满。沿用未对调的 (fw, fh) 会让 footprint 变成
           另一个矩形 → 内容被裁、面板留黑（用户实测：最大化态切 90° 内容
           只占半屏、右侧全黑；全屏态同理）。0/180 不换轴。
           注：presentScale 是**屏级**系数（surfW / 屏逻辑宽），与窗口尺寸
           无关，故换向后放大倍数保持一致，无需另算。 */
        if (XWindow_rotationSwapsAxes(XWindow_contentRotationDegrees(window))) {
            int t = fw;
            fw = fh;
            fh = t;
        }
        XRect_init(&target, 0, 0, fw, fh);
        XWindow_setGeometry_rect(window, &target);
        XWindowSystemInterface_handleGeometryChange(window, &target);
        pthread_mutex_lock(&g_xpad.m_mutex);
        g_xpad.m_surfaceDirty = true;
        pthread_cond_broadcast(&g_xpad.m_cond);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        XPAD_LOGI("maximize: geometry set to %dx%d (frame %s)",
                  fw, fh, logicalFrame ? "logical" : "physical/dpr");
        return true;
    }

    /* 还原（NoState）：[手机语义 2026-10-25] 手机没有"窗口化"——退出
       全屏/还原仍回落**全屏几何**（与最大化同口径：queryFrameSize ÷dpr
       + 旋转对调），只把框架状态位翻回 Normal 供窗口设置页回读。沿用
       800x600 会在手机上把窗口缩成画布一角（画布恒全屏，窗口外露黑）。 */
    {
        int fw = 0;
        int fh = 0;
        float dpr;
        bool logicalFrame;
        if (XPlatformScreen_queryFrameSize(&fw, &fh) && fw > 0 && fh > 0) {
            pthread_mutex_lock(&g_xpad.m_mutex);
            dpr = g_xpad.m_dpr;
            logicalFrame = (g_xpad.m_viewSpace == XPAD_VIEW_SPACE_LOGICAL);
            pthread_mutex_unlock(&g_xpad.m_mutex);
            if (!logicalFrame && dpr > 0.0f) {
                fw = (int)((float)fw / dpr + 0.5f);
                fh = (int)((float)fh / dpr + 0.5f);
            }
            if (XWindow_rotationSwapsAxes(
                    XWindow_contentRotationDegrees(window))) {
                int t = fw;
                fw = fh;
                fh = t;
            }
            XRect_init(&target, 0, 0, fw, fh);
        } else {
            XRect_init(&target, 0, 0, 800, 600); /* 查询失败兜底。 */
        }
        XWindow_setGeometry_rect(window, &target);
        XWindowSystemInterface_handleGeometryChange(window, &target);
        pthread_mutex_lock(&g_xpad.m_mutex);
        g_xpad.m_surfaceDirty = true;
        pthread_cond_broadcast(&g_xpad.m_cond);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        XPAD_LOGI("restore: geometry set to %dx%d (phone=fullscreen)",
                  target.width, target.height);
        return true;
    }
}

bool XPlatformNativeWindow_deferGeometry(XWindow* window, bool deferred)
{
    /* 安卓单 surface：无平台级“延迟几何”通道，直接确认即可。 */
    (void)window; (void)deferred;
    return false;
}

bool XPlatformNativeWindow_setWindowFlags(XWindow* window, uint32_t flags)
{
    (void)window; (void)flags;
    return true;
}

bool XPlatformNativeWindow_setTitle(XWindow* window, const XString* title)
{
    (void)window; (void)title;
    return true;
}

bool XPlatformNativeWindow_setIcon(XWindow* window, const XIcon* icon)
{
    /* 安卓无运行期窗口图标通道（NativeActivity surface 无 WM 概念）：
       启动器/任务切换图标走 APK mipmap 资源（AndroidManifest
       android:icon=@mipmap/ic_launcher，res/mipmap-* 由打包脚本入包），
       与桌面（X11 _NET_WM_ICON / Win32 类资源图标）互为两套口径。 */
    (void)window; (void)icon;
    return true;
}

bool XPlatformNativeWindow_setSizeHints(XWindow* window)
{
    (void)window; /* Android 无 WM 尺寸约束语义，no-op。 */
    return true;
}

bool XPlatformNativeWindow_setTransientParent(XWindow* window, XWindow* parent)
{
    /* [安卓契约补齐 2026-10-24] 安卓无 WM，无 owner/瞬态父的 Z 序语义，
     * 与 Unsupported 存根同口径：no-op 且返回 true（公共层据此视为已落地）。
     *
     * 缺这个实现的直接后果：全新配置目录链接 libxguidemo.so 必报
     * "undefined symbol: XPlatformNativeWindow_setTransientParent"
     * （引用点 Src/XGui/Window/XWindow.c 的 XWindow_setTransientParent，
     *  守卫 XPLATFORMNATIVEWINDOW_ON=1，平台无关故各平台都需要）。
     * 此前 x86_64 能链接是侥幸——其 libXinYueCS.a 是陈旧归档，符号来自
     * 旧的 Unsupported 存根；一旦重新配置/全新目录即暴露。 */
    (void)window; (void)parent;
    return true;
}

bool XPlatformNativeWindow_setKeyboardGrabEnabled(XWindow* window, bool grab)
{ (void)window; (void)grab; return true; }

bool XPlatformNativeWindow_setMouseGrabEnabled(XWindow* window, bool grab)
{ (void)window; (void)grab; return true; }

bool XPlatformNativeWindow_requestActivate(XWindow* window)
{ (void)window; return true; }

bool XPlatformNativeWindow_raise(XWindow* window)
{ (void)window; return true; }

bool XPlatformNativeWindow_lower(XWindow* window)
{ (void)window; return true; }

XPixmap* XPlatformNativeWindow_grabWindow(XWindowId window,
                                          int x, int y, int w, int h)
{ (void)window; (void)x; (void)y; (void)w; (void)h; return NULL; }

XWindowId XPlatformNativeWindow_winId(const XWindow* window)
{
    (void)window;
    return 1u;   /* 单窗模型固定哨兵 ID */
}

XWindow* XPlatformNativeWindow_windowForWinId(XWindowId id)
{
    XWindow* w;
    if (id != 1u) return NULL;
    pthread_mutex_lock(&g_xpad.m_mutex);
    w = (g_xpadWindowCount > 0) ? g_xpadWindows[g_xpadWindowCount - 1].win : NULL;
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return w;
}

bool XPlatformNativeWindow_processPendingEvents(void)
{
    bool had = false;
    /* [2026-10-24 安卓无障碍接入] 每帧重建无障碍快照：demo 走
       XCoreApplication_exec 循环（不经 XGuiApplication_processEvents），
       故无障碍泵挂在平台层事件泵上——渲染线程、每帧、控件树安全。
       快照供 Java 侧 AccessibilityNodeProvider 读取（TalkBack）。 */
    {
        extern void xa11y_pumpSnapshot(void);
        xa11y_pumpSnapshot();
    }
    pthread_mutex_lock(&g_xpad.m_mutex);
    /* Activity 已销毁后不再向框架注入任何事件（窗口树可能正在析构）。 */
    if (g_xpad.m_destroyed) {
        pthread_mutex_unlock(&g_xpad.m_mutex);
        return false;
    }

    /* ---- 0. 屏幕登记/密度回填（§1.3/§1.4）：XObject 信号面只在事件
     *        线程执行；UI 线程（surface/config 回调）只置 pending 槽。
     *        登记失败（app 未建）在函数内保留重试，下次 pending 再试。
     *        消费后向 surfaceDirty 传递：登记改变 dpr/几何需要全窗重绘
     *        的场景由后续 pending 的 expose 覆盖。 ---- */
    if (g_xpad.m_screenPending) {
        g_xpad.m_screenPending = false;
        pthread_mutex_unlock(&g_xpad.m_mutex);
        xpad_ensureScreenRegistered();
        pthread_mutex_lock(&g_xpad.m_mutex);
    }

    /* ---- 1. surface 变化：广播 Expose（不注入几何——resize 重排会清
     *        手工布局位置，见 expose 注释）。expose 矩形=逻辑全窗
     *        （§4.3：框架坐标恒逻辑口径；dpr=1 时与物理逐位一致）。 ---- */
    if (g_xpad.m_surfaceDirty && g_xpadWindowCount > 0) {
        int i;
        int count = g_xpadWindowCount;
        int logicalW = g_xpad.m_logicalW;
        int logicalH = g_xpad.m_logicalH;
        XWindow* targets[XPAD_MAX_WINDOWS];
        for (i = 0; i < count; ++i) targets[i] = g_xpadWindows[i].win;
        g_xpad.m_surfaceDirty = false;
        had = true;
        pthread_mutex_unlock(&g_xpad.m_mutex);
        {
            XRegion region;
            XRect full;
            full.x = 0; full.y = 0;
            full.width = logicalW > 0 ? logicalW : g_xpadWidth;
            full.height = logicalH > 0 ? logicalH : g_xpadHeight;
            XRegion_init(&region);
            XRegion_addRect(&region, &full);
            for (i = 0; i < count; ++i)
                XWindowSystemInterface_handleExposeEvent(targets[i], &region);
            XRegion_deinit(&region);
        }
        pthread_mutex_lock(&g_xpad.m_mutex);
    }

    /* ---- 1.5 全屏几何回推（2026-10-25）：框架主窗几何跟随 surface ----
     *        安卓无窗口管理器，surface 即全部画布：框架窗口必须按
     *        surface 的逻辑尺寸排布，否则布局尺寸与画布不匹配（内容被
     *        presentScale 放大后裁切/留边）。本处回推同时产生框架的
     *        几何事件（RESIZE）→ 控件树重排 → 竖横屏切换后布局自适应。
     *        严禁持 m_mutex 跨框架调用（handleGeometryChange 同步重排
     *        并回 present 抢锁），故取快照后解锁再推。 */
    if (g_xpad.m_geomPending && g_xpadWindowCount > 0) {
        XWindow* primary = g_xpadPrimaryWindow;
        int gw = g_xpad.m_geomW;
        int gh = g_xpad.m_geomH;
        g_xpad.m_geomPending = false;
        had = true;
        pthread_mutex_unlock(&g_xpad.m_mutex);
        if (primary && gw > 0 && gh > 0) {
            XRect rect;
            XRect_init(&rect, 0, 0, gw, gh);
            XWindow_setGeometry_rect(primary, &rect);
            XWindowSystemInterface_handleGeometryChange(primary, &rect);
            /* [启动状态对齐 2026-10-27] 框架主窗软件状态停在 NoState，
               而窗口设置页「全屏显示」回读 isFullScreen——与用户所见
               （沉浸式恒满屏）矛盾（真机反馈）。对齐方式=**读取平台
               真实状态**（用户裁定，不硬编码）：JNI 比较应用窗口边界与
               显示真实边界——全屏则推 FullScreen 软件态；窗口化形态
               （自由窗口/分屏）保持 NoState。仅**首次**几何回推时读取
               对齐；此后用户经窗口设置页自由切换，旋转等后续回推不再
               重推。 */
            if (!g_xpad.m_startupFullscreenPushed) {
                bool fs = XAndroid_isWindowFullscreen();
                g_xpad.m_startupFullscreenPushed = true;
                if (fs) {
                    XWindowSystemInterface_handleWindowStateChanged(
                        primary, XWindowState_FullScreen);
                }
                XPAD_LOGI("startup state aligned: fullscreen=%d", (int)fs);
            }
            XPAD_LOGI("geometry pushed: %dx%d (surface logical)", gw, gh);
        }
        pthread_mutex_lock(&g_xpad.m_mutex);
    }

    /* ---- 2. 触摸注入（多点帧；目标 = 最后一个可见图层；注入在锁外） ---- */
    {
        XPadLayer* targetLayer = NULL;
        int ti;
        /* 设备原点先行换算：命中判定用的图层原点必须是**设备轴**原点
           （旋转态弹层与主窗的 devPos 不同，见 xpad_updateDeviceOrigins）。
           present 侧已算过一遍，这里再算一次以覆盖「首帧前注入」与
           「弹层几何在两次 present 之间变更」两种情形（幂等、纯算）。 */
        xpad_updateDeviceOrigins();
        for (ti = g_xpadWindowCount - 1; ti >= 0; --ti)
            if (g_xpadWindows[ti].visible) { targetLayer = &g_xpadWindows[ti]; break; }
        if (targetLayer) {
            XWindow* target = targetLayer->win;
            XPadTouchEvent frame;
            size_t got;
            while ((got = XRingBuffer_read(&g_xpad.m_touchQueue, &frame,
                                           sizeof(frame))) == sizeof(frame)) {
                XEventType type = XEVENT_TYPE_NONE;
                XTouchPoint pts[XPAD_MAX_TOUCH_POINTS];
                int32_t baseAction = frame.action & AMOTION_EVENT_ACTION_MASK;
                int actionIndex = (int)((frame.action &
                    AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                    AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
                int n = frame.count;
                int i;
                had = true;
                if (n <= 0) continue;          /* 畸形空帧：不可注入 */
                if (n > XPAD_MAX_TOUCH_POINTS) n = XPAD_MAX_TOUCH_POINTS;
                if (actionIndex >= n) actionIndex = n - 1;  /* 畸形下标兜底 */
                /* 移动压缩（降低低配主机拖拽负载；与本仓 fbdev/X11/
                   win32 输入泵同圈口径）：队列里紧随的仍是 MOVE 帧时
                   丢弃当前帧——拖拽语义按绝对位置无状态重算，中间移动
                   帧丢尾无损（对标 Qt 触摸输入合帧）；末帧成为队首时
                   正常注入，重排频率自动钳到事件循环圈频率。偷看不
                   移除（peek），非 MOVE 即断链照常注入，沿时序逐位不
                   变。 */
                if (baseAction == AMOTION_EVENT_ACTION_MOVE) {
                    XPadTouchEvent next;
                    if (XRingBuffer_peek(&g_xpad.m_touchQueue, &next,
                                         sizeof(next)) == sizeof(next) &&
                        (next.action & AMOTION_EVENT_ACTION_MASK) ==
                            AMOTION_EVENT_ACTION_MOVE)
                        continue; /* 后面还有 MOVE：本帧折叠。 */
                    /* [跨圈帧界钳 2026-10-27 接线] 折叠后仍到队首的 MOVE
                       再过时间门：距上次注入不足 XGUI_MOVE_FRAME_MS 毫秒
                       则本帧不入（位置由后续放行帧按绝对坐标补齐；60Hz
                       报点=隔帧放行 30fps，120Hz+=每 2~5 帧放行一次，拖
                       拽跟手性与 25ms 帧界一致）。按下/抬起不受此门。 */
                    if (g_xpad.m_moveFrameMs > 0) {
                        int64_t nowMs = xpad_monoMs();
                        if (g_xpad.m_lastMoveCommitMs != 0 &&
                            nowMs - g_xpad.m_lastMoveCommitMs <
                                (int64_t)g_xpad.m_moveFrameMs)
                            continue; /* 帧界内：本帧不注入。 */
                        g_xpad.m_lastMoveCommitMs = nowMs;
                    }
                }
                /* 动作映射（K3 多点收口，对标 QWindowSystemInterface::
                   handleTouchEvent 的 QEventPoint 列表形态）：
                   - DOWN         → BEGIN（单点 PRESSED）
                   - POINTER_DOWN → BEGIN（actionIndex 点 PRESSED、其余
                     STATIONARY——新指落屏携带全帧快照，框架按点命中/
                     按 id 抓取）
                   - MOVE         → UPDATE（坐标较上帧变化者 UPDATED、
                     未变者 STATIONARY——按 m_lastPts 缓存逐点判定）
                   - POINTER_UP   → END（actionIndex 点 RELEASED、其余
                     STATIONARY）
                   - UP           → END（末点 RELEASED）
                   - CANCEL       → CANCEL（全部 RELEASED） */
                switch (baseAction) {
                case AMOTION_EVENT_ACTION_DOWN:
                    type = XEVENT_TYPE_TOUCH_BEGIN;
                    for (i = 0; i < n; ++i)
                        pts[i].m_state = XTOUCHPOINT_STATE_PRESSED;
                    break;
                case AMOTION_EVENT_ACTION_POINTER_DOWN:
                    type = XEVENT_TYPE_TOUCH_BEGIN;
                    for (i = 0; i < n; ++i)
                        pts[i].m_state = (i == actionIndex)
                            ? XTOUCHPOINT_STATE_PRESSED
                            : XTOUCHPOINT_STATE_STATIONARY;
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                    type = XEVENT_TYPE_TOUCH_UPDATE;
                    /* 逐点对照上次坐标缓存（m_lastPts）：坐标未变=静止
                       伴指，标 STATIONARY（控件层只派发 UPDATED 点，全帧
                       UPDATED 会把静止伴指逐帧重投到抓取靶，同 Win32
                       修复口径）；缓存无此 id（队列重置后的首帧等）保守
                       标 UPDATED，维持旧行为下限。 */
                    for (i = 0; i < n; ++i) {
                        int j;
                        bool moved = true;
                        for (j = 0; j < g_xpad.m_lastCount; ++j) {
                            if (g_xpad.m_lastPts[j].id == frame.pts[i].id) {
                                moved = (g_xpad.m_lastPts[j].x !=
                                             frame.pts[i].x ||
                                         g_xpad.m_lastPts[j].y !=
                                             frame.pts[i].y);
                                break;
                            }
                        }
                        pts[i].m_state = moved
                            ? XTOUCHPOINT_STATE_UPDATED
                            : XTOUCHPOINT_STATE_STATIONARY;
                    }
                    break;
                case AMOTION_EVENT_ACTION_POINTER_UP:
                    type = XEVENT_TYPE_TOUCH_END;
                    for (i = 0; i < n; ++i)
                        pts[i].m_state = (i == actionIndex)
                            ? XTOUCHPOINT_STATE_RELEASED
                            : XTOUCHPOINT_STATE_STATIONARY;
                    break;
                case AMOTION_EVENT_ACTION_UP:
                    type = XEVENT_TYPE_TOUCH_END;
                    for (i = 0; i < n; ++i)
                        pts[i].m_state = XTOUCHPOINT_STATE_RELEASED;
                    break;
                case AMOTION_EVENT_ACTION_CANCEL:
                    type = XEVENT_TYPE_TOUCH_CANCEL;
                    for (i = 0; i < n; ++i)
                        pts[i].m_state = XTOUCHPOINT_STATE_RELEASED;
                    break;
                case AMOTION_EVENT_ACTION_HOVER_MOVE:
                    /* 笔悬停（悬停语义=无触点按压，帧 count>0 但非按压
                       状态，触摸四态无法表达）：保留既有单点鼠标 MOVE
                       注入，供悬浮光标/悬停预热（buttons 恒 NoButton，
                       与 Android hover 仅在无按压时上报的契约一致）。 */
                    {
                        XPoint pos;
                        XPoint global;
                        /* BlueStacks 等报屏幕坐标，WSA 报窗口本地坐标——
                           按图层登记的窗口原点换算（原点 0 平台不受影响）；
                           单 surface 模型下表面坐标即全局坐标。 */
                        pos.x = frame.pts[0].x - targetLayer->devPos.x;
                        pos.y = frame.pts[0].y - targetLayer->devPos.y;
                        global.x = frame.pts[0].x;
                        global.y = frame.pts[0].y;
                        pthread_mutex_unlock(&g_xpad.m_mutex);
                        XWindowSystemInterface_handleMouseEvent_ex(
                            target, XEVENT_TYPE_MOUSE_MOVE,
                            XMouseButton_NoButton, XMouseButton_NoButton,
                            XKeyboardModifier_NoModifier, pos, &global,
                            frame.timestampMs);
                        pthread_mutex_lock(&g_xpad.m_mutex);
                    }
                    continue;
                default:
                    continue;   /* 其余动作（HOVER_ENTER/EXIT/SCROLL 等，
                                   与既有口径一致）静默丢弃 */
                }
                /* 逐点填充触点负载：本地=帧坐标减图层原点（同上换算）；
                   global=表面坐标（单 surface 模型下即全局坐标，沿用旧
                   口径——CSD 拖拽锚依赖全局坐标算增量）；无压力 valuator
                   按 1.0（XWindowEvent.h 契约）。
                   [内容旋转 2026-10-24 安卓旋转接入] 帧坐标是**表面逻辑
                   坐标**，减去图层原点后得到图层内的**设备轴**坐标；旋转
                   态下还需经 R⁻¹ 回到内容坐标（与 present 侧 R⁻¹ 转置互为
                   逆运算，同一 XWindowRotation.h 口径）。基准 W/H 取该
                   图层图像的逻辑宽高——即整层逻辑尺寸，与 present 侧
                   xpad_drawImageRotated 用的 w/h 完全一致。 */
                for (i = 0; i < n; ++i) {
                    pts[i].m_id = frame.pts[i].id;
                    pts[i].m_position.x = frame.pts[i].x - targetLayer->devPos.x;
                    pts[i].m_position.y = frame.pts[i].y - targetLayer->devPos.y;
                    /* 旋转角实时取自窗口（与 present 侧同源，避免两端口径分叉）。
                       基准 W/H 取该图层图像的逻辑宽高——与 present 侧
                       xpad_drawImageRotated 用的 w/h 完全一致。 */
                    if (targetLayer->win &&
                        XWindow_contentRotationDegrees(targetLayer->win) != 0 &&
                        targetLayer->lastImage) {
                        int rot = XWindow_contentRotationDegrees(targetLayer->win);
                        int lw = XImage_width(targetLayer->lastImage);
                        int lh = XImage_height(targetLayer->lastImage);
                        int lx = 0, ly = 0;
                        if (lw > 0 && lh > 0) {
                            XWindow_rotationUnmapPoint(rot, lw, lh,
                                                      pts[i].m_position.x,
                                                      pts[i].m_position.y,
                                                      &lx, &ly);
                            pts[i].m_position.x = lx;
                            pts[i].m_position.y = ly;
                        }
                    }
                    pts[i].m_globalPosition.x = frame.pts[i].x;
                    pts[i].m_globalPosition.y = frame.pts[i].y;
                    pts[i].m_pressure = 1.0f;
                }
                /* 帧后刷新坐标缓存：UP（全部抬起）/CANCEL 清空，残快照
                   不得泄入下一触摸序列；其余动作整体替换为当前帧快照
                   （POINTER_UP 的离屏点随下一帧自然剔除）。仍在锁内。 */
                if (baseAction == AMOTION_EVENT_ACTION_UP ||
                    baseAction == AMOTION_EVENT_ACTION_CANCEL) {
                    g_xpad.m_lastCount = 0;
                }
                else {
                    int ci;
                    for (ci = 0; ci < n; ++ci)
                        g_xpad.m_lastPts[ci] = frame.pts[ci];
                    g_xpad.m_lastCount = n;
                }
                pthread_mutex_unlock(&g_xpad.m_mutex);
                /* [软件光标喂位 2026-10-27] 触摸帧即光标位置来源（安卓
                 * 无指针设备，手指所在即「鼠标」位置；窗口设置页的
                 * 「显示鼠标光标」开关据此绘制精灵）。
                 *
                 * **不在此处请求重绘**：光标单独移动（手指在空白处滑动、
                 * 无控件脏区）时不产生 PAINT/flush，精灵位置会滞后到下一
                 * 次重绘——这与 fbdev 侧「空白处移动要不要搬运可见缓冲」
                 * 是同一个取舍，fbdev 用专有搬运通道解决；present 侧已改
                 * 脏区直递（精灵带上帧带还原+本帧带落笔都走欠账），此后
                 * 若要「空白处光标即时跟随」，只需在此以带状脏区请求局部
                 * 重绘即可（成本=两条 32px 带的补画），暂保持「随帧更新」
                 * 不引入额外重绘请求。
                 *
                 * 此前工作树版本在这里无条件 handlePaintEvent（整窗
                 * 重绘），触摸报点 100~400Hz 时逐帧触发 ⇒ 真机实测 CPU
                 * 满核、FPS 104、画面残留重。 */
#if XCURSOR_ON
                if (n > 0 && targetLayer && targetLayer->win) {
                    int swRot = XWindow_contentRotationDegrees(
                                    targetLayer->win);
                    int swW = targetLayer->lastImage
                                  ? XImage_width(targetLayer->lastImage) : 0;
                    int swH = targetLayer->lastImage
                                  ? XImage_height(targetLayer->lastImage) : 0;
                    XCursor_swSetPosFrame(swRot, swW, swH,
                                          &targetLayer->devPos);
                    XCursor_setPos(pts[0].m_globalPosition.x,
                                   pts[0].m_globalPosition.y);
                }
#endif /* XCURSOR_ON */
                XWindowSystemInterface_handleTouchPoints_ex(target, type, pts,
                                                            n,
                                                            frame.timestampMs);
                if (baseAction != AMOTION_EVENT_ACTION_MOVE)
                    XPAD_LOGI("inject touch: type=%d count=%d primary=(%d,%d)",
                              (int)type, n, pts[0].m_position.x,
                              pts[0].m_position.y);
                pthread_mutex_lock(&g_xpad.m_mutex);
            }
        }
    }

    /* ---- 3. 键盘注入（[2026-10-24] 与触摸同通道：目标=最后可见图层；
       帧翻译已在 UI 线程完成，这里只投递。整段在锁外调用 WSI，与
       触摸段同一纪律。） ---- */
    if (g_xpadWindowCount > 0) {
        XWindow* keyTarget = NULL;
        int ki;
        for (ki = g_xpadWindowCount - 1; ki >= 0; --ki)
            if (g_xpadWindows[ki].visible) { keyTarget = g_xpadWindows[ki].win; break; }
        if (keyTarget) {
            XPadKeyEvent keyFrame;
            size_t keyGot;
            while ((keyGot = XRingBuffer_read(&g_xpad.m_keyQueue, &keyFrame,
                                              sizeof(keyFrame))) ==
                   sizeof(keyFrame)) {
                had = true;
                pthread_mutex_unlock(&g_xpad.m_mutex);
                XWindowSystemInterface_handleKeyEvent_ex(
                    keyTarget, (XEventType)keyFrame.type, keyFrame.key,
                    (XKeyboardModifiers)keyFrame.modifiers,
                    keyFrame.autoRepeat, (uint32_t)keyFrame.scanCode,
                    keyFrame.timestampMs);
                XPAD_LOGI("inject key: type=%d key=0x%x mods=0x%x",
                          (int)keyFrame.type, (unsigned)keyFrame.key,
                          (unsigned)keyFrame.modifiers);
                pthread_mutex_lock(&g_xpad.m_mutex);
            }
        }
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return had;
}

bool XPlatformNativeWindow_refreshScreenLogicalDpi(void)
{
    /* 安卓 logicalDpi 恒保持默认 96、永不回填（H1 封死）：经
       handleScreenLogicalDotsPerInchChange 灌入 densityDpi 会触发
       dpr²x(densityDpi/96) 复合爆炸；密度唯一载体是 dpr（§0 标定定
       版），运行期密度变化走 XPad_onConfigurationChanged 的 pending
       槽差分回填（setDevicePixelRatio+setGeometry 成对）。通道本身
       保留给桌面（posix RR 现网 / win32 WM_DPICHANGED 未来批次）。 */
    return true;
}

/* [2026-10-24 安卓剪贴板接入] 后端实现在 Drive/Android/Core/XClipboardAndroid.c
   （JNI ClipboardManager 单格式文本），此处仅转发安装。 */
void XAndroidClipboard_installBackend(void);

void XPlatformNativeWindow_installClipboardBackend(void)
{
    XAndroidClipboard_installBackend();
}

bool XPlatformNativeWindow_waitForEvents(int maxMilliseconds)
{
    /* 渲染线程睡在条件变量上；UI 线程事件/surface 变化 signal 唤醒。 */
    struct timespec ts;
    int rc;
    if (maxMilliseconds < 0) {
        pthread_mutex_lock(&g_xpad.m_mutex);
        while (!(g_xpad.m_queueReady &&
                 (XRingBuffer_available(&g_xpad.m_touchQueue) >=
                      (size_t)sizeof(XPadTouchEvent) ||
                  XRingBuffer_available(&g_xpad.m_keyQueue) >=
                      (size_t)sizeof(XPadKeyEvent))) &&
               !g_xpad.m_surfaceDirty && !g_xpad.m_destroyed)
            pthread_cond_wait(&g_xpad.m_cond, &g_xpad.m_mutex);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        return true;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += maxMilliseconds / 1000;
    ts.tv_nsec += (long)(maxMilliseconds % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ++ts.tv_sec;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&g_xpad.m_mutex);
    rc = 0;
    while (!(g_xpad.m_queueReady &&
             (XRingBuffer_available(&g_xpad.m_touchQueue) >=
                  (size_t)sizeof(XPadTouchEvent) ||
              XRingBuffer_available(&g_xpad.m_keyQueue) >=
                  (size_t)sizeof(XPadKeyEvent))) &&
           !g_xpad.m_surfaceDirty && !g_xpad.m_destroyed && rc == 0)
        rc = pthread_cond_timedwait(&g_xpad.m_cond, &g_xpad.m_mutex, &ts);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return true;
}

bool XPlatformNativeWindow_queryKeyboardModifiers(
        XKeyboardModifiers* outModifiers)
{
    /* [2026-10-24 平台补扫] 由键盘帧维护的修饰键位快照（物理键盘场景；
       触摸为主时恒 NoModifier——与 win32 GetKeyState 通道语义对齐）。 */
    if (!outModifiers) return false;
    pthread_mutex_lock(&g_xpad.m_mutex);
    *outModifiers = (XKeyboardModifiers)g_xpad.m_keyModifiers;
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return true;
}

bool XPlatformNativeWindow_present(XWindow* window, const XImage* image,
                                   const XRegion* region, const XPoint* offset)
{
    return xpad_present(window, image, region, offset);
}

/* 内容旋转能力（2026-10-24 安卓旋转接入）：安卓为软件渲染直写 surface，
   旋转由 present 侧逐像素 R⁻¹ 转置实现（xpad_drawImageRotated），
   无 X11 那种 visual/深度限制，故恒可旋转。
   dpr>1 放行（本层自持，2026-10-24 从框架层下沉）：安卓的 presentScale 由
   surface 实测推导（surfW/logicalW），与 dpr 解耦——旋转在**逻辑空间**
   完成后再按 presentScale 放大到物理，两者正交，dpr 只影响放大倍数、
   不影响旋转正确性，故 dpr>1 仍判支持（桌面 win32 / X11 直拷不缩放，
   相反需降级 0°，见各自后端的同名函数）。 */
bool XPlatformNativeWindow_rotationSupported(const XWindow* window)
{
    (void)window;
    return true;
}


void* XPlatformNativeWindow_nativeConnection(
        XPlatformNativeWindowConnectionType* outType)
{
    if (outType) *outType = XPlatformNativeWindowConnection_None;
    return NULL;
}

/* ==================== NativeActivity 生命周期接线（供入口调用） ==================== */



/**
 * @brief  把渲染缓冲重设为窗口逻辑尺寸（WSA 缩放对齐）。
 * @details WSA 拖动窗口改变大小时只改窗口逻辑尺寸，surface 缓冲保持旧
 *          尺寸被拉伸显示；输入事件在逻辑空间而渲染在旧像素空间，点击
 *          全部落偏。经 JNI 查询 decorView 尺寸后 setBuffersGeometry 对
 *          齐，输入与渲染坐标 1:1。查询失败保持现状（真机无此问题）。
 */
static void xpad_alignBufferToWindow(ANativeWindow* win)
{
    int fw = 0;
    int fh = 0;
    int32_t format;
    if (!win) return;
    if (!XPlatformScreen_queryFrameSize(&fw, &fh)) {
        XPAD_LOGI("align: frame size query failed, keep buffer");
        return;
    }
    if (fw <= 0 || fh <= 0) return;
    if (ANativeWindow_getWidth(win) == fw &&
        ANativeWindow_getHeight(win) == fh)
        return; /* 已对齐 */
    format = ANativeWindow_getFormat(win);
    if (ANativeWindow_setBuffersGeometry(win, fw, fh, format) == 0)
        XPAD_LOGI("align: buffer set to %dx%d", fw, fh);
    else
        XPAD_LOGI("align: setBuffersGeometry %dx%d failed", fw, fh);
}

void XPad_onNativeWindowCreated(ANativeWindow* window)
{
    ANativeWindow* old = NULL;
    XPlatformScreenDpiInfo dpiInfo;
    float density = 1.0f;
    int densityDpi = 160;
    int xdpi = 160;
    int ydpi = 160;
    bool densityValid = false;
    int viewW = 0;
    int viewH = 0;
    int surfW = 0;
    int surfH = 0;
    bool viewOk;
    XPadViewSpace viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
    int logicalW = 0;
    int logicalH = 0;
    float presentScale = 1.0f;

    ANativeWindow_acquire(window); /* 持自身引用：窗口销毁回调返回后对象仍可用 */

    /* H3①：首帧密度同步采样——纯 JNI 读数（GetEnv/Attach 自适应任意
       线程），不属 §1.4 信号面收口，UI 线程可直接调；必须先于探针/
       门控求值，否则 dpr 落默认 1.0、门控走错分支（G1 要防的情形会经
       默认分支重新进入且持续到下次 surface 重建才自愈）。失败兜底
       160/1.0 = 安全退化（PHYSICAL + align 照跑 = 现网行为）。成功位随
       快照发布：xdpi/ydpi 只有查询成功才是设备真值，mm 上报链（probe
       的 ABI2 字段）据此守「0=未知，严禁编造」契约。 */
    densityValid = XPlatformScreen_queryDpi(&dpiInfo);
    density = dpiInfo.m_density;
    densityDpi = dpiInfo.m_densityDpi;
    xdpi = dpiInfo.m_xdpi;
    ydpi = dpiInfo.m_ydpi;
    /* density<1（0.75 档老设备）钳为 1.0：逻辑口径会反大于物理、present
       需要缩小路径（本设计未实现）——钳 1.0 即精确现网行为（安全退化，
       同 §10-13 兜底哲学）；主流设备/模拟器密度恒 >=1.0。 */
    if (density < 1.0f) density = 1.0f;
    /* 强制 DPI（XGUI_FORCE_DPI，调试/放大屏）：统一切口替换原生读数，
       后续标定/触摸÷dpr/present 放大/XScreen 上报全链自洽随动。 */
    density = XPlatformScreen_applyDpiOverride(density);

    /* H4②：探针采样在锁外完成（JNI 往返不持 g_xpad.m_mutex——该锁同
       时被事件线程 processPendingEvents 竞争，持锁 JNI 会阻塞事件循环
       一个 JNI 往返）。getWindowFrameSize=Activity 内容视图尺寸（View
       体系口径），ANativeWindow 尺寸=surface 原生物理值。 */
    viewOk = XPlatformScreen_queryFrameSize(&viewW, &viewH);
    surfW = ANativeWindow_getWidth(window);
    surfH = ANativeWindow_getHeight(window);

    /* 坐标空间标定探针（§4.3，不预设谁对，运行期二选一）：
       - viewW==surfW                → PHYSICAL（raw/origin 均物理 px）
       - |view*dpr − surf| <= 1      → LOGICAL（view 尺寸为逻辑口径）
       - 其他                        → PHYSICAL（按 Android 文档兜底）
       dpr=1 时 viewW==surfW 恒落 PHYSICAL——LOGICAL 分支蕴涵 dpr>1，
       按分支门控 align 因此覆盖全部有害情形（H2）。 */
    /* 标定块条件=仅 surf 有效即可（view 缺失不再整体跳过）：viewOk=false
       时 viewW/H 保持 0，三路判定自然落入 PHYSICAL 兜底分支（按 Android
       文档口径 surf/dpr 折算）——曾因 view 探针单点失败把标定整块跳过，
       m_dpr 与 m_presentScale/logicalW desync（输入 1.5/渲染 1.0，冷启动
       首帧 1:1、点击错位 p/3；后续 resize 重推条件 logicalW>0 永假无法
       自愈，直到 surface 重建重标）。 */
    if (surfW > 0 && surfH > 0) {
        float dW = (float)viewW * density - (float)surfW;
        float dH = (float)viewH * density - (float)surfH;
        if (dW < 0.0f) dW = -dW;
        if (dH < 0.0f) dH = -dH;
        if (viewW == surfW && viewH == surfH)
            viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
        else if (dW <= 1.0f && dH <= 1.0f)
            viewSpace = XPAD_VIEW_SPACE_LOGICAL;
        else
            viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
        /* 标定分支下逻辑尺寸来源：PHYSICAL→surf/dpr（round）；
           LOGICAL→view 尺寸即逻辑口径（§4.3.1-②）。 */
        if (viewSpace == XPAD_VIEW_SPACE_LOGICAL) {
            logicalW = viewW;
            logicalH = viewH;
        }
        else {
            logicalW = (int)((float)surfW / density + 0.5f);
            logicalH = (int)((float)surfH / density + 0.5f);
        }
        /* dpr<=1 钳 1.0：快路径红线（dpr=1/WSA 逐位不变），陈旧比值
           不得进入 dpr=1 链路；dpr>1 时系数=surfW/logicalW 实测推导。 */
        presentScale = (density > 1.0f) ? (float)surfW / (float)logicalW
                                        : 1.0f;
    }
    /* 采样失败：逻辑口径未知，重置为 scale=1 安全退化（不带旧 surface
       的陈旧系数进入新 surface）。 */

    /* 持锁段：发布密度快照 + 标定结果 + 屏幕登记待办（UI 写/事件线程
       同锁读；XScreen 信号面本身留给事件线程，§1.4）。 */
    pthread_mutex_lock(&g_xpad.m_mutex);
    old = g_xpad.m_window;
    g_xpad.m_window = window;
    g_xpad.m_dpr = density;
    g_xpad.m_densityDpi = densityDpi;
    g_xpad.m_xdpi = (float)xdpi;
    g_xpad.m_ydpi = (float)ydpi;
    g_xpad.m_densityValid = densityValid;
    g_xpad.m_viewSpace = viewSpace;
    g_xpad.m_logicalW = logicalW;
    g_xpad.m_logicalH = logicalH;
    g_xpad.m_presentScale = presentScale;
    g_xpad.m_screenPending = true;
    /* [全屏几何回推 2026-10-25] surface 尺寸此刻已定 → 通知事件线程把
     * 框架主窗几何改成 surface 的逻辑尺寸（0,0,logicalW,logicalH）。
     * 旋转重建 surface 亦走本函数 → 新尺寸自动生效。 */
    g_xpad.m_geomPending = true;
    g_xpad.m_geomW = logicalW;
    g_xpad.m_geomH = logicalH;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);

    /* H2③：align 按标定分支门控（与 RedrawNeeded 处统一）——仅
       LOGICAL 分支跳过（frameSize=逻辑口径，setBuffersGeometry 会把
       缓冲压成逻辑尺寸，破坏 §4.3 物理契约；可选强化=对齐
       round(viewW*dpr)）；PHYSICAL 分支保留（含 WSA 拖窗修复，任意
       dpr 正确；口径一致时对齐短路 no-op）。 */
    if (viewSpace != XPAD_VIEW_SPACE_LOGICAL)
        xpad_alignBufferToWindow(window);

    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpadWidth = ANativeWindow_getWidth(window);   /* 物理：buffer 契约（§4.3） */
    g_xpadHeight = ANativeWindow_getHeight(window);
    /* 实测放大系数（§4.3.1-④兜底）：align 可能改写缓冲几何，用对齐后
       的实际 surface 宽重推——不盲取 dpr，即使标定分支判错，系数仍与
       真实 surface/逻辑比自洽，blit 不越界不漏铺。登记时更新。 */
    if (g_xpad.m_logicalW > 0 && g_xpadWidth > 0 && g_xpad.m_dpr > 1.0f)
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
    presentScale = g_xpad.m_presentScale;
    logicalW = g_xpad.m_logicalW;
    logicalH = g_xpad.m_logicalH;
    g_xpad.m_surfaceDirty = true;
    xpad_bufLedgerResetLocked();   /* 新 surface：旧缓冲身份全部失效 */
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (old) ANativeWindow_release(old);
    /* 标定结果打日志（§4.3）：设备矩阵（BlueStacks/Pixel5/WSA）按此
       行核对分支判定与放大系数。 */
    XPAD_LOGI("surface created %dx%d fmt=%d viewSpace=%s dpr=%.2f (%ddpi) "
              "presentScale=%.3f logical=%dx%d view=%dx%d",
              g_xpadWidth, g_xpadHeight, ANativeWindow_getFormat(window),
              (viewSpace == XPAD_VIEW_SPACE_LOGICAL) ? "LOGICAL" : "PHYSICAL",
              density, densityDpi, presentScale, logicalW, logicalH,
              viewW, viewH);
}

void XPad_onNativeWindowResized(ANativeWindow* window)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    /* resize 期间系统可能重分配 surface 缓冲：摘下窗口指针，present
       立即放弃本次提交（VSADragResizing 实测拖动窗口边缘时旧映射
       写入 SEGV）。onNativeWindowRedrawNeeded 到达后再挂回。 */
    if (g_xpad.m_window == window) g_xpad.m_window = NULL;
    g_xpadWidth = ANativeWindow_getWidth(window);
    g_xpadHeight = ANativeWindow_getHeight(window);
    /* 实测放大系数随 resize 更新（§5：登记/resize 时更新，不盲取
       dpr）；标定分支复用不重标（§4.3.1-②）。logicalW 未标定（冷启动
       created 标定块曾整体跳过的残留态）时按 PHYSICAL 口径补齐并置
       pending 让屏幕差分回填——系数与真值解耦的部分只剩 dpr 一路。 */
    /* [全屏几何回推 2026-10-25] PHYSICAL 标定下逻辑尺寸=surface/dpr，
     * 旋转/尺寸变化后创建期的旧值已失效，须随新 surface 重算。
     * [顺序红线 2026-10-25 二修] **先定逻辑尺寸、后算 presentScale**：
     * 原实现先用旧逻辑宽算系数（真机竖→横实测 2464/400=6.160，应为
     * 2464/821=3.0），render 按错系数放大而输入仍 ÷dpr → 所见与点击
     * 错一倍（用户真机实测「点击坐标都不准」的根因）。本机清单声明
     * configChanges，旋转走**原地 resize**（surface 不重建、Created
     * 重标定不来），错系数会一直存活到 redrawNeeded 才被治愈——中间
     * 的点击全部落错。LOGICAL 标定下逻辑尺寸=view 尺寸，本路径取不到
     * 新 view 值，沿用创建期标定值，随后的 Created 重标定修正。 */
    if (g_xpad.m_viewSpace == XPAD_VIEW_SPACE_PHYSICAL && g_xpadWidth > 0 &&
        g_xpad.m_dpr > 0.0f) {
        g_xpad.m_logicalW = (int)((float)g_xpadWidth / g_xpad.m_dpr + 0.5f);
        g_xpad.m_logicalH = (int)((float)g_xpadHeight / g_xpad.m_dpr + 0.5f);
    }
    /* 实测放大系数随**新逻辑值**重推（dpr<=1 钳 1.0：快路径红线）。 */
    if (g_xpad.m_logicalW > 0 && g_xpadWidth > 0 && g_xpad.m_dpr > 1.0f)
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
    else if (g_xpad.m_dpr <= 1.0f)
        g_xpad.m_presentScale = 1.0f;
    g_xpad.m_geomPending = true;
    g_xpad.m_geomW = g_xpad.m_logicalW;
    g_xpad.m_geomH = g_xpad.m_logicalH;
    g_xpad.m_surfaceDirty = true;
    xpad_bufLedgerResetLocked();   /* resize 期缓冲可能重分配：身份全失效 */
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("surface resized %dx%d (window detached until redrawNeeded) "
              "presentScale=%.3f",
              g_xpadWidth, g_xpadHeight, g_xpad.m_presentScale);
}

void XPad_onNativeWindowRedrawNeeded(ANativeWindow* window)
{
    XPadViewSpace viewSpace;
    pthread_mutex_lock(&g_xpad.m_mutex);
    /* resize 完成后的重绘请求：此时缓冲已稳定，重新挂回并全窗 expose。 */
    g_xpad.m_window = window;
    viewSpace = g_xpad.m_viewSpace;   /* 复用已标定分支，不重标 */
    pthread_mutex_unlock(&g_xpad.m_mutex);
    /* 拖动缩放后缓冲与窗口逻辑尺寸可能失配，先对齐再上报几何——
       H2③ 门控与 onNativeWindowCreated 统一：仅 LOGICAL 分支跳过
       （frameSize=逻辑口径会压小缓冲），PHYSICAL 保留（含 WSA 拖窗
       修复 :WSA-quirk，任意 dpr 正确）。 */
    if (viewSpace != XPAD_VIEW_SPACE_LOGICAL)
        xpad_alignBufferToWindow(window);
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpadWidth = ANativeWindow_getWidth(window);
    g_xpadHeight = ANativeWindow_getHeight(window);
    /* [顺序红线 2026-10-25] 先定逻辑尺寸、后算系数（与 Resized 同修）：
       PHYSICAL 标定下旧逻辑值对新 surface 已失效，先用它算系数会落
       newW/oldLogicalW 的错倍率（render 与输入口径劈叉=点击错位）。 */
    if (g_xpad.m_viewSpace == XPAD_VIEW_SPACE_PHYSICAL && g_xpadWidth > 0 &&
        g_xpad.m_dpr > 0.0f) {
        g_xpad.m_logicalW = (int)((float)g_xpadWidth / g_xpad.m_dpr + 0.5f);
        g_xpad.m_logicalH = (int)((float)g_xpadHeight / g_xpad.m_dpr + 0.5f);
    }
    if (g_xpad.m_logicalW > 0 && g_xpadWidth > 0 && g_xpad.m_dpr > 1.0f)
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
    else if (g_xpad.m_logicalW <= 0 && g_xpadWidth > 0 &&
             g_xpad.m_dpr > 1.0f) {
        /* 未标定补齐（与 Resized 同款兜底）：冷启动标定缺口在此闭合，
           不得拖到下次 surface 重建才自愈。 */
        g_xpad.m_viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
        g_xpad.m_screenPending = true;
    }
    g_xpad.m_surfaceDirty = true;
    xpad_bufLedgerResetLocked();   /* 重挂窗口：缓冲身份按失效处理 */
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("redrawNeeded: window re-attached %dx%d presentScale=%.3f",
              g_xpadWidth, g_xpadHeight, g_xpad.m_presentScale);
}

void XPad_onNativeWindowDestroyed(void)
{
    ANativeWindow* old = NULL;
    pthread_mutex_lock(&g_xpad.m_mutex);
    old = g_xpad.m_window;
    g_xpad.m_window = NULL;
    g_xpad.m_surfaceDirty = false;
    xpad_bufLedgerResetLocked();   /* surface 销毁：欠账表连同失效 */
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (old) ANativeWindow_release(old); /* 引用计数保证 present 中的对象存活 */
}

/** @brief AKEYCODE → XKey 码位翻译（对标 win32 xpwn_translateKey 码表）。
 *  @details 字母恒大写（X11 同款归一：XKey_A..Z=0x41..0x5A，大小写经
 *           Shift 修饰位表达）；数字/可打印 ASCII 恒等；功能键/方向键
 *           走 XKey_* 专码。无法识别返回 XKey_None（事件整条丢弃）。
 *           AKEYCODE_BACK 映射 XKey_Escape：壳层对输入恒 handled=1，
 *           返回键无系统默认行为，给 Esc 语义（退弹层/关对话框）比
 *           静默丢弃有用。 */
static int xpad_translateKeyCode(int32_t keycode)
{
    switch (keycode) {
    case AKEYCODE_SPACE:          return XKey_Space;
    /* 剪贴板专键（AKEYCODE 语义即动作）：映射框架 Ctrl+C/X/V 快捷键位，
       修饰位由调用方强制补 Control（xlc_matchCtrlLetter 依赖）。 */
    case AKEYCODE_COPY:           return 'C';
    case AKEYCODE_CUT:            return 'X';
    case AKEYCODE_PASTE:          return 'V';
    case AKEYCODE_ENTER:          return XKey_Enter;
    case AKEYCODE_NUMPAD_ENTER:   return XKey_Enter;
    case AKEYCODE_BACK:           return XKey_Escape;
    case AKEYCODE_ESCAPE:         return XKey_Escape;
    case AKEYCODE_DEL:            return XKey_Backspace;
    case AKEYCODE_FORWARD_DEL:    return XKey_Delete;
    case AKEYCODE_INSERT:         return XKey_Insert;
    case AKEYCODE_MOVE_HOME:      return XKey_Home;
    case AKEYCODE_MOVE_END:       return XKey_End;
    case AKEYCODE_PAGE_UP:        return XKey_PageUp;
    case AKEYCODE_PAGE_DOWN:      return XKey_PageDown;
    case AKEYCODE_DPAD_LEFT:      return XKey_Left;
    case AKEYCODE_DPAD_UP:        return XKey_Up;
    case AKEYCODE_DPAD_RIGHT:     return XKey_Right;
    case AKEYCODE_DPAD_DOWN:      return XKey_Down;
    case AKEYCODE_TAB:            return XKey_Tab;
    case AKEYCODE_SHIFT_LEFT:
    case AKEYCODE_SHIFT_RIGHT:    return XKey_Shift;
    case AKEYCODE_CTRL_LEFT:
    case AKEYCODE_CTRL_RIGHT:     return XKey_Control;
    case AKEYCODE_ALT_LEFT:
    case AKEYCODE_ALT_RIGHT:      return XKey_Alt;
    case AKEYCODE_META_LEFT:
    case AKEYCODE_META_RIGHT:     return XKey_Meta;
    case AKEYCODE_CAPS_LOCK:      return XKey_CapsLock;
    case AKEYCODE_NUM_LOCK:       return XKey_NumLock;
    case AKEYCODE_SCROLL_LOCK:    return XKey_ScrollLock;
    /* 主键盘区标点：返回基础字形（Shift 变体由框架布局表派生）。 */
    case AKEYCODE_COMMA:          return XKey_Comma;
    case AKEYCODE_PERIOD:         return XKey_Period;
    case AKEYCODE_MINUS:          return XKey_Minus;
    case AKEYCODE_EQUALS:         return XKey_Equal;
    case AKEYCODE_LEFT_BRACKET:   return XKey_BracketLeft;
    case AKEYCODE_RIGHT_BRACKET:  return XKey_BracketRight;
    case AKEYCODE_BACKSLASH:      return XKey_Backslash;
    case AKEYCODE_SEMICOLON:      return XKey_Semicolon;
    case AKEYCODE_APOSTROPHE:     return XKey_Apostrophe;
    case AKEYCODE_SLASH:          return XKey_Slash;
    case AKEYCODE_AT:             return XKey_At;
    case AKEYCODE_PLUS:           return XKey_Plus;
    case AKEYCODE_GRAVE:          return XKey_QuoteLeft;
    case AKEYCODE_STAR:           return XKey_Asterisk;
    case AKEYCODE_POUND:          return 0x23; /* '#'（ASCII 恒等区）。 */
    /* 小键盘：数字/符号复用主键盘码位（KeypadModifier 由调用方补充）。 */
    case AKEYCODE_NUMPAD_0:       return XKey_0;
    case AKEYCODE_NUMPAD_1:       return XKey_1;
    case AKEYCODE_NUMPAD_2:       return XKey_2;
    case AKEYCODE_NUMPAD_3:       return XKey_3;
    case AKEYCODE_NUMPAD_4:       return XKey_4;
    case AKEYCODE_NUMPAD_5:       return XKey_5;
    case AKEYCODE_NUMPAD_6:       return XKey_6;
    case AKEYCODE_NUMPAD_7:       return XKey_7;
    case AKEYCODE_NUMPAD_8:       return XKey_8;
    case AKEYCODE_NUMPAD_9:       return XKey_9;
    case AKEYCODE_NUMPAD_DIVIDE:  return XKey_Slash;
    case AKEYCODE_NUMPAD_MULTIPLY:return XKey_Asterisk;
    case AKEYCODE_NUMPAD_SUBTRACT:return XKey_Minus;
    case AKEYCODE_NUMPAD_ADD:     return XKey_Plus;
    case AKEYCODE_NUMPAD_DOT:     return XKey_Period;
    case AKEYCODE_NUMPAD_COMMA:   return XKey_Comma;
    case AKEYCODE_NUMPAD_EQUALS:  return XKey_Equal;
    default:
        /* 功能键 F1..F12（AKEYCODE_F1=131..F12=142）。 */
        if (keycode >= AKEYCODE_F1 && keycode <= AKEYCODE_F12)
            return XKey_F1 + (keycode - AKEYCODE_F1);
        /* 字母（A=29..Z=54）与数字（0=7..9=16）：大写/ASCII 恒一口径。 */
        if (keycode >= AKEYCODE_A && keycode <= AKEYCODE_Z)
            return XKey_A + (keycode - AKEYCODE_A);
        if (keycode >= AKEYCODE_0 && keycode <= AKEYCODE_9)
            return XKey_0 + (keycode - AKEYCODE_0);
        return XKey_None;
    }
}

/** @brief AMETA_* 位 → XKeyboardModifiers 翻译。 */
static int xpad_translateKeyModifiers(int32_t metaState, int32_t keycode,
                                      int key)
{
    int modifiers = XKeyboardModifier_NoModifier;
    if ((metaState & AMETA_SHIFT_ON) != 0)
        modifiers |= XKeyboardModifier_ShiftModifier;
    if ((metaState & AMETA_CTRL_ON) != 0)
        modifiers |= XKeyboardModifier_ControlModifier;
    if ((metaState & AMETA_ALT_ON) != 0)
        modifiers |= XKeyboardModifier_AltModifier;
    if ((metaState & AMETA_META_ON) != 0)
        modifiers |= XKeyboardModifier_MetaModifier;
    /* 小键盘键（NUMPAD_* 族）补 Keypad 标记，与 win32 VK 0x60-0x6f
       同一约定（keyToText 按此位跳过 Shift 布局表）。 */
    if (key != XKey_None &&
        keycode >= AKEYCODE_NUMPAD_0 && keycode <= AKEYCODE_NUMPAD_EQUALS)
        modifiers |= XKeyboardModifier_KeypadModifier;
    return modifiers;
}

void XPad_onInputEvent(AInputEvent* event)
{
    int32_t type = AInputEvent_getType(event);
    int originX = 0;
    int originY = 0;
    float dpr;
    XPadViewSpace viewSpace;
    XPadTouchEvent frame;
    int32_t baseAction;
    int n;
    int i;
    size_t written;

    /* ---- 键盘分支（[2026-10-24 安卓键盘接入] 此前 KEY 事件整类丢弃，
       物理键盘/adb input text 打字全无反应）: UI 线程就地翻译（码表 +
       修饰位 + 时间戳 ns→ms），值拷贝入 m_keyQueue，渲染线程泵注入。 */
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t action = AKeyEvent_getAction(event);
        int32_t keycode = AKeyEvent_getKeyCode(event);
        int32_t metaState = AKeyEvent_getMetaState(event);
        int32_t scanCode = AKeyEvent_getScanCode(event);
        XPadKeyEvent keyFrame;
        int key = xpad_translateKeyCode(keycode);
        if (key == XKey_None) return; /* 未识别键（音量/媒体等）静默丢弃 */
        keyFrame.key = key;
        keyFrame.modifiers = xpad_translateKeyModifiers(metaState, keycode,
                                                        key);
        /* 剪贴板专键强制 Ctrl 修饰（框架复制/剪切/粘贴入口=Ctrl+字母）。 */
        if (keycode == AKEYCODE_COPY || keycode == AKEYCODE_CUT ||
            keycode == AKEYCODE_PASTE)
            keyFrame.modifiers |= XKeyboardModifier_ControlModifier;
        keyFrame.scanCode = scanCode;
        keyFrame.timestampMs =
            (uint32_t)(AKeyEvent_getEventTime(event) / 1000000LL);
        if (keyFrame.timestampMs == 0)
            keyFrame.timestampMs = 1; /* 非合成判据：时间戳/扫描码不双零 */
        if (action == AKEY_EVENT_ACTION_UP) {
            keyFrame.type = XEVENT_TYPE_KEY_RELEASE;
            keyFrame.autoRepeat = false;
        }
        else {
            /* DOWN 与 ACTION_MULTIPLE（系统连发合并帧）都按按下注入；
               MULTIPLE 的 repeatCount 不逐条展开——框架按 autoRepeat
               识别连发节奏，行编辑的删除/移动键自然连续。 */
            keyFrame.type = XEVENT_TYPE_KEY_PRESS;
            keyFrame.autoRepeat = (action == AKEY_EVENT_ACTION_MULTIPLE)
                                      ? true
                                      : AKeyEvent_getRepeatCount(event) > 0;
        }
        pthread_mutex_lock(&g_xpad.m_mutex);
        if (!g_xpad.m_queueReady) {
            XRingBuffer_init(&g_xpad.m_touchQueue, 512);
            XRingBuffer_init(&g_xpad.m_keyQueue, 512);
            g_xpad.m_queueReady = true;
        }
        /* 修饰键位跟踪（queryKeyboardModifiers 口径）：
         * PRESS=以事件自带 metaState 全量重建（自校正）；修饰键自身的
         * RELEASE 单独清位（metaState 在修饰键 UP 上是否含自身位因
         * 设备而异，不能全量信任）。其余键的 RELEASE 不动位。 */
        if (keyFrame.type == XEVENT_TYPE_KEY_PRESS) {
            g_xpad.m_keyModifiers = keyFrame.modifiers;
            if (key == XKey_Shift)
                g_xpad.m_keyModifiers |= XKeyboardModifier_ShiftModifier;
            else if (key == XKey_Control)
                g_xpad.m_keyModifiers |= XKeyboardModifier_ControlModifier;
            else if (key == XKey_Alt)
                g_xpad.m_keyModifiers |= XKeyboardModifier_AltModifier;
            else if (key == XKey_Meta)
                g_xpad.m_keyModifiers |= XKeyboardModifier_MetaModifier;
        }
        else if (key == XKey_Shift)
            g_xpad.m_keyModifiers &= ~XKeyboardModifier_ShiftModifier;
        else if (key == XKey_Control)
            g_xpad.m_keyModifiers &= ~XKeyboardModifier_ControlModifier;
        else if (key == XKey_Alt)
            g_xpad.m_keyModifiers &= ~XKeyboardModifier_AltModifier;
        else if (key == XKey_Meta)
            g_xpad.m_keyModifiers &= ~XKeyboardModifier_MetaModifier;
        written = XRingBuffer_write(&g_xpad.m_keyQueue, &keyFrame,
                                    sizeof(keyFrame));
        if (written != sizeof(keyFrame)) {
            XRingBuffer_reset(&g_xpad.m_keyQueue);
            XPAD_LOGI("key queue: partial write %u/%u, reset & drop",
                      (unsigned)written, (unsigned)sizeof(keyFrame));
        }
        pthread_cond_broadcast(&g_xpad.m_cond);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        XPAD_LOGI("key frame: action=%d keycode=%d key=0x%x mods=0x%x",
                  (int)action, (int)keycode, (unsigned)keyFrame.key,
                  (unsigned)keyFrame.modifiers);
        return;
    }

    if (type != AINPUT_EVENT_TYPE_MOTION) return;
    /* origin 查询（JNI 往返）在锁外：不持 g_xpad.m_mutex 做 JNI（与
       探针同纪律，H4）；查询失败按 (0,0) 处理——退化路径仍按标定分支
       换算，不退化为混算（§6.1）。 */
    XPlatformScreen_queryOrigin(&originX, &originY);
    /* AInputEvent 仅在本回调内有效（UI 线程）：整帧快照（全部触点
       id/raw 坐标 + 时间戳）必须就地抽成值拷贝入队，严禁把 AInputEvent
       指针跨线程携带。 */
    frame.action = AMotionEvent_getAction(event);  /* 未掩码：含 INDEX 位 */
    baseAction = frame.action & AMOTION_EVENT_ACTION_MASK;
    n = AMotionEvent_getPointerCount(event);
    if (n > XPAD_MAX_TOUCH_POINTS) n = XPAD_MAX_TOUCH_POINTS;
    frame.count = n;
    /* getEventTime 为 ns；框架触摸时间戳口径为 ms（X11 XIDeviceEvent
       的 time 同口径，经 handleTouchPoints_ex 透传给合成器）。 */
    frame.timestampMs =
        (uint32_t)(AMotionEvent_getEventTime(event) / 1000000LL);
    pthread_mutex_lock(&g_xpad.m_mutex);
    if (!g_xpad.m_queueReady) {
        XRingBuffer_init(&g_xpad.m_touchQueue, 512);
        XRingBuffer_init(&g_xpad.m_keyQueue, 512);
        g_xpad.m_queueReady = true;
    }
    dpr = g_xpad.m_dpr;
    viewSpace = g_xpad.m_viewSpace;
    if (!(dpr > 0.0f)) dpr = 1.0f;
    /* 统一坐标口径 + 反缩放（§6.1：输入逻辑化的唯一承担者=平台入口
       归一化）。逐点 getRawX/Y（屏幕坐标）减窗口屏幕原点得窗口本地
       坐标，再按标定分支折算框架逻辑 px，两分支产物均为换算后逻辑
       坐标：
       - PHYSICAL（origin 为物理 px）：evt = (raw − origin) / dpr
       - LOGICAL （origin 为逻辑 px）：evt = raw / dpr − origin       */
    for (i = 0; i < n; ++i) {
        float rawX = AMotionEvent_getRawX(event, i);
        float rawY = AMotionEvent_getRawY(event, i);
        frame.pts[i].id = (int32_t)AMotionEvent_getPointerId(event, i);
        if (viewSpace == XPAD_VIEW_SPACE_LOGICAL) {
            frame.pts[i].x = (int32_t)(rawX / dpr + 0.5f) - originX;
            frame.pts[i].y = (int32_t)(rawY / dpr + 0.5f) - originY;
        }
        else {
            frame.pts[i].x = (int32_t)((rawX - (float)originX) / dpr + 0.5f);
            frame.pts[i].y = (int32_t)((rawY - (float)originY) / dpr + 0.5f);
        }
    }
    written = XRingBuffer_write(&g_xpad.m_touchQueue, &frame, sizeof(frame));
    if (written != sizeof(frame))
    {
        /* 半帧写入（扩容加块/块写失败返回部分写入）：108 字节定长读
         * 契约下字节流一旦错位，此后每个"帧"都是两帧拼接的垃圾且永
         * 不自愈——锁内重置队列丢弃本帧，下一帧从块边界重新开始
         * （丢一帧远好于全流撕碎/幻影触摸）。 */
        XRingBuffer_reset(&g_xpad.m_touchQueue);
        XPAD_LOGI("touch queue: partial write %u/%u, reset & drop frame",
                  (unsigned)written, (unsigned)sizeof(frame));
    }
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    /* 诊断保持低频：仅帧边界动作打点（MOVE/HOVER_MOVE 连发逐帧静默，
       避免 logcat 刷屏——移动手势每秒可上百帧）。 */
    if (baseAction != AMOTION_EVENT_ACTION_MOVE &&
        baseAction != AMOTION_EVENT_ACTION_HOVER_MOVE)
        XPAD_LOGI("touch frame: action=%d index=%d count=%d t=%u",
                  (int)baseAction,
                  (int)((frame.action &
                         AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                        AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                  n, (unsigned)frame.timestampMs);
}

void XPad_setRunning(bool running)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_running = running;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

void XPad_setDestroyed(void)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_destroyed = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

void XPad_resetDestroyed(void)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_destroyed = false;
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

void XPad_setActivity(ANativeActivity* activity)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_activity = activity;
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

void XPad_onConfigurationChanged(void)
{
    XPlatformScreenDpiInfo dpiInfo;
    float density = 1.0f;
    int densityDpi = 160;
    int xdpi = 160;
    int ydpi = 160;
    bool densityValid = false;

    /* UI 线程（ANativeActivityCallbacks.onConfigurationChanged，wm
       density 运行期变化）：只做纯 JNI 读数 + 写 g_xpad 快照 + 置
       pending 槽；XScreen 差分回填（setDevicePixelRatio+setGeometry
       成对）由框架事件线程在 processPendingEvents 消费（§1.3/§1.4
       ——XObject 信号面永不出现在 UI 线程）。surface 尺寸随配置变化
       的重标定由系统随后重发的 onNativeWindowCreated 完成。成功位随
       快照发布（mm 上报链守「0=未知，严禁编造」，同 created 路径）。 */
    densityValid = XPlatformScreen_queryDpi(&dpiInfo);
    density = dpiInfo.m_density;
    densityDpi = dpiInfo.m_densityDpi;
    xdpi = dpiInfo.m_xdpi;
    ydpi = dpiInfo.m_ydpi;
    /* density<1 钳 1.0（同 onNativeWindowCreated：缩小路径未实现，安全
       退化为现网行为）。 */
    if (density < 1.0f) density = 1.0f;
    /* 强制 DPI（XGUI_FORCE_DPI）：与 surface 标定同切口（统一切口见
       XPlatformScreen.h）。 */
    density = XPlatformScreen_applyDpiOverride(density);
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_dpr = density;
    g_xpad.m_densityDpi = densityDpi;
    g_xpad.m_xdpi = (float)xdpi;
    g_xpad.m_ydpi = (float)ydpi;
    g_xpad.m_densityValid = densityValid;
    g_xpad.m_screenPending = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("configuration changed: dpr=%.2f densityDpi=%d xdpi=%d "
              "ydpi=%d (screen backfill pending)",
              density, densityDpi, xdpi, ydpi);
}

#endif /* XPLATFORMINTEGRATION_ON && XPLATFORM_ANDROID_ON */
#endif /* __ANDROID__ && XGUI_ON */
