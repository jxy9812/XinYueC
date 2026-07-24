#include "XDocPropsApp.h"
#include "XMemory.h"
#include "XMap.h"
#include <stdlib.h>

#include <string.h>


/* 简单的 const char* 比较函数 */
static int32_t str_compare(const void* lhs, const void* rhs)
{
    const char* a = *(const char**)lhs;
    const char* b = *(const char**)rhs;
    if (!a && !b) return XCompare_Equality;
    if (!a) return XCompare_Less;
    if (!b) return XCompare_Greater;
    int ret = strcmp(a, b);
    return (ret < 0) ? XCompare_Less : (ret > 0) ? XCompare_Greater : XCompare_Equality;
}

XDocPropsApp* XDocPropsApp_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XDocPropsApp* self = (XDocPropsApp*)XMalloc_System(sizeof(XDocPropsApp));
    if (!self) return NULL;
    memset(self, 0, sizeof(XDocPropsApp));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_titlesOfPartsList = XStringList_create();
    self->m_headingPairsList = XVector_Create(XPair);
    self->m_properties = XMap_create(sizeof(const char*), sizeof(XString*), str_compare);
    return self;
}

void XDocPropsApp_delete(XDocPropsApp* self)
{
    if (!self) return;
    if (self->m_titlesOfPartsList) XStringList_delete_base(self->m_titlesOfPartsList);
    if (self->m_headingPairsList) { XVector_deinit_base(self->m_headingPairsList); XFree_System(self->m_headingPairsList); }
    if (self->m_properties)
    {
        /* 释放所有 XString* 值 */
        XMap_iterator it = XMap_begin(self->m_properties);
        XMap_iterator end = XMap_end(self->m_properties);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it))
        {
            XPair* pair = XMap_iterator_data(&it);
            if (pair)
            {
                XString* val = *(XString**)XPair_second(pair);
                if (val) { XString_deinit_base(val); XFree_System(val); }
            }
        }
        XMap_deinit_base(self->m_properties);
        XFree_System(self->m_properties);
    }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

void XDocPropsApp_addPartTitle(XDocPropsApp* self, const char* title)
{
    if (self && title) XStringList_push_back_utf8(self->m_titlesOfPartsList, title);
}

void XDocPropsApp_addHeadingPair(XDocPropsApp* self, const char* name, int value)
{
    (void)self; (void)name; (void)value;
}

bool XDocPropsApp_setProperty(XDocPropsApp* self, const char* name, const char* value)
{
    if (!self || !name || !value) return false;
    XString* valStr = XString_create();
    if (!valStr) return false;
    XString_append_utf8(valStr, value);
    XMap_insert_base(self->m_properties, &name, &valStr);
    return true;
}

const char* XDocPropsApp_property(const XDocPropsApp* self, const char* name)
{
    if (!self || !name) return "";
    XMap_iterator it = XMap_begin(self->m_properties);
    XMap_iterator end = XMap_end(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it))
    {
        XPair* pair = XMap_iterator_data(&it);
        if (pair)
        {
            const char* key = *(const char**)XPair_first(pair);
            if (key && strcmp(key, name) == 0)
            {
                XString* val = *(XString**)XPair_second(pair);
                if (val) return XString_toUtf8(val);
            }
        }
    }
    return "";
}

int XDocPropsApp_propertyNames(const XDocPropsApp* self, XString*** names)
{
    (void)self; (void)names;
    return 0;
}

bool XDocPropsApp_saveToXmlFile(XDocPropsApp* self, const char* filePath)
{
    (void)self; (void)filePath;
    return false;
}

bool XDocPropsApp_loadFromXmlFile(XDocPropsApp* self, const char* filePath)
{
    (void)self; (void)filePath;
    return false;
}
