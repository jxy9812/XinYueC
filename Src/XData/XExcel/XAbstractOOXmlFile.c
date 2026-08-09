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
    if (self->m_filePath) XString_delete_base(self->m_filePath);
    if (self->m_relationships) XRelationships_delete(self->m_relationships);
}

XRelationships* XAbstractOOXmlFile_relationships(const XAbstractOOXmlFile* self)
{ return self ? self->m_relationships : NULL; }

void XAbstractOOXmlFile_setFilePath(XAbstractOOXmlFile* self, const XString* path)
{
    if (!self) return;
    if (!self->m_filePath) self->m_filePath = XString_create();
    if (self->m_filePath) { XString_clear_base(self->m_filePath); if (path) XString_append(self->m_filePath, path); }
}

const XString* XAbstractOOXmlFile_filePath(const XAbstractOOXmlFile* self)
{ return (self && self->m_filePath) ? self->m_filePath : NULL; }
