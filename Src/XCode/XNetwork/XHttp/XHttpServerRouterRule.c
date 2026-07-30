/**
 * @file       XHttpServerRouterRule.c
 * @brief      HTTP 服务端路由规则实现。
 */

#include "XHttpServerRouterRule.h"

#include "XMemory.h"
#include <string.h>

static void VXHttpServerRouterRule_deinit(XHttpServerRouterRule* self)
{
    if (!self)
        return;
    if (self->m_pathPattern)
        XClass_delete_base((XClass*)self->m_pathPattern);
    self->m_pathPattern = NULL;
    self->m_handler = NULL;
    self->m_context = NULL;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static bool xhttp_router_match_path(const char* pattern,
                                    size_t patternSize,
                                    size_t patternPos,
                                    const char* path,
                                    size_t pathSize,
                                    size_t pathPos)
{
    /* <arg> 是 Qt 路由中的非空捕获段；C 接口不转换捕获值，但保留匹配语义。 */
    if (patternPos == patternSize)
        return pathPos == pathSize;
    if (patternPos + 5 <= patternSize &&
        memcmp(pattern + patternPos, "<arg>", 5) == 0) {
        size_t candidate;
        if (pathPos >= pathSize)
            return false;
        for (candidate = pathPos + 1; candidate <= pathSize; ++candidate) {
            if (xhttp_router_match_path(pattern, patternSize, patternPos + 5,
                                        path, pathSize, candidate))
                return true;
        }
        return false;
    }
    if (pathPos >= pathSize || pattern[patternPos] != path[pathPos])
        return false;
    return xhttp_router_match_path(pattern, patternSize, patternPos + 1,
                                   path, pathSize, pathPos + 1);
}

XVtable* XHttpServerRouterRule_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHttpServerRouterRule))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpServerRouterRule_deinit);
    return XVTABLE_DEFAULT;
}

XHttpServerRouterRule* XHttpServerRouterRule_create(const char* pathPattern,
                                                    uint32_t methods,
                                                    XHttpServer_RouteHandler handler,
                                                    void* context)
{
    XHttpServerRouterRule* self;
    if (!pathPattern || !handler)
        return NULL;
    self = (XHttpServerRouterRule*)XMalloc_System(sizeof(*self));
    if (!self)
        return NULL;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpServerRouterRule);
    self->m_pathPattern = XString_create_utf8(pathPattern);
    self->m_methods = methods;
    self->m_handler = handler;
    self->m_context = context;
    Set_Class_MemoryFree(self, XFree_System);
    if (!self->m_pathPattern) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

const XString* XHttpServerRouterRule_pathPattern_const(
    const XHttpServerRouterRule* self)
{
    return self ? self->m_pathPattern : NULL;
}

uint32_t XHttpServerRouterRule_methods(const XHttpServerRouterRule* self)
{
    return self ? self->m_methods : UINT32_C(0);
}

bool XHttpServerRouterRule_matches(const XHttpServerRouterRule* self,
                                   const XHttpServerRequest* request)
{
    const XString* path;
    const char* patternData;
    const char* pathData;
    size_t patternSize;
    size_t pathSize;
    if (!self || !self->m_pathPattern || !request || !request->m_url)
        return false;
    if ((self->m_methods & XHttpServerRequest_methodFlag(request->m_method)) == 0)
        return false;
    path = XUrl_path_const(request->m_url);
    if (!path)
        return false;
    patternData = XString_toUtf8(self->m_pathPattern);
    pathData = XString_toUtf8(path);
    patternSize = XString_toUtf8_length(self->m_pathPattern);
    pathSize = XString_toUtf8_length(path);
    if (!patternData || !pathData)
        return false;
    return xhttp_router_match_path(patternData, patternSize, 0,
                                   pathData, pathSize, 0);
}

bool XHttpServerRouterRule_exec(const XHttpServerRouterRule* self,
                                const XHttpServerRequest* request,
                                XHttpServerResponder* responder)
{
    if (!self || !self->m_handler ||
        !XHttpServerRouterRule_matches(self, request))
        return false;
    self->m_handler(request, responder, self->m_context);
    return true;
}
