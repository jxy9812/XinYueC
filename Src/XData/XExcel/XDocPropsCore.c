#include "XDocPropsCore.h"
#include "XMemory.h"
#include <stdlib.h>#include <string.h>
XDocPropsCore* XDocPropsCore_create(XAbstractOOXmlFile_CreateFlag flag) {
    XDocPropsCore* self = (XDocPropsCore*)XMalloc_System(sizeof(XDocPropsCore));
    if (!self) return NULL; memset(self, 0, sizeof(XDocPropsCore));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_properties = XMap_create();
    return self;
}
void XDocPropsCore_delete(XDocPropsCore* self) {
    if (!self) return;
    if (self->m_properties) XFree_System(self->m_properties);
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
bool XDocPropsCore_setProperty(XDocPropsCore* self, const char* name, const char* value) {
    if (!self || !name || !value) return false;
    XMap_insert(self->m_properties, name, value);
    return true;
}
const char* XDocPropsCore_property(const XDocPropsCore* self, const char* name) {
    if (!self || !name) return "";
    XString* val = (XString*)XMap_value(self->m_properties, name);
    return val ? XString_toUtf8_const(val) : "";
}
int XDocPropsCore_propertyNames(const XDocPropsCore* self, XString*** names) { (void)self; (void)names; return 0; }
bool XDocPropsCore_saveToXmlFile(XDocPropsCore* self, const char* filePath) { (void)self; (void)filePath; return false; }
bool XDocPropsCore_loadFromXmlFile(XDocPropsCore* self, const char* filePath) { (void)self; (void)filePath; return false; }
