/**
 * @file XCharFreeRTOS.c
 * @brief GBK编码转换的FreeRTOS平台实现（占位/提示）
 *
 * FreeRTOS嵌入式平台通常没有系统级API支持GBK编码转换。
 * 请使用文件模式(XCHAR_USE_FILE_GBK)或代码模式(XCHAR_USE_CODE_GBK)代替。
 *
 * 三种模式优先级（从高到低）：
 *   1. XCHAR_USE_CODE_GBK   - 代码模式（静态数组）
 *   2. XCHAR_USE_FILE_GBK   - 文件模式（读取外部文件）
 *   3. XCHAR_USE_SYSTEM_GBK - 系统API模式（FreeRTOS不支持）
 */

#ifdef __FreeRTOS__
#include "XChar_conf.h"

/* FreeRTOS不支持系统API模式，仅当用户错误地启用了系统API模式时给出提示 */
#if defined(XCHAR_USE_SYSTEM_GBK) && (!defined(XCHAR_USE_FILE_GBK)) && (!defined(XCHAR_USE_CODE_GBK))

#error "XChar: FreeRTOS平台不支持XCHAR_USE_SYSTEM_GBK模式，请使用XCHAR_USE_FILE_GBK或XCHAR_USE_CODE_GBK"

#endif /* XCHAR_USE_SYSTEM_GBK && (!XCHAR_USE_FILE_GBK) && (!XCHAR_USE_CODE_GBK) */
#endif /* __FreeRTOS__ */
