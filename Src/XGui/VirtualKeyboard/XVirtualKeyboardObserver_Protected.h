/**
 * @file       XVirtualKeyboardObserver_Protected.h
 * @brief      XVirtualKeyboardObserver 保护头（类名级）：默认面板布局
 *             通知入口。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDOBSERVER_PROTECTED_H
#define XVIRTUALKEYBOARDOBSERVER_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XVirtualKeyboardObserver.h"

#if XVIRTUALKEYBOARD_ON

/**
 * @brief      使布局失效并按新描述符刷新（对标私有槽 invalidateLayout）。
 * @details    默认面板在布局/模式切换时调用：更新描述符缓存，变化时
 *             发 layoutChanged。observer 传单例（NULL 拒绝）。
 * @param      observer 观察器对象（通常 XVirtualKeyboardObserver_instance()）。
 * @param      layoutType 布局类型（UTF-8："main"/"symbols"/"digits"/
 *             "numbers"/"dialpad"）。
 * @param      locale 区域语言（UTF-8）。
 * @param      inputMode 当前输入模式枚举值。
 */
void XVirtualKeyboardObserver_invalidateLayout(
        XVirtualKeyboardObserver* observer, const char* layoutType,
        const char* locale, int inputMode);

#endif /* XVIRTUALKEYBOARD_ON */
#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDOBSERVER_PROTECTED_H */
