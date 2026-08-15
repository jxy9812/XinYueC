#ifndef CXINYUECONFIG_H
#define CXINYUECONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        ??/????????                                */
/* ========================================================================== */
/* ??????????? 1???? 0?
 * ??????FreeRTOS > Windows > POSIX(Linux/macOS/BSD) > ??
 * ???? #if XPLATFORM_WINDOWS / XPLATFORM_POSIX / XPLATFORM_FREERTOS /
 *       XPLATFORM_BAREMETAL ????????????? _WIN32/__linux__ ??
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

/* ??????????????/POSIX/FreeRTOS ???? OS? */
#define XPLATFORM_HAS_OS        (XPLATFORM_WINDOWS || XPLATFORM_POSIX || XPLATFORM_FREERTOS)
/* ?????Windows / Linux / macOS????????????? */
#define XPLATFORM_DESKTOP       (XPLATFORM_WINDOWS || XPLATFORM_POSIX)

/* ? lwIP NO_SYS ?????????
 *   ???? -> NO_SYS=1??? sys_arch?? tcpip_thread?
 *   ? OS ?? -> NO_SYS=0??? sys_arch?lwIP ???? tcpip_thread?
 * @note ????? XNetwork_config.h ? XNETWORK_LWIP_NO_SYS ???
 *       ????? #define XNETWORK_LWIP_NO_SYS ?????? */
#define XPLATFORM_LWIP_NO_SYS_DEFAULT   (XPLATFORM_BAREMETAL ? 1 : 0)

/* ========================================================================== */
/*                        ???(???)????                               */
/* ========================================================================== */
/* ?????????????????????
 * GCC/Clang: __BYTE_ORDER__ + __ORDER_BIG_ENDIAN__
 * IAR/ARM:   __BIG_ENDIAN / __ARM_BIG_ENDIAN
 * MSVC:      x86/x64/ARM ?????
 * ????????????????: -DIS_BIG_ENDIAN=1 */
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
/*                        ??????                                        */
/* ========================================================================== */
#define DEBUG_ON						0
#define XERROR_ON						1//????
#define DEMOTEST						1//????

/* ========================================================================== */
/*                        ????/??????                                */
/* ========================================================================== */
#include"XClass/XClassConfig.h"
#include"XContainer/XContainerConfig.h"
#include"XCode/XProcess/XProcessConfig.h"


/* ========================================================================== */
/*                        XSync ??????                             */
/* ========================================================================== */
/** @brief XSync ??????? 0 ??????????????????
 * @note ??????? XSync_config.h ????XMUTEX_ON / XTHREAD_ON ???
 *       ??????????????? XSync ????????????? */
#ifndef XSYNC_ON
#define XSYNC_ON 1
#endif
#include"XCode/XSync/XSync_config.h"

/* ========================================================================== */
/*                        XFile ????????                                */
/* ========================================================================== */
/** @brief XFile ??????? 0 ????? XFile ???? API ???????
 * @note ????? XFileSystem_config.h ??????? XFile ???????
 *       ????????????????????????/????? */
#ifndef XFILE_ON
#define XFILE_ON 1
#endif
#include"XCode/XFile/XFileSystem_config.h"

/* ========================================================================== */
/*                        XNetwork ????????                             */
/* ========================================================================== */
/** @brief XNetwork ??????? 0 ????? XNetwork ???? API ???????
 * @note ????? XNetwork_config.h ??????? XNetwork ???????
 *       ????????????????????????? */
#ifndef XNETWORK_ON
#define XNETWORK_ON 1
#endif
#include"XCode/XNetwork/XNetwork_config.h"

/* ========================================================================== */
/*                        XProtocol 协议栈模块                             */
/* ========================================================================== */
/** @brief XProtocol 模块总开关；置 0 时裁剪整个 XProtocol 旗下所有协议。 */
#ifndef XPROTOCOL_ON
#define XPROTOCOL_ON 1
#endif
#include"XProtocol/XProtocol_config.h"

/* ========================================================================== */
/*                        XConsoleShell ??????                           */
/* ========================================================================== */
/** @brief Shell ????? 0 ????? Shell ?? API ??????? */
#ifndef XCONSOLE_SHELL_ON
#define XCONSOLE_SHELL_ON 1
#endif
#include"XCode/XConsoleShell/XConsoleShellConfig.h"

/* ========================================================================== */
/*                        XTui TUI 模块开关                              */
/* ========================================================================== */
/** @brief XTui 通用 TUI 模块总开关；置 0 时裁剪整个 XTui 公共 API。 */
#ifndef XTUI_ON
#define XTUI_ON 1
#endif
#include"XTui/XTuiConfig.h"

/* ========================================================================== */
/*                        XPrintf ??????                                */
/* ========================================================================== */
/* XPRINTF_UTF8_CONSOLE = 1  ??????UTF-8?????UTF-8?Windows?????
 *                          - ???UTF-8?/utf-8????????UTF-8?????
 *                          - ?lwIP?SetConsoleOutputCP(CP_UTF8)??
 *                          - ????XPrintf???????????
 * XPRINTF_UTF8_CONSOLE = 0  ???????(GBK)???????GBK???????
 *                          - ??XChar_utf8ToGbkStream?UTF-8->GBK??
 *                          - ???????????GBK???
 * ???Linux/macOS??????UTF-8??????Windows */
#define XPRINTF_UTF8_CONSOLE			1

/* ========================================================================== */
/*                        ??????                                        */
/* ========================================================================== */
#define XBase64_ON						1
#define	XAbstractNetIoRing_ON					1

/* ========================================================================== */
/*                        ???? ??????                                */
/* ========================================================================== */
#ifndef TryPostEvent_QueueSize
#if XPLATFORM_DESKTOP
#define TryPostEvent_QueueSize      512 
#else
#define TryPostEvent_QueueSize      64   /* ?????????RAM */
#endif
#endif

/* ========================================================================== */
/*                        ??/?????                                      */
/* ========================================================================== */
#define IS_ON_DEBUG(on)						ISNULL(on,"???????"#on",?CXinYueConfig.h")

/** @brief 输出带文件、函数和行号上下文的格式化日志。 */
int XPrintf_context(const char* label, const char* file, const char* function,
                    int line, const char* format, ...);

//??debug??????
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

//??????????
#if ((XERROR_ON))
#define XERROR_PRINTF(...) XPrintf_context("XError", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define XERROR_PRINTF(...)
#endif

#ifdef __cplusplus
}
#endif
#endif
