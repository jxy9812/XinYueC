#include "XPlatformDrag.h"
#include <stddef.h>

#if XPLATFORMINTEGRATION_ON && !defined(_WIN32) && \
    !(defined(__linux__) && defined(XINYUE_C_HAS_X11))
struct XPlatformDrag { int unused; };
XPlatformDrag* XPlatformDrag_create(void) { return NULL; }
void XPlatformDrag_delete(XPlatformDrag* self) { (void)self; }
bool XPlatformDrag_isAvailable(const XPlatformDrag* self)
{ (void)self; return false; }
XPlatformDragResult XPlatformDrag_exec(XPlatformDrag* self, XWindow* source,
                                       const XMimeData* data, uint32_t actions)
{
    (void)self; (void)source; (void)data; (void)actions;
    return XPlatformDragResult_Unsupported;
}
#endif
