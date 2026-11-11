/**
 * @file       XFileDialogAlignTest.c
 * @brief      XFileDialog × Qt 6.8 QFileDialog 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qfiledialog.h
 *             与 Qt 6.8 源码 src/widgets/dialogs/qfiledialog.cpp（jsdelivr
 *             v6.8.0 原文），关键行为用本机 Qt 6.8.0（QT_QPA_PLATFORM=
 *             offscreen）探针实测：
 *             - 构造默认 fileMode=AnyFile(0)、acceptMode=AcceptOpen(0)、
 *               options=0（F0，qfiledialog.cpp:2921 `setFileMode(AnyFile)`）；
 *               viewMode=Detail（qfiledialog.cpp:166 文档 + 本机 Qt 6.8
 *               探针 F0 实测 0=Detail）；
 *             - 构造缺省 nameFilters==["All Files (*)"]、selectedNameFilter
 *               同值（qplatformdialoghelper.cpp:587-603；探针 F0）；
 *             - 无选择且 fileMode 不是 ExistingFile/ExistingFiles 时
 *               selectedFiles 回落当前目录（qfiledialog.cpp:1291-1306；
 *               探针 F0/F5）；
 *             - selectFile 为**替换**语义并按当前目录补全绝对路径，空串
 *               忽略（qfiledialog.cpp:1058-1098；探针 F7）；
 *             - setNameFilters 整体替换后当前选中过滤器取**首条**
 *               （qfiledialog.cpp:1409-1436 `useNameFilter(0)`；F1 实测
 *               selectedNameFilter=="Text (*.txt)"），空列表则清空；
 *             - selectNameFilter 只对**列表内**过滤器生效（qfiledialog.cpp:
 *               1449-1475 findText 判定；F2 未知过滤器不改状态、F3 已知
 *               过滤器切换）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON

#include "XFileDialog.h"
#include "XDialog.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"
#include "XStringList.h"
#include "XClass.h"
#include "XFileSystem_config.h"
#if XFILE_ON && XDIR_ON
#include "XDir.h"
#endif

/** @brief 取列表元素数（借用列表）。 */
static int64_t xfdCount(const XStringList* list)
{
    if (!list) return 0;
    return XStringList_size_base((const XContainer*)list);
}

/** @brief 空串安全转换：本库 XString_toUtf8 对空串返回 NULL。 */
static const char* xfdCStr(const char* s)
{
    return s ? s : "";
}

/** @brief 判断列表下标元素是否等于给定 UTF-8 文本。 */
static bool xfdAtEq(const XStringList* list, int64_t index, const char* utf8)
{
    const XString* item;
    if (!list || !utf8) return false;
    item = (const XString*)(const void*)XStringList_at_base(
        (const XVector*)list, index);
    if (!item) return false;
    return strcmp(xfdCStr(XString_toUtf8(item)), utf8) == 0;
}

/** @brief 消费一个堆上 XString 并判断其 UTF-8 内容（失败也释放）。 */
static bool xfdTakeEq(XString* text, const char* utf8)
{
    bool same;
    if (!text || !utf8) {
        if (text) XClassDelete((XClass*)text);
        return false;
    }
    same = strcmp(xfdCStr(XString_toUtf8(text)), utf8) == 0;
    XClassDelete((XClass*)text);
    return same;
}

/* ==================== ① 构造默认值 ==================== */

XQ_TEST(XFileDialog, constructorDefaults)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XStringList* filters;
    XStringList* files;
    XQ_REQUIRE(dlg != NULL);

    /* 实测 F0：fileMode=0（AnyFile）、acceptMode=0（AcceptOpen）、
     * options=0（qfiledialog.cpp:2921 + 属性文档 :1678）；viewMode 缺省
     * Detail（qfiledialog.cpp:166，本机 6.8 探针 F0 实测 0）。 */
    XQ_EQ_I64(XFileDialog_fileMode(dlg), XFileDialog_AnyFile);
    XQ_EQ_I64(XFileDialog_acceptMode(dlg), XFileDialog_AcceptOpen);
    XQ_EQ_I64(XFileDialog_viewMode(dlg), XFileDialogViewMode_Detail);
    XQ_EQ_I64(XFileDialog_options(dlg), 0);
    XQ_CHECK(!XFileDialog_testOption(dlg,
                                     XFileDialog_DontUseNativeDialog));

    /* 缺省 nameFilters==["All Files (*)"]、selectedNameFilter 同值
     * （qplatformdialoghelper.cpp:587-603；探针 F0）。 */
    filters = XFileDialog_nameFilters(dlg);
    XQ_EQ_I64(xfdCount(filters), 1);
    XQ_CHECK(xfdAtEq(filters, 0, "All Files (*)"));
    if (filters) XClassDelete((XClass*)filters);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg), "All Files (*)"));

    /* 无选择 + AnyFile：selectedFiles 回落当前目录（qfiledialog.cpp:
     * 1291-1306；探针 F0）。裁剪构建（XDIR_ON=0）取不到当前目录，
     * 回落为空列表。 */
    files = XFileDialog_selectedFiles(dlg);
#if XFILE_ON && XDIR_ON
    XQ_EQ_I64(xfdCount(files), 1);
    {
        XString* cwd = XDir_currentPath();
        if (cwd) {
            XQ_CHECK(xfdAtEq(files, 0, xfdCStr(XString_toUtf8(cwd))));
            XClassDelete((XClass*)cwd);
        }
    }
#else
    XQ_EQ_I64(xfdCount(files), 0);
#endif
    if (files) XClassDelete((XClass*)files);

    /* ExistingFile 模式无选择：空列表（探针 F5）；selectedFile 同口径。 */
    XFileDialog_setFileMode(dlg, XFileDialog_ExistingFile);
    files = XFileDialog_selectedFiles(dlg);
    XQ_EQ_I64(xfdCount(files), 0);
    if (files) XClassDelete((XClass*)files);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), ""));
    XFileDialog_setFileMode(dlg, XFileDialog_AnyFile);

    /* directory 仍为本库「显式设置才非空」口径。 */
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(dlg), ""));

    XClassDelete(dlg);
}

/* ==================== ② 名称过滤器选择语义（F1~F3） ==================== */

XQ_TEST(XFileDialog, nameFilterSelection)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XStringList* filters;
    XString* f;
    XQ_REQUIRE(dlg != NULL);
    filters = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(filters != NULL);
    {
        XString* a = XString_create_utf8("Text (*.txt)");
        XString* b = XString_create_utf8("All (*)");
        XQ_REQUIRE(a != NULL && b != NULL);
        XStringList_push_back_move_base((XVector*)filters, a);
        XStringList_push_back_move_base((XVector*)filters, b);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
    }

    /* 实测 F1：setNameFilters 后选中首条（useNameFilter(0)）。 */
    XFileDialog_setNameFilters(dlg, filters);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg),
                       "Text (*.txt)"));

    /* 实测 F2：列表外过滤器不改变选中。 */
    f = XString_create_utf8("Nope (*.zzz)");
    XQ_REQUIRE(f != NULL);
    XFileDialog_selectNameFilter(dlg, f);
    XClassDelete((XClass*)f);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg),
                       "Text (*.txt)"));

    /* 实测 F3：列表内过滤器切换成功。 */
    f = XString_create_utf8("All (*)");
    XQ_REQUIRE(f != NULL);
    XFileDialog_selectNameFilter(dlg, f);
    XClassDelete((XClass*)f);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg), "All (*)"));

    /* 空列表清空选中（本机 Qt 6.8 实测 setNameFilters(空) 后为 ""）。 */
    XFileDialog_setNameFilters(dlg, NULL);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg), ""));
    {
        XStringList* got = XFileDialog_nameFilters(dlg);
        XQ_EQ_I64(xfdCount(got), 0);
        if (got) XClassDelete((XClass*)got);
    }

    XClassDelete((XClass*)filters);
    XClassDelete(dlg);
}

/* ==================== ③ setNameFilter 的 ";;" 拆分 ==================== */

XQ_TEST(XFileDialog, setNameFilterSplit)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XString* filter;
    XStringList* got;
    XQ_REQUIRE(dlg != NULL);

    /* 对标 qfiledialog.cpp:1363-1367 setNameFilter = setNameFilters(
     * qt_make_filter_list(filter))——按 ";;" 拆分，首条成为选中。 */
    filter = XString_create_utf8("图片 (*.png);;文本 (*.txt)");
    XQ_REQUIRE(filter != NULL);
    XFileDialog_setNameFilter(dlg, filter);
    XClassDelete((XClass*)filter);

    got = XFileDialog_nameFilters(dlg);
    XQ_EQ_I64(xfdCount(got), 2);
    XQ_CHECK(xfdAtEq(got, 0, "图片 (*.png)"));
    XQ_CHECK(xfdAtEq(got, 1, "文本 (*.txt)"));
    if (got) XClassDelete((XClass*)got);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedNameFilter(dlg),
                       "图片 (*.png)"));

    XClassDelete(dlg);
}

/* ==================== ④ fileMode / acceptMode / 选项往返 ==================== */

XQ_TEST(XFileDialog, modeAndOptionsRoundTrip)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);

    XFileDialog_setFileMode(dlg, XFileDialog_ExistingFile);
    XQ_EQ_I64(XFileDialog_fileMode(dlg), XFileDialog_ExistingFile);
    XFileDialog_setFileMode(dlg, XFileDialog_Directory);
    XQ_EQ_I64(XFileDialog_fileMode(dlg), XFileDialog_Directory);

    XFileDialog_setAcceptMode(dlg, XFileDialog_AcceptSave);
    XQ_EQ_I64(XFileDialog_acceptMode(dlg), XFileDialog_AcceptSave);

    /* 选项位值与 Qt::Option 一致（qfiledialog.h:43-56）。 */
    XFileDialog_setOption(dlg, XFileDialog_DontConfirmOverwrite, true);
    XQ_CHECK(XFileDialog_testOption(dlg,
                                    XFileDialog_DontConfirmOverwrite));
    XFileDialog_setOption(dlg, XFileDialog_ReadOnly, true);
    XQ_EQ_I64(XFileDialog_options(dlg),
              (XFileDialog_DontConfirmOverwrite | XFileDialog_ReadOnly));
    XFileDialog_setOption(dlg, XFileDialog_DontConfirmOverwrite, false);
    XQ_CHECK(!XFileDialog_testOption(dlg,
                                     XFileDialog_DontConfirmOverwrite));
    XQ_EQ_I64(XFileDialog_options(dlg), XFileDialog_ReadOnly);

    XClassDelete(dlg);
}

/* ==================== ⑤ selectFile 替换 + 路径补全（F7） ============== */

XQ_TEST(XFileDialog, selectFileReplacesAndResolves)
{
    XFileDialog* dlg = XFileDialog_create(NULL, 0);
    XString* a;
    XString* b;
    XStringList* files;
    XQ_REQUIRE(dlg != NULL);

    /* 目录 "doc"（相对）用于验证相对名补全口径：QDir::filePath 拼接
     * → "doc/<name>"（探针 F7 在 cwd="/home/..." 时得到绝对路径；
     * 本库 setDirectory 保留调用方给的相对目录，拼接口径一致）。 */
    {
        XString* d = XString_create_utf8("doc");
        XQ_REQUIRE(d != NULL);
        XFileDialog_setDirectory(dlg, d);
        XClassDelete((XClass*)d);
    }
    a = XString_create_utf8("a.txt");
    b = XString_create_utf8("b.txt");
    XQ_REQUIRE(a != NULL && b != NULL);
    XFileDialog_selectFile(dlg, a);
    XFileDialog_selectFile(dlg, b);
    XClassDelete((XClass*)a);
    XClassDelete((XClass*)b);

    /* 替换而非累积（qfiledialog.cpp:1088 clear + setText；探针 F7：
     * 两次 selectFile 后 selectedFiles()==1 条）。 */
    files = XFileDialog_selectedFiles(dlg);
    XQ_EQ_I64(xfdCount(files), 1);
    XQ_CHECK(xfdAtEq(files, 0, "doc/b.txt"));
    if (files) XClassDelete((XClass*)files);
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), "doc/b.txt"));

    /* 空串忽略（qfiledialog.cpp:1060 `if (filename.isEmpty()) return`）。 */
    {
        XString* empty = XString_create_utf8("");
        XQ_REQUIRE(empty != NULL);
        XFileDialog_selectFile(dlg, empty);
        XClassDelete((XClass*)empty);
    }
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), "doc/b.txt"));

    /* 绝对名原样保留，并把目录切到父目录（qfiledialog.cpp:1069-1075）。 */
    {
        XString* abs = XString_create_utf8("/tmp/xfd_abs.txt");
        XQ_REQUIRE(abs != NULL);
        XFileDialog_selectFile(dlg, abs);
        XClassDelete((XClass*)abs);
    }
    XQ_CHECK(xfdTakeEq(XFileDialog_selectedFile(dlg), "/tmp/xfd_abs.txt"));
#if XFILE_ON && XDIR_ON && XFILEINFO_ON
    XQ_CHECK(xfdTakeEq(XFileDialog_directory(dlg), "/tmp"));

    /* 无选择 + AnyFile → selectedFiles 回落 directory（探针 F0/F5）。 */
    if (dlg->m_selectedFiles)
        XStringList_clear_base((XContainer*)dlg->m_selectedFiles);
    files = XFileDialog_selectedFiles(dlg);
    XQ_EQ_I64(xfdCount(files), 1);
    XQ_CHECK(xfdAtEq(files, 0, "/tmp"));
    if (files) XClassDelete((XClass*)files);
#endif

    /* selectUrl 同样替换（Qt selectUrl→selectFile，qfiledialog.cpp:
     * 1099-1112）。 */
    {
        XString* u = XString_create_utf8("file:///tmp/x.c");
        XQ_REQUIRE(u != NULL);
        XFileDialog_selectUrl(dlg, u);
        XClassDelete((XClass*)u);
    }
    files = XFileDialog_selectedFiles(dlg);
    XQ_EQ_I64(xfdCount(files), 1);
    XQ_CHECK(xfdAtEq(files, 0, "file:///tmp/x.c"));
    if (files) XClassDelete((XClass*)files);

    XClassDelete(dlg);
}

/* ==================== ⑥ 父子所有权 ==================== */

XQ_TEST(XFileDialog, parentOwnership)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XFileDialog* dlg;
    XQ_REQUIRE(parent != NULL);
    dlg = XFileDialog_create(parent, 0);
    XQ_REQUIRE(dlg != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)dlg) == parent);
    XQ_CHECK(XClassGetVtable((XObject*)dlg) == XFileDialog_class_init());
    XQ_CHECK(XWidget_isWindow((XWidget*)dlg));
    XClassDelete(dlg);
    XClassDelete(parent);
}

#endif /* 模块开关 */
