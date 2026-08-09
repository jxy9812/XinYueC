#include "XCommandLineOption.h"
#include "XMemory.h"
#include "XString.h"
#include "XStringList.h"
#include <string.h>

/* ==================== XCommandLineOption 实现（对标 QCommandLineOption） ==================== */

static char* xStrDup(const char* str)
{
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = XMalloc_System(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

XCommandLineOption* XCommandLineOption_create(const char* name)
{
    XCommandLineOption* opt = XMalloc_System(sizeof(XCommandLineOption));
    if (!opt) return NULL;

    opt->names = XStringList_create();
    if (!opt->names) { XFree_System(opt); return NULL; }
    if (name) {
        XStringList_push_back_utf8(opt->names, name);
    }

    opt->description = NULL;
    opt->valueName = NULL;
    opt->defaultValues = XStringList_create();
    opt->flags = 0;
    return opt;
}

XCommandLineOption* XCommandLineOption_createWithNames(const XStringList* names)
{
    XCommandLineOption* opt = XMalloc_System(sizeof(XCommandLineOption));
    if (!opt) return NULL;

    opt->names = XStringList_create();
    if (!opt->names) { XFree_System(opt); return NULL; }
    if (names) {
        for (size_t i = 0; i < XStringList_size_base(names); ++i) {
            const XString* s = XStringList_at_base(names, i);
            if (s) {
                XString* copy = XString_create_copy(s);
                if (copy) {
                    XStringList_push_back_move_base(opt->names, copy);
                    XString_delete_base(copy);
                }
            }
        }
    }

    opt->description = NULL;
    opt->valueName = NULL;
    opt->defaultValues = XStringList_create();
    opt->flags = 0;
    return opt;
}

XCommandLineOption* XCommandLineOption_createFull(const char* name,
    const char* description, const char* valueName, const char* defaultValue)
{
    XCommandLineOption* opt = XCommandLineOption_create(name);
    if (!opt) return NULL;
    if (description) {
        opt->description = XString_create_utf8(description);
    }
    if (valueName) {
        opt->valueName = XString_create_utf8(valueName);
    }
    if (defaultValue) {
        XStringList_push_back_utf8(opt->defaultValues, defaultValue);
    }
    return opt;
}

XCommandLineOption* XCommandLineOption_createFullWithNames(const XStringList* names,
    const char* description, const char* valueName, const char* defaultValue)
{
    XCommandLineOption* opt = XCommandLineOption_createWithNames(names);
    if (!opt) return NULL;
    if (description) {
        opt->description = XString_create_utf8(description);
    }
    if (valueName) {
        opt->valueName = XString_create_utf8(valueName);
    }
    if (defaultValue) {
        XStringList_push_back_utf8(opt->defaultValues, defaultValue);
    }
    return opt;
}


void XCommandLineOption_addName(XCommandLineOption* option, const char* name)
{
    if (!option || !name) return;
    XStringList_push_back_utf8(option->names, name);
}

void XCommandLineOption_delete(XCommandLineOption* option)
{
    if (!option) return;
    XStringList_delete_base(option->names);
    XString_delete_base(option->description);
    XString_delete_base(option->valueName);
    XStringList_delete_base(option->defaultValues);
    XFree_System(option);
}

const XStringList* XCommandLineOption_names(const XCommandLineOption* option)
{
    return option ? option->names : NULL;
}

void XCommandLineOption_setValueName(XCommandLineOption* option, const char* name)
{
    if (!option) return;
    XString_delete_base(option->valueName);
    option->valueName = name ? XString_create_utf8(name) : NULL;
}

const char* XCommandLineOption_valueName(const XCommandLineOption* option)
{
    return (option && option->valueName) ? XString_toUtf8(option->valueName) : NULL;
}

void XCommandLineOption_setDescription(XCommandLineOption* option, const char* description)
{
    if (!option) return;
    XString_delete_base(option->description);
    option->description = description ? XString_create_utf8(description) : NULL;
}

const char* XCommandLineOption_description(const XCommandLineOption* option)
{
    return (option && option->description) ? XString_toUtf8(option->description) : NULL;
}

void XCommandLineOption_setDefaultValue(XCommandLineOption* option, const char* value)
{
    if (!option) return;
    XStringList_clear_base(option->defaultValues);
    if (value) {
        XStringList_push_back_utf8(option->defaultValues, value);
    }
}

void XCommandLineOption_setDefaultValues(XCommandLineOption* option, const XStringList* values)
{
    if (!option) return;
    XStringList_clear_base(option->defaultValues);
    if (values) {
        for (size_t i = 0; i < XStringList_size_base(values); ++i) {
            const XString* s = XStringList_at_base(values, i);
            if (s) {
                XString* copy = XString_create_copy(s);
                if (copy) {
                    XStringList_push_back_move_base(option->defaultValues, copy);
                    XString_delete_base(copy);
                }
            }
        }
    }
}

const XStringList* XCommandLineOption_defaultValues(const XCommandLineOption* option)
{
    return option ? option->defaultValues : NULL;
}

void XCommandLineOption_setFlags(XCommandLineOption* option, int flags)
{
    if (option) option->flags = flags;
}

int XCommandLineOption_flags(const XCommandLineOption* option)
{
    return option ? option->flags : 0;
}

bool XCommandLineOption_isHidden(const XCommandLineOption* option)
{
    return option && (option->flags & XCOMMANDLINE_OPTION_FLAG_HIDDEN_FROM_HELP);
}

bool XCommandLineOption_requiresValue(const XCommandLineOption* option)
{
    return option && option->valueName != NULL && XString_length_base(option->valueName) > 0;
}

