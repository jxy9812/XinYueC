/**
 * @file       XPlatformAccessibility_win32.c
 * @brief      Windows UI Automation 辅助功能桥接。
 * @details    使用系统 HWND host provider 公开每个 XWindow；Windows 的
 *              UIA 客户端可通过 UI Automation Core 查询窗口名称、角色和
 *              几何。窗口对象树/属性变化以 LayoutInvalidated 通知刷新。
 */
#include "XPlatformAccessibility.h"

#if XWINDOW_ON && XACCESSIBLE_ON && XPLATFORMACCESSIBILITY_UIA_ON && \
    defined(_WIN32)

#include "XWindow.h"
#include "XMemory.h"
#include <windows.h>
#include <ole2.h>
#include <uiautomationcore.h>
#include <string.h>

typedef struct XPlatformAccessibilityWin32
{
    bool comInitialized;
    bool active;
} XPlatformAccessibilityWin32;

bool XPlatformAccessibilityDriver_start(XPlatformAccessibility* bridge,
                                        void** nativeState)
{
    HRESULT result;
    XPlatformAccessibilityWin32* state;
    (void)bridge;
    if (nativeState) *nativeState = NULL;
    if (!nativeState) return false;
    state = (XPlatformAccessibilityWin32*)XMalloc_System(sizeof(*state));
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (result == S_OK || result == S_FALSE) {
        state->comInitialized = true;
        state->active = true;
    } else if (result == RPC_E_CHANGED_MODE) {
        /* 调用线程已选定其它 COM apartment；UIA 仍可使用现有 apartment。 */
        state->active = true;
    }
    *nativeState = state;
    return state->active;
}

void XPlatformAccessibilityDriver_stop(void* nativeState)
{
    XPlatformAccessibilityWin32* state = (XPlatformAccessibilityWin32*)nativeState;
    if (!state) return;
    if (state->comInitialized) CoUninitialize();
    XFree_System(state);
}

bool XPlatformAccessibilityDriver_isActive(void* nativeState)
{
    XPlatformAccessibilityWin32* state = (XPlatformAccessibilityWin32*)nativeState;
    return state && state->active;
}

void XPlatformAccessibilityDriver_notify(void* nativeState,
                                         XAccessibleEvent event,
                                         XAccessible* accessible)
{
    XWindow* window;
    HWND hwnd;
    IRawElementProviderSimple* provider;
    (void)event;
    if (!XPlatformAccessibilityDriver_isActive(nativeState) || !accessible)
        return;
    window = XAccessible_window(accessible);
    if (!window) return;
    hwnd = (HWND)(uintptr_t)XWindow_winId(window);
    if (!hwnd || !IsWindow(hwnd)) return;
    provider = NULL;
    if (FAILED(UiaHostProviderFromHwnd(hwnd, &provider)) || !provider)
        return;
    /* host provider 从 HWND 读取标题/矩形；这一通知让屏幕阅读器在 XWindow
       的 title、geometry、visible 生命周期变化后重新获取标准属性。 */
    (void)UiaRaiseAutomationEvent(provider, UIA_LayoutInvalidatedEventId);
    provider->lpVtbl->Release(provider);
}

void XPlatformAccessibilityDriver_processEvents(void* nativeState)
{ (void)nativeState; }

#endif /* Windows UIA */
