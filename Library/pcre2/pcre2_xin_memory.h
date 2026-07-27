/**
 * @file pcre2_xin_memory.h
 * @brief PCRE2 与 XinYueC XMemory 的内存分配桥接。
 */

#ifndef PCRE2_XIN_MEMORY_H
#define PCRE2_XIN_MEMORY_H

#include "pcre2.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建使用 XinYueC XMemory 分配器的 PCRE2 通用上下文。
 *
 * 由该上下文创建的 compile context、match context、match data 和 JIT
 * stack 会继承同一套 XMemory 分配器。调用者必须使用对应的 PCRE2
 * *_free 函数释放这些对象。
 *
 * @return 成功返回 PCRE2 通用上下文；内存不足返回 NULL。
 */
pcre2_general_context *pcre2_xin_general_context_create(void);

#ifdef __cplusplus
}
#endif

#endif /* PCRE2_XIN_MEMORY_H */
