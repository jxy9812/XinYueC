/**
 * @file       XCommonStyle.h
 * @brief      嵌入式公共样式公开 API。
 * @details    对标 Qt 6.8 QCommonStyle：提供各平台共享的基元/控件绘制
 *             （边框、按钮面板、复选/单选指示器、箭头、进度条槽与块、
 *             页签形状等）；XFusionStyle 继承本类并覆盖 Fusion 主题
 *             细节。实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX、
 *             Qt 或其他平台 API。
 */
#ifndef XCOMMONSTYLE_H
#define XCOMMONSTYLE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XStyle.h"

#if XSTYLE_ON

XCLASS_DEFINE_BEGING(XCommonStyle)
XCLASS_DEFINE_EXTEND_END(XCommonStyle, XStyle)

/**
 * @brief 公共样式（对标 Qt 6.8 QCommonStyle）。
 *
 *        提供 Qt 各平台共享的基元/控件绘制：边框、按钮面板、
 *        复选/单选指示器、箭头、进度条槽与块、页签形状等。
 *        XFusionStyle 继承本类并覆盖 Fusion 主题细节。
 */
typedef struct XCommonStyle
{
    XStyle m_base;   /**< 基类成员；必须是第一个。 */
} XCommonStyle;

/**
 * @brief 初始化 XCommonStyle 类的虚函数表。
 * @return 指向初始化完成的 XVtable 的指针。
 */
XVtable* XCommonStyle_class_init(void);

/**
 * @brief 初始化嵌入式公共样式。
 *
 * @param self 目标样式指针，不能为空。
 * @return 无返回值。
 */
void XCommonStyle_init(XCommonStyle* self);

/**
 * @brief 堆上创建公共样式。
 *
 * @param memory 内存类型。
 * @return 样式指针；分配失败返回 NULL。
 */
XCommonStyle* XCommonStyle_create_ex(XMemoryType memory);
#define XCommonStyle_create() XCommonStyle_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief 文本省略（按 mode 插入省略号使宽度不超 maxW；对标 Qt
 *        fontMetrics elidedText；UTF-8 码点边界切割）。
 * @param buf 调用方栈缓冲（承载省略结果）。
 * @param bufCap buf 容量（字节）。
 * @param text 原文（UTF-8）。
 * @param mode 省略模式，**数值对齐 Qt::TextElideMode**：
 *             0=ElideLeft（头部…）1=ElideRight（尾部…）
 *             2=ElideMiddle（中段…）3=ElideNone（截断不加省略号）。
 *             本库不再维护第二套编号。
 * @param maxW 可用宽（px）。
 * @param font 字体。
 * @return 省略后的文本指针（入参原样返回或 buf）。
 * @note 声明必须留在本守卫块内（extern "C" + XSTYLE_ON + XGUI_ON）：放到
 *       include guard 之后会让 C++ 侧拿到 C++ 链接性而实现是 C 口径，
 *       且 XGUI_ON=0 时 XFont 未定义。
 */
const char* XCommonStyle_elideText(char* buf, int bufCap, const char* text,
                                    int mode, int maxW, const XFont* font);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCOMMONSTYLE_H */
