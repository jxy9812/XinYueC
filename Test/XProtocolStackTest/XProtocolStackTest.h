#ifndef XPROTOCOLSTACKTEST_H
#define XPROTOCOLSTACKTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDataStructConfig.h"
#include"XClass.h"
#if DEMOTEST
	void XModbusTest();
	void XDataFrameCommTest();
	void TJCHMICommTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif