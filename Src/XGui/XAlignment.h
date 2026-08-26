/******************************************************************************
 * @file       XAlignment.h
 * @brief      GUI/控件/布局共用对齐标志（对标 Qt 6.8 Qt::Alignment）。
 * @details    XAlignment 是控件与布局共享的位标志类型，水平/垂直两组位
 *             可以按位或组合；Leading/Trailing 是 Left/Right 的语义别名，
 *             按当前控件布局方向解释（XGui 当前默认 LeftToRight）。
 *             本头文件不依赖布局/控件模块，可在 XWIDGET_ON/XLAYOUT_ON
 *             关闭时独立使用。
 * @note       模块总开关：无独立开关，随 XGUI 公共头文件提供。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XALIGNMENT_H
#define XALIGNMENT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief      对齐标志（对标 Qt 6.8 Qt::Alignment，数值一致）。
 * @details    HorizontalMask/VerticalMask 用于提取对应轴的对齐位；
 *             Center 为 HCenter|VCenter 的快捷组合。
 */
typedef enum XAlignment
{
    XAlignment_Left          = 0x0001, /**< 水平左对齐（对标 Qt::AlignLeft）。 */
    XAlignment_Leading       = 0x0001, /**< 水平起始对齐（同 Left；对标 AlignLeading）。 */
    XAlignment_Right         = 0x0002, /**< 水平右对齐（对标 Qt::AlignRight）。 */
    XAlignment_Trailing      = 0x0002, /**< 水平末尾对齐（同 Right；对标 AlignTrailing）。 */
    XAlignment_HCenter       = 0x0004, /**< 水平居中（对标 Qt::AlignHCenter）。 */
    XAlignment_Justify       = 0x0008, /**< 水平两端对齐（对标 Qt::AlignJustify）。 */
    XAlignment_Absolute      = 0x0010, /**< 忽略布局方向强制左右语义（对标 AlignAbsolute）。 */
    XAlignment_HorizontalMask = 0x000f,/**< 水平对齐掩码（Left|Right|HCenter|Justify）。 */
    XAlignment_Top           = 0x0020, /**< 垂直顶部对齐（对标 Qt::AlignTop）。 */
    XAlignment_Bottom        = 0x0040, /**< 垂直底部对齐（对标 Qt::AlignBottom）。 */
    XAlignment_VCenter       = 0x0080, /**< 垂直居中（对标 Qt::AlignVCenter）。 */
    XAlignment_Baseline      = 0x0100, /**< 按基线对齐（对标 AlignBaseline）。 */
    XAlignment_VerticalMask  = 0x01e0, /**< 垂直对齐掩码（Top|Bottom|VCenter|Baseline）。 */
    XAlignment_Center        = 0x0084  /**< 水平+垂直居中（HCenter|VCenter）。 */
} XAlignment;
/** @brief 对齐标志组合类型（可为多个 XAlignment 位标志按位或）。 */
typedef uint32_t XAlignments;

#ifdef __cplusplus
}
#endif
#endif /* XALIGNMENT_H */
