#ifndef XTYPES_H
#define XTYPES_H
#ifdef __cplusplus
extern "C" {
#endif
typedef  void* XHandle;//句柄
typedef struct XPLCTask XPLCTask;
typedef struct XObject XObject; 
typedef struct XSetBase XSetBase;
typedef struct XEventMin XEventMin; 
typedef struct XThread XThread;
typedef struct XEventDispatcherThread XEventDispatcherThread;
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
typedef struct XPair XPair;
typedef struct XString XString;
typedef struct XListBase XListBase;
typedef struct XVector XVector;
typedef struct XByteArray XByteArray;
typedef struct XTimerGroupWheel XTimerGroupWheel;
typedef struct XQueueBase XQueueBase;
typedef struct XTimerBase XTimerBase;
typedef struct XEventDispatcher XEventDispatcher;
typedef struct XTimerGroupBase XTimerGroupBase;
typedef struct XDataFrameComm  XDataFrameComm;
typedef struct XIODeviceBase  XIODeviceBase;
typedef struct XCircularQueueAtomic XCircularQueueAtomic;
typedef struct XMutex XMutex;
typedef struct XSocket XSocket;
typedef struct XStack XStack;
typedef struct XPoint XPoint;
typedef struct XVariant XVariant;
typedef struct XSerialPortBase XSerialPortBase;
typedef XMapBase XFuncCodeMap;

#ifdef __cplusplus
}
#endif
#endif // 
