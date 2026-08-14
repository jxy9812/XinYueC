# XConsoleShell 设计文档

## 1. 文档状态

- 模块：XConsoleShell
- 状态：核心实现已落地，本文档同时作为实现与验收基线
- 语言：C99，兼容 C++11 调用
- 目标：裸机、自研 RTOS、FreeRTOS、Zephyr 适配环境、嵌入式 Linux
- 主要依赖：CXinYueConfig.h、XMemory.h、XObject.h、XPrintf.h
- 可选库依赖：XFileSystem.h、XIODevice.h、XSerialPort.h、XTcpSocket.h、XTcpServer.h、XEventLoop.h

本文档引用仓库根目录的《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》。该规范是本模块公共头文件、生命周期、内存分配、错误处理、平台适配、第三方适配和测试实现的约束；若本文档与该规范冲突，以仓库规范和实际公共 API 约定为准。

本模块逐项遵守该规范的以下部分：命名规范、类的创建、虚函数表与虚函数重载、对象生命周期管理、API 设计规范、公共头文件与 API 注释格式、内存管理注意事项、错误处理与断言、文件组织规范、平台适配规范、第三方库集成规范和常见错误与最佳实践。实现阶段必须逐项对照其中的快速检查清单，不能只满足编译。

XProcess 和 XConsoleShell 的虚函数公共调度入口统一使用 `_base` 后缀；复用
XClass/XIODevice 父类入口时使用宏别名，槽实现只保留 `VX<Class>_*` 静态函数。
普通启动、环境、文件命令和状态 API 不进入虚表。

## 2. 设计目标

### 2.1 必须支持

1. 通过 UART、USB CDC、RTT、TCP、Telnet 或应用自定义传输接收命令。
2. 逐字节输入和整行输入两种模式。
3. 命令注册、命令层级、别名、帮助文本、参数数量检查和错误码。
4. help、version、echo、pwd、cd、ls、cat、hexdump、stat、rm、mkdir、rmdir、cp、mv 等文件命令。
5. date 日期时间查看命令，以及 net ifconfig、net hostname、net resolve 和 net ping 网络命令；统一调用
   XNetwork 公共抽象，不在 Shell 中直接访问平台套接字、DNS 或网卡接口。
6. 设备控制命令通过回调注册，不依赖操作系统进程。
7. 设备控制命令通过已注册的库级回调执行；不在本模块内执行任意外部进程。
8. 每个功能独立编译开关，且由一个总开关统一控制。
9. 默认使用固定缓冲区和静态命令表，关闭功能后不产生对应符号和依赖。

### 2.2 默认不支持

- 不调用任何平台进程、线程、文件、串口、网络或终端 API。
- 不调用 system()、popen()、posix_spawn()、execve() 或任意字符串 shell。
- 不默认启用管道、重定向、脚本、历史、补全、颜色和 Telnet。
- 不把 XCommandLineParser 当作交互式 Shell 解析器；它继续服务一次性 argc/argv 参数解析。
- 不把进程级当前目录作为多会话 Shell 的目录状态；每个会话使用自己的逻辑当前目录。
- 不在核心层依赖 XVector、XHashMap、XStringList 维护命令注册表。

## 3. 外部项目参考

### 3.1 Zephyr Shell

借鉴静态命令注册、层级子命令、参数约束、help、补全、历史、编辑键处理、多后端、日志和会话状态隔离。

### 3.2 FreeRTOS-Plus-CLI

借鉴命令名、帮助文本、处理函数、参数数量组成的最小命令描述；借鉴分段输出，使 ls、help 和大文件输出不需要大临时缓冲。

### 3.3 取舍

Zephyr Shell 和 FreeRTOS-Plus-CLI 只作为行为参考，不作为本项目的运行时依赖；不复制它们的 RTOS、设备树、Kconfig、原生串口或原生网络调用。

| 能力 | XinYueC 的结论 |
|---|---|
| 命令描述 | 采用 FreeRTOS-CLI 的轻量结构，增加子命令、别名、权限和取消标志 |
| 层级命令 | 采用 Zephyr Shell 的树形模型 |
| 行编辑 | 核心预留接口，默认关闭 |
| 历史/补全 | 独立开关，默认关闭 |
| 传输层 | 通过 XinYueC 的 XIODevice、XSerialPort、XTcpSocket、XTcpServer 接入；回调只作为扩展 |
| 输出 | 支持同步分块输出；异步输出单独开关 |
| 文件命令 | 基于 XFileSystem，不调用平台文件 API |
| 进程执行 | 当前不提供任意外部进程执行；仅执行已注册的 XinYueC 命令回调 |
| 命令注册 | 默认静态表；动态注册为可选能力 |

两者都不是完整 Unix Shell。文件命令、设备命令、进程安全策略和产品权限仍由 XConsoleShell 与产品适配层实现。

## 4. 文件规划

~~~
Src/XCode/XConsoleShell/
  XConsoleShellConfig.h          # 总开关、功能开关、容量和默认值
  XConsoleShell.h                 # Shell 对象、生命周期、输入输出 API
  XConsoleShellCommand.h          # 命令描述、注册、查找、权限
  XConsoleShellIo.h               # 输入、输出、刷新、取消回调
  XConsoleShell.c
  XConsoleShellParser.c
  XConsoleShellFileSystem.c
  XConsoleShellExecutor.c         # 注册命令执行和可选 XProcess 适配
  XConsoleShell_Telnet.h          # Telnet 协议过滤声明
  XConsoleShell_Telnet.c          # Telnet 协商和数据过滤
  XConsoleShell_Protected.h       # 仅内部实现和子模块使用
  XConsoleShell_设计文档.md

  XConsoleShell_XIODevice.h        # 基于 XIODevice 的通用传输适配声明
  XConsoleShell_XIODevice.c        # 基于 XIODevice 的通用传输适配
  XConsoleShell_XSerialPort.h     # 基于 XSerialPort 的串口适配声明
  XConsoleShell_XSerialPort.c     # 基于 XSerialPort 的串口适配
  XConsoleShell_XTcpSocket.h      # 基于 XTcpSocket 的 TCP 会话适配声明
  XConsoleShell_XTcpSocket.c      # 基于 XTcpSocket 的 TCP 会话适配
  XConsoleShell_XTcpServer.h      # 基于 XTcpServer 的多会话适配声明
  XConsoleShell_XTcpServer.c      # 基于 XTcpServer 的多会话适配
  XConsoleShell_XSsh.h            # 基于 SSH 2.0 的服务端适配声明
  XConsoleShell_XSsh.c            # SSH 握手、认证、通道和会话适配

Test/XCodeTest/
  XConsoleShellTest.c
  XConsoleShellConfigTest.c
  XTestCommand.c                # 默认入口的 Test 命令
  XTestCommand.h
~~~

本模块不新增直接面向平台的 Drive 文件；所需平台差异由已有 XinYueC 后端在库内部处理。所有新增 .c 和 .h 使用 UTF-8 BOM。每个公共头文件和内部保护头文件都必须有详细文件头注释，至少包含 `@file`、`@brief`、`@details`、依赖、线程安全、生命周期、编码和平台约束。公共类型、枚举值、结构体成员、宏、函数参数和返回值都必须有详细中文 Doxygen。

## 5. 配置设计

### 5.1 总开关

新模块使用全大写宏：

~~~
#ifndef XCONSOLE_SHELL_ON
#define XCONSOLE_SHELL_ON 0
#endif
~~~

XConsoleShellConfig.h 由 CXinYueConfig.h 统一包含，也允许应用直接包含。总开关默认开启；
产品固件可在编译配置中覆盖为 0，以裁剪默认主入口和库实现。
总开关为 0 时，所有功能开关强制为 0，公共头文件不暴露可调用 API，源文件不编译功能实现。

### 5.2 默认主入口

main.c 在无命令行参数时创建 XConsoleShell，通过标准输入输出回调驱动
XConsoleShell_pump()。Shell 注册 Test 根命令；直接输入 Test 进入原有 XMenuTest
测试菜单，也支持 Test list、Test xprocess、Test xconsole-shell、Test xconsole-shell-backend
和 Test all。历史 XMenuTest 代码不再由 main 直接启动，而是由 Test 命令调度；脚本兼容入口
--test <name> 保留。

### 5.3 功能开关

每个宏都在 XConsoleShellConfig.h 中定义默认值，并允许编译命令或产品配置先行定义覆盖。

~~~
/* 核心 */
XCONSOLE_SHELL_COMMAND_ON
XCONSOLE_SHELL_PARSER_ON
XCONSOLE_SHELL_HELP_ON
XCONSOLE_SHELL_ALIAS_ON
XCONSOLE_SHELL_SUBCOMMAND_ON
XCONSOLE_SHELL_CALLBACK_COMMAND_ON

/* 交互 */
XCONSOLE_SHELL_LINE_EDITOR_ON
XCONSOLE_SHELL_HISTORY_ON
XCONSOLE_SHELL_COMPLETION_ON
XCONSOLE_SHELL_COLOR_ON
XCONSOLE_SHELL_CANCEL_ON

/* 输入输出与后端 */
XCONSOLE_SHELL_IO_ON
XCONSOLE_SHELL_ASYNC_OUTPUT_ON
XCONSOLE_SHELL_ASYNC_ON
XCONSOLE_SHELL_ASYNC_RUN_MODE
XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER
XCONSOLE_SHELL_ASYNC_MODE_THREAD
XCONSOLE_SHELL_LOG_ON
XCONSOLE_SHELL_MULTI_SESSION_ON
XCONSOLE_SHELL_XIODEVICE_BACKEND_ON
XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON
XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON
XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
XCONSOLE_SHELL_TELNET_PROTOCOL_ON
XCONSOLE_SHELL_TRANSPORT_CALLBACK_ON

/* 日期时间命令 */
XCONSOLE_SHELL_DATETIME_ON
XCONSOLE_SHELL_DATE_ON

/* 内存池查询命令 */
XCONSOLE_SHELL_MEMORY_ON
XCONSOLE_SHELL_MEMORY_POOL_ON
XCONSOLE_SHELL_INFO_ON
XCONSOLE_SHELL_UPTIME_ON
XCONSOLE_SHELL_TASKS_ON
XCONSOLE_SHELL_CLEAR_ON
XCONSOLE_SHELL_RESET_ON
XCONSOLE_SHELL_REBOOT_ON
XCONSOLE_SHELL_SHUTDOWN_ON
XCONSOLE_SHELL_GPIO_ON
XCONSOLE_SHELL_GPIO_LIST_ON
XCONSOLE_SHELL_GPIO_OPEN_ON
XCONSOLE_SHELL_GPIO_CLOSE_ON
XCONSOLE_SHELL_GPIO_INFO_ON
XCONSOLE_SHELL_GPIO_READ_ON
XCONSOLE_SHELL_GPIO_WRITE_ON
XCONSOLE_SHELL_GPIO_TOGGLE_ON
XCONSOLE_SHELL_GPIO_CONFIGURE_ON
XCONSOLE_SHELL_GPIO_INTERRUPT_ON
XCONSOLE_SHELL_ADC_ON
XCONSOLE_SHELL_ADC_LIST_ON
XCONSOLE_SHELL_ADC_OPEN_ON
XCONSOLE_SHELL_ADC_CLOSE_ON
XCONSOLE_SHELL_ADC_INFO_ON
XCONSOLE_SHELL_ADC_READ_ON
XCONSOLE_SHELL_ADC_CONFIGURE_ON
XCONSOLE_SHELL_PWM_ON
XCONSOLE_SHELL_PWM_LIST_ON
XCONSOLE_SHELL_PWM_OPEN_ON
XCONSOLE_SHELL_PWM_CLOSE_ON
XCONSOLE_SHELL_PWM_INFO_ON
XCONSOLE_SHELL_PWM_CONFIGURE_ON
XCONSOLE_SHELL_PWM_START_ON
XCONSOLE_SHELL_PWM_STOP_ON
XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON
XCONSOLE_SHELL_PWM_SET_DUTY_ON
XCONSOLE_SHELL_I2C_ON
XCONSOLE_SHELL_I2C_LIST_ON
XCONSOLE_SHELL_I2C_OPEN_ON
XCONSOLE_SHELL_I2C_CLOSE_ON
XCONSOLE_SHELL_I2C_INFO_ON
XCONSOLE_SHELL_I2C_READ_ON
XCONSOLE_SHELL_I2C_WRITE_ON
XCONSOLE_SHELL_I2C_WRITEREAD_ON
XCONSOLE_SHELL_SPI_ON
XCONSOLE_SHELL_SPI_LIST_ON
XCONSOLE_SHELL_SPI_OPEN_ON
XCONSOLE_SHELL_SPI_CLOSE_ON
XCONSOLE_SHELL_SPI_INFO_ON
XCONSOLE_SHELL_SPI_TRANSFER_ON
XCONSOLE_SHELL_CAN_ON
XCONSOLE_SHELL_CAN_LIST_ON
XCONSOLE_SHELL_CAN_OPEN_ON
XCONSOLE_SHELL_CAN_CLOSE_ON
XCONSOLE_SHELL_CAN_INFO_ON
XCONSOLE_SHELL_CAN_STATUS_ON
XCONSOLE_SHELL_CAN_START_ON
XCONSOLE_SHELL_CAN_STOP_ON
XCONSOLE_SHELL_CAN_SEND_ON
XCONSOLE_SHELL_CAN_RECEIVE_ON
XCONSOLE_SHELL_CAN_RECOVER_ON
XCONSOLE_SHELL_CAN_FILTER_ON

/* 网络命令 */
XCONSOLE_SHELL_NETWORK_ON
XCONSOLE_SHELL_NET_IFCONFIG_ON
XCONSOLE_SHELL_NET_HOSTNAME_ON
XCONSOLE_SHELL_NET_RESOLVE_ON
XCONSOLE_SHELL_NET_PING_ON

/* 文件系统 */
XCONSOLE_SHELL_FILESYSTEM_ON
XCONSOLE_SHELL_FS_PWD_ON
XCONSOLE_SHELL_FS_CD_ON
XCONSOLE_SHELL_FS_LS_ON
XCONSOLE_SHELL_FS_CAT_ON
XCONSOLE_SHELL_FS_HEXDUMP_ON
XCONSOLE_SHELL_FS_STAT_ON
XCONSOLE_SHELL_FS_RM_ON
XCONSOLE_SHELL_FS_MKDIR_ON
XCONSOLE_SHELL_FS_RMDIR_ON
XCONSOLE_SHELL_FS_CP_ON
XCONSOLE_SHELL_FS_MV_ON
XCONSOLE_SHELL_FS_WRITE_ON
XCONSOLE_SHELL_FS_LINK_ON
XCONSOLE_SHELL_FS_LN_ON       /* 仅实现 ln -s；无硬链接公共 API */
XCONSOLE_SHELL_FS_UNLINK_ON
XCONSOLE_SHELL_FS_FORMAT_ON
XCONSOLE_SHELL_FS_TOUCH_ON
XCONSOLE_SHELL_FS_CHMOD_ON
XCONSOLE_SHELL_FS_READLINK_ON
XCONSOLE_SHELL_FS_REALPATH_ON
XCONSOLE_SHELL_FS_TRUNCATE_ON
XCONSOLE_SHELL_FS_DF_ON
XCONSOLE_SHELL_FS_DU_ON
XCONSOLE_SHELL_FS_WC_ON
XCONSOLE_SHELL_FS_HEAD_ON
XCONSOLE_SHELL_FS_TAIL_ON
XCONSOLE_SHELL_FS_FIND_ON
XCONSOLE_SHELL_FS_TREE_ON
XCONSOLE_SHELL_FS_CMP_ON
XCONSOLE_SHELL_FS_FILE_ON
XCONSOLE_SHELL_FS_BASENAME_ON
XCONSOLE_SHELL_FS_DIRNAME_ON

/* 执行与扩展 */
XCONSOLE_SHELL_EXECUTOR_ON
XCONSOLE_SHELL_EXTERNAL_PROCESS_ON   /* 依赖 XProcess_ON，默认关闭 */
XCONSOLE_SHELL_PROCESS_ASYNC_ON      /* 同上，默认强制为 0 */
XCONSOLE_SHELL_PIPE_ON
XCONSOLE_SHELL_REDIRECT_ON
XCONSOLE_SHELL_SCRIPT_ON
XCONSOLE_SHELL_DYNAMIC_REGISTER_ON

/* 安全与诊断 */
XCONSOLE_SHELL_AUTH_ON
XCONSOLE_SHELL_PERMISSION_ON
XCONSOLE_SHELL_AUDIT_ON
XCONSOLE_SHELL_STATS_ON
~~~

### 5.3 默认值

| 功能 | 默认值 | 原因 |
|---|---:|---|
| XCONSOLE_SHELL_ON | 0 | 可选模块，不能改变现有固件体积 |
| COMMAND/PARSER/IO | 1 | 最小可运行核心 |
| HELP/SUBCOMMAND/CALLBACK_COMMAND | 1 | 控制台基本能力 |
| FILESYSTEM 和只读命令 | 1 | 当前需求；最终受总开关控制 |
| NETWORK 和网络查询命令 | 1 | 通过 XNetwork 公共抽象；最终受总开关控制 |
| CLEAR | 1 | 只输出 ANSI 清屏序列，不改变设备状态 |
| RESET/REBOOT | 0 | 系统控制命令，必须由产品显式开启并注册 XSystem 后端 |
| GPIO | 0 | XGpio 平台抽象默认关闭；写入、配置和中断还要分别显式打开 |
| ADC/PWM/I2C/SPI | 0 | 平台契约和 Shell 命令默认关闭；危险事务还要由子开关和授权回调控制 |
| CAN | 0 | XCan 平台抽象默认关闭；发送、恢复和过滤器还要分别显式打开 |
| 文件写入、删除、格式化 | 0 | 危险操作必须由产品显式开启 |
| LINE_EDITOR/HISTORY/COMPLETION | 0 | 默认节省 RAM/Flash |
| EXTERNAL_PROCESS/PIPE/REDIRECT/PROCESS_ASYNC | 0 | 依赖 XProcess 公共 API，默认关闭并需要产品白名单 |
| SCRIPT | 0 | 依赖 FILESYSTEM 和 XFileSystem，默认关闭；不依赖 XProcess |
| AUTH/PERMISSION/AUDIT | 0 | 产品必须提供凭据和策略 |
| XIODevice/XSerialPort/XTcpSocket/XTcpServer 后端 | 0 | 由产品显式选择库级对象 |

文件系统默认值只在 XCONSOLE_SHELL_ON 为 1 时有意义。产品可以仅启用核心，不包含任何文件系统头文件或后端。

### 5.4 容量参数

容量宏同样允许覆盖：

~~~
XCONSOLE_SHELL_LINE_BUFFER_SIZE       256
XCONSOLE_SHELL_MAX_ARGUMENTS          16
XCONSOLE_SHELL_MAX_PATH               256
XCONSOLE_SHELL_COMMAND_CAPACITY       64
XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE      128
XCONSOLE_SHELL_HISTORY_CAPACITY       8
XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY  512
XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY 8
XCONSOLE_SHELL_MAX_SESSIONS           4
XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY 4
~~~

配置头对 0 值、最大参数数、路径长度、历史长度和多会话容量做预处理检查。普通固定容量
运行时到达上限时返回 XConsoleResult_ResourceLimit，不截断后继续执行；异步输出队列按下节
约定先排空再继续。

启用 `XCONSOLE_SHELL_ASYNC_OUTPUT_ON` 后，`XConsoleShell_write` 先把数据写入固定环形队列，
由事件循环或传输任务调用 `XConsoleShell_flushOutput` 排空。队列满载时，写入函数会先排空
已有数据再继续入队；因此大段 `ls`、`help` 或文件输出不会因为单次输出超过队列容量而被
截断。刷新失败仍返回 I/O 错误，调用方必须在下一轮重试或停止会话。该模式仍要求所有
调用在同一执行上下文串行进行，不创建后台线程。

### 5.3 Shell 输入异步调度

Shell 输入异步化必须建立在 XinYueC 的事件调度器之上，不能在 Shell 内部直接创建
`select`、`poll`、`epoll`、Win32 等平台等待。同步 `XConsoleShell_pump()` 保留为底层
处理函数，但异步模式只在“输入就绪事件”到达后调用它，并要求 `XConsoleReadFn` 在
该模式下为非阻塞读：没有数据时必须立即返回 0，不能等待下一字节。

配置宏如下，所有宏均在 `XConsoleShellConfig.h` 中定义并允许产品覆盖：

```c
#define XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER 0
#define XCONSOLE_SHELL_ASYNC_MODE_THREAD           1

#ifndef XCONSOLE_SHELL_ASYNC_ON
#define XCONSOLE_SHELL_ASYNC_ON 1
#endif
#ifndef XCONSOLE_SHELL_ASYNC_AUTO_START_ON
#define XCONSOLE_SHELL_ASYNC_AUTO_START_ON 1
#endif
#ifndef XCONSOLE_SHELL_ASYNC_RUN_MODE
#define XCONSOLE_SHELL_ASYNC_RUN_MODE XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER
#endif
#ifndef XCONSOLE_SHELL_ASYNC_READ_BUDGET
#define XCONSOLE_SHELL_ASYNC_READ_BUDGET 32
#endif
#ifndef XCONSOLE_SHELL_ASYNC_POLL_INTERVAL_MS
#define XCONSOLE_SHELL_ASYNC_POLL_INTERVAL_MS 10
#endif
```

`XCONSOLE_SHELL_ASYNC_ON` 为总开关，默认开启；开启后只能选择一个运行模式。
`XCONSOLE_SHELL_ASYNC_AUTO_START_ON` 默认开启，且只有 IO 提供 `inputAttach` 时才会在
Shell 创建完成后自动启动；没有输入源附加回调的测试或嵌入式代码仍可显式调用
`startAsync()`。默认事件模式会通过 `XCONSOLE_SHELL_ASYNC_POLL_INTERVAL_MS` 定时调用
非阻塞 `read`，因此主函数只需要进入 `XCoreApplication_exec()`。

| `XCONSOLE_SHELL_ASYNC_RUN_MODE` | 执行上下文 | 事件归属 | 适用场景 |
|---|---|---|---|
| `EVENT_DISPATCHER` | 当前 Shell 所属线程 | 当前线程的 `XAbstractEventDispatcher` | 已有主循环、GUI、网络服务或串口事件循环 |
| `THREAD` | Shell 独占的 `XThread` | 专用线程自己的 `XEventLoop` 和事件调度器 | 没有可复用主循环、需要隔离传输阻塞的产品 |

配置头必须拒绝未知模式；`ASYNC_ON` 关闭时裁剪异步字段、线程和事件相关符号。
两种模式都使用同一组公共 API：

```c
bool XConsoleShell_startAsync(XConsoleShell* self);
bool XConsoleShell_stopAsync(XConsoleShell* self, uint32_t timeoutMs);
bool XConsoleShell_isAsyncRunning(const XConsoleShell* self);
bool XConsoleShell_notifyInput(XConsoleShell* self);
```

`startAsync()` 建立调度关系并调用可选的 `inputAttach`；`notifyInput()` 是传输层在确认
输入就绪后调用的入口。Shell 将通知合并为一个用户事件并投递到自己的事件队列，事件
到达后在所属线程内清除合并标志。事件模式的轮询和传输通知都只读取当前已经可读的
字节，绝不会等待输入。`feedByte()` 收到 `\n`（同时忽略 `\r`）时立即提交当前完整行，
不会等待下一次事件。重复通知不会为每个字节分配事件，从而避免高频串口中断造成堆
分配和事件风暴。

事件调度器模式要求 Shell 已经属于当前线程，并且产品主循环最终进入
`XCoreApplication_exec()` 或等价的 `XEventLoop_exec()`。POSIX/Windows 标准输入可使用
`XFileSystem_openStandardInput()` 和 `XFileSystem_readStandardInput()`，默认入口已经
采用该非阻塞适配；普通文件描述符传输可以由产品用 `XSocketNotifier` 监听可读事件，
再调用 `notifyInput()`。UART、RTT 和无文件描述符设备由驱动任务在可安全调用库 API 的
上下文中调用同一函数。中断服务程序不得直接执行 `notifyInput()`，应先投递到驱动任务
或平台事件队列。`inputDetach` 在停止和析构时调用，负责关闭或解除底层输入源；`prompt`
可用于完整命令后的提示符输出。

线程模式在 `startAsync()` 中创建 `XThread`，把 Shell 迁移到该线程；线程入口创建并
运行自己的 `XEventLoop`，随后 `notifyInput()` 自动把事件投递到该线程的调度器。
`stopAsync()` 先请求事件循环退出，再等待线程结束并把 Shell 迁回创建线程。停止和
析构必须幂等；析构前若异步仍运行，必须先停止并等待，不能释放仍可能收到事件的
Shell。异步运行期间禁止从其他线程直接调用 `processLine()`、`feedData()`、注册或
注销命令；跨线程输入只能使用 `notifyInput()`，跨线程停止只能使用 `stopAsync()`。

两种模式都不改变命令回调签名，不把 `argv`、会话指针或 `userData` 转移到后台线程。
传输 `read/write/flush/cancelled` 回调及其 `userData` 必须在所选执行线程有效；线程模式
下输出回调也必须是线程安全的，或由产品在回调内部转发到设备任务。异步 Shell 仍然
是单执行者模型，同一时刻只有一个线程进入解析器和命令回调。

### 5.5 依赖裁剪

~~~
总开关关闭
  -> 全部功能关闭

PARSER 关闭
  -> COMMAND 关闭，因而不编译任何 Shell 公共入口和命令实现

COMMAND 关闭
  -> HELP、SUBCOMMAND、ALIAS、COMPLETION、CALLBACK_COMMAND、FILESYSTEM 关闭

LINE_EDITOR 关闭
  -> ANSI 键序列编辑关闭；HISTORY 若显式打开仍可记录并通过 history 命令查询

FILESYSTEM 关闭
  -> 文件命令和 SCRIPT 不可用；SCRIPT_ON 会触发配置错误

EXTERNAL_PROCESS 关闭
  -> PROCESS_ASYNC、PIPE、REDIRECT 是无效组合并触发配置错误；SCRIPT 不受影响

MULTI_SESSION 关闭
  -> 只编译默认会话；MAX_SESSIONS 容量不参与对象布局
~~~

若产品显式打开无效组合，配置头使用 error 报出原因；不得静默编出接口与实现不一致的库。

### 5.6 已实现可选能力的依赖矩阵

下表以 `XConsoleShellConfig.h` 的实际裁剪规则为准。宏为 1 只表示编译对应实现，
不表示适配层会替产品创建、打开或管理底层对象。

| 能力 | 必须同时打开 | 编译结果与运行约束 |
|---|---|---|
| LOG | `XCONSOLE_SHELL_ON`、`COMMAND_ON`、`IO_ON` | `XConsoleShellIo.log` 可选；每次逻辑输出被接受后收到一次观察回调。日志回调不参与写入成功判定，必须自行脱敏。|
| DYNAMIC_REGISTER | `COMMAND_ON`、`PARSER_ON` | 增加 `registerCommand/unregisterCommand`；动态描述字符串和子命令树由 Shell 深复制，handler 与 userData 借用。|
| MULTI_SESSION | `COMMAND_ON`、`PARSER_ON`、`IO_ON`，且 `MAX_SESSIONS >= 2` | 默认会话加固定附加会话槽；`XConsoleShell_XTcpServer.h` 还要求 TCP Server 后端。API 调用仍须由一个执行上下文串行化。|
| XIODevice | `IO_ON`、`XIODEVICE_BACKEND_ON` | 只调用 `XIODevice_read_1/write_1/flush`；设备指针和生命周期由产品拥有。|
| XSerialPort | `XIODEVICE_BACKEND_ON`、`XSERIALPORT_BACKEND_ON` | 自动打开 XIODevice 后端；不负责串口配置、打开、关闭或事件循环。|
| XTcpSocket | `XIODEVICE_BACKEND_ON`、`XTCPSOCKET_BACKEND_ON` | 自动打开 XIODevice 后端；Socket 必须由产品连接和关闭。|
| XTcpServer | `MULTI_SESSION_ON`、`XTCPSERVER_BACKEND_ON` | 自动打开 XTcpSocket 后端；固定数组绑定待处理连接，超过容量不再接收。|
| SSH Server | `MULTI_SESSION_ON`、`XTCPSERVER_BACKEND_ON`、`LOGIN_ON`、`AUTH_ON` | 在 `XTcpServer` 上承载 SSH 2.0 服务端；当前支持版本交换、`curve25519-sha256` / ECDH P-256 密钥交换、AES-256/192/128-CTR、HMAC-SHA-256、密码认证、pty、env、shell、窗口调整和通道流控；`exec/subsystem` 请求明确拒绝。主机密钥首次启动生成并持久化到 `XCONSOLE_SHELL_XSSH_HOSTKEY_FILE`，跨进程重启保持不变；认证失败达到 `XSSH_MAX_AUTH_ATTEMPTS` 后关闭会话。|
| SSH Client | `XSSH_CLIENT_ON`、`XSSH_KEX_*`、`XSSH_CIPHER_*`、`XSSH_MAC_*` | 绑定调用方提供的 `XIODevice`，完成 SSH 2.0 版本协商、共享算法套件选择、主机密钥确认、password 认证、pty/shell 通道和双向数据；未知主机密钥默认拒绝，不创建线程或网络对象。|
| Telnet | `IO_ON`、`TELNET_PROTOCOL_ON` | 在 Shell I/O 前过滤 Telnet IAC、协商、子协商和 CR-NUL；不创建网络对象。|
| Telnet Client | `XTELNET_CLIENT_ON` | 绑定调用方提供的 `XIODevice`，完成 NVT/IAC 协商、ECHO/SGA、CRLF 规范化和 IAC 转义；不创建线程或网络对象。|
| PROCESS_ASYNC | `EXTERNAL_PROCESS_ON`、`XProcess_ON` | 增加固定异步任务槽和 `pollProcesses`；每个任务由 Shell 持有，必须由事件循环或任务显式轮询。|

`XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON` 未打开 `XCONSOLE_SHELL_MULTI_SESSION_ON` 时，配置头
直接报错；`XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON` 会自动打开 Socket 和 XIODevice 后端。
`XCONSOLE_SHELL_PARSER_ON` 或总开关关闭时，动态注册、多会话和后端公共声明都会被裁剪。
异步进程容量由 `XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY` 控制，动态命令容量由
`XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY` 控制，均在编译期检查必须大于零。

### 5.7 平台 API 禁止清单

本模块源文件不得包含或直接调用下列平台接口：

- POSIX/ISO C 文件接口：open、read、write、close、stat、opendir、readdir、remove、rename、mkdir、chdir、system、popen、execve、posix_spawn。
- POSIX/Win32 线程、锁、事件、定时器和套接字接口。
- FreeRTOS 原生任务、队列、信号量和内存接口。
- Zephyr 原生 shell、UART、设备树和 Kconfig 运行时接口。
- Win32 文件、串口、线程、进程、控制台和 Winsock 接口。

允许使用的调用边界只有 XinYueC 公共 API：

- 文件：XFile、XDir、XFileInfo、XFileSystem_*。
- 输入输出：XIODevice、XIODevice_*、XSerialPort_*。
- 网络：XTcpSocket_*、XTcpServer_*、XAbstractSocket_*。
- 事件和定时：XEventLoop、XEvent、XTimer、XSignal。
- 输出和错误：XPrintf、XERROR_PRINTF、XDEBUG_PRINTF。
- 内存：XMalloc_*、XCalloc_*、XRealloc_*、XFree_*、XMemory_strdup。
- 同步：XMutex、XReadWriteLock、XWaitCondition 等已有 XinYueC 抽象。

测试中的 fake transport 可以使用 XConsoleShellIo 回调，但回调实现也必须通过 XinYueC API；不得为了测试绕过本约束调用原生平台函数。

## 6. 分层架构

~~~
传输后端
  XIODevice / XSerialPort / XTcpSocket / XTcpServer / 用户回调
                    |
                    v
XConsoleShellIo      # 读写、刷新、取消、背压
                    |
                    v
XConsoleShell        # 会话、行状态、提示符、生命周期
                    |
                    v
XConsoleShellParser  # 分词、命令树、参数、权限、错误
                    |
        +-----------+------------+
        v                        v
XConsoleShellCommand       内建/用户命令
                               |
                 +-------------+-------------+
                 v                           v
          XConsoleShellFileSystem     XConsoleShellExecutor
                 |                           |
          XFileSystem API             XinYueC 库级对象 API
~~~

核心层和对象适配层不得直接包含 UART、POSIX、FreeRTOS、Zephyr 或 Win32 头文件。适配层只包含 XinYueC 公共头文件，遵循仓库的通用对象、公共抽象、既有库后端三层结构；底层平台差异只能留在已有 XinYueC 后端内部。

## 7. 对象和生命周期

### 7.1 XConsoleShell

XConsoleShell 是有状态服务对象，继承 XObject，结构体第一个成员必须是 XObject base。对象保存固定根命令表、默认会话、可选附加会话、输入工作区和 I/O 回调。

~~~
typedef struct XConsoleShell {
    XObject base;                     /**< 第一个成员，由 XObject 管理，禁止手工修改。 */
    XConsoleShellIo m_io;             /**< 默认会话借用的 I/O 回调。 */
    const XConsoleCommand* m_commands[XCONSOLE_SHELL_COMMAND_CAPACITY];
                                      /**< 固定根命令指针表。 */
    size_t m_commandCount;            /**< 当前根命令数量。 */
    XConsoleShellSession m_session;   /**< 始终存在的默认会话。 */
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    XConsoleShellSession m_sessions[XCONSOLE_SHELL_MAX_SESSIONS - 1u];
                                      /**< 固定附加会话槽，由 Shell 内嵌拥有。 */
#endif
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
    XConsoleShellDynamicCommand*
        m_dynamicCommands[XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY];
                                      /**< Shell 拥有的动态命令树根节点。 */
#endif
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
    XConsoleShellAsyncProcess*
        m_asyncProcesses[XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY];
                                      /**< Shell 拥有的异步进程任务。 */
#endif
    char m_lineBuffer[XCONSOLE_SHELL_LINE_BUFFER_SIZE];
                                      /**< tokenizer 与默认会话工作缓冲。 */
    const char* m_arguments[XCONSOLE_SHELL_MAX_ARGUMENTS];
                                      /**< 当前调用期间借用的 token 指针。 */
    bool m_running;                   /**< 是否接受输入。 */
} XConsoleShell;
~~~

init 使用对象内嵌的固定数组；create 仅在应用允许堆分配时使用，必须调用
Set_Class_MemoryFree。静态命令表、I/O 回调、会话 userData 与后端对象均为借用，Shell 只释放
动态命令树和异步进程任务。所有释放使用 XFree_System 或匹配的 XinYueC 分配器，禁止直接使用
malloc/free。

### 7.2 虚函数规则

- XConsoleShell_class_init() 必须先继承 XObject 的表，再重载析构函数。
- 传输后端使用回调，不强制继承，避免每个后端增加 vtable 开销。
- 如果未来增加虚函数，枚举值从 XCLASS_VTABLE_GET_SIZE(XObject) 开始。
- 所有 VXConsoleShell_* 静态实现必须匹配槽位签名。
- deinit 释放历史、动态命令、进程句柄和内部资源，并把拥有指针置 NULL。
- Shell 含有 I/O 和用户借用指针，首版不公开 copy/move。若以后公开，必须实现完整 copy_base/move_base，禁止 memcpy。

### 7.3 栈对象和堆对象

~~~
XConsoleShell shell;
XConsoleShell_init(&shell, &io);
XConsoleShell_feedData(&shell, input, length);
XConsoleShell_deinit_base(&shell);
~~~

所有 init 与 deinit 必须成对；create 失败的每条返回路径必须释放已经获得的资源。

## 8. 公共 API 草案

### 8.1 结果码

~~~
typedef enum XConsoleResult {
    XConsoleResult_Ok = 0,
    XConsoleResult_MoreOutput = 1,
    XConsoleResult_InvalidArgument = -1,
    XConsoleResult_UnknownCommand = -2,
    XConsoleResult_InvalidSyntax = -3,
    XConsoleResult_PermissionDenied = -4,
    XConsoleResult_NotSupported = -5,
    XConsoleResult_ResourceLimit = -6,
    XConsoleResult_Cancelled = -7,
    XConsoleResult_IoError = -8,
    XConsoleResult_Failed = -9
} XConsoleResult;
~~~

枚举值不可按位组合。公共函数使用结果码说明可恢复错误；错误文本经 XConsoleShell_writeError 输出。

### 8.2 I/O 回调

~~~
typedef int64_t (*XConsoleReadFn)(void* userData, void* data, size_t size);
typedef int64_t (*XConsoleWriteFn)(void* userData, const void* data, size_t size);
typedef bool (*XConsoleFlushFn)(void* userData);
typedef bool (*XConsoleCancelFn)(void* userData);
typedef bool (*XConsoleInputAttachFn)(void* userData, XConsoleShell* shell);
typedef void (*XConsoleInputDetachFn)(void* userData, XConsoleShell* shell);
typedef void (*XConsolePromptFn)(void* userData, XConsoleShell* shell);
#if XCONSOLE_SHELL_LOG_ON
typedef void (*XConsoleLogFn)(void* userData,
                              const XConsoleShellSession* session,
                              const void* data, size_t size);
#endif
#if XCONSOLE_SHELL_AUDIT_ON
typedef void (*XConsoleAuditFn)(void* userData,
                                const XConsoleShellSession* session,
                                const XConsoleCommand* command, int result);
#endif

typedef struct XConsoleShellIo {
    XConsoleReadFn read;
    XConsoleWriteFn write;
    XConsoleFlushFn flush;
    XConsoleCancelFn cancelled;
    XConsoleInputAttachFn inputAttach;
    XConsoleInputDetachFn inputDetach;
    XConsolePromptFn prompt;
#if XCONSOLE_SHELL_LOG_ON
    XConsoleLogFn log;
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    XConsoleAuditFn audit;
#endif
    void* userData;
} XConsoleShellIo;
~~~

write 允许短写，核心负责保留偏移和继续发送。userData 由调用方拥有，Shell 不释放它。

首版公共入口：

~~~
XVtable* XConsoleShell_class_init(void);
void XConsoleShell_init(XConsoleShell* self,
                        const XConsoleShellIo* io);
XConsoleShell* XConsoleShell_create(const XConsoleShellIo* io);
void XConsoleShell_deinit_base(XConsoleShell* self);
void XConsoleShell_delete_base(XConsoleShell* self);
XConsoleResult XConsoleShell_feedByte(XConsoleShell* self, uint8_t byte);
XConsoleResult XConsoleShell_feedData(XConsoleShell* self,
                                      const void* data, size_t size);
XConsoleResult XConsoleShell_processLine(XConsoleShell* self,
                                         const char* line, size_t length);
bool XConsoleShell_write(XConsoleShell* self, const void* data, size_t size);
bool XConsoleShell_writeUtf8(XConsoleShell* self, const char* text);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
bool XConsoleShell_flushOutput(XConsoleShell* self);
#endif
bool XConsoleShell_writeError(XConsoleShell* self, XConsoleResult result,
                              const char* detail);
~~~

所有公开函数需要完整中文 Doxygen。参数必须说明借用/转移/输出存储；返回值必须说明失败状态与释放方式。

### 8.3 命令描述和注册

~~~
typedef int (*XConsoleCommandHandler)(XConsoleShell* shell,
                                      XConsoleShellSession* session,
                                      int argc,
                                      const char* const* argv,
                                      void* userData);

typedef struct XConsoleCommand {
    const char* name;
    const char* aliases;
    const char* description;
    const char* usage;
    int minArgs;
    int maxArgs;
    uint32_t flags;
    XConsoleCommandHandler handler;
    const struct XConsoleCommand* subcommands;
    size_t subcommandCount;
    void* userData;
} XConsoleCommand;
~~~

name、aliases、description、usage 和 subcommands 默认是静态 UTF-8 借用数据。处理函数不得保存 argv 指针；需要异步使用时必须复制。

~~~
bool XConsoleShell_registerStaticCommands(XConsoleShell* self,
                                          const XConsoleCommand* commands,
                                          size_t count);
bool XConsoleShell_unregisterStaticCommands(XConsoleShell* self,
                                            const XConsoleCommand* commands,
                                            size_t count);
bool XConsoleShell_findCommand(const XConsoleShell* self,
                               const char* path,
                               const XConsoleCommand** result);
~~~

XCONSOLE_SHELL_DYNAMIC_REGISTER_ON 开启后增加下列 API；字符串和子命令树深复制的边界见
12.2，所有动态分配均通过 XMemory.h 管理。

~~~c
bool XConsoleShell_registerCommand(XConsoleShell* self,
                                   const XConsoleCommand* command);
bool XConsoleShell_unregisterCommand(XConsoleShell* self, const char* name);
~~~

### 8.4 会话

~~~
typedef struct XConsoleShellSession {
    uint32_t id;
    char currentPath[XCONSOLE_SHELL_MAX_PATH];
    uint32_t permissionMask;
    void* userData;
    bool authenticated;
    bool cancelled;
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    XConsoleShellIo m_io;             /**< 附加会话借用的传输回调。 */
    char m_lineBuffer[XCONSOLE_SHELL_LINE_BUFFER_SIZE];
    bool m_open;                      /**< 槽位是否已打开。 */
#endif
} XConsoleShellSession;
~~~

cd 仅修改会话 currentPath。文件命令把逻辑路径解析为 XString 后调用 XFileSystem_resolvePath，不调用进程全局 XFileSystem_setCurrentPath，避免多线程和多会话互相影响。

## 9. 解析和输出

### 9.1 语法

- 命令和参数用 ASCII 空白分隔。
- 单引号、双引号保留空格；反斜杠用于转义。
- 默认不做通配符、变量、环境变量和命令替换。
- 命令名按 ASCII 区分大小写；路径遵循底层文件系统规则。
- 达到行长、参数数或路径容量时返回 ResourceLimit，不截断并执行。
- tokenizer 只返回行缓冲内的借用指针，不为每个 token 分配堆内存。

### 9.2 执行流水

~~~
输入字节
  -> 行编辑（可选）
  -> 行结束识别
  -> tokenizer
  -> 命令树查找
  -> 参数数量和权限检查
  -> 处理函数
  -> 分块输出和结果码
~~~

### 9.3 取消和背压

- Ctrl-C 或 XConsoleCancelFn 只设置取消状态，不从任意线程释放命令上下文。
- 大输出必须通过 XConsoleShell_write 分块发送。
- I/O 短写时保存剩余偏移；失败返回 IoError。
- 异步命令不能复用下一条命令的 argv 缓冲区。
- 每个输出循环都要检查取消、超时和会话是否仍有效。

## 10. 内建命令

### 10.1 核心

| 命令 | 开关 | 说明 |
|---|---|---|
| help [path] | HELP | 列出根命令或子命令帮助 |
| version | COMMAND | 输出库和产品版本 |
| echo <text...> | COMMAND | 原样输出参数 |
| history | HISTORY | 固定环形缓冲、历史查询和输出历史命令 |
| stats | STATS | 输出 Shell 输入、输出、成功和失败统计 |
| clear | CLEAR | 输出 ANSI 终端清屏序列，不依赖行编辑器 |
| reset | RESET | 调用 XSystem_reset 请求立即系统复位；危险、管理员命令 |
| reboot | REBOOT | 调用 XSystem_reboot 请求有序系统重启；危险、管理员命令 |
| shutdown | SHUTDOWN | 调用 XSystem_shutdown 请求关机；无操作系统或无后端时结束当前 Shell；危险、管理员命令 |
| exit | EXIT | 退出当前 Shell 和应用程序事件循环 |
| gpio | GPIO | 通过 XGpio 公共 API 管理固定槽位、读写和中断；默认开启 |
| adc | ADC | 通过 XAdc 公共 API 读取原始值或毫伏值；默认开启 |
| pwm | PWM | 通过 XPwm 公共 API 管理频率、占空比和启停；默认开启 |
| i2c | I2C | 通过 XI2c 公共 API 执行固定缓冲读写事务；默认开启 |
| spi | SPI | 通过 XSpi 公共 API 执行固定缓冲全双工事务；默认开启 |
| can | CAN | 通过 XCan 公共 API 管理控制器、位时序、状态和帧收发；默认开启 |
| info | INFO | 输出框架、Shell、平台和核心功能编译信息；只读 |
| uptime | UPTIME | 输出 Shell 初始化后的运行时间；只读 |
| tasks | TASKS | 输出 XThread 注册表和产品任务快照；无可用数据时返回不支持 |
| date [-u] [-I] [+FORMAT] | DATETIME、DATE | 查看本地或 UTC 日期时间；只读 |
| mem [-v] | MEMORY、MEMORY_POOL | 查看全局 XMultiPool 容量和使用率；`-v` 显示各子池；只读 |
| vi/vim <path> | EDITOR | 文件编辑命令；TUI 开启时对齐 Linux vim 常用编辑操作，子功能由 `XTUI_VIM_*` 宏裁剪；默认开启 |

`date` 默认输出 `yyyy-MM-dd HH:mm:ss`；`-u/--utc` 使用 UTC；`-I/--iso-8601`
只输出 ISO 日期。自定义格式支持 `%Y`、`%y`、`%m`、`%d`、`%e`、`%H`、`%M`、
`%S`、`%f`（毫秒）、`%F`、`%T`、`%s`（Unix 秒）和 `%%`。格式化使用固定栈
缓冲，超出容量或使用未实现的控制符时返回失败；不注册 `date -s` 等修改系统时间
的写操作。

`mem` 只报告全局 `XMultiPool`；`mem -v/--verbose` 追加每个 `XFixedPool` 子池的
总容量、已用容量、空闲容量和使用率。`XMemory` 的系统堆、混合分配器以及产品
自行创建但未注册的 `XVariablePool` 不会被命令伪造为可查询池。命令不会触发分配、
释放或整理，只读取当前统计快照。

`uptime` 在 Shell 初始化时记录 `XDateTime_currentMSecsSinceEpoch()`，执行时计算
当前时间与该值的差，并输出 `天 HH:MM:SS`。它表示 Shell 初始化后的运行时间，
不是系统启动时间；底层使用日历时钟，因此 RTC/NTP 校时可能造成偏差，时间 API
不可用或检测到时钟回拨时返回 `NotSupported`。

`tasks` 不直接枚举操作系统任务。`Src/XPlatform/XTask.h` 定义统一的任务状态和
快照字段，并维护由 `XThread` 生命周期注册的固定容量注册表；Linux 和 Windows
复用各自已有的 XThread 平台后端，不需要额外的原生任务枚举驱动。产品可以通过
`XConsoleShell_setTaskProvider()` 同步逐条补充 FreeRTOS、Zephyr 或其他任务，
不能把平台头文件带入 Shell。注册表为空且没有产品提供者时命令返回 `NotSupported`。

clear、reset、reboot 和 shutdown 的职责严格分离：clear 只发送固定 ANSI 清屏序列；
reset 通过 XSystem_reset 请求立即复位；reboot 通过 XSystem_reboot 请求产品
完成必要清理后有序重启。Linux 后端的 reset 直接请求内核重新启动，reboot
先执行 sync 再正常重启；Windows 后端分别使用强制重启和正常重启标志，并申请
系统关机权限。shutdown 在 Linux 上请求 `RB_POWER_OFF`，Windows 上请求
`EWX_POWEROFF`；当 `XPLATFORM_HAS_OS` 为 0 时，XSystem 不调用任何平台默认
后端，Shell 将 shutdown 视为退出当前程序。其他平台没有回调时返回
NotSupported；产品注册的 XSystem 回调优先于平台默认实现。reset、reboot 和
shutdown 默认关闭；exit 默认开启但可单独关闭。reset、reboot 和 shutdown 带有
Dangerous 和 Administrator 标志，三个系统命令都
不注册别名。

GPIO 命令使用 Shell 内嵌的固定槽位表，controller 和 line 作为逻辑引脚键。
open 创建并打开 XGpio，close 关闭并释放句柄；Shell 析构时会先禁用中断、清除
回调，再删除全部槽位。read、info、list 是只读操作；write、toggle、configure、
open、close 和 irq 配置属于危险管理员操作。XCONSOLE_SHELL_GPIO_REQUIRE_POLICY_ON
默认开启时，产品还必须通过 XConsoleShell_setGpioAuthorizeCallback() 授权具体
引脚，避免误操作 Flash、JTAG 或电源控制线。中断回调只原子增加事件计数，irq wait
在普通任务上下文调用 XGpio_processEvents()，不在中断上下文输出文本。

Linux 后端位于 Drive/Posix/XGpio_posix.c，优先使用 GPIO character device ABI v2，
不支持 v2 的内核退回 ABI v1 基本输入输出；非 Linux 平台由安全存根返回
XGpioError_Unsupported，产品可在对应 Drive 目录提供完整后端。GPIO Shell 默认
不启用，启用后仍可单独裁剪每个子命令；XGPIO_TEST_BACKEND 只用于测试目标，
不会链接到产品后端。

CAN 命令使用 Shell 固定槽位持有 XCan 句柄，逻辑 `controller channel` 作为槽位键，
可选 `--name` 保存 SocketCAN 或供应商适配器名称。`can open` 只完成资源打开，
必须再执行 `can start` 才能收发；`can stop` 保留句柄，`can close` 释放句柄。
`can info` 输出 Classical/CAN FD 配置、位速率、模式、能力和最近错误，`can status`
输出总线状态、错误计数和待处理队列；`can send` 支持标准/扩展帧、Remote 帧、
CAN FD 和十六进制负载，`can receive` 支持固定数量和超时。发送、启动、停止、
打开、关闭和恢复均可通过 XConsoleShell_setCanAuthorizeCallback() 执行产品策略。
Shell 不直接调用 SocketCAN、Win32 或 MCU 驱动；XCan 后端必须在 Drive 中实现，
没有后端时应返回 XCanError_Unsupported。XCONSOLE_SHELL_CAN_TEST_BACKEND 仅用于
Shell 自动化测试，提供固定内存队列，不表示真实硬件能力。

根级 `ls [path]` 也可直接列出当前目录或指定目录；它与 `fs ls [path]` 共用
同一文件系统实现，关闭 `XCONSOLE_SHELL_FS_LS_ON` 时两者同时裁剪。

### 10.2 文件系统

文件命令统一置于 `fs` 子树；为兼容常见 Linux 控制台用法，`ls [path]` 另提供
根级入口：

~~~
fs pwd
fs cd <path>
fs ls [-alRFdh] [path...]
fs cat <path> [--offset N] [--length N]
fs hexdump [-C] [-s offset] [-n length] <path>
fs stat <path>
fs rm [-fr] <path...>
fs mkdir [-p] <path...>
fs rmdir [-r] <path...>
fs cp [-rf] <source...> <target>
fs mv [-f] <source...> <target>
fs write <path> <data>
fs link <target> <link>
fs ln -s <target> <link>
fs unlink <path...>
fs touch [-c|--no-create] <path...>
fs chmod <octal-mode> <path...>
fs readlink <path>
fs realpath <path>
fs truncate <path> <bytes>
fs df [path]
fs du [path]
fs wc <path>
fs head [-n lines] <path>
fs tail [-n lines] <path>
fs find <path> [-name pattern] [-type f|d] [-maxdepth N]
fs tree [path]
fs cmp <left> <right>
fs file <path>
fs basename <path>
fs dirname <path>
fs format <volume>
~~~

Linux POSIX 后端已实现上述全部命令及列出的常用参数。每个命令受独立开关控制；写入、删除、权限和截断命令还需要产品权限策略。默认配置只打开只读命令，产品可通过配置宏启用危险命令。根级入口同步注册 `pwd/cd/cat/hexdump/stat/rm/mkdir/rmdir/cp/mv/link/ln/unlink/touch/chmod/readlink/realpath/df/du/wc/head/tail/find/tree/cmp/file/basename/dirname`，`fs` 子树保留为显式命名空间。

当前抽象没有硬链接、用户/组所有者、FIFO/设备节点、挂载点和 umask 的公共 API，
因此 `ln` 只接受显式的 `-s/--symbolic` 符号链接形式；`chown/chgrp`、`mkfifo/mknod`、`mount/umount`
和 `umask` 仍不注册，不能用平台 API 绕过这一边界。

- ls 使用 XFileSystem_opendir/readdir/closedir。
- cat 固定块读取，持续检查取消，绝不把完整文件读入内存。
- hexdump 每次读取 16 字节，支持 `-s/--skip/--offset` 和 `-n/--length`，输出偏移、十六进制字节和 ASCII 侧栏。
- stat 使用 XFileSystem_stat。
- 文件和目录句柄无论成功、失败、取消或短写都必须关闭。
- format 默认关闭，并要求危险命令确认。
- POSIX format 只接受块设备路径；目录和普通文件直接失败，不会调用格式化工具。

`vi`/`vim` 是独立的文件编辑命令，默认开启（`XCONSOLE_SHELL_EDITOR_ON`）。打开
文件后进入编辑状态，后续输入由 Shell 直接分流到编辑器状态机，不进入历史、
tokenizer 和普通权限判断；编辑器只通过 `XFileSystem` 公共 API 读取和写回文件。

- `XCONSOLE_SHELL_EDITOR_TUI_ON` 为 1 时使用 XTui 全屏 `XTuiVim` 控件，行为对齐
  Linux vim 的常用编辑操作；置 0 时回退到精简行式编辑器（`i`/`a` 插入、`d` 删除、
  `r <行号> <文本>` 替换、`:w`/`:q`/`:wq`/`:x`/`:q!`）。行式编辑器从 `i`/`a`
  进入插入模式，输入 `.` 或空行结束；缓冲固定为 `XCONSOLE_SHELL_EDITOR_MAX_LINES`
  行，超过上限或保存失败时返回失败。
- 全屏 vim 的每个子功能都有独立编译期开关，默认全部开启，产品可按嵌入式内存目标
  单独裁剪。产品优先在 `XConsoleShellConfig.h` 使用 `XCONSOLE_SHELL_VIM_*` 别名配置；
  这些别名兼容并映射到 `Src/XTui/XTuiConfig.h` 的 `XTUI_VIM_*`，并带依赖检查；Shell 总开关为
  `XCONSOLE_SHELL_EDITOR_TUI_ON`，关闭时 `XConsoleShellConfig.h` 会强制把所有 `XTUI_VIM_*`
  子开关置 0 以裁剪内存。子开关包括 `_VIM_MULTIBUFFER_ON`（多缓冲与
  `:edit/:bnext/:buffers`）、`_VIM_EX_ON`（扩展 Ex 命令）、`_VIM_SUBSTITUTE_ON`
  （`:s/:%s` 替换）、`_VIM_SUBSTITUTE_CONFIRM_ON`（替换逐项确认 `c` flag，依赖
  `XRegularExpression_ON`）、`_VIM_SEARCH_ON`（`/`、`?`、`n/N`、`*`、`#` 搜索与
  高亮）、`_VIM_YANK_PASTE_ON`（`y/p/P` 与无名寄存器）、`_VIM_REGISTER_ON`
  （命名、编号、黑洞寄存器）、`_VIM_UNDO_REDO_ON`（多级撤销/重做）、
  `_VIM_REPLACE_ON`（`R/r/s/S/C` 等替换快捷操作）、`_VIM_MACRO_ON`（`q/@` 宏录制
  回放）、`_VIM_MARK_ON`（`m{a-zA-Z}` 标记）、`_VIM_JUMPLIST_ON`（跳转列表与
  `Ctrl-O/Ctrl-I`）、`_VIM_VISUAL_ON`（字符/行/块可视模式）、
  `_VIM_ADVANCED_MOTION_ON`（词移动 `w/e/b/W/E/B`、`f/F/t/T`、`gg/G`、括号配对、
  文本对象、操作符计数与重复操作 `.`）、`_VIM_HISTORY_ON`（冒号命令和搜索上下键历史，
  容量由 `_VIM_HISTORY_MAX` 控制）。多缓冲 Ex 还支持 `:w {path}`、`:saveas`、
  `:wa`/`:wqa`、`:qa`/`:qa!` 和 `:bd`/`:bd!`。
- `XTUI_VIM_LINE_MAX`、`XTUI_VIM_CMD_MAX`、`XTUI_VIM_STATUS_MAX`、
  `XTUI_VIM_MAX_BUFFERS`、`XTUI_VIM_JUMPLIST_MAX`、`XTUI_VIM_HISTORY_MAX` 为编译期
  容量，运行时不动态增长。全部子功能关闭时 `XTuiVim` 对象从约 4.1 KiB 精简到约
  0.6 KiB，适合 M 级以下 RAM 的嵌入式目标。

### 10.3 产品回调

产品可注册设备、网络和诊断命令，例如：

~~~
device info
device reboot
device gpio read <pin>
device gpio write <pin> <0|1>
net ifconfig
net ping <address>
~~~

危险命令设置 XCONSOLE_COMMAND_FLAG_DANGEROUS，认证/权限模块或产品回调负责二次确认。核心不预设硬件行为。

## 11. 命令执行

### 11.1 注册命令执行

当前“执行命令”只表示查找命令树并调用 XConsoleCommandHandler。设备控制、协议操作、诊断、复位和库服务都必须通过静态命令表或 XinYueC 对象 API 注册；处理函数通过 XConsoleShell_write 输出结果。

命令处理函数不得调用平台 API，不得保存 tokenizer 返回的 argv 指针，也不得使用字符串拼接后交给系统 shell。需要异步执行时，必须由产品提供一个使用 XThread、XEventLoop、XTimer 等 XinYueC API 的任务对象，并把生命周期绑定到 Shell 会话。

### 11.2 外部进程执行的边界

仓库现已提供 XProcess 公共模块。XCONSOLE_SHELL_EXTERNAL_PROCESS_ON 打开且 XProcess_ON 为 1 时，Shell 编译 `exec <program> [args...]` 适配命令；其实现只调用 XProcess_start_utf8、XProcess_waitForFinished、XProcess_readAllStandardOutput、XProcess_readAllStandardError 和 XProcess_kill 等公开 API，不包含 Qt 私有头文件，也不调用 POSIX、Win32、FreeRTOS 或 Zephyr 进程接口。

该开关默认仍为 0。产品开启后必须在静态命令表或产品层增加程序白名单、参数长度限制、输出上限、超时、取消、认证和审计策略；Shell 不接受字符串 shell 语法，不自动启用管道。
打开 `XCONSOLE_SHELL_REDIRECT_ON` 后，exec 支持显式参数
`--stdin <path>`、`--stdout <path>`、`--stderr <path>` 和 `--append`（同时接受
单独的 `<`、`>`、`2>` 标记），输出通过 XFileSystem 公共 API 写入目标文件。XProcess
自身的管道、环境、工作目录、重定向和 detached 行为由 XProcess 公共模块负责，并有独立
回归与泄漏测试。
打开 `XCONSOLE_SHELL_PIPE_ON` 后，exec 支持
`exec --pipe <producer> -- <consumer>`；两个程序分别以 XProcess 对象启动，并通过
`XProcess_setStandardOutputProcess` 连接，Shell 不调用系统 shell，也不解析通配符或变量。

## 12. 行编辑、历史和补全

feedByte 状态机在对应开关开启后支持：

- CR、LF、CRLF。
- Backspace、Delete、左右移动。
- Ctrl-C 取消当前行或当前命令。
- Ctrl-D 在空行上请求关闭会话。
- 上下键历史。
- Tab 命令及参数补全。

当前已实现固定历史环形缓冲、history 命令、上下箭头历史浏览、左右箭头光标移动、退格删除、Ctrl-D 删除当前字符、clear 清屏命令，以及 Tab 对唯一根命令前缀的补全。文件路径和参数补全仍由 XCONSOLE_SHELL_COMPLETION_ON 的后续扩展提供。

核心不依赖外部行编辑库。若集成 readline、linenoise 一类库，必须放在 Library，按第三方库集成规范提供适配层；核心仍通过 processLine 接收整行，不把上游类型泄漏到公共 API。

### 12.1 输出日志观察

打开 `XCONSOLE_SHELL_LOG_ON` 后，`XConsoleShellIo` 增加可选的 `log` 回调。回调参数中的
session、data 均为调用期间借用，不能保存或释放；data 不是以 NUL 结尾的字符串，必须使用
size。同步输出在底层 write 和 flush 均成功后通知日志，异步输出在数据完整进入固定环形队列后通知，
因此日志表示“Shell 已接受的逻辑输出”，不保证已经到达物理串口或网络。flush 失败不会再次
触发同一日志回调。日志回调不改变命令结果，也不负责认证、审计或脱敏；产品应避免记录口令、
令牌和完整敏感参数，并自行处理持久化、限流和故障降级。

### 12.2 动态命令注册

打开 `XCONSOLE_SHELL_DYNAMIC_REGISTER_ON` 后可调用：

~~~c
bool XConsoleShell_registerCommand(XConsoleShell* self,
                                   const XConsoleCommand* command);
bool XConsoleShell_unregisterCommand(XConsoleShell* self, const char* name);
~~~

registerCommand 会复制根命令的 name、aliases、description、usage 以及整个子命令树，
递归深度上限为 8；动态命令和子命令的描述存储由 Shell 通过 XMemory 管理。处理函数和
userData 是借用指针，不会复制或释放，必须在命令注销前保持有效。动态根节点同时占用动态
容量和总命令容量，超过任一容量立即失败，不会部分注册。名称与现有根命令重复时拒绝注册；
unregisterCommand 只按根名称删除动态节点并释放其完整副本，静态命令不能由该 API 删除。
注册、注销、查找和执行不能与同一 Shell 的输入调用并行进行，产品必须在外层串行化或加锁。
Shell deinit 会回收尚未注销的全部动态命令。

### 12.3 多会话与并发边界

打开 `XCONSOLE_SHELL_MULTI_SESSION_ON` 后，Shell 固定保存一个默认会话（id 为 1）和
`XCONSOLE_SHELL_MAX_SESSIONS - 1` 个附加槽位；附加会话 id 从 2 开始，id 与槽位稳定对应，
直到该槽再次被打开。openSession 复制默认会话当前路径，认证状态按 AUTH 配置初始化，并复制
调用方传入的 I/O 回调集合。会话的 currentPath、permissionMask、authenticated、cancelled、
输入行、历史和行编辑状态彼此隔离。closeSession 只接受附加会话；关闭后返回的会话指针立即
失效，Shell 不释放 userData 或底层传输对象。

`feedByteForSession`、`feedDataForSession`、`processLineForSession` 和
`writeForSession` 会在调用期间切换 Shell 工作区，调用结束后恢复默认会话，因此能够在一个
事件循环中交错处理多个连接。该实现是串行上下文切换，不会创建线程，也不允许两个线程同时
进入同一个 Shell；需要并行 I/O 时由产品为每个 Shell 或每个事件循环分配锁和调度任务。异步
输出模式下，附加会话的 ForSession API 在返回前刷新其队列；默认会话仍由调用方显式调用
`XConsoleShell_flushOutput`。

### 12.4 XIODevice、串口和 TCP 后端

`XConsoleShell_XIODevice.h` 是通用适配层，只保存借用的 `XIODevice*`，把 Shell I/O 回调
映射到 `XIODevice_read_1`、`XIODevice_write_1` 和 `XIODevice_flush`。它不打开、关闭、
配置或删除设备，也不提供独立读线程。

`XConsoleShell_XSerialPort.h` 和 `XConsoleShell_XTcpSocket.h` 只复用各自公开的
XIODevice 继承布局。串口适配器要求端口已由产品配置；Socket 适配器要求连接已建立。两者
均不负责串口参数、连接状态、重连、事件循环、关闭或内存释放；设备必须在生成的 ShellIo
仍被使用期间保持有效。

### 12.5 XTcpServer 多会话适配

打开 `XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON` 时必须同时打开 MULTI_SESSION。适配器保存借用
的 `XConsoleShell*`、`XTcpServer*` 和固定绑定数组。产品事件循环按以下顺序驱动：

1. `acceptPending` 通过 `XTcpServer_hasPendingConnections_base` 和
   `XTcpServer_nextPendingConnection_base` 取得待处理 Socket，并为每个连接打开附加会话。
2. `pump(maxBytes)` 对每个绑定调用 `XIODevice_read_1`，把本轮最多 maxBytes 字节投递到对应
   会话；0 表示当前无数据，负值由传输层自行处理。
3. 连接关闭时调用 `closeSession`，适配器关闭 Socket 的库级句柄并释放 Shell 会话绑定；
   Socket 的最终所有权仍由 XTcpServer/产品决定。

没有空闲绑定槽时，acceptPending 停止接收并保留服务端队列，产品应在下一轮重试。适配器不
监听端口、不创建线程、不调用平台网络 API。Shell 和 XTcpServer 适配器必须在同一事件循环
串行调用，不能让 accept、pump 和 closeSession 并行修改 bindings。

### 12.6 Telnet 协议过滤

打开 `XCONSOLE_SHELL_TELNET_PROTOCOL_ON` 后使用 `XConsoleShellTelnetAdapter` 包裹底层
ShellIo。输入过滤器实现 IAC 转义、DO/DONT/WILL/WONT 协商、SB/SE 子协商跳过、CR-NUL
规则和 IP（Interrupt Process）到 Ctrl-C 的映射；不支持的 DO/WILL 选项分别回复 WONT/DONT，
不会接受远端协商改变本地终端模式。输出中的 IAC 字节自动加倍，普通输出和协商回复都通过
底层 write 并处理短写。适配器不缓存完整命令、不创建 Socket、不分配堆内存；transport 和
userData 由产品拥有，Telnet 适配器必须比生成的 ShellIo 存活更久。多会话场景把同一个
适配器实例绑定到一个会话，输入通过 `feedData` 指定 session 投递。

### 12.7 异步外部进程

打开 `XCONSOLE_SHELL_PROCESS_ASYNC_ON` 时必须同时打开 EXTERNAL_PROCESS 和 XProcess_ON，
并为 `XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY` 个固定槽预留任务指针。命令形式为：

~~~text
exec --async <program> [args...]
~~~

命令通过 XProcess_start_utf8 启动并立即返回 `XConsoleResult_MoreOutput`；Shell 拥有任务
结构和 XProcess 对象，不把 argv 指针保存到任务中。产品事件循环或任务周期调用
`XConsoleShell_pollProcesses(timeoutMsecs)`，该函数轮询进程，进程结束后一次性读取标准输出和
标准错误，写入启动时记录的会话 id，并释放 XByteArray、XProcess 和任务结构。会话已关闭时，
迟到输出被丢弃但任务仍会正常回收。槽位耗尽返回 ResourceLimit。

PROCESS_ASYNC 当前只覆盖独立 `exec --async` 路径，不与 `--pipe`、重定向参数或脚本嵌套
组合；这些组合必须由产品另行编排。Shell deinit 会 kill 未结束进程、等待退出并回收全部
任务。poll、输入、注销和 deinit 必须由同一执行上下文串行调用，禁止从后台线程直接修改
Shell；若产品在独立任务中轮询，仍需把所有 Shell API 调用纳入同一把锁。

## 13. 安全

- 默认关闭进程执行、脚本、管道、重定向和格式化。
- 命令权限至少分只读、写入、危险、管理员四级。
- 启用 XCONSOLE_SHELL_PERMISSION_ON 后，危险命令检查 XConsoleShellPermission_Dangerous，管理员命令额外检查 XConsoleShellPermission_Administrator；关闭时保持产品自行处理权限的兼容模式。
- 每个会话独立保存认证状态、权限和当前目录。
- 文件路径限制于产品根目录，拒绝越界的 ..。
- 危险命令要求二次确认或产品回调确认。
- 启用 AUTH 后，Shell 初始为未认证状态；产品通过 XConsoleShell_setAuthenticated 完成认证状态切换，危险命令会在认证前被拒绝。
- 启用 AUDIT 后，产品可通过 XConsoleShellIo.audit 获得命令、会话和结果码；解析失败时 command 为 NULL，回调不得保存会话或命令行的临时参数。
- 认证失败使用固定延迟和失败次数限制。
- 审计日志不记录口令，也不记录可能包含凭据的完整参数。
- TCP/Telnet 后端默认不接受明文认证；必须有 TLS 或物理隔离策略。

## 14. 内存和线程

### 14.1 内存

- 核心行缓冲、token 指针、命令表和单会话状态默认静态或由调用方提供。
- 不使用 malloc、calloc、realloc、free、strdup；动态内存只能走 XMemory.h。
- XString 临时对象必须 init/deinit_base 成对，禁止 memcpy。
- 默认不保存 token 副本；异步命令显式复制参数。
- 动态注册、历史和异步进程开启后必须在 API 文档中明确所有权。

### 14.2 线程

- 默认一个 Shell 实例只能由一个输入执行上下文调用。
- 异步输出由调用方串行化，或由配置指定输出锁。
- MULTI_SESSION 开启后每个会话有独立 line buffer、cwd、权限和取消状态。
- 命令处理函数默认不保证线程安全，共享设备由产品加锁。
- 文件命令绝不改变进程级当前目录；Shell 会话目录只保存在会话结构中。

### 14.3 资源目标

基线配置：256 字节行缓冲、16 参数、静态 64 命令、单会话、关闭编辑/历史/进程。

- Shell 常驻 RAM 目标不超过 4 KiB，不含 XFileSystem 后端和产品命令上下文。
- 核心 Flash 增量目标不超过 16 KiB；文件命令单独测量。
- 每条核心命令不产生长期堆分配。

目标必须用目标板 map 文件、size 输出和运行时分配统计验证。

## 15. 测试设计

用户要求全量测试和内存泄漏测试。Shell 测试不能只覆盖几个命令，必须覆盖所有配置、错误路径、后端和仓库回归。

### 15.1 文件与入口

- Test/XCodeTest/XConsoleShellTest.c：核心、解析、命令和文件命令。
- Test/XCodeTest/XConsoleShellConfigTest.c：配置依赖与关闭宏。
- Test/XCodeTest/XCodeTest.c：注册 XMenu_XConsoleShellTest。
- main.c：增加非交互测试路径，例如 --test xconsole-shell-all。
- 需要独立目标的 fake transport、fake filesystem 和库级对象测试放 Test/XConsoleShellTest；fake 实现只能调用 XinYueC 公共 API。

### 15.2 功能矩阵

核心和解析：

- 空行、CR/LF/CRLF、普通参数、连续空格、单双引号、转义。
- 未知命令、未知子命令、参数过少/过多、重复别名、空 handler。
- 行缓冲满、参数满、命令名过长、非法 UTF-8、不完整转义。
- help、隐藏命令、层级命令、别名、取消。
- Backspace、Delete、左右键、历史、Tab 在开关开启时验证。

文件命令：

- 根目录、相对路径、绝对路径、点路径、越界路径。
- 空目录、大目录、大文件、读写失败、权限失败、磁盘满、介质移除。
- 只读文件命令、大文件取消、短写、目录迭代器错误。
- 每一个 FS_* 关闭时的编译和运行行为。

回调、进程和后端：

- 静态注册、注销、重复命令、动态注册开关。
- 回调返回 Ok、MoreOutput、Cancelled、Failed。
- XConsoleCommandHandler 参数分词、分段输出、超时和取消状态。
- XSerialPort、XTcpSocket、XTcpServer、XIODevice 适配与多会话交错输入。
- 输出短写、flush 失败、异步输出、取消竞态。

### 15.2.1 可选能力验收矩阵

每行均需在对应宏打开的独立构建中执行；宏关闭时还要检查头文件自包含、静态库链接和
对应符号裁剪。硬件或真实网络验收不能由 fake transport 替代，应在目标平台补做。

| 能力 | 最小运行用例 | 需要确认的所有权/并发结论 |
|---|---|---|
| LOG | `echo`、`help` 和失败命令各执行一次，检查 log 次数、数据长度和 session id | 回调只观察逻辑输出；data/session 不被保存，日志失败不改变命令结果 |
| DYNAMIC_REGISTER | 注册后修改原始 name/description，执行命令，再注销并重复执行 | 描述和子命令仍可用，handler/userData 仍由调用方持有，注销释放 Shell 副本且无泄漏 |
| MULTI_SESSION | 打开两个附加会话，交错输入、不同 cwd/历史和输出，关闭一个再查找 | 状态和 I/O 隔离，关闭后指针失效；交错是串行上下文切换，不产生后台线程 |
| XIODevice/Serial/TCP | 构造空闲对象并生成 ShellIo；对 fake XIODevice 验证短写和 flush | 适配器不打开、关闭或释放底层对象，设备生命周期覆盖 ShellIo 使用期 |
| XTcpServer | 无待处理连接时 accept/pump 返回 0；容量满时拒绝额外连接；逐连接 pump | bindings 固定存储，server/socket 由产品拥有，accept/pump/close 必须串行 |
| Telnet | 协商拒绝、IAC 转义、SB/SE、CR-NUL、IP/Ctrl-C 和普通命令输入输出 | 协商回复和数据输出均处理短写；适配器不分配、不创建线程、不拥有 transport |
| PROCESS_ASYNC | `exec --async` 启动、轮询完成、输出/错误读取、槽位耗尽和 deinit 中止 | `pollProcesses` 驱动回收；会话关闭后丢弃迟到输出；任务由 Shell 持有且调用需串行 |

现有 `Test/XCodeTest/XConsoleShellTest.c` 已按上述宏使用条件编译覆盖：核心构建验证命令
和文件系统，LOG/DYNAMIC_REGISTER/MULTI_SESSION/Telnet/后端适配验证公共入口，
PROCESS_ASYNC 验证启动、轮询和输出。真实 UART、TCP 连接、Telnet 客户端兼容性以及外部
进程白名单仍属于产品集成测试，不能仅凭静态库单元测试宣称完成。

安全和压力：

- 未认证调用只读、写入和危险命令。
- 认证失败次数限制、超时和审计脱敏。
- 100,000 条混合命令、10,000 次 init/deinit。
- 分配失败注入、句柄失败注入和所有错误返回路径。

### 15.3 配置组合

至少建立并运行：

~~~
1. XCONSOLE_SHELL_ON=0
2. 核心 COMMAND/PARSER/IO/HELP
3. 核心 + FILESYSTEM 只读
4. 核心 + 写文件命令
5. 核心 + LINE_EDITOR/HISTORY/COMPLETION
6. 核心 + ASYNC_OUTPUT/MULTI_SESSION
7. 核心 + EXTERNAL_PROCESS（显式打开 XProcess 后编译并运行 exec 回归）
8. 核心 + LOG/DYNAMIC_REGISTER
9. 核心 + MULTI_SESSION/XTCPSERVER（同时验证 XTCPSOCKET/XIODEVICE 依赖）
10. 核心 + TELNET_PROTOCOL
11. 核心 + EXTERNAL_PROCESS/PROCESS_ASYNC
12. 核心 + 全部 Shell 后端宏
13. 全功能
14. FreeRTOS 交叉编译
15. 裸机/STM32 交叉编译
~~~

每种配置都必须通过头文件自包含、静态库编译和测试目标链接。关闭开关后不得残留对应公共符号或平台依赖。

### 15.4 全仓库回归

Shell 测试通过后，必须执行 XinYueC 全量测试入口，不能只运行 XConsoleShellTest：

~~~
cmake -S . -B build
cmake --build build -j$(nproc)
./bin/XinYueC_Static --test all
~~~

如果当前菜单没有非交互 all 路径，实现阶段必须补充脚本入口，同时保持菜单方式兼容。全量回归覆盖容器、内存、代码、I/O、网络、协议栈、数据、定时器、设备和 GUI 的已有测试组。

### 15.5 ASan、UBSan、LSan

~~~
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-asan -j$(nproc)
printf 'Test all\n' | \
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:strict_string_checks=1 \
LSAN_OPTIONS=report_objects=1:exitcode=23 \
./bin/XinYueC_Static
~~~

验收要求：AddressSanitizer、UndefinedBehaviorSanitizer、LeakSanitizer 无报告。核心、文件、动态注册、异步输出和进程错误路径必须都在 sanitizer 构建中执行。

### 15.6 Valgrind

~~~
printf 'Test all\n' | valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=definite,indirect \
  --error-exitcode=1 \
  ./bin/XinYueC_Static
~~~

definite、indirect、possible 的新增泄漏均须归零。第三方库已有噪声使用独立基线和抑制文件说明，不能忽略整个报告。

### 15.7 长期泄漏测试

- 每个用例执行 init/deinit 和 create/delete 至少 10,000 次。
- 动态注册、历史覆盖、文件迭代异常、进程超时循环测试。
- 连续运行混合命令至少 10 分钟且不少于 100,000 条。
- 记录分配器统计、最大常驻 RAM、未释放块和进程 RSS；前后基线一致。
- 在 XMalloc_System 失败注入下验证每条错误路径释放临时资源。

## 16. 实现阶段

### 阶段一：配置与核心

建立 Config、总开关、依赖裁剪、I/O 回调、静态命令表、tokenizer；实现 help、version、echo 与 fake transport；完成核心和配置组合的 sanitizer 基线。

### 阶段二：文件系统

接入 XFileSystem，完成会话逻辑路径；先实现只读命令，再逐项开启写命令；补齐大文件、大目录、取消、短写、错误和泄漏测试。

### 阶段三：交互与后端

行编辑、历史、根命令补全、异步输出、XIODevice、XSerialPort、XTcpSocket、XTcpServer 和
Telnet 适配已经落地；当前验收重点是各配置组合、短写、flush 失败、多会话交错和真实目标
后端验证。颜色输出、文件路径/参数补全、USB CDC/RTT 专用适配仍由产品按需要扩展，不能把
通用 XIODevice 适配器宣称为这些设备的专用驱动。

### 阶段四：进程与安全

XProcess 公共模块已经完成跨层实现：公共层管理状态机和 XIODevice 行为，Drive 后端管理管道、
exec、wait、重定向和 detached。Shell 已通过 XProcess 公共 API 提供同步 exec、管道、重定向、
source 脚本和 PROCESS_ASYNC 适配；异步 exec 不组合管道、重定向或脚本。认证、权限、审计、
白名单、超时和危险命令确认仍由产品配置和安全策略补齐。

### 阶段五：全量验收

运行所有配置组合、全仓库回归、ASan/UBSan/LSan、Valgrind、失败注入、10 分钟长跑；输出 Flash/RAM/map、线程、锁和后端运行证据。

## 17. 完成标准

只有同时满足下列条件才可认为模块完成：

1. 所有公共头文件和配置开关符合仓库代码风格。
2. 总开关关闭时不引入 Shell 代码和平台依赖。
3. 每个功能开关可独立关闭并通过编译。
4. 裸机/RTOS 不依赖 system；有 OS 的进程执行有白名单和超时。
5. 文件命令全部通过 XFileSystem，不绕过平台抽象。
6. 正常、错误、取消、短写和容量路径都有测试。
7. 全仓库测试通过。
8. ASan/UBSan/LSan 和 Valgrind 无新增错误或泄漏。
9. 10 分钟、至少 100,000 条命令压力测试无崩溃、无句柄泄漏、无常驻内存增长。
10. 提供目标平台的 Flash、RAM、线程、锁和后端依赖报告。

## 18. 当前固定约定

- 第一版 API 使用静态字符串和静态命令表；动态注册不是核心依赖。
- 第一版默认只读文件命令，写命令必须产品配置显式开启。
- 默认配置不提供通配符、变量展开、脚本、管道和重定向；SCRIPT、PIPE、REDIRECT 仅在各自宏和依赖打开后提供受限语法，绝不转交给字符串 shell。
- 当前目录是会话状态，不改变进程全局目录。
- 核心不强制外部行编辑库；外部库只能经适配层接入。
- 所有实现和测试提交前必须同时对照本文档与仓库代码风格清单。

## 19. ADC 与 PWM 命令

`adc` 和 `pwm` 通过 `XAdc`、`XPwm` 纯平台契约访问硬件，不在 Shell 中包含
Linux、Windows、MCU HAL 或 sysfs 头文件。两棵命令树都有独立总开关和子命令
开关，句柄保存在固定槽位，Shell 析构时自动释放；打开、配置、启停和动态修改
等危险操作可由产品授权回调限制。

ADC 支持 `list`、`open`、`info`、`read` 和可选 `configure`，读取可选择原始值
或毫伏值；PWM 支持 `list`、`open`、`info`、`configure`、`start`、`stop`、
`set-frequency` 和 `set-duty`。当前没有通用 Linux ADC/PWM 设备契约，因此
Linux、Windows 等未接入产品驱动的平台使用 `Unsupported` 存根，不伪造硬件
状态；`XADC_TEST_BACKEND` 与 `XPWM_TEST_BACKEND` 用于 Shell 回归和泄漏测试。

## 20. I2C 与 SPI 命令

`i2c` 和 `spi` 使用独立的 `XI2c`、`XSpi` 不透明句柄契约。命令层只保存固定
槽位，不保存平台文件描述符或寄存器地址；Shell 通过产品授权回调保护写事务。
I2C 支持 `list`、`open`、`info`、`read`、可选 `write` 和 `writeread`，事务数据
使用固定十六进制缓冲并受 `XCONSOLE_SHELL_I2C_MAX_TRANSFER` 限制。七位地址
为默认模式；十位地址必须在 `open`、`info`、`read`、`write`、`writeread`和
`close` 中显式传入 `--ten-bit`，避免两种地址模式访问到错误槽位。SPI 支持
`list`、`open`、`info`、`close`，可选 `transfer`，打开时可配置频率、模式、字长
和位序，交换数据受 `XCONSOLE_SHELL_SPI_MAX_TRANSFER` 限制。

当前仓库没有通用 Linux、Windows 或 MCU 的 I2C/SPI 产品驱动，unsupported 后端
会返回 `XI2cError_Unsupported` 或 `XSpiError_Unsupported`，不会伪造总线状态。
`XI2C_TEST_BACKEND`、`XSPI_TEST_BACKEND` 只用于 Shell 回归测试；产品接入真实
控制器时，应在 `Drive` 下实现同名公共 API，并准确报告能力和错误。

## 21. 登录与 Linux 风格用户管理

`XCONSOLE_SHELL_LOGIN_ON` 是登录与账户库总开关，默认开启；
`XCONSOLE_SHELL_LOGIN_REQUIRED_ON` 是强制认证开关，默认跟随登录总开关开启。
强制认证开启后，普通命令在登录前返回权限错误，仅 `login`、首次账户初始化所需的
`useradd`/`passwd`、`logout`、`help`、`version` 和 `exit` 等显式允许的命令可执行。
登录总开关会同步开启 `XCONSOLE_SHELL_AUTH_ON` 和 `XCONSOLE_SHELL_PERMISSION_ON`，产品可以在配置头文件中
显式关闭整个登录模块。账户文件默认是当前会话目录下的 `xconsole_users.json`，也可
通过 `XConsoleShellLogin_setDatabasePath()` 为每个 Shell 设置路径。文件读写只经过
`XFileSystem`，解析和生成只经过 `XJsonDocument`，不读取宿主机 `/etc/passwd`、
`/etc/shadow`、Windows 注册表或任何平台账户数据库。

JSON 根对象包含 `version` 和 `users` 数组；每个用户记录包含 `name`、`uid`、`gid`、
`groups`、`permissions`、`locked`、`passwordSet`、`iterations`、`salt` 和 `hash`。
`passwordSet` 为 false 时表示账户尚未设置密码，账户保持锁定且不能登录。密码保存为随机盐
的迭代 HMAC-SHA256 摘要，不保存明文；账户文件成功写入后，POSIX 后端请求 `0600`
权限，FatFs 等不支持权限位的后端忽略该设置失败。用户名遵循 Linux 账户名的 ASCII
子集，UID/GID 和附加组使用无符号 32 位值，当前会话固定保存一个主 GID 和有限数量
附加组，容量由 `XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY` 控制。

首次没有账户文件时，`useradd <name>` 允许创建一个 UID/GID 为 0、同时拥有 `Dangerous`
和 `Administrator` 权限的无密码 bootstrap 管理员；该账户在设置密码前不能登录，但可
使用 `passwd <name>` 后按两次密码提示完成首次初始化。账户文件存在后，添加、删除、修改用户
必须由已登录管理员执行。后续用户默认从 UID/GID 1000 开始，可通过
`-u/--uid`、`-g/--gid`、`-G/--groups GID,...`、`-a/--append`、`-L/--lock`、
`-U/--unlock` 和 `-l/--login` 调整账户；`--permissions` 与 `--admin` 是 XinYueC
扩展选项。删除最后一个管理员、删除当前登录账户均被拒绝；`userdel -r` 因当前账户模型
没有主目录字段而明确返回不支持，不会伪造删除行为。

命令包括 `login`、`logout`、`whoami`、`id [user]`、`groups [user]`、`users`、
`userlist`、`useradd [options] LOGIN`、`userdel [-r] LOGIN`、`usermod [options] LOGIN`
和 `passwd [user]`（兼容别名 `password`）。`login <user>` 后由 Shell 提示
`Password:` 并读取下一行；`passwd [user]` 依次提示 `Current password:`（非管理员
修改自己的密码时）、`New password:` 和 `Retype new password:`。管理员可以指定其他
用户，改其他用户密码不要求旧密码；首次无密码 bootstrap 管理员允许在未登录状态
使用 `passwd <user>` 完成初始化。`id [user]`/`groups [user]` 输出 Linux 风格
`uid=1001(name)` 形式，UID/GID 与账户名匹配时同时显示组名，任意已认证用户可以
查询本地 JSON 账户库中的其他用户。密码命令标记为敏感命令，Shell 历史不保存
其输入行；传输层仍应使用受控串口、USB CDC、RTT 或加密网络，不应把密码作为明文传输
到不可信链路。若 `XConsoleShellIo.inputEcho` 已接入，Shell 会通过库级输入抽象关闭并
恢复终端回显；不支持回显控制的管道、重定向和 FatFs 后端仍保证 Shell 不主动输出密码。
`users` 显示已认证会话用户名；`userlist` 才显示本地 JSON 账户的用户名、
UID、GID、权限掩码和锁定状态，二者都不显示盐或摘要。

登录成功后，当前会话填充用户名、UID、GID、附加组和权限掩码；默认入口的提示符由
`XinYueC> ` 变为 `<username>> `，注销后恢复。`uid=0` 自动补齐危险和管理员位；其他
用户的危险命令需要 `XConsoleShellPermission_Dangerous`，管理员命令还需要
`XConsoleShellPermission_Administrator`。这些是 Shell 应用层权限，不会伪装或切换
宿主机进程的真实 Linux UID/GID，文件系统实际访问仍由 `XFileSystem` 后端和宿主系统
权限共同决定。
