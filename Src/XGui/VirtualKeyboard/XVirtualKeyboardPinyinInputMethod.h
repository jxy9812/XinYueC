/**
 * @file       XVirtualKeyboardPinyinInputMethod.h
 * @brief      XVirtualKeyboardPinyinInputMethod 拼音输入法插件（对标
 *             Qt VK pinyin 插件形态；内嵌 XPinyinEngine 实例=插件独占）。
 * @details    【apiMapping#13 定案】XPinyinEngine 实例归属=插件独占：
 *             面板零直连（m_ime 成员删除），全部消费面走 Qt 形态——
 *             - 候选=engine.wordCandidateListModel()（面板经 dataAt 取
 *               XVariant*）；本插件实现 selectionLists/Count/Data 钩子
 *               喂 XPinyinEngine 候选（WordCompletionLength=0、
 *               CanRemoveSuggestion=false、Dictionary=Default）；
 *             - 组串显示=context.preeditText()（状态镜像：本插件每次
 *               组串变化经 setPreeditText 同步，Qt pinyin
 *               pinyininputmethod.cpp:215 同型）；
 *             - 分页=面板本地 UI 状态（XPinyinEngine 的页机制留存状态
 *               机供 API 完整，不再被面板消费）；
 *             - 提交=Committed→context.commit（commitRequested 公共
 *               信号落地）；reset()=resetComposition（closePopup 弃草
 *               稿链）；中/EN=engine.setInputMode(Pinyin/Latin)。
 *             keyEvent 映射：'a'..'z' 追加拼写；Backspace 删组串；
 *             Return 提交原串；Space 提交首选；'1'..'9' 选候选（无页
 *             面板语义=全量下标 page*pageSize+d-1 由面板换算后经
 *             selectItem 走候选钩子，本插件 keyEvent 的数字键仅在组串
 *             中吞掉越界）；未消费返回 false。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON 且 XKEYBOARD_IME_ON（内嵌
 *             XPinyinEngine 状态机）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDPINYININPUTMETHOD_H
#define XVIRTUALKEYBOARDPINYININPUTMETHOD_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XVirtualKeyboardAbstractInputMethod.h"

#if XVIRTUALKEYBOARD_ON && XKEYBOARD_IME_ON

#include "XPinyinEngine.h"

/** @brief 声明 XVirtualKeyboardPinyinInputMethod 虚函数枚举：继承
 *         XVirtualKeyboardAbstractInputMethod（重载基类槽位，无新增）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardPinyinInputMethod)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardPinyinInputMethod,
                         XVirtualKeyboardAbstractInputMethod)

/**
 * @brief      拼音输入法插件对象；m_base 必须为第一个成员。
 * @details    m_ime 为插件独占组串状态机（内嵌实例；面板零直连）。
 */
typedef struct XVirtualKeyboardPinyinInputMethod
{
    XVirtualKeyboardAbstractInputMethod m_base; /**< 基类成员；必须是第一个。 */
    XPinyinEngine m_ime;                         /**< 插件独占组串状态机。 */
} XVirtualKeyboardPinyinInputMethod;

/** @brief 初始化类虚函数表并返回共享表指针。 */
XVtable* XVirtualKeyboardPinyinInputMethod_class_init(void);

/**
 * @brief      初始化（状态机默认中文态）。
 * @param      self 待初始化对象。
 */
void XVirtualKeyboardPinyinInputMethod_init(
        XVirtualKeyboardPinyinInputMethod* self);

/**
 * @brief      使用默认内存类型创建。
 * @return     新对象指针；失败返回 NULL。
 */
#define XVirtualKeyboardPinyinInputMethod_create() \
    XVirtualKeyboardPinyinInputMethod_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型创建。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XVirtualKeyboardPinyinInputMethod*
XVirtualKeyboardPinyinInputMethod_create_ex(XMemoryType memory);


/**
 * @brief      locale 工厂（注册表签名；返回基类指针形态新实例）。
 * @return     新建输入法实例；失败返回 NULL。
 */
XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardPinyinInputMethod_factory(void);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDPINYININPUTMETHOD_H */
