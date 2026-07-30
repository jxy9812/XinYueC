/**
 * @file XRegularExpressionValidator.h
 * @brief Qt 6.8 QRegularExpressionValidator 对齐实现。
 */
#ifndef XREGULAREXPRESSIONVALIDATOR_H
#define XREGULAREXPRESSIONVALIDATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#if !defined(XREGULAREXPRESSIONVALIDATOR_API_H) && XRegularExpression_ON
#define XREGULAREXPRESSIONVALIDATOR_API_H

#include "XClass.h"
#include "XRegularExpression.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 输入校验结果。
 * @details 数值与 Qt QValidator::State 保持一致。
 */
typedef enum XRegularExpressionValidator_State {
    XRegularExpressionValidator_Invalid = 0, ///< 输入不匹配
    XRegularExpressionValidator_Intermediate = 1, ///< 追加字符后可能形成有效匹配
    XRegularExpressionValidator_Acceptable = 2 ///< 输入已经完整匹配
} XRegularExpressionValidator_State;

XCLASS_DEFINE_BEGING(XRegularExpressionValidator)
XCLASS_DEFINE_EXTEND_END(XRegularExpressionValidator, XClass)

/**
 * @brief 使用正则表达式校验输入字符串。
 */
typedef struct XRegularExpressionValidator {
    XClass m_class;                         ///< 基类虚函数表，必须位于第一位
    XRegularExpression m_originalExpression; ///< 用户设置的原始表达式
    XRegularExpression m_usedExpression;   ///< 自动加锚点后的实际表达式
} XRegularExpressionValidator;

/**
 * @brief 初始化 XRegularExpressionValidator 类虚函数表。
 * @return 类虚函数表指针，由框架缓存和管理，不得由调用者释放。
 */
XVtable* XRegularExpressionValidator_class_init(void);
/**
 * @brief 初始化校验器对象。
 * @param validator 待初始化的栈对象或已分配对象；不能传入 NULL。
 */
void XRegularExpressionValidator_init(XRegularExpressionValidator* validator);
/**
 * @brief 创建默认校验器。
 * @return 成功返回堆对象，调用者必须使用 XRegularExpressionValidator_delete_base 释放；失败返回 NULL。
 */
XRegularExpressionValidator* XRegularExpressionValidator_create(void);
/**
 * @brief 使用指定正则表达式创建校验器。
 * @param expression 初始正则表达式；函数只读取该对象，不能传入 NULL。
 * @return 成功返回新校验器，调用者必须使用 XRegularExpressionValidator_delete_base 释放；失败返回 NULL。
 */
XRegularExpressionValidator* XRegularExpressionValidator_create_ex(
        const XRegularExpression* expression);
/**
 * @brief 创建校验器的深拷贝。
 * @param other 源校验器；不能传入 NULL。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL。
 */
XRegularExpressionValidator* XRegularExpressionValidator_create_copy(const XRegularExpressionValidator* other);
/**
 * @brief 创建校验器的移动对象。
 * @param other 源校验器；成功后源对象进入可反初始化状态。
 * @return 成功返回新堆对象，调用者负责释放；失败返回 NULL，源对象不变。
 */
XRegularExpressionValidator* XRegularExpressionValidator_create_move(XRegularExpressionValidator* other);

/**
 * @brief 校验器的基础生命周期操作宏。
 * @details 宏统一转发到 XClass 虚函数，保证校验器内部两个正则对象按生命周期规则释放。
 * @note delete_base 仅用于堆对象；栈对象只调用 deinit_base。
 */
#define XRegularExpressionValidator_deinit_base XClass_deinit_base
#define XRegularExpressionValidator_delete_base XClass_delete_base
#define XRegularExpressionValidator_copy_base XClass_copy_base
#define XRegularExpressionValidator_move_base XClass_move_base

/**
 * @brief 获取当前正则表达式的副本。
 * @param validator 校验器对象
 * @return 新创建的正则表达式，调用者负责 delete_base；参数无效时返回 NULL
 */
XRegularExpression* XRegularExpressionValidator_regularExpression(const XRegularExpressionValidator* validator);

/**
 * @brief 获取当前正则表达式的内部只读引用。
 * @param validator 校验器对象
 * @return 内部正则表达式指针，不得释放
 */
const XRegularExpression* XRegularExpressionValidator_regularExpression_const(const XRegularExpressionValidator* validator);

/**
 * @brief 设置用于校验的正则表达式。
 * @param validator 校验器对象
 * @param expression 新的正则表达式；NULL 等价于空表达式
 */
void XRegularExpressionValidator_setRegularExpression(XRegularExpressionValidator* validator,
                                                       const XRegularExpression* expression);

/**
 * @brief 校验 UTF-16 输入。
 * @param validator 校验器对象
 * @param input 待校验的字符串
 * @param position 输入位置，可为 NULL；输入无效时更新为字符串长度
 * @return Invalid、Intermediate 或 Acceptable
 */
XRegularExpressionValidator_State XRegularExpressionValidator_validate(
        const XRegularExpressionValidator* validator, const XString* input, int64_t* position);

/**
 * @brief 校验 UTF-8 输入。
 * @param validator 校验器对象
 * @param input UTF-8 待校验字符串
 * @param position 输入位置，可为 NULL
 * @return Invalid、Intermediate 或 Acceptable
 */
XRegularExpressionValidator_State XRegularExpressionValidator_validate_utf8(
        const XRegularExpressionValidator* validator, const char* input, int64_t* position);

#endif /* XREGULAREXPRESSIONVALIDATOR_API_H */

#ifdef __cplusplus
}
#endif

#endif /* XREGULAREXPRESSIONVALIDATOR_H */
