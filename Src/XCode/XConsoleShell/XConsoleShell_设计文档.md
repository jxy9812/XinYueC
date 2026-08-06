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
4. help、version、echo、pwd、cd、ls、cat、stat、rm、mkdir、rmdir、cp、mv 等文件命令。
5. 设备控制命令通过回调注册，不依赖操作系统进程。
6. 设备控制命令通过已注册的库级回调执行；不在本模块内执行任意外部进程。
7. 每个功能独立编译开关，且由一个总开关统一控制。
8. 默认使用固定缓冲区和静态命令表，关闭功能后不产生对应符号和依赖。

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
XCONSOLE_SHELL_LOG_ON
XCONSOLE_SHELL_MULTI_SESSION_ON
XCONSOLE_SHELL_XIODEVICE_BACKEND_ON
XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON
XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON
XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
XCONSOLE_SHELL_TELNET_PROTOCOL_ON
XCONSOLE_SHELL_TRANSPORT_CALLBACK_ON

/* 文件系统 */
XCONSOLE_SHELL_FILESYSTEM_ON
XCONSOLE_SHELL_FS_PWD_ON
XCONSOLE_SHELL_FS_CD_ON
XCONSOLE_SHELL_FS_LS_ON
XCONSOLE_SHELL_FS_CAT_ON
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
| Telnet | `IO_ON`、`TELNET_PROTOCOL_ON` | 在 Shell I/O 前过滤 Telnet IAC、协商、子协商和 CR-NUL；不创建网络对象。|
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
| clear | LINE_EDITOR | 输出 ANSI 终端清屏序列 |

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

Linux POSIX 后端已实现上述全部命令及列出的常用参数。每个命令受独立开关控制；写入、删除、权限和截断命令还需要产品权限策略。默认配置只打开只读命令，产品可通过配置宏启用危险命令。根级入口同步注册 `pwd/cd/cat/stat/rm/mkdir/rmdir/cp/mv/link/ln/unlink/touch/chmod/readlink/realpath/df/du/wc/head/tail/find/tree/cmp/file/basename/dirname`，`fs` 子树保留为显式命名空间。

当前抽象没有硬链接、用户/组所有者、FIFO/设备节点、挂载点和 umask 的公共 API，
因此 `ln` 只接受显式的 `-s/--symbolic` 符号链接形式；`chown/chgrp`、`mkfifo/mknod`、`mount/umount`
和 `umask` 仍不注册，不能用平台 API 绕过这一边界。

- ls 使用 XFileSystem_opendir/readdir/closedir。
- cat 固定块读取，持续检查取消，绝不把完整文件读入内存。
- stat 使用 XFileSystem_stat。
- 文件和目录句柄无论成功、失败、取消或短写都必须关闭。
- format 默认关闭，并要求危险命令确认。
- POSIX format 只接受块设备路径；目录和普通文件直接失败，不会调用格式化工具。

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
