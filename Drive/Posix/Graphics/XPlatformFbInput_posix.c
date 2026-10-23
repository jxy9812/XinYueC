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
 *             - 命中目标窗口 xpfi_windowAt 自持遍历 allWindows（全局
 *               像素按 XWindow_screenFootprint 反查顶层，主点取触点列表
 *               首点；**不用** XGuiApplication_topLevelAt——它用逻辑矩形
 *               比面板像素，旋转 footprint 对调后判定错误）后经
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
 *             【移动压缩 2026-10-11】pump 一圈把 evdev 积压全部抽干，
 *             重排昂贵时（拖拽分割条/面板全页重排期间）积压可达成串
 *             SYN_REPORT 帧，逐帧同步注入=一圈 N 个 TOUCH_UPDATE 逐一
 *             触发 N 次全页重排（昆仑通态 A33 真机拖拽持续满载根因）。
 *             同圈合并：一圈内仅最后一个「纯 UPDATE 帧」立即注入，同圈
 *             更早的纯 UPDATE 帧整体丢弃——拖拽语义按绝对位置无状态重
 *             算（按下锚/基准尺寸+总位移），中间移动帧丢尾无损（对标
 *             Qt XCB MOTION_NOTIFY 压缩 / Windows PeekMessage 抽干只
 *             处理最新位置的口径）。混合帧（含按下/抬起沿）与尾部纯
 *             UPDATE 帧照常即时注入：沿时序逐位不变，多指 per-id 抓取
 *             语义不受影响；抽干循环本圈不再注入被合并帧，其坐标已落
 *             槽表（rawX/rawY 帧间保持），由尾帧或下一圈代表末态。
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
#include "XWindowRotation.h" /* 命中 footprint 与窗口局部坐标的 R⁻¹ 逆变换。 */
#include "XWidget.h" /* mouseGrabber/windowHandle：按住拖出窗口界的注入。 */
#include "XVector.h"   /* XGuiApplication_allWindows 窗口登记表自持遍历。 */
#include "XGuiApplication.h" /* allWindows：命中测试自持遍历（不用 topLevelAt）。 */
#include "XSystem.h"
#include "XPlatformBackingStore.h" /* 光标面板坐标变化的原位搬运请求（fbdev 直写侧）。 */
#if XCURSOR_ON
#include "XCursor.h" /* 软件光标精灵：触摸位置喂给 XCursor_setPos。 */
#endif

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
 * @brief      把顶层窗**逻辑全局**几何矩形经旋转正映射为**面板设备**
 *             矩形（B 域；2026-10-22 弹层面板位根修输入侧配套）。
 * @details    XPlatformBackingStore_posix.c xpbs_topLevelDeviceRect 的
 *             同口径孪生（hit/局部换算/光标参考系与直写落笔必须同域，
 *             弹层「显示位=点击命中位」才成立）。口径：顶层窗几何 xy=
 *             桌面逻辑全局位（XComboBox/XMenu 等定位层逻辑域喂入），
 *             桌面逻辑域=面板宽高换轴（90/270）或同源（0/180）；
 *             XWindow_screenFootprint 保持 A 域旧口径不翻转（hideRestore/
 *             装饰拖拽等消费方继续用）。rot=0 恒等（逐字节零回归）。
 * @param      rot  窗口内容有效旋转角。
 * @param      geometry  窗口逻辑全局几何。
 * @param      info  面板 probe 信息（m_width/m_height=物理扫描宽高）。
 * @param      out   输出面板设备矩形（不钳面板界）。
 */
static void xpfi_topLevelDeviceRect(int rot, const XRect* geometry,
                                    const XPlatformDisplayInfo* info,
                                    XRect* out)
{
    int deskW;
    int deskH;
    int rx = geometry->x;
    int ry = geometry->y;
    int rw = geometry->width;
    int rh = geometry->height;
    int ox = 0;
    int oy = 0;
    int ow = 0;
    int oh = 0;
    if (XWindow_rotationSwapsAxes(rot))
    {
        deskW = info->m_height;
        deskH = info->m_width;
    }
    else
    {
        deskW = info->m_width;
        deskH = info->m_height;
    }
    XWindow_rotationMapRect(rot, deskW, deskH, &rx, &ry, &rw, &rh,
                            &ox, &oy, &ow, &oh);
    XRect_init(out, ox, oy, ow, oh);
}

/**
 * @brief      全局像素坐标命中顶层窗口并换算出窗口局部坐标。
 * @details    命中测试**自持遍历**（2026-10-18）：不走
 *             XGuiApplication_topLevelAt——它内部用逻辑矩形
 *             XRect_contains 比面板像素，footprint 对调后会拒绝真实
 *             footprint 的下缘、接受面板上不属于本窗的区域；该函数还有
 *             X11 侧调用方，改它会波及无关平台，故在本驱动内自持遍历
 *             allWindows 并按 XWindow_screenFootprint 判定。
 *             局部坐标：先减原点得**设备窗口局部**（原点不随旋转变，
 *             减法只用原点），再施加 R⁻¹ 得逻辑局部。R⁻¹ **不钳位**：
 *             负坐标域合法（按住拖出窗界的拖拽流依赖），-1 式自然
 *             连续外插（90° 下 sx=-1→ly=H）。
 * @return     命中的顶层窗口（借用指针）；无命中返回 false。
 */
static bool xpfi_windowAt(int x, int y, XWindow** outWindow, XPoint* outLocal)
{
    XWindow* fallback = NULL;
    XVector* windows;
    int64_t i;
    XPoint global;
    /* 【B 域命中 2026-10-22】面板 probe 一次取宽高（与 xpfi_normalizePoint
       同源）：rot≠0 顶层窗命中矩形须按旋转正映射面板域算（与直写落笔
       同域）。probe 失败回退 A 域 screenFootprint（=旧行为）。 */
    const XPlatformDisplayDriverOps* probeOps = XPlatformDisplayDriver_active();
    XPlatformDisplayInfo probeInfo;
    bool havePanel = false;
    memset(&probeInfo, 0, sizeof(probeInfo));
    if (probeOps && probeOps->probe && probeOps->probe(&probeInfo))
        havePanel = true;
    global.x = x;
    global.y = y;
    windows = XGuiApplication_allWindows();
    if (!windows) return false;
    /* 后登记的窗口视为更上层：倒序遍历，与 topLevelAt 口径一致。 */
    for (i = (int64_t)XVector_size_base((const XContainer*)windows) - 1;
         i >= 0; --i)
    {
        XWindow* w = XVector_At_Base(windows, i, XWindow*);
        XRect footprint;
        int rot, W, H, lx, ly;
        if (!w) continue;
        if (XWindow_parent(w, XWindowAncestor_ExcludeTransients) != NULL)
            continue;
        rot = XWindow_contentRotationDegrees(w);
        if (rot != 0 && havePanel)
        {
            XRect wg = XWindow_geometry(w);
            xpfi_topLevelDeviceRect(rot, &wg, &probeInfo, &footprint);
        }
        else
        {
            footprint = XWindow_screenFootprint(w);
        }
        if (!XRect_contains(&footprint, x, y)) continue;
        if (XWindow_isVisible(w)) { fallback = w; break; }
        if (!fallback) fallback = w;
    }
    XClassDelete((XClass*)windows);
    if (!fallback) return false;
    if (outLocal)
    {
        XRect geometry = XWindow_geometry(fallback);
        int W = geometry.width;
        int H = geometry.height;
        int rot = XWindow_contentRotationDegrees(fallback);
        /* 【B 域局部 2026-10-22】局部换算减**设备原点**（正映射 footprint
           左上）而非逻辑 geometry.xy：与命中矩形/直写落笔同域。R⁻¹ 基准
           仍取窗口自身逻辑宽高（own-basis 旋转与桌面旋转在窗内逐点同
           变换）。rot=0 或 probe 缺席时设备原点==逻辑 xy，逐位不变。 */
        int originX = geometry.x;
        int originY = geometry.y;
        if (rot != 0 && havePanel)
        {
            XRect dfp;
            xpfi_topLevelDeviceRect(rot, &geometry, &probeInfo, &dfp);
            originX = dfp.x;
            originY = dfp.y;
        }
        /* [跨平台编译修 2026-10-19] lx/ly 声明在 if 内（armel gcc-12
           C89 混合声明更严：X11 构建树见过的代码在 fbdev 树才编）。 */
        int lx = 0;
        int ly = 0;
        outLocal->x = x - originX;
        outLocal->y = y - originY;
        if (rot != 0)
        {
            lx = outLocal->x;
            ly = outLocal->y;
            XWindow_rotationUnmapPoint(rot, W, H, outLocal->x, outLocal->y,
                                       &lx, &ly);
            outLocal->x = lx;
            outLocal->y = ly;
        }
    }
    *outWindow = fallback;
    return true;
}

#if XCURSOR_ON
/**
 * @brief      记录软件光标位置的参考坐标系=指定窗口（命中/抓取窗口）
 *             的内容旋转角+整窗逻辑宽高+全局原点。
 * @details    XCursor_pos 存的是该窗口的局部坐标（原点/旋转逐窗不同），
 *             呈现层（XPlatformBackingStore_posix.c 的 fbdev 直写各精
 *             灵重画路径）经 XCursor_swPosFrame 取同一坐标系把精灵正
 *             变换到面板像素——精灵面板位置只随本喂给变化，与「哪个
 *             窗口恰好在上屏」无关（HUD 类自续重绘的其它顶层窗口交替
 *             present、条带旁路直写都不再把精灵画到换系错位处来回跳）。
 *             先记系后喂位：任何观察者见到的 (pos, frame) 恒同源。
 */
static void xpfi_cursorRecordWindowFrame(XWindow* window)
{
    XRect geometry;
    XPoint origin;
    if (!window) return;
    geometry = XWindow_geometry(window);
    XPoint_init(&origin, geometry.x, geometry.y);
    {
        /* 【B 域光标参考系 2026-10-22】rot≠0 时喂**设备原点**（正映射，
         * 与 xpfi_topLevelDeviceRect 命中/局部换算、直写落笔同域）：精灵
         * 呈现侧按 origin+R·pos 正变换，喂逻辑 xy 会把精灵画到弹层旧逻
         * 辑位。probe 缺席回退逻辑 xy（=旧行为）；rot=0 恒等逐位不变。 */
        const XPlatformDisplayDriverOps* ops =
            XPlatformDisplayDriver_active();
        XPlatformDisplayInfo info;
        int rot = XWindow_contentRotationDegrees(window);
        memset(&info, 0, sizeof(info));
        if (rot != 0 && ops && ops->probe && ops->probe(&info))
        {
            XRect dfp;
            xpfi_topLevelDeviceRect(rot, &geometry, &info, &dfp);
            origin.x = dfp.x;
            origin.y = dfp.y;
        }
    }
    XCursor_swSetPosFrame(XWindow_contentRotationDegrees(window),
                          geometry.width, geometry.height, &origin);
}

/**
 * @brief      把面板像素坐标换算成全局逻辑坐标喂给软件光标精灵。
 * @details    变换口径与事件分发一致（xpfi_windowAt 同源）：命中窗口先
 *             减窗口原点再施加 R⁻¹（XWindow_rotationUnmapPoint，不钳
 *             位），0° 旋转时面板坐标==逻辑坐标；命中失败回退显式鼠标
 *             抓取窗口（同一换算），再失败面板坐标直通（启动早期窗口
 *             未建的正常时序）。每次喂位同步记录参考坐标系（命中/抓取
 *             窗口系或恒等系，见 xpfi_cursorRecordWindowFrame）。
 *             XCursor_setPos 首次调用即点亮精灵的
 *             「已收到触摸位置」激活条件；X11/Win32 有硬件光标后端，
 *             精灵在 XCursor_swActive 内整体休眠，此处照常喂不碍事。
 */
static void xpfi_cursorFeedPanelPoint(int px, int py)
{
    XWindow* window = NULL;
    XPoint local;
    /* [SWC 探针 2026-10-23] 喂位一行账（面板点→窗口→局部→参考系）；
     * XGUI_SWCURSOR_DEBUG=1 开，默认零成本。 */
    static signed char swcDbg = -1;
    if (swcDbg < 0) swcDbg = (getenv("XGUI_SWCURSOR_DEBUG") != NULL);
    local.x = px;
    local.y = py;
    if (xpfi_windowAt(px, py, &window, &local) && window)
    {
        xpfi_cursorRecordWindowFrame(window);
        XCursor_setPos(local.x, local.y);
        if (swcDbg)
        {
            int frot = 0;
            int flogW = 0;
            int flogH = 0;
            XPoint forigin;
            XCursor_swPosFrame(&frot, &flogW, &flogH, &forigin);
            fprintf(stderr,
                    "[SWC] feed panel=(%d,%d) win=%p local=(%d,%d) "
                    "frame rot=%d logW=%d logH=%d org=(%d,%d)\n",
                    px, py, (void*)window, local.x, local.y, frot, flogW,
                    flogH, forigin.x, forigin.y);
        }
        return;
    }
    {
        XWidget* grabber = XWidget_mouseGrabber();
        window = grabber ? XWidget_windowHandle(grabber) : NULL;
    }
    if (window)
    {
        XRect geometry = XWindow_geometry(window);
        int rot = XWindow_contentRotationDegrees(window);
        /* 【B 域光标喂位 2026-10-22】同 xpfi_windowAt 局部换算口径：减
           设备原点（正映射）而非逻辑 xy；rot=0/probe 缺席逐位不变。 */
        int originX = geometry.x;
        int originY = geometry.y;
        if (rot != 0)
        {
            const XPlatformDisplayDriverOps* ops =
                XPlatformDisplayDriver_active();
            XPlatformDisplayInfo info;
            memset(&info, 0, sizeof(info));
            if (ops && ops->probe && ops->probe(&info))
            {
                XRect dfp;
                xpfi_topLevelDeviceRect(rot, &geometry, &info, &dfp);
                originX = dfp.x;
                originY = dfp.y;
            }
        }
        {
            int lx = px - originX;
            int ly = py - originY;
            if (rot != 0)
            {
                int ux = lx;
                int uy = ly;
                XWindow_rotationUnmapPoint(rot, geometry.width, geometry.height,
                                           lx, ly, &ux, &uy);
                lx = ux;
                ly = uy;
            }
            xpfi_cursorRecordWindowFrame(window);
            XCursor_setPos(lx, ly);
            return;
        }
    }
    /* 无窗口：面板坐标直通（rot==0 恒等价）；参考坐标系同步复位恒等
     * （呈现层 0° 换算与宽高/原点无关，按面板坐标取精灵）。 */
    XCursor_swSetPosFrame(0, 0, 0, NULL);
    XCursor_setPos(px, py);
}

/* 原位搬运触发的面板坐标判重簿记（上次触发时主点的面板像素位）：空白
 * 处 tap/拖动不产生控件脏区→无 present→精灵原地不动（跟随延迟根因
 * 2026-10-21）。帧提交喂位后检测面板坐标变化即同步直调
 * XPlatformBackingStore_requestCursorMotionRefresh（可见缓冲内原位搬
 * 运，只碰新旧两个 ≤12x19 精灵外接矩形）：泵跑在事件循环 poll 回调=
 * 主线程，present 只发生在事件派发/绘制调用栈内，单线程设计下 poll
 * 相不可能与 present 在途重叠——同步直调无重入，无需投递一拍（下一
 * 拍必达的底线以本拍即达满足）。触发侧只做坐标判重，「光标激活/位置
 * 未变/首帧前/pan 失败态」等全部守卫在 backing store 侧权威复判。 */
static bool s_xpfiCursorMotionInited = false; /**< 尚无已触发的面板坐标。 */
static int  s_xpfiCursorMotionX = 0;          /**< 上次触发时主点面板 X。 */
static int  s_xpfiCursorMotionY = 0;          /**< 上次触发时主点面板 Y。 */

/**
 * @brief      喂位后按需请求一次精灵原位搬运（主点面板坐标变化才触发）。
 * @details    帧界折叠的纯 UPDATE 帧不经本函数（不注入即不喂位）——拖
 *             动时搬运频率随 25ms 帧界钳自然限频，与注入/重排同拍；沿
 *             帧与圈界尾注帧照常即时触发。搬运频率上限=注入帧率，无每
 *             事件放大。
 */
static void xpfi_cursorRequestMotionRefresh(int px, int py)
{
    if (s_xpfiCursorMotionInited && s_xpfiCursorMotionX == px &&
        s_xpfiCursorMotionY == py)
        return; /* 面板坐标未变：精灵面板位置只随喂给变化，无事可做。 */
    s_xpfiCursorMotionInited = true;
    s_xpfiCursorMotionX = px;
    s_xpfiCursorMotionY = py;
    XPlatformBackingStore_requestCursorMotionRefresh();
}
#endif /* XCURSOR_ON */

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

/* ==================== 移动压缩（同圈纯 UPDATE 帧合并） ==================== */

/**
 * @brief      当前槽表状态是否为「无沿纯移动」帧（可被同圈折叠）。
 * @details    任一槽带 framePressed/frameReleased 即混合帧（沿时序必须
 *             即时保序注入，折叠会破坏 BEGIN/END 与 UPDATE 的先后与
 *             per-id 抓取语义）；按下沿注入失败保留重试期同样走混合
 *             路径即时提交（重试语义不变）。
 */
static bool xpfi_framePureUpdate(void)
{
    int i;
    for (i = 0; i < XPFI_SLOT_COUNT; ++i)
    {
        const XpfiTouchSlot* s = &g_xpfiSlots[i];
        if (s->framePressed || s->frameReleased) return false;
    }
    return true;
}

/* 同圈压缩簿记：圈内首个纯 UPDATE 帧即时注入后置位 committed；其后纯
 * UPDATE 帧不再 commitFrame（坐标已在槽表 rawX/rawY 帧间保持），仅记
 * 末帧时间戳；混合帧即时提交（其 UPDATED 判定与沿同帧消费折叠位移）
 * 并清账使后续首帧恢复即时注入；pump 抽干结束时若存在被折叠帧，按末
 * 帧时间戳补一次 commitFrame——整圈移动折叠为单帧末态注入，中间帧零
 * 注入零重排。
 * 【跨圈帧界 2026-10-11】同圈折叠只保证"一圈一次重排"，而重排+提交
 * 单帧 ~25ms（昆仑通态 A33）：真实手指报点 100~400Hz > 处理率时，圈
 * 与圈之间仍每圈一次重排 → 重排频率≈100Hz+ → CPU 持续跑满（拖拽卡
 * 顿根因第二层）。帧界门：距上次纯 UPDATE 提交 < XPF_MOVE_FRAME_MS
 * (33ms≈30fps) 时本圈纯 UPDATE 帧全部折叠（坐标照常落槽表，沿帧不
 * 受限），committed/skipped 跨圈保持——重排频率钳到 30fps，报点密度
 * 再高也不放大重排量。拖拽语义按绝对位置无状态重算，被折叠帧的位移
 * 由放行帧一次落位，零丢失。 */
#define XPF_MOVE_FRAME_MS 25 /* 实测单帧处理 8.35ms（A33 主页拖宽），25ms=40fps 且有 3 倍余量；33 曾=30fps */
static bool     s_xpfiMoveCommitted = false; /**< 帧界窗口内已提交过纯 UPDATE 帧（跨圈保持）。 */
static bool     s_xpfiMoveSkipped = false;   /**< 帧界内被折叠的纯 UPDATE 帧（跨圈保持）。 */
static uint32_t s_xpfiMoveTs = 0;            /**< 最近被折叠帧的时间戳。 */
static int64_t  s_xpfiMoveLastCommitMs = 0;  /**< 上次纯 UPDATE 帧提交的单调毫秒（帧界基准；0=从未）。 */

/**
 * @brief      把活动槽的当前 rawX/rawY 归一化推进 lastX/lastY（帧界折叠
 *             帧的坐标语义消费）。
 * @details    帧界内被折叠的纯 UPDATE 帧没有注入，槽 lastX/lastY 停在
 *             上次注入点；若就此抬起（混合帧 RELEASED 用 lastX/lastY
 *             落点），抬起会落回拖拽起点（物理末位置丢失）。折叠时把
 *             物理末位置推进 lastX/lastY，抬起沿落点=物理末位置，位移
 *             零丢失。
 */
static void xpfi_syncSlotLastFromRaw(void)
{
    int i;
    for (i = 0; i < XPFI_SLOT_COUNT; ++i)
    {
        XpfiTouchSlot* s = &g_xpfiSlots[i];
        if (!s->active || !s->injected || !s->havePoint) continue;
        xpfi_normalizePoint(s->rawX, s->rawY, &s->lastX, &s->lastY);
    }
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
                /* 落点=物理末位置：帧界折叠期间 rawX/rawY 持续推进而
                 * lastX/lastY 停在放行点，抬起沿用归一化末位置（havePoint
                 * 时）；无坐标上报才回退上次注入位置（历史口径：抬起帧
                 * 坐标偶发清零/跳变，落于原位语义更稳）。 */
                if (s->havePoint)
                    xpfi_normalizePoint(s->rawX, s->rawY, &x, &y);
                else
                {
                    x = s->lastX;
                    y = s->lastY;
                }
                s->injected = false;
                s->havePoint = false; /* 抬起沿已消费物理末位置，防下次接触误用。 */
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
#if XCURSOR_ON
    /* 软件光标精灵位置喂给（主点=列表首点，与下方命中测试同点）：
     * 按下/移动/抬起各沿收集到的坐标在此统一换算喂入（抬起沿保留末位
     * 坐标即照常更新一次）；先于注入命中早退，滑出窗界的帧精灵照跟。 */
    xpfi_cursorFeedPanelPoint(points[0].m_globalPosition.x,
                              points[0].m_globalPosition.y);
    /* 空白处 tap/拖动跟随延迟根修（2026-10-21）：交互控件帧随重绘
     * present 自然跟手，空白处帧无脏区→无 present→精灵原地不动。喂位
     * 后检测主点面板坐标变化即同步请求一次可见缓冲内原位搬运（fbdev
     * 直写侧权威复判激活/位置/首帧/pan 失败态；X11/win32 有硬件光标时
     * 该调用 no-op，零影响）。 */
    xpfi_cursorRequestMotionRefresh(points[0].m_globalPosition.x,
                                    points[0].m_globalPosition.y);
#endif
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
        if (!grabber)
        {
            /* 诊断探针（XGUI_ROT_DEBUG=1；命中失败且无抓取=丢帧）。 */
            static signed char dbg = -1;
            if (dbg < 0) dbg = (getenv("XGUI_ROT_DEBUG") != NULL);
            if (dbg)
                fprintf(stderr,
                        "[ROT] fbinput DROP panel=(%d,%d) edges p=%d r=%d "
                        "u=%d: no hit window, no grabber\n",
                        points[0].m_globalPosition.x,
                        points[0].m_globalPosition.y, pressedCount,
                        releasedCount, updatedCount);
            return; /* 按下沿保留至下帧重试；抬起沿已消费。 */
        }
        window = XWidget_windowHandle(grabber);
        if (!window)
        {
            static signed char dbg2 = -1;
            if (dbg2 < 0) dbg2 = (getenv("XGUI_ROT_DEBUG") != NULL);
            if (dbg2)
                fprintf(stderr,
                        "[ROT] fbinput DROP panel=(%d,%d): grabber %p has "
                        "no window handle\n",
                        points[0].m_globalPosition.x,
                        points[0].m_globalPosition.y, (void*)grabber);
            return;
        }
    }
    {
        /* 诊断探针（XGUI_ROT_DEBUG=1；每个注入帧的命中/派发链一行）。
           【B 域探针 2026-10-22】fp 记命中所用面板域矩形（rot≠0 正映射，
           与 xpfi_windowAt 同口径）；geom 仍记逻辑全局几何。 */
        static signed char dbg = -1;
        if (dbg < 0) dbg = (getenv("XGUI_ROT_DEBUG") != NULL);
        if (dbg)
        {
            XRect fp;
            XRect geom = XWindow_geometry(window);
            int probeRot = XWindow_contentRotationDegrees(window);
            if (probeRot != 0)
            {
                const XPlatformDisplayDriverOps* probeOps =
                    XPlatformDisplayDriver_active();
                XPlatformDisplayInfo probeInfo;
                memset(&probeInfo, 0, sizeof(probeInfo));
                if (probeOps && probeOps->probe &&
                    probeOps->probe(&probeInfo))
                    xpfi_topLevelDeviceRect(probeRot, &geom, &probeInfo,
                                            &fp);
                else
                    fp = XWindow_screenFootprint(window);
            }
            else
            {
                fp = XWindow_screenFootprint(window);
            }
            fprintf(stderr,
                    "[ROT] fbinput frame panel=(%d,%d) edges p=%d r=%d "
                    "u=%d -> win=%p type=%d vis=%d rot=%d "
                    "geom=(%d,%d %dx%d) fp=(%d,%d %dx%d)\n",
                    points[0].m_globalPosition.x,
                    points[0].m_globalPosition.y, pressedCount,
                    releasedCount, updatedCount, (void*)window,
                    (int)XWindow_type(window), XWindow_isVisible(window) ? 1 : 0,
                    probeRot, geom.x, geom.y,
                    geom.width, geom.height, fp.x, fp.y, fp.width, fp.height);
        }
    }
    geometry = XWindow_geometry(window);
    /* 窗口局部 = 面板全局 - 设备原点，其后 R⁻¹ 回逻辑局部（2026-10-18；
       【B 域局部 2026-10-22】rot≠0 时设备原点=几何正映射面板位
       （xpfi_topLevelDeviceRect，与命中/直写落笔同域）——减逻辑 xy 会把
       弹层显位处的触摸换算成错位局部坐标（点显处不选中）。rot=0 设备
       原点==逻辑 xy 逐位不变。R⁻¹ 不钳位：越界负坐标合法，拖出窗界的
       拖拽流依赖（处理器自行钳制）。此路覆盖鼠标抓取兜底分支（命中失
       败后按 grabber 改靶）；本局部 geometry 随后传入 xpfi_collectState
       （混合帧拆分同口径）。 */
    {
        int rot = XWindow_contentRotationDegrees(window);
        if (rot != 0)
        {
            const XPlatformDisplayDriverOps* geomOps =
                XPlatformDisplayDriver_active();
            XPlatformDisplayInfo geomInfo;
            memset(&geomInfo, 0, sizeof(geomInfo));
            if (geomOps && geomOps->probe && geomOps->probe(&geomInfo))
            {
                XRect dfp;
                xpfi_topLevelDeviceRect(rot, &geometry, &geomInfo, &dfp);
                geometry.x = dfp.x;
                geometry.y = dfp.y;
            }
        }
        for (i = 0; i < count; ++i)
        {
            int lx, ly;
            points[i].m_position.x =
                points[i].m_globalPosition.x - geometry.x;
            points[i].m_position.y =
                points[i].m_globalPosition.y - geometry.y;
            if (rot == 0) continue;
            lx = points[i].m_position.x;
            ly = points[i].m_position.y;
            XWindow_rotationUnmapPoint(rot, geometry.width, geometry.height,
                                       lx, ly, &lx, &ly);
            points[i].m_position.x = lx;
            points[i].m_position.y = ly;
        }
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
        {
            /* 诊断探针（XGUI_ROT_DEBUG=1）：混合帧拆分注入账。 */
            static signed char dbg = -1;
            if (dbg < 0) dbg = (getenv("XGUI_ROT_DEBUG") != NULL);
            if (dbg)
                fprintf(stderr,
                        "[ROT] fbinput mixed frame: split END/UPDATE/BEGIN "
                        "(p=%d r=%d u=%d)\n", pressedCount, releasedCount,
                        updatedCount);
        }
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
    {
        /* 诊断探针（XGUI_ROT_DEBUG=1）：投递失败（参数/分配）。 */
        static signed char dbg = -1;
        if (dbg < 0) dbg = (getenv("XGUI_ROT_DEBUG") != NULL);
        if (dbg)
            fprintf(stderr,
                    "[ROT] fbinput INJECT FAILED type=%d count=%d\n",
                    (int)type, count);
        return; /* 投递失败（参数/分配）：按下沿保留，下帧重试。 */
    }
    {
        /* 诊断探针（XGUI_ROT_DEBUG=1）：单态帧注入成功。 */
        static signed char dbg = -1;
        if (dbg < 0) dbg = (getenv("XGUI_ROT_DEBUG") != NULL);
        if (dbg)
            fprintf(stderr,
                    "[ROT] fbinput injected type=%d count=%d win=%p\n",
                    (int)type, count, (void*)window);
    }
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
                 * 槽，提交时作幽灵沿作废）。rawX/rawY+havePoint 保留：
                 * 抬起沿落点取物理末位置（帧界折叠期间 lastX/lastY 停
                 * 在放行点，2026-10-11 拖拽终态丢失根修）；提交消费后
                 * commitFrame 清 havePoint，防下一次接触误用旧坐标。 */
                int i;
                for (i = 0; i < XPFI_SLOT_COUNT; ++i)
                {
                    XpfiTouchSlot* s = &g_xpfiSlots[i];
                    if (s->active)
                    {
                        s->frameReleased = true;
                        s->active = false;
                    }
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
                    /* 移动压缩（见文件头）：纯 UPDATE 帧圈内首帧即时注
                     * 入、后续帧折叠（槽表 rawX/rawY 已保持末态）；混合
                     * 帧（含按下/抬起沿）即时提交保沿时序。折叠帧由圈
                     * 界尾注按末帧时间戳补注。 */
                    if (xpfi_framePureUpdate())
                    {
                        int64_t nowMs = (int64_t)timestampMs;
                        if (s_xpfiMoveLastCommitMs != 0 &&
                            nowMs < s_xpfiMoveLastCommitMs)
                            nowMs = s_xpfiMoveLastCommitMs; /* 时间戳回绕/乱序兜底。 */
                        if (!s_xpfiMoveCommitted &&
                            (s_xpfiMoveLastCommitMs == 0 ||
                             nowMs - s_xpfiMoveLastCommitMs >= XPF_MOVE_FRAME_MS))
                        {
                            xpfi_commitFrame(timestampMs);
                            s_xpfiMoveCommitted = true;
                            s_xpfiMoveLastCommitMs = (int64_t)timestampMs;
                            delivered = true;
                        }
                        else
                        {
                            s_xpfiMoveSkipped = true;
                            s_xpfiMoveTs = timestampMs;
                            xpfi_syncSlotLastFromRaw();
                        }
                    }
                    else
                    {
                        xpfi_commitFrame(timestampMs);
                        /* 混合帧已按沿+移动注入：本圈折叠账清零，其后
                         * 首个纯 UPDATE 帧恢复即时注入（更低延迟）。
                         * 帧界基准照常推进（沿帧不含移动折叠语义）。 */
                        s_xpfiMoveCommitted = false;
                        s_xpfiMoveSkipped = false;
                        s_xpfiMoveLastCommitMs = (int64_t)timestampMs;
                        delivered = true;
                    }
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
    if (s_xpfiMoveSkipped)
    {
        /* 圈界尾注：整圈折叠移动按末帧时间戳补注单帧——但仅当末帧已
         * 出帧界窗口：帧界内则连尾注一并折叠（坐标留槽表，下圈放行帧
         * 一次落位；防 3ms 报点下圈界补注变成 100Hz 重排的旁路）。
         * 末态无变化时 commitFrame 内部「未动不收集」自然空转。 */
        int64_t nowMs = (int64_t)s_xpfiMoveTs;
        if (s_xpfiMoveLastCommitMs != 0 && nowMs < s_xpfiMoveLastCommitMs)
            nowMs = s_xpfiMoveLastCommitMs;
        if (s_xpfiMoveLastCommitMs == 0 ||
            nowMs - s_xpfiMoveLastCommitMs >= XPF_MOVE_FRAME_MS)
        {
            xpfi_commitFrame(s_xpfiMoveTs);
            s_xpfiMoveLastCommitMs = (int64_t)s_xpfiMoveTs;
            s_xpfiMoveSkipped = false;
            delivered = true;
        }
    }
    s_xpfiMoveCommitted = false;
    return delivered;
}

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
#if XCURSOR_ON
    s_xpfiCursorMotionInited = false; /* 原位搬运判重簿记复位（重注册首
                                         帧必触发一次对位）。 */
#endif
}

bool XPlatformFbInput_isAvailable(void)
{
    return g_xpfiRegistered && g_xpfiFd >= 0;
}

#endif /* defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON && XPLATFORM_FBINPUT_ON */
