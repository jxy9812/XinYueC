#include"XSocketDescriptor.h"
XSocketDescriptor XSocketDescriptor_Invalid(void)
{
    XSocketDescriptor sd = { -1 };
    return sd;
}

bool XSocketDescriptor_isValid(XSocketDescriptor sd)
{
#ifdef _WIN32
    return sd.value != (intptr_t)-1 && sd.value != 0;
#else
    return sd.value >= 0;
#endif
}

XSocketDescriptor XSocketDescriptor_fromIntptr(intptr_t value)
{
    XSocketDescriptor sd = { value };
    return sd;
}
intptr_t XSocketDescriptor_toIntptr(XSocketDescriptor sd)
{
    return sd.value;
}