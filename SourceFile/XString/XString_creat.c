#include"XString.h"
#include"XString_head.h"
#include"XVector.h"
#include"XContainerObject.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
struct XString* XString_init()
{
	struct XSTRING* this_XString = malloc(sizeof(struct XSTRING));
	if (isObjectNULL(this_XString,"XString_init-malloc"))
		return NULL;
	XVector* vector= XVector_init("char", sizeof(char));
	if (isObjectNULL(vector, "XString_init-XVector_init"))
		return NULL;
	this_XString->_data = vector;
	//初始化数组为0;
	char zero = '\0';
	XVector_Push_Back(vector, &zero);
	this_XString->append = XString_append;
	this_XString->assign = XString_assign;
	this_XString->at = XString_at;
	this_XString->capacity = XString_capacity;
	this_XString->clear = XString_clear;
	this_XString->data = XString_data;
	this_XString->empty = XString_empty;
	this_XString->erase = XString_erase;
	this_XString->find_first_not_of = XString_find_first_not_of;
	this_XString->find_first_of = XString_find_first_of;
	this_XString->find_last_not_of = XString_find_last_not_of;
	this_XString->find_last_of = XString_find_last_of;
	this_XString->free = XString_free;
	this_XString->insert = XString_insert;
	this_XString->pop_back = XString_pop_back;
	this_XString->size = XString_size;
	this_XString->swap = XString_swap;
}