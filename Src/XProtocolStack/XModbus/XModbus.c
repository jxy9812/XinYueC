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
    modbus->recvQueue=XModbusFrameQueue_new();
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
    ((char*)(XVector_begin(modbus->recvBuffer)))[XVector_size(modbus->recvBuffer)] = 0;
    printf("数据:%s  大小:%d buff接收缓冲区大小:%d\n",XVector_begin(modbus->recvBuffer), XVector_size(modbus->recvBuffer), XVector_capacity(modbus->recvBuffer));
    // 调用对应模式的接收函数，获取帧数据（地址、缓冲区、长度）
    XModbusFrameData dataFrame = XModbusFrameData_new();
    modbus->errorCode = modbus->peMBFrameReceiveCur(modbus,&dataFrame);
    if (modbus->errorCode == MB_ENOERR) {
        //            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
        if ((dataFrame.address == modbus->address) || (dataFrame.address == MB_ADDRESS_BROADCAST)) 
        {
            XModbusFrameQueue_push(modbus->recvQueue, &dataFrame);
            XEventQueue_push(modbus->eventQueue, EV_EXECUTE);
            //                xMBPortEventPost(EV_EXECUTE); // 触发功能码执行事件
        }
        //        }
    }
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
        case EV_EXECUTE: { // 执行功能码处理（接收到有效帧后触发）
    //        ucFunctionCode = ucMBFrame[MB_PDU_FUNC_OFF]; // 提取功能码（位于 PDU 起始位置）
    //        eException = MB_EX_ILLEGAL_FUNCTION; // 初始化为非法功能码异常

    //        // 查找功能码对应的处理函数（遍历注册的处理函数表）
    //        for (i = 0; i < MB_FUNC_HANDLERS_MAX; i++) {
    //            if (xFuncHandlers[i].ucFunctionCode == 0) {
    //                break; // 遇到空槽位，停止查找（处理函数表已遍历完）
    //            }
    //            else if (xFuncHandlers[i].ucFunctionCode == ucFunctionCode) {
    //                // 调用处理函数，获取异常码（正常处理返回 MB_EX_NONE，异常返回对应错误码）
    //                eException = xFuncHandlers[i].pxHandler(ucMBFrame, &usLength);
    //                break; // 找到匹配的处理函数，停止查找
                }
           // }

    //        // 非广播地址需要返回响应（广播地址请求无需响应）
    //        if (ucRcvAddress != MB_ADDRESS_BROADCAST) {
    //            if (eException != MB_EX_NONE) { // 处理过程中发生异常，构建错误帧
    //                usLength = 0;
    //                ucMBFrame[usLength++] = ucFunctionCode | MB_FUNC_ERROR; // 错误功能码（原功能码 | 0x80）
    //                ucMBFrame[usLength++] = eException; // 添加异常码
    //            }
    //            // ASCII 模式发送前添加延迟（根据配置，确保字符间隔符合协议要求）
    //            if ((eMBCurrentMode == MB_ASCII) && MB_ASCII_TIMEOUT_WAIT_BEFORE_SEND_MS) {
    //                vMBPortTimersDelay(MB_ASCII_TIMEOUT_WAIT_BEFORE_SEND_MS); // 平台特定的延迟函数
    //            }
    //            // 调用对应模式的发送函数，返回响应帧
    //            eStatus = peMBFrameSendCur(ucMBAddress, ucMBFrame, usLength);
    //        }
    //        break;
    //    }
    //                   // 其他事件（如 EV_READY、EV_FRAME_SENT）暂时忽略，留空处理
    //    case EV_READY:
    //    case EV_FRAME_SENT:
    //        break;
        }
   XEventQueue_pop(modbus->eventQueue);
   return modbus->errorCode; // 轮询成功（无错误或错误已处理）
}
