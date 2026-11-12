/**
 * @file       XFontComboBoxAlignTest.c
 * @brief      XFontComboBox × Qt 6.8 QFontComboBox 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qfontcombobox.h
 *               （writingSystem/setWritingSystem、FontFilter 位标志
 *               AllFonts=0/ScalableFonts=1/NonScalableFonts=2/
 *               MonospacedFonts=4/ProportionalFonts=8、fontFilters、
 *               currentFont/setCurrentFont、sizeHint override、
 *               sampleTextForSystem/ForFont 与 setDisplayFont/displayFont、
 *               currentFontChanged 信号）；
 *             - Qt 6.8.3 源码 qfontcombobox.cpp：
 *               构造 setEditable(true)（:403）、
 *               sizeHint = QComboBox::sizeHint 且宽度强制为
 *               `fontMetrics().horizontalAdvance('m') * 14`（:544-550，
 *               即宽度**不随最长字体族名变化**）、
 *               currentChanged 槽按族名去重后发 currentFontChanged
 *               （:352-360）、setWritingSystem/setFontFilters 触发
 *               updateModel 重建族列表（:434-511 / updateModel:307-370）、
 *               弹层宽 = min(width()*5/3, 屏宽)（:526-537）；
 *             - 本机 Qt 6.8.0 + offscreen 探针实测：
 *               fonts: setFontFilters(MonospacedFonts) 后 count 276→8、
 *               currentIndex 回到 0 且发 currentFontChanged；
 *               setWritingSystem(SimplifiedChinese) 后 count 276→33；
 *               setCurrentFont(QFont("dejavu serif")) 按大小写不敏感选中
 *               "DejaVu Serif"；未知族名经 QFontInfo 解析成缺省族；
 *               displayFont(family) 已登记返回登记字体、未登记返回该族
 *               QFont；字体组合框 120 宽弹层 200（普通组合框 120）。
 *             - 已知差异（gaps）：书写系统过滤缺逐族标注数据（scalable/
 *               fixedPitch 以 face 种类 + 度量为等价探测）。
 *             - 环境口径：族列表来自 XPlatformFontDatabase（Linux 上是
 *               fontconfig 的全系统可缩放族），而 XFont 的 FT 解析只按
 *               XFONT_EXTERNAL_FT_FONT_DIR/exeDir 找 `<family>.ttf`；系统
 *               族名没有同名文件时全部回退内置点阵 face，等宽/可缩放过滤
 *               结果为空（实现按「未知族保守判非等宽/非轮廓」处理）。
 *               故过滤断言写成「结果 == 按同一谓词独立复算的族数」，不
 *               硬编码 monoCount>0（那只在字体文件可按族名解析的机器上
 *               成立）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON

#include "XFontComboBox.h"
#include "XComboBox.h"
#include "XLineEdit.h"
#include "XAbstractItemView.h" /* P3-62：弹层 FontRole 断言。 */
#include "XListView.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVarList.h"
#include "XString.h"
#include "XStringUtils.h" /* XStrcmp/XStrlen（族名大小写用例）。 */
#include "XFont.h"
#include "XFontFace.h" /* XFont_face/XFontFace_kind（过滤器谓词独立复算）。 */
#include "XPainter.h"
#include "XClass.h"

/* ==================== 信号探针 ==================== */

static int fc_fontChanged = 0; /**< currentFontChanged 次数。 */

static void fc_fontChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++fc_fontChanged;
}

/* ==================== ① 构造缺省（可编辑）+ 族名选择 ================ */

XQ_TEST(XFontComboBox, defaultsAndFamilySelection)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    const char* firstFamily;

    XQ_REQUIRE(fc != NULL);

    /* 构造即 setEditable(true)（qfontcombobox.cpp:403）。 */
    XQ_CHECK(XComboBox_isEditable((XComboBox*)fc));
    XQ_CHECK(XComboBox_lineEdit((XComboBox*)fc) != NULL);

    /* 过滤器/书写系统缺省（qfontcombobox.h 属性缺省）。 */
    XQ_EQ_I64(XFontComboBox_fontFilters(fc),
              XFontComboBoxFilter_AllFonts);
    XQ_EQ_I64(XFontComboBox_writingSystem(fc),
              XFontComboBoxWritingSystem_Any);

    /* 族列表非空（字体库为空时回退默认项），当前项落在 0。 */
    XQ_CHECK(XComboBox_count((XComboBox*)fc) > 0);
    XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), 0);
    firstFamily = XFontComboBox_currentFamily(fc);
    XQ_CHECK_PTR(firstFamily);
    XQ_CHECK(firstFamily[0] != '\0');
    XQ_EQ_STR(firstFamily, XComboBox_itemText_2((XComboBox*)fc, 0));
    /* 可编辑框与当前族名同步（构造时经 setLineEdit 回填）。 */
    XQ_EQ_STR(XLineEdit_text(XComboBox_lineEdit((XComboBox*)fc)),
              firstFamily);

    /* 过滤器：按条件**重建族列表**（Qt updateModel，qfontcombobox.cpp:307）。
     * 本机 Qt 探针：MonospacedFonts 使 count 276→8、currentIndex 回 0。 */
    {
        int allCount = XComboBox_count((XComboBox*)fc);
        XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_MonospacedFonts);
        XQ_EQ_I64(XFontComboBox_fontFilters(fc),
                  (int)XFontComboBoxFilter_MonospacedFonts);
        XQ_CHECK(XComboBox_count((XComboBox*)fc) <= allCount);
        if (XComboBox_count((XComboBox*)fc) > 0)
            XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), 0);
        /* 重建后每条仍是真实族名（当前族名即第 0 条）。 */
        XQ_EQ_STR(XFontComboBox_currentFamily(fc),
                  XComboBox_itemText_2((XComboBox*)fc,
                                       XComboBox_currentIndex((XComboBox*)fc)));
        /* 恢复 AllFonts ⇒ 列表回到全量。 */
        XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
        XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);
    }
    /* 组合位往返（Monospaced|Scalable）。 */
    XFontComboBox_setFontFilters(
        fc, (int)XFontComboBoxFilter_MonospacedFonts |
                (int)XFontComboBoxFilter_ScalableFonts);
    XQ_EQ_I64(XFontComboBox_fontFilters(fc),
              (int)XFontComboBoxFilter_MonospacedFonts |
                  (int)XFontComboBoxFilter_ScalableFonts);
    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
    XFontComboBox_setWritingSystem(
        fc, (int)XFontComboBoxWritingSystem_SimplifiedChinese);
    XQ_EQ_I64(XFontComboBox_writingSystem(fc),
              (int)XFontComboBoxWritingSystem_SimplifiedChinese);
    /* 非法书写系统忽略（头文件契约）。 */
    XFontComboBox_setWritingSystem(fc, 7);
    XQ_EQ_I64(XFontComboBox_writingSystem(fc),
              (int)XFontComboBoxWritingSystem_SimplifiedChinese);
    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
    XFontComboBox_setWritingSystem(fc,
                                   (int)XFontComboBoxWritingSystem_Any);

    XClassDelete((XClass*)fc);
}

/* ==================== ② sizeHint 宽度固定 14×'m' ==================== */

XQ_TEST(XFontComboBox, sizeHintFixedByMWide)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    XSize hintBefore;
    XSize hintAfter;
    XFont font;
    int mWidth;

    XQ_REQUIRE(fc != NULL);
    hintBefore = XWidget_sizeHint_base((XWidget*)fc);
    XQ_CHECK(hintBefore.width > 0);
    XQ_CHECK(hintBefore.height > 0);

    /* Qt：宽度 == fontMetrics().horizontalAdvance('m') * 14。 */
    font = XWidget_font((const XWidget*)fc);
    mWidth = XPainter_textWidth(&font, "m");
    XClassDeinit(&font);
    if (mWidth > 0)
        XQ_EQ_I64(hintBefore.width, mWidth * 14);

    /* 关键行为：加一个超长字体族名，宽度**不变**（Qt 不按最长族名定宽；
     * 旧实现继承组合框的「最长项测宽」，此处会显著变宽）。 */
    XComboBox_addItem_2((XComboBox*)fc,
                        "AVeryLongFontFamilyNameThatWouldExpandTheCombo"
                        "BoxWidthIfItWereContentDriven0123456789");
    hintAfter = XWidget_sizeHint_base((XWidget*)fc);
    XQ_EQ_I64(hintAfter.width, hintBefore.width);
    XQ_CHECK(hintAfter.height >= hintBefore.height);

    XClassDelete((XClass*)fc);
}

/* ==================== ③ currentFontChanged / 样例文本 / 显示字体 ==== */

XQ_TEST(XFontComboBox, currentFontSignalAndSamples)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    const char* family1;
    XString* sample;
    int base;

    XQ_REQUIRE(fc != NULL);
    /* 至少两个族名可切换（字体库只报一个族时补一个合成族，保证用例确定）。 */
    if (XComboBox_count((XComboBox*)fc) < 2)
        XComboBox_addItem_2((XComboBox*)fc, "XAlignFallbackFamily");
    XQ_CHECK(XComboBox_count((XComboBox*)fc) >= 2);
    family1 = XComboBox_itemText_2((XComboBox*)fc, 1);

    fc_fontChanged = 0;
    XObject_connect_2((XObject*)fc,
                      (size_t)XFontComboBox_currentFontChanged_signal(
                          fc, NULL),
                      fc_fontChangedSlot);

    /* 换族名 ⇒ currentFontChanged 恰一次（qfontcombobox.cpp:352-360）。 */
    XFontComboBox_setCurrentFamily(fc, family1);
    XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), 1);
    XQ_EQ_STR(XFontComboBox_currentFamily(fc), family1);
    XQ_EQ_I64(fc_fontChanged, 1);

    /* 同族重复 set ⇒ 去重不发（槽内按族名比对）。 */
    XFontComboBox_setCurrentFamily(fc, family1);
    XQ_EQ_I64(fc_fontChanged, 1);

    /* setCurrentFont 为 setCurrentFamily 的 Qt 名别名。 */
    XFontComboBox_setCurrentFont(fc, family1);
    XQ_EQ_I64(fc_fontChanged, 1);

    /* 未命中族名：Qt 经 QFontInfo 解析成缺省族（本机探针里即列表内的
     * "思源黑体"）；本库以控件字体族近似，族名不在列表时保持当前项。
     * 两种情形下 currentFamily 都必须是**列表内真实族名**，不得残留未知串。 */
    base = XComboBox_currentIndex((XComboBox*)fc);
    XFontComboBox_setCurrentFamily(fc, "NoSuchFamily#");
    XQ_CHECK(XComboBox_findText_2((XComboBox*)fc,
                                  XFontComboBox_currentFamily(fc)) >= 0);
    XQ_CHECK(XStrcmp(XFontComboBox_currentFamily(fc), "NoSuchFamily#") != 0);
    if (XComboBox_currentIndex((XComboBox*)fc) == base)
        XQ_EQ_STR(XFontComboBox_currentFamily(fc), family1);

    /* 大小写不敏感命中（Qt：QFontInfo 规范族名大小写；探针
     * setCurrentFont(QFont("dejavu serif")) 选中 "DejaVu Serif"）：
     * 取一个含大写字母的族名，改成全小写再选。 */
    {
        int i;
        int n = XComboBox_count((XComboBox*)fc);
        int target = -1;
        char lower[256];
        for (i = 0; i < n; ++i) {
            const char* txt = XComboBox_itemText_2((XComboBox*)fc, i);
            int hasUpper = 0;
            size_t k;
            if (!txt || !txt[0]) continue;
            for (k = 0; txt[k]; ++k)
                if (txt[k] >= 'A' && txt[k] <= 'Z') { hasUpper = 1; break; }
            if (hasUpper && XStrlen(txt) < sizeof(lower)) { target = i; break; }
        }
        if (target >= 0) {
            const char* txt = XComboBox_itemText_2((XComboBox*)fc, target);
            size_t k;
            for (k = 0; k <= XStrlen(txt); ++k) {
                char c = txt[k];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                lower[k] = c;
            }
            XFontComboBox_setCurrentFamily(fc, lower);
            XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), target);
            XQ_EQ_STR(XFontComboBox_currentFamily(fc), txt);
        }
    }

    /* Qt 形状的 currentFont 值返回（qfontcombobox.h:44 QFont）：族名与
     * currentFamily 一致。[P1-35] Qt 名主入口即值版（原 _2 后缀让名）。 */
    {
        XFont cur = XFontComboBox_currentFont(fc);
        XQ_CHECK(XFont_family(&cur) != NULL);
        if (XFont_family(&cur))
            XQ_EQ_STR(XFont_family(&cur), XFontComboBox_currentFamily(fc));
        XClassDeinit((XClass*)&cur);
    }

    /* 样例文本：未自定义时回退族名；自定义后返回自定义；NULL 移除后回退。 */
    sample = XFontComboBox_sampleTextForFont(fc, "SampleFamily");
    XQ_CHECK_PTR(sample);
    if (sample) XQ_EQ_STR(XString_toUtf8(sample), "SampleFamily");
    if (sample) XClassDelete((XClass*)sample);
    XFontComboBox_setSampleTextForFont(fc, "SampleFamily", "Abc 字体");
    sample = XFontComboBox_sampleTextForFont(fc, "SampleFamily");
    XQ_CHECK_PTR(sample);
    if (sample) XQ_EQ_STR(XString_toUtf8(sample), "Abc 字体");
    if (sample) XClassDelete((XClass*)sample);
    XFontComboBox_setSampleTextForFont(fc, "SampleFamily", NULL);
    sample = XFontComboBox_sampleTextForFont(fc, "SampleFamily");
    if (sample) XQ_EQ_STR(XString_toUtf8(sample), "SampleFamily");
    if (sample) XClassDelete((XClass*)sample);
    /* 参数非法返回 NULL。 */
    XQ_CHECK(XFontComboBox_sampleTextForFont(fc, NULL) == NULL);
    XQ_CHECK(XFontComboBox_sampleTextForSystem(fc, -1) == NULL);

    /* 书写系统样例：未采样时回退当前族名。 */
    sample = XFontComboBox_sampleTextForSystem(
        fc, (int)XFontComboBoxWritingSystem_Latin);
    XQ_CHECK_PTR(sample);
    if (sample)
        XQ_EQ_STR(XString_toUtf8(sample), XFontComboBox_currentFamily(fc));
    if (sample) XClassDelete((XClass*)sample);

    /* 显示字体的「不改当前项、不发信号」基准（逐族用例在此之后断言；
     * 前面的未命中/大小写用例可能合法改变当前族，故先取基准）。 */
    base = XComboBox_currentIndex((XComboBox*)fc);

    /* 逐族显示字体（qfontcombobox.h:53-54 setDisplayFont(family,font) /
     * optional<QFont> displayFont(family)）：登记族返回登记字体，
     * 未登记族返回该族 XFont（Qt 探针同形），空族名返回空 optional。
     * [P2-52/53] 单值版已删（Qt 6.8 只有逐族版，单值状态无消费者），
     * 此处原「单值往返」用例随之改为逐族往返覆盖（深拷贝/不改当前项/
     * 不发信号的契约在下方逐族断言中继续成立）；[P1-36/37] 逐族版即
     * Qt 名主入口（原 _2 后缀让名）。 */
    {
        XFont df;
        XFont got;
        int changesBefore = fc_fontChanged;
        XFont_init(&df);
        XFont_setFamily(&df, "XAlignPerFamily");
        XFont_setPointSize(&df, 23);
        XFontComboBox_setDisplayFont(fc, "XAlignPerFamily", &df);
        XQ_CHECK(XFontComboBox_displayFont(fc, "XAlignPerFamily", &got));
        XQ_EQ_I64(XFont_pointSize(&got), 23);
        if (XFont_family(&got))
            XQ_EQ_STR(XFont_family(&got), "XAlignPerFamily");
        XClassDeinit(&got);
        /* 登记显示字体不改当前项、不发 currentFontChanged（P2-52 前置
         * 用例断言的同款契约，迁到逐族入口继续覆盖）。 */
        XQ_EQ_I64(XComboBox_currentIndex((XComboBox*)fc), base);
        XQ_EQ_I64(fc_fontChanged, changesBefore);
        /* 未登记族：返回该族字体（family = 请求族名）。 */
        XQ_CHECK(XFontComboBox_displayFont(fc, "XAlignOtherFamily", &got));
        if (XFont_family(&got))
            XQ_EQ_STR(XFont_family(&got), "XAlignOtherFamily");
        XClassDeinit(&got);
        /* 空族名 ⇒ 空 optional。 */
        XQ_CHECK(!XFontComboBox_displayFont(fc, "", &got));
        XQ_CHECK(!XFontComboBox_displayFont(fc, NULL, NULL));
        XClassDeinit(&df);
    }

    XClassDelete((XClass*)fc);
}

/* ==================== ④ 字体过滤器语义（Qt updateModel 口径） ======= */

XQ_TEST(XFontComboBox, fontFiltersRebuildList)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    int allCount;
    int monoCount;
    int propCount;
    int expectMono = 0;
    int expectScalable = 0;
    int i;

    XQ_REQUIRE(fc != NULL);
    allCount = XComboBox_count((XComboBox*)fc);
    XQ_CHECK(allCount > 0);

    /* 先按过滤器契约独立复算全量谓词：等宽 = 可解析成**轮廓** face
     * 且 'i'/'W' 推进相同；可缩放 = 可解析成轮廓 face（与实现
     * xfcb_familyIsFixedPitch/xfcb_familyIsScalable 同源，但不走过滤器
     * 内部路径）。不能硬断言 monoCount>0：本库经
     * XPlatformFontDatabase 列的是系统字体族，而 XFont 的 FT 解析只按
     * XFONT_EXTERNAL_FT_FONT_DIR/exeDir 找 `<family>.ttf` 文件；本机
     * （Linux/offscreen）系统族名没有同名文件 ⇒ 全部落回内置点阵 face，
     * 按「未知族保守判非等宽/非轮廓」口径两个过滤结果都为空。断言
     * **过滤结果 == 独立复算值** 才是环境无关的契约（可解析的机器上
     * 自然覆盖非空分支，本机覆盖保守回退分支）。 */
    for (i = 0; i < allCount; ++i) {
        XFont f;
        const XFontFace* face;
        int wi;
        int ww;
        const char* family = XComboBox_itemText_2((XComboBox*)fc, i);
        if (!family || !family[0]) continue;
        XFont_init_ex(&f, family, 12, -1, false);
        face = XFont_face(&f);
        wi = XPainter_textWidth(&f, "i");
        ww = XPainter_textWidth(&f, "W");
        XClassDeinit((XClass*)&f);
        if (face && XFontFace_kind(face) == XFontFace_Outline) {
            ++expectScalable;
            if (wi > 0 && wi == ww) ++expectMono;
        }
    }

    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_MonospacedFonts);
    monoCount = XComboBox_count((XComboBox*)fc);
    XQ_EQ_I64(monoCount, expectMono);
    XQ_CHECK(monoCount <= allCount);
    /* 过滤后每一条都应是等宽的（'i' 与 'W' 推进相同；与实现的探测同源，
     * 这里验证的是「列表真的按条件筛过」而不是仅存状态）。 */
    for (i = 0; i < monoCount; ++i) {
        XFont f;
        int wi;
        int ww;
        const char* family = XComboBox_itemText_2((XComboBox*)fc, i);
        if (!family || !family[0]) continue;
        XFont_init_ex(&f, family, 12, -1, false);
        wi = XPainter_textWidth(&f, "i");
        ww = XPainter_textWidth(&f, "W");
        XClassDeinit((XClass*)&f);
        XQ_CHECK(wi > 0 && wi == ww);
    }

    XFontComboBox_setFontFilters(fc,
                                 (int)XFontComboBoxFilter_ProportionalFonts);
    propCount = XComboBox_count((XComboBox*)fc);
    /* 比例 = 非等宽：与等宽互补，两类之和恰为全量（Qt 的过滤位互斥）。 */
    XQ_EQ_I64(propCount, allCount - expectMono);
    XQ_CHECK(monoCount + propCount == allCount);

    /* 可缩放过滤：结果 == 独立复算的轮廓 face 族数。 */
    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_ScalableFonts);
    {
        int n = XComboBox_count((XComboBox*)fc);
        XQ_EQ_I64(n, expectScalable);
        XQ_CHECK(n <= allCount);
    }
    /* 不可缩放 = 非轮廓 face：与可缩放互补。 */
    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_NonScalableFonts);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount - expectScalable);

    XFontComboBox_setFontFilters(fc, (int)XFontComboBoxFilter_AllFonts);
    XQ_EQ_I64(XComboBox_count((XComboBox*)fc), allCount);

    /* 弹层宽虚槽：min(宽×5/3, 屏宽)（本机 Qt 探针 120→200；屏宽钳位在
     * 基类弹层定位里做，此处验证 5/3 放大）。 */
    XWidget_setGeometry((XWidget*)fc, 0, 0, 120, 24);
    XQ_EQ_I64(XComboBox_popupWidth((XComboBox*)fc), 200);

    XClassDelete((XClass*)fc);
}

/* ==================== ⑤ 弹层逐族显示字体（P3-62） ================== */
/* Qt 6.8 依据：弹层条目按登记字体渲染（QFontComboBox 的家族委托口径）；
 * 本库以弹层视图 FontRole 承载（XAbstractItemView_setItemFont，借用
 * const XFont*），绘制消费在 XListView.c:434（xlv_drawRowText 的
 * XPainter_setFont）。触发点＝条目重建、登记表变更、弹层显示。
 * 本用例覆盖前两个确定路径（弹层显示路径经 popupShown 自连，需真弹窗，
 * 不在离屏断言内）。 */
XQ_TEST(XFontComboBox, displayFontAppliedToPopupRows)
{
    XFontComboBox* fc = XFontComboBox_create(NULL, 0);
    XFont df;
    XListView* view;
    /* 族名**快照**：setFontFilters 重建走 XComboBox_clear（释放条目 XString）
     * ⇒ 借用指针会悬垂，必须拷成自有缓冲后再跨重建使用。 */
    char family0[256];
    char family1[256];
    int idx0;
    int idx1;

    XQ_REQUIRE(fc != NULL);
    /* 至少两个族可用（字体库只报一个族时补合成族，保证用例确定）。 */
    if (XComboBox_count((XComboBox*)fc) < 2)
        XComboBox_addItem_2((XComboBox*)fc, "XAlignDisplayPopFamily");
    XQ_CHECK(XComboBox_count((XComboBox*)fc) >= 2);
    family0[0] = '\0';
    family1[0] = '\0';
    XStrncpy(family0, XComboBox_itemText_2((XComboBox*)fc, 0),
             sizeof(family0) - 1);
    XStrncpy(family1, XComboBox_itemText_2((XComboBox*)fc, 1),
             sizeof(family1) - 1);
    XQ_REQUIRE(family0[0] != '\0');
    XQ_REQUIRE(family1[0] != '\0');

    view = XComboBox_view((XComboBox*)fc);
    XQ_CHECK_PTR(view);
    /* 未登记：FontRole 为空（回落控件字体）。 */
    XQ_CHECK(XAbstractItemView_itemFont((XAbstractItemView*)view, 0, 0)
             == NULL);

    /* 登记族 0 的显示字体 ⇒ 该行 FontRole 指向登记字体（接口返回借用）。 */
    XFont_init(&df);
    XFont_setFamily(&df, family0);
    XFont_setPointSize(&df, 27);
    XFontComboBox_setDisplayFont(fc, family0, &df);
    {
        const XFont* role =
            XAbstractItemView_itemFont((XAbstractItemView*)view, 0, 0);
        XQ_CHECK_PTR(role);
        if (role) XQ_EQ_I64(XFont_pointSize(role), 27);
    }
    /* 未登记族 1 仍空。 */
    XQ_CHECK(XAbstractItemView_itemFont((XAbstractItemView*)view, 1, 0)
             == NULL);

    /* 列表重建（setFontFilters 走 xfcb_rebuild）后整体重放：族 0 的
     * 新行号仍需带登记字体，未登记族仍空（借用指针不得悬垂）。
     * 先切到 Monospaced 再切回全量——真触发两次重建（同值会早退）。 */
    XFontComboBox_setFontFilters(fc,
                                 (int)XFontComboBoxFilter_MonospacedFonts);
    XFontComboBox_setFontFilters(fc,
                                 (int)XFontComboBoxFilter_AllFonts);
    idx0 = XComboBox_findText_2((XComboBox*)fc, family0);
    idx1 = XComboBox_findText_2((XComboBox*)fc, family1);
    if (idx0 >= 0) {
        const XFont* role =
            XAbstractItemView_itemFont((XAbstractItemView*)view, idx0, 0);
        XQ_CHECK_PTR(role);
        if (role) XQ_EQ_I64(XFont_pointSize(role), 27);
    }
    if (idx1 >= 0)
        XQ_CHECK(XAbstractItemView_itemFont((XAbstractItemView*)view, idx1, 0)
                 == NULL);

    /* 清除登记（font=NULL）⇒ 该行回落空 FontRole。 */
    XFontComboBox_setDisplayFont(fc, family0, NULL);
    if (idx0 >= 0)
        XQ_CHECK(XAbstractItemView_itemFont((XAbstractItemView*)view, idx0, 0)
                 == NULL);
    XClassDeinit((XClass*)&df);

    XClassDelete((XClass*)fc);
}

#endif /* XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */
