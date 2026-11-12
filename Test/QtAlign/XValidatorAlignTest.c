/**
 * @file       XValidatorAlignTest.c
 * @brief      §validator：XValidator 抽象基类 + 三控件统一消费 对齐契约测试。
 * @details    对齐依据（Qt 6.8.3 原文 + 本机框架对照）：
 *             - `QValidator : QObject`，`enum State { Invalid, Intermediate,
 *               Acceptable }`（gui/util/qvalidator.h:23-34）；
 *             - `virtual State validate(QString&,int&) const` /
 *               `virtual void fixup(QString&) const`（qvalidator.h:40-41）；
 *             - 基类 fixup 空实现（qvalidator.cpp:250），
 *               QRegularExpressionValidator 只重写 validate（:994-1013）；
 *             - QComboBox::setValidator 转发行编辑（qcombobox.cpp:1914-1920）、
 *               QLineEdit::setValidator 转发控制器（qlineedit.cpp:596-600）；
 *             - QAbstractSpinBoxPrivate::init 把 QSpinBoxValidator 装到行编辑
 *               （qabstractspinbox.cpp:1596-1597），interpret 前置
 *               validate/fixup（:1893-1901），QSpinBoxValidator 转发
 *               q->validate/fixup（:1949-1971）。
 *
 *             用例覆盖（任务书验收）：
 *             ① 基类缺省 validate=Acceptable / fixup 空；派生重载两槽经
 *                _base 虚表分派；派生回落父类走 XClass_Parent；
 *             ② XRegularExpressionValidatorAdapter 经基类消费：Invalid
 *                回滚（inputRejected）/ Enter 失焦门禁，XLineEdit/XComboBox/
 *                XAbstractSpinBox 各一例；
 *             ③ 既有 `XComboBox_setValidator(void*)` 不透明承载回归锁：
 *                传 static int 地址 + 组合框自身指针，零解引用不越界；
 *             ④ 关闭可编辑/析构路径借用账目平（运行器 --leak-tolerance=0）；
 *             ⑤ XComboBox/XLineEdit/XAbstractSpinBox 既有过滤套件由统一门禁
 *                另跑（本文件不替代，见结果里的过滤命令）。
 * @note       C99；全部夹具释放，漏删由运行器 [LEAK] 判定。XValidator 是
 *             XObject 派生，析构走 XClassDelete/Deinit；借用型 XValidator*
 *             由用例保证生命周期覆盖控件使用期（先删控件后删校验器）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

/* XRegularExpression_ON 进外层门：文件内无条件使用正则校验器与
 * XRegularExpressionValidatorAdapter（该适配器的声明也在门内）。 */
#if XGUI_ON && XWIDGET_ON && XLINEEDIT_ON && XCOMBOBOX_ON && \
    XABSTRACTSPINBOX_ON && XRegularExpression_ON

#include "XValidator.h"
#include "XRegularExpression.h"
#include "XRegularExpressionValidator.h"
#include "XLineEdit.h"
#include "XComboBox.h"
#include "XAbstractSpinBox.h"
#include "XObject.h"
#include "XClass.h"
#include "XEvent.h"
#include "XWindowEvent.h"            /* XFocusEvent_create（失焦门禁）。 */
#include "XCoreApplication.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include <string.h>

/* ==================== ① 自定义派生校验器（重载 validate + fixup） ==================== */

XCLASS_DEFINE_BEGING(XQRecordingValidator)
XCLASS_DEFINE_EXTEND_END(XQRecordingValidator, XValidator)

/**
 * @brief 记录型派生校验器：validate 按注入状态返回、fixup 把输入改成
 *        "FIXED"（并计数），用于证明双虚槽都经 _base 分派。
 */
typedef struct XQRecordingValidator
{
    XValidator m_base;            /**< 基类；第一个成员。 */
    XValidatorState m_next;       /**< validate 返回值。 */
    int m_validateCalls;          /**< validate 调用计数。 */
    int m_fixupCalls;             /**< fixup 调用计数。 */
    int m_parentCalls;            /**< 回落父类次数。 */
    bool m_useParent;             /**< true 时回落父类缺省（XClass_Parent）。 */
} XQRecordingValidator;

static XValidatorState xqRecording_validate(XValidator* self, char** input,
                                            int* pos)
{
    XQRecordingValidator* v = (XQRecordingValidator*)self;
    if (!v) return XValidatorState_Invalid;
    ++v->m_validateCalls;
    if (v->m_useParent) {
        ++v->m_parentCalls;
        /* §497 条款 4：派生槽内回落父类必须 XClass_Parent，不能调自身
         * _base（会经对象虚表再次进入本槽 ⇒ 递归）。 */
        return XClass_Parent(XValidator, EXValidator_Validate,
                             XValidatorState(*)(XValidator*, char**, int*))(
            self, input, pos);
    }
    if (pos) *pos = (int)XStrlen(*input);
    return v->m_next;
}

static void xqRecording_fixup(XValidator* self, char** input)
{
    XQRecordingValidator* v = (XQRecordingValidator*)self;
    char* fixed;
    if (!v || !input) return;
    ++v->m_fixupCalls;
    fixed = XStrdup("FIXED");
    if (!fixed) return;
    XFree_System(*input);        /* 契约：槽负责释放旧缓冲，替换新堆串。 */
    *input = fixed;
}

static XVtable* XQRecordingValidator_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQRecordingValidator)
    XVTABLE_INHERIT_XCLASS(XValidator);
    XVTABLE_OVERLOAD_DEFAULT(EXValidator_Validate, xqRecording_validate);
    XVTABLE_OVERLOAD_DEFAULT(EXValidator_Fixup, xqRecording_fixup);
    return XVTABLE_DEFAULT;
}

static void xqRecording_init(XQRecordingValidator* v)
{
    if (!v) return;
    XMemset(v, 0, sizeof(*v));
    XValidator_init((XValidator*)v, NULL);
    XClassSetVtable(v, XQRecordingValidator);
    v->m_next = XValidatorState_Acceptable;
    v->m_useParent = false;
}

/** @brief 造一份正则校验器（调用方负责 XClassDelete）。 */
static XRegularExpressionValidator* xqva_regex(const char* pattern)
{
    XRegularExpression* re = XRegularExpression_create_utf8(pattern, 0);
    XRegularExpressionValidator* v;
    if (!re) return NULL;
    v = XRegularExpressionValidator_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, re);
    XClassDelete((XClass*)re);
    return v;
}

/** @brief 造适配器（借用正则校验器；调用方负责 XClassDelete 适配器）。 */
static XRegularExpressionValidatorAdapter* xqva_adapter(
        const XRegularExpressionValidator* regex)
{
    return XRegularExpressionValidatorAdapter_create(regex);
}

/* ==================== ① 基类缺省 / 派生重载 / _base 分派 ==================== */

/** @brief 基类缺省：validate=Acceptable、fixup 空（不改写输入）。 */
XQ_TEST(XValidator, baseDefaultsAcceptableAndEmptyFixup)
{
    XValidator base;
    char* text = XStrdup("hello");
    int pos = 0;
    XQ_REQUIRE(text != NULL);
    XValidator_init(&base, NULL);
    XQ_EQ_I64(XValidator_validate_base(&base, &text, &pos),
              XValidatorState_Acceptable);
    XQ_EQ_I64(pos, 0);                  /* 基类缺省不动光标。 */
    XValidator_fixup_base(&base, &text);
    XQ_EQ_STR(text, "hello");           /* 空 fixup 不改写缓冲。 */
    /* NULL 安全：self 无效返回 Invalid / 空操作，不解引用。 */
    XQ_EQ_I64(XValidator_validate_base(NULL, &text, &pos),
              XValidatorState_Invalid);
    XValidator_fixup_base(NULL, &text);
    XFree_System(text);
    XClassDeinit((XClass*)&base);
}

/** @brief 派生重载双槽 + _base 虚表分派 + XClass_Parent 回落父类。 */
XQ_TEST(XValidator, derivedOverrideViaBaseAndParentFallback)
{
    XQRecordingValidator v;
    char* text = XStrdup("abc");
    int pos = 0;
    XQ_REQUIRE(text != NULL);
    xqRecording_init(&v);

    /* validate 经 _base 分派进派生槽：返回注入的 Intermediate。 */
    v.m_next = XValidatorState_Intermediate;
    XQ_EQ_I64(XValidator_validate_base((XValidator*)&v, &text, &pos),
              XValidatorState_Intermediate);
    XQ_EQ_I64(v.m_validateCalls, 1);
    XQ_EQ_I64(pos, 3);

    /* fixup 经 _base 分派进派生槽：替换缓冲为 "FIXED"。 */
    XValidator_fixup_base((XValidator*)&v, &text);
    XQ_EQ_I64(v.m_fixupCalls, 1);
    XQ_EQ_STR(text, "FIXED");

    /* 派生回落父类：XClass_Parent 拿到基类缺省 Acceptable，不递归。 */
    v.m_useParent = true;
    XQ_EQ_I64(XValidator_validate_base((XValidator*)&v, &text, &pos),
              XValidatorState_Acceptable);
    XQ_EQ_I64(v.m_parentCalls, 1);
    XQ_EQ_I64(v.m_validateCalls, 2);    /* 只进派生槽一次，未递归。 */

    /* 状态枚举数值逐值对齐 Qt qvalidator.h:30-34（0/1/2）。 */
    XQ_EQ_I64(XValidatorState_Invalid, 0);
    XQ_EQ_I64(XValidatorState_Intermediate, 1);
    XQ_EQ_I64(XValidatorState_Acceptable, 2);

    XFree_System(text);
    XClassDeinit((XClass*)&v);
}

/** @brief 正则适配器：validate 转发、fixup 走基类空实现（Qt 口径）。 */
XQ_TEST(XValidator, regexAdapterValidateAndEmptyFixup)
{
    XRegularExpressionValidator* regex = xqva_regex("[A-Z][0-9]");
    XRegularExpressionValidatorAdapter* ad;
    char* text;
    int pos = 0;
    XQ_REQUIRE(regex != NULL);
    ad = xqva_adapter(regex);
    XQ_REQUIRE(ad != NULL);
    XQ_CHECK_PTR(XRegularExpressionValidatorAdapter_regularExpression_const(ad));
    XQ_CHECK(XRegularExpressionValidatorAdapter_regularExpression_const(ad)
             == regex);

    /* Acceptable / Intermediate / Invalid 三态逐值转发。 */
    text = XStrdup("A5");
    XQ_EQ_I64(XValidator_validate_base((XValidator*)ad, &text, &pos),
              XValidatorState_Acceptable);
    XFree_System(text);
    text = XStrdup("A");
    XQ_EQ_I64(XValidator_validate_base((XValidator*)ad, &text, &pos),
              XValidatorState_Intermediate);
    XFree_System(text);
    text = XStrdup("zz");
    pos = 0;
    XQ_EQ_I64(XValidator_validate_base((XValidator*)ad, &text, &pos),
              XValidatorState_Invalid);
    /* Qt qvalidator.cpp:1011 `pos = input.size()`；Invalid 时把位置置输入
     * 长度（本库位置口径=UTF-8 字节偏移）。 */
    XQ_EQ_I64(pos, 2);
    /* fixup 不重写 ⇒ 走 XValidator 基类空实现，缓冲保持原样。 */
    XValidator_fixup_base((XValidator*)ad, &text);
    XQ_EQ_STR(text, "zz");
    XFree_System(text);

    /* 换成空表达式（借用切换）⇒ 恒 Acceptable。 */
    {
        XRegularExpressionValidator* empty = xqva_regex("");
        XQ_REQUIRE(empty != NULL);
        XRegularExpressionValidatorAdapter_setRegularExpression(ad, empty);
        text = XStrdup("anything!");
        XQ_EQ_I64(XValidator_validate_base((XValidator*)ad, &text, &pos),
                  XValidatorState_Acceptable);
        XFree_System(text);
        XRegularExpressionValidatorAdapter_setRegularExpression(ad, NULL);
        /* NULL 借用 ⇒ 经 XClass_Parent 回落基类缺省 Acceptable。 */
        text = XStrdup("x");
        XQ_EQ_I64(XValidator_validate_base((XValidator*)ad, &text, &pos),
                  XValidatorState_Acceptable);
        XFree_System(text);
        XClassDelete((XClass*)empty);
    }

    XClassDelete((XClass*)ad);   /* 先删适配器（借用不释放正则），再删正则。 */
    XClassDelete((XClass*)regex);
}

/* ==================== ② 三控件统一消费（Invalid 回滚 / 失焦门禁） ==================== */

/** @brief XLineEdit：类型化入口统一消费——非法输入拒绝 + hasAcceptableInput
 *         门禁 + 合法输入可接受；两入口互斥、借用可回读、NULL 可清除。 */
XQ_TEST(XValidator, lineEditTypedValidatorConsumed)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    XRegularExpressionValidator* regex = xqva_regex("[A-Z][0-9]");
    XRegularExpressionValidatorAdapter* ad;
    XQ_REQUIRE(le != NULL);
    XQ_REQUIRE(regex != NULL);
    ad = xqva_adapter(regex);
    XQ_REQUIRE(ad != NULL);

    XQ_CHECK(XLineEdit_validatorObject(le) == NULL);
    XLineEdit_setValidator_2(le, (XValidator*)ad);
    XQ_CHECK(XLineEdit_validatorObject(le) == (XValidator*)ad);
    XQ_CHECK(XLineEdit_validator(le) == NULL);   /* 两入口互斥。 */

    /* 合法：Acceptable（hasAcceptableInput 门禁放行）。 */
    XLineEdit_setText(le, "A5");
    XQ_CHECK_MSG(XLineEdit_hasAcceptableInput(le),
                 "「A5」应满足 [A-Z][0-9]（Acceptable）");
    /* 中间态：非 Acceptable ⇒ 门禁不放行（但编辑期允许保留）。 */
    XLineEdit_setText(le, "A");
    XQ_CHECK(!XLineEdit_hasAcceptableInput(le));
    /* 非法：Invalid ⇒ hasAcceptableInput 为假。 */
    XLineEdit_setText(le, "zz");
    XQ_CHECK(!XLineEdit_hasAcceptableInput(le));

    /* 编辑期 Invalid 回滚：insert 的候选文本被控制器判 Invalid 时回滚到
     * 变更前文本（xlc_finishChange 的 validateFromState 回滚路径）。 */
    XLineEdit_setText(le, "A5");
    XLineEdit_insert(le, "Z");          /* 候选 "A5Z" 不匹配 [A-Z][0-9]。 */
    XQ_CHECK_MSG(XStrcmp(XLineEdit_text(le), "A5") == 0,
                 "非法插入应被回滚（Invalid ⇒ 拒绝变更）");

    /* 换校验器表达式（新正则立即生效）：数字串。 */
    {
        XRegularExpressionValidator* digits = xqva_regex("[0-9]+");
        XRegularExpressionValidatorAdapter* ad2 = xqva_adapter(digits);
        XQ_REQUIRE(ad2 != NULL);
        XLineEdit_setValidator_2(le, (XValidator*)ad2);
        XQ_CHECK(XLineEdit_validatorObject(le) == (XValidator*)ad2);
        XLineEdit_setText(le, "123");
        XQ_CHECK(XLineEdit_hasAcceptableInput(le));
        XLineEdit_setText(le, "A5");
        XQ_CHECK(!XLineEdit_hasAcceptableInput(le));
        /* NULL 清除：两读取面都空，任意文本可接受。 */
        XLineEdit_setValidator_2(le, NULL);
        XQ_CHECK(XLineEdit_validatorObject(le) == NULL);
        XQ_CHECK(XLineEdit_validator(le) == NULL);
        XLineEdit_setText(le, "whatever");
        XQ_CHECK(XLineEdit_hasAcceptableInput(le));
        XClassDelete((XClass*)ad2);
        XClassDelete((XClass*)digits);
    }

    /* 旧函数指针入口与类型化入口互斥（逐字保留 §B10 契约）。 */
    XLineEdit_setValidator(le, NULL, NULL);
    XQ_CHECK(XLineEdit_validatorObject(le) == NULL);

    XClassDelete((XClass*)le);
    XClassDelete((XClass*)ad);
    XClassDelete((XClass*)regex);
}

/** @brief XComboBox：内嵌行编辑复用统一链——非法失焦不提交、合法命中项、
 *         三入口互斥、关闭可编辑后校验器移除且不复活（§B10 口径）。 */
XQ_TEST(XValidator, comboBoxTypedValidatorConsumed)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XRegularExpressionValidator* regex = xqva_regex("[A-Z][0-9]");
    XRegularExpressionValidatorAdapter* ad;
    XLineEdit* edit;
    XFocusEvent* ev;
    XQ_REQUIRE(cb != NULL);
    XQ_REQUIRE(regex != NULL);
    ad = xqva_adapter(regex);
    XQ_REQUIRE(ad != NULL);

    /* 非可编辑先设：保存并可回读（本实现有意偏差，§B10 口径）。 */
    XComboBox_setValidator_2(cb, (XValidator*)ad);
    XQ_CHECK(XComboBox_validator(cb) == (void*)ad);
    XQ_CHECK(XComboBox_validatorObject(cb) == (XValidator*)ad);
    XQ_CHECK(XComboBox_regularExpressionValidator(cb) == NULL);
    XComboBox_setEditable(cb, true);
    edit = XComboBox_lineEdit(cb);
    XQ_REQUIRE(edit != NULL);
    /* 组合框的类型化校验器已下发内嵌行编辑（统一消费链）。 */
    XQ_CHECK(XLineEdit_validatorObject(edit) == (XValidator*)ad);
    XQ_CHECK(XLineEdit_validator(edit) == NULL);

    XComboBox_addItem_2(cb, "A5");
    /* 非法输入失焦：Enter/失焦门禁不放行 ⇒ 不新增条目、不改当前项，
     * 非法文本原样留在编辑框（QRegularExpressionValidator 无 fixup）。 */
    XLineEdit_setText(edit, "zzz");
    ev = XFocusEvent_create(XEVENT_TYPE_FOCUS_OUT, XFocusReason_Other);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)edit, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(XComboBox_count(cb), 1);
    XQ_EQ_STR(XLineEdit_text(edit), "zzz");
    /* 合法输入失焦：门禁放行（命中已有项，不新增）。 */
    XLineEdit_setText(edit, "A5");
    ev = XFocusEvent_create(XEVENT_TYPE_FOCUS_OUT, XFocusReason_Other);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)edit, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(XComboBox_count(cb), 1);

    /* 三入口互斥：切到 §B10 正则入口 ⇒ 类型化面清零；切回不透明入口
     * ⇒ 两个类型化面都清零（零解引用契约见下个用例）。 */
    XComboBox_setRegularExpressionValidator(cb, regex);
    XQ_CHECK(XComboBox_validatorObject(cb) == NULL);
    XQ_CHECK(XLineEdit_validatorObject(edit) == NULL);
    XQ_CHECK(XComboBox_regularExpressionValidator(cb) == regex);
    XComboBox_setValidator_2(cb, (XValidator*)ad);
    XQ_CHECK(XComboBox_regularExpressionValidator(cb) == NULL);
    XQ_CHECK(XLineEdit_validatorObject(edit) == (XValidator*)ad);

    /* 关闭可编辑：四字段同批清、重开不复活（§B10 + §validator 借用账）。 */
    XComboBox_setEditable(cb, false);
    XQ_CHECK(XComboBox_validator(cb) == NULL);
    XQ_CHECK(XComboBox_validatorObject(cb) == NULL);
    XComboBox_setEditable(cb, true);
    edit = XComboBox_lineEdit(cb);
    XQ_REQUIRE(edit != NULL);
    XQ_CHECK(XLineEdit_validatorObject(edit) == NULL);

    XClassDelete((XClass*)cb);   /* 先删控件，后删借用校验器。 */
    XClassDelete((XClass*)ad);
    XClassDelete((XClass*)regex);
}

/** @brief XAbstractSpinBox：类型化校验器经内嵌行编辑统一消费 + interpret
 *         前置 validate/fixup + hasAcceptableInput 两层判定；换框重挂。 */
XQ_TEST(XValidator, abstractSpinBoxTypedValidatorConsumed)
{
    XAbstractSpinBox* spin = XAbstractSpinBox_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XRegularExpressionValidator* regex = xqva_regex("[0-9]+");
    XRegularExpressionValidatorAdapter* ad;
    XLineEdit* edit;
    XQ_REQUIRE(spin != NULL);
    XQ_REQUIRE(regex != NULL);
    ad = xqva_adapter(regex);
    XQ_REQUIRE(ad != NULL);
    edit = XAbstractSpinBox_lineEdit(spin);
    XQ_REQUIRE(edit != NULL);

    XQ_CHECK(XAbstractSpinBox_validatorObject(spin) == NULL);
    /* 基类无校验器时默认可接受（对标 QAbstractSpinBox::validate 基类默认）。 */
    XLineEdit_setText(edit, "42");
    XQ_CHECK(XAbstractSpinBox_hasAcceptableInput(spin));

    XAbstractSpinBox_setValidator_2(spin, (XValidator*)ad);
    XQ_CHECK(XAbstractSpinBox_validatorObject(spin) == (XValidator*)ad);
    /* 已下发内嵌行编辑（编辑期 Invalid 回滚由 XLineControl 统一链承担）。 */
    XQ_CHECK(XLineEdit_validatorObject(edit) == (XValidator*)ad);
    XQ_CHECK(XLineEdit_validator(edit) == NULL);

    /* 合法数字串：两层校验都 Acceptable。 */
    XLineEdit_setText(edit, "123");
    XQ_CHECK(XAbstractSpinBox_hasAcceptableInput(spin));
    XQ_CHECK(XLineEdit_hasAcceptableInput(edit));
    /* 非数字：类型化校验器判 Invalid ⇒ hasAcceptableInput 为假；
     * 行编辑门禁同样不放行（失焦 editingFinished 不提交）。 */
    XLineEdit_setText(edit, "abc");
    XQ_CHECK(!XAbstractSpinBox_hasAcceptableInput(spin));
    XQ_CHECK(!XLineEdit_hasAcceptableInput(edit));
    /* interpretText 的 validate/fixup 前置不崩、文本保持（正则无 fixup，
     * 与 Qt QRegularExpressionValidator 不重写 fixup 同口径）。 */
    XAbstractSpinBox_interpretText(spin);
    XQ_EQ_STR(XLineEdit_text(edit), "abc");

    /* 更换内嵌行编辑：类型化校验器重挂到新框（Qt setLineEdit 口径）。 */
    {
        XLineEdit* ext = XLineEdit_create(NULL, 0);
        XQ_REQUIRE(ext != NULL);
        XAbstractSpinBox_setLineEdit(spin, ext);
        XCoreApplication_processEvents(XEventLoop_AllEvents); /* 旧框 deleteLater 回收拍。 */
        XQ_CHECK(XLineEdit_validatorObject(ext) == (XValidator*)ad);
        XLineEdit_setText(ext, "789");
        XQ_CHECK(XAbstractSpinBox_hasAcceptableInput(spin));
        XLineEdit_setText(ext, "zz");
        XQ_CHECK(!XAbstractSpinBox_hasAcceptableInput(spin));
    }

    /* NULL 清除：行编辑接线摘除，任意文本可接受。 */
    XAbstractSpinBox_setValidator_2(spin, NULL);
    XQ_CHECK(XAbstractSpinBox_validatorObject(spin) == NULL);
    {
        XLineEdit* e = XAbstractSpinBox_lineEdit(spin);
        XQ_REQUIRE(e != NULL);
        XQ_CHECK(XLineEdit_validatorObject(e) == NULL);
        XLineEdit_setText(e, "anything");
        XQ_CHECK(XAbstractSpinBox_hasAcceptableInput(spin));
    }

    XClassDelete((XClass*)spin);
    XClassDelete((XClass*)ad);
    XClassDelete((XClass*)regex);
}

/* ==================== ③ 既有 setValidator(void*) 零解引用回归锁 ==================== */

/** @brief `setValidator(void*)` 不透明承载：传 static int 地址、组合框自身
 *         指针、NULL，全部零解引用、可回读、不接线、不崩（既有回归锁口径
 *         xgui_regression_test.c:35228 / xgui_demo_apitest_input.c:2508）。 */
XQ_TEST(XValidator, opaqueSetValidatorZeroDereference)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    static int dummyCarrier = 0;   /* 非 XClass 载荷：嗅探即越界读 UB。 */
    XQ_REQUIRE(cb != NULL);
    XComboBox_setEditable(cb, true);

    XComboBox_setValidator(cb, &dummyCarrier);
    XQ_CHECK_MSG(XComboBox_validator(cb) == (void*)&dummyCarrier,
                 "不透明入口零解引用、可回读（static int 地址）");
    XQ_CHECK(XComboBox_validatorObject(cb) == NULL);
    XQ_CHECK(XComboBox_regularExpressionValidator(cb) == NULL);
    {
        XLineEdit* edit = XComboBox_lineEdit(cb);
        XQ_REQUIRE(edit != NULL);
        XQ_CHECK(XLineEdit_validator(edit) == NULL);
        XQ_CHECK(XLineEdit_validatorObject(edit) == NULL);
        /* 载荷不参与过滤：任意文本可接受。 */
        XLineEdit_setText(edit, "arbitrary");
        XQ_CHECK(XLineEdit_hasAcceptableInput(edit));
    }

    /* 组合框自身指针（历史回归锁的载荷形态）同样不崩。 */
    XComboBox_setValidator(cb, (void*)cb);
    XQ_CHECK(XComboBox_validator(cb) == (void*)cb);

    /* NULL 清除。 */
    XComboBox_setValidator(cb, NULL);
    XQ_CHECK(XComboBox_validator(cb) == NULL);

    XClassDelete((XClass*)cb);
}

/** @brief 三入口切换的借用账目平：类型化→不透明→正则→类型化逐次切换，
 *         validator() 单一读取面与各类型化面不分叉，析构无悬垂。 */
XQ_TEST(XValidator, threeEntryMutualExclusionAndBorrowAccount)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XRegularExpressionValidator* regex = xqva_regex("[A-Z]+");
    XRegularExpressionValidatorAdapter* ad;
    static int carrier = 7;
    XQ_REQUIRE(cb != NULL);
    XQ_REQUIRE(regex != NULL);
    ad = xqva_adapter(regex);
    XQ_REQUIRE(ad != NULL);
    XComboBox_setEditable(cb, true);

    /* 类型化 → 不透明：类型化面清零，行编辑钩子摘除。 */
    XComboBox_setValidator_2(cb, (XValidator*)ad);
    XQ_CHECK(XComboBox_validator(cb) == (void*)ad);
    XQ_CHECK(XLineEdit_validatorObject(XComboBox_lineEdit(cb)) ==
             (XValidator*)ad);
    XComboBox_setValidator(cb, &carrier);
    XQ_CHECK(XComboBox_validator(cb) == (void*)&carrier);
    XQ_CHECK(XComboBox_validatorObject(cb) == NULL);
    XQ_CHECK(XLineEdit_validatorObject(XComboBox_lineEdit(cb)) == NULL);

    /* 不透明 → 正则（§B10）：正则面生效，类型化面仍空。 */
    XComboBox_setRegularExpressionValidator(cb, regex);
    XQ_CHECK(XComboBox_validator(cb) == (void*)regex);
    XQ_CHECK(XComboBox_regularExpressionValidator(cb) == regex);
    XQ_CHECK(XComboBox_validatorObject(cb) == NULL);
    XQ_CHECK(XLineEdit_validator(XComboBox_lineEdit(cb)) != NULL);

    /* 正则 → 类型化：正则钩子摘除，类型化面接管。 */
    XComboBox_setValidator_2(cb, (XValidator*)ad);
    XQ_CHECK(XComboBox_regularExpressionValidator(cb) == NULL);
    XQ_CHECK(XComboBox_validator(cb) == (void*)ad);
    XQ_CHECK(XLineEdit_validatorObject(XComboBox_lineEdit(cb)) ==
             (XValidator*)ad);
    XQ_CHECK(XLineEdit_validator(XComboBox_lineEdit(cb)) == NULL);

    /* 关闭可编辑（借用清零账）+ 析构（先删控件后删借用校验器）。 */
    XComboBox_setEditable(cb, false);
    XQ_CHECK(XComboBox_validator(cb) == NULL);
    XQ_CHECK(XComboBox_validatorObject(cb) == NULL);
    XClassDelete((XClass*)cb);
    XClassDelete((XClass*)ad);
    XClassDelete((XClass*)regex);
}

/* ==================== ④ 析构/拷贝/移动路径借用账目 ==================== */

/** @brief 类型化校验器装在各控件后走 deinit/copy/move 路径：无悬垂借用、
 *         拷贝/移动不继承借用指针、堆计数归零（运行器泄漏门判定）。 */
XQ_TEST(XValidator, borrowAccountOnDeinitCopyMove)
{
    /* XLineEdit：拷贝/移动后目标的类型化面为空（借用电不跨拷贝/移动）。 */
    {
        XLineEdit* a = XLineEdit_create(NULL, 0);
        XLineEdit* b = XLineEdit_create(NULL, 0);
        XRegularExpressionValidator* regex = xqva_regex("[0-9]+");
        XRegularExpressionValidatorAdapter* ad;
        XQ_REQUIRE(a != NULL && b != NULL && regex != NULL);
        ad = xqva_adapter(regex);
        XQ_REQUIRE(ad != NULL);
        XLineEdit_setValidator_2(a, (XValidator*)ad);
        XQ_CHECK(XLineEdit_validatorObject(a) == (XValidator*)ad);
        XClassCopy(b, a);
        XQ_CHECK_MSG(XLineEdit_validatorObject(b) == NULL,
                     "拷贝不继承借用型 XValidator（§B10 UAF 口径）");
        XLineEdit_setValidator_2(a, NULL);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
        XClassDelete((XClass*)ad);
        XClassDelete((XClass*)regex);
    }
    /* XAbstractSpinBox：拷贝重建默认编辑框且类型化面清零；析构平账。 */
    {
        XAbstractSpinBox* a = XAbstractSpinBox_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
        XAbstractSpinBox* b = XAbstractSpinBox_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
        XRegularExpressionValidator* regex = xqva_regex("[0-9]+");
        XRegularExpressionValidatorAdapter* ad;
        XQ_REQUIRE(a != NULL && b != NULL && regex != NULL);
        ad = xqva_adapter(regex);
        XQ_REQUIRE(ad != NULL);
        XAbstractSpinBox_setValidator_2(a, (XValidator*)ad);
        XQ_CHECK(XAbstractSpinBox_validatorObject(a) == (XValidator*)ad);
        XClassCopy(b, a);
        XQ_CHECK(XAbstractSpinBox_validatorObject(b) == NULL);
        XQ_CHECK(XLineEdit_validatorObject(XAbstractSpinBox_lineEdit(b)) == NULL);
        XAbstractSpinBox_setValidator_2(a, NULL);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
        XClassDelete((XClass*)ad);
        XClassDelete((XClass*)regex);
    }
    /* XComboBox：拷贝/移动后四个借用字段全清；析构平账。 */
    {
        XComboBox* a = XComboBox_create(NULL, 0);
        XComboBox* b = XComboBox_create(NULL, 0);
        XRegularExpressionValidator* regex = xqva_regex("[0-9]+");
        XRegularExpressionValidatorAdapter* ad;
        XQ_REQUIRE(a != NULL && b != NULL && regex != NULL);
        ad = xqva_adapter(regex);
        XQ_REQUIRE(ad != NULL);
        XComboBox_setEditable(a, true);
        XComboBox_setValidator_2(a, (XValidator*)ad);
        XClassCopy(b, a);
        XQ_CHECK(XComboBox_validator(b) == NULL);
        XQ_CHECK(XComboBox_validatorObject(b) == NULL);
        XClassMove(a, b);
        XQ_CHECK(XComboBox_validator(a) == NULL);
        XQ_CHECK(XComboBox_validatorObject(a) == NULL);
        XQ_CHECK(XComboBox_validator(b) == NULL);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
        XClassDelete((XClass*)ad);
        XClassDelete((XClass*)regex);
    }
}

/* ==================== ⑤ setLineEdit 外部类型化钩子保留（§B10 复核⑤ 口径） ==================== */

/** @brief 外部行编辑自设的 XValidator 不得被 setLineEdit 静默清掉
 *         （Qt setLineEdit 只在行编辑还没装校验器时才补内部桥，
 *         qabstractspinbox.cpp:684-685 / qcombobox.cpp:1831-1848）。
 *         三控件的「只摘自己装的钩子」身份判定分别覆盖：
 *         XComboBox（m_validatorTypedInstalled）、XAbstractSpinBox
 *         （同名字段）、XLineEdit（自身即宿主，不适用）。 */
XQ_TEST(XValidator, externalTypedHookPreservedOnSetLineEdit)
{
    /* XComboBox：外部编辑框先装自己的 XValidator，再挂给组合框。 */
    {
        XComboBox* cb = XComboBox_create(NULL, 0);
        XLineEdit* ext;
        XValidator base;               /* 外部自设的一枚基类校验器。 */
        XQ_REQUIRE(cb != NULL);
        ext = XLineEdit_create(NULL, 0);
        XQ_REQUIRE(ext != NULL);
        XValidator_init(&base, NULL);
        XLineEdit_setValidator_2(ext, &base);
        XQ_REQUIRE(XLineEdit_validatorObject(ext) == &base);
        XComboBox_setEditable(cb, true);
        XComboBox_setLineEdit(cb, ext);
        XCoreApplication_processEvents(XEventLoop_AllEvents); /* 旧框 deleteLater 回收拍。 */
        /* 组合框未持有校验器 ⇒ 外部钩子原样保留（不得按「有类型化面就清」
         * 误删，这正是 m_validatorTypedInstalled 身份判定的意义）。 */
        XQ_CHECK_MSG(XLineEdit_validatorObject(ext) == &base,
                     "setLineEdit 不得清掉外部编辑框自设的 XValidator（复核⑤）");
        XClassDelete((XClass*)cb);     /* ext 已是 cb 子控件，随 cb 析构。 */
        XClassDeinit((XClass*)&base);
    }
    /* XAbstractSpinBox：同口径。 */
    {
        XAbstractSpinBox* spin = XAbstractSpinBox_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
        XLineEdit* ext;
        XValidator base;
        XQ_REQUIRE(spin != NULL);
        ext = XLineEdit_create(NULL, 0);
        XQ_REQUIRE(ext != NULL);
        XValidator_init(&base, NULL);
        XLineEdit_setValidator_2(ext, &base);
        XAbstractSpinBox_setLineEdit(spin, ext);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XQ_CHECK_MSG(XLineEdit_validatorObject(ext) == &base,
                     "spinbox setLineEdit 不得清掉外部编辑框自设的 XValidator");
        XClassDelete((XClass*)spin);
        XClassDeinit((XClass*)&base);
    }
}

#endif /* XGUI_ON && XWIDGET_ON && XLINEEDIT_ON && XCOMBOBOX_ON &&
          XABSTRACTSPINBOX_ON && XRegularExpression_ON */
