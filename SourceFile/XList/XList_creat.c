#include"XList.h"
#include"XList_head.h"
#include<string.h>
#include<stdlib.h>
XList* List_init(int TypeSize)
{
	XLIST* this_list = malloc(sizeof(XLIST));
	this_list->push_front = List_push_front;
	this_list->push_back = List_push_back;
	this_list->insert_front_p = List_insert_front_p;
	this_list->insert_front_int = List_insert_front_int;
	this_list->insert = List_insert;
	this_list->at = List_at;
	this_list->pop_front = List_pop_front;
	this_list->pop_back = List_pop_back;
	this_list->erase_p = List_erase_p;
	this_list->erase_int = List_erase_int;
	this_list->clear = List_clear;
	this_list->at = List_at;
	this_list->front = List_front;
	this_list->back = List_back;
	this_list->find = List_find;
	this_list->empty = List_empty;
	this_list->size = List_size;
	this_list->sort = List_sort;
	this_list->swap = List_swap;
	this_list->free = List_free;
	XContainerObject_init(&this_list->object, TypeSize);
	return this_list;
}