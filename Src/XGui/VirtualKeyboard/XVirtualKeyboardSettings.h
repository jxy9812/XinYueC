/**
 * @file       XVirtualKeyboardSettings.h
 * @brief      XVirtualKeyboardSettings 虚拟键盘设置单例公开 API（对标
 *             Qt VK 内部 Settings 单例 settings_p.h:40-117 属性面）。
 * @details    偏离声明：Qt 无公共 C++ Settings 类（QML 单例后端私有），
 *             本类为 XGui C++ 单例（进程单例 instance()）。属性面照抄
 *             settings_p.h；字符串 setter 走 XString 主版本 + _2 UTF-8
 *             转发。存储-only 属性如实标注（style/styleName/locale 族/
 *             layoutPath/userDataPath/hwrTimeoutFor*fullScreenMode/
 *             visibleFunctionKeys）。XGui 扩展：setKeyboardEnabled 总
 *             开关（Qt 无直接等价物，diverge）——false 时守护停弹+已
 *             弹层收起+引擎虚键吞掉+commitRequested 无人消费。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON；实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDSETTINGS_H
#define XVIRTUALKEYBOARDSETTINGS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"
#include "XString.h"

#if XVIRTUALKEYBOARD_ON

/** @brief 声明 XVirtualKeyboardSettings 虚函数枚举：继承 XObject（无
 *         新增槽位）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardSettings)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardSettings, XObject)

/**
 * @brief      设置单例对象；m_class 必须为第一个成员。
 * @details    m_data 私有块保存全部属性存储。
 */
typedef struct XVirtualKeyboardSettings
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块，由对象拥有；仅供实现使用。 */
} XVirtualKeyboardSettings;

/**
 * @brief      初始化类虚函数表并返回共享表指针。
 * @return     类共享虚函数表指针（进程期常驻，借用）；不失败。
 */
XVtable* XVirtualKeyboardSettings_class_init(void);

/**
 * @brief      进程单例（惰性创建；对标 Settings::instance）。
 * @details    首次调用时创建并初始化单例；后续调用返回同一对象。
 * @return     单例借用指针，不得释放；分配失败返回 NULL（空句柄语义
 *             的调用方没有提供对象之外的异常路径）。
 */
XVirtualKeyboardSettings* XVirtualKeyboardSettings_instance(void);

/* ==================== 字符串属性（XString 主版本 + _2 转发） ==================== */

/**
 * @brief      返回样式名（存储-only；对标 styleName 属性读侧）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     新建 XString*，调用方负责释放；self 为 NULL 或分配失败
 *             返回 NULL。
 */
XString* XVirtualKeyboardSettings_styleName(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置样式名（存储-only；发 styleNameChanged）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      styleName 新样式名（借用，函数不取得所有权）；NULL 按
 *             空字符串处理。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setStyleName(XVirtualKeyboardSettings* self,
                                           const XString* styleName);
/**
 * @brief      设置样式名（UTF-8 转发版本；对标 setStyleName）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      styleName 新样式名，按 UTF-8 解码；NULL 按空字符串处理，
 *             只在调用期间借用。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setStyleName_2(XVirtualKeyboardSettings* self,
                                             const char* styleName);

/**
 * @brief      返回区域语言（对标 locale 属性读侧）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     新建 XString*，调用方负责释放；默认值 "zh_CN"；self 为
 *             NULL 或分配失败返回 NULL。
 */
XString* XVirtualKeyboardSettings_locale(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置区域语言（本轮 zh_CN+latin 一套，存储-only）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      locale 新区域语言（借用，函数不取得所有权）；NULL 按
 *             空字符串处理。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setLocale(XVirtualKeyboardSettings* self,
                                        const XString* locale);
/**
 * @brief      设置区域语言（UTF-8 转发版本；对标 setLocale）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      locale 新区域语言，按 UTF-8 解码；NULL 按空字符串处理，
 *             只在调用期间借用。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setLocale_2(XVirtualKeyboardSettings* self,
                                          const char* locale);

/**
 * @brief      返回布局搜索路径（存储-only；对标 layoutPath 属性读侧）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     新建 XString*，调用方负责释放；self 为 NULL 或分配失败
 *             返回 NULL。
 */
XString* XVirtualKeyboardSettings_layoutPath(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置布局搜索路径（存储-only；布局为 C 静态表，目录解析
 *             scopeOut）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      layoutPath 新布局搜索路径（借用，函数不取得所有权）；
 *             NULL 按空字符串处理。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setLayoutPath(XVirtualKeyboardSettings* self,
                                            const XString* layoutPath);
/**
 * @brief      设置布局搜索路径（UTF-8 转发版本；对标 setLayoutPath）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      layoutPath 新布局搜索路径，按 UTF-8 解码；NULL 按空字符
 *             串处理，只在调用期间借用。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setLayoutPath_2(XVirtualKeyboardSettings* self,
                                              const char* layoutPath);

/**
 * @brief      返回用户数据路径（存储-only；对标 userDataPath 属性读侧）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     新建 XString*，调用方负责释放；self 为 NULL 或分配失败
 *             返回 NULL。
 */
XString* XVirtualKeyboardSettings_userDataPath(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置用户数据路径（存储-only；用户词典持久化 N-A）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      userDataPath 新用户数据路径（借用，函数不取得所有权）；
 *             NULL 按空字符串处理。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setUserDataPath(
        XVirtualKeyboardSettings* self, const XString* userDataPath);
/**
 * @brief      设置用户数据路径（UTF-8 转发版本；对标 setUserDataPath）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      userDataPath 新用户数据路径，按 UTF-8 解码；NULL 按空字
 *             符串处理，只在调用期间借用。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setUserDataPath_2(
        XVirtualKeyboardSettings* self, const char* userDataPath);

/* ==================== 区域列表（本轮 zh_CN+latin 一套） ==================== */

/**
 * @brief      返回可用区域列表（对标 availableLocales）。
 * @details    本轮恒 {"zh_CN", "en"}（一套布局集；42 locale 集合
 *             scopeOut）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @param      outLocales 调用方提供的存储空间（const char* 数组，元素
 *             为内部静态字符串借用指针）；可为 NULL，NULL 时仅查询条数。
 * @param      maxCount outLocales 容量上限（条数）；outLocales 为 NULL
 *             时忽略。
 * @return     实际可用区域条数；outLocales 非 NULL 时最多写入 maxCount
 *             条。self 为 NULL 返回 0。
 */
int XVirtualKeyboardSettings_availableLocales(
        const XVirtualKeyboardSettings* self, const char** outLocales,
        int maxCount);

/**
 * @brief      返回激活区域列表（对标 activeLocales）。
 * @details    默认等同可用列表；setActiveLocales 可收窄（存储面）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @param      outLocales 调用方提供的存储空间（元素为内部静态字符串
 *             借用指针）；可为 NULL，NULL 时仅查询条数。
 * @param      maxCount outLocales 容量上限（条数）；outLocales 为 NULL
 *             时忽略。
 * @return     实际激活区域条数；outLocales 非 NULL 时最多写入 maxCount
 *             条。self 为 NULL 返回 0。
 */
int XVirtualKeyboardSettings_activeLocales(
        const XVirtualKeyboardSettings* self, const char** outLocales,
        int maxCount);

/**
 * @brief      设置激活区域列表（校验必须在可用列表内；发
 *             activeLocalesChanged）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      locales 待激活区域名数组（UTF-8；借用，函数不取得所有
 *             权）；可为 NULL。
 * @param      count locales 条目数；locales 为 NULL 时应为 0。
 * @return     全部条目有效并已设置返回 true；任一条目不在可用列表内、
 *             self 或 locales 为 NULL 返回 false 且对象保持不变。
 */
bool XVirtualKeyboardSettings_setActiveLocales(
        XVirtualKeyboardSettings* self, const char* const* locales,
        int count);

/* ==================== 数值/布尔属性 ==================== */

/**
 * @brief      返回候选条自动隐藏延时（对标 wclAutoHideDelay 读侧）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     延时值（毫秒；默认 5000）；self 为 NULL 返回 0。
 */
int XVirtualKeyboardSettings_wclAutoHideDelay(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置候选条自动隐藏延时（对标 wclAutoHideDelay 写侧）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      delayMs 新延时值（毫秒）。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setWclAutoHideDelay(
        XVirtualKeyboardSettings* self, int delayMs);

/**
 * @brief      返回候选条常显（对标 wclAlwaysVisible 读侧）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     常显返回 true（默认 false）；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardSettings_wclAlwaysVisible(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置候选条常显（对标 wclAlwaysVisible 写侧）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      enabled true=候选条常显。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setWclAlwaysVisible(
        XVirtualKeyboardSettings* self, bool enabled);

/**
 * @brief      返回候选自动提交（对标 wclAutoCommitWord 读侧；存储-only）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     自动提交返回 true（默认 false）；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardSettings_wclAutoCommitWord(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置候选自动提交（对标 wclAutoCommitWord 写侧；存储-only）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      enabled true=候选自动提交。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setWclAutoCommitWord(
        XVirtualKeyboardSettings* self, bool enabled);

/**
 * @brief      返回全屏模式（对标 fullScreenMode 读侧；存储-only）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     全屏模式返回 true（默认 false）；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardSettings_fullScreenMode(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置全屏模式（对标 fullScreenMode 写侧；存储-only，影子
 *             输入控件 scopeOut）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      enabled true=启用全屏模式。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setFullScreenMode(
        XVirtualKeyboardSettings* self, bool enabled);

/**
 * @brief      返回字母手写超时（对标 hwrTimeoutForAlphabetic 读侧；
 *             存储-only，手写引擎 N-A）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     超时值（毫秒；默认 500）；self 为 NULL 返回 0。
 */
int XVirtualKeyboardSettings_hwrTimeoutForAlphabetic(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置字母手写超时（对标写侧；存储-only）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      timeoutMs 新超时值（毫秒）。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setHwrTimeoutForAlphabetic(
        XVirtualKeyboardSettings* self, int timeoutMs);

/**
 * @brief      返回 CJK 手写超时（对标 hwrTimeoutForCjk 读侧；存储-only）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     超时值（毫秒；默认 500）；self 为 NULL 返回 0。
 */
int XVirtualKeyboardSettings_hwrTimeoutForCjk(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置 CJK 手写超时（对标写侧；存储-only）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      timeoutMs 新超时值（毫秒）。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setHwrTimeoutForCjk(
        XVirtualKeyboardSettings* self, int timeoutMs);

/**
 * @brief      返回设置级输入法提示位（对标 inputMethodHints；与控件级
 *             hints OR 叠加，见 InputContext）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     提示位（可按位组合）；self 为 NULL 返回 0。
 */
uint32_t XVirtualKeyboardSettings_inputMethodHints(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置输入法提示位（变化时发 inputMethodHintsChanged）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      hints 新提示位（可按位组合）。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setInputMethodHints(
        XVirtualKeyboardSettings* self, uint32_t hints);

/**
 * @brief      返回手写模式禁用（对标 handwritingModeDisabled 读侧；
 *             存储-only）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     禁用返回 true（默认 false）；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardSettings_handwritingModeDisabled(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置手写模式禁用（对标写侧；存储-only）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      disabled true=禁用手写模式。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setHandwritingModeDisabled(
        XVirtualKeyboardSettings* self, bool disabled);

/**
 * @brief      返回默认输入法禁用（对标 defaultInputMethodDisabled 读侧）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     禁用返回 true（默认 false）；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardSettings_defaultInputMethodDisabled(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置默认输入法禁用（对标写侧）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      disabled true=禁用默认输入法。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setDefaultInputMethodDisabled(
        XVirtualKeyboardSettings* self, bool disabled);

/**
 * @brief      返回默认词典禁用（对标 defaultDictionaryDisabled 读侧；
 *             与 DictionaryManager.activeDictionaries 联动面本轮简化）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     禁用返回 true（默认 false）；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardSettings_defaultDictionaryDisabled(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置默认词典禁用（对标写侧）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      disabled true=禁用默认词典。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setDefaultDictionaryDisabled(
        XVirtualKeyboardSettings* self, bool disabled);

/**
 * @brief      返回可见功能键位集（对标 visibleFunctionKeys 读侧；
 *             存储-only）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     功能键位集（可按位组合）；self 为 NULL 返回 0。
 */
uint32_t XVirtualKeyboardSettings_visibleFunctionKeys(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置可见功能键位集（对标写侧；存储-only）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      functionKeys 新功能键位集（可按位组合）。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setVisibleFunctionKeys(
        XVirtualKeyboardSettings* self, uint32_t functionKeys);

/**
 * @brief      返回回车收面板（对标 closeOnReturn 读侧；非 MultiLine
 *             回车收面板，MultiLine 不收）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     回车收面板返回 true（默认 false）；self 为 NULL 返回
 *             false。
 */
bool XVirtualKeyboardSettings_closeOnReturn(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置回车收面板（对标 closeOnReturn 写侧）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      enabled true=非 MultiLine 输入时回车收起面板。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setCloseOnReturn(
        XVirtualKeyboardSettings* self, bool enabled);

/* ==================== XGui 扩展 ==================== */

/**
 * @brief      返回键盘总开关（XGui 扩展，Qt 无直接等价物；对标读侧，
 *             默认 true）。
 * @details    false=守护停弹+已弹层收起+引擎虚键吞掉+commitRequested
 *             无人消费。直呼 XVirtualKeyboard_popup 不受抑制（保底口径）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     键盘可用返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardSettings_keyboardEnabled(
        const XVirtualKeyboardSettings* self);
/**
 * @brief      设置键盘总开关（XGui 扩展；变化发 keyboardEnabledChanged）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @param      enabled false=守护停弹+已弹层收起+引擎虚键吞掉。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardSettings_setKeyboardEnabled(
        XVirtualKeyboardSettings* self, bool enabled);

/**
 * @brief      触发用户数据复位通知（对标 userDataReset；本轮清无持久
 *             化面，仅发信号）。
 * @param      self 单例对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XVirtualKeyboardSettings_userDataReset(XVirtualKeyboardSettings* self);

/* ==================== 信号（全 *Changed + userDataReset，纯 ID getter） ==================== */

/**
 * @brief      styleNameChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_styleNameChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      localeChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_localeChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      availableLocalesChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_availableLocalesChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      activeLocalesChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_activeLocalesChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      layoutPathChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_layoutPathChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      wclAutoHideDelayChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_wclAutoHideDelayChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      wclAlwaysVisibleChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_wclAlwaysVisibleChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      wclAutoCommitWordChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_wclAutoCommitWordChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      fullScreenModeChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_fullScreenModeChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      userDataPathChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_userDataPathChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      hwrTimeoutForAlphabeticChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_hwrTimeoutForAlphabeticChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      hwrTimeoutForCjkChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_hwrTimeoutForCjkChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      inputMethodHintsChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_inputMethodHintsChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      handwritingModeDisabledChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_handwritingModeDisabledChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      defaultInputMethodDisabledChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_defaultInputMethodDisabledChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      defaultDictionaryDisabledChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_defaultDictionaryDisabledChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      visibleFunctionKeysChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_visibleFunctionKeysChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      closeOnReturnChanged() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_closeOnReturnChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      keyboardEnabledChanged() 信号标识（XGui 扩展）。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_keyboardEnabledChanged_signal(
        XVirtualKeyboardSettings* self);
/**
 * @brief      userDataReset() 信号标识。
 * @param      self 单例对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSettings_userDataReset_signal(
        XVirtualKeyboardSettings* self);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDSETTINGS_H */
