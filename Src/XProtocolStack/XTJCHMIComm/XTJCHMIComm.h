#ifndef XTJCHMICOMM_H
#define XTJCHMICOMM_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDataFrameComm.h"
#define XTJCHMICOMM_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XTJCHMIComm))       //XCommunicatorBase虚函数表大小
XCLASS_DEFINE_BEGING(XTJCHMIComm)
//XCLASS_DEFINE_ENUM(XTJCHMIComm, Connect) = XCLASS_VTABLE_GET_SIZE(XDataFrameComm),
XCLASS_DEFINE_EXTEND_END(XTJCHMIComm, XDataFrameComm)
typedef struct XTJCHMIComm
{
	XDataFrameComm m_class;
}XTJCHMIComm;
XVtable* XTJCHMIComm_class_init();
XTJCHMIComm* XTJCHMIComm_create(XIODeviceBase* io);
void  XTJCHMIComm_init(XTJCHMIComm* comm, XIODeviceBase* io);

#define XTJCHMIComm_delete_base					XDataFrameComm_delete_base
#define XTJCHMIComm_setRecvValidCRC16_base		XDataFrameComm_setRecvValidCRC16_base
#define XTJCHMIComm_setSendValidCRC16_base		XDataFrameComm_setSendValidCRC16_base

#ifdef __cplusplus
}
#endif
#endif // XTJCHMICOMM_H
