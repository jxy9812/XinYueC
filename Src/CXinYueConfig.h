#ifndef CXINYUECONFIG_H
#define CXINYUECONFIG_H
#ifdef __cplusplus
extern "C" {
#endif
/* ==================== 字节序(大小端)自动检测 ==================== */
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
//数据结构配置文件
#define DEBUG_ON						0
#define XERROR_ON						1//错误输出
#include"XClass/XClassConfig.h"
#include"XContainer/XContainerConfig.h"
/* ========================================================================== */
/*                        XPrintf 输出编码模式                                */
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
#define DEMOTEST						1//测试代码
/*                          算法                            */
#define XCrc_ON                         1
#define XCrc16_ON                       1
#define XCrc32_ON                       1
#define XBase64_ON						1
#define	XAbstractNetIoRing_ON					1
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

// 事件投递 无锁队列大小
#ifndef TryPostEvent_QueueSize
#if XPLATFORM_DESKTOP
#define TryPostEvent_QueueSize      512 
#else
#define TryPostEvent_QueueSize      64   /* 适配嵌入式设备节省RAM */
#endif
#endif

#define IS_ON_DEBUG(on)						ISNULL(on,"此函数需要开启"#on",在CXinYueConfig.h")

//定义debug信息输出方式
#ifdef DEBUG_ON
#if ((DEBUG_ON) && defined(_DEBUG))
#define XDEBUG_PRINTF(fmt,...) XPrintf("[FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,##__VA_ARGS__)
#else
#define XDEBUG_PRINTF(fmt,...)
#endif
#else
#if defined _DEBUG
#define XDEBUG_PRINTF(fmt,...) XPrintf("Debug [FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,##__VA_ARGS__)
#else
#define XDEBUG_PRINTF(fmt,...)
#endif
#endif // !DEBUG_ON

//定义错误信息输出方式
#if ((XERROR_ON))
#define XERROR_PRINTF(fmt,...) XPrintf("XError [FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,##__VA_ARGS__)
#else
#define XERROR_PRINTF(fmt,...)
#endif

#ifdef __cplusplus
}
#endif
#endif
