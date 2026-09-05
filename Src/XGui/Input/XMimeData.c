/******************************************************************************
 * @file       XMimeData.c
 * @brief      XMimeData 剪贴板 MIME 数据容器实现（对标 Qt 6.8 QMimeData）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XMimeData.h"
#include "XMemory.h"
#include <string.h>

#if XMIMEDATA_ON

/** @brief 自定义格式登记项：格式名 + 原始字节数据。 */
typedef struct XMimeCustomEntry
{
    XString* m_format; /**< 格式名（UTF-8），由条目拥有。 */
    XString* m_data;   /**< 原始字节数据，由条目拥有。 */
} XMimeCustomEntry;

/** @brief XMimeData 私有数据块。 */
struct XMimeDataPrivate
{
    XString*       m_text;       /**< 纯文本（text/plain）。 */
    XString*       m_html;       /**< HTML（text/html）。 */
    XColor         m_color;      /**< 颜色数据（application/x-color）。 */
    bool           m_hasColor;   /**< 是否已登记颜色。 */
    XImage*        m_image;      /**< 图像（application/x-qt-image）。 */
    XVector*       m_custom;     /**< XMimeCustomEntry* 列表，自定义格式。 */
};

/** @brief 删除私有数据块中的全部资源并把指针槽位置空。 */
static void mime_clearPrivate(XMimeDataPrivate* d)
{
    size_t i;
    if (!d)
        return;
    if (d->m_text)  { XString_delete_base(d->m_text);  d->m_text  = NULL; }
    if (d->m_html)  { XString_delete_base(d->m_html);  d->m_html  = NULL; }
    if (d->m_image) { XImage_delete_base((XClass*)d->m_image); d->m_image = NULL; }
    d->m_hasColor = false;
    XColor_init_rgb(&d->m_color, 0, 0, 0, 0);
    if (d->m_custom) {
        for (i = 0; i < XVector_size_base((const XContainer*)d->m_custom); ++i) {
            XMimeCustomEntry* entry = (XMimeCustomEntry*)XVector_at_base(d->m_custom, (int64_t)i);
            if (entry) {
                if (entry->m_format) XString_delete_base(entry->m_format);
                if (entry->m_data)   XString_delete_base(entry->m_data);
                XFree_System(entry);
            }
        }
        XVector_delete_base((XClass*)d->m_custom);
        d->m_custom = NULL;
    }
}

static void VXMimeData_deinit(XMimeData* self)
{
    if (!self) return;
    if (self->m_data) {
        mime_clearPrivate(self->m_data);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXMimeData_copy(XMimeData* self, const XMimeData* other)
{
    XMimeDataPrivate *source, *target;
    size_t i;
    if (!self || !other || self == other || !(source = other->m_data))
        return;
    if (XClassIsVtableNull(self)) XMimeData_init(self);
    target = self->m_data;
    if (!target)
        return;

    mime_clearPrivate(target);
    target->m_text  = source->m_text  ? XString_create_copy(source->m_text)  : NULL;
    target->m_html  = source->m_html  ? XString_create_copy(source->m_html)  : NULL;
    target->m_hasColor = source->m_hasColor;
    target->m_color    = source->m_color;
    if (source->m_image) {
        target->m_image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (target->m_image)
            XCopy(target->m_image, source->m_image);
    }
    if (source->m_custom) {
        size_t n = XVector_size_base((const XContainer*)source->m_custom);
        for (i = 0; i < n; ++i) {
            XMimeCustomEntry* srcEntry = (XMimeCustomEntry*)XVector_at_base(source->m_custom, (int64_t)i);
            XMimeCustomEntry* dstEntry;
            if (!srcEntry)
                continue;
            dstEntry = (XMimeCustomEntry*)XMalloc_System(sizeof(XMimeCustomEntry));
            if (!dstEntry)
                continue;
            dstEntry->m_format = srcEntry->m_format ? XString_create_copy(srcEntry->m_format) : NULL;
            dstEntry->m_data   = srcEntry->m_data   ? XString_create_copy(srcEntry->m_data)   : NULL;
            if (!target->m_custom)
                target->m_custom = XVector_Create(XMimeCustomEntry*);
            if (target->m_custom) {
                XVector_Push_Back_Base(target->m_custom, XMimeCustomEntry*, dstEntry);
            } else {
                if (dstEntry->m_format) XString_delete_base(dstEntry->m_format);
                if (dstEntry->m_data)   XString_delete_base(dstEntry->m_data);
                XFree_System(dstEntry);
            }
        }
    }
}

static void VXMimeData_move(XMimeData* self, XMimeData* other)
{
    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self)) XMimeData_init(self);
    if (self->m_data)
        mime_clearPrivate(self->m_data);
    if (self->m_data) {
        self->m_data->m_text  = other->m_data ? other->m_data->m_text  : NULL;
        self->m_data->m_html  = other->m_data ? other->m_data->m_html  : NULL;
        self->m_data->m_color = other->m_data ? other->m_data->m_color : (XColor){0};
        self->m_data->m_hasColor = other->m_data ? other->m_data->m_hasColor : false;
        self->m_data->m_image = other->m_data ? other->m_data->m_image : NULL;
        self->m_data->m_custom = other->m_data ? other->m_data->m_custom : NULL;
    }
    if (other->m_data) {
        other->m_data->m_text = NULL;
        other->m_data->m_html = NULL;
        other->m_data->m_image = NULL;
        other->m_data->m_custom = NULL;
        other->m_data->m_hasColor = false;
    }
}

XVtable* XMimeData_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMimeData)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMimeData_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy,   VXMimeData_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move,   VXMimeData_move);
    return XVTABLE_DEFAULT;
}

void XMimeData_init(XMimeData* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XMimeData));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XMimeData);
    self->m_data = (XMimeDataPrivate*)XMalloc_System(sizeof(XMimeDataPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XMimeDataPrivate));
    XColor_init_rgb(&self->m_data->m_color, 0, 0, 0, 0);
    self->m_data->m_hasColor = false;
}

XMimeData* XMimeData_create_ex(XMemoryType memory)
{
    XMimeData* self = (XMimeData*)XMemory_malloc(sizeof(XMimeData), memory);
    if (!self) return NULL;
    XMimeData_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XMimeData_clear(XMimeData* self)
{
    if (!self || !self->m_data) return;
    mime_clearPrivate(self->m_data);
}

/** @brief 大小写不敏感的 ASCII 字符串比较（MIME 类型名规则）。 */
static int mime_ascii_icmp(const char* a, const char* b)
{
    unsigned char ca, cb;
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    while (*a && *b) {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return (int)ca - (int)cb;
        ++a; ++b;
    }
    ca = (unsigned char)*a; cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
    return (int)ca - (int)cb;
}

/** @brief 精确匹配自定义格式表，返回条目索引；未找到返回 -1。 */
static int64_t mime_findCustom(const XMimeData* self, const char* format)
{
    size_t i, n;
    if (!self || !self->m_data || !self->m_data->m_custom || !format)
        return -1;
    n = XVector_size_base((const XContainer*)self->m_data->m_custom);
    for (i = 0; i < n; ++i) {
        XMimeCustomEntry* entry = (XMimeCustomEntry*)XVector_at_base(self->m_data->m_custom, (int64_t)i);
        if (!entry || !entry->m_format)
            continue;
        if (mime_ascii_icmp(XString_toUtf8(entry->m_format), format) == 0)
            return (int64_t)i;
    }
    return -1;
}

bool XMimeData_hasFormat(const XMimeData* self, const char* mimeType)
{
    if (!mimeType)
        return false;
    if (!self || !self->m_data)
        return false;
    if (mime_ascii_icmp(mimeType, "text/plain") == 0)
        return self->m_data->m_text != NULL;
    if (mime_ascii_icmp(mimeType, "text/html") == 0)
        return self->m_data->m_html != NULL;
    if (mime_ascii_icmp(mimeType, "application/x-color") == 0)
        return self->m_data->m_hasColor;
    if (mime_ascii_icmp(mimeType, "application/x-qt-image") == 0)
        return self->m_data->m_image != NULL;
    return mime_findCustom(self, mimeType) >= 0;
}

XStringList* XMimeData_formats(const XMimeData* self)
{
    XStringList* list;
    size_t i, n;
    list = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!list)
        return NULL;
    if (!self || !self->m_data)
        return list;

    if (self->m_data->m_text)
        XStringList_push_back_utf8(list, "text/plain");
    if (self->m_data->m_html)
        XStringList_push_back_utf8(list, "text/html");
    if (self->m_data->m_hasColor)
        XStringList_push_back_utf8(list, "application/x-color");
    if (self->m_data->m_image)
        XStringList_push_back_utf8(list, "application/x-qt-image");

    n = self->m_data->m_custom
            ? XVector_size_base((const XContainer*)self->m_data->m_custom) : 0;
    for (i = 0; i < n; ++i) {
        XMimeCustomEntry* entry = (XMimeCustomEntry*)XVector_at_base(self->m_data->m_custom, (int64_t)i);
        if (entry && entry->m_format)
            XStringList_push_back_base(list, entry->m_format);
    }
    return list;
}

bool XMimeData_hasText(const XMimeData* self)
{
    return self && self->m_data && self->m_data->m_text != NULL;
}

XString* XMimeData_text(const XMimeData* self)
{
    if (!self || !self->m_data || !self->m_data->m_text)
        return NULL;
    return XString_create_copy(self->m_data->m_text);
}

void XMimeData_setText(XMimeData* self, const XString* text)
{
    if (!self || !self->m_data)
        return;
    if (self->m_data->m_text) XString_delete_base(self->m_data->m_text);
    self->m_data->m_text = text ? XString_create_copy(text) : XString_create_utf8("");
}

bool XMimeData_hasHtml(const XMimeData* self)
{
    return self && self->m_data && self->m_data->m_html != NULL;
}

XString* XMimeData_html(const XMimeData* self)
{
    if (!self || !self->m_data || !self->m_data->m_html)
        return NULL;
    return XString_create_copy(self->m_data->m_html);
}

void XMimeData_setHtml(XMimeData* self, const XString* html)
{
    if (!self || !self->m_data)
        return;
    if (self->m_data->m_html) XString_delete_base(self->m_data->m_html);
    self->m_data->m_html = html ? XString_create_copy(html) : XString_create_utf8("");
}

bool XMimeData_hasColor(const XMimeData* self)
{
    return self && self->m_data && self->m_data->m_hasColor;
}

XColor XMimeData_colorData(const XMimeData* self)
{
    XColor invalid = XColor_create();
    if (!self || !self->m_data || !self->m_data->m_hasColor)
        return invalid;
    return self->m_data->m_color;
}

void XMimeData_setColorData(XMimeData* self, XColor color)
{
    if (!self || !self->m_data)
        return;
    self->m_data->m_color = color;
    self->m_data->m_hasColor = true;
}

bool XMimeData_hasImage(const XMimeData* self)
{
    return self && self->m_data && self->m_data->m_image != NULL;
}

XImage* XMimeData_imageData(const XMimeData* self)
{
    XImage* image;
    if (!self || !self->m_data || !self->m_data->m_image)
        return NULL;
    image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!image)
        return NULL;
    XCopy(image, self->m_data->m_image);
    return image;
}

void XMimeData_setImageData(XMimeData* self, const XImage* image)
{
    XImage* copy = NULL;
    if (!self || !self->m_data)
        return;
    if (image) {
        copy = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (copy)
            XCopy(copy, image);
    }
    if (self->m_data->m_image)
        XImage_delete_base((XClass*)self->m_data->m_image);
    self->m_data->m_image = copy;
}

void XMimeData_setData(XMimeData* self, const char* format, const XString* data)
{
    XMimeCustomEntry* entry;
    int64_t index;
    XString* fmt;

    if (!self || !self->m_data || !format)
        return;

    /* 内置格式路由。 */
    if (mime_ascii_icmp(format, "text/plain") == 0) {
        XMimeData_setText(self, data);
        return;
    }
    if (mime_ascii_icmp(format, "text/html") == 0) {
        XMimeData_setHtml(self, data);
        return;
    }

    /* 自定义格式：覆盖已有同名条目。 */
    index = mime_findCustom(self, format);
    if (index >= 0) {
        entry = (XMimeCustomEntry*)XVector_at_base(self->m_data->m_custom, index);
        if (!entry)
            return;
        if (entry->m_data) XString_delete_base(entry->m_data);
        entry->m_data = data ? XString_create_copy(data) : XString_create_utf8("");
        return;
    }

    entry = (XMimeCustomEntry*)XMalloc_System(sizeof(XMimeCustomEntry));
    if (!entry)
        return;
    fmt = XString_create_utf8(format);
    entry->m_format = fmt;
    entry->m_data   = data ? XString_create_copy(data) : XString_create_utf8("");
    if (!self->m_data->m_custom)
        self->m_data->m_custom = XVector_Create(XMimeCustomEntry*);
    if (!self->m_data->m_custom) {
        if (entry->m_format) XString_delete_base(entry->m_format);
        if (entry->m_data)   XString_delete_base(entry->m_data);
        XFree_System(entry);
        return;
    }
    XVector_Push_Back_Base(self->m_data->m_custom, XMimeCustomEntry*, entry);
}

XString* XMimeData_data(const XMimeData* self, const char* format)
{
    int64_t index;
    XMimeCustomEntry* entry;
    if (!format)
        return NULL;
    if (!self || !self->m_data)
        return NULL;
    if (mime_ascii_icmp(format, "text/plain") == 0)
        return XMimeData_text(self);
    if (mime_ascii_icmp(format, "text/html") == 0)
        return XMimeData_html(self);
    index = mime_findCustom(self, format);
    if (index < 0)
        return NULL;
    entry = (XMimeCustomEntry*)XVector_at_base(self->m_data->m_custom, index);
    if (!entry || !entry->m_data)
        return NULL;
    return XString_create_copy(entry->m_data);
}

#endif /* XMIMEDATA_ON */
