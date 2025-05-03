#include "XModbus.h"
#include "XMemory.h"
#include "XModbusConfig.h"
#include "XModbusProto.h"
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

void XModbus_init(XModbus* modbus,size_t bufferSize, XModbusMode mode, XModbusGetByte xGetByte, XModbusPutByte xPutByte)
{
	if (modbus == NULL)
		return;
	modbus->recvBuffer = XVector_New(uint8_t);
	XVector_resize(modbus->recvBuffer,bufferSize);
	modbus->sendQueue=XQueue_New(XVector*);
    modbus->recvFrameQueue=XModbusFrameQueue_new();
	modbus->eventQueue = XEventQueue_new(XEventQueue_defaultConfigInit);
	modbus->mode = mode;
	modbus->xGetByte = xGetByte;
	modbus->xPutByte = xPutByte;
    modbus->errorCode = MB_ENOERR;
    // 根据选择的模式初始化对应的函数指针和底层模块
    switch (mode) {
#if MB_RTU_ENABLED > 0
    case MB_RTU_MASTER: 
    case MB_RTU_SLAVE:
        XModbusRtuInit(modbus);
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
        modbus->errorCode = MB_EINVAL; // 不支持的模式错误
        return;
    }

    // 初始化成功后配置协议栈状态
    if (modbus->errorCode == MB_ENOERR) {
        // 初始化端口事件模块（如事件队列，用于驱动协议栈轮询）
        if (modbus->eventQueue==NULL) {
            modbus->errorCode = MB_EPORTERR; // 端口事件初始化失败（平台相关错误）
        }
        else {
            //eMBCurrentMode = eMode; // 记录当前工作模式
            modbus->state = STATE_DISABLED; // 初始化后状态为“禁用”，需调用 eMBEnable 激活
        }
    }
//}
}

XModbus* XModbus_new( size_t bufferSize, XModbusMode mode, XModbusGetByte xGetByte, XModbusPutByte xPutByte)
{
	XModbus* modbus = XMemory_malloc(sizeof(XModbus));
	if(modbus==NULL)
		return modbus;
	//正式初始化
	XModbus_init(modbus,bufferSize,mode,xGetByte,xPutByte);
}
XModbusErrorCode XModbus_enable(XModbus* modbus)
{
    //printf("mode\n");
    if (modbus && modbus->pvMBFrameStartCur)
    {
        if (modbus->state == STATE_DISABLED) {
            modbus->pvMBFrameStartCur(modbus); // 启动协议栈（初始化端口资源，开始接收/发送数据）
            modbus->state = STATE_ENABLED; // 更新状态为启用
        }
        else {
            modbus->errorCode = MB_EILLSTATE; // 非法状态（已启用或未初始化）
        }
        return modbus->errorCode;
    }
    return MB_EINVAL;
}
XModbusErrorCode XModbus_disable(XModbus* modbus)
{
    if (modbus && modbus->pvMBFrameStopCur)
    {
        if (modbus->state == STATE_ENABLED) { // 从启用状态禁用
            modbus->pvMBFrameStopCur(modbus); // 停止协议栈（暂停接收/发送，清理临时资源）
            modbus->state = STATE_DISABLED; // 更新状态为禁用
            modbus->errorCode = MB_ENOERR;
        }
        else if (modbus->state == STATE_DISABLED) { // 已禁用状态，直接返回成功
            modbus->errorCode = MB_ENOERR;
        }
        else { // 未初始化状态
            modbus->errorCode = MB_EILLSTATE; // 非法状态错误
        }
        return modbus->errorCode;
    }
    return MB_EINVAL;
   
}
// 接收到完整的 Modbus 帧
static void XModbus_EV_FRAME_RECEIVED(XModbus* modbus)
{
   /* ((char*)(XVector_begin(modbus->recvBuffer)))[XVector_size(modbus->recvBuffer)] = 0;
    printf("数据:%s  大小:%d buff接收缓冲区大小:%d\n",XVector_begin(modbus->recvBuffer), XVector_size(modbus->recvBuffer), XVector_capacity(modbus->recvBuffer));*/
    // 调用对应模式的接收函数，获取帧数据（地址、缓冲区、长度）
    XModbusFrameData* dataFrame = XModbusFrameData_new();
    //解析数据帧
    modbus->peMBFrameReceiveCur(modbus,dataFrame);
   
    if (modbus->errorCode == MB_ENOERR) {
        UCHAR address= XModbusFrameData_getRtuAddress(dataFrame);
        UCHAR code= XModbusFrameData_getRtuFuncCode(dataFrame);
        //XString* str= XModbusFrameData_to16HexString(dataFrame);
        //printf("地址:%X 功能码:%X 完整:%s\n", address,code,XString_c_str(str));
        ////            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
        //XString_free(str);
        if (modbus->mode % 2 == 0)
        {
            //printf("当前是主站\n");
           
        }
        else if ((address == modbus->address) || (address == MB_ADDRESS_BROADCAST))
        {
            //printf("将有效的帧加入接收队列处理\n");
            XModbusFrameQueue_push(modbus->recvFrameQueue, dataFrame);
            XEventQueue_push(modbus->eventQueue, EV_EXECUTE);
            //                xMBPortEventPost(EV_EXECUTE); // 触发功能码执行事件
        }
        else
        {
            XModbusFrameData_free(dataFrame);
        }
    }
    else
    {
        XModbusFrameData_free(dataFrame);
    }
    
}
//功能码处理
static void  XModbus_EV_EXECUTE(XModbus* modbus)
{
    //printf("处理功能码\n");
    if (modbus == NULL || XModbusFrameQueue_empty(modbus->recvFrameQueue))
        return;
   
    XModbusFrameData* frame = XModbusFrameQueue_Top(modbus->recvFrameQueue);
    UCHAR address = XModbusFrameData_getRtuAddress(frame);
    UCHAR code = XModbusFrameData_getRtuFuncCode(frame);
    XString* str = XModbusFrameData_to16HexString(frame);
    printf("地址:%X 功能码:%X 完整:%s\n", address, code, XString_c_str(str));
    //            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
    XString_free(str);



    //释放一个资源
    XModbusFrameQueue_pop(modbus->recvFrameQueue);
}
XModbusErrorCode XModbus_poll(XModbus* modbus)
{
    if (modbus == NULL)
        return MB_EINVAL;
    //XModbusErrorCode eStatus = MB_ENOERR;
    //printf("轮询中");
    //// 检查协议栈状态（必须已启用才能处理事件）
    if (modbus->state != STATE_ENABLED) {
        modbus->errorCode = MB_EILLSTATE;
        return MB_EILLSTATE; // 非法状态，直接返回错误
    }
    //return modbus->errorCode;
    // 获取端口事件（如接收完成、定时器超时，驱动协议栈处理）
    if (XEventQueue_empty(modbus->eventQueue))
    {
        modbus->errorCode= MB_ENOERR;
        return modbus->errorCode;
    }
   
    //从队列中获取事件
    XModbusEventType eEvent = XEventQueue_Top(modbus->eventQueue);
    printf("有新的事件%d\n", eEvent);
    //{ // 有事件待处理
        switch (eEvent) {
        case EV_FRAME_RECEIVED: XModbus_EV_FRAME_RECEIVED(modbus);  break; // 接收到完整的 Modbus 帧
        case EV_EXECUTE: XModbus_EV_EXECUTE(modbus); break;
    //                   // 其他事件（如 EV_READY、EV_FRAME_SENT）暂时忽略，留空处理
    //    case EV_READY:
    //    case EV_FRAME_SENT:
    //        break;
        }
   XEventQueue_pop(modbus->eventQueue);
   return modbus->errorCode; // 轮询成功（无错误或错误已处理）
}
