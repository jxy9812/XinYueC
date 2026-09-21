/******************************************************************************
 * @file       XMimeData.c
 * @brief      XMimeData 剪贴板 MIME 数据容器实现（对标 Qt 6.8 QMimeData）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XMimeData.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XMIMEDATA_ON

/** @brief 自定义格式登记项：格式名 + 原始字节数据。 */
typedef struct XMimeCustomEntry
{
    XString*     m_format; /**< 格式名（UTF-8），由条目拥有。 */
    XByteArray*  m_data;   /**< 原始字节数据（对标 QMimeData 的 QByteArray
                              载荷：逐字节透明保存，不做 UTF-8 转换，
                              PNG 魔数 0x89 等二进制写入读回精确），
                              由条目拥有。 */
} XMimeCustomEntry;

/** @brief XMimeData 私有数据块。 */
struct XMimeDataPrivate
{
    XString*       m_text;       /**< 纯文本（text/plain）。 */
    XString*       m_html;       /**< HTML（text/html）。 */
    XColor         m_color;      /**< 颜色数据（application/x-color）。 */
    bool           m_hasColor;   /**< 是否已登记颜色。 */
    XImage*        m_image;      /**< 图像（application/x-qt-image）。 */
    XStringList*   m_urls;       /**< URL 列表（text/uri-list 行；拥有）。 */
    XVector*       m_custom;     /**< XMimeCustomEntry* 列表，自定义格式。 */
};

/**
 * @brief      取自定义条目表中第 index 条。
 * @details    根因说明：m_custom 是存放 XMimeCustomEntry* 指针的 XVector，
 *             XVector_at_base 返回的是"元素槽位地址"（XMimeCustomEntry**），
 *             不是元素值。此前各处直接把槽位地址强转成 XMimeCustomEntry*，
 *             读 entry->m_format 实际读到槽内第一个字（条目指针本身被当成
 *             XString*），读 entry->m_data 则越过槽位读到相邻内存——格式名
 *             存储后即"损坏"，hasFormat/formats 随即 SEGV。此处统一解引用
 *             槽位取出真正的条目指针；越界时 XVector_at_base 返回 NULL。
 */
static XMimeCustomEntry* mime_customAt(XVector* custom, int64_t index)
{
    XMimeCustomEntry** slot;
    if (!custom || index < 0)
        return NULL;
    slot = (XMimeCustomEntry**)XVector_at_base(custom, index);
    return slot ? *slot : NULL;
}

/** @brief 删除私有数据块中的全部资源并把指针槽位置空。 */
static void mime_clearPrivate(XMimeDataPrivate* d)
{
    size_t i;
    if (!d)
        return;
    if (d->m_text)  { XString_delete_base(d->m_text);  d->m_text  = NULL; }
    if (d->m_html)  { XString_delete_base(d->m_html);  d->m_html  = NULL; }
    if (d->m_image) { XImage_delete_base((XClass*)d->m_image); d->m_image = NULL; }
    if (d->m_urls)  { XStringList_delete_base(d->m_urls); d->m_urls = NULL; }
    d->m_hasColor = false;
    XColor_init_rgb(&d->m_color, 0, 0, 0, 0);
    if (d->m_custom) {
        for (i = 0; i < XVector_size_base((const XContainer*)d->m_custom); ++i) {
            XMimeCustomEntry* entry = mime_customAt(d->m_custom, (int64_t)i);
            if (entry) {
                if (entry->m_format) XString_delete_base(entry->m_format);
                if (entry->m_data)   XByteArray_delete_base((XClass*)entry->m_data);
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
    if (source->m_urls)
        target->m_urls = XStringList_create_copy(source->m_urls);
    if (source->m_custom) {
        size_t n = XVector_size_base((const XContainer*)source->m_custom);
        for (i = 0; i < n; ++i) {
            XMimeCustomEntry* srcEntry = mime_customAt(source->m_custom, (int64_t)i);
            XMimeCustomEntry* dstEntry;
            if (!srcEntry)
                continue;
            dstEntry = (XMimeCustomEntry*)XMalloc_System(sizeof(XMimeCustomEntry));
            if (!dstEntry)
                continue;
            dstEntry->m_format = srcEntry->m_format ? XString_create_copy(srcEntry->m_format) : NULL;
            dstEntry->m_data   = srcEntry->m_data   ? XByteArray_create_copy(srcEntry->m_data) : NULL;
            if (!target->m_custom)
                target->m_custom = XVector_Create(XMimeCustomEntry*);
            if (target->m_custom) {
                XVector_Push_Back_Base(target->m_custom, XMimeCustomEntry*, dstEntry);
            } else {
                if (dstEntry->m_format) XString_delete_base(dstEntry->m_format);
                if (dstEntry->m_data)   XByteArray_delete_base((XClass*)dstEntry->m_data);
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
        /* m_urls 同属条目型存储：移动时必须一并转移所有权，否则目标丢失
         * urls、源却仍持有并会在自身清理时释放（所有权单边悬空）。 */
        self->m_data->m_urls  = other->m_data ? other->m_data->m_urls  : NULL;
        self->m_data->m_custom = other->m_data ? other->m_data->m_custom : NULL;
    }
    if (other->m_data) {
        other->m_data->m_text = NULL;
        other->m_data->m_html = NULL;
        other->m_data->m_image = NULL;
        other->m_data->m_urls = NULL;
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
    XMemset(self, 0, sizeof(XMimeData));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XMimeData);
    self->m_data = (XMimeDataPrivate*)XMalloc_System(sizeof(XMimeDataPrivate));
    if (!self->m_data) return;
    XMemset(self->m_data, 0, sizeof(XMimeDataPrivate));
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
        XMimeCustomEntry* entry = mime_customAt(self->m_data->m_custom, (int64_t)i);
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

/** @brief 判断 MIME 类型名是否为 application/x-qt* 框架内部类型。
 *  @details 对标 Qt：application/x-qt-image 等以 "application/x-qt" 为前缀
 *           的类型是框架内部交换通道（批次二十二模块决策），不对外呈现。
 */
static bool mime_isInternalQtType(const char* mimeType)
{
    static const char kPrefix[] = "application/x-qt";
    size_t i;
    if (!mimeType)
        return false;
    /* 前缀比较大小写不敏感（与 hasFormat 同一规则）。 */
    for (i = 0; i < sizeof(kPrefix) - 1; ++i) {
        char c = mimeType[i];
        if (!c)
            return false; /* 短于前缀：非内部类型。 */
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != kPrefix[i])
            return false;
    }
    return true;
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

    /* 对标 QMimeData::formats：剔除全部 application/x-qt* 内部类型
     * （批次二十二模块决策；Qt 平台剪贴板集成层同样不把 x-qt* 呈现给
     * formats() 调用方）。内部类型仍可经 hasFormat/data 命中——Qt 的
     * hasFormat 走存储检查，application/x-qt-image 查询不依赖 formats
     * 列表，本实现 hasFormat 对内置格式的直接存储分支即该语义。
     * 框架内部消费方（如 XClipboard 的图像派生推送）一律走
     * XMimeData_hasImage 等内部查询，不经 formats() 往返。 */
    if (self->m_data->m_text)
        XStringList_push_back_utf8(list, "text/plain");
    if (self->m_data->m_html)
        XStringList_push_back_utf8(list, "text/html");
    if (self->m_data->m_hasColor)
        XStringList_push_back_utf8(list, "application/x-color");
    /* application/x-qt-image（内置图像内部类型）不列入。 */

    n = self->m_data->m_custom
            ? XVector_size_base((const XContainer*)self->m_data->m_custom) : 0;
    for (i = 0; i < n; ++i) {
        XMimeCustomEntry* entry = mime_customAt(self->m_data->m_custom, (int64_t)i);
        if (entry && entry->m_format &&
            !mime_isInternalQtType(XString_toUtf8(entry->m_format)))
            XStringList_push_back_base(list, entry->m_format);
    }
    return list;
}

bool XMimeData_hasUrls(const XMimeData* self)
{
    return self && self->m_data && self->m_data->m_urls &&
           XStringList_size_base((const XStringList*)self->m_data->m_urls) > 0;
}

XStringList* XMimeData_urls(const XMimeData* self)
{
    XStringList* list = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self || !self->m_data || !self->m_data->m_urls || !list)
        return list;
    {
        int64_t i;
        int64_t n = XStringList_size_base(
            (const XStringList*)self->m_data->m_urls);
        for (i = 0; i < n; ++i) {
            const XString* s = XStringList_at_base(
                (const XStringList*)self->m_data->m_urls, i);
            if (s)
                XStringList_push_back_base(list, (XString*)s);
        }
    }
    return list;
}

void XMimeData_setUrls(XMimeData* self, const XStringList* urls)
{
    XMimeDataPrivate* d;
    if (!self || !(d = self->m_data)) return;
    if (d->m_urls)
        XStringList_delete_base(d->m_urls);
    d->m_urls = urls ? XStringList_create_copy(urls) : NULL;
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

void XMimeData_setData_bytes(XMimeData* self, const char* format,
                             const unsigned char* data, int len)
{
    XMimeCustomEntry* entry;
    int64_t index;
    XString* fmt;
    XByteArray* blob;

    if (!self || !self->m_data || !format)
        return;
    if (len < 0)
        len = 0;

    /* 内置格式路由（对标 Qt：setData("text/plain", ba) 即写入文本载荷，
     * text()/data() 同源；保持既有 XString 语义零回归）。 */
    if (mime_ascii_icmp(format, "text/plain") == 0 ||
        mime_ascii_icmp(format, "text/html") == 0) {
        /* 字节 → 文本（文本格式本就是 UTF-8 字节流，转换无损）。 */
        XString* text = (len > 0)
            ? XString_create_with_length_utf8((const char*)data, (size_t)len)
            : XString_create_utf8("");
        if (!text)
            return;
        if (mime_ascii_icmp(format, "text/plain") == 0)
            XMimeData_setText(self, text);
        else
            XMimeData_setHtml(self, text);
        XString_delete_base(text);
        return;
    }

    /* 字节载荷深拷贝进 XByteArray（二进制透明，无 UTF-8 转换）。 */
    blob = XByteArray_create_with_data((const char*)data, (size_t)len);
    if (!blob)
        return;

    /* 自定义格式：覆盖已有同名条目（条目槽位由 mime_customAt 解引用）。 */
    index = mime_findCustom(self, format);
    if (index >= 0) {
        entry = mime_customAt(self->m_data->m_custom, index);
        if (!entry) {
            XByteArray_delete_base((XClass*)blob);
            return;
        }
        if (entry->m_data) XByteArray_delete_base((XClass*)entry->m_data);
        entry->m_data = blob;
        return;
    }

    entry = (XMimeCustomEntry*)XMalloc_System(sizeof(XMimeCustomEntry));
    if (!entry) {
        XByteArray_delete_base((XClass*)blob);
        return;
    }
    fmt = XString_create_utf8(format);
    entry->m_format = fmt;
    entry->m_data   = blob;
    if (!self->m_data->m_custom)
        self->m_data->m_custom = XVector_Create(XMimeCustomEntry*);
    if (!self->m_data->m_custom) {
        if (entry->m_format) XString_delete_base(entry->m_format);
        if (entry->m_data)   XByteArray_delete_base((XClass*)entry->m_data);
        XFree_System(entry);
        return;
    }
    XVector_Push_Back_Base(self->m_data->m_custom, XMimeCustomEntry*, entry);
}

void XMimeData_setData(XMimeData* self, const char* format, const XString* data)
{
    /* 文本入参统一取其 UTF-8 字节走二进制透明通道（text 载荷本就是
     * UTF-8 字节流，转换等价于旧版的 XString 深拷贝语义）。 */
    XMimeData_setData_bytes(self, format,
        data ? (const unsigned char*)XString_toUtf8(data) : NULL,
        data ? (int)XString_toUtf8_length(data) : 0);
}

XByteArray* XMimeData_data_bytes(const XMimeData* self, const char* format)
{
    int64_t index;
    XMimeCustomEntry* entry;
    if (!format)
        return NULL;
    if (!self || !self->m_data)
        return NULL;
    /* 内置文本格式：返回对应文本的 UTF-8 字节（与 data()/text()/html()
     * 同源，对标 Qt data("text/plain") 返回文本字节）。 */
    if (mime_ascii_icmp(format, "text/plain") == 0) {
        XString* text = XMimeData_text(self);
        XByteArray* bytes;
        if (!text)
            return NULL;
        bytes = XByteArray_create_with_data(XString_toUtf8(text),
                                            XString_toUtf8_length(text));
        XString_delete_base(text);
        return bytes;
    }
    if (mime_ascii_icmp(format, "text/html") == 0) {
        XString* html = XMimeData_html(self);
        XByteArray* bytes;
        if (!html)
            return NULL;
        bytes = XByteArray_create_with_data(XString_toUtf8(html),
                                            XString_toUtf8_length(html));
        XString_delete_base(html);
        return bytes;
    }
    /* 自定义格式：XByteArray 载荷深拷贝，逐字节精确（含 0x00/0x89 等）。 */
    index = mime_findCustom(self, format);
    if (index < 0)
        return NULL;
    entry = mime_customAt(self->m_data->m_custom, index);
    if (!entry || !entry->m_data)
        return NULL;
    return XByteArray_create_copy(entry->m_data);
}

XString* XMimeData_data(const XMimeData* self, const char* format)
{
    int64_t index;
    XMimeCustomEntry* entry;
    XByteArray* bytes;
    XString* out;
    size_t len;
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
    entry = mime_customAt(self->m_data->m_custom, index);
    if (!entry || !entry->m_data)
        return NULL;
    /* XString 通道面向文本格式：载荷字节重解码为 XString（合法 UTF-8
     * 内容与旧版深拷贝语义一致）；二进制格式须走 XMimeData_data_bytes。 */
    len = XByteArray_size_base((const XContainer*)entry->m_data);
    if (len == 0)
        return XString_create_utf8("");
    bytes = entry->m_data;
    out = XString_create_with_length_utf8(
        (const char*)XByteArray_constData(bytes), len);
    return out;
}

bool XMimeData_removeFormat(XMimeData* self, const char* format)
{
    bool removed = false;
    if (!self || !self->m_data || !format)
        return false;

    /* 对标 QMimeData::removeFormat：删除一种格式；text/plain 与 text/html
     * 是 setText/setHtml 登记的内置格式，删除时同步清空对应存储，后续
     * text()/html() 即返回 NULL（与 Qt 删除映射项后 data() 为空一致）。 */
    if (mime_ascii_icmp(format, "text/plain") == 0) {
        if (self->m_data->m_text) {
            XString_delete_base(self->m_data->m_text);
            self->m_data->m_text = NULL;
            removed = true;
        }
    } else if (mime_ascii_icmp(format, "text/html") == 0) {
        if (self->m_data->m_html) {
            XString_delete_base(self->m_data->m_html);
            self->m_data->m_html = NULL;
            removed = true;
        }
    } else if (mime_ascii_icmp(format, "application/x-color") == 0) {
        if (self->m_data->m_hasColor) {
            self->m_data->m_hasColor = false;
            XColor_init_rgb(&self->m_data->m_color, 0, 0, 0, 0);
            removed = true;
        }
    } else if (mime_ascii_icmp(format, "application/x-qt-image") == 0) {
        if (self->m_data->m_image) {
            XImage_delete_base((XClass*)self->m_data->m_image);
            self->m_data->m_image = NULL;
            removed = true;
        }
    } else {
        /* setData 登记的自定义格式：整条移除并释放条目内存。 */
        int64_t index = mime_findCustom(self, format);
        if (index >= 0) {
            XMimeCustomEntry* entry = mime_customAt(self->m_data->m_custom, index);
            if (entry) {
                if (entry->m_format) XString_delete_base(entry->m_format);
                if (entry->m_data)   XByteArray_delete_base((XClass*)entry->m_data);
                XFree_System(entry);
            }
            XVector_remove_base(self->m_data->m_custom, index, 1);
            removed = true;
        }
    }
    return removed;
}

#endif /* XMIMEDATA_ON */
