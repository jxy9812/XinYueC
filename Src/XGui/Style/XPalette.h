/******************************************************************************
 * @file       XPalette.h
 * @brief      XPalette 调色板值类型（对标 Qt 6.8 QPalette）。
 * @details    XPalette 是纯 C 值类型（无虚函数表、无堆资源），按
 *             「颜色组 × 颜色角色」保存 4×21 个颜色单元，对标 QPalette
 *             的 ColorGroup × ColorRole 矩阵。QPalette 的笔刷(Brush)本
 *             模块用「纯色笔刷」等价：每个角色仅存一个 XColor，不存在
 *             渐变/图片笔刷，嵌入式友好。默认构造使用 Qt 6.8 Fusion
 *             浅色主题的纯色角色配色，供 XGuiApplication 使用；上层可自由修改
 *             任一颜色单元后整体传回 setPalette。
 * @note       模块开关 XPALETTE_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XPalette 公共 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPALETTE_H
#define XPALETTE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XColor.h"
#if XPALETTE_ON

/** @brief 颜色组（对标 Qt 6.8 QPalette::ColorGroup）。 */
typedef enum XPaletteColorGroup
{
    XPaletteColorGroup_Active = 0,   /**< 活动组：有键盘焦点的窗口使用。 */
    XPaletteColorGroup_Disabled,     /**< 禁用组：禁用控件使用。 */
    XPaletteColorGroup_Inactive,     /**< 非活动组：无键盘焦点的窗口使用。 */
    XPaletteColorGroup_Current,      /**< 当前组：写入时映射到活动组，读取时等价活动组。 */
    XPaletteColorGroup_NColorGroups  /**< 颜色组总数（内部使用）。 */
} XPaletteColorGroup;

/** @brief 颜色角色（对标 Qt 6.8 QPalette::ColorRole，取值与排序一致）。 */
typedef enum XPaletteColorRole
{
    XPaletteColorRole_WindowText = 0,      /**< 窗口前景文本色。 */
    XPaletteColorRole_Button,              /**< 按钮底色。 */
    XPaletteColorRole_Light,               /**< 比 Button 更亮的颜色。 */
    XPaletteColorRole_Midlight,            /**< 介于 Button 与 Light 之间。 */
    XPaletteColorRole_Dark,                /**< 比 Button 更暗的颜色。 */
    XPaletteColorRole_Mid,                 /**< 介于 Button 与 Dark 之间。 */
    XPaletteColorRole_Text,                /**< 文本前景色（Base 之上）。 */
    XPaletteColorRole_BrightText,          /**< 明亮前景色（如 Dark 之上）。 */
    XPaletteColorRole_ButtonText,          /**< 按钮前景文本色。 */
    XPaletteColorRole_Base,                /**< 输入类控件的底色。 */
    XPaletteColorRole_Window,              /**< 窗口底色。 */
    XPaletteColorRole_Shadow,              /**< 阴影色。 */
    XPaletteColorRole_Highlight,           /**< 选中项背景色。 */
    XPaletteColorRole_HighlightedText,     /**< 选中项前景文本色。 */
    XPaletteColorRole_Link,                /**< 超链接色。 */
    XPaletteColorRole_LinkVisited,         /**< 已访问超链接色。 */
    XPaletteColorRole_AlternateBase,       /**< 交替行底色（表/列表）。 */
    XPaletteColorRole_NoRole,              /**< 无角色（占位，读写均不落盘）。 */
    XPaletteColorRole_ToolTipBase,         /**< 工具提示底色。 */
    XPaletteColorRole_ToolTipText,         /**< 工具提示文本色。 */
    XPaletteColorRole_PlaceholderText,     /**< 输入占位符文本色。 */
    XPaletteColorRole_NColorRoles          /**< 颜色角色总数（内部使用）。 */
} XPaletteColorRole;

/**
 * @brief      XPalette 调色板值类型。
 * @details    m_colors 为 [组][角色] 二维数组；XPaletteColorGroup_Current
 *             槽位仅为对齐 Qt 而保留，读写统一映射到活动组。所有颜色初值
 *             由 XPalette_init_default 填充；本类型无堆资源，可直接按值
 *             拷贝，不需要 *_delete 释放。
 */
typedef struct XPalette
{
    XColor m_colors[XPaletteColorGroup_NColorGroups][XPaletteColorRole_NColorRoles];
} XPalette;

/**
 * @brief      创建默认浅色主题调色板（对标 QPalette 默认构造）。
 * @details    配色按 Qt 6.8 qt_fusionPalette() 的浅色分支计算：Window/Button
 *             为 #efefef、Base 为白、禁用文本为 #bebebe、高亮为 #308cc6，
 *             PlaceholderText 使用文本色 50% 透明度；Current 槽位与 Active
 *             内容一致。纯色 XColor 是 QBrush 纯色的嵌入式等价表示。
 * @return     已填充默认配色的 XPalette 值。
 */
XPalette XPalette_create(void);

/**
 * @brief      与 XPalette_create 等价，按浅色主题初始化调色板。
 * @param      self 目标调色板指针；可为 NULL。
 */
void XPalette_init_default(XPalette* self);

/**
 * @brief      按值拷贝调色板（薄封装，等价结构体赋值）。
 * @param      dest 目标调色板指针；不可与 src 重叠。
 * @param      src  源调色板指针。
 */
void XPalette_copy(XPalette* dest, const XPalette* src);

/**
 * @brief      读取指定组/角色的颜色（对标 QPalette::color）。
 * @details    Current 组等价读取 Active 组；NoRole 返回无效颜色。
 * @param      self  目标调色板指针；可为 NULL。
 * @param      group 颜色组（自动归一化处理 Current）。
 * @param      role  颜色角色。
 * @return     颜色值；参数无效时返回 XColor_create() 的无效颜色。
 */
XColor XPalette_color(const XPalette* self, XPaletteColorGroup group, XPaletteColorRole role);

/**
 * @brief      设置指定组/角色的颜色（对标 QPalette::setColor）。
 * @details    Current 组统一写入 Active 组（Qt 语义上 Current 是活动/非
 *             活动中的“当前”组，这里简化映射到 Active，文档明示）；
 *             NoRole 忽略。角色越界时越界行为为 no-op。
 * @param      self  目标调色板指针；可为 NULL。
 * @param      group 颜色组。
 * @param      role  颜色角色。
 * @param      color 新颜色值。
 */
void XPalette_setColor(XPalette* self, XPaletteColorGroup group, XPaletteColorRole role, XColor color);

/**
 * @brief      比较两个调色板是否逐单元相等（对标 QPalette::operator==）。
 * @param      a 调色板 A；可为 NULL。
 * @param      b 调色板 B；可为 NULL。
 * @return     a==b 返回 true；全 NULL 视为相等。
 */
bool XPalette_isEqual(const XPalette* a, const XPalette* b);

#else /* !XPALETTE_ON */
/** @brief 裁剪回退值类型：仅保留兼容布局所需的占位字段。 */
typedef struct XPalette
{
    int m_disabled;
} XPalette;
#endif /* XPALETTE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPALETTE_H */
