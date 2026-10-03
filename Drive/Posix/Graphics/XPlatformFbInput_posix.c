/******************************************************************************
 * @file       XPlatformFbInput_posix.c
 * @brief      Linux fbdev 模式触摸输入驱动实现（linux/input.h evdev ->
 *             多点触摸事件注入）。
 * @details    实现 XPlatformFbInput_posix.h 的注册契约。事件链路（与
 *             X11 后端 XPlatformNativeWindow_posix.c 的 XI2 触摸注入
 *             同入口 handleTouchPoints_ex）：
 *             - 事件循环每轮经轮询回调进入 xpfi_pump，非阻塞 read 抽干
 *               evdev 积压报文（linux/input.h 的 struct input_event 流）；
 *             - 逐报文更新 8 槽触点状态机（协议 B，对标 Qt 6.8
 *               qevdevtouchhandler.cpp 的 per-slot 口径）：ABS_MT_SLOT
 *               切换当前槽；ABS_MT_TRACKING_ID 为身份判据（>=0 该槽
 *               生效并透传 id、-1 该槽本帧抬起）；ABS_MT_POSITION_X/Y
 *               写当前槽；BTN_TOUCH 为全局接触判据（=0 时全部槽视为
 *               释放）；ABS_PRESSURE>0 只补坐标有效性、不参与按压判
 *               定。纯 ST 屏（无 MT 轴，TSC2007 等电阻屏）退化单槽
 *               id=0：坐标 ABS_X/ABS_Y、按压沿 BTN_TOUCH；
 *             - 仅在 SYN_REPORT 帧边界提交状态机（半帧报文不注入）：
 *               收集本帧发生变化的槽构造 XTouchPoint 数组（id=身份 id；
 *               本帧新按=PRESSED、本帧抬=RELEASED、其余移动=UPDATED，
 *               无变化不注入），有按下沿投 TOUCH_BEGIN、有抬起沿投
 *               TOUCH_END、否则 TOUCH_UPDATE；坐标按「面板分辨率 +
 *               EVIOCGABS 范围」归一到像素（面板尺寸取活动显示驱动
 *               probe 快照，与直写显示同源；范围为 0 或未探测到时按
 *               设备报告值即像素直通——部分触摸屏 abs info 本就输出
 *               屏幕坐标；纯 MT 屏缺 ABS_X/ABS_Y 轴时以
 *               ABS_MT_POSITION 范围为归一基准）；
 *             - 命中目标窗口 XGuiApplication_topLevelAt（全局坐标反查
 *               顶层，主点取触点列表首点）后经
 *               XWindowSystemInterface_handleTouchPoints_ex 注入多点
 *               触摸（对标 QWindowSystemInterface::handleTouchEvent 的
 *               QEventPoint 列表形态；控件级命中/per-id 触摸抓取/
 *               touch→mouse 仿真/模态拦截由 VXWidgetWindow_event ->
 *               XWidget_dispatchTouchEvent 既有管线完成，本文件不重复
 *               实现）。
 *             命中失败时显式鼠标抓取窗口兜底（滑出窗界的拖拽流不断
 *             流），再失败静默丢弃本帧（启动早期窗口未建属正常时序）；
 *             按下沿在注入成功前保持（下帧重试），抬起沿消费即终不跨
 *             帧重放（防幽灵事件）；无接触的悬空坐标上报不注入（触屏
 *             无 hover 口径保持）。
 * @note       本文件只在「__linux__ && XGUI_ON && XPLATFORM_FBDEV_ON &&
 *             XPLATFORM_FBINPUT_ON」时参与编译（开关默认 0）。内存体
 *             系：驱动全程无堆分配，状态静态（触点槽表定容 8 槽）；
 *             单线程主循环设计（与 fbdev 显示驱动同口径），不加锁。
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
/** @brief ABS_MT_POSITION 范围（纯 MT 屏无 ABS_X/ABS_Y 轴时的线性
 *  归一基准；0 上限表示未探测到）。 */
static int g_xpfiAbsMtXMax = 0;
static int g_xpfiAbsMtYMax = 0;

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

/* ==================== 触点槽位状态机（协议 B，静态无堆） ==================== */

/** @brief 槽表容量：常见面板 <=5 指；超额槽位（10 指面板）钳到末槽
 *  （槽表静态定容，遵守文件头无堆分配纪律）。 */
#define XPFI_SLOT_COUNT 8

/**
 * @brief      单槽触点状态（SYN_REPORT 帧间保持；下标即协议 B 槽号，
 *             对标 Qt 6.8 qevdevtouchhandler.cpp 的 per-slot 数据）。
 */
typedef struct XpfiTouchSlot
{
    int32_t trackingId;    /**< 身份 id（ABS_MT_TRACKING_ID 透传；ST 恒 0）。 */
    bool    active;        /**< 接触态（身份判据维护，帧间保持）。 */
    bool    injected;      /**< 本接触序列按下沿已成功注入（抬起沿收尾依据）。 */
    bool    framePressed;  /**< 本帧新按沿（注入成功才清除，失败下帧重试）。 */
    bool    frameReleased; /**< 本帧抬起沿（提交时先消费，不跨帧重放）。 */
    bool    havePoint;     /**< 坐标有效（位置/压力已上报；抬起即失效）。 */
    int     rawX;          /**< 原始 X（abs 域，帧间保持）。 */
    int     rawY;          /**< 原始 Y（abs 域，帧间保持）。 */
    int     lastX;         /**< 上次注入像素 X（移动去抖/抬起落点）。 */
    int     lastY;         /**< 上次注入像素 Y。 */
} XpfiTouchSlot;

/** @brief 触点槽表（全 0 初始即全槽空闲）。 */
static XpfiTouchSlot g_xpfiSlots[XPFI_SLOT_COUNT];
/** @brief 当前槽游标（ABS_MT_SLOT 切换；纯 ST 协议恒 0）。 */
static int g_xpfiCurrentSlot = 0;
/** @brief 设备具备 MT 轴（注册期 EVIOCGBIT 探测）：true 按协议 B 槽位
 *  模型（ABS_X/ABS_Y 为内核 ST 仿真回显，忽略）；false 纯 ST 单槽。 */
static bool g_xpfiMtProtocol = false;

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
 * @brief      把单槽 evdev 原始坐标归一为面板像素坐标（逐槽复用）。
 * @details    面板宽高取活动显示驱动 probe 快照（fbdev 注册成功时必然
 *             可用；与直写显示同源，无需编译期分辨率）。abs 范围有效
 *             时线性映射，越界钳位；范围无效时视为设备直出屏幕坐标，
 *             仅钳位到面板范围。归一基准 ST 范围优先，纯 MT 屏缺
 *             ABS_X/ABS_Y 轴时退 ABS_MT_POSITION 范围。
 * @param      rawX 原始 X（abs 域）。
 * @param      rawY 原始 Y（abs 域）。
 * @param      outX 输出面板像素 X。
 * @param      outY 输出面板像素 Y。
 */
static void xpfi_normalizePoint(int rawX, int rawY, int* outX, int* outY)
{
    const XPlatformDisplayDriverOps* ops = XPlatformDisplayDriver_active();
    XPlatformDisplayInfo info;
    int width = 0;
    int height = 0;
    int xMax = g_xpfiAbsXMax > 0 ? g_xpfiAbsXMax : g_xpfiAbsMtXMax;
    int yMax = g_xpfiAbsYMax > 0 ? g_xpfiAbsYMax : g_xpfiAbsMtYMax;
    int x = rawX;
    int y = rawY;
    memset(&info, 0, sizeof(info));
    if (ops && ops->probe(&info))
    {
        width = info.m_width;
        height = info.m_height;
    }
    if (width < 1) width = xMax > 0 ? xMax + 1 : x + 1;
    if (height < 1) height = yMax > 0 ? yMax + 1 : y + 1;
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
        if (xMax > 0)
            x = (int)((int64_t)(x)* (width - 1) / xMax);
        if (yMax > 0)
            y = (int)((int64_t)(y)* (height - 1) / yMax);
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
    if (outLocal)
    {
        outLocal->x = x - geometry.x;
        outLocal->y = y - geometry.y;
    }
    *outWindow = window;
    return true;
}

/**
 * @brief      按状态过滤收集触点子集（混合帧拆分注入用）。
 * @details    从 commitFrame 收集好的整帧触点列表中摘出指定状态的点，
 *             槽指针数组随行（簿记用），本地坐标按窗口几何换算。返回
 *             子集点数（0=该状态本帧无点，调用方跳过注入）。
 */
static int xpfi_collectState(const XTouchPoint* points,
                             XpfiTouchSlot** slots, int count, int state,
                             const XRect* geometry, XTouchPoint* out,
                             XpfiTouchSlot** outSlots)
{
    int n = 0;
    int i;
    for (i = 0; i < count; ++i)
    {
        if (points[i].m_state != state) continue;
        out[n] = points[i];
        out[n].m_position.x = points[i].m_globalPosition.x - geometry->x;
        out[n].m_position.y = points[i].m_globalPosition.y - geometry->y;
        outSlots[n] = slots[i];
        n++;
    }
    return n;
}

/**
 * @brief      按槽位状态机收集本帧触点并注入多点触摸（SYN_REPORT 边界
 *             调用）。
 * @details    逐槽沿判定：本帧新按=PRESSED、本帧抬=RELEASED、其余坐标
 *             变化=UPDATED；无变化的帧不注入（含无接触悬空坐标上报——
 *             触屏无 hover 口径保持）。单态帧按沿合成单一事件（有按
 *             下沿投 TOUCH_BEGIN、有抬起沿投 TOUCH_END、否则
 *             TOUCH_UPDATE）；混合帧（收集态非单一：双指一抬一落、
 *             一抬一移、一落一移均为协议 B 常规时序）必须拆分注入——
 *             控件层 XWidget_dispatchTouchEvent 按状态过滤（BEGIN 只
 *             派发 PRESSED、UPDATE 只派发 UPDATED、END 只派发
 *             RELEASED），混投单一事件会丢沿并破坏该 id 的触点抓取
 *             （抬起沿混投漏派发对应控件停留按压态直至 CANCEL；END
 *             混携更新点则误摘仍按住手指的抓取，拖拽断流或改靶）；
 *             拆分顺序=先 END 关旧序列、后 BEGIN 开新序列（UPDATED 若
 *             有则单独补发 UPDATE）。目标窗口按主点（列表首点）全局
 *             坐标命中顶层；命中失败走显式鼠标抓取窗口兜底（滑出窗界
 *             的拖拽流不断流），再失败丢弃本帧——按下沿保留至下帧重
 *             试（启动早期窗口未建属正常时序），抬起沿消费即终不跨帧
 *             重放（防幽灵事件）。抬起触点落点沿用该槽上次注入位置
 *             （抬起帧坐标偶发清零/跳变，落于原位语义更稳，同旧鼠标
 *             路径口径）。
 */
static void xpfi_commitFrame(uint32_t timestampMs)
{
    XTouchPoint points[XPFI_SLOT_COUNT];
    XpfiTouchSlot* slots[XPFI_SLOT_COUNT];
    XWindow* window;
    XRect geometry;
    XEventType type;
    int count = 0;
    int pressedCount = 0;
    int releasedCount = 0;
    int updatedCount = 0;
    int i;
    for (i = 0; i < XPFI_SLOT_COUNT; ++i)
    {
        XpfiTouchSlot* s = &g_xpfiSlots[i];
        int state = -1; /* -1 本槽无点；其余取 XTOUCHPOINT_STATE_*。 */
        int x = 0;
        int y = 0;
        if (s->frameReleased)
        {
            /* 抬起沿先消费：无论后续是否命中窗口（物理抬起已终结，
             * 重放只产生幽灵事件）。 */
            s->frameReleased = false;
            if (s->injected)
            {
                state = XTOUCHPOINT_STATE_RELEASED;
                x = s->lastX;
                y = s->lastY;
                s->injected = false;
                releasedCount++;
            }
            else if (!s->active)
            {
                /* 按下从未注入（含按下沿注入失败期抬起）：幽灵沿一并
                 * 作废。active 时（同帧先抬后按的新身份）保留按下沿，
                 * 新接触下帧补 PRESSED，序列仍为 抬->按。 */
                s->framePressed = false;
            }
        }
        else if (s->framePressed)
        {
            if (s->havePoint)
            {
                xpfi_normalizePoint(s->rawX, s->rawY, &x, &y);
                state = XTOUCHPOINT_STATE_PRESSED;
                pressedCount++;
            }
            /* 坐标未上报：沿保持，等下一帧（不产无位按下）。 */
        }
        else if (s->active && s->injected && s->havePoint)
        {
            xpfi_normalizePoint(s->rawX, s->rawY, &x, &y);
            if (x != s->lastX || y != s->lastY)
            {
                state = XTOUCHPOINT_STATE_UPDATED; /* 未动不收集。 */
                updatedCount++;
            }
        }
        if (state >= 0)
        {
            points[count].m_id = s->trackingId;
            points[count].m_state = state;
            points[count].m_globalPosition.x = x;
            points[count].m_globalPosition.y = y;
            points[count].m_pressure = 1.0f; /* 无 valuator：契约缺省满压。 */
            slots[count] = s;
            count++;
        }
    }
    if (count == 0)
        return; /* 本帧无任何触点变化（含悬空移动）：不注入。 */
    /* 目标窗口：主点（列表首点）全局坐标命中顶层。 */
    if (!xpfi_windowAt(points[0].m_globalPosition.x,
                       points[0].m_globalPosition.y, &window, NULL))
    {
        /* 按住期间手指滑出窗口边界（窗口边缘向外拖拽改尺寸的必经路
           径）：有显式鼠标抓取（标题栏拖拽移动/边缘改尺寸 grabMouse）
           时整帧触点继续注入抓取窗口，否则边框拖出窗界即断流，只跟
           一小段就不再跟手（用户真机实测 2026-09-28）。局部坐标按窗
           口原点换算（越界为负值合法，处理器自行钳制）。 */
        XWidget* grabber = XWidget_mouseGrabber();
        if (!grabber) return; /* 按下沿保留至下帧重试；抬起沿已消费。 */
        window = XWidget_windowHandle(grabber);
        if (!window) return;
    }
    geometry = XWindow_geometry(window);
    for (i = 0; i < count; ++i)
    {
        points[i].m_position.x = points[i].m_globalPosition.x - geometry.x;
        points[i].m_position.y = points[i].m_globalPosition.y - geometry.y;
    }
    if ((pressedCount > 0) + (releasedCount > 0) + (updatedCount > 0) > 1)
    {
        /* 混合帧拆分注入（收集态非单一即拆：一抬一落、一抬一移、一落
         * 一移均属协议 B 面板常规时序；先 END、UPDATED 单独补发、后
         * BEGIN）：控件层按状态过滤派发（BEGIN 只派发 PRESSED/UPDATE
         * 只派发 UPDATED/END 只派发 RELEASED），混投单一事件必丢沿——
         * 「按下+抬起」丢抬起沿并泄漏该 id 抓取；「抬起+更新」以单
         * TOUCH_END 混投则丢更新沿，且 END 按负载内全部 id 逐 id 摘隐
         * 式抓取，仍按住的更新指抓取被误摘（后续点因查无抓取被整点
         * 丢弃，拖拽断流；抓取清空后退化逐帧命中，拖出控件界即改靶）
         * ——驱动侧抬起沿消费即终不再重放，损害无法自愈。各子集注入
         * 成败独立：抬起沿消费即终（失败亦不重放）；更新沿成功落位、
         * 失败下帧自然重报；按下沿失败保留沿重试语义照旧（framePressed
         * 未清则下帧重来）。 */
        XTouchPoint sub[XPFI_SLOT_COUNT];
        XpfiTouchSlot* subSlots[XPFI_SLOT_COUNT];
        int n;
        /* 1) 抬起沿：TOUCH_END（关旧序列；框架按事件携带 id 摘抓取）。 */
        n = xpfi_collectState(points, slots, count,
                              XTOUCHPOINT_STATE_RELEASED, &geometry, sub,
                              subSlots);
        if (n > 0)
            (void)XWindowSystemInterface_handleTouchPoints_ex(
                window, XEVENT_TYPE_TOUCH_END, sub, n, timestampMs);
        /* 2) 更新沿：TOUCH_UPDATE（成功才落位，失败下帧自然重报）。 */
        n = xpfi_collectState(points, slots, count,
                              XTOUCHPOINT_STATE_UPDATED, &geometry, sub,
                              subSlots);
        if (n > 0 &&
            XWindowSystemInterface_handleTouchPoints_ex(
                window, XEVENT_TYPE_TOUCH_UPDATE, sub, n, timestampMs))
        {
            for (i = 0; i < n; ++i)
            {
                subSlots[i]->lastX = sub[i].m_globalPosition.x;
                subSlots[i]->lastY = sub[i].m_globalPosition.y;
            }
        }
        /* 3) 按下沿：TOUCH_BEGIN（开新序列；成功簿记，失败保留沿）。 */
        n = xpfi_collectState(points, slots, count,
                              XTOUCHPOINT_STATE_PRESSED, &geometry, sub,
                              subSlots);
        if (n > 0 &&
            XWindowSystemInterface_handleTouchPoints_ex(
                window, XEVENT_TYPE_TOUCH_BEGIN, sub, n, timestampMs))
        {
            for (i = 0; i < n; ++i)
            {
                subSlots[i]->framePressed = false;
                subSlots[i]->injected = true;
                subSlots[i]->lastX = sub[i].m_globalPosition.x;
                subSlots[i]->lastY = sub[i].m_globalPosition.y;
            }
        }
        return;
    }
    type = pressedCount > 0 ? XEVENT_TYPE_TOUCH_BEGIN
         : (releasedCount > 0 ? XEVENT_TYPE_TOUCH_END
                              : XEVENT_TYPE_TOUCH_UPDATE);
    if (!XWindowSystemInterface_handleTouchPoints_ex(window, type, points,
                                                     count, timestampMs))
        return; /* 投递失败（参数/分配）：按下沿保留，下帧重试。 */
    /* 注入成功的簿记：按下沿消费、坐标落位；抬起槽保持 injected=false
     * 与原落点（lastX/lastY 留作下个接触序列的初值无碍，按下沿会覆写）。 */
    for (i = 0; i < count; ++i)
    {
        XpfiTouchSlot* s = slots[i];
        if (points[i].m_state != XTOUCHPOINT_STATE_RELEASED)
        {
            s->framePressed = false;
            s->injected = true;
            s->lastX = points[i].m_globalPosition.x;
            s->lastY = points[i].m_globalPosition.y;
        }
    }
}

/**
 * @brief      单个 evdev 报文的槽位状态机更新（SYN_REPORT 之前只累积）。
 * @details    判据分工（对齐文件头按压口径，不再"同帧最后写入者"）：
 *             BTN_TOUCH=全局接触判据（=0 全部活动槽视为本帧释放）；
 *             ABS_MT_TRACKING_ID=身份判据（>=0 该槽生效并透传 id、-1
 *             该槽本帧释放）；ABS_PRESSURE>0 仅补坐标有效性、不置按压。
 *             注册期协议探测分流：MT 设备忽略 ABS_X/ABS_Y（内核 ST
 *             仿真回显，写入会污染多指槽态）；纯 ST 设备坐标走
 *             ABS_X/ABS_Y、按下沿落单槽 0（id 恒 0）。
 */
static void xpfi_feedEvent(const struct input_event* ev)
{
    XpfiTouchSlot* slot = &g_xpfiSlots[g_xpfiCurrentSlot];
    switch (ev->type)
    {
    case EV_ABS:
        switch (ev->code)
        {
        case ABS_MT_SLOT:
            /* 协议 B 槽位游标：后续 MT 字段落到该槽；超出槽表容量的
             * 值钳到末槽（静态定容防越界写，多出的指并入末槽）。 */
            if (ev->value < 0)
                g_xpfiCurrentSlot = 0;
            else if (ev->value >= XPFI_SLOT_COUNT)
                g_xpfiCurrentSlot = XPFI_SLOT_COUNT - 1;
            else
                g_xpfiCurrentSlot = (int)ev->value;
            break;
        case ABS_MT_TRACKING_ID:
            if (ev->value >= 0)
            {
                /* 身份生效：新沿置按下标记并透传 id（id 中途变化亦随
                 * 写，控件层按 id 匹配触点抓取）。 */
                if (!slot->active)
                {
                    slot->framePressed = true;
                    slot->active = true;
                }
                slot->trackingId = (int32_t)ev->value;
            }
            else
            {
                /* -1：该槽本帧抬起（与 BTN_TOUCH=0 全槽释放路径幂等）。 */
                if (slot->active)
                {
                    slot->frameReleased = true;
                    slot->active = false;
                }
                slot->havePoint = false;
            }
            break;
        case ABS_MT_POSITION_X:
            slot->rawX = (int)ev->value;
            slot->havePoint = true;
            break;
        case ABS_MT_POSITION_Y:
            slot->rawY = (int)ev->value;
            slot->havePoint = true;
            break;
        case ABS_X:
            /* ST 协议坐标（仅纯 ST 设备采用；MT 设备此字段为内核单点
             * 仿真回显，多指语义歧义，忽略以保槽位一致性）。 */
            if (!g_xpfiMtProtocol)
            {
                slot->rawX = (int)ev->value;
                slot->havePoint = true;
            }
            break;
        case ABS_Y:
            if (!g_xpfiMtProtocol)
            {
                slot->rawY = (int)ev->value;
                slot->havePoint = true;
            }
            break;
        case ABS_PRESSURE:
            /* 压力>0 只补坐标有效性（数据帧佐证），按压判定不经此通
             * 道——避免 BTN_TOUCH/压力报文顺序差异产生假沿（头注释
             * 优先级口径自本版起成立）。 */
            if (ev->value > 0)
                slot->havePoint = true;
            break;
        default:
            break;
        }
        break;
    case EV_KEY:
        if (ev->code == BTN_TOUCH)
        {
            if (ev->value != 0)
            {
                /* 全局接触判据成立。MT 协议：按下身份交给
                 * ABS_MT_TRACKING_ID（各槽各沿），此处不改槽态；纯 ST：
                 * 无身份轴，按下沿落单槽 0（退化 id=0）。 */
                if (!g_xpfiMtProtocol)
                {
                    XpfiTouchSlot* s0 = &g_xpfiSlots[0];
                    if (!s0->active)
                    {
                        s0->framePressed = true;
                        s0->active = true;
                        s0->trackingId = 0;
                    }
                }
            }
            else
            {
                /* 接触结束：全部槽视为本帧释放（含按下沿注入失败期的
                 * 槽，提交时作幽灵沿作废）；坐标有效性一并失效。 */
                int i;
                for (i = 0; i < XPFI_SLOT_COUNT; ++i)
                {
                    XpfiTouchSlot* s = &g_xpfiSlots[i];
                    if (s->active)
                    {
                        s->frameReleased = true;
                        s->active = false;
                    }
                    s->havePoint = false;
                }
            }
        }
        break;
    default:
        break;
    }
}

/* ==================== 轮询泵（事件分发器每轮回调） ==================== */

/**
 * @brief      抽干 evdev 积压报文并按帧注入多点触摸事件。
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
    /* 位图缓冲按 ABS_MT_TRACKING_ID 定容（恒 8 字节，覆盖全部 MT 位）：
     * ABS_MAX 随内核头版本漂移（旧头 ABS_MAX=0x2f 时 (ABS_MAX+7)/8 仅
     * 6 字节，EVIOCGBIT 按请求截短返回 → MT 协议探测静默失效，且
     * absBits[ABS_MT_POSITION_X>>3] 越界读）。 */
    unsigned char absBits[(ABS_MT_TRACKING_ID + 7) / 8];
    int absBitsLen;
    int haveMtAxis = 0;
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
    /* 协议探测：EVIOCGBIT(EV_ABS) 查 MT 轴位——具备 ABS_MT_SLOT/
     * ABS_MT_POSITION_X 即按协议 B 8 槽模型走（ABS_X/ABS_Y 视为内核
     * ST 仿真回显忽略）；缺席退纯 ST 单槽（对标 Qt qevdevtouch 的
     * 协议分流）。返回字节数须覆盖被测位所在字节。 */
    memset(absBits, 0, sizeof(absBits));
    absBitsLen = ioctl(g_xpfiFd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits);
    if (absBitsLen > (int)(ABS_MT_POSITION_X / 8))
    {
        g_xpfiMtProtocol =
            ((absBits[ABS_MT_SLOT >> 3] >> (ABS_MT_SLOT & 7)) & 1) ||
            ((absBits[ABS_MT_POSITION_X >> 3] >> (ABS_MT_POSITION_X & 7)) & 1);
    }
    /* 纯 MT 屏补探 ABS_MT_POSITION 范围作线性归一基准（老面板原始域
     * 0..4095 直通像素会错位）；轴存在性同时作为协议判定的第二信源
     * （个别内核 EVIOCGBIT 回抄字节数偏短时位图测试失灵，双保险）。
     * 判存在必须看 maximum>0：内核 evdev 的 EVIOCGABS 对任意轴码只查
     * absinfo 是否存在、不查 absbit——设备只要报过 ABS_X/ABS_Y（触摸
     * 屏都报），对缺席的 MT 轴也 ioctl 成功并读回全零 absinfo；仅凭
     * 调用成功会把纯 ST 屏（TSC2007 类，本文件明示服务对象）误判成
     * MT 屏（忽略 ABS_X/ABS_Y 且 TRACKING_ID 永不到来）→ 触摸全死。
     * 真实 MT 位置轴 maximum 恒>0，缺席轴读回 0，以此区分。 */
    memset(&abs, 0, sizeof(abs));
    if (ioctl(g_xpfiFd, EVIOCGABS(ABS_MT_POSITION_X), &abs) == 0 &&
        abs.maximum > 0)
    {
        g_xpfiAbsMtXMax = abs.maximum;
        haveMtAxis = 1;
    }
    memset(&abs, 0, sizeof(abs));
    if (ioctl(g_xpfiFd, EVIOCGABS(ABS_MT_POSITION_Y), &abs) == 0 &&
        abs.maximum > 0)
    {
        g_xpfiAbsMtYMax = abs.maximum;
        haveMtAxis = 1;
    }
    if (haveMtAxis)
        g_xpfiMtProtocol = true;
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
    fprintf(stderr, "[fbinput] 触摸输入已接入：%s（%s，%s，校准=%s）\n", path,
            name, g_xpfiMtProtocol ? "MT 协议 B 8 槽" : "ST 单点协议",
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
    g_xpfiAbsMtXMax = 0;
    g_xpfiAbsMtYMax = 0;
    g_xpfiMtProtocol = false;
    memset(g_xpfiSlots, 0, sizeof(g_xpfiSlots)); /* 全槽复位（含沿标记）。 */
    g_xpfiCurrentSlot = 0;
}

bool XPlatformFbInput_isAvailable(void)
{
    return g_xpfiRegistered && g_xpfiFd >= 0;
}

#endif /* defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON && XPLATFORM_FBINPUT_ON */
