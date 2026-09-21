/**
 * @file       XSvgIconEngine.c
 * @brief      SVG 图标引擎实现（对标 Qt 6.8 QSvgIconEngine 最小语义）。
 * @author     XinYueC 团队
 */

#include "XSvgIconEngine.h"

/* XSVGICON_ON=0 时整体裁剪：引擎无自动注册、全仓无外部引用，
 * 关闭后仅损失 SVG 图标加载能力，不影响 XIcon 其它引擎路径。 */
#if XSVGICON_ON

#include "XAlgorithm.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XMemory.h"


/** @brief Pixmap 虚槽：解码 SVG 文件为像素图。 */
static void VSvgEngine_pixmap(const XIconEngine* self, const XSize* size,
                              XIconMode mode, XIconState state, XPixmap* out)
{
    XSvgIconEngine* se = (XSvgIconEngine*)self;
    XImage image;
    (void)size;
    (void)mode;
    (void)state;
    if (!out || !se || !se->m_fileName) return;
    XImage_init(&image);
    /* 对标 QSvgIconEngine 的 pixmap 缓存诉求：每次请求都完整「读文件 +
       解析 SVG + 光栅化」代价毫秒级，hover/状态切换/窗口重绘高频触发。
       XImageCache（默认开）在此路径命中后跳过全部 IO 与解析，是本缓存
       的首个受益者。 */
    if (!XImage_load(&image, se->m_fileName, NULL) || XImage_isNull(&image)) {
        XImage_deinit_base(&image);
        return;
    }
    XPixmap_fromImage(&image, 0, out);
    XImage_deinit_base(&image);
}

/** @brief Key 虚槽：返回 "svg"。 */
static XString* VSvgEngine_key(const XIconEngine* self)
{
    (void)self;
    return XString_create_utf8("svg");
}

/** @brief IconName 虚槽：Qt SVG 引擎返回空串。 */
static XString* VSvgEngine_iconName(const XIconEngine* self)
{
    (void)self;
    return XString_create();
}

/** @brief IsNull 虚槽：文件名为空判定。 */
static bool VSvgEngine_isNull(const XIconEngine* self)
{
    XSvgIconEngine* se = (XSvgIconEngine*)self;
    return !se || !se->m_fileName ||
           XString_length_base(se->m_fileName) == 0;
}

static void VSvgEngine_deinit(XSvgIconEngine* self)
{
    if (!self) return;
    if (self->m_fileName) {
        XString_delete_base(self->m_fileName);
        self->m_fileName = NULL;
    }
    XClass_Deinit_Parent(XIconEngine, (XIconEngine*)self);
}

XVtable* XSvgIconEngine_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSvgIconEngine)
    XVTABLE_INHERIT_XCLASS(XIconEngine);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Pixmap, VSvgEngine_pixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Key, VSvgEngine_key);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_IconName, VSvgEngine_iconName);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_IsNull, VSvgEngine_isNull);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VSvgEngine_deinit);
    return XVTABLE_DEFAULT;
}

static void VSvgEngine_init(XSvgIconEngine* self, const XString* fileName)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XIconEngine_init((XIconEngine*)self);
    XClassSetVtable(self, XSvgIconEngine);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    if (fileName)
        self->m_fileName = XString_create_copy(fileName);
}

XSvgIconEngine* XSvgIconEngine_create(const XString* fileName)
{
    XSvgIconEngine* self =
        (XSvgIconEngine*)XMemory_malloc(sizeof(*self),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    VSvgEngine_init(self, fileName);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

XSvgIconEngine* XSvgIconEngine_create_2(const char* utf8FileName)
{
    XSvgIconEngine* engine;
    XString* name;
    if (!utf8FileName) return XSvgIconEngine_create(NULL);
    name = XString_create_utf8(utf8FileName);
    if (!name) return NULL;
    engine = XSvgIconEngine_create(name);
    XString_delete_base(name);
    return engine;
}
#endif /* XSVGICON_ON */

