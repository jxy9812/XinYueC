/****************************************************************************
 * @file       XPlatformServices.c
 * @brief      平台服务对象实现。
 ****************************************************************************/
#include "XPlatformServices.h"
#include "XMemory.h"
#include <string.h>
#if XPLATFORMINTEGRATION_ON
struct XPlatformServices { bool m_available; };
XPlatformServices* XPlatformServices_create_ex(XMemoryType memory)
{
    XPlatformServices* self = (XPlatformServices*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    self->m_available = XPlatformServicesDriver_isAvailable();
    return self;
}
void XPlatformServices_destroy(XPlatformServices* self) { if (self) XFree_System(self); }
bool XPlatformServices_isAvailable(const XPlatformServices* self)
{ return self && self->m_available; }
bool XPlatformServices_openUrl(XPlatformServices* self, const char* url)
{ return self && self->m_available && url && XPlatformServicesDriver_openUrl(url); }
#endif
