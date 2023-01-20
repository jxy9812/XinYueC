#ifndef XALGORITHM_H
#define XALGORITHM_H
#include"XSort/XSort.h"
//交换任意数据类型的函数
void swap(void* valOne, void* valTwo, const int typeSize);
//在该字符串str中查找第一个属于字符串sfchar中的字符返回其指针，失败为NULL
char* string_find_first_of(const char* str, const char* fchar);
//在该字符串str最后中查找第一个属于字符串sfchar中的字符返回其指针，失败为NULL
char* string_find_last_of(const char* str, const char* fchar);
//在该字符串str中查找第一个不属于字符串sfchar中的字符返回其指针，失败为NULL
char* string_find_first_not_of(const char* str, const char* fchar);
//在该字符串str最后中查找第一个不属于字符串sfchar中的字符返回其指针，失败为NULL
char* string_find_last_not_of(const char* str, const char* fchar);
//删除在该字符串str中查找第一个属于字符串pc中的字符仅删除匹配到的第一个字符
bool string_erase(char* str, const char* pc);
enum UnblankType//去空格类型
{
	left = 1,//左
	right = 2,//右
	middle = 4//中
};
//去空格
void Unblank(char* str, const  enum UnblankType type);
#endif // !XALGORITHM_H

