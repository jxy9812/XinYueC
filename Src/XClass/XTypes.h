/**
 * @file       XTypes.h
 * @brief      公共基础类型定义(跨模块共享的前向声明、别名与公共枚举)。
 * @details    对齐 Qt 公共类型:XTimerType 对齐 Qt::TimerType,
 *             XFindChildOption 对齐 Qt::FindChildOption,XSocketActType 对齐
 *             QSocketNotifier::Type(允许按位组合)。以上类型均为 XinYueC
 *             库内类型,不依赖 Win32、POSIX、Qt 等其他平台 API。
 */
#ifndef XTYPES_H
#define XTYPES_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
typedef  void* XHandle;//句柄
typedef struct XEpoll XEpoll;
typedef struct XTimer XTimer;
typedef struct XClass XClass;
typedef struct XSemaphore XSemaphore;
typedef struct XPLCTask XPLCTask;
typedef struct XTreeNode XTreeNode;
typedef struct XObject XObject; 
typedef struct XSetBase XSetBase;
typedef struct XAction XAction;
typedef struct XMenu XMenu;
typedef struct XSet XSet;
typedef struct XEvent XEvent; 
typedef struct XThread XThread;
typedef struct XModbusDigitalSwitch XModbusDigitalSwitch; 
typedef struct XSwitchDeviceModbus XSwitchDeviceModbus;
typedef struct XModbus XModbus;
typedef struct XModbusFrame XModbusFrame;
typedef struct XStringList XStringList;
typedef struct XStepMotor XStepMotor;
typedef struct XPWMDeviceBase XPWMDeviceBase;
typedef struct XSwitchDeviceBase XSwitchDeviceBase;
typedef struct XMapBase XMapBase;
typedef struct XTJCHMIComm XTJCHMIComm;
typedef struct XEventLoop  XEventLoop;
typedef struct XPair XPair;
typedef struct XMap XMap;
typedef struct XPriorityQueue XPriorityQueue;
typedef struct XPriorityMapQueue XPriorityMapQueue;
typedef struct XAbstractEventDispatcher XAbstractEventDispatcher;
typedef struct XHashMap XHashMap;
typedef struct XString XString;
typedef struct XListBase XListBase;
typedef struct XVector XVector;
typedef struct XByteArray XByteArray;
typedef struct XTimeWheelGroup XTimeWheelGroup;
typedef struct XQueueBase XQueueBase;
typedef struct XTimer XTimer;
typedef struct XEventDispatcher XEventDispatcher;
typedef struct XTimerGroupBase XTimerGroupBase;
typedef struct XDataFrameComm  XDataFrameComm;
typedef struct XIODevice  XIODevice;
typedef struct XLockFreeQueue XLockFreeQueue;
typedef struct XMutex XMutex;
typedef struct XWaitCondition XWaitCondition;
typedef struct XSocket XSocket;
typedef struct XStack XStack;
typedef struct XPoint XPoint;
typedef struct XBitArray XBitArray;
typedef struct XVariant XVariant;
typedef struct XVariantList XVariantList;
typedef struct XJsonArray XJsonArray;
typedef struct XJsonDocument XJsonDocument;
typedef struct XJsonObject XJsonObject;
typedef struct XJsonValue XJsonValue;
typedef struct XBsonArray XBsonArray;
typedef struct XBsonDocument XBsonDocument;
typedef struct XBsonValue XBsonValue;
typedef struct XSignalSlot XSignalSlot;
typedef struct XAtomic_Int32 XAtomic_Int32;
typedef struct XAbstractNativeEventFilter XAbstractNativeEventFilter;
typedef struct XVarList XVarList;
typedef XMapBase XFuncCodeMap;       /**< 功能码映射：以功能码为键的语义化容器别名。 */
typedef XMap XVariantMap;            /**< Variant 键值映射：键与值均为 XVariant 的语义化容器别名。 */
typedef XHashMap XVariantHashMap;    /**< Variant 哈希映射：键与值均为 XVariant 的语义化容器别名。 */

typedef intptr_t XFd;               /**< 文件描述符/套接字句柄 */
#define XFD_INVALID  ((XFd)(-1))   /**< 无效句柄 */

typedef void (*XCallableToRun)(XVarList*);   /**< 可调用回调函数指针：形式为 void(XVarList*)，参数列表承载实参 */
/**
 * @brief 函数参数的传递方式
 */
typedef enum XFuncParamType
{
	XFuncParamType_Copy,//拷贝传递
	XFuncParamType_Move,//移动传递
	XFuncParamType_Ref//引用传递
}XFuncParamType;
/**
 * @brief 返回值的传递方式
 */
typedef enum XFuncReturnType
{
	XFuncReturnType_Copy,//拷贝传递
	XFuncReturnType_Move,//移动传递
	XFuncReturnType_Ref//引用传递
}XFuncReturnType;
/**
 * @brief 定时器标识符类型
 */
typedef XFd XTimerId;
#define  XTIMER_INVALID_ID  ((XTimerId)(-1))   /**< 无效定时器标识符 */
/**
 * @brief 时长类型(Duration)。
 */
typedef int64_t XDuration;

/**
 * @brief 定时器精度类型
 */
typedef enum XTimerType {
	XTimerType_PreciseTimer,   ///< 精确计时(毫秒级精度,较慢)
	XTimerType_CoarseTimer,   ///< 粗糙计时(允许约 5% 误差,较快)
	XTimerType_VeryCoarseTimer// 非常粗糙计时(秒级精度)
} XTimerType;
/**
 * @brief 子对象查找选项(对齐 Qt::FindChildOption)
 *
 * 供 QObject::findChild() / findChildren() 一类的查找接口选用
 */
typedef enum XFindChildOption {
	XFindChildOption_DirectChildrenOnly = 0x0,  /**< 仅查找直接子对象(不递归) */
	XFindChildOption_ChildrenRecursively = 0x1  /**< 连同全部后代一起查找(递归) */
}XFindChildOption;
/**
 * @brief 套接字活动类型
 * @details 值可按位组合,一次监听可用按位或/组合多种活动兴趣
 *          (如 ReadWrite 即 Read 与 Write 的按位或)。
 */
typedef enum {
	XSocketActType_Invalid = 0,       ///< 无效活动(0 表示未选择任何活动)
	XSocketActType_Read = 1,          ///< 读活动(值 0x1,可按位组合)
	XSocketActType_Write = 2,         ///< 写活动(值 0x2,可按位组合)
	XSocketActType_ReadWrite = XSocketActType_Read | XSocketActType_Write,   ///< 读写活动(Read|Write 组合为 0x3)
	XSocketActType_Connect = 4,       ///< 发起连接(TCP)
	XSocketActType_Accept=8,          ///< 监听到新连接待接受(TCP 服务端)
	XSocketActType_Exception = 16     ///< 异常活动(对应 QSocketNotifier::Exception)
} XSocketActType;

/**
 * @brief 锁的类型
 */
typedef enum
{
	XLock_NonRecursive = 1,			//非递归互斥锁-非自旋
	XLock_Spin = 2,					//自旋锁-参见XLock_SpinNonRecursive
	XLock_SpinNonRecursive = 3,		//非递归自旋锁
	XLock_Recursive = 4,			//递归锁-同一线程可重入
	XLock_SpinRecursive = 6			//递归自旋锁
} XLock_Type;
#ifdef __cplusplus
}
#endif
#endif // 
