#include "XModbusRtu.h"
#include "XModbusFrameData.h"
#include "XCrc.h"
#include "XModbusConfig.h"
void XModbusRtuInit(XModbus* modbus)
{
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
        // 调用 RTU 底层初始化（配置串口参数：端口号、波特率、校验位）
        //eStatus = eMBRTUInit(ucMBAddress, ucPort, ulBaudRate, eParity);
    }
}
void XModbusRtuStart(XModbus* modbus)
{
    ENTER_CRITICAL_SECTION();
    modbus->eRcvState = STATE_RX_INIT;  // 初始状态：等待总线空闲
    if(modbus->SerialEnable)
        modbus->SerialEnable(modbus,TRUE, FALSE);  // 使能接收，禁用发送
    XTimer_start(modbus->timer);  // 启动定时器（T35用于检测帧间隔）
    EXIT_CRITICAL_SECTION();
}
void XModbusRtuStop(XModbus* modbus)
{
    ENTER_CRITICAL_SECTION();
    if (modbus->SerialEnable)
        modbus->SerialEnable(modbus, FALSE, FALSE);  // 禁用接收和发送
    XTimer_stop(modbus->timer); // 关闭定时器
    EXIT_CRITICAL_SECTION();
}
XModbusErrorCode XModbusRtuReceive(XModbus* modbus, XModbusFrameData* dataFrame)
{
    if (modbus == NULL|| dataFrame==NULL)
        return MB_EINVAL;
    bool            xFrameReceived = false;
    modbus->errorCode= MB_ENOERR;

    ENTER_CRITICAL_SECTION();
    //assert(usRcvBufferPos < MB_SER_PDU_SIZE_MAX);  // 确保缓冲区未溢出
    XModbusFrameData_setRtuData(dataFrame, modbus->recvBuffer);
    //解析的帧有问题
    if(dataFrame->pPduFramePos==NULL)
        modbus->errorCode = MB_EIO;
    EXIT_CRITICAL_SECTION();
    //XModbusFrameQueue_push(modbus->object.recvFrameQueue,&dataFrame);
    return  modbus->errorCode;
}

XModbusErrorCode XModbusRtuSend(XModbus* modbus, XModbusFrameData* dataFrame)
{
    if (modbus == NULL)
        return MB_EINVAL;
    //XModbusErrorCode    eStatus = MB_ENOERR;
    //USHORT          usCRC16;  // CRC - 16校验值
    XVector* dataVector = XVector_New(UCHAR);
    if (dataVector == NULL)
    {
        modbus->errorCode = MB_ENORES;
        return modbus->errorCode;
    }
    XVector_copy(dataVector, dataFrame->dataFrame);
    ////设置数据的总长度
    //XVector_resize(dataVector, usLength+1+ MB_SER_PDU_SIZE_CRC);
    ////获取内部数据指针
    //UCHAR* dataFrame = XVector_front(dataVector);
    ////保存从机地址
    //dataFrame[MB_SER_PDU_ADDR_OFF] = ucSlaveAddress;  // 添加从机地址
    ////保存pdu数据
    //for (size_t i = 0; i < usLength; i++)
    //{
    //    dataFrame[MB_SER_PDU_PDU_OFF + i] = pucFrame[i];
    //} 
    //// 计算CRC（包含地址和PDU）
    //usCRC16 = XCrc_get16((UCHAR*)dataFrame, usLength+1);
    ////保存校验码
    //uint16_t pos = usLength + 1;//校验码所属位置
    //dataFrame[pos++] = (UCHAR)(usCRC16 & 0xFF);  // CRC低位（小端模式）
    //dataFrame[pos++] = (UCHAR)(usCRC16 >> 8);       // CRC高位
    ////将发送数据插入发送队列
    //XQueue_push(modbus->object.sendQueue,&dataVector);
    ENTER_CRITICAL_SECTION();
    XQueue_push(modbus->sendQueue, &dataVector);
    EXIT_CRITICAL_SECTION();
    return modbus->errorCode;
}

bool XModbusRtuReceiveFSM(XModbus* modbus)
{
    if (modbus == NULL)
        return FALSE;
    UCHAR           ucByte;
    if (modbus->eSndState != STATE_TX_IDLE /*|| modbus->eSndState == STATE_TX_END*/)
        return FALSE;
    //assert(modbus->eSndState == STATE_TX_IDLE);  // 确保发送状态为空闲
    //发送完数据后3.5字符内收到了返回信息 重置发送状态
    if (modbus->eSndState == STATE_TX_END)
    {
        modbus->eSndState = STATE_TX_IDLE;
    }
    // 读取接收到的字节（平台特定：从串口缓冲区获取）
    (void)modbus->xGetByte(modbus,(CHAR*)&ucByte);
    XVector* recvVector = modbus->recvBuffer;
    switch (modbus->eRcvState) {
    case STATE_RX_INIT:  // 初始状态（等待总线空闲）
        XTimer_start(modbus->timer);  // 启动T35定时器，检测帧间隔
        break;

    case STATE_RX_ERROR:  // 接收错误状态（忽略后续字节）
        XTimer_start(modbus->timer);  // 保持定时器运行，等待错误帧结束
        break;

    case STATE_RX_IDLE:  // 空闲状态（接收到新帧起始字节）
        XVector_clear(recvVector);  // 重置接收缓冲区位置
        XVector_push_back(recvVector,&ucByte);  // 存储第一个字节（从机地址）
        modbus->eRcvState = STATE_RX_RCV;  // 切换到接收中状态
        XTimer_start(modbus->timer);  // 启动T35定时器
        break;

    case STATE_RX_RCV:  // 接收中状态（连续接收字节）
        if (XVector_size(recvVector) < MB_SER_PDU_SIZE_MAX) {
            XVector_push_back(recvVector, &ucByte);  // 存储字节到缓冲区
        }
        else {
            modbus->eRcvState = STATE_RX_ERROR;  // 缓冲区溢出，标记错误状态
            //printf("缓冲区溢出\n");
        }
        XTimer_start(modbus->timer);  // 每次接收到字节后重置定时器
        break;
    }
    return TRUE;
}

bool XModbusRtuTransmitFSM(XModbus* modbus)
{
    if (modbus == NULL)
        return false;
   // bool            xNeedPoll = FALSE;
    //printf("检查是否可以发送\n");
    if (modbus->eRcvState != STATE_RX_IDLE|| modbus->eSndState == STATE_TX_END)
        return false;

    //以下可以发送数据
    XModbusFrameQueue* sendQueue = modbus->sendQueue;
    XModbusFrameData* frame = NULL;
    XVector* dataVector = NULL;
    if (!XModbusFrameQueue_empty(sendQueue))
    {
        frame = XModbusFrameQueue_top(sendQueue);
        dataVector=frame->dataFrame;
        if (modbus->SerialEnable)
            modbus->SerialEnable(modbus, FALSE,TRUE);  // 发送，禁用接收
    }
    else
    {
        //printf("发送队列是空的\n");
        if (modbus->SerialEnable)
            modbus->SerialEnable(modbus,TRUE, FALSE);  // 禁用发送，重新使能接收
        return true;
    }

    switch (modbus->eSndState) 
    {
        case STATE_TX_IDLE:  // 发送空闲状态（无数据发送）
        {
            modbus->sendRemaining = XVector_size(dataVector);

            modbus->eSndState = STATE_TX_XMIT;
            break;
        }
        case STATE_TX_XMIT:  // 发送中状态（逐个字节发送）
        {
            if (modbus->sendRemaining != 0)
            {
                modbus->xPutByte(modbus,XVector_At(dataVector, XVector_size(dataVector)- modbus->sendRemaining,UCHAR));
                --modbus->sendRemaining;
            }
            else
            {//发送完成
                modbus->eSndState = STATE_TX_END;  // 切换到发送空闲状态
                XTimer_start(modbus->timer);  // 发送完成等待下一帧
                if (modbus->SerialEnable)
                    modbus->SerialEnable(modbus, TRUE, FALSE);  // 禁用发送，重新使能接收
#if MB_SEND_FRAME_SHOW
                XString* str = XModbusFrameData_to16HexString(frame);
                printf("发送帧:%s\n", XString_c_str(str));
                //            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
                XString_free(str);
#endif // MB_SEND_FRAME_SHOW
                XModbusFrameQueue_pop(sendQueue);
                // 发送完成，通知上层协议帧已发送
                XEventQueue_push(modbus->eventQueue, EV_FRAME_SENT);
                //xNeedPoll = xMBPortEventPost(EV_FRAME_SENT);
            }
            break;
        }
    }
    return true;
}

bool XModbusRtuTimerT35Expired(XModbus* modbus)
{
    bool            xNeedPoll = FALSE;
    //发完一帧数据总线等待
    if (modbus->eSndState == STATE_TX_END)
    {
        modbus->eSndState = STATE_TX_IDLE;
        XTimer_stop(modbus->timer);  // 关闭定时器
        return true;
    }
    //接收超时等待
    switch (modbus->eRcvState) {
    case STATE_RX_INIT:  // 初始状态超时（总线空闲，进入IDLE）
        xNeedPoll = XEventQueue_push(modbus->eventQueue, EV_READY);  // 通知协议栈总线就绪
        break;

    case STATE_RX_RCV:   // 接收中状态超时（帧接收完成）
        xNeedPoll = XEventQueue_push(modbus->eventQueue, EV_FRAME_RECEIVED);  // 通知上层协议帧已接收
        break;

    case STATE_RX_ERROR: // 错误状态超时（忽略）
        break;

    default:            // 非法状态（断言检查）
        assert((modbus->eRcvState == STATE_RX_INIT) || (modbus->eRcvState == STATE_RX_RCV) || (modbus->eRcvState == STATE_RX_ERROR));
    }

    XTimer_stop(modbus->timer);  // 关闭定时器
    modbus->eRcvState = STATE_RX_IDLE;  // 切换到接收空闲状态
    return xNeedPoll;
}
