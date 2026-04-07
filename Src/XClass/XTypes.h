#ifndef XTYPES_H
#define XTYPES_H
#ifdef __cplusplus
extern "C" {
#endif
typedef  void* XHandle;//句柄
typedef void* ptr;//指针类型
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
typedef struct XTimerGroupWheel XTimerGroupWheel;
typedef struct XQueueBase XQueueBase;
typedef struct XTimerBase XTimerBase;
typedef struct XTimerTimeWheel XTimerTimeWheel;
typedef struct XEventDispatcher XEventDispatcher;
typedef struct XTimerGroupBase XTimerGroupBase;
typedef struct XDataFrameComm  XDataFrameComm;
typedef struct XIODevice  XIODevice;
typedef struct XCircularQueueAtomic XCircularQueueAtomic;
typedef struct XMutex XMutex;
typedef struct XWaitCondition XWaitCondition;
typedef struct XSocket XSocket;
typedef struct XStack XStack;
typedef struct XPoint XPoint;
typedef struct XBitArray XBitArray;
typedef struct XVariant XVariant;
typedef struct XSerialPortBase XSerialPortBase;
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
typedef XMapBase XFuncCodeMap;
typedef XMap XVariantMap;
typedef XHashMap XVariantHashMap;
#ifdef __cplusplus
}
#endif
#endif // 
