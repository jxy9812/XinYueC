/**
 * @file       HsfPages.c
 * @brief      页面注册表实现：页名表 + 导航树。
 */
#include "HsfPages.h"

/** 页名表（标题栏/状态栏文本同源；UTF-8）。 */
static const char* const kHsfPageNames[HSF_PAGE_COUNT] = {
    "主界面",
    "菜单",
    "监视",
    "数字量",
    "模拟量",
    "变频器",
    "数据",
    "历史数据",
    "日志",
    "报警",
    "设置",
    "本地设置",
    "远端设置",
    "网络设置",
    "悬浮窗设置",
    "窗口设置"
};

/**
 * 导航树（顺序=面板显示顺序；childOf=父节点下标）。
 * @details 主界面为顶层叶子；监视/数据/设置为可展开分类，其子项为叶子。
 */
static const HsfNavNode kHsfNavTree[] = {
    { "主界面", HSF_PAGE_HOME,             -1, false },
    { "监视",   HSF_PAGE_MONITOR_MENU,     -1, true  },
    { "数字量", HSF_PAGE_MONITOR_DIGITAL,   1, false },
    { "模拟量", HSF_PAGE_MONITOR_ANALOG,    1, false },
    { "变频器", HSF_PAGE_MONITOR_VFD,       1, false },
    { "数据",   HSF_PAGE_DATA_MENU,         -1, true  },
    { "历史数据", HSF_PAGE_DATA_HISTORY,     5, false },
    { "日志",   HSF_PAGE_DATA_LOG,           5, false },
    { "报警",   HSF_PAGE_DATA_ALARM,         5, false },
    { "设置",   HSF_PAGE_SETTINGS_MENU,      -1, true  },
    { "本地设置", HSF_PAGE_SETTINGS_LOCAL,    9, false },
    { "远端设置", HSF_PAGE_SETTINGS_REMOTE,   9, false },
    { "网络设置", HSF_PAGE_SETTINGS_NETWORK,  9, false },
    { "悬浮窗设置", HSF_PAGE_SETTINGS_OVERLAY, 9, false },
    { "窗口设置", HSF_PAGE_SETTINGS_WINDOW,   9, false }
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
