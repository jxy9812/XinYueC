#include "XDocPropsApp.h"
#include "XMemory.h"
#include <stdlib.h>#include <string.h>
XDocPropsApp* XDocPropsApp_create(XAbstractOOXmlFile_CreateFlag flag) {
    XDocPropsApp* self = (XDocPropsApp*)XMalloc_System(sizeof(XDocPropsApp));
    if (!self) return NULL; memset(self, 0, sizeof(XDocPropsApp));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_titlesOfPartsList = XStringList_create();
    self->m_headingPairsList = XVector_Create(XPair);
    self->m_properties = XMap_create();
    return self;
}
void XDocPropsApp_delete(XDocPropsApp* self) {
    if (!self) return;
    if (self->m_titlesOfPartsList) XStringList_delete(self->m_titlesOfPartsList);
    if (self->m_headingPairsList) XFree_System(self->m_headingPairsList);
    if (self->m_properties) XFree_System(self->m_properties);
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XDocPropsApp_addPartTitle(XDocPropsApp* self, const char* title) { if (self && title) XStringList_append_utf8(self->m_titlesOfPartsList, title); }
void XDocPropsApp_addHeadingPair(XDocPropsApp* self, const char* name, int value) { (void)self; (void)name; (void)value; }
bool XDocPropsApp_setProperty(XDocPropsApp* self, const char* name, const char* value) {
    if (!self || !name || !value) return false;
    XMap_insert(self->m_properties, name, value);
    return true;
}
const char* XDocPropsApp_property(const XDocPropsApp* self, const char* name) {
    if (!self || !name) return "";
    XString* val = (XString*)XMap_value(self->m_properties, name);
    return val ? XString_toUtf8_const(val) : "";
}
int XDocPropsApp_propertyNames(const XDocPropsApp* self, XString*** names) { (void)self; (void)names; return 0; }
bool XDocPropsApp_saveToXmlFile(XDocPropsApp* self, const char* filePath) { (void)self; (void)filePath; return false; }
bool XDocPropsApp_loadFromXmlFile(XDocPropsApp* self, const char* filePath) { (void)self; (void)filePath; return false; }
