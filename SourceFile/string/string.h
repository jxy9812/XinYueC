#ifndef STRING_H
#define STRING_H
#include<stdbool.h>

struct string
{
	//插入函数
	void(*append)(struct string* this_string, const char* str);//尾插
	void (*assign)(struct string* this_string, const char* str);// 赋值
	void (*insert)(struct string* this_string, const int nSel, const char* str);// 第索引处开始插入字符串
	//删除函数
	void (*pop_back)(struct string* this_string);//尾删
	void (*erase)(struct string* this_string, const int nSel, const int str);//删除索引处开始的n个字符
	void (*clear) (struct string* this_string);//清空字符串
	//遍历函数
	char (*at)(const struct string* this_string, int);// 返回索引处字符
	char* (*data)(const struct string* this_string);// 返回字符串
	//查找函数
	int (*find_first_of)(const struct string* this_string, const char* find);
	int (*find_last_of)(const struct string* this_string, const char* find);
	int (*find_first_not_of)(const struct string* this_string, const char* find);
	int (*find_last_not_of)(const struct string* this_string, const char* find);
	//判断函数
	bool (*empty)(const struct string* this_string);// 
	//大小函数
	int (*size)(const struct string* this_string);//
	int (*capacity)(const struct string* this_string); //
	//其他函数
	void (*swap)(struct string* stringOne, struct string* stringTwo);//交换
	//释放容器
	void (*release)(const struct string* this_string);
};
#endif // !STRING_H
