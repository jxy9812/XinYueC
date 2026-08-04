/**
 * @file       XHttpServerRouter.c
 * @brief      HTTP 服务端路由器实现。
 */

#include "XHttpServerRouter.h"

#include "XHttpServerRouterRule.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>

static void VXHttpServerRouter_deinit(XHttpServerRouter* self)
{
    size_t i;
    if (!self)
        return;
    if (self->m_rules) {
        for (i = 0; i < XVector_size_base(self->m_rules); ++i) {
            XHttpServerRouterRule** slot =
                (XHttpServerRouterRule**)XVector_at_base(self->m_rules, (int64_t)i);
            if (slot && *slot)
                XClass_delete_base((XClass*)*slot);
        }
        XClass_delete_base((XClass*)self->m_rules);
        self->m_rules = NULL;
    }
    self->m_server = NULL;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

XVtable* XHttpServerRouter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttpServerRouter)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHttpServerRouter");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpServerRouter_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XHttpServerRouter);
    return XVTABLE_DEFAULT;
}

XHttpServerRouter* XHttpServerRouter_create(XHttpServer* server)
{
    XHttpServerRouter* self =
        (XHttpServerRouter*)XMalloc_System(sizeof(*self));
    if (!self)
        return NULL;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpServerRouter);
    self->m_server = server;
    self->m_rules = XVector_Create(XHttpServerRouterRule*);
    Set_Class_MemoryFree(self, XFree_System);
    if (!self->m_rules) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

size_t XHttpServerRouter_size(const XHttpServerRouter* self)
{
    return self && self->m_rules ? XVector_size_base(self->m_rules) : 0;
}

bool XHttpServerRouter_addRule(XHttpServerRouter* self,
                               XHttpServerRouterRule* rule)
{
    if (!self || !self->m_rules || !rule)
        return false;
    return XVector_push_back_1_base(self->m_rules, &rule);
}

XHttpServerRouterRule* XHttpServerRouter_addRule_utf8(
    XHttpServerRouter* self,
    const char* pathPattern,
    uint32_t methods,
    XHttpServer_RouteHandler handler,
    void* context)
{
    XHttpServerRouterRule* rule;
    if (!self)
        return NULL;
    rule = XHttpServerRouterRule_create(pathPattern, methods, handler, context);
    if (!rule)
        return NULL;
    if (!XHttpServerRouter_addRule(self, rule)) {
        XClass_delete_base((XClass*)rule);
        return NULL;
    }
    return rule;
}

void XHttpServerRouter_clear(XHttpServerRouter* self)
{
    size_t i;
    if (!self || !self->m_rules)
        return;
    for (i = 0; i < XVector_size_base(self->m_rules); ++i) {
        XHttpServerRouterRule** slot =
            (XHttpServerRouterRule**)XVector_at_base(self->m_rules, (int64_t)i);
        if (slot && *slot)
            XClass_delete_base((XClass*)*slot);
    }
    XVector_clear_base(self->m_rules);
}

bool XHttpServerRouter_handleRequest(const XHttpServerRouter* self,
                                     const XHttpServerRequest* request,
                                     XHttpServerResponder* responder)
{
    size_t i;
    if (!self || !self->m_rules || !request || !responder)
        return false;
    for (i = 0; i < XVector_size_base(self->m_rules); ++i) {
        XHttpServerRouterRule** slot =
            (XHttpServerRouterRule**)XVector_at_base(self->m_rules, (int64_t)i);
        if (slot && *slot && XHttpServerRouterRule_exec(*slot, request, responder))
            return true;
    }
    return false;
}
