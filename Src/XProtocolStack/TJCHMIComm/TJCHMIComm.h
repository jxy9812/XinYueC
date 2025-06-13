#ifndef TJCHMICOMM_H
#define TJCHMICOMM_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDataFrameComm.h"
#define TJCHMICOMM_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(TJCHMIComm))       //XCommunicatorBase虚函数表大小
//XCLASS_DEFINE_BEGING(TJCHMIComm)
//XCLASS_DEFINE_ENUM(TJCHMIComm, Connect) = XCLASS_VTABLE_GET_SIZE(XDataFrameComm),
//XCLASS_DEFINE_END(TJCHMIComm)

#ifdef __cplusplus
}
#endif
#endif // TJCHMICOMM_H
