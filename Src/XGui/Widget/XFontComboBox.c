/**
 * @file       XFontComboBox.c
 * @brief      字体选择下拉框控件实现（对标 Qt 6.8 QFontComboBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XFontComboBox.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#if XPLATFORMFONTDATABASE_ON
#include "XPlatformFontDatabase.h"
#endif

#if XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON

/* 样例文本表使用的内存池类型：与本类堆创建默认类型一致（分配与释放
 * 使用同一常量保证配对）。 */
#define XFCB_TABLE_MEMORY XCLASS_DEFAULT_MEMORY_TYPE

static void xfcb_fontSampleFree(XFontComboBox* self);
static void xfcb_systemSampleFree(XFontComboBox* self);

/* ==================== 样例文本表（族名/书写系统 → 样例文本） ==================== */

/* 样例文本表首配容量（之后倍增摊还）。 */
#define XFCB_SAMPLE_MIN_CAPACITY 4

/** @brief 族名样例表条目（键=UTF-8 族名；值=样例文本；两者均对象拥有）。 */
typedef struct xfcb_FontSample
{
    char* m_family; /**< 族名键（对象拥有）。 */
    char* m_sample; /**< 样例文本（对象拥有；可为 NULL=键已占位）。 */
} xfcb_FontSample;

/** @brief 书写系统样例表条目（键=书写系统枚举值）。 */
typedef struct xfcb_SystemSample
{
    int m_system;   /**< 书写系统枚举值（键）。 */
    char* m_sample; /**< 样例文本（对象拥有）。 */
} xfcb_SystemSample;

/** @brief 以 XMemory 复制 UTF-8 字符串（含结尾 NUL）；失败返回 NULL。 */
static char* xfcb_textClone(const char* text)
{
    size_t len;
    char* copy;
    if (!text) return NULL;
    len = XStrlen(text);
    copy = (char*)XMemory_malloc(len + 1, XFCB_TABLE_MEMORY);
    if (!copy) return NULL;
    XMemcpy(copy, text, len + 1);
    return copy;
}

/** @brief 查询族名自定义样例文本；未设置返回 NULL。 */
static const char* xfcb_fontSampleGet(const XFontComboBox* self,
                                      const char* family)
{
    const xfcb_FontSample* table =
        (const xfcb_FontSample*)self->m_fontSamples;
    int i;
    for (i = 0; i < self->m_fontSampleCount; ++i) {
        if (table[i].m_family && XStrcmp(table[i].m_family, family) == 0)
            return table[i].m_sample;
    }
    return NULL;
}

/**
 * @brief 设置/移除族名样例文本（sample=NULL 移除条目，回退默认行为；
 *        已有同键条目时替换文本；容量不足时倍增扩容）。
 */
static void xfcb_fontSampleSet(XFontComboBox* self, const char* family,
                               const char* sample)
{
    xfcb_FontSample* table = (xfcb_FontSample*)self->m_fontSamples;
    int target = -1;
    int i;
    if (!family || family[0] == '\0') return;
    for (i = 0; i < self->m_fontSampleCount; ++i) {
        if (table[i].m_family && XStrcmp(table[i].m_family, family) == 0) {
            target = i;
            break;
        }
    }
    if (!sample) {
        /* NULL 样例 = 移除该族自定义（交换尾条目保持表紧凑）。 */
        if (target < 0) return;
        XMemory_free(table[target].m_family, XFCB_TABLE_MEMORY);
        if (table[target].m_sample)
            XMemory_free(table[target].m_sample, XFCB_TABLE_MEMORY);
        table[target] = table[self->m_fontSampleCount - 1];
        --self->m_fontSampleCount;
        return;
    }
    if (target < 0) {
        if (self->m_fontSampleCount >= self->m_fontSampleCap) {
            int newCap = self->m_fontSampleCap > 0
                ? self->m_fontSampleCap * 2
                : XFCB_SAMPLE_MIN_CAPACITY;
            xfcb_FontSample* fresh = (xfcb_FontSample*)XMemory_calloc(
                (size_t)newCap, sizeof(xfcb_FontSample), XFCB_TABLE_MEMORY);
            if (!fresh) return;
            if (table) {
                XMemcpy(fresh, table,
                        (size_t)self->m_fontSampleCount *
                            sizeof(xfcb_FontSample));
                XMemory_free(table, XFCB_TABLE_MEMORY);
            }
            self->m_fontSamples = fresh;
            self->m_fontSampleCap = newCap;
            table = fresh;
        }
        target = self->m_fontSampleCount;
        table[target].m_family = xfcb_textClone(family);
        table[target].m_sample = NULL;
        if (!table[target].m_family) return; /* 键复制失败则不入表。 */
        ++self->m_fontSampleCount;
    }
    {
        char* copy = xfcb_textClone(sample);
        if (!copy) return; /* 分配失败保留旧文本。 */
        if (table[target].m_sample)
            XMemory_free(table[target].m_sample, XFCB_TABLE_MEMORY);
        table[target].m_sample = copy;
    }
}

/** @brief 释放族名样例表全部条目并归零容量（析构/拷贝/移动同步入口）。 */
static void xfcb_fontSampleFree(XFontComboBox* self)
{
    xfcb_FontSample* table = (xfcb_FontSample*)self->m_fontSamples;
    int i;
    for (i = 0; i < self->m_fontSampleCount; ++i) {
        if (table[i].m_family)
            XMemory_free(table[i].m_family, XFCB_TABLE_MEMORY);
        if (table[i].m_sample)
            XMemory_free(table[i].m_sample, XFCB_TABLE_MEMORY);
    }
    if (table) XMemory_free(table, XFCB_TABLE_MEMORY);
    self->m_fontSamples = NULL;
    self->m_fontSampleCount = 0;
    self->m_fontSampleCap = 0;
}

/** @brief 查询书写系统自定义样例文本；未设置返回 NULL。 */
static const char* xfcb_systemSampleGet(const XFontComboBox* self, int system)
{
    const xfcb_SystemSample* table =
        (const xfcb_SystemSample*)self->m_systemSamples;
    int i;
    for (i = 0; i < self->m_systemSampleCount; ++i) {
        if (table[i].m_system == system) return table[i].m_sample;
    }
    return NULL;
}

/**
 * @brief 设置/移除书写系统样例文本（语义同 xfcb_fontSampleSet；键为
 *        枚举值，容量不足时倍增扩容）。
 */
static void xfcb_systemSampleSet(XFontComboBox* self, int system,
                                 const char* sample)
{
    xfcb_SystemSample* table = (xfcb_SystemSample*)self->m_systemSamples;
    int target = -1;
    int i;
    for (i = 0; i < self->m_systemSampleCount; ++i) {
        if (table[i].m_system == system) {
            target = i;
            break;
        }
    }
    if (!sample) {
        /* NULL 样例 = 移除该系统自定义（交换尾条目保持表紧凑）。 */
        if (target < 0) return;
        if (table[target].m_sample)
            XMemory_free(table[target].m_sample, XFCB_TABLE_MEMORY);
        table[target] = table[self->m_systemSampleCount - 1];
        --self->m_systemSampleCount;
        return;
    }
    if (target < 0) {
        if (self->m_systemSampleCount >= self->m_systemSampleCap) {
            int newCap = self->m_systemSampleCap > 0
                ? self->m_systemSampleCap * 2
                : XFCB_SAMPLE_MIN_CAPACITY;
            xfcb_SystemSample* fresh = (xfcb_SystemSample*)XMemory_calloc(
                (size_t)newCap, sizeof(xfcb_SystemSample),
                XFCB_TABLE_MEMORY);
            if (!fresh) return;
            if (table) {
                XMemcpy(fresh, table,
                        (size_t)self->m_systemSampleCount *
                            sizeof(xfcb_SystemSample));
                XMemory_free(table, XFCB_TABLE_MEMORY);
            }
            self->m_systemSamples = fresh;
            self->m_systemSampleCap = newCap;
            table = fresh;
        }
        target = self->m_systemSampleCount;
        table[target].m_system = system;
        table[target].m_sample = NULL;
        ++self->m_systemSampleCount;
    }
    {
        char* copy = xfcb_textClone(sample);
        if (!copy) return; /* 分配失败保留旧文本。 */
        if (table[target].m_sample)
            XMemory_free(table[target].m_sample, XFCB_TABLE_MEMORY);
        table[target].m_sample = copy;
    }
}

/** @brief 释放书写系统样例表全部条目并归零容量（析构/拷贝/移动同步入口）。 */
static void xfcb_systemSampleFree(XFontComboBox* self)
{
    xfcb_SystemSample* table = (xfcb_SystemSample*)self->m_systemSamples;
    int i;
    for (i = 0; i < self->m_systemSampleCount; ++i) {
        if (table[i].m_sample)
            XMemory_free(table[i].m_sample, XFCB_TABLE_MEMORY);
    }
    if (table) XMemory_free(table, XFCB_TABLE_MEMORY);
    self->m_systemSamples = NULL;
    self->m_systemSampleCount = 0;
    self->m_systemSampleCap = 0;
}

static void xfcb_populate(XFontComboBox* self)
{
#if XPLATFORMFONTDATABASE_ON
    XPlatformFontDatabase* db;
    XVector* families = NULL;
    int64_t i;
    int64_t n;
    if (!self) return;
    db = XPlatformFontDatabase_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!db) return;
    if (XPlatformFontDatabase_isValid(db))
        families = XPlatformFontDatabase_families(db);
    if (families) {
        n = XVector_size_base((const XContainer*)families);
        for (i = 0; i < n; ++i) {
            XString** name = (XString**)XVector_at_base(families, i);
            if (name && *name) {
                XComboBox_addItem(self, *name);
                XString_delete_base(*name);
                *name = NULL;
            }
        }
        XVector_delete_base(families);
        families = NULL;
    }
    XPlatformFontDatabase_destroy(db);
#endif /* XPLATFORMFONTDATABASE_ON */
    /* 回退：数据库未启用或无字体族时补默认条目确保控件非空。 */
    if (XComboBox_count(self) > 0)
        XComboBox_setCurrentIndex(self, 0);
    if (XComboBox_count(self) == 0) {
        XComboBox_addItem_2(self, "XFontOutlineCommon");
        XComboBox_addItem_2(self, "Sans Serif");
        XComboBox_addItem_2(self, "Serif");
        XComboBox_addItem_2(self, "Monospace");
        XComboBox_setCurrentIndex(self, 0);
    }
}

/* ==================== 生命周期虚函数（copy/move/deinit 同步派生字段） ==================== */

/** @brief 深拷贝：基类拷贝后同步字体过滤、书写系统、显示字体与样例文本字段。 */
static void VXFontComboBox_copy(XFontComboBox* self,
                                const XFontComboBox* other)
{
    const xfcb_FontSample* fontSamples;
    const xfcb_SystemSample* systemSamples;
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XFontComboBox_init(self, NULL, 0);
    XClass_Parent(XComboBox, EXClass_Copy,
                  void (*)(XComboBox*, const XComboBox*))(
        (XComboBox*)self, (const XComboBox*)other);
    self->m_filters = other->m_filters;
    self->m_writingSystem = other->m_writingSystem;
    /* 显示字体值承载：经 XFont 多态拷贝深复制族名/样式字符串。 */
    XCopy(&self->m_displayFont, &other->m_displayFont);
    /* 样例文本表深拷贝（键与文本均复制所有权）。 */
    xfcb_fontSampleFree(self);
    xfcb_systemSampleFree(self);
    fontSamples = (const xfcb_FontSample*)other->m_fontSamples;
    for (i = 0; i < other->m_fontSampleCount; ++i) {
        if (fontSamples[i].m_family)
            xfcb_fontSampleSet(self, fontSamples[i].m_family,
                               fontSamples[i].m_sample);
    }
    systemSamples = (const xfcb_SystemSample*)other->m_systemSamples;
    for (i = 0; i < other->m_systemSampleCount; ++i) {
        xfcb_systemSampleSet(self, systemSamples[i].m_system,
                             systemSamples[i].m_sample);
    }
}

/** @brief 移动语义：基类移动后转移状态字段与样例表所有权，源对象归默认值。 */
static void VXFontComboBox_move(XFontComboBox* self, XFontComboBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XFontComboBox_init(self, NULL, 0);
    XClass_Parent(XComboBox, EXClass_Move,
                  void (*)(XComboBox*, XComboBox*))(
        (XComboBox*)self, (XComboBox*)other);
    self->m_filters = other->m_filters;
    self->m_writingSystem = other->m_writingSystem;
    /* 显示字体：转移族名/样式字符串所有权（源由 XFont_move 归默认值）。 */
    XMove(&self->m_displayFont, &other->m_displayFont);
    /* 样例文本表：整体转移所有权。 */
    xfcb_fontSampleFree(self);
    self->m_fontSamples = other->m_fontSamples;
    self->m_fontSampleCount = other->m_fontSampleCount;
    self->m_fontSampleCap = other->m_fontSampleCap;
    other->m_fontSamples = NULL;
    other->m_fontSampleCount = 0;
    other->m_fontSampleCap = 0;
    xfcb_systemSampleFree(self);
    self->m_systemSamples = other->m_systemSamples;
    self->m_systemSampleCount = other->m_systemSampleCount;
    self->m_systemSampleCap = other->m_systemSampleCap;
    other->m_systemSamples = NULL;
    other->m_systemSampleCount = 0;
    other->m_systemSampleCap = 0;
    other->m_filters = (int)XFontComboBoxFilter_AllFonts;
    other->m_writingSystem = (int)XFontComboBoxWritingSystem_Any;
}

/** @brief 析构：释放样例文本表与显示字体资源，复位派生状态字段后
 *         释放基类资源。 */
static void VXFontComboBox_deinit(XFontComboBox* self)
{
    if (!self) return;
    xfcb_fontSampleFree(self);
    xfcb_systemSampleFree(self);
    XClass_deinit_base((XClass*)&self->m_displayFont);
    self->m_filters = (int)XFontComboBoxFilter_AllFonts;
    self->m_writingSystem = (int)XFontComboBoxWritingSystem_Any;
    XClass_Deinit_Parent(XComboBox, (XComboBox*)self);
}

XVtable* XFontComboBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFontComboBox)
    XVTABLE_INHERIT_XCLASS(XComboBox);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXFontComboBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXFontComboBox_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFontComboBox_deinit);
    return XVTABLE_DEFAULT;
}

void XFontComboBox_init(XFontComboBox* self, XWidget* parent,
                        XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XComboBox_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XFontComboBox);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_filters = (int)XFontComboBoxFilter_AllFonts;
    self->m_writingSystem = (int)XFontComboBoxWritingSystem_Any;
    XFont_init(&self->m_displayFont);
    self->m_fontSamples = NULL;
    self->m_fontSampleCount = 0;
    self->m_fontSampleCap = 0;
    self->m_systemSamples = NULL;
    self->m_systemSampleCount = 0;
    self->m_systemSampleCap = 0;
    xfcb_populate(self);
}

XFontComboBox* XFontComboBox_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XFontComboBox* self =
        (XFontComboBox*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XFontComboBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XFontComboBox_setFontFilters(XFontComboBox* self, int filters)
{
    if (!self || self->m_filters == filters) return;
    self->m_filters = filters;
    /* 其余过滤项依赖字体元数据（可缩放/等宽），待字体数据库能力
     * 补齐后重算条目；当前仅存储。 */
}

int XFontComboBox_fontFilters(const XFontComboBox* self)
{
    return self ? self->m_filters : 0;
}

/** @brief 校验书写系统是否属于 XFontComboBoxWritingSystem 简化子集
 *         （数值对齐 Qt::WritingSystem 对应项）。 */
static bool xfcb_isValidWritingSystem(int system)
{
    switch (system)
    {
    case (int)XFontComboBoxWritingSystem_Any:
    case (int)XFontComboBoxWritingSystem_Latin:
    case (int)XFontComboBoxWritingSystem_Greek:
    case (int)XFontComboBoxWritingSystem_Cyrillic:
    case (int)XFontComboBoxWritingSystem_Armenian:
    case (int)XFontComboBoxWritingSystem_Hebrew:
    case (int)XFontComboBoxWritingSystem_Arabic:
    case (int)XFontComboBoxWritingSystem_Devanagari:
    case (int)XFontComboBoxWritingSystem_Bengali:
    case (int)XFontComboBoxWritingSystem_Tamil:
    case (int)XFontComboBoxWritingSystem_Thai:
    case (int)XFontComboBoxWritingSystem_SimplifiedChinese:
    case (int)XFontComboBoxWritingSystem_TraditionalChinese:
    case (int)XFontComboBoxWritingSystem_Japanese:
    case (int)XFontComboBoxWritingSystem_Korean:
    case (int)XFontComboBoxWritingSystem_Vietnamese:
    case (int)XFontComboBoxWritingSystem_Symbol:
        return true;
    default:
        return false;
    }
}

void XFontComboBox_setWritingSystem(XFontComboBox* self, int writingSystem)
{
    if (!self || !xfcb_isValidWritingSystem(writingSystem)) return;
    if (self->m_writingSystem == writingSystem) return;
    self->m_writingSystem = writingSystem;
    /* 对标 Qt：书写系统筛选会重填字体族列表；当前字体数据库元数据
     * 未支持书写系统标注，仅保存状态（头文件 @details 已注明）。 */
}

int XFontComboBox_writingSystem(const XFontComboBox* self)
{
    return self ? self->m_writingSystem
                : (int)XFontComboBoxWritingSystem_Any;
}

/* ==================== 显示字体与样例文本（状态承载） ==================== */

void XFontComboBox_setDisplayFont(XFontComboBox* self, const XFont* font)
{
    XFont temp;
    if (!self || !font) return;
    /* 同 XWidget_setFont 的值承载方案：临时字体深拷贝后移入状态字段
     * （XFont_move 转移族名/样式字符串所有权并归默认源）。 */
    XFont_init(&temp);
    XCopy(&temp, font);
    XMove(&self->m_displayFont, &temp);
    /* 状态承载：字形预览/委托绘制未建，仅保存供后续批次读取；
     * 不改当前条目、不发 currentFontChanged（与 setCurrentFont 分工）。 */
}

XFont XFontComboBox_displayFont(const XFontComboBox* self)
{
    /* 返回显示字体状态深拷贝（族名/样式字符串独立副本）；self 为
     * NULL 时返回默认构造字体（对标 Qt optional 空值的可用近似）。 */
    XFont font;
    XFont_init(&font);
    if (self)
        XCopy(&font, &self->m_displayFont);
    return font;
}

XString* XFontComboBox_sampleTextForFont(const XFontComboBox* self,
                                         const char* family)
{
    const char* text;
    if (!self || !family) return NULL;
    text = xfcb_fontSampleGet(self, family);
    if (!text) text = family; /* 字体采样未建：回退返回家族名作样例。 */
    return XString_create_utf8(text);
}

XString* XFontComboBox_sampleTextForSystem(const XFontComboBox* self,
                                           int system)
{
    const char* text;
    if (!self || !xfcb_isValidWritingSystem(system)) return NULL;
    text = xfcb_systemSampleGet(self, system);
    if (!text) text = XFontComboBox_currentFamily(self); /* 未采样回退当前族名。 */
    return XString_create_utf8(text);
}

void XFontComboBox_setSampleTextForFont(XFontComboBox* self,
                                        const char* family,
                                        const char* sample)
{
    if (!self) return;
    xfcb_fontSampleSet(self, family, sample);
}

void XFontComboBox_setSampleTextForSystem(XFontComboBox* self, int system,
                                          const char* sample)
{
    if (!self || !xfcb_isValidWritingSystem(system)) return;
    xfcb_systemSampleSet(self, system, sample);
}

const char* XFontComboBox_currentFamily(const XFontComboBox* self)
{
    if (!self) return "";
    return XComboBox_currentText_2(self);
}


/** @brief 发射带 const char* 参数的信号（UTF-8 借用）。 */
static void xfcb_emitText(XFontComboBox* self, size_t signal, const char* text)
{
    XVarList* args = XVarList_Create(XVar(const char*, text));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

void XFontComboBox_setCurrentFamily(XFontComboBox* self, const char* family)
{
    int i;
    int n;
    if (!self || !family) return;
    n = XComboBox_count(self);
    for (i = 0; i < n; ++i) {
        if (XStrcmp(XComboBox_itemText_2(self, i), family) == 0) {
            XComboBox_setCurrentIndex(self, i);
            xfcb_emitText(self,
                          (size_t)XFontComboBox_currentFontChanged_signal(
                              self, family),
                          family);
            return;
        }
    }
}




void XFontComboBox_setCurrentFont(XFontComboBox* self, const char* family)
{ XFontComboBox_setCurrentFamily(self, family); }





void* XFontComboBox_currentFontChanged_signal(
        XFontComboBox* self, const char* family)
{
    (void)family;
    return (void*)(size_t)XFontComboBox_currentFontChanged_signal;
}

#endif /* XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */