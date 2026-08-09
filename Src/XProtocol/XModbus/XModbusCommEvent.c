#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CORE_ON
#include "XModbusCommEvent.h"

XModbusCommEvent XModbusCommEvent_create(XModbusCommEvent_EventByte byte)
{
    XModbusCommEvent event;
    event.m_eventByte = (uint8_t)byte;
    return event;
}

XModbusCommEvent XModbusCommEvent_fromUint8(uint8_t byte)
{
    XModbusCommEvent event;
    event.m_eventByte = byte;
    return event;
}

uint8_t XModbusCommEvent_toUint8(const XModbusCommEvent* event)
{
    if (!event) return 0;
    return event->m_eventByte;
}

XModbusCommEvent_EventByte XModbusCommEvent_toEventByte(const XModbusCommEvent* event)
{
    if (!event) return XModbusCommEvent_InitiatedCommunicationRestart;
    return (XModbusCommEvent_EventByte)(event->m_eventByte);
}

void XModbusCommEvent_setEventByte(XModbusCommEvent* event, XModbusCommEvent_EventByte byte)
{
    if (event) event->m_eventByte = (uint8_t)byte;
}

XModbusCommEvent* XModbusCommEvent_orWithSendFlag(XModbusCommEvent* event, XModbusCommEvent_SendFlag flag)
{
    if (event) event->m_eventByte |= (uint8_t)flag;
    return event;
}

XModbusCommEvent* XModbusCommEvent_orWithReceiveFlag(XModbusCommEvent* event, XModbusCommEvent_ReceiveFlag flag)
{
    if (event) event->m_eventByte |= (uint8_t)flag;
    return event;
}

bool XModbusCommEvent_testSendFlag(const XModbusCommEvent* event, XModbusCommEvent_SendFlag flag)
{
    if (!event) return false;
    return (event->m_eventByte & (uint8_t)flag) != 0;
}

bool XModbusCommEvent_testReceiveFlag(const XModbusCommEvent* event, XModbusCommEvent_ReceiveFlag flag)
{
    if (!event) return false;
    return (event->m_eventByte & (uint8_t)flag) != 0;
}

bool XModbusCommEvent_isSentEvent(const XModbusCommEvent* event)
{
    if (!event) return false;
    return (event->m_eventByte & (uint8_t)XModbusCommEvent_SentEvent) != 0;
}

bool XModbusCommEvent_isReceiveEvent(const XModbusCommEvent* event)
{
    if (!event) return false;
    return (event->m_eventByte & (uint8_t)XModbusCommEvent_ReceiveEvent) != 0;
}

bool XModbusCommEvent_isListenOnlyMode(const XModbusCommEvent* event)
{
    if (!event) return false;
    return (event->m_eventByte & (uint8_t)XModbusCommEvent_EnteredListenOnlyMode) != 0;
}

bool XModbusCommEvent_isRestartEvent(const XModbusCommEvent* event)
{
    if (!event) return false;
    return event->m_eventByte == (uint8_t)XModbusCommEvent_InitiatedCommunicationRestart;
}

XModbusCommEvent_EventByte XModbusCommEvent_combineEventByte(
    XModbusCommEvent_EventByte byte, XModbusCommEvent_SendFlag flag)
{
    return (XModbusCommEvent_EventByte)((uint8_t)byte | (uint8_t)flag);
}

XModbusCommEvent_EventByte XModbusCommEvent_combineEventByteWithReceive(
    XModbusCommEvent_EventByte byte, XModbusCommEvent_ReceiveFlag flag)
{
    return (XModbusCommEvent_EventByte)((uint8_t)byte | (uint8_t)flag);
}

#endif /* XMODBUS_CORE_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
