/**
 * @file       XFontComboBox.h
 * @brief      XFontComboBox 字体族选择下拉框（对标 Qt 6.8 QFontComboBox
 *             核心公共 API）。
 * @details    继承 XComboBox；构造时经 XPlatformFontDatabase 字体族
 *             列表填充条目；fontFilters（FontFilter 枚举位标志，数值
 *             对齐）第一版仅 AllFonts 生效（其余过滤待字体元数据
 *             补齐）；currentFont 返回当前字体族名对应的 XFont。
 *             信号 currentFontChanged(XFont*)。
 * @note       模块总开关 XFONTCOMBOBOX_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XFONTCOMBOBOX_H
#define XFONTCOMBOBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XComboBox.h"

#if XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON

/** @brief 字体过滤器（对标 QFontComboBox::FontFilter，数值一致）。 */
typedef enum XFontComboBoxFilter
{
    XFontComboBoxFilter_AllFonts = 0x00,
    XFontComboBoxFilter_ScalableFonts = 0x01,
    XFontComboBoxFilter_NonScalableFonts = 0x02,
    XFontComboBoxFilter_MonospacedFonts = 0x04,
    XFontComboBoxFilter_ProportionalFonts = 0x08
} XFontComboBoxFilter;

XCLASS_DEFINE_BEGING(XFontComboBox)
XCLASS_DEFINE_EXTEND_END(XFontComboBox, XComboBox)

typedef struct XFontComboBox
{
    XComboBox m_base;     /**< 基类成员；必须是第一个。 */
    int m_filters;        /**< 字体过滤位标志（默认 AllFonts）。 */
} XFontComboBox;

XVtable* XFontComboBox_class_init(void);
void XFontComboBox_init(XFontComboBox* self, XWidget* parent,
                        XWidgetFlags flags);
#define XFontComboBox_create(parent, flags) XFontComboBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XFontComboBox* XFontComboBox_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags);
#define XFontComboBox_deinit_base(self) XComboBox_deinit_base((XComboBox*)(self))
#define XFontComboBox_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      设置字体过滤器。
 */
void XFontComboBox_setFontFilters(XFontComboBox* self, int filters);
/**
 * @brief      获取字体过滤器。
 */
int XFontComboBox_fontFilters(const XFontComboBox* self);
/** @brief 查询当前字体族名（取自当前条目文本；对标 currentFont().family()）。 */
/**
 * @brief      获取当前字体族。
 */
const char* XFontComboBox_currentFamily(const XFontComboBox* self);
/** @brief 按族名选中条目（对标 setCurrentFont(QFont(family))）。 */
/**
 * @brief      按族名选中条目。
 */
void XFontComboBox_setCurrentFamily(XFontComboBox* self, const char* family);

#endif /* XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */

#ifdef __cplusplus
}
#endif

/* ==================== 信号 ==================== */

void* XFontComboBox_currentFontChanged_signal(XFontComboBox* self);
#endif /* XFONTCOMBOBOX_H */