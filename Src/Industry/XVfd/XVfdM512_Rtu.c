/**
 * @file       XVfdM512_Rtu.c
 * @brief      511M/512M/513M 专用 Modbus-RTU 客户端实现（本厂非标准错误帧归一化）。
 * @details    见 XVfdM512_Rtu.h 的完整说明：
 *             本厂错误帧 8 字节、功能码不置 0x80 ⇒ 通用客户端算不出帧长、
 *             帧永远留缓冲 ⇒ 请求超时（且仅重试才清缓冲）。
 *             本文件**只做一件事**：帧间超时到期时，把缓冲里的本厂错误帧
 *             就地改写成标准 5 字节异常帧，再交父类走标准分支。
 *             框架（XModbusRtuSerialClient）**零改动**，厂商特例不污染通用类。
 */
#include "XVfdM512_Rtu.h"

#if XVFD_ON && XVFD_M512_ON

#include "XByteArray.h"
#include "XContainer.h"
#include "XCrc.h"
#include "XEvent.h"
#include "XTimer.h"
#include <string.h>

/** 本厂错误帧特征：[站号][fc][80][01][00][错误码][CRC][CRC] */
#define XVFD_M512_VENDOR_ERR_LEN     8
#define XVFD_M512_VENDOR_ERR_TAG0    0x80
#define XVFD_M512_VENDOR_ERR_TAG1    0x01
#define XVFD_M512_VENDOR_ERR_TAG2    0x00
/** 标准异常帧长度：[站号][fc|0x80][错误码][CRC][CRC] */
#define XVFD_M512_STD_EXC_LEN        5

uint8_t XVfdM512Rtu_mapErrorCode(uint8_t vendorCode)
{
    switch (vendorCode) {
    case 0x02: return (uint8_t)XModbusPdu_IllegalFunction;   /* 本厂非法功能码 */
    case 0x05: return (uint8_t)XModbusPdu_IllegalDataAddress; /* 本厂非法数据地址 */
    case 0x04: return (uint8_t)XModbusPdu_IllegalDataValue;   /* 本厂非法数据值 */
    default:   return (uint8_t)XModbusPdu_ServerDeviceFailure;
    }
}

/**
 * @brief 缓冲里若是本厂非标准错误帧 ⇒ 就地归一化成标准 5 字节异常帧。
 * @param client RTU 客户端（继承自 XModbusRtuSerialClient）。
 * @return 命中并已改写返回 true；未命中返回 false（缓冲不变）。
 */
static bool xvfdM512RtuNormalizeVendorError(XModbusRtuSerialClient* client)
{
    XByteArray* buf;
    const uint8_t* d;
    size_t len;
    uint8_t std[XVFD_M512_STD_EXC_LEN];
    uint16_t crc;
    if (!client || !client->m_receiveBuffer) return false;
    buf = client->m_receiveBuffer;
    len = XByteArray_size_base(buf);
    if (len < XVFD_M512_VENDOR_ERR_LEN) return false;
    d = XContainerDataAddr(buf);
    /* 特征位：[80][01][00] */
    if (d[2] != XVFD_M512_VENDOR_ERR_TAG0 ||
        d[3] != XVFD_M512_VENDOR_ERR_TAG1 ||
        d[4] != XVFD_M512_VENDOR_ERR_TAG2) return false;
    /* 功能码置了 0x80 的是标准异常帧 ⇒ 交父类，不归一化 */
    if (d[1] & XMODBUS_PDU_EXCEPTION_BYTE) return false;
    /* CRC 校验（覆盖前 6 字节，低字节在前） */
    crc = XCrc16_calculate(XCrc16_Algorithm_Modbus, d, XVFD_M512_VENDOR_ERR_LEN - 2);
    if ((uint16_t)(d[6] | ((uint16_t)d[7] << 8)) != crc) return false;

    /* 归一化：[站号][fc|0x80][标准异常码][CRC] */
    std[0] = d[0];
    std[1] = (uint8_t)(d[1] | XMODBUS_PDU_EXCEPTION_BYTE);
    std[2] = XVfdM512Rtu_mapErrorCode(d[5]);
    crc = XCrc16_calculate(XCrc16_Algorithm_Modbus, std, XVFD_M512_STD_EXC_LEN - 2);
    std[3] = (uint8_t)(crc & 0xFF);
    std[4] = (uint8_t)(crc >> 8);

    XByteArray_clear_base(buf);
    XByteArray_push_back_2(buf, std, XVFD_M512_STD_EXC_LEN);
    return true;
}

/**
 * @brief 定时器事件：帧间超时到期前先把本厂非标准错误帧归一化。
 * @param obj 本客户端对象。
 * @param event 定时器事件。
 * @return 无。
 * @note 归一化后交父类走**标准**异常分支：错误语义、reply 收口
 *       （先摘 m_currentReply 再置错）、发送队列续泵全走框架自己的实现。
 */
static void XVfdM512RtuClient_timerEvent(XObject* obj, XTimerEvent* event)
{
    if (obj && event) {
        XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)obj;
        if (client->m_interFrameTimer != XTIMER_INVALID_ID &&
            XTimerEvent_timerId(event) == client->m_interFrameTimer) {
            (void)xvfdM512RtuNormalizeVendorError(client);
        }
    }
    XClass_Parent(XModbusRtuSerialClient, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(obj, event);
}

/* ==================== 虚函数表 / 生命周期 ==================== */

XVtable* XVfdM512RtuClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdM512RtuClient)
    XVTABLE_INHERIT_XCLASS(XModbusRtuSerialClient);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, XVfdM512RtuClient_timerEvent);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdM512RtuClient");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdM512RtuClient);
    return XVTABLE_DEFAULT;
}

XVfdM512RtuClient* XVfdM512RtuClient_create_ex(XMemoryType memory)
{
    XVfdM512RtuClient* dev =
        (XVfdM512RtuClient*)XMemory_malloc(sizeof(XVfdM512RtuClient), memory);
    if (!dev) return NULL;
    XVfdM512RtuClient_init(dev);
    Set_Class_Memory(dev, memory);
    Set_Class_IsHeap(dev, true);
    return dev;
}

void XVfdM512RtuClient_init(XVfdM512RtuClient* dev)
{
    if (!dev) return;
    memset(dev, 0, sizeof(*dev));
    XModbusRtuSerialClient_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdM512RtuClient_class_init();
}

#endif /* XVFD_ON && XVFD_M512_ON */
