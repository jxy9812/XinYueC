#include"XSocketDescriptor.h"
XSocketDescriptor XSocketDescriptor_Invalid(void)
{
    XSocketDescriptor sd = { -1 };
    return sd;
}

bool XSocketDescriptor_isValid(XSocketDescriptor sd)
{
#ifdef _WIN32
    // 使用 uintptr_t 避免符号问题，并只检查 -1
    return (uintptr_t)sd.value != (uintptr_t)-1;
#else
    return sd.value >= 0;
#endif
}

XSocketDescriptor XSocketDescriptor_fromIntptr(intptr_t value)
{
    XSocketDescriptor sd = { value };
    return sd;
}
int32_t XSocketDescriptor_compare(const XSocketDescriptor* str1, const XSocketDescriptor* str2)
{
    return str1->value-str2->value;
}
intptr_t XSocketDescriptor_toIntptr(XSocketDescriptor sd)
{
    return sd.value;
}