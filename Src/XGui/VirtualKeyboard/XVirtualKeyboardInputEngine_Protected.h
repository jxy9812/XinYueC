/**
 * @file       XVirtualKeyboardInputEngine_Protected.h
 * @brief      XVirtualKeyboardInputEngine 保护头（类名级）：引擎私造
 *             入口、语言插件工厂注册表接口与内部访问（契约头，签名冻
 *             结）。
 * @details    - create_ex 仅 InputContext TU 调用（Qt 私有构造 :39 口
 *               径）；Trace/SelectionListModel 的引擎侧创建入口亦在此
 *               汇总声明（实现在各自 TU）；
 *             - locale→工厂注册表：QVirtualKeyboardExtensionPlugin 的
 *               引擎内等价物——按 locale 注册工厂函数，面板/上下文装
 *               配链经 createInputMethod 取实例；tcime/hangul/openwnn/
 *               thai=预留点（注册即插）；
 *             - wordCandidateListVisibleHint setter（属性写侧，Qt 为
 *               布局层驱动）与候选模型重建入口 updateSelectionListModels。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDINPUTENGINE_PROTECTED_H
#define XVIRTUALKEYBOARDINPUTENGINE_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XVirtualKeyboardInputEngine.h"
#include "XVirtualKeyboardAbstractInputMethod.h"
#include "XVirtualKeyboardSelectionListModel.h"
#include "XVirtualKeyboardTrace.h"

#if XVIRTUALKEYBOARD_ON

/** @brief locale 输入法数量上限（注册表容量；超出拒绝并诊断）。 */
#define XVIRTUALKEYBOARD_LOCALE_FACTORY_MAX 16

/**
 * @brief      语言输入法工厂函数（locale→实例；对标 QML 插件
 *             createInputMethod 机制）。
 * @return     新建输入法实例（堆对象，引擎装配后所有权归调用方装配链
 *             管理——引擎仅借用）；失败返回 NULL。
 */
typedef XVirtualKeyboardAbstractInputMethod* (
        *XVirtualKeyboardInputMethodFactory)(void);

/**
 * @brief      使用默认内存类型创建引擎（仅 InputContext TU 调用）。
 * @param      context 所属上下文借用指针（反向引用；可为 NULL）。
 * @return     新引擎指针；失败返回 NULL。
 */
XVirtualKeyboardInputEngine* XVirtualKeyboardInputEngine_create_forContext(
        XVirtualKeyboardInputContext* context);

/**
 * @brief      初始化（栈对象路径；引擎创建入口内部使用）。
 * @param      self 待初始化引擎。
 * @param      context 所属上下文借用指针；可为 NULL。
 */
void XVirtualKeyboardInputEngine_init_ex(XVirtualKeyboardInputEngine* self,
                                         XVirtualKeyboardInputContext* context);

/**
 * @brief      注册 locale 输入法工厂（语言插件 TU 调用；同 locale 重
 *             复注册以最后一次为准）。
 * @param      locale 区域语言（UTF-8 借用，注册表持拷贝；如 "zh_CN"）。
 * @param      factory 工厂函数；NULL 拒绝。
 * @return     注册成功返回 true；表满/入参非法返回 false。
 */
bool XVirtualKeyboardInputEngine_registerInputMethodFactory(
        const char* locale, XVirtualKeyboardInputMethodFactory factory);

/**
 * @brief      注册 locale 输入法工厂到指定引擎（直指目标，不路由单
 *             例）。上下文 init 期单例尚未落座（instance() 重入会无
 *             限递归），构造链注册必须经本入口；语义同上。
 * @param      self 目标引擎（通常为上下文私有块自有的 m_engine）。
 * @param      locale 区域语言（UTF-8 借用，注册表持拷贝）。
 * @param      factory 工厂函数；NULL 拒绝。
 * @return     注册成功返回 true；表满/入参非法返回 false。
 */
bool XVirtualKeyboardInputEngine_registerInputMethodFactory_for(
        XVirtualKeyboardInputEngine* self, const char* locale,
        XVirtualKeyboardInputMethodFactory factory);

/**
 * @brief      按 locale 经注册表创建输入法实例（未注册返回 NULL）。
 * @return     新建输入法实例；调用方负责所有权（通常立即交
 *             setInputMethod 装配链）。
 */
XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardInputEngine_createInputMethod(const char* locale);

/**
 * @brief      设置候选列表可见提示（属性写侧；变化发
 *             wordCandidateListVisibleHintChanged）。
 */
void XVirtualKeyboardInputEngine_setWordCandidateListVisibleHint(
        XVirtualKeyboardInputEngine* self, bool visible);

/**
 * @brief      重建候选模型数据源（对标 updateSelectionListModels）：
 *             按输入法 selectionLists 申报装配 wordCandidateList 模型，
 *             未申报类型置空数据源；随后发
 *             wordCandidateListModelChanged（首次创建模型时）。
 */
void XVirtualKeyboardInputEngine_updateSelectionListModels(
        XVirtualKeyboardInputEngine* self);

/**
 * @brief      Trace 引擎侧创建入口（QML_UNCREATABLE 口径：只能经
 *             traceBegin 获得）。
 * @return     新轨迹对象；失败返回 NULL。
 */
XVirtualKeyboardTrace* XVirtualKeyboardTrace_create_engine(void);

#endif /* XVIRTUALKEYBOARD_ON */
#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDINPUTENGINE_PROTECTED_H */
