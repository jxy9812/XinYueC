/******************************************************************************
 * @file       xgui_hover_probe.c
 * @brief      悬停合成探针：复现「翻层删旧按钮 → 悬浮新按钮」场景，断言
 *             ENTER/LEAVE 派发链在**旧靶被销毁**后仍然工作。
 * @details    背景（2026-10-31 用户实测）：demo 测试菜单点进子菜单后，悬浮
 *             新按钮样式不再变化，切走再切回才恢复。
 *             根因：悬停合成器用「焦点/模态/抓取」三个瞬态锚反解
 *             窗口→顶层控件；翻层把带焦点的旧按钮 deleteLater 后锚全空，
 *             合成器解析不到顶层 → ENTER 永不再派发。
 *             修复：主路径改用桥接窗的 m_widget 反查（Qt QWidgetWindow::
 *             m_widget 同位，窗口与控件同生共死，构造上恒活）。
 *             本探针覆盖该链：销毁带焦点的旧控件后，向新控件注入鼠标移动，
 *             断言新控件拿到 UnderMouse（ENTER 已派发）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XWindow.h"
#include "XWindowSystemInterface.h"
#include "XPushButton.h"
#include "XMemory.h"
#include "XAtomic.h"

/* 统计：新按钮收到的 ENTER 次数（用 enterEvent 覆写计数）。 */
typedef struct HoverProbeButton
{
    XWidget m_base;   /**< 基类成员；必须是第一个。 */
    int m_enterCount; /**< 收到的 enterEvent 次数。 */
    int m_leaveCount; /**< 收到的 leaveEvent 次数。 */
} HoverProbeButton;

static XVtable* HoverProbeButton_class_init(void);
static void HoverProbeButton_enterEvent(XWidget* self, XEvent* event);
static void HoverProbeButton_leaveEvent(XWidget* self, XEvent* event);

XCLASS_DEFINE_BEGING(HoverProbeButton)
XCLASS_DEFINE_EXTEND_END(HoverProbeButton, XWidget)

/** @brief enterEvent 覆写：计数并转父类。 */
static void HoverProbeButton_enterEvent(XWidget* self, XEvent* event)
{
    HoverProbeButton* b = (HoverProbeButton*)self;
    if (b) b->m_enterCount++;
    XClass_Parent(XWidget, EXWidget_EnterEvent,
                  void (*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief leaveEvent 覆写：计数并转父类。 */
static void HoverProbeButton_leaveEvent(XWidget* self, XEvent* event)
{
    HoverProbeButton* b = (HoverProbeButton*)self;
    if (b) b->m_leaveCount++;
    XClass_Parent(XWidget, EXWidget_LeaveEvent,
                  void (*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static XVtable* HoverProbeButton_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HoverProbeButton)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_EnterEvent, HoverProbeButton_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, HoverProbeButton_leaveEvent);
    return XVTABLE_DEFAULT;
}

static HoverProbeButton* probeButton_create(XWidget* parent, int x, int y)
{
    HoverProbeButton* b =
        (HoverProbeButton*)XMalloc_System(sizeof(HoverProbeButton));
    if (!b) return NULL;
    memset(b, 0, sizeof(*b));
    XWidget_init(&b->m_base, parent, 0);
    XClassSetVtable(&b->m_base, HoverProbeButton);
    Set_Class_Memory(&b->m_base, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(&b->m_base, true);
    XWidget_setGeometry(&b->m_base, x, y, 100, 40);
    return b;
}

int main(int argc, char** argv)
{
    char* argv0 = (char*)"xgui_hover_probe";
    char* args[2];
    int rc = 0;
    XGuiApplication* app;
    XWidget* top;
    HoverProbeButton* oldBtn;
    HoverProbeButton* newBtn;
    XWindow* win;
    XPoint pos;

    args[0] = argv0;
    args[1] = NULL;
    (void)argc; (void)argv;

    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, args);
    if (!app) {
        fprintf(stderr, "[HOVER-PROBE] FATAL: no application\n");
        return 2;
    }

    /* 顶层控件 + 两个按钮（旧靶带焦点，翻层时被销毁）。 */
    top = XWidget_create(NULL, 0);
    if (!top) {
        fprintf(stderr, "[HOVER-PROBE] FATAL: no top\n");
        return 2;
    }
    XWidget_setGeometry(top, 0, 0, 400, 300);
    oldBtn = probeButton_create(top, 10, 10);
    newBtn = probeButton_create(top, 10, 60);
    if (!oldBtn || !newBtn) {
        fprintf(stderr, "[HOVER-PROBE] FATAL: no buttons\n");
        return 2;
    }
    XWidget_show(top);
    XWidget_show(&oldBtn->m_base);
    XWidget_show(&newBtn->m_base);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    win = (XWindow*)XWidget_windowHandle(top);
    if (!win) {
        fprintf(stderr, "[HOVER-PROBE] FATAL: no window handle\n");
        return 2;
    }

    /* 步骤 1：鼠标移到旧按钮上 → 旧按钮应收到 ENTER。
     * 注：先注入一次原生窗边界 ENTER（平台侧 EnterNotify 的等价物），
     * 让桥接路径把命中靶回报给悬停合成器（XWidget.c 回报钩子）——真实
     * GUI 里窗口获得焦点时的首个 ENTER 即走此路。之后**窗内移动**才由
     * 合成器换靶（Qt dispatchEnterLeave 同款）。 */
    pos.x = 50; pos.y = 30;
    XWindowSystemInterface_handleEnterEvent(win, pos, NULL);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XWindowSystemInterface_handleMouseEvent_ex(win, XEVENT_TYPE_MOUSE_MOVE,
                                               XMouseButton_NoButton,
                                               XMouseButton_NoButton,
                                               XKeyboardModifier_NoModifier,
                                               pos, NULL, 0);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    printf("[HOVER-PROBE] step1 oldEnter=%d (expect >=1)\n", oldBtn->m_enterCount);
    if (oldBtn->m_enterCount < 1) {
        fprintf(stderr, "[HOVER-PROBE] FAIL: step1 old button got no ENTER\n");
        rc = 1;
    }

    /* 步骤 2：给旧按钮焦点（模拟点击后的焦点锚），再销毁它——这正是
     *    「旧靶 deleteLater → 锚全空」的场景。销毁后**焦点锚必须为空**
     *    （否则合成器仍可经锚解析顶层，测不到缺陷本身）。 */
    XWidget_setFocusReason(&oldBtn->m_base, XFocusReason_Mouse);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    printf("[HOVER-PROBE] focus after set=%p (oldBtn=%p)\n",
           (void*)XWidget_appFocusWidget(), (void*)&oldBtn->m_base);
    /* 同步销毁（与 deleteLater 销掉同一个 XString/焦点锚/悬停登记白名单，
     * 但立即生效）：deleteLater 的 DeferredDelete 事件被 Qt 同款条件拦截
     * （无嵌套事件循环时 scope 层级不回落，事件不投递——探针无 exec 循环，
     * 事件永不兑现）。此处要的就是「旧靶已销毁」这一状态本身。 */
    XClassDelete((XClass*)&oldBtn->m_base);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    printf("[HOVER-PROBE] focus after delete=%p\n",
           (void*)XWidget_appFocusWidget());
    if (XWidget_appFocusWidget() != NULL) {
        fprintf(stderr, "[HOVER-PROBE] FAIL: precondition: focus anchor not "
                        "cleared after old target destroyed\n");
        rc = 1;
    }

    /* 步骤 3：鼠标移到新按钮上 → 新按钮必须收到 ENTER（旧靶消失后
     *    合成器仍要能解析顶层并派发）。 */
    pos.x = 50; pos.y = 80;
    XWindowSystemInterface_handleMouseEvent_ex(win, XEVENT_TYPE_MOUSE_MOVE,
                                               XMouseButton_NoButton,
                                               XMouseButton_NoButton,
                                               XKeyboardModifier_NoModifier,
                                               pos, NULL, 0);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    printf("[HOVER-PROBE] step3 newEnter=%d newLeave=%d (expect enter>=1)\n",
           newBtn->m_enterCount, newBtn->m_leaveCount);
    if (newBtn->m_enterCount < 1) {
        fprintf(stderr, "[HOVER-PROBE] FAIL: new button got no ENTER after "
                        "old target destroyed (hover synthesis broken)\n");
        rc = 1;
    }
    if (!XWidget_testAttribute(&newBtn->m_base, XWidgetAttribute_UnderMouse)) {
        fprintf(stderr, "[HOVER-PROBE] FAIL: new button lacks UnderMouse\n");
        rc = 1;
    }

    if (rc == 0) fprintf(stderr, "[HOVER-PROBE] PASS\n");
    return rc;
}
