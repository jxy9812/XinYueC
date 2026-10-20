/**
 * @file user_main.c
 * @brief ESP8266 RTOS SDK 的固件入口。
 * @details
 * SDK 的启动路径固定为 user_init()——由 SDK 在其主任务中调用。应用不能
 * 在这里长时间阻塞，因此本文件只做一件事：建一个栈足够的 FreeRTOS 任务，
 * 在其中拉起 XinYueC 的核心对象与事件循环。
 *
 * 启动链与桌面版 main.c 保持一致：XCoreApplication_create 负责组装核心对象
 * 和事件分发器，XAbstractEventDispatcher_runDefaultShell 负责挂上默认
 * 控制台会话并进入事件循环。差别只在于控制台传输——桌面走 stdin/stdout，
 * 本平台由 Drive/ESP8266 把 stdin/stdout 接到 UART0，因此这里的代码路径
 * 与桌面完全相同。
 *
 * @note 波特率必须在任何输出之前设定。SDK 上电默认是 74880，而 CH340
 *       侧的终端通常按 115200 打开；先改 UART0 寄存器，之后 os_printf 与
 *       fwrite(stdout) 两条输出路径都会按新波特率走。
 */

#include "esp_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XConsoleShell.h"
#include "XConsoleShellInfo.h"
#include "XConsoleShellMemory.h"
#include "XConsoleShellSystem.h"
#include "XConsoleShellUptime.h"
#include "XSerialPortESP8266.h"

/** @brief 控制台波特率；与 Drive/ESP8266/Console 的默认值保持一致。 */
#define XESP8266_CONSOLE_BAUD 115200u

/** @brief 控制台任务栈（字节）。事件循环 + Shell 解析栈不浅，给足余量。 */
#define XESP8266_CONSOLE_STACK 8192

/**
 * @brief 最小构建保留的命令。
 * @note 必须逐个以 count=1 注册，不能把多个命令对象凑成静态数组：这些对象
 *       定义在各自的编译单元里，C 语言不允许拿跨编译单元的变量做静态初始化
 *       （会报 "initializer element is not constant"）。
 */
static const XConsoleCommand* const XESP8266_COMMANDS[] = {
    &XConsoleShellInfo_command,
    &XConsoleShellMemory_command,
    &XConsoleShellUptime_command,
    &XConsoleShellClear_command,
    &XConsoleShellReset_command,
    &XConsoleShellReboot_command,
    &XConsoleShellExit_command
};

static void xesp8266_console_task(void* parameter)
{
    static char* argv[] = { (char*)"xinyuec", NULL };
    XCoreApplication* app;

    (void)parameter;

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, argv);
    if (!app) {
        os_printf("[xinyuec] XCoreApplication 创建失败\r\n");
        vTaskDelete(NULL);
        return;
    }

    {
        XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
        if (dispatcher) {
            XConsoleShell* shell = XAbstractEventDispatcher_consoleShell(dispatcher);
            if (shell) {
                size_t count = sizeof(XESP8266_COMMANDS) / sizeof(XESP8266_COMMANDS[0]);
                size_t i;
                for (i = 0; i < count; ++i)
                    (void)XConsoleShell_registerStaticCommands(shell, XESP8266_COMMANDS[i], 1);
            }
            XAbstractEventDispatcher_runDefaultShell(dispatcher);
        }
        else {
            XCoreApplication_exec();
        }
    }

    XClassDelete(app);
    vTaskDelete(NULL);
}

/**
 * @brief SDK 强制的射频校准扇区声明。
 * @details SDK 会倒序划走末尾若干扇区，必须由应用显式指出哪一扇还空着，
 * 否则会覆盖 SDK 参数区。本板为 4MB Flash，取 128 - 5。
 */
uint32 user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
    case FLASH_SIZE_4M_MAP_256_256:
        rf_cal_sec = 128 - 5;
        break;
    case FLASH_SIZE_8M_MAP_512_512:
        rf_cal_sec = 256 - 5;
        break;
    case FLASH_SIZE_16M_MAP_512_512:
    case FLASH_SIZE_16M_MAP_1024_1024:
        rf_cal_sec = 512 - 5;
        break;
    /* 本板 4MB（32Mbit）。FLASH_SIZE_32M_MAP_2048_2048 是 NodeMCU 兼容值，
     * 一并处理，避免落到 default=0 把 RF 校准写到扇区 0（引导区）。 */
    case FLASH_SIZE_32M_MAP_512_512:
    case FLASH_SIZE_32M_MAP_1024_1024:
    case FLASH_SIZE_32M_MAP_2048_2048:
        rf_cal_sec = 1024 - 5;
        break;
    default:
        rf_cal_sec = 0;
        break;
    }
    return rf_cal_sec;
}

/** @brief SDK 应用入口：只做波特率设置与任务创建。 */
void user_init(void)
{
    /* 必须先于任何输出：SDK 默认 74880 与常见串口终端的 115200 不一致。 */
    XSerialPortESP8266_configureUart(XESP8266_CONSOLE_BAUD);

    xTaskCreate(&xesp8266_console_task, "console", XESP8266_CONSOLE_STACK,
                NULL, 2, NULL);
}
