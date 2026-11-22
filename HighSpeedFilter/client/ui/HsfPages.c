/**
 * @file       HsfPages.c
 * @brief      页面注册表实现：页名表 + 导航树。
 */
#include "HsfPages.h"

/** 页名表（标题栏/状态栏文本同源；UTF-8）。 */
static const char* const kHsfPageNames[HSF_PAGE_COUNT] = {
    "主界面",
    "菜单",
    "设备",
    "数字量",
    "模拟量",
    /* [2026-11-12 用户口径] 页名按工艺用途叫「搅拌泵」（设备**类别**仍是
     * 变频器，首页类型列口径不变）——对齐 Python pages_registry.py:42。 */
    "搅拌泵",
    "流量计",
    "电动球阀",
    "数据",
    "历史数据",
    "日志",
    "报警",
    "设置",
    "本地设置",
    "远端设置",
    "网络设置"
};

/**
 * 导航树（顺序=面板显示顺序；childOf=父节点下标）。
 * @details 主界面为顶层叶子；监视/数据/设置为可展开分类，其子项为叶子。
 *          [2026-11-13 三页合并] 设置分类下只剩三个子项：本地设置（内含
 *          悬浮窗/窗口与屏幕两分区）/远端设置/网络设置——原「悬浮窗设置」
 *          「窗口设置」两个独立页号（16/17）已作废。节点数 17→15。
 */
static const HsfNavNode kHsfNavTree[] = {
    { "主界面", HSF_PAGE_HOME,             -1, false },
    { "设备",   HSF_PAGE_MONITOR_MENU,     -1, true  },
    { "数字量", HSF_PAGE_MONITOR_DIGITAL,   1, false },
    { "模拟量", HSF_PAGE_MONITOR_ANALOG,    1, false },
    { "搅拌泵", HSF_PAGE_MONITOR_VFD,       1, false },
    { "流量计", HSF_PAGE_MONITOR_FLOWMETER, 1, false },
    { "电动球阀", HSF_PAGE_MONITOR_VALVE,   1, false },
    { "数据",   HSF_PAGE_DATA_MENU,         -1, true  },
    { "历史数据", HSF_PAGE_DATA_HISTORY,     7, false },
    { "日志",   HSF_PAGE_DATA_LOG,           7, false },
    { "报警",   HSF_PAGE_DATA_ALARM,         7, false },
    { "设置",   HSF_PAGE_SETTINGS_MENU,      -1, true  },
    { "本地设置", HSF_PAGE_SETTINGS_LOCAL,    11, false },
    { "远端设置", HSF_PAGE_SETTINGS_REMOTE,   11, false },
    { "网络设置", HSF_PAGE_SETTINGS_NETWORK,  11, false }
};

const char* HsfPages_name(int index)
{
    if (index < 0 || index >= HSF_PAGE_COUNT)
        return "?";
    return kHsfPageNames[index];
}

const HsfNavNode* HsfPages_navTree(int* countOut)
{
    if (countOut)
        *countOut = (int)(sizeof(kHsfNavTree) / sizeof(kHsfNavTree[0]));
    return kHsfNavTree;
}

int HsfPages_categoryOf(const HsfNavNode* tree, int count, int index)
{
    if (!tree || index < 0 || index >= count)
        return -1;
    if (tree[index].childOf >= 0)
        return tree[index].childOf;
    /* 顶层节点：可展开者自身即分类，否则无分类归属。 */
    return tree[index].expandable ? index : -1;
}
