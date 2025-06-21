#ifndef XMODBUSREGULARLYSENDFRAME_H
#define XMODBUSREGULARLYSENDFRAME_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include"XListSLinked.h"
typedef struct XModbusFrame XModbusFrame;
typedef XListSLinked XModbusRegularlySendFrameLsit;
typedef struct XModbus XModbus;
typedef struct XTimerBase XTimerBase;
typedef struct XModbusRegularlySendFrame
{
	uint32_t time;//定时时间
	size_t timeOut;//超时时间
	XModbusFrame* frame;//帧数据
	XModbus* modbus;//
	XTimerBase* timer;
}XModbusRegularlySendFrame;
XModbusRegularlySendFrameLsit* XModbusRegularlySendFrameList_create();
#ifdef __cplusplus
}
#endif
#endif // !XModbusRegularlySendFrame_H
