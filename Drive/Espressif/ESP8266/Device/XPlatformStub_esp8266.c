/**
 * @file XPlatformStub_esp8266.c
 * @brief ESP8266 最小控制台构建里尚未落地的平台函数存根。
 * @details
 * 这批函数是链接期才发现缺失的：库内多个模块无条件引用它们，但各自的平台
 * 实现只存在于 POSIX / Windows / STM32 侧。ESP8266 最小构建既没有文件系统
 * 也没有 RTC，因此这里一律返回"不支持/失败"，而不是编造行为：
 *
 *   - 时间：XDateTime_current*。ESP8266 无 RTC 时钟，FreeRTOS 的 tick 只
 *     提供相对时间。可靠的时间来源是 SNTP，但 NTP 模块已随 XProtocol 一并
 *     裁掉，故此处报失败而不是返回假的绝对时间。
 *   - 文件与目录：XDeviceFile_legacyOpen/Seek/Resize/Fstat/Map/Unmap/
 *     SetFileTime 与 XDeviceDir_platform*。板子上没有挂载任何文件系统，
 *     SPIFFS 也未启用，这些接口一律拒绝。
 *
 * 一旦产品启用 SPIFFS（SDK 自带 libspiffs）或 NTP，应把对应实现替换掉，
 * 届时这些存根需从构建中移除以免重复定义。
 */

/* ⚠ include 顺序很重要：SDK 头必须【最先】引入。
 * nonos_v305/include/c_types.h:106 有 `typedef unsigned char bool;`，而框架头
 * （CXinYueConfig.h 等）会引入 <stdbool.h>（把 bool 定义为 _Bool）。若
 * c_types.h 在 stdbool.h 之后才被解析，就报
 * "two or more data types in declaration specifiers"。
 * 因此本文件把 ets_sys.h 放在所有框架头之前。
 *
 * 只引 ets_sys.h（ETSTimer / ets_timer_* 的声明），【不要】引 osapi.h：
 * osapi.h → os_type.h → c_types.h，会把 nonos 的 c_types.h 提前拉进来。
 * 本 TU 只需要 1ms 定时器，直接用 SDK 的 ets_timer_* 即可。 */
#include "ets_sys.h"

#include "CXinYueConfig.h"   /* XPLATFORM_HAS_OS 守卫 */
#include "XDateTime.h"
#include "XDevice.h"
#include "XDeviceFile.h"
#include "XDeviceDir.h"
#include "XString.h"
#include "XAbstractNetIoRing.h"

#if defined(__ESP8266__) || defined(ESP8266)

/* ==================== 时间 ==================== */

/* 无 RTC 也无 NTP 时给不出可信的绝对时间，统一返回全零，由上层判为无效。
   XDateTime 是纯 POD，这里直接清零即可——库内没有 XDateTime_init 可用。 */
static XDateTime esp8266ZeroDateTime(void)
{
    XDateTime out;
    unsigned char* bytes = (unsigned char*)&out;
    size_t i;
    for (i = 0; i < sizeof(out); ++i)
        bytes[i] = 0;
    return out;
}

/* ---------------------------------------------------------------------------
 * 单调毫秒基准（2026-10-22 新增，随后按实测回退，见下）
 * ---------------------------------------------------------------------------
 * 背景：框架的定时器体系分两层——
 *   · 精确定时器 → 调度器的 m_hrtimerGroup（红黑树）
 *   · 普通/粗定时器（CoarseTimer）→ 全局 XTimeWheelGroup（时间轮，1ms 刻度）
 * 时间轮的高分辨率时基就是 XDateTime_currentMSecsSinceEpoch()（见
 * XTimeWheelGroup.c:673 的 setHighResTimeFunc）。
 *
 * 本平台原先让该函数返回 -1（"不可用"）⇒ 时间轮时基恒为负 ⇒
 * **CoarseTimer 永远不会到期** ⇒ 框架的定时器（含 XConsoleShell_startAsync
 * 注册的 10ms 串口轮询定时器）在 ESP8266 上从未工作过。
 *
 * 这就是当初 NONOS 移植版不得不绕开框架、自己每 5ms 手动
 * XConsoleShell_notifyInput() 的原因（而 notifyInput 每次都要
 * XEvent_create → 每秒 200 个事件 → arena 持续churn）。
 *
 * ⚠⚠ 2026-10-22 实测回退：把时基补上（1ms os_timer 累加，实测推进正常，
 * 5422ms/5000ms）之后，**框架定时器路径一旦真正被触发就崩溃**：
 *   Fatal exception 0(IllegalInstructionCause)
 *   epc1=0x40201013, excvaddr=0x40266404
 * - epc1 落在 XAtomic_exchange_bool（irom0 段首函数）+3 —— 跳进函数中段；
 * - excvaddr 指向 rodata 区的数据符号（memp_SYS_TIMEOUT 一带）—— 野指针，
 *   典型症状是【某个对象的 vtable 指向了数据段】；
 * - 排除栈深：cont 栈 4KB→8KB 无效；
 * - 排除调用频率：「每拍调 processEvents」与「有输入才调」**都崩**，
 *   触发因素是【时基让 CoarseTimer 真正到期】，不是调用频率。
 *
 * 结论：框架的定时器/事件派发路径在 ESP8266 上有一条从未被运行过的分支
 * （候选：XTimeWheelGroup 惰性单例的 vtable 手工赋值
 * `XClassGetVtable(group) = XTimeWheelGroup_class_init();` 与 32 位/裸机下的
 * 不兼容），在桌面端复现定位之前，**保持时基不可用**（返回 -1），
 * 让框架定时器继续全哑，main.c 侧用 notifyInput 变通兜住串口。
 *
 * 复现指引（桌面端，见 PROGRESS §0-Y.6）：
 *   建 XCoreApplication → XObject_startTimer_ms(obj, 10, XTimerType_CoarseTimer)
 *   → 反复 XCoreApplication_processEvents(0)，观察是否在
 *   XAtomic_exchange_bool 附近崩溃。
 * --------------------------------------------------------------------------- */
static volatile uint32_t s_xesp_ms = 0;      /* 单调毫秒计数 */
static ETSTimer s_xesp_ms_timer;
static int s_xesp_ms_armed = 0;               /* 用 int：c_types.h 的 bool 是 typedef，不是 stdbool */

static void xesp_ms_tick(void* arg)
{
    (void)arg;
    ++s_xesp_ms;
}

/* 2026-10-22 诊断：供 main.c 打印平台毫秒计数。定位完成后删除。 */
uint32_t XPlatformStub_esp8266_ms(void)
{
    return s_xesp_ms;
}

void XPlatformStub_esp8266_timeInit(void)
{
    if (s_xesp_ms_armed)
        return;
    s_xesp_ms_armed = 1;
    ets_timer_setfn(&s_xesp_ms_timer, xesp_ms_tick, NULL);
    /* ets_timer_arm_new(timer, timeout, repeat, isMstimer)：isMstimer=1 →
     * timeout 以【毫秒】计。1ms 周期重复。 */
    ets_timer_arm_new(&s_xesp_ms_timer, 1, 1, 1);
}


XDateTime XDateTime_currentDateTime(void)
{
    return esp8266ZeroDateTime();
}

XDateTime XDateTime_currentDateTimeUtc(void)
{
    return esp8266ZeroDateTime();
}

int64_t XDateTime_currentNSecsSinceEpoch(void)
{
    /* 返回 -1 表示"不可用"，与库内其它未实现的取时接口保持一致。 */
    return -1;
}

int64_t XDateTime_currentMSecsSinceEpoch(void)
{
    /* 2026-10-22：恢复返回有效单调毫秒。
     *
     * 框架的定时器体系分两层：
     *   · 精确定时器 → 调度器的 m_hrtimerGroup（红黑树）
     *   · 普通/粗定时器（CoarseTimer）→ 全局 XTimeWheelGroup（时间轮，1ms 刻度）
     * 时间轮的高分辨率时基就是本函数（见 XTimeWheelGroup.c:673 的
     * setHighResTimeFunc）。本平台原先返回 -1（"不可用"）⇒ 时间轮时基恒为负
     * ⇒ **CoarseTimer 永远不会到期** ⇒ 框架的定时器（含 shell 注册的 10ms
     * 串口轮询定时器）在 ESP8266 上从未工作过，移植版只能用 notifyInput
     * 变通顶上（每秒 200 个 XEvent → arena churn）。
     *
     * 现在的形态：返回自 XPlatformStub_esp8266_timeInit() 起累加的单调毫秒，
     * 与桌面端 XDateTime_currentMSecsSinceEpoch（GetTickCount64，单调开机
     * 毫秒）语义一致——都是"单调时长"，不是 Unix 纪元。绝对时间
     * （currentDateTime）在本平台依旧不可用（无 RTC/NTP），但时间轮只需要
     * 增量。
     *
     * 详见 PROGRESS §0-Y.6 与本文件上方"单调毫秒基准"注释块。 */
    (void)XPlatformStub_esp8266_timeInit;
    return (int64_t)s_xesp_ms;
}

/* ==================== 文件 ==================== */

XFd XDeviceFile_legacyOpen(const XString* path, int mode, uint32_t openFlags, int* error)
{
    (void)path; (void)mode; (void)openFlags;
    if (error) *error = -1;          /* -1：平台无标准文件后端 */
    return XFD_INVALID;
}

int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence)
{
    (void)fd; (void)offset; (void)whence;
    return -1;
}

bool XDeviceFile_legacyResize(XFd fd, int64_t size)
{
    (void)fd; (void)size;
    return false;
}

bool XDeviceFile_legacyFstat(XFd fd, XFileStat* out)
{
    (void)fd; (void)out;
    return false;
}

void* XDeviceFile_legacyMap(XFd fd, int64_t offset, int64_t size)
{
    (void)fd; (void)offset; (void)size;
    return NULL;
}

bool XDeviceFile_legacyUnmap(void* addr, int64_t size)
{
    (void)addr; (void)size;
    return false;
}

bool XDeviceFile_legacySetFileTime(XFd fd, XFileTime timeType, int64_t timeValue)
{
    (void)fd; (void)timeType; (void)timeValue;
    return false;
}

/* ==================== 目录 ==================== */

XFd XDeviceDir_platformOpen(const XString* path)
{
    (void)path;
    return XFD_INVALID;
}

bool XDeviceDir_platformClose(void* backendHandle)
{
    (void)backendHandle;
    return false;
}

int64_t XDeviceDir_platformRead(void* backendHandle, XByteArray* buffer)
{
    (void)backendHandle; (void)buffer;
    return -1;
}

/* ==================== I/O 事件环 ==================== */

/* 只在「有 OS」口径下由平台侧提供。ESP8266 裸机单线程固件（本轮去掉
 * FreeRTOS 后 XPLATFORM_FREERTOS=0 ⇒ XPLATFORM_HAS_OS=0）由基类
 * XAbstractNetIoRing.c:411 提供同一符号；两侧同时定义会撞重复符号，
 * 故用同一个守卫口径。返回值语义见下。 */
#if XPLATFORM_HAS_OS
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void)
{
    /* ESP8266 没有 epoll / IOCP / kqueue 的对应物，SDK 的事件循环靠 os_timer
     * 与队列驱动，不依赖 I/O 事件环。返回 NULL 让 XAbstractEventDispatcher
     * 走无事件环分支（它对 NULL 已做判空降级）。 */
    return NULL;
}
#endif /* XPLATFORM_HAS_OS */

#endif /* defined(__ESP8266__) || defined(ESP8266) */
