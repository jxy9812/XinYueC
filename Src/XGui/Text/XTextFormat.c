/**
 * @file       XTextFormat.c
 * @brief      XTextFormat 文本格式属性池值对象实现（对标 Qt 6.8
 *             QTextFormat 的「format_type + 属性键值表」核心）。
 * @details    属性表 = XVector（useCow=false）线性存储
 *             XTextFormatProperty{key, XVariant value}；容器注册元素
 *             深拷贝/移动/析构回调（XContainerSetDataCopyMethod 三件），
 *             整容器深拷贝、remove、clear 全部自动按回调逐条深拷贝/释放。
 *             语义取证见头注释（qtextformat.cpp 行号以 v6.8.3 为准）。
 * @author     XinYueC 团队
 */
#if XGUI_ON

#include "XTextFormat.h"
#include "XMemory.h"

#include <string.h>

#if XTEXTDOCUMENT_ON

/* ==================== 内部工具 ==================== */

/** @brief Qt::LayoutDirectionAuto（intProperty 的特例缺省，
 *         qtextformat.cpp:1062）。 */
#define XTF_LAYOUT_DIRECTION_AUTO 3
/** @brief QFont::Normal 字重（fontWeight 的「属性未设」缺省，
 *         qtextformat.h:451）。 */
#define XTF_FONT_WEIGHT_NORMAL 400

/** @brief 属性表有效判定（init 过的 XVector 才可操作）。 */
static bool xtf_propsValid(const XTextFormat* self)
{
    return self && XClassGetVtable((XClass*)&self->m_props) ==
                       XVector_class_init();
}

/** @brief 属性条数（XVector→XContainer 跨结构体强转统一在此）。 */
static int xtf_propCount(const XTextFormat* self)
{
    if (!xtf_propsValid(self)) return 0;
    return (int)XVector_size_base((const XContainer*)&self->m_props);
}

/** @brief 按键线性查条目（Qt QTextFormatPrivate::propertyIndex 的线性
 *         扫描同构，qtextformat.cpp:184-191）。 */
static XTextFormatProperty* xtf_findSlot(XTextFormat* self, int key)
{
    int i;
    int n;
    if (!xtf_propsValid(self)) return NULL;
    n = xtf_propCount(self);
    for (i = 0; i < n; ++i) {
        XTextFormatProperty* p =
            (XTextFormatProperty*)XVector_at_base(&self->m_props, i);
        if (p && p->key == key) return p;
    }
    return NULL;
}

static const XTextFormatProperty* xtf_findSlotConst(const XTextFormat* self,
                                                    int key)
{
    return xtf_findSlot((XTextFormat*)self, key);
}

/** @brief 属性值相等（类型 + 载荷；XVariant_compare 对「类型不同」返回
 *         0 会被误判相等，故类型先行自比——见 XVariant.c:698-704）。 */
static bool xtf_valueEqual(const XVariant* a, const XVariant* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->m_type != b->m_type || a->m_dataSize != b->m_dataSize) return false;
    if (a->m_data == NULL || b->m_data == NULL)
        return a->m_data == b->m_data;
    return XVariant_compare((XVariant*)a, (XVariant*)b) == 0;
}

/* ---- 容器元素回调：深拷贝 / 移动 / 析构（XVector 按 typeSize 裸字节
 *     管理元素，含堆载荷的条目必须挂这三件，否则整容器 copy 共享载荷、
 *     clear/remove 漏释放） ---- */

static void xtf_propCopyCb(void* data, const void* sData)
{
    XTextFormatProperty* d;
    const XTextFormatProperty* s;
    if (!data || !sData || data == sData) return;
    d = (XTextFormatProperty*)data;
    s = (const XTextFormatProperty*)sData;
    d->key = s->key;
    /* 新槽可能是未初始化字节：先清零（虚表 NULL）再深拷贝——
     * VXVariant_copy 的「目标虚表空」分支据此走 init+深拷贝。 */
    memset(&d->value, 0, sizeof(XVariant));
    XClassCopy(&d->value, &s->value);
}

static void xtf_propMoveCb(void* data, const void* sData)
{
    XTextFormatProperty* d;
    XTextFormatProperty* s;
    if (!data || !sData || data == sData) return;
    d = (XTextFormatProperty*)data;
    s = (XTextFormatProperty*)sData;
    memset(&d->value, 0, sizeof(XVariant));
    d->key = s->key;
    d->value = s->value;                     /* 结构浅转：载荷所有权随之转移 */
    memset(&s->value, 0, sizeof(XVariant));  /* 源置空：不再持所有权 */
}

static void xtf_propDeinitCb(void* data)
{
    XTextFormatProperty* p;
    if (!data) return;
    p = (XTextFormatProperty*)data;
    XClassDeinit(&p->value); /* 多态分发 VXVariant_deinit；载荷 NULL 幂等返回 */
}

/** @brief 标量载体构造（XVariant_init 正路：虚表有效 + 堆载荷深拷贝；
 *         裸 memset 载体虚表为 NULL，XClassCopy 按 src 虚表分发会静默
 *         不拷——2026-11-16 实锅，勿改回）。调用方消费后须
 *         XClassDeinit 释放 init 的载荷。 */
static void xtf_makeScalar(XVariant* v, int type, const void* data,
                           size_t size)
{
    XVariant_init(v, (void*)data, size, type);
}

/** @brief String 值直接落槽（槽内新建 XString 深拷贝 UTF-8；前置
 *         XVariant_deinit 幂等清旧载荷——String 旧载荷内嵌 XString
 *         对象，直接 free 会漏内部缓冲）。 */
static void xtf_storeString(XVariant* slotVar, const char* utf8)
{
    XString* s;
    /* 空虚表（未初始化载体/栈裸变量）没有可分发的析构槽，XClassDeinit
     * 会刷 ArgIsNULL 错误日志——直接跳过（等价其「对 NULL 虚表返回」
     * 语义），不留诊断噪声。 */
    if (XClassGetVtable((XClass*)slotVar) != NULL)
        XClassDeinit((XClass*)slotVar); /* 幂等清旧载荷（含 String 内嵌 XString 深释放） */
    memset(slotVar, 0, sizeof(*slotVar));
    /* 手动构造的载体补类虚表：后续作为 XClassCopy 的 src / XClassDeinit
     * 的目标都必须经虚表分发（XVariant.c:96 同款手法） */
    XClassGetVtable(slotVar) = XVariant_class_init();
    slotVar->m_type = XVariantType_String;
    slotVar->m_dataSize = sizeof(XString);
    slotVar->m_data = XMalloc_System(sizeof(XString));
    if (!slotVar->m_data) {
        slotVar->m_type = XVariantType_NULL;
        slotVar->m_dataSize = 0;
        return;
    }
    memset(slotVar->m_data, 0, sizeof(XString));
    s = (XString*)slotVar->m_data;
    XString_init(s);
    if (utf8 && utf8[0]) XString_assign_utf8(s, utf8);
}

/* ==================== 生命周期 ==================== */

void XTextFormat_init(XTextFormat* self, int type)
{
    if (!self) return;
    if (xtf_propsValid(self))
        XTextFormat_deinit(self);   /* 重复 init：先释放旧属性表（防泄漏契约） */
    else
        memset(self, 0, sizeof(*self)); /* 未清零栈实例防呆（对齐 XVariant_init） */
    self->m_type = type;
    /* useCow=false：元素含堆载荷，COW 浅共享必然踩双重释放。 */
    XVector_init(&self->m_props, sizeof(XTextFormatProperty), false);
    XContainerSetDataCopyMethod(&self->m_props, xtf_propCopyCb);
    XContainerSetDataMoveMethod(&self->m_props, xtf_propMoveCb);
    XContainerSetDataDeinitMethod(&self->m_props, xtf_propDeinitCb);
}

void XTextFormat_deinit(XTextFormat* self)
{
    int i;
    int n;
    if (!self) return;
    if (!xtf_propsValid(self)) {
        memset(self, 0, sizeof(*self));
        return;
    }
    /* 手动逐条析构（容器回调同款动作的显式一次）——即便容器 remove/clear
     * 不走回调也账目平（回调幂等）。 */
    n = xtf_propCount(self);
    for (i = 0; i < n; ++i) {
        XTextFormatProperty* p =
            (XTextFormatProperty*)XVector_at_base(&self->m_props, i);
        if (p) XClassDeinit((XClass*)&p->value);
    }
    XContainer_clear_base((XContainer*)&self->m_props); /* 元素回调逐条析构 + 清计数（幂等） */
    XClassDelete(&self->m_props);       /* 内嵌成员：仅反初始化，不 free 成员 */
    memset(self, 0, sizeof(*self));
    self->m_type = XTextFormatType_InvalidFormat; /* 复位空壳 = 无效格式 */
}

void XTextFormat_copy(XTextFormat* dst, const XTextFormat* src)
{
    if (!dst || dst == src) return;
    XTextFormat_deinit(dst);
    if (!src) return;
    dst->m_type = src->m_type;
    if (!xtf_propsValid(src)) return;
    if (xtf_propCount(src) == 0) return;
    /* XVector 多态深拷贝（VXClass_copy 非 COW 分支）：目标先释放旧资源，
     * 逐元素经注册的 DataCopyMethod 深拷贝（XVector.c:223-244）。 */
    XClassCopy(&dst->m_props, (XClass*)&src->m_props);
}

void XTextFormat_move(XTextFormat* dst, XTextFormat* src)
{
    if (!dst || !src || dst == src) return;
    XTextFormat_deinit(dst);
    if (!xtf_propsValid(src)) {
        memset(src, 0, sizeof(*src));
        return;
    }
    dst->m_type = src->m_type;
    /* XVector 多态移动：堆缓冲转移，源置空（XVector.c VXClass_move）。 */
    XClassMove(&dst->m_props, (XClass*)&src->m_props);
    src->m_type = XTextFormatType_InvalidFormat;
    /* 源重建立空属性表：保持「合法空格式」状态（虚表/回调齐备）。 */
    XVector_init(&src->m_props, sizeof(XTextFormatProperty), false);
    XContainerSetDataCopyMethod(&src->m_props, xtf_propCopyCb);
    XContainerSetDataMoveMethod(&src->m_props, xtf_propMoveCb);
    XContainerSetDataDeinitMethod(&src->m_props, xtf_propDeinitCb);
}

XTextFormat XTextFormat_make(int type)
{
    XTextFormat f;
    memset(&f, 0, sizeof(f));
    XTextFormat_init(&f, type);
    return f;
}

/* ==================== 类型与状态 ==================== */

int XTextFormat_type(const XTextFormat* self)
{
    return self ? self->m_type : XTextFormatType_InvalidFormat;
}

bool XTextFormat_isValid(const XTextFormat* self)
{
    return self && self->m_type != XTextFormatType_InvalidFormat;
}

bool XTextFormat_isCharFormat(const XTextFormat* self)
{
    return self && self->m_type == XTextFormatType_CharFormat;
}

bool XTextFormat_isBlockFormat(const XTextFormat* self)
{
    return self && self->m_type == XTextFormatType_BlockFormat;
}

bool XTextFormat_isListFormat(const XTextFormat* self)
{
    return self && self->m_type == XTextFormatType_ListFormat;
}

bool XTextFormat_isFrameFormat(const XTextFormat* self)
{
    return self && self->m_type == XTextFormatType_FrameFormat;
}

bool XTextFormat_isImageFormat(const XTextFormat* self)
{
    /* 对标 qtextformat.h:330：type==CharFormat && objectType()==ImageObject。 */
    return self && self->m_type == XTextFormatType_CharFormat &&
           XTextFormat_intProperty(self, XTextFormatProperty_ObjectType) == 1;
}

bool XTextFormat_isEmpty(const XTextFormat* self)
{
    return XTextFormat_propertyCount(self) == 0;
}

int XTextFormat_propertyCount(const XTextFormat* self)
{
    return xtf_propCount(self);
}

/* ==================== 属性存取 ==================== */

bool XTextFormat_hasProperty(const XTextFormat* self, int propertyId)
{
    return xtf_findSlotConst(self, propertyId) != NULL;
}

const XVariant* XTextFormat_property(const XTextFormat* self, int propertyId)
{
    const XTextFormatProperty* p = xtf_findSlotConst(self, propertyId);
    return p ? (const XVariant*)&p->value : NULL;
}

void XTextFormat_setProperty_value(XTextFormat* self, int propertyId,
                                   const XVariant* value)
{
    XTextFormatProperty* slot;
    XTextFormatProperty tmp;
    XVariant nullValue;
    if (!self) return;
    if (!xtf_propsValid(self)) return;
    slot = xtf_findSlot(self, propertyId);
    if (slot) {
        /* 同键覆盖（Qt insertProperty 的覆盖分支，qtextformat.cpp:172-176）；
         * XClassCopy→VXVariant_copy：类型切换先析构旧值再深拷贝（安全）。 */
        if (value) {
            XClassCopy(&slot->value, (const XClass*)value);
        } else {
            /* 无效值照存（Qt 无条件存 QVariant() 同口径）；载体走 init
             * 正路保证虚表有效（XClassCopy 按 src 虚表分发） */
            XVariant_init(&nullValue, NULL, 0, XVariantType_NULL);
            XClassCopy(&slot->value, (const XClass*)&nullValue);
            XClassDeinit((XClass*)&nullValue);
        }
        return;
    }
    /* 新键追加：临时条目 push（容器回调深拷贝进新槽），临时载荷即收。 */
    memset(&tmp, 0, sizeof(tmp));
    tmp.key = propertyId;
    if (value) {
        memset(&tmp.value, 0, sizeof(tmp.value));
        XClassCopy(&tmp.value, (const XClass*)value);
    } else {
        XVariant_init(&tmp.value, NULL, 0, XVariantType_NULL);
    }
    XVector_push_back_1_base(&self->m_props, &tmp);
    XClassDeinit((XClass*)&tmp.value); /* 临时载体载荷回收（槽内为深拷贝） */
}

void XTextFormat_setProperty_int(XTextFormat* self, int propertyId, int value)
{
    XVariant v;
    xtf_makeScalar(&v, XVariantType_Int, &value, sizeof(int));
    XTextFormat_setProperty_value(self, propertyId, &v);
    XClassDeinit((XClass*)&v); /* makeScalar 的 init 载荷回收（槽内为深拷贝） */
}

void XTextFormat_setProperty_double(XTextFormat* self, int propertyId,
                                    double value)
{
    XVariant v;
    xtf_makeScalar(&v, XVariantType_Double, &value, sizeof(double));
    XTextFormat_setProperty_value(self, propertyId, &v);
    XClassDeinit((XClass*)&v); /* makeScalar 的 init 载荷回收（槽内为深拷贝） */
}

void XTextFormat_setProperty_bool(XTextFormat* self, int propertyId,
                                  bool value)
{
    XVariant v;
    xtf_makeScalar(&v, XVariantType_Bool, &value, sizeof(bool));
    XTextFormat_setProperty_value(self, propertyId, &v);
    XClassDeinit((XClass*)&v); /* makeScalar 的 init 载荷回收（槽内为深拷贝） */
}

void XTextFormat_setProperty_string(XTextFormat* self, int propertyId,
                                    const char* utf8)
{
    XTextFormatProperty* slot;
    if (!self) return;
    if (!xtf_propsValid(self)) return;
    slot = xtf_findSlot(self, propertyId);
    if (slot) {
        xtf_storeString(&slot->value, utf8);
        return;
    }
    {
        XVariant v;
        memset(&v, 0, sizeof(v));
        xtf_storeString(&v, utf8);
        /* 手动构造的载体补类虚表（XClassCopy 按 src 虚表分发——与
         * XVariant.c:96 对 init 后对象设虚表同款手法） */
        XClassGetVtable(&v) = XVariant_class_init();
        XTextFormat_setProperty_value(self, propertyId, &v);
        XClassDeinit((XClass*)&v); /* 临时载体载荷回收（槽内为深拷贝） */
    }
}

void XTextFormat_setProperty_color(XTextFormat* self, int propertyId,
                                   uint32_t argb)
{
    XVariant v;
    xtf_makeScalar(&v, XVariantType_Uint32, &argb, sizeof(uint32_t));
    XTextFormat_setProperty_value(self, propertyId, &v);
    XClassDeinit((XClass*)&v); /* makeScalar 的 init 载荷回收（槽内为深拷贝） */
}

void XTextFormat_clearProperty(XTextFormat* self, int propertyId)
{
    int i;
    int n;
    if (!xtf_propsValid(self)) return;
    n = xtf_propCount(self);
    for (i = 0; i < n; ++i) {
        XTextFormatProperty* p =
            (XTextFormatProperty*)XVector_at_base(&self->m_props, i);
        if (p && p->key == propertyId) {
            XClassDeinit((XClass*)&p->value); /* 先收载荷（幂等双保险） */
            XVector_remove_base(&self->m_props, i, 1); /* 容器回调再析构一次仍安全 */
            return;
        }
    }
    /* 键不存在：不动（Qt clearProperty 同口径，qtextformat.cpp:186-192） */
}

int XTextFormat_intProperty(const XTextFormat* self, int propertyId)
{
    const XVariant* v;
    int def = 0;
    /* LayoutDirection 特例：缺省 LayoutDirectionAuto（qtextformat.cpp:1062） */
    if (propertyId == XTextFormatProperty_LayoutDirection)
        def = XTF_LAYOUT_DIRECTION_AUTO;
    v = XTextFormat_property(self, propertyId);
    if (!v) return def;
    if (v->m_type != XVariantType_Int) return def;
    if (!v->m_data || v->m_dataSize != sizeof(int)) return def;
    return *((const int*)v->m_data);
}

double XTextFormat_doubleProperty(const XTextFormat* self, int propertyId)
{
    const XVariant* v;
    v = XTextFormat_property(self, propertyId);
    if (!v) return 0.0;
    /* Qt 接受 Double 与 Float（qtextformat.cpp:1088-1089） */
    if (v->m_type != XVariantType_Double && v->m_type != XVariantType_Float)
        return 0.0;
    if (!v->m_data) return 0.0;
    if (v->m_type == XVariantType_Float)
        return (double)*((const float*)v->m_data);
    if (v->m_dataSize != sizeof(double)) return 0.0;
    return *((const double*)v->m_data);
}

bool XTextFormat_boolProperty(const XTextFormat* self, int propertyId)
{
    const XVariant* v;
    v = XTextFormat_property(self, propertyId);
    if (!v) return false;
    if (v->m_type != XVariantType_Bool) return false;
    if (!v->m_data || v->m_dataSize != sizeof(bool)) return false;
    return *((const bool*)v->m_data);
}

const char* XTextFormat_stringProperty(const XTextFormat* self, int propertyId)
{
    const XVariant* v;
    v = XTextFormat_property(self, propertyId);
    if (!v) return "";
    if (v->m_type != XVariantType_String) return "";
    if (!v->m_data) return "";
    {
        const char* utf8 = XString_toUtf8((const XString*)v->m_data);
        return utf8 ? utf8 : "";
    }
}

uint32_t XTextFormat_colorProperty(const XTextFormat* self, int propertyId)
{
    const XVariant* v;
    v = XTextFormat_property(self, propertyId);
    if (!v) return 0u;
    if (v->m_type != XVariantType_Uint32) return 0u;
    if (!v->m_data || v->m_dataSize != sizeof(uint32_t)) return 0u;
    return *((const uint32_t*)v->m_data);
}

/* ==================== merge / equal ==================== */

void XTextFormat_merge(XTextFormat* self, const XTextFormat* other)
{
    int i;
    int n;
    if (!self || !other) return;
    if (self->m_type != other->m_type) return; /* Qt：type 不同直接返回 */
    if (!xtf_propsValid(other)) return;
    if (XTextFormat_propertyCount(self) == 0) {
        /* Qt：self 无属性表 → d = other.d（共享）；值语义下等价全量接收 */
        XTextFormat_copy(self, other);
        return;
    }
    if (xtf_propCount(other) == 0) return;
    n = xtf_propCount(other);
    for (i = 0; i < n; ++i) {
        const XTextFormatProperty* p = (const XTextFormatProperty*)
            XVector_at_base((const XVector*)&other->m_props, i);
        if (!p) continue;
        /* Qt merge（qtextformat.cpp:958-963）：值有效 → 覆盖；无效 → 清键 */
        if (p->value.m_type != XVariantType_NULL)
            XTextFormat_setProperty_value(self, p->key, &p->value);
        else
            XTextFormat_clearProperty(self, p->key);
    }
}

bool XTextFormat_equal(const XTextFormat* a, const XTextFormat* b)
{
    int na;
    int nb;
    int i;
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->m_type != b->m_type) return false;
    na = XTextFormat_propertyCount(a);
    nb = XTextFormat_propertyCount(b);
    /* Qt operator==：双方属性表全空等价（d 空 vs 空表不区分，:1368-1373） */
    if (na != nb) return false;
    for (i = 0; i < na; ++i) {
        const XTextFormatProperty* pa;
        const XTextFormatProperty* pb;
        pa = (const XTextFormatProperty*)XVector_at_base(&a->m_props, i);
        pb = (const XTextFormatProperty*)XVector_at_base(&b->m_props, i);
        if (!pa || !pb) return false;
        if (pa->key != pb->key) return false;
        if (!xtf_valueEqual(&pa->value, &pb->value)) return false;
    }
    return true;
}

/* ==================== CharFormat ==================== */

void XTextCharFormat_init(XTextCharFormat* self)
{
    XTextFormat_init(self, XTextFormatType_CharFormat);
}

XTextCharFormat XTextCharFormat_make(void)
{
    return XTextFormat_make(XTextFormatType_CharFormat);
}

void XTextCharFormat_convert(XTextCharFormat* dst, const XTextFormat* src)
{
    if (!dst) return;
    XTextFormat_copy(dst, src);
    if (dst) dst->m_type = XTextFormatType_CharFormat;
}

void XTextCharFormat_setFontFamily(XTextCharFormat* self, const char* family)
{
    /* Qt 6.8（qtextformat.h:417-419）：setProperty(FontFamilies, ...) */
    XTextFormat_setProperty_string(self, XTextFormatProperty_FontFamilies,
                                   family);
}

const char* XTextCharFormat_fontFamily(const XTextCharFormat* self)
{
    const XVariant* v;
    if (!self) return NULL;
    /* Qt（qtextformat.h:419-420）：property(FontFamilies) 的首族；属性
     * 缺失时 Qt 是空表 first() 的未定义行为，本类明确回落 NULL。 */
    v = XTextFormat_property(self, XTextFormatProperty_FontFamilies);
    if (!v) return NULL;
    if (v->m_type == XVariantType_String && v->m_data) {
        const char* utf8 = XString_toUtf8((const XString*)v->m_data);
        return utf8 ? utf8 : NULL;
    }
    return NULL;
}

void XTextCharFormat_setFontPointSize(XTextCharFormat* self, double size)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_FontPointSize,
                                   size);
}

double XTextCharFormat_fontPointSize(const XTextCharFormat* self)
{
    return XTextFormat_doubleProperty(self, XTextFormatProperty_FontPointSize);
}

void XTextCharFormat_setFontWeight(XTextCharFormat* self, int weight)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_FontWeight, weight);
}

int XTextCharFormat_fontWeight(const XTextCharFormat* self)
{
    /* qtextformat.h:451：hasProperty ? intProperty : QFont::Normal */
    if (!XTextFormat_hasProperty(self, XTextFormatProperty_FontWeight))
        return XTF_FONT_WEIGHT_NORMAL;
    return XTextFormat_intProperty(self, XTextFormatProperty_FontWeight);
}

void XTextCharFormat_setFontItalic(XTextCharFormat* self, bool italic)
{
    XTextFormat_setProperty_bool(self, XTextFormatProperty_FontItalic, italic);
}

bool XTextCharFormat_fontItalic(const XTextCharFormat* self)
{
    return XTextFormat_boolProperty(self, XTextFormatProperty_FontItalic);
}

void XTextCharFormat_setFontUnderline(XTextCharFormat* self, bool underline)
{
    /* Qt 6.8（qtextformat.h:473-474）：写 TextUnderlineStyle，非 FontUnderline */
    XTextFormat_setProperty_int(
        self, XTextFormatProperty_TextUnderlineStyle,
        underline ? XTextFormatUnderline_SingleUnderline
                  : XTextFormatUnderline_NoUnderline);
}

bool XTextCharFormat_fontUnderline(const XTextCharFormat* self)
{
    /* qtextformat.cpp:1638-1643：TextUnderlineStyle 优先，兼容键回落 */
    if (XTextFormat_hasProperty(self, XTextFormatProperty_TextUnderlineStyle))
        return XTextCharFormat_underlineStyle(self) ==
               XTextFormatUnderline_SingleUnderline;
    return XTextFormat_boolProperty(self, XTextFormatProperty_FontUnderline);
}

void XTextCharFormat_setUnderlineStyle(XTextCharFormat* self, int style)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_TextUnderlineStyle,
                                style);
}

int XTextCharFormat_underlineStyle(const XTextCharFormat* self)
{
    return XTextFormat_intProperty(self,
                                   XTextFormatProperty_TextUnderlineStyle);
}

void XTextCharFormat_setFontOverline(XTextCharFormat* self, bool overline)
{
    XTextFormat_setProperty_bool(self, XTextFormatProperty_FontOverline,
                                 overline);
}

bool XTextCharFormat_fontOverline(const XTextCharFormat* self)
{
    return XTextFormat_boolProperty(self, XTextFormatProperty_FontOverline);
}

void XTextCharFormat_setFontStrikeOut(XTextCharFormat* self, bool strikeOut)
{
    XTextFormat_setProperty_bool(self, XTextFormatProperty_FontStrikeOut,
                                 strikeOut);
}

bool XTextCharFormat_fontStrikeOut(const XTextCharFormat* self)
{
    return XTextFormat_boolProperty(self, XTextFormatProperty_FontStrikeOut);
}

void XTextCharFormat_setFontFixedPitch(XTextCharFormat* self, bool fixedPitch)
{
    XTextFormat_setProperty_bool(self, XTextFormatProperty_FontFixedPitch,
                                 fixedPitch);
}

bool XTextCharFormat_fontFixedPitch(const XTextCharFormat* self)
{
    return XTextFormat_boolProperty(self, XTextFormatProperty_FontFixedPitch);
}

void XTextCharFormat_setFontPixelSize(XTextCharFormat* self, int pixels)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_FontPixelSize,
                                pixels);
}

int XTextCharFormat_fontPixelSize(const XTextCharFormat* self)
{
    return XTextFormat_intProperty(self, XTextFormatProperty_FontPixelSize);
}

void XTextCharFormat_setVerticalAlignment(XTextCharFormat* self, int alignment)
{
    XTextFormat_setProperty_int(self,
                                XTextFormatProperty_TextVerticalAlignment,
                                alignment);
}

int XTextCharFormat_verticalAlignment(const XTextCharFormat* self)
{
    return XTextFormat_intProperty(
        self, XTextFormatProperty_TextVerticalAlignment);
}

void XTextCharFormat_setSuperScriptBaseline(XTextCharFormat* self,
                                            double baseline)
{
    XTextFormat_setProperty_double(
        self, XTextFormatProperty_TextSuperScriptBaseline, baseline);
}

double XTextCharFormat_superScriptBaseline(const XTextCharFormat* self)
{
    /* qtextformat.h:548：未设 50.0 */
    if (!XTextFormat_hasProperty(self,
                                 XTextFormatProperty_TextSuperScriptBaseline))
        return 50.0;
    return XTextFormat_doubleProperty(
        self, XTextFormatProperty_TextSuperScriptBaseline);
}

void XTextCharFormat_setSubScriptBaseline(XTextCharFormat* self,
                                          double baseline)
{
    XTextFormat_setProperty_double(
        self, XTextFormatProperty_TextSubScriptBaseline, baseline);
}

double XTextCharFormat_subScriptBaseline(const XTextCharFormat* self)
{
    /* qtextformat.h:553：未设 100.0/6.0 */
    if (!XTextFormat_hasProperty(self,
                                 XTextFormatProperty_TextSubScriptBaseline))
        return 100.0 / 6.0;
    return XTextFormat_doubleProperty(
        self, XTextFormatProperty_TextSubScriptBaseline);
}

void XTextCharFormat_setBaselineOffset(XTextCharFormat* self, double offset)
{
    XTextFormat_setProperty_double(self,
                                   XTextFormatProperty_TextBaselineOffset,
                                   offset);
}

double XTextCharFormat_baselineOffset(const XTextCharFormat* self)
{
    /* qtextformat.h:558：未设 0.0 */
    if (!XTextFormat_hasProperty(self,
                                 XTextFormatProperty_TextBaselineOffset))
        return 0.0;
    return XTextFormat_doubleProperty(
        self, XTextFormatProperty_TextBaselineOffset);
}

void XTextCharFormat_setAnchor(XTextCharFormat* self, bool anchor)
{
    XTextFormat_setProperty_bool(self, XTextFormatProperty_IsAnchor, anchor);
}

bool XTextCharFormat_isAnchor(const XTextCharFormat* self)
{
    return XTextFormat_boolProperty(self, XTextFormatProperty_IsAnchor);
}

void XTextCharFormat_setAnchorHref(XTextCharFormat* self, const char* value)
{
    XTextFormat_setProperty_string(self, XTextFormatProperty_AnchorHref,
                                   value);
}

const char* XTextCharFormat_anchorHref(const XTextCharFormat* self)
{
    return XTextFormat_stringProperty(self, XTextFormatProperty_AnchorHref);
}

void XTextCharFormat_setForeground(XTextCharFormat* self, uint32_t argb)
{
    XTextFormat_setProperty_color(self, XTextFormatProperty_ForegroundBrush,
                                  argb);
}

uint32_t XTextCharFormat_foreground(const XTextCharFormat* self)
{
    return XTextFormat_colorProperty(self, XTextFormatProperty_ForegroundBrush);
}

void XTextCharFormat_clearForeground(XTextCharFormat* self)
{
    XTextFormat_clearProperty(self, XTextFormatProperty_ForegroundBrush);
}

void XTextCharFormat_setBackground(XTextCharFormat* self, uint32_t argb)
{
    XTextFormat_setProperty_color(self, XTextFormatProperty_BackgroundBrush,
                                  argb);
}

uint32_t XTextCharFormat_background(const XTextCharFormat* self)
{
    return XTextFormat_colorProperty(self, XTextFormatProperty_BackgroundBrush);
}

void XTextCharFormat_clearBackground(XTextCharFormat* self)
{
    XTextFormat_clearProperty(self, XTextFormatProperty_BackgroundBrush);
}

void XTextCharFormat_setUnderlineColor(XTextCharFormat* self, uint32_t argb)
{
    XTextFormat_setProperty_color(self, XTextFormatProperty_TextUnderlineColor,
                                  argb);
}

uint32_t XTextCharFormat_underlineColor(const XTextCharFormat* self)
{
    return XTextFormat_colorProperty(self,
                                     XTextFormatProperty_TextUnderlineColor);
}

/* ==================== BlockFormat ==================== */

void XTextBlockFormat_init(XTextBlockFormat* self)
{
    /* QTextBlockFormat()（qtextformat.cpp:2221）：零缺省属性 */
    XTextFormat_init(self, XTextFormatType_BlockFormat);
}

XTextBlockFormat XTextBlockFormat_make(void)
{
    return XTextFormat_make(XTextFormatType_BlockFormat);
}

void XTextBlockFormat_convert(XTextBlockFormat* dst, const XTextFormat* src)
{
    if (!dst) return;
    XTextFormat_copy(dst, src);
    if (dst) dst->m_type = XTextFormatType_BlockFormat;
}

void XTextBlockFormat_setAlignment(XTextBlockFormat* self, int alignment)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_BlockAlignment,
                                alignment);
}

int XTextBlockFormat_alignment(const XTextBlockFormat* self)
{
    int a = XTextFormat_intProperty(self, XTextFormatProperty_BlockAlignment);
    /* qtextformat.h:622-624：0 回落 AlignLeft（=0x01，XTDAlignment 同值） */
    if (a == 0) a = 0x01;
    return a;
}

void XTextBlockFormat_setTopMargin(XTextBlockFormat* self, double margin)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_BlockTopMargin,
                                   margin);
}

double XTextBlockFormat_topMargin(const XTextBlockFormat* self)
{
    return XTextFormat_doubleProperty(self,
                                      XTextFormatProperty_BlockTopMargin);
}

void XTextBlockFormat_setBottomMargin(XTextBlockFormat* self, double margin)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_BlockBottomMargin,
                                   margin);
}

double XTextBlockFormat_bottomMargin(const XTextBlockFormat* self)
{
    return XTextFormat_doubleProperty(self,
                                      XTextFormatProperty_BlockBottomMargin);
}

void XTextBlockFormat_setLeftMargin(XTextBlockFormat* self, double margin)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_BlockLeftMargin,
                                   margin);
}

double XTextBlockFormat_leftMargin(const XTextBlockFormat* self)
{
    return XTextFormat_doubleProperty(self,
                                      XTextFormatProperty_BlockLeftMargin);
}

void XTextBlockFormat_setRightMargin(XTextBlockFormat* self, double margin)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_BlockRightMargin,
                                   margin);
}

double XTextBlockFormat_rightMargin(const XTextBlockFormat* self)
{
    return XTextFormat_doubleProperty(self,
                                      XTextFormatProperty_BlockRightMargin);
}

void XTextBlockFormat_setTextIndent(XTextBlockFormat* self, double indent)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_TextIndent,
                                   indent);
}

double XTextBlockFormat_textIndent(const XTextBlockFormat* self)
{
    return XTextFormat_doubleProperty(self, XTextFormatProperty_TextIndent);
}

void XTextBlockFormat_setIndent(XTextBlockFormat* self, int indent)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_BlockIndent, indent);
}

int XTextBlockFormat_indent(const XTextBlockFormat* self)
{
    return XTextFormat_intProperty(self, XTextFormatProperty_BlockIndent);
}

void XTextBlockFormat_setHeadingLevel(XTextBlockFormat* self, int level)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_HeadingLevel, level);
}

int XTextBlockFormat_headingLevel(const XTextBlockFormat* self)
{
    return XTextFormat_intProperty(self, XTextFormatProperty_HeadingLevel);
}

void XTextBlockFormat_setLineHeight(XTextBlockFormat* self, double height,
                                    int heightType)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_LineHeight,
                                   height);
    XTextFormat_setProperty_int(self, XTextFormatProperty_LineHeightType,
                                heightType);
}

double XTextBlockFormat_lineHeight(const XTextBlockFormat* self)
{
    return XTextFormat_doubleProperty(self, XTextFormatProperty_LineHeight);
}

int XTextBlockFormat_lineHeightType(const XTextBlockFormat* self)
{
    return XTextFormat_intProperty(self, XTextFormatProperty_LineHeightType);
}

/* ==================== ListFormat ==================== */

void XTextListFormat_init(XTextListFormat* self)
{
    /* QTextListFormat()（qtextformat.cpp:2644-2648）：indent=1、start=1 */
    XTextFormat_init(self, XTextFormatType_ListFormat);
    XTextFormat_setProperty_int(self, XTextFormatProperty_ListIndent, 1);
    XTextFormat_setProperty_int(self, XTextFormatProperty_ListStart, 1);
}

XTextListFormat XTextListFormat_make(void)
{
    XTextListFormat f;
    memset(&f, 0, sizeof(f));
    XTextListFormat_init(&f);
    return f;
}

void XTextListFormat_convert(XTextListFormat* dst, const XTextFormat* src)
{
    /* 对齐 QTextListFormat(const QTextFormat&)：直取原属性，不补构造缺省 */
    if (!dst) return;
    XTextFormat_copy(dst, src);
    if (dst) dst->m_type = XTextFormatType_ListFormat;
}

void XTextListFormat_setStyle(XTextListFormat* self, int style)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_ListStyle, style);
}

int XTextListFormat_style(const XTextListFormat* self)
{
    return XTextFormat_intProperty(self, XTextFormatProperty_ListStyle);
}

void XTextListFormat_setIndent(XTextListFormat* self, int indent)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_ListIndent, indent);
}

int XTextListFormat_indent(const XTextListFormat* self)
{
    return XTextFormat_intProperty(self, XTextFormatProperty_ListIndent);
}

void XTextListFormat_setNumberPrefix(XTextListFormat* self, const char* prefix)
{
    XTextFormat_setProperty_string(self, XTextFormatProperty_ListNumberPrefix,
                                   prefix);
}

const char* XTextListFormat_numberPrefix(const XTextListFormat* self)
{
    return XTextFormat_stringProperty(self,
                                      XTextFormatProperty_ListNumberPrefix);
}

void XTextListFormat_setNumberSuffix(XTextListFormat* self, const char* suffix)
{
    XTextFormat_setProperty_string(self, XTextFormatProperty_ListNumberSuffix,
                                   suffix);
}

const char* XTextListFormat_numberSuffix(const XTextListFormat* self)
{
    return XTextFormat_stringProperty(self,
                                      XTextFormatProperty_ListNumberSuffix);
}

void XTextListFormat_setStart(XTextListFormat* self, int start)
{
    XTextFormat_setProperty_int(self, XTextFormatProperty_ListStart, start);
}

int XTextListFormat_start(const XTextListFormat* self)
{
    return XTextFormat_intProperty(self, XTextFormatProperty_ListStart);
}

/* ==================== ImageFormat ==================== */

void XTextImageFormat_init(XTextImageFormat* self)
{
    /* QTextImageFormat()（qtextformat.cpp:3387）：CharFormat + ObjectType=
     * ImageObject(1) 落表（isImageFormat 的判定属性）。 */
    XTextFormat_init(self, XTextFormatType_CharFormat);
    XTextFormat_setProperty_int(self, XTextFormatProperty_ObjectType, 1);
}

XTextImageFormat XTextImageFormat_make(void)
{
    XTextImageFormat f;
    memset(&f, 0, sizeof(f));
    XTextImageFormat_init(&f);
    return f;
}

void XTextImageFormat_convert(XTextImageFormat* dst, const XTextFormat* src)
{
    if (!dst) return;
    XTextFormat_copy(dst, src);
    if (dst) {
        dst->m_type = XTextFormatType_CharFormat;
        XTextFormat_setProperty_int(dst, XTextFormatProperty_ObjectType, 1);
    }
}

void XTextImageFormat_setName(XTextImageFormat* self, const char* name)
{
    XTextFormat_setProperty_string(self, XTextFormatProperty_ImageName, name);
}

const char* XTextImageFormat_name(const XTextImageFormat* self)
{
    return XTextFormat_stringProperty(self, XTextFormatProperty_ImageName);
}

void XTextImageFormat_setWidth(XTextImageFormat* self, double width)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_ImageWidth,
                                   width);
}

double XTextImageFormat_width(const XTextImageFormat* self)
{
    return XTextFormat_doubleProperty(self, XTextFormatProperty_ImageWidth);
}

void XTextImageFormat_setHeight(XTextImageFormat* self, double height)
{
    XTextFormat_setProperty_double(self, XTextFormatProperty_ImageHeight,
                                   height);
}

double XTextImageFormat_height(const XTextImageFormat* self)
{
    return XTextFormat_doubleProperty(self, XTextFormatProperty_ImageHeight);
}

#endif /* XTEXTDOCUMENT_ON */
#endif /* XGUI_ON */
