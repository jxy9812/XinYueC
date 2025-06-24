#ifndef XPLC_H
#define XPLC_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#define XPLC_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XPLC))       //XPLC虚函数表大小
//XPLC虚函数表枚举XCLASS_DEFINE_BEGING(XPLC)
XCLASS_DEFINE_BEGING(XPLC)
XCLASS_DEFINE_ENUM(XPLC, AddOutIODevice) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XPLC, AddInIODevice),
XCLASS_DEFINE_ENUM(XPLC, RemoveOutId),
XCLASS_DEFINE_ENUM(XPLC, RemoveInId),
XCLASS_DEFINE_ENUM(XPLC, RemoveIODevice),
XCLASS_DEFINE_ENUM(XPLC, Poll),
XCLASS_DEFINE_END(XPLC)
typedef struct XPLC
{
	XClass m_parent;//继承类
	XMapBase* m_outIO;//输出
	XMapBase* m_inIO;//输入
	XQueueBase* m_taskQueue;//任务队列
	XIOCallbackQueue* m_callbackQueue;//回调队列 统一处理io设备的回调函数
}XPLC;
XVtable* XPLC_class_init();
XPLC* XPLC_create();
void XPLC_init(XPLC* plc);
bool XPLC_addOutIODevice_base(XPLC* plc,int32_t id,XIODeviceBase* io);
bool XPLC_addInIODevice_base(XPLC* plc, int32_t id, XIODeviceBase* io);
bool XPLC_removeOutId_base(XPLC* plc, int32_t id);
bool XPLC_removeInId_base(XPLC* plc, int32_t id);
bool XPLC_removeIODevice_base(XPLC* plc,XIODeviceBase* io);
void XPLC_poll_base(XPLC* plc);

//设置回调队列
void XPLC_setCallbackQueue(XPLC* plc, XIOCallbackQueue* queue);
#ifdef __cplusplus
}
#endif
#endif // !XPLC_H
