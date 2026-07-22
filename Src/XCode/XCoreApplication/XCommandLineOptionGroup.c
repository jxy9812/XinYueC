#include "XCommandLineOptionGroup.h"
#include "XMemory.h"
#include "XString.h"

XCommandLineOptionGroup* XCommandLineOptionGroup_create(const XString* name,
    const XString* description,
    bool isExclusive)
{
    XCommandLineOptionGroup* group = XMalloc_System(sizeof(XCommandLineOptionGroup));
    if (!group) return NULL;

    group->name = name ? XString_create_copy(name) : NULL;
    group->description = description ? XString_create_copy(description) : NULL;
    group->isExclusive = isExclusive;
    group->options = XVector_create(sizeof(XCommandLineOption*));

    if (!group->options) {
        XString_delete_base(group->name);
        XString_delete_base(group->description);
        XFree_System(group);
        return NULL;
    }

    return group;
}

void XCommandLineOptionGroup_delete(XCommandLineOptionGroup* group)
{
    if (!group) return;
    XString_delete_base(group->name);
    XString_delete_base(group->description);
    XVector_delete_base(group->options);
    XFree_System(group);
}

void XCommandLineOptionGroup_addOption(XCommandLineOptionGroup* group,
    const XCommandLineOption* option)
{
    if (!group || !option) return;
    XVector_push_back_1_base(group->options, &option);
}

size_t XCommandLineOptionGroup_optionCount(const XCommandLineOptionGroup* group)
{
    return group ? XVector_size_base(group->options) : 0;
}

const XCommandLineOption* XCommandLineOptionGroup_optionAt(const XCommandLineOptionGroup* group, size_t index)
{
    if (!group || index >= XVector_size_base(group->options)) return NULL;
    XCommandLineOption** opt = (XCommandLineOption**)XVector_at_base(group->options, index);
    return opt ? *opt : NULL;
}
