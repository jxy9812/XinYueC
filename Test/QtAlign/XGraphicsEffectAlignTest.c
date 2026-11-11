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
    XObject_connect_1((XObject*)effect,
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
    XObject_connect_1((XObject*)first, XSignal(XObject_destroyed_signal),
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
