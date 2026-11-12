/**
 * @file       XFontComboBox.h
 * @brief      XFontComboBox 字体族选择下拉框（对标 Qt 6.8 QFontComboBox
 *             核心公共 API）。
 * @details    继承 XComboBox；构造时经 XPlatformFontDatabase 字体族
 *             列表填充条目；fontFilters（FontFilter 枚举位标志，数值
 *             对齐）按 FontFilter 位标志**重填字体族列表**（对标
 *             QFontComboBox::setFontFilters → updateModel）；
 *             writingSystem 为简化子集枚举（数值对齐 Qt::WritingSystem，
 *             按族标注的书写系统数据缺失，过滤待字体元数据补齐）；
 *             currentFont 按 Qt 形状返回 XFont 值（[P1-35] Qt 名主入口），
 *             族名形态由次级入口 `XFontComboBox_currentFamily` 承担；
 *             displayFont 支持逐族登记（`setDisplayFont`/`displayFont`，
 *             [P1-36/37] Qt 名主入口；C 侧以 bool+出参表达 Qt 的 optional）；
 *             sizeHint 按 QFontComboBox::sizeHint（qfontcombobox.cpp:544-550）
 *             重载：高度取组合框口径，宽度固定为控件字体 'm' 宽的 14 倍
 *             ——不随最长族名变化；弹层宽按 qfontcombobox.cpp:526-537
 *             取 `min(控件宽×5/3, 屏宽)`（重载 XComboBox 的 PopupWidth
 *             虚槽；本机 Qt 6.8 探针 120 宽控件弹层 200）。
 *             信号 currentFontChanged(XFont*)。
 * @note       模块总开关 XFONTCOMBOBOX_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XFONTCOMBOBOX_H
#define XFONTCOMBOBOX_H

#if XGUI_ON
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
    /* ---- 指针组（8 字节对齐，置顶） ---- */
    void* m_fontSamples;  /**< 族名样例文本表（内部条目数组；按需扩容；NULL=未分配）。 */
    void* m_systemSamples;/**< 书写系统样例文本表（内部条目数组；按需扩容；NULL=未分配）。 */
    void* m_displayFonts; /**< 逐族显示字体表（内部条目数组；按需扩容；NULL=未分配；
                              对标 QFontComboBox::setDisplayFont(family, font)）。 */
    /* ---- ≥64B 值类型成员（XFont ≈72B，须 8 字节对齐；按手册第 5 步不擅改） ---- */
    XFont m_displayFont;  /**< 显示字体值（保持 XClassCopy/Move/Deinit 值语义；
                              公开写读入口已按 P2-52/53 收敛为逐族版
                              setDisplayFont/displayFont）。 */
    /* ---- 4 字节组 ---- */
    int m_filters;        /**< 字体过滤位标志（默认 AllFonts；setFontFilters
                              *   原样存取外部值——getter 往返契约，保持 int）。 */
    int m_fontSampleCount;/**< 族名样例表已用条数（0=空）。 */
    int m_fontSampleCap;  /**< 族名样例表已分配容量（0=未分配）。 */
    int m_systemSampleCount; /**< 书写系统样例表已用条数（0=空）。 */
    int m_systemSampleCap;   /**< 书写系统样例表已分配容量（0=未分配）。 */
    int m_displayFontCount;  /**< 逐族显示字体表已用条数（0=空）。 */
    int m_displayFontCap;    /**< 逐族显示字体表已分配容量（0=未分配）。 */
    /* ---- 1 字节组 ---- */
    uint8_t m_writingSystem; /**< 书写系统（XFontComboBoxWritingSystem，最大
                              *   Symbol=30；setWritingSystem 已按
                              *   xfcb_isValidWritingSystem 校验，默认 Any）。 */
    /* ---- currentFont 记忆（对标 QFontComboBoxPrivate::currentFont，
     *   值语义：NULL=未记。转发槽去重比对**记忆值**与新文本（Qt
     *   qfontcombobox.cpp `_q_currentChanged` 同口径），不比实时
     *   currentText()——后者随当前项已更新，比对恒等会吞掉首次
     *   currentFontChanged（2026-11-03 空→首插语义对齐后暴露）。 */
    XString* m_currentFont; /**< 已发 currentFontChanged 的族名（拥有）。 */
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

/**
 * @brief      设置字体过滤器（对标 QFontComboBox::setFontFilters）。
 * @details    按位标志过滤字体族并**重填条目列表**（Qt
 *             QFontComboBoxPrivate::updateModel，qfontcombobox.cpp:307-370）：
 *             - AllFonts：不过滤；
 *             - MonospacedFonts / ProportionalFonts：按族等宽度量探测
 *               （'i' 与 'W' 推进宽相等 ⇒ 等宽）；字体数据库无
 *               isFixedPitch 元数据接口，此为等价探测，未知族保守判为
 *               非等宽；
 *             - ScalableFonts / NonScalableFonts：按解析出的字库 face 种类
 *               （XFontFace_kind == Outline ⇒ 可缩放）；
 *             - 组合位：位间为**与**语义（Qt：每个族须满足全部选中条件）。
 *             过滤后条目重建会把当前项按 Qt updateModel 口径重设（列表非空
 *             取 0 并发射 currentIndexChanged/currentFontChanged，探针
 *             实测 setFontFilters(MonospacedFonts) 后 cur=0）。书写系统条件
 *             （setWritingSystem）因缺逐族标注数据不参与过滤。
 * @param self 目标控件；NULL 无操作。
 * @param filters FontFilter 位组合。
 * @return 无返回值。
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
/** @brief 按族名选中条目（对标 setCurrentFont(QFont(family))）。
 * @details 匹配顺序对齐 Qt 6.8 实测（QFontComboBox::setCurrentFont 经
 *          QFontInfo 解析族名）：
 *          ① 族名精确命中 ⇒ 选中；
 *          ② 大小写不敏感命中（QFontInfo 会规范族名大小写；探针
 *             setCurrentFont(QFont("dejavu serif")) 选中 "DejaVu Serif"）；
 *          ③ 都未命中 ⇒ 取本控件字体（XWidget_font）的族名再按 ①② 试——
 *             Qt 的 QFontInfo 对未知族名解析成应用缺省族（探针里即列表中的
 *             "思源黑体"）；本库无字体解析器，以控件字体族近似；
 *          ④ 仍未命中 ⇒ 无操作（不臆造条目）。
 *          命中后经 currentTextChanged → _q_currentChanged 发射
 *          currentFontChanged（同族重复 set 去重）。
 */
void XFontComboBox_setCurrentFamily(XFontComboBox* self, const char* family);

/**
 * @brief 查询当前字体（XFont 值返回；对标 QFontComboBox::currentFont，
 *        qfontcombobox.h:44）。
 * @details [P1-35] Qt 名主入口（原 `XFontComboBox_currentFont_2`；本族
 *          此前的 `XFontComboBox_currentFont` 宏别名按 P2-51 已删，Qt 名
 *          随之归还值版）。Qt 返回 QFont（可能带解析后的族名）；本函数
 *          以当前族名新建 XFont（缺省字号/粗细），返回值由调用方拥有，
 *          须 `XClassDeinit((XClass*)&font)` 释放内部字符串。
 *          族名形态（UTF-8 借用、零分配）由次级入口
 *          `XFontComboBox_currentFamily` 承担。
 * @param self 目标控件；NULL 返回默认构造字体。
 * @return XFont 值。
 */
XFont XFontComboBox_currentFont(const XFontComboBox* self);
/** @brief 按字体族名选中条目（对标 QFontComboBox::setCurrentFont；
 *         以 UTF-8 族名承载 QFont；转发 setCurrentFamily）。 */
void XFontComboBox_setCurrentFont(XFontComboBox* self, const char* family);

/* ==================== 显示字体与样例文本（状态承载） ==================== */

/**
 * @brief      登记指定族的显示字体（对标
 *             `QFontComboBox::setDisplayFont(const QString&, const QFont&)`，
 *             qfontcombobox.h:53）。
 * @details [P1-36] Qt 名主入口（原 `XFontComboBox_setDisplayFont_2` 让名
 *          后由本入口接管）。[P2-52] 原无族名单值版
 *          `XFontComboBox_setDisplayFont(self,font)` 已删（Qt 6.8 只有
 *          逐族版，且单值状态无任何消费者）。
 *          逐族登记（同一族重复登记为替换；NULL 字体=清除该族登记）。
 *          显示字体只影响该族在列表中的展示样式，不改当前项、不发信号。
 * @param self 目标控件；NULL 无操作。
 * @param family 字体族名（UTF-8；NULL/空串忽略）。
 * @param font 显示字体（借用，函数内深拷贝）；NULL 清除该族登记。
 * @return 无返回值。
 */
void XFontComboBox_setDisplayFont(XFontComboBox* self, const char* family,
                                   const XFont* font);
/**
 * @brief      查询指定族的显示字体（对标
 *             `std::optional<QFont> QFontComboBox::displayFont(const QString&)`
 *             const，qfontcombobox.h:54）。
 * @details [P1-37] Qt 名主入口（原 `XFontComboBox_displayFont_2` 让名后
 *          由本入口接管）；`bool + 出参` 是 C 的 optional 等价物，保留。
 *          [P2-53] 原无族名单值版 `XFontComboBox_displayFont(self)` 已删
 *          （Qt 6.8 只有逐族版；单值状态无消费者）。
 *          Qt 6.8 本机探针：已登记族返回登记字体；未登记族返回该族的
 *          QFont（QFont(family)，族名不存在时被字体系统解析成缺省族）；
 *          空族名返回空 optional。本实现：
 *          - family 为空/NULL ⇒ 返回 false（空 optional）；
 *          - 已登记 ⇒ 深拷贝登记字体写 *out，返回 true；
 *          - 未登记 ⇒ XFont(family) 写 *out，返回 true（与 Qt 同）。
 * @param self 目标控件；NULL 返回 false。
 * @param family 字体族名（UTF-8）。
 * @param out 输出字体（深拷贝；调用方负责 XClassDeinit）；NULL 时仅查询
 *            是否有登记。
 * @return 有值返回 true。
 */
bool XFontComboBox_displayFont(const XFontComboBox* self, const char* family,
                                XFont* out);
/**
 * @brief 查询字体族样例文本（新建 XString 返回；调用方持有并负责
 *        XClassDelete 释放）。
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
 *        XClassDelete 释放）。
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

#endif /* XGUI_ON */

#endif /* XFONTCOMBOBOX_H */
