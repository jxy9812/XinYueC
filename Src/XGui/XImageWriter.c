/******************************************************************************
 * @file       XImageWriter.c
 * @brief      XImageWriter 图像写入器实现（对标 Qt 6.8 QImageWriter）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageWriter.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/**
 * @brief      XImageWriter 私有数据
 */
typedef struct XImageWriterPrivate
{
    XIODevice*  m_device;            /**< IO 设备 */
    char*       m_fileName;          /**< 文件名 */
    char*       m_format;            /**< 格式字符串 */
    int         m_quality;           /**< 质量参数 */
    int         m_compression;       /**< 压缩参数 */
    char*       m_subType;           /**< 子类型 */
    bool        m_optimizedWrite;    /**< 是否优化写入 */
    bool        m_progressiveScan;   /**< 是否渐进式扫描 */
    XImageIOHandlerTransformation m_transformation; /**< 变换类型 */
    XImageWriterError m_error;       /**< 错误码 */
    char*       m_errorString;       /**< 错误描述 */
}XImageWriterPrivate;

static void VXImageWriter_deinit(XImageWriter* self)
{
    if (ISNULL(self, "XImageWriter")) return;
    if (self->m_data)
    {
        if (self->m_data->m_fileName) XFree_System(self->m_data->m_fileName);
        if (self->m_data->m_format) XFree_System(self->m_data->m_format);
        if (self->m_data->m_subType) XFree_System(self->m_data->m_subType);
        if (self->m_data->m_errorString) XFree_System(self->m_data->m_errorString);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
}

XVtable* XImageWriter_class_init()
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XImageWriter));
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageWriter_deinit);
    return XVTABLE_DEFAULT;
}

XImageWriter* XImageWriter_create()
{
    XImageWriter* self = (XImageWriter*)XMalloc_System(sizeof(XImageWriter));
    if (!self) return NULL;
    XImageWriter_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XImageWriter_init(XImageWriter* self)
{
    if (ISNULL(self, "XImageWriter")) return;
    memset(self, 0, sizeof(XImageWriter));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XImageWriter);
    self->m_data = (XImageWriterPrivate*)XMalloc_System(sizeof(XImageWriterPrivate));
    if (self->m_data)
    {
        memset(self->m_data, 0, sizeof(XImageWriterPrivate));
        self->m_data->m_quality = -1;
    }
}

void XImageWriter_init_device(XImageWriter* self, XIODevice* device, const char* format)
{
    XImageWriter_init(self);
    if (self->m_data)
    {
        self->m_data->m_device = device;
        if (format && format[0])
        {
            self->m_data->m_format = (char*)XMalloc_System(strlen(format) + 1);
            if (self->m_data->m_format)
            {
                char* p = self->m_data->m_format;
                strcpy(p, format);
                while (*p) { *p = (char)toupper(*p); p++; }
            }
        }
    }
}

void XImageWriter_init_file(XImageWriter* self, const char* fileName, const char* format)
{
    XImageWriter_init(self);
    if (self->m_data && fileName)
    {
        self->m_data->m_fileName = (char*)XMalloc_System(strlen(fileName) + 1);
        if (self->m_data->m_fileName) strcpy(self->m_data->m_fileName, fileName);
        if (format && format[0])
        {
            self->m_data->m_format = (char*)XMalloc_System(strlen(format) + 1);
            if (self->m_data->m_format)
            {
                char* p = self->m_data->m_format;
                strcpy(p, format);
                while (*p) { *p = (char)toupper(*p); p++; }
            }
        }
    }
}

void XImageWriter_deinit(XImageWriter* self) { XImageWriter_deinit_base(self); }
void XImageWriter_deinit_base(XImageWriter* self)
{
    if (ISNULL(self, "XImageWriter") || ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XImageWriter*))(self);
}

void XImageWriter_setFormat(XImageWriter* self, const char* format)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_format) XFree_System(self->m_data->m_format);
    self->m_data->m_format = format ? (char*)XMalloc_System(strlen(format) + 1) : NULL;
    if (format && self->m_data->m_format)
    {
        char* p = self->m_data->m_format;
        strcpy(p, format);
        while (*p) { *p = (char)toupper(*p); p++; }
    }
}

const char* XImageWriter_format(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_format : NULL; }

void XImageWriter_setDevice(XImageWriter* self, XIODevice* device)
{ if (self && self->m_data) self->m_data->m_device = device; }

XIODevice* XImageWriter_device(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_device : NULL; }

void XImageWriter_setFileName(XImageWriter* self, const char* fileName)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_fileName) XFree_System(self->m_data->m_fileName);
    self->m_data->m_fileName = fileName ? (char*)XMalloc_System(strlen(fileName) + 1) : NULL;
    if (fileName && self->m_data->m_fileName) strcpy(self->m_data->m_fileName, fileName);
}

const char* XImageWriter_fileName(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_fileName : NULL; }

void XImageWriter_setQuality(XImageWriter* self, int quality)
{ if (self && self->m_data) self->m_data->m_quality = quality; }

int XImageWriter_quality(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_quality : -1; }

void XImageWriter_setCompression(XImageWriter* self, int compression)
{ if (self && self->m_data) self->m_data->m_compression = compression; }

int XImageWriter_compression(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_compression : 0; }

void XImageWriter_setSubType(XImageWriter* self, const char* type)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_subType) XFree_System(self->m_data->m_subType);
    self->m_data->m_subType = type ? (char*)XMalloc_System(strlen(type) + 1) : NULL;
    if (type && self->m_data->m_subType) strcpy(self->m_data->m_subType, type);
}

const char* XImageWriter_subType(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_subType : NULL; }

void* XImageWriter_supportedSubTypes(const XImageWriter* self) { (void)self; return NULL; }

void XImageWriter_setOptimizedWrite(XImageWriter* self, bool optimize)
{ if (self && self->m_data) self->m_data->m_optimizedWrite = optimize; }

bool XImageWriter_optimizedWrite(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_optimizedWrite : false; }

void XImageWriter_setProgressiveScanWrite(XImageWriter* self, bool progressive)
{ if (self && self->m_data) self->m_data->m_progressiveScan = progressive; }

bool XImageWriter_progressiveScanWrite(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_progressiveScan : false; }

XImageIOHandlerTransformation XImageWriter_transformation(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_transformation : XImageIOHandlerTransformation_None; }

void XImageWriter_setTransformation(XImageWriter* self, XImageIOHandlerTransformation orientation)
{ if (self && self->m_data) self->m_data->m_transformation = orientation; }

void XImageWriter_setText(XImageWriter* self, const char* key, const char* text)
{ (void)self; (void)key; (void)text; }

bool XImageWriter_canWrite(const XImageWriter* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_fileName != NULL || self->m_data->m_device != NULL;
}

bool XImageWriter_write(XImageWriter* self, const XImage* image)
{
    if (!self || !self->m_data || !image) return false;
    // 暂未实现实际编码，返回 false
    return false;
}

XImageWriterError XImageWriter_error(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_error : XImageWriterError_UnknownError; }

const char* XImageWriter_errorString(const XImageWriter* self)
{ return (self && self->m_data) ? self->m_data->m_errorString : NULL; }

bool XImageWriter_supportsOption(const XImageWriter* self, XImageIOHandlerOption option)
{
    (void)self;
    (void)option;
    return false;
}

void* XImageWriter_supportedImageFormats() { return NULL; }
void* XImageWriter_supportedMimeTypes() { return NULL; }
void* XImageWriter_imageFormatsForMimeType(const char* mimeType) { (void)mimeType; return NULL; }

