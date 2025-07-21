#ifndef XACTION_H
#define XACTION_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
typedef void(*Action)(XVariant* data);
//动作
typedef struct XAction
{
	char* text;//文本
	Action action;//动作
	XVariant* data;//数据
}XAction;

XAction* XAction_create(const char* text);
void XAction_init(XAction* action, const char* text);
void XAction_setText(XAction* action, const char* text);
void XAction_setAction(XAction* action, Action func);
void XAction_setData(XAction* action, XVariant* data);
void XAction_delete(XAction* action);
//触发动作
void XAction_trigger(XAction* action);
const char* XAction_getText(XAction* action);
Action XAction_getAction(XAction* action);
XVariant* XAction_getData(XAction* action);
#ifdef __cplusplus
}
#endif
#endif// !XREDBLACKTREE_H
