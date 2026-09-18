/**
 * @file       XSvgIconEnginePlugin.c
 * @brief      SVG 图标引擎插件实现（Task 2.15 内置真实插件）。
 * @author     XinYueC 团队
 */

#include "XSvgIconEnginePlugin.h"
#include "XSvgIconEngine.h"

/* XSVGICON_ON=0 时整体裁剪：插件不参与自动注册（全仓无注册调用），
 * 关闭后 SVG 键不再可用，图标引擎插件注册表行为不变。 */
#if XSVGICON_ON

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XStringList.h"


/** @brief Create 虚槽：仅接受 svg/svgz 后缀，返回 XSvgIconEngine。 */
static XIconEngine* VSvgPlugin_create(XIconEnginePlugin* self,
                                      const XString* fileName)
{
    const char* utf8;
    (void)self;
    if (!fileName) return NULL;
    utf8 = XString_toUtf8(fileName);
    if (!utf8) return NULL;
    return (XIconEngine*)XSvgIconEngine_create_2(utf8);
}

/** @brief Keys 虚槽：支持 svg/svgz。 */
static XStringList* VSvgPlugin_keys(const XIconEnginePlugin* self)
{
    XStringList* list;
    (void)self;
    list = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!list) return NULL;
    XStringList_push_back_utf8(list, "svg");
    XStringList_push_back_utf8(list, "svgz");
    return list;
}

XVtable* XSvgIconEnginePlugin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSvgIconEnginePlugin)
    XVTABLE_INHERIT_XCLASS(XIconEnginePlugin);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEnginePlugin_Create, VSvgPlugin_create);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEnginePlugin_Keys, VSvgPlugin_keys);
    return XVTABLE_DEFAULT;
}

static void VSvgPlugin_init(XSvgIconEnginePlugin* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XIconEnginePlugin_init((XIconEnginePlugin*)self);
    XClassSetVtable(self, XSvgIconEnginePlugin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
}

XSvgIconEnginePlugin* XSvgIconEnginePlugin_create(void)
{
    XSvgIconEnginePlugin* self =
        (XSvgIconEnginePlugin*)XMemory_malloc(sizeof(*self),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    VSvgPlugin_init(self);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}
#endif /* XSVGICON_ON */

