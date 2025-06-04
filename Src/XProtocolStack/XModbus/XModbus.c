#include "XModbus.h"
#include "XMemory.h"
#include "XModbusConfig.h"
#include "XModbusProto.h"
#include "XModbusFunctionHandler.h"
#include "XCircularQueue.h"
// 条件编译：根据配置启用不同 Modbus 通信模式
#if MB_RTU_ENABLED == 1    // 启用 RTU 模式（二进制传输，常用于串口）
#include "XModbusRtu.h"        // RTU 模式具体实现
#endif
#if MB_ASCII_ENABLED == 1  // 启用 ASCII 模式（文本传输，兼容性好）
#include "mbascii.h"        // ASCII 模式具体实现
#endif
#if MB_TCP_ENABLED == 1   // 启用 TCP 模式（Modbus TCP，需额外网络层支持）
#include "mbtcp.h"          // TCP 模式具体实现
#endif

// 定义端口关闭功能（若平台未实现端口关闭函数，默认禁用）
#ifndef MB_PORT_HAS_CLOSE
#define MB_PORT_HAS_CLOSE 0
#endif
static const bool recvHandleMaster_XEquality(const void* Value, const void* CompareValue)
{
    uint16_t waitAddressCode = (*((XModbusFrameDataRecvHandle**)Value))->waitAddressCode;
    uint16_t  value = *((uint16_t*)CompareValue);
    return (*((XModbusFrameDataRecvHandle**)Value))->waitAddressCode == *((uint16_t*)CompareValue);
}
XModbusErrorCode XModbus_init(XModbus* modbus, XModbus_PortFunc* func, XModbusMode mode, uint8_t address, uint8_t port, uint32_t baudRate, XModbusParity parity)
{
	if (modbus == NULL|| func==NULL)
		return MB_EINVAL;
    XModbusErrorCode error= MB_ENOERR;
	modbus->sendQueue= XModbusFrameQueue_new(MB_FRAME_SEND_QUEUE_COUNT);
    modbus->recvFrameQueue=XModbusFrameQueue_new(MB_FRAME_RECV_QUEUE_COUNT);
    if(func->EventQueuePort.create_funcPointer)
        modbus->eventQueue = XCustomQueue_new(&(func->EventQueuePort),sizeof(XModbusEventType), MB_EVENT_QUEUE_COUNT);
    else
	    modbus->eventQueue = XCustomQueue_new_XCircularQueue(sizeof(XModbusEventType), MB_EVENT_QUEUE_COUNT);
    modbus->funcCodeList = XModbusFuncCodeList_new();
	modbus->mode = mode;
    //assert(func->IO_Port.readData_funcPointer);

    //assert(func->IO_Port.writeData_funcPointer);
    modbus->recvBuffer = XVector_new(sizeof(char));
    XVector_resize_base(modbus->recvBuffer ,MB_RECV_BUFFER_SIZE);
 
    modbus->recvHandleMaster = NULL;
#if MB_CALIBRATION_TIMER_SETTINGS
    modbus->calibrationTimer_current = XTimerBase_getCurrentTime();
#endif
    // 根据选择的模式初始化对应的函数指针和底层模块
    if (XModbus_isMaster(modbus))
    {//主站下初始化资源
        modbus->recvHandleMaster = XVector_New(XModbusFrameDataRecvHandle*);
        modbus->recvHandleMaster->m_equality = recvHandleMaster_XEquality;
        modbus->regularlySendMaster = XModbusRegularlySendFrameList_new();
    }
    else
    {//从站初始化资源
       
    }
    switch (mode) {
#if MB_RTU_ENABLED > 0
    case MB_RTU_MASTER: 
    case MB_RTU_SLAVE:
        error = XModbusRtuInit(modbus,mode,func,address,port,baudRate,parity);
    break;
#endif
#if MB_ASCII_ENABLED > 0
    case MB_ASCII: 
    { // ASCII 模式初始化（文本传输，可读性好，适合调试）
        //// 绑定 ASCII 模式专用函数（来自 mbascii.c）
        //pvMBFrameStartCur = eMBASCIIStart;      // 启动 ASCII 协议栈（初始化串口）
        //pvMBFrameStopCur = eMBASCIIStop;        // 停止 ASCII 协议栈
        //peMBFrameSendCur = eMBASCIISend;        // 发送 ASCII 帧（添加 LRC 校验和）
        //peMBFrameReceiveCur = eMBASCIIReceive;  // 接收 ASCII 帧（校验 LRC 并解析数据）
        //pvMBFrameCloseCur = MB_PORT_HAS_CLOSE ? vMBPortClose : NULL;

        //// 绑定 ASCII 模式状态机回调
        //pxMBFrameCBByteReceived = xMBASCIIReceiveFSM;  // ASCII 接收状态机（处理冒号分隔符、十六进制转换）
        //pxMBFrameCBTransmitterEmpty = xMBASCIITransmitFSM; // ASCII 发送状态机
        //pxMBPortCBTimerExpired = xMBASCIITimerT1SExpired;  // ASCII T1S 超时处理（字符间隔超时）

        //// 调用 ASCII 底层初始化（配置串口参数）
        //eStatus = eMBASCIIInit(ucMBAddress, ucPort, ulBaudRate, eParity);
        //break;
   }
#endif
    default: // 非法模式处理
        error = MB_EINVAL; // 不支持的模式错误
        return error;
    }

    // 初始化成功后配置协议栈状态
    if (error == MB_ENOERR) 
    {
    // 初始化端口事件模块（如事件队列，用于驱动协议栈轮询）
        if (modbus->eventQueue==NULL) 
        {
            error = MB_EPORTERR; // 端口事件初始化失败（平台相关错误）
        }
        else 
        {
            //eMBCurrentMode = eMode; // 记录当前工作模式
            modbus->state = STATE_DISABLED; // 初始化后状态为“禁用”，需调用 eMBEnable 激活
        }
    }
    return error;
}
//向接收队列中增加功能码帧数据
static bool recvFrameQueue_pushExecuteFrame(XModbus* modbus, XModbusFrame* recvFrame)
{
    if (!XModbusFrameQueue_push(modbus->recvFrameQueue, recvFrame))
    {
#if MB_QUEUE_FULL_SHOW
        printf("接收帧队列溢出当前最大:%d,建议增大队列,调整:MB_FRAME_RECV_QUEUE_COUNT\n", MB_FRAME_RECV_QUEUE_COUNT);
#endif // MB_QUEUE_FULL_SHOW
        return false;
    }

    return XModbus_sendEvent(modbus, EV_EXECUTE);
}
XModbusErrorCode XModbus_enable(XModbus* modbus)
{
    XModbusErrorCode error = MB_ENOERR;
    if (modbus && modbus->pvMBFrameStartCur)
    {
        if (modbus->state == STATE_DISABLED) {
            modbus->pvMBFrameStartCur(modbus); // 启动协议栈（初始化端口资源，开始接收/发送数据）
            modbus->state = STATE_ENABLED; // 更新状态为启用
        }
        else {
            error = MB_EILLSTATE; // 非法状态（已启用或未初始化）
        }
        return error;
    }
    return MB_EINVAL;
}
XModbusErrorCode XModbus_disable(XModbus* modbus)
{
    XModbusErrorCode error = MB_ENOERR;
    if (modbus && modbus->pvMBFrameStopCur)
    {
        if (modbus->state == STATE_ENABLED) { // 从启用状态禁用
            modbus->pvMBFrameStopCur(modbus); // 停止协议栈（暂停接收/发送，清理临时资源）
            modbus->state = STATE_DISABLED; // 更新状态为禁用
            error = MB_ENOERR;
        }
        else if (modbus->state == STATE_DISABLED) { // 已禁用状态，直接返回成功
            error = MB_ENOERR;
        }
        else { // 未初始化状态
            error = MB_EILLSTATE; // 非法状态错误
        }
        return error;
    }
    return MB_EINVAL;
   
}
// 接收到完整的 Modbus 帧
static XModbusErrorCode XModbus_EV_FRAME_RECEIVED(XModbus* modbus)
{
    XModbusErrorCode error = MB_ENOERR;
   /* ((char*)(XVector_begin(modbus->recvBuffer)))[XVector_getSize_base(modbus->recvBuffer)] = 0;
    printf("数据:%s  大小:%d buff接收缓冲区大小:%d\n",XVector_begin(modbus->recvBuffer), XVector_getSize_base(modbus->recvBuffer), XVector_getCapacity_base(modbus->recvBuffer));*/
    
    // 调用对应模式的接收函数，获取帧数据（地址、缓冲区、长度）
    XModbusFrame* recvFrame = XModbusFrame_new();
    if (recvFrame == NULL)
        return MB_ENORES;
    recvFrame->mode = modbus->mode;
    //解析数据帧
    error = modbus->peMBFrameReceiveCur(modbus,recvFrame);
    //printf("进入帧数据处理:%d\n", error);
    if (error == MB_ENOERR) {
        uint8_t address= XModbusFrame_getAddress(recvFrame);
        uint8_t code= XModbusFrame_getFuncCode(recvFrame);
#if MB_RECV_FRAME_SHOW
        XString* str = XModbusFrameRTU_to16HexString(recvFrame);
        printf("接收帧:%s\n", XString_c_str(str));
        //            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
        XString_free_base(str);
#endif // MB_SEND_FRAME_SHOW
        
        if (XModbus_isMaster(modbus))
        {
            //printf("当前是主站\n");
           //printf("将有效的帧加入接收队列处理\n");
            if (recvFrameQueue_pushExecuteFrame(modbus, recvFrame))
                return error;
            error = MB_ENORES;
        }
        else if ((address == modbus->address) || (address == MB_ADDRESS_BROADCAST))
        {//当前是从站
            //printf("将有效的帧加入接收队列处理\n");
            if (recvFrameQueue_pushExecuteFrame(modbus, recvFrame))
                return error;
            error = MB_ENORES;
            //                xMBPortEventPost(EV_EXECUTE); // 触发功能码执行事件
        }
        else
        {
            error = MB_EIO;
        }
    }
    XModbusFrame_free(recvFrame);
    return error;
}
//功能码处理
static void  XModbus_EV_EXECUTE(XModbus* modbus)
{
    //printf("处理功能码\n");
    if (modbus == NULL || XModbusFrameQueue_empty(modbus->recvFrameQueue))
        return;
   
    XModbusFrame* frame = XModbusFrameQueue_top(modbus->recvFrameQueue);
    uint8_t address = XModbusFrame_getAddress(frame);
    uint8_t code = XModbusFrame_getFuncCode(frame);
    //XString* str = XModbusFrameRTU_to16HexString(frame);
    //printf("地址:%X 功能码:%X 完整:%s\n", address, code, XString_c_str(str));
    ////            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
    //XString_free_base(str);

    //如果是主站检查是否有回调函数
    if (XModbus_isMaster(modbus)/*&& modbus->recvHandleMaster!=NULL*/)
    {
        uint16_t waitAddressCode= (address << 8 | code);
        XModbusFrameDataRecvHandle** value=XVector_find_base(modbus->recvHandleMaster,&waitAddressCode);
        //printf("value:%p\n",value);
        if (value)
        {
            if (frame->recvHandle != NULL)//释放准备交换
                XMemory_free(frame->recvHandle);
            //拷贝数据
            frame->recvHandle = *value;
            *value = NULL;
            //执行回调函数
            frame->recvHandle->pRecvHandCallFunc(modbus, frame);
            //释放一个资源
            XModbusFrameQueue_pop(modbus->recvFrameQueue);
            XVector_erase_base(modbus->recvHandleMaster, value);
            return;
        }
    }
    //执行功能码
    XModbusFunctionHandler* FunctionHandler=XModbusFuncCodeList_findFuncCode(modbus->funcCodeList,code);
    if (FunctionHandler == NULL)
    {//没有对应的处理函数

    }
    else
    {
        //printf("执行功能码\n");
        assert(FunctionHandler->function);
        FunctionHandler->function(modbus,frame, FunctionHandler);//处理中
    }

    //释放一个资源
    XModbusFrameQueue_pop(modbus->recvFrameQueue);
}
static XModbusErrorCode XModbus_EventEmpty(XModbus* modbus)
{
    //static size_t currentTime = 0;
    //if (currentTime + 1000 < XTimerBase_getCurrentTime())
    //{
    //    currentTime = XTimerBase_getCurrentTime();
    //    printf("事件队列是空\n");
    //}
    XModbusErrorCode error = MB_ENOERR;
    //检查回调函数是否超时
    if (modbus->recvHandleMaster != NULL)
    {
        XModbusFrameDataRecvHandle** Handle = XVector_front_base(modbus->recvHandleMaster);
        if (Handle != NULL)
        {//已经超时了
            if ((*Handle) != NULL)
            {
                if ((*Handle)->timeout < XTimerBase_getCurrentTime())
                {
                    if ((*Handle)->pRecvHandCallFunc)
                        (*Handle)->pRecvHandCallFunc(modbus, NULL);
                    XMemory_free(*Handle);
                    XVector_pop_front_base(modbus->recvHandleMaster);
                }
            }
            else
            {
                XVector_pop_front_base(modbus->recvHandleMaster);
            }
        }
    }
    //处理定时发送帧数据
    if (modbus->regularlySendMaster != NULL)
    {
        XListDNode* frontNode = XContainerDataPtr(modbus->regularlySendMaster);
        if (frontNode != NULL)
        {
            XModbusRegularlySendFrame* regularly = (XModbusRegularlySendFrame*)(frontNode->data);
            //XModbusFrame*  frame= regularly->frame;
            if (regularly->timeOut < XTimerBase_getCurrentTime())
            {//时间到了
                regularly->timeOut = regularly->time + XTimerBase_getCurrentTime();
                //printf("时间到了:%\n");
                XModbus_sendFrame(modbus, XModbusFrame_copy(regularly->frame));

            }
            //重新指向新节点
            XContainerDataPtr(modbus->regularlySendMaster) = frontNode->next;
        }
    }
    //处理设备缓冲区
    if (modbus->eSndState == STATE_TX_XMIT ||XCircularQueue_isEmpty_base(modbus->ioDevice->m_readBuffer))
    {
       //printf("发送数据\n");
        modbus->pxMBFrameCBTransmitterEmpty(modbus);
#if MB_CALIBRATION_TIMER_SETTINGS //软件定时校准状态
        if (modbus->calibrationTimer_current + (modbus->timer->m_interval * 100) < XTimerBase_getCurrentTime())
        {
            printf("超时了\n");
            modbus->calibrationTimer_current = XTimerBase_getCurrentTime();
            //if (modbus->eRcvState == STATE_RX_RCV)
            //    XModbus_sendEvent(modbus, EV_FRAME_RECEIVED);
            //modbus->eRcvState = STATE_RX_IDLE;  // 切换到接收空闲状态
            //if (modbus->eSndState == STATE_TX_END)
            //    modbus->eSndState = STATE_TX_IDLE;
            XTimer_stop(modbus->timer);  // 关闭定时器
            modbus->pxMBPortCBTimerExpired(modbus);
        }
#endif // MB_CALIBRATION_TIMER_SETTINGS
    }
    else //if(modbus->eSndState != STATE_TX_XMIT)
    {
        //printf("处理接收数据\n");
        modbus->pxMBFrameCBByteReceived(modbus);
    }
    return error;
}
XModbusErrorCode XModbus_poll(XModbus* modbus)
{
    if (modbus == NULL)
        return MB_EINVAL;
    XModbusErrorCode error = MB_ENOERR;
    //printf("轮询中\n");
    //// 检查协议栈状态（必须已启用才能处理事件）
    if (modbus->state != STATE_ENABLED) {
        return MB_EILLSTATE; // 非法状态，直接返回错误
    }
    XModbusEventType eEvent;
    // 获取端口事件（如接收完成、定时器超时，驱动协议栈处理）
    if (!XCustomQueue_receive(modbus->eventQueue,&eEvent,0))
    {
        return XModbus_EventEmpty(modbus);
    }
#if MB_EVENT_HANDLE_SHOW
#if MB_ENUM_TO_STRING
    printf("准备处理事件:%s\n", XModbusEventType_toString(eEvent));
#else
    printf("准备处理事件:%d\n", eEvent);
#endif
#endif // MB_EVENT_SHOH
    //{ // 有事件待处理
    switch (eEvent)
    {
    case EV_FRAME_RECEIVED: XModbus_EV_FRAME_RECEIVED(modbus);  break; // 接收到完整的 Modbus 帧
    case EV_EXECUTE: XModbus_EV_EXECUTE(modbus); break;
    }
   return error; // 轮询成功（无错误或错误已处理）
}
//发送帧之前处理一些信息
static bool setSendFrame(XModbus* modbus, XModbusFrame* frame)
{
    if (modbus == NULL || frame == NULL)
        return false;
    if (frame->frameData == NULL || XVector_isEmpty_base(frame->frameData))
    {
        XModbusFrame_free(frame);
        return false;
    }
    //
    frame->mode = modbus->mode;
    //
    if (frame->recvHandle != NULL)
    {
        frame->recvHandle->waitAddressCode = XModbusFrame_getAddress(frame) << 8 | XModbusFrame_getFuncCode(frame);
        frame->recvHandle->timeout = XTimerBase_getCurrentTime() + MB_MASTER_RECV_OUT_TIME;//设置超时时间
    }
    return true;
}
XModbusErrorCode XModbus_sendFrame(XModbus* modbus, XModbusFrame* frame)
{
    if (modbus == NULL || frame == NULL)
        return MB_EINVAL;
   // printf("fa\n");
    if (setSendFrame(modbus, frame))
    {
        if (!XModbusFrameQueue_push(modbus->sendQueue, frame))
        {
#if MB_QUEUE_FULL_SHOW
            printf("发送帧队列溢出当前最大:%d,建议增大队列,调整:MB_FRAME_SEND_QUEUE_COUNT\n", MB_FRAME_SEND_QUEUE_COUNT);
#endif // MB_QUEUE_FULL_SHOW
            XModbusFrame_free(frame);
            return MB_ENORES;
        }
    }
    return MB_ENOERR;
}

XModbusErrorCode XModbus_sendFrameRegularlyMaster(XModbus* modbus, XModbusFrame* frame, uint32_t time)
{
    if (modbus == NULL || frame == NULL)
        return MB_EINVAL;
    if (setSendFrame(modbus, frame))
    {
        XModbusRegularlySendFrame regularly = { 0 };
        regularly.frame = frame;
        regularly.time = time;
        regularly.timeOut = time + XTimerBase_getCurrentTime();
        XListDLinked_push_back_base(modbus->regularlySendMaster, &regularly);
    }
    return MB_ENOERR;
}

bool XModbus_isMaster(XModbus* modbus)
{
    return modbus->mode % 2 == 0;
}

XModbusErrorCode XModbus_setFunctionHandler(XModbus* modbus, XModbusFunctionHandler* FunctionHandler)
{
    if (modbus==NULL)
        return MB_EINVAL;
    if (FunctionHandler == NULL)
        return MB_EINVAL;
    XModbusFuncCodeList_push(modbus->funcCodeList, FunctionHandler);
    return MB_ENOERR;
}

bool XModbus_sendEvent(XModbus* modbus, XModbusEventType event)
{
    //XModbusEventType event = EV_EXECUTE;
    if (!XCustomQueue_push(modbus->eventQueue, &event))
    {
#if MB_QUEUE_FULL_SHOW
        printf("事件队列溢出当前最大:%d,建议增大队列,调整:MB_EVENT_QUEUE_COUNT\n", MB_EVENT_QUEUE_COUNT);
#endif // MB_QUEUE_FULL_SHOW
        return false;
    }
    return true;
}
