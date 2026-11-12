/**
 * @file       XTextFormatAlignTest.c
 * @brief      XTextFormat 属性池 / XTextCharFormat / XTextBlockFormat ×
 *             Qt 6.8 QTextFormat 族对齐契约测试（§B16 W1-a 文本格式池）。
 * @details    对齐依据（Qt 6.8.3 原文，行号以函数名为锚）：
 *             - qtextformat.cpp setProperty/insertProperty（qtextformat.cpp
 *               :166-183、:1216-1227：属性无条件落表、同键覆盖否则追加）；
 *             - 各型 getter 的严格类型回落缺省（boolProperty :1042、
 *               intProperty :1059 含 LayoutDirection 特例=3、
 *               doubleProperty :1080 接受 Double/Float、stringProperty
 *               :1098、colorProperty :1116）；
 *             - merge（:940-965：type 不同不动、空表全量收、冲突 other
 *               优先、无效值清键）；operator==（:1362-1380）；
 *             - 缺省值表：fontWeight 未设 Normal=400（qtextformat.h:451）、
 *               superScriptBaseline 50.0 / subScriptBaseline 100.0/6.0 /
 *               baselineOffset 0.0（:548/:553/:558）、blockFormat alignment
 *               未设 AlignLeft=0x01（:622-624）、listFormat 构造缺省
 *               indent=1/start=1（qtextformat.cpp:2644-2648）、imageFormat
 *               构造落 ObjectType=ImageObject（:3387）、setFontFamily 写
 *               FontFamilies（qtextformat.h:417-419）、setFontUnderline 写
 *               TextUnderlineStyle（:473-474）、fontUnderline 读序
 *               TextUnderlineStyle→FontUnderline 回落（qtextformat.cpp
 *               :1638-1643）；
 *             - 文档侧：格式池对标 QTextFormatCollection 去重集合；
 *               markContentsDirty 的区间合并逐字对齐
 *               QTextDocumentPrivate::documentChange（qtextdocument_p.cpp
 *               :1229-1244）；markContentsDirty 公共语义
 *               （qtextdocument.cpp:615-625）。
 *
 *             覆盖：属性 set/get/has/clear/merge 逐项（含缺省值表对拍）、
 *             类型位与 Qt 同名属性值、文档格式池去重与 clone 重建、
 *             内联⇄属性格式转换桥往返等价（渲染路径逐字等价）、
 *             markContentsDirty 区间合并、copy/move/deinit 账目平。
 * @note       C99；所有栈实例显式 XTextFormat_deinit，文档 XClassDelete，
 *             漏删即运行器判 [LEAK]。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XTEXTDOCUMENT_ON

#include "XTextDocument.h"
#include "XTextFormat.h"
#include "XMemory.h"
#include "XObject.h"

#include <math.h>
#include <string.h>

#define XTF_EPS 1e-9

/** @brief double 相等断言（小容差）。 */
static void xtf_eq_dbl(double a, double b, int line)
{
    xq_check_impl(fabs(a - b) <= XTF_EPS, "double equal", __FILE__, line,
                  "left=%.9f right=%.9f", a, b);
}
#define XTF_EQ_DBL(a, b) xtf_eq_dbl((a), (b), __LINE__)

/** @brief 释放 textFormatToCharFormat 输出的字符串字段（反向桥契约：
 *         字段由桥新建、归调用方释放）。 */
static void xtf_freeXTD(XTDCharFormat* f)
{
    if (!f) return;
    if (f->fontFamily) {
        XClassDelete(f->fontFamily);
        f->fontFamily = NULL;
    }
    if (f->anchorHref) {
        XClassDelete(f->anchorHref);
        f->anchorHref = NULL;
    }
}

/* ==================== ① 属性表 set/get/has/clear ==================== */

XQ_TEST(XTextFormat, propertyTable)
{
    XTextFormat f;
    const XVariant* v;

    XTextFormat_init(&f, XTextFormatType_CharFormat);
    XQ_CHECK(XTextFormat_isEmpty(&f));           /* 构造零属性（Qt:884-887） */
    XQ_EQ_I64(XTextFormat_propertyCount(&f), 0);
    XQ_EQ_I64(XTextFormat_type(&f), XTextFormatType_CharFormat);
    XQ_CHECK(XTextFormat_isValid(&f));

    /* set → has/property/getter 四型 */
    XTextFormat_setProperty_int(&f, XTextFormatProperty_FontWeight, 700);
    XTextFormat_setProperty_double(&f, XTextFormatProperty_FontPointSize,
                                   12.5);
    XTextFormat_setProperty_bool(&f, XTextFormatProperty_FontItalic, true);
    XTextFormat_setProperty_string(&f, XTextFormatProperty_AnchorHref,
                                   "https://example.com/");
    XTextFormat_setProperty_color(&f, XTextFormatProperty_ForegroundBrush,
                                  0xFF336699u);
    XQ_EQ_I64(XTextFormat_propertyCount(&f), 5);
    XQ_CHECK(XTextFormat_hasProperty(&f, XTextFormatProperty_FontWeight));
    XQ_CHECK(!XTextFormat_hasProperty(&f, XTextFormatProperty_BlockIndent));
    XQ_EQ_I64(XTextFormat_intProperty(&f, XTextFormatProperty_FontWeight),
              700);
    XTF_EQ_DBL(
        XTextFormat_doubleProperty(&f, XTextFormatProperty_FontPointSize),
        12.5);
    XQ_CHECK(XTextFormat_boolProperty(&f, XTextFormatProperty_FontItalic));
    XQ_EQ_STR(XTextFormat_stringProperty(&f, XTextFormatProperty_AnchorHref),
              "https://example.com/");
    XQ_EQ_I64(
        (long long)XTextFormat_colorProperty(
            &f, XTextFormatProperty_ForegroundBrush),
        (long long)0xFF336699u);
    v = XTextFormat_property(&f, XTextFormatProperty_FontWeight);
    XQ_CHECK_PTR(v);
    XQ_EQ_I64((long long)v->m_type, (long long)XVariantType_Int);
    /* 借用指针不释放（所有权契约） */

    /* 同键覆盖（Qt insertProperty 覆盖分支：条数不增） */
    XTextFormat_setProperty_int(&f, XTextFormatProperty_FontWeight, 400);
    XQ_EQ_I64(XTextFormat_propertyCount(&f), 5);
    XQ_EQ_I64(XTextFormat_intProperty(&f, XTextFormatProperty_FontWeight),
              400);

    /* clear → has=false、条数减一；清不存在的键不动 */
    XTextFormat_clearProperty(&f, XTextFormatProperty_FontItalic);
    XQ_CHECK(!XTextFormat_hasProperty(&f, XTextFormatProperty_FontItalic));
    XQ_EQ_I64(XTextFormat_propertyCount(&f), 4);
    XTextFormat_clearProperty(&f, XTextFormatProperty_BlockIndent);
    XQ_EQ_I64(XTextFormat_propertyCount(&f), 4);

    /* 类型不符回落缺省（getter 严格类型）：double 属性读 int → 0 */
    XQ_EQ_I64(XTextFormat_intProperty(&f, XTextFormatProperty_FontPointSize),
              0);
    /* int 属性读 double → 0.0 */
    XTF_EQ_DBL(XTextFormat_doubleProperty(&f, XTextFormatProperty_FontWeight),
               0.0);
    /* color 属性读 string → ""（Qt stringProperty 非 QString 型回落空串） */
    XQ_EQ_STR(XTextFormat_stringProperty(
                  &f, XTextFormatProperty_ForegroundBrush),
              "");
    XTextFormat_deinit(&f);
    XQ_CHECK(XTextFormat_isEmpty(&f)); /* deinit 复位空壳 */
    XQ_EQ_I64(XTextFormat_type(&f), XTextFormatType_InvalidFormat);
}

XQ_TEST(XTextFormat, defaultValueTable)
{
    XTextFormat f;
    XTextFormat_init(&f, XTextFormatType_CharFormat);

    /* intProperty 未设 → 0；LayoutDirection(0x801) 特例 → 3
     * （qtextformat.cpp:1062，LayoutDirectionAuto） */
    XQ_EQ_I64(XTextFormat_intProperty(&f, XTextFormatProperty_FontWeight),
              0);
    XQ_EQ_I64(
        XTextFormat_intProperty(&f, XTextFormatProperty_LayoutDirection), 3);
    /* doubleProperty 未设 → 0.0；boolProperty 未设 → false；
     * stringProperty 未设 → ""；colorProperty 未设 → 0（无效色） */
    XTF_EQ_DBL(XTextFormat_doubleProperty(&f, 0x2001), 0.0);
    XQ_CHECK(!XTextFormat_boolProperty(&f, XTextFormatProperty_FontItalic));
    XQ_EQ_STR(XTextFormat_stringProperty(&f, 0x2031), "");
    XQ_EQ_I64((long long)XTextFormat_colorProperty(&f, 0x821), 0);

    /* CharFormat 属性缺省表（「属性未设」分支） */
    XQ_EQ_I64(XTextCharFormat_fontWeight(&f), 400);            /* h:451 */
    XTF_EQ_DBL(XTextCharFormat_superScriptBaseline(&f), 50.0); /* h:548 */
    XTF_EQ_DBL(XTextCharFormat_subScriptBaseline(&f), 100.0 / 6.0); /* h:553 */
    XTF_EQ_DBL(XTextCharFormat_baselineOffset(&f), 0.0);       /* h:558 */
    XQ_EQ_I64(XTextCharFormat_underlineStyle(&f), 0); /* 未设 NoUnderline */
    XQ_EQ_I64(XTextCharFormat_verticalAlignment(&f), 0); /* AlignNormal */
    XQ_CHECK(!XTextCharFormat_fontItalic(&f));
    XQ_CHECK(!XTextCharFormat_fontUnderline(&f));
    XQ_CHECK(!XTextCharFormat_isAnchor(&f));
    XQ_CHECK(XTextCharFormat_fontFamily(&f) == NULL); /* 未设明确 NULL */

    /* BlockFormat 属性缺省表 */
    {
        XTextBlockFormat b;
        XTextBlockFormat_init(&b);
        XQ_CHECK(XTextFormat_isEmpty(&b)); /* 零缺省属性（cpp:2221） */
        XQ_EQ_I64(XTextBlockFormat_alignment(&b), 0x01); /* h:622-624 */
        XQ_EQ_I64(XTextBlockFormat_indent(&b), 0);
        XQ_EQ_I64(XTextBlockFormat_headingLevel(&b), 0);
        XTF_EQ_DBL(XTextBlockFormat_topMargin(&b), 0.0);
        XTF_EQ_DBL(XTextBlockFormat_bottomMargin(&b), 0.0);
        XTF_EQ_DBL(XTextBlockFormat_leftMargin(&b), 0.0);
        XTF_EQ_DBL(XTextBlockFormat_rightMargin(&b), 0.0);
        XTF_EQ_DBL(XTextBlockFormat_textIndent(&b), 0.0);
        XTF_EQ_DBL(XTextBlockFormat_lineHeight(&b), 0.0);
        XQ_EQ_I64(XTextBlockFormat_lineHeightType(&b), 0);
        XTextFormat_deinit(&b);
    }
    /* ListFormat 构造缺省（cpp:2644-2648）：indent=1、start=1 */
    {
        XTextListFormat l;
        XTextListFormat_init(&l);
        XQ_CHECK(!XTextFormat_isEmpty(&l));
        XQ_EQ_I64(XTextFormat_propertyCount(&l), 2);
        XQ_EQ_I64(XTextListFormat_indent(&l), 1);
        XQ_EQ_I64(XTextListFormat_start(&l), 1);
        XQ_EQ_I64(XTextListFormat_style(&l), 0); /* StyleUndefined */
        XTextFormat_deinit(&l);
    }
    XTextFormat_deinit(&f);
}

/* ==================== ② 类型位与同名属性值 ==================== */

XQ_TEST(XTextFormat, typeBits)
{
    XTextCharFormat c;
    XTextBlockFormat b;
    XTextListFormat l;
    XTextImageFormat img;
    XTextFormat plain;

    XTextCharFormat_init(&c);
    XTextBlockFormat_init(&b);
    XTextListFormat_init(&l);
    XTextImageFormat_init(&img);
    XTextFormat_init(&plain, XTextFormatType_FrameFormat);

    /* FormatType 数值逐一对齐 Qt（qtextformat.h:93-101） */
    XQ_EQ_I64(XTextFormat_type(&c), 2);
    XQ_EQ_I64(XTextFormat_type(&b), 1);
    XQ_EQ_I64(XTextFormat_type(&l), 3);
    XQ_EQ_I64(XTextFormat_type(&plain), 5);
    XQ_CHECK(XTextFormat_isCharFormat(&c));
    XQ_CHECK(XTextFormat_isBlockFormat(&b));
    XQ_CHECK(XTextFormat_isListFormat(&l));
    XQ_CHECK(XTextFormat_isFrameFormat(&plain));
    XQ_CHECK(!XTextFormat_isBlockFormat(&c));
    XQ_CHECK(!XTextFormat_isCharFormat(&b));

    /* ImageFormat：CharFormat + ObjectType=ImageObject(1)（cpp:3387） */
    XQ_EQ_I64(XTextFormat_type(&img), 2);
    XQ_CHECK(XTextFormat_isImageFormat(&img));
    XQ_CHECK(!XTextFormat_isImageFormat(&c));
    XQ_EQ_I64(
        XTextFormat_intProperty(&img, XTextFormatProperty_ObjectType), 1);

    /* setFontFamily 落 FontFamilies(0x1FE7)（h:417-419），不落旧键 0x2000 */
    XTextCharFormat_setFontFamily(&c, "Noto Sans CJK SC");
    XQ_CHECK(XTextFormat_hasProperty(&c, XTextFormatProperty_FontFamilies));
    XQ_CHECK(!XTextFormat_hasProperty(&c, 0x2000 /*FontFamily 旧键*/));
    XQ_EQ_STR(XTextCharFormat_fontFamily(&c), "Noto Sans CJK SC");

    /* setFontPointSize → FontPointSize(0x2001) double 承载 */
    XTextCharFormat_setFontPointSize(&c, 14.0);
    XTF_EQ_DBL(XTextCharFormat_fontPointSize(&c), 14.0);
    XQ_EQ_I64((long long)
                  XTextFormat_property(&c, XTextFormatProperty_FontPointSize)
                  ->m_type,
              (long long)XVariantType_Double);

    /* setFontUnderline 落 TextUnderlineStyle(0x2023)（h:473-474），
     * 不落 FontUnderline(0x2005) */
    XTextCharFormat_setFontUnderline(&c, true);
    XQ_CHECK(XTextFormat_hasProperty(&c,
                                     XTextFormatProperty_TextUnderlineStyle));
    XQ_CHECK(!XTextFormat_hasProperty(&c, XTextFormatProperty_FontUnderline));
    XQ_EQ_I64(
        XTextFormat_intProperty(&c, XTextFormatProperty_TextUnderlineStyle),
        XTextFormatUnderline_SingleUnderline);
    XQ_CHECK(XTextCharFormat_fontUnderline(&c));
    /* 兼容读：直接落 FontUnderline 旧键亦被 fontUnderline 认出
     * （qtextformat.cpp:1638-1643 的回落分支） */
    {
        XTextFormat legacy;
        XTextFormat_init(&legacy, XTextFormatType_CharFormat);
        XTextFormat_setProperty_bool(&legacy, XTextFormatProperty_FontUnderline,
                                     true);
        XQ_CHECK(XTextCharFormat_fontUnderline(&legacy));
        XTextFormat_deinit(&legacy);
    }

    /* BlockAlignment 语义：set(Justify=0x08) 原样落 0x1010 */
    XTextBlockFormat_setAlignment(&b, XTDAlignment_Justify);
    XQ_EQ_I64(XTextFormat_intProperty(&b, XTextFormatProperty_BlockAlignment),
              XTDAlignment_Justify);
    XQ_EQ_I64(XTextBlockFormat_alignment(&b), XTDAlignment_Justify);
    /* alignment 未设（0）回落 AlignLeft=0x01（h:622-624） */
    {
        XTextBlockFormat b2;
        XTextBlockFormat_init(&b2);
        XQ_EQ_I64(XTextBlockFormat_alignment(&b2), 0x01);
        XTextFormat_deinit(&b2);
    }

    /* ListStyle 数值一致：ListDisc=-1 …（h:733-743） */
    XTextListFormat_setStyle(&l, XTextFormatList_Decimal);
    XQ_EQ_I64(XTextListFormat_style(&l), -4);
    XTextListFormat_setNumberPrefix(&l, "(");
    XTextListFormat_setNumberSuffix(&l, ")");
    XQ_EQ_STR(XTextListFormat_numberPrefix(&l), "(");
    XQ_EQ_STR(XTextListFormat_numberSuffix(&l), ")");

    XTextFormat_deinit(&c);
    XTextFormat_deinit(&b);
    XTextFormat_deinit(&l);
    XTextFormat_deinit(&img);
    XTextFormat_deinit(&plain);
}

/* ==================== ①merge ==================== */

XQ_TEST(XTextFormat, merge)
{
    XTextFormat base;
    XTextFormat over;
    XTextFormat other;

    /* type 不同不动（cpp:943-945） */
    XTextFormat_init(&base, XTextFormatType_CharFormat);
    XTextFormat_init(&other, XTextFormatType_BlockFormat);
    XTextFormat_setProperty_int(&other, XTextFormatProperty_FontWeight, 700);
    XTextFormat_merge(&base, &other);
    XQ_CHECK(XTextFormat_isEmpty(&base));
    XTextFormat_deinit(&other);

    /* self 空 → 全量接收（cpp:947-950 的值语义等价）；冲突 other 优先 */
    XTextFormat_init(&over, XTextFormatType_CharFormat);
    XTextFormat_setProperty_int(&over, XTextFormatProperty_FontWeight, 700);
    XTextFormat_setProperty_string(&over, XTextFormatProperty_AnchorHref,
                                   "x");
    XTextFormat_setProperty_double(&over, XTextFormatProperty_FontPointSize,
                                   9.0);
    XTextFormat_merge(&base, &over);
    XQ_CHECK(XTextFormat_equal(&base, &over));
    XQ_EQ_I64(XTextFormat_intProperty(&base, XTextFormatProperty_FontWeight),
              700);
    XTF_EQ_DBL(XTextFormat_doubleProperty(&base,
                                          XTextFormatProperty_FontPointSize),
               9.0);
    XTextFormat_deinit(&over);

    /* 冲突 other 优先（cpp:958-960）、非冲突键保留 */
    XTextFormat_init(&over, XTextFormatType_CharFormat);
    XTextFormat_setProperty_int(&over, XTextFormatProperty_FontWeight, 500);
    XTextFormat_merge(&base, &over);
    XQ_EQ_I64(XTextFormat_intProperty(&base, XTextFormatProperty_FontWeight),
              500);
    XTF_EQ_DBL(XTextFormat_doubleProperty(&base,
                                          XTextFormatProperty_FontPointSize),
               9.0); /* 未冲突键不动 */
    XTextFormat_deinit(&over);

    /* 无效值清键（cpp:960-962：value 无效 → clearProperty）：
     * 先经 setProperty_value(id, NULL) 落一条无效值条目（Qt 同样无条件存） */
    {
        XTextFormat withNull;
        XTextFormat_init(&withNull, XTextFormatType_CharFormat);
        XTextFormat_setProperty_value(&withNull,
                                      XTextFormatProperty_AnchorHref, NULL);
        XQ_CHECK(XTextFormat_hasProperty(&withNull,
                                         XTextFormatProperty_AnchorHref));
        {
            const XVariant* v =
                XTextFormat_property(&withNull, XTextFormatProperty_AnchorHref);
            XQ_CHECK_PTR(v);
            XQ_EQ_I64((long long)v->m_type, (long long)XVariantType_NULL);
        }
        XTextFormat_merge(&base, &withNull);
        XQ_CHECK(!XTextFormat_hasProperty(&base,
                                          XTextFormatProperty_AnchorHref));
        XTextFormat_deinit(&withNull);
    }

    /* other 空表不动（cpp:952-954） */
    XTextFormat_init(&other, XTextFormatType_CharFormat);
    XTextFormat_merge(&base, &other);
    XQ_EQ_I64(XTextFormat_propertyCount(&base), 2);
    XTextFormat_deinit(&other);

    XTextFormat_deinit(&base);
}

XQ_TEST(XTextFormat, equal)
{
    XTextFormat a;
    XTextFormat b;
    XTextFormat_init(&a, XTextFormatType_CharFormat);
    XTextFormat_init(&b, XTextFormatType_CharFormat);

    /* 双空表相等（含无表 vs 空表不区分，cpp:1368-1373） */
    XQ_CHECK(XTextFormat_equal(&a, &b));
    XQ_CHECK(XTextFormat_equal(NULL, NULL));
    XQ_CHECK(!XTextFormat_equal(&a, NULL));

    XTextFormat_setProperty_int(&a, XTextFormatProperty_FontWeight, 700);
    XQ_CHECK(!XTextFormat_equal(&a, &b));
    XTextFormat_setProperty_int(&b, XTextFormatProperty_FontWeight, 700);
    XQ_CHECK(XTextFormat_equal(&a, &b));

    /* 键序无关：清了重设同键仍在同位置（线性表覆盖不增条）——换一条验证
     * 值不等即不等 */
    XTextFormat_setProperty_string(&a, XTextFormatProperty_AnchorHref, "p");
    XQ_CHECK(!XTextFormat_equal(&a, &b));
    XTextFormat_setProperty_string(&b, XTextFormatProperty_AnchorHref, "q");
    XQ_CHECK(!XTextFormat_equal(&a, &b));
    XTextFormat_setProperty_string(&b, XTextFormatProperty_AnchorHref, "p");
    XQ_CHECK(XTextFormat_equal(&a, &b));

    /* type 不同不等（cpp:1364-1366） */
    b.m_type = XTextFormatType_BlockFormat;
    XQ_CHECK(!XTextFormat_equal(&a, &b));

    XTextFormat_deinit(&a);
    XTextFormat_deinit(&b);
}

/* ==================== ④ copy/move/deinit 账目 ==================== */

XQ_TEST(XTextFormat, copyMoveAccount)
{
    XTextFormat src;
    XTextFormat dst;
    XTextFormat mv;

    XTextFormat_init(&src, XTextFormatType_CharFormat);
    XTextFormat_setProperty_string(&src, XTextFormatProperty_FontFamilies,
                                   "Serif");
    XTextFormat_setProperty_int(&src, XTextFormatProperty_FontWeight, 700);
    XTextFormat_setProperty_color(&src, XTextFormatProperty_ForegroundBrush,
                                  0xFF112233u);

    /* copy：深拷贝（源释放后目标仍可读） */
    XTextFormat_init(&dst, XTextFormatType_InvalidFormat);
    XTextFormat_copy(&dst, &src);
    XQ_CHECK(XTextFormat_equal(&dst, &src));
    XTextFormat_deinit(&src);
    XQ_EQ_STR(XTextCharFormat_fontFamily(&dst), "Serif");
    XQ_EQ_I64(XTextFormat_intProperty(&dst, XTextFormatProperty_FontWeight),
              700);
    XQ_EQ_I64((long long)XTextFormat_colorProperty(
                  &dst, XTextFormatProperty_ForegroundBrush),
              (long long)0xFF112233u);

    /* copy(NULL)：dst 复位空壳（防泄漏契约） */
    XTextFormat_copy(&dst, NULL);
    XQ_CHECK(XTextFormat_isEmpty(&dst));
    XQ_EQ_I64(XTextFormat_type(&dst), XTextFormatType_InvalidFormat);

    /* move：载荷转移、源空壳（_move 转移留壳契约） */
    XTextFormat_init(&src, XTextFormatType_CharFormat);
    XTextFormat_setProperty_string(&src, XTextFormatProperty_FontFamilies,
                                   "Mono");
    XTextFormat_setProperty_int(&src, XTextFormatProperty_FontWeight, 600);
    XTextFormat_init(&mv, XTextFormatType_CharFormat);
    XTextFormat_move(&mv, &src);
    XQ_EQ_I64(XTextFormat_propertyCount(&mv), 2);
    XQ_EQ_STR(XTextCharFormat_fontFamily(&mv), "Mono");
    XQ_EQ_I64(XTextFormat_intProperty(&mv, XTextFormatProperty_FontWeight),
              600);
    XQ_CHECK(XTextFormat_isEmpty(&src)); /* 空壳仍由调用方管理（已 deinit 态） */
    XTextFormat_deinit(&src);            /* 空壳 deinit 幂等 */
    XTextFormat_deinit(&mv);

    /* make 值返回便利 */
    {
        XTextFormat f = XTextFormat_make(XTextFormatType_ListFormat);
        XQ_EQ_I64(XTextFormat_type(&f), 3);
        XTextFormat_deinit(&f);
    }
    /* 重复 deinit 幂等 */
    XTextFormat_deinit(&dst);
    XTextFormat_deinit(&dst);
}

XQ_TEST(XTextFormat, nullGuards)
{
    /* 全入口 NULL 守卫（值对象安全性） */
    XQ_EQ_I64(XTextFormat_type(NULL), XTextFormatType_InvalidFormat);
    XQ_CHECK(!XTextFormat_isValid(NULL));
    XQ_CHECK(XTextFormat_isEmpty(NULL));
    XQ_EQ_I64(XTextFormat_propertyCount(NULL), 0);
    XQ_CHECK(!XTextFormat_hasProperty(NULL, 0x2003));
    XQ_CHECK(XTextFormat_property(NULL, 0x2003) == NULL);
    XQ_EQ_I64(XTextFormat_intProperty(NULL, 0x2003), 0);
    XTF_EQ_DBL(XTextFormat_doubleProperty(NULL, 0x2001), 0.0);
    XQ_CHECK(!XTextFormat_boolProperty(NULL, 0x2004));
    XQ_EQ_STR(XTextFormat_stringProperty(NULL, 0x2031), "");
    XQ_EQ_I64((long long)XTextFormat_colorProperty(NULL, 0x821), 0);
    XQ_CHECK(XTextCharFormat_fontFamily(NULL) == NULL);
    XQ_EQ_I64(XTextCharFormat_fontWeight(NULL), 400);
    XTF_EQ_DBL(XTextCharFormat_superScriptBaseline(NULL), 50.0);
    XTF_EQ_DBL(XTextCharFormat_subScriptBaseline(NULL), 100.0 / 6.0);
    XTF_EQ_DBL(XTextCharFormat_baselineOffset(NULL), 0.0);
    XQ_EQ_I64(XTextBlockFormat_alignment(NULL), 0x01);
    XQ_EQ_I64(XTextListFormat_style(NULL), 0);
    XQ_EQ_STR(XTextListFormat_numberPrefix(NULL), "");
    XQ_CHECK(!XTextFormat_isImageFormat(NULL));
    XTextFormat_setProperty_int(NULL, 0x2003, 1);          /* 不崩 */
    XTextFormat_setProperty_string(NULL, 0x2031, "x");     /* 不崩 */
    XTextFormat_clearProperty(NULL, 0x2003);               /* 不崩 */
    XTextFormat_merge(NULL, NULL);                         /* 不崩 */
    XQ_CHECK(XTextFormat_equal(NULL, NULL));
    XTextFormat_deinit(NULL);                              /* 不崩 */
    /* convert 类 NULL 目标守卫 */
    XTextCharFormat_convert(NULL, NULL);
    XTextBlockFormat_convert(NULL, NULL);
}

/* ==================== CharFormat 类型化入口 ==================== */

XQ_TEST(XTextCharFormat, typedApi)
{
    XTextCharFormat c;
    XTextCharFormat_init(&c);

    XTextCharFormat_setFontWeight(&c, 700);
    XQ_EQ_I64(XTextCharFormat_fontWeight(&c), 700);
    XTextCharFormat_setFontItalic(&c, true);
    XQ_CHECK(XTextCharFormat_fontItalic(&c));
    XTextCharFormat_setFontStrikeOut(&c, true);
    XQ_CHECK(XTextCharFormat_fontStrikeOut(&c));
    XTextCharFormat_setFontOverline(&c, true);
    XQ_CHECK(XTextCharFormat_fontOverline(&c));
    XTextCharFormat_setFontFixedPitch(&c, true);
    XQ_CHECK(XTextCharFormat_fontFixedPitch(&c));
    XTextCharFormat_setFontPixelSize(&c, 20);
    XQ_EQ_I64(XTextCharFormat_fontPixelSize(&c), 20);

    XTextCharFormat_setVerticalAlignment(
        &c, XTextFormatVerticalAlign_SuperScript);
    XQ_EQ_I64(XTextCharFormat_verticalAlignment(&c),
              XTextFormatVerticalAlign_SuperScript);

    XTextCharFormat_setSuperScriptBaseline(&c, 70.0);
    XTF_EQ_DBL(XTextCharFormat_superScriptBaseline(&c), 70.0);
    XTextCharFormat_setSubScriptBaseline(&c, 20.0);
    XTF_EQ_DBL(XTextCharFormat_subScriptBaseline(&c), 20.0);
    XTextCharFormat_setBaselineOffset(&c, -5.0);
    XTF_EQ_DBL(XTextCharFormat_baselineOffset(&c), -5.0);

    XTextCharFormat_setAnchor(&c, true);
    XQ_CHECK(XTextCharFormat_isAnchor(&c));
    XTextCharFormat_setAnchorHref(&c, "a.md#s1");
    XQ_EQ_STR(XTextCharFormat_anchorHref(&c), "a.md#s1");

    XTextCharFormat_setForeground(&c, 0xFF0000FFu);
    XQ_EQ_I64((long long)XTextCharFormat_foreground(&c),
              (long long)0xFF0000FFu);
    XTextCharFormat_clearForeground(&c);
    XQ_EQ_I64((long long)XTextCharFormat_foreground(&c), 0);
    XTextCharFormat_setBackground(&c, 0x88FFFF00u);
    XQ_EQ_I64((long long)XTextCharFormat_background(&c),
              (long long)0x88FFFF00u);
    XTextCharFormat_clearBackground(&c);
    XQ_EQ_I64((long long)XTextCharFormat_background(&c), 0);
    XTextCharFormat_setUnderlineColor(&c, 0xFFFF0000u);
    XQ_EQ_I64((long long)XTextCharFormat_underlineColor(&c),
              (long long)0xFFFF0000u);

    /* make 值返回 + convert 类型改写 */
    {
        XTextFormat generic = XTextFormat_make(XTextFormatType_BlockFormat);
        XTextCharFormat c2;
        XTextFormat_setProperty_int(&generic, XTextFormatProperty_FontWeight,
                                    300);
        XTextCharFormat_init(&c2);
        XTextCharFormat_convert(&c2, &generic);
        XQ_EQ_I64(XTextFormat_type(&c2), 2); /* 改写为 CharFormat */
        XQ_EQ_I64(XTextFormat_intProperty(&c2, XTextFormatProperty_FontWeight),
                  300); /* 属性随拷贝 */
        XTextFormat_deinit(&generic);
        XTextFormat_deinit(&c2);
    }
    XTextFormat_deinit(&c);
}

/* ==================== Block/List/Image 类型化入口 ==================== */

XQ_TEST(XTextBlockFormat, typedApi)
{
    XTextBlockFormat b;
    XTextBlockFormat_init(&b);

    XTextBlockFormat_setAlignment(&b, XTDAlignment_HCenter);
    XQ_EQ_I64(XTextBlockFormat_alignment(&b), XTDAlignment_HCenter);
    XTextBlockFormat_setTopMargin(&b, 1.5);
    XTextBlockFormat_setBottomMargin(&b, 2.5);
    XTextBlockFormat_setLeftMargin(&b, 3.5);
    XTextBlockFormat_setRightMargin(&b, 4.5);
    XTF_EQ_DBL(XTextBlockFormat_topMargin(&b), 1.5);
    XTF_EQ_DBL(XTextBlockFormat_bottomMargin(&b), 2.5);
    XTF_EQ_DBL(XTextBlockFormat_leftMargin(&b), 3.5);
    XTF_EQ_DBL(XTextBlockFormat_rightMargin(&b), 4.5);
    XTextBlockFormat_setTextIndent(&b, -8.0);
    XTF_EQ_DBL(XTextBlockFormat_textIndent(&b), -8.0);
    XTextBlockFormat_setIndent(&b, 3);
    XQ_EQ_I64(XTextBlockFormat_indent(&b), 3);
    XTextBlockFormat_setHeadingLevel(&b, 2);
    XQ_EQ_I64(XTextBlockFormat_headingLevel(&b), 2);
    XTextBlockFormat_setLineHeight(&b, 150.0,
                                   XTextFormatLineHeight_ProportionalHeight);
    XTF_EQ_DBL(XTextBlockFormat_lineHeight(&b), 150.0);
    XQ_EQ_I64(XTextBlockFormat_lineHeightType(&b),
              XTextFormatLineHeight_ProportionalHeight);
    XTextFormat_deinit(&b);
}

XQ_TEST(XTextListFormat, typedApi)
{
    XTextListFormat l;
    XTextFormat plain;

    XTextListFormat_init(&l);
    XTextListFormat_setStyle(&l, XTextFormatList_UpperRoman);
    XQ_EQ_I64(XTextListFormat_style(&l), -8);
    XTextListFormat_setIndent(&l, 4);
    XQ_EQ_I64(XTextListFormat_indent(&l), 4);
    XTextListFormat_setStart(&l, 7);
    XQ_EQ_I64(XTextListFormat_start(&l), 7);
    XTextFormat_deinit(&l);

    /* convert 不补构造缺省（对齐 QTextListFormat(const QTextFormat&) 直取） */
    XTextFormat_init(&plain, XTextFormatType_BlockFormat);
    XTextListFormat_init(&l);
    XTextListFormat_convert(&l, &plain);
    XQ_EQ_I64(XTextFormat_type(&l), 3);
    XQ_CHECK(XTextFormat_isEmpty(&l)); /* 源空 → 不落 indent/start */
    XTextFormat_deinit(&plain);
    XTextFormat_deinit(&l);
}

XQ_TEST(XTextImageFormat, typedApi)
{
    XTextImageFormat img;
    XTextImageFormat_init(&img);
    XQ_CHECK(XTextFormat_isImageFormat(&img));
    XTextImageFormat_setName(&img, "pic.png");
    XQ_EQ_STR(XTextImageFormat_name(&img), "pic.png");
    XTextImageFormat_setWidth(&img, 64.0);
    XTextImageFormat_setHeight(&img, 32.0);
    XTF_EQ_DBL(XTextImageFormat_width(&img), 64.0);
    XTF_EQ_DBL(XTextImageFormat_height(&img), 32.0);
    XTextFormat_deinit(&img);
}

/* ==================== ③ 文档格式池 ==================== */

XQ_TEST(XTextDocument, formatPool)
{
    XTextDocument* doc;
    XTDCharFormat fmt;
    const XTextFormat* f;

    doc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(doc != NULL);
    /* 空文档：池 0 */
    XQ_EQ_I64(XTextDocument_formatPoolCount(doc), 0);
    XQ_EQ_I64(XTextDocument_allFormats(doc, NULL, 0), 0);

    /* 同一格式两个片段 → 池 1 条、片段索引相同（去重） */
    XMemset(&fmt, 0, sizeof(fmt));
    fmt.bold = 1u;
    fmt.fgColor = 0xFF00FF00u;
    XQ_EQ_I64(XTextDocument_addFragment(doc, 0, "ab", &fmt), 0);
    XQ_EQ_I64(XTextDocument_addFragment(doc, 0, "cd", &fmt), 1);
    XQ_EQ_I64(XTextDocument_formatPoolCount(doc), 1);
    XQ_EQ_I64(XTextDocument_fragmentPoolIndex(doc, 0, 0),
              XTextDocument_fragmentPoolIndex(doc, 0, 1));
    XQ_EQ_I64(XTextDocument_fragmentPoolIndex(doc, 0, 0), 0);

    /* 池条目与内联格式逐字对得上（FontWeight=700、ForegroundBrush 同色） */
    XQ_EQ_I64(XTextDocument_allFormats(doc, &f, 1), 1);
    XQ_REQUIRE(f != NULL);
    XQ_EQ_I64(XTextCharFormat_fontWeight(f), 700);
    XQ_EQ_I64((long long)XTextCharFormat_foreground(f),
              (long long)0xFF00FF00u);

    /* 修改片段格式 → 池 2 条（旧条目保留：Qt formats[] 空洞同口径）；
     * 片段索引指向新条目 */
    XTextDocument_setFragmentBold(doc, 0, 1, false);
    XQ_EQ_I64(XTextDocument_formatPoolCount(doc), 2);
    {
        /* allFormats 的 out 是「指针数组」：cap=2 必须给 2 元数组
         * （单指针 f 取地址只装得下 cap=1，见上方用法） */
        const XTextFormat* fa[2];
        int i0 = XTextDocument_fragmentPoolIndex(doc, 0, 0);
        int i1 = XTextDocument_fragmentPoolIndex(doc, 0, 1);
        XQ_CHECK(i0 != i1);
        XQ_REQUIRE(i1 >= 0 && i1 < 2);
        XQ_EQ_I64(XTextDocument_allFormats(doc, fa, 2), 2);
        XQ_EQ_I64(XTextCharFormat_fontWeight(fa[i1]), 400);
        XQ_EQ_I64((long long)XTextCharFormat_foreground(fa[i1]),
                  (long long)0xFF00FF00u); /* 未改字段保持 */
    }

    /* 渲染路径逐字等价：内联 fmt 字段照旧可读（红线） */
    {
        const XTDFragment* frag = XTextDocument_fragment(doc, 0, 1);
        XQ_REQUIRE(frag != NULL);
        XQ_CHECK(frag->fmt.bold == 0u);
        XQ_EQ_I64((long long)frag->fmt.fgColor, (long long)0xFF00FF00u);
    }

    /* setHtml 解析路径同池（xtd_appendSpan 新建片段也登记） */
    XTextDocument_setHtml(doc, "<p><b>x</b><i>y</i></p>");
    XQ_CHECK(XTextDocument_formatPoolCount(doc) >= 2);
    {
        const XTDFragment* fb = XTextDocument_fragment(doc, 0, 0);
        const XTDFragment* fi = XTextDocument_fragment(doc, 0, 1);
        XQ_REQUIRE(fb != NULL);
        XQ_REQUIRE(fi != NULL);
        XQ_CHECK(fb->fmt.bold == 1u);
        XQ_CHECK(fi->fmt.italic == 1u);
        XQ_CHECK(XTextDocument_fragmentPoolIndex(doc, 0, 0) >= 0);
        XQ_CHECK(XTextDocument_fragmentPoolIndex(doc, 0, 1) >= 0);
    }

    /* clear 后池清空 */
    XTextDocument_clear(doc);
    XQ_EQ_I64(XTextDocument_formatPoolCount(doc), 0);

    /* clone：池随片段重建、渲染字段一致 */
    XTextDocument_setHtml(doc, "<p><b>bold</b><span style=\"color:#ff0000\">"
                               "red</span></p>");
    {
        XTextDocument* cp = XTextDocument_clone(doc);
        XQ_REQUIRE(cp != NULL);
        XQ_EQ_I64(XTextDocument_formatPoolCount(cp),
                  XTextDocument_formatPoolCount(doc));
        {
            const XTDFragment* sf = XTextDocument_fragment(doc, 0, 0);
            const XTDFragment* df = XTextDocument_fragment(cp, 0, 0);
            XQ_REQUIRE(sf != NULL);
            XQ_REQUIRE(df != NULL);
            XQ_CHECK(sf->fmt.bold == df->fmt.bold);
            XQ_CHECK(sf->fmt.fgColor == df->fmt.fgColor);
            XQ_CHECK(XTextDocument_fragmentPoolIndex(cp, 0, 0) >= 0);
        }
        XClassDelete((XClass*)cp);
    }
    XClassDelete((XClass*)doc);
}

XQ_TEST(XTextDocument, charFormatBridge)
{
    XTextDocument* doc;
    XTDCharFormat src;
    XTDCharFormat back;
    XTextFormat fmt;

    doc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(doc != NULL);

    /* 反向桥的输出壳必须先清零（桥按「调用方已提供空壳」契约先清旧
     * 字符串字段——未清零栈实例的字段是栈垃圾指针，xtd_formatClear
     * 会对其 XClassDelete）。 */
    XMemset(&back, 0, sizeof(back));

    /* 正向：内联 → 属性 */
    XMemset(&src, 0, sizeof(src));
    src.fontFamily = XString_create_utf8("Noto Serif CJK SC");
    src.anchorHref = XString_create_utf8("https://x.y/");
    src.bold = 1u;
    src.italic = 1u;
    src.underline = 1u;
    src.strikeOut = 1u;
    src.superScript = 1u;
    src.fgColor = 0xFF123456u;
    src.bgColor = 0x00ABCDEFu;
    src.fontPointSize = 18;
    XTextDocument_charFormatToTextFormat(&fmt, &src);
    XQ_EQ_I64(XTextFormat_type(&fmt), 2);
    XQ_EQ_STR(XTextCharFormat_fontFamily(&fmt), "Noto Serif CJK SC");
    XQ_EQ_STR(XTextCharFormat_anchorHref(&fmt), "https://x.y/");
    XQ_EQ_I64(XTextCharFormat_fontWeight(&fmt), 700);
    XQ_CHECK(XTextCharFormat_fontItalic(&fmt));
    XQ_CHECK(XTextCharFormat_fontUnderline(&fmt));
    XQ_CHECK(XTextCharFormat_fontStrikeOut(&fmt));
    XQ_EQ_I64(XTextCharFormat_verticalAlignment(&fmt),
              XTextFormatVerticalAlign_SuperScript);
    XQ_EQ_I64((long long)XTextCharFormat_foreground(&fmt),
              (long long)0xFF123456u);
    XQ_EQ_I64((long long)XTextCharFormat_background(&fmt),
              (long long)0x00ABCDEFu);
    XTF_EQ_DBL(XTextCharFormat_fontPointSize(&fmt), 18.0);

    /* 反向：属性 → 内联，逐字段等价（渲染路径读取等价验证） */
    XTextDocument_textFormatToCharFormat(&back, &fmt);
    XQ_CHECK(back.fontFamily != NULL);
    XQ_EQ_STR(XString_toUtf8(back.fontFamily), "Noto Serif CJK SC");
    XQ_CHECK(back.anchorHref != NULL);
    XQ_EQ_STR(XString_toUtf8(back.anchorHref), "https://x.y/");
    XQ_CHECK(back.bold == 1u);
    XQ_CHECK(back.italic == 1u);
    XQ_CHECK(back.underline == 1u);
    XQ_CHECK(back.strikeOut == 1u);
    XQ_CHECK(back.superScript == 1u);
    XQ_CHECK(back.subScript == 0u);
    XQ_EQ_I64((long long)back.fgColor, (long long)0xFF123456u);
    XQ_EQ_I64((long long)back.bgColor, (long long)0x00ABCDEFu);
    XQ_EQ_I64((long long)back.fontPointSize, 18);
    xtf_freeXTD(&back); /* 测试侧释放反向桥新建的字符串字段 */

    /* 字重→bold 阈值：400→非粗、500→粗 */
    XTextFormat_init(&fmt, XTextFormatType_CharFormat);
    XTextFormat_setProperty_int(&fmt, XTextFormatProperty_FontWeight, 400);
    XTextDocument_textFormatToCharFormat(&back, &fmt);
    XQ_CHECK(back.bold == 0u);
    xtf_freeXTD(&back);
    XTextFormat_setProperty_int(&fmt, XTextFormatProperty_FontWeight, 500);
    XTextDocument_textFormatToCharFormat(&back, &fmt);
    XQ_CHECK(back.bold == 1u);
    xtf_freeXTD(&back);
    /* TextUnderlineStyle 读序（fontUnderline 同序）：非 Single 风格→非下划线 */
    XTextFormat_setProperty_int(&fmt,
                                XTextFormatProperty_TextUnderlineStyle,
                                XTextFormatUnderline_DotLine);
    XTextDocument_textFormatToCharFormat(&back, &fmt);
    XQ_CHECK(back.underline == 0u);
    xtf_freeXTD(&back);
    XTextFormat_deinit(&fmt);

    /* 空/NULL 桥守卫 */
    XTextDocument_charFormatToTextFormat(&fmt, NULL);
    XQ_CHECK(XTextFormat_isEmpty(&fmt));
    XTextDocument_textFormatToCharFormat(&back, NULL);
    XQ_CHECK(back.fontFamily == NULL && back.anchorHref == NULL);

    /* src 的字符串字段归本用例释放（桥只读不接管） */
    if (src.fontFamily) XClassDelete(src.fontFamily);
    if (src.anchorHref) XClassDelete(src.anchorHref);

    XClassDelete((XClass*)doc);
}

/* ==================== markContentsDirty ==================== */

XQ_TEST(XTextDocument, markContentsDirty)
{
    XTextDocument* doc =
        XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(doc != NULL);

    /* 初值无脏区 */
    XQ_CHECK(!XTextDocument_isContentsDirty(doc));
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), -1);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 0);

    /* 首次登记：from=10 length=5 */
    XTextDocument_markContentsDirty(doc, 10, 5);
    XQ_CHECK(XTextDocument_isContentsDirty(doc));
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), 10);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 5);

    /* 区间合并（qtextdocument_p.cpp:1238-1243）：前扩 → from=0,
     * end=max(3,15)=15, diff=max(0,15-0-5)=10 → length=15 */
    XTextDocument_markContentsDirty(doc, 0, 3);
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), 0);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 15);

    /* 内含区间不扩：from=5 length=4 → end=max(9,15)=15, diff=0 → 不变 */
    XTextDocument_markContentsDirty(doc, 5, 4);
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), 0);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 15);

    /* 后扩：from=20 length=2 → end=max(22,15)=22, diff=7 → length=22 */
    XTextDocument_markContentsDirty(doc, 20, 2);
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), 0);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 22);

    /* 复位（布局消费方收口；Qt lout 回调后 docChangeFrom=-1 同款） */
    XTextDocument_resetContentsDirty(doc);
    XQ_CHECK(!XTextDocument_isContentsDirty(doc));
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), -1);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 0);

    /* 复位后重新登记走首次分支 */
    XTextDocument_markContentsDirty(doc, 7, 2);
    XQ_EQ_I64(XTextDocument_contentsDirtyFrom(doc), 7);
    XQ_EQ_I64(XTextDocument_contentsDirtyLength(doc), 2);

    /* NULL 守卫 */
    XTextDocument_markContentsDirty(NULL, 0, 0);
    XQ_CHECK(!XTextDocument_isContentsDirty(NULL));
    XTextDocument_resetContentsDirty(NULL);

    XClassDelete((XClass*)doc);
}

/* ==================== 既有渲染路径回归护栏（红线） ==================== */

XQ_TEST(XTextDocument, renderFieldsUnchanged)
{
    XTextDocument* doc =
        XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTDCharFormat fmt;
    XQ_REQUIRE(doc != NULL);

    /* setHtml → toHtml 往返 + 内联字段逐字读取（格式池影子层不干扰；
     * span style 子集仅 background-color，前景色走 <font color>） */
    XTextDocument_setHtml(doc, "<p>plain <b>bold</b> <i>it</i> "
                               "<u>ul</u> <s>st</s> "
                               "<font color=\"#ff0000\">red</font> "
                               "<sup>sup</sup> <sub>sub</sub></p>");
    {
        char* html = XTextDocument_toHtml(doc);
        XQ_REQUIRE(html != NULL);
        XQ_CHECK(strstr(html, "<b>") != NULL);
        XQ_CHECK(strstr(html, "<i>") != NULL);
        XQ_CHECK(strstr(html, "<u>") != NULL);
        XQ_CHECK(strstr(html, "<s>") != NULL);
        XQ_CHECK(strstr(html, "ff0000") != NULL);
        XQ_CHECK(strstr(html, "<sup>") != NULL);
        XQ_CHECK(strstr(html, "<sub>") != NULL);
        XFree_System(html);
    }
    {
        const XTDFragment* f0 = XTextDocument_fragment(doc, 0, 0);
        XQ_REQUIRE(f0 != NULL);
        XQ_CHECK(f0->fmt.bold == 0u && f0->fmt.italic == 0u);
        XQ_CHECK(XTextDocument_fragmentPoolIndex(doc, 0, 0) >= 0);
    }
    /* setPlainText 走 xtd_clearBlocksOnly（池清空）→ fill 装入片段重新
     * 入池：装入的空格式片段占 idx 0，addFragment 的 italic 片段占 idx 1 */
    XMemset(&fmt, 0, sizeof(fmt));
    fmt.italic = 1u;
    XTextDocument_setPlainText(doc, "hello");
    XQ_EQ_I64(XTextDocument_formatPoolCount(doc), 1);
    XQ_EQ_I64(XTextDocument_fragmentPoolIndex(doc, 0, 0), 0);
    XTextDocument_addFragment(doc, 0, "world", &fmt);
    XQ_EQ_I64(XTextDocument_formatPoolCount(doc), 2);
    {
        const XTDFragment* f1 = XTextDocument_fragment(doc, 0, 1);
        XQ_REQUIRE(f1 != NULL);
        XQ_CHECK(f1->fmt.italic == 1u);
        XQ_EQ_I64(XTextDocument_fragmentPoolIndex(doc, 0, 1), 1);
        /* 池条目逐条核对：idx0=装入片段的零值格式(桥确定性映射恒落
         * bool 组四属性,非空表)、idx1=FontItalic=true;
         * fragmentPoolIndex 取回的条目与片段格式一致（索引真值非残留假 0） */
        {
            const XTextFormat* pf[2];
            XQ_EQ_I64(XTextDocument_allFormats(doc, pf, 2), 2);
            XQ_EQ_I64(XTextFormat_propertyCount(pf[0]), 4);
            XQ_EQ_I64(XTextCharFormat_fontWeight(pf[0]), 400);
            XQ_CHECK(!XTextFormat_boolProperty(
                pf[0], XTextFormatProperty_FontItalic));
            XQ_CHECK(XTextFormat_boolProperty(
                pf[1], XTextFormatProperty_FontItalic));
            XQ_CHECK(XTextFormat_boolProperty(
                pf[XTextDocument_fragmentPoolIndex(doc, 0, 1)],
                XTextFormatProperty_FontItalic));
        }
    }
    /* undo 快照恢复（setPlainTextNoHistory→fill 路径）后池与内容同步
     * 重建：恢复 "hello" 单片段空格式——池 1 条、索引真值 0、italic=0 */
    XTextDocument_undo(doc);
    {
        char* plain = XTextDocument_toPlainText(doc);
        XQ_REQUIRE(plain != NULL);
        XQ_EQ_STR(plain, "hello");
        XFree_System(plain);
    }
    XQ_EQ_I64(XTextDocument_formatPoolCount(doc), 1);
    {
        const XTDFragment* fu = XTextDocument_fragment(doc, 0, 0);
        const XTextFormat* pu = NULL;
        XQ_REQUIRE(fu != NULL);
        XQ_CHECK(fu->fmt.italic == 0u); /* 快照文本无格式 */
        XQ_EQ_I64(XTextDocument_fragmentPoolIndex(doc, 0, 0), 0);
        XQ_EQ_I64(XTextDocument_allFormats(doc, &pu, 1), 1);
        XQ_REQUIRE(pu != NULL);
        /* 池条目=零值内联格式的桥映射(bool 组四属性、无色无族),与内联一致 */
        XQ_EQ_I64(XTextFormat_propertyCount(pu), 4);
        XQ_EQ_I64(XTextCharFormat_fontWeight(pu), 400);
        XQ_CHECK(!XTextFormat_boolProperty(
            pu, XTextFormatProperty_FontItalic));
        XQ_EQ_I64((long long)XTextCharFormat_foreground(pu), 0);
        XQ_CHECK(XTextCharFormat_fontFamily(pu) == NULL);
    }
    XClassDelete((XClass*)doc);
}

#endif /* XGUI_ON && XTEXTDOCUMENT_ON */
