#include "XModbusFrame.h"
#include "XByteArray.h"
#include "XCircularQueueAtomic.h"
#include "XQueue.h"
#include "XMemory.h"
#include "XCrc.h"
#include <string.h>
XModbusFrame* XModbusFrame_create(XModbusMode mode)
{
	XModbusFrame* frame=XMemory_malloc(sizeof(XModbusFrame));
	if (frame == NULL)
		return NULL;
	frame->mode = XMB_NOT_MODE;
	frame->frameData = NULL;
	frame->data = NULL;
	XModbusFrame_setMode(frame,mode);
	return frame;
}
XModbusFrame* XModbusFrame_copy(XModbusFrame* frame)
{
	if(frame==NULL)
		return NULL;
	XModbusFrame* newFrame = NULL;
	newFrame = XModbusFrame_create(frame->mode);
	if (frame->frameData!=NULL)
	{
		newFrame->frameData =XByteArray_create(0);
		XVector_copy_base(newFrame->frameData, frame->frameData);
	}
	newFrame->mode = frame->mode;
	switch (frame->mode)
	{
	case XMB_RTU_MASTER:
	case XMB_RTU_SLAVE:memcpy(newFrame->data, frame->data, sizeof(XModbusFrameRTU)); break;
	case XMB_NOT_MODE:printf("设置了未知模式\n");  break;
	default:
		break;
	}
	return newFrame;
}
void XModbusFrame_setMode(XModbusFrame* frame, XModbusMode mode)
{
	if (frame == NULL)
		return;
	if (frame->data)
	{
		switch (frame->mode)
		{

		case XMB_RTU_MASTER:
		case XMB_RTU_SLAVE:XModbusFrameRTU_delete(frame->data); break;
		case XMB_NOT_MODE:printf("内存没释放\n"); break;
		default:
			break;
		}
	}
	switch (mode)
	{

	case XMB_RTU_MASTER:
	case XMB_RTU_SLAVE:frame->data=XModbusFrameRTU_create(); break;
	case XMB_NOT_MODE:printf("设置了未知模式\n");  break;
	default:
		break;
	}
	frame->mode = mode;
}
void XModbusFrame_delete(XModbusFrame* frame)
{
	if (frame)
	{
		if (frame->frameData)
		{
			XVector_delete_base(frame->frameData);
		}
		if (frame->data)
		{
			switch (frame->mode)
			{
			
			case XMB_RTU_MASTER:
			case XMB_RTU_SLAVE:XModbusFrameRTU_delete(frame->data); break;
			case XMB_NOT_MODE:printf("内存没释放\n"); break;
			default:
				break;
			}
		}
		XMemory_free(frame);
	}
}
uint8_t XModbusFrame_getAddress(XModbusFrame* frame)
{
	if (frame)
	{
		switch (frame->mode)
		{
		case XMB_RTU_MASTER:
		case XMB_RTU_SLAVE:
			if (frame->data)
				return ((XModbusFrameRTU*)frame->data)->address;
			else
				return XModbusFrameRTU_parseAddress(frame); break;
		default:
			break;
		}
	}
	return 0;
}
uint8_t XModbusFrame_getFuncCode(XModbusFrame* frame)
{
	if (frame)
	{
		switch (frame->mode)
		{
		case XMB_RTU_MASTER:
		case XMB_RTU_SLAVE:
			if (frame->data)
				return ((XModbusFrameRTU*)frame->data)->funcCode;
			else
				return XModbusFrameRTU_parseFuncCode(frame); break;
		default:
			break;
		}
	}
	return 0;
}
bool XModbusFrame_parseData(XModbusFrame* frame, XByteArray* frameData)
{
	if(frame==NULL||frameData==NULL||frame->mode== XMB_NOT_MODE)
		return false;
	switch (frame->mode)
	{
	case XMB_RTU_MASTER:return XModbusFrameRTU_parseData_reply(frame->data, frameData);
	case XMB_RTU_SLAVE:return XModbusFrameRTU_parseData_request(frame->data, frameData);
	default:
		break;
	}
	return false;
}