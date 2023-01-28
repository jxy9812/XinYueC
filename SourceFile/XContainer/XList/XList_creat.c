#include"XList.h"
#include"XList_head.h"
#include<string.h>
#include<stdlib.h>
XList* XList_init(int TypeSize)
{
	XLIST* this_list = malloc(sizeof(XLIST));
	this_list->push_front = XList_push_front;
	this_list->push_back = XList_push_back;
	this_list->insert_front_p = XList_insert_front_p;
	this_list->insert_front_int = XList_insert_front_int;
	this_list->insert = XList_insert;
	this_list->at = XList_at;
	this_list->pop_front = XList_pop_front;
	this_list->pop_back = XList_pop_back;
	this_list->erase_p = XList_erase_p;
	this_list->erase_int = XList_erase_int;
	this_list->clear = XList_clear;
	this_list->at = XList_at;
	this_list->front = XList_front;
	this_list->back = XList_back;
	this_list->find = XList_find;
	this_list->empty = XList_empty;
	this_list->size = XList_size;
	this_list->sort = XList_sort;
	this_list->swap = XList_swap;
	this_list->free = XList_free;
	XContainerObject_init(&this_list->object, TypeSize);
	return this_list;
}