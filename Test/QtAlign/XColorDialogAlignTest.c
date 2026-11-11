/**
 * @file       XColorDialogAlignTest.c
 * @brief      XColorDialog × Qt 6.8 QColorDialog 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qcolordialog.h
 *             与 Qt 6.8 源码 src/widgets/dialogs/qcolordialog.cpp（jsdelivr
 *             v6.8.0 原文），关键行为用本机 Qt 6.8.0（QT_QPA_PLATFORM=
 *             offscreen）探针实测：
 *             - 构造默认 currentColor=#ffffff（白）、options=0；
 *               selectedColor 在接受前是**无效色**（qcolordialog.cpp:
 *               2298-2316 done 分支 + 本机 Qt 6.8.0/6.8.3 探针 C0）；
 *             - customCount 恒 16（qplatformdialoghelper.cpp:362-365），
 *               customColor 缺省 16 槽全不透明白、越界返回不透明白
 *               （:364-371），standardColor 缺省 48 槽按
 *               `index=g*12+r*4+b` 生成 qRgb(r*255/3,g*255/3,b*255/2)
 *               （:251-258）、越界返回不透明白（:392-396）；探针 C5
 *               实测 standardColor(0)=#000000、standardColor(47)=#ffffff、
 *               customColor(-1) 有效白色；
 *             - setCurrentColor 实际变化才发 currentColorChanged，
 *               同值重设不重复发射（C1/C2）；
 *             - done(Accepted) → selectedColor=currentColor 并发
 *               colorSelected；done(非 Accepted) → selectedColor 复位
 *               无效色（C3/C4，qcolordialog.cpp:2303-2316）；
 *             - open 即 QDialog::open（`using QDialog::open`
 *               qcolordialog.h:50）：显示期间 windowModality=WindowModal，
 *               关闭后恢复原值。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON

#include "XColorDialog.h"
#include "XDialog.h"
#include "XWidget.h"
#include "XObject.h"
#include "XColor.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XClass.h"
#include "XString.h"
#include "XDir.h"
#include "XDateTime.h"
#include "XSettings.h"
#include "XVariant.h"

static int g_xcdSelected;  /**< colorSelected 次数。 */
static int g_xcdChanged;   /**< currentColorChanged 次数。 */

#if XSETTINGS_ON
/** @brief 在系统临时目录下生成唯一的持久化文件路径（新 XString）。
 *  用例之间用不同 tag 隔离；文件内容由用例自己写。 */
static XString* xcd_tempSettingsPath(const char* tag)
{
    static int s_seq = 0;
    XString* dir = XDir_tempPath();
    XString* path;
    XString num;
    if (!dir) return NULL;
    path = XString_create_copy(dir);
    XClassDelete((XClass*)dir);
    if (!path) return NULL;
    XString_append_utf8(path, "/xcd_align_");
    XString_append_utf8(path, tag);
    XString_append_utf8(path, "_");
    XString_init(&num);
    XString_setNum_uLong(&num,
        (unsigned long)(XDateTime_currentMSecsSinceEpoch() & 0x3FFFFFFFul) +
        (unsigned long)(++s_seq), 10);
    XString_append(path, &num);
    XClassDeinit((XClass*)&num);
    XString_append_utf8(path, ".conf");
    return path;
}

/** @brief 把自定义色持久化指向 path（NULL = 恢复 QtProject 缺省）并重载。 */
static void xcd_useSettingsFile(const XString* path)
{
    XColorDialog_setCustomColorsFile_utf8(path ? XString_toUtf8(path) : NULL);
    XColorDialog_reloadCustomColors();
}

/** @brief 结束用例的持久化隔离：释放覆盖路径（不动内存表，避免再去读
 *  开发机的 QtProject.conf）。 */
static void xcd_endIsolation(void)
{
    XColorDialog_setCustomColorsFile_utf8(NULL);
}
#endif /* XSETTINGS_ON */

static void xcdSelectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_xcdSelected;
}

static void xcdChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_xcdChanged;
}

/* ==================== ① 构造默认值 ==================== */

XQ_TEST(XColorDialog, constructorDefaults)
{
    XColorDialog* dlg;
    XColor current;
    XColor expected;
#if XSETTINGS_ON
    XString* iso = xcd_tempSettingsPath("defaults");
    /* 持久化隔离（对标 Qt autotest 用 QSettings::setPath 隔离
     * QColorDialog 自定义色的做法）：缺省槽断言只在「存储为空」时成立，
     * 否则开发机 ~/.config/QtProject.conf 里存过的自定义色会让本用例
     * 随环境变化。 */
    xcd_useSettingsFile(iso);
#endif
    dlg = XColorDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);

    /* qcolordialog.cpp 构造：currentColor 缺省白（实测 C0 #ffffff）。 */
    expected = XColor_White;
    current = XColorDialog_currentColor(dlg);
    XQ_CHECK(XColor_equals(&current, &expected));
    XQ_CHECK(XColor_isValid(&current));

    /* 接受前 selectedColor 为无效色（qcolordialog.cpp:2298-2316 只在
     * done(Accepted) 写入；探针 C0）。 */
    {
        XColor sel = XColorDialog_selectedColor(dlg);
        XQ_CHECK(!XColor_isValid(&sel));
    }

    /* 选项位默认 0（实测 C0 opts=0）。 */
    XQ_EQ_I64(XColorDialog_options(dlg), 0);
    XQ_CHECK(!XColorDialog_testOption(dlg,
                                      XColorDialog_ShowAlphaChannel));

    /* customCount 恒 16（qplatformdialoghelper.cpp:362-365 探针 C5）。 */
    XQ_EQ_I64(XColorDialog_customCount(dlg), 16);
    /* 缺省自定义槽全不透明白（:260 std::fill(..., 0xffffffff)）。 */
    {
        XColor white = XColor_create_rgb(255, 255, 255, 255);
        XColor c15 = XColorDialog_customColor(dlg, 15);
        XQ_CHECK(XColor_isValid(&c15));
        XQ_CHECK(XColor_equals(&c15, &white));
    }
    /* 越界读取返回不透明白（:367-371，不是无效色）。 */
    {
        XColor white = XColor_create_rgb(255, 255, 255, 255);
        XColor out = XColorDialog_customColor(dlg, 16);
        XQ_CHECK(XColor_isValid(&out));
        XQ_CHECK(XColor_equals(&out, &white));
        out = XColorDialog_customColor(dlg, -1);
        XQ_CHECK(XColor_equals(&out, &white));
    }
    /* 标准色缺省表（:251-258 生成式）：首槽黑、末槽白。 */
    {
        XColor black = XColor_create_rgb(0, 0, 0, 255);
        XColor white = XColor_create_rgb(255, 255, 255, 255);
        XColor s0 = XColorDialog_standardColor(dlg, 0);
        XColor s47 = XColorDialog_standardColor(dlg, 47);
        XColor s48 = XColorDialog_standardColor(dlg, 48);
        XQ_CHECK(XColor_equals(&s0, &black));
        XQ_CHECK(XColor_equals(&s47, &white));
        XQ_CHECK(XColor_equals(&s48, &white)); /* 越界 → 白（:392-396）。 */
    }

    /* setCurrentColor 往返。 */
    expected = XColor_Red;
    XColorDialog_setCurrentColor(dlg, expected);
    current = XColorDialog_currentColor(dlg);
    XQ_CHECK(XColor_equals(&current, &expected));

    XClassDelete(dlg);
#if XSETTINGS_ON
    xcd_endIsolation();
    if (iso) XClassDelete((XClass*)iso);
#endif
}

/* ==================== ①b 色表进程级共享 + customCount 恒 16 ========== */

XQ_TEST(XColorDialog, customTableStaticShared)
{
    XColorDialog* a;
    XColorDialog* b;
    XColor red = XColor_Red;
    XColor green = XColor_Green;
#if XSETTINGS_ON
    /* setCustomColor 会即时落盘（跨进程持久化）：本用例写色表前先隔离到
     * 临时文件，避免污染开发机的 QtProject.conf。 */
    XString* iso = xcd_tempSettingsPath("shared");
    xcd_useSettingsFile(iso);
#endif
    a = XColorDialog_create(NULL, 0);
    b = XColorDialog_create(NULL, 0);
    XQ_REQUIRE(a != NULL && b != NULL);

    /* qcolordialog.cpp:513-524 → QColorDialogOptions 全进程共享静态表；
     * 本机 Qt 6.8 探针 C6：实例 A setCustomColor(0) 后全新实例 B 读到
     * 同色。 */
    XColorDialog_setCustomColor(a, 0, red);
    {
        XColor got = XColorDialog_customColor(b, 0);
        XQ_CHECK(XColor_equals(&got, &red));
    }
    XColorDialog_setStandardColor(a, 5, green);
    {
        XColor got = XColorDialog_standardColor(b, 5);
        XQ_CHECK(XColor_equals(&got, &green));
    }
    /* customCount 与「已设槽数」无关，恒 16（qplatformdialoghelper.cpp:
     * 362-365；探针 C5 实测 set 后仍 16）。 */
    XQ_EQ_I64(XColorDialog_customCount(a), 16);
    XQ_EQ_I64(XColorDialog_customCount(b), 16);

    XClassDelete(a);
    XClassDelete(b);
#if XSETTINGS_ON
    xcd_endIsolation();
    if (iso) XClassDelete((XClass*)iso);
#endif
}

/* ==================== ② currentColorChanged 只在实际变化时发射 ==================== */

XQ_TEST(XColorDialog, currentColorChangedOnChangeOnly)
{
    XColorDialog* dlg = XColorDialog_create(NULL, 0);
    XColor red;
    XColor green;
    XQ_REQUIRE(dlg != NULL);
    g_xcdSelected = 0;
    g_xcdChanged = 0;
    XObject_connect_1((XObject*)dlg,
                      XSignal(XColorDialog_currentColorChanged_signal),
                      (XObject*)dlg, xcdChangedSlot,
                      XConnectionType_Direct);

    /* 实测 C1：当前色白→红，发射 1 次。 */
    red = XColor_Red;
    XColorDialog_setCurrentColor(dlg, red);
    XQ_EQ_I64(g_xcdChanged, 1);

    /* 实测 C2：同值重设不重复发射。 */
    XColorDialog_setCurrentColor(dlg, red);
    XQ_EQ_I64(g_xcdChanged, 1);

    green = XColor_Green;
    XColorDialog_setCurrentColor(dlg, green);
    XQ_EQ_I64(g_xcdChanged, 2);
    {
        XColor now = XColorDialog_currentColor(dlg);
        XQ_EQ_I64(XColor_red(&now), XColor_red(&green));
        XQ_EQ_I64(XColor_green(&now), XColor_green(&green));
    }

    XClassDelete(dlg);
}

/* ==================== ③ done 的 selectedColor / colorSelected 语义 ==================== */

XQ_TEST(XColorDialog, doneSelectsColor)
{
    XColorDialog* dlg = XColorDialog_create(NULL, 0);
    XColor red;
    XColor selected;
    XQ_REQUIRE(dlg != NULL);
    g_xcdSelected = 0;
    XObject_connect_1((XObject*)dlg,
                      XSignal(XColorDialog_colorSelected_signal),
                      (XObject*)dlg, xcdSelectedSlot,
                      XConnectionType_Direct);

    /* 实测 C3：Accepted → selectedColor=当前色并发 colorSelected。 */
    red = XColor_Red;
    XColorDialog_setCurrentColor(dlg, red);
    XDialog_setResult(&dlg->m_base, 0); /* 复位上一轮结果码。 */
    XColorDialog_done(dlg, (int)XDialogCode_Accepted);
    XQ_EQ_I64(g_xcdSelected, 1);
    selected = XColorDialog_selectedColor(dlg);
    XQ_CHECK(XColor_equals(&selected, &red));
    XQ_EQ_I64(XDialog_result(&dlg->m_base), XDialogCode_Accepted);

    /* 实测 C4：Rejected → selectedColor 复位为无效色，不再发
     * colorSelected（qcolordialog.cpp:2303-2310 else 分支）。 */
    XColorDialog_done(dlg, (int)XDialogCode_Rejected);
    selected = XColorDialog_selectedColor(dlg);
    XQ_CHECK(!XColor_isValid(&selected));
    XQ_EQ_I64(g_xcdSelected, 1);
    XQ_EQ_I64(XDialog_result(&dlg->m_base), XDialogCode_Rejected);

    XClassDelete(dlg);
}

/* ==================== ④ open = QDialog::open（窗口模态） ==================== */

XQ_TEST(XColorDialog, openUsesWindowModality)
{
    XColorDialog* dlg = XColorDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    g_xcdSelected = 0;
    XObject_connect_1((XObject*)dlg,
                      XSignal(XColorDialog_colorSelected_signal),
                      (XObject*)dlg, xcdSelectedSlot,
                      XConnectionType_Direct);

    /* open 前：非模态。 */
    XQ_EQ_I64(XWidget_windowModality((XWidget*)dlg),
              XWindowModality_NonModal);

    /* qdialog.cpp:509-526：open 临时改 WindowModal 并显示、立即返回。 */
    XColorDialog_open(dlg);
    XQ_CHECK(XWidget_isVisible((XWidget*)dlg));
    XQ_EQ_I64(XWidget_windowModality((XWidget*)dlg),
              XWindowModality_WindowModal);

    /* 关闭收口恢复原窗口模态（resetModalitySetByOpen）。 */
    XColorDialog_done(dlg, (int)XDialogCode_Rejected);
    XQ_CHECK(!XWidget_isVisible((XWidget*)dlg));
    XQ_EQ_I64(XWidget_windowModality((XWidget*)dlg),
              XWindowModality_NonModal);

    XClassDelete(dlg);
}

/* ==================== ⑤ 父子所有权 ==================== */

XQ_TEST(XColorDialog, parentOwnership)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XColorDialog* dlg;
    XQ_REQUIRE(parent != NULL);
    dlg = XColorDialog_create(parent, 0);
    XQ_REQUIRE(dlg != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)dlg) == parent);
    /* 继承链贯通：XDialog_init 的「恒为顶层对话框窗口」对派生类生效，
     * 且虚表即本类（XDialog 基类槽已继承）。 */
    XQ_CHECK(XClassGetVtable((XObject*)dlg) == XColorDialog_class_init());
    XQ_CHECK(XWidget_isWindow((XWidget*)dlg));
    XClassDelete(dlg);
    XClassDelete(parent);
}

/* ==================== ⑥ 自定义色跨进程持久化 ====================
 * 对标 qplatformdialoghelper.cpp:248-283：QSettings(UserScope,
 * "QtProject") 的 `Qt/customColors/<i>`，值 = QColor::rgba()。
 * 本机 Qt 6.8.3 探针实测：setCustomColor(0, QColor(255,0,0,128)) 后落盘
 * 文件为 `[Qt]` + `customColors\0=2164195328`（0x80FF0000），customColor(0)
 * 读回 alpha=255（QColor(QRgb) 语义）。 */

#if XSETTINGS_ON

XQ_TEST(XColorDialog, customColorPersistsToSettings)
{
    XString* path = xcd_tempSettingsPath("persist");
    XColorDialog* dlg;
    XColor white = XColor_create_rgb(255, 255, 255, 255);
    XColor stored;
    XQ_REQUIRE(path != NULL);
    xcd_useSettingsFile(path);          /* 空文件 ⇒ 缺省全白 */
    dlg = XColorDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    {
        XColor c = XColorDialog_customColor(dlg, 2);
        XQ_CHECK(XColor_equals(&c, &white));
    }

    /* 设置时即落盘（带 alpha=0x80 的色）。 */
    XColorDialog_setCustomColor(dlg, 2,
                                XColor_create_rgb(0x12, 0x34, 0x56, 0x80));

    /* 用**另一个** XSettings 读同一文件（等价于另一个进程读回）。 */
    {
        XSettings* s = XSettings_create_7_utf8(XString_toUtf8(path),
                                               XSettings_IniFormat, NULL);
        XVariant* v;
        XQ_REQUIRE(s != NULL);
        v = XSettings_value_utf8(s, "Qt/customColors/2");
        XQ_CHECK(v != NULL && XVariant_isValid(v));
        /* 0xAARRGGBB：alpha 也落盘（同 Qt color.rgba()）。 */
        XQ_EQ_I64((long long)XVariant_toUint32(v), 0x80123456LL);
        if (v) XClassDelete((XClass*)v);
        /* 未设置过的槽同样整表落盘（Qt writeSettings 写全部 16 槽）。 */
        v = XSettings_value_utf8(s, "Qt/customColors/15");
        XQ_CHECK(v != NULL && XVariant_isValid(v));
        XQ_EQ_I64((long long)XVariant_toUint32(v), 0xFFFFFFFFLL);
        if (v) XClassDelete((XClass*)v);
        XClassDelete((XClass*)s);
    }

    /* 重载（= 下次进程启动）：内存表来自文件。 */
    XColorDialog_reloadCustomColors();
    stored = XColorDialog_customColor(dlg, 2);
    XQ_EQ_I64(XColor_red(&stored), 0x12);
    XQ_EQ_I64(XColor_green(&stored), 0x34);
    XQ_EQ_I64(XColor_blue(&stored), 0x56);
    /* alpha 口径同 QColor(QRgb)：恒 255（探针实测，见头文件注）。 */
    XQ_EQ_I64(XColor_alpha(&stored), 255);

    /* 其它槽仍是缺省白（文件里的 0xFFFFFFFF）。 */
    {
        XColor c = XColorDialog_customColor(dlg, 3);
        XQ_CHECK(XColor_equals(&c, &white));
    }

    XClassDelete(dlg);
    xcd_endIsolation();                 /* 释放覆盖路径 */
    XClassDelete((XClass*)path);
}

XQ_TEST(XColorDialog, customColorsLoadFromSettings)
{
    XString* path = xcd_tempSettingsPath("load");
    XColorDialog* dlg;
    XColor white = XColor_create_rgb(255, 255, 255, 255);
    XQ_REQUIRE(path != NULL);
    /* 预置一份「另一个进程留下的」配置：只写 0/5 两槽，其余缺键。 */
    {
        XSettings* s = XSettings_create_7_utf8(XString_toUtf8(path),
                                               XSettings_IniFormat, NULL);
        XVariant* v;
        XQ_REQUIRE(s != NULL);
        v = XVariant_create_uint32(0xFFAABBCCu);
        XSettings_setValue_utf8(s, "Qt/customColors/0", v);
        XClassDelete((XClass*)v);
        v = XVariant_create_uint32(0xFF010203u);
        XSettings_setValue_utf8(s, "Qt/customColors/5", v);
        XClassDelete((XClass*)v);
        XSettings_sync(s);
        XClassDelete((XClass*)s);
    }

    xcd_useSettingsFile(path);
    dlg = XColorDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    {
        XColor c = XColorDialog_customColor(dlg, 0);
        XQ_EQ_I64(XColor_red(&c), 0xAA);
        XQ_EQ_I64(XColor_green(&c), 0xBB);
        XQ_EQ_I64(XColor_blue(&c), 0xCC);
    }
    {
        XColor c = XColorDialog_customColor(dlg, 5);
        XQ_EQ_I64(XColor_red(&c), 0x01);
        XQ_EQ_I64(XColor_green(&c), 0x02);
        XQ_EQ_I64(XColor_blue(&c), 0x03);
    }
    /* 缺键槽保持缺省白（对标 readSettings 的 `if (v.isValid())`）。 */
    {
        XColor c = XColorDialog_customColor(dlg, 1);
        XQ_CHECK(XColor_equals(&c, &white));
    }

    XClassDelete(dlg);
    xcd_endIsolation();
    XClassDelete((XClass*)path);
}

XQ_TEST(XColorDialog, setCustomColorOutOfRangeNotPersisted)
{
    XString* path = xcd_tempSettingsPath("oob");
    XQ_REQUIRE(path != NULL);
    xcd_useSettingsFile(path);
    /* 越界写被忽略（qplatformdialoghelper.cpp:379-385），也不得产生
     * 任何落盘副作用。 */
    XColorDialog_setCustomColor(NULL, 16, XColor_Red);
    XColorDialog_setCustomColor(NULL, -1, XColor_Green);
    {
        XSettings* s = XSettings_create_7_utf8(XString_toUtf8(path),
                                               XSettings_IniFormat, NULL);
        XQ_REQUIRE(s != NULL);
        XQ_CHECK(!XSettings_contains_utf8(s, "Qt/customColors/16"));
        XClassDelete((XClass*)s);
    }
    {
        XColor c = XColorDialog_customColor(NULL, 16);
        XColor white = XColor_create_rgb(255, 255, 255, 255);
        XQ_CHECK(XColor_equals(&c, &white));
    }
    xcd_endIsolation();
    XClassDelete((XClass*)path);
}

#endif /* XSETTINGS_ON */

#endif /* 模块开关 */
