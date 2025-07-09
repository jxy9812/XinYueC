#include"XProtocolStackTest.h"
#include"XTJCHMIComm.h"
#include"XSerialPort.h"
#include"XVector.h"
#include"XTimerBase.h"
#include"cJSON.h"
#include"XString.h"
#include"XStringList.h"
#include"XTimerGroupBase.h"
#include <stdio.h>
#include <stdlib.h>
static void XFuncCodeCb0x1A(uint8_t code, void* obj, void* data, void* userData)
{
	XVector* v = data;
	printf("0x1a功能码调用\n");
}
static void XFuncCodeCb0x30(uint8_t code, void* obj, void* data, void* userData)
{
	XVector* v = data;
	printf("0x30功能码调用\n");
	// 解析 JSON 字符串
	cJSON* root = cJSON_ParseWithLength(((uint8_t*)XContainerDataPtr(v))+1, XContainerSize(v)-1);
	if (root == NULL) 
	{
		printf("解析失败\n");
		return 1;
	}
	printf("解析成功\n");
	// 获取整数类型的键值
	cJSON* type = cJSON_GetObjectItem(root, "type");
	if (cJSON_IsNumber(type)) {
		printf("Command: %d\n", type->valueint);
	}
	cJSON* isWhile = cJSON_GetObjectItem(root, "isWhile");
	if (cJSON_IsBool(isWhile)) {
		printf("isWhile: %d\n", cJSON_IsTrue(isWhile));
	}
	cJSON* cylinderOpenTime = cJSON_GetObjectItem(root, "cylinderOpenTime");
	if (cJSON_IsNumber(cylinderOpenTime)) {
		printf("cylinderOpenTime: %d\n", cylinderOpenTime->valueint);
	}
	cJSON* screwStartTime = cJSON_GetObjectItem(root, "screwStartTime");
	if (cJSON_IsNumber(screwStartTime)) {
		printf("screwStartTime: %d\n", screwStartTime->valueint);
	}
	cJSON* waitShearTime = cJSON_GetObjectItem(root, "waitShearTime");
	if (cJSON_IsNumber(waitShearTime)) {
		printf("waitShearTime: %d\n", waitShearTime->valueint);
	}
	cJSON* waitReturnTime = cJSON_GetObjectItem(root, "waitReturnTime");
	if (cJSON_IsNumber(waitReturnTime)) {
		printf("waitReturnTime: %d\n", waitReturnTime->valueint);
	}
	cJSON* cuttingMotorSpeedDocking = cJSON_GetObjectItem(root, "cuttingMotorSpeedDocking");
	if (cJSON_IsString(cuttingMotorSpeedDocking) && (cuttingMotorSpeedDocking->valuestring != NULL)) {
		//printf("cuttingMotorSpeedDocking: %s\n", cuttingMotorSpeedDocking->valuestring);
		XString* str= cJSON_GetStringValue_XString(cuttingMotorSpeedDocking);
		if (str != NULL)
		{
			XStringList* strlist = XString_split(str, "-");
			if (strlist != NULL )
			{
				if (XContainerSize(strlist) == 3)
				{
					printf("%lf %lf %lf\n"
						, XString_toDouble(XStringList_at_base(strlist, 0))
						, XString_toDouble(XStringList_at_base(strlist, 1))
						, XString_toDouble(XStringList_at_base(strlist, 2))
					);
				}
				XStringList_delete_base(strlist);
			}
			XString_delete_base(str);
		}
	}
	cJSON* screwMotorSpeedStartMove = cJSON_GetObjectItem(root, "screwMotorSpeedStartMove");
	if (cJSON_IsString(screwMotorSpeedStartMove) && (screwMotorSpeedStartMove->valuestring != NULL)) {
		printf("screwMotorSpeedStartMove: %s\n", screwMotorSpeedStartMove->valuestring);
	}
	cJSON* screwMotorSpeedDocking = cJSON_GetObjectItem(root, "screwMotorSpeedDocking");
	if (cJSON_IsString(screwMotorSpeedDocking) && (screwMotorSpeedDocking->valuestring != NULL)) {
		printf("screwMotorSpeedDocking: %s\n", screwMotorSpeedDocking->valuestring);
	}
	cJSON* screwMotorSpeedReturn = cJSON_GetObjectItem(root, "screwMotorSpeedReturn");
	if (cJSON_IsString(screwMotorSpeedReturn) && (screwMotorSpeedReturn->valuestring != NULL)) {
		printf("screwMotorSpeedReturn: %s\n", screwMotorSpeedReturn->valuestring);
	}
	cJSON* cuttingArgs = cJSON_GetObjectItem(root, "cuttingArgs");
	if (cJSON_IsString(cuttingArgs) && (cuttingArgs->valuestring != NULL)) {
		printf("cuttingArgs: %s\n", cuttingArgs->valuestring);
	}
	// 将修改后的 JSON 对象转换为字符串
	char* json_str_modified = cJSON_Print(root);
	if (json_str_modified == NULL) {
		cJSON_Delete(root);
		return 1;
	}

	// 打印修改后的 JSON 字符串
	printf("%s\n", json_str_modified);

	// 释放字符串和 cJSON 对象
	XMemory_free(json_str_modified);
	cJSON_Delete(root);
}
void TJCHMICommTest()
{
	printf("开始创建串口\n");
	XSerialPortBase* USART = XSerialPort_create();
	USART->m_baudRate = 115200;
	USART->m_portNum = 20;
	XIODeviceBase_setReadBuffer_base(USART, 1024);
	XIODeviceBase_setWriteBuffer_base(USART, 1024);
	XTJCHMIComm* comm = XTJCHMIComm_create(USART);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
	XDataFrameComm_setSendValidCRC16_base(comm, true);
	//XDataFrameComm_setRecvValidCRC16_base(comm, true);
	//XDataFrameComm_addPeriodicSendText(comm,false, 100, "ain.cuttingMotorSp.val=888");
	uint8_t funcCode = 0x1A;
	XDataFrameComm_addFuncCode(comm, &funcCode, XFuncCodeCb0x1A, NULL);
	funcCode = 0x30;
	XDataFrameComm_addFuncCode(comm, &funcCode, XFuncCodeCb0x30, NULL);
	XDataFrameComm_connect_base(comm);
	size_t speed = 1, current = XTimerBase_getCurrentTime();
	/*while (true)
	{
		if (XTimerBase_getCurrentTime() > current + 1000)
		{
			XDataFrameComm_sendTextFmt(comm, false,  "main.cuttingMotorSp.val=%d", speed++);
			current = XTimerBase_getCurrentTime();
		}
		XDataFrameComm_poll_base(comm);
		XTimerGroupBase_global_poll();
	}*/
}