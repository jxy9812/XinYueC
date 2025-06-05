#ifndef XMODBUSFUNCTIONHANDLER_H
#define XMODBUSFUNCTIONHANDLER_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include"XModbusHandlerObject.h"
typedef struct XModbus XModbus;
typedef struct XModbusFunctionHandler XModbusFunctionHandler;

//Modbus 功能码处理函数表结构体
typedef struct XModbusFunctionHandler
{
	uint8_t code;//功能码
	pXModbusFunctionHandler function;//功能码对应的函数
	void* data;//功能码要用到的数据 XModbusRegObject类型
}XModbusFunctionHandler;

typedef struct XVector XVector;
//功能码列表 XVector<XModbusFunctionHandler>
typedef XVector XModbusFunctionHandlerList;
//XVector<XModbusFunctionHandler>
XModbusFunctionHandlerList* XModbusFuncCodeList_create();
void XModbusFuncCodeList_push(XModbusFunctionHandlerList* list, XModbusFunctionHandler* data);
//删除功能码
void XModbusFuncCodeList_remove(XModbusFunctionHandlerList* list, uint8_t code);
//查找功能码
XModbusFunctionHandler* XModbusFuncCodeList_findFuncCode(XModbusFunctionHandlerList* list, uint8_t code);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
