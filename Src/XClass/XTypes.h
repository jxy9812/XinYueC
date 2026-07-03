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
typedef struct XAtomic_int32_t XAtomic_int32_t;
typedef struct XAbstractNativeEventFilter XAbstractNativeEventFilter;
typedef struct XVarList XVarList;
typedef XMapBase XFuncCodeMap;
typedef XMap XVariantMap;
typedef XHashMap XVariantHashMap;

typedef void (*XCallableToRun)(XVarList*);
//函数参数类型
typedef enum XFuncParamType
{
	XFuncParamType_Copy,//拷贝模式
	XFuncParamType_Move,//移动模式
	XFuncParamType_Ref//引用模式
}XFuncParamType;
//函数返回类型
typedef enum XFuncReturnType
{
	XFuncReturnType_Copy,//拷贝模式
	XFuncReturnType_Move,//移动模式
	XFuncReturnType_Ref//引用模式
}XFuncReturnType;
/**
 * @brief 定时器唯一标识符。
 */
typedef intptr_t XTimerId;
#define  XTIMER_INVALID_ID  ((XTimerId)(-1))
/**
 * @brief 时间持续量（Duration）。
 */
typedef int64_t XDuration;

typedef enum XTimerType {
	XTimerType_PreciseTimer,   ///< 精确定时器（高精度，高功耗）
	XTimerType_CoarseTimer,   ///< 粗略定时器（允许 ±5% 误差，节能）
	XTimerType_VeryCoarseTimer// 仅保持完整的秒级精度。
} XTimerType;
/**
 * @brief 查找子对象的选项（对应 Qt::FindChildOption）
 *
 * 用于控制 QObject::findChild() / findChildren() 的查找行为。
 */
typedef enum XFindChildOption {
	XFindDirectChildrenOnly = 0x0,  /**< 仅查找直接子对象（不递归） */
	XFindChildrenRecursively = 0x1  /**< 递归查找所有后代子对象（默认行为） */
}XFindChildOption;
//套接字活动类型
typedef enum {
	XSocketAct_Invalid = 0,
	XSocketAct_Read = 1,
	XSocketAct_Write = 2,
	XSocketAct_ReadWrite = XSocketAct_Read | XSocketAct_Write,
	XSocketAct_Connect = 4,       ///< 连接完成（TCP）
	XSocketAct_Accept=8,
	XSocketAct_Exception = 16
} XSocketActType;

/**
 * @brief 锁类型枚举
 */
typedef enum
{
	XLock_NonRecursive = 1,			//非递归模式-默认等待中会休眠
	XLock_Spin = 2,					//自旋模式-等于XLock_SpinNonRecursive
	XLock_SpinNonRecursive = 3,		//自旋非递归模式
	XLock_Recursive = 4,			//递归模式-默认等待中会休眠
	XLock_SpinRecursive = 6			//自旋递归模式
} XLock_Type;
#ifdef __cplusplus
}
#endif
#endif // 
