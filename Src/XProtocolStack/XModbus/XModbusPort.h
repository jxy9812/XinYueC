/*
 * FreeModbus库：裸机端口头文件（平台无关层）
 * Copyright (C) 2006 Christian Walter <wolti@sil.at>
 * 遵循GNU Lesser General Public License 2.1或更高版本
 *
 * 功能：定义平台相关的基础类型、宏定义及跨平台适配接口，
 *       确保Modbus协议栈在不同嵌入式平台上的兼容性
 */

 #ifndef XMODBUSPORT_H
 #define XMODBUSPORT_H
 #ifdef __cplusplus
 extern "C" {
 #endif
 /* ----------------------- 标准库及平台头文件 -----------------------------*/
 #include <assert.h>       // 断言头文件（用于调试）
 #include <inttypes.h>     // 整数类型别名头文件（如uint8_t等）
 //#include "system.h"       // 平台相关系统头文件（包含时钟、中断等定义）
 
 /* ----------------------- 跨语言及内联支持 -----------------------------*/
 #define INLINE                      inline         // 内联函数声明宏（优化代码体积）
 #define PR_BEGIN_EXTERN_C           extern "C" {   // C++兼容声明开始
 #define PR_END_EXTERN_C             }              // C++兼容声明结束
 
 /* ----------------------- 临界区操作宏（嵌入式系统关键）-------------------*/
 #define ENTER_CRITICAL_SECTION( )  //INTX_DISABLE() // 进入临界区：关闭中断
 #define EXIT_CRITICAL_SECTION( )   //INTX_ENABLE()  // 退出临界区：开启中断
 
 /* ----------------------- 基础数据类型重定义 -----------------------------*/
 // 布尔类型（1=TRUE, 0=FALSE）

 //typedef uint8_t BOOL;
 
 // 无符号字符型（1字节）
 typedef unsigned char UCHAR;
 // 有符号字符型（1字节）
 typedef char CHAR;
 
 // 无符号短整型（2字节）
 typedef uint16_t USHORT;
 // 有符号短整型（2字节）
 typedef int16_t SHORT;
 
 // 无符号长整型（4字节）
 typedef uint32_t ULONG;
 // 有符号长整型（4字节）
 typedef int32_t LONG;
 
 /* ----------------------- 布尔常量定义 ---------------------------------*/
 #ifndef TRUE
 #define TRUE            1   // 布尔真常量
 #endif
 
 #ifndef FALSE
 #define FALSE           0   // 布尔假常量
 #endif
 
#ifdef __cplusplus
}
#endif
#endif  // _PORT_H