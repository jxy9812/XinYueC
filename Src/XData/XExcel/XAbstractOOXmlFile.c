#include "XAbstractOOXmlFile.h"
#include "XMemory.h"
#include <stdlib.h>

#include <string.h>


void XAbstractOOXmlFile_init(XAbstractOOXmlFile* self, XAbstractOOXmlFile_CreateFlag flag)
{
    if (!self) return;
    memset(self, 0, sizeof(XAbstractOOXmlFile));
    self->m_createFlag = flag;
    self->m_relationships = XRelationships_create();
}

void XAbstractOOXmlFile_deinit(XAbstractOOXmlFile* self)
{
    if (!self) return;
    if (self->m_filePath) { XString_deinit_base(self->m_filePath); XFree_System(self->m_filePath); }
    if (self->m_relationships) XRelationships_delete(self->m_relationships);
}

XRelationships* XAbstractOOXmlFile_relationships(const XAbstractOOXmlFile* self)
{ return self ? self->m_relationships : NULL; }

void XAbstractOOXmlFile_setFilePath(XAbstractOOXmlFile* self, const char* path)
{
    if (!self) return;
    if (!self->m_filePath) self->m_filePath = XString_create();
    if (self->m_filePath) { XString_clear_base(self->m_filePath); XString_append_utf8(self->m_filePath, path); }
}

const char* XAbstractOOXmlFile_filePath(const XAbstractOOXmlFile* self)
{ return (self && self->m_filePath) ? XString_toUtf8(self->m_filePath) : ""; }
