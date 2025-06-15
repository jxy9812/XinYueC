#include"TJCHMIComm.h"
#include"XVector.h"
#include"XTimerBase.h"
#include"XCrc.h"
#include<string.h>
static void XDataFrameComm_recvValid(XDataFrameComm* comm);
static void VXDataFrameComm_RecvFrameFSM(XDataFrameComm* comm);
static void VXDataFrameComm_setRecvValidCRC16(TJCHMIComm* comm, bool enableCRC16);//接收验证数据使用CRC16，小端添加在数据末尾帧尾前
static void VXDataFrameComm_setSendValidCRC16(TJCHMIComm* comm, bool enableCRC16);//发送数据添加验证用CRC16，小端添加在数据末尾帧尾前
static void VXDataFrameComm_setRecvFrameTail(TJCHMIComm* comm, const uint8_t* data, uint8_t dataSize);
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
	XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_RecvFrameFSM, VXDataFrameComm_RecvFrameFSM);
	XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_SetRecvFrameTail, VXDataFrameComm_setRecvFrameTail);
#if SHOWCONTAINERSIZE
	printf("TJCHMIComm size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void VXDataFrameComm_RecvFrameFSM(XDataFrameComm* comm)
{
	//printf("测试\n");
	if (XIODeviceBase_getBytesAvailable_base(((XCommunicatorBase*)comm)->m_io) == 0)
		return;//没有可以接收的
	uint8_t           ucByte;
	XIODeviceBase_read_base(comm->m_parent.m_io, &ucByte, 1);
	XVector* recvVector = comm->m_parent.m_recvAsyncBuffer;
	switch (comm->m_eRcvState)//if (mode == XDFC_FRAME_END_TIMEOUT)
	{
	case XDFC_STATE_RX_INIT:  // 初始状态（等待总线空闲）
	{
		if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
		{
			XTimerBase_start_base(comm->m_timerRecvExpired);
		}
		else
		{
			comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
			//发送一个事件
			XDataFrameComm_sendEvent(comm, XDFC_READY);
		}
		break;
	}
	case XDFC_STATE_RX_ERROR:  // 接收错误状态（忽略后续字节）
	{
		if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
		{
			XTimerBase_start_base(comm->m_timerRecvExpired);// 保持定时器运行，等待错误帧结束
			break;
		}
		else
		{
			//comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
		}

	}
	case XDFC_STATE_RX_IDLE:  // 空闲状态（接收到新帧起始字节）
	{
		XContainerSize(recvVector) = 0;  // 重置接收缓冲区位置
		XVector_push_back_base(recvVector, &ucByte);  // 存储第一个字节
		if (comm->m_recvFrameHead && !XVector_isEmpty_base(comm->m_recvFrameHead))
		{//存在接收帧头
			if (memcmp(XContainerDataPtr(recvVector), XContainerDataPtr(comm->m_recvFrameHead), 1) != 0)
			{//比较第一个
				return;//第一个就不一样 重新来过
			}
			if (XContainerSize(comm->m_recvFrameHead) == 1)
			{
				comm->m_eRcvState = XDFC_STATE_RX_RCV;//切换到接收数据中
			}
			else
			{
				comm->m_eRcvState = XDFC_STATE_RX_HEAD;//切换到接收帧头中
			}
		}
		else
		{
			comm->m_eRcvState = XDFC_STATE_RX_RCV;//切换到接收数据中
		}

		if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
			XTimerBase_start_base(comm->m_timerRecvExpired);
		break;
	}
	case XDFC_STATE_RX_HEAD:    // 接收帧头中
	{
		if (XContainerSize(recvVector) >= XContainerCapacity(recvVector))
		{
			comm->m_eRcvState = XDFC_STATE_RX_ERROR;  // 缓冲区溢出，标记错误状态
			XDataFrameComm_sendEvent(comm, XEventMin_create(XDFC_RX_BUFFER_OVERFLOW, 0));
			return;
		}
		XVector_push_back_base(recvVector, &ucByte);  // 存储字节到缓冲区
		size_t size = XContainerSize(recvVector);
		if (memcmp((uint8_t*)(XContainerDataPtr(recvVector)) + size - 1, ((uint8_t*)XContainerDataPtr(comm->m_recvFrameHead)) + size - 1, 1) != 0)
		{
			comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
			return;//校验失败重新开始
		}
		if (size == XContainerSize(comm->m_recvFrameHead))
		{//校验通过了
			XContainerSize(recvVector) = 0;//清空缓冲区用来存数据了
			comm->m_eRcvState = XDFC_STATE_RX_RCV;//切换到接收数据中
		}
		if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
			XTimerBase_start_base(comm->m_timerRecvExpired);
		break;
	}
	case XDFC_STATE_RX_RCV:  // 接收中状态（连续接收字节）
	{
		if (XContainerSize(recvVector) >= XContainerCapacity(recvVector))
		{
			comm->m_eRcvState = XDFC_STATE_RX_ERROR;  // 缓冲区溢出，标记错误状态
			XDataFrameComm_sendEvent(comm, XEventMin_create(XDFC_RX_BUFFER_OVERFLOW, 0));
			return;
		}
		XVector_push_back_base(recvVector, &ucByte);  // 存储字节到缓冲区

		if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
		{
			XTimerBase_start_base(comm->m_timerRecvExpired);
			return;
		}
		else if (comm->m_frameEndMode == XDFC_FRAME_END_MARKER)
		{//检测结束标志
			
			
			static uint8_t tail[] = {0xFF,0xFF,0xFF };
			if ((XContainerSize(recvVector) >= sizeof(tail)) && memcmp((uint8_t*)(XContainerDataPtr(recvVector)) + XContainerSize(recvVector) - sizeof(tail), tail, sizeof(tail)) == 0)
			{//检测到帧结束标志
				XContainerSize(recvVector) -= sizeof(tail);//缓冲区删除结束标志
				if (XContainerSize(recvVector) != 0)
				{
					XVector* v = XVector_Create(uint8_t);
					if (v == NULL)
						return;
					XVector_copy_base(v, comm->m_parent.m_recvAsyncBuffer);
					if (!XDataFrameComm_sendEvent(comm, XEventRecvFrame_create(XDFC_FRAME_RECEIVED, 0, v)))
					{
						XVector_delete_base(v);//释放数组防止内存泄露
					}
				}
				comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
				return ;
			}
			static uint8_t tailCrc[] = { 0x01, 0xFE,0xFE,0xFE };
			if ((XContainerSize(recvVector) >= sizeof(tailCrc)) && memcmp((uint8_t*)(XContainerDataPtr(recvVector)) + XContainerSize(recvVector) - sizeof(tailCrc), tailCrc, sizeof(tailCrc)) == 0)
			{//检测到帧结束标志
				XContainerSize(recvVector) -= sizeof(tailCrc);//缓冲区删除结束标志
				if (XContainerSize(recvVector) != 0 && XCrc_get16(XContainerDataPtr(recvVector), XContainerSize(recvVector)) == 0)
				{
					XVector* v = XVector_Create(uint8_t);
					if (v == NULL)
						return;
					XVector_copy_base(v, comm->m_parent.m_recvAsyncBuffer);
					if (!XDataFrameComm_sendEvent(comm, XEventRecvFrame_create(XDFC_FRAME_RECEIVED, 0, v)))
					{
						XVector_delete_base(v);//释放数组防止内存泄露
					}
				}
				comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
				return;
			}
			if (comm->m_recvFrameTail != NULL && !XVector_isEmpty_base(comm->m_recvFrameTail))
			{
				size_t size = XContainerSize(comm->m_recvFrameTail);
				if ((XContainerSize(recvVector) >= size) && memcmp((uint8_t*)(XContainerDataPtr(recvVector)) + XContainerSize(recvVector) - size, XContainerDataPtr(comm->m_recvFrameTail), size) == 0)
				{//检测到帧结束标志
					XContainerSize(recvVector) -= size;//缓冲区删除结束标志
					if(XContainerSize(recvVector) != 0)
						XDataFrameComm_recvValid(comm);
					comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
					return;
				}
			}
		}
		break;
	}
	}
}

void XDataFrameComm_recvValid(XDataFrameComm* comm)
{
	if (XVector_isEmpty_base(comm->m_parent.m_recvAsyncBuffer))
		return;//数据缓冲区是空的也就没必要继续了

	if (comm->m_recvValidCb != NULL && !comm->m_recvValidCb(comm->m_parent.m_recvAsyncBuffer))
		return;//校验没通过
	XVector* v = XVector_Create(uint8_t);
	if (v == NULL)
		return;
	XVector_copy_base(v, comm->m_parent.m_recvAsyncBuffer);

	if (!XDataFrameComm_sendEvent(comm, XEventRecvFrame_create(XDFC_FRAME_RECEIVED, 0, v)))
	{
		XVector_delete_base(v);//释放数组防止内存泄露
	}
}
void VXDataFrameComm_setRecvValidCRC16(TJCHMIComm* comm, bool enableCRC16)
{
	XVtableGetFunc(XDataFrameComm_class_init(), EXDataFrameComm_SetRecvValidCRC16, void(*)(XDataFrameComm*, bool))(comm, enableCRC16);
	/*if (enableCRC16)
	{
		uint8_t recvFrameTail[] = { 0x01, 0xFE,0xFE,0xFE };
		XDataFrameComm_setRecvFrameTail_base(comm, recvFrameTail, sizeof(recvFrameTail));
	}
	else
	{
		uint8_t recvFrameTail[] = { 0xFF,0xFF,0xFF };
		XDataFrameComm_setRecvFrameTail_base(comm, recvFrameTail, sizeof(recvFrameTail));
	}*/
}

void VXDataFrameComm_setSendValidCRC16(TJCHMIComm* comm, bool enableCRC16)
{
	XVtableGetFunc(XDataFrameComm_class_init(), EXDataFrameComm_SetSendValidCRC16, void(*)(XDataFrameComm*, bool))(comm, enableCRC16);
	if (enableCRC16)
	{
		uint8_t sendFrameTail[] = { 0x01, 0xFE,0xFE,0xFE };
		XDataFrameComm_setSendFrameTail_base(comm, sendFrameTail, sizeof(sendFrameTail));
	}
	else
	{
		uint8_t sendFrameTail[] = { 0xFF,0xFF,0xFF };
		XDataFrameComm_setSendFrameTail_base(comm, sendFrameTail, sizeof(sendFrameTail));
	}
}

void VXDataFrameComm_setRecvFrameTail(TJCHMIComm* comm, const uint8_t* data, uint8_t dataSize)
{
	//陶晶驰使用的帧尾方式已经添加无需添加
	//printf("检查帧尾是否已经内置了\n");
	if (dataSize == 3)
	{
		uint8_t tail[] = { 0xFF,0xFF,0xFF };
		if (memcmp(data, tail, 3) == 0)
			return;
	}
	if (dataSize == 4)
	{
		uint8_t tail[] = { 0x01, 0xFE,0xFE,0xFE };
		if (memcmp(data, tail, 4) == 0)
			return;
	}
	XClassGetVirtualFunc(comm, EXDataFrameComm_SetRecvFrameTail, bool(*)(XDataFrameComm*, const uint8_t*, uint8_t))(comm, data, dataSize);
}
