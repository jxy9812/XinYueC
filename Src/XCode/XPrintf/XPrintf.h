#ifndef XPRINTF_H
#define XPRINTF_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XTypes.h"
#include"XChar.h"
#include<stdio.h>
#include<stdint.h>
#include<stddef.h>
#include<stdbool.h>

/* [2026-10-31 重新定义] 输出重定合与 Shell 远程后端解耦：GUI 测试菜单页的输出区需要把 XPrintf 两样标准 C 输出收进控件\uff0c这事实与“有吗 Shell 远程后端”无关：松或 Shell 后端全关时，Shell 远程后端功能并不会再应杉发出失请来金不存在的问题。
 *   其中招口失之后变开效＊隌入式机器上知识的平台还回参数量。 */
#ifndef XPRINTF_OUTPUT_REDIRECT_ON
#define XPRINTF_OUTPUT_REDIRECT_ON 1
#endif
#if XPRINTF_OUTPUT_REDIRECT_ON
/**
 * @brief XPrintf 输出重定向回调。
 * @details
 * 回调接收 UTF-8 字节流。回调必须消费全部 size 字节并返回 size；返回负数
 * 或短写表示输出失败。userData 由调用方管理，XPrintf 不保存其所有权。
 * 回调只在对应输出作用域有效，不能保存 data 指针。
 * @param userData 调用方上下文指针；可为 NULL。
 * @param data 待输出的 UTF-8 字节；size 非零时不能为空。
 * @param size data 的字节数；不包含额外的 NUL 终止字节。
 * @return 成功消费的字节数；失败返回负数或小于 size 的值。
 */
typedef int64_t (*XPrintfOutputWrite)(void* userData, const char* data,
                                      size_t size);

/**
 * @brief 作用域激活签名：由 XPrintf_outputPush 写入，XPrintf_outputPop 校验。
 * @details
 * pop 侧只认「签名匹配」的作用域，不认 active 字节——未初始化（栈上残留
 * 0xCC/0x00 混杂）或已被复用改写的作用域一律视为「未 push」并直接返回。
 * 这条守卫是硬要求：pop 唯一的破坏性动作是把 previousWrite 回写到 TLS
 * 回调槽，一旦作用域是脏内存，回写的就是垃圾函数指针，下一次任意
 * XPrintf 调用即跳转到该地址（2026-10-31 demo 实测：槽位变
 * 0xCCCCCCCCCCCCCCCC，崩溃点落在 xprintf_write_redirect 的间接调用）。
 */
#define XPRINTF_SCOPE_MAGIC 0x58505253u /* 'XPRS' */

/**
 * @brief XPrintf 输出重定向作用域保存对象。
 * @details
 * 作用域支持嵌套，push 时保存当前线程的上一级输出目标，pop 时恢复。对象
 * 由调用方提供并仅在 push 到 pop 期间使用；不能把同一个对象重复 push。
 * **调用方应零初始化（`XPrintfOutputScope scope = {0};`）**；未零初始化虽
 * 由 magic 守卫兜底为「安全但未激活」（pop 为 no-op），但 push 的返回值
 * 仍是唯一权威的激活判据——无条件 pop 会静默丢弃 push 失败的信息。
 */
typedef struct XPrintfOutputScope {
    XPrintfOutputWrite previousWrite; /**< push 前的回调；由实现保存。 */
    void* previousUserData;           /**< push 前的回调上下文；由实现保存。 */
    bool active;                      /**< 是否已 push 且尚未 pop。 */
    uint32_t magic;                   /**< 激活签名；见 XPRINTF_SCOPE_MAGIC。 */
} XPrintfOutputScope;

/**
 * @brief 将当前调用线程的 XPrintf 输出重定向到指定回调。
 * @param scope 调用方提供的作用域保存对象；不能为空且不得重复使用未 pop 的对象。
 * @param write 输出回调；不能为空，负责将 UTF-8 数据发送给调用方。
 * @param userData 回调上下文；可为 NULL，不由 XPrintf 释放。
 * @return 参数有效且重定向成功返回 true；参数无效或 scope 已激活返回 false。
 * @note 输出目标按线程保存；Shell 会在命令回调同步执行期间自动建立该作用域。
 */
bool XPrintf_outputPush(XPrintfOutputScope* scope, XPrintfOutputWrite write,
                        void* userData);

/**
 * @brief 恢复 XPrintf 输出重定向作用域的上一级目标。
 * @param scope 已由 XPrintf_outputPush 激活的作用域；可为 NULL。
 * @return 无；scope 未激活时不执行操作。
 */
void XPrintf_outputPop(XPrintfOutputScope* scope);

/**
 * @brief 把抽水线程收集的标准 C 输出（printf 等）在当前线程转投给 TLS 回调。
 * @note  \u7531 processEvents \u5728\u4e3b\u7ebf\u7a0b\u8c03\u7528\uff1b\u65e0\u6d3b\u52a8\u4f5c\u7528\u57df\u65f6\u7a7a\u64cd\u4f5c\u3002 */
void XPrintf_drainRedirectedOutput(void);
#endif

// -------------------------- 打印函数 --------------------------
/**
 * @brief 格式化打印 UTF-8 字符串
 * @param format 格式化字符串（UTF-8）
 * @param ... 可变参数列表
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrintf(const char* format, ...);
/** @brief 打印带固定前缀和换行的格式化文本。 */
int XPrintf_line(const char* prefix, const char* format, ...);
/**
 * @brief 打印 XString 字符串（使用本地编码）
 * @param str XString 对象指针
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrintf_string(const XString* str);

/**
 * @brief 打印 UTF-8 编码字符串
 * @param utf8_str 待打印的 UTF-8 字符串
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrintf_3(const char* utf8_str);

int XPrintf_4(const XByteArray* array);

// 输出单个XChar字符
int XPrintf_5(XChar* ch);
#ifdef __cplusplus
}
#endif	
#endif
