#include "XModbusDigitalSwitch.h"
#include "XMemory.h"
#include "XModbus.h"
#include "XByteArray.h"
#include "XModbusFrame_RTU.h"
#include "XModbusFrame.h"
#include "XTimerBase.h"
#include "XSwitchDeviceBase.h"
#include <string.h>
typedef struct
{
	XDataFrameComm* comm;
	XByteArray* data;
	XTimerBase* timer;
}PeriodicNode;//定时发送的节点
//输出继电器查询状态回调
static void CoilsDisc_0x01_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* device);
//读取输出回调用来做重传
static void readOutHandCb(XModbusDigitalSwitch* ds);
//输入继电器查询状态回调
static void CoilsDisc_0x02_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* device);
//输入查询回调
static void inHandCb(XModbusDigitalSwitch* ds);
//关闭 输出轮询
static void stopOutPoll(XModbusDigitalSwitch* ds);
static void startOutPoll(XModbusDigitalSwitch* ds);
XModbusDigitalSwitch* XModbusDigitalSwitch_create(XModbus* modbus,uint8_t address, uint16_t inCount, uint16_t outCount)
{
	if (inCount == 0 && outCount == 0)
		return NULL;
	XModbusDigitalSwitch* group = XMemory_malloc(sizeof(XModbusDigitalSwitch));
	if (group == NULL)
		return NULL;
	memset(group,0,sizeof(XModbusDigitalSwitch));
	if (inCount > 0)
	{
		group->m_in = XModbusCoilsDisc_create(inCount);
		group->m_cmpIn =XByteArray_create(XContainerSize(group->m_in->parent.data));
	}
	else
	{
		group->m_in = NULL;
		group->m_cmpIn = NULL;
	}

	if (outCount > 0)
	{
		group->m_out = XModbusCoilsDisc_create(outCount);
		group->m_cmpOut = XModbusCoilsDisc_create(outCount);
	}
	else
	{
		group->m_out = NULL;
		group->m_cmpOut = NULL;
	}
	group->m_modbus = modbus;
	XModbusDigitalSwitch_setAddress(group,address);
	return group;
}
void XModbusDigitalSwitch_setAddress(XModbusDigitalSwitch* ds, uint8_t address)
{
	if (ds == NULL||ds->m_address==address)
		return;
	XModbus_removeRecvHand_AddressCode(ds->m_modbus,ds->m_address, MB_FUNC_READ_COILS);
	XModbus_removeRecvHand_AddressCode(ds->m_modbus, ds->m_address, MB_FUNC_READ_DISCRETE_INPUTS);
	switch (ds->m_modbus->m_mode)
	{
	case XMB_RTU_MASTER:
	{
		if (ds->m_out)
		{
			XModbus_addRecvHand_AddressCode(ds->m_modbus, address, MB_FUNC_READ_COILS, CoilsDisc_0x01_RTU_masterRecvHandCb, ds->m_out);
		}
		if (ds->m_in)
		{
			XModbus_addRecvHand_AddressCode(ds->m_modbus, address, MB_FUNC_READ_DISCRETE_INPUTS, CoilsDisc_0x02_RTU_masterRecvHandCb, ds->m_in);
		}
	}break;
	default:
		break;
	}
	
	ds->m_address = address;
}
void XModbusDigitalSwitch_delete(XModbusDigitalSwitch* ds)
{
	if (ds == NULL)
		return;
	if (ds->m_modbus)
	{
		if (ds->m_inHandle)
			XDataFrameComm_removePeriodicSendData_base(ds->m_modbus,ds->m_inHandle);
		if (ds->m_outHandle)
			XDataFrameComm_removePeriodicSendData_base(ds->m_modbus, ds->m_outHandle);
		XModbus_removeRecvHand_AddressCode(ds->m_modbus, ds->m_address, MB_FUNC_READ_COILS);
		XModbus_removeRecvHand_AddressCode(ds->m_modbus, ds->m_address, MB_FUNC_READ_DISCRETE_INPUTS);
	}
	if (ds->m_in)
		XModbusCoilsDisc_delete(ds->m_in);
	if (ds->m_cmpIn)
		XByteArray_delete_base(ds->m_cmpIn);
	if (ds->m_out)
		XModbusCoilsDisc_delete(ds->m_out);
	if (ds->m_cmpOut)
		XModbusCoilsDisc_delete(ds->m_cmpOut);
	if (ds->m_ioInList)
		XVector_delete_base(ds->m_ioInList);
	if (ds->m_ioOutList)
		XVector_delete_base(ds->m_ioOutList);
	XMemory_free(ds);
}
bool XModbusDigitalSwitch_setScanningPeriod(XModbusDigitalSwitch* ds, uint32_t time)
{
	if (ds == NULL )
		return false;
	if (ds->m_outHandle)
	{//先释放上次的
		XDataFrameComm_removePeriodicSendData_base(ds->m_modbus, ds->m_outHandle);
	}
	if (ds->m_inHandle)
	{//先释放上次的
		XDataFrameComm_removePeriodicSendData_base(ds->m_modbus, ds->m_inHandle);
	}
	if (time == 0)
		return false;
	if(ds->m_out)
	{
		XByteArray* data = XByteArray_create(0);
		if(ds->m_modbus->m_mode== XMB_RTU_MASTER)
			XModbusFrameRTU_setFrameData_0x01_request(data, ds->m_address,ds->m_outAddressOffset, ds->m_out->count);
		ds->m_outHandle = XModbus_sendDataPeriodicMaster_base(ds->m_modbus, data, time);
		XTimerBase_setAutoDelete(((PeriodicNode*)ds->m_outHandle)->timer, false);
		XModbusDeviceObject_setCallback(ds->m_out, readOutHandCb);
		XModbusDeviceObject_setUserData(ds->m_out, ds);
	}
	if (ds->m_in)
	{
		XByteArray* data = XByteArray_create(0);
		if (ds->m_modbus->m_mode == XMB_RTU_MASTER)
			XModbusFrameRTU_setFrameData_0x02_request(data, ds->m_address, ds->m_inAddressOffset, ds->m_in->count);
		ds->m_inHandle = XModbus_sendDataPeriodicMaster_base(ds->m_modbus, data, time);
		//XTimerBase_setAutoDelete(((PeriodicNode*)ds->m_inHandle)->timer, false);
		XModbusDeviceObject_setCallback(ds->m_in, inHandCb);
		XModbusDeviceObject_setUserData(ds->m_in, ds);
	}
	return true;
}
void XModbusDigitalSwitch_setOutTimerMode(XModbusDigitalSwitch* ds, XMB_DS_OutTimerMode mode)
{
	if (ds)
		ds->m_outTimerMode = mode;
}
bool XModbusDigitalSwitch_readIn(XModbusDigitalSwitch* ds, uint16_t portNum, bool* state)
{
	if (ds == NULL || ds->m_in == NULL || state == NULL)
		return false;
	*state = XModbusCoilsDisc_at(ds->m_in, portNum);
	return true;
}
bool XModbusDigitalSwitch_writeOut(XModbusDigitalSwitch* ds, uint16_t portNum, bool state)
{
	if (ds == NULL || ds->m_in == NULL )
		return false;
	XModbusCoilsDisc_write_bool(ds->m_cmpOut,portNum,state);
	//XByteArray* data = XByteArray_create(0);
	//XModbusFrameRTU_setFrameData_0x0F_request(data, ds->m_address, 0, ds->m_out->count, XContainerDataPtr(ds->m_cmpOut->parent.data));
	//XModbus_sendData_base(ds->m_modbus, data);
	if (ds->m_outTimerMode == XMB_DS_OutTimerMode_Write_Run)
		startOutPoll(ds);
}
bool XModbusDigitalSwitch_readOut(XModbusDigitalSwitch* ds, uint16_t portNum, bool *state)
{
	if (ds == NULL || ds->m_in == NULL || state == NULL)
		return false;
	*state = XModbusCoilsDisc_at(ds->m_out, portNum);
	return true;
}
bool XModbusDigitalSwitch_XSwitchDeviceModbusOpen(XModbusDigitalSwitch* ds, XSwitchDeviceModbus* sw, XIODeviceBaseMode mode, uint16_t portNum)
{
	if(ds==NULL||sw==NULL)
		return false;
	if (!(mode & XIODeviceBase_ReadOnly || mode & XIODeviceBase_WriteOnly))
		return false;
	if (mode & XIODeviceBase_ReadOnly)
	{
		if (portNum >= ds->m_in->count)
			return false;
		if (ds->m_ioInList == NULL)
		{
			ds->m_ioInList = XVector_Create(XSwitchDeviceModbus*);
			XVector_resize_base(ds->m_ioInList,ds->m_in->count);
		}
		XVector_At_Base(ds->m_ioInList,portNum, XSwitchDeviceModbus*)=sw;

	}
	else if (mode & XIODeviceBase_WriteOnly)
	{
		if (portNum >= ds->m_out->count)
			return false;
		if (ds->m_ioOutList == NULL)
		{
			ds->m_ioOutList = XVector_Create(XSwitchDeviceModbus*);
			XVector_resize_base(ds->m_ioOutList, ds->m_out->count);
		}
		XVector_At_Base(ds->m_ioOutList, portNum, XSwitchDeviceModbus*)=sw;
	}
}
void XModbusDigitalSwitch_XSwitchDeviceModbusClose(XModbusDigitalSwitch* ds, XSwitchDeviceModbus* sw, XIODeviceBaseMode mode, uint16_t portNum)
{
	if (ds == NULL || sw == NULL)
		return ;
	if (!(mode & XIODeviceBase_ReadOnly || mode & XIODeviceBase_WriteOnly))
		return ;
	if (mode & XIODeviceBase_ReadOnly)
	{
		if (portNum >= ds->m_in->count)
			return ;
		if (ds->m_ioInList == NULL)
			return;
		if(XVector_At_Base(ds->m_ioInList, portNum, XSwitchDeviceModbus*)==sw)
			XVector_At_Base(ds->m_ioInList, portNum, XSwitchDeviceModbus*) = NULL;

	}
	else if (mode & XIODeviceBase_WriteOnly)
	{
		if (portNum >= ds->m_out->count)
			return ;
		if (ds->m_ioOutList == NULL)
			return;
		if(XVector_At_Base(ds->m_ioOutList, portNum, XSwitchDeviceModbus*) == sw)
			XVector_At_Base(ds->m_ioOutList, portNum, XSwitchDeviceModbus*) = NULL;
	}
}
void CoilsDisc_0x01_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* device)
{
	if (modbus == NULL || recvFrame == NULL || device == NULL)
		return;
	//printf("处理主站接收信息\n");
	XModbusCoilsDisc* coilsFunc = device;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	XModbusDigitalSwitch* ds = device->userData;
	if (rtu == NULL)
		return;
	if (rtu->data != NULL)
	{
		if (XModbusCoilsDisc_write(coilsFunc, 0x00, ds->m_out->count, XContainerDataPtr(rtu->data)) && device->cb != NULL)
		    device->cb(device->userData);
	}
}
void readOutHandCb(XModbusDigitalSwitch* ds)
{
	//printf("写入成功\n");
	if (memcmp(XContainerDataPtr(ds->m_out->parent.data), XContainerDataPtr(ds->m_cmpOut->parent.data), XContainerSize(ds->m_cmpOut->parent.data)) != 0)
	{//不相等
		XByteArray* data = XByteArray_create(0);
		XModbusFrameRTU_setFrameData_0x0F_request(data,ds->m_address, ds->m_outAddressOffset,ds->m_out->count, XContainerDataPtr(ds->m_cmpOut->parent.data));
		XModbus_sendData_base(ds->m_modbus, data);
		//printf("不相等重发\n");
	}
	else if(ds->m_outTimerMode== XMB_DS_OutTimerMode_Write_Run)
	{//相等关闭采集
		stopOutPoll(ds);

	}
}

void CoilsDisc_0x02_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* device)
{
	if (modbus == NULL || recvFrame == NULL || device == NULL)
		return;
	XModbusCoilsDisc* coilsFunc = device;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	XModbusDigitalSwitch* ds = device->userData;
	if (rtu == NULL)
		return;
	if (rtu->data != NULL)
	{
		if (XModbusCoilsDisc_write(coilsFunc,0x00, ds->m_in->count, XContainerDataPtr(rtu->data)) && device->cb != NULL)
			device->cb(device->userData);
	}
}

void inHandCb(XModbusDigitalSwitch* ds)
{
	if (memcmp(XContainerDataPtr(ds->m_in->parent.data), XContainerDataPtr(ds->m_cmpIn), XContainerSize(ds->m_cmpIn)) != 0)
	{//不相等 输入发生了变化
		XByteArray_copy_base(ds->m_cmpIn, ds->m_in->parent.data);
		//调用绑定的IO设备更新状态
		if (ds->m_ioInList)
		{
			XSwitchDeviceModbus* sw = NULL;
			for_each_iterator(ds->m_ioInList, XVector, it)
			{
				sw = *((XSwitchDeviceModbus**)XVector_iterator_data(&it));
				if(sw!=NULL)
					XSwitchDeviceBase_poll_base(sw);
			}
		}
		//printf("输入不相等\n");
	}
	else
	{//相等


	}
}

void stopOutPoll(XModbusDigitalSwitch* ds)
{
	if (ds == NULL|| ds->m_outHandle==NULL)
		return;
	PeriodicNode* node = ds->m_outHandle;
	//if (XTimerBase_isRunning(node->timer))
	{
		XTimerBase_stop_base(node->timer);
	}
}

void startOutPoll(XModbusDigitalSwitch* ds)
{
	if (ds == NULL || ds->m_outHandle == NULL)
		return;
	PeriodicNode* node = ds->m_outHandle;
	//if(!XTimerBase_isRunning(node->timer))
	{
		
		XTimerBase_start_base(node->timer);
	}
}
