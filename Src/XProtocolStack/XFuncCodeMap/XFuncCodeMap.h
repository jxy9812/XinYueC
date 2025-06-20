#ifndef XFUNCCODEMAP_H
#define XFUNCCODEMAP_H
#include"XTypes.h"
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include"XFunctionCallback.h"
typedef void (*XFuncCodeCb)(void* funcCode, void* obj, void* data,void* userData);
//功能码节点
typedef struct XFuncCodeNode
{
	XFuncCodeCb cb;//功能码对应的函数
	void* userData;//功能码要用到的数据 
}XFuncCodeNode;


XFuncCodeMap* XFuncCodeMap_create(size_t codeSize, XEquality codeEquality);
bool XFuncCodeMap_add(XFuncCodeMap* map, void* code, XFuncCodeCb cb, void* userData);
bool XFuncCodeMap_remove(XFuncCodeMap* map, void* code);
XFuncCodeNode* XFuncCodeMap_value(XFuncCodeMap* map, void* code);
bool XFuncCodeMap_clear(XFuncCodeMap* map);
void XFuncCodeMap_delete(XFuncCodeMap* map);
//创建功能码
void* XFuncCodeMap_createCode(XFuncCodeMap* map);
void XFuncCodeMap_deleteCode(void* code);
#endif // !XFuncCodeMap_H
