#ifndef XPLC_H
#define XPLC_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#define XPLC_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XClass))       //XPLC虚函数表大小
//XPLC虚函数表枚举XCLASS_DEFINE_BEGING(XPLC)
XCLASS_DEFINE_ENUM(XPLC, AddTaskState) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XPLC, RemoveTaskState),
XCLASS_DEFINE_ENUM(XPLC, ClearTaskState),
XCLASS_DEFINE_ENUM(XPLC, SetState),
XCLASS_DEFINE_ENUM(XPLC, Poll),
XCLASS_DEFINE_ENUM(XPLC, Start),
XCLASS_DEFINE_ENUM(XPLC, Finish),
XCLASS_DEFINE_END(XPLC)

#ifdef __cplusplus
}
#endif
#endif // !XPLC_H
