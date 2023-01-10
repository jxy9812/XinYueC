#include"list.h"
#include"list_head.h"
#include<string.h>
list* Newlist(int size)
{
	LIST* li = malloc(sizeof(LIST));
	li->push_front = List_push_front;
	li->push_back = List_push_back;
	li->insert_front_p = List_insert_front_p;
	li->insert_front_int = List_insert_front_int;
	li->insert = List_insert;
	li->at = List_at;
	li->pop_front = List_pop_front;
	li->pop_back = List_pop_back;
	li->erase_p = List_erase_p;
	li->erase_int = List_erase_int;
	li->clear = List_clear;
	li->at = List_at;
	li->front = List_front;
	li->back = List_back;
	li->find = List_find;
	li->empty = List_empty;
	li->size = List_size;
	li->sort = List_sort;
	li->swap = List_swap;
	li->_type = size;
	li->_current = 0;
	li->_date = NULL;
	return li;
}