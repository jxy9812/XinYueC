/**
 * @file       XVirtualKeyboardDictionaryManager_Protected.h
 * @brief      XVirtualKeyboardDictionaryManager 保护头（类名级）：词典
 *             创建入口（Qt friend 私有构造口径）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDDICTIONARYMANAGER_PROTECTED_H
#define XVIRTUALKEYBOARDDICTIONARYMANAGER_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XVirtualKeyboardDictionaryManager.h"
#include "XVirtualKeyboardDictionary.h"

#if XVIRTUALKEYBOARD_ON

/**
 * @brief      管理器 TU 专用词典创建入口（仅词典 TU 实现、管理器 TU
 *             调用；公共面无创建 API——Qt friend 口径）。
 * @param      name 词典名（拷贝存储）。
 * @return     新词典对象；失败返回 NULL。所有权归调用方。
 */
XVirtualKeyboardDictionary* XVirtualKeyboardDictionary_create_named(
        const char* name);

/**
 * @brief      初始化词典（栈对象路径；创建入口内部使用）。
 * @param      self 待初始化词典。
 */
void XVirtualKeyboardDictionary_init(XVirtualKeyboardDictionary* self);

#endif /* XVIRTUALKEYBOARD_ON */
#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDDICTIONARYMANAGER_PROTECTED_H */
