/**
 * @file       XComboBoxValidatorAlignTest.c
 * @brief      §B10：XComboBox 校验器接线 + 项图标绘制 + sizeAdjustPolicy
 *             三档几何 对齐契约测试。
 * @details    对齐依据（Qt 6.8.3 源码 + 本机 Qt 6.8.3 offscreen 探针）：
 *             **① validator（QComboBox::setValidator / validator）**
 *             - Qt 只做转发：`if (d->lineEdit) d->lineEdit->setValidator(v);`
 *               （qcombobox.cpp:1914-1920）；`validator()` 读行编辑
 *               （:1927-1930）；
 *             - 关闭可编辑态时校验器**随之移除**（:1740 note「When
 *               disabling the editable state, the validator and completer
 *               are removed」；探针：setEditable(false) 后 validator()
 *               ==nullptr，再 setEditable(true) 仍为 nullptr）；
 *             - 校验在行编辑**变更结算**处消费（qwidgetlinecontrol.cpp:
 *               1600-1650 的 `m_validator->validate(...)`：Invalid ⇒
 *               inputRejected + 回滚），Enter/失焦还过
 *               `hasAcceptableInput() || fixup()` 门禁（
 *               qwidgetlinecontrol.cpp:1649-1657、qlineedit.cpp:1958）；
 *             - QRegularExpressionValidator 只重写 validate、不重写 fixup
 *               （qvalidator.cpp:250 缺省空实现，:994-1013 是 validate）。
 *             本库把框架现成的 `XRegularExpressionValidator` 接到
 *             XLineEdit 的校验回调上（铁律 4，不手搓校验器）。
 *             **② itemIcon 绘制（QCommonStyle CE_ComboBoxLabel）**
 *             - 图标在**两种模式**都画：`!cb->currentIcon.isNull()` 分支
 *               在 editable 判定之外（qcommonstyle.cpp:2232-2249）；
 *               文本分支才受 `!cb->editable` 约束（:2250）；
 *             - **判据 = 「可解码」**（返工 P2-a 二修）：Qt 的
 *               QIcon::isNull 经 addFile → QPixmapIconEngine::isNull
 *               （qicon.cpp:453-455/495-497）＝「解不出图即 true」
 *               ——坏路径/垃圾内容 isNull=1（本机 Qt 6.8.0 offscreen
 *               探针实测）⇒ 坏路径走「无图标」分支：不让位、不右移、
 *               不涂 Base；一修误锁「坏路径仍让位」已纠正；
 *               整块先裁剪到内容区（:2235 `p->setClipRect(editRect)`，
 *               返工 P2-b）；
 *             - editable 时图标矩形先涂 QPalette::Base（:2241-2242）、
 *               图标在 iconSize.width()+4 的格内**居中**（:2243），
 *               文本区右移 iconSize.width()+4（:2245-2248）；
 *             - 行编辑框让位由 QComboBoxPrivate::updateLineEditGeometry
 *               （qcombobox.cpp:1262-1279）承担：宽减 iconSize+4 并对编辑区
 *               右对齐；Qt 探针（200×30、16px 图标、editable）lineEdit
 *               x=22 w=157，无图标当前项时 x=2 w=177。**Qt 的绝对宽含
 *               Qt 自身风格收口基线（177），与 X 产出（180/160）差 3px
 *               属两边 chrome 基线不同，非几何口径差**——本文件锁的是
 *               相对让位量（Δx=Δw=iconSize+4、右缘不动）与 X 观测基线，
 *               不锁 Qt 绝对宽。
 *             **③ sizeAdjustPolicy 三档几何（recomputeSizeHint）**
 *             - 项宽只在 `&sh == &sizeHint || minimumContentsLength == 0`
 *               时统计（qcombobox.cpp:357）⇒ mcl>0 时 minimumSizeHint
 *               **不含项文本**；探针 mcl=10 + 长项 ⇒ sizeHint 222 /
 *               minHint 100；
 *             - mcl 字符基准是 'X'（:377），非 'M'（探针 adv X=7 / adv M=10）；
 *             - mcl 档 hasIcon 初值恒真（:352）⇒ 无真图标也吃 iconSize+4；
 *               探针 policy=2/mcl=10/无图标 ⇒ 120，policy=0 同参数 ⇒ 100；
 *             - placeholder 宽并入两枚提示（:380-382，在分支之外）；
 *               探针 mcl=10 + 长占位 ⇒ sizeHint=minHint=310。
 * @note       C99；全部夹具释放，漏删由运行器 [LEAK] 判定。
 *             图标解码落全局 XPixmapCache（Qt 同款纯性能缓存语义），
 *             收尾清缓存避免泄漏门误报（同 XComboBoxAlignTest P3-56 先例）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

/* XRegularExpression_ON 必须进外层门：文件内无条件使用
 * XRegularExpression_create_utf8 / XRegularExpressionValidator_create_ex
 * 与 XComboBox_setRegularExpressionValidator（该 API 的声明也在
 * #if XRegularExpression_ON 内）——裁剪配置（无正则）下整文件编译出
 * （2026-11-15 复核⑦补门）。 */
#if XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON && \
    XRegularExpression_ON

#include "XComboBox.h"
#include "XLineEdit.h"
#include "XLineControl.h"            /* hasAcceptableInput/fixup 门禁口径。 */
#include "XRegularExpressionValidator.h" /* §B10 现成校验器（正则）。 */
#include "XRegularExpression.h"      /* 造校验器表达式。 */
#include "XImage.h"                  /* 图标夹具 + XWidget_grab 像素断言。 */
#include "XIcon.h"                   /* 图标可解码性探针。 */
#include "XPixmapCache.h"            /* 解码缓存收尾清理（防泄漏门误报）。 */
#include "XPainter.h"                /* XPainter_textWidth 复算基准宽。 */
#include "XFont.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVarList.h"
#include "XString.h"
#include "XClass.h"
#include "XEvent.h"                  /* XFocusEvent_create（失焦门禁）。 */
#include "XWindowEvent.h"            /* XFocusEvent_* 定义。 */
#include "XCoreApplication.h"

#include <stdio.h>                   /* remove/snprintf（图标夹具清理与命名）。 */
#include <stdint.h>                  /* uintptr_t（夹具路径地址标签）。 */

/* ==================== 夹具 ==================== */

/** @brief 可解码 PNG 图标夹具路径（每次用例新建/删除）。
 * @details 前缀固定 + 进程内静态地址标签后缀：并发跑同过滤的两个进程
 *          不再互踩同一 /tmp 文件（2026-11-15 复核⑦；旧固定路径
 *          /tmp/xcombo_b10_icon.png 与 XComboBoxAlignTest 的
 *          /tmp/xcombo_align_icon.png 各自为政仍有跨进程同路径隐患）。 */
static char g_xqcvIconPath[80];
static const char* xqcv_iconPath(void)
{
    if (!g_xqcvIconPath[0]) {
        static int anchor;
        snprintf(g_xqcvIconPath, sizeof(g_xqcvIconPath),
                 "/tmp/xcombo_b10_icon_%lx.png",
                 (unsigned long)(uintptr_t)&anchor);
    }
    return g_xqcvIconPath;
}

/** @brief 造一份可解码的 PNG 图标（16×16 纯色，与缺省 iconSize 同宽——
 *  iconW==iconSize 时带内居中位恰在带中点，原点错位缺陷才产生可见溢出，
 *  8×8 小图标在任何原点下都缩在带内、锁不出遮挡）。
 * @param path 输出路径。
 * @param rgba 填充色。
 * @return 造好且可解码返回 true。
 * @note  解码依赖图像插件 ⇒ 先探针解码，不可解码时相关像素断言跳过
 *        （环境无关断言先例：XComboBoxAlignTest itemIconSizeAndDecoration）。 */
static bool xqcv_makeIcon(const char* path, uint32_t rgba)
{
    XImage image;
    XIcon probe;
    bool ok;
    XImage_init_ex(&image, 16, 16, XImageFormat_ARGB32);
    XImage_fill(&image, rgba);
    XImage_save_2(&image, path, "png", 100);
    XClassDeinit((XClass*)&image);
    XIcon_init_file_2(&probe, path);
    ok = !XIcon_isNull(&probe);
    XClassDeinit((XClass*)&probe);
    return ok;
}

/** @brief 统计矩形内「底色之外」的像素数（判断图标带真被画了内容）。
 * @param img 图像。
 * @param x0,y0,x1,y1 半开矩形。
 * @param bg 背景色（与之不同的像素计为「有内容」）。
 * @return 不同色像素数；img 为 NULL 返回 -1。 */
static int xqcv_inkCount(const XImage* img, int x0, int y0, int x1, int y1,
                         uint32_t bg)
{
    int n = 0;
    int x;
    int y;
    int w;
    int h;
    if (!img) return -1;
    w = XImage_width(img);
    h = XImage_height(img);
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x)
            if (XImage_pixel(img, x, y) != bg) ++n;
    return n;
}

/** @brief 统计矩形内「贴近指定色」的像素数（用于确认图标本色被画出来）。
 * @param img 图像。
 * @param x0,y0,x1,y1 半开矩形。
 * @param rr,gg,bb 目标 RGB（0..255）；alpha 不参与。
 * @param tol 每通道容差（图标缩放/混合后的漂移）。
 * @return 命中像素数；img 为 NULL 返回 -1。 */
static int xqcv_colorHits(const XImage* img, int x0, int y0, int x1, int y1,
                          int rr, int gg, int bb, int tol)
{
    int n = 0;
    int x;
    int y;
    int w;
    int h;
    if (!img) return -1;
    w = XImage_width(img);
    h = XImage_height(img);
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            uint32_t px = XImage_pixel(img, x, y);
            int r = (int)((px >> 16) & 0xFFu);
            int g = (int)((px >> 8) & 0xFFu);
            int b = (int)(px & 0xFFu);
            if (r - rr <= tol && rr - r <= tol &&
                g - gg <= tol && gg - g <= tol &&
                b - bb <= tol && bb - b <= tol)
                ++n;
        }
    }
    return n;
}

/** @brief 用内联正则造一个校验器（调用方负责 XClassDelete）。 */
static XRegularExpressionValidator* xqcv_regexValidator(const char* pattern)
{
    XRegularExpression* re = XRegularExpression_create_utf8(pattern, 0);
    XRegularExpressionValidator* v;
    if (!re) return NULL;
    v = XRegularExpressionValidator_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, re);
    XClassDelete((XClass*)re);
    return v;
}

/* ==================== ① validator 接线 ==================== */

/** @brief 校验器接线 + 编辑门禁（类型化入口）。
 * Qt 依据：setValidator 转发（qcombobox.cpp:1914-1920）；validator 读行编辑
 * （:1927-1930）；行编辑 validate 钩子在变更结算处消费
 * （qwidgetlinecontrol.cpp:1600-1650）。
 * 本项目口径：`setValidator(void*)` 是**不透明承载**（零解引用，兼容既有
 * 回归锁传 `static int` 载荷）；真接线走
 * `XComboBox_setRegularExpressionValidator`（类型化入参）。 */
XQ_TEST(XComboBox, validatorWiredIntoLineEdit)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XRegularExpressionValidator* v;
    static int dummyCarrier = 0;   /* 不透明承载探针（非 XClass 载荷）。 */
    XQ_REQUIRE(cb != NULL);

    /* 非可编辑：Qt 静默忽略转发（validator() 仍为 nullptr，因无行编辑）。 */
    XQ_CHECK(XComboBox_validator(cb) == NULL);
    XQ_CHECK(XComboBox_lineEdit(cb) == NULL);

    /* 可编辑 + 正则校验器：指针往返，并真的接进行编辑。 */
    XComboBox_setEditable(cb, true);
    XQ_REQUIRE(XComboBox_lineEdit(cb) != NULL);
    v = xqcv_regexValidator("[A-Z][0-9]");
    XQ_REQUIRE(v != NULL);
    XComboBox_setRegularExpressionValidator(cb, v);
    XQ_CHECK_MSG(XComboBox_validator(cb) == (void*)v,
                 "validator() 应回读已接线的校验器（两入口共用读取面）");
    XQ_CHECK(XComboBox_regularExpressionValidator(cb) == v);
    /* 校验钩子已下发内嵌行编辑（等价 Qt d->lineEdit->setValidator(v)）。 */
    {
        XLineEdit* edit = XComboBox_lineEdit(cb);
        XQ_CHECK(XLineEdit_validator(edit) != NULL);
    }

    /* 非法输入被识别为不可接受、合法输入可接受（validate 语义）。
     * 「A」是中间态（partial）、「A5」Acceptable、「zz」Invalid。 */
    {
        XLineEdit* edit = XComboBox_lineEdit(cb);
        XLineEdit_setText(edit, "A5");
        XQ_CHECK_MSG(XLineEdit_hasAcceptableInput(edit),
                     "「A5」应满足 [A-Z][0-9]（Acceptable）");
        XLineEdit_setText(edit, "zz");
        XQ_CHECK_MSG(!XLineEdit_hasAcceptableInput(edit),
                     "「zz」不应满足 [A-Z][0-9]（Invalid）");
        XLineEdit_setText(edit, "A");
        XQ_CHECK_MSG(!XLineEdit_hasAcceptableInput(edit),
                     "「A」是中间态（partial match），不算 Acceptable");
    }

    /* 换一个校验器：新表达式立即生效（无旧钩子残留）。 */
    {
        XRegularExpressionValidator* v2 = xqcv_regexValidator("[0-9]+");
        XQ_REQUIRE(v2 != NULL);
        XComboBox_setRegularExpressionValidator(cb, v2);
        XQ_CHECK(XComboBox_validator(cb) == (void*)v2);
        {
            XLineEdit* edit = XComboBox_lineEdit(cb);
            XLineEdit_setText(edit, "A5");
            XQ_CHECK_MSG(!XLineEdit_hasAcceptableInput(edit),
                         "换成 [0-9]+ 后「A5」应不可接受（旧校验器未残留）");
            XLineEdit_setText(edit, "123");
            XQ_CHECK(XLineEdit_hasAcceptableInput(edit));
        }
        XClassDelete((XClass*)v2);
    }

    /* 不透明承载入口传任意载荷：保存但不接线（零解引用 ⇒ 不越界读）。
     * 既有回归锁就传 `static int` 地址与组合框自身指针（见文件头注记）。 */
    XComboBox_setValidator(cb, &dummyCarrier);
    XQ_CHECK_MSG(XComboBox_validator(cb) == (void*)&dummyCarrier,
                 "setValidator(void*) 按不透明指针承载（兼容既有调用面）");
    XQ_CHECK_MSG(XComboBox_regularExpressionValidator(cb) == NULL,
                 "不透明入口不写类型化字段（不得把任意载荷当正则用）");
    {
        /* 传组合框自身指针也不得崩（历史回归锁的载荷形态）。 */
        XComboBox_setValidator(cb, (void*)cb);
        XQ_CHECK(XComboBox_validator(cb) == (void*)cb);
    }

    /* NULL 清除：两个读取面都空，且编辑框不再有校验钩子。 */
    XComboBox_setValidator(cb, NULL);
    XQ_CHECK(XComboBox_validator(cb) == NULL);
    XQ_CHECK(XComboBox_regularExpressionValidator(cb) == NULL);
    {
        XLineEdit* edit = XComboBox_lineEdit(cb);
        XQ_CHECK(XLineEdit_validator(edit) == NULL);
        XLineEdit_setText(edit, "whatever");
        XQ_CHECK_MSG(XLineEdit_hasAcceptableInput(edit),
                     "无校验器时任意文本可接受（Qt 同）");
    }

    /* 先删控件后删校验器：不留借用悬垂窗口（借用电须覆盖使用期）。 */
    XClassDelete((XClass*)cb);
    XClassDelete((XClass*)v);
}

/** @brief 关闭可编辑态 ⇒ 校验器移除且不复活（Qt :1740 note + 探针）。
 * 探针实测：setEditable(false) 后 validator()==nullptr；
 * 再 setEditable(true) 仍为 nullptr。 */
XQ_TEST(XComboBox, validatorRemovedWhenNotEditable)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XRegularExpressionValidator* v;
    XQ_REQUIRE(cb != NULL);
    XComboBox_setEditable(cb, true);
    v = xqcv_regexValidator("[A-Z][0-9]");
    XQ_REQUIRE(v != NULL);
    XComboBox_setRegularExpressionValidator(cb, v);
    XQ_CHECK(XComboBox_validator(cb) == (void*)v);

    XComboBox_setEditable(cb, false);
    XQ_CHECK_MSG(XComboBox_validator(cb) == NULL,
                 "关闭可编辑态后校验器随之移除（qcombobox.cpp:1740 note；"
                 "本机 Qt 6.8.3 探针 validator()==nullptr）");
    XQ_CHECK(XComboBox_lineEdit(cb) == NULL);

    XComboBox_setEditable(cb, true);
    XQ_CHECK_MSG(XComboBox_validator(cb) == NULL,
                 "重开可编辑态不恢复校验器（Qt 探针同：仍为 nullptr）");
    {
        XLineEdit* edit = XComboBox_lineEdit(cb);
        XQ_REQUIRE(edit != NULL);
        XQ_CHECK_MSG(XLineEdit_validator(edit) == NULL,
                     "新行编辑上不得残留旧校验钩子");
        XLineEdit_setText(edit, "zz");
        XQ_CHECK(XLineEdit_hasAcceptableInput(edit));
    }

    /* 先删控件后删校验器：不留借用悬垂窗口（借用电须覆盖使用期）。 */
    XClassDelete((XClass*)cb);
    XClassDelete((XClass*)v);
}

/** @brief 外部编辑框自带校验钩子的保留（[复核⑤] setLineEdit 不得静默
 *  清掉外部配置）。
 * Qt 依据：setLineEdit（qcombobox.cpp:1831-1848）不动编辑框自带的
 * validator；组合框持有类型化校验器时才由单一事实源接管。 */
static XLineEditValidatorState xqcv_alwaysAccept(XLineEdit* edit,
                                                 const char* text,
                                                 void* userData)
{
    (void)edit; (void)text; (void)userData;
    return XLineEditValidatorState_Acceptable;
}

XQ_TEST(XComboBox, externalLineEditHookPreserved)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XLineEdit* ext;
    XRegularExpressionValidator* v;
    XQ_REQUIRE(cb != NULL);

    /* 外部编辑框先装自己的钩子，再挂给组合框。 */
    ext = XLineEdit_create(NULL, 0);
    XQ_REQUIRE(ext != NULL);
    XLineEdit_setValidator(ext, xqcv_alwaysAccept, (void*)cb);
    XComboBox_setEditable(cb, true);
    XComboBox_setLineEdit(cb, ext);
    /* 旧内嵌框走 deleteLater：派发一次事件循环让挂起删除落地（防泄漏门误报）。 */
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    /* 组合框无类型化校验器 ⇒ 外部钩子原样保留（Qt 同口径）。 */
    XQ_CHECK_MSG(XLineEdit_validator(ext) == xqcv_alwaysAccept,
                 "setLineEdit 不得清掉外部编辑框自带的校验钩子（复核⑤）");

    /* 组合框设类型化校验器 ⇒ 接管（单一事实源）。 */
    v = xqcv_regexValidator("[A-Z][0-9]");
    XQ_REQUIRE(v != NULL);
    XComboBox_setRegularExpressionValidator(cb, v);
    XQ_CHECK(XLineEdit_validator(ext) != NULL);
    XQ_CHECK(XLineEdit_validator(ext) != xqcv_alwaysAccept);
    XQ_CHECK_MSG(XComboBox_validator(cb) == (void*)v,
                 "接管后 validator() 回读组合框持有的校验器");

    /* 组合框清除 ⇒ 只清自己装的钩子（不得把外部钩子留下或错清他人）。 */
    XComboBox_setRegularExpressionValidator(cb, NULL);
    XQ_CHECK_MSG(XLineEdit_validator(ext) == NULL,
                 "清除只摘组合框自己装的钩子");

    /* ext 已是 cb 子控件，随 cb 析构；v 独立，最后删（借用覆盖使用期）。 */
    XClassDelete((XClass*)cb);
    XClassDelete((XClass*)v);
}

/** @brief 先设校验器再转可编辑：安装时补挂（本实现把 m_validator 作为
 *  单一事实源，比 Qt「设置即丢弃」更不丢配置）。
 * @note  Qt 在无行编辑时 setValidator 是空操作、配置直接丢失；本实现保存
 *        并在 setEditable(true) 补挂。这是**有意偏差**（更可用），
 *        在此显式锁住，避免后续被「对齐 Qt」改回去。 */
XQ_TEST(XComboBox, validatorAppliesWhenBecomingEditable)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XRegularExpressionValidator* v;
    XQ_REQUIRE(cb != NULL);
    v = xqcv_regexValidator("[A-Z][0-9]");
    XQ_REQUIRE(v != NULL);
    XComboBox_setRegularExpressionValidator(cb, v); /* 此时不可编辑 */
    XQ_CHECK(XComboBox_validator(cb) == (void*)v);

    XComboBox_setEditable(cb, true);
    {
        XLineEdit* edit = XComboBox_lineEdit(cb);
        XQ_REQUIRE(edit != NULL);
        XQ_CHECK_MSG(XLineEdit_validator(edit) != NULL,
                     "转为可编辑时补挂校验器（本实现有意偏差，见用例注记）");
        XLineEdit_setText(edit, "A5");
        XQ_CHECK(XLineEdit_hasAcceptableInput(edit));
        XLineEdit_setText(edit, "nope");
        XQ_CHECK(!XLineEdit_hasAcceptableInput(edit));
    }
    /* 先删控件后删校验器：不留借用悬垂窗口（借用电须覆盖使用期）。 */
    XClassDelete((XClass*)cb);
    XClassDelete((XClass*)v);
}

/** @brief 失焦门禁：不可接受且无 fixup ⇒ 不发 editingFinished
 *  （qlineedit.cpp:1958 `d->edited && (hasAcceptableInput() || fixup())`）。
 * @note  QRegularExpressionValidator 不重写 fixup（qvalidator.cpp:250），
 *        故非法文本失焦后**文本保持原样、不提交**（探针 v1 实测：
 *        text='zzz' count=1 cur=0 editingFinished=0）。 */
XQ_TEST(XComboBox, invalidInputDoesNotCommitOnFocusOut)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XRegularExpressionValidator* v;
    XLineEdit* edit;
    XFocusEvent* ev;
    XQ_REQUIRE(cb != NULL);
    XComboBox_setEditable(cb, true);
    XComboBox_addItem_2(cb, "A5");
    v = xqcv_regexValidator("[A-Z][0-9]");
    XQ_REQUIRE(v != NULL);
    XComboBox_setRegularExpressionValidator(cb, v);
    edit = XComboBox_lineEdit(cb);
    XQ_REQUIRE(edit != NULL);

    /* 非法文本失焦：不产生插入（count 不变）；文本仍留在编辑框里
     * （Qt 的行编辑不做回滚，只拒绝提交；探针同）。 */
    XLineEdit_setText(edit, "zzz");
    ev = XFocusEvent_create(XEVENT_TYPE_FOCUS_OUT, XFocusReason_Other);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)edit, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(XComboBox_count(cb), 1);
    XQ_CHECK_MSG(XComboBox_currentIndex(cb) == 0,
                 "非法失焦不改当前项（Qt 探针 cur 恒 0）");
    XQ_EQ_STR(XLineEdit_text(edit), "zzz");

    /* 合法文本失焦：走 editingFinished → 命中已有项（不新增）。 */
    XLineEdit_setText(edit, "A5");
    ev = XFocusEvent_create(XEVENT_TYPE_FOCUS_OUT, XFocusReason_Other);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)edit, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(XComboBox_count(cb), 1);   /* 命中项，不插入重复 */

    /* 先删控件后删校验器：不留借用悬垂窗口（借用电须覆盖使用期）。 */
    XClassDelete((XClass*)cb);
    XClassDelete((XClass*)v);
}

/* ==================== ② itemIcon 绘制 ==================== */

/** @brief 闭态当前项图标真被画出来，且文本右移让位。
 * Qt 依据：CE_ComboBoxLabel 图标占 iconSize.width()+4、文本随之后移
 * （qcommonstyle.cpp:2232-2252）。用 XWidget_grab 离屏重定向真驱动
 * paintEvent（同 XHeaderViewPaintAlignTest 先例，注入 PAINT 事件属空壳）。 */
XQ_TEST(XComboBox, closedStateIconPaintedAndTextShifted)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XImage* shotNoIcon;
    XImage* shotIcon;
    bool decodable;
    XQ_REQUIRE(cb != NULL);

    decodable = xqcv_makeIcon(xqcv_iconPath(), 0xFFFF0000u);
    XComboBox_setSizeAdjustPolicy(cb,
                                  XComboBoxSizeAdjustPolicy_AdjustToContents);
    XComboBox_setIconSize(cb, 16);
    XComboBox_addItem_2(cb, "MMMM");
    XComboBox_setCurrentIndex(cb, 0);
    XWidget_setGeometry((XWidget*)cb, 0, 0, 200, 30);

    shotNoIcon = XWidget_grab((XWidget*)cb);
    XQ_REQUIRE(shotNoIcon != NULL);

    if (!decodable) {
        /* 环境无图像插件：只锁「无图标路径不崩且确实画了底/文本」。 */
        XQ_CHECK_MSG(xqcv_inkCount(shotNoIcon, 0, 0, 200, 30, 0) > 0,
                     "无图标时控件主体仍被绘制");
        XClassDelete((XClass*)shotNoIcon);
        XClassDelete((XClass*)cb);
        remove(xqcv_iconPath());
        return;
    }

    XComboBox_setItemIcon_2(cb, 0, xqcv_iconPath());
    shotIcon = XWidget_grab((XWidget*)cb);
    XQ_REQUIRE(shotIcon != NULL);

    /* 图标带 [2, 2+iconSize+4)（内容左缘原点，16px 图标居中于 [4,20)）内
     * 必须出现**图标本色**——证明 CE_ComboBoxLabel 的图标分支被执行
     * （此前 m_itemIcons 全文无绘制消费）。 */
    {
        int hits = xqcv_colorHits(shotIcon, 2, 4, 22, 26, 255, 0, 0, 40);
        XQ_CHECK_MSG(hits > 0,
                     "图标带 (2..22, 4..26) 内应有红色图标像素（命中 %d）",
                     hits);
        /* [复核③] 带右缘外不得有图标色：图标溢出 22..26 就会被行编辑
         * （可编辑）或文本区盖掉右缘——原点必须与行编辑让位（x=2+inset）
         * 同一基准，带 [2,22) 与行编辑左缘无缝相邻。 */
        XQ_EQ_I64(xqcv_colorHits(shotIcon, 22, 4, 190, 26, 255, 0, 0, 40), 0);
        /* 无图标版同区间不应有红色（对照组，排除「底色本来就是红的」）。 */
        XQ_CHECK_MSG(xqcv_colorHits(shotNoIcon, 2, 4, 22, 26, 255, 0, 0, 40) == 0,
                     "无图标版同区间不该出现图标色（对照）");
    }
    /* 文本右移：图标版在图标带右侧的内容量与无图标版不同（文本被推开）。 */
    {
        int withIconRight = xqcv_inkCount(shotIcon, 26, 0, 190, 30, 0);
        int noIconRight = xqcv_inkCount(shotNoIcon, 26, 0, 190, 30, 0);
        XQ_CHECK_MSG(withIconRight != noIconRight,
                     "图标版与无图标版在图标带右侧的内容量应有差异"
                     "（文本右移，with=%d no=%d）", withIconRight, noIconRight);
    }

    /* 清图标 ⇒ 回到无图标版版式（图标带差异消失）。 */
    XComboBox_setItemIcon_2(cb, 0, NULL);
    {
        XImage* shotCleared = XWidget_grab((XWidget*)cb);
        XQ_REQUIRE(shotCleared != NULL);
        XQ_EQ_I64(xqcv_colorHits(shotCleared, 2, 4, 22, 26, 255, 0, 0, 40), 0);
        XClassDelete((XClass*)shotCleared);
    }

    /* 坏路径不画、不崩（台账验收「空图标路径不画、不崩」）。 */
    XComboBox_setItemIcon_2(cb, 0, "/nonexistent/xcombo-b10-missing.png");
    {
        XImage* shotBad = XWidget_grab((XWidget*)cb);
        XQ_REQUIRE(shotBad != NULL);
        XQ_EQ_I64(xqcv_colorHits(shotBad, 2, 4, 22, 26, 255, 0, 0, 40), 0);
        XClassDelete((XClass*)shotBad);
    }

    XClassDelete((XClass*)shotIcon);
    XClassDelete((XClass*)shotNoIcon);
    XClassDelete((XClass*)cb);
    remove(xqcv_iconPath());
#if XPIXMAPCACHE_ON
    /* 全局像素图缓存是 Qt 同款纯性能缓存（LRU，正常量级不逐出）⇒
     * 收尾清空归还条目，避免运行器泄漏门误报（同 XComboBoxAlignTest）。 */
    XPixmapCache_clear();
#endif
}

/** @brief 可编辑态：图标照画（Qt :2232 在 editable 判定之外），
 *  且内嵌行编辑框让出 iconSize+4 的宽度（updateLineEditGeometry）。
 * Qt 探针（200×30、16px 图标）：行编辑 x=22 w=157；无图标 x=2 w=177
 * ——绝对值含 Qt 自身风格基线，本用例锁相对让位量与 X 观测基线。 */
XQ_TEST(XComboBox, editableIconPaintedAndLineEditInset)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XLineEdit* edit;
    int wNoIcon;
    int wIcon;
    int inset;
    bool decodable;
    XImage* shot;
    XQ_REQUIRE(cb != NULL);

    decodable = xqcv_makeIcon(xqcv_iconPath(), 0xFF0000FFu);
    XComboBox_setIconSize(cb, 16);
    XComboBox_addItem_2(cb, "MMMM");
    XComboBox_addItem_2(cb, "NNNN");
    XComboBox_setCurrentIndex(cb, 0);
    XWidget_setGeometry((XWidget*)cb, 0, 0, 200, 30);
    XComboBox_setEditable(cb, true);
    edit = XComboBox_lineEdit(cb);
    XQ_REQUIRE(edit != NULL);

    /* 无图标当前项：行编辑占满内容区。[复核⑦] 基线用**观测值**（不回述
     * 实现公式），锁定的是左缘与后续相对量：无图标时左缘 = 内容左缘 2。 */
    wNoIcon = XWidget_width((XWidget*)edit);
    XQ_REQUIRE(wNoIcon > 0);
    XQ_EQ_I64(XWidget_x((XWidget*)edit), 2);

    if (decodable) {
        inset = XComboBox_iconSize(cb) + 4;
        XComboBox_setItemIcon_2(cb, 0, xqcv_iconPath());
        wIcon = XWidget_width((XWidget*)edit);
        /* Qt updateLineEditGeometry（qcombobox.cpp:1262-1279）：宽收窄
         * iconSize+4 且对编辑区右对齐 ⇒ 左缘右移同一量、右缘不动。 */
        XQ_EQ_I64(wIcon, wNoIcon - inset);
        XQ_EQ_I64(XWidget_x((XWidget*)edit), 2 + inset);
        XQ_CHECK_MSG(XWidget_x((XWidget*)edit) + wIcon == 2 + wNoIcon,
                     "让位后右缘不动（x+w 恒等，Qt AlignRight 收窄语义）");
        /* 换到无图标的条目 ⇒ 让位收回。 */
        XComboBox_setCurrentIndex(cb, 1);
        XQ_EQ_I64(XWidget_width((XWidget*)edit), wNoIcon);
        XQ_EQ_I64(XWidget_x((XWidget*)edit), 2);
        /* 换回来 + 改图标尺寸 ⇒ 让位量随 iconSize 变。[返工 P1 订正]
         * Qt 的 setIconSize（qcombobox.cpp:1682-1691）只 view()->setIconSize
         * + 清 sizeHint + updateGeometry，**不**直接调 updateLineEditGeometry；
         * X 因显式管理行编辑几何（子控件 setGeometry 模型）需在此同步，
         * 语义等价 Qt「下一次收口」的结果，非逐行对标。 */
        XComboBox_setCurrentIndex(cb, 0);
        XComboBox_setIconSize(cb, 24);
        XQ_EQ_I64(XWidget_width((XWidget*)edit), wNoIcon - 28);
        XQ_EQ_I64(XWidget_x((XWidget*)edit), 2 + 28);
        XComboBox_setIconSize(cb, 16);
        XQ_EQ_I64(XWidget_width((XWidget*)edit), wNoIcon - inset);
        XQ_EQ_I64(XWidget_x((XWidget*)edit), 2 + inset);
    }

    /* [返工 P2-a 二修] 坏路径**不让位**：Qt 的 QIcon(路径) 构造即
     * addFile → QPixmapIconEngine::addFile 对打不开/格式不识别的文件
     * 直接 return（qicon.cpp:453-455），isNull()=pixmaps.isEmpty()
     * （:495-497）⇒ 坏路径 isNull=true（本机 Qt 6.8.0 offscreen 探针
     * 实测 missing/broken 均 isNull=1、editable setItemIcon(坏图标) 后
     * itemIcon(0).isNull()=1）⇒ updateLineEditGeometry 的
     * `!itemIcon(currentIndex).isNull()` 门为假，行编辑**不收窄**、
     * 原地不动（qcombobox.cpp:1268）。一修曾锁反（「坏路径仍收窄」）
     * 已纠正，防止反 Qt 口径固化。 */
    if (decodable) {
        XComboBox_setItemIcon_2(cb, 0, "/nonexistent/xcombo-b10-missing.png");
        XQ_EQ_I64(XWidget_width((XWidget*)edit), wNoIcon);
        XQ_EQ_I64(XWidget_x((XWidget*)edit), 2);
        XComboBox_setItemIcon_2(cb, 0, xqcv_iconPath()); /* 还原：后续像素断言按带图标画 */
        XQ_EQ_I64(XWidget_width((XWidget*)edit), wNoIcon - inset);
        XQ_EQ_I64(XWidget_x((XWidget*)edit), 2 + inset);
    }

    /* 可编辑态图标真被画（此前 editable 分支整块跳过图标绘制）。 */
    if (decodable) {
        XComboBox_clearEditText(cb);
        XLineEdit_setText(edit, "");
        XWidget_setGeometry((XWidget*)cb, 0, 0, 200, 30);
        shot = XWidget_grab((XWidget*)cb);
        XQ_REQUIRE(shot != NULL);
        XQ_CHECK_MSG(xqcv_colorHits(shot, 2, 4, 22, 26, 0, 0, 255, 40) > 0,
                     "可编辑态图标带 (2..22) 内应有蓝色图标像素（Qt :2232 "
                     "该分支不受 editable 约束）");
        /* [复核③] 图标带与行编辑左缘无缝：带右缘外不得再有图标色。 */
        XQ_EQ_I64(xqcv_colorHits(shot, 22, 4, 190, 26, 0, 0, 255, 40), 0);
        XClassDelete((XClass*)shot);
    }

    /* 控件变宽/变窄 ⇒ 行编辑跟随（Qt resizeEvent → updateLineEditGeometry）。
     * [复核⑦] 断相对量：宽随控件宽同幅增长、无图标左缘回 2。 */
    XComboBox_setItemIcon_2(cb, 0, NULL);
    XWidget_setGeometry((XWidget*)cb, 0, 0, 300, 30);
    {
        XResizeEvent re;
        XSize sz;
        XSize_init(&sz, 300, 30);
        XResizeEvent_init(&re, XEVENT_TYPE_RESIZE, &sz, &sz);
        (void)XCoreApplication_sendEvent((XObject*)cb, (XEvent*)&re);
        XClassDeinit((XClass*)&re);
    }
    XQ_EQ_I64(XWidget_width((XWidget*)edit), wNoIcon + 100);
    XQ_EQ_I64(XWidget_x((XWidget*)edit), 2);

    XClassDelete((XClass*)cb);
    remove(xqcv_iconPath());
#if XPIXMAPCACHE_ON
    XPixmapCache_clear();
#endif
}

/* ==================== ③ sizeAdjustPolicy 三档几何 ==================== */

/** @brief 三档几何的 Qt 口径（qcombobox.cpp:347-390 + 本机探针）。
 * 逐条断言：mcl 档不看项文本、mcl 基准 'X'（非 'M'）、mcl 档 hasIcon
 * 初值恒真、minimumSizeHint 与 sizeHint 在 mcl>0 时**分立**。 */
XQ_TEST(XComboBox, sizeAdjustPolicyGeometryMatchesQt)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XFont font;
    int xw;
    int mw;
    int xwLow;
    int base;
    int baseMin;
    int mclW;
    XQ_REQUIRE(cb != NULL);
    font = XWidget_font((const XWidget*)cb);
    xw = XPainter_textWidth(&font, "X");
    mw = XPainter_textWidth(&font, "M");
    xwLow = XPainter_textWidth(&font, "x");
    XClassDeinit((XClass*)&font);
    XQ_REQUIRE(xw > 0);
    XQ_REQUIRE(xwLow > 0);
    XQ_CHECK_MSG(xw != mw,
                 "本用例要求 'X' 与 'M' 字宽不同才能区分基准（x=%d m=%d）",
                 xw, mw);

    /* ---- ⓪ [复核②] 空列表基线 7×'x' **只属内容两档**（Qt :361-364 在
     * case AdjustToContents/OnFirstShow 内）；mcl 档 case 体为空
     * （Qt :374 `;`）⇒ 空 + mcl 档比内容档窄整整 7×'x'，且两枚提示
     * 都是纯 chrome（内容宽 0）。 ---- */
    XComboBox_setSizeAdjustPolicy(
        cb, XComboBoxSizeAdjustPolicy_AdjustToContentsOnFirstShow);
    base = XWidget_sizeHint_base((XWidget*)cb).width;
    XComboBox_setSizeAdjustPolicy(
        cb, XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);
    baseMin = XWidget_sizeHint_base((XWidget*)cb).width;
    XQ_CHECK_MSG(base - baseMin == 7 * xwLow,
                 "空列表：内容档 − mcl 档 = 7×'x'（mcl 档无空基线，Qt :374）"
                 "（contents=%d mcl=%d 7x=%d）", base, baseMin, 7 * xwLow);
    XQ_CHECK_MSG(
        XWidget_minimumSizeHint_base((const XWidget*)cb).width == baseMin,
        "空 + mcl 档两枚提示同为纯 chrome（内容宽 0，无空基线）");

    /* ---- ① mcl 档：**两枚提示都不量项文本**（长项不撑宽；mcl==0 亦然
     * —— Qt :374 case 体为空，minHint 路径同样落空）。
     * [复核⑦] addItem 在 mcl 档不失效缓存（Qt rowsInserted 只对
     * AdjustToContents 失效，qcombobox.cpp:1146-1153）⇒ 每次读数前用
     * 策略往返强制重算，防断言落在缓存上变成空锁。 ---- */
    XComboBox_setMinimumContentsLength(cb, 0);
    XComboBox_addItem_2(cb, "MMMM");
    XComboBox_setSizeAdjustPolicy(
        cb, XComboBoxSizeAdjustPolicy_AdjustToContents);
    XComboBox_setSizeAdjustPolicy(
        cb, XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);
    base = XWidget_sizeHint_base((XWidget*)cb).width;
    baseMin = XWidget_minimumSizeHint_base((const XWidget*)cb).width;
    XComboBox_addItem_2(cb,
        "AVeryLongItemTextThatWouldBlowUpTheWidthIfItemTextWereMeasured"
        "0123456789012345678901234567890123456789");
    XComboBox_setSizeAdjustPolicy(
        cb, XComboBoxSizeAdjustPolicy_AdjustToContents);
    XComboBox_setSizeAdjustPolicy(
        cb, XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);
    XQ_EQ_I64(XWidget_sizeHint_base((XWidget*)cb).width, base);
    XQ_CHECK_MSG(XWidget_minimumSizeHint_base((const XWidget*)cb).width ==
                 baseMin,
                 "mcl==0 的 minimumSizeHint 也不量项文本（Qt :374 case 空）");

    /* ---- ② mcl 基准是 'X'（不是 'M'）；③ mcl 档 hasIcon 恒真 ⇒ +iconSize+4 ---- */
    XComboBox_setMinimumContentsLength(cb, 10);
    mclW = xw * 10 + (XComboBox_iconSize(cb) + 4);
    {
        int hinted = XWidget_sizeHint_base((XWidget*)cb).width;
        XSize minHint = XWidget_minimumSizeHint_base((const XWidget*)cb);
        /* 与「按 'M' 计」的口径必须不同（probe 两字宽不同 ⇒ 差 10×(mw-xw)）。 */
        XQ_CHECK_MSG(hinted != mw * 10 + (XComboBox_iconSize(cb) + 4),
                     "mcl 字符基准应为 'X'（Qt :377），不该是 'M'");
        /* minimumSizeHint 在 mcl>0 时**不含项文本** ⇒ 与 mcl 底宽同源。 */
        XQ_CHECK_MSG(minHint.width < hinted ||
                     /* 项文本短于 mcl 底宽时两者相等，也合法。 */
                     minHint.width == hinted,
                     "minimumSizeHint 不得因长项而大于 sizeHint"
                     "（min=%d size=%d）", minHint.width, hinted);
        /* mcl 底宽口径：minHint 与 sizeHint 都 ≥ mcl 底宽。 */
        XQ_CHECK_MSG(minHint.width >= mclW, "minHint(%d) 应 ≥ mcl 底宽(%d)",
                     minHint.width, mclW);
    }

    /* ---- minimumSizeHint 与 sizeHint 分立：mcl 档 + 长项 → minHint 窄 ---- */
    {
        XSize minHint = XWidget_minimumSizeHint_base((const XWidget*)cb);
        int minNoLongItem;
        /* 清掉长项，只留 MMMM（短于 mcl 底宽）⇒ minHint 应不变（不含项文本）。
         * [复核⑦] 策略往返强制重算：mcl 档内容变化不失效缓存。 */
        XComboBox_clear(cb);
        XComboBox_addItem_2(cb, "MMMM");
        XComboBox_setSizeAdjustPolicy(
            cb, XComboBoxSizeAdjustPolicy_AdjustToContents);
        XComboBox_setSizeAdjustPolicy(
            cb, XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);
        minNoLongItem = XWidget_minimumSizeHint_base((const XWidget*)cb).width;
        XQ_CHECK_MSG(minHint.width == minNoLongItem,
                     "mcl>0 时 minimumSizeHint 不含项文本 ⇒ 长项增删不改它"
                     "（长项时 %d，短项时 %d）", minHint.width, minNoLongItem);
    }

    /* ---- 对照：AdjustToContents 档 + mcl==0 ⇒ 长项把 sizeHint 撑宽，
     * 且 minimumSizeHint 同式（Qt :357 的 mcl==0 分支） ---- */
    XComboBox_setMinimumContentsLength(cb, 0);
    XComboBox_setSizeAdjustPolicy(cb,
                                  XComboBoxSizeAdjustPolicy_AdjustToContents);
    {
        int wShort = XWidget_sizeHint_base((XWidget*)cb).width;
        XSize minShort = XWidget_minimumSizeHint_base((const XWidget*)cb);
        XComboBox_addItem_2(cb,
            "AVeryLongItemTextThatWouldBlowUpTheWidthIfItemTextWereMeasured"
            "0123456789012345678901234567890123456789");
        XQ_CHECK_MSG(XWidget_sizeHint_base((XWidget*)cb).width > wShort,
                     "AdjustToContents 档长项应变宽（内容驱动）");
        XQ_CHECK_MSG(XWidget_minimumSizeHint_base((const XWidget*)cb).width ==
                     XWidget_sizeHint_base((XWidget*)cb).width,
                     "mcl==0 时两枚提示同式（Qt :357 的 mcl==0 分支）");
        XQ_CHECK(minShort.width == wShort);
    }

    /* ---- 三档 setter 往返 + 越界忽略 ---- */
    XComboBox_setSizeAdjustPolicy(cb,
                                  XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);
    XQ_EQ_I64(XComboBox_sizeAdjustPolicy(cb),
              XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);
    XComboBox_setSizeAdjustPolicy(cb, 3);
    XQ_EQ_I64(XComboBox_sizeAdjustPolicy(cb),
              XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);

    XClassDelete((XClass*)cb);
}

/** @brief placeholder 宽并入**两枚**提示（Qt :380-382 在项宽分支之外）。
 * 探针：mcl=10 + 长占位 ⇒ sizeHint == minHint == 310。 */
XQ_TEST(XComboBox, placeholderAffectsBothHints)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XQ_REQUIRE(cb != NULL);
    XComboBox_setSizeAdjustPolicy(
        cb, XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);
    XComboBox_setMinimumContentsLength(cb, 10);
    XComboBox_addItem_2(cb, "A");
    {
        int size0 = XWidget_sizeHint_base((XWidget*)cb).width;
        int min0 = XWidget_minimumSizeHint_base((const XWidget*)cb).width;
        XComboBox_setPlaceholderText_2(
            cb, "A very long placeholder that dominates both size hints 0123456789");
        XQ_CHECK_MSG(XWidget_sizeHint_base((XWidget*)cb).width > size0,
                     "长占位应撑宽 sizeHint");
        XQ_CHECK_MSG(XWidget_minimumSizeHint_base((const XWidget*)cb).width > min0,
                     "长占位也应撑宽 minimumSizeHint（Qt :380-382 在分支之外）");
    }
    XClassDelete((XClass*)cb);
}

#endif /* XGUI_ON && XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON &&
          XRegularExpression_ON */
