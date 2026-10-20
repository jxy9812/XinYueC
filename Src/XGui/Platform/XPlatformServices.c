/****************************************************************************
 * @file       XPlatformServices.c
 * @brief      平台服务对象实现。
 ****************************************************************************/
#if XGUI_ON

#include "XPlatformServices.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#if XPLATFORMINTEGRATION_ON
struct XPlatformServices { XMemoryType m_memory; bool m_available; };
XPlatformServices* XPlatformServices_create_ex(XMemoryType memory)
{
    XPlatformServices* self = (XPlatformServices*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    self->m_memory = memory; /* 记录分配器族: destroy 按同族配对释放。 */
    self->m_available = XPlatformServicesDriver_isAvailable();
    return self;
}
void XPlatformServices_destroy(XPlatformServices* self)
{ if (self) XMemory_free(self, self->m_memory); }
bool XPlatformServices_isAvailable(const XPlatformServices* self)
{ return self && self->m_available; }
bool XPlatformServices_openUrl(XPlatformServices* self, const XString* url)
{
    const char* utf8;
    if (!self || !self->m_available || !url) return false;
    utf8 = XString_toUtf8(url);
    return utf8 && XPlatformServicesDriver_openUrl(utf8);
}
bool XPlatformServices_openUrl_2(XPlatformServices* self, const char* url)
{
    XString* tmp = NULL;
    bool ok;
    if (!url) return false;
    tmp = XString_create_utf8(url);
    if (!tmp) return false;
    ok = XPlatformServices_openUrl(self, tmp);
    XClassDelete(tmp);
    return ok;
}
#endif

#endif /* XGUI_ON */
