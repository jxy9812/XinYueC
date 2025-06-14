#ifndef TJCHMICOMM_H
#define TJCHMICOMM_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDataFrameComm.h"
#define TJCHMICOMM_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(TJCHMIComm))       //XCommunicatorBase虚函数表大小
XCLASS_DEFINE_BEGING(TJCHMIComm)
//XCLASS_DEFINE_ENUM(TJCHMIComm, Connect) = XCLASS_VTABLE_GET_SIZE(XDataFrameComm),
XCLASS_DEFINE_EXTEND_END(TJCHMIComm, XDataFrameComm)
typedef struct TJCHMIComm
{
	XDataFrameComm m_parent;
}TJCHMIComm;
XVtable* TJCHMIComm_class_init();
TJCHMIComm* TJCHMIComm_create(XIODeviceBase* io);
void  TJCHMIComm_init(TJCHMIComm* comm, XIODeviceBase* io);

#define TJCHMIComm_setRecvValidCRC16_base		XDataFrameComm_setRecvValidCRC16_base
#define TJCHMIComm_setSendValidCRC16_base		XDataFrameComm_setSendValidCRC16_base
#ifdef __cplusplus
}
#endif
#endif // TJCHMICOMM_H
