#include "XModbusRTU.h"
#include "XModbusProto.h"
#include "XModbusConfig.h"
#include "XModbusFrame.h"
#include "XModbusFrame_RTU.h"
#include "XTimerWheel.h"
#include "XVector.h"
#include "XQueueBase.h"
#include "XCrc.h"
#include <string.h>
typedef struct XModbusFrame XModbusFrame;
static XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData);
static XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData);
static bool VXModbusBase_ReceiveFSM(XModbusBase* modbus);
static bool VXModbusBase_TransmitFSM(XModbusBase* modbus);

static void VXModbusBase_TimerT35Expired(XModbusBase* modbus);
static void VXModbusBase_TimerSendExpired(XModbusBase* modbus);
XVtable* XModbusRTU_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCOMMUNICATORBASE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XModbusBase_class_init());
	/*void* table[] =
	{

	};*/
	//追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXModbusBase_SendFrame, VXModbusBase_sendFrame);
	XVTABLE_OVERLOAD_DEFAULT(EXModbusBase_RecvFrame, VXModbusBase_recvFrame);
#if SHOWCONTAINERSIZE
	printf("XModbusRTU size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void XModbusRTU_init(XModbusRTU* modbus)
{
    if (modbus == NULL)
        return NULL;
    //开始初始化
    memset(((XModbusBase*)modbus) + 1, 0, sizeof(XModbusRTU) - sizeof(XModbusBase));
    XModbusBase_init(modbus);


    modbus->m_timerSendExpired = XTimerWheel_new();
    XTimerWheel_setTimerCallback(modbus->m_timerSendExpired, VXModbusBase_TimerSendExpired);
    XTimerWheel_setUserData(modbus->m_timerSendExpired, modbus);
    //XTimerWheel_setInterval_base(modbus->m_timerSendExpired, );
    modbus->m_timerT35Expired = XTimerWheel_new();
    XTimerWheel_setTimerCallback(modbus->m_timerT35Expired, VXModbusBase_TimerT35Expired);
    XTimerWheel_setUserData(modbus->m_timerT35Expired, modbus);
}
XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData)
{
	if (modbus == NULL)
		return MB_EINVAL;
	XModbusErrorCode error = MB_ENOERR;
	XVector* dataVector = XVector_New(uint8_t);
	if (dataVector == NULL)
	{
		error = MB_ENORES;
		return error;
	}
	XVector_copy_base(dataVector, frameData->frameData);

	ENTER_CRITICAL_SECTION();
	XQueueBase_push_base(modbus->sendQueue, &dataVector);
	EXIT_CRITICAL_SECTION();
	return error;
}

XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData)
{
	if (modbus == NULL || frameData == NULL)
		return MB_EINVAL;
	bool            xFrameReceived = false;

	ENTER_CRITICAL_SECTION();

	XModbusFrameRTU_parseData_reply(frameData, modbus->m_parent.m_recvAsyncBuffer);

	//解析的帧有问题
	if (XVector_isEmpty_base(frameData->frameData))
		return MB_EIO;
	EXIT_CRITICAL_SECTION();
	return MB_ENOERR;
}
bool VXModbusBase_ReceiveFSM(XModbusBase* modbus)
{
    //printf("检查接收缓冲区\n");
    if (modbus == NULL)
        return false;
    uint8_t           ucByte;
    //上一帧刚发完等待一段时间在尝试接收
    if (modbus->eSndState != STATE_TX_IDLE /*|| (modbus->timerOutNumber < MB_MASTER_RECV_WAIT_TIME && modbus->eSndState == STATE_TX_END)*/)
        return false;
    //printf("可以接收数据\n");
    //assert(modbus->eSndState == STATE_TX_IDLE);  // 确保发送状态为空闲
    //发送完数据后3.5字符内收到了返回信息 重置发送状态

    // 读取接收到的字节（平台特定：从串口缓冲区获取）
    //modbus->ioDevice->m_port.readData_funcPointer(modbus->ioDevice, (uint8_t*)&ucByte,1);
    //XCircularQueue_receive_base(modbus->ioDevice->m_readBuffer, &ucByte);
    XIODeviceBase_read_base(modbus->m_parent.m_io, &ucByte,1);
    XVector* recvVector = modbus->m_parent.m_recvAsyncBuffer;
    switch (modbus->eRcvState) {
    case STATE_RX_INIT:  // 初始状态（等待总线空闲）
        XTimerWheel_start_base(((XModbusRTU*)modbus)->m_timerT35Expired);
        //XTimerBase_start_base(modbus->timer);  // 启动T35定时器，检测帧间隔
#if MB_CALIBRATION_TIMER_SETTINGS
        modbus->calibrationTimer_current = XTimerBase_getCurrentTime();
#endif 
        break;

    case STATE_RX_ERROR:  // 接收错误状态（忽略后续字节）
       // XTimerBase_start_base(modbus->timer);  // 保持定时器运行，等待错误帧结束
        XTimerWheel_start_base(((XModbusRTU*)modbus)->m_timerT35Expired);
#if MB_CALIBRATION_TIMER_SETTINGS
        modbus->calibrationTimer_current = XTimerBase_getCurrentTime();
#endif 
        break;

    case STATE_RX_IDLE:  // 空闲状态（接收到新帧起始字节）
        //printf("开始接收\n");
        XVector_clear_base(recvVector);  // 重置接收缓冲区位置
        XVector_push_back_base(recvVector, &ucByte);  // 存储第一个字节（从机地址）
        if (modbus->eSndState == STATE_TX_END)
            modbus->eSndState = STATE_TX_IDLE;
        modbus->eRcvState = STATE_RX_RCV;  // 切换到接收中状态
        //XTimerBase_start_base(modbus->timer);  // 启动T35定时器
        XTimerWheel_start_base(((XModbusRTU*)modbus)->m_timerT35Expired);
#if MB_CALIBRATION_TIMER_SETTINGS
        modbus->calibrationTimer_current = XTimerBase_getCurrentTime();
#endif 
        break;

    case STATE_RX_RCV:  // 接收中状态（连续接收字节）
        if (XVector_getSize_base(recvVector) < MB_SER_PDU_SIZE_MAX) {
            XVector_push_back_base(recvVector, &ucByte);  // 存储字节到缓冲区
        }
        else {
            modbus->eRcvState = STATE_RX_ERROR;  // 缓冲区溢出，标记错误状态
            //printf("缓冲区溢出\n");
        }
        XTimerBase_start_base(modbus->timer);  // 每次接收到字节后重置定时器
#if MB_CALIBRATION_TIMER_SETTINGS
        modbus->calibrationTimer_current = XTimerBase_getCurrentTime();
#endif 
        break;
    }
    return true;
}

bool VXModbusBase_TransmitFSM(XModbusBase* modbus)
{
    if (modbus == NULL)
        return false;
    //printf("检查是否可以发送\n");
    if (modbus->eRcvState != STATE_RX_IDLE || modbus->eSndState == STATE_TX_END)
        return false;

    //以下可以发送数据
    XModbusFrameQueue* sendQueue = modbus->sendQueue;
    XModbusFrame* frame = NULL;
    XVector* dataVector = NULL;
    if (!XModbusFrameQueue_empty(sendQueue))
    {
        frame = XModbusFrameQueue_top(sendQueue);
        dataVector = frame->frameData;
        //if (modbus->SerialEnable)
        //    modbus->SerialEnable(modbus, false, true);  // 发送，禁用接收
    }
    else
    {
        //printf("发送队列是空的\n");
        //if (modbus->SerialEnable)
        //    modbus->SerialEnable(modbus, true, false);  // 禁用发送，重新使能接收
        return true;
    }
    //printf("fasong\n");
    switch (modbus->eSndState)
    {
    case STATE_TX_IDLE:  // 发送空闲状态（无数据发送）
    {
        modbus->pendingCount = XVector_getSize_base(dataVector);

        modbus->eSndState = STATE_TX_XMIT;
        //printf("发送空闲\n");
        break;
    }
    case STATE_TX_XMIT:  // 发送中状态（逐个字节发送）
    {
        //printf("发送中\n");
        if (modbus->pendingCount != 0)
        {
            XIODeviceBase_write_base(modbus->m_parent.m_io, XVector_at_base(dataVector, XVector_getSize_base(dataVector) - modbus->pendingCount), 1);
#if !MB_IS_COMP_SEND_FRAME
            XIODeviceBase_writeFull_base(modbus->m_parent.m_io);
#endif
            --modbus->pendingCount;
            // printf("end\n");
        }
        else
        {//发送完成
#if MB_IS_COMP_SEND_FRAME
            XIODeviceBase_writeFull_base(modbus->ioDevice);
#endif
            modbus->eSndState = STATE_TX_END;  // 切换到发送空闲状态
            //modbus->timerOutNumber = 0;//开始计数
            //XTimerBase_start_base(modbus->timer);  // 发送完成等待下一帧
#if MB_CALIBRATION_TIMER_SETTINGS
            modbus->calibrationTimer_current = XTimerBase_getCurrentTime();
#endif 
           // if (modbus->SerialEnable)
             //   modbus->SerialEnable(modbus, true, false);  // 禁用发送，重新使能接收
#if MB_SEND_FRAME_SHOW
            XString* str = XModbusFrameRTU_to16HexString(frame);
            printf("发送帧:%s\n", XString_c_str(str));
            //            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
            XString_free_base(str);
#endif // MB_SEND_FRAME_SHOW
            if (XModbus_isMaster(modbus))
            {//如果是主站 设置当前接收回调
                if (frame->recvHandle != NULL && frame->recvHandle->pRecvHandCallFunc)
                {//发送的帧中存在回调方法
                    XVector_push_back_base(modbus->recvHandleMaster, &(frame->recvHandle));
                    frame->recvHandle = NULL;//转移所有权
                }
            }
            XModbusFrameQueue_pop(sendQueue);
            // 发送完成，通知上层协议帧已发送
            //printf("发送完成事件\n");
            XModbusBase_sendEvent(modbus, EV_FRAME_SENT);
            //XCustomQueue_Push(modbus->eventQueue, XModbusEventType, EV_FRAME_SENT);
        }
        break;
    }
    }
    return true;
}
void VXModbusBase_TimerT35Expired(XModbusBase* modbus)
{
    bool            xNeedPoll = false;
    //发完一帧数据总线等待
    if (modbus->eSndState == STATE_TX_END)
    {
        //++modbus->timerOutNumber;
        if (XModbus_isMaster(modbus))
        {
            if ((modbus->timerOutNumber > MB_MASTER_RECV_WAIT_TIME))
            {//发送后等待一段时间
                modbus->eSndState = STATE_TX_IDLE;
                XTimerBase_stop_base(modbus->timer);  // 关闭定时器
            }
        }
        else
        {
            modbus->eSndState = STATE_TX_IDLE;
            XTimerBase_stop_base(modbus->timer);  // 关闭定时器
        }
        return true;
    }
    //接收超时等待
    switch (modbus->eRcvState) {
    case STATE_RX_INIT:  // 初始状态超时（总线空闲，进入IDLE）
        XModbus_sendEvent(modbus, EV_READY);
        //XCustomQueue_Push(modbus->eventQueue, XModbusEventType, EV_READY);  // 通知协议栈总线就绪
        break;

    case STATE_RX_RCV:   // 接收中状态超时（帧接收完成）
        XModbus_sendEvent(modbus, EV_FRAME_RECEIVED);
        //XCustomQueue_Push(modbus->eventQueue, XModbusEventType, EV_FRAME_RECEIVED);  // 通知上层协议帧已接收
        break;

    case STATE_RX_ERROR: // 错误状态超时（忽略）
        break;
    case STATE_RX_IDLE: //接收空闲

        //发生一次错误
        //printf("接收空闲:%d\n", modbus->eSndState);
        break;
    default:            // 非法状态（断言检查）
        assert((modbus->eRcvState == STATE_RX_INIT) || (modbus->eRcvState == STATE_RX_RCV) || (modbus->eRcvState == STATE_RX_ERROR));
    }

    modbus->eRcvState = STATE_RX_IDLE;  // 切换到接收空闲状态
    XTimerBase_stop_base(modbus->timer);  // 关闭定时器
    return xNeedPoll;
}

void VXModbusBase_TimerSendExpired(XModbusBase* modbus)
{
    return false;
}
