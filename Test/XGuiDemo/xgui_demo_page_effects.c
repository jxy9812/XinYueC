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
 *             状态 Label；切换经信号槽走 Direct 连接。页内装配走布局
 *             容器（2026-10-17 容器化）：页根挂垂直盒两行栅格（行盒=
 *             水平盒两格 stretch 均分；格容器定高 224，格内垂直盒=
 *             标题/状态/样例 + 伸缩 + 按钮行贴底），页面内容区约
 *             760x480 设计口径由边距/间距/定尺复刻。
 *
 *             [竖屏单列档 2026-10-25] 真机口径（手机竖屏 ≈400 逻辑宽）
 *             下两列 stretch 均分栅格每格仅 ~184px，而每格最小包络
 *             ≈246px（启/禁钮行 110x2+间距+格边距），右列（②④）整列
 *             越界被内容视口硬裁=内容丢失；adapt 按页根有效宽分档收口
 *             （<600 翻垂直单列+格解除定高交布局协商+触控目标抬 40+
 *             半宽格标题字级收缩），宽屏档逐项还原装配口径、桌面
 *             1000x700 视觉零变化（见 fx_applyLayoutTier / adapt）。
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
#include "XBoxLayout.h" /* 页内布局容器化：页根垂直盒 + 行盒/格盒嵌套。 */
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

/* [布局容器化 2026-10-17] 2x2 组格原点/列位手写坐标（CELL_X2=384/
 * CELL_Y2=244）退场：栅格改由页根垂直盒表达——边距 12 + 行/列间距 8
 * 精确复刻原行列原点（12+224+8=244、12+364+8=384）；列宽由行盒内两
 * 格 stretch 均分（原格宽 364 ≈ (760-2*12-8)/2），格高保持 224 定尺
 * （格容器 setFixedHeight，纵向不随页根伸缩，多余空间留页底=原观感）。 */
#define FX_PAGE_MARGIN   12  /**< 页面外边距 = 行/列间距（原 2x2 栅格口径）。 */
#define FX_CELL_GAP       8  /**< 行/列间距（原列原点 384=12+364+8 推得）。 */
#define FX_CELL_HEIGHT  224  /**< 格容器固定高（原格纵向包络 12..236）。 */

/* [竖屏单列档 2026-10-25] 阈值与主窗口 compact 断点（xgui_window_demo.c
 * DEMO_COMPACT_MAX_W=600）同口径：两列栅格每格最小包络 ≈246px，页根
 * 有效宽低于 600 时两列在任何可达窗宽下都放不下启/禁钮行（600 宽每格
 * 仅 284），整段切单列交竖排承载。 */
#define FX_STACK_MAX_W  600  /**< 单列档阈值（页根有效宽 < 此值切单列）。 */
#define FX_TOUCH_H       40  /**< 单列档触控目标下限（可点控件定高）。 */
/* 标题行不换行无省略，全名最长为组②「② 模糊 XGraphicsBlurEffect
 * （3x3 盒式核）」≈296px@14 号；格内可用宽低于该阈值降到 12 号
 * （≈256px），半宽格（两列档 600~700 窗宽带）不再被硬截。 */
#define FX_TITLE_SHRINK_W 320 /**< 标题字级收缩的格内可用宽阈值。 */

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

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* 页内布局容器（2026-10-17 容器化）：布局器须随页常驻
     * （XWidget_setLayout 仅存借用指针不取所有权），全部存入本静态
     * 登记表；行盒/按钮行盒经 XBoxLayout_addLayout 借挂父盒（借用
     * 不取所有权），格垂直盒经 XWidget_setLayout 挂格容器。 */
    XBoxLayout m_rootLayout;     /**< 页根垂直盒（两行栅格）。 */
    XBoxLayout m_rowLayouts[2];  /**< 行水平盒池（每行两格 stretch 均分）。 */
    XBoxLayout m_cellLayouts[4]; /**< 格垂直盒池（标题/状态/样例/伸缩/按钮行）。 */
    XBoxLayout m_btnRows[3];     /**< 启/禁按钮行水平盒池（组①②③）。 */
    XWidget*   m_cells[4];       /**< 格容器（setFixedHeight 复刻原 224 格高）。 */
    bool       m_compact;        /**< 单列档生效中（宽度分档见 adapt，未越档短路）。 */
    int        m_titleSize;      /**< 标题行现字级（adapt 连续收放去重；0=未同步）。 */
#endif
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
 * @details    [布局容器化 2026-10-17] 原手工 setGeometry（cx/cy 原点
 *             推算）退场：控件只留定尺（标题定高 20、状态行定高 16、
 *             切换钮定尺 110x30），原点/行宽交格内垂直盒布局器分配。
 * @param      cell 格容器（控件挂其下，由格盒排布）。
 * @param      titleText 组标题。
 * @param      pTitle/pState 输出标题/状态 Label 指针。
 * @param      pOn/pOff 输出启用/禁用按钮指针。
 * @param      onSlot/offSlot 启用/禁用 clicked 槽。
 * @return     成功返回 true；任一控件创建失败返回 false。
 */
static bool fx_buildGroup(XWidget* cell, const char* titleText,
                          XLabel** pTitle, XLabel** pState,
                          XPushButton** pOn, XPushButton** pOff,
                          void (*onSlot)(XObject*, XVarList*),
                          void (*offSlot)(XObject*, XVarList*))
{
    XLabel* title = XLabel_create(cell, 0);
    XLabel* state = XLabel_create(cell, 0);
    XPushButton* on = XPushButton_create(cell, 0);
    XPushButton* off = XPushButton_create(cell, 0);
    if (!title || !state || !on || !off) return false;

    XLabel_setText_2(title, titleText);
    XLabel_setTextPixelSize(title, 14);
    XLabel_setAlignment(title, XAlignment_Left | XAlignment_VCenter);
    /* 原手写 (cx+8,cy+6,348,20) → 定高 20（宽随格宽伸展），原点交格
     * 盒边距 (8,6,8,12)。 */
    XWidget_setFixedHeight((XWidget*)title, 20);
    XWidget_show((XWidget*)title);

    XLabel_setText_2(state, "效果未启用");
    XLabel_setTextPixelSize(state, 12);
    XLabel_setAlignment(state, XAlignment_Left | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)state, 16);
    XWidget_show((XWidget*)state);

    XPushButton_setText_2(on, "启用效果");
    XWidget_setFixedWidth((XWidget*)on, 110);
    XWidget_setFixedHeight((XWidget*)on, 30);
    XObject_connect_1((XObject*)on,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)s_page.m_root, onSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)on);

    XPushButton_setText_2(off, "禁用效果");
    XWidget_setFixedWidth((XWidget*)off, 110);
    XWidget_setFixedHeight((XWidget*)off, 30);
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

#if XLAYOUT_ON && XLAYOUT_BOX_ON
/**
 * @brief      格垂直盒装配（组①②③共用）：标题/状态/样例 + 伸缩 +
 *             启/禁按钮行贴底（组④无按钮行，由 build 直接装配）。
 * @details    格盒边距 (8,6,8,12) 复刻原 (cx+8,cy+6) 原点与格底余量
 *             （224-212=12）；条目间距 4 复刻原标题-状态行间隙
 *             （30-26）；按钮行间距 10 复刻原 Off 列位（On 右缘 +10）；
 *             伸缩空白把按钮行压贴格底（原按钮行 y=格+182=224-12-30）。
 *             格盒挂格容器（m_cells[celli] 定高 224）。
 */
static void fx_cellAssemble(int celli, XLabel* title, XLabel* state,
                            XWidget* sample,
                            XPushButton* on, XPushButton* off)
{
    XBoxLayout* cellBox = &s_page.m_cellLayouts[celli];
    XBoxLayout* btnRow = &s_page.m_btnRows[celli]; /* 组①②③=0..2 一一对应。 */
    XBoxLayout_init(cellBox, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)cellBox, 8, 6, 8, 12);
    XLayout_setSpacing((XLayout*)cellBox, 4);
    if (title) XBoxLayout_addWidget(cellBox, (XWidget*)title);
    if (state) XBoxLayout_addWidget(cellBox, (XWidget*)state);
    if (sample) XBoxLayout_addWidget(cellBox, sample);
    XBoxLayout_addStretch(cellBox, 1);
    if (on && off)
    {
        XBoxLayout_init(btnRow, XBoxLayoutDirection_LeftToRight);
        XLayout_setSpacing((XLayout*)btnRow, 10);
        XBoxLayout_addWidget(btnRow, (XWidget*)on);
        XBoxLayout_addWidget(btnRow, (XWidget*)off);
        XBoxLayout_addLayout(cellBox, (XLayout*)btnRow);
    }
    XWidget_setLayout(s_page.m_cells[celli], (XLayout*)cellBox);
}
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

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
 * @param      cell 格容器（标签挂其下，由格盒排布）。
 * @param      w/h 定尺（原手写 w/h；原点交格内布局器）。
 * @param      text 标签文本。
 * @return     标签指针；创建失败返回 NULL。
 */
static XLabel* fx_buildStyledLabel(XWidget* cell, int w, int h,
                                   const char* text)
{
    XLabel* label = XLabel_create(cell, 0);
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
    XWidget_setFixedWidth((XWidget*)label, w);
    XWidget_setFixedHeight((XWidget*)label, h);
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

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* ---- 页内布局容器化（2026-10-17）：页根挂垂直盒两行栅格，原页内
     * setGeometry 手写排布全部退场。观感对齐改造前截图：页根边距 12 +
     * 行间距 8 精确复刻原行列原点（12/244、12/384）；行盒两格 stretch
     * 均分列宽（原格宽 364≈(760-24-8)/2）；格容器定高 224（纵向不伸
     * 缩，多余空间留页底=原观感）；格盒边距 (8,6,8,12)/间距 4 复刻
     * 原标题 (cx+8,cy+6) 原点与行间隙；样例定尺（按钮 180x40/标签
     * 220x52/复选框 220x26）与按钮对 110x30 保持原值。 ---- */
    XBoxLayout_init(&s_page.m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_page.m_rootLayout,
                               FX_PAGE_MARGIN, FX_PAGE_MARGIN,
                               FX_PAGE_MARGIN, FX_PAGE_MARGIN);
    XLayout_setSpacing((XLayout*)&s_page.m_rootLayout, FX_CELL_GAP);
    XBoxLayout_init(&s_page.m_rowLayouts[0], XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_page.m_rowLayouts[0], FX_CELL_GAP);
    XBoxLayout_init(&s_page.m_rowLayouts[1], XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_page.m_rowLayouts[1], FX_CELL_GAP);
#endif

    /* 组①：透明度效果 + 样例按钮（左上格）。 */
    s_page.m_cells[0] = XWidget_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_cells[0]) return s_page.m_root;
    XWidget_setFixedHeight(s_page.m_cells[0], FX_CELL_HEIGHT);
    s_page.m_g0Sample = XPushButton_create(s_page.m_cells[0], 0);
    if (!s_page.m_g0Sample ||
        !fx_buildGroup(s_page.m_cells[0],
                       "① 透明度 XGraphicsOpacityEffect",
                       &s_page.m_g0Title, &s_page.m_g0State,
                       &s_page.m_g0On, &s_page.m_g0Off,
                       fx_g0OnSlot, fx_g0OffSlot))
        return s_page.m_root;
    XPushButton_setText_2(s_page.m_g0Sample, "透明按钮");
    XWidget_setFixedWidth((XWidget*)s_page.m_g0Sample, 180);
    XWidget_setFixedHeight((XWidget*)s_page.m_g0Sample, 40);
    XWidget_show((XWidget*)s_page.m_g0Sample);
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    fx_cellAssemble(0, s_page.m_g0Title, s_page.m_g0State,
                    (XWidget*)s_page.m_g0Sample,
                    s_page.m_g0On, s_page.m_g0Off);
    XBoxLayout_addWidget(&s_page.m_rowLayouts[0], s_page.m_cells[0]);
#endif

    /* 组②：模糊效果 + 样例标签（右上格，Box 边框 + 底色）。 */
    s_page.m_cells[1] = XWidget_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_cells[1]) return s_page.m_root;
    XWidget_setFixedHeight(s_page.m_cells[1], FX_CELL_HEIGHT);
    s_page.m_g1Sample = fx_buildStyledLabel(
        s_page.m_cells[1], 220, 52, "模糊标签");
    if (!s_page.m_g1Sample ||
        !fx_buildGroup(s_page.m_cells[1],
                       "② 模糊 XGraphicsBlurEffect（3x3 盒式核）",
                       &s_page.m_g1Title, &s_page.m_g1State,
                       &s_page.m_g1On, &s_page.m_g1Off,
                       fx_g1OnSlot, fx_g1OffSlot))
        return s_page.m_root;
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    fx_cellAssemble(1, s_page.m_g1Title, s_page.m_g1State,
                    (XWidget*)s_page.m_g1Sample,
                    s_page.m_g1On, s_page.m_g1Off);
    XBoxLayout_addWidget(&s_page.m_rowLayouts[0], s_page.m_cells[1]);
    /* 首行装配完成：两格 stretch 均分列宽，行盒借挂页根垂直盒。 */
    XBoxLayout_setStretch(&s_page.m_rowLayouts[0], 0, 1);
    XBoxLayout_setStretch(&s_page.m_rowLayouts[0], 1, 1);
    XBoxLayout_addLayout(&s_page.m_rootLayout,
                         (XLayout*)&s_page.m_rowLayouts[0]);
#endif

    /* 组③：投影效果 + 样例复选框（左下格）。 */
    s_page.m_cells[2] = XWidget_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_cells[2]) return s_page.m_root;
    XWidget_setFixedHeight(s_page.m_cells[2], FX_CELL_HEIGHT);
    s_page.m_g2Sample = XCheckBox_create(s_page.m_cells[2], 0);
    if (!s_page.m_g2Sample ||
        !fx_buildGroup(s_page.m_cells[2],
                       "③ 投影 XGraphicsDropShadowEffect",
                       &s_page.m_g2Title, &s_page.m_g2State,
                       &s_page.m_g2On, &s_page.m_g2Off,
                       fx_g2OnSlot, fx_g2OffSlot))
        return s_page.m_root;
    XCheckBox_setText_2(s_page.m_g2Sample, "投影复选框");
    XWidget_setFixedWidth((XWidget*)s_page.m_g2Sample, 220);
    XWidget_setFixedHeight((XWidget*)s_page.m_g2Sample, 26);
    XWidget_show((XWidget*)s_page.m_g2Sample);
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    fx_cellAssemble(2, s_page.m_g2Title, s_page.m_g2State,
                    (XWidget*)s_page.m_g2Sample,
                    s_page.m_g2On, s_page.m_g2Off);
    XBoxLayout_addWidget(&s_page.m_rowLayouts[1], s_page.m_cells[2]);
#endif

    /* 组④：无效果基线（右下格；三控件同款同尺寸，永不挂效果，
       供集成阶段截图像素对照）。 */
    s_page.m_cells[3] = XWidget_create((XWidget*)s_page.m_root, 0);
    if (!s_page.m_cells[3]) return s_page.m_root;
    XWidget_setFixedHeight(s_page.m_cells[3], FX_CELL_HEIGHT);
    s_page.m_g3Title = XLabel_create(s_page.m_cells[3], 0);
    s_page.m_g3State = XLabel_create(s_page.m_cells[3], 0);
    if (!s_page.m_g3Title || !s_page.m_g3State) return s_page.m_root;
    XLabel_setText_2(s_page.m_g3Title, "④ 无效果基线（像素对照）");
    XLabel_setTextPixelSize(s_page.m_g3Title, 14);
    XLabel_setAlignment(s_page.m_g3Title, XAlignment_Left | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)s_page.m_g3Title, 20);
    XWidget_show((XWidget*)s_page.m_g3Title);
    XLabel_setText_2(s_page.m_g3State, "原样渲染，无效果挂接");
    XLabel_setTextPixelSize(s_page.m_g3State, 12);
    XLabel_setAlignment(s_page.m_g3State, XAlignment_Left | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)s_page.m_g3State, 16);
    XWidget_show((XWidget*)s_page.m_g3State);

    s_page.m_g3Button = XPushButton_create(s_page.m_cells[3], 0);
    if (!s_page.m_g3Button) return s_page.m_root;
    XPushButton_setText_2(s_page.m_g3Button, "透明按钮");
    XWidget_setFixedWidth((XWidget*)s_page.m_g3Button, 180);
    XWidget_setFixedHeight((XWidget*)s_page.m_g3Button, 40);
    XWidget_show((XWidget*)s_page.m_g3Button);

    s_page.m_g3Label = fx_buildStyledLabel(
        s_page.m_cells[3], 220, 52, "模糊标签");
    if (!s_page.m_g3Label) return s_page.m_root;

    s_page.m_g3Check = XCheckBox_create(s_page.m_cells[3], 0);
    if (!s_page.m_g3Check) return s_page.m_root;
    XCheckBox_setText_2(s_page.m_g3Check, "投影复选框");
    XWidget_setFixedWidth((XWidget*)s_page.m_g3Check, 220);
    XWidget_setFixedHeight((XWidget*)s_page.m_g3Check, 26);
    XWidget_show((XWidget*)s_page.m_g3Check);

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* 格④垂直盒：标题/状态/三基线控件 + 伸缩（无按钮行，同原装配）。 */
    XBoxLayout_init(&s_page.m_cellLayouts[3], XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_page.m_cellLayouts[3],
                               8, 6, 8, 12);
    XLayout_setSpacing((XLayout*)&s_page.m_cellLayouts[3], 4);
    XBoxLayout_addWidget(&s_page.m_cellLayouts[3],
                         (XWidget*)s_page.m_g3Title);
    XBoxLayout_addWidget(&s_page.m_cellLayouts[3],
                         (XWidget*)s_page.m_g3State);
    XBoxLayout_addWidget(&s_page.m_cellLayouts[3],
                         (XWidget*)s_page.m_g3Button);
    XBoxLayout_addWidget(&s_page.m_cellLayouts[3],
                         (XWidget*)s_page.m_g3Label);
    XBoxLayout_addWidget(&s_page.m_cellLayouts[3],
                         (XWidget*)s_page.m_g3Check);
    XBoxLayout_addStretch(&s_page.m_cellLayouts[3], 1);
    XWidget_setLayout(s_page.m_cells[3],
                      (XLayout*)&s_page.m_cellLayouts[3]);
    XBoxLayout_addWidget(&s_page.m_rowLayouts[1], s_page.m_cells[3]);
    /* 次行装配完成：两格 stretch 均分列宽，行盒借挂页根垂直盒；
       页根最后挂根盒（attach 即激活，全页几何一次到位）。 */
    XBoxLayout_setStretch(&s_page.m_rowLayouts[1], 0, 1);
    XBoxLayout_setStretch(&s_page.m_rowLayouts[1], 1, 1);
    XBoxLayout_addLayout(&s_page.m_rootLayout,
                         (XLayout*)&s_page.m_rowLayouts[1]);
    XWidget_setLayout(s_page.m_root, (XLayout*)&s_page.m_rootLayout);
#endif

    s_page.m_ready = true;
    return s_page.m_root;
}

#if XLAYOUT_ON && XLAYOUT_BOX_ON
/**
 * @brief      套用指定档位的布局口径（越档时由 adapt 调用一次）。
 * @details    宽屏档逐项还原 build 装配段定径（格定高 224/切换钮 30/
 *             复选框 26/钮间距 10/格盒边距 (8,6,8,12)/页根边距 12 间距
 *             8），桌面 1000x700 与装配现状同口径；单列档（手机竖屏
 *             ≈400 逻辑宽）则：
 *             - 两行行盒翻垂直单列——条目顺序不变（行0=①②、行1=③④，
 *               即 ①②③④ 自上而下），setDirection 对 magic 空白自动
 *               横纵翻转；格宽即页根全宽，每格最小包络（钮行 246）在
 *               400 宽下放得下，右列不再被内容视口硬裁；
 *             - 格容器解除定高（min 归零/max 放开）交布局协商：四格
 *               定高 224 竖排合计 ≈992px 远超手机竖屏内容视口
 *               （≈670~740），解除后紧则收到内容最小高（配合页根/格盒
 *               边距间距收口，四格最小高合计 ≈650 装得进主流竖屏视
 *               口），松则余高由格内伸缩段吸收（按钮行仍贴格底=桌面
 *               观感）；
 *             - 可点控件（三对启/禁钮、③投影样例复选框与④基线复选框）
 *               统一抬 FX_TOUCH_H 触控目标、启/禁钮间距 10→12 防误触；
 *               ③/④ 复选框同步抬保持同尺寸配对，像素对照口径不破。
 */
static void fx_applyLayoutTier(bool compact)
{
    int i;
    int hToggle;
    int hCheck;
    XPushButton* toggles[6];
    XCheckBox* checks[2];

    XBoxLayout_setDirection(&s_page.m_rowLayouts[0],
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);
    XBoxLayout_setDirection(&s_page.m_rowLayouts[1],
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);

    /* 页根边距/行距与格盒边距/条目距收口：换竖排纵轴预算（宽屏档
       逐项还原装配值，与 build 装配段字面保持一致）。 */
    XLayout_setContentsMargins((XLayout*)&s_page.m_rootLayout,
                               compact ? 10 : FX_PAGE_MARGIN,
                               compact ? 10 : FX_PAGE_MARGIN,
                               compact ? 10 : FX_PAGE_MARGIN,
                               compact ? 10 : FX_PAGE_MARGIN);
    XLayout_setSpacing((XLayout*)&s_page.m_rootLayout,
                       compact ? 6 : FX_CELL_GAP);
    for (i = 0; i < 4; ++i)
    {
        if (!s_page.m_cells[i]) continue;
        if (compact)
        {
            /* 解除定高：格高交布局协商（紧收至内容最小高、松吃余高）。 */
            XWidget_setMinimumSize(s_page.m_cells[i], 0, 0);
            XWidget_setMaximumSize(s_page.m_cells[i],
                                   XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
        }
        else
        {
            XWidget_setFixedHeight(s_page.m_cells[i], FX_CELL_HEIGHT);
        }
        XLayout_setContentsMargins((XLayout*)&s_page.m_cellLayouts[i],
                                   8, compact ? 4 : 6,
                                   8, compact ? 8 : 12);
        XLayout_setSpacing((XLayout*)&s_page.m_cellLayouts[i],
                           compact ? 3 : 4);
    }

    toggles[0] = s_page.m_g0On;
    toggles[1] = s_page.m_g0Off;
    toggles[2] = s_page.m_g1On;
    toggles[3] = s_page.m_g1Off;
    toggles[4] = s_page.m_g2On;
    toggles[5] = s_page.m_g2Off;
    checks[0] = s_page.m_g2Sample;
    checks[1] = s_page.m_g3Check;
    hToggle = compact ? FX_TOUCH_H : 30;
    hCheck = compact ? FX_TOUCH_H : 26;
    for (i = 0; i < 6; ++i)
        if (toggles[i])
            XWidget_setFixedHeight((XWidget*)toggles[i], hToggle);
    for (i = 0; i < 2; ++i)
        if (checks[i])
            XWidget_setFixedHeight((XWidget*)checks[i], hCheck);
    /* 启/禁钮行间距：10 在触屏上易误触，单列档抬到 12（宽屏档不动）。 */
    for (i = 0; i < 3; ++i)
        XLayout_setSpacing((XLayout*)&s_page.m_btnRows[i],
                           compact ? 12 : 10);
}
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

/** @brief 自适应重排（xgui_demo_pages.h 契约）：宽度分档唯一入口。
 * @details [布局容器化 2026-10-17] 同档内几何由页根布局器协商（根尺寸
 *          变化经 XWidget_recomputeGeometry 的 resize→activate 联动自
 *          动重激活），手工重排保持退场；[竖屏单列档 2026-10-25] 在此
 *          按页根有效宽分档（主文件 demo_layout_content 在切页/resize/
 *          startup/停靠全路径的几何分配之后调用本函数，根尺寸保证新
 *          鲜；懒分配首显亦在 demo_switchPage 内先建后跑本漏斗）：
 *          - 宽屏档（≥FX_STACK_MAX_W）：保持装配口径——2x2 两行栅格，
 *            桌面 1000x700 视觉零变化；
 *          - 单列档（<FX_STACK_MAX_W，手机竖屏 ≈400 逻辑宽）：栅格翻
 *            垂直单列 + 触控目标抬 FX_TOUCH_H + 纵轴收口，见
 *            fx_applyLayoutTier。
 *          另按格内可用宽连续收放标题字级（半宽格防硬截，两档内都生
 *          效）。档位/字级留档 s_page，未越档未越阈值时短路零开销
 *          （拖拽期每像素一轮 adapt）；宽 0（几何未分配）时等下一轮
 *          有效宽再套档。契约签名保留（主文件调用点不动），page 与
 *          登记根不符亦无副作用。 */
void demo_page_effects_adapt(XWidget* page)
{
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    int i;
    int w;
    int titleW;
    int titleSize;
    bool compact;
    XLabel* titles[4];

    if (!s_page.m_ready || !s_page.m_root) return;
    if (page && page != s_page.m_root) return;
    w = XWidget_width(s_page.m_root);
    if (w <= 0) return;

    compact = w < FX_STACK_MAX_W;
    if (compact != s_page.m_compact)
    {
        s_page.m_compact = compact;
        fx_applyLayoutTier(compact);
    }

    /* 标题字级随格宽收放（不随档位）：格内可用宽不足最长全名时降号。
       格内可用宽 = 格宽 - 格盒左右边距 8x2；格宽单列档=根宽-页根边距
       10x2，两列档=(根宽-页根边距 12x2-列间距 8)/2。 */
    titleW = compact ? w - 2 * 10 - 16
                     : (w - 2 * FX_PAGE_MARGIN - FX_CELL_GAP) / 2 - 16;
    titleSize = titleW < FX_TITLE_SHRINK_W ? 12 : 14;
    if (titleSize != s_page.m_titleSize)
    {
        titles[0] = s_page.m_g0Title;
        titles[1] = s_page.m_g1Title;
        titles[2] = s_page.m_g2Title;
        titles[3] = s_page.m_g3Title;
        for (i = 0; i < 4; ++i)
            if (titles[i])
                XLabel_setTextPixelSize(titles[i], titleSize);
        s_page.m_titleSize = titleSize;
    }
#else /* 布局系统裁剪：页内无布局器可协商，契约入口保持无副作用。 */
    (void)page;
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
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
