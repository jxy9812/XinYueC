/**
 * @file ESP8266Minimal.h
 * @brief ESP8266 RTOS SDK 最小控制台裁剪档案。
 * @details
 * 本文件由产品构建通过 `-include` 强制包含，必须早于 CXinYueConfig.h 和
 * 任何模块 Config.h 被预处理。所有被裁剪的开关在各模块配置头里都是
 * `#ifndef` 保护的，因此在此处先定义即可生效。
 *
 * 目标形态：ESP8266 NodeMCU + CH340 串口控制台，保留 XCoreApplication
 * 事件循环、XConsoleShell 命令解释与少量内建命令；裁掉图形、网络、协议栈、
 * 文件系统、TUI/编辑器、加密与升级。目标是让固件在 40KB 堆 / 4MB Flash
 * 的 ESP8266 上稳定运行并可通过串口交互验证。
 *
 * 裁剪分三层：
 *   1. 模块级：XGUI/XNETWORK/XPROTOCOL/XACTION/XPROPERTY/XTUI 等整体关闭；
 *   2. 子模块级：XGui/XNetwork/XProtocol 内部的协议实现关闭；
 *   3. 命令级：XConsoleShell 的设备、文件、登录、SSH/Telnet、vim 子命令关闭。
 *
 * @note 本文件不包含任何平台头文件，也不调用平台 API。
 */

#ifndef ESP8266_MINIMAL_H
#define ESP8266_MINIMAL_H

/* ==================================================================== */
/* 1. 模块级总开关                                                        */
/* ==================================================================== */

/* 图形 / 属性 / 升级 / 加密：目前不能置 0。XVtable.h 的
 * XCLASS_VTABLE_GET_SIZE 会展开成 E<Class>_END_SIZE，并无条件引用
 * EXWidget_*、EXUpgradeStore_*、EXProperty_* 等枚举；对应模块一旦关闭，
 * XVtable.h 自身就编译不过。真正省体积的做法是让这些枚举在 XVtable.h 里
 * 加守卫，那是共享代码改动，不属于平台适配范围。
 * 待办：裁剪矩阵与虚表枚举解耦后，这四项可重新置 0。 */
#ifndef XGUI_DEFERRED_NOTE
#define XGUI_DEFERRED_NOTE 1
#endif
#ifndef XGUI_ON
#define XGUI_ON 0
#endif

/* 网络：控制台走 UART，不需要 lwIP 协议栈与 socket 层。 */
#ifndef XNETWORK_ON
#define XNETWORK_ON 0
#endif

/* 无线（Wi-Fi）平台抽象：芯片 WiFi 由 NONOS SDK 直接驱动，尚未接入框架的
 * XDeviceWiFi 契约（扫描/连接走 SDK 的 wifi_station_*），故先裁剪；接入后
 * 与本文件一并加入 esp8266 固件的 cfg/filelist_final.txt。 */
#ifndef XDEVICE_WIFI_ON
#define XDEVICE_WIFI_ON 0
#endif

/* 应用层协议：Modbus/MQTT/DDNS/NTP 等一律不编译。 */
#ifndef XPROTOCOL_ON
#define XPROTOCOL_ON 0
#endif

/* 动作/属性系统：桌面 GUI 的依赖，嵌入式控制台用不到。 */
#ifndef XACTION_ON
#define XACTION_ON 0
#endif
#ifndef XPROPERTY_ON
#define XPROPERTY_ON 0
#endif

/* TUI 与其 vim 编辑器：128x64 屏方案另起模块，不走桌面 TUI。 */
#ifndef XTUI_ON
#define XTUI_ON 0
#endif

/* 同步原语保留：FreeRTOS 平台的 XThread/XSemaphore/XMutex 走这条路径。 */
#ifndef XSYNC_ON
#define XSYNC_ON 1
#endif

/* 升级引擎与调试/演示/错误码框架：最小版一律关闭。 */
#ifndef XUPGRADE_ON
#define XUPGRADE_ON 0
#endif
#ifndef DEBUG_ON
#define DEBUG_ON 0
#endif
#ifndef DEMOTEST_ON
#define DEMOTEST_ON 0
#endif
#ifndef XERROR_ON
#define XERROR_ON 0
#endif

/* 加密：SSH/Telnet 后端关闭后无引用点。 */
#ifndef XCryptographic_ON
#define XCryptographic_ON 1  /* 不可裁：XHashMap/XString 等核心容器依赖其 XxHash64 */
#endif

/* ==================================================================== */
/* 2. 控制台命令裁剪                                                       */
/* ==================================================================== */

/* 保留：命令表、解析器、IO 回调、异步事件调度。 */
#ifndef XCONSOLE_SHELL_ON
#define XCONSOLE_SHELL_ON 1
#endif
#ifndef XCONSOLE_SHELL_COMMAND_ON
#define XCONSOLE_SHELL_COMMAND_ON 1
#endif
#ifndef XCONSOLE_SHELL_PARSER_ON
#define XCONSOLE_SHELL_PARSER_ON 1
#endif
#ifndef XCONSOLE_SHELL_IO_ON
#define XCONSOLE_SHELL_IO_ON 1
#endif
#ifndef XCONSOLE_SHELL_CALLBACK_COMMAND_ON
#define XCONSOLE_SHELL_CALLBACK_COMMAND_ON 1
#endif
#ifndef XCONSOLE_SHELL_HELP_ON
#define XCONSOLE_SHELL_HELP_ON 1
#endif
/* 系统类命令：ESP8266 有真实的 XSystem 后端（复位/重启），全部保留；
 * shutdown 除外——ESP8266 无法真正断电，XSystem 层已返回 NotSupported。*/
#ifndef XCONSOLE_SHELL_CLEAR_ON
#define XCONSOLE_SHELL_CLEAR_ON 1
#endif
#ifndef XCONSOLE_SHELL_RESET_ON
#define XCONSOLE_SHELL_RESET_ON 1
#endif
#ifndef XCONSOLE_SHELL_REBOOT_ON
#define XCONSOLE_SHELL_REBOOT_ON 1
#endif
#ifndef XCONSOLE_SHELL_EXIT_ON
#define XCONSOLE_SHELL_EXIT_ON 1
#endif
#ifndef XCONSOLE_SHELL_SHUTDOWN_ON
#define XCONSOLE_SHELL_SHUTDOWN_ON 0
#endif
#ifndef XCONSOLE_SHELL_ASYNC_ON
#define XCONSOLE_SHELL_ASYNC_ON 1
#endif
/* 控制台保持开启：事件分发器据此创建绑到 stdin/stdout 的默认 Shell，而
 * Drive/Espressif/ESP8266/Device/XDeviceFile_esp8266.c 已把 stdin/stdout
 * 接到 UART0。这样桌面那条启动路径在 ESP8266 上原样复用，共享代码零改动。 */
#ifndef XCONSOLE_SHELL_CONSOLE_ON
#define XCONSOLE_SHELL_CONSOLE_ON 1
#endif

/* 串口设备层：stdio 控制台的传输由 XSerialPort 承载，其平台后端是
 * XSerialPortESP8266.c。不开 XIODevice / XSerialPort 的 Shell 传输后端
 * —— 那是给"把任意串口挂成额外会话"用的，单路控制台用不上。 */
#ifndef XDEVICE_SERIALPORT_ON
#define XDEVICE_SERIALPORT_ON 1
#endif

/* 裁掉：设备类子命令（无对应驱动实现）。 */
#ifndef XCONSOLE_SHELL_GPIO_ON
#define XCONSOLE_SHELL_GPIO_ON 0
#endif
#ifndef XCONSOLE_SHELL_PWM_ON
#define XCONSOLE_SHELL_PWM_ON 0
#endif
#ifndef XCONSOLE_SHELL_SPI_ON
#define XCONSOLE_SHELL_SPI_ON 0
#endif
#ifndef XCONSOLE_SHELL_I2C_ON
#define XCONSOLE_SHELL_I2C_ON 0
#endif
#ifndef XCONSOLE_SHELL_ADC_ON
#define XCONSOLE_SHELL_ADC_ON 0
#endif
#ifndef XCONSOLE_SHELL_CAN_ON
#define XCONSOLE_SHELL_CAN_ON 0
#endif

/* 裁掉：文件系统、进程、登录（ESP8266 无 POSIX 进程模型）。 */
#ifndef XCONSOLE_SHELL_FILESYSTEM_ON
#define XCONSOLE_SHELL_FILESYSTEM_ON 0
#endif
#ifndef XCONSOLE_SHELL_EXTERNAL_PROCESS_ON
#define XCONSOLE_SHELL_EXTERNAL_PROCESS_ON 0
#endif
#ifndef XCONSOLE_SHELL_LOGIN_ON
#define XCONSOLE_SHELL_LOGIN_ON 0
#endif
#ifndef XCONSOLE_SHELL_LOGIN_REQUIRED_ON
#define XCONSOLE_SHELL_LOGIN_REQUIRED_ON 0
#endif

/* 裁掉：所有网络传输后端与 vim。 */
#ifndef XCONSOLE_SHELL_NETWORK_ON
#define XCONSOLE_SHELL_NETWORK_ON 0
#endif
#ifndef XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
#define XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON 0
#endif
#ifndef XCONSOLE_SHELL_XTELNETSERVER_BACKEND_ON
#define XCONSOLE_SHELL_XTELNETSERVER_BACKEND_ON 0
#endif
#ifndef XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
#define XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON 0
#endif
#ifndef XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON
#define XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON 0
#endif
#ifndef XCONSOLE_SHELL_XIODEVICE_BACKEND_ON
#define XCONSOLE_SHELL_XIODEVICE_BACKEND_ON 0
#endif
#ifndef XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON
#define XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON 0
#endif
#ifndef XCONSOLE_SHELL_VIM_ON
#define XCONSOLE_SHELL_VIM_ON 0
#endif

/* 裁掉：TUI 驱动的全屏编辑器入口。 */
#ifndef XCONSOLE_SHELL_EDITOR_ON
#define XCONSOLE_SHELL_EDITOR_ON 0
#endif
#ifndef XCONSOLE_SHELL_EDITOR_TUI_ON
#define XCONSOLE_SHELL_EDITOR_TUI_ON 0
#endif

/* ==================================================================== */
/* 3. 容量收缩                                                            */
/* ==================================================================== */
/* ESP8266 堆约 40KB，固定容量宏必须显式收敛，否则编译期即超出。 */

#ifndef XCONSOLE_SHELL_COMMAND_CAPACITY
#define XCONSOLE_SHELL_COMMAND_CAPACITY 16
#endif
#ifndef XCONSOLE_SHELL_HISTORY_CAPACITY
#define XCONSOLE_SHELL_HISTORY_CAPACITY 8
#endif
#ifndef XCONSOLE_SHELL_LINE_BUFFER_SIZE
#define XCONSOLE_SHELL_LINE_BUFFER_SIZE 64
#endif
#ifndef XCONSOLE_SHELL_MAX_ARGUMENTS
#define XCONSOLE_SHELL_MAX_ARGUMENTS 8
#endif
#ifndef XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE
#define XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE 128
#endif
#ifndef XCONSOLE_SHELL_MAX_SESSIONS
#define XCONSOLE_SHELL_MAX_SESSIONS 1
#endif
#ifndef XCONSOLE_SHELL_MULTI_SESSION_ON
#define XCONSOLE_SHELL_MULTI_SESSION_ON 0
#endif
#ifndef XCONSOLE_SHELL_MEMORY_POOL_ON
#define XCONSOLE_SHELL_MEMORY_POOL_ON 1  /* memory 命令表随此开关编译 */
#endif

/* 屏幕缓冲即使 TUI 关闭仍会被其它路径引用，收敛到 OLED 视口尺寸。 */
#ifndef XTUI_SCREEN_MAX_COLUMNS
#define XTUI_SCREEN_MAX_COLUMNS 21
#endif
#ifndef XTUI_SCREEN_MAX_ROWS
#define XTUI_SCREEN_MAX_ROWS 8
#endif

/* UTF-8 直通串口终端，不做 GBK 转换。注意不能用 "#define ... 0" 来关闭：
 * XCharFreeRTOS.c 用的是 defined() 判断，定义成 0 仍会命中 SYSTEM 分支并
 * 触发 #error。正确做法是不定义 SYSTEM，改选 CODE 模式。 */
#ifndef XCHAR_USE_SYSTEM_GBK
#undef XCHAR_USE_SYSTEM_GBK
#endif
#ifndef XCHAR_USE_CODE_GBK
#define XCHAR_USE_CODE_GBK 1
#endif

/* ==================================================================== */
/* 3.5 依赖第三方库的模块                                                  */
/* ==================================================================== */
/* 以下模块在桌面构建里链接外部库，xtensa 目标没有对应实现，必须裁掉：
 *   XRegularExpression -> pcre2      XSql    -> sqlite3
 *   XUpgrade           -> zlib        XCryptographic -> xxhash */

/* ==================================================================== */
/* 4. FreeRTOS 内核调参                                                     */
/* ==================================================================== */
/* 仓库的 FreeRTOSConfig.h 面向 ARM 应用，默认 32KB 堆 / 32 个优先级；
 * ESP8266 全芯片可用堆只有约 40KB，原样照搬会在链接期挤爆内存。
 * 这些宏在 FreeRTOSConfig.h 里都是 #ifndef 保护的，此处先定义即可生效。 */

#ifndef configTOTAL_HEAP_SIZE
/* 留出约 16KB 给 SDK 自身的 WiFi 协议栈与栈空间。 */
#define configTOTAL_HEAP_SIZE ((size_t)(24 * 1024))
#endif

#ifndef configMAX_PRIORITIES
/* 控制台 + SDK 任务实际用不到 8 个以上。 */
#define configMAX_PRIORITIES (8)
#endif

#ifndef XFONT_FREETYPE_ON
#define XFONT_FREETYPE_ON 0
#endif
#ifndef XEXCEL_ON
#define XEXCEL_ON 0
#endif

/* FreeRTOS：ESP8266 RTOS SDK 只有 V7.5.2，本仓库 Sync 层按 V9/V10 API 编写。
   打开 V7 兼容层补齐 pdMS_TO_TICKS 与任务 TLS（见 XFreeRTOSCompat.h）。
   ARM 构建用 V9.0.0，原生具备这三个接口，不定义本宏。 */
/* I/O 事件环：ESP8266 没有 epoll/IOCP 对应物，SDK 的定时器与事件循环
   不依赖它。关闭后 XAbstractEventDispatcher 不再创建全局事件环。
   注：该开关必须为 0——XAbstractNetIoRing_createPlatform() 的定义在
   "#if !XPLATFORM_HAS_OS" 下，而 ESP8266 走 XPLATFORM_FREERTOS（=有 OS），
   保持开启会导致链接期缺少该平台钩子。 */
/* 保持开启：XAbstractEventDispatcher / XFileDevice 共 17 处引用该模块 API，
   关闭它需要逐个守卫调用点。此处改为在平台侧提供返回 NULL 的
   createPlatform——XAbstractEventDispatcher 对 NULL 已做降级处理。 */
#ifndef XAbstractNetIoRing_ON
#define XAbstractNetIoRing_ON 1
#endif

#ifndef XFREERTOS_V7_COMPAT
#define XFREERTOS_V7_COMPAT 1
#endif

#ifndef XXmlStream_ON
#define XXmlStream_ON 0   /* XML 解析：ESP8266 控制台用不到 */
#endif
#ifndef XDOM_ON
#define XDOM_ON 0         /* 依赖 XXmlStream，一并关闭 */
#endif

#ifndef XRCODE_ON
#define XRCODE_ON 0   /* QRCode 编码：控制台用不到 */
#endif

#endif /* ESP8266_MINIMAL_H */
