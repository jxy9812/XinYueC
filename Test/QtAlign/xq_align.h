/**
 * @file       xq_align.h
 * @brief      Qt 6.8 对齐测试底座：自注册用例 + 断言 + 自动堆计数。
 * @details    本目录（Test/QtAlign）下每个 `.c` 文件登记一组对齐用例，
 *             由运行器 `xq_align_run.c` 统一执行（**无独立 main**）：
 *             - 控制台宿主：`./bin/XinYueC_Static --test qt-align`；
 *             - 交互 Test 菜单：「Qt6.8 对齐(Test/QtAlign) → 运行全部对齐用例」。
 *
 *             设计要点：
 *             ① **自注册**：用例经编译期构造函数登记进全局链表，新增文件
 *                无需改任何注册表/主程序——避免多路并行开发时共享文件冲突；
 *             ② **可过滤**：`--list` / `--filter=<子串>` 便于按类回归；
 *             ③ **泄漏门**：运行器对每个用例前后各取一次框架分配器计数
 *                （systemBytes / poolUsedBytes 合计），默认零容忍；
 *                减少/持平不算泄漏，增长即 `[LEAK]` 判失败；
 *             ④ 断言失败不中断用例（`XQ_CHECK`），需要提前返回时用
 *                `XQ_REQUIRE`。
 *
 * @note       C99，无第三方依赖；MSVC 与 GCC/Clang 均可编译。
 * @author     XinYueC 团队
 */
#ifndef XQ_ALIGN_H
#define XQ_ALIGN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单个用例的执行函数。 */
typedef void (*XQTestFn)(void);

/** @brief 自注册用例节点（静态存储，运行器只读）。 */
typedef struct XQTestCase
{
    const char* name;              /**< 用例名，约定 `<类名>.<场景>`。 */
    XQTestFn fn;                   /**< 执行函数。 */
    struct XQTestCase* next;       /**< 注册链表后继。 */
} XQTestCase;

/** @brief 当前用例累计断言数（运行器在每个用例前清零）。 */
extern int xq_checks;

/** @brief 当前用例累计失败数（运行器在每个用例前清零）。 */
extern int xq_failures;

/**
 * @brief 把一个静态用例节点登记进全局链表（构造期调用）。
 * @param tc 非 NULL、生命周期覆盖整个进程。
 */
void xq_register_case(XQTestCase* tc);

/**
 * @brief 登记一条断言失败并按需打印。
 * @param cond   断言是否通过。
 * @param expr   表达式原文（打印用）。
 * @param file   源文件。
 * @param line   行号。
 * @param fmt    附加说明（可为 NULL）。
 * @param ...    fmt 的参数。
 */
void xq_check_impl(bool cond, const char* expr, const char* file, int line,
                   const char* fmt, ...);

/**
 * @brief 带上下文的失败记录（不依赖表达式）。
 */
void xq_fail_at(const char* file, int line, const char* fmt, ...);

/** @brief 记录一次失败（供 XQ_REQUIRE 使用）。 */
void xq_fail_simple(const char* expr, const char* file, int line);

/* ---- 构造期自注册：GCC/Clang 用 constructor 属性，MSVC 用 CRT 段 ---- */
#if defined(_MSC_VER)
#pragma section(".CRT$XCU", read)
#define XQ_REGISTER_(regfn, tc)                                            \
    static void __cdecl regfn(void) { xq_register_case(tc); }              \
    __declspec(allocate(".CRT$XCU")) static void(__cdecl* regfn##_entry)(void) = regfn;
#else
#define XQ_REGISTER_(regfn, tc)                                            \
    static void regfn(void) __attribute__((constructor));                  \
    static void regfn(void) { xq_register_case(tc); }
#endif

/**
 * @brief 登记一个对齐用例。
 * @param tag  类名/模块名（标识符），如 XObject、XLabel。
 * @param name 场景名（标识符），如 parentChildDelete。
 * @note 用例显示名自动拼为 `<tag>.<name>`。
 */
#define XQ_TEST(tag, name)                                                 \
    static void xq_case_##tag##_##name(void);                              \
    static XQTestCase xq_tc_##tag##_##name = {                              \
        #tag "." #name, xq_case_##tag##_##name, NULL};                     \
    XQ_REGISTER_(xq_reg_##tag##_##name, &xq_tc_##tag##_##name)             \
    static void xq_case_##tag##_##name(void)

/** @brief 断言条件为真（失败继续执行）。 */
#define XQ_CHECK(cond) xq_check_impl(!!(cond), #cond, __FILE__, __LINE__, NULL)

/** @brief 带说明的断言。 */
#define XQ_CHECK_MSG(cond, ...) \
    xq_check_impl(!!(cond), #cond, __FILE__, __LINE__, __VA_ARGS__)

/** @brief 断言指针非空。 */
#define XQ_CHECK_PTR(p) \
    xq_check_impl((p) != NULL, #p " != NULL", __FILE__, __LINE__, NULL)

/** @brief 断言两个整数相等。 */
#define XQ_EQ_I64(a, b)                                                    \
    do {                                                                   \
        long long xq_a_ = (long long)(a);                                  \
        long long xq_b_ = (long long)(b);                                  \
        xq_check_impl(xq_a_ == xq_b_, #a " == " #b, __FILE__, __LINE__,    \
                      "left=%lld right=%lld", xq_a_, xq_b_);               \
    } while (0)

/** @brief 断言两个无符号大小相等。 */
#define XQ_EQ_SIZE(a, b)                                                   \
    do {                                                                   \
        size_t xq_a_ = (size_t)(a);                                        \
        size_t xq_b_ = (size_t)(b);                                        \
        xq_check_impl(xq_a_ == xq_b_, #a " == " #b, __FILE__, __LINE__,    \
                      "left=%zu right=%zu", xq_a_, xq_b_);                 \
    } while (0)

/** @brief 断言两个 C 字符串相等（NULL 与空串不互通）。 */
#define XQ_EQ_STR(a, b)                                                    \
    do {                                                                   \
        const char* xq_a_ = (const char*)(a);                              \
        const char* xq_b_ = (const char*)(b);                              \
        bool xq_same_ = (xq_a_ == NULL && xq_b_ == NULL) ||                \
                        (xq_a_ != NULL && xq_b_ != NULL &&                 \
                         strcmp(xq_a_, xq_b_) == 0);                       \
        xq_check_impl(xq_same_, #a " == " #b, __FILE__, __LINE__,          \
                      "left=%s right=%s", xq_a_ ? xq_a_ : "(null)",        \
                      xq_b_ ? xq_b_ : "(null)");                          \
    } while (0)

/** @brief 断言为真，否则立即结束当前用例。 */
#define XQ_REQUIRE(cond)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            xq_fail_simple(#cond, __FILE__, __LINE__);                     \
            return;                                                        \
        }                                                                  \
    } while (0)

#ifdef __cplusplus
}
#endif

#include <string.h>

#endif /* XQ_ALIGN_H */
