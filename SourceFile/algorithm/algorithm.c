#include"algorithm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
void swap(void* x, void* y, const int n)//交换任意数据类型的函数
{
	void* p = malloc(n);
	if (p == NULL)
	{
		perror("交换函数创建p临时空间失败");
		exit(-1);
	}
	memcpy(p, x, n);
	memcpy(x, y, n);
	memcpy(y, p, n);
	free(p);
}
//查找
char* string_find_first_of(const char* str, const char* fchar)
{
	size_t sLen = strlen(str); 
	size_t fLen = strlen(fchar);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == fchar[j])
				return str + i;
		}
	}
	return NULL;
}
char* string_find_last_of(const char* str, const char* fchar)
{
	size_t sLen = strlen(str);
	size_t fLen = strlen(fchar);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen-i-1] == fchar[j])
				return str +sLen - i - 1;
		}
	}
	return NULL;
}
char* string_find_first_not_of(const char* str, const char* fchar)
{
	size_t sLen = strlen(str);
	size_t fLen = strlen(fchar);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] != fchar[j])
				return str + i;
		}
	}
	return NULL;
}
char* string_find_last_not_of(const char* str, const char* fchar)
{
	size_t sLen = strlen(str);
	size_t fLen = strlen(fchar);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen - i - 1] != fchar[j])
				return str + sLen - i - 1;
		}
	}
	return NULL;
}
bool string_erase(const char* str, const char* pc)
{
	if(str>pc||str==NULL||pc==NULL)
	return false;
	int eraseCharLen = strlen(pc);
	if (eraseCharLen == 0)
		return false;

}
void Unblank(const char* str,const  enum UnblankType type)
{
	if (type == middle)
	{
		char* pleft = string_find_first_not_of(str," \r\n\t");
		char* pright = string_find_last_not_of(str, " \r\n\t")+1;
		if (pleft == NULL || pright == NULL|| pleft> pright)
			return;
		int middleLen = pright - pleft;
		char* pmiddle =(char*)malloc(middleLen+1);
		strncpy(pmiddle, pleft, middleLen);
		char*f= NULL;
		//do
		{
			f = string_find_first_of(pmiddle,"\r\t\n ");
			//if (f != NULL)
			//	temp.erase(sel, 1);
			//else
				//break;
		} //while (f!=NULL);
		//data.replace(nLeft, len, temp);
	}
	if (type == left)
	{

	}
	if (type == right)
	{

	}
}
