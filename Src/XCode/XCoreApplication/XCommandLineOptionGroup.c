#include "XCommandLineOptionGroup.h"
//#include "XCommandLineParser.h"
#include "XMemory.h"

XCommandLineOptionGroup* XCommandLineOptionGroup_create(const char* name,
    const char* description,
    bool isExclusive) {
    // 检查内存分配
    XCommandLineOptionGroup* group = XMalloc_System(sizeof(XCommandLineOptionGroup));
    if (!group) return NULL;

    // 初始化成员变量
    group->name = name;
    group->description = description;
    group->isExclusive = isExclusive;
    group->options = XVector_create(sizeof(XCommandLineOption*));

    // 检查向量创建是否成功
    if (!group->options) {
        XFree_System(group);
        return NULL;
    }

    return group;
}

void XCommandLineOptionGroup_delete(XCommandLineOptionGroup* group) {
    if (!group) return;

    // 释放向量资源（不释放选项本身，因为选项由解析器管理）
    XVector_delete_base(group->options);
    XFree_System(group);
}

void XCommandLineOptionGroup_addOption(XCommandLineOptionGroup* group,
    const XCommandLineOption* option) {
    if (!group || !option) return;
    XVector_push_back_1_base(group->options, &option);
}
