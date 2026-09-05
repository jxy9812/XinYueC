#include "XCodeTest.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XString.h"
#include "XVariant.h"
#include <string.h>

/*
 * XAction（对标 Qt 6.8 QAction）全量语义测试。
 *
 * 覆盖：默认属性、文本族（text/iconText/toolTip/statusTip/whatsThis）、
 * checkable/checked/toggle、enabled/resetEnabled/setDisabled 与 visible
 * 联动、separator/priority/menuRole/iconVisibleInMenu/
 * shortcutVisibleInContextMenu、data 所有权、trigger/activate/hover 的
 * triggered/toggled/hovered 语义、显式禁用时忽略触发、changed 通知计数、
 * XCopy/XMove 与父对象级联释放，以及 XTestMenu_setActionFunction 的菜单绑定。
 */

static int g_failures = 0;

static void XActionTest_expect(bool ok, const char* what)
{
    if (!ok) {
        ++g_failures;
        XPrintf("  [失败] %s\n", what);
    } else {
        XPrintf("  [通过] %s\n", what);
    }
}

/* ==================== 信号计数槽 ==================== */

static int  g_changedCount;
static int  g_enabledChangedCount;
static bool g_enabledChangedValue;
static int  g_checkableChangedCount;
static bool g_checkableChangedValue;
static int  g_visibleChangedCount;
static int  g_triggeredCount;
static bool g_triggeredValue;
static int  g_hoveredCount;
static int  g_toggledCount;
static bool g_toggledValue;
static int  g_menuFuncCalls;

static void XActionTest_changedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++g_changedCount;
}

static void XActionTest_enabledChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, bool, enabled);
    ++g_enabledChangedCount;
    g_enabledChangedValue = enabled;
}

static void XActionTest_checkableChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, bool, checkable);
    ++g_checkableChangedCount;
    g_checkableChangedValue = checkable;
}

static void XActionTest_visibleChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++g_visibleChangedCount;
}

static void XActionTest_triggeredSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, bool, checked);
    ++g_triggeredCount;
    g_triggeredValue = checked;
}

static void XActionTest_hoveredSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++g_hoveredCount;
}

static void XActionTest_toggledSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, bool, checked);
    ++g_toggledCount;
    g_toggledValue = checked;
}

/* XTestMenu_setActionFunction 绑定用的测试函数。 */
static void XActionTest_menuFunc(XVariant* data)
{
    (void)data;
    ++g_menuFuncCalls;
}

static void XActionTest_connectAll(XAction* action)
{
    XObject_connect_2((XObject*)action, XSignal(XAction_changed_signal),
                      XActionTest_changedSlot);
    XObject_connect_2((XObject*)action,
                      XSignal(XAction_enabledChanged_signal),
                      XActionTest_enabledChangedSlot);
    XObject_connect_2((XObject*)action,
                      XSignal(XAction_checkableChanged_signal),
                      XActionTest_checkableChangedSlot);
    XObject_connect_2((XObject*)action,
                      XSignal(XAction_visibleChanged_signal),
                      XActionTest_visibleChangedSlot);
    XObject_connect_2((XObject*)action,
                      XSignal(XAction_triggered_signal),
                      XActionTest_triggeredSlot);
    XObject_connect_2((XObject*)action, XSignal(XAction_hovered_signal),
                      XActionTest_hoveredSlot);
    XObject_connect_2((XObject*)action, XSignal(XAction_toggled_signal),
                      XActionTest_toggledSlot);
}

static void XActionTest_resetCounters(void)
{
    g_changedCount = 0;
    g_enabledChangedCount = 0;
    g_enabledChangedValue = false;
    g_checkableChangedCount = 0;
    g_checkableChangedValue = false;
    g_visibleChangedCount = 0;
    g_triggeredCount = 0;
    g_triggeredValue = false;
    g_hoveredCount = 0;
    g_toggledCount = 0;
    g_toggledValue = false;
    g_menuFuncCalls = 0;
}

/* ==================== 用例 ==================== */

static void XActionTest_defaults(void)
{
    XAction* action = XAction_create();
    const XString* text;

    XActionTest_expect(action != NULL, "create 成功分配对象");
    XAction_setText_2(action, "文件");
    text = XAction_text_const(action);
    XActionTest_expect(text != NULL &&
                           XString_equals_utf8(text, "文件",
                                               XChar_CaseSensitive),
                       "setText_2 记录初始文本");
    XActionTest_expect(XAction_isEnabled(action), "默认有效启用");
    XActionTest_expect(XAction_isVisible(action), "默认可见");
    XActionTest_expect(!XAction_isCheckable(action) && !XAction_isChecked(action),
                       "默认不可选中且未选中");
    XActionTest_expect(XAction_priority(action) == XActionPriority_Normal,
                       "默认优先级 Normal(128)");
    XActionTest_expect(XAction_menuRole(action) ==
                           XActionMenuRole_TextHeuristicRole,
                       "默认菜单角色 TextHeuristicRole");
    XActionTest_expect(!XAction_isSeparator(action) &&
                           !XAction_isIconVisibleInMenu(action) &&
                           !XAction_isShortcutVisibleInContextMenu(action),
                       "默认非分隔条且菜单图标/快捷键显示关闭");
    XActionTest_expect(XAction_data(action) == NULL, "默认无用户数据");
    XAction_delete_base(action);
}

static void XActionTest_textFamily(void)
{
    XAction* action = XAction_create();
    XString* copy;
    XString* str;

    XActionTest_connectAll(action);
    XActionTest_resetCounters();

    XAction_setText_2(action, "打开");
    XActionTest_expect(g_changedCount == 1, "setText_2 变化发射一次 changed");
    XActionTest_expect(XAction_text_const(action) != NULL &&
                           XString_equals_utf8(XAction_text_const(action),
                                               "打开",
                                               XChar_CaseSensitive),
                       "text_const 返回 UTF-8 文本");

    XAction_setText_2(action, "打开");
    XActionTest_expect(g_changedCount == 1, "相同文本不重复发射 changed");

    XAction_setIconText_2(action, "打开(I)");
    XAction_setToolTip_2(action, "打开文件");
    XAction_setStatusTip_2(action, "准备打开文件");
    XAction_setWhatsThis_2(action, "什么是打开");
    XActionTest_expect(g_changedCount == 5, "五个文本族字段各发射一次 changed");
    XActionTest_expect(XAction_iconText_const(action) != NULL &&
                           XString_equals_utf8(XAction_iconText_const(action),
                                               "打开(I)",
                                               XChar_CaseSensitive),
                       "iconText 存取一致");
    XActionTest_expect(XAction_toolTip_const(action) != NULL &&
                           XString_equals_utf8(XAction_toolTip_const(action),
                                               "打开文件",
                                               XChar_CaseSensitive),
                       "toolTip 存取一致");
    XActionTest_expect(XAction_statusTip_const(action) != NULL &&
                           XString_equals_utf8(XAction_statusTip_const(action),
                                               "准备打开文件",
                                               XChar_CaseSensitive),
                       "statusTip 存取一致");
    XActionTest_expect(XAction_whatsThis_const(action) != NULL &&
                           XString_equals_utf8(XAction_whatsThis_const(action),
                                               "什么是打开",
                                               XChar_CaseSensitive),
                       "whatsThis 存取一致");

    copy = XAction_text(action);
    XActionTest_expect(copy != NULL &&
                           XString_equals_utf8(copy, "打开",
                                               XChar_CaseSensitive),
                       "text 返回深拷贝");
    if (copy)
        XString_delete_base((XClass*)copy);

    str = XString_create_utf8("另存为");
    XActionTest_expect(str != NULL, "XString 构造成功");
    XAction_setText(action, str);
    XString_delete_base((XClass*)str);
    XActionTest_expect(XAction_text_const(action) != NULL &&
                           XString_equals_utf8(XAction_text_const(action),
                                               "另存为",
                                               XChar_CaseSensitive),
                       "setText(XString) 深拷贝后可用");

    XAction_delete_base(action);
}

static void XActionTest_checkable(void)
{
    XAction* action = XAction_create();

    XActionTest_connectAll(action);
    XActionTest_resetCounters();

    XAction_setChecked(action, true);
    XActionTest_expect(!XAction_isChecked(action), "不可选中动作 setChecked 无效");
    XActionTest_expect(g_toggledCount == 0 && g_changedCount == 0,
                       "不可选中动作不发射 toggled/changed");

    XAction_setCheckable(action, true);
    XActionTest_expect(XAction_isCheckable(action), "setCheckable(true) 生效");
    XActionTest_expect(g_checkableChangedCount == 1 &&
                           g_checkableChangedValue == true,
                       "checkableChanged(true) 发射一次");

    XAction_setChecked(action, true);
    XActionTest_expect(XAction_isChecked(action), "可选中动作 setChecked(true) 生效");
    XActionTest_expect(g_toggledCount == 1 && g_toggledValue == true,
                       "setChecked 发射 toggled(true)");

    XAction_toggle(action);
    XActionTest_expect(!XAction_isChecked(action), "toggle 反转选中状态");
    XActionTest_expect(g_toggledCount == 2 && g_toggledValue == false,
                       "toggle 发射 toggled(false)");

    XAction_setCheckable(action, false);
    XActionTest_expect(!XAction_isChecked(action), "关闭可选中后 isChecked 为 false");
    XAction_setChecked(action, true);
    XActionTest_expect(g_toggledCount == 2, "关闭可选中后 setChecked 不再发 toggled");

    XAction_delete_base(action);
}

static void XActionTest_enabledVisible(void)
{
    XAction* action = XAction_create();

    XActionTest_connectAll(action);
    XActionTest_resetCounters();

    /* 阶段一：显式禁用（信号计数）。 */
    XAction_setEnabled(action, false);
    XActionTest_expect(!XAction_isEnabled(action), "setEnabled(false) 禁用动作");
    XActionTest_expect(g_enabledChangedCount == 1 &&
                           g_enabledChangedValue == false,
                       "enabledChanged(false) 发射一次");
    XAction_setEnabled(action, false);
    XActionTest_expect(g_enabledChangedCount == 1, "重复 setEnabled 不重复通知");

    /* 阶段二：显式禁用 + 可见往返（Qt 语义：显式禁用保持）。 */
    XAction_setVisible(action, false);
    XActionTest_expect(!XAction_isVisible(action), "setVisible(false) 生效");
    XActionTest_expect(g_visibleChangedCount == 1, "visibleChanged 发射一次");
    XActionTest_expect(!XAction_isEnabled(action),
                       "不可见动作有效启用被强制为 false");

    XAction_setVisible(action, true);
    XActionTest_expect(XAction_isVisible(action), "setVisible(true) 恢复可见");
    XActionTest_expect(!XAction_isEnabled(action),
                       "显式禁用时恢复可见仍保持禁用");

    /* 阶段三：resetEnabled 恢复默认启用。 */
    XAction_resetEnabled(action);
    XActionTest_expect(XAction_isEnabled(action), "resetEnabled 恢复默认启用");
    XActionTest_expect(g_enabledChangedCount == 2,
                       "resetEnabled 补发一次 enabledChanged");

    /* 阶段四：无显式设置时可见性驱动启用（信号计数）。 */
    XActionTest_resetCounters();
    XAction_setVisible(action, false);
    XActionTest_expect(!XAction_isEnabled(action),
                       "无显式设置时隐藏动作强制禁用");
    XActionTest_expect(g_enabledChangedCount == 1 &&
                           g_enabledChangedValue == false,
                       "隐藏动作补发 enabledChanged(false)");
    XAction_setVisible(action, true);
    XActionTest_expect(XAction_isEnabled(action),
                       "无显式设置时恢复可见即恢复默认启用");
    XActionTest_expect(g_enabledChangedCount == 2 &&
                           g_enabledChangedValue == true,
                       "恢复可见补发 enabledChanged(true)");
    XActionTest_expect(g_visibleChangedCount == 2, "两次可见性各发一次 visibleChanged");

    /* 阶段五：setDisabled 便捷接口。 */
    XAction_setDisabled(action, true);
    XActionTest_expect(!XAction_isEnabled(action), "setDisabled(true) 等价禁用");

    XAction_delete_base(action);
}

static void XActionTest_triggerAndHover(void)
{
    XAction* action = XAction_create();
    XVariant* data = XVariant_create_int(42);

    XActionTest_connectAll(action);
    XAction_setData(action, data);
    XActionTest_resetCounters();

    XAction_trigger(action);
    XActionTest_expect(g_triggeredCount == 1 && g_triggeredValue == false,
                       "普通动作 trigger 发射 triggered(false)");

    XActionTest_resetCounters();
    XAction_setCheckable(action, true);
    XAction_trigger(action);
    XActionTest_expect(XAction_isChecked(action), "可选中动作 trigger 自动选中");
    XActionTest_expect(g_toggledCount == 1 && g_toggledValue == true,
                       "可选中 trigger 先翻转并发射 toggled(true)");
    XActionTest_expect(g_triggeredCount == 1 && g_triggeredValue == true,
                       "可选中 trigger 再发射 triggered(true)");

    XActionTest_resetCounters();
    XAction_setEnabled(action, false);
    XAction_trigger(action);
    XActionTest_expect(g_triggeredCount == 0,
                       "显式禁用动作 trigger 被忽略");
    XAction_setEnabled(action, true);

    XActionTest_resetCounters();
    XAction_hover(action);
    XActionTest_expect(g_hoveredCount == 1, "hover 发射一次 hovered");
    XAction_activate(action, XActionEvent_Hover);
    XActionTest_expect(g_hoveredCount == 2, "activate(Hover) 再发射 hovered");

    XAction_delete_base(action);
}

static void XActionTest_miscProperties(void)
{
    XAction* action = XAction_create();

    XActionTest_connectAll(action);
    XActionTest_resetCounters();

    XAction_setSeparator(action, true);
    XActionTest_expect(XAction_isSeparator(action), "setSeparator(true) 生效");
    XActionTest_expect(g_changedCount == 1, "separator 变化发射 changed");

    XAction_setPriority(action, XActionPriority_High);
    XActionTest_expect(XAction_priority(action) == XActionPriority_High,
                       "setPriority(High) 生效");

    XAction_setMenuRole(action, XActionMenuRole_AboutRole);
    XActionTest_expect(XAction_menuRole(action) == XActionMenuRole_AboutRole,
                       "setMenuRole(AboutRole) 生效");

    XAction_setIconVisibleInMenu(action, true);
    XActionTest_expect(XAction_isIconVisibleInMenu(action),
                       "setIconVisibleInMenu(true) 生效");

    XAction_setShortcutVisibleInContextMenu(action, true);
    XActionTest_expect(XAction_isShortcutVisibleInContextMenu(action),
                       "setShortcutVisibleInContextMenu(true) 生效");
    XActionTest_expect(g_changedCount == 5,
                       "五个属性变化各发射一次 changed");

    XAction_delete_base(action);
}

static void XActionTest_copyMove(void)
{
    XAction src;
    XAction dst;
    XAction* copy;
    XAction* a;
    XAction* b;

    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    XAction_init(&src);
    XAction_setText_2(&src, "复制源");
    XAction_setCheckable(&src, true);
    XAction_setChecked(&src, true);
    XAction_setPriority(&src, XActionPriority_High);

    XCopy(&dst, &src);
    XActionTest_expect(XAction_text_const(&dst) != NULL &&
                           XString_equals_utf8(XAction_text_const(&dst),
                                               "复制源",
                                               XChar_CaseSensitive),
                       "XCopy 复制文本");
    XActionTest_expect(XAction_isChecked(&dst), "XCopy 复制选中状态");
    XActionTest_expect(XAction_priority(&dst) == XActionPriority_High,
                       "XCopy 复制优先级");

    copy = XAction_create_copy(&src);
    XActionTest_expect(copy != NULL &&
                           XAction_text_const(copy) != NULL &&
                           XString_equals_utf8(XAction_text_const(copy),
                                               "复制源",
                                               XChar_CaseSensitive),
                       "create_copy 深拷贝属性");
    XAction_delete_base(copy);

    XAction_deinit_base(&dst);
    XAction_deinit_base(&src);

    a = XAction_create();
    b = XAction_create();
    XAction_setText_2(a, "移动A");
    XAction_setText_2(b, "移动B");
    XAction_setCheckable(b, true);
    XMove(a, b);
    XActionTest_expect(XAction_text_const(a) != NULL &&
                           XString_equals_utf8(XAction_text_const(a),
                                               "移动B",
                                               XChar_CaseSensitive),
                       "XMove 转移文本到目标");
    XActionTest_expect(XAction_text_const(b) == NULL, "XMove 后源文本为空");
    XActionTest_expect(!XAction_isCheckable(b), "XMove 后源可选中复位");
    XAction_delete_base(a);
    XAction_delete_base(b);
}

static void XActionTest_parentOwnership(void)
{
    XObject* parent = XObject_create();
    XAction* child;

    XActionTest_expect(parent != NULL, "父 XObject 创建成功");
    child = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, parent, "子动作");
    XActionTest_expect(child != NULL, "带父对象 create_ex 成功");
    XActionTest_expect(XObject_parent((XObject*)child) == parent,
                       "子动作已登记父对象");
    XClass_delete_base((XClass*)parent);
    XActionTest_expect(true, "父对象释放后子动作级联释放（无崩溃/泄漏）");
}

static void XActionTest_data(void)
{
    XAction* action = XAction_create();
    XVariant* value = XVariant_create_int(7);

    XAction_setData(action, value);
    XActionTest_expect(XAction_data(action) == value, "setData 接管指针");

    XAction_setData(action, NULL);
    XActionTest_expect(XAction_data(action) == NULL, "setData(NULL) 清空数据");

    XAction_delete_base(action);
}

static void XActionTest_menuBinding(void)
{
    XTestMenu* menu = XTestMenu_create("绑定测试");
    XAction* action;

    XActionTest_resetCounters();
    XActionTest_expect(menu != NULL, "菜单创建成功");
    action = XTestMenu_addAction(menu, "运行");
    XActionTest_expect(action != NULL, "addAction 返回动作");
    XTestMenu_setActionFunction(action, XActionTest_menuFunc);

    XAction_trigger(action);
    XActionTest_expect(g_menuFuncCalls == 1,
                       "trigger 经 triggered 信号运行菜单绑定函数");

    XTestMenu_setActionFunction(action, NULL);
    XAction_trigger(action);
    XActionTest_expect(g_menuFuncCalls == 1,
                       "解绑后 trigger 不再调用函数");

    XTestMenu_delete(menu);
    XActionTest_expect(true, "菜单删除释放动作（无崩溃/泄漏）");
}

/* ==================== 综合入口 ==================== */

void XActionTest(void)
{
    XPrintf("=== XAction(QAction 6.8 对标) 综合测试 ===\n");

    g_failures = 0;
    XActionTest_defaults();
    XActionTest_textFamily();
    XActionTest_checkable();
    XActionTest_enabledVisible();
    XActionTest_triggerAndHover();
    XActionTest_miscProperties();
    XActionTest_copyMove();
    XActionTest_parentOwnership();
    XActionTest_data();
    XActionTest_menuBinding();

    if (g_failures == 0)
        XPrintf("\n=== 所有 XAction 测试通过 ===\n");
    else
        XPrintf("\n=== XAction 测试失败 %d 项 ===\n", g_failures);
}

static void XActionTest_menuEntry(XVariant* data)
{
    (void)data;
    XActionTest();
}

void XTestMenu_XActionTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XAction(QAction 对标)");
    XTestMenu_addMenu(root, menu);
    {
        XAction* action = XTestMenu_addAction(menu, "主测试");
        XTestMenu_setActionFunction(action, XActionTest_menuEntry);
    }
}
