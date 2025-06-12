#include "XDataFrameCommEnum.h"
#include <stdio.h>
#if XDFC_ENUM_TO_STRING

#define STRINGIFY(x) #x
//数字转字符串
#define EXPAND_THEN_STRINGIFY(x) STRINGIFY(x)
//其他枚举解释 提示此枚举类型为被解释
#define ENUM_DEFAULT_EXPLAIN(str) str",请到:"__FILE__" "EXPAND_THEN_STRINGIFY(__LINE__)"行,文件内添加!!!!\n" 
const char* XDataFrameComm_EventType_toString(XDFC_EventType type)
{
	switch (type)
	{
	case XDFC_READY:return "XDFC_READY(启动完成事件)"; break;
	case XDFC_FRAME_RECEIVED:return "XDFC_FRAME_RECEIVED(接收到完整帧事件)"; break;
	case XDFC_EXECUTE:return "XDFC_EXECUTE(执行功能码处理事件)"; break;
	case XDFC_FRAME_SENT:return "XDFC_FRAME_SENT(帧发送完成事件)"; break;
	case XDFC_RX_BUFFER_OVERFLOW:return "XDFC_RX_BUFFER_OVERFLOW(接收缓冲区溢出事件)"; break;
	default:
		return ENUM_DEFAULT_EXPLAIN("当前事件类型未做说明"); break;
	}
	return NULL;
}

#endif // MB_ENUM_TO_STRING
