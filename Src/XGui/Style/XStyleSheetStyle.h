#ifndef XSTYLESHEETSTYLE_H
#define XSTYLESHEETSTYLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWindowsStyle.h"
#include "XCssStyleSheet.h"

#if XSTYLE_ON

XCLASS_DEFINE_BEGING(XStyleSheetStyle)
XCLASS_DEFINE_EXTEND_END(XStyleSheetStyle, XWindowsStyle)

/**
 * @brief 样式表风格（对标 Qt 6.8 QStyleSheetStyle : QWindowsStyle）。
 *
 *        持有解析后的 XCssStyleSheet 规则表；绘制时按控件类名
 *        （对标 elementName）、objectName（对标 #id）与伪类状态
 *        （:hover/:pressed/:focus/:disabled/:checked/:selected）匹配规则，
 *        命中声明覆盖颜色/边框并回落底层 XWindowsStyle/XFusionStyle。
 *        未命中或未设置样式表时行为与底层样式完全一致。
 */
typedef struct XStyleSheetStyle
{
    XWindowsStyle m_base;      /**< 基类成员；必须是第一个。 */
    XCssStyleSheet m_sheet;    /**< 解析后的规则表（对象拥有）。 */
    XStyle* m_source;          /**< 底层样式（NULL=用全局默认）。 */
    bool m_sourceOwned;        /**< m_source 是否由本对象拥有（析构时释放）。 */
    /* ---- 渲染规则缓存（对标 QStyleSheetStylePrivate::renderRules 的
     *      单槽近似：按 (对象指针,状态) 缓存最高特异度命中规则，同特异度
     *      取后出现者；仅当全表无任何 !important 时命中才直返缓存规则
     *      声明，否则回落全表扫描按 !important > 特异度 > 后定义裁决） ---- */
    const XObject* m_cacheObj;      /**< 缓存对象指针。 */
    uint32_t m_cacheState;          /**< 缓存状态位。 */
    const XCssStyleRule* m_cacheRule; /**< 缓存命中规则。 */
    const XCssSelector* m_cacheSel;  /**< 缓存命中选择器。 */
    int m_cacheSpec;                /**< 缓存特异度。 */
    bool m_cacheValid;              /**< 缓存是否有效。 */
} XStyleSheetStyle;

XVtable* XStyleSheetStyle_class_init(void);

/**
 * @brief 初始化嵌入式样式表风格。
 *
 * @param self 目标样式指针，不能为空。
 * @return 无返回值。
 */
void XStyleSheetStyle_init(XStyleSheetStyle* self);

/**
 * @brief 堆上创建样式表风格。
 *
 * @param memory 内存类型。
 * @return 样式指针；分配失败返回 NULL。
 */
XStyleSheetStyle* XStyleSheetStyle_create_ex(XMemoryType memory);
#define XStyleSheetStyle_create() \
    XStyleSheetStyle_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XStyleSheetStyle_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上样式（查表分派析构并释放内存）。 */
#define XStyleSheetStyle_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/**
 * @brief 设置样式表文本（对标 QStyleSheetStyle::setStyleSheet）。
 *
 * @param self 目标样式指针。
 * @param css UTF-8 CSS 文本（NULL/空清空规则）。
 * @return 解析成功返回 true。
 */
bool XStyleSheetStyle_setStyleSheet(XStyleSheetStyle* self, const char* css);

/**
 * @brief 设置底层样式（借用；NULL=回落全局默认样式）。
 *
 * @param self 目标样式指针。
 * @param source 底层样式指针（借用；本对象不取得所有权）。
 * @return 无返回值。
 */
void XStyleSheetStyle_setSourceStyle(XStyleSheetStyle* self, XStyle* source);
/** @brief 设置底层样式并转移所有权（析构时释放 source；用于 installStyleSheet 接管默认样式）。
 * @param self 目标样式指针。
 * @param source 底层样式指针（所有权转移给 self；可为 NULL）。
 * @return 无返回值。
 */
void XStyleSheetStyle_setSourceStyle_move(XStyleSheetStyle* self, XStyle* source);

/**
 * @brief 查询当前规则数（测试/诊断）。
 *
 * @param self 目标样式指针。
 * @return 规则数。
 */
int XStyleSheetStyle_ruleCount(const XStyleSheetStyle* self);

/**
 * @brief 解析颜色值（#RGB/#RRGGBB/#AARRGGBB/rgb(...) 子集）。
 *
 * @param value CSS 颜色值文本。
 * @param out 输出 ARGB。
 * @return 解析成功返回 true。
 */
bool XCssParseColor(const char* value, uint32_t* out);

/**
 * @brief 限长颜色 token 解析（供 border 简写拆分）。
 *
 * @param value 颜色 token 起始指针。
 * @param len token 长度。
 * @param out 输出 ARGB。
 * @return 解析成功返回 true。
 */
bool XCssParseColorToken(const char* value, size_t len, uint32_t* out);

/**
 * @brief 解析长度值（"12px"/"12" → 12）。
 *
 * @param value CSS 长度文本。
 * @param out 输出像素值。
 * @return 解析成功返回 true。
 */
bool XCssParseLength(const char* value, int* out);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif
#endif /* XSTYLESHEETSTYLE_H */
