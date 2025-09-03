#include"XTJCHMIComm.h"
#include"XCompare.h"
#include"XVector.h"
//获取功能码回调
static bool XTJCHMIComm_GetFuncCodeCb(XDataFrameComm* comm, XByteArray* data, uint8_t* code);
XTJCHMIComm* XTJCHMIComm_create(XIODeviceBase* io)
{
	if (io == NULL)
		return NULL;
	XTJCHMIComm* comm = XMemory_malloc(sizeof(XTJCHMIComm));
	XTJCHMIComm_init(comm, io);
	return comm;
}

void XTJCHMIComm_init(XTJCHMIComm* comm, XIODeviceBase* io)
{
	if (comm == NULL || io == NULL)
		return;
	XDataFrameComm_init(comm,io);
	XClassGetVtable(comm) = XTJCHMIComm_class_init();
	XDataFrameComm_setCommMode_base(comm, XDFC_COMM_MODE_FULL_DUPLEX);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);

	XDataFrameComm_funcCodeMap_create(comm,sizeof(uint8_t),XCompare_uint8_t);
	XDataFrameComm_setGetFuncCodeCb(comm, XTJCHMIComm_GetFuncCodeCb);
}

bool XTJCHMIComm_GetFuncCodeCb(XDataFrameComm* comm, XByteArray* data, uint8_t* code)
{
	if (data == NULL || XVector_isEmpty_base(data) || code == NULL)
		return false;
	*code = *((uint8_t*)XContainerDataPtr(data));
	return true;
}