#include<stdbool.h>
//交换任意数据类型的函数
void swap(void* x, void* y, const int n);//交换任意数据类型的函数
//在该字符串str中查找第一个属于字符串sfchar的字符返回其指针，失败为NULL
char* string_find_first_of(const char* str, const char* fchar);
//在该字符串str最后中查找第一个属于字符串sfchar的字符返回其指针，失败为NULL
char* string_find_last_of(const char* str, const char* fchar);
char* string_find_first_not_of(const char* str, const char* fchar);
char* string_find_last_not_of(const char* str, const char* fchar);
bool string_erase(const char* str, const char* pc);
enum UnblankType//去空格类型
{
	left,//左
	right,//右
	middle//中
};
//去空格
void Unblank(const char* str, const  enum UnblankType type);