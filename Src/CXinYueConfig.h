#ifndef CXINYUECONFIG_H
#define CXINYUECONFIG_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTimerConfig.h"
#define IS_BIG_ENDIAN                   0 //当前是大端吗     默认是小端
//数据结构配置文件
#define VTABLE_ISSTACK					1//虚函数表定义在栈上
#define SHOWCONTAINERSIZE				0//显示容器大小
#define DEBUG_ON						0
#define XERROR_ON						1//错误输出
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
/*                容器                              */
#define XContainer_ON				1
#define XSet_ON							1
#define XHashSet_ON						1
#define XMap_ON							1
#define XHashMap_ON						1
#define XString_ON						1
#define XVariant_ON						1
#define	XPriorityQueue_ON				1
#define	XQueue_ON						1
#define	XList_ON						1
#define	XListDLinked_ON					1
#define	XListSLinked_ON					1
#define	XLockFreeList_ON			1
#define	XStack_ON						1
#define	XVector_ON						1
#define	XByteArray_ON					1
#define	XVectorTwo_ON					1
#define	XStringList_ON					1
#define XVariantList_ON					1
#define	XCircularQueue_ON				1
#define	XLockFreeQueue_ON			1
#define XBitArray_ON					1
#define XRingChunk_ON					1
#define XRingBuffer_ON					1
/*                          算法                            */
#define XCrc_ON                         1
#define XCrc16_ON                       1
#define XCrc32_ON                       1
#define XBase64_ON						1
/*                          依赖                            */
#if !XList_ON
#define	XQueue_ON						0
#endif
#if !XVector_ON					
#define	XStack_ON						0
#define	XPriorityQueue_ON				0
#define	XString_ON						0
#define	XVectorTwo_ON					0
#define	XStringVector_ON				0
#endif
#if !XContainer_ON					
#define XMap_ON							0
#define XString_ON						0
#define	XPriorityQueue_ON				0
#define	XQueue_ON						0
#define	XList_ON						0
#define	XStack_ON						0
#define	XVector_ON						0
#define	XVectorTwo_ON					0
#define	XStringVector_ON				0
#endif
// 事件投递 无锁队列大小
#ifndef TryPostEvent_QueueSize
#if defined(_WIN32) || defined(_WIN64) || defined(__linux__) || defined(__APPLE__)
#define TryPostEvent_QueueSize      512 
#else
#define TryPostEvent_QueueSize      64   /* 适配嵌入式设备节省RAM */
#endif
#endif

#define IS_ON_DEBUG(on)						ISNULL(on,"此函数需要开启"#on",在CXinYueConfig.h")

//定义debug信息输出方式
#ifdef DEBUG_ON
#if ((DEBUG_ON) && defined(_DEBUG))
#define XDEBUG_PRINTF(fmt,...) XPrintf("[FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,__VA_ARGS__)
#else
#define XDEBUG_PRINTF(fmt,...)
#endif
#else
#if defined _DEBUG
#define XDEBUG_PRINTF(fmt,...) XPrintf("Debug [FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,__VA_ARGS__)
#else
#define XDEBUG_PRINTF(fmt,...)
#endif
#endif // !DEBUG_ON

//定义错误信息输出方式
#if ((XERROR_ON))
#define XERROR_PRINTF(fmt,...) XPrintf("XError [FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,__VA_ARGS__)
#else
#define XERROR_PRINTF(fmt,...)
#endif

#ifdef __cplusplus
}
#endif
#endif