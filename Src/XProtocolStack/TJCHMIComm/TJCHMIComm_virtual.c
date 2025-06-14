#include"TJCHMIComm.h"

static void VXDataFrameComm_setRecvValidCRC16(TJCHMIComm* comm, bool enableCRC16);//接收验证数据使用CRC16，小端添加在数据末尾帧尾前
static void VXDataFrameComm_setSendValidCRC16(TJCHMIComm* comm, bool enableCRC16);//发送数据添加验证用CRC16，小端添加在数据末尾帧尾前
XVtable* TJCHMIComm_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(TJCHMIComm))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XDataFrameComm_class_init());
	//void* table[] =
	//{
	//	VXDataFrameComm_SendFrameFSM,VXDataFrameComm_RecvFrameFSM,
	//	VXDataFrameComm_setCommMode,VXDataFrameComm_setFrameEndType,
	//	VXDataFrameComm_sendData,VXDataFrameComm_sendPeriodicData,
	//	VXDataFrameComm_removePeriodicSendData
	//};
	////追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_SetRecvValidCRC16, VXDataFrameComm_setRecvValidCRC16);
	XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_SetSendValidCRC16, VXDataFrameComm_setSendValidCRC16);
	//XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Disconnect, VXCommunicatorBase_disconnect);
#if SHOWCONTAINERSIZE
	printf("TJCHMIComm size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXDataFrameComm_setRecvValidCRC16(TJCHMIComm* comm, bool enableCRC16)
{
	XVtableGetFunc(XDataFrameComm_class_init(), EXDataFrameComm_SetRecvValidCRC16, void(*)(XDataFrameComm*, bool))(comm, enableCRC16);
	if (enableCRC16)
	{
		uint8_t recvFrameTail[] = { 0x01, 0xFE,0xFE,0xFE };
		XDataFrameComm_setRecvFrameTail(comm, recvFrameTail, sizeof(recvFrameTail));
	}
	else
	{
		uint8_t recvFrameTail[] = { 0xFF,0xFF,0xFF };
		XDataFrameComm_setRecvFrameTail(comm, recvFrameTail, sizeof(recvFrameTail));
	}
}

void VXDataFrameComm_setSendValidCRC16(TJCHMIComm* comm, bool enableCRC16)
{
	XVtableGetFunc(XDataFrameComm_class_init(), EXDataFrameComm_SetSendValidCRC16, void(*)(XDataFrameComm*, bool))(comm, enableCRC16);
	if (enableCRC16)
	{
		uint8_t sendFrameTail[] = { 0x01, 0xFE,0xFE,0xFE };
		XDataFrameComm_setSendFrameTail(comm, sendFrameTail, sizeof(sendFrameTail));
	}
	else
	{
		uint8_t sendFrameTail[] = { 0xFF,0xFF,0xFF };
		XDataFrameComm_setSendFrameTail(comm, sendFrameTail, sizeof(sendFrameTail));
	}
}
