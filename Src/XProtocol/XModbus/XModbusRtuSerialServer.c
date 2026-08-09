#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_RTU_ON
#include "XModbusRtuSerialServer.h"
#include "XModbusRtuSerialServer_Protected.h"
#include "XModbusServer_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XModbusAdu.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XCrc.h"
#include "XTimer.h"
#include "XIODevice.h"
#include "XSignalSlot.h"
#include "string.h"

// =============== 常量定义 ================
/** @brief 帧间延迟计算因子（3.5字符 * 11位/字符 * 1000000微秒） */
#define MODBUS_RTU_INTER_FRAME_FACTOR      38500000
/** @brief 最小帧间延迟（微秒），对应最低波特率 */
#define MODBUS_RTU_INTER_FRAME_DELAY_MIN_US 1750
/** @brief 接收缓冲区最大大小（字节），超过此值视为无效数据并清除 */
#define MODBUS_RTU_RECEIVE_BUFFER_MAX_SIZE  1024

// =============== 虚函数前置声明 ================
static bool VXModbusRtuSerialServer_open(XModbusDevice* device);
static void VXModbusRtuSerialServer_close(XModbusDevice* device);
static void VXModbusRtuSerialServer_deinit(XModbusRtuSerialServer* server);
static bool VXModbusRtuSerialServer_processesBroadcast(const XModbusServer* server);
static void VXModbusRtuSerialServer_timerEvent(XObject* obj, XTimerEvent* event);

// =============== 槽函数前置声明 ================
static void XModbusRtuSerialServer_onReadyRead(XObject* receiver, XVarList* args);
static void XModbusRtuSerialServer_onErrorOccurred(XObject* receiver, XVarList* args);

// =============== 辅助函数 ================

/**
 * @brief 计算帧间延迟（基于波特率）
 * @param baudRate 波特率
 * @return 帧间延迟（微秒）
 * @note 3.5个字符传输时间，每个字符11位（1起始+8数据+1校验+1停止）
 */
static inline int calculateInterFrameDelay(int baudRate)
{
    if (baudRate <= 0) return MODBUS_RTU_INTER_FRAME_DELAY_MIN_US;
    // 3.5个字符 * 11位/字符 * 1000000微秒/秒 / 波特率
    int delay = (int)(MODBUS_RTU_INTER_FRAME_FACTOR / baudRate);
    return delay < MODBUS_RTU_INTER_FRAME_DELAY_MIN_US ? MODBUS_RTU_INTER_FRAME_DELAY_MIN_US : delay;
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
 * @brief 构建RTU响应帧并发送（使用 XModbusAdu 封装）
 * @param server XModbusRtuSerialServer实例指针
 * @param serverAddress 从站地址
 * @param response 响应PDU
 */
static void sendRtuResponse(XModbusRtuSerialServer* server,
    uint8_t serverAddress, const XModbusResponse* response)
{
    if (!server || !response) return;

    // 使用 XModbusAdu 创建 RTU 响应帧
    XByteArray* frame = XModbusAdu_createRtuFrame(serverAddress, (const XModbusPdu*)response);
    if (!frame) return;

    // 通过串口发送
    XIODevice_write_1((XIODevice*)server->m_serialPort,
        (const char*)XByteArray_data(frame), XByteArray_size_base(frame));

    XByteArray_delete_base(frame);
}

/**
 * @brief 处理接收到的完整RTU帧（使用 XModbusAdu 解析）
 */
static void processRtuFrame(XModbusRtuSerialServer* server,
    const uint8_t* frame, size_t frameLen)
{
    if (!server || !frame || frameLen < 4) return;

    // 使用 XModbusAdu 解析RTU帧
    XModbusAdu* adu = XModbusAdu_parseRtu(frame, frameLen);
    if (!adu) return;

    // 校验CRC
    if (!XModbusAdu_matchingChecksum(adu)) {
        XModbusAdu_delete(adu);
        return;
    }

    // 提取地址
    int serverAddress = XModbusAdu_serverAddress(adu);
    if (serverAddress < 0) {
        XModbusAdu_delete(adu);
        return;
    }

    // 检查是否广播或发给自己的
    int myAddress = XModbusServer_serverAddress((XModbusServer*)server);
    bool isBroadcast = (serverAddress == 0);
    bool isForMe = (serverAddress == myAddress);

    if (!isBroadcast && !isForMe) {
        XModbusAdu_delete(adu);
        return;
    }

    // 提取PDU
    XModbusPdu pdu;
    if (!XModbusAdu_pdu(adu, &pdu)) {
        XModbusAdu_delete(adu);
        return;
    }

    // 创建请求PDU对象
    XModbusRequest* request = XModbusRequest_create();
    if (!request) {
        XModbusPdu_deinit_base(&pdu);
        XModbusAdu_delete(adu);
        return;
    }

    // 设置功能码
    XModbusPdu_setFunctionCode(request, XModbusPdu_functionCodeRaw(&pdu));

    // 设置数据
    XByteArray* pduData = XModbusPdu_data(&pdu);
    if (pduData) {
        XModbusPdu_setData((XModbusPdu*)request,
            XByteArray_data(pduData), XByteArray_size_base(pduData));
        XByteArray_delete_base(pduData);
    }
    XModbusPdu_deinit_base(&pdu);

    // 广播请求不发送响应
    if (isBroadcast) {
        XModbusResponse* response = XModbusServer_processRequest_base(
            (XModbusServer*)server, request);
        if (response) {
            XModbusResponse_delete_base(response);
        }
        XModbusRequest_delete_base(request);
        XModbusAdu_delete(adu);
        return;
    }

    // 通过虚函数调用processRequest
    XModbusResponse* response = XModbusServer_processRequest_base(
        (XModbusServer*)server, request);

    if (response) {
        sendRtuResponse(server, (uint8_t)serverAddress, response);
        XModbusResponse_delete_base(response);
    }

    XModbusRequest_delete_base(request);
    XModbusAdu_delete(adu);
}

/**
 * @brief 根据 Modbus RTU 帧结构计算帧长度（取代遍历查找）
 * @param data 帧数据起始指针
 * @param dataSize 可用数据大小
 * @return 完整帧长度，-1 表示无法确定（未知功能码或数据不足）
 * @note 通过功能码直接计算 PDU 长度，避免 O(N²) 的遍历解析
 */
static int calculateRtuFrameLength(const uint8_t* data, size_t dataSize)
{
    if (!data || dataSize < 4) return -1;  // 最小帧：地址(1) + 功能码(1) + CRC(2)

    uint8_t functionCode = data[1];

    switch (functionCode) {
    case 0x01: case 0x02: case 0x03: case 0x04:
    case 0x05: case 0x06:
        // 地址(1) + 功能码(1) + 数据(4) + CRC(2) = 8
        // 读请求：起始地址(2) + 数量(2)
        // 写单个：地址(2) + 值(2)
        return 8;

    case 0x07: case 0x0B: case 0x0C: case 0x11:
        // 地址(1) + 功能码(1) + CRC(2) = 4
        // 读异常状态/事件计数器/事件日志/服务器ID
        return 4;

    case 0x0F: case 0x10:
        // 写多个线圈/寄存器：
        // 地址(1) + 功能码(1) + 起始地址(2) + 数量(2) + 字节数(1) + 数据(byteCount) + CRC(2) = 9+byteCount
        if (dataSize < 7) return -1;  // 至少需要读到字节数字段
        return 9 + data[6];

    case 0x16:
        // 掩码写寄存器：地址(1) + 功能码(1) + 参考地址(2) + 与掩码(2) + 或掩码(2) + CRC(2) = 10
        return 10;

    case 0x18:
        // 读FIFO队列：地址(1) + 功能码(1) + 指针地址(2) + CRC(2) = 6
        return 6;

    default:
        // 未知功能码，无法确定帧长度，需回退到扫描方式
        return -1;
    }
}

/**
 * @brief 处理接收缓冲区中的所有完整帧（使用帧长度计算优化）
 * @note 通过 calculateRtuFrameLength 直接计算帧长度，常见功能码为 O(1)，
 *       未知功能码回退到有限范围扫描（最多256字节），避免 O(N²) 遍历
 */
static void processReceiveBuffer(XModbusRtuSerialServer* server)
{
    if (!server || !server->m_receiveBuffer) return;

    XByteArray* buffer = server->m_receiveBuffer;
    const uint8_t* data = XContainerDataAddr(buffer);
    size_t dataSize = XByteArray_size_base(buffer);

    // 持续处理缓冲区中的完整帧
    while (dataSize >= 4) {
        int frameLen = calculateRtuFrameLength(data, dataSize);

        if (frameLen < 0) {
            // 未知功能码或无法确定长度，回退到有限范围扫描
            size_t maxScan = dataSize > 256 ? 256 : dataSize;
            bool found = false;
            for (size_t len = 4; len <= maxScan; len++) {
                XModbusAdu* adu = XModbusAdu_parseRtu(data, len);
                if (adu) {
                    if (XModbusAdu_matchingChecksum(adu)) {
                        processRtuFrame(server, data, len);
                        XByteArray_remove_base(buffer, 0, len);
                        found = true;
                    }
                    XModbusAdu_delete(adu);
                    if (found) break;
                }
            }
            if (!found) {
                // 未找到有效帧，若缓冲区过大则清除
                if (dataSize > MODBUS_RTU_RECEIVE_BUFFER_MAX_SIZE) {
                    XContainer_clear_base((XContainer*)buffer);
                }
                break;
            }
        } else if ((size_t)frameLen <= dataSize) {
            // 已知帧长度，用 XModbusAdu 验证 CRC
            XModbusAdu* adu = XModbusAdu_parseRtu(data, (size_t)frameLen);
            if (adu && XModbusAdu_matchingChecksum(adu)) {
                processRtuFrame(server, data, (size_t)frameLen);
                XModbusAdu_delete(adu);
                XByteArray_remove_base(buffer, 0, (size_t)frameLen);
            } else {
                // CRC 校验失败，移除第一个字节后重试
                if (adu) XModbusAdu_delete(adu);
                XByteArray_remove_base(buffer, 0, 1);
            }
        } else {
            // 数据不足，等待更多数据
            if (dataSize > MODBUS_RTU_RECEIVE_BUFFER_MAX_SIZE) {
                XContainer_clear_base((XContainer*)buffer);
            }
            break;
        }

        data = XContainerDataAddr(buffer);
        dataSize = XByteArray_size_base(buffer);
    }
}

// =============== 类初始化 ================
XVtable* XModbusRtuSerialServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusRtuSerialServer)
	XCLASS_SET_CLASS_NAME_DEFAULT("XModbusRtuSerialServer");
    // 继承 XModbusServer
    XVTABLE_INHERIT_XCLASS(XModbusServer);

    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXModbusRtuSerialServer_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXModbusRtuSerialServer_close);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessesBroadcast, VXModbusRtuSerialServer_processesBroadcast);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusRtuSerialServer_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusRtuSerialServer_deinit);

	XCLASS_SHOW_SIZE(XModbusRtuSerialServer, sizeof(XModbusRtuSerialServer));
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

#endif /* XMODBUS_RTU_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
