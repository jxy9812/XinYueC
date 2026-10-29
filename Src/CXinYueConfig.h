/**
 * @file CXinYueConfig.h
 * @brief XinYueC 全局配置公开头文件。
 * @details 汇总平台/字节序自动检测、调试输出配置、XPrintf 输出编码模式和各模块
 *          条件编译总开关；按「文件组织规范」各模块头文件通常第一个包含本文件。
 *          无 Qt 对齐对象；平台约束：自动检测 FreeRTOS/Windows/POSIX/裸机，
 *          仅依赖编译器预定义宏，不引入任何平台 API 或 C 标准库头文件。
 */
#ifndef CXINYUECONFIG_H
#define CXINYUECONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        平台/操作系统自动检测                                */
/* ========================================================================== */
/* 单平台互斥宏（仅一个为 1，其余为 0）
 * 检测优先级：FreeRTOS > Windows > POSIX(Linux/macOS/BSD) > 裸机
 * 用法：用 #if XPLATFORM_WINDOWS / XPLATFORM_POSIX / XPLATFORM_FREERTOS /
 *       XPLATFORM_BAREMETAL 做平台条件编译，替代散落的 _WIN32/__linux__ 判断
 */
#if defined(__FreeRTOS__)
  #define XPLATFORM_FREERTOS    1
  #define XPLATFORM_WINDOWS     0
  #define XPLATFORM_POSIX       0
  #define XPLATFORM_BAREMETAL   0
#elif defined(_WIN32) || defined(_WIN64)
  #define XPLATFORM_FREERTOS    0
  #define XPLATFORM_WINDOWS     1
  #define XPLATFORM_POSIX       0
  #define XPLATFORM_BAREMETAL   0
#elif defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  #define XPLATFORM_FREERTOS    0
  #define XPLATFORM_WINDOWS     0
  #define XPLATFORM_POSIX       1
  #define XPLATFORM_BAREMETAL   0
#else
  #define XPLATFORM_FREERTOS    0
  #define XPLATFORM_WINDOWS     0
  #define XPLATFORM_POSIX       0
  #define XPLATFORM_BAREMETAL   1
#endif

/* 派生宏：是否有操作系统（桌面/POSIX/FreeRTOS 均视为有 OS） */
#define XPLATFORM_HAS_OS        (XPLATFORM_WINDOWS || XPLATFORM_POSIX || XPLATFORM_FREERTOS)
/* 桌面平台（Windows / Linux / macOS），用于选择大缓冲区等配置 */
#define XPLATFORM_DESKTOP       (XPLATFORM_WINDOWS || XPLATFORM_POSIX)

/* 与 lwIP NO_SYS 对接的推荐默认值：
 *   裸机环境 -> NO_SYS=1（最小 sys_arch，无 tcpip_thread）
 *   有 OS 环境 -> NO_SYS=0（完整 sys_arch，lwIP 内部创建 tcpip_thread）
 * @note 实际取值由 XNetwork_config.h 的 XNETWORK_LWIP_NO_SYS 引用，
 *       用户可显式 #define XNETWORK_LWIP_NO_SYS 覆盖此默认值 */
#define XPLATFORM_LWIP_NO_SYS_DEFAULT   (XPLATFORM_BAREMETAL ? 1 : 0)

/* ========================================================================== */
/*                                字节序(大小端)自动检测                                */
/* ========================================================================== */
/* 编译器预定义宏自动检测字节序，无需手动配置
 * GCC/Clang: __BYTE_ORDER__ + __ORDER_BIG_ENDIAN__
 * IAR/ARM:   __BIG_ENDIAN / __ARM_BIG_ENDIAN
 * MSVC:      x86/x64/ARM 均为小端序
 * 若需手动指定，可在编译命令行覆盖: -DIS_BIG_ENDIAN=1 */
#ifndef IS_BIG_ENDIAN
  #if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
    #define IS_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  #elif defined(__BIG_ENDIAN) || defined(__ARM_BIG_ENDIAN)
    #define IS_BIG_ENDIAN 1
  #elif defined(_MSC_VER)
    #define IS_BIG_ENDIAN 0
  #else
    #define IS_BIG_ENDIAN 0
  #endif
#endif

/* ========================================================================== */
/*                                   调试输出配置                                   */
/* ========================================================================== */
/** @brief 调试输出总开关；置 1 且为 _DEBUG 构建时启用 XDEBUG_PRINTF 输出调试日志，置 0 时不输出。
 * @note 完全不定义此宏时，XDEBUG_PRINTF 回退为仅按 _DEBUG 判定（带 "Debug" 标签）。 */
#define DEBUG_ON						0
/** @brief 错误输出开关；置 1 时启用 XERROR_PRINTF 输出带 "XError" 标签的错误日志，置 0 时编译为空。 */
#define XERROR_ON						1//错误输出
/** @brief 测试/演示代码总开关；置 1 时启用 Test/ 目录测试代码及 main.c 中的演示入口，置 0 时裁剪。 */
#define DEMOTEST_ON						1//测试代码
/** @brief 测试/演示代码开关（FreeType 字库战役侧命名，与 DEMOTEST_ON 同义同值）：
 *  XFontFtTest.{h,c} 等远端新增测试以此门控，两宏必须同时定义，缺一即整段
 *  测试定义编译出局而调用点仍在（LNK2019：XTestMenu_XFontFtTest 未解析）。 */
#define DEMOTEST						1//测试代码

/* ========================================================================== */
/*                                通用模块/编译开关配置                                 */
/* ========================================================================== */
#include"XClassConfig.h"
#include"XContainerConfig.h"
#include"XProcessConfig.h"

/* ========================================================================== */
/*                              XAction 动作模块开关                              */
/* ========================================================================== */
/** @brief XAction 模块总开关；置 0 时裁剪 XAction 公共 API 及其实现。
 * @note 该模块对标 Qt 6.8 QAction，位于 XCode 层、依赖 XObject 信号体系；
 *       关闭后依赖 XAction 的 Test/XTestMenu 测试菜单相关能力也需同步裁剪。 */
#ifndef XACTION_ON
#define XACTION_ON 1
#endif


/* ========================================================================== */
/*                          XProperty 声明式属性绑定模块开关                          */
/* ========================================================================== */
/** @brief XProperty 模块总开关；置 0 时裁剪声明式属性绑定全部公共 API。
 * @note 该模块对标 Qt 6.8 QProperty/QBindable/QPropertyBinding/QPropertyAlias，
 *       位于 XClass 层、依赖 XVariant/XObject 信号体系与 XSync 的 XThreadData
 *       （绑定依赖捕获的线程求值栈）；关闭后依赖属性绑定的模块需同步裁剪。 */
#ifndef XPROPERTY_ON
#define XPROPERTY_ON 1
#endif


/* ========================================================================== */
/*                                XSync 同步模块开关                                */
/* ========================================================================== */
/** @brief XSync 模块总开关；置 0 时裁剪整个 XSync 公共 API 和所有子功能。
 * @note 详细的子功能开关在 XSync_config.h 中定义，如 XMUTEX_ON / XTHREAD_ON 等。
 *       关闭后若仍有其它模块无条件引用 XSync 符号，需同步裁剪对应依赖。 */
#ifndef XSYNC_ON
#define XSYNC_ON 1
#endif
#include"XSync_config.h"

/* ========================================================================== */
/*                              XFile 文件系统模块总开关                               */
/* ========================================================================== */
/*                        设备层（XDevice 家族）总开关                        */
/* ========================================================================== */
/** @brief 设备层总开关；置 0 时整层统一设备抽象裁掉（各设备类与平台后端
 *         一并不编译）。
 * @details 设备域在本文件里**只保留这一个总开关**；各设备类的独立开关与
 *          设备容量配置集中在 Src/XDevice/XDevice_config.h（XDevice_config.h）。
 *          目的：同一套框架可按产品形态适配——桌面全量、单片机裸机精简、
 *          只带若干外设的定制板——只需改设备配置文件，不必翻动框架总配置。
 * @note   必须排在依赖设备的业务开关（XFILE_ON / XNETWORK_ON）之前：业务
 *         开关的默认值由设备开关派生。 */
#ifndef XDEVICE_ON
#define XDEVICE_ON 1
#endif
#include"XDevice_config.h"

/* ========================================================================== */
/** @brief XFile 模块总开关；置 0 时裁剪整个 XFile 对外公开 API 及所有子功能。
 * @note 详细的子功能开关在 XFileSystem_config.h 中定义，如 XDIR_ON / XFILE_OBJECT_ON 等。
 *       关闭后若仍有其它模块无条件引用 XFile 符号，需同步裁剪对应依赖。
 * @details 依赖文件设备：设备被裁剪时业务层一并裁剪（避免未定义符号）。 */
#ifndef XFILE_ON
#define XFILE_ON (XDEVICE_ON && XDEVICE_FILE_ON)
#endif
#include"XFileSystem_config.h"

/* ========================================================================== */
/*                             XNetwork 网络协议模块总开关                             */
/* ========================================================================== */
/** @brief XNetwork 模块总开关；置 0 时裁剪整个 XNetwork 对外公开 API 及所有子功能。
 * @note 详细的子功能开关在 XNetwork_config.h 中定义，如 XNETWORK_TCPSERVER_ON 等。
 *       关闭后若仍有其它模块无条件引用 XNetwork 符号，需同步裁剪对应依赖。
 * @details 依赖网络设备：设备被裁剪时业务层一并裁剪（避免未定义符号）。 */
#ifndef XNETWORK_ON
#define XNETWORK_ON (XDEVICE_ON && XDEVICE_NETWORK_ON)
#endif
#include"XNetwork_config.h"

/* ========================================================================== */
/*                        XProtocol 协议栈模块                             */
/* ========================================================================== */
/** @brief XProtocol 模块总开关；置 0 时裁剪整个 XProtocol 旗下所有协议。 */
#ifndef XPROTOCOL_ON
#define XPROTOCOL_ON 1
#endif
#include"XProtocol_config.h"

/* ========================================================================== */
/** @brief 工业设备层（Industry/）总开关；置 0 时通道接口（XChannel）与
 *         工业设备（XSolenoidValve 等）连同各传输适配器一并裁掉。
 * @details 各子模块开关集中在 Industry/XIndustry_config.h（通道接口
 *          XINDUSTRY_CHANNEL_ON、电磁阀 XSOLENOID_VALVE_ON、适配器
 *          XINDUSTRY_GPIO_ON / XINDUSTRY_MODBUS_ON），与设备层同款裁剪口径。
 * @note   必须排在 XProtocol_config.h 之后：XINDUSTRY_MODBUS_ON 的默认值
 *         在 XIndustry_config.h 内按 XMODBUS_ON 派生，排前会因"头保护挡住
 *         递归包含、派生点早于 XMODBUS_ON 定义"而恒派生为 0（2026-10-29 实锅）。 */
#ifndef XINDUSTRY_ON
#define XINDUSTRY_ON 1
#endif
#include"XIndustry_config.h"

/* ========================================================================== */
/*                             XConsoleShell 模块开关                             */
/* ========================================================================== */
/** @brief XConsoleShell 模块总开关；置 0 时裁剪整个 Shell 公共 API 和所有子功能。 */
#ifndef XCONSOLE_SHELL_ON
#define XCONSOLE_SHELL_ON 1
#endif
#include"XConsoleShellConfig.h"

/* ========================================================================== */
/*                        XTui TUI 模块开关                              */
/* ========================================================================== */
/** @brief XTui 通用 TUI 模块总开关；置 0 时裁剪整个 XTui 公共 API。 */
#ifndef XTUI_ON
#define XTUI_ON 1
#endif
#include"XTuiConfig.h"
/** @brief XExcel 模块（依赖 zlib / mbedtls）。 */
#ifndef XEXCEL_ON
#define XEXCEL_ON 1
#endif
/** @brief SQL 模块总开关；XSql/XSqliteDriver/XMySqlDriver 整体依赖 sqlite3 外部库，
 *  嵌入式裁剪时置 0（驱动侧另需 XSqliteDriver_ON / XMySqlDriver_ON 之类子开关）。 */
#ifndef XSQL_ON
#define XSQL_ON 1
#endif
/* GUI 子开关集中由 Src/XGui/XGuiConfig.h 管理；这里不再定义 GUI 子宏。 */
#ifndef XGUI_ON
#define XGUI_ON 1
#endif
#include "XGui/XGuiConfig.h"

/* 文件顶部以 #if XXX_ON 作守卫的模块，其开关默认值必须在守卫求值前可见。
 * ESP8266 靠 -include ESP8266Minimal.h 拉入本文件；桌面由 CMake 对核心目标
 * /FI CXinYueConfig.h 对齐同一路径。字体 FreeType 与自升级的默认值不在
 * 本文件内，这里补拉对应配置，避免 #if XFONT_FREETYPE_ON / XUPGRADE_ON
 * 在 TU 首行因宏未定义被静默裁成 0。 */
#include "XFontFt_config.h"
#include "XUpgrade_config.h"

/* ========================================================================== */
/*                               XPrintf 输出编码模式                               */
/* ========================================================================== */
/* XPRINTF_UTF8_CONSOLE = 1  强制控制台为UTF-8，直接输出UTF-8（Windows桌面推荐）
 *                          - 源码是UTF-8，/utf-8编译后字面量也是UTF-8，无需转换
 *                          - 与lwIP的SetConsoleOutputCP(CP_UTF8)兼容
 *                          - 首次调用XPrintf时自动设置控制台代码页
 * XPRINTF_UTF8_CONSOLE = 0  转换为本地编码(GBK)后输出（嵌入式GBK串口终端推荐）
 *                          - 调用XChar_utf8ToGbkStream做UTF-8->GBK转换
 *                          - 适用于串口终端等只支持GBK的场景
 * 注意：Linux/macOS始终直接输出UTF-8，此宏仅影响Windows */
#define XPRINTF_UTF8_CONSOLE			1

/* ========================================================================== */
/*                                   算法模块开关                                   */
/* ========================================================================== */
/** @brief XBase64 算法模块开关；置 1 时启用 XBase64.h 公开的 Base64 编码/解码 API，置 0 时裁剪整个模块。 */
#ifndef XBase64_ON
#define XBase64_ON 1
#endif
/** @brief XAbstractNetIoRing 网络 I/O 事件环模块开关；置 0 时裁剪整个模块公共 API 和实现。
 * @note 对齐 io_uring 的 SQ/CQ 抽象事件环，平台 I/O（IOCP/epoll 等）由平台子类实现；
 *       关闭后 XAbstractEventDispatcher 不再创建全局事件环，XFileDevice 的异步注册也随之失效。 */
/** @brief XML 流解析模块总开关；嵌入式裁剪可置 0。 */
/** @brief QRCode 编码模块总开关；与控制台无关，嵌入式裁剪置 0。 */
#ifndef XRCODE_ON
#define XRCODE_ON 1
#endif
#ifndef XXmlStream_ON
#define XXmlStream_ON 1
#endif
/** @brief DOM 模块总开关；依赖 XXmlStream，裁剪时两者需一并关闭。 */
#ifndef XDOM_ON
#define XDOM_ON 1
#endif

#ifndef XAbstractNetIoRing_ON
#define XAbstractNetIoRing_ON 1
#endif

/* ========================================================================== */
/*                                事件投递 无锁队列大小                                 */
/* ========================================================================== */
/** @brief 跨线程事件试投递（XThreadData_tryPostEvent）所用每线程无锁队列的容量，单位为队列元素个数。
 * @note 每个线程的 XThreadData 初始化时按此容量建立元素为 XPostEvent 的 XLockFreeQueue，
 *       队满时试投递失败并输出 XError；用户可在包含本头文件前预定义本宏覆盖默认值：
 *       桌面平台默认 512，其余（嵌入式）平台默认 64 以节省 RAM。 */
#ifndef TRY_POST_EVENT_QUEUE_SIZE
#if XPLATFORM_DESKTOP
#define TRY_POST_EVENT_QUEUE_SIZE      512
#else
#define TRY_POST_EVENT_QUEUE_SIZE      64   /* 适配嵌入式设备节省RAM */
#endif
#endif

/* ========================================================================== */
/*                                  调试/输出宏定义                                  */
/* ========================================================================== */
/** @brief 模块开关守卫宏；on 给出的编译期开关为 0（未开启）时经 ISNULL 报告调用处文件、函数和行号，
 *         并提示该函数需要在 CXinYueConfig.h 中开启对应开关。 */
#define IS_ON_DEBUG(on)						ISNULL(on,"此函数需要开启"#on",在CXinYueConfig.h")

/**
 * @brief 输出带文件、函数和行号上下文的格式化日志。
 * @details 先格式化可变参数，再经 XPrintf 统一处理平台输出编码和换行；label 为空时
 *          不输出标签前缀。供 XDEBUG_PRINTF/XERROR_PRINTF 调试与错误输出宏使用。
 * @param label 日志标签字符串；只读借用，不取得所有权；NULL 或空串时不输出标签前缀。
 * @param file 源文件名，通常传 __FILE__；只读借用。
 * @param function 函数名，通常传 __FUNCTION__；只读借用。
 * @param line 源代码行号，通常传 __LINE__。
 * @param format printf 风格的 UTF-8 格式串；只读借用；为 NULL 时不输出并返回 0。
 * @param ... 与 format 中转换说明符匹配的可变参数。
 * @return 返回 XPrintf 的返回值（实际输出的字符数）；format 为 NULL、格式化失败或
 *         缓冲区分配失败时不产生输出并返回 0。
 */
int XPrintf_context(const char* label, const char* file, const char* function,
                    int line, const char* format, ...);

//定义debug信息输出方式
#ifdef DEBUG_ON
#if ((DEBUG_ON) && defined(_DEBUG))
#define XDEBUG_PRINTF(...) XPrintf_context("", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define XDEBUG_PRINTF(...)
#endif
#else
#if defined _DEBUG
#define XDEBUG_PRINTF(...) XPrintf_context("Debug", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define XDEBUG_PRINTF(...)
#endif
#endif // !DEBUG_ON

//定义错误信息输出方式
#if ((XERROR_ON))
#define XERROR_PRINTF(...) XPrintf_context("XError", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define XERROR_PRINTF(...)
#endif

#ifdef __cplusplus
}
#endif
/** @brief 加密与哈希算法模块总开关。 */
#ifndef XCryptographic_ON
#define XCryptographic_ON 1
#endif

#endif /* CXINYUECONFIG_H */
