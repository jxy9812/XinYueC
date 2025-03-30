#ifndef XDATASTRUCTCONFIG_H
#define XDATASTRUCTCONFIG_H
#ifdef __cplusplus
extern "C" {
#endif
//数据结构配置文件
#define VTABLEISSTACK					1//虚函数表定义在栈上
#define SHOWCONTAINERSIZE				0//显示容器大小
#define DEBUG_ON						0
#define DEMOTEST						1//测试代码

#define XMap_ON							1
#define XString_ON						1
#define	XPriority_Queue_ON				1
#define	XQueue_ON						1
#define	XList_ON						1
#define	XStack_ON						0


#if !XList_ON
#define	XQueue_ON						0
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