#include "XDocPropsApp.h"
#include "XMemory.h"
#include "XMap.h"
#include <stdlib.h>

#include <string.h>


/* XString* 比较函数（比较两个 XString* 的 UTF-8 内容） */
static int32_t str_compare(const void* lhs, const void* rhs)
{
    XString* a = *(XString**)lhs;
    XString* b = *(XString**)rhs;
    if (!a && !b) return XCompare_Equality;
    if (!a) return XCompare_Less;
    if (!b) return XCompare_Greater;
    int ret = strcmp(XString_toUtf8(a), XString_toUtf8(b));
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
    self->m_properties = XMap_create(sizeof(XString*), sizeof(XString*), str_compare);
    return self;
}

void XDocPropsApp_delete(XDocPropsApp* self)
{
    if (!self) return;
    if (self->m_titlesOfPartsList) XStringList_delete_base(self->m_titlesOfPartsList);
    if (self->m_headingPairsList) { XVector_deinit_base(self->m_headingPairsList); XFree_System(self->m_headingPairsList); }
    if (self->m_properties)
    {
        /* 释放所有 XString* 键和值 */
        XMap_iterator it = XMap_begin(self->m_properties);
        XMap_iterator end = XMap_end(self->m_properties);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it))
        {
            XPair* pair = XMap_iterator_data(&it);
            if (pair)
            {
                XString* key = *(XString**)XPair_first(pair);
                if (key) { XString_deinit_base(key); XFree_System(key); }
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

void XDocPropsApp_addPartTitle(XDocPropsApp* self, const XString* title)
{
    if (self && title) {
        XString* t = (XString*)title;
        XStringList_push_back_base(self->m_titlesOfPartsList, &t);
    }
}

void XDocPropsApp_addHeadingPair(XDocPropsApp* self, const XString* name, int value)
{
    (void)self; (void)name; (void)value;
}

bool XDocPropsApp_setProperty(XDocPropsApp* self, const XString* name, const XString* value)
{
    if (!self || !name || !value) return false;
    XString* keyStr = XString_create_copy(name);
    if (!keyStr) return false;
    XString* valStr = XString_create_copy(value);
    if (!valStr) { XString_deinit_base(keyStr); XFree_System(keyStr); return false; }
    XMap_insert_base(self->m_properties, &keyStr, &valStr);
    return true;
}

const XString* XDocPropsApp_property(const XDocPropsApp* self, const XString* name)
{
    if (!self || !name) return NULL;
    XMap_iterator it = XMap_begin(self->m_properties);
    XMap_iterator end = XMap_end(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it))
    {
        XPair* pair = XMap_iterator_data(&it);
        if (pair)
        {
            XString* key = *(XString**)XPair_first(pair);
            if (key && XString_equals(key, name, XChar_CaseSensitive))
            {
                XString* val = *(XString**)XPair_second(pair);
                return val;
            }
        }
    }
    return NULL;
}

int XDocPropsApp_propertyNames(const XDocPropsApp* self, XString*** names)
{
    (void)self; (void)names;
    return 0;
}

bool XDocPropsApp_saveToXmlFile(XDocPropsApp* self, const XString* filePath)
{
    (void)self; (void)filePath;
    return false;
}

bool XDocPropsApp_loadFromXmlFile(XDocPropsApp* self, const XString* filePath)
{
    (void)self; (void)filePath;
    return false;
}
