/******************************************************************************
 * @file       xgui_demo_page_effects.c
 * @brief      XGuiWindowDemo 图形效果页（对标 Qt Widgets 的
 *             QGraphicsEffect 系列示例页）。
 * @details    契约见 xgui_demo_pages.h（demo_page_effects_build /
 *             demo_page_effects_autotest）。页面内容：2x2 四组样例——
 *              ① XPushButton 挂 XGraphicsOpacityEffect（opacity 0.5）；
 *              ② XLabel（Box 边框 + 调色板底色）挂 XGraphicsBlurEffect；
 *              ③ XCheckBox 挂 XGraphicsDropShadowEffect（offset (6,6)）；
 *              ④ 无效果基线：同款三控件原样渲染（按钮/标签/复选框，
 *                与①②③同尺寸，供集成阶段截图像素差分对照）。
 *             每个效果组配"启用效果/禁用效果"一对切换按钮与组标题/
 *             状态 Label；切换经信号槽走 Direct 连接，与主文件手工
 *             setGeometry 几何风格一致（页面内容区约 760x480）。
 *
 *             效果对象所有权（对标 Qt QWidget::setGraphicsEffect 语义，
 *             见 XWidget.h m_graphicsEffect 注释与 XWidget.c 实现）：
 *             控件取得挂接效果的所有权——XWidget_setGraphicsEffect 挂接
 *             新效果前先 delete 旧效果，传 NULL 摘除时也 delete 旧效果，
 *             控件析构时级联 delete。因此本页采用"每次启用新建堆效果
 *             （*_create_ex，内部 Set_Class_IsHeap）、挂接即移交所有权；
 *             禁用经 setGraphicsEffect(NULL) 由控件销毁"的方案，反复
 *             启用/禁用无泄漏，实现文件无需自持效果指针。
 *
 *             autotest 口径：全程非阻塞，仅断言状态（graphicsEffect
 *             getter 非空/为空、效果类型经类虚表指针比对、关键参数
 *             getter、启→禁→启循环后状态正确、状态反馈文本）；
 *             不验证像素——效果呈现的像素差分由集成阶段截图完成
 *             （对照 XGui.md §8.0c/§8.2 的无头目验方法）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XPrintf.h"
#include "XMemory.h"
#include "XObject.h"
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XGraphicsEffect.h"
#include "XGraphicsOpacityEffect.h"
#include "XGraphicsBlurEffect.h"
#include "XGraphicsDropShadowEffect.h"
#include "XPushButton.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XFrame.h"
#include "XPalette.h"
#include "xgui_demo_pages.h"

/*
 * 控件段宏守卫：本页依赖 XWidget 与PushButton/XLabel(XFrame)/XCheckBox
 * 全部样例控件。任一模块被裁剪时降级为契约桩（build 返回 NULL、
 * autotest 返回 -1），保证契约符号始终存在、主文件可无条件链接。
 */
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
    XLABEL_ON && XFRAME_ON && XCHECKBOX_ON

/* ==================== 页面几何常量（内容区约 760x480） ==================== */

/* 2x2 组格：格尺寸 364x224，列 x=12/384，行 y=12/244，组间 8px。 */
#define FX_PAGE_MARGIN   12  /**< 页面外边距。 */
#define FX_CELL_X2       384 /**< 第二列组原点 x。 */
#define FX_CELL_Y2       244 /**< 第二行组原点 y。 */

/* ==================== 页面内部控件登记（demo 单实例 static 自持） ======== */

/**
 * @brief 图形效果页内部控件登记表。
 * @details 控件全部经 *_create 堆创建并挂在页面根控件下（父子链级联
 *          析构），本表只存借用指针；build 时登记、autotest 时使用。
 *          demo 单实例，重复 build 前整体清零（契约约定单次构建）。
 */
static struct
{
    XWidget*         m_root;      /**< 页面根控件（契约返回值；堆对象）。 */
    DemoPageStatusFn m_status;    /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;      /**< 回调上下文（主窗口指针，借用）。 */
    char             m_lastStatus[64]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;     /**< build 已完成且控件指针有效。 */

    /* 组①：透明度效果 + 样例按钮。 */
    XLabel*     m_g0Title;  /**< 组标题。 */
    XLabel*     m_g0State;  /**< 组内状态行。 */
    XPushButton* m_g0Sample;/**< 样例控件（挂 XGraphicsOpacityEffect）。 */
    XPushButton* m_g0On;    /**< 启用效果按钮。 */
    XPushButton* m_g0Off;   /**< 禁用效果按钮。 */

    /* 组②：模糊效果 + 样例标签（Box 边框 + 底色，模糊形变可见）。 */
    XLabel*      m_g1Title; /**< 组标题。 */
    XLabel*      m_g1State; /**< 组内状态行。 */
    XLabel*      m_g1Sample;/**< 样例控件（挂 XGraphicsBlurEffect）。 */
    XPushButton* m_g1On;    /**< 启用效果按钮。 */
    XPushButton* m_g1Off;   /**< 禁用效果按钮。 */

    /* 组③：投影效果 + 样例复选框。 */
    XLabel*      m_g2Title; /**< 组标题。 */
    XLabel*      m_g2State; /**< 组内状态行。 */
    XCheckBox*   m_g2Sample;/**< 样例控件（挂 XGraphicsDropShadowEffect）。 */
    XPushButton* m_g2On;    /**< 启用效果按钮。 */
    XPushButton* m_g2Off;   /**< 禁用效果按钮。 */

    /* 组④：无效果基线（同款三控件、同尺寸，永不挂效果）。 */
    XLabel*      m_g3Title; /**< 组标题。 */
    XLabel*      m_g3State; /**< 组内状态行。 */
    XPushButton* m_g3Button;/**< 基线按钮（对照组①）。 */
    XLabel*      m_g3Label; /**< 基线标签（对照组②，同款边框底色）。 */
    XCheckBox*   m_g3Check; /**< 基线复选框（对照组③）。 */
} s_page;

/* ==================== 内部辅助 ==================== */

/**
 * @brief      向主窗口状态栏反馈并登记最近文本（autotest 断言用）。
 * @param      text 反馈文本；NULL 忽略。
 */
static void fx_report(const char* text)
{
    if (!text) return;
    snprintf(s_page.m_lastStatus, sizeof(s_page.m_lastStatus), "%s", text);
    if (s_page.m_status)
        s_page.m_status(s_page.m_user, text);
}

/**
 * @brief      组内状态行文本更新。
 * @param      state 目标状态 Label；NULL 忽略。
 * @param      text 状态文本。
 */
static void fx_setStateText(XLabel* state, const char* text)
{
    if (state) XLabel_setText_2(state, text);
}

/**
 * @brief      浮点近似比较（autotest 用，避开 math.h 依赖）。
 * @return     |a-b| < 0.001f 返回 true。
 */
static bool fx_fuzzy(float a, float b)
{
    return (a - b) < 0.001f && (b - a) < 0.001f;
}

/**
 * @brief      切换某效果组的效果挂接状态（按钮槽与 autotest 共用路径）。
 * @details    启用：新建堆效果对象并按组配置参数后
 *             XWidget_setGraphicsEffect 挂接（控件取得所有权，旧效果若
 *             存在先被控件销毁）；禁用：setGraphicsEffect(NULL) 摘除并
 *             由控件销毁效果对象。每次启用都新建、禁用即销毁，反复
 *             切换无泄漏（所有权论证见文件头）。
 * @param      group 组索引 0=透明度 1=模糊 2=投影。
 * @param      enable true=启用，false=禁用。
 */
static void fx_groupSetEnabled(int group, bool enable)
{
    XWidget* sample = (XWidget*)0;
    const char* name = "";
    const char* on = "";
    const char* off = "";

    switch (group)
    {
    case 0:
        sample = (XWidget*)s_page.m_g0Sample;
        name = "透明度";
        break;
    case 1:
        sample = (XWidget*)s_page.m_g1Sample;
        name = "模糊";
        break;
    case 2:
        sample = (XWidget*)s_page.m_g2Sample;
        name = "投影";
        break;
    default:
        return;
    }
    if (!sample) return;

    if (enable)
    {
        if (group == 0)
        {
            /* 透明度：opacity 0.5（对标 QGraphicsOpacityEffect::setOpacity）。 */
            XGraphicsOpacityEffect* effect =
                XGraphicsOpacityEffect_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            if (!effect) return;
            XGraphicsOpacityEffect_setOpacity(effect, 0.5f);
            /* 挂接即移交所有权（Qt：QWidget 删除旧效果后安装新效果）。 */
            XWidget_setGraphicsEffect(sample, (XGraphicsEffect*)effect);
        }
        else if (group == 1)
        {
            /* 模糊：blurRadius 保持 API 对齐默认 1.0（实现为固定 3x3
               盒式核，见 XGui.md §8.1 已声明偏差，非缺陷）。 */
            XGraphicsBlurEffect* effect =
                XGraphicsBlurEffect_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            if (!effect) return;
            XWidget_setGraphicsEffect(sample, (XGraphicsEffect*)effect);
        }
        else
        {
            /* 投影：offset (6,6)（对标 QGraphicsDropShadowEffect::
               setOffset；默认 (8,8)，本页取 (6,6) 便于小格内呈现）。 */
            XGraphicsDropShadowEffect* effect =
                XGraphicsDropShadowEffect_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            XPointF offset;
            if (!effect) return;
            XPointF_init(&offset, 6.0f, 6.0f);
            XGraphicsDropShadowEffect_setOffset(effect, offset);
            XWidget_setGraphicsEffect(sample, (XGraphicsEffect*)effect);
        }
        on = "已启用";
    }
    else
    {
        /* 摘除即销毁：setGraphicsEffect(NULL) 由控件 delete 旧效果。 */
        XWidget_setGraphicsEffect(sample, (XGraphicsEffect*)NULL);
        off = "已禁用";
    }
    {
        char text[64];
        snprintf(text, sizeof(text), "%s效果%s", name, enable ? on : off);
        fx_report(text);
        fx_setStateText(group == 0 ? s_page.m_g0State
                        : group == 1 ? s_page.m_g1State : s_page.m_g2State,
                        text);
    }
}

/* ==================== 信号槽（按钮 clicked → 组切换，Direct 连接） ======== */

/** @brief 组①启用按钮 clicked 槽。 */
static void fx_g0OnSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    fx_groupSetEnabled(0, true);
}
/** @brief 组①禁用按钮 clicked 槽。 */
static void fx_g0OffSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    fx_groupSetEnabled(0, false);
}
/** @brief 组②启用按钮 clicked 槽。 */
static void fx_g1OnSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    fx_groupSetEnabled(1, true);
}
/** @brief 组②禁用按钮 clicked 槽。 */
static void fx_g1OffSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    fx_groupSetEnabled(1, false);
}
/** @brief 组③启用按钮 clicked 槽。 */
static void fx_g2OnSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    fx_groupSetEnabled(2, true);
}
/** @brief 组③禁用按钮 clicked 槽。 */
static void fx_g2OffSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    fx_groupSetEnabled(2, false);
}

/* ==================== 控件装配助手 ==================== */

/**
 * @brief      装配一个效果组的标题/状态行与一对切换按钮。
 * @param      cx 组格原点 x；cy 组格原点 y。
 * @param      titleText 组标题。
 * @param      pTitle/pState 输出标题/状态 Label 指针。
 * @param      pOn/pOff 输出启用/禁用按钮指针。
 * @param      onSlot/offSlot 启用/禁用 clicked 槽。
 * @return     成功返回 true；任一控件创建失败返回 false。
 */
static bool fx_buildGroup(int cx, int cy, const char* titleText,
                          XLabel** pTitle, XLabel** pState,
                          XPushButton** pOn, XPushButton** pOff,
                          void (*onSlot)(XObject*, XVarList*),
                          void (*offSlot)(XObject*, XVarList*))
{
    XLabel* title = XLabel_create((XWidget*)s_page.m_root, 0);
    XLabel* state = XLabel_create((XWidget*)s_page.m_root, 0);
    XPushButton* on = XPushButton_create((XWidget*)s_page.m_root, 0);
    XPushButton* off = XPushButton_create((XWidget*)s_page.m_root, 0);
    if (!title || !state || !on || !off) return false;

    XLabel_setText_2(title, titleText);
    XLabel_setTextPixelSize(title, 14);
    XLabel_setAlignment(title, XAlignment_Left | XAlignment_VCenter);
    XWidget_setGeometry((XWidget*)title, cx + 8, cy + 6, 348, 20);
    XWidget_show((XWidget*)title);

    XLabel_setText_2(state, "效果未启用");
    XLabel_setTextPixelSize(state, 12);
    XLabel_setAlignment(state, XAlignment_Left | XAlignment_VCenter);
    XWidget_setGeometry((XWidget*)state, cx + 8, cy + 30, 348, 16);
    XWidget_show((XWidget*)state);

    XPushButton_setText_2(on, "启用效果");
    XWidget_setGeometry((XWidget*)on, cx + 8, cy + 182, 110, 30);
    XObject_connect_1((XObject*)on,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)s_page.m_root, onSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)on);

    XPushButton_setText_2(off, "禁用效果");
    XWidget_setGeometry((XWidget*)off, cx + 128, cy + 182, 110, 30);
    XObject_connect_1((XObject*)off,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)s_page.m_root, offSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)off);

    *pTitle = title;
    *pState = state;
    *pOn = on;
    *pOff = off;
    return true;
}

#if XPALETTE_ON
/**
 * @brief      给样例/基线标签上底色（调色板 Window 角色 + 自动填充）。
 * @details    对标 Qt：label->setAutoFillBackground(true) 后经调色板
 *             Window 角色换色；底色让模糊效果的前后差异肉眼可辨。
 * @param      label 目标标签。
 */
static void fx_labelFillBackground(XLabel* label)
{
    XPalette palette;
    XColor color;
    XPalette_init_default(&palette);
    XColor_init_rgb(&color, 70, 130, 220, 255); /* 钢蓝底色。 */
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, color);
    XWidget_setPalette((XWidget*)label, &palette);
    XWidget_setAutoFillBackground((XWidget*)label, true);
}
#endif /* XPALETTE_ON */

/**
 * @brief      装配"带底色/边框样式"的样例标签（组②样例与组④基线同款）。
 * @param      x/y/w/h 几何（页面局部坐标）。
 * @param      text 标签文本。
 * @return     标签指针；创建失败返回 NULL。
 */
static XLabel* fx_buildStyledLabel(int x, int y, int w, int h,
                                   const char* text)
{
    XLabel* label = XLabel_create((XWidget*)s_page.m_root, 0);
    if (!label) return (XLabel*)0;
    XLabel_setText_2(label, text);
    XLabel_setAlignment(label, XAlignment_Center);
    /* 对标 Qt QLabel : QFrame 的边框样式（Box | Plain，线宽 2）。 */
    XFrame_setFrameShape((XFrame*)label, XFrameShape_Box);
    XFrame_setFrameShadow((XFrame*)label, XFrameShadow_Plain);
    XFrame_setLineWidth((XFrame*)label, 2);
#if XPALETTE_ON
    fx_labelFillBackground(label);
#endif
    XWidget_setGeometry((XWidget*)label, x, y, w, h);
    XWidget_show((XWidget*)label);
    return label;
}

/* ==================== 契约实现：页面构建 ==================== */

/**
 * @brief      构建图形效果页（契约见 xgui_demo_pages.h）。
 * @details    2x2 四组样例；控件指针登记进 s_page，autotest 复用。
 *             效果初始全部未挂接（基线状态），由用户/autotest 切换。
 * @param      parent 主窗口页面容器；可为 NULL（控件挂到顶层）。
 * @param      status 状态栏反馈回调；可为 NULL。
 * @param      user 回调上下文。
 * @return     页面根控件（堆对象，父子链级联析构）；失败返回 NULL。
 */
XWidget* demo_page_effects_build(XWidget* parent,
                                 DemoPageStatusFn status, void* user)
{
    /* demo 单实例：重复 build 前清空登记表（旧页面随旧父链析构）。 */
    memset(&s_page, 0, sizeof(s_page));
    s_page.m_status = status;
    s_page.m_user = user;

    s_page.m_root = XWidget_create(parent, 0);
    if (!s_page.m_root) return (XWidget*)0;

    /* 组①：透明度效果 + 样例按钮（左上）。 */
    s_page.m_g0Sample = XPushButton_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_g0Sample ||
        !fx_buildGroup(FX_PAGE_MARGIN, FX_PAGE_MARGIN,
                       "① 透明度 XGraphicsOpacityEffect",
                       &s_page.m_g0Title, &s_page.m_g0State,
                       &s_page.m_g0On, &s_page.m_g0Off,
                       fx_g0OnSlot, fx_g0OffSlot))
        return s_page.m_root;
    XPushButton_setText_2(s_page.m_g0Sample, "透明按钮");
    XWidget_setGeometry((XWidget*)s_page.m_g0Sample,
                        FX_PAGE_MARGIN + 24, FX_PAGE_MARGIN + 58, 180, 40);
    XWidget_show((XWidget*)s_page.m_g0Sample);

    /* 组②：模糊效果 + 样例标签（右上，Box 边框 + 底色）。 */
    if (!fx_buildGroup(FX_CELL_X2, FX_PAGE_MARGIN,
                       "② 模糊 XGraphicsBlurEffect（3x3 盒式核）",
                       &s_page.m_g1Title, &s_page.m_g1State,
                       &s_page.m_g1On, &s_page.m_g1Off,
                       fx_g1OnSlot, fx_g1OffSlot))
        return s_page.m_root;
    s_page.m_g1Sample = fx_buildStyledLabel(
        FX_CELL_X2 + 24, FX_PAGE_MARGIN + 54, 220, 52, "模糊标签");
    if (!s_page.m_g1Sample) return s_page.m_root;

    /* 组③：投影效果 + 样例复选框（左下）。 */
    s_page.m_g2Sample = XCheckBox_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_g2Sample ||
        !fx_buildGroup(FX_PAGE_MARGIN, FX_CELL_Y2,
                       "③ 投影 XGraphicsDropShadowEffect",
                       &s_page.m_g2Title, &s_page.m_g2State,
                       &s_page.m_g2On, &s_page.m_g2Off,
                       fx_g2OnSlot, fx_g2OffSlot))
        return s_page.m_root;
    XCheckBox_setText_2(s_page.m_g2Sample, "投影复选框");
    XWidget_setGeometry((XWidget*)s_page.m_g2Sample,
                        FX_PAGE_MARGIN + 24, FX_CELL_Y2 + 62, 220, 26);
    XWidget_show((XWidget*)s_page.m_g2Sample);

    /* 组④：无效果基线（右下；三控件同款同尺寸，永不挂效果，
       供集成阶段截图像素对照）。 */
    s_page.m_g3Title = XLabel_create((XWidget*)s_page.m_root, 0);
    s_page.m_g3State = XLabel_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_g3Title || !s_page.m_g3State) return s_page.m_root;
    XLabel_setText_2(s_page.m_g3Title, "④ 无效果基线（像素对照）");
    XLabel_setTextPixelSize(s_page.m_g3Title, 14);
    XLabel_setAlignment(s_page.m_g3Title, XAlignment_Left | XAlignment_VCenter);
    XWidget_setGeometry((XWidget*)s_page.m_g3Title,
                        FX_CELL_X2 + 8, FX_CELL_Y2 + 6, 348, 20);
    XWidget_show((XWidget*)s_page.m_g3Title);
    XLabel_setText_2(s_page.m_g3State, "原样渲染，无效果挂接");
    XLabel_setTextPixelSize(s_page.m_g3State, 12);
    XLabel_setAlignment(s_page.m_g3State, XAlignment_Left | XAlignment_VCenter);
    XWidget_setGeometry((XWidget*)s_page.m_g3State,
                        FX_CELL_X2 + 8, FX_CELL_Y2 + 30, 348, 16);
    XWidget_show((XWidget*)s_page.m_g3State);

    s_page.m_g3Button = XPushButton_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_g3Button) return s_page.m_root;
    XPushButton_setText_2(s_page.m_g3Button, "透明按钮");
    XWidget_setGeometry((XWidget*)s_page.m_g3Button,
                        FX_CELL_X2 + 24, FX_CELL_Y2 + 52, 180, 40);
    XWidget_show((XWidget*)s_page.m_g3Button);

    s_page.m_g3Label = fx_buildStyledLabel(
        FX_CELL_X2 + 24, FX_CELL_Y2 + 100, 220, 52, "模糊标签");
    if (!s_page.m_g3Label) return s_page.m_root;

    s_page.m_g3Check = XCheckBox_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_g3Check) return s_page.m_root;
    XCheckBox_setText_2(s_page.m_g3Check, "投影复选框");
    XWidget_setGeometry((XWidget*)s_page.m_g3Check,
                        FX_CELL_X2 + 24, FX_CELL_Y2 + 160, 220, 26);
    XWidget_show((XWidget*)s_page.m_g3Check);

    s_page.m_ready = true;
    return s_page.m_root;
}

/* ==================== 契约实现：页面自测 ==================== */

/**
 * @brief      断言输出宏（与主文件 demo_input_autotest 口径一致）。
 */
#define FX_EXPECT(cond, what) \
    do { \
        if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
        else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } \
    } while (0)

/**
 * @brief      图形效果页自动化验证（契约见 xgui_demo_pages.h）。
 * @details    全程非阻塞、仅状态断言（不做像素差分——效果呈现的像素
 *             验证由集成阶段截图完成）：每组启用路径断言 getter 非空 +
 *             效果类型（类虚表指针比对）+ 关键参数；禁用路径断言 getter
 *             为 NULL；再断言 启→禁→启 循环后状态正确（同时验证效果
 *             对象反复创建/销毁无崩溃）；基线组断言永无效果；状态反馈
 *             文本经 fx_report 登记断言。autotest 结束后三个效果组保持
 *             启用态，便于集成阶段把"启用效果 + 基线"同屏截图对照。
 * @param      page 页面根控件（须为 demo_page_effects_build 的返回值）。
 * @return     失败断言数（0=全过；page 不符或未构建返回 -1）。
 */
int demo_page_effects_autotest(XWidget* page)
{
    int failures = 0;
    XGraphicsEffect* eff = (XGraphicsEffect*)0;

    if (!page || !s_page.m_ready || page != s_page.m_root)
        return -1;

    /* ---- 组① 透明度效果（样例按钮） ---- */
    fx_groupSetEnabled(0, true);
    eff = XWidget_graphicsEffect((XWidget*)s_page.m_g0Sample);
    FX_EXPECT(eff && XClassGetVtable(eff) ==
                  XGraphicsOpacityEffect_class_init(),
              "透明度：启用后挂接 XGraphicsOpacityEffect");
    FX_EXPECT(eff && fx_fuzzy(
                  XGraphicsOpacityEffect_opacity(
                      (const XGraphicsOpacityEffect*)eff), 0.5f),
              "透明度：opacity=0.5");
    fx_groupSetEnabled(0, false);
    FX_EXPECT(XWidget_graphicsEffect((XWidget*)s_page.m_g0Sample) == NULL,
              "透明度：禁用后 graphicsEffect 为 NULL");
    fx_groupSetEnabled(0, true);
    eff = XWidget_graphicsEffect((XWidget*)s_page.m_g0Sample);
    FX_EXPECT(eff && fx_fuzzy(
                  XGraphicsOpacityEffect_opacity(
                      (const XGraphicsOpacityEffect*)eff), 0.5f),
              "透明度：启→禁→启后重新挂接且参数一致");
    FX_EXPECT(strcmp(s_page.m_lastStatus, "透明度效果已启用") == 0,
              "透明度：状态反馈文本");
    /* 保持启用态（供集成阶段截图）。 */

    /* ---- 组② 模糊效果（样例标签） ---- */
    fx_groupSetEnabled(1, true);
    eff = XWidget_graphicsEffect((XWidget*)s_page.m_g1Sample);
    FX_EXPECT(eff && XClassGetVtable(eff) ==
                  XGraphicsBlurEffect_class_init(),
              "模糊：启用后挂接 XGraphicsBlurEffect");
    FX_EXPECT(eff && fx_fuzzy(
                  XGraphicsBlurEffect_blurRadius(
                      (const XGraphicsBlurEffect*)eff), 1.0f),
              "模糊：blurRadius=1.0（API 对齐默认）");
    fx_groupSetEnabled(1, false);
    FX_EXPECT(XWidget_graphicsEffect((XWidget*)s_page.m_g1Sample) == NULL,
              "模糊：禁用后 graphicsEffect 为 NULL");
    fx_groupSetEnabled(1, true);
    FX_EXPECT(XWidget_graphicsEffect((XWidget*)s_page.m_g1Sample) != NULL,
              "模糊：启→禁→启后重新挂接");
    FX_EXPECT(strcmp(s_page.m_lastStatus, "模糊效果已启用") == 0,
              "模糊：状态反馈文本");
    /* 保持启用态（供集成阶段截图）。 */

    /* ---- 组③ 投影效果（样例复选框） ---- */
    fx_groupSetEnabled(2, true);
    eff = XWidget_graphicsEffect((XWidget*)s_page.m_g2Sample);
    FX_EXPECT(eff && XClassGetVtable(eff) ==
                  XGraphicsDropShadowEffect_class_init(),
              "投影：启用后挂接 XGraphicsDropShadowEffect");
    {
        XPointF offset = XGraphicsDropShadowEffect_offset(
            (const XGraphicsDropShadowEffect*)eff);
        FX_EXPECT(eff && fx_fuzzy(offset.x, 6.0f) && fx_fuzzy(offset.y, 6.0f),
                  "投影：offset=(6,6)");
    }
    fx_groupSetEnabled(2, false);
    FX_EXPECT(XWidget_graphicsEffect((XWidget*)s_page.m_g2Sample) == NULL,
              "投影：禁用后 graphicsEffect 为 NULL");
    fx_groupSetEnabled(2, true);
    {
        XPointF offset = XGraphicsDropShadowEffect_offset(
            (const XGraphicsDropShadowEffect*)XWidget_graphicsEffect(
                (XWidget*)s_page.m_g2Sample));
        FX_EXPECT(fx_fuzzy(offset.x, 6.0f) && fx_fuzzy(offset.y, 6.0f),
                  "投影：启→禁→启后 offset 保持 (6,6)");
    }
    FX_EXPECT(strcmp(s_page.m_lastStatus, "投影效果已启用") == 0,
              "投影：状态反馈文本");
    /* 保持启用态（供集成阶段截图）。 */

    /* ---- 组④ 无效果基线（永无效果；像素对照由集成阶段完成） ---- */
    FX_EXPECT(XWidget_graphicsEffect((XWidget*)s_page.m_g3Button) == NULL,
              "基线：按钮无效果挂接");
    FX_EXPECT(XWidget_graphicsEffect((XWidget*)s_page.m_g3Label) == NULL,
              "基线：标签无效果挂接");
    FX_EXPECT(XWidget_graphicsEffect((XWidget*)s_page.m_g3Check) == NULL,
              "基线：复选框无效果挂接");

    XPrintf("XGuiAutoTest: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#undef FX_EXPECT

#else /* 依赖裁剪：契约桩（符号恒存在） */

/** @brief 裁剪哨兵：控件依赖全关时本翻译单元仅余契约桩与该类型。 */
typedef int xgui_demo_page_effects_stub_t;

XWidget* demo_page_effects_build(XWidget* parent,
                                 DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    XPrintf("XGuiAutoTest: [FAIL] 图形效果页依赖控件被裁剪，无法构建\n");
    return (XWidget*)0;
}

int demo_page_effects_autotest(XWidget* page)
{
    (void)page;
    XPrintf("XGuiAutoTest: [FAIL] 图形效果页依赖控件被裁剪，无法自测\n");
    return -1;
}

#endif /* XWIDGET_ON && ... 控件段宏守卫 */
