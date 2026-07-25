#include "XDocPropsCore.h"
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

XDocPropsCore* XDocPropsCore_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XDocPropsCore* self = (XDocPropsCore*)XMalloc_System(sizeof(XDocPropsCore));
    if (!self) return NULL;
    memset(self, 0, sizeof(XDocPropsCore));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_properties = XMap_create(sizeof(XString*), sizeof(XString*), str_compare);
    return self;
}

void XDocPropsCore_delete(XDocPropsCore* self)
{
    if (!self) return;
    if (self->m_properties)
    {
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

bool XDocPropsCore_setProperty(XDocPropsCore* self, const XString* name, const XString* value)
{
    if (!self || !name || !value) return false;
    XString* keyStr = XString_create_copy(name);
    if (!keyStr) return false;
    XString* valStr = XString_create_copy(value);
    if (!valStr) { XString_deinit_base(keyStr); XFree_System(keyStr); return false; }
    XMap_insert_base(self->m_properties, &keyStr, &valStr);
    return true;
}

const XString* XDocPropsCore_property(const XDocPropsCore* self, const XString* name)
{
    if (!self || !name) return NULL;
    XMap* map = (XMap*)self->m_properties;
    XMap_iterator it = XMap_begin(map);
    XMap_iterator end = XMap_end(map);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(map, &it))
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

int XDocPropsCore_propertyNames(const XDocPropsCore* self, XString*** names)
{
    if (!self || !names) return 0;
    XMap* map = (XMap*)self->m_properties;
    if (!map) { *names = NULL; return 0; }
    int count = (int)XMap_size_base(map);
    if (count == 0) { *names = NULL; return 0; }
    *names = (XString**)XMalloc_System(sizeof(XString*) * (size_t)count);
    if (!*names) return 0;
    int idx = 0;
    XMap_iterator it = XMap_begin(map);
    XMap_iterator end = XMap_end(map);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(map, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        if (pair) {
            XString* key = *(XString**)XPair_first(pair);
            XString* ks = XString_create_copy(key);
            (*names)[idx++] = ks;
        }
    }
    return count;
}

bool XDocPropsCore_saveToXmlFile(XDocPropsCore* self, const XString* filePath)
{
    (void)self; (void)filePath;
    return false;
}

bool XDocPropsCore_loadFromXmlFile(XDocPropsCore* self, const XString* filePath)
{
    (void)self; (void)filePath;
    return false;
}
