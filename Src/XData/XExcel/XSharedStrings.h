/******************************************************************************
 * @file       XSharedStrings.h
 * @brief      XSharedStrings 共享字符串表类
 * @author     XinYueC 团队
 * @note       管理 OOXML 共享字符串表
 ******************************************************************************/
#ifndef XSHAREDSTRINGS_H
#define XSHAREDSTRINGS_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"
#include "XStringList.h"
#include "XVector.h"
#include "XMap.h"
#include "XRichString.h"
#include "XAbstractOOXmlFile.h"
typedef struct XlsxSharedStringInfo { int m_index; int m_count; } XlsxSharedStringInfo;
typedef struct XSharedStrings {
    XAbstractOOXmlFile m_base;
    XMap* m_stringTable;
    XVector* m_stringList;
    int m_stringCount;
} XSharedStrings;
XSharedStrings* XSharedStrings_create(XAbstractOOXmlFile_CreateFlag flag);
void XSharedStrings_delete(XSharedStrings* self);
int XSharedStrings_count(const XSharedStrings* self);
bool XSharedStrings_isEmpty(const XSharedStrings* self);
int XSharedStrings_addSharedString(XSharedStrings* self, const char* string);
int XSharedStrings_addSharedRichString(XSharedStrings* self, const XRichString* rich);
void XSharedStrings_removeSharedString(XSharedStrings* self, const char* string);
void XSharedStrings_incRefByStringIndex(XSharedStrings* self, int idx);
int XSharedStrings_getSharedStringIndex(XSharedStrings* self, const char* string);
XRichString* XSharedStrings_getSharedString(XSharedStrings* self, int index);
XVector* XSharedStrings_getSharedStrings(XSharedStrings* self);
#ifdef __cplusplus
}
#endif
#endif
