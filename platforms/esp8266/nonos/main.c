/**
 * @file main.c
 * @brief ESP8266 NONOS3 固件主入口（XinYueC 模板）
 *
 * @details 本文件只做四件事，**不含任何外设细节**：
 *            1. 初始化全部外设      → XDevice_init_all()   （一行）
 *            2. 拉起网络（SoftAP）  → net_init()
 *            3. 初始化框架 + shell  → console_init_once()
 *            4. 启动事件循环        → os_timer 5ms → cont_run → 每拍 XDevice_poll()
 *
 *          用户业务代码不要写在这里：写到 `app/user_business.c`，覆盖
 *          user_setup() / user_tick() 两个弱符号即可（见该文件说明）。
 *
 * @note 运行形态：单线程、无 FreeRTOS。user_init 跑完首拍即返回，由 libmain
 *       的 ets_run() 泵 os_timer；每 5ms 一拍 cont_run(pump_entry)，在 4KB
 *       cont 栈上跑完即返回，拍间零残留。
 */
#include <string.h>

#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "cont.h"

/* sys_check_timeouts：仓库 lwip 2.2.1 的定时器泵（ARP 老化/TCP 重传/ICMP）。
 * ⚠ 不要 #include "lwip/timeouts.h"：本机 include 路径会解析到 SDK 的 lwip2
 * 头（arch/cc.h → extra_include/ipv4_addr.h），与 nonos_v305 的 user_interface.h
 * 里的 legacy ip_addr.h 结构体/typedef 直接冲突（真机环境实测编译报
 * "redefinition of struct ip_info"）。仓库该符号无重命名（-D 列表里没有
 * sys_check_timeouts），直接声明即可。 */
extern void sys_check_timeouts(void);

#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XAbstractEventDispatcher_Protected.h"
#include "XEventDispatcher.h"
#include "XConsoleShell.h"
#include "XConsoleShellIo.h"
#include "XConsoleShellInfo.h"
#include "XConsoleShellMemory.h"
#include "XConsoleShellUptime.h"
#include "XConsoleShellSystem.h"
#include "XSerialPortESP8266.h"
#include "XMemory.h"

#include "drivers/xdevice_init.h"
#include "drivers/xdevice_uart.h"
#include "drivers/xdevice_oled.h"

/* 平台层：单调毫秒时基（XDateTime_currentMSecsSinceEpoch 的实现） */
extern void XPlatformStub_esp8266_timeInit(void);

/* 网络总闸：0 = 关（不初始化 WiFi、不泵 lwip 定时器、不编译 lwip）。
 * 见 cfg/xgui_shell_esp8266.h 的说明：先把框架原生事件循环跑通，
 * 把网络栈的变量摘干净。 */
#ifndef XESP8266_NET_ON
#define XESP8266_NET_ON 0
#endif

/* ---------------- 用户业务接入点（实现在 app/user_business.c） ----------------
 * 无 user_business.c 时用下面的空弱实现，固件照常构建。 */
void __attribute__((weak)) user_setup(void) {}
void __attribute__((weak)) user_tick(void) {}

extern void xm_stat_dump(const char* tag);
extern void xm_histogram_dump(const char* tag);
extern void xm_leak_dump(const char* tag);

/* ---------------- 静态 shell/IO（零堆需求） ---------------- */
static const XConsoleCommand* const xesp8266_commands[] = {
    &XConsoleShellInfo_command,
    &XConsoleShellMemory_command,
    &XConsoleShellUptime_command,
    &XConsoleShellClear_command,
    &XConsoleShellReset_command,
    &XConsoleShellReboot_command,
    &XConsoleShellExit_command
};

static XConsoleShell   s_staticShell;
static XConsoleShellIo s_staticIo;
static XCoreApplication* g_app = 0;

/* 串口读：直接读 UART0 RX FIFO（非空即取）。不用 XDeviceFile 链是因为
 * 它吃 ~12.4KB 堆。 */
static int64_t xesp_console_read(void* ud, void* data, size_t size)
{
    volatile unsigned int* fifo = (volatile unsigned int*)0x60000000;
    size_t n = 0;
    (void)ud;
    while (n < size) {
        if (*(volatile unsigned int*)0x6000001c & 0x7f) {   /* RX FIFO 非空 */
            *(unsigned char*)((unsigned char*)data + n) = (unsigned char)*fifo;
            ++n;
        }
        else {
            break;
        }
    }
    return (int64_t)n;
}

static int64_t xesp_console_write(void* ud, const void* data, size_t size)
{
    size_t n;
    (void)ud;
    for (n = 0; n < size; ++n)
        uart0_putc_raw(((const char*)data)[n]);
    return (int64_t)size;
}

static bool xesp_console_flush(void* ud) { (void)ud; return true; }

/* 异步输入源「附加」回调：串口没有 fd 可挂，靠 Shell 的 10ms 轮询定时器
 * 驱动读取。XConsoleShell_startAsync 只有 m_io.inputAttach 非空才会起
 * 轮询定时器；为 NULL 时它只把 m_running 置 1，串口字节永远没人读。 */
static bool xesp_console_input_attach(void* ud, XConsoleShell* shell)
{
    (void)ud; (void)shell;
    return true;
}

/* 每处理完一条完整命令后重打提示符，否则用户不知道可以继续输入。 */
static void xesp_console_prompt(void* ud, XConsoleShell* shell)
{
    (void)shell;
    xesp_console_write(ud, "\r\n> ", 4);
}

/* ---------------- 网络：SoftAP 'XinYueAP' + 静态 IP 192.168.4.1/24 ----------
 * 注意：dhcps_start 目前是空桩（胶水层 esp2glue_dhcps_start 不打服务器），
 * PC 需手工配静态 IP 192.168.4.2/24。详见 README「已知行为」。 */
static void ICACHE_FLASH_ATTR net_init(void)
{
#if XESP8266_NET_ON
    struct softap_config ap;
    struct ip_info ip;

    uart0_puts("[wfx] net_init begin\r\n");
    /* 对齐底座（实测 ping 0% 丢包）：不要加 wifi_set_opmode_current(PHY_MODE_11B)
     * / wifi_set_sleep_type 之类，它们会把 PHY 限制在 802.11b 并被怀疑是
     * 丢包差异来源。底座只做：opmode + softap 配置 + 静态 IP。 */
    wifi_set_opmode_current(SOFTAP_MODE);
    uart0_puts("[wfx] opmode=SOFTAP done\r\n");

    wifi_softap_dhcps_stop();
    if (!wifi_softap_get_config(&ap)) {
        memset(&ap, 0, sizeof ap);
        ap.channel = 1;
        ap.beacon_interval = 100;
        uart0_puts("[final] softap_get_config FAIL, using zeroed cfg\r\n");
    }
    memcpy(ap.ssid, "XinYueAP", 8);
    ap.ssid_len = 8;
    ap.password[0] = 0;
    ap.authmode = AUTH_OPEN;
    ap.ssid_hidden = 0;
    if (ap.channel < 1 || ap.channel > 13) ap.channel = 1;
    if (ap.max_connection == 0 || ap.max_connection > 8) ap.max_connection = 4;
    if (ap.beacon_interval < 100 || ap.beacon_interval > 60000) ap.beacon_interval = 100;
    uart0_puts(wifi_softap_set_config_current(&ap) ?
               "[final] softap set_config OK\r\n" : "[final] softap set_config FAIL\r\n");

    memset(&ip, 0, sizeof ip);
    {
        unsigned char* b;
        b = (unsigned char*)&ip.ip.addr;     b[0]=192; b[1]=168; b[2]=4; b[3]=1;
        b = (unsigned char*)&ip.netmask.addr;b[0]=255; b[1]=255; b[2]=255; b[3]=0;
        b = (unsigned char*)&ip.gw.addr;     b[0]=192; b[1]=168; b[2]=4; b[3]=1;
    }
    wifi_set_ip_info(SOFTAP_IF, &ip);
    uart0_puts(wifi_softap_dhcps_start()
                   ? "[wfx] dhcps_start OK\r\n"
                   : "[wfx] dhcps_start FAIL\r\n");
    uart0_puts("[wfx] net_init done\r\n");
#else
    /* NET=0：显式 NULLMODE。不能只"不调 wifi_*"——SDK 上电会从 flash 的
     * SYSTEM_PARAMETER 读旧的 opmode 自动跑 station/softap，PHY/扫描定时器
     * 全都会转起来。NULLMODE 把 WiFi 硬件彻底关掉，只留 SDK 库符号满足
     * libnet80211/libwpa 的链接依赖（它们硬引用 lwip 符号，所以 lwip 仍在
     * 镜像里，只是不初始化、不泵定时器）。 */
    wifi_set_opmode_current(NULL_MODE);
    uart0_puts("[wfx] net OFF (NULLMODE, XESP8266_NET_ON=0)\r\n");
#endif
}

/* ---------------- 阶段 0：框架 + shell 初始化（在 cont 4KB 栈上跑） -------- */
static int console_init_once(void)
{
    static char* argv[] = { (char*)"xinyuec", 0 };
    XAbstractEventDispatcher* dispatcher;
    size_t i;

    /* 串口波特率钉 74880（bootloader 同速，捕获终端同速）。 */
    XSerialPortESP8266_configureUart(74880);

    /* 2026-10-23：OLED 二次确认+整屏重画已移到 user_init（SDK SYS 大栈）。
     * 首拍 cont 栈的最高水位就是这里整屏字体重绘贡献的（8192/8192 打满），
     * 移出后 cont 栈才能安全缩到 6144 给 lwh 腾 2KB。 */

    g_app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, argv);
    if (!g_app) {
        uart0_puts("[app] XCoreApplication create FAIL\r\n");
        return 0;
    }
    dispatcher = XCoreApplication_eventDispatcher();
    uart0_puts(dispatcher ? "[app] dispatcher E1\r\n" : "[app] dispatcher E0\r\n");
    if (!dispatcher) {
        uart0_puts("[app] no event dispatcher\r\n");
        return 0;
    }

    memset(&s_staticIo, 0, sizeof(s_staticIo));
    s_staticIo.read = xesp_console_read;
    s_staticIo.write = xesp_console_write;
    s_staticIo.flush = xesp_console_flush;
    s_staticIo.inputAttach = xesp_console_input_attach;
    s_staticIo.prompt = xesp_console_prompt;
    XConsoleShell_init(&s_staticShell, &s_staticIo);
    dispatcher->m_d_ptr->m_consoleShell = &s_staticShell;
    /* 只注册内建没有的命令：XConsoleShell_init 已注册 help/version/echo/
     * clear/reset/reboot/exit 等内建（cmdCount=12），再注册同名命令会让
     * xcs_register_static_commands 的整批预检失败（一个重名 → 全批拒绝），
     * 连同批的 info/memory/uptime 也一起丢。逐个注册缺失的三个。 */
    (void)XConsoleShell_registerStaticCommands(&s_staticShell, xesp8266_commands[0], 1); /* info   */
    (void)XConsoleShell_registerStaticCommands(&s_staticShell, xesp8266_commands[1], 1); /* memory */
    (void)XConsoleShell_registerStaticCommands(&s_staticShell, xesp8266_commands[2], 1); /* uptime */
    uart0_puts("[app] cmdCount=");
    uart0_u((unsigned)s_staticShell.m_commandCount);
    uart0_puts("\r\n");
    /* 必须手动 startAsync：XConsoleShell_init 末尾的自动启动要求
     * m_io.inputAttach 非空，而我们是 init 之后才补上该回调。 */
    if (!XConsoleShell_startAsync(&s_staticShell))
        uart0_puts("[console] startAsync FAIL (no command response)\r\n");
    else
        uart0_puts("[console] async input started\r\n");
    xesp_console_write(0, "\r\n> ", 4);
    uart0_puts("[app] console init done\r\n");
    user_setup();   /* 用户业务初始化：事件循环启动前 */
    return 1;
}

/* ---------------- 事件循环：一拍 = 一次 processEvents + 外设轮询 ---------- */
static cont_t s_pumpCont __attribute__((aligned(16)));
static int s_phase = 0;      /* 0=init 未跑，≥1=事件循环 */
static unsigned s_beat = 0;

/* cont 栈高水位：cont_init 用 0xfeefeefe 哨兵填满整段栈，栈从高往低长，
 * 「还等于哨兵的前缀长度」= 没用到的余量。 */
static unsigned cont_stack_used(void)
{
    unsigned* p = s_pumpCont.stack;
    unsigned n = (unsigned)(sizeof(s_pumpCont.stack) / sizeof(unsigned));
    unsigned i = 0;
    while (i < n && p[i] == 0xfeefeefeu)
        ++i;
    return (n - i) * 4u;
}

static void ICACHE_FLASH_ATTR pump_entry(void)
{
    if (s_phase == 0) {
        s_phase = 1;
        console_init_once();
        if (g_app) XCoreApplication_processEvents(0);
        xm_stat_dump("[mem] after-init");
        xm_histogram_dump("[histo] after-init");
        return;
    }

    /* ---- 框架事件循环：框架原生形态（2026-10-22 恢复）----
     * 原生形态 = 「每拍调 XCoreApplication_processEvents(0)」，
     * 由框架自己的定时器体系（全局 XTimeWheelGroup 时间轮，1ms 刻度）
     * 驱动 CoarseTimer 到期——包括 XConsoleShell_startAsync 注册的
     * 10ms 串口轮询定时器。框架的 timerEvent → xcs_async_read_ready()
     * 直接读 UART，不分配事件，零 arena churn。
     *
     * 前提：平台层有有效单调毫秒时基（XDateTime_currentMSecsSinceEpoch
     * ��回有效值），否则时间轮无刻度、CoarseTimer 永不到期。
     * 时基此前为 -1 时，本移植版用「有输入才 processEvents + 手工
     * XConsoleShell_notifyInput 投事件」变通顶上，代价是每 5ms 一个
     * XEvent（空 FIFO 也投）→ arena churn；该变通现已删除。
     * 详见 PROGRESS §0-Y.6。 */
    if (g_app) {
        XCoreApplication_processEvents(0);   /* flags=0：不阻塞 */
    }

    user_tick();          /* 用户业务每拍轮询（非阻塞） */
    ++s_beat;

    /* 外设轮询：OLED 增量上屏，每拍一页（8 拍整屏）。 */
    (void)XDevice_poll();

    /* 统计每 1000 拍（5s）打一条。 */
    if ((s_beat % 1000u) == 0u) {
        uart0_puts("[gui] i2cErrs=");
        uart0_u(xdevice_oled_ack_errors());
        uart0_puts("\r\n");
        uart0_puts("[cont] stackUsed=");
        uart0_u(cont_stack_used());
        uart0_puts("/");
        uart0_u((unsigned)CONT_STACKSIZE);
        uart0_puts("\r\n");
        xm_stat_dump("[mem] beat");
        {
            extern uint32 system_get_free_heap_size(void);
            uart0_puts("[heap] sdk_free=");
            uart0_u((unsigned)system_get_free_heap_size());
            uart0_puts("\r\n");
        }
        xm_leak_dump("[leak] beat");
    }
}

static os_timer_t s_pumpTmr;
static void ICACHE_FLASH_ATTR pump_cb(void* arg)
{
    (void)arg;
    cont_run(&s_pumpCont, pump_entry);
}

/* ---------------- lwip 定时器（os_timer 上下文，ets_run 驱动） ------------
 * 仓库 lwip 需要外部泵 sys_check_timeouts()（ARP 老化 / TCP 重传 / ICMP）。
 * 底座固件就是靠这个 5ms 泵跑出 0% 丢包，必须保留。 */
static os_timer_t lwip_tmr;
static unsigned s_lwipTicks;
static void ICACHE_FLASH_ATTR lwip_tmr_cb(void* arg)
{
    (void)arg;
    sys_check_timeouts();
    if (++s_lwipTicks >= 1000u) {          /* 5ms × 1000 = 5s */
        extern uint32 system_get_free_heap_size(void);
        extern unsigned g_rxReq, g_rxOk, g_txOut, g_txFreed;
        extern void xpb_trace_dump(void);
        s_lwipTicks = 0;

        uart0_puts("[beat] sdk_free=");
        uart0_u((unsigned)system_get_free_heap_size());
        extern unsigned int xesp_lwh_capacity(void);
        extern unsigned int xesp_lwh_alloc_fail(void);
        uart0_puts(" lwhCap=");
        uart0_u(xesp_lwh_capacity());
        uart0_puts(" lwhFail=");
        uart0_u(xesp_lwh_alloc_fail());
        uart0_puts(" rxReq=");
        uart0_u(g_rxReq);
        uart0_puts(" rxOk=");
        uart0_u(g_rxOk);
        uart0_puts(" txOut=");
        uart0_u(g_txOut);
        uart0_puts(" txFreed=");
        uart0_u(g_txFreed);
        xm_stat_dump(" [mem] beat");
        xm_leak_dump(" [leak] beat");
#if XPB_TRACE_ON   /* 取证表已裁（cfg XPB_TRACE_ON=0）时该函数不存在 */
        xpb_trace_dump();
#endif
        uart0_puts("\r\n");
    }
}

/* ---------------- SDK3 分区表（4MB 非 FOTA，照抄已验收底座） --------------
 * ⚠ 必须留在 DRAM：.rodata 整段搬进 irom0（flash）后，SDK 在 user_pre_init
 * 里读这张表拿到的是代码字节，故用显式 .data 段名钉住。 */
static const partition_item_t at_partition_table[] __attribute__((section(".data"))) = {
    { SYSTEM_PARTITION_PHY_DATA,         0x3FB000, 0x1000 },
    { SYSTEM_PARTITION_RF_CAL,           0x3FC000, 0x1000 },
    { SYSTEM_PARTITION_SYSTEM_PARAMETER, 0x3FD000, 0x3000 },
};

/* ---- V9 总闸（cfg/xgui_shell_esp8266.h:XESP8266_FREERTOS_V9_ON，默认 0）----
 * =0：单线程事件循环（默认，已真机验证）。
 * =1：V9 模式 —— 建任务并起调度器（未在本机验证，见 PROGRESS）。 */
#if XESP8266_FREERTOS_V9_ON
#include "task.h"
#define V9_OS_STACK_WORDS 512
static void ICACHE_FLASH_ATTR v9_app_task(void* arg)
{
    (void)arg;
    for (;;) {
        if (g_app)
            XCoreApplication_processEvents(0);
        system_soft_wdt_feed();
        os_delay_ms(5);
    }
}
static void ICACHE_FLASH_ATTR v9_boot_tasks(void)
{
    if (xTaskCreate(v9_app_task, "app", V9_OS_STACK_WORDS, 0, 1, 0) != pdPASS)
        uart0_puts("[v9] xTaskCreate FAIL\r\n");
    xTaskStartScheduler();   /* 不返回 */
    uart0_puts("[v9] scheduler returned!?\r\n");
}
#endif /* XESP8266_FREERTOS_V9_ON */

void ICACHE_FLASH_ATTR user_pre_init(void)
{
    *(volatile unsigned int*)0x60000000 = 's';
    ets_install_putc1(xdevice_uart_putc1);   /* os_install_putc1 是它的宏别名 */
    *(volatile unsigned int*)0x60000000 = 'p';
    if (!system_partition_table_regist(at_partition_table, 3, system_get_flash_size_map())) {
        uart0_puts("partition fail\r\n");
        while (1) { }
    }
    *(volatile unsigned int*)0x60000000 = 'r';
}

void ICACHE_FLASH_ATTR user_init(void)
{
    *(volatile unsigned int*)0x60000000 = 'U';
    uart0_puts("[xinyuec] NONOS3 shell+lwip+XGui single-thread boot\r\n");
    uart0_puts("F");   /* 身份标记 */

    /* ---- 1. 全部外设初始化：一行搞定（细节见 drivers/xdevice_init.c）---- */
    if (!XDevice_init_all())
        uart0_puts("[xinyuec] WARN: some device init failed\r\n");

    /* ---- 1a. OLED 整屏重画（2026-10-23 从首拍 cont 栈移来）----
     * user_init 跑在 SDK SYS 栈（大），首拍 cont 栈（小）曾因这里的全量
     * 字体重绘打到 8192/8192。earlyInit 已画过 testPattern，这里补
     * 正式画面；cont 栈内只剩增量页刷（浅）。 */
    if (xdevice_oled_init())
        xdevice_oled_draw();

    /* ---- 1b. 平台毫秒时基（框架定时器的时基，必须早于任何定时器注册）----
     * XDateTime_currentMSecsSinceEpoch() 在本平台由它提供单调毫秒计数；
     * 缺了它框架的 CoarseTimer（时间轮）永不到期。 */
    XPlatformStub_esp8266_timeInit();

    /* ---- 2. 网络（SoftAP）---- */
    net_init();

    /* ---- 3. lwip 定时器：注册后由 ets_run 的 os_timer 泵驱动 ---- */
#if XESP8266_NET_ON
    os_timer_setfn(&lwip_tmr, lwip_tmr_cb, NULL);
    os_timer_arm(&lwip_tmr, 5, 1);
#endif

    /* ---- 4. 框架 + shell 首拍（cont 栈上跑），随后每 5ms 一拍事件循环 ---- */
    cont_init(&s_pumpCont);
    cont_run(&s_pumpCont, pump_entry);

    os_timer_setfn(&s_pumpTmr, pump_cb, NULL);
    os_timer_arm(&s_pumpTmr, 5, 1);

    /* ---- 5. 时基状态自检 ----
     * 框架的 CoarseTimer 依赖 XDateTime_currentMSecsSinceEpoch() 作为时间轮
     * 时基；本平台该值目前为 -1（无时基）⇒ 框架定时器全哑，串口输入靠
     * notifyInput 变通兜住（见 pump_entry 的说明）。这里打印出来便于
     * 确认平台时基状态，一旦框架侧修好、这里能变成有效值，就说明可以
     * 切回「每拍 processEvents」的原生形态。 */
    {
        extern int64_t XDateTime_currentMSecsSinceEpoch(void);
        int64_t tb = XDateTime_currentMSecsSinceEpoch();
        uart0_puts("[time] platform base = ");
        if (tb < 0)
            uart0_puts("UNAVAILABLE(-1) -> framework CoarseTimer idle\r\n");
        else {
            uart0_u((unsigned)tb);
            uart0_puts("ms\r\n");
        }
    }

    uart0_puts("[xinyuec] user_init returned -> ets_run\r\n");
    xm_stat_dump("[mem] boot");

#if XESP8266_FREERTOS_V9_ON
    v9_boot_tasks();
#endif
}
