/**
 * @file       XStatusBarAlignTest.c
 * @brief      XStatusBar × Qt 6.8 QStatusBar 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qstatusbar.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/widgets/qstatusbar.cpp。
 *             本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认 sizeGripEnabled=true、currentMessage() 为空；
 *             - messageChanged 只在消息**真正变化**时发射：showMessage
 *               同文不再发；clearMessage 在消息已空时**不发射**
 *               （qstatusbar.cpp:517-528 的 `if (d->tempItem.isEmpty())
 *               return;`，探针 clear 连调两次第二次计数不增）；
 *             - 临时消息在场时普通区控件隐藏、永久区控件不受影响
 *               （qstatusbar.cpp:555+ hideOrShow）；
 *             - insertWidget/insertPermanentWidget 越界时告警并追加，
 *               返回值是各自区段内的落位索引（探针 0 / 1）；
 *             - sizeGrip：构造即创建真 QSizeGrip 手柄子控件
 *               （qstatusbar.cpp:199-200 构造尾 setSizeGripEnabled(true)）；
 *               isSizeGripEnabled = !!d->resizer（qstatusbar.cpp:376-383，
 *               P3-32）；禁用即删除手柄（qstatusbar.cpp:386-407）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XSTATUSBAR_ON

#include "XStatusBar.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XString.h"
#include "XVarList.h"
#include "XClass.h"
#include "XContainer.h"
#include "XVector.h"

#if XSIZEGRIP_ON
#include "XSizeGrip.h" /* P3-31：真手柄子控件断言（!!d->resizer 口径）。 */
#endif

/** @brief 按虚表在子表中找 XSizeGrip（XDialogAlignTest 同款扫描）。 */
#if XSIZEGRIP_ON
static XSizeGrip* xsbTest_findGrip(const XStatusBar* bar)
{
    XObject* object;
    int i;
    int count;
    if (!bar) return NULL;
    object = (XObject*)(void*)bar;
    if (!object->m_children) return NULL;
    count = XVector_size_base((const XContainer*)object->m_children);
    for (i = 0; i < count; ++i) {
        XObject* const* children =
            (XObject* const*)XContainerDataAddr(object->m_children);
        if (children[i] && children[i]->m_is_widget &&
            XClassGetVtable(children[i]) == XSizeGrip_class_init())
            return (XSizeGrip*)children[i];
    }
    return NULL;
}
#endif

/** @brief messageChanged 到达次数与最近文本。 */
static int g_xsbMsgCount;
static char g_xsbLastMsg[64];

/** @brief messageChanged 槽（XStatusBar 载荷是 XString*）。
 *  @note  空串经 XString_toUtf8 返回 NULL（空串无 UTF-8 视图），
 *         此处归一为 ""，与 Qt 的 messageChanged(QString())。 */
static void xsb_msgTextSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_xsbMsgCount;
    g_xsbLastMsg[0] = '\0';
    if (!args) return;
    {
        XVarList_args_1(args, XString*, text);
        if (text) {
            const char* utf8 = XString_toUtf8(text);
            if (utf8) {
                size_t i = 0;
                while (utf8[i] && i + 1 < sizeof(g_xsbLastMsg)) {
                    g_xsbLastMsg[i] = utf8[i];
                    ++i;
                }
                g_xsbLastMsg[i] = '\0';
            }
        }
    }
}

/* ==================== ① 默认值 + 消息生命周期 + 信号时机 ==================== */

XQ_TEST(XStatusBar, defaultsAndMessageLifecycle)
{
    XStatusBar* bar = XStatusBar_create(NULL, 0);
    XWidget* normal;

    XQ_REQUIRE(bar != NULL);
    XWidget_resize((XWidget*)bar, 320, 22);

    /* 构造默认（QStatusBar Q_PROPERTY 缺省 + 探针）。 */
    XQ_CHECK(XStatusBar_isSizeGripEnabled(bar));
    XQ_EQ_STR(XStatusBar_currentMessage(bar), "");
    XQ_EQ_STR(XStatusBar_currentMessage(NULL), "");

    normal = XWidget_create(NULL, 0);
    XQ_REQUIRE(normal != NULL);
    XStatusBar_addWidget(bar, normal, 0);
    XQ_CHECK(XObject_parent((XObject*)normal) == (XObject*)bar);
    XQ_CHECK(!XWidget_isHidden(normal));

    g_xsbMsgCount = 0;
    g_xsbLastMsg[0] = '\0';
    XObject_connect((XObject*)bar,
                      (size_t)XStatusBar_messageChanged_signal(NULL, NULL),
                      (XObject*)bar, xsb_msgTextSlot,
                      XConnectionType_Direct);

    /* 空消息时 Qt 6.8 clearMessage 直接返回、不发射 messageChanged
     * （qstatusbar.cpp:517-521 `if (d->tempItem.isEmpty()) return;`；
     * 本机 Qt 6.8 探针：空态 clear 计数 0）。 */
    XStatusBar_clearMessage(bar);
    XQ_EQ_I64(g_xsbMsgCount, 0);

    /* showMessage：消息在场 → 普通区控件隐藏；同文再发不重发信号。 */
    XStatusBar_showMessage(bar, "就绪", 0);
    XQ_EQ_STR(XStatusBar_currentMessage(bar), "就绪");
    XQ_EQ_I64(g_xsbMsgCount, 1);
    XQ_EQ_STR(g_xsbLastMsg, "就绪");
    XQ_CHECK(XWidget_isHidden(normal));
    XStatusBar_showMessage(bar, "就绪", 0);
    XQ_EQ_I64(g_xsbMsgCount, 1); /* 同文早退（qstatusbar.cpp:504-505） */

    /* 换成另一条消息：普通区仍隐藏、信号 +1。 */
    XStatusBar_showMessage(bar, "处理中", 0);
    XQ_EQ_I64(g_xsbMsgCount, 2);
    XQ_EQ_STR(g_xsbLastMsg, "处理中");
    XQ_CHECK(XWidget_isHidden(normal));

    /* 空文本 = 清除（等价 clearMessage）：信号 +1、普通区恢复。 */
    XStatusBar_showMessage(bar, "", 0);
    XQ_EQ_STR(XStatusBar_currentMessage(bar), "");
    XQ_EQ_I64(g_xsbMsgCount, 3);
    XQ_EQ_STR(g_xsbLastMsg, "");
    XQ_CHECK(!XWidget_isHidden(normal));

    /* 已空后再清除：Qt 早退、不发射（探针：clear 已空计数不再增）。 */
    XStatusBar_clearMessage(bar);
    XQ_EQ_I64(g_xsbMsgCount, 3);
    XStatusBar_clearMessage(bar);
    XQ_EQ_I64(g_xsbMsgCount, 3);

    /* NULL 安全。 */
    XStatusBar_showMessage(NULL, "x", 0);
    XStatusBar_clearMessage(NULL);
    XQ_EQ_I64(XStatusBar_currentMessage(NULL)[0], '\0');

    XClassDelete((XClass*)bar);
}

/* ==================== ② 普通区/永久区 + 尺寸手柄 + 移除语义 ==================== */

XQ_TEST(XStatusBar, permanentWidgetsAndSizeGrip)
{
    XStatusBar* bar = XStatusBar_create(NULL, 0);
    XWidget* w1;
    XWidget* w2;
    XWidget* w3;
    XWidget* perm;

    XQ_REQUIRE(bar != NULL);
    XWidget_resize((XWidget*)bar, 320, 22);

    w1 = XWidget_create(NULL, 0);
    w2 = XWidget_create(NULL, 0);
    w3 = XWidget_create(NULL, 0);
    perm = XWidget_create(NULL, 0);
    XQ_REQUIRE(w1 != NULL && w2 != NULL && w3 != NULL && perm != NULL);

    /* 普通区插入：越界索引被夹到有效区间并返回落位索引
       （qstatusbar.cpp:255-278 `index = idx + 1` 的追加口径）。 */
    XStatusBar_addWidget(bar, w1, 0);              /* [w1] */
    XQ_EQ_I64(XStatusBar_insertWidget(bar, 0, w2, 0), 0); /* [w2,w1] */
    XQ_EQ_I64(XStatusBar_insertWidget(bar, 99, w3, 0), 2); /* 追加 */
    XQ_EQ_I64(XStatusBar_insertWidget(NULL, 0, w1, 0), -1);
    XQ_EQ_I64(XStatusBar_insertWidget(bar, 0, NULL, 0), -1);

    /* 永久区：插到最右且不受临时消息遮挡（qstatusbar.cpp:294-339）。 */
    XQ_EQ_I64(XStatusBar_insertPermanentWidget(bar, 0, perm, 0), 0);
    XQ_EQ_I64(XStatusBar_insertPermanentWidget(bar, 0, NULL, 0), -1);
    XQ_CHECK(XObject_parent((XObject*)perm) == (XObject*)bar);
    XQ_CHECK(!XWidget_isHidden(perm));

    XStatusBar_showMessage(bar, "临时消息", 0);
    XQ_CHECK(XWidget_isHidden(w1));
    XQ_CHECK(XWidget_isHidden(w2));
    XQ_CHECK(XWidget_isHidden(w3));
    XQ_CHECK(!XWidget_isHidden(perm)); /* 永久区不被消息遮挡 */
    XStatusBar_clearMessage(bar);
    XQ_CHECK(!XWidget_isHidden(w1));
    XQ_CHECK(!XWidget_isHidden(w2));

    /* removeWidget：摘除登记（普通区/永久区都查），**只 hide、保留父子
       关系、不销毁**（对标 qstatusbar.cpp:351-365；本机 Qt 6.8 探针：
       parent() 仍为状态栏、isHidden()==true；gaps 199）。 */
    XStatusBar_removeWidget(bar, perm);
    XQ_CHECK(XObject_parent((XObject*)perm) == (XObject*)bar);
    XQ_CHECK(XWidget_isHidden(perm));
    XStatusBar_removeWidget(bar, perm); /* 不在登记表内：无操作 */
    XStatusBar_removeWidget(bar, w1);
    XQ_CHECK(XObject_parent((XObject*)w1) == (XObject*)bar);
    XQ_CHECK(XWidget_isHidden(w1));
    XStatusBar_removeWidget(bar, w2);
    XStatusBar_removeWidget(bar, w3);
    XStatusBar_removeWidget(NULL, w1);

    /* 尺寸手柄开关（默认 true，Q_PROPERTY 往返）+ 真手柄子控件
     * （P3-31/32：构造即创建 XSizeGrip（qstatusbar.cpp:199-200），
     * getter = !!手柄（qstatusbar.cpp:376-383），禁用即删除手柄）。 */
    XQ_CHECK(XStatusBar_isSizeGripEnabled(bar));
#if XSIZEGRIP_ON
    {
        XSizeGrip* grip = xsbTest_findGrip(bar);
        XQ_CHECK(grip != NULL);
        if (grip) {
            /* 贴右下角（qdialog.cpp:1052-1066 同款 bottomRight 口径；
             * bar 当前 320×22）。 */
            XQ_EQ_I64(XWidget_x((XWidget*)grip),
                      320 - XWidget_width((XWidget*)grip));
            XQ_EQ_I64(XWidget_y((XWidget*)grip),
                      22 - XWidget_height((XWidget*)grip));
            /* resize 后重摆（resizeEvent → xsb_layoutItems）。 */
            XWidget_resize((XWidget*)bar, 400, 24);
            XQ_EQ_I64(XWidget_x((XWidget*)grip),
                      400 - XWidget_width((XWidget*)grip));
            XQ_EQ_I64(XWidget_y((XWidget*)grip),
                      24 - XWidget_height((XWidget*)grip));
        }
    }
    XStatusBar_setSizeGripEnabled(bar, false);
    XQ_CHECK(!XStatusBar_isSizeGripEnabled(bar));
    XQ_CHECK(xsbTest_findGrip(bar) == NULL); /* 禁用即删除手柄子控件。 */
    XStatusBar_setSizeGripEnabled(bar, true);
    XQ_CHECK(XStatusBar_isSizeGripEnabled(bar));
    XQ_CHECK(xsbTest_findGrip(bar) != NULL);
#else
    /* 裁剪构建（XSIZEGRIP_ON=0）：仅存位往返。 */
    XStatusBar_setSizeGripEnabled(bar, false);
    XQ_CHECK(!XStatusBar_isSizeGripEnabled(bar));
    XStatusBar_setSizeGripEnabled(bar, true);
    XQ_CHECK(XStatusBar_isSizeGripEnabled(bar));
#endif
    XQ_CHECK(!XStatusBar_isSizeGripEnabled(NULL));

    /* removeWidget 保留父子关系 ⇒ 控件随状态栏级联回收，不再单独释放
       （单独删除会与父析构双释放；手柄子控件同理随本栏级联回收）。 */
    XClassDelete((XClass*)bar);
}

#endif /* XGUI_ON && XWIDGET_ON && XSTATUSBAR_ON */
