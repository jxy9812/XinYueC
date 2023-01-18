#ifndef STRING_HEAD
#define STRING_HEAD
struct vector;
struct STRING
{
	//插入函数
	void(*append)(struct STRING* this_string,const char* str);//尾插
	void (*assign)(struct STRING* this_string, const char* str);// 赋值
	void (*insert)(struct STRING* this_string, const int nSel,const char* str);// 第索引处开始插入字符串
	//删除函数
	void (*pop_back)(struct STRING* this_string);//尾删
	void (*erase)(struct STRING* this_string, const int nSel, const int str);//删除索引处开始的n个字符
	void (*clear) (struct STRING* this_string);//清空，释放内存
	//遍历函数
	char (*at)(const struct STRING* this_string, int);// 返回索引处字符
	char* (*data)(const struct STRING* this_string);// 返回字符串
	//查找函数
	int (*find_first_of)(const struct STRING* this_string, const char* find);
	int (*find_last_of)(const struct STRING* this_string, const char* find);
	int (*find_first_not_of)(const struct STRING* this_string, const char* find);
	int (*find_last_not_of)(const struct STRING* this_string, const char* find);
	//判断函数
	bool (*empty)(const struct STRING* this_string);// 
	//大小函数
	int (*size)(const struct STRING* this_string);//
	int (*capacity)(const struct STRING* this_string); //
	//其他函数
	void (*swap)(struct STRING* stringOne, struct STRING* stringTwo);//交换
	//释放容器
	void (*release)(const struct STRING* this_string);
	vector* _data;
};
#endif 
