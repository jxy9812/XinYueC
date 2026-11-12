/**
 * @file       XValidator.c
 * @brief      XValidator 输入校验抽象基类 + Widget 侧正则适配器实现。
 * @details    对齐 Qt 6.8 QValidator（qvalidator.h / qvalidator.cpp）。
 *             _base 契约见《代码风格…注意事项》§497；本文件所有虚槽实现
 *             都是 static VX* 函数，只在 *_class_init 注册，不对外暴露。
 */
#include "CXinYueConfig.h"

#if XGUI_ON

#include "XValidator.h"
#include "XMemory.h"
#include "XStringUtils.h"
#if XRegularExpression_ON
#include "XRegularExpressionValidator.h"
#endif

/* ==================== 基类虚槽缺省实现（static，仅经 class_init 注册） ==================== */

/**
 * @brief Validate 缺省实现：恒 Acceptable（任务书口径；对标
 *        QAbstractSpinBox::validate 基类默认 qabstractspinbox.cpp:561-563）。
 * @note  Qt 把 QValidator::validate 声明为纯虚；本库按任务书给出可实例化
 *        的基类缺省（「基类缺省 Acceptable」），使「无校验」场景可直接用
 *        基类对象。派生回落父类一律 XClass_Parent，不经本函数直调。
 */
static XValidatorState VXValidator_validate(XValidator* self, char** input,
                                            int* pos)
{
    (void)self;
    (void)input;
    (void)pos;
    return XValidatorState_Acceptable;
}

/** @brief Fixup 缺省实现：空操作（qvalidator.cpp:250「the default does
 *         nothing」）。 */
static void VXValidator_fixup(XValidator* self, char** input)
{
    (void)self;
    (void)input;
}

XVtable* XValidator_class_init(void)
{
    /* 新槽按枚举顺序追加；与 XAbstractSpinBox.c 的 class_init 同款
     * （ADD_FUNC_LIST 撑起 m_size + OVERLOAD 逐槽钉实现）。 */
    void* protectedSlots[] = {
        VXValidator_validate,
        VXValidator_fixup
    };

    XVTABLE_INIT_DEFAULT(XValidator)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(protectedSlots);
    XVTABLE_OVERLOAD_DEFAULT(EXValidator_Validate, VXValidator_validate);
    XVTABLE_OVERLOAD_DEFAULT(EXValidator_Fixup, VXValidator_fixup);
    return XVTABLE_DEFAULT;
}

void XValidator_init(XValidator* self, XObject* parent)
{
    if (!self) return;
    /* init 显式清零（MSVC /RTC1：栈上局部结构体交给框架前一律清零）；
     * XObject_init 会再清基类之后的部分，此处整块先归零与 XButtonGroup.c
     * 的 XButtonGroup_init 同款。 */
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    if (parent)
        XObject_setParent((XObject*)self, parent);
    XClassSetVtable(self, XValidator);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
}

XValidator* XValidator_create_ex(XMemoryType memory, XObject* parent)
{
    XValidator* self = (XValidator*)XMemory_malloc(sizeof(XValidator), memory);
    if (!self) return NULL;
    XValidator_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 唯一公开虚入口（*_base：只查表、只分派） ==================== */

XValidatorState XValidator_validate_base(XValidator* self, char** input,
                                         int* pos)
{
    if (ISNULL(self, "XValidator") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return XValidatorState_Invalid;
    return XClassGetVirtualFunc(self, EXValidator_Validate,
                                XValidatorState(*)(XValidator*, char**, int*))(
        self, input, pos);
}

void XValidator_fixup_base(XValidator* self, char** input)
{
    if (ISNULL(self, "XValidator") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    /* void 槽的空槽保护（§497 条款 5）。 */
    if (!XClassGetVirtualFunc(self, EXValidator_Fixup, bool)) return;
    XClassGetVirtualFunc(self, EXValidator_Fixup,
                         void(*)(XValidator*, char**))(self, input);
}

/* ==================== Widget 侧正则适配器（借用转发，不改 XCore） ==================== */

#if XRegularExpression_ON

/**
 * @brief Validate 派生槽：转发框架现成的
 *        XRegularExpressionValidator_validate_utf8（铁律 4 不手搓）。
 * @details 数值逐值同口径（两份枚举都是 0/1/2），显式 switch 而非强转，
 *          避免任一枚举将来改动时静默串位。
 *          Invalid 时按 Qt qvalidator.cpp:1011 `pos = input.size()` 把
 *          *pos 置为输入长度——本库位置口径是 UTF-8 字节偏移，故取
 *          XStrlen(*input)（XString 内码长度不适用，见
 *          XRegularExpressionValidator.c:147 的 position 语义差异）。
 *          **不修改 *input**：QRegularExpressionValidator::validate 不改写
 *          输入串（qvalidator.cpp:994-1013），缓冲所有权仍归调用方。
 *          无借用正则时经 XClass_Parent 回落基类缺省 Acceptable（§497
 *          条款 4：派生槽内回落父类必须走 XClass_Parent，不能调自身
 *          _base 造成递归）。
 */
static XValidatorState VXRegularExpressionValidatorAdapter_validate(
        XValidator* self, char** input, int* pos)
{
    XRegularExpressionValidatorAdapter* adapter =
            (XRegularExpressionValidatorAdapter*)self;
    const char* text;
    int64_t position = 0;
    XRegularExpressionValidator_State st;
    if (!adapter || !adapter->m_regexValidator) {
        return XClass_Parent(XValidator, EXValidator_Validate,
                             XValidatorState(*)(XValidator*, char**, int*))(
            self, input, pos);
    }
    text = (input && *input) ? *input : "";
    st = XRegularExpressionValidator_validate_utf8(
            adapter->m_regexValidator, text, &position);
    switch (st) {
    case XRegularExpressionValidator_Acceptable:
        return XValidatorState_Acceptable;
    case XRegularExpressionValidator_Intermediate:
        return XValidatorState_Intermediate;
    case XRegularExpressionValidator_Invalid:
    default:
        if (pos) *pos = (int)XStrlen(text);
        return XValidatorState_Invalid;
    }
}

XVtable* XRegularExpressionValidatorAdapter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XRegularExpressionValidatorAdapter)
    XVTABLE_INHERIT_XCLASS(XValidator);
    /* 只重载 Validate；Fixup 沿用 XValidator 的空实现，与 Qt
     * QRegularExpressionValidator 不重写 fixup 完全一致。 */
    XVTABLE_OVERLOAD_DEFAULT(EXValidator_Validate,
                             VXRegularExpressionValidatorAdapter_validate);
    return XVTABLE_DEFAULT;
}

void XRegularExpressionValidatorAdapter_init(
        XRegularExpressionValidatorAdapter* self,
        const struct XRegularExpressionValidator* regex)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XValidator_init((XValidator*)self, NULL);
    XClassSetVtable(self, XRegularExpressionValidatorAdapter);
    self->m_regexValidator = regex;
}

XRegularExpressionValidatorAdapter*
XRegularExpressionValidatorAdapter_create_ex(
        XMemoryType memory, const struct XRegularExpressionValidator* regex)
{
    XRegularExpressionValidatorAdapter* self =
            (XRegularExpressionValidatorAdapter*)XMemory_malloc(
                    sizeof(XRegularExpressionValidatorAdapter), memory);
    if (!self) return NULL;
    XRegularExpressionValidatorAdapter_init(self, regex);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XRegularExpressionValidatorAdapter_setRegularExpression(
        XRegularExpressionValidatorAdapter* self,
        const struct XRegularExpressionValidator* regex)
{
    if (!self) return;
    self->m_regexValidator = regex;  /* 借用，不取得所有权，不解引用。 */
}

const struct XRegularExpressionValidator*
XRegularExpressionValidatorAdapter_regularExpression_const(
        const XRegularExpressionValidatorAdapter* self)
{
    return self ? self->m_regexValidator : NULL;
}

#endif /* XRegularExpression_ON */

#endif /* XGUI_ON */
