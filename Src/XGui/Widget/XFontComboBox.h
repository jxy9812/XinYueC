/**
 * @file       XFontComboBox.h
 * @brief      XFontComboBox 字体族选择下拉框（对标 Qt 6.8 QFontComboBox
 *             核心公共 API）。
 * @details    继承 XComboBox；构造时经 XPlatformFontDatabase 字体族
 *             列表填充条目；fontFilters（FontFilter 枚举位标志，数值
 *             对齐）第一版仅 AllFonts 生效（其余过滤待字体元数据
 *             补齐）；currentFont 返回当前字体族名对应的 XFont；
 *             writingSystem 为简化子集枚举（数值对齐 Qt::WritingSystem，
 *             当前仅状态承载，家族过滤未接）。
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

/** @brief 书写系统（对标 QFontDatabase::WritingSystem 的简化子集；
 *         各枚举值与 Qt::WritingSystem 对应项数值一致）。 */
typedef enum XFontComboBoxWritingSystem
{
    XFontComboBoxWritingSystem_Any = 0,                 /**< 任意（Qt Any=0；不过滤，默认）。 */
    XFontComboBoxWritingSystem_Latin = 1,               /**< 拉丁文（Qt Latin=1）。 */
    XFontComboBoxWritingSystem_Greek = 2,               /**< 希腊文（Qt Greek=2）。 */
    XFontComboBoxWritingSystem_Cyrillic = 3,            /**< 西里尔文（Qt Cyrillic=3）。 */
    XFontComboBoxWritingSystem_Armenian = 4,            /**< 亚美尼亚文（Qt Armenian=4）。 */
    XFontComboBoxWritingSystem_Hebrew = 5,              /**< 希伯来文（Qt Hebrew=5）。 */
    XFontComboBoxWritingSystem_Arabic = 6,              /**< 阿拉伯文（Qt Arabic=6）。 */
    XFontComboBoxWritingSystem_Devanagari = 9,          /**< 天城文（Qt Devanagari=9）。 */
    XFontComboBoxWritingSystem_Bengali = 10,            /**< 孟加拉文（Qt Bengali=10）。 */
    XFontComboBoxWritingSystem_Tamil = 14,              /**< 泰米尔文（Qt Tamil=14）。 */
    XFontComboBoxWritingSystem_Thai = 19,               /**< 泰文（Qt Thai=19）。 */
    XFontComboBoxWritingSystem_SimplifiedChinese = 25,  /**< 简体中文（Qt SimplifiedChinese=25）。 */
    XFontComboBoxWritingSystem_TraditionalChinese = 26, /**< 繁体中文（Qt TraditionalChinese=26）。 */
    XFontComboBoxWritingSystem_Japanese = 27,           /**< 日文（Qt Japanese=27）。 */
    XFontComboBoxWritingSystem_Korean = 28,             /**< 韩文（Qt Korean=28）。 */
    XFontComboBoxWritingSystem_Vietnamese = 29,         /**< 越南文（Qt Vietnamese=29）。 */
    XFontComboBoxWritingSystem_Symbol = 30              /**< 符号（Qt Symbol=30）。 */
} XFontComboBoxWritingSystem;

XCLASS_DEFINE_BEGING(XFontComboBox)
XCLASS_DEFINE_EXTEND_END(XFontComboBox, XComboBox)

typedef struct XFontComboBox
{
    XComboBox m_base;     /**< 基类成员；必须是第一个。 */
    int m_filters;        /**< 字体过滤位标志（默认 AllFonts）。 */
    int m_writingSystem;  /**< 书写系统（默认 Any；当前仅状态承载）。 */
    XFont m_displayFont;  /**< 显示字体状态（值承载；默认构造；setDisplayFont 写入）。 */
    void* m_fontSamples;  /**< 族名样例文本表（内部条目数组；按需扩容；NULL=未分配）。 */
    int m_fontSampleCount;/**< 族名样例表已用条数（0=空）。 */
    int m_fontSampleCap;  /**< 族名样例表已分配容量（0=未分配）。 */
    void* m_systemSamples;/**< 书写系统样例文本表（内部条目数组；按需扩容；NULL=未分配）。 */
    int m_systemSampleCount; /**< 书写系统样例表已用条数（0=空）。 */
    int m_systemSampleCap;   /**< 书写系统样例表已分配容量（0=未分配）。 */
} XFontComboBox;

/** @brief X字体Combo盒classinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
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
/** @brief 设置书写系统（对标 QFontComboBox::setWritingSystem）。
 * @details 当前版本仅保存状态供 writingSystem 查询，字体族列表不按
 *          书写系统过滤（依赖字体元数据，待字体数据库能力补齐）；
 *          取值须为 XFontComboBoxWritingSystem 子集成员，非法值忽略。
 */
/**
 * @brief      设置书写系统。
 */
void XFontComboBox_setWritingSystem(XFontComboBox* self, int writingSystem);
/**
 * @brief      获取书写系统。
 */
int XFontComboBox_writingSystem(const XFontComboBox* self);
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

/** @brief 查询当前字体（对标 QFontComboBox::currentFont；宏别名复用
 *         XFontComboBox_currentFamily，以 UTF-8 族名承载 QFont）。
 * @param self 目标控件；可为 NULL。
 * @return 当前字体族名（UTF-8）；借用内部缓存，禁止释放。
 */
#define XFontComboBox_currentFont(self) XFontComboBox_currentFamily((self))
/** @brief 按字体族名选中条目（对标 QFontComboBox::setCurrentFont；
 *         以 UTF-8 族名承载 QFont；转发 setCurrentFamily）。 */
void XFontComboBox_setCurrentFont(XFontComboBox* self, const char* family);

/* ==================== 显示字体与样例文本（状态承载） ==================== */

/**
 * @brief 设置显示字体状态（值承载；深拷贝 font 到内部状态）。
 *
 *        与 setCurrentFont（按族名选中条目并发射 currentFontChanged）
 *        互不影响：本接口只写字体状态，不改当前条目、不发信号。
 *
 * @param self 目标控件。
 * @param font 字体源指针（借用；只读；NULL 忽略）。
 * @return 无返回值。
 *
 * @note 状态承载：字体状态尚无消费者（字形预览/委托绘制未建），
 *       仅保存供后续批次读取；currentFont 已按项目惯例以族名宏别名
 *       承载（见 XFontComboBox_currentFont），不另设 XFont 值查询。
 */
void XFontComboBox_setDisplayFont(XFontComboBox* self, const XFont* font);
/**
 * @brief 查询显示字体状态（XFont 值拷贝返回；对标
 *        QFontComboBox::displayFont）。
 *
 *        返回 setDisplayFont 写入的显示字体深拷贝（含家族/样式字符串
 *        的独立副本）；从未设置时返回默认构造字体。
 *
 * @param self 目标控件；NULL 返回默认构造字体。
 * @return XFont 值拷贝；调用方负责以
 *         XClass_deinit_base((XClass*)&font)（宏 XFont_deinit_base）
 *         释放内部字符串后复用或弃置。
 *
 * @note 简化项：Qt 按族名查询每族覆盖字体并返回 std::optional（无
 *       覆盖为空）；本项目 setDisplayFont 为单值状态承载（无族名键、
 *       不可查空），故本查询不带族名参数，"未设置"以默认构造字体
 *       承载。currentFont 宏别名（族名 UTF-8 承载）不覆盖本 XFont 值
 *       查询，二者互补。
 */
XFont XFontComboBox_displayFont(const XFontComboBox* self);
/**
 * @brief 查询字体族样例文本（新建 XString 返回；调用方持有并负责
 *        XString_delete_base 释放）。
 *
 *        已用 setSampleTextForFont 自定义该族样例时返回自定义文本；
 *        否则回退返回家族名本身（同 Qt 无采样时的可用近似）。
 *
 * @param self 目标控件。
 * @param family 字体族名（UTF-8；NULL 视为非法返回 NULL）。
 * @return 新建样例文本 XString；self 为空或参数非法返回 NULL。
 *
 * @note 字体采样未建：默认样例文本（按书写系统挑字形）依赖字体
 *       元数据，当前以家族名回退。
 */
XString* XFontComboBox_sampleTextForFont(const XFontComboBox* self,
                                         const char* family);
/**
 * @brief 查询书写系统样例文本（新建 XString 返回；调用方持有并负责
 *        XString_delete_base 释放）。
 *
 *        已用 setSampleTextForSystem 自定义该系统样例时返回自定义
 *        文本；否则回退返回当前字体族名（同 sampleTextForFont 的
 *        未采样回退策略）。
 *
 * @param self 目标控件。
 * @param system 书写系统（XFontComboBoxWritingSystem 成员；非法值
 *               返回 NULL）。
 * @return 新建样例文本 XString；self 为空或参数非法返回 NULL。
 *
 * @note 字体采样未建：各书写系统默认采样文本未内置，当前以当前
 *       族名回退。
 */
XString* XFontComboBox_sampleTextForSystem(const XFontComboBox* self,
                                           int system);
/**
 * @brief 设置字体族样例文本（状态承载）。
 *
 * @param self 目标控件。
 * @param family 字体族名（UTF-8；NULL/空串忽略）。
 * @param sample 样例文本（UTF-8；内部深拷贝；NULL=移除该族自定义，
 *               回退默认行为）。
 * @return 无返回值。
 */
void XFontComboBox_setSampleTextForFont(XFontComboBox* self,
                                        const char* family,
                                        const char* sample);
/**
 * @brief 设置书写系统样例文本（状态承载）。
 *
 * @param self 目标控件。
 * @param system 书写系统（XFontComboBoxWritingSystem 成员；非法值忽略）。
 * @param sample 样例文本（UTF-8；内部深拷贝；NULL=移除该系统自定义）。
 * @return 无返回值。
 */
void XFontComboBox_setSampleTextForSystem(XFontComboBox* self, int system,
                                          const char* sample);

/** @brief currentFontChanged(const char*) 信号（对标 QFontComboBox::currentFontChanged；
 *         载荷：字体家族 UTF-8）。 */
void* XFontComboBox_currentFontChanged_signal(XFontComboBox* self,
                                              const char* family);

#endif /* XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */
/* ==================== 信号 ==================== */
#endif /* XFONTCOMBOBOX_H */