#include"XProtocolStackTest.h"
#include"XTJCHMIComm.h"
#include"XSerialPortBase.h"
#include"XVector.h"
#include"XTimerBase.h"
#include"cJSON.h"
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
	cJSON* root = cJSON_ParseWithLength(((uint8_t*)XContainerDataPtr(v))+1, XContainerSize(v)-1-2);
	if (root == NULL) 
	{
		printf("解析失败\n");
		return 1;
	}
	printf("解析成功\n");
	// 获取整数类型的键值
	cJSON* type = cJSON_GetObjectItem(root, "type");
	if (cJSON_IsNumber(type)) {
		printf("type: %d\n", type->valueint);
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
		printf("cuttingMotorSpeedDocking: %s\n", cuttingMotorSpeedDocking->valuestring);
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
	XSerialPortBase* USART = XSerialPortWin32_create();
	USART->m_baudRate = 115200;
	USART->m_portNum = 2;
	XIODeviceBase_setReadBuffer_base(USART, 1024);
	XIODeviceBase_setWriteBuffer_base(USART, 1024);
	XTJCHMIComm* comm = XTJCHMIComm_create(USART);
	/*{
		uint8_t sendFrameTail[] = { 0x01, 0xFE,0xFE,0xFE };
		uint8_t recvFrameTail1[] = { 0x01, 0xFE,0xFE,0xFE };
		XDataFrameComm_setSendFrameTail_base(comm, sendFrameTail, sizeof(sendFrameTail));
		XDataFrameComm_setRecvFrameTail_base(comm, recvFrameTail1, sizeof(recvFrameTail1));
	}*/
	//XDataFrameComm_setCommMode_base(comm, XDFC_COMM_MODE_HALF_DUPLEX);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
	XDataFrameComm_setSendValidCRC16_base(comm, true);
	//XDataFrameComm_setRecvValidCRC16_base(comm, true);
	//XDataFrameComm_addPeriodicSendText(comm,false, 100, "ain.cuttingMotorSp.val=888");
	XDataFrameComm_addFuncCode(comm, 0x1A, XFuncCodeCb0x1A, NULL);
	XDataFrameComm_addFuncCode(comm, 0x30, XFuncCodeCb0x30, NULL);
	XDataFrameComm_connect_base(comm);
	size_t speed = 1, current = XTimerBase_getCurrentTime();
	while (true)
	{
		if (XTimerBase_getCurrentTime() > current + 1000)
		{
			XDataFrameComm_sendTextFmt(comm, false,  "main.cuttingMotorSp.val=%d", speed++);
			current = XTimerBase_getCurrentTime();
		}
		XDataFrameComm_poll_base(comm);
	}
}