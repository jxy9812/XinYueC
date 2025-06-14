#include"TJCHMIComm.h"
TJCHMIComm* TJCHMIComm_create(XIODeviceBase* io)
{
	if (io == NULL)
		return NULL;
	TJCHMIComm* comm = XMemory_malloc(sizeof(TJCHMIComm));
	TJCHMIComm_init(comm, io);
	return comm;
}

void TJCHMIComm_init(TJCHMIComm* comm, XIODeviceBase* io)
{
	if (comm == NULL || io == NULL)
		return;
	XDataFrameComm_init(comm,io);
	XDataFrameComm_setCommMode_base(comm, XDFC_COMM_MODE_FULL_DUPLEX);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
}
