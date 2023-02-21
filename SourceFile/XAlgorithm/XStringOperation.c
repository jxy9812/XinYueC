#include"XStringOperation.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
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
			if (str[sLen - i - 1] == fchar[j])
				return str + sLen - i - 1;
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
		size_t n = 0;
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == fchar[j])
				break;
			n++;
		}
		if (n == fLen)
			return str + i;
	}
	return NULL;
}
char* string_find_last_not_of(const char* str, const char* fchar)
{
	size_t sLen = strlen(str);
	size_t fLen = strlen(fchar);
	for (size_t i = 0; i < sLen; i++)
	{
		size_t n = 0;
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen - i - 1] == fchar[j])
				break;
			n++;
		}
		if (n == fLen)
			return str + sLen - i - 1;
	}
	return NULL;
}
bool string_erase(char* str, const char* pc)
{
	if (str > pc || str == NULL || pc == NULL)
		return false;
	int eraseCharLen = strlen(pc);
	if (eraseCharLen == 0)
		return false;
	char* start = string_find_first_of(str, pc);
	if (start == NULL)
		return false;
	strcpy(start, start + 1);
	return true;
}
void Unblank(char* str, const  enum UnblankType type)
{
	if (type & middle)
	{
		char* pleft = string_find_first_not_of(str, "\r\t\n ");
		char* pright = string_find_last_not_of(str, "\r\t\n ") + 1;
		if (pleft == NULL || pright == NULL || pleft > pright)
			return;
		int middleLen = pright - pleft;
		char* pmiddle = (char*)malloc(strlen(str) + 1);
		memset(pmiddle, 0, middleLen + 1);
		strncpy(pmiddle, pleft, middleLen);
		char* f = NULL;
		do
		{
			f = string_find_first_of(pmiddle, "\r\t\n ");
			if (f != NULL)
				string_erase(pmiddle, "\r\t\n ");

		} while (f != NULL);
		strcat(pmiddle, pright);
		strcpy(pleft, pmiddle);
		free(pmiddle);
	}
	if (type & left)
	{
		char* pleft = string_find_first_not_of(str, "\r\t\n ");
		for (char* p = str; pleft != NULL && p < pleft; p++)
		{
			string_erase(str, "\r\t\n ");
		}
	}
	if (type & right)
	{
		char* pright = string_find_last_not_of(str, "\r\t\n ") + 1;
		for (char* p = pright; pright != NULL && p < str + strlen(str); p++)
		{
			string_erase(str, "\r\t\n ");
		}
	}
}
