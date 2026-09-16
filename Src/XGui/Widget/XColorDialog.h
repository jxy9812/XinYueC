/******************************************************************************
 * @file       XColorDialog.h
 * @brief      XColorDialog 颜色对话框控件（对标 Qt 6.8 QColorDialog : QDialog）。
 * @details    继承 XDialog，提供颜色选择对话框的公共 API 面：
 *             - 静态便捷函数 getColor（无 GUI 对话框环境返回 initial）；
 *             - 实例属性：currentColor/selectedColor、ColorDialogOption
 *               选项位（ShowAlphaChannel=0x1/NoButtons=0x2/
 *               DontUseNativeDialog=0x4/NoEyeDropperButton=0x8，
 *               数值与 Qt 6.8.3 qcolordialog.h 一致）；
 *             - 信号：currentColorChanged(XColor)/colorSelected(XColor)。
 *             颜色以 XColor 值类型传递（XData/XColor）。
 * @note       模块总开关 XDIALOG_ON（XWIDGET_ON && XDIALOG_ON 有效）。
 * @note       无 GUI 对话框环境：getColor 不做模态执行，直接返回 initial；
 *             colorSelected 由应用在“接受”动作处手动触发（测试用）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCOLORDIALOG_H
#define XCOLORDIALOG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#include "XColor.h"
#if XDIALOG_ON
#include "XDialog.h"
#endif /* XDIALOG_ON */

#if XWIDGET_ON && XDIALOG_ON

/** @brief 颜色对话框选项位（对标 QColorDialog::ColorDialogOption，数值一致）。 */
typedef enum XColorDialogOption
{
    XColorDialog_ShowAlphaChannel    = 0x00000001, /**< 显示 Alpha 通道。 */
    XColorDialog_NoButtons           = 0x00000002, /**< 不显示按钮。 */
    XColorDialog_DontUseNativeDialog = 0x00000004, /**< 不使用原生对话框。 */
    XColorDialog_NoEyeDropperButton  = 0x00000008  /**< 不显示取色器按钮。 */
} XColorDialogOption;
typedef uint32_t XColorDialogOptions;

XCLASS_DEFINE_BEGING(XColorDialog)
XCLASS_DEFINE_EXTEND_END(XColorDialog, XDialog)

/**
 * @brief      XColorDialog 颜色对话框对象；m_base 必须是第一个成员。
 */
typedef struct XColorDialog
{
    XDialog m_base;               /**< 基类成员；必须是第一个。 */
    XColor m_currentColor;        /**< 当前颜色（默认白；对标 currentColor）。 */
    XColor m_selectedColor;       /**< 最近一次接受的颜色（对标 selectedColor）。 */
    XColorDialogOptions m_options;/**< 选项位集；默认 0。 */
    XColor m_customColors[16];    /**< 自定义颜色表（对标 customColor/customCount）。 */
    int m_customCount;            /**< 自定义颜色数量。 */
    XColor m_standardColors[48];  /**< 标准颜色表（对标 standardColor）。 */
    int m_standardCount;          /**< 标准颜色数量。 */
} XColorDialog;

/**
 * @brief      XColorDialog 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XColorDialog_class_init(void);

/**
 * @brief      初始化 XColorDialog（对标 QColorDialog 构造）。
 * @param      self 目标对象指针；不可为 NULL。
 * @param      initial 初始颜色。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XColorDialog_init(XColorDialog* self, XColor initial, XWidget* parent,
                       XWidgetFlags flags);
/**
 * @brief      以默认初始颜色（白）初始化 XColorDialog。
 * @param      self 目标对象指针；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @return     无返回值。
 */
void XColorDialog_init_default(XColorDialog* self, XWidget* parent);
#define XColorDialog_create(parent, flags) \
    XColorDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XColor_White, (parent), (flags))
/**
 * @brief      使用指定内存类型创建 XColorDialog。
 * @param      memory 对象内存类型。
 * @param      initial 初始颜色。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新对象指针；失败返回 NULL。
 */
XColorDialog* XColorDialog_create_ex(XMemoryType memory, XColor initial,
                                     XWidget* parent, XWidgetFlags flags);
#define XColorDialog_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XColorDialog_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 实例属性（对标 QColorDialog） ==================== */

/**
 * @brief      设置当前颜色（对标 QColorDialog::setCurrentColor）。
 * @details    颜色实际变化时发射 currentColorChanged(XColor)。
 * @param      self 目标对话框。
 * @param      color 新颜色。
 * @return     无返回值。
 */
void XColorDialog_setCurrentColor(XColorDialog* self, XColor color);
/**
 * @brief      获取当前颜色（对标 QColorDialog::currentColor）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前颜色；无效时返回无效 XColor。
 */
XColor XColorDialog_currentColor(const XColorDialog* self);
/**
 * @brief      获取最近一次接受的颜色（对标 QColorDialog::selectedColor）。
 * @param      self 目标对话框；可为 NULL。
 * @return     选中颜色；无效时返回无效 XColor。
 */
XColor XColorDialog_selectedColor(const XColorDialog* self);
/**
 * @brief      设置单个选项位（对标 QColorDialog::setOption）。
 * @param      self 目标对话框。
 * @param      option 选项位（XColorDialogOption）。
 * @param      on true=置位，false=清位。
 * @return     无返回值。
 */
void XColorDialog_setOption(XColorDialog* self, XColorDialogOption option, bool on);
/**
 * @brief      测试选项位（对标 QColorDialog::testOption）。
 * @param      self 目标对话框；可为 NULL。
 * @param      option 选项位（XColorDialogOption）。
 * @return     置位返回 true；无效返回 false。
 */
bool XColorDialog_testOption(const XColorDialog* self, XColorDialogOption option);
/**
 * @brief      设置整个选项位集（对标 QColorDialog::setOptions）。
 * @param      self 目标对话框。
 * @param      options 选项位组合。
 * @return     无返回值。
 */
void XColorDialog_setOptions(XColorDialog* self, XColorDialogOptions options);
/**
 * @brief      获取选项位集（对标 QColorDialog::options）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前选项位组合；无效返回 0。
 */
XColorDialogOptions XColorDialog_options(const XColorDialog* self);

/* ==================== 静态便捷函数（对标 QColorDialog） ==================== */

/**
 * @brief      弹出颜色选择并返回选中颜色（对标 QColorDialog::getColor）。
 * @note       无 GUI 对话框环境：不做模态执行，直接返回 initial。
 * @param      initial 初始颜色。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题；可为 NULL。
 * @param      options 选项位组合。
 * @return     选中的颜色；无 GUI 环境返回 initial。
 */
/** @brief 自定义颜色数量（对标 customCount；QColorDialog 内置 16 个自定义槽位）。 */
int XColorDialog_customCount(const XColorDialog* self);
/** @brief 指定自定义颜色（对标 setCustomColor）。 */
void XColorDialog_setCustomColor(XColorDialog* self, int index, XColor color);
/** @brief 查询自定义颜色（对标 customColor；越界返回黑色）。 */
XColor XColorDialog_customColor(const XColorDialog* self, int index);
/** @brief 指定标准颜色（对标 setStandardColor）。 */
void XColorDialog_setStandardColor(XColorDialog* self, int index, XColor color);
/** @brief 查询标准颜色（对标 standardColor；越界返回黑色）。 */
XColor XColorDialog_standardColor(const XColorDialog* self, int index);
/** @brief 以非模态方式打开对话框（对标 QDialog::open；show + 置模态）。
 * @note 无模态事件循环，仅显示并记录。 */
void XColorDialog_open(XColorDialog* self);
XColor XColorDialog_getColor(XColor initial, XWidget* parent,
                             const XString* title, XColorDialogOptions options);
/**
 * @brief      弹出颜色选择并返回选中颜色（UTF-8 重载）。
 * @note       无 GUI 对话框环境：不做模态执行，直接返回 initial。
 * @param      initial 初始颜色。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题（UTF-8）；可为 NULL。
 * @param      options 选项位组合。
 * @return     选中的颜色；无 GUI 环境返回 initial。
 */
XColor XColorDialog_getColor_2(XColor initial, XWidget* parent,
                               const char* title, XColorDialogOptions options);

/* ==================== 信号（对标 QColorDialog） ==================== */

/**
 * @brief      当前颜色变化信号（对标 QColorDialog::currentColorChanged；
 *             setCurrentColor 与手动触发时发射）。
 * @param      self 目标对话框。
 * @param      color 新颜色。
 * @return     信号标识。
 */
void* XColorDialog_currentColorChanged_signal(XColorDialog* self, XColor color);
/**
 * @brief      颜色选中信号（对标 QColorDialog::colorSelected；接受动作处
 *             触发，测试可手动调用）。
 * @param      self 目标对话框。
 * @param      color 选中的颜色。
 * @return     信号标识。
 */
void* XColorDialog_colorSelected_signal(XColorDialog* self, XColor color);

#endif /* XWIDGET_ON && XDIALOG_ON */

#endif /* XCOLORDIALOG_H */
