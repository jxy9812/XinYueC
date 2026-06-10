#ifndef XPLC_H
#define XPLC_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XObject.h"
#define XPLC_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XPLC))       //XPLC虚函数表大小
//XPLC虚函数表枚举XCLASS_DEFINE_BEGING(XPLC)
XCLASS_DEFINE_BEGING(XPLC)
XCLASS_DEFINE_ENUM(XPLC, AddOutIODevice) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XPLC, AddInIODevice),
XCLASS_DEFINE_ENUM(XPLC, RemoveOutId),
XCLASS_DEFINE_ENUM(XPLC, RemoveInId),
XCLASS_DEFINE_ENUM(XPLC, RemoveIODevice),
XCLASS_DEFINE_END(XPLC)
typedef struct XPLC
{
	XObject m_class;//继承类
	XMapBase* m_outIO;//输出
	XMapBase* m_inIO;//输入
	XQueueBase* m_taskQueue;//任务队列
	void(*m_delay_ms_cb)(size_t delay_ms);//延迟毫秒的回调函数
	uint16_t m_delay_ms;//延迟毫秒数/扫描周期
}XPLC;
XVtable* XPLC_class_init();
XPLC* XPLC_create();
void XPLC_init(XPLC* plc);
bool XPLC_addOutIODevice_base(XPLC* plc,int32_t id,XIODevice* io);
bool XPLC_addInIODevice_base(XPLC* plc, int32_t id, XIODevice* io);
bool XPLC_removeOutId_base(XPLC* plc, int32_t id);
bool XPLC_removeInId_base(XPLC* plc, int32_t id);
bool XPLC_removeIODevice_base(XPLC* plc,XIODevice* io);

//设置扫描周期
void XPLC_setScanPeriod(XPLC* plc, uint16_t delay_ms);
int32_t XPLC_getScanPeriod(XPLC* plc);
//设置延迟回调函数
void XPLC_setDelayMsCb(XPLC* plc, void(*delay_ms_cb)(size_t));
#ifdef __cplusplus
}
#endif
#endif // !XPLC_H
