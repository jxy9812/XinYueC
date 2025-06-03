#ifndef XMODBUSREGULARLYSENDFRAME_H
#define XMODBUSREGULARLYSENDFRAME_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include"XListSLinked.h"
typedef struct XModbusFrame XModbusFrame;
typedef XListSLinked XModbusRegularlySendFrameLsit;
typedef struct XModbusRegularlySendFrame
{
	uint32_t time;//定时时间
	size_t timeOut;//超时时间
	XModbusFrame* frame;//帧数据
}XModbusRegularlySendFrame;
XModbusRegularlySendFrameLsit* XModbusRegularlySendFrameList_new();
#ifdef __cplusplus
}
#endif
#endif // !XModbusRegularlySendFrame_H
