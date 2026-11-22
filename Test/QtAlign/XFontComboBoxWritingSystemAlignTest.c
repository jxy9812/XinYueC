/**
 * @file       XFontComboBoxWritingSystemAlignTest.c
 * @brief      XFontComboBox × Qt 6.8「书写系统过滤」对齐契约测试（§B11）。
 * @details    对齐依据（Qt 6.8.3 源码 + 本机 Qt 6.8.0 offscreen 探针）：
 *             - `qfontcombobox.cpp:442-449` `setWritingSystem`：
 *               写进 delegate 的 writingSystem 后**立即** `d->updateModel()`；
 *             - `qfontcombobox.cpp:286-370` `updateModel`：
 *               `QFontDatabase::families(system)` → 逐族按 filters 过滤 →
 *               重建模型 → 列表非空 `setCurrentIndex(offset)`（offset ≡ 当前
 *               族在新列表里的行号，缺省 0）、列表为空不动当前项；
 *             - `qfontdatabase.cpp:1433-1465` `families(writingSystem)`：
 *               `Any` 不过滤；否则逐族 `f->writingSystems[ws] == Supported`；
 *             - `qfontdatabase.h:56` `Other = Symbol`（本类 Symbol 即 Qt 的
 *               Other）；
 *             - `qfontconfigdatabase.cpp:264-300` `languageForWritingSystem[]`
 *               （逐书写系统的 FC_LANG 语言标签）与 `:433-465` 的判定
 *               （`FcLangSetHasLang(...) != FcLangDifferentLang`；无已知标签
 *               ⇒ Other）。
 *
 *             本机 Qt 6.8.0 探针实测（offscreen，2026-11-15）：
 *             `QFontDatabase::families(ws)` 计数 Any=276 / Latin=79 /
 *             Cyrillic=75 / Arabic=7 / SC=33 / JP=28；剥 foundry 后缀后
 *             唯一族名 Any=264 / Latin=67 / Cyrillic=65 / Arabic=7 / SC=33；
 *             `setWritingSystem(SimplifiedChinese)` 后 count 276→33；
 *             `setWritingSystem(Ogham)`（本机零族）后 count=0、currentIndex=-1；
 *             当前族在新列表仍入选时保持（`Noto Sans SignWriting` 158→7），
 *             被剔除时回落首项。
 *
 *             **环境口径（断言必须与它一致，否则在本机/CI 上二选一必红）**：
 *             本库族列表来自 `XPlatformFontDatabase`（Linux 上是 fontconfig
 *             的全系统可缩放族；无 fontconfig 构建为平台后端/兜底四族）；
 *             书写系统标注两档都有真实数据源——**有 fontconfig 时**同源取
 *             `FC_LANG`（与上表逐值同口径），**无 fontconfig 时**按框架
 *             字库的真实字形覆盖逐族探测（自有 face 判定 + 各书写系统
 *             代表码位字形存在性；B11 返工落地，替换早前「只存值不过滤」
 *             的门控，也不允许退化回与族无关的常量探测）。故本文件的硬
 *             断言只锁「与编辑器无关的结构契约」（过滤后是原列表子集、
 *             Any 不过滤、空过滤不掺假族、当前项收敛规则），而「每族
 *             是否支持该脚本」的**语义**断言按当前编译环境分档：
 *             有 `XINYUE_C_HAS_FONTCONFIG` 时与 `FC_LANG` 独立复算对拍；
 *             否则与字形覆盖探测 oracle 对拍 + 非常量硬锚（见测试⑦）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON

#include "XFontComboBox.h"
#include "XComboBox.h"
#include "XLineEdit.h"
#include "XObject.h"
#include "XVarList.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XFont.h"
#include "XFontFace.h"
#include "XClass.h"

/* 与实现同一份环境判据：有 fontconfig 才有 FC_LANG 标注数据。
 * （实现侧见 XFontComboBox.c 的 XFCB_HAS_FONTCONFIG。） */
#if defined(XINYUE_C_HAS_FONTCONFIG) && defined(__linux__)
#include <fontconfig/fontconfig.h>
#define XFCB_TEST_HAS_FONTCONFIG 1
#else
#define XFCB_TEST_HAS_FONTCONFIG 0
#endif

/* ==================== 信号探针 ==================== */

/* 缓冲尺寸（探针与快照共用；族名 >191 字节截断——实际系统族名远短于此）。 */
#define XFCBWS_MAX_ITEMS 4096
#define XFCBWS_NAME_MAX  192

static int fcbws_fontChanged = 0; /**< currentFontChanged 次数。 */
static int fcbws_indexChanged = 0; /**< currentIndexChanged 次数。 */
static int fcbws_textChanged = 0;  /**< currentTextChanged 次数。 */
static char fcbws_lastFontFamily[XFCBWS_NAME_MAX] = {0}; /**< 末次 fontChanged 载荷。 */
static char fcbws_lastText[XFCBWS_NAME_MAX] = {0};       /**< 末次 textChanged 载荷。 */

static void fcbws_fontChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_1(args, const char*, t);
        if (t) {
            XStrncpy(fcbws_lastFontFamily, t, XFCBWS_NAME_MAX - 1);
        } else {
            fcbws_lastFontFamily[0] = '\0';
        }
    }
    ++fcbws_fontChanged;
}

static void fcbws_indexChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++fcbws_indexChanged;
}

static void fcbws_textChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_1(args, const char*, t);
        if (t) {
            XStrncpy(fcbws_lastText, t, XFCBWS_NAME_MAX - 1);
        } else {
            fcbws_lastText[0] = '\0';
        }
    }
    ++fcbws_textChanged;
}

/* ==================== 工具 ==================== */

/** @brief 把组合框当前全部条目族名拷进静态缓冲（借用指针跨重建失效，
 *        断言需要跨重建比对。 */
static char s_names[XFCBWS_MAX_ITEMS][XFCBWS_NAME_MAX];
static int s_nameCount = 0;

/** @brief 快照当前条目族名到 s_names（超出上限截断）。 */
static void fcbws_snapshot(XComboBox* combo)
{
    int i;
    int n = XComboBox_count(combo);
    s_nameCount = 0;
    for (i = 0; i < n && i < XFCBWS_MAX_ITEMS; ++i) {
        const char* text = XComboBox_itemText_2(combo, i);
        if (!text) text = "";
        XStrncpy(s_names[s_nameCount], text, XFCBWS_NAME_MAX - 1);
        ++s_nameCount;
    }
}

/** @brief s_names 里是否有该族名（精确）。 */
static bool fcbws_snapshotHas(const char* family)
{
    int i;
    if (!family) return false;
    for (i = 0; i < s_nameCount; ++i)
        if (XStrcmp(s_names[i], family) == 0) return true;
    return false;
}

/* ============ 独立复算（两档各有真实数据源） ============ */
/* fontconfig 档：FC_LANG 独立枚举对拍（见下方 fc 段）。
 * 无 fontconfig 档：**逐族字形覆盖探测** oracle（fcbws_degradedSupports，
 * 见下方降档段）——B11 返工后实现侧是「自有 face 判定 + 各书写系统
 * 代表码位字形存在性」的真实判定（不再是与族无关的常量函数，也不是
 * 「只存值不过滤」的门控），oracle 与其同构对拍锁接线与过滤主体；
 * 该对拍的自洽局限（两侧同原语）由硬锚用例
 * writingSystemDegradedRealProbe 的「非常量」锁补位（见测试⑦）。 */

#if XFCB_TEST_HAS_FONTCONFIG
/* ===== 有 fontconfig 档：与实现**同源口径**的 FC_LANG 判定 ===== */
/* 局限声明（诚实口径，复核 2026-11-15）：本缓存的码位来源虽与实现独立
 * （直接 FcFontList+FcLangSetHasLang），但**语言标签表**（下方
 * fcbws_fontconfigLang / kFcbwsKnownLangs）与实现（xfcb_fontconfigLang /
 * kXfcbKnownLangs）同从 Qt 6.8 qfontconfigdatabase.cpp:264-300 抄录——
 * 表项抄错则两边同错、对拍仍绿。本对拍能抓的回归：没过滤/接线错/
 * 过滤器组合算错/实现枚举与判定脱节；抓不了的：两侧同一张表的共同
 * 抄录错。表项正确性由 Qt 探针计数（DomainResult 取证）背书，不在
 * 本文件内自证。
 * 口径陷阱（实测）：逐族 `FcFontList(FcPatternBuild(FC_FAMILY=x))` 一次
 * 3.5µs 且每次都分配/释放 fontconfig 内部结构——逐族对拍会打 4000+ 次，
 * 把进程堆 arena 搅成不稳定态，污染紧随其后的其它用例的泄漏门（本机
 * 实测：逐族版使 XIconTheme 四个用例从 [INIT] 变 [LEAK]）。故复算用
 * 「每个用例一次全量枚举 + 查表」，与实现同生产方式。 */

/** @brief 书写系统 → FC_LANG 语言标签（Qt languageForWritingSystem 抄录）。 */
static const char* fcbws_fontconfigLang(int system)
{
    switch (system) {
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
    default: return NULL;
    }
}

/** @brief Qt `languageForWritingSystem[]` 的全量表项（Other 判定用）。
 * @note  必须是**全量**非空项（qfontconfigdatabase.cpp:264-300），不能只列
 *        本类枚举的 15 个成员——Qt 的判定循环是全表（:442-455），按子集判
 *        会把只有 Malayalam/Kannada/Lao/Tibetan 等标签的族误算进 Other
 *        （本机实测差 24 族）。 */
static const char* const kFcbwsKnownLangs[] = {
    "en", "el", "ru", "hy", "he", "ar", "syr", "div", "hi", "bn", "pa",
    "gu", "or", "ta", "te", "kn", "ml", "si", "th", "lo", "bo", "my",
    "ka", "km", "zh-cn", "zh-tw", "ja", "ko", "vi", "sga", "non", "man"
};

/** @brief 逐族 FC_LANG 判定缓存（一次枚举建成，用例内反复查）。 */
#define XFCBWS_FC_MAX 4096
typedef struct fcbws_FcCacheEntry
{
    char m_family[128];
    bool m_other;                        /**< langset 无任何已知语言。 */
    bool m_system[32];                   /**< 支持位：按 Qt 语言表逐项判。 */
} fcbws_FcCacheEntry;

static fcbws_FcCacheEntry s_fcCache[XFCBWS_FC_MAX];
static int s_fcCacheCount = 0;

/** @brief 枚举一次 fontconfig 全部族，填充逐族语言判定缓存（只读，不 FcInit）。 */
static void fcbws_fcCacheBuild(void)
{
    FcPattern* pattern;
    FcObjectSet* objects;
    FcFontSet* set;
    FcConfig* config;
    int i;
    int j;
    s_fcCacheCount = 0;
    config = FcConfigGetCurrent();
    if (!config) return;
    pattern = FcPatternCreate();
    objects = FcObjectSetBuild(FC_FAMILY, FC_LANG, NULL);
    set = (pattern && objects) ? FcFontList(config, pattern, objects) : NULL;
    if (pattern) FcPatternDestroy(pattern);
    if (objects) FcObjectSetDestroy(objects);
    if (!set) return;
    for (i = 0; i < set->nfont && s_fcCacheCount < XFCBWS_FC_MAX; ++i) {
        FcChar8* fam = NULL;
        FcLangSet* langset = NULL;
        fcbws_FcCacheEntry* e;
        bool known = false;
        if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &fam) !=
                FcResultMatch || !fam || !fam[0])
            continue;
        /* 同名族的多个 face 取并集（与 Qt 按族登记 writingSystems 同）。 */
        e = NULL;
        for (j = 0; j < s_fcCacheCount; ++j)
            if (XStrcmp(s_fcCache[j].m_family, (const char*)fam) == 0)
                { e = &s_fcCache[j]; break; }
        if (!e) {
            e = &s_fcCache[s_fcCacheCount];
            XMemset(e, 0, sizeof(*e));
            XStrncpy(e->m_family, (const char*)fam, sizeof(e->m_family) - 1);
            /* m_other 缺省 false：**任一 face 无已知语言**就置 true（Qt 逐
             * pattern 登记，Other 是「该 pattern 无任何已知标签」；族级
             * 语义为「存在这样的 face」→ 该族在 Other 集合里）。 */
            e->m_other = false;
            ++s_fcCacheCount;
        }
        if (FcPatternGetLangSet(set->fonts[i], FC_LANG, 0, &langset) !=
                FcResultMatch || !langset) {
            e->m_other = true; /* 无 FC_LANG 的 face ⇒ Other（Qt 同）。 */
            continue;
        }
        for (j = 1; j < 32; ++j) {
            const char* lang = fcbws_fontconfigLang(j);
            if (!lang) continue;
            if (FcLangSetHasLang(langset, (const FcChar8*)lang) !=
                FcLangDifferentLang)
                e->m_system[j] = true;
        }
        for (j = 0; j < (int)(sizeof(kFcbwsKnownLangs) /
                              sizeof(kFcbwsKnownLangs[0])); ++j) {
            if (FcLangSetHasLang(langset,
                                 (const FcChar8*)kFcbwsKnownLangs[j]) !=
                FcLangDifferentLang) {
                known = true;
                break;
            }
        }
        if (!known) e->m_other = true;
    }
    FcFontSetDestroy(set);
}

/** @brief 查缓存：该族是否支持给定书写系统。 */
static bool fcbws_cacheSupports(const char* family, int system)
{
    int i;
    if (!family || !family[0]) return false;
    for (i = 0; i < s_fcCacheCount; ++i) {
        if (XStrcmp(s_fcCache[i].m_family, family) != 0) continue;
        if (system == (int)XFontComboBoxWritingSystem_Symbol)
            return s_fcCache[i].m_other;
        if (system < 0 || system >= 32) return false;
        return s_fcCache[i].m_system[system];
    }
    return false;
}

#else
/* ===== 无 fontconfig 档：逐族字形覆盖探测 oracle =====
 * 与实现同构（XFont_face + 自有 face 判定 + 代表码位探测），独立遍历：
 * 锁「过滤主体/接线/逐族独立」回归。自洽局限（两侧同原语）由测试⑦的
 * 非常量硬锚补位——实现侧口径见 XFontComboBox.c「两档同构」注：
 * ① 自有 face：resolved face->m_family == 查询族名（XFontFace.h:217
 *    公共字段）；回落替身（XFontFace.c:384 XFontFace_first）＝无标注；
 * ② 代表码位：Qt 6.8 qfontdatabase.cpp:349-386 scriptForWritingSystem
 *    的每系统一码位近似（与实现同一张表，抄录自同一依据）；
 * ③ 判定原语：轮廓 face 走 LoadOutlineGlyph 槽（FT 槽
 *    FT_Get_Char_Index==0 ⇒ false，渲染同源）；位图 face 走
 *    LoadBitmapGlyph 槽（out=NULL 纯 dsc 查询，cmap 查找失败即 false）；
 * ④ Symbol=Other：自有 face 全受理系统不中（fc Other 的子集级近似）。 */

/** @brief 书写系统 → 代表探测码位（与实现同一份抄录，逐值对齐 Qt）。 */
static uint32_t fcbws_scriptSample(int system)
{
    switch (system) {
    case (int)XFontComboBoxWritingSystem_Latin: return 0x41u;
    case (int)XFontComboBoxWritingSystem_Greek: return 0x0391u;
    case (int)XFontComboBoxWritingSystem_Cyrillic: return 0x0410u;
    case (int)XFontComboBoxWritingSystem_Armenian: return 0x0531u;
    case (int)XFontComboBoxWritingSystem_Hebrew: return 0x05D0u;
    case (int)XFontComboBoxWritingSystem_Arabic: return 0x0627u;
    case (int)XFontComboBoxWritingSystem_Devanagari: return 0x0905u;
    case (int)XFontComboBoxWritingSystem_Bengali: return 0x0985u;
    case (int)XFontComboBoxWritingSystem_Tamil: return 0x0B85u;
    case (int)XFontComboBoxWritingSystem_Thai: return 0x0E01u;
    case (int)XFontComboBoxWritingSystem_SimplifiedChinese: return 0x4E2Du;
    case (int)XFontComboBoxWritingSystem_TraditionalChinese: return 0x6F22u;
    case (int)XFontComboBoxWritingSystem_Japanese: return 0x3042u;
    case (int)XFontComboBoxWritingSystem_Korean: return 0xAC00u;
    case (int)XFontComboBoxWritingSystem_Vietnamese: return 0x1EA0u;
    default: return 0u;
    }
}

/** @brief 单码位字形存在性（与实现同一原语的独立调用）。 */
static bool fcbws_glyphPresent(const XFontFace* face, const XFont* font,
                               uint32_t codepoint)
{
    if (!face || !font) return false;
    if (XFontFace_kind(face) == XFontFace_Outline) {
        XFontOutlineGlyphMetrics metrics;
        XFontOutlineSink sink;
        XMemset(&sink, 0, sizeof(sink));
        return XFontFace_loadOutlineGlyph_base(face, font, codepoint,
                                               &metrics, &sink);
    }
    if (XFontFace_kind(face) == XFontFace_Bitmap) {
        XFontGlyphDsc dsc;
        return XFontFace_loadBitmapGlyph_base(face, font, codepoint, &dsc,
                                              NULL, 0);
    }
    return false;
}

/** @brief 该族解析到的 face 是否自有（而非回落替身）。 */
static bool fcbws_degradedOwnFace(const char* family)
{
    XFont font;
    const XFontFace* face;
    bool own;
    if (!family || !family[0]) return false;
    XFont_init_ex(&font, family, 12, -1, false);
    face = XFont_face(&font);
    own = (face && face->m_family &&
           XStrcmp(face->m_family, family) == 0);
    XClassDeinit((XClass*)&font);
    return own;
}

/** @brief 降档 oracle：该族是否支持给定书写系统（逐次独立解析，与实现
 *          的表式批量探测同语义）。 */
static bool fcbws_degradedFamilySupports(const char* family, int system)
{
    XFont font;
    const XFontFace* face;
    bool own;
    bool result = false;
    if (!family || !family[0]) return false;
    XFont_init_ex(&font, family, 12, -1, false);
    face = XFont_face(&font);
    own = (face && face->m_family &&
           XStrcmp(face->m_family, family) == 0);
    if (own) {
        if (system == (int)XFontComboBoxWritingSystem_Symbol) {
            /* Symbol=Other：自有 face 在全部受理系统上全不中
             * （fc 档「langset 无任何已知语言」的子集级近似）。 */
            int j;
            bool anyKnown = false;
            for (j = 1; j < 32 && !anyKnown; ++j) {
                uint32_t cp = fcbws_scriptSample(j);
                if (cp && fcbws_glyphPresent(face, &font, cp))
                    anyKnown = true;
            }
            result = !anyKnown;
        }
        else {
            uint32_t cp = fcbws_scriptSample(system);
            result = cp != 0u && fcbws_glyphPresent(face, &font, cp);
        }
    }
    /* 回落替身族：无标注 ⇒ 任何系统（含 Symbol）不入选（实现同口径）。 */
    XClassDeinit((XClass*)&font);
    return result;
}
#endif /* XFCB_TEST_HAS_FONTCONFIG */

/** @brief 用例入口：按档准备独立复算所需的环境快照。
 * @details 有 fontconfig 档枚举一次 FC_LANG 缓存（见上方「口径陷阱」注）；
 *          无 fontconfig 档无需预热（覆盖探测逐族解析 face，无全局状态）。
 *          每个用例开头调用一次——缓存是**用例级**快照，用例之间不共享，
 *          避免跨用例（尤其跨 FontComboBox 用例）的堆状态耦合。 */
static void fcbws_prepareOracle(void)
{
#if XFCB_TEST_HAS_FONTCONFIG
    fcbws_fcCacheBuild();
#endif
}

/** @brief 独立复算：该族在给定书写系统下应入选。 */
static bool fcbws_expectedAllowed(const char* family, int system)
{
    if (!family || !family[0]) return false;
    if (system == (int)XFontComboBoxWritingSystem_Any) return true;
#if XFCB_TEST_HAS_FONTCONFIG
    return fcbws_cacheSupports(family, system);
#else
    return fcbws_degradedFamilySupports(family, system);
#endif
}

/* ==================== ① Any 不过滤 + 过滤后是原列表子集 ============ */

XQ_TEST(XFontComboBox, writingSystemFiltersRebuildList)
{
    static const int systems[] = {
        (int)XFontComboBoxWritingSystem_Latin,
        (int)XFontComboBoxWritingSystem_Cyrillic,
        (int)XFontComboBoxWritingSystem_Arabic,
        (int)XFontComboBoxWritingSystem_SimplifiedChinese,
        (int)XFontComboBoxWritingSystem_Japanese,
        (int)XFontComboBoxWritingSystem_Symbol
    };
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    int allCount;
    int k;

    XQ_REQUIRE(fc != NULL);
    fcbws_prepareOracle();
    allCount = XComboBox_count((XComboBox*)fc);
    XQ_CHECK(allCount > 0);

    /* Any（缺省）不过滤：Qt `families(Any)` 走「skipping filter」分支
     * （qfontdatabase.cpp:1443-1448）。已有测试覆盖缺省，这里锁**回到**
     * Any 后恢复全量（往返）。 */
    XFontComboBox_setWritingSystem(fc,
                                   (int)XFontComboBoxWritingSystem_Arabic);
    XQ_CHECK(XComboBox_count((XComboBox*)fc) <= allCount);
    XFontComboBox_setWritingSystem(fc, (int)XFontComboBoxWritingSystem_Any);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);

    /* 各系统：结果 ⊂ 全量族集（Qt 是 families(system) ⊂ families()），
     * 且不掺入全量列表里没有的族（防「兜底假条目」）。 */
    fcbws_snapshot((XComboBox*)fc); /* Any 全量族名快照（跨重建借用指针失效）。 */
    XQ_EQ_I64(s_nameCount, allCount);
    for (k = 0; k < (int)(sizeof(systems) / sizeof(systems[0])); ++k) {
        int i;
        int filtered;
        XFontComboBox_setWritingSystem(fc, systems[k]);
        filtered = XComboBox_count((XComboBox*)fc);
        XQ_CHECK(filtered <= allCount);
        for (i = 0; i < filtered; ++i) {
            const char* name = XComboBox_itemText_2((XComboBox*)fc, i);
            XQ_CHECK(name != NULL);
            /* 过滤后的每一条都必须是 Any 全量里的真实族名（快照比对，
             * 不依赖重建后的借用指针）。 */
            XQ_CHECK(fcbws_snapshotHas(name));
        }
        /* 每族入选与否由**独立复算**决定（见下方 ② 的逐族对拍）。 */
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
        XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
    }

    XClassDelete((XClass*)fc);
}

/* ============ ② 逐族对拍：过滤结果 == 独立复算的族集 ============ */

XQ_TEST(XFontComboBox, writingSystemMatchesIndependentPredicate)
{
    static const int systems[] = {
        (int)XFontComboBoxWritingSystem_Latin,
        (int)XFontComboBoxWritingSystem_Greek,
        (int)XFontComboBoxWritingSystem_Cyrillic,
        (int)XFontComboBoxWritingSystem_Armenian,
        (int)XFontComboBoxWritingSystem_Hebrew,
        (int)XFontComboBoxWritingSystem_Arabic,
        (int)XFontComboBoxWritingSystem_Devanagari,
        (int)XFontComboBoxWritingSystem_Bengali,
        (int)XFontComboBoxWritingSystem_Tamil,
        (int)XFontComboBoxWritingSystem_Thai,
        (int)XFontComboBoxWritingSystem_SimplifiedChinese,
        (int)XFontComboBoxWritingSystem_TraditionalChinese,
        (int)XFontComboBoxWritingSystem_Japanese,
        (int)XFontComboBoxWritingSystem_Korean,
        (int)XFontComboBoxWritingSystem_Vietnamese,
        (int)XFontComboBoxWritingSystem_Symbol
    };
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    int allCount;
    int k;

    XQ_REQUIRE(fc != NULL);
    fcbws_prepareOracle();
    /* 全量族名快照（跨重建借用指针失效，必须拷贝）。 */
    fcbws_snapshot((XComboBox*)fc);
    allCount = s_nameCount;
    XQ_CHECK(allCount > 0);

    for (k = 0; k < (int)(sizeof(systems) / sizeof(systems[0])); ++k) {
        int i;
        int expect = 0;
        int filtered;
        /* 独立复算期望命中数（快照 + 纯字符串/face 查询，不经实现内部）。 */
        for (i = 0; i < allCount; ++i)
            if (fcbws_expectedAllowed(s_names[i], systems[k])) ++expect;
        XFontComboBox_setWritingSystem(fc, systems[k]);
        filtered = XComboBox_count((XComboBox*)fc);
        XQ_EQ_I64(filtered, expect);
        /* 过滤后每一条都真的满足谓词（不接受「数量对但内容错」）。 */
        for (i = 0; i < filtered; ++i) {
            const char* name = XComboBox_itemText_2((XComboBox*)fc, i);
            XQ_CHECK(fcbws_expectedAllowed(name, systems[k]));
        }
        /* 每条入选族都来自 Any 全量（无凭空造族）。 */
        for (i = 0; i < filtered; ++i) {
            const char* name = XComboBox_itemText_2((XComboBox*)fc, i);
            XQ_CHECK(fcbws_snapshotHas(name));
        }
    }

    XFontComboBox_setWritingSystem(fc, (int)XFontComboBoxWritingSystem_Any);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);

    XClassDelete((XClass*)fc);
}

/* ============ ③ 当前项收敛：仍入选则保持，否则回落首项 ============ */
/* 信号口径（Qt 6.8 实测序列，探针 /tmp 探针程序 + qfontcombobox.cpp
 * :339-355）：一次 setWritingSystem 重建至多一组当前项信号——
 * - 当前族名不变：indexChanged 恰 1 次（offset 收敛），textChanged/
 *   fontChanged 0 次；
 * - 当前族被剔除（回落首项）：三路各 1 次；
 * - 空结果：fontChanged 1 次且载荷空族名（qfontcombobox.cpp:347-352
 *   currentFont=QFont() 显式 emit）。
 * 中间态（-1/空串）绝不发射（本实现用 XObject_blockSignals 挡
 * clear/populate 段，对标 Qt QSignalBlocker）。 */
XQ_TEST(XFontComboBox, writingSystemKeepsOrFallsBackCurrent)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    int allCount;
    char target[XFCBWS_NAME_MAX];
    XConnection* connF = NULL;
    XConnection* connI = NULL;
    XConnection* connT = NULL;
    int i;

    XQ_REQUIRE(fc != NULL);
    fcbws_prepareOracle();
    allCount = XComboBox_count((XComboBox*)fc);
    XQ_CHECK(allCount >= 1);

    /* 找一个「Any 下存在、但在 Latin 过滤后**不**入选」的族作为回落样本；
     * 找不到（罕见：全是 Latin 族）就跳过回落段（fc 档下目标条件同 oracle）。 */
    target[0] = '\0';
    fcbws_snapshot((XComboBox*)fc);
    for (i = 0; i < s_nameCount; ++i) {
        if (!s_names[i][0]) continue;
        if (!fcbws_expectedAllowed(s_names[i],
                                   (int)XFontComboBoxWritingSystem_Latin)) {
            XStrncpy(target, s_names[i], XFCBWS_NAME_MAX - 1);
            break;
        }
    }

    /* --- 保持段：LATIN 下应入选的族 → 切到 Latin 后当前族保持且行号跟随 --- */
    {
        char keep[XFCBWS_NAME_MAX];
        keep[0] = '\0';
        for (i = 0; i < s_nameCount; ++i) {
            if (!s_names[i][0]) continue;
            if (fcbws_expectedAllowed(s_names[i],
                                      (int)XFontComboBoxWritingSystem_Latin)) {
                XStrncpy(keep, s_names[i], XFCBWS_NAME_MAX - 1);
                break;
            }
        }
        if (keep[0]) {
            XFontComboBox_setCurrentFamily(fc, keep);
            XQ_EQ_STR(XFontComboBox_currentFamily(fc), keep);
            fcbws_fontChanged = 0;
            fcbws_indexChanged = 0;
            fcbws_textChanged = 0;
            connF = XObject_connect_2((XObject*)fc,
                              (size_t)XFontComboBox_currentFontChanged_signal(
                                  fc, NULL),
                              fcbws_fontChangedSlot);
            connI = XObject_connect_2((XObject*)fc,
                              (size_t)XComboBox_currentIndexChanged_signal(
                                  (XComboBox*)fc, 0),
                              fcbws_indexChangedSlot);
            connT = XObject_connect_2((XObject*)fc,
                              (size_t)XComboBox_currentTextChanged_signal(
                                  (XComboBox*)fc, NULL),
                              fcbws_textChangedSlot);
            XFontComboBox_setWritingSystem(
                fc, (int)XFontComboBoxWritingSystem_Latin);
            /* Qt updateModel 的 offset 语义：当前族仍入选 ⇒ 保持（本机
             * Qt 6.8 探针 `Noto Sans SignWriting` 158→7 且 currentText
             * 不变）。信号：族名未变 ⇒ fontChanged/textChanged 零发、
             * indexChanged 恰 1（offset 收敛）。 */
            XQ_EQ_STR(XFontComboBox_currentFamily(fc), keep);
            XQ_CHECK(XComboBox_currentIndex((XComboBox*)fc) >= 0);
            XQ_EQ_STR(XComboBox_itemText_2(
                          (XComboBox*)fc,
                          XComboBox_currentIndex((XComboBox*)fc)), keep);
            /* 信号（Qt 实测：族名不变 ⇒ fontChanged 零发、indexChanged
             * 恰 1）。textChanged 由 XComboBox 发射点语义恒发一组（不做
             * 同值判断，见 xfcb_rebuild 注），这里锁「恰 1 次且载荷=当前
             * 族名、绝无空串中间态」。**两档同口径**：B11 返工后
             * setWritingSystem 两档都重建（fc=FC_LANG 表，降档=字形覆盖
             * 表），信号序列不再分档。 */
            XQ_EQ_I64(fcbws_fontChanged, 0);
            XQ_EQ_I64(fcbws_textChanged, 1);
            XQ_EQ_STR(fcbws_lastText, keep);
            XQ_EQ_I64(fcbws_indexChanged, 1);
            if (connT) XObject_disconnect_2(connT);
            if (connI) XObject_disconnect_2(connI);
            if (connF) XObject_disconnect_2(connF);
            connT = connI = connF = NULL;
            XFontComboBox_setWritingSystem(
                fc, (int)XFontComboBoxWritingSystem_Any);
            XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
        }
    }

    /* --- 回落段：当前族被过滤剔除 ⇒ 当前项回落新列表首项（Qt 的
     *     `offset` 缺省 0，探针 `Bitstream Charter` 被等宽过滤剔除后
     *     currentText 变成新列表首项）。信号：族名已变 ⇒ 三路各 1 次。
     *     两档同口径（B11 返工后降档同样重建+过滤）。---
     * [2026-11-17 合并修正·平台前提] 本段前提是「过滤后**仍有条目**、
     *   当前族被剔除后回落新列表首项」——需要族列表带 writing-system
     *   标注，而该标注只来自 fontconfig（实现侧同一条件：
     *   Src/XGui/Widget/XFontComboBox.c:31
     *   `#if defined(XINYUE_C_HAS_FONTCONFIG) && defined(__linux__)`）。
     *   Windows 无 fontconfig ⇒ setWritingSystem 后列表**全空**（而非
     *   「只剔除当前族」）⇒ 本段断言（过滤后列表非空 / 回落首项 / 三路
     *   信号恰 1）在本平台不成立（远端自带该用例，其 POSIX 上的绿不能
     *   外推到 Windows）。本仓无 fontconfig 等价标注来源 ⇒ 本段按平台
     *   前提只在有 fontconfig 时执行；其后的「空列表段」照跑（它本就
     *   是无条目的口径，与平台无关）。 */
#if defined(XINYUE_C_HAS_FONTCONFIG) && defined(__linux__)
    if (target[0]) {
        int firstAfter;
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
        XFontComboBox_setCurrentFamily(fc, target);
        XQ_EQ_STR(XFontComboBox_currentFamily(fc), target);
        fcbws_fontChanged = 0;
        fcbws_indexChanged = 0;
        fcbws_textChanged = 0;
        connF = XObject_connect_2((XObject*)fc,
                          (size_t)XFontComboBox_currentFontChanged_signal(
                              fc, NULL),
                          fcbws_fontChangedSlot);
        connI = XObject_connect_2((XObject*)fc,
                          (size_t)XComboBox_currentIndexChanged_signal(
                                  (XComboBox*)fc, 0),
                          fcbws_indexChangedSlot);
        connT = XObject_connect_2((XObject*)fc,
                          (size_t)XComboBox_currentTextChanged_signal(
                                  (XComboBox*)fc, NULL),
                          fcbws_textChangedSlot);
        XFontComboBox_setWritingSystem(
            fc, (int)XFontComboBoxWritingSystem_Latin);
        XQ_CHECK(XComboBox_count((XComboBox*)fc) > 0);
        firstAfter = XComboBox_count((XComboBox*)fc) > 0
            ? XComboBox_currentIndex((XComboBox*)fc) : -1;
        XQ_EQ_I64(firstAfter, 0);
        /* 被剔除的族不得再是当前族（回落首项 ⇒ 族名已变）。 */
        XQ_CHECK(XStrcmp(XFontComboBox_currentFamily(fc), target) != 0);
        XQ_EQ_STR(XFontComboBox_currentFamily(fc),
                  XComboBox_itemText_2((XComboBox*)fc, 0));
        XQ_EQ_I64(fcbws_fontChanged, 1);
        XQ_EQ_I64(fcbws_textChanged, 1);
        XQ_EQ_STR(fcbws_lastText,
                  XComboBox_itemText_2((XComboBox*)fc, 0));
        XQ_EQ_I64(fcbws_indexChanged, 1);
        if (connT) XObject_disconnect_2(connT);
        if (connI) XObject_disconnect_2(connI);
        if (connF) XObject_disconnect_2(connF);
        connT = connI = connF = NULL;
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
    }
#endif /* fontconfig 平台前提（见段头注） */

    /* --- 空列表段（两档都实跑，B11 返工「探针改活」）：过滤后一条不剩
     *     ⇒ 当前项归 -1、列表为空、**不掺假族**、记忆清空 + 显式发一次
     *     空族名 currentFontChanged（qfontcombobox.cpp:347-352）+ 访问
     *     器收敛（currentFamily 归空，不再残留行编辑分叉文本）。
     *     原实现只在「零族书写系统」可复现时才锁本分支（本机 fc 档
     *     16 受理系统全有族 ⇒ [SKIP]＝复核点名的探针死设）；现改走
     *     fontFilters=MonospacedFonts 空结果路径（与书写系统零族进同一
     *     xfcb_rebuild count==0 分支），两档本机环境都非空转。若环境
     *     变化（部署了可解析等宽族）致 Mono 非空，退回零族书写系统
     *     扫描；两路都不可得才打诊断行（不静默跳过）。 --- */
    {
        int emptyCount;
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
        XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
        /* 钉已知族为当前项：记忆非空是「空态显式发射」的前置。 */
        {
            const char* first = XComboBox_itemText_2((XComboBox*)fc, 0);
            XQ_REQUIRE(first != NULL && first[0]);
            XFontComboBox_setCurrentFamily(fc, first);
            XQ_EQ_STR(XFontComboBox_currentFamily(fc), first);
        }
        fcbws_fontChanged = 0;
        XStrncpy(fcbws_lastFontFamily, "SENTINEL",
                 sizeof(fcbws_lastFontFamily) - 1);
        connF = XObject_connect_2((XObject*)fc,
                          (size_t)XFontComboBox_currentFontChanged_signal(
                              fc, NULL),
                          fcbws_fontChangedSlot);
        XFontComboBox_setFontFilters(fc,
                                     (int)XFontComboBoxFilter_MonospacedFonts);
        emptyCount = XComboBox_count((XComboBox*)fc);
        if (emptyCount == 0) {
            XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), -1);
            /* 访问器一致（B11 返工新增锁）：列表清空 ⇒ currentFamily
             * 归空串。行编辑残留未收敛时 currentText_2 走分叉态返回
             * 残留族名，与刚发射的空族名载荷自相矛盾（QComboBox::clear
             * 的 clearEditText 口径）。 */
            XQ_EQ_STR(XFontComboBox_currentFamily(fc), "");
            /* Qt :347-352：记忆非空 ⇒ currentFont=QFont() + 发一次空族名
             * （恰 1 次；行编辑收敛只发 editTextChanged，不叠加）。 */
            XQ_EQ_I64(fcbws_fontChanged, 1);
            XQ_EQ_STR(fcbws_lastFontFamily, "");
        } else {
            int probe2;
            int emptySystem = 0;
            static const int kProbes[] = {
                (int)XFontComboBoxWritingSystem_Latin,
                (int)XFontComboBoxWritingSystem_Greek,
                (int)XFontComboBoxWritingSystem_Cyrillic,
                (int)XFontComboBoxWritingSystem_Armenian,
                (int)XFontComboBoxWritingSystem_Hebrew,
                (int)XFontComboBoxWritingSystem_Arabic,
                (int)XFontComboBoxWritingSystem_Devanagari,
                (int)XFontComboBoxWritingSystem_Bengali,
                (int)XFontComboBoxWritingSystem_Tamil,
                (int)XFontComboBoxWritingSystem_Thai,
                (int)XFontComboBoxWritingSystem_SimplifiedChinese,
                (int)XFontComboBoxWritingSystem_TraditionalChinese,
                (int)XFontComboBoxWritingSystem_Japanese,
                (int)XFontComboBoxWritingSystem_Korean,
                (int)XFontComboBoxWritingSystem_Vietnamese,
                (int)XFontComboBoxWritingSystem_Symbol
            };
            XFontComboBox_setFontFilters(fc,
                                         (int)XFontComboBoxFilter_AllFonts);
            fcbws_fontChanged = 0;
            XStrncpy(fcbws_lastFontFamily, "SENTINEL",
                     sizeof(fcbws_lastFontFamily) - 1);
            for (probe2 = 0;
                 probe2 < (int)(sizeof(kProbes) / sizeof(kProbes[0]));
                 ++probe2) {
                int expect = 0;
                for (i = 0; i < s_nameCount; ++i)
                    if (fcbws_expectedAllowed(s_names[i], kProbes[probe2]))
                        ++expect;
                if (expect == 0) { emptySystem = kProbes[probe2]; break; }
            }
            if (emptySystem != 0) {
                XFontComboBox_setWritingSystem(fc, emptySystem);
                XQ_EQ_I64(XComboBox_count((XComboBox*)fc), 0);
                XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), -1);
                XQ_EQ_STR(XFontComboBox_currentFamily(fc), "");
                XQ_EQ_I64(fcbws_fontChanged, 1);
                XQ_EQ_STR(fcbws_lastFontFamily, "");
                XFontComboBox_setWritingSystem(
                    fc, (int)XFontComboBoxWritingSystem_Any);
            } else {
                printf("  [SKIP] 空列表分支：本机等宽过滤非空且 16 个受理"
                       "书写系统全有族——两路都无零族环境可复现（环境已"
                       "变化，需重取锚）。\n");
            }
        }
        if (connF) XObject_disconnect_2(connF);
        connF = NULL;
        /* 回到无过滤全量（Qt 探针 Any↔空态 往返 count 复原）。 */
        XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
        XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
    }

    /* --- 去重锁（P2）：同族重复 setCurrentFamily ⇒ currentFontChanged
     *     恰 0 次（转发槽按记忆族名去重）；同值 setWritingSystem ⇒ 不
     *     重建，三路全 0。复核点名的「currentFontChanged 双发」在两档
     *     转移矩阵（16 系统 × 3 滤波组合 + setCurrent 三态 + 分叉编辑）
     *     探针实测均不可复现（fc 档 0 告警），此处把「至多一组信号」
     *     钉成运行时锁。 --- */
    {
        const char* cur = XFontComboBox_currentFamily(fc);
        XQ_REQUIRE(cur != NULL && cur[0]);
        connF = XObject_connect_2((XObject*)fc,
                          (size_t)XFontComboBox_currentFontChanged_signal(
                              fc, NULL),
                          fcbws_fontChangedSlot);
        connT = XObject_connect_2((XObject*)fc,
                          (size_t)XComboBox_currentTextChanged_signal(
                                  (XComboBox*)fc, NULL),
                          fcbws_textChangedSlot);
        fcbws_fontChanged = 0;
        fcbws_textChanged = 0;
        XFontComboBox_setCurrentFamily(fc, cur);
        XQ_EQ_I64(fcbws_fontChanged, 0);
        XQ_EQ_I64(fcbws_textChanged, 0);
        XFontComboBox_setCurrentFamily(fc, cur);
        XQ_EQ_I64(fcbws_fontChanged, 0);
        XQ_EQ_I64(fcbws_textChanged, 0);
        fcbws_fontChanged = 0;
        fcbws_indexChanged = 0;
        fcbws_textChanged = 0;
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
        XQ_EQ_I64(fcbws_fontChanged, 0);
        XQ_EQ_I64(fcbws_indexChanged, 0);
        XQ_EQ_I64(fcbws_textChanged, 0);
        if (connT) XObject_disconnect_2(connT);
        if (connF) XObject_disconnect_2(connF);
        connT = connF = NULL;
    }

    /* 非法值忽略（保持原值，不重建）。 */
    XFontComboBox_setWritingSystem(
        fc, (int)XFontComboBoxWritingSystem_Latin);
    {
        int before = XComboBox_count((XComboBox*)fc);
        XFontComboBox_setWritingSystem(fc, 7); /* Syriac，不在本类枚举子集 */
        XQ_EQ_I64(XFontComboBox_writingSystem(fc),
                  (int)XFontComboBoxWritingSystem_Latin);
        XQ_EQ_I64(XComboBox_count((XComboBox*)fc), before);
    }
    XFontComboBox_setWritingSystem(fc, (int)XFontComboBoxWritingSystem_Any);

    XClassDelete((XClass*)fc);
}

/* ======== ④ writingSystem × fontFilters 为「与」关系（Qt updateModel） ==== */

XQ_TEST(XFontComboBox, writingSystemCombinesWithFontFilters)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    int allCount;
    int wsOnly;
    int bothCount;
    int i;

    XQ_REQUIRE(fc != NULL);
    fcbws_prepareOracle();
    allCount = XComboBox_count((XComboBox*)fc);
    XQ_CHECK(allCount > 0);

    /* 纯书写系统（Monospaced 关）：SC 下族数（Qt 探针 276→33）。 */
    XFontComboBox_setWritingSystem(
        fc, (int)XFontComboBoxWritingSystem_SimplifiedChinese);
    wsOnly = XComboBox_count((XComboBox*)fc);
    XQ_CHECK(wsOnly <= allCount);

    /* 再叠加等宽过滤：结果 ⊆ 纯书写系统结果（Qt：families(system) 之后
     * 逐族再过 filters）。 */
    XFontComboBox_setFontFilters(fc,
                                 (int)XFontComboBoxFilter_MonospacedFonts);
    bothCount = XComboBox_count((XComboBox*)fc);
    XQ_CHECK(bothCount <= wsOnly);
    /* 每条同时满足两个条件（书写系统谓词独立复算 + 等宽由 i/W 推进判定）。 */
    for (i = 0; i < bothCount; ++i) {
        const char* name = XComboBox_itemText_2((XComboBox*)fc, i);
        XFont f;
        int wi;
        int ww;
        XQ_CHECK(fcbws_expectedAllowed(
            name, (int)XFontComboBoxWritingSystem_SimplifiedChinese));
        if (!name || !name[0]) continue;
        XFont_init_ex(&f, name, 12, -1, false);
        wi = XPainter_textWidth(&f, "i");
        ww = XPainter_textWidth(&f, "W");
        XClassDeinit((XClass*)&f);
        /* 等宽＝可解析成轮廓 face 且 i/W 推进相等（与既有 fontFilters
         * 用例同源谓词）；不满足者不得出现在等宽过滤结果里。 */
        XQ_CHECK(wi > 0 && wi == ww);
    }

    /* 反向顺序（先 filters 再 writingSystem）结果一致：两个条件可交换
     * （Qt 侧 filters 在 families(system) 之后逐族判，顺序不影响集合）。 */
    XFontComboBox_setWritingSystem(fc,
                                   (int)XFontComboBoxWritingSystem_Any);
    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
    XFontComboBox_setFontFilters(fc,
                                 (int)XFontComboBoxFilter_MonospacedFonts);
    XFontComboBox_setWritingSystem(
        fc, (int)XFontComboBoxWritingSystem_SimplifiedChinese);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), bothCount);

    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
    XFontComboBox_setWritingSystem(fc, (int)XFontComboBoxWritingSystem_Any);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);

    XClassDelete((XClass*)fc);
}

/* ======== ⑤ 重建不破坏其它受管状态（显示字体/记忆/当前族记忆） ======== */

XQ_TEST(XFontComboBox, writingSystemRebuildKeepsManagedState)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    XFont df;
    XFont got;
    char family0[XFCBWS_NAME_MAX];
    int i;

    XQ_REQUIRE(fc != NULL);
    fcbws_prepareOracle();
    XQ_CHECK(XComboBox_count((XComboBox*)fc) > 0);
    /* 取一个**在 Latin 过滤下也应入选**的族做显示字体登记宿主，
     * 这样重建后它仍在列表里，角色重放可断言。 */
    family0[0] = '\0';
    fcbws_snapshot((XComboBox*)fc);
    for (i = 0; i < s_nameCount; ++i) {
        if (s_names[i][0] &&
            fcbws_expectedAllowed(s_names[i],
                                  (int)XFontComboBoxWritingSystem_Latin)) {
            XStrncpy(family0, s_names[i], XFCBWS_NAME_MAX - 1);
            break;
        }
    }

    if (family0[0]) {
        XFont_init(&df);
        XFont_setFamily(&df, family0);
        XFont_setPointSize(&df, 29);
        XFontComboBox_setDisplayFont(fc, family0, &df);
        /* 书写系统重建（触发 xfcb_rebuild 的重放路径）。 */
        XFontComboBox_setWritingSystem(
            fc, (int)XFontComboBoxWritingSystem_Latin);
        /* 登记表本身不被重建清掉（Qt：displayFontForFontFamily 是独立
         * d-pointer 状态，updateModel 不动它）。 */
        XQ_CHECK(XFontComboBox_displayFont(fc, family0, &got));
        XQ_EQ_I64(XFont_pointSize(&got), 29);
        if (XFont_family(&got))
            XQ_EQ_STR(XFont_family(&got), family0);
        XClassDeinit((XClass*)&got);
        /* 重建后该族仍入选 ⇒ 当前族保持为该族（Qt offset 口径）。 */
        XQ_EQ_STR(XFontComboBox_currentFamily(fc), family0);
        XFontComboBox_setDisplayFont(fc, family0, NULL);
        XClassDeinit((XClass*)&df);
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
    }

    /* 逐族样例文本（setSampleTextForFont）是独立状态，重建不清。 */
    XFontComboBox_setSampleTextForFont(fc, "XAlignWsSample", "样例");
    {
        XString* sample = XFontComboBox_sampleTextForFont(fc, "XAlignWsSample");
        XQ_CHECK(sample != NULL);
        if (sample) XQ_EQ_STR(XString_toUtf8(sample), "样例");
        if (sample) XClassDelete((XClass*)sample);
    }
    XFontComboBox_setWritingSystem(
        fc, (int)XFontComboBoxWritingSystem_Arabic);
    {
        XString* sample = XFontComboBox_sampleTextForFont(fc, "XAlignWsSample");
        XQ_CHECK(sample != NULL);
        if (sample) XQ_EQ_STR(XString_toUtf8(sample), "样例");
        if (sample) XClassDelete((XClass*)sample);
    }
    XFontComboBox_setWritingSystem(fc, (int)XFontComboBoxWritingSystem_Any);

    /* 同值重复 set 不重建（Qt 探针：setWritingSystem(Any) 再次调用当前项
     * 与索引都不动）。 */
    {
        int idx;
        const char* cur;
        XQ_REQUIRE(XComboBox_count((XComboBox*)fc) > 1);
        idx = XComboBox_currentIndex((XComboBox*)fc);
        cur = XFontComboBox_currentFamily(fc);
        XQ_CHECK(cur != NULL);
        if (cur && cur[0])
            XFontComboBox_setCurrentFamily(fc, cur);
        idx = XComboBox_currentIndex((XComboBox*)fc);
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
        XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), idx);
        XQ_EQ_STR(XFontComboBox_currentFamily(fc), cur);
    }

    XClassDelete((XClass*)fc);
}

/* === ⑥ fontFilters 掩码语义 + 过滤保序不变式（Qt updateModel :297-310） === */
XQ_TEST(XFontComboBox, fontFiltersMaskSemanticsAndOrderKeep)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    int allCount;
    int i;

    XQ_REQUIRE(fc != NULL);
    fcbws_prepareOracle();
    allCount = XComboBox_count((XComboBox*)fc);
    XQ_CHECK(allCount > 0);

    /* 掩码语义：Scalable|NonScalable 同置互相抵消＝不过滤（Qt 原文
     * `(filters & mask) && (filters & mask) != mask`；本机 Qt 6.8 探针
     * 实测矛盾位组合 count 保持 276）。两对掩码同时矛盾同样不过滤。 */
    XFontComboBox_setFontFilters(fc,
        (int)XFontComboBoxFilter_ScalableFonts |
        (int)XFontComboBoxFilter_NonScalableFonts);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
    XFontComboBox_setFontFilters(fc,
        (int)XFontComboBoxFilter_ScalableFonts |
        (int)XFontComboBoxFilter_NonScalableFonts |
        (int)XFontComboBoxFilter_MonospacedFonts |
        (int)XFontComboBoxFilter_ProportionalFonts);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);

    /* 过滤保序不变式：单边位过滤后，Any 全量第 0 族若幸存，则仍是子集
     * 第 0 项且为当前项（锁「setFontFilters 保持语义下的 index==0」——
     * 旧断言 XFontComboBoxAlignTest.c:111-112 依赖的正是这条，此前无
     * 运行时锁）。 */
    fcbws_snapshot((XComboBox*)fc);
    XQ_CHECK(s_nameCount == allCount);
    {
        char firstAny[XFCBWS_NAME_MAX];
        XStrncpy(firstAny, s_names[0], XFCBWS_NAME_MAX - 1);
        XFontComboBox_setFontFilters(fc,
            (int)XFontComboBoxFilter_MonospacedFonts);
        {
            int filtered = XComboBox_count((XComboBox*)fc);
            XQ_CHECK(filtered <= allCount);
            for (i = 0; i < filtered; ++i) {
                const char* name = XComboBox_itemText_2((XComboBox*)fc, i);
                XQ_CHECK(name != NULL && name[0]);
            }
            /* 保序性本身：子集序 == 全量序的子序列。 */
            {
                int cursor = 0;
                for (i = 0; i < filtered; ++i) {
                    const char* name =
                        XComboBox_itemText_2((XComboBox*)fc, i);
                    while (cursor < s_nameCount &&
                           XStrcmp(s_names[cursor], name) != 0)
                        ++cursor;
                    XQ_CHECK(cursor < s_nameCount);
                    if (cursor < s_nameCount) ++cursor;
                }
            }
            /* 全量首族幸存 ⇒ 它就是子集首项且为当前项。 */
            for (i = 0; i < filtered; ++i) {
                const char* name =
                    XComboBox_itemText_2((XComboBox*)fc, i);
                if (name && XStrcmp(name, firstAny) == 0) {
                    XQ_EQ_I64(i, 0);
                    XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), 0);
                    XQ_EQ_STR(XFontComboBox_currentFamily(fc), firstAny);
                    break;
                }
            }
        }
        XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
        XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
    }

    XClassDelete((XClass*)fc);
}

/* === ⑦ 降级档真实探测契约：逐族字形覆盖过滤 + 非常量硬锚 === */
/* 仅无 fontconfig 构建编译（fc 档下书写系统过滤走 FC_LANG 对拍，
 * 见用例②；本用例锁降档的「自有 face 判定 + 代表码位探测」契约，
 * 替换早前「只存值不过滤」的门控锁——复核 2026-11-15 定案）。 */
#if !XFCB_TEST_HAS_FONTCONFIG
XQ_TEST(XFontComboBox, writingSystemDegradedRealProbe)
{
    static const int systems[] = {
        (int)XFontComboBoxWritingSystem_Latin,
        (int)XFontComboBoxWritingSystem_Greek,
        (int)XFontComboBoxWritingSystem_Cyrillic,
        (int)XFontComboBoxWritingSystem_Armenian,
        (int)XFontComboBoxWritingSystem_Hebrew,
        (int)XFontComboBoxWritingSystem_Arabic,
        (int)XFontComboBoxWritingSystem_Devanagari,
        (int)XFontComboBoxWritingSystem_Bengali,
        (int)XFontComboBoxWritingSystem_Tamil,
        (int)XFontComboBoxWritingSystem_Thai,
        (int)XFontComboBoxWritingSystem_SimplifiedChinese,
        (int)XFontComboBoxWritingSystem_TraditionalChinese,
        (int)XFontComboBoxWritingSystem_Japanese,
        (int)XFontComboBoxWritingSystem_Korean,
        (int)XFontComboBoxWritingSystem_Vietnamese,
        (int)XFontComboBoxWritingSystem_Symbol
    };
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    int allCount;
    int k;
    int latinAll = 0;
    int latinPass = 0;

    XQ_REQUIRE(fc != NULL);
    fcbws_prepareOracle();
    fcbws_snapshot((XComboBox*)fc);
    allCount = s_nameCount;
    XQ_CHECK(allCount > 0);

    /* 非常量硬锚的环境侧写（复核①定案口径）：统计 Latin 下 oracle 的
     * 命中数。本机降档环境（族集=兜底四族，仅 XFontOutlineCommon 解析
     * 自有 face）oracle 判 1/4 ——与「全过」「全不过」两个常量答案都
     * 不同。若环境变化致 oracle 给出常量答案，下方分支锁自动退化为
     * 「实现==oracle」一致性锁并打诊断行（诚实降档，不伪造）。 */
    for (k = 0; k < allCount; ++k) {
        ++latinAll;
        if (fcbws_expectedAllowed(s_names[k],
                                  (int)XFontComboBoxWritingSystem_Latin))
            ++latinPass;
    }

    /* 主契约：每个受理系统的过滤结果 == oracle 独立复算（数量 + 逐条
     * 成员 + 全来自 Any 全量），当前项收敛与信号序列同 fc 档用例③。 */
    for (k = 0; k < (int)(sizeof(systems) / sizeof(systems[0])); ++k) {
        int i;
        int expect = 0;
        int filtered;
        for (i = 0; i < allCount; ++i)
            if (fcbws_expectedAllowed(s_names[i], systems[k])) ++expect;
        XFontComboBox_setWritingSystem(fc, systems[k]);
        XQ_EQ_I64(XFontComboBox_writingSystem(fc), systems[k]);
        filtered = XComboBox_count((XComboBox*)fc);
        XQ_EQ_I64(filtered, expect);
        for (i = 0; i < filtered; ++i) {
            const char* name = XComboBox_itemText_2((XComboBox*)fc, i);
            XQ_CHECK(fcbws_expectedAllowed(name, systems[k]));
            XQ_CHECK(fcbws_snapshotHas(name));
        }
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
        XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
    }

    /* 非常量锁：Latin 下 oracle 非平凡（既非全过也非全不过）⇒ 实现
     * 结果必须同样非平凡——这正是上轮「常量谓词」（Latin 全过/其余
     * 全无）的事故签名。 */
    if (latinPass > 0 && latinPass < latinAll) {
        int filtered;
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Latin);
        filtered = XComboBox_count((XComboBox*)fc);
        XQ_CHECK(filtered > 0 && filtered < latinAll);
        XFontComboBox_setWritingSystem(fc,
                                       (int)XFontComboBoxWritingSystem_Any);
    } else {
        printf("  [环境] 降档 Latin oracle 平凡（全过 %d/%d 或全无）"
               "——非常量锁退化为一致性锁，需重取环境锚。\n",
               latinPass, latinAll);
    }

    /* 自有 face 判定硬锚：本机环境 Sans Serif/Serif/Monospace 解析回落
     * 替身 face（XFont8x16 位图）⇒ 无标注 ⇒ 任何书写系统（含 Symbol）
     * 都不得出现。若环境部署了同名字库文件使别名族获得自有 face，
     * oracle 自行翻转，此锁按 oracle 分支放行并打诊断。 */
    {
        static const char* const kAliases[] = {
            "Sans Serif", "Serif", "Monospace"
        };
        int a;
        int aliasOwn = 0;
        for (a = 0; a < (int)(sizeof(kAliases) / sizeof(kAliases[0])); ++a)
            if (fcbws_snapshotHas(kAliases[a]))
                aliasOwn += fcbws_degradedOwnFace(kAliases[a]) ? 1 : 0;
        if (aliasOwn < (int)(sizeof(kAliases) / sizeof(kAliases[0]))) {
            int i;
            for (k = 0;
                 k < (int)(sizeof(systems) / sizeof(systems[0])); ++k) {
                int filtered;
                XFontComboBox_setWritingSystem(fc, systems[k]);
                filtered = XComboBox_count((XComboBox*)fc);
                for (i = 0; i < filtered; ++i) {
                    const char* name =
                        XComboBox_itemText_2((XComboBox*)fc, i);
                    int a2;
                    for (a2 = 0; a2 < (int)(sizeof(kAliases) /
                                            sizeof(kAliases[0])); ++a2)
                        if (!fcbws_degradedOwnFace(kAliases[a2]))
                            XQ_CHECK(XStrcmp(name, kAliases[a2]) != 0);
                }
                XFontComboBox_setWritingSystem(
                    fc, (int)XFontComboBoxWritingSystem_Any);
            }
        } else {
            printf("  [环境] 兜底别名族全部获得自有 face——替身判定锚"
                   "失效，需重取环境锚。\n");
        }
    }

    /* 存取往返（setter 语义保底，两档同）。 */
    XFontComboBox_setWritingSystem(fc, (int)XFontComboBoxWritingSystem_Korean);
    XQ_EQ_I64(XFontComboBox_writingSystem(fc),
              (int)XFontComboBoxWritingSystem_Korean);
    XFontComboBox_setWritingSystem(fc, 7); /* Syriac，不在本类枚举子集。 */
    XQ_EQ_I64(XFontComboBox_writingSystem(fc),
              (int)XFontComboBoxWritingSystem_Korean);
    XFontComboBox_setWritingSystem(fc, (int)XFontComboBoxWritingSystem_Any);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
    XClassDelete((XClass*)fc);
}
#endif /* !XFCB_TEST_HAS_FONTCONFIG */

#endif /* XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */
