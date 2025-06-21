#include "XModbusEnum.h"
#include <stdio.h>
#if MB_ENUM_TO_STRING

#define STRINGIFY(x) #x
//数字转字符串
#define EXPAND_THEN_STRINGIFY(x) STRINGIFY(x)
//其他枚举解释 提示此枚举类型为被解释
#define ENUM_DEFAULT_EXPLAIN(str) str",请到:"__FILE__" "EXPAND_THEN_STRINGIFY(__LINE__)"行,文件内添加!!!!\n" 
#endif // MB_ENUM_TO_STRING
