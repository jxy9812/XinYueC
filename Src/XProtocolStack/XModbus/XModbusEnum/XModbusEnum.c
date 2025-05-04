#include "XModbusEnum.h"
#include <stdio.h>
#if MB_ENUM_TO_STRING

#define STRINGIFY(x) #x
//数字转字符串
#define EXPAND_THEN_STRINGIFY(x) STRINGIFY(x)
//其他枚举解释 提示此枚举类型为被解释
#define ENUM_DEFAULT_EXPLAIN(str) str",请到:"__FILE__" "EXPAND_THEN_STRINGIFY(__LINE__)"行,文件内添加!!!!\n" 
const char* XModbusEventType_toString(XModbusEventType type)
{
	switch (type)
	{
	case EV_READY:return "EV_READY(启动事件完成)"; break;
	case EV_FRAME_RECEIVED:return "EV_FRAME_RECEIVED(接收到完整帧事件)"; break;
	case EV_EXECUTE:return "EV_EXECUTE(执行功能码处理事件)"; break;
	case EV_FRAME_SENT:return "EV_FRAME_SENT(帧发送完成事件)"; break;
	default:
		return ENUM_DEFAULT_EXPLAIN("当前事件类型未做说明"); break;
	}
	return NULL;
}

#endif // MB_ENUM_TO_STRING
