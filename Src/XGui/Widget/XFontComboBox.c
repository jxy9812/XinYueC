/**
 * @file       XFontComboBox.c
 * @brief      字体选择下拉框控件实现（对标 Qt 6.8 QFontComboBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XFontComboBox.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGuiConfig.h"
#include "XWidget.h"     /* XWidget_font（sizeHint 的 'm' 宽度测量字体）。 */
#include "XPainter.h"    /* XPainter_textWidth（对标 QFontMetrics::horizontalAdvance）。 */
#include "XClass.h"      /* XClass_Parent / XClassDeinit（父类槽与字体副本回收）。 */
#include "XFontFace.h"   /* XFont_face / XFontFace_kind（可缩放字体过滤）。 */
#include "XAbstractItemView.h" /* setItemFont（P3-62 弹层逐族显示字体）。 */

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
static void xfcb_emitText(XFontComboBox* self, size_t signal, const char* text);

/* ==================== 样例文本表（族名/书写系统 → 样例文本） ==================== */

/* 样例文本表首配容量（之后倍增摊还）。 */
#define XFCB_SAMPLE_MIN_CAPACITY 4

/** @brief 族名样例表条目（键=族名；值=样例文本；两者均为对象拥有的
 *         XString，经 XClassDelete 释放）。 */
typedef struct xfcb_FontSample
{
    XString* m_family; /**< 族名键（对象拥有）。 */
    XString* m_sample; /**< 样例文本（对象拥有；可为 NULL=键已占位）。 */
} xfcb_FontSample;

/** @brief 书写系统样例表条目（键=书写系统枚举值）。 */
typedef struct xfcb_SystemSample
{
    int m_system;       /**< 书写系统枚举值（键）。 */
    XString* m_sample;  /**< 样例文本（对象拥有）。 */
} xfcb_SystemSample;

/** @brief 逐族显示字体表条目（键=族名；值=XFont 值承载）。 */
typedef struct xfcb_DisplayFont
{
    XString* m_family;  /**< 族名键（对象拥有）。 */
    XFont m_font;       /**< 显示字体（值承载，内部字符串归条目）。 */
} xfcb_DisplayFont;

/** @brief 以 UTF-8 文本新建样例 XString 对象（表条目拥有）；失败返回 NULL。 */
static XString* xfcb_textClone(const char* text)
{
    if (!text) return NULL;
    return XString_create_utf8(text);
}

/** @brief 查询族名自定义样例文本；未设置返回 NULL。 */
static const char* xfcb_fontSampleGet(const XFontComboBox* self,
                                      const char* family)
{
    const xfcb_FontSample* table =
        (const xfcb_FontSample*)self->m_fontSamples;
    int i;
    for (i = 0; i < self->m_fontSampleCount; ++i) {
        if (table[i].m_family &&
            XString_equals_utf8(table[i].m_family, family,
                                XChar_CaseSensitive))
            return table[i].m_sample ? XString_toUtf8(table[i].m_sample)
                                     : "";
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
        if (table[i].m_family &&
            XString_equals_utf8(table[i].m_family, family,
                                XChar_CaseSensitive)) {
            target = i;
            break;
        }
    }
    if (!sample) {
        /* NULL 样例 = 移除该族自定义（交换尾条目保持表紧凑）。 */
        if (target < 0) return;
        if (table[target].m_family)
            XClassDelete((XClass*)table[target].m_family);
        if (table[target].m_sample)
            XClassDelete((XClass*)table[target].m_sample);
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
        XString* copy = xfcb_textClone(sample);
        if (!copy) return; /* 分配失败保留旧文本。 */
        if (table[target].m_sample)
            XClassDelete((XClass*)table[target].m_sample);
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
            XClassDelete((XClass*)table[i].m_family);
        if (table[i].m_sample)
            XClassDelete((XClass*)table[i].m_sample);
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
        if (table[i].m_system == system)
            return table[i].m_sample ? XString_toUtf8(table[i].m_sample)
                                     : "";
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
            XClassDelete((XClass*)table[target].m_sample);
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
        XString* copy = xfcb_textClone(sample);
        if (!copy) return; /* 分配失败保留旧文本。 */
        if (table[target].m_sample)
            XClassDelete((XClass*)table[target].m_sample);
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
            XClassDelete((XClass*)table[i].m_sample);
    }
    if (table) XMemory_free(table, XFCB_TABLE_MEMORY);
    self->m_systemSamples = NULL;
    self->m_systemSampleCount = 0;
    self->m_systemSampleCap = 0;
}

/* ==================== 逐族显示字体表（对标 setDisplayFont(family, font)） ==================== */

/** @brief 查询族显示字体登记（借用；未登记 NULL）。 */
static const XFont* xfcb_displayFontGet(const XFontComboBox* self,
                                        const char* family)
{
    const xfcb_DisplayFont* table = (const xfcb_DisplayFont*)self->m_displayFonts;
    int i;
    if (!self || !family) return NULL;
    for (i = 0; i < self->m_displayFontCount; ++i) {
        if (table[i].m_family &&
            XString_equals_utf8(table[i].m_family, family,
                                XChar_CaseSensitive))
            return &table[i].m_font;
    }
    return NULL;
}

/** @brief 登记/清除族显示字体（font=NULL 清除；同族替换；容量倍增）。 */
static void xfcb_displayFontSet(XFontComboBox* self, const char* family,
                                const XFont* font)
{
    xfcb_DisplayFont* table = (xfcb_DisplayFont*)self->m_displayFonts;
    int target = -1;
    int i;
    if (!self || !family || family[0] == '\0') return;
    for (i = 0; i < self->m_displayFontCount; ++i) {
        if (table[i].m_family &&
            XString_equals_utf8(table[i].m_family, family,
                                XChar_CaseSensitive)) {
            target = i;
            break;
        }
    }
    if (!font) {
        /* NULL 字体 = 清除该族登记（交换尾条目保持表紧凑）。 */
        if (target < 0) return;
        if (table[target].m_family)
            XClassDelete((XClass*)table[target].m_family);
        XClassDeinit((XClass*)&table[target].m_font);
        table[target] = table[self->m_displayFontCount - 1];
        --self->m_displayFontCount;
        return;
    }
    if (target < 0) {
        if (self->m_displayFontCount >= self->m_displayFontCap) {
            int newCap = self->m_displayFontCap > 0
                ? self->m_displayFontCap * 2 : XFCB_SAMPLE_MIN_CAPACITY;
            xfcb_DisplayFont* fresh = (xfcb_DisplayFont*)XMemory_calloc(
                (size_t)newCap, sizeof(xfcb_DisplayFont), XFCB_TABLE_MEMORY);
            if (!fresh) return;
            if (table) {
                XMemcpy(fresh, table,
                        (size_t)self->m_displayFontCount *
                            sizeof(xfcb_DisplayFont));
                XMemory_free(table, XFCB_TABLE_MEMORY);
            }
            self->m_displayFonts = fresh;
            self->m_displayFontCap = newCap;
            table = fresh;
        }
        target = self->m_displayFontCount;
        table[target].m_family = xfcb_textClone(family);
        XFont_init(&table[target].m_font);
        if (!table[target].m_family) return;
        ++self->m_displayFontCount;
    }
    /* 值语义深拷贝（XFont 内部族名/样式字符串归条目）。 */
    XClassCopy(&table[target].m_font, font);
}

/** @brief 释放逐族显示字体表（析构/拷贝/移动同步入口）。 */
static void xfcb_displayFontFree(XFontComboBox* self)
{
    xfcb_DisplayFont* table = (xfcb_DisplayFont*)self->m_displayFonts;
    int i;
    if (!self) return;
    for (i = 0; i < self->m_displayFontCount; ++i) {
        if (table[i].m_family)
            XClassDelete((XClass*)table[i].m_family);
        XClassDeinit((XClass*)&table[i].m_font);
    }
    if (table) XMemory_free(table, XFCB_TABLE_MEMORY);
    self->m_displayFonts = NULL;
    self->m_displayFontCount = 0;
    self->m_displayFontCap = 0;
}

/* ==================== 字体过滤器（对标 QFontComboBox::updateModel） ==================== */

/** @brief 族是否等宽（度量探测：'i' 与 'W' 推进宽相等）。
 * @details Qt 用 QFontDatabase::isFixedPitch（FT 元数据）；XPlatformFontDatabase
 *          只暴露家族快照、无该元数据接口（Src/XGui/Platform/
 *          XPlatformFontDatabase.h），故以度量探测等价：等宽字体两个宽度
 *          悬殊字符的推进相同。无法解析成轮廓 face 的族视为未知，保守判
 *          **非等宽**（避免把回退点阵度量误当等宽）。 */
static bool xfcb_familyIsFixedPitch(const char* family)
{
    XFont font;
    const XFontFace* face;
    int wi;
    int ww;
    bool fixedPitch = false;
    if (!family || !family[0]) return false;
    XFont_init_ex(&font, family, 12, -1, false);
    face = XFont_face(&font);
    if (face && XFontFace_kind(face) == XFontFace_Outline) {
        wi = XPainter_textWidth(&font, "i");
        ww = XPainter_textWidth(&font, "W");
        fixedPitch = (wi > 0 && wi == ww);
    }
    XClassDeinit((XClass*)&font);
    return fixedPitch;
}

/** @brief 族是否可缩放（解析 face 种类为 Outline）。
 * @details 对标 QFontDatabase::isSmoothlyScalable：轮廓字库可平滑缩放，
 *          内嵌点阵 face 不可（Qt 探针口径；本库以 face kind 判定）。 */
static bool xfcb_familyIsScalable(const char* family)
{
    XFont font;
    const XFontFace* face;
    bool scalable = false;
    if (!family || !family[0]) return false;
    XFont_init_ex(&font, family, 12, -1, false);
    face = XFont_face(&font);
    if (face) scalable = (XFontFace_kind(face) == XFontFace_Outline);
    XClassDeinit((XClass*)&font);
    return scalable;
}

/** @brief 按 FontFilter 位组合判断族是否入选（位间**与**语义）。
 * @details Qt QFontComboBoxPrivate::updateModel 的过滤条件：
 *          `(!filter || isSmoothlyScalable) && (!mono || isFixedPitch) ...`；
 *          全部位为 0（AllFonts）时不过滤。书写系统条件因缺标注数据
 *          不参与（见 setWritingSystem）。 */
static bool xfcb_familyAllowed(const XFontComboBox* self, const char* family)
{
    int f;
    if (!self) return true;
    f = self->m_filters;
    if (f == 0) return true;
    if ((f & (int)XFontComboBoxFilter_ScalableFonts) &&
        !xfcb_familyIsScalable(family))
        return false;
    if ((f & (int)XFontComboBoxFilter_NonScalableFonts) &&
        xfcb_familyIsScalable(family))
        return false;
    if ((f & (int)XFontComboBoxFilter_MonospacedFonts) &&
        !xfcb_familyIsFixedPitch(family))
        return false;
    if ((f & (int)XFontComboBoxFilter_ProportionalFonts) &&
        xfcb_familyIsFixedPitch(family))
        return false;
    return true;
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
                /* 按 fontFilters 过滤（Qt updateModel：families + 条件过滤）。 */
                if (xfcb_familyAllowed(self, XString_toUtf8(*name)))
                    XComboBox_addItem((XComboBox*)self, *name);
                XClassDelete(*name);
                *name = NULL;
            }
        }
        XClassDelete(families);
        families = NULL;
    }
    XPlatformFontDatabase_destroy(db);
#endif /* XPLATFORMFONTDATABASE_ON */
    /* 回退：数据库未启用或无字体族（且未设过滤）时补默认条目确保控件非空。
       设了过滤却一条都不匹配时保持空列表（不掺入与过滤条件不符的假条目，
       对标 Qt「过滤后为空即空」）。 */
    if (XComboBox_count((XComboBox*)self) == 0 && self->m_filters == 0) {
        XComboBox_addItem_2((XComboBox*)self, "XFontOutlineCommon");
        XComboBox_addItem_2((XComboBox*)self, "Sans Serif");
        XComboBox_addItem_2((XComboBox*)self, "Serif");
        XComboBox_addItem_2((XComboBox*)self, "Monospace");
    }
    if (XComboBox_count((XComboBox*)self) > 0)
        XComboBox_setCurrentIndex((XComboBox*)self, 0);
}

/* [P3-62] 前向声明：xfcb_rebuild 末尾重放逐族显示字体（定义在下方）。 */
static void xfcb_replayDisplayFonts(XFontComboBox* self, bool createView);

/** @brief 按当前 fontFilters 重建字体族列表（对标 updateModel）。
 * @details Qt 6.8 探针：setFontFilters 后列表重填、当前项回到 0 并发射
 *          currentIndexChanged(0)（族变化时另有 currentFontChanged）。
 *          本实现 clear + populate 复现该口径；清空步骤经
 *          XComboBox_clear 发 currentIndexChanged(-1)（Qt updateModel 内部
 *          的模型重置同样使当前索引短暂无效）。 */
static void xfcb_rebuild(XFontComboBox* self)
{
    if (!self) return;
    XComboBox_clear((XComboBox*)self);
    xfcb_populate(self);
    /* [P3-62] 条目重建后重放逐族显示字体（行号整体变化，FontRole 借用
     * 指针必须整体刷新，否则悬垂/错行）。 */
    xfcb_replayDisplayFonts(self, false);
}

/* ==================== 弹层逐族显示字体重放（P3-62） ==================== */

/** @brief 把已登记族的显示字体写到弹层视图 FontRole（整表重放）。
 * @details 对标 Qt 弹层条目按登记字体渲染（QFontComboBox 弹层的
 *          QFontFamilyDelegate 口径）：逐行 XAbstractItemView_setItemFont
 *          （借用 const XFont*，指向 m_displayFonts 表内条目）；未登记族
 *          一律写 NULL（清除旧 role，回落控件字体）。触发点＝
 *          ① 条目重建（xfcb_rebuild）；② 逐族登记表变更
 *          （setDisplayFont 增删可致表扩容/条目搬移，旧借用指针随之
 *          失效，必须整体重放）；③ 弹层每次显示（popupShown 自连一次，
 *          X 无 showPopup 虚槽可重载）。
 * @param self 目标控件。
 * @param createView true=弹层未建时懒建（弹层显示路径）；false=仅弹层
 *        已存在时重放（构造/重建路径避免提前建弹层）。 */
static void xfcb_replayDisplayFonts(XFontComboBox* self, bool createView)
{
    XListView* view;
    int i;
    int n;
    int col;
    if (!self) return;
    view = createView ? XComboBox_view((XComboBox*)self)
                      : ((XComboBox*)self)->m_popupView;
    if (!view) return;
    n = XComboBox_count((XComboBox*)self);
    col = XComboBox_modelColumn((XComboBox*)self);
    for (i = 0; i < n; ++i) {
        const char* family = XComboBox_itemText_2((XComboBox*)self, i);
        const XFont* font = (family && family[0])
            ? xfcb_displayFontGet(self, family) : NULL;
        XAbstractItemView_setItemFont((XAbstractItemView*)view, i, col, font);
    }
}

/** @brief 弹层显示即重放逐族显示字体（自连 popupShown 的槽）。 */
static void xfcb_popupShownSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    xfcb_replayDisplayFonts((XFontComboBox*)receiver, true);
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
    XClassCopy(&self->m_displayFont, &other->m_displayFont);
    /* 样例文本表深拷贝（键与文本均为对象拥有的 XString，按 UTF-8
     * 转换后重设，重建目标侧所有权）。 */
    xfcb_fontSampleFree(self);
    xfcb_systemSampleFree(self);
    xfcb_displayFontFree(self);
    fontSamples = (const xfcb_FontSample*)other->m_fontSamples;
    for (i = 0; i < other->m_fontSampleCount; ++i) {
        const char* familyUtf8 = fontSamples[i].m_family
            ? XString_toUtf8(fontSamples[i].m_family) : NULL;
        const char* sampleUtf8 = fontSamples[i].m_sample
            ? XString_toUtf8(fontSamples[i].m_sample) : NULL;
        if (familyUtf8)
            xfcb_fontSampleSet(self, familyUtf8, sampleUtf8);
    }
    systemSamples = (const xfcb_SystemSample*)other->m_systemSamples;
    for (i = 0; i < other->m_systemSampleCount; ++i) {
        const char* sampleUtf8 = systemSamples[i].m_sample
            ? XString_toUtf8(systemSamples[i].m_sample) : NULL;
        xfcb_systemSampleSet(self, systemSamples[i].m_system, sampleUtf8);
    }
    /* 逐族显示字体表深拷贝（键/字体均重建到目标侧所有权）。 */
    {
        const xfcb_DisplayFont* dfs =
            (const xfcb_DisplayFont*)other->m_displayFonts;
        for (i = 0; i < other->m_displayFontCount; ++i) {
            if (dfs[i].m_family)
                xfcb_displayFontSet(self, XString_toUtf8(dfs[i].m_family),
                                    &dfs[i].m_font);
        }
    }
    /* currentFont 记忆深拷贝（对标 currentFont 值语义）。 */
    if (self->m_currentFont) {
        XClassDelete((XClass*)self->m_currentFont);
        self->m_currentFont = NULL;
    }
    if (other->m_currentFont) {
        const char* fam = XString_toUtf8(other->m_currentFont);
        if (fam) self->m_currentFont = XString_create_utf8(fam);
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
    XClassMove(&self->m_displayFont, &other->m_displayFont);
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
    /* 逐族显示字体表：整体转移所有权。 */
    xfcb_displayFontFree(self);
    self->m_displayFonts = other->m_displayFonts;
    self->m_displayFontCount = other->m_displayFontCount;
    self->m_displayFontCap = other->m_displayFontCap;
    other->m_displayFonts = NULL;
    other->m_displayFontCount = 0;
    other->m_displayFontCap = 0;
    /* currentFont 记忆：转移所有权，源归 NULL。 */
    if (self->m_currentFont) XClassDelete((XClass*)self->m_currentFont);
    self->m_currentFont = other->m_currentFont;
    other->m_currentFont = NULL;
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
    xfcb_displayFontFree(self);
    if (self->m_currentFont) {
        XClassDelete((XClass*)self->m_currentFont);
        self->m_currentFont = NULL;
    }
    XClassDeinit((XClass*)&self->m_displayFont);
    self->m_filters = (int)XFontComboBoxFilter_AllFonts;
    self->m_writingSystem = (int)XFontComboBoxWritingSystem_Any;
    XClass_Deinit_Parent(XComboBox, (XComboBox*)self);
}

/**
 * @brief sizeHint 虚槽（对标 QFontComboBox::sizeHint，qfontcombobox.cpp:544-550）。
 * @details Qt 6.8 原文：
 *          `QSize sz = QComboBox::sizeHint(); QFontMetrics fm(font());
 *           sz.setWidth(fm.horizontalAdvance(u'm') * 14); return sz;`
 *          ——字体族名长短差异极大（"Noto Sans CJK SC" vs "Serif"），
 *          Qt 有意**不**按最长族名定宽，一律取控件字体下 'm' 的 14 倍宽；
 *          高度仍取 QComboBox::sizeHint。此前本类未重载 sizeHint，宽度被
 *          最长族名撑开（组合框本体与弹层同宽）。minimumSizeHint 保持继承
 *          QComboBox 口径（Qt 未在 QFontComboBox 里重载它）。
 * @param self 目标控件（虚表口径）。
 * @return 首选尺寸；self 为 NULL 时返回 (-1,-1)。
 */
static XSize VXFontComboBox_sizeHintSlot(const XWidget* self)
{
    XSize hint;
    XFont font;
    int w;
    if (!self) {
        XSize_init(&hint, -1, -1);
        return hint;
    }
    /* 父类（XComboBox）首选尺寸：高度与内容策略口径同 Qt。 */
    hint = XClass_Parent(XComboBox, EXWidget_SizeHint,
                         XSize (*)(const XWidget*))(self);
    font = XWidget_font(self);
    w = XPainter_textWidth(&font, "m") * 14;
    XClassDeinit((XClass*)&font);
    if (w > 0) hint.width = w;
    return hint;
}

/** @brief 弹层宽度虚槽（对标 QFontComboBox 的弹层容器宽设定，
 *         qfontcombobox.cpp:526-537）。
 * @details Qt 6.8 原文口径：`min(width()*5/3, screenWidth)`；本机 Qt 6.8
 *          探针：120 宽的字体组合框弹层宽 200（屏宽 800 未触顶），而普通
 *          QComboBox 同宽弹层保持 120。屏宽钳位由基类
 *          xcombo_popupReposition 的「钳到可用区宽」承担，此处只做 5/3 放大。
 * @param self 目标控件（虚表口径）。 @return 弹层期望宽。 */
static int VXFontComboBox_popupWidthSlot(const XComboBox* self)
{
    int w = XComboBox_popupWidth_base(self);
    if (w < 1) return w;
    return w * 5 / 3;
}

XVtable* XFontComboBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFontComboBox)
    XVTABLE_INHERIT_XCLASS(XComboBox);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXFontComboBox_sizeHintSlot);
    /* [Qt 对齐 2026-11-11] 弹层宽 5/3（gap 283）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXComboBox_PopupWidth,
                             VXFontComboBox_popupWidthSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXFontComboBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXFontComboBox_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFontComboBox_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 转发槽：基类 currentTextChanged → currentFontChanged（对标
 *  QFontComboBoxPrivate::currentChanged，qfontcombobox.cpp 6.8.2:352-360）：
 *  当前文本与**记忆族名**（m_currentFont，对标 currentFont 成员）不同才
 *  更新记忆并发射——不比实时 currentText（当前项已更新，比对恒等会吞掉
 *  发射；旧实现依赖「初始 current=-1 读空串」的偶然前置，首插语义对齐
 *  后暴露）。用户经弹层/键入/程序化三条路径的当前文本变化由此统一收口。 */
static void xfcb_currentTextChangedSlot(XObject* receiver, XVarList* args)
{
    XFontComboBox* self = (XFontComboBox*)receiver;
    const char* text = NULL;
    const char* remembered;
    if (args) {
        XVarList_args_1(args, const char*, t);
        text = t;
    }
    if (!self) return;
    if (!text) text = XComboBox_currentText_2((XComboBox*)self);
    if (!text) text = "";
    /* 列表重建的中间态（clear 后 count=0）不发空族名的 currentFontChanged：
     * Qt updateModel 只在新列表选中后发真实族名（本机探针 setFontFilters
     * 后只看到新族名的 fontChanged）。 */
    if (text[0] == '\0' && XComboBox_count((XComboBox*)self) == 0) return;
    remembered = self->m_currentFont
        ? XString_toUtf8(self->m_currentFont) : "";
    if (XStrcmp(remembered, text) == 0) return;
    if (self->m_currentFont)
        XString_assign_utf8(self->m_currentFont, text);
    else
        self->m_currentFont = XString_create_utf8(text);
    xfcb_emitText(self,
                  (size_t)XFontComboBox_currentFontChanged_signal(
                      self, text),
                  text);
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
    /* 对标 qfontcombobox.cpp:403 QFontComboBox 构造：setEditable(true)——
       字体组合框恒可编辑（缺省 false 使 isEditable 语义与 Qt 相反）。 */
    XComboBox_setEditable((XComboBox*)self, true);
    self->m_filters = (int)XFontComboBoxFilter_AllFonts;
    self->m_writingSystem = (int)XFontComboBoxWritingSystem_Any;
    XFont_init(&self->m_displayFont);
    self->m_fontSamples = NULL;
    self->m_fontSampleCount = 0;
    self->m_fontSampleCap = 0;
    self->m_systemSamples = NULL;
    self->m_systemSampleCount = 0;
    self->m_systemSampleCap = 0;
    self->m_displayFonts = NULL;
    self->m_displayFontCount = 0;
    self->m_displayFontCap = 0;
    self->m_currentFont = NULL;
    xfcb_populate(self);
    /* 对标 qfontcombobox.cpp:412-416：currentTextChanged → currentChanged
       （用户选字体时发 currentFontChanged 的通路）；在 populate 置当前项
       之后连接，初始填充不触发信号（Qt 同序）。连接后按 Qt
       `_q_currentChanged` 口径记一次当前族名（初值记忆——否则首个用户
       选择与初始族名同文本时被误去重）。 */
    {
        const char* init = XFontComboBox_currentFamily(self);
        if (init && init[0])
            self->m_currentFont = XString_create_utf8(init);
    }
    XObject_connect((XObject*)self,
                      (size_t)XComboBox_currentTextChanged_signal(
                          (XComboBox*)self, NULL),
                      (XObject*)self, xfcb_currentTextChangedSlot,
                      XConnectionType_Direct);
    /* [P3-62] 弹层每次显示即重放逐族显示字体（X 无 showPopup 虚槽可
       重载，用现成 popupShown 扩展信号自连一次；rebuild/登记表变更
       两触发点见 xfcb_replayDisplayFonts 注记）。 */
    XObject_connect((XObject*)self,
                      (size_t)XComboBox_popupShown_signal((XComboBox*)self),
                      (XObject*)self, xfcb_popupShownSlot,
                      XConnectionType_Direct);
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
    /* [Qt 对齐 2026-11-11] 过滤条件变化即按新条件重填族列表
     * （QFontComboBoxPrivate::updateModel，qfontcombobox.cpp:307-370）；
     * 此前仅存状态、列表不重建。 */
    xfcb_rebuild(self);
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

/* [P2-52] XFontComboBox_setDisplayFont(self,font) 无族名单值版已删：
 * Qt 6.8 只有逐族版；单值状态无任何消费者（头注自认「字形预览/委托
 * 绘制未建」）。Qt 名 setDisplayFont 归还逐族版（P1-36）。
 * 结构体字段 m_displayFont 保留：XClassCopy/Move/Deinit 与 init 仍按
 * 值语义维护它（拷贝/移动契约不变），仅公开写读入口收敛为逐族版。 */

void XFontComboBox_setDisplayFont(XFontComboBox* self, const char* family,
                                   const XFont* font)
{
    /* 对标 QFontComboBox::setDisplayFont(family, font)：逐族登记；
     * font=NULL 清除该族登记（Qt 没有删除接口，本库扩展语义）。 */
    if (!self) return;
    xfcb_displayFontSet(self, family, font);
    /* [P3-62] 登记表可能扩容/搬移（条目交换保持紧凑），弹层 FontRole
     * 的借用指针必须整体重放，否则悬垂。 */
    xfcb_replayDisplayFonts(self, false);
}

bool XFontComboBox_displayFont(const XFontComboBox* self, const char* family,
                                XFont* out)
{
    const XFont* reg;
    if (!self || !family || !family[0]) return false;
    reg = xfcb_displayFontGet(self, family);
    if (out) {
        XFont_init(out);
        if (reg) XClassCopy(out, reg);
        else XFont_init_ex(out, family, 12, -1, false); /* 未登记：QFont(family) 口径。 */
    }
    return true;
}

XFont XFontComboBox_currentFont(const XFontComboBox* self)
{
    /* 对标 QFontComboBox::currentFont 返回 QFont：本库以当前族名构造 XFont
     * 值（字号/粗细取缺省；调用方 XClassDeinit 释放）。 */
    XFont font;
    const char* family = self ? XFontComboBox_currentFamily(self) : NULL;
    XFont_init(&font);
    if (family && family[0])
        XFont_init_ex(&font, family, 12, -1, false);
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
    return XComboBox_currentText_2((const XComboBox*)self);
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

/** @brief 在条目列表中按族名查找（① 精确 → ② 大小写不敏感）。
 * @param self 目标控件。
 * @param family 族名（UTF-8）。
 * @param caseSensitiveOnly true=只做精确匹配。
 * @return 命中索引；未命中 -1。 */
static int xfcb_findFamilyIndex(const XFontComboBox* self, const char* family,
                                bool caseSensitiveOnly)
{
    int i;
    int n;
    const char* text;
    if (!self || !family || !family[0]) return -1;
    n = XComboBox_count((XComboBox*)self);
    for (i = 0; i < n; ++i) {
        /* itemText_2 透传 toUtf8：空 XString 条目返回 NULL（如本地化字体
           族名枚举产生的空串），比较前必须跳过，否则 strcmp(NULL) 段错误。 */
        text = XComboBox_itemText_2((XComboBox*)self, i);
        if (!text) continue;
        if (XStrcmp(text, family) == 0) return i;
    }
    if (caseSensitiveOnly) return -1;
    for (i = 0; i < n; ++i) {
        text = XComboBox_itemText_2((XComboBox*)self, i);
        if (!text) continue;
        if (XStrcasecmp(text, family) == 0) return i;
    }
    return -1;
}

/** @brief 选中指定索引并收敛编辑框显示文本（同索引但编辑文本分叉时）。
 * @param self 目标控件。
 * @param i 命中索引。
 * @param family 目标族名（回填用）。
 * @return 无。 */
static void xfcb_selectFamilyIndex(XFontComboBox* self, int i,
                                   const char* family)
{
    if (XComboBox_currentIndex((XComboBox*)self) == i) {
        /* 同行号但编辑文本已分叉（用户键入未采纳）时回填收敛：
           setCurrentIndex 去重不发射，此处仅收敛显示文本。 */
        const char* edit = XComboBox_lineEdit((XComboBox*)self)
            ? XLineEdit_text(XComboBox_lineEdit((XComboBox*)self))
            : NULL;
        if (edit && XStrcmp(edit, family) != 0)
            XComboBox_setEditText_2((XComboBox*)self, family);
        return;
    }
    XComboBox_setCurrentIndex((XComboBox*)self, i);
}

void XFontComboBox_setCurrentFamily(XFontComboBox* self, const char* family)
{
    int idx;
    if (!self || !family || !family[0]) return;
    /* ① 精确 → ② 大小写不敏感（QFontInfo 会规范族名大小写；本机 Qt 6.8
     * 探针 setCurrentFont(QFont("dejavu serif")) 选中 "DejaVu Serif"）。 */
    idx = xfcb_findFamilyIndex(self, family, false);
    if (idx >= 0) {
        /* 对标 Qt：setCurrentFont → setCurrentText → setEditable 态
           setCurrentIndex；currentFontChanged 经构造连接的
           currentTextChanged → currentChanged 通路发射（qfontcombobox.cpp:
           412-416）——同族重复 set 不发射（槽内按族名去重）。 */
        xfcb_selectFamilyIndex(self, idx, family);
        return;
    }
    /* ③ 未命中 ⇒ 按本控件字体族重试（Qt 经 QFontInfo 把未知族名解析成
     *    应用缺省族；本库无字体解析器，以控件字体族近似——本机探针里
     *    未知族名最终选中缺省族 "思源黑体"）。 */
    {
        XFont wfont = XWidget_font((const XWidget*)self);
        const char* wfamily = XFont_family(&wfont);
        int widx = -1;
        if (wfamily && wfamily[0] && XStrcmp(wfamily, family) != 0)
            widx = xfcb_findFamilyIndex(self, wfamily, false);
        XClassDeinit((XClass*)&wfont);
        if (widx >= 0) {
            xfcb_selectFamilyIndex(self, widx, family);
            return;
        }
    }
    /* ④ 仍未命中：无操作（不臆造条目；gaps 登记此 Qt 解析差异）。 */
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

#endif /* XGUI_ON */
