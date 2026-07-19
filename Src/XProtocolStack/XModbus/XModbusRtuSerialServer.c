#include "XModbusRtuSerialServer.h"
#include "XModbusRtuSerialServer_Protected.h"
#include "XModbusServer_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XCrc.h"
#include "XTimer.h"
#include "XIODevice.h"
#include "XSignalSlot.h"
#include "string.h"

// =============== 虚函数前置声明 ================
static bool VXModbusRtuSerialServer_open(XModbusDevice* device);
static void VXModbusRtuSerialServer_close(XModbusDevice* device);
static void VXModbusRtuSerialServer_deinit(XModbusRtuSerialServer* server);
static XModbusResponse* VXModbusRtuSerialServer_processRequest(XModbusServer* server, const XModbusRequest* request);
static bool VXModbusRtuSerialServer_processesBroadcast(const XModbusServer* server);
static void VXModbusRtuSerialServer_timerEvent(XObject* obj, XTimerEvent* event);

// =============== 槽函数前置声明 ================
static void XModbusRtuSerialServer_onReadyRead(XObject* receiver, XVarList* args);
static void XModbusRtuSerialServer_onErrorOccurred(XObject* receiver, XVarList* args);

// =============== 辅助函数 ================

/**
 * @brief 计算CRC16校验值
 */
static inline uint16_t calculateCrc16(const uint8_t* data, size_t len)
{
    return XCrc_get16((uint8_t*)data, len);
}

/**
 * @brief 验证RTU帧的CRC校验
 * @param frame 帧数据
 * @param frameLen 帧长度（包含CRC的2字节）
 * @return 校验通过返回true，否则返回false
 */
static inline bool validateRtuFrame(const uint8_t* frame, size_t frameLen)
{
    if (!frame || frameLen < 4) return false;

    uint16_t calculatedCrc = calculateCrc16(frame, frameLen - 2);

    // Modbus RTU CRC 是小端序
    uint16_t receivedCrc;
    XMemory_read_data(frame + frameLen - 2, XBYTE_ORDER_LITTLE_ENDIAN,
        (uint8_t*)&receivedCrc, sizeof(uint16_t));

    return calculatedCrc == receivedCrc;
}

/**
 * @brief 计算帧间延迟（基于波特率）
 * @param baudRate 波特率
 * @return 帧间延迟（微秒）
 * @note 3.5个字符传输时间，每个字符11位（1起始+8数据+1校验+1停止）
 */
static inline int calculateInterFrameDelay(int baudRate)
{
    if (baudRate <= 0) return 1750;
    // 3.5个字符 * 11位/字符 * 1000000微秒/秒 / 波特率
    int delay = (int)(38500000 / baudRate);
    return delay < 1750 ? 1750 : delay;
}

/**
 * @brief 停止帧间延迟定时器
 */
static inline void interFrameTimerStop(XModbusRtuSerialServer* server)
{
    if (server->m_interFrameTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)server, server->m_interFrameTimer);
        server->m_interFrameTimer = XTIMER_INVALID_ID;
    }
}

/**
 * @brief 启动帧间延迟定时器
 */
static inline void interFrameTimerStart(XModbusRtuSerialServer* server)
{
    interFrameTimerStop(server);
    int interFrameDelayMs = (server->m_interFrameDelay + 999) / 1000;
    if (interFrameDelayMs < 1) interFrameDelayMs = 1;
    server->m_interFrameTimer = XObject_startTimer_ms((XObject*)server,
        interFrameDelayMs, XTimerType_CoarseTimer);
}

/**
 * @brief 停止接收缓冲区定时器
 */
static inline void receiveBufferTimerStop(XModbusRtuSerialServer* server)
{
    if (server->m_receiveBufferTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)server, server->m_receiveBufferTimer);
        server->m_receiveBufferTimer = XTIMER_INVALID_ID;
    }
}

/**
 * @brief 启动接收缓冲区定时器（用于帧超时检测）
 * @param server XModbusRtuSerialServer实例指针
 * @note 定时器超时时间 = 帧间延迟 + 1ms余量
 */
static inline void receiveBufferTimerStart(XModbusRtuSerialServer* server)
{
    receiveBufferTimerStop(server);
    int timeoutMs = (server->m_interFrameDelay + 999) / 1000 + 1;
    if (timeoutMs < 2) timeoutMs = 2;
    server->m_receiveBufferTimer = XObject_startTimer_ms((XObject*)server,
        timeoutMs, XTimerType_CoarseTimer);
}

/**
 * @brief 停止响应延迟定时器
 */
static inline void turnaroundTimerStop(XModbusRtuSerialServer* server)
{
    if (server->m_turnaroundTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)server, server->m_turnaroundTimer);
        server->m_turnaroundTimer = XTIMER_INVALID_ID;
    }
}

/**
 * @brief 启动响应延迟定时器
 */
static inline void turnaroundTimerStart(XModbusRtuSerialServer* server)
{
    turnaroundTimerStop(server);
    if (server->m_turnaroundDelay > 0) {
        server->m_turnaroundTimer = XObject_startTimer_ms((XObject*)server,
            server->m_turnaroundDelay, XTimerType_CoarseTimer);
    }
}

/**
 * @brief 构建RTU响应帧并发送
 * @param server XModbusRtuSerialServer实例指针
 * @param serverAddress 从站地址
 * @param response 响应PDU
 */
static void sendRtuResponse(XModbusRtuSerialServer* server,
    uint8_t serverAddress, const XModbusResponse* response)
{
    if (!server || !response) return;

    // 获取PDU数据
    XByteArray* pduData = ((XModbusPdu*)response)->m_data;
    size_t pduDataSize = XByteArray_size_base(pduData);

    // 构建帧：地址(1) + 功能码(1) + 数据(N) + CRC(2)
    size_t frameSize = 1 + 1 + pduDataSize + 2;
    uint8_t* frame = (uint8_t*)XMalloc_System(frameSize);
    if (!frame) return;

    frame[0] = serverAddress;
    frame[1] = (uint8_t)XModbusPdu_functionCode(&response->m_base);
    if (pduDataSize > 0) {
        memcpy(frame + 2, XContainerDataAddr(pduData), pduDataSize);
    }

    // 计算并追加CRC
    uint16_t crc = calculateCrc16(frame, frameSize - 2);
    XMemory_write_data(frame + frameSize - 2, XBYTE_ORDER_LITTLE_ENDIAN,
        (const uint8_t*)&crc, sizeof(uint16_t));

    // 通过串口发送
    XIODevice_write_1((XIODevice*)server->m_serialPort, (const char*)frame, frameSize);

    XFree_System(frame);
}

/**
 * @brief 处理接收到的完整RTU帧
 */
static void processRtuFrame(XModbusRtuSerialServer* server,
    const uint8_t* frame, size_t frameLen)
{
    if (!server || !frame || frameLen < 4) return;

    // 验证CRC
    if (!validateRtuFrame(frame, frameLen)) {
        return;
    }

    // 提取地址
    uint8_t serverAddress = frame[0];

    // 检查是否广播或发给自己的
    int myAddress = XModbusServer_serverAddress((XModbusServer*)server);
    bool isBroadcast = (serverAddress == 0);
    bool isForMe = (serverAddress == (uint8_t)myAddress);

    if (!isBroadcast && !isForMe) {
        return;
    }

    // 提取PDU：功能码(1) + 数据(N)
    size_t pduLen = frameLen - 3; // 减去地址(1)和CRC(2)
    if (pduLen < 1) return;

    // 创建请求PDU
    XModbusRequest* request = XModbusRequest_create();
    if (!request) return;

    XModbusPdu_setFunctionCode(request, (XModbusPdu_FunctionCode)frame[1]);
    if (pduLen > 1) {
        XModbusPdu_setData(request, frame + 2, pduLen - 1);
    }

    // 广播请求不发送响应
    if (isBroadcast) {
        // 处理请求但不发送响应
        XModbusResponse* response = XModbusServer_processRequest_base(
            (XModbusServer*)server, request);
        if (response) {
            XModbusResponse_delete_base(response);
        }
        XModbusRequest_delete_base(request);
        return;
    }

    // 通过虚函数调用processRequest
    XModbusResponse* response = XModbusServer_processRequest_base(
        (XModbusServer*)server, request);

    if (response) {
        sendRtuResponse(server, serverAddress, response);
        XModbusResponse_delete_base(response);
    }

    XModbusRequest_delete_base(request);
}

/**
 * @brief 处理接收缓冲区中的所有完整帧
 */
static void processReceiveBuffer(XModbusRtuSerialServer* server)
{
    if (!server || !server->m_receiveBuffer) return;

    XByteArray* buffer = server->m_receiveBuffer;
    const uint8_t* data = XContainerDataAddr(buffer);
    size_t dataSize = XByteArray_size_base(buffer);

    // 持续处理缓冲区中的完整帧
    // 最小RTU帧：地址(1) + 功能码(1) + CRC(2) = 4字节
    while (dataSize >= 4) {
        // 验证帧完整性（通过CRC验证）
        // 从最小4字节开始，逐步增大尝试
        bool frameFound = false;
        size_t frameLen = 0;

        // 尝试从当前位置到缓冲区末尾查找完整帧
        // 遍历可能的帧长度（从4字节到dataSize）
        for (size_t len = 4; len <= dataSize; len++) {
            if (validateRtuFrame(data, len)) {
                frameFound = true;
                frameLen = len;
                break;
            }
        }

        if (!frameFound) {
            // 没有找到完整帧，等待更多数据
            // 但如果数据量太大，可能是无效数据，清除
            if (dataSize > 1024) {
                XContainer_clear_base((XContainer*)buffer);
            }
            break;
        }

        // 处理找到的完整帧
        processRtuFrame(server, data, frameLen);

        // 移除已处理的数据
        XByteArray_remove_base(buffer, 0, frameLen);
        data = XContainerDataAddr(buffer);
        dataSize = XByteArray_size_base(buffer);
    }
}

// =============== 类初始化 ================
XVtable* XModbusRtuSerialServer_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusRtuSerialServer))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承 XModbusServer
    XVTABLE_INHERIT_XCLASS(XModbusServer);

    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXModbusRtuSerialServer_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXModbusRtuSerialServer_close);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessRequest, VXModbusRtuSerialServer_processRequest);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessesBroadcast, VXModbusRtuSerialServer_processesBroadcast);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusRtuSerialServer_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusRtuSerialServer_deinit);

#if SHOWCONTAINERSIZE
    printf("XModbusRtuSerialServer size: %zu\n", sizeof(XModbusRtuSerialServer));
#endif
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ================
XModbusRtuSerialServer* XModbusRtuSerialServer_create(void)
{
    XModbusRtuSerialServer* server = XMalloc_System(sizeof(XModbusRtuSerialServer));
    if (!server) return NULL;
    XModbusRtuSerialServer_init(server);
    Set_Class_MemoryFree(server, XFree_System);
    return server;
}

void XModbusRtuSerialServer_init(XModbusRtuSerialServer* server)
{
    if (!server) return;

    // 初始化基类
    XModbusServer_init(&server->m_base);
    XClassGetVtable(server) = XModbusRtuSerialServer_class_init();

    // 创建串口对象
    server->m_serialPort = XSerialPort_create();
    if (server->m_serialPort) {
        // 设置默认波特率
        XSerialPort_setBaudRate(server->m_serialPort, XSerialPort_Baud19200, XSerialPort_AllDirections);
    }

    // 初始化帧间延迟（0表示自动计算）
    server->m_interFrameDelay = 0;
    server->m_turnaroundDelay = 100; // 默认100ms响应延迟

    // 创建接收缓冲区
    server->m_receiveBuffer = XByteArray_create();

    // 初始化定时器ID
    server->m_interFrameTimer = XTIMER_INVALID_ID;
    server->m_receiveBufferTimer = XTIMER_INVALID_ID;
    server->m_turnaroundTimer = XTIMER_INVALID_ID;
}

// =============== 串口访问 ================
XSerialPort* XModbusRtuSerialServer_serialPort(const XModbusRtuSerialServer* server)
{
    return server ? server->m_serialPort : NULL;
}

// =============== 帧间延迟 ================
int XModbusRtuSerialServer_interFrameDelay(const XModbusRtuSerialServer* server)
{
    if (!server) return -1;

    if (server->m_interFrameDelay != 0) {
        return server->m_interFrameDelay;
    }

    // 自动计算
    if (server->m_serialPort) {
        int baudRate = (int)XSerialPort_baudRate(server->m_serialPort, XSerialPort_AllDirections);
        return calculateInterFrameDelay(baudRate);
    }

    return 1750; // 默认值
}

void XModbusRtuSerialServer_setInterFrameDelay(XModbusRtuSerialServer* server, int microseconds)
{
    if (server) {
        server->m_interFrameDelay = microseconds;
    }
}

// =============== 虚函数实现 ================

static bool VXModbusRtuSerialServer_open(XModbusDevice* device)
{
    XModbusRtuSerialServer* server = (XModbusRtuSerialServer*)device;
    if (!server || !server->m_serialPort) {
        XModbusDevice_setError(device, XModbusDevice_ConfigurationError, "No serial port available");
        return false;
    }

    // 如果已经打开，先关闭
    if (XIODevice_isOpen((XIODevice*)server->m_serialPort)) {
        XIODevice_close_base((XIODevice*)server->m_serialPort);
    }

    // 打开串口
    if (!XIODevice_open_base((XIODevice*)server->m_serialPort, XIODevice_ReadWrite)) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Failed to open serial port");
        XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
        return false;
    }

    // 清除接收缓冲区
    XContainer_clear_base((XContainer*)server->m_receiveBuffer);

    // 更新帧间延迟（如果设置为自动计算）
    if (server->m_interFrameDelay == 0) {
        int baudRate = (int)XSerialPort_baudRate(server->m_serialPort, XSerialPort_AllDirections);
        server->m_interFrameDelay = calculateInterFrameDelay(baudRate);
    }

    // 连接串口信号
    XObject_connect_1((XObject*)server->m_serialPort,
        XSignal(XIODevice_readyRead_signal),
        (XObject*)server,
        XModbusRtuSerialServer_onReadyRead,
        XConnectionType_Auto);

    XObject_connect_1((XObject*)server->m_serialPort,
        XSignal(XSerialPort_errorOccurred_signal),
        (XObject*)server,
        XModbusRtuSerialServer_onErrorOccurred,
        XConnectionType_Auto);

    // 设置设备状态
    XModbusDevice_setError(device, XModbusDevice_NoError, NULL);
    XModbusDevice_setState(device, XModbusDevice_ConnectedState);

    return true;
}

static void VXModbusRtuSerialServer_close(XModbusDevice* device)
{
    XModbusRtuSerialServer* server = (XModbusRtuSerialServer*)device;
    if (!server) return;

    // 停止所有定时器
    interFrameTimerStop(server);
    receiveBufferTimerStop(server);
    turnaroundTimerStop(server);

    // 断开信号连接
    if (server->m_serialPort) {
        XObject_disconnect_1((XObject*)server->m_serialPort, 0, (XObject*)server, NULL);
    }

    // 清除接收缓冲区
    if (server->m_receiveBuffer) {
        XContainer_clear_base((XContainer*)server->m_receiveBuffer);
    }

    // 关闭串口
    if (server->m_serialPort && XIODevice_isOpen((XIODevice*)server->m_serialPort)) {
        XIODevice_close_base((XIODevice*)server->m_serialPort);
    }

    // 设置设备状态
    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

static XModbusResponse* VXModbusRtuSerialServer_processRequest(
    XModbusServer* baseServer, const XModbusRequest* request)
{
    // RTU Server 的 processRequest 委托给基类实现
    // 基类 XModbusServer_processRequest_base 会处理标准功能码
    return XModbusServer_processRequest_base(baseServer, request);
}

static bool VXModbusRtuSerialServer_processesBroadcast(const XModbusServer* server)
{
    (void)server;
    // RTU服务器支持广播地址（地址0）
    return true;
}

static void VXModbusRtuSerialServer_timerEvent(XObject* obj, XTimerEvent* event)
{
    XModbusRtuSerialServer* server = (XModbusRtuSerialServer*)obj;
    if (!server || !event) return;

    XTimerId timerId = event->timerId;

    // 帧间延迟定时器触发
    if (timerId == server->m_interFrameTimer) {
        server->m_interFrameTimer = XTIMER_INVALID_ID;
        return;
    }

    // 接收缓冲区超时定时器触发
    if (timerId == server->m_receiveBufferTimer) {
        server->m_receiveBufferTimer = XTIMER_INVALID_ID;
        // 接收缓冲区超时，尝试处理缓冲区中的完整帧
        processReceiveBuffer(server);
        return;
    }

    // 响应延迟定时器触发
    if (timerId == server->m_turnaroundTimer) {
        server->m_turnaroundTimer = XTIMER_INVALID_ID;
        return;
    }
}

static void VXModbusRtuSerialServer_deinit(XModbusRtuSerialServer* server)
{
    if (!server) return;

    // 停止所有定时器
    interFrameTimerStop(server);
    receiveBufferTimerStop(server);
    turnaroundTimerStop(server);

    // 释放接收缓冲区
    if (server->m_receiveBuffer) {
        XByteArray_delete_base(server->m_receiveBuffer);
        server->m_receiveBuffer = NULL;
    }

    // 释放串口对象
    if (server->m_serialPort) {
        if (XIODevice_isOpen((XIODevice*)server->m_serialPort)) {
            XIODevice_close_base((XIODevice*)server->m_serialPort);
        }
        XIODevice_deleteLater((XIODevice*)server->m_serialPort);
        server->m_serialPort = NULL;
    }

    server->m_interFrameDelay = 0;
    server->m_turnaroundDelay = 0;
}

// =============== 槽函数 ================

/**
 * @brief 处理串口数据到达
 */
static void XModbusRtuSerialServer_onReadyRead(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusRtuSerialServer* server = (XModbusRtuSerialServer*)receiver;
    if (!server || !server->m_serialPort || !server->m_receiveBuffer) return;

    // 读取所有可用数据到接收缓冲区
    char tempBuf[4096];
    int64_t bytesRead = XIODevice_read_1((XIODevice*)server->m_serialPort, tempBuf, sizeof(tempBuf));
    while (bytesRead > 0) {
        XByteArray_push_back_2(server->m_receiveBuffer,
            (const uint8_t*)tempBuf, (size_t)bytesRead);
        bytesRead = XIODevice_read_1((XIODevice*)server->m_serialPort, tempBuf, sizeof(tempBuf));
    }

    // 启动接收缓冲区超时定时器
    // 当数据停止到达时，定时器触发后会尝试处理帧
    receiveBufferTimerStart(server);
}

/**
 * @brief 处理串口错误
 */
static void XModbusRtuSerialServer_onErrorOccurred(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusRtuSerialServer* server = (XModbusRtuSerialServer*)receiver;
    if (!server) return;

    XModbusDevice* device = (XModbusDevice*)server;

    // 停止所有定时器
    interFrameTimerStop(server);
    receiveBufferTimerStop(server);
    turnaroundTimerStop(server);

    // 设置设备错误状态
    XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Serial port error");
    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}
