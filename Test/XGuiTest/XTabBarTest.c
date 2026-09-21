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
    XTabBar_addTab_2(bar, "Tab1");
    XTabBar_addTab_2(bar, "Tab2");
    XTabBar_insertTab_2(bar, 0, "First");
    tb_expect(XTabBar_count(bar) == 3, "插入后 3 项");
    tb_expect(strcmp(XTabBar_tabText_2(bar, 0), "First") == 0, "插到最前");
    tb_expect(XTabBar_currentIndex(bar) == 0, "首次添加当前=0");

    /* 2. setCurrentIndex + 信号。 */
    tb_curChanged = 0;
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_currentChanged_signal(bar, 0),
                      tb_curChangedSlot);
    XTabBar_setCurrentIndex(bar, 2);
    tb_expect(XTabBar_currentIndex(bar) == 2 && tb_curChanged == 1,
              "setCurrentIndex 发射一次");
    XTabBar_setCurrentIndex(bar, 2);
    tb_expect(tb_curChanged == 1, "同值不重复发射");

    /* 3. setTabText/isTabEnabled。 */
    XTabBar_setTabText_2(bar, 0, "改文本");
    tb_expect(strcmp(XTabBar_tabText_2(bar, 0), "改文本") == 0, "setTabText 生效");
    XTabBar_setTabEnabled(bar, 0, false);
    tb_expect(!XTabBar_isTabEnabled(bar, 0), "setTabEnabled(false) 生效");
    XTabBar_setTabEnabled(bar, 0, true);

    /* 4. removeTab 与索引收敛。 */
    XTabBar_removeTab(bar, 0);
    tb_expect(XTabBar_count(bar) == 2 && XTabBar_currentIndex(bar) <= 1,
              "removeTab 后索引收敛");

    /* 5. TabWidget：addTab/indexOf/currentWidget。 */
    tb_curChanged = 0;
    XTabWidget_addTab_2(tw, p1, "页一");
    XTabWidget_addTab_2(tw, p2, "页二");
    tb_expect(XTabWidget_count(tw) == 2, "TabWidget 2 页");
    tb_expect(XTabWidget_indexOf(tw, p2) == 1, "indexOf 命中");
    tb_expect(XTabWidget_currentWidget(tw) == p1, "默认显示页一");
    XTabWidget_setCurrentIndex(tw, 1);
    tb_expect(XTabWidget_currentWidget(tw) == p2, "切页后当前页正确");
    XTabWidget_setTabText_2(tw, 1, "页二改");
    tb_expect(strcmp(XTabWidget_tabText_2(tw, 1), "页二改") == 0,
              "setTabText 转发页签条");

    /* 6. tabsClosable/movable 转发。 */
    XTabWidget_setTabsClosable(tw, true);
    tb_expect(XTabWidget_tabsClosable(tw) && XTabBar_tabsClosable(XTabWidget_tabBar(tw)),
              "tabsClosable 双向同步");
    XTabWidget_setMovable(tw, true);
    tb_expect(XTabWidget_isMovable(tw), "setMovable 生效");

    XTabWidget_delete_base(tw);
    /* 6. Task 2.2：外观/几何/项属性。 */
    XTabBar_setDocumentMode(bar, true);
    tb_expect(XTabBar_documentMode(bar), "documentMode");
    XTabBar_setElideMode(bar, 2);
    tb_expect(XTabBar_elideMode(bar) == 2, "elideMode");
    XTabBar_setExpanding(bar, false);
    tb_expect(!XTabBar_expanding(bar), "expanding");
    XTabBar_setUsesScrollButtons(bar, true);
    tb_expect(XTabBar_usesScrollButtons(bar), "usesScrollButtons");
    XTabBar_setDrawBase(bar, false);
    tb_expect(!XTabBar_drawBase(bar), "drawBase");
    XTabBar_setTabTextColor(bar, 0, 0xFF112233u);
    tb_expect(XTabBar_tabTextColor(bar, 0) == 0xFF112233u,
              "tabTextColor");
    XTabBar_setTabIcon_2(bar, 0, "icon.png");
    tb_expect(XStrcmp(XTabBar_tabIcon_2(bar, 0), "icon.png") == 0,
              "tabIcon");
    XTabBar_setTabToolTip_2(bar, 0, "提示");
    tb_expect(XStrcmp(XTabBar_tabToolTip_2(bar, 0), "提示") == 0,
              "tabToolTip");
    XTabBar_setTabData_2(bar, 0, "data1");
    tb_expect(XStrcmp(XTabBar_tabData_2(bar, 0), "data1") == 0,
              "tabData");
    XTabBar_setTabVisible(bar, 1, false);
    tb_expect(!XTabBar_isTabVisible(bar, 1), "tabVisible");
    {
        XRect r;
        XPoint p;
        tb_expect(XTabBar_tabRect(bar, 0, &r) && r.width > 0 &&
                  r.height > 0, "tabRect");
        XPoint_init(&p, r.x + 1, r.y + 1);
        tb_expect(XTabBar_tabAt(bar, &p) == 0, "tabAt 命中");
        tb_expect(XTabBar_tabIndexAt(bar, r.x + 1, r.y + 1) == 0,
                  "tabIndexAt");
        tb_expect(XTabBar_tabWidth(bar) > 0 && XTabBar_tabHeight(bar) > 0,
                  "tabWidth/Height");
    }
    XTabBar_moveTab(bar, 0, 1);
    tb_expect(XStrcmp(XTabBar_tabText_2(bar, 1), "Tab1") == 0,
              "moveTab 后 0→1 文本正确");
    tb_expect(XTabBar_tabData_2(bar, 1) != NULL &&
              XStrcmp(XTabBar_tabData_2(bar, 1), "data1") == 0,
              "moveTab 数据随行");
    /* §8.0g11：elide 模式通道 + 按住连发状态机（此时 elideMode 已被
       上文改为 2；仅验证 setter 回读，默认值断言须在新对象上）。 */
    XTabBar_setElideMode(bar, 0);
    tb_expect(XTabBar_elideMode(bar) == 0, "elideMode 设为无省略");
    XTabBar_setElideMode(bar, 1);
    tb_expect(XTabBar_elideMode(bar) == 1, "elideMode 设回右省略");
    {
        XTabBar* fresh = XTabBar_create(NULL, 0);
        tb_expect(fresh != NULL && XTabBar_elideMode(fresh) == 1,
                  "elideMode 新对象默认右省略");
        tb_expect(fresh != NULL && fresh->m_scrollRepeatDir == 0 &&
                  fresh->m_scrollRepeatTimer == XTIMER_INVALID_ID &&
                  fresh->m_repeatSkip == 0,
                  "连发状态初始为无");
        if (fresh) XTabBar_delete_base(fresh);
    }
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
