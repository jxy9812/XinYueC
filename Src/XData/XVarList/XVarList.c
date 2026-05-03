#include"XVarList.h"
#include"XMultiPool.h"
#include<stdarg.h>
#include<string.h>
void XVarList_delete(XVarList* list)
{
    if (list)
    {
        if (list->argsDel)
            list->argsDel(list);
        if (list->m_free)
            list->m_free(list);
    }
}
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
    XVarList* list = NULL;
    if (XMultiPool_global())
    {
        list= XMultiPool_global_malloc(ALIGN_UP(sumTypeSize + sizeof(XVarList), sizeof(void*)));
        if(list)
            list->m_free = XMultiPool_global_free;
    }
    if (!list)
    {
        list = XMalloc(ALIGN_UP(sumTypeSize + sizeof(XVarList), sizeof(void*)));
        list->m_free = XFree;
    }
    if (!list)return NULL;

    XVarList_setArgsDel(list,NULL);
    XVarList_start(list);//指向开头
    uint8_t* ptr = *((uint8_t**)list);
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