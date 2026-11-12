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
/* [B11] 书写系统过滤的逐族标注来源：Linux 桌面用 fontconfig 的 FC_LANG
 * langset（与 Qt 6.8 qfontconfigdatabase.cpp:433-456 同源同口径）。
 * 命中条件与 Drive 侧 XPlatformFontDatabase_posix.c 完全一致（同一份
 * CMake 探测宏），保证「族列表来自 fontconfig ⇒ 只要系统有 fontconfig，
 * 过滤就有标注数据」不会出现半有半无的组合。 */
#if defined(XINYUE_C_HAS_FONTCONFIG) && defined(__linux__)
#include <fontconfig/fontconfig.h>
#define XFCB_HAS_FONTCONFIG 1
#else
#define XFCB_HAS_FONTCONFIG 0
#endif

#if XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON

/* 样例文本表使用的内存池类型：与本类堆创建默认类型一致（分配与释放
 * 使用同一常量保证配对）。 */
#define XFCB_TABLE_MEMORY XCLASS_DEFAULT_MEMORY_TYPE

static void xfcb_fontSampleFree(XFontComboBox* self);
static void xfcb_systemSampleFree(XFontComboBox* self);
static void xfcb_emitText(XFontComboBox* self, size_t signal, const char* text);
/* [B11] 条目重建后按族名重选当前项（定义在文件后半；重建在 populate 之前）。 */
static int xfcb_findFamilyIndex(const XFontComboBox* self, const char* family,
                                bool caseSensitiveOnly);
static void xfcb_selectFamilyIndex(XFontComboBox* self, int i);
/* [B11] 书写系统取值校验（定义在 setWritingSystem 段；书写系统判定先用到）。 */
static bool xfcb_isValidWritingSystem(int system);

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

/* ============ 书写系统过滤的逐族支持判定（B11） ============ */

/** @brief 书写系统 → fontconfig 语言标签（Qt 6.8 单一来源）。
 * @details 逐值对齐 Qt 6.8 `qfontconfigdatabase.cpp` 的
 *          `languageForWritingSystem[]`（本机 fontconfig 2.17.1 / Qt 6.8.0
 *          原文，见该文件 :264-300）：索引即 Qt::WritingSystem 值，
 *          `""` 表示该系统无对应语言标签。只列本类枚举受理的成员
 *          （其余项即使有标签也不受理，枚举面不扩大）。
 * @note    Qt 的 Ojham/Runic/Nko 等项本类无对应枚举成员，故表内留空。 */
static const char* xfcb_fontconfigLang(int system)
{
    switch (system)
    {
    case (int)XFontComboBoxWritingSystem_Latin: return "en";
    case (int)XFontComboBoxWritingSystem_Greek: return "el";
    case (int)XFontComboBoxWritingSystem_Cyrillic: return "ru";
    case (int)XFontComboBoxWritingSystem_Armenian: return "hy";
    case (int)XFontComboBoxWritingSystem_Hebrew: return "he";
    case (int)XFontComboBoxWritingSystem_Arabic: return "ar";
    case (int)XFontComboBoxWritingSystem_Devanagari: return "hi";
    case (int)XFontComboBoxWritingSystem_Bengali: return "bn";
    case (int)XFontComboBoxWritingSystem_Tamil: return "ta";
    case (int)XFontComboBoxWritingSystem_Thai: return "th";
    case (int)XFontComboBoxWritingSystem_SimplifiedChinese: return "zh-cn";
    case (int)XFontComboBoxWritingSystem_TraditionalChinese: return "zh-tw";
    case (int)XFontComboBoxWritingSystem_Japanese: return "ja";
    case (int)XFontComboBoxWritingSystem_Korean: return "ko";
    case (int)XFontComboBoxWritingSystem_Vietnamese: return "vi";
    default: return NULL; /* Any / Symbol：无语言标签（另走通配/样本口径）。 */
    }
}

#if XFCB_HAS_FONTCONFIG
/* ===== 标注来源 A：fontconfig FC_LANG（Qt 同源口径） ===== */

/** @brief fontconfig 全局配置是否可用（**只读**，不调 FcInit/FcFini）。
 * @details 红线依据（本机 fontconfig 2.17.1 探针，见
 *          `Drive/Posix/Graphics/XPlatformFontDatabase_posix.c:10-52`）：
 *          **FcInit 必须与 FcFini 一一配对**，多一次 FcInit 就多 630 字节
 *          泄漏，而该文件是全仓唯一的 FcInit 站点（它按「进程期初始化
 *          一次 + atexit 统一释放」管理）。本文件若自建 FcInit/FcFini 配对，
 *          会与 Drive 的单例互相拆台（本机 ASan 探针实测：两边各 FcInit
 *          一次、只 FcFini 一次 ⇒ 仍报 630 字节）。
 *          故这里**只读**：FcConfigGetCurrent 在 libfontconfig 内部会
 *          惰性载入默认配置（本机探针实测 FcFontList 在未调 FcInit 时
 *          已可用、族数一致），不引入第二个 FcInit 站点、不新增泄漏。
 * @return 配置句柄；不可用返回 NULL（调用方据此降级）。 */
static FcConfig* xfcb_fontconfigConfig(void)
{
    return FcConfigGetCurrent();
}

/** @brief 全量语言标签表（`xfcb_fcFamiliesBuild` 的 Other 判定用）。
 * @details Qt 6.8 `qfontconfigdatabase.cpp:264-300` 的
 *          `languageForWritingSystem[]` **全量非空项**（索引 = Qt::WritingSystem
 *          值，仅跳过 `""` 项）。Qt 判「某族不属于任何已知书写系统」时
 *          扫的就是这张全表（`:442-455` 的 `for (j = 1; j < WritingSystemsCount;
 *          ++j)`），故此处必须同样全量——只列本类枚举的 15 个成员会把只有
 *          Malayalam/Kannada/Lao/Tibetan 等标签的族误算进 Other
 *          （本机实测差 24 族：174 vs 151）。 */
static const char* const kXfcbKnownLangs[] = {
    "en",    /* Latin */
    "el",    /* Greek */
    "ru",    /* Cyrillic */
    "hy",    /* Armenian */
    "he",    /* Hebrew */
    "ar",    /* Arabic */
    "syr",   /* Syriac */
    "div",   /* Thaana */
    "hi",    /* Devanagari */
    "bn",    /* Bengali */
    "pa",    /* Gurmukhi */
    "gu",    /* Gujarati */
    "or",    /* Oriya */
    "ta",    /* Tamil */
    "te",    /* Telugu */
    "kn",    /* Kannada */
    "ml",    /* Malayalam */
    "si",    /* Sinhala */
    "th",    /* Thai */
    "lo",    /* Lao */
    "bo",    /* Tibetan */
    "my",    /* Myanmar */
    "ka",    /* Georgian */
    "km",    /* Khmer */
    "zh-cn", /* SimplifiedChinese */
    "zh-tw", /* TraditionalChinese */
    "ja",    /* Japanese */
    "ko",    /* Korean */
    "vi",    /* Vietnamese */
    "sga",   /* Ogham */
    "non",   /* Runic */
    "man"    /* Nko */
};

/** @brief fontconfig 族标注表（每次重建枚举一次，避免逐族 FcFontList）。
 * @details **为什么要建表**：逐族 `FcFontList(FcPatternBuild(FC_FAMILY=x))`
 *          每次都要走一遍 fontconfig 的规则匹配（本机 251 族实测单次重建
 *          15ms，全部耗在这里）。改成一趟全量枚举（与 Qt
 *          `populateFontDatabase` 同为 `FcFontList(config, FcPatternCreate(),
 *          os)`）把逐族查询变成哈希查表，重建耗时回到 populate 本身的开销。
 *          口径与 Qt 一致：一次枚举里逐 pattern 取 `FC_FAMILY[0]` 与
 *          `FC_LANG` langset，把 langset 换算成「支持哪些书写系统」的位集
 *          （`languageForWritingSystem` 逐项 `FcLangSetHasLang`，判定式同
 *          qfontconfigdatabase.cpp:447-449）；同名族多 face 取**并集**
 *          （Qt 按族登记 writingSystems 亦为并集）。无 FC_LANG 或 langset
 *          无任何已知语言 ⇒ 该族置 Other 位。
 * @note   本表是**每次重建的局部快照**（不复用跨调用），故 fontconfig
 *         配置在进程期变化（fc-cache 刷新等）后下一次重建自动跟随，与
 *         Qt 的 `fontDatabaseChanged` 重建语义方向一致（Qt 靠信号，本库
 *         靠下一次 setWritingSystem 重建）。
 * @warning 只用 FcFontList 与 Fc*Destroy 读接口，**不持有** FcConfig；
 *          配置句柄失败时返回 false，调用方降级（不崩）。
 */
typedef struct xfcb_FcFamilyEntry
{
    char m_family[128];                     /**< 族名（截断保护）。 */
    uint32_t m_systems[(32 / 32) + 1];      /**< 支持位集：bit(system)（含
                                             *   Other 位 = Symbol 值）。 */
    bool m_other;                           /**< 无任何已知语言（Qt Other）。 */
} xfcb_FcFamilyEntry;

/** @brief 一次 fontconfig 枚举的结果表（栈上值对象，用后释放）。 */
typedef struct xfcb_FcFamilies
{
    xfcb_FcFamilyEntry* m_entries;          /**< 条目数组（堆；release 释放）。 */
    int m_count;                            /**< 条目数（0=枚举失败/空）。 */
    int m_capacity;                         /**< 容量。 */
} xfcb_FcFamilies;

/** @brief 该族支持某书写系统返回 true（查已建好的表）。 */
static bool xfcb_fcFamiliesSupports(const xfcb_FcFamilies* table,
                                    const char* family, int system)
{
    int i;
    if (!table || !table->m_entries || !family || !family[0]) return false;
    for (i = 0; i < table->m_count; ++i)
    {
        if (XStrcmp(table->m_entries[i].m_family, family) == 0)
        {
            if (system == (int)XFontComboBoxWritingSystem_Symbol)
                return table->m_entries[i].m_other;
            if (system < 0 || system >= 32) return false;
            return (table->m_entries[i].m_systems[system / 32] &
                    ((uint32_t)1u << (system % 32))) != 0u;
        }
    }
    return false; /* 表里没有该族 ⇒ 无标注 ⇒ 保守判不支持（同 Qt 无标签口径）。 */
}

/** @brief 释放枚举表。 */
static void xfcb_fcFamiliesRelease(xfcb_FcFamilies* table)
{
    if (!table) return;
    if (table->m_entries)
        XMemory_free(table->m_entries, XFCB_TABLE_MEMORY);
    table->m_entries = NULL;
    table->m_count = 0;
    table->m_capacity = 0;
}

/** @brief 在表里按族名找条目（返回可写指针；未命中返回 NULL）。 */
static xfcb_FcFamilyEntry* xfcb_fcFamiliesFind(xfcb_FcFamilies* table,
                                               const char* family)
{
    int i;
    if (!table || !table->m_entries || !family) return NULL;
    for (i = 0; i < table->m_count; ++i)
        if (XStrcmp(table->m_entries[i].m_family, family) == 0)
            return &table->m_entries[i];
    return NULL;
}

/** @brief 枚举一次 fontconfig 全部字体族并换算成书写系统支持位集。
 * @details 实现要点见 xfcb_FcFamilies 的头注。只读 fontconfig，不
 *          FcInit/FcFini（红线见 xfcb_fontconfigConfig）。
 * @param table 出参（先前内容会被释放后重建）。
 * @return 枚举成功（即使 0 族）返回 true。
 */
static bool xfcb_fcFamiliesBuild(xfcb_FcFamilies* table)
{
    FcConfig* config;
    FcPattern* pattern;
    FcObjectSet* objects;
    FcFontSet* set;
    int i;
    int j;
    if (!table) return false;
    xfcb_fcFamiliesRelease(table);
    config = xfcb_fontconfigConfig();
    if (!config) return false;
    pattern = FcPatternCreate();
    objects = FcObjectSetBuild(FC_FAMILY, FC_LANG, NULL);
    set = (pattern && objects) ? FcFontList(config, pattern, objects) : NULL;
    if (pattern) FcPatternDestroy(pattern);
    if (objects) FcObjectSetDestroy(objects);
    if (!set) return false;
    for (i = 0; i < set->nfont; ++i)
    {
        FcChar8* fam = NULL;
        FcLangSet* langset = NULL;
        bool hasKnownLang = false;
        xfcb_FcFamilyEntry* entry;
        if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &fam) !=
                FcResultMatch || !fam || !fam[0])
            continue;
        /* langset 保留 FcPatternGetLangSet 的 match 结果：未匹配保持
           NULL，下方 `if (langset)` 与「无 FC_LANG ⇒ Other」（Qt 同）
           同口径；「langset 存在但无值」同「无标签」。 */
        (void)FcPatternGetLangSet(set->fonts[i], FC_LANG, 0, &langset);
        entry = xfcb_fcFamiliesFind(table, (const char*)fam);
        if (!entry)
        {
            if (table->m_count >= table->m_capacity)
            {
                int newCap = table->m_capacity > 0 ? table->m_capacity * 2
                                                   : 256;
                xfcb_FcFamilyEntry* fresh = (xfcb_FcFamilyEntry*)XMemory_calloc(
                    (size_t)newCap, sizeof(xfcb_FcFamilyEntry),
                    XFCB_TABLE_MEMORY);
                if (!fresh) break;
                if (table->m_entries)
                {
                    XMemcpy(fresh, table->m_entries,
                            (size_t)table->m_count *
                                sizeof(xfcb_FcFamilyEntry));
                    XMemory_free(table->m_entries, XFCB_TABLE_MEMORY);
                }
                table->m_entries = fresh;
                table->m_capacity = newCap;
            }
            entry = &table->m_entries[table->m_count];
            XMemset(entry, 0, sizeof(*entry));
            XStrncpy(entry->m_family, (const char*)fam,
                     sizeof(entry->m_family) - 1);
            ++table->m_count;
        }
        if (langset)
        {
            for (j = 1; j < 32; ++j)
            {
                const char* lang = xfcb_fontconfigLang(j);
                if (!lang) continue;
                if (FcLangSetHasLang(langset, (const FcChar8*)lang) !=
                    FcLangDifferentLang)
                {
                    entry->m_systems[j / 32] |=
                        ((uint32_t)1u << (j % 32));
                    hasKnownLang = true;
                }
            }
            /* Other 的判定必须按 Qt 全表（不是本类枚举子集，见
             * kXfcbKnownLangs 的头注）：langset 里没有任何**已知书写系统**
             * 语言 ⇒ 该 face 属 Other。 */
            {
                size_t k;
                for (k = 0; k < sizeof(kXfcbKnownLangs) /
                                    sizeof(kXfcbKnownLangs[0]); ++k)
                {
                    if (FcLangSetHasLang(
                            langset, (const FcChar8*)kXfcbKnownLangs[k]) !=
                        FcLangDifferentLang)
                    {
                        hasKnownLang = true;
                        break;
                    }
                }
            }
        }
        if (!hasKnownLang) entry->m_other = true;
    }
    FcFontSetDestroy(set);
    return true;
}
#endif /* XFCB_HAS_FONTCONFIG */

/** @brief 无库兜底族清单（兜底条目与降档标注表的单源）：标注表必须建在
 * 「将要列入的族集」上，兜底族也参与书写系统过滤——XFontOutlineCommon
 * 解析自有 face 有真实标注；Sans Serif/Serif/Monospace 解析回落替身
 * ⇒ 无标注 ⇒ 被过滤，正是降档真实口径，不是「常量探测」。 */
static const char* const xfcb_kFallbackFamilies[] = {
    "XFontOutlineCommon", "Sans Serif", "Serif", "Monospace"
};

#if !XFCB_HAS_FONTCONFIG
/* ===== 标注来源 B：框架字库逐族字形覆盖探测（降级档，B11 返工） =====
 * 结构与 fc 档标注表同构（族名 + 支持位集 + Other 位），判定入口
 * xfcb_glyphFamiliesSupports 与 xfcb_fcFamiliesSupports 逐语义对应，
 * 过滤主体（xfcb_familyAllowedWithTable）两档共用同一份代码。 */

/** @brief 书写系统 → 代表探测码位（Qt scriptForWritingSystem 的子集）。
 * @details 逐值依据 Qt 6.8 qfontdatabase.cpp:349-386 的
 *          `scriptForWritingSystem[]`（书写系统 → QChar::Script）：
 *          每个受理系统取该 script 一个高频代表码位。SC/TC/JP 同属
 *          Script_Han，此处以「中(0x4E2D)/漢(0x6F22)/あ(0x3042)」三码位
 *          近似区分（日文字库多含「漢」⇒ TC 档偏宽，登记窄口径，见
 *          「两档同构」注）；Vietnamese 在 Qt 同 Latin（:383 注释），以
 *          越南文专属变音字 Ạ(0x1EA0) 判别（fc 口径 lang=vi 的代表字）。
 *          Any/Symbol 无码位：Any 不过滤；Symbol=Other（全不中判定，
 *          见 xfcb_glyphFamiliesBuild 尾注）。 */
static uint32_t xfcb_scriptSample(int system)
{
    switch (system)
    {
    case (int)XFontComboBoxWritingSystem_Latin: return 0x41u;         /* A */
    case (int)XFontComboBoxWritingSystem_Greek: return 0x0391u;       /* Α */
    case (int)XFontComboBoxWritingSystem_Cyrillic: return 0x0410u;    /* А */
    case (int)XFontComboBoxWritingSystem_Armenian: return 0x0531u;    /* Ա */
    case (int)XFontComboBoxWritingSystem_Hebrew: return 0x05D0u;      /* א */
    case (int)XFontComboBoxWritingSystem_Arabic: return 0x0627u;      /* ا */
    case (int)XFontComboBoxWritingSystem_Devanagari: return 0x0905u;  /* अ */
    case (int)XFontComboBoxWritingSystem_Bengali: return 0x0985u;     /* অ */
    case (int)XFontComboBoxWritingSystem_Tamil: return 0x0B85u;       /* அ */
    case (int)XFontComboBoxWritingSystem_Thai: return 0x0E01u;        /* ก */
    case (int)XFontComboBoxWritingSystem_SimplifiedChinese:
        return 0x4E2Du;                                               /* 中 */
    case (int)XFontComboBoxWritingSystem_TraditionalChinese:
        return 0x6F22u;                                               /* 漢 */
    case (int)XFontComboBoxWritingSystem_Japanese: return 0x3042u;    /* あ */
    case (int)XFontComboBoxWritingSystem_Korean: return 0xAC00u;      /* 가 */
    case (int)XFontComboBoxWritingSystem_Vietnamese: return 0x1EA0u;  /* Ạ */
    default: return 0u; /* Any / Symbol：无直接码位（Symbol=Other 推定）。 */
    }
}

/** @brief 该族 face 是否真实含 codepoint 字形（存在性判定，非装载）。
 * @details 轮廓 face 走 LoadOutlineGlyph 槽：FT 槽在
 *          FT_Get_Char_Index==0 时返回 false（XFontFt.c 缺字形分支，
 *          与渲染路径同一判定）；FT_Outline_Decompose 对逐回调
 *          NULL 判空（XFontFt.c 轮廓分发 wrapper），全空 sink 安全。
 *          位图 face 走 LoadBitmapGlyph 槽：out=NULL/outSize=0 是
 *          合法的「只查 dsc」形态（XFontBitmapFace.c VXFontBitmapFace_
 *          loadGlyph 入参契约），内置 provider 带 cmap
 *          （XFontBitmapFace_findGlyph 失败即 false）。
 * @return face 可绘制 codepoint 返回 true；face 缺字形/种类未知返回 false。 */
static bool xfcb_glyphPresent(const XFontFace* face, const XFont* font,
                              uint32_t codepoint)
{
    if (!face || !font) return false;
    if (XFontFace_kind(face) == XFontFace_Outline)
    {
        XFontOutlineGlyphMetrics metrics;
        XFontOutlineSink sink;
        XMemset(&sink, 0, sizeof(sink)); /* 全 NULL 回调：分发器逐个判空。 */
        return XFontFace_loadOutlineGlyph_base(face, font, codepoint,
                                               &metrics, &sink);
    }
    if (XFontFace_kind(face) == XFontFace_Bitmap)
    {
        XFontGlyphDsc dsc;
        return XFontFace_loadBitmapGlyph_base(face, font, codepoint, &dsc,
                                              NULL, 0);
    }
    return false;
}

/** @brief 降级档族标注表（与 xfcb_FcFamilyEntry 同构的局部快照）。 */
typedef struct xfcb_GlyphFamilyEntry
{
    char m_family[128];                /**< 族名（截断保护）。 */
    uint32_t m_systems[(32 / 32) + 1]; /**< 支持位集：bit(system)。 */
    bool m_other;                      /**< 全已知系统都不中（Symbol=Other）。 */
} xfcb_GlyphFamilyEntry;

/** @brief 一次降档探测的结果表（栈上值对象，用后释放）。 */
typedef struct xfcb_GlyphFamilies
{
    xfcb_GlyphFamilyEntry* m_entries;  /**< 条目数组（堆；release 释放）。 */
    int m_count;
    int m_capacity;
} xfcb_GlyphFamilies;

/** @brief 该族支持某书写系统返回 true（查已建好的表；语义同
 *         xfcb_fcFamiliesSupports：表未命中/回落替身族 ⇒ 保守判不支持）。 */
static bool xfcb_glyphFamiliesSupports(const xfcb_GlyphFamilies* table,
                                       const char* family, int system)
{
    int i;
    if (!table || !table->m_entries || !family || !family[0]) return false;
    for (i = 0; i < table->m_count; ++i)
    {
        if (XStrcmp(table->m_entries[i].m_family, family) == 0)
        {
            if (system == (int)XFontComboBoxWritingSystem_Symbol)
                return table->m_entries[i].m_other;
            if (system < 0 || system >= 32) return false;
            return (table->m_entries[i].m_systems[system / 32] &
                    ((uint32_t)1u << (system % 32))) != 0u;
        }
    }
    return false;
}

/** @brief 释放降档探测表。 */
static void xfcb_glyphFamiliesRelease(xfcb_GlyphFamilies* table)
{
    if (!table) return;
    if (table->m_entries)
        XMemory_free(table->m_entries, XFCB_TABLE_MEMORY);
    table->m_entries = NULL;
    table->m_count = 0;
    table->m_capacity = 0;
}

/** @brief 降档表按族名找条目（返回可写指针；未命中 NULL）。 */
static xfcb_GlyphFamilyEntry* xfcb_glyphFamiliesFind(xfcb_GlyphFamilies* table,
                                                     const char* family)
{
    int i;
    if (!table || !table->m_entries || !family) return NULL;
    for (i = 0; i < table->m_count; ++i)
        if (XStrcmp(table->m_entries[i].m_family, family) == 0)
            return &table->m_entries[i];
    return NULL;
}

/** @brief 单族探测并并入标注表（两处 builder 共用的逐族主体）。
 * @details 每族一次 XFont_face 解析（与渲染路径同参：12pt/PreferDefault，
 *          同 xfcb_familyIsFixedPitch 的构造形态）：
 *          - 解析不到**自有** face（resolved m_family ≠ 查询族名）⇒
 *            族记入表但不置任何位（含 Other）——「无标注」族在任何
 *            书写系统（含 Symbol）下都不入选；
 *          - 自有 face ⇒ 逐受理系统探测代表码位，命中置位；15 个受理
 *            系统全不中 ⇒ 置 Other 位（fc 档「langset 无任何已知语言」
 *            的子集级近似：本类枚举外的 script（如 Georgian）不参与
 *            「已知」判定，登记窄口径）。
 *          探测只读，不改解析器全局状态（XFont_face 的会话 memo/
 *          家族槽按既有语义工作；文件探测退避窗口的档内干扰与
 *          fontFilters 判定的既有形态一致）。 */
static void xfcb_glyphProbeAppend(xfcb_GlyphFamilies* table,
                                  const char* family)
{
    XFont font;
    const XFontFace* face;
    bool own;
    bool known = false;
    int j;
    xfcb_GlyphFamilyEntry* entry;
    if (!table || !family || !family[0]) return;
    entry = xfcb_glyphFamiliesFind(table, family);
    if (!entry)
    {
        if (table->m_count >= table->m_capacity)
        {
            int newCap = table->m_capacity > 0 ? table->m_capacity * 2 : 64;
            xfcb_GlyphFamilyEntry* fresh =
                (xfcb_GlyphFamilyEntry*)XMemory_calloc(
                    (size_t)newCap, sizeof(xfcb_GlyphFamilyEntry),
                    XFCB_TABLE_MEMORY);
            if (!fresh) return;
            if (table->m_entries)
            {
                XMemcpy(fresh, table->m_entries,
                        (size_t)table->m_count *
                            sizeof(xfcb_GlyphFamilyEntry));
                XMemory_free(table->m_entries, XFCB_TABLE_MEMORY);
            }
            table->m_entries = fresh;
            table->m_capacity = newCap;
        }
        entry = &table->m_entries[table->m_count];
        XMemset(entry, 0, sizeof(*entry));
        XStrncpy(entry->m_family, family, sizeof(entry->m_family) - 1);
        ++table->m_count;
    }
    XFont_init_ex(&font, family, 12, -1, false);
    face = XFont_face(&font);
    /* 自有 face 判定：resolved face 的自有族名必须与查询族名一致；
     * 不一致 = 解析器把该族静默回落到了替身 face（XFontFace.c:384
     * XFontFace_first），替身的覆盖与本族无关 ⇒ 不产生任何标注。 */
    own = (face && face->m_family &&
           XStrcmp(face->m_family, family) == 0);
    if (own)
    {
        for (j = 1; j < 32; ++j)
        {
            uint32_t cp = xfcb_scriptSample(j);
            if (!cp) continue;
            if (xfcb_glyphPresent(face, &font, cp))
            {
                entry->m_systems[j / 32] |= ((uint32_t)1u << (j % 32));
                known = true;
            }
        }
    }
    XClassDeinit((XClass*)&font);
    /* 受理系统全不中（含回落替身族）⇒ Other 位仅对「自有 face 全
     * 不中」的族置位；替身族不置 Other（无标注 ≠ 确知无 script，
     * 与 fc 档「表里没有该族」同判：Symbol 也不入选）。 */
    if (own && !known) entry->m_other = true;
}

/** @brief 逐族探测平台族快照的真实字形覆盖（族列表路径）。
 * @param table 出参（先前内容释放后重建）。
 * @param families 平台字库库的族名向量（元素 XString*，借用）。
 * @return 探测完成（即使 0 族）返回 true。
 */
static bool xfcb_glyphFamiliesBuild(xfcb_GlyphFamilies* table,
                                    const XVector* families)
{
    int64_t i;
    int64_t n;
    if (!table) return false;
    xfcb_glyphFamiliesRelease(table);
    if (!families) return true;
    n = XVector_size_base((const XContainer*)families);
    for (i = 0; i < n; ++i)
    {
        XString** name = (XString**)XVector_at_base(families, i);
        if (!name || !*name) continue;
        xfcb_glyphProbeAppend(table, XString_toUtf8(*name));
    }
    return true;
}

/** @brief 无库回退族集的标注表（平台快照缺失时，兜底四族照常参与
 *         书写系统过滤）。 */
static bool xfcb_glyphFamiliesBuildFallback(xfcb_GlyphFamilies* table)
{
    size_t k;
    if (!table) return false;
    xfcb_glyphFamiliesRelease(table);
    for (k = 0;
         k < sizeof(xfcb_kFallbackFamilies) / sizeof(xfcb_kFallbackFamilies[0]);
         ++k)
        xfcb_glyphProbeAppend(table, xfcb_kFallbackFamilies[k]);
    return true;
}
#endif /* !XFCB_HAS_FONTCONFIG */

/** @brief fontFilters 是否存在「生效的过滤位」（updateModel 掩码语义）。
 * @details Qt 原文 `(filters & mask) && (filters & mask) != mask`：
 *          Scalable|NonScalable、Monospaced|Proportional 两对互斥掩码
 *          **恰好置一位**才构成过滤，两位同置互相抵消 ≡ 未过滤。兜底
 *          闸据此判「未过滤」——降档无库构建里矛盾组合若按 `filters==0`
 *          字面判，兜底四族会被清成空列表，偏离 Qt 的「矛盾组合＝全量」
 *          实测口径。 */
static bool xfcb_filtersActive(int f)
{
    const int scalableMask = (int)XFontComboBoxFilter_ScalableFonts |
                             (int)XFontComboBoxFilter_NonScalableFonts;
    const int spacingMask = (int)XFontComboBoxFilter_ProportionalFonts |
                            (int)XFontComboBoxFilter_MonospacedFonts;
    return ((f & scalableMask) && (f & scalableMask) != scalableMask) ||
           ((f & spacingMask) && (f & spacingMask) != spacingMask);
}

/* ===== 书写系统逐族标注：两档同构（B11 返工 2026-11-15） =====
 * fontconfig 档：FC_LANG langset（Qt 同源同值，见上方 fc 段）。
 * 无 fontconfig 档（Windows GDI / 嵌入式 / 裁剪构建）：按**框架字库的
 * 真实字形覆盖**逐族判定——不是编译期常量，也不是上一版「只存值不
 * 过滤」的门控，更不是曾被打回的「逐族量回落 face」常量探测：
 * ① **自有 face 判定**：XFont_face 解析该族，取 resolved face 的
 *    m_family（face 的自有族名，XFontFace.h:217 公共字段）与查询族名
 *    精确比对——不一致即「回落替身」（XFontFace.c:384
 *    `return XFontFace_first(...)` 会把解析不到的族静默落到第一张注册
 *    face）。回落替身的族**无标注**，任何书写系统（含 Symbol）都不
 *    入选——与 fc 档「标注表里没有该族 ⇒ 保守判不支持」同口径
 *    （xfcb_fcFamiliesSupports 的表未命中分支）。
 * ② **字形存在性探测**：自有 face 逐书写系统探测 Qt 各 script 的代表
 *    码位（xfcb_scriptSample，索引集对标 Qt 6.8
 *    qfontdatabase.cpp:349-386 的 scriptForWritingSystem[]：WS→script
 *    映射）。判定原语与渲染路径**同源**：FT 槽字形装载在
 *    `FT_Get_Char_Index == 0`（cmap 无映射）时返回 false
 *    （XFontFt.c XFontFt_slotLoadGlyph 缺字形分支），位图 face 走
 *    cmap 查找（XFontBitmapFace_findGlyph 失败即 false）——量的是
 *    「该族真实能画出什么」，不是字库元数据声明。
 * **覆盖面窄于 Qt 是登记的平台限制**（非 bug）：Qt 的 FT 后端从 OS/2
 * ulUnicodeRange 位集换算书写系统（qfreetypefontdatabase.cpp:237-262 +
 * qplatformfontdatabase.cpp:522 writingSystemsFromTrueTypeBits），fc 后
 * 端从 FC_LANG 换算；本框架 XFontFt 不暴露 OS/2 位集，降档只有字形
 * 存在性一个真实信号，且代表码位每系统只测 1 个（CJK 三系统共用汉字
 * 区，SC/TC/JP 的区分是近似——「漢/あ/中」三码位，登记 openNotes）。
 * 真正的等 Qt 口径仍需「族名 → 系统字体文件路径」的平台映射专项
 * （Win32 GDI / Android Typeface），见 DomainResult openNotes。 */


/** @brief 重建期使用的组合过滤判定（书写系统 + fontFilters）。
 * @details 逐条对标 Qt QFontComboBoxPrivate::updateModel 的过滤体
 *          （qfontcombobox.cpp:297-310 原文）：
 *          - 书写系统先判：Qt 侧 `QFontDatabase::families(system)` 的
 *            入参，先于 filters 生效。两档都有此条件——fontconfig 档查
 *            FC_LANG 标注表，无 fontconfig 档查字形覆盖探测表（见
 *            上方「两档同构」注记）；表未命中（table NULL 或族无标注）
 *            一律保守判不支持。
 *          - fontFilters 按掩码语义：`scalableMask`（Scalable|NonScalable）
 *            与 `spacingMask`（Proportional|Monospaced）**恰好置一位**时
 *            才按该位过滤——两位同置互相抵消（Qt 原文
 *            `(filters & mask) && (filters & mask) != mask` 本机探针
 *            实测：矛盾位组合列表不动、count 保持 276）。
 * @param self 目标控件（读 m_filters）。
 * @param family 族名（UTF-8）。
 * @param table 书写系统支持位集表（两档各自的标注表；未建时 NULL）。
 * @return 该族应入选返回 true。
 */
static bool xfcb_familyAllowedWithTable(const XFontComboBox* self,
                                        const char* family,
                                        const void* table)
{
    const int scalableMask = (int)XFontComboBoxFilter_ScalableFonts |
                             (int)XFontComboBoxFilter_NonScalableFonts;
    const int spacingMask = (int)XFontComboBoxFilter_ProportionalFonts |
                            (int)XFontComboBoxFilter_MonospacedFonts;
    int f;
    int system;
    if (!self) return true;
    system = (int)self->m_writingSystem;
    if (system != (int)XFontComboBoxWritingSystem_Any &&
        xfcb_isValidWritingSystem(system) && family && family[0])
    {
#if XFCB_HAS_FONTCONFIG
        if (!xfcb_fcFamiliesSupports((const xfcb_FcFamilies*)table, family,
                                     system))
            return false;
#else
        if (!xfcb_glyphFamiliesSupports((const xfcb_GlyphFamilies*)table,
                                        family, system))
            return false;
#endif
    }
    f = self->m_filters;
    /* Qt 掩码语义：两位恰好置一位才过滤；同置（或全 0）不过滤。 */
    if ((f & scalableMask) && (f & scalableMask) != scalableMask)
    {
        if (((f & (int)XFontComboBoxFilter_ScalableFonts) != 0) !=
            xfcb_familyIsScalable(family))
            return false;
    }
    if ((f & spacingMask) && (f & spacingMask) != spacingMask)
    {
        if (((f & (int)XFontComboBoxFilter_MonospacedFonts) != 0) !=
            xfcb_familyIsFixedPitch(family))
            return false;
    }
    return true;
}

/** @brief 按平台字库库 + 书写系统/fontFilters 过滤重填族列表。
 * @param self 目标控件。
 * @param selectFirst true=重填后把当前项置 0（构造期首次填充口径）；
 *        false=不碰当前项（rebuild 路径：挡外按 Qt updateModel 的
 *        offset 收敛，见 xfcb_rebuild）。
 */
static void xfcb_populate(XFontComboBox* self, bool selectFirst)
{
#if XPLATFORMFONTDATABASE_ON
    XPlatformFontDatabase* db;
    XVector* families = NULL;
    int64_t i;
    int64_t n;
#if XFCB_HAS_FONTCONFIG
    /* [B11] fontconfig 书写系统标注：**一次枚举**成全表（逐族查表，避免
     * 每族各走一趟 FcFontList——本机 251 族实测逐族版单次重建 15ms）。
     * 只在「书写系统不是 Any」时才建表（Any 不过滤，省一趟枚举）。 */
    xfcb_FcFamilies fcTable;
    bool hasTable = false;
    const void* annotTable = NULL;
    XMemset(&fcTable, 0, sizeof(fcTable));
#else
    /* 无 fontconfig 档：按框架字库**真实字形覆盖**逐族探测成标注表
     * （B11 返工：不再是「只存值不过滤」的门控，也不允许退化为
     * 「逐族量同一张回落 face」的常量函数——自有 face 判定 + 字形
     * 存在性探测见「两档同构」注记）。同样只在非 Any 时建表。 */
    xfcb_GlyphFamilies glyphTable;
    bool hasTable = false;
    const void* annotTable = NULL;
    XMemset(&glyphTable, 0, sizeof(glyphTable));
#endif
    if (!self) return;
    db = XPlatformFontDatabase_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!db) return;
    if (XPlatformFontDatabase_isValid(db))
        families = XPlatformFontDatabase_families(db);
#if XFCB_HAS_FONTCONFIG
    if (self->m_writingSystem != (int)XFontComboBoxWritingSystem_Any)
    {
        hasTable = xfcb_fcFamiliesBuild(&fcTable);
        annotTable = hasTable ? (const void*)&fcTable : NULL;
    }
#else
    if (self->m_writingSystem != (int)XFontComboBoxWritingSystem_Any)
    {
        /* 降档：标注表建在「将要列入的族集」上——平台快照有族用快照；
         * 快照缺失（无库构建）用兜底四族建表（兜底族也照常参与书写
         * 系统过滤，见 xfcb_glyphFamiliesBuildFallback 注）。 */
        hasTable = families
            ? xfcb_glyphFamiliesBuild(&glyphTable, families)
            : xfcb_glyphFamiliesBuildFallback(&glyphTable);
        annotTable = hasTable ? (const void*)&glyphTable : NULL;
    }
#endif
    if (families) {
        n = XVector_size_base((const XContainer*)families);
        for (i = 0; i < n; ++i) {
            XString** name = (XString**)XVector_at_base(families, i);
            if (name && *name) {
                /* 按书写系统 + fontFilters 过滤（Qt updateModel：
                 * families(system) + 条件过滤）。 */
                if (xfcb_familyAllowedWithTable(
                        self, XString_toUtf8(*name), annotTable))
                    XComboBox_addItem((XComboBox*)self, *name);
                XClassDelete(*name);
                *name = NULL;
            }
        }
        XClassDelete(families);
        families = NULL;
    }
    else if (xfcb_filtersActive(self->m_filters) ||
             self->m_writingSystem !=
                 (int)XFontComboBoxWritingSystem_Any)
    {
        /* 无库构建（平台快照缺失）的**过滤路径**：兜底族集逐条过同一
           判定（标注表已按兜底族集建好，见 xfcb_glyphFamiliesBuild
           的降档分支与 xfcb_glyphFamiliesBuildFallback），命中才入列
           ——Qt「过滤后为空即空」不变式在无库环境同样成立
           （qfontcombobox.cpp:363-368）。此前兜底四族只走下方无条件
           兜底闸，任何过滤态下列表直接清空（B11 返工补的缺口）。 */
        size_t k;
        for (k = 0; k < sizeof(xfcb_kFallbackFamilies) /
                            sizeof(xfcb_kFallbackFamilies[0]); ++k)
            if (xfcb_familyAllowedWithTable(
                    self, xfcb_kFallbackFamilies[k], annotTable))
                XComboBox_addItem_2((XComboBox*)self,
                                    xfcb_kFallbackFamilies[k]);
    }
    XPlatformFontDatabase_destroy(db);
#if XFCB_HAS_FONTCONFIG
    /* 枚举表是本次重建的局部快照：条目已按它筛完，此处释放。 */
    xfcb_fcFamiliesRelease(&fcTable);
#else
    /* 降档探测表同口径：本次重建的局部快照，用后即释放。 */
    xfcb_glyphFamiliesRelease(&glyphTable);
#endif
#endif /* XPLATFORMFONTDATABASE_ON */
    /* 回退：数据库未启用或无字体族（且**无生效过滤条件**）时补默认条目
       确保控件非空。设了过滤（fontFilters 或书写系统）却一条都不匹配时
       保持空列表——不掺入与过滤条件不符的假条目，对标 Qt「过滤后为空
       即空」（qfontcombobox.cpp:363-368：result 为空即
       currentFont=QFont()，模型清空，无任何兜底族）。
       「无生效过滤条件」按 updateModel 的掩码语义算（Qt 原文
       `(filters & mask) && (filters & mask) != mask`）：矛盾位组合
       （Scalable|NonScalable 等）的过滤体被跳过 ≡ 未过滤（本机 Qt 6.8
       探针实测矛盾组合 count 保持全量）⇒ 兜底照常可用；B11 返工前该
       闸按 `m_filters == 0` 字面判断，降档构建里矛盾组合会把兜底四族
       一并清成空列表（≠ Qt 的「矛盾组合＝全量」实测口径）。 */
    if (XComboBox_count((XComboBox*)self) == 0 &&
        !xfcb_filtersActive(self->m_filters) &&
        self->m_writingSystem == (int)XFontComboBoxWritingSystem_Any) {
        size_t k;
        for (k = 0; k < sizeof(xfcb_kFallbackFamilies) /
                            sizeof(xfcb_kFallbackFamilies[0]); ++k)
            XComboBox_addItem_2((XComboBox*)self, xfcb_kFallbackFamilies[k]);
    }
    if (selectFirst && XComboBox_count((XComboBox*)self) > 0)
        XComboBox_setCurrentIndex((XComboBox*)self, 0);
}

/* [P3-62] 前向声明：xfcb_rebuild 末尾重放逐族显示字体（定义在下方）。 */
static void xfcb_replayDisplayFonts(XFontComboBox* self, bool createView);

/** @brief 重建字体族列表，并按 Qt updateModel 口径收敛当前项与信号。
 * @details 对标 QFontComboBoxPrivate::updateModel
 *          （qfontcombobox.cpp:286-355，本机 Qt 6.8 探针实测序列佐证）：
 *          - **信号序列**：Qt 重建全程至多一组当前项信号——实测
 *            setWritingSystem(Cyrillic)（当前族两列表都入选、行号 5→4）
 *            只发 1 次 currentIndexChanged，族名未变 ⇒ textChanged/
 *            currentFontChanged 零发；绝无 -1/空串中间态发射。
 *            本实现用 `XObject_blockSignals` 包住 clear+populate（对标
 *            Qt 的 `QSignalBlocker blocker(m)`：挡住模型重置期间的全部
 *            当前项信号），挡外**只做一次** `setCurrentIndex(offset)`。
 *          - **offset**＝当前字体族在**新列表**里的行号（Qt 用
 *            `family == fi.family()` 匹配；本库先把当前条目族名拷成
 *            自有缓冲（`kept`）当等价键，再在新列表里按
 *            `xfcb_findFamilyIndex` 的精确/大小写不敏感两档找回）。
 *            挡内 populate(selectFirst=false) 不设当前项 ⇒ 挡外
 *            setCurrentIndex 从 clear 落下的 -1 起跳，与 Qt「整表替换
 *            后 currentIndex 内部复位、setCurrentIndex(offset) 恒发
 *            一组信号」的实测行为一致（矛盾位探针：同族名也发 1 次
 *            indexChanged）。
 *          - 列表为空 ⇒ 不设当前项（currentIndex 停 -1）；记忆族名
 *            非空时清记忆并**显式发一次**空族名 currentFontChanged
 *            （对标 qfontcombobox.cpp:347-352 `currentFont = QFont();
 *            emit currentFontChanged(...)`；不经转发槽——槽的
 *            「count==0 不发空族名」闸只管文本变化路径）。
 *          「仍入选则保持（行号跟随），否则回落首项」——本机 Qt 6.8
 *          探针实测：当前族 `Noto Sans SignWriting` 在等宽过滤后仍入选
 *          且行号 158→7，`currentText` 保持；当前族 `Bitstream Charter`
 *          被等宽过滤剔除时 `currentText` 回落到新列表首项。
 * @param self 目标控件。
 * @note  `kept[256]` 对超长族名（>255 字节）静默截断 ⇒ 找不回 ⇒ 回落
 *        首项（Qt 无此限制；框架字库实际族名远短于此，见 DomainResult）。
 */
static void xfcb_rebuild(XFontComboBox* self)
{
    /* 族名快照：XComboBox_clear 会释放条目 XString，借用指针必须提前拷走。 */
    char kept[256];
    bool blocked;
    if (!self) return;
    kept[0] = '\0';
    {
        const char* current = XComboBox_currentText_2((const XComboBox*)self);
        if (current && current[0])
            XStrncpy(kept, current, sizeof(kept) - 1);
    }
    /* 挡住重建中间态的全部信号（Qt QSignalBlocker 同款语义；布尔位、
     * 返回旧值，见 XObject.h:314-328）。 */
    blocked = XObject_blockSignals((XObject*)self, true);
    XComboBox_clear((XComboBox*)self);
    xfcb_populate(self, false);
    XObject_blockSignals((XObject*)self, blocked);
    if (XComboBox_count((XComboBox*)self) > 0)
    {
        int target = 0;
        if (kept[0])
        {
            int keptIndex = xfcb_findFamilyIndex(self, kept, false);
            if (keptIndex >= 0) target = keptIndex; /* 未命中回落首项。 */
        }
        /* 挡内 addItem 对首条自动置当前项 0 ⇒ 挡外 setCurrentIndex(target)
         * 同值早退会零发射，转发槽没机会把 m_currentFont 对齐到新列表
         * （记忆可能与当前项脱节）。先**静默**复位到 -1（直写内部索引，
         * 不发射；同文件族内字段可见），再走 setCurrentIndex——从 -1
         * 起跳恒发一组 indexChanged+textChanged，槽随之把记忆收敛到
         * 当前项。对齐 Qt 实测：整表替换后 setCurrentIndex(offset) 恒发
         * 一次 indexChanged（矛盾位探针：同族名也发）。 */
        ((XComboBox*)self)->m_currentIndex = -1;
        XComboBox_setCurrentIndex((XComboBox*)self, target);
        /* textChanged 的载荷恒为当前项文本（xcombo_changeCurrentIndex
         * 发射点不做同值判断——XComboBox 既有语义），族名未变时
         * currentFontChanged 由转发槽的**记忆比对**去重为零发，与 Qt
         * 「族名不变 ⇒ fontChanged 零发」一致（Qt 的 currentTextChanged
         * 同文本不发射，此差异归 XComboBox 发射点，非本域）。 */
    }
    else if (self->m_currentFont)
    {
        /* 空列表：Qt 把 currentFont 收敛为 QFont() 并显式发一次
         * （qfontcombobox.cpp:347-352）。记忆**可能已是空串**（上次空
         * 列表分支 assign 过），XString_toUtf8 对空串返回 NULL——必须
         * 先取借用再判，不得对 toUtf8 结果直接下标（SEGV 修复
         * 2026-11-15，见 repro5 时序）。 */
        const char* remembered = XString_toUtf8(self->m_currentFont);
        if (remembered && remembered[0] != '\0')
        {
            XString_assign_utf8(self->m_currentFont, "");
            xfcb_emitText(self,
                          (size_t)XFontComboBox_currentFontChanged_signal(
                              self, ""),
                          "");
        }
        /* 行编辑残留收敛（B11 返工：清空列表后的「当前项处理明确」）：
         * XComboBox_clear 不清内嵌编辑框，可编辑框残留的旧族名会让
         * currentText_2 走分叉态返回残留文本 ⇒ currentFamily/currentFont
         * 访问器与刚发射的空族名载荷自相矛盾，且下一次重建的 kept 快照
         * 会拿到残留族名。QComboBox::clear 口径（qcombobox.cpp clear()
         * 对可编辑框一并 clearEditText）：列表清空 ⇒ 编辑框归空。此处的
         * XLineEdit_setText 只发 editTextChanged（编辑联动信号），不再
         * 触发 currentFontChanged（记忆已清，转发槽亦被 count==0 闸
         * 挡住），信号序列仍与 Qt 探针实测一致（空态 fontChanged 恰 1
         * 次）。 */
        if (XComboBox_count((XComboBox*)self) == 0 && self->m_base.m_editable &&
            self->m_base.m_lineEdit)
        {
            const char* edit = XLineEdit_text(self->m_base.m_lineEdit);
            if (edit && edit[0])
                XComboBox_setEditText_2((XComboBox*)self, "");
        }
    }
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
    /* 槽内回落基类口径走 XClass_Parent（查基类虚表槽，不经对象虚表；
     * 2026-11-14 契约：XComboBox_popupWidth_base 已是分派入口，槽内直调
     * 会经本对象虚表自递归）。 */
    int w = XClass_Parent(XComboBox, EXComboBox_PopupWidth,
                          int (*)(const XComboBox*))(self);
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
    /* 记忆**可能已是空串 XString**（空列表分支 assign("") 后 XString 的
     * 内码被清空），XString_toUtf8 对空串返回 NULL（XString.c 空串早退）
     * ——XStrcmp 无 NULL 守卫，借用必须先归一为 ""（SEGV 修复
     * 2026-11-15，repro5 时序：Mono 过滤空列表 → 记忆清空 → 恢复 AllFonts
     * 首次发射 XStrcmp(NULL) 崩）。 */
    remembered = (self->m_currentFont && XString_toUtf8(self->m_currentFont))
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
    xfcb_populate(self, true);
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
     * （QFontComboBoxPrivate::updateModel，qfontcombobox.cpp:286-355）；
     * 此前仅存状态、列表不重建。
     * [B11] 当前项按 Qt updateModel 口径收敛（仍入选则保持、否则回落
     * 首项），信号序列见 xfcb_rebuild 的 @details。
     * 注：两档构建都跑重建——fontFilters 判定（scalable/fixedPitch）
     * 两档同参；书写系统条件两档也都有（fc=FC_LANG 表，降档=字形覆盖
     * 表，见 xfcb_familyAllowedWithTable）。 */
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
    /* 对标 QFontComboBox::setWritingSystem（qfontcombobox.cpp:442-449）：
     * 写入后立即 updateModel 重填族列表。当前项按 Qt updateModel 口径
     * 收敛（仍入选则保持、否则回落首项），信号序列见 xfcb_rebuild 的
     * @details。**两档构建都重建**（B11 返工）：fontconfig 档按 FC_LANG
     * 标注过滤；无 fontconfig 档按框架字库真实字形覆盖探测过滤（逐族
     * 自有 face 判定 + 代表码位字形存在性，见「两档同构」注记）——该档
     * 覆盖面窄于 Qt（无 OS/2 位集/FC_LANG 信号）是登记的平台限制，
     * 不是不过滤的理由。 */
    xfcb_rebuild(self);
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
 * @details 回填文本取**命中条目自身文本**（XComboBox_itemText_2）而非
 *          调用方传入的族名：setCurrentFamily 的大小写不敏感命中
 *          （"dejavu sans" → 条目 "DejaVu Serif"）与 ③ 分支的控件字体
 *          族解析（未知族 → 应用缺省族，Qt QFontInfo 口径）里，条目
 *          文本才是本族的标准形态；回填调用方原文会把未命中/小写输入
 *          原样写回编辑框（对标 Qt setCurrentFont 后 currentText 收敛
 *          为规范族名的探针实测）。
 * @param self 目标控件。
 * @param i 命中索引。
 * @return 无。 */
static void xfcb_selectFamilyIndex(XFontComboBox* self, int i)
{
    if (XComboBox_currentIndex((XComboBox*)self) == i) {
        /* 同行号但编辑文本已分叉（用户键入未采纳）时回填收敛：
           setCurrentIndex 去重不发射，此处仅收敛显示文本。 */
        const char* itemText = XComboBox_itemText_2((XComboBox*)self, i);
        const char* edit = XComboBox_lineEdit((XComboBox*)self)
            ? XLineEdit_text(XComboBox_lineEdit((XComboBox*)self))
            : NULL;
        if (edit && itemText && itemText[0] &&
            XStrcmp(edit, itemText) != 0)
            XComboBox_setEditText_2((XComboBox*)self, itemText);
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
        xfcb_selectFamilyIndex(self, idx);
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
            xfcb_selectFamilyIndex(self, widx);
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
