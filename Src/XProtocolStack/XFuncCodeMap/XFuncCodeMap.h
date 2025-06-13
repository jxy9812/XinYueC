#ifndef XFUNCCODEMAP_H
#define XFUNCCODEMAP_H
#include"XTypes.h"
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
typedef void (*XFuncCodeCb)(uint8_t code, void* obj,void* userData);
//功能码节点
typedef struct XFuncCodeNode
{
	XFuncCodeCb cb;//功能码对应的函数
	void* obj;//
	void* userData;//功能码要用到的数据 
}XFuncCodeNode;


XFuncCodeMap* XFuncCodeMap_create();
bool XFuncCodeMap_add(XFuncCodeMap* map, uint8_t code, XFuncCodeCb cb, void* obj, void* userData);
bool XFuncCodeMap_remove(XFuncCodeMap* map, uint8_t code);
XFuncCodeNode* XFuncCodeMap_value(XFuncCodeMap* map, uint8_t code);
bool XFuncCodeMap_clear(XFuncCodeMap* map);
void XFuncCodeMap_delete(XFuncCodeMap* map);
#endif // !XFuncCodeMap_H
