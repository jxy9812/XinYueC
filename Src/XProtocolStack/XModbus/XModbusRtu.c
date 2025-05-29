#include "XModbusRtu.h"
#include "XModbusFrame.h"
#include "XCrc.h"
#include "XModbusConfig.h"
#include "XSerialPort.h"
#include "XCircularQueue.h"
#include <string.h>
static void  XModbusRtu_Timer_out(void* arg)
{
    ((XModbus*)arg)->pxMBPortCBTimerExpired(arg);
}
XModbusErrorCode XModbusRtuInit(XModbus* modbus, XModbusMode mode, XModbus_PortFunc* func, uint8_t address, uint8_t port, uint32_t baudRate, XModbusParity parity)
{
    XModbusErrorCode error = MB_ENOERR;
    modbus->ioDevice = XSerialPort_new(&(func->IO_Port));
    XIODevice_setReadBuffer(modbus->ioDevice, MB_DEVICE_RECV_BUFFER_SIZE);
    XIODevice_setWriteBuffer(modbus->ioDevice, MB_DEVICE_SEND_BUFFER_SIZE);
    // 调用 RTU 底层初始化（配置串口参数：端口号、波特率、校验位）
    if (!XSerialPort_open(modbus->ioDevice, mode, port, baudRate, parity))
    {
        error = MB_EPORTERR;
        return error;
    }
    { // RTU 模式初始化（二进制传输，效率高，适合串口通信）
        // 绑定 RTU 模式专用函数（来自 mbrtu.c）
        modbus->pvMBFrameStartCur = XModbusRtuStart;      // 启动 RTU 协议栈（初始化串口、设置波特率、校验位）
        modbus->pvMBFrameStopCur = XModbusRtuStop;        // 停止 RTU 协议栈（关闭接收/发送线程）
        modbus->peMBFrameSendCur = XModbusRtuSend;        // 发送 RTU 帧（添加 CRC 校验码）
        modbus->peMBFrameReceiveCur = XModbusRtuReceive;  // 接收 RTU 帧（校验 CRC 并解析数据）
        //modbus->pvMBFrameCloseCur = MB_PORT_HAS_CLOSE ? vMBPortClose : NULL; // 可选的端口关闭函数

        // 绑定 RTU 模式状态机回调（处理字节接收、发送缓冲区空、定时器超时）
        modbus->pxMBFrameCBByteReceived = XModbusRtuReceiveFSM;  // RTU 接收状态机（处理字节流到完整帧）
        modbus->pxMBFrameCBTransmitterEmpty = XModbusRtuTransmitFSM; // RTU 发送状态机（控制发送缓冲区数据）
        modbus->pxMBPortCBTimerExpired = XModbusRtuTimerT35Expired;  // RTU T35 超时处理（帧间隔超时）

        modbus->eSndState = STATE_TX_IDLE;
        modbus->eRcvState = STATE_RX_INIT;

 
        modbus->SerialEnable = func->SerialEnable;
        //定时器初始化
        assert(func->timer);
        modbus->timer = func->timer;
        XTimerBase_setUserData(modbus->timer,modbus);
        XTimerBase_setTimerCallback(modbus->timer,XModbusRtu_Timer_out);
        //设置定时时间
        XTimerBase_setInterval_base(modbus->timer, 3.5 * (10 + parity) * 1000 / baudRate);
        //modbus->timer->m_interval = 3.5 * (10+ parity) * 1000 / baudRate;
        if (XTimerBase_getInterval(modbus->timer) < 2)
            XTimerBase_setInterval_base(modbus->timer, 2);
        //XTimer_create(modbus->timer);
    }
    return error;
}
void XModbusRtuStart(XModbus* modbus)
{
    ENTER_CRITICAL_SECTION();
    modbus->eRcvState = STATE_RX_INIT;  // 初始状态：等待总线空闲
    if(modbus->SerialEnable)
        modbus->SerialEnable(modbus->ioDevice,true, false);  // 使能接收，禁用发送
    XTimerBase_start_base(modbus->timer);  // 启动定时器（T35用于检测帧间隔）
    EXIT_CRITICAL_SECTION();
}
void XModbusRtuStop(XModbus* modbus)
{
    ENTER_CRITICAL_SECTION();
    if (modbus->SerialEnable)
        modbus->SerialEnable(modbus, false, false);  // 禁用接收和发送
    XTimerBase_stop_base(modbus->timer); // 关闭定时器
    EXIT_CRITICAL_SECTION();
}
XModbusErrorCode XModbusRtuReceive(XModbus* modbus, XModbusFrame* frameData)
{
    if (modbus == NULL|| frameData==NULL)
        return MB_EINVAL;
    bool            xFrameReceived = false;

    ENTER_CRITICAL_SECTION();

    XModbusFrameRTU_parseData_reply(frameData, modbus->recvBuffer);

    //解析的帧有问题
    if(XVector_isEmpty_base(frameData->frameData))
        return MB_EIO;
    EXIT_CRITICAL_SECTION();
    return MB_ENOERR;
}

XModbusErrorCode XModbusRtuSend(XModbus* modbus, XModbusFrame* frameData)
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
    XQueue_push_base(modbus->sendQueue, &dataVector);
    EXIT_CRITICAL_SECTION();
    return error;
}

bool XModbusRtuReceiveFSM(XModbus* modbus)
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
    XCircularQueue_receive_base(modbus->ioDevice->m_readBuffer, &ucByte);
    XVector* recvVector = modbus->recvBuffer;
    switch (modbus->eRcvState) {
    case STATE_RX_INIT:  // 初始状态（等待总线空闲）
        XTimerBase_start_base(modbus->timer);  // 启动T35定时器，检测帧间隔
#if MB_CALIBRATION_TIMER_SETTINGS
        modbus->calibrationTimer_current = XTimer_getCurrentTime();
#endif 
        break;

    case STATE_RX_ERROR:  // 接收错误状态（忽略后续字节）
        XTimerBase_start_base(modbus->timer);  // 保持定时器运行，等待错误帧结束
#if MB_CALIBRATION_TIMER_SETTINGS
        modbus->calibrationTimer_current = XTimer_getCurrentTime();
#endif 
        break;

    case STATE_RX_IDLE:  // 空闲状态（接收到新帧起始字节）
        //printf("开始接收\n");
        XVector_clear_base(recvVector);  // 重置接收缓冲区位置
        XVector_push_back_base(recvVector,&ucByte);  // 存储第一个字节（从机地址）
        if (modbus->eSndState == STATE_TX_END)
            modbus->eSndState = STATE_TX_IDLE;
        modbus->eRcvState = STATE_RX_RCV;  // 切换到接收中状态
        XTimerBase_start_base(modbus->timer);  // 启动T35定时器
#if MB_CALIBRATION_TIMER_SETTINGS
        modbus->calibrationTimer_current = XTimer_getCurrentTime();
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
        modbus->calibrationTimer_current = XTimer_getCurrentTime();
#endif 
        break;
    }
    return true;
}

bool XModbusRtuTransmitFSM(XModbus* modbus)
{
    if (modbus == NULL)
        return false;
    //printf("检查是否可以发送\n");
    if (modbus->eRcvState != STATE_RX_IDLE|| modbus->eSndState == STATE_TX_END)
        return false;

    //以下可以发送数据
    XModbusFrameQueue* sendQueue = modbus->sendQueue;
    XModbusFrame* frame = NULL;
    XVector* dataVector = NULL;
    if (!XModbusFrameQueue_empty(sendQueue))
    {
        frame = XModbusFrameQueue_top(sendQueue);
        dataVector=frame->frameData;
        if (modbus->SerialEnable)
            modbus->SerialEnable(modbus, false,true);  // 发送，禁用接收
    }
    else
    {
        //printf("发送队列是空的\n");
        if (modbus->SerialEnable)
            modbus->SerialEnable(modbus,true, false);  // 禁用发送，重新使能接收
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
                XIODevice_write(modbus->ioDevice, XVector_at_base(dataVector, XVector_getSize_base(dataVector) - modbus->pendingCount), 1);
#if !MB_IS_COMP_SEND_FRAME
                XIODevice_writeFull(modbus->ioDevice);
#endif
                --modbus->pendingCount;
               // printf("end\n");
            }
            else
            {//发送完成
#if MB_IS_COMP_SEND_FRAME
                XIODevice_writeFull(modbus->ioDevice);
#endif
                modbus->eSndState = STATE_TX_END;  // 切换到发送空闲状态
                modbus->timerOutNumber = 0;//开始计数
                XTimerBase_start_base(modbus->timer);  // 发送完成等待下一帧
#if MB_CALIBRATION_TIMER_SETTINGS
                modbus->calibrationTimer_current = XTimer_getCurrentTime();
#endif 
                if (modbus->SerialEnable)
                    modbus->SerialEnable(modbus, true, false);  // 禁用发送，重新使能接收
#if MB_SEND_FRAME_SHOW
                XString* str = XModbusFrameRTU_to16HexString(frame);
                printf("发送帧:%s\n", XString_c_str(str));
                //            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
                XString_free(str);
#endif // MB_SEND_FRAME_SHOW
                if (XModbus_isMaster(modbus))
                {//如果是主站 设置当前接收回调
                    if (frame->recvHandle != NULL && frame->recvHandle->pRecvHandCallFunc)
                    {//发送的帧中存在回调方法
                        XVector_push_back_base(modbus->recvHandleMaster,&(frame->recvHandle));
                        frame->recvHandle = NULL;//转移所有权
                    }
                }
                XModbusFrameQueue_pop(sendQueue);
                // 发送完成，通知上层协议帧已发送
                //printf("发送完成事件\n");
                XModbus_sendEvent(modbus, EV_FRAME_SENT);
                //XCustomQueue_Push(modbus->eventQueue, XModbusEventType, EV_FRAME_SENT);
            }
            break;
        }
    }
    return true;
}

bool XModbusRtuTimerT35Expired(XModbus* modbus)
{
    bool            xNeedPoll = false;
    //发完一帧数据总线等待
    if (modbus->eSndState == STATE_TX_END)
    {
        ++modbus->timerOutNumber;
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
