#include "XCommandLineOption.h"
#include "XMemory.h"
#include "XString.h"
#include "XStringList.h"

/* ==================== XCommandLineOption 实现（对标 QCommandLineOption） ==================== */

XCommandLineOption* XCommandLineOption_create(const XString* name)
{
    XCommandLineOption* opt = XMalloc_System(sizeof(XCommandLineOption));
    if (!opt) return NULL;

    opt->names = XStringList_create();
    if (!opt->names) { XFree_System(opt); return NULL; }
    if (name) {
        XStringList_push_back_base(opt->names, (void*)name);
    }

    opt->description = NULL;
    opt->valueName = NULL;
    opt->defaultValues = XStringList_create();
    opt->flags = 0;
    return opt;
}

XCommandLineOption* XCommandLineOption_create_2(const char* name)
{
    XString* temp = name ? XString_create_utf8(name) : NULL;
    XCommandLineOption* opt = XCommandLineOption_create(temp);
    if (temp) XClassDelete(temp);
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
                    XClassDelete(copy);
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

XCommandLineOption* XCommandLineOption_createFull(const XString* name,
    const XString* description, const XString* valueName, const XString* defaultValue)
{
    XCommandLineOption* opt = XCommandLineOption_create(name);
    if (!opt) return NULL;
    if (description) {
        opt->description = XString_create_copy(description);
    }
    if (valueName) {
        opt->valueName = XString_create_copy(valueName);
    }
    if (defaultValue) {
        XStringList_push_back_base(opt->defaultValues, (void*)defaultValue);
    }
    return opt;
}

XCommandLineOption* XCommandLineOption_createFull_2(const char* name, const char* description,
    const char* valueName, const char* defaultValue)
{
    XString* nameStr = name ? XString_create_utf8(name) : NULL;
    XString* descStr = description ? XString_create_utf8(description) : NULL;
    XString* valueStr = valueName ? XString_create_utf8(valueName) : NULL;
    XString* defaultStr = defaultValue ? XString_create_utf8(defaultValue) : NULL;
    XCommandLineOption* opt = XCommandLineOption_createFull(nameStr, descStr, valueStr, defaultStr);
    if (nameStr) XClassDelete(nameStr);
    if (descStr) XClassDelete(descStr);
    if (valueStr) XClassDelete(valueStr);
    if (defaultStr) XClassDelete(defaultStr);
    return opt;
}

XCommandLineOption* XCommandLineOption_createFullWithNames(const XStringList* names,
    const XString* description, const XString* valueName, const XString* defaultValue)
{
    XCommandLineOption* opt = XCommandLineOption_createWithNames(names);
    if (!opt) return NULL;
    if (description) {
        opt->description = XString_create_copy(description);
    }
    if (valueName) {
        opt->valueName = XString_create_copy(valueName);
    }
    if (defaultValue) {
        XStringList_push_back_base(opt->defaultValues, (void*)defaultValue);
    }
    return opt;
}

XCommandLineOption* XCommandLineOption_createFullWithNames_2(const XStringList* names,
    const char* description, const char* valueName, const char* defaultValue)
{
    XString* descStr = description ? XString_create_utf8(description) : NULL;
    XString* valueStr = valueName ? XString_create_utf8(valueName) : NULL;
    XString* defaultStr = defaultValue ? XString_create_utf8(defaultValue) : NULL;
    XCommandLineOption* opt = XCommandLineOption_createFullWithNames(names, descStr, valueStr, defaultStr);
    if (descStr) XClassDelete(descStr);
    if (valueStr) XClassDelete(valueStr);
    if (defaultStr) XClassDelete(defaultStr);
    return opt;
}


void XCommandLineOption_addName(XCommandLineOption* option, const XString* name)
{
    if (!option || !name) return;
    XStringList_push_back_base(option->names, (void*)name);
}

void XCommandLineOption_addName_2(XCommandLineOption* option, const char* name)
{
    XString* temp;
    if (!option || !name) return;
    temp = XString_create_utf8(name);
    if (!temp) return;
    XCommandLineOption_addName(option, temp);
    XClassDelete(temp);
}

void XCommandLineOption_delete(XCommandLineOption* option)
{
    if (!option) return;
    XClassDelete(option->names);
    XClassDelete(option->description);
    XClassDelete(option->valueName);
    XClassDelete(option->defaultValues);
    XFree_System(option);
}

const XStringList* XCommandLineOption_names(const XCommandLineOption* option)
{
    return option ? option->names : NULL;
}

void XCommandLineOption_setValueName(XCommandLineOption* option, const XString* name)
{
    if (!option) return;
    XClassDelete(option->valueName);
    option->valueName = name ? XString_create_copy(name) : NULL;
}

void XCommandLineOption_setValueName_2(XCommandLineOption* option, const char* name)
{
    XString* temp = name ? XString_create_utf8(name) : NULL;
    XCommandLineOption_setValueName(option, temp);
    if (temp) XClassDelete(temp);
}

const char* XCommandLineOption_valueName(const XCommandLineOption* option)
{
    return (option && option->valueName) ? XString_toUtf8(option->valueName) : NULL;
}

void XCommandLineOption_setDescription(XCommandLineOption* option, const XString* description)
{
    if (!option) return;
    XClassDelete(option->description);
    option->description = description ? XString_create_copy(description) : NULL;
}

void XCommandLineOption_setDescription_2(XCommandLineOption* option, const char* description)
{
    XString* temp = description ? XString_create_utf8(description) : NULL;
    XCommandLineOption_setDescription(option, temp);
    if (temp) XClassDelete(temp);
}

const char* XCommandLineOption_description(const XCommandLineOption* option)
{
    return (option && option->description) ? XString_toUtf8(option->description) : NULL;
}

void XCommandLineOption_setDefaultValue(XCommandLineOption* option, const XString* value)
{
    if (!option) return;
    XStringList_clear_base(option->defaultValues);
    if (value) {
        XStringList_push_back_base(option->defaultValues, (void*)value);
    }
}

void XCommandLineOption_setDefaultValue_2(XCommandLineOption* option, const char* value)
{
    XString* temp = value ? XString_create_utf8(value) : NULL;
    XCommandLineOption_setDefaultValue(option, temp);
    if (temp) XClassDelete(temp);
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
                    XClassDelete(copy);
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

