#include"XDataFrameCommunicator.h"

XVtable* XDataFrameCommunicator_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XDATAFRAMECOMMUNICATOR_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XCommunicatorBase_class_init());
	//void* table[] =
	//{
	//	VXModbusBase_sendFrame,VXModbusBase_recvFrame,VXModbusBase_ReceiveFSM,VXModbusBase_TransmitFSM
	//};
	////追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	/*XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Poll, VXModbusBase_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Connect, VXCommunicatorBase_connect);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Disconnect, VXCommunicatorBase_disconnect);*/
#if SHOWCONTAINERSIZE
	printf("XDataFrameCommunicator size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}