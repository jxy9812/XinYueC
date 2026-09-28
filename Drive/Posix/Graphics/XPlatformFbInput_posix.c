/******************************************************************************
 * @file       XPlatformFbInput_posix.c
 * @brief      Linux fbdev 模式触摸输入驱动实现（linux/input.h evdev ->
 *             指针事件注入）。
 * @details    实现 XPlatformFbInput_posix.h 的注册契约。事件链路（与
 *             X11 后端 XPlatformNativeWindow_posix.c 同一注入面）：
 *             - 事件循环每轮经轮询回调进入 xpfi_pump，非阻塞 read 抽干
 *               evdev 积压报文（linux/input.h 的 struct input_event 流）；
 *             - 逐报文更新触点状态机：ABS_X/ABS_Y（ST 单点协议）与
 *               ABS_MT_POSITION_X/Y（MT 协议 A/B，取最新值；单点触摸
 *               场景 slot 差异可忽略）刷新原始坐标；按下判定按
 *               BTN_TOUCH > ABS_MT_TRACKING_ID（-1 抬起）> ABS_PRESSURE
 *               的优先级取第一个可用来源（TSC2007 等 ST 屏只有前者，
 *               纯 MT 屏落到后两者）；
 *             - 仅在 SYN_REPORT 帧边界提交状态机（同帧多报文以最后一
 *               个为准，避免半帧坐标），产出按下/移动/抬起三类指针
 *               事件：坐标按「面板分辨率 + EVIOCGABS 范围」归一到像素
 *               （面板尺寸取活动显示驱动 probe 快照，与直写显示同源；
 *               范围为 0 或未探测到时按设备报告值即像素直通——部分触
 *               摸屏 abs info 本就输出屏幕坐标）；
 *             - 命中目标窗口 XGuiApplication_topLevelAt（全局坐标反查
 *               顶层），窗口几何换算出窗口局部坐标后经
 *               XWindowSystemInterface_handleMouseEvent_ex 注入——按下
 *               MOUSE_BUTTON_PRESS / 移动 MOUSE_MOVE / 抬起
 *               MOUSE_BUTTON_RELEASE，按钮统一 LeftButton（单点触摸
 *               等价左键，对标 Qt linuxfb/tslib 的触摸->鼠标合成口径）。
 *               控件命中/抓取/双击下游语义由既有桥接管线处理。
 *             找不到命中窗口时静默丢弃本帧（启动早期窗口未建属正常时
 *             序）；按下期间窗口消失则复位按压状态，防止悬挂的按下态。
 * @note       本文件只在「__linux__ && XGUI_ON && XPLATFORM_FBDEV_ON &&
 *             XPLATFORM_FBINPUT_ON」时参与编译（开关默认 0）。内存体
 *             系：驱动全程无堆分配，状态静态；单线程主循环设计（与
 *             fbdev 显示驱动同口径），不加锁。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformFbInput_posix.h"

#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON && XPLATFORM_FBINPUT_ON

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h> /* struct timeval（input_event.time）。 */
#include <unistd.h>
#include <linux/input.h>

#include "XAbstractEventDispatcher.h"
#include "XWindowSystemInterface.h"
#include "XWidget.h" /* mouseGrabber/windowHandle：按住拖出窗口界的注入。 */
#include "XSystem.h"

/* ==================== 进程级驱动状态（静态，无堆分配） ==================== */

static int g_xpfiFd = -1;              /**< evdev 设备描述符；<0 未打开。 */
static bool g_xpfiRegistered = false;  /**< 读泵已注册进轮询链。 */
static XHandle g_xpfiPumpHandle;       /**< 轮询链句柄（remove 用）。 */

/** @brief ABS 坐标取值范围（EVIOCGABS 探测；0 上限表示未探测到）。 */
static int g_xpfiAbsXMax = 0;
static int g_xpfiAbsYMax = 0;

/* ==================== 厂商触摸校准（QWS /etc/pointercal） ==================== */

/** @brief 校准文件路径（板级可经编译选项覆盖）。 */
#ifndef XPLATFORM_FBINPUT_POINTERCAL
#define XPLATFORM_FBINPUT_POINTERCAL "/etc/pointercal"
#endif

/** @brief QWS 线性校准矩阵：Xs=(a*x+b*y+c)/s，Ys=(d*x+e*y+f)/s。
 *  7 参数与 Qt/Embedded pointercal 同口径（a b c d e f s），涵盖轴
 *  交换/旋转/镜像/缩放——电阻屏触摸区与出厂数据域即由此文件定义。 */
static int g_xpfiCalA, g_xpfiCalB, g_xpfiCalC;
static int g_xpfiCalD, g_xpfiCalE, g_xpfiCalF, g_xpfiCalS;
static bool g_xpfiCalLoaded = false; /**< 校准文件存在且解析成功。 */

/**
 * @brief      读取厂商 QWS 校准文件（/etc/pointercal，7 个十进制整数）。
 * @details    MCGS 出厂屏在 /etc/pointercal 里保存 Qt/Embedded 校准
 *             矩阵（qws 内部同式）。存在即优先采用——面板装配方向
 *             （轴交换/旋转）与触摸区边沿全部包含在矩阵内，比裸
 *             EVIOCGABS 线性归一准确；文件缺席/解析失败/s=0 时保持
 *             未加载，回退既有范围归一逻辑。
 */
static void xpfi_loadPointercal(void)
{
    int fd;
    char buf[128];
    ssize_t n;
    long v[7];
    int i;
    char* p;
    fd = open(XPLATFORM_FBINPUT_POINTERCAL, O_RDONLY);
    if (fd < 0) return;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    p = buf;
    for (i = 0; i < 7; ++i)
    {
        char* end;
        long value = strtol(p, &end, 10);
        if (end == p) return; /* 任一参数解析失败：视为无校准。 */
        v[i] = value;
        p = end;
    }
    if (v[6] == 0) return; /* 缩放因子 0：非法校准。 */
    g_xpfiCalA = (int)v[0];
    g_xpfiCalB = (int)v[1];
    g_xpfiCalC = (int)v[2];
    g_xpfiCalD = (int)v[3];
    g_xpfiCalE = (int)v[4];
    g_xpfiCalF = (int)v[5];
    g_xpfiCalS = (int)v[6];
    g_xpfiCalLoaded = true;
}

/** @brief 触点状态机（SYN_REPORT 帧间保持）。 */
static bool g_xpfiPressed = false;     /**< 当前物理按压态。 */
static bool g_xpfiLastInjected = false;/**< 上帧已注入的按压态（沿判定）。 */
static bool g_xpfiHavePoint = false;   /**< 原始坐标已有效（首帧前不注入）。 */
static int g_xpfiRawX = 0;             /**< 本帧原始 X（abs 值）。 */
static int g_xpfiRawY = 0;             /**< 本帧原始 Y（abs 值）。 */
static int g_xpfiLastX = -1;           /**< 上帧注入像素 X（移动去抖）。 */
static int g_xpfiLastY = -1;           /**< 上帧注入像素 Y。 */

/** @brief 双击合成状态（对齐 X11 平台层 XPWN_DOUBLE_CLICK_* 口径：
 *  同为左键、时间窗 400ms、位置偏差 4px 内的两次按下沿，第二次注入
 *  DBL_CLICK。X11/Win32 由平台合成双击，fbdev 无窗口系统必须自补——
 *  文件对话框进目录/列表展开均依赖双击（真机 TSC2007 实测无合成时
 *  双击无效）。 */
#define XPFI_DOUBLE_CLICK_INTERVAL_MS 400
#define XPFI_DOUBLE_CLICK_DISTANCE    4
static uint32_t g_xpfiLastPressTimeMs;/**< 最近按下沿时间戳（单调 ms）。 */
static int g_xpfiLastPressX;          /**< 最近按下沿像素 X。 */
static int g_xpfiLastPressY;          /**< 最近按下沿像素 Y。 */
static bool g_xpfiLastPressValid = false; /**< 按下沿状态有效。 */

/* ==================== 工具函数 ==================== */

/**
 * @brief      运行期设备节点解析。
 * @return     NULL 表示运行期禁用（XPLATFORM_FBINPUT_DEVICE=0）；否则
 *             返回节点串（环境变量优先，其次编译期宏；静态生命期）。
 */
static const char* xpfi_devicePath(void)
{
    const char* env;
    /* 总开关：XPLATFORM_FBINPUT=0 运行期禁用（编译已开启时的逃生口）。 */
    env = XSystem_environment("XPLATFORM_FBINPUT");
    if (env && strcmp(env, "0") == 0)
        return NULL;
    env = XSystem_environment("XPLATFORM_FBINPUT_DEVICE");
    if (env)
    {
        if (strcmp(env, "0") == 0)
            return NULL; /* 显式禁用通道。 */
        if (*env)
            return env;
    }
    return XPLATFORM_FBINPUT_DEVICE;
}

/**
 * @brief      把 evdev 原始坐标归一为面板像素坐标。
 * @details    面板宽高取活动显示驱动 probe 快照（fbdev 注册成功时必然
 *             可用；与直写显示同源，无需编译期分辨率）。abs 范围有效
 *             （max>min）时线性映射，越界钳位；范围无效时视为设备直出
 *             屏幕坐标，仅钳位到面板范围。
 */
static void xpfi_normalizePoint(int* outX, int* outY)
{
    const XPlatformDisplayDriverOps* ops = XPlatformDisplayDriver_active();
    XPlatformDisplayInfo info;
    int width = 0;
    int height = 0;
    int x = g_xpfiRawX;
    int y = g_xpfiRawY;
    memset(&info, 0, sizeof(info));
    if (ops && ops->probe(&info))
    {
        width = info.m_width;
        height = info.m_height;
    }
    if (width < 1) width = g_xpfiAbsXMax > 0 ? g_xpfiAbsXMax + 1 : x + 1;
    if (height < 1) height = g_xpfiAbsYMax > 0 ? g_xpfiAbsYMax + 1 : y + 1;
    if (g_xpfiCalLoaded)
    {
        /* 厂商 QWS 校准矩阵优先（含轴交换/旋转/镜像与触摸区边沿）：
         * 原始域坐标直接代入矩阵；64 位中间量防溢出（c/f 项达 1e8，
         * 乘以 raw 域 4095 后超 32 位）。 */
        int64_t sx = (int64_t)g_xpfiCalA * x + (int64_t)g_xpfiCalB * y +
                     g_xpfiCalC;
        int64_t sy = (int64_t)g_xpfiCalD * x + (int64_t)g_xpfiCalE * y +
                     g_xpfiCalF;
        x = (int)(sx / g_xpfiCalS);
        y = (int)(sy / g_xpfiCalS);
    }
    else
    {
        if (g_xpfiAbsXMax > 0)
            x = (int)((int64_t)(x)* (width - 1) / g_xpfiAbsXMax);
        if (g_xpfiAbsYMax > 0)
            y = (int)((int64_t)(y)* (height - 1) / g_xpfiAbsYMax);
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > width - 1) x = width - 1;
    if (y > height - 1) y = height - 1;
    *outX = x;
    *outY = y;
}

/**
 * @brief      全局像素坐标命中顶层窗口并换算出窗口局部坐标。
 * @return     命中的顶层窗口（借用指针）；无命中返回 false。
 */
static bool xpfi_windowAt(int x, int y, XWindow** outWindow, XPoint* outLocal)
{
    XPoint global;
    XWindow* window;
    XRect geometry;
    global.x = x;
    global.y = y;
    window = XGuiApplication_topLevelAt(&global);
    if (!window) return false;
    geometry = XWindow_geometry(window);
    outLocal->x = x - geometry.x;
    outLocal->y = y - geometry.y;
    *outWindow = window;
    return true;
}

/**
 * @brief      按当前状态机注入一帧指针事件（SYN_REPORT 边界调用）。
 * @details    沿判定：抬起->按下注入 PRESS，按下->抬起注入 RELEASE，
 *             按下保持且坐标变化注入 MOVE。按压期间窗口缺失（顶层未
 *             建等启动时序）保持"未注入按下"状态，下帧重试 PRESS；
 *             抬起沿无论窗口是否命中一律清按压沿（物理抬起已消费，
 *             不跨帧重放，防幽灵 PRESS/MOVE）。
 */
static void xpfi_commitFrame(uint32_t timestampMs)
{
    int x;
    int y;
    XWindow* window;
    XPoint local;
    XPoint global;
    if (g_xpfiPressed)
    {
        if (!g_xpfiHavePoint)
            return; /* 触点坐标未上报：等下一帧。 */
        xpfi_normalizePoint(&x, &y);
        if (!xpfi_windowAt(x, y, &window, &local))
        {
            /* 按住期间手指滑出窗口边界（窗口边缘向外拖拽改尺寸的必经
               路径）：无抓取时维持旧行为丢弃；有抓取（标题栏拖拽移动/
               边缘改尺寸显式 grabMouse）时 MOVE 必须继续注入抓取窗口，
               否则边框拖出窗界即断流，只跟一小段就不再跟手（用户真机
               实测 2026-09-28）。局部坐标按窗口原点换算（越界为负值
               合法，改尺寸处理器自行钳制）。 */
            XWidget* grabber = XWidget_mouseGrabber();
            XRect wg;
            if (!grabber) return;
            window = XWidget_windowHandle(grabber);
            if (!window) return;
            wg = XWindow_geometry(window);
            local.x = x - wg.x;
            local.y = y - wg.y;
        }
        global.x = x;
        global.y = y;
        if (!g_xpfiLastInjected)
        {
            /* 按下沿：先做双击识别（对齐 X11 平台层：同键+时间窗+距离窗
             * → 第二次按下沿注入 DBL_CLICK 而非 PRESS；Qt 语义里双击
             * 序列仍以随后的 RELEASE 收尾，抬起沿路径不变）。 */
            bool isDoubleClick = false;
            if (g_xpfiLastPressValid &&
                timestampMs - g_xpfiLastPressTimeMs <
                    XPFI_DOUBLE_CLICK_INTERVAL_MS &&
                g_xpfiLastPressX >= x - XPFI_DOUBLE_CLICK_DISTANCE &&
                g_xpfiLastPressX <= x + XPFI_DOUBLE_CLICK_DISTANCE &&
                g_xpfiLastPressY >= y - XPFI_DOUBLE_CLICK_DISTANCE &&
                g_xpfiLastPressY <= y + XPFI_DOUBLE_CLICK_DISTANCE)
            {
                isDoubleClick = true;
            }
            g_xpfiLastPressValid = true;
            g_xpfiLastPressTimeMs = timestampMs;
            g_xpfiLastPressX = x;
            g_xpfiLastPressY = y;
            XWindowSystemInterface_handleMouseEvent_ex(
                window,
                isDoubleClick ? XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK
                              : XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XMouseButton_LeftButton, XMouseButton_LeftButton,
                XKeyboardModifier_NoModifier, local, &global, timestampMs);
        }
        else
        {
            if (x == g_xpfiLastX && y == g_xpfiLastY)
                return; /* 按压保持且未动：不重发。 */
            XWindowSystemInterface_handleMouseEvent_ex(
                window, XEVENT_TYPE_MOUSE_MOVE,
                XMouseButton_NoButton, XMouseButton_LeftButton,
                XKeyboardModifier_NoModifier, local, &global, timestampMs);
        }
        g_xpfiLastInjected = true;
        g_xpfiLastX = x;
        g_xpfiLastY = y;
        return;
    }
    if (!g_xpfiLastInjected)
        return; /* 悬空移动（未按压且无按压沿）不注入：触屏无 hover。 */
    /* 抬起沿：优先投递最后按压位置（抬起帧坐标偶发清零/跳变，
     * RELEASE 落在按下点语义更稳）。无论是否命中窗口都消费按压沿：
     * 按下态已随物理抬起终结，重放只会产生幽灵事件。 */
    g_xpfiLastInjected = false;
    if (g_xpfiLastX < 0)
        return; /* 从未成功注入过按下（窗口时序缺失）：无位可释放。 */
    if (!xpfi_windowAt(g_xpfiLastX, g_xpfiLastY, &window, &local))
    {
        /* 抬起点在窗外（边缘向外拖拽改尺寸后抬手）：有抓取时仍向抓取
           窗口注入 RELEASE，否则改尺寸状态机卡在按住态永不收尾。 */
        XWidget* grabber = XWidget_mouseGrabber();
        XRect wg;
        if (!grabber) return;
        window = XWidget_windowHandle(grabber);
        if (!window) return;
        wg = XWindow_geometry(window);
        local.x = g_xpfiLastX - wg.x;
        local.y = g_xpfiLastY - wg.y;
    }
    global.x = g_xpfiLastX;
    global.y = g_xpfiLastY;
    XWindowSystemInterface_handleMouseEvent_ex(
        window, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
        XMouseButton_LeftButton, XMouseButton_NoButton,
        XKeyboardModifier_NoModifier, local, &global, timestampMs);
}

/**
 * @brief      单个 evdev 报文的状态机更新（SYN_REPORT 之前只累积）。
 */
static void xpfi_feedEvent(const struct input_event* ev)
{
    switch (ev->type)
    {
    case EV_ABS:
        switch (ev->code)
        {
        case ABS_X:
        case ABS_MT_POSITION_X:
            g_xpfiRawX = (int)ev->value;
            g_xpfiHavePoint = true;
            break;
        case ABS_Y:
        case ABS_MT_POSITION_Y:
            g_xpfiRawY = (int)ev->value;
            g_xpfiHavePoint = true;
            break;
        case ABS_MT_TRACKING_ID:
            /* MT 协议 B：id>=0 触点生效，-1 抬起。仅在 BTN_TOUCH 缺席
             * 的设备上作为按压来源（置位时按下判定以此为准，见
             * xpfi_commitFrame 的优先级说明——这里直接维护按压态，
             * BTN_TOUCH 到来时覆盖之）。 */
            g_xpfiPressed = (ev->value >= 0);
            if (ev->value >= 0) g_xpfiHavePoint = true;
            break;
        case ABS_PRESSURE:
            /* 压力来源（TSC2007 等 ST 屏附带）：仅作"有触点"标记，
             * 按压态仍以 BTN_TOUCH 为准（压力>0 不改按压沿，避免
             * BTN_TOUCH 与压力报文顺序差异产生假抬起）。 */
            if (ev->value > 0) g_xpfiHavePoint = true;
            break;
        default:
            break;
        }
        break;
    case EV_KEY:
        if (ev->code == BTN_TOUCH)
            g_xpfiPressed = (ev->value != 0); /* ST 协议按压来源（优先）。 */
        break;
    default:
        break;
    }
}

/* ==================== 轮询泵（事件分发器每轮回调） ==================== */

/**
 * @brief      抽干 evdev 积压报文并按帧注入指针事件。
 * @details    非阻塞读循环：每次最多 32 报文，EAGAIN 即积压抽干。设备
 *             错误（非 EAGAIN 的负返回）时一次性告警并摘除自身（设备
 *             热拔后不逐轮刷屏重试；恢复需重新 register）。
 * @return     true 本轮回注入过事件；false 无事件（与原生事件泵约定
 *             一致，供分发器统计）。
 */
static bool xpfi_pump(void* userData)
{
    struct input_event batch[32];
    bool delivered = false;
    (void)userData;
    if (g_xpfiFd < 0) return false;
    for (;;)
    {
        ssize_t n = read(g_xpfiFd, batch, sizeof(batch));
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; /* 积压抽干。 */
            fprintf(stderr,
                    "[fbinput] ERROR: evdev 读取失败（errno=%d），"
                    "触摸输入已停用；恢复请重新注册。\n", errno);
            XPlatformFbInput_unregister();
            return delivered;
        }
        if (n == 0)
            break; /* EOF：设备关闭，无更多数据。 */
        {
            size_t count = (size_t)n / sizeof(struct input_event);
            size_t i;
            for (i = 0; i < count; ++i)
            {
                uint32_t timestampMs =
                    (uint32_t)(batch[i].time.tv_sec * 1000 +
                               batch[i].time.tv_usec / 1000);
                if (batch[i].type == EV_SYN && batch[i].code == SYN_REPORT)
                {
                    xpfi_commitFrame(timestampMs);
                    delivered = true;
                }
                else
                {
                    xpfi_feedEvent(&batch[i]);
                }
            }
        }
        if (n < (ssize_t)sizeof(batch))
            break; /* 不足整批：内核已无更多积压，尽快返回。 */
    }
    return delivered;
}

/* ==================== 契约注册入口 ==================== */

bool XPlatformFbInput_register(void)
{
    const char* path;
    char name[256] = "未知设备";
    struct input_absinfo abs;
    if (g_xpfiFd >= 0)
        return XAbstractEventDispatcher_instance(NULL) != NULL; /* 幂等。 */
    path = xpfi_devicePath();
    if (!path) return false; /* 运行期经环境变量显式禁用。 */
    g_xpfiFd = open(path, O_RDONLY | O_NONBLOCK);
    if (g_xpfiFd < 0)
    {
        fprintf(stderr,
                "[fbinput] WARNING: 无法打开触摸设备 %s（errno=%d），"
                "无输入路径；可用环境变量 XPLATFORM_FBINPUT_DEVICE 指定。\n",
                path, errno);
        return false;
    }
    /* 设备标识打一次（板级日志定位用；失败不致命）。 */
    if (ioctl(g_xpfiFd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
        strcpy(name, "未知设备");
    name[sizeof(name) - 1] = '\0';
    /* ABS 范围探测（归一基准）：ABS_X 缺席（纯 MT 屏只报 ABS_MT_*）时
     * 保持 0，归一化退化为坐标直通+钳位。 */
    memset(&abs, 0, sizeof(abs));
    if (ioctl(g_xpfiFd, EVIOCGABS(ABS_X), &abs) == 0)
        g_xpfiAbsXMax = abs.maximum;
    memset(&abs, 0, sizeof(abs));
    if (ioctl(g_xpfiFd, EVIOCGABS(ABS_Y), &abs) == 0)
        g_xpfiAbsYMax = abs.maximum;
    /* 厂商校准优先：/etc/pointercal 存在则坐标经 QWS 矩阵变换（含
     * 轴交换/旋转），覆盖下方线性归一回退路径。 */
    xpfi_loadPointercal();
    /* 读泵挂轮询链：与 XGuiApplication 原生事件泵同链分发。分发器须
     * 已存在（应用单例创建后），未就绪则失败回滚设备占用。 */
    if (XAbstractEventDispatcher_instance(NULL) == NULL)
    {
        fprintf(stderr,
                "[fbinput] ERROR: 事件分发器未就绪，请于 XGuiApplication "
                "创建后再调用 XPlatformFbInput_register。\n");
        close(g_xpfiFd);
        g_xpfiFd = -1;
        return false;
    }
    g_xpfiPumpHandle = XAbstractEventDispatcher_addPollCallback(xpfi_pump, NULL);
    if (!g_xpfiPumpHandle)
    {
        close(g_xpfiFd);
        g_xpfiFd = -1;
        return false;
    }
    g_xpfiRegistered = true;
    fprintf(stderr, "[fbinput] 触摸输入已接入：%s（%s，校准=%s）\n", path, name,
            g_xpfiCalLoaded ? "pointercal" : "线性归一回退");
    return true;
}

void XPlatformFbInput_unregister(void)
{
    if (g_xpfiRegistered)
    {
        XAbstractEventDispatcher_removePollCallback(g_xpfiPumpHandle);
        g_xpfiRegistered = false;
    }
    if (g_xpfiFd >= 0)
    {
        close(g_xpfiFd);
        g_xpfiFd = -1;
    }
    g_xpfiAbsXMax = 0;
    g_xpfiAbsYMax = 0;
    g_xpfiPressed = false;
    g_xpfiLastInjected = false;
    g_xpfiHavePoint = false;
    g_xpfiLastX = -1;
    g_xpfiLastY = -1;
}

bool XPlatformFbInput_isAvailable(void)
{
    return g_xpfiRegistered && g_xpfiFd >= 0;
}

#endif /* defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON && XPLATFORM_FBINPUT_ON */
