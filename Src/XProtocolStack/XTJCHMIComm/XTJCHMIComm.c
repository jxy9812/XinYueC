#include"XTJCHMIComm.h"
#include"XEquality.h"
#include"XVector.h"
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

	XDataFrameComm_funcCodeMap_create(comm,sizeof(uint8_t),XEquality_uint8_t);
	XDataFrameComm_setGetFuncCodeCb(comm, XDataFrameComm_GetFuncCodeCb);
}

bool XDataFrameComm_GetFuncCodeCb(XDataFrameComm* comm, XVector* data, uint8_t* code)
{
	if (data == NULL || XVector_isEmpty_base(data) || code == NULL)
		return false;
	*code = *((uint8_t*)XContainerDataPtr(data));
	return true;
}