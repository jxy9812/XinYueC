#include "XStackedWidgetTest.h"
#include "XStackedWidget.h"
#if XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON
#include "XLabel.h"
#include "XMemory.h"
#include "XEvent.h"
#include <stdio.h>
#include <string.h>

static int sw_failures = 0;
static int sw_currentChanged = 0;
static int sw_widgetRemoved = 0;
static int sw_lastIndex = -1;

static void sw_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[SW-FAIL] %s\n", what ? what : "");
        ++sw_failures;
    }
}

static void sw_currentChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args) return;
    XVarList_args_1(args, int, index);
    sw_lastIndex = index;
    ++sw_currentChanged;
}

static void sw_widgetRemovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++sw_widgetRemoved;
}

bool XStackedWidgetTest_runAll(void)
{
    XStackedWidget* stack = XStackedWidget_create(NULL, 0);
    XLabel* page0 = XLabel_create(NULL, 0);
    XLabel* page1 = XLabel_create(NULL, 0);
    XLabel* page2 = XLabel_create(NULL, 0);

    sw_expect(stack != NULL, "XStackedWidget 创建");
    if (sw_failures == 0) {
    sw_expect(XStackedWidget_count(stack) == 0, "初始页数 0");
    sw_expect(XStackedWidget_currentIndex(stack) == -1, "初始索引 -1");
    sw_expect(XStackedWidget_currentWidget(stack) == NULL,
              "初始无当前页");

    /* 1. addWidget/insertWidget/count。 */
    sw_expect(XStackedWidget_addWidget(stack, (XWidget*)page0) == 0,
              "addWidget page0 索引 0");
    sw_expect(XStackedWidget_addWidget(stack, (XWidget*)page2) == 1,
              "addWidget page2 索引 1");
    sw_expect(XStackedWidget_insertWidget(stack, 1,
               (XWidget*)page1) == 1, "insertWidget page1 到索引 1");
    sw_expect(XStackedWidget_count(stack) == 3, "共 3 页");
    sw_expect(XStackedWidget_widget(stack, 1) == (XWidget*)page1,
              "widget(1) 返回 page1");
    sw_expect(XStackedWidget_indexOf(stack, (XWidget*)page1) == 1,
              "indexOf(page1) 为 1");
    sw_expect(XStackedWidget_indexOf(stack, (XWidget*)page2) == 2,
              "indexOf(page2) 为 2");
    sw_expect(XStackedWidget_indexOf(stack, NULL) == -1, "indexOf(NULL) -1");

    /* 2. 当前页切换与信号。 */
    sw_currentChanged = 0;
    sw_lastIndex = -1;
    XObject_connect_2((XObject*)stack,
        XSignal(XStackedWidget_currentChanged_signal), sw_currentChangedSlot);
    XStackedWidget_setCurrentIndex(stack, 1);
    sw_expect(XStackedWidget_currentIndex(stack) == 1, "setCurrentIndex(1)");
    sw_expect(XStackedWidget_currentWidget(stack) == (XWidget*)page1,
              "currentWidget 为 page1");
    sw_expect(sw_currentChanged == 1 && sw_lastIndex == 1,
              "currentChanged 发射一次且携带索引 1");
    XStackedWidget_setCurrentWidget(stack, (XWidget*)page2);
    sw_expect(XStackedWidget_currentIndex(stack) == 2,
              "setCurrentWidget(page2)");

    /* 3. removeWidget 与 widgetRemoved 信号。 */
    sw_widgetRemoved = 0;
    XObject_connect_2((XObject*)stack,
        XSignal(XStackedWidget_widgetRemoved_signal), sw_widgetRemovedSlot);
    XStackedWidget_removeWidget(stack, (XWidget*)page1);
    sw_expect(XStackedWidget_count(stack) == 2, "移除后剩 2 页");
    sw_expect(XStackedWidget_indexOf(stack, (XWidget*)page1) == -1,
              "移除后 indexOf -1");
    sw_expect(sw_widgetRemoved == 1, "widgetRemoved 发射一次");
    sw_expect(XStackedWidget_count(stack) == 2, "移除后页数 2");

    XStackedWidget_delete_base(stack);
    /* page0/page2 已随 stack 析构（addWidget 后所有权归容器）；
       page1 经 removeWidget 归还调用方，仍由测试销毁。 */
    XLabel_delete_base(page1);

    {
        int failures = sw_failures;
        sw_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XStackedWidget test: PASS\n");
            return true;
        }
        fprintf(stderr, "XStackedWidget test: %d assertion(s) failed\n",
                failures);
        return false;
    }
}
}
#else
bool XStackedWidgetTest_runAll(void)
{
    fprintf(stderr, "XStackedWidgetTest: skipped (module off)\n");
    return true;
}
#endif /* XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON */
