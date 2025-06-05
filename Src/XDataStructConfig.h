#ifndef XDATASTRUCTCONFIG_H
#define XDATASTRUCTCONFIG_H
#ifdef __cplusplus
extern "C" {
#endif
#define IS_BIG_ENDIAN                   0 //当前是大端吗     默认是小端
//数据结构配置文件
#define VTABLE_ISSTACK					1//虚函数表定义在栈上
#define SHOWCONTAINERSIZE				0//显示容器大小
#define DEBUG_ON						0
#define DEMOTEST						1//测试代码

#define XContainerObject_ON				1
#define XMap_ON							1
#define XHashMap_ON							1
#define XString_ON						1
#define	XPriorityQueue_ON				1
#define	XQueue_ON						1
#define	XList_ON						1
#define	XListDLinked_ON					1
#define	XListSLinked_ON					1
#define	XStack_ON						1
#define	XVector_ON						1
#define	XVectorTwo_ON					1
#define	XStringVector_ON				1
#define	XCircularQueue_ON				1
#define	XCircularQueueAtomic_ON			1

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
#if !XContainerObject_ON					
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



#define IS_ON_DEBUG(on)						ISNULL(on,"此函数需要开启"#on",在XDataStructConfig.h")

#ifdef DEBUG_ON
#if ((DEBUG_ON) && defined(_DEBUG))
#define DEBUG_PRINTF(fmt,...) printf("[FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt,...)
#endif
#else
#if defined _DEBUG
#define DEBUG_PRINTF(fmt,...) printf("[FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt,...)
#endif
#endif // !DEBUG_ON

#ifdef __cplusplus
}
#endif
#endif