/**
 * @file       XGraphicsEffectAlignTest.c
 * @brief      XGraphicsEffect 与 Qt 6.8 QGraphicsEffect 对齐契约测试。
 * @details    覆盖基类五条语义线（对齐依据：本机
 *             /usr/include/x86_64-linux-gnu/qt6/QtWidgets/qgraphicseffect.h
 *             + 同名 Qt 6.8 探针实测，结论逐条写进断言注释）：
 *             ① enabled 切换：默认启用、同值早退、信号载荷、无源 update 空操作；
 *             ② 源坐标系：boundingRect() 是**源本地坐标**（原点恒 (0,0)），
 *                不是控件在父级中的位置；无源为空矩形；
 *             ③ 安装所有权：控件拥有效果，换装/置 NULL 删除旧效果（用
 *                destroyed 信号取证），重复安装同一效果不动作；
 *             ④ 生命周期：直接删除已挂接效果会摘空控件槽位；同一效果挂到
 *                第二个控件时从原控件摘下（防双删）；
 *             ⑤ 源像素图 API：sourceIsPixmap() 恒 false（控件源）、
 *                sourcePixmap() 的 PixmapPadMode 三种补边量与 offset
 *                （Qt 6.8 探针：30x20 源 → NoPad 30x20/(0,0)、
 *                PadToTransparentBorder 32x22/(-1,-1)、
 *                PadToEffectiveBoundingRect 跟 boundingRectFor 生长），
 *                以及 draw() 之外返回 NULL 的口径（Qt 同场景实测段错误）。
 *
 *             泄漏由运行器自动判定：用例故意不手工删除「所有权已转移」的
 *             效果对象——控件没删干净就会判 [LEAK]。
 * @note       本机 Qt 6.8 实测要点（用于断言取值）：
 *             - effect.parent() 安装后仍为 NULL（控件不重设父对象）；
 *             - w.setGraphicsEffect(nullptr) 后效果析构（alive 计数 -1）；
 *             - delete effect 后 w.graphicsEffect() == nullptr；
 *             - 同一效果挂到 w2 后 w1.graphicsEffect() == nullptr；
 *             - 控件源 sourceIsPixmap() == false（draw() 内外一致）；
 *             - draw() 之外调 sourcePixmap(Qt::LogicalCoordinates) 段错误
 *               （QWidgetEffectSourcePrivate::pixmap 解引用空 context）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGraphicsEffect.h"
#include "XGraphicsBlurEffect.h"
#include "XObject.h"
#include "XVarList.h"
#include "XWidget.h"
#include "XImage.h"
#include "XColor.h"
#include "XClass.h"   /* 源变更探针：XCLASS_DEFINE 系列/XVTABLE_OVERLOAD/XClassSetVtable。 */
#include "XMemory.h"  /* 探针对象堆分配（XMemory_malloc）。 */

/* ==================== 探针槽函数 ==================== */

static int s_enabledCount = 0;   /**< enabledChanged 触发次数。 */
static bool s_enabledLast = false; /**< 最近一次 enabledChanged 载荷。 */
static int s_destroyedCount = 0;   /**< destroyed 触发次数。 */

/** @brief enabledChanged(bool) 槽：记录次数与载荷。 */
static void xq_effectEnabledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    {
        XVarList_args_1(args, bool, enabled);
        ++s_enabledCount;
        s_enabledLast = enabled;
    }
}

/** @brief destroyed() 槽：只记次数（与该信号的无参载荷一致）。 */
static void xq_effectDestroyedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_destroyedCount;
}

/* ==================== 源变更探针（自定义效果子类） ==================== */

/*
 * [P0-14] 自定义效果靠 sourceChanged(SourceDetached) 清缓存，故必须能
 * 观测「通知发的时机」。用文件级静态量记账（而不是探针实例字段）：
 * 换装路径里控件会**删除**旧效果，实例字段读完就是 UAF。
 */
XCLASS_DEFINE_BEGING(XGraphicsEffectProbe)
XCLASS_DEFINE_EXTEND_END(XGraphicsEffectProbe, XGraphicsEffect)

typedef struct XGraphicsEffectProbe
{
    XGraphicsEffect m_base; /**< 基类；必须第一个。 */
} XGraphicsEffectProbe;

static int s_detachedCount = 0;       /**< SourceDetached 次数。 */
static int s_attachedCount = 0;       /**< SourceAttached 次数。 */
static int s_sourceAliveAtDetach = 0; /**< 通知 detached 时 source() 仍非 NULL。 */
static int s_boundsValidAtDetach = 0; /**< 通知 detached 时上一轮包围盒已标脏。 */
static int s_lastFlags = 0;           /**< 最近一次 sourceChanged 的 flags。 */

static void xq_effectProbeReset(void)
{
    s_detachedCount = 0;
    s_attachedCount = 0;
    s_sourceAliveAtDetach = 0;
    s_boundsValidAtDetach = 0;
    s_lastFlags = 0;
}

/** @brief SourceChanged 槽覆写：只记账（刻意不转 update()，以便验证调用方
 *         的「update → 通知 → 断源」顺序：boundsValid 只可能由调用方置位）。 */
static void xq_effectProbeSourceChanged(XGraphicsEffect* self, int flags)
{
    if (!self) return;
    s_lastFlags = flags;
    if (flags & XGraphicsEffectChange_SourceDetached) {
        ++s_detachedCount;
        if (XGraphicsEffect_source(self) != NULL) ++s_sourceAliveAtDetach;
        if (self->m_lastBoundsValid) ++s_boundsValidAtDetach;
    }
    if (flags & XGraphicsEffectChange_SourceAttached) ++s_attachedCount;
}

static XVtable* XGraphicsEffectProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsEffectProbe)
    XVTABLE_INHERIT_XCLASS(XGraphicsEffect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_SourceChanged,
                             xq_effectProbeSourceChanged);
    return XVTABLE_DEFAULT;
}

/** @brief 堆上创建探针效果（调用方按 XClassDelete 释放；挂接后归控件）。 */
static XGraphicsEffectProbe* xq_effectProbeCreate(void)
{
    XGraphicsEffectProbe* p = (XGraphicsEffectProbe*)
        XMemory_malloc(sizeof(XGraphicsEffectProbe),
                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    XMemset(p, 0, sizeof(*p));
    XGraphicsEffect_init(&p->m_base);
    XClassSetVtable(p, XGraphicsEffectProbe);
    Set_Class_Memory(p, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(p, true);
    return p;
}

/* ==================== ① enabled 切换 ==================== */

/**
 * @brief 默认启用、同值早退、信号载荷、无源 update 安全。
 * @details Qt 语义：QGraphicsEffect 默认 enabled=true；
 *          setEnabled 同值直接 return（不发射）；update() 在无源时是空操作
 *          （`if (d->source) d->source->update();`）。
 */
XQ_TEST(XGraphicsEffect, enabledToggle)
{
    XObject* probe = XObject_create();
    XGraphicsEffect* effect = XGraphicsEffect_create();
    XQ_REQUIRE(probe != NULL && effect != NULL);

    s_enabledCount = 0;
    s_enabledLast = false;

    XQ_CHECK_MSG(XGraphicsEffect_isEnabled(effect),
                 "QGraphicsEffect 默认启用（Qt 构造 isEnabled=true）");
    XObject_connect((XObject*)effect,
                      XSignal(XGraphicsEffect_enabledChanged_signal), probe,
                      xq_effectEnabledSlot, XConnectionType_Direct);

    XGraphicsEffect_setEnabled(effect, false);
    XQ_EQ_I64(s_enabledCount, 1);
    XQ_CHECK(!s_enabledLast);
    XQ_CHECK(!XGraphicsEffect_isEnabled(effect));

    /* 同值：Qt 首行 `if (d->isEnabled == enable) return;` */
    XGraphicsEffect_setEnabled(effect, false);
    XQ_EQ_I64(s_enabledCount, 1);

    XGraphicsEffect_setEnabled(effect, true);
    XQ_EQ_I64(s_enabledCount, 2);
    XQ_CHECK(s_enabledLast);

    /* 无源时 update/updateBoundingRect 必须是无异常的空操作。 */
    XQ_CHECK(XGraphicsEffect_source(effect) == NULL);
    XGraphicsEffect_update(effect);
    XGraphicsEffect_updateBoundingRect(effect);
    /* drawSource 只在 Draw 派发期间有效；此处无上下文，必须是空操作。 */
    XGraphicsEffect_drawSource(effect, NULL);

    /* NULL 安全契约（仓库既有口径：查询类返回默认值，设置类无操作）。 */
    XQ_CHECK(!XGraphicsEffect_isEnabled(NULL));
    XGraphicsEffect_setEnabled(NULL, true);

    XClassDelete((XClass*)effect);
    XClassDelete((XClass*)probe);
}

/* ==================== ② 源坐标系与包围盒 ==================== */

/**
 * @brief boundingRect()/sourceBoundingRect() 的坐标系与无源语义。
 * @details Qt 6.8 实测：子控件 setGeometry(30,40,80,20) 后，其效果的
 *          boundingRect() 为 (0,0,80,20)——原点恒为源本地 (0,0)，**不**随
 *          控件在父级中的位置漂移；无源时 boundingRect() 为空矩形。
 *          基类 boundingRectFor 原样返回入参（Qt 基类默认实现）。
 */
XQ_TEST(XGraphicsEffect, sourceCoordinate)
{
    XWidget* top = XWidget_create(NULL, 0);
    XWidget* child = XWidget_create(NULL, 0);
    XGraphicsEffect* effect = XGraphicsEffect_create();
    XRectF input;
    XRectF got;
    XQ_REQUIRE(top != NULL && child != NULL && effect != NULL);

    XWidget_resize(top, 300, 300);
    XWidget_setParent(child, top, 0);
    XWidget_move(child, 30, 40);
    XWidget_resize(child, 80, 20);

    /* 无源：两个查询都必须是空矩形（Qt `return QRectF();`）。 */
    got = XGraphicsEffect_boundingRect(effect);
    XQ_CHECK(got.width == 0.0f && got.height == 0.0f);
    got = XGraphicsEffect_sourceBoundingRect(effect);
    XQ_CHECK(got.width == 0.0f && got.height == 0.0f);

    /* 基类 boundingRectFor 原样返回（Qt 基类不做任何外扩）。 */
    input.x = 10.0f;
    input.y = 20.0f;
    input.width = 100.0f;
    input.height = 50.0f;
    got = XGraphicsEffect_boundingRectFor(effect, &input);
    XQ_CHECK(got.x == 10.0f && got.y == 20.0f);
    XQ_CHECK(got.width == 100.0f && got.height == 50.0f);
    /* NULL 入参返回空矩形而不是崩溃。 */
    got = XGraphicsEffect_boundingRectFor(effect, NULL);
    XQ_CHECK(got.width == 0.0f && got.height == 0.0f);

    /* 安装后：所有权交给 child，source 关联建立。 */
    XWidget_setGraphicsEffect(child, effect);
    XQ_CHECK(XWidget_graphicsEffect(child) == effect);
    XQ_CHECK(XGraphicsEffect_source(effect) == child);

    got = XGraphicsEffect_sourceBoundingRect(effect);
    XQ_CHECK_MSG(got.x == 0.0f && got.y == 0.0f,
                 "源包围盒原点恒为 (0,0)，不随控件位置漂移");
    XQ_CHECK(got.width == 80.0f && got.height == 20.0f);
    got = XGraphicsEffect_boundingRect(effect);
    XQ_CHECK_MSG(got.x == 0.0f && got.y == 0.0f && got.width == 80.0f &&
                     got.height == 20.0f,
                 "Qt 6.8 实测：child(30,40,80,20) 的 boundingRect=(0,0,80,20)");

    /* 摘除：Qt 删除旧效果，控件槽位复位（effect 此后不得再被访问）。 */
    XWidget_setGraphicsEffect(child, NULL);
    XQ_CHECK(XWidget_graphicsEffect(child) == NULL);

    XClassDelete((XClass*)child);
    XClassDelete((XClass*)top);
}

/* ==================== ③ 安装所有权与替换 ==================== */

/**
 * @brief 控件拥有效果：重复安装不动作、换装删除旧效果、置 NULL 删除。
 * @details 对齐 Qt QWidget::setGraphicsEffect：
 *          `if (d->graphicsEffect == effect) return;` → 旧效果 delete，
 *          再安装新效果。用 destroyed 信号取证"旧效果确实被控件删除"。
 */
XQ_TEST(XGraphicsEffect, mountOwnership)
{
    XWidget* widget = XWidget_create(NULL, 0);
    XObject* probe = XObject_create();
    XGraphicsEffect* first = XGraphicsEffect_create();
    XGraphicsEffect* second = XGraphicsEffect_create();
    XQ_REQUIRE(widget != NULL && probe != NULL && first != NULL &&
               second != NULL);

    XWidget_resize(widget, 64, 48);
    XQ_CHECK(XWidget_graphicsEffect(widget) == NULL);

    s_destroyedCount = 0;
    XObject_connect((XObject*)first, XSignal(XObject_destroyed_signal),
                      probe, xq_effectDestroyedSlot,
                      XConnectionType_Direct);

    XWidget_setGraphicsEffect(widget, first);
    XQ_CHECK(XWidget_graphicsEffect(widget) == first);
    XQ_CHECK(XGraphicsEffect_source(first) == widget);

    /* 同一效果重复设置：Qt 早退，既不删除也不重建关联。 */
    XWidget_setGraphicsEffect(widget, first);
    XQ_CHECK(XWidget_graphicsEffect(widget) == first);
    XQ_CHECK(XGraphicsEffect_source(first) == widget);
    XQ_EQ_I64(s_destroyedCount, 0);

    /* 换装：旧效果被控件删除（Qt `delete d->graphicsEffect`）。 */
    XWidget_setGraphicsEffect(widget, second);
    XQ_EQ_I64(s_destroyedCount, 1);
    XQ_CHECK(XWidget_graphicsEffect(widget) == second);
    XQ_CHECK(XGraphicsEffect_source(second) == widget);

    /* 置 NULL 摘除：当前效果同样由控件删除，槽位复位。 */
    XWidget_setGraphicsEffect(widget, NULL);
    XQ_CHECK(XWidget_graphicsEffect(widget) == NULL);

    /* second 已随摘除释放，此处不得再 delete。 */
    XClassDelete((XClass*)widget);
    XClassDelete((XClass*)probe);
}

/* ==================== ④ 生命周期：直删与易主 ==================== */

/**
 * @brief 直接删除已挂接的效果 ⇒ 控件槽位被摘空。
 * @details 对标 Qt `QGraphicsEffect::~QGraphicsEffect →
 *          setGraphicsEffectSource(nullptr)` → QWidgetEffectSourcePrivate::
 *          detach。Qt 6.8 实测：`delete effect` 后 `w.graphicsEffect()`
 *          返回 nullptr。不摘空的话控件会留悬垂指针，下次绘制读已释放对象。
 */
XQ_TEST(XGraphicsEffect, effectDeleteDetaches)
{
    XWidget* widget = XWidget_create(NULL, 0);
    XGraphicsEffect* effect = XGraphicsEffect_create();
    XQ_REQUIRE(widget != NULL && effect != NULL);

    XWidget_resize(widget, 40, 30);
    XWidget_setGraphicsEffect(widget, effect);
    XQ_CHECK(XWidget_graphicsEffect(widget) == effect);

    XClassDelete((XClass*)effect); /* 外部直删（不走控件） */
    XQ_CHECK_MSG(XWidget_graphicsEffect(widget) == NULL,
                 "效果析构必须摘空控件槽位（Qt detach 语义）");

    XClassDelete((XClass*)widget);
}

/**
 * @brief 同一效果挂到第二个控件 ⇒ 从原控件摘下。
 * @details Qt 6.8 实测：w2.setGraphicsEffect(e) 之后 w1.graphicsEffect()
 *          变 nullptr、w2 持有 e。不先摘下旧宿主时两控件都认为自己拥有该
 *          效果，任一控件析构 delete 后另一控件悬垂并二次释放。
 */
XQ_TEST(XGraphicsEffect, secondWidgetSteals)
{
    XWidget* first = XWidget_create(NULL, 0);
    XWidget* second = XWidget_create(NULL, 0);
    XGraphicsEffect* effect = XGraphicsEffect_create();
    XQ_REQUIRE(first != NULL && second != NULL && effect != NULL);

    XWidget_resize(first, 20, 20);
    XWidget_resize(second, 30, 30);

    XWidget_setGraphicsEffect(first, effect);
    XQ_CHECK(XWidget_graphicsEffect(first) == effect);

    XWidget_setGraphicsEffect(second, effect);
    XQ_CHECK_MSG(XWidget_graphicsEffect(first) == NULL,
                 "易主后原控件槽位必须清空（Qt 实测 w1.graphicsEffect()==NULL）");
    XQ_CHECK(XWidget_graphicsEffect(second) == effect);
    XQ_CHECK(XGraphicsEffect_source(effect) == second);

    /* 原控件析构不得带走已易主的效果。 */
    XClassDelete((XClass*)first);
    XQ_CHECK(XWidget_graphicsEffect(second) == effect);

    XClassDelete((XClass*)second); /* effect 随 second 释放 */
}

/* ==================== ④b 源变更通知（SourceDetached） ==================== */

/**
 * @brief 换装 / 置 NULL / 被第二控件夺走：旧源必须收到 SourceDetached，
 *        且顺序为「update → 通知 → 断源」；控件析构路径保持静默。
 * @details 对齐依据：Qt QGraphicsEffectPrivate::setGraphicsEffectSource
 *          （qgraphicseffect_p.h:119-133）：
 *            if (source) {
 *                flags |= QGraphicsEffect::SourceDetached;
 *                source->d_func()->effectBoundingRectChanged();
 *                source->d_func()->invalidateCache();
 *                source->d_func()->detach();
 *                delete source;
 *            }
 *            source = newSource;
 *            if (newSource) flags |= QGraphicsEffect::SourceAttached;
 *            q_func()->sourceChanged(flags);
 *          即「先让旧效果失效（标脏）、再 sourceChanged、最后才把 source
 *          指到新源」。XWidget_setGraphicsEffect 照此顺序：
 *          XGraphicsEffect_update(old) → sourceChanged(SourceDetached) →
 *          setSource(old, NULL)。
 *          【P0-14 2026-11-11】此前 SourceDetached 全仓零发射点，自定义
 *          效果收不到断源通知，会带着旧缓存继续画。
 * @note    析构路径（控件析构带走效果 / 拷贝重置）**有意**静默：此刻控件
 *          已在半拆状态，回调进用户槽再标脏会重入。本用例末段锁定该约定。
 */
XQ_TEST(XGraphicsEffect, sourceChangedDetached)
{
    XWidget* a = XWidget_create(NULL, 0);
    XWidget* b = XWidget_create(NULL, 0);
    XWidget* c = NULL;
    XGraphicsEffectProbe* probe = NULL;
    XGraphicsEffect* replacement = XGraphicsEffect_create();
    XQ_REQUIRE(a != NULL && b != NULL && replacement != NULL);
    XWidget_resize(a, 60, 40);
    XWidget_resize(b, 30, 30);

    /* ---- ① 挂接：SourceAttached ---- */
    xq_effectProbeReset();
    probe = xq_effectProbeCreate();
    XQ_REQUIRE(probe != NULL);
    XWidget_setGraphicsEffect(a, (XGraphicsEffect*)probe);
    XQ_EQ_I64(s_attachedCount, 1);
    XQ_EQ_I64(s_detachedCount, 0);
    XQ_CHECK((s_lastFlags & XGraphicsEffectChange_SourceAttached) != 0);

    /* ---- ② 换装：旧效果收 SourceDetached，且「update → 通知 → 断源」----
       先手工清 lastBoundsValid：若实现顺序正确，通知里它必已被调用方的
       update(old) 重新置位；若先通知后 update，这里会是 0。 */
    probe->m_base.m_lastBoundsValid = false;
    xq_effectProbeReset();
    XWidget_setGraphicsEffect(a, replacement); /* probe 由控件删除 */
    probe = NULL;
    XQ_EQ_I64(s_detachedCount, 1);
    XQ_EQ_I64(s_sourceAliveAtDetach, 1);
    XQ_CHECK_MSG(s_boundsValidAtDetach == 1,
                 "SourceDetached 之前必须先跑 update()（旧外扩环已标脏）");
    XQ_CHECK((s_lastFlags & XGraphicsEffectChange_SourceDetached) != 0);

    /* ---- ③ 置 NULL 摘除：同一条断源通知 ---- */
    probe = xq_effectProbeCreate();
    XQ_REQUIRE(probe != NULL);
    XWidget_setGraphicsEffect(b, (XGraphicsEffect*)probe);
    probe->m_base.m_lastBoundsValid = false;
    xq_effectProbeReset();
    XWidget_setGraphicsEffect(b, NULL); /* probe 由控件删除 */
    probe = NULL;
    XQ_EQ_I64(s_detachedCount, 1);
    XQ_EQ_I64(s_sourceAliveAtDetach, 1);
    XQ_CHECK(s_boundsValidAtDetach == 1);

    /* ---- ④ 被第二个控件夺走：原宿主同样算「断源」---- */
    probe = xq_effectProbeCreate();
    XQ_REQUIRE(probe != NULL);
    XWidget_setGraphicsEffect(b, (XGraphicsEffect*)probe); /* b 取得所有权 */
    xq_effectProbeReset();
    XWidget_setGraphicsEffect(a, (XGraphicsEffect*)probe); /* 从 b 夺走 */
    XQ_EQ_I64(s_detachedCount, 1);
    XQ_EQ_I64(s_sourceAliveAtDetach, 1);
    XQ_EQ_I64(s_attachedCount, 1);
    XQ_CHECK(XGraphicsEffect_source((XGraphicsEffect*)probe) == a);
    XQ_CHECK(XWidget_graphicsEffect(b) == NULL);

    /* ---- ⑤ 控件析构带走效果：有意静默（不发射 SourceDetached）---- */
    c = XWidget_create(NULL, 0);
    XQ_REQUIRE(c != NULL);
    {
        XGraphicsEffectProbe* dying = xq_effectProbeCreate();
        XQ_REQUIRE(dying != NULL);
        xq_effectProbeReset();
        XWidget_setGraphicsEffect(c, (XGraphicsEffect*)dying);
        XQ_EQ_I64(s_attachedCount, 1);
        XClassDelete((XClass*)c); /* 控件析构 → 断开源 → 删效果 */
        c = NULL;
        XQ_EQ_I64(s_detachedCount, 0);
    }

    XClassDelete((XClass*)a); /* 带走 probe（a 此刻持有它；replacement 已在
                                 ④ 换装时由控件删除，不得重复释放） */
    XClassDelete((XClass*)b); /* 无效果 */
}

/* ==================== ④c update() 的标脏覆盖（P0-59） ==================== */

/**
 * @brief update() 的脏区必须覆盖「源控件矩形 ∪ 效果包围盒」，包围盒
 *        收缩到小于控件矩形、乃至面积为负时也必须标控件矩形。
 * @details Qt 语义：QGraphicsEffect::update() → source->invalidateCache()
 *          → 源控件 `widget->update()`（**整控件**失效）。X 版为实现
 *          K-18 的粒度优化只标效果包围盒，但 boundingRectFor 可收缩：
 *          qpixmapfilter.cpp:545-555 的 `delta = 2.5*radius + 1`，半径
 *          为负时包围盒比控件矩形**小**（radius=-1 ⇒ 每边内缩 1.5px），
 *          四周边环不在包围盒里；面积为负时旧实现更直接 return（一行都
 *          不标）⇒ setBlurRadius(5→-20) 或 setEnabled(false) 后上一帧
 *          画面永远留在屏上。
 *          【P0-59 2026-11-11】标记矩形改为「源控件矩形 ∪ 效果包围盒 ∪
 *          上一轮包围盒」，空/负面积分支只清 m_lastBoundsValid 并标控件
 *          矩形。
 * @note    脏区只在控件**可见**时入队（对标 Qt qwidget.cpp:11303
 *          `if (!q->isVisible() ...) return;`，XWidget_addDirty 同闸），
 *          故这里必须先 show 再断言——隐藏控件上任何 update 都是空操作，
 *          与 Qt 一致。控件无父 ⇒ 脏区直接落在它的 m_dirty（坐标即控件
 *          局部坐标），断言的像素口径无中间换算。
 */
XQ_TEST(XGraphicsEffect, updateMarksSourceRect)
{
    XWidget* top = XWidget_create(NULL, 0);
    XGraphicsBlurEffect* blur = XGraphicsBlurEffect_create();
    XRect bbox;
    XQ_REQUIRE(top != NULL && blur != NULL);
    XWidget_resize(top, 60, 40);
    XWidget_show(top); /* 可见性闸（隐藏控件的 update 不入脏区）。 */

    /* 负半径 -1 ⇒ pad = 2.5*(-1)+1 = -1.5 ⇒ 包围盒 (1.5,1.5,57,37)，
       **小于**控件整幅 (0,0,60,40)：四边 1.5px 环只能靠控件矩形补脏。 */
    XGraphicsBlurEffect_setBlurRadius(blur, -1.0f);
    XWidget_setGraphicsEffect(top, (XGraphicsEffect*)blur);

    XRegion_clear(&top->m_dirty);
    XGraphicsEffect_update((XGraphicsEffect*)blur);
    XQ_CHECK_MSG(XRegion_contains(&top->m_dirty, 0, 0),
                 "脏区必须覆盖控件矩形左上角（包围盒收缩到 (1.5,1.5) 起）");
    XQ_CHECK_MSG(XRegion_contains(&top->m_dirty, 59, 39),
                 "脏区必须覆盖控件矩形右下角");
    XRegion_boundingRect(&top->m_dirty, &bbox);
    XQ_CHECK_MSG(bbox.x <= 0 && bbox.y <= 0 &&
                     bbox.x + bbox.width >= 60 && bbox.y + bbox.height >= 40,
                 "脏区外包矩形应覆盖 (0,0,60,40)（实得 %d,%d,%d,%d）",
                 bbox.x, bbox.y, bbox.width, bbox.height);

    /* 包围盒面积为负（半径 -20 ⇒ pad = -49）：旧实现直接 return（零标脏）；
       现在至少标控件整幅，并作废旧包围盒缓存。 */
    XGraphicsBlurEffect_setBlurRadius(blur, -20.0f);
    XRegion_clear(&top->m_dirty);
    XGraphicsEffect_update((XGraphicsEffect*)blur);
    XQ_CHECK_MSG(!XRegion_isEmpty(&top->m_dirty),
                 "包围盒为负时也必须标脏（否则上一帧画面永不覆写）");
    XQ_CHECK(XRegion_contains(&top->m_dirty, 0, 0));
    XQ_CHECK(XRegion_contains(&top->m_dirty, 59, 39));
    XQ_CHECK_MSG(!((XGraphicsEffect*)blur)->m_lastBoundsValid,
                 "空/负面积时旧包围盒缓存必须作废（不再并入失效矩形）");

    /* setEnabled(false) 复用同一入口：禁用后画面必须被常规绘制覆写。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 5.0f); /* 恢复正外扩 */
    XRegion_clear(&top->m_dirty);
    XGraphicsEffect_setEnabled((XGraphicsEffect*)blur, false);
    XQ_CHECK_MSG(!XRegion_isEmpty(&top->m_dirty),
                 "setEnabled(false) 必须标脏（复用 XGraphicsEffect_update）");
    XQ_CHECK(XRegion_contains(&top->m_dirty, 0, 0));

    XClassDelete((XClass*)top); /* 带走 blur */
}

/* ==================== ⑤ 源像素图（sourcePixmap/sourceIsPixmap） ==================== */

/**
 * @brief sourceIsPixmap 恒 false；sourcePixmap 按 PixmapPadMode 补边。
 * @details 对齐依据：Qt 6.8 qgraphicseffect.h:40-44（PixmapPadMode 枚举值
 *          0/1/2）与 QWidgetEffectSourcePrivate::pixmap（qwidget.cpp:5823-5867）：
 *              effectRect = 源矩形（NoPad）
 *                         | 源矩形四周 +1（PadToTransparentBorder）
 *                         | boundingRectFor(源矩形) 整数对齐（PadToEffective-
 *                           BoundingRect）
 *              *offset = effectRect.topLeft()
 *          本机 Qt 6.8 探针（30x20 控件、基类效果）实测：
 *              NoPad 30x20 offset(0,0)；PadToTransparentBorder 32x22
 *              offset(-1,-1)；PadToEffectiveBoundingRect 30x20 offset(0,0)。
 *          探针另实测：draw() 之外调用会段错误（空 context），故 XGui 在
 *          无绘制上下文时返回 NULL（无源同样 NULL）。
 *          本用例以合成的绘制上下文（管线 Draw 期间的 m_drawCtx 形态）
 *          驱动真实实现，避免依赖离屏整窗渲染。
 */
XQ_TEST(XGraphicsEffect, sourcePixmapPadModes)
{
    XWidget* widget = XWidget_create(NULL, 0);
    XGraphicsEffect* base = XGraphicsEffect_create();
    XGraphicsBlurEffect* blur = XGraphicsBlurEffect_create();
    XGraphicsEffectDrawContext ctx;
    XImage* snapshot = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XImage* dest = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XImage* got;
    XPoint offset;
    XColor px;
    XQ_REQUIRE(widget != NULL && base != NULL && blur != NULL &&
               snapshot != NULL && dest != NULL);

    XWidget_resize(widget, 30, 20);
    XQ_CHECK(XImage_reinit_ex(snapshot, 30, 20,
                              XImageFormat_ARGB32_Premultiplied));
    XQ_CHECK(XImage_reinit_ex(dest, 30, 20,
                              XImageFormat_ARGB32_Premultiplied));
    /* 源快照 = 全幅不透明红（0xAARRGGBB）。 */
    XImage_fillRect(snapshot, NULL, 0xFFFF0000u);
    XImage_fillRect(dest, NULL, 0u);

    /* 枚举值与 Qt 逐项一致。 */
    XQ_EQ_I64((long long)XGraphicsEffectPadMode_NoPad, 0);
    XQ_EQ_I64((long long)XGraphicsEffectPadMode_PadToTransparentBorder, 1);
    XQ_EQ_I64((long long)XGraphicsEffectPadMode_PadToEffectiveBoundingRect, 2);

    /* 无源：NULL + offset 归零（Qt `return QPixmap()` 口径）。 */
    offset.x = 7;
    offset.y = 9;
    XQ_CHECK(XGraphicsEffect_sourcePixmap(base, &offset,
                                          XGraphicsEffectPadMode_NoPad) == NULL);
    XQ_CHECK(offset.x == 0 && offset.y == 0);

    /* 控件源恒非像素图（Qt 探针：draw() 内外都是 false）。 */
    XQ_CHECK(!XGraphicsEffect_sourceIsPixmap(base));
    XQ_CHECK(!XGraphicsEffect_sourceIsPixmap(NULL));

    /* 挂接后（所有权转移给 widget）。 */
    XWidget_setGraphicsEffect(widget, (XGraphicsEffect*)blur);
    XQ_CHECK(!XGraphicsEffect_sourceIsPixmap((XGraphicsEffect*)blur));

    /* 无绘制上下文（Draw 之外）：NULL，不冒充离屏源。 */
    XQ_CHECK(XGraphicsEffect_sourcePixmap((XGraphicsEffect*)blur, &offset,
                                          XGraphicsEffectPadMode_NoPad) == NULL);

    /* 合成 Draw 期上下文。 */
    ctx.m_source = snapshot;
    ctx.m_sourceRect.x = 0;
    ctx.m_sourceRect.y = 0;
    ctx.m_sourceRect.width = 30;
    ctx.m_sourceRect.height = 20;
    ctx.m_dest = dest;
    ctx.m_destRect = ctx.m_sourceRect;
    XGraphicsBlurEffect_setBlurRadius(blur, 2.0f); /* pad = 1 + 2.5*2 = 6 */
    ((XGraphicsEffect*)blur)->m_drawCtx = &ctx;

    /* NoPad：与源同尺寸、offset(0,0)、内容原位。 */
    offset.x = 0;
    offset.y = 0;
    got = XGraphicsEffect_sourcePixmap((XGraphicsEffect*)blur, &offset,
                                       XGraphicsEffectPadMode_NoPad);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_I64(XImage_width(got), 30);
    XQ_EQ_I64(XImage_height(got), 20);
    XQ_CHECK(offset.x == 0 && offset.y == 0);
    px = XImage_pixelColor(got, 0, 0);
    XQ_CHECK(XColor_red(&px) == 255 && XColor_alpha(&px) == 255);
    XClassDelete((XClass*)got);

    /* PadToTransparentBorder：四周各 +1、offset(-1,-1)，补边区透明。 */
    got = XGraphicsEffect_sourcePixmap((XGraphicsEffect*)blur, &offset,
                                       XGraphicsEffectPadMode_PadToTransparentBorder);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_I64(XImage_width(got), 32);
    XQ_EQ_I64(XImage_height(got), 22);
    XQ_CHECK(offset.x == -1 && offset.y == -1);
    px = XImage_pixelColor(got, 0, 0);
    XQ_CHECK_MSG(XColor_alpha(&px) == 0, "补边像素必须透明（Qt 先 fill(transparent)）");
    px = XImage_pixelColor(got, 1, 1);
    XQ_CHECK(XColor_red(&px) == 255 && XColor_alpha(&px) == 255);
    XClassDelete((XClass*)got);

    /* PadToEffectiveBoundingRect：跟随 boundingRectFor（blur r=2 → 6px）。 */
    got = XGraphicsEffect_sourcePixmap((XGraphicsEffect*)blur, &offset,
                                       XGraphicsEffectPadMode_PadToEffectiveBoundingRect);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_I64(XImage_width(got), 42);
    XQ_EQ_I64(XImage_height(got), 32);
    XQ_CHECK(offset.x == -6 && offset.y == -6);
    px = XImage_pixelColor(got, 0, 0);
    XQ_CHECK(XColor_alpha(&px) == 0);
    px = XImage_pixelColor(got, 6, 6);
    XQ_CHECK(XColor_red(&px) == 255 && XColor_alpha(&px) == 255);
    XClassDelete((XClass*)got);

    ((XGraphicsEffect*)blur)->m_drawCtx = NULL;
    XClassDelete((XClass*)widget); /* 带走 blur */
    /* base 未挂接：自行释放；snapshot/dest 为用例所有。 */
    XClassDelete((XClass*)base);
    XClassDelete((XClass*)snapshot);
    XClassDelete((XClass*)dest);
}
