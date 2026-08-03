/**
 * @file XRegularExpressionValidator.c
 * @brief Qt 6.8 QRegularExpressionValidator 对齐实现。
 */

#include "XRegularExpressionValidator.h"
#include "XMemory.h"
#include <string.h>

static void VXRegularExpressionValidator_deinit(XRegularExpressionValidator* validator)
{
    if (!validator) return;
    XRegularExpression_deinit_base(&validator->m_originalExpression);
    XRegularExpression_deinit_base(&validator->m_usedExpression);
}

static void VXRegularExpressionValidator_copy(XRegularExpressionValidator* dest,
                                               const XRegularExpressionValidator* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRegularExpressionValidator_init(dest);
    XRegularExpression_copy_base(&dest->m_originalExpression, &src->m_originalExpression);
    XRegularExpression_copy_base(&dest->m_usedExpression, &src->m_usedExpression);
}

static void VXRegularExpressionValidator_move(XRegularExpressionValidator* dest,
                                               XRegularExpressionValidator* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRegularExpressionValidator_init(dest);
    XRegularExpression_move_base(&dest->m_originalExpression, &src->m_originalExpression);
    XRegularExpression_move_base(&dest->m_usedExpression, &src->m_usedExpression);
}

XVtable* XRegularExpressionValidator_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XRegularExpressionValidator)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRegularExpressionValidator_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXRegularExpressionValidator_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXRegularExpressionValidator_move);
    return XVTABLE_DEFAULT;
}

void XRegularExpressionValidator_init(XRegularExpressionValidator* validator)
{
    if (!validator) return;
    memset(validator, 0, sizeof(*validator));
    XClass_init(validator);
    XClassSetVtable(validator, XRegularExpressionValidator);
    XRegularExpression_init(&validator->m_originalExpression);
    XRegularExpression_init(&validator->m_usedExpression);
}

XRegularExpressionValidator* XRegularExpressionValidator_create(void)
{
    XRegularExpressionValidator* validator =
            (XRegularExpressionValidator*)XMalloc_System(sizeof(*validator));
    if (!validator) return NULL;
    XRegularExpressionValidator_init(validator);
    Set_Class_MemoryFree(validator, XFree_System);
    return validator;
}

XRegularExpressionValidator* XRegularExpressionValidator_create_ex(
        const XRegularExpression* expression)
{
    if (!expression) return NULL;
    XRegularExpressionValidator* validator = XRegularExpressionValidator_create();
    if (!validator) return NULL;
    XRegularExpressionValidator_setRegularExpression(validator, expression);
    return validator;
}

XRegularExpressionValidator* XRegularExpressionValidator_create_copy(
        const XRegularExpressionValidator* other)
{
    if (!other) return NULL;
    XRegularExpressionValidator* validator = XRegularExpressionValidator_create();
    if (!validator) return NULL;
    XRegularExpressionValidator_copy_base(validator, other);
    return validator;
}

XRegularExpressionValidator* XRegularExpressionValidator_create_move(
        XRegularExpressionValidator* other)
{
    if (!other) return NULL;
    XRegularExpressionValidator* validator = XRegularExpressionValidator_create();
    if (!validator) return NULL;
    XRegularExpressionValidator_move_base(validator, other);
    return validator;
}

XRegularExpression* XRegularExpressionValidator_regularExpression(
        const XRegularExpressionValidator* validator)
{
    return validator ? XRegularExpression_create_copy(&validator->m_originalExpression) : NULL;
}

const XRegularExpression* XRegularExpressionValidator_regularExpression_const(
        const XRegularExpressionValidator* validator)
{
    return validator ? &validator->m_originalExpression : NULL;
}

void XRegularExpressionValidator_setRegularExpression(XRegularExpressionValidator* validator,
                                                       const XRegularExpression* expression)
{
    if (!validator) return;
    if (expression) {
        XRegularExpression_copy_base(&validator->m_originalExpression, expression);
        XRegularExpression_copy_base(&validator->m_usedExpression, expression);
    } else {
        XRegularExpression_setPattern_utf8(&validator->m_originalExpression, "");
        XRegularExpression_setPatternOptions(&validator->m_originalExpression,
                                              XRegularExpression_NoPatternOption);
        XRegularExpression_setPattern_utf8(&validator->m_usedExpression, "");
        XRegularExpression_setPatternOptions(&validator->m_usedExpression,
                                              XRegularExpression_NoPatternOption);
    }

    XString* pattern = XRegularExpression_pattern(&validator->m_originalExpression);
    XString* anchored = XRegularExpression_anchoredPattern_2(pattern);
    if (anchored) {
        XRegularExpression_setPattern(&validator->m_usedExpression, anchored);
        XString_delete_base(anchored);
    }
    if (pattern) XString_delete_base(pattern);
}

XRegularExpressionValidator_State XRegularExpressionValidator_validate(
        const XRegularExpressionValidator* validator, const XString* input, int64_t* position)
{
    if (!validator) return XRegularExpressionValidator_Invalid;
    const XString* original = XRegularExpression_pattern_const(&validator->m_originalExpression);
    if (!original || XString_isEmpty_base(original)) return XRegularExpressionValidator_Acceptable;

    XRegularExpressionMatch* match = XRegularExpression_match(
            &validator->m_usedExpression, input, 0,
            XRegularExpression_PartialPreferCompleteMatch,
            XRegularExpression_NoMatchOption);
    if (!match) return XRegularExpressionValidator_Invalid;

    XRegularExpressionValidator_State state;
    if (XRegularExpressionMatch_hasMatch(match)) {
        state = XRegularExpressionValidator_Acceptable;
    } else if ((!input || XString_isEmpty_base(input)) ||
               XRegularExpressionMatch_hasPartialMatch(match)) {
        state = XRegularExpressionValidator_Intermediate;
    } else {
        if (position) *position = input ? (int64_t)XString_size_base(input) : 0;
        state = XRegularExpressionValidator_Invalid;
    }
    XRegularExpressionMatch_delete_base(match);
    return state;
}

XRegularExpressionValidator_State XRegularExpressionValidator_validate_utf8(
        const XRegularExpressionValidator* validator, const char* input, int64_t* position)
{
    XString* value = XString_create_utf8(input ? input : "");
    if (!value) return XRegularExpressionValidator_Invalid;
    XRegularExpressionValidator_State state =
            XRegularExpressionValidator_validate(validator, value, position);
    XString_delete_base(value);
    return state;
}
