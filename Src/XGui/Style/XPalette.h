/******************************************************************************
 * @file       XPalette.h
 * @brief      XPalette 调色板值类型（对标 Qt 6.8 QPalette）。
 * @details    XPalette 是纯 C 值类型（无虚函数表、无堆资源），按
 *             「颜色组 × 颜色角色」保存 4×21 个颜色单元，对标 QPalette
 *             的 ColorGroup × ColorRole 矩阵。QPalette 的笔刷(Brush)本
 *             模块用「纯色笔刷」等价：每个角色仅存一个 XColor，不存在
 *             渐变/图片笔刷，嵌入式友好。默认构造使用 Qt 6.8 Fusion
 *             浅色主题的纯色角色配色，供 XGuiApplication 使用；上层可自由修改
 *             任一颜色单元后整体传回 setPalette。XPalette_init_dark 提供
 *             Qt 6.5+ StandardPalette 深色语义的内置深色组（对标
 *             qt_fusionPalette 深色分支），供 XGuiApplication 的
 *             theme×调色板联动在深色 scheme 时整体切换。
 * @note       模块开关 XPALETTE_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XPalette 公共 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPALETTE_H
#define XPALETTE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XColor.h"
#if XPALETTE_ON
#include "XBrush.h"

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
    XPaletteColorRole_Accent,              /**< 强调色（对标 QPalette::Accent，Qt 6.8）。 */
    XPaletteColorRole_NColorRoles          /**< 颜色角色总数（内部使用）。 */
} XPaletteColorRole;

/**
 * @brief      XPalette 调色板值类型。
 * @details    m_colors 为 [组][角色] 二维画刷矩阵（XBrush 8B 只读句柄），
 *             与 Qt 6.8 QPalettePrivate::Data 的 QBrush 数组同构：调色板
 *             持笔刷而非纯色，纯色只是 SolidPattern 的特例。句柄指向
 *             XBrush 常驻数据块，因此本类型仍无堆资源、可直接按值拷贝，
 *             不需要 *_delete 释放。
 *             颜色读写仍走 XPalette_color/setColor：访问按 RGB 归一后的
 *             纯色（语义=「最终显示色」，丢弃原色彩空间标签）。
 *             笔刷读写走 XPalette_brush/setBrush/isBrushSet（对标
 *             QPalette 同名 API）。m_colors 勿直访。
 */
typedef struct XPalette
{
    XBrush m_colors[XPaletteColorGroup_NColorGroups][XPaletteColorRole_NColorRoles];
    uint64_t m_resolveMask; /**< 每角色一位（对标 QPalette::ResolveMask）。 */
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
 * @brief      创建深色标准调色板（对标 Qt 6.5+ StandardPalette 深色语义）。
 * @details    按 Qt 6.8 qt_fusionPalette() 的深色分支（darkAppearance）逐
 *             角色计算：Window/Button 为 #323232、Base 为 #232323、文本为
 *             #f0f0f0、高亮为 #308cc6，深色下 Link 复用高亮色、Accent 镜像
 *             高亮色；衍生灰阶用 QColor::lighter/darker 的 HSV V 通道整数
 *             折算（见实现注释，灰色通道经 HSV 往返不变）。工具提示/交替行
 *             等融合层未覆盖的角色沿用 QPalette 构造默认。供
 *             XGuiApplication 的 theme×调色板联动使用。
 * @return     已填充深色配色的 XPalette 值。
 */
XPalette XPalette_create_dark(void);

/**
 * @brief      与 XPalette_create_dark 等价，按深色主题初始化调色板。
 * @param      self 目标调色板指针；可为 NULL。
 */
void XPalette_init_dark(XPalette* self);

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
 * @brief      查询指定角色的画刷（对标 QPalette::brush）。
 * @details    返回 8B 句柄副本（无所有权）；NoRole/越界返回 NoBrush。
 * @param      self 目标调色板；NULL 返回 NoBrush。
 * @param      group 颜色组。
 * @param      role 颜色角色。
 * @return     画刷值；未设置或参数非法为 NoBrush。
 */
XBrush XPalette_brush(const XPalette* self, XPaletteColorGroup group, XPaletteColorRole role);

/**
 * @brief      设置指定角色的画刷（对标 QPalette::setBrush）。
 * @details    整笔刷替换（样式/颜色/渐变/纹理/变换）；Current 组映射到
 *             Active，NoRole 忽略。
 * @param      self 目标调色板；可为 NULL（无操作）。
 * @param      group 颜色组。
 * @param      role 颜色角色。
 * @param      brush 新画刷；可为 NULL（等价 NoBrush）。
 */
void XPalette_setBrush(XPalette* self, XPaletteColorGroup group, XPaletteColorRole role, const XBrush* brush);

/**
 * @brief      查询角色是否显式设置了画刷（对标 QPalette::isBrushSet）。
 * @details    以 resolveMask 对应位判定：setColor/setBrush 置位，init 系
 *             初始化清零；NoRole/越界不参与判定。
 * @param      self 目标调色板；可为 NULL。
 * @param      group 颜色组。
 * @param      role 颜色角色。
 * @return     已设置返回 true；NoRole/越界/NULL 返回 false。
 */
bool XPalette_isBrushSet(const XPalette* self, XPaletteColorGroup group, XPaletteColorRole role);

/**
 * @brief      交换两个调色板（对标 QPalette::swap）。
 * @details    逐字节交换画刷矩阵与 resolveMask；句柄指向驻留池，交换后
 *             两侧数据仍有效。
 * @param      a 调色板 A；可为 NULL（无操作）。
 * @param      b 调色板 B；可为 NULL（无操作）。
 * @return     无返回值。
 */
void XPalette_swap(XPalette* a, XPalette* b);
/**
 * @brief      计算调色板内容哈希（对标 QPalette::cacheKey）。
 * @details    FNV-1a 逐格混入颜色 ARGB 与 resolveMask；内容相同则键相同，
 *             可直接用作缓存索引。
 * @param      self 目标调色板；可为 NULL。
 * @return     64 位哈希；NULL 返回 0。
 */
uint64_t XPalette_cacheKey(const XPalette* self);
/**
 * @brief      判断是否与另一调色板内容相同（对标 isCopyOf）。
 * @details    本库调色板是值类型（无共享数据指针），「同源拷贝」等价于
 *             逐格内容全等，与 isEqual 同口径。
 * @param      a 调色板 A；可为 NULL。
 * @param      b 调色板 B；可为 NULL。
 * @return     内容全等返回 true。
 */
bool XPalette_isCopyOf(const XPalette* a, const XPalette* b);
/**
 * @brief      合并另一个调色板已解析的角色（对标 QPalette::resolve）。
 * @details    返回新值：复制自身全量后，仅用 other 中 resolveMask 置位的
 *             角色覆盖（覆盖全部分组），并同步置位这些角色；未置位角色
 *             保持自身值。
 * @param      self 基础调色板；NULL 时以默认调色板为底。
 * @param      other 覆盖源；NULL 时原样返回基础调色板。
 * @return     合并后的调色板值。
 */
XPalette XPalette_resolve(const XPalette* self, const XPalette* other);
/**
 * @brief      查询解析位掩码（对标 resolveMask）。
 * @details    每角色一位（低位对应 XPaletteColorRole 枚举序号）。
 * @param      self 目标调色板；可为 NULL。
 * @return     64 位掩码；NULL 返回 0。
 */
uint64_t XPalette_resolveMask(const XPalette* self);
/**
 * @brief      设置解析位掩码（对标 setResolveMask）。
 * @param      self 目标调色板；可为 NULL（无操作）。
 * @param      mask 新掩码；每角色一位。
 * @return     无返回值。
 */
void XPalette_setResolveMask(XPalette* self, uint64_t mask);

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

#endif /* XGUI_ON */

#endif /* XPALETTE_H */
