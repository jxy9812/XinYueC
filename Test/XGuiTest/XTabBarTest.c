#include "XTabBarTest.h"
#include "XTabBar.h"
#include "XTabWidget.h"
#include "XMemory.h"
#include "XEvent.h"
#include <stdio.h>
#include <string.h>

static int tb_failures = 0;
static int tb_curChanged = 0;
static void tb_expect(bool c, const char* w)
{
    if (!c) { fprintf(stderr, "[TB-FAIL] %s\n", w ? w : ""); ++tb_failures; }
}
static void tb_curChangedSlot(void* s, XVarList* a) { (void)s; (void)a; ++tb_curChanged; }

bool XTabBarTest_runAll(void)
{
    XTabBar* bar = XTabBar_create(NULL, 0);
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* p1 = XWidget_create((XWidget*)tw, 0);
    XWidget* p2 = XWidget_create((XWidget*)tw, 0);

    /* 1. TabBar：addTab/insertTab/removeTab。 */
    XTabBar_addTab(bar, "Tab1");
    XTabBar_addTab(bar, "Tab2");
    XTabBar_insertTab(bar, 0, "First");
    tb_expect(XTabBar_count(bar) == 3, "插入后 3 项");
    tb_expect(strcmp(XTabBar_tabText(bar, 0), "First") == 0, "插到最前");
    tb_expect(XTabBar_currentIndex(bar) == 0, "首次添加当前=0");

    /* 2. setCurrentIndex + 信号。 */
    tb_curChanged = 0;
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_currentChanged_signal(bar),
                      tb_curChangedSlot);
    XTabBar_setCurrentIndex(bar, 2);
    tb_expect(XTabBar_currentIndex(bar) == 2 && tb_curChanged == 1,
              "setCurrentIndex 发射一次");
    XTabBar_setCurrentIndex(bar, 2);
    tb_expect(tb_curChanged == 1, "同值不重复发射");

    /* 3. setTabText/isTabEnabled。 */
    XTabBar_setTabText(bar, 0, "改文本");
    tb_expect(strcmp(XTabBar_tabText(bar, 0), "改文本") == 0, "setTabText 生效");
    XTabBar_setTabEnabled(bar, 0, false);
    tb_expect(!XTabBar_isTabEnabled(bar, 0), "setTabEnabled(false) 生效");
    XTabBar_setTabEnabled(bar, 0, true);

    /* 4. removeTab 与索引收敛。 */
    XTabBar_removeTab(bar, 0);
    tb_expect(XTabBar_count(bar) == 2 && XTabBar_currentIndex(bar) <= 1,
              "removeTab 后索引收敛");

    /* 5. TabWidget：addTab/indexOf/currentWidget。 */
    tb_curChanged = 0;
    XTabWidget_addTab(tw, p1, "页一");
    XTabWidget_addTab(tw, p2, "页二");
    tb_expect(XTabWidget_count(tw) == 2, "TabWidget 2 页");
    tb_expect(XTabWidget_indexOf(tw, p2) == 1, "indexOf 命中");
    tb_expect(XTabWidget_currentWidget(tw) == p1, "默认显示页一");
    XTabWidget_setCurrentIndex(tw, 1);
    tb_expect(XTabWidget_currentWidget(tw) == p2, "切页后当前页正确");
    XTabWidget_setTabText(tw, 1, "页二改");
    tb_expect(strcmp(XTabWidget_tabText(tw, 1), "页二改") == 0,
              "setTabText 转发页签条");

    /* 6. tabsClosable/movable 转发。 */
    XTabWidget_setTabsClosable(tw, true);
    tb_expect(XTabWidget_tabsClosable(tw) && XTabBar_tabsClosable(XTabWidget_tabBar(tw)),
              "tabsClosable 双向同步");
    XTabWidget_setMovable(tw, true);
    tb_expect(XTabWidget_isMovable(tw), "setMovable 生效");

    XTabWidget_delete_base(tw);
    XTabBar_delete_base(bar);

    {
        int failures = tb_failures;
        tb_failures = 0;
        tb_curChanged = 0;
        if (failures == 0) {
            fprintf(stderr, "XTabBar/XTabWidget test: PASS\n");
            return true;
        }
        fprintf(stderr, "XTabBar/XTabWidget test: %d assertion(s) failed\n", failures);
        return false;
    }
}
