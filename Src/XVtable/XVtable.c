#include"XVtable.h"
#include"XContainerObject.h"
#include<stdlib.h>
#include<string.h>
#define VECTORNUM 4//初始数组大小
//检测是否需要扩容
static void XVtableEnlargeCapacity(XVtable* this_vtable)
{
	if (ISNULL(this_vtable, ""))
		return;
	if (this_vtable->capacity == 0)
	{
		this_vtable->data = malloc(sizeof(void*)* VECTORNUM);
		if (this_vtable->data == NULL)
		{
			perror("初始化vector失败");
			exit(-1);
		}
		else
		{
			this_vtable->capacity = VECTORNUM;
		}
	}
	else if (this_vtable->capacity == this_vtable->size)//空间已满需要扩容
	{
		void* _data = realloc(this_vtable->data, this_vtable->capacity * sizeof(void*) * 1.5);
		if (_data == NULL)
		{
			perror("扩容失败vector");
			exit(-1);
		}
		else
		{
			this_vtable->data = _data;
			this_vtable->capacity *= 1.5;
		}
	}
}
XVtable* XVtable_new()
{
	XVtable* this_vtable = malloc(sizeof(XVtable));
	XVtable_init(this_vtable);
	return this_vtable;
}

void XVtable_init(XVtable* this_vtable)
{
	if (ISNULL(this_vtable, ""))
		return NULL;
	this_vtable->data = NULL;
	this_vtable->capacity = 0;
	this_vtable->size = 0;
	//XVtableEnlargeCapacity(this_vtable);
}

void XVtable_insert(XVtable* this_vtable, int64_t index, const void* func)
{
	if (ISNULL(this_vtable, ""))
		return;
	const void* ptr = (void*)(this_vtable->data + index);
	size_t typeSize = sizeof(void*);
	if (!XVtable_empty(this_vtable) && ptr >= this_vtable->data && (index == 0 || ptr <= this_vtable->data + (index - 1)))
	{
		int64_t size = (char*)(&this_vtable->data[this_vtable->size - 1]) - (char*)ptr + typeSize;
		void* temp = malloc(size);
		memcpy(temp, ptr, size);
		int64_t sizen = ((char*)ptr - (char*)this_vtable->data) / typeSize;
		for (size_t i = 0; i < 1; i++)
		{
			XVtableEnlargeCapacity(this_vtable);
			memcpy(this_vtable->data + sizen, &func, typeSize);
			sizen++;
			this_vtable->size++;
		}
		memcpy(this_vtable->data + sizen, temp, size);
		free(temp);
	}
}

void XVtable_insertArray(XVtable* this_vtable, int64_t index, const void** begin, size_t n)
{
	if (ISNULL(this_vtable, ""))
		return;
	//XVtableEnlargeCapacity(this_vtable);
	const void* ptr = (void*)(this_vtable->data + index);
	size_t typeSize = sizeof(void*);
	if (!XVtable_empty(this_vtable) && ptr >= this_vtable->data && (index == 0 || ptr <= this_vtable->data + (index - 1)))
	{
		int64_t size = (char*)(&this_vtable->data[this_vtable->size-1]) - (char*)ptr + typeSize;
		void* temp = malloc(size);
		memcpy(temp, ptr, size);
		int64_t sizen = ((char*)ptr - (char*)this_vtable->data) / typeSize;
		for (size_t i = 0; i < n; i++)
		{
			XVtableEnlargeCapacity(this_vtable);
			memcpy(this_vtable->data + sizen, (char*)begin + i * typeSize, typeSize);
			sizen++;
			this_vtable->size++;
		}
		memcpy(this_vtable->data + sizen, temp, size);
		free(temp);
	}
	else if (XVtable_empty(this_vtable))
	{
		for (size_t i = 0; i < n; i++)
		{
			XVtable_push_back(this_vtable, begin[i]);
		}
	}
}

void XVtable_push_back(XVtable* this_vtable, void* func)
{
	if (ISNULL(this_vtable, ""))
		return;
	XVtableEnlargeCapacity(this_vtable);
	char* ptr = (char*)this_vtable->data + this_vtable->size * sizeof(void*);
	memcpy(ptr, &func, sizeof(void*));
	++this_vtable->size;
}

void XVtable_pop_back(XVtable* this_vtable)
{
	if (XVtable_empty(this_vtable))
		return ;
	--this_vtable->size;
}

void XVtable_clear(XVtable* this_vtable)
{
	if (XVtable_empty(this_vtable))
		return;
	this_vtable->size = 0;
}

bool XVtable_empty(XVtable* this_vtable)
{
	if (ISNULL(this_vtable, ""))
		return true;
	return this_vtable->size==0;
}

size_t XVtable_size(XVtable* this_vtable)
{
	if (ISNULL(this_vtable, ""))
		return 0;
	return this_vtable->size;
}

void* XVtable_at(XVtable* this_vtable, int64_t index)
{
	if (ISNULL(this_vtable, ""))
		return NULL;
	if (index<0 || index + 1 > this_vtable->size)
	{
		return NULL;
	}
	return *(this_vtable->data + index);
}
