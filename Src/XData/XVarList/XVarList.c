#include"XVarList.h"
#include"XMemory.h"
#include<stdarg.h>
#include<string.h>
XVarList* XVarList_create(uint8_t count, ...)
{
    if (count % 2 == 1)
        return NULL;
    va_list ap;       // 声明参数指针
    va_start(ap, count);  // 初始化 ap，指向第一个可变参数
    size_t sumTypeSize = 0;
    for (int i = 0; i < count; i++) 
    {
        if(i%2==0)
            sumTypeSize += va_arg(ap, int);
        else 
            va_arg(ap, void*);
        //printf("%d\t", va_arg(ap, int));  // 获取 int 类型参数，累加
    }
    va_end(ap);  // 结束访问
    uint8_t* list = XMemory_malloc(sumTypeSize+sizeof(uint8_t*));
    XVarList_start(list);//指向开头
    uint8_t* ptr = list+sizeof(uint8_t*);
    //void* data = NULL;
    size_t typeSize = 0;
    va_start(ap, count);  // 初始化 ap，指向第一个可变参数
    for (int i = 0; i < count; i++)
    {
        if (i % 2 == 1)
        {
            //data= va_arg(ap, void*);
            memcpy(ptr, va_arg(ap, void*),typeSize);
            ptr += typeSize;
        }
        else
        {
            typeSize=va_arg(ap,int);
        }
    }
    va_end(ap);  // 结束访问
	return list;
}