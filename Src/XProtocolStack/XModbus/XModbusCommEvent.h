#ifndef XMODBUSCOMMEVENT_H
#define XMODBUSCOMMEVENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XModbusCommEvent_ReadExceptionSent = 0x01,
    XModbusCommEvent_ServerAbortExceptionSent = 0x02,
    XModbusCommEvent_ServerDeviceBusy = 0x04,
    XModbusCommEvent_ServerProgramNAKExceptionSent = 0x08,
    XModbusCommEvent_WriteTimeoutErrorOccurred = 0x10,
    XModbusCommEvent_CurrentlyInListenOnlyMode = 0x20,
} XModbusCommEvent_SendFlag;

typedef enum {
    XModbusCommEvent_CommunicationError = 0x02,
    XModbusCommEvent_CharacterOverrun = 0x10,
    XModbusCommEvent_ReceiveListenOnlyMode = 0x20,
    XModbusCommEvent_BroadcastReceived = 0x40,
} XModbusCommEvent_ReceiveFlag;

typedef enum {
    XModbusCommEvent_InitiatedCommunicationRestart = 0x00,
    XModbusCommEvent_EnteredListenOnlyMode = 0x04,
    XModbusCommEvent_SentEvent = 0x40,
    XModbusCommEvent_ReceiveEvent = 0x80,
} XModbusCommEvent_EventByte;

typedef struct XModbusCommEvent {
    uint8_t m_eventByte;
} XModbusCommEvent;

XModbusCommEvent XModbusCommEvent_create(XModbusCommEvent_EventByte byte);
XModbusCommEvent XModbusCommEvent_fromUint8(uint8_t byte);
uint8_t XModbusCommEvent_toUint8(const XModbusCommEvent* event);
XModbusCommEvent_EventByte XModbusCommEvent_toEventByte(const XModbusCommEvent* event);
void XModbusCommEvent_setEventByte(XModbusCommEvent* event, XModbusCommEvent_EventByte byte);
XModbusCommEvent* XModbusCommEvent_orWithSendFlag(XModbusCommEvent* event, XModbusCommEvent_SendFlag flag);
XModbusCommEvent* XModbusCommEvent_orWithReceiveFlag(XModbusCommEvent* event, XModbusCommEvent_ReceiveFlag flag);
bool XModbusCommEvent_testSendFlag(const XModbusCommEvent* event, XModbusCommEvent_SendFlag flag);
bool XModbusCommEvent_testReceiveFlag(const XModbusCommEvent* event, XModbusCommEvent_ReceiveFlag flag);
bool XModbusCommEvent_isSentEvent(const XModbusCommEvent* event);
bool XModbusCommEvent_isReceiveEvent(const XModbusCommEvent* event);
bool XModbusCommEvent_isListenOnlyMode(const XModbusCommEvent* event);
bool XModbusCommEvent_isRestartEvent(const XModbusCommEvent* event);
XModbusCommEvent_EventByte XModbusCommEvent_combineEventByte(XModbusCommEvent_EventByte byte, XModbusCommEvent_SendFlag flag);
XModbusCommEvent_EventByte XModbusCommEvent_combineEventByteWithReceive(XModbusCommEvent_EventByte byte, XModbusCommEvent_ReceiveFlag flag);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSCOMMEVENT_H